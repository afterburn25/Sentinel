#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::simulation {

struct ChatTurn {
    enum class Speaker {
        Investigator,
        SyntheticSubject,
        ModelSuggestion
    };

    Speaker speaker{};
    std::string text;
};

struct ModelContext {
    std::string scenario;
    std::string personaSummary;
    // Relevant material retrieved from older conversations. This is model-only
    // background memory, not text that should be repeated verbatim to the user.
    std::string recalledMemory;
    bool learningMode{true};
    unsigned int variationSeed{};
    std::vector<ChatTurn> history;
};

class IModelAdapter {
public:
    virtual ~IModelAdapter() = default;

    [[nodiscard]] virtual std::string Name() const = 0;

    [[nodiscard]] virtual std::string GenerateSyntheticReply(
        std::string_view investigatorMessage,
        const ModelContext& context) = 0;

    [[nodiscard]] virtual std::string GenerateInvestigatorSuggestion(
        const ModelContext& context) = 0;

    [[nodiscard]] virtual std::string GenerateSyntheticInitiative(
        const ModelContext& context) = 0;

    [[nodiscard]] virtual std::string GeneratePersonaRuleReply(
        std::string_view approvedMeaning,
        const ModelContext& context) = 0;

    [[nodiscard]] virtual std::string GenerateBehaviorProfile(
        int age,
        std::string_view background,
        const ModelContext& context) = 0;

    [[nodiscard]] virtual std::string GenerateCorrectionPreview(
        std::string_view originalInput,
        std::string_view originalResponse,
        std::string_view trainerInstruction,
        const ModelContext& context) = 0;
};

std::unique_ptr<IModelAdapter> CreateRuleBasedTestModel();

std::unique_ptr<IModelAdapter> CreateOpenAICompatibleModel(
    std::string endpoint,
    std::string model,
    std::string apiKey = {},
    double temperature = 0.65,
    int maxTokens = 220);

std::vector<std::string> DiscoverOpenAICompatibleModels(
    std::string endpoint,
    std::string apiKey = {});

std::string SelectPreferredOpenAICompatibleModel(
    const std::vector<std::string>& models,
    std::string_view configuredModel,
    bool requireSentinelChat);

}
