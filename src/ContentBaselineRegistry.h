#ifndef CONTENT_BASELINE_REGISTRY_H
#define CONTENT_BASELINE_REGISTRY_H
#include "DbcReader.h"
#include <set>

struct ContentBaseline
{
    DbcDocument document;
    std::string hash, source;
    std::uint32_t clientBuild = 0, descriptorVersion = 0;
    std::string table;
};

// One registry per world database; client build + compiled table identity is the key.
class ContentBaselineRegistry
{
public:
    static ContentBaseline Inspect(std::filesystem::path const& directory, DbcDescriptor const& descriptor);
    static bool Status(ContentBaseline const& candidate, std::string& status, std::string& error);
    static bool Accept(ContentBaseline const& candidate, std::string const& optionalPin, std::string& error);
    static bool Review(ContentBaseline const& candidate, std::string const& actor, std::uint64_t& review, std::string& error);
    static bool Approve(ContentBaseline const& currentFile, std::string const& optionalPin,
        std::uint64_t review, std::string const& actor, std::string& error);
    static bool History(ContentBaseline const& baseline, std::set<std::string>& hashes, std::string& error);
    static std::string Condition(ContentBaseline const& baseline);
    static std::string HistoryCondition(ContentBaseline const& baseline);
};
#endif
