#pragma once

#include "Sentinel/Storage/SqliteDatabase.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::simulation {

struct ResponseRuleMatchRecord {
    long long id{};
    std::string personaName;
    std::string conversationId;
    long long ruleId{};
    std::string matchType;
    int matchScore{};
    std::string triggerText;
    std::string inputText;
    std::string responseMode;
    std::string outputText;
    std::string createdUtc;
};

class ResponseRuleMatchLog {
public:
    explicit ResponseRuleMatchLog(SqliteDatabase& db):db_(db){}

    long long Append(
        std::string_view personaName,
        std::string_view conversationId,
        long long ruleId,
        std::string_view matchType,
        int matchScore,
        std::string_view triggerText,
        std::string_view inputText,
        std::string_view responseMode,
        std::string_view outputText);

    std::vector<ResponseRuleMatchRecord> Recent(
        std::string_view personaName,
        size_t limit=20) const;

    long long CountForRule(
        std::string_view personaName,
        long long ruleId) const;

private:
    SqliteDatabase& db_;
};

}
