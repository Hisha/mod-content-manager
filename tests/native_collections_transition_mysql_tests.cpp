// Native Collections enabled -> disabled -> enabled integration regression.
//
// Runs the production package registry, cumulative builder, exact parity
// inspection, activation and publication paths against a disposable MySQL
// schema. The only injected surface is ContentManager configuration/discovery.
#include "DatabaseEnv.h"
#include "ContentBuildHash.h"
#include "ContentBuildRegistry.h"
#include "ContentBuildService.h"
#include "ContentFrameXml.h"
#include "ContentManager.h"
#include "ContentPackage.h"
#include "ContentPackageRegistry.h"
#include "ContentServerBundle.h"
#include "ContentServerDeployment.h"
#include "third_party/json/json.hpp"
#include "third_party/miniz/miniz.h"

#include <StormLib.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace
{
std::filesystem::path root;
std::filesystem::path huntsPackage;
std::filesystem::path transmogPackage;

std::string const StockToc =
    "LFDFrame.xml\r\n"
    "MainMenuBar.xml\r\n";

void AddMember(mz_zip_archive& zip, std::string const& name,
    std::string const& bytes)
{
    assert(mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(),
        MZ_BEST_COMPRESSION));
}

void Package(std::filesystem::path const& path, json manifest,
    std::map<std::string, std::string> const& members)
{
    manifest["clientFrameXml"]["stockTocSha256"] =
        ContentBuildHash::Bytes({StockToc.begin(), StockToc.end()});
    mz_zip_archive zip{};
    assert(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
    auto body = manifest.dump();
    AddMember(zip, "manifest.json", body);
    AddMember(zip, "upstream/FrameXML.toc", StockToc);
    for (auto const& member : members)
        AddMember(zip, member.first, member.second);
    assert(mz_zip_writer_finalize_archive(&zip));
    mz_zip_writer_end(&zip);
}

bool HasEntry(std::filesystem::path const& mpq, std::string entry)
{
    HANDLE archive = nullptr;
    HANDLE file = nullptr;
    assert(SFileOpenArchive(mpq.c_str(), 0, MPQ_OPEN_READ_ONLY, &archive));
    std::replace(entry.begin(), entry.end(), '/', '\\');
    bool present =
        SFileOpenFileEx(archive, entry.c_str(), SFILE_OPEN_FROM_MPQ, &file) != 0;
    if (present)
        SFileCloseFile(file);
    SFileCloseArchive(archive);
    return present;
}

std::string EntryText(std::filesystem::path const& mpq, std::string entry)
{
    HANDLE archive = nullptr;
    HANDLE file = nullptr;
    assert(SFileOpenArchive(mpq.c_str(), 0, MPQ_OPEN_READ_ONLY, &archive));
    std::replace(entry.begin(), entry.end(), '/', '\\');
    assert(SFileOpenFileEx(archive, entry.c_str(), SFILE_OPEN_FROM_MPQ, &file));
    auto size = SFileGetFileSize(file, nullptr);
    std::string bytes(size, '\0');
    DWORD read = 0;
    assert(SFileReadFile(file, bytes.data(), size, &read, nullptr) && read == size);
    SFileCloseFile(file);
    SFileCloseArchive(archive);
    return bytes;
}

std::string Read(std::filesystem::path const& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.is_open());
    return {std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

json Parity(ContentBuildResult const& build)
{
    return json::parse(Read(build.outputPath.string() + ".parity.json"));
}

std::string MpqHash(ContentBuildResult const& build)
{
    std::string hash;
    std::string error;
    assert(ContentBuildHash::Calculate(build.outputPath, hash, error));
    return hash;
}

std::optional<ContentBuildRecord> Active()
{
    std::optional<ContentBuildRecord> active;
    std::string error;
    assert(ContentBuildRegistry().GetActiveBuild(active, error));
    return active;
}

std::string State(std::uint32_t build)
{
    auto query = WorldDatabase.Query("SELECT state FROM content_manager_build WHERE build_number=" +
        std::to_string(build));
    assert(query);
    return query->Fetch()[0].Get<std::string>();
}
}

ContentManager& ContentManager::Instance()
{
    static ContentManager manager;
    return manager;
}

void ContentManager::LoadConfig()
{
    _enabled = true;
    _clientBuild = 12340;
    _baselineDbcDirectory = (root / "baseline").string();
    _workDirectory = (root / "staging").string();
    _outputDirectory = (root / "output").string();
    _itemBaselineSha256.clear();
    _currencyTypesBaselineSha256.clear();
}

bool ContentManager::IsEnabled() const { return _enabled; }
std::string const& ContentManager::GetBaselineDbcDirectory() const
{
    return _baselineDbcDirectory;
}
std::string const& ContentManager::GetWorkDirectory() const
{
    return _workDirectory;
}
std::string const& ContentManager::GetOutputDirectory() const
{
    return _outputDirectory;
}
std::string const& ContentManager::GetItemBaselineSha256() const
{
    return _itemBaselineSha256;
}
std::uint32_t ContentManager::GetClientBuild() const { return _clientBuild; }
std::vector<ContentPackageCandidate> ContentManager::ScanAvailablePackages() const
{
    return {
        {huntsPackage, huntsPackage.filename().string(), ContentPackageSource::Module,
            "fixture"},
        {transmogPackage, transmogPackage.filename().string(),
            ContentPackageSource::Module, "fixture"}};
}

int main(int argc, char** argv)
{
    assert(argc == 3);
    WorldDatabase.Connect(argv[1]);
    root = std::filesystem::absolute(argv[2]);
    huntsPackage = root / "native-hunts.epf";
    transmogPackage = root / "native-transmog.epf";
    std::filesystem::create_directories(root / "baseline");
    std::filesystem::create_directories(root / "staging");
    std::filesystem::create_directories(root / "output");
    std::filesystem::create_directories(root / "published");

    json hunts = {
        {"schema", 3}, {"package", "mod-native-hunts"},
        {"name", "Native Hunts"}, {"version", "fixture-1"},
        {"content", json::array({{{"type", "file"},
            {"source", "client/NativeHuntsFrame.xml"},
            {"target", "Interface/FrameXML/NativeHuntsFrame.xml"}}})},
        {"clientFrameXml", {{"stockTocSource", "upstream/FrameXML.toc"},
            {"loadEntries", json::array({{{"target", "Interface/FrameXML/NativeHuntsFrame.xml"},
                {"after", ContentFrameXml::NativePveLuaTarget()}}})}}},
        {"nativePvePages", json::array({{{"id", "hunts"}, {"name", "Hunts"},
            {"frame", "NativeHuntsFrame"},
            {"loadTarget", "Interface/FrameXML/NativeHuntsFrame.xml"}}})}};
    json transmog = {
        {"schema", 3}, {"package", "mod-native-transmog"},
        {"name", "Native Transmog"}, {"version", "fixture-1"},
        {"content", json::array({{{"type", "file"},
            {"source", "client/NativeTransmogFrame.xml"},
            {"target", "Interface/FrameXML/NativeTransmogFrame.xml"}}})},
        {"clientFrameXml", {{"stockTocSource", "upstream/FrameXML.toc"},
            {"loadEntries", json::array({{{"target", "Interface/FrameXML/NativeTransmogFrame.xml"},
                {"after", ContentFrameXml::CollectionsLuaTarget()}}})}}},
        {"nativeCollectionPages", json::array({{{"id", "appearances"},
            {"name", "Appearances"}, {"order", 100},
            {"frame", "NativeTransmogCollectionsFrame"},
            {"loadTarget", "Interface/FrameXML/NativeTransmogFrame.xml"}}})}};
    Package(huntsPackage, hunts,
        {{"client/NativeHuntsFrame.xml", "<Ui><Frame name=\"NativeHuntsFrame\"/></Ui>"}});
    Package(transmogPackage, transmog,
        {{"client/NativeTransmogFrame.xml", "<Ui><Frame name=\"NativeTransmogCollectionsFrame\"/></Ui>"}});

    auto huntsManifest = ContentPackage(huntsPackage).Validate();
    auto transmogManifest = ContentPackage(transmogPackage).Validate();
    assert(huntsManifest.valid && transmogManifest.valid);
    auto install = [](ContentPackageManifest const& manifest,
                       std::filesystem::path const& path) {
        auto result = ContentPackageRegistry().Install({manifest.packageKey,
            manifest.name, manifest.version, "fixture", path.string(), {}});
        assert(result.success && result.changed);
    };
    install(huntsManifest.manifest, huntsPackage);
    install(transmogManifest.manifest, transmogPackage);

    auto& manager = ContentManager::Instance();
    manager.LoadConfig();
    auto build = [&](char const* label) {
        auto result = ContentBuildService().Build(manager, "Eitrigg");
        if (!result.success)
            std::cerr << label << " build failed: " << result.error << '\n';
        assert(result.success);
        return result;
    };
    auto activate = [&](ContentBuildResult const& selected) {
        ContentActivationResult activation;
        ContentPublicationResult publication;
        bool alreadyActive = false;
        std::string error;
        assert(ContentServerDeployment::Activate(selected.buildNumber, "Eitrigg",
            root / "output", root / "published", activation, publication,
            alreadyActive, error));
        assert(!activation.hasManagedServerContent && !alreadyActive);
        return publication;
    };

    // Build A: both contributors select Collections, its icon and Appearances.
    auto a = build("A");
    auto aParity = Parity(a);
    auto aHashes = aParity.at("frameXmlSha256");
    assert(a.packageCount == 2 && a.fileCount == 6);
    assert(aHashes.size() == 4);
    assert(aHashes.contains(ContentFrameXml::NativePveLuaTarget()));
    assert(aHashes.contains(ContentFrameXml::CollectionsLuaTarget()));
    assert(aHashes.contains(ContentFrameXml::CollectionsIconTarget()));
    assert(aHashes.contains(ContentFrameXml::StockTocTarget()));
    assert(HasEntry(a.outputPath, ContentFrameXml::CollectionsLuaTarget()));
    assert(HasEntry(a.outputPath, ContentFrameXml::CollectionsIconTarget()));
    assert(HasEntry(a.outputPath, "Interface/FrameXML/NativeTransmogFrame.xml"));
    assert(EntryText(a.outputPath, ContentFrameXml::CollectionsLuaTarget())
        .find("name=\"Appearances\"") != std::string::npos);
    auto aPublication = activate(a);
    assert(Active()->buildNumber == a.buildNumber);

    // Uninstall changes only desired package state. A remains ACTIVE and its
    // versioned published artifact remains historical until B is activated.
    auto removed = ContentPackageRegistry().Uninstall("mod-native-transmog");
    assert(removed.success && removed.changed);
    assert(Active()->buildNumber == a.buildNumber);
    assert(std::filesystem::exists(aPublication.path));

    // Build B: Hunts only. Exact generated-resource selection, cumulative file
    // count, MPQ contents and parity manifest all omit Collections.
    auto b = build("B");
    auto bParity = Parity(b);
    auto bHashes = bParity.at("frameXmlSha256");
    assert(b.packageCount == 1 && b.fileCount == 3);
    assert(bHashes.size() == 2);
    assert(bHashes.contains(ContentFrameXml::NativePveLuaTarget()));
    assert(bHashes.contains(ContentFrameXml::StockTocTarget()));
    assert(!bHashes.contains(ContentFrameXml::CollectionsLuaTarget()));
    assert(!bHashes.contains(ContentFrameXml::CollectionsIconTarget()));
    assert(!HasEntry(b.outputPath, ContentFrameXml::CollectionsLuaTarget()));
    assert(!HasEntry(b.outputPath, ContentFrameXml::CollectionsIconTarget()));
    assert(!HasEntry(b.outputPath, "Interface/FrameXML/NativeTransmogFrame.xml"));
    assert(Read(b.outputPath.string() + ".parity.json").find("Collections") ==
        std::string::npos);
    assert(EntryText(b.outputPath, ContentFrameXml::StockTocTarget())
        .find("Collections") == std::string::npos);

    // Production activation performs exact parity validation before publication,
    // then atomically moves lifecycle state from A to B. A stays on disk only as
    // a versioned historical artifact.
    auto bPublication = activate(b);
    assert(Active()->buildNumber == b.buildNumber);
    assert(State(a.buildNumber) == "SUPERSEDED" && State(b.buildNumber) == "ACTIVE");
    assert(std::filesystem::exists(aPublication.path));
    assert(std::filesystem::exists(bPublication.path));

    // Build C: reinstall restores the same selected resources and bytes. Build
    // number changes only the parity sidecar, not the deterministic cumulative MPQ.
    install(transmogManifest.manifest, transmogPackage);
    auto c = build("C");
    auto cHashes = Parity(c).at("frameXmlSha256");
    assert(c.packageCount == 2 && c.fileCount == a.fileCount);
    assert(cHashes == aHashes);
    assert(MpqHash(c) == MpqHash(a));
    assert(HasEntry(c.outputPath, ContentFrameXml::CollectionsLuaTarget()));
    assert(HasEntry(c.outputPath, ContentFrameXml::CollectionsIconTarget()));
    activate(c);
    assert(Active()->buildNumber == c.buildNumber);
    assert(State(b.buildNumber) == "SUPERSEDED" && State(c.buildNumber) == "ACTIVE");

    std::cout << "PASS Native Collections A->B->C: conditional resources, exact counts/hashes/parity, explicit activation, historical publication, deterministic restore\n";
    return 0;
}
