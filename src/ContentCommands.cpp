#include "Chat.h"
#include "CommandScript.h"
#include "ContentManager.h"
#include "ContentPackage.h"
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
            }
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
};

void AddSC_content_manager_commands()
{
    new content_manager_commandscript();
}