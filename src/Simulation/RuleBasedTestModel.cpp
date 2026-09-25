#include "Sentinel/Simulation/IModelAdapter.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

namespace sentinel::simulation {
namespace {

std::string Lower(std::string_view input) {
    std::string out(input);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

class RuleBasedTestModel final : public IModelAdapter {
public:
    std::string Name() const override {
        return "Built-in deterministic test model";
    }

    std::string GenerateSyntheticReply(
        std::string_view investigatorMessage,
        const ModelContext&) override
    {
        const auto m = Lower(investigatorMessage);

        if (m.find("hello") != std::string::npos ||
            m.find("hi") != std::string::npos)
            return "Hi. Who is this?";

        if (m.find("name") != std::string::npos)
            return "You can call me Alex.";

        if (m.find("where") != std::string::npos ||
            m.find("location") != std::string::npos)
            return "I'm at home right now.";

        if (m.find("today") != std::string::npos ||
            m.find("tonight") != std::string::npos)
            return "Nothing special planned right now.";

        if (m.find("?") != std::string::npos)
            return "I'm not sure. What do you mean?";

        return "Okay. Tell me more.";
    }

    std::string GenerateInvestigatorSuggestion(
        const ModelContext& context) override
    {
        if (context.history.empty())
            return "Suggested reply: Start with a neutral greeting and let the synthetic subject establish context.";

        const auto& last = context.history.back();
        if (last.speaker == ChatTurn::Speaker::SyntheticSubject) {
            const auto l = Lower(last.text);
            if (l.find("who is this") != std::string::npos)
                return "Suggested reply: Introduce the test persona using only the configured synthetic persona facts.";
            if (l.find("what do you mean") != std::string::npos)
                return "Suggested reply: Clarify the previous question without adding new persona facts.";
        }

        return "Suggested reply: Acknowledge the last message and ask one neutral follow-up question.";
    }
};

}

std::unique_ptr<IModelAdapter> CreateRuleBasedTestModel() {
    return std::make_unique<RuleBasedTestModel>();
}

}
