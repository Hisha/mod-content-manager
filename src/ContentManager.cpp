#include "ContentManager.h"

#include "Config.h"
#include "Log.h"
#include "World.h"

#include <algorithm>

ContentManager& ContentManager::Instance()
{
    static ContentManager instance;
    return instance;
}

void ContentManager::LoadConfig()
{
    _enabled = sConfigMgr->GetOption<bool>("ContentManager.Enable", false);

    _patchHoldDirectory =
        sConfigMgr->GetOption<std::string>(
            "ContentManager.PatchHoldDirectory",
            "./patchhold");

    _workDirectory =
        sConfigMgr->GetOption<std::string>(
            "ContentManager.WorkDirectory",
            "./patchwork");

    _outputDirectory =
        sConfigMgr->GetOption<std::string>(
            "ContentManager.OutputDirectory",
            "./patches");

	LOG_INFO(
		"module",
		"mod-content-manager: enabled={}, patchhold='{}', work='{}', output='{}'",
		_enabled,
		_patchHoldDirectory,
		_workDirectory,
		_outputDirectory);
}

bool ContentManager::IsEnabled() const
{
    return _enabled;
}

std::string const& ContentManager::GetPatchHoldDirectory() const
{
    return _patchHoldDirectory;
}

std::string const& ContentManager::GetWorkDirectory() const
{
    return _workDirectory;
}

std::string const& ContentManager::GetOutputDirectory() const
{
    return _outputDirectory;
}

std::vector<ContentPackageCandidate> ContentManager::ScanPatchHold() const
{
    std::vector<ContentPackageCandidate> packages;

    if (!_enabled)
        return packages;

    std::filesystem::path root(_patchHoldDirectory);

    std::error_code ec;

    if (!std::filesystem::exists(root, ec))
    {
        LOG_WARN(
            "module",
            "mod-content-manager: patchhold directory '{}' does not exist",
            root.string());

        return packages;
    }

    for (auto const& entry :
         std::filesystem::directory_iterator(root, ec))
    {
        if (ec)
            break;

        if (!entry.is_regular_file())
            continue;

        std::filesystem::path path = entry.path();

        std::string extension = path.extension().string();

        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });

        if (extension != ".epf")
            continue;

        packages.push_back(
        {
            path,
            path.filename().string()
        });
    }

    std::sort(
        packages.begin(),
        packages.end(),
        [](ContentPackageCandidate const& a,
           ContentPackageCandidate const& b)
        {
            return a.filename < b.filename;
        });

    return packages;
}