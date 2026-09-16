#include "CurrencyDbcComposer.h"
#include "ContentResourceAllocator.h"
#include "ContentServerBundle.h"
#include "ContentBuildHash.h"
#include "third_party/json/json.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>

bool ContentBuildHash::Valid(std::string const& hash)
{
    return hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == std::string::npos;
}
template<class F> void Reject(F f) { bool rejected = false; try { f(); } catch (std::exception const&) { rejected = true; } assert(rejected); }
int main(int argc, char** argv)
{
    DbcDocument baseline{2,4,16,1,{1,40752,22,1,2,40753,22,2},{0}};
    auto occupancy = CurrencyDbcComposer::Inspect(baseline);
    assert(occupancy.bits == std::set<std::uint32_t>({1,2}));
    assert(CurrencyDbcComposer::Category(baseline,40752) == 22);
    Reject([&] { CurrencyDbcComposer::Category(baseline,99); });
    std::string hash(64,'a'), otherHash(64,'b');
    auto policy = ContentResourceAllocator::CurrencyKnownBitPolicy();
    auto plan = ContentResourceAllocator::Plan("test",policy,{{"a","currency","currency.known-bit"}}, {}, occupancy.bits,1,hash);
    assert(plan[0].value == 3);
    ItemAllocation item{"test","a","seal",56807,"reserved",1,1,otherHash};
    auto mixed = plan; mixed.push_back(item);
    auto again = ContentResourceAllocator::Plan("test",policy,{{"a","currency","currency.known-bit"}}, mixed,occupancy.bits,2,hash);
    assert(again[0].value == 3 && again[0].firstBuild == 1 && again[0].lastBuild == 2);
    auto taken=occupancy.bits;taken.insert(3);
    Reject([&] { ContentResourceAllocator::Plan("test",policy,{{"a","currency","currency.known-bit"}},plan,taken,2,hash); });
    for (unsigned i=1;i<=64;++i)taken.insert(i);
    Reject([&] { ContentResourceAllocator::Plan("test",policy,{{"b","c","currency.known-bit"}},plan,taken,2,hash); });
    taken.erase(64);
    auto last = ContentResourceAllocator::Plan("test",policy,{{"b","c","currency.known-bit"}},plan,taken,2,hash);
    assert(last[0].value == 64);
    Reject([&] { ContentResourceAllocator::Plan("test",policy,{{"a","currency","currency.known-bit"}},plan,{},2,otherHash); });
    auto corrupt=plan; corrupt.push_back(plan[0]); corrupt.back().symbol="other";
    Reject([&] { ContentResourceAllocator::Plan("test",policy,{},corrupt,{},2,hash); });
    std::vector<ResolvedCurrency> additions={{"currency",56807,22,3},{"other",56808,22,64}};
    auto bytes = CurrencyDbcComposer::Compose(baseline,additions);
    std::reverse(additions.begin(),additions.end());
    assert(bytes == CurrencyDbcComposer::Compose(baseline,additions));
    auto parsed=DbcReader::Parse(bytes,*FindDbcDescriptor(12340,"CurrencyTypes"));
    assert(parsed.valid && parsed.document.recordCount==4 && parsed.document.words[11]==3);
    assert(baseline.recordCount==2);
    for (unsigned bit : {0U,1U,65U})
        Reject([&] { CurrencyDbcComposer::Compose(baseline,{{"bad",56807,22,bit}}); });
    Reject([&] { CurrencyDbcComposer::Compose(baseline,{{"bad",1,22,3}}); });
    Reject([&] { CurrencyDbcComposer::Compose(baseline,{{"bad",40752,22,3}}); });
    auto bad=baseline;bad.words[7]=1;Reject([&] { CurrencyDbcComposer::Inspect(bad); });
    bytes.pop_back();assert(!DbcReader::Parse(bytes,*FindDbcDescriptor(12340,"CurrencyTypes")).valid);
    ResolvedServerItem server;
    server.packageKey="a";server.packageVersion="4";server.symbol="seal";server.id=item.value;server.displayId=6418;
    server.client.classID=15;server.server.symbol="seal";server.server.name="Seal";server.server.stackable=200;
    server.server.bagFamily=8192;server.currency={"currency",item.value,22,3};
    auto bundle=ContentServerBundle::ServerJson("test",{server});
    std::vector<ResolvedServerItem> rows;std::string error;
    assert(ContentServerBundle::ParseServer(bundle,"test",rows,error));
    assert(rows[0].currency.bitIndex==3);
    auto parity=ContentServerBundle::ParityJson("test",2,mixed,rows,otherHash,hash,hash,hash,hash);
    assert(ContentServerBundle::VerifyParity(parity,"test",2,otherHash,hash,hash,rows,mixed,error));
    auto mismatched=rows;mismatched[0].currency.bitIndex=4;
    assert(!ContentServerBundle::VerifyParity(parity,"test",2,otherHash,hash,hash,mismatched,mixed,error));
    auto forged=nlohmann::json::parse(bundle);forged["rows"][0]["currency"]["BitIndex"]=65;
    assert(!ContentServerBundle::ParseServer(forged.dump(2)+"\n","test",rows,error));
    if(argc>1)
    {
        auto real=DbcReader::Read(argv[1],*FindDbcDescriptor(12340,"CurrencyTypes"));assert(real.valid);
        auto used=CurrencyDbcComposer::Inspect(real.document);
        auto allocation=ContentResourceAllocator::Plan("test",policy,{{"a","currency","currency.known-bit"}}, {},used.bits,1,hash);
        auto generated=CurrencyDbcComposer::Compose(real.document,{{"currency",56807,
            CurrencyDbcComposer::Category(real.document,40752),allocation[0].value}});
        auto output=DbcReader::Parse(generated,*FindDbcDescriptor(12340,"CurrencyTypes"));
        assert(output.valid && output.document.recordCount==real.document.recordCount+1);
        std::cout << "Local client baseline records " << real.document.recordCount << "; auto bit " << allocation[0].value << '\n';
    }
    std::cout << "Currency allocator/composer/parity tests passed\n";
}
