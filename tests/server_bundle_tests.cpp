#include "ContentServerBundle.h"
#include "ContentServerOwnership.h"
#include "ContentBuildHash.h"
#include "ServerTableDescriptor.h"
#include <cassert>
#include <string>

// The production hash helper is file oriented; only its format check is needed here.
bool ContentBuildHash::Valid(std::string const& hash)
{
    return hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == std::string::npos;
}

int main()
{
    std::string hash(64, 'a');
    ItemAllocation lease{"Eitrigg", "mod-hunts", "seal", 56807, "reserved", 1, 7, hash};
    ContentItemRow client;
    client.symbol = "seal";
    client.classID = 15;
    client.subclassID = 0;
    client.soundOverrideSubclassID = -1;
    client.material = -1;
    client.inventoryType = 0;
    client.sheatheType = 0;
    ContentServerItemRow server;
    server.symbol = "seal";
    server.name = "Huntmaster's Seal";
    server.description = "A token issued by the Huntmasters.";
    server.quality = 1;
    server.stackable = 200;
    server.bonding = 0;
    server.bagFamily = 0;
    ResolvedServerItem row{"mod-hunts", "3.0.0", "seal", lease.value, 6418, client, server};
    auto bundle = ContentServerBundle::ServerJson("Eitrigg", {row});
    assert(bundle == ContentServerBundle::ServerJson("Eitrigg", {row}));
    std::vector<ResolvedServerItem> parsed;
    std::string error;
    assert(ContentServerBundle::ParseServer(bundle, "Eitrigg", parsed, error));
    assert(parsed.size() == 1 && parsed[0].id == lease.value && parsed[0].server.name == server.name);
    auto parity = ContentServerBundle::ParityJson("Eitrigg", 7, {lease}, parsed, hash, hash, hash, hash);
    assert(parity == ContentServerBundle::ParityJson("Eitrigg", 7, {lease}, parsed, hash, hash, hash, hash));
    assert(ContentServerBundle::VerifyParity(parity, "Eitrigg", 7, hash, hash, hash,
        parsed, {lease}, error));
    auto wrong = parity;
    auto position = wrong.find("56807");
    assert(position != std::string::npos);
    wrong.replace(position, 5, "56808");
    assert(!ContentServerBundle::VerifyParity(wrong, "Eitrigg", 7, hash, hash, hash,
        parsed, {lease}, error));
    assert(!ContentServerBundle::VerifyParity(parity, "Eitrigg", 8, hash, hash, hash,
        parsed, {lease}, error)); // Another build cannot claim this manifest.
    std::string anotherHash(64, 'b');
    assert(!ContentServerBundle::VerifyParity(parity, "Eitrigg", 7, hash, hash, anotherHash,
        parsed, {lease}, error)); // Wrong recorded server artifact hash.
    auto wrongServerId = parsed;
    wrongServerId[0].id = 56808;
    assert(!ContentServerBundle::VerifyParity(parity, "Eitrigg", 7, hash, hash, hash,
        wrongServerId, {lease}, error)); // Client/server ID mismatch.
    auto sql = ContentServerBundle::InsertSql(row);
    assert(sql.find("56807") != std::string::npos);
    assert(sql.find("Huntmaster's Seal") == std::string::npos); // SQL text is hex encoded.
    assert(sql.find("CONVERT(X'") != std::string::npos);
    assert(sql.find("REPLACE") == std::string::npos);
    assert(sql == ContentServerBundle::InsertSql(row)); // Stable generated SQL.
    auto const* descriptor = FindServerTableDescriptor("item_template");
    assert(descriptor);
    for (auto const& column : descriptor->columns)
        if (std::string(column.name) == "name" || std::string(column.name) == "description")
            assert(std::string(column.collation) == "utf8mb4_unicode_ci");
    assert(sql.find("USING utf8mb4) COLLATE utf8mb4_unicode_ci") != std::string::npos);
    assert(sql.find("utf8mb4_0900_ai_ci") == std::string::npos);
    auto update = ContentServerBundle::UpdateSql(row, ContentServerBundle::RowJson(row));
    assert(update.find("WHERE `entry`=56807") != std::string::npos);
    assert(update.find(" AND `name`=") != std::string::npos); // Optimistic drift guard.
    assert(update == ContentServerBundle::UpdateSql(row, ContentServerBundle::RowJson(row)));
    assert(update.find(" AND `name`=CONVERT(X'") != std::string::npos);
    assert(update.find(" COLLATE utf8mb4_unicode_ci") != std::string::npos);
    assert(update.find("utf8mb4_0900_ai_ci") == std::string::npos);
    auto match = ContentServerBundle::MatchSql(row, "t");
    assert(match.find("t.`name`=CONVERT(X'") != std::string::npos);
    assert(match.find("t.`description`=CONVERT(X'") != std::string::npos);
    assert(match.find(" COLLATE utf8mb4_unicode_ci") != std::string::npos);
    assert(match.find("utf8mb4_0900_ai_ci") == std::string::npos);
    assert(match == ContentServerBundle::MatchSql(row, "t")); // Used by convergence and post-apply guards.
    auto identity = ContentServerBundle::SqlIdentityText("mod-hunts' seal");
    assert(identity.find(" COLLATE utf8mb4_bin") != std::string::npos);
    assert(identity.find("mod-hunts' seal") == std::string::npos);
    assert(identity.find("utf8mb4_unicode_ci") == std::string::npos);

    ContentItemOwner owner{"Eitrigg", "mod-hunts", "seal", "item.id", 7, hash,
        ContentServerBundle::RowJson(row)};
    using Decision = ContentItemOwnershipAction;
    assert(ContentServerOwnership::Classify(false, false, {}, "Eitrigg", "mod-hunts", "seal", "")
        == Decision::Insert);
    assert(ContentServerOwnership::Classify(true, true, owner, "Eitrigg", "mod-hunts", "seal",
        owner.rowJson) == Decision::Converge);
    assert(ContentServerOwnership::Classify(true, false, {}, "Eitrigg", "mod-hunts", "seal",
        owner.rowJson) == Decision::Conflict); // Retained lease is not row ownership.
    assert(ContentServerOwnership::Classify(false, true, owner, "Eitrigg", "mod-hunts", "seal",
        "") == Decision::Conflict); // Orphaned provenance is not proof of a row.
    assert(ContentServerOwnership::Classify(true, true, owner, "Eitrigg", "other", "seal",
        owner.rowJson) == Decision::Conflict);
    assert(ContentServerOwnership::Classify(true, true, owner, "Eitrigg", "mod-hunts", "seal",
        "drift") == Decision::Conflict);
}
