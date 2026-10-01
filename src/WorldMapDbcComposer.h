#ifndef CONTENT_WORLD_MAP_DBC_COMPOSER_H
#define CONTENT_WORLD_MAP_DBC_COMPOSER_H
#include "ContentPackage.h"
#include "ContentResourceAllocator.h"
#include "DbcReader.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// One package's world-map contribution, already validated by
// ContentPackage::Validate().
struct ResolvedWorldMap
{
    std::string packageKey;
    ContentWorldMap declaration;
};

// Client-only native map composition for the build-12340 world-map tables.
//
// Every contributed row keeps its author-declared, client-baked identity, so
// composition is append-only: the verified stock rows and the stock string
// block are preserved byte for byte and package rows follow in a deterministic
// package/manifest order. Nothing here edits or reorders a stock record.
class WorldMapDbcComposer
{
public:
    // Durable ownership kind for `table`. The allocation registry is keyed by
    // this kind plus the row ID, so the mapping lives with the composer. It is
    // header-inline because the parity writer and the allocation whitelist need
    // the same answer without taking a link dependency on the composer.
    static std::string const& ResourceKind(std::string const& table)
    {
        static std::map<std::string, std::string> const kinds = {
            {"DungeonMap", "worldmap.dungeon-map.id"},
            {"DungeonMapChunk", "worldmap.dungeon-map-chunk.id"},
            {"WorldMapArea", "worldmap.world-map-area.id"},
            {"WorldMapTransforms", "worldmap.world-map-transforms.id"}};
        auto found = kinds.find(table);
        if (found == kinds.end())
            throw std::runtime_error("Unsupported world-map DBC table: " + table);
        return found->second;
    }
    // SHA-256 of the verified stock 3.3.5a build-12340 DBC files. The build
    // refuses any other baseline, so a stock row can never be edited in place.
    static std::string const& VerifiedBaselineSha256(std::string const& table);
    // Appends one fixed-ID lease request per authored row of every world map in
    // `maps`, keyed by table, in declaration order. This is the single source of
    // the request set the build service plans, so the planner and its tests
    // cannot drift apart. A row is requested exactly once: a DungeonMap row is
    // owned by the area that declares the floor, so its symbol carries the area
    // and there is never a second, area-less alias competing for the same ID.
    // A world map that declares no transform requests no WorldMapTransforms row
    // at all, and an area's dungeonMapId is a reference that never requests the
    // DungeonMap row it names.
    //
    // The symbol is durable ownership, so it is built only from immutable facts
    // about the authored row -- the declaring area's row ID and the row's own
    // ID. The position of the map inside worldMaps[] never appears in it. That
    // makes identity independent of manifest order by construction: inserting,
    // removing or reordering entries cannot change the identity of any row they
    // do not themselves change. See the canonical grammar below.
    static void AppendRequests(std::string const& packageKey,
        std::vector<ContentWorldMap> const& maps,
        std::map<std::string, std::vector<ResourceAllocationRequest>>& out);
    // The canonical, position-free identity of one authored row.
    //
    //   WorldMapArea        worldmap/area/<areaId>
    //   DungeonMap          worldmap/area/<areaId>/floor/<floorId>
    //   DungeonMapChunk     worldmap/area/<areaId>/chunk/<chunkId>
    //   WorldMapTransforms  worldmap/transform/<transformId>
    //
    // Every component is an immutable property of the row: the area that owns a
    // floor or chunk, and the row's own client-baked ID. No worldMaps[] ordinal
    // participates. A transform is keyed by its own declared transform ID, which
    // distinguishes two genuinely different transform rows even when they belong
    // to the same map.
    static std::string CanonicalSymbol(std::string const& table, std::uint32_t areaId,
        std::uint32_t rowId);
    // Reports whether a retained symbol and a requested symbol of the same kind
    // name one authored row, so the planner can reuse a row ID a package already
    // owns after its release grows or reorders.
    //
    // Both the canonical form and the historical positional form
    // ("worldmap/<manifest position>/...") are understood, which is how leases
    // persisted before the canonical grammar keep working. The historical form
    // is accepted for matching only: no new symbol is ever minted from it.
    //
    // A historical transform symbol carries no row identity at all -- every
    // transform in a package shares the row path "transform" -- so the retained
    // row ID is the only fact that can identify it. That is why both row IDs are
    // arguments. It is also why a bare "transform" equivalence would be unsafe:
    // without them a retained transform could be adopted by a different
    // transform's declaration.
    static bool SameRow(std::string const& retainedSymbol, std::uint32_t retainedValue,
        std::string const& requestSymbol, std::uint32_t requestValue);
    // Validates the stock baseline dimensions for `table` and returns its row IDs.
    static std::set<std::uint32_t> Inspect(std::string const& table, DbcDocument const& baseline);
    // Contributed row IDs for `table`, in deterministic composition order.
    static std::vector<std::uint32_t> Rows(std::string const& table, std::vector<ResolvedWorldMap> const& maps);
    static std::vector<std::uint32_t> FloorWords(std::uint32_t mapId, ContentDungeonMapFloor const& row);
    static std::vector<std::uint32_t> ChunkWords(std::uint32_t mapId, ContentDungeonMapChunk const& row);
    static std::vector<std::uint32_t> AreaWords(std::uint32_t mapId, ContentWorldMapArea const& row, std::uint32_t internalNameOffset);
    static std::vector<std::uint32_t> TransformWords(std::uint32_t mapId, ContentWorldMapTransform const& row);
    // Appends the declared rows after the untouched stock rows.
    static std::vector<std::uint8_t> Compose(std::string const& table, DbcDocument const& baseline,
        std::vector<ResolvedWorldMap> const& maps);
    // Writes composed bytes to <workspace>/DBFilesClient/<table>.dbc and reads
    // them back. Shared by the cumulative build and the acceptance tests so the
    // staged artifact is verified exactly once, in one place.
    static bool Stage(std::string const& table, std::vector<std::uint8_t> const& bytes,
        std::filesystem::path const& workspace, DbcDocument& reparsed, std::string& error);
};
#endif
