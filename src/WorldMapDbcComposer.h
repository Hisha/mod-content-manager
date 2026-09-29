#ifndef CONTENT_WORLD_MAP_DBC_COMPOSER_H
#define CONTENT_WORLD_MAP_DBC_COMPOSER_H
#include "ContentPackage.h"
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
