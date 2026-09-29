#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::simulation {

struct EvaluationRun;

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

class ModelRegistry {
public:
    RegisteredModel& Register(std::string endpoint,std::string modelName);
    bool Approve(
        size_t index,
        const EvaluationRun& evaluation,
        std::string_view currentFoundationId,
        std::string_view currentAdapterId);
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

}
