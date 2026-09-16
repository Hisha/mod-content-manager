#ifndef CONTENT_CURRENCY_DBC_COMPOSER_H
#define CONTENT_CURRENCY_DBC_COMPOSER_H
#include "DbcReader.h"
#include <set>
#include <string>

struct ResolvedCurrency
{
    std::string symbol;
    std::uint32_t itemId = 0;
    std::uint32_t categoryId = 0;
    std::uint32_t bitIndex = 0;
    std::string categorySymbol; // Empty for legacy categoryCopyFromItem.
    // CurrencyTypes.ID is derived from the allocated ItemID, never EPF-authored.
    // This also keeps the SQL overlay's ID order equal to its ItemID lookup order.
};
struct CurrencyOccupancy
{
    std::set<std::uint32_t> ids, items, bits;
};
class CurrencyDbcComposer
{
public:
    static CurrencyOccupancy Inspect(DbcDocument const& baseline);
    static std::uint32_t Category(DbcDocument const& baseline, std::uint32_t donorItem);
    static std::vector<std::uint8_t> Compose(DbcDocument const& baseline,
        std::vector<ResolvedCurrency> const& rows);
};
#endif
