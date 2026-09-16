// Test-only synchronous adapter. Exercises production SQL against disposable MySQL;
// it does not simulate AzerothCore's async worker pool or Field metadata checks.
#pragma once
#include <mysql.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <iostream>
using uint32=std::uint32_t;
struct Field
{
    bool null=false; std::string text;
    bool IsNull() const { return null; }
    template<class T> T Get() const
    {
        if constexpr(std::is_same_v<T,std::string>) return text;
        else if constexpr(std::is_signed_v<T>) return static_cast<T>(std::stoll(text));
        else return static_cast<T>(std::stoull(text));
    }
};
struct Result
{
    std::vector<std::vector<Field>> rows;std::size_t index=0;
    Field* Fetch(){return rows[index].data();}
    bool NextRow(){return ++index<rows.size();}
    std::uint64_t GetRowCount() const{return rows.size();}
};
using QueryResult=std::shared_ptr<Result>;
struct Transaction
{
    std::vector<std::string> sql;
    void Append(std::string const& statement){sql.push_back(statement);}
};
class TestDatabase
{
    MYSQL* connection=nullptr;
public:
    std::function<void()> beforeCommit;
    std::string lastError;
    ~TestDatabase(){if(connection)mysql_close(connection);}
    void Connect(char const* socket)
    {
        connection=mysql_init(nullptr);
        if(!mysql_real_connect(connection,"localhost","root","","phase4_test",0,socket,0))
            throw std::runtime_error(mysql_error(connection));
        Execute("SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci");
    }
    bool Execute(std::string const& sql)
    {
        if(mysql_query(connection,sql.c_str())){lastError=mysql_error(connection);return false;}
        if(auto* result=mysql_store_result(connection))mysql_free_result(result);
        return true;
    }
    void DirectExecute(std::string const& sql){if(!Execute(sql))throw std::runtime_error(lastError);}
    QueryResult Query(std::string const& sql)
    {
        if(mysql_query(connection,sql.c_str())){lastError=mysql_error(connection);std::cerr<<lastError<<'\n';return {};}
        auto* result=mysql_store_result(connection);if(!result)return {};
        auto output=std::make_shared<Result>();
        while(auto row=mysql_fetch_row(result))
        {
            auto lengths=mysql_fetch_lengths(result);std::vector<Field> fields;
            for(unsigned i=0;i<mysql_num_fields(result);++i)fields.push_back({!row[i],row[i]?std::string(row[i],lengths[i]):""});
            output->rows.push_back(std::move(fields));
        }
        mysql_free_result(result);return output->rows.empty()?QueryResult():output;
    }
    std::shared_ptr<Transaction> BeginTransaction(){return std::make_shared<Transaction>();}
    void DirectCommitTransaction(std::shared_ptr<Transaction> tx)
    {
        if(beforeCommit){auto fn=std::move(beforeCommit);beforeCommit={};fn();}
        if(!Execute("START TRANSACTION"))throw std::runtime_error(lastError);
        for(auto const& sql:tx->sql)if(!Execute(sql)){std::cerr<<"Transaction rejected: "<<lastError<<'\n';Execute("ROLLBACK");return;}
        if(!Execute("COMMIT"))throw std::runtime_error(lastError);
    }
};
inline TestDatabase WorldDatabase,CharacterDatabase;
