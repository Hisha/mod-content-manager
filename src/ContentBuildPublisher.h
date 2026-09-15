#ifndef CONTENT_BUILD_PUBLISHER_H
#define CONTENT_BUILD_PUBLISHER_H

#include <filesystem>
#include <string>

struct ContentPublicationResult
{
    bool success = false;
    bool reused = false;
    std::filesystem::path path;
    std::string sha256;
    std::string error;
};

class ContentBuildPublisher
{
public:
    // Verifies the source, copies privately, verifies, promotes atomically, and
    // verifies the final artifact. Never changes build state or deletes a source.
    ContentPublicationResult Publish(std::filesystem::path const& source,
        std::filesystem::path const& publishDirectory, std::string const& expectedHash) const;
};
#endif
