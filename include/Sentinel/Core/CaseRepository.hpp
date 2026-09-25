#pragma once
#include "Sentinel/Core/Case.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"
namespace sentinel {
class SqliteCaseRepository final:public ICaseRepository{public:explicit SqliteCaseRepository(SqliteDatabase& db):db_(db){}void Insert(const CaseRecord&) override;std::optional<CaseRecord> Get(const CaseId&) override;std::vector<CaseRecord> List() override;void Update(const CaseRecord&) override;private:SqliteDatabase& db_;};
}
