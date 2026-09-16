#include "CurrencyDbcComposer.h"
#include "DbcDescriptor.h"
#include <array>
#include <algorithm>
#include <stdexcept>

CurrencyOccupancy CurrencyDbcComposer::Inspect(DbcDocument const& baseline)
{
    if (baseline.fieldCount != 4 || baseline.recordSize != 16
        || baseline.words.size() != std::size_t(baseline.recordCount) * 4
        || baseline.strings.size() != baseline.stringBlockSize)
        throw std::runtime_error("Invalid CurrencyTypes dimensions");
    CurrencyOccupancy result;
    for (std::size_t i = 0; i < baseline.words.size(); i += 4)
    {
        auto id = baseline.words[i], item = baseline.words[i + 1], bit = baseline.words[i + 3];
        if (!id || id > 0x7fffffffU || !item || item > 0x7fffffffU
            || baseline.words[i + 2] > 0x7fffffffU || !bit || bit > 64 || !result.ids.insert(id).second
            || !result.items.insert(item).second || !result.bits.insert(bit).second)
            throw std::runtime_error("CurrencyTypes has duplicate/invalid ID, ItemID or BitIndex (expected 1..64)");
    }
    return result;
}
std::uint32_t CurrencyDbcComposer::Category(DbcDocument const& baseline, std::uint32_t donorItem)
{
    Inspect(baseline);
    for (std::size_t i = 0; i < baseline.words.size(); i += 4)
        if (baseline.words[i + 1] == donorItem)
        {
            auto category = baseline.words[i + 2];
            if (!category || category > 0x7fffffffU)
                throw std::runtime_error("Currency category donor is outside server descriptor range");
            return category;
        }
    throw std::runtime_error("Currency category donor is absent from baseline CurrencyTypes");
}
std::vector<std::uint8_t> CurrencyDbcComposer::Compose(DbcDocument const& baseline,
    std::vector<ResolvedCurrency> const& additions)
{
    auto occupied = Inspect(baseline);
    std::vector<std::array<std::uint32_t, 4>> rows;
    for (std::size_t i = 0; i < baseline.words.size(); i += 4)
        rows.push_back({baseline.words[i], baseline.words[i + 1], baseline.words[i + 2], baseline.words[i + 3]});
    for (auto const& row : additions)
    {
        if (!row.itemId || row.itemId > 0x7fffffffU || !row.categoryId || row.categoryId > 0x7fffffffU
            || !row.bitIndex || row.bitIndex > 64 || !occupied.ids.insert(row.itemId).second
            || !occupied.items.insert(row.itemId).second || !occupied.bits.insert(row.bitIndex).second)
            throw std::runtime_error("CurrencyTypes generated ID, item or bit collision/out of bounds");
        rows.push_back({row.itemId, row.itemId, row.categoryId, row.bitIndex});
    }
    std::sort(rows.begin(), rows.end());
    auto result = baseline;
    result.recordCount = static_cast<std::uint32_t>(rows.size());
    result.words.clear();
    for (auto const& row : rows) result.words.insert(result.words.end(), row.begin(), row.end());
    auto bytes = DbcReader::Serialize(result);
    auto check = DbcReader::Parse(bytes, *FindDbcDescriptor(12340, "CurrencyTypes"));
    if (!check.valid || check.document.words != result.words || check.document.strings != baseline.strings)
        throw std::runtime_error("CurrencyTypes readback mismatch");
    Inspect(check.document);
    return bytes;
}
