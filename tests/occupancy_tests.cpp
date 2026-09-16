#include "ContentItemOccupancy.h"
#include <cassert>
#include <cstdint>
#include <set>
#include <type_traits>

// Mirrors Field::Get's signedness warning without requiring a live database.
struct RecordingField
{
    std::int32_t value;
    bool isNull = false;
    int* conversionWarnings;

    bool IsNull() const { return isNull; }

    template <typename T>
    T Get() const
    {
        if constexpr (std::is_same_v<T, std::uint32_t>)
        {
            if (value < 0)
                ++*conversionWarnings;
        }
        return static_cast<T>(value);
    }
};

int main()
{
    std::set<std::uint32_t> occupied;
    int warnings = 0;
    RecordingField reference{-34040, false, &warnings};
    AddContentItemOccupancy(occupied, reference, ContentItemColumnType::SignedVendorItemOrReference);
    assert(warnings == 0 && occupied.empty());
    assert(!occupied.count(static_cast<std::uint32_t>(-34040)));

    RecordingField anotherReference{-1, false, &warnings};
    RecordingField zero{0, false, &warnings};
    RecordingField item{56807, false, &warnings};
    RecordingField referencedVendorItem{34041, false, &warnings};
    for (auto* field : {&anotherReference, &zero, &item, &referencedVendorItem})
        AddContentItemOccupancy(occupied, *field, ContentItemColumnType::SignedVendorItemOrReference);
    assert(warnings == 0 && occupied == std::set<std::uint32_t>({34041, 56807}));

    RecordingField unsignedItem{57576, false, &warnings};
    RecordingField nullItem{0, true, &warnings};
    AddContentItemOccupancy(occupied, unsignedItem, ContentItemColumnType::UnsignedItemId);
    AddContentItemOccupancy(occupied, nullItem, ContentItemColumnType::UnsignedItemId);
    assert(warnings == 0 && occupied == std::set<std::uint32_t>({34041, 56807, 57576}));
}
