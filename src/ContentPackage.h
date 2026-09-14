#ifndef CONTENT_PACKAGE_H
#define CONTENT_PACKAGE_H

#include <filesystem>
#include <string>

struct ContentPackageManifest
{
    uint32_t schema = 0;
    std::string packageKey;
    std::string name;
    std::string version;
};

struct ContentPackageValidationResult
{
    bool valid = false;
    std::string error;
    ContentPackageManifest manifest;
};

class ContentPackage
{
public:
    explicit ContentPackage(std::filesystem::path path);

    ContentPackageValidationResult Validate() const;

private:
    std::filesystem::path _path;
};

#endif