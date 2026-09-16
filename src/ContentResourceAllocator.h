#ifndef CONTENT_RESOURCE_ALLOCATOR_H
#define CONTENT_RESOURCE_ALLOCATOR_H
#include "ContentAllocationRegistry.h"
#include <cstdint>
#include <set>
#include <string>
#include <vector>

struct ResourceAllocationRequest
{
    std::string packageKey;
    std::string symbol;
    std::string resourceKind = "item.id";
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

class ContentResourceAllocator
{
public:
    // Positive signed-32-bit identities; policy v1 limits NEW IDs to 65535 to
    // bound index growth. This is an operational ceiling, not a protocol limit.
    static ResourceAllocationPolicy CurrencyCategoryIdPolicy() { return {"currency-category.id", 1, 65535}; }
    static ResourceAllocationPolicy CurrencyKnownBitPolicy() { return {"currency.known-bit", 1, 64}; }
    static ResourceAllocationPolicy ItemIdPolicy(std::set<std::uint32_t> const& baselineIDs);
    static std::vector<ItemAllocation> Plan(std::string const& realm,
        ResourceAllocationPolicy const& policy,
        std::vector<ResourceAllocationRequest> const& requests,
        std::vector<ItemAllocation> const& retained,
        std::set<std::uint32_t> const& occupiedExternal,
        std::uint32_t build, std::string const& baselineSha256,
        std::set<std::string> const& acceptedHistory = {});
};
#endif
