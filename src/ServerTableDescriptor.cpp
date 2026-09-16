#include "ServerTableDescriptor.h"

ServerTableDescriptor const* FindServerTableDescriptor(std::string const& table)
{
    static ServerTableDescriptor const item = {"item_template", 1,
        {{"name", ServerFieldType::String, true, 1, 255},
         {"description", ServerFieldType::String, true, 0, 255},
         {"Quality", ServerFieldType::UInt8, true, 0, 7},
         {"stackable", ServerFieldType::Int32, true, 1, 1000},
         {"bonding", ServerFieldType::UInt8, true, 0, 5},
         {"BagFamily", ServerFieldType::Int32, true, 0, 8192}},
        {{"entry", "int unsigned", false}, {"class", "tinyint unsigned", false},
         {"subclass", "tinyint unsigned", false}, {"SoundOverrideSubclass", "tinyint", false},
         {"name", "varchar(255)", false, "utf8mb4_unicode_ci"}, {"displayid", "int unsigned", false},
         {"Quality", "tinyint unsigned", false}, {"InventoryType", "tinyint unsigned", false},
         {"stackable", "int", true}, {"bonding", "tinyint unsigned", false},
         {"description", "varchar(255)", false, "utf8mb4_unicode_ci"}, {"Material", "tinyint", false},
         {"sheath", "tinyint unsigned", false}, {"BagFamily", "int", false},
         {"Flags", "int unsigned", false}}};
    return table == item.table ? &item : nullptr;
}
