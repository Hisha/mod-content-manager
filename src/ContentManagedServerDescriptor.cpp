#include "ContentManagedServer.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <set>
#include <stdexcept>
#include <tuple>
using nlohmann::json;
namespace {
std::string FloatSql(float value) {
	if (!std::isfinite(value))
		throw std::runtime_error("Non-finite managed server value");
	std::ostringstream out;
	out << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
	return out.str();
}
json CreatureModels(ResolvedCreatureTemplate const &r) {
	json models = json::array();
	for (auto const &model : r.models)
		models.push_back(
			{{"CreatureID", r.entry},
			 {"Idx", model.index},
			 {"CreatureDisplayID", model.displayId},
			 {"DisplayScale", model.displayScale},
			 {"Probability", model.probability},
			 {"VerifiedBuild", model.verifiedBuild
							   ? json(*model.verifiedBuild)
							   : json(nullptr)}});
	return models;
}
json Creature(ResolvedCreatureTemplate const &r) {
	return {{"resourceKind", "creature-template.id"},
			{"package", r.packageKey},
			{"packageVersion", r.packageVersion},
			{"symbol", r.symbol},
			{"entry", r.entry},
			{"copyFrom", r.copyFrom},
			{"models", CreatureModels(r)},
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
json GameObject(ResolvedGameObjectTemplate const &r) {
	json f = {{"type", r.type},
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
		f["Data" + std::to_string(i)] = r.data[i];
	return {{"resourceKind", "gameobject-template.id"},
			{"package", r.packageKey},
			{"packageVersion", r.packageVersion},
			{"symbol", r.symbol},
			{"entry", r.entry},
			{"copyFrom", r.copyFrom},
			{"fields", f}};
}
json Spawn(ResolvedCreatureSpawn const &r) {
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
	if (!v.at("models").is_array())
		throw std::runtime_error("Invalid creature-template models");
	std::uint32_t previous = 0;
	bool first = true;
	for (auto const &source : v.at("models")) {
		ResolvedCreatureModel model;
		if (source.at("CreatureID") != r.entry)
			throw std::runtime_error("Creature model parent identity mismatch");
		model.index = source.at("Idx");
		model.displayId = source.at("CreatureDisplayID");
		model.displayScale = source.at("DisplayScale");
		model.probability = source.at("Probability");
		if (!source.at("VerifiedBuild").is_null())
			model.verifiedBuild = source.at("VerifiedBuild");
		if (model.index > 3 || (!first && model.index <= previous) ||
			!std::isfinite(model.displayScale) ||
			!std::isfinite(model.probability))
			throw std::runtime_error("Invalid creature-template model row");
		first = false;
		previous = model.index;
		r.models.push_back(model);
	}
	if (v.at("resourceKind") != "creature-template.id" || !r.entry ||
		!r.copyFrom || r.packageKey.empty() || r.packageVersion.empty() ||
		r.symbol.empty() || r.models.empty() || r.models.size() > 4)
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
} // namespace
std::string ContentManagedServer::CreatureModelConditionSql(
	ResolvedCreatureTemplate const &r) {
	auto number = [](std::uint32_t value) { return std::to_string(value); };
	std::string condition =
		"(SELECT COUNT(*) FROM creature_template_model m WHERE "
		"m.CreatureID=" + number(r.entry) + ")=" + number(r.models.size());
	for (auto const &model : r.models) {
		condition +=
			" AND EXISTS(SELECT 1 FROM creature_template_model m WHERE "
			"m.CreatureID=" + number(r.entry) + " AND m.Idx=" +
			number(model.index) + " AND m.CreatureDisplayID=" +
			number(model.displayId) + " AND m.DisplayScale=CAST(" +
			FloatSql(model.displayScale) +
			" AS FLOAT) AND m.Probability=CAST(" +
			FloatSql(model.probability) + " AS FLOAT)" +
			(model.verifiedBuild
				 ? " AND m.VerifiedBuild=" +
					   std::to_string(*model.verifiedBuild)
				 : " AND m.VerifiedBuild IS NULL") + ")";
	}
	return condition;
}
std::vector<std::string> ContentManagedServer::CreatureModelInsertSql(
	ResolvedCreatureTemplate const &r) {
	std::vector<std::string> sql;
	for (auto const &model : r.models)
		sql.push_back(
			"INSERT INTO creature_template_model (CreatureID,Idx,"
			"CreatureDisplayID,DisplayScale,Probability,VerifiedBuild) VALUES (" +
			std::to_string(r.entry) + "," + std::to_string(model.index) + "," +
			std::to_string(model.displayId) + "," +
			FloatSql(model.displayScale) + "," + FloatSql(model.probability) + "," +
			(model.verifiedBuild ? std::to_string(*model.verifiedBuild) : "NULL") +
			")");
	return sql;
}
std::string
ContentManagedServer::DescriptorFingerprint(std::string const &kind) {
	if (kind == "creature-template.id")
		return "6f8a44b7fa08e9130a48f886b3c8eacc05626fd8ec48818c3aaa079628ceaa6"
			   "d";
	if (kind == "gameobject-template.id")
		return "d9745969651d85c8a533f038d66ad1d2bdf8b0e1f020f171f427929a78094e2"
			   "8";
	if (kind == "creature-spawn.guid")
		return "71d3cfd4fa4332bf9d02da92f57b93fafa96f6b32a2d3b8f12b1c05e7e57533"
			   "4";
	throw std::runtime_error("Unknown managed server resource kind");
}
json ContentManagedServer::Objects(
	std::vector<ResolvedCreatureTemplate> creatures,
	std::vector<ResolvedGameObjectTemplate> gameObjects,
	std::vector<ResolvedCreatureSpawn> spawns) {
	auto less = [](auto const &a, auto const &b) {
		return std::tie(a.packageKey, a.symbol) <
			   std::tie(b.packageKey, b.symbol);
	};
	std::sort(creatures.begin(), creatures.end(), less);
	std::sort(gameObjects.begin(), gameObjects.end(), less);
	std::sort(spawns.begin(), spawns.end(), less);
	json result = {{"descriptorVersion", DescriptorVersion},
				   {"creatureTemplates", json::array()},
				   {"gameobjectTemplates", json::array()},
				   {"creatureSpawns", json::array()}};
	for (auto const &r : creatures)
		result["creatureTemplates"].push_back(Creature(r));
	for (auto const &r : gameObjects)
		result["gameobjectTemplates"].push_back(GameObject(r));
	for (auto const &r : spawns)
		result["creatureSpawns"].push_back(Spawn(r));
	return result;
}
void ContentManagedServer::Parse(
	json const &value, std::vector<ResolvedCreatureTemplate> &creatures,
	std::vector<ResolvedGameObjectTemplate> &gameObjects,
	std::vector<ResolvedCreatureSpawn> &spawns) {
	creatures.clear();
	gameObjects.clear();
	spawns.clear();
	if (!value.is_object() || value.size() != 4 ||
		value.at("descriptorVersion") != DescriptorVersion)
		throw std::runtime_error("Invalid managed-server descriptor");
	std::set<std::pair<std::string, std::string>> identities;
	std::set<std::uint32_t> entries;
	for (auto const &v : value.at("creatureTemplates")) {
		auto r = ParseCreature(v);
		if (!identities.emplace(r.packageKey, r.symbol).second ||
			!entries.insert(r.entry).second)
			throw std::runtime_error("Duplicate creature-template identity");
		creatures.push_back(r);
	}
	identities.clear();
	entries.clear();
	for (auto const &v : value.at("gameobjectTemplates")) {
		auto r = ParseGameObject(v);
		if (!identities.emplace(r.packageKey, r.symbol).second ||
			!entries.insert(r.entry).second)
			throw std::runtime_error("Duplicate gameobject-template identity");
		gameObjects.push_back(r);
	}
	identities.clear();
	entries.clear();
	for (auto const &v : value.at("creatureSpawns")) {
		auto r = ParseSpawn(v);
		if (!identities.emplace(r.packageKey, r.symbol).second ||
			!entries.insert(r.guid).second)
			throw std::runtime_error("Duplicate creature-spawn identity");
		spawns.push_back(r);
	}
	if (Objects(creatures, gameObjects, spawns) != value)
		throw std::runtime_error("Managed-server rows are not canonical");
}
