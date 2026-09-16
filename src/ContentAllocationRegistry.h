#ifndef CONTENT_ALLOCATION_REGISTRY_H
#define CONTENT_ALLOCATION_REGISTRY_H
#include "ContentBuildRegistry.h"
#include <cstdint>
#include <set>
#include <string>
#include <vector>

struct ItemAllocation
{
    std::string realm;
    std::string packageKey;
    std::string symbol;
    std::uint32_t value = 0;
    std::string state;
    std::uint32_t firstBuild = 0;
    std::uint32_t lastBuild = 0;
    std::string baselineSha256;
    std::string resourceKind = "item.id";
    std::uint32_t policyVersion = 1;
};

class ContentAllocationRegistry
{
public:
    bool Read(std::string const& realm, std::vector<ItemAllocation>& rows, std::string& error) const;
    bool OccupiedWorldItems(std::set<std::uint32_t>& entries, std::string& error) const;
    bool CommitComposed(ContentBuildRecord const& build, std::vector<ItemAllocation> const& plan,
        std::string& error) const;
};
#endif
