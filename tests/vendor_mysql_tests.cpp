#include "DatabaseEnv.h"
#include "ContentManager.h"
#include "ContentBuildRegistry.h"
#include "ContentServerDeployment.h"
#include "ContentVendorServer.h"
#include "ContentBuildService.h"
#include "ContentBuildHash.h"
#include "ContentPackage.h"
#include "ContentAllocationRegistry.h"
#include "ContentServerBundle.h"
#include "ContentPackageRegistry.h"
#include "ItemExtendedCostDbc.h"
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
    _itemBaselineSha256.clear(); _currencyTypesBaselineSha256.clear(); // Normal operation needs no per-DBC pins.
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
static unsigned Scalar(std::string const& sql)
{
    auto q=WorldDatabase.Query(sql);assert(q);return q->Fetch()[0].Get<unsigned>();
}
static void Install(std::filesystem::path const& path)
{
    auto v=ContentPackage(path).Validate();if(!v.valid)throw std::runtime_error(v.error);
    auto const& m=v.manifest;
    auto installed=ContentPackageRegistry().Install({m.packageKey,m.name,m.version,"fixture",path.string(),{}});
    if(!installed.success)throw std::runtime_error(installed.error);
}
static std::string Read(std::filesystem::path const& path)
{
    std::ifstream f(path);assert(f.is_open());return std::string(std::istreambuf_iterator<char>(f),{});
}
static std::set<std::string> Entries(std::filesystem::path const& path)
{
    HANDLE archive=nullptr;assert(SFileOpenArchive(path.c_str(),0,MPQ_OPEN_READ_ONLY,&archive));
    SFILE_FIND_DATA data{};auto search=SFileFindFirstFile(archive,"*",&data,nullptr);assert(search!=nullptr);
    std::set<std::string> names;
    do
    {
        std::string name=data.cFileName;
        if(!name.empty()&&name[0]!='('){std::replace(name.begin(),name.end(),'\\','/');names.insert(name);}
    }while(SFileFindNextFile(search,&data));
    SFileFindClose(search);SFileCloseArchive(archive);return names;
}
int main(int argc,char** argv)
{
    assert(argc==6);WorldDatabase.Connect(argv[1]);CharacterDatabase.Connect(argv[1]);
    root=std::filesystem::absolute(argv[2]);hunts=argv[3];auto updated=std::filesystem::path(argv[4]);aq=argv[5];
    auto& manager=ContentManager::Instance();manager.LoadConfig();Install(hunts);Install(aq);
    std::string error,summary;auto text=ContentServerBundle::SqlIdentityText;
    auto build=[&]{auto b=ContentBuildService().Build(manager,"Eitrigg");if(!b.success)throw std::runtime_error(b.error);return b;};
    auto old=build();assert(ContentServerDeployment::Apply(old.buildNumber,"Eitrigg",root/"output",summary,error));
    ContentPublicationResult publication;bool already=false;
    assert(ContentBuildRegistry().ActivateBuild(old.buildNumber,root/"output",root/"published",publication,already,error));
    std::vector<ItemAllocation> before;assert(ContentAllocationRegistry().Read("Eitrigg",before,error));assert(before.size()==4);
    assert(ContentPackageRegistry().Uninstall("mod-hunts").success);hunts=updated;Install(hunts);
    auto next=build();assert(Scalar("SELECT COUNT(*) FROM npc_vendor")==1); // only occupancy fixture
    assert(Scalar("SELECT COUNT(*) FROM content_manager_vendor_owner")==0);
    assert(Scalar("SELECT npcflag FROM creature_template WHERE entry=14999989")==1);
    assert(Scalar("SELECT build_number FROM content_manager_build WHERE state='ACTIVE'")==old.buildNumber);
    auto manifest=Read(next.outputPath.string()+".server.json");
    std::vector<ResolvedServerItem> rows;std::vector<ResolvedExtendedCost> costs;std::vector<ResolvedVendorRow> vendors;
    assert(ContentServerBundle::ParseServer(manifest,"Eitrigg",rows,error,&costs,&vendors));assert(vendors.size()==1 && costs.size()==1);
    auto v=vendors[0];assert(v.itemEntry==40717 && v.creatureEntry==14999989 && v.costId==costs[0].id && v.costSymbol=="seal-cost-5");
    auto repeat=build();assert(Read(repeat.outputPath.string()+".server.json")==manifest);
    for(auto const& name:Entries(old.outputPath))assert(Extract(old.outputPath,name.c_str())==Extract(next.outputPath,name.c_str()));
    std::vector<ItemAllocation> after;assert(ContentAllocationRegistry().Read("Eitrigg",after,error));assert(after.size()==before.size());
    for(auto const& a:before)assert(std::any_of(after.begin(),after.end(),[&](auto const& b){return a.packageKey==b.packageKey && a.symbol==b.symbol && a.resourceKind==b.resourceKind && a.value==b.value && a.baselineSha256==b.baselineSha256;}));
    // Unowned row, even with identical merchandise/cost, must never be adopted.
    SQL("INSERT INTO npc_vendor(entry,item,ExtendedCost) VALUES(14999989,40717,"+std::to_string(v.costId)+")");
    assert(!ContentServerDeployment::Apply(next.buildNumber,"Eitrigg",root/"output",summary,error));
    SQL("DELETE FROM npc_vendor WHERE entry=14999989");
    // Drift introduced between preflight and transaction is caught by SQL guards.
    WorldDatabase.beforeCommit=[] {SQL("UPDATE creature_template SET npcflag=3 WHERE entry=14999989");};
    assert(!ContentServerDeployment::Apply(next.buildNumber,"Eitrigg",root/"output",summary,error));
    assert(Scalar("SELECT COUNT(*) FROM content_manager_vendor_owner")==0);
    assert(Scalar("SELECT COUNT(*) FROM npc_vendor WHERE entry=14999989")==0);
    SQL("UPDATE creature_template SET npcflag=1 WHERE entry=14999989");
    // Failure after vendor insertion rolls back the vendor, flags, owners, and other resources.
    SQL("CREATE TRIGGER reject_vendor_owner BEFORE INSERT ON content_manager_vendor_owner FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected rollback'");
    assert(!ContentServerDeployment::Apply(next.buildNumber,"Eitrigg",root/"output",summary,error));
    assert(Scalar("SELECT COUNT(*) FROM npc_vendor WHERE entry=14999989")==0);
    assert(Scalar("SELECT npcflag FROM creature_template WHERE entry=14999989")==1);
    SQL("DROP TRIGGER reject_vendor_owner");
    if(!ContentServerDeployment::Apply(next.buildNumber,"Eitrigg",root/"output",summary,error))throw std::runtime_error(error);
    assert(Scalar("SELECT ExtendedCost FROM npc_vendor WHERE entry=14999989")==v.costId);
    assert(Scalar("SELECT npcflag FROM creature_template WHERE entry=14999989")==129);
    ContentServerStatus status;assert(ContentServerDeployment::Inspect(next.buildNumber,"Eitrigg",root/"output",status,rows,error));
    assert(ContentServerDeployment::Apply(next.buildNumber,"Eitrigg",root/"output",summary,error));
    SQL("UPDATE npc_vendor SET maxcount=1 WHERE entry=14999989");
    assert(!ContentServerDeployment::Inspect(next.buildNumber,"Eitrigg",root/"output",status,rows,error));
    SQL("UPDATE npc_vendor SET maxcount=0 WHERE entry=14999989");
    auto retained=build();assert(Read(retained.outputPath.string()+".server.json")==manifest);
    // Only explicit activation replaces the old ACTIVE build.
    assert(Scalar("SELECT build_number FROM content_manager_build WHERE state='ACTIVE'")==old.buildNumber);
    assert(ContentBuildRegistry().ActivateBuild(next.buildNumber,root/"output",root/"published",publication,already,error));
    assert(Scalar("SELECT build_number FROM content_manager_build WHERE state='ACTIVE'")==next.buildNumber);
    std::cout<<"PASS symbolic vendor, deterministic composition, retained resources, STAGED isolation, unowned collision, race/drift rollback, apply idempotence, activation, all DBC and AQ payloads unchanged\n";
}
