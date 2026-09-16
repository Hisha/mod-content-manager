#ifndef CONTENT_CURRENCY_CATEGORY_DBC_COMPOSER_H
#define CONTENT_CURRENCY_CATEGORY_DBC_COMPOSER_H
#include "DbcReader.h"
#include <array>
#include <map>
#include <set>

struct ResolvedCurrencyCategory
{
    std::string packageKey, symbol;
    std::uint32_t id = 0;
    std::map<std::string, std::string> names;
};
class CurrencyCategoryDbcComposer
{
public:
    inline static std::array<char const*, 9> const Locales = {
        "enUS", "koKR", "frFR", "deDE", "zhCN", "zhTW", "esES", "esMX", "ruRU"};
    static void ValidateNames(std::map<std::string, std::string> const& names);
    static std::string Name(DbcDocument const& document, std::size_t row, std::size_t locale);
    static std::set<std::uint32_t> Inspect(DbcDocument const& baseline);
    static std::set<std::uint32_t> Occupancy(DbcDocument const& categories, DbcDocument const& currencies);
    static std::vector<std::uint8_t> Compose(DbcDocument const& baseline,
        std::vector<ResolvedCurrencyCategory> rows);
};
#endif
