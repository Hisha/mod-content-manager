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

std::string const& ContentFrameXml::NativePveLuaTarget()
{
    static std::string const target =
        "Interface/FrameXML/ContentManagerNativePve.lua";
    return target;
}

std::string const& ContentFrameXml::CollectionsLuaTarget()
{
    static std::string const target =
        "Interface/FrameXML/ContentManagerCollections.lua";
    return target;
}

std::string const& ContentFrameXml::CollectionsIconTarget()
{
    static std::string const target =
        "Interface/Buttons/ContentManagerCollections.tga";
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

bool ContentFrameXml::ComposeNativePveLua(
    std::vector<NativePvePage> const& pages, std::string& text,
    std::string& error)
{
    if (pages.empty())
    {
        error = "Native PvE composition requires at least one page";
        return false;
    }
    std::set<std::string> ids;
    for (auto const& page : pages)
    {
        if (page.id.empty() || page.name.empty() || page.frame.empty() ||
            !ids.insert(page.id).second)
        {
            error = "Native PvE pages require unique non-empty ids, names, and frames";
            return false;
        }
    }
    auto orderedPages = pages;
    auto fold = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) {
                return c >= 'A' && c <= 'Z'
                    ? static_cast<char>(c - 'A' + 'a')
                    : static_cast<char>(c);
            });
        return value;
    };
    std::sort(orderedPages.begin(), orderedPages.end(), [&](auto const& left,
                                                             auto const& right) {
        auto ln = fold(left.name), rn = fold(right.name);
        return ln == rn ? left.id < right.id : ln < rn;
    });

    std::string out =
        "-- Generated by mod-content-manager for WoW 3.3.5a build 12340.\n"
        "local registry, ordered, tabs = {}, {}, {}\n"
        "local selectedId, initialized = \"dungeon-finder\", false\n"
        "local function report(message)\n"
        " if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then DEFAULT_CHAT_FRAME:AddMessage(\"|cffff2020Player vs. Environment:|r \"..message) end\n"
        "end\n"
        "local function invoke(name, page)\n"
        " if not name or name==\"\" then return true end\n"
        " local callback=_G[name]\n"
        " if type(callback)~=\"function\" then report(\"page '\"..page.name..\"' is missing callback \"..name); return false end\n"
        " local ok,message=pcall(callback)\n"
        " if not ok then report(\"page '\"..page.name..\"' callback failed: \"..tostring(message)); return false end\n"
        " return true\n"
        "end\n"
        "local function portrait(visible)\n"
        " local regions={LFDParentFramePortraitTexture,LFDParentFramePortraitIcon}\n"
        " for _,region in ipairs(regions) do if region then if visible then region:Show() else region:Hide() end end end\n"
        "end\n"
        "local function hidePage(page)\n"
        " if not page then return end\n"
        " invoke(page.onHide,page)\n"
        " local frame=_G[page.frame]; if frame then frame:Hide() end\n"
        "end\n"
        "local function sortPages()\n"
        " ordered={} for _,page in pairs(registry) do table.insert(ordered,page) end\n"
        " table.sort(ordered,function(a,b) local an,bn=string.lower(a.name),string.lower(b.name); if an==bn then return a.id<b.id end return an<bn end)\n"
        "end\n"
        "function ContentManagerNativePve_SelectPage(id)\n"
        " if not LFDParentFrame or not LFDQueueFrame then report(\"Dungeon Finder controls are unavailable\"); return false end\n"
        " local previous=registry[selectedId]; if previous then hidePage(previous) end\n"
        " if id==\"dungeon-finder\" then selectedId=id; LFDQueueFrame:Show(); portrait(true); if PanelTemplates_SetTab then PanelTemplates_SetTab(LFDParentFrame,1) end; return true end\n"
        " local page=registry[id]; local frame=page and _G[page.frame]\n"
        " if not page or not frame then report(page and (\"page '\"..page.name..\"' is missing frame \"..page.frame) or (\"unknown page id \"..tostring(id))); selectedId=\"dungeon-finder\"; LFDQueueFrame:Show(); portrait(true); if PanelTemplates_SetTab then PanelTemplates_SetTab(LFDParentFrame,1) end; return false end\n"
        " selectedId=id; LFDQueueFrame:Hide(); portrait(false); frame:Show()\n"
        " if not invoke(page.onShow,page) then frame:Hide(); selectedId=\"dungeon-finder\"; LFDQueueFrame:Show(); portrait(true); if PanelTemplates_SetTab then PanelTemplates_SetTab(LFDParentFrame,1) end; return false end\n"
        " if PanelTemplates_SetTab then for index,candidate in ipairs(ordered) do if candidate.id==id then PanelTemplates_SetTab(LFDParentFrame,index+1); break end end end\n"
        " return true\n"
        "end\n"
        "local function rebuildTabs()\n"
        " if not LFDParentFrame then report(\"LFDParentFrame is unavailable\"); return end\n"
        " sortPages(); local count=#ordered+1\n"
        " for index=1,count do\n"
        "  local tab=_G[\"LFDParentFrameTab\"..index] or CreateFrame(\"Button\",\"LFDParentFrameTab\"..index,LFDParentFrame,\"CharacterFrameTabButtonTemplate\")\n"
        "  tabs[index]=tab; tab:SetID(index); tab:SetText(index==1 and \"Dungeon Finder\" or ordered[index-1].name); tab:ClearAllPoints()\n"
        "  if index==1 then tab:SetPoint(\"BOTTOMLEFT\",LFDParentFrame,\"BOTTOMLEFT\",18,-27) else tab:SetPoint(\"LEFT\",tabs[index-1],\"RIGHT\",-15,0) end\n"
        "  local pageId=index==1 and \"dungeon-finder\" or ordered[index-1].id; tab:SetScript(\"OnClick\",function() ContentManagerNativePve_SelectPage(pageId) end); tab:Show()\n"
        "  if PanelTemplates_TabResize then PanelTemplates_TabResize(tab,0) end\n"
        " end\n"
        " local extra=count+1 while _G[\"LFDParentFrameTab\"..extra] do _G[\"LFDParentFrameTab\"..extra]:Hide(); extra=extra+1 end\n"
        " LFDParentFrame.numTabs=count; if PanelTemplates_SetNumTabs then PanelTemplates_SetNumTabs(LFDParentFrame,count) end\n"
        "end\n"
        "function ContentManager_RegisterNativePvePage(page)\n"
        " if type(page)~=\"table\" or type(page.id)~=\"string\" or type(page.name)~=\"string\" or type(page.frame)~=\"string\" or page.id==\"\" or page.name==\"\" or page.frame==\"\" then report(\"invalid page registration\"); return false end\n"
        " if registry[page.id] then report(\"duplicate page id '\"..page.id..\"'\"); return false end\n"
        " registry[page.id]=page; if initialized then rebuildTabs() end; return true\n"
        "end\n";

    for (auto const& page : orderedPages)
    {
        out += "ContentManager_RegisterNativePvePage({id=" + LuaString(page.id) +
               ",name=" + LuaString(page.name) + ",frame=" + LuaString(page.frame);
        if (!page.onShow.empty()) out += ",onShow=" + LuaString(page.onShow);
        if (!page.onHide.empty()) out += ",onHide=" + LuaString(page.onHide);
        out += "})\n";
    }
    out +=
        "initialized=true; rebuildTabs(); ContentManagerNativePve_SelectPage(\"dungeon-finder\")\n"
        "if LFDMicroButton then\n"
        " local function tooltip(button) button.tooltipText=MicroButtonTooltipText(\"Player vs. Environment\",\"TOGGLELFGPARENT\") end\n"
        " tooltip(LFDMicroButton); if LFDMicroButton.HookScript then LFDMicroButton:HookScript(\"OnEvent\",function(button,event) if event==\"UPDATE_BINDINGS\" then tooltip(button) end end) end\n"
        "end\n"
        "if LFDParentFrame and LFDParentFrame.HookScript then\n"
        " LFDParentFrame:HookScript(\"OnShow\",function() rebuildTabs(); ContentManagerNativePve_SelectPage(selectedId) end)\n"
        " LFDParentFrame:HookScript(\"OnHide\",function() hidePage(registry[selectedId]) end)\n"
        "end\n"
        "if hooksecurefunc and type(LFDFrame_OnEvent)==\"function\" then hooksecurefunc(\"LFDFrame_OnEvent\",function(_,event) if event==\"LFG_OPEN_FROM_GOSSIP\" then ContentManagerNativePve_SelectPage(\"dungeon-finder\") end end) end\n";
    text = std::move(out);
    return true;
}

bool ContentFrameXml::ComposeCollectionsLua(
    std::vector<NativeCollectionPage> const& pages, std::string& text,
    std::string& error)
{
    if (pages.empty())
    {
        error = "Collections composition requires at least one page";
        return false;
    }
    std::set<std::string> ids;
    for (auto const& page : pages)
    {
        if (page.id.empty() || page.name.empty() || page.frame.empty() ||
            !ids.insert(page.id).second)
        {
            error = "Collections pages require unique non-empty ids, names, and frames";
            return false;
        }
    }
    auto orderedPages = pages;
    auto fold = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) {
                return c >= 'A' && c <= 'Z'
                    ? static_cast<char>(c - 'A' + 'a')
                    : static_cast<char>(c);
            });
        return value;
    };
    std::sort(orderedPages.begin(), orderedPages.end(), [&](auto const& left,
                                                             auto const& right) {
        if (left.order != right.order) return left.order < right.order;
        auto ln = fold(left.name), rn = fold(right.name);
        return ln == rn ? left.id < right.id : ln < rn;
    });

    std::string out =
        "-- Generated by mod-content-manager for WoW 3.3.5a build 12340.\n"
        "local registry, ordered, tabs = {}, {}, {}\n"
        "local selectedId, initialized, button, positionIcon\n"
        "local DEFAULT_WIDTH,DEFAULT_HEIGHT,DEFAULT_ORDER,CONTENT_TOP,CONTENT_BOTTOM=940,590,1000,32,0\n"
        "local host=CreateFrame(\"Frame\",\"ContentManagerCollectionsFrame\",UIParent)\n"
        "host:SetWidth(DEFAULT_WIDTH); host:SetHeight(DEFAULT_HEIGHT); host:SetPoint(\"CENTER\"); host:SetFrameStrata(\"DIALOG\"); host:SetToplevel(true); host:SetMovable(true); host:EnableMouse(true); host:Hide()\n"
        "host:SetBackdrop({bgFile=\"Interface\\\\DialogFrame\\\\UI-DialogBox-Background-Dark\",edgeFile=\"Interface\\\\DialogFrame\\\\UI-DialogBox-Gold-Border\",tile=true,tileSize=32,edgeSize=32,insets={left=11,right=12,top=12,bottom=11}})\n"
        "local titlePlate=host:CreateTexture(nil,\"BORDER\"); titlePlate:SetTexture(\"Interface\\\\DialogFrame\\\\UI-DialogBox-Header\"); titlePlate:SetWidth(420); titlePlate:SetHeight(64); titlePlate:SetPoint(\"TOP\",host,\"TOP\",0,12)\n"
        "local title=host:CreateFontString(nil,\"ARTWORK\",\"GameFontNormalLarge\"); title:SetPoint(\"TOP\",0,-10); title:SetText(\"Collections\"); host.TitleText=title\n"
        "local emblem=CreateFrame(\"Frame\",\"ContentManagerCollectionsEmblem\",host); emblem:SetWidth(72); emblem:SetHeight(72); emblem:SetPoint(\"TOPLEFT\",host,\"TOPLEFT\",-12,8); emblem:SetFrameLevel(host:GetFrameLevel()+5)\n"
        "local emblemIcon=emblem:CreateTexture(nil,\"ARTWORK\"); emblemIcon:SetAllPoints(emblem); emblemIcon:SetTexture(\"Interface\\\\Buttons\\\\ContentManagerCollections\")\n"
        "host.CollectionsIcon=emblem\n"
        "local close=CreateFrame(\"Button\",\"ContentManagerCollectionsCloseButton\",host,\"UIPanelCloseButton\"); close:SetPoint(\"TOPRIGHT\",-5,-5); close:SetScript(\"OnClick\",function() host:Hide() end)\n"
        "UISpecialFrames[#UISpecialFrames+1]=host:GetName()\n"
        "local function report(message) if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then DEFAULT_CHAT_FRAME:AddMessage(\"|cffff2020Collections:|r \"..message) end end\n"
        "local function invoke(name,page) if not name or name==\"\" then return true end local callback=_G[name]; if type(callback)~=\"function\" then report(\"page '\"..page.name..\"' is missing callback \"..name); return false end local ok,message=pcall(callback); if not ok then report(\"page '\"..page.name..\"' callback failed: \"..tostring(message)); return false end return true end\n"
        "local function hidePage(page) if not page then return end invoke(page.onHide,page); local frame=_G[page.frame]; if frame then frame:Hide() end end\n"
        "local function sortPages() ordered={} for _,page in pairs(registry) do table.insert(ordered,page) end table.sort(ordered,function(a,b) if a.order~=b.order then return a.order<b.order end local an,bn=string.lower(a.name),string.lower(b.name); if an==bn then return a.id<b.id end return an<bn end) end\n"
        "local function layoutPage(page,frame) host:SetWidth(page.width or DEFAULT_WIDTH); host:SetHeight(page.height or DEFAULT_HEIGHT); host:ClearAllPoints(); host:SetPoint(\"CENTER\"); title:SetText(page.title and page.title~=\"\" and page.title or page.name); frame:ClearAllPoints(); frame:SetPoint(\"TOPLEFT\",host,\"TOPLEFT\",0,-CONTENT_TOP); frame:SetPoint(\"BOTTOMRIGHT\",host,\"BOTTOMRIGHT\",0,CONTENT_BOTTOM) end\n"
        "local function activatePage(page) local frame=page and _G[page.frame]; if not frame then if page then report(\"page '\"..page.name..\"' is missing frame \"..page.frame) end return false end if selectedId~=page.id then hidePage(registry[selectedId]) end; selectedId=nil; layoutPage(page,frame); frame:Show(); if not invoke(page.onShow,page) then frame:Hide(); return false end; selectedId=page.id; for index,candidate in ipairs(ordered) do if candidate.id==page.id and PanelTemplates_SetTab then PanelTemplates_SetTab(host,index) end end; return true end\n"
        "function ContentManagerCollections_SelectPage(id) local requested=registry[id]; if not requested then report(\"unknown page id \"..tostring(id)) elseif activatePage(requested) then return true end; for _,candidate in ipairs(ordered) do if candidate~=requested and activatePage(candidate) then return true end end; selectedId=nil; title:SetText(\"Collections\"); return false end\n"
        "local function rebuildTabs() sortPages(); local count=#ordered; for index,page in ipairs(ordered) do local tab=tabs[index] or CreateFrame(\"Button\",\"ContentManagerCollectionsFrameTab\"..index,host,\"CharacterFrameTabButtonTemplate\"); tabs[index]=tab; tab:SetID(index); tab:SetText(page.name); tab:SetHeight(32); tab:SetFrameLevel(host:GetFrameLevel()+3); tab:ClearAllPoints(); if index==1 then tab:SetPoint(\"BOTTOMLEFT\",host,\"BOTTOMLEFT\",22,-26) else tab:SetPoint(\"LEFT\",tabs[index-1],\"RIGHT\",-15,0) end local id=page.id; tab:SetScript(\"OnClick\",function() ContentManagerCollections_SelectPage(id) end); tab:Show(); if PanelTemplates_TabResize then PanelTemplates_TabResize(tab,0) end end for index=count+1,#tabs do tabs[index]:Hide() end host.numTabs=count; if PanelTemplates_SetNumTabs then PanelTemplates_SetNumTabs(host,count) end end\n"
        "function ContentManager_RegisterCollectionPage(page) if type(page)~=\"table\" or type(page.id)~=\"string\" or type(page.name)~=\"string\" or type(page.frame)~=\"string\" or (page.order~=nil and type(page.order)~=\"number\") or page.id==\"\" or page.name==\"\" or page.frame==\"\" or (page.title~=nil and type(page.title)~=\"string\") or (page.width~=nil and (type(page.width)~=\"number\" or page.width<320 or page.width>1600)) or (page.height~=nil and (type(page.height)~=\"number\" or page.height<240 or page.height>1200)) then report(\"invalid page registration\"); return false end if registry[page.id] then report(\"duplicate page id '\"..page.id..\"'\"); return false end if page.order==nil then page.order=DEFAULT_ORDER end; registry[page.id]=page; if initialized then rebuildTabs() end; return true end\n";

    for (auto const& page : orderedPages)
    {
        out += "ContentManager_RegisterCollectionPage({id=" + LuaString(page.id) +
               ",name=" + LuaString(page.name) + ",order=" + Number(page.order) +
               ",frame=" + LuaString(page.frame);
        if (!page.onShow.empty()) out += ",onShow=" + LuaString(page.onShow);
        if (!page.onHide.empty()) out += ",onHide=" + LuaString(page.onHide);
        if (!page.title.empty()) out += ",title=" + LuaString(page.title);
        if (page.width != 0) out += ",width=" + Number(page.width);
        if (page.height != 0) out += ",height=" + Number(page.height);
        out += "})\n";
    }
    out +=
        "initialized=true; rebuildTabs()\n"
        "host:SetScript(\"OnShow\",function() rebuildTabs(); if not ContentManagerCollections_SelectPage(selectedId or (ordered[1] and ordered[1].id)) then host:Hide(); return end; if button then button:SetButtonState(\"PUSHED\",true); positionIcon(true) end end)\n"
        "host:SetScript(\"OnHide\",function() hidePage(registry[selectedId]); if button then button:SetButtonState(\"NORMAL\"); positionIcon(false) end end)\n"
        "local parent=MainMenuBarArtFrame or UIParent\n"
        "button=CreateFrame(\"Button\",\"ContentManagerCollectionsMicroButton\",parent)\n"
        "button:SetWidth(28); button:SetHeight(58); button:SetHitRectInsets(0,0,18,0); button:SetFrameStrata(\"MEDIUM\"); button.tooltipText=\"Collections\"\n"
        "button:SetNormalTexture(\"Interface\\\\Buttons\\\\UI-MicroButtonCharacter-Up\")\n"
        "button:SetPushedTexture(\"Interface\\\\Buttons\\\\UI-MicroButtonCharacter-Down\")\n"
        "button:SetHighlightTexture(\"Interface\\\\Buttons\\\\UI-MicroButton-Hilight\",\"ADD\")\n"
        // The stock Character button supplies clean Blizzard chrome and renders
        // its portrait separately in a y=5..30 aperture.  A 20-pixel chest at
        // bottom=7 is therefore centered in the visible aperture, not in the
        // otherwise transparent upper portion of the 28x58 button texture.
        "local icon=button:CreateTexture(nil,\"OVERLAY\"); icon:SetWidth(20); icon:SetHeight(20); icon:SetPoint(\"BOTTOM\",button,\"BOTTOM\",0,7); icon:SetTexture(\"Interface\\\\Buttons\\\\ContentManagerCollections\")\n"
        "positionIcon=function(pressed) icon:ClearAllPoints(); icon:SetPoint(\"BOTTOM\",button,\"BOTTOM\",pressed and 1 or 0,pressed and 6 or 7) end\n"
        "button:SetScript(\"OnMouseDown\",function() positionIcon(true) end)\n"
        "button:SetScript(\"OnMouseUp\",function() positionIcon(host:IsShown()) end)\n"
        "button:SetScript(\"OnEnter\",function(self) GameTooltip:SetOwner(self,\"ANCHOR_RIGHT\"); GameTooltip:SetText(\"Collections\",1,1,1); GameTooltip:Show() end)\n"
        "button:SetScript(\"OnLeave\",function() GameTooltip:Hide() end)\n"
        "button:SetScript(\"OnClick\",function() if host:IsShown() then host:Hide() else host:Show() end end)\n"
        "local function layout() if button:GetParent()~=parent then return end if not LFDMicroButton then button:Hide(); return end button:Show(); button:ClearAllPoints(); button:SetPoint(\"LEFT\",LFDMicroButton,\"RIGHT\",-3,0); if MainMenuMicroButton then MainMenuMicroButton:ClearAllPoints(); MainMenuMicroButton:SetPoint(\"LEFT\",button,\"RIGHT\",-3,0) end end\n"
        "layout(); if hooksecurefunc then if type(UpdateMicroButtons)==\"function\" then hooksecurefunc(\"UpdateMicroButtons\",layout) end if type(MoveMicroButtons)==\"function\" then hooksecurefunc(\"MoveMicroButtons\",layout) end end\n";
    text = std::move(out);
    return true;
}

std::vector<std::uint8_t> ContentFrameXml::ComposeCollectionsIcon()
{
    // Original Content Manager artwork.  It is deliberately generated here so
    // the emblem remains deterministic, package-owned, and independent of any
    // gameplay contributor.  Coordinates are doubled around a half-pixel
    // center to keep the circular silhouette exactly symmetric.
    constexpr int width = 128, height = 128;
    std::vector<std::uint8_t> bytes(18 + width * height * 4, 0);
    bytes[2] = 2; // uncompressed true-color
    bytes[12] = width & 0xff; bytes[13] = width >> 8;
    bytes[14] = height & 0xff; bytes[15] = height >> 8;
    bytes[16] = 32; bytes[17] = 0x28; // eight alpha bits, top-left origin
    auto pixel = [&](int x, int y, std::uint8_t r, std::uint8_t g,
                     std::uint8_t b, std::uint8_t a) {
        auto offset = 18 + static_cast<std::size_t>(y * width + x) * 4;
        bytes[offset] = b; bytes[offset + 1] = g;
        bytes[offset + 2] = r; bytes[offset + 3] = a;
    };
    auto circle = [](int x, int y, int cx, int cy, int radius) {
        int dx = x - cx, dy = y - cy;
        return dx * dx + dy * dy <= radius * radius;
    };
    auto diagonal = [](int x, int y, int x1, int y1, int x2, int y2,
                       int thickness) {
        int vx = x2 - x1, vy = y2 - y1;
        int wx = x - x1, wy = y - y1;
        int dot = wx * vx + wy * vy;
        int length2 = vx * vx + vy * vy;
        if (dot < 0 || dot > length2) return false;
        int cross = wx * vy - wy * vx;
        return cross * cross <= thickness * thickness * length2;
    };

    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            int dx = 2 * x - 127, dy = 2 * y - 127;
            int radius2 = dx * dx + dy * dy;
            if (radius2 > 122 * 122) continue;

            // A dark outline, raised antique-gold rim, inset shadow, and deep
            // amethyst field give the medallion its WotLK-era painted depth.
            if (radius2 > 116 * 116)
                pixel(x, y, 38, 24, 16, 255);
            else if (radius2 > 106 * 106)
            {
                int light = (127 - dy) / 10;
                pixel(x, y, static_cast<std::uint8_t>(166 + light),
                      static_cast<std::uint8_t>(105 + light / 2), 28, 255);
            }
            else if (radius2 > 98 * 98)
                pixel(x, y, 47, 24, 49, 255);
            else
            {
                int glow = (127 - y) / 5;
                pixel(x, y, static_cast<std::uint8_t>(73 + glow / 2),
                      static_cast<std::uint8_t>(19 + glow / 5),
                      static_cast<std::uint8_t>(105 + glow), 255);
            }
        }

    // Eight small brass studs break up the rim and remain legible when the
    // same generated texture is reduced for the micro-menu button.
    constexpr int studs[][2] = {{64,8},{99,22},{120,63},{105,99},
                                 {64,120},{22,105},{8,64},{22,22}};
    for (auto const& stud : studs)
        for (int y = stud[1] - 3; y <= stud[1] + 3; ++y)
            for (int x = stud[0] - 3; x <= stud[0] + 3; ++x)
                if (circle(x, y, stud[0], stud[1], 3))
                    pixel(x, y, 229, 180, 70, 255);

    // An original wardrobe crest: a gold hanger/shoulder line above a pale
    // split mantle.  The lavender shadow and central amethyst gem tie it to
    // the field without reproducing artwork from the retail reference.
    for (int y = 35; y <= 42; ++y)
        for (int x = 35; x <= 92; ++x)
            if (diagonal(x, y, 36, 41, 53, 34, 3) ||
                diagonal(x, y, 53, 34, 64, 31, 3) ||
                diagonal(x, y, 64, 31, 75, 34, 3) ||
                diagonal(x, y, 75, 34, 92, 41, 3))
                pixel(x, y, 222, 165, 58, 255);
    for (int y = 40; y <= 99; ++y)
        for (int x = 32; x <= 95; ++x)
        {
            bool leftMantle = diagonal(x, y, 38, 42, 63, 69, 8) ||
                              diagonal(x, y, 63, 69, 49, 96, 9);
            bool rightMantle = diagonal(x, y, 90, 42, 65, 69, 8) ||
                               diagonal(x, y, 65, 69, 79, 96, 9);
            if (leftMantle || rightMantle)
                pixel(x, y, y < 68 ? 222 : 174, y < 68 ? 190 : 118,
                      y < 68 ? 236 : 204, 255);
        }
    for (int y = 58; y <= 76; ++y)
        for (int x = 55; x <= 73; ++x)
            if (circle(x, y, 64, 67, 9))
                pixel(x, y, y < 64 ? 203 : 130, 79, y < 64 ? 241 : 191, 255);
    for (int y = 60; y <= 64; ++y)
        for (int x = 60; x <= 64; ++x)
            if (circle(x, y, 62, 62, 2)) pixel(x, y, 245, 220, 255, 255);
    return bytes;
}

bool ContentFrameXml::ComposeToc(std::vector<std::uint8_t> const& stock,
                                 std::vector<LoadEntry> const& entries,
                                 ResourceSelection const& resources,
                                 std::string& text,
                                 std::string& error)
{
    bool const includeGeneratedLua =
        resources.count(GeneratedResource::FloorNames) != 0;
    bool const includeNativePve =
        resources.count(GeneratedResource::NativePve) != 0;
    bool const includeCollections =
        resources.count(GeneratedResource::CollectionsController) != 0;
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
        if ((includeGeneratedLua && key == ContentBuildPaths::Fold(GeneratedLuaTarget())) ||
            (includeNativePve && key == ContentBuildPaths::Fold(NativePveLuaTarget())) ||
            (includeCollections && key == ContentBuildPaths::Fold(CollectionsLuaTarget())))
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

    if (includeNativePve)
    {
        std::string target, relative, anchor, anchorRelative;
        if (!FrameXmlTarget(NativePveLuaTarget(), target, relative, error) ||
            !FrameXmlTarget("Interface/FrameXML/LFDFrame.xml", anchor,
                            anchorRelative, error))
            return false;
        auto key = ContentBuildPaths::Fold(target);
        if (!additions.emplace(key, Node{target, relative,
                ContentBuildPaths::Fold(anchor)}).second)
        {
            error = "Native PvE generated target collides with a load entry";
            return false;
        }
    }

    if (includeCollections)
    {
        std::string target, relative, anchor, anchorRelative;
        if (!FrameXmlTarget(CollectionsLuaTarget(), target, relative, error) ||
            !FrameXmlTarget("Interface/FrameXML/LFDFrame.xml", anchor,
                            anchorRelative, error))
            return false;
        auto key = ContentBuildPaths::Fold(target);
        if (!additions.emplace(key, Node{target, relative,
                ContentBuildPaths::Fold(anchor)}).second)
        {
            error = "Collections generated target collides with a load entry";
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
                                 std::vector<LoadEntry> const& entries,
                                 bool includeGeneratedLua, std::string& text,
                                 std::string& error)
{
    ResourceSelection resources;
    if (includeGeneratedLua) resources.insert(GeneratedResource::FloorNames);
    return ComposeToc(stock, entries, resources, text, error);
}

bool ContentFrameXml::ComposeToc(std::vector<std::uint8_t> const& stock,
                                 std::string& text, std::string& error)
{
    return ComposeToc(stock, {}, {GeneratedResource::FloorNames}, text, error);
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
    return Stage(target, std::vector<std::uint8_t>(text.begin(), text.end()),
                 workspace, error);
}

bool ContentFrameXml::Stage(std::string const& target,
                            std::vector<std::uint8_t> const& bytes,
                            std::filesystem::path const& workspace,
                            std::string& error)
{
    namespace fs = std::filesystem;
    using namespace ContentBuildPaths;
    try
    {
        Require(!workspace.empty(), "Build workspace must not be empty");
        Require(!bytes.empty(), "Generated FrameXML content must not be empty");
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
            output.write(reinterpret_cast<char const*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            Require(output.good(), "Cannot write generated " + safe);
        }
        std::ifstream input(destination, std::ios::binary);
        Require(input.is_open(), "Cannot read back generated " + safe);
        std::vector<std::uint8_t> readBack(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        Require(input.good() || input.eof(), "Cannot read generated " + safe);
        Require(readBack == bytes,
                "Generated " + safe + " read-back differs from composed text");
        return true;
    }
    catch (std::exception const& exception)
    {
        error = exception.what();
        return false;
    }
}

std::vector<std::string> ContentFrameXml::Targets(
    ResourceSelection const& resources)
{
    std::vector<std::string> targets = {StockTocTarget()};
    if (resources.count(GeneratedResource::FloorNames))
        targets.push_back(GeneratedLuaTarget());
    if (resources.count(GeneratedResource::NativePve))
        targets.push_back(NativePveLuaTarget());
    if (resources.count(GeneratedResource::CollectionsController))
        targets.push_back(CollectionsLuaTarget());
    if (resources.count(GeneratedResource::CollectionsIcon))
        targets.push_back(CollectionsIconTarget());
    std::sort(targets.begin(), targets.end());
    return targets;
}

std::vector<std::string> ContentFrameXml::AllTargets()
{
    return Targets({GeneratedResource::FloorNames,
                    GeneratedResource::NativePve,
                    GeneratedResource::CollectionsController,
                    GeneratedResource::CollectionsIcon});
}
