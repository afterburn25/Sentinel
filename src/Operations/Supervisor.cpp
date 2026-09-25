#include "Sentinel/Operations/Supervisor.hpp"
#include <iomanip>
#include <sstream>
#include <functional>

namespace sentinel::operations {
std::string ComputeApprovalActionHash(const std::string& canonicalAction) {
    auto v=std::hash<std::string>{}(canonicalAction);
    std::ostringstream out; out<<std::hex<<std::setw(sizeof(v)*2)<<std::setfill('0')<<v;
    return out.str();
}
ApprovalRequest CreateApprovalRequest(const std::string& action,const std::string& requestedBy) {
    ApprovalRequest r;
    r.id="approval-"+ComputeApprovalActionHash(action+"|"+requestedBy);
    r.action=action;
    r.actionHash=ComputeApprovalActionHash(action);
    r.requestedBy=requestedBy;
    return r;
}
void Approve(ApprovalRequest& r,const std::string& reviewer,const std::string& note) {
    r.status=ApprovalStatus::Approved; r.reviewedBy=reviewer; r.note=note;
}
void Reject(ApprovalRequest& r,const std::string& reviewer,const std::string& note) {
    r.status=ApprovalStatus::Rejected; r.reviewedBy=reviewer; r.note=note;
}
}
