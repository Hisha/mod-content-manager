#pragma once

#include <cstdint>
#include <set>

enum class ContentItemColumnType
{
    UnsignedItemId,
    SignedVendorItemOrReference
};

// npc_vendor.item is a signed INT. Negative values reference another vendor's
// inventory (for example -34040 references vendor 34040), not an Item ID.
// All positive vendor rows, including referenced inventories, are scanned.
template <typename FieldType>
void AddContentItemOccupancy(std::set<std::uint32_t>& entries, FieldType& field,
    ContentItemColumnType type)
{
    if (field.IsNull())
        return;
    if (type == ContentItemColumnType::SignedVendorItemOrReference)
    {
        std::int32_t const value = field.template Get<std::int32_t>();
        if (value > 0)
            entries.insert(static_cast<std::uint32_t>(value));
    }
    else
    {
        std::uint32_t const value = field.template Get<std::uint32_t>();
        if (value > 0)
            entries.insert(value);
    }
}
