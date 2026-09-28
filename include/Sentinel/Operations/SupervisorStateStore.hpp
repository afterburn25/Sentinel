#pragma once
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace sentinel::operations {

struct SupervisorControlState {
    std::string operationKey;
    bool investigatorTakeover{};
    std::string takeoverActor;
    std::string takeoverNote;
    std::string takeoverUtc;
    std::string releasedBy;
    std::string releasedUtc;
    std::string updatedUtc;
};

class SupervisorStateStore {
public:
    explicit SupervisorStateStore(SqliteDatabase& db):db_(db){}

    [[nodiscard]] std::optional<SupervisorControlState> Get(std::string_view operationKey) const;
    SupervisorControlState SetTakeover(
        std::string operationKey,
        bool active,
        std::string actor,
        std::string note={});

private:
    SqliteDatabase& db_;
};

}
