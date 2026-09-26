#pragma once
#include "Sentinel/Simulation/PersonaPolicy.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::simulation {

class PersonaProfileStore {
public:
    explicit PersonaProfileStore(SqliteDatabase& db):db_(db){}

    void Save(const PersonaProfile& profile);
    std::optional<PersonaProfile> Load(std::string_view name) const;
    std::vector<std::string> ListNames() const;
    bool Delete(std::string_view name);

private:
    SqliteDatabase& db_;
};

}
