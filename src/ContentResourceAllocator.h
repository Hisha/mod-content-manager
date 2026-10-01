#ifndef CONTENT_RESOURCE_ALLOCATOR_H
#define CONTENT_RESOURCE_ALLOCATOR_H
#include "ContentAllocationRegistry.h"
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

struct ResourceAllocationRequest
{
    std::string packageKey;
    std::string symbol;
    std::string resourceKind = "item.id";
    // Author-declared identity. Only honoured by PlanFixed; the searching
    // planners always ignore it and allocate the next free candidate.
    std::uint32_t fixedValue = 0;
};

// Each future resource supplies its own valid space and occupancy sources.
// item.id and currency.known-bit have independent policies and retained leases.
struct ResourceAllocationPolicy
{
    std::string resourceKind;
    std::uint32_t firstCandidate = 0;
    std::uint32_t lastCandidate = 0;
    std::uint32_t version = 1;
};

// Optional caller-supplied equivalence over author-declared symbols. It reports
// whether two symbols of the same resource kind address one authored row, so a
// release that inserts or reorders manifest entries can still recognise the row
// IDs it already owns. The caller owns the symbol grammar, so the rule can never
// be widened to a resource family that caller does not compose. The retained
// lease is reused verbatim: owner, symbol, row ID and first build are never
// rewritten, so only last_build advances.
using SymbolEquivalence =
    std::function<bool(std::string const&, std::string const&)>;

class ContentResourceAllocator
{
public:
    // Positive signed-32-bit identities; policy v1 limits NEW IDs to 65535 to
    // bound index growth. This is an operational ceiling, not a protocol limit.
    // Native DBC/overlay IDs are int32, but AC persists refundable paidExtendedCost
    // as uint16. Use the positive intersection, not an administrator-selected pool.
    static ResourceAllocationPolicy ItemExtendedCostIdPolicy() { return {"item-extended-cost.id", 1, 65535}; }
    static ResourceAllocationPolicy CurrencyCategoryIdPolicy() { return {"currency-category.id", 1, 65535}; }
    static ResourceAllocationPolicy CurrencyKnownBitPolicy() { return {"currency.known-bit", 1, 64}; }
    static ResourceAllocationPolicy CreatureTemplateIdPolicy() { return {"creature-template.id", 1, 0x00ffffff}; }
    static ResourceAllocationPolicy GameObjectTemplateIdPolicy() { return {"gameobject-template.id", 1, 0x00ffffff}; }
    static ResourceAllocationPolicy CreatureSpawnGuidPolicy() { return {"creature-spawn.guid", 1, 0xffffffff}; }
    static ResourceAllocationPolicy ItemIdPolicy(std::set<std::uint32_t> const& baselineIDs);
    // Spell.dbc and SpellMgr each allocate a dense pointer index. Policy v1
    // budgets 64 MiB for both indexes together on a 64-bit process.
    static ResourceAllocationPolicy SpellIdPolicy(std::set<std::uint32_t> const& baselineIDs);
    // A client map row identity is baked into the stock client, so a package
    // declares the exact ID it owns instead of searching for a free one. The
    // whole 1..0xffffffff space is legal; no allocator pool is imposed.
    static ResourceAllocationPolicy FixedRowIdPolicy(std::string const& resourceKind)
    { return {std::move(resourceKind), 1, 0xffffffffu}; }
    static std::vector<ItemAllocation> Plan(std::string const& realm,
        ResourceAllocationPolicy const& policy,
        std::vector<ResourceAllocationRequest> const& requests,
        std::vector<ItemAllocation> const& retained,
        std::set<std::uint32_t> const& occupiedExternal,
        std::uint32_t build, std::string const& baselineSha256,
        std::set<std::string> const& acceptedHistory = {},
        AllocationReplacements const& replacements = {});
    // Reserves the exact identity each request declares. Retained and retired
    // leases, the verified stock baseline and this build's own reservations all
    // stay occupied, so ownership is keyed deterministically by resource kind
    // (the DBC table) plus the row ID.
    static std::vector<ItemAllocation> PlanFixed(std::string const& realm,
        ResourceAllocationPolicy const& policy,
        std::vector<ResourceAllocationRequest> const& requests,
        std::vector<ItemAllocation> const& retained,
        std::set<std::uint32_t> const& occupiedExternal,
        std::uint32_t build, std::string const& baselineSha256,
        std::set<std::string> const& acceptedHistory = {},
        AllocationReplacements const& replacements = {},
        SymbolEquivalence const& sameRow = nullptr);
};
#endif
