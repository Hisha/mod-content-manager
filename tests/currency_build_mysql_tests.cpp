#include "DatabaseEnv.h"
#include "ContentManager.h"
#include "ContentBuildService.h"
#include "ContentBuildHash.h"
#include "ContentPackage.h"
#include "ContentAllocationRegistry.h"
#include "ContentServerBundle.h"
#include "DbcReader.h"
#include "third_party/json/json.hpp"
#include <StormLib.h>
#include <cassert>
#include <algorithm>
#include <iostream>
#include <fstream>

namespace { std::filesystem::path root, hunts, aq; }
// Only config/discovery are injected. Build, staging, allocation registry, parity and MPQ are production code.
ContentManager& ContentManager::Instance(){static ContentManager manager;return manager;}
void ContentManager::LoadConfig()
{
    _enabled=true;_clientBuild=12340;_baselineDbcDirectory=(root/"baseline").string();
    _workDirectory=(root/"staging").string();_outputDirectory=(root/"output").string();
    std::string error;
    assert(ContentBuildHash::Calculate(root/"baseline/Item.dbc",_itemBaselineSha256,error));
    assert(ContentBuildHash::Calculate(root/"baseline/CurrencyTypes.dbc",_currencyTypesBaselineSha256,error));
}
bool ContentManager::IsEnabled() const{return _enabled;}
std::string const& ContentManager::GetBaselineDbcDirectory() const{return _baselineDbcDirectory;}
std::string const& ContentManager::GetWorkDirectory() const{return _workDirectory;}
std::string const& ContentManager::GetOutputDirectory() const{return _outputDirectory;}
std::string const& ContentManager::GetItemBaselineSha256() const{return _itemBaselineSha256;}
std::uint32_t ContentManager::GetClientBuild() const{return _clientBuild;}
std::vector<ContentPackageCandidate> ContentManager::ScanAvailablePackages() const
{return {{hunts,hunts.filename().string(),ContentPackageSource::Module,"hunts"},{aq,aq.filename().string(),ContentPackageSource::Module,"aq"}};}
static void SQL(std::string const& sql){if(!WorldDatabase.Execute(sql))throw std::runtime_error(WorldDatabase.lastError);}
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
int main(int argc,char** argv)
{
    assert(argc==5);WorldDatabase.Connect(argv[1]);CharacterDatabase.Connect(argv[1]);
    root=std::filesystem::absolute(argv[2]);hunts=std::filesystem::absolute(argv[3]);aq=std::filesystem::absolute(argv[4]);
    auto& manager=ContentManager::Instance();manager.LoadConfig();auto text=ContentServerBundle::SqlIdentityText;
    for(auto const& p:{hunts,aq})
    {
        auto validation=ContentPackage(p).Validate();assert(validation.valid);auto const& m=validation.manifest;
        SQL("INSERT INTO content_manager_package(package_key,name,version,provider,source_path) VALUES ("
            +text(m.packageKey)+","+text(m.name)+","+text(m.version)+",'fixture',"+text(p.string())+")");
    }
    // A real retained Phase 2/3 identity, using the handoff's baseline hash, must survive.
    SQL("INSERT INTO content_manager_allocation VALUES ('Eitrigg','mod-hunts','seal','item.id',56807,'reserved',1,7,"
        +text(manager.GetItemBaselineSha256())+",1,1)");
    auto first=ContentBuildService().Build(manager,"Eitrigg",[](auto const& message){std::cout<<message<<'\n';});
    if(!first.success)throw std::runtime_error(first.error);
    assert(first.packageCount==2 && first.fileCount==3);
    auto count=WorldDatabase.Query("SELECT (SELECT COUNT(*) FROM item_template)+(SELECT COUNT(*) FROM currencytypes_dbc)");
    assert(count->Fetch()[0].Get<std::uint64_t>()==0); // Build made no live content changes.
    auto itemBytes=Extract(first.outputPath,"DBFilesClient\\Item.dbc");
    auto currencyBytes=Extract(first.outputPath,"DBFilesClient\\CurrencyTypes.dbc");
    auto item=DbcReader::Parse(itemBytes,*FindDbcDescriptor(12340,"Item"));
    auto currency=DbcReader::Parse(currencyBytes,*FindDbcDescriptor(12340,"CurrencyTypes"));
    assert(item.valid&&item.document.recordCount==46097&&currency.valid&&currency.document.recordCount==27);
    assert(item.document.words[(item.document.recordCount-1)*8]==56807);
    auto aqManifest=ContentPackage(aq).Validate().manifest;
    auto stage=root/"aq-check";std::filesystem::create_directories(stage);
    assert(ContentPackage(aq).StageInto(stage,aqManifest).success);
    for(auto const& entry:aqManifest.content)
    {
        auto actual=Extract(first.outputPath,entry.target.c_str());
        std::ifstream input(stage/entry.target,std::ios::binary);
        std::vector<std::uint8_t> expected((std::istreambuf_iterator<char>(input)),{});
        assert(actual==expected);
    }
    auto second=ContentBuildService().Build(manager,"Eitrigg");if(!second.success)throw std::runtime_error(second.error);
    assert(Extract(second.outputPath,"DBFilesClient\\Item.dbc")==itemBytes);
    assert(Extract(second.outputPath,"DBFilesClient\\CurrencyTypes.dbc")==currencyBytes);
    std::vector<ItemAllocation> leases;std::string error;assert(ContentAllocationRegistry().Read("Eitrigg",leases,error));
    assert(leases.size()==2);
    for(auto const& lease:leases)assert(lease.lastBuild==second.buildNumber);
    std::cout<<"PASS production cumulative Build: two EPFs, AQ raw asset exact, both DBCs, no live content writes, stable leases and deterministic DBC rebuild\n";
}
