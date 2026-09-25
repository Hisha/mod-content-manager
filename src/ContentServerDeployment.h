#ifndef CONTENT_SERVER_DEPLOYMENT_H
#define CONTENT_SERVER_DEPLOYMENT_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
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
};
#endif
