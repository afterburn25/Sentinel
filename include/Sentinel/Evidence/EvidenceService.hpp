#pragma once
#include "Sentinel/Audit/AuditService.hpp"
#include "Sentinel/Evidence/SevContainer.hpp"
#include <filesystem>
namespace sentinel {
struct EvidenceImportRequest{CaseId caseId;std::filesystem::path sourcePath;uint8_t type{};UserId actor;};
struct EvidenceImportResult{EvidenceId id;Hash256 originalHash;Hash256 containerHash;std::filesystem::path storedPath;};
class EvidenceService{public:EvidenceService(std::filesystem::path root,ISecureRandom& random,IHashService& hash,IAeadCipher& cipher,AuditService& audit):root_(std::move(root)),random_(random),hash_(hash),cipher_(cipher),audit_(audit){}EvidenceImportResult Import(const EvidenceImportRequest&,std::span<const std::byte> caseKey);private:std::filesystem::path root_;ISecureRandom& random_;IHashService& hash_;IAeadCipher& cipher_;AuditService& audit_;};
}
