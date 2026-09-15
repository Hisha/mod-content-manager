#include "Chat.h"
#include "CommandScript.h"
#include "ContentManager.h"
#include "ContentPackage.h"
#include "MpqBuilder.h"
#include "RBAC.h"

using namespace Acore::ChatCommands;

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
	    auto packages = sContentManager.ScanAvailablePackages();

	    handler->PSendSysMessage(
	        "Content Manager found {} EPF package(s).",
	        packages.size());

		handler->PSendSysMessage(
			"Module Directory: {}",
			sContentManager.GetModuleDirectory());
				
	    for (auto const& package : packages)
	    {
	        ContentPackage contentPackage(package.path);
	        auto result = contentPackage.Validate();

	        handler->PSendSysMessage(
	            " - {}",
	            package.filename);

	        if (!result.valid)
	        {
	            handler->PSendSysMessage(
	                "   INVALID: {}",
	                result.error);

	            continue;
	        }

	        handler->PSendSysMessage(
	            "   Package: {}",
	            result.manifest.packageKey);

	        handler->PSendSysMessage(
	            "   Name: {}",
	            result.manifest.name);

	        handler->PSendSysMessage(
	            "   Version: {}",
	            result.manifest.version);

	        handler->PSendSysMessage(
	            "   Schema: {}",
	            result.manifest.schema);
				
			handler->PSendSysMessage(
			    "   Content: {} item(s)",
			    result.manifest.content.size());

			for (auto const& entry : result.manifest.content)
			{
			    handler->PSendSysMessage(
			        "     {} -> {}",
			        entry.type,
			        entry.target);
			}	
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