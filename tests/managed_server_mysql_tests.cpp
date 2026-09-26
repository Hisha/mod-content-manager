#include "ContentManagedServer.h"
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
} // namespace
int main(int argc, char **argv) {
	assert(argc == 2);
	WorldDatabase.Connect(argv[1]);
	SQL("DROP TABLE IF EXISTS "
		"content_manager_server_resource_owner,creature,gameobject_template,"
		"creature_template");
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
	SQL("INSERT INTO "
		"creature_template(entry,name,minlevel,maxlevel,faction,npcflag) "
		"VALUES(100,'Donor',10,10,14,1)");
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
		   Has(error, "donor=999") && Has(error, "donor creature_template entry 999 is missing"));
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
			 creature, "Realm", false, 1, std::string(64, 'a')))
		tx->Append(sql);
	for (auto const &sql : ContentManagedServer::ApplySql(
			 object, "Realm", false, 1, std::string(64, 'a')))
		tx->Append(sql);
	for (auto const &sql : ContentManagedServer::ApplySql(
			 spawn, "Realm", false, 1, std::string(64, 'a')))
		tx->Append(sql);
	tx->Append(
		"INSERT INTO "
		"content_manager_server_resource_owner(resource_kind,entry,realm_name,"
		"package_key,symbol,row_json,applied_build,artifact_sha256) "
		"VALUES('creature-template.id',1000,'x','x','x','x',1,'x')");
	WorldDatabase.DirectCommitTransaction(tx);
	assert(Scalar("SELECT COUNT(*) FROM creature_template WHERE entry=1000") ==
		   0);
	assert(
		Scalar("SELECT COUNT(*) FROM gameobject_template WHERE entry=2000") ==
		0);
	assert(Scalar("SELECT COUNT(*) FROM creature WHERE guid=3000") == 0);
	tx = WorldDatabase.BeginTransaction();
	for (auto const &sql : ContentManagedServer::ApplySql(
			 creature, "Realm", false, 1, std::string(64, 'a')))
		tx->Append(sql);
	for (auto const &sql : ContentManagedServer::ApplySql(
			 object, "Realm", false, 1, std::string(64, 'a')))
		tx->Append(sql);
	for (auto const &sql : ContentManagedServer::ApplySql(
			 spawn, "Realm", false, 1, std::string(64, 'a')))
		tx->Append(sql);
	WorldDatabase.DirectCommitTransaction(tx);
	assert(ContentManagedServer::Verify(creature, "Realm", 1,
										std::string(64, 'a'), error));
	assert(ContentManagedServer::Verify(object, "Realm", 1,
										std::string(64, 'a'), error));
	assert(ContentManagedServer::Verify(spawn, "Realm", 1, std::string(64, 'a'),
										error));
	SQL("UPDATE creature_template SET name='Drifted' WHERE entry=1000");
	assert(!ContentManagedServer::Check(creature, "Realm", exists, error));
	assert(Has(error, "kind=creature-template.id") &&
		   Has(error, "managed target fields differ from the expected row"));
	SQL("UPDATE creature_template SET name='Managed Creature' WHERE entry=1000");
	SQL("UPDATE content_manager_server_resource_owner SET package_key='wrong' "
		"WHERE resource_kind='creature-template.id' AND entry=1000");
	assert(!ContentManagedServer::Check(creature, "Realm", exists, error));
	assert(Has(error, "ownership row does not match realm=Realm package=package-a "
				  "symbol=managed-creature"));
	SQL("UPDATE content_manager_server_resource_owner SET package_key='package-a' "
		"WHERE resource_kind='creature-template.id' AND entry=1000");
	SQL("UPDATE content_manager_server_resource_owner SET row_json='{}' WHERE "
		"resource_kind='creature-template.id' AND entry=1000");
	assert(!ContentManagedServer::Check(creature, "Realm", exists, error));
	assert(Has(error, "ownership snapshot differs from the expected row"));
	SQL("DROP TABLE creature");
	assert(!ContentManagedServer::Check(spawn, "Realm", exists, error));
	assert(Has(error, "kind=creature-spawn.guid") &&
		   Has(error, "condition SQL query failed"));
	std::cout << "PASS donor validation, managed representations, ownership, "
				 "spawn resolution, diagnostics and transactional rollback\n";
}
