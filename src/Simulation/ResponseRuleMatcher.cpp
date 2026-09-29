#include "Sentinel/Simulation/ResponseRuleMatcher.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace sentinel::simulation {
namespace {

std::vector<std::string> RuleWords(std::string_view input)
{
    auto normalized=NormalizeResponseRuleText(input);
    std::vector<std::string> words;
    std::istringstream stream(normalized);
    std::string word;
    while(stream>>word) {
        if(word=="whats") { words.push_back("what"); words.push_back("is"); continue; }
        if(word=="im") { words.push_back("i"); words.push_back("am"); continue; }
        if(word=="youre") { words.push_back("you"); words.push_back("are"); continue; }
        if(word=="dont") { words.push_back("do"); words.push_back("not"); continue; }
        if(word=="cant") { words.push_back("can"); words.push_back("not"); continue; }
        if(word=="wont") { words.push_back("will"); words.push_back("not"); continue; }
        words.push_back(std::move(word));
    }
    return words;
}

int EditDistance(std::string_view a,std::string_view b)
{
    std::vector<int> previous(b.size()+1),current(b.size()+1);
    for(std::size_t j=0;j<=b.size();++j) previous[j]=(int)j;

    for(std::size_t i=1;i<=a.size();++i) {
        current[0]=(int)i;
        for(std::size_t j=1;j<=b.size();++j) {
            const int cost=a[i-1]==b[j-1]?0:1;
            current[j]=std::min({
                previous[j]+1,
                current[j-1]+1,
                previous[j-1]+cost
            });
        }
        previous.swap(current);
    }
    return previous[b.size()];
}

bool WordClose(std::string_view a,std::string_view b)
{
    if(a==b) return true;
    if(a.size()<4 || b.size()<4) return false;
    return EditDistance(a,b)<=1;
}

int SmartScore(std::string_view input,std::string_view trigger)
{
    const auto inputWords=RuleWords(input);
    const auto triggerWords=RuleWords(trigger);
    if(triggerWords.empty() || inputWords.empty()) return 0;

    std::size_t matched=0;
    std::vector<bool> used(inputWords.size(),false);
    for(const auto& triggerWord:triggerWords) {
        for(std::size_t i=0;i<inputWords.size();++i) {
            if(used[i]) continue;
            if(WordClose(triggerWord,inputWords[i])) {
                used[i]=true;
                ++matched;
                break;
            }
        }
    }

    const double recall=(double)matched/(double)triggerWords.size();
    const double precision=(double)matched/(double)inputWords.size();
    const int score=(int)std::lround((recall*0.75+precision*0.25)*100.0);

    if(triggerWords.size()<=2)
        return recall>=1.0?score:0;
    return recall>=0.75?score:0;
}

}

std::string NormalizeResponseRuleText(std::string_view input)
{
    std::string output;
    bool pendingSpace=false;

    for(unsigned char ch:input) {
        if(std::isalnum(ch)) {
            if(pendingSpace && !output.empty()) output.push_back(' ');
            output.push_back((char)std::tolower(ch));
            pendingSpace=false;
        } else if(std::isspace(ch)) {
            pendingSpace=true;
        } else {
            // Ignore punctuation so "what's", "whats", and "what's?" normalize
            // consistently for investigator-authored response rules.
        }
    }
    return output;
}

ResponseRuleMatchQuality EvaluateResponseRuleMatch(
    std::string_view matchType,
    std::string_view input,
    std::string_view trigger)
{
    ResponseRuleMatchQuality quality;
    const auto normalizedInput=NormalizeResponseRuleText(input);
    const auto normalizedTrigger=NormalizeResponseRuleText(trigger);
    quality.specificity=normalizedTrigger.size();

    if(normalizedTrigger.empty() || normalizedInput.empty())
        return quality;

    if(matchType=="exact") {
        quality.typeRank=3;
        if(normalizedInput==normalizedTrigger) quality.score=100;
        return quality;
    }

    if(matchType=="contains") {
        quality.typeRank=2;
        if(normalizedInput.find(normalizedTrigger)!=std::string::npos)
            quality.score=95;
        return quality;
    }

    if(matchType=="smart") {
        quality.typeRank=1;
        quality.score=SmartScore(input,trigger);
        return quality;
    }

    return quality;
}

bool PreferResponseRuleMatch(
    int candidatePriority,
    long long candidateId,
    const ResponseRuleMatchQuality& candidate,
    int currentPriority,
    long long currentId,
    const ResponseRuleMatchQuality& current) noexcept
{
    if(!candidate.Matched()) return false;
    if(!current.Matched()) return true;
    if(candidatePriority!=currentPriority)
        return candidatePriority>currentPriority;
    if(candidate.typeRank!=current.typeRank)
        return candidate.typeRank>current.typeRank;
    if(candidate.score!=current.score)
        return candidate.score>current.score;
    if(candidate.specificity!=current.specificity)
        return candidate.specificity>current.specificity;

    // Newer investigator-authored rules intentionally win only a true tie.
    return candidateId>currentId;
}

std::vector<std::string> SplitResponseRuleVariants(std::string_view responseText)
{
    std::vector<std::string> variants;
    std::string current;

    auto trim=[](std::string value) {
        const auto first=value.find_first_not_of(" \t\r\n");
        if(first==std::string::npos) return std::string{};
        const auto last=value.find_last_not_of(" \t\r\n");
        return value.substr(first,last-first+1);
    };

    for(std::size_t i=0;i<responseText.size();) {
        if(i+1<responseText.size() &&
           responseText[i]=='|' && responseText[i+1]=='|')
        {
            auto value=trim(std::move(current));
            if(!value.empty()) variants.push_back(std::move(value));
            current.clear();
            i+=2;
            continue;
        }
        current.push_back(responseText[i]);
        ++i;
    }

    auto value=trim(std::move(current));
    if(!value.empty()) variants.push_back(std::move(value));
    return variants;
}

std::string SelectResponseRuleVariant(
    std::string_view responseText,
    std::string_view deterministicBasis)
{
    auto variants=SplitResponseRuleVariants(responseText);
    if(variants.empty()) return {};
    if(variants.size()==1) return variants.front();

    // FNV-1a gives stable cross-process selection unlike std::hash, whose
    // implementation is not part of the persistence contract.
    std::uint64_t hash=1469598103934665603ULL;
    for(unsigned char ch:deterministicBasis) {
        hash^=ch;
        hash*=1099511628211ULL;
    }
    return variants[(std::size_t)(hash%variants.size())];
}

bool ResponseRulePassesFilter(
    std::string_view trigger,
    std::string_view response,
    std::string_view matchType,
    bool enabled,
    std::string_view searchText,
    std::string_view typeFilter,
    int stateFilter)
{
    if(stateFilter==1 && !enabled) return false;
    if(stateFilter==0 && enabled) return false;

    if(!typeFilter.empty() && typeFilter!="all" && matchType!=typeFilter)
        return false;

    const auto normalizedSearch=NormalizeResponseRuleText(searchText);
    if(normalizedSearch.empty()) return true;

    const auto normalizedTrigger=NormalizeResponseRuleText(trigger);
    const auto normalizedResponse=NormalizeResponseRuleText(response);
    return normalizedTrigger.find(normalizedSearch)!=std::string::npos ||
           normalizedResponse.find(normalizedSearch)!=std::string::npos;
}

ResponseRulePageWindow ComputeResponseRulePageWindow(
    std::size_t totalItems,
    std::size_t requestedPage,
    std::size_t pageSize) noexcept
{
    ResponseRulePageWindow out;
    if(pageSize==0) pageSize=1;

    out.pageCount=totalItems==0?1:(totalItems+pageSize-1)/pageSize;
    out.pageIndex=std::min(requestedPage,out.pageCount-1);
    out.start=std::min(totalItems,out.pageIndex*pageSize);
    out.end=std::min(totalItems,out.start+pageSize);
    return out;
}

}
