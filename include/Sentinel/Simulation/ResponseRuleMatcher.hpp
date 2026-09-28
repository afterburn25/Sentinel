#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace sentinel::simulation {

struct ResponseRuleMatchQuality {
    int score{};
    int typeRank{};
    std::size_t specificity{};

    [[nodiscard]] bool Matched() const noexcept { return score>0; }
};

[[nodiscard]] std::string NormalizeResponseRuleText(std::string_view input);

[[nodiscard]] ResponseRuleMatchQuality EvaluateResponseRuleMatch(
    std::string_view matchType,
    std::string_view input,
    std::string_view trigger);

[[nodiscard]] bool PreferResponseRuleMatch(
    int candidatePriority,
    long long candidateId,
    const ResponseRuleMatchQuality& candidate,
    int currentPriority,
    long long currentId,
    const ResponseRuleMatchQuality& current) noexcept;

}
