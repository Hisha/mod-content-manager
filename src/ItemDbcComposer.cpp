#include "ItemDbcComposer.h"
#include "DbcDescriptor.h"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

std::vector<std::uint8_t> ItemDbcComposer::Compose(DbcDocument const& baseline,
    std::vector<PlannedItemRow> const& rows)
{
    auto descriptor = FindDbcDescriptor(12340, "Item");
    if (!descriptor || baseline.fieldCount != 8 || baseline.recordSize != 32
        || baseline.words.size() != std::size_t(baseline.recordCount) * 8)
        throw std::runtime_error("Invalid baseline Item.dbc descriptor or record dimensions");
    std::map<std::uint32_t, std::size_t> baselineIDs;
    for (std::size_t i = 0; i < baseline.recordCount; ++i)
        if (!baselineIDs.emplace(baseline.words[i * 8], i).second)
            throw std::runtime_error("Duplicate baseline Item ID: " + std::to_string(baseline.words[i * 8]));
    auto sorted = rows;
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) { return a.id < b.id; });
    std::set<std::uint32_t> added;
    DbcDocument composed = baseline;
    for (auto const& row : sorted)
    {
        if (!row.id || baselineIDs.count(row.id) || !added.insert(row.id).second)
            throw std::runtime_error("Allocated Item ID collides with baseline/composed row: " + std::to_string(row.id));
        auto stock = baselineIDs.find(row.declaration.displayCopyFromItem);
        if (stock == baselineIDs.end())
            throw std::runtime_error("Stock display source Item ID is absent from baseline: "
                + std::to_string(row.declaration.displayCopyFromItem));
        auto display = baseline.words[stock->second * 8 + 5];
        if (!display)
            throw std::runtime_error("Stock display source Item has no DisplayInfoID: "
                + std::to_string(row.declaration.displayCopyFromItem));
        composed.words.insert(composed.words.end(), {row.id, row.declaration.classID,
            row.declaration.subclassID,
            static_cast<std::uint32_t>(row.declaration.soundOverrideSubclassID),
            static_cast<std::uint32_t>(row.declaration.material), display,
            row.declaration.inventoryType, row.declaration.sheatheType});
        ++composed.recordCount;
    }
    auto bytes = DbcReader::Serialize(composed);
    auto parsed = DbcReader::Parse(bytes, *descriptor);
    if (!parsed.valid || parsed.document.recordCount != baseline.recordCount + rows.size()
        || !std::equal(baseline.words.begin(), baseline.words.end(), parsed.document.words.begin())
        || parsed.document.strings != baseline.strings)
        throw std::runtime_error("Composed Item.dbc failed reparse/preservation validation: " + parsed.error);
    return bytes;
}
