#ifndef CONTENT_PACKAGE_H
#define CONTENT_PACKAGE_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ContentPackageEntry
{
    std::string type;
    std::string source;
    std::string target;
};

struct ContentItemRow
{
    std::string symbol;
    std::uint32_t classID = 0;
    std::uint32_t subclassID = 0;
    std::int32_t soundOverrideSubclassID = -1;
    std::int32_t material = -1;
    std::uint32_t displayCopyFromItem = 0;
    std::uint32_t inventoryType = 0;
    std::uint32_t sheatheType = 0;
    bool operator==(ContentItemRow const& other) const
    {
        return symbol == other.symbol && classID == other.classID && subclassID == other.subclassID
            && soundOverrideSubclassID == other.soundOverrideSubclassID && material == other.material
            && displayCopyFromItem == other.displayCopyFromItem && inventoryType == other.inventoryType
            && sheatheType == other.sheatheType;
    }
};

struct ContentServerItemRow
{
    std::string symbol;
    std::string name;
    std::string description;
    std::uint8_t quality = 0;
    std::int32_t stackable = 1;
    std::uint8_t bonding = 0;
    std::int32_t bagFamily = 0;
    bool operator==(ContentServerItemRow const& other) const
    {
        return symbol == other.symbol && name == other.name && description == other.description
            && quality == other.quality && stackable == other.stackable
            && bonding == other.bonding && bagFamily == other.bagFamily;
    }
};

struct ContentCurrencyRow
{
    std::string symbol;
    std::string itemSymbol;
    std::uint32_t categoryCopyFromItem = 0;
    bool operator==(ContentCurrencyRow const& other) const
    {
        return symbol == other.symbol && itemSymbol == other.itemSymbol
            && categoryCopyFromItem == other.categoryCopyFromItem;
    }
};

struct ContentPackageManifest
{
    uint32_t schema = 0;
    std::string packageKey;
    std::string name;
    std::string version;
    std::string description;

    std::vector<ContentPackageEntry> content;
    std::vector<ContentItemRow> itemRows;
    std::vector<ContentServerItemRow> serverItemRows;
    std::vector<ContentCurrencyRow> currencyRows;
};

struct ContentPackageStageResult
{
    bool success = false;
    std::string error;
    std::filesystem::path stagingDirectory;
    std::vector<std::filesystem::path> stagedFiles;
};

struct ContentPackageValidationResult
{
    bool valid = false;
    std::string error;
    ContentPackageManifest manifest;
};

class ContentPackage
{
public:
    explicit ContentPackage(std::filesystem::path path);

    ContentPackageValidationResult Validate() const;

    ContentPackageStageResult Stage(
        std::filesystem::path const& workDirectory) const;

    // Append only declared files to an existing owned workspace. Never clears
    // directories or overwrites files; revalidates against the preflight manifest.
    ContentPackageStageResult StageInto(
        std::filesystem::path const& stagingDirectory,
        ContentPackageManifest const& expected) const;

private:
    std::filesystem::path _path;
};

#endif
