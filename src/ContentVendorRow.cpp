#include "ContentVendorServer.h"
#include <algorithm>
#include <set>
#include <tuple>
using nlohmann::json;
json ContentVendorServer::Objects(std::vector<ResolvedVendorRow> rows)
{
    std::sort(rows.begin(),rows.end(),[](auto const& a,auto const& b){return std::tie(a.packageKey,a.symbol)<std::tie(b.packageKey,b.symbol);});
    auto j=json::array();
    for(auto const& r:rows) j.push_back({{"package",r.packageKey},{"packageVersion",r.packageVersion},{"symbol",r.symbol},
        {"creatureEntry",r.creatureEntry},{"itemEntry",r.itemEntry},{"extendedCostSymbol",r.costSymbol},
        {"extendedCostId",r.costId},{"originalNpcFlags",r.originalFlags}});
    return j;
}
std::vector<ResolvedVendorRow> ContentVendorServer::Parse(json const& j)
{
    if(!j.is_array())throw std::runtime_error("vendorRows must be an array");
    std::vector<ResolvedVendorRow> rows;std::set<std::uint32_t> entries;
    std::set<std::pair<std::string,std::string>> symbols;
    for(auto const& d:j)
    {
        if(!d.is_object()||d.size()!=8)throw std::runtime_error("Invalid resolved vendor fields");
        for(auto key:{"creatureEntry","itemEntry","extendedCostId","originalNpcFlags"})
            if(!d.at(key).is_number_unsigned()||d.at(key).get<std::uint64_t>()>0xffffffffULL)
                throw std::runtime_error("Invalid resolved vendor number");
        ResolvedVendorRow r{d.at("package"),d.at("packageVersion"),d.at("symbol"),d.at("extendedCostSymbol"),
            d.at("creatureEntry"),d.at("itemEntry"),d.at("extendedCostId"),d.at("originalNpcFlags")};
        if(r.packageKey.empty()||r.symbol.empty()||r.costSymbol.empty()||!r.creatureEntry||r.creatureEntry>0xffffff
            ||!r.itemEntry||r.itemEntry>0xffffff||!r.costId||r.costId>65535
            ||!entries.insert(r.creatureEntry).second||!symbols.emplace(r.packageKey,r.symbol).second)
            throw std::runtime_error("Invalid/duplicate resolved vendor identity");
        rows.push_back(r);
    }
    if(Objects(rows)!=j)throw std::runtime_error("Noncanonical vendor relationships");
    return rows;
}
