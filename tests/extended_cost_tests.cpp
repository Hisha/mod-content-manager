#include "ItemExtendedCostDbc.h"
#include "ContentBuildHash.h"
#include "ContentResourceAllocator.h"
#include "ContentPackage.h"
#include "ContentServerBundle.h"
#include "third_party/json/json.hpp"
#include "third_party/miniz/miniz.h"
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <iostream>
using json=nlohmann::json;
bool ContentBuildHash::Valid(std::string const& s)
{return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
template<class F> void Reject(F f){bool rejected=false;try{f();}catch(std::exception const&){rejected=true;}assert(rejected);}
void Save(std::filesystem::path const& p,json const& m)
{
    mz_zip_archive z{};assert(mz_zip_writer_init_file(&z,p.c_str(),0));auto s=m.dump();
    assert(mz_zip_writer_add_mem(&z,"manifest.json",s.data(),s.size(),MZ_BEST_COMPRESSION));
    assert(mz_zip_writer_finalize_archive(&z));mz_zip_writer_end(&z);
}
int main(int argc,char**argv)
{
    auto d=FindDbcDescriptor(12340,"ItemExtendedCost");assert(d&&d->fields.size()==16&&d->version==1);
    char const* names[]={"ID","HonorPoints","ArenaPoints","ArenaBracket","ItemID_1","ItemID_2","ItemID_3","ItemID_4","ItemID_5",
        "ItemCount_1","ItemCount_2","ItemCount_3","ItemCount_4","ItemCount_5","RequiredArenaRating","ItemPurchaseGroup"};
    for(unsigned i=0;i<16;++i)assert(std::string(d->fields[i].name)==names[i]);
    assert(std::string(d->fields[0].allocationNamespace)=="item-extended-cost.id");
    assert(!FindDbcDescriptor(10192,"ItemExtendedCost"));
    DbcDocument base; base.recordCount=1;base.fieldCount=16;base.recordSize=64;base.stringBlockSize=1;base.strings={0};
    base.words={1,13,9,0,117,118,0,0,0,1,2,1,1,1,0,17}; // Stock-style empty-slot counts and nonzero purchase group stay intact.
    assert(ItemExtendedCostDbc::Inspect(base).purchaseGroups.count(17));
    ResolvedExtendedCost a{"test-a","1","cost",2,{{"mod-hunts","seal",56807,5}}};
    auto b=a;b.packageKey="test-b";b.id=3;b.requirements[0].count=7;
    auto bytes=ItemExtendedCostDbc::Compose(base,{a,b});
    assert(bytes==ItemExtendedCostDbc::Compose(base,{b,a}));
    auto parsed=DbcReader::Parse(bytes,*d);assert(parsed.valid&&parsed.document.recordCount==3);
    assert(std::equal(base.words.begin(),base.words.end(),parsed.document.words.begin()));
    assert(parsed.document.strings==base.strings&&parsed.document.words[16+4]==56807&&parsed.document.words[16+9]==5);
    assert(DbcReader::Serialize(parsed.document)==bytes);
    auto bad=base;bad.fieldCount=15;Reject([&]{ItemExtendedCostDbc::Inspect(bad);});
    bad=base;bad.words[0]=0;Reject([&]{ItemExtendedCostDbc::Inspect(bad);});
    bad=base;bad.words[14]=0xffffffff;Reject([&]{ItemExtendedCostDbc::Inspect(bad);});
    auto truncated=bytes;truncated.pop_back();assert(!DbcReader::Parse(truncated,*d).valid);
    auto dims=bytes;dims[8]=15;assert(!DbcReader::Parse(dims,*d).valid);
    Reject([&]{ItemExtendedCostDbc::Compose(base,{a,a});});
    auto c=a;c.id=1;Reject([&]{ItemExtendedCostDbc::Compose(base,{c});});
    c=a;c.id=65536;Reject([&]{ItemExtendedCostDbc::Words(c);});
    c=a;c.requirements[0].count=0;Reject([&]{ItemExtendedCostDbc::Words(c);});
    c=a;c.requirements[0].count=ItemExtendedCostDbc::MaxCount+1;Reject([&]{ItemExtendedCostDbc::Words(c);});
    c=a;c.honorPoints=ItemExtendedCostDbc::MaxPoints+1;Reject([&]{ItemExtendedCostDbc::Words(c);});
    c=a;c.requirements.push_back(c.requirements[0]);Reject([&]{ItemExtendedCostDbc::Words(c);});
    c=a;c.arenaBracket=1;Reject([&]{ItemExtendedCostDbc::Words(c);});
    c.requiredArenaRating=1500;c.arenaPoints=250;auto w=ItemExtendedCostDbc::Words(c);assert(w[2]==250&&w[3]==1&&w[14]==1500&&w[15]==0);
    auto policy=ContentResourceAllocator::ItemExtendedCostIdPolicy();assert(policy.firstCandidate==1&&policy.lastCandidate==65535);
    std::vector<ResourceAllocationRequest> requests={{"test-b","cost","item-extended-cost.id"},{"test-a","cost","item-extended-cost.id"}};
    std::string hash(64,'a');auto plan=ContentResourceAllocator::Plan("Eitrigg",policy,requests,{}, {1,2,4,6},1,hash);
    assert(plan[0].value==3&&plan[1].value==5);
    auto restart=ContentResourceAllocator::Plan("Eitrigg",policy,requests,plan,{1,2,4,6},2,hash);
    assert(restart[0].value==3&&restart[1].value==5);
    auto retired=plan;retired[0].state="retired";
    auto next=ContentResourceAllocator::Plan("Eitrigg",policy,{{"test-c","cost","item-extended-cost.id"}},retired,{1,2,4,6},3,hash);
    assert(next[0].value==7);
    Reject([&]{ContentResourceAllocator::Plan("Eitrigg",policy,requests,plan,{3},2,hash);});
    Reject([&]{ContentResourceAllocator::Plan("Eitrigg",policy,requests,plan,{},2,std::string(64,'b'));});
    auto replacement=ContentResourceAllocator::Plan("Eitrigg",policy,requests,plan,{},2,std::string(64,'b'),{hash});
    assert(replacement[0].value==3&&replacement[0].baselineSha256==hash);
    json m={{"schema",2},{"package","test-a"},{"name","Cost test"},{"version","1"},{"extendedCosts",json::array({
        {{"symbol","cost"},{"requirements",json::array({{{"item",{{"package","mod-hunts"},{"symbol","seal"}}},{"count",5}}})}}})}};
    auto path=std::filesystem::temp_directory_path()/"content-extended-cost-test.epf";
    Save(path,m);assert(ContentPackage(path).Validate().valid);
    for(auto key:{"ID","id","ItemID","ItemPurchaseGroup","unknown"})
    {auto invalid=m;invalid["extendedCosts"][0][key]=3;Save(path,invalid);assert(!ContentPackage(path).Validate().valid);}
    for(auto count:{json(0),json(-1),json(1.5),json(0xffffffffULL),json("5")})
    {auto invalid=m;invalid["extendedCosts"][0]["requirements"][0]["count"]=count;Save(path,invalid);assert(!ContentPackage(path).Validate().valid);}
    auto invalid=m;invalid["extendedCosts"][0]["requirements"][0]["item"]=56807;Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=m;invalid["extendedCosts"][0]["requirements"][0]["item"].erase("package");Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=m;invalid["extendedCosts"][0]["requirements"].push_back(invalid["extendedCosts"][0]["requirements"][0]);Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=m;invalid["extendedCosts"][0]["requirements"]=json::array();Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=m;invalid["extendedCosts"][0]["requirements"][0]["item"]["symbol"]="../seal";Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    invalid=m;invalid["schema"]=1;Save(path,invalid);assert(!ContentPackage(path).Validate().valid);
    // A declared local client+server Item is valid and keeps the reference logical.
    auto local=m;local["extendedCosts"][0]["requirements"][0]["item"]={{"symbol","seal"}};
    local["dbcRows"]=json::array({{{"op","add"},{"table","Item"},{"symbol","seal"},{"fields",{
        {"ClassID",15},{"SubclassID",0},{"SoundOverrideSubclassID",-1},{"Material",-1},{"DisplayInfoID",{{"copyFromItem",6948}}},{"InventoryType",0},{"SheatheType",0}}}}});
    local["serverRows"]=json::array({{{"op","upsert"},{"table","item_template"},{"symbol","seal"},{"fields",{
        {"name","Token"},{"description","test"},{"Quality",1},{"stackable",200},{"bonding",0},{"BagFamily",0}}}}});
    Save(path,local);auto valid=ContentPackage(path).Validate();assert(valid.valid&&valid.manifest.extendedCosts[0].requirements[0].packageKey=="test-a");
    auto changed=valid.manifest;changed.extendedCosts[0].requirements[0].count=6;
    auto stage=std::filesystem::temp_directory_path()/"cost-manifest-stage";std::filesystem::create_directories(stage);
    assert(!ContentPackage(path).StageInto(stage,changed).success);
    std::filesystem::remove_all(stage);std::filesystem::remove(path);
    auto bundle=ContentServerBundle::ServerJson("Eitrigg",{}, {a,b});
    std::vector<ResolvedServerItem> items;std::vector<ResolvedExtendedCost> costs;std::string error;
    assert(ContentServerBundle::ParseServer(bundle,"Eitrigg",items,error,&costs)&&costs.size()==2);
    auto artifact=json::parse(bundle);artifact["extendedCosts"][0]["requirements"][0]["count"]=0;
    assert(!ContentServerBundle::ParseServer(artifact.dump(2)+"\n","Eitrigg",items,error));
    if(argc>1)
    {
        auto real=DbcReader::Read(argv[1],*d);assert(real.valid);auto occupancy=ItemExtendedCostDbc::Inspect(real.document);
        auto fresh=a;fresh.id=65535;auto realBytes=ItemExtendedCostDbc::Compose(real.document,{fresh});
        auto check=DbcReader::Parse(realBytes,*d);assert(check.valid&&check.document.recordCount==real.document.recordCount+1);
        for(std::size_t i=0;i<real.document.recordCount;++i)
        {
            auto start=real.document.words.begin()+i*16;bool found=false;
            for(std::size_t j=0;j<check.document.recordCount;++j)
                if(check.document.words[j*16]==*start){assert(std::equal(start,start+16,check.document.words.begin()+j*16));found=true;break;}
            assert(found);
        }
        assert(check.document.strings==real.document.strings);
        std::cout<<"PASS real local baseline: "<<real.document.recordCount<<" rows preserved exactly; this is not Eitrigg acceptance\n";
    }
    std::cout<<"PASS ItemExtendedCost descriptor, malformed data, preservation, counts, deterministic composition/readback, typed allocation/retention, parser, symbolic references, canonical artifact\n";
}
