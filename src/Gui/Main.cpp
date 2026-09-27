#include "Sentinel/Audit/AuditService.hpp"
#include "Sentinel/Core/CaseRepository.hpp"
#include "Sentinel/Core/CaseService.hpp"
#include "Sentinel/Evidence/EvidenceService.hpp"
#include "Sentinel/Evidence/SevContainer.hpp"
#include "Sentinel/Security/Crypto.hpp"
#include "Sentinel/Security/KeyManager.hpp"
#include "Sentinel/Security/SecretProtector.hpp"
#include "Sentinel/Storage/MigrationService.hpp"
#include "Sentinel/Simulation/IModelAdapter.hpp"
#include "Sentinel/Simulation/PersonaPolicy.hpp"
#include "Sentinel/Simulation/SettingsStore.hpp"
#include "Sentinel/Simulation/ResponseEvaluator.hpp"
#include "Sentinel/Simulation/SessionStore.hpp"
#include "Sentinel/Simulation/ConversationMemory.hpp"
#include "Sentinel/Simulation/ModelRegistry.hpp"
#include "Sentinel/Simulation/PersonaProfileStore.hpp"
#include "Sentinel/Simulation/TrainingReviewStore.hpp"
#include "Sentinel/Simulation/TrainerStore.hpp"
#include "Sentinel/Operations/Messaging.hpp"
#include "Sentinel/Operations/Supervisor.hpp"
#include "Sentinel/Channels/ChannelCore.hpp"
#include "Sentinel/Channels/AutomationEngine.hpp"
#include "Sentinel/Channels/ChannelAdapterRegistry.hpp"
#include "Sentinel/Channels/JurisdictionRules.hpp"
#include "Sentinel/Agency/AgencyServer.hpp"
#include "Sentinel/Update/UpdateService.hpp"

#include <windows.h>
#include <windowsx.h>
#ifdef DecryptFile
#undef DecryptFile
#endif
#include <commdlg.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <chrono>
#include <cctype>
#include <memory>
#include <iterator>
#include <optional>
#include <random>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClassName[] = L"SARANativeWindow";
constexpr int kSaraIconResource = 101;
constexpr int kSidebar = 220;
constexpr int kHeader = 78;
constexpr UINT_PTR kSimTypingStartTimer = 4101;
constexpr UINT_PTR kSimReplyTimer = 4102;
constexpr UINT_PTR kSimEngagementTimer = 4103;
constexpr int kSimVisibleRows = 4;

enum class Page { Dashboard, Cases, Evidence, Audit, Verification, Simulation, Persona, ModelLab, Trainer, Messaging, Supervisor, Agency, Settings };
enum class IconKind { Shield, Home, Folder, Database, Document, Check, Gear, Search, Plus, Chain, Lock, Chat };

struct RectF { float l,t,r,b; bool Contains(float x,float y) const { return x>=l&&x<=r&&y>=t&&y<=b; } };

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);
    std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n);
    return out;
}

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n,nullptr,nullptr);
    return out;
}

std::filesystem::path AppDataRoot() {
    PWSTR p{};
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&p))) {
        const std::filesystem::path local(p);
        CoTaskMemFree(p);
        const auto sara = local / L"SARA";
        const auto legacy = local / L"Sentinel";
        if (std::filesystem::exists(sara)) return sara;
        if (std::filesystem::exists(legacy)) return legacy; // preserve existing Sentinel data on upgrade
        return sara;
    }
    return std::filesystem::current_path() / "sara-data";
}

std::filesystem::path ExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr,path,MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

std::filesystem::path MigrationsDir() {
    auto p = ExeDir() / "migrations";
    if (std::filesystem::exists(p)) return p;
    p = ExeDir().parent_path() / "migrations";
    if (std::filesystem::exists(p)) return p;
    p = std::filesystem::current_path() / "migrations";
    return p;
}

struct SplashState {
    HBITMAP bitmap{};
    UINT imageWidth{};
    UINT imageHeight{};
    HANDLE readyEvent{};
    ULONGLONG started{};
    int progressPercent{};
};

struct SplashThreadContext {
    HINSTANCE instance{};
    HANDLE readyEvent{};
    HANDLE createdEvent{};
};

bool LoadBitmapWithWic(const std::filesystem::path& path,HBITMAP& bitmap,UINT& width,UINT& height) {
    bitmap=nullptr; width=0; height=0;
    if(!std::filesystem::exists(path)) return false;

    ComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(
            CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
            __uuidof(IWICImagingFactory),reinterpret_cast<void**>(factory.GetAddressOf()))) || !factory)
        return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if(FAILED(factory->CreateDecoderFromFilename(
            path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)) || !decoder)
        return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if(FAILED(decoder->GetFrame(0,&frame)) || !frame) return false;
    if(FAILED(frame->GetSize(&width,&height)) || width==0 || height==0) return false;

    ComPtr<IWICFormatConverter> converter;
    if(FAILED(factory->CreateFormatConverter(&converter)) || !converter) return false;
    if(FAILED(converter->Initialize(
            frame.Get(),GUID_WICPixelFormat32bppBGR,
            WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom)))
        return false;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=(LONG)width;
    bmi.bmiHeader.biHeight=-(LONG)height;
    bmi.bmiHeader.biPlanes=1;
    bmi.bmiHeader.biBitCount=32;
    bmi.bmiHeader.biCompression=BI_RGB;

    void* bits=nullptr;
    HDC dc=GetDC(nullptr);
    bitmap=CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
    ReleaseDC(nullptr,dc);
    if(!bitmap || !bits) {
        if(bitmap) DeleteObject(bitmap);
        bitmap=nullptr;
        return false;
    }

    const UINT stride=width*4;
    if(FAILED(converter->CopyPixels(nullptr,stride,stride*height,reinterpret_cast<BYTE*>(bits)))) {
        DeleteObject(bitmap);
        bitmap=nullptr;
        return false;
    }
    return true;
}

void PaintSplashImage(HWND hwnd,SplashState* state,HDC dc) {
    RECT rc{}; GetClientRect(hwnd,&rc);
    if(state && state->bitmap) {
        HDC mem=CreateCompatibleDC(dc);
        HGDIOBJ old=SelectObject(mem,state->bitmap);
        const int clientW=rc.right-rc.left;
        const int clientH=rc.bottom-rc.top;
        if(clientW==(int)state->imageWidth && clientH==(int)state->imageHeight) {
            BitBlt(dc,0,0,clientW,clientH,mem,0,0,SRCCOPY);
        } else {
            SetStretchBltMode(dc,HALFTONE);
            SetBrushOrgEx(dc,0,0,nullptr);
            StretchBlt(dc,0,0,clientW,clientH,mem,0,0,
                (int)state->imageWidth,(int)state->imageHeight,SRCCOPY);
        }
        SelectObject(mem,old);
        DeleteDC(mem);
        return;
    }

    FillRect(dc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,RGB(225,240,255));
    HFONT title=CreateFontW(
        -72,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HFONT sub=CreateFontW(
        -24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HFONT oldFont=(HFONT)SelectObject(dc,title);
    RECT titleRc{0,rc.bottom/2-70,rc.right,rc.bottom/2+20};
    DrawTextW(dc,L"SARA",-1,&titleRc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(dc,sub);
    RECT subRc{0,rc.bottom/2+10,rc.right,rc.bottom/2+60};
    DrawTextW(dc,L"Synthetic Adaptive Response Agent",-1,&subRc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(dc,oldFont);
    DeleteObject(title);
    DeleteObject(sub);
}

void PaintSplashProgress(HWND hwnd,SplashState* state,HDC dc) {
    if(!state || !state->bitmap || state->imageWidth==0 || state->imageHeight==0) return;

    RECT rc{}; GetClientRect(hwnd,&rc);
    const int clientW=rc.right-rc.left;
    const int clientH=rc.bottom-rc.top;

    // This is the one progress bar built into the approved 1672x941 artwork.
    // The source PNG starts empty; SARA fills this same bar left-to-right.
    constexpr int srcX=688;
    constexpr int srcY=820;
    constexpr int srcW=302;
    constexpr int srcH=8;

    const double scaleX=(double)clientW/(double)state->imageWidth;
    const double scaleY=(double)clientH/(double)state->imageHeight;
    const int dstX=(int)std::lround(srcX*scaleX);
    const int dstY=(int)std::lround(srcY*scaleY);
    const int dstW=std::max(1,(int)std::lround(srcW*scaleX));
    const int dstH=std::max(2,(int)std::lround(srcH*scaleY));

    // Restore the original empty bar before drawing the new progress amount.
    HDC mem=CreateCompatibleDC(dc);
    HGDIOBJ old=SelectObject(mem,state->bitmap);
    if(clientW==(int)state->imageWidth && clientH==(int)state->imageHeight) {
        BitBlt(dc,dstX,dstY,dstW,dstH,mem,srcX,srcY,SRCCOPY);
    } else {
        SetStretchBltMode(dc,HALFTONE);
        StretchBlt(dc,dstX,dstY,dstW,dstH,mem,srcX,srcY,srcW,srcH,SRCCOPY);
    }
    SelectObject(mem,old);
    DeleteDC(mem);

    const int fillW=(dstW*std::clamp(state->progressPercent,0,100))/100;
    if(fillW>0) {
        HBRUSH cyan=CreateSolidBrush(RGB(0,220,255));
        RECT fill{dstX,dstY,dstX+fillW,dstY+dstH};
        FillRect(dc,&fill,cyan);
        DeleteObject(cyan);
    }
}

LRESULT CALLBACK SplashWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* state=reinterpret_cast<SplashState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE) {
        auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    if(msg==WM_ERASEBKGND) return 1;
    if(msg==WM_TIMER) {
        if(state) {
            const ULONGLONG elapsed=GetTickCount64()-state->started;
            const bool ready=state->readyEvent &&
                WaitForSingleObject(state->readyEvent,0)==WAIT_OBJECT_0;

            if(ready && elapsed>=7000ULL)
                state->progressPercent=100;
            else
                state->progressPercent=(int)std::min<ULONGLONG>(90ULL,(elapsed*90ULL)/7000ULL);

            HDC dc=GetDC(hwnd);
            if(dc) {
                PaintSplashProgress(hwnd,state,dc);
                ReleaseDC(hwnd,dc);
            }
        }
        return 0;
    }
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc=BeginPaint(hwnd,&ps);
        PaintSplashImage(hwnd,state,dc);
        PaintSplashProgress(hwnd,state,dc);
        EndPaint(hwnd,&ps);
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

DWORD WINAPI SaraSplashThreadProc(LPVOID param) {
    auto* ctx=reinterpret_cast<SplashThreadContext*>(param);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);

    SplashState state;
    state.readyEvent=ctx->readyEvent;
    state.started=GetTickCount64();
    state.progressPercent=0;
    const bool imageLoaded=LoadBitmapWithWic(
        ExeDir()/L"assets"/L"SARA-Splash.png",
        state.bitmap,state.imageWidth,state.imageHeight);

    constexpr wchar_t splashClass[]=L"SARAStartupSplash";
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc=SplashWndProc;
    wc.hInstance=ctx->instance;
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName=splashClass;
    RegisterClassExW(&wc);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int width=imageLoaded?(int)state.imageWidth:1672;
    int height=imageLoaded?(int)state.imageHeight:941;
    const int maxW=(int)((work.right-work.left)*0.96);
    const int maxH=(int)((work.bottom-work.top)*0.96);
    if(width>maxW || height>maxH) {
        const double scale=std::min((double)maxW/width,(double)maxH/height);
        width=(int)(width*scale);
        height=(int)(height*scale);
    }
    const int x=work.left+(work.right-work.left-width)/2;
    const int y=work.top+(work.bottom-work.top-height)/2;

    if(!imageLoaded) {
        state.imageWidth=1672;
        state.imageHeight=941;
    }

    HWND splash=CreateWindowExW(
        WS_EX_TOOLWINDOW|WS_EX_TOPMOST,splashClass,L"SARA",
        WS_POPUP,x,y,width,height,nullptr,nullptr,ctx->instance,&state);

    if(ctx->createdEvent) SetEvent(ctx->createdEvent);

    if(splash) {
        ShowWindow(splash,SW_SHOWNORMAL);
        UpdateWindow(splash);
        SetTimer(splash,1,50,nullptr);

        while(true) {
            MSG msg{};
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            const ULONGLONG elapsed=GetTickCount64()-state.started;
            const bool appReady=ctx->readyEvent &&
                WaitForSingleObject(ctx->readyEvent,0)==WAIT_OBJECT_0;

            if(appReady && elapsed>=7000ULL) {
                state.progressPercent=100;
                HDC dc=GetDC(splash);
                if(dc) {
                    PaintSplashProgress(splash,&state,dc);
                    ReleaseDC(splash,dc);
                }
                Sleep(120);
                break;
            }

            Sleep(10);
        }

        KillTimer(splash,1);
        DestroyWindow(splash);
    }

    if(state.bitmap) DeleteObject(state.bitmap);
    UnregisterClassW(splashClass,ctx->instance);
    CoUninitialize();
    return 0;
}

bool IsLocalModelEndpoint(const std::string& endpoint) {
    return endpoint.find("127.0.0.1") != std::string::npos ||
           endpoint.find("localhost") != std::string::npos;
}

std::wstring ReadTextFileTail(const std::filesystem::path& path,size_t maxChars=1800) {
    if(!std::filesystem::exists(path)) return {};
    std::ifstream in(path,std::ios::binary);
    if(!in) return {};
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(bytes.size()>maxChars) bytes=bytes.substr(bytes.size()-maxChars);
    return Widen(bytes);
}

bool StartBundledAiService(std::wstring* failure = nullptr) {
    const auto aiDir = ExeDir() / L"ai";
    const auto script = aiDir / L"Start-Sentinel-With-AI.ps1";
    const auto setup = ExeDir() / L"Setup-Sentinel-AI.cmd";
    const auto model = aiDir / L"models" / L"Qwen3.5-9B-Q4_K_M.gguf";
    const auto startupLog = aiDir / L"logs" / L"startup.log";

    if (!std::filesystem::exists(script)) {
        if (failure) *failure = L"Bundled AI launcher is missing: " + script.wstring();
        return false;
    }

    bool runtimeFound=false;
    for(const auto& root : {aiDir/L"runtime",aiDir/L"runtime_cpu"}) {
        if(!std::filesystem::exists(root)) continue;
        for(const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if(entry.is_regular_file() && _wcsicmp(entry.path().filename().c_str(),L"llama-server.exe")==0) {
                runtimeFound=true;
                break;
            }
        }
        if(runtimeFound) break;
    }

    if(!std::filesystem::exists(model)) {
        if(failure) {
            *failure=L"Local model is not installed: "+model.wstring()+
                L". Run "+setup.wstring()+L" once to download/install the local AI backend.";
        }
        return false;
    }
    if(!runtimeFound) {
        if(failure) {
            *failure=L"llama.cpp runtime is not installed under "+aiDir.wstring()+
                L". Run "+setup.wstring()+L" once to install/repair the local AI backend.";
        }
        return false;
    }

    std::filesystem::create_directories(aiDir/L"logs");
    std::wstring command =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" +
        script.wstring() + L"\" -NoLaunch";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(
            nullptr, command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, ExeDir().c_str(), &si, &pi)) {
        if (failure) *failure = L"Could not launch PowerShell for the bundled local AI service. Windows error "+std::to_wstring(GetLastError())+L".";
        return false;
    }

    const DWORD wait = WaitForSingleObject(pi.hProcess, 180000);
    DWORD exitCode = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (wait != WAIT_OBJECT_0) {
        if (failure) *failure = L"Timed out while starting the bundled local AI service. See "+startupLog.wstring();
        return false;
    }
    if (exitCode != 0) {
        if (failure) {
            auto detail=ReadTextFileTail(startupLog);
            *failure=L"Bundled local AI service failed (PowerShell exit "+std::to_wstring(exitCode)+L").";
            if(!detail.empty()) *failure+=L" Startup log: "+detail;
            else *failure+=L" No startup log was produced.";
        }
        return false;
    }
    return true;
}

bool BundledAiPrerequisitesPresent() {
    const auto aiDir=ExeDir()/L"ai";
    const auto model=aiDir/L"models"/L"Qwen3.5-9B-Q4_K_M.gguf";
    if(!std::filesystem::exists(model)) return false;

    for(const auto& root : {aiDir/L"runtime",aiDir/L"runtime_cpu"}) {
        if(!std::filesystem::exists(root)) continue;
        for(const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if(entry.is_regular_file() &&
               _wcsicmp(entry.path().filename().c_str(),L"llama-server.exe")==0)
                return true;
        }
    }
    return false;
}

bool RunBundledAiSetup(std::wstring* failure=nullptr) {
    const auto setup=ExeDir()/L"Setup-Sentinel-AI.cmd";
    if(!std::filesystem::exists(setup)) {
        if(failure) *failure=L"Bundled AI installer is missing: "+setup.wstring();
        return false;
    }

    std::wstring command=L"cmd.exe /d /c \"\""+setup.wstring()+L"\"\"";
    STARTUPINFOW si{};
    si.cb=sizeof(si);
    PROCESS_INFORMATION pi{};
    if(!CreateProcessW(
            nullptr,command.data(),nullptr,nullptr,FALSE,
            CREATE_NEW_CONSOLE,nullptr,ExeDir().c_str(),&si,&pi)) {
        if(failure) *failure=L"Could not launch bundled AI setup. Windows error "+std::to_wstring(GetLastError())+L".";
        return false;
    }

    const DWORD wait=WaitForSingleObject(pi.hProcess,INFINITE);
    DWORD exitCode=1;
    if(wait==WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess,&exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if(wait!=WAIT_OBJECT_0 || exitCode!=0) {
        if(failure) {
            *failure=L"Bundled AI setup failed";
            const auto log=ReadTextFileTail(ExeDir()/L"ai"/L"logs"/L"setup.log",2400);
            if(!log.empty()) *failure+=L". Setup log: "+log;
        }
        return false;
    }
    return BundledAiPrerequisitesPresent();
}

std::wstring AuditActionName(int action) {
    switch(action) {
        case 1: return L"Application initialized";
        case 100: return L"Case created";
        case 101: return L"Case opened";
        case 102: return L"Case updated";
        case 103: return L"Case closed";
        case 104: return L"Case reopened";
        case 200: return L"Evidence import started";
        case 201: return L"Evidence imported";
        case 202: return L"Evidence viewed";
        case 203: return L"Evidence verified";
        case 204: return L"Evidence exported";
        case 205: return L"Evidence derivative created";
        case 206: return L"Evidence integrity failure";
        case 300: return L"Key created";
        case 301: return L"Key rotated";
        case 400: return L"Integrity check started";
        case 401: return L"Integrity check completed";
        case 402: return L"Integrity check failed";
        case 500: return L"Recovery started";
        case 501: return L"Recovery completed";
        case 502: return L"Recovery failed";
        case 900: return L"Application shutdown";
        default: return L"System event";
    }
}

struct Runtime {
    std::filesystem::path root;
    sentinel::SqliteDatabase db;
    sentinel::WindowsSecureRandom random;
    sentinel::WindowsHashService hash;
    sentinel::WindowsAesGcmCipher cipher;
    sentinel::WindowsDpapiSecretProtector dpapi;
    sentinel::MigrationService migrations;
    sentinel::channels::ChannelCoreStore channelCore;
    sentinel::channels::AutomationEngine automationEngine;
    sentinel::channels::ChannelAdapterRegistry channelAdapters;
    sentinel::channels::JurisdictionRuleStore jurisdictionRules;
    sentinel::simulation::ConversationMemoryStore conversationMemory;
    sentinel::simulation::PersonaProfileStore personaProfiles;
    sentinel::simulation::TrainingReviewStore trainingReviews;
    sentinel::simulation::TrainerStore trainer;
    sentinel::KeyManager keys;
    sentinel::SqliteCaseRepository caseRepo;
    sentinel::CaseService cases;
    sentinel::AuditService audit;

    Runtime()
        : root(AppDataRoot()),
          cipher(random),
          migrations(db),
          channelCore(db),
          jurisdictionRules(db),
          conversationMemory(db),
          personaProfiles(db),
          trainingReviews(db),
          trainer(db),
          keys(root/"keys"/"master.dpapi",db,dpapi,random,cipher),
          caseRepo(db,&keys,&cipher),
          cases(caseRepo),
          audit(db,hash) {
        std::filesystem::create_directories(root);
        db.Open(root/"sentinel.db");
        migrations.ApplyDirectory(MigrationsDir());
        jurisdictionRules.EnsureBuiltInBaselines();
        trainer.EnsureDefaultFoundation(
            "SARA Foundation 1",
            "Qwen3.5-9B",
            (ExeDir()/L"ai"/L"models"/L"Qwen3.5-9B-Q4_K_M.gguf").string());
        keys.Initialize();
    }

    std::vector<sentinel::EvidenceSummary> Evidence(const sentinel::CaseId& id) {
        auto key = keys.GetCaseKey(id);
        sentinel::EvidenceService svc(root/"evidence",db,random,hash,cipher,audit);
        return svc.ListForCase(id,key.Span());
    }

    long long AuditCount() {
        sqlite3_stmt* s{};
        long long count=0;
        if (sqlite3_prepare_v2(db.Handle(),"SELECT COUNT(*) FROM audit_records",-1,&s,nullptr)==SQLITE_OK &&
            sqlite3_step(s)==SQLITE_ROW) count=sqlite3_column_int64(s,0);
        sqlite3_finalize(s);
        return count;
    }

    long long EvidenceCount() {
        sqlite3_stmt* s{};
        long long count=0;
        if (sqlite3_prepare_v2(db.Handle(),"SELECT COUNT(*) FROM evidence",-1,&s,nullptr)==SQLITE_OK &&
            sqlite3_step(s)==SQLITE_ROW) count=sqlite3_column_int64(s,0);
        sqlite3_finalize(s);
        return count;
    }

    long long OpenCaseCount() {
        sqlite3_stmt* s{};
        long long count=0;
        if (sqlite3_prepare_v2(db.Handle(),"SELECT COUNT(*) FROM cases WHERE status=1",-1,&s,nullptr)==SQLITE_OK &&
            sqlite3_step(s)==SQLITE_ROW) count=sqlite3_column_int64(s,0);
        sqlite3_finalize(s);
        return count;
    }
};

struct BrushSet {
    ComPtr<ID2D1SolidColorBrush> bg, panel, panel2, sidebar, border, text, muted, blue, cyan, green, yellow, red, selection;
};

struct SelectableTextRun {
    std::wstring text;
    RectF rect;
    IDWriteTextFormat* format{};
    DWRITE_TEXT_ALIGNMENT align{DWRITE_TEXT_ALIGNMENT_LEADING};
    bool noWrap{false};
    bool paragraphCenter{false};
};

struct PersonaMediaItem {
    std::string id;
    std::string personaName;
    std::string originalName;
    std::string storedPath;
    std::string sha256;
    std::string tags;
    bool approved{false};
};

class App {
public:
    static void PositionVisibleChatCaret(HWND hwnd) {
        if(GetFocus()!=hwnd) return;

        DWORD selStart=0,selEnd=0;
        SendMessageW(hwnd,EM_GETSEL,(WPARAM)&selStart,(LPARAM)&selEnd);

        const int textLen=GetWindowTextLengthW(hwnd);
        int x=8;
        int y=7;

        if(selEnd < (DWORD)textLen) {
            const LRESULT pos=SendMessageW(hwnd,EM_POSFROMCHAR,(WPARAM)selEnd,0);
            if(pos!=-1) {
                x=(int)(short)LOWORD(pos);
                y=(int)(short)HIWORD(pos);
            }
        } else if(textLen>0) {
            // EM_POSFROMCHAR does not return a usable position for the insertion
            // point *after* the final character. Measure the final character and
            // place the caret immediately after it instead of snapping to x=0.
            const int last=textLen-1;
            const LRESULT pos=SendMessageW(hwnd,EM_POSFROMCHAR,(WPARAM)last,0);
            if(pos!=-1) {
                x=(int)(short)LOWORD(pos);
                y=(int)(short)HIWORD(pos);

                std::wstring fullText((size_t)textLen+1,L'\0');
                GetWindowTextW(hwnd,fullText.data(),textLen+1);
                wchar_t ch[2]{fullText[(size_t)last],L'\0'};

                HDC dc=GetDC(hwnd);
                if(dc) {
                    HFONT font=(HFONT)SendMessageW(hwnd,WM_GETFONT,0,0);
                    HGDIOBJ oldFont=nullptr;
                    if(font) oldFont=SelectObject(dc,font);
                    SIZE size{};
                    if(ch[0] && GetTextExtentPoint32W(dc,ch,1,&size)) x+=std::max(1L,size.cx);
                    else x+=8;
                    if(oldFont) SelectObject(dc,oldFont);
                    ReleaseDC(hwnd,dc);
                }
            }
        }

        SetCaretPos(std::max(8,x),std::max(7,y));
    }

    static LRESULT CALLBACK ChatEditSubclassProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref) {
        auto* app=reinterpret_cast<App*>(ref);

        if(msg==WM_SETFOCUS) {
            const LRESULT result=DefSubclassProc(hwnd,msg,wp,lp);
            DestroyCaret();
            CreateCaret(hwnd,nullptr,2,22);
            PositionVisibleChatCaret(hwnd);
            ShowCaret(hwnd);
            return result;
        }

        if(msg==WM_KILLFOCUS) {
            HideCaret(hwnd);
            DestroyCaret();
            return DefSubclassProc(hwnd,msg,wp,lp);
        }

        if(msg==WM_KEYDOWN && wp==VK_RETURN) {
            if(app) app->SendSimulationMessage();
            return 0;
        }

        if(msg==WM_KEYDOWN && (GetKeyState(VK_CONTROL)&0x8000)) {
            switch(wp) {
                case 'A': SendMessageW(hwnd,EM_SETSEL,0,-1); PositionVisibleChatCaret(hwnd); return 0;
                case 'C': SendMessageW(hwnd,WM_COPY,0,0); return 0;
                case 'X': SendMessageW(hwnd,WM_CUT,0,0); PositionVisibleChatCaret(hwnd); return 0;
                case 'V': SendMessageW(hwnd,WM_PASTE,0,0); PositionVisibleChatCaret(hwnd); return 0;
                case 'Z': SendMessageW(hwnd,WM_UNDO,0,0); PositionVisibleChatCaret(hwnd); return 0;
            }
        }

        if(msg==WM_CONTEXTMENU) {
            DWORD selStart=0,selEnd=0;
            SendMessageW(hwnd,EM_GETSEL,(WPARAM)&selStart,(LPARAM)&selEnd);
            const bool hasSelection=selStart!=selEnd;
            const bool canPaste=IsClipboardFormatAvailable(CF_UNICODETEXT)!=FALSE;
            const bool canUndo=SendMessageW(hwnd,EM_CANUNDO,0,0)!=0;

            HMENU menu=CreatePopupMenu();
            if(!menu) return 0;
            AppendMenuW(menu,MF_STRING|(canUndo?0:MF_GRAYED),1,L"Undo");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING|(hasSelection?0:MF_GRAYED),2,L"Cut");
            AppendMenuW(menu,MF_STRING|(hasSelection?0:MF_GRAYED),3,L"Copy");
            AppendMenuW(menu,MF_STRING|(canPaste?0:MF_GRAYED),4,L"Paste");
            AppendMenuW(menu,MF_STRING|(hasSelection?0:MF_GRAYED),5,L"Delete");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING,6,L"Select All");

            POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(pt.x==-1 && pt.y==-1) {
                RECT rc{};
                GetWindowRect(hwnd,&rc);
                pt.x=rc.left+12;
                pt.y=rc.top+12;
            }

            const int cmd=TrackPopupMenu(
                menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd,nullptr);
            DestroyMenu(menu);

            switch(cmd) {
                case 1: SendMessageW(hwnd,WM_UNDO,0,0); break;
                case 2: SendMessageW(hwnd,WM_CUT,0,0); break;
                case 3: SendMessageW(hwnd,WM_COPY,0,0); break;
                case 4: SendMessageW(hwnd,WM_PASTE,0,0); break;
                case 5: SendMessageW(hwnd,WM_CLEAR,0,0); break;
                case 6: SendMessageW(hwnd,EM_SETSEL,0,-1); break;
            }
            PositionVisibleChatCaret(hwnd);
            return 0;
        }

        const LRESULT result=DefSubclassProc(hwnd,msg,wp,lp);
        if(msg==WM_CHAR || msg==WM_KEYUP || msg==WM_LBUTTONUP ||
           msg==WM_PASTE || msg==WM_CUT || msg==WM_CLEAR || msg==WM_UNDO) {
            PositionVisibleChatCaret(hwnd);
        }
        return result;
    }

    void BeginTextSelection(float x,float y) {
        selectionDragging_=false;
        selectionMouseDown_=true;
        selectionStartPoint_={x,y};
        const int run=FindSelectableRun(x,y);
        if(run<0) {
            selectedTextRun_=-1;
            selectionAnchor_=selectionActive_=0;
            return;
        }
        selectedTextRun_=run;
        selectionAnchor_=selectionActive_=HitTestTextPosition(textRuns_[(size_t)run],x,y);
        SetCapture(hwnd_);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    bool UpdateTextSelection(float x,float y) {
        if(!selectionMouseDown_ || selectedTextRun_<0 || selectedTextRun_>=(int)textRuns_.size()) return false;
        if(std::abs(x-selectionStartPoint_.x)>3.0f || std::abs(y-selectionStartPoint_.y)>3.0f)
            selectionDragging_=true;
        selectionActive_=HitTestTextPosition(textRuns_[(size_t)selectedTextRun_],x,y);
        InvalidateRect(hwnd_,nullptr,FALSE);
        return true;
    }

    bool EndTextSelection(float x,float y) {
        if(!selectionMouseDown_) return false;
        if(selectedTextRun_>=0 && selectedTextRun_<(int)textRuns_.size())
            selectionActive_=HitTestTextPosition(textRuns_[(size_t)selectedTextRun_],x,y);
        const bool consumed=selectionDragging_;
        selectionMouseDown_=false;
        if(GetCapture()==hwnd_) ReleaseCapture();
        InvalidateRect(hwnd_,nullptr,FALSE);
        return consumed;
    }

    bool CopySelectedTextAt(float x,float y) {
        if(selectedTextRun_<0 || selectedTextRun_>=(int)textRuns_.size()) return false;
        const auto& run=textRuns_[(size_t)selectedTextRun_];
        const size_t a=std::min(selectionAnchor_,selectionActive_);
        const size_t b=std::max(selectionAnchor_,selectionActive_);
        if(a>=b || a>=run.text.size()) return false;
        const auto selected=run.text.substr(a,std::min(b,run.text.size())-a);

        HMENU menu=CreatePopupMenu();
        if(!menu) return false;
        AppendMenuW(menu,MF_STRING,1,L"Copy");
        AppendMenuW(menu,MF_STRING,2,L"Select All");
        POINT pt{(LONG)x,(LONG)y};
        ClientToScreen(hwnd_,&pt);
        const int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd_,nullptr);
        DestroyMenu(menu);
        if(cmd==2) {
            selectionAnchor_=0;
            selectionActive_=run.text.size();
            InvalidateRect(hwnd_,nullptr,FALSE);
            return true;
        }
        if(cmd!=1) return true;

        if(!OpenClipboard(hwnd_)) return true;
        EmptyClipboard();
        const SIZE_T bytes=(selected.size()+1)*sizeof(wchar_t);
        HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes);
        if(mem) {
            void* ptr=GlobalLock(mem);
            if(ptr) {
                memcpy(ptr,selected.c_str(),bytes);
                GlobalUnlock(mem);
                SetClipboardData(CF_UNICODETEXT,mem);
                mem=nullptr;
            }
            if(mem) GlobalFree(mem);
        }
        CloseClipboard();
        statusText_=L"Selected text copied";
        InvalidateRect(hwnd_,nullptr,FALSE);
        return true;
    }

    App() : runtime_(std::make_unique<Runtime>()) {}
    ~App() {
        if(chatFont_) DeleteObject(chatFont_);
    }

    HRESULT Init(HWND hwnd) {
        hwnd_=hwnd;
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
        D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory),
            nullptr,
            reinterpret_cast<void**>(factory_.GetAddressOf()));
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf()));
        CoCreateInstance(
            CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
            __uuidof(IWICImagingFactory),reinterpret_cast<void**>(wicFactory_.GetAddressOf()));
        CreateResources();
        LoadData();

        caseNumberEdit_ = CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,
            0,0,0,0,hwnd_,(HMENU)1001,GetModuleHandleW(nullptr),nullptr);
        caseTitleEdit_ = CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,
            0,0,0,0,hwnd_,(HMENU)1002,GetModuleHandleW(nullptr),nullptr);
        chatEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,L"EDIT",L"",
            WS_CHILD|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,
            0,0,0,0,hwnd_,(HMENU)1003,GetModuleHandleW(nullptr),nullptr);
        modelEndpointEdit_ = CreateWindowExW(0,L"EDIT",L"http://127.0.0.1:1234/v1/chat/completions",
            WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1004,GetModuleHandleW(nullptr),nullptr);
        modelNameEdit_ = CreateWindowExW(0,L"EDIT",L"local-model",
            WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1005,GetModuleHandleW(nullptr),nullptr);
        modelCombo_ = CreateWindowExW(0,L"COMBOBOX",L"",
            WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1007,GetModuleHandleW(nullptr),nullptr);
        simScroll_ = CreateWindowExW(0,L"SCROLLBAR",L"",WS_CHILD|SBS_VERT,
            0,0,0,0,hwnd_,(HMENU)1006,GetModuleHandleW(nullptr),nullptr);

        personaNameEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_MULTILINE|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1010,GetModuleHandleW(nullptr),nullptr);
        personaAgeCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1011,GetModuleHandleW(nullptr),nullptr);
        personaLocationEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1012,GetModuleHandleW(nullptr),nullptr);
        personaInterestsEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1013,GetModuleHandleW(nullptr),nullptr);
        personaStyleEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1014,GetModuleHandleW(nullptr),nullptr);
        personaWritingStyleCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1039,GetModuleHandleW(nullptr),nullptr);
        personaProfileCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1040,GetModuleHandleW(nullptr),nullptr);
        scenarioNameEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1015,GetModuleHandleW(nullptr),nullptr);
        scenarioObjectiveEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1016,GetModuleHandleW(nullptr),nullptr);
        scenarioSeedEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1017,GetModuleHandleW(nullptr),nullptr);
        minDelayEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1018,GetModuleHandleW(nullptr),nullptr);
        maxDelayEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1019,GetModuleHandleW(nullptr),nullptr);
        ageStateCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1020,GetModuleHandleW(nullptr),nullptr);
        personaGenderCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1023,GetModuleHandleW(nullptr),nullptr);
        personaPronounsCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1024,GetModuleHandleW(nullptr),nullptr);
        personaRelationshipCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1025,GetModuleHandleW(nullptr),nullptr);
        personaPersonalityCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1026,GetModuleHandleW(nullptr),nullptr);
        personaSocialCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1027,GetModuleHandleW(nullptr),nullptr);
        personaConfidenceCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1028,GetModuleHandleW(nullptr),nullptr);
        personaOccupationEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1029,GetModuleHandleW(nullptr),nullptr);
        personaEducationEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1030,GetModuleHandleW(nullptr),nullptr);
        personaFamilyEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1031,GetModuleHandleW(nullptr),nullptr);
        personaBackgroundEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1032,GetModuleHandleW(nullptr),nullptr);
        responseRuleTriggerEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1042,GetModuleHandleW(nullptr),nullptr);
        responseRuleResponseEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1043,GetModuleHandleW(nullptr),nullptr);
        trainerModeCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1050,GetModuleHandleW(nullptr),nullptr);
        trainerFoundationCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1051,GetModuleHandleW(nullptr),nullptr);
        trainerForkNameEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1052,GetModuleHandleW(nullptr),nullptr);
        trainerBasePathEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1053,GetModuleHandleW(nullptr),nullptr);
        trainerLoraNameEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1054,GetModuleHandleW(nullptr),nullptr);
        trainerLoraPathEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1055,GetModuleHandleW(nullptr),nullptr);
        trainerDatasetEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1056,GetModuleHandleW(nullptr),nullptr);
        trainerOutputEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1057,GetModuleHandleW(nullptr),nullptr);
        trainerInstructionEdit_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,0,0,0,0,hwnd_,(HMENU)1058,GetModuleHandleW(nullptr),nullptr);
        agencyEndpointEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1021,GetModuleHandleW(nullptr),nullptr);
        agencyIdEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1022,GetModuleHandleW(nullptr),nullptr);
        operatingStateCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1033,GetModuleHandleW(nullptr),nullptr);
        personaCommunicationCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1034,GetModuleHandleW(nullptr),nullptr);
        personaCognitiveCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1041,GetModuleHandleW(nullptr),nullptr);
        personaSlangCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1035,GetModuleHandleW(nullptr),nullptr);
        personaGrammarCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1036,GetModuleHandleW(nullptr),nullptr);
        personaTypoCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1037,GetModuleHandleW(nullptr),nullptr);
        personaEmojiCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1038,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(caseNumberEdit_,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        SendMessageW(caseTitleEdit_,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        chatFont_=CreateFontW(
            -21,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        SendMessageW(chatEdit_,WM_SETFONT,(WPARAM)(chatFont_?chatFont_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(chatEdit_,EM_SETLIMITTEXT,4000,0);
        SendMessageW(modelEndpointEdit_,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        SendMessageW(modelNameEdit_,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        SendMessageW(modelCombo_,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        HWND advancedEdits[]={personaNameEdit_,personaLocationEdit_,personaInterestsEdit_,
            personaOccupationEdit_,personaEducationEdit_,personaFamilyEdit_,personaBackgroundEdit_,
            responseRuleTriggerEdit_,responseRuleResponseEdit_,
            trainerForkNameEdit_,trainerBasePathEdit_,trainerLoraNameEdit_,trainerLoraPathEdit_,trainerDatasetEdit_,trainerOutputEdit_,trainerInstructionEdit_,
            scenarioNameEdit_,scenarioObjectiveEdit_,scenarioSeedEdit_,minDelayEdit_,maxDelayEdit_,agencyEndpointEdit_,agencyIdEdit_};
        for(HWND e:advancedEdits) {
            SendMessageW(e,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
            SetWindowTheme(e,L"DarkMode_Explorer",nullptr);
            SendMessageW(e,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
        }
        HWND personaCombos[]={personaAgeCombo_,ageStateCombo_,personaGenderCombo_,personaPronounsCombo_,personaRelationshipCombo_,
            personaPersonalityCombo_,personaSocialCombo_,personaConfidenceCombo_,modelCombo_,operatingStateCombo_,
            personaCommunicationCombo_,personaCognitiveCombo_,personaSlangCombo_,personaGrammarCombo_,personaTypoCombo_,personaEmojiCombo_,
            personaWritingStyleCombo_,personaProfileCombo_,trainerModeCombo_,trainerFoundationCombo_};
        for(HWND combo:personaCombos) {
            SendMessageW(combo,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
            SetWindowTheme(combo,L"DarkMode_Explorer",nullptr);
            SendMessageW(combo,CB_SETITEMHEIGHT,0,24);
            SendMessageW(combo,CB_SETITEMHEIGHT,(WPARAM)-1,24);
        }
        SendMessageW(caseNumberEdit_,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(10,10));
        SendMessageW(caseTitleEdit_,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(10,10));
        SendMessageW(caseNumberEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"CR-2026-0001");
        SendMessageW(caseTitleEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Investigation title");
        SetWindowTheme(caseNumberEdit_,L"DarkMode_Explorer",nullptr);
        SetWindowTheme(caseTitleEdit_,L"DarkMode_Explorer",nullptr);
        SetWindowTheme(chatEdit_,L"DarkMode_Explorer",nullptr);
        SetWindowTheme(modelEndpointEdit_,L"DarkMode_Explorer",nullptr);
        SetWindowTheme(modelNameEdit_,L"DarkMode_Explorer",nullptr);
        SetWindowTheme(modelCombo_,L"DarkMode_Explorer",nullptr);
        SetWindowTheme(simScroll_,L"DarkMode_Explorer",nullptr);
        SendMessageW(chatEdit_,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(10,10));
        SendMessageW(modelEndpointEdit_,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
        SendMessageW(modelNameEdit_,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
        SendMessageW(chatEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Type a synthetic test message and press Enter...");
        SendMessageW(responseRuleTriggerEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Trigger or sample question");
        SendMessageW(responseRuleResponseEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Investigator-approved response override");
        SendMessageW(trainerForkNameEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"SARA Foundation 2");
        SendMessageW(trainerBasePathEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Trainable source checkpoint path");
        SendMessageW(trainerLoraNameEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Samantha Personality v1");
        SendMessageW(trainerLoraPathEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"LoRA adapter path");
        SendMessageW(trainerDatasetEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Approved training JSONL path");
        SendMessageW(trainerOutputEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Training output folder");
        SendMessageW(trainerInstructionEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Tell SARA how this persona/model should change...");
        SetWindowSubclass(chatEdit_,ChatEditSubclassProc,1,reinterpret_cast<DWORD_PTR>(this));
        simSettings_=sentinel::simulation::LoadSimulationSettings(runtime_->root/"simulation.ini");
        SetWindowTextW(modelEndpointEdit_,Widen(simSettings_.endpoint).c_str());
        SetWindowTextW(modelNameEdit_,Widen(simSettings_.model).c_str());

        const wchar_t* stateItems[]={
            L"Alabama (AL)",L"Alaska (AK)",L"Arizona (AZ)",L"Arkansas (AR)",L"California (CA)",
            L"Colorado (CO)",L"Connecticut (CT)",L"Delaware (DE)",L"Florida (FL)",L"Georgia (GA)",
            L"Hawaii (HI)",L"Idaho (ID)",L"Illinois (IL)",L"Indiana (IN)",L"Iowa (IA)",
            L"Kansas (KS)",L"Kentucky (KY)",L"Louisiana (LA)",L"Maine (ME)",L"Maryland (MD)",
            L"Massachusetts (MA)",L"Michigan (MI)",L"Minnesota (MN)",L"Mississippi (MS)",L"Missouri (MO)",
            L"Montana (MT)",L"Nebraska (NE)",L"Nevada (NV)",L"New Hampshire (NH)",L"New Jersey (NJ)",
            L"New Mexico (NM)",L"New York (NY)",L"North Carolina (NC)",L"North Dakota (ND)",L"Ohio (OH)",
            L"Oklahoma (OK)",L"Oregon (OR)",L"Pennsylvania (PA)",L"Rhode Island (RI)",L"South Carolina (SC)",
            L"South Dakota (SD)",L"Tennessee (TN)",L"Texas (TX)",L"Utah (UT)",L"Vermont (VT)",
            L"Virginia (VA)",L"Washington (WA)",L"West Virginia (WV)",L"Wisconsin (WI)",L"Wyoming (WY)"
        };
        for(const auto* state:stateItems) SendMessageW(operatingStateCombo_,CB_ADDSTRING,0,(LPARAM)state);
        LoadOperatingJurisdiction();

        const wchar_t* ageItems[]={
            L"Unknown / not established",L"Self-reported minor",L"Self-reported adult",
            L"Documented minor",L"Documented adult",L"Conflicting age information"
        };
        const wchar_t* genderItems[]={
            L"Unspecified",L"Female",L"Male",L"Non-binary",L"Genderfluid",L"Other"
        };
        const wchar_t* pronounItems[]={
            L"Unspecified",L"She / Her",L"He / Him",L"They / Them",L"She / They",L"He / They",L"Other"
        };
        const wchar_t* relationshipItems[]={
            L"Unspecified",L"Single",L"Casually dating",L"Dating",L"In a relationship",
            L"Engaged",L"Married",L"Separated",L"Divorced",L"Widowed"
        };
        const wchar_t* personalityItems[]={
            L"Reserved",L"Balanced",L"Outgoing",L"Playful",L"Serious",L"Curious",
            L"Guarded",L"Confident",L"Warm",L"Analytical",L"Impulsive",L"Sarcastic",
            L"Easygoing",L"Independent"
        };
        const wchar_t* socialItems[]={
            L"Very reserved",L"Reserved",L"Quiet but responsive",L"Balanced",
            L"Social",L"Very social",L"Attention-seeking",L"Peer-approval focused"
        };
        const wchar_t* confidenceItems[]={
            L"Very low",L"Low",L"Medium",L"High",L"Very high"
        };
        const wchar_t* communicationItems[]={
            L"Age-appropriate",L"Simple",L"Average",L"Advanced"
        };
        const wchar_t* cognitiveItems[]={
            L"Simple",L"Average",L"Above average",L"Analytical"
        };
        const wchar_t* slangItems[]={
            L"None",L"Light",L"Moderate",L"Heavy"
        };
        const wchar_t* grammarItems[]={
            L"Careful",L"Casual",L"Loose",L"Very loose"
        };
        const wchar_t* typoItems[]={
            L"None",L"Rare",L"Occasional",L"Frequent"
        };
        const wchar_t* emojiItems[]={
            L"None",L"Rare",L"Occasional",L"Frequent"
        };
        const wchar_t* writingStyleItems[]={
            L"Casual",L"Friendly",L"Dry",L"Playful",L"Shy",L"Direct",L"Chatty",
            L"Reserved",L"Sarcastic",L"Enthusiastic",L"Thoughtful",L"Blunt",L"Warm",L"Minimalist"
        };
        auto fillCombo=[&](HWND combo,const wchar_t* const* items,size_t count){
            SendMessageW(combo,CB_RESETCONTENT,0,0);
            for(size_t i=0;i<count;i++) SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)items[i]);
        };
        SendMessageW(personaAgeCombo_,CB_RESETCONTENT,0,0);
        for(int age=8;age<=17;++age) {
            auto label=std::to_wstring(age);
            SendMessageW(personaAgeCombo_,CB_ADDSTRING,0,(LPARAM)label.c_str());
        }
        fillCombo(ageStateCombo_,ageItems,std::size(ageItems));
        fillCombo(personaGenderCombo_,genderItems,std::size(genderItems));
        fillCombo(personaPronounsCombo_,pronounItems,std::size(pronounItems));
        fillCombo(personaRelationshipCombo_,relationshipItems,std::size(relationshipItems));
        fillCombo(personaPersonalityCombo_,personalityItems,std::size(personalityItems));
        fillCombo(personaSocialCombo_,socialItems,std::size(socialItems));
        fillCombo(personaConfidenceCombo_,confidenceItems,std::size(confidenceItems));
        fillCombo(personaCommunicationCombo_,communicationItems,std::size(communicationItems));
        fillCombo(personaCognitiveCombo_,cognitiveItems,std::size(cognitiveItems));
        fillCombo(personaSlangCombo_,slangItems,std::size(slangItems));
        fillCombo(personaGrammarCombo_,grammarItems,std::size(grammarItems));
        fillCombo(personaTypoCombo_,typoItems,std::size(typoItems));
        fillCombo(personaEmojiCombo_,emojiItems,std::size(emojiItems));
        fillCombo(personaWritingStyleCombo_,writingStyleItems,std::size(writingStyleItems));
        const wchar_t* trainerModes[]={
            L"Behavior / Parameters",L"Correction Dataset",L"Persona LoRA",L"Foundation Fork SFT",L"Preference Training"
        };
        fillCombo(trainerModeCombo_,trainerModes,std::size(trainerModes));
        SendMessageW(trainerModeCombo_,CB_SETCURSEL,0,0);
        RefreshTrainerFoundationList();
        RefreshPersonaProfileList();
        SendMessageW(ageStateCombo_,CB_SETCURSEL,(WPARAM)static_cast<int>(simSettings_.ageState),0);
        LoadProfileEditors();
        LoadPersonaMedia();

        messagingAdapter_=sentinel::operations::CreateInMemoryMessageAdapter();
        agencyConfig_.workstationId="local-workstation";
        modelRegistry_.Load(runtime_->root/"model-registry.tsv");

        AutoInitializeLocalAi();
        ResumeOrCreateConversation();
        ApplyPageControls();
        return S_OK;
    }

    void Resize() {
        if (!target_) return;
        RECT rc{}; GetClientRect(hwnd_,&rc);
        target_->Resize(D2D1::SizeU(rc.right,rc.bottom));
        LayoutNativeControls();
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void Paint() {
        CreateResources();
        if (!target_) return;
        RECT rc{}; GetClientRect(hwnd_,&rc);
        float w=(float)rc.right,h=(float)rc.bottom;

        target_->BeginDraw();
        target_->Clear(D2D1::ColorF(0x07131F));

        target_->FillRectangle(D2D1::RectF(0,0,(float)kSidebar,h),brush_.sidebar.Get());
        target_->FillRectangle(D2D1::RectF((float)kSidebar,0,w,(float)kHeader),brush_.panel.Get());
        target_->DrawLine(D2D1::Point2F((float)kSidebar,(float)kHeader),D2D1::Point2F(w,(float)kHeader),brush_.border.Get(),1);

        textRuns_.clear();
        DrawBrand();
        DrawSidebar();
        DrawHeader(w);

        buttons_.clear();
        switch(page_) {
            case Page::Dashboard: DrawDashboard(w,h); break;
            case Page::Cases: DrawCases(w,h); break;
            case Page::Evidence: DrawEvidence(w,h); break;
            case Page::Audit: DrawAudit(w,h); break;
            case Page::Verification: DrawVerification(w,h); break;
            case Page::Simulation: DrawSimulation(w,h); break;
            case Page::Persona: DrawPersona(w,h); break;
            case Page::ModelLab: DrawModelLab(w,h); break;
            case Page::Trainer: DrawTrainer(w,h); break;
            case Page::Messaging: DrawMessaging(w,h); break;
            case Page::Supervisor: DrawSupervisor(w,h); break;
            case Page::Agency: DrawAgency(w,h); break;
            case Page::Settings: DrawSettings(w,h); break;
        }

        HRESULT hr=target_->EndDraw();
        if (hr==D2DERR_RECREATE_TARGET) { brandBitmap_.Reset(); target_.Reset(); brushesReady_=false; }
    }

    void Click(float x,float y) {
        if (x<kSidebar && y>kHeader) {
            int idx=(int)((y-kHeader-16)/44);
            if (idx>=0&&idx<13) {
                page_=(Page)idx;
                ApplyPageControls();
                if(page_==Page::Simulation && chatEdit_) {
                    SetFocus(chatEdit_);
                    SendMessageW(chatEdit_,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);
                }
                InvalidateRect(hwnd_,nullptr,FALSE);
                return;
            }
        }

        for (auto& b:buttons_) if (b.rect.Contains(x,y)) {
            if (b.id==L"new_case") CreateCase();
            else if (b.id==L"import") ImportEvidence();
            else if (b.id==L"verify") VerifySelected();
            else if (b.id==L"integrity") VerifyAudit();
            else if (b.id==L"dashboard") { page_=Page::Dashboard; ShowCaseEditors(false); ShowChatEditor(false); }
            else if (b.id==L"sim_send") SendSimulationMessage();
            else if (b.id==L"sim_suggest") GenerateSimulationSuggestion();
            else if (b.id==L"sim_reset" || b.id==L"sim_new_chat") ResetSimulation();
            else if (b.id==L"sim_previous_chat") LoadPreviousConversation();
            else if (b.id==L"sim_model") ConfigureLocalModel();
            else if (b.id==L"sim_browse_models") BrowseModels();
            else if (b.id==L"sim_install_ai") InstallOrRepairLocalAi();
            else if (b.id==L"sim_preserve") PreserveSimulationTranscript();
            else if (b.id==L"sim_export_persona_log") ExportPersonaConversationLog();
            else if (b.id==L"persona_save") SaveProfileEditors();
            else if (b.id==L"persona_load") LoadSelectedPersonaProfile();
            else if (b.id==L"persona_delete") DeleteSelectedPersonaProfile();
            else if (b.id==L"persona_generate_behavior") GenerateBehaviorFromBackground();
            else if (b.id==L"media_import") ImportPersonaMedia();
            else if (b.id==L"media_approve") TogglePersonaMediaApproval();
            else if (b.id==L"media_delete") DeletePersonaMedia();
            else if (b.id.rfind(L"media:",0)==0) {
                selectedPersonaMedia_=(int)std::stol(b.id.substr(6));
                statusText_=L"Persona media selected";
            }
            else if (b.id==L"model_register") RegisterCurrentModel();
            else if (b.id==L"model_eval") EvaluateSelectedRegistryModel();
            else if (b.id==L"model_approve") ApproveSelectedRegistryModel();
            else if (b.id==L"model_activate") ActivateSelectedRegistryModel();
            else if (b.id==L"model_rollback") RollbackRegistryModel();
            else if (b.id==L"training_stage") StageLatestTrainingExample();
            else if (b.id==L"training_approve") ReviewStagedTrainingExample(true);
            else if (b.id==L"training_reject") ReviewStagedTrainingExample(false);
            else if (b.id==L"training_export") ExportApprovedTrainingDataset();
            else if (b.id==L"rule_add_smart") AddPersonaResponseRule("smart");
            else if (b.id==L"rule_add_contains") AddPersonaResponseRule("contains");
            else if (b.id==L"rule_add_exact") AddPersonaResponseRule("exact");
            else if (b.id==L"rule_test") TestPersonaResponseRuleMatch();
            else if (b.id==L"rule_clear") ClearPersonaResponseRules();
            else if (b.id==L"rule_wording_toggle") ToggleResponseRuleWordingMode();
            else if (b.id.rfind(L"rule_delete:",0)==0)
                DeletePersonaResponseRule(std::stoll(b.id.substr(12)));
            else if (b.id==L"learning_toggle") ToggleLearningMode();
            else if (b.id.rfind(L"regmodel:",0)==0) selectedRegistryModel_=(int)std::stol(b.id.substr(9));
            else if (b.id==L"msg_queue") QueueOperatorTestMessage();
            else if (b.id==L"approval_request") RequestLatestSuggestionApproval();
            else if (b.id==L"approval_approve") ApproveFirstPending();
            else if (b.id==L"agency_toggle") ToggleAgency();
            else if (b.id==L"agency_enqueue") EnqueueAgencySnapshot();
            else if (b.id==L"jurisdiction_apply") ApplyOperatingJurisdiction();
            else if (b.id==L"check_updates") CheckForUpdates();
            else if (b.id==L"ai_diagnostics") RunAiDiagnostics();
            else if (b.id.rfind(L"copy:",0)==0) CopySimulationMessage((size_t)std::stoul(b.id.substr(5)));
            else if (b.id.rfind(L"case:",0)==0) SelectCase((size_t)std::stoul(b.id.substr(5)));
            else if (b.id.rfind(L"ev:",0)==0) SelectEvidence((size_t)std::stoul(b.id.substr(3)));
            ApplyPageControls();
            InvalidateRect(hwnd_,nullptr,FALSE);
            return;
        }
    }

    HBRUSH EditBrush() {
        static HBRUSH brush = CreateSolidBrush(RGB(15,32,48));
        return brush;
    }

    void MoveControl(HWND control,int x,int y,int w,int h,BOOL repaint=TRUE) {
        if(!control) return;
        RECT r{};
        GetWindowRect(control,&r);
        POINT tl{r.left,r.top}, br{r.right,r.bottom};
        ScreenToClient(hwnd_,&tl);
        ScreenToClient(hwnd_,&br);
        if(tl.x==x && tl.y==y && (br.x-tl.x)==w && (br.y-tl.y)==h) return;
        SetWindowPos(control,nullptr,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE|(repaint?0:SWP_NOREDRAW));
    }


    int FindComboText(HWND combo,const std::string& value) {
        const auto target=Widen(value);
        int count=(int)SendMessageW(combo,CB_GETCOUNT,0,0);
        for(int i=0;i<count;i++) {
            wchar_t buf[512]{};
            SendMessageW(combo,CB_GETLBTEXT,i,(LPARAM)buf);
            if(_wcsicmp(buf,target.c_str())==0) return i;
        }
        return 0;
    }

    std::string ComboText(HWND combo) const {
        int sel=(int)SendMessageW(combo,CB_GETCURSEL,0,0);
        if(sel==CB_ERR) return {};
        int len=(int)SendMessageW(combo,CB_GETLBTEXTLEN,sel,0);
        if(len<0) return {};
        std::wstring v((size_t)len+1,L'\0');
        SendMessageW(combo,CB_GETLBTEXT,sel,(LPARAM)v.data());
        v.resize((size_t)len);
        return Narrow(v);
    }

    void HandleSimScroll(WPARAM wp) {
        if(page_!=Page::Simulation) return;
        int maxStart=std::max(0,(int)simContext_.history.size()-kSimVisibleRows);
        switch(LOWORD(wp)) {
            case SB_LINEUP: simFirstVisible_--; break;
            case SB_LINEDOWN: simFirstVisible_++; break;
            case SB_PAGEUP: simFirstVisible_-=kSimVisibleRows; break;
            case SB_PAGEDOWN: simFirstVisible_+=kSimVisibleRows; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: simFirstVisible_=HIWORD(wp); break;
            case SB_TOP: simFirstVisible_=0; break;
            case SB_BOTTOM: simFirstVisible_=maxStart; break;
        }
        simFirstVisible_=std::clamp(simFirstVisible_,0,maxStart);
        UpdateSimulationScrollbar();
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void RightClick(float x,float y) {
        if(page_!=Page::Simulation) return;
        for(const auto& item:simMessageRects_) {
            if(item.first.Contains(x,y)) {
                CopySimulationMessage(item.second);
                statusText_=L"Message copied to clipboard";
                InvalidateRect(hwnd_,nullptr,FALSE);
                return;
            }
        }
    }

    void HandleSimWheel(short delta) {
        if(page_!=Page::Simulation) return;
        simFirstVisible_ += delta>0 ? -2 : 2;
        int maxStart=std::max(0,(int)simContext_.history.size()-kSimVisibleRows);
        simFirstVisible_=std::clamp(simFirstVisible_,0,maxStart);
        UpdateSimulationScrollbar();
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void HandleTimer(UINT_PTR id) {
        if(id==kSimEngagementTimer) {
            KillTimer(hwnd_,kSimEngagementTimer);
            if(simReplyPending_ || simBotTyping_ || simInitiativeSent_ || !model_) return;
            if(page_!=Page::Simulation) return;

            simInitiativeSent_=true;
            try {
                simContext_.variationSeed=(unsigned int)(
                    std::hash<std::string>{}(currentConversationId_) & 0xffffffffu);
                simContext_.learningMode=simSettings_.learningMode;
                const auto participantFacts=runtime_->conversationMemory.RecallParticipantFacts(
                    currentConversationId_,10);
                if(!participantFacts.empty()) {
                    if(!simContext_.recalledMemory.empty()) simContext_.recalledMemory+="\n";
                    simContext_.recalledMemory+=participantFacts;
                }
                const auto personaClaims=runtime_->conversationMemory.RecallPersonaClaims(
                    currentConversationId_,12);
                if(!personaClaims.empty()) {
                    if(!simContext_.recalledMemory.empty()) simContext_.recalledMemory+="\n";
                    simContext_.recalledMemory+=personaClaims;
                }

                auto initiative=model_->GenerateSyntheticInitiative(simContext_);
                if(!initiative.empty()) {
                    simContext_.history.push_back({
                        sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
                        initiative});
                    runtime_->conversationMemory.Append(
                        currentConversationId_,
                        sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
                        initiative);
                    LogPersonaConversationEvent(
                        "proactive_followup",{},initiative,0,0);
                    RecordLearnedPersonaNote(initiative,"proactive_persona_claim");
                    sentinel::simulation::SaveSession(
                        runtime_->root/"simulation-session.tsv",simContext_);
                    statusText_=L"Synthetic subject started a benign follow-up";
                    ScrollSimulationToBottom();
                    InvalidateRect(hwnd_,nullptr,FALSE);
                }
            } catch(const std::exception& e) {
                statusText_=L"Proactive engagement skipped: "+Widen(e.what());
            }
            return;
        }

        if(id==kSimTypingStartTimer) {
            KillTimer(hwnd_,kSimTypingStartTimer);
            if(!simReplyPending_ || simPendingMessage_.empty()) return;

            simBotTyping_=true;
            statusText_=L"Synthetic subject typing";
            ScrollSimulationToBottom();
            InvalidateRect(hwnd_,nullptr,FALSE);
            UpdateWindow(hwnd_);

            try {
                if(simPreparedFromRule_) {
                    if(simRuleResponseMode_=="persona_variation" && model_) {
                        simPreparedReply_=model_->GeneratePersonaRuleReply(
                            simRuleMeaning_,simContext_);
                        const auto policy=sentinel::simulation::EvaluateSimulationPolicy(
                            simSettings_.ageState,simPreparedReply_);
                        if(!policy.allowed)
                            simPreparedReply_=simRuleMeaning_;
                    } else if(simPreparedReply_.empty()) {
                        simPreparedReply_=simRuleMeaning_;
                    }
                } else if(model_) {
                    simPreparedReply_=model_->GenerateSyntheticReply(
                        simPendingMessage_,simContext_);
                }
            } catch(const std::exception& e) {
                simPreparedReply_=std::string("Model error: ")+e.what();
            }

            const int perChar=RandomInRange(
                std::max(10,simSettings_.persona.typingMsPerCharMin),
                std::max(simSettings_.persona.typingMsPerCharMin,simSettings_.persona.typingMsPerCharMax));
            const int startupJitter=RandomInRange(250,1200);
            int typingDelay=startupJitter+(int)simPreparedReply_.size()*perChar;
            typingDelay=std::clamp(typingDelay,600,15000);
            simLastTypingDelayMs_=typingDelay;
            SetTimer(hwnd_,kSimReplyTimer,(UINT)typingDelay,nullptr);
            return;
        }

        if(id!=kSimReplyTimer) return;
        KillTimer(hwnd_,kSimReplyTimer);
        if(!simReplyPending_) return;

        try {
            if(!simPreparedReply_.empty()) {
                const bool modelError=simPreparedReply_.rfind("Model error:",0)==0;
                auto speaker=modelError
                    ? sentinel::simulation::ChatTurn::Speaker::ModelSuggestion
                    : sentinel::simulation::ChatTurn::Speaker::SyntheticSubject;
                simContext_.history.push_back({speaker,simPreparedReply_});
                if(!modelError) {
                    runtime_->conversationMemory.Append(
                        currentConversationId_,
                        sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
                        simPreparedReply_);
                    LogPersonaConversationEvent(
                        simPreparedFromRule_?"reactive_reply_rule":"reactive_reply",
                        simPendingMessage_,simPreparedReply_,
                        simLastStartDelayMs_,simLastTypingDelayMs_);
                    if(!simPreparedFromRule_)
                        RecordLearnedPersonaNote(simPreparedReply_,"reactive_persona_claim");
                    if(simPreparedFromRule_) {
                        statusText_=simRuleResponseMode_=="persona_variation"
                            ? L"Response rule applied in persona voice"
                            : L"Exact response rule applied";
                    } else {
                        const auto source=Widen(model_?model_->Name():"No model");
                        statusText_=L"Response from "+source;
                        if(!simContext_.recalledMemory.empty())
                            statusText_+=L" | prior-conversation context used";
                    }
                } else {
                    statusText_=L"Model request failed";
                }
            }
        } catch(const std::exception& e) {
            simContext_.history.push_back({sentinel::simulation::ChatTurn::Speaker::ModelSuggestion,
                std::string("Model error: ")+e.what()});
            statusText_=L"Model request failed";
        }

        simBotTyping_=false;
        simReplyPending_=false;
        simPendingMessage_.clear();
        simPreparedReply_.clear();
        simPreparedFromRule_=false;
        simRuleMeaning_.clear();
        simRuleResponseMode_.clear();
        sentinel::simulation::SaveSession(runtime_->root/"simulation-session.tsv",simContext_);
        // One benign proactive nudge is allowed in Simulation after 60 seconds
        // of silence. Live-channel automation remains governed by the operation
        // rules profile and is not enabled by this simulation timer.
        if(!simInitiativeSent_) SetTimer(hwnd_,kSimEngagementTimer,60000,nullptr);
        ScrollSimulationToBottom();
        SetFocus(chatEdit_);
        SendMessageW(chatEdit_,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

private:
    struct Button { RectF rect; std::wstring id; };

    HWND hwnd_{},caseNumberEdit_{},caseTitleEdit_{},chatEdit_{},modelEndpointEdit_{},modelNameEdit_{},modelCombo_{},simScroll_{};
    HWND personaNameEdit_{},personaAgeCombo_{},personaLocationEdit_{},personaInterestsEdit_{},personaStyleEdit_{};
    HWND personaWritingStyleCombo_{},personaProfileCombo_{};
    HWND personaOccupationEdit_{},personaEducationEdit_{},personaFamilyEdit_{},personaBackgroundEdit_{};
    HWND personaGenderCombo_{},personaPronounsCombo_{},personaRelationshipCombo_{},personaPersonalityCombo_{},personaSocialCombo_{},personaConfidenceCombo_{};
    HWND scenarioNameEdit_{},scenarioObjectiveEdit_{},scenarioSeedEdit_{},minDelayEdit_{},maxDelayEdit_{},ageStateCombo_{};
    HWND agencyEndpointEdit_{},agencyIdEdit_{},operatingStateCombo_{};
    HWND personaCommunicationCombo_{},personaCognitiveCombo_{},personaSlangCombo_{},personaGrammarCombo_{},personaTypoCombo_{},personaEmojiCombo_{};
    HWND responseRuleTriggerEdit_{},responseRuleResponseEdit_{};
    HWND trainerModeCombo_{},trainerFoundationCombo_{};
    HWND trainerForkNameEdit_{},trainerBasePathEdit_{},trainerLoraNameEdit_{},trainerLoraPathEdit_{},trainerDatasetEdit_{},trainerOutputEdit_{},trainerInstructionEdit_{};
    std::unique_ptr<Runtime> runtime_;
    Page page_{Page::Dashboard};
    std::vector<sentinel::CaseRecord> cases_;
    std::vector<sentinel::EvidenceSummary> evidence_;
    size_t selectedCase_{0},selectedEvidence_{0};
    std::wstring statusText_=L"System Operational";
    std::wstring lastVerify_=L"No verification performed yet";
    std::unique_ptr<sentinel::simulation::IModelAdapter> model_;
    sentinel::simulation::ModelContext simContext_;
    std::wstring simSuggestion_=L"No suggestion generated yet";
    std::wstring modelStatus_=L"Built-in test model";
    std::string currentConversationId_;
    std::wstring currentConversationTitle_=L"New conversation";
    int archiveCursor_{0};
    int simFirstVisible_{0};
    bool simBotTyping_{false};
    bool simReplyPending_{false};
    bool simInitiativeSent_{false};
    std::string simPendingMessage_;
    std::string simPreparedReply_;
    bool simPreparedFromRule_{false};
    std::string simRuleMeaning_;
    std::string simRuleResponseMode_;
    int simLastStartDelayMs_{0};
    int simLastTypingDelayMs_{0};
    std::vector<std::pair<RectF,size_t>> simMessageRects_;
    sentinel::simulation::SimulationSettings simSettings_;
    std::unique_ptr<sentinel::operations::IMessageAdapter> messagingAdapter_;
    std::vector<sentinel::operations::ApprovalRequest> approvals_;
    sentinel::simulation::ModelRegistry modelRegistry_;
    int selectedRegistryModel_{-1};
    std::string selectedTrainingReviewId_;
    sentinel::simulation::ResponseEvaluation lastEvaluation_;
    sentinel::agency::AgencyServerConfig agencyConfig_;
    sentinel::agency::AgencySyncQueue agencyQueue_;
    std::wstring policyStatus_=L"Policy ready";
    std::wstring updateStatus_=L"Updates not checked";
    std::wstring aiDiagnostics_=L"Not run";
    std::wstring jurisdictionStatus_=L"No operating jurisdiction selected";
    std::string operatingStateCode_;
    std::vector<PersonaMediaItem> personaMedia_;
    int selectedPersonaMedia_{-1};
    std::vector<sentinel::simulation::ModelFoundation> trainerFoundations_;
    std::wstring trainerRuntimeStatus_=L"No persona LoRA active";
    bool responseRuleExactWording_{false};

    HFONT chatFont_{};
    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<IDWriteFactory> writeFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;
    ComPtr<ID2D1Bitmap> brandBitmap_;
    ComPtr<IDWriteTextFormat> titleFmt_,h1Fmt_,bodyFmt_,smallFmt_,tinyFmt_,bigFmt_;
    BrushSet brush_;
    bool brushesReady_{false};
    std::vector<Button> buttons_;
    std::vector<SelectableTextRun> textRuns_;
    int selectedTextRun_{-1};
    size_t selectionAnchor_{0};
    size_t selectionActive_{0};
    bool selectionMouseDown_{false};
    bool selectionDragging_{false};
    D2D1_POINT_2F selectionStartPoint_{0,0};

    ComPtr<ID2D1Bitmap> LoadD2DBitmap(const std::filesystem::path& path) {
        ComPtr<ID2D1Bitmap> bitmap;
        if(!target_ || !wicFactory_ || !std::filesystem::exists(path)) return bitmap;

        ComPtr<IWICBitmapDecoder> decoder;
        if(FAILED(wicFactory_->CreateDecoderFromFilename(
                path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)) || !decoder)
            return bitmap;

        ComPtr<IWICBitmapFrameDecode> frame;
        if(FAILED(decoder->GetFrame(0,&frame)) || !frame) return bitmap;

        ComPtr<IWICFormatConverter> converter;
        if(FAILED(wicFactory_->CreateFormatConverter(&converter)) || !converter) return bitmap;
        if(FAILED(converter->Initialize(
                frame.Get(),GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom)))
            return bitmap;

        target_->CreateBitmapFromWicBitmap(converter.Get(),nullptr,&bitmap);
        return bitmap;
    }

    void CreateResources() {
        if (!target_) {
            RECT rc{}; GetClientRect(hwnd_,&rc);
            factory_->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(hwnd_,D2D1::SizeU(std::max(1L,rc.right),std::max(1L,rc.bottom))),
                &target_);
        }
        if(target_ && !brandBitmap_) {
            brandBitmap_=LoadD2DBitmap(ExeDir()/L"assets"/L"SARA-Logo.png");
        }
        if (!titleFmt_) {
            writeFactory_->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,30,L"en-us",&titleFmt_);
            writeFactory_->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,24,L"en-us",&h1Fmt_);
            writeFactory_->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,15,L"en-us",&bodyFmt_);
            writeFactory_->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,12,L"en-us",&smallFmt_);
            writeFactory_->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,9,L"en-us",&tinyFmt_);
            writeFactory_->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,34,L"en-us",&bigFmt_);
        }
        if (!brushesReady_ && target_) {
            auto mk=[&](UINT rgb,float a=1.0f){ ComPtr<ID2D1SolidColorBrush> b; target_->CreateSolidColorBrush(D2D1::ColorF(rgb,a),&b); return b; };
            brush_.bg=mk(0x07131F); brush_.sidebar=mk(0x0A1826); brush_.panel=mk(0x0F2030);
            brush_.panel2=mk(0x12283A); brush_.border=mk(0x24445C); brush_.text=mk(0xEEF6FF);
            brush_.muted=mk(0x93A9BC); brush_.blue=mk(0x1597F6); brush_.cyan=mk(0x22C7FF);
            brush_.green=mk(0x38E89A); brush_.yellow=mk(0xF6C84A); brush_.red=mk(0xFF5B66);
            brush_.selection=mk(0x2D7FF9,0.55f);
            brushesReady_=true;
        }
    }

    void LoadData() {
        cases_=runtime_->cases.ListCases();
        if (selectedCase_>=cases_.size()) selectedCase_=0;
        evidence_.clear();
        if (!cases_.empty()) {
            try { evidence_=runtime_->Evidence(cases_[selectedCase_].id); } catch (...) {}
        }
        if (selectedEvidence_>=evidence_.size()) selectedEvidence_=0;
    }

    ComPtr<IDWriteTextLayout> CreateSelectableLayout(const SelectableTextRun& run) const {
        ComPtr<IDWriteTextLayout> layout;
        if(FAILED(writeFactory_->CreateTextLayout(
                run.text.c_str(),(UINT32)run.text.size(),run.format,
                std::max(1.0f,run.rect.r-run.rect.l),
                std::max(1.0f,run.rect.b-run.rect.t),&layout)) || !layout)
            return {};
        if(run.noWrap) layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        layout->SetTextAlignment(run.align);
        if(run.paragraphCenter) layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        return layout;
    }

    int RegisterSelectableText(
        const std::wstring& s,float x,float y,float w,float h,IDWriteTextFormat* fmt,
        DWRITE_TEXT_ALIGNMENT align,bool noWrap,bool paragraphCenter)
    {
        textRuns_.push_back({s,{x,y,x+w,y+h},fmt,align,noWrap,paragraphCenter});
        return (int)textRuns_.size()-1;
    }

    void DrawSelectionForRun(int runIndex,IDWriteTextLayout* layout,float x,float y) {
        if(runIndex!=selectedTextRun_ || !layout) return;
        const size_t a=std::min(selectionAnchor_,selectionActive_);
        const size_t b=std::max(selectionAnchor_,selectionActive_);
        if(a>=b) return;

        UINT32 actual=0;
        layout->HitTestTextRange(
            (UINT32)std::min(a,(size_t)UINT32_MAX),
            (UINT32)std::min(b-a,(size_t)UINT32_MAX),
            x,y,nullptr,0,&actual);
        if(!actual) return;
        std::vector<DWRITE_HIT_TEST_METRICS> metrics(actual);
        if(FAILED(layout->HitTestTextRange(
                (UINT32)a,(UINT32)(b-a),x,y,metrics.data(),actual,&actual))) return;
        for(UINT32 i=0;i<actual;i++) {
            const auto& m=metrics[i];
            target_->FillRectangle(
                D2D1::RectF(m.left,m.top,m.left+m.width,m.top+m.height),
                brush_.selection.Get());
        }
    }

    int FindSelectableRun(float x,float y) const {
        for(int i=(int)textRuns_.size()-1;i>=0;--i)
            if(textRuns_[(size_t)i].rect.Contains(x,y) && !textRuns_[(size_t)i].text.empty()) return i;
        return -1;
    }

    size_t HitTestTextPosition(const SelectableTextRun& run,float x,float y) const {
        auto layout=CreateSelectableLayout(run);
        if(!layout) return 0;
        BOOL trailing=FALSE,inside=FALSE;
        DWRITE_HIT_TEST_METRICS metrics{};
        const float localX=x-run.rect.l;
        const float localY=y-run.rect.t;
        if(FAILED(layout->HitTestPoint(localX,localY,&trailing,&inside,&metrics))) return 0;
        size_t pos=(size_t)metrics.textPosition + (trailing?metrics.length:0);
        return std::min(pos,run.text.size());
    }

    void Text(const std::wstring& s,float x,float y,float w,float h,IDWriteTextFormat* fmt,ID2D1Brush* br) {
        if(w<=1 || h<=1) return;
        const int runIndex=RegisterSelectableText(s,x,y,w,h,fmt,DWRITE_TEXT_ALIGNMENT_LEADING,false,false);
        auto layout=CreateSelectableLayout(textRuns_[(size_t)runIndex]);
        if(!layout) return;
        DrawSelectionForRun(runIndex,layout.Get(),x,y);
        target_->DrawTextLayout(D2D1::Point2F(x,y),layout.Get(),br,D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void TextLine(const std::wstring& s,float x,float y,float w,float h,IDWriteTextFormat* fmt,ID2D1Brush* br,
                  DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING) {
        if(w<=1 || h<=1) return;
        const int runIndex=RegisterSelectableText(s,x,y,w,h,fmt,align,true,true);
        auto layout=CreateSelectableLayout(textRuns_[(size_t)runIndex]);
        if(!layout) return;
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};
        ComPtr<IDWriteInlineObject> sign;
        if(SUCCEEDED(writeFactory_->CreateEllipsisTrimmingSign(fmt,&sign)))
            layout->SetTrimming(&trim,sign.Get());
        DrawSelectionForRun(runIndex,layout.Get(),x,y);
        target_->DrawTextLayout(D2D1::Point2F(x,y),layout.Get(),br,D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void Rounded(float x,float y,float w,float h,ID2D1Brush* fill,ID2D1Brush* stroke=nullptr,float radius=8) {
        auto rr=D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),radius,radius);
        target_->FillRoundedRectangle(rr,fill);
        if (stroke) target_->DrawRoundedRectangle(rr,stroke,1);
    }

    void Badge(const std::wstring& s,float x,float y,ID2D1Brush* color,float width=76) {
        Rounded(x,y,width,24,brush_.panel2.Get(),color,12);
        TextLine(s,x+8,y+2,width-16,20,smallFmt_.Get(),color,DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void AddButton(const std::wstring& id,const std::wstring& label,float x,float y,float w,float h,bool primary=false) {
        Rounded(x,y,w,h,primary?brush_.blue.Get():brush_.panel2.Get(),primary?brush_.cyan.Get():brush_.border.Get(),7);
        TextLine(label,x+10,y+1,w-20,h-2,smallFmt_.Get(),brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
        buttons_.push_back({{x,y,x+w,y+h},id});
    }

    void StatusDot(float x,float y,float r,ID2D1Brush* color) {
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),r,r),color);
    }

    void DrawShield(float x,float y,float s,ID2D1Brush* stroke,ID2D1Brush* fill=nullptr,bool check=false) {
        ComPtr<ID2D1PathGeometry> geo;
        factory_->CreatePathGeometry(&geo);
        ComPtr<ID2D1GeometrySink> sink;
        geo->Open(&sink);
        sink->BeginFigure(D2D1::Point2F(x+s*0.50f,y),fill?D2D1_FIGURE_BEGIN_FILLED:D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(x+s*0.87f,y+s*0.14f));
        sink->AddLine(D2D1::Point2F(x+s*0.81f,y+s*0.58f));
        sink->AddBezier(D2D1::BezierSegment(
            D2D1::Point2F(x+s*0.75f,y+s*0.75f),
            D2D1::Point2F(x+s*0.61f,y+s*0.89f),
            D2D1::Point2F(x+s*0.50f,y+s*0.96f)));
        sink->AddBezier(D2D1::BezierSegment(
            D2D1::Point2F(x+s*0.39f,y+s*0.89f),
            D2D1::Point2F(x+s*0.25f,y+s*0.75f),
            D2D1::Point2F(x+s*0.19f,y+s*0.58f)));
        sink->AddLine(D2D1::Point2F(x+s*0.13f,y+s*0.14f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        if (fill) target_->FillGeometry(geo.Get(),fill);
        target_->DrawGeometry(geo.Get(),stroke,std::max(2.0f,s*0.07f));
        if (check) {
            target_->DrawLine(D2D1::Point2F(x+s*0.30f,y+s*0.48f),D2D1::Point2F(x+s*0.44f,y+s*0.62f),stroke,std::max(2.0f,s*0.07f));
            target_->DrawLine(D2D1::Point2F(x+s*0.44f,y+s*0.62f),D2D1::Point2F(x+s*0.70f,y+s*0.34f),stroke,std::max(2.0f,s*0.07f));
        }
    }

    void DrawIcon(IconKind kind,float x,float y,float s,ID2D1Brush* color) {
        const float t=std::max(1.6f,s*0.075f);
        switch(kind) {
            case IconKind::Shield:
                DrawShield(x,y,s,color,nullptr,false);
                break;
            case IconKind::Home:
                target_->DrawLine(D2D1::Point2F(x+s*0.10f,y+s*0.48f),D2D1::Point2F(x+s*0.50f,y+s*0.12f),color,t);
                target_->DrawLine(D2D1::Point2F(x+s*0.50f,y+s*0.12f),D2D1::Point2F(x+s*0.90f,y+s*0.48f),color,t);
                target_->DrawRectangle(D2D1::RectF(x+s*0.22f,y+s*0.44f,x+s*0.78f,y+s*0.88f),color,t);
                target_->DrawRectangle(D2D1::RectF(x+s*0.46f,y+s*0.62f,x+s*0.58f,y+s*0.88f),color,t);
                break;
            case IconKind::Folder: {
                ComPtr<ID2D1PathGeometry> geo; factory_->CreatePathGeometry(&geo);
                ComPtr<ID2D1GeometrySink> sink; geo->Open(&sink);
                sink->BeginFigure(D2D1::Point2F(x+s*0.08f,y+s*0.30f),D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(D2D1::Point2F(x+s*0.36f,y+s*0.30f));
                sink->AddLine(D2D1::Point2F(x+s*0.45f,y+s*0.42f));
                sink->AddLine(D2D1::Point2F(x+s*0.90f,y+s*0.42f));
                sink->AddLine(D2D1::Point2F(x+s*0.84f,y+s*0.84f));
                sink->AddLine(D2D1::Point2F(x+s*0.10f,y+s*0.84f));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED); sink->Close();
                target_->DrawGeometry(geo.Get(),color,t);
                break;
            }
            case IconKind::Database:
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.50f,y+s*0.26f),s*0.34f,s*0.14f),color,t);
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.50f,y+s*0.52f),s*0.34f,s*0.14f),color,t);
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.50f,y+s*0.76f),s*0.34f,s*0.14f),color,t);
                target_->DrawLine(D2D1::Point2F(x+s*0.16f,y+s*0.26f),D2D1::Point2F(x+s*0.16f,y+s*0.76f),color,t);
                target_->DrawLine(D2D1::Point2F(x+s*0.84f,y+s*0.26f),D2D1::Point2F(x+s*0.84f,y+s*0.76f),color,t);
                break;
            case IconKind::Document:
                target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x+s*0.22f,y+s*0.10f,x+s*0.78f,y+s*0.90f),2,2),color,t);
                for(int i=0;i<3;i++) target_->DrawLine(D2D1::Point2F(x+s*0.34f,y+s*(0.40f+i*0.14f)),D2D1::Point2F(x+s*0.68f,y+s*(0.40f+i*0.14f)),color,t*0.75f);
                break;
            case IconKind::Check:
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.50f,y+s*0.50f),s*0.38f,s*0.38f),color,t);
                target_->DrawLine(D2D1::Point2F(x+s*0.30f,y+s*0.50f),D2D1::Point2F(x+s*0.44f,y+s*0.64f),color,t);
                target_->DrawLine(D2D1::Point2F(x+s*0.44f,y+s*0.64f),D2D1::Point2F(x+s*0.72f,y+s*0.34f),color,t);
                break;
            case IconKind::Gear:
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.50f,y+s*0.50f),s*0.24f,s*0.24f),color,t);
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.50f,y+s*0.50f),s*0.08f,s*0.08f),color,t);
                for(int i=0;i<8;i++){ float a=3.1415926f*2*i/8.0f; float dx=cosf(a),dy=sinf(a);
                    target_->DrawLine(D2D1::Point2F(x+s*(0.50f+dx*0.29f),y+s*(0.50f+dy*0.29f)),D2D1::Point2F(x+s*(0.50f+dx*0.40f),y+s*(0.50f+dy*0.40f)),color,t);
                }
                break;
            case IconKind::Search:
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.42f,y+s*0.42f),s*0.26f,s*0.26f),color,t);
                target_->DrawLine(D2D1::Point2F(x+s*0.60f,y+s*0.60f),D2D1::Point2F(x+s*0.88f,y+s*0.88f),color,t);
                break;
            case IconKind::Plus:
                target_->DrawLine(D2D1::Point2F(x+s*0.50f,y+s*0.18f),D2D1::Point2F(x+s*0.50f,y+s*0.82f),color,t);
                target_->DrawLine(D2D1::Point2F(x+s*0.18f,y+s*0.50f),D2D1::Point2F(x+s*0.82f,y+s*0.50f),color,t);
                break;
            case IconKind::Chain:
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.30f,y+s*0.50f),s*0.16f,s*0.16f),color,t);
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.70f,y+s*0.50f),s*0.16f,s*0.16f),color,t);
                target_->DrawLine(D2D1::Point2F(x+s*0.44f,y+s*0.50f),D2D1::Point2F(x+s*0.56f,y+s*0.50f),color,t);
                break;
            case IconKind::Lock:
                target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x+s*0.22f,y+s*0.42f,x+s*0.78f,y+s*0.86f),4,4),color,t);
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.50f,y+s*0.42f),s*0.20f,s*0.24f),color,t);
                target_->FillRectangle(D2D1::RectF(x+s*0.26f,y+s*0.42f,x+s*0.74f,y+s*0.55f),brush_.panel.Get());
                break;
            case IconKind::Chat:
                target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x+s*0.12f,y+s*0.18f,x+s*0.88f,y+s*0.72f),5,5),color,t);
                target_->DrawLine(D2D1::Point2F(x+s*0.30f,y+s*0.72f),D2D1::Point2F(x+s*0.23f,y+s*0.90f),color,t);
                target_->DrawLine(D2D1::Point2F(x+s*0.23f,y+s*0.90f),D2D1::Point2F(x+s*0.46f,y+s*0.73f),color,t);
                break;
        }
    }

    IconKind NavIcon(int i) const {
        static const IconKind icons[]={
            IconKind::Home,IconKind::Folder,IconKind::Database,IconKind::Document,IconKind::Shield,
            IconKind::Chat,IconKind::Document,IconKind::Database,IconKind::Gear,IconKind::Chat,IconKind::Shield,IconKind::Database,IconKind::Gear
        };
        return icons[std::clamp(i,0,12)];
    }

    void DrawBrand() {
        if(brandBitmap_) {
            const auto sz=brandBitmap_->GetSize();
            const float h=64.0f;
            const float w=h*(sz.width/std::max(1.0f,sz.height));
            target_->DrawBitmap(
                brandBitmap_.Get(),D2D1::RectF(10,7,10+w,7+h),1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        Text(L"SARA",76,10,132,36,titleFmt_.Get(),brush_.text.Get());
        Text(L"SYNTHETIC ADAPTIVE",77,43,136,14,tinyFmt_.Get(),brush_.cyan.Get());
        Text(L"RESPONSE AGENT",77,56,136,14,tinyFmt_.Get(),brush_.muted.Get());
    }

    void DrawSidebar() {
        static const wchar_t* names[]={
            L"Dashboard",L"Cases",L"Evidence",L"Audit Log",L"Verification",L"Simulation Lab",
            L"Persona & Policy",L"Model Lab",L"Trainer",L"Messaging",L"Supervisor",L"Agency Server",L"Settings"
        };
        constexpr float rowH=44.0f;
        for (int i=0;i<13;i++) {
            float y=(float)kHeader+16+i*rowH;
            if ((int)page_==i) {
                target_->FillRectangle(D2D1::RectF(0,y-4,(float)kSidebar,y+36),brush_.panel2.Get());
                target_->FillRectangle(D2D1::RectF(0,y-4,4,y+36),brush_.cyan.Get());
                Rounded(20,y,36,30,brush_.sidebar.Get(),brush_.border.Get(),8);
            }
            DrawIcon(NavIcon(i),26,y+3,22,((int)page_==i)?brush_.cyan.Get():brush_.muted.Get());
            TextLine(names[i],66,y+3,145,26,smallFmt_.Get(),((int)page_==i)?brush_.cyan.Get():brush_.text.Get());
        }
        Text(L"SARA v1.0.15",24,674,170,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"Secure Local Mode",24,696,170,20,smallFmt_.Get(),brush_.green.Get());
    }

    void DrawHeader(float w) {
        Text(L"EVIDENCE",kSidebar+28,25,80,20,tinyFmt_.Get(),brush_.muted.Get());
        Text(L"|",kSidebar+104,24,12,20,tinyFmt_.Get(),brush_.border.Get());
        Text(L"INTEGRITY",kSidebar+118,25,80,20,tinyFmt_.Get(),brush_.muted.Get());
        Text(L"|",kSidebar+195,24,12,20,tinyFmt_.Get(),brush_.border.Get());
        Text(L"JUSTICE",kSidebar+208,25,70,20,tinyFmt_.Get(),brush_.muted.Get());

        Rounded(w-405,16,255,42,brush_.sidebar.Get(),brush_.border.Get(),8);
        DrawIcon(IconKind::Search,w-390,27,18,brush_.muted.Get());
        Text(L"Search cases, evidence, hashes...",w-366,27,190,22,smallFmt_.Get(),brush_.muted.Get());

        Rounded(w-132,18,36,36,brush_.panel2.Get(),brush_.border.Get(),18);
        Text(L"JD",w-122,26,22,20,smallFmt_.Get(),brush_.text.Get());
        Text(L"Local",w-84,20,58,18,smallFmt_.Get(),brush_.text.Get());
        Text(L"Investigator",w-84,36,76,18,tinyFmt_.Get(),brush_.muted.Get());

        StatusDot(w-174,67,4,brush_.green.Get());
        Text(statusText_,w-163,58,153,18,tinyFmt_.Get(),brush_.green.Get());
    }

    void PageTitle(const std::wstring& title,const std::wstring& sub) {
        TextLine(title,kSidebar+28,kHeader+18,440,42,titleFmt_.Get(),brush_.text.Get());
        TextLine(sub,kSidebar+30,kHeader+56,760,24,bodyFmt_.Get(),brush_.muted.Get());
    }

    void Metric(float x,float y,float w,const std::wstring& label,const std::wstring& value,const std::wstring& sub,ID2D1Brush* accent,IconKind icon) {
        Rounded(x,y,w,112,brush_.panel.Get(),brush_.border.Get(),10);
        target_->DrawLine(D2D1::Point2F(x+12,y+1),D2D1::Point2F(x+w-12,y+1),accent,1.6f);
        Rounded(x+16,y+17,46,46,brush_.panel2.Get(),nullptr,12);
        DrawIcon(icon,x+26,y+27,26,accent);
        Text(label,x+76,y+15,w-94,22,bodyFmt_.Get(),brush_.muted.Get());
        Text(value,x+76,y+39,w-94,42,bigFmt_.Get(),brush_.text.Get());
        Text(sub,x+76,y+80,w-124,18,tinyFmt_.Get(),brush_.muted.Get());
        for(int i=0;i<5;i++) {
            float bh=8.0f+i*4.2f;
            target_->FillRectangle(D2D1::RectF(x+w-54+i*8,y+91-bh,x+w-49+i*8,y+91),accent);
        }
    }

    void DrawDashboard(float w,float h) {
        PageTitle(L"Dashboard",L"Overview of cases, evidence, and system integrity");
        float x=kSidebar+28,y=kHeader+96,g=14;
        float card=(w-x-28-g*3)/4;
        Metric(x,y,card,L"Open Cases",std::to_wstring(runtime_->OpenCaseCount()),L"Active investigations",brush_.cyan.Get(),IconKind::Folder);
        Metric(x+(card+g),y,card,L"Evidence Items",std::to_wstring(runtime_->EvidenceCount()),L"Encrypted local objects",brush_.blue.Get(),IconKind::Database);
        Metric(x+2*(card+g),y,card,L"Audit Records",std::to_wstring(runtime_->AuditCount()),L"Hash-linked events",brush_.cyan.Get(),IconKind::Chain);
        Metric(x+3*(card+g),y,card,L"Secure Store",L"Online",L"DPAPI protected",brush_.green.Get(),IconKind::Lock);

        float panelY=y+130;
        float left=(w-x-42)*0.58f;
        Rounded(x,panelY,left,310,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Evidence Activity",x+18,panelY+15,260,28,h1Fmt_.Get(),brush_.text.Get());
        const float chartLeft=x+54, chartRight=x+left-22, chartTop=panelY+58, chartBottom=panelY+254;
        for(int gy=0;gy<5;gy++) {
            float yy=chartTop+(chartBottom-chartTop)*gy/4.0f;
            target_->DrawLine(D2D1::Point2F(chartLeft,yy),D2D1::Point2F(chartRight,yy),brush_.border.Get(),0.7f);
        }
        if (evidence_.empty()) {
            DrawIcon(IconKind::Database,x+left*0.50f-22,panelY+112,44,brush_.muted.Get());
            Text(L"No evidence activity yet",x+left*0.50f-90,panelY+166,180,24,bodyFmt_.Get(),brush_.muted.Get());
            Text(L"Imported evidence will appear here.",x+left*0.50f-110,panelY+192,220,20,smallFmt_.Get(),brush_.muted.Get());
        } else {
            const int bars=std::min<int>(8,(int)evidence_.size());
            for(int i=0;i<bars;i++) {
                float bh=48.0f+22.0f*(i%5);
                float bx=chartLeft+24+i*((chartRight-chartLeft-60)/8.0f);
                target_->FillRectangle(D2D1::RectF(bx,chartBottom-bh,bx+28,chartBottom),brush_.blue.Get());
            }
        }
        static const wchar_t* labels[]={L"Mon",L"Tue",L"Wed",L"Thu",L"Fri",L"Sat",L"Sun",L"Now"};
        for(int i=0;i<8;i++) {
            float bx=chartLeft+12+i*((chartRight-chartLeft-28)/8.0f);
            Text(labels[i],bx,chartBottom+12,42,18,tinyFmt_.Get(),brush_.muted.Get());
        }

        float rx=x+left+14,rw=w-rx-28;
        Rounded(rx,panelY,rw,310,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Recent Activity",rx+18,panelY+15,rw-36,28,h1Fmt_.Get(),brush_.text.Get());
        std::vector<std::wstring> rows;
        sqlite3_stmt* recent{};
        if(sqlite3_prepare_v2(runtime_->db.Handle(),"SELECT action,target_type,target_id FROM audit_records ORDER BY sequence DESC LIMIT 4",-1,&recent,nullptr)==SQLITE_OK) {
            while(sqlite3_step(recent)==SQLITE_ROW) {
                int action=sqlite3_column_int(recent,0);
                const char* type=(const char*)sqlite3_column_text(recent,1);
                const char* id=(const char*)sqlite3_column_text(recent,2);
                std::wstring row=AuditActionName(action)+L" | "+Widen(type?type:"");
                if(id && *id) row+=L" | "+Widen(id);
                rows.push_back(row);
            }
        }
        sqlite3_finalize(recent);
        if(rows.empty()) rows.push_back(L"Secure local store initialized");
        for (size_t i=0;i<rows.size();++i) {
            float yy=panelY+58+(float)i*52;
            DrawIcon(i==0?IconKind::Document:IconKind::Check,rx+18,yy-1,20,i==0?brush_.cyan.Get():brush_.green.Get());
            Text(rows[i],rx+50,yy,rw-66,22,bodyFmt_.Get(),brush_.text.Get());
            target_->DrawLine(D2D1::Point2F(rx+18,yy+34),D2D1::Point2F(rx+rw-18,yy+34),brush_.border.Get(),1);
        }

        float bottom=panelY+326;
        Rounded(x,bottom,left,150,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"System Integrity",x+18,bottom+15,240,26,h1Fmt_.Get(),brush_.text.Get());
        StatusDot(x+29,bottom+69,5,brush_.green.Get());
        Text(L"Secure Store",x+43,bottom+58,160,22,bodyFmt_.Get(),brush_.green.Get());
        StatusDot(x+227,bottom+69,5,runtime_->audit.VerifyChain()?brush_.green.Get():brush_.red.Get());
        Text(L"Audit Chain",x+241,bottom+58,160,22,bodyFmt_.Get(),runtime_->audit.VerifyChain()?brush_.green.Get():brush_.red.Get());
        StatusDot(x+29,bottom+103,5,brush_.green.Get());
        Text(L"AES-256-GCM",x+43,bottom+92,160,22,bodyFmt_.Get(),brush_.green.Get());
        StatusDot(x+227,bottom+103,5,brush_.green.Get());
        Text(L"DPAPI Master Key",x+241,bottom+92,180,22,bodyFmt_.Get(),brush_.green.Get());

        Rounded(rx,bottom,rw,150,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Quick Actions",rx+18,bottom+15,200,26,h1Fmt_.Get(),brush_.text.Get());
        float gap=10.0f;
        float bw=(rw-42-gap*3)/4.0f;
        const float by=bottom+54, bh=72;
        Rounded(rx+12,by,bw,bh,brush_.blue.Get(),brush_.cyan.Get(),8);
        DrawIcon(IconKind::Folder,rx+24,by+14,26,brush_.text.Get());
        Text(L"New Case",rx+56,by+16,bw-64,22,bodyFmt_.Get(),brush_.text.Get());
        Text(L"Create investigation",rx+56,by+40,bw-64,18,tinyFmt_.Get(),brush_.text.Get());
        buttons_.push_back({{rx+12,by,rx+12+bw,by+bh},L"new_case"});

        float q2=rx+12+bw+gap;
        Rounded(q2,by,bw,bh,brush_.panel2.Get(),brush_.border.Get(),8);
        DrawIcon(IconKind::Database,q2+12,by+14,26,brush_.cyan.Get());
        Text(L"Import",q2+44,by+16,bw-52,22,bodyFmt_.Get(),brush_.text.Get());
        Text(L"Add evidence",q2+44,by+40,bw-52,18,tinyFmt_.Get(),brush_.muted.Get());
        buttons_.push_back({{q2,by,q2+bw,by+bh},L"import"});

        float q3=q2+bw+gap;
        Rounded(q3,by,bw,bh,brush_.panel2.Get(),brush_.border.Get(),8);
        DrawIcon(IconKind::Shield,q3+12,by+14,26,brush_.green.Get());
        Text(L"Verify",q3+44,by+16,bw-52,22,bodyFmt_.Get(),brush_.text.Get());
        Text(L"Authenticate file",q3+44,by+40,bw-52,18,tinyFmt_.Get(),brush_.muted.Get());
        buttons_.push_back({{q3,by,q3+bw,by+bh},L"verify"});

        float q4=q3+bw+gap;
        Rounded(q4,by,bw,bh,brush_.panel2.Get(),brush_.border.Get(),8);
        DrawIcon(IconKind::Check,q4+12,by+14,26,brush_.green.Get());
        Text(L"Integrity",q4+44,by+16,bw-52,22,bodyFmt_.Get(),brush_.text.Get());
        Text(L"Verify audit chain",q4+44,by+40,bw-52,18,tinyFmt_.Get(),brush_.muted.Get());
        buttons_.push_back({{q4,by,q4+bw,by+bh},L"integrity"});
    }

    void DrawCases(float w,float h) {
        PageTitle(L"Cases",L"Manage investigations, review evidence, and track case progress");
        float x=kSidebar+28,y=kHeader+102;
        Text(L"Case Number",x,y-24,130,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"Title",x+180,y-24,130,20,smallFmt_.Get(),brush_.muted.Get());
        AddButton(L"new_case",L"+ Create Case",x+500,y,140,34,true);

        float tableY=y+54;
        Rounded(x,tableY,w-x-28,360,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Case ID",x+16,tableY+12,180,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"Case Number",x+220,tableY+12,130,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"Title",x+370,tableY+12,360,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"Status",w-190,tableY+12,100,20,smallFmt_.Get(),brush_.muted.Get());

        size_t maxRows=std::min<size_t>(cases_.size(),7);
        for (size_t i=0;i<maxRows;i++) {
            float yy=tableY+40+(float)i*44;
            if (i==selectedCase_) target_->FillRectangle(D2D1::RectF(x+2,yy-3,w-30,yy+36),brush_.panel2.Get());
            Text(Widen(cases_[i].id.ToString()).substr(0,18)+L"...",x+16,yy,185,22,smallFmt_.Get(),brush_.text.Get());
            Text(Widen(cases_[i].caseNumber),x+220,yy,135,22,bodyFmt_.Get(),brush_.text.Get());
            Text(Widen(cases_[i].title),x+370,yy,w-x-580,22,bodyFmt_.Get(),brush_.text.Get());
            auto st=cases_[i].status==sentinel::CaseStatus::Open?L"Open":cases_[i].status==sentinel::CaseStatus::Closed?L"Closed":L"Other";
            Badge(st,w-190,yy-2,cases_[i].status==sentinel::CaseStatus::Open?brush_.green.Get():brush_.cyan.Get(),80);
            buttons_.push_back({{x+2,yy-3,w-30,yy+36},L"case:"+std::to_wstring(i)});
        }

        float detail=tableY+376;
        Rounded(x,detail,w-x-28,180,brush_.panel.Get(),brush_.border.Get(),8);
        if (!cases_.empty()) {
            const auto& c=cases_[selectedCase_];
            Text(Widen(c.caseNumber),x+20,detail+16,220,34,h1Fmt_.Get(),brush_.text.Get());
            Badge(c.status==sentinel::CaseStatus::Open?L"Open":L"Closed",x+250,detail+18,c.status==sentinel::CaseStatus::Open?brush_.green.Get():brush_.cyan.Get());
            Text(Widen(c.title),x+20,detail+55,w-x-420,26,bodyFmt_.Get(),brush_.muted.Get());
            Text(L"Case ID",x+20,detail+98,90,20,smallFmt_.Get(),brush_.muted.Get());
            Text(Widen(c.id.ToString()),x+100,detail+96,330,22,smallFmt_.Get(),brush_.text.Get());
            Text(L"Evidence",x+450,detail+98,90,20,smallFmt_.Get(),brush_.muted.Get());
            Text(std::to_wstring(evidence_.size())+L" items",x+520,detail+96,120,22,bodyFmt_.Get(),brush_.text.Get());
            AddButton(L"import",L"Add Evidence",w-360,detail+26,140,42,true);
            AddButton(L"verify",L"Verify Evidence",w-205,detail+26,150,42,false);
        } else {
            Text(L"No cases yet. Enter a case number and title above, then create the first case.",x+20,detail+40,w-x-80,50,bodyFmt_.Get(),brush_.muted.Get());
        }
    }

    void DrawEvidence(float w,float h) {
        PageTitle(L"Evidence",L"Secure evidence management, verification, and chain of custody");
        float x=kSidebar+28,y=kHeader+100;
        if (cases_.empty()) {
            Rounded(x,y,w-x-28,180,brush_.panel.Get(),brush_.border.Get(),8);
            Text(L"Create a case before importing evidence.",x+24,y+28,500,30,h1Fmt_.Get(),brush_.text.Get());
            AddButton(L"dashboard",L"Return to Dashboard",x+24,y+82,190,44,true);
            return;
        }
        Text(L"Selected case: "+Widen(cases_[selectedCase_].caseNumber)+L" - "+Widen(cases_[selectedCase_].title),x,y,650,28,bodyFmt_.Get(),brush_.text.Get());
        AddButton(L"import",L"+ Import Evidence",w-210,y-4,160,38,true);

        float tableY=y+48;
        Rounded(x,tableY,w-x-360,430,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Original Filename",x+18,tableY+12,230,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"Size",x+310,tableY+12,90,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"Original SHA-256",x+420,tableY+12,230,20,smallFmt_.Get(),brush_.muted.Get());
        size_t maxRows=std::min<size_t>(evidence_.size(),8);
        for (size_t i=0;i<maxRows;i++) {
            float yy=tableY+42+(float)i*44;
            if (i==selectedEvidence_) target_->FillRectangle(D2D1::RectF(x+2,yy-3,w-362,yy+36),brush_.panel2.Get());
            Text(Widen(evidence_[i].originalFilename),x+18,yy,270,22,bodyFmt_.Get(),brush_.text.Get());
            Text(std::to_wstring(evidence_[i].originalSize/1024)+L" KB",x+310,yy,90,22,smallFmt_.Get(),brush_.text.Get());
            Text(Widen(evidence_[i].originalHash.ToHex()).substr(0,22)+L"...",x+420,yy,230,22,smallFmt_.Get(),brush_.muted.Get());
            Badge(L"Verified",w-460,yy-2,brush_.green.Get(),82);
            buttons_.push_back({{x+2,yy-3,w-362,yy+36},L"ev:"+std::to_wstring(i)});
        }
        if (evidence_.empty()) Text(L"No evidence imported for this case.",x+18,tableY+68,360,24,bodyFmt_.Get(),brush_.muted.Get());

        float rx=w-340;
        Rounded(rx,tableY,312,430,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Evidence Details",rx+18,tableY+14,240,28,h1Fmt_.Get(),brush_.text.Get());
        if (!evidence_.empty()) {
            auto& e=evidence_[selectedEvidence_];
            Rounded(rx+18,tableY+54,276,120,brush_.sidebar.Get(),brush_.border.Get(),8);
            Text(L"SECURE",rx+92,tableY+85,160,30,h1Fmt_.Get(),brush_.green.Get());
            Text(L"AES-256-GCM container",rx+65,tableY+122,220,22,smallFmt_.Get(),brush_.muted.Get());
            Text(L"Filename",rx+18,tableY+195,80,20,smallFmt_.Get(),brush_.muted.Get());
            Text(Widen(e.originalFilename),rx+18,tableY+218,270,24,bodyFmt_.Get(),brush_.text.Get());
            Text(L"SHA-256",rx+18,tableY+255,80,20,smallFmt_.Get(),brush_.muted.Get());
            Text(Widen(e.originalHash.ToHex()).substr(0,34)+L"...",rx+18,tableY+278,270,42,smallFmt_.Get(),brush_.text.Get());
            AddButton(L"verify",L"Verify Selected",rx+18,tableY+342,130,44,true);
            AddButton(L"integrity",L"Audit Chain",rx+160,tableY+342,130,44,false);
        }
    }

    void DrawAudit(float w,float h) {
        PageTitle(L"Audit Log",L"Immutable activity records for system and evidence operations");
        float x=kSidebar+28,y=kHeader+100,g=14;
        float card=(w-x-28-g*3)/4;
        Metric(x,y,card,L"Audit Chain",runtime_->audit.VerifyChain()?L"Valid":L"INVALID",L"Tamper-evident ledger",runtime_->audit.VerifyChain()?brush_.green.Get():brush_.red.Get(),IconKind::Chain);
        Metric(x+card+g,y,card,L"Total Records",std::to_wstring(runtime_->AuditCount()),L"Recorded system events",brush_.cyan.Get(),IconKind::Document);
        Metric(x+2*(card+g),y,card,L"Integrity",runtime_->audit.VerifyChain()?L"100%":L"Failed",L"Current chain status",brush_.green.Get(),IconKind::Shield);
        Metric(x+3*(card+g),y,card,L"Secure Store",L"Online",L"Local encrypted mode",brush_.green.Get(),IconKind::Lock);

        float ty=y+126;
        Rounded(x,ty,w-x-28,390,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Timestamp",x+18,ty+12,180,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"Action",x+230,ty+12,220,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"Target",x+520,ty+12,240,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"Chain Status",w-190,ty+12,120,20,smallFmt_.Get(),brush_.muted.Get());

        sqlite3_stmt* s{};
        if (sqlite3_prepare_v2(runtime_->db.Handle(),"SELECT timestamp,action,target_type,target_id FROM audit_records ORDER BY sequence DESC LIMIT 8",-1,&s,nullptr)==SQLITE_OK) {
            int row=0;
            while (sqlite3_step(s)==SQLITE_ROW) {
                float yy=ty+42+row*42;
                const char* ts=(const char*)sqlite3_column_text(s,0);
                int action=sqlite3_column_int(s,1);
                const char* type=(const char*)sqlite3_column_text(s,2);
                const char* id=(const char*)sqlite3_column_text(s,3);
                Text(Widen(ts?ts:""),x+18,yy,200,20,smallFmt_.Get(),brush_.text.Get());
                Text(AuditActionName(action),x+230,yy,250,20,bodyFmt_.Get(),brush_.text.Get());
                Text(Widen(type?type:"")+L"  "+Widen(id?id:""),x+520,yy,w-x-760,20,smallFmt_.Get(),brush_.text.Get());
                Badge(L"Valid",w-190,yy-3,brush_.green.Get(),72);
                row++;
            }
        }
        sqlite3_finalize(s);

        float by=ty+408;
        Rounded(x,by,w-x-28,120,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Audit Chain Integrity",x+18,by+14,260,28,h1Fmt_.Get(),brush_.text.Get());
        Text(runtime_->audit.VerifyChain()?L"Chain verified - no tampering detected":L"WARNING  Audit chain validation failed",x+20,by+55,w-x-250,32,bodyFmt_.Get(),runtime_->audit.VerifyChain()?brush_.green.Get():brush_.red.Get());
        AddButton(L"integrity",L"Verify Audit Chain",w-220,by+38,165,42,true);
    }

    void DrawVerification(float w,float h) {
        PageTitle(L"Verification",L"Evidence authentication and integrity validation");
        float x=kSidebar+28,y=kHeader+100;
        Rounded(x,y,w-x-390,230,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Verification Result",x+18,y+14,260,28,h1Fmt_.Get(),brush_.text.Get());
        Rounded(x+24,y+62,120,120,brush_.sidebar.Get(),brush_.green.Get(),22);
        Text(L"VALID",x+54,y+82,70,70,bigFmt_.Get(),brush_.green.Get());
        Text(lastVerify_.find(L"VALID")!=std::wstring::npos?L"VALID":L"READY",x+170,y+70,220,46,bigFmt_.Get(),lastVerify_.find(L"VALID")!=std::wstring::npos?brush_.green.Get():brush_.cyan.Get());
        Text(lastVerify_,x+170,y+124,w-x-590,64,bodyFmt_.Get(),brush_.muted.Get());

        float rx=w-362;
        Rounded(rx,y,334,230,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Evidence Container",rx+18,y+14,260,28,h1Fmt_.Get(),brush_.text.Get());
        Text(evidence_.empty()?L"No evidence selected":Widen(evidence_[selectedEvidence_].originalFilename),rx+18,y+58,290,48,bodyFmt_.Get(),brush_.text.Get());
        AddButton(L"verify",L"Verify Selected Evidence",rx+18,y+116,298,48,true);

        float sy=y+248;
        Rounded(x,sy,w-x-28,300,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Verification Steps",x+18,sy+14,250,28,h1Fmt_.Get(),brush_.text.Get());
        const wchar_t* steps[]={L"1  Container structure analysis",L"2  AES-GCM authentication",L"3  SHA-256 calculation",L"4  Audit confirmation"};
        for(int i=0;i<4;i++) {
            float yy=sy+60+i*48;
            Text(L"*",x+24,yy,20,20,bodyFmt_.Get(),brush_.green.Get());
            Text(steps[i],x+50,yy,w-x-250,24,bodyFmt_.Get(),brush_.text.Get());
            Text(lastVerify_.find(L"VALID")!=std::wstring::npos?L"Completed":L"Ready",w-180,yy,110,24,smallFmt_.Get(),lastVerify_.find(L"VALID")!=std::wstring::npos?brush_.green.Get():brush_.muted.Get());
        }
    }


    void DrawSimulation(float w,float h) {
        PageTitle(L"Simulation Lab",L"Synthetic conversation testing, model selection, and evidence capture");
        const float x=kSidebar+28.0f;
        const float y=kHeader+102.0f;
        const float gap=14.0f;
        const float sideW=390.0f;
        const float chatW=std::max(560.0f,w-x-sideW-gap-28.0f);

        // Conversation card
        Rounded(x,y,chatW,540,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Synthetic Conversation",x+20,y+12,280,34,h1Fmt_.Get(),brush_.text.Get());
        Badge(L"SIMULATION",x+chatW-126,y+15,brush_.cyan.Get(),104);

        const float transcriptTop=y+56;
        const float transcriptBottom=y+430;
        const float messageBottom=simBotTyping_ ? transcriptBottom-54.0f : transcriptBottom;
        const int total=(int)simContext_.history.size();
        const int maxStart=std::max(0,total-kSimVisibleRows);
        simFirstVisible_=std::clamp(simFirstVisible_,0,maxStart);
        const int end=std::min(total,simFirstVisible_+kSimVisibleRows);
        const int visible=std::max(0,end-simFirstVisible_);
        simMessageRects_.clear();

        float yy=messageBottom-visible*82.0f;
        for(int i=simFirstVisible_;i<end;++i) {
            const auto& turn=simContext_.history[(size_t)i];
            const bool investigator=turn.speaker==sentinel::simulation::ChatTurn::Speaker::Investigator;
            const bool suggestion=turn.speaker==sentinel::simulation::ChatTurn::Speaker::ModelSuggestion;
            const float bubbleW=std::min(chatW-120.0f,610.0f);
            const float bx=investigator?x+chatW-bubbleW-34.0f:x+20.0f;
            ID2D1Brush* fill=investigator?brush_.panel2.Get():brush_.sidebar.Get();
            ID2D1Brush* border=suggestion?brush_.yellow.Get():(investigator?brush_.blue.Get():brush_.border.Get());

            Rounded(bx,yy,bubbleW,72,fill,border,10);
            TextLine(investigator?L"Investigator":suggestion?L"Model / System":L"Synthetic Subject",
                bx+12,yy+5,bubbleW-78,17,tinyFmt_.Get(),
                suggestion?brush_.yellow.Get():(investigator?brush_.cyan.Get():brush_.green.Get()));
            TextLine(L"Copy",bx+bubbleW-54,yy+5,42,17,tinyFmt_.Get(),brush_.muted.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
            buttons_.push_back({{bx+bubbleW-58,yy+3,bx+bubbleW-8,yy+21},L"copy:"+std::to_wstring(i)});
            Text(Widen(turn.text),bx+12,yy+25,bubbleW-24,40,smallFmt_.Get(),brush_.text.Get());
            simMessageRects_.push_back({{bx,yy,bx+bubbleW,yy+72},(size_t)i});
            yy+=82;
        }

        if(simBotTyping_) {
            const float typingY=transcriptBottom-38;
            Rounded(x+20,typingY,218,30,brush_.sidebar.Get(),brush_.border.Get(),15);
            StatusDot(x+38,typingY+15,3,brush_.green.Get());
            TextLine(L"Synthetic subject is typing...",x+50,typingY+2,174,26,tinyFmt_.Get(),brush_.muted.Get());
        }

        UpdateSimulationScrollbar();

        // The EDIT control itself is the full composer.  Its formatting rectangle
        // vertically centers the caret/text without shrinking the textbox.
        AddButton(L"sim_send",L"Send",x+chatW-204,y+452,186,46,true);

        // Model / scenario card
        const float rx=x+chatW+gap;
        Rounded(rx,y,sideW,330,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Scenario & Model",rx+18,y+12,sideW-36,34,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Persona",rx+18,y+54,84,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(Widen(simSettings_.persona.name+" - synthetic profile"),rx+108,y+52,sideW-126,24,smallFmt_.Get(),brush_.text.Get());

        TextLine(L"Age state",rx+18,y+84,84,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(Widen(sentinel::simulation::ToString(simSettings_.ageState)),rx+108,y+82,sideW-126,24,smallFmt_.Get(),brush_.cyan.Get());

        TextLine(L"OpenAI-compatible endpoint",rx+18,y+118,sideW-36,18,tinyFmt_.Get(),brush_.muted.Get());

        AddButton(L"sim_browse_models",L"Browse Models",rx+18,y+182,126,34,false);
        AddButton(L"sim_install_ai",L"Install / Repair AI",rx+154,y+182,132,34,true);

        TextLine(L"Available model",rx+18,y+224,112,20,tinyFmt_.Get(),brush_.muted.Get());

        TextLine(L"Manual model",rx+18,y+278,106,20,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"sim_model",L"Connect",rx+sideW-110,y+299,92,32,true);

        StatusDot(rx+24,y+355,4,modelStatus_.find(L"Connected")!=std::wstring::npos?brush_.green.Get():brush_.yellow.Get());
        TextLine(modelStatus_,rx+36,y+342,sideW-54,28,tinyFmt_.Get(),brush_.text.Get());

        TextLine(L"Conversation",rx+18,y+374,90,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(currentConversationTitle_,rx+112,y+371,sideW-130,24,smallFmt_.Get(),brush_.text.Get());
        AddButton(L"sim_previous_chat",L"Previous Chat",rx+18,y+402,142,34,false);
        AddButton(L"sim_new_chat",L"New Chat",rx+170,y+402,112,34,true);

        // Suggestion card
        Rounded(rx,y+400,sideW,140,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Model Suggestion",rx+18,y+410,sideW-36,28,h1Fmt_.Get(),brush_.text.Get());
        Rounded(rx+18,y+444,sideW-36,48,brush_.sidebar.Get(),brush_.border.Get(),8);
        Text(simSuggestion_,rx+28,y+451,sideW-56,34,tinyFmt_.Get(),brush_.text.Get());

        const float bw=(sideW-52)/3.0f;
        AddButton(L"sim_suggest",L"Generate",rx+18,y+500,bw,32,false);
        AddButton(L"sim_reset",L"Reset",rx+26+bw,y+500,bw,32,false);
        AddButton(L"sim_preserve",L"Preserve",rx+34+bw*2,y+500,bw,32,false);
        AddButton(L"sim_export_persona_log",L"Export Persona Log",rx+18,y+544,sideW-36,30,false);
    }

    void ShowChatEditor(bool show) {
        if(chatEdit_) ShowWindow(chatEdit_,show?SW_SHOW:SW_HIDE);
        if(modelEndpointEdit_) ShowWindow(modelEndpointEdit_,show?SW_SHOW:SW_HIDE);
        if(modelNameEdit_) ShowWindow(modelNameEdit_,show?SW_SHOW:SW_HIDE);
        if(modelCombo_) ShowWindow(modelCombo_,show?SW_SHOW:SW_HIDE);
        if(simScroll_) ShowWindow(simScroll_,show?SW_SHOW:SW_HIDE);
    }

    void ShowPersonaEditors(bool show) {
        HWND controls[]={
            personaNameEdit_,personaAgeCombo_,personaLocationEdit_,personaInterestsEdit_,
            personaOccupationEdit_,personaEducationEdit_,personaFamilyEdit_,personaBackgroundEdit_,
            personaGenderCombo_,personaPronounsCombo_,personaRelationshipCombo_,personaPersonalityCombo_,personaSocialCombo_,personaConfidenceCombo_,
            scenarioNameEdit_,scenarioObjectiveEdit_,scenarioSeedEdit_,minDelayEdit_,maxDelayEdit_,ageStateCombo_,
            personaCommunicationCombo_,personaCognitiveCombo_,personaSlangCombo_,personaGrammarCombo_,personaTypoCombo_,personaEmojiCombo_,
            personaWritingStyleCombo_,personaProfileCombo_
        };
        for(HWND h:controls) if(h) ShowWindow(h,show?SW_SHOW:SW_HIDE);
    }

    void ShowAgencyEditors(bool show) {
        if(agencyEndpointEdit_) ShowWindow(agencyEndpointEdit_,show?SW_SHOW:SW_HIDE);
        if(agencyIdEdit_) ShowWindow(agencyIdEdit_,show?SW_SHOW:SW_HIDE);
        if(operatingStateCombo_) ShowWindow(operatingStateCombo_,show?SW_SHOW:SW_HIDE);
    }

    void ApplyPageControls() {
        ShowCaseEditors(page_==Page::Cases);
        ShowChatEditor(page_==Page::Simulation);
        ShowPersonaEditors(page_==Page::Persona);
        ShowAgencyEditors(page_==Page::Agency);
        if(responseRuleTriggerEdit_) ShowWindow(responseRuleTriggerEdit_,page_==Page::ModelLab?SW_SHOW:SW_HIDE);
        if(responseRuleResponseEdit_) ShowWindow(responseRuleResponseEdit_,page_==Page::ModelLab?SW_SHOW:SW_HIDE);
        LayoutNativeControls();
    }

    void LayoutNativeControls() {
        if(!hwnd_) return;
        RECT rc{}; GetClientRect(hwnd_,&rc);
        const float w=(float)rc.right;

        if(page_==Page::Cases) {
            const float x=kSidebar+28.0f, y=kHeader+102.0f;
            MoveControl(caseNumberEdit_,(int)x,(int)y,160,34,TRUE);
            MoveControl(caseTitleEdit_,(int)(x+180),(int)y,300,34,TRUE);
        }

        if(page_==Page::Simulation) {
            const float x=kSidebar+28.0f, y=kHeader+102.0f, gap=14.0f, sideW=390.0f;
            const float chatW=std::max(560.0f,w-x-sideW-gap-28.0f);
            const float transcriptTop=y+56.0f;
            const float transcriptBottom=y+430.0f;
            const float rx=x+chatW+gap;

            MoveControl(simScroll_,(int)(x+chatW-20),(int)transcriptTop,14,(int)(transcriptBottom-transcriptTop),TRUE);

            const int composerW=(int)(chatW-236);
            const int composerH=46;
            MoveControl(chatEdit_,(int)(x+18),(int)(y+452),composerW,composerH,TRUE);
            RECT composerTextRect{12,9,std::max(24,composerW-12),composerH-8};
            SendMessageW(chatEdit_,EM_SETRECTNP,0,(LPARAM)&composerTextRect);

            MoveControl(modelEndpointEdit_,(int)(rx+18),(int)(y+138),(int)(sideW-36),32,TRUE);
            MoveControl(modelCombo_,(int)(rx+18),(int)(y+244),(int)(sideW-36),180,TRUE);
            MoveControl(modelNameEdit_,(int)(rx+18),(int)(y+299),(int)(sideW-140),32,TRUE);
        }

        if(page_==Page::Persona) {
            const float x=kSidebar+28.0f, y=kHeader+104.0f, gap=14.0f;
            const float profileW=360.0f;
            MoveControl(personaProfileCombo_,(int)(x+108),(int)(y-42),(int)profileW,180);
            const float contentW=w-x-28.0f;
            const float colW=(contentW-gap)/2.0f;
            const float rightX=x+colW+gap;

            const float lx=x+20, lf=x+132, lw=colW-152;
            float row=y+54;
            MoveControl(personaNameEdit_,(int)lf,(int)row,(int)lw,32);
            RECT personaNameRect{8,6,std::max(20,(int)lw-8),27};
            SendMessageW(personaNameEdit_,EM_SETRECTNP,0,(LPARAM)&personaNameRect);
            row+=42;

            MoveControl(personaAgeCombo_,(int)(lx+50),(int)row,72,140);
            MoveControl(ageStateCombo_,(int)(lx+212),(int)row,(int)(colW-232),170); row+=42;

            MoveControl(personaGenderCombo_,(int)(lx+64),(int)row,132,160);
            MoveControl(personaPronounsCombo_,(int)(lx+278),(int)row,(int)(colW-298),160); row+=42;

            MoveControl(personaLocationEdit_,(int)lf,(int)row,(int)lw,32); row+=42;
            MoveControl(personaOccupationEdit_,(int)lf,(int)row,(int)lw,32); row+=42;
            MoveControl(personaEducationEdit_,(int)lf,(int)row,(int)lw,32); row+=42;
            MoveControl(personaRelationshipCombo_,(int)lf,(int)row,(int)lw,180);

            const float rx=rightX+20, rf=rightX+132, rw=colW-152;
            row=y+54;
            MoveControl(personaPersonalityCombo_,(int)rf,(int)row,(int)rw,180); row+=42;
            MoveControl(personaSocialCombo_,(int)rf,(int)row,(int)rw,160); row+=42;
            MoveControl(personaConfidenceCombo_,(int)rf,(int)row,(int)rw,140); row+=42;
            MoveControl(personaInterestsEdit_,(int)rf,(int)row,(int)rw,32); row+=42;
            MoveControl(personaWritingStyleCombo_,(int)rf,(int)row,(int)rw,180); row+=42;
            MoveControl(personaFamilyEdit_,(int)rf,(int)row,(int)rw,32); row+=42;
            MoveControl(personaBackgroundEdit_,(int)rf,(int)row,(int)rw,32);

            const float sy=y+396;
            MoveControl(scenarioNameEdit_,(int)(x+92),(int)(sy+50),210,32);
            MoveControl(scenarioObjectiveEdit_,(int)(x+390),(int)(sy+50),(int)std::max(220.0f,contentW-690),32);
            MoveControl(scenarioSeedEdit_,(int)(x+72),(int)(sy+92),74,32);
            MoveControl(minDelayEdit_,(int)(x+250),(int)(sy+92),86,32);
            MoveControl(maxDelayEdit_,(int)(x+370),(int)(sy+92),86,32);

            const float ty=sy+158;
            MoveControl(personaCommunicationCombo_,(int)(x+112),(int)(ty+44),145,150);
            MoveControl(personaCognitiveCombo_,(int)(x+347),(int)(ty+44),120,150);
            MoveControl(personaSlangCombo_,(int)(x+547),(int)(ty+44),145,150);
            MoveControl(personaGrammarCombo_,(int)(x+112),(int)(ty+84),145,150);
            MoveControl(personaTypoCombo_,(int)(x+327),(int)(ty+84),140,150);
            MoveControl(personaEmojiCombo_,(int)(x+547),(int)(ty+84),145,150);
        }

        if(page_==Page::ModelLab) {
            const float x=kSidebar+28.0f, y=kHeader+104.0f;
            const float contentW=w-x-28.0f;
            MoveControl(responseRuleTriggerEdit_,(int)(x+88),(int)(y+108),220,30,TRUE);
            MoveControl(responseRuleResponseEdit_,(int)(x+382),(int)(y+108),(int)std::max(180.0f,contentW-880.0f),30,TRUE);
        }

        if(page_==Page::Agency) {
            const float x=kSidebar+28.0f, y=kHeader+104.0f, gap=14.0f;
            const float contentW=w-x-28.0f;
            const float leftW=(contentW-gap)*0.58f;
            MoveControl(agencyEndpointEdit_,(int)(x+142),(int)(y+62),(int)(leftW-164),32);
            MoveControl(agencyIdEdit_,(int)(x+142),(int)(y+108),(int)(leftW-164),32);
            MoveControl(operatingStateCombo_,(int)(x+142),(int)(y+304),(int)(leftW-164),220);
        }
    }

    std::wstring EditText(HWND h) const {
        int len=GetWindowTextLengthW(h);
        std::wstring value((size_t)len+1,L'\0');
        GetWindowTextW(h,value.data(),len+1);
        value.resize((size_t)len);
        return value;
    }

    static std::string LowerAscii(std::string value) {
        std::transform(value.begin(),value.end(),value.begin(),
            [](unsigned char ch){ return (char)std::tolower(ch); });
        return value;
    }

    static std::string NormalizeRuleText(std::string_view input) {
        std::string out;
        bool pendingSpace=false;
        for(unsigned char ch:input) {
            if(std::isalnum(ch)) {
                if(pendingSpace && !out.empty()) out.push_back(' ');
                out.push_back((char)std::tolower(ch));
                pendingSpace=false;
            } else if(std::isspace(ch)) {
                pendingSpace=true;
            } else {
                // Ignore punctuation entirely so "what's", "whats", and
                // "what's?" normalize consistently for rule matching.
            }
        }
        return out;
    }


    static std::vector<std::string> RuleWords(std::string_view input) {
        auto normalized=NormalizeRuleText(input);
        std::vector<std::string> words;
        std::istringstream in(normalized);
        std::string word;
        while(in>>word) {
            if(word=="whats") { words.push_back("what"); words.push_back("is"); continue; }
            if(word=="im") { words.push_back("i"); words.push_back("am"); continue; }
            if(word=="youre") { words.push_back("you"); words.push_back("are"); continue; }
            if(word=="dont") { words.push_back("do"); words.push_back("not"); continue; }
            if(word=="cant") { words.push_back("can"); words.push_back("not"); continue; }
            if(word=="wont") { words.push_back("will"); words.push_back("not"); continue; }
            words.push_back(word);
        }
        return words;
    }

    static int EditDistance(std::string_view a,std::string_view b) {
        std::vector<int> prev(b.size()+1),cur(b.size()+1);
        for(size_t j=0;j<=b.size();++j) prev[j]=(int)j;
        for(size_t i=1;i<=a.size();++i) {
            cur[0]=(int)i;
            for(size_t j=1;j<=b.size();++j) {
                const int cost=a[i-1]==b[j-1]?0:1;
                cur[j]=std::min({prev[j]+1,cur[j-1]+1,prev[j-1]+cost});
            }
            prev.swap(cur);
        }
        return prev[b.size()];
    }

    static bool WordClose(std::string_view a,std::string_view b) {
        if(a==b) return true;
        if(a.size()<4 || b.size()<4) return false;
        return EditDistance(a,b)<=1;
    }

    static int SmartRuleScore(std::string_view input,std::string_view trigger) {
        const auto inputWords=RuleWords(input);
        const auto triggerWords=RuleWords(trigger);
        if(triggerWords.empty() || inputWords.empty()) return 0;

        size_t matched=0;
        std::vector<bool> used(inputWords.size(),false);
        for(const auto& tw:triggerWords) {
            for(size_t i=0;i<inputWords.size();++i) {
                if(used[i]) continue;
                if(WordClose(tw,inputWords[i])) {
                    used[i]=true;
                    ++matched;
                    break;
                }
            }
        }

        const double recall=(double)matched/(double)triggerWords.size();
        const double precision=(double)matched/(double)inputWords.size();
        const int score=(int)std::lround((recall*0.75+precision*0.25)*100.0);

        // Require nearly all meaningful trigger words so "favorite color"
        // cannot accidentally match a totally different favorite-* question.
        if(triggerWords.size()<=2) return recall>=1.0?score:0;
        return recall>=0.75?score:0;
    }

    struct PersonaResponseRuleView {
        long long id{};
        std::string matchType;
        std::string trigger;
        std::string response;
        std::string responseMode{"persona_variation"};
        bool enabled{};
        int matchScore{};
    };

    std::vector<PersonaResponseRuleView> PersonaResponseRules(size_t limit=8) const {
        std::vector<PersonaResponseRuleView> out;
        sqlite3_stmt* s{};
        const char* sql=
            "SELECT id,match_type,trigger_text,response_text,response_mode,enabled "
            "FROM persona_response_rules WHERE persona_name=? "
            "ORDER BY priority DESC,id ASC LIMIT ?";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)
            return out;
        sqlite3_bind_text(s,1,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_int(s,2,(int)std::min<size_t>(limit,50));
        while(sqlite3_step(s)==SQLITE_ROW) {
            PersonaResponseRuleView item;
            item.id=sqlite3_column_int64(s,0);
            const auto* mt=(const char*)sqlite3_column_text(s,1);
            const auto* tr=(const char*)sqlite3_column_text(s,2);
            const auto* rp=(const char*)sqlite3_column_text(s,3);
            item.matchType=mt?mt:"";
            item.trigger=tr?tr:"";
            item.response=rp?rp:"";
            const auto* rm=(const char*)sqlite3_column_text(s,4);
            item.responseMode=rm?rm:"persona_variation";
            item.enabled=sqlite3_column_int(s,5)!=0;
            out.push_back(std::move(item));
        }
        sqlite3_finalize(s);
        return out;
    }

    int PersonaResponseRuleCount() const {
        sqlite3_stmt* s{};
        int count=0;
        if(sqlite3_prepare_v2(runtime_->db.Handle(),
            "SELECT COUNT(*) FROM persona_response_rules WHERE persona_name=? AND enabled=1",
            -1,&s,nullptr)==SQLITE_OK) {
            sqlite3_bind_text(s,1,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
            if(sqlite3_step(s)==SQLITE_ROW) count=sqlite3_column_int(s,0);
        }
        sqlite3_finalize(s);
        return count;
    }

    std::optional<PersonaResponseRuleView> FindPersonaResponseRule(const std::string& input) const {
        sqlite3_stmt* s{};
        const char* sql=
            "SELECT id,match_type,trigger_text,response_text,response_mode,enabled "
            "FROM persona_response_rules "
            "WHERE persona_name=? AND enabled=1 "
            "ORDER BY priority DESC,id ASC";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)
            return std::nullopt;

        sqlite3_bind_text(s,1,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
        const auto normalized=NormalizeRuleText(input);
        std::optional<PersonaResponseRuleView> found;

        while(sqlite3_step(s)==SQLITE_ROW) {
            PersonaResponseRuleView item;
            item.id=sqlite3_column_int64(s,0);
            const auto* mt=(const char*)sqlite3_column_text(s,1);
            const auto* tr=(const char*)sqlite3_column_text(s,2);
            const auto* rp=(const char*)sqlite3_column_text(s,3);
            const auto* rm=(const char*)sqlite3_column_text(s,4);
            item.matchType=mt?mt:"";
            item.trigger=tr?tr:"";
            item.response=rp?rp:"";
            item.responseMode=rm?rm:"persona_variation";
            item.enabled=sqlite3_column_int(s,5)!=0;

            const auto trigger=NormalizeRuleText(item.trigger);
            int score=0;
            if(item.matchType=="exact" && !trigger.empty() && normalized==trigger)
                score=100;
            else if(item.matchType=="contains" && !trigger.empty() && normalized.find(trigger)!=std::string::npos)
                score=95;
            else if(item.matchType=="smart")
                score=SmartRuleScore(input,item.trigger);

            if(score>0 && (!found || score>found->matchScore)) {
                item.matchScore=score;
                found=item;
                if(score==100) break;
            }
        }

        sqlite3_finalize(s);
        return found;
    }

    void TestPersonaResponseRuleMatch() {
        const auto sample=Narrow(EditText(responseRuleTriggerEdit_));
        if(sample.empty()) {
            statusText_=L"Enter a sample question in Trigger first";
            return;
        }
        auto rule=FindPersonaResponseRule(sample);
        if(!rule) {
            statusText_=L"No response rule matched that sample";
            MessageBoxW(hwnd_,L"No saved response rule matched the sample text.",
                L"Rule Test",MB_OK|MB_ICONINFORMATION);
            return;
        }
        const std::wstring msg=
            L"Matched Rule #"+std::to_wstring(rule->id)+
            L"\nMatch: "+Widen(rule->matchType)+
            L" ("+std::to_wstring(rule->matchScore)+L"%)"+
            L"\nTrigger: "+Widen(rule->trigger)+
            L"\nResponse: "+Widen(rule->response);
        statusText_=L"Rule #"+std::to_wstring(rule->id)+L" matched sample at "+
            std::to_wstring(rule->matchScore)+L"%";
        MessageBoxW(hwnd_,msg.c_str(),L"Rule Test Result",MB_OK|MB_ICONINFORMATION);
    }

    void AddPersonaResponseRule(const std::string& matchType) {
        const auto trigger=Narrow(EditText(responseRuleTriggerEdit_));
        const auto response=Narrow(EditText(responseRuleResponseEdit_));
        if(trigger.empty() || response.empty()) {
            statusText_=L"Enter both a trigger and a response";
            return;
        }

        const auto policy=sentinel::simulation::EvaluateSimulationPolicy(
            simSettings_.ageState,response);
        if(!policy.allowed) {
            statusText_=L"Response rule rejected by active safety policy";
            MessageBoxW(hwnd_,Widen(policy.reason).c_str(),
                L"Response Rule Blocked",MB_OK|MB_ICONWARNING);
            return;
        }

        sqlite3_stmt* s{};
        const char* sql=
            "INSERT INTO persona_response_rules("
            "persona_name,match_type,trigger_text,response_text,response_mode,enabled,priority"
            ") VALUES(?,?,?,?,?,1,100)";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) {
            statusText_=L"Unable to prepare response rule";
            return;
        }
        sqlite3_bind_text(s,1,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(s,2,matchType.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(s,3,trigger.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(s,4,response.c_str(),-1,SQLITE_TRANSIENT);
        const std::string responseMode=responseRuleExactWording_?"exact":"persona_variation";
        sqlite3_bind_text(s,5,responseMode.c_str(),-1,SQLITE_TRANSIENT);
        const int rc=sqlite3_step(s);
        sqlite3_finalize(s);

        if(rc==SQLITE_DONE) {
            SetWindowTextW(responseRuleTriggerEdit_,L"");
            SetWindowTextW(responseRuleResponseEdit_,L"");
            if(matchType=="exact") statusText_=L"Exact response rule added";
            else if(matchType=="contains") statusText_=L"Contains response rule added";
            else statusText_=L"Smart response rule added";
        } else {
            statusText_=L"Response rule could not be saved";
        }
    }

    void ToggleResponseRuleWordingMode() {
        responseRuleExactWording_=!responseRuleExactWording_;
        statusText_=responseRuleExactWording_
            ? L"Response rule wording set to Exact"
            : L"Response rule wording set to Persona Variation";
    }

    void ClearPersonaResponseRules() {
        if(MessageBoxW(hwnd_,
            (L"Delete all response rules for "+Widen(simSettings_.persona.name)+L"?").c_str(),
            L"Clear Response Rules",MB_YESNO|MB_ICONQUESTION)!=IDYES) return;

        sqlite3_stmt* s{};
        if(sqlite3_prepare_v2(runtime_->db.Handle(),
            "DELETE FROM persona_response_rules WHERE persona_name=?",
            -1,&s,nullptr)==SQLITE_OK) {
            sqlite3_bind_text(s,1,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_step(s);
        }
        sqlite3_finalize(s);
        statusText_=L"Persona response rules cleared";
    }

    void DeletePersonaResponseRule(long long id) {
        sqlite3_stmt* s{};
        if(sqlite3_prepare_v2(runtime_->db.Handle(),
            "DELETE FROM persona_response_rules WHERE id=? AND persona_name=?",
            -1,&s,nullptr)==SQLITE_OK) {
            sqlite3_bind_int64(s,1,id);
            sqlite3_bind_text(s,2,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_step(s);
        }
        sqlite3_finalize(s);
        statusText_=L"Response rule deleted";
    }

    void ToggleLearningMode() {
        simSettings_.learningMode=!simSettings_.learningMode;
        simContext_.learningMode=simSettings_.learningMode;
        sentinel::simulation::SaveSimulationSettings(
            runtime_->root/"simulation.ini",simSettings_);
        statusText_=simSettings_.learningMode
            ? L"Learning mode enabled"
            : L"Learning mode disabled";
    }

    void RecordLearnedPersonaNote(const std::string& text,const char* sourceKind) {
        if(!simSettings_.learningMode || text.empty()) return;
        sqlite3_stmt* s{};
        const char* sql=
            "INSERT INTO persona_learned_notes(persona_name,conversation_id,source_kind,note_text) "
            "VALUES(?,?,?,?)";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)==SQLITE_OK) {
            sqlite3_bind_text(s,1,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(s,2,currentConversationId_.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(s,3,sourceKind,-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(s,4,text.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_step(s);
        }
        sqlite3_finalize(s);
    }

    std::string BuildPersonaSummary() const {
        const auto& p=simSettings_.persona;
        return p.name+", age "+std::to_string(p.age)+
            ", gender "+p.gender+", pronouns "+p.pronouns+
            ", location "+p.location+", occupation "+p.occupation+
            ", education "+p.education+", relationship status "+p.relationshipStatus+
            ", family context "+p.familyContext+", personality "+p.personality+
            ", social style "+p.socialStyle+", confidence "+p.confidenceLevel+
            ", background "+p.background+", interests "+p.interests+
            ", writing style "+p.writingStyle+
            ", communication level "+p.communicationLevel+
            ", cognitive level "+p.cognitiveLevel+
            ", slang "+p.slangLevel+
            ", grammar "+p.grammarQuality+
            ", typo frequency "+p.typoFrequency+
            ", emoji use "+p.emojiLevel+
            ", vocabulary "+p.vocabularyLevel+
            ", capitalization "+p.capitalizationStyle+
            ", message length "+p.messageLength+
            ", response start delay "+std::to_string(p.responseStartMinMs)+"-"+std::to_string(p.responseStartMaxMs)+" ms.";
    }

    void RefreshPersonaProfileList(const std::string& selectName={}) {
        if(!personaProfileCombo_) return;
        const auto names=runtime_->personaProfiles.ListNames();
        SendMessageW(personaProfileCombo_,CB_RESETCONTENT,0,0);
        int selected=-1;
        for(size_t i=0;i<names.size();++i) {
            const auto w=Widen(names[i]);
            SendMessageW(personaProfileCombo_,CB_ADDSTRING,0,(LPARAM)w.c_str());
            if(names[i]==selectName) selected=(int)i;
        }
        if(selected<0 && !names.empty()) selected=0;
        if(selected>=0) SendMessageW(personaProfileCombo_,CB_SETCURSEL,(WPARAM)selected,0);
    }

    void LoadSelectedPersonaProfile() {
        const auto name=ComboText(personaProfileCombo_);
        if(name.empty()) { statusText_=L"Select a saved persona profile"; return; }
        auto profile=runtime_->personaProfiles.Load(name);
        if(!profile) { statusText_=L"Persona profile could not be loaded"; return; }
        simSettings_.persona=*profile;
        simSettings_.minDelayMs=profile->responseStartMinMs;
        simSettings_.maxDelayMs=profile->responseStartMaxMs;
        LoadProfileEditors();
        LoadPersonaMedia();
        simContext_.personaSummary=BuildPersonaSummary();
        sentinel::simulation::SaveSimulationSettings(runtime_->root/"simulation.ini",simSettings_);
        statusText_=L"Loaded persona profile: "+Widen(name);
    }

    void DeleteSelectedPersonaProfile() {
        const auto name=ComboText(personaProfileCombo_);
        if(name.empty()) return;
        if(MessageBoxW(hwnd_,(L"Delete saved persona profile '"+Widen(name)+L"'?").c_str(),
            L"Delete Persona Profile",MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
        if(runtime_->personaProfiles.Delete(name)) {
            RefreshPersonaProfileList();
            statusText_=L"Persona profile deleted";
        }
    }

    static std::string ProfileLineValue(const std::string& text,const std::string& key) {
        const std::string marker=key+"=";
        auto pos=text.find(marker);
        if(pos==std::string::npos) return {};
        pos+=marker.size();
        auto end=text.find_first_of("\r\n",pos);
        return text.substr(pos,end==std::string::npos?std::string::npos:end-pos);
    }

    void GenerateBehaviorFromBackground() {
        if(!model_) { statusText_=L"No model available for behavior generation"; return; }
        const auto background=Narrow(EditText(personaBackgroundEdit_));
        if(background.empty()) { statusText_=L"Enter a persona background first"; return; }
        int ageSel=(int)SendMessageW(personaAgeCombo_,CB_GETCURSEL,0,0);
        const int age=(ageSel==CB_ERR)?simSettings_.persona.age:(8+ageSel);
        try {
            sentinel::simulation::ModelContext ctx=simContext_;
            ctx.personaSummary=BuildPersonaSummary();
            const auto result=model_->GenerateBehaviorProfile(age,background,ctx);
            LogPersonaConversationEvent(
                "behavior_profile_generation",background,result,0,0);

            auto apply=[&](HWND combo,const std::string& key){
                const auto v=ProfileLineValue(result,key);
                if(v.empty()) return;
                const int idx=FindComboText(combo,v);
                SendMessageW(combo,CB_SETCURSEL,(WPARAM)idx,0);
            };
            apply(personaPersonalityCombo_,"PERSONALITY");
            apply(personaSocialCombo_,"SOCIAL_STYLE");
            apply(personaConfidenceCombo_,"CONFIDENCE");
            apply(personaWritingStyleCombo_,"WRITING_STYLE");
            apply(personaCommunicationCombo_,"COMMUNICATION");
            apply(personaCognitiveCombo_,"COGNITIVE");
            apply(personaSlangCombo_,"SLANG");
            apply(personaGrammarCombo_,"GRAMMAR");
            apply(personaTypoCombo_,"TYPOS");
            apply(personaEmojiCombo_,"EMOJI");

            const auto interests=ProfileLineValue(result,"INTERESTS");
            if(!interests.empty() && interests!="keep existing interests")
                SetWindowTextW(personaInterestsEdit_,Widen(interests).c_str());

            statusText_=L"Behavior profile generated from background; review and Save";
        } catch(const std::exception& e) {
            statusText_=L"Behavior generation failed: "+Widen(e.what());
        }
    }

    void LoadProfileEditors() {
        SetWindowTextW(personaNameEdit_,Widen(simSettings_.persona.name).c_str());
        SendMessageW(personaAgeCombo_,CB_SETCURSEL,(WPARAM)std::clamp(simSettings_.persona.age-8,0,9),0);
        SetWindowTextW(personaLocationEdit_,Widen(simSettings_.persona.location).c_str());
        SetWindowTextW(personaOccupationEdit_,Widen(simSettings_.persona.occupation).c_str());
        SetWindowTextW(personaEducationEdit_,Widen(simSettings_.persona.education).c_str());
        SetWindowTextW(personaFamilyEdit_,Widen(simSettings_.persona.familyContext).c_str());
        SetWindowTextW(personaBackgroundEdit_,Widen(simSettings_.persona.background).c_str());
        SetWindowTextW(personaInterestsEdit_,Widen(simSettings_.persona.interests).c_str());

        SendMessageW(personaGenderCombo_,CB_SETCURSEL,FindComboText(personaGenderCombo_,simSettings_.persona.gender),0);
        SendMessageW(personaPronounsCombo_,CB_SETCURSEL,FindComboText(personaPronounsCombo_,simSettings_.persona.pronouns),0);
        SendMessageW(personaRelationshipCombo_,CB_SETCURSEL,FindComboText(personaRelationshipCombo_,simSettings_.persona.relationshipStatus),0);
        SendMessageW(personaPersonalityCombo_,CB_SETCURSEL,FindComboText(personaPersonalityCombo_,simSettings_.persona.personality),0);
        SendMessageW(personaSocialCombo_,CB_SETCURSEL,FindComboText(personaSocialCombo_,simSettings_.persona.socialStyle),0);
        SendMessageW(personaConfidenceCombo_,CB_SETCURSEL,FindComboText(personaConfidenceCombo_,simSettings_.persona.confidenceLevel),0);
        SendMessageW(personaCommunicationCombo_,CB_SETCURSEL,FindComboText(personaCommunicationCombo_,simSettings_.persona.communicationLevel),0);
        SendMessageW(personaCognitiveCombo_,CB_SETCURSEL,FindComboText(personaCognitiveCombo_,simSettings_.persona.cognitiveLevel),0);
        SendMessageW(personaSlangCombo_,CB_SETCURSEL,FindComboText(personaSlangCombo_,simSettings_.persona.slangLevel),0);
        SendMessageW(personaGrammarCombo_,CB_SETCURSEL,FindComboText(personaGrammarCombo_,simSettings_.persona.grammarQuality),0);
        SendMessageW(personaTypoCombo_,CB_SETCURSEL,FindComboText(personaTypoCombo_,simSettings_.persona.typoFrequency),0);
        SendMessageW(personaEmojiCombo_,CB_SETCURSEL,FindComboText(personaEmojiCombo_,simSettings_.persona.emojiLevel),0);
        SendMessageW(personaWritingStyleCombo_,CB_SETCURSEL,FindComboText(personaWritingStyleCombo_,simSettings_.persona.writingStyle),0);

        SetWindowTextW(scenarioNameEdit_,Widen(simSettings_.scenario.name).c_str());
        SetWindowTextW(scenarioObjectiveEdit_,Widen(simSettings_.scenario.objective).c_str());
        SetWindowTextW(scenarioSeedEdit_,std::to_wstring(simSettings_.scenario.seed).c_str());
        SetWindowTextW(minDelayEdit_,std::to_wstring(simSettings_.persona.responseStartMinMs).c_str());
        SetWindowTextW(maxDelayEdit_,std::to_wstring(simSettings_.persona.responseStartMaxMs).c_str());
    }

    void SaveProfileEditors() {
        try {
            simSettings_.persona.name=Narrow(EditText(personaNameEdit_));
            {
                int ageSel=(int)SendMessageW(personaAgeCombo_,CB_GETCURSEL,0,0);
                simSettings_.persona.age=(ageSel==CB_ERR)?18:(13+ageSel);
            }
            simSettings_.persona.location=Narrow(EditText(personaLocationEdit_));
            simSettings_.persona.gender=ComboText(personaGenderCombo_);
            simSettings_.persona.pronouns=ComboText(personaPronounsCombo_);
            simSettings_.persona.occupation=Narrow(EditText(personaOccupationEdit_));
            simSettings_.persona.education=Narrow(EditText(personaEducationEdit_));
            simSettings_.persona.relationshipStatus=ComboText(personaRelationshipCombo_);
            simSettings_.persona.familyContext=Narrow(EditText(personaFamilyEdit_));
            simSettings_.persona.personality=ComboText(personaPersonalityCombo_);
            simSettings_.persona.socialStyle=ComboText(personaSocialCombo_);
            simSettings_.persona.confidenceLevel=ComboText(personaConfidenceCombo_);
            simSettings_.persona.background=Narrow(EditText(personaBackgroundEdit_));
            simSettings_.persona.interests=Narrow(EditText(personaInterestsEdit_));
            simSettings_.persona.writingStyle=ComboText(personaWritingStyleCombo_);
            simSettings_.persona.communicationLevel=ComboText(personaCommunicationCombo_);
            simSettings_.persona.cognitiveLevel=ComboText(personaCognitiveCombo_);
            simSettings_.persona.slangLevel=ComboText(personaSlangCombo_);
            simSettings_.persona.grammarQuality=ComboText(personaGrammarCombo_);
            simSettings_.persona.typoFrequency=ComboText(personaTypoCombo_);
            simSettings_.persona.emojiLevel=ComboText(personaEmojiCombo_);
            // These are age-sensitive derived defaults for now; later they can
            // be exposed as advanced controls without changing the profile format.
            const int age=simSettings_.persona.age;
            if(age<=10) {
                simSettings_.persona.vocabularyLevel="Child 8-10";
                simSettings_.persona.messageLength="Usually short";
            } else if(age<=13) {
                simSettings_.persona.vocabularyLevel="Preteen 11-13";
                simSettings_.persona.messageLength="Short to medium";
            } else if(age<=15) {
                simSettings_.persona.vocabularyLevel="Young teen 14-15";
                simSettings_.persona.messageLength="Varied short to medium";
            } else {
                simSettings_.persona.vocabularyLevel="Older teen 16-17";
                simSettings_.persona.messageLength="Varied short to medium";
            }
            if(simSettings_.persona.communicationLevel=="Simple")
                simSettings_.persona.vocabularyLevel+=" / simple";
            else if(simSettings_.persona.communicationLevel=="Advanced")
                simSettings_.persona.vocabularyLevel+=" / advanced-for-age";
            simSettings_.persona.capitalizationStyle =
                simSettings_.persona.grammarQuality=="Careful" ? "Standard" : "Casual";
            simSettings_.scenario.name=Narrow(EditText(scenarioNameEdit_));
            simSettings_.scenario.objective=Narrow(EditText(scenarioObjectiveEdit_));
            simSettings_.scenario.seed=(unsigned int)std::max(1,std::stoi(EditText(scenarioSeedEdit_)));
            simSettings_.persona.responseStartMinMs=std::clamp(std::stoi(EditText(minDelayEdit_)),250,30000);
            simSettings_.persona.responseStartMaxMs=std::clamp(
                std::stoi(EditText(maxDelayEdit_)),simSettings_.persona.responseStartMinMs,60000);
            simSettings_.minDelayMs=simSettings_.persona.responseStartMinMs;
            simSettings_.maxDelayMs=simSettings_.persona.responseStartMaxMs;
            int ageSel=(int)SendMessageW(ageStateCombo_,CB_GETCURSEL,0,0);
            if(ageSel>=0 && ageSel<=5) simSettings_.ageState=(sentinel::simulation::AgeKnowledgeState)ageSel;
            simSettings_.endpoint=Narrow(EditText(modelEndpointEdit_));
            simSettings_.model=Narrow(EditText(modelNameEdit_));
            sentinel::simulation::SaveSimulationSettings(runtime_->root/"simulation.ini",simSettings_);
            runtime_->personaProfiles.Save(simSettings_.persona);
            RefreshPersonaProfileList(simSettings_.persona.name);

            simContext_.scenario=simSettings_.scenario.name+": "+simSettings_.scenario.objective;
            simContext_.personaSummary=simSettings_.persona.name+", age "+std::to_string(simSettings_.persona.age)+
                ", gender "+simSettings_.persona.gender+", pronouns "+simSettings_.persona.pronouns+
                ", location "+simSettings_.persona.location+", occupation "+simSettings_.persona.occupation+
                ", education "+simSettings_.persona.education+", relationship status "+simSettings_.persona.relationshipStatus+
                ", family context "+simSettings_.persona.familyContext+", personality "+simSettings_.persona.personality+
                ", social style "+simSettings_.persona.socialStyle+", confidence "+simSettings_.persona.confidenceLevel+
                ", background "+simSettings_.persona.background+", interests "+simSettings_.persona.interests+
                ", writing style "+simSettings_.persona.writingStyle+
                ", communication level "+simSettings_.persona.communicationLevel+
                ", slang "+simSettings_.persona.slangLevel+
                ", grammar "+simSettings_.persona.grammarQuality+
                ", typo frequency "+simSettings_.persona.typoFrequency+
                ", emoji use "+simSettings_.persona.emojiLevel+
                ", vocabulary "+simSettings_.persona.vocabularyLevel+
                ", capitalization "+simSettings_.persona.capitalizationStyle+
                ", message length "+simSettings_.persona.messageLength+".";
            LoadPersonaMedia();
            policyStatus_=L"Profile saved. Age state: "+Widen(sentinel::simulation::ToString(simSettings_.ageState));
            statusText_=L"Persona, policy, scenario, and delay settings saved";
        } catch(const std::exception& e) {
            statusText_=L"Profile save failed";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Save Profile Failed",MB_OK|MB_ICONERROR);
        }
    }

    void LoadPersonaMedia() {
        personaMedia_.clear();
        selectedPersonaMedia_=-1;
        sqlite3_stmt* stmt{};
        const char* sql=
            "SELECT id,persona_name,original_name,stored_path,sha256,tags,approved "
            "FROM persona_media WHERE persona_name=? ORDER BY created_utc DESC";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&stmt,nullptr)!=SQLITE_OK) return;
        sqlite3_bind_text(stmt,1,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
        while(sqlite3_step(stmt)==SQLITE_ROW) {
            PersonaMediaItem item;
            auto col=[&](int i)->std::string{
                const auto* p=(const char*)sqlite3_column_text(stmt,i);
                return p?p:"";
            };
            item.id=col(0);
            item.personaName=col(1);
            item.originalName=col(2);
            item.storedPath=col(3);
            item.sha256=col(4);
            item.tags=col(5);
            item.approved=sqlite3_column_int(stmt,6)!=0;
            personaMedia_.push_back(std::move(item));
        }
        sqlite3_finalize(stmt);
        if(!personaMedia_.empty()) selectedPersonaMedia_=0;
    }

    std::optional<std::filesystem::path> PickPersonaMediaFile() {
        wchar_t file[32768]{};
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.hwndOwner=hwnd_;
        ofn.lpstrFile=file;
        ofn.nMaxFile=32768;
        ofn.lpstrFilter=
            L"Image Files (*.jpg;*.jpeg;*.png;*.webp;*.bmp)\0*.jpg;*.jpeg;*.png;*.webp;*.bmp\0"
            L"All Files\0*.*\0\0";
        ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if(GetOpenFileNameW(&ofn)) return std::filesystem::path(file);
        return std::nullopt;
    }

    static bool IsAllowedPersonaMediaExtension(const std::filesystem::path& path) {
        auto ext=path.extension().wstring();
        std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);
        return ext==L".jpg" || ext==L".jpeg" || ext==L".png" || ext==L".webp" || ext==L".bmp";
    }

    void ImportPersonaMedia() {
        auto file=PickPersonaMediaFile();
        if(!file) return;
        if(!IsAllowedPersonaMediaExtension(*file)) {
            MessageBoxW(hwnd_,L"Choose a JPG, JPEG, PNG, WEBP, or BMP image.",L"Persona Media",MB_OK|MB_ICONINFORMATION);
            return;
        }
        if(simSettings_.persona.name.empty()) {
            MessageBoxW(hwnd_,L"Save the persona name before importing media.",L"Persona Media",MB_OK|MB_ICONINFORMATION);
            return;
        }

        try {
            const auto hash=runtime_->hash.Sha256File(*file).ToHex();
            const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const std::string id="media-"+hash.substr(0,12)+"-"+std::to_string(stamp);

            auto mediaDir=runtime_->root/"persona-media";
            std::filesystem::create_directories(mediaDir);
            auto stored=mediaDir/(Widen(id)+file->extension().wstring());
            std::filesystem::copy_file(*file,stored,std::filesystem::copy_options::overwrite_existing);

            sqlite3_stmt* stmt{};
            const char* sql=
                "INSERT INTO persona_media(id,persona_name,original_name,stored_path,sha256,tags,approved) "
                "VALUES(?,?,?,?,?,?,0)";
            if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&stmt,nullptr)!=SQLITE_OK)
                throw std::runtime_error("could not prepare persona media insert");
            sqlite3_bind_text(stmt,1,id.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt,2,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
            const auto original=Narrow(file->filename().wstring());
            const auto storedUtf8=Narrow(stored.wstring());
            sqlite3_bind_text(stmt,3,original.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt,4,storedUtf8.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt,5,hash.c_str(),-1,SQLITE_TRANSIENT);
            const std::string tags="casual,selfie,benign";
            sqlite3_bind_text(stmt,6,tags.c_str(),-1,SQLITE_TRANSIENT);
            if(sqlite3_step(stmt)!=SQLITE_DONE) {
                sqlite3_finalize(stmt);
                throw std::runtime_error("could not save persona media");
            }
            sqlite3_finalize(stmt);
            LoadPersonaMedia();
            statusText_=L"Persona image imported as UNAPPROVED";
            MessageBoxW(
                hwnd_,
                L"Image imported as UNAPPROVED.\n\nApprove only benign, non-sexual synthetic-persona media suitable for ordinary conversation.",
                L"Persona Media",
                MB_OK|MB_ICONINFORMATION);
        } catch(const std::exception& e) {
            statusText_=L"Persona media import failed";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Persona Media Import Failed",MB_OK|MB_ICONERROR);
        }
    }

    void TogglePersonaMediaApproval() {
        if(selectedPersonaMedia_<0 || selectedPersonaMedia_>=(int)personaMedia_.size()) {
            statusText_=L"Select a persona image first";
            return;
        }
        auto& item=personaMedia_[(size_t)selectedPersonaMedia_];
        const bool next=!item.approved;
        if(next) {
            const int answer=MessageBoxW(
                hwnd_,
                L"Approve this image for ordinary persona use?\n\nOnly approve benign, non-sexual synthetic-persona media. Do not approve nudity, underwear/lingerie, sexualized poses, or explicit content involving a minor persona.",
                L"Approve Persona Media",
                MB_YESNO|MB_ICONWARNING);
            if(answer!=IDYES) return;
        }
        sqlite3_stmt* stmt{};
        if(sqlite3_prepare_v2(runtime_->db.Handle(),"UPDATE persona_media SET approved=? WHERE id=?",-1,&stmt,nullptr)!=SQLITE_OK) return;
        sqlite3_bind_int(stmt,1,next?1:0);
        sqlite3_bind_text(stmt,2,item.id.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        item.approved=next;
        statusText_=next?L"Persona image approved for benign use":L"Persona image approval removed";
    }

    void DeletePersonaMedia() {
        if(selectedPersonaMedia_<0 || selectedPersonaMedia_>=(int)personaMedia_.size()) {
            statusText_=L"Select a persona image first";
            return;
        }
        const auto item=personaMedia_[(size_t)selectedPersonaMedia_];
        if(MessageBoxW(hwnd_,L"Delete this persona image from Sentinel?",L"Delete Persona Media",MB_YESNO|MB_ICONQUESTION)!=IDYES)
            return;
        sqlite3_stmt* stmt{};
        if(sqlite3_prepare_v2(runtime_->db.Handle(),"DELETE FROM persona_media WHERE id=?",-1,&stmt,nullptr)==SQLITE_OK) {
            sqlite3_bind_text(stmt,1,item.id.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(Widen(item.storedPath)),ec);
        LoadPersonaMedia();
        statusText_=L"Persona image deleted";
    }

    int FindApprovedPersonaMedia() const {
        if(selectedPersonaMedia_>=0 && selectedPersonaMedia_<(int)personaMedia_.size() &&
           personaMedia_[(size_t)selectedPersonaMedia_].approved)
            return selectedPersonaMedia_;
        for(size_t i=0;i<personaMedia_.size();++i)
            if(personaMedia_[i].approved) return (int)i;
        return -1;
    }

    static bool LooksLikeBenignPictureRequest(const std::string& text) {
        std::string lower=text;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return (char)std::tolower(c);});
        const bool wantsImage=
            lower.find("send a pic")!=std::string::npos ||
            lower.find("send me a pic")!=std::string::npos ||
            lower.find("send a picture")!=std::string::npos ||
            lower.find("send me a picture")!=std::string::npos ||
            lower.find("send a photo")!=std::string::npos ||
            lower.find("send me a photo")!=std::string::npos ||
            lower.find("selfie")!=std::string::npos;
        if(!wantsImage) return false;

        const char* blocked[]={"nude","naked","underwear","panties","bra ","lingerie","sexy pic","explicit","boob","breast","vagina","penis","dick"};
        for(auto* term:blocked) if(lower.find(term)!=std::string::npos) return false;
        return true;
    }

    void StageApprovedPersonaMedia(const std::string& triggerText) {
        if(!LooksLikeBenignPictureRequest(triggerText)) return;
        const int idx=FindApprovedPersonaMedia();
        if(idx<0) {
            statusText_=L"Picture requested, but no approved benign persona image is available";
            return;
        }
        const auto& item=personaMedia_[(size_t)idx];
        const std::string caption="Here you go.";
        const std::string action=
            "media:local-sim:"+item.id+"|"+item.sha256+"|"+item.storedPath+"|"+caption;
        approvals_.push_back(sentinel::operations::CreateApprovalRequest(action,"local-investigator"));
        statusText_=L"Benign persona image staged for supervisor approval";
    }

    void UpdateSimulationScrollbar() {
        if(!simScroll_) return;
        int maxStart=std::max(0,(int)simContext_.history.size()-kSimVisibleRows);
        SCROLLINFO si{};
        si.cbSize=sizeof(si);
        si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
        si.nMin=0;
        si.nMax=std::max(0,(int)simContext_.history.size()-1);
        si.nPage=kSimVisibleRows;
        si.nPos=std::clamp(simFirstVisible_,0,maxStart);
        SetScrollInfo(simScroll_,SB_CTL,&si,TRUE);
    }

    void ScrollSimulationToBottom() {
        simFirstVisible_=std::max(0,(int)simContext_.history.size()-kSimVisibleRows);
        UpdateSimulationScrollbar();
    }

    void ResumeOrCreateConversation() {
        try {
            auto conversations=runtime_->conversationMemory.List(50);
            if(conversations.empty()) {
                sentinel::simulation::ModelContext legacy=simContext_;
                if(sentinel::simulation::LoadSession(runtime_->root/"simulation-session.tsv",legacy)
                    && !legacy.history.empty()) {
                    const std::string title="Recovered previous conversation";
                    currentConversationId_=runtime_->conversationMemory.StartConversation(
                        title,legacy.personaSummary,legacy.scenario);
                    for(const auto& turn:legacy.history) {
                        runtime_->conversationMemory.Append(currentConversationId_,turn.speaker,turn.text);
                    }
                    simContext_=std::move(legacy);
                    currentConversationTitle_=Widen(title);
                    simContext_.recalledMemory.clear();
                    ScrollSimulationToBottom();
                    statusText_=L"Recovered previous conversation";
                    return;
                }
                ResetSimulation();
                return;
            }

            sentinel::simulation::ModelContext loaded=simContext_;
            if(runtime_->conversationMemory.Load(conversations.front().id,loaded)) {
                currentConversationId_=conversations.front().id;
                currentConversationTitle_=Widen(conversations.front().title);
                simContext_=std::move(loaded);
                simContext_.recalledMemory.clear();
                archiveCursor_=0;
                simBotTyping_=false;
                simReplyPending_=false;
                simPendingMessage_.clear();
                simPreparedReply_.clear();
                ScrollSimulationToBottom();
                statusText_=L"Most recent conversation resumed";
                return;
            }
        } catch(...) {}
        ResetSimulation();
    }

    void ResetSimulation() {
        simContext_.scenario=simSettings_.scenario.name+": "+simSettings_.scenario.objective;
        simContext_.personaSummary=simSettings_.persona.name+", age "+std::to_string(simSettings_.persona.age)+
            ", gender "+simSettings_.persona.gender+", pronouns "+simSettings_.persona.pronouns+
            ", location "+simSettings_.persona.location+", occupation "+simSettings_.persona.occupation+
            ", education "+simSettings_.persona.education+", relationship status "+simSettings_.persona.relationshipStatus+
            ", family context "+simSettings_.persona.familyContext+", personality "+simSettings_.persona.personality+
            ", social style "+simSettings_.persona.socialStyle+", confidence "+simSettings_.persona.confidenceLevel+
            ", background "+simSettings_.persona.background+", interests "+simSettings_.persona.interests+
            ", writing style "+simSettings_.persona.writingStyle+".";
        simContext_.recalledMemory.clear();
        simContext_.history.clear();
        simContext_.history.push_back({sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
            "Simulation ready. Send a test message to begin."});

        const std::string title=simSettings_.scenario.name.empty()
            ? ("Conversation with "+simSettings_.persona.name)
            : simSettings_.scenario.name;
        currentConversationId_=runtime_->conversationMemory.StartConversation(
            title,simContext_.personaSummary,simContext_.scenario);
        currentConversationTitle_=Widen(title);
        archiveCursor_=0;

        simSuggestion_=L"No suggestion generated yet";
        simBotTyping_=false;
        simReplyPending_=false;
        simPendingMessage_.clear();
        simPreparedReply_.clear();
        KillTimer(hwnd_,kSimTypingStartTimer);
        KillTimer(hwnd_,kSimReplyTimer);
        KillTimer(hwnd_,kSimEngagementTimer);
        simInitiativeSent_=false;
        if(chatEdit_) {
            SetWindowTextW(chatEdit_,L"");
            if(page_==Page::Simulation) {
                SetFocus(chatEdit_);
                SendMessageW(chatEdit_,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);
            }
        }
        ScrollSimulationToBottom();
        statusText_=L"New persistent conversation started";
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void LoadPreviousConversation() {
        try {
            auto conversations=runtime_->conversationMemory.List(50);
            if(conversations.empty()) {
                statusText_=L"No previous conversations yet";
                return;
            }

            size_t target=0;
            auto it=std::find_if(conversations.begin(),conversations.end(),[&](const auto& item){
                return item.id==currentConversationId_;
            });
            if(it!=conversations.end()) {
                size_t current=(size_t)std::distance(conversations.begin(),it);
                target=(current+1<conversations.size())?current+1:0;
            } else if(archiveCursor_>=0 && (size_t)archiveCursor_<conversations.size()) {
                target=(size_t)archiveCursor_;
            }

            sentinel::simulation::ModelContext loaded=simContext_;
            if(!runtime_->conversationMemory.Load(conversations[target].id,loaded)) {
                statusText_=L"Selected conversation has no messages yet";
                return;
            }

            currentConversationId_=conversations[target].id;
            currentConversationTitle_=Widen(conversations[target].title);
            archiveCursor_=(int)target;
            simContext_=std::move(loaded);
            simContext_.recalledMemory.clear();
            simBotTyping_=false;
            simReplyPending_=false;
            simPendingMessage_.clear();
            simPreparedReply_.clear();
            KillTimer(hwnd_,kSimTypingStartTimer);
            KillTimer(hwnd_,kSimReplyTimer);
            KillTimer(hwnd_,kSimEngagementTimer);
            simInitiativeSent_=false;
            if(chatEdit_) {
                SetWindowTextW(chatEdit_,L"");
                SetFocus(chatEdit_);
                SendMessageW(chatEdit_,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);
            }
            ScrollSimulationToBottom();
            statusText_=L"Previous conversation loaded";
            InvalidateRect(hwnd_,nullptr,FALSE);
        } catch(const std::exception& e) {
            statusText_=L"Conversation load failed: "+Widen(e.what());
        }
    }

    int RandomInRange(int lo,int hi) {
        if(hi<lo) std::swap(lo,hi);
        static thread_local std::mt19937 rng([]{
            std::random_device rd;
            return std::mt19937(rd());
        }());
        std::uniform_int_distribution<int> dist(lo,hi);
        return dist(rng);
    }

    void SendSimulationMessage() {
        if(simBotTyping_ || simReplyPending_) return;
        wchar_t buffer[2048]{};
        GetWindowTextW(chatEdit_,buffer,2048);
        std::wstring message=buffer;
        while(!message.empty() && (message.back()==L'\r' || message.back()==L'\n' || message.back()==L' ')) message.pop_back();
        if(message.empty()) return;
        auto utf8=Narrow(message);
        if(currentConversationId_.empty()) ResetSimulation();
        simContext_.variationSeed=(unsigned int)(
            std::hash<std::string>{}(currentConversationId_) & 0xffffffffu);
        simContext_.learningMode=simSettings_.learningMode;
        simContext_.recalledMemory=runtime_->conversationMemory.RecallRelevant(
            utf8,currentConversationId_,12);
        const auto participantFacts=runtime_->conversationMemory.RecallParticipantFacts(
            currentConversationId_,10);
        if(!participantFacts.empty()) {
            if(!simContext_.recalledMemory.empty()) simContext_.recalledMemory+="\n";
            simContext_.recalledMemory+=participantFacts;
        }
        const auto personaClaims=runtime_->conversationMemory.RecallPersonaClaims(
            currentConversationId_,12);
        if(!personaClaims.empty()) {
            if(!simContext_.recalledMemory.empty()) simContext_.recalledMemory+="\n";
            simContext_.recalledMemory+=personaClaims;
        }
        LogPersonaConversationEvent("inbound",utf8,{},0,0);
        simContext_.history.push_back({sentinel::simulation::ChatTurn::Speaker::Investigator,utf8});
        runtime_->conversationMemory.Append(
            currentConversationId_,
            sentinel::simulation::ChatTurn::Speaker::Investigator,
            utf8);
        StageApprovedPersonaMedia(utf8);
        sentinel::simulation::SaveSession(runtime_->root/"simulation-session.tsv",simContext_);
        SetWindowTextW(chatEdit_,L"");
        simSuggestion_=L"No suggestion generated yet";
        simPendingMessage_=utf8;
        simPreparedReply_.clear();
        simPreparedFromRule_=false;

        simRuleMeaning_.clear();
        simRuleResponseMode_.clear();

        if(auto rule=FindPersonaResponseRule(utf8)) {
            const auto policy=sentinel::simulation::EvaluateSimulationPolicy(
                simSettings_.ageState,rule->response);
            if(policy.allowed) {
                simPreparedFromRule_=true;
                simRuleMeaning_=rule->response;
                simRuleResponseMode_=rule->responseMode;
                if(rule->responseMode=="exact")
                    simPreparedReply_=rule->response;
                statusText_=L"Rule #"+std::to_wstring(rule->id)+L" matched ("+
                    std::to_wstring(rule->matchScore)+L"%): "+Widen(rule->trigger);
            } else {
                statusText_=L"Matched response rule was blocked by active policy";
            }
        }

        simReplyPending_=true;
        simBotTyping_=false;
        simInitiativeSent_=false;
        KillTimer(hwnd_,kSimEngagementTimer);
        ScrollSimulationToBottom();

        // Human-like pacing: first read/think silently, then show typing.
        const int baseStart=RandomInRange(
            simSettings_.persona.responseStartMinMs,
            simSettings_.persona.responseStartMaxMs);
        const int lengthAdjustment=std::min(1800,(int)utf8.size()*RandomInRange(4,12));
        int readingDelay=std::clamp(baseStart+lengthAdjustment,250,60000);
        simLastStartDelayMs_=readingDelay;
        simLastTypingDelayMs_=0;
        SetTimer(hwnd_,kSimTypingStartTimer,(UINT)readingDelay,nullptr);
        if(!simPreparedFromRule_)
            statusText_=L"Message delivered";
        SetFocus(chatEdit_);
        SendMessageW(chatEdit_,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);
        InvalidateRect(chatEdit_,nullptr,FALSE);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void CopySimulationMessage(size_t index) {
        if(index>=simContext_.history.size()) return;
        std::wstring text=Widen(simContext_.history[index].text);
        if(!OpenClipboard(hwnd_)) return;
        EmptyClipboard();
        SIZE_T bytes=(text.size()+1)*sizeof(wchar_t);
        HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes);
        if(mem) {
            void* ptr=GlobalLock(mem);
            if(ptr) {
                memcpy(ptr,text.c_str(),bytes);
                GlobalUnlock(mem);
                SetClipboardData(CF_UNICODETEXT,mem);
                mem=nullptr;
            }
            if(mem) GlobalFree(mem);
        }
        CloseClipboard();
    }

    void PopulateModelDropdown(const std::vector<std::string>& models,const std::string& selected) {
        SendMessageW(modelCombo_,CB_RESETCONTENT,0,0);
        int selectedIndex=-1;
        for(size_t i=0;i<models.size();++i) {
            const auto w=Widen(models[i]);
            SendMessageW(modelCombo_,CB_ADDSTRING,0,(LPARAM)w.c_str());
            if(models[i]==selected) selectedIndex=(int)i;
        }
        if(selectedIndex<0 && !models.empty()) selectedIndex=0;
        if(selectedIndex>=0) {
            SendMessageW(modelCombo_,CB_SETCURSEL,(WPARAM)selectedIndex,0);
            SetWindowTextW(modelNameEdit_,Widen(models[(size_t)selectedIndex]).c_str());
        }
    }

    bool ConnectDiscoveredLocalModel(const std::vector<std::string>& models,std::wstring* failure=nullptr) {
        if(models.empty()) {
            if(failure) *failure=L"Local backend returned no models.";
            return false;
        }

        std::string chosen=simSettings_.model;
        if(chosen.empty() || std::find(models.begin(),models.end(),chosen)==models.end()) {
            auto preferred=std::find(models.begin(),models.end(),"sentinel-chat");
            chosen=preferred!=models.end()?*preferred:models.front();
        }

        try {
            auto candidate=sentinel::simulation::CreateOpenAICompatibleModel(
                simSettings_.endpoint,chosen,{},simSettings_.temperature,simSettings_.maxTokens);
            sentinel::simulation::ModelContext testContext;
            testContext.scenario="Sentinel local model startup connection test";
            testContext.personaSummary="Startup diagnostic identity.";
            (void)candidate->GenerateInvestigatorSuggestion(testContext);

            model_=std::move(candidate);
            simSettings_.model=chosen;
            PopulateModelDropdown(models,chosen);
            SetWindowTextW(modelEndpointEdit_,Widen(simSettings_.endpoint).c_str());
            SetWindowTextW(modelNameEdit_,Widen(chosen).c_str());
            sentinel::simulation::SaveSimulationSettings(runtime_->root/"simulation.ini",simSettings_);
            modelStatus_=L"Connected automatically: "+Widen(chosen);
            statusText_=L"Local AI loaded automatically";
            return true;
        } catch(const std::exception& e) {
            if(failure) *failure=Widen(e.what());
            return false;
        }
    }

    void AutoInitializeLocalAi() {
        if(simSettings_.endpoint.empty())
            simSettings_.endpoint="http://127.0.0.1:1234/v1/chat/completions";
        if(simSettings_.model.empty())
            simSettings_.model="sentinel-chat";

        SetWindowTextW(modelEndpointEdit_,Widen(simSettings_.endpoint).c_str());
        SetWindowTextW(modelNameEdit_,Widen(simSettings_.model).c_str());

        if(!IsLocalModelEndpoint(simSettings_.endpoint)) {
            // Preserve explicitly configured remote/OpenAI-compatible endpoints.
            try {
                auto models=sentinel::simulation::DiscoverOpenAICompatibleModels(simSettings_.endpoint);
                std::wstring failure;
                if(ConnectDiscoveredLocalModel(models,&failure)) return;
                throw std::runtime_error(Narrow(failure));
            } catch(const std::exception& e) {
                model_=sentinel::simulation::CreateRuleBasedTestModel();
                modelStatus_=L"Configured model unavailable; fallback active: "+Widen(e.what());
                return;
            }
        }

        std::wstring setupFailure;
        if(!BundledAiPrerequisitesPresent()) {
            modelStatus_=L"First-run local AI setup starting automatically...";
            statusText_=L"Installing bundled local AI";
            if(!RunBundledAiSetup(&setupFailure)) {
                model_=sentinel::simulation::CreateRuleBasedTestModel();
                modelStatus_=L"Automatic local AI setup failed: "+setupFailure;
                statusText_=L"Local AI setup failed";
                return;
            }
        }

        // Setup may already have started the server. Discovery is attempted first;
        // otherwise start the existing bundled backend and retry.
        try {
            auto models=sentinel::simulation::DiscoverOpenAICompatibleModels(simSettings_.endpoint);
            std::wstring failure;
            if(ConnectDiscoveredLocalModel(models,&failure)) return;
        } catch(...) {}

        std::wstring startFailure;
        if(StartBundledAiService(&startFailure)) {
            try {
                auto models=sentinel::simulation::DiscoverOpenAICompatibleModels(simSettings_.endpoint);
                std::wstring failure;
                if(ConnectDiscoveredLocalModel(models,&failure)) return;
                startFailure=failure;
            } catch(const std::exception& e) {
                startFailure=Widen(e.what());
            }
        }

        model_=sentinel::simulation::CreateRuleBasedTestModel();
        modelStatus_=L"Local AI automatic startup failed; fallback active. "+startFailure;
        statusText_=L"Local AI startup failed";
    }

    void InstallOrRepairLocalAi() {
        const auto setup=ExeDir()/L"Setup-Sentinel-AI.cmd";
        if(!std::filesystem::exists(setup)) {
            modelStatus_=L"AI installer is missing: "+setup.wstring();
            statusText_=L"Local AI installer missing";
            InvalidateRect(hwnd_,nullptr,FALSE);
            return;
        }
        const auto rc=(INT_PTR)ShellExecuteW(
            hwnd_,L"open",setup.c_str(),nullptr,ExeDir().c_str(),SW_SHOWNORMAL);
        if(rc<=32) {
            modelStatus_=L"Could not launch AI installer. ShellExecute error "+std::to_wstring(rc)+L".";
            statusText_=L"Local AI installer failed to launch";
        } else {
            modelStatus_=L"AI installer launched. Sentinel will discover and connect the model automatically on the next start.";
            statusText_=L"Local AI installation / repair started";
        }
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void BrowseModels() {
        wchar_t endpoint[2048]{};
        GetWindowTextW(modelEndpointEdit_,endpoint,2048);
        std::wstring we=endpoint;
        if(we.empty()) {
            modelStatus_=L"Enter a model endpoint first.";
            return;
        }
        modelStatus_=L"Browsing models...";
        InvalidateRect(hwnd_,nullptr,FALSE);
        UpdateWindow(hwnd_);
        try {
            auto models=sentinel::simulation::DiscoverOpenAICompatibleModels(Narrow(we));
            SendMessageW(modelCombo_,CB_RESETCONTENT,0,0);
            for(const auto& item:models) {
                auto w=Widen(item);
                SendMessageW(modelCombo_,CB_ADDSTRING,0,(LPARAM)w.c_str());
            }
            if(!models.empty()) {
                SendMessageW(modelCombo_,CB_SETCURSEL,0,0);
                SetWindowTextW(modelNameEdit_,Widen(models[0]).c_str());
                modelStatus_=L"Found "+std::to_wstring(models.size())+L" model(s). Select one and Connect.";
                statusText_=L"Model list loaded";
            } else {
                SendMessageW(modelCombo_,CB_RESETCONTENT,0,0);
                const std::wstring none=L"<no local models discovered>";
                SendMessageW(modelCombo_,CB_ADDSTRING,0,(LPARAM)none.c_str());
                SendMessageW(modelCombo_,CB_SETCURSEL,0,0);
                modelStatus_=L"No models returned by /v1/models. The local backend is not running correctly or has no loaded model.";
                statusText_=L"No local models available";
            }
        } catch(const std::exception& e) {
            bool recovered=false;
            std::wstring launcherFailure;
            if(IsLocalModelEndpoint(Narrow(we)) && StartBundledAiService(&launcherFailure)) {
                try {
                    auto models=sentinel::simulation::DiscoverOpenAICompatibleModels(Narrow(we));
                    SendMessageW(modelCombo_,CB_RESETCONTENT,0,0);
                    for(const auto& item:models) {
                        auto w=Widen(item);
                        SendMessageW(modelCombo_,CB_ADDSTRING,0,(LPARAM)w.c_str());
                    }
                    if(!models.empty()) {
                        SendMessageW(modelCombo_,CB_SETCURSEL,0,0);
                        SetWindowTextW(modelNameEdit_,Widen(models[0]).c_str());
                    }
                    modelStatus_=L"Found "+std::to_wstring(models.size())+L" model(s). Select one and Connect.";
                    statusText_=L"Model list loaded";
                    recovered=true;
                } catch(...) {}
            }
            if(!recovered) {
                modelStatus_=L"Browse failed: "+Widen(e.what());
                if(!launcherFailure.empty()) modelStatus_+=L" | "+launcherFailure;
                statusText_=L"Model discovery failed";
            }
        }
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void ConfigureLocalModel() {
        wchar_t endpoint[2048]{}, modelName[512]{};
        GetWindowTextW(modelEndpointEdit_,endpoint,2048);

        std::wstring wm;
        int sel=(int)SendMessageW(modelCombo_,CB_GETCURSEL,0,0);
        if(sel!=CB_ERR) {
            int len=(int)SendMessageW(modelCombo_,CB_GETLBTEXTLEN,sel,0);
            if(len>=0) {
                std::wstring selected((size_t)len+1,L'\0');
                SendMessageW(modelCombo_,CB_GETLBTEXT,sel,(LPARAM)selected.data());
                selected.resize((size_t)len);
                wm=selected;
                SetWindowTextW(modelNameEdit_,wm.c_str());
            }
        }
        if(wm.empty()) {
            GetWindowTextW(modelNameEdit_,modelName,512);
            wm=modelName;
        }
        std::wstring we=endpoint;
        if(we.empty() || wm.empty()) {
            modelStatus_=L"Enter an endpoint URL and model name.";
            return;
        }
        try {
            auto candidate=sentinel::simulation::CreateOpenAICompatibleModel(
                Narrow(we),Narrow(wm),{},simSettings_.temperature,simSettings_.maxTokens);
            sentinel::simulation::ModelContext testContext;
            testContext.scenario="SARA local model connection test";
            testContext.personaSummary="Synthetic test only.";
            auto probe=candidate->GenerateInvestigatorSuggestion(testContext);
            model_=std::move(candidate);
            modelStatus_=L"Connected: "+wm;
            simSuggestion_=L"Connection test passed.";
            simSettings_.endpoint=Narrow(we);
            simSettings_.model=Narrow(wm);
            sentinel::simulation::SaveSimulationSettings(runtime_->root/"simulation.ini",simSettings_);
            statusText_=L"Local model connected";
        } catch(const std::exception& e) {
            modelStatus_=L"Connection failed: "+Widen(e.what());
            statusText_=L"Local model connection failed";
        }
    }

    void GenerateSimulationSuggestion() {
        if(!model_) {
            simSuggestion_=L"No model adapter configured.";
            return;
        }
        try {
            auto suggestion=model_->GenerateInvestigatorSuggestion(simContext_);
            auto decision=sentinel::simulation::EvaluateSimulationPolicy(simSettings_.ageState,suggestion);
            simSuggestion_=Widen(suggestion);
            policyStatus_=Widen(decision.reason);
            lastEvaluation_=sentinel::simulation::EvaluateResponse(simSettings_.persona,simSettings_.ageState,suggestion);
            if(!decision.allowed) simSuggestion_=L"[BLOCKED BY POLICY] "+simSuggestion_;
            statusText_=decision.allowed?L"Test suggestion generated":L"Suggestion blocked by policy";
        } catch(const std::exception& e) {
            simSuggestion_=L"Model error: "+Widen(e.what());
            statusText_=L"Model request failed";
        }
    }

    static std::string SqlJsonEscape(std::string_view input) {
        std::string out;
        for(unsigned char ch:input) {
            switch(ch) {
                case '\\': out+="\\\\"; break;
                case '"': out+="\\\""; break;
                case '\n': out+="\\n"; break;
                case '\r': out+="\\r"; break;
                case '\t': out+="\\t"; break;
                default:
                    if(ch>=0x20) out+=(char)ch;
                    break;
            }
        }
        return out;
    }

    std::string PersonaContextJson() const {
        const auto& p=simSettings_.persona;
        return std::string("{")+
            "\"age\":"+std::to_string(p.age)+
            ",\"personality\":\""+SqlJsonEscape(p.personality)+"\""+
            ",\"socialStyle\":\""+SqlJsonEscape(p.socialStyle)+"\""+
            ",\"confidence\":\""+SqlJsonEscape(p.confidenceLevel)+"\""+
            ",\"writingStyle\":\""+SqlJsonEscape(p.writingStyle)+"\""+
            ",\"communicationLevel\":\""+SqlJsonEscape(p.communicationLevel)+"\""+
            ",\"cognitiveLevel\":\""+SqlJsonEscape(p.cognitiveLevel)+"\""+
            ",\"slang\":\""+SqlJsonEscape(p.slangLevel)+"\""+
            ",\"grammar\":\""+SqlJsonEscape(p.grammarQuality)+"\""+
            ",\"typos\":\""+SqlJsonEscape(p.typoFrequency)+"\""+
            ",\"emoji\":\""+SqlJsonEscape(p.emojiLevel)+"\""+
            ",\"vocabulary\":\""+SqlJsonEscape(p.vocabularyLevel)+"\""+
            ",\"capitalization\":\""+SqlJsonEscape(p.capitalizationStyle)+"\""+
            ",\"messageLength\":\""+SqlJsonEscape(p.messageLength)+"\""+
            ",\"responseStartMinMs\":"+std::to_string(p.responseStartMinMs)+
            ",\"responseStartMaxMs\":"+std::to_string(p.responseStartMaxMs)+
            ",\"typingMsPerCharMin\":"+std::to_string(p.typingMsPerCharMin)+
            ",\"typingMsPerCharMax\":"+std::to_string(p.typingMsPerCharMax)+
            "}";
    }

    void LogPersonaConversationEvent(
        const std::string& eventKind,
        const std::string& input,
        const std::string& output,
        int startDelayMs=0,
        int typingDelayMs=0)
    {
        try {
            sqlite3_stmt* s{};
            const char* sql=
                "INSERT INTO persona_conversation_logs("
                "id,conversation_id,persona_name,model_name,event_kind,input_text,output_text,"
                "persona_summary,recalled_memory,context_json,start_delay_ms,typing_delay_ms,policy_status) "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)";
            if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) return;

            const auto id=sentinel::Uuid::Random().ToString();
            const auto modelName=model_?model_->Name():"";
            const auto contextJson=PersonaContextJson();
            const auto policy=Narrow(policyStatus_);
            auto bind=[&](int i,const std::string& value){
                sqlite3_bind_text(s,i,value.c_str(),-1,SQLITE_TRANSIENT);
            };
            bind(1,id); bind(2,currentConversationId_); bind(3,simSettings_.persona.name);
            bind(4,modelName); bind(5,eventKind); bind(6,input); bind(7,output);
            bind(8,simContext_.personaSummary); bind(9,simContext_.recalledMemory); bind(10,contextJson);
            sqlite3_bind_int(s,11,startDelayMs); sqlite3_bind_int(s,12,typingDelayMs);
            bind(13,policy);
            sqlite3_step(s);
            sqlite3_finalize(s);
        } catch(...) {}
    }

    void ExportPersonaConversationLog() {
        try {
            const auto stamp=std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            std::string safeName=simSettings_.persona.name.empty()?"persona":simSettings_.persona.name;
            for(char& ch:safeName)
                if(!(std::isalnum((unsigned char)ch) || ch=='-' || ch=='_')) ch='_';

            const auto defaultName=Widen(
                "SARA-persona-log-"+safeName+"-"+std::to_string(stamp)+".txt");
            wchar_t fileName[MAX_PATH]{};
            wcsncpy_s(fileName,defaultName.c_str(),_TRUNCATE);

            const wchar_t filter[]=
                L"Text log (*.txt)\0*.txt\0All files (*.*)\0*.*\0\0";
            OPENFILENAMEW ofn{};
            ofn.lStructSize=sizeof(ofn);
            ofn.hwndOwner=hwnd_;
            ofn.lpstrFilter=filter;
            ofn.lpstrFile=fileName;
            ofn.nMaxFile=MAX_PATH;
            ofn.lpstrDefExt=L"txt";
            ofn.lpstrTitle=L"Save SARA Persona Log";
            ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;

            if(!GetSaveFileNameW(&ofn)) {
                const DWORD err=CommDlgExtendedError();
                if(err==0) {
                    statusText_=L"Persona log export cancelled";
                    return;
                }
                throw std::runtime_error("Windows Save As dialog failed");
            }

            const std::filesystem::path path(fileName);
            std::ofstream out(path,std::ios::binary|std::ios::trunc);
            if(!out) throw std::runtime_error("could not create selected persona log file");

            out<<"SARA PERSONA CONVERSATION DEBUG LOG\n";
            out<<"Persona: "<<simSettings_.persona.name<<"\n";
            out<<"Conversation ID: "<<currentConversationId_<<"\n";
            out<<"Model: "<<(model_?model_->Name():"")<<"\n";
            out<<"Persona summary: "<<BuildPersonaSummary()<<"\n";
            out<<"Profile settings: "<<PersonaContextJson()<<"\n\n";

            sqlite3_stmt* s{};
            const char* sql=
                "SELECT created_utc,event_kind,input_text,output_text,recalled_memory,context_json,"
                "start_delay_ms,typing_delay_ms,policy_status,model_name "
                "FROM persona_conversation_logs WHERE conversation_id=? ORDER BY created_utc,id";
            if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)
                throw std::runtime_error("could not prepare persona log export");
            sqlite3_bind_text(s,1,currentConversationId_.c_str(),-1,SQLITE_TRANSIENT);

            int n=0;
            while(sqlite3_step(s)==SQLITE_ROW) {
                auto col=[&](int i)->std::string{
                    const auto* p=(const char*)sqlite3_column_text(s,i);
                    return p?p:"";
                };
                out<<"============================================================\n";
                out<<"Event "<<(++n)<<" | "<<col(0)<<" | "<<col(1)<<"\n";
                out<<"Model: "<<col(9)<<"\n";
                out<<"Start delay: "<<sqlite3_column_int(s,6)<<" ms\n";
                out<<"Typing delay: "<<sqlite3_column_int(s,7)<<" ms\n";
                out<<"Policy: "<<col(8)<<"\n";
                out<<"INPUT:\n"<<col(2)<<"\n\n";
                out<<"OUTPUT:\n"<<col(3)<<"\n\n";
                out<<"RECALLED MEMORY / FACTS:\n"<<col(4)<<"\n\n";
                out<<"PERSONA SETTINGS SNAPSHOT:\n"<<col(5)<<"\n\n";
            }
            sqlite3_finalize(s);

            out<<"============================================================\n";
            out<<"VISIBLE CONVERSATION TRANSCRIPT\n";
            for(const auto& turn:simContext_.history) {
                const char* who=
                    turn.speaker==sentinel::simulation::ChatTurn::Speaker::Investigator?"OTHER PERSON":
                    turn.speaker==sentinel::simulation::ChatTurn::Speaker::SyntheticSubject?"PERSONA":"MODEL/SYSTEM";
                out<<who<<": "<<turn.text<<"\n";
            }
            out.close();
            if(!out) throw std::runtime_error("failed while writing persona log");

            statusText_=L"Persona log saved: "+path.wstring();
            MessageBoxW(hwnd_,
                (L"Persona log saved directly to:\n\n"+path.wstring()).c_str(),
                L"Persona Log Saved",MB_OK|MB_ICONINFORMATION);
        } catch(const std::exception& e) {
            statusText_=L"Persona log export failed";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Persona Log Export Failed",MB_OK|MB_ICONERROR);
        }
    }

    void PreserveSimulationTranscript() {
        if(cases_.empty()) {
            MessageBoxW(hwnd_,L"Create or select a case before preserving the transcript.",L"SARA",MB_OK|MB_ICONINFORMATION);
            return;
        }
        try {
            auto exportDir=runtime_->root/"exports";
            std::filesystem::create_directories(exportDir);
            auto path=exportDir/"simulation-transcript.txt";
            std::ofstream out(path,std::ios::trunc);
            out<<"SARA Simulation Transcript\n";
            out<<"Scenario: "<<simSettings_.scenario.name<<"\n";
            out<<"Persona: "<<simSettings_.persona.name<<"\n";
            out<<"Age state: "<<sentinel::simulation::ToString(simSettings_.ageState)<<"\n\n";
            for(const auto& turn:simContext_.history) {
                const char* who=turn.speaker==sentinel::simulation::ChatTurn::Speaker::Investigator?"Investigator":
                    turn.speaker==sentinel::simulation::ChatTurn::Speaker::SyntheticSubject?"Synthetic Subject":"Model Suggestion";
                out<<who<<": "<<turn.text<<"\n";
            }
            out.close();

            auto key=runtime_->keys.GetCaseKey(cases_[selectedCase_].id);
            sentinel::EvidenceService svc(runtime_->root/"evidence",runtime_->db,runtime_->random,runtime_->hash,runtime_->cipher,runtime_->audit);
            svc.Import({cases_[selectedCase_].id,path,0,sentinel::UserId::Random()},key.Span());
            evidence_=runtime_->Evidence(cases_[selectedCase_].id);
            statusText_=L"Simulation transcript preserved as encrypted evidence";
        } catch(const std::exception& e) {
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Preserve Transcript Failed",MB_OK|MB_ICONERROR);
        }
    }

    void QueueOperatorTestMessage() {
        RequestLatestSuggestionApproval();
        page_=Page::Supervisor;
        statusText_=L"Message queued for supervisor approval";
    }

    void RequestLatestSuggestionApproval() {
        std::wstring candidate=simSuggestion_;
        if(candidate.empty() || candidate==L"No suggestion generated yet") {
            statusText_=L"Generate a model suggestion first";
            return;
        }
        auto text=Narrow(candidate);
        auto decision=sentinel::simulation::EvaluateSimulationPolicy(simSettings_.ageState,text);
        if(!decision.allowed) {
            policyStatus_=Widen(decision.reason);
            statusText_=L"Policy blocked approval request";
            return;
        }
        approvals_.push_back(sentinel::operations::CreateApprovalRequest("message:local-sim:"+text,"local-investigator"));
        statusText_=L"Supervisor approval requested";
    }

    void ApproveFirstPending() {
        for(auto& a:approvals_) {
            if(a.status==sentinel::operations::ApprovalStatus::Pending) {
                sentinel::operations::Approve(a,"local-supervisor","Approved in Sentinel supervisor console");
                const std::string prefix="message:local-sim:";
                const std::string mediaPrefix="media:local-sim:";
                if(a.action.rfind(prefix,0)==0 && messagingAdapter_) {
                    messagingAdapter_->QueueOperatorApproved("local-sim",a.action.substr(prefix.size()));
                    statusText_=L"Supervisor approval recorded and message queued";
                } else if(a.action.rfind(mediaPrefix,0)==0 && messagingAdapter_) {
                    const auto payload=a.action.substr(mediaPrefix.size());
                    const auto p1=payload.find('|');
                    const auto p2=p1==std::string::npos?std::string::npos:payload.find('|',p1+1);
                    const auto p3=p2==std::string::npos?std::string::npos:payload.find('|',p2+1);
                    if(p1!=std::string::npos && p2!=std::string::npos && p3!=std::string::npos) {
                        const auto mediaId=payload.substr(0,p1);
                        const auto sha=payload.substr(p1+1,p2-p1-1);
                        const auto path=payload.substr(p2+1,p3-p2-1);
                        const auto caption=payload.substr(p3+1);
                        const auto it=std::find_if(personaMedia_.begin(),personaMedia_.end(),[&](const auto& m){
                            return m.id==mediaId && m.approved && m.sha256==sha && m.storedPath==path;
                        });
                        if(it!=personaMedia_.end()) {
                            messagingAdapter_->QueueOperatorApprovedMedia("local-sim",caption,path,sha);
                            statusText_=L"Supervisor approved benign persona image; media queued";
                        } else {
                            statusText_=L"Media approval rejected: asset missing, changed, or no longer approved";
                        }
                    }
                } else {
                    statusText_=L"Supervisor approval recorded";
                }
                return;
            }
        }
        statusText_=L"No pending approvals";
    }

    std::string SelectedStateCode() const {
        int sel=(int)SendMessageW(operatingStateCombo_,CB_GETCURSEL,0,0);
        if(sel==CB_ERR) return {};
        wchar_t buf[128]{};
        SendMessageW(operatingStateCombo_,CB_GETLBTEXT,sel,(LPARAM)buf);
        std::wstring text=buf;
        const auto l=text.find_last_of(L'(');
        const auto r=text.find_last_of(L')');
        if(l==std::wstring::npos || r==std::wstring::npos || r<=l+1) return {};
        return Narrow(text.substr(l+1,r-l-1));
    }

    void LoadOperatingJurisdiction() {
        operatingStateCode_.clear();
        sqlite3_stmt* s{};
        if(sqlite3_prepare_v2(runtime_->db.Handle(),
            "SELECT region_code FROM operation_jurisdiction WHERE operation_key='local-default' LIMIT 1",
            -1,&s,nullptr)==SQLITE_OK && sqlite3_step(s)==SQLITE_ROW) {
            const auto* p=(const char*)sqlite3_column_text(s,0);
            if(p) operatingStateCode_=p;
        }
        sqlite3_finalize(s);
        if(operatingStateCode_.empty()) operatingStateCode_="LA";

        int count=(int)SendMessageW(operatingStateCombo_,CB_GETCOUNT,0,0);
        for(int i=0;i<count;i++) {
            wchar_t buf[128]{};
            SendMessageW(operatingStateCombo_,CB_GETLBTEXT,i,(LPARAM)buf);
            std::wstring needle=L"("+Widen(operatingStateCode_)+L")";
            if(std::wstring(buf).find(needle)!=std::wstring::npos) {
                SendMessageW(operatingStateCombo_,CB_SETCURSEL,i,0);
                break;
            }
        }
        RefreshJurisdictionStatus();
    }

    void RefreshJurisdictionStatus() {
        if(operatingStateCode_.empty()) {
            jurisdictionStatus_=L"No operating jurisdiction selected";
            return;
        }
        auto profile=runtime_->jurisdictionRules.LatestProfile(sentinel::channels::RuleLayerType::State,"US",operatingStateCode_);
        if(!profile) {
            jurisdictionStatus_=L"No rules profile installed for "+Widen(operatingStateCode_)+L" — automation locked to review";
            return;
        }
        const wchar_t* review=L"DRAFT";
        if(profile->reviewStatus==sentinel::channels::LegalReviewStatus::LegallyReviewed) review=L"LEGALLY REVIEWED";
        else if(profile->reviewStatus==sentinel::channels::LegalReviewStatus::Active) review=L"ACTIVE";
        else if(profile->reviewStatus==sentinel::channels::LegalReviewStatus::Expired) review=L"EXPIRED";
        jurisdictionStatus_=Widen(profile->name)+L" | "+review+L" | version "+Widen(profile->version);
        if(!profile->AutomationLegallyActive()) jurisdictionStatus_+=L" | AUTO-SEND LOCKED";
    }

    void ApplyOperatingJurisdiction() {
        const auto state=SelectedStateCode();
        if(state.empty()) {
            statusText_=L"Select an operating state";
            return;
        }
        operatingStateCode_=state;
        auto profile=runtime_->jurisdictionRules.LatestProfile(sentinel::channels::RuleLayerType::State,"US",state);
        const auto federal=runtime_->jurisdictionRules.LatestProfile(
            sentinel::channels::RuleLayerType::Federal,"US");
        const std::string federalId=federal?federal->id:"";
        const std::string stateId=profile?profile->id:"";
        runtime_->jurisdictionRules.SelectForOperation(
            "local-default","US",state,federalId,stateId,{},"local-investigator");
        RefreshJurisdictionStatus();
        if(!profile) {
            statusText_=L"State selected; no rules pack installed, automation remains review-only";
        } else if(profile->AutomationLegallyActive()) {
            statusText_=L"Operating state and ACTIVE legal rules profile applied";
        } else {
            statusText_=L"Operating state applied; rules profile requires legal activation before auto-send";
        }
    }

    void ToggleAgency() {
        agencyConfig_.endpoint=Narrow(EditText(agencyEndpointEdit_));
        agencyConfig_.agencyId=Narrow(EditText(agencyIdEdit_));
        if(agencyConfig_.endpoint.empty() || agencyConfig_.agencyId.empty()) {
            statusText_=L"Enter agency endpoint and agency ID first";
            return;
        }
        agencyConfig_.enabled=!agencyConfig_.enabled;
        statusText_=agencyConfig_.enabled?L"Agency sync configuration enabled":L"Agency sync configuration disabled";
    }

    void EnqueueAgencySnapshot() {
        agencyQueue_.Enqueue({
            "sync-"+std::to_string(agencyQueue_.Items().size()+1),
            sentinel::agency::SyncItemType::AuditRecord,
            "audit-count:"+std::to_string(runtime_->AuditCount()),0,false});
        statusText_=L"Encrypted-sync work item queued locally";
    }

    void DrawPersona(float w,float h) {
        PageTitle(L"Persona & Policy",L"Structured synthetic identity, behavior, scenario, age state, and pacing");
        const float x=kSidebar+28.0f;
        const float y=kHeader+104.0f;
        const float gap=14.0f;
        const float contentW=w-x-28.0f;
        const float colW=(contentW-gap)/2.0f;
        const float rightX=x+colW+gap;

        Rounded(x,y-46,contentW,38,brush_.panel.Get(),brush_.border.Get(),8);
        TextLine(L"Saved Persona",x+12,y-42,92,28,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"persona_load",L"Load",x+contentW-266,y-42,72,28,false);
        AddButton(L"persona_delete",L"Delete",x+contentW-186,y-42,72,28,false);
        AddButton(L"persona_save",L"Save",x+contentW-106,y-42,88,28,true);

        // Identity & background
        Rounded(x,y,colW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Identity & Background",x+18,y+12,colW-36,32,h1Fmt_.Get(),brush_.text.Get());

        const float lx=x+20, lf=x+132, lw=colW-152;
        float row=y+54;

        TextLine(L"Name",lx,row,96,30,tinyFmt_.Get(),brush_.muted.Get());
        row+=42;

        TextLine(L"Age",lx,row,46,30,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Age state",lx+136,row,72,30,tinyFmt_.Get(),brush_.muted.Get());
        row+=42;

        TextLine(L"Gender",lx,row,58,30,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Pronouns",lx+210,row,62,30,tinyFmt_.Get(),brush_.muted.Get());
        row+=42;

        TextLine(L"Location",lx,row,96,30,tinyFmt_.Get(),brush_.muted.Get());
        row+=42;

        TextLine(L"Occupation",lx,row,96,30,tinyFmt_.Get(),brush_.muted.Get());
        row+=42;

        TextLine(L"Education",lx,row,96,30,tinyFmt_.Get(),brush_.muted.Get());
        row+=42;

        TextLine(L"Relationship",lx,row,96,30,tinyFmt_.Get(),brush_.muted.Get());

        // Behavior & context
        Rounded(rightX,y,colW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Behavior & Context",rightX+18,y+12,colW-36,32,h1Fmt_.Get(),brush_.text.Get());

        const float rx=rightX+20, rf=rightX+132, rw=colW-152;
        row=y+54;

        TextLine(L"Personality",rx,row,96,30,tinyFmt_.Get(),brush_.muted.Get());
        row+=42;

        TextLine(L"Social style",rx,row,96,30,tinyFmt_.Get(),brush_.muted.Get());
        row+=42;

        TextLine(L"Confidence",rx,row,96,30,tinyFmt_.Get(),brush_.muted.Get());
        row+=42;

        TextLine(L"Interests",rx,row,96,30,tinyFmt_.Get(),brush_.muted.Get());
        row+=42;

        TextLine(L"Writing style",rx,row,96,30,tinyFmt_.Get(),brush_.muted.Get());
        row+=42;

        TextLine(L"Family",rx,row,96,30,tinyFmt_.Get(),brush_.muted.Get());
        row+=42;

        TextLine(L"Background",rx,row,96,30,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"persona_generate_behavior",L"Generate Behavior",rightX+colW-158,row,140,28,false);

        // Scenario & pacing
        const float sy=y+396;
        Rounded(x,sy,contentW,144,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Scenario, Policy & Pacing",x+18,sy+10,300,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Scenario",x+20,sy+50,68,30,tinyFmt_.Get(),brush_.muted.Get());

        TextLine(L"Objective",x+316,sy+50,70,30,tinyFmt_.Get(),brush_.muted.Get());

        TextLine(L"Seed",x+20,sy+92,48,30,tinyFmt_.Get(),brush_.muted.Get());

        TextLine(L"Start delay ms",x+164,sy+92,82,30,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"to",x+342,sy+92,24,30,tinyFmt_.Get(),brush_.muted.Get());

        AddButton(L"persona_save",L"Save Persona & Policy",x+contentW-210,sy+88,190,38,true);

        // Compact policy status
        TextLine(L"Policy",x+474,sy+92,52,30,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(policyStatus_,x+530,sy+88,contentW-760,38,tinyFmt_.Get(),brush_.cyan.Get());

        // Texting voice / communication imperfections.
        const float ty=sy+158;
        Rounded(x,ty,contentW,142,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Texting Voice",x+18,ty+10,180,28,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Age influences vocabulary and message length; these controls tune how polished or casual the persona sounds.",
            x+205,ty+10,contentW-225,28,tinyFmt_.Get(),brush_.muted.Get());

        TextLine(L"Communication",x+20,ty+48,88,24,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Cognitive",x+275,ty+48,60,24,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Slang",x+485,ty+48,48,24,tinyFmt_.Get(),brush_.muted.Get());

        TextLine(L"Grammar",x+20,ty+88,56,24,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Typos",x+275,ty+88,48,24,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Emoji",x+485,ty+88,48,24,tinyFmt_.Get(),brush_.muted.Get());

        // Approved benign persona media library.
        const float my=ty+156;
        Rounded(x,my,contentW,170,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Persona Media Library",x+18,my+10,250,28,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Imported images are unapproved until explicitly reviewed. Benign ordinary-use media only.",
            x+280,my+10,contentW-300,28,tinyFmt_.Get(),brush_.muted.Get());

        AddButton(L"media_import",L"Import Picture",x+18,my+46,126,32,true);
        AddButton(L"media_approve",L"Approve / Revoke",x+154,my+46,142,32,false);
        AddButton(L"media_delete",L"Delete",x+306,my+46,88,32,false);

        float mediaY=my+88;
        if(personaMedia_.empty()) {
            TextLine(L"No persona pictures imported.",x+20,mediaY,contentW-40,24,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(size_t i=0;i<personaMedia_.size() && i<3;i++) {
                const auto& item=personaMedia_[i];
                const bool selected=(int)i==selectedPersonaMedia_;
                Rounded(x+18,mediaY,contentW-36,24,
                    selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),6);
                const std::wstring state=item.approved?L"APPROVED":L"UNAPPROVED";
                TextLine(Widen(item.originalName),x+28,mediaY,300,24,tinyFmt_.Get(),brush_.text.Get());
                TextLine(state,x+340,mediaY,90,24,tinyFmt_.Get(),item.approved?brush_.green.Get():brush_.yellow.Get());
                TextLine(Widen(item.sha256.substr(0,16))+L"...",x+440,mediaY,160,24,tinyFmt_.Get(),brush_.muted.Get());
                TextLine(Widen(item.tags),x+610,mediaY,contentW-650,24,tinyFmt_.Get(),brush_.muted.Get());
                buttons_.push_back({{x+18,mediaY,x+contentW-18,mediaY+24},L"media:"+std::to_wstring(i)});
                mediaY+=28;
            }
        }
    }

    void RegisterCurrentModel() {
        auto endpoint=Narrow(EditText(modelEndpointEdit_));
        auto name=Narrow(EditText(modelNameEdit_));
        if(endpoint.empty() || name.empty()) {
            statusText_=L"Configure a model endpoint and name first";
            return;
        }
        auto& item=modelRegistry_.Register(endpoint,name);
        selectedRegistryModel_=(int)(&item-modelRegistry_.Models().data());
        modelRegistry_.Save(runtime_->root/"model-registry.tsv");
        statusText_=L"Model registered as candidate";
    }

    void EvaluateSelectedRegistryModel() {
        if(selectedRegistryModel_<0 || selectedRegistryModel_>=(int)modelRegistry_.Models().size()) {
            statusText_=L"Select a registered model first";
            return;
        }
        auto& item=modelRegistry_.Models()[(size_t)selectedRegistryModel_];
        auto started=std::chrono::steady_clock::now();
        try {
            auto candidate=sentinel::simulation::CreateOpenAICompatibleModel(item.endpoint,item.modelName);
            sentinel::simulation::ModelContext ctx;
            ctx.scenario="SARA Model Lab candidate evaluation";
            ctx.personaSummary=simContext_.personaSummary;
            ctx.history.push_back({sentinel::simulation::ChatTurn::Speaker::Investigator,"Hello, introduce yourself briefly."});
            auto reply=candidate->GenerateSyntheticReply("Hello, introduce yourself briefly.",ctx);
            auto eval=sentinel::simulation::EvaluateResponse(simSettings_.persona,simSettings_.ageState,reply);
            item.evaluationScore=eval.score;
            item.latencyMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
            lastEvaluation_=eval;
            modelRegistry_.Save(runtime_->root/"model-registry.tsv");
            statusText_=L"Candidate model evaluation complete";
        } catch(const std::exception& e) {
            item.evaluationScore=0;
            item.latencyMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
            statusText_=L"Model evaluation failed";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Model Evaluation Failed",MB_OK|MB_ICONERROR);
        }
    }

    void ApproveSelectedRegistryModel() {
        if(selectedRegistryModel_<0 || selectedRegistryModel_>=(int)modelRegistry_.Models().size()) return;
        modelRegistry_.Approve((size_t)selectedRegistryModel_);
        modelRegistry_.Save(runtime_->root/"model-registry.tsv");
        statusText_=L"Candidate model approved";
    }

    void ActivateSelectedRegistryModel() {
        if(selectedRegistryModel_<0 || selectedRegistryModel_>=(int)modelRegistry_.Models().size()) return;
        auto& item=modelRegistry_.Models()[(size_t)selectedRegistryModel_];
        modelRegistry_.Activate((size_t)selectedRegistryModel_);
        if(modelRegistry_.ActiveIndex()==selectedRegistryModel_) {
            model_=sentinel::simulation::CreateOpenAICompatibleModel(item.endpoint,item.modelName);
            SetWindowTextW(modelEndpointEdit_,Widen(item.endpoint).c_str());
            SetWindowTextW(modelNameEdit_,Widen(item.modelName).c_str());
            simSettings_.endpoint=item.endpoint;
            simSettings_.model=item.modelName;
            sentinel::simulation::SaveSimulationSettings(runtime_->root/"simulation.ini",simSettings_);
            modelStatus_=L"Active registry model: "+Widen(item.modelName);
            modelRegistry_.Save(runtime_->root/"model-registry.tsv");
            statusText_=L"Approved model activated";
        } else statusText_=L"Model must be approved before activation";
    }

    void RollbackRegistryModel() {
        if(!modelRegistry_.Rollback()) {
            statusText_=L"No prior active model is available for rollback";
            return;
        }
        int idx=modelRegistry_.ActiveIndex();
        if(idx>=0) {
            auto& item=modelRegistry_.Models()[(size_t)idx];
            model_=sentinel::simulation::CreateOpenAICompatibleModel(item.endpoint,item.modelName);
            modelStatus_=L"Rolled back to: "+Widen(item.modelName);
        }
        modelRegistry_.Save(runtime_->root/"model-registry.tsv");
        statusText_=L"Model rollback completed";
    }

    void StageLatestTrainingExample() {
        try {
            auto staged=runtime_->trainingReviews.StageLatestReply(currentConversationId_);
            if(!staged) {
                statusText_=L"No logged persona reply is available to stage";
                return;
            }
            selectedTrainingReviewId_=staged->id;
            statusText_=staged->status==sentinel::simulation::TrainingReviewStatus::Pending
                ? L"Latest persona reply staged for training review"
                : L"Latest persona reply is already in the review set";
        } catch(const std::exception& e) {
            statusText_=L"Unable to stage training example";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Training Review",MB_OK|MB_ICONERROR);
        }
    }

    void ReviewStagedTrainingExample(bool approve) {
        if(selectedTrainingReviewId_.empty()) {
            statusText_=L"Stage a persona reply before reviewing it";
            return;
        }
        try {
            const auto status=approve
                ? sentinel::simulation::TrainingReviewStatus::Approved
                : sentinel::simulation::TrainingReviewStatus::Rejected;
            if(runtime_->trainingReviews.Review(
                    selectedTrainingReviewId_,status,"local-reviewer",
                    approve?"Approved in Sentinel Model Lab":"Rejected in Sentinel Model Lab")) {
                statusText_=approve
                    ? L"Training example approved for dataset export"
                    : L"Training example rejected";
            } else {
                statusText_=L"Training review item could not be updated";
            }
        } catch(const std::exception& e) {
            statusText_=L"Training review failed";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Training Review",MB_OK|MB_ICONERROR);
        }
    }

    void ExportApprovedTrainingDataset() {
        try {
            auto exportDir=runtime_->root/"exports";
            std::filesystem::create_directories(exportDir);
            auto path=exportDir/"sentinel-approved-training.jsonl";
            const auto count=runtime_->trainingReviews.ExportApprovedJsonl(path);
            statusText_=L"Exported "+std::to_wstring(count)+L" approved training examples";
            ShellExecuteW(hwnd_,L"open",exportDir.wstring().c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        } catch(const std::exception& e) {
            statusText_=L"Training dataset export failed";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Training Dataset Export",MB_OK|MB_ICONERROR);
        }
    }

    void DrawModelLab(float w,float h) {
        PageTitle(L"Model Lab",L"Curate reviewed training examples, evaluate candidates, and control deployment");
        const float x=kSidebar+28.0f;
        const float y=kHeader+104.0f;
        const float contentW=w-x-28.0f;

        Rounded(x,y,contentW,300,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Model Registry & Persona Controls",x+18,y+12,340,30,h1Fmt_.Get(),brush_.text.Get());

        const float gap=10.0f;
        const float bw=136.0f;
        float bx=x+18;
        AddButton(L"model_register",L"Register Current",bx,y+50,bw,36,true); bx+=bw+gap;
        AddButton(L"model_eval",L"Evaluate",bx,y+50,bw,36,false); bx+=bw+gap;
        AddButton(L"model_approve",L"Approve",bx,y+50,bw,36,false); bx+=bw+gap;
        AddButton(L"model_activate",L"Activate",bx,y+50,bw,36,false); bx+=bw+gap;
        AddButton(L"model_rollback",L"Rollback",bx,y+50,bw,36,false);
        AddButton(L"learning_toggle",
            simSettings_.learningMode?L"Learning: ON":L"Learning: OFF",
            x+contentW-168,y+50,150,36,simSettings_.learningMode);

        TextLine(L"Trigger",x+18,y+110,64,24,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Response",x+318,y+110,60,24,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"rule_wording_toggle",
            responseRuleExactWording_?L"Wording: Exact":L"Wording: Persona",
            x+contentW-620,y+105,126,32,responseRuleExactWording_);
        AddButton(L"rule_add_smart",L"Add Smart",x+contentW-484,y+105,94,32,true);
        AddButton(L"rule_add_contains",L"Add Contains",x+contentW-380,y+105,104,32,false);
        AddButton(L"rule_add_exact",L"Add Exact",x+contentW-266,y+105,94,32,false);
        AddButton(L"rule_test",L"Test Match",x+contentW-162,y+105,92,32,false);
        AddButton(L"rule_clear",L"Clear",x+contentW-60,y+105,54,32,false);

        auto rules=PersonaResponseRules(3);
        TextLine(L"Saved Rules ("+std::to_wstring(PersonaResponseRuleCount())+L")",
            x+18,y+152,190,24,smallFmt_.Get(),brush_.cyan.Get());
        TextLine(L"MATCH",x+34,y+181,70,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"WORDING",x+114,y+181,84,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"TRIGGER",x+210,y+181,250,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"RESPONSE",x+470,y+181,contentW-610,18,tinyFmt_.Get(),brush_.muted.Get());

        float ruleY=y+202;
        if(rules.empty()) {
            TextLine(L"No response rules saved for this persona.",
                x+34,ruleY,contentW-68,28,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(const auto& rule:rules) {
                Rounded(x+22,ruleY,contentW-44,30,brush_.sidebar.Get(),brush_.border.Get(),6);
                TextLine(Widen(rule.matchType),x+34,ruleY+4,70,22,tinyFmt_.Get(),brush_.cyan.Get());
                TextLine(rule.responseMode=="exact"?L"Exact":L"Persona",
                    x+114,ruleY+4,84,22,tinyFmt_.Get(),brush_.text.Get());
                std::wstring trigger=Widen(rule.trigger);
                if(trigger.size()>34) trigger=trigger.substr(0,31)+L"...";
                std::wstring response=Widen(rule.response);
                if(response.size()>58) response=response.substr(0,55)+L"...";
                TextLine(trigger,x+210,ruleY+4,250,22,tinyFmt_.Get(),brush_.text.Get());
                TextLine(response,x+470,ruleY+4,contentW-610,22,tinyFmt_.Get(),brush_.muted.Get());
                AddButton(L"rule_delete:"+std::to_wstring(rule.id),L"Delete",
                    x+contentW-112,ruleY+2,78,26,false);
                ruleY+=34;
            }
        }

        Rounded(x,y+314,contentW,220,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Registered Models",x+18,y+326,260,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"MODEL",x+34,y+363,280,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STATE",x+332,y+363,100,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"SCORE",x+452,y+363,72,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"LATENCY",x+544,y+363,80,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"ENDPOINT",x+646,y+363,contentW-680,20,tinyFmt_.Get(),brush_.muted.Get());

        float yy=y+389;
        if(modelRegistry_.Models().empty()) {
            Rounded(x+22,yy,contentW-44,52,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"No registered models. Configure one in Simulation Lab, then choose Register Current.",
                x+34,yy+7,contentW-68,38,bodyFmt_.Get(),brush_.muted.Get());
        }

        for(size_t i=0;i<modelRegistry_.Models().size() && i<3;i++) {
            const auto& m=modelRegistry_.Models()[i];
            const bool selected=(int)i==selectedRegistryModel_;
            Rounded(x+22,yy,contentW-44,52,selected?brush_.panel2.Get():brush_.sidebar.Get(),
                selected?brush_.cyan.Get():brush_.border.Get(),8);
            TextLine(Widen(m.modelName),x+34,yy+4,280,22,smallFmt_.Get(),brush_.text.Get());
            TextLine(Widen(sentinel::simulation::ToString(m.stage)),x+332,yy+4,100,22,tinyFmt_.Get(),
                m.stage==sentinel::simulation::ModelStage::Active?brush_.green.Get():brush_.cyan.Get());
            TextLine(std::to_wstring(m.evaluationScore),x+452,yy+4,72,22,tinyFmt_.Get(),brush_.text.Get());
            TextLine(std::to_wstring(m.latencyMs)+L" ms",x+544,yy+4,80,22,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(m.endpoint),x+646,yy+4,contentW-680,22,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"ID "+Widen(m.id),x+34,yy+28,contentW-68,18,tinyFmt_.Get(),brush_.muted.Get());
            buttons_.push_back({{x+22,yy,x+contentW-22,yy+52},L"regmodel:"+std::to_wstring(i)});
            yy+=62;
        }

        Rounded(x,y+548,contentW,132,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Last Evaluation",x+18,y+560,220,30,h1Fmt_.Get(),brush_.text.Get());

        const float metricY=y+608;
        Rounded(x+22,metricY,110,48,brush_.sidebar.Get(),brush_.border.Get(),8);
        TextLine(L"Score",x+34,metricY+4,86,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(std::to_wstring(lastEvaluation_.score),x+34,metricY+20,86,22,bodyFmt_.Get(),
            lastEvaluation_.score>=80?brush_.green.Get():lastEvaluation_.score>=50?brush_.yellow.Get():brush_.red.Get());

        Rounded(x+144,metricY,180,48,brush_.sidebar.Get(),brush_.border.Get(),8);
        TextLine(L"Policy",x+156,metricY+4,156,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(lastEvaluation_.policyAllowed?L"Allowed":L"Blocked",x+156,metricY+20,156,22,smallFmt_.Get(),
            lastEvaluation_.policyAllowed?brush_.green.Get():brush_.red.Get());

        Rounded(x+336,metricY,220,48,brush_.sidebar.Get(),brush_.border.Get(),8);
        TextLine(L"Persona consistency",x+348,metricY+4,196,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(lastEvaluation_.personaConsistent?L"Consistent":L"Contradiction",x+348,metricY+20,196,22,smallFmt_.Get(),
            lastEvaluation_.personaConsistent?brush_.green.Get():brush_.yellow.Get());

        if(!lastEvaluation_.warnings.empty()) {
            TextLine(Widen(lastEvaluation_.warnings.front()),x+580,metricY,contentW-602,48,tinyFmt_.Get(),brush_.yellow.Get());
        }

        const float reviewY=y+694;
        Rounded(x,reviewY,contentW,118,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Reviewed Learning Dataset",x+18,reviewY+10,300,28,h1Fmt_.Get(),brush_.text.Get());

        auto reviewCounts=runtime_->trainingReviews.Counts();
        TextLine(L"Pending "+std::to_wstring(reviewCounts.pending)+
                 L"   Approved "+std::to_wstring(reviewCounts.approved)+
                 L"   Rejected "+std::to_wstring(reviewCounts.rejected),
                 x+330,reviewY+12,contentW-350,24,smallFmt_.Get(),brush_.muted.Get());

        float tbx=x+18;
        AddButton(L"training_stage",L"Stage Latest Reply",tbx,reviewY+54,150,36,true); tbx+=160;
        AddButton(L"training_approve",L"Approve Staged",tbx,reviewY+54,150,36,false); tbx+=160;
        AddButton(L"training_reject",L"Reject Staged",tbx,reviewY+54,140,36,false); tbx+=150;
        AddButton(L"training_export",L"Export Approved",tbx,reviewY+54,150,36,false);

        const std::wstring reviewHint=selectedTrainingReviewId_.empty()
            ? L"No training example staged in this session."
            : L"Selected review item: "+Widen(selectedTrainingReviewId_);
        TextLine(reviewHint,x+18,reviewY+94,contentW-36,18,tinyFmt_.Get(),brush_.muted.Get());
    }

    void DrawMessaging(float w,float h) {
        PageTitle(L"Messaging",L"Operator-approved messaging core and local conversation queue");
        const float x=kSidebar+28.0f;
        const float y=kHeader+104.0f;
        const float contentW=w-x-28.0f;
        const float gap=14.0f;
        const float infoW=(contentW-gap)*0.42f;
        const float queueW=contentW-gap-infoW;

        Rounded(x,y,infoW,238,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Adapter Status",x+18,y+12,infoW-36,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Provider",x+20,y+56,96,26,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(Widen(messagingAdapter_?messagingAdapter_->ProviderName():"Not configured"),
            x+122,y+54,infoW-142,28,smallFmt_.Get(),brush_.text.Get());

        TextLine(L"Connection",x+20,y+94,96,26,tinyFmt_.Get(),brush_.muted.Get());
        StatusDot(x+130,y+107,4,messagingAdapter_&&messagingAdapter_->Connected()?brush_.green.Get():brush_.red.Get());
        TextLine(messagingAdapter_&&messagingAdapter_->Connected()?L"Local test adapter online":L"Offline",
            x+142,y+92,infoW-162,28,smallFmt_.Get(),messagingAdapter_&&messagingAdapter_->Connected()?brush_.green.Get():brush_.red.Get());

        TextLine(L"Outbound control",x+20,y+132,96,26,tinyFmt_.Get(),brush_.muted.Get());
        Text(L"Messages must pass the operator / supervisor approval path before they are queued.",
            x+122,y+132,infoW-142,48,smallFmt_.Get(),brush_.cyan.Get());

        Text(L"This development build has no live third-party messaging transport connected.",
            x+20,y+190,infoW-40,34,tinyFmt_.Get(),brush_.muted.Get());

        auto msgs=messagingAdapter_?messagingAdapter_->Poll("local-sim"):std::vector<sentinel::operations::NormalizedMessage>{};
        const float qx=x+infoW+gap;
        Rounded(qx,y,queueW,238,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Conversation Queue",qx+18,y+12,queueW-36,30,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"local-sim",qx+18,y+50,150,24,tinyFmt_.Get(),brush_.cyan.Get());
        TextLine(std::to_wstring(msgs.size())+L" approved / queued",qx+180,y+50,queueW-198,24,tinyFmt_.Get(),brush_.muted.Get());

        float yy=y+82;
        if(msgs.empty()) {
            Rounded(qx+18,yy,queueW-36,50,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"No approved messages queued.",qx+32,yy+6,queueW-64,38,bodyFmt_.Get(),brush_.muted.Get());
        } else {
            for(size_t i=0;i<msgs.size() && i<3;i++) {
                Rounded(qx+18,yy,queueW-36,46,brush_.sidebar.Get(),brush_.border.Get(),8);
                std::wstring rowText=Widen(msgs[i].text);
                if(!msgs[i].mediaPath.empty()) rowText=L"[IMAGE] "+rowText+L" | "+std::filesystem::path(Widen(msgs[i].mediaPath)).filename().wstring();
                TextLine(rowText,qx+30,yy+4,queueW-60,38,smallFmt_.Get(),brush_.text.Get());
                yy+=54;
            }
        }

        Rounded(x,y+254,contentW,286,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Operator Workflow",x+18,y+266,contentW-36,30,h1Fmt_.Get(),brush_.text.Get());

        const float cardY=y+310;
        const float stepW=(contentW-76)/3.0f;
        const wchar_t* stepTitles[]={L"1. Generate",L"2. Approve",L"3. Preserve"};
        const wchar_t* stepText[]={
            L"Create a candidate reply in Simulation Lab.",
            L"Send it through Supervisor for human approval.",
            L"Preserve the simulation transcript into encrypted case evidence."
        };
        for(int i=0;i<3;i++) {
            float sx=x+20+i*(stepW+18);
            Rounded(sx,cardY,stepW,112,brush_.sidebar.Get(),brush_.border.Get(),9);
            TextLine(stepTitles[i],sx+14,cardY+10,stepW-28,24,bodyFmt_.Get(),brush_.cyan.Get());
            Text(stepText[i],sx+14,cardY+42,stepW-28,54,smallFmt_.Get(),brush_.text.Get());
        }

        AddButton(L"msg_queue",L"Request Approval",x+20,y+442,180,38,true);
        AddButton(L"sim_preserve",L"Preserve Transcript",x+214,y+442,190,38,false);
    }

    void DrawSupervisor(float w,float h) {
        PageTitle(L"Supervisor",L"Human approval ledger with SHA-256 action hashes");
        const float x=kSidebar+28.0f;
        const float y=kHeader+104.0f;
        const float contentW=w-x-28.0f;

        size_t pending=0,approved=0,rejected=0;
        for(const auto& a:approvals_) {
            if(a.status==sentinel::operations::ApprovalStatus::Pending) ++pending;
            else if(a.status==sentinel::operations::ApprovalStatus::Approved) ++approved;
            else ++rejected;
        }

        const float cardW=(contentW-28)/3.0f;
        struct M{const wchar_t* label;size_t value;ID2D1Brush* brush;};
        M ms[]={{L"Pending",pending,brush_.yellow.Get()},{L"Approved",approved,brush_.green.Get()},{L"Rejected",rejected,brush_.red.Get()}};
        for(int i=0;i<3;i++) {
            float cx=x+i*(cardW+14);
            Rounded(cx,y,cardW,92,brush_.panel.Get(),brush_.border.Get(),10);
            TextLine(ms[i].label,cx+18,y+14,cardW-36,20,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(std::to_wstring(ms[i].value),cx+18,y+36,cardW-36,40,bigFmt_.Get(),ms[i].brush);
        }

        Rounded(x,y+108,contentW,90,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Approval Actions",x+18,y+120,200,28,h1Fmt_.Get(),brush_.text.Get());
        AddButton(L"approval_request",L"Request Latest Suggestion",x+240,y+132,220,38,false);
        AddButton(L"approval_approve",L"Approve First Pending",x+474,y+132,210,38,true);

        Rounded(x,y+214,contentW,326,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Approval Ledger",x+18,y+226,240,30,h1Fmt_.Get(),brush_.text.Get());

        float yy=y+266;
        if(approvals_.empty()) {
            Rounded(x+22,yy,contentW-44,52,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"No approval requests yet.",x+36,yy+6,contentW-72,40,bodyFmt_.Get(),brush_.muted.Get());
        }

        for(size_t i=0;i<approvals_.size() && i<4;i++) {
            const auto& a=approvals_[i];
            Rounded(x+22,yy,contentW-44,58,brush_.sidebar.Get(),brush_.border.Get(),8);
            std::wstring state=a.status==sentinel::operations::ApprovalStatus::Pending?L"PENDING":
                a.status==sentinel::operations::ApprovalStatus::Approved?L"APPROVED":L"REJECTED";
            ID2D1Brush* sb=a.status==sentinel::operations::ApprovalStatus::Pending?brush_.yellow.Get():
                a.status==sentinel::operations::ApprovalStatus::Approved?brush_.green.Get():brush_.red.Get();
            TextLine(state,x+34,yy+6,90,20,tinyFmt_.Get(),sb);
            TextLine(Widen(a.action),x+136,yy+4,contentW-172,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"SHA-256 "+Widen(a.actionHash),x+136,yy+30,contentW-172,20,tinyFmt_.Get(),brush_.muted.Get());
            yy+=68;
        }
    }

    void DrawAgency(float w,float h) {
        PageTitle(L"Agency Server",L"Encrypted synchronization configuration and offline work queue");
        const float x=kSidebar+28.0f;
        const float y=kHeader+104.0f;
        const float contentW=w-x-28.0f;
        const float gap=14.0f;
        const float leftW=(contentW-gap)*0.58f;
        const float rightW=contentW-gap-leftW;
        const float rx=x+leftW+gap;

        Rounded(x,y,leftW,278,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Agency Connection",x+18,y+12,leftW-36,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Server endpoint",x+20,y+62,116,30,tinyFmt_.Get(),brush_.muted.Get());

        TextLine(L"Agency ID",x+20,y+108,116,30,tinyFmt_.Get(),brush_.muted.Get());

        AddButton(L"agency_toggle",agencyConfig_.enabled?L"Disable Sync":L"Enable Sync",x+20,y+162,150,38,true);
        StatusDot(x+194,y+181,4,agencyConfig_.enabled?brush_.green.Get():brush_.yellow.Get());
        TextLine(agencyConfig_.enabled?L"Configuration enabled":L"Offline / local-only",
            x+206,y+163,leftW-226,36,smallFmt_.Get(),agencyConfig_.enabled?brush_.green.Get():brush_.muted.Get());

        Text(L"Transport remains inactive until an agency endpoint and authentication contract are actually implemented.",
            x+20,y+218,leftW-40,42,tinyFmt_.Get(),brush_.muted.Get());

        Rounded(rx,y,rightW,278,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Sync Queue",rx+18,y+12,rightW-36,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Pending work items",rx+20,y+64,132,24,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(std::to_wstring(agencyQueue_.PendingCount()),rx+158,y+56,90,38,bigFmt_.Get(),brush_.cyan.Get());

        AddButton(L"agency_enqueue",L"Queue Audit Snapshot",rx+20,y+116,190,38,false);

        Rounded(rx+20,y+172,rightW-40,72,brush_.sidebar.Get(),brush_.border.Get(),8);
        TextLine(L"Offline-first",rx+34,y+182,rightW-68,22,bodyFmt_.Get(),brush_.green.Get());
        Text(L"Case and evidence access stays available even when no agency server is configured.",
            rx+34,y+207,rightW-68,30,tinyFmt_.Get(),brush_.muted.Get());

        Rounded(x,y+294,leftW,148,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Operating Jurisdiction",x+18,y+306,leftW-36,30,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"State",x+20,y+348,100,28,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"jurisdiction_apply",L"Apply Rules Profile",x+20,y+390,156,34,true);
        TextLine(jurisdictionStatus_,x+188,y+386,leftW-208,42,tinyFmt_.Get(),brush_.cyan.Get());

        Rounded(rx,y+294,rightW,148,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Rules Enforcement",rx+18,y+306,rightW-36,30,h1Fmt_.Get(),brush_.text.Get());
        Text(L"State rules sit above every provider adapter. Unreviewed, missing, or expired profiles force human review even if a channel supports automation.",
            rx+20,y+348,rightW-40,72,tinyFmt_.Get(),brush_.muted.Get());

        Rounded(x,y+456,contentW,174,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Planned Server Responsibilities",x+18,y+468,320,30,h1Fmt_.Get(),brush_.text.Get());

        const wchar_t* items[]={
            L"Encrypted case and evidence synchronization",
            L"Central policy and model-profile distribution",
            L"Workstation registration and role administration",
            L"Multi-investigator coordination and redundant backup"
        };
        float iy=y+516;
        for(auto* item:items) {
            StatusDot(x+28,iy+10,3,brush_.cyan.Get());
            TextLine(item,x+42,iy,contentW-64,22,smallFmt_.Get(),brush_.text.Get());
            iy+=38;
        }
    }

    void CheckForUpdates() {
        try {
            sentinel::update::UpdateService service;
            const std::string url="https://raw.githubusercontent.com/afterburn25/Sentinel/main/release/update-manifest.json";
            auto info=service.Check(url,"1.0.7");
            if(info.newer) {
                updateStatus_=L"Update available: "+Widen(info.version);
                statusText_=L"Sentinel update available";
            } else {
                updateStatus_=L"Current version 1.0.7 is up to date";
                statusText_=L"No Sentinel update available";
            }
        } catch(const std::exception& e) {
            updateStatus_=L"Update check failed: "+Widen(e.what());
            statusText_=L"Update check failed";
        }
    }

    void RunAiDiagnostics() {
        std::wstringstream report;
        report << L"Configured endpoint: " << Widen(simSettings_.endpoint) << L"\n";
        report << L"Configured model: " << Widen(simSettings_.model) << L"\n";
        report << L"Active adapter: " << Widen(model_?model_->Name():"None") << L"\n";

        const auto aiDir=ExeDir()/L"ai";
        const std::array<std::filesystem::path,3> runtimeCandidates{
            aiDir/L"runtime"/L"cuda"/L"llama-server.exe",
            aiDir/L"runtime"/L"cpu"/L"llama-server.exe",
            aiDir/L"runtime"/L"llama-server.exe"
        };
        bool runtimeFound=false;
        for(const auto& p:runtimeCandidates) {
            if(std::filesystem::exists(p)) { runtimeFound=true; break; }
        }
        report << L"Bundled llama-server: " << (runtimeFound?L"FOUND":L"MISSING") << L"\n";

        const auto modelDir=aiDir/L"models";
        bool modelFileFound=false;
        if(std::filesystem::exists(modelDir)) {
            for(const auto& entry:std::filesystem::directory_iterator(modelDir)) {
                if(entry.is_regular_file() && entry.path().extension()==L".gguf") {
                    modelFileFound=true;
                    break;
                }
            }
        }
        report << L"Local GGUF: " << (modelFileFound?L"FOUND":L"MISSING") << L"\n";

        try {
            auto models=sentinel::simulation::DiscoverOpenAICompatibleModels(simSettings_.endpoint);
            report << L"/v1/models: OK (" << models.size() << L" model";
            if(models.size()!=1) report << L"s";
            report << L")\n";
            if(!models.empty()) report << L"First discovered model: " << Widen(models.front()) << L"\n";

            try {
                auto probe=sentinel::simulation::CreateOpenAICompatibleModel(
                    simSettings_.endpoint,simSettings_.model,{},simSettings_.temperature,64);
                sentinel::simulation::ModelContext ctx;
                ctx.scenario="Sentinel AI diagnostic";
                ctx.personaSummary="Synthetic diagnostic persona.";
                auto response=probe->GenerateInvestigatorSuggestion(ctx);
                report << L"Completion probe: OK";
                if(!response.empty()) report << L" (" << std::min<size_t>(response.size(),120) << L" chars)";
                report << L"\n";
            } catch(const std::exception& e) {
                report << L"Completion probe: FAILED - " << Widen(e.what()) << L"\n";
            }
        } catch(const std::exception& first) {
            report << L"/v1/models: FAILED - " << Widen(first.what()) << L"\n";
            std::wstring startFailure;
            if(IsLocalModelEndpoint(simSettings_.endpoint)) {
                if(StartBundledAiService(&startFailure)) {
                    try {
                        auto models=sentinel::simulation::DiscoverOpenAICompatibleModels(simSettings_.endpoint);
                        report << L"Auto-start retry: OK (" << models.size() << L" model(s))\n";
                    } catch(const std::exception& second) {
                        report << L"Auto-start retry: FAILED - " << Widen(second.what()) << L"\n";
                    }
                } else {
                    report << L"Bundled service start: FAILED - " << startFailure << L"\n";
                }
            }
        }

        aiDiagnostics_=report.str();
        statusText_=L"AI diagnostics complete";
        MessageBoxW(hwnd_,aiDiagnostics_.c_str(),L"SARA AI Diagnostics",MB_OK|MB_ICONINFORMATION);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void DrawSettings(float w,float h) {
        PageTitle(L"Settings",L"Secure storage, application information, and update status");
        const float x=kSidebar+28.0f;
        const float y=kHeader+104.0f;
        const float contentW=w-x-28.0f;
        const float gap=14.0f;
        const float leftW=(contentW-gap)*0.60f;
        const float rightW=contentW-gap-leftW;
        const float rx=x+leftW+gap;

        Rounded(x,y,leftW,230,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Secure Local Store",x+18,y+12,leftW-36,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Location",x+20,y+62,90,26,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(runtime_->root.wstring(),x+118,y+60,leftW-138,30,smallFmt_.Get(),brush_.text.Get());

        TextLine(L"Encryption",x+20,y+102,90,26,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"AES-256-GCM",x+118,y+100,leftW-138,30,smallFmt_.Get(),brush_.green.Get());

        TextLine(L"Key protection",x+20,y+142,90,26,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Windows DPAPI workstation master key",x+118,y+140,leftW-138,30,smallFmt_.Get(),brush_.text.Get());

        TextLine(L"Mode",x+20,y+182,90,26,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(agencyConfig_.enabled?L"Offline-first + agency sync configuration":L"Offline-first / local-only",
            x+118,y+180,leftW-138,30,smallFmt_.Get(),brush_.cyan.Get());

        Rounded(rx,y,rightW,230,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Application",rx+18,y+12,rightW-36,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Version",rx+20,y+62,78,26,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Sentinel 1.0.7",rx+104,y+60,rightW-124,30,bodyFmt_.Get(),brush_.text.Get());

        TextLine(L"Build",rx+20,y+102,78,26,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Development Release",rx+104,y+100,rightW-124,30,smallFmt_.Get(),brush_.muted.Get());

        AddButton(L"check_updates",L"Check for Updates",rx+20,y+146,150,38,false);
        AddButton(L"ai_diagnostics",L"AI Diagnostics",rx+180,y+146,140,38,true);
        TextLine(updateStatus_,rx+20,y+190,rightW-40,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(modelStatus_,rx+20,y+208,rightW-40,18,tinyFmt_.Get(),
            modelStatus_.find(L"Connected")!=std::wstring::npos?brush_.green.Get():brush_.yellow.Get());

        Rounded(x,y+246,contentW,294,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Release Security",x+18,y+258,260,30,h1Fmt_.Get(),brush_.text.Get());

        const wchar_t* rows[][2]={
            {L"Evidence integrity",L"SHA-256 + AES-GCM authentication"},
            {L"Audit integrity",L"Hash-linked audit ledger"},
            {L"Update transport",L"HTTPS-only manifest checking"},
            {L"Outbound messaging",L"Human approval required"},
            {L"Code signing",L"Pipeline ready; trusted signing identity not configured"}
        };
        float yy=y+304;
        for(auto& row:rows) {
            TextLine(row[0],x+22,yy,160,26,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(row[1],x+194,yy,contentW-216,26,smallFmt_.Get(),brush_.text.Get());
            yy+=42;
        }
    }

    void ShowCaseEditors(bool show) {
        ShowWindow(caseNumberEdit_,show?SW_SHOW:SW_HIDE);
        ShowWindow(caseTitleEdit_,show?SW_SHOW:SW_HIDE);
    }

    void SelectCase(size_t i) {
        if (i>=cases_.size()) return;
        selectedCase_=i; selectedEvidence_=0;
        try { evidence_=runtime_->Evidence(cases_[i].id); } catch (...) { evidence_.clear(); }
    }

    void SelectEvidence(size_t i) {
        if (i<evidence_.size()) selectedEvidence_=i;
    }

    void CreateCase() {
        wchar_t nbuf[256]{},tbuf[512]{};
        GetWindowTextW(caseNumberEdit_,nbuf,256);
        GetWindowTextW(caseTitleEdit_,tbuf,512);
        std::wstring wn=nbuf,wt=tbuf;
        if (wn.empty()||wt.empty()) {
            MessageBoxW(hwnd_,L"Enter both a case number and title.",L"SARA",MB_OK|MB_ICONINFORMATION);
            page_=Page::Cases; ShowCaseEditors(true); return;
        }
        try {
            sentinel::SqliteTransaction tx(runtime_->db);
            auto actor=sentinel::UserId::Random();
            auto rec=runtime_->cases.CreateCase({Narrow(wn),Narrow(wt),"",actor});
            runtime_->keys.CreateCaseKey(rec.id);
            runtime_->caseRepo.Update(rec);
            runtime_->audit.Append({actor,sentinel::AuditAction::CaseCreated,"case",rec.id.ToString(),{}});
            tx.Commit();
            SetWindowTextW(caseNumberEdit_,L""); SetWindowTextW(caseTitleEdit_,L"");
            LoadData(); page_=Page::Cases; ShowCaseEditors(true);
            statusText_=L"Case created securely";
        } catch (const std::exception& e) {
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Create Case Failed",MB_OK|MB_ICONERROR);
        }
    }

    std::optional<std::filesystem::path> PickFile() {
        wchar_t file[32768]{};
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.hwndOwner=hwnd_;
        ofn.lpstrFile=file;
        ofn.nMaxFile=32768;
        ofn.lpstrFilter=L"All Files\0*.*\0\0";
        ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) return std::filesystem::path(file);
        return std::nullopt;
    }

    void ImportEvidence() {
        if (cases_.empty()) {
            MessageBoxW(hwnd_,L"Create a case first.",L"SARA",MB_OK|MB_ICONINFORMATION);
            return;
        }
        auto file=PickFile(); if (!file) return;
        try {
            auto key=runtime_->keys.GetCaseKey(cases_[selectedCase_].id);
            sentinel::EvidenceService svc(runtime_->root/"evidence",runtime_->db,runtime_->random,runtime_->hash,runtime_->cipher,runtime_->audit);
            svc.Import({cases_[selectedCase_].id,*file,0,sentinel::UserId::Random()},key.Span());
            evidence_=runtime_->Evidence(cases_[selectedCase_].id);
            selectedEvidence_=0; page_=Page::Evidence; ShowCaseEditors(false);
            statusText_=L"Evidence imported and encrypted";
        } catch (const std::exception& e) {
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Evidence Import Failed",MB_OK|MB_ICONERROR);
        }
    }

    void VerifySelected() {
        if (cases_.empty()||evidence_.empty()) {
            MessageBoxW(hwnd_,L"Select or import evidence first.",L"SARA",MB_OK|MB_ICONINFORMATION);
            return;
        }
        try {
            auto key=runtime_->keys.GetCaseKey(cases_[selectedCase_].id);
            sentinel::Hash256 hash{};
            auto plain=sentinel::SevContainer::DecryptFile(evidence_[selectedEvidence_].storedPath,key.Span(),runtime_->cipher,runtime_->hash,&hash);
            if (hash!=evidence_[selectedEvidence_].originalHash) throw std::runtime_error("plaintext hash mismatch");
            runtime_->audit.Append({sentinel::UserId::Random(),sentinel::AuditAction::EvidenceVerified,"evidence",evidence_[selectedEvidence_].id.ToString(),{}});
            lastVerify_=L"VALID | AUTHENTICATED | SHA-256 "+Widen(hash.ToHex()).substr(0,20)+L"...";
            page_=Page::Verification; ShowCaseEditors(false); statusText_=L"Evidence verification passed";
        } catch (const std::exception& e) {
            lastVerify_=L"INVALID | "+Widen(e.what());
            page_=Page::Verification; ShowCaseEditors(false); statusText_=L"Verification failed";
        }
    }

    void VerifyAudit() {
        bool ok=runtime_->audit.VerifyChain();
        statusText_=ok?L"Audit chain verified":L"Audit integrity failure";
        MessageBoxW(hwnd_,ok?L"Audit chain is VALID. No tampering detected.":L"Audit chain verification FAILED.",
            L"Sentinel Audit Verification",MB_OK|(ok?MB_ICONINFORMATION:MB_ICONERROR));
    }
};

App* g_app{};

LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
        case WM_CREATE:
            try { g_app=new App(); g_app->Init(hwnd); }
            catch (const std::exception& e) {
                MessageBoxW(hwnd,Widen(e.what()).c_str(),L"Sentinel Startup Failed",MB_OK|MB_ICONERROR);
                return -1;
            }
            return 0;
        case WM_SIZE: if(g_app) g_app->Resize(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; BeginPaint(hwnd,&ps); if(g_app) g_app->Paint(); EndPaint(hwnd,&ps); return 0;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc=(HDC)wp;
            SetTextColor(dc,RGB(238,246,255));
            SetBkColor(dc,RGB(15,32,48));
            return g_app ? (LRESULT)g_app->EditBrush() : (LRESULT)GetStockObject(BLACK_BRUSH);
        }
        case WM_VSCROLL: if(g_app) g_app->HandleSimScroll(wp); return 0;
        case WM_MOUSEWHEEL: if(g_app) g_app->HandleSimWheel(GET_WHEEL_DELTA_WPARAM(wp)); return 0;
        case WM_TIMER: if(g_app) g_app->HandleTimer((UINT_PTR)wp); return 0;
        case WM_LBUTTONDOWN:
            if(g_app) g_app->BeginTextSelection((float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp));
            return 0;
        case WM_MOUSEMOVE:
            if(g_app && (wp&MK_LBUTTON))
                g_app->UpdateTextSelection((float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp));
            return 0;
        case WM_RBUTTONUP:
            if(g_app && g_app->CopySelectedTextAt((float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp))) return 0;
            if(g_app) g_app->RightClick((float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp));
            return 0;
        case WM_LBUTTONUP:
            if(g_app) {
                const float mx=(float)GET_X_LPARAM(lp), my=(float)GET_Y_LPARAM(lp);
                if(!g_app->EndTextSelection(mx,my)) g_app->Click(mx,my);
            }
            return 0;
        case WM_DESTROY: delete g_app; g_app=nullptr; PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);

    SplashThreadContext splashCtx{};
    splashCtx.instance=instance;
    splashCtx.readyEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    splashCtx.createdEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE splashThread=CreateThread(nullptr,0,SaraSplashThreadProc,&splashCtx,0,nullptr);
    if(splashThread && splashCtx.createdEvent)
        WaitForSingleObject(splashCtx.createdEvent,5000);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style=CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc=WndProc;
    wc.hInstance=instance;
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName=kClassName;
    wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(kSaraIconResource));
    if(!wc.hIcon) wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
    wc.hIconSm=wc.hIcon;
    RegisterClassExW(&wc);

    HWND hwnd=CreateWindowExW(
        0,kClassName,L"SARA - Synthetic Adaptive Response Agent",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,1500,900,
        nullptr,nullptr,instance,nullptr);
    if (!hwnd) {
        if(splashCtx.readyEvent) SetEvent(splashCtx.readyEvent);
        if(splashThread) WaitForSingleObject(splashThread,INFINITE);
        if(splashThread) CloseHandle(splashThread);
        if(splashCtx.createdEvent) CloseHandle(splashCtx.createdEvent);
        if(splashCtx.readyEvent) CloseHandle(splashCtx.readyEvent);
        CoUninitialize();
        return 1;
    }

    // The main window has completed WM_CREATE/App::Init and is ready to show.
    // Keep the splash visible until this point (and for the requested minimum
    // seven seconds), then reveal the already-initialized app immediately.
    if(splashCtx.readyEvent) SetEvent(splashCtx.readyEvent);
    if(splashThread) WaitForSingleObject(splashThread,INFINITE);
    if(splashThread) CloseHandle(splashThread);
    if(splashCtx.createdEvent) CloseHandle(splashCtx.createdEvent);
    if(splashCtx.readyEvent) CloseHandle(splashCtx.readyEvent);

    ShowWindow(hwnd,show);
    UpdateWindow(hwnd);

    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}
