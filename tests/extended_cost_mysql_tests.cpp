#include "DatabaseEnv.h"
#include "ContentManager.h"
#include "ContentBuildService.h"
#include "ContentBuildHash.h"
#include "ContentPackage.h"
#include "ContentAllocationRegistry.h"
#include "ContentServerBundle.h"
#include "ContentServerDeployment.h"
#include "DbcReader.h"
#include "third_party/json/json.hpp"
#include <StormLib.h>
#include "ContentExtendedCostServer.h"
#include "third_party/miniz/miniz.h"
#include <cassert>
#include <algorithm>
#include <iostream>
#include <fstream>

namespace { std::filesystem::path root, hunts, aq; std::vector<ContentPackageCandidate> extras; }
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
{auto result=extras;result.push_back({hunts,hunts.filename().string(),ContentPackageSource::Module,"hunts"});result.push_back({aq,aq.filename().string(),ContentPackageSource::Module,"aq"});return result;}
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
using json=nlohmann::json;
static std::string Read(std::filesystem::path const& p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
static void Write(std::filesystem::path const& p,std::vector<std::uint8_t> const& b)
{std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<char const*>(b.data()),b.size());assert(f.good());}
static void Package(std::filesystem::path const& p,json const& m,bool raw=false)
{
    mz_zip_archive z{};assert(mz_zip_writer_init_file(&z,p.c_str(),0));auto s=m.dump();
    assert(mz_zip_writer_add_mem(&z,"manifest.json",s.data(),s.size(),MZ_BEST_COMPRESSION));
    if(raw)assert(mz_zip_writer_add_mem(&z,"raw.dbc","fake",4,MZ_BEST_COMPRESSION));
    assert(mz_zip_writer_finalize_archive(&z));mz_zip_writer_end(&z);
}
static void Install(std::filesystem::path const& p)
{
    auto v=ContentPackage(p).Validate();if(!v.valid)throw std::runtime_error(v.error);auto const& m=v.manifest;auto text=ContentServerBundle::SqlIdentityText;
    SQL("INSERT INTO content_manager_package(package_key,name,version,provider,source_path) VALUES ("
        +text(m.packageKey)+","+text(m.name)+","+text(m.version)+",'fixture',"+text(p.string())+")");
}
static unsigned Scalar(std::string const& sql){auto q=WorldDatabase.Query(sql);assert(q);return q->Fetch()[0].Get<unsigned>();}
int main(int argc,char** argv)
{
    assert(argc==5);WorldDatabase.Connect(argv[1]);CharacterDatabase.Connect(argv[1]);
    root=std::filesystem::absolute(argv[2]);hunts=std::filesystem::absolute(argv[3]);aq=std::filesystem::absolute(argv[4]);
    auto& manager=ContentManager::Instance();manager.LoadConfig();auto text=ContentServerBundle::SqlIdentityText;
    Install(hunts);Install(aq);std::string error,summary;
    auto itemBaseline=ContentBaselineRegistry::Inspect(root/"baseline",*FindDbcDescriptor(12340,"Item"));
    auto currencyBaseline=ContentBaselineRegistry::Inspect(root/"baseline",*FindDbcDescriptor(12340,"CurrencyTypes"));
    auto categoryBaseline=ContentBaselineRegistry::Inspect(root/"baseline",*FindDbcDescriptor(12340,"CurrencyCategory"));
    auto original=ContentBaselineRegistry::Inspect(root/"baseline",*FindDbcDescriptor(12340,"ItemExtendedCost"));
    // Eitrigg's observed retained identities appear ONLY as database fixture leases.
    SQL("INSERT INTO content_manager_allocation VALUES ('Eitrigg','mod-hunts','seal','item.id',56807,'reserved',1,7,"+text(itemBaseline.hash)+",1,1),"
        "('Eitrigg','mod-hunts','seal-currency','currency.known-bit',4,'reserved',1,7,"+text(currencyBaseline.hash)+",1,1),"
        "('Eitrigg','mod-hunts','hunts','currency-category.id',5,'reserved',1,7,"+text(categoryBaseline.hash)+",1,1)");
    auto build=[&]{auto b=ContentBuildService().Build(manager,"Eitrigg");if(!b.success)throw std::runtime_error(b.error);return b;};
    auto legacy=build();assert(legacy.fileCount==4);
    auto itemBytes=Extract(legacy.outputPath,"DBFilesClient/Item.dbc");
    auto currencyBytes=Extract(legacy.outputPath,"DBFilesClient/CurrencyTypes.dbc");
    auto categoryBytes=Extract(legacy.outputPath,"DBFilesClient/CurrencyCategory.dbc");
    assert(Scalar("SELECT COUNT(*) FROM content_manager_baseline WHERE table_name='ItemExtendedCost'")==0);
    std::string state;assert(ContentBaselineRegistry::Status(original,state,error)&&state.find("UNREGISTERED")!=std::string::npos);
    ExtendedCostReferences refs;assert(ContentExtendedCostServer::References(refs,error));
    assert(Scalar("SELECT COUNT(*) FROM content_manager_baseline WHERE table_name='ItemExtendedCost'")==0); // inspect is read only
    // Fill the first gaps with physical overlay, regular vendor, reference-template, event and refund identities.
    auto gaps=std::vector<unsigned>{};auto stock=ItemExtendedCostDbc::Inspect(original.document).ids;
    for(unsigned id=1;gaps.size()<9;++id)if(!stock.count(id))gaps.push_back(id);
    SQL("INSERT INTO itemextendedcost_dbc(ID) VALUES("+std::to_string(gaps[0])+")");
    SQL("INSERT INTO npc_vendor(item,ExtendedCost) VALUES(100,"+std::to_string(gaps[1])+"),(-20,"+std::to_string(gaps[2])+")");
    SQL("INSERT INTO game_event_npc_vendor(ExtendedCost) VALUES("+std::to_string(gaps[3])+")");
    SQL("INSERT INTO item_refund_instance(paidExtendedCost) VALUES("+std::to_string(gaps[4])+")");
    SQL("INSERT INTO content_manager_allocation VALUES ('Eitrigg','removed-package','old-cost','item-extended-cost.id',"
        +std::to_string(gaps[5])+",'retired',1,7,"+text(original.hash)+",1,1)");
    assert(ContentExtendedCostServer::References(refs,error));
    assert(refs.overlay.count(gaps[0])&&refs.vendors.count(gaps[1])&&refs.vendors.count(gaps[2])&&refs.events.count(gaps[3])&&refs.refunds.count(gaps[4]));
    json a={{"schema",2},{"package","test-cost-a"},{"name","Cost A"},{"version","1"},{"extendedCosts",json::array({
        {{"symbol","seal-cost"},{"requirements",json::array({{{"item",{{"package","mod-hunts"},{"symbol","seal"}}},{"count",5}}})}}})}};
    auto b=a;b["package"]="test-cost-b";b["name"]="Cost B";b["extendedCosts"][0]["requirements"][0]["count"]=7;
    auto pa=root/"a.epf",pb=root/"b.epf";Package(pa,a);Package(pb,b);Install(pa);Install(pb);
    extras={{pb,"b.epf",ContentPackageSource::Module,"test"},{pa,"a.epf",ContentPackageSource::Module,"test"}};
    auto first=build();assert(first.packageCount==4&&first.fileCount==5);
    assert(Scalar("SELECT COUNT(*) FROM item_template")==0&&Scalar("SELECT COUNT(*) FROM currencytypes_dbc")==0);
    assert(Scalar("SELECT COUNT(*) FROM itemextendedcost_dbc")==1&&Scalar("SELECT COUNT(*) FROM content_manager_extended_cost_owner")==0);
    assert(Scalar("SELECT COUNT(*) FROM content_manager_baseline")==4);
    auto costBytes=Extract(first.outputPath,"DBFilesClient/ItemExtendedCost.dbc");
    auto composed=DbcReader::Parse(costBytes,*FindDbcDescriptor(12340,"ItemExtendedCost"));
    assert(composed.valid&&composed.document.recordCount==original.document.recordCount+2);
    std::vector<ItemAllocation> leases;assert(ContentAllocationRegistry().Read("Eitrigg",leases,error));assert(leases.size()==6);
    std::vector<ResolvedServerItem> rows;std::vector<ResolvedExtendedCost> costs;
    auto server=Read(first.outputPath.string()+".server.json");auto parity=Read(first.outputPath.string()+".parity.json");auto parityObj=json::parse(parity);
    assert(ContentServerBundle::ParseServer(server,"Eitrigg",rows,error,&costs)&&costs.size()==2&&rows.size()==1);
    assert(costs[0].id==gaps[6]&&costs[1].id==gaps[7]);
    for(auto const& c:costs){assert(c.requirements.size()==1&&c.requirements[0].itemId==56807);}
    assert(costs[0].requirements[0].count==5&&costs[1].requirements[0].count==7);
    auto active=leases;active.erase(std::remove_if(active.begin(),active.end(),[](auto const& x){return x.state=="retired";}),active.end());
    assert(ContentServerBundle::VerifyParity(parity,"Eitrigg",first.buildNumber,itemBaseline.hash,
        parityObj["clientMpqSha256"],parityObj["serverBundleSha256"],rows,active,error,costs));
    auto wrong=costs;wrong[0].requirements[0].itemId++;
    assert(!ContentServerBundle::VerifyParity(parity,"Eitrigg",first.buildNumber,itemBaseline.hash,
        parityObj["clientMpqSha256"],parityObj["serverBundleSha256"],rows,active,error,wrong));
    assert(Extract(first.outputPath,"DBFilesClient/Item.dbc")==itemBytes);
    assert(Extract(first.outputPath,"DBFilesClient/CurrencyTypes.dbc")==currencyBytes);
    assert(Extract(first.outputPath,"DBFilesClient/CurrencyCategory.dbc")==categoryBytes);
    for(auto const& entry:ContentPackage(aq).Validate().manifest.content)
        assert(Extract(first.outputPath,entry.target.c_str())==Extract(legacy.outputPath,entry.target.c_str()));
    // Reopen DB connections to prove lease reconstruction does not use process-local state.
    WorldDatabase.Connect(argv[1]);CharacterDatabase.Connect(argv[1]);
    auto second=build();assert(Extract(second.outputPath,"DBFilesClient/ItemExtendedCost.dbc")==costBytes);
    std::vector<ItemAllocation> later;assert(ContentAllocationRegistry().Read("Eitrigg",later,error));
    for(auto const& old:leases)for(auto const& now:later)
        if(old.packageKey==now.packageKey&&old.symbol==now.symbol&&old.resourceKind==now.resourceKind)
            assert(old.value==now.value&&old.baselineSha256==now.baselineSha256);
    // A new world reference between preflight and lease commit must abort the build transaction.
    auto racedId=std::to_string(costs[0].id);
    WorldDatabase.beforeCommit=[&]{SQL("INSERT INTO npc_vendor(item,ExtendedCost) VALUES(123,"+racedId+")");};
    auto raced=ContentBuildService().Build(manager,"Eitrigg");assert(!raced.success&&!raced.recorded);
    assert(Scalar("SELECT COUNT(*) FROM content_manager_build WHERE build_number="+std::to_string(raced.buildNumber))==0);
    SQL("DELETE FROM npc_vendor WHERE item=123 AND ExtendedCost="+racedId);
    // Explicitly discard only this failed fixture's preserved artifacts so later test builds can retry the number.
    for(auto suffix:{"",".server.json",".parity.json"})std::filesystem::remove(raced.outputPath.string()+suffix);
    auto invalid=a;invalid["extendedCosts"][0]["requirements"][0]["item"]["symbol"]="missing";Package(pa,invalid);
    assert(!ContentBuildService().Build(manager,"Eitrigg").success);Package(pa,a);
    json raw={{"schema",1},{"package","test-raw-cost"},{"name","raw"},{"version","1"},{"content",json::array({
        {{"type","file"},{"source","raw.dbc"},{"target","DBFilesClient/ItemExtendedCost.dbc"}}})}};
    auto pr=root/"raw.epf";Package(pr,raw,true);Install(pr);extras.push_back({pr,"raw.epf",ContentPackageSource::Module,"test"});
    assert(!ContentBuildService().Build(manager,"Eitrigg").success);extras.pop_back();SQL("DELETE FROM content_manager_package WHERE package_key='test-raw-cost'");
    // Dangling baseline item references must block item reuse (no new Seal identity).
    auto changed=original.document;changed.words[4]=56807;Write(root/"baseline/ItemExtendedCost.dbc",DbcReader::Serialize(changed));
    assert(!ContentBuildService().Build(manager,"Eitrigg").success); // unexpected drift
    Write(root/"baseline/ItemExtendedCost.dbc",DbcReader::Serialize(original.document));
    // An unowned row appearing after STAGED prevents apply.
    auto id=std::to_string(costs[0].id);SQL("INSERT INTO itemextendedcost_dbc(ID) VALUES("+id+")");
    assert(!ContentServerDeployment::Apply(second.buildNumber,"Eitrigg",root/"output",summary,error));
    assert(Scalar("SELECT COUNT(*) FROM item_template")==0);SQL("DELETE FROM itemextendedcost_dbc WHERE ID="+id);
    // Inject the same collision after preflight: all item/currency/cost writes roll back.
    WorldDatabase.beforeCommit=[&]{SQL("INSERT INTO itemextendedcost_dbc(ID) VALUES("+id+")");};
    assert(!ContentServerDeployment::Apply(second.buildNumber,"Eitrigg",root/"output",summary,error));
    assert(Scalar("SELECT COUNT(*) FROM item_template")==0&&Scalar("SELECT COUNT(*) FROM content_manager_extended_cost_owner")==0);
    SQL("DELETE FROM itemextendedcost_dbc WHERE ID="+id);
    auto vendorCount=Scalar("SELECT COUNT(*) FROM npc_vendor"),eventCount=Scalar("SELECT COUNT(*) FROM game_event_npc_vendor");
    if(!ContentServerDeployment::Apply(second.buildNumber,"Eitrigg",root/"output",summary,error))throw std::runtime_error(error);
    assert(ContentServerDeployment::Apply(second.buildNumber,"Eitrigg",root/"output",summary,error));
    assert(Scalar("SELECT COUNT(*) FROM npc_vendor")==vendorCount&&Scalar("SELECT COUNT(*) FROM game_event_npc_vendor")==eventCount);
    assert(Scalar("SELECT COUNT(*) FROM content_manager_extended_cost_owner")==2);
    // Existing applied definition can be referenced by a refund and retained on rebuild.
    SQL("INSERT INTO item_refund_instance(paidExtendedCost) VALUES("+id+")");
    auto third=build();assert(Extract(third.outputPath,"DBFilesClient/ItemExtendedCost.dbc")==costBytes);
    invalid=a;invalid["extendedCosts"][0]["requirements"][0]["count"]=6;Package(pa,invalid);
    assert(!ContentBuildService().Build(manager,"Eitrigg").success);Package(pa,a); // immutable after apply
    SQL("UPDATE itemextendedcost_dbc SET ItemCount_1=6 WHERE ID="+id);
    assert(!ContentBuildService().Build(manager,"Eitrigg").success);
    assert(!ContentServerDeployment::Apply(third.buildNumber,"Eitrigg",root/"output",summary,error));
    SQL("UPDATE itemextendedcost_dbc SET ItemCount_1=5 WHERE ID="+id);
    // Approved noncolliding baseline replacement retains origin hash and identity.
    changed=original.document;changed.words[15]=1;Write(root/"baseline/ItemExtendedCost.dbc",DbcReader::Serialize(changed));
    auto replacement=ContentBaselineRegistry::Inspect(root/"baseline",*FindDbcDescriptor(12340,"ItemExtendedCost"));
    std::uint64_t review=0;assert(ContentBaselineRegistry::Review(replacement,"fixture-admin",review,error));
    assert(ContentBaselineRegistry::Approve(replacement,"",review,"fixture-admin",error));
    auto fourth=build();
    assert(ContentAllocationRegistry().Read("Eitrigg",later,error));
    for(auto const& x:later)if(x.resourceKind=="item-extended-cost.id")assert(x.baselineSha256==original.hash);
    if(!ContentServerDeployment::Apply(fourth.buildNumber,"Eitrigg",root/"output",summary,error))throw std::runtime_error(error);
    // Even a dangling item reference in an approved baseline reserves that Item ID.
    auto dangling=changed;dangling.words[4]=56807;Write(root/"baseline/ItemExtendedCost.dbc",DbcReader::Serialize(dangling));
    auto danglingBaseline=ContentBaselineRegistry::Inspect(root/"baseline",*FindDbcDescriptor(12340,"ItemExtendedCost"));
    assert(ContentBaselineRegistry::Review(danglingBaseline,"fixture-admin",review,error));
    assert(ContentBaselineRegistry::Approve(danglingBaseline,"",review,"fixture-admin",error));
    assert(!ContentBuildService().Build(manager,"Eitrigg").success);
    // An approved physical collision remains forbidden.
    changed.words.insert(changed.words.end(),16,0);changed.words[changed.words.size()-16]=costs[0].id;++changed.recordCount;
    Write(root/"baseline/ItemExtendedCost.dbc",DbcReader::Serialize(changed));
    replacement=ContentBaselineRegistry::Inspect(root/"baseline",*FindDbcDescriptor(12340,"ItemExtendedCost"));
    assert(ContentBaselineRegistry::Review(replacement,"fixture-admin",review,error));assert(ContentBaselineRegistry::Approve(replacement,"",review,"fixture-admin",error));
    assert(!ContentBuildService().Build(manager,"Eitrigg").success);
    std::cout<<"PASS Phase 5 real-MySQL production Build/Apply: 4 EPFs; all occupancy sources; preserved Seal/category/bit leases; cross-package symbols; stock and AQ bytes; deterministic restart; generic registry/history; drift; collision; raw conflict; unresolved symbol; parity; immutable applied costs; rollback; explicit deployment; no vendor writes\n";
}
