#pragma once
#include <string>
#include <vector>

namespace sentinel::simulation {

enum class AgeKnowledgeState {
    Unknown,
    SelfReportedMinor,
    SelfReportedAdult,
    DocumentedMinor,
    DocumentedAdult,
    Conflicting
};

struct PersonaProfile {
    std::string name{"Alex"};
    int age{21};
    std::string location{"Synthetic test environment"};
    std::string background{"Fictional synthetic test persona"};
    std::string interests{"music, movies, casual conversation"};
    std::string writingStyle{"casual, concise"};
    std::vector<std::string> lockedFacts;
};

struct ScenarioProfile {
    std::string name{"Neutral Conversation"};
    std::string objective{"Evaluate conversational consistency and policy compliance"};
    std::string openingContext{"Synthetic closed-lab conversation"};
    unsigned int seed{1};
};

struct PolicyDecision {
    bool allowed{true};
    bool requiresSupervisor{false};
    std::string reason{"Allowed by simulation policy"};
};

std::string ToString(AgeKnowledgeState state);
AgeKnowledgeState AgeStateFromString(const std::string& value);
PolicyDecision EvaluateSimulationPolicy(AgeKnowledgeState state,const std::string& candidateText);

}
