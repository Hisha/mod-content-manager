#include "ContentResourceAllocator.h"
#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>

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

std::vector<ItemAllocation> ContentResourceAllocator::Plan(std::string const& realm,
    ResourceAllocationPolicy const& policy, std::vector<ResourceAllocationRequest> const& requests,
    std::vector<ItemAllocation> const& retained, std::set<std::uint32_t> const& occupiedExternal,
    std::uint32_t build, std::string const& hash)
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
            if (lease.baselineSha256 != hash)
                throw std::runtime_error("Retained allocation was pinned to a different baseline; review migration before reuse");
            if (lease.policyVersion != policy.version)
                throw std::runtime_error("Retained allocation uses another resource policy version; review migration before reuse");
            if (!lease.value || lease.value > policy.lastCandidate || occupiedExternal.count(lease.value))
                throw std::runtime_error("Retained resource ID is invalid or now externally occupied: " + std::to_string(lease.value));
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
