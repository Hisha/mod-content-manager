#ifndef CONTENT_PACKAGE_REGISTRY_H
#define CONTENT_PACKAGE_REGISTRY_H

#include <string>
#include <vector>

struct InstalledContentPackage
{
    std::string packageKey;
    std::string name;
    std::string version;
    std::string provider;
    std::string sourcePath;
    std::string installedAt;
};

struct PackageRegistryResult
{
    bool success = false;
    std::string error;
};

struct PackageInstalledResult : PackageRegistryResult
{
    bool installed = false;
};

struct InstalledPackagesResult : PackageRegistryResult
{
    // Rows are retained here even when their source EPFs no longer exist.
    std::vector<InstalledContentPackage> packages;
};

struct PackageRegistryChangeResult : PackageRegistryResult
{
    bool changed = false;
};

class ContentPackageRegistry
{
public:
    PackageInstalledResult IsInstalled(std::string const& packageKey) const;
    InstalledPackagesResult GetInstalledPackages() const;
    PackageRegistryChangeResult Install(InstalledContentPackage const& package) const;
    PackageRegistryChangeResult Uninstall(std::string const& packageKey) const;
};

#endif
