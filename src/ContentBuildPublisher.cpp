#include "ContentBuildPublisher.h"
#include "ContentBuildHash.h"
#include "ContentBuildPaths.h"

#include <random>

ContentPublicationResult ContentBuildPublisher::Publish(std::filesystem::path const& source,
    std::filesystem::path const& publishDirectory, std::string const& expectedHash) const
{
    using namespace ContentBuildPaths;
    ContentPublicationResult result;
    fs::path temporaryDirectory;
    auto cleanup = [&]()
    {
        if (temporaryDirectory.empty())
            return;
        try
        {
            // Remove only our reserved file and directory, never recurse into an
            // externally substituted directory or remove another attempt's files.
            RejectLinks(temporaryDirectory);
            fs::remove(temporaryDirectory / "artifact.tmp");
            fs::remove(temporaryDirectory);
        }
        catch (std::exception const& exception)
        {
            if (!result.error.empty()) result.error += "; ";
            result.error += "Temporary publication cleanup failed at " + temporaryDirectory.string() + ": " + exception.what();
        }
    };
    try
    {
        Require(ContentBuildHash::Valid(expectedHash), "Invalid expected SHA256");
        Require(!publishDirectory.empty(), "PublishDirectory must not be empty");
        auto name = Target(source.filename().string());
        Require(name.find('/') == std::string::npos, "Publication requires a single versioned filename");
        std::string hash, error;
        if (!ContentBuildHash::Calculate(source, hash, error))
            throw std::runtime_error("Source verification failed: " + error);
        Require(hash == expectedHash, "Source SHA256 mismatch; publication refused");

        RejectLinks(publishDirectory);
        auto root = fs::weakly_canonical(fs::absolute(publishDirectory));
        auto output = fs::canonical(source).parent_path();
        Require(root != output && !IsBeneath(root, output) && !IsBeneath(output, root),
            "PublishDirectory and OutputDirectory must be separate, non-overlapping directories");
        fs::create_directories(root);
        RejectLinks(root);
        Require(fs::is_directory(root), "PublishDirectory is not a directory");
        result.path = root / name;
        RejectLinks(result.path);
        if (fs::exists(result.path))
        {
            Require(fs::is_regular_file(result.path), "Published destination is not a regular file");
            if (ContentBuildHash::Calculate(result.path, hash, error) && hash == expectedHash)
            {
                result.sha256 = hash;
                result.reused = true;
                result.success = true;
                return result;
            }
        }

        // Reserve a unique private staging directory on the destination filesystem.
        // Permissions are restricted before any bytes are copied. Competing
        // processes cannot share a temporary file; no predictable name is opened.
        std::random_device random;
        for (unsigned attempt = 0; attempt < 100 && temporaryDirectory.empty(); ++attempt)
        {
            auto candidate = root / (".content-publish-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (fs::create_directory(candidate)) temporaryDirectory = candidate;
        }
        Require(!temporaryDirectory.empty(), "Could not reserve publication staging directory");
        fs::permissions(temporaryDirectory, fs::perms::owner_all, fs::perm_options::replace);
        auto temporary = temporaryDirectory / "artifact.tmp";
        Require(fs::copy_file(source, temporary, fs::copy_options::none), "Temporary MPQ copy failed");
        if (!ContentBuildHash::Calculate(temporary, hash, error))
            throw std::runtime_error("Temporary MPQ verification failed: " + error);
        Require(hash == expectedHash, "Temporary MPQ SHA256 mismatch; source may have changed during copying");

        RejectLinks(result.path);
        Require(!fs::exists(result.path) || fs::is_regular_file(result.path), "Published destination is not a regular file");
        // Same-filesystem rename exposes only a fully copied, verified artifact.
        // Never remove the final path first: platforms that cannot replace an
        // existing file atomically fail safely and retain its previous contents.
        fs::rename(temporary, result.path);
        if (!ContentBuildHash::Calculate(result.path, hash, error))
            throw std::runtime_error("Final published MPQ verification failed: " + error);
        Require(hash == expectedHash, "Final published MPQ SHA256 mismatch");
        result.sha256 = hash;
        cleanup();
        Require(result.error.empty(), result.error);
        result.success = true;
    }
    catch (std::exception const& exception)
    {
        result.error = exception.what();
        cleanup();
    }
    return result;
}
