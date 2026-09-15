#include "ContentBuildRegistry.h"

#include "DatabaseEnv.h"
#include "Transaction.h"
#include "ContentBuildHash.h"
#include "ContentBuildPaths.h"
#include <mutex>
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
        "COUNT(filename), COUNT(package_count), COUNT(file_count), COUNT(created_at), COUNT(state), COUNT(sha256) FROM content_manager_build");
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
        || record.filename.empty() || record.filename.size() > 255 || !record.packageCount || !record.fileCount
        || record.state != "STAGED" || !ContentBuildHash::Valid(record.sha256))
    {
        error = "Invalid completed build metadata";
        return false;
    }
    WorldDatabase.DirectExecute("INSERT INTO content_manager_build "
        "(build_number, realm_name, filename, package_count, file_count, state, sha256) VALUES ("
        + std::to_string(record.buildNumber) + "," + SqlText(record.realmName) + ","
        + SqlText(record.filename) + "," + std::to_string(record.packageCount) + ","
        + std::to_string(record.fileCount) + ",'STAGED'," + SqlText(record.sha256) + ")");
    // AzerothCore's synchronous DirectExecute has no return value; verify the row.
    auto query = WorldDatabase.Query("SELECT realm_name, filename, package_count, file_count, state, sha256 "
        "FROM content_manager_build WHERE build_number = " + std::to_string(record.buildNumber));
    if (!query)
    {
        error = DatabaseError;
        return false;
    }
    auto fields = query->Fetch();
    if (fields[0].Get<std::string>() != record.realmName || fields[1].Get<std::string>() != record.filename
        || fields[2].Get<uint32>() != record.packageCount || fields[3].Get<uint32>() != record.fileCount
        || fields[4].Get<std::string>() != "STAGED" || fields[5].Get<std::string>() != record.sha256)
    {
        error = "Build number already belongs to different metadata; completed MPQ was preserved";
        return false;
    }
    return true;
}

namespace
{
std::mutex lifecycleMutex;
std::filesystem::path Artifact(ContentBuildRecord const& record, std::filesystem::path const& root)
{
    using namespace ContentBuildPaths;
    Require(!root.empty(), "OutputDirectory must not be empty");
    auto name = Target(record.filename);
    Require(name.find('/') == std::string::npos && name == record.filename,
        "Build filename must be a single safe path component");
    return fs::absolute(root / name);
}
}

bool ContentBuildRegistry::GetBuilds(std::vector<ContentBuildRecord>& records, std::string& error) const
{
    records.clear();
    // LEFT JOIN guarantees an empty registry still returns a row, unlike a failed query.
    auto query = WorldDatabase.Query("SELECT b.build_number, b.realm_name, b.filename, b.package_count, "
        "b.file_count, b.state, b.sha256 FROM (SELECT 1) AS seed LEFT JOIN content_manager_build b ON 1=1 "
        "ORDER BY b.build_number DESC");
    if (!query) { error = DatabaseError; return false; }
    do
    {
        auto f = query->Fetch();
        if (!f[0].IsNull())
        {
            auto hash = f[6].Get<std::string>();
            if (!ContentBuildHash::Valid(hash))
            {
                error = "Build " + std::to_string(f[0].Get<uint32>()) + " has an invalid SHA256; check build registry integrity";
                records.clear();
                return false;
            }
            records.push_back({f[0].Get<uint32>(), f[1].Get<std::string>(), f[2].Get<std::string>(),
                f[3].Get<uint32>(), f[4].Get<uint32>(), f[5].Get<std::string>(), hash});
        }
    } while (query->NextRow());
    return true;
}

bool ContentBuildRegistry::GetBuild(std::uint32_t number, std::optional<ContentBuildRecord>& record, std::string& error) const
{
    record.reset();
    std::vector<ContentBuildRecord> records;
    if (!GetBuilds(records, error)) return false;
    for (auto const& row : records)
        if (row.buildNumber == number) record = row;
    return true;
}

bool ContentBuildRegistry::GetActiveBuild(std::optional<ContentBuildRecord>& record, std::string& error) const
{
    record.reset();
    std::vector<ContentBuildRecord> records;
    if (!GetBuilds(records, error)) return false;
    for (auto const& row : records)
        if (row.state == "ACTIVE")
        {
            if (record) { error = "Multiple ACTIVE builds found; repair registry before activation"; return false; }
            record = row;
        }
    return true;
}

bool ContentBuildRegistry::ActivateBuild(std::uint32_t number, std::filesystem::path const& outputDirectory,
    std::filesystem::path const& publishDirectory, ContentPublicationResult& publication,
    bool& alreadyActive, std::string& error) const
{
    std::lock_guard<std::mutex> lock(lifecycleMutex);
    alreadyActive = false;
    publication = {};
    try
    {
        using ContentBuildPaths::Require;
        std::optional<ContentBuildRecord> record;
        if (!GetBuild(number, record, error)) return false;
        Require(record.has_value(), "Build does not exist");
        Require(ContentBuildHash::Valid(record->sha256), "Build record has an invalid SHA256; activation refused");
        Require(record->state == "STAGED" || record->state == "ACTIVE" || record->state == "SUPERSEDED", "Unknown build state");
        publication = ContentBuildPublisher().Publish(Artifact(*record, outputDirectory), publishDirectory, record->sha256);
        if (!publication.success)
        {
            error = "Publication failed; build states unchanged: " + publication.error;
            return false;
        }
        auto const& actual = publication.sha256;
        if (record->state == "ACTIVE") { alreadyActive = true; return true; }
        auto transaction = WorldDatabase.BeginTransaction();
        // This singleton write serializes lifecycle transactions across worldserver processes.
        // INSERT also restores the lock row if an administrator removed it.
        transaction->Append("INSERT INTO content_manager_build_lock (id) VALUES (1) ON DUPLICATE KEY UPDATE id=1");
        // Join to the verified target so a removed/changed target cannot demote the active build.
        auto predicate = "target.build_number=" + std::to_string(number) + " AND target.sha256=" + SqlText(actual)
            + " AND target.filename=" + SqlText(record->filename) + " AND target.state IN ('STAGED','SUPERSEDED','ACTIVE')";
        transaction->Append("UPDATE content_manager_build old JOIN content_manager_build target ON " + predicate
            + " SET old.state='SUPERSEDED' WHERE old.state='ACTIVE' AND old.build_number<>target.build_number");
        transaction->Append("UPDATE content_manager_build target SET target.state='ACTIVE' WHERE " + predicate);
        WorldDatabase.DirectCommitTransaction(transaction);
        std::optional<ContentBuildRecord> active;
        if (!GetActiveBuild(active, error)) return false;
        Require(active && active->buildNumber == number && active->sha256 == actual,
            "Activation could not be verified; check SQL logs and .content build list (another administrator may have activated a build)");
        return true;
    }
    catch (std::exception const& exception) { error = exception.what(); return false; }
}
