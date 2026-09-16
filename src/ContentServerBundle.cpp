#include "ContentServerBundle.h"
#include "ContentBuildHash.h"
#include "ServerTableDescriptor.h"
#include "third_party/json/json.hpp"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <tuple>

using json = nlohmann::json;

namespace
{
std::array<char const*, 14> const columns = {"class", "subclass", "SoundOverrideSubclass",
    "name", "displayid", "Quality", "InventoryType", "stackable", "bonding",
    "description", "Material", "sheath", "BagFamily", "Flags"};

char const* TargetCollation(std::string const& column)
{
    auto const* descriptor = FindServerTableDescriptor("item_template");
    if (!descriptor) throw std::runtime_error("item_template descriptor is unavailable");
    for (auto const& field : descriptor->columns)
        if (column == field.name) return field.collation;
    throw std::runtime_error("Unknown item_template column: " + column);
}

std::string SqlValue(json const& value, std::string const& column)
{
    if (auto const* collation = TargetCollation(column))
        return ContentServerBundle::SqlText(value.get<std::string>()) + " COLLATE " + collation;
    return value.dump();
}

json RowObject(ResolvedServerItem const& row)
{
    return {{"class", row.client.classID}, {"subclass", row.client.subclassID},
        {"SoundOverrideSubclass", row.client.soundOverrideSubclassID},
        {"name", row.server.name}, {"displayid", row.displayId},
        {"Quality", row.server.quality}, {"InventoryType", row.client.inventoryType},
        {"stackable", row.server.stackable}, {"bonding", row.server.bonding},
        {"description", row.server.description}, {"Material", row.client.material},
        {"sheath", row.client.sheatheType}, {"BagFamily", row.server.bagFamily}, {"Flags", 0}};
}

bool LessRow(ResolvedServerItem const& a, ResolvedServerItem const& b)
{
    return std::tie(a.packageKey, a.symbol) < std::tie(b.packageKey, b.symbol);
}

json ResourceObject(ItemAllocation const& allocation, std::vector<ResolvedServerItem> const& rows)
{
    if (allocation.resourceKind == "currency.known-bit" || allocation.resourceKind == "currency-category.id")
        return {{"package", allocation.packageKey}, {"symbol", allocation.symbol},
            {"resourceKind", allocation.resourceKind}, {"value", allocation.value},
            {"baselineSha256", allocation.baselineSha256}, {"allocationPolicyVersion", allocation.policyVersion},
            {"dbcDescriptorVersion", 1}};
    if (allocation.resourceKind != "item.id") throw std::runtime_error("Unknown parity resource");
    auto found = std::find_if(rows.begin(), rows.end(), [&](auto const& row) {
        return row.packageKey == allocation.packageKey && row.symbol == allocation.symbol;
    });
    json resource = {{"package", allocation.packageKey}, {"symbol", allocation.symbol},
        {"resourceKind", "item.id"}, {"value", allocation.value},
        {"itemDbcId", allocation.value}, {"baselineSha256", allocation.baselineSha256},
        {"allocationPolicyVersion", allocation.policyVersion}, {"dbcDescriptorVersion", 1},
        {"serverDescriptorVersion", found == rows.end() ? 0 : 1},
        {"itemTemplateEntry", found == rows.end() ? json(nullptr) : json(found->id)}};
    if (found != rows.end()) resource["packageVersion"] = found->packageVersion;
    return resource;
}
}

std::string ContentServerBundle::SqlText(std::string const& value)
{
    static char const digits[] = "0123456789abcdef";
    std::string sql = "CONVERT(X'";
    for (unsigned char byte : value) { sql += digits[byte >> 4]; sql += digits[byte & 15]; }
    return sql + "' USING utf8mb4)";
}

std::string ContentServerBundle::SqlIdentityText(std::string const& value)
{
    // Content Manager identity and provenance columns use utf8mb4_bin.
    return SqlText(value) + " COLLATE utf8mb4_bin";
}

std::string ContentServerBundle::RowJson(ResolvedServerItem const& row)
{
    return RowObject(row).dump();
}

std::string ContentServerBundle::ServerJson(std::string const& realm, std::vector<ResolvedServerItem> rows)
{
    std::sort(rows.begin(), rows.end(), LessRow);
    json artifact = {{"format", 1}, {"realm", realm}, {"table", "item_template"},
        {"descriptorVersion", 1}, {"rows", json::array()}};
    for (auto const& row : rows)
    {
        artifact["rows"].push_back({{"package", row.packageKey}, {"packageVersion", row.packageVersion},
            {"symbol", row.symbol}, {"resourceKind", "item.id"}, {"entry", row.id},
            {"fields", RowObject(row)}});
        if (row.currency.itemId)
        {
            artifact["format"] = std::max(artifact["format"].get<int>(), row.currency.categorySymbol.empty() ? 2 : 3);
            artifact["rows"].back()["currency"] = {{"symbol", row.currency.symbol},
                {"ID", row.currency.itemId}, {"ItemID", row.currency.itemId},
                {"CategoryID", row.currency.categoryId}, {"BitIndex", row.currency.bitIndex}};
            if (!row.currency.categorySymbol.empty())
                artifact["rows"].back()["currency"]["categorySymbol"] = row.currency.categorySymbol;
        }
    }
    return artifact.dump(2) + "\n";
}

std::string ContentServerBundle::ParityJson(std::string const& realm, std::uint32_t build,
    std::vector<ItemAllocation> const& allocations, std::vector<ResolvedServerItem> const& rows,
    std::string const& baselineSha256, std::string const& itemDbcSha256,
    std::string const& clientMpqSha256, std::string const& serverSha256, std::string const& currencyDbcSha256,
    std::string const& categoryDbcSha256, std::vector<ResolvedCurrencyCategory> categories, std::vector<ContentBaseline> baselines)
{
    auto sorted = allocations;
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) {
        return std::tie(a.packageKey, a.symbol) < std::tie(b.packageKey, b.symbol);
    });
    json artifact = {{"format", 1}, {"realm", realm}, {"build", build},
        {"baselineSha256", baselineSha256}, {"itemDbcSha256", itemDbcSha256},
        {"clientMpqSha256", clientMpqSha256}, {"serverBundleSha256", serverSha256},
        {"resources", json::array()}};
    for (auto const& allocation : sorted)
        artifact["resources"].push_back(ResourceObject(allocation, rows));
    if (!currencyDbcSha256.empty())
    {
        artifact["format"] = 2;
        artifact["currencyDbcSha256"] = currencyDbcSha256;
        artifact["currencies"] = json::array();
        auto items = rows;
        std::sort(items.begin(), items.end(), LessRow);
        for (auto const& row : items)
            if (row.currency.itemId)
                artifact["currencies"].push_back({{"package", row.packageKey}, {"symbol", row.currency.symbol},
                    {"itemSymbol", row.symbol}, {"ID", row.id}, {"ItemID", row.id},
                    {"CategoryID", row.currency.categoryId}, {"BitIndex", row.currency.bitIndex},
                    {"itemTemplateEntry", row.id}, {"BagFamily", row.server.bagFamily},
                    {"serverTable", "currencytypes_dbc"}, {"serverDescriptorVersion", 1}});
    }
    if (!categoryDbcSha256.empty())
    {
        artifact["format"] = 3;
        artifact["categoryDbcSha256"] = categoryDbcSha256;
        artifact["categoryFallback"] = "authored-locale-else-enUS;reserved-slots-empty;v1";
        artifact["categories"] = json::array();
        std::sort(categories.begin(), categories.end(), [](auto const& a, auto const& b) {
            return std::tie(a.packageKey,a.symbol) < std::tie(b.packageKey,b.symbol);
        });
        for (auto const& c : categories)
            artifact["categories"].push_back({{"package",c.packageKey},{"symbol",c.symbol},{"ID",c.id},{"name",c.names}});
        for (auto& currency : artifact["currencies"])
            for (auto const& row : rows)
                if (currency["package"] == row.packageKey && currency["symbol"] == row.currency.symbol
                    && !row.currency.categorySymbol.empty()) currency["categorySymbol"] = row.currency.categorySymbol;
    }
    if (!baselines.empty())
    {
        artifact["format"] = 3;
        artifact["baselines"] = json::array();
        std::sort(baselines.begin(), baselines.end(), [](auto const& a, auto const& b) { return a.table < b.table; });
        for (auto const& b : baselines)
            artifact["baselines"].push_back({{"table",b.table},{"clientBuild",b.clientBuild},
                {"descriptorVersion",b.descriptorVersion},{"sha256",b.hash}});
    }
    return artifact.dump(2) + "\n";
}

bool ContentServerBundle::ParseServer(std::string const& text, std::string const& realm,
    std::vector<ResolvedServerItem>& rows, std::string& error)
{
    rows.clear();
    try
    {
        auto artifact = json::parse(text);
        if (!artifact.is_object() || artifact.size() != 5 || (artifact.at("format") != 1 && artifact.at("format") != 2 && artifact.at("format") != 3)
            || artifact.at("realm") != realm || artifact.at("table") != "item_template"
            || artifact.at("descriptorVersion") != 1 || !artifact.at("rows").is_array())
            throw std::runtime_error("server bundle header or descriptor mismatch");
        std::set<std::pair<std::string, std::string>> identities;
        std::set<std::uint32_t> ids;
        for (auto const& source : artifact.at("rows"))
        {
            if (!source.is_object() || (source.size() != 6 && source.size() != 7) || source.at("resourceKind") != "item.id")
                throw std::runtime_error("invalid server bundle row");
            ResolvedServerItem row;
            row.packageKey = source.at("package").get<std::string>();
            row.packageVersion = source.at("packageVersion").get<std::string>();
            row.symbol = source.at("symbol").get<std::string>();
            if (!source.at("entry").is_number_unsigned()
                || source.at("entry").get<std::uint64_t>() > 0xffffffffULL)
                throw std::runtime_error("invalid server entry");
            row.id = source.at("entry").get<std::uint32_t>();
            auto const& fields = source.at("fields");
            if (!row.id || row.packageKey.empty() || row.symbol.empty() || row.packageVersion.empty()
                || !fields.is_object() || fields.size() != columns.size() || fields.at("Flags") != 0)
                throw std::runtime_error("invalid server bundle identity or fields");
            for (auto const* column : columns)
                if (!fields.contains(column)) throw std::runtime_error("server bundle field missing");
            auto unsignedValue = [&](char const* name, std::uint64_t maximum) -> std::uint32_t {
                auto const& value = fields.at(name);
                if (!value.is_number_unsigned() || value.get<std::uint64_t>() > maximum)
                    throw std::runtime_error(std::string("invalid unsigned server field: ") + name);
                return static_cast<std::uint32_t>(value.get<std::uint64_t>());
            };
            auto signedValue = [&](char const* name, std::int64_t minimum,
                std::int64_t maximum) -> std::int32_t {
                auto const& value = fields.at(name);
                if (!value.is_number_integer() || value.get<std::int64_t>() < minimum
                    || value.get<std::int64_t>() > maximum)
                    throw std::runtime_error(std::string("invalid signed server field: ") + name);
                return static_cast<std::int32_t>(value.get<std::int64_t>());
            };
            row.client.classID = unsignedValue("class", 255);
            row.client.subclassID = unsignedValue("subclass", 255);
            row.client.soundOverrideSubclassID = signedValue("SoundOverrideSubclass", -128, 127);
            row.client.inventoryType = unsignedValue("InventoryType", 255);
            row.client.material = signedValue("Material", -128, 127);
            row.client.sheatheType = unsignedValue("sheath", 255);
            row.displayId = unsignedValue("displayid", 0xffffffffULL);
            row.server.symbol = row.symbol;
            row.server.name = fields.at("name").get<std::string>();
            row.server.description = fields.at("description").get<std::string>();
            row.server.quality = static_cast<std::uint8_t>(unsignedValue("Quality", 7));
            row.server.stackable = signedValue("stackable", 1, 1000);
            row.server.bonding = static_cast<std::uint8_t>(unsignedValue("bonding", 5));
            row.server.bagFamily = signedValue("BagFamily", 0, 8192);
            if (source.contains("currency"))
            {
                auto const& currency = source.at("currency");
                if (!currency.is_object() || (currency.size() != 5 && currency.size() != 6) || currency.at("ID") != row.id
                    || currency.at("ItemID") != row.id || row.id > 0x7fffffffU
                    || !currency.at("CategoryID").is_number_unsigned()
                    || currency.at("CategoryID").get<std::uint64_t>() > 0x7fffffffULL
                    || !currency.at("BitIndex").is_number_unsigned()
                    || currency.at("BitIndex").get<std::uint64_t>() > 64)
                    throw std::runtime_error("Invalid currency server row");
                row.currency = {currency.at("symbol").get<std::string>(), row.id,
                    currency.at("CategoryID").get<std::uint32_t>(), currency.at("BitIndex").get<std::uint32_t>()};
                if (currency.contains("categorySymbol"))
                {
                    row.currency.categorySymbol = currency.at("categorySymbol").get<std::string>();
                    if (row.currency.categorySymbol.empty() || row.currency.categoryId > 65535)
                        throw std::runtime_error("Invalid category symbol/allocated ID");
                }
                if (row.currency.symbol.empty() || !row.currency.categoryId || !row.currency.bitIndex
                    || row.server.bagFamily != 8192)
                    throw std::runtime_error("Currency server row lacks token semantics");
            }
            auto const* descriptor = FindServerTableDescriptor("item_template");
            if (!descriptor || !row.displayId || row.server.name.empty() || row.server.name.size() > 255
                || row.server.description.size() > 255
                || row.server.name.find('\0') != std::string::npos
                || row.server.description.find('\0') != std::string::npos
                || row.server.quality > 7
                || row.server.stackable < 1 || row.server.stackable > 1000
                || row.server.bonding > 5 || (row.server.bagFamily != 0 && !row.currency.itemId)
                || row.client.classID > 255 || row.client.subclassID > 255
                || row.client.inventoryType > 255 || row.client.sheatheType > 255
                || row.client.soundOverrideSubclassID < -128 || row.client.soundOverrideSubclassID > 127
                || row.client.material < -128 || row.client.material > 127
                || !identities.emplace(row.packageKey, row.symbol).second || !ids.insert(row.id).second)
                throw std::runtime_error("server bundle row violates item_template descriptor");
            rows.push_back(std::move(row));
        }
        if (ServerJson(realm, rows) != text)
            throw std::runtime_error("server bundle is not in canonical generated form");
        return true;
    }
    catch (std::exception const& exception)
    { error = std::string("Invalid server bundle: ") + exception.what(); rows.clear(); return false; }
}

bool ContentServerBundle::VerifyParity(std::string const& text, std::string const& realm,
    std::uint32_t build, std::string const& baselineSha256, std::string const& clientMpqSha256,
    std::string const& serverSha256, std::vector<ResolvedServerItem> const& rows,
    std::vector<ItemAllocation> const& allocations, std::string& error)
{
    try
    {
        auto actual = json::parse(text);
        auto itemSha = actual.at("itemDbcSha256").get<std::string>();
        auto currencySha = actual.value("currencyDbcSha256", std::string());
        bool currencyRows = std::any_of(rows.begin(), rows.end(), [](auto const& r) { return r.currency.itemId != 0; });
        if (currencyRows != !currencySha.empty() || (currencyRows && !ContentBuildHash::Valid(currencySha)))
            throw std::runtime_error("Currency DBC hash missing or invalid");
        std::set<std::uint32_t> currencyBits;
        for (auto const& row : rows)
            if (row.currency.itemId)
            {
                auto found = std::find_if(allocations.begin(), allocations.end(), [&](auto const& a) {
                    return a.resourceKind == "currency.known-bit" && a.packageKey == row.packageKey
                        && a.symbol == row.currency.symbol && a.value == row.currency.bitIndex;
                });
                if (found == allocations.end() || !ContentBuildHash::Valid(found->baselineSha256)
                    || !currencyBits.insert(row.currency.bitIndex).second)
                    throw std::runtime_error("Currency parity allocation missing or duplicate");
            }
        auto categorySha = actual.value("categoryDbcSha256", std::string());
        std::vector<ResolvedCurrencyCategory> categories;
        std::set<std::pair<std::string,std::string>> categoryIdentities;
        std::set<std::uint32_t> categoryIds;
        if (!categorySha.empty())
        {
            if (!ContentBuildHash::Valid(categorySha) || !actual.at("categories").is_array())
                throw std::runtime_error("Invalid category artifact provenance");
            for (auto const& source : actual.at("categories"))
            {
                ResolvedCurrencyCategory c{source.at("package").get<std::string>(),source.at("symbol").get<std::string>(),
                    source.at("ID").get<std::uint32_t>(), source.at("name").get<std::map<std::string,std::string>>()};
                CurrencyCategoryDbcComposer::ValidateNames(c.names);
                if (!c.id || c.id > 65535 || !categoryIdentities.emplace(c.packageKey,c.symbol).second || !categoryIds.insert(c.id).second)
                    throw std::runtime_error("Duplicate/invalid category identity");
                auto lease = std::find_if(allocations.begin(),allocations.end(),[&](auto const& a) {
                    return a.resourceKind == "currency-category.id" && a.packageKey == c.packageKey && a.symbol == c.symbol && a.value == c.id;
                });
                if (lease == allocations.end() || !ContentBuildHash::Valid(lease->baselineSha256))
                    throw std::runtime_error("Category lease missing");
                if (std::none_of(rows.begin(),rows.end(),[&](auto const& row) {
                    return row.packageKey == c.packageKey && row.currency.categorySymbol == c.symbol && row.currency.categoryId == c.id;
                })) throw std::runtime_error("Unreferenced category in parity manifest");
                categories.push_back(c);
            }
            if (categories.empty()) throw std::runtime_error("Empty category artifact");
        }
        for (auto const& row : rows)
            if (!row.currency.categorySymbol.empty() && std::none_of(categories.begin(),categories.end(),[&](auto const& c) {
                return c.packageKey == row.packageKey && c.symbol == row.currency.categorySymbol && c.id == row.currency.categoryId;
            })) throw std::runtime_error("Currency category relationship missing or mismatched");
        for (auto const& a : allocations)
            if (a.resourceKind == "currency-category.id" && !categoryIdentities.count({a.packageKey,a.symbol}))
                throw std::runtime_error("Orphan category lease in manifest");
        std::vector<ContentBaseline> baselines;
        if (actual.contains("baselines"))
        {
            std::set<std::string> tables;
            for (auto const& source : actual.at("baselines"))
            {
                ContentBaseline b; b.table = source.at("table").get<std::string>();
                b.clientBuild = source.at("clientBuild").get<std::uint32_t>();
                b.descriptorVersion = source.at("descriptorVersion").get<std::uint32_t>();
                b.hash = source.at("sha256").get<std::string>();
                auto d = FindDbcDescriptor(b.clientBuild,b.table);
                if (!d || d->version != b.descriptorVersion || !ContentBuildHash::Valid(b.hash) || !tables.insert(b.table).second)
                    throw std::runtime_error("Invalid baseline snapshot");
                if (b.table == "Item" && b.hash != baselineSha256) throw std::runtime_error("Item baseline snapshot mismatch");
                baselines.push_back(b);
            }
            std::set<std::string> expected{"Item"};
            if (currencyRows) expected.insert("CurrencyTypes");
            if (!categories.empty()) expected.insert("CurrencyCategory");
            if (tables != expected) throw std::runtime_error("Baseline snapshot set mismatch");
        }
        if (!ContentBuildHash::Valid(itemSha) || actual != json::parse(ParityJson(realm, build,
            allocations, rows, baselineSha256, itemSha, clientMpqSha256, serverSha256, currencySha, categorySha, categories, baselines)))
            throw std::runtime_error("manifest values differ from build, allocation, or server bundle");
        return true;
    }
    catch (std::exception const& exception)
    { error = std::string("Parity manifest mismatch: ") + exception.what(); return false; }
}

std::string ContentServerBundle::InsertSql(ResolvedServerItem const& row)
{
    auto fields = RowObject(row);
    std::string sql = "INSERT INTO item_template (`entry`";
    for (auto const* column : columns) sql += ",`" + std::string(column) + "`";
    sql += ") VALUES (" + std::to_string(row.id);
    for (auto const* column : columns) sql += "," + SqlValue(fields.at(column), column);
    return sql + ")";
}

std::string ContentServerBundle::UpdateSql(ResolvedServerItem const& row, std::string const& previousRowJson)
{
    auto fields = RowObject(row);
    auto previous = json::parse(previousRowJson);
    std::string sql = "UPDATE item_template SET ";
    bool first = true;
    for (auto const* column : columns)
    {
        if (!first) sql += ",";
        first = false;
        sql += "`" + std::string(column) + "`=" + SqlValue(fields.at(column), column);
    }
    sql += " WHERE `entry`=" + std::to_string(row.id);
    for (auto const* column : columns)
        sql += " AND `" + std::string(column) + "`=" + SqlValue(previous.at(column), column);
    return sql;
}

std::string ContentServerBundle::MatchSql(ResolvedServerItem const& row, std::string const& alias)
{
    auto fields = RowObject(row);
    std::string sql = alias + ".`entry`=" + std::to_string(row.id);
    for (auto const* column : columns)
        sql += " AND " + alias + ".`" + std::string(column) + "`=" + SqlValue(fields.at(column), column);
    return sql;
}
