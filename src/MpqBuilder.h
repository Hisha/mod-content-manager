#ifndef CONTENT_MANAGER_MPQ_BUILDER_H
#define CONTENT_MANAGER_MPQ_BUILDER_H

#include <cstddef>
#include <filesystem>
#include <string>

struct MpqBuildResult
{
    bool success = false;
    std::string error;
    std::filesystem::path outputPath;
    // Number of input files, excluding StormLib's internal metadata.
    std::size_t fileCount = 0;
};

class MpqBuilder
{
public:
    MpqBuildResult Build(std::filesystem::path const& sourceDirectory,
        std::filesystem::path const& outputMpq) const;
};

#endif
