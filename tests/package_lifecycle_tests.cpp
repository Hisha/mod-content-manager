#include "ContentAllocationRegistry.h"
#include "ContentPackageLifecycle.h"
#include "ContentServerOwnership.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main()
{
    std::filesystem::path directory = std::filesystem::temp_directory_path() / "content-lifecycle-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    auto source = ContentPackageLifecycle::ClassifySource("(no such path)");
    assert(source.state == ContentInstalledSourceState::SourceMissing);
    assert(source.detail.empty());

    std::string epf = (directory / "present.epf").string();
    {
        std::ofstream(directory / "present.epf") << "pk";
    }
    source = ContentPackageLifecycle::ClassifySource(epf);
    assert(source.state == ContentInstalledSourceState::Active);

    source = ContentPackageLifecycle::ClassifySource(directory.string());
    assert(source.state == ContentInstalledSourceState::SourceUnavailable);
    assert(!source.detail.empty());

    assert(std::string(ContentPackageLifecycle::StateLabel(ContentInstalledSourceState::Active)) == "INSTALLED");
    assert(std::string(ContentPackageLifecycle::StateLabel(ContentInstalledSourceState::SourceMissing))
        == "INSTALLED - SOURCE MISSING");
    assert(std::string(ContentPackageLifecycle::StateLabel(ContentInstalledSourceState::SourceUnavailable))
        == "INSTALLED - SOURCE UNAVAILABLE");

    ContentPackageRemovalAnalysis present;
    present.packageKey = "mod-example";
    present.installed = true;
    present.installedName = "Example Content";
    present.installedVersion = "1.0.0";
    present.installedProvider = "Example";
    present.installedSourcePath = epf;
    present.installedAt = "1970-01-01 00:00:00";
    present.sourceState = ContentInstalledSourceState::Active;
    present.currentlyDiscovered = true;
    assert(!ContentPackageLifecycle::HasRetainedHistory(present));
    auto lines = ContentPackageLifecycle::Summary(present);
    assert(!lines.empty());
    auto contains = [&](std::string const& needle) {
        for (auto const& line : lines)
            if (line.find(needle) != std::string::npos) return true;
        return false;
    };
    assert(contains("Package: mod-example"));
    assert(contains("State: INSTALLED"));
    assert(contains("Installed version: 1.0.0"));
    assert(contains("Currently discovered EPF: yes"));
    assert(contains("original EPF is available and is not deleted"));

    ContentPackageRemovalAnalysis missing = present;
    missing.installedSourcePath = "(gone)";
    missing.sourceState = ContentInstalledSourceState::SourceMissing;
    missing.currentlyDiscovered = false;
    missing.sourceDetail = "";
    auto missingLines = ContentPackageLifecycle::Summary(missing);
    auto missingContains = [&](std::string const& needle) {
        for (auto const& line : missingLines)
            if (line.find(needle) != std::string::npos) return true;
        return false;
    };
    assert(missingContains("State: INSTALLED - SOURCE MISSING"));
    assert(missingContains("Currently discovered EPF: no"));
    assert(missingContains("original EPF is missing. Removal does not require or recreate it"));

    ContentPackageRemovalAnalysis retained = present;
    retained.allocations = {ItemAllocation{"Eitrigg", "mod-example", "seal", 1, "reserved", 4, 6, "hash"}};
    retained.allocationFirstBuild = 4;
    retained.allocationLastBuild = 6;
    retained.firstMarkerBuild = 4;
    retained.lastMarkerBuild = 6;
    retained.ownedItemTemplates = 3;
    retained.ownerLatestBuild = 7;
    assert(ContentPackageLifecycle::HasRetainedHistory(retained));
    auto retainedLines = ContentPackageLifecycle::Summary(retained);
    auto retainedContains = [&](std::string const& needle) {
        for (auto const& line : retainedLines)
            if (line.find(needle) != std::string::npos) return true;
        return false;
    };
    assert(retainedContains("Retained DBC allocations: 1"));
    assert(retainedContains("(builds 4..6)"));
    assert(retainedContains("item_template: 3"));
    assert(retainedContains("Build membership markers: builds 4..6"));
    assert(retainedContains("active build") == false);
    assert(retainedContains("Removal changes only the desired installed package set"));
    assert(retainedContains("Preserved: completed builds and sidecars"));

    ContentPackageRemovalAnalysis withActive = retained;
    withActive.activeBuild = 5;
    withActive.activeBuildIncludesPackage = true;
    auto activeLines = ContentPackageLifecycle::Summary(withActive);
    auto activeContains = [&](std::string const& needle) {
        for (auto const& line : activeLines)
            if (line.find(needle) != std::string::npos) return true;
        return false;
    };
    assert(activeContains("currently ACTIVE build (5) includes this package"));

    ContentPackageRemovalAnalysis none;
    assert(!ContentPackageLifecycle::HasRetainedHistory(none));
    none.ownedVendors = 2;
    assert(ContentPackageLifecycle::HasRetainedHistory(none));

    std::filesystem::remove_all(directory);
    return 0;
}