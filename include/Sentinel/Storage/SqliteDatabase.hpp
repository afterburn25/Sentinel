#pragma once
#include <filesystem>
#include <sqlite3.h>
#include <string_view>
namespace sentinel {
class SqliteDatabase{
public:
    SqliteDatabase()=default; ~SqliteDatabase(); SqliteDatabase(const SqliteDatabase&)=delete;SqliteDatabase& operator=(const SqliteDatabase&)=delete;
    void Open(const std::filesystem::path& path); void Close(); void Execute(std::string_view sql); [[nodiscard]] sqlite3* Handle() const noexcept{return db_;}
    void Begin(); void Commit(); void Rollback() noexcept;
private:sqlite3* db_{};
};
class SqliteTransaction{public:explicit SqliteTransaction(SqliteDatabase& db):db_(db){db_.Begin();}~SqliteTransaction(){if(!done_)db_.Rollback();}void Commit(){db_.Commit();done_=true;}private:SqliteDatabase& db_;bool done_{};};
}
