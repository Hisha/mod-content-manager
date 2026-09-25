#include "ContentBuildHash.h"
#include "ContentManagedServer.h"
#include "ContentPackage.h"
#include "ContentResourceAllocator.h"
#include "ContentServerBundle.h"
#include "third_party/json/json.hpp"
#include "third_party/miniz/miniz.h"
#include <cassert>
#include <filesystem>
#include <stdexcept>

using nlohmann::json;
bool ContentBuildHash::Valid(std::string const &hash) {
	return hash.size() == 64 &&
		   hash.find_first_not_of("0123456789abcdef") == std::string::npos;
}
namespace {
void Save(std::filesystem::path const &path, json const &manifest) {
	mz_zip_archive zip{};
	assert(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
	auto body = manifest.dump();
	assert(mz_zip_writer_add_mem(&zip, "manifest.json", body.data(),
								 body.size(), MZ_BEST_COMPRESSION));
	assert(mz_zip_writer_finalize_archive(&zip));
	mz_zip_writer_end(&zip);
}
} // namespace
int main() {
	auto hash =
		ContentManagedServer::DescriptorFingerprint("creature-template.id");
	auto policy = ContentResourceAllocator::CreatureTemplateIdPolicy();
	std::vector<ResourceAllocationRequest> requests = {
		{"package-a", "wolf", "creature-template.id"}};
	auto first = ContentResourceAllocator::Plan(
		"Realm", policy, requests, {}, std::set<std::uint32_t>{1, 2}, 1, hash);
	assert(first.size() == 1 && first[0].value == 3 &&
		   first[0].packageKey == "package-a");
	auto stable =
		ContentResourceAllocator::Plan("Realm", policy, requests, first,
									   std::set<std::uint32_t>{1, 2}, 2, hash);
	assert(stable[0].value == 3 && stable[0].firstBuild == 1 &&
		   stable[0].lastBuild == 2);
	auto next = ContentResourceAllocator::Plan(
		"Realm", policy, {{"package-a", "bear", "creature-template.id"}}, first,
		std::set<std::uint32_t>{1, 2}, 2, hash);
	assert(next[0].value ==
		   4); // retained-but-undesired lease 3 is not recycled
	bool rejected = false;
	try {
		ContentResourceAllocator::Plan("Realm", policy,
									   {requests[0], requests[0]}, first,
									   {1, 2}, 2, hash);
	} catch (std::runtime_error const &) {
		rejected = true;
	}
	assert(rejected);
	rejected = false;
	try {
		ContentResourceAllocator::Plan("Realm", policy, requests, first,
									   {1, 2, 3}, 2, hash);
	} catch (std::runtime_error const &) {
		rejected = true;
	}
	assert(rejected);

	ResolvedCreatureTemplate creature;
	creature.packageKey = "package-a";
	creature.packageVersion = "1";
	creature.symbol = "wolf";
	creature.entry = 3;
	creature.copyFrom = 100;
	creature.name = "Managed Wolf";
	creature.minLevel = 10;
	creature.maxLevel = 10;
	creature.faction = 14;
	creature.speedWalk = 1;
	creature.speedRun = 1.14286f;
	creature.scale = 1;
	creature.baseAttackTime = 2000;
	creature.rangeAttackTime = 2000;
	creature.unitClass = 1;
	creature.type = 1;
	creature.inhabitType = 1;
	creature.regenHealth = 1;
	ResolvedGameObjectTemplate object;
	object.packageKey = "package-a";
	object.packageVersion = "1";
	object.symbol = "marker";
	object.entry = 4;
	object.copyFrom = 200;
	object.type = 5;
	object.displayId = 300;
	object.name = "Managed Marker";
	object.size = 1;
	ResolvedCreatureSpawn spawn;
	spawn.packageKey = "package-a";
	spawn.packageVersion = "1";
	spawn.symbol = "wolf-spawn";
	spawn.creatureSymbol = "wolf";
	spawn.guid = 5;
	spawn.creatureEntry = 3;
	spawn.map = 0;
	spawn.x = -1;
	spawn.y = 2;
	spawn.z = 3;
	auto managed = ContentManagedServer::Objects({creature}, {object}, {spawn});
	std::vector<ResolvedCreatureTemplate> creatures;
	std::vector<ResolvedGameObjectTemplate> objects;
	std::vector<ResolvedCreatureSpawn> spawns;
	ContentManagedServer::Parse(managed, creatures, objects, spawns);
	assert(creatures.size() == 1 && objects.size() == 1 && spawns.size() == 1);
	auto server = ContentServerBundle::ServerJson("Realm", {}, {}, {},
												  creatures, objects, spawns);
	std::vector<ResolvedServerItem> items;
	std::string error;
	assert(ContentServerBundle::ParseServer(server, "Realm", items, error,
											nullptr, nullptr, &creatures,
											&objects, &spawns));
	std::vector<ItemAllocation> leases = {
		first[0],
		{"Realm", "package-a", "marker", 4, "reserved", 1, 1,
		 ContentManagedServer::DescriptorFingerprint("gameobject-template.id"),
		 "gameobject-template.id", 1},
		{"Realm", "package-a", "wolf-spawn", 5, "reserved", 1, 1,
		 ContentManagedServer::DescriptorFingerprint("creature-spawn.guid"),
		 "creature-spawn.guid", 1}};
	std::string artifactHash(64, 'a');
	auto parity = ContentServerBundle::ParityJson(
		"Realm", 1, leases, {}, "", "", artifactHash, artifactHash, "", "", {},
		{}, "", {}, {}, creatures, objects, spawns);
	assert(ContentServerBundle::VerifyParity(
		parity, "Realm", 1, "", artifactHash, artifactHash, {}, leases, error,
		{}, {}, creatures, objects, spawns));

	auto path =
		std::filesystem::temp_directory_path() / "content-managed-server.epf";
	json manifest = {
		{"schema", 2},
		{"package", "package-a"},
		{"name", "Managed"},
		{"version", "1"},
		{"creatureTemplates", json::array({{{"symbol", "wolf"},
											{"copyFrom", 100},
											{"overrides",
											 {{"name", "Managed Wolf"},
											  {"minLevel", 10},
											  {"maxLevel", 10}}}}})},
		{"gameobjectTemplates",
		 json::array(
			 {{{"symbol", "marker"},
			   {"copyFrom", 200},
			   {"overrides", {{"name", "Managed Marker"}, {"size", 1.0}}}}})},
		{"creatureSpawns", json::array({{{"symbol", "wolf-spawn"},
										 {"creature", {{"symbol", "wolf"}}},
										 {"map", 0},
										 {"x", -1.0},
										 {"y", 2.0},
										 {"z", 3.0}}})}};
	Save(path, manifest);
	auto validation = ContentPackage(path).Validate();
	assert(validation.valid);
	assert(validation.manifest.schema == 2);
	auto invalid = manifest;
	invalid["creatureTemplates"][0]["copyFrom"] = 0;
	Save(path, invalid);
	validation = ContentPackage(path).Validate();
	assert(!validation.valid);
	invalid = manifest;
	invalid["creatureSpawns"][0]["creature"]["symbol"] = "missing";
	Save(path, invalid);
	validation = ContentPackage(path).Validate();
	assert(!validation.valid);
	auto vendorManifest = manifest;
	vendorManifest["dbcRows"] =
		json::array({{{"op", "add"},
					  {"table", "Item"},
					  {"symbol", "token"},
					  {"fields",
					   {{"ClassID", 15},
						{"SubclassID", 0},
						{"SoundOverrideSubclassID", -1},
						{"Material", -1},
						{"DisplayInfoID", {{"copyFromItem", 1}}},
						{"InventoryType", 0},
						{"SheatheType", 0}}}}});
	vendorManifest["serverRows"] = json::array({{{"table", "item_template"},
												 {"op", "upsert"},
												 {"symbol", "token"},
												 {"fields",
												  {{"name", "Token"},
												   {"description", ""},
												   {"Quality", 1},
												   {"stackable", 20},
												   {"bonding", 0},
												   {"BagFamily", 0}}}}});
	vendorManifest["extendedCosts"] = json::array(
		{{{"symbol", "token-cost"},
		  {"requirements",
		   json::array({{{"item", {{"symbol", "token"}}}, {"count", 1}}})}}});
	vendorManifest["vendorRows"] =
		json::array({{{"symbol", "token-vendor"},
					  {"creature", {{"symbol", "wolf"}}},
					  {"itemEntry", 25},
					  {"extendedCost", {{"symbol", "token-cost"}}}}});
	Save(path, vendorManifest);
	validation = ContentPackage(path).Validate();
	assert(validation.valid);
	assert(validation.manifest.vendorRows.size() == 1);
	assert(validation.manifest.vendorRows[0].creatureSymbol == "wolf");
	assert(validation.manifest.vendorRows[0].creatureEntry == 0);
	std::filesystem::remove(path);
}
