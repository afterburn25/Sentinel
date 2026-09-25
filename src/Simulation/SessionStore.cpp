#include "Sentinel/Simulation/SessionStore.hpp"
#include <fstream>
#include <string>

namespace sentinel::simulation {
namespace {
std::string Escape(std::string s) {
    std::string out;
    for(char c:s) {
        if(c=='\\') out+="\\\\";
        else if(c=='\n') out+="\\n";
        else if(c=='\t') out+="\\t";
        else out+=c;
    }
    return out;
}
std::string Unescape(const std::string& s) {
    std::string out;
    for(size_t i=0;i<s.size();++i) {
        if(s[i]=='\\' && i+1<s.size()) {
            char n=s[++i];
            out+=(n=='n'?'\n':n=='t'?'\t':n);
        } else out+=s[i];
    }
    return out;
}
}
void SaveSession(const std::filesystem::path& path,const ModelContext& context) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::trunc);
    out<<"scenario\t"<<Escape(context.scenario)<<"\n";
    out<<"persona\t"<<Escape(context.personaSummary)<<"\n";
    for(const auto& turn:context.history) {
        out<<"turn\t"<<static_cast<int>(turn.speaker)<<"\t"<<Escape(turn.text)<<"\n";
    }
}
bool LoadSession(const std::filesystem::path& path,ModelContext& context) {
    std::ifstream in(path);
    if(!in) return false;
    ModelContext loaded;
    std::string line;
    while(std::getline(in,line)) {
        auto a=line.find('\t');
        if(a==std::string::npos) continue;
        auto type=line.substr(0,a);
        if(type=="scenario") loaded.scenario=Unescape(line.substr(a+1));
        else if(type=="persona") loaded.personaSummary=Unescape(line.substr(a+1));
        else if(type=="turn") {
            auto b=line.find('\t',a+1);
            if(b==std::string::npos) continue;
            try {
                int speaker=std::stoi(line.substr(a+1,b-a-1));
                if(speaker<0 || speaker>2) continue;
                loaded.history.push_back({static_cast<ChatTurn::Speaker>(speaker),Unescape(line.substr(b+1))});
            } catch(...) {}
        }
    }
    if(loaded.history.empty()) return false;
    context=std::move(loaded);
    return true;
}
}
