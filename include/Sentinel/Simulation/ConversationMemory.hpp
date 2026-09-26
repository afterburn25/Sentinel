#pragma once

#include "Sentinel/Simulation/IModelAdapter.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::simulation {

struct ArchivedConversation {
    std::string id;
    std::string title;
    std::string personaName;
    std::string scenario;
    std::string createdUtc;
    std::string updatedUtc;
    int messageCount{};
};

class ConversationMemoryStore {
public:
    explicit ConversationMemoryStore(SqliteDatabase& db) : db_(db) {}

    std::string StartConversation(
        std::string_view title,
        std::string_view personaName,
        std::string_view scenario);

    void Append(
        std::string_view conversationId,
        ChatTurn::Speaker speaker,
        std::string_view text);

    std::vector<ArchivedConversation> List(size_t limit=50) const;

    bool Load(
        std::string_view conversationId,
        ModelContext& context) const;

    std::string RecallRelevant(
        std::string_view query,
        std::string_view currentConversationId,
        size_t maxMessages=12) const;

private:
    SqliteDatabase& db_;
};

}
