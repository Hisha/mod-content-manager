#include "DbcDescriptor.h"

namespace
{
DbcDescriptor const item12340 = {
    12340, "Item", 1, "DBFilesClient/Item.dbc", "Item.dbc",
    {
        {"ID", DbcFieldType::UInt32, true, nullptr, "item.id"},
        {"ClassID", DbcFieldType::UInt32},
        {"SubclassID", DbcFieldType::UInt32},
        {"SoundOverrideSubclassID", DbcFieldType::Int32},
        {"Material", DbcFieldType::Int32},
        {"DisplayInfoID", DbcFieldType::UInt32, false, "ItemDisplayInfo"},
        {"InventoryType", DbcFieldType::UInt32},
        {"SheatheType", DbcFieldType::UInt32}
    }
};
DbcDescriptor const currency12340 = {
    12340, "CurrencyTypes", 1, "DBFilesClient/CurrencyTypes.dbc", "CurrencyTypes.dbc",
    {
        {"ID", DbcFieldType::Int32, true},
        {"ItemID", DbcFieldType::Int32, false, "Item"},
        {"CategoryID", DbcFieldType::Int32, false, "CurrencyCategory"},
        {"BitIndex", DbcFieldType::Int32, false, nullptr, "currency.known-bit"}
    }
};
}

DbcDescriptor const* FindDbcDescriptor(std::uint32_t clientBuild, std::string const& tableName)
{
    if (clientBuild != 12340) return nullptr;
    if (tableName == "Item") return &item12340;
    if (tableName == "CurrencyTypes") return &currency12340;
    return nullptr;
}

bool IsKnownDbcTable(std::string const& tableName)
{
    return tableName == "Item" || tableName == "CurrencyTypes";
}
