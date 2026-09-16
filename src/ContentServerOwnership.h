#ifndef CONTENT_SERVER_OWNERSHIP_H
#define CONTENT_SERVER_OWNERSHIP_H

#include "ContentAllocationRegistry.h"
#include <cstdint>
#include <set>
#include <string>
#include <vector>

struct ContentItemOwner
{
    std::string realm;
    std::string packageKey;
    std::string symbol;
    std::string resourceKind;
    std::uint32_t appliedBuild = 0;
    std::string artifactSha256;
    std::string rowJson;
};

enum class ContentItemOwnershipAction { Insert, Converge, Conflict };

class ContentServerOwnership
{
public:
    static bool ReadOwner(std::uint32_t entry, bool& exists, ContentItemOwner& owner, std::string& error);
    static bool ReadCurrentRow(std::uint32_t entry, bool& exists, std::string& rowJson, std::string& error);
    static bool ExcludeOwned(std::string const& realm, std::vector<ItemAllocation> const& retained,
        std::set<std::uint32_t>& occupied, std::string& error);
    static ContentItemOwnershipAction Classify(bool itemExists, bool ownerExists,
        ContentItemOwner const& owner, std::string const& realm, std::string const& packageKey,
        std::string const& symbol, std::string const& currentRowJson)
    {
        if (!itemExists && !ownerExists) return ContentItemOwnershipAction::Insert;
        if (itemExists && ownerExists && owner.realm == realm && owner.packageKey == packageKey
            && owner.symbol == symbol && owner.resourceKind == "item.id"
            && owner.rowJson == currentRowJson)
            return ContentItemOwnershipAction::Converge;
        return ContentItemOwnershipAction::Conflict;
    }
};
#endif
