#include "Sentinel/Simulation/IModelAdapter.hpp"

#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <memory>
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

std::string HttpGetJson(const std::string& endpoint,const std::string& apiKey) {
    auto u=ParseUrl(endpoint);
    auto modelsPath=ModelsPathFromChatPath(u.path);

    HINTERNET session=WinHttpOpen(L"Sentinel/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0);
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

    BOOL ok=WinHttpSendRequest(request,headers.c_str(),(DWORD)-1L,WINHTTP_NO_REQUEST_DATA,0,0,0);
    if(ok) ok=WinHttpReceiveResponse(request,nullptr);
    if(!ok) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        throw std::runtime_error("model discovery request failed");
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
    OpenAICompatibleModel(std::string endpoint,std::string model,std::string apiKey)
        : endpoint_(std::move(endpoint)), model_(std::move(model)), apiKey_(std::move(apiKey)) {}

    std::string Name() const override {
        return "OpenAI-compatible model: " + model_;
    }

    std::string GenerateSyntheticReply(
        std::string_view investigatorMessage,
        const ModelContext& context) override
    {
        std::string system =
            "You are the synthetic counterpart inside Sentinel Simulation Lab. This is a closed simulation only. "
            "Stay strictly consistent with this configured fictional persona: " + context.personaSummary + " "
            "Conversation rules: answer the investigator's most recent message directly before adding anything else; "
            "use the recent conversation history to resolve pronouns, follow-ups, yes/no replies, and references such as 'that' or 'why'; "
            "do not ignore a direct question and pivot to an unrelated topic; do not invent persona facts that are not in the configured profile; "
            "if a requested fact is not configured, say naturally that you have not shared or established it yet; "
            "keep tone natural and conversational, usually 1-3 short sentences; ask at most one relevant follow-up question; "
            "avoid repetitive stock phrases and do not sound like a customer-service bot; "
            "when older conversation memory is provided, remember the meaning and important facts but paraphrase naturally. "
            "Do not repeat old lines word-for-word unless the investigator explicitly asks for an exact quote. "
            "Human-style recall can be slightly approximate in wording while remaining faithful to the remembered facts. "
            "do not claim real-world actions occurred outside this simulation.";

        if(!context.recalledMemory.empty()) {
            system += " Relevant earlier-conversation memory follows. Treat it as private background context, not as text to copy: " +
                context.recalledMemory;
        }
        return Complete(system,context,std::string(investigatorMessage));
    }

    std::string GenerateInvestigatorSuggestion(
        const ModelContext& context) override
    {
        std::string system =
            "You are Sentinel's response-review assistant inside a closed Simulation Lab. "
            "Suggest one concise investigator reply that directly responds to the synthetic subject's latest message. "
            "Use the conversation history, preserve all configured persona facts, do not invent facts, "
            "avoid abrupt topic changes, and do not send anything automatically.";
        return Complete(system,context,{});
    }

private:
    std::string endpoint_,model_,apiKey_;

    std::string Complete(
        const std::string& system,
        const ModelContext& context,
        const std::string& latestUser)
    {
        std::string json="{\"model\":\""+JsonEscape(model_)+"\",\"temperature\":0.35,\"messages\":[";
        json+="{\"role\":\"system\",\"content\":\""+JsonEscape(system)+"\"}";
        for(const auto& turn:context.history) {
            if(turn.speaker==ChatTurn::Speaker::ModelSuggestion) continue;
            const char* role=turn.speaker==ChatTurn::Speaker::Investigator?"user":"assistant";
            json+=",{\"role\":\"";
            json+=role;
            json+="\",\"content\":\""+JsonEscape(turn.text)+"\"}";
        }
        if(!latestUser.empty()) {
            bool alreadyLast=!context.history.empty() &&
                context.history.back().speaker==ChatTurn::Speaker::Investigator &&
                context.history.back().text==latestUser;
            if(!alreadyLast) json+=",{\"role\":\"user\",\"content\":\""+JsonEscape(latestUser)+"\"}";
        }
        json+="]}";

        auto u=ParseUrl(endpoint_);
        HINTERNET session=WinHttpOpen(L"Sentinel/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0);
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
    std::string apiKey)
{
    return std::make_unique<OpenAICompatibleModel>(
        std::move(endpoint),std::move(model),std::move(apiKey));
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

}
