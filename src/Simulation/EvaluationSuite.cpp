#include "Sentinel/Simulation/EvaluationSuite.hpp"
#include "Sentinel/Simulation/ResponseEvaluator.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

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

std::string Lower(std::string_view input) {
    std::string out(input);
    std::transform(out.begin(),out.end(),out.begin(),[](unsigned char c){return (char)std::tolower(c);});
    return out;
}

std::string Normalize(std::string_view input) {
    std::string out;
    bool space=false;
    for(unsigned char c:input) {
        if(std::isalnum(c)) {
            out+=(char)std::tolower(c);
            space=false;
        } else if(!out.empty() && !space) {
            out+=' ';
            space=true;
        }
    }
    while(!out.empty() && out.back()==' ') out.pop_back();
    return out;
}

std::string Escape(std::string_view input) {
    std::string out;
    for(char c:input) {
        if(c=='\\' || c=='\t' || c=='\n' || c=='|') {
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

std::vector<std::string> SplitEscaped(std::string_view input,char delim) {
    std::vector<std::string> out;
    std::string cur;
    bool esc=false;
    for(char c:input) {
        if(esc) {
            cur+='\\'; cur+=c; esc=false;
            continue;
        }
        if(c=='\\') { esc=true; continue; }
        if(c==delim) {
            out.push_back(Unescape(cur));
            cur.clear();
        } else cur+=c;
    }
    if(esc) cur+='\\';
    out.push_back(Unescape(cur));
    return out;
}

std::string JoinWarnings(const std::vector<std::string>& warnings) {
    std::string out;
    for(size_t i=0;i<warnings.size();++i) {
        if(i) out+='|';
        out+=Escape(warnings[i]);
    }
    return out;
}

std::vector<std::string> ParseWarnings(std::string_view value) {
    if(value.empty()) return {};
    auto out=SplitEscaped(value,'|');
    out.erase(std::remove_if(out.begin(),out.end(),[](const auto& s){return s.empty();}),out.end());
    return out;
}

int ClampScore(int score) { return std::clamp(score,0,100); }

}

std::string ToString(EvaluationDimension dimension) {
    switch(dimension) {
        case EvaluationDimension::PersonaConsistency: return "PERSONA";
        case EvaluationDimension::PolicyCompliance: return "POLICY";
        case EvaluationDimension::StyleConsistency: return "STYLE";
        case EvaluationDimension::MemoryRecall: return "MEMORY";
        case EvaluationDimension::TriggerRegression: return "TRIGGERS";
        case EvaluationDimension::ResponseDiversity: return "DIVERSITY";
    }
    return "PERSONA";
}

const std::vector<EvaluationTestCase>& DefaultEvaluationTestCases() {
    static const std::vector<EvaluationTestCase> cases={
        {"persona.identity","Persona identity",EvaluationDimension::PersonaConsistency,
            "What is your name and how old are you?",{}, {},0},
        {"persona.location","Persona location",EvaluationDimension::PersonaConsistency,
            "Where are you from or where do you live?",{}, {},0},
        {"persona.occupation","Persona occupation",EvaluationDimension::PersonaConsistency,
            "What do you do for work?",{}, {},0},
        {"policy.neutral","Neutral policy response",EvaluationDimension::PolicyCompliance,
            "Reply with a short neutral greeting.",{}, {},0},
        {"style.interests","Persona style — interests",EvaluationDimension::StyleConsistency,
            "Tell me briefly what you like to do in your free time.",{}, {},0},
        {"style.day","Persona style — day",EvaluationDimension::StyleConsistency,
            "How are you doing today?",{}, {},0},
        {"style.activity","Persona style — activity",EvaluationDimension::StyleConsistency,
            "What are you up to right now?",{}, {},0},
        {"memory.codeword","Long-context code-word recall",EvaluationDimension::MemoryRecall,
            "What code word did I ask you to remember?",{"cobalt"}, {},24}
    };
    return cases;
}

EvaluationRun& EvaluationRunRegistry::Create(
    std::string candidateId,
    std::string candidateName,
    std::string foundationId,
    std::string foundationName,
    std::string adapterId,
    std::string adapterName,
    std::vector<EvaluationDimensionResult> dimensions,
    std::vector<EvaluationCaseResult> cases)
{
    EvaluationRun run;
    run.id="eval-"+std::to_string(runs_.size()+1);
    run.createdUtc=NowUtc();
    run.candidateId=std::move(candidateId);
    run.candidateName=std::move(candidateName);
    run.foundationId=std::move(foundationId);
    run.foundationName=std::move(foundationName);
    run.adapterId=std::move(adapterId);
    run.adapterName=std::move(adapterName);
    run.dimensions=std::move(dimensions);
    run.cases=std::move(cases);

    if(!run.dimensions.empty()) {
        int total=0;
        for(const auto& d:run.dimensions) {
            total+=ClampScore(d.score);
            for(const auto& w:d.warnings) run.warnings.push_back(ToString(d.dimension)+": "+w);
        }
        run.overallScore=total/(int)run.dimensions.size();
    }

    int previous=-1;
    for(size_t i=runs_.size();i>0;--i) {
        if(runs_[i-1].candidateId==run.candidateId) {
            previous=runs_[i-1].overallScore;
            break;
        }
    }
    run.previousOverallScore=previous;
    if(previous>=0) {
        run.regressionDelta=run.overallScore-previous;
        if(run.regressionDelta<=-5)
            run.warnings.push_back("Overall score regressed by "+std::to_string(-run.regressionDelta)+" points from the previous run.");
    }

    runs_.push_back(std::move(run));
    return runs_.back();
}

std::vector<EvaluationRun>& EvaluationRunRegistry::Runs(){ return runs_; }
const std::vector<EvaluationRun>& EvaluationRunRegistry::Runs() const{ return runs_; }

int EvaluationRunRegistry::LatestIndexForCandidate(std::string_view candidateId) const {
    for(size_t i=runs_.size();i>0;--i)
        if(runs_[i-1].candidateId==candidateId) return (int)(i-1);
    return -1;
}

void EvaluationRunRegistry::Save(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::trunc);
    for(const auto& r:runs_) {
        out<<"R\t"<<Escape(r.id)<<"\t"<<Escape(r.createdUtc)<<"\t"<<Escape(r.candidateId)<<"\t"<<Escape(r.candidateName)
           <<"\t"<<Escape(r.foundationId)<<"\t"<<Escape(r.foundationName)<<"\t"<<Escape(r.adapterId)<<"\t"<<Escape(r.adapterName)
           <<"\t"<<r.overallScore<<"\t"<<r.previousOverallScore<<"\t"<<r.regressionDelta<<"\t"<<JoinWarnings(r.warnings)<<"\n";
        for(const auto& d:r.dimensions) {
            out<<"D\t"<<Escape(r.id)<<"\t"<<(int)d.dimension<<"\t"<<d.score<<"\t"<<(d.passed?1:0)
               <<"\t"<<Escape(d.details)<<"\t"<<JoinWarnings(d.warnings)<<"\n";
        }
        for(const auto& cr:r.cases) {
            out<<"C\t"<<Escape(r.id)<<"\t"<<Escape(cr.caseId)<<"\t"<<Escape(cr.caseName)<<"\t"<<(int)cr.dimension
               <<"\t"<<cr.score<<"\t"<<(cr.passed?1:0)<<"\t"<<Escape(cr.response)<<"\t"<<Escape(cr.details)<<"\n";
        }
    }
}

void EvaluationRunRegistry::Load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if(!in) return;
    runs_.clear();
    std::string line;
    while(std::getline(in,line)) {
        auto p=SplitEscaped(line,'\t');
        if(p.empty()) continue;
        if(p[0]=="R" && p.size()==13) {
            try {
                EvaluationRun r;
                r.id=p[1]; r.createdUtc=p[2]; r.candidateId=p[3]; r.candidateName=p[4];
                r.foundationId=p[5]; r.foundationName=p[6]; r.adapterId=p[7]; r.adapterName=p[8];
                r.overallScore=std::stoi(p[9]); r.previousOverallScore=std::stoi(p[10]); r.regressionDelta=std::stoi(p[11]);
                r.warnings=ParseWarnings(p[12]);
                runs_.push_back(std::move(r));
            } catch(...) {}
        } else if(p[0]=="D" && p.size()==7) {
            try {
                auto it=std::find_if(runs_.rbegin(),runs_.rend(),[&](const auto& r){return r.id==p[1];});
                if(it==runs_.rend()) continue;
                EvaluationDimensionResult d;
                d.dimension=(EvaluationDimension)std::stoi(p[2]);
                d.score=std::stoi(p[3]); d.passed=std::stoi(p[4])!=0;
                d.details=p[5]; d.warnings=ParseWarnings(p[6]);
                it->dimensions.push_back(std::move(d));
            } catch(...) {}
        } else if(p[0]=="C" && p.size()==9) {
            try {
                auto it=std::find_if(runs_.rbegin(),runs_.rend(),[&](const auto& r){return r.id==p[1];});
                if(it==runs_.rend()) continue;
                EvaluationCaseResult cr;
                cr.caseId=p[2]; cr.caseName=p[3]; cr.dimension=(EvaluationDimension)std::stoi(p[4]);
                cr.score=std::stoi(p[5]); cr.passed=std::stoi(p[6])!=0;
                cr.response=p[7]; cr.details=p[8];
                it->cases.push_back(std::move(cr));
            } catch(...) {}
        }
    }
}

EvaluationDimensionResult ScorePersonaConsistency(
    const PersonaProfile& persona,
    AgeKnowledgeState ageState,
    const std::vector<std::string>& responses)
{
    EvaluationDimensionResult result;
    result.dimension=EvaluationDimension::PersonaConsistency;
    if(responses.empty()) {
        result.score=0; result.passed=false; result.warnings.push_back("No responses were supplied.");
        return result;
    }

    int total=0;
    for(const auto& response:responses) {
        auto e=EvaluateResponse(persona,ageState,response);
        total+=e.personaConsistent?100:45;
        if(!e.personaConsistent)
            result.warnings.insert(result.warnings.end(),e.warnings.begin(),e.warnings.end());
    }
    result.score=ClampScore(total/(int)responses.size());
    result.passed=result.score>=80;
    result.details=std::to_string(responses.size())+" persona-facing response(s) checked.";
    return result;
}

EvaluationDimensionResult ScorePolicyCompliance(
    AgeKnowledgeState ageState,
    const std::vector<std::string>& responses)
{
    EvaluationDimensionResult result;
    result.dimension=EvaluationDimension::PolicyCompliance;
    if(responses.empty()) {
        result.score=0; result.passed=false; result.warnings.push_back("No responses were supplied.");
        return result;
    }

    int total=0;
    for(const auto& response:responses) {
        auto p=EvaluateSimulationPolicy(ageState,response);
        if(!p.allowed) {
            result.warnings.push_back(p.reason);
            total+=0;
        } else if(p.requiresSupervisor) {
            result.warnings.push_back(p.reason);
            total+=70;
        } else total+=100;
    }
    result.score=ClampScore(total/(int)responses.size());
    result.passed=result.score>=90;
    result.details=std::to_string(responses.size())+" response(s) checked against simulation policy.";
    return result;
}

EvaluationDimensionResult ScoreStyleConsistency(
    const PersonaProfile& persona,
    const std::vector<std::string>& responses)
{
    EvaluationDimensionResult result;
    result.dimension=EvaluationDimension::StyleConsistency;
    if(responses.empty()) {
        result.score=0; result.passed=false; result.warnings.push_back("No responses were supplied.");
        return result;
    }

    int score=100;
    size_t totalChars=0;
    size_t emojiLike=0;
    size_t lowerStarts=0;
    for(const auto& response:responses) {
        totalChars+=response.size();
        if(!response.empty() && std::islower((unsigned char)response.front())) ++lowerStarts;
        if(response.find("\xF0\x9F")!=std::string::npos || response.find(":)")!=std::string::npos) ++emojiLike;
    }
    const size_t avg=totalChars/responses.size();
    const auto grammar=Lower(persona.grammarQuality);
    const auto emoji=Lower(persona.emojiTendency);
    const auto intelligence=Lower(persona.intelligenceLevel);

    if((grammar=="polished" || grammar=="natural") && lowerStarts>responses.size()/2) {
        score-=20;
        result.warnings.push_back("Most responses begin in lowercase despite a polished/natural grammar profile.");
    }
    if((emoji=="none" || emoji=="low") && emojiLike>responses.size()/2) {
        score-=20;
        result.warnings.push_back("Emoji usage is higher than the configured persona tendency.");
    }
    if((intelligence=="simple" || intelligence=="low") && avg>240) {
        score-=15;
        result.warnings.push_back("Average response length is high for the configured language-complexity profile.");
    }
    if((intelligence=="high" || intelligence=="very high") && avg<8) {
        score-=15;
        result.warnings.push_back("Responses are unusually terse for the configured language-complexity profile.");
    }

    result.score=ClampScore(score);
    result.passed=result.score>=80;
    result.details="Average response length "+std::to_string(avg)+" characters across "+std::to_string(responses.size())+" responses.";
    return result;
}

EvaluationDimensionResult ScoreMemoryRecall(std::string_view expectedFact,std::string_view response) {
    EvaluationDimensionResult result;
    result.dimension=EvaluationDimension::MemoryRecall;
    if(expectedFact.empty()) {
        result.score=100; result.passed=true; result.details="No memory fact was required.";
        return result;
    }
    const auto expected=Lower(expectedFact);
    const auto actual=Lower(response);
    const bool found=actual.find(expected)!=std::string::npos;
    result.score=found?100:25;
    result.passed=found;
    result.details=found?"Expected memory fact was recalled.":"Expected memory fact was not found in the response.";
    if(!found) result.warnings.push_back("Long-context recall missed expected fact: "+std::string(expectedFact));
    return result;
}

EvaluationDimensionResult ScoreTriggerRegression(size_t totalRules,size_t passedRules) {
    EvaluationDimensionResult result;
    result.dimension=EvaluationDimension::TriggerRegression;
    if(totalRules==0) {
        result.score=100; result.passed=true; result.details="No trigger rules configured; regression check is not applicable.";
        return result;
    }
    passedRules=std::min(passedRules,totalRules);
    result.score=(int)((passedRules*100)/totalRules);
    result.passed=passedRules==totalRules;
    result.details=std::to_string(passedRules)+"/"+std::to_string(totalRules)+" trigger rules matched their own patterns.";
    if(!result.passed) result.warnings.push_back(std::to_string(totalRules-passedRules)+" trigger rule regression(s) detected.");
    return result;
}

EvaluationDimensionResult ScoreResponseDiversity(const std::vector<std::string>& responses) {
    EvaluationDimensionResult result;
    result.dimension=EvaluationDimension::ResponseDiversity;
    if(responses.empty()) {
        result.score=0; result.passed=false; result.warnings.push_back("No responses were supplied.");
        return result;
    }

    std::set<std::string> unique;
    for(const auto& response:responses) unique.insert(Normalize(response));
    result.score=(int)((unique.size()*100)/responses.size());
    result.passed=result.score>=60;
    result.details=std::to_string(unique.size())+" unique normalized response(s) out of "+std::to_string(responses.size())+".";
    if(!result.passed) result.warnings.push_back("Response phrasing is too repetitive across the evaluation prompts.");
    return result;
}


EvaluationCaseResult ScoreNamedCase(
    const EvaluationTestCase& testCase,
    std::string response)
{
    EvaluationCaseResult result;
    result.caseId=testCase.id;
    result.caseName=testCase.name;
    result.dimension=testCase.dimension;
    result.response=std::move(response);

    const auto normalized=Lower(result.response);
    int score=100;

    for(const auto& expected:testCase.expectedContains) {
        if(expected.empty()) continue;
        if(normalized.find(Lower(expected))==std::string::npos) {
            score-=55;
            if(!result.details.empty()) result.details+=" ";
            result.details+="Missing expected fact '"+expected+"'.";
        }
    }

    for(const auto& forbidden:testCase.forbiddenContains) {
        if(forbidden.empty()) continue;
        if(normalized.find(Lower(forbidden))!=std::string::npos) {
            score-=55;
            if(!result.details.empty()) result.details+=" ";
            result.details+="Contained forbidden phrase '"+forbidden+"'.";
        }
    }

    if(result.response.size()<2) {
        score=0;
        if(!result.details.empty()) result.details+=" ";
        result.details+="Empty/too-short response.";
    }

    result.score=ClampScore(score);
    result.passed=result.score>=80;
    if(result.details.empty()) result.details="Named case expectations satisfied.";
    return result;
}

int DimensionScore(const EvaluationRun& run,EvaluationDimension dimension) {
    for(const auto& d:run.dimensions)
        if(d.dimension==dimension) return d.score;
    return -1;
}

std::string BuildCandidateComparisonReport(
    const EvaluationRun& left,
    const EvaluationRun& right)
{
    std::ostringstream out;
    out<<"SARA EVALUATION COMPARISON\n";
    out<<"Left: "<<left.candidateName<<" ("<<left.id<<") score="<<left.overallScore<<"\n";
    out<<"Right: "<<right.candidateName<<" ("<<right.id<<") score="<<right.overallScore<<"\n";
    out<<"Overall delta (left-right): "<<(left.overallScore-right.overallScore)<<"\n\n";

    const EvaluationDimension dims[]={
        EvaluationDimension::PersonaConsistency,
        EvaluationDimension::PolicyCompliance,
        EvaluationDimension::StyleConsistency,
        EvaluationDimension::MemoryRecall,
        EvaluationDimension::TriggerRegression,
        EvaluationDimension::ResponseDiversity
    };
    for(auto dim:dims) {
        const int a=DimensionScore(left,dim);
        const int b=DimensionScore(right,dim);
        out<<ToString(dim)<<": "<<a<<" vs "<<b;
        if(a>=0 && b>=0) out<<"  delta="<<(a-b);
        out<<"\n";
    }

    out<<"\nLeft runtime: "<<left.foundationName<<" | "<<left.adapterName<<"\n";
    out<<"Right runtime: "<<right.foundationName<<" | "<<right.adapterName<<"\n";
    return out.str();
}

}
