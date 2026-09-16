#include "CurrencyCategoryDbcComposer.h"
#include "CurrencyDbcComposer.h"
#include "third_party/json/json.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

void CurrencyCategoryDbcComposer::ValidateNames(std::map<std::string, std::string> const& names)
{
    if (!names.count("enUS")) throw std::runtime_error("Currency category requires authored enUS name");
    for (auto const& [locale, name] : names)
    {
        if (std::find(Locales.begin(), Locales.end(), locale) == Locales.end()
            || name.empty() || name.size() > 255
            || std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            throw std::runtime_error("Invalid category locale or name (1..255 UTF-8 bytes, no control characters)");
        (void)nlohmann::json(name).dump(); // strict UTF-8 validation, no translation or normalization.
    }
}

std::string CurrencyCategoryDbcComposer::Name(DbcDocument const& document, std::size_t row, std::size_t locale)
{
    if (locale >= 16 || row >= document.recordCount || document.words.size() != document.recordCount * 19ULL)
        throw std::runtime_error("CurrencyCategory row/locale out of bounds");
    auto offset = document.words[row * 19 + 2 + locale];
    if (offset >= document.strings.size()) throw std::runtime_error("CurrencyCategory string offset out of bounds");
    auto start = document.strings.begin() + offset;
    auto end = std::find(start, document.strings.end(), 0);
    if (end == document.strings.end()) throw std::runtime_error("Unterminated CurrencyCategory name");
    std::string value(start, end);
    (void)nlohmann::json(value).dump();
    return value;
}

std::set<std::uint32_t> CurrencyCategoryDbcComposer::Inspect(DbcDocument const& baseline)
{
    if (baseline.fieldCount != 19 || baseline.recordSize != 76
        || baseline.words.size() != baseline.recordCount * 19ULL
        || baseline.strings.size() != baseline.stringBlockSize
        || baseline.strings.empty() || baseline.strings.front() != 0)
        throw std::runtime_error("Invalid CurrencyCategory dimensions/string block");
    std::set<std::uint32_t> ids;
    for (std::size_t i = 0; i < baseline.recordCount; ++i)
    {
        auto id = baseline.words[i * 19];
        if (!id || id > 0x7fffffffU || !ids.insert(id).second)
            throw std::runtime_error("Duplicate/invalid CurrencyCategory ID");
        for (std::size_t locale = 0; locale < 16; ++locale) (void)Name(baseline, i, locale);
    }
    return ids;
}

std::set<std::uint32_t> CurrencyCategoryDbcComposer::Occupancy(DbcDocument const& categories, DbcDocument const& currencies)
{
    auto ids = Inspect(categories);
    CurrencyDbcComposer::Inspect(currencies);
    for (std::size_t i = 0; i < currencies.recordCount; ++i)
        if (auto id = currencies.words[i * 4 + 2]) ids.insert(id); // Includes dangling references.
    return ids;
}

std::vector<std::uint8_t> CurrencyCategoryDbcComposer::Compose(DbcDocument const& baseline,
    std::vector<ResolvedCurrencyCategory> additions)
{
    auto occupied = Inspect(baseline);
    // NameFlags is opaque locale metadata. Copy it from the lowest-ID ordinary
    // stock category with an enUS name, rather than inventing flag semantics.
    std::uint32_t templateId = std::numeric_limits<std::uint32_t>::max(), nameFlags = 0;
    std::vector<std::array<std::uint32_t, 19>> rows;
    for (std::size_t i = 0; i < baseline.recordCount; ++i)
    {
        std::array<std::uint32_t, 19> row{};
        std::copy_n(baseline.words.begin() + i * 19, 19, row.begin());
        rows.push_back(row);
        if (!row[1] && row[0] < templateId && !Name(baseline, i, 0).empty())
        { templateId = row[0]; nameFlags = row[18]; }
    }
    if (!additions.empty() && templateId == std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("CurrencyCategory baseline has no ordinary enUS metadata template");
    auto result = baseline;
    std::map<std::string, std::uint32_t> appended;
    auto intern = [&](std::string const& text) {
        auto found = appended.find(text);
        if (found != appended.end()) return found->second;
        if (result.strings.size() + text.size() + 1 > 256ULL * 1024 * 1024)
            throw std::runtime_error("CurrencyCategory string block too large");
        auto offset = static_cast<std::uint32_t>(result.strings.size());
        result.strings.insert(result.strings.end(), text.begin(), text.end());
        result.strings.push_back(0);
        appended.emplace(text, offset);
        return offset;
    };
    std::sort(additions.begin(), additions.end(), [](auto const& a, auto const& b) { return a.id < b.id; });
    std::set<std::pair<std::string, std::string>> identities;
    for (auto const& added : additions)
    {
        ValidateNames(added.names);
        if (!added.id || added.id > 65535 || !occupied.insert(added.id).second
            || !identities.emplace(added.packageKey, added.symbol).second)
            throw std::runtime_error("CurrencyCategory allocation collision/outside policy");
        std::array<std::uint32_t, 19> row{};
        row[0] = added.id; row[1] = 0; row[18] = nameFlags;
        for (std::size_t i = 0; i < Locales.size(); ++i)
        {
            auto found = added.names.find(Locales[i]);
            row[2 + i] = intern(found == added.names.end() ? added.names.at("enUS") : found->second);
        }
        // Slots 9..15 are reserved in this client; leave their empty-string offsets zero.
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end());
    result.recordCount = static_cast<std::uint32_t>(rows.size());
    result.stringBlockSize = static_cast<std::uint32_t>(result.strings.size());
    result.words.clear();
    for (auto const& row : rows) result.words.insert(result.words.end(), row.begin(), row.end());
    auto bytes = DbcReader::Serialize(result);
    auto check = DbcReader::Parse(bytes, *FindDbcDescriptor(12340, "CurrencyCategory"));
    if (!check.valid || check.document.words != result.words || check.document.strings != result.strings)
        throw std::runtime_error("CurrencyCategory readback failed");
    Inspect(check.document);
    return bytes;
}
