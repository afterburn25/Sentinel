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
            "You are a synthetic test counterpart inside Sentinel Simulation Lab. "
            "This is a closed simulation only. Stay consistent with this fictional persona: "
            + context.personaSummary +
            " Do not claim real-world actions occurred. Reply naturally and concisely to the investigator's test message.";

        return Complete(system,context,std::string(investigatorMessage));
    }

    std::string GenerateInvestigatorSuggestion(
        const ModelContext& context) override
    {
        std::string system =
            "You are Sentinel's test response assistant inside a closed Simulation Lab. "
            "Suggest one concise investigator reply for testing. "
            "Preserve the configured synthetic persona facts, avoid inventing facts, and do not send anything automatically.";
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
        HINTERNET session=WinHttpOpen(L"Sentinel/0.4",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0);
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

}
