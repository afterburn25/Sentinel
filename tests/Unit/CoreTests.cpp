#include "Sentinel/Core/Types.hpp"
#include "Sentinel/Evidence/SevContainer.hpp"
#include "Sentinel/Security/Crypto.hpp"
#include "Sentinel/Security/SecretProtector.hpp"
#include "Sentinel/Security/KeyManager.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include "Sentinel/Storage/MigrationService.hpp"
#include "Sentinel/Core/CaseRepository.hpp"
#include "Sentinel/Channels/ChannelCore.hpp"
#include "Sentinel/Channels/JurisdictionRules.hpp"
#include "Sentinel/Simulation/TrainingReviewStore.hpp"
#include "Sentinel/Simulation/TrainerStore.hpp"
#include "Sentinel/Simulation/ModelRegistry.hpp"
#include "Sentinel/Simulation/ConversationMemory.hpp"
#include "Sentinel/Simulation/EvaluationSuite.hpp"
#include "Sentinel/Simulation/DeploymentRegistry.hpp"
#include "Sentinel/Simulation/TrainingData.hpp"
#include "Sentinel/Identity/SubjectIdentityStore.hpp"
#include "Sentinel/Operations/SupervisorStateStore.hpp"

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


void TestTrainingReviewRecentList()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    const auto root=std::filesystem::temp_directory_path()/("sara-review-list-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    sentinel::SqliteDatabase db;
    db.Open(root/"reviews.db");
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR)/"migrations");

    db.Execute(
        "INSERT INTO training_review_items("
        "id,source_log_id,conversation_id,persona_name,model_name,input_text,output_text,status"
        ") VALUES"
        "('review-old','source-old','conversation-1','Samantha','model-a','old input','old output',1),"
        "('review-new','source-new','conversation-2','Nikki','model-b','new input','new output',0);");

    sentinel::simulation::TrainingReviewStore reviews(db);
    const auto all=reviews.ListRecent(10);
    Require(all.size()==2,"recent training review list count mismatch");
    Require(all.front().id=="review-new","recent training review ordering mismatch");

    const auto pending=reviews.ListRecent(
        10,sentinel::simulation::TrainingReviewStatus::Pending);
    Require(pending.size()==1,"pending training review filter count mismatch");
    Require(pending.front().id=="review-new","pending training review filter returned wrong item");

    const auto approved=reviews.ListRecent(
        10,sentinel::simulation::TrainingReviewStatus::Approved);
    Require(approved.size()==1,"approved training review filter count mismatch");
    Require(approved.front().id=="review-old","approved training review filter returned wrong item");

    db.Close();
    std::filesystem::remove_all(root);
}



void TestPersonaLoraHistory()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    const auto root=std::filesystem::temp_directory_path()/("sara-lora-history-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    sentinel::SqliteDatabase db;
    db.Open(root/"trainer.db");
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR)/"migrations");

    sentinel::simulation::TrainerStore trainer(db);
    trainer.EnsureDefaultFoundation("SARA Foundation","base-model","runtime.gguf");
    const auto foundations=trainer.ListFoundations();
    Require(!foundations.empty(),"default foundation missing");

    const auto first=trainer.BindPersonaLora(
        "Samantha",foundations.front().id,"Samantha v1","samantha-v1.gguf",1.0);
    Require(first.active,"first persona LoRA should be active");

    const auto second=trainer.BindPersonaLora(
        "Samantha",foundations.front().id,"Samantha v2","samantha-v2.gguf",0.9);
    Require(second.active,"second persona LoRA should be active");

    const auto history=trainer.ListPersonaLoras("Samantha",10);
    Require(history.size()==2,"persona LoRA history count mismatch");
    Require(history.front().loraName=="Samantha v2","active persona LoRA was not listed first");
    Require(history.front().active,"active persona LoRA history flag missing");
    Require(!history.back().active,"prior persona LoRA should have been deactivated");

    const auto resolved=trainer.ResolvePersonaLora("Samantha");
    Require(resolved.has_value(),"active persona LoRA did not resolve");
    Require(resolved->loraName=="Samantha v2","resolved persona LoRA version mismatch");

    Require(trainer.ActivatePersonaLora(first.id),"prior persona LoRA could not be reactivated");
    const auto rolledBack=trainer.ResolvePersonaLora("Samantha");
    Require(rolledBack.has_value(),"reactivated persona LoRA did not resolve");
    Require(rolledBack->id==first.id,"exact persona LoRA version was not restored by ID");

    const auto manifest=trainer.BuildPersonaLoraManifest(first.id);
    Require(manifest.find("\"schema\": \"sara-persona-lora-v1\"")!=std::string::npos,
        "persona LoRA manifest schema missing");
    Require(manifest.find("Samantha v1")!=std::string::npos,
        "persona LoRA manifest name missing");
    Require(manifest.find(foundations.front().id)!=std::string::npos,
        "persona LoRA manifest foundation linkage missing");

    db.Close();
    std::filesystem::remove_all(root);
}



void TestTrainerFoundationAndJobs()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    const auto root=std::filesystem::temp_directory_path()/("sara-trainer-lifecycle-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    sentinel::SqliteDatabase db;
    db.Open(root/"trainer.db");
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR)/"migrations");

    sentinel::simulation::TrainerStore trainer(db);
    trainer.EnsureDefaultFoundation("SARA Foundation","base-model","runtime.gguf");
    const auto foundations=trainer.ListFoundations();
    Require(foundations.size()==1,"default foundation count mismatch");
    Require(foundations.front().status=="ACTIVE","default foundation should be active");

    const auto fork=trainer.CreateFork(
        "SARA Foundation 2",foundations.front().id,
        foundations.front().sourceModel,"trainable-source","");
    Require(fork.parentId==foundations.front().id,"foundation fork parent mismatch");
    Require(fork.status=="DRAFT","new foundation fork should be draft");
    Require(trainer.ApproveFoundation(fork.id),"foundation approval failed");
    Require(trainer.ActivateFoundation(fork.id),"approved foundation activation failed");

    const auto activeFork=trainer.GetFoundation(fork.id);
    Require(activeFork.has_value() && activeFork->status=="ACTIVE",
        "selected foundation did not become active");
    const auto priorBase=trainer.GetFoundation(foundations.front().id);
    Require(priorBase.has_value() && priorBase->status=="APPROVED",
        "previous active foundation was not preserved as approved");

    const auto job=trainer.QueueJob(
        sentinel::simulation::TrainingMode::FoundationSft,
        fork.name,"Samantha",fork.id,
        "approved.jsonl","trainable-source","output-folder");
    Require(job.state=="QUEUED","new trainer job should be queued");

    const auto jobs=trainer.ListJobs(10);
    Require(!jobs.empty(),"trainer job list is empty");
    Require(jobs.front().id==job.id,"trainer jobs should return newest first");
    Require(jobs.front().mode==sentinel::simulation::TrainingMode::FoundationSft,
        "trainer job mode mismatch");

    db.Close();
    std::filesystem::remove_all(root);
}

void TestModelRegistryLifecycle()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    sentinel::simulation::ModelRegistry registry;
    auto& first=registry.Register("http://127.0.0.1:8001","candidate-a");
    first.evaluationScore=91;
    registry.Approve(0);
    Require(registry.Models()[0].stage==sentinel::simulation::ModelStage::Approved,
        "candidate approval failed");
    registry.Activate(0);
    Require(registry.ActiveIndex()==0,"first model activation failed");
    Require(registry.Models()[0].stage==sentinel::simulation::ModelStage::Active,
        "first model active stage missing");

    auto& second=registry.Register("http://127.0.0.1:8002","candidate-b");
    second.evaluationScore=95;
    registry.Approve(1);
    registry.Activate(1);
    Require(registry.ActiveIndex()==1,"second model activation failed");
    Require(registry.Models()[0].stage==sentinel::simulation::ModelStage::Approved,
        "previous active model should return to approved");

    Require(registry.Rollback(),"model rollback should succeed");
    Require(registry.ActiveIndex()==0,"model rollback did not restore previous active model");

    registry.Retire(1);
    Require(registry.Models()[1].stage==sentinel::simulation::ModelStage::Retired,
        "model retirement failed");
}


void TestPersonaScopedConversationMemory()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    const auto root=std::filesystem::temp_directory_path()/("sara-persona-memory-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    sentinel::SqliteDatabase db;
    db.Open(root/"memory.db");
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR)/"migrations");

    sentinel::simulation::ConversationMemoryStore memory(db);

    const auto samanthaOld=memory.StartConversation(
        "Samantha old","Samantha","Samantha, age 13, playful","Neutral");
    memory.Append(samanthaOld,sentinel::simulation::ChatTurn::Speaker::Investigator,
        "My favorite gemstone is cobalt.");
    memory.Append(samanthaOld,sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
        "okay i remember cobalt");

    const auto samanthaCurrent=memory.StartConversation(
        "Samantha current","Samantha","Samantha, age 13, playful","Neutral");
    memory.Append(samanthaCurrent,sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
        "current samantha session");

    const auto nikkiOld=memory.StartConversation(
        "Nikki old","Nikki","Nikki, age 16, confident","Neutral");
    memory.Append(nikkiOld,sentinel::simulation::ChatTurn::Speaker::Investigator,
        "My favorite gemstone is amber.");
    memory.Append(nikkiOld,sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
        "okay i remember amber");

    const auto nikkiCurrent=memory.StartConversation(
        "Nikki current","Nikki","Nikki, age 16, confident","Neutral");
    memory.Append(nikkiCurrent,sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
        "current nikki session");

    const auto samanthaList=memory.ListForPersona("Samantha",20);
    Require(samanthaList.size()==2,"Samantha archive count mismatch");
    Require(std::all_of(samanthaList.begin(),samanthaList.end(),
        [](const auto& item){return item.personaName=="Samantha";}),
        "Samantha archive leaked another persona");

    const auto nikkiList=memory.ListForPersona("Nikki",20);
    Require(nikkiList.size()==2,"Nikki archive count mismatch");
    Require(std::all_of(nikkiList.begin(),nikkiList.end(),
        [](const auto& item){return item.personaName=="Nikki";}),
        "Nikki archive leaked another persona");

    const auto samanthaRecall=memory.RecallRelevant(
        "What gemstone did I mention before?",samanthaCurrent,"Samantha",12);
    Require(samanthaRecall.find("cobalt")!=std::string::npos,
        "Samantha recall missed Samantha memory");
    Require(samanthaRecall.find("amber")==std::string::npos,
        "Samantha recall leaked Nikki memory");

    const auto nikkiRecall=memory.RecallRelevant(
        "What gemstone did I mention before?",nikkiCurrent,"Nikki",12);
    Require(nikkiRecall.find("amber")!=std::string::npos,
        "Nikki recall missed Nikki memory");
    Require(nikkiRecall.find("cobalt")==std::string::npos,
        "Nikki recall leaked Samantha memory");

    sentinel::simulation::ModelContext loaded;
    Require(memory.Load(samanthaOld,loaded),"failed to load Samantha conversation");
    Require(loaded.personaSummary.find("Samantha")!=std::string::npos,
        "loaded persona summary lost persona identity");
    Require(loaded.personaSummary.find("playful")!=std::string::npos,
        "loaded persona summary lost persona details");

    db.Close();
    std::filesystem::remove_all(root);
}



void TestPersistentEvaluationSuite()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    sentinel::simulation::PersonaProfile persona;
    persona.name="Samantha";
    persona.age=13;
    persona.grammarQuality="Casual";
    persona.emojiLevel="Occasional";
    persona.cognitiveLevel="Average";

    const std::vector<std::string> responses={
        "hey im samantha and im 13",
        "music is probably my favorite thing rn",
        "im just hanging out today"
    };

    auto personaScore=sentinel::simulation::ScorePersonaConsistency(
        persona,sentinel::simulation::AgeKnowledgeState::Unknown,responses);
    auto policyScore=sentinel::simulation::ScorePolicyCompliance(
        sentinel::simulation::AgeKnowledgeState::Unknown,responses);
    auto styleScore=sentinel::simulation::ScoreStyleConsistency(persona,responses);
    auto memoryScore=sentinel::simulation::ScoreMemoryRecall("cobalt","yeah i remember cobalt");
    auto triggerScore=sentinel::simulation::ScoreTriggerRegression(4,4);
    auto diversityScore=sentinel::simulation::ScoreResponseDiversity(responses);

    sentinel::simulation::EvaluationRunRegistry registry;
    auto& first=registry.Create(
        "candidate-a","Candidate A",
        "foundation-1","SARA Foundation",
        "adapter-1","Samantha v1",
        {personaScore,policyScore,styleScore,memoryScore,triggerScore,diversityScore});
    Require(first.overallScore>0,"evaluation run overall score missing");
    Require(first.previousOverallScore==-1,"first evaluation run should not have a previous score");

    auto weaker=diversityScore;
    weaker.score=20;
    weaker.passed=false;
    weaker.warnings={"deliberate regression"};
    auto& second=registry.Create(
        "candidate-a","Candidate A",
        "foundation-1","SARA Foundation",
        "adapter-1","Samantha v1",
        {personaScore,policyScore,styleScore,memoryScore,triggerScore,weaker});
    Require(second.previousOverallScore==first.overallScore,
        "evaluation run did not link previous candidate score");
    Require(second.regressionDelta==second.overallScore-first.overallScore,
        "evaluation run regression delta mismatch");

    const auto root=std::filesystem::temp_directory_path()/("sara-eval-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);
    const auto path=root/"evaluation-runs.tsv";
    registry.Save(path);

    sentinel::simulation::EvaluationRunRegistry loaded;
    loaded.Load(path);
    Require(loaded.Runs().size()==2,"evaluation registry persistence count mismatch");
    Require(loaded.LatestIndexForCandidate("candidate-a")==1,
        "evaluation registry latest candidate index mismatch");
    Require(sentinel::simulation::DimensionScore(
        loaded.Runs().back(),sentinel::simulation::EvaluationDimension::ResponseDiversity)==20,
        "evaluation dimension persistence mismatch");

    const auto report=sentinel::simulation::BuildEvaluationRunReport(loaded.Runs().back());
    Require(report.find("SARA EVALUATION RUN REPORT")!=std::string::npos,
        "evaluation run report header missing");
    const auto comparison=sentinel::simulation::BuildCandidateComparisonReport(
        loaded.Runs().front(),loaded.Runs().back());
    Require(comparison.find("Overall delta")!=std::string::npos,
        "evaluation comparison report missing delta");

    std::filesystem::remove_all(root);
}



void TestDeploymentRegistryLifecycle()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    sentinel::simulation::DeploymentRegistry deployments;
    auto& first=deployments.Prepare(
        "model-1","Candidate A",
        "foundation-1","SARA Foundation 1.0",
        "1","Samantha v1",
        "Samantha","eval-1",92);
    const std::string firstDeploymentId=first.id;
    Require(first.stage==sentinel::simulation::DeploymentStage::Staged,
        "prepared deployment should be staged");
    Require(first.versionLocked,"prepared deployment should start locked");
    Require(deployments.Activate(0),"first deployment activation failed");
    Require(deployments.ActiveIndex()==0,"first deployment active index mismatch");
    Require(deployments.HasActiveLockedDeployment(),
        "active locked deployment was not reported");

    deployments.SetLocked(0,false);
    Require(!deployments.HasActiveLockedDeployment(),
        "deployment unlock did not change active lock state");
    deployments.SetLocked(0,true);

    auto& second=deployments.Prepare(
        "model-2","Candidate \"B\"",
        "foundation-2","SARA Foundation 2.0",
        "2","Samantha v2",
        "Samantha","eval-2",96);
    Require(second.previousDeploymentId==firstDeploymentId,
        "prepared deployment did not link previous active package");
    Require(deployments.Activate(1),"second deployment activation failed");
    Require(deployments.ActiveIndex()==1,"second deployment active index mismatch");
    Require(deployments.PreviousIndex()==0,"previous deployment index was not preserved");

    const auto manifest=deployments.BuildManifest(1);
    Require(manifest.find("\"schema\": \"sara-deployment-v1\"")!=std::string::npos,
        "deployment manifest schema missing");
    Require(manifest.find("foundation-2")!=std::string::npos,
        "deployment manifest foundation missing");
    Require(manifest.find("Candidate \\\"B\\\"")!=std::string::npos,
        "deployment manifest JSON escaping failed");

    Require(deployments.Rollback(),"deployment rollback failed");
    Require(deployments.ActiveIndex()==0,"deployment rollback active index mismatch");
    Require(deployments.Packages()[0].stage==sentinel::simulation::DeploymentStage::Active,
        "rollback target did not become active");
    Require(deployments.Packages()[1].stage==sentinel::simulation::DeploymentStage::RolledBack,
        "rolled-back package stage mismatch");

    const auto root=std::filesystem::temp_directory_path()/("sara-deployment-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);
    const auto path=root/"deployment-registry.tsv";
    deployments.Save(path);

    sentinel::simulation::DeploymentRegistry loaded;
    loaded.Load(path);
    Require(loaded.Packages().size()==2,"deployment registry persistence count mismatch");
    Require(loaded.ActiveIndex()==0,"deployment registry active index persistence mismatch");
    Require(loaded.Packages()[1].evaluationRunId=="eval-2",
        "deployment registry evaluation linkage persistence mismatch");
    Require(loaded.Packages()[0].versionLocked,
        "deployment version lock persistence mismatch");

    std::filesystem::remove_all(root);
}



void TestVersionedDatasetSnapshots()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    sentinel::simulation::TrainingDataRegistry data;
    auto& example=data.Capture(
        "Samantha","foundation-1","adapter-1","conversation-1",
        "hello","hey","make it shorter","hey","Behavior");
    example.reviewer="unit-test";
    data.SetState(0,sentinel::simulation::TrainingExampleState::Approved);

    auto& first=data.CreateSnapshot("dataset-1");
    const std::string firstSnapshotId=first.id;
    Require(first.exampleIds.size()==1,"dataset snapshot did not include approved example");
    Require(first.parentId.empty(),"first dataset snapshot should have no parent");

    auto& second=data.CreateSnapshot("dataset-2");
    Require(second.parentId==firstSnapshotId,"dataset snapshot lineage did not link prior snapshot");

    const auto root=std::filesystem::temp_directory_path()/("sara-dataset-versioning-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);
    const auto registryPath=root/"training-data.tsv";
    const auto exchangePath=root/"dataset.sara-dataset";

    data.Save(registryPath);
    data.ExportSnapshot(1,exchangePath);
    Require(std::filesystem::exists(exchangePath),"dataset snapshot export file missing");

    sentinel::simulation::TrainingDataRegistry loaded;
    loaded.Load(registryPath);
    Require(loaded.Examples().size()==1,"dataset registry example persistence mismatch");
    Require(loaded.Snapshots().size()==2,"dataset registry snapshot persistence mismatch");
    Require(loaded.Snapshots()[1].parentId==loaded.Snapshots()[0].id,
        "dataset snapshot parent persistence mismatch");

    sentinel::simulation::TrainingDataRegistry imported;
    imported.ImportSnapshot(exchangePath);
    imported.ImportSnapshot(exchangePath);
    Require(imported.Snapshots().size()==2,"repeated snapshot import count mismatch");
    Require(imported.Examples().size()==2,"repeated snapshot import example count mismatch");
    Require(imported.Snapshots()[0].id!=imported.Snapshots()[1].id,
        "repeated snapshot import did not de-duplicate snapshot ID");
    Require(imported.Examples()[0].id!=imported.Examples()[1].id,
        "repeated snapshot import did not de-duplicate example ID");

    std::filesystem::remove_all(root);
}



void TestSubjectIdentityStore()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    const auto root=std::filesystem::temp_directory_path()/("sara-subject-identity-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    sentinel::SqliteDatabase db;
    db.Open(root/"subjects.db");
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR)/"migrations");

    const auto caseA=sentinel::CaseId::Random();
    const auto caseB=sentinel::CaseId::Random();
    const auto user=sentinel::UserId::Random();

    auto insertCase=[&](const sentinel::CaseId& id,const char* number) {
        sqlite3_stmt* s{};
        if(sqlite3_prepare_v2(db.Handle(),
            "INSERT INTO cases(id,case_number,title,description,status,created_by,created_at,modified_at) "
            "VALUES(?,?,?,'',1,?,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)",
            -1,&s,nullptr)!=SQLITE_OK)
            throw std::runtime_error("prepare test case insert failed");
        const auto idText=id.ToString();
        const auto userText=user.ToString();
        sqlite3_bind_text(s,1,idText.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(s,2,number,-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(s,3,"Identity test",-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(s,4,userText.c_str(),-1,SQLITE_TRANSIENT);
        if(sqlite3_step(s)!=SQLITE_DONE) {
            sqlite3_finalize(s);
            throw std::runtime_error("test case insert failed");
        }
        sqlite3_finalize(s);
    };
    insertCase(caseA,"CASE-A");
    insertCase(caseB,"CASE-B");

    sentinel::identity::SubjectIdentityStore store(db);
    auto subject=store.CreateSubject(caseA,"Known Handle");
    subject.legalName="Possible Legal Name";
    subject.aliases="Alias One, Alias Two";
    subject.usernames="@knownhandle";
    subject.contactIdentifiers="example@example.test";
    subject.notes="Investigator note";
    store.SaveSubject(subject);

    auto other=store.CreateSubject(caseB,"Other Case Subject");
    Require(other.caseId==caseB,"other-case subject case id mismatch");

    auto listA=store.ListForCase(caseA);
    Require(listA.size()==1,"case A subject count mismatch");
    Require(listA.front().displayName=="Known Handle","case A subject display name mismatch");
    Require(listA.front().usernames=="@knownhandle","subject usernames did not persist");

    auto lead=store.AddLead(
        subject.id,
        "public-record",
        "record-123",
        "Possible name/address correlation",
        72,
        "Retrieved from authorized public-record source during case review");
    Require(lead.status==sentinel::identity::IdentityLeadStatus::Lead,
        "identity lead should begin as LEAD");
    Require(lead.confidence==72,"identity lead confidence mismatch");
    Require(!lead.provenance.empty(),"identity lead provenance missing");

    auto counts=store.CountsForCase(caseA);
    Require(counts.subjects==1,"subject count mismatch");
    Require(counts.leads==1,"identity lead count mismatch");
    Require(counts.verifiedLeads==0,"unreviewed lead counted as verified");
    Require(counts.confirmedSubjects==0,"unconfirmed subject counted as confirmed");

    Require(store.ReviewLead(
        lead.id,
        sentinel::identity::IdentityLeadStatus::Verified,
        "unit-investigator",
        "source and correlation reviewed"),
        "identity lead verification failed");
    auto verified=store.GetLead(lead.id);
    Require(verified.has_value(),"verified identity lead could not be reloaded");
    Require(verified->status==sentinel::identity::IdentityLeadStatus::Verified,
        "verified identity lead status mismatch");
    Require(verified->reviewer=="unit-investigator","identity lead reviewer did not persist");

    counts=store.CountsForCase(caseA);
    Require(counts.verifiedLeads==1,"verified identity lead count mismatch");
    Require(counts.confirmedSubjects==0,
        "verifying a lead must not automatically confirm subject identity");

    Require(store.SetSubjectStatus(
        subject.id,
        sentinel::identity::SubjectIdentityStatus::Confirmed),
        "explicit subject identity confirmation failed");
    auto confirmed=store.GetSubject(subject.id);
    Require(confirmed.has_value(),"confirmed subject could not be reloaded");
    Require(confirmed->identityStatus==sentinel::identity::SubjectIdentityStatus::Confirmed,
        "subject confirmation state mismatch");

    counts=store.CountsForCase(caseA);
    Require(counts.confirmedSubjects==1,"confirmed subject count mismatch");

    auto listB=store.ListForCase(caseB);
    Require(listB.size()==1 && listB.front().id==other.id,
        "case scoping leaked subjects across investigations");

    db.Close();
    std::filesystem::remove_all(root);
}


void TestSupervisorStateStore()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    const auto root=std::filesystem::temp_directory_path()/
        ("sara-supervisor-state-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    sentinel::SqliteDatabase db;
    db.Open(root/"supervisor.db");
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR)/"migrations");

    sentinel::operations::SupervisorStateStore store(db);
    Require(!store.Get("case-unit").has_value(),
        "supervisor state should not exist before first control change");

    auto active=store.SetTakeover(
        "case-unit",true,"unit-investigator","manual operational handoff");
    Require(active.investigatorTakeover,"investigator takeover was not activated");
    Require(active.takeoverActor=="unit-investigator","takeover actor mismatch");
    Require(active.takeoverNote=="manual operational handoff","takeover note mismatch");
    Require(!active.takeoverUtc.empty(),"takeover timestamp missing");
    Require(active.releasedBy.empty(),"fresh takeover unexpectedly has release actor");

    auto reloaded=store.Get("case-unit");
    Require(reloaded.has_value(),"persisted takeover state could not be reloaded");
    Require(reloaded->investigatorTakeover,"reloaded takeover state lost active flag");

    auto released=store.SetTakeover("case-unit",false,"unit-investigator");
    Require(!released.investigatorTakeover,"investigator takeover was not released");
    Require(released.releasedBy=="unit-investigator","takeover release actor mismatch");
    Require(!released.releasedUtc.empty(),"takeover release timestamp missing");
    Require(released.takeoverActor=="unit-investigator",
        "takeover activation provenance was lost on release");

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

#ifdef _WIN32
void TestUnifiedChannelCore()
{
    auto RequireChannel=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };
    const auto root = std::filesystem::temp_directory_path() / ("sentinel-channel-test-" + sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    sentinel::SqliteDatabase db;
    db.Open(root/"sentinel.db");
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR)/"migrations");

    sentinel::channels::JurisdictionRuleStore rules(db);
    rules.EnsureBuiltInBaselines();

    auto federal=rules.LatestProfile(sentinel::channels::RuleLayerType::Federal,"US");
    auto louisiana=rules.LatestProfile(sentinel::channels::RuleLayerType::State,"US","LA");
    RequireChannel(federal.has_value(),"federal jurisdiction baseline missing");
    RequireChannel(louisiana.has_value(),"Louisiana jurisdiction baseline missing");
    RequireChannel(!federal->AutomationLegallyActive(),"federal reference baseline should not self-authorize automation");
    RequireChannel(!louisiana->AutomationLegallyActive(),"Louisiana reference baseline should not self-authorize automation");

    rules.SelectForOperation(
        "unit-operation","US","LA",federal->id,louisiana->id,{},"unit-test");
    auto stack=rules.SelectedForOperation("unit-operation");
    RequireChannel(stack.has_value(),"operation rule stack was not persisted");
    RequireChannel(stack->federal.has_value(),"operation rule stack missing federal layer");
    RequireChannel(stack->state.has_value(),"operation rule stack missing state layer");
    RequireChannel(!stack->fullyActive,"draft rule stack unexpectedly marked active");

    sentinel::channels::ChannelCoreStore store(db);
    const auto subject=store.CreateSubject("case-test","Cross-channel subject");
    const auto identity=store.AddSubjectIdentity(
        subject,"phone","twilio","+13375550199","+1 337-555-0199",
        sentinel::channels::IdentityLinkState::Candidate,0.75);
    RequireChannel(store.ConfirmSubjectIdentity(identity,"unit-test"),"subject identity confirmation failed");

    sentinel::channels::ChannelAccount account;
    account.type=sentinel::channels::ChannelType::Sms;
    account.provider="twilio";
    account.externalAccountId="acct-test";
    account.displayName="Sentinel SMS";
    account.address="+13375550100";
    account.capabilities.Set(sentinel::channels::ReceiveText);
    account.capabilities.Set(sentinel::channels::SendText);
    account.capabilities.Set(sentinel::channels::DeliveryReceipts);
    const auto accountId=store.UpsertChannelAccount(account);

    sentinel::channels::ChannelConversation conversation;
    conversation.subjectId=subject;
    conversation.personaName="Casey";
    conversation.channelAccountId=accountId;
    conversation.type=sentinel::channels::ChannelType::Sms;
    conversation.provider="twilio";
    conversation.providerConversationId="sms:+13375550199";
    conversation.externalPeerId="+13375550199";
    const auto conversationId=store.OpenConversation(conversation);

    sentinel::channels::RawChannelEvent event;
    event.id=sentinel::Uuid::Random().ToString();
    event.channelConversationId=conversationId;
    event.provider="twilio";
    event.type=sentinel::channels::ChannelType::Sms;
    event.providerAccountId="acct-test";
    event.providerConversationId="sms:+13375550199";
    event.providerMessageId="SM-unit-1";
    event.eventType="message.received";
    event.direction=sentinel::channels::Direction::Inbound;
    event.senderExternalId="+13375550199";
    event.recipientExternalId="+13375550100";
    event.rawPayload="{\"Body\":\"hello\"}";
    event.payloadSha256=std::string(64,'a');
    store.RecordRawEvent(event);

    sentinel::channels::NormalizedMessage message;
    message.id=sentinel::Uuid::Random().ToString();
    message.eventId=event.id;
    message.channelConversationId=conversationId;
    message.subjectId=subject;
    message.personaName="Casey";
    message.direction=sentinel::channels::Direction::Inbound;
    message.senderExternalId=event.senderExternalId;
    message.recipientExternalId=event.recipientExternalId;
    message.body="hello";
    message.automationMode=sentinel::channels::AutomationMode::ApprovalRequired;
    message.providerMessageId=event.providerMessageId;
    store.RecordMessage(message);

    auto found=store.FindConversation("twilio",accountId,"sms:+13375550199");
    RequireChannel(found.has_value(),"channel conversation lookup failed");
    RequireChannel(found->subjectId==subject,"channel conversation subject mismatch");

    auto recent=store.RecentMessages(conversationId,10);
    RequireChannel(recent.size()==1,"normalized message count mismatch");
    RequireChannel(recent[0].body=="hello","normalized message body mismatch");
    RequireChannel(recent[0].subjectId==subject,"normalized message subject mismatch");

    db.Close();
    std::filesystem::remove_all(root);
}
#endif

}

int main()
{
    try {
        std::cout << "[core] ids/hashes..." << std::endl;
        TestIdsAndHashes();
        std::cout << "[core] ids/hashes PASS" << std::endl;
        std::cout << "[core] training review recent list..." << std::endl;
        TestTrainingReviewRecentList();
        std::cout << "[core] training review recent list PASS" << std::endl;
        std::cout << "[core] persona LoRA history..." << std::endl;
        TestPersonaLoraHistory();
        std::cout << "[core] persona LoRA history PASS" << std::endl;
        std::cout << "[core] trainer foundation/jobs..." << std::endl;
        TestTrainerFoundationAndJobs();
        std::cout << "[core] trainer foundation/jobs PASS" << std::endl;
        std::cout << "[core] model registry lifecycle..." << std::endl;
        TestModelRegistryLifecycle();
        std::cout << "[core] model registry lifecycle PASS" << std::endl;
        std::cout << "[core] persona-scoped conversation memory..." << std::endl;
        TestPersonaScopedConversationMemory();
        std::cout << "[core] persona-scoped conversation memory PASS" << std::endl;
        std::cout << "[core] persistent evaluation suite..." << std::endl;
        TestPersistentEvaluationSuite();
        std::cout << "[core] persistent evaluation suite PASS" << std::endl;
        std::cout << "[core] deployment registry lifecycle..." << std::endl;
        TestDeploymentRegistryLifecycle();
        std::cout << "[core] deployment registry lifecycle PASS" << std::endl;
        std::cout << "[core] versioned dataset snapshots..." << std::endl;
        TestVersionedDatasetSnapshots();
        std::cout << "[core] versioned dataset snapshots PASS" << std::endl;
        std::cout << "[core] subject identity store..." << std::endl;
        TestSubjectIdentityStore();
        std::cout << "[core] subject identity store PASS" << std::endl;
        std::cout << "[core] supervisor takeover state..." << std::endl;
        TestSupervisorStateStore();
        std::cout << "[core] supervisor takeover state PASS" << std::endl;
#ifdef _WIN32
        std::cout << "[core] windows crypto/evidence..." << std::endl;
        TestWindowsCryptoAndSev();
        std::cout << "[core] windows crypto/evidence PASS" << std::endl;
        std::cout << "[core] unified channel/jurisdiction..." << std::endl;
        TestUnifiedChannelCore();
        std::cout << "[core] unified channel/jurisdiction PASS" << std::endl;
#endif
        std::cout << "SentinelCoreTests passed" << std::endl;
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "SentinelCoreTests exception: " << e.what() << std::endl;
        return 2;
    } catch(...) {
        std::cerr << "SentinelCoreTests unknown exception" << std::endl;
        return 3;
    }
}
