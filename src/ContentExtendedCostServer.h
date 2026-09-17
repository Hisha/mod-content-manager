#ifndef CONTENT_EXTENDED_COST_SERVER_H
#define CONTENT_EXTENDED_COST_SERVER_H
#include "ItemExtendedCostDbc.h"
#include "ContentAllocationRegistry.h"
#include <map>
struct ExtendedCostReferences
{
    std::set<std::uint32_t> overlay, vendors, events, refunds, items;
};
class ContentExtendedCostServer
{
public:
    static bool References(ExtendedCostReferences& refs,std::string& error);
    static bool Occupancy(std::string const& realm,std::vector<ItemAllocation> const& leases,
        std::set<std::uint32_t>& ids,std::set<std::uint32_t>& items,std::string& error);
    static std::string RowJson(ResolvedExtendedCost const& row);
    static bool Check(ResolvedExtendedCost const& row,std::string const& realm,bool& exists,std::string& error);
    static std::string Condition(ResolvedExtendedCost const& row,std::string const& realm,bool exists);
    static std::vector<std::string> ApplySql(ResolvedExtendedCost const& row,std::string const& realm,bool exists,
        std::uint32_t build,std::string const& hash);
    static bool Verify(ResolvedExtendedCost const& row,std::string const& realm,std::uint32_t build,
        std::string const& hash,std::string& error);
};
#endif
