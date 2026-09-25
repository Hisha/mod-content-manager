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

// Provable ownership records retained for one package across every managed
// server table. These are provenance counts, never live-row snapshots: the
// ownership tables are written only by an explicit .content server apply.
struct ContentOwnerSummary
{
    std::uint32_t itemTemplates = 0;
    std::uint32_t currencies = 0;
    std::uint32_t extendedCosts = 0;
    std::uint32_t vendors = 0;
    std::uint32_t managedServerResources = 0;
    std::uint32_t latestAppliedBuild = 0;
};

enum class ContentItemOwnershipAction { Insert, Converge, Conflict };

class ContentServerOwnership
{
public:
    static bool ReadOwner(std::uint32_t entry, bool& exists, ContentItemOwner& owner, std::string& error);
    static bool ReadCurrentRow(std::uint32_t entry, bool& exists, std::string& rowJson, std::string& error);
    static bool CountOwned(std::string const& realm, std::string const& packageKey,
        ContentOwnerSummary& summary, std::string& error);
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
