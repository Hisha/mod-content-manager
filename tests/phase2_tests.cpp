#include "ContentPackage.h"
#include "DbcDescriptor.h"
#include "DbcReader.h"
#include "ItemDbcComposer.h"
#include "ContentResourceAllocator.h"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>

int main(int argc, char** argv)
{
    std::string hash(64, 'a');
    std::set<std::uint32_t> baseline = {1, 2, 6948};
    std::set<std::uint32_t> itemTemplate = {6949};
    std::set<std::uint32_t> npcVendor = {6950};
    std::set<std::uint32_t> characterItems = {6952};
    std::set<std::uint32_t> world = baseline;
    world.insert(itemTemplate.begin(), itemTemplate.end());
    world.insert(npcVendor.begin(), npcVendor.end());
    world.insert(characterItems.begin(), characterItems.end());
    auto policy = ContentResourceAllocator::ItemIdPolicy(baseline);
    assert(policy.resourceKind == "item.id" && policy.firstCandidate == 6949);
    // No administrator range or allocation configuration is passed to Plan.
    std::vector<ResourceAllocationRequest> requests = {{"mod-hunts", "seal"}};
    auto first = ContentResourceAllocator::Plan("Eitrigg", policy, requests, {}, world, 1, hash);
    assert(first.size() == 1 && first[0].value == 6951); // Skips both world sources.
    auto again = ContentResourceAllocator::Plan("Eitrigg", policy, requests, {}, world, 1, hash);
    assert(again[0].value == first[0].value); // Deterministic from identical inputs.
    ResourceAllocationPolicy overlapProbe{"item.id", 6948, 6952};
    auto skipped = ContentResourceAllocator::Plan("Eitrigg", overlapProbe, requests, {}, world, 1, hash);
    assert(skipped[0].value == 6951); // Baseline 6948 is occupied too.
    auto second = ContentResourceAllocator::Plan("Eitrigg", policy, requests, first, world, 2, hash);
    assert(second[0].value == 6951 && second[0].firstBuild == 1 && second[0].lastBuild == 2);
    auto third = ContentResourceAllocator::Plan("Eitrigg", policy,
        {{"other", "token"}, {"other", "another"}}, second, world, 3, hash);
    assert(third.size() == 2 && third[0].value != third[1].value);
    auto thirdReordered = ContentResourceAllocator::Plan("Eitrigg", policy,
        {{"other", "another"}, {"other", "token"}}, second, world, 3, hash);
    assert(thirdReordered[0].value == third[0].value && thirdReordered[1].value == third[1].value);
    assert(third[0].value != 6951 && third[1].value != 6951); // Retained removed lease occupied.
    auto sealAfterOther = ContentResourceAllocator::Plan("Eitrigg", policy,
        {{"other", "token"}, {"mod-hunts", "seal"}}, second, world, 4, hash);
    assert(sealAfterOther[0].value == 6951); // Package ordering cannot renumber Seal.
    auto reinstall = ContentResourceAllocator::Plan("Eitrigg", policy, requests, second, world, 5, hash);
    assert(reinstall[0].value == 6951); // Simulated restart/uninstall/reinstall from retained DB row.
    ResourceAllocationPolicy tiny{"item.id", 6949, 6951};
    bool exhausted = false;
    try { (void)ContentResourceAllocator::Plan("Eitrigg", tiny, {{"third", "x"}}, second,
        world, 6, hash); }
    catch (std::exception const&) { exhausted = true; }
    assert(exhausted);
    DbcDocument doc;
    doc.recordCount = 46096; doc.fieldCount = 8; doc.recordSize = 32; doc.stringBlockSize = 1;
    doc.words.resize(std::size_t(doc.recordCount) * 8);
    doc.strings = {0};
    for (std::uint32_t i = 0; i < doc.recordCount; ++i) doc.words[std::size_t(i) * 8] = i + 1;
    doc.words[std::size_t(6947) * 8 + 5] = 6418;
    ContentItemRow seal;
    seal.symbol = "seal"; seal.classID = 15; seal.soundOverrideSubclassID = -1;
    seal.material = -1; seal.displayCopyFromItem = 6948;
    auto bytes = ItemDbcComposer::Compose(doc, {{100000, seal}});
    auto parsed = DbcReader::Parse(bytes, *FindDbcDescriptor(12340, "Item"));
    assert(parsed.valid && parsed.document.recordCount == 46097 && parsed.document.words.back() == 0);
    assert(std::equal(doc.words.begin(), doc.words.end(), parsed.document.words.begin()));
    assert(parsed.document.words[std::size_t(46096) * 8] == 100000);
    assert(parsed.document.words[std::size_t(46096) * 8 + 5] == 6418);
    assert(bytes == ItemDbcComposer::Compose(doc, {{100000, seal}}));
    if (argc > 1)
    {
        auto real = DbcReader::Read(argv[1], *FindDbcDescriptor(12340, "Item"));
        assert(real.valid && real.document.recordCount == 46096);
        auto realBytes = ItemDbcComposer::Compose(real.document, {{100000, seal}});
        assert(DbcReader::Parse(realBytes, *FindDbcDescriptor(12340, "Item")).valid);
    }
    std::cout << "Phase 2 allocator/composer tests passed\n";
}
