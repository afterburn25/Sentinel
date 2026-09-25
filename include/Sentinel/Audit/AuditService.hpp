#pragma once
#include "Sentinel/Core/Types.hpp"
#include "Sentinel/Security/Crypto.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include <optional>
#include <string>
#include <vector>
namespace sentinel {
enum class AuditAction:uint32_t{ApplicationInitialized=1,CaseCreated=100,CaseOpened=101,CaseUpdated=102,CaseClosed=103,CaseReopened=104,EvidenceImportStarted=200,EvidenceImported=201,EvidenceVerified=203,EvidenceIntegrityFailure=206,IntegrityCheckStarted=400,IntegrityCheckCompleted=401,IntegrityCheckFailed=402,RecoveryStarted=500,RecoveryCompleted=501,RecoveryFailed=502,ApplicationShutdown=900};
struct AuditEvent{UserId actor;AuditAction action;std::string targetType;std::optional<std::string> targetId;std::vector<std::byte> metadata;};
class AuditService{public:AuditService(SqliteDatabase& db,IHashService& hash):db_(db),hash_(hash){}AuditId Append(const AuditEvent&);bool VerifyChain();Hash256 GetCurrentHead();private:SqliteDatabase& db_;IHashService& hash_;};
}
