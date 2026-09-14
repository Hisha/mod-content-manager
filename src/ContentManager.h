#ifndef CONTENT_MANAGER_H
#define CONTENT_MANAGER_H

#include <filesystem>
#include <string>
#include <vector>

struct ContentPackageCandidate
{
    std::filesystem::path path;
    std::string filename;
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

    std::vector<ContentPackageCandidate> ScanPatchHold() const;

private:
    ContentManager() = default;

    bool _enabled = false;

    std::string _patchHoldDirectory;
    std::string _workDirectory;
    std::string _outputDirectory;
};

#define sContentManager ContentManager::Instance()

#endif