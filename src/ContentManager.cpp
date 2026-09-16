#include "ContentManager.h"

#include "Config.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <StormLib.h>

namespace
{
    bool EnsureDirectory(std::string const& directory, char const* description)
    {
        std::error_code ec;
        std::filesystem::path path(directory);

        if (std::filesystem::exists(path, ec))
        {
            if (!std::filesystem::is_directory(path, ec))
            {
                LOG_ERROR(
                    "module",
                    "mod-content-manager: {} '{}' exists but is not a directory",
                    description,
                    directory);

                return false;
            }

            return true;
        }

        if (!std::filesystem::create_directories(path, ec))
        {
            LOG_ERROR(
                "module",
                "mod-content-manager: failed to create {} '{}': {}",
                description,
                directory,
                ec.message());

            return false;
        }

        LOG_INFO(
            "module",
            "mod-content-manager: created {} '{}'",
            description,
            directory);

        return true;
    }
}

ContentManager& ContentManager::Instance()
{
    static ContentManager instance;
    return instance;
}

void ContentManager::LoadConfig()
{
    _enabled = sConfigMgr->GetOption<bool>("ContentManager.Enable", false);

	_moduleDirectory =
		sConfigMgr->GetOption<std::string>(
	    	"ContentManager.ModuleDirectory",
	    	"./modules");

    _baselineDbcDirectory = sConfigMgr->GetOption<std::string>(
        "ContentManager.BaselineDbcDirectory", "");
    _clientBuild = sConfigMgr->GetOption<std::uint32_t>(
        "ContentManager.ClientBuild", 12340);
	
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

    _publishDirectory = sConfigMgr->GetOption<std::string>(
        "ContentManager.PublishDirectory", "./published-content");

	LOG_INFO(
		"module",
		"mod-content-manager: enabled={}, patchhold='{}', work='{}', output='{}', publish='{}'",
		_enabled,
		_patchHoldDirectory,
		_workDirectory,
		_outputDirectory,
        _publishDirectory);
		
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

std::string const& ContentManager::GetPublishDirectory() const
{
    return _publishDirectory;
}

std::string const& ContentManager::GetModuleDirectory() const
{
    return _moduleDirectory;
}

std::string const& ContentManager::GetBaselineDbcDirectory() const
{
    return _baselineDbcDirectory;
}

std::uint32_t ContentManager::GetClientBuild() const
{
    return _clientBuild;
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
			path.filename().string(),
			ContentPackageSource::PatchHold,
			"patchhold"
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

std::vector<ContentPackageCandidate>
ContentManager::ScanAvailablePackages() const
{
    std::vector<ContentPackageCandidate> packages =
        ScanPatchHold();

    if (!_enabled)
        return packages;

    std::filesystem::path modulesRoot(_moduleDirectory);

    std::error_code ec;

    if (!std::filesystem::exists(modulesRoot, ec))
    {
        LOG_WARN(
            "module",
            "mod-content-manager: module directory '{}' does not exist",
            modulesRoot.string());

        return packages;
    }

    for (auto const& moduleEntry :
         std::filesystem::directory_iterator(modulesRoot, ec))
    {
        if (ec)
            break;

        if (!moduleEntry.is_directory())
            continue;

        std::filesystem::path contentDirectory =
            moduleEntry.path() / "content";

        std::error_code contentEc;

        if (!std::filesystem::exists(
                contentDirectory,
                contentEc) ||
            !std::filesystem::is_directory(
                contentDirectory,
                contentEc))
        {
            continue;
        }

        std::string provider =
            moduleEntry.path().filename().string();

        for (auto const& entry :
             std::filesystem::directory_iterator(
                 contentDirectory,
                 contentEc))
        {
            if (contentEc)
                break;

            if (!entry.is_regular_file())
                continue;

            std::filesystem::path path = entry.path();

            std::string extension =
                path.extension().string();

            std::transform(
                extension.begin(),
                extension.end(),
                extension.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(
                        std::tolower(c));
                });

            if (extension != ".epf")
                continue;

            packages.push_back(
            {
                path,
                path.filename().string(),
                ContentPackageSource::Module,
                provider
            });
        }
    }

    std::sort(
        packages.begin(),
        packages.end(),
        [](ContentPackageCandidate const& a,
           ContentPackageCandidate const& b)
        {
            if (a.provider != b.provider)
                return a.provider < b.provider;

            return a.filename < b.filename;
        });

    return packages;
}
