#include "ContentPackageRegistry.h"

#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"

#include <algorithm>
#include <mutex>

namespace
{
// Serialize this module's read/write/verification sequences within the worldserver.
std::mutex registryWrites;

// Hex literals keep metadata out of SQL syntax, independent of quote/backslash SQL modes.
std::string SqlText(std::string const& value)
{
    static char const digits[] = "0123456789abcdef";
    std::string sql = "CONVERT(X'";
    for (unsigned char byte : value)
    {
        sql += digits[byte >> 4];
        sql += digits[byte & 15];
    }
    return sql + "' USING utf8mb4)";
}

bool ValidKey(std::string const& key)
{
    return !key.empty() && key.size() <= 191 && key.back() != ' '
        && key.find('\0') == std::string::npos;
}

char const* DatabaseError = "Package registry database operation failed. Apply the module world SQL updates and check the SQL error log.";
}

PackageInstalledResult ContentPackageRegistry::IsInstalled(std::string const& packageKey) const
{
    PackageInstalledResult result;
    if (!ValidKey(packageKey))
    {
        result.error = "Package key must be 1-191 UTF-8 bytes, without NUL or a trailing space.";
        return result;
    }
    // EXISTS returns a row even for an absent key, distinguishing absence from query failure.
    auto query = WorldDatabase.Query("SELECT EXISTS(SELECT 1 FROM content_manager_package WHERE package_key = "
        + SqlText(packageKey) + ")");
    if (!query)
    {
        result.error = DatabaseError;
        return result;
    }
    result.installed = query->Fetch()[0].Get<uint32>() != 0;
    result.success = true;
    return result;
}

InstalledPackagesResult ContentPackageRegistry::GetInstalledPackages() const
{
    InstalledPackagesResult result;
    // The sentinel guarantees a nonempty result even when the registry is empty.
    // A null QueryResult therefore means an error, never "nothing installed".
    auto query = WorldDatabase.Query(
        "SELECT 1 AS is_package, package_key, name, version, provider, source_path, "
        "CAST(installed_at AS CHAR) FROM content_manager_package "
        "UNION ALL SELECT 0, '', '', '', '', '', ''");
    if (!query)
    {
        result.error = DatabaseError;
        return result;
    }
    do
    {
        auto fields = query->Fetch();
        if (fields[0].Get<uint32>() == 0)
            continue;
        result.packages.push_back({fields[1].Get<std::string>(), fields[2].Get<std::string>(),
            fields[3].Get<std::string>(), fields[4].Get<std::string>(),
            fields[5].Get<std::string>(), fields[6].Get<std::string>()});
    } while (query->NextRow());
    std::sort(result.packages.begin(), result.packages.end(),
        [](InstalledContentPackage const& a, InstalledContentPackage const& b) { return a.packageKey < b.packageKey; });
    result.success = true;
    return result;
}

PackageRegistryChangeResult ContentPackageRegistry::Install(InstalledContentPackage const& package) const
{
    std::lock_guard<std::mutex> lock(registryWrites);
    PackageRegistryChangeResult result;
    auto existing = IsInstalled(package.packageKey);
    if (!existing.success)
    {
        result.error = existing.error;
        return result;
    }
    if (existing.installed)
    {
        result.success = true;
        return result;
    }
    // Conservative byte limits prevent silent truncation under permissive SQL modes.
    if (package.name.empty() || package.name.size() > 255 || package.version.empty()
        || package.version.size() > 64 || package.provider.empty() || package.provider.size() > 255
        || package.sourcePath.empty() || package.sourcePath.size() > 65535)
    {
        result.error = "Package metadata is empty or exceeds registry limits (name/provider 255, version 64, source path 65535 UTF-8 bytes).";
        return result;
    }
    WorldDatabase.DirectExecute(
        "INSERT INTO content_manager_package (package_key, name, version, provider, source_path) VALUES ("
        + SqlText(package.packageKey) + "," + SqlText(package.name) + "," + SqlText(package.version)
        + "," + SqlText(package.provider) + "," + SqlText(package.sourcePath)
        + ") ON DUPLICATE KEY UPDATE package_key = package_key");

    // DirectExecute returns void in AzerothCore. Verify persisted metadata synchronously;
    // never claim success merely because a write was submitted. Duplicate keys never update versions.
    auto after = GetInstalledPackages();
    if (!after.success)
    {
        result.error = after.error;
        return result;
    }
    auto found = std::find_if(after.packages.begin(), after.packages.end(),
        [&](InstalledContentPackage const& row) { return row.packageKey == package.packageKey; });
    if (found == after.packages.end() || found->name != package.name || found->version != package.version
        || found->provider != package.provider || found->sourcePath != package.sourcePath)
    {
        result.error = "Could not verify installed metadata; the write failed or another writer changed the registry. Check .content scan and the SQL error log.";
        return result;
    }
    result.success = true;
    result.changed = true;
    return result;
}

PackageRegistryChangeResult ContentPackageRegistry::Uninstall(std::string const& packageKey) const
{
    std::lock_guard<std::mutex> lock(registryWrites);
    PackageRegistryChangeResult result;
    auto before = IsInstalled(packageKey);
    if (!before.success)
    {
        result.error = before.error;
        return result;
    }
    if (!before.installed)
    {
        result.success = true;
        return result;
    }
    WorldDatabase.DirectExecute("DELETE FROM content_manager_package WHERE package_key = " + SqlText(packageKey));
    auto after = IsInstalled(packageKey);
    if (!after.success || after.installed)
    {
        result.error = after.success ? DatabaseError : after.error;
        return result;
    }
    result.success = true;
    result.changed = true;
    return result;
}

