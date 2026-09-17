#ifndef CONTENT_ITEM_EXTENDED_COST_DBC_H
#define CONTENT_ITEM_EXTENDED_COST_DBC_H
#include "ContentPackage.h"
#include "DbcReader.h"
#include <array>
#include <set>

struct ResolvedCostRequirement
{
    std::string packageKey, symbol;
    std::uint32_t itemId = 0, count = 0;
};
struct ResolvedExtendedCost
{
    std::string packageKey, packageVersion, symbol;
    std::uint32_t id = 0;
    std::vector<ResolvedCostRequirement> requirements;
    std::uint32_t honorPoints = 0, arenaPoints = 0, arenaBracket = 0, requiredArenaRating = 0;
};
struct ExtendedCostOccupancy
{
    std::set<std::uint32_t> ids, items, purchaseGroups;
};
class ItemExtendedCostDbc
{
public:
    // The core multiplies costs by a uint8 purchase count. Honor/Arena deduction
    // converts the product to int32; required-item counts remain uint32.
    static constexpr std::uint32_t MaxPoints = 0x7fffffffU / 255;
    static constexpr std::uint32_t MaxCount = 0xffffffffU / 255;
    static ExtendedCostOccupancy Inspect(DbcDocument const& document);
    static std::array<std::uint32_t,16> Words(ResolvedExtendedCost const& row);
    static std::vector<std::uint8_t> Compose(DbcDocument const& baseline, std::vector<ResolvedExtendedCost> rows);
};
#endif
