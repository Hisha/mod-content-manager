// Schema 3 client requirements against disposable MySQL through the production
// Build/registry path: aggregation across participating manifests, immutable
// per-build persistence, empty result for requirement-free builds, union
// deduplication, package-state independence, and unchanged MPQ/states.
// Uses the supplied schema-1 EPFs plus schema-3 fixtures created in the root.
#include "DatabaseEnv.h"
#include "ContentManager.h"
#include "ContentBuildService.h"
#include "ContentBuildRegistry.h"
#include "ContentBuildHash.h"
#include "ContentPackage.h"
#include "ContentPackageRegistry.h"
#include "ContentClientRequirement.h"
#include "third_party/json/json.hpp"
#include "third_party/miniz/miniz.h"
#include <StormLib.h>
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace { std::filesystem::path root, legacyOne, legacyTwo, requiringA, requiringB; }
using json = nlohmann::json;
// Only config/discovery are injected. Registry, build and MPQ are production code.
ContentManager& ContentManager::Instance(){static ContentManager manager;return manager;}
void ContentManager::LoadConfig()
{
    _enabled=true;_clientBuild=12340;_baselineDbcDirectory=(root/"baseline").string();
    _workDirectory=(root/"staging").string();_outputDirectory=(root/"output").string();
    _itemBaselineSha256.clear(); _currencyTypesBaselineSha256.clear(); // Raw-only builds need no per-DBC pins.
}
bool ContentManager::IsEnabled() const{return _enabled;}
std::string const& ContentManager::GetBaselineDbcDirectory() const{return _baselineDbcDirectory;}
std::string const& ContentManager::GetWorkDirectory() const{return _workDirectory;}
std::string const& ContentManager::GetOutputDirectory() const{return _outputDirectory;}
std::string const& ContentManager::GetItemBaselineSha256() const{return _itemBaselineSha256;}
std::uint32_t ContentManager::GetClientBuild() const{return _clientBuild;}
std::vector<ContentPackageCandidate> ContentManager::ScanAvailablePackages() const
{
    ContentPackageCandidate a{legacyOne, legacyOne.filename().string(), ContentPackageSource::Module, "fixture"};
    ContentPackageCandidate b{legacyTwo, legacyTwo.filename().string(), ContentPackageSource::Module, "fixture"};
    ContentPackageCandidate c{requiringA, requiringA.filename().string(), ContentPackageSource::Module, "fixture"};
    ContentPackageCandidate d{requiringB, requiringB.filename().string(), ContentPackageSource::Module, "fixture"};
    return {a, b, c, d};
}
static void Package(std::filesystem::path const& path, json const& manifest, std::string const& assetName,
    std::string const& assetBytes)
{
    mz_zip_archive zip{};
    assert(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
    auto body = manifest.dump();
    assert(mz_zip_writer_add_mem(&zip, "manifest.json", body.data(), body.size(), MZ_BEST_COMPRESSION));
    assert(mz_zip_writer_add_mem(&zip, assetName.c_str(), assetBytes.data(), assetBytes.size(), MZ_BEST_COMPRESSION));
    assert(mz_zip_writer_finalize_archive(&zip));
    mz_zip_writer_end(&zip);
}
static std::vector<std::uint8_t> Extract(std::filesystem::path const& path, char const* name)
{
    HANDLE archive=nullptr,file=nullptr;assert(SFileOpenArchive(path.c_str(),0,MPQ_OPEN_READ_ONLY,&archive));
    std::string mpqName(name);std::replace(mpqName.begin(),mpqName.end(),'/','\\');
    if (!SFileOpenFileEx(archive,mpqName.c_str(),SFILE_OPEN_FROM_MPQ,&file))
        throw std::runtime_error("Missing MPQ entry: "+mpqName);
    auto size=SFileGetFileSize(file,nullptr);std::vector<std::uint8_t> bytes(size);DWORD read=0;
    assert(SFileReadFile(file,bytes.data(),size,&read,nullptr)&&read==size);
    SFileCloseFile(file);SFileCloseArchive(archive);return bytes;
}
static bool HasAsset(std::filesystem::path const& mpq, std::string const& entry)
{
    HANDLE archive=nullptr,file=nullptr;
    assert(SFileOpenArchive(mpq.c_str(),0,MPQ_OPEN_READ_ONLY,&archive));
    std::string mpqName=entry;std::replace(mpqName.begin(),mpqName.end(),'/','\\');
    bool present=SFileOpenFileEx(archive,mpqName.c_str(),SFILE_OPEN_FROM_MPQ,&file)!=FALSE;
    if(present)SFileCloseFile(file);
    SFileCloseArchive(archive);
    return present;
}
static std::uint64_t Count(char const* table)
{
    auto query=WorldDatabase.Query(std::string("SELECT COUNT(*) FROM ")+table);
    assert(query);
    return query->Fetch()[0].Get<std::uint64_t>();
}
static std::vector<std::string> Requirements(std::uint32_t number)
{
    std::vector<std::string> result;
    std::string error;
    assert(ContentBuildRegistry().GetClientRequirements(number, result, error));
    return result;
}
int main(int argc,char** argv)
{
    assert(argc==5);WorldDatabase.Connect(argv[1]);
    root=std::filesystem::absolute(argv[2]);
    auto legacyOneSource=std::filesystem::absolute(argv[3]);
    auto legacyTwoSource=std::filesystem::absolute(argv[4]);
    assert(std::filesystem::exists(legacyOneSource)&&std::filesystem::exists(legacyTwoSource));
    auto& manager=ContentManager::Instance();manager.LoadConfig();
    std::filesystem::create_directories(root/"baseline");std::filesystem::create_directories(root/"staging");
    std::filesystem::create_directories(root/"output");
    legacyOne=root/"legacy-one.epf";legacyTwo=root/"legacy-two.epf";
    std::filesystem::copy_file(legacyOneSource,legacyOne,std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(legacyTwoSource,legacyTwo,std::filesystem::copy_options::overwrite_existing);
    auto const oneManifest=ContentPackage(legacyOne).Validate().manifest;
    auto const twoManifest=ContentPackage(legacyTwo).Validate().manifest;
    assert(oneManifest.schema==1||oneManifest.schema==2);
    assert(oneManifest.itemRows.empty()&&oneManifest.serverItemRows.empty());
    assert(!oneManifest.content.empty());
    auto const oneAsset=oneManifest.content.front().target;
    assert(!twoManifest.content.empty());
    auto const twoAsset=twoManifest.content.front().target;

    // Fixture schema-3 packages declaring protected-framexml. Both installed
    // together must yield exactly one build requirement (deduplicated union).
    std::string const baseAsset="Documentation/requiring.txt";
    requiringA=root/"requiring-a.epf";
    requiringB=root/"requiring-b.epf";
    json requirement = {{"schema",3},{"package","requiring-a"},{"name","Requiring A"},{"version","1"},
        {"clientRequirements",json::array({ContentClientRequirement::ProtectedFrameXml})},
        {"content",json::array({{{"type","file"},{"source","assets/requiring-a.txt"},{"target",baseAsset}}})}};
    auto requirementB = requirement; requirementB["package"]="requiring-b"; requirementB["name"]="Requiring B";
    requirementB["content"][0]["source"]="assets/requiring-b.txt"; requirementB["content"][0]["target"]="Documentation/requiring-b.txt";
    Package(requiringA,requirement,"assets/requiring-a.txt","requiring-a");
    Package(requiringB,requirementB,"assets/requiring-b.txt","requiring-b");
    auto const reqManifest=ContentPackage(requiringA).Validate().manifest;
    assert(reqManifest.schema==3&&reqManifest.clientRequirements.size()==1);
    assert(reqManifest.clientRequirements[0]==ContentClientRequirement::ProtectedFrameXml);

    auto install=[&](ContentPackageManifest const& m,std::filesystem::path const& source){
        auto r=ContentPackageRegistry().Install({m.packageKey,m.name,m.version,"fixture",source.string(),{}});
        assert(r.success&&r.changed);
    };
    install(oneManifest,legacyOne);install(twoManifest,legacyTwo);
    install(reqManifest,requiringA);
    auto const reqBManifest=ContentPackage(requiringB).Validate().manifest;
    install(reqBManifest,requiringB);

    auto build=[&](std::string const& label){
        auto result=ContentBuildService().Build(manager,"Eitrigg",[&](std::string const& message){std::cout<<message<<'\n';});
        if(!result.success)std::cout<<label<<" error: "<<result.error<<"\n";
        assert(result.success);
        return result;
    };

    // Build 1 includes both schema-3 packages plus two legacy packages: the
    // union is a single protected-framexml requirement recorded for build 1.
    auto first=build("first");
    assert(Count("content_manager_build")==1);
    assert(Count("content_manager_build_client_requirement")==1);
    auto firstReq=Requirements(first.buildNumber);
    assert(firstReq.size()==1&&firstReq[0]==ContentClientRequirement::ProtectedFrameXml);
    assert(HasAsset(first.outputPath,baseAsset));
    assert(HasAsset(first.outputPath,oneAsset));
    assert(HasAsset(first.outputPath,twoAsset));

    // A build number that was never recorded is distinguishable from an existing
    // build that requires nothing.
    {
        std::vector<std::string> missingRequirements;
        std::string missingError;
        assert(!ContentBuildRegistry().GetClientRequirements(9999, missingRequirements, missingError));
        assert(missingRequirements.empty());
        assert(missingError.find("does not exist") != std::string::npos);
    }

    // Removing the schema-3 packages changes only the NEXT build. Build 1's
    // recorded requirement is immutable.
    auto withdrawal=ContentPackageRegistry().Uninstall("requiring-a");
    assert(withdrawal.success&&withdrawal.changed);
    auto withdrawalB=ContentPackageRegistry().Uninstall("requiring-b");
    assert(withdrawalB.success&&withdrawalB.changed);
    auto second=build("withdrawn");
    assert(second.buildNumber==first.buildNumber+1);
    assert(Count("content_manager_build_client_requirement")==1); // only build 1
    assert(Requirements(second.buildNumber).empty());             // no requirements recorded
    std::vector<std::uint8_t> legacyBytes;
    {
        auto mpq=second.outputPath;
        legacyBytes=Extract(mpq,oneAsset.c_str());
        assert(HasAsset(mpq,twoAsset));
        assert(!HasAsset(mpq,baseAsset));
    }

    // Reinstalling one schema-3 package records the requirement again on the
    // new build; the intermediate requirement-free build stays empty.
    install(reqManifest,requiringA);
    auto third=build("restored");
    assert(third.buildNumber==second.buildNumber+1);
    assert(Count("content_manager_build")==3);
    assert(Count("content_manager_build_client_requirement")==2); // builds 1 and 3
    auto thirdReq=Requirements(third.buildNumber);
    assert(thirdReq.size()==1&&thirdReq[0]==ContentClientRequirement::ProtectedFrameXml);
    assert(Requirements(second.buildNumber).empty());
    assert(Extract(third.outputPath,oneAsset.c_str())==legacyBytes); // requirements never change MPQ content
    assert(HasAsset(third.outputPath,baseAsset));

    // Existing build/activation/publication semantics are unchanged: STAGED
    // only, no ACTIVE selection, no allocations or owner rows from raw builds.
    assert(Count("content_manager_server_build")==3);
    assert(Count("content_manager_allocation")==0);
    auto actives=WorldDatabase.Query("SELECT COUNT(*) FROM content_manager_build WHERE state='ACTIVE'");
    assert(actives&&actives->Fetch()[0].Get<uint64>()==0);
    auto staged=WorldDatabase.Query("SELECT COUNT(*) FROM content_manager_build WHERE state='STAGED'");
    assert(staged&&staged->Fetch()[0].Get<uint64>()==3);

    // The command-facing registry limit is applied in SQL and ordered by the
    // actual build number. General lifecycle callers still receive all rows.
    for(std::uint32_t offset=1;offset<=9;++offset)
    {
        auto number=third.buildNumber+offset;
        WorldDatabase.DirectExecute("INSERT INTO content_manager_build(build_number,realm_name,filename,"
            "package_count,file_count,state,sha256) VALUES ("+std::to_string(number)+",'Eitrigg','fixture-"
            +std::to_string(number)+".mpq',1,1,'STAGED','"+std::string(64,'b')+"')");
    }
    std::string listError;
    std::vector<ContentBuildRecord> recent;
    assert(ContentBuildRegistry().GetBuilds(recent,listError,10));
    assert(recent.size()==10&&recent.front().buildNumber==third.buildNumber+9
        &&recent.back().buildNumber==third.buildNumber);
    std::vector<ContentBuildRecord> all;
    assert(ContentBuildRegistry().GetBuilds(all,listError));
    assert(all.size()==12&&all.front().buildNumber==third.buildNumber+9
        &&all.back().buildNumber==first.buildNumber);

    std::cout<<"PASS client requirements: schema-3 declaration, deduplicated union, immutable per-build persistence, empty for no-requirement builds, distinguished missing builds, unchanged MPQ/lifecycle, newest-ten build listing\n";
    return 0;
}
