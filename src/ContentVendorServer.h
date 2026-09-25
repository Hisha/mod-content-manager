#ifndef CONTENT_VENDOR_SERVER_H
#define CONTENT_VENDOR_SERVER_H
#include "ContentPackage.h"
#include "third_party/json/json.hpp"
struct ResolvedVendorRow
{
    std::string packageKey, packageVersion, symbol, costSymbol;
    std::uint32_t creatureEntry=0, itemEntry=0, costId=0, originalFlags=0;
    bool flagsManaged=false;
};
class ContentVendorServer
{
public:
    static nlohmann::json Objects(std::vector<ResolvedVendorRow> rows);
    static std::vector<ResolvedVendorRow> Parse(nlohmann::json const& value);
    static bool Prepare(ResolvedVendorRow& row, std::string const& realm, std::string& error);
    static bool Check(ResolvedVendorRow const& row, std::string const& realm, bool& exists, std::string& error);
    static std::string Condition(ResolvedVendorRow const& row, std::string const& realm, bool exists);
    static std::vector<std::string> ApplySql(ResolvedVendorRow const& row, std::string const& realm, bool exists,
        std::uint32_t build, std::string const& hash);
    static bool Verify(ResolvedVendorRow const& row, std::string const& realm, std::uint32_t build,
        std::string const& hash, std::string& error);
};
#endif
