#ifndef CONTENT_BUILD_REGISTRY_H
#define CONTENT_BUILD_REGISTRY_H

#include "ContentBuildPublisher.h"

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
struct ContentServerBuildRecord;

class ContentBuildRegistry
{
public:
    bool GetBuilds(std::vector<ContentBuildRecord>& records, std::string& error) const;
    bool GetBuild(std::uint32_t number, std::optional<ContentBuildRecord>& record, std::string& error) const;
    bool GetActiveBuild(std::optional<ContentBuildRecord>& record, std::string& error) const;
    bool ActivateBuild(std::uint32_t number, std::filesystem::path const& outputDirectory,
        std::filesystem::path const& publishDirectory, ContentPublicationResult& publication,
        bool& alreadyActive, std::string& error) const;
    bool NextNumber(std::uint32_t& number, std::string& error) const;
    // Called only after the private output MPQ is closed and hashed successfully.
    bool Record(ContentBuildRecord const& record, ContentServerBuildRecord const& server,
        std::string& error) const;
};
#endif
