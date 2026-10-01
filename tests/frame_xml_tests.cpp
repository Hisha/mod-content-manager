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

json ClientFrameXml()
{
    return json{{"stockTocSource", "upstream/FrameXML.toc"},
        {"stockTocSha256", ContentFrameXml::VerifiedStockTocSha256()}};
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

int main()
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

    // 17. Staging writes under the workspace and refuses to overwrite.
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
            {ContentFrameXml::StockTocTarget(),
             ContentBuildHash::Bytes({toc.begin(), toc.end()})}};
        auto artifact = Parity(hashes);
        auto document = json::parse(artifact);
        assert(document.at("format") == 9);
        assert(document.at("frameXmlSha256").size() == 2);
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
