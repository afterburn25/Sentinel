#pragma once
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include <filesystem>
namespace sentinel {
class MigrationService{public:explicit MigrationService(SqliteDatabase& db):db_(db){} void ApplyDirectory(const std::filesystem::path&);[[nodiscard]] int CurrentVersion() const;private:SqliteDatabase& db_;};
}
