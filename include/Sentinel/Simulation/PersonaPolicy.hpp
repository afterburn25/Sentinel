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
    std::string gender{"Unspecified"};
    std::string pronouns{"Unspecified"};
    std::string occupation{"Unspecified"};
    std::string education{"Unspecified"};
    std::string relationshipStatus{"Unspecified"};
    std::string familyContext{"Unspecified"};
    std::string personality{"Balanced"};
    std::string socialStyle{"Balanced"};
    std::string confidenceLevel{"Medium"};
    std::string background{"Fictional synthetic test persona"};
    std::string interests{"music, movies, casual conversation"};
    std::string writingStyle{"Casual"};
    std::string communicationLevel{"Age-appropriate"};
    std::string cognitiveLevel{"Average"};
    std::string slangLevel{"Moderate"};
    std::string grammarQuality{"Casual"};
    std::string typoFrequency{"Occasional"};
    std::string emojiLevel{"Occasional"};
    std::string vocabularyLevel{"Age-appropriate"};
    std::string capitalizationStyle{"Casual"};
    std::string messageLength{"Short to medium"};
    int responseStartMinMs{1400};
    int responseStartMaxMs{5200};
    int typingMsPerCharMin{28};
    int typingMsPerCharMax{52};
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
