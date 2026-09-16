#ifndef CONTENT_CURRENCY_SERVER_H
#define CONTENT_CURRENCY_SERVER_H
#include "ContentServerBundle.h"
#include <set>

class ContentCurrencyServer
{
public:
    static bool Occupancy(std::string const& realm, std::vector<ItemAllocation> const& leases,
        std::set<std::uint32_t>& bits, std::set<std::uint32_t>& ids, std::string& error);
    static bool CategoryOccupancy(std::string const& realm, std::vector<ItemAllocation> const& leases,
        std::set<std::uint32_t>& categories, std::string& error);
    static std::string CategoryCondition(std::string const& realm, std::string const& package, std::uint32_t category);
    static bool Prepare(ResolvedServerItem const& row, std::string const& realm, bool& exists,
        ResolvedServerItem& previous, std::string& error);
    static bool ValidateSchema(std::string& error);
    static bool Check(ResolvedServerItem const& row, std::string const& realm, bool& exists, std::string& error);
    static std::string Condition(ResolvedServerItem const& row, std::string const& realm, bool exists);
    static std::vector<std::string> ApplySql(ResolvedServerItem const& row, std::string const& realm,
        bool exists, std::uint32_t build, std::string const& hash, std::uint32_t previousCategory = 0);
    static bool Verify(ResolvedServerItem const& row, std::string const& realm,
        std::uint32_t build, std::string const& hash, std::string& error);
};
#endif
