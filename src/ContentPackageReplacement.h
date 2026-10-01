#ifndef CONTENT_PACKAGE_REPLACEMENT_H
#define CONTENT_PACKAGE_REPLACEMENT_H

#include "ContentAllocationRegistry.h"
#include "ContentPackage.h"

#include <map>
#include <set>
#include <string>
#include <vector>

// Resolve build-wide replacement declarations before any allocation planning.
// This is deliberately package-generic and side-effect free so the dangerous
// selection cases are covered without a database fixture.
inline bool ResolveContentPackageReplacements(
    std::vector<ContentPackageManifest const*> const& manifests,
    AllocationReplacements& replacements, std::string& error)
{
    replacements.clear();
    error.clear();
    std::set<std::string> selected;
    std::map<std::string, std::string> successorByPredecessor;
    for (auto const* manifest : manifests)
        selected.insert(manifest->packageKey);
    for (auto const* manifest : manifests)
        for (auto const& predecessor : manifest->replaces) {
            if (selected.count(predecessor)) {
                error = "Package '" + manifest->packageKey + "' replaces '" +
                    predecessor + "', but both are selected in the same build";
                replacements.clear();
                return false;
            }
            auto inserted = successorByPredecessor.emplace(
                predecessor, manifest->packageKey);
            if (!inserted.second && inserted.first->second != manifest->packageKey) {
                error = "Historical package '" + predecessor +
                    "' is replaced by multiple selected packages: '" +
                    inserted.first->second + "' and '" + manifest->packageKey + "'";
                replacements.clear();
                return false;
            }
            replacements[manifest->packageKey].insert(predecessor);
        }
    return true;
}

#endif
