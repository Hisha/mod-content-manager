#include "ContentServerDeployment.h"
#include "ContentAllocationRegistry.h"
#include "ContentBuildHash.h"
#include "ContentBuildPaths.h"
#include "ContentBuildRegistry.h"
#include "ContentCurrencyServer.h"
#include "ContentExtendedCostServer.h"
#include "ContentServerBundle.h"
#include "ContentServerOwnership.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include "ServerTableDescriptor.h"
#include "Transaction.h"
#include "third_party/json/json.hpp"
#include <algorithm>
#include <fstream>
#include <mutex>
#include <openssl/evp.h>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>

using json = nlohmann::json;

namespace {
std::mutex deploymentMutex;

std::filesystem::path SidecarPath(std::filesystem::path const& outputDirectory,
								  std::string const &filename) {
    using namespace ContentBuildPaths;
    Require(!outputDirectory.empty(), "OutputDirectory is not configured");
    auto safe = Target(filename);
    Require(safe == filename && filename.find('/') == std::string::npos,
        "Unsafe sidecar filename in build registry");
    auto path = fs::absolute(outputDirectory / filename);
    RejectLinks(path);
    return path;
}

std::string ReadFile(std::filesystem::path const &path) {
    using namespace ContentBuildPaths;
    RejectLinks(path);
	Require(fs::is_regular_file(path) && fs::file_size(path) > 0 &&
				fs::file_size(path) <= 10 * 1024 * 1024,
        "Server sidecar is missing, empty, or too large: " + path.string());
    std::ifstream input(path, std::ios::binary);
    Require(input.is_open(), "Cannot open server sidecar: " + path.string());
	std::string text((std::istreambuf_iterator<char>(input)),
					 std::istreambuf_iterator<char>());
    Require(!input.bad(), "Cannot read server sidecar: " + path.string());
    return text;
}

std::string HashText(std::string const &value) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int size = 0;
	if (EVP_Digest(value.data(), value.size(), digest, &size, EVP_sha256(),
				   nullptr) != 1 ||
		size != 32)
        throw std::runtime_error("Cannot hash server sidecar contents");
    static char const digits[] = "0123456789abcdef";
    std::string hash;
	for (unsigned i = 0; i < size; ++i) {
		hash += digits[digest[i] >> 4];
		hash += digits[digest[i] & 15];
	}
    return hash;
}

bool ValidateItemSchema(std::string &error) {
    auto const* descriptor = FindServerTableDescriptor("item_template");
	if (!descriptor || descriptor->version != 1) {
		error = "item_template descriptor v1 is unavailable";
		return false;
	}
	std::string sql = "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE "
					  "TABLE_SCHEMA=DATABASE() "
        "AND TABLE_NAME='item_template' AND (";
    bool first = true;
    std::string included;
	for (auto const &column : descriptor->columns) {
		if (!first) {
			sql += " OR ";
			included += ",";
		}
        first = false;
		sql +=
			"(COLUMN_NAME=" + ContentServerBundle::SqlText(column.name) +
			" AND COLUMN_TYPE=" +
			ContentServerBundle::SqlText(column.columnType) +
			" AND IS_NULLABLE='" +
			(column.nullable ? std::string("YES") : std::string("NO")) + "'" +
			(column.collation
				 ? " AND COLLATION_NAME='" + std::string(column.collation) + "'"
				 : "") +
			")";
        included += ContentServerBundle::SqlText(column.name);
    }
    sql += ")";
    auto result = WorldDatabase.Query(sql);
	if (!result ||
		result->Fetch()[0].Get<std::uint64_t>() != descriptor->columns.size()) {
		error = "Deployed item_template columns differ from descriptor v1; "
				"server apply refused";
		return false;
	}
	auto missingDefaults =
		WorldDatabase.Query("SELECT COUNT(*) FROM information_schema.COLUMNS "
							"WHERE TABLE_SCHEMA=DATABASE() AND "
							"TABLE_NAME='item_template' AND IS_NULLABLE='NO' "
							"AND COLUMN_DEFAULT IS NULL AND EXTRA NOT LIKE "
							"'%auto_increment%' AND COLUMN_NAME NOT IN (" +
							included + ")");
	if (!missingDefaults ||
		missingDefaults->Fetch()[0].Get<std::uint64_t>() != 0) {
		error = "item_template has an omitted required column without a "
				"default; server apply refused";
		return false;
	}
    return true;
}

std::vector<ItemAllocation>
ManifestAllocations(json const &parity,
					std::vector<ItemAllocation> const &current) {
    std::vector<ItemAllocation> result;
    std::set<std::tuple<std::string, std::string, std::string>> identities;
	if (!parity.at("resources").is_array())
		throw std::runtime_error("parity resources are not an array");
	for (auto const &resource : parity.at("resources")) {
        auto package = resource.at("package").get<std::string>();
        auto symbol = resource.at("symbol").get<std::string>();
        auto value = resource.at("value").get<std::uint32_t>();
        auto baseline = resource.at("baselineSha256").get<std::string>();
		auto policy =
			resource.at("allocationPolicyVersion").get<std::uint32_t>();
        auto kind = resource.at("resourceKind").get<std::string>();
		if (!value ||
			(kind != "item.id" && kind != "currency.known-bit" &&
			 kind != "currency-category.id" &&
			 kind != "item-extended-cost.id" &&
			 kind != "creature-template.id" &&
			 kind != "gameobject-template.id" &&
			 kind != "creature-spawn.guid") ||
			((kind == "currency-category.id" ||
			  kind == "item-extended-cost.id") &&
			 value > 65535) ||
			((kind == "creature-template.id" ||
			  kind == "gameobject-template.id") &&
			 value > 0x00ffffff) ||
			(kind == "currency.known-bit" && value > 64) ||
			!ContentBuildHash::Valid(baseline) ||
			!identities.emplace(package, symbol, kind).second)
            throw std::runtime_error("invalid parity allocation");
		auto found = std::find_if(
			current.begin(), current.end(), [&](auto const &lease) {
				return lease.packageKey == package && lease.symbol == symbol &&
					   lease.resourceKind == kind;
        });
		if (found == current.end() || found->value != value ||
			found->baselineSha256 != baseline || found->policyVersion != policy)
			throw std::runtime_error(
				"retained allocation no longer matches parity manifest: " +
				package + "/" + symbol);
        result.push_back(*found);
    }
    return result;
}
} // namespace

bool ContentServerDeployment::ReadStatus(std::uint32_t build, bool& exists,
										 ContentServerStatus &status,
										 std::string &error) {
    exists = false;
	auto result = WorldDatabase.Query(
		"SELECT "
		"s.build_number,s.server_state,s.bundle_filename,s.bundle_sha256,"
		"s.parity_filename,s.parity_sha256 FROM (SELECT 1) seed LEFT JOIN "
		"content_manager_server_build s "
		"ON s.build_number=" +
		std::to_string(build));
	if (!result) {
		error = "Cannot read server build registry; apply Phase 3 world SQL "
				"and check SQL logs";
		return false;
	}
    auto f = result->Fetch();
	if (f[0].IsNull())
		return true;
	status = {f[0].Get<uint32>(),	   f[1].Get<std::string>(),
			  f[2].Get<std::string>(), f[3].Get<std::string>(),
			  f[4].Get<std::string>(), f[5].Get<std::string>()};
    exists = true;
    return true;
}

bool ContentServerDeployment::Inspect(
	std::uint32_t build, std::string const &realm,
    std::filesystem::path const& outputDirectory, ContentServerStatus& status,
    std::vector<ResolvedServerItem>& rows, std::string& error,
	std::vector<ResolvedExtendedCost> *outCosts,
	std::vector<ResolvedVendorRow> *outVendors,
	std::vector<ResolvedCreatureTemplate> *outCreatures,
	std::vector<ResolvedGameObjectTemplate> *outGameObjects,
	std::vector<ResolvedCreatureSpawn> *outSpawns) {
	try {
        std::optional<ContentBuildRecord> record;
		if (!ContentBuildRegistry().GetBuild(build, record, error))
			return false;
		if (!record || record->realmName != realm) {
			error = "Build does not exist for this realm";
			return false;
		}
        bool exists = false;
		if (!ReadStatus(build, exists, status, error))
			return false;
		if (!exists) {
			error = "Build has no Phase 3 server artifact";
			return false;
		}
        auto path = SidecarPath(outputDirectory, status.bundleFilename);
        auto contents = ReadFile(path);
		if (HashText(contents) != status.bundleSha256) {
			error = "Server bundle hash mismatch";
			return false;
		}
        std::vector<ResolvedExtendedCost> costs;
        std::vector<ResolvedVendorRow> vendors;
		std::vector<ResolvedCreatureTemplate> creatures;
		std::vector<ResolvedGameObjectTemplate> gameObjects;
		std::vector<ResolvedCreatureSpawn> spawns;
		if (!ContentServerBundle::ParseServer(contents, realm, rows, error,
											  &costs, &vendors, &creatures,
											  &gameObjects, &spawns))
			return false;
		if (status.state == "APPLIED")
			for (auto const &r : creatures)
				if (!ContentManagedServer::Verify(r, realm, build,
												  status.bundleSha256, error))
					return false;
		if (status.state == "APPLIED")
			for (auto const &r : gameObjects)
				if (!ContentManagedServer::Verify(r, realm, build,
												  status.bundleSha256, error))
					return false;
		if (status.state == "APPLIED")
			for (auto const &r : spawns)
				if (!ContentManagedServer::Verify(r, realm, build,
												  status.bundleSha256, error))
					return false;
        if (status.state == "APPLIED")
            for (auto const& vendor : vendors)
				if (!ContentVendorServer::Verify(vendor, realm, build,
												 status.bundleSha256, error))
					return false;
        if (status.state == "APPLIED")
            for (auto const& cost : costs)
				if (!ContentExtendedCostServer::Verify(
						cost, realm, build, status.bundleSha256, error))
					return false;
        if (status.state == "APPLIED")
			for (auto const &row : rows) {
				if (row.currency.itemId &&
					!ContentCurrencyServer::Verify(row, realm, build,
												   status.bundleSha256, error))
                    return false;
                bool owned = false, itemExists = false;
                ContentItemOwner owner;
                std::string current;
				if (!ContentServerOwnership::ReadOwner(row.id, owned, owner,
													   error) ||
					!ContentServerOwnership::ReadCurrentRow(row.id, itemExists,
															current, error))
					return false;
				if (ContentServerOwnership::Classify(itemExists, owned, owner,
													 realm, row.packageKey,
													 row.symbol, current) !=
					ContentItemOwnershipAction::Converge) {
					error = "APPLIED server row has ownership or item_template "
							"drift: " +
							std::to_string(row.id);
                    return false;
				}
            }
        using ContentBuildPaths::Require;
		Require(status.bundleFilename == record->filename + ".server.json" &&
					status.parityFilename == record->filename + ".parity.json",
            "Inconsistent sidecar names");
        std::string clientHash;
		Require(ContentBuildHash::Calculate(
					SidecarPath(outputDirectory, record->filename), clientHash,
					error) &&
					clientHash == record->sha256,
            "Client artifact hash mismatch");
		auto parity =
			ReadFile(SidecarPath(outputDirectory, status.parityFilename));
		Require(HashText(parity) == status.paritySha256,
				"Parity artifact hash mismatch");
        auto parityObject=json::parse(parity);
        std::vector<ItemAllocation> retained;
		Require(ContentAllocationRegistry().Read(realm, retained, error),
				error);
        auto allocations=ManifestAllocations(parityObject,retained);
		Require(ContentServerBundle::VerifyParity(
					parity, realm, build, parityObject.at("baselineSha256"),
					record->sha256, status.bundleSha256, rows, allocations,
					error, costs, vendors, creatures, gameObjects, spawns),
				error);
        if(parityObject.contains("baselines"))
			for (auto const &snapshot : parityObject.at("baselines")) {
				ContentBaseline b;
				b.table = snapshot.at("table");
				b.clientBuild = snapshot.at("clientBuild");
				b.descriptorVersion = snapshot.at("descriptorVersion");
				b.hash = snapshot.at("sha256");
				auto check = WorldDatabase.Query(
					"SELECT (" + ContentBaselineRegistry::HistoryCondition(b) +
					")");
				Require(check && check->Fetch()[0].Get<std::uint64_t>() == 1,
						"Baseline history missing");
			}
		if (outCosts)
			*outCosts = costs;
		if (outVendors)
			*outVendors = vendors;
		if (outCreatures)
			*outCreatures = creatures;
		if (outGameObjects)
			*outGameObjects = gameObjects;
		if (outSpawns)
			*outSpawns = spawns;
        return true;
	} catch (std::exception const &exception) {
		error = exception.what();
		return false;
    }
}

bool ContentServerDeployment::Activate(
	std::uint32_t build, std::string const &realm,
	std::filesystem::path const &outputDirectory,
	std::filesystem::path const &publishDirectory,
	ContentActivationResult &result, ContentPublicationResult &publication,
	bool &alreadyActive, std::string &error) {
	result = {};
	publication = {};
	alreadyActive = false;
	try {
		using ContentBuildPaths::Require;
		std::optional<ContentBuildRecord> record;
		if (!ContentBuildRegistry().GetBuild(build, record, error))
			return false;
		Require(record && record->realmName == realm,
				"Build does not exist for this realm");
		Require(record->state == "STAGED" || record->state == "ACTIVE" ||
				record->state == "SUPERSEDED",
				"Unknown build state");
		Require(ContentBuildHash::Valid(record->sha256),
				"Build record has an invalid SHA256; activation refused");

		ContentServerStatus status;
		std::vector<ResolvedServerItem> rows;
		std::vector<ResolvedExtendedCost> costs;
		std::vector<ResolvedVendorRow> vendors;
		std::vector<ResolvedCreatureTemplate> creatures;
		std::vector<ResolvedGameObjectTemplate> gameObjects;
		std::vector<ResolvedCreatureSpawn> spawns;
		if (!Inspect(build, realm, outputDirectory, status, rows, error, &costs,
					 &vendors, &creatures, &gameObjects, &spawns)) {
			error = "Activation prerequisite validation failed before client "
					"publication: " + error;
			return false;
		}
		result.hasManagedServerContent =
			!rows.empty() || !costs.empty() || !vendors.empty() ||
			!creatures.empty() || !gameObjects.empty() || !spawns.empty();
		if (result.hasManagedServerContent) {
			Require(status.state == "STAGED" || status.state == "APPLIED",
					"Unknown server deployment state");
			if (status.state == "STAGED") {
				std::string applySummary;
				if (!Apply(build, realm, outputDirectory, applySummary, error)) {
					error = "Managed server prerequisite failed before client "
							"publication: " + error;
					return false;
				}
				result.serverAppliedNow = true;
				result.serverSummary = "Server content APPLIED and verified for "
					"build " + std::to_string(build) + ".";
			} else {
				result.serverAlreadyApplied = true;
				result.serverSummary =
					"Server content already APPLIED and verified.";
			}
		}

		std::string activationError;
		if (!ContentBuildRegistry().ActivateBuild(
				build, outputDirectory, publishDirectory, publication,
				alreadyActive, activationError)) {
			error = result.hasManagedServerContent
				? "Client activation failed after the managed server prerequisite "
				  "succeeded: " + activationError
				: activationError;
			return false;
		}
		return true;
	} catch (std::exception const &exception) {
		error = exception.what();
		return false;
	}
}

bool ContentServerDeployment::Apply(
	std::uint32_t build, std::string const &realm,
	std::filesystem::path const &outputDirectory, std::string &summary,
	std::string &error) {
    std::lock_guard<std::mutex> lock(deploymentMutex);
	try {
        using ContentBuildPaths::Require;
        std::optional<ContentBuildRecord> record;
		if (!ContentBuildRegistry().GetBuild(build, record, error))
			return false;
		Require(record && record->realmName == realm &&
					record->state == "STAGED",
            "Server apply requires a recorded STAGED build for this realm");
        bool exists = false;
        ContentServerStatus status;
		if (!ReadStatus(build, exists, status, error))
			return false;
        Require(exists, "Build has no server bundle record");
		Require(status.state == "STAGED" || status.state == "APPLIED",
				"Unknown server deployment state");
		Require(status.bundleFilename == record->filename + ".server.json" &&
					status.parityFilename ==
						record->filename + ".parity.json" &&
					ContentBuildHash::Valid(status.bundleSha256) &&
					ContentBuildHash::Valid(status.paritySha256),
				"Server sidecar registry is inconsistent");
        auto clientPath = SidecarPath(outputDirectory, record->filename);
        auto bundlePath = SidecarPath(outputDirectory, status.bundleFilename);
        auto parityPath = SidecarPath(outputDirectory, status.parityFilename);
        std::string actual;
        Require(ContentBuildHash::Calculate(clientPath, actual, error), error);
		Require(actual == record->sha256,
				"Client MPQ hash differs from recorded build");
        auto bundle = ReadFile(bundlePath);
        auto parity = ReadFile(parityPath);
        Require(HashText(bundle) == status.bundleSha256,
            "Server bundle hash differs from recorded build");
        Require(HashText(parity) == status.paritySha256,
            "Parity manifest hash differs from recorded build");
        std::vector<ResolvedServerItem> rows;
        std::vector<ResolvedExtendedCost> costs;
        std::vector<ResolvedVendorRow> vendors;
		std::vector<ResolvedCreatureTemplate> creatures;
		std::vector<ResolvedGameObjectTemplate> gameObjects;
		std::vector<ResolvedCreatureSpawn> spawns;
		if (!ContentServerBundle::ParseServer(bundle, realm, rows, error,
											  &costs, &vendors, &creatures,
											  &gameObjects, &spawns))
			return false;
		Require(!rows.empty() || !costs.empty() || !vendors.empty() ||
					!creatures.empty() || !gameObjects.empty() ||
					!spawns.empty(),
				"Server bundle has no managed rows to apply");
        auto parityObject = json::parse(parity);
        std::vector<ItemAllocation> retained;
		if (!ContentAllocationRegistry().Read(realm, retained, error))
			return false;
        auto allocations = ManifestAllocations(parityObject, retained);
        auto baseline = parityObject.at("baselineSha256").get<std::string>();
		Require(rows.empty() ? baseline.empty()
							 : ContentBuildHash::Valid(baseline),
				"Parity item baseline fingerprint is invalid");
		if (!ContentServerBundle::VerifyParity(
				parity, realm, build, baseline, record->sha256,
				status.bundleSha256, rows, allocations, error, costs, vendors,
				creatures, gameObjects, spawns))
			return false;
        std::vector<std::string> provenanceGuards;
        if (parityObject.contains("baselines"))
			for (auto const &snapshot : parityObject.at("baselines")) {
				ContentBaseline b;
				b.table = snapshot.at("table").get<std::string>();
                b.clientBuild = snapshot.at("clientBuild").get<std::uint32_t>();
				b.descriptorVersion =
					snapshot.at("descriptorVersion").get<std::uint32_t>();
                b.hash = snapshot.at("sha256").get<std::string>();
				provenanceGuards.push_back(
					ContentBaselineRegistry::HistoryCondition(b));
                auto descriptor = FindDbcDescriptor(b.clientBuild, b.table);
				Require(descriptor,
						"Historical baseline descriptor unavailable");
                for (auto const& lease : allocations)
                    for (auto const& field : descriptor->fields)
						if (field.allocationNamespace &&
							lease.resourceKind == field.allocationNamespace) {
							auto origin = b;
							origin.hash = lease.baselineSha256;
							provenanceGuards.push_back(
								ContentBaselineRegistry::HistoryCondition(
									origin));
                        }
            }
		for (auto const &condition : provenanceGuards) {
            auto check = WorldDatabase.Query("SELECT (" + condition + ")");
			Require(check && check->Fetch()[0].Get<std::uint64_t>() == 1,
					"Build/lease baseline acceptance history is missing");
		}
		for (auto const &row : rows) {
			auto found = std::find_if(
				allocations.begin(), allocations.end(), [&](auto const &lease) {
					return lease.resourceKind == "item.id" &&
						   lease.packageKey == row.packageKey &&
						   lease.symbol == row.symbol && lease.value == row.id;
				});
			Require(
				found != allocations.end() &&
					(found->baselineSha256 == baseline ||
					 parityObject.contains("baselines")),
				"Server item does not match retained allocation or baseline: " +
					row.packageKey + "/" + row.symbol);
		}
		if (!rows.empty() && !ValidateItemSchema(error))
			return false;
		bool hasCurrency =
			std::any_of(rows.begin(), rows.end(), [](auto const &row) {
				return row.currency.itemId != 0;
            });
        std::vector<bool> currencyExists(rows.size(), false);
        auto previousCurrencies = rows;
		if (hasCurrency) {
            std::set<std::uint32_t> bits, ids;
			Require(ContentCurrencyServer::Occupancy(realm, retained, bits, ids,
													 error),
					error);
            for (std::size_t i = 0; i < rows.size(); ++i)
				if (rows[i].currency.itemId) {
                    bool present = false;
					Require(ContentCurrencyServer::Prepare(
								rows[i], realm, present, previousCurrencies[i],
								error),
							error);
                    currencyExists[i] = present;
                }
        }
        std::vector<bool> costExists;
		if (!costs.empty()) {
            std::set<std::uint32_t> ids, items;
			Require(ContentExtendedCostServer::Occupancy(realm, retained, ids,
														 items, error),
					error);
			for (auto const &cost : costs) {
				Require(!ids.count(cost.id), "Extended-cost ID acquired an "
											 "external reference/collision");
                bool present=false;
				Require(ContentExtendedCostServer::Check(cost, realm, present,
														 error),
						error);
                costExists.push_back(present);
            }
        }
        std::vector<bool> vendorExists;
		for (auto const &vendor : vendors) {
            bool present=false;
			bool planned = std::any_of(
				creatures.begin(), creatures.end(),
				[&](auto const &c) { return c.entry == vendor.creatureEntry; });
			if (!planned)
				Require(
					ContentVendorServer::Check(vendor, realm, present, error),
					error);
            vendorExists.push_back(present);
        }
		std::vector<bool> creatureExists, gameObjectExists, spawnExists;
		for (auto const &r : creatures) {
			bool present = false;
			Require(ContentManagedServer::Check(r, realm, present, error),
					error);
			creatureExists.push_back(present);
		}
		for (auto const &r : gameObjects) {
			bool present = false;
			Require(ContentManagedServer::Check(r, realm, present, error),
					error);
			gameObjectExists.push_back(present);
		}
		for (auto const &r : spawns) {
			bool present = false;
			Require(ContentManagedServer::Check(r, realm, present, error),
					error);
			spawnExists.push_back(present);
		}
		struct Existing {
			bool item = false;
			bool owner = false;
			ContentItemOwner provenance;
		};
        std::vector<Existing> before;
		for (auto const &row : rows) {
            Existing state;
			Require(ContentServerOwnership::ReadOwner(row.id, state.owner,
													  state.provenance, error),
					error);
            bool itemExists = false;
            std::string currentRow;
			Require(ContentServerOwnership::ReadCurrentRow(row.id, itemExists,
														   currentRow, error),
					error);
            state.item = itemExists;
			Require(ContentServerOwnership::Classify(
						itemExists, state.owner, state.provenance, realm,
						row.packageKey, row.symbol,
						currentRow) != ContentItemOwnershipAction::Conflict,
					"Retained allocation has an unowned or drifted "
					"item_template collision: " +
						std::to_string(row.id));
            before.push_back(std::move(state));
        }
		if (status.state == "APPLIED") {
            for (std::size_t i = 0; i < rows.size(); ++i)
				Require(before[i].owner &&
							before[i].provenance.rowJson ==
								ContentServerBundle::RowJson(rows[i]),
						"APPLIED state has item_template drift; inspect "
						"ownership before retrying");
            for (auto const& row : rows)
                if (row.currency.itemId)
					Require(ContentCurrencyServer::Verify(
								row, realm, build, status.bundleSha256, error),
							error);
            for (auto const& cost : costs)
				Require(ContentExtendedCostServer::Verify(
							cost, realm, build, status.bundleSha256, error),
						error);
            for (auto const& vendor : vendors)
				Require(ContentVendorServer::Verify(vendor, realm, build,
													status.bundleSha256, error),
						error);
			for (auto const &r : creatures)
				Require(ContentManagedServer::Verify(
							r, realm, build, status.bundleSha256, error),
						error);
			for (auto const &r : gameObjects)
				Require(ContentManagedServer::Verify(
							r, realm, build, status.bundleSha256, error),
						error);
			for (auto const &r : spawns)
				Require(ContentManagedServer::Verify(
							r, realm, build, status.bundleSha256, error),
						error);
			summary = "Server content already APPLIED and verified. Restart "
					  "worldserver for item-template visibility.";
            return true;
        }
        auto tx = WorldDatabase.BeginTransaction();
		tx->Append("INSERT INTO content_manager_build_lock (id) VALUES (1) ON "
				   "DUPLICATE KEY UPDATE id=1");
		if (hasCurrency) {
			// Lock the small overlay range before checking its alternate
			// ItemID/BitIndex keys.
            tx->Append("UPDATE currencytypes_dbc SET ID=ID");
            tx->Append("UPDATE content_manager_currency_owner SET entry=entry");
        }
		if (!costs.empty()) {
            tx->Append("UPDATE itemextendedcost_dbc SET ID=ID");
			tx->Append(
				"UPDATE content_manager_extended_cost_owner SET entry=entry");
        }
		// A failed guard deliberately duplicates a transaction-local sentinel
		// primary key, aborting the entire InnoDB transaction. Give each guard
		// a distinct sentinel so SQL errors identify the failed assertion.
		std::uint32_t guardId = 1;

		auto guard = [&](std::string const& condition) {
		    ++guardId;

		    LOG_INFO("module",
		             "Content Manager activation guard {}: {}",
		             guardId, condition);

		    tx->Append(
		        "INSERT INTO content_manager_build_lock (id) VALUES (" +
		        std::to_string(guardId) +
		        ") ON DUPLICATE KEY UPDATE id=id");

		    tx->Append(
		        "INSERT INTO content_manager_build_lock (id) SELECT " +
		        std::to_string(guardId) +
		        " WHERE NOT (" + condition + ")");
		};
        auto registryCondition = [&](char const* serverState) {
			return "EXISTS(SELECT 1 FROM content_manager_build b JOIN "
				   "content_manager_server_build s "
				   "ON s.build_number=b.build_number WHERE b.build_number=" +
				   std::to_string(build) + " AND b.realm_name=" +
				   ContentServerBundle::SqlIdentityText(realm) +
				   " AND b.state='STAGED' AND b.sha256=" +
				   ContentServerBundle::SqlIdentityText(record->sha256) +
				   " AND b.filename=" +
				   ContentServerBundle::SqlIdentityText(record->filename) +
				   " AND s.server_state=" +
				   ContentServerBundle::SqlIdentityText(serverState) +
				   " AND s.bundle_sha256=" +
				   ContentServerBundle::SqlIdentityText(status.bundleSha256) +
				   " AND s.parity_sha256=" +
				   ContentServerBundle::SqlIdentityText(status.paritySha256) +
				   ")";
        };
        guard(registryCondition("STAGED"));
		for (std::size_t i = 0; i < creatures.size(); ++i) {
			tx->Append("UPDATE creature_template SET entry=entry WHERE entry=" +
					   std::to_string(creatureExists[i]
										  ? creatures[i].entry
										  : creatures[i].copyFrom));
			guard(ContentManagedServer::Condition(creatures[i], realm,
												  creatureExists[i]));
			for (auto const &sql : ContentManagedServer::ApplySql(
					 creatures[i], realm, creatureExists[i], build,
					 status.bundleSha256))
				tx->Append(sql);
		}
		for (std::size_t i = 0; i < gameObjects.size(); ++i) {
			tx->Append(
				"UPDATE gameobject_template SET entry=entry WHERE entry=" +
				std::to_string(gameObjectExists[i] ? gameObjects[i].entry
												   : gameObjects[i].copyFrom));
			guard(ContentManagedServer::Condition(gameObjects[i], realm,
												  gameObjectExists[i]));
			for (auto const &sql : ContentManagedServer::ApplySql(
					 gameObjects[i], realm, gameObjectExists[i], build,
					 status.bundleSha256))
				tx->Append(sql);
		}
		for (std::size_t i = 0; i < vendors.size(); ++i) {
			tx->Append(
				"UPDATE creature_template SET npcflag=npcflag WHERE entry=" +
				std::to_string(vendors[i].creatureEntry));
			tx->Append("UPDATE npc_vendor SET slot=slot WHERE entry=" +
					   std::to_string(vendors[i].creatureEntry));
			guard(ContentVendorServer::Condition(vendors[i], realm,
												 vendorExists[i]));
        }
        for (std::size_t i=0;i<costs.size();++i)
			guard(ContentExtendedCostServer::Condition(costs[i], realm,
													   costExists[i]));
		for (auto const &condition : provenanceGuards)
			guard(condition);
		for (std::size_t i = 0; i < rows.size(); ++i) {
            auto const& row = rows[i];
            if (row.currency.itemId)
				guard(ContentCurrencyServer::Condition(
					previousCurrencies[i], realm, currencyExists[i]));
            if (!row.currency.categorySymbol.empty())
				guard(ContentCurrencyServer::CategoryCondition(
					realm, row.packageKey, row.currency.categoryId));
            if (!before[i].owner)
				guard("NOT EXISTS(SELECT 1 FROM item_template WHERE entry=" +
					  std::to_string(row.id) +
					  ") AND NOT EXISTS(SELECT 1 FROM "
					  "content_manager_item_owner WHERE entry=" +
					  std::to_string(row.id) + ")");
            else
				guard("EXISTS(SELECT 1 FROM content_manager_item_owner WHERE "
					  "entry=" +
					  std::to_string(row.id) + " AND realm_name=" +
					  ContentServerBundle::SqlIdentityText(realm) +
					  " AND package_key=" +
					  ContentServerBundle::SqlIdentityText(row.packageKey) +
					  " AND symbol=" +
					  ContentServerBundle::SqlIdentityText(row.symbol) +
					  " AND resource_kind=" +
					  ContentServerBundle::SqlIdentityText("item.id") +
					  " AND row_json=" +
					  ContentServerBundle::SqlIdentityText(
						  before[i].provenance.rowJson) +
					  ")");
        }
        for (auto const& allocation : allocations)
			guard(
				"EXISTS(SELECT 1 FROM content_manager_allocation WHERE "
				"realm_name=" +
				ContentServerBundle::SqlIdentityText(realm) +
				" AND package_key=" +
				ContentServerBundle::SqlIdentityText(allocation.packageKey) +
				" AND symbol=" +
				ContentServerBundle::SqlIdentityText(allocation.symbol) +
				" AND resource_kind=" +
				ContentServerBundle::SqlIdentityText(allocation.resourceKind) +
				" AND allocated_value=" + std::to_string(allocation.value) +
				" AND baseline_sha256=" +
				ContentServerBundle::SqlIdentityText(
					allocation.baselineSha256) +
				" AND policy_version=" +
				std::to_string(allocation.policyVersion) + ")");
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].currency.itemId)
				for (auto const &sql : ContentCurrencyServer::ApplySql(
						 rows[i], realm, currencyExists[i], build,
						 status.bundleSha256,
						 previousCurrencies[i].currency.categoryId))
                    tx->Append(sql);
        for (std::size_t i=0;i<costs.size();++i)
			for (auto const &sql : ContentExtendedCostServer::ApplySql(
					 costs[i], realm, costExists[i], build,
					 status.bundleSha256))
                tx->Append(sql);
        for (std::size_t i=0;i<vendors.size();++i)
			for (auto const &sql : ContentVendorServer::ApplySql(
					 vendors[i], realm, vendorExists[i], build,
					 status.bundleSha256))
				tx->Append(sql);
		for (std::size_t i = 0; i < spawns.size(); ++i) {
			tx->Append("UPDATE creature SET guid=guid WHERE guid=" +
					   std::to_string(spawns[i].guid));
			guard(ContentManagedServer::Condition(spawns[i], realm,
												  spawnExists[i]));
			for (auto const &sql : ContentManagedServer::ApplySql(
					 spawns[i], realm, spawnExists[i], build,
					 status.bundleSha256))
                tx->Append(sql);
		}
        auto identityText = ContentServerBundle::SqlIdentityText;
		for (std::size_t i = 0; i < rows.size(); ++i) {
            auto const& row = rows[i];
            auto const rowJson = ContentServerBundle::RowJson(row);
			auto identity =
				"o.entry=" + std::to_string(row.id) +
				" AND o.realm_name=" + identityText(realm) +
				" AND o.package_key=" + identityText(row.packageKey) +
				" AND o.symbol=" + identityText(row.symbol) +
				" AND o.resource_kind=" + identityText("item.id");
			if (!before[i].owner) {
				tx->Append("INSERT INTO content_manager_item_owner "
						   "(entry,realm_name,package_key,symbol,"
						   "resource_kind,applied_build,artifact_sha256,row_"
						   "json) VALUES (" +
						   std::to_string(row.id) + "," +
						   ContentServerBundle::SqlText(realm) + "," +
						   ContentServerBundle::SqlText(row.packageKey) + "," +
						   ContentServerBundle::SqlText(row.symbol) +
						   ",'item.id'," + std::to_string(build) + "," +
						   ContentServerBundle::SqlText(status.bundleSha256) +
						   "," + ContentServerBundle::SqlText(rowJson) + ")");
                tx->Append(ContentServerBundle::InsertSql(row));
			} else {
				tx->Append(ContentServerBundle::UpdateSql(
					row, before[i].provenance.rowJson));
				tx->Append(
					"UPDATE content_manager_item_owner o JOIN item_template t "
					"ON t.entry=o.entry "
					"SET o.applied_build=" +
					std::to_string(build) + ",o.artifact_sha256=" +
					ContentServerBundle::SqlText(status.bundleSha256) +
					",o.row_json=" + ContentServerBundle::SqlText(rowJson) +
					" WHERE " + identity + " AND o.row_json=" +
					identityText(before[i].provenance.rowJson) + " AND " +
					ContentServerBundle::MatchSql(row, "t"));
            }
        }
        std::string condition;
		for (auto const &row : rows) {
			condition += " AND EXISTS(SELECT 1 FROM content_manager_allocation "
						 "a WHERE a.realm_name=" +
						 identityText(realm) +
						 " AND a.package_key=" + identityText(row.packageKey) +
						 " AND a.symbol=" + identityText(row.symbol) +
						 " AND a.resource_kind=" + identityText("item.id") +
						 " AND a.allocated_value=" + std::to_string(row.id) +
						 ")";
			condition +=
				" AND EXISTS(SELECT 1 FROM content_manager_item_owner o JOIN "
				"item_template t "
				"ON t.entry=o.entry WHERE o.entry=" +
				std::to_string(row.id) +
				" AND o.realm_name=" + identityText(realm) +
				" AND o.package_key=" + identityText(row.packageKey) +
				" AND o.symbol=" + identityText(row.symbol) +
				" AND o.resource_kind=" + identityText("item.id") +
				" AND o.row_json=" +
				identityText(ContentServerBundle::RowJson(row)) +
				" AND o.artifact_sha256=" + identityText(status.bundleSha256) +
				" AND " + ContentServerBundle::MatchSql(row, "t") + ")";
		}
		if (hasCurrency) {
			// The reference core orders its SQL overlay by ID before sizing the
			// ItemID index.
			guard("(SELECT ItemID FROM currencytypes_dbc ORDER BY ID DESC "
				  "LIMIT 1)="
                "(SELECT MAX(ItemID) FROM currencytypes_dbc)");
            for (auto const& row : rows)
                if (row.currency.itemId)
                    guard(ContentCurrencyServer::Condition(row, realm, true));
        }
		for (auto const &cost : costs) {
            guard(ContentExtendedCostServer::Condition(cost,realm,true));
			guard("EXISTS(SELECT 1 FROM content_manager_extended_cost_owner "
				  "WHERE entry=" +
				  std::to_string(cost.id) +
				  " AND applied_build=" + std::to_string(build) +
				  " AND artifact_sha256=" + identityText(status.bundleSha256) +
				  ")");
        }
		for (auto const &vendor : vendors)
			guard(ContentVendorServer::Condition(vendor, realm, true));
		for (auto const &r : creatures)
			guard(ContentManagedServer::Condition(r, realm, true));
		for (auto const &r : gameObjects)
			guard(ContentManagedServer::Condition(r, realm, true));
		for (auto const &r : spawns)
			guard(ContentManagedServer::Condition(r, realm, true));
        guard("1=1" + condition);
		tx->Append("UPDATE content_manager_server_build SET "
				   "server_state='APPLIED',applied_at=NOW() "
				   "WHERE build_number=" +
				   std::to_string(build) +
				   " AND server_state=" + identityText("STAGED") +
				   " AND bundle_sha256=" + identityText(status.bundleSha256) +
				   " AND parity_sha256=" + identityText(status.paritySha256) +
				   condition);
        guard(registryCondition("APPLIED"));
        WorldDatabase.DirectCommitTransaction(tx);
        ContentServerStatus after;
		Require(ReadStatus(build, exists, after, error) && exists &&
					after.state == "APPLIED",
				"Server apply transaction did not verify; inspect SQL logs and "
				"server status");
		for (auto const &row : rows) {
            ContentItemOwner owner;
            bool owned = false, itemExists = false;
            std::string currentRow;
			Require(ContentServerOwnership::ReadOwner(row.id, owned, owner,
													  error) &&
						ContentServerOwnership::ReadCurrentRow(
							row.id, itemExists, currentRow, error) &&
						owned && itemExists && owner.realm == realm &&
						owner.packageKey == row.packageKey &&
						owner.symbol == row.symbol &&
						owner.resourceKind == "item.id" &&
						owner.appliedBuild == build &&
						owner.artifactSha256 == status.bundleSha256 &&
						owner.rowJson == ContentServerBundle::RowJson(row) &&
						currentRow == owner.rowJson,
                "Post-apply item_template/ownership verification failed");
        }
        for (auto const& row : rows)
            if (row.currency.itemId)
				Require(ContentCurrencyServer::Verify(
							row, realm, build, status.bundleSha256, error),
						error);
        for (auto const& cost : costs)
			Require(ContentExtendedCostServer::Verify(
						cost, realm, build, status.bundleSha256, error),
					error);
        for (auto const& vendor : vendors)
			Require(ContentVendorServer::Verify(vendor, realm, build,
												status.bundleSha256, error),
					error);
		for (auto const &r : creatures)
			Require(ContentManagedServer::Verify(r, realm, build,
												 status.bundleSha256, error),
					error);
		for (auto const &r : gameObjects)
			Require(ContentManagedServer::Verify(r, realm, build,
												 status.bundleSha256, error),
					error);
		for (auto const &r : spawns)
			Require(ContentManagedServer::Verify(r, realm, build,
												 status.bundleSha256, error),
					error);
		summary = "Server content APPLIED for build " + std::to_string(build) +
				  ". Restart worldserver to load item_template, "
				  "currencytypes_dbc and itemextendedcost_dbc before testing; "
				  "client patch remains unactivated.";
        return true;
	} catch (std::exception const &exception) {
		error = exception.what();
		return false;
    }
}
