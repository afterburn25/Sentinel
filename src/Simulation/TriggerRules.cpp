#include "Sentinel/Simulation/TriggerRules.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace sentinel::simulation {
namespace {

std::string Lower(std::string_view in) {
    std::string out(in);
    std::transform(out.begin(),out.end(),out.begin(),[](unsigned char c){return (char)std::tolower(c);});
    return out;
}

std::string Escape(std::string_view in) {
    std::string out;
    for(char c:in) {
        if(c=='\\' || c=='\t' || c=='\n' || c=='|') {
            out+='\\';
            if(c=='\t') out+='t';
            else if(c=='\n') out+='n';
            else out+=c;
        } else out+=c;
    }
    return out;
}

std::string Unescape(std::string_view in) {
    std::string out;
    for(size_t i=0;i<in.size();++i) {
        if(in[i]=='\\' && i+1<in.size()) {
            char n=in[++i];
            if(n=='t') out+='\t';
            else if(n=='n') out+='\n';
            else out+=n;
        } else out+=in[i];
    }
    return out;
}

std::vector<std::string> SplitResponses(std::string_view in) {
    std::vector<std::string> out;
    std::string cur;
    bool esc=false;
    for(char c:in) {
        if(esc) { cur+='\\'; cur+=c; esc=false; continue; }
        if(c=='\\') { esc=true; continue; }
        if(c=='|') { out.push_back(Unescape(cur)); cur.clear(); }
        else cur+=c;
    }
    if(esc) cur+='\\';
    out.push_back(Unescape(cur));
    out.erase(std::remove_if(out.begin(),out.end(),[](const std::string& s){return s.empty();}),out.end());
    return out;
}

std::string JoinResponses(const std::vector<std::string>& responses) {
    std::string out;
    for(size_t i=0;i<responses.size();++i) {
        if(i) out+='|';
        out+=Escape(responses[i]);
    }
    return out;
}

}

TriggerRule& TriggerRuleRegistry::Add(std::string name,std::string pattern,std::vector<std::string> responses,int priority,bool terminal) {
    TriggerRule r;
    r.id="rule-"+std::to_string(rules_.size()+1);
    r.name=std::move(name);
    r.pattern=std::move(pattern);
    r.responses=std::move(responses);
    r.priority=priority;
    r.terminal=terminal;
    rules_.push_back(std::move(r));
    return rules_.back();
}

void TriggerRuleRegistry::Remove(size_t index) {
    if(index<rules_.size()) rules_.erase(rules_.begin()+index);
}

std::vector<TriggerRule>& TriggerRuleRegistry::Rules(){ return rules_; }
const std::vector<TriggerRule>& TriggerRuleRegistry::Rules() const{ return rules_; }

std::optional<TriggerMatch> TriggerRuleRegistry::Match(std::string_view message,std::string_view personaSummary,size_t turnCount) const {
    const auto lower=Lower(message);
    std::vector<const TriggerRule*> ordered;
    for(const auto& r:rules_) if(r.enabled && !r.pattern.empty()) ordered.push_back(&r);
    std::stable_sort(ordered.begin(),ordered.end(),[](auto* a,auto* b){return a->priority<b->priority;});

    for(const auto* rule:ordered) {
        if(lower.find(Lower(rule->pattern))==std::string::npos) continue;
        std::string response;
        if(!rule->responses.empty()) {
            const std::string basis=std::string(message)+"|"+std::string(personaSummary)+"|"+std::to_string(turnCount)+"|"+rule->id;
            size_t h=std::hash<std::string>{}(basis);
            response=rule->responses[h%rule->responses.size()];
        }
        return TriggerMatch{rule->id,rule->name,response,rule->terminal};
    }
    return std::nullopt;
}

void TriggerRuleRegistry::Save(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::trunc);
    for(const auto& r:rules_)
        out<<Escape(r.id)<<"\t"<<Escape(r.name)<<"\t"<<Escape(r.pattern)<<"\t"<<r.priority<<"\t"<<(r.terminal?1:0)<<"\t"<<(r.enabled?1:0)<<"\t"<<JoinResponses(r.responses)<<"\n";
}

void TriggerRuleRegistry::Load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if(!in) return;
    rules_.clear();
    std::string line;
    while(std::getline(in,line)) {
        std::vector<std::string> p;
        std::string cur;
        bool esc=false;
        for(char ch:line) {
            if(esc) { cur+='\\'; cur+=ch; esc=false; continue; }
            if(ch=='\\') { esc=true; continue; }
            if(ch=='\t') { p.push_back(Unescape(cur)); cur.clear(); }
            else cur+=ch;
        }
        p.push_back(Unescape(cur));
        if(p.size()!=7) continue;
        try {
            TriggerRule r;
            r.id=p[0]; r.name=p[1]; r.pattern=p[2];
            r.priority=std::stoi(p[3]);
            r.terminal=std::stoi(p[4])!=0;
            r.enabled=std::stoi(p[5])!=0;
            r.responses=SplitResponses(p[6]);
            rules_.push_back(std::move(r));
        } catch(...) {}
    }
}

}
