#pragma once
#include "Sentinel/Core/Types.hpp"
#include <optional>
#include <string>
#include <vector>
namespace sentinel {
enum class CaseStatus:uint8_t{Open=1,Suspended=2,Closed=3,Archived=4};
struct CaseRecord{CaseId id;std::string caseNumber;std::string title;std::string description;CaseStatus status{CaseStatus::Open};UserId createdBy;Timestamp createdAt;Timestamp modifiedAt;};
struct CreateCaseRequest{std::string caseNumber;std::string title;std::string description;UserId actor;};
class ICaseRepository{public:virtual ~ICaseRepository()=default;virtual void Insert(const CaseRecord&)=0;virtual std::optional<CaseRecord> Get(const CaseId&)=0;virtual std::vector<CaseRecord> List()=0;virtual void Update(const CaseRecord&)=0;};
}
