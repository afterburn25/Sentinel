#include "Sentinel/Simulation/IModelAdapter.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <sstream>
#include <set>

namespace sentinel::simulation {
namespace {

std::string Lower(std::string_view input) {
    std::string out(input);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool HasAny(const std::string& s, std::initializer_list<std::string_view> words) {
    for (auto w : words) if (s.find(w) != std::string::npos) return true;
    return false;
}

size_t InvestigatorTurnCount(const ModelContext& context) {
    size_t count=0;
    for (const auto& t : context.history)
        if (t.speaker==ChatTurn::Speaker::Investigator) ++count;
    return count;
}

std::string LastSynthetic(const ModelContext& context) {
    for (auto it=context.history.rbegin(); it!=context.history.rend(); ++it)
        if (it->speaker==ChatTurn::Speaker::SyntheticSubject) return it->text;
    return {};
}


std::string LastInvestigator(const ModelContext& context) {
    for (auto it=context.history.rbegin(); it!=context.history.rend(); ++it)
        if (it->speaker==ChatTurn::Speaker::Investigator) return it->text;
    return {};
}

std::string Trim(std::string s) {
    auto notSpace=[](unsigned char ch){ return !std::isspace(ch); };
    s.erase(s.begin(),std::find_if(s.begin(),s.end(),notSpace));
    s.erase(std::find_if(s.rbegin(),s.rend(),notSpace).base(),s.end());
    while(!s.empty() && (s.back()=='.' || s.back()==',' || s.back()==';')) s.pop_back();
    return s;
}

std::string PersonaField(const std::string& summary,std::string_view label) {
    const std::string key=std::string(label);
    auto pos=summary.find(key);
    if(pos==std::string::npos) return {};
    pos+=key.size();
    static const std::array<std::string_view,15> nextLabels={
        ", age ",", gender ",", pronouns ",", location ",", occupation ",", education ",
        ", relationship status ",", family context ",", personality ",", social style ",
        ", confidence ",", background ",", interests ",", writing style ","."
    };
    size_t end=summary.size();
    for(auto next:nextLabels) {
        auto p=summary.find(std::string(next),pos);
        if(p!=std::string::npos) end=std::min(end,p);
    }
    return Trim(summary.substr(pos,end-pos));
}

std::string ShortTopic(std::string_view message) {
    auto m=Lower(message);
    static const std::array<std::string_view,18> stop={
        "what","why","how","when","where","who","do","does","did","are","is","you","your",
        "a","an","the","about","really"
    };
    std::string word,best;
    for(char ch:m) {
        if(std::isalnum((unsigned char)ch) || ch=='\'') word+=ch;
        else if(!word.empty()) {
            bool skip=word.size()<3;
            for(auto s:stop) if(word==s) skip=true;
            if(!skip && word.size()>best.size()) best=word;
            word.clear();
        }
    }
    if(!word.empty() && word.size()>best.size()) best=word;
    return best;
}


std::vector<std::string> MeaningfulWords(std::string_view input) {
    static const std::set<std::string> stop={
        "what","why","how","when","where","who","do","does","did","are","is","was","were",
        "you","your","yours","me","my","mine","i","im","i'm","we","our","a","an","the","and",
        "or","but","about","really","just","that","this","there","then","than","have","has","had",
        "remember","remembered","before","previous","conversation","talked","said","tell","told"
    };
    std::vector<std::string> words;
    std::string word;
    auto flush=[&]{
        if(word.size()>=3 && !stop.contains(word)) words.push_back(word);
        word.clear();
    };
    for(unsigned char ch:input) {
        if(std::isalnum(ch) || ch=='\'') word+=(char)std::tolower(ch);
        else flush();
    }
    flush();
    return words;
}

std::string TopicPhrase(std::string_view input,size_t maxWords=3) {
    auto words=MeaningfulWords(input);
    if(words.empty()) return {};
    std::string out;
    size_t used=0;
    for(const auto& w:words) {
        if(std::find(words.begin(),words.begin()+used,w)!=words.begin()+used) continue;
        if(!out.empty()) out+=" ";
        out+=w;
        if(++used>=maxWords) break;
    }
    return out;
}

std::string MemoryTopicHint(const ModelContext& context,std::string_view query) {
    if(context.recalledMemory.empty()) return {};
    const auto queryWords=MeaningfulWords(query);
    std::istringstream in(context.recalledMemory);
    std::string line,bestLine;
    int bestScore=-1;
    while(std::getline(in,line)) {
        auto colon=line.find(':');
        if(colon==std::string::npos) continue;
        auto body=Trim(line.substr(colon+1));
        if(body.empty()) continue;
        auto lower=Lower(body);
        int score=0;
        for(const auto& word:queryWords)
            if(lower.find(word)!=std::string::npos) score+=3;
        if(score>=bestScore) {
            bestScore=score;
            bestLine=body;
        }
    }
    if(bestLine.empty()) return {};
    auto phrase=TopicPhrase(bestLine,3);
    if(phrase.empty()) phrase=ShortTopic(bestLine);
    return phrase;
}

class RuleBasedTestModel final : public IModelAdapter {
public:
    std::string Name() const override {
        return "Built-in contextual conversation model";
    }

    std::string GenerateSyntheticReply(
        std::string_view investigatorMessage,
        const ModelContext& context) override
    {
        const auto m=Lower(investigatorMessage);
        const auto turn=InvestigatorTurnCount(context);
        const auto previous=Lower(LastSynthetic(context));
        const auto priorUser=Lower(LastInvestigator(context));

        const auto name=PersonaField(context.personaSummary,"");
        const auto age=PersonaField(context.personaSummary,", age ");
        const auto gender=PersonaField(context.personaSummary,", gender ");
        const auto pronouns=PersonaField(context.personaSummary,", pronouns ");
        const auto location=PersonaField(context.personaSummary,", location ");
        const auto occupation=PersonaField(context.personaSummary,", occupation ");
        const auto education=PersonaField(context.personaSummary,", education ");
        const auto relationship=PersonaField(context.personaSummary,", relationship status ");
        const auto family=PersonaField(context.personaSummary,", family context ");
        const auto personality=PersonaField(context.personaSummary,", personality ");
        const auto social=PersonaField(context.personaSummary,", social style ");
        const auto confidence=PersonaField(context.personaSummary,", confidence ");
        const auto background=PersonaField(context.personaSummary,", background ");
        const auto interests=PersonaField(context.personaSummary,", interests ");
        const auto writing=PersonaField(context.personaSummary,", writing style ");

        auto known=[](const std::string& v) {
            auto l=Lower(v);
            return !v.empty() && l!="unspecified" && l!="unknown" && l!="synthetic test environment";
        };

        if (!context.recalledMemory.empty()) {
            const auto memoryTopic=MemoryTopicHint(context,investigatorMessage);
            const bool explicitRecall=HasAny(m,{"remember","last time","previous","before","we talked","you said","i said","mentioned"});
            if(explicitRecall) {
                if(!memoryTopic.empty()) {
                    switch(turn%3) {
                        case 0: return "Yeah, I remember that coming up before — the part about "+memoryTopic+". What happened with it after that?";
                        case 1: return "I remember the conversation. The "+memoryTopic+" part is what sticks out to me. Did anything change after that?";
                        default: return "Yeah, that rings a bell. We talked about "+memoryTopic+". Where did things end up with that?";
                    }
                }
                return "Yeah, I remember the conversation. I don't remember every word exactly, but I remember the general situation. What part did you mean?";
            }

            auto currentTopic=TopicPhrase(investigatorMessage,2);
            if(!memoryTopic.empty() && !currentTopic.empty()) {
                auto memLower=Lower(memoryTopic);
                bool overlaps=false;
                for(const auto& word:MeaningfulWords(currentTopic))
                    if(memLower.find(word)!=std::string::npos) overlaps=true;
                if(overlaps)
                    return "Yeah, I remember you bringing that up before. What happened with "+currentTopic+" since then?";
            }
        }

        if (HasAny(m,{"hello","hey"," hi","hi ","good morning","good evening"})) {
            return turn<=1 ? "Hey. Who's this?" : "Hey. What's up?";
        }

        if (HasAny(m,{"who are you","your name","what's your name","whats your name","name?"})) {
            if(known(name)) return "I'm "+name+". What should I call you?";
            return "You can call me Alex. What should I call you?";
        }

        if (HasAny(m,{"how old are you","your age","what age are you","age?"})) {
            if(known(age)) return "I'm "+age+". What about you?";
            return "I haven't said my age yet.";
        }

        if (HasAny(m,{"gender","are you a girl","are you a guy","male or female"})) {
            if(known(gender)) return "I'm "+gender+".";
            return "I haven't really specified that.";
        }

        if (HasAny(m,{"pronouns","what pronouns"})) {
            if(known(pronouns)) return "I use "+pronouns+".";
            return "I haven't specified pronouns.";
        }

        if (HasAny(m,{"where do you live","where are you from","where you live","what city","your location","where are you","where you at"})) {
            if(known(location)) return "I'm in "+location+". You from around there too?";
            return "I haven't given an exact location.";
        }

        if (HasAny(m,{"what do you do","what's your job","whats your job","occupation","where do you work","your job","work?"})) {
            if(known(occupation)) return "I work as "+occupation+". What do you do?";
            return "I haven't really said what I do for work.";
        }

        if (HasAny(m,{"school","college","education","where do you go to school"})) {
            if(known(education)) return education+". What about you?";
            return "I haven't said much about school.";
        }

        if (HasAny(m,{"single","dating","boyfriend","girlfriend","married","relationship"})) {
            if(known(relationship)) return "I'm "+relationship+". Why, you curious?";
            return "I haven't really talked about my relationship status.";
        }

        if (HasAny(m,{"family","parents","brother","sister","siblings"})) {
            if(known(family)) return family+".";
            return "I haven't said much about my family yet.";
        }

        if (HasAny(m,{"what do you like","interests","hobbies","hobby","fun","free time"})) {
            if(known(interests)) return "I'm into "+interests+". What are you into?";
            return "Mostly normal stuff. Music, movies, and whatever catches my attention.";
        }

        if (HasAny(m,{"what kind of person","personality","are you shy","are you outgoing"})) {
            if(known(personality) || known(social)) {
                std::string out="I'd say I'm ";
                if(known(personality)) out+=Lower(personality);
                if(known(personality) && known(social)) out+=" and ";
                if(known(social)) out+=Lower(social);
                out+=".";
                return out;
            }
        }

        if (HasAny(m,{"how are you","how's it going","hows it going","you okay","how you doing"}))
            return turn<3 ? "I'm good. Just having a pretty normal day. How are you?" : "I'm good. Just relaxing a little. What about you?";

        if (HasAny(m,{"what are you doing","what you doing","what are you up to","what you up to"}))
            return "Just relaxing and talking to you right now. What are you doing?";

        if (HasAny(m,{"today","tonight","weekend","plans"}))
            return "Nothing major planned right now. I'm mostly taking it easy.";

        if (HasAny(m,{"music","song","listen"})) {
            if(known(interests) && Lower(interests).find("music")!=std::string::npos)
                return "Yeah, I like music. My taste depends on my mood. What do you listen to?";
            return "I listen to music sometimes. What kind are you into?";
        }

        if (HasAny(m,{"movie","movies","show","netflix","watching"}))
            return "I watch a mix of stuff. I usually pick whatever looks interesting instead of sticking to one genre.";

        if (HasAny(m,{"game","gaming","video game"}))
            return "A little. I'm more casual with games than competitive.";

        if (HasAny(m,{"sorry","apolog"}))
            return "You're fine. I didn't take it badly.";

        if (HasAny(m,{"thank","thanks"}))
            return "No problem.";

        if (HasAny(m,{"really","seriously","for real"})) {
            if(!previous.empty()) return "Yeah. I meant what I said.";
            return "Yeah.";
        }

        if (HasAny(m,{"why","how come"})) {
            if(!previous.empty())
                return "Because that's honestly how I see it. What part are you asking about?";
            return "What part do you mean?";
        }

        if (HasAny(m,{"yes","yeah","yep","sure","okay"," ok","ok "})) {
            if (previous.find("what should i call you")!=std::string::npos)
                return "So what should I call you?";
            if (previous.find("what about you")!=std::string::npos)
                return "What about you?";
            return "Gotcha.";
        }

        if (HasAny(m,{"no","nope","not really"}))
            return "Okay, got it.";

        if (HasAny(m,{"lost my job","got fired","laid off","breakup","broke up","died","passed away","sick","hospital","hurt","awful","terrible"})) {
            const auto topic=TopicPhrase(investigatorMessage,2);
            return topic.empty()
                ? "Damn, that sounds rough. What happened?"
                : "Damn, that sounds rough. What happened with "+topic+"?";
        }

        if (HasAny(m,{"excited","awesome","amazing","great news","finally","got the job","won","passed","graduated"})) {
            const auto topic=TopicPhrase(investigatorMessage,2);
            return topic.empty()
                ? "Okay, that's actually exciting. What happened?"
                : "That's awesome. How did the "+topic+" thing happen?";
        }

        if (HasAny(m,{"mad","angry","pissed","annoyed","frustrated"})) {
            const auto topic=TopicPhrase(investigatorMessage,2);
            return topic.empty()
                ? "Yeah, I can tell that got under your skin. What happened?"
                : "Yeah, I get why you'd be annoyed about "+topic+". What happened?";
        }

        if (HasAny(m,{"i am ","i'm ","im ","i was ","my ","i have ","i've ","ive "}) && m.find('?')==std::string::npos) {
            const auto topic=TopicPhrase(investigatorMessage,3);
            if(!topic.empty()) {
                switch(turn%4) {
                    case 0: return "Oh wow. How did the "+topic+" situation happen?";
                    case 1: return "Okay, that makes more sense. How long has "+topic+" been going on?";
                    case 2: return "I get you. So what happened next with "+topic+"?";
                    default: return "That's interesting. What made you bring up "+topic+"?";
                }
            }
        }

        if (m.find('?')!=std::string::npos) {
            const auto topic=ShortTopic(investigatorMessage);
            if(!topic.empty()) {
                switch(turn%3) {
                    case 0: return "Maybe. What exactly do you mean about "+topic+"?";
                    case 1: return "I'm not sure yet. What made you ask about "+topic+"?";
                    default: return "It depends. What part of "+topic+" are you asking about?";
                }
            }
            return "What do you mean exactly?";
        }

        if (HasAny(m,{"because","i think","i feel","i like","i don't","i dont"})) {
            const auto topic=ShortTopic(investigatorMessage);
            if(!topic.empty())
                return "I get what you mean about "+topic+".";
            return "I get what you mean.";
        }

        if (m.size()<12)
            return "Yeah, I'm listening.";

        const auto topic=ShortTopic(investigatorMessage);
        if(!topic.empty()) {
            switch(turn%4) {
                case 0: return "Okay, now I'm curious. What happened with "+topic+"?";
                case 1: return "I get what you're saying. How did "+topic+" even come up?";
                case 2: return "Wait, seriously? Tell me more about "+topic+".";
                default: return "I didn't expect that. What happened next with "+topic+"?";
            }
        }
        return "Okay, now I'm curious. What happened next?";
    }

    std::string GenerateInvestigatorSuggestion(
        const ModelContext& context) override
    {
        if (context.history.empty())
            return "Suggested reply: Start with a neutral greeting and let the synthetic subject establish context.";

        const auto& last=context.history.back();
        if (last.speaker==ChatTurn::Speaker::SyntheticSubject) {
            const auto l=Lower(last.text);
            if (l.find("who's this")!=std::string::npos || l.find("who is this")!=std::string::npos)
                return "Suggested reply: Introduce the configured test persona without adding facts that are not in the profile.";
            if (l.find("what should i call you")!=std::string::npos)
                return "Suggested reply: Provide the configured persona name, then ask a neutral reciprocal question.";
            if (l.find("what about you")!=std::string::npos)
                return "Suggested reply: Answer using an existing persona fact, then continue with one open-ended question.";
            if (l.find("why")!=std::string::npos || l.find("what made you")!=std::string::npos)
                return "Suggested reply: Explain using only established scenario facts and avoid introducing unsupported details.";
        }

        return "Suggested reply: Respond naturally to the last message, preserve persona consistency, and ask one relevant follow-up.";
    }
};

}

std::unique_ptr<IModelAdapter> CreateRuleBasedTestModel() {
    return std::make_unique<RuleBasedTestModel>();
}

}
