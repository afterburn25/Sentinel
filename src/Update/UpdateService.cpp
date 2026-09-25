#include "Sentinel/Update/UpdateService.hpp"
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace sentinel::update {
namespace {
std::wstring Widen(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);
    std::wstring out((size_t)n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n);
    return out;
}
std::string ExtractString(const std::string& json,const std::string& key) {
    auto p=json.find("\""+key+"\"");
    if(p==std::string::npos) return {};
    p=json.find(':',p);
    if(p==std::string::npos) return {};
    p=json.find('"',p);
    if(p==std::string::npos) return {};
    std::string out;
    bool esc=false;
    for(size_t i=p+1;i<json.size();++i) {
        char c=json[i];
        if(!esc && c=='"') break;
        if(!esc && c=='\\') { esc=true; continue; }
        if(esc) {
            if(c=='n') out+='\n';
            else if(c=='r') out+='\r';
            else if(c=='t') out+='\t';
            else out+=c;
            esc=false;
        } else out+=c;
    }
    return out;
}
std::vector<int> Parts(const std::string& v) {
    std::vector<int> out;
    size_t start=0;
    while(start<v.size()) {
        auto end=v.find('.',start);
        auto token=v.substr(start,end==std::string::npos?std::string::npos:end-start);
        size_t digits=0;
        while(digits<token.size() && token[digits]>='0' && token[digits]<='9') ++digits;
        try { out.push_back(digits?std::stoi(token.substr(0,digits)):0); } catch(...) { out.push_back(0); }
        if(end==std::string::npos) break;
        start=end+1;
    }
    while(out.size()<3) out.push_back(0);
    return out;
}
std::string GetHttps(const std::string& url) {
    auto w=Widen(url);
    URL_COMPONENTS uc{}; uc.dwStructSize=sizeof(uc);
    wchar_t host[512]{},path[4096]{};
    uc.lpszHostName=host; uc.dwHostNameLength=511;
    uc.lpszUrlPath=path; uc.dwUrlPathLength=4095;
    if(!WinHttpCrackUrl(w.c_str(),0,0,&uc)) throw std::runtime_error("invalid update manifest URL");
    if(uc.nScheme!=INTERNET_SCHEME_HTTPS) throw std::runtime_error("update manifest must use HTTPS");

    HINTERNET session=WinHttpOpen(L"Sentinel-Update/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0);
    if(!session) throw std::runtime_error("cannot initialize update connection");
    WinHttpSetTimeouts(session,5000,5000,10000,15000);
    std::wstring h(host,uc.dwHostNameLength),p(path,uc.dwUrlPathLength);
    HINTERNET connect=WinHttpConnect(session,h.c_str(),uc.nPort,0);
    if(!connect) { WinHttpCloseHandle(session); throw std::runtime_error("cannot connect to update host"); }
    HINTERNET request=WinHttpOpenRequest(connect,L"GET",p.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if(!request) {
        WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
        throw std::runtime_error("cannot create update request");
    }
    std::wstring headers=L"Accept: application/json\r\nUser-Agent: Sentinel/1.0\r\n";
    BOOL ok=WinHttpSendRequest(request,headers.c_str(),(DWORD)-1L,WINHTTP_NO_REQUEST_DATA,0,0,0);
    if(ok) ok=WinHttpReceiveResponse(request,nullptr);
    if(!ok) {
        WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
        throw std::runtime_error("update manifest request failed");
    }
    DWORD status=0,size=sizeof(status);
    WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr);
    std::string body;
    for(;;) {
        DWORD avail=0; if(!WinHttpQueryDataAvailable(request,&avail) || !avail) break;
        size_t old=body.size(); body.resize(old+avail);
        DWORD read=0; if(!WinHttpReadData(request,body.data()+old,avail,&read)) break;
        body.resize(old+read);
    }
    WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
    if(status<200 || status>=300) throw std::runtime_error("update manifest returned HTTP "+std::to_string(status));
    return body;
}
}

bool UpdateService::IsNewerVersion(const std::string& candidate,const std::string& current) {
    auto a=Parts(candidate),b=Parts(current);
    size_t n=std::max(a.size(),b.size()); a.resize(n); b.resize(n);
    for(size_t i=0;i<n;i++) {
        if(a[i]>b[i]) return true;
        if(a[i]<b[i]) return false;
    }
    return false;
}

UpdateInfo UpdateService::Check(const std::string& manifestUrl,const std::string& currentVersion) const {
    auto json=GetHttps(manifestUrl);
    UpdateInfo info;
    info.version=ExtractString(json,"version");
    info.downloadUrl=ExtractString(json,"download_url");
    info.sha256=ExtractString(json,"sha256");
    info.notes=ExtractString(json,"notes");
    if(info.version.empty()) throw std::runtime_error("update manifest has no version");
    if(!info.downloadUrl.empty() && info.downloadUrl.rfind("https://",0)!=0)
        throw std::runtime_error("update download URL must use HTTPS");
    info.newer=IsNewerVersion(info.version,currentVersion);
    return info;
}
}
