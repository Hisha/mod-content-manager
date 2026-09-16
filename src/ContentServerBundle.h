#ifndef CONTENT_SERVER_BUNDLE_H
#define CONTENT_SERVER_BUNDLE_H

#include "ContentPackage.h"
#include "CurrencyDbcComposer.h"
#include "ContentAllocationRegistry.h"
#include <cstdint>
#include <string>
#include <vector>

struct ResolvedServerItem
{
    std::string packageKey;
    std::string packageVersion;
    std::string symbol;
    std::uint32_t id = 0;
    std::uint32_t displayId = 0;
    ContentItemRow client;
    ContentServerItemRow server;
    ResolvedCurrency currency;
};

class ContentServerBundle
{
public:
    static std::string ServerJson(std::string const& realm, std::vector<ResolvedServerItem> rows);
    static std::string ParityJson(std::string const& realm, std::uint32_t build,
        std::vector<ItemAllocation> const& allocations,
        std::vector<ResolvedServerItem> const& rows, std::string const& baselineSha256,
        std::string const& itemDbcSha256, std::string const& clientMpqSha256,
        std::string const& serverSha256, std::string const& currencyDbcSha256 = "");
    static bool ParseServer(std::string const& text, std::string const& realm,
        std::vector<ResolvedServerItem>& rows, std::string& error);
    static bool VerifyParity(std::string const& text, std::string const& realm,
        std::uint32_t build, std::string const& baselineSha256, std::string const& clientMpqSha256,
        std::string const& serverSha256, std::vector<ResolvedServerItem> const& rows,
        std::vector<ItemAllocation> const& allocations, std::string& error);
    static std::string RowJson(ResolvedServerItem const& row);
    static std::string SqlText(std::string const& value);
    static std::string SqlIdentityText(std::string const& value);
    static std::string InsertSql(ResolvedServerItem const& row);
    static std::string UpdateSql(ResolvedServerItem const& row, std::string const& previousRowJson);
    static std::string MatchSql(ResolvedServerItem const& row, std::string const& alias);
};

#endif
