#include "Sentinel/Audit/AuditService.hpp"
#include "Sentinel/Core/CaseRepository.hpp"
#include "Sentinel/Core/CaseService.hpp"
#include "Sentinel/Evidence/SevContainer.hpp"
#include "Sentinel/Evidence/EvidenceService.hpp"
#include "Sentinel/Security/Crypto.hpp"
#include "Sentinel/Security/KeyManager.hpp"
#include "Sentinel/Security/SecretProtector.hpp"
#include "Sentinel/Storage/MigrationService.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path ResolveMigrations(const char* argv0)
{
    const auto cwd = std::filesystem::current_path() / "migrations";
    if (std::filesystem::exists(cwd)) return cwd;

    const auto exe = std::filesystem::absolute(argv0).parent_path();
    const auto installed = exe.parent_path() / "migrations";
    if (std::filesystem::exists(installed)) return installed;

    throw std::runtime_error("migrations directory not found");
}

struct Runtime {
    std::filesystem::path root;
    sentinel::SqliteDatabase db;
    sentinel::WindowsSecureRandom random;
    sentinel::WindowsHashService hash;
    sentinel::WindowsAesGcmCipher cipher;
    sentinel::WindowsDpapiSecretProtector dpapi;
    sentinel::MigrationService migrations;
    sentinel::KeyManager keys;
    sentinel::SqliteCaseRepository caseRepo;
    sentinel::CaseService cases;
    sentinel::AuditService audit;

    Runtime(std::filesystem::path rootPath, const std::filesystem::path& migrationsPath)
        : root(std::move(rootPath)),
          cipher(random),
          migrations(db),
          keys(root / "keys" / "master.dpapi", db, dpapi, random, cipher),
          caseRepo(db, &keys, &cipher),
          cases(caseRepo),
          audit(db, hash)
    {
        std::filesystem::create_directories(root);
        db.Open(root / "sentinel.db");
        migrations.ApplyDirectory(migrationsPath);
        keys.Initialize();
    }
};

void Usage()
{
    std::cout <<
        "Sentinel 0.2.0\n\n"
        "Usage:\n"
        "  SentinelCli <data-root> init\n"
        "  SentinelCli <data-root> case-create <case-number> <title>\n"
        "  SentinelCli <data-root> case-list\n"
        "  SentinelCli <data-root> evidence-import <case-id> <file>\n"
        "  SentinelCli <data-root> evidence-list <case-id>\n"
        "  SentinelCli <data-root> evidence-verify <case-id> <sev-file>\n"
        "  SentinelCli <data-root> audit-verify\n"
        "  SentinelCli <data-root> self-test\n";
}

}

int main(int argc, char** argv)
{
    try {
        if (argc < 3) {
            Usage();
            return 2;
        }

        const std::filesystem::path root = argv[1];
        const std::string command = argv[2];
        Runtime rt(root, ResolveMigrations(argv[0]));

        if (command == "init") {
            std::cout << "Sentinel secure store initialized\n";
            std::cout << "Database: " << (root / "sentinel.db").string() << "\n";
            std::cout << "Master key: " << (root / "keys" / "master.dpapi").string() << "\n";
            return 0;
        }

        if (command == "case-create") {
            if (argc < 5) {
                Usage();
                return 2;
            }

            sentinel::SqliteTransaction tx(rt.db);
            const auto actor = sentinel::UserId::Random();
            auto record = rt.cases.CreateCase({argv[3], argv[4], "", actor});
            rt.keys.CreateCaseKey(record.id);
            rt.caseRepo.Update(record);
            rt.audit.Append({
                actor,
                sentinel::AuditAction::CaseCreated,
                "case",
                record.id.ToString(),
                {}
            });
            tx.Commit();

            std::cout << "Case created\n";
            std::cout << "ID: " << record.id.ToString() << "\n";
            std::cout << "Number: " << record.caseNumber << "\n";
            std::cout << "Title: " << record.title << "\n";
            return 0;
        }

        if (command == "case-list") {
            const auto cases = rt.cases.ListCases();
            if (cases.empty()) {
                std::cout << "No cases.\n";
                return 0;
            }
            for (const auto& c : cases) {
                std::cout << c.id.ToString() << "  "
                          << c.caseNumber << "  "
                          << c.title << "  status="
                          << static_cast<int>(c.status) << "\n";
            }
            return 0;
        }

        if (command == "evidence-import") {
            if (argc < 5) {
                Usage();
                return 2;
            }
            const auto caseId = sentinel::CaseId::Parse(argv[3]);
            if (!caseId) throw std::runtime_error("invalid case id");
            if (!rt.cases.GetCase(*caseId)) throw std::runtime_error("case not found");

            auto caseKey = rt.keys.GetCaseKey(*caseId);
            sentinel::EvidenceService evidence(
                root / "evidence",
                rt.db,
                rt.random,
                rt.hash,
                rt.cipher,
                rt.audit);

            const auto actor = sentinel::UserId::Random();
            const auto result = evidence.Import(
                {*caseId, std::filesystem::path(argv[4]), 0, actor},
                caseKey.Span());

            std::cout << "Evidence imported\n";
            std::cout << "Evidence ID: " << result.id.ToString() << "\n";
            std::cout << "Original SHA-256: " << result.originalHash.ToHex() << "\n";
            std::cout << "Container SHA-256: " << result.containerHash.ToHex() << "\n";
            std::cout << "Stored: " << result.storedPath.string() << "\n";
            return 0;
        }


        if (command == "evidence-list") {
            if (argc < 4) {
                Usage();
                return 2;
            }
            const auto caseId = sentinel::CaseId::Parse(argv[3]);
            if (!caseId) throw std::runtime_error("invalid case id");
            if (!rt.cases.GetCase(*caseId)) throw std::runtime_error("case not found");

            auto caseKey = rt.keys.GetCaseKey(*caseId);
            sentinel::EvidenceService evidence(
                root / "evidence",
                rt.db,
                rt.random,
                rt.hash,
                rt.cipher,
                rt.audit);

            const auto items = evidence.ListForCase(*caseId, caseKey.Span());
            if (items.empty()) {
                std::cout << "No evidence.\n";
                return 0;
            }

            for (const auto& item : items) {
                std::cout << item.id.ToString() << "  "
                          << item.originalFilename << "  "
                          << item.originalSize << " bytes  "
                          << item.originalHash.ToHex() << "\n";
                std::cout << "  " << item.storedPath.string() << "\n";
            }
            return 0;
        }

        if (command == "evidence-verify") {
            if (argc < 5) {
                Usage();
                return 2;
            }
            const auto caseId = sentinel::CaseId::Parse(argv[3]);
            if (!caseId) throw std::runtime_error("invalid case id");
            auto caseKey = rt.keys.GetCaseKey(*caseId);

            sentinel::Hash256 plaintextHash{};
            const auto plaintext = sentinel::SevContainer::DecryptFile(
                argv[4],
                caseKey.Span(),
                rt.cipher,
                rt.hash,
                &plaintextHash);

            std::cout << "Evidence authentication: VALID\n";
            std::cout << "Plaintext SHA-256: " << plaintextHash.ToHex() << "\n";
            std::cout << "Plaintext bytes: " << plaintext.size() << "\n";
            return 0;
        }


        if (command == "self-test") {
            const auto actor = sentinel::UserId::Random();
            sentinel::CaseRecord record;
            {
                sentinel::SqliteTransaction tx(rt.db);
                record = rt.cases.CreateCase({
                    "SELFTEST-0001",
                    "Sentinel End-to-End Self Test",
                    "Encrypted local test case",
                    actor
                });
                rt.keys.CreateCaseKey(record.id);
                rt.caseRepo.Update(record);
                rt.audit.Append({
                    actor,
                    sentinel::AuditAction::CaseCreated,
                    "case",
                    record.id.ToString(),
                    {}
                });
                tx.Commit();
            }

            const auto reopened = rt.cases.GetCase(record.id);
            if (!reopened ||
                reopened->caseNumber != "SELFTEST-0001" ||
                reopened->title != "Sentinel End-to-End Self Test" ||
                reopened->description != "Encrypted local test case")
                throw std::runtime_error("encrypted case reopen self-test failed");

            sqlite3_stmt* secureCheck{};
            if (sqlite3_prepare_v2(
                    rt.db.Handle(),
                    "SELECT case_number,title,description,"
                    "case_number_cipher,title_cipher,description_cipher "
                    "FROM cases WHERE id=?1",
                    -1,
                    &secureCheck,
                    nullptr) != SQLITE_OK)
                throw std::runtime_error("self-test case storage query failed");

            const auto caseText = record.id.ToString();
            sqlite3_bind_text(
                secureCheck, 1, caseText.c_str(), -1, SQLITE_TRANSIENT);

            if (sqlite3_step(secureCheck) != SQLITE_ROW) {
                sqlite3_finalize(secureCheck);
                throw std::runtime_error("self-test case row missing");
            }

            const auto* legacyNumber =
                reinterpret_cast<const char*>(sqlite3_column_text(secureCheck, 0));
            const auto* legacyTitle =
                reinterpret_cast<const char*>(sqlite3_column_text(secureCheck, 1));
            const auto* legacyDescription =
                reinterpret_cast<const char*>(sqlite3_column_text(secureCheck, 2));

            const bool plaintextBlank =
                (!legacyNumber || legacyNumber[0] == '\0') &&
                (!legacyTitle || legacyTitle[0] == '\0') &&
                (!legacyDescription || legacyDescription[0] == '\0');
            const bool encryptedPresent =
                sqlite3_column_type(secureCheck, 3) == SQLITE_BLOB &&
                sqlite3_column_type(secureCheck, 4) == SQLITE_BLOB &&
                sqlite3_column_type(secureCheck, 5) == SQLITE_BLOB;

            sqlite3_finalize(secureCheck);

            if (!plaintextBlank || !encryptedPresent)
                throw std::runtime_error("case metadata was not stored encrypted");

            const auto sample = root / "self-test-evidence.txt";
            const std::string payload =
                "Sentinel end-to-end authenticated evidence self-test\n";
            {
                std::ofstream out(sample, std::ios::binary | std::ios::trunc);
                if (!out) throw std::runtime_error("cannot create self-test evidence");
                out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            }

            auto caseKey = rt.keys.GetCaseKey(record.id);
            sentinel::EvidenceService evidence(
                root / "evidence",
                rt.db,
                rt.random,
                rt.hash,
                rt.cipher,
                rt.audit);

            const auto imported = evidence.Import(
                {record.id, sample, 0, actor},
                caseKey.Span());

            const auto listed = evidence.ListForCase(record.id, caseKey.Span());
            if (listed.size() != 1 ||
                listed.front().id.ToString() != imported.id.ToString() ||
                listed.front().originalFilename != sample.filename().string())
                throw std::runtime_error("persistent evidence listing self-test failed");

            sentinel::Hash256 verifiedHash{};
            const auto recovered = sentinel::SevContainer::DecryptFile(
                imported.storedPath,
                caseKey.Span(),
                rt.cipher,
                rt.hash,
                &verifiedHash);

            const std::string recoveredText(
                reinterpret_cast<const char*>(recovered.data()),
                recovered.size());

            if (recoveredText != payload ||
                verifiedHash != imported.originalHash)
                throw std::runtime_error("evidence authentication self-test failed");

            if (!rt.audit.VerifyChain())
                throw std::runtime_error("audit-chain self-test failed");

            std::filesystem::remove(sample);

            std::cout << "SENTINEL SELF-TEST: PASS\n";
            std::cout << "Case encryption: PASS\n";
            std::cout << "Case reopen: PASS\n";
            std::cout << "Evidence encryption/authentication: PASS\n";
            std::cout << "Evidence persistence/listing: PASS\n";
            std::cout << "Audit chain: PASS\n";
            return 0;
        }

        if (command == "audit-verify") {
            const bool valid = rt.audit.VerifyChain();
            std::cout << "Audit chain: " << (valid ? "VALID" : "INVALID") << "\n";
            std::cout << "Head: " << rt.audit.GetCurrentHead().ToHex() << "\n";
            return valid ? 0 : 1;
        }

        Usage();
        return 2;
    }
    catch (const std::exception& e) {
        std::cerr << "Sentinel error: " << e.what() << "\n";
        return 1;
    }
}
