#include "ContentBuildHash.h"
#include "ContentPackage.h"
#include "ContentResourceAllocator.h"
#include "ContentServerBundle.h"
#include "DbcDescriptor.h"
#include "DbcReader.h"
#include "WorldMapDbcComposer.h"
#include "third_party/json/json.hpp"
#include "third_party/miniz/miniz.h"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using nlohmann::json;
namespace fs = std::filesystem;

// The real SHA-256 implementation needs OpenSSL. The parity artifact only needs
// the shape check, exactly as in the other standalone suites.
bool ContentBuildHash::Valid(std::string const &hash) {
	return hash.size() == 64 &&
		   hash.find_first_not_of("0123456789abcdef") == std::string::npos;
}

namespace
{
std::string const kHash(64, 'a');

fs::path Scratch()
{
	auto path = fs::temp_directory_path() / "content-world-map-tests";
	fs::remove_all(path);
	fs::create_directories(path);
	return path;
}

std::string ReadAll(fs::path const &path)
{
	std::ifstream input(path, std::ios::binary);
	assert(input.good());
	return std::string(std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

DbcDocument Stock(std::string const &table, fs::path const &directory)
{
	auto descriptor = FindDbcDescriptor(12340, table);
	assert(descriptor);
	auto read = DbcReader::Read(directory / (table + ".dbc"), *descriptor);
	assert(read.valid);
	return read.document;
}

void Save(fs::path const &path, json const &manifest)
{
	mz_zip_archive zip{};
	assert(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
	auto body = manifest.dump();
	assert(mz_zip_writer_add_mem(&zip, "manifest.json", body.data(), body.size(),
								 MZ_BEST_COMPRESSION));
	assert(mz_zip_writer_finalize_archive(&zip));
	mz_zip_writer_end(&zip);
}

bool Throws(std::function<void()> const &body)
{
	try {
		body();
	} catch (std::exception const &) {
		return true;
	}
	return false;
}

ContentWorldMap MinimalMap(std::uint32_t mapId = 36, std::uint32_t floorId = 900,
	std::uint32_t areaId = 800, std::uint32_t chunkId = 5000)
{
	ContentWorldMap map;
	map.mapId = mapId;
	map.transform =
		ContentWorldMapTransform{12, -1.0f, 1.0f, 2.0f, 3.0f, mapId, 0.0f, 0.0f,
			floorId};
	map.areas.push_back(ContentWorldMapArea{areaId, 42, "TestMap", 1.0f, 2.0f, 3.0f,
		4.0f, -1, 0, 0, {ContentDungeonMapFloor{floorId, 1, 0.0f, 0.0f, 0.0f, 0.0f, 1}},
		{ContentDungeonMapChunk{chunkId, 7, floorId, 0.0f}}});
	return map;
}

json BaseManifest()
{
	return json{{"schema", 3}, {"package", "test-world-map"},
				{"name", "Test World Map"}, {"version", "1.0.0"}};
}

// Raw JSON keeps the section's shape readable next to the EPF documentation.
json WorldMapManifest()
{
	return json::parse(R"({
      "schema": 3, "package": "test-world-map", "name": "Test World Map", "version": "1.0.0",
      "worldMaps": [
        {
          "mapId": 36,
          "transform": {"id": 12, "regionBottom": -1.0, "regionRight": 1.0, "regionTop": 2.0,
            "regionLeft": 3.0, "newMapId": 36, "regionOffsetX": 0.0, "regionOffsetY": 0.0,
            "newDungeonMapId": 900},
          "areas": [
            {"id": 800, "areaId": 42, "internalName": "TestMap", "y1": 1.0, "y2": 2.0,
             "x1": 3.0, "x2": 4.0, "virtualMapId": -1, "dungeonMapId": 0, "parentMapId": 0,
             "floors": [{"id": 900, "floor": 1, "field3": 0.0, "field4": 0.0, "field5": 0.0,
               "field6": 0.0, "field7": 1}],
             "chunks": [{"id": 5000, "field2": 7, "dungeonMapId": 900, "field4": 0.0}]}
          ]
        }
      ]
    })");
}

// A native instance map exactly as WDM Stable ships it: a WorldMapArea, its
// DungeonMap floors, its DungeonMapChunk rows and the artwork, and no
// WorldMapTransforms row. Gnomeregan (map 90, four floors), ShadowfangKeep (map
// 33, seven floors), BlackrockSpire (map 229, seven floors), ScarletMonastery
// (map 189, four floors), DireMaul (map 429, six floors), Scholomance (map 289,
// four floors), BlackTemple (map 564, seven floors and dungeonMapId -1),
// SunwellPlateau (map 580, dungeonMapId -1) and Karazhan (map 532, seventeen
// floors) are all of this shape in WDM Stable, so the absence of a transform is
// a real client shape rather than an invented one. The IDs are synthetic so no
// test ever collides with a stock or WDM row.
json NativeMapManifest(std::int32_t dungeonMapId = 0, std::size_t floorCount = 1)
{
	json floors = json::array();
	json chunks = json::array();
	for (std::size_t index = 0; index < floorCount; ++index)
	{
		floors.push_back({{"id", 900 + index}, {"floor", index + 1},
			{"field3", -100.0 - static_cast<double>(index)},
			{"field4", -200.0}, {"field5", -300.0}, {"field6", 40.0},
			{"field7", 39}});
		chunks.push_back({{"id", 5000 + index}, {"field2", 7},
			{"dungeonMapId", 900 + index}, {"field4", -10000.0}});
	}
	json area = {{"id", 691}, {"areaId", 940}, {"internalName", "Gnomeregan"},
		{"y1", -4500.0}, {"y2", -4800.0}, {"x1", -5200.0}, {"x2", -4900.0},
		{"virtualMapId", -1}, {"dungeonMapId", dungeonMapId}, {"parentMapId", 0},
		{"floors", floors}, {"chunks", chunks}};
	json map = {{"mapId", 90}, {"areas", json::array({area})}};
	return json{{"schema", 3}, {"package", "test-native-map"},
		{"name", "Test Native Map"}, {"version", "1.0.0"},
		{"worldMaps", json::array({map})}};
}

// Validation helper: every rejection must name the offending concept so an
// operator can act on the message instead of guessing.
std::string Reject(fs::path const &scratch, json const &manifest,
	std::string const &needle)
{
	auto path = scratch / "case.epf";
	fs::remove(path);
	Save(path, manifest);
	auto bad = ContentPackage(path).Validate();
	assert(!bad.valid);
	if (bad.error.find(needle) == std::string::npos) {
		std::cerr << "  expected error containing '" << needle << "' but got: "
				  << bad.error << "\n";
		assert(false);
	}
	return bad.error;
}

// The positive counterpart: a case that must be accepted keeps its own
// diagnostics so a rejected fixture is never mistaken for a broken assertion.
ContentPackageManifest Accept(fs::path const &scratch, std::string const &name,
	json const &manifest)
{
	auto const path = scratch / (name + ".epf");
	fs::remove(path);
	Save(path, manifest);
	auto const good = ContentPackage(path).Validate();
	if (!good.valid)
		std::cerr << "  expected " << name << " to validate: " << good.error << "\n";
	assert(good.valid);
	return good.manifest;
}

// Locates a node by a mixed string/index path so a rejection case can be built
// by patching one leaf of an otherwise valid manifest.
json *Node(json &manifest, std::vector<std::string> const &path)
{
	json *node = &manifest;
	for (auto const &step : path)
		node = node->is_array() ? &(*node)[std::stoul(step)] : &(*node)[step];
	return node;
}

json Replace(json manifest, std::vector<std::string> const &path, json value)
{
	*Node(manifest, path) = std::move(value);
	return manifest;
}

json With(json manifest, std::vector<std::string> const &path)
{
	Node(manifest, path)->operator[]("bogus") = 1;
	return manifest;
}

json Erase(json manifest, std::vector<std::string> const &path)
{
	Node(manifest, std::vector<std::string>(path.begin(), path.end() - 1))
		->erase(path.back());
	return manifest;
}
} // namespace

static void DescriptorTests()
{
	struct Expected { char const *table; std::size_t fields; std::size_t recordSize; };
	static Expected const expected[] = {{"DungeonMap", 8, 32},
		{"DungeonMapChunk", 5, 20}, {"WorldMapArea", 11, 44},
		{"WorldMapTransforms", 10, 40}};
	for (auto const &e : expected) {
		auto descriptor = FindDbcDescriptor(12340, e.table);
		assert(descriptor && descriptor->version == 1);
		assert(descriptor->fields.size() == e.fields);
		assert(e.recordSize == descriptor->fields.size() * 4);
		assert(std::string(descriptor->clientPath) ==
			   "DBFilesClient/" + std::string(e.table) + ".dbc");
		assert(descriptor->fields[0].type == DbcFieldType::UInt32);
		assert(IsKnownDbcTable(e.table));
		assert(WorldMapDbcComposer::ResourceKind(e.table).rfind(
				   "worldmap.", 0) == 0);
		assert(ContentBuildHash::Valid(
			WorldMapDbcComposer::VerifiedBaselineSha256(e.table)));
	}
	// The client-baked identity is the first column of each table, and the
	// WorldMapArea internal name is the only string column.
	assert(FindDbcDescriptor(12340, "WorldMapArea")->fields[3].type ==
		   DbcFieldType::StringOffset);
	assert(!FindDbcDescriptor(12340, "NotADbc"));
	// Only build 12340 compiles the world-map tables.
	assert(!FindDbcDescriptor(11723, "DungeonMap"));
	assert(FindDbcDescriptor(12340, "Item") != nullptr);
	assert(WorldMapDbcTables().size() == 4);
	std::set<std::string> kinds;
	for (auto const &table : WorldMapDbcTables())
		assert(kinds.insert(WorldMapDbcComposer::ResourceKind(table)).second);
	assert(Throws([] { (void)WorldMapDbcComposer::ResourceKind("Item"); }));
	assert(Throws([] { (void)WorldMapDbcComposer::VerifiedBaselineSha256("Item"); }));
	std::cout << "  world-map descriptors: PASS\n";
}

static void CompositionTests(fs::path const &baselineDirectory)
{
	DbcDocument const dungeonMap = Stock("DungeonMap", baselineDirectory);
	DbcDocument const chunk = Stock("DungeonMapChunk", baselineDirectory);
	DbcDocument const area = Stock("WorldMapArea", baselineDirectory);
	DbcDocument const transform = Stock("WorldMapTransforms", baselineDirectory);
	assert(dungeonMap.recordCount == 55 && dungeonMap.recordSize == 32);
	assert(chunk.recordCount == 622 && chunk.recordSize == 20);
	assert(area.recordCount == 108 && area.recordSize == 44);
	assert(area.stringBlockSize == 1303);
	assert(transform.recordCount == 9 && transform.recordSize == 40);

	std::vector<ResolvedWorldMap> const maps{{"pkg-a", MinimalMap()}};
	// Composition is a pure function of the verified baseline plus the ordered
	// input, and the declared row IDs are kept verbatim.
	for (auto const &table : WorldMapDbcTables()) {
		DbcDocument const &stock = table == "DungeonMap" ? dungeonMap
			: table == "DungeonMapChunk" ? chunk
			: table == "WorldMapArea" ? area : transform;
		auto once = WorldMapDbcComposer::Compose(table, stock, maps);
		assert(once == WorldMapDbcComposer::Compose(table, stock, maps));
		assert(Throws([&] {
			(void)WorldMapDbcComposer::Rows("Item", maps);
		}));
		assert(Throws([&] { (void)WorldMapDbcComposer::Inspect("Item", stock); }));
		// The stock records survive byte for byte and the count grows by one.
		auto parsed = DbcReader::Parse(once, *FindDbcDescriptor(12340, table));
		assert(parsed.valid);
		assert(parsed.document.recordCount == stock.recordCount + 1);
		assert(std::equal(stock.words.begin(), stock.words.end(),
			parsed.document.words.begin()));
		assert(std::equal(stock.strings.begin(), stock.strings.end(),
			parsed.document.strings.begin()));
		assert(WorldMapDbcComposer::Inspect(table, parsed.document).size() ==
			   stock.recordCount + 1);
		// Adding nothing reproduces the verified stock file exactly.
		assert(WorldMapDbcComposer::Compose(table, stock, {}) ==
			   DbcReader::Serialize(stock));
	}

	assert(WorldMapDbcComposer::Rows("DungeonMap", maps) ==
		   std::vector<std::uint32_t>{900});
	assert(WorldMapDbcComposer::Rows("DungeonMapChunk", maps) ==
		   std::vector<std::uint32_t>{5000});
	assert(WorldMapDbcComposer::Rows("WorldMapArea", maps) ==
		   std::vector<std::uint32_t>{800});
	assert(WorldMapDbcComposer::Rows("WorldMapTransforms", maps) ==
		   std::vector<std::uint32_t>{12});

	// The internal name is appended after every existing stock string, so no
	// stock offset can move.
	auto composed = WorldMapDbcComposer::Compose("WorldMapArea", area, maps);
	auto parsedArea =
		DbcReader::Parse(composed, *FindDbcDescriptor(12340, "WorldMapArea"));
	assert(parsedArea.valid);
	assert(parsedArea.document.stringBlockSize == area.stringBlockSize + 8);
	assert(std::string(reinterpret_cast<char const *>(
		parsedArea.document.strings.data() + area.stringBlockSize), 8) ==
		   std::string("TestMap\0", 8));
	assert(parsedArea.document.words[108 * 11 + 1] == 36);
	assert(parsedArea.document.words[108 * 11 + 2] == 42);
	assert(parsedArea.document.words[108 * 11 + 3] == area.stringBlockSize);
	// Two areas sharing one name intern a single string.
	auto shared = MinimalMap();
	shared.areas.push_back(shared.areas[0]);
	shared.areas[1].id = 801;
	shared.areas[1].chunks[0].id = 5001;
	auto sharedArea =
		DbcReader::Parse(WorldMapDbcComposer::Compose("WorldMapArea", area,
					{{"pkg-a", shared}}),
			*FindDbcDescriptor(12340, "WorldMapArea"));
	assert(sharedArea.valid);
	assert(sharedArea.document.stringBlockSize == area.stringBlockSize + 8);
	assert(sharedArea.document.words[109 * 11 + 3] == area.stringBlockSize);

	// Package order comes from the package key, never from the caller's order,
	// so a rebuild composes identical bytes.
	ContentWorldMap const second = MinimalMap(37, 901, 801, 5001);
	auto forward = WorldMapDbcComposer::Compose("DungeonMapChunk", chunk,
		{{"pkg-a", MinimalMap()}, {"pkg-b", second}});
	auto reversed = WorldMapDbcComposer::Compose("DungeonMapChunk", chunk,
		{{"pkg-b", second}, {"pkg-a", MinimalMap()}});
	assert(forward == reversed);
	auto parsedChunks = DbcReader::Parse(forward,
		*FindDbcDescriptor(12340, "DungeonMapChunk"));
	assert(parsedChunks.valid && parsedChunks.document.recordCount == 624);
	assert(parsedChunks.document.words[622 * 5 + 1] == 36);
	assert(parsedChunks.document.words[623 * 5 + 1] == 37);
	// Contributed rows keep declaration order and are never sorted: the
	// client-baked row order is part of the data.
	auto unsorted = MinimalMap();
	unsorted.areas[0].chunks[0].id = 2521;
	unsorted.areas[0].chunks[0].field2 = 246;
	auto declared = WorldMapDbcComposer::Compose("DungeonMapChunk", chunk,
		{{"pkg-a", unsorted}});
	auto parsedDeclared = DbcReader::Parse(declared,
		*FindDbcDescriptor(12340, "DungeonMapChunk"));
	assert(parsedDeclared.document.words[622 * 5] == 2521);
	assert(parsedDeclared.document.words[622 * 5 + 2] == 246);

	// Two packages claiming one row ID collide.
	assert(Throws([&] {
		(void)WorldMapDbcComposer::Compose("DungeonMapChunk", chunk,
			{{"pkg-a", MinimalMap()}, {"pkg-b", MinimalMap(37, 901, 801, 5000)}});
	}));
	// A stock row can never be edited in place: ID 34 is a live DungeonMap row.
	auto ontoStock = MinimalMap(36, 34, 800, 5000);
	assert(Throws([&] {
		(void)WorldMapDbcComposer::Compose("DungeonMap", dungeonMap,
			{{"pkg-a", ontoStock}});
	}));
	// Removing a package removes exactly its rows.
	assert(WorldMapDbcComposer::Compose("DungeonMapChunk", chunk, {}) ==
		   DbcReader::Serialize(chunk));

	// Row layout and identity guards.
	assert(WorldMapDbcComposer::FloorWords(36, {900, 1, 0, 0, 0, 0, 1}).size() == 8);
	assert(WorldMapDbcComposer::ChunkWords(36, {5000, 7, 900, 0}).size() == 5);
	assert(WorldMapDbcComposer::AreaWords(36, MinimalMap().areas[0], 1303).size() == 11);
	assert(WorldMapDbcComposer::TransformWords(36, *MinimalMap().transform).size() == 10);
	assert(Throws([] { (void)WorldMapDbcComposer::FloorWords(36, {0, 1, 0, 0, 0, 0, 1}); }));
	assert(Throws([] { (void)WorldMapDbcComposer::FloorWords(36, {900, 0, 0, 0, 0, 0, 1}); }));
	assert(Throws([] { (void)WorldMapDbcComposer::ChunkWords(0, {5000, 7, 900, 0}); }));
	assert(Throws([] {
		(void)WorldMapDbcComposer::ChunkWords(36, {5000, 7, 0, 0});
	}));
	assert(Throws([] {
		(void)WorldMapDbcComposer::TransformWords(36, {12, 0, 0, 0, 0, 0, 0, 0, 0});
	}));

	// A baseline whose shape does not match the descriptor is refused, as is one
	// carrying a duplicate or zero stock ID.
	auto truncated = chunk;
	truncated.recordCount = 621;
	assert(Throws([&] {
		(void)WorldMapDbcComposer::Inspect("DungeonMapChunk", truncated);
	}));
	auto duplicated = chunk;
	duplicated.words[5] = duplicated.words[0];
	assert(Throws([&] {
		(void)WorldMapDbcComposer::Inspect("DungeonMapChunk", duplicated);
	}));
	auto zeroId = chunk;
	zeroId.words[0] = 0;
	assert(Throws([&] {
		(void)WorldMapDbcComposer::Inspect("DungeonMapChunk", zeroId);
	}));
	// A stock string block must open with the empty string.
	auto noStrings = area;
	noStrings.strings[0] = 'x';
	assert(Throws([&] {
		(void)WorldMapDbcComposer::Inspect("WorldMapArea", noStrings);
	}));
	std::cout << "  world-map composition: PASS\n";
}

static void StagingTests(fs::path const &baselineDirectory)
{
	auto const scratch = Scratch() / "stage";
	auto const stock = Stock("DungeonMap", baselineDirectory);
	auto const composed = WorldMapDbcComposer::Compose("DungeonMap", stock,
		std::vector<ResolvedWorldMap>{{"pkg-a", MinimalMap()}});
	DbcDocument staged;
	std::string error;
	assert(WorldMapDbcComposer::Stage("DungeonMap", composed, scratch, staged, error));
	assert(staged.recordCount == 56);
	assert(fs::exists(scratch / "DBFilesClient" / "DungeonMap.dbc"));
	// Staging one table twice into a workspace is refused rather than silently
	// overwriting an artifact.
	assert(!WorldMapDbcComposer::Stage("DungeonMap", composed, scratch, staged, error));
	// An unsupported table never creates a target.
	assert(!WorldMapDbcComposer::Stage("Item", composed, scratch / "other", staged, error));
	assert(!fs::exists(scratch / "other" / "DBFilesClient" / "Item.dbc"));
	// Corrupt composed bytes fail the read-back instead of landing in the MPQ.
	assert(!WorldMapDbcComposer::Stage("DungeonMap", {1, 2, 3, 4}, scratch / "bad",
		staged, error));
	std::cout << "  world-map staging: PASS\n";
}

static void AllocationTests()
{
	auto const policy = ContentResourceAllocator::FixedRowIdPolicy(
		WorldMapDbcComposer::ResourceKind("DungeonMap"));
	assert(policy.version == 1);
	assert(policy.firstCandidate == 1 && policy.lastCandidate == 0xffffffffu);
	assert(WorldMapDbcComposer::ResourceKind("DungeonMap") !=
		   WorldMapDbcComposer::ResourceKind("WorldMapArea"));

	std::vector<ResourceAllocationRequest> const requests = {
		{"pkg-a", "floor/166", "worldmap.dungeon-map.id", 166},
		{"pkg-a", "floor/167", "worldmap.dungeon-map.id", 167}};
	// The declared ID is used verbatim: no searching, no pool.
	auto const plan =
		ContentResourceAllocator::PlanFixed("realm", policy, requests, {}, {}, 1, kHash);
	assert(plan.size() == 2);
	assert(plan[0].value == 166 && plan[1].value == 167);
	assert(plan[0].resourceKind == "worldmap.dungeon-map.id");
	assert(plan[0].policyVersion == 1 && plan[0].state == "reserved");
	assert(plan[0].packageKey == "pkg-a" && plan[0].symbol == "floor/166");
	// The verified stock baseline is occupied.
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"pkg-a", "x", "worldmap.dungeon-map.id", 34}}, {}, {1, 2, 34}, 1, kHash);
	}));
	// Another package already owns the ID.
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"pkg-b", "floor/166", "worldmap.dungeon-map.id", 166},
			 {"pkg-b", "floor/167", "worldmap.dungeon-map.id", 167}}, plan, {}, 1, kHash);
	}));
	// Explicit package consolidation may transfer one compatible fixed identity
	// even when the semantic path changed (the real Karazhan map index did).
	auto historical = plan.front();
	historical.packageKey = "mod-native-instance-maps.karazhan";
	historical.symbol = "worldmap/0/area/799/floor/383";
	historical.value = 383;
	historical.firstBuild = 49;
	historical.lastBuild = 52;
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "worldmap/1/area/799/floor/383",
			  "worldmap.dungeon-map.id", 383}}, {historical}, {}, 53, kHash);
	}));
	AllocationReplacements replacements = {{"mod-native-instance-maps",
		{"mod-native-instance-maps.karazhan"}}};
	auto migrated = ContentResourceAllocator::PlanFixed("realm", policy,
		{{"mod-native-instance-maps", "worldmap/1/area/799/floor/383",
		  "worldmap.dungeon-map.id", 383}}, {historical}, {}, 53, kHash, {},
		replacements);
	assert(migrated.size() == 1);
	assert(migrated[0].packageKey == "mod-native-instance-maps");
	assert(migrated[0].symbol == "worldmap/1/area/799/floor/383");
	assert(migrated[0].value == 383 && migrated[0].firstBuild == 49 &&
		migrated[0].lastBuild == 53);
	// Once persisted under the successor, ordinary rebuilds no longer need the
	// predecessor lease or special planning path.
	auto persisted = ContentResourceAllocator::PlanFixed("realm", policy,
		{{"mod-native-instance-maps", "worldmap/1/area/799/floor/383",
		  "worldmap.dungeon-map.id", 383}}, migrated, {}, 54, kHash);
	assert(persisted.size() == 1 && persisted[0].lastBuild == 54);
	// Declaring replacement of some other package does not authorize stealing.
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "new", "worldmap.dungeon-map.id", 383}},
			{historical}, {}, 53, kHash, {},
			{{"mod-native-instance-maps", {"unrelated-old"}}});
	}));
	// Baseline, kind and ambiguity protections remain fail-closed.
	auto incompatible = historical;
	incompatible.baselineSha256 = std::string(64, 'b');
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "new", "worldmap.dungeon-map.id", 383}},
			{incompatible}, {}, 53, kHash, {}, replacements);
	}));
	auto wrongKind = historical;
	wrongKind.resourceKind = "worldmap.world-map-area.id";
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "new", "worldmap.dungeon-map.id", 383}},
			{wrongKind}, {}, 53, kHash, {}, replacements);
	}));
	auto secondOwner = historical;
	secondOwner.packageKey = "another-proof-package";
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "new", "worldmap.dungeon-map.id", 383}},
			{historical, secondOwner}, {}, 53, kHash, {},
			{{"mod-native-instance-maps",
			  {"mod-native-instance-maps.karazhan", "another-proof-package"}}});
	}));
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "new-a", "worldmap.dungeon-map.id", 383},
			 {"mod-native-instance-maps", "new-b", "worldmap.dungeon-map.id", 383}},
			{historical}, {}, 53, kHash, {}, replacements);
	}));
	// A different package may take a neighbouring ID in the same table.
	assert(ContentResourceAllocator::PlanFixed("realm", policy,
		{{"pkg-b", "floor/168", "worldmap.dungeon-map.id", 168}}, plan, {}, 1, kHash)
		.front().value == 168);
	// One identity declared twice is a manifest error.
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"pkg-a", "floor/166", "worldmap.dungeon-map.id", 166},
			 {"pkg-a", "floor/166", "worldmap.dungeon-map.id", 167}}, {}, {}, 1, kHash);
	}));
	// One ID claimed by two symbols is a manifest error.
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"pkg-a", "a", "worldmap.dungeon-map.id", 166},
			 {"pkg-a", "b", "worldmap.dungeon-map.id", 166}}, {}, {}, 1, kHash);
	}));
	// A request for another table's kind is a policy mismatch.
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"pkg-a", "a", "worldmap.world-map-area.id", 166}}, {}, {}, 1, kHash);
	}));
	// A zero identity is refused.
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"pkg-a", "a", "worldmap.dungeon-map.id", 0}}, {}, {}, 1, kHash);
	}));
	// A rebuild with the same manifest keeps the same lease.
	auto const rebuilt =
		ContentResourceAllocator::PlanFixed("realm", policy, requests, plan, {}, 2, kHash);
	assert(rebuilt.size() == 2 && rebuilt[0].value == 166 && rebuilt[0].lastBuild == 2);
	// A rebuild whose manifest changed a declared ID is refused.
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"pkg-a", "floor/166", "worldmap.dungeon-map.id", 168},
			 {"pkg-a", "floor/167", "worldmap.dungeon-map.id", 167}}, plan, {}, 2, kHash);
	}));
	// A retired lease keeps its ID occupied so no other package can claim it.
	auto retired = plan;
	retired[0].state = "retired";
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"pkg-b", "floor/166", "worldmap.dungeon-map.id", 166}}, retired, {}, 3, kHash);
	}));
	// A lease pinned to another baseline is refused unless that baseline is in
	// the accepted history.
	auto pinned = plan;
	pinned[0].baselineSha256 = std::string(64, 'b');
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy, requests, pinned, {},
			3, kHash);
	}));
	assert(ContentResourceAllocator::PlanFixed("realm", policy, requests, pinned, {},
		3, kHash, {pinned[0].baselineSha256}).size() == 2);
	// Retained leases must belong to this realm and this kind.
	auto foreign = plan;
	foreign[0].realm = "other-realm";
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy, {}, foreign, {}, 4, kHash);
	}));
	// Leases of other kinds share the same registry and are left alone, so one
	// realm can hold spells, items and world-map rows at once.
	auto unrelated = plan;
	unrelated[0].resourceKind = "spell.id";
	assert(ContentResourceAllocator::PlanFixed("realm", policy, requests, unrelated, {},
		4, kHash).size() == 2);
	// A retained row ID outside the policy bounds is refused.
	auto outOfBounds = plan;
	outOfBounds[0].value = 0;
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy, {}, outOfBounds, {}, 4, kHash);
	}));
	// Request order does not change the plan.
	auto const reversed = ContentResourceAllocator::PlanFixed("realm", policy,
		{requests[1], requests[0]}, {}, {}, 1, kHash);
	assert(reversed.size() == 2 && reversed[0].value == 166 && reversed[1].value == 167);
	std::cout << "  world-map fixed-ID allocation: PASS\n";
}

// Regression: the first real build of a world-map package used to fail planning.
// The build service emitted two requests for every DungeonMap row -- one keyed by
// the floor alone and one keyed by the area that declares it -- so PlanFixed saw
// the package claiming its own ID under a second symbol and rejected the build.
// Planning is exercised here through the composer, which is the same request
// builder the build service calls, so the two can no longer drift apart.
// Regression: expanding mod-native-instance-maps from four maps to forty-three
// renumbered every map's manifest position, so Karazhan's authored floor stopped
// matching the symbol its own allocation had been persisted under. PlanFixed
// looked the lease up by (package, symbol, kind), missed, and then rejected the
// build because the package appeared to already own ID 383 under another symbol.
// The persisted row was correct the whole time. Only the row path identifies an
// authored row, so the composer now reports when two symbols name the same one
// and the planner reuses that lease verbatim.
static void RelocatedRowTests()
{
	auto const policy = ContentResourceAllocator::FixedRowIdPolicy(
		WorldMapDbcComposer::ResourceKind("DungeonMap"));
	auto const kind = policy.resourceKind;
	ItemAllocation persisted;
	persisted.realm = "realm";
	persisted.packageKey = "mod-native-instance-maps";
	persisted.symbol = "worldmap/1/area/799/floor/383";
	persisted.value = 383;
	persisted.state = "reserved";
	persisted.firstBuild = 49;
	persisted.lastBuild = 53;
	persisted.baselineSha256 = kHash;
	persisted.resourceKind = kind;
	persisted.policyVersion = 1;

	// Only the row path is identity; the manifest position is not.
	assert(WorldMapDbcComposer::SameRow("worldmap/1/area/799/floor/383",
		"worldmap/13/area/799/floor/383"));
	assert(WorldMapDbcComposer::SameRow("worldmap/0/area/799/floor/383",
		"worldmap/0/area/799/floor/383"));
	// A different authored row is never the same row, however similar it looks.
	assert(!WorldMapDbcComposer::SameRow("worldmap/1/area/799/floor/383",
		"worldmap/13/area/799/floor/384"));
	assert(!WorldMapDbcComposer::SameRow("worldmap/1/area/799/floor/383",
		"worldmap/13/area/800/floor/383"));
	assert(!WorldMapDbcComposer::SameRow("worldmap/1/area/799/floor/383",
		"worldmap/13/area/799/chunk/383"));
	// Malformed symbols are equivalent to nothing, so the rule cannot be
	// satisfied by an empty or hand-written path.
	assert(!WorldMapDbcComposer::SameRow("worldmap/1/area/799/floor/383", ""));
	assert(!WorldMapDbcComposer::SameRow("worldmap/1/area/799/floor/383",
		"worldmap//area/799/floor/383"));
	assert(!WorldMapDbcComposer::SameRow("worldmap/1/area/799/floor/383",
		"worldmap/1x/area/799/floor/383"));
	assert(!WorldMapDbcComposer::SameRow("worldmap/1/area/799/floor/383",
		"worldmap/1/"));
	assert(!WorldMapDbcComposer::SameRow("worldmap/1/area/799/floor/383",
		"floor/383"));
	assert(!WorldMapDbcComposer::SameRow("worldmap/1/area/799/floor/383",
		"worldmap/1/area/799/floor/383/extra"));

	// The rebuild that failed in production now reuses the canonical allocation
	// and keeps its persisted symbol, owner and first build.
	auto const reused = ContentResourceAllocator::PlanFixed("realm", policy,
		{{"mod-native-instance-maps", "worldmap/13/area/799/floor/383", kind, 383}},
		{persisted}, {}, 54, kHash, {}, {}, WorldMapDbcComposer::SameRow);
	assert(reused.size() == 1);
	assert(reused[0].packageKey == "mod-native-instance-maps");
	assert(reused[0].value == 383);
	assert(reused[0].symbol == "worldmap/1/area/799/floor/383");
	assert(reused[0].firstBuild == 49 && reused[0].lastBuild == 54);
	// Reusing the returned plan is stable: the same build plans identically and
	// a later build only advances last_build.
	auto const again = ContentResourceAllocator::PlanFixed("realm", policy,
		{{"mod-native-instance-maps", "worldmap/13/area/799/floor/383", kind, 383}},
		reused, {}, 55, kHash, {}, {}, WorldMapDbcComposer::SameRow);
	assert(again.size() == 1 && again[0].symbol == "worldmap/1/area/799/floor/383"
		&& again[0].firstBuild == 49 && again[0].lastBuild == 55);
	// A relocated row does not stop the build claiming further fixed rows.
	auto const grown = ContentResourceAllocator::PlanFixed("realm", policy,
		{{"mod-native-instance-maps", "worldmap/13/area/799/floor/383", kind, 383},
		 {"mod-native-instance-maps", "worldmap/14/area/756/floor/166", kind, 166}},
		reused, {}, 56, kHash, {}, {}, WorldMapDbcComposer::SameRow);
	assert(grown.size() == 2);
	// The plan is ordered by symbol, so worldmap/13 precedes worldmap/14.
	assert(grown[0].value == 383 && grown[0].symbol == "worldmap/1/area/799/floor/383"
		&& grown[0].firstBuild == 49 && grown[0].lastBuild == 56);
	assert(grown[1].value == 166 && grown[1].firstBuild == 56
		&& grown[1].lastBuild == 56);

	// Without the caller-supplied equivalence nothing is relaxed: the pre-fix
	// collision is still reported, so no other resource kind gains the rule.
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "worldmap/13/area/799/floor/383", kind, 383}},
			{persisted}, {}, 54, kHash);
	}));
	// The same package and row ID under a genuinely different row is a manifest
	// error, not a relocation, and keeps failing.
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "worldmap/13/area/800/floor/383", kind, 383}},
			{persisted}, {}, 54, kHash, {}, {}, WorldMapDbcComposer::SameRow);
	}));
	// The row path carries no row ID for a transform, so the declared ID is the
	// only thing that can pair a relocation up. A different transform ID is a
	// new row and must not adopt the retained one.
	auto transformPolicy = ContentResourceAllocator::FixedRowIdPolicy(
		WorldMapDbcComposer::ResourceKind("WorldMapTransforms"));
	auto transform = persisted;
	transform.symbol = "worldmap/1/transform";
	transform.value = 45;
	transform.resourceKind = transformPolicy.resourceKind;
	assert(WorldMapDbcComposer::SameRow("worldmap/1/transform", "worldmap/9/transform"));
	auto const movedTransform =
		ContentResourceAllocator::PlanFixed("realm", transformPolicy,
			{{"mod-native-instance-maps", "worldmap/9/transform",
			  transformPolicy.resourceKind, 45}},
			{transform}, {}, 54, kHash, {}, {}, WorldMapDbcComposer::SameRow);
	assert(movedTransform.size() == 1 && movedTransform[0].value == 45
		&& movedTransform[0].symbol == "worldmap/1/transform"
		&& movedTransform[0].firstBuild == 49);
	auto const otherTransform =
		ContentResourceAllocator::PlanFixed("realm", transformPolicy,
			{{"mod-native-instance-maps", "worldmap/9/transform",
			  transformPolicy.resourceKind, 46}},
			{transform}, {}, 54, kHash, {}, {}, WorldMapDbcComposer::SameRow);
	assert(otherTransform.size() == 1 && otherTransform[0].value == 46
		&& otherTransform[0].symbol == "worldmap/9/transform"
		&& otherTransform[0].firstBuild == 54);
	// One retained row may not be adopted by two declarations in one build.
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "worldmap/13/area/799/floor/383", kind, 383},
			 {"mod-native-instance-maps", "worldmap/4/area/799/floor/383", kind, 383}},
			{persisted}, {}, 54, kHash, {}, {}, WorldMapDbcComposer::SameRow);
	}));
	// Relocation never crosses an owner: another package's row stays its own.
	auto foreign = persisted;
	foreign.packageKey = "mod-another-package";
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "worldmap/13/area/799/floor/383", kind, 383}},
			{foreign}, {}, 54, kHash, {}, {}, WorldMapDbcComposer::SameRow);
	}));
	// Every validation a fresh allocation would face still applies, because a
	// relocated row is checked exactly like an inherited one.
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "worldmap/13/area/799/floor/383", kind, 383}},
			{persisted}, {383}, 54, kHash, {}, {}, WorldMapDbcComposer::SameRow);
	}));
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "worldmap/13/area/799/floor/383", kind, 383}},
			{persisted}, {}, 54, std::string(64, 'b'), {}, {},
			WorldMapDbcComposer::SameRow);
	}));
	auto wrongPolicy = persisted;
	wrongPolicy.policyVersion = 2;
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", policy,
			{{"mod-native-instance-maps", "worldmap/13/area/799/floor/383", kind, 383}},
			{wrongPolicy}, {}, 54, kHash, {}, {}, WorldMapDbcComposer::SameRow);
	}));
	// Resource kinds are independent allocation spaces, so a retained row can
	// never be adopted across kinds: the DungeonMap request below is planned as
	// a fresh lease rather than reusing the transform row.
	auto const crossKind = ContentResourceAllocator::PlanFixed("realm", policy,
		{{"mod-native-instance-maps", "worldmap/13/area/799/floor/45", kind, 45}},
		{transform}, {}, 54, kHash, {}, {}, WorldMapDbcComposer::SameRow);
	assert(crossKind.size() == 1 && crossKind[0].value == 45
		&& crossKind[0].symbol == "worldmap/13/area/799/floor/45"
		&& crossKind[0].firstBuild == 54);
	std::cout << "  world-map relocated row reuse: PASS\n";
}

static void FirstBuildPlanningTests()
{
	auto const fixture = fs::path(__FILE__).parent_path() / "fixtures" / "deadmines";
	json const authored = json::parse(ReadAll(fixture / "manifest.json"));
	auto content = BaseManifest();
	for (auto const &key : {"package", "name", "version"})
		content[key] = authored.at(key);
	content["worldMaps"] = authored.at("worldMaps");
	auto const scratch = fs::temp_directory_path() / "deadmines-planning-test.epf";
	fs::remove(scratch);
	Save(scratch, content);
	auto const validated = ContentPackage(scratch).Validate();
	if (!validated.valid) std::cerr << "  deadmines manifest rejected: " << validated.error << "\n";
	assert(validated.valid);
	auto const &maps = validated.manifest.worldMaps;
	assert(maps.size() == 1 && maps[0].areas.size() == 1);
	auto const &area = maps[0].areas[0];
	assert(area.floors.size() == 2 && area.chunks.size() == 29);

	std::map<std::string, std::vector<ResourceAllocationRequest>> requests;
	WorldMapDbcComposer::AppendRequests(validated.manifest.packageKey, maps, requests);
	// Exactly one request per authored row, and no other table is touched.
	assert(requests.size() == WorldMapDbcTables().size());
	for (auto const& table : WorldMapDbcTables())
		assert(requests.count(table));
	assert(requests.at("DungeonMap").size() == area.floors.size());
	assert(requests.at("DungeonMapChunk").size() == area.chunks.size());
	assert(requests.at("WorldMapArea").size() == maps[0].areas.size());
	assert(requests.at("WorldMapTransforms").size() == maps.size());
	// A DungeonMap row belongs to the area that declares the floor, so its
	// symbol carries that area and no area-less alias exists for the same ID.
	for (auto const& request : requests.at("DungeonMap"))
		assert(request.symbol.find("dungeonmap/") == std::string::npos);
	assert(requests.at("DungeonMap")[0].symbol ==
		"worldmap/0/area/" + std::to_string(area.id) + "/floor/166");
	// No row ID is claimed twice under two different symbols of one package:
	// that is exactly what the planner rejects.
	for (auto const& table : WorldMapDbcTables()) {
		std::set<std::string> identities;
		for (auto const& request : requests.at(table)) {
			assert(request.packageKey == validated.manifest.packageKey);
			assert(request.resourceKind ==
				WorldMapDbcComposer::ResourceKind(table));
			assert(identities.insert(request.packageKey + "/" +
				std::to_string(request.fixedValue)).second);
		}
	}

	// The first build: nothing retained, nothing occupied by another package.
	for (auto const& table : WorldMapDbcTables()) {
		auto const kind = WorldMapDbcComposer::ResourceKind(table);
		auto const plan = ContentResourceAllocator::PlanFixed("realm",
			ContentResourceAllocator::FixedRowIdPolicy(kind), requests.at(table), {},
			{}, 1, WorldMapDbcComposer::VerifiedBaselineSha256(table));
		assert(plan.size() == requests.at(table).size());
		for (auto const& lease : plan) {
			assert(lease.value != 0);
			assert(lease.state == "reserved" && lease.firstBuild == 1);
			assert(lease.baselineSha256 ==
				WorldMapDbcComposer::VerifiedBaselineSha256(table));
		}
	}
	// The second build reuses the same leases and adds nothing.
	for (auto const& table : WorldMapDbcTables()) {
		auto const kind = WorldMapDbcComposer::ResourceKind(table);
		auto const first = ContentResourceAllocator::PlanFixed("realm",
			ContentResourceAllocator::FixedRowIdPolicy(kind), requests.at(table), {},
			{}, 1, WorldMapDbcComposer::VerifiedBaselineSha256(table));
		auto const second = ContentResourceAllocator::PlanFixed("realm",
			ContentResourceAllocator::FixedRowIdPolicy(kind), requests.at(table), first,
			{}, 2, WorldMapDbcComposer::VerifiedBaselineSha256(table));
		assert(second.size() == first.size());
		for (auto i = 0u; i < first.size(); ++i) {
			assert(second[i].value == first[i].value);
			assert(second[i].symbol == first[i].symbol);
			assert(second[i].lastBuild == 2);
		}
	}
	// The area-less alias that caused the failure is still refused, so the fix
	// removes the duplicate request instead of weakening the collision rule.
	auto const dungeonMapPolicy = ContentResourceAllocator::FixedRowIdPolicy(
		WorldMapDbcComposer::ResourceKind("DungeonMap"));
	auto alias = requests.at("DungeonMap");
	alias.push_back({validated.manifest.packageKey, "worldmap/0/dungeonmap/166",
		WorldMapDbcComposer::ResourceKind("DungeonMap"), 166});
	assert(Throws([&] {
		(void)ContentResourceAllocator::PlanFixed("realm", dungeonMapPolicy, alias,
			{}, {}, 1, kHash);
	}));
	// The real stock IDs stay rejected for the real request set.
	auto stock = ContentResourceAllocator::PlanFixed("realm", dungeonMapPolicy,
		requests.at("DungeonMap"), {}, {}, 1, kHash);
	for (auto const& lease : stock)
		assert(Throws([&] {
			(void)ContentResourceAllocator::PlanFixed("realm", dungeonMapPolicy,
				{{"other-package", "x", lease.resourceKind, lease.value}}, stock, {}, 2,
				kHash);
		}));
	std::cout << "  world-map first-build planning: PASS\n";
}

// A native instance map that declares no transform is the ordinary shape of the
// data. It must reach DBC composition, and it must not conjure a transform on
// the way: no row, no fixed ID, no lease, and WorldMapTransforms left exactly as
// the verified stock file.
static void NoTransformTests(fs::path const &scratch, fs::path const &baselineDirectory)
{
	auto const validated = Accept(scratch, "native-map", NativeMapManifest());
	assert(validated.worldMaps.size() == 1);
	auto const& map = validated.worldMaps[0];
	assert(map.mapId == 90);
	// Absent, not defaulted: no zeroed transform is standing in for one.
	assert(!map.transform);
	auto const& area = map.areas[0];
	assert(area.id == 691 && area.dungeonMapId == 0 && area.virtualMapId == -1);
	assert(area.floors.size() == 1 && area.chunks.size() == 1);

	// No transform request exists at all, so the build never plans that table.
	std::map<std::string, std::vector<ResourceAllocationRequest>> requests;
	WorldMapDbcComposer::AppendRequests(validated.packageKey, validated.worldMaps,
		requests);
	assert(!requests.count("WorldMapTransforms"));
	assert(requests.at("DungeonMap").size() == 1);
	assert(requests.at("DungeonMapChunk").size() == 1);
	assert(requests.at("WorldMapArea").size() == 1);
	for (auto const& request : requests.at("DungeonMap"))
		assert(request.fixedValue == 900);

	// Fixed-ID planning succeeds for exactly the three tables the map owns.
	for (auto const& table : {"DungeonMap", "DungeonMapChunk", "WorldMapArea"}) {
		auto const plan = ContentResourceAllocator::PlanFixed("realm",
			ContentResourceAllocator::FixedRowIdPolicy(
				WorldMapDbcComposer::ResourceKind(table)),
			requests.at(table), {}, {}, 1,
			WorldMapDbcComposer::VerifiedBaselineSha256(table));
		assert(plan.size() == requests.at(table).size());
		for (auto const& lease : plan) {
			assert(lease.value != 0);
			assert(lease.state == "reserved" && lease.firstBuild == 1);
			assert(lease.baselineSha256 ==
				WorldMapDbcComposer::VerifiedBaselineSha256(table));
		}
	}

	std::vector<ResolvedWorldMap> const maps{{validated.packageKey, map}};
	assert(WorldMapDbcComposer::Rows("DungeonMap", maps) ==
		std::vector<std::uint32_t>{900});
	assert(WorldMapDbcComposer::Rows("DungeonMapChunk", maps) ==
		std::vector<std::uint32_t>{5000});
	assert(WorldMapDbcComposer::Rows("WorldMapArea", maps) ==
		std::vector<std::uint32_t>{691});
	// Nothing is composed for the transform table, so the stock file is returned
	// byte for byte rather than a synthesised row appended to it.
	assert(WorldMapDbcComposer::Rows("WorldMapTransforms", maps).empty());
	auto const stockTransforms = Stock("WorldMapTransforms", baselineDirectory);
	assert(WorldMapDbcComposer::Compose("WorldMapTransforms", stockTransforms, maps) ==
		DbcReader::Serialize(stockTransforms));

	// The other three tables compose normally: the stock rows survive, the
	// declared rows follow, and the composed record carries exactly the authored
	// words. "@" stands for the appended internal-name string offset, which
	// depends on the stock string block.
	struct Expected { char const* table; std::size_t stock; char const* words; };
	static Expected const expected[] = {
		{"DungeonMap", 55, "0x00000384 0x0000005A 0x00000001 0xC2C80000 0xC3480000 "
			"0xC3960000 0x42200000 0x00000027"},
		{"DungeonMapChunk", 622, "0x00001388 0x0000005A 0x00000007 0x00000384 0xC61C4000"},
		{"WorldMapArea", 108, "0x000002B3 0x0000005A 0x000003AC @ 0xC58CA000 "
			"0xC5960000 0xC5A28000 0xC5992000 0xFFFFFFFF 0x00000000 0x00000000"}};
	for (auto const& e : expected) {
		auto const stock = Stock(e.table, baselineDirectory);
		auto const composed = WorldMapDbcComposer::Compose(e.table, stock, maps);
		auto const parsed = DbcReader::Parse(composed, *FindDbcDescriptor(12340, e.table));
		if (!parsed.valid) std::cerr << "  " << e.table << " did not reparse\n";
		assert(parsed.valid);
		assert(parsed.document.recordCount == e.stock + 1);
		assert(std::equal(stock.words.begin(), stock.words.end(),
			parsed.document.words.begin()));
		assert(std::equal(stock.strings.begin(), stock.strings.end(),
			parsed.document.strings.begin()));
		auto const fields = FindDbcDescriptor(12340, e.table)->fields.size();
		std::vector<std::uint32_t> const row(
			parsed.document.words.begin() + e.stock * fields,
			parsed.document.words.begin() + (e.stock + 1) * fields);
		std::ostringstream declared;
		for (std::size_t index = 0; index < row.size(); ++index)
			declared << (index ? " " : "") << "0x" << std::hex << std::uppercase
				<< std::setw(8) << std::setfill('0') << row[index];
		std::string const text = declared.str();
		std::ostringstream offset;
		offset << "0x" << std::hex << std::uppercase << std::setw(8)
			<< std::setfill('0') << stock.stringBlockSize;
		std::string const wanted = std::string(e.words).find('@') == std::string::npos
			? std::string(e.words)
			: std::string(e.words).substr(0, std::string(e.words).find('@')) +
				offset.str() + std::string(e.words).substr(
					std::string(e.words).find('@') + 1);
		if (text != wanted)
			std::cerr << "  " << e.table << " composed row " << text << " != "
					  << wanted << "\n";
		assert(text == wanted);
		if (e.table == std::string("WorldMapArea")) {
			// The internal name is appended after every stock string, so no
			// existing offset can move.
			assert(parsed.document.stringBlockSize == stock.stringBlockSize + 11);
			assert(std::string(reinterpret_cast<char const*>(
				parsed.document.strings.data() + stock.stringBlockSize), 11) ==
				std::string("Gnomeregan\0", 11));
		}
	}

	// Parity covers exactly the three tables that were composed, so no
	// WorldMapTransforms hash is recorded and none is required.
	std::map<std::string, std::string> hashes;
	std::vector<ContentBaseline> baselines;
	std::vector<ItemAllocation> retained;
	for (auto const& table : {"DungeonMap", "DungeonMapChunk", "WorldMapArea"}) {
		auto const plan = ContentResourceAllocator::PlanFixed("realm",
			ContentResourceAllocator::FixedRowIdPolicy(
				WorldMapDbcComposer::ResourceKind(table)),
			requests.at(table), {}, {}, 12,
			WorldMapDbcComposer::VerifiedBaselineSha256(table));
		retained.insert(retained.end(), plan.begin(), plan.end());
		hashes[table] = WorldMapDbcComposer::VerifiedBaselineSha256(table);
		ContentBaseline snapshot;
		snapshot.table = table;
		snapshot.clientBuild = 12340;
		snapshot.descriptorVersion = FindDbcDescriptor(12340, table)->version;
		snapshot.hash = hashes[table];
		baselines.push_back(snapshot);
	}
	auto const parity = ContentServerBundle::ParityJson("realm", 12, retained, {},
		"", "", "mpq", "server", "", "", {}, baselines, "", {}, {}, {}, {}, {}, {},
		"", false, hashes);
	auto const artifact = json::parse(parity);
	assert(!artifact.at("worldMapDbcSha256").count("WorldMapTransforms"));
	std::string error;
	auto const declared = ContentServerBundle::ReadParityAllocations(parity, retained);
	assert(declared.worldMapDbcSha256 == hashes);
	if (!ContentServerBundle::VerifyParity(parity, "realm", 12, "", "mpq", "server",
		{}, declared.allocations, error, {}, {}, {}, {}, {}, {}, declared.worldMapDbcSha256))
		std::cerr << "  no-transform parity refused: " << error << "\n";
	assert(ContentServerBundle::VerifyParity(parity, "realm", 12, "", "mpq", "server",
		{}, declared.allocations, error, {}, {}, {}, {}, {}, {}, declared.worldMapDbcSha256));
	// Recording a transform hash for a table this package never composed is
	// refused: there is no lease to account for it.
	{
		auto forged = hashes;
		forged["WorldMapTransforms"] =
			WorldMapDbcComposer::VerifiedBaselineSha256("WorldMapTransforms");
		assert(!ContentServerBundle::VerifyParity(parity, "realm", 12, "", "mpq",
			"server", {}, declared.allocations, error, {}, {}, {}, {}, {}, {}, forged));
	}
	// A lease for a transform the package never declared is an orphan.
	{
		auto orphaned = retained;
		orphaned.push_back(ContentResourceAllocator::PlanFixed("realm",
			ContentResourceAllocator::FixedRowIdPolicy(
				WorldMapDbcComposer::ResourceKind("WorldMapTransforms")),
			{{validated.packageKey, "worldmap/0/transform",
			  WorldMapDbcComposer::ResourceKind("WorldMapTransforms"), 900}},
			{}, {}, 12,
			WorldMapDbcComposer::VerifiedBaselineSha256("WorldMapTransforms")).front());
		assert(!ContentServerBundle::VerifyParity(parity, "realm", 12, "", "mpq",
			"server", {}, orphaned, error, {}, {}, {}, {}, {}, {}, hashes));
	}
	std::cout << "  world-map without a transform: PASS\n";
}

// Several floors never imply a transform. Karazhan has seventeen and none in
// WDM Stable, so the model must not require one merely because floors.size() > 1.
static void MultiFloorNoTransformTests(fs::path const &scratch,
	fs::path const &baselineDirectory)
{
	auto const validated = Accept(scratch, "native-multifloor",
		NativeMapManifest(0, 2));
	assert(validated.worldMaps.size() == 1);
	auto const& map = validated.worldMaps[0];
	assert(!map.transform);
	assert(map.areas[0].floors.size() == 2 && map.areas[0].chunks.size() == 2);
	assert(map.areas[0].floors[0].id == 900 && map.areas[0].floors[1].id == 901);
	assert(map.areas[0].chunks[0].dungeonMapId == 900 &&
		   map.areas[0].chunks[1].dungeonMapId == 901);

	std::map<std::string, std::vector<ResourceAllocationRequest>> requests;
	WorldMapDbcComposer::AppendRequests(validated.packageKey, validated.worldMaps,
		requests);
	assert(!requests.count("WorldMapTransforms"));
	assert(requests.at("DungeonMap").size() == 2);

	std::vector<ResolvedWorldMap> const maps{{validated.packageKey, map}};
	std::vector<std::uint32_t> const twoFloors{900, 901};
	assert(WorldMapDbcComposer::Rows("DungeonMap", maps) == twoFloors);
	assert(WorldMapDbcComposer::Rows("WorldMapTransforms", maps).empty());

	auto const stock = Stock("DungeonMap", baselineDirectory);
	auto const parsed = DbcReader::Parse(
		WorldMapDbcComposer::Compose("DungeonMap", stock, maps),
		*FindDbcDescriptor(12340, "DungeonMap"));
	assert(parsed.valid && parsed.document.recordCount == stock.recordCount + 2);
	// Both floors carry their own authored Floor value, in declaration order.
	assert(parsed.document.words[55 * 8 + 0] == 900 &&
		   parsed.document.words[55 * 8 + 2] == 1);
	assert(parsed.document.words[56 * 8 + 0] == 901 &&
		   parsed.document.words[56 * 8 + 2] == 2);
	assert(WorldMapDbcComposer::Compose("WorldMapTransforms",
		Stock("WorldMapTransforms", baselineDirectory), maps) ==
		DbcReader::Serialize(Stock("WorldMapTransforms", baselineDirectory)));

	// BlackTemple's shape: several floors, no transform, and dungeonMapId -1.
	auto const sentinel = Accept(scratch, "native-multifloor-sentinel",
		NativeMapManifest(-1, 2));
	assert(!sentinel.worldMaps[0].transform);
	assert(sentinel.worldMaps[0].areas[0].dungeonMapId == -1);
	assert(sentinel.worldMaps[0].areas[0].floors.size() == 2);
	std::cout << "  world-map multi-floor without a transform: PASS\n";
}

// areas[].dungeonMapId is a reference field. Stock 3.3.5a and WDM Stable both
// carry 0 and -1 there, and WDM points one area (AhnQiraj, map 531) at a
// DungeonMap row owned by a different map (ID 2, map 574). All of it must be
// representable and preserved exactly, and none of it may become a claim on the
// row it names.
static void DungeonMapIdReferenceTests(fs::path const &scratch)
{
	// 0 and -1 are the stock spellings; both validate and both survive verbatim.
	for (auto const value : {0, -1})
	{
		auto const validated = Accept(scratch, "dungeonmapid-" +
			std::to_string(value), NativeMapManifest(value));
		assert(validated.worldMaps[0].areas[0].dungeonMapId == value);
	}
	// A reference to a DungeonMap row this package does not own is legitimate
	// and is never turned into an ownership claim.
	{
		auto manifest = NativeMapManifest();
		// AhnQiraj's shape: an area naming a floor of another map entirely.
		manifest["worldMaps"][0]["areas"][0]["dungeonMapId"] = 2;
		auto const validated = Accept(scratch, "dungeonmapid-external", manifest);
		assert(validated.worldMaps[0].areas[0].dungeonMapId == 2);
		std::map<std::string, std::vector<ResourceAllocationRequest>> requests;
		WorldMapDbcComposer::AppendRequests(validated.packageKey,
			validated.worldMaps, requests);
		// The referenced row is neither requested nor leased: naming it is not
		// owning it.
		assert(requests.at("DungeonMap").size() == 1);
		for (auto const& request : requests.at("DungeonMap"))
			assert(request.fixedValue == 900);
		auto const leases = ContentResourceAllocator::PlanFixed("realm",
			ContentResourceAllocator::FixedRowIdPolicy(
				WorldMapDbcComposer::ResourceKind("DungeonMap")),
			requests.at("DungeonMap"), {}, {}, 1,
			WorldMapDbcComposer::VerifiedBaselineSha256("DungeonMap"));
		assert(leases.size() == 1 && leases[0].value == 900);
	}
	// A reference to a floor this package owns under a different world map is
	// still refused: the client would read that row as if it belonged here. A
	// floor can only be owned once, so the second map declares its own row.
	{
		auto manifest = NativeMapManifest();
		manifest["worldMaps"][0]["areas"][0]["dungeonMapId"] = 902;
		manifest["worldMaps"].push_back(json::parse(R"({
          "mapId": 91,
          "areas": [
            {"id": 692, "areaId": 941, "internalName": "GnomereganTwo",
             "y1": -4500.0, "y2": -4800.0, "x1": -5200.0, "x2": -4900.0,
             "virtualMapId": -1, "dungeonMapId": 0, "parentMapId": 0,
             "floors": [{"id": 902, "floor": 1, "field3": 0.0, "field4": 0.0,
               "field5": 0.0, "field6": 0.0, "field7": 39}],
             "chunks": [{"id": 5010, "field2": 7, "dungeonMapId": 902, "field4": 0.0}]}
          ]
        })"));
		Reject(scratch, manifest, "which this package owns under world map 91");
	}
	// The field is still a typed signed 32-bit value: no float, no string, and
	// nothing outside the range the record can hold.
	for (auto const& value : std::vector<json>{1.5, "2", 4294967296LL, -4294967296LL})
		Reject(scratch, Replace(NativeMapManifest(), {"worldMaps", "0", "areas", "0",
			"dungeonMapId"}, value), "worldMaps area requires");
	// A chunk's floor is a different relationship: a chunk draws a floor of its
	// own map, so a reference outside the map stays refused. WDM Stable resolves
	// all 1932 of its chunk references inside their own map.
	Reject(scratch, Replace(NativeMapManifest(), {"worldMaps", "0", "areas", "0",
		"chunks", "0", "dungeonMapId"}, 901), "which is not a floor of world map 90");
	std::cout << "  world-map dungeonMapId reference: PASS\n";
}

// The reference value reaches the record exactly as authored: 0 stays 0, -1
// serializes to its raw uint32 representation, and an external ID is stored
// unchanged. No truncation, no sign corruption, no rewriting.
static void DungeonMapIdCompositionTests(fs::path const &scratch,
	fs::path const &baselineDirectory)
{
	DbcDocument const stock = Stock("WorldMapArea", baselineDirectory);
	auto composed = [&](std::int32_t value) {
		auto const validated = Accept(scratch, "dungeonmapid-composed-" +
			std::to_string(value), NativeMapManifest(value));
		std::vector<ResolvedWorldMap> const maps{{validated.packageKey,
			validated.worldMaps[0]}};
		auto const parsed = DbcReader::Parse(
			WorldMapDbcComposer::Compose("WorldMapArea", stock, maps),
			*FindDbcDescriptor(12340, "WorldMapArea"));
		assert(parsed.valid);
		assert(parsed.document.recordCount == stock.recordCount + 1);
		assert(std::equal(stock.words.begin(), stock.words.end(),
			parsed.document.words.begin()));
		return parsed.document;
	};
	// Word 9 is dungeonMap_id and word 8 is virtual_map_id; both are int32 in the
	// descriptor, so -1 must arrive as 0xFFFFFFFF rather than 0 or 1.
	auto const zero = composed(0);
	assert(zero.words[108 * 11 + 8] == 0xFFFFFFFFu);
	assert(zero.words[108 * 11 + 9] == 0u);
	auto const sentinel = composed(-1);
	assert(sentinel.words[108 * 11 + 8] == 0xFFFFFFFFu);
	assert(sentinel.words[108 * 11 + 9] == 0xFFFFFFFFu);
	auto const external = composed(2);
	assert(external.words[108 * 11 + 9] == 2u);
	// Composition is still a pure function of the baseline plus the input.
	assert(composed(-1).words == sentinel.words);
	std::cout << "  world-map dungeonMapId serialization: PASS\n";
}

static void PackageTests(fs::path const &scratch)
{
	auto const manifest = WorldMapManifest();
	auto const path = scratch / "valid.epf";
	Save(path, manifest);
	auto const result = ContentPackage(path).Validate();
	assert(result.valid);
	assert(result.manifest.worldMaps.size() == 1);
	auto const &area = result.manifest.worldMaps[0].areas[0];
	assert(area.floors.size() == 1 && area.chunks.size() == 1);
	assert(area.dungeonMapId == 0 && area.virtualMapId == -1);
	assert(result.manifest.worldMaps[0].transform->newDungeonMapId == 900);

	// Schema 1 has no worldMaps section at all.
	Reject(scratch, Replace(manifest, {"schema"}, 1), "Schema 2");
	// Closed keys at every level of the section.
	for (auto const &path : {std::vector<std::string>{"worldMaps", "0"},
			 std::vector<std::string>{"worldMaps", "0", "transform"},
			 std::vector<std::string>{"worldMaps", "0", "areas", "0"},
			 std::vector<std::string>{"worldMaps", "0", "areas", "0", "floors", "0"},
			 std::vector<std::string>{"worldMaps", "0", "areas", "0", "chunks", "0"}}) {
		Reject(scratch, With(manifest, path), "Unsupported or allocator-owned");
	}
	// Wrong types, missing structure and zero identities.
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "mapId"}, "36"),
		"worldMaps requires");
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "mapId"}, 0),
		"worldMaps requires");
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "transform", "id"}, true),
		"worldMaps transform requires");
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "transform", "regionTop"},
		"north"), "worldMaps transform requires");
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "transform", "newDungeonMapId"}, 0),
		"worldMaps transform requires");
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "areas", "0", "internalName"}, 5),
		"worldMaps area requires internalName");
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "areas", "0", "internalName"}, ""),
		"internalName must be 1..255");
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "areas", "0", "internalName"},
		std::string("Bad\tName")), "no control characters");
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "areas", "0", "virtualMapId"}, 1.5),
		"worldMaps area requires");
	// virtualMapId must be explicit so -1 is never confused with a missing 0.
	Reject(scratch, Erase(manifest, {"worldMaps", "0", "areas", "0", "virtualMapId"}),
		"worldMaps area requires");
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "areas", "0", "id"}, 0),
		"worldMaps area requires");
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "areas", "0", "floors", "0", "id"}, 0),
		"worldMaps floor requires");
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "areas", "0", "floors", "0", "field6"},
		"x"), "worldMaps floor requires");
	// A declared transform is still validated in full: only its absence is legal.
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "transform"}, 5),
		"worldMaps transform must be an object");
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "transform"}, json()),
		"worldMaps transform must be an object");
	// Two maps may not claim the same WorldMapTransforms ID. A map with no
	// transform at all is fine beside one that has it, and claims no ID.
	{
		auto shared = manifest;
		shared["worldMaps"].push_back(json::parse(R"({
          "mapId": 37,
          "transform": {"id": 12, "regionBottom": -1.0, "regionRight": 1.0, "regionTop": 2.0,
            "regionLeft": 3.0, "newMapId": 37, "regionOffsetX": 0.0, "regionOffsetY": 0.0,
            "newDungeonMapId": 902},
          "areas": [
            {"id": 802, "areaId": 44, "internalName": "TestMapTwo", "y1": 1.0, "y2": 2.0,
             "x1": 3.0, "x2": 4.0, "virtualMapId": -1, "dungeonMapId": 0, "parentMapId": 0,
             "floors": [{"id": 902, "floor": 1, "field3": 0.0, "field4": 0.0, "field5": 0.0,
               "field6": 0.0, "field7": 1}],
             "chunks": [{"id": 5002, "field2": 9, "dungeonMapId": 902, "field4": 0.0}]}
          ]
        })"));
		Reject(scratch, shared, "Duplicate WorldMapTransforms ID");
		// The same package with the second transform removed is valid and owns
		// exactly one transform row.
		shared["worldMaps"][1].erase("transform");
		auto const mixed = Accept(scratch, "mixed-transform", shared);
		assert(mixed.worldMaps.size() == 2);
		assert(mixed.worldMaps[0].transform && !mixed.worldMaps[1].transform);
		std::map<std::string, std::vector<ResourceAllocationRequest>> requests;
		WorldMapDbcComposer::AppendRequests(mixed.packageKey, mixed.worldMaps,
			requests);
		assert(requests.at("WorldMapTransforms").size() == 1);
		assert(requests.at("WorldMapTransforms")[0].fixedValue == 12);
	}
	Reject(scratch, Erase(manifest, {"worldMaps", "0", "areas"}), "nonempty areas");
	Reject(scratch, Erase(manifest, {"worldMaps", "0", "areas", "0", "floors"}),
		"nonempty floors");
	Reject(scratch, Erase(manifest, {"worldMaps", "0", "areas", "0", "chunks"}),
		"chunks array");
	Reject(scratch, Replace(manifest, {"worldMaps"}, 5), "worldMaps must be an array");
	Reject(scratch, Replace(manifest, {"worldMaps", "0"}, 5),
		"worldMaps entry must be an object");
	// Duplicate row identities inside one package.
	for (auto const &list : {"floors", "chunks"}) {
		auto duplicate = manifest;
		duplicate["worldMaps"][0]["areas"][0][list].push_back(
			duplicate["worldMaps"][0]["areas"][0][list][0]);
		Reject(scratch, duplicate, std::string("Duplicate DungeonMap") +
			(list[0] == 'f' ? "" : "Chunk") + " ID");
	}
	auto duplicateArea = manifest;
	duplicateArea["worldMaps"][0]["areas"].push_back(duplicateArea["worldMaps"][0]["areas"][0]);
	Reject(scratch, duplicateArea, "Duplicate WorldMapArea ID");
	Reject(scratch, Replace(manifest, {"worldMaps"},
		json::array({manifest["worldMaps"][0], manifest["worldMaps"][0]})),
		"declared once per package");
	// A chunk may reference any floor of the same world map, never another map.
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "areas", "0", "chunks", "0",
		"dungeonMapId"}, 901), "which is not a floor of world map 36");
	// The transform must point at a floor of its own map.
	Reject(scratch, Replace(manifest, {"worldMaps", "0", "transform", "newDungeonMapId"}, 901),
		"which is not a floor of world map 36");
	// A second area of the same map may host a chunk for a floor declared in the
	// first area, so the reference resolves across the whole map.
	auto split = json::parse(R"({
      "schema": 3, "package": "test-world-map", "name": "Test World Map", "version": "1.0.0",
      "worldMaps": [
        {
          "mapId": 36,
          "transform": {"id": 12, "regionBottom": -1.0, "regionRight": 1.0, "regionTop": 2.0,
            "regionLeft": 3.0, "newMapId": 36, "regionOffsetX": 0.0, "regionOffsetY": 0.0,
            "newDungeonMapId": 900},
          "areas": [
            {"id": 800, "areaId": 42, "internalName": "TestMap", "y1": 1.0, "y2": 2.0,
             "x1": 3.0, "x2": 4.0, "virtualMapId": -1, "dungeonMapId": 0, "parentMapId": 0,
             "floors": [{"id": 900, "floor": 1, "field3": 0.0, "field4": 0.0, "field5": 0.0,
               "field6": 0.0, "field7": 1}],
             "chunks": [{"id": 5000, "field2": 7, "dungeonMapId": 900, "field4": 0.0}]},
            {"id": 801, "areaId": 43, "internalName": "TestMap", "y1": 1.0, "y2": 2.0,
             "x1": 3.0, "x2": 4.0, "virtualMapId": -1, "dungeonMapId": 0, "parentMapId": 0,
             "floors": [{"id": 901, "floor": 2, "field3": 0.0, "field4": 0.0, "field5": 0.0,
               "field6": 0.0, "field7": 1}],
             "chunks": [{"id": 5001, "field2": 8, "dungeonMapId": 900, "field4": 0.0}]}
          ]
        }
      ]
    })");
	auto splitPath = scratch / "split.epf";
	fs::remove(splitPath);
	Save(splitPath, split);
	auto const splitResult = ContentPackage(splitPath).Validate();
	assert(splitResult.valid);
	assert(splitResult.manifest.worldMaps[0].areas.size() == 2);
	// The same reference across two world maps is refused: a floor of map 36
	// cannot satisfy a chunk declared under map 37.
	auto perMap = split;
	perMap["worldMaps"].push_back(json::parse(R"({
      "mapId": 37,
      "transform": {"id": 13, "regionBottom": -1.0, "regionRight": 1.0, "regionTop": 2.0,
        "regionLeft": 3.0, "newMapId": 37, "regionOffsetX": 0.0, "regionOffsetY": 0.0,
        "newDungeonMapId": 902},
      "areas": [
        {"id": 802, "areaId": 44, "internalName": "TestMap2", "y1": 1.0, "y2": 2.0,
         "x1": 3.0, "x2": 4.0, "virtualMapId": -1, "dungeonMapId": 0, "parentMapId": 0,
         "floors": [{"id": 902, "floor": 1, "field3": 0.0, "field4": 0.0, "field5": 0.0,
           "field6": 0.0, "field7": 1}],
         "chunks": [{"id": 5002, "field2": 9, "dungeonMapId": 900, "field4": 0.0}]}
      ]
    })"));
	Reject(scratch, perMap, "which is not a floor of world map 37");

	// World-map artwork must live under a declared area directory. Reusing an
	// existing EPF member as the source isolates the target rule from the
	// missing-source rule.
	auto withArtwork = manifest;
	withArtwork["content"] = json::array({json{{"type", "file"},
		{"source", "manifest.json"},
		{"target", "Interface/WorldMap/TestMap/TestMap1_1.blp"}}});
	auto artPath = scratch / "artwork.epf";
	fs::remove(artPath);
	Save(artPath, withArtwork);
	assert(ContentPackage(artPath).Validate().valid);
	// The same file installed outside a declared area is refused.
	auto stray = withArtwork;
	stray["content"][0]["target"] = "Interface/WorldMap/Other/Other1_1.blp";
	auto strayPath = scratch / "stray.epf";
	fs::remove(strayPath);
	Save(strayPath, stray);
	auto const strayResult = ContentPackage(strayPath).Validate();
	assert(!strayResult.valid);
	assert(strayResult.error.find("not beneath a declared area directory") !=
		   std::string::npos);
	// Unrelated client assets are unaffected by the rule.
	auto unrelated = withArtwork;
	unrelated["content"][0]["target"] = "Interface/FrameXML/FrameXML.lua";
	auto unrelatedPath = scratch / "unrelated.epf";
	fs::remove(unrelatedPath);
	Save(unrelatedPath, unrelated);
	assert(ContentPackage(unrelatedPath).Validate().valid);
	// A traversal target is still refused by the existing safe-path rules.
	auto traversal = withArtwork;
	traversal["content"][0]["target"] = "Interface/WorldMap/../../evil.blp";
	auto traversalPath = scratch / "traversal.epf";
	fs::remove(traversalPath);
	Save(traversalPath, traversal);
	assert(!ContentPackage(traversalPath).Validate().valid);

	// Staging installs the declared artwork and never writes a world-map DBC:
	// those four files are composed by the build service, never staged raw.
	auto workspace = scratch / "workspace";
	fs::create_directories(workspace);
	auto const staged =
		ContentPackage(artPath).StageInto(workspace, ContentPackage(artPath).Validate().manifest);
	assert(staged.success);
	assert(staged.stagedFiles.size() == 1);
	assert(staged.stagedFiles.size() == 1);
	assert(staged.stagedFiles[0].filename() == "TestMap1_1.blp");
	assert(staged.stagedFiles[0].parent_path().filename() == "TestMap");
	assert(!fs::exists(workspace / "DBFilesClient"));
	// A duplicate install target inside one package is refused at staging time
	// rather than letting the second write clobber the first.
	auto duplicateTarget = withArtwork;
	duplicateTarget["content"].push_back(duplicateTarget["content"][0]);
	auto duplicatePath = scratch / "duplicate-target.epf";
	fs::remove(duplicatePath);
	Save(duplicatePath, duplicateTarget);
	auto const duplicateResult = ContentPackage(duplicatePath).Validate();
	assert(duplicateResult.valid);
	auto const duplicateWorkspace = scratch / "duplicate-workspace";
	fs::create_directories(duplicateWorkspace);
	auto const duplicateStaged =
		ContentPackage(duplicatePath).StageInto(duplicateWorkspace, duplicateResult.manifest);
	assert(!duplicateStaged.success);
	assert(duplicateStaged.error.find("Staging target already exists") !=
		   std::string::npos);
	// A changed manifest is refused at staging time, so a build cannot install
	// something other than what it validated.
	auto changed = result.manifest;
	changed.worldMaps[0].mapId = 37;
	auto const drift =
		ContentPackage(artPath).StageInto(scratch / "drift", changed);
	assert(!drift.success);
	std::cout << "  world-map EPF validation: PASS\n";
}

static void ParityTests(fs::path const &baselineDirectory)
{
	DbcDocument const stock = Stock("DungeonMap", baselineDirectory);
	// Production pins each lease to the accepted snapshot of its own table.
	std::string const pin = WorldMapDbcComposer::VerifiedBaselineSha256("DungeonMap");
	std::vector<ItemAllocation> const plan =
		ContentResourceAllocator::PlanFixed("realm",
			ContentResourceAllocator::FixedRowIdPolicy(
				WorldMapDbcComposer::ResourceKind("DungeonMap")),
			{{"pkg-a", "floor/900", "worldmap.dungeon-map.id", 900}}, {}, {}, 1, pin);
	std::map<std::string, std::string> const hashes = {{"DungeonMap", pin}};
	std::vector<ContentBaseline> baselines(1);
	baselines[0].table = "DungeonMap";
	baselines[0].clientBuild = 12340;
	baselines[0].descriptorVersion = 1;
	baselines[0].hash = pin;

	auto const parity = ContentServerBundle::ParityJson("realm", 7, plan, {}, "", "",
		"mpq", "server", "", "", {}, baselines, "", {}, {}, {}, {}, {}, {}, "", false,
		hashes);
	std::string error;
	bool const verified = ContentServerBundle::VerifyParity(parity, "realm", 7, "", "mpq",
		"server", {}, plan, error, {}, {}, {}, {}, {}, {}, hashes);
	if (!verified) std::cerr << "  parity rejected: " << error << "\n";
	assert(verified);
	// An orphan world-map lease with no recorded composed hash is refused.
	auto orphan = plan;
	orphan[0].resourceKind = "worldmap.dungeon-map.id";
	assert(!ContentServerBundle::VerifyParity(parity, "realm", 7, "", "mpq", "server",
		{}, orphan, error, {}, {}, {}, {}, {}, {}));
	// A hash for a table with no lease is refused.
	auto extraHashes = hashes;
	extraHashes["WorldMapArea"] = kHash;
	assert(!ContentServerBundle::VerifyParity(parity, "realm", 7, "", "mpq", "server",
		{}, plan, error, {}, {}, {}, {}, {}, {}, extraHashes));
	// A hash for an unknown table is refused.
	auto unknown = hashes;
	unknown["Item"] = kHash;
	assert(!ContentServerBundle::VerifyParity(parity, "realm", 7, "", "mpq", "server",
		{}, plan, error, {}, {}, {}, {}, {}, {}, unknown));
	// A lease pinned to a baseline the parity snapshot does not carry is refused.
	auto repinned = plan;
	repinned[0].baselineSha256 = std::string(64, 'b');
	assert(!ContentServerBundle::VerifyParity(parity, "realm", 7, "", "mpq", "server",
		{}, repinned, error, {}, {}, {}, {}, {}, {}, hashes));
	// A different composed hash than the caller expects is refused.
	auto other = hashes;
	other.at("DungeonMap") = std::string(64, 'c');
	assert(!ContentServerBundle::VerifyParity(parity, "realm", 7, "", "mpq", "server",
		{}, plan, error, {}, {}, {}, {}, {}, {}, other));
	// Removing the contribution and its snapshot together is a clean rebuild.
	std::vector<ItemAllocation> const none;
	bool const clean = ContentServerBundle::VerifyParity(
		ContentServerBundle::ParityJson("realm", 8, none, {}, "", "", "mpq", "server", "",
			"", {}, {}, "", {}, {}, {}, {}, {}, {}, "", false, {}),
		"realm", 8, "", "mpq", "server", {}, none, error, {}, {}, {}, {}, {}, {});
	if (!clean) std::cerr << "  clean rebuild rejected: " << error << "\n";
	assert(clean);
	// The composed file is the real baseline plus the declared row.
	auto const bytes = WorldMapDbcComposer::Compose("DungeonMap", stock,
		{{"pkg-a", MinimalMap()}});
	auto const parsed = DbcReader::Parse(bytes, *FindDbcDescriptor(12340, "DungeonMap"));
	assert(parsed.valid && parsed.document.recordCount == 56);
	assert(std::equal(stock.words.begin(), stock.words.end(), parsed.document.words.begin()));
	std::cout << "  world-map parity artifact: PASS\n";
}

// Activation reads the artifact through ContentServerBundle::ReadParityAllocations
// and then ContentServerBundle::VerifyParity, in that order. This reproduces both
// steps for the real Deadmines fixed world-map leases, which is how a production
// STAGED build is checked before its MPQ is published. The pre-fix activation
// reader rejected every worldmap.* kind, so a perfectly valid artifact could be
// recorded by a build and still refuse to activate.
static void ActivationTests()
{
	auto const fixture = fs::path(__FILE__).parent_path() / "fixtures" / "deadmines";
	json const authored = json::parse(ReadAll(fixture / "manifest.json"));
	auto content = BaseManifest();
	for (auto const &key : {"package", "name", "version"})
		content[key] = authored.at(key);
	content["worldMaps"] = authored.at("worldMaps");
	auto const manifestPath = fs::temp_directory_path() / "deadmines-activation.epf";
	fs::remove(manifestPath);
	Save(manifestPath, content);
	auto const validated = ContentPackage(manifestPath).Validate();
	assert(validated.valid);
	auto const &maps = validated.manifest.worldMaps;
	assert(maps.size() == 1 && maps[0].areas.size() == 1);
	auto const &area = maps[0].areas[0];
	auto const floorKind = WorldMapDbcComposer::ResourceKind("DungeonMap");

	// Plan the first build for all four tables exactly as the build service does,
	// then record the composed hashes and baseline snapshots a build writes.
	std::map<std::string, std::vector<ResourceAllocationRequest>> requests;
	WorldMapDbcComposer::AppendRequests(validated.manifest.packageKey, maps, requests);
	std::vector<ItemAllocation> retained;
	std::map<std::string, std::string> hashes;
	std::vector<ContentBaseline> baselines;
	for (auto const &table : WorldMapDbcTables()) {
		auto const plan = ContentResourceAllocator::PlanFixed("realm",
			ContentResourceAllocator::FixedRowIdPolicy(
				WorldMapDbcComposer::ResourceKind(table)),
			requests.at(table), {}, {}, 48,
			WorldMapDbcComposer::VerifiedBaselineSha256(table));
		for (auto const &lease : plan) retained.push_back(lease);
		hashes[table] = WorldMapDbcComposer::VerifiedBaselineSha256(table);
		ContentBaseline snapshot;
		snapshot.table = table;
		snapshot.clientBuild = 12340;
		snapshot.descriptorVersion = FindDbcDescriptor(12340, table)->version;
		snapshot.hash = hashes[table];
		baselines.push_back(snapshot);
	}
	assert(retained.size() == area.floors.size() + area.chunks.size() + 2);
	// The Deadmines floor IDs are small numbers inside the stock ID space. They
	// are valid: composition proved they are absent from the stock DBC, so no
	// dynamically allocated numeric bound may reject them during activation.
	for (auto const &lease : retained)
		if (lease.resourceKind == floorKind)
			assert(lease.value < 256);

	auto const parity = ContentServerBundle::ParityJson("realm", 48, retained, {},
		"", "", "mpq", "server", "", "", {}, baselines, "", {}, {}, {}, {}, {}, {},
		"", false, hashes);
	auto const artifact = json::parse(parity);
	assert(artifact.at("format") == 8);
	assert(artifact.at("resources").size() == retained.size());
	std::size_t floorResources = 0;
	for (auto const &resource : artifact.at("resources"))
		if (resource.at("resourceKind") == floorKind) {
			++floorResources;
			assert(resource.at("dbcDescriptorVersion") == 1);
		}
	assert(floorResources == area.floors.size());

	// The exact activation sequence: read the declared leases and composed
	// hashes out of the artifact, then verify the artifact against them.
	std::string error;
	auto const activate = [&error](std::string const &text, json const &doc,
		std::vector<ItemAllocation> const &retainedLeases) {
		auto const declared =
			ContentServerBundle::ReadParityAllocations(text, retainedLeases);
		return ContentServerBundle::VerifyParity(text, "realm", 48,
			doc.at("baselineSha256"), "mpq", "server", {}, declared.allocations,
			error, {}, {}, {}, {}, {}, {}, declared.worldMapDbcSha256);
	};
	if (!activate(parity, artifact, retained))
		std::cerr << "  activation refused: " << error << "\n";
	assert(activate(parity, artifact, retained));
	// The reader returns the artifact's own leases and the hashes it recorded.
	auto const declared = ContentServerBundle::ReadParityAllocations(parity, retained);
	assert(declared.allocations.size() == retained.size());
	assert(declared.worldMapDbcSha256 == hashes);
	// Activation must hand VerifyParity the hashes it read from this artifact.
	// Passing an empty set -- the pre-fix activation call -- leaves the recorded
	// composed hashes unaccounted for and must be refused.
	{
		std::string local;
		assert(!ContentServerBundle::VerifyParity(parity, "realm", 48,
			artifact.at("baselineSha256"), "mpq", "server", {}, declared.allocations,
			local, {}, {}, {}, {}, {}, {}, {}));
	}

	// Every mutation below must fail closed through the same two steps, and the
	// diagnostic must name the allocation or the artifact, never just "invalid".
	auto rejects = [&](json const &doc, char const *why) {
		auto const text = doc.dump(2) + "\n";
		std::string detail;
		bool accepted = false;
		try {
			accepted = activate(text, doc, retained);
			detail = error;
		} catch (std::exception const &exception) {
			detail = exception.what();
		}
		if (accepted) {
			std::cerr << "  accepted a mutated artifact: " << why << "\n";
			assert(false);
		}
		if (detail.find("package=") == std::string::npos &&
			detail.find("parity") == std::string::npos &&
			detail.find("Parity") == std::string::npos) {
			std::cerr << "  uninformative diagnostic for " << why << ": " << detail
					  << "\n";
			assert(false);
		}
	};
	// Applies a mutation to the first DungeonMap resource of a fresh copy.
	auto mutatesFloor = [&](std::function<void(json&)> mutation) {
		auto doc = artifact;
		for (auto &resource : doc.at("resources"))
			if (resource.at("resourceKind") == floorKind) {
				mutation(resource);
				break;
			}
		return doc;
	};
	rejects(mutatesFloor([](json &r) { r["value"] = 999; }), "unowned row ID");
	rejects(mutatesFloor([](json &r) { r["value"] = 0; }), "zero row ID");
	rejects(mutatesFloor([](json &r) { r["value"] = "166"; }), "row ID of wrong type");
	rejects(mutatesFloor([](json &r) { r["resourceKind"] = "worldmap.bogus.id"; }),
		"unknown resource kind");
	// A fixed lease relabelled as a dynamically allocated kind must be held to
	// that kind's bounds instead of inheriting the fixed-ID exemption.
	rejects(mutatesFloor([](json &r) { r["resourceKind"] = "currency.known-bit"; }),
		"fixed lease relabelled as a dynamic kind");
	rejects(mutatesFloor([](json &r) { r["allocationPolicyVersion"] = 2; }),
		"policy version drift");
	rejects(mutatesFloor([](json &r) { r["baselineSha256"] = kHash; }),
		"baseline provenance drift");
	rejects(mutatesFloor([](json &r) { r["baselineSha256"] = "not-a-hash"; }),
		"malformed baseline fingerprint");
	rejects(mutatesFloor([](json &r) { r.erase("baselineSha256"); }),
		"absent baseline fingerprint");
	rejects(mutatesFloor([](json &r) { r["dbcDescriptorVersion"] = 2; }),
		"descriptor version drift");
	rejects(mutatesFloor([](json &r) { r.erase("dbcDescriptorVersion"); }),
		"absent descriptor version");
	{
		auto doc = artifact;
		doc["worldMapDbcSha256"]["DungeonMap"] = "nope";
		rejects(doc, "malformed composed hash");
	}
	{
		auto doc = artifact;
		doc.at("worldMapDbcSha256").erase("DungeonMap");
		rejects(doc, "missing composed hash");
	}
	{
		// Every composed world-map table has a lease, so the only hash that
		// cannot be accounted for is one for a table outside the set.
		auto doc = artifact;
		doc["worldMapDbcSha256"]["Item"] = kHash;
		rejects(doc, "composed hash for a table outside the world-map set");
	}
	{
		// A hash whose lease is gone from the artifact is an orphan.
		auto doc = artifact;
		for (auto it = doc["resources"].begin(); it != doc["resources"].end(); ++it)
			if (it->at("resourceKind") == WorldMapDbcComposer::ResourceKind("WorldMapArea")) {
				doc["resources"].erase(it);
				break;
			}
		rejects(doc, "composed hash with no lease");
	}
	{
		auto doc = artifact;
		for (auto &snapshot : doc.at("baselines"))
			if (snapshot.at("table") == "DungeonMap")
				snapshot["clientBuild"] = 11723;
		rejects(doc, "unavailable descriptor build");
	}
	{
		auto doc = artifact;
		for (auto const &resource : doc.at("resources"))
			if (resource.at("resourceKind") == floorKind) {
				doc["resources"].push_back(resource);
				break;
			}
		rejects(doc, "duplicate identity");
	}
	{
		auto doc = artifact;
		doc["resources"] = json::object();
		rejects(doc, "resources is not an array");
	}
	// A lease the world database no longer holds at all.
	{
		auto withoutFloor = retained;
		withoutFloor.erase(std::remove_if(withoutFloor.begin(), withoutFloor.end(),
			[&](auto const &lease) {
				return lease.resourceKind == floorKind &&
					lease.value == area.floors[0].id;
			}), withoutFloor.end());
		assert(Throws([&] {
			(void)ContentServerBundle::ReadParityAllocations(parity, withoutFloor);
		}));
	}
	std::cout << "  world-map activation parity: PASS\n";
}

// The Deadmines fixture must reproduce the verified stock patch byte for byte.
static void GoldenTests(fs::path const &baselineDirectory, fs::path const &goldenDirectory,
	fs::path const &artworkDirectory)
{
	auto const fixture = fs::path(__FILE__).parent_path() / "fixtures" / "deadmines";
	assert(fs::is_regular_file(fixture / "manifest.json"));
	// The manifest is a bare JSON document, so its semantic section is exercised
	// through a synthesized EPF. Client artwork is stock data and is not
	// committed, so the artwork list is checked rather than extracted.
	json const manifest = json::parse(ReadAll(fixture / "manifest.json"));
	assert(manifest.at("schema") == 3);
	auto const manifestPath = fs::temp_directory_path() / "deadmines-manifest-test.epf";
	fs::remove(manifestPath);
	auto content = BaseManifest();
	for (auto const &key : {"schema", "package", "name", "version", "description"})
		content[key] = manifest.at(key);
	content["worldMaps"] = manifest.at("worldMaps");
	Save(manifestPath, content);
	auto const validated = ContentPackage(manifestPath).Validate();
	if (!validated.valid) std::cerr << "  deadmines manifest rejected: " << validated.error << "\n";
	assert(validated.valid);
	assert(validated.manifest.worldMaps.size() == 1);
	assert(validated.manifest.worldMaps[0].mapId == 36);
	auto const &deadmines = validated.manifest.worldMaps[0];
	assert(deadmines.areas.size() == 1);
	assert(deadmines.areas[0].id == 756);
	assert(deadmines.areas[0].internalName == "TheDeadmines");
	assert(deadmines.areas[0].floors.size() == 2);
	assert(deadmines.areas[0].chunks.size() == 29);
	assert(deadmines.areas[0].floors[0].id == 166 &&
		   deadmines.areas[0].floors[1].id == 167);
	assert(deadmines.transform && deadmines.transform->id == 11 &&
		   deadmines.transform->newMapId == 36);
	assert(deadmines.transform->newDungeonMapId == 167);
	// The declared artwork is exactly the 24 Deadmines tiles under the client
	// directory the area's internal name derives.
	std::set<std::string> expected;
	for (auto floor : {1, 2})
		for (auto tile = 1; tile <= 12; ++tile)
			expected.insert("Interface/WorldMap/TheDeadmines/TheDeadmines" +
				std::to_string(floor) + "_" + std::to_string(tile) + ".blp");
	std::set<std::string> targets;
	for (auto const &entry : manifest.at("content")) {
		assert(entry.at("type") == "file");
		targets.insert(entry.at("target").get<std::string>());
	}
	assert(targets == expected);
	// When the caller supplies the artwork, every declared tile must be present
	// under the supplied directory and non-empty, and the assembled EPF must
	// validate and stage exactly those files.
	if (!artworkDirectory.empty() && fs::is_directory(artworkDirectory)) {
		auto const scratch = fs::temp_directory_path() / "content-world-map-tests" /
			"golden";
		fs::remove_all(scratch);
		fs::create_directories(scratch);
		auto const epfPath = scratch / "deadmines.epf";
		{
			mz_zip_archive zip{};
			assert(mz_zip_writer_init_file(&zip, epfPath.string().c_str(), 0));
			auto const body = ReadAll(fixture / "manifest.json");
			assert(mz_zip_writer_add_mem(&zip, "manifest.json", body.data(), body.size(),
										 MZ_BEST_COMPRESSION));
			for (auto const &entry : manifest.at("content")) {
				// The caller supplies the area's artwork directory, so resolve each
				// declared tile by its leaf name beneath it.
				auto const source = entry.at("source").get<std::string>();
				auto const payload = ReadAll(artworkDirectory /
					fs::path(source).filename());
				assert(!payload.empty());
				assert(mz_zip_writer_add_mem(&zip, source.c_str(), payload.data(),
					payload.size(), MZ_BEST_COMPRESSION));
			}
			assert(mz_zip_writer_finalize_archive(&zip));
			mz_zip_writer_end(&zip);
		}
		auto const real = ContentPackage(epfPath).Validate();
		if (!real.valid) std::cerr << "  assembled EPF rejected: " << real.error << "\n";
		assert(real.valid);
		assert(real.manifest.worldMaps.size() == 1);
		assert(real.manifest.content.size() == 24);
		auto const workspace = scratch / "deadmines-workspace";
		fs::create_directories(workspace);
		auto const staged = ContentPackage(epfPath).StageInto(workspace, real.manifest);
		assert(staged.success);
		assert(staged.stagedFiles.size() == 24);
		for (auto const &target : expected)
			assert(fs::is_regular_file(workspace / target));
		// The four world-map DBCs are composed by the build service, never staged.
		assert(!fs::exists(workspace / "DBFilesClient"));
		// Staging the same EPF twice is refused rather than overwriting.
		assert(!ContentPackage(epfPath).StageInto(workspace, real.manifest).success);
	}

	std::vector<ResolvedWorldMap> const maps{
		{manifest.at("package").get<std::string>(), deadmines}};
	// Final record counts: the stock rows plus the declared rows.
	assert(WorldMapDbcComposer::Rows("DungeonMap", maps).size() == 2);
	assert(WorldMapDbcComposer::Rows("DungeonMapChunk", maps).size() == 29);
	assert(WorldMapDbcComposer::Rows("WorldMapArea", maps).size() == 1);
	assert(WorldMapDbcComposer::Rows("WorldMapTransforms", maps).size() == 1);
	for (auto const &table : WorldMapDbcTables()) {
		auto const composed =
			WorldMapDbcComposer::Compose(table, Stock(table, baselineDirectory), maps);
		auto const expectedPath = goldenDirectory / (table + ".dbc");
		assert(fs::is_regular_file(expectedPath));
		std::string const golden = ReadAll(expectedPath);
		std::vector<std::uint8_t> const expected(golden.begin(), golden.end());
		if (composed != expected) {
			std::cerr << "  " << table << ".dbc does not match the verified patch\n";
			assert(composed.size() == expected.size());
			for (auto i = std::size_t{0}; i < composed.size(); ++i)
				if (composed[i] != expected[i]) {
					std::cerr << "  first difference at byte " << i << ": "
							  << static_cast<int>(composed[i]) << " != "
							  << static_cast<int>(expected[i]) << "\n";
					break;
				}
			assert(false);
		}
		auto const reparsed =
			DbcReader::Parse(composed, *FindDbcDescriptor(12340, table));
		assert(reparsed.valid);
		std::cout << "  " << table << ".dbc matches the verified patch ("
				  << reparsed.document.recordCount << " records)\n";
	}
	// The WorldMapArea string block appends exactly "TheDeadmines\0" at 1303.
	auto const composedArea = DbcReader::Parse(
		WorldMapDbcComposer::Compose("WorldMapArea", Stock("WorldMapArea", baselineDirectory),
			maps),
		*FindDbcDescriptor(12340, "WorldMapArea"));
	assert(composedArea.valid);
	assert(composedArea.document.stringBlockSize == 1316);
	assert(composedArea.document.words[108 * 11 + 3] == 1303);

	// A declared transform is preserved exactly, so Deadmines still carries its
	// own WorldMapTransforms row and its fixed ID 11 lease, unchanged.
	assert(WorldMapDbcComposer::Rows("WorldMapTransforms", maps) ==
		   std::vector<std::uint32_t>{11});
	{
		auto const parsed = DbcReader::Parse(WorldMapDbcComposer::Compose(
			"WorldMapTransforms", Stock("WorldMapTransforms", baselineDirectory),
			maps), *FindDbcDescriptor(12340, "WorldMapTransforms"));
		assert(parsed.valid && parsed.document.recordCount == 10);
		// ID 11, MapID 36, RegionBottom/Right -20000, RegionTop/Left 20000,
		// NewMapID 36, RegionOffset 0/0, NewDungeonMapID 167.
		assert(parsed.document.words[9 * 10 + 0] == 11);
		assert(parsed.document.words[9 * 10 + 1] == 36);
		assert(parsed.document.words[9 * 10 + 2] == 0xC69C4000u);
		assert(parsed.document.words[9 * 10 + 3] == 0xC69C4000u);
		assert(parsed.document.words[9 * 10 + 4] == 0x469C4000u);
		assert(parsed.document.words[9 * 10 + 5] == 0x469C4000u);
		assert(parsed.document.words[9 * 10 + 6] == 36);
		assert(parsed.document.words[9 * 10 + 7] == 0);
		assert(parsed.document.words[9 * 10 + 8] == 0);
		assert(parsed.document.words[9 * 10 + 9] == 167);
	}
	// Dropping the transform from this very fixture must change the transform
	// table alone: the other three are byte-for-byte the golden patch, and
	// WorldMapTransforms falls back to the verified stock file. So the optional
	// transform is what the Deadmines row rests on, and nothing else moved.
	{
		ContentWorldMap withTransform = deadmines;
		ContentWorldMap withoutTransform = deadmines;
		withoutTransform.transform.reset();
		std::vector<ResolvedWorldMap> const bare{{"deadmines", withTransform}};
		std::vector<ResolvedWorldMap> const without{{"deadmines", withoutTransform}};
		for (auto const &table : {"DungeonMap", "DungeonMapChunk", "WorldMapArea"}) {
			auto const with = WorldMapDbcComposer::Compose(table,
				Stock(table, baselineDirectory), bare);
			auto const withoutBytes = WorldMapDbcComposer::Compose(table,
				Stock(table, baselineDirectory), without);
			assert(with == withoutBytes);
			std::string const golden =
				ReadAll(goldenDirectory / (std::string(table) + ".dbc"));
			assert(with == std::vector<std::uint8_t>(golden.begin(), golden.end()));
		}
		assert(WorldMapDbcComposer::Rows("WorldMapTransforms", without).empty());
		auto const stockTransforms = Stock("WorldMapTransforms", baselineDirectory);
		assert(WorldMapDbcComposer::Compose("WorldMapTransforms", stockTransforms,
			without) == DbcReader::Serialize(stockTransforms));
	}
	std::cout << "  Deadmines golden fixture: PASS\n";
}

int main(int argc, char **argv)
{
	DescriptorTests();
	AllocationTests();
	RelocatedRowTests();
	FirstBuildPlanningTests();
	ActivationTests();
	auto const scratch = Scratch();
	auto const stockDirectory = fs::path(argc > 1 && argv[1][0] ? argv[1] : "");
	if (!stockDirectory.empty() && fs::is_directory(stockDirectory)) {
		CompositionTests(stockDirectory);
		StagingTests(stockDirectory);
		ParityTests(stockDirectory);
		NoTransformTests(scratch, stockDirectory);
		MultiFloorNoTransformTests(scratch, stockDirectory);
		DungeonMapIdCompositionTests(scratch, stockDirectory);
		auto const golden = fs::path(argc > 2 && argv[2][0] ? argv[2] : "");
		auto const artwork = fs::path(argc > 3 && argv[3][0] ? argv[3] : "");
		if (!golden.empty() && fs::is_directory(golden))
			GoldenTests(stockDirectory, golden, artwork);
		else
			std::cout << "  Deadmines golden fixture: SKIP (no --world-map-golden-dir)\n";
	} else {
		std::cout << "  world-map baseline checks: SKIP (no --world-map-baseline-dir)\n";
	}
	DungeonMapIdReferenceTests(scratch);
	PackageTests(scratch);
	return 0;
}
