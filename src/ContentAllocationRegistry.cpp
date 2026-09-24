#include "ContentAllocationRegistry.h"
#include "ContentBuildHash.h"
#include "ContentClientRequirement.h"
#include "ContentItemOccupancy.h"
#include "ContentServerOwnership.h"
#include "ContentCurrencyServer.h"
#include "ContentExtendedCostServer.h"
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
    return sql + "' USING utf8mb4) COLLATE utf8mb4_bin";
}
char const* DbError = "Allocation registry query failed; apply module world SQL and check SQL logs";
}

bool ContentAllocationRegistry::Read(std::string const& realm, std::vector<ItemAllocation>& rows, std::string& error) const
{
    rows.clear();
    auto query = WorldDatabase.Query("SELECT a.package_key, a.symbol, a.allocated_value, a.state, "
        "a.first_build, a.last_build, a.baseline_sha256, a.policy_version, a.resource_kind FROM (SELECT 1) seed LEFT JOIN "
        "content_manager_allocation a ON a.realm_name=" + SqlText(realm)
        + " ORDER BY a.package_key, a.symbol,a.resource_kind");
    if (!query) { error = DbError; return false; }
    do
    {
        auto f = query->Fetch();
        if (!f[0].IsNull()) rows.push_back({realm, f[0].Get<std::string>(), f[1].Get<std::string>(),
            f[2].Get<uint32>(), f[3].Get<std::string>(), f[4].Get<uint32>(),
            f[5].Get<uint32>(), f[6].Get<std::string>(), f[8].Get<std::string>(), f[7].Get<uint32>()});
    } while (query->NextRow());
    return true;
}

bool ContentAllocationRegistry::FindByPackage(std::string const& realm,
    std::string const& packageKey, std::vector<ItemAllocation>& rows, std::string& error) const
{
    rows.clear();
    auto query = WorldDatabase.Query("SELECT a.package_key, a.symbol, a.allocated_value, a.state, "
        "a.first_build, a.last_build, a.baseline_sha256, a.policy_version, a.resource_kind FROM (SELECT 1) seed LEFT JOIN "
        "content_manager_allocation a ON a.realm_name=" + SqlText(realm) + " AND a.package_key=" + SqlText(packageKey)
        + " ORDER BY a.package_key, a.symbol, a.resource_kind");
    if (!query) { error = DbError; return false; }
    do
    {
        auto f = query->Fetch();
        if (!f[0].IsNull()) rows.push_back({realm, f[0].Get<std::string>(), f[1].Get<std::string>(),
            f[2].Get<uint32>(), f[3].Get<std::string>(), f[4].Get<uint32>(),
            f[5].Get<uint32>(), f[6].Get<std::string>(), f[8].Get<std::string>(), f[7].Get<uint32>()});
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
    std::vector<ItemAllocation> const& plan, ContentServerBuildRecord const& server, std::string& error, std::vector<ContentBaseline> const& baselines, std::vector<ResolvedExtendedCost> const& costs) const
{
    std::lock_guard<std::mutex> lock(allocationWrites);
    if (plan.empty() || build.state != "STAGED" || !ContentBuildHash::Valid(build.sha256)
        || !ContentClientRequirement::ValidSet(build.clientRequirements)
        || server.bundleFilename != build.filename + ".server.json"
        || server.parityFilename != build.filename + ".parity.json"
        || !ContentBuildHash::Valid(server.bundleSha256) || !ContentBuildHash::Valid(server.paritySha256))
    { error = "Invalid composed build metadata"; return false; }
    std::vector<ItemAllocation> before;
    if (!Read(build.realmName, before, error)) return false;
    std::set<std::uint32_t> occupied;
    for (auto const& row : plan)
    {
        if (row.realm != build.realmName || (row.resourceKind != "item.id" && row.resourceKind != "currency.known-bit" && row.resourceKind != "currency-category.id" && row.resourceKind != "item-extended-cost.id") || row.policyVersion != 1 || !row.value
            || !ContentBuildHash::Valid(row.baselineSha256))
        { error = "Invalid allocation plan"; return false; }
        for (auto const& prior : before)
            if (prior.resourceKind == row.resourceKind && ((prior.packageKey == row.packageKey && prior.symbol == row.symbol
                    && (prior.value != row.value || prior.policyVersion != row.policyVersion
                        || prior.baselineSha256 != row.baselineSha256))
                || (prior.value == row.value && (prior.packageKey != row.packageKey || prior.symbol != row.symbol))))
            { error = "Allocation registry changed since planning; rebuild required"; return false; }
    }
    if (!OccupiedWorldItems(occupied, error)) return false;
    if (!ContentServerOwnership::ExcludeOwned(build.realmName, before, occupied, error)) return false;
    for (auto const& row : plan)
        if (row.resourceKind == "item.id" && occupied.count(row.value))
        { error = "Planned Item ID became occupied in item_template: " + std::to_string(row.value); return false; }
    if (std::any_of(plan.begin(), plan.end(), [](auto const& a) { return a.resourceKind == "currency.known-bit"; }))
    {
        std::set<std::uint32_t> bits, ids;
        if (!ContentCurrencyServer::Occupancy(build.realmName, before, bits, ids, error)) return false;
        for (auto const& row : plan)
            if ((row.resourceKind == "currency.known-bit" && (row.value > 64 || bits.count(row.value)))
                || (row.resourceKind == "item.id" && ids.count(row.value)))
            { error = "Planned currency bit or derived CurrencyTypes ID became occupied or invalid"; return false; }
    }
    if (std::any_of(plan.begin(), plan.end(), [](auto const& a) { return a.resourceKind == "currency-category.id"; }))
    {
        std::set<std::uint32_t> categories;
        if (!ContentCurrencyServer::CategoryOccupancy(build.realmName, before, categories, error)) return false;
        for (auto const& row : plan)
            if (row.resourceKind == "currency-category.id" && (row.value > 65535 || categories.count(row.value)))
            { error = "Planned category became externally referenced or invalid"; return false; }
    }
    if (std::any_of(plan.begin(), plan.end(), [](auto const& a) { return a.resourceKind == "item-extended-cost.id"; }))
    {
        std::set<std::uint32_t> ids, items;
        if (!ContentExtendedCostServer::Occupancy(build.realmName,before,ids,items,error)) return false;
        if (!ContentServerOwnership::ExcludeOwned(build.realmName,before,items,error)) return false;
        for (auto const& row : plan)
            if ((row.resourceKind == "item-extended-cost.id" && (row.value > 65535 || ids.count(row.value)))
                || (row.resourceKind == "item.id" && items.count(row.value)))
            { error = "Planned extended-cost ID/item became occupied or invalid"; return false; }
    }
    std::vector<std::string> costGuards;
    auto costLeaseCount = std::count_if(plan.begin(),plan.end(),[](auto const& a){return a.resourceKind == "item-extended-cost.id";});
    if (static_cast<std::size_t>(costLeaseCount) != costs.size())
    { error="Extended-cost commit requires all resolved definitions"; return false; }
    for (auto const& cost : costs)
    {
        auto lease=std::find_if(plan.begin(),plan.end(),[&](auto const& a){return a.resourceKind == "item-extended-cost.id"
            && a.packageKey == cost.packageKey && a.symbol == cost.symbol && a.value == cost.id;});
        if (lease==plan.end()) { error="Resolved extended cost lacks its planned lease"; return false; }
        bool exists=false;
        if (!ContentExtendedCostServer::Check(cost,build.realmName,exists,error)) return false;
        costGuards.push_back(ContentExtendedCostServer::Condition(cost,build.realmName,exists));
    }
    auto tx = WorldDatabase.BeginTransaction();
    tx->Append("INSERT INTO content_manager_build_lock (id) VALUES (1) ON DUPLICATE KEY UPDATE id=1");
    for (auto const& condition : costGuards)
        tx->Append("INSERT INTO content_manager_build_lock (id) SELECT 1 WHERE NOT (" + condition + ")");
    for (auto const& baseline : baselines)
        tx->Append("INSERT INTO content_manager_build_lock (id) SELECT 1 WHERE NOT (" + ContentBaselineRegistry::Condition(baseline) + ")");
    for (auto const& row : plan)
    {
        bool existing = false;
        for (auto const& old : before)
            if (old.packageKey == row.packageKey && old.symbol == row.symbol && old.resourceKind == row.resourceKind)
                existing = true;
        if (existing)
            tx->Append("UPDATE content_manager_allocation SET last_build=" + std::to_string(build.buildNumber)
                + " WHERE realm_name=" + SqlText(row.realm) + " AND package_key=" + SqlText(row.packageKey)
                + " AND symbol=" + SqlText(row.symbol) + " AND resource_kind=" + SqlText(row.resourceKind) + " AND allocated_value="
                + std::to_string(row.value));
        else
            tx->Append("INSERT INTO content_manager_allocation (realm_name,package_key,symbol,resource_kind,"
            "allocated_value,state,first_build,last_build,baseline_sha256,descriptor_version,policy_version) VALUES ("
            + SqlText(row.realm) + "," + SqlText(row.packageKey) + "," + SqlText(row.symbol)
            + "," + SqlText(row.resourceKind) + "," + std::to_string(row.value) + ",'reserved',"
            + std::to_string(build.buildNumber) + "," + std::to_string(build.buildNumber) + ","
            + SqlText(row.baselineSha256) + ",1," + std::to_string(row.policyVersion) + ")");
    }
    tx->Append("INSERT INTO content_manager_build (build_number,realm_name,filename,package_count,file_count,state,sha256) VALUES ("
        + std::to_string(build.buildNumber) + "," + SqlText(build.realmName) + "," + SqlText(build.filename)
        + "," + std::to_string(build.packageCount) + "," + std::to_string(build.fileCount)
        + ",'STAGED'," + SqlText(build.sha256) + ")");
    tx->Append("INSERT INTO content_manager_server_build (build_number,bundle_filename,bundle_sha256,"
        "parity_filename,parity_sha256,server_state) VALUES (" + std::to_string(build.buildNumber)
        + "," + SqlText(server.bundleFilename) + "," + SqlText(server.bundleSha256)
        + "," + SqlText(server.parityFilename) + "," + SqlText(server.paritySha256) + ",'STAGED')");
    if (!build.clientRequirements.empty())
    {
        std::string requirements = "INSERT INTO content_manager_build_client_requirement (build_number,requirement) VALUES ";
        for (std::size_t i = 0; i < build.clientRequirements.size(); ++i)
        {
            if (i) requirements += ",";
            requirements += "(" + std::to_string(build.buildNumber) + "," + SqlText(build.clientRequirements[i]) + ")";
        }
        tx->Append(requirements);
    }
    WorldDatabase.DirectCommitTransaction(tx);
    auto check = WorldDatabase.Query("SELECT filename,sha256 FROM content_manager_build WHERE build_number="
        + std::to_string(build.buildNumber));
    if (!check || check->Fetch()[0].Get<std::string>() != build.filename
        || check->Fetch()[1].Get<std::string>() != build.sha256)
    { error = "Composed build transaction could not be verified; inspect SQL logs"; return false; }
    auto serverCheck = WorldDatabase.Query("SELECT bundle_filename,bundle_sha256,parity_filename,parity_sha256,"
        "server_state FROM content_manager_server_build WHERE build_number=" + std::to_string(build.buildNumber));
    if (!serverCheck || serverCheck->Fetch()[0].Get<std::string>() != server.bundleFilename
        || serverCheck->Fetch()[1].Get<std::string>() != server.bundleSha256
        || serverCheck->Fetch()[2].Get<std::string>() != server.parityFilename
        || serverCheck->Fetch()[3].Get<std::string>() != server.paritySha256
        || serverCheck->Fetch()[4].Get<std::string>() != "STAGED")
    { error = "Server artifact registry could not be verified; inspect SQL logs"; return false; }
    std::vector<ItemAllocation> after;
    if (!Read(build.realmName, after, error)) return false;
    for (auto const& row : plan)
    {
        bool found = false;
        for (auto const& saved : after)
            if (saved.packageKey == row.packageKey && saved.symbol == row.symbol && saved.resourceKind == row.resourceKind && saved.value == row.value
                && saved.lastBuild == build.buildNumber && saved.baselineSha256 == row.baselineSha256)
                found = true;
        if (!found) { error = "Committed allocation did not verify; inspect SQL logs"; return false; }
    }
    std::vector<std::string> savedRequirements;
    if (!ContentBuildRegistry().GetClientRequirements(build.buildNumber, savedRequirements, error)) return false;
    if (savedRequirements != build.clientRequirements)
    { error = "Client requirements could not be verified; inspect SQL logs"; return false; }
    return true;
}
