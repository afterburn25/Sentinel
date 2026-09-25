#include "Sentinel/Simulation/PersonaPolicy.hpp"
#include <algorithm>
#include <cctype>

namespace sentinel::simulation {
namespace {
std::string Lower(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){ return (char)std::tolower(c); });
    return s;
}
}
std::string ToString(AgeKnowledgeState state) {
    switch(state) {
        case AgeKnowledgeState::Unknown: return "UNKNOWN";
        case AgeKnowledgeState::SelfReportedMinor: return "SELF_REPORTED_MINOR";
        case AgeKnowledgeState::SelfReportedAdult: return "SELF_REPORTED_ADULT";
        case AgeKnowledgeState::DocumentedMinor: return "DOCUMENTED_MINOR";
        case AgeKnowledgeState::DocumentedAdult: return "DOCUMENTED_ADULT";
        case AgeKnowledgeState::Conflicting: return "CONFLICTING";
    }
    return "UNKNOWN";
}
AgeKnowledgeState AgeStateFromString(const std::string& value) {
    const auto v=Lower(value);
    if(v=="self_reported_minor") return AgeKnowledgeState::SelfReportedMinor;
    if(v=="self_reported_adult") return AgeKnowledgeState::SelfReportedAdult;
    if(v=="documented_minor") return AgeKnowledgeState::DocumentedMinor;
    if(v=="documented_adult") return AgeKnowledgeState::DocumentedAdult;
    if(v=="conflicting") return AgeKnowledgeState::Conflicting;
    return AgeKnowledgeState::Unknown;
}
PolicyDecision EvaluateSimulationPolicy(AgeKnowledgeState state,const std::string& candidateText) {
    const auto t=Lower(candidateText);
    PolicyDecision d;
    if(state==AgeKnowledgeState::Conflicting) {
        d.requiresSupervisor=true;
        d.reason="Age information conflicts; supervisor review required before using sensitive model guidance.";
    }
    if(state==AgeKnowledgeState::SelfReportedMinor || state==AgeKnowledgeState::DocumentedMinor) {
        const bool sensitive =
            t.find("sexual")!=std::string::npos || t.find("nude")!=std::string::npos ||
            t.find("explicit")!=std::string::npos || t.find("meet me")!=std::string::npos;
        if(sensitive) {
            d.allowed=false;
            d.requiresSupervisor=true;
            d.reason="Simulation policy blocks sensitive escalation when the synthetic age state is minor.";
        } else {
            d.reason="Minor age state active; neutral/non-escalating simulation content only.";
        }
    }
    return d;
}
}
