#include "Sentinel/Core/Types.hpp"
#include "Sentinel/Evidence/SevContainer.hpp"
#include "Sentinel/Security/Crypto.hpp"
#include "Sentinel/Security/SecretProtector.hpp"
#include "Sentinel/Security/KeyManager.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include "Sentinel/Storage/MigrationService.hpp"
#include "Sentinel/Core/CaseRepository.hpp"
#include "Sentinel/Simulation/ModelRegistry.hpp"
#include "Sentinel/Simulation/TrainingData.hpp"
#include "Sentinel/Simulation/TriggerRules.hpp"
#include "Sentinel/Simulation/PersonaPolicy.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void TestIdsAndHashes()
{
    auto id = sentinel::CaseId::Random();
    auto s = id.ToString();
    auto parsed = sentinel::CaseId::Parse(s);
    assert(parsed.has_value());
    assert(parsed->ToString() == s);

    auto h = sentinel::Hash256::FromHex(std::string(64, '0'));
    assert(h.has_value());
    assert(h->ToHex() == std::string(64, '0'));
}


void TestSaraModelLabRegistries()
{
    using namespace sentinel::simulation;

    FoundationRegistry foundations;
    foundations.EnsureBase("Base Model","base");
    assert(foundations.Models()[0].immutableBase);
    const auto baseId=foundations.Models()[0].id;
    foundations.CreateFork(0,"SARA Foundation","1.0");
    assert(!foundations.Models()[1].immutableBase);
    assert(foundations.Models()[1].parentId==baseId);
    const auto fork1Id=foundations.Models()[1].id;
    foundations.Approve(1);
    foundations.Activate(1);
    assert(foundations.ActiveIndex()==1);

    auto& fork2=foundations.CreateFork(1,"SARA Foundation","1.1");
    foundations.Approve(2);
    foundations.Activate(2);
    assert(foundations.ActiveIndex()==2);
    assert(foundations.Rollback());
    assert(foundations.ActiveIndex()==1);

    PersonaAdapterRegistry adapters;
    adapters.Add("Samantha","Samantha.lora","v1",fork1Id);
    assert(adapters.Adapters()[0].stage==AdapterStage::Staging);
    const auto adapter1Id=adapters.Adapters()[0].id;
    adapters.Activate(0);
    assert(adapters.ResolveActiveIndex("Samantha")==0);
    adapters.Add("Samantha","Samantha.lora","v2",fork1Id);
    adapters.Activate(1);
    assert(adapters.ResolveActiveIndex("Samantha")==1);
    assert(adapters.Rollback("Samantha"));
    assert(adapters.ResolveActiveIndex("Samantha")==0);

    TrainingDataRegistry data;
    data.Capture("Samantha",fork1Id,adapter1Id,"conv-1","hello","hey","shorter","hey");
    assert(data.Count(TrainingExampleState::Review)==1);
    data.SetState(0,TrainingExampleState::Approved);
    assert(data.Count(TrainingExampleState::Approved)==1);
    auto& snapshot=data.CreateSnapshot("dataset-1");
    assert(snapshot.exampleIds.size()==1);

    TrainingJobRegistry jobs;
    auto& job=jobs.Create("SARA Foundation 1.0",snapshot.id);
    assert(job.state=="QUEUED");
    jobs.SetState(0,"RUNNING",35);
    assert(jobs.Jobs()[0].progress==35);
    jobs.SetState(0,"COMPLETED",100);
    assert(jobs.Jobs()[0].state=="COMPLETED");

    TriggerRuleRegistry rules;
    rules.Add("priority-low","hello",{"low"},200,true);
    rules.Add("priority-high","hello",{"one","two"},10,true);
    auto match=rules.Match("hello there","Samantha",3);
    assert(match.has_value());
    assert(match->ruleName=="priority-high");
    assert(match->terminal);

    PersonaProfile persona;
    persona.slangLevel="High";
    persona.emojiTendency="High";
    persona.mood="Playful";
    auto varied=ApplyPersonaWritingVariation(persona,"Okay, I really get it.",10);
    assert(!varied.empty());
}

#ifdef _WIN32
void TestWindowsCryptoAndSev()
{
    sentinel::WindowsSecureRandom random;
    sentinel::WindowsHashService hash;
    sentinel::WindowsAesGcmCipher cipher(random);

    auto key = random.Bytes(32);
    const auto root = std::filesystem::temp_directory_path() / ("sentinel-test-" + sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    const auto source = root / "evidence.bin";
    const auto container = root / "evidence.sev";

    const std::string payload = "Sentinel authenticated evidence payload\nline two\n";
    {
        std::ofstream out(source, std::ios::binary);
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }

    const auto evidenceId = sentinel::EvidenceId::Random();
    const auto caseId = sentinel::CaseId::Random();
    sentinel::Hash256 original{};

    sentinel::SevContainer::EncryptFile(
        source,
        container,
        evidenceId,
        caseId,
        key.Span(),
        cipher,
        hash,
        original);

    assert(sentinel::SevContainer::BasicValidate(container));

    sentinel::Hash256 decryptedHash{};
    const auto plain = sentinel::SevContainer::DecryptFile(
        container,
        key.Span(),
        cipher,
        hash,
        &decryptedHash);

    const std::string recovered(
        reinterpret_cast<const char*>(plain.data()),
        plain.size());

    assert(recovered == payload);
    assert(decryptedHash == original);

    {
        std::fstream io(container, std::ios::binary | std::ios::in | std::ios::out);
        const auto offset = static_cast<std::streamoff>(sizeof(sentinel::SevHeaderV1) + 12);
        io.seekg(offset);
        char c{};
        io.read(&c, 1);
        c ^= 0x01;
        io.seekp(offset);
        io.write(&c, 1);
    }

    assert(sentinel::SevContainer::BasicValidate(container));
    bool tamperRejected = false;
    try {
        (void)sentinel::SevContainer::DecryptFile(container, key.Span(), cipher, hash);
    }
    catch (...) {
        tamperRejected = true;
    }
    assert(tamperRejected);

    sentinel::WindowsDpapiSecretProtector dpapi;
    auto protectedKey = dpapi.Protect(key.Span());
    auto unprotectedKey = dpapi.Unprotect(protectedKey);
    assert(unprotectedKey.Span().size() == key.Span().size());
    for (size_t i = 0; i < key.Span().size(); ++i)
        assert(unprotectedKey.Span()[i] == key.Span()[i]);

    sentinel::SqliteDatabase db;
    const auto dbPath = root / "sentinel.db";
    db.Open(dbPath);
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR) / "migrations");

    sentinel::SqliteCaseRepository cases(db);
    sentinel::CaseRecord caseRecord{
        caseId,
        "TEST-KEY-0001",
        "Key manager integration test",
        "",
        sentinel::CaseStatus::Open,
        sentinel::UserId::Random(),
        sentinel::NowUtc(),
        sentinel::NowUtc()
    };
    cases.Insert(caseRecord);

    sentinel::KeyManager keyManager(root / "keys" / "master.dpapi", db, dpapi, random, cipher);
    keyManager.Initialize();
    keyManager.CreateCaseKey(caseId);
    assert(keyManager.HasCaseKey(caseId));
    auto loadedCaseKey = keyManager.GetCaseKey(caseId);
    assert(loadedCaseKey.Span().size() == 32);

    sentinel::KeyManager reopened(root / "keys" / "master.dpapi", db, dpapi, random, cipher);
    reopened.Initialize();
    auto loadedAgain = reopened.GetCaseKey(caseId);
    assert(loadedAgain.Span().size() == loadedCaseKey.Span().size());
    for (size_t i = 0; i < loadedCaseKey.Span().size(); ++i)
        assert(loadedAgain.Span()[i] == loadedCaseKey.Span()[i]);

    db.Close();
    std::filesystem::remove_all(root);
}
#endif

}

int main()
{
    TestIdsAndHashes();
    TestSaraModelLabRegistries();
#ifdef _WIN32
    TestWindowsCryptoAndSev();
#endif
    std::cout << "SARA Core Tests passed\n";
}
