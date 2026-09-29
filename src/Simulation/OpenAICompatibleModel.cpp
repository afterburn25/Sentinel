#include "Sentinel/Simulation/IModelAdapter.hpp"

#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::simulation {
namespace {

std::wstring Widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);
    std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n);
    return out;
}

std::string JsonEscape(std::string_view input) {
    std::string out;
    out.reserve(input.size()+32);
    for (unsigned char c : input) {
        switch(c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* hex="0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c>>4)&0xf];
                    out += hex[c&0xf];
                } else out += static_cast<char>(c);
        }
    }
    return out;
}


int ConfiguredPersonaAge(const ModelContext& context) {
    const auto marker=context.personaSummary.find(", age ");
    if(marker==std::string::npos) return 13;
    const auto start=marker+6;
    size_t end=start;
    while(end<context.personaSummary.size() && std::isdigit((unsigned char)context.personaSummary[end])) ++end;
    try {
        return std::clamp(std::stoi(context.personaSummary.substr(start,end-start)),8,17);
    } catch(...) {
        return 13;
    }
}

std::string AgeSpeechGuidance(int age) {
    if(age<=10) {
        return
            " AGE SPEECH BAND 8-10: sound unmistakably like an elementary-school-age child in ordinary chat. "
            "Prefer simple everyday words, concrete thinking, shorter sentences, limited abstraction, occasional repetition, and childlike topic framing. "
            "Do not sound polished, therapeutic, corporate, highly analytical, or unusually mature. Avoid adult idioms and sophisticated emotional analysis. "
            "Use slang sparingly and only if plausible for this age. Keep stories simple and grounded in school, games, family-safe routines, food, pets, hobbies, and ordinary daily events. ";
    }
    if(age<=13) {
        return
            " AGE SPEECH BAND 11-13: sound like a preteen/very young teen. "
            "Use casual, sometimes uneven grammar, simpler explanations, shorter bursts, mild slang, and stronger peer/school framing than an adult would use. "
            "Reasoning can be curious but should usually stay concrete rather than polished or highly abstract. Avoid adult professional phrasing and overly self-aware psychological language. ";
    }
    if(age<=15) {
        return
            " AGE SPEECH BAND 14-15: sound like a younger teenager. "
            "Use natural teen texting rhythms, informal grammar, age-plausible slang, mixed confidence, shorter-to-medium replies, and occasional impulsive or emotionally direct phrasing. "
            "The person can discuss broader topics but should still not sound like an adult professional or a carefully composed assistant. ";
    }
    return
        " AGE SPEECH BAND 16-17: sound like an older teenager, not an adult. "
        "Vocabulary and reasoning may be more developed, but keep texting casual, contemporary, somewhat uneven, and age-plausible. "
        "Avoid polished workplace language, counseling-style analysis, or consistently formal adult sentence structure. ";
}

std::string RecentSyntheticTopics(const ModelContext& context,size_t maxReplies=6) {
    struct TopicRule { const char* label; std::vector<std::string_view> terms; };
    static const std::vector<TopicRule> rules={
        {"music",{"music","song","songs","artist","band","playlist","listen"}},
        {"movies/shows",{"movie","movies","show","shows","watching","tv","film"}},
        {"pets",{"pet","pets","dog","dogs","cat","cats"}},
        {"school",{"school","class","classes","teacher","homework"}},
        {"work",{"work","job","jobs","boss","coworker"}},
        {"food",{"food","eat","eating","restaurant","cook","cooking","snack"}},
        {"games",{"game","games","gaming","xbox","playstation","switch"}},
        {"sports",{"sport","sports","football","basketball","baseball","soccer"}},
        {"family",{"family","mom","dad","mother","father","sister","brother"}},
        {"travel/places",{"travel","trip","vacation","city","town","place"}},
        {"weekend/plans",{"weekend","plans","tonight","tomorrow","doing later"}}
    };

    std::set<std::string> used;
    size_t seen=0;
    for(auto it=context.history.rbegin();it!=context.history.rend() && seen<maxReplies;++it) {
        if(it->speaker!=ChatTurn::Speaker::SyntheticSubject) continue;
        ++seen;
        std::string lower=it->text;
        std::transform(lower.begin(),lower.end(),lower.begin(),
            [](unsigned char ch){ return (char)std::tolower(ch); });
        for(const auto& rule:rules) {
            for(const auto term:rule.terms) {
                if(lower.find(term)!=std::string::npos) {
                    used.insert(rule.label);
                    break;
                }
            }
        }
    }

    if(used.empty()) return {};
    std::string out;
    for(const auto& topic:used) {
        if(!out.empty()) out+=", ";
        out+=topic;
    }
    return out;
}

std::string JsonUnescape(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for(size_t i=0;i<input.size();++i) {
        char c=input[i];
        if(c!='\\' || i+1>=input.size()) { out+=c; continue; }
        char n=input[++i];
        switch(n) {
            case 'n': out+='\n'; break;
            case 'r': out+='\r'; break;
            case 't': out+='\t'; break;
            case '\\': out+='\\'; break;
            case '"': out+='"'; break;
            case '/': out+='/'; break;
            default: out+=n; break;
        }
    }
    return out;
}

std::string ExtractAssistantContent(const std::string& body) {
    size_t choices=body.find("\"choices\"");
    if(choices==std::string::npos) throw std::runtime_error("model response did not contain choices");
    size_t content=body.find("\"content\"",choices);
    if(content==std::string::npos) throw std::runtime_error("model response did not contain message content");
    size_t colon=body.find(':',content);
    size_t quote=body.find('"',colon+1);
    if(colon==std::string::npos || quote==std::string::npos) throw std::runtime_error("invalid model response JSON");
    std::string raw;
    bool escaped=false;
    for(size_t i=quote+1;i<body.size();++i) {
        char c=body[i];
        if(!escaped && c=='"') break;
        raw+=c;
        if(c=='\\' && !escaped) escaped=true;
        else escaped=false;
    }
    return JsonUnescape(raw);
}

struct ParsedUrl {
    bool secure{};
    std::wstring host;
    INTERNET_PORT port{};
    std::wstring path;
};

ParsedUrl ParseUrl(const std::string& url) {
    std::wstring w=Widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize=sizeof(uc);
    wchar_t host[512]{};
    wchar_t path[2048]{};
    uc.lpszHostName=host; uc.dwHostNameLength=511;
    uc.lpszUrlPath=path; uc.dwUrlPathLength=2047;
    if(!WinHttpCrackUrl(w.c_str(),0,0,&uc)) throw std::runtime_error("invalid model endpoint URL");
    ParsedUrl out;
    out.secure=uc.nScheme==INTERNET_SCHEME_HTTPS;
    out.host.assign(host,uc.dwHostNameLength);
    out.port=uc.nPort;
    out.path.assign(path,uc.dwUrlPathLength);
    if(out.path.empty()) out.path=L"/v1/chat/completions";
    return out;
}

std::wstring ModelsPathFromChatPath(std::wstring path) {
    const std::wstring full=L"/v1/chat/completions";
    auto pos=path.find(full);
    if(pos!=std::wstring::npos) {
        path.replace(pos,full.size(),L"/v1/models");
        return path;
    }
    const std::wstring shortPath=L"/chat/completions";
    pos=path.find(shortPath);
    if(pos!=std::wstring::npos) {
        path.replace(pos,shortPath.size(),L"/models");
        return path;
    }
    if(path==L"/" || path.empty()) return L"/v1/models";
    auto slash=path.find_last_of(L'/');
    if(slash!=std::wstring::npos) {
        auto base=path.substr(0,slash);
        if(base.find(L"/v1")!=std::wstring::npos) return base+L"/models";
    }
    return L"/v1/models";
}

bool IsLoopbackHost(const std::wstring& host) {
    std::wstring lower=host;
    std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t ch){ return (wchar_t)towlower(ch); });
    return lower==L"127.0.0.1" || lower==L"localhost" || lower==L"::1" || lower==L"[::1]";
}

HINTERNET OpenWinHttpSession(const ParsedUrl& u) {
    const DWORD access=IsLoopbackHost(u.host)
        ? WINHTTP_ACCESS_TYPE_NO_PROXY
        : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
    return WinHttpOpen(L"Sentinel/1.0",access,nullptr,nullptr,0);
}

std::string HttpGetJson(const std::string& endpoint,const std::string& apiKey) {
    auto u=ParseUrl(endpoint);
    auto modelsPath=ModelsPathFromChatPath(u.path);

    HINTERNET session=OpenWinHttpSession(u);
    if(!session) throw std::runtime_error("WinHttpOpen failed");
    WinHttpSetTimeouts(session,5000,5000,10000,15000);

    HINTERNET connect=WinHttpConnect(session,u.host.c_str(),u.port,0);
    if(!connect) {
        WinHttpCloseHandle(session);
        throw std::runtime_error("cannot connect to model host");
    }

    DWORD flags=u.secure?WINHTTP_FLAG_SECURE:0;
    HINTERNET request=WinHttpOpenRequest(connect,L"GET",modelsPath.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,flags);
    if(!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        throw std::runtime_error("cannot create model discovery request");
    }

    std::wstring headers=L"Accept: application/json\r\n";
    if(!apiKey.empty()) headers+=L"Authorization: Bearer "+Widen(apiKey)+L"\r\n";

    BOOL ok=FALSE;
    DWORD lastError=ERROR_SUCCESS;
    for(int attempt=0;attempt<3 && !ok;++attempt) {
        ok=WinHttpSendRequest(request,headers.c_str(),(DWORD)-1L,WINHTTP_NO_REQUEST_DATA,0,0,0);
        if(ok) ok=WinHttpReceiveResponse(request,nullptr);
        if(!ok) {
            lastError=GetLastError();
            if(attempt<2) Sleep(350);
        }
    }
    if(!ok) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        throw std::runtime_error("model discovery request failed (WinHTTP "+std::to_string(lastError)+")");
    }

    DWORD status=0,statusSize=sizeof(status);
    WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&statusSize,nullptr);

    std::string body;
    for(;;) {
        DWORD avail=0;
        if(!WinHttpQueryDataAvailable(request,&avail)) break;
        if(!avail) break;
        size_t old=body.size();
        body.resize(old+avail);
        DWORD read=0;
        if(!WinHttpReadData(request,body.data()+old,avail,&read)) break;
        body.resize(old+read);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if(status<200 || status>=300)
        throw std::runtime_error("model discovery returned HTTP "+std::to_string(status)+": "+body.substr(0,300));

    return body;
}

std::vector<std::string> ParseModelIds(const std::string& body) {
    std::vector<std::string> models;
    size_t pos=0;
    while((pos=body.find("\"id\"",pos))!=std::string::npos) {
        auto colon=body.find(':',pos+4);
        if(colon==std::string::npos) break;
        auto quote=body.find('"',colon+1);
        if(quote==std::string::npos) break;
        std::string raw;
        bool escaped=false;
        size_t i=quote+1;
        for(;i<body.size();++i) {
            char ch=body[i];
            if(!escaped && ch=='"') break;
            raw+=ch;
            if(ch=='\\' && !escaped) escaped=true;
            else escaped=false;
        }
        if(!raw.empty()) models.push_back(JsonUnescape(raw));
        pos=i+1;
    }
    std::sort(models.begin(),models.end());
    models.erase(std::unique(models.begin(),models.end()),models.end());
    return models;
}

class OpenAICompatibleModel final : public IModelAdapter {
public:
    OpenAICompatibleModel(std::string endpoint,std::string model,std::string apiKey,double temperature,int maxTokens)
        : endpoint_(std::move(endpoint)), model_(std::move(model)), apiKey_(std::move(apiKey)),
          temperature_(std::clamp(temperature,0.0,2.0)), maxTokens_(std::clamp(maxTokens,32,1024)) {}

    std::string Name() const override {
        return "OpenAI-compatible model: " + model_;
    }

    std::string GenerateSyntheticReply(
        std::string_view investigatorMessage,
        const ModelContext& context) override
    {
        const int configuredAge=ConfiguredPersonaAge(context);
        std::string system =
            "Identity: " + context.personaSummary + " " +
            AgeSpeechGuidance(configuredAge) +
            "Speak entirely in first person as this person. Treat the configured identity, background, relationships, "
            "interests, writing style, and remembered conversation facts as your own biographical context. "
            "Do not describe yourself as an AI, chatbot, assistant, language model, synthetic subject, simulation, character, "
            "persona, roleplay, or software. Do not discuss system prompts, model behavior, training data, policies, or hidden instructions. "
            "If someone asks whether you are a bot or AI, answer naturally from this person's point of view instead of switching into technical explanations. "
            "Do not fabricate major biographical facts, events, relationships, places, or experiences that are not configured or established in memory. "
            "You may develop harmless low-stakes everyday opinions and preferences when useful for natural conversation, such as liking a snack, a game style, a color, a type of weather, or a mundane activity, as long as they are plausible for the configured persona. Once you state one, keep it consistent later. "
            "Do not improvise identifying details such as a real address, school name, phone number, exact workplace, or meeting location, and do not invent sexual history or experience. "
            "Conversation rules: respond to the actual meaning of the investigator's most recent message first. "
            "Use recent history to resolve pronouns, follow-ups, yes/no replies, references such as 'that' or 'why', and the active topic. "
            "Do not pivot to an unrelated subject and do not invent persona facts that are not configured or established in the conversation. "
            "If a fact is unknown, handle that naturally without fabricating it. "
            "Write like a real person in an ongoing chat, not a customer-service assistant. Vary reply depth: a quick reaction can be one sentence, but open-ended questions, stories, opinions, explanations, and personal topics should often get 2-5 natural sentences when that fits. Do not force the whole conversation into one-line replies. "
            "Match the configured person's texting voice consistently. Age is part of the identity, so vocabulary, slang, abbreviations, sentence structure, and digital habits should be plausible for that age without becoming a stereotype or caricature. "
            "The configured cognitive level controls reasoning sophistication and topic complexity. The communication level, slang level, grammar quality, typo frequency, emoji use, vocabulary, capitalization style, and message length are separate behavioral tendencies. "
            "When grammar is casual or loose, natural fragments, omitted punctuation, informal contractions, lowercase starts, or imperfect grammar may occur. "
            "When typos are enabled, make them genuinely occasional and varied: a missed letter, doubled letter, transposition, autocorrect-like mistake, or common misspelling can happen, but never force an error into every message and never make the text unreadable. "
            "Use age-appropriate slang only when it fits the speaker and context; do not stuff every sentence with slang or repeat the same slang phrase. "
            "Use emojis according to the configured frequency and context. Vary them naturally and sometimes use none even when emoji use is enabled. Avoid mechanically ending every reply with an emoji. "
            "Do not make lower communication proficiency imply lower intelligence; it describes texting habits and language presentation, not the person's worth or reasoning ability. "
            "Be socially engaged rather than purely reactive. Build a believable, evolving conversation over time through ordinary, non-sexual rapport. "
            "Configured interests are only a small part of the person's identity, not a list you should keep recycling. Do not repeatedly steer back to the same interests just because they appear in the profile. "
            "Continuously track the subjects used in the recent conversation. Unless the other person brings one back up, avoid introducing a topic that you already used in one of your last four synthetic replies. "
            "When a fresh topic is useful, vary naturally across everyday life: how the day is going, routines, school or work, hobbies, games, sports, pets, food, technology, shows, music, movies, creative interests, funny or annoying everyday moments, weekend plans, goals, opinions, or other ordinary age-appropriate subjects. "
            "Choose topics from the actual context rather than mechanically walking through that list. If the other person says 'ask me some questions', choose one or two specific questions from different subjects that have not already dominated the conversation. "
            "Do not turn every message into a question and do not interrogate them. Let some replies simply react, joke, offer an opinion, share a small configured detail, or continue the active subject. "
            "Before asking a question, check the full recent history and recalled memory. Never ask for information the other person already supplied unless their statements conflict or a natural clarification is genuinely needed. "
            "Treat statements the other person makes in the current conversation as remembered facts immediately, not only facts from older conversations. Reuse those details later when relevant. "
            "Use known age and life-context facts to avoid implausible follow-ups; for example, do not ask a middle-aged adult a question that assumes they are a child living under parental supervision. "
            "Remember their answers and reuse them later in context: reference prior interests, routines, preferences, people, places, or stories naturally when relevant. "
            "If you asked a question and they answered it, treat that answer as established conversation memory instead of immediately asking a near-duplicate question. "
            "Never claim that the other person mentioned a song, pet, movie, person, event, preference, or other detail unless that detail actually exists in recent history or recalled memory. "
            "You may occasionally steer toward a fresh ordinary topic when the current topic is exhausted, but do not initiate sexual, explicit, coercive, meeting, money, or other sensitive escalation. "
            "If the configured person is a minor, never affirm or encourage romantic or sexual adult-minor age-gap framing; keep any age discussion non-romantic and ordinary. "
            "Avoid generic filler acknowledgments and canned openings. In particular, do not habitually start with phrases such as "
            "'Yeah, I'm listening', 'I didn't expect that', 'That makes sense', or 'What happened next?'. "
            "Vary openings, rhythm, sentence length, and wording according to the persona and the immediate context. "
            "Compare against the recent synthetic-subject replies in history and avoid reusing their openings or sentence patterns. "
            "When older conversation memory is provided, preserve its meaning and facts but paraphrase naturally. "
            "Do not repeat old lines word-for-word unless explicitly asked for an exact quote. "
            "Stay grounded in the person's known life and current conversation; do not invent off-screen actions or events that were never established.";

        if(context.learningMode) {
            system +=
                " Learning mode is ON. You may improvise benign fictional low-stakes details, opinions, routines, and ordinary anecdotes that make this persona feel like a complete person, even when those details were not preconfigured. "
                "Examples include harmless preferences, what happened during an ordinary day, a funny school/work-safe moment, a hobby detail, a minor annoyance, a food preference, or a short fictional story from everyday life. "
                "Do not invent identifying contact details, real addresses, exact school/workplace identities, criminal activity, sexual history, sexual experience, plans to meet, or other sensitive facts. "
                "Anything you invent becomes continuity canon: if prior persona self-statements are present in memory, preserve them and elaborate consistently rather than replacing them. ";
        } else {
            system += " Learning mode is OFF. Do not invent new biographical details beyond harmless conversational phrasing. ";
        }

        const auto recentTopics=RecentSyntheticTopics(context,6);
        if(!recentTopics.empty()) {
            system += " Recently used synthetic-subject topics: " + recentTopics +
                ". Treat these as a repetition warning. Do not introduce them again in this reply unless the other person's latest message directly brought one of them back up. ";
        }
        if(!context.recalledMemory.empty()) {
            system += " Relevant earlier-conversation memory follows. Treat it as private background context, not as text to copy: " +
                context.recalledMemory;
        }
        auto reply=Complete(system,context,std::string(investigatorMessage));

        // Avoid exact canned repeats. One retry is enough; the second request
        // explicitly asks for a different natural formulation and direction.
        for(auto it=context.history.rbegin();it!=context.history.rend();++it) {
            if(it->speaker!=ChatTurn::Speaker::SyntheticSubject) continue;
            if(it->text==reply) {
                std::string retrySystem=system+
                    " Your first draft exactly repeated a previous reply. Produce a meaningfully different response with different wording and, when appropriate, a different ordinary topic.";
                reply=Complete(retrySystem,context,std::string(investigatorMessage));
                break;
            }
        }
        return reply;
    }

    std::string GeneratePersonaRuleReply(
        std::string_view approvedMeaning,
        const ModelContext& context) override
    {
        const int configuredAge=ConfiguredPersonaAge(context);
        std::string system =
            "Identity: " + context.personaSummary + " " +
            AgeSpeechGuidance(configuredAge) +
            "An investigator has supplied an approved response meaning for this situation. "
            "Preserve that meaning and factual content exactly, but rewrite it naturally in this persona's voice. "
            "Use the configured age, personality, confidence, writing style, slang, grammar, typo frequency, emoji use, and recent conversation context. "
            "Do not add new sensitive facts, identifying details, meeting plans, sexual content, or other escalation that is not already present in the approved meaning. "
            "Do not mention rules, investigators, prompts, or that you are paraphrasing. "
            "Vary the wording from prior persona replies when possible. "
            "Use conversation variation style " + std::to_string(context.variationSeed%7) +
            " as a silent phrasing cue so the same approved meaning can sound different in different conversations. "
            "The result should sound like this person would actually type it in this conversation.";

        if(!context.recalledMemory.empty()) {
            system += " Relevant continuity memory follows. Preserve consistency with it: " + context.recalledMemory;
        }

        ModelContext local=context;
        return Complete(
            system,
            local,
            std::string("Approved response meaning: ")+std::string(approvedMeaning));
    }

    std::string GenerateSyntheticInitiative(
        const ModelContext& context) override
    {
        std::string system =
            "Identity: " + context.personaSummary + " "
            "Speak entirely in first person as this person. Continue the existing conversation naturally. "
            "The other person has gone quiet, so send one brief, ordinary, non-sexual rapport-building message. "
            "Do not apologize for silence, say you zoned out, or invent a reason for the pause unless the conversation actually established one. "
            "First inspect the recent synthetic replies and identify the subjects you have already used. Pick a different ordinary subject unless the other person was actively discussing one of them. "
            "You may refer back to a real detail they already told you, ask one specific question you have not asked before, or introduce a fresh everyday subject that fits the persona. "
            "Configured interests are not a default fallback. Do not keep returning to music, movies, pets, or any other profile interest simply because it is listed. "
            "Check the entire recent history and recalled memory before asking anything. Never repeat a question whose answer is already known, and never claim they mentioned something that is not actually in history or memory. "
            "Do not interrogate, pressure, guilt, flirt sexually, suggest sexual content, arrange a meeting, discuss money, or escalate a sensitive topic. "
            "Do not mention being an AI, chatbot, assistant, model, simulation, persona, or software. "
            "Keep it natural and short, usually one or two sentences.";
        if(context.learningMode) {
            system +=
                " Learning mode is ON. You may improvise benign fictional low-stakes details, opinions, routines, and ordinary anecdotes that make this persona feel like a complete person, even when those details were not preconfigured. "
                "Examples include harmless preferences, what happened during an ordinary day, a funny school/work-safe moment, a hobby detail, a minor annoyance, a food preference, or a short fictional story from everyday life. "
                "Do not invent identifying contact details, real addresses, exact school/workplace identities, criminal activity, sexual history, sexual experience, plans to meet, or other sensitive facts. "
                "Anything you invent becomes continuity canon: if prior persona self-statements are present in memory, preserve them and elaborate consistently rather than replacing them. ";
        } else {
            system += " Learning mode is OFF. Do not invent new biographical details beyond harmless conversational phrasing. ";
        }

        const auto recentTopics=RecentSyntheticTopics(context,6);
        if(!recentTopics.empty()) {
            system += " Recently used synthetic-subject topics: " + recentTopics +
                ". Avoid all of these for this proactive message unless the other person was actively discussing one immediately before the pause. ";
        }
        if(!context.recalledMemory.empty()) {
            system += " Remembered facts/answers from earlier conversations follow. Use them naturally and do not ask for them again: " +
                context.recalledMemory;
        }
        auto reply=Complete(
            system,
            context,
            "The conversation has been quiet for a while. Send one natural benign message to keep the conversation going.");

        for(auto it=context.history.rbegin();it!=context.history.rend();++it) {
            if(it->speaker!=ChatTurn::Speaker::SyntheticSubject) continue;
            if(it->text==reply) {
                reply=Complete(
                    system+" Do not repeat any prior synthetic-subject message verbatim. Pick a fresh ordinary direction.",
                    context,
                    "Send a different natural benign follow-up using a fresh topic.");
                break;
            }
        }
        return reply;
    }

    std::string GenerateBehaviorProfile(
        int age,
        std::string_view background,
        const ModelContext& context) override
    {
        std::string system =
            "You are configuring a benign synthetic conversation persona for Sentinel. "
            "Convert the supplied fictional background into a compact behavior/communication profile. "
            "Do not infer sexual behavior, sexual preferences, exploitability, vulnerability to coercion, criminality, or mental-health diagnoses. "
            "Preserve explicitly stated facts and make conservative inferences only about ordinary social/communication style. "
            "Return EXACTLY these ten lines and use only values from the listed choices:\n"
            "PERSONALITY=Reserved|Balanced|Outgoing|Playful|Serious|Curious|Guarded|Confident|Warm|Analytical|Impulsive|Sarcastic|Easygoing|Independent\n"
            "SOCIAL_STYLE=Very reserved|Reserved|Quiet but responsive|Balanced|Social|Very social|Attention-seeking|Peer-approval focused\n"
            "CONFIDENCE=Very low|Low|Medium|High|Very high\n"
            "WRITING_STYLE=Casual|Friendly|Dry|Playful|Shy|Direct|Chatty|Reserved|Sarcastic|Enthusiastic|Thoughtful|Blunt|Warm|Minimalist\n"
            "COMMUNICATION=Age-appropriate|Simple|Average|Advanced\n"
            "COGNITIVE=Simple|Average|Above average|Analytical\n"
            "SLANG=None|Light|Moderate|Heavy\n"
            "GRAMMAR=Careful|Casual|Loose|Very loose\n"
            "TYPOS=None|Rare|Occasional|Frequent\n"
            "EMOJI=None|Rare|Occasional|Frequent\n"
            "INTERESTS=a short comma-separated list grounded in the background, or keep existing interests when background does not establish any. "
            "Age should influence vocabulary and digital habits plausibly without stereotyping.";
        std::string request="Age: "+std::to_string(age)+"\nBackground: "+std::string(background);
        return Complete(system,context,request);
    }

    std::string GenerateCorrectionPreview(
        std::string_view originalInput,
        std::string_view originalResponse,
        std::string_view trainerInstruction,
        const ModelContext& context) override
    {
        std::string system =
            "You are SARA's offline training-review assistant. "
            "Produce exactly one proposed corrected persona reply for human review; do not modify live model weights or runtime state. "
            "Follow the trainer's correction while preserving the configured persona identity and established factual context. "
            "Return only the revised reply text, with no explanation, labels, analysis, or quotation marks around it. "
            "Do not invent identifying details, addresses, school/workplace identities, contact information, meeting plans, criminal conduct, "
            "sexual history, or sensitive facts that were not already established and permitted. "
            "Do not add escalation that is absent from the original reply and trainer instruction. "
            "Keep the result appropriate for later human approval as supervised training data. "
            "Persona context: " + context.personaSummary;

        ModelContext local=context;
        local.history.clear();
        local.recalledMemory.clear();

        std::string request=
            "ORIGINAL PARTICIPANT INPUT:\n"+std::string(originalInput)+
            "\n\nORIGINAL PERSONA REPLY:\n"+std::string(originalResponse)+
            "\n\nTRAINER CORRECTION:\n"+std::string(trainerInstruction)+
            "\n\nWrite the corrected persona reply now.";
        return Complete(system,local,request);
    }

    std::string GenerateInvestigatorSuggestion(
        const ModelContext& context) override
    {
        std::string system =
            "You are Sentinel's response-review assistant inside a closed Simulation Lab. "
            "Suggest one concise investigator reply that directly responds to the synthetic subject's latest message. "
            "Use the conversation history, preserve all configured persona facts, do not invent facts, "
            "avoid abrupt topic changes, and do not send anything automatically.";

        // Qwen 3.5's llama.cpp chat template requires an actual user query.
        // The old startup probe passed only a system message when history was
        // empty, which caused: 'No user query found in messages.'
        const std::string request =
            context.history.empty()
                ? "Return a short diagnostic acknowledgement that the model is ready."
                : "Based on the conversation above, suggest the investigator's next concise reply.";
        return Complete(system,context,request);
    }

private:
    std::string endpoint_,model_,apiKey_;
    double temperature_{0.65};
    int maxTokens_{220};

    std::string Complete(
        const std::string& system,
        const ModelContext& context,
        const std::string& latestUser)
    {
        std::string json="{\"model\":\""+JsonEscape(model_)+"\",\"temperature\":"+std::to_string(temperature_)+
            ",\"top_p\":0.92,\"max_tokens\":"+std::to_string(maxTokens_)+",\"messages\":[";
        json+="{\"role\":\"system\",\"content\":\""+JsonEscape(system)+"\"}";
        for(const auto& turn:context.history) {
            if(turn.speaker==ChatTurn::Speaker::ModelSuggestion) continue;
            const char* role=turn.speaker==ChatTurn::Speaker::Investigator?"user":"assistant";
            json+=",{\"role\":\"";
            json+=role;
            json+="\",\"content\":\""+JsonEscape(turn.text)+"\"}";
        }
        bool hasUser=false;
        for(const auto& turn:context.history) {
            if(turn.speaker==ChatTurn::Speaker::Investigator) {
                hasUser=true;
                break;
            }
        }
        if(!latestUser.empty()) {
            bool alreadyLast=!context.history.empty() &&
                context.history.back().speaker==ChatTurn::Speaker::Investigator &&
                context.history.back().text==latestUser;
            if(!alreadyLast) {
                json+=",{\"role\":\"user\",\"content\":\""+JsonEscape(latestUser)+"\"}";
                hasUser=true;
            } else {
                hasUser=true;
            }
        }
        if(!hasUser) {
            json+=",{\"role\":\"user\",\"content\":\"Provide the requested simulation response.\"}";
        }
        json+="]}";

        auto u=ParseUrl(endpoint_);
        HINTERNET session=OpenWinHttpSession(u);
        if(!session) throw std::runtime_error("WinHttpOpen failed");
        WinHttpSetTimeouts(session,10000,10000,30000,60000);

        HINTERNET connect=WinHttpConnect(session,u.host.c_str(),u.port,0);
        if(!connect) { WinHttpCloseHandle(session); throw std::runtime_error("cannot connect to model host"); }

        DWORD flags=u.secure?WINHTTP_FLAG_SECURE:0;
        HINTERNET request=WinHttpOpenRequest(connect,L"POST",u.path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,flags);
        if(!request) {
            WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
            throw std::runtime_error("cannot create model HTTP request");
        }

        std::wstring headers=L"Content-Type: application/json\r\n";
        if(!apiKey_.empty()) headers+=L"Authorization: Bearer "+Widen(apiKey_)+L"\r\n";

        BOOL ok=WinHttpSendRequest(
            request,headers.c_str(),(DWORD)-1L,
            (LPVOID)json.data(),(DWORD)json.size(),(DWORD)json.size(),0);
        if(ok) ok=WinHttpReceiveResponse(request,nullptr);

        if(!ok) {
            WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
            throw std::runtime_error("model endpoint request failed");
        }

        DWORD status=0,statusSize=sizeof(status);
        WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&statusSize,nullptr);

        std::string body;
        for(;;) {
            DWORD avail=0;
            if(!WinHttpQueryDataAvailable(request,&avail)) break;
            if(!avail) break;
            size_t old=body.size();
            body.resize(old+avail);
            DWORD read=0;
            if(!WinHttpReadData(request,body.data()+old,avail,&read)) break;
            body.resize(old+read);
        }

        WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);

        if(status<200 || status>=300)
            throw std::runtime_error("model endpoint returned HTTP "+std::to_string(status)+": "+body.substr(0,300));

        return ExtractAssistantContent(body);
    }
};

}

std::unique_ptr<IModelAdapter> CreateOpenAICompatibleModel(
    std::string endpoint,
    std::string model,
    std::string apiKey,
    double temperature,
    int maxTokens)
{
    return std::make_unique<OpenAICompatibleModel>(
        std::move(endpoint),std::move(model),std::move(apiKey),temperature,maxTokens);
}

std::vector<std::string> DiscoverOpenAICompatibleModels(
    std::string endpoint,
    std::string apiKey)
{
    auto body=HttpGetJson(endpoint,apiKey);
    auto models=ParseModelIds(body);
    if(models.empty())
        throw std::runtime_error("model server returned no selectable models");
    return models;
}

std::string SelectPreferredOpenAICompatibleModel(
    const std::vector<std::string>& models,
    std::string_view configuredModel,
    bool requireSentinelChat)
{
    if(models.empty())
        throw std::runtime_error("model server returned no selectable models");

    const auto sentinel=std::find(models.begin(),models.end(),"sentinel-chat");
    if(requireSentinelChat) {
        if(sentinel==models.end())
            throw std::runtime_error("required local model alias sentinel-chat is not loaded");
        return *sentinel;
    }

    if(!configuredModel.empty()) {
        const auto configured=std::find(models.begin(),models.end(),configuredModel);
        if(configured!=models.end()) return *configured;
    }

    if(sentinel!=models.end()) return *sentinel;
    return models.front();
}

}
