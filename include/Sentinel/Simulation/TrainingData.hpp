#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace sentinel::simulation {

enum class TrainingExampleState { Captured, Review, Approved, Rejected };

struct TrainingExample {
    std::string id;
    std::string persona;
    std::string foundationId;
    std::string adapterId;
    std::string sourceConversationId;
    std::string input;
    std::string originalResponse;
    std::string correction;
    std::string targetResponse;
    std::string category{"Behavior"};
    std::string createdUtc;
    std::string reviewer;
    TrainingExampleState state{TrainingExampleState::Captured};
};

struct DatasetSnapshot {
    std::string id;
    std::string name;
    std::vector<std::string> exampleIds;
};

class TrainingDataRegistry {
public:
    TrainingExample& Capture(
        std::string persona,std::string foundationId,std::string adapterId,std::string sourceConversationId,
        std::string input,std::string originalResponse,std::string correction,std::string targetResponse,
        std::string category="Behavior");
    void SetState(size_t index,TrainingExampleState state);
    DatasetSnapshot& CreateSnapshot(std::string name);
    std::vector<TrainingExample>& Examples();
    const std::vector<TrainingExample>& Examples() const;
    std::vector<DatasetSnapshot>& Snapshots();
    const std::vector<DatasetSnapshot>& Snapshots() const;
    size_t Count(TrainingExampleState state) const;
    void Save(const std::filesystem::path& path) const;
    void Load(const std::filesystem::path& path);
private:
    std::vector<TrainingExample> examples_;
    std::vector<DatasetSnapshot> snapshots_;
};

std::string ToString(TrainingExampleState state);

}
