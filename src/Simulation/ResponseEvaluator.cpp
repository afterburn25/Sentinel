#include "Sentinel/Simulation/ResponseEvaluator.hpp"
#include <algorithm>
#include <cctype>

namespace sentinel::simulation {
namespace {
std::string Lower(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){ return (char)std::tolower(c); });
    return s;
}
}
ResponseEvaluation EvaluateResponse(
    const PersonaProfile& persona,
    AgeKnowledgeState ageState,
    const std::string& response)
{
    ResponseEvaluation out;
    const auto lower=Lower(response);
    const auto policy=EvaluateSimulationPolicy(ageState,response);
    out.policyAllowed=policy.allowed;
    if(!policy.allowed) {
        out.score-=60;
        out.warnings.push_back(policy.reason);
    } else if(policy.requiresSupervisor) {
        out.score-=15;
        out.warnings.push_back(policy.reason);
    }

    if(response.size()<3) {
        out.score-=25;
        out.warnings.push_back("Response is too short to evaluate as a useful conversational turn.");
    }
    if(lower=="okay. tell me more." || lower=="okay, tell me more.") {
        out.score-=20;
        out.warnings.push_back("Generic fallback response detected.");
    }

    const auto nameMarker=lower.find("my name is ");
    if(nameMarker!=std::string::npos) {
        auto claimed=lower.substr(nameMarker+11);
        auto end=claimed.find_first_of(".,!? ");
        if(end!=std::string::npos) claimed.resize(end);
        if(!claimed.empty() && claimed!=Lower(persona.name)) {
            out.personaConsistent=false;
            out.score-=35;
            out.warnings.push_back("Response appears to contradict the configured persona name.");
        }
    }

    const auto ageMarker=lower.find("i'm ");
    if(ageMarker!=std::string::npos && lower.find(" years old")!=std::string::npos) {
        auto start=ageMarker+4;
        auto end=lower.find(' ',start);
        if(end!=std::string::npos) {
            try {
                int claimed=std::stoi(lower.substr(start,end-start));
                if(claimed!=persona.age) {
                    out.personaConsistent=false;
                    out.score-=35;
                    out.warnings.push_back("Response appears to contradict the configured persona age.");
                }
            } catch(...) {}
        }
    }

    out.score=std::clamp(out.score,0,100);
    return out;
}
}
