#pragma once

#include "Sentinel/Storage/SqliteDatabase.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace sentinel::simulation {

enum class TrainingReviewStatus {
    Pending = 0,
    Approved = 1,
    Rejected = 2
};

struct TrainingReviewCounts {
    int pending{};
    int approved{};
    int rejected{};
};

struct TrainingReviewItem {
    std::string id;
    std::string sourceLogId;
    std::string conversationId;
    std::string personaName;
    std::string modelName;
    std::string inputText;
    std::string outputText;
    std::string personaSummary;
    std::string recalledMemory;
    std::string contextJson;
    TrainingReviewStatus status{TrainingReviewStatus::Pending};
    std::string reviewer;
    std::string notes;
    std::string createdUtc;
    std::string reviewedUtc;
};

class TrainingReviewStore {
public:
    explicit TrainingReviewStore(SqliteDatabase& db) : db_(db) {}

    std::optional<TrainingReviewItem> StageLatestReply(std::string_view conversationId);
    std::optional<TrainingReviewItem> Get(std::string_view id) const;
    bool Review(
        std::string_view id,
        TrainingReviewStatus status,
        std::string_view reviewer,
        std::string_view notes = {});
    TrainingReviewCounts Counts() const;
    size_t ExportApprovedJsonl(const std::filesystem::path& path) const;

private:
    SqliteDatabase& db_;
};

std::string ToString(TrainingReviewStatus status);

}
