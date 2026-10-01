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

struct ContentSpellRow {
    std::string symbol;
    std::uint32_t copyFrom = 0;
    std::uint32_t iconCopyFromSpell = 0;
    std::string profile;
    std::map<std::string, std::string> names;
    std::map<std::string, std::string> descriptions;
    std::map<std::string, std::string> auraDescriptions;
    bool operator==(ContentSpellRow const &o) const {
        return symbol == o.symbol && copyFrom == o.copyFrom &&
               iconCopyFromSpell == o.iconCopyFromSpell && profile == o.profile &&
               names == o.names && descriptions == o.descriptions &&
               auraDescriptions == o.auraDescriptions;
    }
};

// ---------------------------------------------------------------------------
// Native client world-map contribution (build 12340).
//
// A stock 3.3.5a dungeon map is identified by client-baked IDs. A package
// therefore declares the exact IDs it owns instead of asking the allocator for
// them; row ownership is deterministic and keyed by "DBC table + row ID". The
// worldserver never reads these tables. Only the client MPQ is composed.
//
// Fields the repository cannot prove keep neutral fieldN names; no semantics
// are invented for them.
// ---------------------------------------------------------------------------

// One DungeonMap.dbc row: a drawable floor of a dungeon map.
struct ContentDungeonMapFloor {
    std::uint32_t id = 0;    // DungeonMap.ID
    std::uint32_t floor = 0; // DungeonMap.Floor
    float field3 = 0, field4 = 0, field5 = 0, field6 = 0;
    std::uint32_t field7 = 0;
    bool operator==(ContentDungeonMapFloor const &o) const {
        return id == o.id && floor == o.floor && field3 == o.field3 &&
               field4 == o.field4 && field5 == o.field5 && field6 == o.field6 &&
               field7 == o.field7;
    }
};

// One DungeonMapChunk.dbc row. DungeonMapChunk.ID is unrelated to the WDL tile
// numbering and is never assumed to map one-to-one onto BLP artwork.
struct ContentDungeonMapChunk {
    std::uint32_t id = 0;           // DungeonMapChunk.ID
    std::uint32_t field2 = 0;       // DungeonMapChunk.field2
    std::uint32_t dungeonMapId = 0; // DungeonMapChunk.DungeonMapID
    float field4 = 0;               // DungeonMapChunk.field4
    bool operator==(ContentDungeonMapChunk const &o) const {
        return id == o.id && field2 == o.field2 &&
               dungeonMapId == o.dungeonMapId && field4 == o.field4;
    }
};

// Locale-aware dungeon level labels declared for one WorldMapArea.
//
// A key is the exact dropdown row the stock
// WorldMapLevelDropDown_Initialize loop enumerates, so a label can only ever
// land on that row. Stock terrain-map floorNum adjustment is retained for any
// missing custom label. Labels are keyed by locale
// because the client selects them with GetLocale(); a locale the build-12340
// client cannot return is refused at parse time.
struct ContentWorldMapFloorNames {
    std::map<std::uint32_t, std::string> labels;
    bool operator==(ContentWorldMapFloorNames const &o) const {
        return labels == o.labels;
    }
};

// One additive load-list contribution to the stock build-12340 FrameXML.toc.
// Both paths are canonical MPQ targets below Interface/FrameXML. `target` is
// emitted immediately after `after` (or after earlier deterministic siblings)
// without giving the package ownership of FrameXML.toc itself.
struct ContentFrameXmlLoadEntry {
    std::string target;
    std::string after;
    bool operator==(ContentFrameXmlLoadEntry const &o) const {
        return target == o.target && after == o.after;
    }
};

// The verified stock FrameXML.toc this package's floor labels will be inserted
// into. The package carries the bytes because only it knows which client build
// its content was authored against; Content Manager refuses any digest other
// than the pinned build-12340 one. loadEntries are additive and may anchor to a
// stock entry or another managed contribution.
struct ContentClientFrameXml {
    std::string stockTocSource; // member path inside the package EPF
    std::string stockTocSha256; // pinned digest of those exact bytes
    std::vector<ContentFrameXmlLoadEntry> loadEntries;
    bool operator==(ContentClientFrameXml const &o) const {
        return stockTocSource == o.stockTocSource &&
               stockTocSha256 == o.stockTocSha256 &&
               loadEntries == o.loadEntries;
    }
};

// One WorldMapArea.dbc row plus the floors and chunks of that area.
//
// `dungeonMapId` is a reference field, not an owned row: naming an ID here never
// leases or composes that DungeonMap row. Only `floors` below is owned. Stock
// 3.3.5a already carries 0 and -1 here, and WDM Stable additionally points one
// area at a DungeonMap row owned by a different map, so no same-map resolution
// is required of this field.
//
// `floorNames` is client-only and composes no DBC row: it describes how this
// area's levels are labelled in the world-map level dropdown.
struct ContentWorldMapArea {
    std::uint32_t id = 0;     // WorldMapArea.ID
    std::uint32_t areaId = 0; // WorldMapArea.area_id
    std::string internalName; // WorldMapArea.internal_name
    float y1 = 0, y2 = 0, x1 = 0, x2 = 0;
    std::int32_t virtualMapId = -1;
    std::int32_t dungeonMapId = 0;
    std::uint32_t parentMapId = 0;
    std::vector<ContentDungeonMapFloor> floors;
    std::vector<ContentDungeonMapChunk> chunks;
    std::map<std::string, ContentWorldMapFloorNames> floorNames;
    bool operator==(ContentWorldMapArea const &o) const {
        return id == o.id && areaId == o.areaId &&
               internalName == o.internalName && y1 == o.y1 && y2 == o.y2 &&
               x1 == o.x1 && x2 == o.x2 && virtualMapId == o.virtualMapId &&
               dungeonMapId == o.dungeonMapId && parentMapId == o.parentMapId &&
               floors == o.floors && chunks == o.chunks &&
               floorNames == o.floorNames;
    }
};

// One WorldMapTransforms.dbc row.
struct ContentWorldMapTransform {
    std::uint32_t id = 0; // WorldMapTransforms.ID
    float regionBottom = 0, regionRight = 0, regionTop = 0, regionLeft = 0;
    std::uint32_t newMapId = 0;
    float regionOffsetX = 0, regionOffsetY = 0;
    std::uint32_t newDungeonMapId = 0;
    bool operator==(ContentWorldMapTransform const &o) const {
        return id == o.id && regionBottom == o.regionBottom &&
               regionRight == o.regionRight && regionTop == o.regionTop &&
               regionLeft == o.regionLeft && newMapId == o.newMapId &&
               regionOffsetX == o.regionOffsetX &&
               regionOffsetY == o.regionOffsetY &&
               newDungeonMapId == o.newDungeonMapId;
    }
};

// One MapID contribution. `mapId` is written to every owned row's MapID/map_id.
//
// `transform` is optional. A native dungeon map is described by its
// WorldMapArea, DungeonMap floors, DungeonMapChunk rows and artwork alone, and
// stock 3.3.5a and WDM Stable both carry such maps with no WorldMapTransforms
// row at all (Karazhan, map 532, has seventeen floors and no transform). So an
// absent transform is a valid contribution, not a missing declaration: no row is
// composed, no ID is requested and no lease is created. When a transform is
// declared it is preserved exactly as authored. None is ever synthesised, and
// NewDungeonMapID is never guessed.
struct ContentWorldMap {
    std::uint32_t mapId = 0;
    std::optional<ContentWorldMapTransform> transform;
    std::vector<ContentWorldMapArea> areas;
    bool operator==(ContentWorldMap const &o) const {
        return mapId == o.mapId && transform == o.transform && areas == o.areas;
    }
};

struct ContentPackageManifest {
    uint32_t schema = 0;
    std::string packageKey;
    std::string name;
    std::string version;
    std::string description;
	// Schema 3: historical package identities whose compatible retained
	// allocations this package may explicitly inherit during a build.
	std::vector<std::string> replaces;
	// Schema 3: sorted, deduplicated immutable client requirements (e.g.
	// protected-framexml).
    std::vector<std::string> clientRequirements;
    // Schema 3: the verified stock FrameXML.toc used by floor labels and/or
    // additive package load entries.
    std::optional<ContentClientFrameXml> clientFrameXml;

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
	std::vector<ContentSpellRow> spells;
	std::vector<ContentWorldMap> worldMaps;
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

	// Reads one member's exact bytes without staging it. Used to verify the
	// stock FrameXML.toc a package declares before any of its floor labels are
	// allowed to influence the build.
	static bool ReadMember(std::filesystem::path const& epf,
		std::string const& member, std::vector<std::uint8_t>& out,
		std::string& error);

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
