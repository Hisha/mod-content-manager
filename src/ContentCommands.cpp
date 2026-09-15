#include "Chat.h"
#include "CommandScript.h"
#include "ContentManager.h"
#include "ContentBuildService.h"
#include "World.h"
#include "ContentPackage.h"
#include "ContentPackageRegistry.h"
#include "MpqBuilder.h"
#include "RBAC.h"

#include <algorithm>
#include <set>

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
        static ChatCommandTable contentCommandTable =
        {
            { "build", HandleBuildCommand, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Console::Yes },
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

        return true;
    }

    static bool HandleScanCommand(ChatHandler* handler)
    {
        auto installed = ContentPackageRegistry().GetInstalledPackages();
        if (!installed.success)
        {
            handler->PSendSysMessage("Cannot read package states: {}", installed.error);
            return false;
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
            return false;
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
            return false;
        }
        if (matches.size() != 1)
        {
            handler->PSendSysMessage("Cannot install '{}': multiple discovered EPFs declare this package key.", packageKey);
            for (auto const& candidate : matches)
                handler->PSendSysMessage("  Conflict: {} (provider: {})", candidate.path.string(), candidate.provider);
            return false;
        }
        auto const& candidate = matches.front();
        // Validate again immediately before asking the registry to change desired state.
        auto validation = ContentPackage(candidate.path).Validate();
        if (!validation.valid || validation.manifest.packageKey != packageKey)
        {
            handler->PSendSysMessage("Cannot install '{}': {}", packageKey,
                validation.valid ? "Package key changed during validation" : validation.error);
            return false;
        }
        std::error_code ec;
        auto source = std::filesystem::canonical(candidate.path, ec);
        if (ec)
        {
            handler->PSendSysMessage("Cannot resolve package source: {}", ec.message());
            return false;
        }
        auto const& manifest = validation.manifest;
        auto result = ContentPackageRegistry().Install({manifest.packageKey, manifest.name,
            manifest.version, candidate.provider, source.string(), {}});
        if (!result.success)
        {
            handler->PSendSysMessage("Install failed: {}", result.error);
            return false;
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
            return false;
        }
        auto result = ContentPackageRegistry().Uninstall(packageKey);
        if (!result.success)
        {
            handler->PSendSysMessage("Uninstall failed: {}", result.error);
            return false;
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

    static bool HandleBuildCommand(ChatHandler* handler)
    {
        auto result = ContentBuildService().Build(sContentManager, sWorld->GetRealmName(),
            [handler](std::string const& message) { handler->PSendSysMessage("{}", message); });
        if (!result.success)
        {
            handler->PSendSysMessage("Content build failed: {}", result.error);
            if (!result.workspace.empty())
                handler->PSendSysMessage("Workspace preserved: {}", result.workspace.string());
            if (result.mpqCreated)
                handler->PSendSysMessage("Completed MPQ preserved: {}", result.outputPath.string());
            return false;
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

	        return false;
	    }

        // The package key is used as a single staging/output path component.
        if (packageKey == "." || packageKey == ".."
            || packageKey.find_first_of("/\\:") != std::string::npos)
        {
            handler->SendSysMessage("Invalid package key: expected a single directory name.");
            return false;
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

	            return false;
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
                return false;
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

	    return false;
	}
};

void AddSC_content_manager_commands()
{
    new content_manager_commandscript();
}



