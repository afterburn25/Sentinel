#include "Sentinel/Security/KeyManager.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace sentinel {
namespace {

constexpr size_t kMasterKeyBytes = 32;
constexpr size_t kCaseKeyBytes = 32;
constexpr uint32_t kCaseKeyVersion = 1;

std::vector<std::byte> ReadAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        throw std::runtime_error("cannot open protected master key");
    const auto end = in.tellg();
    if (end <= 0)
        throw std::runtime_error("protected master key is empty");
    std::vector<std::byte> data(static_cast<size_t>(end));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))
        throw std::runtime_error("cannot read protected master key");
    return data;
}

void WriteAllAtomic(const std::filesystem::path& path, std::span<const std::byte> data)
{
    std::filesystem::create_directories(path.parent_path());
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("cannot create protected master key");
        if (!data.empty())
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out)
            throw std::runtime_error("cannot write protected master key");
    }
    if (std::filesystem::exists(path))
        throw std::runtime_error("refusing to overwrite existing master key");
    std::filesystem::rename(tmp, path);
}

}

KeyManager::KeyManager(
    std::filesystem::path masterKeyPath,
    SqliteDatabase& db,
    ISecretProtector& protector,
    ISecureRandom& random,
    IAeadCipher& cipher)
    : masterKeyPath_(std::move(masterKeyPath)),
      db_(db),
      protector_(protector),
      random_(random),
      cipher_(cipher)
{
}

void KeyManager::Initialize()
{
    if (std::filesystem::exists(masterKeyPath_)) {
        auto master = LoadMasterKey();
        if (master.Span().size() != kMasterKeyBytes)
            throw std::runtime_error("invalid Sentinel master key length");
        return;
    }

    auto master = random_.Bytes(kMasterKeyBytes);
    auto protectedBytes = protector_.Protect(master.Span());
    WriteAllAtomic(masterKeyPath_, protectedBytes);

    auto check = LoadMasterKey();
    if (check.Span().size() != kMasterKeyBytes)
        throw std::runtime_error("master key verification failed");
}

std::vector<std::byte> KeyManager::CaseKeyAad(const CaseId& caseId, uint32_t version)
{
    std::vector<std::byte> aad;
    aad.reserve(16 + sizeof(version));
    const auto& id = caseId.Bytes();
    aad.insert(aad.end(), id.begin(), id.end());
    for (unsigned shift = 0; shift < 32; shift += 8)
        aad.push_back(std::byte((version >> shift) & 0xff));
    return aad;
}

SecureBuffer KeyManager::LoadMasterKey() const
{
    auto protectedBytes = ReadAll(masterKeyPath_);
    return protector_.Unprotect(protectedBytes);
}

bool KeyManager::HasCaseKey(const CaseId& caseId) const
{
    sqlite3_stmt* stmt{};
    const char* sql = "SELECT 1 FROM case_keys WHERE case_id=?1 LIMIT 1";
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare case key existence query failed");

    const auto id = caseId.ToString();
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

void KeyManager::CreateCaseKey(const CaseId& caseId)
{
    if (HasCaseKey(caseId))
        throw std::runtime_error("case key already exists");

    auto master = LoadMasterKey();
    if (master.Span().size() != kMasterKeyBytes)
        throw std::runtime_error("invalid master key length");

    auto dek = random_.Bytes(kCaseKeyBytes);
    const auto aad = CaseKeyAad(caseId, kCaseKeyVersion);
    const auto wrapped = cipher_.Encrypt(dek.Span(), master.Span(), aad);

    sqlite3_stmt* stmt{};
    const char* sql =
        "INSERT INTO case_keys(case_id,wrapped_key,nonce,tag,key_version,created_at) "
        "VALUES(?1,?2,?3,?4,?5,?6)";
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare case key insert failed");

    const auto id = caseId.ToString();
    const auto created = ToIso8601Utc(NowUtc());
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, wrapped.ciphertext.data(), static_cast<int>(wrapped.ciphertext.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 3, wrapped.nonce.data(), static_cast<int>(wrapped.nonce.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 4, wrapped.tag.data(), static_cast<int>(wrapped.tag.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, static_cast<int>(kCaseKeyVersion));
    sqlite3_bind_text(stmt, 6, created.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const auto message = std::string("insert case key failed: ") + sqlite3_errmsg(db_.Handle());
        sqlite3_finalize(stmt);
        throw std::runtime_error(message);
    }
    sqlite3_finalize(stmt);
}

SecureBuffer KeyManager::GetCaseKey(const CaseId& caseId)
{
    sqlite3_stmt* stmt{};
    const char* sql = "SELECT wrapped_key,nonce,tag,key_version FROM case_keys WHERE case_id=?1";
    if (sqlite3_prepare_v2(db_.Handle(), sql, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error("prepare case key query failed");

    const auto id = caseId.ToString();
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("case key not found");
    }

    const auto* wrappedPtr = static_cast<const std::byte*>(sqlite3_column_blob(stmt, 0));
    const auto wrappedLen = sqlite3_column_bytes(stmt, 0);
    const auto* noncePtr = static_cast<const std::byte*>(sqlite3_column_blob(stmt, 1));
    const auto nonceLen = sqlite3_column_bytes(stmt, 1);
    const auto* tagPtr = static_cast<const std::byte*>(sqlite3_column_blob(stmt, 2));
    const auto tagLen = sqlite3_column_bytes(stmt, 2);
    const auto version = static_cast<uint32_t>(sqlite3_column_int(stmt, 3));

    if (wrappedLen != static_cast<int>(kCaseKeyBytes) || nonceLen != 12 || tagLen != 16) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("invalid wrapped case key record");
    }

    std::vector<std::byte> wrapped(wrappedPtr, wrappedPtr + wrappedLen);
    std::array<std::byte,12> nonce{};
    std::array<std::byte,16> tag{};
    std::memcpy(nonce.data(), noncePtr, nonce.size());
    std::memcpy(tag.data(), tagPtr, tag.size());
    sqlite3_finalize(stmt);

    auto master = LoadMasterKey();
    const auto aad = CaseKeyAad(caseId, version);
    auto plain = cipher_.Decrypt(wrapped, master.Span(), nonce, tag, aad);
    if (plain.size() != kCaseKeyBytes)
        throw std::runtime_error("invalid decrypted case key length");

    return SecureBuffer(plain);
}

}
