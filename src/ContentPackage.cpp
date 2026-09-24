#include "ContentPackage.h"
#include "CurrencyCategoryDbcComposer.h"
#include "ItemExtendedCostDbc.h"
#include "ContentBuildPaths.h"
#include "ServerTableDescriptor.h"


#include "third_party/json/json.hpp"
#include "third_party/miniz/miniz.h"

#include <memory>
#include <algorithm>
#include <set>
#include <limits>
#include <utility>

using json = nlohmann::json;

namespace
{
    bool ValidSymbol(std::string const& value)
    {
        if (value.empty() || value.size() > 64 || !(value[0] >= 'a' && value[0] <= 'z')) return false;
        for (char c : value)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
                return false;
        return true;
    }

    bool IsSafeRelativePath(std::string const& value)
    {
        try { ContentBuildPaths::Target(value); return true; }
        catch (std::exception const&) { return false; }
    }

    bool IsRegularZipEntry(mz_zip_archive& zip, int index)
    {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, index, &stat) || stat.m_is_directory)
            return false;
        // ZIP Unix mode bits distinguish files from symlinks/devices. Zero means
        // the producer did not supply Unix type metadata (normal DOS/Windows ZIP).
        auto type = (stat.m_external_attr >> 16) & 0170000;
        return type == 0 || type == 0100000;
    }
}
ContentPackage::ContentPackage(std::filesystem::path path)
    : _path(std::move(path))
{
}

ContentPackageValidationResult ContentPackage::Validate() const
{
    ContentPackageValidationResult result;
    try
    {
        ContentBuildPaths::RejectLinks(_path);
        ContentBuildPaths::Require(std::filesystem::is_regular_file(_path), "EPF source is not a regular file");
    }
    catch (std::exception const& exception)
    {
        result.error = exception.what();
        return result;
    }

    mz_zip_archive zip{};

    if (!mz_zip_reader_init_file(&zip, _path.string().c_str(), 0))
    {
        result.error = "File is not a valid ZIP archive";
        return result;
    }

    struct ZipCloser
    {
        mz_zip_archive* zip;

        ~ZipCloser()
        {
            mz_zip_reader_end(zip);
        }
    } closer{ &zip };

    int fileIndex = mz_zip_reader_locate_file(
        &zip,
        "manifest.json",
        nullptr,
        0);

    if (fileIndex < 0)
    {
        result.error = "manifest.json not found";
        return result;
    }

    if (!IsRegularZipEntry(zip, fileIndex))
    {
        result.error = "manifest.json is not a regular ZIP file";
        return result;
    }
    size_t manifestSize = 0;

    void* manifestData =
        mz_zip_reader_extract_to_heap(
            &zip,
            fileIndex,
            &manifestSize,
            0);

    if (!manifestData)
    {
        result.error = "Could not read manifest.json";
        return result;
    }

    std::unique_ptr<void, decltype(&mz_free)>
        manifestBuffer(manifestData, mz_free);

    std::string manifestText(
        static_cast<char const*>(manifestData),
        manifestSize);

    json manifest;

    try
    {
        manifest = json::parse(manifestText);
    }
    catch (std::exception const& e)
    {
        result.error =
            std::string("Invalid JSON: ") + e.what();

        return result;
    }

    // ---------------------------------------------------------
    // Required manifest metadata
    // ---------------------------------------------------------

    if (!manifest.contains("schema") ||
        !manifest["schema"].is_number_unsigned())
    {
        result.error = "Missing or invalid 'schema'";
        return result;
    }

    if (!manifest.contains("package") ||
        !manifest["package"].is_string())
    {
        result.error = "Missing or invalid 'package'";
        return result;
    }

    result.manifest.packageKey = manifest["package"].get<std::string>();

    if (!manifest.contains("name") ||
        !manifest["name"].is_string())
    {
        result.error = "Missing or invalid 'name'";
        return result;
    }

    if (!manifest.contains("version") ||
        !manifest["version"].is_string())
    {
        result.error = "Missing or invalid 'version'";
        return result;
    }

    result.manifest.schema =
        manifest["schema"].get<uint32_t>();

    result.manifest.packageKey =
        manifest["package"].get<std::string>();

    result.manifest.name =
        manifest["name"].get<std::string>();

    result.manifest.version =
        manifest["version"].get<std::string>();

    // ---------------------------------------------------------
    // Validate basic metadata
    // ---------------------------------------------------------

    if (result.manifest.schema != 1 && result.manifest.schema != 2 && result.manifest.schema != 3)
    {
        result.error =
            "Unsupported manifest schema " +
            std::to_string(result.manifest.schema);

        return result;
    }
    if (result.manifest.schema == 1 && (manifest.contains("serverRows") || manifest.contains("currencies") || manifest.contains("currencyCategories") || manifest.contains("extendedCosts") || manifest.contains("vendorRows")))
    { result.error = "serverRows require Schema 2"; return result; }

    // ---------------------------------------------------------
    // Schema 3 client capability requirements
    // ---------------------------------------------------------

    if (result.manifest.schema < 3 && manifest.contains("clientRequirements"))
    {
        result.error = "clientRequirements require Schema 3";
        return result;
    }

    if (manifest.contains("clientRequirements") &&
        !manifest["clientRequirements"].is_array())
    {
        result.error = "'clientRequirements' must be an array";
        return result;
    }

    {
        std::set<std::string> requirements;
        for (auto const& requirement : (manifest.contains("clientRequirements") ? manifest["clientRequirements"] : json::array()))
        {
            if (!requirement.is_string())
            {
                result.error = "Client requirement must be a string";
                return result;
            }
            auto name = requirement.get<std::string>();
            if (!ContentClientRequirement::IsSupported(name))
            {
                result.error = "Unsupported client requirement '" + name + "'; supported: " + ContentClientRequirement::ProtectedFrameXml;
                return result;
            }
            // Duplicates are an authoring artifact; the manifest requirement set stays deduplicated.
            requirements.insert(name);
        }
        result.manifest.clientRequirements.assign(requirements.begin(), requirements.end());
    }

    if (result.manifest.packageKey.empty())
    {
        result.error = "'package' cannot be empty";
        return result;
    }

    if (result.manifest.name.empty())
    {
        result.error = "'name' cannot be empty";
        return result;
    }

    if (result.manifest.version.empty())
    {
        result.error = "'version' cannot be empty";
        return result;
    }

    // ---------------------------------------------------------
    // Optional description
    // ---------------------------------------------------------

    if (manifest.contains("description"))
    {
        if (!manifest["description"].is_string())
        {
            result.error = "Invalid 'description'";
            return result;
        }

        result.manifest.description =
            manifest["description"].get<std::string>();
    }

    // ---------------------------------------------------------
    // Content array
    // ---------------------------------------------------------

    if ((result.manifest.schema == 1 && !manifest.contains("content")) ||
        (manifest.contains("content") && !manifest["content"].is_array()))
    {
        result.error = "Missing or invalid 'content'";
        return result;
    }

    if (result.manifest.schema == 1 && manifest["content"].empty())
    {
        result.error = "'content' cannot be empty";
        return result;
    }

    for (auto const& item : (manifest.contains("content") ? manifest["content"] : json::array()))
    {
        if (!item.is_object())
        {
            result.error =
                "Content entry must be an object";

            return result;
        }

        if (!item.contains("type") ||
            !item["type"].is_string())
        {
            result.error =
                "Content entry missing or invalid 'type'";

            return result;
        }

        if (!item.contains("source") ||
            !item["source"].is_string())
        {
            result.error =
                "Content entry missing or invalid 'source'";

            return result;
        }

        if (!item.contains("target") ||
            !item["target"].is_string())
        {
            result.error =
                "Content entry missing or invalid 'target'";

            return result;
        }

        ContentPackageEntry entry;

        entry.type =
            item["type"].get<std::string>();

        entry.source =
            item["source"].get<std::string>();

        entry.target =
            item["target"].get<std::string>();

        if (entry.type.empty() ||
            entry.source.empty() ||
            entry.target.empty())
        {
            result.error =
                "Content entry fields cannot be empty";

            return result;
        }

        // Schema 1 currently supports raw files only.
        if (entry.type != "file")
        {
            result.error =
                "Unsupported content type '" +
                entry.type + "'";

            return result;
        }

        // Never allow an EPF to escape its staging area.
        if (!IsSafeRelativePath(entry.source))
        {
            result.error =
                "Unsafe content source path: " +
                entry.source;

            return result;
        }

        if (!IsSafeRelativePath(entry.target))
        {
            result.error =
                "Unsafe content target path: " +
                entry.target;

            return result;
        }

        // Make sure the source declared by the manifest
        // really exists inside this EPF.
        int sourceIndex =
            mz_zip_reader_locate_file(
                &zip,
                entry.source.c_str(),
                nullptr,
                0);

        if (sourceIndex < 0)
        {
            result.error =
                "Content source not found in EPF: " +
                entry.source;

            return result;
        }

        if (!IsRegularZipEntry(zip, sourceIndex))
        {
            result.error = "Content source is not a regular ZIP file: " + entry.source;
            return result;
        }
        result.manifest.content.push_back(
            std::move(entry));
    }

    if (result.manifest.schema >= 2)
    {
        if (manifest.contains("dbcRows") && !manifest["dbcRows"].is_array())
        {
            result.error = "Schema 2 dbcRows must be an array";
            return result;
        }
        std::set<std::string> symbols;
        for (auto const& row : (manifest.contains("dbcRows") ? manifest["dbcRows"] : json::array()))
        {
            if (!row.is_object())
            { result.error = "DBC row must be an object"; return result; }
            static std::set<std::string> const rowKeys = {"op", "table", "symbol", "fields"};
            for (auto it = row.begin(); it != row.end(); ++it)
                if (!rowKeys.count(it.key()))
                { result.error = "Unsupported or allocator-owned DBC row key: " + it.key(); return result; }
            if (!row.is_object() || !row.contains("op") || !row["op"].is_string()
                || row["op"] != "add")
            { result.error = "Schema 2 supports only dbcRows op=add"; return result; }
            if (!row.contains("table") || !row["table"].is_string() || row["table"] != "Item")
            { result.error = "Schema 2 supports only dbcRows table=Item"; return result; }
            if (!row.contains("symbol") || !row["symbol"].is_string()
                || !ValidSymbol(row["symbol"].get<std::string>()))
            { result.error = "Invalid Item symbol (use lowercase ASCII letter, then letters/digits/._-)"; return result; }
            ContentItemRow item;
            item.symbol = row["symbol"].get<std::string>();
            if (!symbols.insert(item.symbol).second)
            { result.error = "Duplicate Schema 2 symbol: " + item.symbol; return result; }
            if (!row.contains("fields") || !row["fields"].is_object())
            { result.error = "Item add requires fields object"; return result; }
            auto const& fields = row["fields"];
            static std::set<std::string> const allowed = {"ClassID", "SubclassID", "SoundOverrideSubclassID",
                "Material", "DisplayInfoID", "InventoryType", "SheatheType"};
            for (auto it = fields.begin(); it != fields.end(); ++it)
                if (!allowed.count(it.key()))
                { result.error = "Unsupported or allocator-owned Item field: " + it.key(); return result; }
            auto unsignedField = [&](char const* name, std::uint32_t& out) -> bool {
                if (!fields.contains(name) || !fields[name].is_number_unsigned()) return false;
                auto value = fields[name].get<std::uint64_t>();
                if (value > std::numeric_limits<std::uint32_t>::max()) return false;
                out = static_cast<std::uint32_t>(value); return true;
            };
            auto signedField = [&](char const* name, std::int32_t& out) -> bool {
                if (!fields.contains(name) || !fields[name].is_number_integer()) return false;
                auto value = fields[name].get<std::int64_t>();
                if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) return false;
                out = static_cast<std::int32_t>(value); return true;
            };
            if (!unsignedField("ClassID", item.classID) || !unsignedField("SubclassID", item.subclassID)
                || !signedField("SoundOverrideSubclassID", item.soundOverrideSubclassID)
                || !signedField("Material", item.material)
                || !unsignedField("InventoryType", item.inventoryType)
                || !unsignedField("SheatheType", item.sheatheType))
            { result.error = "Item fields missing or out of 32-bit range"; return result; }
            if (!fields.contains("DisplayInfoID") || !fields["DisplayInfoID"].is_object()
                || fields["DisplayInfoID"].size() != 1
                || !fields["DisplayInfoID"].contains("copyFromItem")
                || !fields["DisplayInfoID"]["copyFromItem"].is_number_unsigned())
            { result.error = "DisplayInfoID must use {copyFromItem: stock Item ID}"; return result; }
            auto copy = fields["DisplayInfoID"]["copyFromItem"].get<std::uint64_t>();
            if (!copy || copy > std::numeric_limits<std::uint32_t>::max())
            { result.error = "Invalid DisplayInfoID copyFromItem ID"; return result; }
            item.displayCopyFromItem = static_cast<std::uint32_t>(copy);
            result.manifest.itemRows.push_back(std::move(item));
        }
        if (manifest.contains("serverRows") && !manifest["serverRows"].is_array())
        { result.error = "Schema 2 serverRows must be an array"; return result; }
        std::set<std::string> serverSymbols;
        for (auto const& row : (manifest.contains("serverRows") ? manifest["serverRows"] : json::array()))
        {
            if (!row.is_object())
            { result.error = "Server row must be an object"; return result; }
            static std::set<std::string> const rowKeys = {"table", "op", "symbol", "fields"};
            for (auto it = row.begin(); it != row.end(); ++it)
                if (!rowKeys.count(it.key()))
                { result.error = "Unsupported or allocator-owned server row key: " + it.key(); return result; }
            if (!row.contains("table") || !row["table"].is_string()
                || !FindServerTableDescriptor(row["table"].get<std::string>()))
            { result.error = "Unsupported server table (only item_template)"; return result; }
            if (!row.contains("op") || !row["op"].is_string() || row["op"] != "upsert")
            { result.error = "item_template supports only op=upsert"; return result; }
            if (!row.contains("symbol") || !row["symbol"].is_string()
                || !ValidSymbol(row["symbol"].get<std::string>()))
            { result.error = "Invalid server row symbol"; return result; }
            auto symbol = row["symbol"].get<std::string>();
            if (!serverSymbols.insert(symbol).second)
            { result.error = "Duplicate server declaration: " + symbol; return result; }
            if (!symbols.count(symbol))
            { result.error = "Server row references missing package-local Item symbol: " + symbol; return result; }
            auto client = std::find_if(result.manifest.itemRows.begin(), result.manifest.itemRows.end(),
                [&](auto const& item) { return item.symbol == symbol; });
            if (client == result.manifest.itemRows.end() || client->classID > 255 || client->subclassID > 255
                || client->inventoryType > 255 || client->sheatheType > 255
                || client->soundOverrideSubclassID < -128 || client->soundOverrideSubclassID > 127
                || client->material < -128 || client->material > 127)
            { result.error = "Item DBC fields exceed item_template descriptor types"; return result; }
            if (!row.contains("fields") || !row["fields"].is_object())
            { result.error = "item_template requires fields object"; return result; }
            auto const* descriptor = FindServerTableDescriptor("item_template");
            auto const& fields = row["fields"];
            for (auto it = fields.begin(); it != fields.end(); ++it)
            {
                auto known = std::find_if(descriptor->fields.begin(), descriptor->fields.end(),
                    [&](auto const& field) { return it.key() == field.name; });
                if (known == descriptor->fields.end())
                { result.error = "Unsupported or allocator-owned item_template field: " + it.key(); return result; }
            }
            for (auto const& field : descriptor->fields)
            {
                if (field.required && !fields.contains(field.name))
                { result.error = std::string("Missing item_template field: ") + field.name; return result; }
                auto const& value = fields[field.name];
                if (field.type == ServerFieldType::String)
                {
                    if (!value.is_string() || value.get<std::string>().size() < static_cast<std::size_t>(field.minimum)
                        || value.get<std::string>().size() > static_cast<std::size_t>(field.maximum)
                        || value.get<std::string>().find('\0') != std::string::npos)
                    { result.error = std::string("Invalid item_template string field: ") + field.name; return result; }
                }
                else if (!value.is_number_unsigned() || value.get<std::uint64_t>()
                    > static_cast<std::uint64_t>(field.maximum)
                    || value.get<std::uint64_t>() < static_cast<std::uint64_t>(field.minimum))
                { result.error = std::string("Invalid item_template numeric field: ") + field.name; return result; }
            }
            ContentServerItemRow item;
            item.symbol = symbol;
            item.name = fields["name"].get<std::string>();
            item.description = fields["description"].get<std::string>();
            item.quality = fields["Quality"].get<std::uint8_t>();
            item.stackable = fields["stackable"].get<std::int32_t>();
            item.bonding = fields["bonding"].get<std::uint8_t>();
            item.bagFamily = fields["BagFamily"].get<std::int32_t>();
            result.manifest.serverItemRows.push_back(std::move(item));
        }
        if (manifest.contains("currencyCategories"))
        {
            if (!manifest["currencyCategories"].is_array())
            { result.error = "currencyCategories must be an array"; return result; }
            for (auto const& category : manifest["currencyCategories"])
            {
                if (!category.is_object() || category.size() != 2 || !category.contains("symbol")
                    || !category["symbol"].is_string() || !category.contains("name") || !category["name"].is_object())
                { result.error = "Currency category requires only symbol and localized name; ID is allocator-owned"; return result; }
                ContentCurrencyCategory row;
                row.symbol = category["symbol"].get<std::string>();
                if (!ValidSymbol(row.symbol) || !symbols.insert(row.symbol).second)
                { result.error = "Invalid/duplicate currency category symbol"; return result; }
                try
                {
                    row.names = category["name"].get<std::map<std::string, std::string>>();
                    CurrencyCategoryDbcComposer::ValidateNames(row.names);
                }
                catch (std::exception const& e) { result.error = e.what(); return result; }
                result.manifest.currencyCategories.push_back(row);
            }
        }
        if (manifest.contains("currencies"))
        {
            if (!manifest["currencies"].is_array())
            { result.error = "currencies must be an array"; return result; }
            std::set<std::string> currencyItems;
            for (auto const& currency : manifest["currencies"])
            {
                if (!currency.is_object() || currency.size() != 3
                    || !currency.contains("symbol") || !currency["symbol"].is_string()
                    || !currency.contains("item") || !currency["item"].is_string()
                    || (currency.contains("categoryCopyFromItem") == currency.contains("category")))
                { result.error = "Currency requires symbol, item and exactly one of categoryCopyFromItem or category"; return result; }
                ContentCurrencyRow row{currency["symbol"].get<std::string>(), currency["item"].get<std::string>(), 0};
                std::uint64_t donor = 0;
                if (currency.contains("categoryCopyFromItem"))
                {
                    if (!currency["categoryCopyFromItem"].is_number_unsigned())
                    { result.error = "categoryCopyFromItem requires a stock ItemID"; return result; }
                    donor = currency["categoryCopyFromItem"].get<std::uint64_t>();
                    if (!donor || donor > 0x7fffffffULL)
                    { result.error = "Invalid category donor ItemID"; return result; }
                }
                else
                {
                    auto const& ref = currency["category"];
                    if (!ref.is_object() || ref.size() != 1 || !ref.contains("symbol") || !ref["symbol"].is_string())
                    { result.error = "category requires a package-local symbol reference"; return result; }
                    row.categorySymbol = ref["symbol"].get<std::string>();
                    if (std::none_of(result.manifest.currencyCategories.begin(), result.manifest.currencyCategories.end(),
                        [&](auto const& c) { return c.symbol == row.categorySymbol; }))
                    { result.error = "Missing package-local currency category symbol"; return result; }
                }
                if (!ValidSymbol(row.symbol) || !symbols.insert(row.symbol).second
                    || !currencyItems.insert(row.itemSymbol).second)
                { result.error = "Invalid/duplicate currency symbol, item or category donor"; return result; }
                auto server = std::find_if(result.manifest.serverItemRows.begin(), result.manifest.serverItemRows.end(),
                    [&](auto const& item) { return item.symbol == row.itemSymbol; });
                if (server == result.manifest.serverItemRows.end() || server->bagFamily != 8192)
                { result.error = "Currency must reference a package-local server Item with BagFamily=8192"; return result; }
                row.categoryCopyFromItem = static_cast<std::uint32_t>(donor);
                result.manifest.currencyRows.push_back(row);
            }
        }
        for (auto const& category : result.manifest.currencyCategories)
            if (std::none_of(result.manifest.currencyRows.begin(), result.manifest.currencyRows.end(),
                [&](auto const& currency) { return currency.categorySymbol == category.symbol; }))
            { result.error = "Currency category must be referenced by a package-local currency"; return result; }
        for (auto const& server : result.manifest.serverItemRows)
            if (server.bagFamily != 0 && (server.bagFamily != 8192 || std::none_of(
                result.manifest.currencyRows.begin(), result.manifest.currencyRows.end(),
                [&](auto const& row) { return row.itemSymbol == server.symbol; })))
            { result.error = "BagFamily supports only 0 or a declared currency token (8192)"; return result; }
        if (manifest.contains("extendedCosts"))
        {
            if (!ValidSymbol(result.manifest.packageKey) || !manifest["extendedCosts"].is_array())
            { result.error = "extendedCosts needs a valid logical package key and an array"; return result; }
            for (auto const& declaration : manifest["extendedCosts"])
            {
                if (!declaration.is_object() || !declaration.contains("symbol") || !declaration["symbol"].is_string()
                    || !ValidSymbol(declaration["symbol"].get<std::string>())
                    || !symbols.insert(declaration["symbol"].get<std::string>()).second
                    || !declaration.contains("requirements") || !declaration["requirements"].is_array()
                    || declaration["requirements"].empty() || declaration["requirements"].size() > 5)
                { result.error = "Extended cost requires a unique logical symbol and 1..5 item requirements"; return result; }
                for (auto const& field : declaration.items())
                    if (field.key() != "symbol" && field.key() != "requirements" && field.key() != "honorPoints"
                        && field.key() != "arenaPoints" && field.key() != "arenaBracket" && field.key() != "requiredArenaRating")
                    { result.error = "Unsupported or allocator-owned extended-cost field: " + field.key(); return result; }
                ContentExtendedCost cost; cost.symbol = declaration["symbol"].get<std::string>();
                auto number = [&](char const* key,std::uint32_t max) {
                    if (!declaration.contains(key)) return std::uint32_t(0);
                    auto const& value = declaration[key];
                    if (!value.is_number_unsigned() || value.get<std::uint64_t>() > max)
                        throw std::runtime_error(std::string("Invalid extended-cost field: ") + key);
                    return value.get<std::uint32_t>();
                };
                try
                {
                    cost.honorPoints=number("honorPoints",ItemExtendedCostDbc::MaxPoints);
                    cost.arenaPoints=number("arenaPoints",ItemExtendedCostDbc::MaxPoints);
                    cost.arenaBracket=number("arenaBracket",2);
                    cost.requiredArenaRating=number("requiredArenaRating",0x7fffffffU);
                    if (cost.arenaBracket && !cost.requiredArenaRating) throw std::runtime_error("arenaBracket requires a rating requirement");
                    std::set<std::pair<std::string,std::string>> references;
                    for (auto const& requirement : declaration["requirements"])
                    {
                        if (!requirement.is_object() || requirement.size()!=2 || !requirement.contains("item")
                            || !requirement.contains("count") || !requirement["count"].is_number_unsigned()
                            || !requirement["count"].get<std::uint64_t>() || requirement["count"].get<std::uint64_t>()>ItemExtendedCostDbc::MaxCount)
                            throw std::runtime_error("Extended-cost requirement needs a logical item and positive safe count");
                        auto const& item=requirement["item"];
                        if (!item.is_object() || !item.contains("symbol") || !item["symbol"].is_string()
                            || !ValidSymbol(item["symbol"].get<std::string>())
                            || item.size()!=(item.contains("package")?2:1)
                            || (item.contains("package") && (!item["package"].is_string() || !ValidSymbol(item["package"].get<std::string>()))))
                            throw std::runtime_error("Item requirement accepts only a logical symbol and optional package");
                        ContentCostRequirement ref{item.value("package",result.manifest.packageKey),item["symbol"].get<std::string>(),
                            requirement["count"].get<std::uint32_t>()};
                        if (!references.emplace(ref.packageKey,ref.symbol).second) throw std::runtime_error("Duplicate extended-cost item reference");
                        if (ref.packageKey==result.manifest.packageKey && std::none_of(result.manifest.serverItemRows.begin(),result.manifest.serverItemRows.end(),
                            [&](auto const& row){return row.symbol==ref.symbol;}))
                            throw std::runtime_error("Local cost requirement must reference a declared client/server Item");
                        cost.requirements.push_back(ref);
                    }
                }
                catch (std::exception const& e) { result.error=e.what(); return result; }
                result.manifest.extendedCosts.push_back(cost);
            }
        }
        if (manifest.contains("vendorRows"))
        {
            if (!manifest["vendorRows"].is_array()) { result.error="vendorRows must be an array"; return result; }
            std::set<std::uint32_t> vendors;
            for (auto const& d : manifest["vendorRows"])
            {
                if (!d.is_object() || d.size()!=4 || !d.contains("symbol") || !d["symbol"].is_string()
                    || !ValidSymbol(d["symbol"].get<std::string>()) || !symbols.insert(d["symbol"].get<std::string>()).second
                    || !d.contains("extendedCost") || !d["extendedCost"].is_object() || d["extendedCost"].size()!=1
                    || !d["extendedCost"].contains("symbol") || !d["extendedCost"]["symbol"].is_string()
                    || !d.contains("creatureEntry") || !d["creatureEntry"].is_number_unsigned()
                    || !d["creatureEntry"].get<std::uint64_t>() || d["creatureEntry"].get<std::uint64_t>()>0xffffff
                    || !d.contains("itemEntry") || !d["itemEntry"].is_number_unsigned()
                    || !d["itemEntry"].get<std::uint64_t>() || d["itemEntry"].get<std::uint64_t>()>0xffffff)
                { result.error="vendorRows requires symbol, existing creatureEntry/itemEntry and logical extendedCost.symbol"; return result; }
                ContentVendorRow row{d["symbol"].get<std::string>(),d["extendedCost"]["symbol"].get<std::string>(),
                    d["creatureEntry"].get<std::uint32_t>(),d["itemEntry"].get<std::uint32_t>()};
                if (!vendors.insert(row.creatureEntry).second || std::none_of(result.manifest.extendedCosts.begin(),result.manifest.extendedCosts.end(),
                    [&](auto const& c){return c.symbol==row.extendedCostSymbol;}))
                { result.error="Vendor needs a local declared extended cost and a distinct existing creature"; return result; }
                result.manifest.vendorRows.push_back(row);
            }
        }
        if (result.manifest.content.empty() && result.manifest.itemRows.empty() && result.manifest.extendedCosts.empty())
        { result.error = "Schema 2 or newer needs content, dbcRows or extendedCosts"; return result; }
    }
    result.valid = true;
    return result;
}

ContentPackageStageResult ContentPackage::Stage(std::filesystem::path const& workDirectory) const
{
    namespace fs = std::filesystem;
    using namespace ContentBuildPaths;
    ContentPackageStageResult result;
    try
    {
        auto validation = Validate();
        Require(validation.valid, "Package validation failed: " + validation.error);
        auto const& key = validation.manifest.packageKey;
        Require(Target(key) == key && key.find('/') == std::string::npos,
            "Package key must be a single safe directory name");
        Require(!workDirectory.empty(), "WorkDirectory must not be empty");
        RejectLinks(workDirectory);
        fs::create_directories(workDirectory);
        auto root = fs::canonical(workDirectory);
        result.stagingDirectory = root / key;
        RejectLinks(result.stagingDirectory);
        if (fs::exists(result.stagingDirectory))
        {
            std::string error;
            if (!Cleanup(result.stagingDirectory, root, error))
                throw std::runtime_error("Could not clear staging directory: " + error);
        }
        Require(fs::create_directory(result.stagingDirectory), "Could not create clean staging directory");
        return StageInto(result.stagingDirectory, validation.manifest);
    }
    catch (std::exception const& exception)
    {
        result.error = exception.what();
        return result;
    }
}

ContentPackageStageResult ContentPackage::StageInto(std::filesystem::path const& stagingDirectory,
    ContentPackageManifest const& expected) const
{
    namespace fs = std::filesystem;
    using namespace ContentBuildPaths;
    ContentPackageStageResult result;
    result.stagingDirectory = stagingDirectory;
    try
    {
        auto validation = Validate();
        Require(validation.valid, "Package validation failed: " + validation.error);
        auto const& actual = validation.manifest;
        bool same = actual.schema == expected.schema && actual.packageKey == expected.packageKey
            && actual.version == expected.version && actual.content.size() == expected.content.size()
            && actual.clientRequirements == expected.clientRequirements
            && actual.itemRows == expected.itemRows && actual.serverItemRows == expected.serverItemRows
            && actual.currencyRows == expected.currencyRows && actual.currencyCategories == expected.currencyCategories && actual.extendedCosts == expected.extendedCosts && actual.vendorRows == expected.vendorRows;
        if (same)
            for (std::size_t i = 0; i < actual.content.size(); ++i)
                same = same && actual.content[i].type == expected.content[i].type
                    && actual.content[i].source == expected.content[i].source
                    && actual.content[i].target == expected.content[i].target;
        Require(same, "Package manifest changed after build preflight: " + expected.packageKey);
        RejectLinks(stagingDirectory);
        Require(fs::is_directory(stagingDirectory), "Staging workspace does not exist");
        auto root = fs::canonical(stagingDirectory);

        mz_zip_archive zip{};
        Require(mz_zip_reader_init_file(&zip, _path.string().c_str(), 0), "Could not reopen EPF archive");
        struct ZipCloser
        {
            mz_zip_archive* zip;
            ~ZipCloser() { mz_zip_reader_end(zip); }
        } closer{&zip};

        for (auto const& entry : expected.content)
        {
            auto destination = root / Target(entry.target);
            RejectLinks(destination);
            Require(IsBeneath(fs::weakly_canonical(destination), root), "Target escapes staging workspace");
            Require(!fs::exists(fs::symlink_status(destination)), "Staging target already exists: " + entry.target);
            fs::create_directories(destination.parent_path());
            RejectLinks(destination);
            auto index = mz_zip_reader_locate_file(&zip, entry.source.c_str(), nullptr, 0);
            Require(index >= 0 && IsRegularZipEntry(zip, index), "Missing or non-regular EPF source: " + entry.source);
            Require(mz_zip_reader_extract_to_file(&zip, index, destination.string().c_str(), 0),
                "Could not extract '" + entry.source + "' to '" + destination.string() + "'");
            result.stagedFiles.push_back(destination);
        }
        result.success = true;
    }
    catch (std::exception const& exception)
    {
        result.error = exception.what();
    }
    return result;
}

