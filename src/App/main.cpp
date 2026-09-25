#include "Sentinel/Core/CaseRepository.hpp"
#include "Sentinel/Core/CaseService.hpp"
#include "Sentinel/Storage/MigrationService.hpp"
#include <filesystem>
#include <iostream>
int main(int argc,char**argv){try{std::filesystem::path root=argc>1?argv[1]:std::filesystem::current_path()/"sentinel-data";std::filesystem::create_directories(root);sentinel::SqliteDatabase db;db.Open(root/"sentinel.db");sentinel::MigrationService migrations(db);auto migrationsDir=std::filesystem::current_path()/"migrations";if(std::filesystem::exists(migrationsDir))migrations.ApplyDirectory(migrationsDir);sentinel::SqliteCaseRepository repo(db);sentinel::CaseService cases(repo);std::cout<<"Sentinel 0.2.0 secure-core bootstrap\n";std::cout<<"Database: "<<(root/"sentinel.db").string()<<"\n";std::cout<<"Cases: "<<cases.ListCases().size()<<"\n";return 0;}catch(const std::exception&e){std::cerr<<"Sentinel startup failed: "<<e.what()<<"\n";return 1;}}
