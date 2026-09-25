#include "Sentinel/Evidence/EvidenceService.hpp"
#include <filesystem>
namespace sentinel {
EvidenceImportResult EvidenceService::Import(const EvidenceImportRequest&r,std::span<const std::byte> caseKey){if(!std::filesystem::is_regular_file(r.sourcePath))throw std::runtime_error("evidence source is not a regular file");auto id=EvidenceId::Random();auto caseDir=root_/r.caseId.ToString();auto staging=root_.parent_path()/"staging"/"evidence";std::filesystem::create_directories(caseDir);std::filesystem::create_directories(staging);auto tmp=staging/(id.ToString()+".tmp");Hash256 original{};SevContainer::EncryptFile(r.sourcePath,tmp,id,r.caseId,caseKey,cipher_,hash_,original);auto container=hash_.Sha256File(tmp);auto final=caseDir/(original.ToHex()+".sev");std::filesystem::rename(tmp,final);audit_.Append({r.actor,AuditAction::EvidenceImported,"evidence",id.ToString(),{}});return {id,original,container,final};}
}
