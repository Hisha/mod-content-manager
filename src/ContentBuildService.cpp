#include "ContentBuildService.h"

#include "ContentAllocationRegistry.h"
#include "ContentBaselineRegistry.h"
#include "ContentBuildHash.h"
#include "ContentBuildPaths.h"
#include "ContentBuildRegistry.h"
#include "ContentClientRequirement.h"
#include "ContentCurrencyServer.h"
#include "ContentExtendedCostServer.h"
#include "ContentManagedServer.h"
#include "ContentManager.h"
#include "ContentPackage.h"
#include "ContentPackageRegistry.h"
#include "ContentResourceAllocator.h"
#include "ContentServerBundle.h"
#include "ContentServerOwnership.h"
#include "CurrencyCategoryDbcComposer.h"
#include "CurrencyDbcComposer.h"
#include "DbcDescriptor.h"
#include "DbcReader.h"
#include "ItemDbcComposer.h"
#include "MpqBuilder.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <vector>

namespace {
std::mutex buildMutex;
}

std::string ContentBuildService::Number(std::uint32_t number) {
    auto text = std::to_string(number);
    return std::string(text.size() < 6 ? 6 - text.size() : 0, '0') + text;
}

std::string ContentBuildService::FilenameRealm(std::string const &realmName) {
    std::string safe;
	for (unsigned char c : realmName) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_')
            safe += static_cast<char>(c);
        else if (!safe.empty() && safe.back() != '-')
            safe += '-';
        if (safe.size() == 200)
            break;
    }
    while (!safe.empty() && safe.back() == '-')
        safe.pop_back();
    return safe.empty() ? "Realm" : safe;
}

ContentBuildResult ContentBuildService::Build(ContentManager const &manager,
											  std::string const &realmName,
											  Progress const &progress) const {
    namespace fs = std::filesystem;
    using namespace ContentBuildPaths;
    ContentBuildResult result;
    std::unique_lock<std::mutex> lock(buildMutex, std::try_to_lock);
	if (!lock.owns_lock()) {
        result.error = "Another content build is running";
        return result;
    }
	auto report = [&](std::string const &message) {
		if (progress)
			progress(message);
	};
	try {
        Require(manager.IsEnabled(), "Content Manager is disabled");
		Require(!realmName.empty() && realmName.size() <= 255 &&
					realmName.find('\0') == std::string::npos,
            "Current realm name is empty or exceeds 255 UTF-8 bytes");
        auto installed = ContentPackageRegistry().GetInstalledPackages();
        Require(installed.success, installed.error);
		Require(!installed.packages.empty(),
				"No packages are installed; there is nothing to build");
		Require(installed.packages.size() <=
					std::numeric_limits<std::uint32_t>::max(),
				"Too many installed packages");
        result.packageCount = installed.packages.size();

		struct Source {
			ContentPackageCandidate candidate;
			ContentPackageValidationResult validation;
		};
        std::map<std::string, std::vector<Source>> discovered;
		for (auto const &candidate : manager.ScanAvailablePackages()) {
            auto validation = ContentPackage(candidate.path).Validate();
            if (!validation.manifest.packageKey.empty())
				discovered[validation.manifest.packageKey].push_back(
					{candidate, validation});
        }
        std::vector<Source> selected;
        std::map<std::string, std::string> owners;
		for (auto const &package : installed.packages) {
            auto found = discovered.find(package.packageKey);
			Require(found != discovered.end(),
					"Package '" + package.packageKey +
						"': no discovered EPF source (missing or invalid)");
			if (found->second.size() != 1) {
				std::string message = "Package '" + package.packageKey +
									  "': duplicate discovered EPF sources";
                for (auto const& source : found->second)
					message += " [" + source.candidate.provider + ": " +
							   source.candidate.path.string() + "]";
                throw std::runtime_error(message);
            }
            auto source = found->second.front();
			source.validation =
				ContentPackage(source.candidate.path).Validate();
			Require(source.validation.valid,
					"Package '" + package.packageKey +
						"': " + source.validation.error);
            auto const& manifest = source.validation.manifest;
			Require(manifest.packageKey == package.packageKey,
					"Package key changed during discovery: " +
						package.packageKey);
			Require(manifest.version == package.version,
					"Package '" + package.packageKey + "': installed version " +
						package.version + ", discovered version " +
						manifest.version + "; refusing automatic upgrade");
			for (auto const &entry : manifest.content) {
                auto target = Fold(Target(entry.target));
                auto conflict = owners.find(target);
				Require(conflict == owners.end(),
						"Target collision '" + entry.target +
							"' between packages '" + package.packageKey +
							"' and '" +
							(conflict == owners.end() ? "" : conflict->second) +
							"'");
                owners.emplace(target, package.packageKey);
            }
            selected.push_back(std::move(source));
        }
		// Semantic composition owns the canonical Item.dbc MPQ target once
		// requested.
        std::vector<ResourceAllocationRequest> itemRequests;
        for (auto const& source : selected)
            for (auto const& row : source.validation.manifest.itemRows)
				itemRequests.push_back(
					{source.validation.manifest.packageKey, row.symbol});
        std::vector<ResourceAllocationRequest> currencyRequests;
        for (auto const& source : selected)
            for (auto const& row : source.validation.manifest.currencyRows)
				currencyRequests.push_back(
					{source.validation.manifest.packageKey, row.symbol,
					 "currency.known-bit"});
        std::vector<ResourceAllocationRequest> categoryRequests;
        for (auto const& source : selected)
			for (auto const &row :
				 source.validation.manifest.currencyCategories)
				categoryRequests.push_back(
					{source.validation.manifest.packageKey, row.symbol,
					 "currency-category.id"});
        std::vector<ResourceAllocationRequest> costRequests;
        for (auto const& source : selected)
            for (auto const& row : source.validation.manifest.extendedCosts)
				costRequests.push_back({source.validation.manifest.packageKey,
										row.symbol, "item-extended-cost.id"});
		std::vector<ResourceAllocationRequest> creatureRequests,
			gameObjectRequests, spawnRequests;
		for (auto const &source : selected) {
			auto const &m = source.validation.manifest;
			for (auto const &row : m.creatureTemplates)
				creatureRequests.push_back(
					{m.packageKey, row.symbol, "creature-template.id"});
			for (auto const &row : m.gameObjectTemplates)
				gameObjectRequests.push_back(
					{m.packageKey, row.symbol, "gameobject-template.id"});
			for (auto const &row : m.creatureSpawns)
				spawnRequests.push_back(
					{m.packageKey, row.symbol, "creature-spawn.guid"});
		}
        bool composingCost = !costRequests.empty();
		if (composingCost) {
            auto key = Fold("DBFilesClient/ItemExtendedCost.dbc");
			Require(
				!owners.count(key),
				"Raw ItemExtendedCost.dbc conflicts with semantic composition");
            owners.emplace(key, "ItemExtendedCost composer");
			Require(!itemRequests.empty(),
					"Extended costs require active semantic Item declarations");
        }
        bool composingCategory = !categoryRequests.empty();
		if (composingCategory) {
            auto key = Fold("DBFilesClient/CurrencyCategory.dbc");
			Require(
				!owners.count(key),
				"Raw CurrencyCategory.dbc conflicts with semantic composition");
            owners.emplace(key, "CurrencyCategory composer");
        }
        bool composingCurrency = !currencyRequests.empty();
		if (composingCurrency) {
            auto key = Fold("DBFilesClient/CurrencyTypes.dbc");
			Require(
				!owners.count(key),
				"Raw CurrencyTypes.dbc conflicts with semantic composition");
            owners.emplace(key, "CurrencyTypes composer");
        }
        bool composingItem = !itemRequests.empty();
		if (composingItem) {
            auto raw = owners.find(Fold("DBFilesClient/Item.dbc"));
			Require(raw == owners.end(),
					"Raw Item.dbc from package '" +
						(raw == owners.end() ? std::string() : raw->second) +
						"' conflicts with composed Item.dbc from package '" +
						itemRequests.front().packageKey + "'");
			owners.emplace(Fold("DBFilesClient/Item.dbc"),
						   itemRequests.front().packageKey + " (composer)");
        }
        // A file also cannot occupy a directory needed by another target.
		for (auto const &entry : owners) {
            auto prefix = entry.first;
			while (prefix.find('/') != std::string::npos) {
                prefix.resize(prefix.rfind('/'));
                auto conflict = owners.find(prefix);
				Require(conflict == owners.end(),
						"File/directory target collision '" + prefix +
							"' between packages '" + entry.second + "' and '" +
							(conflict == owners.end() ? "" : conflict->second) +
							"'");
            }
        }
		Require(owners.size() <= std::numeric_limits<std::uint32_t>::max(),
				"Too many cumulative files");
        ContentBuildRegistry builds;
        std::string error;
        if (!builds.NextNumber(result.buildNumber, error))
            throw std::runtime_error(error);
        std::vector<ItemAllocation> allocationPlan;
        std::vector<std::uint8_t> composedItemBytes;
        std::vector<ResolvedServerItem> resolvedServerRows;
        std::string baselineHash;
        std::string composedHash;
        std::vector<std::uint8_t> composedCurrencyBytes;
        std::string currencyBaselineHash, currencyHash;
        DbcReadResult currencyBaseline;
        std::set<std::uint32_t> currencyExternalBits, currencyExternalIds;
        std::uint32_t expectedItemRecords = 0;
        std::vector<ContentBaseline> baselines;
        std::map<std::string, std::set<std::string>> acceptedHistory;
		auto loadBaseline = [&](std::string const &table,
								std::string const &pin) {
			auto descriptor =
				FindDbcDescriptor(manager.GetClientBuild(), table);
			Require(descriptor, "No compiled descriptor for " + table +
									" and configured ClientBuild");
			auto b = ContentBaselineRegistry::Inspect(
				manager.GetBaselineDbcDirectory(), *descriptor);
            Require(ContentBaselineRegistry::Accept(b, pin, error), error);
			Require(ContentBaselineRegistry::History(b, acceptedHistory[table],
													 error),
					error);
            baselines.push_back(b);
			report("Accepted baseline: " + table + " descriptor v" +
				   std::to_string(b.descriptorVersion) + " SHA-256 " + b.hash);
            return b;
        };
        ContentBaseline costBaseline;
		std::set<std::uint32_t> costOccupied, costBaselineItems,
			costOverlayItems;
        std::vector<ResolvedExtendedCost> costs;
        std::vector<std::uint8_t> composedCostBytes;
        std::string costHash;
		std::vector<ResolvedCreatureTemplate> creatureTemplates;
		std::vector<ResolvedGameObjectTemplate> gameObjectTemplates;
		std::vector<ResolvedCreatureSpawn> creatureSpawns;
		if (composingCost) {
            costBaseline = loadBaseline("ItemExtendedCost", "");
            auto occupied = ItemExtendedCostDbc::Inspect(costBaseline.document);
            std::vector<ItemAllocation> retained;
			Require(
				ContentAllocationRegistry().Read(realmName, retained, error),
				error);
			Require(
				ContentExtendedCostServer::Occupancy(
					realmName, retained, costOccupied, costOverlayItems, error),
				error);
            costOccupied.insert(occupied.ids.begin(), occupied.ids.end());
            costBaselineItems = occupied.items;
        }
		auto planManaged =
			[&](std::string const &kind, ResourceAllocationPolicy const &policy,
				std::vector<ResourceAllocationRequest> const &requests) {
				if (requests.empty())
					return std::vector<ItemAllocation>{};
				std::vector<ItemAllocation> retained;
				Require(ContentAllocationRegistry().Read(realmName, retained,
														 error),
						error);
				std::set<std::uint32_t> occupied;
				Require(ContentManagedServer::Occupancy(
							kind, realmName, retained, occupied, error),
						error);
				auto plan = ContentResourceAllocator::Plan(
					realmName, policy, requests, retained, occupied,
					result.buildNumber,
					ContentManagedServer::DescriptorFingerprint(kind));
				allocationPlan.insert(allocationPlan.end(), plan.begin(),
									  plan.end());
				return plan;
			};
		auto creaturePlan =
			planManaged("creature-template.id",
						ContentResourceAllocator::CreatureTemplateIdPolicy(),
						creatureRequests);
		auto gameObjectPlan =
			planManaged("gameobject-template.id",
						ContentResourceAllocator::GameObjectTemplateIdPolicy(),
						gameObjectRequests);
		auto spawnPlan = planManaged(
			"creature-spawn.guid",
			ContentResourceAllocator::CreatureSpawnGuidPolicy(), spawnRequests);
		for (auto const &source : selected) {
			auto const &m = source.validation.manifest;
			for (auto const &d : m.creatureTemplates) {
				auto lease =
					std::find_if(creaturePlan.begin(), creaturePlan.end(),
								 [&](auto const &a) {
									 return a.packageKey == m.packageKey &&
											a.symbol == d.symbol;
								 });
				Require(lease != creaturePlan.end(),
						"Creature-template allocation missing");
				ResolvedCreatureTemplate row;
				Require(
					ContentManagedServer::ResolveDonor(
						d, m.packageKey, m.version, lease->value, row, error),
					error);
				bool exists = false;
				Require(
					ContentManagedServer::Check(row, realmName, exists, error),
					error);
				creatureTemplates.push_back(row);
				report("Planned creature-template.id: " + m.packageKey + "/" +
					   d.symbol + " = " + std::to_string(row.entry));
			}
			for (auto const &d : m.gameObjectTemplates) {
				auto lease =
					std::find_if(gameObjectPlan.begin(), gameObjectPlan.end(),
								 [&](auto const &a) {
									 return a.packageKey == m.packageKey &&
											a.symbol == d.symbol;
								 });
				Require(lease != gameObjectPlan.end(),
						"Gameobject-template allocation missing");
				ResolvedGameObjectTemplate row;
				Require(
					ContentManagedServer::ResolveDonor(
						d, m.packageKey, m.version, lease->value, row, error),
					error);
				bool exists = false;
				Require(
					ContentManagedServer::Check(row, realmName, exists, error),
					error);
				gameObjectTemplates.push_back(row);
				report("Planned gameobject-template.id: " + m.packageKey + "/" +
					   d.symbol + " = " + std::to_string(row.entry));
			}
			for (auto const &d : m.creatureSpawns) {
				auto lease = std::find_if(
					spawnPlan.begin(), spawnPlan.end(), [&](auto const &a) {
						return a.packageKey == m.packageKey &&
							   a.symbol == d.symbol;
					});
				auto creature =
					std::find_if(creatureTemplates.begin(),
								 creatureTemplates.end(), [&](auto const &c) {
									 return c.packageKey == m.packageKey &&
											c.symbol == d.creatureSymbol;
								 });
				Require(lease != spawnPlan.end() &&
							creature != creatureTemplates.end(),
						"Creature-spawn allocation/reference missing");
				ResolvedCreatureSpawn row{m.packageKey,
										  m.version,
										  d.symbol,
										  d.creatureSymbol,
										  lease->value,
										  creature->entry,
										  d.map,
										  d.spawnMask,
										  d.phaseMask,
										  d.respawnSeconds,
										  d.movementType,
										  d.x,
										  d.y,
										  d.z,
										  d.orientation,
										  d.wanderDistance};
				bool exists = false;
				Require(
					ContentManagedServer::Check(row, realmName, exists, error),
					error);
				creatureSpawns.push_back(row);
				report("Planned creature-spawn.guid: " + m.packageKey + "/" +
					   d.symbol + " = " + std::to_string(row.guid));
			}
		}
        std::vector<ResolvedCurrencyCategory> categories;
        std::vector<std::uint8_t> composedCategoryBytes;
        std::string categoryHash;
		if (composingCurrency) {
			Require(manager.GetClientBuild() == 12340,
					"CurrencyTypes supports build 12340 only");
			auto accepted = loadBaseline(
				"CurrencyTypes", manager.GetCurrencyTypesBaselineSha256());
            currencyBaseline = {true, {}, accepted.document};
            currencyBaselineHash = accepted.hash;
			auto occupancy =
				CurrencyDbcComposer::Inspect(currencyBaseline.document);
            std::vector<ItemAllocation> retained;
			Require(
				ContentAllocationRegistry().Read(realmName, retained, error),
				error);
			Require(ContentCurrencyServer::Occupancy(
						realmName, retained, currencyExternalBits,
						currencyExternalIds, error),
					error);
			currencyExternalBits.insert(occupancy.bits.begin(),
										occupancy.bits.end());
			currencyExternalIds.insert(occupancy.ids.begin(),
									   occupancy.ids.end());
			currencyExternalIds.insert(occupancy.items.begin(),
									   occupancy.items.end());
        }
		if (composingItem) {
			Require(manager.GetClientBuild() == 12340,
					"Item composition supports only client build 12340");
			auto accepted =
				loadBaseline("Item", manager.GetItemBaselineSha256());
            DbcReadResult baseline{true, {}, accepted.document};
            baselineHash = accepted.hash;
            fs::path baselinePath = accepted.source;
            std::set<std::uint32_t> baselineIDs;
            for (std::size_t i = 0; i < baseline.document.recordCount; ++i)
				Require(
					baselineIDs.insert(baseline.document.words[i * 8]).second,
					"Duplicate baseline Item ID");
            ContentAllocationRegistry registry;
            std::vector<ItemAllocation> retained;
            Require(registry.Read(realmName, retained, error), error);
            std::set<std::uint32_t> worldIDs;
            Require(registry.OccupiedWorldItems(worldIDs, error), error);
            worldIDs.insert(costOverlayItems.begin(), costOverlayItems.end());
			Require(ContentServerOwnership::ExcludeOwned(realmName, retained,
														 worldIDs, error),
					error);
            worldIDs.insert(costBaselineItems.begin(), costBaselineItems.end());
            worldIDs.insert(baselineIDs.begin(), baselineIDs.end());
			// CurrencyTypes.ID derives from ItemID; exclude both external keys
			// before Item allocation.
			worldIDs.insert(currencyExternalIds.begin(),
							currencyExternalIds.end());
            auto policy = ContentResourceAllocator::ItemIdPolicy(baselineIDs);
			auto itemPlan = ContentResourceAllocator::Plan(
				realmName, policy, itemRequests, retained, worldIDs,
				result.buildNumber, baselineHash, acceptedHistory["Item"]);
			allocationPlan.insert(allocationPlan.end(), itemPlan.begin(),
								  itemPlan.end());
            for (auto const& source : selected)
				for (auto const &server :
					 source.validation.manifest.serverItemRows) {
                    auto const& manifest = source.validation.manifest;
					auto allocation = std::find_if(
						allocationPlan.begin(), allocationPlan.end(),
						[&](auto const &a) {
							return a.packageKey == manifest.packageKey &&
								   a.symbol == server.symbol;
                    });
					auto client = std::find_if(
						manifest.itemRows.begin(), manifest.itemRows.end(),
						[&](auto const &row) {
                        return row.symbol == server.symbol;
                    });
					Require(allocation != allocationPlan.end() &&
								client != manifest.itemRows.end(),
							"Server item symbol lacks package-local Item "
							"allocation");
					resolvedServerRows.push_back(
						{manifest.packageKey, manifest.version, server.symbol,
                        allocation->value, 0, *client, server});
                }
            std::vector<PlannedItemRow> rows;
            for (auto const& source : selected)
				for (auto const &row : source.validation.manifest.itemRows) {
					auto found = std::find_if(
						allocationPlan.begin(), allocationPlan.end(),
						[&](auto const &a) {
							return a.packageKey ==
									   source.validation.manifest.packageKey &&
								   a.symbol == row.symbol;
                    });
					Require(found != allocationPlan.end(),
							"Missing Item ID allocation plan");
                    rows.push_back({found->value, row});
					report("Planned item.id: " + found->packageKey + "/" +
						   found->symbol + " = " +
						   std::to_string(found->value));
                }
			composedItemBytes =
				ItemDbcComposer::Compose(baseline.document, rows);
			expectedItemRecords = baseline.document.recordCount +
								  static_cast<std::uint32_t>(rows.size());
            report("Item baseline SHA-256: " + baselineHash);
			report("Item records: " +
				   std::to_string(baseline.document.recordCount) + " -> " +
				   std::to_string(baseline.document.recordCount + rows.size()));
            std::string recheckHash;
			Require(
				ContentBuildHash::Calculate(baselinePath, recheckHash, error) &&
					recheckHash == baselineHash,
                "Item baseline changed during composition");
        }
		if (composingCategory) {
            auto baseline = loadBaseline("CurrencyCategory", "");
			auto occupied = CurrencyCategoryDbcComposer::Occupancy(
				baseline.document, currencyBaseline.document);
            std::vector<ItemAllocation> retained;
			Require(
				ContentAllocationRegistry().Read(realmName, retained, error),
				error);
            std::set<std::uint32_t> external;
			Require(ContentCurrencyServer::CategoryOccupancy(
						realmName, retained, external, error),
					error);
            occupied.insert(external.begin(), external.end());
			auto plan = ContentResourceAllocator::Plan(
				realmName, ContentResourceAllocator::CurrencyCategoryIdPolicy(),
				categoryRequests, retained, occupied, result.buildNumber,
				baseline.hash, acceptedHistory["CurrencyCategory"]);
            for (auto const& source : selected)
				for (auto const &declaration :
					 source.validation.manifest.currencyCategories) {
                    auto const& package = source.validation.manifest.packageKey;
					auto lease = std::find_if(
						plan.begin(), plan.end(), [&](auto const &a) {
							return a.packageKey == package &&
								   a.symbol == declaration.symbol;
                    });
                    Require(lease != plan.end(), "Category allocation missing");
					categories.push_back({package, declaration.symbol,
										  lease->value, declaration.names});
					report("Planned currency-category.id: " + package + "/" +
						   declaration.symbol + " = " +
						   std::to_string(lease->value));
                }
			composedCategoryBytes = CurrencyCategoryDbcComposer::Compose(
				baseline.document, categories);
			allocationPlan.insert(allocationPlan.end(), plan.begin(),
								  plan.end());
        }
		if (composingCurrency) {
            std::vector<ItemAllocation> retained;
			Require(
				ContentAllocationRegistry().Read(realmName, retained, error),
				error);
			auto currencyPlan = ContentResourceAllocator::Plan(
				realmName, ContentResourceAllocator::CurrencyKnownBitPolicy(),
				currencyRequests, retained, currencyExternalBits,
				result.buildNumber, currencyBaselineHash,
				acceptedHistory["CurrencyTypes"]);
            std::vector<ResolvedCurrency> rows;
            for (auto const& source : selected)
				for (auto const &declaration :
					 source.validation.manifest.currencyRows) {
                    auto const& package = source.validation.manifest.packageKey;
					auto item = std::find_if(
						resolvedServerRows.begin(), resolvedServerRows.end(),
						[&](auto const &r) {
							return r.packageKey == package &&
								   r.symbol == declaration.itemSymbol;
                    });
					auto bit =
						std::find_if(currencyPlan.begin(), currencyPlan.end(),
									 [&](auto const &a) {
										 return a.packageKey == package &&
												a.symbol == declaration.symbol;
                    });
					Require(item != resolvedServerRows.end() &&
								bit != currencyPlan.end(),
							"Currency plan reference missing");
                    std::uint32_t category = 0;
                    if (declaration.categorySymbol.empty())
						category = CurrencyDbcComposer::Category(
							currencyBaseline.document,
							declaration.categoryCopyFromItem);
					else {
						auto found = std::find_if(
							categories.begin(), categories.end(),
							[&](auto const &c) {
								return c.packageKey == package &&
									   c.symbol == declaration.categorySymbol;
                        });
						Require(found != categories.end(),
								"Resolved category reference missing");
                        category = found->id;
                    }
					item->currency = {declaration.symbol, item->id, category,
									  bit->value, declaration.categorySymbol};
                    rows.push_back(item->currency);
					report("Planned currency.known-bit: " + package + "/" +
						   declaration.symbol + " = " +
						   std::to_string(bit->value));
					report(
						"Currency parity: CurrencyTypes.ID/ItemID = "
						"currencytypes_dbc.ID/ItemID = item_template.entry = " +
						std::to_string(item->id) + "; BitIndex = " +
						std::to_string(bit->value) + "; BagFamily = 8192");
                }
			composedCurrencyBytes =
				CurrencyDbcComposer::Compose(currencyBaseline.document, rows);
			allocationPlan.insert(allocationPlan.end(), currencyPlan.begin(),
								  currencyPlan.end());
            std::string recheck;
			Require(ContentBuildHash::Calculate(
						fs::canonical(manager.GetBaselineDbcDirectory()) /
							"CurrencyTypes.dbc",
						recheck, error) &&
						recheck == currencyBaselineHash,
					"CurrencyTypes baseline changed during composition");
            report("CurrencyTypes baseline SHA-256: " + currencyBaselineHash);
			report("CurrencyTypes records: " +
				   std::to_string(currencyBaseline.document.recordCount) +
				   " -> " +
				   std::to_string(currencyBaseline.document.recordCount +
								  rows.size()));
        }
		if (composingCost) {
            std::vector<ItemAllocation> retained;
			Require(
				ContentAllocationRegistry().Read(realmName, retained, error),
				error);
			auto plan = ContentResourceAllocator::Plan(
				realmName, ContentResourceAllocator::ItemExtendedCostIdPolicy(),
				costRequests, retained, costOccupied, result.buildNumber,
				costBaseline.hash, acceptedHistory["ItemExtendedCost"]);
            for (auto const& source : selected)
				for (auto const &declaration :
					 source.validation.manifest.extendedCosts) {
                    auto const& manifest = source.validation.manifest;
					auto lease = std::find_if(
						plan.begin(), plan.end(), [&](auto const &a) {
							return a.packageKey == manifest.packageKey &&
								   a.symbol == declaration.symbol;
                    });
					Require(lease != plan.end(),
							"Extended-cost allocation missing");
                    ResolvedExtendedCost cost;
					cost.packageKey = manifest.packageKey;
					cost.packageVersion = manifest.version;
					cost.symbol = declaration.symbol;
					cost.id = lease->value;
					cost.honorPoints = declaration.honorPoints;
					cost.arenaPoints = declaration.arenaPoints;
					cost.arenaBracket = declaration.arenaBracket;
					cost.requiredArenaRating = declaration.requiredArenaRating;
					for (auto const &requirement : declaration.requirements) {
						auto item = std::find_if(
							resolvedServerRows.begin(),
							resolvedServerRows.end(), [&](auto const &row) {
								return row.packageKey ==
										   requirement.packageKey &&
									   row.symbol == requirement.symbol;
                        });
						Require(item != resolvedServerRows.end(),
								"Unresolved extended-cost item: " +
									requirement.packageKey + "/" +
									requirement.symbol);
						cost.requirements.push_back(
							{requirement.packageKey, requirement.symbol,
							 item->id, requirement.count});
                    }
                    bool exists=false;
					Require(ContentExtendedCostServer::Check(cost, realmName,
															 exists, error),
							error);
                    costs.push_back(cost);
					report("Planned item-extended-cost.id: " + cost.packageKey +
						   "/" + cost.symbol + " = " + std::to_string(cost.id));
                }
			composedCostBytes =
				ItemExtendedCostDbc::Compose(costBaseline.document, costs);
			allocationPlan.insert(allocationPlan.end(), plan.begin(),
								  plan.end());
        }
        std::vector<ResolvedVendorRow> vendors;
        std::set<std::uint32_t> vendorEntries;
        for (auto const& source : selected)
			for (auto const &declaration :
				 source.validation.manifest.vendorRows) {
                auto const& m=source.validation.manifest;
				auto cost = std::find_if(
					costs.begin(), costs.end(), [&](auto const &c) {
						return c.packageKey == m.packageKey &&
							   c.symbol == declaration.extendedCostSymbol;
					});
				Require(cost != costs.end(),
						"Unresolved vendor extended-cost symbol");
				auto creatureEntry = declaration.creatureEntry;
				if (!declaration.creatureSymbol.empty()) {
					auto creature = std::find_if(
						creatureTemplates.begin(), creatureTemplates.end(),
						[&](auto const &c) {
							return c.packageKey == m.packageKey &&
								   c.symbol == declaration.creatureSymbol;
						});
					Require(creature != creatureTemplates.end(),
							"Unresolved symbolic vendor creature");
					creatureEntry = creature->entry;
				}
				Require(vendorEntries.insert(creatureEntry).second,
						"Multiple packages claim the same vendor creature");
				ResolvedVendorRow row{
					m.packageKey,		m.version,
					declaration.symbol, declaration.extendedCostSymbol,
					creatureEntry,		declaration.itemEntry,
					cost->id,			0};
				if (declaration.creatureSymbol.empty())
					Require(ContentVendorServer::Prepare(row, realmName, error),
							error);
				else {
					auto managed = std::find_if(
						creatureTemplates.begin(), creatureTemplates.end(),
						[&](auto const &c) {
							return c.entry == creatureEntry;
						});
					row.originalFlags = managed->npcFlags;
					row.flagsManaged = true;
					managed->npcFlags |= 128;
				}
                vendors.push_back(row);
				report("Planned vendor relationship: " + row.packageKey + "/" +
					   row.symbol + " -> " + row.costSymbol);
            }
		Require(!manager.GetOutputDirectory().empty() &&
					!manager.GetWorkDirectory().empty(),
				"Build directories must not be empty");
		auto filename = FilenameRealm(realmName) + "-Content-" +
						Number(result.buildNumber) + ".mpq";
		result.outputPath =
			fs::absolute(fs::path(manager.GetOutputDirectory()) / filename);
        RejectLinks(result.outputPath);
		Require(!fs::exists(fs::symlink_status(result.outputPath)),
				"Candidate MPQ already exists; preserved: " +
					result.outputPath.string());
        auto serverPath = fs::path(result.outputPath.string() + ".server.json");
        auto parityPath = fs::path(result.outputPath.string() + ".parity.json");
		RejectLinks(serverPath);
		RejectLinks(parityPath);
		Require(!fs::exists(fs::symlink_status(serverPath)) &&
					!fs::exists(fs::symlink_status(parityPath)),
            "Candidate server/parity sidecar already exists; preserved");
        RejectLinks(manager.GetWorkDirectory());
        fs::create_directories(manager.GetWorkDirectory());
        auto workRoot = fs::canonical(manager.GetWorkDirectory());
        std::random_device random;
		for (unsigned attempt = 0; attempt < 100 && result.workspace.empty();
			 ++attempt) {
			auto candidate = workRoot / ("build-" + Number(result.buildNumber) +
										 "-" + std::to_string(random()));
            if (fs::create_directory(candidate))
                result.workspace = candidate;
        }
		Require(!result.workspace.empty(),
				"Could not reserve a new build workspace");
        report("Building realm content patch...");
        report("Realm: " + realmName);
        report("Build: " + Number(result.buildNumber));
        report("Installed packages: " + std::to_string(result.packageCount));
        report("Build workspace: " + result.workspace.string());
		for (auto const &source : selected) {
            auto const& manifest = source.validation.manifest;
            report("Staging: " + manifest.packageKey + " " + manifest.version);
			auto staged = ContentPackage(source.candidate.path)
							  .StageInto(result.workspace, manifest);
			Require(staged.success, "Package '" + manifest.packageKey +
										"': staging failed: " + staged.error);
            result.fileCount += staged.stagedFiles.size();
			report("  " + std::to_string(staged.stagedFiles.size()) +
				   " file(s)");
        }
		Require(result.fileCount + (composingItem ? 1 : 0) +
						(composingCurrency ? 1 : 0) +
						(composingCategory ? 1 : 0) + (composingCost ? 1 : 0) ==
					owners.size(),
            "Staged file count does not match the declared cumulative set");
		if (composingItem) {
            auto target = result.workspace / "DBFilesClient" / "Item.dbc";
            RejectLinks(target);
            fs::create_directories(target.parent_path());
			Require(!fs::exists(fs::symlink_status(target)),
					"Composed Item target already exists in workspace");
			{
				std::ofstream output(target,
									 std::ios::binary | std::ios::trunc);
              Require(output.is_open(), "Cannot create composed Item.dbc");
				output.write(
					reinterpret_cast<char const *>(composedItemBytes.data()),
                  static_cast<std::streamsize>(composedItemBytes.size()));
				Require(output.good(), "Cannot write composed Item.dbc");
			}
			auto reparsed =
				DbcReader::Read(target, *FindDbcDescriptor(12340, "Item"));
			Require(reparsed.valid,
					"Generated Item.dbc failed read-back: " + reparsed.error);
			Require(reparsed.document.recordCount == expectedItemRecords &&
						reparsed.document.fieldCount == 8 &&
						reparsed.document.recordSize == 32,
					"Composed Item.dbc dimensions mismatch");
			Require(ContentBuildHash::Calculate(target, composedHash, error),
					"Composed Item SHA-256 failed: " + error);
            report("Composed Item.dbc SHA-256: " + composedHash);
			for (auto &row : resolvedServerRows) {
                bool found = false;
				for (std::size_t index = 0;
					 index < reparsed.document.recordCount; ++index)
					if (reparsed.document.words[index * 8] == row.id) {
                        row.displayId = reparsed.document.words[index * 8 + 5];
						Require(row.displayId &&
									reparsed.document.words[index * 8 + 1] ==
										row.client.classID &&
									reparsed.document.words[index * 8 + 2] ==
										row.client.subclassID &&
									reparsed.document.words[index * 8 + 6] ==
										row.client.inventoryType,
                            "Client/server Item row parity failed");
                        found = true;
                        break;
                    }
				Require(found,
						"Resolved server item missing from composed Item.dbc");
				report("Parity PASS: " + row.packageKey + "/" + row.symbol +
					   " Item.dbc.ID = item_template.entry = " +
					   std::to_string(row.id));
            }
            ++result.fileCount;
        }
		if (composingCurrency) {
			auto target =
				result.workspace / "DBFilesClient" / "CurrencyTypes.dbc";
            RejectLinks(target);
            fs::create_directories(target.parent_path());
			Require(!fs::exists(fs::symlink_status(target)),
					"CurrencyTypes target already exists");
            {
                std::ofstream output(target, std::ios::binary);
                Require(output.is_open(), "Cannot create CurrencyTypes.dbc");
				output.write(reinterpret_cast<char const *>(
								 composedCurrencyBytes.data()),
							 composedCurrencyBytes.size());
                Require(output.good(), "Cannot write CurrencyTypes.dbc");
            }
			auto parsed = DbcReader::Read(
				target, *FindDbcDescriptor(12340, "CurrencyTypes"));
			Require(parsed.valid && DbcReader::Serialize(parsed.document) ==
										composedCurrencyBytes,
                "CurrencyTypes disk readback failed");
			Require(ContentBuildHash::Calculate(target, currencyHash, error),
					error);
            report("Composed CurrencyTypes.dbc SHA-256: " + currencyHash);
            ++result.fileCount;
        }
		if (composingCategory) {
			auto target =
				result.workspace / "DBFilesClient" / "CurrencyCategory.dbc";
            RejectLinks(target);
            fs::create_directories(target.parent_path());
			Require(!fs::exists(fs::symlink_status(target)),
					"CurrencyCategory target already exists");
            {
                std::ofstream out(target, std::ios::binary);
                Require(out.is_open(), "Cannot create CurrencyCategory.dbc");
				out.write(reinterpret_cast<char const *>(
							  composedCategoryBytes.data()),
						  composedCategoryBytes.size());
                Require(out.good(), "Cannot write CurrencyCategory.dbc");
            }
			auto parsed = DbcReader::Read(
				target, *FindDbcDescriptor(manager.GetClientBuild(),
										   "CurrencyCategory"));
			Require(parsed.valid && DbcReader::Serialize(parsed.document) ==
										composedCategoryBytes,
					"Category disk readback failed");
			Require(ContentBuildHash::Calculate(target, categoryHash, error),
					error);
            for (auto const& row : resolvedServerRows)
				if (!row.currency.categorySymbol.empty()) {
					auto definition = std::find_if(
						categories.begin(), categories.end(),
						[&](auto const &c) {
							return c.packageKey == row.packageKey &&
								   c.symbol == row.currency.categorySymbol &&
								   c.id == row.currency.categoryId;
                    });
					Require(
						definition != categories.end(),
						"Client/server category relationship parity failed");
                    bool found = false;
					for (std::size_t i = 0; i < parsed.document.recordCount;
						 ++i)
						if (parsed.document.words[i * 19] == definition->id) {
							Require(CurrencyCategoryDbcComposer::Name(
										parsed.document, i, 0) ==
										definition->names.at("enUS"),
                                "Generated category name parity failed");
                            found = true;
                        }
                    Require(found, "Client category row missing");
                }
            ++result.fileCount;
            report("Composed CurrencyCategory.dbc SHA-256: " + categoryHash);
        }
		if (composingCost) {
			auto target =
				result.workspace / "DBFilesClient" / "ItemExtendedCost.dbc";
            RejectLinks(target);
            fs::create_directories(target.parent_path());
			Require(!fs::exists(fs::symlink_status(target)),
					"ItemExtendedCost target already exists");
            {
                std::ofstream out(target,std::ios::binary);
                Require(out.is_open(), "Cannot create ItemExtendedCost.dbc");
				out.write(
					reinterpret_cast<char const *>(composedCostBytes.data()),
					composedCostBytes.size());
                Require(out.good(), "Cannot write ItemExtendedCost.dbc");
            }
			auto parsed = DbcReader::Read(
				target, *FindDbcDescriptor(manager.GetClientBuild(),
										   "ItemExtendedCost"));
			Require(parsed.valid && DbcReader::Serialize(parsed.document) ==
										composedCostBytes,
					"ItemExtendedCost disk readback failed");
            ItemExtendedCostDbc::Inspect(parsed.document);
			for (auto const &cost : costs) {
                auto words=ItemExtendedCostDbc::Words(cost);
                bool found=false;
                for (std::size_t i=0;i<parsed.document.recordCount;++i)
					if (parsed.document.words[i * 16] == cost.id) {
						Require(
							std::equal(words.begin(), words.end(),
									   parsed.document.words.begin() + i * 16),
							"Extended-cost client/server parity mismatch");
                        found=true;
                    }
                Require(found,"Extended-cost client row missing");
            }
			Require(ContentBuildHash::Calculate(target, costHash, error),
					error);
            ++result.fileCount;
            report("Composed ItemExtendedCost.dbc SHA-256: " + costHash);
        }
		// Recheck every accepted snapshot immediately before artifact assembly.
		// The commit also guards registry identities.
		for (auto const &baseline : baselines) {
			auto current = ContentBaselineRegistry::Inspect(
				manager.GetBaselineDbcDirectory(),
                *FindDbcDescriptor(baseline.clientBuild, baseline.table));
			Require(current.hash == baseline.hash,
					"Baseline changed during build: " + baseline.table);
        }
        report("Cumulative files: " + std::to_string(result.fileCount));
        auto mpq = MpqBuilder().Build(result.workspace, result.outputPath);
        Require(mpq.success, "MPQ build failed: " + mpq.error);
        result.mpqCreated = true;
        result.fileCount = mpq.fileCount;
        report("MPQ built successfully.");
        report("MPQ: " + result.outputPath.string());
        report("Packages: " + std::to_string(result.packageCount));
        report("MPQ files: " + std::to_string(result.fileCount));
        std::string hash;
        if (!ContentBuildHash::Calculate(result.outputPath, hash, error))
            throw std::runtime_error("MPQ SHA256 failed: " + error);
        report("SHA256: " + hash);
        ContentServerBuildRecord serverRecord;
        {
			auto writeSidecar = [&](fs::path const &path,
									std::string const &contents) {
                RejectLinks(path);
				Require(!fs::exists(fs::symlink_status(path)),
						"Sidecar already exists: " + path.string());
                std::ofstream output(path, std::ios::binary | std::ios::trunc);
				Require(output.is_open(),
						"Cannot create sidecar: " + path.string());
				output.write(contents.data(),
							 static_cast<std::streamsize>(contents.size()));
				Require(output.good(),
						"Cannot write sidecar: " + path.string());
            };
			writeSidecar(serverPath, ContentServerBundle::ServerJson(
										 realmName, resolvedServerRows, costs,
										 vendors, creatureTemplates,
										 gameObjectTemplates, creatureSpawns));
            serverRecord.bundleFilename = serverPath.filename().string();
			Require(ContentBuildHash::Calculate(
						serverPath, serverRecord.bundleSha256, error),
                "Server bundle SHA-256 failed: " + error);
			writeSidecar(parityPath,
						 ContentServerBundle::ParityJson(
							 realmName, result.buildNumber, allocationPlan,
							 resolvedServerRows, baselineHash, composedHash,
							 hash, serverRecord.bundleSha256, currencyHash,
							 categoryHash, categories, baselines, costHash,
							 costs, vendors, creatureTemplates,
							 gameObjectTemplates, creatureSpawns));
            serverRecord.parityFilename = parityPath.filename().string();
			Require(ContentBuildHash::Calculate(
						parityPath, serverRecord.paritySha256, error),
                "Parity manifest SHA-256 failed: " + error);
			report("Server rows: item_template: " +
				   std::to_string(resolvedServerRows.size()) +
				   "; itemextendedcost_dbc: " + std::to_string(costs.size()));
			report("Server bundle: " + serverPath.string() + " SHA-256 " +
				   serverRecord.bundleSha256);
			report("Parity manifest: " + parityPath.string() + " SHA-256 " +
				   serverRecord.paritySha256);
        }
        // Aggregated client requirements belong to THIS generated build. They
        // are captured only from the exact manifests participating now, and are
        // recorded immutably; later package state never rewrites them.
        std::vector<std::vector<std::string>> requirementSets;
        for (auto const& source : selected)
			requirementSets.push_back(
				source.validation.manifest.clientRequirements);
		auto clientRequirements =
			ContentClientRequirement::Merge(requirementSets);
        {
            std::string line;
            for (auto const& requirement : clientRequirements)
                line += (line.empty() ? "" : ", ") + requirement;
            report("Client requirements: " + (line.empty() ? "none" : line));
        }
		ContentBuildRecord record{
			result.buildNumber,
			realmName,
			filename,
			static_cast<std::uint32_t>(result.packageCount),
			static_cast<std::uint32_t>(result.fileCount),
			"STAGED",
			hash,
			clientRequirements};
		bool committed = !allocationPlan.empty()
							 ? ContentAllocationRegistry().CommitComposed(
								   record, allocationPlan, serverRecord, error,
								   baselines, costs)
            : builds.Record(record, serverRecord, error);
        if (!committed)
			throw std::runtime_error(
				"MPQ was created, but recording the build/allocation could not "
				"be verified: " +
				error);
        result.recorded = true;
        report("Build state: STAGED");
        report("Build recorded successfully.");
		result.cleaned =
			Cleanup(result.workspace, workRoot, result.cleanupWarning);
        result.success = true;
        if (result.cleaned)
            report("Build workspace cleanup completed.");
        else
			report("WARNING: MPQ and build record are valid, but workspace "
				   "cleanup failed: " +
				   result.cleanupWarning);
	} catch (std::exception const &exception) {
        result.error = exception.what();
        // No artifact or workspace deletion on failure. A DB failure after MPQ
        // publication is explicitly reported for administrator inspection.
    }
    return result;
}
