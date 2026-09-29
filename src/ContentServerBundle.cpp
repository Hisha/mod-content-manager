#include "ContentServerBundle.h"
#include "ContentBuildHash.h"
#include "ServerTableDescriptor.h"
#include "WorldMapDbcComposer.h"
#include "third_party/json/json.hpp"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <tuple>

using json = nlohmann::json;

namespace {
std::array<char const *, 14> const columns = {
	"class",		 "subclass",  "SoundOverrideSubclass",
	"name",			 "displayid", "Quality",
	"InventoryType", "stackable", "bonding",
	"description",	 "Material",  "sheath",
	"BagFamily",	 "Flags"};

char const *TargetCollation(std::string const &column) {
    auto const* descriptor = FindServerTableDescriptor("item_template");
	if (!descriptor)
		throw std::runtime_error("item_template descriptor is unavailable");
    for (auto const& field : descriptor->columns)
		if (column == field.name)
			return field.collation;
    throw std::runtime_error("Unknown item_template column: " + column);
}

std::string SqlValue(json const &value, std::string const &column) {
    if (auto const* collation = TargetCollation(column))
		return ContentServerBundle::SqlText(value.get<std::string>()) +
			   " COLLATE " + collation;
    return value.dump();
}

json RowObject(ResolvedServerItem const &row) {
	return {{"class", row.client.classID},
			{"subclass", row.client.subclassID},
        {"SoundOverrideSubclass", row.client.soundOverrideSubclassID},
			{"name", row.server.name},
			{"displayid", row.displayId},
			{"Quality", row.server.quality},
			{"InventoryType", row.client.inventoryType},
			{"stackable", row.server.stackable},
			{"bonding", row.server.bonding},
			{"description", row.server.description},
			{"Material", row.client.material},
			{"sheath", row.client.sheatheType},
			{"BagFamily", row.server.bagFamily},
			{"Flags", 0}};
}

bool LessRow(ResolvedServerItem const &a, ResolvedServerItem const &b) {
    return std::tie(a.packageKey, a.symbol) < std::tie(b.packageKey, b.symbol);
}

json CostObjects(std::vector<ResolvedExtendedCost> costs) {
    std::sort(costs.begin(), costs.end(), [](auto const& a, auto const& b) {
		return std::tie(a.packageKey, a.symbol) <
			   std::tie(b.packageKey, b.symbol);
    });
    json result = json::array();
    std::set<std::pair<std::string,std::string>> symbols;
    std::set<std::uint32_t> ids;
	for (auto &c : costs) {
        ItemExtendedCostDbc::Words(c);
		if (!symbols.emplace(c.packageKey, c.symbol).second ||
			!ids.insert(c.id).second)
            throw std::runtime_error("Duplicate extended-cost identity");
		std::sort(c.requirements.begin(), c.requirements.end(),
				  [](auto const &a, auto const &b) {
					  return std::tie(a.packageKey, a.symbol) <
							 std::tie(b.packageKey, b.symbol);
        });
        json requirements = json::array();
        for (auto const& q : c.requirements)
			requirements.push_back({{"package", q.packageKey},
									{"symbol", q.symbol},
									{"itemId", q.itemId},
									{"count", q.count}});
		result.push_back({{"package", c.packageKey},
						  {"packageVersion", c.packageVersion},
						  {"symbol", c.symbol},
						  {"id", c.id},
						  {"requirements", requirements},
						  {"honorPoints", c.honorPoints},
						  {"arenaPoints", c.arenaPoints},
						  {"arenaBracket", c.arenaBracket},
						  {"requiredArenaRating", c.requiredArenaRating}});
    }
    return result;
}

std::vector<ResolvedExtendedCost> ParseCosts(json const &source) {
	if (!source.is_array() || source.empty())
		throw std::runtime_error("Empty/invalid extended-cost artifact");
    std::vector<ResolvedExtendedCost> result;
    auto number = [](json const& v) {
        if (!v.is_number_unsigned() || v.get<std::uint64_t>() > 0xffffffffULL)
            throw std::runtime_error("Invalid extended-cost integer");
        return v.get<std::uint32_t>();
    };
	for (auto const &v : source) {
        ResolvedExtendedCost c;
		c.packageKey = v.at("package").get<std::string>();
		c.packageVersion = v.at("packageVersion").get<std::string>();
		c.symbol = v.at("symbol").get<std::string>();
		c.id = number(v.at("id"));
		c.honorPoints = number(v.at("honorPoints"));
		c.arenaPoints = number(v.at("arenaPoints"));
		c.arenaBracket = number(v.at("arenaBracket"));
		c.requiredArenaRating = number(v.at("requiredArenaRating"));
		if (c.packageVersion.empty() || !v.at("requirements").is_array())
			throw std::runtime_error("Invalid cost requirements/version");
        for (auto const& q : v.at("requirements"))
			c.requirements.push_back({q.at("package").get<std::string>(),
									  q.at("symbol").get<std::string>(),
									  number(q.at("itemId")),
									  number(q.at("count"))});
        result.push_back(c);
    }
	if (CostObjects(result) != source)
		throw std::runtime_error("Noncanonical extended-cost definitions");
    return result;
}

json ResourceObject(ItemAllocation const &allocation,
					std::vector<ResolvedServerItem> const &rows) {
	if (allocation.resourceKind == "spell.id")
		return {{"package",allocation.packageKey},{"symbol",allocation.symbol},{"resourceKind",allocation.resourceKind},{"value",allocation.value},{"baselineSha256",allocation.baselineSha256},{"allocationPolicyVersion",allocation.policyVersion},{"dbcDescriptorVersion",1},{"serverDescriptorVersion",1}};
	if (allocation.resourceKind == "currency.known-bit" ||
		allocation.resourceKind == "currency-category.id" ||
		allocation.resourceKind == "item-extended-cost.id")
		return {{"package", allocation.packageKey},
				{"symbol", allocation.symbol},
				{"resourceKind", allocation.resourceKind},
				{"value", allocation.value},
				{"baselineSha256", allocation.baselineSha256},
				{"allocationPolicyVersion", allocation.policyVersion},
            {"dbcDescriptorVersion", 1}};
	if (allocation.resourceKind == "creature-template.id" ||
		allocation.resourceKind == "gameobject-template.id" ||
		allocation.resourceKind == "creature-spawn.guid")
		return {{"package", allocation.packageKey},
				{"symbol", allocation.symbol},
				{"resourceKind", allocation.resourceKind},
				{"value", allocation.value},
				{"baselineSha256", allocation.baselineSha256},
				{"allocationPolicyVersion", allocation.policyVersion},
				{"serverDescriptorVersion",
				 ContentManagedServer::DescriptorVersion}};
	for (auto const& table : WorldMapDbcTables())
		if (allocation.resourceKind == WorldMapDbcComposer::ResourceKind(table))
			// A native world map row is client-only: the lease plus the composed
			// file hash is the whole durable record, so there is no server row to
			// mirror.
			return {{"package", allocation.packageKey},
					{"symbol", allocation.symbol},
					{"resourceKind", allocation.resourceKind},
					{"value", allocation.value},
					{"baselineSha256", allocation.baselineSha256},
					{"allocationPolicyVersion", allocation.policyVersion},
					{"dbcDescriptorVersion", 1}};
	if (allocation.resourceKind != "item.id")
		throw std::runtime_error("Unknown parity resource");
    auto found = std::find_if(rows.begin(), rows.end(), [&](auto const& row) {
		return row.packageKey == allocation.packageKey &&
			   row.symbol == allocation.symbol;
    });
	json resource = {{"package", allocation.packageKey},
					 {"symbol", allocation.symbol},
					 {"resourceKind", "item.id"},
					 {"value", allocation.value},
					 {"itemDbcId", allocation.value},
					 {"baselineSha256", allocation.baselineSha256},
					 {"allocationPolicyVersion", allocation.policyVersion},
					 {"dbcDescriptorVersion", 1},
        {"serverDescriptorVersion", found == rows.end() ? 0 : 1},
					 {"itemTemplateEntry",
					  found == rows.end() ? json(nullptr) : json(found->id)}};
	if (found != rows.end())
		resource["packageVersion"] = found->packageVersion;
    return resource;
}

void ValidateSpellStructure(ResolvedSpell const&s)
{
	if(s.packageKey.empty()||s.packageVersion.empty()||s.symbol.empty()||s.profile.empty()||!s.id||!s.copyFrom||!s.iconCopyFromSpell||s.words[0]!=s.id)
		throw std::runtime_error("Invalid managed spell identity");
}
json SpellObjects(std::vector<ResolvedSpell> spells,bool validateCurrentPolicy=true)
{
	std::sort(spells.begin(),spells.end(),[](auto const&a,auto const&b){return std::tie(a.packageKey,a.symbol)<std::tie(b.packageKey,b.symbol);});
	json out=json::array(); std::set<std::uint32_t> ids;std::set<std::pair<std::string,std::string>> identities;
	for(auto const&s:spells){ValidateSpellStructure(s);if((validateCurrentPolicy&&!SpellDbcComposer::BehaviorMatches(s))||!ids.insert(s.id).second||!identities.emplace(s.packageKey,s.symbol).second)throw std::runtime_error("Invalid managed spell artifact");out.push_back({{"package",s.packageKey},{"packageVersion",s.packageVersion},{"symbol",s.symbol},{"resourceKind","spell.id"},{"id",s.id},{"copyFrom",s.copyFrom},{"iconCopyFromSpell",s.iconCopyFromSpell},{"profile",s.profile},{"words",s.words},{"localized",s.localized}});}
	return out;
}
std::vector<ResolvedSpell> ParseSpells(json const& source)
{
	if(!source.is_array()||source.empty())throw std::runtime_error("Empty/invalid managed spell artifact");std::vector<ResolvedSpell> out;
	for(auto const&v:source){ResolvedSpell s;s.packageKey=v.at("package");s.packageVersion=v.at("packageVersion");s.symbol=v.at("symbol");s.profile=v.at("profile");s.id=v.at("id");s.copyFrom=v.at("copyFrom");s.iconCopyFromSpell=v.at("iconCopyFromSpell");s.words=v.at("words").get<std::array<std::uint32_t,234>>();s.localized=v.at("localized").get<std::array<std::array<std::string,16>,4>>();if(v.at("resourceKind")!="spell.id")throw std::runtime_error("Invalid managed spell row");ValidateSpellStructure(s);out.push_back(std::move(s));}
	if(SpellObjects(out,false)!=source)throw std::runtime_error("Noncanonical managed spell definitions");return out;
}
} // namespace

std::string ContentServerBundle::SqlText(std::string const &value) {
    static char const digits[] = "0123456789abcdef";
    std::string sql = "CONVERT(X'";
	for (unsigned char byte : value) {
		sql += digits[byte >> 4];
		sql += digits[byte & 15];
	}
    return sql + "' USING utf8mb4)";
}

std::string ContentServerBundle::SqlIdentityText(std::string const &value) {
    // Content Manager identity and provenance columns use utf8mb4_bin.
    return SqlText(value) + " COLLATE utf8mb4_bin";
}

std::string ContentServerBundle::RowJson(ResolvedServerItem const &row) {
    return RowObject(row).dump();
}

std::string ContentServerBundle::ServerJson(
	std::string const &realm, std::vector<ResolvedServerItem> rows,
	std::vector<ResolvedExtendedCost> costs,
	std::vector<ResolvedVendorRow> vendors,
	std::vector<ResolvedCreatureTemplate> creatures,
	std::vector<ResolvedGameObjectTemplate> gameObjects,
	std::vector<ResolvedCreatureSpawn> spawns,
	std::vector<ResolvedSpell> spells, bool validateCurrentSpellPolicy) {
    std::sort(rows.begin(), rows.end(), LessRow);
	json artifact = {{"format", 1},
					 {"realm", realm},
					 {"table", "item_template"},
					 {"descriptorVersion", 1},
					 {"rows", json::array()}};
	for (auto const &row : rows) {
		artifact["rows"].push_back({{"package", row.packageKey},
									{"packageVersion", row.packageVersion},
									{"symbol", row.symbol},
									{"resourceKind", "item.id"},
									{"entry", row.id},
            {"fields", RowObject(row)}});
		if (row.currency.itemId) {
			artifact["format"] =
				std::max(artifact["format"].get<int>(),
						 row.currency.categorySymbol.empty() ? 2 : 3);
			artifact["rows"].back()["currency"] = {
				{"symbol", row.currency.symbol},
				{"ID", row.currency.itemId},
				{"ItemID", row.currency.itemId},
				{"CategoryID", row.currency.categoryId},
				{"BitIndex", row.currency.bitIndex}};
            if (!row.currency.categorySymbol.empty())
				artifact["rows"].back()["currency"]["categorySymbol"] =
					row.currency.categorySymbol;
		}
        }
	if (!costs.empty()) {
		artifact["format"] = 4;
		artifact["extendedCosts"] = CostObjects(costs);
	}
	if (!vendors.empty()) {
		artifact["format"] = 5;
		artifact["vendorRows"] = ContentVendorServer::Objects(vendors);
	}
	if (!creatures.empty() || !gameObjects.empty() || !spawns.empty()) {
		artifact["format"] = 6;
		artifact["managedServer"] =
			ContentManagedServer::Objects(creatures, gameObjects, spawns);
    }
	if (!spells.empty()) { artifact["format"] = 7; artifact["spells"] = SpellObjects(spells,validateCurrentSpellPolicy); }
    return artifact.dump(2) + "\n";
}

std::string ContentServerBundle::ParityJson(
	std::string const &realm, std::uint32_t build,
	std::vector<ItemAllocation> const &allocations,
	std::vector<ResolvedServerItem> const &rows,
    std::string const& baselineSha256, std::string const& itemDbcSha256,
	std::string const &clientMpqSha256, std::string const &serverSha256,
	std::string const &currencyDbcSha256, std::string const &categoryDbcSha256,
	std::vector<ResolvedCurrencyCategory> categories,
	std::vector<ContentBaseline> baselines,
	std::string const &extendedCostDbcSha256,
	std::vector<ResolvedExtendedCost> costs,
	std::vector<ResolvedVendorRow> vendors,
	std::vector<ResolvedCreatureTemplate> creatures,
	std::vector<ResolvedGameObjectTemplate> gameObjects,
	std::vector<ResolvedCreatureSpawn> spawns,
    std::vector<ResolvedSpell> spells, std::string const& spellDbcSha256,
	bool validateCurrentSpellPolicy,
	std::map<std::string, std::string> const& worldMapDbcSha256) {
    auto sorted = allocations;
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) {
		return std::tie(a.packageKey, a.symbol) <
			   std::tie(b.packageKey, b.symbol);
    });
	json artifact = {{"format", 1},
					 {"realm", realm},
					 {"build", build},
					 {"baselineSha256", baselineSha256},
					 {"itemDbcSha256", itemDbcSha256},
					 {"clientMpqSha256", clientMpqSha256},
					 {"serverBundleSha256", serverSha256},
        {"resources", json::array()}};
    for (auto const& allocation : sorted)
        artifact["resources"].push_back(ResourceObject(allocation, rows));
	if (!currencyDbcSha256.empty()) {
        artifact["format"] = 2;
        artifact["currencyDbcSha256"] = currencyDbcSha256;
        artifact["currencies"] = json::array();
        auto items = rows;
        std::sort(items.begin(), items.end(), LessRow);
        for (auto const& row : items)
            if (row.currency.itemId)
				artifact["currencies"].push_back(
					{{"package", row.packageKey},
					 {"symbol", row.currency.symbol},
					 {"itemSymbol", row.symbol},
					 {"ID", row.id},
					 {"ItemID", row.id},
					 {"CategoryID", row.currency.categoryId},
					 {"BitIndex", row.currency.bitIndex},
					 {"itemTemplateEntry", row.id},
					 {"BagFamily", row.server.bagFamily},
					 {"serverTable", "currencytypes_dbc"},
					 {"serverDescriptorVersion", 1}});
	}
	if (!categoryDbcSha256.empty()) {
        artifact["format"] = 3;
        artifact["categoryDbcSha256"] = categoryDbcSha256;
		artifact["categoryFallback"] =
			"authored-locale-else-enUS;reserved-slots-empty;v1";
        artifact["categories"] = json::array();
		std::sort(categories.begin(), categories.end(),
				  [](auto const &a, auto const &b) {
					  return std::tie(a.packageKey, a.symbol) <
							 std::tie(b.packageKey, b.symbol);
        });
        for (auto const& c : categories)
			artifact["categories"].push_back({{"package", c.packageKey},
											  {"symbol", c.symbol},
											  {"ID", c.id},
											  {"name", c.names}});
        for (auto& currency : artifact["currencies"])
            for (auto const& row : rows)
				if (currency["package"] == row.packageKey &&
					currency["symbol"] == row.currency.symbol &&
					!row.currency.categorySymbol.empty())
					currency["categorySymbol"] = row.currency.categorySymbol;
    }
	if (!baselines.empty()) {
        artifact["format"] = 3;
        artifact["baselines"] = json::array();
		std::sort(
			baselines.begin(), baselines.end(),
			[](auto const &a, auto const &b) { return a.table < b.table; });
        for (auto const& b : baselines)
			artifact["baselines"].push_back(
				{{"table", b.table},
				 {"clientBuild", b.clientBuild},
				 {"descriptorVersion", b.descriptorVersion},
				 {"sha256", b.hash}});
    }
	if (!costs.empty()) {
        artifact["format"] = 4;
        artifact["extendedCostDbcSha256"] = extendedCostDbcSha256;
        artifact["extendedCosts"] = CostObjects(costs);
    }
	if (!vendors.empty()) {
		artifact["format"] = 5;
		artifact["vendorRows"] = ContentVendorServer::Objects(vendors);
	}
	if (!creatures.empty() || !gameObjects.empty() || !spawns.empty()) {
		artifact["format"] = 6;
		artifact["managedServer"] =
			ContentManagedServer::Objects(creatures, gameObjects, spawns);
	}
	if (!spells.empty()) {
		if (!ContentBuildHash::Valid(spellDbcSha256)) throw std::runtime_error("Invalid composed Spell.dbc hash");
		artifact["format"] = 7; artifact["spellDbcSha256"] = spellDbcSha256;
		artifact["spells"] = SpellObjects(spells,validateCurrentSpellPolicy);
	}
	if (!worldMapDbcSha256.empty()) {
		// A native world map is a client-only contribution, so its only durable
		// record is the lease plus the composed file hash. Recording both keeps a
		// rebuild or reinstall auditable without a world-database row.
		artifact["format"] = 8;
		artifact["worldMapDbcSha256"] = json::object();
		for (auto const& entry : worldMapDbcSha256) {
			if (!ContentBuildHash::Valid(entry.second))
				throw std::runtime_error("Invalid composed " + entry.first + ".dbc hash");
			artifact["worldMapDbcSha256"][entry.first] = entry.second;
		}
	}
    return artifact.dump(2) + "\n";
}

bool ContentServerBundle::ParseServer(
	std::string const &text, std::string const &realm,
	std::vector<ResolvedServerItem> &rows, std::string &error,
	std::vector<ResolvedExtendedCost> *costs,
	std::vector<ResolvedVendorRow> *vendors,
	std::vector<ResolvedCreatureTemplate> *creatures,
	std::vector<ResolvedGameObjectTemplate> *gameObjects,
	std::vector<ResolvedCreatureSpawn> *spawns,
	std::vector<ResolvedSpell> *spells) {
	if (costs)
		costs->clear();
	if (vendors)
		vendors->clear();
	if (creatures)
		creatures->clear();
	if (gameObjects)
		gameObjects->clear();
	if (spawns)
		spawns->clear();
	if (spells) spells->clear();
    rows.clear();
	try {
        auto artifact = json::parse(text);
		auto format = artifact.value("format", 0);
		if (!artifact.is_object() || (format < 1 || format > 7) ||
			artifact.at("realm") != realm ||
			artifact.at("table") != "item_template" ||
			artifact.at("descriptorVersion") != 1 ||
			!artifact.at("rows").is_array())
			throw std::runtime_error(
				"server bundle header or descriptor mismatch");
		static std::set<std::string> const allowed = {
			"format", "realm",		   "table",		 "descriptorVersion",
			"rows",	  "extendedCosts", "vendorRows", "managedServer", "spells"};
		for (auto it = artifact.begin(); it != artifact.end(); ++it)
			if (!allowed.count(it.key()))
				throw std::runtime_error("unknown server bundle key");
		if ((artifact.contains("extendedCosts") && format < 4) ||
			(artifact.contains("vendorRows") && format < 5) ||
			(artifact.contains("managedServer") && format < 6) ||
			(artifact.contains("spells") && format != 7))
			throw std::runtime_error("server bundle feature/format mismatch");
        std::set<std::pair<std::string, std::string>> identities;
        std::set<std::uint32_t> ids;
		for (auto const &source : artifact.at("rows")) {
			if (!source.is_object() ||
				(source.size() != 6 && source.size() != 7) ||
				source.at("resourceKind") != "item.id")
                throw std::runtime_error("invalid server bundle row");
            ResolvedServerItem row;
            row.packageKey = source.at("package").get<std::string>();
            row.packageVersion = source.at("packageVersion").get<std::string>();
            row.symbol = source.at("symbol").get<std::string>();
			if (!source.at("entry").is_number_unsigned() ||
				source.at("entry").get<std::uint64_t>() > 0xffffffffULL)
                throw std::runtime_error("invalid server entry");
            row.id = source.at("entry").get<std::uint32_t>();
            auto const& fields = source.at("fields");
			if (!row.id || row.packageKey.empty() || row.symbol.empty() ||
				row.packageVersion.empty() || !fields.is_object() ||
				fields.size() != columns.size() || fields.at("Flags") != 0)
				throw std::runtime_error(
					"invalid server bundle identity or fields");
            for (auto const* column : columns)
				if (!fields.contains(column))
					throw std::runtime_error("server bundle field missing");
			auto unsignedValue = [&](char const *name,
									 std::uint64_t maximum) -> std::uint32_t {
                auto const& value = fields.at(name);
				if (!value.is_number_unsigned() ||
					value.get<std::uint64_t>() > maximum)
					throw std::runtime_error(
						std::string("invalid unsigned server field: ") + name);
                return static_cast<std::uint32_t>(value.get<std::uint64_t>());
            };
            auto signedValue = [&](char const* name, std::int64_t minimum,
                std::int64_t maximum) -> std::int32_t {
                auto const& value = fields.at(name);
				if (!value.is_number_integer() ||
					value.get<std::int64_t>() < minimum ||
					value.get<std::int64_t>() > maximum)
					throw std::runtime_error(
						std::string("invalid signed server field: ") + name);
                return static_cast<std::int32_t>(value.get<std::int64_t>());
            };
            row.client.classID = unsignedValue("class", 255);
            row.client.subclassID = unsignedValue("subclass", 255);
			row.client.soundOverrideSubclassID =
				signedValue("SoundOverrideSubclass", -128, 127);
            row.client.inventoryType = unsignedValue("InventoryType", 255);
            row.client.material = signedValue("Material", -128, 127);
            row.client.sheatheType = unsignedValue("sheath", 255);
            row.displayId = unsignedValue("displayid", 0xffffffffULL);
            row.server.symbol = row.symbol;
            row.server.name = fields.at("name").get<std::string>();
			row.server.description =
				fields.at("description").get<std::string>();
			row.server.quality =
				static_cast<std::uint8_t>(unsignedValue("Quality", 7));
            row.server.stackable = signedValue("stackable", 1, 1000);
			row.server.bonding =
				static_cast<std::uint8_t>(unsignedValue("bonding", 5));
            row.server.bagFamily = signedValue("BagFamily", 0, 8192);
			if (source.contains("currency")) {
                auto const& currency = source.at("currency");
				if (!currency.is_object() ||
					(currency.size() != 5 && currency.size() != 6) ||
					currency.at("ID") != row.id ||
					currency.at("ItemID") != row.id || row.id > 0x7fffffffU ||
					!currency.at("CategoryID").is_number_unsigned() ||
					currency.at("CategoryID").get<std::uint64_t>() >
						0x7fffffffULL ||
					!currency.at("BitIndex").is_number_unsigned() ||
					currency.at("BitIndex").get<std::uint64_t>() > 64)
                    throw std::runtime_error("Invalid currency server row");
				row.currency = {currency.at("symbol").get<std::string>(),
								row.id,
								currency.at("CategoryID").get<std::uint32_t>(),
								currency.at("BitIndex").get<std::uint32_t>()};
				if (currency.contains("categorySymbol")) {
					row.currency.categorySymbol =
						currency.at("categorySymbol").get<std::string>();
					if (row.currency.categorySymbol.empty() ||
						row.currency.categoryId > 65535)
						throw std::runtime_error(
							"Invalid category symbol/allocated ID");
				}
				if (row.currency.symbol.empty() || !row.currency.categoryId ||
					!row.currency.bitIndex || row.server.bagFamily != 8192)
					throw std::runtime_error(
						"Currency server row lacks token semantics");
            }
            auto const* descriptor = FindServerTableDescriptor("item_template");
			if (!descriptor || !row.displayId || row.server.name.empty() ||
				row.server.name.size() > 255 ||
				row.server.description.size() > 255 ||
				row.server.name.find('\0') != std::string::npos ||
				row.server.description.find('\0') != std::string::npos ||
				row.server.quality > 7 || row.server.stackable < 1 ||
				row.server.stackable > 1000 || row.server.bonding > 5 ||
				(row.server.bagFamily != 0 && !row.currency.itemId) ||
				row.client.classID > 255 || row.client.subclassID > 255 ||
				row.client.inventoryType > 255 ||
				row.client.sheatheType > 255 ||
				row.client.soundOverrideSubclassID < -128 ||
				row.client.soundOverrideSubclassID > 127 ||
				row.client.material < -128 || row.client.material > 127 ||
				!identities.emplace(row.packageKey, row.symbol).second ||
				!ids.insert(row.id).second)
				throw std::runtime_error(
					"server bundle row violates item_template descriptor");
            rows.push_back(std::move(row));
        }
		auto parsedCosts = artifact.contains("extendedCosts")
							   ? ParseCosts(artifact.at("extendedCosts"))
							   : std::vector<ResolvedExtendedCost>{};
		auto parsedVendors =
			artifact.contains("vendorRows")
				? ContentVendorServer::Parse(artifact.at("vendorRows"))
				: std::vector<ResolvedVendorRow>{};
		std::vector<ResolvedCreatureTemplate> parsedCreatures;
		std::vector<ResolvedGameObjectTemplate> parsedGameObjects;
		std::vector<ResolvedCreatureSpawn> parsedSpawns;
		auto parsedSpells = artifact.contains("spells") ? ParseSpells(artifact.at("spells")) : std::vector<ResolvedSpell>{};
		if (artifact.contains("managedServer"))
			ContentManagedServer::Parse(artifact.at("managedServer"),
										parsedCreatures, parsedGameObjects,
										parsedSpawns);
        for (auto const& v : parsedVendors)
			if (std::none_of(
					parsedCosts.begin(), parsedCosts.end(), [&](auto const &c) {
						return c.packageKey == v.packageKey &&
							   c.symbol == v.costSymbol && c.id == v.costId;
					}))
				throw std::runtime_error(
					"Vendor cost relationship is unresolved");
		if (ServerJson(realm, rows, parsedCosts, parsedVendors, parsedCreatures,
					   parsedGameObjects, parsedSpawns, parsedSpells, false) != text)
			throw std::runtime_error(
				"server bundle is not in canonical generated form");
		if (costs)
			*costs = std::move(parsedCosts);
		if (vendors)
			*vendors = std::move(parsedVendors);
		if (creatures)
			*creatures = std::move(parsedCreatures);
		if (gameObjects)
			*gameObjects = std::move(parsedGameObjects);
		if (spawns)
			*spawns = std::move(parsedSpawns);
		if (spells) *spells = std::move(parsedSpells);
        return true;
	} catch (std::exception const &exception) {
		error = std::string("Invalid server bundle: ") + exception.what();
		rows.clear();
		return false;
    }
}

bool ContentServerBundle::VerifyParity(
	std::string const &text, std::string const &realm, std::uint32_t build,
	std::string const &baselineSha256, std::string const &clientMpqSha256,
	std::string const &serverSha256,
	std::vector<ResolvedServerItem> const &rows,
	std::vector<ItemAllocation> const &allocations, std::string &error,
	std::vector<ResolvedExtendedCost> const &costs,
	std::vector<ResolvedVendorRow> const &vendors,
	std::vector<ResolvedCreatureTemplate> const &creatures,
	std::vector<ResolvedGameObjectTemplate> const &gameObjects,
        std::vector<ResolvedCreatureSpawn> const &spawns,
	std::vector<ResolvedSpell> const &spells,
	std::map<std::string, std::string> const &expectedWorldMapDbcSha256) {
	try {
        auto actual = json::parse(text);
		auto spellSha=actual.value("spellDbcSha256",std::string());
		if(spells.empty()!=spellSha.empty()||(!spells.empty()&&!ContentBuildHash::Valid(spellSha)))throw std::runtime_error("Spell DBC hash missing or invalid");
		for(auto const&s:spells){ValidateSpellStructure(s);auto lease=std::find_if(allocations.begin(),allocations.end(),[&](auto const&a){return a.resourceKind=="spell.id"&&a.packageKey==s.packageKey&&a.symbol==s.symbol&&a.value==s.id;});if(lease==allocations.end()||!ContentBuildHash::Valid(lease->baselineSha256))throw std::runtime_error("Managed spell lease missing");}
		for(auto const&a:allocations)if(a.resourceKind=="spell.id"&&std::none_of(spells.begin(),spells.end(),[&](auto const&s){return s.packageKey==a.packageKey&&s.symbol==a.symbol&&s.id==a.value;}))throw std::runtime_error("Orphan managed spell lease");
        auto itemSha = actual.at("itemDbcSha256").get<std::string>();
        auto costSha = actual.value("extendedCostDbcSha256", std::string());
		if ((!costs.empty() || !spells.empty()) && !actual.contains("baselines"))
			throw std::runtime_error(
				"Managed DBC resources require accepted baseline snapshots");
		if (costs.empty() != costSha.empty() ||
			(!costs.empty() && !ContentBuildHash::Valid(costSha)))
            throw std::runtime_error("Extended-cost hash missing or invalid");
        std::set<std::pair<std::string,std::string>> costSymbols;
		for (auto const &c : costs) {
            ItemExtendedCostDbc::Words(c);
            costSymbols.emplace(c.packageKey,c.symbol);
			auto lease = std::find_if(
				allocations.begin(), allocations.end(), [&](auto const &a) {
					return a.resourceKind == "item-extended-cost.id" &&
						   a.packageKey == c.packageKey &&
						   a.symbol == c.symbol && a.value == c.id;
            });
			if (lease == allocations.end() ||
				!ContentBuildHash::Valid(lease->baselineSha256))
                throw std::runtime_error("Extended-cost lease missing");
			for (auto const &q : c.requirements) {
				auto item =
					std::find_if(rows.begin(), rows.end(), [&](auto const &r) {
						return r.packageKey == q.packageKey &&
							   r.symbol == q.symbol && r.id == q.itemId;
                });
				auto allocation = std::find_if(
					allocations.begin(), allocations.end(), [&](auto const &a) {
						return a.resourceKind == "item.id" &&
							   a.packageKey == q.packageKey &&
							   a.symbol == q.symbol && a.value == q.itemId;
                });
                if (item == rows.end() || allocation == allocations.end())
					throw std::runtime_error("Extended-cost requirement lacks "
											 "active item and retained lease");
            }
        }
        for (auto const& a : allocations)
			if (a.resourceKind == "item-extended-cost.id" &&
				!costSymbols.count({a.packageKey, a.symbol}))
                throw std::runtime_error("Orphan extended-cost lease");
        auto currencySha = actual.value("currencyDbcSha256", std::string());
		bool currencyRows =
			std::any_of(rows.begin(), rows.end(),
						[](auto const &r) { return r.currency.itemId != 0; });
		if (currencyRows != !currencySha.empty() ||
			(currencyRows && !ContentBuildHash::Valid(currencySha)))
            throw std::runtime_error("Currency DBC hash missing or invalid");
        std::set<std::uint32_t> currencyBits;
        for (auto const& row : rows)
			if (row.currency.itemId) {
				auto found = std::find_if(
					allocations.begin(), allocations.end(), [&](auto const &a) {
						return a.resourceKind == "currency.known-bit" &&
							   a.packageKey == row.packageKey &&
							   a.symbol == row.currency.symbol &&
							   a.value == row.currency.bitIndex;
                });
				if (found == allocations.end() ||
					!ContentBuildHash::Valid(found->baselineSha256) ||
					!currencyBits.insert(row.currency.bitIndex).second)
					throw std::runtime_error(
						"Currency parity allocation missing or duplicate");
            }
        auto categorySha = actual.value("categoryDbcSha256", std::string());
        std::vector<ResolvedCurrencyCategory> categories;
        std::set<std::pair<std::string,std::string>> categoryIdentities;
        std::set<std::uint32_t> categoryIds;
		if (!categorySha.empty()) {
			if (!ContentBuildHash::Valid(categorySha) ||
				!actual.at("categories").is_array())
				throw std::runtime_error(
					"Invalid category artifact provenance");
			for (auto const &source : actual.at("categories")) {
				ResolvedCurrencyCategory c{
					source.at("package").get<std::string>(),
					source.at("symbol").get<std::string>(),
					source.at("ID").get<std::uint32_t>(),
					source.at("name")
						.get<std::map<std::string, std::string>>()};
                CurrencyCategoryDbcComposer::ValidateNames(c.names);
				if (!c.id || c.id > 65535 ||
					!categoryIdentities.emplace(c.packageKey, c.symbol)
						 .second ||
					!categoryIds.insert(c.id).second)
					throw std::runtime_error(
						"Duplicate/invalid category identity");
				auto lease = std::find_if(
					allocations.begin(), allocations.end(), [&](auto const &a) {
						return a.resourceKind == "currency-category.id" &&
							   a.packageKey == c.packageKey &&
							   a.symbol == c.symbol && a.value == c.id;
                });
				if (lease == allocations.end() ||
					!ContentBuildHash::Valid(lease->baselineSha256))
                    throw std::runtime_error("Category lease missing");
				if (std::none_of(
						rows.begin(), rows.end(), [&](auto const &row) {
							return row.packageKey == c.packageKey &&
								   row.currency.categorySymbol == c.symbol &&
								   row.currency.categoryId == c.id;
						}))
					throw std::runtime_error(
						"Unreferenced category in parity manifest");
                categories.push_back(c);
            }
			if (categories.empty())
				throw std::runtime_error("Empty category artifact");
        }
        for (auto const& row : rows)
			if (!row.currency.categorySymbol.empty() &&
				std::none_of(
					categories.begin(), categories.end(), [&](auto const &c) {
						return c.packageKey == row.packageKey &&
							   c.symbol == row.currency.categorySymbol &&
							   c.id == row.currency.categoryId;
					}))
				throw std::runtime_error(
					"Currency category relationship missing or mismatched");
        for (auto const& a : allocations)
			if (a.resourceKind == "currency-category.id" &&
				!categoryIdentities.count({a.packageKey, a.symbol}))
                throw std::runtime_error("Orphan category lease in manifest");
		auto requireLease = [&](std::string const &kind,
								std::string const &package,
								std::string const &symbol,
								std::uint32_t value) {
			auto found = std::find_if(
				allocations.begin(), allocations.end(), [&](auto const &a) {
					return a.resourceKind == kind && a.packageKey == package &&
						   a.symbol == symbol && a.value == value;
				});
			if (found == allocations.end() ||
				found->baselineSha256 !=
					ContentManagedServer::DescriptorFingerprint(kind))
				throw std::runtime_error(
					"Managed server resource lease missing or descriptor "
					"provenance mismatched");
		};
		for (auto const &r : creatures)
			requireLease("creature-template.id", r.packageKey, r.symbol,
						 r.entry);
		for (auto const &r : gameObjects)
			requireLease("gameobject-template.id", r.packageKey, r.symbol,
						 r.entry);
		for (auto const &r : spawns) {
			requireLease("creature-spawn.guid", r.packageKey, r.symbol, r.guid);
			if (std::none_of(creatures.begin(), creatures.end(),
							 [&](auto const &c) {
								 return c.packageKey == r.packageKey &&
										c.symbol == r.creatureSymbol &&
										c.entry == r.creatureEntry;
							 }))
				throw std::runtime_error(
					"Creature spawn template relationship is unresolved");
		}
		for (auto const &a : allocations)
			if ((a.resourceKind == "creature-template.id" &&				 std::none_of(creatures.begin(), creatures.end(),
							  [&](auto const &r) {
								  return r.packageKey == a.packageKey &&
										 r.symbol == a.symbol &&
										 r.entry == a.value;
							  })) ||
				(a.resourceKind == "gameobject-template.id" &&
				 std::none_of(gameObjects.begin(), gameObjects.end(),
							  [&](auto const &r) {
								  return r.packageKey == a.packageKey &&
										 r.symbol == a.symbol &&
										 r.entry == a.value;
							  })) ||
				(a.resourceKind == "creature-spawn.guid" &&
				 std::none_of(spawns.begin(), spawns.end(), [&](auto const &r) {
					 return r.packageKey == a.packageKey &&
							r.symbol == a.symbol && r.guid == a.value;
				 })))
				throw std::runtime_error("Orphan managed server allocation");
        // A native world map is client-only. Its durable record is exactly the
        // lease plus the composed file hash, so every world-map lease must be
        // covered by a recorded hash for its own table and vice versa.
        std::map<std::string, std::string> worldMapHashes;
        {
            std::map<std::string, std::string> kinds;
            for (auto const& table : WorldMapDbcTables())
                kinds.emplace(table, WorldMapDbcComposer::ResourceKind(table));
            std::map<std::string, std::set<std::string>> leases;
            for (auto const &a : allocations)
                for (auto const &entry : kinds)
                    if (a.resourceKind == entry.second)
                        leases[entry.first].insert(a.packageKey + "/" + a.symbol);
            std::set<std::string> tableOf;
            for (auto const &entry : kinds) tableOf.insert(entry.second);
            for (auto const &a : allocations)
                if (tableOf.count(a.resourceKind) &&
                    !ContentBuildHash::Valid(a.baselineSha256))
                    throw std::runtime_error(
                        "World-map lease has no valid baseline provenance");
            if (actual.contains("worldMapDbcSha256")) {
                if (!actual.at("worldMapDbcSha256").is_object())
                    throw std::runtime_error("Invalid worldMapDbcSha256");
                for (auto it = actual.at("worldMapDbcSha256").begin();
                     it != actual.at("worldMapDbcSha256").end(); ++it) {
                    if (!kinds.count(it.key()))
                        throw std::runtime_error("Unknown world-map table hash: " + it.key());
                    auto hash = it.value().get<std::string>();
                    if (!ContentBuildHash::Valid(hash))
                        throw std::runtime_error("Invalid world-map table hash: " + it.key());
                    worldMapHashes[it.key()] = hash;
                }
            }
            for (auto const &entry : kinds)
                if (leases.count(entry.first) != worldMapHashes.count(entry.first))
                    throw std::runtime_error(
                        "World-map lease/hash coverage mismatch for " + entry.first);
            if (worldMapHashes != expectedWorldMapDbcSha256)
                throw std::runtime_error(
                    "World-map composed hash set mismatch");
        }
        std::vector<ContentBaseline> baselines;
		if (actual.contains("baselines")) {
            std::set<std::string> tables;
			for (auto const &source : actual.at("baselines")) {
				ContentBaseline b;
				b.table = source.at("table").get<std::string>();
                b.clientBuild = source.at("clientBuild").get<std::uint32_t>();
				b.descriptorVersion =
					source.at("descriptorVersion").get<std::uint32_t>();
                b.hash = source.at("sha256").get<std::string>();
                auto d = FindDbcDescriptor(b.clientBuild,b.table);
				if (!d || d->version != b.descriptorVersion ||
					!ContentBuildHash::Valid(b.hash) ||
					!tables.insert(b.table).second)
                    throw std::runtime_error("Invalid baseline snapshot");
				if (b.table == "Item" && b.hash != baselineSha256)
					throw std::runtime_error("Item baseline snapshot mismatch");
                baselines.push_back(b);
            }
			std::set<std::string> expected;
			if (!rows.empty()) expected.insert("Item");
			if (currencyRows)
				expected.insert("CurrencyTypes");
			if (!categories.empty())
				expected.insert("CurrencyCategory");
			if (!costs.empty())
				expected.insert("ItemExtendedCost");
			if (!spells.empty()) expected.insert("Spell");
			for (auto const &entry : worldMapHashes)
				expected.insert(entry.first);
			if (tables != expected)
				throw std::runtime_error("Baseline snapshot set mismatch");
			if(!spells.empty()){
				auto baseline=std::find_if(baselines.begin(),baselines.end(),[](auto const&b){return b.table=="Spell";});
				if(baseline==baselines.end()||std::any_of(allocations.begin(),allocations.end(),[&](auto const&a){return a.resourceKind=="spell.id"&&a.baselineSha256!=baseline->hash;}))throw std::runtime_error("Spell lease/baseline provenance mismatch");
			}
			// Every world-map lease must be pinned to the accepted snapshot of the
			// one table it belongs to, so a re-registered baseline can never be
			// reused silently.
			for (auto const &table : WorldMapDbcTables()) {
				auto const &kind = WorldMapDbcComposer::ResourceKind(table);
				auto baseline = std::find_if(
					baselines.begin(), baselines.end(),
					[&](auto const &b) { return b.table == table; });
				if (std::any_of(allocations.begin(), allocations.end(),
						[&](auto const &a) {
							return a.resourceKind == kind &&
								   (baseline == baselines.end() ||
									a.baselineSha256 != baseline->hash);
						}))
					throw std::runtime_error(
						"World-map lease/baseline provenance mismatch for " + table);
			}
		}
		if ((!rows.empty() && !ContentBuildHash::Valid(itemSha)) ||
			(rows.empty() && !itemSha.empty()) ||
			actual != json::parse(ParityJson(
						  realm, build, allocations, rows, baselineSha256,
						  itemSha, clientMpqSha256, serverSha256, currencySha,
						  categorySha, categories, baselines, costSha, costs,
						  vendors, creatures, gameObjects, spawns, spells, spellSha,
						  false, worldMapHashes)))
			throw std::runtime_error("manifest values differ from build, "
									 "allocation, or server bundle");
        return true;
	} catch (std::exception const &exception) {
		error = std::string("Parity manifest mismatch: ") + exception.what();
		return false;
    }
}

std::string ContentServerBundle::InsertSql(ResolvedServerItem const &row) {
    auto fields = RowObject(row);
    std::string sql = "INSERT INTO item_template (`entry`";
	for (auto const *column : columns)
		sql += ",`" + std::string(column) + "`";
    sql += ") VALUES (" + std::to_string(row.id);
	for (auto const *column : columns)
		sql += "," + SqlValue(fields.at(column), column);
    return sql + ")";
}

std::string ContentServerBundle::UpdateSql(ResolvedServerItem const &row,
										   std::string const &previousRowJson) {
    auto fields = RowObject(row);
    auto previous = json::parse(previousRowJson);
    std::string sql = "UPDATE item_template SET ";
    bool first = true;
	for (auto const *column : columns) {
		if (!first)
			sql += ",";
        first = false;
		sql += "`" + std::string(column) +
			   "`=" + SqlValue(fields.at(column), column);
    }
    sql += " WHERE `entry`=" + std::to_string(row.id);
    for (auto const* column : columns)
		sql += " AND `" + std::string(column) +
			   "`=" + SqlValue(previous.at(column), column);
    return sql;
}

std::string ContentServerBundle::MatchSql(ResolvedServerItem const &row,
										  std::string const &alias) {
    auto fields = RowObject(row);
    std::string sql = alias + ".`entry`=" + std::to_string(row.id);
    for (auto const* column : columns)
		sql += " AND " + alias + ".`" + std::string(column) +
			   "`=" + SqlValue(fields.at(column), column);
    return sql;
}
