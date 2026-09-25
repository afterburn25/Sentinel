#pragma once
#include "Sentinel/Simulation/PersonaPolicy.hpp"
#include <filesystem>
#include <string>

namespace sentinel::simulation {

struct SimulationSettings {
    std::string endpoint{"http://127.0.0.1:1234/v1/chat/completions"};
    std::string model{"local-model"};
    double temperature{0.35};
    int maxTokens{512};
    int minDelayMs{3000};
    int maxDelayMs{8500};
    PersonaProfile persona;
    ScenarioProfile scenario;
    AgeKnowledgeState ageState{AgeKnowledgeState::Unknown};
};

SimulationSettings LoadSimulationSettings(const std::filesystem::path& path);
void SaveSimulationSettings(const std::filesystem::path& path,const SimulationSettings& settings);

}
