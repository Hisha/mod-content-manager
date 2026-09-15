#ifndef CONTENT_BUILD_HASH_H
#define CONTENT_BUILD_HASH_H
#include <filesystem>
#include <string>
namespace ContentBuildHash
{
bool Valid(std::string const& hash);
bool Calculate(std::filesystem::path const& path, std::string& hash, std::string& error);
}
#endif
