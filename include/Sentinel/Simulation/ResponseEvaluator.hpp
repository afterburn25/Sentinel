#pragma once
#include "Sentinel/Simulation/PersonaPolicy.hpp"
#include <string>
#include <vector>

namespace sentinel::simulation {

struct ResponseEvaluation {
    int score{100};
    bool policyAllowed{true};
    bool personaConsistent{true};
    std::vector<std::string> warnings;
};

ResponseEvaluation EvaluateResponse(
    const PersonaProfile& persona,
    AgeKnowledgeState ageState,
    const std::string& response);

}
