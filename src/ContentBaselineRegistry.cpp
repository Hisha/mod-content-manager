#include "ContentBaselineRegistry.h"
#include "ContentBuildHash.h"
#include "ContentServerBundle.h"
#include "CurrencyDbcComposer.h"
#include "CurrencyCategoryDbcComposer.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include "Transaction.h"
#include <mutex>
#include <stdexcept>

namespace
{
std::mutex writes;
auto T = ContentServerBundle::SqlIdentityText;
std::string Key(ContentBaseline const& b) { return "client_build=" + std::to_string(b.clientBuild) + " AND table_name=" + T(b.table); }
bool Read(ContentBaseline const& b, std::uint64_t& revision, std::string& hash, std::uint32_t& version, std::string& error)
{
    auto engines = WorldDatabase.Query("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() "
        "AND TABLE_NAME IN ('content_manager_baseline','content_manager_baseline_history','content_manager_baseline_review',"
        "'content_manager_build_lock','content_manager_allocation') AND ENGINE='InnoDB'");
    if (!engines || engines->Fetch()[0].Get<std::uint64_t>() != 5)
    { error = "Baseline provenance requires the module registry/lock/allocation tables with InnoDB; apply world SQL"; return false; }
    auto q = WorldDatabase.Query("SELECT b.revision,b.sha256,b.descriptor_version FROM (SELECT 1) seed LEFT JOIN "
        "content_manager_baseline b ON " + Key(b));
    if (!q) { error = "Cannot read baseline registry; apply module world SQL"; return false; }
    auto f = q->Fetch(); revision = f[0].IsNull() ? 0 : f[0].Get<std::uint64_t>();
    hash = revision ? f[1].Get<std::string>() : ""; version = revision ? f[2].Get<std::uint32_t>() : 0;
    return true;
}
bool Pin(ContentBaseline const& b, std::string const& pin, std::string& error)
{
    if (!pin.empty() && (!ContentBuildHash::Valid(pin) || pin != b.hash))
    { error = "Optional explicit SHA-256 pin disagrees with " + b.table + "; registration/approval refused"; return false; }
    return true;
}
std::string Values(ContentBaseline const& b)
{
    return std::to_string(b.clientBuild) + "," + T(b.table) + "," + std::to_string(b.descriptorVersion)
        + "," + T(b.hash) + "," + T(b.source) + "," + std::to_string(b.document.recordCount)
        + "," + std::to_string(b.document.fieldCount) + "," + std::to_string(b.document.recordSize)
        + "," + std::to_string(b.document.stringBlockSize);
}
char const* Columns = "client_build,table_name,descriptor_version,sha256,source_path,record_count,field_count,record_size,string_bytes";
std::string HistorySql(ContentBaseline const& b, std::string const& actor, std::string const& method)
{
    return "INSERT INTO content_manager_baseline_history (" + std::string(Columns)
        + ",revision,actor,acceptance_method) SELECT client_build,table_name,descriptor_version,sha256,source_path,"
        "record_count,field_count,record_size,string_bytes,revision," + T(actor) + "," + T(method)
        + " FROM content_manager_baseline WHERE " + Key(b);
}
}

ContentBaseline ContentBaselineRegistry::Inspect(std::filesystem::path const& directory, DbcDescriptor const& descriptor)
{
    auto read = DbcReader::ReadBaseline(directory, descriptor);
    if (!read.valid) throw std::runtime_error(read.error);
    ContentBaseline b; b.document = std::move(read.document); b.clientBuild = descriptor.clientBuild;
    b.table = descriptor.tableName; b.descriptorVersion = descriptor.version;
    b.source = (std::filesystem::canonical(directory) / descriptor.serverFile).string();
    std::string error;
    if (!ContentBuildHash::Calculate(b.source, b.hash, error)) throw std::runtime_error(error);
    if (b.hash != ContentBuildHash::Bytes(DbcReader::Serialize(b.document)))
        throw std::runtime_error("Baseline changed between validation and hashing");
    if (b.table == "CurrencyTypes") CurrencyDbcComposer::Inspect(b.document);
    if (b.table == "CurrencyCategory") CurrencyCategoryDbcComposer::Inspect(b.document);
    if (b.table == "Item")
    {
        std::set<std::uint32_t> ids;
        for (std::size_t i = 0; i < b.document.recordCount; ++i)
            if (!ids.insert(b.document.words[i * b.document.fieldCount]).second)
                throw std::runtime_error("Duplicate baseline Item ID");
    }
    return b;
}

std::string ContentBaselineRegistry::Condition(ContentBaseline const& b)
{
    return "EXISTS(SELECT 1 FROM content_manager_baseline WHERE " + Key(b) + " AND sha256=" + T(b.hash)
        + " AND descriptor_version=" + std::to_string(b.descriptorVersion) + ")";
}

std::string ContentBaselineRegistry::HistoryCondition(ContentBaseline const& b)
{
    return "EXISTS(SELECT 1 FROM content_manager_baseline_history WHERE " + Key(b) + " AND sha256=" + T(b.hash)
        + " AND descriptor_version=" + std::to_string(b.descriptorVersion) + ")";
}

bool ContentBaselineRegistry::Status(ContentBaseline const& b, std::string& status, std::string& error)
{
    std::uint64_t revision; std::uint32_t version; std::string hash;
    if (!Read(b, revision, hash, version, error)) return false;
    status = !revision ? "UNREGISTERED (first validated build may register automatically)"
        : "revision " + std::to_string(revision) + ", descriptor v" + std::to_string(version) + ", SHA-256 " + hash
            + ((hash == b.hash && version == b.descriptorVersion) ? " MATCH" : " MISMATCH: explicit review/approval required");
    return true;
}

bool ContentBaselineRegistry::Accept(ContentBaseline const& b, std::string const& pin, std::string& error)
{
    std::lock_guard<std::mutex> lock(writes);
    if (!Pin(b, pin, error)) return false;
    std::uint64_t revision; std::uint32_t version; std::string hash;
    if (!Read(b, revision, hash, version, error)) return false;
    if (revision)
    {
        if (hash == b.hash && version == b.descriptorVersion) return true;
        error = b.table + " baseline differs from its accepted identity; use .content dbc inspect/review/approve";
        return false;
    }
    // Existing Phase 2/4 leases are evidence of an established baseline, not permission to adopt a different file.
    auto descriptor = FindDbcDescriptor(b.clientBuild, b.table);
    if (!descriptor || descriptor->version != b.descriptorVersion)
    { error = "Baseline has no matching compiled descriptor"; return false; }
    std::set<std::string> resourceKinds;
    for (auto const& field : descriptor->fields)
        if (field.allocationNamespace) resourceKinds.insert(field.allocationNamespace);
    auto tx = WorldDatabase.BeginTransaction();
    tx->Append("INSERT INTO content_manager_build_lock (id) VALUES (1) ON DUPLICATE KEY UPDATE id=1");
    for (auto const& kind : resourceKinds)
        tx->Append("INSERT INTO content_manager_build_lock (id) SELECT 1 WHERE EXISTS(SELECT 1 FROM content_manager_allocation "
            "WHERE resource_kind=" + T(kind) + " AND baseline_sha256<>" + T(b.hash) + ")");
    tx->Append("INSERT INTO content_manager_baseline (" + std::string(Columns) + ",revision) VALUES (" + Values(b) + ",1)");
    tx->Append(HistorySql(b, "automatic", pin.empty() ? "validated-first-use" : "validated-config-pin"));
    WorldDatabase.DirectCommitTransaction(tx);
    if (!Read(b, revision, hash, version, error)) return false;
    if (!revision || hash != b.hash || version != b.descriptorVersion)
    { error = "Baseline registration failed or conflicts with retained provenance; inspect SQL logs. Existing leases were preserved."; return false; }
    return true;
}

bool ContentBaselineRegistry::Review(ContentBaseline const& b, std::string const& actor, std::uint64_t& review, std::string& error)
{
    std::lock_guard<std::mutex> lock(writes);
    std::uint64_t revision; std::uint32_t version; std::string hash;
    if (!Read(b, revision, hash, version, error)) return false;
    if (!revision) { error = "Baseline is unregistered; first validated build registers it with legacy-provenance checks"; return false; }
    // Bind readback to this exact write even when multiple worldserver processes review concurrently.
    auto tokenQuery = WorldDatabase.Query("SELECT UUID()");
    if (!tokenQuery) { error = "Cannot reserve baseline review token"; return false; }
    auto token = tokenQuery->Fetch()[0].Get<std::string>();
    // The user-facing review number is database-allocated, never a manually calculated fingerprint.
    auto tx = WorldDatabase.BeginTransaction();
    tx->Append("INSERT INTO content_manager_baseline_review (" + std::string(Columns)
        + ",expected_revision,expected_sha256,expected_descriptor_version,reviewed_by,request_token) VALUES (" + Values(b)
        + "," + std::to_string(revision) + "," + T(hash) + "," + std::to_string(version) + "," + T(actor) + "," + T(token) + ")");
    WorldDatabase.DirectCommitTransaction(tx);
    auto q = WorldDatabase.Query("SELECT review_id FROM content_manager_baseline_review WHERE " + Key(b)
        + " AND sha256=" + T(b.hash) + " AND expected_revision=" + std::to_string(revision)
        + " AND descriptor_version=" + std::to_string(b.descriptorVersion) + " AND source_path=" + T(b.source)
        + " AND reviewed_by=" + T(actor) + " AND request_token=" + T(token));
    if (!q || q->Fetch()[0].IsNull()) { error = "Baseline review could not be recorded"; return false; }
    review = q->Fetch()[0].Get<std::uint64_t>(); return true;
}

bool ContentBaselineRegistry::Approve(ContentBaseline const& b, std::string const& pin,
    std::uint64_t review, std::string const& actor, std::string& error)
{
    std::lock_guard<std::mutex> lock(writes);
    if (!Pin(b, pin, error)) return false;
    std::uint64_t acceptedRevision; std::uint32_t acceptedVersion; std::string acceptedHash;
    if (!Read(b, acceptedRevision, acceptedHash, acceptedVersion, error)) return false;
    auto id = std::to_string(review);
    auto unused = WorldDatabase.Query("SELECT COUNT(*) FROM content_manager_baseline_review WHERE review_id=" + id + " AND approved_at IS NULL");
    if (!unused || unused->Fetch()[0].Get<std::uint64_t>() != 1)
    { error = "Baseline review is missing or already consumed; inspect/review again"; return false; }
    auto predicate = "r.review_id=" + id + " AND r.client_build=" + std::to_string(b.clientBuild)
        + " AND r.table_name=" + T(b.table) + " AND r.sha256=" + T(b.hash)
        + " AND r.descriptor_version=" + std::to_string(b.descriptorVersion) + " AND r.source_path=" + T(b.source)
        + " AND r.approved_at IS NULL AND r.expected_revision=b.revision AND r.expected_sha256=b.sha256"
          " AND r.expected_descriptor_version=b.descriptor_version";
    auto tx = WorldDatabase.BeginTransaction();
    tx->Append("INSERT INTO content_manager_build_lock (id) VALUES (1) ON DUPLICATE KEY UPDATE id=1");
    tx->Append("UPDATE content_manager_baseline SET revision=revision WHERE " + Key(b));
    tx->Append("INSERT INTO content_manager_build_lock (id) SELECT 1 WHERE NOT EXISTS(SELECT 1 FROM "
        "content_manager_baseline b JOIN content_manager_baseline_review r ON r.client_build=b.client_build "
        "AND r.table_name=b.table_name WHERE " + predicate + ")");
    tx->Append("UPDATE content_manager_baseline b JOIN content_manager_baseline_review r ON r.client_build=b.client_build "
        "AND r.table_name=b.table_name SET b.sha256=r.sha256,b.descriptor_version=r.descriptor_version,"
        "b.source_path=r.source_path,b.record_count=r.record_count,b.field_count=r.field_count,b.record_size=r.record_size,"
        "b.string_bytes=r.string_bytes,b.revision=b.revision+1,b.accepted_at=NOW() WHERE " + predicate);
    tx->Append(HistorySql(b, actor, "approved-review-" + id));
    tx->Append("UPDATE content_manager_baseline_review SET approved_at=NOW(),approved_by=" + T(actor) + " WHERE review_id=" + id);
    WorldDatabase.DirectCommitTransaction(tx);
    auto q = WorldDatabase.Query("SELECT COUNT(*) FROM content_manager_baseline_review r JOIN content_manager_baseline b "
        "ON b.client_build=r.client_build AND b.table_name=r.table_name WHERE r.review_id=" + id
        + " AND r.approved_at IS NOT NULL AND b.revision=r.expected_revision+1 AND b.sha256=" + T(b.hash)
        + " AND b.descriptor_version=" + std::to_string(b.descriptorVersion));
    if (!q || q->Fetch()[0].Get<std::uint64_t>() != 1)
    { error = "Approval refused: stale review, changed candidate, registry race, or SQL failure; inspect again"; return false; }
    return true;
}

bool ContentBaselineRegistry::History(ContentBaseline const& b, std::set<std::string>& hashes, std::string& error)
{
    hashes.clear();
    auto q = WorldDatabase.Query("SELECT sha256 FROM content_manager_baseline_history WHERE " + Key(b)
        + " AND descriptor_version=" + std::to_string(b.descriptorVersion));
    if (!q) { error = "Accepted baseline history is unavailable"; return false; }
    do { hashes.insert(q->Fetch()[0].Get<std::string>()); } while (q->NextRow());
    return true;
}
