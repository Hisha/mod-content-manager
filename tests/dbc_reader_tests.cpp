#include "DbcDescriptor.h"
#include "DbcReader.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition) throw std::runtime_error(message);
}
void Set32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
void Reject(std::vector<std::uint8_t> const& bytes, DbcDescriptor const& descriptor, char const* reason)
{
    auto result = DbcReader::Parse(bytes, descriptor);
    if (result.valid || result.error.find(reason) == std::string::npos)
        throw std::runtime_error(std::string("Expected rejection containing '") + reason + "', got '" + result.error + "'");
}
}

int main()
{
    try
    {
        auto item = FindDbcDescriptor(12340, "Item");
        Require(item && item->fields.size() == 8 && item->version == 1, "Item descriptor missing/incomplete");
        Require(!FindDbcDescriptor(12341, "Item"), "unsupported build accepted");
        Require(!FindDbcDescriptor(12340, "CurrencyTypes"), "unsupported table accepted");
        DbcDocument fixture;
        fixture.recordCount = 2;
        fixture.fieldCount = 8;
        fixture.recordSize = 32;
        fixture.stringBlockSize = 3;
        fixture.words = {1, 2, 3, 0xffffffffu, 0xfffffffeu, 6, 7, 8,
                         9, 10, 11, 12, 13, 14, 15, 16};
        fixture.strings = {0, 0, 0}; // Preserve unusual but valid string bytes exactly.
        auto bytes = DbcReader::Serialize(fixture);
        auto parsed = DbcReader::Parse(bytes, *item);
        Require(parsed.valid && parsed.document.recordCount == 2, "valid Item fixture failed");
        Require(DbcReader::Serialize(parsed.document) == bytes, "unchanged Item round trip differs");
        auto bad = bytes;
        bad[0] = 'X'; Reject(bad, *item, "magic");
        Reject(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 19), *item, "header");
        bad = bytes; bad.erase(bad.begin() + 25); Reject(bad, *item, "Truncated");
        bad = bytes; Set32(bad, 4, 0xffffffffu); Reject(bad, *item, "256 MiB");
        bad = bytes; Set32(bad, 16, 4); Reject(bad, *item, "Truncated");
        bad = bytes; Set32(bad, 8, 7); Reject(bad, *item, "Field count");
        bad = bytes; Set32(bad, 12, 28); Reject(bad, *item, "Record size");
        DbcDescriptor stringDescriptor = {12340, "StringFixture", 1, "StringFixture.dbc", "StringFixture.dbc",
            {{"Name", DbcFieldType::StringOffset}}};
        DbcDocument stringFixture;
        stringFixture.recordCount = 1; stringFixture.fieldCount = 1;
        stringFixture.recordSize = 4; stringFixture.stringBlockSize = 2;
        stringFixture.words = {1}; stringFixture.strings = {'x', 0};
        auto stringBytes = DbcReader::Serialize(stringFixture);
        Require(DbcReader::Parse(stringBytes, stringDescriptor).valid, "valid string fixture failed");
        bad = stringBytes; Set32(bad, 20, 2); Reject(bad, stringDescriptor, "offset out of range");
        bad = stringBytes; bad.back() = 'y'; Reject(bad, stringDescriptor, "NUL terminator");
        Require(!DbcReader::ReadBaseline({}, *item).valid, "unconfigured baseline accepted");
        Require(!DbcReader::ReadBaseline("missing-dbc-baseline-test-directory", *item).valid,
            "missing baseline accepted");
        auto temp = std::filesystem::temp_directory_path() / "mcm-dbc-reader-test";
        std::filesystem::create_directories(temp);
        auto source = temp / "Item.dbc";
        { std::ofstream out(source, std::ios::binary | std::ios::trunc);
          out.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
        auto baseline = DbcReader::ReadBaseline(temp, *item);
        Require(baseline.valid && DbcReader::Serialize(baseline.document) == bytes,
            "baseline read/round trip failed");
        std::filesystem::remove(source);
        std::filesystem::remove(temp);
        std::cout << "DBC reader tests passed\n";
        return 0;
    }
    catch (std::exception const& e)
    {
        std::cerr << "DBC reader test failed: " << e.what() << '\n';
        return 1;
    }
}


