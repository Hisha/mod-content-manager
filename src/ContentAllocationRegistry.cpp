#include "ContentAllocationRegistry.h"
#include "ContentBuildHash.h"
#include "ContentItemOccupancy.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include "Transaction.h"
#include <algorithm>
#include <mutex>

namespace
{
std::mutex allocationWrites;
std::string SqlText(std::string const& value)
{
    static char const digits[] = "0123456789abcdef";
    std::string sql = "CONVERT(X'";
    for (unsigned char c : value) { sql += digits[c >> 4]; sql += digits[c & 15]; }
    return sql + "' USING utf8mb4)";
}
char const* DbError = "Allocation registry query failed; apply module world SQL and check SQL logs";
}

bool ContentAllocationRegistry::Read(std::string const& realm, std::vector<ItemAllocation>& rows, std::string& error) const
{
    rows.clear();
    auto query = WorldDatabase.Query("SELECT a.package_key, a.symbol, a.allocated_value, a.state, "
        "a.first_build, a.last_build, a.baseline_sha256, a.policy_version FROM (SELECT 1) seed LEFT JOIN "
        "content_manager_allocation a ON a.realm_name=" + SqlText(realm)
        + " AND a.resource_kind='item.id' ORDER BY a.package_key, a.symbol");
    if (!query) { error = DbError; return false; }
    do
    {
        auto f = query->Fetch();
        if (!f[0].IsNull()) rows.push_back({realm, f[0].Get<std::string>(), f[1].Get<std::string>(),
            f[2].Get<uint32>(), f[3].Get<std::string>(), f[4].Get<uint32>(),
            f[5].Get<uint32>(), f[6].Get<std::string>(), "item.id", f[7].Get<uint32>()});
    } while (query->NextRow());
    return true;
}

bool ContentAllocationRegistry::OccupiedWorldItems(std::set<std::uint32_t>& entries,
    std::string& error) const
{
    entries.clear();
    struct Source { char const* table; char const* column; ContentItemColumnType type; };
    // item_template is the authoritative server item definition. The other
    // tables may contain manual references not yet backed by item_template;
    // keep those IDs unavailable to the allocator as well.
    static Source const sources[] = {
        {"item_template", "entry", ContentItemColumnType::UnsignedItemId},
        {"npc_vendor", "item", ContentItemColumnType::SignedVendorItemOrReference},
        {"playercreateinfo_item", "itemid", ContentItemColumnType::UnsignedItemId},
        {"creature_equip_template", "ItemID1", ContentItemColumnType::UnsignedItemId},
        {"creature_equip_template", "ItemID2", ContentItemColumnType::UnsignedItemId},
        {"creature_equip_template", "ItemID3", ContentItemColumnType::UnsignedItemId}
    };
    for (auto const& source : sources)
    {
        // Names are compile-time constants above, never interpolated from EPFs.
        auto query = WorldDatabase.Query(std::string("SELECT ") + source.column + " FROM "
            + source.table + " UNION ALL SELECT 0");
        if (!query)
        {
            error = std::string("Cannot inspect ") + source.table + "." + source.column
                + " item occupancy; check world schema/SQL logs. Allocation refused.";
            entries.clear();
            return false;
        }
        do { AddContentItemOccupancy(entries, query->Fetch()[0], source.type); }
        while (query->NextRow());
    }
    // A character can still possess an item after its world template is removed.
    auto instances = CharacterDatabase.Query("SELECT itemEntry FROM item_instance UNION ALL SELECT 0");
    if (!instances)
    {
        error = "Cannot inspect character item_instance.itemEntry occupancy; allocation refused.";
        entries.clear();
        return false;
    }
    do { AddContentItemOccupancy(entries, instances->Fetch()[0], ContentItemColumnType::UnsignedItemId); }
    while (instances->NextRow());
    return true;
}

bool ContentAllocationRegistry::CommitComposed(ContentBuildRecord const& build,
    std::vector<ItemAllocation> const& plan, std::string& error) const
{
    std::lock_guard<std::mutex> lock(allocationWrites);
    if (plan.empty() || build.state != "STAGED" || !ContentBuildHash::Valid(build.sha256))
    { error = "Invalid composed build metadata"; return false; }
    std::vector<ItemAllocation> before;
    if (!Read(build.realmName, before, error)) return false;
    std::set<std::uint32_t> occupied;
    for (auto const& row : plan)
    {
        if (row.realm != build.realmName || row.resourceKind != "item.id" || row.policyVersion != 1 || !row.value
            || !ContentBuildHash::Valid(row.baselineSha256))
        { error = "Invalid allocation plan"; return false; }
        for (auto const& prior : before)
            if ((prior.packageKey == row.packageKey && prior.symbol == row.symbol
                    && (prior.value != row.value || prior.policyVersion != row.policyVersion
                        || prior.baselineSha256 != row.baselineSha256))
                || (prior.value == row.value && (prior.packageKey != row.packageKey || prior.symbol != row.symbol)))
            { error = "Allocation registry changed since planning; rebuild required"; return false; }
    }
    if (!OccupiedWorldItems(occupied, error)) return false;
    for (auto const& row : plan)
        if (occupied.count(row.value))
        { error = "Planned Item ID became occupied in item_template: " + std::to_string(row.value); return false; }
    auto tx = WorldDatabase.BeginTransaction();
    tx->Append("INSERT INTO content_manager_build_lock (id) VALUES (1) ON DUPLICATE KEY UPDATE id=1");
    for (auto const& row : plan)
    {
        bool existing = false;
        for (auto const& old : before)
            if (old.packageKey == row.packageKey && old.symbol == row.symbol)
                existing = true;
        if (existing)
            tx->Append("UPDATE content_manager_allocation SET last_build=" + std::to_string(build.buildNumber)
                + " WHERE realm_name=" + SqlText(row.realm) + " AND package_key=" + SqlText(row.packageKey)
                + " AND symbol=" + SqlText(row.symbol) + " AND resource_kind='item.id' AND allocated_value="
                + std::to_string(row.value));
        else
            tx->Append("INSERT INTO content_manager_allocation (realm_name,package_key,symbol,resource_kind,"
            "allocated_value,state,first_build,last_build,baseline_sha256,descriptor_version,policy_version) VALUES ("
            + SqlText(row.realm) + "," + SqlText(row.packageKey) + "," + SqlText(row.symbol)
            + ",'item.id'," + std::to_string(row.value) + ",'reserved',"
            + std::to_string(build.buildNumber) + "," + std::to_string(build.buildNumber) + ","
            + SqlText(row.baselineSha256) + ",1," + std::to_string(row.policyVersion) + ")");
    }
    tx->Append("INSERT INTO content_manager_build (build_number,realm_name,filename,package_count,file_count,state,sha256) VALUES ("
        + std::to_string(build.buildNumber) + "," + SqlText(build.realmName) + "," + SqlText(build.filename)
        + "," + std::to_string(build.packageCount) + "," + std::to_string(build.fileCount)
        + ",'STAGED'," + SqlText(build.sha256) + ")");
    WorldDatabase.DirectCommitTransaction(tx);
    auto check = WorldDatabase.Query("SELECT filename,sha256 FROM content_manager_build WHERE build_number="
        + std::to_string(build.buildNumber));
    if (!check || check->Fetch()[0].Get<std::string>() != build.filename
        || check->Fetch()[1].Get<std::string>() != build.sha256)
    { error = "Composed build transaction could not be verified; inspect SQL logs"; return false; }
    std::vector<ItemAllocation> after;
    if (!Read(build.realmName, after, error)) return false;
    for (auto const& row : plan)
    {
        bool found = false;
        for (auto const& saved : after)
            if (saved.packageKey == row.packageKey && saved.symbol == row.symbol && saved.value == row.value
                && saved.lastBuild == build.buildNumber && saved.baselineSha256 == row.baselineSha256)
                found = true;
        if (!found) { error = "Committed allocation did not verify; inspect SQL logs"; return false; }
    }
    return true;
}
