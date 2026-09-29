#include "Sentinel/Simulation/PersonaPolicy.hpp"
#include "Sentinel/Simulation/SettingsStore.hpp"
#include "Sentinel/Simulation/ResponseEvaluator.hpp"
#include "Sentinel/Simulation/SessionStore.hpp"
#include "Sentinel/Simulation/ModelRegistry.hpp"
#include "Sentinel/Simulation/EvaluationSuite.hpp"
#include "Sentinel/Simulation/TrainingReviewStore.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include "Sentinel/Operations/Messaging.hpp"
#include "Sentinel/Operations/Supervisor.hpp"
#include "Sentinel/Channels/AutomationEngine.hpp"
#include "Sentinel/Channels/ChannelAdapterRegistry.hpp"
#include "Sentinel/Channels/LocalSimulationChannelAdapter.hpp"
#include "Sentinel/Channels/JurisdictionRules.hpp"
#include "Sentinel/Agency/AgencyServer.hpp"
#include "Sentinel/Update/UpdateService.hpp"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <stdexcept>

static void Require(bool v,const char* msg) {
    if(!v) throw std::runtime_error(msg);
}

int main() {
    using namespace sentinel;
    simulation::SimulationSettings settings;
    settings.endpoint="http://127.0.0.1:1234/v1/chat/completions";
    settings.model="test-model";
    settings.persona.name="Casey";
    settings.persona.age=27;
    settings.scenario.name="Regression";
    settings.ageState=simulation::AgeKnowledgeState::SelfReportedAdult;

    auto path=std::filesystem::temp_directory_path()/"sentinel-platform-settings-test.ini";
    simulation::SaveSimulationSettings(path,settings);
    auto loaded=simulation::LoadSimulationSettings(path);
    Require(loaded.model=="test-model","settings model did not round-trip");
    Require(loaded.persona.name=="Casey","persona did not round-trip");
    Require(loaded.scenario.name=="Regression","scenario did not round-trip");
    Require(loaded.ageState==simulation::AgeKnowledgeState::SelfReportedAdult,"age state did not round-trip");
    std::filesystem::remove(path);
    auto minorDecision=simulation::EvaluateSimulationPolicy(
        simulation::AgeKnowledgeState::DocumentedMinor,
        "send explicit sexual content");
    Require(!minorDecision.allowed,"minor-sensitive simulation policy should block");
    Require(minorDecision.requiresSupervisor,"minor-sensitive block should require supervisor");

    auto evaluation=simulation::EvaluateResponse(settings.persona,simulation::AgeKnowledgeState::SelfReportedAdult,"My name is Casey.");
    Require(evaluation.score>=80,"consistent response evaluation unexpectedly low");
    simulation::ModelRegistry registry;
    auto& registered=registry.Register(settings.endpoint,settings.model);

    simulation::EvaluationRun approvalRun;
    approvalRun.id="platform-eval-1";
    approvalRun.candidateId=registered.id;
    approvalRun.candidateName=registered.modelName;
    approvalRun.overallScore=92;
    const simulation::EvaluationDimension requiredDimensions[]={
        simulation::EvaluationDimension::PersonaConsistency,
        simulation::EvaluationDimension::PolicyCompliance,
        simulation::EvaluationDimension::StyleConsistency,
        simulation::EvaluationDimension::MemoryRecall,
        simulation::EvaluationDimension::TriggerRegression,
        simulation::EvaluationDimension::ResponseDiversity
    };
    for(auto dimension:requiredDimensions)
        approvalRun.dimensions.push_back({dimension,92,true,"platform test pass",{}});

    Require(registry.Approve(0,approvalRun,"",""),
        "model registry approval failed");
    registry.Activate(0);
    Require(registry.ActiveIndex()==0,"model registry activation failed");

    auto registryPath=std::filesystem::temp_directory_path()/"sentinel-model-registry-test.tsv";
    registry.Save(registryPath);
    simulation::ModelRegistry registry2;
    registry2.Load(registryPath);
    Require(registry2.Models().size()==1,"model registry persistence failed");
    std::filesystem::remove(registryPath);
    simulation::ModelContext session;
    session.scenario="test";
    session.personaSummary="Casey";
    session.history.push_back({simulation::ChatTurn::Speaker::Investigator,"hello"});
    auto sessionPath=std::filesystem::temp_directory_path()/"sentinel-session-test.tsv";
    simulation::SaveSession(sessionPath,session);
    simulation::ModelContext loadedSession;
    Require(simulation::LoadSession(sessionPath,loadedSession),"session load failed");
    Require(loadedSession.history.size()==1,"session turn did not persist");
    std::filesystem::remove(sessionPath);
    {
        auto dbPath=std::filesystem::temp_directory_path()/"sentinel-training-review-test.db";
        std::filesystem::remove(dbPath);
        SqliteDatabase db;
        db.Open(dbPath);
        db.Execute(
            "CREATE TABLE persona_conversation_logs ("
            "id TEXT PRIMARY KEY,conversation_id TEXT NOT NULL,persona_name TEXT NOT NULL,"
            "model_name TEXT NOT NULL,event_kind TEXT NOT NULL,input_text TEXT NOT NULL,"
            "output_text TEXT NOT NULL,persona_summary TEXT NOT NULL,recalled_memory TEXT NOT NULL,"
            "context_json TEXT NOT NULL,start_delay_ms INTEGER NOT NULL,typing_delay_ms INTEGER NOT NULL,"
            "policy_status TEXT NOT NULL,created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
            "CREATE TABLE training_review_items ("
            "id TEXT PRIMARY KEY,source_log_id TEXT NOT NULL UNIQUE,conversation_id TEXT NOT NULL DEFAULT '',"
            "persona_name TEXT NOT NULL DEFAULT '',model_name TEXT NOT NULL DEFAULT '',"
            "input_text TEXT NOT NULL DEFAULT '',output_text TEXT NOT NULL DEFAULT '',"
            "persona_summary TEXT NOT NULL DEFAULT '',recalled_memory TEXT NOT NULL DEFAULT '',"
            "context_json TEXT NOT NULL DEFAULT '{}',status INTEGER NOT NULL DEFAULT 0,"
            "reviewer TEXT NOT NULL DEFAULT '',notes TEXT NOT NULL DEFAULT '',"
            "created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,reviewed_utc TEXT NOT NULL DEFAULT '');"
            "INSERT INTO persona_conversation_logs("
            "id,conversation_id,persona_name,model_name,event_kind,input_text,output_text,"
            "persona_summary,recalled_memory,context_json,start_delay_ms,typing_delay_ms,policy_status"
            ") VALUES("
            "'log-1','conversation-1','Casey','test-model','reactive_reply','hi','hey :)',"
            "'Casey persona','likes movies','{}',500,900,'allowed');");
        simulation::TrainingReviewStore reviews(db);
        auto staged=reviews.StageLatestReply("conversation-1");
        Require(staged.has_value(),"latest persona reply should stage for review");
        Require(staged->status==simulation::TrainingReviewStatus::Pending,
            "new training review item should start pending");
        Require(reviews.Review(staged->id,simulation::TrainingReviewStatus::Approved,"tester","good example"),
            "training review approval failed");
        auto counts=reviews.Counts();
        Require(counts.approved==1 && counts.pending==0,
            "training review counts are incorrect after approval");

        auto datasetPath=std::filesystem::temp_directory_path()/"sentinel-training-review-test.jsonl";
        const auto exported=reviews.ExportApprovedJsonl(datasetPath);
        Require(exported==1,"approved dataset export count incorrect");
        std::ifstream dataset(datasetPath);
        std::string jsonl;
        std::getline(dataset,jsonl);
        Require(jsonl.find("\"source_log_id\":\"log-1\"")!=std::string::npos,
            "dataset export lost source-log provenance");
        Require(jsonl.find("\"input\":\"hi\"")!=std::string::npos,
            "dataset export lost input text");
        dataset.close();
        db.Close();
        std::filesystem::remove(datasetPath);
        std::filesystem::remove(dbPath);
    }
    auto adapter=operations::CreateInMemoryMessageAdapter();
    Require(adapter->Connected(),"local messaging adapter should be connected");
    auto msg=adapter->QueueOperatorApproved("conversation-1","hello");
    Require(msg.state==operations::DeliveryState::Queued,"approved message should queue");
    Require(adapter->Poll("conversation-1").size()==1,"queued message should poll");
    channels::AutomationEngine automation;
    auto ordinary=automation.Decide({
        channels::AutomationMode::AuthorizedAutomatic,
        channels::ActionKind::OrdinaryReply,
        true,
        true,
        false,
        true,
        true,
        false});
    Require(ordinary.decision==channels::AutomationDecisionKind::AutoSend,
        "authorized ordinary reply should be eligible for automatic sending");

    auto mediaDecision=automation.Decide({
        channels::AutomationMode::AuthorizedAutomatic,
        channels::ActionKind::BenignMedia,
        true,
        true,
        false,
        true,
        true,
        false});
    Require(mediaDecision.decision==channels::AutomationDecisionKind::RequireApproval,
        "media should remain approval-gated");

    auto noLegalActivation=automation.Decide({
        channels::AutomationMode::AuthorizedAutomatic,
        channels::ActionKind::OrdinaryReply,
        true,
        true,
        false,
        false,
        false,
        true});
    Require(noLegalActivation.decision==channels::AutomationDecisionKind::RequireApproval,
        "inactive jurisdiction profile must prevent auto-send");
    auto localChannel=std::make_unique<channels::LocalSimulationChannelAdapter>(
        operations::CreateInMemoryMessageAdapter());
    Require(localChannel->Capabilities().Has(channels::SendText),
        "local channel should advertise text sending");
    Require(localChannel->Capabilities().Has(channels::SendImage),
        "local channel should advertise image sending");

    channels::ChannelAdapterRegistry channelRegistry;
    channelRegistry.Register(std::move(localChannel));
    Require(channelRegistry.FindByName("SARA Local Simulation")!=nullptr,
        "channel adapter registry lookup failed");
    Require(channelRegistry.FindByType(channels::ChannelType::LocalSimulation).size()==1,
        "channel adapter registry type lookup failed");

    auto approval=operations::CreateApprovalRequest("send:conversation-1:hello","investigator");
    Require(approval.status==operations::ApprovalStatus::Pending,"approval should start pending");
    operations::Approve(approval,"supervisor","approved test");
    Require(approval.status==operations::ApprovalStatus::Approved,"approval did not transition");

    Require(update::UpdateService::IsNewerVersion("1.0.1","1.0.0"),"update version comparison failed");
    Require(!update::UpdateService::IsNewerVersion("1.0.0","1.0.0"),"equal version should not update");
    agency::AgencySyncQueue queue;
    queue.Enqueue({"sync-1",agency::SyncItemType::AuditRecord,"audit:1",0,false});
    Require(queue.PendingCount()==1,"agency sync queue count incorrect");

    std::cout<<"SARA platform tests passed\n";
    return 0;
}
