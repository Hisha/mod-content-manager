#include "ContentBuildService.h"

#include "ContentBuildPaths.h"
#include "ContentBuildRegistry.h"
#include "ContentBuildHash.h"
#include "ContentManager.h"
#include "ContentPackage.h"
#include "ContentPackageRegistry.h"
#include "MpqBuilder.h"
#include "ContentAllocationRegistry.h"
#include "ContentResourceAllocator.h"
#include "ItemDbcComposer.h"
#include "DbcDescriptor.h"
#include "DbcReader.h"

#include <limits>
#include <fstream>
#include <set>
#include <map>
#include <mutex>
#include <random>
#include <vector>

namespace
{
std::mutex buildMutex;
}

std::string ContentBuildService::Number(std::uint32_t number)
{
    auto text = std::to_string(number);
    return std::string(text.size() < 6 ? 6 - text.size() : 0, '0') + text;
}

std::string ContentBuildService::FilenameRealm(std::string const& realmName)
{
    std::string safe;
    for (unsigned char c : realmName)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
            safe += static_cast<char>(c);
        else if (!safe.empty() && safe.back() != '-')
            safe += '-';
        if (safe.size() == 200)
            break;
    }
    while (!safe.empty() && safe.back() == '-')
        safe.pop_back();
    return safe.empty() ? "Realm" : safe;
}

ContentBuildResult ContentBuildService::Build(ContentManager const& manager, std::string const& realmName,
    Progress const& progress) const
{
    namespace fs = std::filesystem;
    using namespace ContentBuildPaths;
    ContentBuildResult result;
    std::unique_lock<std::mutex> lock(buildMutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        result.error = "Another content build is running";
        return result;
    }
    auto report = [&](std::string const& message) { if (progress) progress(message); };
    try
    {
        Require(manager.IsEnabled(), "Content Manager is disabled");
        Require(!realmName.empty() && realmName.size() <= 255 && realmName.find('\0') == std::string::npos,
            "Current realm name is empty or exceeds 255 UTF-8 bytes");
        auto installed = ContentPackageRegistry().GetInstalledPackages();
        Require(installed.success, installed.error);
        Require(!installed.packages.empty(), "No packages are installed; there is nothing to build");
        Require(installed.packages.size() <= std::numeric_limits<std::uint32_t>::max(), "Too many installed packages");
        result.packageCount = installed.packages.size();

        struct Source { ContentPackageCandidate candidate; ContentPackageValidationResult validation; };
        std::map<std::string, std::vector<Source>> discovered;
        for (auto const& candidate : manager.ScanAvailablePackages())
        {
            auto validation = ContentPackage(candidate.path).Validate();
            if (!validation.manifest.packageKey.empty())
                discovered[validation.manifest.packageKey].push_back({candidate, validation});
        }
        std::vector<Source> selected;
        std::map<std::string, std::string> owners;
        for (auto const& package : installed.packages)
        {
            auto found = discovered.find(package.packageKey);
            Require(found != discovered.end(), "Package '" + package.packageKey + "': no discovered EPF source (missing or invalid)");
            if (found->second.size() != 1)
            {
                std::string message = "Package '" + package.packageKey + "': duplicate discovered EPF sources";
                for (auto const& source : found->second)
                    message += " [" + source.candidate.provider + ": " + source.candidate.path.string() + "]";
                throw std::runtime_error(message);
            }
            auto source = found->second.front();
            source.validation = ContentPackage(source.candidate.path).Validate();
            Require(source.validation.valid, "Package '" + package.packageKey + "': " + source.validation.error);
            auto const& manifest = source.validation.manifest;
            Require(manifest.packageKey == package.packageKey, "Package key changed during discovery: " + package.packageKey);
            Require(manifest.version == package.version, "Package '" + package.packageKey + "': installed version "
                + package.version + ", discovered version " + manifest.version + "; refusing automatic upgrade");
            for (auto const& entry : manifest.content)
            {
                auto target = Fold(Target(entry.target));
                auto conflict = owners.find(target);
                Require(conflict == owners.end(), "Target collision '" + entry.target + "' between packages '"
                    + package.packageKey + "' and '" + (conflict == owners.end() ? "" : conflict->second) + "'");
                owners.emplace(target, package.packageKey);
            }
            selected.push_back(std::move(source));
        }
        // Semantic composition owns the canonical Item.dbc MPQ target once requested.
        std::vector<ResourceAllocationRequest> itemRequests;
        for (auto const& source : selected)
            for (auto const& row : source.validation.manifest.itemRows)
                itemRequests.push_back({source.validation.manifest.packageKey, row.symbol});
        bool composingItem = !itemRequests.empty();
        if (composingItem)
        {
            auto raw = owners.find(Fold("DBFilesClient/Item.dbc"));
            Require(raw == owners.end(), "Raw Item.dbc from package '" + (raw == owners.end() ? std::string() : raw->second)
                + "' conflicts with composed Item.dbc from package '" + itemRequests.front().packageKey + "'");
            owners.emplace(Fold("DBFilesClient/Item.dbc"), itemRequests.front().packageKey + " (composer)");
        }
        // A file also cannot occupy a directory needed by another target.
        for (auto const& entry : owners)
        {
            auto prefix = entry.first;
            while (prefix.find('/') != std::string::npos)
            {
                prefix.resize(prefix.rfind('/'));
                auto conflict = owners.find(prefix);
                Require(conflict == owners.end(), "File/directory target collision '" + prefix + "' between packages '"
                    + entry.second + "' and '" + (conflict == owners.end() ? "" : conflict->second) + "'");
            }
        }
        Require(owners.size() <= std::numeric_limits<std::uint32_t>::max(), "Too many cumulative files");
        ContentBuildRegistry builds;
        std::string error;
        if (!builds.NextNumber(result.buildNumber, error))
            throw std::runtime_error(error);
        std::vector<ItemAllocation> allocationPlan;
        std::vector<std::uint8_t> composedItemBytes;
        std::string baselineHash;
        std::uint32_t expectedItemRecords = 0;
        if (composingItem)
        {
            Require(manager.GetClientBuild() == 12340, "Item composition supports only client build 12340");
            Require(ContentBuildHash::Valid(manager.GetItemBaselineSha256()),
                "Set ContentManager.ItemBaselineSha256 to an administrator-verified Item.dbc hash");
            auto descriptor = FindDbcDescriptor(12340, "Item");
            Require(descriptor, "Item descriptor for build 12340 is missing");
            auto baseline = DbcReader::ReadBaseline(manager.GetBaselineDbcDirectory(), *descriptor);
            Require(baseline.valid, "Item baseline validation failed: " + baseline.error);
            fs::path baselinePath = fs::canonical(manager.GetBaselineDbcDirectory()) / descriptor->serverFile;
            Require(ContentBuildHash::Calculate(baselinePath, baselineHash, error), "Item baseline SHA-256 failed: " + error);
            Require(baselineHash == manager.GetItemBaselineSha256(),
                "Item baseline SHA-256 differs from ContentManager.ItemBaselineSha256; composition refused");
            std::set<std::uint32_t> baselineIDs;
            for (std::size_t i = 0; i < baseline.document.recordCount; ++i)
                Require(baselineIDs.insert(baseline.document.words[i * 8]).second, "Duplicate baseline Item ID");
            ContentAllocationRegistry registry;
            std::vector<ItemAllocation> retained;
            Require(registry.Read(realmName, retained, error), error);
            std::set<std::uint32_t> worldIDs;
            Require(registry.OccupiedWorldItems(worldIDs, error), error);
            worldIDs.insert(baselineIDs.begin(), baselineIDs.end());
            auto policy = ContentResourceAllocator::ItemIdPolicy(baselineIDs);
            allocationPlan = ContentResourceAllocator::Plan(realmName, policy, itemRequests,
                retained, worldIDs, result.buildNumber, baselineHash);
            std::vector<PlannedItemRow> rows;
            for (auto const& source : selected)
                for (auto const& row : source.validation.manifest.itemRows)
                {
                    auto found = std::find_if(allocationPlan.begin(), allocationPlan.end(), [&](auto const& a) {
                        return a.packageKey == source.validation.manifest.packageKey && a.symbol == row.symbol;
                    });
                    Require(found != allocationPlan.end(), "Missing Item ID allocation plan");
                    rows.push_back({found->value, row});
                    report("Planned item.id: " + found->packageKey + "/" + found->symbol + " = " + std::to_string(found->value));
                }
            composedItemBytes = ItemDbcComposer::Compose(baseline.document, rows);
            expectedItemRecords = baseline.document.recordCount + static_cast<std::uint32_t>(rows.size());
            report("Item baseline SHA-256: " + baselineHash);
            report("Item records: " + std::to_string(baseline.document.recordCount) + " -> "
                + std::to_string(baseline.document.recordCount + rows.size()));
            std::string recheckHash;
            Require(ContentBuildHash::Calculate(baselinePath, recheckHash, error) && recheckHash == baselineHash,
                "Item baseline changed during composition");
        }
        Require(!manager.GetOutputDirectory().empty() && !manager.GetWorkDirectory().empty(), "Build directories must not be empty");
        auto filename = FilenameRealm(realmName) + "-Content-" + Number(result.buildNumber) + ".mpq";
        result.outputPath = fs::absolute(fs::path(manager.GetOutputDirectory()) / filename);
        RejectLinks(result.outputPath);
        Require(!fs::exists(fs::symlink_status(result.outputPath)), "Candidate MPQ already exists; preserved: " + result.outputPath.string());
        RejectLinks(manager.GetWorkDirectory());
        fs::create_directories(manager.GetWorkDirectory());
        auto workRoot = fs::canonical(manager.GetWorkDirectory());
        std::random_device random;
        for (unsigned attempt = 0; attempt < 100 && result.workspace.empty(); ++attempt)
        {
            auto candidate = workRoot / ("build-" + Number(result.buildNumber) + "-" + std::to_string(random()));
            if (fs::create_directory(candidate))
                result.workspace = candidate;
        }
        Require(!result.workspace.empty(), "Could not reserve a new build workspace");
        report("Building realm content patch...");
        report("Realm: " + realmName);
        report("Build: " + Number(result.buildNumber));
        report("Installed packages: " + std::to_string(result.packageCount));
        report("Build workspace: " + result.workspace.string());
        for (auto const& source : selected)
        {
            auto const& manifest = source.validation.manifest;
            report("Staging: " + manifest.packageKey + " " + manifest.version);
            auto staged = ContentPackage(source.candidate.path).StageInto(result.workspace, manifest);
            Require(staged.success, "Package '" + manifest.packageKey + "': staging failed: " + staged.error);
            result.fileCount += staged.stagedFiles.size();
            report("  " + std::to_string(staged.stagedFiles.size()) + " file(s)");
        }
        Require(result.fileCount + (composingItem ? 1 : 0) == owners.size(),
            "Staged file count does not match the declared cumulative set");
        if (composingItem)
        {
            auto target = result.workspace / "DBFilesClient" / "Item.dbc";
            RejectLinks(target);
            fs::create_directories(target.parent_path());
            Require(!fs::exists(fs::symlink_status(target)), "Composed Item target already exists in workspace");
            { std::ofstream output(target, std::ios::binary | std::ios::trunc);
              Require(output.is_open(), "Cannot create composed Item.dbc");
              output.write(reinterpret_cast<char const*>(composedItemBytes.data()),
                  static_cast<std::streamsize>(composedItemBytes.size()));
              Require(output.good(), "Cannot write composed Item.dbc"); }
            auto reparsed = DbcReader::Read(target, *FindDbcDescriptor(12340, "Item"));
            Require(reparsed.valid, "Generated Item.dbc failed read-back: " + reparsed.error);
            Require(reparsed.document.recordCount == expectedItemRecords && reparsed.document.fieldCount == 8
                && reparsed.document.recordSize == 32, "Composed Item.dbc dimensions mismatch");
            std::string composedHash;
            Require(ContentBuildHash::Calculate(target, composedHash, error), "Composed Item SHA-256 failed: " + error);
            report("Composed Item.dbc SHA-256: " + composedHash);
            ++result.fileCount;
        }
        report("Cumulative files: " + std::to_string(result.fileCount));
        auto mpq = MpqBuilder().Build(result.workspace, result.outputPath);
        Require(mpq.success, "MPQ build failed: " + mpq.error);
        result.mpqCreated = true;
        result.fileCount = mpq.fileCount;
        report("MPQ built successfully.");
        report("MPQ: " + result.outputPath.string());
        report("Packages: " + std::to_string(result.packageCount));
        report("MPQ files: " + std::to_string(result.fileCount));
        std::string hash;
        if (!ContentBuildHash::Calculate(result.outputPath, hash, error))
            throw std::runtime_error("MPQ SHA256 failed: " + error);
        report("SHA256: " + hash);
        ContentBuildRecord record{result.buildNumber, realmName, filename,
            static_cast<std::uint32_t>(result.packageCount), static_cast<std::uint32_t>(result.fileCount), "STAGED", hash};
        bool committed = composingItem
            ? ContentAllocationRegistry().CommitComposed(record, allocationPlan, error)
            : builds.Record(record, error);
        if (!committed)
            throw std::runtime_error("MPQ was created, but recording the build/allocation could not be verified: " + error);
        result.recorded = true;
        report("Build state: STAGED");
        report("Build recorded successfully.");
        result.cleaned = Cleanup(result.workspace, workRoot, result.cleanupWarning);
        result.success = true;
        if (result.cleaned)
            report("Build workspace cleanup completed.");
        else
            report("WARNING: MPQ and build record are valid, but workspace cleanup failed: " + result.cleanupWarning);
    }
    catch (std::exception const& exception)
    {
        result.error = exception.what();
        // No artifact or workspace deletion on failure. A DB failure after MPQ
        // publication is explicitly reported for administrator inspection.
    }
    return result;
}
