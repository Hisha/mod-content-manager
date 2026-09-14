#include "ContentManager.h"
#include "ScriptMgr.h"

void AddSC_content_manager_commands();

class content_manager_worldscript : public WorldScript
{
public:
    content_manager_worldscript()
        : WorldScript("content_manager_worldscript")
    {
    }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sContentManager.LoadConfig();
    }
};

void Addmod_content_managerScripts()
{
    new content_manager_worldscript();

    AddSC_content_manager_commands();
}