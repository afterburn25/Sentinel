#include "Sentinel/Evidence/EvidenceService.hpp"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace sentinel {
namespace {

std::vector<std::byte> FilenameAad(const CaseId& caseId, const EvidenceId& evidenceId)
{
    std::vector<std::byte> aad;
    const auto& c = caseId.Bytes();
    const auto& e = evidenceId.Bytes();
    aad.insert(aad.end(), c.begin(), c.end());
    aad.insert(aad.end(), e.begin(), e.end());
    constexpr std::string_view label = "evidence_filename";
    aad.insert(
        aad.end(),
        reinterpret_cast<const std::byte*>(label.data()),
        reinterpret_cast<const std::byte*>(label.data() + label.size()));
    return aad;
}

std::string DecryptFilename(
    const CaseId& caseId,
    const EvidenceId& evidenceId,
    sqlite3_stmt* stmt,
    int cipherColumn,
    int nonceColumn,
    int tagColumn,
    std::span<const std::byte> caseKey,
    IAeadCipher& cipher)
{
    if (sqlite3_column_type(stmt, cipherColumn) == SQLITE_NULL)
        return {};

    const auto* cipherPtr =
        static_cast<const std::byte*>(sqlite3_column_blob(stmt, cipherColumn));
    const auto cipherLen = sqlite3_column_bytes(stmt, cipherColumn);
    const auto* noncePtr =
        static_cast<const std::byte*>(sqlite3_column_blob(stmt, nonceColumn));
    const auto nonceLen = sqlite3_column_bytes(stmt, nonceColumn);
    const auto* tagPtr =
        static_cast<const std::byte*>(sqlite3_column_blob(stmt, tagColumn));
    const auto tagLen = sqlite3_column_bytes(stmt, tagColumn);

    if (!cipherPtr || !noncePtr || !tagPtr || nonceLen != 12 || tagLen != 16)
        throw std::runtime_error("invalid encrypted evidence filename");

    const auto aad = FilenameAad(caseId, evidenceId);
    const auto plain = cipher.Decrypt(
        {cipherPtr, static_cast<size_t>(cipherLen)},
        caseKey,
        {noncePtr, static_cast<size_t>(nonceLen)},
        {tagPtr, static_cast<size_t>(tagLen)},
        aad);

    return std::string(
        reinterpret_cast<const char*>(plain.data()),
        plain.size());
}

std::vector<std::byte> VerificationMetadata(std::string_view text)
{
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        reinterpret_cast<const std::byte*>(text.data() + text.size())
    };
}

}

EvidenceImportResult EvidenceService::Import(
    const EvidenceImportRequest& request,
    std::span<const std::byte> caseKey)
{
    if (!std::filesystem::is_regular_file(request.sourcePath))
        throw std::runtime_error("evidence source is not a regular file");

    const auto evidenceId = EvidenceId::Random();
    const auto caseDir = root_ / request.caseId.ToString();
    const auto staging = root_.parent_path() / "staging" / "evidence";
    std::filesystem::create_directories(caseDir);
    std::filesystem::create_directories(staging);

    const auto tmp = staging / (evidenceId.ToString() + ".tmp");

    Hash256 originalHash{};
    SevContainer::EncryptFile(
        request.sourcePath,
        tmp,
        evidenceId,
        request.caseId,
        caseKey,
        cipher_,
        hash_,
        originalHash);

    if (!SevContainer::BasicValidate(tmp)) {
        std::filesystem::remove(tmp);
        throw std::runtime_error("new evidence container failed structural verification");
    }

    const auto containerHash = hash_.Sha256File(tmp);
    const auto finalName =
        originalHash.ToHex() + "-" + evidenceId.ToString() + ".sev";
    const auto finalPath = caseDir / finalName;
    const auto relativePath =
        std::filesystem::relative(finalPath, root_).generic_string();

    const auto sourceSize = std::filesystem::file_size(request.sourcePath);
    const auto storedSize = std::filesystem::file_size(tmp);
    const auto importedAt = NowUtc();

    const auto filenameUtf8 = request.sourcePath.filename().u8string();
    const std::string filename(
        reinterpret_cast<const char*>(filenameUtf8.data()),
        filenameUtf8.size());
    const auto filenamePlain = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(filename.data()),
        filename.size());
    const auto filenameAad = FilenameAad(request.caseId, evidenceId);
    const auto encryptedFilename =
        cipher_.Encrypt(filenamePlain, caseKey, filenameAad);

    bool moved = false;
    try {
        SqliteTransaction tx(db_);

        std::filesystem::rename(tmp, finalPath);
        moved = true;

        sqlite3_stmt* stmt{};
        const char* sql =
            "INSERT INTO evidence("
            "id,case_id,evidence_type,original_filename,media_type,"
            "original_sha256,container_sha256,original_size,stored_size,"
            "storage_relative_path,parent_evidence_id,acquisition_time,"
            "imported_at,imported_by,status,"
            "filename_cipher,filename_nonce,filename_tag"
            ") VALUES("
            "?1,?2,?3,?4,NULL,?5,?6,?7,?8,?9,NULL,NULL,?10,?11,1,?12,?13,?14"
            ")";

        if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK)
            throw std::runtime_error("prepare evidence insert failed");

        const auto idText = evidenceId.ToString();
        const auto caseText = request.caseId.ToString();
        const auto actorText = request.actor.ToString();
        const auto importedText = ToIso8601Utc(importedAt);
        const auto originalText = originalHash.ToHex();
        const auto containerText = containerHash.ToHex();
        static const char emptyBlob = 0;

        sqlite3_bind_text(stmt, 1, idText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, caseText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, request.type);
        sqlite3_bind_blob(stmt, 4, &emptyBlob, 0, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, originalText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, containerText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(sourceSize));
        sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(storedSize));
        sqlite3_bind_text(stmt, 9, relativePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 10, importedText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 11, actorText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(
            stmt, 12,
            encryptedFilename.ciphertext.data(),
            static_cast<int>(encryptedFilename.ciphertext.size()),
            SQLITE_TRANSIENT);
        sqlite3_bind_blob(
            stmt, 13,
            encryptedFilename.nonce.data(),
            static_cast<int>(encryptedFilename.nonce.size()),
            SQLITE_TRANSIENT);
        sqlite3_bind_blob(
            stmt, 14,
            encryptedFilename.tag.data(),
            static_cast<int>(encryptedFilename.tag.size()),
            SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            const std::string error = sqlite3_errmsg(db_.Handle());
            sqlite3_finalize(stmt);
            throw std::runtime_error("evidence insert failed: " + error);
        }
        sqlite3_finalize(stmt);

        audit_.Append({
            request.actor,
            AuditAction::EvidenceImported,
            "evidence",
            evidenceId.ToString(),
            {}
        });

        tx.Commit();
    }
    catch (...) {
        if (std::filesystem::exists(tmp))
            std::filesystem::remove(tmp);
        if (moved && std::filesystem::exists(finalPath))
            std::filesystem::remove(finalPath);
        throw;
    }

    return {
        evidenceId,
        originalHash,
        containerHash,
        finalPath
    };
}

std::vector<EvidenceSummary> EvidenceService::ListForCase(
    const CaseId& caseId,
    std::span<const std::byte> caseKey)
{
    sqlite3_stmt* stmt{};
    const char* sql =
        "SELECT id,original_sha256,container_sha256,original_size,stored_size,"
        "storage_relative_path,imported_at,filename_cipher,filename_nonce,filename_tag "
        "FROM evidence WHERE case_id=?1 ORDER BY imported_at DESC";

    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare evidence list failed");

    const auto caseText = caseId.ToString();
    sqlite3_bind_text(stmt, 1, caseText.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<EvidenceSummary> result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* idText =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto id = EvidenceId::Parse(idText ? idText : "");
        if (!id) {
            sqlite3_finalize(stmt);
            throw std::runtime_error("invalid evidence id in database");
        }

        const auto* originalText =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* containerText =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto originalHash = Hash256::FromHex(originalText ? originalText : "");
        const auto containerHash = Hash256::FromHex(containerText ? containerText : "");
        if (!originalHash || !containerHash) {
            sqlite3_finalize(stmt);
            throw std::runtime_error("invalid evidence hash in database");
        }

        EvidenceSummary item;
        item.id = *id;
        item.originalFilename = DecryptFilename(
            caseId, *id, stmt, 7, 8, 9, caseKey, cipher_);
        item.originalHash = *originalHash;
        item.containerHash = *containerHash;
        item.originalSize =
            static_cast<uint64_t>(sqlite3_column_int64(stmt, 3));
        item.storedSize =
            static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));

        const auto* pathText =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        item.storedPath = root_ / (pathText ? pathText : "");
        item.importedAt = NowUtc();
        result.push_back(std::move(item));
    }

    sqlite3_finalize(stmt);
    return result;
}

EvidenceVerificationResult EvidenceService::Verify(
    const EvidenceSummary& evidence,
    std::span<const std::byte> caseKey,
    UserId actor)
{
    EvidenceVerificationResult result;

    auto recordFailure=[&](std::string detail) {
        result.valid=false;
        result.detail=std::move(detail);
        audit_.Append({
            actor,
            AuditAction::EvidenceIntegrityFailure,
            "evidence",
            evidence.id.ToString(),
            VerificationMetadata(result.detail)
        });
        return result;
    };

    try {
        result.structureValid=SevContainer::BasicValidate(evidence.storedPath);
        if(!result.structureValid)
            return recordFailure("SEV container structure validation failed");

        result.observedContainerHash=hash_.Sha256File(evidence.storedPath);
        result.containerHashMatches=
            result.observedContainerHash==evidence.containerHash;
        if(!result.containerHashMatches)
            return recordFailure("container SHA-256 does not match the imported evidence record");

        Hash256 plaintextHash{};
        (void)SevContainer::DecryptFile(
            evidence.storedPath,
            caseKey,
            cipher_,
            hash_,
            &plaintextHash);
        result.authenticated=true;
        result.observedPlaintextHash=plaintextHash;
        result.plaintextHashMatches=
            plaintextHash==evidence.originalHash;
        if(!result.plaintextHashMatches)
            return recordFailure("decrypted plaintext SHA-256 does not match the imported evidence record");

        result.valid=true;
        result.detail="container structure, AES-GCM authentication, container SHA-256 and plaintext SHA-256 verified";
        const std::string metadata=
            result.detail+
            "; container_sha256="+result.observedContainerHash.ToHex()+
            "; plaintext_sha256="+result.observedPlaintextHash.ToHex();
        audit_.Append({
            actor,
            AuditAction::EvidenceVerified,
            "evidence",
            evidence.id.ToString(),
            VerificationMetadata(metadata)
        });
        return result;
    }
    catch(const std::exception& e) {
        return recordFailure(e.what());
    }
}

EvidenceVerificationStatus EvidenceService::LastVerification(
    const EvidenceId& evidenceId) const
{
    EvidenceVerificationStatus status;
    sqlite3_stmt* stmt{};
    const char* sql=
        "SELECT action,timestamp FROM audit_records "
        "WHERE target_type='evidence' AND target_id=?1 AND action IN (?2,?3) "
        "ORDER BY sequence DESC LIMIT 1";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&stmt,nullptr)!=SQLITE_OK)
        return status;

    const auto id=evidenceId.ToString();
    sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,2,(int)AuditAction::EvidenceVerified);
    sqlite3_bind_int(stmt,3,(int)AuditAction::EvidenceIntegrityFailure);

    if(sqlite3_step(stmt)==SQLITE_ROW) {
        const auto action=(AuditAction)sqlite3_column_int(stmt,0);
        const auto* checked=(const char*)sqlite3_column_text(stmt,1);
        status.state=action==AuditAction::EvidenceVerified
            ? EvidenceVerificationState::Verified
            : EvidenceVerificationState::Failed;
        status.checkedUtc=checked?checked:"";
    }
    sqlite3_finalize(stmt);
    return status;
}

}
