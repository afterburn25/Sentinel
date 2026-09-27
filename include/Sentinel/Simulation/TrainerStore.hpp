#pragma once

#include "Sentinel/Storage/SqliteDatabase.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::simulation {

enum class TrainingMode { Behavior, Correction, PersonaLora, FoundationSft, Preference };

struct ModelFoundation {
    std::string id;
    std::string name;
    std::string parentId;
    std::string sourceModel;
    std::string trainableSourcePath;
    std::string runtimeGgufPath;
    int version{1};
    std::string status{"DRAFT"};
    std::string notes;
};

struct PersonaLoraBinding {
    long long id{};
    std::string personaName;
    std::string foundationId;
    std::string loraName;
    std::string loraPath;
    double weight{1.0};
    bool active{true};
};

struct TrainerJobRecord {
    std::string id;
    TrainingMode mode{TrainingMode::Behavior};
    std::string targetName;
    std::string personaName;
    std::string foundationId;
    std::string datasetPath;
    std::string baseModelPath;
    std::string outputPath;
    std::string state{"DRAFT"};
    int progress{};
};

class TrainerStore {
public:
    explicit TrainerStore(SqliteDatabase& db) : db_(db) {}
    void EnsureDefaultFoundation(std::string_view name,std::string_view sourceModel,std::string_view runtimeGgufPath);
    std::vector<ModelFoundation> ListFoundations() const;
    std::optional<ModelFoundation> GetFoundation(std::string_view id) const;
    ModelFoundation CreateFork(std::string_view name,std::string_view parentId,std::string_view sourceModel,std::string_view trainableSourcePath,std::string_view runtimeGgufPath = {});
    PersonaLoraBinding BindPersonaLora(std::string_view personaName,std::string_view foundationId,std::string_view loraName,std::string_view loraPath,double weight = 1.0);
    std::optional<PersonaLoraBinding> ResolvePersonaLora(std::string_view personaName) const;
    TrainerJobRecord QueueJob(TrainingMode mode,std::string_view targetName,std::string_view personaName,std::string_view foundationId,std::string_view datasetPath,std::string_view baseModelPath,std::string_view outputPath,std::string_view configJson = "{}");
    std::vector<TrainerJobRecord> ListJobs(size_t limit=12) const;
private:
    SqliteDatabase& db_;
};

std::string ToString(TrainingMode mode);
TrainingMode TrainingModeFromString(std::string_view value);

} // namespace sentinel::simulation
