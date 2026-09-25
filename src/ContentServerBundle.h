#ifndef CONTENT_SERVER_BUNDLE_H
#define CONTENT_SERVER_BUNDLE_H

#include "ContentPackage.h"
#include "ContentVendorServer.h"
#include "ItemExtendedCostDbc.h"
#include "CurrencyDbcComposer.h"
#include "CurrencyCategoryDbcComposer.h"
#include "ContentAllocationRegistry.h"
#include "ContentManagedServer.h"
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
    static std::string ServerJson(std::string const& realm, std::vector<ResolvedServerItem> rows, std::vector<ResolvedExtendedCost> costs = {}, std::vector<ResolvedVendorRow> vendors = {},
        std::vector<ResolvedCreatureTemplate> creatures = {}, std::vector<ResolvedGameObjectTemplate> gameObjects = {}, std::vector<ResolvedCreatureSpawn> spawns = {});
    static std::string ParityJson(std::string const& realm, std::uint32_t build,
        std::vector<ItemAllocation> const& allocations,
        std::vector<ResolvedServerItem> const& rows, std::string const& baselineSha256,
        std::string const& itemDbcSha256, std::string const& clientMpqSha256,
        std::string const& serverSha256, std::string const& currencyDbcSha256 = "",
        std::string const& categoryDbcSha256 = "", std::vector<ResolvedCurrencyCategory> categories = {},
        std::vector<ContentBaseline> baselines = {}, std::string const& extendedCostDbcSha256 = "",
        std::vector<ResolvedExtendedCost> costs = {}, std::vector<ResolvedVendorRow> vendors = {},
        std::vector<ResolvedCreatureTemplate> creatures = {}, std::vector<ResolvedGameObjectTemplate> gameObjects = {}, std::vector<ResolvedCreatureSpawn> spawns = {});
    static bool ParseServer(std::string const& text, std::string const& realm,
        std::vector<ResolvedServerItem>& rows, std::string& error, std::vector<ResolvedExtendedCost>* costs = nullptr, std::vector<ResolvedVendorRow>* vendors = nullptr,
        std::vector<ResolvedCreatureTemplate>* creatures = nullptr, std::vector<ResolvedGameObjectTemplate>* gameObjects = nullptr, std::vector<ResolvedCreatureSpawn>* spawns = nullptr);
    static bool VerifyParity(std::string const& text, std::string const& realm,
        std::uint32_t build, std::string const& baselineSha256, std::string const& clientMpqSha256,
        std::string const& serverSha256, std::vector<ResolvedServerItem> const& rows,
        std::vector<ItemAllocation> const& allocations, std::string& error,
        std::vector<ResolvedExtendedCost> const& costs = {}, std::vector<ResolvedVendorRow> const& vendors = {},
        std::vector<ResolvedCreatureTemplate> const& creatures = {}, std::vector<ResolvedGameObjectTemplate> const& gameObjects = {}, std::vector<ResolvedCreatureSpawn> const& spawns = {});
    static std::string RowJson(ResolvedServerItem const& row);
    static std::string SqlText(std::string const& value);
    static std::string SqlIdentityText(std::string const& value);
    static std::string InsertSql(ResolvedServerItem const& row);
    static std::string UpdateSql(ResolvedServerItem const& row, std::string const& previousRowJson);
    static std::string MatchSql(ResolvedServerItem const& row, std::string const& alias);
};

#endif
