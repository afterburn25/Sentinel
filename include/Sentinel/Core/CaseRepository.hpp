#pragma once
#include "Sentinel/Core/Case.hpp"
#include "Sentinel/Security/Crypto.hpp"
#include "Sentinel/Security/KeyManager.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"

namespace sentinel {

class SqliteCaseRepository final : public ICaseRepository {
public:
    SqliteCaseRepository(
        SqliteDatabase& db,
        IKeyManager* keys = nullptr,
        IAeadCipher* cipher = nullptr)
        : db_(db), keys_(keys), cipher_(cipher) {}

    void Insert(const CaseRecord&) override;
    std::optional<CaseRecord> Get(const CaseId&) override;
    std::vector<CaseRecord> List() override;
    void Update(const CaseRecord&) override;

private:
    SqliteDatabase& db_;
    IKeyManager* keys_{};
    IAeadCipher* cipher_{};
};

}
