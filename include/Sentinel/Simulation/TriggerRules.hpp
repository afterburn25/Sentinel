#pragma once
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::simulation {

struct TriggerRule {
    std::string id;
    std::string name;
    std::string pattern;
    std::vector<std::string> responses;
    int priority{100};
    bool terminal{true};
    bool enabled{true};
};

struct TriggerMatch {
    std::string ruleId;
    std::string ruleName;
    std::string response;
    bool terminal{true};
};

class TriggerRuleRegistry {
public:
    TriggerRule& Add(std::string name,std::string pattern,std::vector<std::string> responses,int priority=100,bool terminal=true);
    void Remove(size_t index);
    std::vector<TriggerRule>& Rules();
    const std::vector<TriggerRule>& Rules() const;
    std::optional<TriggerMatch> Match(std::string_view message,std::string_view personaSummary,size_t turnCount) const;
    void Save(const std::filesystem::path& path) const;
    void Load(const std::filesystem::path& path);
private:
    std::vector<TriggerRule> rules_;
};

}
