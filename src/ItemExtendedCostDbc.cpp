#include "ItemExtendedCostDbc.h"
#include <algorithm>
#include <stdexcept>
#include <tuple>

ExtendedCostOccupancy ItemExtendedCostDbc::Inspect(DbcDocument const& doc)
{
    if (doc.fieldCount != 16 || doc.recordSize != 64 || doc.words.size() != doc.recordCount * 16ULL
        || doc.strings.size() != doc.stringBlockSize || doc.strings.empty() || doc.strings.front() != 0)
        throw std::runtime_error("Invalid ItemExtendedCost dimensions/string block");
    ExtendedCostOccupancy occupied;
    for (std::size_t row = 0; row < doc.recordCount; ++row)
    {
        auto id = doc.words[row*16];
        if (!id || id > 0x7fffffffU || !occupied.ids.insert(id).second)
            throw std::runtime_error("Duplicate/invalid ItemExtendedCost identity");
        for (std::size_t field = 1; field < 16; ++field)
            if (doc.words[row*16+field] > 0x7fffffffU)
                throw std::runtime_error("Negative/unrepresentable baseline ItemExtendedCost field");
        for (std::size_t field = 4; field < 9; ++field)
            if (auto item = doc.words[row*16+field]) occupied.items.insert(item);
        if (auto group = doc.words[row*16+15]) occupied.purchaseGroups.insert(group);
        // Stock has nonzero counts in empty item slots (including row 1). Preserve
        // those words; stricter authored-row rules must not rewrite/reject stock quirks.
    }
    return occupied;
}

std::array<std::uint32_t,16> ItemExtendedCostDbc::Words(ResolvedExtendedCost const& row)
{
    if (row.packageKey.empty() || row.symbol.empty() || !row.id || row.id > 65535
        || row.requirements.empty() || row.requirements.size() > 5
        || row.honorPoints > MaxPoints || row.arenaPoints > MaxPoints || row.arenaBracket > 2
        || row.requiredArenaRating > 0x7fffffffU || (row.arenaBracket && !row.requiredArenaRating))
        throw std::runtime_error("Invalid authored extended-cost identity/requirements");
    std::array<std::uint32_t,16> words{};
    words[0]=row.id; words[1]=row.honorPoints; words[2]=row.arenaPoints; words[3]=row.arenaBracket;
    words[14]=row.requiredArenaRating; // ItemPurchaseGroup remains zero; AC does not consume it.
    auto requirements = row.requirements;
    std::sort(requirements.begin(),requirements.end(),[](auto const& a,auto const& b) {
        return std::tie(a.packageKey,a.symbol) < std::tie(b.packageKey,b.symbol);
    });
    std::set<std::uint32_t> items;
    std::set<std::pair<std::string,std::string>> identities;
    for (std::size_t i=0; i<requirements.size(); ++i)
    {
        auto const& r=requirements[i];
        if (r.packageKey.empty() || r.symbol.empty() || !r.itemId || r.itemId>0x7fffffffU || !r.count || r.count>MaxCount
            || !items.insert(r.itemId).second || !identities.emplace(r.packageKey,r.symbol).second)
            throw std::runtime_error("Invalid/duplicate resolved extended-cost item requirement");
        words[4+i]=r.itemId; words[9+i]=r.count;
    }
    return words;
}

std::vector<std::uint8_t> ItemExtendedCostDbc::Compose(DbcDocument const& baseline,std::vector<ResolvedExtendedCost> additions)
{
    auto occupied=Inspect(baseline);
    std::vector<std::array<std::uint32_t,16>> rows;
    for (std::size_t i=0; i<baseline.recordCount; ++i)
    {
        std::array<std::uint32_t,16> row{};
        std::copy_n(baseline.words.begin()+i*16,16,row.begin()); rows.push_back(row);
    }
    std::set<std::pair<std::string,std::string>> identities;
    for (auto const& row:additions)
    {
        auto words=Words(row);
        if (!occupied.ids.insert(row.id).second || !identities.emplace(row.packageKey,row.symbol).second)
            throw std::runtime_error("Extended-cost identity collision");
        rows.push_back(words);
    }
    std::sort(rows.begin(),rows.end());
    auto result=baseline; result.words.clear(); result.recordCount=static_cast<std::uint32_t>(rows.size());
    for (auto const& row:rows) result.words.insert(result.words.end(),row.begin(),row.end());
    auto bytes=DbcReader::Serialize(result);
    auto check=DbcReader::Parse(bytes,*FindDbcDescriptor(12340,"ItemExtendedCost"));
    if (!check.valid || check.document.words != result.words || check.document.strings != baseline.strings)
        throw std::runtime_error("ItemExtendedCost composition readback failed");
    Inspect(check.document); return bytes;
}
