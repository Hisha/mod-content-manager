#include "MpqBuilder.h"

#include <StormLib.h>

#include <algorithm>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

namespace
{
namespace fs = std::filesystem;

void Require(bool condition, std::string const& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void RejectLinks(fs::path const& path)
{
    fs::path prefix;
    for (auto const& part : path)
    {
        prefix /= part;
        Require(!fs::is_symlink(fs::symlink_status(prefix)),
            "Symlink is not allowed: " + prefix.string());
    }
}

bool IsBeneath(fs::path const& path, fs::path const& root)
{
    auto relative = path.lexically_relative(root);
    return !relative.empty() && relative != "." && !relative.is_absolute()
        && *relative.begin() != "..";
}

std::basic_string<TCHAR> NativeName(fs::path const& path)
{
#ifdef _UNICODE
    return path.wstring();
#else
    return path.string();
#endif
}

std::string StormError(char const* operation)
{
    return std::string(operation) + " (StormLib error "
        + std::to_string(SErrGetLastError()) + ")";
}

struct ArchiveGuard
{
    HANDLE handle = nullptr;
    fs::path directory;

    ~ArchiveGuard()
    {
        if (handle)
            SFileCloseArchive(handle);
        if (!directory.empty())
        {
            std::error_code ignored;
            fs::remove(directory / "archive.mpq", ignored);
            fs::remove(directory, ignored);
        }
    }
};
}

MpqBuildResult MpqBuilder::Build(fs::path const& sourceDirectory,
    fs::path const& outputMpq) const
{
    MpqBuildResult result;
    result.outputPath = outputMpq;
    ArchiveGuard archive;
    try
    {
        Require(!sourceDirectory.empty(), "Source directory is empty");
        auto source = fs::absolute(sourceDirectory);
        RejectLinks(source);
        Require(fs::is_directory(source), "Source is not a directory: " + source.string());
        source = fs::canonical(source);

        Require(!outputMpq.empty() && outputMpq.has_filename(), "Output MPQ filename is empty");
        auto output = fs::absolute(outputMpq);
        RejectLinks(output);
        Require(!fs::exists(fs::symlink_status(output)), "Output already exists: " + output.string());
        output = fs::weakly_canonical(output);
        Require(!IsBeneath(output, source), "Output MPQ must be outside the source directory");

        struct Input { fs::path path; std::string name; };
        std::vector<Input> files;
        std::set<std::string> names;
        for (auto const& entry : fs::recursive_directory_iterator(source))
        {
            auto status = entry.symlink_status();
            Require(!fs::is_symlink(status), "Symlink is not allowed: " + entry.path().string());
            Require(IsBeneath(fs::canonical(entry.path()), source), "Input escapes source directory");
            if (fs::is_directory(status))
                continue;
            Require(fs::is_regular_file(status), "Non-regular input: " + entry.path().string());
            auto relative = entry.path().lexically_relative(source);
            std::string name;
            for (auto const& part : relative)
            {
                auto component = part.string();
                Require(component != ".." && component.find_first_of("\\:") == std::string::npos,
                    "Unsafe MPQ path: " + relative.string());
                if (!name.empty())
                    name += '\\';
                name += component;
            }
            auto folded = name;
            for (char& c : folded)
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            Require(folded != "(LISTFILE)" && folded != "(ATTRIBUTES)" && folded != "(SIGNATURE)",
                "Reserved MPQ name: " + name);
            Require(names.insert(folded).second, "Duplicate MPQ name: " + name);
            files.push_back({entry.path(), name});
        }
        Require(!files.empty(), "Source directory contains no regular files");
        // StormLib adds a reserved listfile slot and rounds up to a power of two.
        Require(files.size() < HASH_TABLE_SIZE_MAX, "Too many files for an MPQ v1 archive");
        std::sort(files.begin(), files.end(), [](Input const& a, Input const& b) { return a.name < b.name; });
        fs::create_directories(output.parent_path());
        std::random_device random;
        for (unsigned attempt = 0; attempt < 100 && archive.directory.empty(); ++attempt)
        {
            auto candidate = output.parent_path() / (".mpq-build-" + std::to_string(random()));
            if (fs::create_directory(candidate))
                archive.directory = candidate;
        }
        Require(!archive.directory.empty(), "Could not reserve temporary MPQ directory");
        auto temporary = archive.directory / "archive.mpq";
        auto nativeOutput = NativeName(temporary);
        if (!SFileCreateArchive(nativeOutput.c_str(), MPQ_CREATE_ARCHIVE_V1 | MPQ_CREATE_LISTFILE,
            static_cast<DWORD>(files.size()), &archive.handle))
            throw std::runtime_error(StormError("Could not create MPQ"));

        for (auto const& file : files)
        {
            RejectLinks(file.path);
            Require(fs::is_regular_file(file.path) && IsBeneath(fs::canonical(file.path), source),
                "Input changed during build: " + file.path.string());
            auto nativeInput = NativeName(file.path);
            if (!SFileAddFileEx(archive.handle, nativeInput.c_str(), file.name.c_str(),
                MPQ_FILE_COMPRESS, MPQ_COMPRESSION_ZLIB, MPQ_COMPRESSION_ZLIB))
                throw std::runtime_error(StormError("Could not add file") + ": " + file.name);
        }
        // Close alone does not propagate flush errors in StormLib 9.30.0.
        if (!SFileFlushArchive(archive.handle))
            throw std::runtime_error(StormError("Could not flush MPQ"));
        bool closed = SFileCloseArchive(archive.handle);
        archive.handle = nullptr;
        if (!closed)
            throw std::runtime_error(StormError("Could not close MPQ"));

        // A hard link atomically publishes the complete file and fails if the target
        // exists, including when another builder wins the race. Both paths share a volume.
        fs::create_hard_link(temporary, output);
        result.success = true;
        result.fileCount = files.size();
    }
    catch (std::exception const& exception)
    {
        result.error = exception.what();
    }
    return result;
}


