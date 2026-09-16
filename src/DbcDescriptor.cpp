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
DbcDescriptor const category12340 = {
    12340, "CurrencyCategory", 1, "DBFilesClient/CurrencyCategory.dbc", "CurrencyCategory.dbc",
    {
        {"ID", DbcFieldType::Int32, true, nullptr, "currency-category.id"},
        {"Flags", DbcFieldType::Int32},
        {"Name_enUS", DbcFieldType::StringOffset}, {"Name_koKR", DbcFieldType::StringOffset},
        {"Name_frFR", DbcFieldType::StringOffset}, {"Name_deDE", DbcFieldType::StringOffset},
        {"Name_zhCN", DbcFieldType::StringOffset}, {"Name_zhTW", DbcFieldType::StringOffset},
        {"Name_esES", DbcFieldType::StringOffset}, {"Name_esMX", DbcFieldType::StringOffset},
        {"Name_ruRU", DbcFieldType::StringOffset}, {"Name_reserved9", DbcFieldType::StringOffset},
        {"Name_reserved10", DbcFieldType::StringOffset}, {"Name_reserved11", DbcFieldType::StringOffset},
        {"Name_reserved12", DbcFieldType::StringOffset}, {"Name_reserved13", DbcFieldType::StringOffset},
        {"Name_reserved14", DbcFieldType::StringOffset}, {"Name_reserved15", DbcFieldType::StringOffset},
        {"NameFlags", DbcFieldType::UInt32}
    }
};
}

DbcDescriptor const* FindDbcDescriptor(std::uint32_t clientBuild, std::string const& tableName)
{
    if (clientBuild != 12340) return nullptr;
    if (tableName == "Item") return &item12340;
    if (tableName == "CurrencyTypes") return &currency12340;
    if (tableName == "CurrencyCategory") return &category12340;
    return nullptr;
}

bool IsKnownDbcTable(std::string const& tableName)
{
    return tableName == "Item" || tableName == "CurrencyTypes" || tableName == "CurrencyCategory";
}
