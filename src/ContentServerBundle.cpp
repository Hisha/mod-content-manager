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

bool IsTextColumn(std::string const& column)
{
    return column == "name" || column == "description";
}

std::string SqlValue(json const& value, std::string const& column)
{
    if (IsTextColumn(column)) return ContentServerBundle::SqlText(value.get<std::string>());
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
        artifact["rows"].push_back({{"package", row.packageKey}, {"packageVersion", row.packageVersion},
            {"symbol", row.symbol}, {"resourceKind", "item.id"}, {"entry", row.id},
            {"fields", RowObject(row)}});
    return artifact.dump(2) + "\n";
}

std::string ContentServerBundle::ParityJson(std::string const& realm, std::uint32_t build,
    std::vector<ItemAllocation> const& allocations, std::vector<ResolvedServerItem> const& rows,
    std::string const& baselineSha256, std::string const& itemDbcSha256,
    std::string const& clientMpqSha256, std::string const& serverSha256)
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
    return artifact.dump(2) + "\n";
}

bool ContentServerBundle::ParseServer(std::string const& text, std::string const& realm,
    std::vector<ResolvedServerItem>& rows, std::string& error)
{
    rows.clear();
    try
    {
        auto artifact = json::parse(text);
        if (!artifact.is_object() || artifact.size() != 5 || artifact.at("format") != 1
            || artifact.at("realm") != realm || artifact.at("table") != "item_template"
            || artifact.at("descriptorVersion") != 1 || !artifact.at("rows").is_array())
            throw std::runtime_error("server bundle header or descriptor mismatch");
        std::set<std::pair<std::string, std::string>> identities;
        std::set<std::uint32_t> ids;
        for (auto const& source : artifact.at("rows"))
        {
            if (!source.is_object() || source.size() != 6 || source.at("resourceKind") != "item.id")
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
            row.server.bagFamily = signedValue("BagFamily", 0, 0);
            auto const* descriptor = FindServerTableDescriptor("item_template");
            if (!descriptor || !row.displayId || row.server.name.empty() || row.server.name.size() > 255
                || row.server.description.size() > 255
                || row.server.name.find('\0') != std::string::npos
                || row.server.description.find('\0') != std::string::npos
                || row.server.quality > 7
                || row.server.stackable < 1 || row.server.stackable > 1000
                || row.server.bonding > 5 || row.server.bagFamily != 0
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
        if (!ContentBuildHash::Valid(itemSha) || actual != json::parse(ParityJson(realm, build,
            allocations, rows, baselineSha256, itemSha, clientMpqSha256, serverSha256)))
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
