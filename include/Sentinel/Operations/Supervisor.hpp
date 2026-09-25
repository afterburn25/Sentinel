#pragma once
#include <string>
#include <vector>

namespace sentinel::operations {

enum class ApprovalStatus { Pending, Approved, Rejected };

struct ApprovalRequest {
    std::string id;
    std::string action;
    std::string actionHash;
    std::string requestedBy;
    ApprovalStatus status{ApprovalStatus::Pending};
    std::string reviewedBy;
    std::string note;
};

std::string ComputeApprovalActionHash(const std::string& canonicalAction);
ApprovalRequest CreateApprovalRequest(const std::string& action,const std::string& requestedBy);
void Approve(ApprovalRequest& request,const std::string& reviewer,const std::string& note={});
void Reject(ApprovalRequest& request,const std::string& reviewer,const std::string& note={});

}
