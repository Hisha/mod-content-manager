#ifndef CONTENT_PACKAGE_LIFECYCLE_H
#define CONTENT_PACKAGE_LIFECYCLE_H

#include "ContentAllocationRegistry.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// Classification of the source EPF recorded for an installed package.
enum class ContentInstalledSourceState
{
    Active,          // recorded source path is a readable regular file
    SourceMissing,   // recorded source path no longer exists
    SourceUnavailable // recorded source path exists but is not a regular file
};

struct ContentSourceStatus
{
    ContentInstalledSourceState state = ContentInstalledSourceState::SourceMissing;
    std::string detail;
};

// Everything Content Manager can currently determine about one package before
// the administrator retires it from the desired installed set. This is the
// pre-removal survey: every field is retained state that uninstall must NOT
// delete (builds, sidecars, published artifacts, allocation leases, and server
// ownership records are preserved for history).
struct ContentPackageRemovalAnalysis
{
    std::string packageKey;
    bool installed = false;
    std::string installedName;
    std::string installedVersion;
    std::string installedProvider;
    std::string installedSourcePath;
    std::string installedAt;
    ContentInstalledSourceState sourceState = ContentInstalledSourceState::SourceMissing;
    std::string sourceDetail;
    bool currentlyDiscovered = false;

    // Persistent DBC/resource leases retained for this package.
    std::vector<ItemAllocation> allocations;
    std::uint32_t allocationFirstBuild = 0;
    std::uint32_t allocationLastBuild = 0;

    // Applied server-side rows owned by this package (provenance-retained).
    std::uint32_t ownedItemTemplates = 0;
    std::uint32_t ownedCurrencies = 0;
    std::uint32_t ownedExtendedCosts = 0;
    std::uint32_t ownedVendors = 0;
    std::uint32_t ownedManagedServerResources = 0;
    std::uint32_t ownerLatestBuild = 0;

    // Best-effort build membership markers. Retained leases start at
    // allocationFirstBuild and end at allocationLastBuild; server ownership may
    // add an applied build. Raw-file-only packages have no persisted marker.
    std::uint32_t firstMarkerBuild = 0;
    std::uint32_t lastMarkerBuild = 0;

    std::uint32_t activeBuild = 0;
    bool activeBuildIncludesPackage = false;
};

class ContentPackageLifecycle
{
public:
    // Pure filesystem classification, shared by scan and uninstall reporting.
    static ContentSourceStatus ClassifySource(std::string const& sourcePath);
    static char const* StateLabel(ContentInstalledSourceState state);
    static bool HasRetainedHistory(ContentPackageRemovalAnalysis const& analysis);

    // Pure pre-removal summary lines; no database access. Used by .content
    // uninstall before it performs the removal, and by tests.
    static std::vector<std::string> Summary(ContentPackageRemovalAnalysis const& analysis);

    // Database-backed survey: gathers the installed record, retained leases,
    // applied server ownership, and current build state for one package.
    static bool Analyse(std::string const& realm, std::string const& packageKey,
        bool currentlyDiscovered, ContentPackageRemovalAnalysis& analysis, std::string& error);
};

inline ContentSourceStatus ContentPackageLifecycle::ClassifySource(std::string const& sourcePath)
{
    std::error_code ec;
    auto status = std::filesystem::status(sourcePath, ec);
    if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory
        || (!ec && !std::filesystem::exists(status)))
        return {ContentInstalledSourceState::SourceMissing, {}};
    if (ec || !std::filesystem::is_regular_file(status))
        return {ContentInstalledSourceState::SourceUnavailable,
            ec ? ec.message() : "Source is not a regular file"};
    return {ContentInstalledSourceState::Active, {}};
}

inline char const* ContentPackageLifecycle::StateLabel(ContentInstalledSourceState state)
{
    switch (state)
    {
        case ContentInstalledSourceState::Active: return "INSTALLED";
        case ContentInstalledSourceState::SourceMissing: return "INSTALLED - SOURCE MISSING";
        case ContentInstalledSourceState::SourceUnavailable: return "INSTALLED - SOURCE UNAVAILABLE";
    }
    return "INSTALLED - SOURCE MISSING";
}

inline bool ContentPackageLifecycle::HasRetainedHistory(ContentPackageRemovalAnalysis const& analysis)
{
    return !analysis.allocations.empty() || analysis.ownedItemTemplates || analysis.ownedCurrencies
        || analysis.ownedExtendedCosts || analysis.ownedVendors || analysis.ownedManagedServerResources;
}

inline std::vector<std::string> ContentPackageLifecycle::Summary(
    ContentPackageRemovalAnalysis const& analysis)
{
    std::vector<std::string> lines;
    lines.push_back("Package: " + analysis.packageKey);
    lines.push_back(std::string("  State: ") + StateLabel(analysis.sourceState));
    lines.push_back("  Installed version: " + analysis.installedVersion
        + "   provider: " + analysis.installedProvider);
    if (!analysis.installedAt.empty())
        lines.push_back("  Installed at: " + analysis.installedAt);
    lines.push_back("  Installed source: " + analysis.installedSourcePath);
    if (!analysis.sourceDetail.empty())
        lines.push_back("  Source detail: " + analysis.sourceDetail);
    lines.push_back(std::string("  Currently discovered EPF: ")
        + (analysis.currentlyDiscovered ? "yes" : "no"));
    if (analysis.sourceState == ContentInstalledSourceState::SourceMissing)
        lines.push_back("  The original EPF is missing. Removal does not require or recreate it.");
    else
        lines.push_back("  The original EPF is available and is not deleted by removal.");
    if (!analysis.allocations.empty())
        lines.push_back("  Retained DBC allocations: " + std::to_string(analysis.allocations.size())
            + (analysis.allocationFirstBuild
                ? " (builds " + std::to_string(analysis.allocationFirstBuild) + ".."
                    + std::to_string(analysis.allocationLastBuild) + ")"
                : std::string())
            + "; these leases are never recycled on removal");
    if (analysis.ownedItemTemplates || analysis.ownedCurrencies
        || analysis.ownedExtendedCosts || analysis.ownedVendors || analysis.ownedManagedServerResources)
    {
        lines.push_back("  Applied server content owned by this package:");
        lines.push_back("    item_template: " + std::to_string(analysis.ownedItemTemplates)
            + "   currencytypes_dbc: " + std::to_string(analysis.ownedCurrencies)
            + "   itemextendedcost_dbc: " + std::to_string(analysis.ownedExtendedCosts)
            + "   npc_vendor relationships: " + std::to_string(analysis.ownedVendors)
            + "   generic server resources: " + std::to_string(analysis.ownedManagedServerResources));
        if (analysis.ownerLatestBuild)
            lines.push_back("    latest applied build: " + std::to_string(analysis.ownerLatestBuild));
    }
    if (analysis.firstMarkerBuild)
        lines.push_back("  Build membership markers: builds "
            + std::to_string(analysis.firstMarkerBuild) + ".."
            + std::to_string(analysis.lastMarkerBuild)
            + " (retained leases/applied content; raw-file-only packages have no marker)");
    if (analysis.activeBuildIncludesPackage)
        lines.push_back("  The currently ACTIVE build (" + std::to_string(analysis.activeBuild)
            + ") includes this package. Rebuild and activate to retire it from active content.");
    lines.push_back("Removal changes only the desired installed package set: it removes this package's");
    lines.push_back("current selection record from content_manager_package.");
    lines.push_back("  Preserved: completed builds and sidecars, published artifacts, allocation leases,");
    lines.push_back("  and server ownership records, so historical builds and rollback remain coherent.");
    lines.push_back("  Not deleted: live server rows (item_template, currencytypes_dbc,");
    lines.push_back("  itemextendedcost_dbc, npc_vendor, managed templates/spawns). Remove those separately if no longer wanted.");
    lines.push_back("  No build, publish, or activation happens here. Run .content build, then");
    lines.push_back("  .content activate <build-number> (and .content server apply if server rows apply).");
    return lines;
}

#endif
