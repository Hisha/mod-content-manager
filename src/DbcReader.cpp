#include "DbcReader.h"
#include "ContentBuildPaths.h"
#include <fstream>
#include <limits>
#include <stdexcept>

namespace
{
constexpr std::uint64_t MaxDbcBytes = 256ULL * 1024 * 1024;

std::uint32_t Get32(std::vector<std::uint8_t> const& data, std::size_t offset)
{
    return std::uint32_t(data[offset]) | (std::uint32_t(data[offset + 1]) << 8)
        | (std::uint32_t(data[offset + 2]) << 16) | (std::uint32_t(data[offset + 3]) << 24);
}

void Put32(std::vector<std::uint8_t>& data, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        data.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
}

DbcReadResult DbcReader::ReadBaseline(std::filesystem::path const& directory, DbcDescriptor const& descriptor)
{
    DbcReadResult result;
    try
    {
        if (directory.empty())
            throw std::runtime_error("DBC baseline is unconfigured");
        ContentBuildPaths::RejectLinks(directory);
        if (!std::filesystem::is_directory(directory))
            throw std::runtime_error("DBC baseline path is missing or not a directory: " + directory.string());
        auto root = std::filesystem::canonical(directory);
        auto source = root / descriptor.serverFile;
        ContentBuildPaths::RejectLinks(source);
        if (!ContentBuildPaths::IsBeneath(source, root))
            throw std::runtime_error("DBC source escaped baseline directory");
        return Read(source, descriptor);
    }
    catch (std::exception const& e)
    {
        result.error = e.what();
        return result;
    }
}

DbcReadResult DbcReader::Read(std::filesystem::path const& path, DbcDescriptor const& descriptor)
{
    DbcReadResult result;
    try
    {
        ContentBuildPaths::RejectLinks(path);
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("DBC source is missing or not a regular file: " + path.string());
        auto size = std::filesystem::file_size(path);
        if (size > MaxDbcBytes)
            throw std::runtime_error("DBC exceeds 256 MiB read limit: " + path.string());
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot open DBC: " + path.string());
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (size && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
            throw std::runtime_error("DBC changed or became unreadable while reading: " + path.string());
        if (std::filesystem::file_size(path) != size)
            throw std::runtime_error("DBC size changed during read: " + path.string());
        return Parse(bytes, descriptor);
    }
    catch (std::exception const& e)
    {
        result.error = e.what();
        return result;
    }
}

DbcReadResult DbcReader::Parse(std::vector<std::uint8_t> const& bytes, DbcDescriptor const& descriptor)
{
    DbcReadResult result;
    try
    {
        if (bytes.size() > MaxDbcBytes)
            throw std::runtime_error("WDBC input exceeds 256 MiB read limit");
        if (bytes.size() < 20)
            throw std::runtime_error("Truncated WDBC header (need 20 bytes)");
        if (bytes[0] != 'W' || bytes[1] != 'D' || bytes[2] != 'B' || bytes[3] != 'C')
            throw std::runtime_error("Invalid WDBC magic");
        auto& doc = result.document;
        doc.recordCount = Get32(bytes, 4);
        doc.fieldCount = Get32(bytes, 8);
        doc.recordSize = Get32(bytes, 12);
        doc.stringBlockSize = Get32(bytes, 16);
        if (doc.fieldCount != descriptor.fields.size())
            throw std::runtime_error("Field count mismatch for " + std::string(descriptor.tableName)
                + ": expected " + std::to_string(descriptor.fields.size()) + ", got " + std::to_string(doc.fieldCount));
        if (doc.recordSize != descriptor.fields.size() * 4)
            throw std::runtime_error("Record size mismatch for " + std::string(descriptor.tableName)
                + ": expected " + std::to_string(descriptor.fields.size() * 4) + ", got " + std::to_string(doc.recordSize));
        std::uint64_t recordBytes = std::uint64_t(doc.recordCount) * doc.recordSize;
        if (recordBytes > MaxDbcBytes - 20 || doc.stringBlockSize > MaxDbcBytes - 20 - recordBytes)
            throw std::runtime_error("WDBC header declares data beyond 256 MiB limit");
        std::uint64_t required = 20ULL + recordBytes + doc.stringBlockSize;
        if (required != bytes.size())
            throw std::runtime_error(required > bytes.size() ? "Truncated WDBC record or string region"
                : "Unexpected trailing bytes after WDBC string block");
        doc.words.reserve(static_cast<std::size_t>(recordBytes / 4));
        for (std::size_t offset = 20; offset < 20 + recordBytes; offset += 4)
            doc.words.push_back(Get32(bytes, offset));
        doc.strings.assign(bytes.begin() + static_cast<std::ptrdiff_t>(20 + recordBytes), bytes.end());
        for (std::size_t row = 0; row < doc.recordCount; ++row)
        {
            for (std::size_t field = 0; field < descriptor.fields.size(); ++field)
            {
                if (descriptor.fields[field].type != DbcFieldType::StringOffset)
                    continue;
                auto stringOffset = doc.words[row * doc.fieldCount + field];
                if (stringOffset >= doc.strings.size())
                    throw std::runtime_error("String offset out of range at row " + std::to_string(row)
                        + ", field " + descriptor.fields[field].name);
                bool terminated = false;
                for (std::size_t i = stringOffset; i < doc.strings.size(); ++i)
                    if (doc.strings[i] == 0) { terminated = true; break; }
                if (!terminated)
                    throw std::runtime_error("String missing NUL terminator at row " + std::to_string(row)
                        + ", field " + descriptor.fields[field].name);
            }
        }
        result.valid = true;
    }
    catch (std::exception const& e)
    {
        result.error = e.what();
    }
    return result;
}

std::vector<std::uint8_t> DbcReader::Serialize(DbcDocument const& document)
{
    std::uint64_t recordBytes = std::uint64_t(document.recordCount) * document.recordSize;
    if (recordBytes > MaxDbcBytes - 20 || document.stringBlockSize > MaxDbcBytes - 20 - recordBytes)
        throw std::runtime_error("Cannot serialize oversized WDBC document");
    std::uint64_t required = 20ULL + recordBytes + document.stringBlockSize;
    if (document.recordSize != std::uint64_t(document.fieldCount) * 4
        || document.words.size() != std::uint64_t(document.recordCount) * document.fieldCount
        || document.strings.size() != document.stringBlockSize)
        throw std::runtime_error("Cannot serialize inconsistent WDBC document");
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(required));
    bytes.insert(bytes.end(), {'W', 'D', 'B', 'C'});
    Put32(bytes, document.recordCount);
    Put32(bytes, document.fieldCount);
    Put32(bytes, document.recordSize);
    Put32(bytes, document.stringBlockSize);
    for (auto word : document.words)
        Put32(bytes, word);
    bytes.insert(bytes.end(), document.strings.begin(), document.strings.end());
    return bytes;
}
