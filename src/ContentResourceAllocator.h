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
// Only the item.id policy is registered in Phase 2.
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
    static ResourceAllocationPolicy ItemIdPolicy(std::set<std::uint32_t> const& baselineIDs);
    static std::vector<ItemAllocation> Plan(std::string const& realm,
        ResourceAllocationPolicy const& policy,
        std::vector<ResourceAllocationRequest> const& requests,
        std::vector<ItemAllocation> const& retained,
        std::set<std::uint32_t> const& occupiedExternal,
        std::uint32_t build, std::string const& baselineSha256);
};
#endif
