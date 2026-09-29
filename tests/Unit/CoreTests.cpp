#include "Sentinel/Core/Types.hpp"
#include "Sentinel/Evidence/SevContainer.hpp"
#include "Sentinel/Evidence/EvidenceService.hpp"
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
#include "Sentinel/Simulation/ResponseRuleMatcher.hpp"
#include "Sentinel/Simulation/ResponseRuleMatchLog.hpp"
#include "Sentinel/Simulation/DeploymentRegistry.hpp"
#include "Sentinel/Simulation/TrainingData.hpp"
#include "Sentinel/Identity/SubjectIdentityStore.hpp"
#include "Sentinel/Identity/IdentityResearchProviderAdapter.hpp"
#include "Sentinel/Identity/IdentityResearchPackage.hpp"
#include "Sentinel/Operations/SupervisorStateStore.hpp"
#include "Sentinel/Agency/AgencyServer.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {



void TestResponseRuleMatchLog()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    const auto root=std::filesystem::temp_directory_path()/
        ("sara-rule-log-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);

    sentinel::SqliteDatabase db;
    db.Open(root/"rule-log.db");
    sentinel::MigrationService migrations(db);
    migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR)/"migrations");

    sentinel::simulation::ResponseRuleMatchLog log(db);
    const auto first=log.Append(
        "Samantha","conversation-a",42,"exact",100,
        "favorite color","what is your favorite color","persona_variation","blue mostly");
    const auto second=log.Append(
        "Samantha","conversation-b",42,"smart",88,
        "favorite color","whats ur fav color","persona_variation","probably blue");
    log.Append(
        "Nikki","conversation-c",77,"contains",95,
        "music","what music do you like","exact","rock");

    Require(first>0 && second>first,"response-rule match ids were not monotonic");
    Require(log.CountForRule("Samantha",42)==2,
        "response-rule match count mismatch");
    Require(log.CountForRule("Nikki",42)==0,
        "response-rule match count leaked across personas");

    const auto recent=log.Recent("Samantha",10);
    Require(recent.size()==2,"response-rule recent log count mismatch");
    Require(recent.front().id==second,"response-rule recent log ordering mismatch");
    Require(recent.front().matchScore==88,"response-rule match score mismatch");
    Require(recent.front().outputText=="probably blue",
        "response-rule output log mismatch");

    db.Close();
    std::filesystem::remove_all(root);
}

void TestResponseRuleMatcher()
{
    using sentinel::simulation::EvaluateResponseRuleMatch;
    using sentinel::simulation::PreferResponseRuleMatch;

    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    const auto exact=EvaluateResponseRuleMatch(
        "exact","What's your favorite color?","whats your favorite color");
    Require(exact.Matched() && exact.score==100 && exact.typeRank==3,
        "exact response-rule normalization/match failed");

    const auto contains=EvaluateResponseRuleMatch(
        "contains","hey so what is your favorite color today","favorite color");
    Require(contains.Matched() && contains.score==95 && contains.typeRank==2,
        "contains response-rule match failed");

    const auto smart=EvaluateResponseRuleMatch(
        "smart","whats your favrite color","what is your favorite color");
    Require(smart.Matched() && smart.typeRank==1,
        "smart response-rule typo/contraction match failed");

    const auto unrelated=EvaluateResponseRuleMatch(
        "smart","what time is school","favorite color");
    Require(!unrelated.Matched(),
        "smart response rule matched unrelated input");

    Require(PreferResponseRuleMatch(100,10,exact,100,20,smart),
        "exact rule should outrank smart rule at equal priority");
    Require(!PreferResponseRuleMatch(100,20,smart,100,10,exact),
        "smart rule should not outrank exact rule at equal priority");

    const auto exactTie=EvaluateResponseRuleMatch(
        "exact","hello there","hello there");
    Require(PreferResponseRuleMatch(100,22,exactTie,100,21,exactTie),
        "newer rule should win a true equal-priority/equal-quality tie");
    Require(!PreferResponseRuleMatch(99,99,exactTie,100,1,contains),
        "higher explicit priority must outrank match type");

    const auto variants=sentinel::simulation::SplitResponseRuleVariants(
        " blue mostly || probably blue ||  blue is my favorite  ");
    Require(variants.size()==3,"response-rule alternate parsing count mismatch");
    Require(variants[0]=="blue mostly" && variants[2]=="blue is my favorite",
        "response-rule alternate trimming mismatch");

    const auto selectedA=sentinel::simulation::SelectResponseRuleVariant(
        "one || two || three","conversation-a|rule-7");
    const auto selectedB=sentinel::simulation::SelectResponseRuleVariant(
        "one || two || three","conversation-a|rule-7");
    Require(selectedA==selectedB,
        "response-rule alternate selection was not deterministic");
    Require(selectedA=="one" || selectedA=="two" || selectedA=="three",
        "response-rule alternate selection returned an invalid variant");
}

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

    db.Execute("UPDATE training_review_items SET status=1,reviewer='unit' WHERE id='review-new';");
    const auto personaExport=root/"samantha-approved.jsonl";
    const auto exported=reviews.ExportApprovedJsonlForPersona(personaExport,"Samantha");
    Require(exported==1,"persona-scoped approved export count mismatch");
    {
        std::ifstream personaIn(personaExport,std::ios::binary);
        const std::string personaJson(
            (std::istreambuf_iterator<char>(personaIn)),
            std::istreambuf_iterator<char>());
        Require(personaJson.find("\"persona_name\":\"Samantha\"")!=std::string::npos,
            "persona-scoped export omitted the selected persona");
        Require(personaJson.find("Nikki")==std::string::npos,
            "persona-scoped export leaked another persona into the dataset");
    }

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

    const auto previous=trainer.PreviousPersonaLora("Samantha");
    Require(previous.has_value() && previous->id==first.id,
        "persona LoRA activation history did not preserve the prior binding");
    Require(trainer.RollbackPersonaLora("Samantha"),
        "persona LoRA rollback failed");
    const auto rolledBack=trainer.ResolvePersonaLora("Samantha");
    Require(rolledBack.has_value(),"rolled-back persona LoRA did not resolve");
    Require(rolledBack->id==first.id,"exact persona LoRA version was not restored by rollback");

    const auto nikki=trainer.BindPersonaLora(
        "Nikki",foundations.front().id,"Nikki v1","nikki-v1.gguf",1.0);
    Require(nikki.active,"Nikki persona LoRA should be active");
    const auto samanthaStill=trainer.ResolvePersonaLora("Samantha");
    Require(samanthaStill.has_value() && samanthaStill->id==first.id,
        "persona LoRA activation leaked across personas");

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
    Require(fork.version==foundations.front().version+1,
        "foundation fork version should increment from its parent");
    Require(fork.status=="DRAFT","new foundation fork should be draft");
    Require(trainer.ApproveFoundation(fork.id),"foundation approval failed");
    Require(trainer.ActivateFoundation(fork.id),"approved foundation activation failed");

    const auto activeFork=trainer.GetFoundation(fork.id);
    Require(activeFork.has_value() && activeFork->status=="ACTIVE",
        "selected foundation did not become active");
    const auto priorBase=trainer.GetFoundation(foundations.front().id);
    Require(priorBase.has_value() && priorBase->status=="APPROVED",
        "previous active foundation was not preserved as approved");
    const auto previous=trainer.PreviousFoundation();
    Require(previous.has_value() && previous->id==foundations.front().id,
        "foundation activation history did not preserve previous runtime");
    Require(trainer.RollbackFoundation(),"foundation rollback failed");
    const auto rolledBackBase=trainer.GetFoundation(foundations.front().id);
    const auto rolledBackFork=trainer.GetFoundation(fork.id);
    Require(rolledBackBase.has_value() && rolledBackBase->status=="ACTIVE",
        "foundation rollback did not reactivate the previous base");
    Require(rolledBackFork.has_value() && rolledBackFork->status=="APPROVED",
        "foundation rollback did not preserve the replaced fork");
    Require(trainer.ActivateFoundation(fork.id),
        "foundation could not be reactivated after rollback");

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

    Require(trainer.CancelQueuedJob(job.id),
        "queued trainer job could not be cancelled");
    auto cancelledJob=trainer.GetJob(job.id);
    Require(cancelledJob.has_value() && cancelledJob->state=="CANCELLED",
        "cancelled trainer job state mismatch");
    Require(!cancelledJob->completedUtc.empty(),
        "cancelled trainer job completion timestamp missing");
    Require(!trainer.CancelQueuedJob(job.id),
        "cancelled trainer job should not cancel twice");

    Require(trainer.RetryJob(job.id),
        "cancelled trainer job could not be retried");
    auto retriedJob=trainer.GetJob(job.id);
    Require(retriedJob.has_value() && retriedJob->state=="QUEUED",
        "retried trainer job did not return to queue");
    Require(retriedJob->progress==0 && retriedJob->startedUtc.empty() &&
            retriedJob->completedUtc.empty() && retriedJob->errorText.empty(),
        "retried trainer job did not reset worker state");

    db.Execute(
        "UPDATE trainer_jobs SET state='FAILED',progress=64,"
        "started_utc=CURRENT_TIMESTAMP,completed_utc=CURRENT_TIMESTAMP,"
        "error_text='unit failure' WHERE id='"+job.id+"';");
    Require(trainer.RetryJob(job.id),
        "failed trainer job could not be retried");
    auto failedRetry=trainer.GetJob(job.id);
    Require(failedRetry.has_value() && failedRetry->state=="QUEUED" &&
            failedRetry->progress==0 && failedRetry->errorText.empty() &&
            failedRetry->startedUtc.empty() && failedRetry->completedUtc.empty(),
        "failed trainer job retry did not reset state");
    Require(!trainer.RetryJob(job.id),
        "queued trainer job should not be retryable");

    db.Execute(
        "UPDATE trainer_jobs SET state='RUNNING',progress=31,"
        "started_utc=datetime('now','-10 minutes'),"
        "heartbeat_utc=datetime('now','-10 minutes'),worker_pid=4242 "
        "WHERE id='"+job.id+"';");
    Require(trainer.RecoverStaleRunningJobs(3)==1,
        "stale running trainer job was not recovered");
    auto staleJob=trainer.GetJob(job.id);
    Require(staleJob.has_value() && staleJob->state=="FAILED" &&
            staleJob->workerPid==0 &&
            staleJob->errorText.find("heartbeat expired")!=std::string::npos,
        "stale trainer job recovery state mismatch");
    Require(trainer.RetryJob(job.id),
        "recovered stale trainer job could not be retried");

    db.Execute(
        "UPDATE trainer_jobs SET state='RUNNING',progress=32,"
        "started_utc=CURRENT_TIMESTAMP,heartbeat_utc=CURRENT_TIMESTAMP,worker_pid=4343 "
        "WHERE id='"+job.id+"';");
    Require(trainer.RecoverStaleRunningJobs(3)==0,
        "healthy running trainer job was incorrectly marked stale");

    const auto dialogue=trainer.EnsureDialogueSession(
        "Samantha",sentinel::simulation::TrainingMode::Behavior);
    Require(!dialogue.id.empty(),"trainer dialogue session id missing");
    Require(dialogue.active,"trainer dialogue session should be active");

    const auto operatorTurn=trainer.AppendDialogueTurn(
        dialogue.id,"OPERATOR","make her writing more casual");
    const auto previewTurn=trainer.AppendDialogueTurn(
        dialogue.id,"SARA","preview generated",
        "PERSONALITY=Balanced\nWRITING_STYLE=Casual\n");
    Require(operatorTurn.id>0 && previewTurn.id>operatorTurn.id,
        "trainer dialogue turn ids were not ordered");

    auto dialogueTurns=trainer.ListDialogueTurns(dialogue.id,10);
    Require(dialogueTurns.size()==2,"trainer dialogue turn count mismatch");
    Require(dialogueTurns.front().role=="OPERATOR",
        "trainer dialogue list should be chronological");
    Require(dialogueTurns.back().payload.find("WRITING_STYLE=Casual")!=std::string::npos,
        "trainer dialogue preview payload missing");

    Require(trainer.MarkDialogueTurnApplied(previewTurn.id),
        "trainer dialogue preview could not be marked applied");
    dialogueTurns=trainer.ListDialogueTurns(dialogue.id,10);
    Require(dialogueTurns.back().applied,
        "trainer dialogue applied state was not persisted");

    const auto replacementSession=trainer.NewDialogueSession(
        "Samantha",sentinel::simulation::TrainingMode::Behavior,"Second review");
    Require(replacementSession.id!=dialogue.id,
        "new trainer dialogue session reused the prior id");
    auto activeDialogue=trainer.ActiveDialogueSession(
        "Samantha",sentinel::simulation::TrainingMode::Behavior);
    Require(activeDialogue.has_value() && activeDialogue->id==replacementSession.id,
        "new trainer dialogue session did not become active");

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
    memory.Append(samanthaOld,sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
        "Do you have any pets?");
    memory.Append(samanthaOld,sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
        "What's your favorite color?");
    memory.Append(samanthaOld,sentinel::simulation::ChatTurn::Speaker::Investigator,
        "purple");

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
    memory.Append(nikkiOld,sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
        "What kind of music do you like?");
    memory.Append(nikkiOld,sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
        "What's your favorite food?");
    memory.Append(nikkiOld,sentinel::simulation::ChatTurn::Speaker::Investigator,
        "tacos");

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

    const auto samanthaFacts=memory.RecallParticipantFacts(samanthaCurrent,20);
    Require(samanthaFacts.find("favorite color")!=std::string::npos &&
            samanthaFacts.find("purple")!=std::string::npos,
        "Samantha short answer lost the question that gave it meaning");
    Require(samanthaFacts.find("tacos")==std::string::npos,
        "Samantha participant facts leaked Nikki short answers");

    const auto nikkiFacts=memory.RecallParticipantFacts(nikkiCurrent,20);
    Require(nikkiFacts.find("favorite food")!=std::string::npos &&
            nikkiFacts.find("tacos")!=std::string::npos,
        "Nikki short answer lost the question that gave it meaning");
    Require(nikkiFacts.find("purple")==std::string::npos,
        "Nikki participant facts leaked Samantha short answers");

    const auto samanthaQuestions=memory.RecallQuestionHistory(samanthaCurrent,20);
    Require(samanthaQuestions.find("pets")!=std::string::npos,
        "Samantha question history missed a previously asked question");
    Require(samanthaQuestions.find("music")==std::string::npos,
        "Samantha question history leaked Nikki questions");

    const auto nikkiQuestions=memory.RecallQuestionHistory(nikkiCurrent,20);
    Require(nikkiQuestions.find("music")!=std::string::npos,
        "Nikki question history missed a previously asked question");
    Require(nikkiQuestions.find("pets")==std::string::npos,
        "Nikki question history leaked Samantha questions");

    db.Execute(
        "INSERT INTO persona_learned_notes(persona_name,conversation_id,source_kind,note_text) VALUES"
        "('Samantha','" + samanthaOld + "','reactive_persona_claim','I always pick strawberry ice cream.'),"
        "('Nikki','" + nikkiOld + "','proactive_persona_claim','I always pick mint ice cream.');");
    const auto samanthaLearned=memory.RecallLearnedPersonaNotes("Samantha",20);
    Require(samanthaLearned.find("strawberry")!=std::string::npos,
        "Samantha learned-note recall missed persona continuity");
    Require(samanthaLearned.find("mint")==std::string::npos,
        "Samantha learned-note recall leaked Nikki continuity");
    const auto nikkiLearned=memory.RecallLearnedPersonaNotes("Nikki",20);
    Require(nikkiLearned.find("mint")!=std::string::npos,
        "Nikki learned-note recall missed persona continuity");
    Require(nikkiLearned.find("strawberry")==std::string::npos,
        "Nikki learned-note recall leaked Samantha continuity");

    sentinel::simulation::ModelContext loaded;
    Require(memory.Load(samanthaOld,loaded),"failed to load Samantha conversation");
    Require(loaded.personaSummary.find("Samantha")!=std::string::npos,
        "loaded persona summary lost persona identity");
    Require(loaded.personaSummary.find("playful")!=std::string::npos,
        "loaded persona summary lost persona details");

    auto fallback=sentinel::simulation::CreateRuleBasedTestModel();
    sentinel::simulation::ModelContext initiativeContext;
    initiativeContext.recalledMemory=
        "Questions this same persona already asked:\nDo you have any pets?\n";
    initiativeContext.variationSeed=2;
    const auto initiative=fallback->GenerateSyntheticInitiative(initiativeContext);
    Require(initiative.find("pets")==std::string::npos,
        "fallback initiative repeated a previously asked pets question");

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
    Require(sentinel::simulation::EvaluationPassedApprovalGate(first),
        "complete passing evaluation did not satisfy approval gate");

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
    Require(!sentinel::simulation::EvaluationPassedApprovalGate(second),
        "failed evaluation dimension incorrectly passed approval gate");

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




void TestIdentityResearchProviderAdapters()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    sentinel::identity::IdentityResearchProviderAdapterRegistry registry;
    registry.Register(std::make_unique<
        sentinel::identity::ManualIdentityResearchProviderAdapter>());

    Require(registry.Find("manual/authorized")!=nullptr,
        "manual identity research adapter was not registered");
    Require(!registry.CanExecute(
        "manual/authorized",
        sentinel::identity::IdentityResearchType::PublicRecords),
        "manual identity research adapter must never report automatic execution readiness");
    Require(registry.ExecutableCount()==0,
        "manual identity research adapter was counted as executable");

    struct UnitAdapter final
        : sentinel::identity::IIdentityResearchProviderAdapter {
        std::string ProviderId() const override { return "unit-username-provider"; }
        bool Configured() const noexcept override { return true; }
        bool Supports(sentinel::identity::IdentityResearchType type) const noexcept override {
            return type==sentinel::identity::IdentityResearchType::Username;
        }
        sentinel::identity::IdentityResearchExecutionResult Execute(
            const sentinel::identity::IdentityResearchExecutionRequest& request) override {
            sentinel::identity::IdentityResearchExecutionResult out;
            out.completed=true;
            out.resultSummary="unit result for "+request.queryText;
            out.resultReference="unit://result";
            out.provenance="unit adapter";
            return out;
        }
    };

    registry.Register(std::make_unique<UnitAdapter>());
    Require(registry.ExecutableCount()==1,
        "configured identity research adapter count mismatch");
    Require(registry.CanExecute(
        "unit-username-provider",
        sentinel::identity::IdentityResearchType::Username),
        "configured username adapter was not executable for its supported type");
    Require(!registry.CanExecute(
        "unit-username-provider",
        sentinel::identity::IdentityResearchType::PublicRecords),
        "username adapter incorrectly reported public-record support");

    bool duplicateRejected=false;
    try {
        registry.Register(std::make_unique<UnitAdapter>());
    } catch(const std::exception&) {
        duplicateRejected=true;
    }
    Require(duplicateRejected,
        "duplicate identity research adapter provider id was accepted");
}


void TestIdentityResearchPackages()
{
    auto Require=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    sentinel::identity::IdentityResearchRequestPackage request;
    request.taskId="task-123";
    request.caseId="case-456";
    request.subjectId="subject-789";
    request.subjectDisplayName="Example Subject";
    request.researchType="PUBLIC RECORDS";
    request.providerId="agency-public-records";
    request.providerDisplayName="Agency Public Records Connector";
    request.accessMode="API";
    request.endpointHint="https://provider.example/";
    request.queryText="reference value\nwith second line";
    request.purpose="authorized case purpose";
    request.createdUtc="2026-09-29T01:00:00Z";

    const auto requestText=
        sentinel::identity::SerializeResearchRequestPackage(request);
    Require(requestText.find("credential_reference\t")==std::string::npos,
        "research request package unexpectedly contains a credential-reference field");
    Require(requestText.find("api_key\t")==std::string::npos &&
            requestText.find("password\t")==std::string::npos &&
            requestText.find("bearer_token\t")==std::string::npos,
        "research request package unexpectedly contains secret fields");
    const auto parsedRequest=
        sentinel::identity::ParseResearchRequestPackage(requestText);
    Require(parsedRequest.taskId==request.taskId,
        "research request task id did not round-trip");
    Require(parsedRequest.queryText==request.queryText,
        "research request escaped query did not round-trip");

    sentinel::identity::IdentityResearchResultPackage result;
    result.taskId=request.taskId;
    result.providerId=request.providerId;
    result.resultSummary="possible correlation only";
    result.resultReference="provider-ref-42";
    result.provenance="authorized provider export 2026-09-29";

    const auto resultText=
        sentinel::identity::SerializeResearchResultPackage(result);
    const auto parsedResult=
        sentinel::identity::ParseResearchResultPackage(resultText);
    Require(parsedResult.taskId==result.taskId,
        "research result task id did not round-trip");
    Require(parsedResult.providerId==result.providerId,
        "research result provider id did not round-trip");
    Require(parsedResult.provenance==result.provenance,
        "research result provenance did not round-trip");

    bool malformedRejected=false;
    try {
        (void)sentinel::identity::ParseResearchResultPackage(
            "SARA_IDENTITY_RESEARCH_RESULT_V1\ntask_id\ttask-123\nprovider_id\tagency-public-records\n");
    } catch(...) {
        malformedRejected=true;
    }
    Require(malformedRejected,
        "research result without finding/provenance was not rejected");
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

    const auto defaultProviders=store.ListResearchProviders(false);
    Require(defaultProviders.size()==1,
        "only the manual identity research provider should be enabled by default");
    Require(defaultProviders.front().id=="manual/authorized",
        "manual identity research provider baseline missing");
    Require(defaultProviders.front().Supports(
        sentinel::identity::IdentityResearchType::ImageReference),
        "manual identity research provider should support all research types");

    auto configuredProvider=store.SaveResearchProvider({
        "unit-username-provider",
        "Unit Username Provider",
        sentinel::identity::IdentityResearchAccessMode::Api,
        1u<<static_cast<unsigned int>(sentinel::identity::IdentityResearchType::Username),
        "https://provider.example.test",
        "secret-ref/unit-provider",
        true,
        "Unit test provider metadata only"
    });
    Require(configuredProvider.enabled,"configured provider should be enabled");
    Require(configuredProvider.credentialReference=="secret-ref/unit-provider",
        "provider credential reference did not persist");
    Require(configuredProvider.Supports(sentinel::identity::IdentityResearchType::Username),
        "configured provider username capability missing");
    Require(!configuredProvider.Supports(sentinel::identity::IdentityResearchType::PublicRecords),
        "configured provider unexpectedly supports public records");

    auto research=store.QueueResearch(
        subject.id,
        sentinel::identity::IdentityResearchType::Username,
        configuredProvider.id,
        "@knownhandle",
        "Correlate a public username to additional public profiles for CASE-A");
    Require(research.status==sentinel::identity::IdentityResearchStatus::Queued,
        "identity research should begin queued");
    Require(research.subjectId==subject.id,"identity research subject mismatch");

    auto researchList=store.ListResearch(subject.id);
    Require(researchList.size()==1,"identity research list count mismatch");
    Require(researchList.front().queryText=="@knownhandle",
        "identity research query did not persist");

    Require(store.CompleteResearch(
        research.id,
        "Possible public profile correlation",
        "https://example.test/profile/knownhandle",
        "Manual review of an authorized public source"),
        "identity research completion failed");

    auto completedResearch=store.GetResearch(research.id);
    Require(completedResearch.has_value(),"completed identity research missing");
    Require(completedResearch->status==sentinel::identity::IdentityResearchStatus::Completed,
        "identity research completion status mismatch");

    auto promoted=store.PromoteResearchToLead(
        research.id,61,"unit-investigator",
        "Promoted for later identity-lead verification");
    Require(promoted.status==sentinel::identity::IdentityLeadStatus::Lead,
        "promoted research must remain an unverified lead");
    Require(promoted.confidence==61,"promoted research confidence mismatch");

    completedResearch=store.GetResearch(research.id);
    Require(completedResearch->status==sentinel::identity::IdentityResearchStatus::PromotedToLead,
        "identity research promotion state mismatch");
    Require(completedResearch->promotedLeadId==promoted.id.ToString(),
        "identity research promoted lead id mismatch");

    const auto researchReport=store.BuildResearchReport(research.id);
    Require(researchReport.find("SARA IDENTITY RESEARCH REPORT")!=std::string::npos,
        "identity research report header missing");
    Require(researchReport.find("Possible public profile correlation")!=std::string::npos,
        "identity research report omitted the finding");
    Require(researchReport.find("does not establish or automatically confirm")!=std::string::npos,
        "identity research report omitted the non-confirmation notice");

    Require(store.SetResearchProviderEnabled(configuredProvider.id,false),
        "configured identity research provider could not be disabled");
    bool disabledProviderRejected=false;
    try {
        (void)store.QueueResearch(
            subject.id,
            sentinel::identity::IdentityResearchType::Username,
            configuredProvider.id,
            "@blocked",
            "Disabled provider must not be usable");
    } catch(const std::exception&) {
        disabledProviderRejected=true;
    }
    Require(disabledProviderRejected,
        "disabled registered identity research provider was accepted");

    auto rejectTask=store.QueueResearch(
        subject.id,
        sentinel::identity::IdentityResearchType::ImageReference,
        "manual-visual-review",
        "case-image-reference-1",
        "Compare a case-authorized image reference against public material");
    Require(store.RejectResearch(
        rejectTask.id,"unit-investigator","insufficient similarity"),
        "identity research rejection failed");
    auto rejectedResearch=store.GetResearch(rejectTask.id);
    Require(rejectedResearch.has_value() &&
            rejectedResearch->status==sentinel::identity::IdentityResearchStatus::Rejected,
        "identity research rejection state mismatch");

    auto listB=store.ListForCase(caseB);
    Require(listB.size()==1 && listB.front().id==other.id,
        "case scoping leaked subjects across investigations");

    Require(store.DeleteSubject(subject.id),
        "subject with identity research tasks could not be deleted");
    Require(store.ListForCase(caseA).empty(),
        "deleted subject still appears in case subject list");
    Require(store.ListResearch(subject.id).empty(),
        "identity research tasks survived subject deletion");

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


void TestAgencyServerStore()
{
    auto RequireAgency=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };

    const auto root=std::filesystem::temp_directory_path()/
        ("sara-agency-store-"+sentinel::Uuid::Random().ToString());
    std::filesystem::create_directories(root);
    const auto dbPath=root/"agency.db";

    std::string firstId;
    {
        sentinel::SqliteDatabase db;
        db.Open(dbPath);
        sentinel::MigrationService migrations(db);
        migrations.ApplyDirectory(std::filesystem::path(SENTINEL_SOURCE_DIR)/"migrations");

        sentinel::agency::AgencyServerStore store(db);
        auto config=store.LoadConfig();
        RequireAgency(config.workstationId=="local-workstation",
            "default agency workstation ID missing");

        config.endpoint="https://agency.example.invalid/api";
        config.agencyId="unit-agency";
        config.workstationId="unit-workstation";
        config.enabled=true;
        store.SaveConfig(config);

        auto first=store.Enqueue({
            {},sentinel::agency::SyncItemType::AuditRecord,"audit-count:7",0,false});
        auto second=store.Enqueue({
            {},sentinel::agency::SyncItemType::EvidenceManifest,"evidence-manifest:case-a",0,false});
        firstId=first.id;
        RequireAgency(!first.id.empty() && !second.id.empty(),
            "persistent agency queue IDs were not generated");
        RequireAgency(store.PendingCount()==2,
            "persistent agency queue pending count mismatch");

        db.Close();
    }

    {
        sentinel::SqliteDatabase db;
        db.Open(dbPath);
        sentinel::agency::AgencyServerStore store(db);

        const auto config=store.LoadConfig();
        RequireAgency(config.endpoint=="https://agency.example.invalid/api",
            "agency endpoint did not persist");
        RequireAgency(config.agencyId=="unit-agency","agency ID did not persist");
        RequireAgency(config.workstationId=="unit-workstation",
            "agency workstation ID did not persist");
        RequireAgency(config.enabled,"agency enabled state did not persist");

        const auto items=store.Items(10);
        RequireAgency(items.size()==2,"agency sync queue did not persist across reopen");
        RequireAgency(store.PendingCount()==2,
            "reopened agency sync queue pending count mismatch");
        RequireAgency(store.MarkComplete(firstId),
            "agency sync queue completion update failed");
        RequireAgency(store.PendingCount()==1,
            "completed agency sync item remained pending");

        db.Close();
    }

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

    sentinel::AuditService audit(db,hash);
    auto RequireAudit=[](bool value,const char* message) {
        if(!value) throw std::runtime_error(message);
    };
    const auto verificationActor=sentinel::UserId::Random();

    // Simulate one immutable pre-0027 audit-v1 row. Mixed v1/v2 chains must
    // remain verifiable after the metadata-binding migration.
    {
        const std::string timestamp="2026-09-28T00:00:00.000Z";
        const std::string actor=verificationActor.ToString();
        const std::string previous(64,'0');
        const std::string canonical=
            "audit-v1|1|"+timestamp+"|"+actor+"|1|legacy||"+previous;
        const std::vector<std::byte> canonicalBytes(
            reinterpret_cast<const std::byte*>(canonical.data()),
            reinterpret_cast<const std::byte*>(canonical.data()+canonical.size()));
        const auto recordHash=hash.Sha256(canonicalBytes).ToHex();
        const auto legacyId=sentinel::AuditId::Random().ToString();

        sqlite3_stmt* legacy{};
        RequireAudit(sqlite3_prepare_v2(
            db.Handle(),
            "INSERT INTO audit_records("
            "id,sequence,timestamp,actor_id,action,target_type,target_id,metadata,"
            "previous_hash,record_hash,metadata_sha256"
            ") VALUES(?1,1,?2,?3,1,'legacy','',X'6c6567616379',?4,?5,'')",
            -1,&legacy,nullptr)==SQLITE_OK,
            "audit-v1 compatibility row prepare failed");
        sqlite3_bind_text(legacy,1,legacyId.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(legacy,2,timestamp.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(legacy,3,actor.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(legacy,4,previous.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(legacy,5,recordHash.c_str(),-1,SQLITE_TRANSIENT);
        RequireAudit(sqlite3_step(legacy)==SQLITE_DONE,"audit-v1 compatibility row insert failed");
        sqlite3_finalize(legacy);
        RequireAudit(audit.VerifyChain(),"legacy audit-v1 row did not verify after migration");
    }

    sentinel::EvidenceService evidenceService(root/"evidence",db,random,hash,cipher,audit);
    const auto imported=evidenceService.Import(
        {caseId,source,0,verificationActor},
        loadedCaseKey.Span());

    auto importedItems=evidenceService.ListForCase(caseId,loadedCaseKey.Span());
    RequireAudit(importedItems.size()==1,"evidence import/list failed in audit-v2 regression");
    auto verificationStatus=evidenceService.LastVerification(importedItems.front().id);
    RequireAudit(verificationStatus.state==sentinel::EvidenceVerificationState::Never,"new evidence unexpectedly had verification state");

    const auto verified=evidenceService.Verify(
        importedItems.front(),
        loadedCaseKey.Span(),
        verificationActor);
    RequireAudit(verified.valid,"valid evidence did not verify under audit-v2");
    RequireAudit(verified.structureValid,"SEV structure validation did not pass");
    RequireAudit(verified.containerHashMatches,"container hash comparison did not pass");
    RequireAudit(verified.authenticated,"AES-GCM authentication did not pass");
    RequireAudit(verified.plaintextHashMatches,"plaintext hash comparison did not pass");
    verificationStatus=evidenceService.LastVerification(importedItems.front().id);
    RequireAudit(verificationStatus.state==sentinel::EvidenceVerificationState::Verified,"verification PASS state was not persisted in audit history");
    RequireAudit(!verificationStatus.checkedUtc.empty(),"verification timestamp missing");

    {
        std::fstream io(imported.storedPath,std::ios::binary|std::ios::in|std::ios::out);
        const auto offset=static_cast<std::streamoff>(sizeof(sentinel::SevHeaderV1)+12);
        io.seekg(offset);
        char byte{};
        io.read(&byte,1);
        byte^=0x01;
        io.seekp(offset);
        io.write(&byte,1);
    }

    const auto rejected=evidenceService.Verify(
        importedItems.front(),
        loadedCaseKey.Span(),
        verificationActor);
    RequireAudit(!rejected.valid,"tampered evidence was not rejected");
    verificationStatus=evidenceService.LastVerification(importedItems.front().id);
    RequireAudit(verificationStatus.state==sentinel::EvidenceVerificationState::Failed,"integrity-failure state was not persisted");
    RequireAudit(audit.VerifyChain(),"mixed legacy-v1/new-v2 audit chain did not verify");

    // New audit-v2 records must cryptographically bind the metadata blob.
    sqlite3_stmt* latestAudit{};
    RequireAudit(sqlite3_prepare_v2(
        db.Handle(),
        "SELECT id,metadata_sha256 FROM audit_records ORDER BY sequence DESC LIMIT 1",
        -1,&latestAudit,nullptr)==SQLITE_OK,
        "latest audit-v2 lookup prepare failed");
    RequireAudit(sqlite3_step(latestAudit)==SQLITE_ROW,"latest audit-v2 row not found");
    const std::string latestAuditId=(const char*)sqlite3_column_text(latestAudit,0);
    const std::string metadataDigest=(const char*)sqlite3_column_text(latestAudit,1);
    sqlite3_finalize(latestAudit);
    RequireAudit(metadataDigest.size()==64,"audit-v2 metadata digest was not stored");

    sqlite3_stmt* tamperAudit{};
    RequireAudit(sqlite3_prepare_v2(
        db.Handle(),
        "UPDATE audit_records SET metadata=X'01' WHERE id=?1",
        -1,&tamperAudit,nullptr)==SQLITE_OK,
        "audit metadata tamper update prepare failed");
    sqlite3_bind_text(tamperAudit,1,latestAuditId.c_str(),-1,SQLITE_TRANSIENT);
    RequireAudit(sqlite3_step(tamperAudit)==SQLITE_DONE,"audit metadata tamper update failed");
    sqlite3_finalize(tamperAudit);
    RequireAudit(!audit.VerifyChain(),"audit-v2 metadata tampering was not detected");

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
        std::cout << "[core] response-rule match log..." << std::endl;
        TestResponseRuleMatchLog();
        std::cout << "[core] response-rule match log PASS" << std::endl;
        std::cout << "[core] response-rule matcher..." << std::endl;
        TestResponseRuleMatcher();
        std::cout << "[core] response-rule matcher PASS" << std::endl;
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
        std::cout << "[core] identity research provider adapters..." << std::endl;
        TestIdentityResearchProviderAdapters();
        std::cout << "[core] identity research provider adapters PASS" << std::endl;
        std::cout << "[core] identity research packages..." << std::endl;
        TestIdentityResearchPackages();
        std::cout << "[core] identity research packages PASS" << std::endl;
        std::cout << "[core] subject identity store..." << std::endl;
        TestSubjectIdentityStore();
        std::cout << "[core] subject identity store PASS" << std::endl;
        std::cout << "[core] supervisor takeover state..." << std::endl;
        TestSupervisorStateStore();
        std::cout << "[core] supervisor takeover state PASS" << std::endl;
        std::cout << "[core] agency server persistence..." << std::endl;
        TestAgencyServerStore();
        std::cout << "[core] agency server persistence PASS" << std::endl;
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
