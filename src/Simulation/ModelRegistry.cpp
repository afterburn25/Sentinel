#include "Sentinel/Simulation/ModelRegistry.hpp"
#include <algorithm>
#include <fstream>

namespace sentinel::simulation {
std::string ToString(ModelStage stage) {
    switch(stage) {
        case ModelStage::Candidate: return "CANDIDATE";
        case ModelStage::Approved: return "APPROVED";
        case ModelStage::Active: return "ACTIVE";
        case ModelStage::Retired: return "RETIRED";
    }
    return "CANDIDATE";
}

std::string ToString(FoundationStage stage) {
    switch(stage) {
        case FoundationStage::Base: return "BASE";
        case FoundationStage::Candidate: return "CANDIDATE";
        case FoundationStage::Approved: return "APPROVED";
        case FoundationStage::Active: return "ACTIVE";
        case FoundationStage::Retired: return "RETIRED";
    }
    return "CANDIDATE";
}

FoundationModel& FoundationRegistry::EnsureBase(std::string name,std::string version) {
    for(auto& m:models_) if(m.immutableBase && m.name==name && m.version==version) return m;
    FoundationModel m;
    m.id="foundation-"+std::to_string(models_.size()+1);
    m.name=std::move(name);
    m.version=std::move(version);
    m.immutableBase=true;
    m.stage=FoundationStage::Base;
    models_.push_back(std::move(m));
    return models_.back();
}

FoundationModel& FoundationRegistry::CreateFork(size_t parentIndex,std::string name,std::string version) {
    if(parentIndex>=models_.size()) throw std::out_of_range("foundation parent index");
    FoundationModel m;
    m.id="foundation-"+std::to_string(models_.size()+1);
    m.name=std::move(name);
    m.parentId=models_[parentIndex].id;
    m.version=std::move(version);
    m.immutableBase=false;
    m.stage=FoundationStage::Candidate;
    models_.push_back(std::move(m));
    return models_.back();
}

void FoundationRegistry::Approve(size_t i) {
    if(i<models_.size() && models_[i].stage==FoundationStage::Candidate) models_[i].stage=FoundationStage::Approved;
}

void FoundationRegistry::Activate(size_t i) {
    if(i>=models_.size() || models_[i].immutableBase) return;
    if(models_[i].stage!=FoundationStage::Approved && models_[i].stage!=FoundationStage::Active) return;
    if(activeIndex_>=0 && activeIndex_<(int)models_.size()) {
        previousActiveIndex_=activeIndex_;
        models_[(size_t)activeIndex_].stage=FoundationStage::Approved;
    }
    activeIndex_=(int)i;
    models_[i].stage=FoundationStage::Active;
}

bool FoundationRegistry::Rollback() {
    if(previousActiveIndex_<0 || previousActiveIndex_>=(int)models_.size()) return false;
    int target=previousActiveIndex_;
    if(activeIndex_>=0 && activeIndex_<(int)models_.size()) models_[(size_t)activeIndex_].stage=FoundationStage::Approved;
    activeIndex_=target;
    models_[(size_t)activeIndex_].stage=FoundationStage::Active;
    previousActiveIndex_=-1;
    return true;
}

std::vector<FoundationModel>& FoundationRegistry::Models(){ return models_; }
const std::vector<FoundationModel>& FoundationRegistry::Models() const{ return models_; }
int FoundationRegistry::ActiveIndex() const{ return activeIndex_; }

void FoundationRegistry::Save(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::trunc);
    for(const auto& m:models_)
        out<<m.id<<"\t"<<m.name<<"\t"<<m.parentId<<"\t"<<m.version<<"\t"<<(m.immutableBase?1:0)<<"\t"<<(int)m.stage<<"\n";
}

void FoundationRegistry::Load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if(!in) return;
    models_.clear(); activeIndex_=-1; previousActiveIndex_=-1;
    std::string line;
    while(std::getline(in,line)) {
        std::vector<std::string> p; size_t start=0;
        for(;;) {
            auto pos=line.find('\t',start);
            if(pos==std::string::npos) { p.push_back(line.substr(start)); break; }
            p.push_back(line.substr(start,pos-start)); start=pos+1;
        }
        if(p.size()!=6) continue;
        try {
            FoundationModel m{p[0],p[1],p[2],p[3],std::stoi(p[4])!=0,(FoundationStage)std::stoi(p[5])};
            if(m.stage==FoundationStage::Active) activeIndex_=(int)models_.size();
            models_.push_back(std::move(m));
        } catch(...) {}
    }
}
RegisteredModel& ModelRegistry::Register(std::string endpoint,std::string modelName) {
    for(auto& m:models_) if(m.endpoint==endpoint && m.modelName==modelName) return m;
    RegisteredModel m;
    m.endpoint=std::move(endpoint);
    m.modelName=std::move(modelName);
    m.id="model-"+std::to_string(models_.size()+1);
    models_.push_back(std::move(m));
    return models_.back();
}
void ModelRegistry::Approve(size_t i) {
    if(i<models_.size() && models_[i].stage==ModelStage::Candidate) models_[i].stage=ModelStage::Approved;
}
void ModelRegistry::Activate(size_t i) {
    if(i>=models_.size()) return;
    if(models_[i].stage!=ModelStage::Approved && models_[i].stage!=ModelStage::Active) return;
    if(activeIndex_>=0 && activeIndex_<(int)models_.size()) {
        previousActiveIndex_=activeIndex_;
        models_[(size_t)activeIndex_].stage=ModelStage::Approved;
    }
    activeIndex_=(int)i;
    models_[i].stage=ModelStage::Active;
}
void ModelRegistry::Retire(size_t i) {
    if(i>=models_.size()) return;
    models_[i].stage=ModelStage::Retired;
    if(activeIndex_==(int)i) activeIndex_=-1;
}
bool ModelRegistry::Rollback() {
    if(previousActiveIndex_<0 || previousActiveIndex_>=(int)models_.size()) return false;
    int target=previousActiveIndex_;
    if(activeIndex_>=0 && activeIndex_<(int)models_.size()) models_[(size_t)activeIndex_].stage=ModelStage::Approved;
    activeIndex_=target;
    models_[(size_t)activeIndex_].stage=ModelStage::Active;
    previousActiveIndex_=-1;
    return true;
}
std::vector<RegisteredModel>& ModelRegistry::Models(){ return models_; }
const std::vector<RegisteredModel>& ModelRegistry::Models() const{ return models_; }
int ModelRegistry::ActiveIndex() const{ return activeIndex_; }
void ModelRegistry::Save(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::trunc);
    for(const auto& m:models_)
        out<<m.id<<"\t"<<m.endpoint<<"\t"<<m.modelName<<"\t"<<m.evaluationScore<<"\t"<<m.latencyMs<<"\t"<<(int)m.stage<<"\n";
}
void ModelRegistry::Load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if(!in) return;
    models_.clear(); activeIndex_=-1; previousActiveIndex_=-1;
    std::string line;
    while(std::getline(in,line)) {
        std::vector<std::string> p; size_t start=0;
        for(;;) {
            auto pos=line.find('\t',start);
            if(pos==std::string::npos) { p.push_back(line.substr(start)); break; }
            p.push_back(line.substr(start,pos-start)); start=pos+1;
        }
        if(p.size()!=6) continue;
        try {
            RegisteredModel m{p[0],p[1],p[2],std::stoi(p[3]),std::stoll(p[4]),(ModelStage)std::stoi(p[5])};
            if(m.stage==ModelStage::Active) activeIndex_=(int)models_.size();
            models_.push_back(std::move(m));
        } catch(...) {}
    }
}
}
