#include "Sentinel/Simulation/IModelAdapter.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <array>

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

        if (HasAny(m,{"hello","hey"," hi","hi ","good morning","good evening"})) {
            return turn<=1 ? "Hey. Who's this?" : "Hey. What's up?";
        }

        if (HasAny(m,{"who are you","your name","what's your name","whats your name","name?"})) {
            if(known(name)) return "I'm "+name+". What should I call you?";
            return "You can call me Alex. What should I call you?";
        }

        if (HasAny(m,{"how old are you","your age","what age are you","age?"})) {
            if(known(age)) return "I'm "+age+".";
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
            if(known(location)) return "I'm in "+location+".";
            return "I haven't given an exact location.";
        }

        if (HasAny(m,{"what do you do","what's your job","whats your job","occupation","where do you work","your job","work?"})) {
            if(known(occupation)) return "I work as "+occupation+".";
            return "I haven't really said what I do for work.";
        }

        if (HasAny(m,{"school","college","education","where do you go to school"})) {
            if(known(education)) return "My education is "+education+".";
            return "I haven't said much about school.";
        }

        if (HasAny(m,{"single","dating","boyfriend","girlfriend","married","relationship"})) {
            if(known(relationship)) return "I'm "+relationship+".";
            return "I haven't really talked about my relationship status.";
        }

        if (HasAny(m,{"family","parents","brother","sister","siblings"})) {
            if(known(family)) return family+".";
            return "I haven't said much about my family yet.";
        }

        if (HasAny(m,{"what do you like","interests","hobbies","hobby","fun","free time"})) {
            if(known(interests)) return "I'm into "+interests+".";
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

        if (m.find('?')!=std::string::npos) {
            const auto topic=ShortTopic(investigatorMessage);
            if(!topic.empty())
                return "I'm not completely sure what you mean by ""+topic+"". Can you be a little more specific?";
            return "Can you be a little more specific about what you're asking?";
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
        if(!topic.empty())
            return "I get you. What happened with "+topic+"?";
        return "I get you. Tell me a little more about that.";
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
