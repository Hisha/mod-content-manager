#ifndef CONTENT_BUILD_REGISTRY_H
#define CONTENT_BUILD_REGISTRY_H

#include <cstdint>
#include <string>

struct ContentBuildRecord
{
    std::uint32_t buildNumber = 0;
    std::string realmName;
    std::string filename;
    std::uint32_t packageCount = 0;
    std::uint32_t fileCount = 0;
};

class ContentBuildRegistry
{
public:
    bool NextNumber(std::uint32_t& number, std::string& error) const;
    // Called only after the MPQ is closed and published successfully.
    bool Record(ContentBuildRecord const& record, std::string& error) const;
};
#endif
