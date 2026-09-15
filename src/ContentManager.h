#ifndef CONTENT_MANAGER_H
#define CONTENT_MANAGER_H

#include <filesystem>
#include <string>
#include <vector>

enum class ContentPackageSource
{
    PatchHold,
    Module
};

struct ContentPackageCandidate
{
    std::filesystem::path path;
    std::string filename;

    ContentPackageSource source;
    std::string provider;
};

class ContentManager
{
public:
    static ContentManager& Instance();

    void LoadConfig();

    bool IsEnabled() const;

    std::string const& GetPatchHoldDirectory() const;
    std::string const& GetWorkDirectory() const;
    std::string const& GetOutputDirectory() const;
    std::string const& GetPublishDirectory() const;
	std::string const& GetModuleDirectory() const;

	std::vector<ContentPackageCandidate> ScanAvailablePackages() const;
    std::vector<ContentPackageCandidate> ScanPatchHold() const;

private:
    ContentManager() = default;

    bool _enabled = false;

    std::string _patchHoldDirectory;
    std::string _workDirectory;
    std::string _outputDirectory;
    std::string _publishDirectory;
	std::string _moduleDirectory;
};

#define sContentManager ContentManager::Instance()

#endif