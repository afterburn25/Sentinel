#include "Sentinel/Simulation/TrainingData.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace sentinel::simulation {
namespace {
std::string NowUtc() {
    auto now=std::chrono::system_clock::now();
    auto t=std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm,&t);
#else
    gmtime_r(&t,&tm);
#endif
    std::ostringstream out;
    out<<std::put_time(&tm,"%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
std::string Escape(const std::string& s) {
    std::string o;
    for(char c:s) {
        if(c=='\\' || c=='\t' || c=='\n' || c=='|') {
            o+='\\';
            if(c=='\t') o+='t';
            else if(c=='\n') o+='n';
            else o+=c;
        } else o+=c;
    }
    return o;
}
std::string Unescape(const std::string& s) {
    std::string o;
    for(size_t i=0;i<s.size();++i) {
        if(s[i]=='\\' && i+1<s.size()) {
            char n=s[++i];
            if(n=='t') o+='\t';
            else if(n=='n') o+='\n';
            else o+=n;
        } else o+=s[i];
    }
    return o;
}
std::vector<std::string> Split(const std::string& line,char delim) {
    std::vector<std::string> out;
    std::string cur;
    bool esc=false;
    for(char c:line) {
        if(esc) { cur+='\\'; cur+=c; esc=false; continue; }
        if(c=='\\') { esc=true; continue; }
        if(c==delim) { out.push_back(Unescape(cur)); cur.clear(); }
        else cur+=c;
    }
    out.push_back(Unescape(cur));
    return out;
}
}

std::string ToString(TrainingExampleState state) {
    switch(state) {
        case TrainingExampleState::Captured: return "CAPTURED";
        case TrainingExampleState::Review: return "REVIEW";
        case TrainingExampleState::Approved: return "APPROVED";
        case TrainingExampleState::Rejected: return "REJECTED";
    }
    return "CAPTURED";
}

TrainingExample& TrainingDataRegistry::Capture(
    std::string persona,std::string foundationId,std::string adapterId,std::string sourceConversationId,
    std::string input,std::string originalResponse,std::string correction,std::string targetResponse,
    std::string category)
{
    TrainingExample e;
    e.id="example-"+std::to_string(examples_.size()+1);
    e.persona=std::move(persona);
    e.foundationId=std::move(foundationId);
    e.adapterId=std::move(adapterId);
    e.sourceConversationId=std::move(sourceConversationId);
    e.input=std::move(input);
    e.originalResponse=std::move(originalResponse);
    e.correction=std::move(correction);
    e.targetResponse=std::move(targetResponse);
    e.category=category.empty()?"Behavior":std::move(category);
    e.createdUtc=NowUtc();
    e.state=TrainingExampleState::Review;
    examples_.push_back(std::move(e));
    return examples_.back();
}

void TrainingDataRegistry::SetState(size_t index,TrainingExampleState state) {
    if(index<examples_.size()) examples_[index].state=state;
}

DatasetSnapshot& TrainingDataRegistry::CreateSnapshot(std::string name) {
    DatasetSnapshot s;
    s.id="dataset-"+std::to_string(snapshots_.size()+1);
    s.name=std::move(name);
    for(const auto& e:examples_) if(e.state==TrainingExampleState::Approved) s.exampleIds.push_back(e.id);
    snapshots_.push_back(std::move(s));
    return snapshots_.back();
}

std::vector<TrainingExample>& TrainingDataRegistry::Examples(){ return examples_; }
const std::vector<TrainingExample>& TrainingDataRegistry::Examples() const{ return examples_; }
std::vector<DatasetSnapshot>& TrainingDataRegistry::Snapshots(){ return snapshots_; }
const std::vector<DatasetSnapshot>& TrainingDataRegistry::Snapshots() const{ return snapshots_; }

size_t TrainingDataRegistry::Count(TrainingExampleState state) const {
    return (size_t)std::count_if(examples_.begin(),examples_.end(),[&](const auto& e){return e.state==state;});
}

void TrainingDataRegistry::Save(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::trunc);
    for(const auto& e:examples_) {
        out<<"E\t"<<Escape(e.id)<<"\t"<<Escape(e.persona)<<"\t"<<Escape(e.foundationId)<<"\t"<<Escape(e.adapterId)
           <<"\t"<<Escape(e.sourceConversationId)<<"\t"<<Escape(e.input)<<"\t"<<Escape(e.originalResponse)
           <<"\t"<<Escape(e.correction)<<"\t"<<Escape(e.targetResponse)<<"\t"<<Escape(e.category)
           <<"\t"<<Escape(e.createdUtc)<<"\t"<<Escape(e.reviewer)<<"\t"<<(int)e.state<<"\n";
    }
    for(const auto& s:snapshots_) {
        std::string ids;
        for(size_t i=0;i<s.exampleIds.size();++i) { if(i) ids+='|'; ids+=Escape(s.exampleIds[i]); }
        out<<"D\t"<<Escape(s.id)<<"\t"<<Escape(s.name)<<"\t"<<ids<<"\n";
    }
}

void TrainingDataRegistry::Load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if(!in) return;
    examples_.clear(); snapshots_.clear();
    std::string line;
    while(std::getline(in,line)) {
        auto p=Split(line,'\t');
        if(p.empty()) continue;
        if(p[0]=="E" && (p.size()==11 || p.size()==14)) {
            try {
                TrainingExample e;
                e.id=p[1]; e.persona=p[2]; e.foundationId=p[3]; e.adapterId=p[4];
                e.sourceConversationId=p[5]; e.input=p[6]; e.originalResponse=p[7];
                e.correction=p[8]; e.targetResponse=p[9];
                if(p.size()==14) {
                    e.category=p[10]; e.createdUtc=p[11]; e.reviewer=p[12];
                    e.state=(TrainingExampleState)std::stoi(p[13]);
                } else {
                    e.category="Behavior";
                    e.state=(TrainingExampleState)std::stoi(p[10]);
                }
                examples_.push_back(std::move(e));
            } catch(...) {}
        } else if(p[0]=="D" && p.size()==4) {
            DatasetSnapshot s{p[1],p[2],{}};
            auto ids=Split(p[3],'|');
            for(auto& id:ids) if(!id.empty()) s.exampleIds.push_back(id);
            snapshots_.push_back(std::move(s));
        }
    }
}

}
