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
#include "Sentinel/Simulation/PersonaProfileStore.hpp"
#include "Sentinel/Channels/ChannelCore.hpp"
#include "Sentinel/Channels/ChannelAdapterRegistry.hpp"
#include "Sentinel/Channels/AutomationEngine.hpp"
#include "Sentinel/Channels/JurisdictionRules.hpp"
#include "Sentinel/Channels/LocalSimulationChannelAdapter.hpp"
#include "Sentinel/Operations/Messaging.hpp"
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
    std::vector<EvaluationCaseResult> caseResults={
        ScoreNamedCase({"persona.identity","Persona identity",EvaluationDimension::PersonaConsistency,
            "What is your name?",{"Samantha"},{},0},"My name is Samantha."),
        ScoreNamedCase({"memory.codeword","Long-context code-word recall",EvaluationDimension::MemoryRecall,
            "What code word?",{"cobalt"},{},24},"The code word was cobalt.")
    };
    assert(caseResults[0].passed);
    assert(caseResults[1].passed);

    EvaluationRunRegistry registry;
    registry.Create(
        "model-1","Candidate A","foundation-1","SARA Foundation 1.0",
        "adapter-1","Samantha.lora v1",dimensions,caseResults);
    assert(registry.Runs()[0].overallScore>0);
    assert(registry.Runs()[0].previousOverallScore==-1);
    const int firstScore=registry.Runs()[0].overallScore;

    auto regressed=dimensions;
    regressed[0].score=40; regressed[0].passed=false;
    regressed[3].score=25; regressed[3].passed=false;
    auto& second=registry.Create(
        "model-1","Candidate A","foundation-1","SARA Foundation 1.0",
        "adapter-1","Samantha.lora v1",regressed,caseResults);
    assert(second.previousOverallScore==firstScore);
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
    assert(loaded.Runs()[0].cases.size()==2);
    assert(loaded.Runs()[1].regressionDelta==second.regressionDelta);
    assert(DimensionScore(loaded.Runs()[0],EvaluationDimension::MemoryRecall)==100);
    auto singleReport=BuildEvaluationRunReport(loaded.Runs()[0]);
    assert(singleReport.find("SARA EVALUATION RUN REPORT")!=std::string::npos);
    assert(singleReport.find("NAMED CASES")!=std::string::npos);
    auto report=BuildCandidateComparisonReport(loaded.Runs()[0],loaded.Runs()[1]);
    assert(report.find("SARA EVALUATION COMPARISON")!=std::string::npos);
    assert(report.find("MEMORY")!=std::string::npos);
    assert(loaded.LatestIndexForCandidate("model-1")==1);

    const auto& cases=DefaultEvaluationTestCases();
    assert(cases.size()>=6);
    assert(std::any_of(cases.begin(),cases.end(),[](const auto& tc){
        return tc.dimension==EvaluationDimension::MemoryRecall &&
            !tc.expectedContains.empty() && tc.expectedContains.front()=="cobalt" &&
            tc.minimumHistoryTurns>=24;
    }));

    std::filesystem::remove_all(root);
}


void TestReusablePersonaProfiles()
{
    using namespace sentinel::simulation;

    const auto root=std::filesystem::temp_directory_path()/("sara-personas-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    sentinel::SqliteDatabase db;
    db.Open(root/"persona-test.db");
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR)/"migrations");

    PersonaProfileStore store(db);
    PersonaProfile samantha;
    samantha.name="Samantha";
    samantha.age=24;
    samantha.location="Lafayette";
    samantha.personality="Playful";
    samantha.intelligenceLevel="High";
    samantha.slangLevel="Medium";
    samantha.grammarQuality="Natural";
    samantha.typoTendency="Low";
    samantha.emojiTendency="High";
    samantha.mood="Warm";
    samantha.lockedFacts={"likes music","prefers short messages"};

    store.Save(samantha,1400,5200);
    assert(store.Count()==1);

    auto loaded=store.Load("Samantha");
    assert(loaded.has_value());
    assert(loaded->profile.name=="Samantha");
    assert(loaded->profile.location=="Lafayette");
    assert(loaded->profile.lockedFacts.size()==2);
    assert(loaded->minDelayMs==1400);
    assert(loaded->maxDelayMs==5200);

    PersonaProfile nikki=samantha;
    nikki.name="Nikki";
    nikki.personality="Confident";
    store.Save(nikki,2200,6400);
    assert(store.Count()==2);

    auto profiles=store.List();
    assert(profiles.size()==2);
    assert(std::any_of(profiles.begin(),profiles.end(),[](const auto& item){return item.profile.name=="Samantha";}));
    assert(std::any_of(profiles.begin(),profiles.end(),[](const auto& item){return item.profile.name=="Nikki";}));

    samantha.mood="Guarded";
    store.Save(samantha,1800,5600);
    loaded=store.Load("Samantha");
    assert(loaded.has_value());
    assert(loaded->profile.mood=="Guarded");
    assert(loaded->minDelayMs==1800);

    assert(store.Delete("Nikki"));
    assert(store.Count()==1);
    assert(!store.Load("Nikki").has_value());

    db.Close();
    std::filesystem::remove_all(root);
}


void TestUnifiedChannelCore()
{
    using namespace sentinel::channels;

    const auto root=std::filesystem::temp_directory_path()/("sara-channels-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    sentinel::SqliteDatabase db;
    db.Open(root/"channels.db");
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR)/"migrations");

    ChannelCoreStore store(db);
    const auto subjectId=store.CreateSubject("case-1","Synthetic subject");
    assert(!subjectId.empty());

    const auto identityId=store.AddSubjectIdentity(
        subjectId,"username","local","test-user","test-user",
        IdentityLinkState::Candidate,0.60);
    assert(!identityId.empty());
    assert(store.ConfirmSubjectIdentity(identityId,"unit-test"));

    ChannelAccount account;
    account.id="account-1";
    account.type=ChannelType::LocalSimulation;
    account.provider="local";
    account.externalAccountId="local-account";
    account.displayName="Local Simulation";
    account.address="local";
    account.jurisdiction="test";
    account.complianceStatus="test-only";
    account.capabilities.Set(Capability::ReceiveText);
    account.capabilities.Set(Capability::SendText);
    account.capabilities.Set(Capability::SendImage);
    account.capabilities.Set(Capability::AutomatedSending);
    assert(store.UpsertChannelAccount(account)=="account-1");

    ChannelConversation conversation;
    conversation.subjectId=subjectId;
    conversation.personaName="Samantha";
    conversation.channelAccountId=account.id;
    conversation.type=ChannelType::LocalSimulation;
    conversation.provider="local";
    conversation.providerConversationId="conversation-1";
    conversation.externalPeerId="test-peer";
    const auto conversationId=store.OpenConversation(conversation);
    assert(!conversationId.empty());

    auto found=store.FindConversation("local",account.id,"conversation-1");
    assert(found.has_value());
    assert(found->personaName=="Samantha");

    RawChannelEvent event;
    event.id="event-1";
    event.channelConversationId=conversationId;
    event.provider="local";
    event.type=ChannelType::LocalSimulation;
    event.providerConversationId="conversation-1";
    event.providerMessageId="provider-message-1";
    event.eventType="message";
    event.direction=Direction::Inbound;
    event.senderExternalId="test-peer";
    event.recipientExternalId="local-account";
    event.providerTimestamp="2026-09-27T00:00:00Z";
    event.rawPayload="{\"text\":\"hello\"}";
    event.payloadSha256="test-hash";
    store.RecordRawEvent(event);

    sentinel::channels::NormalizedMessage normalized;
    normalized.id="message-1";
    normalized.eventId=event.id;
    normalized.channelConversationId=conversationId;
    normalized.subjectId=subjectId;
    normalized.personaName="Samantha";
    normalized.direction=Direction::Inbound;
    normalized.senderExternalId="test-peer";
    normalized.recipientExternalId="local-account";
    normalized.body="hello";
    normalized.automationMode=AutomationMode::Manual;
    normalized.providerMessageId=event.providerMessageId;
    store.RecordMessage(normalized);

    auto recent=store.RecentMessages(conversationId,10);
    assert(recent.size()==1);
    assert(recent[0].body=="hello");

    ChannelAdapterRegistry registry;
    auto legacyAdapter=sentinel::operations::CreateInMemoryMessageAdapter();
    registry.Register(std::make_unique<LocalSimulationChannelAdapter>(*legacyAdapter));
    auto* adapter=registry.FindByName("SARA Local Simulation");
    assert(adapter!=nullptr);
    assert(adapter->Connected());
    assert(adapter->Capabilities().Has(Capability::SendText));
    assert(adapter->Capabilities().Has(Capability::SendImage));

    auto send=adapter->SendText({conversationId,"approved reply","reply-1"});
    assert(send.accepted);
    auto media=adapter->SendMedia({conversationId,"approved image","C:/synthetic/test.png","image/png","abc123","media-1"});
    assert(media.accepted);

    AutomationEngine engine;
    AutomationRequest request;
    request.mode=AutomationMode::AuthorizedAutomatic;
    request.action=ActionKind::OrdinaryReply;
    request.providerSupportsAutomation=true;
    request.policyAllowed=true;
    request.policyRequiresSupervisor=false;
    request.jurisdictionProfileActive=true;
    request.jurisdictionAllowsAutomation=true;
    request.jurisdictionRequiresReview=false;
    auto decision=engine.Decide(request);
    assert(decision.decision==AutomationDecisionKind::AutoSend);
    assert(!decision.requiresHuman);

    request.action=ActionKind::MeetingArrangement;
    decision=engine.Decide(request);
    assert(decision.decision==AutomationDecisionKind::RequireApproval);
    assert(decision.requiresHuman);

    request.action=ActionKind::OrdinaryReply;
    request.jurisdictionProfileActive=false;
    decision=engine.Decide(request);
    assert(decision.decision==AutomationDecisionKind::RequireApproval);
    assert(decision.requiresHuman);

    request.jurisdictionProfileActive=true;
    request.policyRequiresSupervisor=true;
    decision=engine.Decide(request);
    assert(decision.decision==AutomationDecisionKind::RequireApproval);
    assert(decision.requiresHuman);

    JurisdictionRuleStore jurisdictions(db);
    jurisdictions.EnsureBuiltInBaselines();
    auto federal=jurisdictions.LatestProfile(RuleLayerType::Federal,"US");
    assert(federal.has_value());
    assert(!federal->AutomationLegallyActive());

    db.Close();
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
    TestReusablePersonaProfiles();
    TestUnifiedChannelCore();
#ifdef _WIN32
    TestWindowsCryptoAndSev();
#endif
    std::cout << "SARA Core Tests passed\n";
}
