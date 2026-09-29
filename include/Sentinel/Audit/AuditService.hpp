#pragma once
#include "Sentinel/Core/Types.hpp"
#include "Sentinel/Security/Crypto.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include <optional>
#include <string>
#include <vector>
namespace sentinel {
enum class AuditAction:uint32_t{ApplicationInitialized=1,CaseCreated=100,CaseOpened=101,CaseUpdated=102,CaseClosed=103,CaseReopened=104,EvidenceImportStarted=200,EvidenceImported=201,EvidenceVerified=203,EvidenceIntegrityFailure=206,IntegrityCheckStarted=400,IntegrityCheckCompleted=401,IntegrityCheckFailed=402,RecoveryStarted=500,RecoveryCompleted=501,RecoveryFailed=502,DeploymentPrepared=600,DeploymentActivated=601,DeploymentRolledBack=602,DeploymentLockChanged=603,DeploymentManifestExported=604,FoundationEvaluated=605,FoundationApproved=606,FoundationActivated=607,FoundationRolledBack=608,PersonaLoraEvaluated=609,PersonaLoraApproved=610,PersonaLoraActivated=611,PersonaLoraRolledBack=612,SubjectCreated=700,SubjectUpdated=701,SubjectDeleted=702,IdentityLeadAdded=710,IdentityLeadVerified=711,IdentityLeadRejected=712,SubjectIdentityConfirmed=713,IdentityResearchQueued=714,IdentityResearchCompleted=715,IdentityResearchPromoted=716,IdentityResearchRejected=717,IdentityResearchPreserved=718,IdentityResearchRequestExported=719,SupervisorTakeoverActivated=720,SupervisorTakeoverReleased=721,SupervisorApprovalRequested=722,SupervisorApprovalApproved=723,SupervisorApprovalRejected=724,IdentityResearchPortalOpened=725,ApplicationShutdown=900};
struct AuditEvent{UserId actor;AuditAction action;std::string targetType;std::optional<std::string> targetId;std::vector<std::byte> metadata;};
class AuditService{public:AuditService(SqliteDatabase& db,IHashService& hash):db_(db),hash_(hash){}AuditId Append(const AuditEvent&);bool VerifyChain();Hash256 GetCurrentHead();private:SqliteDatabase& db_;IHashService& hash_;};
}
