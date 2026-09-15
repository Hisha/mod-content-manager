#ifndef CONTENT_BUILD_REGISTRY_H
#define CONTENT_BUILD_REGISTRY_H

#include <cstdint>
#include <string>
#include <optional>
#include <vector>
#include <filesystem>

struct ContentBuildRecord
{
    std::uint32_t buildNumber = 0;
    std::string realmName;
    std::string filename;
    std::uint32_t packageCount = 0;
    std::uint32_t fileCount = 0;
    std::string state = "STAGED";
    std::string sha256;
};

class ContentBuildRegistry
{
public:
    bool GetBuilds(std::vector<ContentBuildRecord>& records, std::string& error) const;
    bool GetBuild(std::uint32_t number, std::optional<ContentBuildRecord>& record, std::string& error) const;
    bool GetActiveBuild(std::optional<ContentBuildRecord>& record, std::string& error) const;
    bool ActivateBuild(std::uint32_t number, std::filesystem::path const& outputDirectory,
        bool& alreadyActive, std::string& error) const;
    bool NextNumber(std::uint32_t& number, std::string& error) const;
    // Called only after the MPQ is closed and published successfully.
    bool Record(ContentBuildRecord const& record, std::string& error) const;
};
#endif
