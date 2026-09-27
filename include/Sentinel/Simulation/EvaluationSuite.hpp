#pragma once

#include "Sentinel/Simulation/PersonaPolicy.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::simulation {

enum class EvaluationDimension {
    PersonaConsistency,
    PolicyCompliance,
    StyleConsistency,
    MemoryRecall,
    TriggerRegression,
    ResponseDiversity
};

struct EvaluationDimensionResult {
    EvaluationDimension dimension{EvaluationDimension::PersonaConsistency};
    int score{100};
    bool passed{true};
    std::string details;
    std::vector<std::string> warnings;
};

struct EvaluationRun {
    std::string id;
    std::string createdUtc;
    std::string candidateId;
    std::string candidateName;
    std::string foundationId;
    std::string foundationName;
    std::string adapterId;
    std::string adapterName;
    int overallScore{0};
    int previousOverallScore{-1};
    int regressionDelta{0};
    std::vector<EvaluationDimensionResult> dimensions;
    std::vector<std::string> warnings;
};

class EvaluationRunRegistry {
public:
    EvaluationRun& Create(
        std::string candidateId,
        std::string candidateName,
        std::string foundationId,
        std::string foundationName,
        std::string adapterId,
        std::string adapterName,
        std::vector<EvaluationDimensionResult> dimensions);

    std::vector<EvaluationRun>& Runs();
    const std::vector<EvaluationRun>& Runs() const;

    int LatestIndexForCandidate(std::string_view candidateId) const;

    void Save(const std::filesystem::path& path) const;
    void Load(const std::filesystem::path& path);

private:
    std::vector<EvaluationRun> runs_;
};

std::string ToString(EvaluationDimension dimension);

EvaluationDimensionResult ScorePersonaConsistency(
    const PersonaProfile& persona,
    AgeKnowledgeState ageState,
    const std::vector<std::string>& responses);

EvaluationDimensionResult ScorePolicyCompliance(
    AgeKnowledgeState ageState,
    const std::vector<std::string>& responses);

EvaluationDimensionResult ScoreStyleConsistency(
    const PersonaProfile& persona,
    const std::vector<std::string>& responses);

EvaluationDimensionResult ScoreMemoryRecall(
    std::string_view expectedFact,
    std::string_view response);

EvaluationDimensionResult ScoreTriggerRegression(
    size_t totalRules,
    size_t passedRules);

EvaluationDimensionResult ScoreResponseDiversity(
    const std::vector<std::string>& responses);

}
