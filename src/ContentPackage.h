#ifndef CONTENT_PACKAGE_H
#define CONTENT_PACKAGE_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ContentPackageEntry
{
    std::string type;
    std::string source;
    std::string target;
};

struct ContentPackageManifest
{
    uint32_t schema = 0;
    std::string packageKey;
    std::string name;
    std::string version;
    std::string description;

    std::vector<ContentPackageEntry> content;
};

struct ContentPackageStageResult
{
    bool success = false;
    std::string error;
    std::filesystem::path stagingDirectory;
    std::vector<std::filesystem::path> stagedFiles;
};

struct ContentPackageValidationResult
{
    bool valid = false;
    std::string error;
    ContentPackageManifest manifest;
};

class ContentPackage
{
public:
    explicit ContentPackage(std::filesystem::path path);

    ContentPackageValidationResult Validate() const;

    ContentPackageStageResult Stage(
        std::filesystem::path const& workDirectory) const;

    // Append only declared files to an existing owned workspace. Never clears
    // directories or overwrites files; revalidates against the preflight manifest.
    ContentPackageStageResult StageInto(
        std::filesystem::path const& stagingDirectory,
        ContentPackageManifest const& expected) const;

private:
    std::filesystem::path _path;
};

#endif
