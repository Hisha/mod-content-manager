#include "ContentPackage.h"

#include "Log.h"

#include "../third_party/json/json.hpp"
#include "../third_party/miniz/miniz.h"

#include <memory>

using json = nlohmann::json;

ContentPackage::ContentPackage(std::filesystem::path path)
    : _path(std::move(path))
{
}

ContentPackageValidationResult ContentPackage::Validate() const
{
    ContentPackageValidationResult result;

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

    if (result.manifest.schema != 1)
    {
        result.error =
            "Unsupported manifest schema " +
            std::to_string(result.manifest.schema);

        return result;
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

    result.valid = true;
    return result;
}