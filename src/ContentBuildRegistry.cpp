#include "ContentBuildRegistry.h"

#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"

#include <limits>

namespace
{
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
char const* DatabaseError = "Build registry database operation failed. Apply module world SQL updates and check the SQL error log.";
}

bool ContentBuildRegistry::NextNumber(std::uint32_t& number, std::string& error) const
{
    // Aggregate returns a row for an empty table. Referencing every column also
    // catches an incompatible schema before any MPQ is created.
    auto query = WorldDatabase.Query("SELECT COALESCE(MAX(build_number), 0), COUNT(realm_name), "
        "COUNT(filename), COUNT(package_count), COUNT(file_count), COUNT(created_at) FROM content_manager_build");
    if (!query)
    {
        error = DatabaseError;
        return false;
    }
    auto maximum = query->Fetch()[0].Get<uint64>();
    if (maximum >= std::numeric_limits<std::uint32_t>::max())
    {
        error = "Build number range exhausted";
        return false;
    }
    number = static_cast<std::uint32_t>(maximum + 1);
    return true;
}

bool ContentBuildRegistry::Record(ContentBuildRecord const& record, std::string& error) const
{
    if (!record.buildNumber || record.realmName.empty() || record.realmName.size() > 255
        || record.filename.empty() || record.filename.size() > 255 || !record.packageCount || !record.fileCount)
    {
        error = "Invalid completed build metadata";
        return false;
    }
    WorldDatabase.DirectExecute("INSERT INTO content_manager_build "
        "(build_number, realm_name, filename, package_count, file_count) VALUES ("
        + std::to_string(record.buildNumber) + "," + SqlText(record.realmName) + ","
        + SqlText(record.filename) + "," + std::to_string(record.packageCount) + ","
        + std::to_string(record.fileCount) + ")");
    // AzerothCore's synchronous DirectExecute has no return value; verify the row.
    auto query = WorldDatabase.Query("SELECT realm_name, filename, package_count, file_count "
        "FROM content_manager_build WHERE build_number = " + std::to_string(record.buildNumber));
    if (!query)
    {
        error = DatabaseError;
        return false;
    }
    auto fields = query->Fetch();
    if (fields[0].Get<std::string>() != record.realmName || fields[1].Get<std::string>() != record.filename
        || fields[2].Get<uint32>() != record.packageCount || fields[3].Get<uint32>() != record.fileCount)
    {
        error = "Build number already belongs to different metadata; completed MPQ was preserved";
        return false;
    }
    return true;
}
