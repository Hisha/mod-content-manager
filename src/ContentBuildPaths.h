#ifndef CONTENT_BUILD_PATHS_H
#define CONTENT_BUILD_PATHS_H

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

namespace ContentBuildPaths
{
namespace fs = std::filesystem;
inline void Require(bool condition, std::string const& message)
{
    if (!condition)
        throw std::runtime_error(message);
}
inline bool IsBeneath(fs::path const& path, fs::path const& root)
{
    auto relative = path.lexically_relative(root);
    return !relative.empty() && relative != "." && !relative.is_absolute() && *relative.begin() != "..";
}
inline void RejectLinks(fs::path const& path)
{
    fs::path prefix;
    for (auto const& component : fs::absolute(path))
    {
        prefix /= component;
        std::error_code ec;
        auto status = fs::symlink_status(prefix, ec);
        Require(!ec || ec == std::errc::no_such_file_or_directory,
            "Could not inspect path '" + prefix.string() + "': " + ec.message());
        Require(!fs::is_symlink(status), "Symlink is not allowed: " + prefix.string());
    }
}
inline std::string Fold(std::string value)
{
    for (char& c : value)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return value;
}
// Use one portable namespace for filesystem paths and MPQ names. This also
// rejects Windows drive/ADS names and aliases that would hide collisions.
inline std::string Target(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');
    Require(!value.empty() && value.front() != '/' && value.back() != '/', "Unsafe target: " + value);
    std::size_t start = 0;
    while (start < value.size())
    {
        auto end = value.find('/', start);
        auto component = value.substr(start, end == std::string::npos ? end : end - start);
        Require(!component.empty() && component != "." && component != ".."
            && component.back() != '.' && component.back() != ' ', "Unsafe target: " + value);
        for (unsigned char c : component)
            Require(c >= 32 && c < 127 && std::string("<>:\"|?*").find(c) == std::string::npos,
                "Target must use portable ASCII path characters: " + value);
        auto stem = Fold(component.substr(0, component.find('.')));
        Require(stem != "CON" && stem != "PRN" && stem != "AUX" && stem != "NUL"
            && !(stem.size() == 4 && (stem.substr(0, 3) == "COM" || stem.substr(0, 3) == "LPT")
                && stem[3] >= '1' && stem[3] <= '9'), "Reserved target name: " + value);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return value;
}
inline bool Cleanup(fs::path const& workspace, fs::path const& workDirectory, std::string& error)
{
    try
    {
        Require(!workspace.empty() && !workDirectory.empty(), "Cleanup paths must not be empty");
        RejectLinks(workspace);
        auto root = fs::canonical(workDirectory);
        auto owned = fs::canonical(workspace);
        Require(IsBeneath(owned, root), "Refusing cleanup outside or at WorkDirectory");
        Require(fs::is_directory(owned), "Cleanup path is not a directory");
        std::error_code ec;
        fs::remove_all(owned, ec);
        if (ec)
            throw std::runtime_error(ec.message());
        return true;
    }
    catch (std::exception const& exception)
    {
        error = exception.what();
        return false;
    }
}
}
#endif
