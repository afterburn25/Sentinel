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
    std::string createdUtc;
    std::string startedUtc;
    std::string completedUtc;
    std::string errorText;
};

struct TrainerDialogueSession {
    std::string id;
    std::string personaName;
    TrainingMode mode{TrainingMode::Behavior};
    std::string title;
    bool active{true};
    std::string createdUtc;
    std::string updatedUtc;
};

struct TrainerDialogueTurn {
    long long id{};
    std::string sessionId;
    std::string role;
    std::string text;
    std::string payload;
    bool applied{};
    std::string createdUtc;
    std::string appliedUtc;
};

class TrainerStore {
public:
    explicit TrainerStore(SqliteDatabase& db) : db_(db) {}
    void EnsureDefaultFoundation(std::string_view name,std::string_view sourceModel,std::string_view runtimeGgufPath);
    std::vector<ModelFoundation> ListFoundations() const;
    std::optional<ModelFoundation> GetFoundation(std::string_view id) const;
    bool ApproveFoundation(std::string_view id);
    bool ActivateFoundation(std::string_view id);
    ModelFoundation CreateFork(std::string_view name,std::string_view parentId,std::string_view sourceModel,std::string_view trainableSourcePath,std::string_view runtimeGgufPath = {});
    PersonaLoraBinding BindPersonaLora(std::string_view personaName,std::string_view foundationId,std::string_view loraName,std::string_view loraPath,double weight = 1.0);
    std::optional<PersonaLoraBinding> ResolvePersonaLora(std::string_view personaName) const;
    std::optional<PersonaLoraBinding> GetPersonaLora(long long id) const;
    bool ActivatePersonaLora(long long id);
    std::string BuildPersonaLoraManifest(long long id) const;
    std::vector<PersonaLoraBinding> ListPersonaLoras(std::string_view personaName,size_t limit=12) const;
    TrainerJobRecord QueueJob(TrainingMode mode,std::string_view targetName,std::string_view personaName,std::string_view foundationId,std::string_view datasetPath,std::string_view baseModelPath,std::string_view outputPath,std::string_view configJson = "{}");
    std::vector<TrainerJobRecord> ListJobs(size_t limit=12) const;

    std::optional<TrainerDialogueSession> ActiveDialogueSession(
        std::string_view personaName,
        TrainingMode mode) const;
    TrainerDialogueSession EnsureDialogueSession(
        std::string_view personaName,
        TrainingMode mode);
    TrainerDialogueSession NewDialogueSession(
        std::string_view personaName,
        TrainingMode mode,
        std::string_view title = {});
    TrainerDialogueTurn AppendDialogueTurn(
        std::string_view sessionId,
        std::string_view role,
        std::string_view text,
        std::string_view payload = {});
    std::vector<TrainerDialogueTurn> ListDialogueTurns(
        std::string_view sessionId,
        size_t limit=20) const;
    bool MarkDialogueTurnApplied(long long turnId);

private:
    SqliteDatabase& db_;
};

std::string ToString(TrainingMode mode);
TrainingMode TrainingModeFromString(std::string_view value);

} // namespace sentinel::simulation
