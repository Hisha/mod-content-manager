#ifndef CONTENT_DBC_DESCRIPTOR_H
#define CONTENT_DBC_DESCRIPTOR_H

#include <cstdint>
#include <string>
#include <vector>

enum class DbcFieldType { UInt32, Int32, Float32, StringOffset };

struct DbcFieldDescriptor
{
    char const* name;
    DbcFieldType type;
    bool primaryKey = false;
    char const* referenceTable = nullptr;
    char const* allocationNamespace = nullptr;
};

struct DbcDescriptor
{
    std::uint32_t clientBuild;
    char const* tableName;
    std::uint32_t version;
    char const* clientPath;
    char const* serverFile;
    std::vector<DbcFieldDescriptor> fields;
};

// Registered layouts are compiled into Content Manager, never supplied by an EPF.
DbcDescriptor const* FindDbcDescriptor(std::uint32_t clientBuild, std::string const& tableName);
bool IsKnownDbcTable(std::string const& tableName);

// Build-12340 client-only world-map tables, in deterministic composition order.
// These have no worldserver counterpart; a package contributes rows and the
// composed tables are written into the client MPQ only.
std::vector<std::string> const& WorldMapDbcTables();

#endif
