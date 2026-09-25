#include "Sentinel/Simulation/SettingsStore.hpp"
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace sentinel::simulation {
namespace {
std::string Escape(const std::string& s) {
    std::string o;
    for(char c:s) {
        if(c=='\\' || c=='\n' || c=='=') { o+='\\'; if(c=='\n') o+='n'; else o+=c; }
        else o+=c;
    }
    return o;
}
std::string Unescape(const std::string& s) {
    std::string o;
    for(size_t i=0;i<s.size();++i) {
        if(s[i]=='\\' && i+1<s.size()) {
            char n=s[++i]; o+=(n=='n'?'\n':n);
        } else o+=s[i];
    }
    return o;
}
}
SimulationSettings LoadSimulationSettings(const std::filesystem::path& path) {
    SimulationSettings s;
    std::ifstream in(path);
    std::string line;
    while(std::getline(in,line)) {
        auto eq=line.find('=');
        if(eq==std::string::npos) continue;
        auto k=line.substr(0,eq),v=Unescape(line.substr(eq+1));
        try {
            if(k=="endpoint") s.endpoint=v;
            else if(k=="model") s.model=v;
            else if(k=="temperature") s.temperature=std::stod(v);
            else if(k=="maxTokens") s.maxTokens=std::stoi(v);
            else if(k=="minDelayMs") s.minDelayMs=std::stoi(v);
            else if(k=="maxDelayMs") s.maxDelayMs=std::stoi(v);
            else if(k=="persona.name") s.persona.name=v;
            else if(k=="persona.age") s.persona.age=std::stoi(v);
            else if(k=="persona.location") s.persona.location=v;
            else if(k=="persona.background") s.persona.background=v;
            else if(k=="persona.interests") s.persona.interests=v;
            else if(k=="persona.writingStyle") s.persona.writingStyle=v;
            else if(k=="scenario.name") s.scenario.name=v;
            else if(k=="scenario.objective") s.scenario.objective=v;
            else if(k=="scenario.openingContext") s.scenario.openingContext=v;
            else if(k=="scenario.seed") s.scenario.seed=(unsigned int)std::stoul(v);
            else if(k=="ageState") s.ageState=AgeStateFromString(v);
        } catch(...) {}
    }
    if(s.minDelayMs<500) s.minDelayMs=500;
    if(s.maxDelayMs<s.minDelayMs) s.maxDelayMs=s.minDelayMs;
    return s;
}
void SaveSimulationSettings(const std::filesystem::path& path,const SimulationSettings& s) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::trunc);
    out<<"endpoint="<<Escape(s.endpoint)<<"\n";
    out<<"model="<<Escape(s.model)<<"\n";
    out<<"temperature="<<s.temperature<<"\n";
    out<<"maxTokens="<<s.maxTokens<<"\n";
    out<<"minDelayMs="<<s.minDelayMs<<"\n";
    out<<"maxDelayMs="<<s.maxDelayMs<<"\n";
    out<<"persona.name="<<Escape(s.persona.name)<<"\n";
    out<<"persona.age="<<s.persona.age<<"\n";
    out<<"persona.location="<<Escape(s.persona.location)<<"\n";
    out<<"persona.background="<<Escape(s.persona.background)<<"\n";
    out<<"persona.interests="<<Escape(s.persona.interests)<<"\n";
    out<<"persona.writingStyle="<<Escape(s.persona.writingStyle)<<"\n";
    out<<"scenario.name="<<Escape(s.scenario.name)<<"\n";
    out<<"scenario.objective="<<Escape(s.scenario.objective)<<"\n";
    out<<"scenario.openingContext="<<Escape(s.scenario.openingContext)<<"\n";
    out<<"scenario.seed="<<s.scenario.seed<<"\n";
    out<<"ageState="<<ToString(s.ageState)<<"\n";
}
}
