#include "ContentPackageLifecycle.h"

#include "ContentBuildRegistry.h"
#include "ContentPackageRegistry.h"
#include "ContentServerOwnership.h"

#include <algorithm>
#include <optional>

bool ContentPackageLifecycle::Analyse(std::string const& realm, std::string const& packageKey,
    bool currentlyDiscovered, ContentPackageRemovalAnalysis& analysis, std::string& error)
{
    analysis = ContentPackageRemovalAnalysis{};
    analysis.packageKey = packageKey;
    analysis.currentlyDiscovered = currentlyDiscovered;

    auto installed = ContentPackageRegistry().GetInstalledPackages();
    if (!installed.success)
    {
        error = installed.error;
        return false;
    }
    auto row = std::find_if(installed.packages.begin(), installed.packages.end(),
        [&](InstalledContentPackage const& package) { return package.packageKey == packageKey; });
    if (row != installed.packages.end())
    {
        analysis.installed = true;
        analysis.installedName = row->name;
        analysis.installedVersion = row->version;
        analysis.installedProvider = row->provider;
        analysis.installedSourcePath = row->sourcePath;
        analysis.installedAt = row->installedAt;
        auto source = ClassifySource(row->sourcePath);
        analysis.sourceState = source.state;
        analysis.sourceDetail = source.detail;
    }

    std::vector<ItemAllocation> leases;
    if (!ContentAllocationRegistry().FindByPackage(realm, packageKey, leases, error))
        return false;
    analysis.allocations = leases;
    for (auto const& lease : leases)
    {
        if (!analysis.allocationFirstBuild || lease.firstBuild < analysis.allocationFirstBuild)
            analysis.allocationFirstBuild = lease.firstBuild;
        if (lease.lastBuild > analysis.allocationLastBuild)
            analysis.allocationLastBuild = lease.lastBuild;
        analysis.firstMarkerBuild = analysis.allocationFirstBuild;
        analysis.lastMarkerBuild = analysis.allocationLastBuild;
    }

    ContentOwnerSummary owners;
    if (!ContentServerOwnership::CountOwned(realm, packageKey, owners, error))
        return false;
    analysis.ownedItemTemplates = owners.itemTemplates;
    analysis.ownedCurrencies = owners.currencies;
    analysis.ownedExtendedCosts = owners.extendedCosts;
    analysis.ownedVendors = owners.vendors;
    analysis.ownerLatestBuild = owners.latestAppliedBuild;
    if (analysis.ownerLatestBuild)
    {
        if (!analysis.firstMarkerBuild || analysis.ownerLatestBuild < analysis.firstMarkerBuild)
            analysis.firstMarkerBuild = analysis.ownerLatestBuild;
        if (analysis.ownerLatestBuild > analysis.lastMarkerBuild)
            analysis.lastMarkerBuild = analysis.ownerLatestBuild;
    }

    std::optional<ContentBuildRecord> active;
    if (!ContentBuildRegistry().GetActiveBuild(active, error))
        return false;
    analysis.activeBuild = active ? active->buildNumber : 0;
    analysis.activeBuildIncludesPackage = analysis.activeBuild != 0 && analysis.firstMarkerBuild != 0
        && analysis.activeBuild >= analysis.firstMarkerBuild
        && analysis.activeBuild <= analysis.lastMarkerBuild;
    return true;
}