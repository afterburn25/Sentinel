#pragma once

#include "Sentinel/Audit/AuditService.hpp"
#include "Sentinel/Evidence/SevContainer.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace sentinel {

struct EvidenceImportRequest {
    CaseId caseId;
    std::filesystem::path sourcePath;
    uint8_t type{};
    UserId actor;
};

struct EvidenceImportResult {
    EvidenceId id;
    Hash256 originalHash;
    Hash256 containerHash;
    std::filesystem::path storedPath;
};

struct EvidenceSummary {
    EvidenceId id;
    std::string originalFilename;
    Hash256 originalHash;
    Hash256 containerHash;
    uint64_t originalSize{};
    uint64_t storedSize{};
    std::filesystem::path storedPath;
    Timestamp importedAt{};
};

class EvidenceService {
public:
    EvidenceService(
        std::filesystem::path root,
        SqliteDatabase& db,
        ISecureRandom& random,
        IHashService& hash,
        IAeadCipher& cipher,
        AuditService& audit)
        : root_(std::move(root)),
          db_(db),
          random_(random),
          hash_(hash),
          cipher_(cipher),
          audit_(audit) {}

    EvidenceImportResult Import(
        const EvidenceImportRequest&,
        std::span<const std::byte> caseKey);

    std::vector<EvidenceSummary> ListForCase(
        const CaseId&,
        std::span<const std::byte> caseKey);

private:
    std::filesystem::path root_;
    SqliteDatabase& db_;
    ISecureRandom& random_;
    IHashService& hash_;
    IAeadCipher& cipher_;
    AuditService& audit_;
};

}
