#include "CurrencyCategoryDbcComposer.h"
#include "ContentResourceAllocator.h"
#include <cassert>
#include <algorithm>
#include <functional>
#include <iostream>
static void Reject(std::function<void()> action) { bool failed=false;try{action();}catch(...){failed=true;}assert(failed); }
int main()
{
    DbcDocument b{2,19,76,7,{}, {0,'S','t','o','c','k',0}};
    b.words.resize(38);b.words[0]=1;b.words[2]=1;b.words[18]=123;b.words[19]=22;b.words[21]=1;b.words[37]=456;
    DbcDocument c{2,4,16,1,{1,10,2,1,2,11,2089878896,2},{0}};
    auto occupied=CurrencyCategoryDbcComposer::Occupancy(b,c);
    assert(occupied==std::set<std::uint32_t>({1,2,22,2089878896}));
    auto plan=ContentResourceAllocator::Plan("realm",ContentResourceAllocator::CurrencyCategoryIdPolicy(),
        {{"mod-hunts","hunts","currency-category.id"}}, {},occupied,1,std::string(64,'a'));
    assert(plan[0].value==3);
    auto retained=plan; retained[0].symbol="removed";
    auto next=ContentResourceAllocator::Plan("realm",ContentResourceAllocator::CurrencyCategoryIdPolicy(),
        {{"mod-hunts","hunts","currency-category.id"}}, retained,occupied,2,std::string(64,'a'));
    assert(next[0].value==4);
    std::vector<ResolvedCurrencyCategory> rows{{"mod-hunts","hunts",3,{{"enUS","Hunts"}}},
        {"another","pve",4,{{"enUS","Test"},{"frFR","Essai"}}}};
    auto first=CurrencyCategoryDbcComposer::Compose(b,rows);
    std::reverse(rows.begin(),rows.end());assert(first==CurrencyCategoryDbcComposer::Compose(b,rows));
    auto parsed=DbcReader::Parse(first,*FindDbcDescriptor(12340,"CurrencyCategory"));assert(parsed.valid);
    assert(std::equal(b.strings.begin(),b.strings.end(),parsed.document.strings.begin()));
    for(size_t i=0;i<2;++i)
    { auto id=b.words[i*19]; for(size_t j=0;j<parsed.document.recordCount;++j)
        if(parsed.document.words[j*19]==id) assert(std::equal(b.words.begin()+i*19,b.words.begin()+(i+1)*19,parsed.document.words.begin()+j*19)); }
    for(size_t slot=0;slot<9;++slot)assert(CurrencyCategoryDbcComposer::Name(parsed.document,1,slot)=="Hunts");
    for(size_t slot=9;slot<16;++slot)assert(CurrencyCategoryDbcComposer::Name(parsed.document,1,slot).empty());
    assert(parsed.document.words[19+18]==123);
    assert(CurrencyCategoryDbcComposer::Name(parsed.document,2,2)=="Essai");
    auto bad=b;bad.words[2]=999;Reject([&]{CurrencyCategoryDbcComposer::Inspect(bad);});
    bad=b;bad.words[19]=1;Reject([&]{CurrencyCategoryDbcComposer::Inspect(bad);});
    Reject([&]{CurrencyCategoryDbcComposer::Compose(b,{{"mod","x",22,{{"enUS","X"}}}});});
    Reject([&]{CurrencyCategoryDbcComposer::ValidateNames({{"enUS",std::string("x\0y",3)}});});
    Reject([&]{CurrencyCategoryDbcComposer::ValidateNames({{"enUS",std::string(1,char(0xff))}});});
    auto reused=ContentResourceAllocator::Plan("realm",ContentResourceAllocator::CurrencyCategoryIdPolicy(),
        {{"mod-hunts","hunts","currency-category.id"}},plan,occupied,2,std::string(64,'b'),{std::string(64,'a')});
    assert(reused[0].value==3 && reused[0].baselineSha256==std::string(64,'a'));
    occupied.insert(3);Reject([&]{ContentResourceAllocator::Plan("realm",ContentResourceAllocator::CurrencyCategoryIdPolicy(),
        {{"mod-hunts","hunts","currency-category.id"}},plan,occupied,2,std::string(64,'b'),{std::string(64,'a')});});
    std::cout<<"category composition, fallback, occupancy, retained leases: PASS\n";
}
