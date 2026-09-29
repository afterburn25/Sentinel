#include "Sentinel/Simulation/ModelRegistry.hpp"
#include "Sentinel/Simulation/EvaluationSuite.hpp"
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
RegisteredModel& ModelRegistry::Register(std::string endpoint,std::string modelName) {
    for(auto& m:models_) if(m.endpoint==endpoint && m.modelName==modelName) return m;
    RegisteredModel m;
    m.endpoint=std::move(endpoint);
    m.modelName=std::move(modelName);
    m.id="model-"+std::to_string(models_.size()+1);
    models_.push_back(std::move(m));
    return models_.back();
}
bool ModelRegistry::Approve(
    size_t i,
    const EvaluationRun& evaluation,
    std::string_view currentFoundationId,
    std::string_view currentAdapterId)
{
    if(i>=models_.size()) return false;

    auto& model=models_[i];
    if(model.stage==ModelStage::Retired) return false;
    if(evaluation.id.empty() || evaluation.candidateId!=model.id) return false;
    if(!EvaluationPassedApprovalGate(evaluation)) return false;
    if(evaluation.foundationId!=currentFoundationId || evaluation.adapterId!=currentAdapterId) return false;

    model.evaluationScore=std::clamp(evaluation.overallScore,0,100);
    model.approvedEvaluationRunId=evaluation.id;
    model.approvedFoundationId=evaluation.foundationId;
    model.approvedAdapterId=evaluation.adapterId;
    if(model.stage==ModelStage::Candidate) model.stage=ModelStage::Approved;
    return model.stage==ModelStage::Approved || model.stage==ModelStage::Active;
}
void ModelRegistry::Activate(size_t i) {
    if(i>=models_.size()) return;
    if(models_[i].stage!=ModelStage::Approved && models_[i].stage!=ModelStage::Active) return;
    if(models_[i].approvedEvaluationRunId.empty()) return;
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
    if(models_[(size_t)target].approvedEvaluationRunId.empty()) return false;
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
        out<<m.id<<"\t"<<m.endpoint<<"\t"<<m.modelName<<"\t"<<m.evaluationScore<<"\t"<<m.latencyMs<<"\t"<<(int)m.stage
           <<"\t"<<m.approvedEvaluationRunId<<"\t"<<m.approvedFoundationId<<"\t"<<m.approvedAdapterId<<"\n";
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
        if(p.size()!=6 && p.size()!=9) continue;
        try {
            RegisteredModel m;
            m.id=p[0];
            m.endpoint=p[1];
            m.modelName=p[2];
            m.evaluationScore=std::stoi(p[3]);
            m.latencyMs=std::stoll(p[4]);
            m.stage=(ModelStage)std::stoi(p[5]);
            if(p.size()==9) {
                m.approvedEvaluationRunId=p[6];
                m.approvedFoundationId=p[7];
                m.approvedAdapterId=p[8];
            } else if(m.stage==ModelStage::Approved || m.stage==ModelStage::Active) {
                // Legacy model-registry rows predate exact approval proof. Demote them so
                // they must pass a current evaluation before activation/deployment.
                m.stage=ModelStage::Candidate;
                m.evaluationScore=0;
            }
            if(m.stage==ModelStage::Active) activeIndex_=(int)models_.size();
            models_.push_back(std::move(m));
        } catch(...) {}
    }
}
}
