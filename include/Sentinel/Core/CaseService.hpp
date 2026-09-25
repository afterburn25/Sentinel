#pragma once
#include "Sentinel/Core/Case.hpp"
namespace sentinel {
class CaseService{public:explicit CaseService(ICaseRepository& repo):repo_(repo){}CaseRecord CreateCase(const CreateCaseRequest&);std::optional<CaseRecord> GetCase(const CaseId& id){return repo_.Get(id);}std::vector<CaseRecord> ListCases(){return repo_.List();}void CloseCase(const CaseId& id);private:ICaseRepository& repo_;};
}
