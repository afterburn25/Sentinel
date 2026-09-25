#include "Sentinel/Simulation/PersonaPolicy.hpp"
#include "Sentinel/Simulation/SettingsStore.hpp"
#include "Sentinel/Simulation/ResponseEvaluator.hpp"
#include "Sentinel/Simulation/SessionStore.hpp"
#include "Sentinel/Simulation/ModelRegistry.hpp"
#include "Sentinel/Operations/Messaging.hpp"
#include "Sentinel/Operations/Supervisor.hpp"
#include "Sentinel/Agency/AgencyServer.hpp"

#include <filesystem>
#include <iostream>
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
    registered.evaluationScore=92;
    registry.Approve(0);
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

    auto adapter=operations::CreateInMemoryMessageAdapter();
    Require(adapter->Connected(),"local messaging adapter should be connected");
    auto msg=adapter->QueueOperatorApproved("conversation-1","hello");
    Require(msg.state==operations::DeliveryState::Queued,"approved message should queue");
    Require(adapter->Poll("conversation-1").size()==1,"queued message should poll");

    auto approval=operations::CreateApprovalRequest("send:conversation-1:hello","investigator");
    Require(approval.status==operations::ApprovalStatus::Pending,"approval should start pending");
    operations::Approve(approval,"supervisor","approved test");
    Require(approval.status==operations::ApprovalStatus::Approved,"approval did not transition");

    agency::AgencySyncQueue queue;
    queue.Enqueue({"sync-1",agency::SyncItemType::AuditRecord,"audit:1",0,false});
    Require(queue.PendingCount()==1,"agency sync queue count incorrect");

    std::cout<<"Sentinel platform tests passed\n";
    return 0;
}
