#ifndef CONTENT_MANAGED_SERVER_H
#define CONTENT_MANAGED_SERVER_H

#include "ContentAllocationRegistry.h"
#include "ContentPackage.h"
#include "third_party/json/json.hpp"
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

struct ResolvedCreatureTemplate {
	std::string packageKey, packageVersion, symbol;
	std::uint32_t entry = 0, copyFrom = 0, minLevel = 0, maxLevel = 0,
				  faction = 0, npcFlags = 0;
	std::uint32_t rank = 0, damageSchool = 0, baseAttackTime = 0,
				  rangeAttackTime = 0, unitClass = 0;
	std::uint32_t unitFlags = 0, type = 0, typeFlags = 0, regenHealth = 0,
				  flagsExtra = 0;
	float speedWalk = 0, speedRun = 0;
	std::string name, subname, aiName, scriptName;
};

struct ResolvedGameObjectTemplate {
	std::string packageKey, packageVersion, symbol;
	std::uint32_t entry = 0, copyFrom = 0, type = 0, displayId = 0;
	std::string name, iconName, castBarCaption, unk1, aiName, scriptName;
	float size = 0;
	std::array<std::uint32_t, 24> data{};
	std::int32_t verifiedBuild = 0;
};

struct ResolvedCreatureSpawn {
	std::string packageKey, packageVersion, symbol, creatureSymbol;
	std::uint32_t guid = 0, creatureEntry = 0, map = 0, spawnMask = 1,
				  phaseMask = 1;
	std::uint32_t respawnSeconds = 300, movementType = 0;
	float x = 0, y = 0, z = 0, orientation = 0, wanderDistance = 0;
};

class ContentManagedServer {
public:
	static constexpr std::uint32_t DescriptorVersion = 1;
	static std::string DescriptorFingerprint(std::string const &kind);
	static nlohmann::json
	Objects(std::vector<ResolvedCreatureTemplate> creatures,
			std::vector<ResolvedGameObjectTemplate> gameObjects,
			std::vector<ResolvedCreatureSpawn> spawns);
	static void Parse(nlohmann::json const &value,
					  std::vector<ResolvedCreatureTemplate> &creatures,
					  std::vector<ResolvedGameObjectTemplate> &gameObjects,
					  std::vector<ResolvedCreatureSpawn> &spawns);
	static bool ResolveDonor(ContentCreatureTemplate const &,
							 std::string const &package,
							 std::string const &version, std::uint32_t entry,
							 ResolvedCreatureTemplate &, std::string &error);
	static bool ResolveDonor(ContentGameObjectTemplate const &,
							 std::string const &package,
							 std::string const &version, std::uint32_t entry,
							 ResolvedGameObjectTemplate &, std::string &error);
	static bool Occupancy(std::string const &kind, std::string const &realm,
						  std::vector<ItemAllocation> const &retained,
						  std::set<std::uint32_t> &occupied,
						  std::string &error);
	static bool Check(ResolvedCreatureTemplate const &,
					  std::string const &realm, bool &exists,
					  std::string &error);
	static bool Check(ResolvedGameObjectTemplate const &,
					  std::string const &realm, bool &exists,
					  std::string &error);
	static bool Check(ResolvedCreatureSpawn const &, std::string const &realm,
					  bool &exists, std::string &error);
	static std::string Condition(ResolvedCreatureTemplate const &,
								 std::string const &realm, bool exists);
	static std::string Condition(ResolvedGameObjectTemplate const &,
								 std::string const &realm, bool exists);
	static std::string Condition(ResolvedCreatureSpawn const &,
								 std::string const &realm, bool exists);
	static std::vector<std::string> ApplySql(ResolvedCreatureTemplate const &,
											 std::string const &realm,
											 bool exists, std::uint32_t build,
											 std::string const &hash);
	static std::vector<std::string> ApplySql(ResolvedGameObjectTemplate const &,
											 std::string const &realm,
											 bool exists, std::uint32_t build,
											 std::string const &hash);
	static std::vector<std::string> ApplySql(ResolvedCreatureSpawn const &,
											 std::string const &realm,
											 bool exists, std::uint32_t build,
											 std::string const &hash);
	static bool Verify(ResolvedCreatureTemplate const &,
					   std::string const &realm, std::uint32_t build,
					   std::string const &hash, std::string &error);
	static bool Verify(ResolvedGameObjectTemplate const &,
					   std::string const &realm, std::uint32_t build,
					   std::string const &hash, std::string &error);
	static bool Verify(ResolvedCreatureSpawn const &, std::string const &realm,
					   std::uint32_t build, std::string const &hash,
					   std::string &error);
};
#endif
