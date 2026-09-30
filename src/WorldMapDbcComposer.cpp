#include "WorldMapDbcComposer.h"
#include "ContentBuildPaths.h"
#include "DbcDescriptor.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace
{
std::uint32_t Float(float value)
{
    static_assert(sizeof(float) == 4, "Unexpected float width");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

DbcDescriptor const& Table(std::string const& table)
{
    // This composer owns exactly the four native world-map tables. Anything else
    // is refused here rather than relying on a dimension mismatch later.
    static std::set<std::string> const owned = {"DungeonMap", "DungeonMapChunk",
        "WorldMapArea", "WorldMapTransforms"};
    if (!owned.count(table))
        throw std::runtime_error("Unsupported world-map DBC table: " + table);
    auto descriptor = FindDbcDescriptor(12340, table);
    if (!descriptor)
        throw std::runtime_error("Unsupported world-map DBC table: " + table);
    return *descriptor;
}

std::vector<ResolvedWorldMap> Ordered(std::vector<ResolvedWorldMap> const& maps)
{
    auto ordered = maps;
    // Package order comes from the package key, never from database row order,
    // so two builds of the same installed set compose identical bytes.
    std::stable_sort(ordered.begin(), ordered.end(), [](auto const& a, auto const& b) {
        return a.packageKey < b.packageKey;
    });
    return ordered;
}
}

void WorldMapDbcComposer::AppendRequests(std::string const& packageKey,
    std::vector<ContentWorldMap> const& maps,
    std::map<std::string, std::vector<ResourceAllocationRequest>>& out)
{
    // Symbols are derived from the manifest position, which is stable for a
    // fixed EPF and independent of database row order. Each authored row is
    // requested exactly once: a duplicate request for one row would be a
    // different symbol claiming an ID the package already holds, which PlanFixed
    // must keep rejecting rather than silently deduplicate.
    std::size_t mapIndex = 0;
    for (auto const& map : maps) {
        auto const prefix = "worldmap/" + std::to_string(mapIndex) + "/";
        // A map with no declared transform contributes no WorldMapTransforms row
        // at all: no request, no fixed ID, no lease, no composed record. Nothing
        // is synthesised for it, and NewDungeonMapID is never guessed.
        if (map.transform)
            out["WorldMapTransforms"].push_back({packageKey, prefix + "transform",
                ResourceKind("WorldMapTransforms"), map.transform->id});
        for (auto const& area : map.areas) {
            auto const areaPrefix = prefix + "area/" + std::to_string(area.id) + "/";
            out["WorldMapArea"].push_back({packageKey, prefix + "area/" +
                std::to_string(area.id), ResourceKind("WorldMapArea"), area.id});
            // A DungeonMap row belongs to the area that declares the floor, so
            // the floor is requested here and nowhere else.
            for (auto const& floor : area.floors)
                out["DungeonMap"].push_back({packageKey, areaPrefix + "floor/" +
                    std::to_string(floor.id), ResourceKind("DungeonMap"), floor.id});
            for (auto const& chunk : area.chunks)
                out["DungeonMapChunk"].push_back({packageKey, areaPrefix + "chunk/" +
                    std::to_string(chunk.id), ResourceKind("DungeonMapChunk"), chunk.id});
        }
        ++mapIndex;
    }
}

std::string const& WorldMapDbcComposer::VerifiedBaselineSha256(std::string const& table)
{
    static std::map<std::string, std::string> const pins = {
        {"DungeonMap", "aa31db35a2266694d8318c410712a18540d86469e3d6134eb648a1d9694f21f5"},
        {"DungeonMapChunk", "c9fda294ce565501518aa782957a5fe45d92147a79c34a5f29493194a8d318c3"},
        {"WorldMapArea", "90e1ec678c8226c76f4dd9f7904f05a4cb0a58c8b4d386719b24949bd55925bb"},
        {"WorldMapTransforms", "ba3f2a65c35ba0ab158f4a336c6195b0dcf4754d2e4f53fea5dbe5acf1ad479c"}};
    auto found = pins.find(table);
    if (found == pins.end())
        throw std::runtime_error("Unsupported world-map DBC table: " + table);
    return found->second;
}

std::set<std::uint32_t> WorldMapDbcComposer::Inspect(std::string const& table, DbcDocument const& baseline)
{
    auto const& descriptor = Table(table);
    if (baseline.fieldCount != descriptor.fields.size()
        || baseline.recordSize != descriptor.fields.size() * 4
        || baseline.words.size() != std::size_t(baseline.recordCount) * descriptor.fields.size()
        || baseline.strings.size() != baseline.stringBlockSize)
        throw std::runtime_error("Invalid " + table + " baseline dimensions");
    std::set<std::uint32_t> ids;
    for (std::size_t row = 0; row < baseline.recordCount; ++row)
    {
        auto id = baseline.words[row * baseline.fieldCount];
        if (!id || !ids.insert(id).second)
            throw std::runtime_error("Duplicate or zero stock " + table + " ID: " + std::to_string(id));
    }
    // Offset 0 always addresses the empty string, so the block must open with a
    // NUL. Appending a name can therefore never move an existing offset.
    if (baseline.strings.empty() || baseline.strings.front() != 0)
        throw std::runtime_error("Stock " + table
            + " string block must start with an empty string");
    return ids;
}

std::vector<std::uint32_t> WorldMapDbcComposer::FloorWords(std::uint32_t mapId, ContentDungeonMapFloor const& row)
{
    if (!mapId || !row.id || !row.floor)
        throw std::runtime_error("DungeonMap row requires a non-zero ID and Floor");
    return {row.id, mapId, row.floor, Float(row.field3), Float(row.field4), Float(row.field5),
        Float(row.field6), row.field7};
}

std::vector<std::uint32_t> WorldMapDbcComposer::ChunkWords(std::uint32_t mapId, ContentDungeonMapChunk const& row)
{
    if (!mapId || !row.id || !row.field2 || !row.dungeonMapId)
        throw std::runtime_error("DungeonMapChunk row requires a non-zero ID, field2 and DungeonMapID");
    return {row.id, mapId, row.field2, row.dungeonMapId, Float(row.field4)};
}

std::vector<std::uint32_t> WorldMapDbcComposer::AreaWords(std::uint32_t mapId,
    ContentWorldMapArea const& row, std::uint32_t internalNameOffset)
{
    if (!mapId || !row.id || row.internalName.empty())
        throw std::runtime_error("WorldMapArea row requires a non-zero ID and internal name");
    return {row.id, mapId, row.areaId, internalNameOffset, Float(row.y1), Float(row.y2),
        Float(row.x1), Float(row.x2), static_cast<std::uint32_t>(row.virtualMapId),
        static_cast<std::uint32_t>(row.dungeonMapId), row.parentMapId};
}

std::vector<std::uint32_t> WorldMapDbcComposer::TransformWords(std::uint32_t mapId,
    ContentWorldMapTransform const& row)
{
    if (!mapId || !row.id || !row.newMapId || !row.newDungeonMapId)
        throw std::runtime_error("WorldMapTransforms row requires a non-zero ID, NewMapID and NewDungeonMapID");
    return {row.id, mapId, Float(row.regionBottom), Float(row.regionRight), Float(row.regionTop),
        Float(row.regionLeft), row.newMapId, Float(row.regionOffsetX), Float(row.regionOffsetY),
        row.newDungeonMapId};
}

std::vector<std::uint32_t> WorldMapDbcComposer::Rows(std::string const& table, std::vector<ResolvedWorldMap> const& maps)
{
    Table(table); // Rejects unsupported tables before any row is read.
    std::vector<std::uint32_t> ids;
    for (auto const& map : Ordered(maps))
    {
        auto const& declaration = map.declaration;
        if (table == "DungeonMap")
            for (auto const& area : declaration.areas)
                for (auto const& floor : area.floors) ids.push_back(floor.id);
        else if (table == "DungeonMapChunk")
            for (auto const& area : declaration.areas)
                for (auto const& chunk : area.chunks) ids.push_back(chunk.id);
        else if (table == "WorldMapArea")
            for (auto const& area : declaration.areas) ids.push_back(area.id);
        else if (table == "WorldMapTransforms")
        {
            // A map without a declared transform contributes no row at all.
            if (declaration.transform) ids.push_back(declaration.transform->id);
        }
        else
            throw std::runtime_error("Unsupported world-map DBC table: " + table);
    }
    return ids;
}

std::vector<std::uint8_t> WorldMapDbcComposer::Compose(std::string const& table, DbcDocument const& baseline,
    std::vector<ResolvedWorldMap> const& maps)
{
    auto const& descriptor = Table(table);
    auto occupied = Inspect(table, baseline);
    std::uint32_t const fields = static_cast<std::uint32_t>(descriptor.fields.size());
    // Stock records keep their exact order and bytes; only the string block may
    // grow, and only by appending after every existing stock string.
    std::vector<std::uint8_t> strings = baseline.strings;
    std::map<std::string, std::uint32_t> interned;
    auto intern = [&](std::string const& text) {
        auto found = interned.find(text);
        if (found != interned.end()) return found->second;
        if (strings.size() + text.size() + 1 > 256ULL * 1024 * 1024)
            throw std::runtime_error(table + " string block too large");
        auto offset = static_cast<std::uint32_t>(strings.size());
        strings.insert(strings.end(), text.begin(), text.end());
        strings.push_back(0);
        interned.emplace(text, offset);
        return offset;
    };
    std::vector<std::vector<std::uint32_t>> additions;
    for (auto const& map : Ordered(maps))
    {
        auto const& declaration = map.declaration;
        if (!declaration.mapId)
            throw std::runtime_error(table + " contribution requires a non-zero MapID");
        if (table == "DungeonMap")
            for (auto const& area : declaration.areas)
                for (auto const& floor : area.floors)
                    additions.push_back(FloorWords(declaration.mapId, floor));
        else if (table == "DungeonMapChunk")
            for (auto const& area : declaration.areas)
                for (auto const& chunk : area.chunks)
                    additions.push_back(ChunkWords(declaration.mapId, chunk));
        else if (table == "WorldMapArea")
            for (auto const& area : declaration.areas)
                additions.push_back(AreaWords(declaration.mapId, area, intern(area.internalName)));
        else if (table == "WorldMapTransforms")
        {
            // A map without a declared transform composes no transform row, so
            // the verified stock file is reproduced exactly.
            if (declaration.transform)
                additions.push_back(TransformWords(declaration.mapId, *declaration.transform));
        }
        else
            throw std::runtime_error("Unsupported world-map DBC table: " + table);
    }
    for (auto const& words : additions)
    {
        if (words.size() != fields)
            throw std::runtime_error(table + " row width does not match the compiled descriptor");
        if (!occupied.insert(words[0]).second)
            throw std::runtime_error(table + " row ID " + std::to_string(words[0])
                + " already exists in the verified stock baseline or in this build");
    }
    auto result = baseline;
    for (auto const& words : additions) result.words.insert(result.words.end(), words.begin(), words.end());
    result.recordCount = static_cast<std::uint32_t>(baseline.recordCount + additions.size());
    if (table == "WorldMapArea")
    {
        result.strings = std::move(strings);
        result.stringBlockSize = static_cast<std::uint32_t>(result.strings.size());
    }
    auto bytes = DbcReader::Serialize(result);
    auto parsed = DbcReader::Parse(bytes, descriptor);
    if (!parsed.valid)
        throw std::runtime_error("Composed " + table + " failed reparse: " + parsed.error);
    auto const& check = parsed.document;
    if (check.recordCount != result.recordCount || check.fieldCount != fields
        || check.recordSize != fields * 4
        || !std::equal(baseline.words.begin(), baseline.words.end(), check.words.begin())
        || !std::equal(baseline.strings.begin(), baseline.strings.end(), check.strings.begin()))
        throw std::runtime_error("Composed " + table + " did not preserve the stock records or string block");
    // No silent stock-row modification: every stock record survives byte for byte
    // and every contributed record appears exactly once with the authored words.
    Inspect(table, check);
    for (auto const& words : additions)
    {
        std::size_t matches = 0;
        for (std::size_t row = baseline.recordCount; row < check.recordCount; ++row)
            if (std::equal(words.begin(), words.end(), check.words.begin() + row * fields))
                ++matches;
        if (matches != 1)
            throw std::runtime_error("Composed " + table
                + " does not carry its contributed row exactly once");
    }
    return bytes;
}

bool WorldMapDbcComposer::Stage(std::string const& table, std::vector<std::uint8_t> const& bytes,
    std::filesystem::path const& workspace, DbcDocument& reparsed, std::string& error)
{
    namespace fs = std::filesystem;
    using namespace ContentBuildPaths;
    try
    {
        auto const& descriptor = Table(table);
        Require(!workspace.empty(), "Build workspace must not be empty");
        RejectLinks(workspace);
        auto target = workspace / "DBFilesClient" / (table + ".dbc");
        RejectLinks(target);
        fs::create_directories(target.parent_path());
        Require(!fs::exists(fs::symlink_status(target)), "Composed " + table + " target already exists in workspace");
        {
            std::ofstream output(target, std::ios::binary | std::ios::trunc);
            Require(output.is_open(), "Cannot create composed " + table + ".dbc");
            output.write(reinterpret_cast<char const*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            Require(output.good(), "Cannot write composed " + table + ".dbc");
        }
        auto parsed = DbcReader::Read(target, descriptor);
        Require(parsed.valid, "Generated " + table + ".dbc failed read-back: " + parsed.error);
        Require(DbcReader::Serialize(parsed.document) == bytes, "Generated " + table + ".dbc read-back differs from composed bytes");
        reparsed = std::move(parsed.document);
        return true;
    }
    catch (std::exception const& exception)
    {
        error = exception.what();
        reparsed = DbcDocument{};
        return false;
    }
}
