// Declared world-map floor labels: semantic parsing, generated FrameXML, and
// the parity record of what was generated.
//
// The one stock artifact this feature reads is Interface/FrameXML/FrameXML.toc,
// so the first test pins the checked-in fixture to the digest the composer
// accepts. If that assertion ever fails, the pin moved without a review of the
// new stock bytes.
#include "ContentBuildHash.h"
#include "ContentClientRequirement.h"
#include "ContentFrameXml.h"
#include "ContentPackage.h"
#include "ContentServerBundle.h"
#include "third_party/json/json.hpp"
#include "third_party/miniz/miniz.h"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace
{
std::filesystem::path Fixture()
{
    return std::filesystem::path(__FILE__).parent_path() / "fixtures" / "framexml" /
        "FrameXML.toc";
}

std::vector<std::uint8_t> StockToc()
{
    std::ifstream input(Fixture(), std::ios::binary);
    assert(input.is_open());
    std::string bytes((std::istreambuf_iterator<char>(input)),
                      std::istreambuf_iterator<char>());
    return {bytes.begin(), bytes.end()};
}

// One area of Karazhan-shaped declarations, plus the minimal map that owns it.
json Map(json floorNames)
{
    json floor = {{"id", 100}, {"floor", 1}, {"field3", 0.0}, {"field4", 0.0},
                  {"field5", 0.0}, {"field6", 0.0}, {"field7", 0}};
    json area = {{"id", 1},
                 {"areaId", 1},
                 {"internalName", "Karazhan"},
                 {"y1", -1.0}, {"y2", 1.0}, {"x1", -1.0}, {"x2", 1.0},
                 {"virtualMapId", -1}, {"dungeonMapId", 0}, {"parentMapId", 0},
                 {"floors", json::array({floor})},
                 {"chunks", json::array()},
                 {"floorNames", std::move(floorNames)}};
    return json::array({json{{"mapId", 532},
                             {"areas", json::array({area})}}});
}

json ClientFrameXml(json loadEntries = json::array())
{
    json value = {{"stockTocSource", "upstream/FrameXML.toc"},
        {"stockTocSha256", ContentFrameXml::VerifiedStockTocSha256()}};
    if (!loadEntries.empty()) value["loadEntries"] = std::move(loadEntries);
    return value;
}

void Save(std::filesystem::path const& path, json const& manifest)
{
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, path.string().c_str(), 0))
        throw std::runtime_error("zip init failed");
    auto body = manifest.dump();
    auto toc = StockToc();
    if (!mz_zip_writer_add_mem(&zip, "manifest.json", body.data(), body.size(),
                               MZ_BEST_COMPRESSION) ||
        !mz_zip_writer_add_mem(&zip, "upstream/FrameXML.toc",
                               reinterpret_cast<char const*>(toc.data()), toc.size(),
                               MZ_BEST_COMPRESSION) ||
        !mz_zip_writer_add_mem(&zip, "assets/readme.txt", "ok", 2,
                               MZ_BEST_COMPRESSION) ||
        !mz_zip_writer_add_mem(&zip, "assets/NativeHuntsFrame.xml", "<Ui/>", 5,
                               MZ_BEST_COMPRESSION) ||
        !mz_zip_writer_add_mem(&zip, "assets/NativeHuntsFrame.lua", "-- lua", 6,
                               MZ_BEST_COMPRESSION) ||
        !mz_zip_writer_finalize_archive(&zip))
        throw std::runtime_error("zip write failed");
    mz_zip_writer_end(&zip);
}

json Manifest(json floorNames, bool withFrameXml = true)
{
    json manifest = {{"schema", 3},
        {"package", "floors-a"},
        {"name", "Floors A"},
        {"version", "1.0.0"},
        {"description", "instance floor labels"},
        {"content", json::array()},
        {"worldMaps", Map(std::move(floorNames))}};
    if (withFrameXml) manifest["clientFrameXml"] = ClientFrameXml();
    return manifest;
}

json Labels()
{
    return json{{"enUS", {{"1", "Servant's Quarters"},
                          {"2", "Upper Livery Stables"}}},
                {"deDE", {{"1", "Dienerquartier"}}}};
}

json NativeHuntsManifest()
{
    return {{"schema", 3}, {"package", "native-hunts"},
        {"name", "Native Hunts"}, {"version", "1"},
        {"content", json::array({
            {{"type", "file"}, {"source", "assets/NativeHuntsFrame.xml"},
             {"target", "Interface/FrameXML/NativeHuntsFrame.xml"}},
            {{"type", "file"}, {"source", "assets/NativeHuntsFrame.lua"},
             {"target", "Interface/FrameXML/NativeHuntsFrame.lua"}}})},
        {"clientFrameXml", ClientFrameXml(json::array({
            {{"target", "Interface/FrameXML/NativeHuntsFrame.xml"},
             {"after", ContentFrameXml::NativePveLuaTarget()}}}))},
        {"nativePvePages", json::array({json{
            {"id", "hunts"}, {"name", "Hunts"},
            {"frame", "NativeHuntsFrame"},
            {"loadTarget", "Interface/FrameXML/NativeHuntsFrame.xml"},
            {"onShow", "NativeHuntsFrame_OnPveShow"}}})}};
}

json NativeCollectionsManifest()
{
    return {{"schema", 3}, {"package", "native-transmog"},
        {"name", "Native Transmog"}, {"version", "1"},
        {"content", json::array({
            {{"type", "file"}, {"source", "assets/NativeHuntsFrame.xml"},
             {"target", "Interface/FrameXML/NativeTransmogFrame.xml"}}})},
        {"clientFrameXml", ClientFrameXml(json::array({
            {{"target", "Interface/FrameXML/NativeTransmogFrame.xml"},
             {"after", ContentFrameXml::CollectionsLuaTarget()}}}))},
        {"nativeCollectionPages", json::array({json{
            {"id", "appearances"}, {"name", "Appearances"}, {"order", 100},
            {"title", "Appearances"}, {"width", 704}, {"height", 616},
            {"frame", "NativeTransmogCollectionsFrame"},
            {"loadTarget", "Interface/FrameXML/NativeTransmogFrame.xml"},
            {"onShow", "NativeTransmog_OpenCollections"},
            {"onHide", "NativeTransmog_CloseCollections"}}})}};
}

// The parity writer and verifier take their whole context positionally; these
// helpers pin the only thing under test here, the generated-FrameXML map.
std::string Parity(std::map<std::string, std::string> const& frameXmlHashes)
{
    std::string realm = "r", baseline, item, clientMpq, server;
    std::string currency, category, extendedCost, spellSha;
    return ContentServerBundle::ParityJson(realm, 12340,
        std::vector<ItemAllocation>{}, std::vector<ResolvedServerItem>{},
        baseline, item, clientMpq, server, currency, category,
        std::vector<ResolvedCurrencyCategory>{}, std::vector<ContentBaseline>{},
        extendedCost, std::vector<ResolvedExtendedCost>{},
        std::vector<ResolvedVendorRow>{},
        std::vector<ResolvedCreatureTemplate>{},
        std::vector<ResolvedGameObjectTemplate>{},
        std::vector<ResolvedCreatureSpawn>{}, std::vector<ResolvedSpell>{},
        spellSha, true, std::map<std::string, std::string>{}, frameXmlHashes);
}

bool Verify(std::string const& artifact, std::string& error,
            std::map<std::string, std::string> const& frameXmlHashes)
{
    return ContentServerBundle::VerifyParity(artifact, "r", 12340, "", "", "",
        std::vector<ResolvedServerItem>{}, std::vector<ItemAllocation>{}, error,
        std::vector<ResolvedExtendedCost>{}, std::vector<ResolvedVendorRow>{},
        std::vector<ResolvedCreatureTemplate>{},
        std::vector<ResolvedGameObjectTemplate>{},
        std::vector<ResolvedCreatureSpawn>{}, std::vector<ResolvedSpell>{},
        std::map<std::string, std::string>{}, frameXmlHashes);
}
}

int main(int argc, char** argv)
{
    auto path = std::filesystem::temp_directory_path() / "content-frame-xml-test.epf";
    auto work = std::filesystem::temp_directory_path() / "content-frame-xml-work";
    std::filesystem::remove_all(work);

    // 1. The checked-in stock TOC is the exact byte sequence the pin accepts.
    assert(ContentBuildHash::Bytes(StockToc()) ==
           ContentFrameXml::VerifiedStockTocSha256());

    // 2. Declared floor names validate, carry the client-side declaration, and
    //    add the protected-framexml requirement without the author asking.
    Save(path, Manifest(Labels()));
    {
        auto validation = ContentPackage(path).Validate();
        assert(validation.valid);
        assert(validation.manifest.clientFrameXml);
        assert(validation.manifest.clientFrameXml->stockTocSource ==
               "upstream/FrameXML.toc");
        assert(validation.manifest.clientRequirements ==
               std::vector<std::string>{ContentClientRequirement::ProtectedFrameXml});
        auto const& area = validation.manifest.worldMaps.at(0).areas.at(0);
        assert(area.floorNames.at("enUS").labels.at(1) == "Servant's Quarters");
        assert(area.floorNames.at("deDE").labels.size() == 1);
    }

    // A raw package file can independently contribute a TOC load entry. The
    // capability follows from composition and is not an extra author claim.
    Save(path, NativeHuntsManifest());
    {
        auto validation = ContentPackage(path).Validate();
        assert(validation.valid);
        assert(validation.manifest.clientFrameXml->loadEntries.size() == 1);
        auto const& entry = validation.manifest.clientFrameXml->loadEntries.front();
        assert(entry.target == "Interface/FrameXML/NativeHuntsFrame.xml");
        assert(entry.after == ContentFrameXml::NativePveLuaTarget());
        assert(validation.manifest.clientRequirements ==
               std::vector<std::string>{ContentClientRequirement::ProtectedFrameXml});
    }

    // 3. No floor names means no clientFrameXml and no requirement: a package
    //    that ships no labels is never marked as needing FrameXML support.
    Save(path, {{"schema", 3}, {"package", "floors-b"}, {"name", "Floors B"},
        {"version", "1.0.0"},
        {"content", json::array({{{"type", "file"},
            {"source", "assets/readme.txt"},
            {"target", "Documentation/floors-b.txt"}}})}});
    {
        auto validation = ContentPackage(path).Validate();
        assert(validation.valid);
        assert(!validation.manifest.clientFrameXml);
        assert(validation.manifest.clientRequirements.empty());
    }

    // 4. Either half of the pair without the other is refused. An empty
    //    locale object never reaches the pairing check: it is not a declaration
    //    of anything at all, and says so where the mistake is.
    Save(path, Manifest(Labels(), false));
    {
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("clientFrameXml") != std::string::npos);
    }
    Save(path, Manifest(json::object()));
    {
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("floorNames") != std::string::npos);
    }

    // 5. The pair requires schema 3.
    {
        auto manifest = Manifest(Labels());
        manifest["schema"] = 2;
        Save(path, manifest);
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("Schema 3") != std::string::npos);
    }

    // 6. A locale the build-12340 client cannot select is refused rather than
    //    shipped as bytes GetLocale() will never select.
    Save(path, Manifest(json{{"xxXX", {{"1", "Nope"}}}}));
    {
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("xxXX") != std::string::npos);
    }

    // 7. Level keys are canonical decimals of the index the stock loop numbers.
    for (auto const& key : {"0", "01", "1.0", "+1", " 1", "1024", "-1", ""})
    {
        Save(path, Manifest(json{{"enUS", {{key, "Nope"}}}}));
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("floorNames") != std::string::npos);
    }
    Save(path, Manifest(json{{"enUS", {{"1023", "Edge"}}}}));
    assert(ContentPackage(path).Validate().valid);

    // 8. Labels must be presentable text.
    for (json const& bad : {json{{"enUS", {{"1", ""}}}},
                            json{{"enUS", {{"1", 1}}}},
                            json{{"enUS", {{"1", std::string(256, 'x')}}}},
                            json{{"enUS", {{"1", std::string("bad\x01")}}}}})
    {
        Save(path, Manifest(bad));
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("floorNames") != std::string::npos);
    }

    // 9. A label UTF-8 sequence survives the round trip to the parsed manifest.
    Save(path, Manifest(json{{"ruRU", {{"1", "\xd0\x9a\xd0\xb0\xd1\x80\xd0\xb0\xd0\xb7\xd0\xb0\xd0\xbd"}}}}));
    assert(ContentPackage(path).Validate().valid);

    // 10. Only the pinned stock TOC digest is accepted.
    {
        auto manifest = Manifest(Labels());
        manifest["clientFrameXml"]["stockTocSha256"] = std::string(64, '0');
        Save(path, manifest);
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("stockTocSha256") != std::string::npos);
    }
    {
        auto manifest = Manifest(Labels());
        manifest["clientFrameXml"]["stockTocSource"] = "../escape.toc";
        Save(path, manifest);
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("Unsafe") != std::string::npos);
    }
    {
        auto manifest = Manifest(Labels());
        manifest["clientFrameXml"]["extra"] = true;
        Save(path, manifest);
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("clientFrameXml") != std::string::npos);
    }

    // 11. An area whose internalName cannot survive strupper cannot carry
    //     labels: the generated lookup would never match it.
    {
        auto manifest = Manifest(Labels());
        manifest["worldMaps"][0]["areas"][0]["internalName"] = "Karazhan\xd0\x9a";
        Save(path, manifest);
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("floor labels") != std::string::npos);
    }

    // 12. Only the lowercase ASCII fold is treated as the lookup token.
    {
        std::string token;
        assert(ContentFrameXml::FloorNameToken("Karazhan", token));
        assert(token == "KARAZHAN");
        assert(ContentFrameXml::FloorNameToken("Blackrock Spire", token));
        assert(token == "BLACKROCK SPIRE");
        assert(!ContentFrameXml::FloorNameToken("", token));
        assert(!ContentFrameXml::FloorNameToken("Karazhan\xd0\x9a", token));
        assert(!ContentFrameXml::FloorNameToken("Karaz\nhan", token));
    }

    // 13. Generated Lua is deterministic, ordered, escaped, and delegates to
    //     the stock function when it has nothing to say. When a custom table is
    //     present, only the displayed label is augmented: enumeration, terrain
    //     floor adjustment, callback and checked state remain stock.
    ContentFrameXml::Declared declared;
    declared["enUS"]["KARAZHAN"][1] = "Servant's Quarters";
    declared["enUS"]["KARAZHAN"][2] = "A \"quoted\" \\ back\tTab";
    declared["deDE"]["KARAZHAN"][1] = "Dienerquartier";
    std::string lua, error;
    assert(ContentFrameXml::ComposeLua(declared, lua, error));
    std::string again;
    assert(ContentFrameXml::ComposeLua(declared, again, error));
    assert(lua == again);
    assert(lua.find("[\"deDE\"]") < lua.find("[\"enUS\"]"));
    assert(lua.find("[1] = \"Servant's Quarters\"") != std::string::npos);
    assert(lua.find("[2] = \"A \\\"quoted\\\" \\\\ back\\tTab\"") != std::string::npos);
    assert(lua.find("local usesTerrainMap = DungeonUsesTerrainMap()") !=
           std::string::npos);
    assert(lua.find("local mapname = strupper(mapInfo or \"\")") !=
           std::string::npos);
    assert(lua.find("local floorNum = i") != std::string::npos);
    assert(lua.find("floorNum = i - 1") != std::string::npos);
    assert(lua.find(
        "local floorname = _G[\"DUNGEON_FLOOR_\" .. mapname .. floorNum]") !=
        std::string::npos);
    assert(lua.find(
        "info.text = labels[i] or floorname or string.format(FLOOR_NUMBER, i)") !=
        std::string::npos);
    assert(lua.find("info.func = WorldMapLevelButton_OnClick") !=
           std::string::npos);
    assert(lua.find("info.checked = (i == level)") != std::string::npos);
    assert(lua.find("local stockInitialize = WorldMapLevelDropDown_Initialize") !=
           std::string::npos);
    assert(lua.find("return stockInitialize()") != std::string::npos);
    // The only stock function the module replaces.
    assert(lua.find("WorldMapLevelDropDown_Initialize = function()") !=
           std::string::npos);
    assert(lua.find("WorldMapLevelButton_OnClick =") == std::string::npos);
    assert(lua.find("UIDropDownMenu_AddButton =") == std::string::npos);
    assert(ContentFrameXml::ComposeLua(ContentFrameXml::Declared{}, lua, error) ==
           false);

    // 14. Composition refuses a build whose own packages disagree about one
    //     label, and accepts a second package that only adds.
    {
        ContentFrameXml::Declared mine;
        mine["enUS"]["KARAZHAN"][1] = "Servant's Quarters";
        ContentFrameXml::Declared same;
        same["enUS"]["KARAZHAN"][1] = "Servant's Quarters";
        ContentFrameXml::Declared other;
        other["enUS"]["KARAZHAN"][1] = "Something Else";
        ContentFrameXml::Declared extra;
        extra["enUS"]["KARAZHAN"][17] = "Netherspace";
        extra["frFR"]["KARAZHAN"][1] = "Quartiers du Serviteur";
        ContentFrameXml::Declared all = mine;
        assert(ContentFrameXml::Merge(same, "b", all, error));
        assert(all.at("enUS").at("KARAZHAN").size() == 1);
        assert(!ContentFrameXml::Merge(other, "b", all, error));
        assert(error.find("Package 'b'") != std::string::npos);
        assert(ContentFrameXml::Merge(extra, "b", all, error));
        assert(all.at("enUS").at("KARAZHAN").size() == 2);
        assert(all.at("frFR").at("KARAZHAN").at(1) == "Quartiers du Serviteur");
    }

    // 15. The generated TOC is the stock file plus exactly one inserted line.
    auto stock = StockToc();
    std::string toc;
    assert(ContentFrameXml::ComposeToc(stock, toc, error));
    assert(toc.size() == stock.size() + 38);
    assert(toc.find("\r\n## add new modules above here\r\nContentManagerWorldMapFloorNames.lua\r\nLocalizationPost.xml") !=
           std::string::npos);
    {
        // Every stock line survives in order, with one extra entry.
        std::vector<std::string> before, after;
        auto split = [](std::string const& text, std::vector<std::string>& out) {
            std::size_t position = 0;
            while (position < text.size())
            {
                auto stop = text.find('\n', position);
                stop = stop == std::string::npos ? text.size() : stop + 1;
                out.push_back(text.substr(position, stop - position));
                position = stop;
            }
        };
        split(std::string(stock.begin(), stock.end()), before);
        split(toc, after);
        assert(after.size() == before.size() + 1);
        std::size_t inserted = 0, index = 0;
        for (auto const& line : after)
        {
            if (index < before.size() && line == before[index]) { ++index; continue; }
            assert(inserted == 0);
            assert(line == "ContentManagerWorldMapFloorNames.lua\r\n");
            ++inserted;
        }
        assert(inserted == 1);
        assert(index == before.size());
    }

    // 16. A TOC without the marker, or with two of them, is refused rather than
    //     guessed at, and one that already loads the module is not doubled.
    {
        std::string noMarker = "## Interface: 30000\r\nLocalizationPost.xml";
        std::string two = "## add new modules above here\r\na.lua\r\n"
                          "## add new modules above here\r\nb.lua\r\n";
        std::string already = "## add new modules above here\r\n"
                              "ContentManagerWorldMapFloorNames.lua\r\nb.lua\r\n";
        for (auto const* text : {"noMarker", "two", "already"})
        {
            std::string source = text == std::string("noMarker") ? noMarker
                                 : text == std::string("two") ? two : already;
            assert(!ContentFrameXml::ComposeToc({source.begin(), source.end()},
                                                toc, error));
            assert(!error.empty());
        }
    }


    // Generic package entries preserve every stock byte and land at their
    // declared anchors. Siblings are sorted by logical path, and a dependency
    // on a contributed entry remains adjacent to that parent.
    std::vector<ContentFrameXml::LoadEntry> loads = {
        {"Interface/FrameXML/Zeta.xml", "Interface/FrameXML/LFDFrame.xml"},
        {"Interface/FrameXML/NativeHuntsFrame.xml", "Interface/FrameXML/LFDFrame.xml"},
        {"Interface/FrameXML/NativeHuntsAfter.lua",
         "Interface/FrameXML/NativeHuntsFrame.xml"}};
    std::string packageToc, reorderedToc;
    assert(ContentFrameXml::ComposeToc(stock, loads, false, packageToc, error));
    std::reverse(loads.begin(), loads.end());
    assert(ContentFrameXml::ComposeToc(stock, loads, false, reorderedToc, error));
    assert(packageToc == reorderedToc);
    assert(packageToc.find("LFDFrame.xml\r\nNativeHuntsFrame.xml\r\n"
                           "NativeHuntsAfter.lua\r\nZeta.xml\r\nLFRFrame.xml") !=
           std::string::npos);
    assert(packageToc.find("NativeHuntsFrame.xml") ==
           packageToc.rfind("NativeHuntsFrame.xml"));

    // Duplicate logical entries, case aliases, stock duplicates, missing and
    // ambiguous anchors, and contribution cycles all fail closed.
    for (auto const& bad : std::vector<std::vector<ContentFrameXml::LoadEntry>>{
             {{"Interface/FrameXML/A.xml", "Interface/FrameXML/LFDFrame.xml"},
              {"Interface/FrameXML/A.xml", "Interface/FrameXML/LFDFrame.xml"}},
             {{"Interface/FrameXML/A.xml", "Interface/FrameXML/LFDFrame.xml"},
              {"interface/framexml/a.XML", "Interface/FrameXML/LFDFrame.xml"}},
             {{"Interface/FrameXML/LFRFrame.xml", "Interface/FrameXML/LFDFrame.xml"}},
             {{"Interface/FrameXML/A.xml", "Interface/FrameXML/Missing.xml"}},
             {{"Interface/FrameXML/A.xml", "Interface/FrameXML/B.xml"},
              {"Interface/FrameXML/B.xml", "Interface/FrameXML/A.xml"}}})
        assert(!ContentFrameXml::ComposeToc(stock, bad, false, packageToc, error));
    {
        std::string ambiguous = "LFDFrame.xml\r\nlfdframe.XML\r\n";
        std::vector<ContentFrameXml::LoadEntry> one = {
            {"Interface/FrameXML/A.xml", "Interface/FrameXML/LFDFrame.xml"}};
        assert(!ContentFrameXml::ComposeToc(
            {ambiguous.begin(), ambiguous.end()}, one, false, packageToc, error));
        assert(error.find("Ambiguous") != std::string::npos);
    }

    // A declaration is only loadable when its file is owned by the cumulative
    // managed target set; aliases in that set compare case-insensitively.
    {
        std::vector<ContentFrameXml::LoadEntry> one = {
            {"Interface/FrameXML/NativeHuntsFrame.xml",
             "Interface/FrameXML/LFDFrame.xml"}};
        assert(!ContentFrameXml::ValidateTargets(one, {}, error));
        assert(error.find("absent") != std::string::npos);
        assert(ContentFrameXml::ValidateTargets(one,
            {"INTERFACE/FRAMEXML/NATIVEHUNTSFRAME.XML"}, error));
        assert(!ContentFrameXml::ValidateTargets(one,
            {"INTERFACE/FRAMEXML/NATIVEHUNTSFRAME.XML",
             "INTERFACE/FRAMEXML/FRAMEXML.TOC"}, error));
        assert(error.find("Raw Interface/FrameXML/FrameXML.toc") !=
               std::string::npos);
        assert(ContentFrameXml::Targets({}) ==
               std::vector<std::string>{ContentFrameXml::StockTocTarget()});
    }

    // Parser-level duplicate and case-alias contributions are authoring errors.
    {
        auto manifest = NativeHuntsManifest();
        manifest["clientFrameXml"]["loadEntries"].push_back(
            {{"target", "interface/framexml/nativehuntsframe.XML"},
             {"after", "Interface/FrameXML/LFDFrame.xml"}});
        Save(path, manifest);
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("case-alias") != std::string::npos);
    }

    // Native page definitions fail validation before a build when their page
    // target is missing or when a package repeats an ID.
    {
        auto missing = NativeHuntsManifest();
        missing["nativePvePages"][0]["loadTarget"] =
            "Interface/FrameXML/MissingPage.xml";
        Save(path, missing);
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("package-owned") != std::string::npos);

        auto duplicate = NativeHuntsManifest();
        duplicate["nativePvePages"].push_back(duplicate["nativePvePages"][0]);
        Save(path, duplicate);
        validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("Duplicate native PvE page id") !=
               std::string::npos);
    }

    // 17. Staging writes under the workspace and refuses to overwrite.
	// Native PvE declarations are package-owned, generate deterministic
	// alphabetical registration, and load the shared controller before pages.
	{
		Save(path, NativeHuntsManifest());
		auto validation = ContentPackage(path).Validate();
		assert(validation.valid);
		assert(validation.manifest.nativePvePages.size() == 1);
		std::vector<ContentFrameXml::NativePvePage> pages = {
			{"hunts", "Hunts", "NativeHuntsFrame",
			 "Interface/FrameXML/NativeHuntsFrame.xml",
			 "NativeHuntsFrame_OnPveShow", "NativeHuntsFrame_OnPveHide"}};
		std::sort(pages.begin(), pages.end(), [](auto const& a, auto const& b) {
			return a.name < b.name;
		});
		std::string pve;
		assert(ContentFrameXml::ComposeNativePveLua(pages, pve, error));
		if (argc > 1) {
			std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
			assert(output.is_open());
			output.write(pve.data(), static_cast<std::streamsize>(pve.size()));
			assert(output.good());
		}
		auto hunts = pve.find("name=\"Hunts\"");
		assert(hunts != std::string::npos);
		assert(pve.find("name=\"Appearances\"") == std::string::npos);
		assert(pve.find("duplicate page id") != std::string::npos);
		assert(pve.find("missing frame") != std::string::npos);
		assert(pve.find("Player vs. Environment") != std::string::npos);
		auto reversed = pages;
		std::reverse(reversed.begin(), reversed.end());
		std::string reversePve;
		assert(ContentFrameXml::ComposeNativePveLua(reversed, reversePve, error));
		assert(reversePve == pve);
		auto huntsOnly = pages;
		std::string regenerated;
		assert(ContentFrameXml::ComposeNativePveLua(huntsOnly, regenerated, error));
		assert(regenerated.find("name=\"Appearances\"") == std::string::npos);
		assert(regenerated.find("name=\"Hunts\"") != std::string::npos);
		pages.push_back(pages.back());
		assert(!ContentFrameXml::ComposeNativePveLua(pages, pve, error));

		std::vector<ContentFrameXml::LoadEntry> loads = {{
			"Interface/FrameXML/NativeHuntsFrame.xml",
			ContentFrameXml::NativePveLuaTarget()}};
		std::string pveToc;
		assert(ContentFrameXml::ComposeToc(StockToc(), loads,
			{ContentFrameXml::GeneratedResource::NativePve},
			pveToc, error));
		auto manager = pveToc.find("ContentManagerNativePve.lua");
		auto page = pveToc.find("NativeHuntsFrame.xml");
		assert(manager != std::string::npos && manager < page);
		auto pveTargets = ContentFrameXml::Targets(
			{ContentFrameXml::GeneratedResource::NativePve});
		assert(std::find(pveTargets.begin(), pveTargets.end(),
			ContentFrameXml::NativePveLuaTarget()) != pveTargets.end());
	}

	// Collections registrations are schema-3, ordered independently of package
	// discovery, and activate exactly the controller, icon, and composed TOC.
	{
		Save(path, NativeCollectionsManifest());
		auto validation = ContentPackage(path).Validate();
		assert(validation.valid);
		assert(validation.manifest.nativePvePages.empty());
		assert(validation.manifest.nativeCollectionPages.size() == 1);
		auto page = validation.manifest.nativeCollectionPages.front();
		assert(page.id == "appearances" && page.order == 100);
		assert(page.title == "Appearances" && page.width == 704 && page.height == 616);
		auto legacy = NativeCollectionsManifest();
		legacy["nativeCollectionPages"][0].erase("title");
		legacy["nativeCollectionPages"][0].erase("width");
		legacy["nativeCollectionPages"][0].erase("height");
		Save(path, legacy);
		auto legacyValidation = ContentPackage(path).Validate();
		assert(legacyValidation.valid);
		assert(legacyValidation.manifest.nativeCollectionPages.front().title.empty());
		assert(legacyValidation.manifest.nativeCollectionPages.front().width == 0);
		assert(legacyValidation.manifest.nativeCollectionPages.front().height == 0);

		auto fallbackOrder = NativeCollectionsManifest();
		fallbackOrder["nativeCollectionPages"][0].erase("order");
		Save(path, fallbackOrder);
		auto fallbackValidation = ContentPackage(path).Validate();
		assert(fallbackValidation.valid);
		assert(fallbackValidation.manifest.nativeCollectionPages.front().order == 1000);
		auto invalidSize = NativeCollectionsManifest();
		invalidSize["nativeCollectionPages"][0]["width"] = 319;
		Save(path, invalidSize);
		assert(!ContentPackage(path).Validate().valid);
		auto unsupportedField = NativeCollectionsManifest();
		unsupportedField["nativeCollectionPages"][0]["tabOffset"] = -26;
		Save(path, unsupportedField);
		auto unsupportedValidation = ContentPackage(path).Validate();
		assert(!unsupportedValidation.valid);
		assert(unsupportedValidation.error.find("unsupported field tabOffset") !=
			std::string::npos);
		auto duplicate = NativeCollectionsManifest();
		duplicate["nativeCollectionPages"].push_back(
			duplicate["nativeCollectionPages"][0]);
		Save(path, duplicate);
		auto rejected = ContentPackage(path).Validate();
		assert(!rejected.valid);
		assert(rejected.error.find("Duplicate Collections page id") !=
			std::string::npos);

		std::vector<ContentFrameXml::NativeCollectionPage> pages = {
			{"toys", "Toys", 300, "ToysFrame", "Interface/FrameXML/Toys.xml", "", "",
			 "Toy Box", 940, 590},
			{"heirlooms", "Heirlooms", 200, "HeirloomsFrame",
			 "Interface/FrameXML/Heirlooms.xml", "", "", "Heirlooms", 800, 520},
			{"appearances", "Appearances", 100, "NativeTransmogCollectionsFrame",
			 "Interface/FrameXML/NativeTransmogFrame.xml",
			 "NativeTransmog_OpenCollections", "NativeTransmog_CloseCollections",
			 "Appearances", 704, 616}};
		std::string collections, reversed;
		assert(ContentFrameXml::ComposeCollectionsLua(pages, collections, error));
		std::string singlePage;
		assert(ContentFrameXml::ComposeCollectionsLua({pages.back()}, singlePage, error));
		assert(singlePage.find("name=\"Appearances\"") != std::string::npos);
		assert(singlePage.find("name=\"Heirlooms\"") == std::string::npos);
		assert(singlePage.find("name=\"Toys\"") == std::string::npos);
		assert(singlePage.find("tab:SetPoint(\"BOTTOMLEFT\",host,\"BOTTOMLEFT\",22,-26)") !=
			std::string::npos);
		if (argc > 2) {
			std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
			assert(output.is_open());
			output.write(collections.data(),
				static_cast<std::streamsize>(collections.size()));
			assert(output.good());
		}
		std::reverse(pages.begin(), pages.end());
		assert(ContentFrameXml::ComposeCollectionsLua(pages, reversed, error));
		assert(collections == reversed);
		assert(collections.find("Interface\\\\DialogFrame\\\\UI-DialogBox-Background-Dark") != std::string::npos);
		assert(collections.find("Interface\\\\DialogFrame\\\\UI-DialogBox-Gold-Border") != std::string::npos);
		assert(collections.find("Interface\\\\DialogFrame\\\\UI-DialogBox-Header") != std::string::npos);
		assert(collections.find("DEFAULT_WIDTH,DEFAULT_HEIGHT,DEFAULT_ORDER,CONTENT_TOP,CONTENT_BOTTOM=940,590,1000,32,0") != std::string::npos);
		assert(collections.find("title=\"Appearances\",width=704,height=616") != std::string::npos);
		assert(collections.find("title:SetText(page.title and page.title~=\"\" and page.title or page.name)") != std::string::npos);
		assert(collections.find("frame:ClearAllPoints(); frame:SetPoint(\"TOPLEFT\",host,\"TOPLEFT\",0,-CONTENT_TOP); frame:SetPoint(\"BOTTOMRIGHT\",host,\"BOTTOMRIGHT\",0,CONTENT_BOTTOM)") != std::string::npos);
		assert(collections.find("ContentManagerCollectionsEmblem") != std::string::npos);
		assert(collections.find("Interface\\\\Minimap\\\\UI-Minimap-Background") == std::string::npos);
		assert(collections.find("Interface\\\\Minimap\\\\MiniMap-TrackingBorder") == std::string::npos);
		assert(collections.find("emblem:SetWidth(72); emblem:SetHeight(72)") != std::string::npos);
		assert(collections.find("tab:SetHeight(32); tab:SetFrameLevel(host:GetFrameLevel()+3)") != std::string::npos);
		assert(collections.find("tab:SetPoint(\"BOTTOMLEFT\",host,\"BOTTOMLEFT\",22,-26)") != std::string::npos);
		assert(collections.find("tab:SetPoint(\"BOTTOMLEFT\",host,\"BOTTOMLEFT\",64,4)") == std::string::npos);
		assert(collections.find("if page.order==nil then page.order=DEFAULT_ORDER end") != std::string::npos);
		assert(collections.find("for _,candidate in ipairs(ordered)") != std::string::npos);
		assert(collections.find("ContentManagerCollectionsCloseButton") ==
			collections.rfind("ContentManagerCollectionsCloseButton"));
		assert(collections.find("ContentManagerCollectionsMicroButton") != std::string::npos);
		assert(collections.find("ContentManagerCollections.tga") == std::string::npos);
		assert(collections.find("button:SetWidth(28); button:SetHeight(58); button:SetHitRectInsets(0,0,18,0)") != std::string::npos);
		assert(collections.find("button:SetNormalTexture(\"Interface\\\\Buttons\\\\UI-MicroButtonCharacter-Up\")") != std::string::npos);
		assert(collections.find("button:SetPushedTexture(\"Interface\\\\Buttons\\\\UI-MicroButtonCharacter-Down\")") != std::string::npos);
		assert(collections.find("button:SetHighlightTexture(\"Interface\\\\Buttons\\\\UI-MicroButton-Hilight\",\"ADD\")") != std::string::npos);
		assert(collections.find("UI-MicroButton-LFG-Up") == std::string::npos);
		assert(collections.find("UI-MicroButton-LFG-Down") == std::string::npos);
		assert(collections.find("button:CreateTexture(nil,\"OVERLAY\")") != std::string::npos);
		assert(collections.find("icon:SetWidth(20); icon:SetHeight(20); icon:SetPoint(\"BOTTOM\",button,\"BOTTOM\",0,7)") != std::string::npos);
		assert(collections.find("positionIcon=function(pressed)") != std::string::npos);
		assert(collections.find("pressed and 1 or 0,pressed and 6 or 7") != std::string::npos);
		assert(collections.find("if button:GetParent()~=parent then return end") != std::string::npos);
		assert(collections.find("name=\"Appearances\"") < collections.find("name=\"Toys\""));
		assert(collections.find("name=\"Heirlooms\"") < collections.find("name=\"Toys\""));

		ContentFrameXml::ResourceSelection resources = {
			ContentFrameXml::GeneratedResource::CollectionsController,
			ContentFrameXml::GeneratedResource::CollectionsIcon};
		auto targets = ContentFrameXml::Targets(resources);
		assert(targets.size() == 3);
		assert(std::find(targets.begin(), targets.end(),
			ContentFrameXml::CollectionsLuaTarget()) != targets.end());
		assert(std::find(targets.begin(), targets.end(),
			ContentFrameXml::CollectionsIconTarget()) != targets.end());
		std::vector<ContentFrameXml::LoadEntry> collectionLoads = {{
			"Interface/FrameXML/NativeTransmogFrame.xml",
			ContentFrameXml::CollectionsLuaTarget()}};
		std::string collectionToc;
		assert(ContentFrameXml::ComposeToc(StockToc(), collectionLoads,
			resources, collectionToc, error));
		assert(collectionToc.find("ContentManagerCollections.lua") <
			collectionToc.find("NativeTransmogFrame.xml"));

		auto icon = ContentFrameXml::ComposeCollectionsIcon();
		assert(icon == ContentFrameXml::ComposeCollectionsIcon());
		assert(icon.size() == 18 + 128 * 128 * 4);
		assert(icon[2] == 2 && icon[16] == 32 && icon[17] == 0x28);
		assert(icon[12] == 128 && icon[13] == 0);
		assert(icon[14] == 128 && icon[15] == 0);
		int minX = 128, minY = 128, maxX = -1, maxY = -1;
		std::size_t transparent = 0, opaque = 0, purple = 0, gold = 0;
		for (int y = 0; y < 128; ++y)
			for (int x = 0; x < 128; ++x) {
				auto offset = 18 + (y * 128 + x) * 4;
				auto alpha = icon[offset + 3];
				if (!alpha) { ++transparent; continue; }
				++opaque;
				auto blue = icon[offset], green = icon[offset + 1], red = icon[offset + 2];
				if (blue > red && red > green) ++purple;
				if (red > 140 && green > 80 && red > blue * 2) ++gold;
				minX = std::min(minX, x); maxX = std::max(maxX, x);
				minY = std::min(minY, y); maxY = std::max(maxY, y);
			}
		assert(transparent > 3000 && opaque > 10000);
		assert(purple > 5000 && gold > 1000);
		assert(minX == 3 && maxX == 124 && minY == 3 && maxY == 124);
		assert(minX + maxX == 127 && minY + maxY == 127);
	}

    assert(ContentFrameXml::Stage(ContentFrameXml::GeneratedLuaTarget(), lua,
                                  work, error));
    auto staged = work / std::filesystem::path(
        ContentFrameXml::GeneratedLuaTarget()).generic_string();
    assert(std::filesystem::is_regular_file(staged));
    {
        std::ifstream input(staged, std::ios::binary);
        std::string readBack((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
        assert(readBack == lua);
    }
    assert(!ContentFrameXml::Stage(ContentFrameXml::GeneratedLuaTarget(), lua, work,
                                   error));
    assert(error.find("already exists") != std::string::npos);
    assert(!ContentFrameXml::Stage("../escape.lua", "x", work, error));

    // 18. Parity records the generated files by exact target and hash, and
    //     activation accepts nothing else.
    {
        std::map<std::string, std::string> hashes = {
            {ContentFrameXml::GeneratedLuaTarget(),
             ContentBuildHash::Bytes({lua.begin(), lua.end()})},
            {ContentFrameXml::NativePveLuaTarget(),
             ContentBuildHash::Bytes({4, 5, 6})},
            {ContentFrameXml::CollectionsLuaTarget(),
             ContentBuildHash::Bytes({7, 8, 9})},
            {ContentFrameXml::CollectionsIconTarget(),
             ContentBuildHash::Bytes({10, 11, 12})},
            {ContentFrameXml::StockTocTarget(),
             ContentBuildHash::Bytes({toc.begin(), toc.end()})}};
        auto artifact = Parity(hashes);
        auto document = json::parse(artifact);
        assert(document.at("format") == 9);
        assert(document.at("frameXmlSha256").size() == 5);
        auto declared = ContentServerBundle::ReadParityAllocations(artifact, {});
        assert(declared.frameXmlSha256 == hashes);
        std::string errorText;
        assert(Verify(artifact, errorText, hashes));
        // A build that composed different bytes must not accept this artifact.
        auto wrong = hashes;
        wrong[ContentFrameXml::GeneratedLuaTarget()] =
            ContentBuildHash::Bytes({1, 2, 3});
        assert(!Verify(artifact, errorText, wrong));
        // An artifact naming a file this build never generates is refused.
        auto tampered = json::parse(artifact);
        tampered["frameXmlSha256"]["Interface/FrameXML/Other.lua"] =
            hashes.at(ContentFrameXml::StockTocTarget());
        assert(!Verify(tampered.dump(2) + "\n", errorText, hashes));
        // A build that generated nothing must not accept this artifact.
        assert(!Verify(artifact, errorText, {}));
    }

    // 19. A build without floor labels records no generated FrameXML at all.
    {
        auto artifact = Parity({});
        assert(!json::parse(artifact).contains("frameXmlSha256"));
        assert(ContentServerBundle::ReadParityAllocations(artifact, {})
                   .frameXmlSha256.empty());
    }

    std::filesystem::remove_all(work);
    std::filesystem::remove(path);
    std::cout << "frame_xml checks passed" << std::endl;
    return 0;
}
