// Production uninstall lifecycle against disposable MySQL: present-source
// uninstall, SOURCE MISSING uninstall (the rolled-back-module case), rebuild
// membership, deterministic rebuild, and complete retention of history.
// Uses two schema-1 raw-only EPFs so no DBC baselines are required.
#include "DatabaseEnv.h"
#include "ContentManager.h"
#include "ContentBuildService.h"
#include "ContentBuildHash.h"
#include "ContentPackage.h"
#include "ContentPackageRegistry.h"
#include "ContentPackageLifecycle.h"
#include "ContentAllocationRegistry.h"
#include "ContentServerOwnership.h"
#include "ContentServerBundle.h"
#include <StormLib.h>
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace { std::filesystem::path root, nativeSocial, aq, aqAsset; }
// Only config/discovery are injected. Registry, build, lifecycle survey, parity and MPQ are production code.
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
{return {{nativeSocial,nativeSocial.filename().string(),ContentPackageSource::Module,"fixture"},{aq,aq.filename().string(),ContentPackageSource::Module,"fixture"}};}
static std::vector<std::uint8_t> Extract(std::filesystem::path const& path,char const* name)
{
    HANDLE archive=nullptr,file=nullptr;assert(SFileOpenArchive(path.c_str(),0,MPQ_OPEN_READ_ONLY,&archive));
    std::string mpqName(name);std::replace(mpqName.begin(),mpqName.end(),'/','\\');
    if (!SFileOpenFileEx(archive,mpqName.c_str(),SFILE_OPEN_FROM_MPQ,&file))
        throw std::runtime_error("Missing MPQ entry: "+mpqName);
    auto size=SFileGetFileSize(file,nullptr);std::vector<std::uint8_t> bytes(size);DWORD read=0;
    assert(SFileReadFile(file,bytes.data(),size,&read,nullptr)&&read==size);
    SFileCloseFile(file);SFileCloseArchive(archive);return bytes;
}
static std::uint64_t Count(char const* table)
{
    auto query=WorldDatabase.Query(std::string("SELECT COUNT(*) FROM ")+table);
    assert(query);
    return query->Fetch()[0].Get<std::uint64_t>();
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
int main(int argc,char** argv)
{
    assert(argc==5);WorldDatabase.Connect(argv[1]);
    root=std::filesystem::absolute(argv[2]);
    nativeSocial=std::filesystem::absolute(argv[3]);aq=std::filesystem::absolute(argv[4]);
    assert(std::filesystem::exists(nativeSocial)&&std::filesystem::exists(aq));
    auto& manager=ContentManager::Instance();manager.LoadConfig();
    // Fixture copies so a missing source is simulated without losing the originals.
    std::filesystem::create_directories(root/"baseline");std::filesystem::create_directories(root/"staging");
    std::filesystem::create_directories(root/"output");
    auto fixtureNative=root/"mod-native-social.epf";auto fixtureAq=root/"aq-scarab-gong-marker.epf";
    std::filesystem::copy_file(nativeSocial,fixtureNative,std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(aq,fixtureAq,std::filesystem::copy_options::overwrite_existing);
    nativeSocial=fixtureNative;aq=fixtureAq;
    auto const nsManifest=ContentPackage(nativeSocial).Validate().manifest;
    auto const aqManifest=ContentPackage(aq).Validate().manifest;
    assert(nsManifest.itemRows.empty()&&nsManifest.serverItemRows.empty());
    assert(aqManifest.itemRows.empty()&&aqManifest.serverItemRows.empty());
    std::string const aqKey=aqManifest.packageKey;
    std::string const nsAsset=nsManifest.content.front().target;
    aqAsset=aqManifest.content.front().target;

    auto install=[&](ContentPackageManifest const& m,std::filesystem::path const& source){
        auto r=ContentPackageRegistry().Install({m.packageKey,m.name,m.version,"fixture",source.string(),{}});
        assert(r.success&&r.changed);
    };
    install(nsManifest,nativeSocial);install(aqManifest,aq);

    // Pre-removal survey with both sources present.
    ContentPackageRemovalAnalysis survey;std::string error;
    assert(ContentPackageLifecycle::Analyse("Eitrigg",aqKey,true,survey,error));
    assert(survey.installed&&survey.sourceState==ContentInstalledSourceState::Active&&survey.currentlyDiscovered);
    assert(survey.allocations.empty()&&!ContentPackageLifecycle::HasRetainedHistory(survey));

    auto build=[&](std::string const& label){
        auto result=ContentBuildService().Build(manager,"Eitrigg",[&](std::string const& message){std::cout<<message<<'\n';});
        if(!result.success)std::cout<<label<<" error: "<<result.error<<"\n";
        assert(result.success);
        return result;
    };
    auto first=build("first");
    assert(first.packageCount==2&&first.fileCount==4); // native-social 3 raw files + aq 1 raw file
    std::vector<std::uint8_t> nsFirst=Extract(first.outputPath,nsAsset.c_str());
    assert(HasAsset(first.outputPath,aqAsset));
    assert(HasAsset(first.outputPath,nsAsset));

    // SOURCE MISSING dead-end: a rolled-back module leaves its recorded source gone.
    std::filesystem::remove(aq);
    survey=ContentPackageRemovalAnalysis{};
    assert(ContentPackageLifecycle::Analyse("Eitrigg",aqKey,false,survey,error));
    assert(survey.installed&&survey.sourceState==ContentInstalledSourceState::SourceMissing&&!survey.currentlyDiscovered);
    auto dead=ContentBuildService().Build(manager,"Eitrigg");
    assert(!dead.success&&dead.error.find(aqKey)!=std::string::npos);
    std::cout<<"PASS source-missing build dead-end reproduced: "<<dead.error<<"\n";

    // Uninstall removes only the desired-state selection; history stays.
    auto removal=ContentPackageRegistry().Uninstall(aqKey);
    assert(removal.success&&removal.changed);
    auto again=ContentPackageRegistry().Uninstall(aqKey);
    assert(again.success&&!again.changed);
    auto unknown=ContentPackageRegistry().Uninstall("mod-not-installed");
    assert(unknown.success&&!unknown.changed);
    survey=ContentPackageRemovalAnalysis{};
    assert(ContentPackageLifecycle::Analyse("Eitrigg",aqKey,false,survey,error));
    assert(!survey.installed&&survey.allocations.empty()&&!ContentPackageLifecycle::HasRetainedHistory(survey));

    // Rebuild now includes only the retained package, without needing the lost EPF.
    auto withdrawn=build("withdrawn");
    assert(withdrawn.packageCount==1&&withdrawn.fileCount==3);
    assert(!HasAsset(withdrawn.outputPath,aqAsset));
    assert(HasAsset(withdrawn.outputPath,nsAsset));
    assert(Extract(withdrawn.outputPath,nsAsset.c_str())==nsFirst); // deterministic rebuild

    // Reinstall needs the source restored; then the package returns identically.
    std::filesystem::remove_all(aq);std::filesystem::copy_file(std::filesystem::absolute(argv[4]),aq);
    survey=ContentPackageRemovalAnalysis{};
    assert(ContentPackageLifecycle::Analyse("Eitrigg",aqKey,true,survey,error));
    assert(!survey.installed&&survey.currentlyDiscovered);
    install(aqManifest,aq);
    auto restored=build("restored");
    assert(restored.packageCount==2&&restored.fileCount==4);
    assert(HasAsset(restored.outputPath,aqAsset));

    assert(Count("content_manager_build")==3);        // three recorded builds, failed build unrecorded
    assert(Count("content_manager_server_build")==3); // one sidecar per recorded build
    assert(Count("content_manager_allocation")==0);   // raw-only packages allocate no DBC resources
    assert(Count("content_manager_item_owner")==0);
    assert(Count("content_manager_currency_owner")==0);
    assert(Count("content_manager_extended_cost_owner")==0);
    assert(Count("content_manager_vendor_owner")==0);
    assert(Count("content_manager_package")==2);      // both selections restored

    // Activation is a separate, explicit step: uninstalling never activated anything.
    auto actives=WorldDatabase.Query("SELECT COUNT(*) FROM content_manager_build WHERE state='ACTIVE'");
    assert(actives&&actives->Fetch()[0].Get<uint64>()==0);

    std::cout<<"PASS uninstall lifecycle: survey, source-missing removal, no-EPF rebuild, retention of builds/sidecars/allocations/ownership, deterministic reinstall\n";
    return 0;
}