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
}

DbcDescriptor const* FindDbcDescriptor(std::uint32_t clientBuild, std::string const& tableName)
{
    return clientBuild == item12340.clientBuild && tableName == item12340.tableName ? &item12340 : nullptr;
}

bool IsKnownDbcTable(std::string const& tableName)
{
    return tableName == item12340.tableName;
}
