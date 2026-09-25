#pragma once

#include "Sentinel/Core/Types.hpp"
#include "Sentinel/Security/Crypto.hpp"
#include "Sentinel/Security/SecretProtector.hpp"
#include "Sentinel/Security/SecureBuffer.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"

#include <filesystem>

namespace sentinel {

class IKeyManager {
public:
    virtual ~IKeyManager() = default;
    virtual void Initialize() = 0;
    virtual void CreateCaseKey(const CaseId& caseId) = 0;
    [[nodiscard]] virtual bool HasCaseKey(const CaseId& caseId) const = 0;
    [[nodiscard]] virtual SecureBuffer GetCaseKey(const CaseId& caseId) = 0;
};

class KeyManager final : public IKeyManager {
public:
    KeyManager(
        std::filesystem::path masterKeyPath,
        SqliteDatabase& db,
        ISecretProtector& protector,
        ISecureRandom& random,
        IAeadCipher& cipher);

    void Initialize() override;
    void CreateCaseKey(const CaseId& caseId) override;
    [[nodiscard]] bool HasCaseKey(const CaseId& caseId) const override;
    [[nodiscard]] SecureBuffer GetCaseKey(const CaseId& caseId) override;

private:
    [[nodiscard]] SecureBuffer LoadMasterKey() const;
    [[nodiscard]] static std::vector<std::byte> CaseKeyAad(const CaseId& caseId, uint32_t version);

    std::filesystem::path masterKeyPath_;
    SqliteDatabase& db_;
    ISecretProtector& protector_;
    ISecureRandom& random_;
    IAeadCipher& cipher_;
};

}
