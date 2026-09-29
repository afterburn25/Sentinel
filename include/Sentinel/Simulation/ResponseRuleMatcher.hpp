#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::simulation {

struct ResponseRuleMatchQuality {
    int score{};
    int typeRank{};
    std::size_t specificity{};

    [[nodiscard]] bool Matched() const noexcept { return score>0; }
};

struct ResponseRulePageWindow {
    std::size_t pageIndex{};
    std::size_t pageCount{1};
    std::size_t start{};
    std::size_t end{};
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

[[nodiscard]] std::vector<std::string> SplitResponseRuleVariants(
    std::string_view responseText);

[[nodiscard]] std::string SelectResponseRuleVariant(
    std::string_view responseText,
    std::string_view deterministicBasis);

[[nodiscard]] ResponseRulePageWindow ComputeResponseRulePageWindow(
    std::size_t totalItems,
    std::size_t requestedPage,
    std::size_t pageSize) noexcept;

}
