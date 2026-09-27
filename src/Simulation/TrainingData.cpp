#include "Sentinel/Simulation/TrainingData.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

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

DatasetSnapshot& TrainingDataRegistry::CreateSnapshot(std::string name,std::string parentId) {
    DatasetSnapshot s;
    s.id="dataset-"+std::to_string(snapshots_.size()+1);
    s.name=std::move(name);
    if(parentId.empty() && !snapshots_.empty()) parentId=snapshots_.back().id;
    s.parentId=std::move(parentId);
    s.createdUtc=NowUtc();
    for(const auto& e:examples_) if(e.state==TrainingExampleState::Approved) s.exampleIds.push_back(e.id);
    snapshots_.push_back(std::move(s));
    return snapshots_.back();
}

void TrainingDataRegistry::ExportSnapshot(size_t index,const std::filesystem::path& path) const {
    if(index>=snapshots_.size()) throw std::out_of_range("dataset snapshot index");
    std::filesystem::create_directories(path.parent_path().empty()?std::filesystem::path("."):path.parent_path());
    std::ofstream out(path,std::ios::trunc);
    const auto& s=snapshots_[index];
    out<<"SARA_DATASET_V1\n";
    out<<"S\t"<<Escape(s.id)<<"\t"<<Escape(s.name)<<"\t"<<Escape(s.parentId)<<"\t"<<Escape(s.createdUtc)<<"\n";
    for(const auto& id:s.exampleIds) {
        auto it=std::find_if(examples_.begin(),examples_.end(),[&](const auto& e){return e.id==id;});
        if(it==examples_.end()) continue;
        const auto& e=*it;
        out<<"E\t"<<Escape(e.id)<<"\t"<<Escape(e.persona)<<"\t"<<Escape(e.foundationId)<<"\t"<<Escape(e.adapterId)
           <<"\t"<<Escape(e.sourceConversationId)<<"\t"<<Escape(e.input)<<"\t"<<Escape(e.originalResponse)
           <<"\t"<<Escape(e.correction)<<"\t"<<Escape(e.targetResponse)<<"\t"<<Escape(e.category)
           <<"\t"<<Escape(e.createdUtc)<<"\t"<<Escape(e.reviewer)<<"\t"<<(int)e.state<<"\n";
    }
}

DatasetSnapshot& TrainingDataRegistry::ImportSnapshot(const std::filesystem::path& path) {
    std::ifstream in(path);
    if(!in) throw std::runtime_error("cannot open dataset snapshot");
    std::string line;
    if(!std::getline(in,line) || line!="SARA_DATASET_V1") throw std::runtime_error("unsupported dataset snapshot format");

    DatasetSnapshot imported;
    std::vector<TrainingExample> importedExamples;
    while(std::getline(in,line)) {
        auto p=Split(line,'\t');
        if(p.empty()) continue;
        if(p[0]=="S" && p.size()>=5) {
            imported.id=p[1]; imported.name=p[2]; imported.parentId=p[3]; imported.createdUtc=p[4];
        } else if(p[0]=="E" && p.size()==14) {
            TrainingExample e;
            e.id=p[1]; e.persona=p[2]; e.foundationId=p[3]; e.adapterId=p[4]; e.sourceConversationId=p[5];
            e.input=p[6]; e.originalResponse=p[7]; e.correction=p[8]; e.targetResponse=p[9];
            e.category=p[10]; e.createdUtc=p[11]; e.reviewer=p[12]; e.state=(TrainingExampleState)std::stoi(p[13]);
            importedExamples.push_back(std::move(e));
        }
    }
    if(imported.id.empty()) throw std::runtime_error("dataset snapshot has no id");

    auto uniqueId=[&](std::string base,const auto& exists){
        if(!exists(base)) return base;
        for(int i=2;;++i) {
            auto candidate=base+"-import-"+std::to_string(i);
            if(!exists(candidate)) return candidate;
        }
    };

    std::unordered_map<std::string,std::string> remap;
    for(auto& e:importedExamples) {
        const auto original=e.id;
        e.id=uniqueId(e.id,[&](const std::string& id){
            return std::any_of(examples_.begin(),examples_.end(),[&](const auto& x){return x.id==id;});
        });
        remap[original]=e.id;
        examples_.push_back(std::move(e));
    }

    imported.id=uniqueId(imported.id,[&](const std::string& id){
        return std::any_of(snapshots_.begin(),snapshots_.end(),[&](const auto& x){return x.id==id;});
    });
    imported.exampleIds.clear();
    for(const auto& e:importedExamples) {
        auto it=remap.find(e.id);
        imported.exampleIds.push_back(it==remap.end()?e.id:it->second);
    }
    // importedExamples have been moved, so rebuild IDs from the newly appended range.
    imported.exampleIds.clear();
    const size_t count=importedExamples.size();
    const size_t start=examples_.size()>=count?examples_.size()-count:0;
    for(size_t i=start;i<examples_.size();++i) imported.exampleIds.push_back(examples_[i].id);
    if(imported.createdUtc.empty()) imported.createdUtc=NowUtc();
    snapshots_.push_back(std::move(imported));
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
        out<<"D\t"<<Escape(s.id)<<"\t"<<Escape(s.name)<<"\t"<<Escape(s.parentId)<<"\t"<<Escape(s.createdUtc)<<"\t"<<ids<<"\n";
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
        } else if(p[0]=="D" && (p.size()==4 || p.size()==6)) {
            DatasetSnapshot s;
            s.id=p[1]; s.name=p[2];
            std::string idsText;
            if(p.size()==6) { s.parentId=p[3]; s.createdUtc=p[4]; idsText=p[5]; }
            else idsText=p[3];
            auto ids=Split(idsText,'|');
            for(auto& id:ids) if(!id.empty()) s.exampleIds.push_back(id);
            snapshots_.push_back(std::move(s));
        }
    }
}

}
