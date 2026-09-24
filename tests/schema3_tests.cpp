#include "ContentPackage.h"
#include "ContentClientRequirement.h"
#include "third_party/json/json.hpp"
#include "third_party/miniz/miniz.h"
#include <cassert>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace
{
void Save(std::filesystem::path const& path, json const& manifest, bool raw = false)
{
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, path.string().c_str(), 0)) throw std::runtime_error("zip init failed");
    auto body = manifest.dump();
    if (!mz_zip_writer_add_mem(&zip, "manifest.json", body.data(), body.size(), MZ_BEST_COMPRESSION)
        || (raw && !mz_zip_writer_add_mem(&zip, "assets/test.txt", "ok", 2, MZ_BEST_COMPRESSION))
        || !mz_zip_writer_finalize_archive(&zip))
        throw std::runtime_error("zip write failed");
    mz_zip_writer_end(&zip);
}
}

int main()
{
    using ContentClientRequirement::ProtectedFrameXml;
    auto path = std::filesystem::temp_directory_path() / "content-schema3-test.epf";
    auto work = std::filesystem::temp_directory_path() / "content-schema3-work";

    // 1. Existing schema-1 still parses/validates and declares no requirements.
    json schema1 = {{"schema", 1}, {"package", "raw-a"}, {"name", "Raw A"},
        {"version", "1"}, {"content", json::array({{{"type", "file"},
            {"source", "assets/test.txt"}, {"target", "Documentation/a.txt"}}})}};
    Save(path, schema1, true);
    {
        auto validation = ContentPackage(path).Validate();
        assert(validation.valid);
        assert(validation.manifest.schema == 1);
        assert(validation.manifest.clientRequirements.empty());
    }

    // 2. Existing schema-2 still parses/validates and declares no requirements.
    json schema2 = {{"schema", 2}, {"package", "raw-b"}, {"name", "Raw B"},
        {"version", "1"}, {"content", json::array({{{"type", "file"},
            {"source", "assets/test.txt"}, {"target", "Documentation/b.txt"}}})}};
    Save(path, schema2, true);
    {
        auto validation = ContentPackage(path).Validate();
        assert(validation.valid);
        assert(validation.manifest.schema == 2);
        assert(validation.manifest.clientRequirements.empty());
    }

    // 3. Schema 3 without clientRequirements is valid and produces none.
    json schema3 = {{"schema", 3}, {"package", "raw-c"}, {"name", "Raw C"},
        {"version", "1"}, {"content", json::array({{{"type", "file"},
            {"source", "assets/test.txt"}, {"target", "Documentation/c.txt"}}})}};
    Save(path, schema3, true);
    {
        auto validation = ContentPackage(path).Validate();
        assert(validation.valid);
        assert(validation.manifest.schema == 3);
        assert(validation.manifest.clientRequirements.empty());
    }

    // 4. Empty clientRequirements array is valid and produces none.
    auto empty = schema3;
    empty["clientRequirements"] = json::array();
    Save(path, empty, true);
    {
        auto validation = ContentPackage(path).Validate();
        assert(validation.valid);
        assert(validation.manifest.clientRequirements.empty());
    }

    // 5. A supported requirement is valid and lands in the manifest set.
    auto required = schema3;
    required["clientRequirements"] = json::array({ProtectedFrameXml});
    Save(path, required, true);
    {
        auto validation = ContentPackage(path).Validate();
        assert(validation.valid);
        auto const& set = validation.manifest.clientRequirements;
        assert(set.size() == 1 && set[0] == ProtectedFrameXml);
    }

    // 6. Unknown requirement names fail with a useful error.
    auto unsupported = schema3;
    unsupported["clientRequirements"] = json::array({"some-future-thing"});
    Save(path, unsupported, true);
    {
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("Unsupported client requirement 'some-future-thing'") != std::string::npos);
        assert(validation.error.find(ProtectedFrameXml) != std::string::npos);
    }

    // 7. Duplicate declarations do not produce duplicate build requirements.
    auto duplicate = schema3;
    duplicate["clientRequirements"] = json::array({ProtectedFrameXml, ProtectedFrameXml});
    Save(path, duplicate, true);
    {
        auto validation = ContentPackage(path).Validate();
        assert(validation.valid);
        auto const& set = validation.manifest.clientRequirements;
        assert(set.size() == 1 && set[0] == ProtectedFrameXml);
    }

    // clientRequirements belongs to schema 3: older schemas must not silently carry it.
    for (int schema : {1, 2})
    {
        auto older = schema == 1 ? schema1 : schema2;
        older["clientRequirements"] = json::array({ProtectedFrameXml});
        Save(path, older, true);
        auto validation = ContentPackage(path).Validate();
        assert(!validation.valid);
        assert(validation.error.find("clientRequirements require Schema 3") != std::string::npos);
    }

    // Malformed clientRequirements forms are rejected.
    for (auto const& malformed : std::vector<json>{"protected-framexml", json::array({1}),
        json::array({true}), json::array({ProtectedFrameXml, 2})})
    {
        auto bad = schema3;
        bad["clientRequirements"] = malformed;
        Save(path, bad, true);
        assert(!ContentPackage(path).Validate().valid);
    }
    {
        auto merged = ContentClientRequirement::Merge({{}, {ProtectedFrameXml}, {ProtectedFrameXml}});
        assert(merged.size() == 1 && merged[0] == ProtectedFrameXml);
        // Aggregate-init duplication in every participating set still yields one entry.
        std::vector<std::vector<std::string>> sets;
        for (int i = 0; i < 3; ++i) sets.push_back({ProtectedFrameXml, ProtectedFrameXml});
        auto stable = ContentClientRequirement::Merge(sets);
        assert(stable.size() == 1 && stable[0] == ProtectedFrameXml);
        // Requirement comparison is byte-wise; the merged set is sorted.
        assert(ContentClientRequirement::ValidSet(stable));
        // Ordering is deterministic byte-wise, independent of input order.
        auto ordered = ContentClientRequirement::Merge({{"banana", "zesty"}, {}, {"apple", "banana"}});
        assert(ordered == (std::vector<std::string>{"apple", "banana", "zesty"}));
    }

    // Schema-3 packages stage and build exactly like earlier schemas: metadata
    // must not alter the produced files.
    Save(path, required, true);
    {
        auto staged = ContentPackage(path).Stage(work);
        assert(staged.success);
        assert(staged.stagedFiles.size() == 1);
        assert(std::filesystem::is_regular_file(staged.stagedFiles.front()));
    }

    std::filesystem::remove_all(work);
    std::filesystem::remove(path);
    return 0;
}