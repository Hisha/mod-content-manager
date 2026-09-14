#include "Chat.h"
#include "CommandScript.h"
#include "ContentManager.h"
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
            "Realm: {}",
            sContentManager.GetRealmName());

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
        auto packages = sContentManager.ScanPatchHold();

        handler->PSendSysMessage(
            "Content Manager found {} EPF package(s).",
            packages.size());

        for (auto const& package : packages)
        {
            handler->PSendSysMessage(
                " - {}",
                package.filename);
        }

        return true;
    }
};

void AddSC_content_manager_commands()
{
    new content_manager_commandscript();
}