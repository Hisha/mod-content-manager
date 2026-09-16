#ifndef CONTENT_DBC_READER_H
#define CONTENT_DBC_READER_H

#include "DbcDescriptor.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct DbcDocument
{
    std::uint32_t recordCount = 0;
    std::uint32_t fieldCount = 0;
    std::uint32_t recordSize = 0;
    std::uint32_t stringBlockSize = 0;
    std::vector<std::uint32_t> words;
    std::vector<std::uint8_t> strings;
};

struct DbcReadResult
{
    bool valid = false;
    std::string error;
    DbcDocument document;
};

class DbcReader
{
public:
    static DbcReadResult ReadBaseline(std::filesystem::path const& directory, DbcDescriptor const& descriptor);
    static DbcReadResult Read(std::filesystem::path const& path, DbcDescriptor const& descriptor);
    static DbcReadResult Parse(std::vector<std::uint8_t> const& bytes, DbcDescriptor const& descriptor);
    // Serialization is internal and used for lossless tests. It never writes the baseline.
    static std::vector<std::uint8_t> Serialize(DbcDocument const& document);
};

#endif
