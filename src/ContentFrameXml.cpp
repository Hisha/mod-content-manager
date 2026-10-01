#include "ContentFrameXml.h"
#include "ContentBuildHash.h"
#include "ContentBuildPaths.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <fstream>

namespace
{
// One Lua 5.1 short string literal. Backslash, quote and the C0 controls are
// escaped; every other byte, including UTF-8 continuation bytes, is emitted
// literally the way the client's own GlobalStrings.lua already carries them.
std::string LuaString(std::string const& value)
{
    std::string out = "\"";
    for (unsigned char c : value)
    {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 32 || c == 127)
        {
            char escaped[5];
            std::snprintf(escaped, sizeof escaped, "\\%03u", static_cast<unsigned>(c));
            out += escaped;
        }
        else out += static_cast<char>(c);
    }
    out += '"';
    return out;
}

std::string Number(std::uint32_t value)
{
    return std::to_string(value);
}

std::string const FrameXmlPrefix = "Interface/FrameXML/";

bool FrameXmlTarget(std::string const& value, std::string& canonical,
                    std::string& relative, std::string& error)
{
    try
    {
        canonical = ContentBuildPaths::Target(value);
    }
    catch (std::exception const& exception)
    {
        error = exception.what();
        return false;
    }
    auto folded = ContentBuildPaths::Fold(canonical);
    auto prefix = ContentBuildPaths::Fold(FrameXmlPrefix);
    if (folded.compare(0, prefix.size(), prefix) != 0 ||
        canonical.size() <= FrameXmlPrefix.size())
    {
        error = "FrameXML load path must be a file below " + FrameXmlPrefix +
                ": " + value;
        return false;
    }
    relative = canonical.substr(FrameXmlPrefix.size());
    return true;
}
}

std::string const& ContentFrameXml::GeneratedLuaTarget()
{
    static std::string const target =
        "Interface/FrameXML/ContentManagerWorldMapFloorNames.lua";
    return target;
}

std::string const& ContentFrameXml::StockTocTarget()
{
    static std::string const target = "Interface/FrameXML/FrameXML.toc";
    return target;
}

std::string const& ContentFrameXml::TocInsertionMarker()
{
    static std::string const marker = "## add new modules above here";
    return marker;
}

std::string const& ContentFrameXml::VerifiedStockTocSha256()
{
    // Verified stock 3.3.5a build-12340 Interface/FrameXML/FrameXML.toc as the
    // client actually resolves it: the highest-precedence archive that carries
    // the member is enUS/patch-enUS-3.MPQ (2820 bytes, "## Interface: 30300",
    // CRLF, trailing newline).  Base locale archives also carry an older
    // 30000-era copy, but they lose to the patch archives, so pinning one of
    // those would ship a regressed load list.  The digest is pinned so a
    // generated TOC can only ever be this exact file plus one inserted line.
    static std::string const digest =
        "3158bea13225ae51137a389f0f3ab8566e94b6be84196dd2c1fda27024677754";
    return digest;
}

std::set<std::string> const& ContentFrameXml::SupportedLocales()
{
    // Exactly the locales GetLocale() can return on build 12340.
    static std::set<std::string> const locales = {
        "deDE", "enCN", "enGB", "enUS", "esES", "esMX", "frFR",
        "itIT", "koKR", "ptBR", "ruRU", "zhCN", "zhTW"};
    return locales;
}

bool ContentFrameXml::SupportedLocale(std::string const& locale)
{
    return SupportedLocales().count(locale) != 0;
}

bool ContentFrameXml::FloorNameToken(std::string const& internalName,
                                     std::string& token)
{
    token.clear();
    if (internalName.empty()) return false;
    for (unsigned char c : internalName)
    {
        // Non-ASCII would need the client's own Latin-1 fold to reproduce in
        // Lua, which is not something to reimplement here.
        if (c < 32 || c > 126) return false;
        token += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A')
                                        : static_cast<char>(c);
    }
    return !token.empty();
}

bool ContentFrameXml::Collect(std::vector<ContentWorldMap> const& worldMaps,
                              Declared& out, std::string& error)
{
    for (auto const& map : worldMaps)
        for (auto const& area : map.areas)
            for (auto const& entry : area.floorNames)
            {
                if (entry.first.empty() || entry.second.labels.empty())
                {
                    error = "worldMaps floorNames locales must be nonempty for "
                            "area " + std::to_string(area.id);
                    return false;
                }
                std::string token;
                if (!FloorNameToken(area.internalName, token))
                {
                    error = "worldMaps area " + std::to_string(area.id) +
                            " internalName '" + area.internalName +
                            "' cannot carry floor labels: it is outside the "
                            "ASCII range the generated client lookup matches. "
                            "An area without an ASCII internal name keeps the "
                            "stock floor label";
                    return false;
                }
                for (auto const& label : entry.second.labels)
                {
                    auto& levels = out[entry.first][token];
                    auto existing = levels.find(label.first);
                    if (existing != levels.end() && existing->second != label.second)
                    {
                        error = "worldMaps area " + std::to_string(area.id) +
                                " declares conflicting labels for locale '" +
                                entry.first + "' level " + Number(label.first) +
                                " of map token '" + token + "'";
                        return false;
                    }
                    levels[label.first] = label.second;
                }
            }
    return true;
}

bool ContentFrameXml::Merge(Declared const& package, std::string const& packageKey,
                            Declared& out, std::string& error)
{
    for (auto const& locale : package)
        for (auto const& map : locale.second)
            for (auto const& level : map.second)
            {
                auto& levels = out[locale.first][map.first];
                auto existing = levels.find(level.first);
                if (existing != levels.end() && existing->second != level.second)
                {
                    error = "Package '" + packageKey + "' declares a different "
                            "label for locale '" + locale.first + "' map '" +
                            map.first + "' level " + Number(level.first) +
                            " than the package that owns it";
                    return false;
                }
                levels[level.first] = level.second;
            }
    return true;
}

bool ContentFrameXml::ComposeLua(Declared const& floors, std::string& text,
                                 std::string& error)
{
    if (floors.empty())
    {
        error = "Cannot generate floor-name Lua with no declared floor names";
        return false;
    }
    for (auto const& locale : floors)
        if (locale.second.empty())
        {
            error = "Declared locale '" + locale.first + "' has no map labels";
            return false;
        }
    std::string out =
        "-- Generated by Content Manager from declared\n"
        "-- worldMaps[].areas[].floorNames. Do not edit.\n"
        "--\n"
        "-- This module is loaded from the stock FrameXML.toc after\n"
        "-- WorldMapFrame.xml and replaces exactly one stock function,\n"
        "-- WorldMapLevelDropDown_Initialize. Every other stock global is read\n"
        "-- from the file it already comes from; nothing here is reimplemented\n"
        "-- beyond that one function, and the stock function itself still runs\n"
        "-- whenever this module has no label to offer.\n"
        "CONTENT_MANAGER_DUNGEON_FLOOR_NAMES = {\n";
    for (auto const& locale : floors)
    {
        out += "    [" + LuaString(locale.first) + "] = {\n";
        for (auto const& map : locale.second)
        {
            out += "        [" + LuaString(map.first) + "] = {\n";
            for (auto const& level : map.second)
                out += "            [" + Number(level.first) + "] = " +
                       LuaString(level.second) + ",\n";
            out += "        },\n";
        }
        out += "    },\n";
    }
    out +=
        "}\n"
        "\n"
        "do\n"
        "    local floorNames = CONTENT_MANAGER_DUNGEON_FLOOR_NAMES\n"
        "    local stockInitialize = WorldMapLevelDropDown_Initialize\n"
        "    -- Maps with no custom table delegate to stock unchanged. For a map\n"
        "    -- with custom labels, the body below preserves the stock build-12340\n"
        "    -- initializer and only adds labels[i] ahead of its stock floorname\n"
        "    -- fallback. In particular, terrain-map floorNum remains i - 1.\n"
        "    WorldMapLevelDropDown_Initialize = function()\n"
        "        local labels = nil\n"
        "        local mapInfo = GetMapInfo()\n"
        "        if mapInfo then\n"
        "            labels = floorNames[GetLocale()]\n"
        "            if labels then\n"
        "                labels = labels[strupper(mapInfo)]\n"
        "            end\n"
        "        end\n"
        "        if not labels then\n"
        "            return stockInitialize()\n"
        "        end\n"
        "        local info = UIDropDownMenu_CreateInfo()\n"
        "        local level = GetCurrentMapDungeonLevel()\n"
        "        local mapname = strupper(mapInfo or \"\")\n"
        "        local usesTerrainMap = DungeonUsesTerrainMap()\n"
        "        for i = 1, GetNumDungeonMapLevels() do\n"
        "            local floorNum = i\n"
        "            if usesTerrainMap then\n"
        "                floorNum = i - 1\n"
        "            end\n"
        "            local floorname = _G[\"DUNGEON_FLOOR_\" .. mapname .. floorNum]\n"
        "            info.text = labels[i] or floorname or string.format(FLOOR_NUMBER, i)\n"
        "            info.func = WorldMapLevelButton_OnClick\n"
        "            info.checked = (i == level)\n"
        "            UIDropDownMenu_AddButton(info)\n"
        "        end\n"
        "    end\n"
        "end\n";
    text = std::move(out);
    return true;
}

bool ContentFrameXml::ComposeToc(std::vector<std::uint8_t> const& stock,
                                 std::vector<LoadEntry> const& entries,
                                 bool includeGeneratedLua, std::string& text,
                                 std::string& error)
{
    std::string const source(stock.begin(), stock.end());
    std::string const moduleName =
        GeneratedLuaTarget().substr(GeneratedLuaTarget().rfind('/') + 1);

    struct Node
    {
        std::string target;
        std::string relative;
        std::string anchor;
    };
    std::map<std::string, Node> additions;
    for (auto const& entry : entries)
    {
        std::string target, relative, anchor, anchorRelative;
        if (!FrameXmlTarget(entry.target, target, relative, error) ||
            !FrameXmlTarget(entry.after, anchor, anchorRelative, error))
            return false;
        auto key = ContentBuildPaths::Fold(target);
        if (!additions.emplace(key, Node{target, relative,
                ContentBuildPaths::Fold(anchor)}).second)
        {
            error = "Duplicate or case-alias FrameXML load entry: " + target;
            return false;
        }
        if (includeGeneratedLua &&
            key == ContentBuildPaths::Fold(GeneratedLuaTarget()))
        {
            error = "FrameXML load entry conflicts with generated target: " + target;
            return false;
        }
        if (key == ContentBuildPaths::Fold(StockTocTarget()))
        {
            error = "FrameXML load entry cannot own the composed TOC target";
            return false;
        }
    }

    // Index the pinned stock list by logical, case-insensitive MPQ path. The
    // original bytes stay untouched; this index is only for anchor resolution.
    std::map<std::string, std::vector<std::size_t>> stockAnchors;
    std::vector<std::string> lineKeys;
    for (std::size_t position = 0; position < source.size();)
    {
        auto newline = source.find('\n', position);
        auto stop = newline == std::string::npos ? source.size() : newline + 1;
        std::string line = source.substr(position, stop - position);
        position = stop;
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string key;
        if (!line.empty() && line.front() != '#')
        {
            std::string canonical, relative, parseError;
            if (!FrameXmlTarget(FrameXmlPrefix + line, canonical, relative,
                                parseError))
            {
                error = "Invalid stock FrameXML.toc load entry '" + line +
                        "': " + parseError;
                return false;
            }
            key = ContentBuildPaths::Fold(canonical);
            stockAnchors[key].push_back(lineKeys.size());
        }
        lineKeys.push_back(std::move(key));
    }

    for (auto const& addition : additions)
    {
        auto stockTarget = stockAnchors.find(addition.first);
        if (stockTarget != stockAnchors.end())
        {
            error = "FrameXML load entry already exists in stock TOC: " +
                    addition.second.target;
            return false;
        }
        auto stockAnchor = stockAnchors.find(addition.second.anchor);
        if (stockAnchor != stockAnchors.end() && stockAnchor->second.size() != 1)
        {
            error = "Ambiguous FrameXML ordering anchor for " +
                    addition.second.target;
            return false;
        }
        if (stockAnchor == stockAnchors.end() &&
            !additions.count(addition.second.anchor))
        {
            error = "Missing FrameXML ordering anchor for " +
                    addition.second.target;
            return false;
        }
    }

    // A contribution can anchor to another contribution. Detect cycles before
    // writing so no discovery/install ordering can affect the outcome.
    std::map<std::string, unsigned> state;
    std::function<bool(std::string const&)> visit = [&](std::string const& key) {
        if (state[key] == 2) return true;
        if (state[key] == 1)
        {
            error = "FrameXML load-entry ordering cycle at " +
                    additions.at(key).target;
            return false;
        }
        state[key] = 1;
        auto anchor = additions.at(key).anchor;
        if (additions.count(anchor) && !visit(anchor)) return false;
        state[key] = 2;
        return true;
    };
    for (auto const& addition : additions)
        if (!visit(addition.first)) return false;

    std::map<std::string, std::vector<std::string>> children;
    for (auto const& addition : additions)
        children[addition.second.anchor].push_back(addition.first);
    // `additions` is already keyed byte-wise by folded logical path, but keep
    // the ordering explicit at the emission boundary.
    for (auto& group : children)
        std::sort(group.second.begin(), group.second.end());

    std::function<void(std::string const&, std::string const&, std::string&)> emit =
        [&](std::string const& anchor, std::string const& ending,
            std::string& output) {
            for (auto const& key : children[anchor])
            {
                output += additions.at(key).relative + ending;
                emit(key, ending, output);
            }
        };

    std::string out;
    out.reserve(source.size() + moduleName.size() + entries.size() * 48 + 4);
    std::size_t insertions = 0;
    std::size_t lineIndex = 0;
    for (std::size_t position = 0; position < source.size();)
    {
        auto newline = source.find('\n', position);
        auto stop = newline == std::string::npos ? source.size() : newline + 1;
        std::string const raw = source.substr(position, stop - position);
        position = stop;
        std::string line = raw;
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == moduleName)
        {
            error = "Stock FrameXML.toc already loads " + moduleName;
            return false;
        }
        out += raw;
        std::string ending = raw.size() >= 2 &&
            raw.compare(raw.size() - 2, 2, "\r\n") == 0 ? "\r\n" :
            (!raw.empty() && raw.back() == '\n' ? "\n" : "");
        if (!lineKeys.at(lineIndex).empty() && children.count(lineKeys.at(lineIndex)))
        {
            if (ending.empty())
            {
                error = "FrameXML ordering anchor is not a terminated line: " + line;
                return false;
            }
            emit(lineKeys.at(lineIndex), ending, out);
        }
        ++lineIndex;
        if (!includeGeneratedLua || line != TocInsertionMarker()) continue;
        // Reuse this TOC's own line ending for the inserted entry rather than
        // normalizing the whole file, so the diff against stock stays one line.
        if (ending != "\r\n")
        {
            error = "Stock FrameXML.toc insertion marker is not a terminated "
                    "line";
            return false;
        }
        out += moduleName + "\r\n";
        ++insertions;
    }
    if (includeGeneratedLua && insertions != 1)
    {
        error = "Stock FrameXML.toc must contain exactly one '" +
                TocInsertionMarker() + "' line, found " +
                std::to_string(insertions);
        return false;
    }
    text = std::move(out);
    return true;
}

bool ContentFrameXml::ComposeToc(std::vector<std::uint8_t> const& stock,
                                 std::string& text, std::string& error)
{
    return ComposeToc(stock, {}, true, text, error);
}

bool ContentFrameXml::ValidateTargets(std::vector<LoadEntry> const& entries,
                                      std::set<std::string> const& foldedTargets,
                                      std::string& error)
{
    if (foldedTargets.count(ContentBuildPaths::Fold(StockTocTarget())))
    {
        error = "Raw " + StockTocTarget() +
                " conflicts with generated FrameXML content";
        return false;
    }
    for (auto const& entry : entries)
    {
        std::string target, relative;
        if (!FrameXmlTarget(entry.target, target, relative, error)) return false;
        if (!foldedTargets.count(ContentBuildPaths::Fold(target)))
        {
            error = "FrameXML load entry target is absent from the cumulative "
                    "managed build: " + target;
            return false;
        }
    }
    return true;
}

bool ContentFrameXml::Stage(std::string const& target, std::string const& text,
                            std::filesystem::path const& workspace,
                            std::string& error)
{
    namespace fs = std::filesystem;
    using namespace ContentBuildPaths;
    try
    {
        Require(!workspace.empty(), "Build workspace must not be empty");
        Require(!text.empty(), "Generated FrameXML content must not be empty");
        RejectLinks(workspace);
        auto safe = Target(target);
        auto destination = workspace / fs::path(safe).generic_string();
        RejectLinks(destination);
        fs::create_directories(destination.parent_path());
        Require(!fs::exists(fs::symlink_status(destination)),
                "Generated " + safe + " target already exists in workspace");
        {
            std::ofstream output(destination, std::ios::binary | std::ios::trunc);
            Require(output.is_open(), "Cannot create generated " + safe);
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            Require(output.good(), "Cannot write generated " + safe);
        }
        std::ifstream input(destination, std::ios::binary);
        Require(input.is_open(), "Cannot read back generated " + safe);
        std::string readBack((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
        Require(input.good() || input.eof(), "Cannot read generated " + safe);
        Require(readBack == text,
                "Generated " + safe + " read-back differs from composed text");
        return true;
    }
    catch (std::exception const& exception)
    {
        error = exception.what();
        return false;
    }
}

std::vector<std::string> ContentFrameXml::Targets(bool includeGeneratedLua)
{
    std::vector<std::string> targets = {StockTocTarget()};
    if (includeGeneratedLua) targets.insert(targets.begin(), GeneratedLuaTarget());
    return targets;
}
