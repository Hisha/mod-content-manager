#include "ContentCurrencyServer.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include <algorithm>

namespace
{
std::string N(std::uint32_t value) { return std::to_string(value); }
std::string T(std::string const& value) { return ContentServerBundle::SqlIdentityText(value); }
std::string Conflict(ResolvedServerItem const& row)
{
    return "(c.ID=" + N(row.id) + " OR c.ItemID=" + N(row.id)
        + " OR c.BitIndex=" + N(row.currency.bitIndex) + ")";
}
std::string Match(ResolvedServerItem const& row, std::string const& realm)
{
    return "c.ID=" + N(row.id) + " AND c.ItemID=" + N(row.id)
        + " AND c.CategoryID=" + N(row.currency.categoryId) + " AND c.BitIndex=" + N(row.currency.bitIndex)
        + " AND o.realm_name=" + T(realm) + " AND o.package_key=" + T(row.packageKey)
        + " AND o.item_symbol=" + T(row.symbol) + " AND o.symbol=" + T(row.currency.symbol)
        + " AND o.category_id=c.CategoryID AND o.bit_index=c.BitIndex";
}
}

bool ContentCurrencyServer::ValidateSchema(std::string& error)
{
    auto schema = WorldDatabase.Query("SELECT COLUMN_NAME,COLUMN_TYPE,IS_NULLABLE,ORDINAL_POSITION "
        "FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='currencytypes_dbc' "
        "ORDER BY ORDINAL_POSITION");
    char const* names[] = {"ID", "ItemID", "CategoryID", "BitIndex"};
    if (!schema || schema->GetRowCount() != 4)
    { error = "currencytypes_dbc requires the verified four-column build-12340 schema"; return false; }
    unsigned i = 0;
    do
    {
        auto f = schema->Fetch();
        if (f[0].Get<std::string>() != names[i] || f[1].Get<std::string>() != "int"
            || f[2].Get<std::string>() != "NO" || f[3].Get<std::uint64_t>() != i + 1)
        { error = "currencytypes_dbc schema/column order mismatch"; return false; }
        ++i;
    } while (schema->NextRow());
    auto engine = WorldDatabase.Query("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() "
        "AND TABLE_NAME IN ('currencytypes_dbc','content_manager_currency_owner','item_template',"
        "'content_manager_item_owner','content_manager_allocation','content_manager_server_build',"
        "'content_manager_build_lock') AND ENGINE='InnoDB'");
    if (!engine || engine->Fetch()[0].Get<std::uint64_t>() != 7)
    { error = "Phase 4 requires all deployment/ownership tables to use InnoDB; apply Phase 4 SQL"; return false; }
    return true;
}

bool ContentCurrencyServer::Occupancy(std::string const& realm, std::vector<ItemAllocation> const& leases,
    std::set<std::uint32_t>& bits, std::set<std::uint32_t>& ids, std::string& error)
{
    bits.clear(); ids.clear();
    if (!ValidateSchema(error)) return false;
    auto result = WorldDatabase.Query("SELECT c.ID,c.ItemID,c.CategoryID,c.BitIndex,o.realm_name,o.package_key,"
        "o.item_symbol,o.symbol,o.category_id,o.bit_index FROM currencytypes_dbc c LEFT JOIN "
        "content_manager_currency_owner o ON o.entry=c.ID UNION ALL SELECT NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL");
    if (!result) { error = "Cannot inspect server CurrencyTypes occupancy"; return false; }
    std::uint32_t greatestId = 0, itemAtGreatestId = 0, greatestItem = 0;
    std::set<std::uint32_t> seenItems, seenBits;
    do
    {
        auto f = result->Fetch();
        if (f[0].IsNull()) continue;
        for (unsigned i = 0; i < 4; ++i)
            if (f[i].Get<std::int32_t>() <= 0)
            { error = "Invalid signed currencytypes_dbc value"; return false; }
        auto id = static_cast<std::uint32_t>(f[0].Get<std::int32_t>());
        auto item = static_cast<std::uint32_t>(f[1].Get<std::int32_t>());
        auto category = static_cast<std::uint32_t>(f[2].Get<std::int32_t>());
        auto bit = static_cast<std::uint32_t>(f[3].Get<std::int32_t>());
        if (bit > 64 || !seenItems.insert(item).second || !seenBits.insert(bit).second)
        { error = "Duplicate/invalid server CurrencyTypes item or known bit"; return false; }
        if (id > greatestId) { greatestId = id; itemAtGreatestId = item; }
        greatestItem = std::max(greatestItem, item);
        if (!f[4].IsNull())
        {
            auto package = f[5].Get<std::string>();
            auto hasLease = [&](std::string const& symbol, char const* kind, std::uint32_t value) {
                return std::any_of(leases.begin(), leases.end(), [&](auto const& lease) {
                    return lease.realm == realm && lease.packageKey == package && lease.symbol == symbol
                        && lease.resourceKind == kind && lease.value == value;
                });
            };
            if (f[4].Get<std::string>() != realm || id != item
                || category != f[8].Get<std::uint32_t>() || bit != f[9].Get<std::uint32_t>()
                || !hasLease(f[6].Get<std::string>(), "item.id", item)
                || !hasLease(f[7].Get<std::string>(), "currency.known-bit", bit))
            { error = "Owned currencytypes_dbc drift or allocation conflict"; return false; }
        }
        else { bits.insert(bit); ids.insert(id); ids.insert(item); }
    } while (result->NextRow());
    // Reference AC loader orders by ID but indexes by ItemID; reject unsafe external overlays.
    if (itemAtGreatestId != greatestItem)
    { error = "currencytypes_dbc ID ordering is unsafe for the reference AzerothCore DBC loader"; return false; }
    auto orphans = WorldDatabase.Query("SELECT COUNT(*) FROM content_manager_currency_owner o LEFT JOIN "
        "currencytypes_dbc c ON c.ID=o.entry WHERE c.ID IS NULL");
    if (!orphans || orphans->Fetch()[0].Get<std::uint64_t>())
    { error = "Currency ownership table is unavailable or has missing owned rows"; return false; }
    return true;
}

std::string ContentCurrencyServer::Condition(ResolvedServerItem const& row, std::string const& realm, bool exists)
{
    if (!exists)
        return "NOT EXISTS(SELECT 1 FROM currencytypes_dbc c WHERE " + Conflict(row)
            + ") AND NOT EXISTS(SELECT 1 FROM content_manager_currency_owner o WHERE o.entry=" + N(row.id) + ")";
    return "EXISTS(SELECT 1 FROM currencytypes_dbc c JOIN content_manager_currency_owner o ON o.entry=c.ID WHERE "
        + Match(row, realm) + ") AND (SELECT COUNT(*) FROM currencytypes_dbc c WHERE " + Conflict(row) + ")=1";
}

bool ContentCurrencyServer::Check(ResolvedServerItem const& row, std::string const& realm, bool& exists, std::string& error)
{
    auto result = WorldDatabase.Query("SELECT (" + Condition(row, realm, false) + "),(" + Condition(row, realm, true) + ")");
    if (!result || (!result->Fetch()[0].Get<std::uint64_t>() && !result->Fetch()[1].Get<std::uint64_t>()))
    { error = "Currency row has an unowned collision or ownership drift: " + N(row.id); return false; }
    exists = result->Fetch()[1].Get<std::uint64_t>() != 0;
    return true;
}

std::vector<std::string> ContentCurrencyServer::ApplySql(ResolvedServerItem const& row, std::string const& realm,
    bool exists, std::uint32_t build, std::string const& hash, std::uint32_t previousCategory)
{
    if (exists)
    {
        auto previous = row;
        if (previousCategory) previous.currency.categoryId = previousCategory;
        return {"UPDATE currencytypes_dbc c JOIN content_manager_currency_owner o ON o.entry=c.ID "
            "SET c.CategoryID=" + N(row.currency.categoryId) + ",o.category_id=" + N(row.currency.categoryId)
            + ",o.applied_build=" + N(build) + ",o.artifact_sha256=" + T(hash) + " WHERE " + Match(previous, realm)};
    }
    return {"INSERT INTO currencytypes_dbc (ID,ItemID,CategoryID,BitIndex) VALUES (" + N(row.id) + ","
        + N(row.id) + "," + N(row.currency.categoryId) + "," + N(row.currency.bitIndex) + ")",
        "INSERT INTO content_manager_currency_owner (entry,realm_name,package_key,item_symbol,symbol,category_id,"
        "bit_index,applied_build,artifact_sha256) VALUES (" + N(row.id) + "," + T(realm) + "," + T(row.packageKey)
        + "," + T(row.symbol) + "," + T(row.currency.symbol) + "," + N(row.currency.categoryId) + ","
        + N(row.currency.bitIndex) + "," + N(build) + "," + T(hash) + ")"};
}

bool ContentCurrencyServer::Verify(ResolvedServerItem const& row, std::string const& realm,
    std::uint32_t build, std::string const& hash, std::string& error)
{
    auto result = WorldDatabase.Query("SELECT (" + Condition(row, realm, true)
        + ") AND EXISTS(SELECT 1 FROM content_manager_currency_owner WHERE entry=" + N(row.id)
        + " AND applied_build=" + N(build) + " AND artifact_sha256=" + T(hash) + ")");
    if (!result || !result->Fetch()[0].Get<std::uint64_t>())
    { error = "Currency post-apply provenance check failed"; return false; }
    return true;
}

// Only an exactly owned, undrifted row may change its category. All other identity/bit checks stay exact.
bool ContentCurrencyServer::Prepare(ResolvedServerItem const& row, std::string const& realm, bool& exists,
    ResolvedServerItem& previous, std::string& error)
{
    previous = row;
    auto q = WorldDatabase.Query("SELECT o.category_id FROM (SELECT 1) seed LEFT JOIN content_manager_currency_owner o ON o.entry=" + N(row.id));
    if (!q) { error = "Cannot inspect currency ownership snapshot"; return false; }
    if (!q->Fetch()[0].IsNull()) previous.currency.categoryId = q->Fetch()[0].Get<std::uint32_t>();
    return Check(previous, realm, exists, error);
}

std::string ContentCurrencyServer::CategoryCondition(std::string const& realm, std::string const& package, std::uint32_t category)
{
    return "NOT EXISTS(SELECT 1 FROM currencytypes_dbc c LEFT JOIN content_manager_currency_owner o ON o.entry=c.ID "
        "WHERE c.CategoryID=" + N(category) + " AND (o.entry IS NULL OR o.realm_name<>" + T(realm)
        + " OR o.package_key<>" + T(package) + " OR c.ID<>c.ItemID OR o.category_id<>c.CategoryID OR o.bit_index<>c.BitIndex))";
}

bool ContentCurrencyServer::CategoryOccupancy(std::string const& realm, std::vector<ItemAllocation> const& leases,
    std::set<std::uint32_t>& categories, std::string& error)
{
    std::set<std::uint32_t> bits, ids;
    if (!Occupancy(realm, leases, bits, ids, error)) return false;
    categories.clear();
    auto q = WorldDatabase.Query("SELECT c.CategoryID,o.realm_name,o.package_key FROM currencytypes_dbc c LEFT JOIN "
        "content_manager_currency_owner o ON o.entry=c.ID UNION ALL SELECT NULL,NULL,NULL");
    if (!q) { error = "Cannot inspect category references in server CurrencyTypes"; return false; }
    do
    {
        auto f = q->Fetch(); if (f[0].IsNull()) continue;
        auto id = f[0].Get<std::uint32_t>();
        bool owned = !f[1].IsNull() && f[1].Get<std::string>() == realm && std::any_of(leases.begin(), leases.end(), [&](auto const& a) {
            return a.realm == realm && a.packageKey == f[2].Get<std::string>() && a.resourceKind == "currency-category.id" && a.value == id;
        });
        if (!owned) categories.insert(id);
    } while (q->NextRow());
    return true;
}
