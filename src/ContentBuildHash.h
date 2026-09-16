#ifndef CONTENT_BUILD_HASH_H
#define CONTENT_BUILD_HASH_H
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
namespace ContentBuildHash
{
std::string Bytes(std::vector<std::uint8_t> const& bytes);
bool Valid(std::string const& hash);
bool Calculate(std::filesystem::path const& path, std::string& hash, std::string& error);
}
#endif
