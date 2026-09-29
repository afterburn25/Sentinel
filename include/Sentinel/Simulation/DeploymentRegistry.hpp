#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::simulation {

struct EvaluationRun;

enum class DeploymentStage { Staged, Active, RolledBack, Retired };

struct DeploymentPackage {
    std::string id;
    std::string createdUtc;
    std::string activatedUtc;
    std::string rolledBackUtc;
    std::string candidateId;
    std::string candidateName;
    std::string foundationId;
    std::string foundationName;
    std::string adapterId;
    std::string adapterName;
    std::string personaName;
    std::string evaluationRunId;
    int evaluationScore{0};
    bool versionLocked{true};
    DeploymentStage stage{DeploymentStage::Staged};
    std::string previousDeploymentId;
};

class DeploymentRegistry {
public:
    DeploymentPackage& Prepare(
        const EvaluationRun& evaluation,
        std::string personaName);

    bool Activate(size_t index);
    bool Rollback();
    void SetLocked(size_t index,bool locked);

    int ActiveIndex() const;
    int PreviousIndex() const;
    bool HasActiveLockedDeployment() const;

    std::vector<DeploymentPackage>& Packages();
    const std::vector<DeploymentPackage>& Packages() const;

    std::string BuildManifest(size_t index) const;

    void Save(const std::filesystem::path& path) const;
    void Load(const std::filesystem::path& path);

private:
    std::vector<DeploymentPackage> packages_;
    int activeIndex_{-1};
    int previousIndex_{-1};
};

std::string ToString(DeploymentStage stage);

}
