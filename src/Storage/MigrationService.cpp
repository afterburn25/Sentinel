#include "Sentinel/Storage/MigrationService.hpp"
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>
namespace sentinel {
int MigrationService::CurrentVersion() const{sqlite3_stmt* s{};if(sqlite3_prepare_v2(db_.Handle(),"SELECT COALESCE(MAX(version),0) FROM schema_migrations",-1,&s,nullptr)!=SQLITE_OK)return 0;int v=0;if(sqlite3_step(s)==SQLITE_ROW)v=sqlite3_column_int(s,0);sqlite3_finalize(s);return v;}
void MigrationService::ApplyDirectory(const std::filesystem::path& dir){db_.Execute("CREATE TABLE IF NOT EXISTS schema_migrations(version INTEGER PRIMARY KEY,name TEXT NOT NULL,applied_at TEXT NOT NULL,checksum TEXT NOT NULL);");std::vector<std::filesystem::path> files;for(auto& e:std::filesystem::directory_iterator(dir))if(e.is_regular_file()&&e.path().extension()==".sql")files.push_back(e.path());std::sort(files.begin(),files.end());int current=CurrentVersion();std::regex r("^(\\d+)_");for(auto& p:files){std::smatch m;auto name=p.filename().string();if(!std::regex_search(name,m,r))continue;int version=std::stoi(m[1]);if(version<=current)continue;std::ifstream in(p);std::stringstream ss;ss<<in.rdbuf();SqliteTransaction tx(db_);db_.Execute(ss.str());sqlite3_stmt* st{};sqlite3_prepare_v2(db_.Handle(),"INSERT INTO schema_migrations(version,name,applied_at,checksum) VALUES(?1,?2,strftime('%Y-%m-%dT%H:%M:%fZ','now'),'bootstrap')",-1,&st,nullptr);sqlite3_bind_int(st,1,version);sqlite3_bind_text(st,2,name.c_str(),-1,SQLITE_TRANSIENT);if(sqlite3_step(st)!=SQLITE_DONE){sqlite3_finalize(st);throw std::runtime_error("migration history insert failed");}sqlite3_finalize(st);tx.Commit();current=version;}}
}
