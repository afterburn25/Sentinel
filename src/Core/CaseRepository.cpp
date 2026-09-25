#include "Sentinel/Core/CaseRepository.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace sentinel {
namespace {

struct EncryptedField {
    std::vector<std::byte> cipher;
    std::array<std::byte, 12> nonce{};
    std::array<std::byte, 16> tag{};
};

std::vector<std::byte> FieldAad(const CaseId& caseId, std::string_view field)
{
    std::vector<std::byte> aad;
    const auto& id = caseId.Bytes();
    aad.insert(aad.end(), id.begin(), id.end());
    aad.insert(
        aad.end(),
        reinterpret_cast<const std::byte*>(field.data()),
        reinterpret_cast<const std::byte*>(field.data() + field.size()));
    return aad;
}

EncryptedField EncryptText(
    const CaseId& caseId,
    std::string_view field,
    std::string_view value,
    std::span<const std::byte> key,
    IAeadCipher& cipher)
{
    const auto plain = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(value.data()),
        value.size());
    const auto aad = FieldAad(caseId, field);
    auto encrypted = cipher.Encrypt(plain, key, aad);
    return {std::move(encrypted.ciphertext), encrypted.nonce, encrypted.tag};
}

std::string DecryptText(
    const CaseId& caseId,
    std::string_view field,
    const void* cipherData,
    int cipherSize,
    const void* nonceData,
    int nonceSize,
    const void* tagData,
    int tagSize,
    std::span<const std::byte> key,
    IAeadCipher& cipher)
{
    if (!cipherData || !nonceData || !tagData)
        return {};
    if (nonceSize != 12 || tagSize != 16 || cipherSize < 0)
        throw std::runtime_error("invalid encrypted case field");

    const auto ciphertext = std::span<const std::byte>(
        static_cast<const std::byte*>(cipherData),
        static_cast<size_t>(cipherSize));
    const auto nonce = std::span<const std::byte>(
        static_cast<const std::byte*>(nonceData),
        static_cast<size_t>(nonceSize));
    const auto tag = std::span<const std::byte>(
        static_cast<const std::byte*>(tagData),
        static_cast<size_t>(tagSize));
    const auto aad = FieldAad(caseId, field);

    const auto plain = cipher.Decrypt(ciphertext, key, nonce, tag, aad);
    return std::string(
        reinterpret_cast<const char*>(plain.data()),
        plain.size());
}

CaseRecord ReadCase(
    sqlite3_stmt* stmt,
    IKeyManager* keys,
    IAeadCipher* cipher)
{
    CaseRecord r;
    const auto* idText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const auto parsedId = CaseId::Parse(idText ? idText : "");
    if (!parsedId) throw std::runtime_error("invalid case id in database");
    r.id = *parsedId;

    const auto* legacyNumber = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const auto* legacyTitle = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const auto* legacyDescription = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

    r.status = static_cast<CaseStatus>(sqlite3_column_int(stmt, 4));
    const auto* actorText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    const auto parsedActor = UserId::Parse(actorText ? actorText : "");
    if (!parsedActor) throw std::runtime_error("invalid case creator id in database");
    r.createdBy = *parsedActor;

    const bool encrypted =
        sqlite3_column_type(stmt, 8) != SQLITE_NULL &&
        sqlite3_column_type(stmt, 11) != SQLITE_NULL &&
        sqlite3_column_type(stmt, 14) != SQLITE_NULL;

    if (encrypted) {
        if (!keys || !cipher)
            throw std::runtime_error("encrypted case requires key manager and cipher");

        auto key = keys->GetCaseKey(r.id);
        r.caseNumber = DecryptText(
            r.id, "case_number",
            sqlite3_column_blob(stmt, 8), sqlite3_column_bytes(stmt, 8),
            sqlite3_column_blob(stmt, 9), sqlite3_column_bytes(stmt, 9),
            sqlite3_column_blob(stmt, 10), sqlite3_column_bytes(stmt, 10),
            key.Span(), *cipher);
        r.title = DecryptText(
            r.id, "title",
            sqlite3_column_blob(stmt, 11), sqlite3_column_bytes(stmt, 11),
            sqlite3_column_blob(stmt, 12), sqlite3_column_bytes(stmt, 12),
            sqlite3_column_blob(stmt, 13), sqlite3_column_bytes(stmt, 13),
            key.Span(), *cipher);
        r.description = DecryptText(
            r.id, "description",
            sqlite3_column_blob(stmt, 14), sqlite3_column_bytes(stmt, 14),
            sqlite3_column_blob(stmt, 15), sqlite3_column_bytes(stmt, 15),
            sqlite3_column_blob(stmt, 16), sqlite3_column_bytes(stmt, 16),
            key.Span(), *cipher);
    }
    else {
        r.caseNumber = legacyNumber ? legacyNumber : "";
        r.title = legacyTitle ? legacyTitle : "";
        r.description = legacyDescription ? legacyDescription : "";
    }

    return r;
}

constexpr const char* kSelectCase =
    "SELECT id,case_number,title,description,status,created_by,created_at,modified_at,"
    "case_number_cipher,case_number_nonce,case_number_tag,"
    "title_cipher,title_nonce,title_tag,"
    "description_cipher,description_nonce,description_tag "
    "FROM cases ";

}

void SqliteCaseRepository::Insert(const CaseRecord& r)
{
    sqlite3_stmt* stmt{};
    const char* sql =
        "INSERT INTO cases("
        "id,case_number,title,description,status,created_by,created_at,modified_at"
        ") VALUES(?1,?2,?3,?4,?5,?6,?7,?8)";

    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare case insert failed");

    const auto id = r.id.ToString();
    const auto user = r.createdBy.ToString();
    const auto created = ToIso8601Utc(r.createdAt);
    const auto modified = ToIso8601Utc(r.modifiedAt);

    const bool secureRepository = keys_ && cipher_;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, secureRepository ? "" : r.caseNumber.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, secureRepository ? "" : r.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, secureRepository ? "" : r.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, static_cast<int>(r.status));
    sqlite3_bind_text(stmt, 6, user.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, created.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, modified.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const std::string error = sqlite3_errmsg(db_.Handle());
        sqlite3_finalize(stmt);
        throw std::runtime_error("insert case failed: " + error);
    }
    sqlite3_finalize(stmt);
}

std::optional<CaseRecord> SqliteCaseRepository::Get(const CaseId& id)
{
    sqlite3_stmt* stmt{};
    const std::string sql = std::string(kSelectCase) + "WHERE id=?1";
    if (sqlite3_prepare_v2(db_.Handle(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare case query failed");

    const auto idText = id.ToString();
    sqlite3_bind_text(stmt, 1, idText.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<CaseRecord> result;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = ReadCase(stmt, keys_, cipher_);

    sqlite3_finalize(stmt);
    return result;
}

std::vector<CaseRecord> SqliteCaseRepository::List()
{
    sqlite3_stmt* stmt{};
    const std::string sql = std::string(kSelectCase) + "ORDER BY modified_at DESC";
    if (sqlite3_prepare_v2(db_.Handle(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare case list failed");

    std::vector<CaseRecord> result;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        result.push_back(ReadCase(stmt, keys_, cipher_));

    sqlite3_finalize(stmt);
    return result;
}

void SqliteCaseRepository::Update(const CaseRecord& r)
{
    if (!keys_ || !cipher_ || !keys_->HasCaseKey(r.id))
        throw std::runtime_error("secure case update requires an initialized case key");

    auto key = keys_->GetCaseKey(r.id);
    auto number = EncryptText(r.id, "case_number", r.caseNumber, key.Span(), *cipher_);
    auto title = EncryptText(r.id, "title", r.title, key.Span(), *cipher_);
    auto description = EncryptText(r.id, "description", r.description, key.Span(), *cipher_);

    sqlite3_stmt* stmt{};
    const char* sql =
        "UPDATE cases SET "
        "case_number='',title='',description='',"
        "case_number_cipher=?2,case_number_nonce=?3,case_number_tag=?4,"
        "title_cipher=?5,title_nonce=?6,title_tag=?7,"
        "description_cipher=?8,description_nonce=?9,description_tag=?10,"
        "status=?11,modified_at=?12 "
        "WHERE id=?1";

    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare secure case update failed");

    const auto id = r.id.ToString();
    const auto modified = ToIso8601Utc(r.modifiedAt);

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, number.cipher.data(), static_cast<int>(number.cipher.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 3, number.nonce.data(), static_cast<int>(number.nonce.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 4, number.tag.data(), static_cast<int>(number.tag.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 5, title.cipher.data(), static_cast<int>(title.cipher.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 6, title.nonce.data(), static_cast<int>(title.nonce.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 7, title.tag.data(), static_cast<int>(title.tag.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 8, description.cipher.data(), static_cast<int>(description.cipher.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 9, description.nonce.data(), static_cast<int>(description.nonce.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 10, description.tag.data(), static_cast<int>(description.tag.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 11, static_cast<int>(r.status));
    sqlite3_bind_text(stmt, 12, modified.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const std::string error = sqlite3_errmsg(db_.Handle());
        sqlite3_finalize(stmt);
        throw std::runtime_error("secure case update failed: " + error);
    }

    sqlite3_finalize(stmt);
}

}
