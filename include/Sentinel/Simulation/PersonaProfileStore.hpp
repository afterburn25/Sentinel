#pragma once

#include "Sentinel/Simulation/PersonaPolicy.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::simulation {

struct StoredPersonaProfile {
    PersonaProfile profile;
    int minDelayMs{3000};
    int maxDelayMs{8500};
    std::string updatedUtc;
};

class PersonaProfileStore {
public:
    explicit PersonaProfileStore(SqliteDatabase& db):db_(db){}

    void Save(const PersonaProfile& profile,int minDelayMs,int maxDelayMs);
    std::optional<StoredPersonaProfile> Load(std::string_view name) const;
    std::vector<StoredPersonaProfile> List() const;
    bool Delete(std::string_view name);
    size_t Count() const;

private:
    SqliteDatabase& db_;
};

}
