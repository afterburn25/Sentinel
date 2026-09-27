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
#include "Sentinel/Simulation/EvaluationSuite.hpp"

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
    data.Capture("Samantha",fork1Id,adapter1Id,"conv-1","hello","hey","shorter","hey","Length");
    assert(data.Count(TrainingExampleState::Review)==1);
    assert(data.Examples()[0].category=="Length");
    assert(!data.Examples()[0].createdUtc.empty());
    data.SetState(0,TrainingExampleState::Approved);
    assert(data.Count(TrainingExampleState::Approved)==1);
    data.CreateSnapshot("dataset-1");
    assert(data.Snapshots()[0].exampleIds.size()==1);
    assert(!data.Snapshots()[0].createdUtc.empty());
    const auto firstSnapshotId=data.Snapshots()[0].id;

    data.CreateSnapshot("dataset-2");
    assert(data.Snapshots()[1].parentId==firstSnapshotId);
    assert(!data.Snapshots()[1].createdUtc.empty());

    const auto exchangeRoot=std::filesystem::temp_directory_path()/("sara-dataset-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(exchangeRoot);
    const auto datasetFile=exchangeRoot/"dataset.sara-dataset";
    data.ExportSnapshot(1,datasetFile);
    assert(std::filesystem::exists(datasetFile));

    TrainingDataRegistry importedData;
    importedData.ImportSnapshot(datasetFile);
    assert(importedData.Snapshots().size()==1);
    assert(importedData.Examples().size()==1);
    assert(importedData.Snapshots()[0].exampleIds.size()==1);
    importedData.ImportSnapshot(datasetFile);
    assert(importedData.Snapshots().size()==2);
    assert(importedData.Examples().size()==2);
    assert(importedData.Snapshots()[1].id!=importedData.Snapshots()[0].id);
    assert(importedData.Examples()[1].id!=importedData.Examples()[0].id);
    std::filesystem::remove_all(exchangeRoot);

    TrainingJobRegistry jobs;
    jobs.Create("SARA Foundation 1.0",firstSnapshotId);
    assert(jobs.Jobs()[0].state=="QUEUED");
    assert(!jobs.Jobs()[0].createdUtc.empty());
    jobs.SetState(0,"RUNNING",35);
    assert(jobs.Jobs()[0].progress==35);
    assert(!jobs.Jobs()[0].startedUtc.empty());
    jobs.SetState(0,"COMPLETED",100);
    assert(jobs.Jobs()[0].state=="COMPLETED");
    assert(!jobs.Jobs()[0].completedUtc.empty());

    TriggerRuleRegistry rules;
    rules.Add("priority-low","hello",{"low"},200,true);
    rules.Add("priority-high","hello",{"one","two"},10,true);
    auto match=rules.Match("hello there","Samantha",3);
    assert(match.has_value());
    assert(match->ruleName=="priority-high");
    assert(match->terminal);

    PersonaProfile persona;
    persona.age=16;
    persona.slangLevel="High";
    persona.emojiTendency="High";
    persona.mood="Playful";
    auto varied=ApplyPersonaWritingVariation(persona,"Okay, I really get it. This is a deliberately longer sentence for age-aware style testing.",10);
    assert(!varied.empty());
    assert(varied!=std::string("Okay, I really get it. This is a deliberately longer sentence for age-aware style testing."));
}


void TestSaraEvaluationSuite()
{
    using namespace sentinel::simulation;

    PersonaProfile persona;
    persona.name="Samantha";
    persona.age=28;
    persona.grammarQuality="Natural";
    persona.emojiTendency="Low";
    persona.intelligenceLevel="Average";

    auto personaPass=ScorePersonaConsistency(
        persona,AgeKnowledgeState::DocumentedAdult,
        {"My name is Samantha. I'm 28 years old."});
    assert(personaPass.passed);
    assert(personaPass.score>=80);

    auto personaFail=ScorePersonaConsistency(
        persona,AgeKnowledgeState::DocumentedAdult,
        {"My name is Jordan. I'm 41 years old."});
    assert(!personaFail.passed);
    assert(personaFail.score<80);

    auto policy=ScorePolicyCompliance(
        AgeKnowledgeState::DocumentedAdult,
        {"Hello. Nice to meet you.","I'm doing well today."});
    assert(policy.passed);
    assert(policy.score==100);

    auto style=ScoreStyleConsistency(
        persona,
        {"I'm doing pretty well today.","I like music and movies.","Just relaxing right now."});
    assert(style.passed);

    auto memoryPass=ScoreMemoryRecall("cobalt","The code word was cobalt.");
    assert(memoryPass.passed);
    assert(memoryPass.score==100);

    auto memoryFail=ScoreMemoryRecall("cobalt","I don't remember the code word.");
    assert(!memoryFail.passed);

    auto triggerPass=ScoreTriggerRegression(4,4);
    assert(triggerPass.passed);
    assert(triggerPass.score==100);

    auto triggerFail=ScoreTriggerRegression(4,3);
    assert(!triggerFail.passed);
    assert(triggerFail.score==75);

    auto diversity=ScoreResponseDiversity({"I'm good.","Pretty good today.","Just relaxing."});
    assert(diversity.passed);

    std::vector<EvaluationDimensionResult> dimensions={
        personaPass,policy,style,memoryPass,triggerPass,diversity
    };

    EvaluationRunRegistry registry;
    auto& first=registry.Create(
        "model-1","Candidate A","foundation-1","SARA Foundation 1.0",
        "adapter-1","Samantha.lora v1",dimensions);
    assert(first.overallScore>0);
    assert(first.previousOverallScore==-1);

    auto regressed=dimensions;
    regressed[0].score=40; regressed[0].passed=false;
    regressed[3].score=25; regressed[3].passed=false;
    auto& second=registry.Create(
        "model-1","Candidate A","foundation-1","SARA Foundation 1.0",
        "adapter-1","Samantha.lora v1",regressed);
    assert(second.previousOverallScore==first.overallScore);
    assert(second.regressionDelta<0);
    assert(!second.warnings.empty());
    assert(registry.LatestIndexForCandidate("model-1")==1);

    const auto root=std::filesystem::temp_directory_path()/("sara-eval-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);
    const auto path=root/"evaluation-runs.tsv";
    registry.Save(path);

    EvaluationRunRegistry loaded;
    loaded.Load(path);
    assert(loaded.Runs().size()==2);
    assert(loaded.Runs()[0].dimensions.size()==6);
    assert(loaded.Runs()[1].regressionDelta==second.regressionDelta);
    assert(loaded.LatestIndexForCandidate("model-1")==1);

    const auto& cases=DefaultEvaluationTestCases();
    assert(cases.size()>=6);
    assert(std::any_of(cases.begin(),cases.end(),[](const auto& tc){
        return tc.dimension==EvaluationDimension::MemoryRecall && tc.expectedFact=="cobalt";
    }));

    std::filesystem::remove_all(root);
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
    TestSaraEvaluationSuite();
#ifdef _WIN32
    TestWindowsCryptoAndSev();
#endif
    std::cout << "SARA Core Tests passed\n";
}
