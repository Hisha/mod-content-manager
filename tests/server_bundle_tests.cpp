#include "ContentServerBundle.h"
#include "ContentServerOwnership.h"
#include "ContentBuildHash.h"
#include "ServerTableDescriptor.h"
#include "third_party/json/json.hpp"
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

    // Spell client/server artifacts share one canonical row and lease.
    ResolvedSpell spell; spell.packageKey="generic";spell.packageVersion="2";
    spell.symbol="informational";spell.profile=SpellDbcComposer::Profile;
    spell.id=80865;spell.copyFrom=19;spell.iconCopyFromSpell=19;
    spell.words[0]=spell.id;spell.words[28]=1;spell.words[40]=21;
    spell.words[46]=1;spell.words[68]=0xFFFFFFFFu;spell.words[71]=6;spell.words[86]=1;
    spell.words[95]=4;spell.words[133]=25;spell.words[225]=1;
    spell.localized[0][0]="Informational";
    assert(SpellDbcComposer::BehaviorMatches(spell));
    ItemAllocation spellLease{"Eitrigg","generic","informational",spell.id,
        "reserved",1,8,hash,"spell.id"};
    auto spellBundle=ContentServerBundle::ServerJson("Eitrigg",{},{},{},{},{},{},{spell});
    std::vector<ResolvedSpell> parsedSpells;
    assert(ContentServerBundle::ParseServer(spellBundle,"Eitrigg",parsed,error,
        nullptr,nullptr,nullptr,nullptr,nullptr,&parsedSpells));
    assert(parsed.empty()&&parsedSpells.size()==1&&parsedSpells[0].words==spell.words);
    ContentBaseline spellBaseline;spellBaseline.table="Spell";
    spellBaseline.clientBuild=12340;spellBaseline.descriptorVersion=1;spellBaseline.hash=hash;
    auto spellParity=ContentServerBundle::ParityJson("Eitrigg",8,{spellLease},{},
        "","",hash,hash,"","",{},{spellBaseline},"",{},{},{},{},{},{spell},hash);
    assert(ContentServerBundle::VerifyParity(spellParity,"Eitrigg",8,"",hash,hash,
        {},{spellLease},error,{},{},{},{},{},{spell}));
	// Historical canonical bundles remain readable even when their Spell row no
	// longer satisfies the current composer policy.
	auto historical = spell;
	historical.packageVersion = "1";
	historical.words[68] = 0;
	assert(!SpellDbcComposer::BehaviorMatches(historical));
	bool rejectedHistoricalGeneration = false;
	try {
		(void)ContentServerBundle::ServerJson(
			"Eitrigg", {}, {}, {}, {}, {}, {}, {historical});
	} catch (std::exception const&) {
		rejectedHistoricalGeneration = true;
	}
	assert(rejectedHistoricalGeneration);
	auto historicalBundleObject = nlohmann::json::parse(spellBundle);
	historicalBundleObject["spells"][0]["packageVersion"] = "1";
	historicalBundleObject["spells"][0]["words"][68] = 0;
	auto historicalBundle = historicalBundleObject.dump(2) + "\n";
	parsedSpells.clear();
	assert(ContentServerBundle::ParseServer(historicalBundle, "Eitrigg", parsed,
		error, nullptr, nullptr, nullptr, nullptr, nullptr, &parsedSpells));
	assert(parsedSpells.size() == 1 && parsedSpells[0].id == spell.id &&
		parsedSpells[0].words[68] == 0);
	auto historicalParityObject = nlohmann::json::parse(spellParity);
	historicalParityObject["spells"][0]["packageVersion"] = "1";
	historicalParityObject["spells"][0]["words"][68] = 0;
	auto historicalParity = historicalParityObject.dump(2) + "\n";
	assert(ContentServerBundle::VerifyParity(historicalParity, "Eitrigg", 8,
		"", hash, hash, {}, {spellLease}, error, {}, {}, {}, {}, {},
		{historical}));
    auto changed=spell;changed.words[34]=1;
    assert(!SpellDbcComposer::BehaviorMatches(changed));
	bool rejectedInvalidGeneration = false;
	try {
		(void)ContentServerBundle::ServerJson(
			"Eitrigg", {}, {}, {}, {}, {}, {}, {changed});
	} catch (std::exception const&) {
		rejectedInvalidGeneration = true;
	}
	assert(rejectedInvalidGeneration);
    assert(!ContentServerBundle::VerifyParity(spellParity,"Eitrigg",8,"",hash,hash,
        {},{},error,{},{},{},{},{},{spell})); // retained spell lease is mandatory
}
