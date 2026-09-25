#ifndef CONTENT_PACKAGE_H
#define CONTENT_PACKAGE_H

#include "ContentClientRequirement.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct ContentPackageEntry {
    std::string type;
    std::string source;
    std::string target;
};

struct ContentItemRow {
    std::string symbol;
    std::uint32_t classID = 0;
    std::uint32_t subclassID = 0;
    std::int32_t soundOverrideSubclassID = -1;
    std::int32_t material = -1;
    std::uint32_t displayCopyFromItem = 0;
    std::uint32_t inventoryType = 0;
    std::uint32_t sheatheType = 0;
	bool operator==(ContentItemRow const &other) const {
		return symbol == other.symbol && classID == other.classID &&
			   subclassID == other.subclassID &&
			   soundOverrideSubclassID == other.soundOverrideSubclassID &&
			   material == other.material &&
			   displayCopyFromItem == other.displayCopyFromItem &&
			   inventoryType == other.inventoryType &&
			   sheatheType == other.sheatheType;
    }
};

struct ContentServerItemRow {
    std::string symbol;
    std::string name;
    std::string description;
    std::uint8_t quality = 0;
    std::int32_t stackable = 1;
    std::uint8_t bonding = 0;
    std::int32_t bagFamily = 0;
	bool operator==(ContentServerItemRow const &other) const {
		return symbol == other.symbol && name == other.name &&
			   description == other.description && quality == other.quality &&
			   stackable == other.stackable && bonding == other.bonding &&
			   bagFamily == other.bagFamily;
    }
};

struct ContentCurrencyCategory {
    std::string symbol;
    std::map<std::string, std::string> names;
	bool operator==(ContentCurrencyCategory const &other) const {
		return symbol == other.symbol && names == other.names;
	}
};

struct ContentCurrencyRow {
    std::string symbol;
    std::string itemSymbol;
    std::uint32_t categoryCopyFromItem = 0;
    std::string categorySymbol;
	bool operator==(ContentCurrencyRow const &other) const {
		return symbol == other.symbol && itemSymbol == other.itemSymbol &&
			   categoryCopyFromItem == other.categoryCopyFromItem &&
			   categorySymbol == other.categorySymbol;
    }
};

struct ContentCostRequirement {
    std::string packageKey, symbol;
    std::uint32_t count = 0;
	bool operator==(ContentCostRequirement const &other) const {
		return packageKey == other.packageKey && symbol == other.symbol &&
			   count == other.count;
	}
};

struct ContentExtendedCost {
    std::string symbol;
    std::vector<ContentCostRequirement> requirements;
	std::uint32_t honorPoints = 0, arenaPoints = 0, arenaBracket = 0,
				  requiredArenaRating = 0;
	bool operator==(ContentExtendedCost const &other) const {
		return symbol == other.symbol && requirements == other.requirements &&
			   honorPoints == other.honorPoints &&
			   arenaPoints == other.arenaPoints &&
			   arenaBracket == other.arenaBracket &&
			   requiredArenaRating == other.requiredArenaRating;
	}
};

// Bounded vendor relationship: one owned merchandise row per existing creature.
struct ContentVendorRow {
    std::string symbol, extendedCostSymbol;
	std::string creatureSymbol;
    std::uint32_t creatureEntry = 0, itemEntry = 0;
	bool operator==(ContentVendorRow const &o) const {
		return symbol == o.symbol &&
			   extendedCostSymbol == o.extendedCostSymbol &&
			   creatureSymbol == o.creatureSymbol &&
			   creatureEntry == o.creatureEntry && itemEntry == o.itemEntry;
	}
};

struct ContentCreatureTemplate {
	std::string symbol;
	std::uint32_t copyFrom = 0;
	std::optional<std::string> name, subname, aiName, scriptName;
	std::optional<std::uint32_t> minLevel, maxLevel, faction, npcFlags,
		unitFlags, typeFlags;
	bool operator==(ContentCreatureTemplate const &o) const {
		return symbol == o.symbol && copyFrom == o.copyFrom && name == o.name &&
			   subname == o.subname && aiName == o.aiName &&
			   scriptName == o.scriptName && minLevel == o.minLevel &&
			   maxLevel == o.maxLevel && faction == o.faction &&
			   npcFlags == o.npcFlags && unitFlags == o.unitFlags &&
			   typeFlags == o.typeFlags;
	}
};

struct ContentGameObjectTemplate {
	std::string symbol;
	std::uint32_t copyFrom = 0;
	std::optional<std::string> name, aiName, scriptName;
	std::optional<std::uint32_t> type, displayId;
	std::optional<float> size;
	bool operator==(ContentGameObjectTemplate const &o) const {
		return symbol == o.symbol && copyFrom == o.copyFrom && name == o.name &&
			   aiName == o.aiName && scriptName == o.scriptName &&
			   type == o.type && displayId == o.displayId && size == o.size;
	}
};

struct ContentCreatureSpawn {
	std::string symbol, creatureSymbol;
	std::uint32_t map = 0, spawnMask = 1, phaseMask = 1, respawnSeconds = 300,
				  movementType = 0;
	float x = 0, y = 0, z = 0, orientation = 0, wanderDistance = 0;
	bool operator==(ContentCreatureSpawn const &o) const {
		return symbol == o.symbol && creatureSymbol == o.creatureSymbol &&
			   map == o.map && spawnMask == o.spawnMask &&
			   phaseMask == o.phaseMask && respawnSeconds == o.respawnSeconds &&
			   movementType == o.movementType && x == o.x && y == o.y &&
			   z == o.z && orientation == o.orientation &&
			   wanderDistance == o.wanderDistance;
	}
};

struct ContentPackageManifest {
    uint32_t schema = 0;
    std::string packageKey;
    std::string name;
    std::string version;
    std::string description;
	// Schema 3: sorted, deduplicated immutable client requirements (e.g.
	// protected-framexml).
    std::vector<std::string> clientRequirements;

    std::vector<ContentPackageEntry> content;
    std::vector<ContentItemRow> itemRows;
    std::vector<ContentServerItemRow> serverItemRows;
    std::vector<ContentCurrencyRow> currencyRows;
    std::vector<ContentCurrencyCategory> currencyCategories;
    std::vector<ContentExtendedCost> extendedCosts;
    std::vector<ContentVendorRow> vendorRows;
	std::vector<ContentCreatureTemplate> creatureTemplates;
	std::vector<ContentGameObjectTemplate> gameObjectTemplates;
	std::vector<ContentCreatureSpawn> creatureSpawns;
};

struct ContentPackageStageResult {
    bool success = false;
    std::string error;
    std::filesystem::path stagingDirectory;
    std::vector<std::filesystem::path> stagedFiles;
};

struct ContentPackageValidationResult {
    bool valid = false;
    std::string error;
    ContentPackageManifest manifest;
};

class ContentPackage {
public:
    explicit ContentPackage(std::filesystem::path path);

    ContentPackageValidationResult Validate() const;

	ContentPackageStageResult
	Stage(std::filesystem::path const &workDirectory) const;

    // Append only declared files to an existing owned workspace. Never clears
	// directories or overwrites files; revalidates against the preflight
	// manifest.
	ContentPackageStageResult
	StageInto(std::filesystem::path const &stagingDirectory,
        ContentPackageManifest const& expected) const;

private:
    std::filesystem::path _path;
};

#endif
