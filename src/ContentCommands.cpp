#include "Chat.h"
#include "CommandScript.h"
#include "ContentManager.h"
#include "ContentBuildService.h"
#include "ContentBuildRegistry.h"
#include <charconv>
#include "World.h"
#include "WorldSession.h"
#include "Realm.h"
#include "ContentPackage.h"
#include "ContentPackageRegistry.h"
#include "MpqBuilder.h"
#include "RBAC.h"
#include "ContentBuildHash.h"
#include "ContentBuildPaths.h"
#include "DbcDescriptor.h"
#include "DbcReader.h"
#include "ContentAllocationRegistry.h"
#include "ContentServerDeployment.h"

#include <algorithm>
#include <set>
#include <stdexcept>

using namespace Acore::ChatCommands;

namespace
{
bool CleanupStagingDirectory(std::filesystem::path const& stagingDirectory,
    std::filesystem::path const& workDirectory, std::string& error)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (stagingDirectory.empty() || workDirectory.empty())
    {
        error = "Staging and work directory paths must not be empty.";
        return false;
    }

    auto work = fs::canonical(workDirectory, ec);
    if (ec)
    {
        error = "Could not resolve work directory: " + ec.message();
        return false;
    }

    auto staging = fs::absolute(stagingDirectory, ec);
    if (ec)
    {
        error = "Could not resolve staging directory: " + ec.message();
        return false;
    }

    // Refuse links before canonicalization so a redirected staging path cannot
    // cause cleanup of another package, even if its target is inside WorkDirectory.
    fs::path prefix;
    for (auto const& component : staging)
    {
        prefix /= component;
        auto status = fs::symlink_status(prefix, ec);
        if (ec || fs::is_symlink(status))
        {
            error = ec ? "Could not inspect staging path: " + ec.message()
                : "Refusing cleanup through a symlink.";
            return false;
        }
    }

    staging = fs::canonical(staging, ec);
    if (ec)
    {
        error = "Could not resolve staging directory: " + ec.message();
        return false;
    }
    auto relative = staging.lexically_relative(work);
    if (relative.empty() || relative == "." || relative.is_absolute()
        || *relative.begin() == "..")
    {
        error = "Refusing cleanup: staging directory must be strictly beneath WorkDirectory.";
        return false;
    }
    if (!fs::is_directory(staging, ec) || ec)
    {
        error = ec ? "Could not inspect staging directory: " + ec.message()
            : "Staging path is not a directory.";
        return false;
    }

    // Delete only the validated directory derived from the successful Stage result.
    fs::remove_all(staging, ec);
    if (ec)
    {
        error = "Could not remove staging directory: " + ec.message();
        return false;
    }
    return true;
}
}

class content_manager_commandscript : public CommandScript
{
public:
    content_manager_commandscript()
        : CommandScript("content_manager_commandscript")
    {
    }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable buildCommandTable =
        {
            { "list", HandleBuildListCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "", HandleBuildCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
        };
        static ChatCommandTable dbcCommandTable =
        {
            { "inspect", HandleDbcInspectCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes }
        };
        static ChatCommandTable serverCommandTable =
        {
            { "apply", HandleServerApplyCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "status", HandleServerStatusCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes }
        };
        static ChatCommandTable contentCommandTable =
        {
            { "activate", HandleActivateCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "allocations", HandleAllocationsCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "build", buildCommandTable },
            { "dbc", dbcCommandTable },
            { "server", serverCommandTable },
            { "install", HandleInstallCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            { "uninstall", HandleUninstallCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
            {
                "status",
                HandleStatusCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
            {
                "scan",
                HandleScanCommand,
                rbac::RBAC_PERM_COMMAND_SERVER_INFO,
                Console::Yes
            },
			{ 
				"stage",
				 HandleStageCommand,
				 rbac::RBAC_PERM_COMMAND_SERVER_INFO,
				 Console::Yes
			},
        };

        static ChatCommandTable commandTable =
        {
            {
                "content",
                contentCommandTable
            }
        };

        return commandTable;
    }

    static bool HandleStatusCommand(ChatHandler* handler)
    {
        handler->PSendSysMessage(
            "Content Manager: {}",
            sContentManager.IsEnabled()
                ? "enabled"
                : "disabled");

        handler->PSendSysMessage(
            "PatchHold: {}",
            sContentManager.GetPatchHoldDirectory());

        handler->PSendSysMessage(
            "Work: {}",
            sContentManager.GetWorkDirectory());

        handler->PSendSysMessage(
            "Output: {}",
            sContentManager.GetOutputDirectory());
        handler->PSendSysMessage("Publish Directory: {}", sContentManager.GetPublishDirectory());

        handler->PSendSysMessage("DBC baseline: {}", sContentManager.GetBaselineDbcDirectory().empty()
            ? "unconfigured" : sContentManager.GetBaselineDbcDirectory());
        handler->PSendSysMessage("DBC client build: {}", sContentManager.GetClientBuild());

        return true;
    }

    static bool HandleAllocationsCommand(ChatHandler* handler)
    {
        std::vector<ItemAllocation> rows;
        std::string error;
        if (!ContentAllocationRegistry().Read(realm.Name, rows, error))
        {
            handler->PSendSysMessage("Allocation lookup failed: {}", error);
            return true;
        }
        handler->PSendSysMessage("Retained item.id allocations for realm {}: {}", realm.Name, rows.size());
        for (auto const& row : rows)
            handler->PSendSysMessage("{} / {} / item.id = {} [{}], builds {}..{}, baseline {}",
                row.packageKey, row.symbol, row.value, row.state, row.firstBuild, row.lastBuild,
                row.baselineSha256);
        return true;
    }

    static bool ParseBuildNumber(std::string const& text, std::uint32_t& value)
    {
        if (text.empty()) return false;
        auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() && value > 0;
    }

    static bool HandleServerStatusCommand(ChatHandler* handler, std::string argument)
    {
        std::uint32_t number = 0;
        if (!argument.empty() && !ParseBuildNumber(argument, number))
        { handler->SendSysMessage("Usage: .content server status [build-number]"); return true; }
        std::vector<ContentBuildRecord> builds;
        std::string error;
        if (!ContentBuildRegistry().GetBuilds(builds, error))
        { handler->PSendSysMessage("Server status failed: {}", error); return true; }
        if (!number)
            for (auto const& build : builds)
                if (build.realmName == realm.Name)
                {
                    bool exists = false;
                    ContentServerStatus candidate;
                    if (!ContentServerDeployment::ReadStatus(build.buildNumber, exists, candidate, error))
                    { handler->PSendSysMessage("Server status failed: {}", error); return true; }
                    if (exists) { number = build.buildNumber; break; }
                }
        auto build = std::find_if(builds.begin(), builds.end(), [&](auto const& row) {
            return row.buildNumber == number && row.realmName == realm.Name;
        });
        if (build == builds.end())
        { handler->SendSysMessage("No realm build found."); return true; }
        ContentServerStatus status;
        std::vector<ResolvedServerItem> rows;
        if (!ContentServerDeployment::Inspect(number, realm.Name, sContentManager.GetOutputDirectory(),
            status, rows, error))
        { handler->PSendSysMessage("Server status unavailable: {}", error); return true; }
        handler->PSendSysMessage("Build: {}  server: {}  client: {}", ContentBuildService::Number(number),
            status.state, build->state);
        handler->PSendSysMessage("Server item_template rows: {}", rows.size());
        for (auto const& row : rows)
            handler->PSendSysMessage("{} / {} / item.id = {}; item_template.entry = {}",
                row.packageKey, row.symbol, row.id, row.id);
        handler->PSendSysMessage("Server bundle SHA-256: {}", status.bundleSha256);
        handler->PSendSysMessage("Parity manifest SHA-256: {}", status.paritySha256);
        return true;
    }

    static bool HandleServerApplyCommand(ChatHandler* handler, std::string argument)
    {
        std::uint32_t number = 0;
        if (!ParseBuildNumber(argument, number))
        { handler->SendSysMessage("Usage: .content server apply <build-number>"); return true; }
        std::string summary, error;
        if (!ContentServerDeployment::Apply(number, realm.Name,
            sContentManager.GetOutputDirectory(), summary, error))
            handler->PSendSysMessage("Server apply refused: {}", error);
        else
            handler->SendSysMessage(summary);
        return true;
    }

    static bool HandleDbcInspectCommand(ChatHandler* handler, std::string table)
    {
        if (table.empty())
        {
            handler->SendSysMessage("Usage: .content dbc inspect <table>");
            return true;
        }
        if (!IsKnownDbcTable(table))
        {
            handler->PSendSysMessage("Unsupported DBC table '{}'. Registered table: Item.", table);
            return true;
        }
        auto build = sContentManager.GetClientBuild();
        auto descriptor = FindDbcDescriptor(build, table);
        if (!descriptor)
        {
            handler->PSendSysMessage("Unsupported DBC client build {}. This inspector supports build 12340 only.", build);
            return true;
        }
        auto const& configured = sContentManager.GetBaselineDbcDirectory();
        if (configured.empty())
        {
            handler->SendSysMessage("DBC baseline is unconfigured. Set ContentManager.BaselineDbcDirectory for inspection.");
            return true;
        }
        try
        {
            namespace fs = std::filesystem;
            auto parsed = DbcReader::ReadBaseline(configured, *descriptor);
            if (!parsed.valid)
                throw std::runtime_error(parsed.error);
            fs::path directory = fs::canonical(configured);
            fs::path source = directory / descriptor->serverFile;
            std::string sha256, hashError;
            if (!ContentBuildHash::Calculate(source, sha256, hashError))
                throw std::runtime_error("SHA-256 failed: " + hashError);
            handler->PSendSysMessage("DBC inspection PASS: {} (descriptor v{})", table, descriptor->version);
            handler->PSendSysMessage("Client build: {}", build);
            handler->PSendSysMessage("Baseline directory: {}", directory.string());
            handler->PSendSysMessage("Source: {}", source.string());
            handler->PSendSysMessage("SHA-256: {}", sha256);
            handler->PSendSysMessage("Records: {}; fields: {}; record bytes: {}; string bytes: {}",
                parsed.document.recordCount, parsed.document.fieldCount,
                parsed.document.recordSize, parsed.document.stringBlockSize);
            handler->SendSysMessage("Layout validation passed. Baseline provenance is reported, not approved or pinned by this phase.");
        }
        catch (std::exception const& e)
        {
            handler->PSendSysMessage("DBC inspection failed for '{}': {}", table, e.what());
        }
        return true;
    }

    static bool HandleScanCommand(ChatHandler* handler)
    {
        auto installed = ContentPackageRegistry().GetInstalledPackages();
        if (!installed.success)
        {
            handler->PSendSysMessage("Cannot read package states: {}", installed.error);
            return true;
        }
        auto packages = sContentManager.ScanAvailablePackages();
        handler->PSendSysMessage("Content Manager found {} EPF package(s).", packages.size());
        std::set<std::string> displayed;
        for (auto const& candidate : packages)
        {
            auto validation = ContentPackage(candidate.path).Validate();
            if (!validation.valid)
            {
                handler->PSendSysMessage(" - {}: INVALID: {}", candidate.path.string(), validation.error);
                continue;
            }
            auto const& manifest = validation.manifest;
            auto row = std::find_if(installed.packages.begin(), installed.packages.end(),
                [&](InstalledContentPackage const& package) { return package.packageKey == manifest.packageKey; });
            handler->PSendSysMessage("Package: {}", manifest.packageKey);
            if (row == installed.packages.end())
                handler->SendSysMessage("  State: AVAILABLE");
            else
            {
                ReportInstalledPackage(handler, *row);
                displayed.insert(row->packageKey);
            }
            handler->PSendSysMessage("  Name: {}", manifest.name);
            handler->PSendSysMessage("  Available version: {}", manifest.version);
            handler->PSendSysMessage("  Provider: {}", candidate.provider);
            handler->PSendSysMessage("  Source: {}", candidate.path.string());
            handler->PSendSysMessage("  Schema: {}", manifest.schema);
            handler->PSendSysMessage("  Content: {} item(s)", manifest.content.size());
            handler->PSendSysMessage("  DBC rows: {}", manifest.itemRows.size());
            handler->PSendSysMessage("  Server item_template rows: {}", manifest.serverItemRows.size());
            for (auto const& entry : manifest.content)
                handler->PSendSysMessage("    {} -> {}", entry.type, entry.target);
        }
        // Discovery cannot show a removed module. Still display its persistent row.
        for (auto const& row : installed.packages)
        {
            if (displayed.count(row.packageKey))
                continue;
            handler->PSendSysMessage("Package: {}", row.packageKey);
            ReportInstalledPackage(handler, row);
            handler->PSendSysMessage("  Installed name: {}", row.name);
            handler->SendSysMessage("  No valid EPF with this key is currently discovered.");
        }
        return true;
    }

    static void ReportInstalledPackage(ChatHandler* handler, InstalledContentPackage const& package)
    {
        std::error_code ec;
        auto status = std::filesystem::status(package.sourcePath, ec);
        if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory
            || (!ec && !std::filesystem::exists(status)))
            handler->SendSysMessage("  State: INSTALLED - SOURCE MISSING");
        else if (ec || !std::filesystem::is_regular_file(status))
            handler->PSendSysMessage("  State: INSTALLED - SOURCE UNAVAILABLE: {}",
                ec ? ec.message() : "Source is not a regular file");
        else
            handler->SendSysMessage("  State: INSTALLED");
        handler->PSendSysMessage("  Installed version: {}", package.version);
        handler->PSendSysMessage("  Installed provider: {}", package.provider);
        handler->PSendSysMessage("  Installed source: {}", package.sourcePath);
        handler->PSendSysMessage("  Installed at: {}", package.installedAt);
    }

    static bool HandleInstallCommand(ChatHandler* handler, std::string packageKey)
    {
        if (packageKey.empty())
        {
            handler->SendSysMessage("Usage: .content install <package-key>");
            return true;
        }
        std::vector<ContentPackageCandidate> matches;
        for (auto const& candidate : sContentManager.ScanAvailablePackages())
        {
            auto validation = ContentPackage(candidate.path).Validate();
            // Count any readable manifest declaring this key, even when a later
            // content validation fails. Never choose arbitrarily between matches.
            if (validation.manifest.packageKey == packageKey)
                matches.push_back(candidate);
        }
        if (matches.empty())
        {
            handler->PSendSysMessage("Package '{}' is not available.", packageKey);
            return true;
        }
        if (matches.size() != 1)
        {
            handler->PSendSysMessage("Cannot install '{}': multiple discovered EPFs declare this package key.", packageKey);
            for (auto const& candidate : matches)
                handler->PSendSysMessage("  Conflict: {} (provider: {})", candidate.path.string(), candidate.provider);
            return true;
        }
        auto const& candidate = matches.front();
        // Validate again immediately before asking the registry to change desired state.
        auto validation = ContentPackage(candidate.path).Validate();
        if (!validation.valid || validation.manifest.packageKey != packageKey)
        {
            handler->PSendSysMessage("Cannot install '{}': {}", packageKey,
                validation.valid ? "Package key changed during validation" : validation.error);
            return true;
        }
        std::error_code ec;
        auto source = std::filesystem::canonical(candidate.path, ec);
        if (ec)
        {
            handler->PSendSysMessage("Cannot resolve package source: {}", ec.message());
            return true;
        }
        auto const& manifest = validation.manifest;
        auto result = ContentPackageRegistry().Install({manifest.packageKey, manifest.name,
            manifest.version, candidate.provider, source.string(), {}});
        if (!result.success)
        {
            handler->PSendSysMessage("Install failed: {}", result.error);
            return true;
        }
        if (!result.changed)
        {
            handler->PSendSysMessage("Package '{}' is already installed. Installed metadata was not changed.", packageKey);
            return true;
        }
        handler->SendSysMessage("Installed package:");
        handler->PSendSysMessage("  {} {}", packageKey, manifest.version);
        handler->PSendSysMessage("  Provider: {}", candidate.provider);
        handler->SendSysMessage("Only the desired package set changed. Run .content build to generate a cumulative MPQ; no patch was rebuilt or published.");
        return true;
    }

    static bool HandleUninstallCommand(ChatHandler* handler, std::string packageKey)
    {
        if (packageKey.empty())
        {
            handler->SendSysMessage("Usage: .content uninstall <package-key>");
            return true;
        }
        auto result = ContentPackageRegistry().Uninstall(packageKey);
        if (!result.success)
        {
            handler->PSendSysMessage("Uninstall failed: {}", result.error);
            return true;
        }
        if (!result.changed)
        {
            handler->PSendSysMessage("Package '{}' is not installed.", packageKey);
            return true;
        }
        handler->PSendSysMessage("Uninstalled package: {}", packageKey);
        handler->SendSysMessage("The EPF was preserved. Run .content build to generate a cumulative MPQ; no patch was rebuilt or published.");
        return true;
    }

    static bool HandleBuildListCommand(ChatHandler* handler)
    {
        std::vector<ContentBuildRecord> records;
        std::string error;
        if (!ContentBuildRegistry().GetBuilds(records, error))
            handler->PSendSysMessage("Cannot list builds: {}", error);
        else if (records.empty())
            handler->SendSysMessage("No realm content builds exist.");
        else
        {
            handler->SendSysMessage("Realm content builds:");
            for (auto const& row : records)
            {
                handler->PSendSysMessage("{}  {}", ContentBuildService::Number(row.buildNumber), row.state);
                handler->PSendSysMessage("  {}", row.filename);
                handler->PSendSysMessage("  Packages: {}", row.packageCount);
                handler->PSendSysMessage("  Files: {}", row.fileCount);
                handler->PSendSysMessage("  SHA256: {}", row.sha256);
            }
        }
        return true;
    }

    static bool HandleActivateCommand(ChatHandler* handler, std::string argument)
    {
        if (handler->GetSession() && handler->GetSession()->GetSecurity() < SEC_ADMINISTRATOR)
        {
            handler->SendSysMessage("Build activation requires administrator access.");
            return true;
        }
        if (!sContentManager.IsEnabled())
        {
            handler->SendSysMessage("Content Manager is disabled.");
            return true;
        }
        std::uint32_t number = 0;
        auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), number);
        if (parsed.ec != std::errc() || parsed.ptr != argument.data() + argument.size() || !number)
        {
            handler->SendSysMessage("Invalid build number: expected a positive 32-bit decimal integer.");
            return true;
        }
        std::string error;
        bool alreadyActive = false;
        ContentBuildRegistry registry;
        ContentPublicationResult publication;
        if (!registry.ActivateBuild(number, sContentManager.GetOutputDirectory(),
            sContentManager.GetPublishDirectory(), publication, alreadyActive, error))
            handler->PSendSysMessage("Activation refused: {}", error);
        else
        {
            handler->PSendSysMessage("Build {} verified.", ContentBuildService::Number(number));
            handler->PSendSysMessage("Published: {}", publication.path.string());
            handler->PSendSysMessage("SHA256: {}", publication.sha256);
            if (publication.reused)
                handler->SendSysMessage("Matching published artifact reused.");
            handler->PSendSysMessage("Build {} {}", ContentBuildService::Number(number),
                alreadyActive ? "is already ACTIVE." : "is now ACTIVE.");
        }
        return true;
    }

    static bool HandleBuildCommand(ChatHandler* handler)
    {
        auto result = ContentBuildService().Build(sContentManager, realm.Name,
            [handler](std::string const& message) { handler->PSendSysMessage("{}", message); });
        if (!result.success)
        {
            handler->PSendSysMessage("Content build failed: {}", result.error);
            if (!result.workspace.empty())
                handler->PSendSysMessage("Workspace preserved: {}", result.workspace.string());
            if (result.mpqCreated)
                handler->PSendSysMessage("Completed MPQ preserved: {}", result.outputPath.string());
            // The failure was reported above; do not append generic command usage.
            return true;
        }
        return true;
    }
	static bool HandleStageCommand(
	    ChatHandler* handler,
	    std::string packageKey)
	{
	    if (packageKey.empty())
	    {
	        handler->SendSysMessage(
	            "Usage: .content stage <package-key>");

	        return true;
	    }

        // The package key is used as a single staging/output path component.
        if (packageKey == "." || packageKey == ".."
            || packageKey.find_first_of("/\\:") != std::string::npos)
        {
            handler->SendSysMessage("Invalid package key: expected a single directory name.");
            return true;
        }
	    auto packages =
	        sContentManager.ScanAvailablePackages();

	    for (auto const& candidate : packages)
	    {
	        ContentPackage package(candidate.path);

	        ContentPackageValidationResult validation =
	            package.Validate();

	        if (!validation.valid)
	            continue;

	        if (validation.manifest.packageKey != packageKey)
	            continue;

            if (!validation.manifest.itemRows.empty())
            {
                handler->SendSysMessage("Schema 2 DBC rows require an installed cumulative .content build for allocation and composition.");
                return true;
            }

	        handler->PSendSysMessage(
	            "Staging package '{}'...",
	            validation.manifest.name);

	        ContentPackageStageResult result =
	            package.Stage(
	                sContentManager.GetWorkDirectory());

	        if (!result.success)
	        {
	            handler->PSendSysMessage(
	                "Stage failed: {}",
	                result.error);

	            return true;
	        }

	        handler->PSendSysMessage(
	            "Staged {} content item(s).",
	            result.stagedFiles.size());

	        for (auto const& file :
	             result.stagedFiles)
	        {
	            handler->PSendSysMessage(
	                " - {}",
	                file.string());
	        }

	        handler->SendSysMessage(
	            "Package staged successfully.");

            handler->PSendSysMessage("Staged files: {}", result.stagedFiles.size());
            auto output = std::filesystem::path(sContentManager.GetOutputDirectory())
                / (validation.manifest.packageKey + "-test.mpq");
            auto build = MpqBuilder().Build(result.stagingDirectory, output);
            if (!build.success)
            {
                handler->PSendSysMessage("MPQ build failed: {}", build.error);
                return true;
            }
            handler->SendSysMessage("MPQ built successfully.");
            handler->PSendSysMessage("MPQ: {}", build.outputPath.string());
            handler->PSendSysMessage("MPQ files: {}", build.fileCount);
            std::string cleanupError;
            if (CleanupStagingDirectory(result.stagingDirectory,
                sContentManager.GetWorkDirectory(), cleanupError))
            {
                handler->SendSysMessage("Staging cleanup completed.");
            }
            else
            {
                handler->PSendSysMessage(
                    "WARNING: MPQ was built successfully, but staging cleanup failed: {}",
                    cleanupError);
            }
	        return true;
	    }

	    handler->PSendSysMessage(
	        "Content package '{}' was not found.",
	        packageKey);

	    return true;
	}
};

void AddSC_content_manager_commands()
{
    new content_manager_commandscript();
}
