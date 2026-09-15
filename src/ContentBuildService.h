#ifndef CONTENT_BUILD_SERVICE_H
#define CONTENT_BUILD_SERVICE_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

class ContentManager;
struct ContentBuildResult
{
    bool success = false;
    bool mpqCreated = false;
    bool recorded = false;
    bool cleaned = false;
    std::string error;
    std::string cleanupWarning;
    std::uint32_t buildNumber = 0;
    std::size_t packageCount = 0;
    std::size_t fileCount = 0;
    std::filesystem::path outputPath;
    std::filesystem::path workspace;
};

class ContentBuildService
{
public:
    using Progress = std::function<void(std::string const&)>;
    ContentBuildResult Build(ContentManager const& manager, std::string const& realmName,
        Progress const& progress = {}) const;
    static std::string Number(std::uint32_t number);
    static std::string FilenameRealm(std::string const& realmName);
};
#endif
