#include "ContentResourceAllocator.h"
#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>

namespace {
bool Replaces(AllocationReplacements const& replacements,
    std::string const& successor, std::string const& predecessor)
{
    auto found = replacements.find(successor);
    return found != replacements.end() && found->second.count(predecessor);
}

void ValidateInherited(ItemAllocation const& lease,
    ResourceAllocationPolicy const& policy, std::set<std::uint32_t> const& occupiedExternal,
    std::string const& hash, std::set<std::string> const& acceptedHistory)
{
    if (lease.baselineSha256 != hash && !acceptedHistory.count(lease.baselineSha256))
        throw std::runtime_error(
            "Replacement allocation was pinned to an incompatible baseline");
    if (lease.policyVersion != policy.version)
        throw std::runtime_error(
            "Replacement allocation uses another resource policy version");
    if (lease.value < policy.firstCandidate || lease.value > policy.lastCandidate ||
        occupiedExternal.count(lease.value))
        throw std::runtime_error(
            "Replacement resource ID is invalid or externally occupied: " +
            std::to_string(lease.value));
}
}

ResourceAllocationPolicy ContentResourceAllocator::ItemIdPolicy(std::set<std::uint32_t> const& baselineIDs)
{
    if (baselineIDs.empty())
        throw std::runtime_error("Cannot allocate item.id without validated baseline Item IDs");
    // AzerothCore DBCStorage has a dense pointer index of maxID + 1 slots.
    // Keep one Item index below a 64 MiB operational memory budget. This is a
    // resource safety bound, not an administrator-selected ID pool.
    constexpr std::uint64_t indexBudget = 64ULL * 1024 * 1024;
    auto pointerBytes = std::max<std::size_t>(sizeof(void*), 8);
    auto last = std::min<std::uint64_t>(std::numeric_limits<std::uint32_t>::max() - 1ULL,
        indexBudget / pointerBytes - 1ULL);
    auto stockMax = *baselineIDs.rbegin();
    if (stockMax >= last)
        throw std::runtime_error("Baseline Item ID exceeds safe DBC dense-index policy; review allocator budget");
    return {"item.id", static_cast<std::uint32_t>(stockMax + 1), static_cast<std::uint32_t>(last)};
}

ResourceAllocationPolicy ContentResourceAllocator::SpellIdPolicy(std::set<std::uint32_t> const& baselineIDs)
{
	if (baselineIDs.empty()) throw std::runtime_error("Cannot allocate spell.id without validated baseline Spell IDs");
	constexpr std::uint64_t budget=64ULL*1024*1024;
	auto pointerBytes=std::max<std::size_t>(sizeof(void*),8);
	auto last=std::min<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()-1ULL,budget/(2*pointerBytes)-1ULL);
	auto stockMax=*baselineIDs.rbegin();
	if(stockMax>=last)throw std::runtime_error("Baseline Spell ID exceeds safe dual dense-index policy");
	return {"spell.id",static_cast<std::uint32_t>(stockMax+1),static_cast<std::uint32_t>(last)};
}

std::vector<ItemAllocation> ContentResourceAllocator::Plan(std::string const& realm,
    ResourceAllocationPolicy const& policy, std::vector<ResourceAllocationRequest> const& requests,
    std::vector<ItemAllocation> const& retained, std::set<std::uint32_t> const& occupiedExternal,
    std::uint32_t build, std::string const& hash,
    std::set<std::string> const& acceptedHistory,
    AllocationReplacements const& replacements)
{
    if (policy.resourceKind.empty() || !policy.version || !policy.firstCandidate
        || policy.lastCandidate < policy.firstCandidate)
        throw std::runtime_error("Invalid resource allocation policy");
    std::set<std::uint32_t> occupied = occupiedExternal;
    std::map<std::tuple<std::string, std::string, std::string>, ItemAllocation> byIdentity;
    for (auto const& lease : retained)
    {
        if (lease.resourceKind != policy.resourceKind) continue;
        if (lease.realm != realm)
            throw std::runtime_error("Retained allocation scope does not match resource policy");
        if (!byIdentity.empty())
            for (auto const& prior : byIdentity)
                if (prior.second.value == lease.value)
                    throw std::runtime_error("Duplicate retained resource value");
        if (!lease.value || lease.value > policy.lastCandidate)
            throw std::runtime_error("Retained allocation outside resource bounds");
        occupied.insert(lease.value); // Removed and retired leases remain occupied.
        if (!byIdentity.emplace(std::make_tuple(lease.packageKey, lease.symbol, lease.resourceKind), lease).second)
            throw std::runtime_error("Duplicate retained resource allocation identity");
    }
    auto sorted = requests;
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) {
        return std::tie(a.packageKey, a.symbol, a.resourceKind)
            < std::tie(b.packageKey, b.symbol, b.resourceKind);
    });
    std::vector<ItemAllocation> plan;
    for (auto const& request : sorted)
    {
        if (request.resourceKind != policy.resourceKind)
            throw std::runtime_error("Request resource kind does not match allocation policy");
        if (!plan.empty() && plan.back().packageKey == request.packageKey
            && plan.back().symbol == request.symbol && plan.back().resourceKind == request.resourceKind)
            throw std::runtime_error("Duplicate resource allocation request: " + request.packageKey + "/" + request.symbol);
        auto existing = byIdentity.find({request.packageKey, request.symbol, request.resourceKind});
        if (existing != byIdentity.end())
        {
            auto lease = existing->second;
            if (lease.baselineSha256 != hash && !acceptedHistory.count(lease.baselineSha256))
                throw std::runtime_error("Retained allocation was pinned to a different baseline; review migration before reuse");
            if (lease.policyVersion != policy.version)
                throw std::runtime_error("Retained allocation uses another resource policy version; review migration before reuse");
            if (!lease.value || lease.value > policy.lastCandidate || occupiedExternal.count(lease.value))
                throw std::runtime_error("Retained resource ID is invalid or now externally occupied: " + std::to_string(lease.value));
            lease.lastBuild = build;
            plan.push_back(std::move(lease));
            continue;
        }
        // Generated resources have no authored numeric ID. A successor can
        // therefore identify exactly one historical lease only by the same
        // semantic symbol and resource kind.
        std::vector<ItemAllocation const*> inherited;
        bool wrongKind = false;
        for (auto const& lease : retained)
            if (Replaces(replacements, request.packageKey, lease.packageKey) &&
                lease.symbol == request.symbol) {
                if (lease.resourceKind == request.resourceKind)
                    inherited.push_back(&lease);
                else
                    wrongKind = true;
            }
        if (inherited.size() > 1)
            throw std::runtime_error("Ambiguous replacement allocation for " +
                request.packageKey + "/" + request.symbol + "/" + request.resourceKind);
        if (inherited.empty() && wrongKind)
            throw std::runtime_error("Replacement allocation resource kind mismatch for " +
                request.packageKey + "/" + request.symbol);
        if (!inherited.empty()) {
            auto lease = *inherited.front();
            ValidateInherited(lease, policy, occupiedExternal, hash, acceptedHistory);
            lease.packageKey = request.packageKey;
            lease.symbol = request.symbol;
            lease.lastBuild = build;
            plan.push_back(std::move(lease));
            continue;
        }
        std::uint64_t candidate = policy.firstCandidate;
        while (candidate <= policy.lastCandidate && occupied.count(static_cast<std::uint32_t>(candidate))) ++candidate;
        if (candidate > policy.lastCandidate)
            throw std::runtime_error("No unoccupied " + policy.resourceKind + " remains in safe numeric search space "
                + std::to_string(policy.firstCandidate) + ".." + std::to_string(policy.lastCandidate));
        auto value = static_cast<std::uint32_t>(candidate);
        occupied.insert(value); // Covers all allocations planned in this build.
        ItemAllocation lease{realm, request.packageKey, request.symbol, value,
            "reserved", build, build, hash};
        lease.resourceKind = request.resourceKind;
        lease.policyVersion = policy.version;
        plan.push_back(std::move(lease));
    }
    return plan;
}

std::vector<ItemAllocation> ContentResourceAllocator::PlanFixed(std::string const& realm,
    ResourceAllocationPolicy const& policy, std::vector<ResourceAllocationRequest> const& requests,
    std::vector<ItemAllocation> const& retained, std::set<std::uint32_t> const& occupiedExternal,
    std::uint32_t build, std::string const& hash,
    std::set<std::string> const& acceptedHistory,
    AllocationReplacements const& replacements)
{
    if (policy.resourceKind.empty() || !policy.version || !policy.firstCandidate
        || policy.lastCandidate < policy.firstCandidate)
        throw std::runtime_error("Invalid resource allocation policy");
    std::set<std::uint32_t> occupied = occupiedExternal;
    std::map<std::tuple<std::string, std::string, std::string>, ItemAllocation> byIdentity;
    std::map<std::pair<std::string, std::uint32_t>, std::string> byValue;
    for (auto const& lease : retained)
    {
        if (lease.resourceKind != policy.resourceKind) continue;
        if (lease.realm != realm)
            throw std::runtime_error("Retained allocation scope does not match resource policy");
        if (lease.value < policy.firstCandidate || lease.value > policy.lastCandidate)
            throw std::runtime_error("Retained allocation outside resource bounds");
        if (!byIdentity.emplace(std::make_tuple(lease.packageKey, lease.symbol, lease.resourceKind), lease).second)
            throw std::runtime_error("Duplicate retained resource allocation identity");
        // Removed and retired leases remain occupied, exactly as for allocated
        // identities: a retired row ID is never silently handed to another owner.
        if (!byValue.emplace(std::make_pair(lease.packageKey, lease.value), lease.symbol).second)
            throw std::runtime_error("Duplicate retained resource value");
        occupied.insert(lease.value);
    }
    auto sorted = requests;
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) {
        return std::tie(a.packageKey, a.symbol, a.resourceKind)
            < std::tie(b.packageKey, b.symbol, b.resourceKind);
    });
    std::vector<ItemAllocation> plan;
    for (auto const& request : sorted)
    {
        if (request.resourceKind != policy.resourceKind)
            throw std::runtime_error("Request resource kind does not match allocation policy");
        if (!plan.empty() && plan.back().packageKey == request.packageKey
            && plan.back().symbol == request.symbol && plan.back().resourceKind == request.resourceKind)
            throw std::runtime_error("Duplicate resource allocation request: " + request.packageKey + "/" + request.symbol);
        if (!request.fixedValue || request.fixedValue < policy.firstCandidate
            || request.fixedValue > policy.lastCandidate)
            throw std::runtime_error("Declared " + policy.resourceKind + " row ID is out of bounds: "
                + std::to_string(request.fixedValue));
        auto existing = byIdentity.find({request.packageKey, request.symbol, policy.resourceKind});
        if (existing != byIdentity.end())
        {
            auto lease = existing->second;
            if (lease.value != request.fixedValue)
                throw std::runtime_error("Retained " + policy.resourceKind + " row ID disagrees with the declared ID: "
                    + request.packageKey + "/" + request.symbol);
            if (lease.baselineSha256 != hash && !acceptedHistory.count(lease.baselineSha256))
                throw std::runtime_error("Retained allocation was pinned to a different baseline; review migration before reuse");
            if (lease.policyVersion != policy.version)
                throw std::runtime_error("Retained allocation uses another resource policy version; review migration before reuse");
            if (occupiedExternal.count(lease.value))
                throw std::runtime_error("Retained " + policy.resourceKind + " row ID is now externally occupied: "
                    + std::to_string(lease.value));
            lease.lastBuild = build;
            plan.push_back(std::move(lease));
            continue;
        }
        // Fixed resources carry their stable identity in the manifest. The
        // semantic path may legitimately move when packages are consolidated,
        // so inheritance matches the declared value plus resource kind.
        std::vector<ItemAllocation const*> inherited;
        bool wrongKind = false;
        for (auto const& lease : retained)
            if (Replaces(replacements, request.packageKey, lease.packageKey) &&
                lease.value == request.fixedValue) {
                if (lease.resourceKind == request.resourceKind)
                    inherited.push_back(&lease);
                else
                    wrongKind = true;
            }
        if (inherited.size() > 1)
            throw std::runtime_error("Ambiguous replacement allocation for " +
                request.packageKey + "/" + request.resourceKind + "/" +
                std::to_string(request.fixedValue));
        if (inherited.empty() && wrongKind)
            throw std::runtime_error("Replacement allocation resource kind mismatch for " +
                request.packageKey + "/" + request.resourceKind + "/" +
                std::to_string(request.fixedValue));
        if (!inherited.empty()) {
            if (std::any_of(plan.begin(), plan.end(), [&](auto const& prior) {
                    return prior.value == request.fixedValue;
                }))
                throw std::runtime_error(
                    "Multiple successor declarations claim one replacement allocation: " +
                    request.packageKey + "/" + request.resourceKind + "/" +
                    std::to_string(request.fixedValue));
            auto lease = *inherited.front();
            ValidateInherited(lease, policy, occupiedExternal, hash, acceptedHistory);
            lease.packageKey = request.packageKey;
            lease.symbol = request.symbol;
            lease.lastBuild = build;
            plan.push_back(std::move(lease));
            continue;
        }
        // Reached only by a genuinely new identity (the retained-identity case
        // returned above), so an existing byValue entry with a different symbol
        // is a request-construction fault, not a concurrent lease.
        auto owner = byValue.find({request.packageKey, request.fixedValue});
        if (owner != byValue.end() && owner->second != request.symbol)
            throw std::runtime_error("Package '" + request.packageKey + "' already owns "
                + policy.resourceKind + " row ID " + std::to_string(request.fixedValue)
                + " as '" + owner->second + "'");
        if (occupiedExternal.count(request.fixedValue))
            throw std::runtime_error(policy.resourceKind + " row ID " + std::to_string(request.fixedValue)
                + " is already present in the verified stock baseline");
        if (!occupied.insert(request.fixedValue).second)
            throw std::runtime_error(policy.resourceKind + " row ID " + std::to_string(request.fixedValue)
                + " is already owned by another package or earlier request in this build");
        byValue.emplace(std::make_pair(request.packageKey, request.fixedValue), request.symbol);
        ItemAllocation lease{realm, request.packageKey, request.symbol, request.fixedValue,
            "reserved", build, build, hash};
        lease.resourceKind = request.resourceKind;
        lease.policyVersion = policy.version;
        plan.push_back(std::move(lease));
    }
    return plan;
}
