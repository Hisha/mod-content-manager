#include "ContentServerOwnership.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include "third_party/json/json.hpp"

using json = nlohmann::json;

bool ContentServerOwnership::ReadOwner(std::uint32_t entry, bool& exists,
    ContentItemOwner& owner, std::string& error)
{
    exists = false;
    auto result = WorldDatabase.Query("SELECT o.realm_name,o.package_key,o.symbol,o.resource_kind,o.applied_build,"
        "o.artifact_sha256,o.row_json FROM (SELECT 1) seed LEFT JOIN content_manager_item_owner o "
        "ON o.entry=" + std::to_string(entry));
    if (!result) { error = "Cannot inspect item ownership; apply Phase 3 world SQL and check SQL logs"; return false; }
    auto fields = result->Fetch();
    if (fields[0].IsNull()) return true;
    owner = {fields[0].Get<std::string>(), fields[1].Get<std::string>(),
        fields[2].Get<std::string>(), fields[3].Get<std::string>(), fields[4].Get<uint32>(),
        fields[5].Get<std::string>(), fields[6].Get<std::string>()};
    exists = true;
    return true;
}

bool ContentServerOwnership::ReadCurrentRow(std::uint32_t entry, bool& exists,
    std::string& rowJson, std::string& error)
{
    exists = false;
    auto result = WorldDatabase.Query("SELECT t.`class`,t.`subclass`,t.`SoundOverrideSubclass`,"
        "t.`name`,t.`displayid`,t.`Quality`,t.`InventoryType`,t.`stackable`,t.`bonding`,"
        "t.`description`,t.`Material`,t.`sheath`,t.`BagFamily`,t.`Flags` "
        "FROM (SELECT 1) seed LEFT JOIN item_template t ON t.entry=" + std::to_string(entry));
    if (!result) { error = "Cannot inspect item_template row; check deployed schema/SQL logs"; return false; }
    auto f = result->Fetch();
    if (f[0].IsNull()) return true;
    for (unsigned index : {3U, 5U, 7U, 9U, 13U})
        if (f[index].IsNull()) { error = "Managed item_template row has NULL in a required parity field"; return false; }
    json fields = {{"class", f[0].Get<std::uint8_t>()}, {"subclass", f[1].Get<std::uint8_t>()},
        {"SoundOverrideSubclass", f[2].Get<std::int8_t>()}, {"name", f[3].Get<std::string>()},
        {"displayid", f[4].Get<std::uint32_t>()}, {"Quality", f[5].Get<std::uint8_t>()},
        {"InventoryType", f[6].Get<std::uint8_t>()}, {"stackable", f[7].Get<std::int32_t>()},
        {"bonding", f[8].Get<std::uint8_t>()}, {"description", f[9].Get<std::string>()},
        {"Material", f[10].Get<std::int8_t>()}, {"sheath", f[11].Get<std::uint8_t>()},
        {"BagFamily", f[12].Get<std::int32_t>()}, {"Flags", f[13].Get<std::uint32_t>()}};
    rowJson = fields.dump();
    exists = true;
    return true;
}

bool ContentServerOwnership::ExcludeOwned(std::string const& realm,
    std::vector<ItemAllocation> const& retained, std::set<std::uint32_t>& occupied, std::string& error)
{
    for (auto const& lease : retained)
    {
        if (!occupied.count(lease.value)) continue;
        bool owned = false;
        ContentItemOwner owner;
        if (!ReadOwner(lease.value, owned, owner, error)) return false;
        if (!owned) continue; // A retained allocation alone never proves row ownership.
        if (owner.realm != realm || owner.packageKey != lease.packageKey || owner.symbol != lease.symbol
            || owner.resourceKind != "item.id")
        { error = "Item ownership conflicts with retained allocation: " + std::to_string(lease.value); return false; }
        bool itemExists = false;
        std::string actual;
        if (!ReadCurrentRow(lease.value, itemExists, actual, error)) return false;
        if (!itemExists || actual != owner.rowJson)
        { error = "Owned item_template row drifted from recorded provenance: " + std::to_string(lease.value); return false; }
        occupied.erase(lease.value);
    }
    return true;
}
