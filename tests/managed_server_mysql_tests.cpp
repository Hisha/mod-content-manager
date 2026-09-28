#include "ContentManagedServer.h"
#include "ContentSpellServer.h"
#include "DatabaseEnv.h"
#include <cassert>
#include <iostream>

namespace {
void SQL(std::string const &sql) {
	if (!WorldDatabase.Execute(sql))
		throw std::runtime_error(WorldDatabase.lastError);
}
unsigned Scalar(std::string const &sql) {
	auto q = WorldDatabase.Query(sql);
	assert(q);
	return q->Fetch()[0].Get<unsigned>();
}
bool Has(std::string const &value, std::string const &part) {
	return value.find(part) != std::string::npos;
}
bool SpellString(std::size_t i, std::size_t &group, std::size_t &locale) {
	for (std::size_t g = 0; g < 4; ++g) {
		auto base = std::array<std::size_t, 4>{136, 153, 170, 187}[g];
		if (i >= base && i < base + 16) {
			group = g;
			locale = i - base;
			return true;
		}
	}
	return false;
}
std::string SpellColumnType(std::size_t i) {
	std::size_t group = 0, locale = 0;
	if (SpellString(i, group, locale)) {
		if (group == 2)
			return "TEXT";
		if (group == 3 && locale != 15)
			return "VARCHAR(550)";
		return "VARCHAR(100)";
	}
	if (i == 12 || i == 14)
		return "BIGINT UNSIGNED";
	if (i == 47 || (i >= 77 && i <= 79) || (i >= 101 && i <= 103) ||
		(i >= 119 && i <= 121) || (i >= 216 && i <= 218) ||
		(i >= 229 && i <= 231))
		return "FLOAT";
	if (i == 0 || i == 13 || i == 15 || i == 41 ||
		(i >= 52 && i <= 70) || (i >= 74 && i <= 76) ||
		(i >= 80 && i <= 82) || (i >= 110 && i <= 115) || i == 224 ||
		i == 228)
		return "INT";
	return "INT UNSIGNED";
}
} // namespace
int main(int argc, char **argv) {
	assert(argc == 2);
	WorldDatabase.Connect(argv[1]);
	SQL("DROP TABLE IF EXISTS "
		"content_manager_server_resource_owner,spell_dbc,creature,gameobject_template,"
		"creature_template_model,creature_template");
	SQL("CREATE TABLE creature_template(entry INT UNSIGNED PRIMARY KEY,name "
		"VARCHAR(255) NOT NULL DEFAULT '',subname VARCHAR(255) NOT NULL "
		"DEFAULT '',minlevel TINYINT UNSIGNED NOT NULL DEFAULT 1,maxlevel "
		"TINYINT UNSIGNED NOT NULL DEFAULT 1,faction INT UNSIGNED NOT NULL "
		"DEFAULT 0,npcflag INT UNSIGNED NOT NULL DEFAULT 0,speed_walk FLOAT "
		"NOT NULL DEFAULT 1,speed_run FLOAT NOT NULL DEFAULT 1,rank INT "
		"UNSIGNED NOT NULL DEFAULT 0,dmgschool INT "
		"UNSIGNED NOT NULL DEFAULT 0,BaseAttackTime INT UNSIGNED NOT NULL "
		"DEFAULT 2000,RangeAttackTime INT UNSIGNED NOT NULL DEFAULT "
		"2000,unit_class INT UNSIGNED NOT NULL DEFAULT 1,unit_flags INT "
		"UNSIGNED NOT NULL DEFAULT 0,type INT UNSIGNED NOT NULL DEFAULT "
		"1,type_flags INT UNSIGNED NOT NULL DEFAULT 0,RegenHealth INT UNSIGNED "
		"NOT NULL DEFAULT "
		"1,flags_extra INT UNSIGNED NOT NULL DEFAULT 0,AIName VARCHAR(255) NOT "
		"NULL DEFAULT '',ScriptName VARCHAR(255) NOT NULL DEFAULT '') "
		"ENGINE=InnoDB");
	SQL("CREATE TABLE creature_template_model(CreatureID INT UNSIGNED NOT "
		"NULL,Idx SMALLINT UNSIGNED NOT NULL DEFAULT 0,CreatureDisplayID INT "
		"UNSIGNED NOT NULL,DisplayScale FLOAT NOT NULL DEFAULT 1,Probability "
		"FLOAT NOT NULL DEFAULT 0,VerifiedBuild INT DEFAULT NULL,PRIMARY "
		"KEY(CreatureID,Idx),CHECK(Idx<=3)) ENGINE=InnoDB");
	std::string go =
		"CREATE TABLE gameobject_template(entry INT UNSIGNED PRIMARY KEY,type "
		"INT UNSIGNED NOT NULL,displayId INT UNSIGNED NOT NULL,name "
		"VARCHAR(255) NOT NULL,IconName VARCHAR(255) NOT NULL DEFAULT "
		"'',castBarCaption VARCHAR(255) NOT NULL DEFAULT '',unk1 VARCHAR(255) "
		"NOT NULL DEFAULT '',size FLOAT NOT NULL DEFAULT 1";
	for (unsigned i = 0; i < 24; ++i)
		go += ",Data" + std::to_string(i) + " INT UNSIGNED NOT NULL DEFAULT 0";
	go += ",AIName VARCHAR(255) NOT NULL DEFAULT '',ScriptName VARCHAR(255) "
		  "NOT NULL DEFAULT '',VerifiedBuild INT NOT NULL DEFAULT 12340) "
		  "ENGINE=InnoDB";
	SQL(go);
	SQL("CREATE TABLE creature(guid INT UNSIGNED PRIMARY KEY,id INT UNSIGNED "
		"NOT NULL,map INT UNSIGNED NOT NULL,spawnMask INT UNSIGNED NOT NULL "
		"DEFAULT 1,phaseMask INT UNSIGNED NOT NULL DEFAULT 1,position_x FLOAT "
		"NOT NULL,position_y FLOAT NOT NULL,position_z FLOAT NOT "
		"NULL,orientation FLOAT NOT NULL,spawntimesecs INT UNSIGNED NOT NULL "
		"DEFAULT 300,wander_distance FLOAT NOT NULL DEFAULT 0,MovementType INT "
		"UNSIGNED NOT NULL DEFAULT 0) ENGINE=InnoDB");
	SQL("CREATE TABLE content_manager_server_resource_owner(resource_kind "
		"VARCHAR(32) COLLATE utf8mb4_bin NOT NULL,entry INT UNSIGNED NOT "
		"NULL,realm_name VARCHAR(255) COLLATE utf8mb4_bin NOT NULL,package_key "
		"VARCHAR(191) COLLATE utf8mb4_bin NOT NULL,symbol VARCHAR(64) COLLATE "
		"utf8mb4_bin NOT NULL,row_json LONGTEXT COLLATE utf8mb4_bin NOT "
		"NULL,applied_build INT UNSIGNED NOT NULL,artifact_sha256 CHAR(64) "
		"COLLATE utf8mb4_bin NOT NULL,PRIMARY KEY(resource_kind,entry),UNIQUE "
		"KEY(realm_name,package_key,symbol,resource_kind)) ENGINE=InnoDB "
		"DEFAULT CHARSET=utf8mb4");
	std::string spellTable = "CREATE TABLE spell_dbc(";
	auto spellColumns = ContentSpellServer::Columns();
	for (std::size_t i = 0; i < spellColumns.size(); ++i) {
		std::size_t group = 0, locale = 0;
		if (i)
			spellTable += ',';
		spellTable += '`' + spellColumns[i] + "` " + SpellColumnType(i);
		if (SpellString(i, group, locale))
			spellTable +=
				" CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NULL";
		else
			spellTable += " NOT NULL";
	}
	spellTable += ",PRIMARY KEY(ID)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 "
				  "COLLATE=utf8mb4_unicode_ci";
	SQL(spellTable);
	// Reproduce the PTR mismatch: expressions default to 0900 while every
	// localized spell_dbc column remains utf8mb4_unicode_ci.
	SQL("SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci");
	ResolvedSpell spell;
	spell.packageKey = "cm-managed-spell-ptr-test";
	spell.packageVersion = "1.0.0";
	spell.symbol = "managed-aura-test";
	spell.profile = SpellDbcComposer::Profile;
	spell.id = 80865;
	spell.copyFrom = 1243;
	spell.iconCopyFromSpell = 1243;
	spell.words[0] = spell.id;
	spell.words[28] = 1;
	spell.words[40] = SpellDbcComposer::PermanentDuration;
	spell.words[46] = 1;
	spell.words[68] = 0xFFFFFFFFu;
	spell.words[71] = 6;
	spell.words[86] = 1;
	spell.words[95] = 4;
	spell.words[133] = 685;
	spell.words[225] = 1;
	for (auto &locale : spell.localized[0])
		locale = "CM Managed Aura Test";
	spell.localized[0][3] = "Aura g\xC3\xA9" "r\xC3\xA9" "e";
	for (auto &locale : spell.localized[2])
		locale = "Disposable PTR validation spell for Content Manager.";
	for (auto &locale : spell.localized[3])
		locale = "Informational aura with no gameplay effect.";
	auto spellTx = WorldDatabase.BeginTransaction();
	for (auto const &sql : ContentSpellServer::ApplySql(
			 spell, spell, "Realm", false, 44, std::string(64, 's')))
		spellTx->Append(sql);
	WorldDatabase.DirectCommitTransaction(spellTx);
	assert(Scalar("SELECT EquippedItemClass+1 FROM spell_dbc WHERE ID=80865") == 0);
	assert(Scalar("SELECT CAST((" +
		ContentSpellServer::Condition(spell, "Realm", true) +
		") AS UNSIGNED)") == 1);
	std::string spellError;
	assert(ContentSpellServer::Verify(
		spell, "Realm", 44, std::string(64, 's'), spellError));
	SQL("UPDATE spell_dbc SET Name_Lang_frFR='D\xC3\xA9rive' WHERE ID=80865");
	assert(!ContentSpellServer::Verify(
		spell, "Realm", 44, std::string(64, 's'), spellError));
	SQL("DELETE FROM content_manager_server_resource_owner WHERE "
		"resource_kind='spell.id' AND entry=80865");
	SQL("DROP TABLE spell_dbc");
	SQL("INSERT INTO "
		"creature_template(entry,name,minlevel,maxlevel,faction,npcflag) "
		"VALUES(100,'Donor',10,10,14,1),(101,'Single Model',10,10,14,1),"
		"(102,'Missing Model',10,10,14,1)");
	SQL("INSERT INTO creature_template_model VALUES"
		"(100,0,1859,1.0,0.75,12340),(100,2,1860,0.875,0.25,NULL),"
		"(101,0,1900,1.0,1.0,12340)");
	SQL("INSERT INTO gameobject_template(entry,type,displayId,name) "
		"VALUES(200,5,300,'Donor Object')");
	ContentCreatureTemplate c;
	c.symbol = "managed-creature";
	c.copyFrom = 100;
	c.name = "Managed Creature";
	ResolvedCreatureTemplate creature;
	std::string error;
	assert(ContentManagedServer::ResolveDonor(c, "package-a", "1", 1000,
											  creature, error));
	creature.packageVersion = "2";
	assert(creature.models.size() == 2 && creature.models[0].index == 0 &&
		   creature.models[0].displayId == 1859 &&
		   creature.models[1].index == 2 &&
		   !creature.models[1].verifiedBuild);
	c.copyFrom = 101;
	ResolvedCreatureTemplate singleModel;
	assert(ContentManagedServer::ResolveDonor(c, "package-a", "1", 1001,
											  singleModel, error));
	assert(singleModel.models.size() == 1 &&
		   singleModel.models[0].displayId == 1900);
	c.copyFrom = 102;
	ResolvedCreatureTemplate missingModels;
	assert(!ContentManagedServer::ResolveDonor(c, "package-a", "1", 1002,
											   missingModels, error));
	assert(Has(error, "has no creature_template_model rows"));
	c.copyFrom = 999;
	ResolvedCreatureTemplate missing;
	assert(!ContentManagedServer::ResolveDonor(c, "package-a", "1", 1001,
											   missing, error));
	ContentGameObjectTemplate g;
	g.symbol = "managed-object";
	g.copyFrom = 200;
	g.name = "Managed Object";
	ResolvedGameObjectTemplate object;
	assert(ContentManagedServer::ResolveDonor(g, "package-a", "1", 2000, object,
											  error));
	ResolvedCreatureSpawn spawn{"package-a",
								"1",
								"managed-spawn",
								"managed-creature",
								3000,
								1000,
								0,
								1,
								1,
								300,
								0,
								1,
								2,
								3,
								0,
								0};
	bool exists = false;
	auto missingDonor = creature;
	missingDonor.copyFrom = 999;
	assert(!ContentManagedServer::Check(missingDonor, "Realm", exists, error));
	assert(Has(error, "kind=creature-template.id") &&
		   Has(error, "package=package-a") &&
		   Has(error, "symbol=managed-creature") && Has(error, "entry=1000") &&
		   Has(error, "donor=999") &&
		   Has(error, "donor creature_template entry 999 is missing"));
	// Removing an existing-path-only column proves that a true new-resource
	// condition returns without evaluating the irrelevant existing condition.
	SQL("ALTER TABLE creature_template DROP COLUMN `rank`");
	assert(ContentManagedServer::Check(creature, "Realm", exists, error) &&
		   !exists);
	SQL("ALTER TABLE creature_template ADD COLUMN `rank` INT UNSIGNED NOT NULL "
		"DEFAULT 0");
	SQL("INSERT INTO creature_template(entry,name) VALUES(1000,'Occupied')");
	assert(!ContentManagedServer::Check(creature, "Realm", exists, error));
	assert(Has(error, "entry=1000") &&
		   Has(error, "occupied by an unowned target row"));
	SQL("DELETE FROM creature_template WHERE entry=1000");
	SQL("INSERT INTO gameobject_template(entry,type,displayId,name) "
		"VALUES(2000,5,300,'Occupied')");
	assert(!ContentManagedServer::Check(object, "Realm", exists, error));
	assert(Has(error, "kind=gameobject-template.id") &&
		   Has(error, "package=package-a") && Has(error, "symbol=managed-object") &&
		   Has(error, "entry=2000") && Has(error, "donor=200") &&
		   Has(error, "occupied by an unowned target row"));
	SQL("DELETE FROM gameobject_template WHERE entry=2000");
	SQL("INSERT INTO creature(guid,id,map,position_x,position_y,position_z,"
		"orientation) VALUES(3000,1000,0,1,2,3,0)");
	assert(!ContentManagedServer::Check(spawn, "Realm", exists, error));
	assert(Has(error, "kind=creature-spawn.guid") &&
		   Has(error, "package=package-a") && Has(error, "symbol=managed-spawn") &&
		   Has(error, "guid=3000") && Has(error, "creatureEntry=1000") &&
		   Has(error, "occupied by an unowned target row"));
	SQL("DELETE FROM creature WHERE guid=3000");
	assert(ContentManagedServer::Check(creature, "Realm", exists, error) &&
		   !exists);
	assert(ContentManagedServer::Check(object, "Realm", exists, error) &&
		   !exists);
	assert(ContentManagedServer::Check(spawn, "Realm", exists, error) &&
		   !exists);
	auto tx = WorldDatabase.BeginTransaction();
	for (auto const &sql : ContentManagedServer::ApplySql(
			 creature, creature, "Realm", false, 1, std::string(64, 'a')))
		tx->Append(sql);
	for (auto const &sql : ContentManagedServer::ApplySql(
			 object, object, "Realm", false, 1, std::string(64, 'a')))
		tx->Append(sql);
	for (auto const &sql : ContentManagedServer::ApplySql(
			 spawn, spawn, "Realm", false, 1, std::string(64, 'a')))
		tx->Append(sql);
	tx->Append(
		"INSERT INTO "
		"content_manager_server_resource_owner(resource_kind,entry,realm_name,"
		"package_key,symbol,row_json,applied_build,artifact_sha256) "
		"VALUES('creature-template.id',1000,'x','x','x','x',1,'x')");
	WorldDatabase.DirectCommitTransaction(tx);
	assert(Scalar("SELECT COUNT(*) FROM creature_template WHERE entry=1000") ==
		   0);
	assert(Scalar("SELECT COUNT(*) FROM creature_template_model WHERE "
				  "CreatureID=1000") == 0);
	assert(
		Scalar("SELECT COUNT(*) FROM gameobject_template WHERE entry=2000") ==
		0);
	assert(Scalar("SELECT COUNT(*) FROM creature WHERE guid=3000") == 0);
	tx = WorldDatabase.BeginTransaction();
	for (auto const &sql : ContentManagedServer::ApplySql(
			 creature, creature, "Realm", false, 1, std::string(64, 'a')))
		tx->Append(sql);
	for (auto const &sql : ContentManagedServer::ApplySql(
			 object, object, "Realm", false, 1, std::string(64, 'a')))
		tx->Append(sql);
	for (auto const &sql : ContentManagedServer::ApplySql(
			 spawn, spawn, "Realm", false, 1, std::string(64, 'a')))
		tx->Append(sql);
	WorldDatabase.DirectCommitTransaction(tx);
	assert(ContentManagedServer::Verify(creature, "Realm", 1,
										std::string(64, 'a'), error));
	assert(Scalar("SELECT COUNT(*) FROM creature_template_model WHERE "
				  "CreatureID=1000") == 2);
	SQL("DELETE FROM creature_template_model WHERE CreatureID=1000 AND Idx=2");
	assert(!ContentManagedServer::Verify(creature, "Realm", 1,
										 std::string(64, 'a'), error));
	SQL("INSERT INTO creature_template_model VALUES"
		"(1000,2,1860,0.875,0.25,NULL)");
	SQL("UPDATE creature_template_model SET CreatureDisplayID=9999 WHERE "
		"CreatureID=1000 AND Idx=0");
	assert(!ContentManagedServer::Verify(creature, "Realm", 1,
										 std::string(64, 'a'), error));
	SQL("UPDATE creature_template_model SET CreatureDisplayID=1859 WHERE "
		"CreatureID=1000 AND Idx=0");
	SQL("UPDATE creature_template_model SET DisplayScale=0.5,Probability=0.5 "
		"WHERE CreatureID=1000 AND Idx=0");
	assert(!ContentManagedServer::Verify(creature, "Realm", 1,
										 std::string(64, 'a'), error));
	SQL("UPDATE creature_template_model SET DisplayScale=1.0,Probability=0.75 "
		"WHERE CreatureID=1000 AND Idx=0");
	assert(ContentManagedServer::Verify(creature, "Realm", 1,
										std::string(64, 'a'), error));
	SQL("INSERT INTO creature_template_model VALUES"
		"(1000,1,1901,1.0,0.0,12340)");
	assert(!ContentManagedServer::Verify(creature, "Realm", 1,
										 std::string(64, 'a'), error));
	SQL("DELETE FROM creature_template_model WHERE CreatureID=1000 AND Idx=1");
	assert(ContentManagedServer::Verify(object, "Realm", 1,
										std::string(64, 'a'), error));
	assert(ContentManagedServer::Verify(spawn, "Realm", 1, std::string(64, 'a'),
										error));
	ResolvedCreatureTemplate recorded;
	assert(ContentManagedServer::Check(creature, "Realm", exists, recorded,
									 error) &&
		   exists && recorded.entry == creature.entry &&
		   recorded.name == creature.name &&
		   recorded.models.size() == creature.models.size());
	// A changed descriptor is legitimate when the live parent and complete child
	// set still match the ownership snapshot recorded by the prior package.
	auto evolved = creature;
	evolved.packageVersion = "3";
	evolved.name = "Managed Creature v3";
	evolved.models[1].displayId = 2860;
	assert(ContentManagedServer::Check(evolved, "Realm", exists, recorded,
									 error) &&
		   exists && recorded.entry == creature.entry &&
		   recorded.name == creature.name && recorded.models[1].displayId == 1860);
	tx = WorldDatabase.BeginTransaction();
	for (auto const &sql : ContentManagedServer::ApplySql(
			 evolved, recorded, "Realm", true, 2, std::string(64, 'b')))
		tx->Append(sql);
	WorldDatabase.DirectCommitTransaction(tx);
	assert(ContentManagedServer::Verify(evolved, "Realm", 2,
									std::string(64, 'b'), error));
	assert(Scalar("SELECT COUNT(*) FROM creature_template WHERE entry=1000 AND "
				  "name='Managed Creature v3'") == 1);
	assert(Scalar("SELECT COUNT(*) FROM creature_template_model WHERE "
				  "CreatureID=1000 AND Idx=2 AND CreatureDisplayID=2860") == 1);
	assert(Scalar("SELECT COUNT(*) FROM content_manager_server_resource_owner "
				  "WHERE resource_kind='creature-template.id' AND entry=1000 AND "
				  "realm_name='Realm' AND package_key='package-a' AND "
				  "symbol='managed-creature'") == 1);
	// The allocation identity is retained: evolution updates entry 1000 in
	// place and neither creates nor takes ownership of another entry.
	assert(Scalar("SELECT COUNT(*) FROM creature_template WHERE entry<>1000 AND "
				  "name='Managed Creature v3'") == 0);
	creature = evolved;
	SQL("UPDATE creature_template SET name='Drifted' WHERE entry=1000");
	assert(!ContentManagedServer::Check(creature, "Realm", exists, error));
	assert(Has(error, "kind=creature-template.id") &&
		   Has(error, "managed target differs from recorded ownership snapshot"));
	SQL("UPDATE creature_template SET name='Managed Creature v3' WHERE "
		"entry=1000");
	SQL("UPDATE creature_template_model SET CreatureDisplayID=9999 WHERE "
		"CreatureID=1000 AND Idx=2");
	assert(!ContentManagedServer::Check(creature, "Realm", exists, error));
	assert(Has(error, "managed target differs from recorded ownership snapshot"));
	SQL("UPDATE creature_template_model SET CreatureDisplayID=2860 WHERE "
		"CreatureID=1000 AND Idx=2");
	SQL("UPDATE content_manager_server_resource_owner SET package_key='wrong' "
		"WHERE resource_kind='creature-template.id' AND entry=1000");
	assert(!ContentManagedServer::Check(creature, "Realm", exists, error));
	assert(Has(error, "ownership row does not match realm=Realm package=package-a "
				  "symbol=managed-creature"));
	SQL("UPDATE content_manager_server_resource_owner SET package_key='package-a' "
		"WHERE resource_kind='creature-template.id' AND entry=1000");
	SQL("ALTER TABLE creature_template DROP COLUMN `rank`");
	assert(!ContentManagedServer::Check(creature, "Realm", exists, error));
	assert(Has(error, "kind=creature-template.id") &&
		   Has(error, "recorded-snapshot condition SQL query failed"));
	SQL("UPDATE content_manager_server_resource_owner SET row_json='{}' WHERE "
		"resource_kind='creature-template.id' AND entry=1000");
	assert(!ContentManagedServer::Check(creature, "Realm", exists, error));
	assert(Has(error, "invalid recorded ownership snapshot"));
	SQL("DROP TABLE creature");
	assert(!ContentManagedServer::Check(spawn, "Realm", exists, error));
	assert(Has(error, "kind=creature-spawn.guid") &&
		   Has(error, "new-resource condition SQL query failed"));
	std::cout << "PASS donor/model validation, managed representations, "
				 "unchanged resources, v2-to-v3 evolution, retained allocation "
				 "identity, parent/child drift rejection, FLOAT-safe comparisons, "
				 "ownership, spawn resolution, diagnostics and transactional "
				 "rollback\n";
}
