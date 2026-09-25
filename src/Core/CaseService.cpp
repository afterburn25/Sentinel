#include "Sentinel/Core/CaseService.hpp"
#include <stdexcept>
namespace sentinel {
CaseRecord CaseService::CreateCase(const CreateCaseRequest& req){if(req.caseNumber.empty()||req.title.empty())throw std::invalid_argument("case number and title are required");auto now=NowUtc();CaseRecord r{CaseId::Random(),req.caseNumber,req.title,req.description,CaseStatus::Open,req.actor,now,now};repo_.Insert(r);return r;}
void CaseService::CloseCase(const CaseId&id){auto r=repo_.Get(id);if(!r)throw std::runtime_error("case not found");r->status=CaseStatus::Closed;r->modifiedAt=NowUtc();repo_.Update(*r);}
}
