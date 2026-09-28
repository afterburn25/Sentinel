#include "Sentinel/Simulation/DeploymentRegistry.hpp"

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

std::string JsonEscape(std::string_view input) {
    std::string out;
    for(unsigned char c:input) {
        switch(c) {
            case '\\': out+="\\\\"; break;
            case '"': out+="\\\""; break;
            case '\n': out+="\\n"; break;
            case '\r': out+="\\r"; break;
            case '\t': out+="\\t"; break;
            default:
                if(c<0x20) {
                    static const char* hex="0123456789abcdef";
                    out+="\\u00";
                    out+=hex[(c>>4)&0xf];
                    out+=hex[c&0xf];
                } else out+=(char)c;
        }
    }
    return out;
}

std::string Escape(std::string_view input) {
    std::string out;
    for(char c:input) {
        if(c=='\\' || c=='\t' || c=='\n') {
            out+='\\';
            if(c=='\t') out+='t';
            else if(c=='\n') out+='n';
            else out+=c;
        } else out+=c;
    }
    return out;
}

std::string Unescape(std::string_view input) {
    std::string out;
    for(size_t i=0;i<input.size();++i) {
        if(input[i]=='\\' && i+1<input.size()) {
            char n=input[++i];
            if(n=='t') out+='\t';
            else if(n=='n') out+='\n';
            else out+=n;
        } else out+=input[i];
    }
    return out;
}

std::vector<std::string> Split(std::string_view line) {
    std::vector<std::string> out;
    std::string cur;
    bool esc=false;
    for(char c:line) {
        if(esc) { cur+='\\'; cur+=c; esc=false; continue; }
        if(c=='\\') { esc=true; continue; }
        if(c=='\t') { out.push_back(Unescape(cur)); cur.clear(); }
        else cur+=c;
    }
    if(esc) cur+='\\';
    out.push_back(Unescape(cur));
    return out;
}

}

std::string ToString(DeploymentStage stage) {
    switch(stage) {
        case DeploymentStage::Staged: return "STAGED";
        case DeploymentStage::Active: return "ACTIVE";
        case DeploymentStage::RolledBack: return "ROLLED_BACK";
        case DeploymentStage::Retired: return "RETIRED";
    }
    return "STAGED";
}

DeploymentPackage& DeploymentRegistry::Prepare(
    std::string candidateId,
    std::string candidateName,
    std::string foundationId,
    std::string foundationName,
    std::string adapterId,
    std::string adapterName,
    std::string personaName,
    std::string evaluationRunId,
    int evaluationScore)
{
    DeploymentPackage package;
    package.id="deploy-"+std::to_string(packages_.size()+1);
    package.createdUtc=NowUtc();
    package.candidateId=std::move(candidateId);
    package.candidateName=std::move(candidateName);
    package.foundationId=std::move(foundationId);
    package.foundationName=std::move(foundationName);
    package.adapterId=std::move(adapterId);
    package.adapterName=std::move(adapterName);
    package.personaName=std::move(personaName);
    package.evaluationRunId=std::move(evaluationRunId);
    package.evaluationScore=std::clamp(evaluationScore,0,100);
    package.versionLocked=true;
    package.stage=DeploymentStage::Staged;
    if(activeIndex_>=0 && activeIndex_<(int)packages_.size())
        package.previousDeploymentId=packages_[(size_t)activeIndex_].id;
    packages_.push_back(std::move(package));
    return packages_.back();
}

bool DeploymentRegistry::Activate(size_t index) {
    if(index>=packages_.size()) return false;

    if(activeIndex_>=0 && activeIndex_<(int)packages_.size() && activeIndex_!=(int)index) {
        previousIndex_=activeIndex_;
        packages_[(size_t)activeIndex_].stage=DeploymentStage::Retired;
    }

    activeIndex_=(int)index;
    packages_[index].stage=DeploymentStage::Active;
    if(packages_[index].activatedUtc.empty()) packages_[index].activatedUtc=NowUtc();
    return true;
}

bool DeploymentRegistry::Rollback() {
    if(previousIndex_<0 || previousIndex_>=(int)packages_.size()) return false;
    const int oldActive=activeIndex_;
    const int target=previousIndex_;

    if(oldActive>=0 && oldActive<(int)packages_.size()) {
        packages_[(size_t)oldActive].stage=DeploymentStage::RolledBack;
        packages_[(size_t)oldActive].rolledBackUtc=NowUtc();
    }

    activeIndex_=target;
    packages_[(size_t)target].stage=DeploymentStage::Active;
    previousIndex_=-1;
    return true;
}

void DeploymentRegistry::SetLocked(size_t index,bool locked) {
    if(index<packages_.size()) packages_[index].versionLocked=locked;
}

int DeploymentRegistry::ActiveIndex() const { return activeIndex_; }
int DeploymentRegistry::PreviousIndex() const { return previousIndex_; }

bool DeploymentRegistry::HasActiveLockedDeployment() const {
    return activeIndex_>=0 && activeIndex_<(int)packages_.size() && packages_[(size_t)activeIndex_].versionLocked;
}

std::vector<DeploymentPackage>& DeploymentRegistry::Packages(){ return packages_; }
const std::vector<DeploymentPackage>& DeploymentRegistry::Packages() const{ return packages_; }

std::string DeploymentRegistry::BuildManifest(size_t index) const {
    if(index>=packages_.size()) return {};
    const auto& p=packages_[index];
    std::ostringstream out;
    out<<"{\n";
    out<<"  \"schema\": \"sara-deployment-v1\",\n";
    out<<"  \"deployment_id\": \""<<JsonEscape(p.id)<<"\",\n";
    out<<"  \"created_utc\": \""<<JsonEscape(p.createdUtc)<<"\",\n";
    out<<"  \"candidate_id\": \""<<JsonEscape(p.candidateId)<<"\",\n";
    out<<"  \"candidate_name\": \""<<JsonEscape(p.candidateName)<<"\",\n";
    out<<"  \"foundation_id\": \""<<JsonEscape(p.foundationId)<<"\",\n";
    out<<"  \"foundation_name\": \""<<JsonEscape(p.foundationName)<<"\",\n";
    out<<"  \"adapter_id\": \""<<JsonEscape(p.adapterId)<<"\",\n";
    out<<"  \"adapter_name\": \""<<JsonEscape(p.adapterName)<<"\",\n";
    out<<"  \"persona_name\": \""<<JsonEscape(p.personaName)<<"\",\n";
    out<<"  \"evaluation_run_id\": \""<<JsonEscape(p.evaluationRunId)<<"\",\n";
    out<<"  \"evaluation_score\": "<<p.evaluationScore<<",\n";
    out<<"  \"version_locked\": "<<(p.versionLocked?"true":"false")<<",\n";
    out<<"  \"stage\": \""<<ToString(p.stage)<<"\"\n";
    out<<"}\n";
    return out.str();
}

void DeploymentRegistry::Save(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::trunc);
    out<<"META\t"<<activeIndex_<<"\t"<<previousIndex_<<"\n";
    for(const auto& p:packages_) {
        out<<"P\t"<<Escape(p.id)<<"\t"<<Escape(p.createdUtc)<<"\t"<<Escape(p.activatedUtc)<<"\t"<<Escape(p.rolledBackUtc)
           <<"\t"<<Escape(p.candidateId)<<"\t"<<Escape(p.candidateName)
           <<"\t"<<Escape(p.foundationId)<<"\t"<<Escape(p.foundationName)
           <<"\t"<<Escape(p.adapterId)<<"\t"<<Escape(p.adapterName)
           <<"\t"<<Escape(p.personaName)<<"\t"<<Escape(p.evaluationRunId)
           <<"\t"<<p.evaluationScore<<"\t"<<(p.versionLocked?1:0)<<"\t"<<(int)p.stage
           <<"\t"<<Escape(p.previousDeploymentId)<<"\n";
    }
}

void DeploymentRegistry::Load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if(!in) return;
    packages_.clear(); activeIndex_=-1; previousIndex_=-1;
    std::string line;
    while(std::getline(in,line)) {
        auto p=Split(line);
        if(p.empty()) continue;
        if(p[0]=="META" && p.size()==3) {
            try { activeIndex_=std::stoi(p[1]); previousIndex_=std::stoi(p[2]); } catch(...) {}
        } else if(p[0]=="P" && p.size()==17) {
            try {
                DeploymentPackage d;
                d.id=p[1]; d.createdUtc=p[2]; d.activatedUtc=p[3]; d.rolledBackUtc=p[4];
                d.candidateId=p[5]; d.candidateName=p[6];
                d.foundationId=p[7]; d.foundationName=p[8];
                d.adapterId=p[9]; d.adapterName=p[10];
                d.personaName=p[11]; d.evaluationRunId=p[12];
                d.evaluationScore=std::stoi(p[13]); d.versionLocked=std::stoi(p[14])!=0;
                d.stage=(DeploymentStage)std::stoi(p[15]); d.previousDeploymentId=p[16];
                packages_.push_back(std::move(d));
            } catch(...) {}
        }
    }
    if(activeIndex_<0 || activeIndex_>=(int)packages_.size()) activeIndex_=-1;
    if(previousIndex_<0 || previousIndex_>=(int)packages_.size()) previousIndex_=-1;
}

}
