#ifndef CONTENT_CLIENT_REQUIREMENT_H
#define CONTENT_CLIENT_REQUIREMENT_H

#include <cstddef>
#include <set>
#include <string>
#include <vector>

// Immutable client capability requirements attached to a generated build.
// Packages declare WHAT a build requires; downstream modules decide HOW that
// capability is provided. Content Manager only records the declared set with
// the exact build that used those package manifests.
namespace ContentClientRequirement
{
inline constexpr char const* ProtectedFrameXml = "protected-framexml";
// Storage bound: requirement names persist into a fixed-width SQL column.
inline constexpr std::size_t MaxNameLength = 64;

inline bool IsSupported(std::string const& requirement)
{
    return requirement == ProtectedFrameXml;
}

// Registry-bound check before persistence: supported names within the storage
// bound, strictly sorted, no duplicates.
inline bool ValidSet(std::vector<std::string> const& requirements)
{
    for (auto const& requirement : requirements)
        if (!IsSupported(requirement) || requirement.size() > MaxNameLength)
            return false;
    for (std::size_t i = 1; i < requirements.size(); ++i)
        if (!(requirements[i - 1] < requirements[i]))
            return false;
    return true;
}

// Deterministic union of the requirement sets declared by the exact package
// manifests participating in a build. Each manifest is already a deduplicated,
// sorted set; the combined result is sorted byte-wise, contains no duplicates,
// and does not depend on package discovery order.
inline std::vector<std::string> Merge(std::vector<std::vector<std::string>> const& packageSets)
{
    std::set<std::string> combined;
    for (auto const& set : packageSets)
        for (auto const& requirement : set)
            combined.insert(requirement);
    return {combined.begin(), combined.end()};
}
}
#endif