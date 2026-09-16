#ifndef CONTENT_SERVER_TABLE_DESCRIPTOR_H
#define CONTENT_SERVER_TABLE_DESCRIPTOR_H

#include <cstdint>
#include <string>
#include <vector>

enum class ServerFieldType { String, UInt8, Int32 };
struct ServerFieldDescriptor
{
    char const* name;
    ServerFieldType type;
    bool required;
    std::int64_t minimum;
    std::int64_t maximum;
};
struct ServerSqlColumnDescriptor
{
    char const* name;
    char const* columnType;
    bool nullable;
    // Null for numeric columns; textual columns require this deployed collation.
    char const* collation = nullptr;
};
struct ServerTableDescriptor
{
    char const* table;
    std::uint32_t version;
    std::vector<ServerFieldDescriptor> fields;
    std::vector<ServerSqlColumnDescriptor> columns;
};

// Phase 3 supports this deliberately small subset of the deployed AzerothCore
// item_template schema. Client parity columns come from the same Item row plan.
ServerTableDescriptor const* FindServerTableDescriptor(std::string const& table);

#endif
