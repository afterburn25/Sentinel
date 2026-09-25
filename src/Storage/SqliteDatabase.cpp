#include "Sentinel/Storage/SqliteDatabase.hpp"
#include <stdexcept>
namespace sentinel {
SqliteDatabase::~SqliteDatabase(){Close();}
void SqliteDatabase::Open(const std::filesystem::path& path){Close();if(sqlite3_open_v2(path.string().c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){std::string e=db_?sqlite3_errmsg(db_):"sqlite open failed";Close();throw std::runtime_error(e);}Execute("PRAGMA foreign_keys=ON;PRAGMA journal_mode=WAL;PRAGMA synchronous=FULL;PRAGMA secure_delete=ON;");}
void SqliteDatabase::Close(){if(db_){sqlite3_close(db_);db_=nullptr;}}
void SqliteDatabase::Execute(std::string_view sql){char* err=nullptr;if(sqlite3_exec(db_,std::string(sql).c_str(),nullptr,nullptr,&err)!=SQLITE_OK){std::string e=err?err:"sqlite exec failed";sqlite3_free(err);throw std::runtime_error(e);}}
void SqliteDatabase::Begin(){Execute("BEGIN IMMEDIATE;");}void SqliteDatabase::Commit(){Execute("COMMIT;");}void SqliteDatabase::Rollback() noexcept{try{Execute("ROLLBACK;");}catch(...){}}
}
