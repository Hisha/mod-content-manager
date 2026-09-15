#include "ContentPackage.h"
#include "ContentBuildPaths.h"

#include "Log.h"

#include "third_party/json/json.hpp"
#include "third_party/miniz/miniz.h"

#include <memory>
#include <utility>

using json = nlohmann::json;

namespace
{
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

    if (!manifest.contains("content") ||
        !manifest["content"].is_array())
    {
        result.error = "Missing or invalid 'content'";
        return result;
    }

    if (manifest["content"].empty())
    {
        result.error = "'content' cannot be empty";
        return result;
    }

    for (auto const& item : manifest["content"])
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
            && actual.version == expected.version && actual.content.size() == expected.content.size();
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

