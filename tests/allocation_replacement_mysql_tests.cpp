// Generic retained-allocation replacement against disposable MySQL. This uses
// production planner and registry code, including the guarded build transaction.
#include "ContentAllocationRegistry.h"
#include "ContentResourceAllocator.h"
#include "DatabaseEnv.h"

#include <cassert>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
std::string const Realm = "replacement-test";
std::string const Hash(64, 'a');
std::string const Kind = "worldmap.dungeon-map.id";

ContentBuildRecord Build(std::uint32_t number, std::string const& filename,
    char digest)
{
    return {number, Realm, filename, 1, 1, "STAGED",
        std::string(64, digest), {}};
}

ContentServerBuildRecord Server(std::string const& filename, char digest)
{
    return {filename + ".server.json", std::string(64, digest),
        filename + ".parity.json", std::string(64, digest)};
}

void Seed(std::string const& packageKey, std::string const& symbol,
    std::uint32_t value, std::uint32_t firstBuild)
{
    assert(WorldDatabase.Execute(
        "INSERT INTO content_manager_allocation "
        "(realm_name,package_key,symbol,resource_kind,allocated_value,state,"
        "first_build,last_build,baseline_sha256,descriptor_version,policy_version) "
        "VALUES ('" + Realm + "','" + packageKey + "','" + symbol + "','" +
        Kind + "'," + std::to_string(value) + ",'reserved'," +
        std::to_string(firstBuild) + "," + std::to_string(firstBuild) + ",'" +
        Hash + "',1,1)"));
}

std::vector<ItemAllocation> Read()
{
    std::vector<ItemAllocation> rows;
    std::string error;
    assert(ContentAllocationRegistry().Read(Realm, rows, error));
    return rows;
}

ItemAllocation const* Find(std::vector<ItemAllocation> const& rows,
    std::string const& packageKey, std::uint32_t value)
{
    for (auto const& row : rows)
        if (row.packageKey == packageKey && row.value == value)
            return &row;
    return nullptr;
}
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    WorldDatabase.Connect(argv[1]);
    CharacterDatabase.Connect(argv[1]);

    // The fixture is private and fresh. These are the empty core occupancy
    // tables CommitComposed intentionally requires even for a world-map lease.
    assert(WorldDatabase.Execute("CREATE TABLE IF NOT EXISTS item_template(entry INT UNSIGNED PRIMARY KEY) ENGINE=InnoDB"));
    assert(WorldDatabase.Execute("CREATE TABLE IF NOT EXISTS npc_vendor(item INT) ENGINE=InnoDB"));
    assert(WorldDatabase.Execute("CREATE TABLE IF NOT EXISTS playercreateinfo_item(itemid INT UNSIGNED) ENGINE=InnoDB"));
    assert(WorldDatabase.Execute("CREATE TABLE IF NOT EXISTS creature_equip_template(ItemID1 INT UNSIGNED,ItemID2 INT UNSIGNED,ItemID3 INT UNSIGNED) ENGINE=InnoDB"));
    assert(WorldDatabase.Execute("CREATE TABLE IF NOT EXISTS item_instance(itemEntry INT UNSIGNED) ENGINE=InnoDB"));

    ContentResourceAllocator allocator;
    ContentAllocationRegistry registry;
    auto const policy = ContentResourceAllocator::FixedRowIdPolicy(Kind);
    AllocationReplacements replaces{{"package-b", {"package-a"}}};

    // A retained package-A Karazhan ID cannot be claimed by B without explicit
    // metadata, then migrates with value and first-build provenance intact.
    Seed("package-a", "worldmap/0/area/799/floor/383", 383, 12);
    bool refused = false;
    try {
        (void)allocator.PlanFixed(Realm, policy,
            {{"package-b", "worldmap/1/area/799/floor/383", Kind, 383}},
            Read(), {}, 700, Hash);
    } catch (std::exception const&) { refused = true; }
    assert(refused);
    auto plan = allocator.PlanFixed(Realm, policy,
        {{"package-b", "worldmap/1/area/799/floor/383", Kind, 383}},
        Read(), {}, 700, Hash, {}, replaces);
    std::string error;
    assert(registry.CommitComposed(Build(700, "migration.mpq", 'b'), plan,
        Server("migration.mpq", 'c'), error, {}, {}, replaces));
    auto after = Read();
    auto migrated = Find(after, "package-b", 383);
    assert(migrated && migrated->firstBuild == 12 && migrated->lastBuild == 700);
    assert(!Find(after, "package-a", 383));

    // A later process/rebuild uses only B's persisted identity and no special
    // replacement input.
    auto rebuild = allocator.PlanFixed(Realm, policy,
        {{"package-b", "worldmap/1/area/799/floor/383", Kind, 383}},
        Read(), {}, 701, Hash);
    assert(registry.CommitComposed(Build(701, "rebuild.mpq", 'd'), rebuild,
        Server("rebuild.mpq", 'e'), error));

    // Inject a later transaction failure with a duplicate build number. The
    // preceding migration UPDATE must roll back, leaving A's second lease intact.
    Seed("package-a", "worldmap/0/area/799/floor/384", 384, 13);
    auto rollbackPlan = allocator.PlanFixed(Realm, policy,
        {{"package-b", "worldmap/1/area/799/floor/384", Kind, 384}},
        Read(), {}, 701, Hash, {}, replaces);
    assert(!registry.CommitComposed(Build(701, "must-rollback.mpq", 'f'),
        rollbackPlan, Server("must-rollback.mpq", 'f'), error, {}, {}, replaces));
    auto rolledBack = Read();
    auto original = Find(rolledBack, "package-a", 384);
    assert(original && original->firstBuild == 13 && original->lastBuild == 13);
    assert(!Find(rolledBack, "package-b", 384));

    std::cout << "PASS allocation replacement commit, persistence and rollback\n";
    return 0;
}
