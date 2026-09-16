#ifndef CONTENT_ITEM_DBC_COMPOSER_H
#define CONTENT_ITEM_DBC_COMPOSER_H
#include "ContentPackage.h"
#include "DbcReader.h"
#include <cstdint>
#include <vector>

struct PlannedItemRow
{
    std::uint32_t id;
    ContentItemRow declaration;
};

class ItemDbcComposer
{
public:
    static std::vector<std::uint8_t> Compose(DbcDocument const& baseline,
        std::vector<PlannedItemRow> const& rows);
};
#endif
