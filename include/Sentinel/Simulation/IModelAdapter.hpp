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
};

std::unique_ptr<IModelAdapter> CreateRuleBasedTestModel();

}
