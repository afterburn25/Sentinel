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

std::string ApplyPersonaWritingVariation(const PersonaProfile& persona,std::string text,size_t variationSeed) {
    auto level=Lower(persona.slangLevel);
    auto grammar=Lower(persona.grammarQuality);
    auto typos=Lower(persona.typoTendency);
    auto emoji=Lower(persona.emojiTendency);
    auto mood=Lower(persona.mood);
    auto intelligence=Lower(persona.intelligenceLevel);

    if(level=="high" || level=="very high") {
        if(variationSeed%3==0 && text.find("really")!=std::string::npos) {
            auto pos=text.find("really");
            text.replace(pos,6,"fr");
        }
        if(variationSeed%4==1 && text.find("Okay")!=std::string::npos) text.replace(text.find("Okay"),4,"Yeah");
    }

    if(intelligence=="simple" || intelligence=="low") {
        if(text.size()>140) {
            auto p=text.find('.',80);
            if(p!=std::string::npos) text=text.substr(0,p+1);
        }
    } else if(intelligence=="high" || intelligence=="very high") {
        if(text.find("I don't know")!=std::string::npos && variationSeed%2==0)
            text.replace(text.find("I don't know"),12,"I'm not entirely sure");
    }

    if(grammar=="casual" || grammar=="loose") {
        if(!text.empty() && variationSeed%3==1) text[0]=(char)std::tolower((unsigned char)text[0]);
        if(variationSeed%4==0) {
            auto p=text.find("I'm");
            if(p!=std::string::npos) text.replace(p,3,"im");
        }
    }

    if((typos=="medium" || typos=="high") && text.size()>18) {
        size_t p=8+(variationSeed%(text.size()-8));
        if(p<text.size()-1 && std::isalpha((unsigned char)text[p]) && std::isalpha((unsigned char)text[p+1]) && variationSeed%3==0)
            std::swap(text[p],text[p+1]);
    }

    if(mood=="playful" && variationSeed%3==0) text+=" lol";
    else if(mood=="warm" && variationSeed%4==0) text+=" :)";
    else if(mood=="guarded" && text.size()>90) {
        auto p=text.find('.');
        if(p!=std::string::npos) text=text.substr(0,p+1);
    }

    if((emoji=="medium" && variationSeed%5==0) || (emoji=="high" && variationSeed%2==0) || emoji=="very high") {
        static const char* marks[]={" 🙂"," 😅"," 👀"," 😂"," 🤷"};
        text+=marks[variationSeed%5];
    }
    return text;
}
