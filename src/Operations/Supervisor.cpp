#include "Sentinel/Operations/Supervisor.hpp"
#include "Sentinel/Security/Crypto.hpp"
#include <span>

namespace sentinel::operations {

std::string ComputeApprovalActionHash(const std::string& canonicalAction) {
    sentinel::WindowsHashService hash;
    auto chars=std::span<const char>(canonicalAction.data(),canonicalAction.size());
    auto bytes=std::as_bytes(chars);
    return hash.Sha256(bytes).ToHex();
}

ApprovalRequest CreateApprovalRequest(const std::string& action,const std::string& requestedBy) {
    ApprovalRequest r;
    const auto seed=action+"|"+requestedBy;
    r.id="approval-"+ComputeApprovalActionHash(seed).substr(0,24);
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
