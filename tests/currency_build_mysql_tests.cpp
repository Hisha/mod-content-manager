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
    std::string itemBaseline, currencyBaseline, baselineError;
    assert(ContentBuildHash::Calculate(root/"baseline/Item.dbc",itemBaseline,baselineError));
    assert(ContentBuildHash::Calculate(root/"baseline/CurrencyTypes.dbc",currencyBaseline,baselineError));
    SQL("INSERT INTO content_manager_allocation VALUES ('Eitrigg','mod-hunts','seal-currency','currency.known-bit',4,'reserved',1,7,"
        +text(currencyBaseline)+",1,1)");
    // A real retained Phase 2/3 identity, using the handoff's baseline hash, must survive.
    SQL("INSERT INTO content_manager_allocation VALUES ('Eitrigg','mod-hunts','seal','item.id',56807,'reserved',1,7,"
        +text(itemBaseline)+",1,1)");
    auto first=ContentBuildService().Build(manager,"Eitrigg",[](auto const& message){std::cout<<message<<'\n';});
    if(!first.success)throw std::runtime_error(first.error);
    assert(first.packageCount==2 && first.fileCount==4);
    auto count=WorldDatabase.Query("SELECT (SELECT COUNT(*) FROM item_template)+(SELECT COUNT(*) FROM currencytypes_dbc)");
    assert(count->Fetch()[0].Get<std::uint64_t>()==0); // Build made no live content changes.
    auto itemBytes=Extract(first.outputPath,"DBFilesClient\\Item.dbc");
    auto currencyBytes=Extract(first.outputPath,"DBFilesClient\\CurrencyTypes.dbc");
    auto categoryBytes=Extract(first.outputPath,"DBFilesClient/CurrencyCategory.dbc");
    auto category=DbcReader::Parse(categoryBytes,*FindDbcDescriptor(12340,"CurrencyCategory"));
    assert(category.valid && category.document.recordCount==9);
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
    assert(leases.size()==3);
    for(auto const& lease:leases)
    {
        assert(lease.lastBuild==second.buildNumber);
        if(lease.resourceKind=="item.id")assert(lease.value==56807);
        if(lease.resourceKind=="currency.known-bit")assert(lease.value==4);
        if(lease.resourceKind=="currency-category.id")
        {
            assert(lease.symbol=="hunts");
            bool found=false;
            for(size_t i=0;i<currency.document.recordCount;++i)
                if(currency.document.words[i*4+1]==56807){assert(currency.document.words[i*4+2]==lease.value);found=true;}
            assert(found);
        }
    }
    assert(Extract(second.outputPath,"DBFilesClient/CurrencyCategory.dbc")==categoryBytes);
    auto baselineCount=WorldDatabase.Query("SELECT COUNT(*) FROM content_manager_baseline");
    assert(baselineCount->Fetch()[0].Get<unsigned>()==3);
    // Approving a valid category baseline replacement preserves the original lease identity.
    auto descriptor=FindDbcDescriptor(12340,"CurrencyCategory");
    auto original=ContentBaselineRegistry::Inspect(root/"baseline",*descriptor);
    auto changed=original.document;changed.words[18]^=1;
    auto saveBaseline=[&](DbcDocument const& d) {
        auto bytes=DbcReader::Serialize(d);std::ofstream out(root/"baseline/CurrencyCategory.dbc",std::ios::binary|std::ios::trunc);
        out.write(reinterpret_cast<char const*>(bytes.data()),bytes.size());assert(out.good());
    };
    saveBaseline(changed);
    assert(!ContentBuildService().Build(manager,"Eitrigg").success);
    auto candidate=ContentBaselineRegistry::Inspect(root/"baseline",*descriptor);
    std::uint64_t review=0;assert(ContentBaselineRegistry::Review(candidate,"test-admin",review,error));
    assert(ContentBaselineRegistry::Approve(candidate,"",review,"test-admin",error));
    auto third=ContentBuildService().Build(manager,"Eitrigg");if(!third.success)throw std::runtime_error(third.error);
    std::vector<ItemAllocation> later;assert(ContentAllocationRegistry().Read("Eitrigg",later,error));
    for(auto const& a:leases)for(auto const& b:later)if(a.resourceKind==b.resourceKind)
        assert(a.value==b.value && a.baselineSha256==b.baselineSha256);
    auto readText=[](std::filesystem::path const& p){std::ifstream f(p);return std::string(std::istreambuf_iterator<char>(f),{});};
    auto bundle=readText(third.outputPath.string()+".server.json");
    auto parity=readText(third.outputPath.string()+".parity.json");
    auto object=nlohmann::json::parse(parity);
    std::vector<ResolvedServerItem> serverRows;assert(ContentServerBundle::ParseServer(bundle,"Eitrigg",serverRows,error));
    assert(ContentServerBundle::VerifyParity(parity,"Eitrigg",third.buildNumber,itemBaseline,
        object.at("clientMpqSha256"),object.at("serverBundleSha256"),serverRows,later,error));
    // Real generated format-3 artifacts require both current snapshot and immutable lease-origin acceptance history.
    SQL("UPDATE content_manager_baseline_history SET sha256="+text(std::string(64,'f'))+" WHERE table_name='CurrencyCategory' AND revision=1");
    std::string summary;
    assert(!ContentServerDeployment::Apply(third.buildNumber,"Eitrigg",root/"output",summary,error));
    SQL("UPDATE content_manager_baseline_history SET sha256="+text(original.hash)+" WHERE table_name='CurrencyCategory' AND revision=1");
    if(!ContentServerDeployment::Apply(third.buildNumber,"Eitrigg",root/"output",summary,error))throw std::runtime_error(error);
    assert(ContentServerDeployment::Apply(third.buildNumber,"Eitrigg",root/"output",summary,error));
    // Baseline approval during the build cannot commit a build against a replaced registry identity.
    WorldDatabase.beforeCommit=[&]{SQL("UPDATE content_manager_baseline SET sha256="+text(std::string(64,'f'))+" WHERE table_name='CurrencyCategory'");};
    auto raced=ContentBuildService().Build(manager,"Eitrigg");assert(!raced.success && !raced.recorded);
    SQL("UPDATE content_manager_baseline SET sha256="+text(candidate.hash)+" WHERE table_name='CurrencyCategory'");
    // Even an explicitly approved baseline may not claim an already leased category ID.
    auto catLease=std::find_if(later.begin(),later.end(),[](auto const& a){return a.resourceKind=="currency-category.id";});
    auto collision=changed;std::vector<std::uint32_t> added(19);added[0]=catLease->value;added[2]=changed.words[2];
    collision.words.insert(collision.words.end(),added.begin(),added.end());++collision.recordCount;
    saveBaseline(collision);auto conflicting=ContentBaselineRegistry::Inspect(root/"baseline",*descriptor);
    assert(ContentBaselineRegistry::Review(conflicting,"test-admin",review,error));
    assert(ContentBaselineRegistry::Approve(conflicting,"",review,"test-admin",error));
    assert(!ContentBuildService().Build(manager,"Eitrigg").success);
    std::cout<<"PASS approved baseline replacement: original leases preserved, current snapshot parity, concurrent registry drift rollback, new baseline collision refused\n";
    // Corrupting the configured baseline is detected without any explicit hash pin.
    { std::ofstream out(root/"baseline/CurrencyCategory.dbc",std::ios::binary|std::ios::app);out.put('x'); }
    auto corrupt=ContentBuildService().Build(manager,"Eitrigg");assert(!corrupt.success);
    std::cout<<"PASS generic baselines: blank pins, legacy lease import, registry persistence, changed baseline refusal\n";
    std::cout<<"PASS production cumulative Build: two EPFs, AQ raw asset exact, all three DBCs, no live content writes, stable leases and deterministic DBC rebuild\n";
}
