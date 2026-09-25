#include "ContentManagedServer.h"
#include "ContentServerBundle.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tuple>

using nlohmann::json;
namespace {
auto T = ContentServerBundle::SqlIdentityText;
std::string N(std::uint32_t value) { return std::to_string(value); }
std::string F(float value) {
	if (!std::isfinite(value))
		throw std::runtime_error("Non-finite managed server value");
	std::ostringstream out;
	out << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
	return out.str();
}
std::string Q(std::string const &value) {
	return ContentServerBundle::SqlText(value);
}
std::string OwnerIdentity(std::string const &kind, std::uint32_t entry,
						  std::string const &realm, std::string const &package,
						  std::string const &symbol,
						  std::string const &snapshot) {
	return "o.resource_kind=" + T(kind) + " AND o.entry=" + N(entry) +
		   " AND o.realm_name=" + T(realm) +
		   " AND o.package_key=" + T(package) + " AND o.symbol=" + T(symbol) +
		   " AND o.row_json=" + T(snapshot);
}
json CreatureObject(ResolvedCreatureTemplate const &r) {
	return {{"resourceKind", "creature-template.id"},
			{"package", r.packageKey},
			{"packageVersion", r.packageVersion},
			{"symbol", r.symbol},
			{"entry", r.entry},
			{"copyFrom", r.copyFrom},
			{"fields",
			 {{"name", r.name},
			  {"subname", r.subname},
			  {"minlevel", r.minLevel},
			  {"maxlevel", r.maxLevel},
			  {"faction", r.faction},
			  {"npcflag", r.npcFlags},
			  {"speed_walk", r.speedWalk},
			  {"speed_run", r.speedRun},
			  {"rank", r.rank},
			  {"dmgschool", r.damageSchool},
			  {"BaseAttackTime", r.baseAttackTime},
			  {"RangeAttackTime", r.rangeAttackTime},
			  {"unit_class", r.unitClass},
			  {"unit_flags", r.unitFlags},
			  {"type", r.type},
			  {"type_flags", r.typeFlags},
			  {"RegenHealth", r.regenHealth},
			  {"flags_extra", r.flagsExtra},
			  {"AIName", r.aiName},
			  {"ScriptName", r.scriptName}}}};
}
json GameObjectObject(ResolvedGameObjectTemplate const &r) {
	json fields = {{"type", r.type},
				   {"displayId", r.displayId},
				   {"name", r.name},
				   {"IconName", r.iconName},
				   {"castBarCaption", r.castBarCaption},
				   {"unk1", r.unk1},
				   {"size", r.size},
				   {"AIName", r.aiName},
				   {"ScriptName", r.scriptName},
				   {"VerifiedBuild", r.verifiedBuild}};
	for (std::size_t i = 0; i < r.data.size(); ++i)
		fields["Data" + std::to_string(i)] = r.data[i];
	return {{"resourceKind", "gameobject-template.id"},
			{"package", r.packageKey},
			{"packageVersion", r.packageVersion},
			{"symbol", r.symbol},
			{"entry", r.entry},
			{"copyFrom", r.copyFrom},
			{"fields", fields}};
}
json SpawnObject(ResolvedCreatureSpawn const &r) {
	return {{"resourceKind", "creature-spawn.guid"},
			{"package", r.packageKey},
			{"packageVersion", r.packageVersion},
			{"symbol", r.symbol},
			{"guid", r.guid},
			{"creatureSymbol", r.creatureSymbol},
			{"creatureEntry", r.creatureEntry},
			{"fields",
			 {{"map", r.map},
			  {"spawnMask", r.spawnMask},
			  {"phaseMask", r.phaseMask},
			  {"position_x", r.x},
			  {"position_y", r.y},
			  {"position_z", r.z},
			  {"orientation", r.orientation},
			  {"spawntimesecs", r.respawnSeconds},
			  {"wander_distance", r.wanderDistance},
			  {"MovementType", r.movementType}}}};
}
std::string Snapshot(ResolvedCreatureTemplate const &r) {
	return CreatureObject(r).dump();
}
std::string Snapshot(ResolvedGameObjectTemplate const &r) {
	return GameObjectObject(r).dump();
}
std::string Snapshot(ResolvedCreatureSpawn const &r) {
	return SpawnObject(r).dump();
}
std::uint32_t Entry(ResolvedCreatureTemplate const &r) { return r.entry; }
std::uint32_t Entry(ResolvedGameObjectTemplate const &r) { return r.entry; }
std::uint32_t Entry(ResolvedCreatureSpawn const &r) { return r.guid; }
std::string Kind(ResolvedCreatureTemplate const &) {
	return "creature-template.id";
}
std::string Kind(ResolvedGameObjectTemplate const &) {
	return "gameobject-template.id";
}
std::string Kind(ResolvedCreatureSpawn const &) {
	return "creature-spawn.guid";
}

ResolvedCreatureTemplate ParseCreature(json const &v) {
	ResolvedCreatureTemplate r;
	r.packageKey = v.at("package");
	r.packageVersion = v.at("packageVersion");
	r.symbol = v.at("symbol");
	r.entry = v.at("entry");
	r.copyFrom = v.at("copyFrom");
	auto const &f = v.at("fields");
	r.name = f.at("name");
	r.subname = f.at("subname");
	r.minLevel = f.at("minlevel");
	r.maxLevel = f.at("maxlevel");
	r.faction = f.at("faction");
	r.npcFlags = f.at("npcflag");
	r.speedWalk = f.at("speed_walk");
	r.speedRun = f.at("speed_run");
	r.rank = f.at("rank");
	r.damageSchool = f.at("dmgschool");
	r.baseAttackTime = f.at("BaseAttackTime");
	r.rangeAttackTime = f.at("RangeAttackTime");
	r.unitClass = f.at("unit_class");
	r.unitFlags = f.at("unit_flags");
	r.type = f.at("type");
	r.typeFlags = f.at("type_flags");
	r.regenHealth = f.at("RegenHealth");
	r.flagsExtra = f.at("flags_extra");
	r.aiName = f.at("AIName");
	r.scriptName = f.at("ScriptName");
	if (v.at("resourceKind") != "creature-template.id" || !r.entry ||
		!r.copyFrom || r.packageKey.empty() || r.packageVersion.empty() ||
		r.symbol.empty())
		throw std::runtime_error("Invalid creature-template identity");
	return r;
}
ResolvedGameObjectTemplate ParseGameObject(json const &v) {
	ResolvedGameObjectTemplate r;
	r.packageKey = v.at("package");
	r.packageVersion = v.at("packageVersion");
	r.symbol = v.at("symbol");
	r.entry = v.at("entry");
	r.copyFrom = v.at("copyFrom");
	auto const &f = v.at("fields");
	r.type = f.at("type");
	r.displayId = f.at("displayId");
	r.name = f.at("name");
	r.iconName = f.at("IconName");
	r.castBarCaption = f.at("castBarCaption");
	r.unk1 = f.at("unk1");
	r.size = f.at("size");
	r.aiName = f.at("AIName");
	r.scriptName = f.at("ScriptName");
	r.verifiedBuild = f.at("VerifiedBuild");
	for (std::size_t i = 0; i < r.data.size(); ++i)
		r.data[i] = f.at("Data" + std::to_string(i));
	if (v.at("resourceKind") != "gameobject-template.id" || !r.entry ||
		!r.copyFrom || r.packageKey.empty() || r.packageVersion.empty() ||
		r.symbol.empty() || !std::isfinite(r.size) || r.size <= 0)
		throw std::runtime_error("Invalid gameobject-template identity/size");
	return r;
}
ResolvedCreatureSpawn ParseSpawn(json const &v) {
	ResolvedCreatureSpawn r;
	r.packageKey = v.at("package");
	r.packageVersion = v.at("packageVersion");
	r.symbol = v.at("symbol");
	r.guid = v.at("guid");
	r.creatureSymbol = v.at("creatureSymbol");
	r.creatureEntry = v.at("creatureEntry");
	auto const &f = v.at("fields");
	r.map = f.at("map");
	r.spawnMask = f.at("spawnMask");
	r.phaseMask = f.at("phaseMask");
	r.x = f.at("position_x");
	r.y = f.at("position_y");
	r.z = f.at("position_z");
	r.orientation = f.at("orientation");
	r.respawnSeconds = f.at("spawntimesecs");
	r.wanderDistance = f.at("wander_distance");
	r.movementType = f.at("MovementType");
	if (v.at("resourceKind") != "creature-spawn.guid" || !r.guid ||
		!r.creatureEntry || r.packageKey.empty() || r.packageVersion.empty() ||
		r.symbol.empty() || r.creatureSymbol.empty())
		throw std::runtime_error("Invalid creature-spawn identity");
	return r;
}
template <class Row>
bool QueryCheck(Row const &r, std::string const &realm, bool &exists,
				std::string &error) {
	auto q = WorldDatabase.Query(
		"SELECT CAST((" + ContentManagedServer::Condition(r, realm, false) +
		") AS UNSIGNED),CAST((" +
		ContentManagedServer::Condition(r, realm, true) + ") AS UNSIGNED)");
	if (!q || (!q->Fetch()[0].template Get<std::uint64_t>() &&
			   !q->Fetch()[1].template Get<std::uint64_t>())) {
		error = "Managed server resource collision, missing donor, ownership "
				"mismatch or drift";
		return false;
	}
	exists = q->Fetch()[1].template Get<std::uint64_t>() != 0;
	return true;
}
template <class Row>
bool QueryVerify(Row const &r, std::string const &realm, std::uint32_t build,
				 std::string const &hash, std::string &error) {
	auto q = WorldDatabase.Query(
		"SELECT CAST((" + ContentManagedServer::Condition(r, realm, true) +
		") AND EXISTS(SELECT 1 FROM content_manager_server_resource_owner "
		"WHERE resource_kind=" +
		T(Kind(r)) + " AND entry=" + N(Entry(r)) + " AND applied_build=" +
		N(build) + " AND artifact_sha256=" + T(hash) + ") AS UNSIGNED)");
	if (!q || !q->Fetch()[0].template Get<std::uint64_t>()) {
		error = "Managed server resource post-apply provenance mismatch";
		return false;
	}
	return true;
}
} // namespace

bool ContentManagedServer::ResolveDonor(ContentCreatureTemplate const &d,
										std::string const &package,
										std::string const &version,
										std::uint32_t entry,
										ResolvedCreatureTemplate &r,
										std::string &error) {
	auto q = WorldDatabase.Query(
		"SELECT "
		"name,subname,minlevel,maxlevel,faction,npcflag,speed_walk,speed_run,"
		"`rank`,dmgschool,BaseAttackTime,RangeAttackTime,unit_class,unit_flags,"
		"type,type_flags,RegenHealth,flags_extra,AIName,"
		"ScriptName FROM creature_template WHERE entry=" +
		N(d.copyFrom));
	if (!q) {
		error = "Creature template donor does not exist: " + N(d.copyFrom);
		return false;
	}
	auto f = q->Fetch();
	r.packageKey = package;
	r.packageVersion = version;
	r.symbol = d.symbol;
	r.entry = entry;
	r.copyFrom = d.copyFrom;
	r.name = f[0].Get<std::string>();
	r.subname = f[1].Get<std::string>();
	r.minLevel = f[2].Get<std::uint32_t>();
	r.maxLevel = f[3].Get<std::uint32_t>();
	r.faction = f[4].Get<std::uint32_t>();
	r.npcFlags = f[5].Get<std::uint32_t>();
	r.speedWalk = f[6].Get<float>();
	r.speedRun = f[7].Get<float>();
	r.rank = f[8].Get<std::uint32_t>();
	r.damageSchool = f[9].Get<std::uint32_t>();
	r.baseAttackTime = f[10].Get<std::uint32_t>();
	r.rangeAttackTime = f[11].Get<std::uint32_t>();
	r.unitClass = f[12].Get<std::uint32_t>();
	r.unitFlags = f[13].Get<std::uint32_t>();
	r.type = f[14].Get<std::uint32_t>();
	r.typeFlags = f[15].Get<std::uint32_t>();
	r.regenHealth = f[16].Get<std::uint32_t>();
	r.flagsExtra = f[17].Get<std::uint32_t>();
	r.aiName = f[18].Get<std::string>();
	r.scriptName = f[19].Get<std::string>();
	if (d.name)
		r.name = *d.name;
	if (d.subname)
		r.subname = *d.subname;
	if (d.minLevel)
		r.minLevel = *d.minLevel;
	if (d.maxLevel)
		r.maxLevel = *d.maxLevel;
	if (d.faction)
		r.faction = *d.faction;
	if (d.npcFlags)
		r.npcFlags = *d.npcFlags;
	if (d.unitFlags)
		r.unitFlags = *d.unitFlags;
	if (d.typeFlags)
		r.typeFlags = *d.typeFlags;
	if (d.aiName)
		r.aiName = *d.aiName;
	if (d.scriptName)
		r.scriptName = *d.scriptName;
	if (r.name.empty() || !r.minLevel || !r.maxLevel ||
		r.minLevel > r.maxLevel) {
		error = "Resolved creature template has invalid name/level range";
		return false;
	}
	return true;
}

bool ContentManagedServer::ResolveDonor(ContentGameObjectTemplate const &d,
										std::string const &package,
										std::string const &version,
										std::uint32_t entry,
										ResolvedGameObjectTemplate &r,
										std::string &error) {
	std::string sql =
		"SELECT type,displayId,name,IconName,castBarCaption,unk1,size";
	for (unsigned i = 0; i < 24; ++i)
		sql += ",Data" + std::to_string(i);
	sql += ",AIName,ScriptName,VerifiedBuild FROM gameobject_template WHERE "
		   "entry=" +
		   N(d.copyFrom);
	auto q = WorldDatabase.Query(sql);
	if (!q) {
		error = "Gameobject template donor does not exist: " + N(d.copyFrom);
		return false;
	}
	auto f = q->Fetch();
	r.packageKey = package;
	r.packageVersion = version;
	r.symbol = d.symbol;
	r.entry = entry;
	r.copyFrom = d.copyFrom;
	r.type = f[0].Get<std::uint32_t>();
	r.displayId = f[1].Get<std::uint32_t>();
	r.name = f[2].Get<std::string>();
	r.iconName = f[3].Get<std::string>();
	r.castBarCaption = f[4].Get<std::string>();
	r.unk1 = f[5].Get<std::string>();
	r.size = f[6].Get<float>();
	for (unsigned i = 0; i < 24; ++i)
		r.data[i] = f[7 + i].Get<std::uint32_t>();
	r.aiName = f[31].Get<std::string>();
	r.scriptName = f[32].Get<std::string>();
	r.verifiedBuild = f[33].Get<std::int32_t>();
	if (d.name)
		r.name = *d.name;
	if (d.type)
		r.type = *d.type;
	if (d.displayId)
		r.displayId = *d.displayId;
	if (d.size)
		r.size = *d.size;
	if (d.aiName)
		r.aiName = *d.aiName;
	if (d.scriptName)
		r.scriptName = *d.scriptName;
	if (r.name.empty() || !std::isfinite(r.size) || r.size <= 0) {
		error = "Resolved gameobject template has invalid name/size";
		return false;
	}
	return true;
}

bool ContentManagedServer::Occupancy(
	std::string const &kind, std::string const &realm,
	std::vector<ItemAllocation> const &retained,
	std::set<std::uint32_t> &occupied, std::string &error) {
	occupied.clear();
	std::string table, column;
	if (kind == "creature-template.id") {
		table = "creature_template";
		column = "entry";
	} else if (kind == "gameobject-template.id") {
		table = "gameobject_template";
		column = "entry";
	} else if (kind == "creature-spawn.guid") {
		table = "creature";
		column = "guid";
	} else {
		error = "Unknown occupancy kind";
		return false;
	}
	auto q = WorldDatabase.Query("SELECT " + column + " FROM " + table +
								 " UNION ALL SELECT 0");
	if (!q) {
		error = "Cannot inspect " + table + " occupancy";
		return false;
	}
	do {
		auto v = q->Fetch()[0].Get<std::uint32_t>();
		if (v)
			occupied.insert(v);
	} while (q->NextRow());
	auto owners = WorldDatabase.Query(
		"SELECT entry,package_key,symbol,row_json FROM "
		"content_manager_server_resource_owner WHERE realm_name=" +
		T(realm) + " AND resource_kind=" + T(kind) +
		" UNION ALL SELECT 0,NULL,NULL,NULL");
	if (!owners) {
		error = "Cannot inspect managed server ownership";
		return false;
	}
	do {
		auto f = owners->Fetch();
		if (!f[0].Get<std::uint32_t>())
			continue;
		auto entry = f[0].Get<std::uint32_t>();
		auto package = f[1].Get<std::string>();
		auto symbol = f[2].Get<std::string>();
		auto lease =
			std::find_if(retained.begin(), retained.end(), [&](auto const &a) {
				return a.resourceKind == kind && a.packageKey == package &&
					   a.symbol == symbol && a.value == entry;
			});
		if (lease == retained.end()) {
			error =
				"Managed server ownership lacks matching retained allocation";
			return false;
		}
		bool valid = false, exists = false;
		try {
			auto v = json::parse(f[3].Get<std::string>());
			if (kind == "creature-template.id")
				valid = Check(ParseCreature(v), realm, exists, error);
			else if (kind == "gameobject-template.id")
				valid = Check(ParseGameObject(v), realm, exists, error);
			else
				valid = Check(ParseSpawn(v), realm, exists, error);
		} catch (std::exception const &e) {
			error = e.what();
			return false;
		}
		if (!valid || !exists) {
			if (error.empty())
				error = "Managed server row drift";
			return false;
		}
		occupied.erase(entry);
	} while (owners->NextRow());
	return true;
}

std::string ContentManagedServer::Condition(ResolvedCreatureTemplate const &r,
											std::string const &realm,
											bool exists) {
	auto fields =
		"t.name=" + Q(r.name) + " AND t.subname=" + Q(r.subname) +
		" AND t.minlevel=" + N(r.minLevel) +
		" AND t.maxlevel=" + N(r.maxLevel) + " AND t.faction=" + N(r.faction) +
		" AND t.npcflag=" + N(r.npcFlags) +
		" AND t.speed_walk=" + F(r.speedWalk) +
		" AND t.speed_run=" + F(r.speedRun) +
		" AND t.`rank`=" + N(r.rank) + " AND t.dmgschool=" + N(r.damageSchool) +
		" AND t.BaseAttackTime=" + N(r.baseAttackTime) +
		" AND t.RangeAttackTime=" + N(r.rangeAttackTime) +
		" AND t.unit_class=" + N(r.unitClass) +
		" AND t.unit_flags=" + N(r.unitFlags) + " AND t.type=" + N(r.type) +
		" AND t.type_flags=" + N(r.typeFlags) +
		" AND t.RegenHealth=" + N(r.regenHealth) +
		" AND t.flags_extra=" + N(r.flagsExtra) +
		" AND t.AIName=" + Q(r.aiName) + " AND t.ScriptName=" + Q(r.scriptName);
	if (!exists)
		return "EXISTS(SELECT 1 FROM creature_template WHERE entry=" +
			   N(r.copyFrom) +
			   ") AND NOT EXISTS(SELECT 1 FROM creature_template WHERE entry=" +
			   N(r.entry) +
			   ") AND NOT EXISTS(SELECT 1 FROM "
			   "content_manager_server_resource_owner WHERE resource_kind=" +
			   T("creature-template.id") + " AND entry=" + N(r.entry) + ")";
	return "EXISTS(SELECT 1 FROM creature_template t JOIN "
		   "content_manager_server_resource_owner o ON o.entry=t.entry AND "
		   "o.resource_kind=" +
		   T("creature-template.id") + " WHERE t.entry=" + N(r.entry) +
		   " AND " + fields + " AND " +
		   OwnerIdentity("creature-template.id", r.entry, realm, r.packageKey,
						 r.symbol, Snapshot(r)) +
		   ")";
}
std::string ContentManagedServer::Condition(ResolvedGameObjectTemplate const &r,
											std::string const &realm,
											bool exists) {
	auto fields = "t.type=" + N(r.type) + " AND t.displayId=" + N(r.displayId) +
				  " AND t.name=" + Q(r.name) +
				  " AND t.IconName=" + Q(r.iconName) +
				  " AND t.castBarCaption=" + Q(r.castBarCaption) +
				  " AND t.unk1=" + Q(r.unk1) + " AND t.size=" + F(r.size);
	for (unsigned i = 0; i < 24; ++i)
		fields += " AND t.Data" + std::to_string(i) + "=" + N(r.data[i]);
	fields += " AND t.AIName=" + Q(r.aiName) +
			  " AND t.ScriptName=" + Q(r.scriptName) +
			  " AND t.VerifiedBuild=" + std::to_string(r.verifiedBuild);
	if (!exists)
		return "EXISTS(SELECT 1 FROM gameobject_template WHERE entry=" +
			   N(r.copyFrom) +
			   ") AND NOT EXISTS(SELECT 1 FROM gameobject_template WHERE "
			   "entry=" +
			   N(r.entry) +
			   ") AND NOT EXISTS(SELECT 1 FROM "
			   "content_manager_server_resource_owner WHERE resource_kind=" +
			   T("gameobject-template.id") + " AND entry=" + N(r.entry) + ")";
	return "EXISTS(SELECT 1 FROM gameobject_template t JOIN "
		   "content_manager_server_resource_owner o ON o.entry=t.entry AND "
		   "o.resource_kind=" +
		   T("gameobject-template.id") + " WHERE t.entry=" + N(r.entry) +
		   " AND " + fields + " AND " +
		   OwnerIdentity("gameobject-template.id", r.entry, realm, r.packageKey,
						 r.symbol, Snapshot(r)) +
		   ")";
}
std::string ContentManagedServer::Condition(ResolvedCreatureSpawn const &r,
											std::string const &realm,
											bool exists) {
	auto fields =
		"t.id=" + N(r.creatureEntry) + " AND t.map=" + N(r.map) +
		" AND t.spawnMask=" + N(r.spawnMask) +
		" AND t.phaseMask=" + N(r.phaseMask) + " AND t.position_x=" + F(r.x) +
		" AND t.position_y=" + F(r.y) + " AND t.position_z=" + F(r.z) +
		" AND t.orientation=" + F(r.orientation) +
		" AND t.spawntimesecs=" + N(r.respawnSeconds) +
		" AND t.wander_distance=" + F(r.wanderDistance) +
		" AND t.MovementType=" + N(r.movementType);
	if (!exists)
		return "NOT EXISTS(SELECT 1 FROM creature WHERE guid=" + N(r.guid) +
			   ") AND NOT EXISTS(SELECT 1 FROM "
			   "content_manager_server_resource_owner WHERE resource_kind=" +
			   T("creature-spawn.guid") + " AND entry=" + N(r.guid) + ")";
	return "EXISTS(SELECT 1 FROM creature t JOIN "
		   "content_manager_server_resource_owner o ON o.entry=t.guid AND "
		   "o.resource_kind=" +
		   T("creature-spawn.guid") + " WHERE t.guid=" + N(r.guid) + " AND " +
		   fields + " AND " +
		   OwnerIdentity("creature-spawn.guid", r.guid, realm, r.packageKey,
						 r.symbol, Snapshot(r)) +
		   ")";
}
bool ContentManagedServer::Check(ResolvedCreatureTemplate const &r,
								 std::string const &realm, bool &exists,
								 std::string &error) {
	return QueryCheck(r, realm, exists, error);
}
bool ContentManagedServer::Check(ResolvedGameObjectTemplate const &r,
								 std::string const &realm, bool &exists,
								 std::string &error) {
	return QueryCheck(r, realm, exists, error);
}
bool ContentManagedServer::Check(ResolvedCreatureSpawn const &r,
								 std::string const &realm, bool &exists,
								 std::string &error) {
	return QueryCheck(r, realm, exists, error);
}

std::vector<std::string>
ContentManagedServer::ApplySql(ResolvedCreatureTemplate const &r,
							   std::string const &realm, bool exists,
							   std::uint32_t build, std::string const &hash) {
	std::string const owner = "content_manager_server_resource_owner";
	if (exists)
		return {"UPDATE " + owner + " SET applied_build=" + N(build) +
				",artifact_sha256=" + T(hash) + " WHERE resource_kind=" +
				T("creature-template.id") + " AND entry=" + N(r.entry)};
	std::string columns =
		"entry,name,subname,minlevel,maxlevel,faction,npcflag,speed_walk,speed_"
		"run,`rank`,dmgschool,BaseAttackTime,RangeAttackTime,unit_class,unit_flags,"
		"type,type_flags,RegenHealth,flags_extra,AIName,"
		"ScriptName";
	std::string values =
		N(r.entry) + "," + Q(r.name) + "," + Q(r.subname) + "," +
		N(r.minLevel) + "," + N(r.maxLevel) + "," + N(r.faction) + "," +
		N(r.npcFlags) + "," + F(r.speedWalk) + "," + F(r.speedRun) + "," +
		N(r.rank) + "," + N(r.damageSchool) + "," +
		N(r.baseAttackTime) + "," + N(r.rangeAttackTime) + "," +
		N(r.unitClass) + "," + N(r.unitFlags) + "," + N(r.type) + "," +
		N(r.typeFlags) + "," + N(r.regenHealth) + "," +
		N(r.flagsExtra) + "," + Q(r.aiName) + "," + Q(r.scriptName);
	return {"INSERT INTO creature_template (" + columns + ") VALUES (" +
				values + ")",
			"INSERT INTO " + owner +
				"(resource_kind,entry,realm_name,package_key,symbol,row_json,"
				"applied_build,artifact_sha256) VALUES (" +
				T("creature-template.id") + "," + N(r.entry) + "," + T(realm) +
				"," + T(r.packageKey) + "," + T(r.symbol) + "," +
				T(Snapshot(r)) + "," + N(build) + "," + T(hash) + ")"};
}
std::vector<std::string>
ContentManagedServer::ApplySql(ResolvedGameObjectTemplate const &r,
							   std::string const &realm, bool exists,
							   std::uint32_t build, std::string const &hash) {
	std::string const owner = "content_manager_server_resource_owner";
	if (exists)
		return {"UPDATE " + owner + " SET applied_build=" + N(build) +
				",artifact_sha256=" + T(hash) + " WHERE resource_kind=" +
				T("gameobject-template.id") + " AND entry=" + N(r.entry)};
	std::string
		columns = "entry,type,displayId,name,IconName,castBarCaption,unk1,size",
		values = N(r.entry) + "," + N(r.type) + "," + N(r.displayId) + "," +
				 Q(r.name) + "," + Q(r.iconName) + "," + Q(r.castBarCaption) +
				 "," + Q(r.unk1) + "," + F(r.size);
	for (unsigned i = 0; i < 24; ++i) {
		columns += ",Data" + std::to_string(i);
		values += "," + N(r.data[i]);
	}
	columns += ",AIName,ScriptName,VerifiedBuild";
	values += "," + Q(r.aiName) + "," + Q(r.scriptName) + "," +
			  std::to_string(r.verifiedBuild);
	return {"INSERT INTO gameobject_template (" + columns + ") VALUES (" +
				values + ")",
			"INSERT INTO " + owner +
				"(resource_kind,entry,realm_name,package_key,symbol,row_json,"
				"applied_build,artifact_sha256) VALUES (" +
				T("gameobject-template.id") + "," + N(r.entry) + "," +
				T(realm) + "," + T(r.packageKey) + "," + T(r.symbol) + "," +
				T(Snapshot(r)) + "," + N(build) + "," + T(hash) + ")"};
}
std::vector<std::string>
ContentManagedServer::ApplySql(ResolvedCreatureSpawn const &r,
							   std::string const &realm, bool exists,
							   std::uint32_t build, std::string const &hash) {
	std::string const owner = "content_manager_server_resource_owner";
	if (exists)
		return {"UPDATE " + owner + " SET applied_build=" + N(build) +
				",artifact_sha256=" + T(hash) + " WHERE resource_kind=" +
				T("creature-spawn.guid") + " AND entry=" + N(r.guid)};
	auto insert = "INSERT INTO "
				  "creature(guid,id,map,spawnMask,phaseMask,position_x,"
				  "position_y,position_z,orientation,spawntimesecs,wander_"
				  "distance,MovementType) VALUES (" +
				  N(r.guid) + "," + N(r.creatureEntry) + "," + N(r.map) + "," +
				  N(r.spawnMask) + "," + N(r.phaseMask) + "," + F(r.x) + "," +
				  F(r.y) + "," + F(r.z) + "," + F(r.orientation) + "," +
				  N(r.respawnSeconds) + "," + F(r.wanderDistance) + "," +
				  N(r.movementType) + ")";
	return {insert, "INSERT INTO " + owner +
						"(resource_kind,entry,realm_name,package_key,symbol,"
						"row_json,applied_build,artifact_sha256) VALUES (" +
						T("creature-spawn.guid") + "," + N(r.guid) + "," +
						T(realm) + "," + T(r.packageKey) + "," + T(r.symbol) +
						"," + T(Snapshot(r)) + "," + N(build) + "," + T(hash) +
						")"};
}
bool ContentManagedServer::Verify(ResolvedCreatureTemplate const &r,
								  std::string const &realm, std::uint32_t build,
								  std::string const &hash, std::string &error) {
	return QueryVerify(r, realm, build, hash, error);
}
bool ContentManagedServer::Verify(ResolvedGameObjectTemplate const &r,
								  std::string const &realm, std::uint32_t build,
								  std::string const &hash, std::string &error) {
	return QueryVerify(r, realm, build, hash, error);
}
bool ContentManagedServer::Verify(ResolvedCreatureSpawn const &r,
								  std::string const &realm, std::uint32_t build,
								  std::string const &hash, std::string &error) {
	return QueryVerify(r, realm, build, hash, error);
}
