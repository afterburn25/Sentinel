#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace sentinel::simulation {

enum class ModelStage { Candidate, Approved, Active, Retired };

struct RegisteredModel {
    std::string id;
    std::string endpoint;
    std::string modelName;
    int evaluationScore{0};
    long long latencyMs{0};
    ModelStage stage{ModelStage::Candidate};
};

struct TrainingJob {
    std::string id;
    std::string baseModel;
    std::string dataset;
    std::string state{"QUEUED"};
    int progress{0};
};

enum class FoundationStage { Base, Candidate, Approved, Active, Retired };

struct FoundationModel {
    std::string id;
    std::string name;
    std::string parentId;
    std::string version;
    bool immutableBase{false};
    FoundationStage stage{FoundationStage::Candidate};
};

enum class AdapterStage { Training, Staging, Active, Archived };

struct PersonaAdapter {
    std::string id;
    std::string personaName;
    std::string adapterName;
    std::string version;
    std::string foundationId;
    AdapterStage stage{AdapterStage::Staging};
};

class PersonaAdapterRegistry {
public:
    PersonaAdapter& Add(std::string personaName,std::string adapterName,std::string version,std::string foundationId);
    void Activate(size_t index);
    bool Rollback(std::string_view personaName);
    std::vector<PersonaAdapter>& Adapters();
    const std::vector<PersonaAdapter>& Adapters() const;
    int ResolveActiveIndex(std::string_view personaName) const;
    void Save(const std::filesystem::path& path) const;
    void Load(const std::filesystem::path& path);
private:
    std::vector<PersonaAdapter> adapters_;
};

class TrainingJobRegistry {
public:
    TrainingJob& Create(std::string baseModel,std::string dataset);
    void SetState(size_t index,std::string state,int progress);
    std::vector<TrainingJob>& Jobs();
    const std::vector<TrainingJob>& Jobs() const;
    void Save(const std::filesystem::path& path) const;
    void Load(const std::filesystem::path& path);
private:
    std::vector<TrainingJob> jobs_;
};

class FoundationRegistry {
public:
    FoundationModel& EnsureBase(std::string name,std::string version="base");
    FoundationModel& CreateFork(size_t parentIndex,std::string name,std::string version);
    void Approve(size_t index);
    void Activate(size_t index);
    bool Rollback();
    std::vector<FoundationModel>& Models();
    const std::vector<FoundationModel>& Models() const;
    int ActiveIndex() const;
    void Save(const std::filesystem::path& path) const;
    void Load(const std::filesystem::path& path);
private:
    std::vector<FoundationModel> models_;
    int activeIndex_{-1};
    int previousActiveIndex_{-1};
};

class ModelRegistry {
public:
    RegisteredModel& Register(std::string endpoint,std::string modelName);
    void Approve(size_t index);
    void Activate(size_t index);
    void Retire(size_t index);
    bool Rollback();
    std::vector<RegisteredModel>& Models();
    const std::vector<RegisteredModel>& Models() const;
    int ActiveIndex() const;
    void Save(const std::filesystem::path& path) const;
    void Load(const std::filesystem::path& path);
private:
    std::vector<RegisteredModel> models_;
    int activeIndex_{-1};
    int previousActiveIndex_{-1};
};

std::string ToString(ModelStage stage);
std::string ToString(FoundationStage stage);
std::string ToString(AdapterStage stage);

}
