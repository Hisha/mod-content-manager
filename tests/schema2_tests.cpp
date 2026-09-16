#include "ContentPackage.h"
#include "third_party/json/json.hpp"
#include "third_party/miniz/miniz.h"
#include <cassert>
#include <filesystem>
#include <stdexcept>

using json = nlohmann::json;

namespace
{
void Save(std::filesystem::path const& path, json const& manifest, bool raw = false)
{
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, path.string().c_str(), 0)) throw std::runtime_error("zip init failed");
    auto body = manifest.dump();
    if (!mz_zip_writer_add_mem(&zip, "manifest.json", body.data(), body.size(), MZ_BEST_COMPRESSION)
        || (raw && !mz_zip_writer_add_mem(&zip, "assets/test.txt", "ok", 2, MZ_BEST_COMPRESSION))
        || !mz_zip_writer_finalize_archive(&zip))
        throw std::runtime_error("zip write failed");
    mz_zip_writer_end(&zip);
}
}

int main(int argc, char** argv)
{
    auto path = std::filesystem::temp_directory_path() / "content-schema2-test.epf";
    json row = {{"op", "add"}, {"table", "Item"}, {"symbol", "seal"}, {"fields", {
        {"ClassID", 15}, {"SubclassID", 0}, {"SoundOverrideSubclassID", -1},
        {"Material", -1}, {"DisplayInfoID", {{"copyFromItem", 6948}}},
        {"InventoryType", 0}, {"SheatheType", 0}}}};
    json manifest = {{"schema", 2}, {"package", "mod-hunts"}, {"name", "Seal"},
        {"version", "1"}, {"content", json::array()}, {"dbcRows", json::array({row})}};
    Save(path, manifest);
    assert(ContentPackage(path).Validate().valid);
    json server = {{"table", "item_template"}, {"op", "upsert"}, {"symbol", "seal"},
        {"fields", {{"name", "Huntmaster's Seal"}, {"description", "A token"},
            {"Quality", 1}, {"stackable", 200}, {"bonding", 0}, {"BagFamily", 0}}}};
    auto withServer = manifest;
    withServer["serverRows"] = json::array({server});
    Save(path, withServer); assert(ContentPackage(path).Validate().valid);
    auto invalidServer = withServer;
    invalidServer["serverRows"][0]["entry"] = 56807;
    Save(path, invalidServer); assert(!ContentPackage(path).Validate().valid);
    invalidServer = withServer;
    invalidServer["serverRows"][0]["fields"]["entry"] = 56807;
    Save(path, invalidServer); assert(!ContentPackage(path).Validate().valid);
    invalidServer = withServer;
    invalidServer["serverRows"][0]["table"] = "creature_template";
    Save(path, invalidServer); assert(!ContentPackage(path).Validate().valid);
    invalidServer = withServer;
    invalidServer["serverRows"][0]["op"] = "replace";
    Save(path, invalidServer); assert(!ContentPackage(path).Validate().valid);
    invalidServer = withServer;
    invalidServer["serverRows"][0]["fields"]["unknown"] = 1;
    Save(path, invalidServer); assert(!ContentPackage(path).Validate().valid);
    invalidServer = withServer;
    invalidServer["serverRows"].push_back(server);
    Save(path, invalidServer); assert(!ContentPackage(path).Validate().valid);
    invalidServer = withServer;
    invalidServer["serverRows"][0]["symbol"] = "other-package-seal";
    Save(path, invalidServer); assert(!ContentPackage(path).Validate().valid);
    invalidServer = withServer;
    invalidServer["serverRows"][0]["fields"]["BagFamily"] = 8192;
    Save(path, invalidServer); assert(!ContentPackage(path).Validate().valid);
    auto currency = withServer;
    currency["serverRows"][0]["fields"]["BagFamily"] = 8192;
    currency["currencies"] = json::array({{{"symbol","seal-currency"},{"item","seal"},{"categoryCopyFromItem",40752}}});
    Save(path,currency); assert(ContentPackage(path).Validate().valid);
    for (auto key : {"ID","BitIndex","knownBit"})
    {
        auto invalid=currency;invalid["currencies"][0][key]=4;
        Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    }
    auto invalid=currency;invalid["currencies"][0]["item"]="absent";
    Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=currency;invalid["currencies"].push_back(invalid["currencies"][0]);
    Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=currency;invalid["serverRows"][0]["fields"]["BagFamily"]=0;
    Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    auto category = currency;
    category["currencies"][0].erase("categoryCopyFromItem");
    category["currencies"][0]["category"] = {{"symbol","hunts"}};
    category["currencyCategories"] = json::array({{{"symbol","hunts"},{"name",{{"enUS","Hunts"}}}}});
    Save(path,category); assert(ContentPackage(path).Validate().valid);
    for (auto name : {"enUS", "frFR", "ruRU"})
    { auto localized=category;localized["currencyCategories"][0]["name"][name]="Hunts";
      Save(path,localized);assert(ContentPackage(path).Validate().valid); }
    invalid=category;invalid["currencies"][0]["categoryCopyFromItem"]=40752;
    Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=category;invalid["currencies"][0].erase("category");
    Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=category;invalid["currencyCategories"][0]["ID"]=5;
    Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=category;invalid["currencyCategories"][0]["name"].erase("enUS");
    Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=category;invalid["currencyCategories"][0]["name"]["xxXX"]="Hunts";
    Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=category;invalid["currencies"][0]["category"]["symbol"]="missing";
    Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=category;invalid["currencyCategories"][0]["name"]["enUS"]="";
    Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    auto bad = manifest; bad["dbcRows"][0]["table"] = "CurrencyTypes";
    Save(path, bad); assert(!ContentPackage(path).Validate().valid);
    bad = manifest; bad["dbcRows"][0]["op"] = "modify";
    Save(path, bad); assert(!ContentPackage(path).Validate().valid);
    bad = manifest; bad["dbcRows"].push_back(row);
    Save(path, bad); assert(!ContentPackage(path).Validate().valid);
    bad = manifest; bad["dbcRows"][0]["symbol"] = "Bad Symbol";
    Save(path, bad); assert(!ContentPackage(path).Validate().valid);
    bad = manifest; bad["dbcRows"][0]["fields"]["ID"] = 100000;
    Save(path, bad); assert(!ContentPackage(path).Validate().valid);
    bad = manifest; bad["dbcRows"][0]["id"] = 100000;
    Save(path, bad); assert(!ContentPackage(path).Validate().valid);
    json schema1 = {{"schema", 1}, {"package", "raw-test"}, {"name", "Raw"},
        {"version", "1"}, {"content", json::array({{{"type", "file"},
            {"source", "assets/test.txt"}, {"target", "Documentation/test.txt"}}})}};
    Save(path, schema1, true); assert(ContentPackage(path).Validate().valid);
    auto rawSchema2 = schema1; rawSchema2["schema"] = 2;
    Save(path, rawSchema2, true); assert(ContentPackage(path).Validate().valid);
    if (argc > 1) assert(ContentPackage(argv[1]).Validate().valid); // Real Scarab Gong EPF.
    std::filesystem::remove(path);
}
