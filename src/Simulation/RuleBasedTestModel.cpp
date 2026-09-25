#include "Sentinel/Simulation/IModelAdapter.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

        if (HasAny(m,{"hello","hey"," hi","hi ","good morning","good evening"})) {
            if (turn<=1) return "Hey. I don't think I have you saved. Who's this?";
            return "Hey. What's going on?";
        }

        if (HasAny(m,{"who are you","your name","what's your name","whats your name"}))
            return "I'm Alex. What should I call you?";

        if (HasAny(m,{"how are you","how's it going","hows it going","you okay"}))
            return turn<3
                ? "I'm alright. Just having a pretty quiet day. How about you?"
                : "Doing okay. A little bored, honestly. What are you up to?";

        if (HasAny(m,{"where are you","where you at","location","what city","live at","live in"}))
            return "I'm home right now. I don't usually give out my exact location to somebody I just started talking to.";

        if (HasAny(m,{"what are you doing","what you doing","what are you up to","what you up to"}))
            return "Mostly just relaxing and scrolling around. Nothing exciting. What about you?";

        if (HasAny(m,{"today","tonight","weekend","plans"}))
            return "I don't really have much planned yet. I might just stay in unless something interesting comes up.";

        if (HasAny(m,{"work","job","school"}))
            return "It's been a normal week. Kind of repetitive, which is probably why I'm happy to have somebody new to talk to.";

        if (HasAny(m,{"music","song","listen"}))
            return "I listen to a little bit of everything, but it depends on my mood. What have you been listening to lately?";

        if (HasAny(m,{"movie","movies","show","netflix","watching"}))
            return "I've been bouncing between random shows more than actually finishing anything. Do you have one that's worth watching?";

        if (HasAny(m,{"game","gaming","video game"}))
            return "Sometimes. I'm more casual about it than serious. What do you play?";

        if (HasAny(m,{"hobby","hobbies","fun","free time"}))
            return "Music, movies, going out when I feel like it, and way too much time on my phone. Pretty normal stuff.";

        if (HasAny(m,{"why","how come"}))
            return "Mostly because I like to get a feel for someone before I say too much. You never really know who you're talking to online.";

        if (HasAny(m,{"sorry","apolog"}))
            return "You're fine. I didn't take it badly.";

        if (HasAny(m,{"thank","thanks"}))
            return "No problem.";

        if (HasAny(m,{"yes","yeah","yep","sure","okay","ok"})) {
            if (previous.find("what should i call you")!=std::string::npos)
                return "So what should I call you?";
            return turn%2==0
                ? "Fair enough. So what made you message me?"
                : "Gotcha. What else should I know about you?";
        }

        if (m.find('?')!=std::string::npos) {
            if (turn%4==0)
                return "That's a good question. I guess it depends on the situation. What made you ask?";
            if (turn%4==1)
                return "I'm not totally sure yet. I'd probably need a little more context before I answered that.";
            if (turn%4==2)
                return "Maybe. I could see it either way. What's your take on it?";
            return "I haven't really thought about it like that before. Why are you curious?";
        }

        if (m.size()<12)
            return turn%2==0
                ? "Yeah? Go on."
                : "Okay, I'm listening.";

        if (HasAny(m,{"because","i think","i feel","i like","i don't","i dont"}))
            return turn%3==0
                ? "That makes sense. I can see why you'd look at it that way."
                : turn%3==1
                    ? "I get what you mean. Has that always been how you felt about it?"
                    : "Interesting. I probably would've looked at that a little differently, but I get your point.";

        static const char* fallback[]={
            "That's actually kind of interesting. What happened after that?",
            "I get what you're saying. What made you bring that up?",
            "That sounds like there's more to the story.",
            "I can see that. How did you end up getting into that?",
            "Okay, now I'm curious. What do you mean by that?",
            "I hadn't expected you to say that. Tell me a little more."
        };
        return fallback[turn % (sizeof(fallback)/sizeof(fallback[0]))];
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
