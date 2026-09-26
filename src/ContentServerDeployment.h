#ifndef CONTENT_SERVER_DEPLOYMENT_H
#define CONTENT_SERVER_DEPLOYMENT_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "ContentBuildPublisher.h"
#include "ContentServerBundle.h"

struct ContentServerStatus
{
    std::uint32_t build = 0;
    std::string state;
    std::string bundleFilename;
    std::string bundleSha256;
    std::string parityFilename;
    std::string paritySha256;
};

struct ContentActivationResult
{
    bool hasManagedServerContent = false;
    bool serverAppliedNow = false;
    bool serverAlreadyApplied = false;
    std::string serverSummary;
};

class ContentServerDeployment
{
public:
    static bool ReadStatus(std::uint32_t build, bool& exists, ContentServerStatus& status,
        std::string& error);
    static bool Inspect(std::uint32_t build, std::string const& realm,
        std::filesystem::path const& outputDirectory, ContentServerStatus& status,
        std::vector<ResolvedServerItem>& rows, std::string& error,
        std::vector<ResolvedExtendedCost>* costs = nullptr, std::vector<ResolvedVendorRow>* vendors = nullptr,
        std::vector<ResolvedCreatureTemplate>* creatures = nullptr,
        std::vector<ResolvedGameObjectTemplate>* gameObjects = nullptr,
        std::vector<ResolvedCreatureSpawn>* spawns = nullptr);
    static bool Apply(std::uint32_t build, std::string const& realm,
        std::filesystem::path const& outputDirectory, std::string& summary, std::string& error);
    // Activation orchestration only: managed server content is applied or
    // verified before the existing client publication/lifecycle path runs.
    static bool Activate(std::uint32_t build, std::string const& realm,
        std::filesystem::path const& outputDirectory,
        std::filesystem::path const& publishDirectory,
        ContentActivationResult& result, ContentPublicationResult& publication,
        bool& alreadyActive, std::string& error);
};
#endif
