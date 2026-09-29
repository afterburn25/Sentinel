#include "Sentinel/Audit/AuditService.hpp"
#include "Sentinel/Core/CaseRepository.hpp"
#include "Sentinel/Core/CaseService.hpp"
#include "Sentinel/Evidence/EvidenceService.hpp"
#include "Sentinel/Evidence/SevContainer.hpp"
#include "Sentinel/Identity/SubjectIdentityStore.hpp"
#include "Sentinel/Security/Crypto.hpp"
#include "Sentinel/Security/KeyManager.hpp"
#include "Sentinel/Security/SecretProtector.hpp"
#include "Sentinel/Storage/MigrationService.hpp"
#include "Sentinel/Simulation/IModelAdapter.hpp"
#include "Sentinel/Simulation/PersonaPolicy.hpp"
#include "Sentinel/Simulation/SettingsStore.hpp"
#include "Sentinel/Simulation/ResponseEvaluator.hpp"
#include "Sentinel/Simulation/ResponseRuleMatcher.hpp"
#include "Sentinel/Simulation/ResponseRuleMatchLog.hpp"
#include "Sentinel/Simulation/EvaluationSuite.hpp"
#include "Sentinel/Simulation/DeploymentRegistry.hpp"
#include "Sentinel/Simulation/SessionStore.hpp"
#include "Sentinel/Simulation/ConversationMemory.hpp"
#include "Sentinel/Simulation/ModelRegistry.hpp"
#include "Sentinel/Simulation/PersonaProfileStore.hpp"
#include "Sentinel/Simulation/TrainingReviewStore.hpp"
#include "Sentinel/Simulation/TrainingData.hpp"
#include "Sentinel/Simulation/TrainerStore.hpp"
#include "Sentinel/Operations/Messaging.hpp"
#include "Sentinel/Operations/Supervisor.hpp"
#include "Sentinel/Operations/SupervisorStateStore.hpp"
#include "Sentinel/Channels/ChannelCore.hpp"
#include "Sentinel/Channels/AutomationEngine.hpp"
#include "Sentinel/Channels/ChannelAdapterRegistry.hpp"
#include "Sentinel/Channels/LocalSimulationChannelAdapter.hpp"
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

enum class Page { Dashboard, Cases, Evidence, Audit, Verification, Simulation, Persona, ModelLab, Trainer, Messaging, Supervisor, Agency, Settings, ModelLabDatasets, ModelLabPersonas, ModelLabFoundations, ModelLabJobs, ModelLabEvaluation, ModelLabDeployment, Subjects, IdentityResearch };
enum class PersonaTab { Profile, Bio, Behavior, Scenario, Gallery, Rules };
enum class IconKind { Shield, Home, Folder, Database, Document, Check, Gear, Search, Plus, Chain, Lock, Chat, Smile, Paperclip };

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

std::vector<std::byte> AuditMetadata(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for(unsigned char ch:text) out.push_back(static_cast<std::byte>(ch));
    return out;
}

std::filesystem::path AppDataRoot() {
    wchar_t overrideRoot[4096]{};
    const DWORD overrideLength=GetEnvironmentVariableW(
        L"SARA_DATA_ROOT",overrideRoot,(DWORD)std::size(overrideRoot));
    if(overrideLength>0 && overrideLength<std::size(overrideRoot))
        return std::filesystem::path(overrideRoot);

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

bool StartBundledAiService(std::wstring* failure = nullptr,bool forceRestart=false) {
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
    if(forceRestart) command+=L" -ForceRestart";

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

enum class LocalModelMarkerState {
    ModelMissing,
    MarkerMissing,
    MarkerMismatch,
    MarkerCurrent
};

LocalModelMarkerState LocalModelVerificationMarkerState() {
    static constexpr const char* kExpectedModelSha256=
        "03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8";

    const auto model=
        ExeDir()/L"ai"/L"models"/L"Qwen3.5-9B-Q4_K_M.gguf";
    const auto marker=
        ExeDir()/L"ai"/L"models"/L"Qwen3.5-9B-Q4_K_M.gguf.sha256";

    if(!std::filesystem::exists(model))
        return LocalModelMarkerState::ModelMissing;
    if(!std::filesystem::exists(marker))
        return LocalModelMarkerState::MarkerMissing;

    std::ifstream in(marker,std::ios::binary);
    if(!in) return LocalModelMarkerState::MarkerMissing;

    std::string value;
    std::getline(in,value);
    while(!value.empty() &&
          (value.back()=='\r' || value.back()=='\n' ||
           value.back()==' ' || value.back()=='\t'))
        value.pop_back();
    std::transform(value.begin(),value.end(),value.begin(),
        [](unsigned char ch){ return (char)std::tolower(ch); });

    return value==kExpectedModelSha256
        ? LocalModelMarkerState::MarkerCurrent
        : LocalModelMarkerState::MarkerMismatch;
}

std::wstring LocalModelVerificationMarkerLabel(LocalModelMarkerState state) {
    switch(state) {
        case LocalModelMarkerState::ModelMissing:
            return L"Model not installed";
        case LocalModelMarkerState::MarkerMissing:
            return L"Model present; verification marker missing";
        case LocalModelMarkerState::MarkerMismatch:
            return L"Verification marker does not match protected model hash";
        case LocalModelMarkerState::MarkerCurrent:
            return L"Installer SHA-256 verification marker is current";
    }
    return L"Unknown";
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
        case 600: return L"Deployment prepared";
        case 601: return L"Deployment activated";
        case 602: return L"Deployment rolled back";
        case 603: return L"Deployment lock changed";
        case 604: return L"Deployment manifest exported";
        case 700: return L"Subject created";
        case 701: return L"Subject updated";
        case 702: return L"Subject deleted";
        case 710: return L"Identity lead added";
        case 711: return L"Identity lead verified";
        case 712: return L"Identity lead rejected";
        case 713: return L"Subject identity confirmed";
        case 714: return L"Identity research queued";
        case 715: return L"Identity research completed";
        case 716: return L"Identity research promoted";
        case 717: return L"Identity research rejected";
        case 720: return L"Investigator takeover activated";
        case 721: return L"Investigator takeover released";
        case 722: return L"Supervisor approval requested";
        case 723: return L"Supervisor approval approved";
        case 724: return L"Supervisor approval rejected";
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
    sentinel::operations::SupervisorStateStore supervisorState;
    sentinel::agency::AgencyServerStore agencyStore;
    sentinel::identity::SubjectIdentityStore subjectIdentity;
    sentinel::simulation::ConversationMemoryStore conversationMemory;
    sentinel::simulation::PersonaProfileStore personaProfiles;
    sentinel::simulation::TrainingReviewStore trainingReviews;
    sentinel::simulation::TrainerStore trainer;
    sentinel::simulation::ResponseRuleMatchLog responseRuleMatches;
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
          supervisorState(db),
          agencyStore(db),
          subjectIdentity(db),
          conversationMemory(db),
          personaProfiles(db),
          trainingReviews(db),
          trainer(db),
          responseRuleMatches(db),
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
            "Qwen/Qwen3.5-9B",
            (ExeDir()/L"ai"/L"models"/L"Qwen3.5-9B-Q4_K_M.gguf").string());
        keys.Initialize();

        wchar_t fixture[128]{};
        const DWORD fixtureLength=GetEnvironmentVariableW(
            L"SARA_CAPTURE_FIXTURE",fixture,(DWORD)std::size(fixture));
        if(fixtureLength>0 && fixtureLength<std::size(fixture) &&
           std::wstring(fixture)==L"identity-research-v1")
        {
            sqlite3_stmt* countStmt{};
            long long caseCount=0;
            if(sqlite3_prepare_v2(db.Handle(),"SELECT COUNT(*) FROM cases",-1,&countStmt,nullptr)==SQLITE_OK &&
               sqlite3_step(countStmt)==SQLITE_ROW)
                caseCount=sqlite3_column_int64(countStmt,0);
            sqlite3_finalize(countStmt);

            if(caseCount==0) {
                sentinel::SqliteTransaction tx(db);
                const auto actor=sentinel::UserId::Random();
                auto rec=cases.CreateCase({
                    "CI-RESEARCH-001",
                    "Identity Research UI Capture",
                    "Disposable packaged-UI fixture",
                    actor
                });
                keys.CreateCaseKey(rec.id);
                caseRepo.Update(rec);
                audit.Append({actor,sentinel::AuditAction::CaseCreated,"case",rec.id.ToString(),
                    AuditMetadata("capture_fixture=identity-research-v1")});
                auto subject=subjectIdentity.CreateSubject(rec.id,"Research Capture Subject");
                audit.Append({actor,sentinel::AuditAction::SubjectCreated,"subject",subject.id.ToString(),
                    AuditMetadata("capture_fixture=identity-research-v1")});
                tx.Commit();
            }
        }
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
        int x=4;
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

        SetCaretPos(std::max(4,x),std::max(7,y));
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

        if(msg==WM_CHAR && (wp==L'\r' || wp==L'\n')) {
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
        if(uiFont_) DeleteObject(uiFont_);
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
        personaBackgroundEdit_=CreateWindowExW(
            WS_EX_CLIENTEDGE,L"EDIT",L"",
            WS_CHILD|WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN,
            0,0,0,0,hwnd_,(HMENU)1032,GetModuleHandleW(nullptr),nullptr);
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

        subjectDisplayEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1060,GetModuleHandleW(nullptr),nullptr);
        subjectLegalEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1061,GetModuleHandleW(nullptr),nullptr);
        subjectAliasesEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1062,GetModuleHandleW(nullptr),nullptr);
        subjectUsernamesEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1063,GetModuleHandleW(nullptr),nullptr);
        subjectContactsEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1064,GetModuleHandleW(nullptr),nullptr);
        subjectNotesEdit_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL,0,0,0,0,hwnd_,(HMENU)1065,GetModuleHandleW(nullptr),nullptr);

        identitySourceTypeEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1066,GetModuleHandleW(nullptr),nullptr);
        identitySourceRefEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1067,GetModuleHandleW(nullptr),nullptr);
        identityLeadValueEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1068,GetModuleHandleW(nullptr),nullptr);
        identityConfidenceEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1069,GetModuleHandleW(nullptr),nullptr);
        identityProvenanceEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1070,GetModuleHandleW(nullptr),nullptr);

        researchTypeCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1071,GetModuleHandleW(nullptr),nullptr);
        researchProviderCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWN|CBS_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1072,GetModuleHandleW(nullptr),nullptr);
        researchQueryEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1073,GetModuleHandleW(nullptr),nullptr);
        researchPurposeEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1074,GetModuleHandleW(nullptr),nullptr);
        researchResultEdit_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,0,0,0,0,hwnd_,(HMENU)1075,GetModuleHandleW(nullptr),nullptr);
        researchReferenceEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1076,GetModuleHandleW(nullptr),nullptr);
        researchProvenanceEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1077,GetModuleHandleW(nullptr),nullptr);
        researchConfidenceEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1078,GetModuleHandleW(nullptr),nullptr);

        researchProviderIdEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1079,GetModuleHandleW(nullptr),nullptr);
        researchProviderNameEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1080,GetModuleHandleW(nullptr),nullptr);
        researchProviderModeCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1081,GetModuleHandleW(nullptr),nullptr);
        researchProviderEndpointEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1082,GetModuleHandleW(nullptr),nullptr);
        researchProviderCredentialEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1083,GetModuleHandleW(nullptr),nullptr);
        researchProviderNotesEdit_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,0,0,0,0,hwnd_,(HMENU)1084,GetModuleHandleW(nullptr),nullptr);

        personaCommunicationCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1034,GetModuleHandleW(nullptr),nullptr);
        personaCognitiveCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1041,GetModuleHandleW(nullptr),nullptr);
        personaSlangCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1035,GetModuleHandleW(nullptr),nullptr);
        personaGrammarCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1036,GetModuleHandleW(nullptr),nullptr);
        personaTypoCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1037,GetModuleHandleW(nullptr),nullptr);
        personaEmojiCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1038,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(caseNumberEdit_,WM_SETFONT,(WPARAM)(uiFont_?uiFont_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(caseTitleEdit_,WM_SETFONT,(WPARAM)(uiFont_?uiFont_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        uiFont_=CreateFontW(
            -18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        chatFont_=CreateFontW(
            -21,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        SendMessageW(chatEdit_,WM_SETFONT,(WPARAM)(chatFont_?chatFont_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(chatEdit_,EM_SETLIMITTEXT,4000,0);
        SendMessageW(modelEndpointEdit_,WM_SETFONT,(WPARAM)(uiFont_?uiFont_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(modelNameEdit_,WM_SETFONT,(WPARAM)(uiFont_?uiFont_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(modelCombo_,WM_SETFONT,(WPARAM)(uiFont_?uiFont_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        HWND advancedEdits[]={personaNameEdit_,personaLocationEdit_,personaInterestsEdit_,
            personaOccupationEdit_,personaEducationEdit_,personaFamilyEdit_,personaBackgroundEdit_,
            responseRuleTriggerEdit_,responseRuleResponseEdit_,
            trainerForkNameEdit_,trainerBasePathEdit_,trainerLoraNameEdit_,trainerLoraPathEdit_,trainerDatasetEdit_,trainerOutputEdit_,trainerInstructionEdit_,
            scenarioNameEdit_,scenarioObjectiveEdit_,scenarioSeedEdit_,minDelayEdit_,maxDelayEdit_,agencyEndpointEdit_,agencyIdEdit_,
            subjectDisplayEdit_,subjectLegalEdit_,subjectAliasesEdit_,subjectUsernamesEdit_,subjectContactsEdit_,subjectNotesEdit_,
            identitySourceTypeEdit_,identitySourceRefEdit_,identityLeadValueEdit_,identityConfidenceEdit_,identityProvenanceEdit_,
            researchQueryEdit_,researchPurposeEdit_,researchResultEdit_,researchReferenceEdit_,researchProvenanceEdit_,researchConfidenceEdit_,
            researchProviderIdEdit_,researchProviderNameEdit_,researchProviderEndpointEdit_,researchProviderCredentialEdit_,researchProviderNotesEdit_};
        for(HWND e:advancedEdits) {
            SendMessageW(e,WM_SETFONT,(WPARAM)(uiFont_?uiFont_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
            SetWindowTheme(e,L"DarkMode_Explorer",nullptr);
            SendMessageW(e,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
        }
        HWND centeredEdits[]={
            caseNumberEdit_,caseTitleEdit_,modelEndpointEdit_,modelNameEdit_,
            personaNameEdit_,personaLocationEdit_,personaInterestsEdit_,
            personaOccupationEdit_,personaEducationEdit_,personaFamilyEdit_,
            responseRuleTriggerEdit_,responseRuleResponseEdit_,
            trainerForkNameEdit_,trainerBasePathEdit_,trainerLoraNameEdit_,trainerLoraPathEdit_,
            trainerDatasetEdit_,trainerOutputEdit_,
            scenarioNameEdit_,scenarioObjectiveEdit_,scenarioSeedEdit_,minDelayEdit_,maxDelayEdit_,
            agencyEndpointEdit_,agencyIdEdit_,
            subjectDisplayEdit_,subjectLegalEdit_,subjectAliasesEdit_,subjectUsernamesEdit_,subjectContactsEdit_,
            identitySourceTypeEdit_,identitySourceRefEdit_,identityLeadValueEdit_,identityConfidenceEdit_,identityProvenanceEdit_,
            researchQueryEdit_,researchPurposeEdit_,researchReferenceEdit_,researchProvenanceEdit_,researchConfidenceEdit_,
            researchProviderIdEdit_,researchProviderNameEdit_,researchProviderEndpointEdit_,researchProviderCredentialEdit_
        };
        for(HWND e:centeredEdits) {
            if(!e) continue;
            LONG_PTR style=GetWindowLongPtrW(e,GWL_STYLE);
            SetWindowLongPtrW(e,GWL_STYLE,style|ES_CENTER);
            SetWindowPos(e,nullptr,0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        }

        SendMessageW(personaBackgroundEdit_,EM_SETLIMITTEXT,16000,0);
        SendMessageW(personaBackgroundEdit_,EM_SETCUEBANNER,TRUE,
            (LPARAM)L"Write the persona's background, home life, family dynamics, routines, school life, habits, memories, likes, dislikes, and ordinary history...");
        HWND personaCombos[]={personaAgeCombo_,ageStateCombo_,personaGenderCombo_,personaPronounsCombo_,personaRelationshipCombo_,
            personaPersonalityCombo_,personaSocialCombo_,personaConfidenceCombo_,modelCombo_,operatingStateCombo_,
            personaCommunicationCombo_,personaCognitiveCombo_,personaSlangCombo_,personaGrammarCombo_,personaTypoCombo_,personaEmojiCombo_,
            personaWritingStyleCombo_,personaProfileCombo_,trainerModeCombo_,trainerFoundationCombo_,
            researchTypeCombo_,researchProviderCombo_,researchProviderModeCombo_};
        for(HWND combo:personaCombos) {
            SendMessageW(combo,WM_SETFONT,(WPARAM)(uiFont_?uiFont_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
            SetWindowTheme(combo,L"DarkMode_Explorer",nullptr);
            SendMessageW(combo,CB_SETITEMHEIGHT,0,28);
            SendMessageW(combo,CB_SETITEMHEIGHT,(WPARAM)-1,28);
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
        SendMessageW(chatEdit_,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(4,8));
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
        SendMessageW(trainerInstructionEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Talk to SARA: describe the behavior, correction, or training goal...");
        SendMessageW(subjectDisplayEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Subject / handle");
        SendMessageW(subjectLegalEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Legal name if verified");
        SendMessageW(subjectAliasesEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Aliases, separated by commas");
        SendMessageW(subjectUsernamesEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Usernames / profile handles");
        SendMessageW(subjectContactsEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Phone / email / identifiers where permitted");
        SendMessageW(identitySourceTypeEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Source type");
        SendMessageW(identitySourceRefEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Source reference / URL / record ID");
        SendMessageW(identityLeadValueEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Identity lead / finding");
        SendMessageW(identityConfidenceEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"0-100");
        SendMessageW(identityProvenanceEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"How this lead was obtained / source context");
        SendMessageW(researchProviderCombo_,CB_SETCUEBANNER,0,(LPARAM)L"Authorized provider / portal / manual source");
        SendMessageW(researchQueryEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Search term, username, record key, contact, or image reference");
        SendMessageW(researchPurposeEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Case purpose / legal-basis note for this research task");
        SendMessageW(researchResultEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Investigator-recorded finding; do not enter an identity conclusion unless verified");
        SendMessageW(researchReferenceEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Result URL / record ID / evidence reference");
        SendMessageW(researchProvenanceEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"How, when, and from which authorized source the result was obtained");
        SendMessageW(researchConfidenceEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"0-100");
        SendMessageW(researchProviderIdEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"stable-provider-id");
        SendMessageW(researchProviderNameEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Provider display name");
        SendMessageW(researchProviderEndpointEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Endpoint / portal hint; no credentials");
        SendMessageW(researchProviderCredentialEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Credential alias / vault reference only");
        SendMessageW(researchProviderNotesEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Configuration notes; never store secrets here");
        SendMessageW(researchProviderNotesEdit_,EM_SETLIMITTEXT,4000,0);
        SendMessageW(researchResultEdit_,EM_SETLIMITTEXT,12000,0);
        SendMessageW(researchConfidenceEdit_,WM_SETTEXT,0,(LPARAM)L"50");
        SendMessageW(subjectNotesEdit_,EM_SETLIMITTEXT,12000,0);
        HWND trainerEdits[]={
            trainerForkNameEdit_,trainerBasePathEdit_,trainerLoraNameEdit_,trainerLoraPathEdit_,
            trainerDatasetEdit_,trainerOutputEdit_,trainerInstructionEdit_
        };
        for(HWND edit:trainerEdits) {
            SetWindowTheme(edit,L"DarkMode_Explorer",nullptr);
            SendMessageW(edit,WM_SETFONT,(WPARAM)(uiFont_?uiFont_:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
            SendMessageW(edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(10,10));
        }
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
            L"Behavior Tuning",L"Dataset Training",L"Persona LoRA Training",
            L"Foundation Fork Training",L"Evaluation / Test"
        };
        fillCombo(trainerModeCombo_,trainerModes,std::size(trainerModes));
        SendMessageW(trainerModeCombo_,CB_SETCURSEL,0,0);

        const wchar_t* researchTypes[]={
            L"Public Records",L"Social Profile",L"Username",
            L"Contact Identifier",L"Image Reference"
        };
        fillCombo(researchTypeCombo_,researchTypes,std::size(researchTypes));
        SendMessageW(researchTypeCombo_,CB_SETCURSEL,0,0);
        const wchar_t* researchProviderModes[]={L"Manual",L"Portal",L"API"};
        fillCombo(researchProviderModeCombo_,researchProviderModes,std::size(researchProviderModes));
        SendMessageW(researchProviderModeCombo_,CB_SETCURSEL,0,0);
        RefreshIdentityResearchProviderList();
        RefreshTrainerFoundationList();
        RefreshPersonaProfileList();
        SendMessageW(ageStateCombo_,CB_SETCURSEL,(WPARAM)static_cast<int>(simSettings_.ageState),0);
        LoadProfileEditors();
        LoadPersonaMedia();

        messagingAdapter_=sentinel::operations::CreateInMemoryMessageAdapter();
        runtime_->channelAdapters.Register(
            std::make_unique<sentinel::channels::LocalSimulationChannelAdapter>(*messagingAdapter_));
        agencyConfig_=runtime_->agencyStore.LoadConfig();
        if(agencyConfig_.workstationId.empty()) {
            agencyConfig_.workstationId="local-workstation";
            runtime_->agencyStore.SaveConfig(agencyConfig_);
        }
        SetWindowTextW(agencyEndpointEdit_,Widen(agencyConfig_.endpoint).c_str());
        SetWindowTextW(agencyIdEdit_,Widen(agencyConfig_.agencyId).c_str());
        modelRegistry_.Load(runtime_->root/"model-registry.tsv");
        trainingData_.Load(runtime_->root/"training-data.tsv");
        if(!trainingData_.Snapshots().empty())
            selectedDatasetSnapshot_=(int)trainingData_.Snapshots().size()-1;
        evaluationRuns_.Load(runtime_->root/"evaluation-runs.tsv");
        if(!evaluationRuns_.Runs().empty())
            selectedEvaluationRun_=(int)evaluationRuns_.Runs().size()-1;
        deploymentRegistry_.Load(runtime_->root/"deployment-registry.tsv");
        selectedDeployment_=deploymentRegistry_.ActiveIndex();
        if(selectedDeployment_<0 && !deploymentRegistry_.Packages().empty())
            selectedDeployment_=(int)deploymentRegistry_.Packages().size()-1;

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
            case Page::Subjects: DrawSubjects(w,h); break;
            case Page::IdentityResearch: DrawIdentityResearch(w,h); break;
            case Page::Evidence: DrawEvidence(w,h); break;
            case Page::Audit: DrawAudit(w,h); break;
            case Page::Verification: DrawVerification(w,h); break;
            case Page::Simulation: DrawSimulation(w,h); break;
            case Page::Persona: DrawPersona(w,h); break;
            case Page::ModelLab: DrawModelLab(w,h); break;
            case Page::Trainer: DrawTrainer(w,h); break;
            case Page::ModelLabDatasets: DrawDatasets(w,h); break;
            case Page::ModelLabPersonas: DrawPersonasLoras(w,h); break;
            case Page::ModelLabFoundations: DrawFoundationForks(w,h); break;
            case Page::ModelLabJobs: DrawJobs(w,h); break;
            case Page::ModelLabEvaluation: DrawEvaluation(w,h); break;
            case Page::ModelLabDeployment: DrawDeployment(w,h); break;
            case Page::Messaging: DrawMessaging(w,h); break;
            case Page::Supervisor: DrawSupervisor(w,h); break;
            case Page::Agency: DrawAgency(w,h); break;
            case Page::Settings: DrawSettings(w,h); break;
        }

        HRESULT hr=target_->EndDraw();
        if (hr==D2DERR_RECREATE_TARGET) { brandBitmap_.Reset(); target_.Reset(); brushesReady_=false; }
    }

    void Click(float x,float y) {
        if(x<kSidebar && y>kHeader) {
            constexpr float rowH=44.0f;
            const float startY=(float)kHeader+16.0f;
            const int idx=(int)((y-startY)/rowH);
            if(idx>=0 && idx<12) {
                page_=MainNavPage(idx);
                ApplyPageControls();

                if(page_==Page::Simulation && chatEdit_) {
                    SetFocus(chatEdit_);
                    SendMessageW(chatEdit_,EM_SETSEL,0,0);
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
            else if (b.id==L"verification_center") {
                page_=Page::Verification;
                ApplyPageControls();
                statusText_=L"Evidence verification center";
            }
            else if (b.id==L"dashboard") { page_=Page::Dashboard; ShowCaseEditors(false); ShowChatEditor(false); }
            else if (b.id==L"dashboard_cases") { page_=Page::Cases; ApplyPageControls(); }
            else if (b.id==L"dashboard_subjects") { page_=Page::Subjects; ApplyPageControls(); }
            else if (b.id==L"dashboard_simulation") { page_=Page::Simulation; ApplyPageControls(); }
            else if (b.id==L"dashboard_channels") { page_=Page::Messaging; ApplyPageControls(); }
            else if (b.id==L"dashboard_supervisor") { page_=Page::Supervisor; ApplyPageControls(); }
            else if (b.id==L"dashboard_evidence") { page_=Page::Evidence; ApplyPageControls(); }
            else if (b.id==L"case_subjects") { page_=Page::Subjects; ApplyPageControls(); }
            else if (b.id==L"case_simulation") { page_=Page::Simulation; ApplyPageControls(); }
            else if (b.id==L"case_evidence") { page_=Page::Evidence; ApplyPageControls(); }
            else if (b.id==L"case_audit") { page_=Page::Audit; ApplyPageControls(); }
            else if (b.id==L"subjects_create_case") { page_=Page::Cases; ApplyPageControls(); }
            else if (b.id==L"subject_new") NewSubjectDraft();
            else if (b.id==L"subject_save") SaveSubjectFromEditors();
            else if (b.id==L"subject_delete") DeleteSelectedSubject();
            else if (b.id==L"subject_confirm") ConfirmSelectedSubject();
            else if (b.id.rfind(L"subject_row:",0)==0) SelectSubjectById(Narrow(b.id.substr(12)));
            else if (b.id==L"identity_add_lead") AddIdentityLeadFromEditors();
            else if (b.id.rfind(L"identity_lead:",0)==0) SelectIdentityLeadById(Narrow(b.id.substr(14)));
            else if (b.id==L"identity_verify") ReviewSelectedIdentityLead(true);
            else if (b.id==L"identity_reject") ReviewSelectedIdentityLead(false);
            else if (b.id==L"identity_research_open") OpenIdentityResearch();
            else if (b.id==L"identity_research_back") { page_=Page::Subjects; ApplyPageControls(); statusText_=L"Returned to Subjects & Identity"; }
            else if (b.id==L"research_queue") QueueIdentityResearchFromEditors();
            else if (b.id==L"research_provider_manager") OpenResearchProviderManager();
            else if (b.id==L"research_provider_back") CloseResearchProviderManager();
            else if (b.id==L"research_provider_new") NewResearchProviderConfig();
            else if (b.id==L"research_provider_save") SaveResearchProviderConfig();
            else if (b.id==L"research_provider_toggle") ToggleSelectedResearchProviderConfig();
            else if (b.id.rfind(L"research_provider_row:",0)==0)
                SelectResearchProviderConfig(Narrow(b.id.substr(22)));
            else if (b.id.rfind(L"research_provider_type:",0)==0)
                ToggleResearchProviderType((int)std::stol(b.id.substr(23)));
            else if (b.id.rfind(L"research_task:",0)==0) SelectIdentityResearchTask(Narrow(b.id.substr(14)));
            else if (b.id==L"research_complete") CompleteSelectedIdentityResearch();
            else if (b.id==L"research_promote") PromoteSelectedIdentityResearch();
            else if (b.id==L"research_reject") RejectSelectedIdentityResearch();
            else if (b.id==L"sim_send") SendSimulationMessage();
            else if (b.id==L"sim_emoji") OpenEmojiPicker();
            else if (b.id==L"sim_attach") AttachImageToConversation();
            else if (b.id==L"sim_suggest") GenerateSimulationSuggestion();
            else if (b.id==L"sim_reset" || b.id==L"sim_new_chat") ResetSimulation();
            else if (b.id==L"sim_previous_chat") LoadPreviousConversation();
            else if (b.id==L"sim_model") ConfigureLocalModel();
            else if (b.id==L"sim_browse_models") BrowseModels();
            else if (b.id==L"sim_install_ai") InstallOrRepairLocalAi();
            else if (b.id==L"sim_preserve") PreserveSimulationTranscript();
            else if (b.id==L"sim_export_persona_log") ExportPersonaConversationLog();
            else if (b.id==L"persona_tab_profile") { personaTab_=PersonaTab::Profile; ApplyPageControls(); }
            else if (b.id==L"persona_tab_bio") { personaTab_=PersonaTab::Bio; ApplyPageControls(); }
            else if (b.id==L"persona_tab_behavior") { personaTab_=PersonaTab::Behavior; ApplyPageControls(); }
            else if (b.id==L"persona_tab_scenario") { personaTab_=PersonaTab::Scenario; ApplyPageControls(); }
            else if (b.id==L"persona_tab_gallery") { personaTab_=PersonaTab::Gallery; ApplyPageControls(); }
            else if (b.id==L"persona_tab_rules") { personaTab_=PersonaTab::Rules; ApplyPageControls(); }
            else if (b.id==L"persona_rules_open") { page_=Page::Persona; personaTab_=PersonaTab::Rules; ApplyPageControls(); }
            else if (b.id==L"ml_overview") { page_=Page::ModelLab; ApplyPageControls(); }
            else if (b.id==L"ml_train") { page_=Page::Trainer; ApplyPageControls(); }
            else if (b.id==L"ml_personas") { page_=Page::ModelLabPersonas; ApplyPageControls(); }
            else if (b.id==L"ml_datasets") { page_=Page::ModelLabDatasets; ApplyPageControls(); }
            else if (b.id==L"ml_foundations") { page_=Page::ModelLabFoundations; ApplyPageControls(); }
            else if (b.id==L"ml_jobs") { page_=Page::ModelLabJobs; ApplyPageControls(); }
            else if (b.id==L"ml_evaluation") { page_=Page::ModelLabEvaluation; ApplyPageControls(); }
            else if (b.id==L"ml_deployment") { page_=Page::ModelLabDeployment; ApplyPageControls(); }
            else if (b.id==L"ml_diagnostics_export") ExportModelLabDiagnostics();
            // Deliberately break this dispatch chain into independent groups so MSVC
            // does not treat the full button table as >128 nested else/if blocks.
            if (b.id==L"persona_save") SaveProfileEditors();
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
            else if (b.id.rfind(L"evalrun:",0)==0) selectedEvaluationRun_=(int)std::stol(b.id.substr(8));
            else if (b.id.rfind(L"evalcompare:",0)==0) comparisonEvaluationRun_=(int)std::stol(b.id.substr(12));
            else if (b.id==L"eval_export_run") ExportSelectedEvaluationRun();
            else if (b.id==L"eval_export_compare") ExportEvaluationComparison();
            else if (b.id==L"model_approve") ApproveSelectedRegistryModel();
            else if (b.id==L"model_activate") ActivateSelectedRegistryModel();
            else if (b.id==L"model_rollback") RollbackRegistryModel();
            else if (b.id==L"model_retire") RetireSelectedRegistryModel();
            else if (b.id==L"deployment_prepare") PrepareDeploymentPackage();
            else if (b.id==L"deployment_activate") ActivateSelectedDeploymentPackage();
            else if (b.id==L"deployment_rollback") RollbackDeploymentPackage();
            else if (b.id==L"deployment_lock") ToggleDeploymentLock();
            else if (b.id==L"deployment_export") ExportDeploymentManifest();
            else if (b.id.rfind(L"deployment_select:",0)==0) {
                selectedDeployment_=(int)std::stol(b.id.substr(18));
                statusText_=L"Deployment package selected";
            }
            if (b.id==L"training_stage") StageLatestTrainingExample();
            else if (b.id==L"training_approve") ReviewStagedTrainingExample(true);
            else if (b.id==L"training_reject") ReviewStagedTrainingExample(false);
            else if (b.id==L"training_export") ExportApprovedTrainingDataset();
            else if (b.id==L"dataset_snapshot") CreateReviewedDatasetSnapshot();
            else if (b.id==L"dataset_import_snapshot") ImportDatasetSnapshotFile();
            else if (b.id==L"dataset_export_snapshot") ExportSelectedDatasetSnapshot();
            else if (b.id.rfind(L"dataset_snapshot_row:",0)==0) {
                selectedDatasetSnapshot_=(int)std::stol(b.id.substr(21));
                statusText_=L"Dataset snapshot selected";
            }
            else if (b.id==L"rule_add_smart") AddPersonaResponseRule("smart");
            else if (b.id==L"rule_add_contains") AddPersonaResponseRule("contains");
            else if (b.id==L"rule_add_exact") AddPersonaResponseRule("exact");
            else if (b.id==L"rule_test") TestPersonaResponseRuleMatch();
            else if (b.id==L"rule_clear") ClearPersonaResponseRules();
            else if (b.id==L"rule_wording_toggle") ToggleResponseRuleWordingMode();
            else if (b.id==L"rule_terminal_toggle") ToggleResponseRuleTerminalMode();
            else if (b.id.rfind(L"rule_open:",0)==0)
                LoadPersonaResponseRuleIntoEditors(std::stoll(b.id.substr(10)));
            else if (b.id.rfind(L"rule_toggle:",0)==0)
                TogglePersonaResponseRuleEnabled(std::stoll(b.id.substr(12)));
            else if (b.id.rfind(L"rule_delete:",0)==0)
                DeletePersonaResponseRule(std::stoll(b.id.substr(12)));
            else if (b.id==L"learning_toggle") ToggleLearningMode();
            else if (b.id==L"learning_notes_toggle") personaShowLearnedNotes_=!personaShowLearnedNotes_;
            else if (b.id.rfind(L"learning_note_delete:",0)==0)
                DeletePersonaLearnedNote(std::stoll(b.id.substr(21)));
            else if (b.id==L"trainer_create_fork") CreateFoundationForkFromTrainer();
            else if (b.id==L"trainer_bind_lora") BindCurrentPersonaLoraFromTrainer();
            else if (b.id==L"trainer_queue") QueueTrainerJobFromControls();
            else if (b.id==L"trainer_run_latest") RunLatestQueuedTrainerJob();
            else if (b.id==L"trainer_setup_env") PrepareTrainerEnvironment();
            else if (b.id==L"trainer_apply_instruction") SendTrainerConversationMessage();
            else if (b.id==L"trainer_apply_preview") ApplyLatestTrainerBehaviorPreview();
            else if (b.id==L"trainer_new_session") StartNewTrainerConversation();
            else if (b.id==L"trainer_advanced_toggle") trainerAdvancedOpen_=!trainerAdvancedOpen_;
            if (b.id.rfind(L"dataset_item:",0)==0) {
                selectedTrainingReviewId_=Narrow(b.id.substr(13));
                statusText_=L"Training review item selected";
            }
            else if (b.id.rfind(L"persona_row:",0)==0) {
                selectedModelLabPersonaName_=Narrow(b.id.substr(12));
                selectedModelLabLoraId_=0;
                statusText_=L"Persona selected: "+b.id.substr(12);
            }
            else if (b.id.rfind(L"persona_lora_row:",0)==0) {
                selectedModelLabLoraId_=std::stoll(b.id.substr(17));
                statusText_=L"Persona LoRA version selected";
            }
            else if (b.id==L"persona_lora_activate") ActivateSelectedPersonaLoraVersion();
            else if (b.id==L"persona_lora_rollback") RollbackSelectedPersonaLoraVersion();
            else if (b.id==L"persona_lora_compare") CompareSelectedPersonaLoraVersion();
            else if (b.id==L"persona_lora_export") ExportSelectedPersonaLoraManifest();
            else if (b.id==L"persona_use_selected") {
                if(selectedModelLabPersonaName_.empty()) {
                    statusText_=L"Select a persona first";
                } else {
                    const int idx=FindComboText(personaProfileCombo_,selectedModelLabPersonaName_);
                    if(idx>=0) SendMessageW(personaProfileCombo_,CB_SETCURSEL,(WPARAM)idx,0);
                    LoadSelectedPersonaProfile();
                }
            }
            else if (b.id==L"persona_edit_selected") {
                if(!selectedModelLabPersonaName_.empty()) {
                    const int idx=FindComboText(personaProfileCombo_,selectedModelLabPersonaName_);
                    if(idx>=0) SendMessageW(personaProfileCombo_,CB_SETCURSEL,(WPARAM)idx,0);
                    LoadSelectedPersonaProfile();
                }
                page_=Page::Persona;
                personaTab_=PersonaTab::Profile;
                ApplyPageControls();
            }
            else if (b.id==L"persona_train_selected") {
                if(!selectedModelLabPersonaName_.empty()) {
                    const int idx=FindComboText(personaProfileCombo_,selectedModelLabPersonaName_);
                    if(idx>=0) SendMessageW(personaProfileCombo_,CB_SETCURSEL,(WPARAM)idx,0);
                    LoadSelectedPersonaProfile();
                }
                page_=Page::Trainer;
                ApplyPageControls();
            }
            else if (b.id.rfind(L"foundation_row:",0)==0) {
                selectedModelLabFoundationId_=Narrow(b.id.substr(15));
                statusText_=L"Foundation selected";
            }
            else if (b.id==L"foundation_open_trainer") {
                RefreshTrainerFoundationList(selectedModelLabFoundationId_);
                trainerAdvancedOpen_=true;
                SendMessageW(trainerModeCombo_,CB_SETCURSEL,3,0);
                page_=Page::Trainer;
                ApplyPageControls();
            }
            else if (b.id==L"foundation_approve_selected") {
                if(selectedModelLabFoundationId_.empty()) statusText_=L"Select a foundation first";
                else if(runtime_->trainer.ApproveFoundation(selectedModelLabFoundationId_)) {
                    RefreshTrainerFoundationList(selectedModelLabFoundationId_);
                    statusText_=L"Foundation approved for activation/deployment";
                } else statusText_=L"Foundation could not be approved from its current state";
            }
            else if (b.id==L"foundation_activate_selected") ActivateSelectedFoundationFromLab();
            else if (b.id==L"foundation_rollback") RollbackFoundationFromLab();
            else if (b.id==L"foundation_compare") CompareSelectedFoundationToActive();
            else if (b.id==L"foundation_new_fork") {
                RefreshTrainerFoundationList(selectedModelLabFoundationId_);
                trainerAdvancedOpen_=true;
                page_=Page::Trainer;
                ApplyPageControls();
            }
            if (b.id.rfind(L"job_row:",0)==0) {
                selectedModelLabJobId_=Narrow(b.id.substr(8));
                statusText_=L"Training job selected";
            }
            else if (b.id==L"job_run_selected") RunSelectedTrainerJob();
            else if (b.id==L"job_cancel_selected") CancelSelectedTrainerJob();
            else if (b.id==L"job_retry_selected") RetrySelectedTrainerJob();
            else if (b.id==L"job_recover_stale") RecoverStaleTrainerJobs();
            else if (b.id==L"job_open_trainer") {
                page_=Page::Trainer;
                ApplyPageControls();
            }
            else if (b.id.rfind(L"regmodel:",0)==0) selectedRegistryModel_=(int)std::stol(b.id.substr(9));
            else if (b.id==L"msg_queue") QueueOperatorTestMessage();
            else if (b.id==L"approval_request") RequestLatestSuggestionApproval();
            else if (b.id==L"approval_approve") ApproveFirstPending();
            else if (b.id==L"approval_reject") RejectFirstPending();
            else if (b.id==L"supervisor_takeover") ToggleInvestigatorTakeover();
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
                const auto questionHistory=runtime_->conversationMemory.RecallQuestionHistory(
                    currentConversationId_,16);
                if(!questionHistory.empty()) {
                    if(!simContext_.recalledMemory.empty()) simContext_.recalledMemory+="\n";
                    simContext_.recalledMemory+=questionHistory;
                }
                const auto learnedNotes=runtime_->conversationMemory.RecallLearnedPersonaNotes(
                    simSettings_.persona.name,16);
                if(!learnedNotes.empty()) {
                    if(!simContext_.recalledMemory.empty()) simContext_.recalledMemory+="\n";
                    simContext_.recalledMemory+=learnedNotes;
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
                    std::string ruleReply=simRuleMeaning_;
                    if(simRuleResponseMode_=="persona_variation" && model_) {
                        ruleReply=model_->GeneratePersonaRuleReply(
                            simRuleMeaning_,simContext_);
                        const auto policy=sentinel::simulation::EvaluateSimulationPolicy(
                            simSettings_.ageState,ruleReply);
                        if(!policy.allowed)
                            ruleReply=simRuleMeaning_;
                    }
                    simPreparedReply_=ruleReply;

                    if(simMatchedRule_ && !simMatchedRule_->terminal && model_) {
                        try {
                            const auto generated=model_->GenerateSyntheticReply(
                                simPendingMessage_,simContext_);
                            if(!generated.empty()) {
                                const auto combined=ruleReply+
                                    (ruleReply.empty()?"":" ")+generated;
                                const auto policy=sentinel::simulation::EvaluateSimulationPolicy(
                                    simSettings_.ageState,combined);
                                if(policy.allowed)
                                    simPreparedReply_=combined;
                            }
                        } catch(...) {
                            // Continue-mode failure preserves the approved rule response.
                        }
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
            simTypingStartedTick_=GetTickCount64();
            simTypingDurationMs_=typingDelay;
            SetTimer(hwnd_,kSimReplyTimer,40,nullptr);
            InvalidateRect(hwnd_,nullptr,FALSE);
            return;
        }

        if(id!=kSimReplyTimer) return;
        if(!simReplyPending_) {
            KillTimer(hwnd_,kSimReplyTimer);
            return;
        }

        if(simBotTyping_ && simTypingDurationMs_>0) {
            const auto elapsed=GetTickCount64()-simTypingStartedTick_;
            if(elapsed<(ULONGLONG)simTypingDurationMs_) {
                InvalidateRect(hwnd_,nullptr,FALSE);
                return;
            }
        }
        KillTimer(hwnd_,kSimReplyTimer);

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
                    if(simPendingPersonaMediaIndex_>=0 &&
                       simPendingPersonaMediaIndex_<(int)personaMedia_.size()) {
                        const auto& media=personaMedia_[(size_t)simPendingPersonaMediaIndex_];
                        StoreConversationImage(
                            sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
                            std::filesystem::path(Widen(media.storedPath)),
                            "Persona image");
                        simPendingPersonaMediaIndex_=-1;
                    }

                    if(simPreparedFromRule_) {
                        if(simMatchedRule_) {
                            try {
                                runtime_->responseRuleMatches.Append(
                                    simSettings_.persona.name,
                                    currentConversationId_,
                                    simMatchedRule_->id,
                                    simMatchedRule_->matchType,
                                    simMatchedRule_->matchScore,
                                    simMatchedRule_->trigger,
                                    simPendingMessage_,
                                    simMatchedRule_->responseMode,
                                    simPreparedReply_);
                            } catch(...) {}
                        }
                        if(simMatchedRule_ && !simMatchedRule_->terminal)
                            statusText_=L"Response rule applied; model continuation included";
                        else
                            statusText_=simRuleResponseMode_=="persona_variation"
                                ? L"Terminal response rule applied in persona voice"
                                : L"Terminal exact response rule applied";
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
        simTypingStartedTick_=0;
        simTypingDurationMs_=0;
        simPendingMessage_.clear();
        simPreparedReply_.clear();
        simPreparedFromRule_=false;
        simRuleMeaning_.clear();
        simRuleResponseMode_.clear();
        simMatchedRule_.reset();
        sentinel::simulation::SaveSession(runtime_->root/"simulation-session.tsv",simContext_);
        // One benign proactive nudge is allowed in Simulation after 60 seconds
        // of silence. Live-channel automation remains governed by the operation
        // rules profile and is not enabled by this simulation timer.
        if(!simInitiativeSent_) SetTimer(hwnd_,kSimEngagementTimer,60000,nullptr);
        ScrollSimulationToBottom();
        SetFocus(chatEdit_);
        SendMessageW(chatEdit_,EM_SETSEL,0,0);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

private:
    struct PersonaResponseRuleView {
        long long id{};
        std::string matchType;
        std::string trigger;
        std::string response;
        std::string responseMode{"persona_variation"};
        bool enabled{};
        bool terminal{true};
        int priority{100};
        int matchScore{};
    };
    struct PersonaLearnedNoteView {
        long long id{};
        std::string sourceKind;
        std::string text;
        std::string createdUtc;
    };
    struct Button { RectF rect; std::wstring id; };

    HWND hwnd_{},caseNumberEdit_{},caseTitleEdit_{},chatEdit_{},modelEndpointEdit_{},modelNameEdit_{},modelCombo_{},simScroll_{};
    HWND personaNameEdit_{},personaAgeCombo_{},personaLocationEdit_{},personaInterestsEdit_{},personaStyleEdit_{};
    HWND personaWritingStyleCombo_{},personaProfileCombo_{};
    HWND personaOccupationEdit_{},personaEducationEdit_{},personaFamilyEdit_{},personaBackgroundEdit_{};
    HWND personaGenderCombo_{},personaPronounsCombo_{},personaRelationshipCombo_{},personaPersonalityCombo_{},personaSocialCombo_{},personaConfidenceCombo_{};
    HWND scenarioNameEdit_{},scenarioObjectiveEdit_{},scenarioSeedEdit_{},minDelayEdit_{},maxDelayEdit_{},ageStateCombo_{};
    HWND agencyEndpointEdit_{},agencyIdEdit_{},operatingStateCombo_{};
    HWND subjectDisplayEdit_{},subjectLegalEdit_{},subjectAliasesEdit_{},subjectUsernamesEdit_{},subjectContactsEdit_{},subjectNotesEdit_{};
    HWND identitySourceTypeEdit_{},identitySourceRefEdit_{},identityLeadValueEdit_{},identityConfidenceEdit_{},identityProvenanceEdit_{};
    HWND researchTypeCombo_{},researchProviderCombo_{},researchQueryEdit_{},researchPurposeEdit_{},researchResultEdit_{},researchReferenceEdit_{},researchProvenanceEdit_{},researchConfidenceEdit_{};
    HWND researchProviderIdEdit_{},researchProviderNameEdit_{},researchProviderModeCombo_{},researchProviderEndpointEdit_{},researchProviderCredentialEdit_{},researchProviderNotesEdit_{};
    HWND personaCommunicationCombo_{},personaCognitiveCombo_{},personaSlangCombo_{},personaGrammarCombo_{},personaTypoCombo_{},personaEmojiCombo_{};
    HWND responseRuleTriggerEdit_{},responseRuleResponseEdit_{};
    HWND trainerModeCombo_{},trainerFoundationCombo_{};
    HWND trainerForkNameEdit_{},trainerBasePathEdit_{},trainerLoraNameEdit_{},trainerLoraPathEdit_{},trainerDatasetEdit_{},trainerOutputEdit_{},trainerInstructionEdit_{};
    std::unique_ptr<Runtime> runtime_;
    Page page_{Page::Dashboard};
    PersonaTab personaTab_{PersonaTab::Profile};
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
    std::optional<PersonaResponseRuleView> simMatchedRule_;
    int simLastStartDelayMs_{0};
    int simLastTypingDelayMs_{0};
    ULONGLONG simTypingStartedTick_{0};
    int simTypingDurationMs_{0};
    std::vector<std::pair<RectF,size_t>> simMessageRects_;
    sentinel::simulation::SimulationSettings simSettings_;
    std::unique_ptr<sentinel::operations::IMessageAdapter> messagingAdapter_;
    std::vector<sentinel::operations::ApprovalRequest> approvals_;
    sentinel::simulation::ModelRegistry modelRegistry_;
    int selectedRegistryModel_{-1};
    std::string selectedTrainingReviewId_;
    sentinel::simulation::TrainingDataRegistry trainingData_;
    int selectedDatasetSnapshot_{-1};
    sentinel::simulation::ResponseEvaluation lastEvaluation_;
    sentinel::simulation::EvaluationRunRegistry evaluationRuns_;
    int selectedEvaluationRun_{-1};
    int comparisonEvaluationRun_{-1};
    sentinel::simulation::DeploymentRegistry deploymentRegistry_;
    int selectedDeployment_{-1};
    sentinel::agency::AgencyServerConfig agencyConfig_;
    std::wstring policyStatus_=L"Policy ready";
    std::wstring updateStatus_=L"Updates not checked";
    std::wstring aiDiagnostics_=L"Not run";
    std::wstring jurisdictionStatus_=L"No operating jurisdiction selected";
    std::string operatingStateCode_;
    std::string selectedSubjectId_;
    std::string selectedIdentityLeadId_;
    std::string selectedResearchTaskId_;
    std::vector<sentinel::identity::IdentityResearchProvider> researchProviders_;
    bool researchProviderManagerOpen_{false};
    std::string selectedResearchProviderConfigId_;
    unsigned int selectedResearchProviderTypesMask_{1u};
    bool subjectDraftNew_{false};
    std::vector<PersonaMediaItem> personaMedia_;
    int selectedPersonaMedia_{-1};
    int simPendingPersonaMediaIndex_{-1};
    std::vector<sentinel::simulation::ModelFoundation> trainerFoundations_;
    std::wstring trainerRuntimeStatus_=L"No persona LoRA active";
    std::string selectedModelLabPersonaName_;
    long long selectedModelLabLoraId_{0};
    std::string selectedModelLabFoundationId_;
    std::string selectedModelLabJobId_;
    bool trainerAdvancedOpen_{false};
    bool responseRuleExactWording_{false};
    bool responseRuleTerminal_{true};
    bool personaShowLearnedNotes_{false};

    HFONT chatFont_{};
    HFONT uiFont_{};
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
        target_->DrawTextLayout(
            D2D1::Point2F(x,y),layout.Get(),br,
            (D2D1_DRAW_TEXT_OPTIONS)(D2D1_DRAW_TEXT_OPTIONS_CLIP|D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT));
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
        target_->DrawTextLayout(
            D2D1::Point2F(x,y),layout.Get(),br,
            (D2D1_DRAW_TEXT_OPTIONS)(D2D1_DRAW_TEXT_OPTIONS_CLIP|D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT));
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


    void AddIconButton(
        const std::wstring& id,
        IconKind icon,
        float x,float y,float w,float h,
        bool primary=false)
    {
        Rounded(
            x,y,w,h,
            primary?brush_.blue.Get():brush_.panel2.Get(),
            primary?brush_.cyan.Get():brush_.border.Get(),7);
        const float iconSize=std::min(w,h)*0.54f;
        DrawIcon(
            icon,
            x+(w-iconSize)*0.5f,
            y+(h-iconSize)*0.5f,
            iconSize,
            primary?brush_.text.Get():brush_.cyan.Get());
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
            case IconKind::Smile:
                target_->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(x+s*0.50f,y+s*0.50f),s*0.36f,s*0.36f),
                    color,t);
                target_->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(x+s*0.38f,y+s*0.42f),s*0.035f,s*0.035f),
                    color);
                target_->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(x+s*0.62f,y+s*0.42f),s*0.035f,s*0.035f),
                    color);
                {
                    ComPtr<ID2D1PathGeometry> geo;
                    factory_->CreatePathGeometry(&geo);
                    ComPtr<ID2D1GeometrySink> sink;
                    geo->Open(&sink);
                    sink->BeginFigure(
                        D2D1::Point2F(x+s*0.33f,y+s*0.58f),
                        D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddBezier(D2D1::BezierSegment(
                        D2D1::Point2F(x+s*0.40f,y+s*0.72f),
                        D2D1::Point2F(x+s*0.60f,y+s*0.72f),
                        D2D1::Point2F(x+s*0.67f,y+s*0.58f)));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->Close();
                    target_->DrawGeometry(geo.Get(),color,t);
                }
                break;
            case IconKind::Paperclip: {
                ComPtr<ID2D1PathGeometry> geo;
                factory_->CreatePathGeometry(&geo);
                ComPtr<ID2D1GeometrySink> sink;
                geo->Open(&sink);
                sink->BeginFigure(
                    D2D1::Point2F(x+s*0.67f,y+s*0.22f),
                    D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddBezier(D2D1::BezierSegment(
                    D2D1::Point2F(x+s*0.82f,y+s*0.36f),
                    D2D1::Point2F(x+s*0.78f,y+s*0.54f),
                    D2D1::Point2F(x+s*0.63f,y+s*0.69f)));
                sink->AddLine(D2D1::Point2F(x+s*0.46f,y+s*0.84f));
                sink->AddBezier(D2D1::BezierSegment(
                    D2D1::Point2F(x+s*0.31f,y+s*0.96f),
                    D2D1::Point2F(x+s*0.12f,y+s*0.79f),
                    D2D1::Point2F(x+s*0.25f,y+s*0.63f)));
                sink->AddLine(D2D1::Point2F(x+s*0.53f,y+s*0.36f));
                sink->AddBezier(D2D1::BezierSegment(
                    D2D1::Point2F(x+s*0.61f,y+s*0.28f),
                    D2D1::Point2F(x+s*0.70f,y+s*0.37f),
                    D2D1::Point2F(x+s*0.62f,y+s*0.45f)));
                sink->AddLine(D2D1::Point2F(x+s*0.39f,y+s*0.68f));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                target_->DrawGeometry(geo.Get(),color,t);
                break;
            }
        }
    }

    IconKind NavIcon(int i) const {
        static const IconKind icons[]={
            IconKind::Home,IconKind::Folder,IconKind::Database,IconKind::Document,IconKind::Shield,
            IconKind::Chat,IconKind::Document,IconKind::Database,IconKind::Gear,IconKind::Chat,IconKind::Shield,IconKind::Database,IconKind::Gear
        };
        return icons[std::clamp(i,0,12)];
    }


    int MainNavIndex(Page page) const {
        switch(page) {
            case Page::Dashboard: return 0;
            case Page::Cases: return 1;
            case Page::Subjects:
            case Page::IdentityResearch: return 2;
            case Page::Simulation: return 3;
            case Page::Persona: return 4;
            case Page::Messaging: return 5;
            case Page::Supervisor: return 6;
            case Page::Evidence: return 7;
            case Page::Audit:
            case Page::Verification: return 8;
            case Page::ModelLab:
            case Page::Trainer:
            case Page::ModelLabDatasets:
            case Page::ModelLabPersonas:
            case Page::ModelLabFoundations:
            case Page::ModelLabJobs:
            case Page::ModelLabEvaluation:
            case Page::ModelLabDeployment: return 9;
            case Page::Agency: return 10;
            case Page::Settings: return 11;
        }
        return 0;
    }

    Page MainNavPage(int index) const {
        static const Page pages[]={
            Page::Dashboard,
            Page::Cases,
            Page::Subjects,
            Page::Simulation,
            Page::Persona,
            Page::Messaging,
            Page::Supervisor,
            Page::Evidence,
            Page::Audit,
            Page::ModelLab,
            Page::Agency,
            Page::Settings
        };
        return pages[std::clamp(index,0,11)];
    }

    const wchar_t* MainNavLabel(int index) const {
        static const wchar_t* labels[]={
            L"Dashboard",
            L"Cases",
            L"Subjects & Identity",
            L"Simulation Chat",
            L"Personas",
            L"Channels & Messaging",
            L"Supervisor & Approvals",
            L"Evidence",
            L"Audit & Compliance",
            L"Model Lab",
            L"Agency Server",
            L"Settings"
        };
        return labels[std::clamp(index,0,11)];
    }

    IconKind MainNavIcon(int index) const {
        static const IconKind icons[]={
            IconKind::Home,
            IconKind::Folder,
            IconKind::Search,
            IconKind::Chat,
            IconKind::Document,
            IconKind::Chat,
            IconKind::Shield,
            IconKind::Database,
            IconKind::Chain,
            IconKind::Gear,
            IconKind::Database,
            IconKind::Gear
        };
        return icons[std::clamp(index,0,11)];
    }

    void DrawBrand() {
        // The approved SARA 1.0.15 PNG already contains transparency.
        // Render it directly on the dark shell; never add a white/light plate.
        if(brandBitmap_) {
            const auto sz=brandBitmap_->GetSize();
            const float maxW=(float)kSidebar-28.0f;
            const float maxH=(float)kHeader-8.0f;
            const float scale=std::min(
                maxW/std::max(1.0f,sz.width),
                maxH/std::max(1.0f,sz.height));
            const float w=sz.width*scale;
            const float h=sz.height*scale;
            const float x=((float)kSidebar-w)*0.5f;
            const float y=((float)kHeader-h)*0.5f;
            target_->DrawBitmap(
                brandBitmap_.Get(),D2D1::RectF(x,y,x+w,y+h),1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            return;
        }

        Text(L"SARA",24,16,172,32,titleFmt_.Get(),brush_.text.Get());
        Text(L"Synthetic Adaptive Response Agent",24,49,172,16,tinyFmt_.Get(),brush_.muted.Get());
    }

    void DrawSidebar() {
        // SARA_PRODUCT_CONTRACT_MAIN_NAV_BEGIN
        // This is the permanent application shell. Model Lab never replaces it.
        constexpr int kMainNavCount=12;
        constexpr float rowH=44.0f;
        const int active=MainNavIndex(page_);

        for(int i=0;i<kMainNavCount;i++) {
            const float y=(float)kHeader+16.0f+i*rowH;
            const bool selected=i==active;
            if(selected) {
                target_->FillRectangle(
                    D2D1::RectF(0,y-4,(float)kSidebar,y+36),
                    brush_.panel2.Get());
                target_->FillRectangle(
                    D2D1::RectF(0,y-4,4,y+36),
                    brush_.cyan.Get());
                Rounded(18,y,34,30,brush_.sidebar.Get(),brush_.border.Get(),8);
            }

            DrawIcon(
                MainNavIcon(i),24,y+4,21,
                selected?brush_.cyan.Get():brush_.muted.Get());
            TextLine(
                MainNavLabel(i),58,y+3,(float)kSidebar-66,26,
                smallFmt_.Get(),
                selected?brush_.cyan.Get():brush_.text.Get());
        }
        // SARA_PRODUCT_CONTRACT_MAIN_NAV_END

        const float footerY=(float)kHeader+16.0f+kMainNavCount*rowH+16.0f;
        target_->DrawLine(
            D2D1::Point2F(18,footerY-8),
            D2D1::Point2F((float)kSidebar-18,footerY-8),
            brush_.border.Get(),1.0f);
        Text(std::wstring(L"SARA v")+Widen(SARA_VERSION_STR),
            24,footerY+2,170,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"Secure Investigator Mode",
            24,footerY+24,176,20,smallFmt_.Get(),brush_.green.Get());
    }

    void DrawHeader(float w) {
        const int section=MainNavIndex(page_);

        Text(L"SARA",kSidebar+28,17,62,20,tinyFmt_.Get(),brush_.cyan.Get());
        Text(L"|",kSidebar+92,16,12,20,tinyFmt_.Get(),brush_.border.Get());
        TextLine(
            MainNavLabel(section),
            kSidebar+108,15,240,24,smallFmt_.Get(),brush_.text.Get());

        TextLine(
            L"INVESTIGATIONS  |  SIMULATION  |  EVIDENCE  |  AI SUPPORT",
            kSidebar+28,39,410,20,tinyFmt_.Get(),brush_.muted.Get());

        Rounded(w-405,16,255,42,brush_.sidebar.Get(),brush_.border.Get(),8);
        DrawIcon(IconKind::Search,w-390,27,18,brush_.muted.Get());
        Text(L"Search SARA...",w-366,27,190,22,smallFmt_.Get(),brush_.muted.Get());

        Rounded(w-132,18,36,36,brush_.panel2.Get(),brush_.border.Get(),18);
        DrawIcon(IconKind::Shield,w-122,27,18,brush_.cyan.Get());
        Text(L"SARA",w-84,20,58,18,smallFmt_.Get(),brush_.text.Get());
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
        PageTitle(
            L"Dashboard",
            L"Operational command view for cases, Simulation Chat, approvals, evidence, channels, and compliance");

        const float x=kSidebar+28.0f;
        const float y=kHeader+96.0f;
        const float gap=14.0f;
        const float contentW=w-x-28.0f;

        size_t pendingApprovals=0;
        for(const auto& approval:approvals_)
            if(approval.status==sentinel::operations::ApprovalStatus::Pending)
                ++pendingApprovals;

        const float metricW=(contentW-gap*3.0f)/4.0f;
        Metric(
            x,y,metricW,
            L"Open Cases",
            std::to_wstring(runtime_->OpenCaseCount()),
            L"Active investigations",
            brush_.cyan.Get(),IconKind::Folder);
        Metric(
            x+(metricW+gap),y,metricW,
            L"Chat Turns",
            std::to_wstring(simContext_.history.size()),
            L"Current Simulation Chat",
            brush_.blue.Get(),IconKind::Chat);
        Metric(
            x+2*(metricW+gap),y,metricW,
            L"Pending Approvals",
            std::to_wstring(pendingApprovals),
            L"Human review queue",
            pendingApprovals?brush_.yellow.Get():brush_.green.Get(),
            IconKind::Shield);
        Metric(
            x+3*(metricW+gap),y,metricW,
            L"Evidence Items",
            std::to_wstring(runtime_->EvidenceCount()),
            L"Encrypted case objects",
            brush_.green.Get(),IconKind::Database);

        const float mainY=y+126.0f;
        const float mainH=236.0f;
        const float leftW=(contentW-gap)*0.59f;
        const float rightW=contentW-gap-leftW;
        const float rightX=x+leftW+gap;

        // Current operational context.
        Rounded(x,mainY,leftW,mainH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Current Operational Context",x+18,mainY+12,leftW-36,30,h1Fmt_.Get(),brush_.text.Get());

        const std::wstring caseName=cases_.empty()
            ? L"No case selected"
            : Widen(cases_[selectedCase_].caseNumber)+L" - "+Widen(cases_[selectedCase_].title);
        const std::wstring conversation=currentConversationTitle_.empty()
            ? L"No active conversation"
            : currentConversationTitle_;

        struct ContextRow {
            const wchar_t* label;
            std::wstring value;
            ID2D1Brush* valueBrush;
        };
        ContextRow rows[]={
            {L"Case",caseName,cases_.empty()?brush_.yellow.Get():brush_.text.Get()},
            {L"Persona",Widen(simSettings_.persona.name),brush_.cyan.Get()},
            {L"Conversation",conversation,brush_.text.Get()},
            {L"Model",modelStatus_,modelStatus_.find(L"Connected")!=std::wstring::npos?brush_.green.Get():brush_.yellow.Get()},
            {L"Jurisdiction",jurisdictionStatus_,jurisdictionStatus_.find(L"No operating")!=std::wstring::npos?brush_.yellow.Get():brush_.cyan.Get()}
        };

        float rowY=mainY+46.0f;
        for(const auto& row:rows) {
            TextLine(row.label,x+20,rowY,92,24,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(row.value,x+120,rowY-2,leftW-140,28,smallFmt_.Get(),row.valueBrush);
            target_->DrawLine(
                D2D1::Point2F(x+20,rowY+30),
                D2D1::Point2F(x+leftW-20,rowY+30),
                brush_.border.Get(),0.8f);
            rowY+=30.0f;
        }

        const float contextActionGap=8.0f;
        const float contextActionW=(leftW-40.0f-contextActionGap*2.0f)/3.0f;
        const float contextActionY=mainY+198.0f;
        AddButton(L"dashboard_simulation",L"Simulation Chat",x+20,contextActionY,contextActionW,28,true);
        AddButton(L"dashboard_subjects",L"Subjects",x+20+contextActionW+contextActionGap,contextActionY,contextActionW,28,false);
        AddButton(L"dashboard_channels",L"Messaging",x+20+(contextActionW+contextActionGap)*2.0f,contextActionY,contextActionW,28,false);

        // Readiness/status board.
        Rounded(rightX,mainY,rightW,mainH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Operational Readiness",rightX+18,mainY+12,rightW-36,30,h1Fmt_.Get(),brush_.text.Get());

        const bool auditOk=runtime_->audit.VerifyChain();
        const bool channelOk=messagingAdapter_ && messagingAdapter_->Connected();
        const bool aiOk=modelStatus_.find(L"Connected")!=std::wstring::npos;
        const bool caseReady=!cases_.empty();
        const bool agencyOn=agencyConfig_.enabled;

        struct ReadyRow {
            const wchar_t* label;
            bool ok;
            const wchar_t* readyText;
            const wchar_t* notReadyText;
        };
        ReadyRow ready[]={
            {L"Case context",caseReady,L"Selected",L"Select/create case"},
            {L"Audit chain",auditOk,L"Verified",L"Integrity warning"},
            {L"Local AI",aiOk,L"Connected",L"Needs attention"},
            {L"Messaging adapter",channelOk,L"Connected",L"Local/offline"},
            {L"Agency sync",agencyOn,L"Enabled",L"Local-only mode"}
        };

        float readyY=mainY+46.0f;
        for(const auto& item:ready) {
            StatusDot(
                rightX+26,readyY+11,4,
                item.ok?brush_.green.Get():brush_.yellow.Get());
            TextLine(item.label,rightX+40,readyY,rightW*0.48f,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(
                item.ok?item.readyText:item.notReadyText,
                rightX+rightW*0.52f,readyY,rightW*0.42f,24,tinyFmt_.Get(),
                item.ok?brush_.green.Get():brush_.yellow.Get(),
                DWRITE_TEXT_ALIGNMENT_TRAILING);
            readyY+=30.0f;
        }

        Rounded(
            rightX+18,mainY+196,rightW-36,30,
            brush_.sidebar.Get(),brush_.border.Get(),7);
        TextLine(
            pendingApprovals
                ? std::to_wstring(pendingApprovals)+L" action(s) waiting for human approval"
                : L"No pending supervisor approvals",
            rightX+30,mainY+198,rightW-60,24,tinyFmt_.Get(),
            pendingApprovals?brush_.yellow.Get():brush_.green.Get());

        // Recent auditable activity.
        const float bottomY=mainY+mainH+12.0f;
        const float bottomH=std::clamp(h-bottomY-18.0f,150.0f,180.0f);
        Rounded(x,bottomY,leftW,bottomH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Recent Auditable Activity",x+18,bottomY+12,leftW-36,28,h1Fmt_.Get(),brush_.text.Get());

        std::vector<std::wstring> activity;
        sqlite3_stmt* recent{};
        if(sqlite3_prepare_v2(
            runtime_->db.Handle(),
            "SELECT action,target_type,target_id FROM audit_records ORDER BY sequence DESC LIMIT 4",
            -1,&recent,nullptr)==SQLITE_OK)
        {
            while(sqlite3_step(recent)==SQLITE_ROW) {
                const int action=sqlite3_column_int(recent,0);
                const char* type=(const char*)sqlite3_column_text(recent,1);
                const char* id=(const char*)sqlite3_column_text(recent,2);
                std::wstring line=AuditActionName(action)+L" | "+Widen(type?type:"");
                if(id && *id) line+=L" | "+Widen(id);
                activity.push_back(std::move(line));
            }
        }
        sqlite3_finalize(recent);

        if(activity.empty())
            activity.push_back(L"Secure local investigator workspace initialized");

        float activityY=bottomY+46.0f;
        for(size_t i=0;i<activity.size() && i<3;i++) {
            DrawIcon(
                i==0?IconKind::Document:IconKind::Check,
                x+20,activityY+1,18,
                i==0?brush_.cyan.Get():brush_.green.Get());
            TextLine(activity[i],x+48,activityY,leftW-68,22,smallFmt_.Get(),brush_.text.Get());
            activityY+=32.0f;
        }

        // Operational quick actions.
        Rounded(rightX,bottomY,rightW,bottomH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Operational Quick Actions",rightX+18,bottomY+12,rightW-36,28,h1Fmt_.Get(),brush_.text.Get());

        const float actionGap=8.0f;
        const float actionW=(rightW-44.0f-actionGap)/2.0f;
        const float actionH=30.0f;
        const float ax=rightX+18.0f;
        const float bx=ax+actionW+actionGap;
        const float ay=bottomY+46.0f;

        AddButton(L"dashboard_cases",L"Cases",ax,ay,actionW,actionH,true);
        AddButton(L"dashboard_subjects",L"Subjects",bx,ay,actionW,actionH,false);

        AddButton(L"dashboard_simulation",L"Simulation Chat",ax,ay+38,actionW,actionH,false);
        AddButton(L"dashboard_channels",L"Messaging",bx,ay+38,actionW,actionH,false);

        AddButton(L"dashboard_evidence",L"Evidence",ax,ay+76,actionW,actionH,false);
        AddButton(L"dashboard_supervisor",L"Approvals",bx,ay+76,actionW,actionH,false);
    }

    void DrawCases(float w,float h) {
        PageTitle(L"Cases",L"Manage investigations, subject context, Simulation Chat, evidence, compliance, and case progress");
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
            const float actionY=detail+112.0f;
            AddButton(L"case_subjects",L"Subjects",x+20,actionY,108,34,false);
            AddButton(L"case_simulation",L"Simulation Chat",x+138,actionY,132,34,true);
            AddButton(L"case_evidence",L"Evidence",x+280,actionY,104,34,false);
            AddButton(L"case_audit",L"Audit & Compliance",x+394,actionY,150,34,false);
        } else {
            Text(L"No cases yet. Enter a case number and title above, then create the first case.",x+20,detail+40,w-x-80,50,bodyFmt_.Get(),brush_.muted.Get());
        }
    }

    void DrawEvidence(float w,float h) {
        PageTitle(L"Evidence",L"Secure evidence management, verification, and chain of custody");
        const float x=kSidebar+28.0f;
        const float y=kHeader+100.0f;
        const float contentW=w-x-28.0f;

        if(cases_.empty()) {
            Rounded(x,y,contentW,180,brush_.panel.Get(),brush_.border.Get(),8);
            Text(L"Create a case before importing evidence.",x+24,y+28,contentW-48,30,h1Fmt_.Get(),brush_.text.Get());
            AddButton(L"dashboard",L"Return to Dashboard",x+24,y+82,190,44,true);
            return;
        }

        sentinel::EvidenceService evidenceService(
            runtime_->root/"evidence",
            runtime_->db,
            runtime_->random,
            runtime_->hash,
            runtime_->cipher,
            runtime_->audit);

        std::vector<sentinel::EvidenceVerificationStatus> verification;
        verification.reserve(evidence_.size());
        size_t passCount=0,failCount=0;
        for(const auto& item:evidence_) {
            auto status=evidenceService.LastVerification(item.id);
            if(status.state==sentinel::EvidenceVerificationState::Verified) ++passCount;
            else if(status.state==sentinel::EvidenceVerificationState::Failed) ++failCount;
            verification.push_back(std::move(status));
        }

        if(!evidence_.empty())
            selectedEvidence_=std::min(selectedEvidence_,evidence_.size()-1);

        TextLine(
            L"Case "+Widen(cases_[selectedCase_].caseNumber)+L" - "+Widen(cases_[selectedCase_].title),
            x,y,contentW-180,26,bodyFmt_.Get(),brush_.text.Get());
        AddButton(L"import",L"+ Import Evidence",x+contentW-160,y-4,160,38,true);

        const float metricY=y+42.0f;
        const float gap=14.0f;
        const float metricW=(contentW-gap*2.0f)/3.0f;
        Metric(x,metricY,metricW,L"Evidence Items",std::to_wstring(evidence_.size()),
            L"Encrypted case objects",brush_.cyan.Get(),IconKind::Database);
        Metric(x+metricW+gap,metricY,metricW,L"Last Checks",
            std::to_wstring(passCount)+L" pass",
            failCount?std::to_wstring(failCount)+L" integrity alert(s)":L"No recorded failures",
            failCount?brush_.red.Get():brush_.green.Get(),IconKind::Check);
        const bool auditOk=runtime_->audit.VerifyChain();
        Metric(x+2.0f*(metricW+gap),metricY,metricW,L"Audit Chain",
            auditOk?L"Valid":L"REVIEW",
            auditOk?L"Tamper-evident ledger verified":L"Integrity validation failed",
            auditOk?brush_.green.Get():brush_.red.Get(),IconKind::Chain);

        const float bodyY=metricY+126.0f;
        const float bodyH=std::max(300.0f,h-bodyY-24.0f);
        const float rightW=std::clamp(contentW*0.37f,270.0f,310.0f);
        const float leftW=contentW-rightW-gap;
        const float rightX=x+leftW+gap;

        Rounded(x,bodyY,leftW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Evidence Library",x+18,bodyY+12,leftW-36,28,h1Fmt_.Get(),brush_.text.Get());

        float rowY=bodyY+50.0f;
        if(evidence_.empty()) {
            Rounded(x+16,rowY,leftW-32,58,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"No evidence imported for this case.",x+30,rowY+10,leftW-60,38,smallFmt_.Get(),brush_.muted.Get());
        } else {
            const size_t maxRows=std::min<size_t>(evidence_.size(),5);
            for(size_t i=0;i<maxRows;i++) {
                const bool selected=i==selectedEvidence_;
                const auto state=verification[i].state;
                ID2D1Brush* stateBrush=
                    state==sentinel::EvidenceVerificationState::Verified?brush_.green.Get():
                    state==sentinel::EvidenceVerificationState::Failed?brush_.red.Get():
                    brush_.yellow.Get();
                const wchar_t* stateText=
                    state==sentinel::EvidenceVerificationState::Verified?L"LAST CHECK PASS":
                    state==sentinel::EvidenceVerificationState::Failed?L"INTEGRITY ALERT":
                    L"NOT YET VERIFIED";

                Rounded(x+12,rowY,leftW-24,54,
                    selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),8);
                std::wstring filename=Widen(evidence_[i].originalFilename);
                if(filename.size()>38) filename=filename.substr(0,35)+L"...";
                TextLine(filename,x+24,rowY+5,leftW-150,21,smallFmt_.Get(),brush_.text.Get());
                TextLine(
                    std::to_wstring(evidence_[i].originalSize/1024)+L" KB | "+
                    Widen(evidence_[i].originalHash.ToHex()).substr(0,16)+L"...",
                    x+24,rowY+28,leftW-150,18,tinyFmt_.Get(),brush_.muted.Get());
                TextLine(stateText,x+leftW-140,rowY+17,112,18,tinyFmt_.Get(),stateBrush,DWRITE_TEXT_ALIGNMENT_TRAILING);
                buttons_.push_back({{x+12,rowY,x+leftW-12,rowY+54},L"ev:"+std::to_wstring(i)});
                rowY+=62.0f;
            }
        }

        Rounded(rightX,bodyY,rightW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Evidence Details",rightX+18,bodyY+12,rightW-36,28,h1Fmt_.Get(),brush_.text.Get());

        if(evidence_.empty()) {
            Text(
                L"Import a file to create an encrypted SEV evidence container tied to the selected case.",
                rightX+18,bodyY+54,rightW-36,70,smallFmt_.Get(),brush_.muted.Get());
            return;
        }

        const auto& item=evidence_[selectedEvidence_];
        const auto& status=verification[selectedEvidence_];
        ID2D1Brush* statusBrush=
            status.state==sentinel::EvidenceVerificationState::Verified?brush_.green.Get():
            status.state==sentinel::EvidenceVerificationState::Failed?brush_.red.Get():
            brush_.yellow.Get();
        const std::wstring statusText=
            status.state==sentinel::EvidenceVerificationState::Verified?L"LAST VERIFICATION: PASS":
            status.state==sentinel::EvidenceVerificationState::Failed?L"LAST VERIFICATION: FAILED":
            L"NOT YET VERIFIED";

        Rounded(rightX+16,bodyY+52,rightW-32,62,brush_.sidebar.Get(),statusBrush,8);
        TextLine(statusText,rightX+28,bodyY+61,rightW-56,20,smallFmt_.Get(),statusBrush);
        TextLine(
            status.checkedUtc.empty()?L"Run Verify Now to authenticate current bytes.":Widen(status.checkedUtc),
            rightX+28,bodyY+83,rightW-56,18,tinyFmt_.Get(),brush_.muted.Get());

        TextLine(L"Filename",rightX+18,bodyY+132,rightW-36,18,tinyFmt_.Get(),brush_.muted.Get());
        Text(Widen(item.originalFilename),rightX+18,bodyY+152,rightW-36,36,smallFmt_.Get(),brush_.text.Get());

        TextLine(L"Original SHA-256",rightX+18,bodyY+196,rightW-36,18,tinyFmt_.Get(),brush_.muted.Get());
        Text(Widen(item.originalHash.ToHex()),rightX+18,bodyY+216,rightW-36,38,tinyFmt_.Get(),brush_.text.Get());

        TextLine(L"Container SHA-256",rightX+18,bodyY+258,rightW-36,18,tinyFmt_.Get(),brush_.muted.Get());
        Text(Widen(item.containerHash.ToHex()),rightX+18,bodyY+278,rightW-36,38,tinyFmt_.Get(),brush_.text.Get());

        const float actionY=bodyY+bodyH-78.0f;
        AddButton(L"verify",L"Verify Now",rightX+18,actionY,rightW-36,32,true);
        const float half=(rightW-44.0f)/2.0f;
        AddButton(L"verification_center",L"Details",rightX+18,actionY+40,half,30,false);
        AddButton(L"integrity",L"Audit Chain",rightX+26+half,actionY+40,half,30,false);
    }

    void DrawAudit(float w,float h) {
        PageTitle(L"Audit & Compliance",L"Audit integrity, evidence verification, jurisdiction controls, and operational accountability");
        const float x=kSidebar+28.0f;
        const float y=kHeader+100.0f;
        const float gap=14.0f;
        const float contentW=w-x-28.0f;
        const bool auditOk=runtime_->audit.VerifyChain();

        long long evidenceAlerts=0;
        sqlite3_stmt* alertStmt{};
        if(sqlite3_prepare_v2(
            runtime_->db.Handle(),
            "SELECT COUNT(*) FROM audit_records WHERE action=?1",
            -1,&alertStmt,nullptr)==SQLITE_OK)
        {
            sqlite3_bind_int(alertStmt,1,(int)sentinel::AuditAction::EvidenceIntegrityFailure);
            if(sqlite3_step(alertStmt)==SQLITE_ROW)
                evidenceAlerts=sqlite3_column_int64(alertStmt,0);
        }
        sqlite3_finalize(alertStmt);

        const float card=(contentW-gap*3.0f)/4.0f;
        Metric(x,y,card,L"Audit Chain",auditOk?L"Valid":L"REVIEW",
            auditOk?L"Tamper-evident ledger":L"Integrity validation failed",
            auditOk?brush_.green.Get():brush_.red.Get(),IconKind::Chain);
        Metric(x+card+gap,y,card,L"Records",std::to_wstring(runtime_->AuditCount()),
            L"Recorded system events",brush_.cyan.Get(),IconKind::Document);
        Metric(x+2.0f*(card+gap),y,card,L"Evidence Alerts",std::to_wstring(evidenceAlerts),
            evidenceAlerts?L"Recorded verification failures":L"No integrity failures recorded",
            evidenceAlerts?brush_.red.Get():brush_.green.Get(),IconKind::Shield);
        Metric(
            x+3.0f*(card+gap),y,card,
            L"Jurisdiction",
            operatingStateCode_.empty()?L"Review":Widen(operatingStateCode_),
            operatingStateCode_.empty()?L"Rules profile not selected":jurisdictionStatus_,
            operatingStateCode_.empty()?brush_.yellow.Get():brush_.cyan.Get(),
            IconKind::Shield);

        const float tableY=y+126.0f;
        const float integrityH=104.0f;
        const float tableH=std::max(220.0f,h-tableY-integrityH-36.0f);
        Rounded(x,tableY,contentW,tableH,brush_.panel.Get(),brush_.border.Get(),8);
        TextLine(L"Timestamp",x+18,tableY+12,178,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Action",x+210,tableY+12,220,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Target",x+442,tableY+12,contentW-590,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Chain",x+contentW-102,tableY+12,78,20,tinyFmt_.Get(),brush_.muted.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);

        sqlite3_stmt* s{};
        int row=0;
        if(sqlite3_prepare_v2(
            runtime_->db.Handle(),
            "SELECT timestamp,action,target_type,target_id FROM audit_records ORDER BY sequence DESC LIMIT 8",
            -1,&s,nullptr)==SQLITE_OK)
        {
            while(sqlite3_step(s)==SQLITE_ROW) {
                const float yy=tableY+42.0f+row*42.0f;
                if(yy+24.0f>tableY+tableH-10.0f) break;
                const char* ts=(const char*)sqlite3_column_text(s,0);
                const int action=sqlite3_column_int(s,1);
                const char* type=(const char*)sqlite3_column_text(s,2);
                const char* id=(const char*)sqlite3_column_text(s,3);

                TextLine(Widen(ts?ts:""),x+18,yy,178,20,tinyFmt_.Get(),brush_.text.Get());
                TextLine(AuditActionName(action),x+210,yy,220,20,smallFmt_.Get(),
                    action==(int)sentinel::AuditAction::EvidenceIntegrityFailure?brush_.red.Get():brush_.text.Get());

                std::wstring target=Widen(type?type:"");
                if(id && *id) target+=L" | "+Widen(id);
                if(target.size()>32) target=target.substr(0,29)+L"...";
                TextLine(target,x+442,yy,contentW-590,20,tinyFmt_.Get(),brush_.text.Get());

                Badge(
                    auditOk?L"Valid":L"Review",
                    x+contentW-96,yy-3,
                    auditOk?brush_.green.Get():brush_.red.Get(),
                    72);
                ++row;
            }
        }
        sqlite3_finalize(s);

        if(row==0)
            TextLine(L"No audit records yet.",x+18,tableY+58,contentW-36,30,smallFmt_.Get(),brush_.muted.Get());

        const float bottomY=tableY+tableH+12.0f;
        Rounded(x,bottomY,contentW,integrityH,brush_.panel.Get(),brush_.border.Get(),8);
        TextLine(L"Audit Chain Integrity",x+18,bottomY+12,260,26,h1Fmt_.Get(),brush_.text.Get());
        Text(
            auditOk
                ? L"Current audit chain verifies from genesis through the latest record."
                : L"WARNING: the audit chain failed cryptographic sequence/hash validation. Treat displayed history as requiring review.",
            x+20,bottomY+46,contentW-390,42,smallFmt_.Get(),
            auditOk?brush_.green.Get():brush_.red.Get());
        AddButton(L"verification_center",L"Verification Center",x+contentW-360,bottomY+34,166,38,false);
        AddButton(L"integrity",L"Verify Audit Chain",x+contentW-180,bottomY+34,162,38,true);
    }

    void DrawVerification(float w,float h) {
        PageTitle(L"Verification",L"Evidence authentication and integrity validation");
        const float x=kSidebar+28.0f;
        const float y=kHeader+100.0f;
        const float contentW=w-x-28.0f;

        const bool valid=lastVerify_.rfind(L"VALID |",0)==0;
        const bool failed=lastVerify_.rfind(L"INVALID |",0)==0 || lastVerify_.rfind(L"ERROR |",0)==0;
        ID2D1Brush* stateBrush=valid?brush_.green.Get():failed?brush_.red.Get():brush_.cyan.Get();
        const wchar_t* stateText=valid?L"VALID":failed?L"FAILED":L"READY";

        const float leftW=contentW-348.0f;
        Rounded(x,y,leftW,230,brush_.panel.Get(),brush_.border.Get(),8);
        TextLine(L"Verification Result",x+18,y+14,leftW-36,28,h1Fmt_.Get(),brush_.text.Get());
        Rounded(x+24,y+62,120,120,brush_.sidebar.Get(),stateBrush,22);
        TextLine(stateText,x+38,y+95,92,38,h1Fmt_.Get(),stateBrush,DWRITE_TEXT_ALIGNMENT_CENTER);
        TextLine(stateText,x+170,y+66,leftW-194,46,bigFmt_.Get(),stateBrush);
        Text(lastVerify_,x+170,y+120,leftW-194,72,smallFmt_.Get(),brush_.muted.Get());

        const float rightX=x+leftW+14.0f;
        Rounded(rightX,y,334,230,brush_.panel.Get(),brush_.border.Get(),8);
        TextLine(L"Evidence Container",rightX+18,y+14,298,28,h1Fmt_.Get(),brush_.text.Get());
        Text(
            evidence_.empty()?L"No evidence selected":Widen(evidence_[selectedEvidence_].originalFilename),
            rightX+18,y+58,298,48,smallFmt_.Get(),brush_.text.Get());
        AddButton(L"verify",L"Verify Selected Evidence",rightX+18,y+116,298,48,true);
        TextLine(
            L"Verification always re-reads the current stored bytes.",
            rightX+18,y+178,298,24,tinyFmt_.Get(),brush_.muted.Get());

        const float stepsY=y+248.0f;
        const float stepsH=std::max(260.0f,h-stepsY-24.0f);
        Rounded(x,stepsY,contentW,stepsH,brush_.panel.Get(),brush_.border.Get(),8);
        TextLine(L"Verification Steps",x+18,stepsY+14,250,28,h1Fmt_.Get(),brush_.text.Get());

        const wchar_t* steps[]={
            L"1  SEV container structure validation",
            L"2  Stored container SHA-256 comparison",
            L"3  AES-GCM authentication / decryption",
            L"4  Original plaintext SHA-256 comparison",
            L"5  Audit result recorded"
        };
        for(int i=0;i<5;i++) {
            const float yy=stepsY+58.0f+i*38.0f;
            StatusDot(x+28,yy+10,3,valid?brush_.green.Get():failed?brush_.red.Get():brush_.muted.Get());
            TextLine(steps[i],x+44,yy,contentW-190,22,smallFmt_.Get(),brush_.text.Get());
            TextLine(
                valid?L"Completed":failed?L"Review":L"Ready",
                x+contentW-126,yy,100,22,tinyFmt_.Get(),
                valid?brush_.green.Get():failed?brush_.red.Get():brush_.muted.Get(),
                DWRITE_TEXT_ALIGNMENT_TRAILING);
        }

        Text(
            valid
                ? L"PASS means the current stored evidence bytes authenticated and matched both hashes at the time of this check."
                : failed
                    ? L"FAILED is preserved as an evidence-integrity audit event. Do not treat the item as authenticated until the discrepancy is resolved."
                    : L"No verification has been run in this session. A historical audit PASS does not replace re-checking the current stored bytes.",
            x+20,stepsY+stepsH-62,contentW-40,46,tinyFmt_.Get(),
            valid?brush_.green.Get():failed?brush_.red.Get():brush_.muted.Get());
    }

    void ClearSubjectEditors() {
        HWND edits[]={
            subjectDisplayEdit_,subjectLegalEdit_,subjectAliasesEdit_,subjectUsernamesEdit_,
            subjectContactsEdit_,subjectNotesEdit_,
            identitySourceTypeEdit_,identitySourceRefEdit_,identityLeadValueEdit_,
            identityConfidenceEdit_,identityProvenanceEdit_
        };
        for(HWND edit:edits) if(edit) SetWindowTextW(edit,L"");
        if(identityConfidenceEdit_) SetWindowTextW(identityConfidenceEdit_,L"50");
    }

    void LoadSubjectEditors(const sentinel::identity::SubjectRecord& subject) {
        SetWindowTextW(subjectDisplayEdit_,Widen(subject.displayName).c_str());
        SetWindowTextW(subjectLegalEdit_,Widen(subject.legalName).c_str());
        SetWindowTextW(subjectAliasesEdit_,Widen(subject.aliases).c_str());
        SetWindowTextW(subjectUsernamesEdit_,Widen(subject.usernames).c_str());
        SetWindowTextW(subjectContactsEdit_,Widen(subject.contactIdentifiers).c_str());
        SetWindowTextW(subjectNotesEdit_,Widen(subject.notes).c_str());
    }

    std::optional<sentinel::SubjectId> SelectedSubjectId() const {
        if(selectedSubjectId_.empty()) return std::nullopt;
        return sentinel::SubjectId::Parse(selectedSubjectId_);
    }

    std::optional<sentinel::SubjectIdentityId> SelectedIdentityLeadId() const {
        if(selectedIdentityLeadId_.empty()) return std::nullopt;
        return sentinel::SubjectIdentityId::Parse(selectedIdentityLeadId_);
    }

    void NewSubjectDraft() {
        if(cases_.empty()) {
            page_=Page::Cases;
            ApplyPageControls();
            statusText_=L"Create a case before adding a subject";
            return;
        }
        selectedSubjectId_.clear();
        selectedIdentityLeadId_.clear();
        subjectDraftNew_=true;
        ClearSubjectEditors();
        statusText_=L"New subject draft";
        SetFocus(subjectDisplayEdit_);
    }

    void SelectSubjectById(const std::string& id) {
        auto parsed=sentinel::SubjectId::Parse(id);
        if(!parsed) return;
        auto subject=runtime_->subjectIdentity.GetSubject(*parsed);
        if(!subject) return;
        if(cases_.empty() || subject->caseId.ToString()!=cases_[selectedCase_].id.ToString()) return;

        selectedSubjectId_=id;
        selectedIdentityLeadId_.clear();
        subjectDraftNew_=false;
        LoadSubjectEditors(*subject);
        statusText_=L"Subject selected: "+Widen(subject->displayName);
    }

    void SelectIdentityLeadById(const std::string& id) {
        auto parsed=sentinel::SubjectIdentityId::Parse(id);
        if(!parsed) return;
        auto lead=runtime_->subjectIdentity.GetLead(*parsed);
        auto subjectId=SelectedSubjectId();
        if(!lead || !subjectId || lead->subjectId.ToString()!=subjectId->ToString()) return;
        selectedIdentityLeadId_=id;
        statusText_=L"Identity lead selected";
    }

    void SaveSubjectFromEditors() {
        if(cases_.empty()) {
            statusText_=L"Create or select a case first";
            return;
        }

        const auto display=Narrow(EditText(subjectDisplayEdit_));
        if(display.empty()) {
            statusText_=L"Subject display name is required";
            SetFocus(subjectDisplayEdit_);
            return;
        }

        try {
            sentinel::identity::SubjectRecord subject;
            bool created=false;
            if(auto id=SelectedSubjectId()) {
                auto existing=runtime_->subjectIdentity.GetSubject(*id);
                if(!existing) {
                    selectedSubjectId_.clear();
                    subjectDraftNew_=true;
                } else {
                    subject=*existing;
                }
            }

            if(selectedSubjectId_.empty()) {
                subject=runtime_->subjectIdentity.CreateSubject(cases_[selectedCase_].id,display);
                created=true;
            }

            subject.caseId=cases_[selectedCase_].id;
            subject.displayName=display;
            subject.legalName=Narrow(EditText(subjectLegalEdit_));
            subject.aliases=Narrow(EditText(subjectAliasesEdit_));
            subject.usernames=Narrow(EditText(subjectUsernamesEdit_));
            subject.contactIdentifiers=Narrow(EditText(subjectContactsEdit_));
            subject.notes=Narrow(EditText(subjectNotesEdit_));
            runtime_->subjectIdentity.SaveSubject(subject);

            selectedSubjectId_=subject.id.ToString();
            selectedIdentityLeadId_.clear();
            subjectDraftNew_=false;

            const std::string meta=
                "case="+subject.caseId.ToString()+
                " display="+subject.displayName+
                " status="+sentinel::identity::ToString(subject.identityStatus);
            runtime_->audit.Append({
                sentinel::UserId::Random(),
                created?sentinel::AuditAction::SubjectCreated:sentinel::AuditAction::SubjectUpdated,
                "subject",
                subject.id.ToString(),
                AuditMetadata(meta)
            });

            auto stored=runtime_->subjectIdentity.GetSubject(subject.id);
            if(stored) LoadSubjectEditors(*stored);
            statusText_=created?L"Subject created":L"Subject updated";
        } catch(const std::exception& e) {
            statusText_=L"Subject save failed";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Subject Save",MB_OK|MB_ICONERROR);
        }
    }

    void DeleteSelectedSubject() {
        auto id=SelectedSubjectId();
        if(!id) {
            statusText_=L"Select a saved subject first";
            return;
        }
        auto subject=runtime_->subjectIdentity.GetSubject(*id);
        if(!subject) return;

        const std::wstring prompt=L"Delete subject '"+Widen(subject->displayName)+
            L"' and its identity leads from this case?";
        if(MessageBoxW(hwnd_,prompt.c_str(),L"Delete Subject",MB_YESNO|MB_ICONWARNING)!=IDYES)
            return;

        if(runtime_->subjectIdentity.DeleteSubject(*id)) {
            runtime_->audit.Append({
                sentinel::UserId::Random(),
                sentinel::AuditAction::SubjectDeleted,
                "subject",
                id->ToString(),
                AuditMetadata("case="+subject->caseId.ToString()+" display="+subject->displayName)
            });
            selectedSubjectId_.clear();
            selectedIdentityLeadId_.clear();
            subjectDraftNew_=false;
            ClearSubjectEditors();
            statusText_=L"Subject deleted";
        }
    }

    void ConfirmSelectedSubject() {
        auto id=SelectedSubjectId();
        if(!id) {
            statusText_=L"Select a saved subject first";
            return;
        }
        auto subject=runtime_->subjectIdentity.GetSubject(*id);
        if(!subject) return;

        if(subject->identityStatus==sentinel::identity::SubjectIdentityStatus::Confirmed) {
            statusText_=L"Subject identity is already confirmed";
            return;
        }

        if(MessageBoxW(
            hwnd_,
            L"Confirm this subject identity as an investigator-reviewed conclusion?\n\n"
            L"This does not occur automatically from lead confidence.",
            L"Confirm Subject Identity",
            MB_YESNO|MB_ICONQUESTION)!=IDYES) return;

        if(runtime_->subjectIdentity.SetSubjectStatus(
            *id,sentinel::identity::SubjectIdentityStatus::Confirmed)) {
            runtime_->audit.Append({
                sentinel::UserId::Random(),
                sentinel::AuditAction::SubjectIdentityConfirmed,
                "subject",
                id->ToString(),
                AuditMetadata("confirmed_by=local-investigator")
            });
            statusText_=L"Subject identity confirmed by investigator";
        }
    }

    void AddIdentityLeadFromEditors() {
        auto subjectId=SelectedSubjectId();
        if(!subjectId) {
            statusText_=L"Save and select a subject before adding identity leads";
            return;
        }

        const auto leadValue=Narrow(EditText(identityLeadValueEdit_));
        const auto provenance=Narrow(EditText(identityProvenanceEdit_));
        if(leadValue.empty()) {
            statusText_=L"Identity lead / finding is required";
            SetFocus(identityLeadValueEdit_);
            return;
        }
        if(provenance.empty()) {
            statusText_=L"Provenance/source context is required";
            SetFocus(identityProvenanceEdit_);
            return;
        }

        int confidence=_wtoi(EditText(identityConfidenceEdit_).c_str());
        confidence=std::clamp(confidence,0,100);

        try {
            auto lead=runtime_->subjectIdentity.AddLead(
                *subjectId,
                Narrow(EditText(identitySourceTypeEdit_)),
                Narrow(EditText(identitySourceRefEdit_)),
                leadValue,
                confidence,
                provenance);

            selectedIdentityLeadId_=lead.id.ToString();
            runtime_->audit.Append({
                sentinel::UserId::Random(),
                sentinel::AuditAction::IdentityLeadAdded,
                "identity_lead",
                lead.id.ToString(),
                AuditMetadata(
                    "subject="+subjectId->ToString()+
                    " confidence="+std::to_string(confidence)+
                    " source_type="+lead.sourceType)
            });

            SetWindowTextW(identitySourceTypeEdit_,L"");
            SetWindowTextW(identitySourceRefEdit_,L"");
            SetWindowTextW(identityLeadValueEdit_,L"");
            SetWindowTextW(identityConfidenceEdit_,L"50");
            SetWindowTextW(identityProvenanceEdit_,L"");
            statusText_=L"Identity lead added for human review";
        } catch(const std::exception& e) {
            statusText_=L"Identity lead could not be saved";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Identity Lead",MB_OK|MB_ICONERROR);
        }
    }

    void ReviewSelectedIdentityLead(bool verified) {
        auto leadId=SelectedIdentityLeadId();
        if(!leadId) {
            statusText_=L"Select an identity lead first";
            return;
        }

        const auto state=verified
            ? sentinel::identity::IdentityLeadStatus::Verified
            : sentinel::identity::IdentityLeadStatus::Rejected;
        const bool ok=runtime_->subjectIdentity.ReviewLead(
            *leadId,state,"local-investigator",
            verified?"Verified by investigator in SARA":"Rejected by investigator in SARA");
        if(!ok) {
            statusText_=L"Identity lead review failed";
            return;
        }

        runtime_->audit.Append({
            sentinel::UserId::Random(),
            verified?sentinel::AuditAction::IdentityLeadVerified:sentinel::AuditAction::IdentityLeadRejected,
            "identity_lead",
            leadId->ToString(),
            AuditMetadata(verified?"status=VERIFIED":"status=REJECTED")
        });
        statusText_=verified?L"Identity lead verified":L"Identity lead rejected";
    }

    void RefreshIdentityResearchProviderList(const std::string& selectId={}) {
        if(!researchProviderCombo_) return;
        researchProviders_=runtime_->subjectIdentity.ListResearchProviders(false);
        SendMessageW(researchProviderCombo_,CB_RESETCONTENT,0,0);

        int selected=-1;
        for(size_t i=0;i<researchProviders_.size();++i) {
            const auto& provider=researchProviders_[i];
            std::wstring label=Widen(provider.displayName+" ["+provider.id+"]");
            SendMessageW(researchProviderCombo_,CB_ADDSTRING,0,(LPARAM)label.c_str());
            if((!selectId.empty() && provider.id==selectId) ||
               (selectId.empty() && provider.id=="manual/authorized"))
                selected=(int)i;
        }
        if(selected>=0)
            SendMessageW(researchProviderCombo_,CB_SETCURSEL,(WPARAM)selected,0);
        else if(!selectId.empty())
            SetWindowTextW(researchProviderCombo_,Widen(selectId).c_str());
    }

    std::string SelectedIdentityResearchProvider() const {
        const int index=(int)SendMessageW(researchProviderCombo_,CB_GETCURSEL,0,0);
        if(index>=0 && index<(int)researchProviders_.size())
            return researchProviders_[(size_t)index].id;

        auto typed=Narrow(EditText(researchProviderCombo_));
        if(typed.empty()) typed="manual/authorized";
        return typed;
    }

    void ClearResearchProviderConfigEditors() {
        selectedResearchProviderConfigId_.clear();
        selectedResearchProviderTypesMask_=
            1u<<static_cast<unsigned int>(SelectedResearchType());
        SetWindowTextW(researchProviderIdEdit_,L"");
        SetWindowTextW(researchProviderNameEdit_,L"");
        SendMessageW(researchProviderModeCombo_,CB_SETCURSEL,0,0);
        SetWindowTextW(researchProviderEndpointEdit_,L"");
        SetWindowTextW(researchProviderCredentialEdit_,L"");
        SetWindowTextW(researchProviderNotesEdit_,L"");
    }

    void OpenResearchProviderManager() {
        researchProviderManagerOpen_=true;
        ClearResearchProviderConfigEditors();
        ApplyPageControls();
        statusText_=L"Identity Research provider registry opened";
    }

    void CloseResearchProviderManager() {
        researchProviderManagerOpen_=false;
        RefreshIdentityResearchProviderList();
        ApplyPageControls();
        statusText_=L"Returned to Identity Research";
    }

    void NewResearchProviderConfig() {
        ClearResearchProviderConfigEditors();
        statusText_=L"New provider configuration";
    }

    void SelectResearchProviderConfig(const std::string& id) {
        auto provider=runtime_->subjectIdentity.GetResearchProvider(id);
        if(!provider) {
            statusText_=L"Research provider could not be loaded";
            return;
        }
        selectedResearchProviderConfigId_=provider->id;
        selectedResearchProviderTypesMask_=provider->supportedTypesMask;
        SetWindowTextW(researchProviderIdEdit_,Widen(provider->id).c_str());
        SetWindowTextW(researchProviderNameEdit_,Widen(provider->displayName).c_str());
        SendMessageW(researchProviderModeCombo_,CB_SETCURSEL,(WPARAM)(int)provider->accessMode,0);
        SetWindowTextW(researchProviderEndpointEdit_,Widen(provider->endpointHint).c_str());
        SetWindowTextW(researchProviderCredentialEdit_,Widen(provider->credentialReference).c_str());
        SetWindowTextW(researchProviderNotesEdit_,Widen(provider->notes).c_str());
        statusText_=L"Research provider selected";
    }

    void ToggleResearchProviderType(int index) {
        if(index<0 || index>4) return;
        const unsigned int bit=1u<<static_cast<unsigned int>(index);
        const unsigned int next=selectedResearchProviderTypesMask_^bit;
        if(next==0) {
            statusText_=L"A provider must support at least one research type";
            return;
        }
        selectedResearchProviderTypesMask_=next;
        statusText_=L"Provider capabilities changed; Save to persist";
    }

    void SaveResearchProviderConfig() {
        sentinel::identity::IdentityResearchProvider provider;
        provider.id=Narrow(EditText(researchProviderIdEdit_));
        provider.displayName=Narrow(EditText(researchProviderNameEdit_));
        provider.accessMode=(sentinel::identity::IdentityResearchAccessMode)std::clamp(
            (int)SendMessageW(researchProviderModeCombo_,CB_GETCURSEL,0,0),0,2);
        provider.supportedTypesMask=selectedResearchProviderTypesMask_;
        provider.endpointHint=Narrow(EditText(researchProviderEndpointEdit_));
        provider.credentialReference=Narrow(EditText(researchProviderCredentialEdit_));
        provider.notes=Narrow(EditText(researchProviderNotesEdit_));

        if(provider.id.empty() || provider.displayName.empty()) {
            statusText_=L"Provider ID and display name are required";
            return;
        }
        if(provider.credentialReference.size()>180) {
            statusText_=L"Credential reference is too long; store only an alias or vault-key reference";
            return;
        }

        auto existing=runtime_->subjectIdentity.GetResearchProvider(provider.id);
        provider.enabled=existing?existing->enabled:
            provider.accessMode==sentinel::identity::IdentityResearchAccessMode::Manual;

        try {
            auto saved=runtime_->subjectIdentity.SaveResearchProvider(std::move(provider));
            selectedResearchProviderConfigId_=saved.id;
            selectedResearchProviderTypesMask_=saved.supportedTypesMask;
            RefreshIdentityResearchProviderList();
            statusText_=saved.enabled
                ? L"Research provider saved and enabled"
                : L"Research provider saved disabled; review configuration before enabling";
        } catch(const std::exception& e) {
            statusText_=L"Research provider could not be saved";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Research Provider",MB_OK|MB_ICONERROR);
        }
    }

    void ToggleSelectedResearchProviderConfig() {
        if(selectedResearchProviderConfigId_.empty()) {
            statusText_=L"Select a registered provider first";
            return;
        }
        if(selectedResearchProviderConfigId_=="manual/authorized") {
            statusText_=L"The built-in Manual / Authorized provider remains enabled";
            return;
        }
        auto provider=runtime_->subjectIdentity.GetResearchProvider(selectedResearchProviderConfigId_);
        if(!provider) return;

        if(!provider->enabled &&
           provider->accessMode!=sentinel::identity::IdentityResearchAccessMode::Manual &&
           (provider->endpointHint.empty() || provider->credentialReference.empty()))
        {
            statusText_=L"Portal/API providers need an endpoint hint and credential reference before enabling";
            return;
        }

        if(runtime_->subjectIdentity.SetResearchProviderEnabled(provider->id,!provider->enabled)) {
            RefreshIdentityResearchProviderList();
            SelectResearchProviderConfig(provider->id);
            statusText_=provider->enabled?L"Research provider disabled":L"Research provider enabled";
        }
    }

    sentinel::identity::IdentityResearchType SelectedResearchType() const {
        const int selected=(int)SendMessageW(researchTypeCombo_,CB_GETCURSEL,0,0);
        switch(selected) {
            case 1: return sentinel::identity::IdentityResearchType::SocialProfile;
            case 2: return sentinel::identity::IdentityResearchType::Username;
            case 3: return sentinel::identity::IdentityResearchType::Contact;
            case 4: return sentinel::identity::IdentityResearchType::ImageReference;
            default: return sentinel::identity::IdentityResearchType::PublicRecords;
        }
    }

    void ClearIdentityResearchResultEditors() {
        SetWindowTextW(researchResultEdit_,L"");
        SetWindowTextW(researchReferenceEdit_,L"");
        SetWindowTextW(researchProvenanceEdit_,L"");
        SetWindowTextW(researchConfidenceEdit_,L"50");
    }

    void OpenIdentityResearch() {
        if(!SelectedSubjectId()) {
            statusText_=L"Save and select a subject before opening Identity Research";
            return;
        }
        selectedResearchTaskId_.clear();
        ClearIdentityResearchResultEditors();
        RefreshIdentityResearchProviderList();
        page_=Page::IdentityResearch;
        ApplyPageControls();
        statusText_=L"Identity Research workspace opened";
    }

    void SelectIdentityResearchTask(const std::string& id) {
        selectedResearchTaskId_=id;
        auto task=runtime_->subjectIdentity.GetResearch(id);
        if(!task) {
            statusText_=L"Identity research task could not be loaded";
            return;
        }
        RefreshIdentityResearchProviderList(task->provider);
        SetWindowTextW(researchQueryEdit_,Widen(task->queryText).c_str());
        SetWindowTextW(researchPurposeEdit_,Widen(task->purpose).c_str());
        SendMessageW(researchTypeCombo_,CB_SETCURSEL,(WPARAM)(int)task->type,0);
        SetWindowTextW(researchResultEdit_,Widen(task->resultSummary).c_str());
        SetWindowTextW(researchReferenceEdit_,Widen(task->resultReference).c_str());
        SetWindowTextW(researchProvenanceEdit_,Widen(task->provenance).c_str());
        statusText_=L"Identity research task selected";
    }

    void QueueIdentityResearchFromEditors() {
        auto subjectId=SelectedSubjectId();
        if(!subjectId) {
            statusText_=L"Select a subject before queueing identity research";
            return;
        }
        const auto query=Narrow(EditText(researchQueryEdit_));
        const auto purpose=Narrow(EditText(researchPurposeEdit_));
        if(query.empty()) {
            statusText_=L"Research query/reference is required";
            SetFocus(researchQueryEdit_);
            return;
        }
        if(purpose.empty()) {
            statusText_=L"Case purpose / legal-basis note is required";
            SetFocus(researchPurposeEdit_);
            return;
        }

        try {
            auto task=runtime_->subjectIdentity.QueueResearch(
                *subjectId,
                SelectedResearchType(),
                SelectedIdentityResearchProvider(),
                query,
                purpose);
            selectedResearchTaskId_=task.id;
            runtime_->audit.Append({
                sentinel::UserId::Random(),
                sentinel::AuditAction::IdentityResearchQueued,
                "identity_research",
                task.id,
                AuditMetadata(
                    "subject="+subjectId->ToString()+
                    " type="+sentinel::identity::ToString(task.type)+
                    " provider="+task.provider)
            });
            ClearIdentityResearchResultEditors();
            statusText_=L"Identity research task queued for investigator work";
        } catch(const std::exception& e) {
            statusText_=L"Identity research task could not be queued";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Identity Research",MB_OK|MB_ICONERROR);
        }
    }

    void CompleteSelectedIdentityResearch() {
        if(selectedResearchTaskId_.empty()) {
            statusText_=L"Select a research task first";
            return;
        }
        const auto result=Narrow(EditText(researchResultEdit_));
        const auto provenance=Narrow(EditText(researchProvenanceEdit_));
        if(result.empty()) {
            statusText_=L"Record the research finding before completing the task";
            SetFocus(researchResultEdit_);
            return;
        }
        if(provenance.empty()) {
            statusText_=L"Research-result provenance is required";
            SetFocus(researchProvenanceEdit_);
            return;
        }

        if(!runtime_->subjectIdentity.CompleteResearch(
                selectedResearchTaskId_,
                result,
                Narrow(EditText(researchReferenceEdit_)),
                provenance))
        {
            statusText_=L"Research task must be queued before it can be completed";
            return;
        }
        runtime_->audit.Append({
            sentinel::UserId::Random(),
            sentinel::AuditAction::IdentityResearchCompleted,
            "identity_research",
            selectedResearchTaskId_,
            AuditMetadata("result recorded; human review required")
        });
        statusText_=L"Research result recorded; it is not yet an identity lead";
    }

    void PromoteSelectedIdentityResearch() {
        if(selectedResearchTaskId_.empty()) {
            statusText_=L"Select a completed research task first";
            return;
        }
        int confidence=_wtoi(EditText(researchConfidenceEdit_).c_str());
        confidence=std::clamp(confidence,0,100);
        try {
            auto lead=runtime_->subjectIdentity.PromoteResearchToLead(
                selectedResearchTaskId_,confidence,"local-investigator",
                "Promoted from Identity Research for separate lead verification");
            selectedIdentityLeadId_=lead.id.ToString();
            runtime_->audit.Append({
                sentinel::UserId::Random(),
                sentinel::AuditAction::IdentityResearchPromoted,
                "identity_research",
                selectedResearchTaskId_,
                AuditMetadata(
                    "lead="+lead.id.ToString()+
                    " confidence="+std::to_string(confidence)+
                    " status=UNVERIFIED_LEAD")
            });
            statusText_=L"Research promoted to an unverified identity lead";
        } catch(const std::exception& e) {
            statusText_=L"Research result could not be promoted";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Identity Research",MB_OK|MB_ICONERROR);
        }
    }

    void RejectSelectedIdentityResearch() {
        if(selectedResearchTaskId_.empty()) {
            statusText_=L"Select a research task first";
            return;
        }
        if(!runtime_->subjectIdentity.RejectResearch(
                selectedResearchTaskId_,"local-investigator",
                "Rejected in SARA Identity Research"))
        {
            statusText_=L"Research task could not be rejected";
            return;
        }
        runtime_->audit.Append({
            sentinel::UserId::Random(),
            sentinel::AuditAction::IdentityResearchRejected,
            "identity_research",
            selectedResearchTaskId_,
            AuditMetadata("status=REJECTED")
        });
        statusText_=L"Identity research task rejected";
    }

    void DrawSubjects(float w,float h) {
        PageTitle(
            L"Subjects & Identity",
            L"Case-scoped subject profiles, provenance-backed identity leads, and investigator confirmation");

        const float x=kSidebar+28.0f;
        const float y=kHeader+94.0f;
        const float contentW=w-x-28.0f;
        const float gap=14.0f;

        if(cases_.empty()) {
            Rounded(x,y,contentW,184,brush_.panel.Get(),brush_.border.Get(),10);
            DrawIcon(IconKind::Folder,x+22,y+28,34,brush_.cyan.Get());
            TextLine(L"Create or select a case before adding subjects.",x+72,y+22,contentW-94,34,h1Fmt_.Get(),brush_.text.Get());
            Text(
                L"Subjects, identity leads, provenance, and confirmation are always scoped to an investigation.",
                x+72,y+62,contentW-112,48,smallFmt_.Get(),brush_.muted.Get());
            AddButton(L"subjects_create_case",L"Open Cases",x+72,y+126,132,36,true);
            return;
        }

        auto subjects=runtime_->subjectIdentity.ListForCase(cases_[selectedCase_].id,100);
        const bool selectedExists=std::any_of(subjects.begin(),subjects.end(),[&](const auto& subject){
            return subject.id.ToString()==selectedSubjectId_;
        });
        if(!subjectDraftNew_ && !selectedExists) {
            selectedSubjectId_.clear();
            selectedIdentityLeadId_.clear();
            if(!subjects.empty()) {
                selectedSubjectId_=subjects.front().id.ToString();
                LoadSubjectEditors(subjects.front());
            } else {
                ClearSubjectEditors();
            }
        }

        std::optional<sentinel::identity::SubjectRecord> selectedSubject;
        if(auto id=SelectedSubjectId()) selectedSubject=runtime_->subjectIdentity.GetSubject(*id);

        std::vector<sentinel::identity::IdentityLead> leads;
        if(selectedSubject) {
            leads=runtime_->subjectIdentity.ListLeads(selectedSubject->id,100);
            const bool leadExists=std::any_of(leads.begin(),leads.end(),[&](const auto& lead){
                return lead.id.ToString()==selectedIdentityLeadId_;
            });
            if(!leadExists) {
                selectedIdentityLeadId_=leads.empty()?"":leads.front().id.ToString();
            }
        } else {
            selectedIdentityLeadId_.clear();
        }

        const auto counts=runtime_->subjectIdentity.CountsForCase(cases_[selectedCase_].id);
        const float metricsH=78.0f;
        const float cardW=(contentW-gap*3.0f)/4.0f;

        struct MetricData {
            const wchar_t* label;
            std::wstring value;
            const wchar_t* sub;
            ID2D1Brush* accent;
            IconKind icon;
        };
        MetricData metrics[]={
            {L"Case",Widen(cases_[selectedCase_].caseNumber),L"Current investigation",brush_.cyan.Get(),IconKind::Folder},
            {L"Subjects",std::to_wstring(counts.subjects),L"Case-scoped profiles",brush_.blue.Get(),IconKind::Document},
            {L"Identity Leads",std::to_wstring(counts.leads),L"Human review required",brush_.yellow.Get(),IconKind::Search},
            {L"Confirmed",std::to_wstring(counts.confirmedSubjects),L"Investigator confirmed",brush_.green.Get(),IconKind::Check}
        };

        for(int i=0;i<4;i++) {
            const float cx=x+i*(cardW+gap);
            Rounded(cx,y,cardW,metricsH,brush_.panel.Get(),brush_.border.Get(),9);
            Rounded(cx+12,y+16,34,34,brush_.panel2.Get(),nullptr,8);
            DrawIcon(metrics[i].icon,cx+19,y+23,20,metrics[i].accent);
            TextLine(metrics[i].label,cx+56,y+9,cardW-66,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(metrics[i].value,cx+56,y+27,cardW-66,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(metrics[i].sub,cx+14,y+57,cardW-28,16,tinyFmt_.Get(),metrics[i].accent);
        }

        const float bodyY=y+metricsH+12.0f;
        const float bodyH=std::max(430.0f,h-bodyY-24.0f);
        const float leftW=std::clamp(contentW*0.34f,248.0f,286.0f);
        const float rightX=x+leftW+gap;
        const float rightW=contentW-leftW-gap;
        const float detailH=254.0f;
        const float leadsY=bodyY+detailH+gap;
        const float leadsH=bodyH-detailH-gap;
        const float leadComposerY=bodyY+bodyH-214.0f;

        // Subject library + lead capture composer.
        Rounded(x,bodyY,leftW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Case Subjects",x+16,bodyY+12,leftW-130,28,h1Fmt_.Get(),brush_.text.Get());
        AddButton(L"subject_new",L"+ New",x+leftW-92,bodyY+12,76,28,true);

        float sy=bodyY+52.0f;
        if(subjects.empty()) {
            Text(
                L"No subjects recorded for this case yet. Choose + New, enter a display name, and save the subject.",
                x+18,sy,leftW-36,70,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(const auto& subject:subjects) {
                if(sy+42.0f>leadComposerY-12.0f) break;
                const bool selected=subject.id.ToString()==selectedSubjectId_;
                Rounded(x+12,sy,leftW-24,42,
                    selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),7);

                TextLine(Widen(subject.displayName),x+22,sy+4,leftW-112,19,smallFmt_.Get(),brush_.text.Get());
                TextLine(Widen(sentinel::identity::ToString(subject.identityStatus)),
                    x+leftW-96,sy+4,72,18,tinyFmt_.Get(),
                    subject.identityStatus==sentinel::identity::SubjectIdentityStatus::Confirmed
                        ?brush_.green.Get():brush_.yellow.Get(),
                    DWRITE_TEXT_ALIGNMENT_TRAILING);

                std::wstring second=Widen(subject.usernames.empty()?subject.aliases:subject.usernames);
                if(second.empty()) second=L"No aliases/usernames";
                if(second.size()>34) second=second.substr(0,31)+L"...";
                TextLine(second,x+22,sy+23,leftW-44,16,tinyFmt_.Get(),brush_.muted.Get());
                buttons_.push_back({{x+12,sy,x+leftW-12,sy+42},L"subject_row:"+Widen(subject.id.ToString())});
                sy+=48.0f;
            }
        }

        target_->DrawLine(
            D2D1::Point2F(x+14,leadComposerY-8),
            D2D1::Point2F(x+leftW-14,leadComposerY-8),
            brush_.border.Get(),1.0f);
        TextLine(L"Add Identity Lead",x+16,leadComposerY,180,24,smallFmt_.Get(),brush_.cyan.Get());

        TextLine(L"Source",x+16,leadComposerY+36,68,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Reference",x+16,leadComposerY+66,68,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Finding",x+16,leadComposerY+96,68,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Confidence",x+16,leadComposerY+126,68,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Provenance",x+16,leadComposerY+156,68,18,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"identity_add_lead",L"Add Lead",x+16,leadComposerY+184,leftW-32,26,false);

        // Subject editor.
        Rounded(rightX,bodyY,rightW,detailH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(
            subjectDraftNew_?L"New Subject":L"Subject Details",
            rightX+16,bodyY+12,180,28,h1Fmt_.Get(),brush_.text.Get());

        if(selectedSubject) {
            Badge(
                Widen(sentinel::identity::ToString(selectedSubject->identityStatus)),
                rightX+190,bodyY+15,
                selectedSubject->identityStatus==sentinel::identity::SubjectIdentityStatus::Confirmed
                    ?brush_.green.Get():brush_.yellow.Get(),
                92);
        }

        AddButton(L"identity_research_open",L"Research",rightX+rightW-370,bodyY+12,84,28,false);
        if(selectedSubject &&
           selectedSubject->identityStatus!=sentinel::identity::SubjectIdentityStatus::Confirmed) {
            AddButton(L"subject_confirm",L"Confirm",rightX+rightW-276,bodyY+12,78,28,false);
        }
        AddButton(L"subject_save",L"Save",rightX+rightW-190,bodyY+12,58,28,true);
        AddButton(L"subject_delete",L"Delete",rightX+rightW-124,bodyY+12,58,28,false);

        const float halfGap=10.0f;
        const float halfW=(rightW-32.0f-halfGap)/2.0f;
        const float fieldLeft=rightX+16.0f;
        const float fieldRight=fieldLeft+halfW+halfGap;

        TextLine(L"Display name / handle",fieldLeft,bodyY+44,halfW,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Legal name (if verified)",fieldRight,bodyY+44,halfW,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Aliases",fieldLeft,bodyY+92,halfW,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Usernames",fieldRight,bodyY+92,halfW,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Contact identifiers",fieldLeft,bodyY+140,rightW-32,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Investigator notes",fieldLeft,bodyY+184,rightW-32,16,tinyFmt_.Get(),brush_.muted.Get());

        // Identity lead review.
        Rounded(rightX,leadsY,rightW,leadsH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Identity Leads",rightX+16,leadsY+10,180,26,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"LEADS ARE NOT IDENTITY CONCLUSIONS",rightX+rightW-230,leadsY+12,214,20,tinyFmt_.Get(),brush_.yellow.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);

        TextLine(L"SOURCE",rightX+16,leadsY+43,88,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"FINDING",rightX+112,leadsY+43,rightW-310,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"CONF.",rightX+rightW-184,leadsY+43,52,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STATUS",rightX+rightW-122,leadsY+43,106,16,tinyFmt_.Get(),brush_.muted.Get());

        const float actionsY=leadsY+leadsH-38.0f;
        float ly=leadsY+62.0f;
        if(leads.empty()) {
            TextLine(L"No identity leads recorded for the selected subject.",
                rightX+18,ly,rightW-36,28,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(const auto& lead:leads) {
                if(ly+40.0f>actionsY-8.0f) break;
                const bool selected=lead.id.ToString()==selectedIdentityLeadId_;
                ID2D1Brush* statusBrush=
                    lead.status==sentinel::identity::IdentityLeadStatus::Verified?brush_.green.Get():
                    lead.status==sentinel::identity::IdentityLeadStatus::Rejected?brush_.red.Get():
                    brush_.yellow.Get();

                Rounded(rightX+12,ly,rightW-24,38,
                    selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),6);

                std::wstring source=Widen(lead.sourceType.empty()?"source":lead.sourceType);
                if(source.size()>14) source=source.substr(0,11)+L"...";
                std::wstring value=Widen(lead.leadValue);
                if(value.size()>36) value=value.substr(0,33)+L"...";

                TextLine(source,rightX+20,ly+4,84,18,tinyFmt_.Get(),brush_.text.Get());
                TextLine(value,rightX+112,ly+4,rightW-310,18,tinyFmt_.Get(),brush_.text.Get());
                TextLine(std::to_wstring(lead.confidence)+L"%",
                    rightX+rightW-184,ly+4,52,18,tinyFmt_.Get(),brush_.cyan.Get());
                TextLine(Widen(sentinel::identity::ToString(lead.status)),
                    rightX+rightW-122,ly+4,100,18,tinyFmt_.Get(),statusBrush,DWRITE_TEXT_ALIGNMENT_TRAILING);

                std::wstring provenance=Widen(lead.provenance);
                if(provenance.size()>70) provenance=provenance.substr(0,67)+L"...";
                TextLine(provenance,rightX+20,ly+21,rightW-42,14,tinyFmt_.Get(),brush_.muted.Get());

                buttons_.push_back({{rightX+12,ly,rightX+rightW-12,ly+38},L"identity_lead:"+Widen(lead.id.ToString())});
                ly+=44.0f;
            }
        }

        if(!selectedIdentityLeadId_.empty()) {
            AddButton(L"identity_verify",L"Verify Lead",rightX+16,actionsY,94,28,true);
            AddButton(L"identity_reject",L"Reject",rightX+118,actionsY,72,28,false);
            TextLine(L"Verification is a human review action; it does not automatically confirm the subject.",
                rightX+202,actionsY+3,rightW-218,22,tinyFmt_.Get(),brush_.muted.Get());
        }
    }

    void DrawIdentityResearchProviders(float w,float h) {
        PageTitle(
            L"Subjects & Identity / Research Providers",
            L"Authorized provider metadata only; credential aliases are references, never secrets");

        const float x=kSidebar+28.0f;
        const float y=kHeader+94.0f;
        const float contentW=w-x-28.0f;
        const float gap=14.0f;
        const float leftW=std::clamp(contentW*0.38f,300.0f,360.0f);
        const float rightX=x+leftW+gap;
        const float rightW=contentW-leftW-gap;
        const float bodyH=std::max(500.0f,h-y-24.0f);

        auto providers=runtime_->subjectIdentity.ListResearchProviders(true);
        if(!selectedResearchProviderConfigId_.empty()) {
            const bool exists=std::any_of(providers.begin(),providers.end(),[&](const auto& p){
                return p.id==selectedResearchProviderConfigId_;
            });
            if(!exists) selectedResearchProviderConfigId_.clear();
        }

        Rounded(x,y,leftW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Provider Registry",x+16,y+12,leftW-122,28,h1Fmt_.Get(),brush_.text.Get());
        AddButton(L"research_provider_back",L"Back",x+leftW-100,y+12,44,28,false);
        AddButton(L"research_provider_new",L"New",x+leftW-50,y+12,34,28,true);
        TextLine(L"DISABLED BY DEFAULT UNLESS MANUAL",x+16,y+43,leftW-32,18,tinyFmt_.Get(),brush_.yellow.Get());

        float rowY=y+68.0f;
        if(providers.empty()) {
            TextLine(L"No provider records.",x+18,rowY,leftW-36,26,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(const auto& provider:providers) {
                if(rowY+58>y+bodyH-12) break;
                const bool selected=provider.id==selectedResearchProviderConfigId_;
                Rounded(x+12,rowY,leftW-24,54,
                    selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),7);
                TextLine(Widen(provider.displayName),x+22,rowY+5,leftW-122,18,tinyFmt_.Get(),brush_.text.Get());
                TextLine(provider.enabled?L"ENABLED":L"DISABLED",
                    x+leftW-108,rowY+5,84,18,tinyFmt_.Get(),
                    provider.enabled?brush_.green.Get():brush_.muted.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);
                TextLine(Widen(provider.id),x+22,rowY+24,leftW-44,15,tinyFmt_.Get(),brush_.cyan.Get());
                TextLine(
                    Widen(sentinel::identity::ToString(provider.accessMode))+
                    L" | mask "+std::to_wstring(provider.supportedTypesMask),
                    x+22,rowY+39,leftW-44,14,tinyFmt_.Get(),brush_.muted.Get());
                buttons_.push_back({{x+12,rowY,x+leftW-12,rowY+54},L"research_provider_row:"+Widen(provider.id)});
                rowY+=60.0f;
            }
        }

        Rounded(rightX,y,rightW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Provider Configuration",rightX+16,y+12,rightW-32,28,h1Fmt_.Get(),brush_.text.Get());
        Text(
            L"Store connector metadata and a credential alias/vault reference only. Never paste API keys, passwords, cookies, tokens, or session credentials here.",
            rightX+16,y+42,rightW-32,42,tinyFmt_.Get(),brush_.yellow.Get());

        TextLine(L"Provider ID",rightX+16,y+90,90,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Display name",rightX+rightW*0.50f,y+90,92,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Access mode",rightX+16,y+136,90,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Endpoint / portal hint",rightX+rightW*0.50f,y+136,130,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Credential alias / vault reference",rightX+16,y+182,rightW-32,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Supported research types",rightX+16,y+226,rightW-32,16,tinyFmt_.Get(),brush_.muted.Get());

        const wchar_t* typeLabels[]={L"Records",L"Social",L"Username",L"Contact",L"Image"};
        const float typeGap=6.0f;
        const float typeW=(rightW-32.0f-typeGap*4.0f)/5.0f;
        for(int i=0;i<5;i++) {
            const bool on=(selectedResearchProviderTypesMask_&(1u<<i))!=0;
            AddButton(
                L"research_provider_type:"+std::to_wstring(i),
                typeLabels[i],
                rightX+16+i*(typeW+typeGap),y+246,typeW,28,on);
        }

        TextLine(L"Configuration notes",rightX+16,y+286,rightW-32,16,tinyFmt_.Get(),brush_.muted.Get());

        const float actionY=y+bodyH-42.0f;
        AddButton(L"research_provider_save",L"Save Provider",rightX+16,actionY,104,30,true);
        if(!selectedResearchProviderConfigId_.empty()) {
            auto selected=runtime_->subjectIdentity.GetResearchProvider(selectedResearchProviderConfigId_);
            if(selected && selected->id!="manual/authorized")
                AddButton(L"research_provider_toggle",selected->enabled?L"Disable":L"Enable",rightX+128,actionY,78,30,false);
        }
        TextLine(
            L"Provider records do not execute lookups; a future adapter must separately prove configuration and authorization.",
            rightX+216,actionY+4,rightW-232,20,tinyFmt_.Get(),brush_.muted.Get());
    }

    void DrawIdentityResearch(float w,float h) {
        if(researchProviderManagerOpen_) {
            DrawIdentityResearchProviders(w,h);
            return;
        }

        PageTitle(
            L"Subjects & Identity / Research",
            L"Authorized public-source research queue; findings remain leads until separately verified");

        const float x=kSidebar+28.0f;
        const float y=kHeader+94.0f;
        const float contentW=w-x-28.0f;
        const float gap=14.0f;

        auto subjectId=SelectedSubjectId();
        if(!subjectId) {
            Rounded(x,y,contentW,190,brush_.panel.Get(),brush_.border.Get(),10);
            DrawIcon(IconKind::Search,x+22,y+28,34,brush_.cyan.Get());
            TextLine(L"Select a subject before starting identity research.",x+72,y+22,contentW-94,34,h1Fmt_.Get(),brush_.text.Get());
            Text(
                L"Research is always tied to a case-scoped subject and produces reviewable leads, never automatic identity confirmation.",
                x+72,y+62,contentW-112,54,smallFmt_.Get(),brush_.muted.Get());
            AddButton(L"identity_research_back",L"Back to Subjects",x+72,y+132,142,36,true);
            return;
        }

        auto subject=runtime_->subjectIdentity.GetSubject(*subjectId);
        if(!subject) {
            selectedSubjectId_.clear();
            statusText_=L"Selected subject no longer exists";
            AddButton(L"identity_research_back",L"Back to Subjects",x,y,142,36,true);
            return;
        }

        auto tasks=runtime_->subjectIdentity.ListResearch(*subjectId,100);
        const bool selectedExists=std::any_of(tasks.begin(),tasks.end(),[&](const auto& task){
            return task.id==selectedResearchTaskId_;
        });
        if(!selectedExists) {
            selectedResearchTaskId_=tasks.empty()?"":tasks.front().id;
        }

        std::optional<sentinel::identity::IdentityResearchTask> selectedTask;
        if(!selectedResearchTaskId_.empty())
            selectedTask=runtime_->subjectIdentity.GetResearch(selectedResearchTaskId_);

        int completed=0,promoted=0,rejected=0;
        for(const auto& task:tasks) {
            if(task.status==sentinel::identity::IdentityResearchStatus::Completed) ++completed;
            else if(task.status==sentinel::identity::IdentityResearchStatus::PromotedToLead) ++promoted;
            else if(task.status==sentinel::identity::IdentityResearchStatus::Rejected) ++rejected;
        }

        const float cardW=(contentW-gap*3.0f)/4.0f;
        struct ResearchMetric {
            const wchar_t* label;
            std::wstring value;
            const wchar_t* sub;
            ID2D1Brush* accent;
        };
        ResearchMetric metrics[]={
            {L"Subject",Widen(subject->displayName),L"Case-scoped research",brush_.cyan.Get()},
            {L"Tasks",std::to_wstring(tasks.size()),L"Queued + reviewed",brush_.blue.Get()},
            {L"Completed",std::to_wstring(completed),L"Result recorded",brush_.green.Get()},
            {L"Promoted",std::to_wstring(promoted),L"Still unverified leads",brush_.yellow.Get()}
        };
        for(int i=0;i<4;i++) {
            const float cx=x+i*(cardW+gap);
            Rounded(cx,y,cardW,78,brush_.panel.Get(),brush_.border.Get(),9);
            TextLine(metrics[i].label,cx+16,y+9,cardW-32,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(metrics[i].value,cx+16,y+28,cardW-32,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(metrics[i].sub,cx+16,y+57,cardW-32,16,tinyFmt_.Get(),metrics[i].accent);
        }

        const float bodyY=y+90.0f;
        const float bodyH=std::max(430.0f,h-bodyY-24.0f);
        const float leftW=std::clamp(contentW*0.35f,270.0f,330.0f);
        const float rightX=x+leftW+gap;
        const float rightW=contentW-leftW-gap;
        const float requestH=204.0f;
        const float resultY=bodyY+requestH+12.0f;
        const float resultH=bodyH-requestH-12.0f;

        Rounded(x,bodyY,leftW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Research Tasks",x+16,bodyY+12,leftW-104,28,h1Fmt_.Get(),brush_.text.Get());
        AddButton(L"identity_research_back",L"Back",x+leftW-76,bodyY+12,60,28,false);
        TextLine(
            L"NO AUTOMATIC IDENTITY CONCLUSIONS",
            x+16,bodyY+43,leftW-32,18,tinyFmt_.Get(),brush_.yellow.Get());

        float rowY=bodyY+68.0f;
        if(tasks.empty()) {
            Text(
                L"No research tasks yet. Create one on the right for an authorized public-record, social, username, contact, or image-reference check.",
                x+18,rowY,leftW-36,86,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(const auto& task:tasks) {
                if(rowY+58>bodyY+bodyH-12) break;
                const bool selected=task.id==selectedResearchTaskId_;
                ID2D1Brush* stateBrush=
                    task.status==sentinel::identity::IdentityResearchStatus::Completed?brush_.green.Get():
                    task.status==sentinel::identity::IdentityResearchStatus::PromotedToLead?brush_.cyan.Get():
                    task.status==sentinel::identity::IdentityResearchStatus::Rejected?brush_.red.Get():
                    brush_.yellow.Get();

                Rounded(x+12,rowY,leftW-24,54,
                    selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),7);
                TextLine(
                    Widen(sentinel::identity::ToString(task.type)),
                    x+22,rowY+5,leftW-112,18,tinyFmt_.Get(),brush_.text.Get());
                TextLine(
                    Widen(sentinel::identity::ToString(task.status)),
                    x+leftW-104,rowY+5,80,18,tinyFmt_.Get(),stateBrush,DWRITE_TEXT_ALIGNMENT_TRAILING);
                std::wstring query=Widen(task.queryText);
                if(query.size()>38) query=query.substr(0,35)+L"...";
                TextLine(query,x+22,rowY+24,leftW-44,16,tinyFmt_.Get(),brush_.muted.Get());
                std::wstring provider=Widen(task.provider);
                if(provider.size()>30) provider=provider.substr(0,27)+L"...";
                TextLine(provider,x+22,rowY+39,leftW-44,14,tinyFmt_.Get(),brush_.cyan.Get());
                buttons_.push_back({{x+12,rowY,x+leftW-12,rowY+54},L"research_task:"+Widen(task.id)});
                rowY+=60.0f;
            }
        }

        Rounded(rightX,bodyY,rightW,requestH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Research Request",rightX+16,bodyY+12,210,28,h1Fmt_.Get(),brush_.text.Get());
        AddButton(L"research_provider_manager",L"Providers",rightX+rightW-88,bodyY+12,72,26,false);
        TextLine(L"Type",rightX+16,bodyY+43,80,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Authorized provider/source",rightX+rightW*0.50f+5,bodyY+43,rightW*0.50f-21,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Query / reference",rightX+16,bodyY+87,rightW-32,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Case purpose / legal-basis note",rightX+16,bodyY+131,rightW-32,16,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"research_queue",L"Queue Research",rightX+16,bodyY+172,126,26,true);
        TextLine(
            L"Provider metadata only; no secrets stored. Automatic execution remains disabled.",
            rightX+154,bodyY+175,rightW-170,20,tinyFmt_.Get(),brush_.muted.Get());

        Rounded(rightX,resultY,rightW,resultH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Research Result",rightX+16,resultY+10,190,28,h1Fmt_.Get(),brush_.text.Get());
        if(selectedTask) {
            Badge(
                Widen(sentinel::identity::ToString(selectedTask->status)),
                rightX+rightW-118,resultY+12,
                selectedTask->status==sentinel::identity::IdentityResearchStatus::Rejected
                    ?brush_.red.Get():
                 selectedTask->status==sentinel::identity::IdentityResearchStatus::Queued
                    ?brush_.yellow.Get():brush_.green.Get(),
                102);
        }

        TextLine(L"Finding / result summary",rightX+16,resultY+40,rightW-32,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Result reference",rightX+16,resultY+114,rightW-32,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Provenance",rightX+16,resultY+154,rightW-32,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Lead confidence",rightX+16,resultY+194,100,16,tinyFmt_.Get(),brush_.muted.Get());

        const float actionY=resultY+resultH-36.0f;
        if(selectedTask && selectedTask->status==sentinel::identity::IdentityResearchStatus::Queued) {
            AddButton(L"research_complete",L"Save Result",rightX+16,actionY,94,26,true);
            AddButton(L"research_reject",L"Reject",rightX+118,actionY,68,26,false);
        } else if(selectedTask && selectedTask->status==sentinel::identity::IdentityResearchStatus::Completed) {
            AddButton(L"research_promote",L"Promote to Lead",rightX+16,actionY,118,26,true);
            AddButton(L"research_reject",L"Reject",rightX+142,actionY,68,26,false);
            TextLine(
                L"Promotion creates an unverified lead. Lead verification and subject confirmation remain separate.",
                rightX+220,actionY+3,rightW-236,20,tinyFmt_.Get(),brush_.muted.Get());
        } else if(selectedTask && selectedTask->status==sentinel::identity::IdentityResearchStatus::PromotedToLead) {
            TextLine(
                L"Promoted to lead "+Widen(selectedTask->promotedLeadId)+L"; verify it separately in Subjects & Identity.",
                rightX+16,actionY+3,rightW-32,20,tinyFmt_.Get(),brush_.cyan.Get());
        } else if(selectedTask && selectedTask->status==sentinel::identity::IdentityResearchStatus::Rejected) {
            TextLine(L"Rejected research task; it cannot be promoted.",rightX+16,actionY+3,rightW-32,20,tinyFmt_.Get(),brush_.red.Get());
        }

        TextLine(
            L"Rejected: "+std::to_wstring(rejected)+L" | Providers: "+
            std::to_wstring(researchProviders_.size())+L" enabled",
            x+16,bodyY+bodyH-24,leftW-32,18,tinyFmt_.Get(),brush_.muted.Get());
    }

    void DrawSimulation(float w,float h) {
        std::wstring simulationSubtitle=
            L"Investigator-controlled synthetic conversation, persona memory, media, and transcript capture";
        if(!cases_.empty()) {
            simulationSubtitle+=L" | Case "+Widen(cases_[selectedCase_].caseNumber);
        }
        PageTitle(L"Simulation Chat",simulationSubtitle);
        const float x=kSidebar+28.0f;
        const float y=kHeader+102.0f;
        const float gap=14.0f;
        const float contentW=std::max(640.0f,w-x-28.0f);
        const float sideW=std::clamp(contentW*0.35f,270.0f,330.0f);
        const float chatW=std::max(360.0f,contentW-sideW-gap);
        const float cardH=std::max(500.0f,h-y-24.0f);

        // Conversation card
        Rounded(x,y,chatW,cardH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Synthetic Conversation",x+20,y+12,280,34,h1Fmt_.Get(),brush_.text.Get());
        AddButton(L"sim_export_persona_log",L"Export Log",x+chatW-204,y+15,82,28,false);
        Badge(L"SIMULATION",x+chatW-112,y+15,brush_.cyan.Get(),90);

        const float transcriptTop=y+56;
        const float composerY=y+cardH-70.0f;
        const float transcriptBottom=composerY-18.0f;
        const float messageBottom=simBotTyping_ ? transcriptBottom-78.0f : transcriptBottom;
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
            if(auto imageId=ParseImageMarker(turn.text)) {
                auto imageInfo=GetConversationImage(*imageId);
                if(imageInfo) {
                    auto bitmap=LoadD2DBitmap(imageInfo->path);
                    if(bitmap) {
                        const auto size=bitmap->GetSize();
                        const float boxW=64.0f, boxH=40.0f;
                        const float scale=std::min(boxW/std::max(1.0f,size.width),boxH/std::max(1.0f,size.height));
                        const float drawW=size.width*scale;
                        const float drawH=size.height*scale;
                        target_->DrawBitmap(
                            bitmap.Get(),
                            D2D1::RectF(bx+12,yy+26,bx+12+drawW,yy+26+drawH),
                            1.0f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    }
                    const std::wstring label=imageInfo->caption.empty()
                        ? Widen(imageInfo->originalName)
                        : Widen(imageInfo->caption);
                    Text(label,bx+84,yy+27,bubbleW-96,36,smallFmt_.Get(),brush_.text.Get());
                } else {
                    TextLine(L"[Image unavailable]",bx+12,yy+28,bubbleW-24,32,smallFmt_.Get(),brush_.muted.Get());
                }
            } else {
                Text(Widen(turn.text),bx+12,yy+25,bubbleW-24,40,smallFmt_.Get(),brush_.text.Get());
            }
            simMessageRects_.push_back({{bx,yy,bx+bubbleW,yy+72},(size_t)i});
            yy+=82;
        }

        if(simBotTyping_) {
            const float typingY=transcriptBottom-62;
            const float typingW=std::min(chatW-120.0f,610.0f);
            Rounded(x+20,typingY,typingW,54,brush_.sidebar.Get(),brush_.border.Get(),10);
            TextLine(L"Synthetic Subject",x+32,typingY+4,typingW-64,17,tinyFmt_.Get(),brush_.green.Get());

            const auto full=Widen(simPreparedReply_);
            size_t visible=0;
            if(!full.empty()) {
                if(simTypingDurationMs_<=0 || simTypingStartedTick_==0) {
                    visible=full.size();
                } else {
                    const auto elapsed=std::min<ULONGLONG>(
                        GetTickCount64()-simTypingStartedTick_,
                        (ULONGLONG)simTypingDurationMs_);
                    visible=(size_t)((elapsed*full.size())/
                        std::max<ULONGLONG>(1,(ULONGLONG)simTypingDurationMs_));
                    if(elapsed>0) visible=std::max<size_t>(1,visible);
                    visible=std::min(visible,full.size());
                }
            }

            std::wstring live=full.substr(0,visible);
            live+=L"|";
            Text(live,x+32,typingY+22,typingW-52,26,tinyFmt_.Get(),brush_.text.Get());
        }

        UpdateSimulationScrollbar();

        // Stable vector buttons avoid font/encoding regressions in packaged builds.
        const float sendW=std::clamp(chatW*0.24f,104.0f,146.0f);
        const float sendX=x+chatW-sendW-18.0f;
        AddIconButton(L"sim_emoji",IconKind::Smile,x+18,composerY,38,46,false);
        AddIconButton(L"sim_attach",IconKind::Paperclip,x+62,composerY,38,46,false);
        AddButton(L"sim_send",L"Send",sendX,composerY,sendW,46,true);

        // Responsive Scenario / Model / Conversation context. This card uses
        // the actual remaining width instead of overflowing the packaged window.
        const float rx=x+chatW+gap;
        Rounded(rx,y,sideW,cardH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Scenario & Model",rx+16,y+12,sideW-32,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Persona",rx+16,y+50,70,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(Widen(simSettings_.persona.name),rx+92,y+47,sideW-108,22,smallFmt_.Get(),brush_.text.Get());

        TextLine(L"Age state",rx+16,y+76,70,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(Widen(sentinel::simulation::ToString(simSettings_.ageState)),rx+92,y+73,sideW-108,22,tinyFmt_.Get(),brush_.cyan.Get());

        TextLine(L"OpenAI-compatible endpoint",rx+16,y+104,sideW-32,18,tinyFmt_.Get(),brush_.muted.Get());

        const float modelButtonGap=8.0f;
        const float modelButtonW=(sideW-40.0f-modelButtonGap)/2.0f;
        AddButton(L"sim_browse_models",L"Browse Models",rx+16,y+164,modelButtonW,32,false);
        AddButton(L"sim_install_ai",L"Install / Repair",rx+16+modelButtonW+modelButtonGap,y+164,modelButtonW,32,true);

        TextLine(L"Available model",rx+16,y+204,112,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Manual model",rx+16,y+258,106,18,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"sim_model",L"Connect",rx+sideW-98,y+278,82,30,true);

        StatusDot(rx+22,y+326,4,modelStatus_.find(L"Connected")!=std::wstring::npos?brush_.green.Get():brush_.yellow.Get());
        TextLine(modelStatus_,rx+34,y+313,sideW-50,28,tinyFmt_.Get(),brush_.text.Get());

        const float sectionY=y+350.0f;
        target_->DrawLine(
            D2D1::Point2F(rx+16,sectionY),
            D2D1::Point2F(rx+sideW-16,sectionY),
            brush_.border.Get(),1.0f);

        TextLine(L"Conversation",rx+16,sectionY+10,sideW-32,22,smallFmt_.Get(),brush_.text.Get());
        TextLine(currentConversationTitle_,rx+16,sectionY+36,sideW-32,20,tinyFmt_.Get(),brush_.cyan.Get());

        const float convButtonW=(sideW-40.0f-modelButtonGap)/2.0f;
        AddButton(L"sim_previous_chat",L"Previous",rx+16,sectionY+62,convButtonW,30,false);
        AddButton(L"sim_new_chat",L"New Chat",rx+16+convButtonW+modelButtonGap,sectionY+62,convButtonW,30,true);

        const float suggestionY=sectionY+104.0f;
        TextLine(L"Model Suggestion",rx+16,suggestionY,sideW-32,22,smallFmt_.Get(),brush_.text.Get());

        const float actionY=y+cardH-38.0f;
        const float suggestionBoxY=suggestionY+26.0f;
        const float suggestionBoxH=std::max(36.0f,actionY-suggestionBoxY-10.0f);
        Rounded(rx+16,suggestionBoxY,sideW-32,suggestionBoxH,brush_.sidebar.Get(),brush_.border.Get(),8);
        Text(simSuggestion_,rx+26,suggestionBoxY+8,sideW-52,suggestionBoxH-14,tinyFmt_.Get(),brush_.text.Get());

        const float bw=(sideW-48.0f)/3.0f;
        AddButton(L"sim_suggest",L"Generate",rx+16,actionY,bw,30,false);
        AddButton(L"sim_reset",L"Reset",rx+24+bw,actionY,bw,30,false);
        AddButton(L"sim_preserve",L"Preserve",rx+32+bw*2,actionY,bw,30,false);
    }

    void ShowChatEditor(bool show) {
        if(chatEdit_) ShowWindow(chatEdit_,show?SW_SHOW:SW_HIDE);
        if(modelEndpointEdit_) ShowWindow(modelEndpointEdit_,show?SW_SHOW:SW_HIDE);
        if(modelNameEdit_) ShowWindow(modelNameEdit_,show?SW_SHOW:SW_HIDE);
        if(modelCombo_) ShowWindow(modelCombo_,show?SW_SHOW:SW_HIDE);
        if(simScroll_) ShowWindow(simScroll_,show?SW_SHOW:SW_HIDE);
    }

    void ShowPersonaEditors(bool show) {
        HWND allControls[]={
            personaNameEdit_,personaAgeCombo_,personaLocationEdit_,personaInterestsEdit_,
            personaOccupationEdit_,personaEducationEdit_,personaFamilyEdit_,personaBackgroundEdit_,
            personaGenderCombo_,personaPronounsCombo_,personaRelationshipCombo_,personaPersonalityCombo_,personaSocialCombo_,personaConfidenceCombo_,
            scenarioNameEdit_,scenarioObjectiveEdit_,scenarioSeedEdit_,minDelayEdit_,maxDelayEdit_,ageStateCombo_,
            personaCommunicationCombo_,personaCognitiveCombo_,personaSlangCombo_,personaGrammarCombo_,personaTypoCombo_,personaEmojiCombo_,
            personaWritingStyleCombo_,personaProfileCombo_
        };
        for(HWND h:allControls) if(h) ShowWindow(h,SW_HIDE);
        if(!show) return;

        if(personaProfileCombo_) ShowWindow(personaProfileCombo_,SW_SHOW);

        auto showGroup=[&](std::initializer_list<HWND> controls){
            for(HWND h:controls) if(h) ShowWindow(h,SW_SHOW);
        };

        switch(personaTab_) {
            case PersonaTab::Profile:
                showGroup({personaNameEdit_,personaAgeCombo_,personaGenderCombo_,personaPronounsCombo_,
                    personaLocationEdit_,personaOccupationEdit_,personaEducationEdit_,personaRelationshipCombo_,
                    personaFamilyEdit_,personaInterestsEdit_});
                break;
            case PersonaTab::Bio:
                showGroup({personaBackgroundEdit_});
                break;
            case PersonaTab::Behavior:
                showGroup({personaPersonalityCombo_,personaSocialCombo_,personaConfidenceCombo_,
                    personaWritingStyleCombo_,personaCommunicationCombo_,personaCognitiveCombo_,
                    personaSlangCombo_,personaGrammarCombo_,personaTypoCombo_,personaEmojiCombo_});
                break;
            case PersonaTab::Scenario:
                showGroup({scenarioNameEdit_,scenarioObjectiveEdit_,scenarioSeedEdit_,minDelayEdit_,maxDelayEdit_,ageStateCombo_});
                break;
            case PersonaTab::Gallery:
            case PersonaTab::Rules:
                break;
        }
    }

    void ShowAgencyEditors(bool show) {
        if(agencyEndpointEdit_) ShowWindow(agencyEndpointEdit_,show?SW_SHOW:SW_HIDE);
        if(agencyIdEdit_) ShowWindow(agencyIdEdit_,show?SW_SHOW:SW_HIDE);
        if(operatingStateCombo_) ShowWindow(operatingStateCombo_,show?SW_SHOW:SW_HIDE);
    }

    void ShowSubjectEditors(bool show) {
        HWND controls[]={
            subjectDisplayEdit_,subjectLegalEdit_,subjectAliasesEdit_,subjectUsernamesEdit_,subjectContactsEdit_,subjectNotesEdit_,
            identitySourceTypeEdit_,identitySourceRefEdit_,identityLeadValueEdit_,identityConfidenceEdit_,identityProvenanceEdit_
        };
        for(HWND control:controls) if(control) ShowWindow(control,show?SW_SHOW:SW_HIDE);
    }

    void ShowIdentityResearchEditors(bool show) {
        HWND controls[]={
            researchTypeCombo_,researchProviderCombo_,researchQueryEdit_,researchPurposeEdit_,
            researchResultEdit_,researchReferenceEdit_,researchProvenanceEdit_,researchConfidenceEdit_
        };
        for(HWND control:controls) if(control) ShowWindow(control,show?SW_SHOW:SW_HIDE);
    }

    void ShowIdentityResearchProviderEditors(bool show) {
        HWND controls[]={
            researchProviderIdEdit_,researchProviderNameEdit_,researchProviderModeCombo_,
            researchProviderEndpointEdit_,researchProviderCredentialEdit_,researchProviderNotesEdit_
        };
        for(HWND control:controls) if(control) ShowWindow(control,show?SW_SHOW:SW_HIDE);
    }

    void ApplyPageControls() {
        ShowCaseEditors(page_==Page::Cases);
        ShowChatEditor(page_==Page::Simulation);
        ShowPersonaEditors(page_==Page::Persona);
        ShowAgencyEditors(page_==Page::Agency);
        ShowSubjectEditors(page_==Page::Subjects && !cases_.empty());
        ShowIdentityResearchEditors(page_==Page::IdentityResearch && !researchProviderManagerOpen_);
        ShowIdentityResearchProviderEditors(page_==Page::IdentityResearch && researchProviderManagerOpen_);
        const bool showResponseRuleEditors=
            page_==Page::ModelLab ||
            (page_==Page::Persona && personaTab_==PersonaTab::Rules);
        if(responseRuleTriggerEdit_) ShowWindow(responseRuleTriggerEdit_,showResponseRuleEditors?SW_SHOW:SW_HIDE);
        if(responseRuleResponseEdit_) ShowWindow(responseRuleResponseEdit_,showResponseRuleEditors?SW_SHOW:SW_HIDE);

        HWND trainerAlways[]={
            trainerModeCombo_,trainerFoundationCombo_,trainerInstructionEdit_
        };
        for(HWND h:trainerAlways) if(h) ShowWindow(h,page_==Page::Trainer?SW_SHOW:SW_HIDE);

        HWND trainerAdvanced[]={
            trainerForkNameEdit_,trainerBasePathEdit_,trainerLoraNameEdit_,trainerLoraPathEdit_,
            trainerDatasetEdit_,trainerOutputEdit_
        };
        const bool showTrainerAdvanced=page_==Page::Trainer && trainerAdvancedOpen_;
        for(HWND h:trainerAdvanced) if(h) ShowWindow(h,showTrainerAdvanced?SW_SHOW:SW_HIDE);

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
            const float x=kSidebar+28.0f;
            const float y=kHeader+102.0f;
            const float gap=14.0f;
            const float contentW=std::max(640.0f,w-x-28.0f);
            const float sideW=std::clamp(contentW*0.35f,270.0f,330.0f);
            const float chatW=std::max(360.0f,contentW-sideW-gap);
            const float cardH=std::max(500.0f,(float)rc.bottom-y-24.0f);
            const float transcriptTop=y+56.0f;
            const float composerY=y+cardH-70.0f;
            const float transcriptBottom=composerY-18.0f;
            const float rx=x+chatW+gap;

            MoveControl(simScroll_,(int)(x+chatW-20),(int)transcriptTop,14,
                (int)std::max(120.0f,transcriptBottom-transcriptTop),TRUE);

            const float sendW=std::clamp(chatW*0.24f,104.0f,146.0f);
            const float sendX=x+chatW-sendW-18.0f;
            const int composerX=(int)(x+104);
            const int composerW=std::max(120,(int)(sendX-(x+104)-12.0f));
            const int composerH=46;
            MoveControl(chatEdit_,composerX,(int)composerY,composerW,composerH,TRUE);
            RECT composerTextRect{12,9,std::max(24,composerW-12),composerH-8};
            SendMessageW(chatEdit_,EM_SETRECTNP,0,(LPARAM)&composerTextRect);

            MoveControl(modelEndpointEdit_,(int)(rx+16),(int)(y+124),(int)(sideW-32),32,TRUE);
            MoveControl(modelCombo_,(int)(rx+16),(int)(y+222),(int)(sideW-32),170,TRUE);
            MoveControl(modelNameEdit_,(int)(rx+16),(int)(y+278),(int)(sideW-122),30,TRUE);
        }

        if(page_==Page::Subjects) {
            const float x=kSidebar+28.0f;
            const float y=kHeader+94.0f;
            const float contentW=w-x-28.0f;
            const float gap=14.0f;
            const float metricsH=78.0f;
            const float bodyY=y+metricsH+12.0f;
            const float bodyH=std::max(430.0f,(float)rc.bottom-bodyY-24.0f);
            const float leftW=std::clamp(contentW*0.34f,248.0f,286.0f);
            const float rightX=x+leftW+gap;
            const float rightW=contentW-leftW-gap;
            const float detailH=238.0f;
            const float leadComposerY=bodyY+bodyH-214.0f;

            const float halfGap=10.0f;
            const float halfW=(rightW-32.0f-halfGap)/2.0f;
            const float fieldLeft=rightX+16.0f;
            const float fieldRight=fieldLeft+halfW+halfGap;

            MoveControl(subjectDisplayEdit_,(int)fieldLeft,(int)(bodyY+62),(int)halfW,28,TRUE);
            MoveControl(subjectLegalEdit_,(int)fieldRight,(int)(bodyY+62),(int)halfW,28,TRUE);
            MoveControl(subjectAliasesEdit_,(int)fieldLeft,(int)(bodyY+110),(int)halfW,28,TRUE);
            MoveControl(subjectUsernamesEdit_,(int)fieldRight,(int)(bodyY+110),(int)halfW,28,TRUE);
            MoveControl(subjectContactsEdit_,(int)fieldLeft,(int)(bodyY+158),(int)(rightW-32.0f),28,TRUE);
            MoveControl(subjectNotesEdit_,(int)fieldLeft,(int)(bodyY+202),(int)(rightW-32.0f),42,TRUE);

            const int leadFieldX=(int)(x+92.0f);
            const int leadFieldW=(int)(leftW-108.0f);
            MoveControl(identitySourceTypeEdit_,leadFieldX,(int)(leadComposerY+34),leadFieldW,24,TRUE);
            MoveControl(identitySourceRefEdit_,leadFieldX,(int)(leadComposerY+64),leadFieldW,24,TRUE);
            MoveControl(identityLeadValueEdit_,leadFieldX,(int)(leadComposerY+94),leadFieldW,24,TRUE);
            MoveControl(identityConfidenceEdit_,leadFieldX,(int)(leadComposerY+124),58,24,TRUE);
            MoveControl(identityProvenanceEdit_,leadFieldX,(int)(leadComposerY+154),leadFieldW,24,TRUE);

            RECT notesRect{8,6,std::max(24,(int)(rightW-48.0f)),34};
            SendMessageW(subjectNotesEdit_,EM_SETRECTNP,0,(LPARAM)&notesRect);
        }

        if(page_==Page::IdentityResearch) {
            const float x=kSidebar+28.0f;
            const float y=kHeader+94.0f;
            const float contentW=w-x-28.0f;
            const float gap=14.0f;
            const float metricsH=78.0f;
            const float bodyY=y+metricsH+12.0f;
            const float bodyH=std::max(430.0f,(float)rc.bottom-bodyY-24.0f);
            const float leftW=std::clamp(contentW*0.35f,270.0f,330.0f);
            const float rightX=x+leftW+gap;
            const float rightW=contentW-leftW-gap;
            const float requestH=204.0f;
            const float resultY=bodyY+requestH+12.0f;

            if(researchProviderManagerOpen_) {
                const float providerLeftW=std::clamp(contentW*0.38f,300.0f,360.0f);
                const float providerRightX=x+providerLeftW+gap;
                const float providerRightW=contentW-providerLeftW-gap;
                const float splitGap=10.0f;
                const float splitW=(providerRightW-32.0f-splitGap)/2.0f;
                MoveControl(researchProviderIdEdit_,(int)(providerRightX+16),(int)(y+106),(int)splitW,28,TRUE);
                MoveControl(researchProviderNameEdit_,(int)(providerRightX+16+splitW+splitGap),(int)(y+106),(int)splitW,28,TRUE);
                MoveControl(researchProviderModeCombo_,(int)(providerRightX+16),(int)(y+152),(int)splitW,150,TRUE);
                MoveControl(researchProviderEndpointEdit_,(int)(providerRightX+16+splitW+splitGap),(int)(y+152),(int)splitW,28,TRUE);
                MoveControl(researchProviderCredentialEdit_,(int)(providerRightX+16),(int)(y+198),(int)(providerRightW-32),28,TRUE);
                MoveControl(researchProviderNotesEdit_,(int)(providerRightX+16),(int)(y+306),(int)(providerRightW-32),88,TRUE);
                RECT providerNotesRect{10,7,std::max(24,(int)(providerRightW-52)),80};
                SendMessageW(researchProviderNotesEdit_,EM_SETRECTNP,0,(LPARAM)&providerNotesRect);
            } else {
                const float halfGap=10.0f;
                const float halfW=(rightW-32.0f-halfGap)/2.0f;
                MoveControl(researchTypeCombo_,(int)(rightX+16),(int)(bodyY+58),(int)halfW,170,TRUE);
                MoveControl(researchProviderCombo_,(int)(rightX+16+halfW+halfGap),(int)(bodyY+58),(int)halfW,180,TRUE);
                MoveControl(researchQueryEdit_,(int)(rightX+16),(int)(bodyY+102),(int)(rightW-32),28,TRUE);
                MoveControl(researchPurposeEdit_,(int)(rightX+16),(int)(bodyY+146),(int)(rightW-32),28,TRUE);

                MoveControl(researchResultEdit_,(int)(rightX+16),(int)(resultY+58),(int)(rightW-32),58,TRUE);
                RECT resultRect{10,7,std::max(24,(int)(rightW-52)),50};
                SendMessageW(researchResultEdit_,EM_SETRECTNP,0,(LPARAM)&resultRect);
                MoveControl(researchReferenceEdit_,(int)(rightX+16),(int)(resultY+130),(int)(rightW-32),26,TRUE);
                MoveControl(researchProvenanceEdit_,(int)(rightX+16),(int)(resultY+170),(int)(rightW-32),26,TRUE);
                MoveControl(researchConfidenceEdit_,(int)(rightX+16),(int)(resultY+210),76,26,TRUE);
            }
        }

        if(page_==Page::Persona) {
            const float x=kSidebar+28.0f, y=kHeader+104.0f;
            const float contentW=w-x-28.0f;

            MoveControl(personaProfileCombo_,(int)(x+108),(int)(y-42),360,180);

            const float py=y+56.0f;

            if(personaTab_==PersonaTab::Profile) {
                const float left=x+34.0f, right=x+contentW*0.52f;
                const float field1=left+118.0f, field2=right+126.0f;
                const int w1=(int)(contentW*0.36f-118.0f);
                const int w2=(int)(contentW*0.42f-138.0f);
                float row=py+74.0f;

                MoveControl(personaNameEdit_,(int)field1,(int)row,w1,34); 
                MoveControl(personaAgeCombo_,(int)field2,(int)row,w2,170); row+=52;

                MoveControl(personaGenderCombo_,(int)field1,(int)row,w1,170);
                MoveControl(personaPronounsCombo_,(int)field2,(int)row,w2,170); row+=52;

                MoveControl(personaLocationEdit_,(int)field1,(int)row,w1,34);
                MoveControl(personaOccupationEdit_,(int)field2,(int)row,w2,34); row+=52;

                MoveControl(personaEducationEdit_,(int)field1,(int)row,w1,34);
                MoveControl(personaRelationshipCombo_,(int)field2,(int)row,w2,170); row+=52;

                MoveControl(personaFamilyEdit_,(int)field1,(int)row,w1,34);
                MoveControl(personaInterestsEdit_,(int)field2,(int)row,w2,34);
            }
            else if(personaTab_==PersonaTab::Bio) {
                MoveControl(personaBackgroundEdit_,
                    (int)(x+42),(int)(py+176),(int)(contentW-84),(int)std::max(260.0f,(float)rc.bottom-(py+222)));
            }
            else if(personaTab_==PersonaTab::Behavior) {
                const float col1=x+34.0f, col2=x+contentW*0.50f+10.0f;
                const float field1=col1+150.0f, field2=col2+150.0f;
                const int fw=(int)(contentW*0.36f);
                float row=py+78.0f;

                MoveControl(personaPersonalityCombo_,(int)field1,(int)row,fw,180);
                MoveControl(personaSocialCombo_,(int)field2,(int)row,fw,180); row+=54;

                MoveControl(personaConfidenceCombo_,(int)field1,(int)row,fw,160);
                MoveControl(personaWritingStyleCombo_,(int)field2,(int)row,fw,180); row+=54;

                MoveControl(personaCommunicationCombo_,(int)field1,(int)row,fw,180);
                MoveControl(personaCognitiveCombo_,(int)field2,(int)row,fw,180); row+=54;

                MoveControl(personaSlangCombo_,(int)field1,(int)row,fw,180);
                MoveControl(personaGrammarCombo_,(int)field2,(int)row,fw,180); row+=54;

                MoveControl(personaTypoCombo_,(int)field1,(int)row,fw,180);
                MoveControl(personaEmojiCombo_,(int)field2,(int)row,fw,180);
            }
            else if(personaTab_==PersonaTab::Scenario) {
                const float left=x+34.0f;
                const float labelW=116.0f;
                const float fieldX=left+labelW;
                const int mainW=(int)(contentW-190.0f);
                float row=py+82.0f;

                MoveControl(scenarioNameEdit_,(int)fieldX,(int)row,mainW,34); row+=58;
                MoveControl(scenarioObjectiveEdit_,(int)fieldX,(int)row,mainW,34); row+=58;

                MoveControl(scenarioSeedEdit_,(int)fieldX,(int)row,150,34);
                MoveControl(ageStateCombo_,(int)(x+contentW*0.42f+104),(int)row,(int)(contentW*0.36f),180); row+=58;

                MoveControl(minDelayEdit_,(int)fieldX,(int)row,160,34);
                MoveControl(maxDelayEdit_,(int)(x+contentW*0.42f+116),(int)row,160,34);
            }
            else if(personaTab_==PersonaTab::Rules) {
                MoveControl(
                    responseRuleTriggerEdit_,
                    (int)(x+86),(int)(py+78),
                    (int)std::max(190.0f,contentW*0.34f),30,TRUE);
                MoveControl(
                    responseRuleResponseEdit_,
                    (int)(x+contentW*0.50f+86),(int)(py+78),
                    (int)std::max(200.0f,contentW*0.50f-108.0f),30,TRUE);
            }
        }

        if(page_==Page::ModelLab) {
            const float x=kSidebar+28.0f;
            const float y=kHeader+94.0f;
            const float contentW=w-x-28.0f;
            const float dashY=y+48.0f;
            const float metricsY=dashY+88.0f;
            const float mainY=metricsY+98.0f;
            const float rulesY=mainY+204.0f+12.0f;
            MoveControl(responseRuleTriggerEdit_,
                (int)(x+76),(int)(rulesY+43),(int)std::max(150.0f,contentW*0.27f),28,TRUE);
            MoveControl(responseRuleResponseEdit_,
                (int)(x+contentW*0.39f+72),(int)(rulesY+43),
                (int)std::max(160.0f,contentW*0.31f),28,TRUE);
        }

        if(page_==Page::Trainer) {
            const float x=kSidebar+28.0f;
            const float y=kHeader+94.0f;
            const float contentW=w-x-28.0f;
            const float gap=14.0f;
            const float heroY=y+48.0f;
            const float heroH=76.0f;
            const float bodyY=heroY+heroH+12.0f;
            const float bodyH=std::max(410.0f,(float)rc.bottom-bodyY-24.0f);
            const float rightW=std::clamp(contentW*0.29f,286.0f,342.0f);
            const float leftW=contentW-rightW-gap;
            const float fieldSplit=leftW*0.48f;
            const float actionY=bodyY+bodyH-42.0f;
            const float composerY=actionY-94.0f;
            const float advancedY=composerY-132.0f;

            MoveControl(trainerModeCombo_,
                (int)(x+18),(int)(bodyY+66),(int)std::max(170.0f,leftW*0.42f),180,TRUE);
            MoveControl(trainerFoundationCombo_,
                (int)(x+fieldSplit),(int)(bodyY+66),(int)std::max(190.0f,leftW-fieldSplit-18.0f),180,TRUE);

            MoveControl(trainerInstructionEdit_,
                (int)(x+18),(int)(composerY+22),(int)(leftW-36),62,TRUE);
            RECT trainerTextRect{12,8,std::max(24,(int)(leftW-60)),54};
            SendMessageW(trainerInstructionEdit_,EM_SETRECTNP,0,(LPARAM)&trainerTextRect);

            MoveControl(trainerForkNameEdit_,
                (int)(x+100),(int)(advancedY+20),(int)std::max(130.0f,fieldSplit-118.0f),28,TRUE);
            MoveControl(trainerBasePathEdit_,
                (int)(x+fieldSplit+74),(int)(advancedY+20),(int)std::max(150.0f,leftW-fieldSplit-92.0f),28,TRUE);

            MoveControl(trainerLoraNameEdit_,
                (int)(x+100),(int)(advancedY+54),(int)std::max(130.0f,fieldSplit-118.0f),28,TRUE);
            MoveControl(trainerLoraPathEdit_,
                (int)(x+fieldSplit+74),(int)(advancedY+54),(int)std::max(150.0f,leftW-fieldSplit-92.0f),28,TRUE);

            MoveControl(trainerDatasetEdit_,
                (int)(x+100),(int)(advancedY+88),(int)std::max(130.0f,fieldSplit-118.0f),28,TRUE);
            MoveControl(trainerOutputEdit_,
                (int)(x+fieldSplit+74),(int)(advancedY+88),(int)std::max(150.0f,leftW-fieldSplit-92.0f),28,TRUE);
        }

        if(page_==Page::Agency) {
            const float x=kSidebar+28.0f, y=kHeader+104.0f, gap=14.0f;
            const float contentW=w-x-28.0f;
            const float leftW=(contentW-gap)*0.58f;
            MoveControl(agencyEndpointEdit_,(int)(x+142),(int)(y+62),(int)(leftW-164),32);
            MoveControl(agencyIdEdit_,(int)(x+142),(int)(y+108),(int)(leftW-164),32);
            MoveControl(operatingStateCombo_,(int)(x+142),(int)(y+304),(int)(leftW-164),180);
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

    std::vector<PersonaResponseRuleView> PersonaResponseRules(size_t limit=8) const {
        std::vector<PersonaResponseRuleView> out;
        sqlite3_stmt* s{};
        const char* sql=
            "SELECT id,match_type,trigger_text,response_text,response_mode,enabled,priority,terminal "
            "FROM persona_response_rules WHERE persona_name=? "
            "ORDER BY priority DESC,id DESC LIMIT ?";
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
            item.priority=sqlite3_column_int(s,6);
            item.terminal=sqlite3_column_int(s,7)!=0;
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
            "SELECT id,match_type,trigger_text,response_text,response_mode,enabled,priority,terminal "
            "FROM persona_response_rules "
            "WHERE persona_name=? AND enabled=1 "
            "ORDER BY priority DESC,id DESC";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)
            return std::nullopt;

        sqlite3_bind_text(s,1,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);

        std::optional<PersonaResponseRuleView> found;
        sentinel::simulation::ResponseRuleMatchQuality bestQuality{};
        int bestPriority=0;
        long long bestId=0;

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
            item.priority=sqlite3_column_int(s,6);
            item.terminal=sqlite3_column_int(s,7)!=0;

            const auto quality=sentinel::simulation::EvaluateResponseRuleMatch(
                item.matchType,input,item.trigger);
            if(!quality.Matched()) continue;

            if(!found || sentinel::simulation::PreferResponseRuleMatch(
                    item.priority,item.id,quality,
                    bestPriority,bestId,bestQuality))
            {
                item.matchScore=quality.score;
                found=item;
                bestQuality=quality;
                bestPriority=item.priority;
                bestId=item.id;
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
        const auto variants=sentinel::simulation::SplitResponseRuleVariants(rule->response);
        const auto selected=sentinel::simulation::SelectResponseRuleVariant(
            rule->response,
            "rule-test|"+sample+"|"+std::to_string(rule->id));
        const std::wstring msg=
            L"Matched Rule #"+std::to_wstring(rule->id)+
            L"\nMatch: "+Widen(rule->matchType)+
            L" ("+std::to_wstring(rule->matchScore)+L"%)"+
            L"\nPriority: "+std::to_wstring(rule->priority)+
            L"\nFlow: "+std::wstring(rule->terminal?L"Terminal / stop":L"Continue with model")+
            L"\nAlternates: "+std::to_wstring(variants.size())+
            L"\nTrigger: "+Widen(rule->trigger)+
            L"\nSelected response: "+Widen(selected);
        statusText_=L"Rule #"+std::to_wstring(rule->id)+L" matched sample at "+
            std::to_wstring(rule->matchScore)+L"%";
        MessageBoxW(hwnd_,msg.c_str(),L"Rule Test Result",MB_OK|MB_ICONINFORMATION);
    }

    void LoadPersonaResponseRuleIntoEditors(long long id) {
        sqlite3_stmt* s{};
        const char* sql=
            "SELECT trigger_text,response_text,response_mode,terminal FROM persona_response_rules "
            "WHERE id=? AND persona_name=? LIMIT 1";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) {
            statusText_=L"Unable to load response rule";
            return;
        }

        sqlite3_bind_int64(s,1,id);
        sqlite3_bind_text(s,2,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
        if(sqlite3_step(s)==SQLITE_ROW) {
            const auto* trigger=(const char*)sqlite3_column_text(s,0);
            const auto* response=(const char*)sqlite3_column_text(s,1);
            const auto* mode=(const char*)sqlite3_column_text(s,2);
            SetWindowTextW(responseRuleTriggerEdit_,Widen(trigger?trigger:"").c_str());
            SetWindowTextW(responseRuleResponseEdit_,Widen(response?response:"").c_str());
            responseRuleExactWording_=mode && std::string(mode)=="exact";
            responseRuleTerminal_=sqlite3_column_int(s,3)!=0;
            statusText_=L"Loaded Rule #"+std::to_wstring(id)+L" into the rule editor";
        } else {
            statusText_=L"Response rule not found";
        }
        sqlite3_finalize(s);
    }

    void TogglePersonaResponseRuleEnabled(long long id) {
        sqlite3_stmt* s{};
        const char* sql=
            "UPDATE persona_response_rules "
            "SET enabled=CASE enabled WHEN 1 THEN 0 ELSE 1 END,"
            "updated_utc=CURRENT_TIMESTAMP "
            "WHERE id=? AND persona_name=?";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK) {
            statusText_=L"Unable to update response rule";
            return;
        }
        sqlite3_bind_int64(s,1,id);
        sqlite3_bind_text(s,2,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
        const bool changed=sqlite3_step(s)==SQLITE_DONE && sqlite3_changes(runtime_->db.Handle())==1;
        sqlite3_finalize(s);
        statusText_=changed
            ? L"Rule #"+std::to_wstring(id)+L" enabled state changed"
            : L"Response rule could not be updated";
    }

    void AddPersonaResponseRule(const std::string& matchType) {
        const auto trigger=Narrow(EditText(responseRuleTriggerEdit_));
        const auto response=Narrow(EditText(responseRuleResponseEdit_));
        if(trigger.empty() || response.empty()) {
            statusText_=L"Enter both a trigger and a response";
            return;
        }

        const auto variants=sentinel::simulation::SplitResponseRuleVariants(response);
        if(variants.empty()) {
            statusText_=L"Enter at least one non-empty response";
            return;
        }
        for(const auto& variant:variants) {
            const auto policy=sentinel::simulation::EvaluateSimulationPolicy(
                simSettings_.ageState,variant);
            if(!policy.allowed) {
                statusText_=L"One alternate response was rejected by active safety policy";
                MessageBoxW(hwnd_,Widen(policy.reason).c_str(),
                    L"Response Rule Blocked",MB_OK|MB_ICONWARNING);
                return;
            }
        }

        sqlite3_stmt* s{};
        const char* sql=
            "INSERT INTO persona_response_rules("
            "persona_name,match_type,trigger_text,response_text,response_mode,enabled,priority,terminal"
            ") VALUES(?,?,?,?,?,1,100,?)";
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
        sqlite3_bind_int(s,6,responseRuleTerminal_?1:0);
        const int rc=sqlite3_step(s);
        sqlite3_finalize(s);

        if(rc==SQLITE_DONE) {
            SetWindowTextW(responseRuleTriggerEdit_,L"");
            SetWindowTextW(responseRuleResponseEdit_,L"");
            if(matchType=="exact") statusText_=L"Exact response rule added";
            else if(matchType=="contains") statusText_=L"Contains response rule added";
            else statusText_=L"Smart response rule added";
            statusText_+=L" | saved for "+Widen(simSettings_.persona.name);
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

    void ToggleResponseRuleTerminalMode() {
        responseRuleTerminal_=!responseRuleTerminal_;
        statusText_=responseRuleTerminal_
            ? L"Response rule flow set to Terminal / stop"
            : L"Response rule flow set to Continue with model";
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

    std::vector<PersonaLearnedNoteView> PersonaLearnedNotes(size_t limit=20) const {
        std::vector<PersonaLearnedNoteView> out;
        sqlite3_stmt* s{};
        const char* sql=
            "SELECT id,source_kind,note_text,created_utc "
            "FROM persona_learned_notes "
            "WHERE persona_name=? "
            "AND source_kind IN ('reactive_persona_claim','proactive_persona_claim') "
            "ORDER BY created_utc DESC,id DESC LIMIT ?";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)
            return out;
        sqlite3_bind_text(s,1,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_int(s,2,(int)std::min<size_t>(limit,100));
        while(sqlite3_step(s)==SQLITE_ROW) {
            PersonaLearnedNoteView item;
            item.id=sqlite3_column_int64(s,0);
            const auto* kind=(const char*)sqlite3_column_text(s,1);
            const auto* text=(const char*)sqlite3_column_text(s,2);
            const auto* when=(const char*)sqlite3_column_text(s,3);
            item.sourceKind=kind?kind:"";
            item.text=text?text:"";
            item.createdUtc=when?when:"";
            out.push_back(std::move(item));
        }
        sqlite3_finalize(s);
        return out;
    }

    void DeletePersonaLearnedNote(long long id) {
        sqlite3_stmt* s{};
        if(sqlite3_prepare_v2(runtime_->db.Handle(),
            "DELETE FROM persona_learned_notes WHERE id=? AND persona_name=?",
            -1,&s,nullptr)!=SQLITE_OK) {
            statusText_=L"Unable to delete learned continuity note";
            return;
        }
        sqlite3_bind_int64(s,1,id);
        sqlite3_bind_text(s,2,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
        const bool changed=sqlite3_step(s)==SQLITE_DONE &&
            sqlite3_changes(runtime_->db.Handle())>0;
        sqlite3_finalize(s);
        statusText_=changed
            ? L"Learned continuity note deleted"
            : L"Learned continuity note was not found";
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
        if(!simSettings_.learningMode || text.empty() || !sourceKind) return;

        const std::string kind=sourceKind;
        if(kind!="reactive_persona_claim" && kind!="proactive_persona_claim")
            return;

        std::string note=text;
        while(!note.empty() && std::isspace((unsigned char)note.front()))
            note.erase(note.begin());
        while(!note.empty() && std::isspace((unsigned char)note.back()))
            note.pop_back();
        if(note.size()<8) return;
        if(note.size()>600) note.resize(600);

        auto lower=note;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char ch){
            return (char)std::tolower(ch);
        });
        const bool firstPerson=
            lower.rfind("i ",0)==0 ||
            lower.rfind("i'm ",0)==0 ||
            lower.rfind("im ",0)==0 ||
            lower.rfind("my ",0)==0 ||
            lower.rfind("we ",0)==0 ||
            lower.rfind("our ",0)==0 ||
            lower.find(" i ")!=std::string::npos ||
            lower.find(" i'm ")!=std::string::npos ||
            lower.find(" im ")!=std::string::npos ||
            lower.find(" my ")!=std::string::npos ||
            lower.find(" we ")!=std::string::npos ||
            lower.find(" our ")!=std::string::npos;
        if(!firstPerson) return;

        sqlite3_stmt* s{};
        const char* sql=
            "INSERT INTO persona_learned_notes(persona_name,conversation_id,source_kind,note_text) "
            "SELECT ?,?,?,? "
            "WHERE NOT EXISTS("
            "  SELECT 1 FROM persona_learned_notes "
            "  WHERE persona_name=? AND lower(note_text)=lower(?)"
            ")";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)==SQLITE_OK) {
            sqlite3_bind_text(s,1,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(s,2,currentConversationId_.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(s,3,kind.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(s,4,note.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(s,5,simSettings_.persona.name.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(s,6,note.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_step(s);
        }
        sqlite3_finalize(s);
    }


    sentinel::simulation::TrainingMode SelectedTrainingMode() const {
        int sel=(int)SendMessageW(trainerModeCombo_,CB_GETCURSEL,0,0);
        switch(sel) {
            case 1: return sentinel::simulation::TrainingMode::Correction;
            case 2: return sentinel::simulation::TrainingMode::PersonaLora;
            case 3: return sentinel::simulation::TrainingMode::FoundationSft;
            case 4: return sentinel::simulation::TrainingMode::Preference;
            default: return sentinel::simulation::TrainingMode::Behavior;
        }
    }

    std::wstring TrainerModeDisplayName(sentinel::simulation::TrainingMode mode) const {
        switch(mode) {
            case sentinel::simulation::TrainingMode::Behavior: return L"Behavior Tuning";
            case sentinel::simulation::TrainingMode::Correction: return L"Dataset Training";
            case sentinel::simulation::TrainingMode::PersonaLora: return L"Persona LoRA Training";
            case sentinel::simulation::TrainingMode::FoundationSft: return L"Foundation Fork Training";
            case sentinel::simulation::TrainingMode::Preference: return L"Evaluation / Test";
        }
        return L"Behavior Tuning";
    }

    void RefreshTrainerFoundationList(const std::string& selectId={}) {
        if(!trainerFoundationCombo_) return;
        trainerFoundations_=runtime_->trainer.ListFoundations();
        SendMessageW(trainerFoundationCombo_,CB_RESETCONTENT,0,0);
        int selected=0;
        for(size_t i=0;i<trainerFoundations_.size();++i) {
            const auto& foundation=trainerFoundations_[i];
            std::string compact=foundation.name+" v"+std::to_string(foundation.version);
            if(foundation.status!="ACTIVE") compact+=" ("+foundation.status+")";
            const auto label=Widen(compact);
            SendMessageW(trainerFoundationCombo_,CB_ADDSTRING,0,(LPARAM)label.c_str());
            if(!selectId.empty() && foundation.id==selectId) selected=(int)i;
        }
        if(!trainerFoundations_.empty())
            SendMessageW(trainerFoundationCombo_,CB_SETCURSEL,(WPARAM)selected,0);
    }

    std::optional<sentinel::simulation::ModelFoundation> SelectedFoundation() const {
        int sel=(int)SendMessageW(trainerFoundationCombo_,CB_GETCURSEL,0,0);
        if(sel<0 || sel>=(int)trainerFoundations_.size()) return std::nullopt;
        return trainerFoundations_[(size_t)sel];
    }

    void CreateFoundationForkFromTrainer() {
        const auto name=Narrow(EditText(trainerForkNameEdit_));
        const auto trainable=Narrow(EditText(trainerBasePathEdit_));
        auto parent=SelectedFoundation();
        if(name.empty() || !parent) {
            statusText_=L"Enter a fork name and select a parent foundation";
            return;
        }
        try {
            auto fork=runtime_->trainer.CreateFork(
                name,parent->id,parent->sourceModel,trainable,{});
            RefreshTrainerFoundationList(fork.id);
            statusText_=L"Created foundation fork: "+Widen(fork.name);
        } catch(const std::exception& e) {
            statusText_=L"Foundation fork failed: "+Widen(e.what());
        }
    }

    void BindCurrentPersonaLoraFromTrainer() {
        auto foundation=SelectedFoundation();
        const auto loraName=Narrow(EditText(trainerLoraNameEdit_));
        const auto loraPath=Narrow(EditText(trainerLoraPathEdit_));
        if(!foundation || loraName.empty() || loraPath.empty()) {
            statusText_=L"Select a foundation and enter LoRA name/path";
            return;
        }
        try {
            auto binding=runtime_->trainer.BindPersonaLora(
                simSettings_.persona.name,foundation->id,loraName,loraPath,1.0);
            statusText_=L"Bound "+Widen(binding.loraName)+L" to "+Widen(binding.personaName);
            ApplyPersonaRuntimeBinding();
        } catch(const std::exception& e) {
            statusText_=L"LoRA binding failed: "+Widen(e.what());
        }
    }

    static std::string SafeTrainerFileToken(std::string value) {
        for(char& ch:value) {
            const unsigned char u=(unsigned char)ch;
            if(!std::isalnum(u) && ch!='-' && ch!='_') ch='-';
        }
        while(value.find("--")!=std::string::npos)
            value.replace(value.find("--"),2,"-");
        while(!value.empty() && value.front()=='-') value.erase(value.begin());
        while(!value.empty() && value.back()=='-') value.pop_back();
        return value.empty()?"training":value;
    }

    bool TrainerEnvironmentReady() const {
        const auto python=runtime_->root/"trainer-venv"/"Scripts"/"python.exe";
        const auto marker=runtime_->root/"trainer-env.version";
        if(!std::filesystem::exists(python) || !std::filesystem::exists(marker))
            return false;

        std::ifstream in(marker,std::ios::binary);
        std::string version;
        std::getline(in,version);
        while(!version.empty() &&
              (version.back()=='\r' || version.back()=='\n' ||
               version.back()==' ' || version.back()=='\t'))
            version.pop_back();
        return version=="sara-trainer-env-v2";
    }

    void PrepareTrainerEnvironment() {
        const auto script=ExeDir()/L"trainer"/L"Setup-SARA-Trainer.ps1";
        if(!std::filesystem::exists(script)) {
            statusText_=L"Trainer environment setup script is missing";
            return;
        }

        const std::wstring params=
            L"-NoProfile -ExecutionPolicy Bypass -File \""+script.wstring()+
            L"\" -DataRoot \""+runtime_->root.wstring()+L"\"";
        const auto rc=(INT_PTR)ShellExecuteW(
            hwnd_,L"open",L"powershell.exe",params.c_str(),
            (ExeDir()/L"trainer").c_str(),SW_SHOWNORMAL);
        if(rc<=32) {
            statusText_=L"Unable to launch SARA Trainer environment setup";
            return;
        }
        statusText_=L"Trainer environment setup launched; return here after it completes";
    }

    std::filesystem::path AutoExportTrainerDataset() {
        const auto dir=runtime_->root/"trainer-datasets";
        std::filesystem::create_directories(dir);
        const auto path=
            dir/(SafeTrainerFileToken(simSettings_.persona.name)+"-approved.jsonl");
        const auto count=runtime_->trainingReviews.ExportApprovedJsonlForPersona(
            path,simSettings_.persona.name);
        if(count==0) {
            std::error_code ec;
            std::filesystem::remove(path,ec);
            throw std::runtime_error(
                "no approved training examples exist for the active persona");
        }
        SetWindowTextW(trainerDatasetEdit_,path.wstring().c_str());
        return path;
    }

    std::filesystem::path DefaultTrainerOutputPath(
        std::string_view target,
        sentinel::simulation::TrainingMode mode)
    {
        const auto token=
            SafeTrainerFileToken(std::string(target))+"-"+
            SafeTrainerFileToken(sentinel::simulation::ToString(mode))+"-"+
            sentinel::Uuid::Random().ToString().substr(0,8);
        const auto path=runtime_->root/"trainer-runs"/token;
        std::filesystem::create_directories(path.parent_path());
        SetWindowTextW(trainerOutputEdit_,path.wstring().c_str());
        return path;
    }

    void QueueTrainerJobFromControls() {
        const auto mode=SelectedTrainingMode();
        auto foundation=SelectedFoundation();
        auto dataset=Narrow(EditText(trainerDatasetEdit_));
        auto output=Narrow(EditText(trainerOutputEdit_));
        auto basePath=Narrow(EditText(trainerBasePathEdit_));
        const auto target=Narrow(EditText(trainerForkNameEdit_)).empty()
            ? simSettings_.persona.name
            : Narrow(EditText(trainerForkNameEdit_));

        if(mode==sentinel::simulation::TrainingMode::Behavior) {
            statusText_=L"Behavior Tuning uses Send then Apply after human review";
            return;
        }
        if(mode==sentinel::simulation::TrainingMode::Preference) {
            statusText_=L"Evaluation / Test does not queue weight training; use the Evaluation workspace";
            return;
        }

        if((mode==sentinel::simulation::TrainingMode::PersonaLora ||
            mode==sentinel::simulation::TrainingMode::FoundationSft) &&
           !foundation)
        {
            statusText_=L"Select a foundation before queueing this training mode";
            return;
        }

        try {
            if(dataset.empty())
                dataset=Narrow(AutoExportTrainerDataset().wstring());

            if(output.empty())
                output=Narrow(DefaultTrainerOutputPath(target,mode).wstring());

            if(basePath.empty() && foundation) {
                basePath=!foundation->trainableSourcePath.empty()
                    ? foundation->trainableSourcePath
                    : foundation->sourceModel;
                SetWindowTextW(trainerBasePathEdit_,Widen(basePath).c_str());
            }

            auto session=runtime_->trainer.ActiveDialogueSession(
                simSettings_.persona.name,mode);
            const std::string sessionId=session?session->id:"";
            const std::string config=
                "{\"source\":\"SARA Conversational Trainer\","
                "\"dialogue_session\":\""+sessionId+"\","
                "\"dataset_scope\":\"persona-approved-only\"}";

            auto job=runtime_->trainer.QueueJob(
                mode,target,simSettings_.persona.name,
                foundation?foundation->id:std::string{},
                dataset,basePath,output,config);

            if(session) {
                runtime_->trainer.AppendDialogueTurn(
                    session->id,"SYSTEM",
                    "Queued reviewed training job "+job.id+
                    " using the approved "+simSettings_.persona.name+
                    " dataset. Active runtime remains unchanged until training, evaluation, and activation complete.");
            }
            if((mode==sentinel::simulation::TrainingMode::PersonaLora ||
                mode==sentinel::simulation::TrainingMode::FoundationSft) &&
               !TrainerEnvironmentReady())
            {
                statusText_=L"Training job queued; select Prepare Env before running weight training";
            } else {
                statusText_=L"Training job queued: "+Widen(job.id);
            }
        } catch(const std::exception& e) {
            statusText_=L"Training job could not be queued: "+Widen(e.what());
        }
    }

    void RunLatestQueuedTrainerJob() {
        const auto jobs=runtime_->trainer.ListJobs(20);
        auto it=std::find_if(jobs.begin(),jobs.end(),[](const auto& job){
            return job.state=="QUEUED";
        });
        if(it==jobs.end()) {
            statusText_=L"No queued trainer job is waiting to run";
            return;
        }

        if((it->mode==sentinel::simulation::TrainingMode::PersonaLora ||
            it->mode==sentinel::simulation::TrainingMode::FoundationSft) &&
           !TrainerEnvironmentReady())
        {
            statusText_=L"Trainer environment is not ready; select Prepare Env before running this weight-training job";
            return;
        }

        const auto script=ExeDir()/L"trainer"/L"Run-SARA-Training.ps1";
        if(!std::filesystem::exists(script)) {
            statusText_=L"Trainer worker script is missing from this installation";
            return;
        }

        const auto dbPath=runtime_->root/"sentinel.db";
        std::wstring params=
            L"-NoProfile -ExecutionPolicy Bypass -File \""+script.wstring()+
            L"\" -Database \""+dbPath.wstring()+
            L"\" -JobId \""+Widen(it->id)+L"\"";

        const auto rc=(INT_PTR)ShellExecuteW(
            hwnd_,L"open",L"powershell.exe",params.c_str(),
            (ExeDir()/L"trainer").c_str(),SW_SHOWNORMAL);

        if(rc<=32) {
            statusText_=L"Unable to launch SARA Trainer worker";
            return;
        }

        statusText_=L"Trainer worker launched for "+Widen(it->id)+
            L" | progress will update in the Training Job list";
    }


    void RunSelectedTrainerJob() {
        if(selectedModelLabJobId_.empty()) {
            statusText_=L"Select a queued training job first";
            return;
        }

        const auto jobs=runtime_->trainer.ListJobs(100);
        auto it=std::find_if(jobs.begin(),jobs.end(),[&](const auto& job){
            return job.id==selectedModelLabJobId_;
        });
        if(it==jobs.end()) {
            statusText_=L"Selected training job no longer exists";
            return;
        }
        if(it->state!="QUEUED") {
            statusText_=L"Only queued training jobs can be launched";
            return;
        }
        if((it->mode==sentinel::simulation::TrainingMode::PersonaLora ||
            it->mode==sentinel::simulation::TrainingMode::FoundationSft) &&
           !TrainerEnvironmentReady())
        {
            statusText_=L"Trainer environment is not ready; use Prepare Env before running this weight-training job";
            return;
        }

        const auto script=ExeDir()/L"trainer"/L"Run-SARA-Training.ps1";
        if(!std::filesystem::exists(script)) {
            statusText_=L"Trainer worker script is missing from this installation";
            return;
        }

        const auto dbPath=runtime_->root/"sentinel.db";
        std::wstring params=
            L"-NoProfile -ExecutionPolicy Bypass -File \""+script.wstring()+
            L"\" -Database \""+dbPath.wstring()+
            L"\" -JobId \""+Widen(it->id)+L"\"";

        const auto rc=(INT_PTR)ShellExecuteW(
            hwnd_,L"open",L"powershell.exe",params.c_str(),
            (ExeDir()/L"trainer").c_str(),SW_SHOWNORMAL);

        if(rc<=32) {
            statusText_=L"Unable to launch selected SARA Trainer job";
            return;
        }

        statusText_=L"Trainer worker launched for "+Widen(it->id);
    }

    void CancelSelectedTrainerJob() {
        if(selectedModelLabJobId_.empty()) {
            statusText_=L"Select a queued training job first";
            return;
        }
        if(runtime_->trainer.CancelQueuedJob(selectedModelLabJobId_)) {
            statusText_=L"Queued training job cancelled before worker launch";
        } else {
            statusText_=L"Only queued jobs can be cancelled from SARA";
        }
    }

    void RetrySelectedTrainerJob() {
        if(selectedModelLabJobId_.empty()) {
            statusText_=L"Select a failed or cancelled training job first";
            return;
        }
        if(runtime_->trainer.RetryJob(selectedModelLabJobId_)) {
            statusText_=L"Training job reset and returned to the queue";
        } else {
            statusText_=L"Only failed or cancelled jobs can be retried";
        }
    }


    void RecoverStaleTrainerJobs() {
        try {
            const auto recovered=runtime_->trainer.RecoverStaleRunningJobs(3);
            statusText_=recovered
                ? std::to_wstring(recovered)+L" stale Trainer job(s) marked failed and made retryable"
                : L"No stale Trainer jobs found; healthy running jobs were left untouched";
        } catch(const std::exception& e) {
            statusText_=L"Stale Trainer job recovery failed: "+Widen(e.what());
        }
    }


    void ApplyBehaviorProfileToEditors(const std::string& result) {
        auto apply=[&](HWND combo,const std::string& key){
            const auto value=ProfileLineValue(result,key);
            if(value.empty()) return;
            SendMessageW(combo,CB_SETCURSEL,(WPARAM)FindComboText(combo,value),0);
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
    }

    std::string TrainerBehaviorPreviewText(const std::string& result) const {
        auto field=[&](const char* key,const char* fallback){
            auto value=ProfileLineValue(result,key);
            return value.empty()?std::string(fallback):value;
        };
        return
            "Preview only - nothing has been applied yet. "
            "I would tune this persona toward personality "+field("PERSONALITY","unchanged")+
            ", social style "+field("SOCIAL_STYLE","unchanged")+
            ", confidence "+field("CONFIDENCE","unchanged")+
            ", writing "+field("WRITING_STYLE","unchanged")+
            ", communication "+field("COMMUNICATION","unchanged")+
            ", slang "+field("SLANG","unchanged")+
            ", grammar "+field("GRAMMAR","unchanged")+
            ", typos "+field("TYPOS","unchanged")+
            ", and emoji use "+field("EMOJI","unchanged")+
            ". Use Apply Preview if this interpretation is correct.";
    }

    void SendTrainerConversationMessage() {
        const auto instruction=Narrow(EditText(trainerInstructionEdit_));
        if(instruction.empty()) {
            statusText_=L"Enter a trainer message first";
            return;
        }

        const auto mode=SelectedTrainingMode();
        try {
            auto session=runtime_->trainer.EnsureDialogueSession(
                simSettings_.persona.name,mode);
            runtime_->trainer.AppendDialogueTurn(
                session.id,"OPERATOR",instruction);

            std::string reply;
            std::string payload;

            if(mode==sentinel::simulation::TrainingMode::Behavior) {
                if(!model_) {
                    reply="I saved the instruction, but no model is available to build a behavior preview.";
                } else {
                    sentinel::simulation::ModelContext ctx=simContext_;
                    ctx.personaSummary=BuildPersonaSummary();
                    const std::string trainingBackground=
                        simSettings_.persona.background+
                        "\nExisting persona: "+BuildPersonaSummary()+
                        "\nTrainer instruction: "+instruction+
                        "\nInterpret this as a proposed profile change only. Do not apply it automatically.";
                    payload=model_->GenerateBehaviorProfile(
                        simSettings_.persona.age,trainingBackground,ctx);
                    reply=TrainerBehaviorPreviewText(payload);
                }
            }
            else if(mode==sentinel::simulation::TrainingMode::Correction) {
                auto staged=runtime_->trainingReviews.StageLatestReply(currentConversationId_);
                if(staged) {
                    payload=staged->id;
                    reply=
                        "I captured that correction and staged the latest Simulation reply as review item "+
                        staged->id+
                        ". The correction remains reviewable; it does not change live model weights.";
                } else {
                    reply=
                        "I saved the correction note in this Trainer conversation. "
                        "There is no recent Simulation reply available to stage yet.";
                }
            }
            else if(mode==sentinel::simulation::TrainingMode::PersonaLora) {
                reply=
                    "I saved this as a Persona LoRA training goal for "+simSettings_.persona.name+
                    ". It will not alter the active adapter until a reviewed dataset is queued, trained, evaluated, and activated.";
            }
            else if(mode==sentinel::simulation::TrainingMode::FoundationSft) {
                reply=
                    "I saved this as a Foundation Fork training goal. "
                    "Use Advanced Setup for the trainable source/dataset, then Queue Mode. "
                    "The current active foundation remains unchanged until later approval and activation.";
            }
            else {
                reply=
                    "I saved this Evaluation / Test instruction. "
                    "Use the Model Lab Evaluation workspace to run scored tests and comparisons. "
                    "This mode does not modify model weights or queue a training job.";
            }

            runtime_->trainer.AppendDialogueTurn(
                session.id,"SARA",reply,payload);
            SetWindowTextW(trainerInstructionEdit_,L"");
            statusText_=L"SARA Trainer replied in "+TrainerModeDisplayName(mode)+L" mode";
        } catch(const std::exception& e) {
            statusText_=L"Trainer conversation failed: "+Widen(e.what());
        }
    }

    void ApplyLatestTrainerBehaviorPreview() {
        if(SelectedTrainingMode()!=sentinel::simulation::TrainingMode::Behavior) {
            statusText_=L"Apply Preview is available only in Behavior mode";
            return;
        }

        try {
            auto session=runtime_->trainer.ActiveDialogueSession(
                simSettings_.persona.name,
                sentinel::simulation::TrainingMode::Behavior);
            if(!session) {
                statusText_=L"No Behavior trainer conversation exists yet";
                return;
            }

            auto turns=runtime_->trainer.ListDialogueTurns(session->id,50);
            auto it=std::find_if(turns.rbegin(),turns.rend(),[](const auto& turn){
                return turn.role=="SARA" && !turn.payload.empty() && !turn.applied;
            });
            if(it==turns.rend()) {
                statusText_=L"No unapplied Behavior preview is waiting";
                return;
            }

            ApplyBehaviorProfileToEditors(it->payload);
            SaveProfileEditors(false);
            runtime_->trainer.MarkDialogueTurnApplied(it->id);
            runtime_->trainer.AppendDialogueTurn(
                session->id,"SYSTEM",
                "Applied the reviewed Behavior preview to the saved persona profile.");
            statusText_=L"Reviewed Behavior preview applied to "+Widen(simSettings_.persona.name);
        } catch(const std::exception& e) {
            statusText_=L"Behavior preview could not be applied: "+Widen(e.what());
        }
    }

    void StartNewTrainerConversation() {
        try {
            const auto mode=SelectedTrainingMode();
            const auto session=runtime_->trainer.NewDialogueSession(
                simSettings_.persona.name,mode);
            SetWindowTextW(trainerInstructionEdit_,L"");
            statusText_=
                L"New "+TrainerModeDisplayName(mode)+
                L" trainer session started; prior session preserved";
        } catch(const std::exception& e) {
            statusText_=L"Could not start a new Trainer conversation: "+Widen(e.what());
        }
    }


    bool WriteActiveRuntimeConfig(
        const std::filesystem::path& modelPath,
        const std::filesystem::path& loraPath,
        const std::string& alias)
    {
        try {
            std::filesystem::create_directories(runtime_->root);
            std::ofstream out(runtime_->root/"active-runtime.ini",std::ios::trunc);
            if(!out) return false;
            out<<"model="<<modelPath.string()<<"\n";
            out<<"lora="<<loraPath.string()<<"\n";
            out<<"alias="<<alias<<"\n";
            return true;
        } catch(...) {
            return false;
        }
    }

    void ApplyPersonaRuntimeBinding() {
        const auto defaultModel=ExeDir()/L"ai"/L"models"/L"Qwen3.5-9B-Q4_K_M.gguf";
        std::filesystem::path modelPath=defaultModel;
        std::filesystem::path loraPath;
        std::string alias="sentinel-chat";

        auto binding=runtime_->trainer.ResolvePersonaLora(simSettings_.persona.name);
        if(binding) {
            auto foundation=runtime_->trainer.GetFoundation(binding->foundationId);
            if(foundation && !foundation->runtimeGgufPath.empty())
                modelPath=std::filesystem::path(foundation->runtimeGgufPath);
            loraPath=std::filesystem::path(binding->loraPath);
            alias="sara-"+simSettings_.persona.name;
            std::replace(alias.begin(),alias.end(),' ','-');
            trainerRuntimeStatus_=L"Persona LoRA: "+Widen(binding->loraName);
        } else {
            const auto foundations=runtime_->trainer.ListFoundations();
            auto active=std::find_if(foundations.begin(),foundations.end(),[](const auto& item){
                return item.status=="ACTIVE";
            });
            if(active!=foundations.end() && !active->runtimeGgufPath.empty()) {
                modelPath=std::filesystem::path(active->runtimeGgufPath);
                alias="sara-foundation-"+std::to_string(active->version);
                trainerRuntimeStatus_=L"Foundation: "+Widen(active->name)+
                    L" v"+std::to_wstring(active->version);
            } else {
                trainerRuntimeStatus_=L"Protected SARA base foundation";
            }
        }

        if(!WriteActiveRuntimeConfig(modelPath,loraPath,alias)) {
            trainerRuntimeStatus_=L"Could not write active runtime configuration";
            return;
        }

        // Only restart when the configured files are actually present. This
        // lets investigators define future LoRA bindings before training has
        // produced the adapter file.
        if(!std::filesystem::exists(modelPath) ||
           (!loraPath.empty() && !std::filesystem::exists(loraPath))) {
            if(!loraPath.empty())
                trainerRuntimeStatus_+=L" | adapter file not created yet";
            return;
        }

        std::wstring failure;
        if(StartBundledAiService(&failure,true)) {
            try {
                auto models=sentinel::simulation::DiscoverOpenAICompatibleModels(simSettings_.endpoint);
                std::wstring connectFailure;
                ConnectDiscoveredLocalModel(models,&connectFailure);
            } catch(...) {}
        } else {
            trainerRuntimeStatus_=L"Persona runtime switch failed: "+failure;
        }
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
        ApplyPersonaRuntimeBinding();
        currentConversationId_.clear();
        ResumeOrCreateConversation();
        statusText_=L"Loaded persona profile: "+Widen(name)+L" | "+trainerRuntimeStatus_;
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

            ApplyBehaviorProfileToEditors(result);

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

    void SaveProfileEditors(bool showConfirmation=true) {
        try {
            simSettings_.persona.name=Narrow(EditText(personaNameEdit_));
            {
                int ageSel=(int)SendMessageW(personaAgeCombo_,CB_GETCURSEL,0,0);
                simSettings_.persona.age=(ageSel==CB_ERR)?13:(8+ageSel);
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
            statusText_=L"Persona saved: "+Widen(simSettings_.persona.name);
            if(showConfirmation) {
                const std::wstring msg=
                    L"Persona '"+Widen(simSettings_.persona.name)+
                    L"' has been saved successfully.";
                MessageBoxW(hwnd_,msg.c_str(),L"Persona Saved",MB_OK|MB_ICONINFORMATION);
            }
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
        if(MessageBoxW(hwnd_,L"Delete this persona image from SARA?",L"Delete Persona Media",MB_YESNO|MB_ICONQUESTION)!=IDYES)
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

    bool PersonaMediaWasAlreadySent(const PersonaMediaItem& item) const {
        sqlite3_stmt* stmt{};
        bool sent=false;
        const char* sql=
            "SELECT 1 FROM conversation_media "
            "WHERE speaker=? AND (stored_path=? OR sha256=?) LIMIT 1";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&stmt,nullptr)==SQLITE_OK) {
            sqlite3_bind_int(stmt,1,(int)sentinel::simulation::ChatTurn::Speaker::SyntheticSubject);
            sqlite3_bind_text(stmt,2,item.storedPath.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt,3,item.sha256.c_str(),-1,SQLITE_TRANSIENT);
            sent=sqlite3_step(stmt)==SQLITE_ROW;
        }
        sqlite3_finalize(stmt);
        return sent;
    }

    int FindApprovedPersonaMedia() const {
        // Never reuse a persona image after it has been sent once.
        if(selectedPersonaMedia_>=0 && selectedPersonaMedia_<(int)personaMedia_.size()) {
            const auto& selected=personaMedia_[(size_t)selectedPersonaMedia_];
            if(selected.approved && !PersonaMediaWasAlreadySent(selected))
                return selectedPersonaMedia_;
        }

        for(size_t i=0;i<personaMedia_.size();++i) {
            const auto& item=personaMedia_[i];
            if(item.approved && !PersonaMediaWasAlreadySent(item))
                return (int)i;
        }
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
            lower.find("do you have a pic")!=std::string::npos ||
            lower.find("do you have any pic")!=std::string::npos ||
            lower.find("do you have a picture")!=std::string::npos ||
            lower.find("do you have any picture")!=std::string::npos ||
            lower.find("do you have a photo")!=std::string::npos ||
            lower.find("do you have any photo")!=std::string::npos ||
            lower.find("got any pics")!=std::string::npos ||
            lower.find("got a pic")!=std::string::npos ||
            lower.find("got any pictures")!=std::string::npos ||
            lower.find("selfie")!=std::string::npos;
        if(!wantsImage) return false;

        const char* blocked[]={"nude","naked","underwear","panties","bra ","lingerie","sexy pic","explicit","boob","breast","vagina","penis","dick"};
        for(auto* term:blocked) if(lower.find(term)!=std::string::npos) return false;
        return true;
    }

    void StageApprovedPersonaMedia(const std::string& triggerText) {
        simPendingPersonaMediaIndex_=-1;
        if(!LooksLikeBenignPictureRequest(triggerText)) return;
        const int idx=FindApprovedPersonaMedia();
        if(idx<0) {
            statusText_=L"Picture requested, but no unused approved persona image is available";
            return;
        }

        simPendingPersonaMediaIndex_=idx;
        if(!simContext_.recalledMemory.empty()) simContext_.recalledMemory+="\n";
        simContext_.recalledMemory+=
            "MEDIA FACT: This persona has an approved benign image available and it is being sent in this conversation. "
            "Do not claim that you have no pictures or cannot send one. Respond naturally and briefly around the image.";
        statusText_=L"Approved persona image ready to send";
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
            auto conversations=runtime_->conversationMemory.ListForPersona(simSettings_.persona.name,50);
            if(conversations.empty()) {
                sentinel::simulation::ModelContext legacy=simContext_;
                if(sentinel::simulation::LoadSession(runtime_->root/"simulation-session.tsv",legacy)
                    && !legacy.history.empty()) {
                    const std::string title="Recovered previous conversation";
                    currentConversationId_=runtime_->conversationMemory.StartConversation(
                        title,simSettings_.persona.name,legacy.personaSummary,legacy.scenario);
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
            title,simSettings_.persona.name,simContext_.personaSummary,simContext_.scenario);
        currentConversationTitle_=Widen(title);
        archiveCursor_=0;

        simSuggestion_=L"No suggestion generated yet";
        simBotTyping_=false;
        simReplyPending_=false;
        simPendingMessage_.clear();
        simPreparedReply_.clear();
        simPendingPersonaMediaIndex_=-1;
        KillTimer(hwnd_,kSimTypingStartTimer);
        KillTimer(hwnd_,kSimReplyTimer);
        KillTimer(hwnd_,kSimEngagementTimer);
        simInitiativeSent_=false;
        if(chatEdit_) {
            SetWindowTextW(chatEdit_,L"");
            if(page_==Page::Simulation) {
                SetFocus(chatEdit_);
                SendMessageW(chatEdit_,EM_SETSEL,0,0);
            }
        }
        ScrollSimulationToBottom();
        statusText_=L"New persistent conversation started";
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void LoadPreviousConversation() {
        try {
            auto conversations=runtime_->conversationMemory.ListForPersona(simSettings_.persona.name,50);
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
                SendMessageW(chatEdit_,EM_SETSEL,0,0);
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
            utf8,currentConversationId_,simSettings_.persona.name,12);
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
        const auto questionHistory=runtime_->conversationMemory.RecallQuestionHistory(
            currentConversationId_,16);
        if(!questionHistory.empty()) {
            if(!simContext_.recalledMemory.empty()) simContext_.recalledMemory+="\n";
            simContext_.recalledMemory+=questionHistory;
        }
        const auto learnedNotes=runtime_->conversationMemory.RecallLearnedPersonaNotes(
            simSettings_.persona.name,16);
        if(!learnedNotes.empty()) {
            if(!simContext_.recalledMemory.empty()) simContext_.recalledMemory+="\n";
            simContext_.recalledMemory+=learnedNotes;
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
        simMatchedRule_.reset();

        if(auto rule=FindPersonaResponseRule(utf8)) {
            const std::string variantBasis=
                currentConversationId_+"|"+utf8+"|"+
                std::to_string(rule->id)+"|"+
                std::to_string(simContext_.history.size());
            const auto selectedVariant=sentinel::simulation::SelectResponseRuleVariant(
                rule->response,variantBasis);
            const auto policy=sentinel::simulation::EvaluateSimulationPolicy(
                simSettings_.ageState,selectedVariant);
            if(policy.allowed) {
                simPreparedFromRule_=true;
                simMatchedRule_=*rule;
                simMatchedRule_->response=selectedVariant;
                simRuleMeaning_=selectedVariant;
                simRuleResponseMode_=rule->responseMode;
                if(rule->responseMode=="exact")
                    simPreparedReply_=selectedVariant;
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
        SendMessageW(chatEdit_,EM_SETSEL,0,0);
        PositionVisibleChatCaret(chatEdit_);
        InvalidateRect(chatEdit_,nullptr,FALSE);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }


    void OpenEmojiPicker() {
        if(!chatEdit_) return;
        SetFocus(chatEdit_);

        INPUT inputs[4]{};
        inputs[0].type=INPUT_KEYBOARD;
        inputs[0].ki.wVk=VK_LWIN;
        inputs[1].type=INPUT_KEYBOARD;
        inputs[1].ki.wVk=VK_OEM_PERIOD;
        inputs[2].type=INPUT_KEYBOARD;
        inputs[2].ki.wVk=VK_OEM_PERIOD;
        inputs[2].ki.dwFlags=KEYEVENTF_KEYUP;
        inputs[3].type=INPUT_KEYBOARD;
        inputs[3].ki.wVk=VK_LWIN;
        inputs[3].ki.dwFlags=KEYEVENTF_KEYUP;
        SendInput(4,inputs,sizeof(INPUT));
        statusText_=L"Emoji picker opened";
    }

    static std::string ImageMarker(long long id) {
        return "[[IMAGE:"+std::to_string(id)+"]]";
    }

    static std::optional<long long> ParseImageMarker(const std::string& text) {
        if(text.rfind("[[IMAGE:",0)!=0 || text.size()<11) return std::nullopt;
        auto end=text.find("]]",8);
        if(end==std::string::npos) return std::nullopt;
        try { return std::stoll(text.substr(8,end-8)); }
        catch(...) { return std::nullopt; }
    }

    struct ConversationImageView {
        long long id{};
        std::filesystem::path path;
        std::string originalName;
        std::string caption;
    };

    std::optional<ConversationImageView> GetConversationImage(long long id) const {
        sqlite3_stmt* s{};
        const char* sql=
            "SELECT id,stored_path,original_name,caption FROM conversation_media "
            "WHERE id=? AND conversation_id=? LIMIT 1";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)
            return std::nullopt;
        sqlite3_bind_int64(s,1,id);
        sqlite3_bind_text(s,2,currentConversationId_.c_str(),-1,SQLITE_TRANSIENT);
        std::optional<ConversationImageView> out;
        if(sqlite3_step(s)==SQLITE_ROW) {
            ConversationImageView v;
            v.id=sqlite3_column_int64(s,0);
            auto col=[&](int i)->std::string{
                const auto* p=(const char*)sqlite3_column_text(s,i);
                return p?p:"";
            };
            v.path=std::filesystem::path(Widen(col(1)));
            v.originalName=col(2);
            v.caption=col(3);
            out=v;
        }
        sqlite3_finalize(s);
        return out;
    }

    long long StoreConversationImage(
        sentinel::simulation::ChatTurn::Speaker speaker,
        const std::filesystem::path& source,
        std::string_view caption)
    {
        if(currentConversationId_.empty()) ResetSimulation();
        if(!std::filesystem::exists(source)) return 0;

        auto ext=source.extension().wstring();
        auto hash=runtime_->hash.Sha256File(source).ToHex();
        auto mediaDir=runtime_->root/"conversation-media";
        std::filesystem::create_directories(mediaDir);
        auto stored=mediaDir/(Widen(hash.substr(0,20))+ext);
        if(!std::filesystem::exists(stored))
            std::filesystem::copy_file(source,stored,std::filesystem::copy_options::overwrite_existing);

        sqlite3_stmt* s{};
        const char* sql=
            "INSERT INTO conversation_media("
            "conversation_id,speaker,stored_path,original_name,sha256,caption"
            ") VALUES(?,?,?,?,?,?)";
        if(sqlite3_prepare_v2(runtime_->db.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)
            throw std::runtime_error("could not prepare conversation image insert");

        const auto storedUtf8=Narrow(stored.wstring());
        const auto original=Narrow(source.filename().wstring());
        sqlite3_bind_text(s,1,currentConversationId_.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_int(s,2,(int)speaker);
        sqlite3_bind_text(s,3,storedUtf8.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(s,4,original.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(s,5,hash.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(s,6,std::string(caption).c_str(),-1,SQLITE_TRANSIENT);
        if(sqlite3_step(s)!=SQLITE_DONE) {
            sqlite3_finalize(s);
            throw std::runtime_error("could not save conversation image");
        }
        const auto id=sqlite3_last_insert_rowid(runtime_->db.Handle());
        sqlite3_finalize(s);

        const auto marker=ImageMarker(id);
        simContext_.history.push_back({speaker,marker});
        runtime_->conversationMemory.Append(currentConversationId_,speaker,marker);
        sentinel::simulation::SaveSession(runtime_->root/"simulation-session.tsv",simContext_);
        ScrollSimulationToBottom();
        return id;
    }

    void AttachImageToConversation() {
        auto file=PickPersonaMediaFile();
        if(!file) return;
        if(!IsAllowedPersonaMediaExtension(*file)) {
            MessageBoxW(hwnd_,L"Choose a JPG, JPEG, PNG, WEBP, or BMP image.",
                L"Attach Image",MB_OK|MB_ICONINFORMATION);
            return;
        }
        try {
            StoreConversationImage(
                sentinel::simulation::ChatTurn::Speaker::Investigator,
                *file,
                "Image attachment");
            statusText_=L"Image attached to conversation";
            InvalidateRect(hwnd_,nullptr,FALSE);
        } catch(const std::exception& e) {
            statusText_=L"Image attachment failed: "+Widen(e.what());
        }
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
            testContext.scenario="SARA local model startup connection test";
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
        // Recovery invariant: startup must never perform a model download, runtime
        // installation, service launch, or network discovery on the UI thread.
        // Those operations can take seconds or minutes and previously trapped the
        // application behind the topmost splash screen.
        if(simSettings_.endpoint.empty())
            simSettings_.endpoint="http://127.0.0.1:1234/v1/chat/completions";
        if(simSettings_.model.empty())
            simSettings_.model="sentinel-chat";

        SetWindowTextW(modelEndpointEdit_,Widen(simSettings_.endpoint).c_str());
        SetWindowTextW(modelNameEdit_,Widen(simSettings_.model).c_str());

        // Always establish an immediately available fallback so the desktop can
        // finish App::Init and reveal the main window.
        model_=sentinel::simulation::CreateRuleBasedTestModel();

        if(IsLocalModelEndpoint(simSettings_.endpoint)) {
            if(BundledAiPrerequisitesPresent()) {
                modelStatus_=L"Local AI is installed. Use Connect/Browse Models to attach the runtime.";
                statusText_=L"Local AI ready to connect";
            } else {
                modelStatus_=
                    L"Local AI model/runtime is missing or incomplete. SARA started safely in built-in mode. "
                    L"Run Install / Repair Local AI to restore the verified local model.";
                statusText_=L"Local AI repair required";
            }
        } else {
            modelStatus_=
                L"Configured external model connection is deferred until after startup. "
                L"Use Connect/Browse Models when ready.";
            statusText_=L"Model connection deferred";
        }
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
            modelStatus_=L"AI installer launched. SARA will discover and connect the model automatically on the next start.";
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

    std::optional<sentinel::operations::SupervisorControlState> CurrentSupervisorState() const {
        if(cases_.empty() || selectedCase_>=cases_.size()) return std::nullopt;
        return runtime_->supervisorState.Get(cases_[selectedCase_].id.ToString());
    }

    bool InvestigatorTakeoverActive() const {
        const auto state=CurrentSupervisorState();
        return state && state->investigatorTakeover;
    }

    void QueueOperatorTestMessage() {
        RequestLatestSuggestionApproval();
        page_=Page::Supervisor;
    }

    void RequestLatestSuggestionApproval() {
        if(InvestigatorTakeoverActive()) {
            statusText_=L"Investigator takeover is active; AI approval requests are locked";
            return;
        }

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

        auto request=sentinel::operations::CreateApprovalRequest(
            "message:local-sim:"+text,"local-investigator");
        approvals_.push_back(request);
        runtime_->audit.Append({
            sentinel::UserId::Random(),
            sentinel::AuditAction::SupervisorApprovalRequested,
            "approval",
            request.id,
            AuditMetadata("action_sha256="+request.actionHash)
        });
        statusText_=L"Supervisor approval requested";
    }

    void ApproveFirstPending() {
        if(InvestigatorTakeoverActive()) {
            statusText_=L"Investigator takeover is active; release takeover before AI outbound is queued";
            return;
        }

        for(auto& a:approvals_) {
            if(a.status==sentinel::operations::ApprovalStatus::Pending) {
                sentinel::operations::Approve(a,"local-supervisor","Approved in SARA supervisor console");
                runtime_->audit.Append({
                    sentinel::UserId::Random(),
                    sentinel::AuditAction::SupervisorApprovalApproved,
                    "approval",
                    a.id,
                    AuditMetadata("action_sha256="+a.actionHash)
                });

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

    void RejectFirstPending() {
        for(auto& a:approvals_) {
            if(a.status!=sentinel::operations::ApprovalStatus::Pending) continue;

            sentinel::operations::Reject(a,"local-supervisor","Rejected in SARA supervisor console");
            runtime_->audit.Append({
                sentinel::UserId::Random(),
                sentinel::AuditAction::SupervisorApprovalRejected,
                "approval",
                a.id,
                AuditMetadata("action_sha256="+a.actionHash)
            });
            statusText_=L"Supervisor rejection recorded; nothing was queued";
            return;
        }
        statusText_=L"No pending approvals";
    }

    void ToggleInvestigatorTakeover() {
        if(cases_.empty() || selectedCase_>=cases_.size()) {
            statusText_=L"Open a case before changing investigator takeover state";
            return;
        }

        const auto caseId=cases_[selectedCase_].id.ToString();
        const bool activate=!InvestigatorTakeoverActive();
        const auto state=runtime_->supervisorState.SetTakeover(
            caseId,
            activate,
            "local-investigator",
            activate?"Manual investigator control activated in SARA supervisor console":"");

        runtime_->audit.Append({
            sentinel::UserId::Random(),
            activate
                ? sentinel::AuditAction::SupervisorTakeoverActivated
                : sentinel::AuditAction::SupervisorTakeoverReleased,
            "case",
            caseId,
            AuditMetadata(
                std::string("investigator_takeover=")+(state.investigatorTakeover?"1":"0"))
        });

        statusText_=activate
            ? L"Investigator takeover active; AI approval and outbound queueing are locked"
            : L"Investigator takeover released; normal supervisor approval gating restored";
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
        if(!agencyConfig_.enabled &&
           (agencyConfig_.endpoint.empty() || agencyConfig_.agencyId.empty())) {
            statusText_=L"Enter agency endpoint and agency ID before enabling configuration";
            return;
        }
        if(agencyConfig_.workstationId.empty())
            agencyConfig_.workstationId="local-workstation";

        agencyConfig_.enabled=!agencyConfig_.enabled;
        runtime_->agencyStore.SaveConfig(agencyConfig_);
        statusText_=agencyConfig_.enabled
            ? L"Agency configuration saved; network transport remains inactive"
            : L"Agency configuration disabled and saved";
    }

    void EnqueueAgencySnapshot() {
        runtime_->agencyStore.Enqueue({
            {},
            sentinel::agency::SyncItemType::AuditRecord,
            "audit-count:"+std::to_string(runtime_->AuditCount()),0,false});
        statusText_=L"Audit snapshot queued persistently for future authenticated sync";
    }

    void DrawPersona(float w,float h) {
        PageTitle(L"Personas",L"Reusable synthetic identities, behavior, rules, communication style, scenarios, media, and policy");

        const float x=kSidebar+28.0f;
        const float y=kHeader+104.0f;
        const float contentW=w-x-28.0f;

        Rounded(x,y-46,contentW,38,brush_.panel.Get(),brush_.border.Get(),8);
        TextLine(L"Saved Persona",x+12,y-42,92,28,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"persona_load",L"Load",x+contentW-266,y-42,72,28,false);
        AddButton(L"persona_delete",L"Delete",x+contentW-186,y-42,72,28,false);
        AddButton(L"persona_save",L"Save",x+contentW-106,y-42,88,28,true);

        // Professional section tabs.
        const float tabGap=8.0f;
        const float tabW=(contentW-tabGap*5)/6.0f;
        const wchar_t* labels[]={
            L"Profile",L"Bio & Home Life",L"Behavior & Speech",
            L"Scenario & Policy",L"Gallery",L"Rules & Learning"
        };
        const wchar_t* ids[]={
            L"persona_tab_profile",L"persona_tab_bio",L"persona_tab_behavior",
            L"persona_tab_scenario",L"persona_tab_gallery",L"persona_tab_rules"
        };
        for(int i=0;i<6;i++) {
            const float tx=x+i*(tabW+tabGap);
            const bool active=(int)personaTab_==i;
            Rounded(tx,y,tabW,42,active?brush_.panel2.Get():brush_.sidebar.Get(),active?brush_.cyan.Get():brush_.border.Get(),8);
            TextLine(labels[i],tx+8,y+3,tabW-16,34,smallFmt_.Get(),active?brush_.cyan.Get():brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
            buttons_.push_back({{tx,y,tx+tabW,y+42},ids[i]});
        }

        const float py=y+56;
        const float panelH=std::max(520.0f,h-py-28.0f);
        Rounded(x,py,contentW,panelH,brush_.panel.Get(),brush_.border.Get(),10);

        if(personaTab_==PersonaTab::Profile) {
            TextLine(L"Core Profile",x+22,py+16,240,32,h1Fmt_.Get(),brush_.text.Get());
            TextLine(L"Basic identity and everyday context used across every conversation.",x+270,py+20,contentW-292,24,tinyFmt_.Get(),brush_.muted.Get());

            const float left=x+34, labelW=118, fieldX=left+labelW;
            const float right=x+contentW*0.52f, rightField=right+126;
            float row=py+74;

            TextLine(L"Name",left,row,100,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Age",right,row,100,30,tinyFmt_.Get(),brush_.muted.Get()); row+=52;

            TextLine(L"Gender",left,row,100,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Pronouns",right,row,100,30,tinyFmt_.Get(),brush_.muted.Get()); row+=52;

            TextLine(L"Location",left,row,100,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Occupation",right,row,100,30,tinyFmt_.Get(),brush_.muted.Get()); row+=52;

            TextLine(L"Education",left,row,100,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Relationship",right,row,100,30,tinyFmt_.Get(),brush_.muted.Get()); row+=52;

            TextLine(L"Family",left,row,100,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Interests",right,row,100,30,tinyFmt_.Get(),brush_.muted.Get());

            Rounded(x+22,py+350,contentW-44,112,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"Age Range",x+40,py+364,120,24,smallFmt_.Get(),brush_.cyan.Get());
            TextLine(L"Personas are restricted to ages 8 through 17. Speech and reasoning are automatically age-banded.",x+40,py+393,contentW-80,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"8-10: child | 11-13: preteen | 14-15: younger teen | 16-17: older teen",x+40,py+422,contentW-80,24,tinyFmt_.Get(),brush_.muted.Get());
        }
        else if(personaTab_==PersonaTab::Bio) {
            TextLine(L"Bio, Background & Home Life",x+22,py+16,360,32,h1Fmt_.Get(),brush_.text.Get());
            TextLine(L"This long-form context is fed directly into the persona model and becomes part of its conversational grounding.",x+22,py+54,contentW-44,40,smallFmt_.Get(),brush_.muted.Get());

            Rounded(x+22,py+104,contentW-44,panelH-154,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"Useful details: home life, family dynamics, school routines, friends, habits, memories, likes/dislikes, hobbies, daily schedule, neighborhood context, and ordinary history.",
                x+40,py+118,contentW-80,48,tinyFmt_.Get(),brush_.muted.Get());

            AddButton(L"persona_generate_behavior",L"Generate Behavior from Bio",x+contentW-246,py+18,224,34,false);
        }
        else if(personaTab_==PersonaTab::Behavior) {
            TextLine(L"Behavior & Speech",x+22,py+16,280,32,h1Fmt_.Get(),brush_.text.Get());
            TextLine(L"Fine-tune how the persona thinks and writes without changing its core identity.",x+310,py+20,contentW-332,24,tinyFmt_.Get(),brush_.muted.Get());

            const float col1=x+34, col2=x+contentW*0.50f+10;
            const float field1=col1+150, field2=col2+150;
            float row=py+78;
            TextLine(L"Personality",col1,row,130,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Social style",col2,row,130,30,tinyFmt_.Get(),brush_.muted.Get()); row+=54;
            TextLine(L"Confidence",col1,row,130,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Writing style",col2,row,130,30,tinyFmt_.Get(),brush_.muted.Get()); row+=54;
            TextLine(L"Communication",col1,row,130,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Cognitive",col2,row,130,30,tinyFmt_.Get(),brush_.muted.Get()); row+=54;
            TextLine(L"Slang",col1,row,130,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Grammar",col2,row,130,30,tinyFmt_.Get(),brush_.muted.Get()); row+=54;
            TextLine(L"Typos",col1,row,130,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Emoji",col2,row,130,30,tinyFmt_.Get(),brush_.muted.Get());

            Rounded(x+22,py+370,contentW-44,100,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"Age-aware speech is mandatory",x+40,py+384,240,24,smallFmt_.Get(),brush_.cyan.Get());
            TextLine(L"These controls modify the age band; they do not override it. An 8-year-old cannot be made to sound like an adult by selecting Advanced.",x+40,py+413,contentW-80,42,tinyFmt_.Get(),brush_.text.Get());
        }
        else if(personaTab_==PersonaTab::Scenario) {
            TextLine(L"Scenario, Policy & Pacing",x+22,py+16,320,32,h1Fmt_.Get(),brush_.text.Get());
            TextLine(L"Conversation scenario, age-state policy, and human-like response timing.",x+350,py+20,contentW-372,24,tinyFmt_.Get(),brush_.muted.Get());

            const float left=x+34;
            float row=py+82;
            TextLine(L"Scenario",left,row,110,30,tinyFmt_.Get(),brush_.muted.Get()); row+=58;
            TextLine(L"Objective",left,row,110,30,tinyFmt_.Get(),brush_.muted.Get()); row+=58;
            TextLine(L"Seed",left,row,110,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Age state",x+contentW*0.42f,row,100,30,tinyFmt_.Get(),brush_.muted.Get()); row+=58;
            TextLine(L"Start delay min",left,row,110,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Start delay max",x+contentW*0.42f,row,110,30,tinyFmt_.Get(),brush_.muted.Get());

            Rounded(x+22,py+360,contentW-44,100,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"Policy Status",x+40,py+374,120,24,smallFmt_.Get(),brush_.cyan.Get());
            TextLine(policyStatus_,x+40,py+405,contentW-80,36,smallFmt_.Get(),brush_.text.Get());
        }
        else if(personaTab_==PersonaTab::Gallery) {
            TextLine(L"Profile Gallery",x+22,py+16,240,32,h1Fmt_.Get(),brush_.text.Get());
            TextLine(L"Approved images can be sent in chat. SARA never sends the same image twice.",x+270,py+20,contentW-292,24,tinyFmt_.Get(),brush_.muted.Get());

            AddButton(L"media_import",L"Import Picture",x+22,py+60,126,34,true);
            AddButton(L"media_approve",L"Approve / Revoke",x+158,py+60,146,34,false);
            AddButton(L"media_delete",L"Delete",x+314,py+60,88,34,false);

            const float gridY=py+116;
            const float cardW=172.0f;
            const float cardH=132.0f;
            const float gap=14.0f;
            const int cols=std::max(1,(int)((contentW-44+gap)/(cardW+gap)));

            if(personaMedia_.empty()) {
                Rounded(x+22,gridY,contentW-44,120,brush_.sidebar.Get(),brush_.border.Get(),8);
                TextLine(L"No gallery images yet.",x+40,gridY+24,contentW-80,28,bodyFmt_.Get(),brush_.muted.Get());
                TextLine(L"Import benign persona images, review them, then approve the ones SARA may use in conversation.",x+40,gridY+58,contentW-80,40,smallFmt_.Get(),brush_.muted.Get());
            } else {
                const size_t maxCards=std::min<size_t>(personaMedia_.size(),(size_t)cols*3);
                for(size_t i=0;i<maxCards;i++) {
                    const int col=(int)i%cols;
                    const int row=(int)i/cols;
                    const float cx=x+22+col*(cardW+gap);
                    const float cy=gridY+row*(cardH+gap);
                    const auto& item=personaMedia_[i];
                    const bool selected=(int)i==selectedPersonaMedia_;

                    Rounded(cx,cy,cardW,cardH,
                        selected?brush_.panel2.Get():brush_.sidebar.Get(),
                        selected?brush_.cyan.Get():brush_.border.Get(),8);

                    auto bitmap=LoadD2DBitmap(std::filesystem::path(Widen(item.storedPath)));
                    if(bitmap) {
                        const auto sz=bitmap->GetSize();
                        const float maxW=cardW-16, maxH=86.0f;
                        const float scale=std::min(maxW/std::max(1.0f,sz.width),maxH/std::max(1.0f,sz.height));
                        const float dw=sz.width*scale, dh=sz.height*scale;
                        const float dx=cx+(cardW-dw)/2.0f;
                        target_->DrawBitmap(bitmap.Get(),D2D1::RectF(dx,cy+8,dx+dw,cy+8+dh),1.0f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    }

                    TextLine(item.approved?L"APPROVED":L"REVIEW",
                        cx+8,cy+96,cardW-16,18,tinyFmt_.Get(),
                        item.approved?brush_.green.Get():brush_.yellow.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);

                    std::wstring name=Widen(item.originalName);
                    if(name.size()>22) name=name.substr(0,19)+L"...";
                    TextLine(name,cx+8,cy+112,cardW-16,16,tinyFmt_.Get(),brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
                    buttons_.push_back({{cx,cy,cx+cardW,cy+cardH},L"media:"+std::to_wstring(i)});
                }
            }
        }
        else if(personaTab_==PersonaTab::Rules) {
            const auto rules=PersonaResponseRules(50);
            size_t enabledRules=0;
            for(const auto& rule:rules) if(rule.enabled) ++enabledRules;

            TextLine(L"Rules & Learning",x+22,py+16,260,32,h1Fmt_.Get(),brush_.text.Get());
            TextLine(
                L"Saved investigator-approved trigger rules are persona-scoped and evaluated before normal model generation.",
                x+290,py+20,contentW-312,28,tinyFmt_.Get(),brush_.muted.Get());

            const auto learnedNotes=PersonaLearnedNotes(50);
            AddButton(
                L"learning_toggle",
                simSettings_.learningMode?L"Learning ON":L"Learning OFF",
                x+contentW-126,py+48,110,28,simSettings_.learningMode);
            AddButton(
                L"learning_notes_toggle",
                personaShowLearnedNotes_?L"Show Rules":L"Notes ("+std::to_wstring(learnedNotes.size())+L")",
                x+contentW-246,py+48,112,28,personaShowLearnedNotes_);

            TextLine(L"Trigger",x+22,py+62,58,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Responses (use || for alternates)",x+contentW*0.50f+18,py+62,190,18,tinyFmt_.Get(),brush_.muted.Get());

            const float actionY=py+116.0f;
            AddButton(
                L"rule_wording_toggle",
                responseRuleExactWording_?L"Exact words":L"Persona voice",
                x+22,actionY,112,28,false);
            AddButton(
                L"rule_terminal_toggle",
                responseRuleTerminal_?L"Terminal":L"Continue",
                x+142,actionY,88,28,false);
            AddButton(L"rule_add_smart",L"Smart",x+238,actionY,68,28,true);
            AddButton(L"rule_add_contains",L"Contains",x+314,actionY,78,28,false);
            AddButton(L"rule_add_exact",L"Exact",x+400,actionY,64,28,false);
            AddButton(L"rule_test",L"Test",x+472,actionY,54,28,false);
            AddButton(L"rule_clear",L"Clear",x+534,actionY,62,28,false);

            TextLine(
                std::to_wstring(enabledRules)+L" enabled / "+
                std::to_wstring(rules.size())+L" saved for "+Widen(simSettings_.persona.name),
                x+22,py+154,contentW-44,20,tinyFmt_.Get(),brush_.cyan.Get());

            const float listY=py+180.0f;
            const float rowH=64.0f;
            if(personaShowLearnedNotes_) {
                TextLine(
                    L"Learned Continuity - benign first-person details remembered by this persona",
                    x+22,listY-24,contentW-44,18,tinyFmt_.Get(),brush_.cyan.Get());
                if(learnedNotes.empty()) {
                    Rounded(x+18,listY,contentW-36,82,brush_.sidebar.Get(),brush_.border.Get(),8);
                    TextLine(L"No learned continuity notes for this persona yet.",
                        x+34,listY+13,contentW-68,24,smallFmt_.Get(),brush_.muted.Get());
                    TextLine(
                        L"Learning Mode will retain deduplicated first-person persona details after they appear naturally in conversation.",
                        x+34,listY+41,contentW-68,28,tinyFmt_.Get(),brush_.muted.Get());
                } else {
                    const size_t visible=std::min<size_t>(learnedNotes.size(),5);
                    for(size_t i=0;i<visible;i++) {
                        const auto& note=learnedNotes[i];
                        const float ry=listY+i*(rowH+6.0f);
                        Rounded(x+18,ry,contentW-36,rowH,brush_.sidebar.Get(),brush_.border.Get(),8);

                        TextLine(
                            L"#"+std::to_wstring(note.id)+L" | "+
                            (note.sourceKind=="proactive_persona_claim"?L"PROACTIVE":L"REACTIVE"),
                            x+30,ry+5,contentW-150,18,tinyFmt_.Get(),brush_.cyan.Get());

                        std::wstring value=Widen(note.text);
                        if(value.size()>150) value=value.substr(0,147)+L"...";
                        Text(value,x+30,ry+24,contentW-140,34,tinyFmt_.Get(),brush_.text.Get());

                        AddButton(
                            L"learning_note_delete:"+std::to_wstring(note.id),
                            L"Delete",x+contentW-82,ry+18,64,28,false);
                    }
                }
            } else {
            if(rules.empty()) {
                Rounded(x+18,listY,contentW-36,82,brush_.sidebar.Get(),brush_.border.Get(),8);
                TextLine(L"No saved response rules for this persona.",x+34,listY+13,contentW-68,24,smallFmt_.Get(),brush_.muted.Get());
                TextLine(
                    L"Enter a trigger and approved response above, choose the wording mode, then add Smart, Contains, or Exact.",
                    x+34,listY+41,contentW-68,28,tinyFmt_.Get(),brush_.muted.Get());
            } else {
                const size_t visible=std::min<size_t>(rules.size(),4);
                for(size_t i=0;i<visible;i++) {
                    const auto& rule=rules[i];
                    const float ry=listY+i*(rowH+6.0f);
                    Rounded(x+18,ry,contentW-36,rowH,brush_.sidebar.Get(),brush_.border.Get(),8);

                    const std::wstring typeLabel=
                        rule.matchType=="exact"?L"EXACT":
                        rule.matchType=="contains"?L"CONTAINS":L"SMART";
                    const std::wstring modeLabel=
                        rule.responseMode=="exact"?L"EXACT":L"PERSONA";
                    const std::wstring flowLabel=rule.terminal?L"STOP":L"CONTINUE";

                    const auto hitCount=runtime_->responseRuleMatches.CountForRule(
                        simSettings_.persona.name,rule.id);
                    TextLine(
                        L"#"+std::to_wstring(rule.id)+L"  "+typeLabel+L" | "+modeLabel+
                        L" | "+flowLabel+L" | P"+std::to_wstring(rule.priority)+
                        L" | "+std::to_wstring(hitCount)+L" hit"+(hitCount==1?L"":L"s"),
                        x+30,ry+5,contentW-260,18,tinyFmt_.Get(),
                        rule.enabled?brush_.cyan.Get():brush_.muted.Get());

                    std::wstring trigger=Widen(rule.trigger);
                    std::wstring response=Widen(rule.response);
                    if(trigger.size()>56) trigger=trigger.substr(0,53)+L"...";
                    if(response.size()>66) response=response.substr(0,63)+L"...";
                    TextLine(L"Trigger: "+trigger,x+30,ry+24,contentW-260,18,tinyFmt_.Get(),brush_.text.Get());
                    TextLine(L"Response: "+response,x+30,ry+42,contentW-260,18,tinyFmt_.Get(),brush_.muted.Get());

                    AddButton(
                        L"rule_open:"+std::to_wstring(rule.id),
                        L"Open",x+contentW-214,ry+18,50,28,false);
                    AddButton(
                        L"rule_toggle:"+std::to_wstring(rule.id),
                        rule.enabled?L"Disable":L"Enable",
                        x+contentW-156,ry+18,72,28,rule.enabled);
                    AddButton(
                        L"rule_delete:"+std::to_wstring(rule.id),
                        L"Delete",x+contentW-76,ry+18,58,28,false);
                }

                if(rules.size()>visible) {
                    TextLine(
                        L"Showing newest "+std::to_wstring(visible)+L" of "+
                        std::to_wstring(rules.size())+L" saved rules.",
                        x+22,listY+visible*(rowH+6.0f)+4,contentW-44,18,tinyFmt_.Get(),brush_.muted.Get());
                }
            }
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

            auto baseContext=simContext_;
            baseContext.history.clear();
            baseContext.recalledMemory.clear();
            baseContext.scenario="SARA Model Lab multidimensional candidate evaluation";
            baseContext.personaSummary=BuildPersonaSummary();

            auto generate=[&](const std::string& prompt,sentinel::simulation::ModelContext ctx=sentinel::simulation::ModelContext{}) {
                if(ctx.personaSummary.empty()) ctx=baseContext;
                return candidate->GenerateSyntheticReply(prompt,ctx);
            };

            std::vector<std::string> personaResponses;
            std::vector<std::string> styleResponses;
            std::vector<std::string> policyResponses;
            std::vector<sentinel::simulation::EvaluationCaseResult> caseResults;
            std::string memoryReply;
            std::string memoryExpected="cobalt";

            auto configured=[](const std::string& value) {
                auto lower=value;
                std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char ch){return (char)std::tolower(ch);});
                return !value.empty() && lower!="unspecified" && lower!="unknown" && lower!="synthetic test environment";
            };

            for(auto testCase:sentinel::simulation::DefaultEvaluationTestCases()) {
                if(testCase.id=="persona.identity") {
                    if(configured(simSettings_.persona.name)) testCase.expectedContains.push_back(simSettings_.persona.name);
                    if(simSettings_.persona.age>0) testCase.expectedContains.push_back(std::to_string(simSettings_.persona.age));
                } else if(testCase.id=="persona.location") {
                    if(configured(simSettings_.persona.location)) testCase.expectedContains.push_back(simSettings_.persona.location);
                } else if(testCase.id=="persona.occupation") {
                    if(configured(simSettings_.persona.occupation)) testCase.expectedContains.push_back(simSettings_.persona.occupation);
                }

                if(testCase.dimension==sentinel::simulation::EvaluationDimension::MemoryRecall) {
                    auto memoryContext=baseContext;
                    if(!testCase.expectedContains.empty()) memoryExpected=testCase.expectedContains.front();
                    memoryContext.history.push_back({
                        sentinel::simulation::ChatTurn::Speaker::Investigator,
                        "For this evaluation, remember the code word "+memoryExpected+"."
                    });
                    memoryContext.history.push_back({
                        sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
                        "Okay, I will remember the code word "+memoryExpected+"."
                    });

                    const size_t required=std::max<size_t>(testCase.minimumHistoryTurns,24);
                    size_t pair=0;
                    while(memoryContext.history.size()<required) {
                        memoryContext.history.push_back({
                            sentinel::simulation::ChatTurn::Speaker::Investigator,
                            "Evaluation filler turn "+std::to_string(pair)+": tell me one short neutral thing about your day."
                        });
                        memoryContext.history.push_back({
                            sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
                            "Evaluation filler response "+std::to_string(pair)+"."
                        });
                        ++pair;
                    }

                    memoryReply=generate(testCase.prompt,memoryContext);
                    policyResponses.push_back(memoryReply);
                    caseResults.push_back(sentinel::simulation::ScoreNamedCase(testCase,memoryReply));
                    continue;
                }

                auto reply=generate(testCase.prompt);
                policyResponses.push_back(reply);
                caseResults.push_back(sentinel::simulation::ScoreNamedCase(testCase,reply));
                if(testCase.dimension==sentinel::simulation::EvaluationDimension::PersonaConsistency)
                    personaResponses.push_back(reply);
                if(testCase.dimension==sentinel::simulation::EvaluationDimension::StyleConsistency)
                    styleResponses.push_back(reply);
            }

            size_t triggerTotal=0;
            size_t triggerPassed=0;
            for(const auto& rule:PersonaResponseRules(50)) {
                if(!rule.enabled) continue;
                ++triggerTotal;
                const auto matched=FindPersonaResponseRule(rule.trigger);
                if(matched && matched->id==rule.id) ++triggerPassed;
            }

            std::vector<sentinel::simulation::EvaluationDimensionResult> dimensions;
            dimensions.push_back(sentinel::simulation::ScorePersonaConsistency(
                simSettings_.persona,simSettings_.ageState,personaResponses));
            dimensions.push_back(sentinel::simulation::ScorePolicyCompliance(
                simSettings_.ageState,policyResponses));
            dimensions.push_back(sentinel::simulation::ScoreStyleConsistency(
                simSettings_.persona,styleResponses));
            dimensions.push_back(sentinel::simulation::ScoreMemoryRecall(memoryExpected,memoryReply));
            dimensions.push_back(sentinel::simulation::ScoreTriggerRegression(
                triggerTotal,triggerPassed));
            dimensions.push_back(sentinel::simulation::ScoreResponseDiversity(styleResponses));

            for(auto& dimension:dimensions) {
                int caseTotal=0;
                int caseCount=0;
                for(const auto& cr:caseResults) {
                    if(cr.dimension!=dimension.dimension) continue;
                    caseTotal+=cr.score;
                    ++caseCount;
                    if(!cr.passed) dimension.warnings.push_back(cr.caseName+": "+cr.details);
                }
                if(caseCount>0) {
                    const int caseAverage=caseTotal/caseCount;
                    dimension.score=(dimension.score+caseAverage)/2;
                    dimension.passed=dimension.score>=80;
                    dimension.details+=" Named-case average "+std::to_string(caseAverage)+".";
                }
            }

            std::string foundationId;
            std::string foundationName;
            std::string adapterId;
            std::string adapterName;

            auto binding=runtime_->trainer.ResolvePersonaLora(simSettings_.persona.name);
            if(binding) {
                foundationId=binding->foundationId;
                adapterId=std::to_string(binding->id);
                adapterName=binding->loraName;
                auto foundation=runtime_->trainer.GetFoundation(binding->foundationId);
                if(foundation) foundationName=foundation->name;
            }
            if(foundationName.empty()) {
                auto foundations=runtime_->trainer.ListFoundations();
                auto active=std::find_if(foundations.begin(),foundations.end(),[](const auto& f){
                    return f.status=="ACTIVE";
                });
                if(active!=foundations.end()) {
                    foundationId=active->id;
                    foundationName=active->name;
                }
            }

            auto& run=evaluationRuns_.Create(
                item.id,item.modelName,
                foundationId,foundationName,
                adapterId,adapterName,
                std::move(dimensions),std::move(caseResults));

            selectedEvaluationRun_=(int)evaluationRuns_.Runs().size()-1;
            comparisonEvaluationRun_=evaluationRuns_.Runs().size()>1
                ? selectedEvaluationRun_-1
                : -1;

            item.evaluationScore=run.overallScore;
            item.latencyMs=std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now()-started).count();

            lastEvaluation_.score=run.overallScore;
            lastEvaluation_.policyAllowed=true;
            lastEvaluation_.personaConsistent=true;
            lastEvaluation_.warnings.clear();
            for(const auto& d:run.dimensions) {
                if(d.dimension==sentinel::simulation::EvaluationDimension::PolicyCompliance)
                    lastEvaluation_.policyAllowed=d.passed;
                if(d.dimension==sentinel::simulation::EvaluationDimension::PersonaConsistency)
                    lastEvaluation_.personaConsistent=d.passed;
            }
            lastEvaluation_.warnings=run.warnings;

            evaluationRuns_.Save(runtime_->root/"evaluation-runs.tsv");
            modelRegistry_.Save(runtime_->root/"model-registry.tsv");

            statusText_=L"Evaluation suite complete: "+std::to_wstring(run.overallScore)+L"/100";
        } catch(const std::exception& e) {
            item.evaluationScore=0;
            item.latencyMs=std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now()-started).count();
            statusText_=L"Model evaluation suite failed";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Model Evaluation Failed",MB_OK|MB_ICONERROR);
        }
    }

    void ExportSelectedEvaluationRun() {
        if(selectedEvaluationRun_<0 || selectedEvaluationRun_>=(int)evaluationRuns_.Runs().size()) {
            statusText_=L"Select an evaluation run first";
            return;
        }

        wchar_t file[MAX_PATH]{};
        wcscpy_s(file,L"SARA-Evaluation-Run.txt");
        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn);
        ofn.hwndOwner=hwnd_;
        ofn.lpstrFile=file;
        ofn.nMaxFile=MAX_PATH;
        ofn.lpstrFilter=L"Text Files\0*.txt\0All Files\0*.*\0\0";
        ofn.lpstrDefExt=L"txt";
        ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
        if(!GetSaveFileNameW(&ofn)) return;

        const auto report=sentinel::simulation::BuildEvaluationRunReport(
            evaluationRuns_.Runs()[(size_t)selectedEvaluationRun_]);
        std::ofstream out(std::filesystem::path(file),std::ios::trunc);
        out<<report;
        statusText_=L"Evaluation run report exported";
    }

    void ExportEvaluationComparison() {
        if(selectedEvaluationRun_<0 || selectedEvaluationRun_>=(int)evaluationRuns_.Runs().size() ||
           comparisonEvaluationRun_<0 || comparisonEvaluationRun_>=(int)evaluationRuns_.Runs().size()) {
            statusText_=L"Run at least two evaluations before exporting a comparison";
            return;
        }

        wchar_t file[MAX_PATH]{};
        wcscpy_s(file,L"SARA-Evaluation-Comparison.txt");
        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn);
        ofn.hwndOwner=hwnd_;
        ofn.lpstrFile=file;
        ofn.nMaxFile=MAX_PATH;
        ofn.lpstrFilter=L"Text Files\0*.txt\0All Files\0*.*\0\0";
        ofn.lpstrDefExt=L"txt";
        ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
        if(!GetSaveFileNameW(&ofn)) return;

        const auto report=sentinel::simulation::BuildCandidateComparisonReport(
            evaluationRuns_.Runs()[(size_t)selectedEvaluationRun_],
            evaluationRuns_.Runs()[(size_t)comparisonEvaluationRun_]);
        std::ofstream out(std::filesystem::path(file),std::ios::trunc);
        out<<report;
        statusText_=L"Evaluation comparison exported";
    }

    std::pair<std::string,std::string> CurrentEvaluationRuntimeIds() const {
        std::string foundationId;
        std::string adapterId;

        auto binding=runtime_->trainer.ResolvePersonaLora(simSettings_.persona.name);
        if(binding) {
            foundationId=binding->foundationId;
            adapterId=std::to_string(binding->id);
            return {foundationId,adapterId};
        }

        const auto foundations=runtime_->trainer.ListFoundations();
        auto active=std::find_if(foundations.begin(),foundations.end(),[](const auto& foundation){
            return foundation.status=="ACTIVE";
        });
        if(active!=foundations.end()) foundationId=active->id;
        return {foundationId,adapterId};
    }

    bool EvaluationMatchesCurrentRuntime(
        const sentinel::simulation::EvaluationRun& run) const
    {
        const auto [foundationId,adapterId]=CurrentEvaluationRuntimeIds();
        return run.foundationId==foundationId && run.adapterId==adapterId;
    }

    void ApproveSelectedRegistryModel() {
        if(selectedRegistryModel_<0 || selectedRegistryModel_>=(int)modelRegistry_.Models().size()) {
            statusText_=L"Select a registered model first";
            return;
        }

        auto& item=modelRegistry_.Models()[(size_t)selectedRegistryModel_];
        const int evalIndex=evaluationRuns_.LatestIndexForCandidate(item.id);
        if(evalIndex<0) {
            statusText_=L"Run Evaluation / Test before approving this model";
            return;
        }

        const auto& run=evaluationRuns_.Runs()[(size_t)evalIndex];
        if(!sentinel::simulation::EvaluationPassedApprovalGate(run)) {
            statusText_=L"Latest evaluation did not pass every required dimension";
            return;
        }

        if(!EvaluationMatchesCurrentRuntime(run)) {
            statusText_=L"Foundation or LoRA changed since evaluation; run Evaluation / Test again";
            return;
        }

        modelRegistry_.Approve((size_t)selectedRegistryModel_);
        if(item.stage!=sentinel::simulation::ModelStage::Approved &&
           item.stage!=sentinel::simulation::ModelStage::Active)
        {
            statusText_=L"Candidate model could not be approved from its current state";
            return;
        }

        modelRegistry_.Save(runtime_->root/"model-registry.tsv");
        statusText_=L"Candidate approved from passing evaluation "+Widen(run.id);
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


    void RetireSelectedRegistryModel() {
        if(selectedRegistryModel_<0 || selectedRegistryModel_>=(int)modelRegistry_.Models().size()) {
            statusText_=L"Select a registered model first";
            return;
        }
        modelRegistry_.Retire((size_t)selectedRegistryModel_);
        modelRegistry_.Save(runtime_->root/"model-registry.tsv");
        statusText_=L"Selected model retired";
    }


    void PrepareDeploymentPackage() {
        if(selectedRegistryModel_<0 || selectedRegistryModel_>=(int)modelRegistry_.Models().size()) {
            statusText_=L"Select an evaluated model before preparing deployment";
            return;
        }

        const auto& modelItem=modelRegistry_.Models()[(size_t)selectedRegistryModel_];
        if(modelItem.stage!=sentinel::simulation::ModelStage::Approved &&
           modelItem.stage!=sentinel::simulation::ModelStage::Active) {
            statusText_=L"Candidate must be approved before deployment preparation";
            return;
        }

        const int evalIndex=evaluationRuns_.LatestIndexForCandidate(modelItem.id);
        if(evalIndex<0) {
            statusText_=L"Run Evaluation before preparing deployment";
            return;
        }
        const auto& eval=evaluationRuns_.Runs()[(size_t)evalIndex];

        if(!sentinel::simulation::EvaluationPassedApprovalGate(eval)) {
            statusText_=L"Latest evaluation does not pass the deployment approval gate";
            return;
        }

        if(!EvaluationMatchesCurrentRuntime(eval)) {
            statusText_=L"Runtime stack changed since evaluation; reevaluate before preparing deployment";
            return;
        }

        if(!eval.foundationId.empty()) {
            auto foundation=runtime_->trainer.GetFoundation(eval.foundationId);
            if(!foundation) {
                statusText_=L"Evaluation foundation no longer exists";
                return;
            }
            if(foundation->status!="ACTIVE" && foundation->status!="APPROVED") {
                statusText_=L"Approve the evaluated foundation before preparing deployment";
                return;
            }
        }

        if(!eval.adapterId.empty()) {
            try {
                const auto adapterId=std::stoll(eval.adapterId);
                auto adapter=runtime_->trainer.GetPersonaLora(adapterId);
                if(!adapter) {
                    statusText_=L"Evaluation LoRA version no longer exists";
                    return;
                }
            } catch(...) {
                statusText_=L"Evaluation LoRA identifier is invalid";
                return;
            }
        }

        auto& package=deploymentRegistry_.Prepare(
            modelItem.id,modelItem.modelName,
            eval.foundationId,eval.foundationName,
            eval.adapterId,eval.adapterName,
            simSettings_.persona.name,
            eval.id,eval.overallScore);

        selectedDeployment_=(int)deploymentRegistry_.Packages().size()-1;
        deploymentRegistry_.Save(runtime_->root/"deployment-registry.tsv");

        const std::string meta=
            "prepared "+package.id+
            " model="+package.candidateId+
            " foundation="+package.foundationId+
            " adapter="+package.adapterId+
            " eval="+package.evaluationRunId;
        runtime_->audit.Append({
            sentinel::UserId::Random(),
            sentinel::AuditAction::DeploymentPrepared,
            "deployment",
            package.id,
            AuditMetadata(meta)
        });

        statusText_=L"Deployment package prepared: "+Widen(package.id);
    }

    bool ApplyDeploymentPackage(size_t index) {
        if(index>=deploymentRegistry_.Packages().size()) return false;
        const auto& package=deploymentRegistry_.Packages()[index];

        int modelIndex=-1;
        for(size_t i=0;i<modelRegistry_.Models().size();++i) {
            if(modelRegistry_.Models()[i].id==package.candidateId) {
                modelIndex=(int)i;
                break;
            }
        }
        if(modelIndex<0) {
            statusText_=L"Deployment model is no longer registered";
            return false;
        }

        auto& modelItem=modelRegistry_.Models()[(size_t)modelIndex];
        if(modelItem.stage!=sentinel::simulation::ModelStage::Approved &&
           modelItem.stage!=sentinel::simulation::ModelStage::Active) {
            statusText_=L"Deployment model is not approved";
            return false;
        }

        if(!package.personaName.empty() && package.personaName!=simSettings_.persona.name) {
            auto profile=runtime_->personaProfiles.Load(package.personaName);
            if(!profile) {
                statusText_=L"Deployment persona profile is missing";
                return false;
            }
            simSettings_.persona=*profile;
            simSettings_.minDelayMs=profile->responseStartMinMs;
            simSettings_.maxDelayMs=profile->responseStartMaxMs;
            LoadProfileEditors();
            LoadPersonaMedia();
        }

        if(!package.foundationId.empty()) {
            if(!runtime_->trainer.ActivateFoundation(package.foundationId)) {
                statusText_=L"Deployment foundation is not approved or cannot be activated";
                return false;
            }
            RefreshTrainerFoundationList(package.foundationId);
        }

        if(!package.adapterId.empty()) {
            try {
                const auto adapterId=std::stoll(package.adapterId);
                auto adapter=runtime_->trainer.GetPersonaLora(adapterId);
                if(!adapter || adapter->personaName!=package.personaName) {
                    statusText_=L"Deployment LoRA does not match the package persona";
                    return false;
                }
                if(!runtime_->trainer.ActivatePersonaLora(adapterId)) {
                    statusText_=L"Deployment LoRA could not be activated";
                    return false;
                }
            } catch(...) {
                statusText_=L"Deployment LoRA identifier is invalid";
                return false;
            }
        }

        modelRegistry_.Activate((size_t)modelIndex);
        if(modelRegistry_.ActiveIndex()!=modelIndex) {
            statusText_=L"Deployment model activation failed";
            return false;
        }

        selectedRegistryModel_=modelIndex;
        model_=sentinel::simulation::CreateOpenAICompatibleModel(modelItem.endpoint,modelItem.modelName);
        SetWindowTextW(modelEndpointEdit_,Widen(modelItem.endpoint).c_str());
        SetWindowTextW(modelNameEdit_,Widen(modelItem.modelName).c_str());
        simSettings_.endpoint=modelItem.endpoint;
        simSettings_.model=modelItem.modelName;
        modelStatus_=L"Deployment: "+Widen(package.id)+L" | "+Widen(modelItem.modelName);

        sentinel::simulation::SaveSimulationSettings(runtime_->root/"simulation.ini",simSettings_);
        modelRegistry_.Save(runtime_->root/"model-registry.tsv");

        if(IsLocalModelEndpoint(simSettings_.endpoint))
            ApplyPersonaRuntimeBinding();
        else {
            auto activeLora=runtime_->trainer.ResolvePersonaLora(simSettings_.persona.name);
            trainerRuntimeStatus_=activeLora
                ? L"Pinned LoRA: "+Widen(activeLora->loraName)
                : L"Pinned foundation without persona LoRA";
        }

        currentConversationId_.clear();
        ResumeOrCreateConversation();
        return true;
    }

    void ActivateSelectedDeploymentPackage() {
        if(selectedDeployment_<0 || selectedDeployment_>=(int)deploymentRegistry_.Packages().size()) {
            statusText_=L"Select a deployment package first";
            return;
        }
        if(!ApplyDeploymentPackage((size_t)selectedDeployment_)) return;
        if(!deploymentRegistry_.Activate((size_t)selectedDeployment_)) {
            statusText_=L"Deployment package activation failed";
            return;
        }

        deploymentRegistry_.Save(runtime_->root/"deployment-registry.tsv");
        const auto& package=deploymentRegistry_.Packages()[(size_t)selectedDeployment_];
        runtime_->audit.Append({
            sentinel::UserId::Random(),
            sentinel::AuditAction::DeploymentActivated,
            "deployment",
            package.id,
            AuditMetadata("activated "+package.id+" score="+std::to_string(package.evaluationScore))
        });
        statusText_=L"Deployment activated and runtime stack pinned";
    }

    void RollbackDeploymentPackage() {
        const int previous=deploymentRegistry_.PreviousIndex();
        if(previous<0 || previous>=(int)deploymentRegistry_.Packages().size()) {
            statusText_=L"No previous deployment package is available";
            return;
        }

        if(!ApplyDeploymentPackage((size_t)previous)) return;
        const std::string targetId=deploymentRegistry_.Packages()[(size_t)previous].id;
        if(!deploymentRegistry_.Rollback()) {
            statusText_=L"Deployment rollback failed";
            return;
        }

        selectedDeployment_=deploymentRegistry_.ActiveIndex();
        deploymentRegistry_.Save(runtime_->root/"deployment-registry.tsv");
        runtime_->audit.Append({
            sentinel::UserId::Random(),
            sentinel::AuditAction::DeploymentRolledBack,
            "deployment",
            targetId,
            AuditMetadata("rollback to "+targetId)
        });
        statusText_=L"Deployment rollback completed";
    }

    void ToggleDeploymentLock() {
        if(selectedDeployment_<0 || selectedDeployment_>=(int)deploymentRegistry_.Packages().size()) {
            statusText_=L"Select a deployment package first";
            return;
        }

        const bool next=!deploymentRegistry_.Packages()[(size_t)selectedDeployment_].versionLocked;
        deploymentRegistry_.SetLocked((size_t)selectedDeployment_,next);
        deploymentRegistry_.Save(runtime_->root/"deployment-registry.tsv");
        const auto& package=deploymentRegistry_.Packages()[(size_t)selectedDeployment_];

        runtime_->audit.Append({
            sentinel::UserId::Random(),
            sentinel::AuditAction::DeploymentLockChanged,
            "deployment",
            package.id,
            AuditMetadata(std::string("version_locked=")+(next?"true":"false"))
        });
        statusText_=next?L"Deployment version lock enabled":L"Deployment version lock disabled";
    }

    void ExportDeploymentManifest() {
        if(selectedDeployment_<0 || selectedDeployment_>=(int)deploymentRegistry_.Packages().size()) {
            statusText_=L"Select a deployment package first";
            return;
        }

        const auto& package=deploymentRegistry_.Packages()[(size_t)selectedDeployment_];
        wchar_t file[MAX_PATH]{};
        auto defaultName=Widen("SARA-"+package.id+".deployment.json");
        wcsncpy_s(file,defaultName.c_str(),_TRUNCATE);

        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn);
        ofn.hwndOwner=hwnd_;
        ofn.lpstrFile=file;
        ofn.nMaxFile=MAX_PATH;
        ofn.lpstrFilter=L"SARA Deployment Manifest\0*.json\0All Files\0*.*\0\0";
        ofn.lpstrDefExt=L"json";
        ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
        if(!GetSaveFileNameW(&ofn)) return;

        std::ofstream out(std::filesystem::path(file),std::ios::trunc);
        out<<deploymentRegistry_.BuildManifest((size_t)selectedDeployment_);

        runtime_->audit.Append({
            sentinel::UserId::Random(),
            sentinel::AuditAction::DeploymentManifestExported,
            "deployment",
            package.id,
            AuditMetadata("manifest exported")
        });
        statusText_=L"Deployment manifest exported";
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
            statusText_=L"Stage or select a training example before reviewing it";
            return;
        }
        try {
            const auto status=approve
                ? sentinel::simulation::TrainingReviewStatus::Approved
                : sentinel::simulation::TrainingReviewStatus::Rejected;
            if(runtime_->trainingReviews.Review(
                    selectedTrainingReviewId_,status,"local-reviewer",
                    approve?"Approved in SARA Model Lab":"Rejected in SARA Model Lab")) {
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


    static std::string CurrentUtcText() {
        SYSTEMTIME st{};
        GetSystemTime(&st);
        char buffer[40]{};
        sprintf_s(
            buffer,sizeof(buffer),
            "%04u-%02u-%02uT%02u:%02u:%02uZ",
            st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
        return buffer;
    }

    void ExportModelLabDiagnostics() {
        wchar_t file[MAX_PATH]{};
        wcscpy_s(file,L"SARA-Recovery-Model-Lab-Diagnostics.txt");

        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn);
        ofn.hwndOwner=hwnd_;
        ofn.lpstrFile=file;
        ofn.nMaxFile=MAX_PATH;
        ofn.lpstrFilter=L"Text Files\0*.txt\0All Files\0*.*\0\0";
        ofn.lpstrDefExt=L"txt";
        ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
        if(!GetSaveFileNameW(&ofn)) return;

        std::ofstream out(std::filesystem::path(file),std::ios::trunc);
        if(!out) {
            statusText_=L"Could not create diagnostics file";
            return;
        }

        const auto reviewCounts=runtime_->trainingReviews.Counts();
        const auto jobs=runtime_->trainer.ListJobs(50);
        const auto foundations=runtime_->trainer.ListFoundations();
        const auto loras=runtime_->trainer.ListPersonaLoras(simSettings_.persona.name,50);
        const auto rules=PersonaResponseRules(50);

        out<<"SARA MODEL LAB RECOVERY DIAGNOSTICS\n";
        out<<"Generated UTC: "<<CurrentUtcText()<<"\n";
        out<<"Compiled version: "<<SARA_VERSION_STR<<"\n";
        out<<"Recovery branch contract: sara-recovery-from-1.0.15\n\n";

        out<<"RUNTIME\n";
        out<<"Model status: "<<Narrow(modelStatus_)<<"\n";
        out<<"Trainer runtime: "<<Narrow(trainerRuntimeStatus_)<<"\n";
        out<<"Endpoint: "<<simSettings_.endpoint<<"\n";
        out<<"Model: "<<simSettings_.model<<"\n";
        out<<"Current conversation: "<<currentConversationId_<<"\n";
        out<<"Current title: "<<Narrow(currentConversationTitle_)<<"\n";
        out<<"Jurisdiction status: "<<Narrow(jurisdictionStatus_)<<"\n\n";

        out<<"PERSONA\n";
        out<<"Name: "<<simSettings_.persona.name<<"\n";
        out<<"Age: "<<simSettings_.persona.age<<"\n";
        out<<"Location: "<<simSettings_.persona.location<<"\n";
        out<<"Personality: "<<simSettings_.persona.personality<<"\n";
        out<<"Social style: "<<simSettings_.persona.socialStyle<<"\n";
        out<<"Confidence: "<<simSettings_.persona.confidenceLevel<<"\n";
        out<<"Writing style: "<<simSettings_.persona.writingStyle<<"\n";
        out<<"Communication: "<<simSettings_.persona.communicationLevel<<"\n";
        out<<"Cognitive level: "<<simSettings_.persona.cognitiveLevel<<"\n";
        out<<"Slang: "<<simSettings_.persona.slangLevel<<"\n";
        out<<"Grammar: "<<simSettings_.persona.grammarQuality<<"\n";
        out<<"Typos: "<<simSettings_.persona.typoFrequency<<"\n";
        out<<"Emoji: "<<simSettings_.persona.emojiLevel<<"\n";
        out<<"Response delay: "<<simSettings_.persona.responseStartMinMs
           <<"-"<<simSettings_.persona.responseStartMaxMs<<" ms\n";
        out<<"Learning mode: "<<(simSettings_.learningMode?"on":"off")<<"\n\n";

        out<<"MODEL LAB COUNTS\n";
        out<<"Saved personas: "<<runtime_->personaProfiles.ListNames().size()<<"\n";
        out<<"Foundations: "<<foundations.size()<<"\n";
        out<<"Persona LoRA versions: "<<loras.size()<<"\n";
        out<<"Review pending: "<<reviewCounts.pending<<"\n";
        out<<"Review approved: "<<reviewCounts.approved<<"\n";
        out<<"Review rejected: "<<reviewCounts.rejected<<"\n";
        out<<"Versioned training examples: "<<trainingData_.Examples().size()<<"\n";
        out<<"Dataset snapshots: "<<trainingData_.Snapshots().size()<<"\n";
        out<<"Training jobs: "<<jobs.size()<<"\n";
        out<<"Registered models: "<<modelRegistry_.Models().size()<<"\n";
        out<<"Evaluation runs: "<<evaluationRuns_.Runs().size()<<"\n";
        out<<"Deployment packages: "<<deploymentRegistry_.Packages().size()<<"\n";
        out<<"Enabled response rules: "<<PersonaResponseRuleCount()<<"\n\n";

        out<<"FOUNDATIONS\n";
        for(const auto& foundation:foundations) {
            out<<foundation.id<<" | "<<foundation.name<<" v"<<foundation.version
               <<" | "<<foundation.status
               <<" | parent="<<foundation.parentId
               <<" | source="<<foundation.sourceModel
               <<" | trainable="<<foundation.trainableSourcePath
               <<" | runtime="<<foundation.runtimeGgufPath<<"\n";
        }

        out<<"\nPERSONA LORA VERSIONS\n";
        for(const auto& lora:loras) {
            out<<lora.id<<" | "<<lora.loraName
               <<" | foundation="<<lora.foundationId
               <<" | active="<<(lora.active?"yes":"no")
               <<" | weight="<<lora.weight
               <<" | path="<<lora.loraPath<<"\n";
        }

        out<<"\nRESPONSE RULES\n";
        for(const auto& rule:rules) {
            out<<rule.id<<" | "<<rule.matchType
               <<" | mode="<<rule.responseMode
               <<" | flow="<<(rule.terminal?"terminal":"continue")
               <<" | variants="<<sentinel::simulation::SplitResponseRuleVariants(rule.response).size()
               <<" | enabled="<<(rule.enabled?"yes":"no")
               <<" | trigger="<<rule.trigger
               <<" | response="<<rule.response<<"\n";
        }

        out<<"\nRECENT RESPONSE RULE MATCHES\n";
        for(const auto& match:runtime_->responseRuleMatches.Recent(
                simSettings_.persona.name,50)) {
            out<<match.id
               <<" | rule="<<match.ruleId
               <<" | type="<<match.matchType
               <<" | score="<<match.matchScore
               <<" | mode="<<match.responseMode
               <<" | conversation="<<match.conversationId
               <<" | input="<<match.inputText
               <<" | output="<<match.outputText
               <<" | created="<<match.createdUtc<<"\n";
        }

        out<<"\nDATASET SNAPSHOTS\n";
        for(const auto& snapshot:trainingData_.Snapshots()) {
            out<<snapshot.id<<" | "<<snapshot.name
               <<" | parent="<<snapshot.parentId
               <<" | created="<<snapshot.createdUtc
               <<" | examples="<<snapshot.exampleIds.size()<<"\n";
        }

        out<<"\nTRAINING JOBS\n";
        for(const auto& job:jobs) {
            out<<job.id<<" | "<<sentinel::simulation::ToString(job.mode)
               <<" | "<<job.targetName
               <<" | persona="<<job.personaName
               <<" | state="<<job.state
               <<" | progress="<<job.progress
               <<" | created="<<job.createdUtc
               <<" | started="<<job.startedUtc
               <<" | completed="<<job.completedUtc;
            if(!job.errorText.empty()) out<<" | error="<<job.errorText;
            out<<"\n";
        }

        out<<"\nEVALUATION RUNS\n";
        for(const auto& run:evaluationRuns_.Runs()) {
            out<<run.id<<" | candidate="<<run.candidateName
               <<" | foundation="<<run.foundationName
               <<" | adapter="<<run.adapterName
               <<" | score="<<run.overallScore
               <<" | previous="<<run.previousOverallScore
               <<" | delta="<<run.regressionDelta
               <<" | warnings="<<run.warnings.size()<<"\n";
        }

        out<<"\nDEPLOYMENT PACKAGES\n";
        for(const auto& package:deploymentRegistry_.Packages()) {
            out<<package.id<<" | "<<sentinel::simulation::ToString(package.stage)
               <<" | candidate="<<package.candidateName
               <<" | foundation="<<package.foundationName
               <<" | adapter="<<package.adapterName
               <<" | persona="<<package.personaName
               <<" | evaluation="<<package.evaluationRunId
               <<" | score="<<package.evaluationScore
               <<" | locked="<<(package.versionLocked?"yes":"no")
               <<" | previous="<<package.previousDeploymentId<<"\n";
        }

        out<<"\nRECENT CONVERSATION\n";
        const size_t historyStart=simContext_.history.size()>20?simContext_.history.size()-20:0;
        for(size_t i=historyStart;i<simContext_.history.size();++i) {
            const auto& turn=simContext_.history[i];
            const char* speaker=
                turn.speaker==sentinel::simulation::ChatTurn::Speaker::Investigator?"Investigator":
                turn.speaker==sentinel::simulation::ChatTurn::Speaker::SyntheticSubject?"SARA":
                "Model Suggestion";
            out<<speaker<<": "<<turn.text<<"\n";
        }

        out.close();
        statusText_=L"Model Lab diagnostics exported";
    }

    void DrawModelLab(float w,float h) {
        PageTitle(
            L"Model Lab - Executive Dashboard",
            L"Current model, training, persona, review, and evaluation state from the recovered SARA 1.0.15 backend");

        const float x=kSidebar+28.0f;
        const float y=kHeader+94.0f;
        const float contentW=w-x-28.0f;
        const float gap=12.0f;

        // Shared hybrid Model Lab navigation.
        const wchar_t* tabLabels[]={
            L"Overview",L"Train",L"Datasets",L"Personas & LoRAs",
            L"Foundation Forks",L"Jobs",L"Evaluation",L"Deployment"
        };
        const wchar_t* tabIds[]={
            L"ml_overview",L"ml_train",L"ml_datasets",L"ml_personas",
            L"ml_foundations",L"ml_jobs",L"ml_evaluation",L"ml_deployment"
        };
        const float tabGap=6.0f;
        const float tabW=(contentW-tabGap*7.0f)/8.0f;
        for(int i=0;i<8;i++) {
            const float tx=x+i*(tabW+tabGap);
            const bool active=i==0;
            Rounded(tx,y,tabW,36,
                active?brush_.panel2.Get():brush_.sidebar.Get(),
                active?brush_.cyan.Get():brush_.border.Get(),7);
            TextLine(tabLabels[i],tx+5,y+2,tabW-10,30,tinyFmt_.Get(),
                active?brush_.cyan.Get():brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
            buttons_.push_back({{tx,y,tx+tabW,y+36},tabIds[i]});
        }

        const float dashY=y+48.0f;
        const auto reviewCounts=runtime_->trainingReviews.Counts();
        const auto jobs=runtime_->trainer.ListJobs(8);
        size_t runningJobs=0;
        for(const auto& job:jobs) {
            if(job.state=="RUNNING" || job.state=="QUEUED") ++runningJobs;
        }

        // Hero / current context.
        Rounded(x,dashY,contentW,76,brush_.panel.Get(),brush_.cyan.Get(),11);
        if(brandBitmap_) {
            const auto sz=brandBitmap_->GetSize();
            const float logoH=56.0f;
            const float logoW=logoH*(sz.width/std::max(1.0f,sz.height));
            target_->DrawBitmap(
                brandBitmap_.Get(),
                D2D1::RectF(x+16,dashY+10,x+16+logoW,dashY+10+logoH),
                1.0f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        TextLine(L"Model Lab Control Center",x+90,dashY+10,330,28,h1Fmt_.Get(),brush_.text.Get());
        TextLine(
            L"Active persona: "+Widen(simSettings_.persona.name)+L"   |   Runtime: "+trainerRuntimeStatus_,
            x+90,dashY+40,contentW-392,22,smallFmt_.Get(),brush_.muted.Get());
        StatusDot(x+contentW-164,dashY+23,5,brush_.green.Get());
        TextLine(L"LOCAL / READY",x+contentW-150,dashY+11,132,24,tinyFmt_.Get(),brush_.green.Get());

        // Four real metrics.
        const float metricsY=dashY+88.0f;
        const float cardW=(contentW-gap*3.0f)/4.0f;
        struct DashboardMetric {
            const wchar_t* label;
            std::wstring value;
            std::wstring sub;
            ID2D1Brush* accent;
            IconKind icon;
        };
        DashboardMetric metrics[]={
            {L"Registered Models",std::to_wstring(modelRegistry_.Models().size()),
                L"Model registry entries",brush_.cyan.Get(),IconKind::Database},
            {L"Reviewed Examples",std::to_wstring(reviewCounts.approved),
                L"Approved for training",brush_.green.Get(),IconKind::Check},
            {L"Response Rules",std::to_wstring(PersonaResponseRuleCount()),
                L"Active persona rules",brush_.blue.Get(),IconKind::Chat},
            {L"Training Jobs",std::to_wstring(jobs.size()),
                std::to_wstring(runningJobs)+L" queued / running",brush_.yellow.Get(),IconKind::Document}
        };
        for(int i=0;i<4;i++) {
            const float mx=x+i*(cardW+gap);
            Rounded(mx,metricsY,cardW,86,brush_.panel.Get(),brush_.border.Get(),10);
            Rounded(mx+14,metricsY+16,38,38,brush_.panel2.Get(),nullptr,9);
            DrawIcon(metrics[i].icon,mx+22,metricsY+24,22,metrics[i].accent);
            TextLine(metrics[i].label,mx+62,metricsY+10,cardW-74,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(metrics[i].value,mx+62,metricsY+28,cardW-74,28,h1Fmt_.Get(),brush_.text.Get());
            TextLine(metrics[i].sub,mx+14,metricsY+62,cardW-28,18,tinyFmt_.Get(),metrics[i].accent);
        }

        const float mainY=metricsY+98.0f;
        const float mainH=204.0f;
        const float leftW=contentW*0.58f-gap*0.5f;
        const float rightW=contentW-leftW-gap;
        const float rightX=x+leftW+gap;

        // Recent training activity.
        Rounded(x,mainY,leftW,mainH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Recent Training Activity",x+16,mainY+10,leftW-32,26,h1Fmt_.Get(),brush_.text.Get());
        AddButton(L"ml_train",L"Open Trainer",x+leftW-112,mainY+10,96,26,false);

        float jy=mainY+48.0f;
        if(jobs.empty()) {
            Rounded(x+14,jy,leftW-28,44,brush_.sidebar.Get(),brush_.border.Get(),7);
            TextLine(L"No training jobs yet. Use Train to create the first reviewed job.",
                x+26,jy+10,leftW-52,24,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(size_t i=0;i<jobs.size() && i<4;i++) {
                const auto& job=jobs[i];
                Rounded(x+14,jy,leftW-28,32,brush_.sidebar.Get(),brush_.border.Get(),6);
                StatusDot(x+28,jy+16,4,
                    job.state=="COMPLETED"?brush_.green.Get():
                    job.state=="RUNNING"?brush_.cyan.Get():brush_.yellow.Get());
                TextLine(Widen(job.targetName),x+40,jy+5,leftW*0.43f,20,tinyFmt_.Get(),brush_.text.Get());
                TextLine(Widen(sentinel::simulation::ToString(job.mode)),
                    x+leftW*0.47f,jy+5,leftW*0.25f,20,tinyFmt_.Get(),brush_.muted.Get());
                TextLine(Widen(job.state)+L"  "+std::to_wstring(job.progress)+L"%",
                    x+leftW-132,jy+5,104,20,tinyFmt_.Get(),
                    job.state=="COMPLETED"?brush_.green.Get():brush_.cyan.Get(),
                    DWRITE_TEXT_ALIGNMENT_TRAILING);
                jy+=38.0f;
            }
        }

        // Evaluation + runtime status.
        Rounded(rightX,mainY,rightW,mainH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Evaluation & Runtime",rightX+16,mainY+10,rightW-32,26,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Last evaluation score",rightX+16,mainY+51,rightW-32,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(std::to_wstring(lastEvaluation_.score),
            rightX+16,mainY+70,92,34,bigFmt_.Get(),
            lastEvaluation_.score>=80?brush_.green.Get():
            lastEvaluation_.score>=50?brush_.yellow.Get():brush_.muted.Get());

        TextLine(L"Policy",rightX+122,mainY+51,72,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(lastEvaluation_.policyAllowed?L"Allowed":L"Not evaluated",
            rightX+122,mainY+73,rightW-138,22,smallFmt_.Get(),
            lastEvaluation_.policyAllowed?brush_.green.Get():brush_.muted.Get());

        TextLine(L"Persona consistency",rightX+16,mainY+112,rightW-32,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(lastEvaluation_.personaConsistent?L"Consistent":L"No result yet",
            rightX+16,mainY+133,rightW-32,22,smallFmt_.Get(),
            lastEvaluation_.personaConsistent?brush_.cyan.Get():brush_.muted.Get());

        TextLine(L"Foundation forks",rightX+16,mainY+166,100,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(std::to_wstring(trainerFoundations_.size()),
            rightX+124,mainY+164,48,22,smallFmt_.Get(),brush_.text.Get());
        TextLine(L"Review pending",rightX+184,mainY+166,88,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(std::to_wstring(reviewCounts.pending),
            rightX+rightW-44,mainY+164,30,22,smallFmt_.Get(),brush_.yellow.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);

        // Review/rules card retains the real 1.0.15 rule editor functionality.
        const float rulesY=mainY+mainH+12.0f;
        const float rulesH=132.0f;
        Rounded(x,rulesY,contentW,rulesH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Persona Rules & Reviewed Learning",x+16,rulesY+10,310,26,h1Fmt_.Get(),brush_.text.Get());

        AddButton(L"learning_toggle",
            simSettings_.learningMode?L"Learning ON":L"Learning OFF",
            x+contentW-126,rulesY+10,110,26,simSettings_.learningMode);

        TextLine(L"Trigger",x+16,rulesY+48,58,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Response",x+contentW*0.39f,rulesY+48,66,18,tinyFmt_.Get(),brush_.muted.Get());

        AddButton(L"rule_add_smart",L"Add Smart",x+contentW-310,rulesY+84,82,28,true);
        AddButton(L"rule_test",L"Test",x+contentW-220,rulesY+84,60,28,false);
        AddButton(L"rule_clear",L"Clear",x+contentW-152,rulesY+84,60,28,false);
        AddButton(L"persona_rules_open",L"Manage",x+contentW-84,rulesY+84,68,28,false);

        TextLine(
            L"Approved "+std::to_wstring(reviewCounts.approved)+
            L"   Pending "+std::to_wstring(reviewCounts.pending)+
            L"   Rejected "+std::to_wstring(reviewCounts.rejected),
            x+16,rulesY+91,contentW-340,20,tinyFmt_.Get(),brush_.muted.Get());

        // Quick actions anchor the dashboard to existing recovered workflows.
        const float quickY=rulesY+rulesH+12.0f;
        Rounded(x,quickY,contentW,62,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Quick Actions",x+16,quickY+9,100,20,smallFmt_.Get(),brush_.text.Get());
        AddButton(L"ml_train",L"Start Training",x+128,quickY+14,112,32,true);
        AddButton(L"ml_personas",L"Manage Persona",x+250,quickY+14,112,32,false);
        AddButton(L"model_register",L"Register Model",x+372,quickY+14,112,32,false);
        AddButton(L"model_eval",L"Evaluate",x+494,quickY+14,92,32,false);
        AddButton(L"training_export",L"Export Reviewed",x+596,quickY+14,122,32,false);
        AddButton(L"ml_diagnostics_export",L"Diagnostics",x+728,quickY+14,102,32,false);
    }

    void DrawTrainer(float w,float h) {
        PageTitle(
            L"Model Lab / Trainer",
            L"Persistent trainer chat with review-before-apply tuning and queued weight training");

        const float x=kSidebar+28.0f;
        const float y=kHeader+94.0f;
        const float contentW=w-x-28.0f;
        const float gap=14.0f;

        const wchar_t* tabLabels[]={
            L"Overview",L"Train",L"Datasets",L"Personas & LoRAs",
            L"Foundation Forks",L"Jobs",L"Evaluation",L"Deployment"
        };
        const wchar_t* tabIds[]={
            L"ml_overview",L"ml_train",L"ml_datasets",L"ml_personas",
            L"ml_foundations",L"ml_jobs",L"ml_evaluation",L"ml_deployment"
        };
        const float tabGap=6.0f;
        const float tabW=(contentW-tabGap*7.0f)/8.0f;
        for(int i=0;i<8;i++) {
            const float tx=x+i*(tabW+tabGap);
            const bool active=i==1;
            Rounded(tx,y,tabW,36,
                active?brush_.panel2.Get():brush_.sidebar.Get(),
                active?brush_.cyan.Get():brush_.border.Get(),7);
            TextLine(tabLabels[i],tx+5,y+2,tabW-10,30,tinyFmt_.Get(),
                active?brush_.cyan.Get():brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
            buttons_.push_back({{tx,y,tx+tabW,y+36},tabIds[i]});
        }

        const float heroY=y+48.0f;
        const float heroH=76.0f;
        Rounded(x,heroY,contentW,heroH,brush_.panel.Get(),brush_.cyan.Get(),11);

        if(brandBitmap_) {
            const auto sz=brandBitmap_->GetSize();
            const float logoH=56.0f;
            const float logoW=logoH*(sz.width/std::max(1.0f,sz.height));
            target_->DrawBitmap(
                brandBitmap_.Get(),
                D2D1::RectF(x+16,heroY+10,x+16+logoW,heroY+10+logoH),
                1.0f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        TextLine(L"Conversational Model Trainer",x+90,heroY+10,360,28,h1Fmt_.Get(),brush_.text.Get());
        TextLine(
            L"Describe a change, review SARA's interpretation, then apply or queue it.",
            x+90,heroY+39,contentW-424,22,smallFmt_.Get(),brush_.muted.Get());
        StatusDot(x+contentW-154,heroY+22,5,brush_.green.Get());
        TextLine(L"REVIEW FIRST",x+contentW-140,heroY+10,122,26,tinyFmt_.Get(),brush_.green.Get());

        const float bodyY=heroY+heroH+12.0f;
        const float bodyH=std::max(410.0f,h-bodyY-24.0f);
        const float rightW=std::clamp(contentW*0.29f,286.0f,342.0f);
        const float leftW=contentW-rightW-gap;
        const float rightX=x+leftW+gap;

        Rounded(x,bodyY,leftW,bodyH,brush_.panel.Get(),brush_.border.Get(),11);
        TextLine(L"Trainer Conversation",x+18,bodyY+12,300,30,h1Fmt_.Get(),brush_.text.Get());
        Badge(L"PERSISTENT",x+leftW-112,bodyY+15,brush_.cyan.Get(),94);

        TextLine(L"Training Mode",x+18,bodyY+49,104,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Foundation",x+leftW*0.50f,bodyY+49,92,18,tinyFmt_.Get(),brush_.muted.Get());

        const auto mode=SelectedTrainingMode();
        const auto session=runtime_->trainer.ActiveDialogueSession(
            simSettings_.persona.name,mode);
        const auto turns=session
            ? runtime_->trainer.ListDialogueTurns(session->id,30)
            : std::vector<sentinel::simulation::TrainerDialogueTurn>{};

        const float actionY=bodyY+bodyH-42.0f;
        const float composerY=actionY-94.0f;
        const float advancedY=composerY-132.0f;
        const float conversationY=bodyY+104.0f;
        const float conversationBottom=trainerAdvancedOpen_
            ? advancedY-10.0f
            : composerY-12.0f;
        const float conversationH=std::max(52.0f,conversationBottom-conversationY);

        TextLine(L"Conversation History",x+18,bodyY+86,leftW-36,18,tinyFmt_.Get(),brush_.cyan.Get());

        const int maxRows=std::max(1,(int)(conversationH/62.0f));
        const size_t start=turns.size()>(size_t)maxRows
            ? turns.size()-(size_t)maxRows
            : 0;
        float turnY=conversationY;

        if(turns.empty()) {
            Rounded(x+18,turnY,leftW-36,std::min(72.0f,conversationH),brush_.sidebar.Get(),brush_.border.Get(),8);
            Text(
                L"SARA Trainer is ready. Describe the behavior you want changed, a correction you want captured, "
                L"or the goal for a LoRA/foundation job. Behavior changes are previewed and require Apply Preview.",
                x+32,turnY+10,leftW-64,std::min(54.0f,conversationH-16.0f),
                tinyFmt_.Get(),brush_.muted.Get());
        } else {
            for(size_t index=start;index<turns.size();++index) {
                const auto& turn=turns[index];
                const bool operatorTurn=turn.role=="OPERATOR";
                const bool systemTurn=turn.role=="SYSTEM";
                ID2D1Brush* accent=
                    operatorTurn?brush_.cyan.Get():
                    systemTurn?brush_.yellow.Get():
                    brush_.green.Get();

                Rounded(x+18,turnY,leftW-36,56,brush_.sidebar.Get(),brush_.border.Get(),8);
                TextLine(
                    operatorTurn?L"Investigator / Trainer":
                    systemTurn?L"Trainer System":L"SARA Trainer",
                    x+30,turnY+5,leftW-190,17,tinyFmt_.Get(),accent);

                if(turn.applied)
                    Badge(L"APPLIED",x+leftW-100,turnY+5,brush_.green.Get(),70);

                std::wstring message=Widen(turn.text);
                if(message.size()>220) message=message.substr(0,217)+L"...";
                Text(message,x+30,turnY+23,leftW-60,28,tinyFmt_.Get(),brush_.text.Get());
                turnY+=62.0f;
                if(turnY+56>conversationY+conversationH+2) break;
            }
        }

        if(trainerAdvancedOpen_) {
            target_->DrawLine(
                D2D1::Point2F(x+18,advancedY-8),
                D2D1::Point2F(x+leftW-18,advancedY-8),
                brush_.border.Get(),1.0f);
            TextLine(L"Advanced Training Setup",x+18,advancedY,200,20,smallFmt_.Get(),brush_.text.Get());
            AddButton(L"trainer_create_fork",L"Create Fork",x+leftW-202,advancedY-3,92,26,false);
            AddButton(L"trainer_bind_lora",L"Bind LoRA",x+leftW-102,advancedY-3,84,26,false);

            TextLine(L"Fork name",x+18,advancedY+25,82,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Source",x+leftW*0.48f,advancedY+25,90,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"LoRA name",x+18,advancedY+59,82,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Adapter",x+leftW*0.48f,advancedY+59,90,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Dataset",x+18,advancedY+93,82,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Output",x+leftW*0.48f,advancedY+93,90,18,tinyFmt_.Get(),brush_.muted.Get());
        }

        TextLine(
            mode==sentinel::simulation::TrainingMode::Behavior
                ? L"MESSAGE TO SARA TRAINER - preview only; nothing changes until Apply"
                : L"MESSAGE TO SARA TRAINER - captured as reviewed training intent",
            x+18,composerY,leftW-36,18,tinyFmt_.Get(),brush_.cyan.Get());

        const float actionGap=6.0f;
        const float actionW=(leftW-36.0f-actionGap*4.0f)/5.0f;
        float bx=x+18.0f;
        AddButton(L"trainer_apply_instruction",L"Send",bx,actionY,actionW,30,true); bx+=actionW+actionGap;
        AddButton(L"trainer_apply_preview",L"Apply",bx,actionY,actionW,30,
            mode==sentinel::simulation::TrainingMode::Behavior); bx+=actionW+actionGap;
        AddButton(L"trainer_new_session",L"New",bx,actionY,actionW,30,false); bx+=actionW+actionGap;
        AddButton(L"trainer_queue",L"Queue",bx,actionY,actionW,30,false); bx+=actionW+actionGap;
        AddButton(
            L"trainer_advanced_toggle",
            trainerAdvancedOpen_?L"Hide Advanced":L"Advanced",
            bx,actionY,actionW,30,false);

        // Right-side training context.
        const float contextH=148.0f;
        Rounded(rightX,bodyY,rightW,contextH,brush_.panel.Get(),brush_.border.Get(),11);
        TextLine(L"Training Context",rightX+16,bodyY+12,rightW-32,28,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Active Persona",rightX+16,bodyY+50,92,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(Widen(simSettings_.persona.name),rightX+118,bodyY+47,rightW-134,24,smallFmt_.Get(),brush_.cyan.Get());

        TextLine(L"Mode",rightX+16,bodyY+78,92,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(TrainerModeDisplayName(mode),rightX+118,bodyY+75,rightW-134,24,tinyFmt_.Get(),brush_.text.Get());

        TextLine(L"Session",rightX+16,bodyY+106,92,18,tinyFmt_.Get(),brush_.muted.Get());
        std::wstring sessionLabel=session?Widen(session->id):L"Not started";
        if(sessionLabel.size()>24) sessionLabel=sessionLabel.substr(0,21)+L"...";
        TextLine(sessionLabel,rightX+118,bodyY+103,rightW-134,24,tinyFmt_.Get(),brush_.text.Get());

        TextLine(L"Weight change",rightX+16,bodyY+132,92,16,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(
            mode==sentinel::simulation::TrainingMode::Behavior
                ? L"Profile only"
                : mode==sentinel::simulation::TrainingMode::Preference
                    ? L"Not enabled"
                    : L"Queued + reviewed",
            rightX+118,bodyY+129,rightW-134,20,tinyFmt_.Get(),
            mode==sentinel::simulation::TrainingMode::Preference?brush_.yellow.Get():brush_.green.Get());

        const float pipelineY=bodyY+contextH+12.0f;
        const float pipelineH=118.0f;
        Rounded(rightX,pipelineY,rightW,pipelineH,brush_.panel.Get(),brush_.border.Get(),11);
        TextLine(L"Training Pipeline",rightX+16,pipelineY+10,rightW-32,26,h1Fmt_.Get(),brush_.text.Get());

        const wchar_t* stages[]={L"Capture",L"Review",L"Dataset",L"Train",L"Evaluate",L"Deploy"};
        const float lineL=rightX+24.0f;
        const float lineR=rightX+rightW-24.0f;
        const float stepGap=(lineR-lineL)/5.0f;
        target_->DrawLine(
            D2D1::Point2F(lineL,pipelineY+58),
            D2D1::Point2F(lineR,pipelineY+58),
            brush_.border.Get(),2.0f);
        const int currentStage=turns.empty()?0:1;
        for(int i=0;i<6;i++) {
            const float sx=lineL+i*stepGap;
            const bool current=i==currentStage;
            Rounded(sx-10,pipelineY+48,20,20,
                current?brush_.blue.Get():brush_.sidebar.Get(),
                current?brush_.cyan.Get():brush_.border.Get(),10);
            TextLine(std::to_wstring(i+1),sx-8,pipelineY+49,16,16,tinyFmt_.Get(),
                current?brush_.text.Get():brush_.muted.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
            TextLine(stages[i],sx-28,pipelineY+76,56,18,tinyFmt_.Get(),
                current?brush_.cyan.Get():brush_.muted.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        const float jobsY=pipelineY+pipelineH+12.0f;
        const float jobsH=std::max(132.0f,bodyH-(jobsY-bodyY));
        Rounded(rightX,jobsY,rightW,jobsH,brush_.panel.Get(),brush_.border.Get(),11);
        TextLine(L"Training Jobs",rightX+16,jobsY+10,rightW-190,26,h1Fmt_.Get(),brush_.text.Get());
        AddButton(
            L"trainer_setup_env",
            TrainerEnvironmentReady()?L"Env Ready":L"Prepare Env",
            rightX+rightW-186,jobsY+10,88,26,TrainerEnvironmentReady());
        AddButton(L"trainer_run_latest",L"Run",rightX+rightW-92,jobsY+10,76,26,false);

        auto jobs=runtime_->trainer.ListJobs(4);
        float jy=jobsY+44.0f;
        if(jobs.empty()) {
            TextLine(L"No training jobs queued yet.",rightX+16,jy,rightW-32,24,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(const auto& job:jobs) {
                Rounded(rightX+14,jy,rightW-28,42,brush_.sidebar.Get(),brush_.border.Get(),7);
                TextLine(Widen(job.targetName),rightX+24,jy+4,rightW-116,18,tinyFmt_.Get(),brush_.text.Get());
                TextLine(Widen(job.state)+L"  "+std::to_wstring(job.progress)+L"%",
                    rightX+24,jy+21,rightW-116,16,tinyFmt_.Get(),
                    job.state=="COMPLETED"?brush_.green.Get():brush_.cyan.Get());
                TextLine(Widen(sentinel::simulation::ToString(job.mode)),
                    rightX+rightW-100,jy+12,76,18,tinyFmt_.Get(),brush_.muted.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);
                jy+=48.0f;
                if(jy+42>jobsY+jobsH-8) break;
            }
        }

        TextLine(L"Runtime",rightX+16,jobsY+jobsH-28,58,18,tinyFmt_.Get(),brush_.muted.Get());
        const std::wstring runtimeSummary=
            trainerRuntimeStatus_+
            (TrainerEnvironmentReady()?L" | env ready":L" | env setup needed");
        TextLine(
            runtimeSummary,rightX+76,jobsY+jobsH-30,rightW-92,20,tinyFmt_.Get(),
            TrainerEnvironmentReady()?brush_.cyan.Get():brush_.yellow.Get());
    }


    size_t SyncApprovedReviewsToVersionedData() {
        const auto approved=runtime_->trainingReviews.ListRecent(
            500,sentinel::simulation::TrainingReviewStatus::Approved);

        size_t added=0;
        for(const auto& item:approved) {
            const bool exists=std::any_of(
                trainingData_.Examples().begin(),trainingData_.Examples().end(),
                [&](const auto& e){
                    return e.persona==item.personaName &&
                           e.sourceConversationId==item.conversationId &&
                           e.input==item.inputText &&
                           e.targetResponse==item.outputText;
                });
            if(exists) continue;

            std::string foundationId;
            std::string adapterId;
            auto binding=runtime_->trainer.ResolvePersonaLora(item.personaName);
            if(binding) {
                foundationId=binding->foundationId;
                adapterId=std::to_string(binding->id);
            } else {
                const auto foundations=runtime_->trainer.ListFoundations();
                auto active=std::find_if(foundations.begin(),foundations.end(),[](const auto& foundation){
                    return foundation.status=="ACTIVE";
                });
                if(active!=foundations.end()) foundationId=active->id;
            }

            auto& example=trainingData_.Capture(
                item.personaName,
                foundationId,
                adapterId,
                item.conversationId,
                item.inputText,
                item.outputText,
                {},
                item.outputText,
                "Reviewed");
            example.reviewer=item.reviewer;
            trainingData_.SetState(
                trainingData_.Examples().size()-1,
                sentinel::simulation::TrainingExampleState::Approved);
            ++added;
        }

        if(added>0) trainingData_.Save(runtime_->root/"training-data.tsv");
        return added;
    }

    void CreateReviewedDatasetSnapshot() {
        try {
            const auto approved=runtime_->trainingReviews.Counts().approved;
            if(approved<=0) {
                statusText_=L"Approve at least one review item before creating a dataset snapshot";
                return;
            }

            const auto added=SyncApprovedReviewsToVersionedData();
            const std::string name=
                "Approved Dataset "+std::to_string(trainingData_.Snapshots().size()+1);
            auto& snapshot=trainingData_.CreateSnapshot(name);
            selectedDatasetSnapshot_=(int)trainingData_.Snapshots().size()-1;
            trainingData_.Save(runtime_->root/"training-data.tsv");
            statusText_=L"Dataset snapshot created with "+
                std::to_wstring(snapshot.exampleIds.size())+
                L" approved examples ("+std::to_wstring(added)+L" newly synchronized)";
        } catch(const std::exception& e) {
            statusText_=L"Dataset snapshot creation failed";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Dataset Snapshot",MB_OK|MB_ICONERROR);
        }
    }

    void ImportDatasetSnapshotFile() {
        wchar_t file[32768]{};
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.hwndOwner=hwnd_;
        ofn.lpstrFile=file;
        ofn.nMaxFile=32768;
        ofn.lpstrFilter=L"SARA Dataset Snapshot\0*.sara-dataset\0All Files\0*.*\0\0";
        ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if(!GetOpenFileNameW(&ofn)) return;

        try {
            auto& snapshot=trainingData_.ImportSnapshot(std::filesystem::path(file));
            selectedDatasetSnapshot_=(int)trainingData_.Snapshots().size()-1;
            trainingData_.Save(runtime_->root/"training-data.tsv");
            statusText_=L"Imported dataset snapshot: "+Widen(snapshot.name)+
                L" ("+std::to_wstring(snapshot.exampleIds.size())+L" examples)";
        } catch(const std::exception& e) {
            statusText_=L"Dataset snapshot import failed";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Dataset Import",MB_OK|MB_ICONERROR);
        }
    }

    void ExportSelectedDatasetSnapshot() {
        if(selectedDatasetSnapshot_<0 ||
           selectedDatasetSnapshot_>=(int)trainingData_.Snapshots().size()) {
            statusText_=L"Select or create a dataset snapshot first";
            return;
        }

        const auto& snapshot=trainingData_.Snapshots()[(size_t)selectedDatasetSnapshot_];
        wchar_t file[MAX_PATH]{};
        auto defaultName=Widen(snapshot.name+".sara-dataset");
        wcsncpy_s(file,defaultName.c_str(),_TRUNCATE);

        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn);
        ofn.hwndOwner=hwnd_;
        ofn.lpstrFile=file;
        ofn.nMaxFile=MAX_PATH;
        ofn.lpstrFilter=L"SARA Dataset Snapshot\0*.sara-dataset\0All Files\0*.*\0\0";
        ofn.lpstrDefExt=L"sara-dataset";
        ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
        if(!GetSaveFileNameW(&ofn)) return;

        try {
            trainingData_.ExportSnapshot(
                (size_t)selectedDatasetSnapshot_,
                std::filesystem::path(file));
            statusText_=L"Dataset snapshot exported";
        } catch(const std::exception& e) {
            statusText_=L"Dataset snapshot export failed";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Dataset Export",MB_OK|MB_ICONERROR);
        }
    }

    void DrawDatasets(float w,float h) {
        PageTitle(
            L"Model Lab / Datasets",
            L"Review, approve, reject, and export the real training examples captured by SARA 1.0.15");

        const float x=kSidebar+28.0f;
        const float y=kHeader+94.0f;
        const float contentW=w-x-28.0f;
        const float gap=12.0f;

        // Shared Model Lab navigation.
        const wchar_t* tabLabels[]={
            L"Overview",L"Train",L"Datasets",L"Personas & LoRAs",
            L"Foundation Forks",L"Jobs",L"Evaluation",L"Deployment"
        };
        const wchar_t* tabIds[]={
            L"ml_overview",L"ml_train",L"ml_datasets",L"ml_personas",
            L"ml_foundations",L"ml_jobs",L"ml_evaluation",L"ml_deployment"
        };
        const float tabGap=6.0f;
        const float tabW=(contentW-tabGap*7.0f)/8.0f;
        for(int i=0;i<8;i++) {
            const float tx=x+i*(tabW+tabGap);
            const bool active=i==2;
            Rounded(tx,y,tabW,36,
                active?brush_.panel2.Get():brush_.sidebar.Get(),
                active?brush_.cyan.Get():brush_.border.Get(),7);
            TextLine(tabLabels[i],tx+5,y+2,tabW-10,30,tinyFmt_.Get(),
                active?brush_.cyan.Get():brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
            buttons_.push_back({{tx,y,tx+tabW,y+36},tabIds[i]});
        }

        const auto counts=runtime_->trainingReviews.Counts();
        const auto recent=runtime_->trainingReviews.ListRecent(8);
        const int total=counts.pending+counts.approved+counts.rejected;

        const float summaryY=y+48.0f;
        const float cardW=(contentW-gap*3.0f)/4.0f;
        struct DatasetMetric {
            const wchar_t* label;
            int value;
            const wchar_t* sub;
            ID2D1Brush* accent;
        };
        const std::wstring snapshotSub=L"Rejected "+std::to_wstring(counts.rejected);
        DatasetMetric metrics[]={
            {L"Captured",total,L"Review records",brush_.cyan.Get()},
            {L"Needs Review",counts.pending,L"Pending approval",brush_.yellow.Get()},
            {L"Approved",counts.approved,L"Export-ready",brush_.green.Get()},
            {L"Snapshots",(int)trainingData_.Snapshots().size(),snapshotSub.c_str(),brush_.blue.Get()}
        };
        for(int i=0;i<4;i++) {
            const float cx=x+i*(cardW+gap);
            Rounded(cx,summaryY,cardW,78,brush_.panel.Get(),brush_.border.Get(),9);
            target_->FillRectangle(D2D1::RectF(cx+14,summaryY+16,cx+18,summaryY+58),metrics[i].accent);
            TextLine(metrics[i].label,cx+30,summaryY+10,cardW-44,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(std::to_wstring(metrics[i].value),cx+30,summaryY+28,cardW-44,28,h1Fmt_.Get(),brush_.text.Get());
            TextLine(metrics[i].sub,cx+30,summaryY+56,cardW-44,16,tinyFmt_.Get(),metrics[i].accent);
        }

        const float bodyY=summaryY+90.0f;
        const float bodyH=std::max(430.0f,h-bodyY-26.0f);
        const float listW=contentW*0.64f-gap*0.5f;
        const float detailW=contentW-listW-gap;
        const float detailX=x+listW+gap;

        Rounded(x,bodyY,listW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Review Queue",x+16,bodyY+12,230,28,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Newest captured examples from the 1.0.15 review store",
            x+16,bodyY+40,listW-32,20,tinyFmt_.Get(),brush_.muted.Get());

        AddButton(L"training_stage",L"Stage Latest",x+listW-360,bodyY+12,102,28,true);
        AddButton(L"dataset_snapshot",L"Snapshot",x+listW-250,bodyY+12,74,28,false);
        AddButton(L"dataset_import_snapshot",L"Import",x+listW-168,bodyY+12,66,28,false);
        AddButton(L"training_export",L"JSONL",x+listW-94,bodyY+12,78,28,false);

        TextLine(L"STATUS",x+18,bodyY+72,66,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"PERSONA",x+94,bodyY+72,96,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"INPUT / CAPTURE",x+202,bodyY+72,listW-324,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"CREATED",x+listW-112,bodyY+72,94,18,tinyFmt_.Get(),brush_.muted.Get());

        auto shorten=[](std::string value,size_t maxLen) {
            for(char& ch:value) if(ch=='\n' || ch=='\r' || ch=='\t') ch=' ';
            if(value.size()>maxLen) value=value.substr(0,maxLen-3)+"...";
            return value;
        };

        float rowY=bodyY+94.0f;
        if(recent.empty()) {
            Rounded(x+14,rowY,listW-28,58,brush_.sidebar.Get(),brush_.border.Get(),7);
            TextLine(L"No training examples yet. Create a Simulation reply, then choose Stage Latest.",
                x+26,rowY+9,listW-52,40,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(const auto& item:recent) {
                const bool selected=item.id==selectedTrainingReviewId_;
                ID2D1Brush* statusBrush=
                    item.status==sentinel::simulation::TrainingReviewStatus::Approved?brush_.green.Get():
                    item.status==sentinel::simulation::TrainingReviewStatus::Rejected?brush_.red.Get():
                    brush_.yellow.Get();

                Rounded(x+14,rowY,listW-28,52,
                    selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),7);
                TextLine(Widen(sentinel::simulation::ToString(item.status)),
                    x+24,rowY+6,66,18,tinyFmt_.Get(),statusBrush);
                TextLine(Widen(shorten(item.personaName,14)),
                    x+94,rowY+6,96,18,tinyFmt_.Get(),brush_.text.Get());
                TextLine(Widen(shorten(item.inputText.empty()?item.outputText:item.inputText,72)),
                    x+202,rowY+5,listW-324,34,tinyFmt_.Get(),brush_.text.Get());
                TextLine(Widen(shorten(item.createdUtc,16)),
                    x+listW-112,rowY+6,94,18,tinyFmt_.Get(),brush_.muted.Get());
                buttons_.push_back({{x+14,rowY,x+listW-14,rowY+52},L"dataset_item:"+Widen(item.id)});
                rowY+=58.0f;
                if(rowY+52>bodyY+bodyH-10) break;
            }
        }

        Rounded(detailX,bodyY,detailW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Example Inspector",detailX+16,bodyY+12,detailW-32,28,h1Fmt_.Get(),brush_.text.Get());

        std::optional<sentinel::simulation::TrainingReviewItem> selectedItem;
        if(!selectedTrainingReviewId_.empty())
            selectedItem=runtime_->trainingReviews.Get(selectedTrainingReviewId_);
        if(!selectedItem && !recent.empty()) selectedItem=recent.front();

        if(!selectedItem) {
            TextLine(L"Select or stage an example to inspect its input, output, persona, and status.",
                detailX+16,bodyY+54,detailW-32,66,smallFmt_.Get(),brush_.muted.Get());
        } else {
            const auto& item=*selectedItem;
            ID2D1Brush* statusBrush=
                item.status==sentinel::simulation::TrainingReviewStatus::Approved?brush_.green.Get():
                item.status==sentinel::simulation::TrainingReviewStatus::Rejected?brush_.red.Get():
                brush_.yellow.Get();

            TextLine(L"Status",detailX+16,bodyY+52,64,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(sentinel::simulation::ToString(item.status)),
                detailX+86,bodyY+49,detailW-102,22,smallFmt_.Get(),statusBrush);

            TextLine(L"Persona",detailX+16,bodyY+82,64,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(item.personaName),detailX+86,bodyY+79,detailW-102,22,smallFmt_.Get(),brush_.cyan.Get());

            TextLine(L"Model",detailX+16,bodyY+112,64,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(item.modelName),detailX+86,bodyY+109,detailW-102,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Input",detailX+16,bodyY+148,detailW-32,18,tinyFmt_.Get(),brush_.muted.Get());
            Rounded(detailX+14,bodyY+168,detailW-28,92,brush_.sidebar.Get(),brush_.border.Get(),7);
            Text(item.inputText.empty()?L"(no input text)":Widen(item.inputText),
                detailX+26,bodyY+178,detailW-52,72,smallFmt_.Get(),brush_.text.Get());

            TextLine(L"Output",detailX+16,bodyY+274,detailW-32,18,tinyFmt_.Get(),brush_.muted.Get());
            Rounded(detailX+14,bodyY+294,detailW-28,112,brush_.sidebar.Get(),brush_.border.Get(),7);
            Text(Widen(item.outputText),
                detailX+26,bodyY+304,detailW-52,92,smallFmt_.Get(),brush_.text.Get());

            const float actionY=std::min(bodyY+bodyH-42.0f,bodyY+422.0f);
            AddButton(L"training_approve",L"Approve",detailX+16,actionY,76,30,true);
            AddButton(L"training_reject",L"Reject",detailX+100,actionY,70,30,false);
            AddButton(L"dataset_export_snapshot",L"Snapshot File",detailX+178,actionY,104,30,false);
        }

        if(selectedDatasetSnapshot_>=0 &&
           selectedDatasetSnapshot_<(int)trainingData_.Snapshots().size()) {
            const auto& snapshot=trainingData_.Snapshots()[(size_t)selectedDatasetSnapshot_];
            std::wstring line=L"Snapshot: "+Widen(snapshot.name)+
                L" | "+std::to_wstring(snapshot.exampleIds.size())+L" examples";
            if(!snapshot.parentId.empty()) line+=L" | parent "+Widen(snapshot.parentId);
            TextLine(line,detailX+16,bodyY+bodyH-58,detailW-32,20,tinyFmt_.Get(),brush_.cyan.Get());
            TextLine(L"Portable .sara-dataset import/export enabled; IDs are de-duplicated on repeated import.",
                detailX+16,bodyY+bodyH-36,detailW-32,20,tinyFmt_.Get(),brush_.muted.Get());
        } else {
            TextLine(L"No versioned snapshot yet. Approve examples, then choose Snapshot.",
                detailX+16,bodyY+bodyH-50,detailW-32,24,tinyFmt_.Get(),brush_.muted.Get());
        }
    }



    void ActivateSelectedPersonaLoraVersion() {
        if(selectedModelLabLoraId_<=0) {
            statusText_=L"Select a LoRA version first";
            return;
        }
        auto binding=runtime_->trainer.GetPersonaLora(selectedModelLabLoraId_);
        if(!binding) {
            statusText_=L"Selected LoRA version no longer exists";
            return;
        }
        if(!runtime_->trainer.ActivatePersonaLora(selectedModelLabLoraId_)) {
            statusText_=L"Could not activate selected LoRA version";
            return;
        }

        if(binding->personaName==simSettings_.persona.name)
            ApplyPersonaRuntimeBinding();

        statusText_=L"Activated LoRA version: "+Widen(binding->loraName);
    }

    void RollbackSelectedPersonaLoraVersion() {
        const std::string persona=
            selectedModelLabPersonaName_.empty()
                ? simSettings_.persona.name
                : selectedModelLabPersonaName_;
        auto previous=runtime_->trainer.PreviousPersonaLora(persona);
        if(!previous) {
            statusText_=L"No previous LoRA activation is available for "+Widen(persona);
            return;
        }
        if(!runtime_->trainer.RollbackPersonaLora(persona)) {
            statusText_=L"Persona LoRA rollback failed";
            return;
        }

        selectedModelLabLoraId_=previous->id;
        if(persona==simSettings_.persona.name)
            ApplyPersonaRuntimeBinding();

        statusText_=L"LoRA rollback restored: "+Widen(previous->loraName);
    }

    void CompareSelectedPersonaLoraVersion() {
        if(selectedModelLabLoraId_<=0) {
            statusText_=L"Select a LoRA version first";
            return;
        }
        auto selected=runtime_->trainer.GetPersonaLora(selectedModelLabLoraId_);
        if(!selected) {
            statusText_=L"Selected LoRA version no longer exists";
            return;
        }
        auto active=runtime_->trainer.ResolvePersonaLora(selected->personaName);
        if(!active) {
            statusText_=L"No active LoRA exists for comparison";
            return;
        }

        const auto foundationName=[&](const std::string& id) {
            auto foundation=runtime_->trainer.GetFoundation(id);
            return foundation
                ? Widen(foundation->name)+L" v"+std::to_wstring(foundation->version)
                : Widen(id);
        };

        const std::wstring message=
            L"ACTIVE\n"+
            Widen(active->loraName)+L" [#"+std::to_wstring(active->id)+L"]"+
            L"\nFoundation: "+foundationName(active->foundationId)+
            L"\nWeight: "+std::to_wstring(active->weight)+
            L"\nPath: "+Widen(active->loraPath)+
            L"\n\nSELECTED\n"+
            Widen(selected->loraName)+L" [#"+std::to_wstring(selected->id)+L"]"+
            L"\nFoundation: "+foundationName(selected->foundationId)+
            L"\nWeight: "+std::to_wstring(selected->weight)+
            L"\nPath: "+Widen(selected->loraPath);

        MessageBoxW(hwnd_,message.c_str(),L"Persona LoRA Comparison",MB_OK|MB_ICONINFORMATION);
        statusText_=L"LoRA comparison opened";
    }


    void ExportSelectedPersonaLoraManifest() {
        if(selectedModelLabLoraId_<=0) {
            statusText_=L"Select a LoRA version first";
            return;
        }
        auto binding=runtime_->trainer.GetPersonaLora(selectedModelLabLoraId_);
        if(!binding) {
            statusText_=L"Selected LoRA version no longer exists";
            return;
        }

        const auto manifest=runtime_->trainer.BuildPersonaLoraManifest(selectedModelLabLoraId_);
        if(manifest.empty()) {
            statusText_=L"Could not build LoRA metadata manifest";
            return;
        }

        wchar_t file[MAX_PATH]{};
        auto defaultName=Widen(binding->personaName+"-"+binding->loraName+".sara-lora.json");
        for(auto& ch:defaultName) {
            if(ch==L'\\' || ch==L'/' || ch==L':' || ch==L'*' || ch==L'?' ||
               ch==L'"' || ch==L'<' || ch==L'>' || ch==L'|') ch=L'-';
        }
        wcsncpy_s(file,defaultName.c_str(),_TRUNCATE);

        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn);
        ofn.hwndOwner=hwnd_;
        ofn.lpstrFile=file;
        ofn.nMaxFile=MAX_PATH;
        ofn.lpstrFilter=L"SARA LoRA Metadata\0*.json\0All Files\0*.*\0\0";
        ofn.lpstrDefExt=L"json";
        ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
        if(!GetSaveFileNameW(&ofn)) return;

        std::ofstream out(std::filesystem::path(file),std::ios::trunc);
        out<<manifest;
        statusText_=L"LoRA metadata exported";
    }

    void DrawPersonasLoras(float w,float h) {
        PageTitle(
            L"Model Lab / Personas & LoRAs",
            L"Manage the real saved SARA personas and their active LoRA bindings from the recovered 1.0.15 backend");

        const float x=kSidebar+28.0f;
        const float y=kHeader+94.0f;
        const float contentW=w-x-28.0f;
        const float gap=12.0f;

        const wchar_t* tabLabels[]={
            L"Overview",L"Train",L"Datasets",L"Personas & LoRAs",
            L"Foundation Forks",L"Jobs",L"Evaluation",L"Deployment"
        };
        const wchar_t* tabIds[]={
            L"ml_overview",L"ml_train",L"ml_datasets",L"ml_personas",
            L"ml_foundations",L"ml_jobs",L"ml_evaluation",L"ml_deployment"
        };
        const float tabGap=6.0f;
        const float tabW=(contentW-tabGap*7.0f)/8.0f;
        for(int i=0;i<8;i++) {
            const float tx=x+i*(tabW+tabGap);
            const bool active=i==3;
            Rounded(tx,y,tabW,36,
                active?brush_.panel2.Get():brush_.sidebar.Get(),
                active?brush_.cyan.Get():brush_.border.Get(),7);
            TextLine(tabLabels[i],tx+5,y+2,tabW-10,30,tinyFmt_.Get(),
                active?brush_.cyan.Get():brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
            buttons_.push_back({{tx,y,tx+tabW,y+36},tabIds[i]});
        }

        const auto names=runtime_->personaProfiles.ListNames();
        if(!names.empty()) {
            const bool selectedStillExists=std::find(
                names.begin(),names.end(),selectedModelLabPersonaName_)!=names.end();
            if(!selectedStillExists) {
                const auto current=std::find(names.begin(),names.end(),simSettings_.persona.name);
                selectedModelLabPersonaName_=current!=names.end()?*current:names.front();
            }
        } else {
            selectedModelLabPersonaName_.clear();
        }

        size_t loraCount=0;
        for(const auto& name:names)
            if(runtime_->trainer.ResolvePersonaLora(name)) ++loraCount;
        const auto foundations=runtime_->trainer.ListFoundations();

        const float summaryY=y+48.0f;
        const float cardW=(contentW-gap*3.0f)/4.0f;
        struct PersonaMetric {
            const wchar_t* label;
            std::wstring value;
            std::wstring sub;
            ID2D1Brush* accent;
            IconKind icon;
        };
        PersonaMetric metrics[]={
            {L"Saved Personas",std::to_wstring(names.size()),L"Reusable profiles",brush_.cyan.Get(),IconKind::Folder},
            {L"Assigned LoRAs",std::to_wstring(loraCount),L"Active persona adapters",brush_.green.Get(),IconKind::Chain},
            {L"Foundations",std::to_wstring(foundations.size()),L"Available model families",brush_.blue.Get(),IconKind::Database},
            {L"Active Persona",Widen(simSettings_.persona.name),L"Loaded in runtime",brush_.yellow.Get(),IconKind::Chat}
        };
        for(int i=0;i<4;i++) {
            const float cx=x+i*(cardW+gap);
            Rounded(cx,summaryY,cardW,82,brush_.panel.Get(),brush_.border.Get(),9);
            Rounded(cx+14,summaryY+16,38,38,brush_.panel2.Get(),nullptr,9);
            DrawIcon(metrics[i].icon,cx+22,summaryY+24,22,metrics[i].accent);
            TextLine(metrics[i].label,cx+62,summaryY+10,cardW-74,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(metrics[i].value,cx+62,summaryY+28,cardW-74,26,smallFmt_.Get(),brush_.text.Get());
            TextLine(metrics[i].sub,cx+14,summaryY+60,cardW-28,16,tinyFmt_.Get(),metrics[i].accent);
        }

        const float bodyY=summaryY+94.0f;
        const float bodyH=std::max(430.0f,h-bodyY-26.0f);
        const float listW=contentW*0.63f-gap*0.5f;
        const float detailW=contentW-listW-gap;
        const float detailX=x+listW+gap;

        Rounded(x,bodyY,listW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Persona Library",x+16,bodyY+12,240,28,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Saved profiles and their currently assigned runtime adapters",
            x+16,bodyY+40,listW-32,20,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"persona_edit_selected",L"Open Profile Editor",x+listW-144,bodyY+12,128,28,false);

        TextLine(L"NAME",x+18,bodyY+72,118,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"AGE",x+142,bodyY+72,38,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"PERSONALITY / STYLE",x+190,bodyY+72,160,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"ASSIGNED LORA",x+360,bodyY+72,listW-474,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STATUS",x+listW-104,bodyY+72,86,18,tinyFmt_.Get(),brush_.muted.Get());

        float rowY=bodyY+94.0f;
        if(names.empty()) {
            Rounded(x+14,rowY,listW-28,58,brush_.sidebar.Get(),brush_.border.Get(),7);
            TextLine(L"No saved personas yet. Open Personas to create the first reusable profile.",
                x+26,rowY+9,listW-52,40,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(const auto& name:names) {
                auto profile=runtime_->personaProfiles.Load(name);
                if(!profile) continue;
                auto lora=runtime_->trainer.ResolvePersonaLora(name);
                const bool selected=name==selectedModelLabPersonaName_;
                const bool active=name==simSettings_.persona.name;

                Rounded(x+14,rowY,listW-28,52,
                    selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),7);
                TextLine(Widen(name),x+24,rowY+5,112,20,smallFmt_.Get(),brush_.text.Get());
                TextLine(std::to_wstring(profile->age),x+142,rowY+5,38,20,tinyFmt_.Get(),brush_.cyan.Get());

                std::wstring style=Widen(profile->personality);
                if(!profile->writingStyle.empty()) style+=L" / "+Widen(profile->writingStyle);
                TextLine(style,x+190,rowY+5,160,20,tinyFmt_.Get(),brush_.text.Get());

                TextLine(lora?Widen(lora->loraName):L"None",
                    x+360,rowY+5,listW-474,20,tinyFmt_.Get(),
                    lora?brush_.green.Get():brush_.muted.Get());

                TextLine(active?L"ACTIVE":L"SAVED",
                    x+listW-104,rowY+5,86,20,tinyFmt_.Get(),
                    active?brush_.green.Get():brush_.muted.Get(),
                    DWRITE_TEXT_ALIGNMENT_TRAILING);

                std::wstring detail=Widen(profile->location);
                if(detail.size()>52) detail=detail.substr(0,49)+L"...";
                TextLine(detail,x+24,rowY+28,listW-128,18,tinyFmt_.Get(),brush_.muted.Get());

                buttons_.push_back({{x+14,rowY,x+listW-14,rowY+52},L"persona_row:"+Widen(name)});
                rowY+=58.0f;
                if(rowY+52>bodyY+bodyH-10) break;
            }
        }

        Rounded(detailX,bodyY,detailW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Persona Details",detailX+16,bodyY+12,detailW-32,28,h1Fmt_.Get(),brush_.text.Get());

        std::optional<sentinel::simulation::PersonaProfile> selectedProfile;
        if(!selectedModelLabPersonaName_.empty())
            selectedProfile=runtime_->personaProfiles.Load(selectedModelLabPersonaName_);

        if(!selectedProfile) {
            TextLine(L"Select a persona to inspect its profile and LoRA assignment.",
                detailX+16,bodyY+54,detailW-32,50,smallFmt_.Get(),brush_.muted.Get());
        } else {
            const auto& p=*selectedProfile;
            auto activeLora=runtime_->trainer.ResolvePersonaLora(p.name);
            auto loras=runtime_->trainer.ListPersonaLoras(p.name,4);
            if(!loras.empty()) {
                const bool selectedExists=std::any_of(loras.begin(),loras.end(),[&](const auto& item){
                    return item.id==selectedModelLabLoraId_;
                });
                if(!selectedExists)
                    selectedModelLabLoraId_=activeLora?activeLora->id:loras.front().id;
            } else {
                selectedModelLabLoraId_=0;
            }

            TextLine(Widen(p.name),detailX+16,bodyY+48,detailW-32,30,h1Fmt_.Get(),brush_.cyan.Get());
            TextLine(L"Age "+std::to_wstring(p.age)+L"  |  "+Widen(p.location),
                detailX+16,bodyY+78,detailW-32,20,tinyFmt_.Get(),brush_.muted.Get());

            TextLine(L"Personality",detailX+16,bodyY+112,78,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(p.personality),detailX+102,bodyY+109,detailW-118,22,smallFmt_.Get(),brush_.text.Get());

            TextLine(L"Social style",detailX+16,bodyY+140,78,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(p.socialStyle),detailX+102,bodyY+137,detailW-118,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Writing",detailX+16,bodyY+168,78,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(p.writingStyle)+L" / "+Widen(p.grammarQuality),
                detailX+102,bodyY+165,detailW-118,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Communication",detailX+16,bodyY+196,78,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(p.communicationLevel),
                detailX+102,bodyY+193,detailW-118,22,tinyFmt_.Get(),brush_.text.Get());

            const float loraY=bodyY+232.0f;
            target_->DrawLine(
                D2D1::Point2F(detailX+16,loraY-10),
                D2D1::Point2F(detailX+detailW-16,loraY-10),
                brush_.border.Get(),1.0f);
            TextLine(L"LoRA Assignment",detailX+16,loraY,detailW-32,24,smallFmt_.Get(),brush_.text.Get());

            if(activeLora) {
                TextLine(Widen(activeLora->loraName),detailX+16,loraY+30,detailW-32,22,smallFmt_.Get(),brush_.green.Get());
                auto foundation=runtime_->trainer.GetFoundation(activeLora->foundationId);
                TextLine(L"Foundation: "+(foundation?Widen(foundation->name):Widen(activeLora->foundationId)),
                    detailX+16,loraY+55,detailW-32,20,tinyFmt_.Get(),brush_.muted.Get());
                TextLine(L"Weight: "+std::to_wstring(activeLora->weight),
                    detailX+16,loraY+76,detailW-32,18,tinyFmt_.Get(),brush_.muted.Get());
            } else {
                TextLine(L"No active LoRA assigned",detailX+16,loraY+32,detailW-32,22,smallFmt_.Get(),brush_.muted.Get());
            }

            const float historyY=loraY+104.0f;
            TextLine(L"Adapter History",detailX+16,historyY,detailW-32,22,smallFmt_.Get(),brush_.text.Get());
            float ly=historyY+28.0f;
            if(loras.empty()) {
                TextLine(L"No LoRA versions recorded for this persona.",
                    detailX+16,ly,detailW-32,20,tinyFmt_.Get(),brush_.muted.Get());
            } else {
                for(const auto& lora:loras) {
                    const bool selectedLora=lora.id==selectedModelLabLoraId_;
                    Rounded(detailX+14,ly,detailW-28,28,
                        selectedLora?brush_.panel2.Get():brush_.sidebar.Get(),
                        selectedLora?brush_.cyan.Get():brush_.border.Get(),6);
                    TextLine(Widen(lora.loraName),detailX+24,ly+4,detailW-118,18,tinyFmt_.Get(),brush_.text.Get());
                    TextLine(lora.active?L"ACTIVE":L"INACTIVE",
                        detailX+detailW-96,ly+4,72,18,tinyFmt_.Get(),
                        lora.active?brush_.green.Get():brush_.muted.Get(),
                        DWRITE_TEXT_ALIGNMENT_TRAILING);
                    buttons_.push_back({
                        {detailX+14,ly,detailX+detailW-14,ly+28},
                        L"persona_lora_row:"+std::to_wstring(lora.id)
                    });
                    ly+=34.0f;
                    if(ly+28>bodyY+bodyH-94) break;
                }
            }

            const float actionY=bodyY+bodyH-42.0f;
            const auto previousLora=runtime_->trainer.PreviousPersonaLora(p.name);
            if(selectedModelLabLoraId_>0) {
                auto selectedLora=runtime_->trainer.GetPersonaLora(selectedModelLabLoraId_);
                if(selectedLora && !selectedLora->active)
                    AddButton(L"persona_lora_activate",L"Activate",detailX+16,actionY-36,72,28,false);
                if(activeLora && selectedLora && activeLora->id!=selectedLora->id)
                    AddButton(L"persona_lora_compare",L"Compare",detailX+96,actionY-36,72,28,false);
                AddButton(L"persona_lora_export",L"Export",detailX+176,actionY-36,72,28,false);
            }
            if(previousLora)
                AddButton(L"persona_lora_rollback",L"Rollback",detailX+16,actionY-72,78,28,false);

            AddButton(L"persona_use_selected",L"Use Persona",detailX+16,actionY,92,30,true);
            AddButton(L"persona_edit_selected",L"Edit",detailX+116,actionY,66,30,false);
            AddButton(L"persona_train_selected",L"Train LoRA",detailX+190,actionY,88,30,false);
        }
    }


    void ActivateSelectedFoundationFromLab() {
        if(selectedModelLabFoundationId_.empty()) {
            statusText_=L"Select a foundation first";
            return;
        }
        auto foundation=runtime_->trainer.GetFoundation(selectedModelLabFoundationId_);
        if(!foundation) {
            statusText_=L"Selected foundation no longer exists";
            return;
        }
        if(foundation->status!="APPROVED" && foundation->status!="ACTIVE") {
            statusText_=L"Foundation must be approved before activation";
            return;
        }
        if(foundation->runtimeGgufPath.empty()) {
            statusText_=L"Foundation has no deployable GGUF runtime yet";
            return;
        }
        if(!runtime_->trainer.ActivateFoundation(foundation->id)) {
            statusText_=L"Foundation activation failed";
            return;
        }
        RefreshTrainerFoundationList(foundation->id);
        ApplyPersonaRuntimeBinding();
        statusText_=L"Foundation activated: "+Widen(foundation->name);
    }

    void RollbackFoundationFromLab() {
        auto previous=runtime_->trainer.PreviousFoundation();
        if(!previous) {
            statusText_=L"No previous foundation activation is available";
            return;
        }
        if(previous->runtimeGgufPath.empty()) {
            statusText_=L"Previous foundation has no deployable GGUF runtime";
            return;
        }
        if(!runtime_->trainer.RollbackFoundation()) {
            statusText_=L"Foundation rollback failed";
            return;
        }
        RefreshTrainerFoundationList(previous->id);
        selectedModelLabFoundationId_=previous->id;
        ApplyPersonaRuntimeBinding();
        statusText_=L"Foundation rollback restored "+Widen(previous->name);
    }

    void CompareSelectedFoundationToActive() {
        if(selectedModelLabFoundationId_.empty()) {
            statusText_=L"Select a foundation first";
            return;
        }
        auto selected=runtime_->trainer.GetFoundation(selectedModelLabFoundationId_);
        if(!selected) return;

        const auto foundations=runtime_->trainer.ListFoundations();
        auto active=std::find_if(foundations.begin(),foundations.end(),[](const auto& item){
            return item.status=="ACTIVE";
        });
        if(active==foundations.end()) {
            statusText_=L"No active foundation is available for comparison";
            return;
        }

        const auto display=[](const std::string& value,const wchar_t* emptyValue){
            return value.empty()?std::wstring(emptyValue):Widen(value);
        };
        const std::wstring message=
            L"ACTIVE\n"+
            Widen(active->name)+L" v"+std::to_wstring(active->version)+
            L" | "+Widen(active->status)+
            L"\nSource: "+display(active->sourceModel,L"(none)")+
            L"\nTrainable: "+display(active->trainableSourcePath,L"(none)")+
            L"\nRuntime: "+display(active->runtimeGgufPath,L"(none)")+
            L"\n\nSELECTED\n"+
            Widen(selected->name)+L" v"+std::to_wstring(selected->version)+
            L" | "+Widen(selected->status)+
            L"\nParent: "+display(selected->parentId,L"Immutable base")+
            L"\nSource: "+display(selected->sourceModel,L"(none)")+
            L"\nTrainable: "+display(selected->trainableSourcePath,L"(none)")+
            L"\nRuntime: "+display(selected->runtimeGgufPath,L"(none)");
        MessageBoxW(hwnd_,message.c_str(),L"Foundation Comparison",MB_OK|MB_ICONINFORMATION);
        statusText_=L"Foundation comparison opened";
    }

    void DrawFoundationForks(float w,float h) {
        PageTitle(
            L"Model Lab / Foundation Forks",
            L"Inspect the recovered SARA foundation lineage and create/train forks without replacing the original base");

        const float x=kSidebar+28.0f;
        const float y=kHeader+94.0f;
        const float contentW=w-x-28.0f;
        const float gap=12.0f;

        const wchar_t* tabLabels[]={
            L"Overview",L"Train",L"Datasets",L"Personas & LoRAs",
            L"Foundation Forks",L"Jobs",L"Evaluation",L"Deployment"
        };
        const wchar_t* tabIds[]={
            L"ml_overview",L"ml_train",L"ml_datasets",L"ml_personas",
            L"ml_foundations",L"ml_jobs",L"ml_evaluation",L"ml_deployment"
        };
        const float tabGap=6.0f;
        const float tabW=(contentW-tabGap*7.0f)/8.0f;
        for(int i=0;i<8;i++) {
            const float tx=x+i*(tabW+tabGap);
            const bool active=i==4;
            Rounded(tx,y,tabW,36,
                active?brush_.panel2.Get():brush_.sidebar.Get(),
                active?brush_.cyan.Get():brush_.border.Get(),7);
            TextLine(tabLabels[i],tx+5,y+2,tabW-10,30,tinyFmt_.Get(),
                active?brush_.cyan.Get():brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
            buttons_.push_back({{tx,y,tx+tabW,y+36},tabIds[i]});
        }

        auto foundations=runtime_->trainer.ListFoundations();
        if(!foundations.empty()) {
            const bool selectedExists=std::any_of(
                foundations.begin(),foundations.end(),[&](const auto& item){
                    return item.id==selectedModelLabFoundationId_;
                });
            if(!selectedExists) {
                auto active=std::find_if(foundations.begin(),foundations.end(),[](const auto& item){
                    return item.status=="ACTIVE";
                });
                selectedModelLabFoundationId_=(active!=foundations.end()?active:foundations.begin())->id;
            }
        } else {
            selectedModelLabFoundationId_.clear();
        }

        size_t activeCount=0,draftCount=0;
        for(const auto& item:foundations) {
            if(item.status=="ACTIVE") ++activeCount;
            if(item.status=="DRAFT" || item.status=="TRAINING") ++draftCount;
        }
        const auto jobs=runtime_->trainer.ListJobs(100);
        size_t foundationJobs=0;
        for(const auto& job:jobs)
            if(job.mode==sentinel::simulation::TrainingMode::FoundationSft) ++foundationJobs;

        const float summaryY=y+48.0f;
        const float cardW=(contentW-gap*3.0f)/4.0f;
        struct MetricRow { const wchar_t* label; std::wstring value; const wchar_t* sub; ID2D1Brush* accent; };
        MetricRow metrics[]={
            {L"Foundations",std::to_wstring(foundations.size()),L"Base + forks",brush_.cyan.Get()},
            {L"Active",std::to_wstring(activeCount),L"Runtime foundation",brush_.green.Get()},
            {L"Draft / Training",std::to_wstring(draftCount),L"Work in progress",brush_.yellow.Get()},
            {L"Foundation Jobs",std::to_wstring(foundationJobs),L"Queued history",brush_.blue.Get()}
        };
        for(int i=0;i<4;i++) {
            const float cx=x+i*(cardW+gap);
            Rounded(cx,summaryY,cardW,78,brush_.panel.Get(),brush_.border.Get(),9);
            target_->FillRectangle(D2D1::RectF(cx+14,summaryY+16,cx+18,summaryY+58),metrics[i].accent);
            TextLine(metrics[i].label,cx+30,summaryY+10,cardW-44,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(metrics[i].value,cx+30,summaryY+28,cardW-44,28,h1Fmt_.Get(),brush_.text.Get());
            TextLine(metrics[i].sub,cx+30,summaryY+56,cardW-44,16,tinyFmt_.Get(),metrics[i].accent);
        }

        const float bodyY=summaryY+90.0f;
        const float bodyH=std::max(430.0f,h-bodyY-26.0f);
        const float listW=contentW*0.62f-gap*0.5f;
        const float detailW=contentW-listW-gap;
        const float detailX=x+listW+gap;

        Rounded(x,bodyY,listW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Foundation Lineage",x+16,bodyY+12,240,28,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"The original base remains intact; forks are separate versioned records.",
            x+16,bodyY+40,listW-32,20,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"foundation_new_fork",L"Create Fork",x+listW-112,bodyY+12,96,28,true);

        TextLine(L"NAME",x+18,bodyY+72,160,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"VERSION",x+190,bodyY+72,64,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STATUS",x+266,bodyY+72,82,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"PARENT / SOURCE",x+360,bodyY+72,listW-378,18,tinyFmt_.Get(),brush_.muted.Get());

        float rowY=bodyY+94.0f;
        if(foundations.empty()) {
            Rounded(x+14,rowY,listW-28,58,brush_.sidebar.Get(),brush_.border.Get(),7);
            TextLine(L"No foundation records are available.",x+26,rowY+12,listW-52,28,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(const auto& item:foundations) {
                const bool selected=item.id==selectedModelLabFoundationId_;
                ID2D1Brush* stateBrush=
                    item.status=="ACTIVE"?brush_.green.Get():
                    item.status=="TRAINING"?brush_.yellow.Get():brush_.cyan.Get();

                Rounded(x+14,rowY,listW-28,52,
                    selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),7);
                TextLine(Widen(item.name),x+24,rowY+5,154,20,smallFmt_.Get(),brush_.text.Get());
                TextLine(L"v"+std::to_wstring(item.version),x+190,rowY+5,64,20,tinyFmt_.Get(),brush_.cyan.Get());
                TextLine(Widen(item.status),x+266,rowY+5,82,20,tinyFmt_.Get(),stateBrush);

                std::wstring source=item.parentId.empty()
                    ? L"BASE | "+Widen(item.sourceModel)
                    : L"Fork of "+Widen(item.parentId);
                if(source.size()>48) source=source.substr(0,45)+L"...";
                TextLine(source,x+360,rowY+5,listW-378,20,tinyFmt_.Get(),brush_.muted.Get());

                std::wstring path=item.runtimeGgufPath.empty()
                    ? Widen(item.trainableSourcePath)
                    : Widen(item.runtimeGgufPath);
                if(path.size()>72) path=path.substr(0,69)+L"...";
                TextLine(path,x+24,rowY+28,listW-48,18,tinyFmt_.Get(),brush_.muted.Get());

                buttons_.push_back({{x+14,rowY,x+listW-14,rowY+52},L"foundation_row:"+Widen(item.id)});
                rowY+=58.0f;
                if(rowY+52>bodyY+bodyH-10) break;
            }
        }

        Rounded(detailX,bodyY,detailW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Foundation Inspector",detailX+16,bodyY+12,detailW-32,28,h1Fmt_.Get(),brush_.text.Get());

        std::optional<sentinel::simulation::ModelFoundation> selected;
        if(!selectedModelLabFoundationId_.empty())
            selected=runtime_->trainer.GetFoundation(selectedModelLabFoundationId_);

        if(!selected) {
            TextLine(L"Select a foundation to inspect its source and fork metadata.",
                detailX+16,bodyY+54,detailW-32,50,smallFmt_.Get(),brush_.muted.Get());
        } else {
            const auto& item=*selected;
            TextLine(Widen(item.name),detailX+16,bodyY+48,detailW-32,30,h1Fmt_.Get(),brush_.cyan.Get());
            TextLine(L"Version "+std::to_wstring(item.version)+L"  |  "+Widen(item.status),
                detailX+16,bodyY+79,detailW-32,20,tinyFmt_.Get(),
                item.status=="ACTIVE"?brush_.green.Get():brush_.muted.Get());

            TextLine(L"Source model",detailX+16,bodyY+116,88,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(item.sourceModel),detailX+112,bodyY+113,detailW-128,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Parent",detailX+16,bodyY+146,88,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(item.parentId.empty()?L"Immutable base":Widen(item.parentId),
                detailX+112,bodyY+143,detailW-128,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Trainable",detailX+16,bodyY+176,88,18,tinyFmt_.Get(),brush_.muted.Get());
            Text(Widen(item.trainableSourcePath.empty()?"Not configured":item.trainableSourcePath),
                detailX+112,bodyY+173,detailW-128,42,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Runtime",detailX+16,bodyY+226,88,18,tinyFmt_.Get(),brush_.muted.Get());
            Text(Widen(item.runtimeGgufPath.empty()?"No GGUF assigned":item.runtimeGgufPath),
                detailX+112,bodyY+223,detailW-128,42,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Notes",detailX+16,bodyY+276,88,18,tinyFmt_.Get(),brush_.muted.Get());
            Text(Widen(item.notes),detailX+16,bodyY+298,detailW-32,64,tinyFmt_.Get(),brush_.muted.Get());

            const float actionY=bodyY+bodyH-42.0f;
            const float upperY=actionY-36.0f;
            const auto previousFoundation=runtime_->trainer.PreviousFoundation();
            auto activeFoundation=std::find_if(foundations.begin(),foundations.end(),[](const auto& f){
                return f.status=="ACTIVE";
            });

            if(item.status=="APPROVED" && !item.runtimeGgufPath.empty())
                AddButton(L"foundation_activate_selected",L"Activate",detailX+16,upperY,78,28,true);
            if(item.status=="ACTIVE" && previousFoundation)
                AddButton(L"foundation_rollback",L"Rollback",detailX+102,upperY,78,28,false);
            if(activeFoundation!=foundations.end() && activeFoundation->id!=item.id)
                AddButton(L"foundation_compare",L"Compare",detailX+188,upperY,78,28,false);

            AddButton(L"foundation_open_trainer",L"Train",detailX+16,actionY,78,30,true);
            if(item.status!="ACTIVE")
                AddButton(L"foundation_approve_selected",L"Approve",detailX+102,actionY,78,30,false);
            AddButton(L"foundation_new_fork",L"Child Fork",detailX+188,actionY,78,30,false);
        }
    }

    void DrawJobs(float w,float h) {
        PageTitle(
            L"Model Lab / Jobs",
            L"Inspect and run the real training jobs queued by the recovered SARA 1.0.15 Trainer");

        const float x=kSidebar+28.0f;
        const float y=kHeader+94.0f;
        const float contentW=w-x-28.0f;
        const float gap=12.0f;

        const wchar_t* tabLabels[]={
            L"Overview",L"Train",L"Datasets",L"Personas & LoRAs",
            L"Foundation Forks",L"Jobs",L"Evaluation",L"Deployment"
        };
        const wchar_t* tabIds[]={
            L"ml_overview",L"ml_train",L"ml_datasets",L"ml_personas",
            L"ml_foundations",L"ml_jobs",L"ml_evaluation",L"ml_deployment"
        };
        const float tabGap=6.0f;
        const float tabW=(contentW-tabGap*7.0f)/8.0f;
        for(int i=0;i<8;i++) {
            const float tx=x+i*(tabW+tabGap);
            const bool active=i==5;
            Rounded(tx,y,tabW,36,
                active?brush_.panel2.Get():brush_.sidebar.Get(),
                active?brush_.cyan.Get():brush_.border.Get(),7);
            TextLine(tabLabels[i],tx+5,y+2,tabW-10,30,tinyFmt_.Get(),
                active?brush_.cyan.Get():brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
            buttons_.push_back({{tx,y,tx+tabW,y+36},tabIds[i]});
        }

        const auto jobs=runtime_->trainer.ListJobs(100);
        if(!jobs.empty()) {
            const bool selectedExists=std::any_of(jobs.begin(),jobs.end(),[&](const auto& item){
                return item.id==selectedModelLabJobId_;
            });
            if(!selectedExists) selectedModelLabJobId_=jobs.front().id;
        } else selectedModelLabJobId_.clear();

        size_t queued=0,running=0,completed=0,failed=0,cancelled=0;
        for(const auto& job:jobs) {
            if(job.state=="QUEUED") ++queued;
            else if(job.state=="RUNNING") ++running;
            else if(job.state=="COMPLETED") ++completed;
            else if(job.state=="FAILED") ++failed;
            else if(job.state=="CANCELLED") ++cancelled;
        }

        const float summaryY=y+48.0f;
        const float cardW=(contentW-gap*3.0f)/4.0f;
        struct JobMetric { const wchar_t* label; size_t value; const wchar_t* sub; ID2D1Brush* accent; };
        JobMetric metrics[]={
            {L"Queued",queued,L"Waiting to run",brush_.yellow.Get()},
            {L"Running",running,L"Worker active",brush_.cyan.Get()},
            {L"Completed",completed,L"Finished jobs",brush_.green.Get()},
            {L"Failed / Cancelled",failed+cancelled,L"Retryable history",brush_.red.Get()}
        };
        for(int i=0;i<4;i++) {
            const float cx=x+i*(cardW+gap);
            Rounded(cx,summaryY,cardW,78,brush_.panel.Get(),brush_.border.Get(),9);
            target_->FillRectangle(D2D1::RectF(cx+14,summaryY+16,cx+18,summaryY+58),metrics[i].accent);
            TextLine(metrics[i].label,cx+30,summaryY+10,cardW-44,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(std::to_wstring(metrics[i].value),cx+30,summaryY+28,cardW-44,28,h1Fmt_.Get(),brush_.text.Get());
            TextLine(metrics[i].sub,cx+30,summaryY+56,cardW-44,16,tinyFmt_.Get(),metrics[i].accent);
        }

        const float bodyY=summaryY+90.0f;
        const float bodyH=std::max(430.0f,h-bodyY-26.0f);
        const float listW=contentW*0.64f-gap*0.5f;
        const float detailW=contentW-listW-gap;
        const float detailX=x+listW+gap;

        Rounded(x,bodyY,listW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Queue & History",x+16,bodyY+12,190,28,h1Fmt_.Get(),brush_.text.Get());
        if(running>0)
            AddButton(L"job_recover_stale",L"Recover Stale",x+listW-210,bodyY+12,100,28,false);
        AddButton(L"job_open_trainer",L"New Job",x+listW-102,bodyY+12,86,28,true);

        TextLine(L"TARGET",x+18,bodyY+58,140,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"MODE",x+168,bodyY+58,112,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"PERSONA",x+290,bodyY+58,100,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STATE",x+400,bodyY+58,90,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"PROGRESS",x+listW-102,bodyY+58,84,18,tinyFmt_.Get(),brush_.muted.Get());

        float rowY=bodyY+80.0f;
        if(jobs.empty()) {
            Rounded(x+14,rowY,listW-28,58,brush_.sidebar.Get(),brush_.border.Get(),7);
            TextLine(L"No training jobs have been queued. Open Train to create one.",
                x+26,rowY+12,listW-52,28,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(const auto& job:jobs) {
                const bool selected=job.id==selectedModelLabJobId_;
                ID2D1Brush* stateBrush=
                    job.state=="COMPLETED"?brush_.green.Get():
                    job.state=="FAILED"?brush_.red.Get():
                    job.state=="CANCELLED"?brush_.muted.Get():
                    job.state=="RUNNING"?brush_.cyan.Get():brush_.yellow.Get();

                Rounded(x+14,rowY,listW-28,48,
                    selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),7);
                TextLine(Widen(job.targetName),x+24,rowY+5,134,18,tinyFmt_.Get(),brush_.text.Get());
                TextLine(Widen(sentinel::simulation::ToString(job.mode)),x+168,rowY+5,112,18,tinyFmt_.Get(),brush_.muted.Get());
                TextLine(Widen(job.personaName),x+290,rowY+5,100,18,tinyFmt_.Get(),brush_.text.Get());
                TextLine(Widen(job.state),x+400,rowY+5,90,18,tinyFmt_.Get(),stateBrush);
                TextLine(std::to_wstring(job.progress)+L"%",
                    x+listW-102,rowY+5,84,18,tinyFmt_.Get(),stateBrush,DWRITE_TEXT_ALIGNMENT_TRAILING);

                std::wstring secondLine=Widen(job.createdUtc);
                if(!job.datasetPath.empty()) secondLine+=L"  |  "+Widen(job.datasetPath);
                if(secondLine.size()>88) secondLine=secondLine.substr(0,85)+L"...";
                TextLine(secondLine,x+24,rowY+26,listW-48,16,tinyFmt_.Get(),brush_.muted.Get());

                buttons_.push_back({{x+14,rowY,x+listW-14,rowY+48},L"job_row:"+Widen(job.id)});
                rowY+=54.0f;
                if(rowY+48>bodyY+bodyH-10) break;
            }
        }

        Rounded(detailX,bodyY,detailW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Job Inspector",detailX+16,bodyY+12,detailW-32,28,h1Fmt_.Get(),brush_.text.Get());

        std::optional<sentinel::simulation::TrainerJobRecord> selected;
        for(const auto& job:jobs) if(job.id==selectedModelLabJobId_) { selected=job; break; }

        if(!selected) {
            TextLine(L"Select a job to inspect its mode, dataset, output, and state.",
                detailX+16,bodyY+54,detailW-32,50,smallFmt_.Get(),brush_.muted.Get());
        } else {
            const auto& job=*selected;
            ID2D1Brush* stateBrush=
                job.state=="COMPLETED"?brush_.green.Get():
                job.state=="FAILED"?brush_.red.Get():
                job.state=="RUNNING"?brush_.cyan.Get():brush_.yellow.Get();

            TextLine(Widen(job.targetName),detailX+16,bodyY+48,detailW-32,30,h1Fmt_.Get(),brush_.cyan.Get());
            TextLine(Widen(job.state)+L"  |  "+std::to_wstring(job.progress)+L"%",
                detailX+16,bodyY+79,detailW-32,20,tinyFmt_.Get(),stateBrush);
            if(job.state=="RUNNING") {
                TextLine(
                    L"Heartbeat "+(job.heartbeatUtc.empty()?L"pending":Widen(job.heartbeatUtc))+
                    L" | PID "+std::to_wstring(job.workerPid),
                    detailX+16,bodyY+98,detailW-32,16,tinyFmt_.Get(),brush_.muted.Get());
            }

            TextLine(L"Mode",detailX+16,bodyY+116,76,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(sentinel::simulation::ToString(job.mode)),
                detailX+100,bodyY+113,detailW-116,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Persona",detailX+16,bodyY+146,76,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(job.personaName),detailX+100,bodyY+143,detailW-116,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Foundation",detailX+16,bodyY+176,76,18,tinyFmt_.Get(),brush_.muted.Get());
            auto foundation=runtime_->trainer.GetFoundation(job.foundationId);
            TextLine(foundation?Widen(foundation->name):Widen(job.foundationId),
                detailX+100,bodyY+173,detailW-116,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Created",detailX+16,bodyY+210,76,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(job.createdUtc),detailX+100,bodyY+207,detailW-116,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Started",detailX+16,bodyY+236,76,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(job.startedUtc.empty()?L"-":Widen(job.startedUtc),
                detailX+100,bodyY+233,detailW-116,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Finished",detailX+16,bodyY+262,76,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(job.completedUtc.empty()?L"-":Widen(job.completedUtc),
                detailX+100,bodyY+259,detailW-116,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Dataset",detailX+16,bodyY+292,76,18,tinyFmt_.Get(),brush_.muted.Get());
            Rounded(detailX+14,bodyY+312,detailW-28,42,brush_.sidebar.Get(),brush_.border.Get(),6);
            Text(Widen(job.datasetPath.empty()?"Not specified":job.datasetPath),
                detailX+24,bodyY+319,detailW-48,28,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Output",detailX+16,bodyY+362,76,18,tinyFmt_.Get(),brush_.muted.Get());
            Rounded(detailX+14,bodyY+382,detailW-28,42,brush_.sidebar.Get(),brush_.border.Get(),6);
            Text(Widen(job.outputPath.empty()?"Not specified":job.outputPath),
                detailX+24,bodyY+389,detailW-48,28,tinyFmt_.Get(),brush_.text.Get());

            if(!job.errorText.empty() && bodyH>500.0f) {
                std::wstring error=Widen(job.errorText);
                if(error.size()>120) error=error.substr(0,117)+L"...";
                TextLine(L"Worker error",detailX+16,bodyY+432,detailW-32,18,tinyFmt_.Get(),brush_.red.Get());
                Text(error,detailX+16,bodyY+452,detailW-32,48,tinyFmt_.Get(),brush_.red.Get());
            }

            const float actionY=bodyY+bodyH-42.0f;
            float jobActionX=detailX+16.0f;
            if(job.state=="QUEUED") {
                AddButton(L"job_run_selected",L"Run",jobActionX,actionY,62,30,true);
                jobActionX+=70.0f;
                AddButton(L"job_cancel_selected",L"Cancel",jobActionX,actionY,66,30,false);
                jobActionX+=74.0f;
            } else if(job.state=="FAILED" || job.state=="CANCELLED") {
                AddButton(L"job_retry_selected",L"Retry",jobActionX,actionY,66,30,true);
                jobActionX+=74.0f;
            }
            AddButton(L"job_open_trainer",L"Trainer",jobActionX,actionY,72,30,false);
        }
    }


    void DrawEvaluation(float w,float h) {
        PageTitle(
            L"Model Lab / Evaluation",
            L"Evaluate registered candidates against the recovered 1.0.15 persona and policy checks before approval");

        const float x=kSidebar+28.0f;
        const float y=kHeader+94.0f;
        const float contentW=w-x-28.0f;
        const float gap=12.0f;

        const wchar_t* tabLabels[]={
            L"Overview",L"Train",L"Datasets",L"Personas & LoRAs",
            L"Foundation Forks",L"Jobs",L"Evaluation",L"Deployment"
        };
        const wchar_t* tabIds[]={
            L"ml_overview",L"ml_train",L"ml_datasets",L"ml_personas",
            L"ml_foundations",L"ml_jobs",L"ml_evaluation",L"ml_deployment"
        };
        const float tabGap=6.0f;
        const float tabW=(contentW-tabGap*7.0f)/8.0f;
        for(int i=0;i<8;i++) {
            const float tx=x+i*(tabW+tabGap);
            const bool active=i==6;
            Rounded(tx,y,tabW,36,
                active?brush_.panel2.Get():brush_.sidebar.Get(),
                active?brush_.cyan.Get():brush_.border.Get(),7);
            TextLine(tabLabels[i],tx+5,y+2,tabW-10,30,tinyFmt_.Get(),
                active?brush_.cyan.Get():brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
            buttons_.push_back({{tx,y,tx+tabW,y+36},tabIds[i]});
        }

        auto& models=modelRegistry_.Models();
        if(!models.empty() && (selectedRegistryModel_<0 || selectedRegistryModel_>=(int)models.size()))
            selectedRegistryModel_=0;

        size_t candidates=0,approved=0,active=0;
        int bestScore=0;
        for(const auto& model:models) {
            if(model.stage==sentinel::simulation::ModelStage::Candidate) ++candidates;
            else if(model.stage==sentinel::simulation::ModelStage::Approved) ++approved;
            else if(model.stage==sentinel::simulation::ModelStage::Active) ++active;
            bestScore=std::max(bestScore,model.evaluationScore);
        }

        const float summaryY=y+48.0f;
        const float cardW=(contentW-gap*3.0f)/4.0f;
        struct EvalMetric { const wchar_t* label; std::wstring value; const wchar_t* sub; ID2D1Brush* accent; };
        EvalMetric metrics[]={
            {L"Candidates",std::to_wstring(candidates),L"Awaiting evaluation",brush_.yellow.Get()},
            {L"Approved",std::to_wstring(approved),L"Eligible to activate",brush_.green.Get()},
            {L"Active",std::to_wstring(active),L"Current runtime",brush_.cyan.Get()},
            {L"Best Score",std::to_wstring(bestScore),L"Latest registry scores",brush_.blue.Get()}
        };
        for(int i=0;i<4;i++) {
            const float cx=x+i*(cardW+gap);
            Rounded(cx,summaryY,cardW,78,brush_.panel.Get(),brush_.border.Get(),9);
            target_->FillRectangle(D2D1::RectF(cx+14,summaryY+16,cx+18,summaryY+58),metrics[i].accent);
            TextLine(metrics[i].label,cx+30,summaryY+10,cardW-44,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(metrics[i].value,cx+30,summaryY+28,cardW-44,28,h1Fmt_.Get(),brush_.text.Get());
            TextLine(metrics[i].sub,cx+30,summaryY+56,cardW-44,16,tinyFmt_.Get(),metrics[i].accent);
        }

        const float bodyY=summaryY+90.0f;
        const float bodyH=std::max(430.0f,h-bodyY-26.0f);
        const float listW=contentW*0.62f-gap*0.5f;
        const float detailW=contentW-listW-gap;
        const float detailX=x+listW+gap;

        Rounded(x,bodyY,listW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Registered Candidates",x+16,bodyY+12,250,28,h1Fmt_.Get(),brush_.text.Get());
        AddButton(L"model_register",L"Register Current",x+listW-132,bodyY+12,116,28,false);

        TextLine(L"MODEL",x+18,bodyY+58,160,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STAGE",x+190,bodyY+58,90,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"SCORE",x+292,bodyY+58,70,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"LATENCY",x+374,bodyY+58,78,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"ENDPOINT",x+464,bodyY+58,listW-482,18,tinyFmt_.Get(),brush_.muted.Get());

        float rowY=bodyY+80.0f;
        if(models.empty()) {
            Rounded(x+14,rowY,listW-28,58,brush_.sidebar.Get(),brush_.border.Get(),7);
            TextLine(L"No registered models yet. Configure one in Simulation Chat, then choose Register Current.",
                x+26,rowY+10,listW-52,40,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(size_t i=0;i<models.size();++i) {
                const auto& model=models[i];
                const bool selected=(int)i==selectedRegistryModel_;
                ID2D1Brush* stageBrush=
                    model.stage==sentinel::simulation::ModelStage::Active?brush_.green.Get():
                    model.stage==sentinel::simulation::ModelStage::Approved?brush_.cyan.Get():
                    model.stage==sentinel::simulation::ModelStage::Retired?brush_.muted.Get():
                    brush_.yellow.Get();

                Rounded(x+14,rowY,listW-28,48,
                    selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),7);
                TextLine(Widen(model.modelName),x+24,rowY+5,154,18,tinyFmt_.Get(),brush_.text.Get());
                TextLine(Widen(sentinel::simulation::ToString(model.stage)),x+190,rowY+5,90,18,tinyFmt_.Get(),stageBrush);
                TextLine(std::to_wstring(model.evaluationScore),x+292,rowY+5,70,18,tinyFmt_.Get(),brush_.text.Get());
                TextLine(std::to_wstring(model.latencyMs)+L" ms",x+374,rowY+5,78,18,tinyFmt_.Get(),brush_.muted.Get());

                std::wstring endpoint=Widen(model.endpoint);
                if(endpoint.size()>36) endpoint=endpoint.substr(0,33)+L"...";
                TextLine(endpoint,x+464,rowY+5,listW-482,18,tinyFmt_.Get(),brush_.muted.Get());
                TextLine(L"ID "+Widen(model.id),x+24,rowY+27,listW-48,16,tinyFmt_.Get(),brush_.muted.Get());

                buttons_.push_back({{x+14,rowY,x+listW-14,rowY+48},L"regmodel:"+std::to_wstring(i)});
                rowY+=54.0f;
                if(rowY+48>bodyY+bodyH-10) break;
            }
        }

        Rounded(detailX,bodyY,detailW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Evaluation Inspector",detailX+16,bodyY+12,detailW-32,28,h1Fmt_.Get(),brush_.text.Get());

        if(selectedRegistryModel_<0 || selectedRegistryModel_>=(int)models.size()) {
            TextLine(L"Select a registered model to evaluate it.",
                detailX+16,bodyY+54,detailW-32,40,smallFmt_.Get(),brush_.muted.Get());
        } else {
            const auto& model=models[(size_t)selectedRegistryModel_];
            TextLine(Widen(model.modelName),detailX+16,bodyY+48,detailW-32,30,h1Fmt_.Get(),brush_.cyan.Get());
            TextLine(Widen(sentinel::simulation::ToString(model.stage)),
                detailX+16,bodyY+79,detailW-32,20,tinyFmt_.Get(),
                model.stage==sentinel::simulation::ModelStage::Active?brush_.green.Get():brush_.muted.Get());

            const float scoreY=bodyY+116.0f;
            Rounded(detailX+14,scoreY,106,76,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"Score",detailX+26,scoreY+9,82,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(std::to_wstring(model.evaluationScore),
                detailX+26,scoreY+29,82,32,bigFmt_.Get(),
                model.evaluationScore>=80?brush_.green.Get():
                model.evaluationScore>=50?brush_.yellow.Get():brush_.text.Get());

            Rounded(detailX+132,scoreY,detailW-146,76,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"Latency",detailX+144,scoreY+9,detailW-170,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(std::to_wstring(model.latencyMs)+L" ms",
                detailX+144,scoreY+31,detailW-170,24,smallFmt_.Get(),brush_.text.Get());

            int latestRun=evaluationRuns_.LatestIndexForCandidate(model.id);
            if(latestRun>=0) {
                if(selectedEvaluationRun_<0 || selectedEvaluationRun_>=(int)evaluationRuns_.Runs().size() ||
                   evaluationRuns_.Runs()[(size_t)selectedEvaluationRun_].candidateId!=model.id) {
                    selectedEvaluationRun_=latestRun;
                }
                comparisonEvaluationRun_=-1;
                for(int i=selectedEvaluationRun_-1;i>=0;--i) {
                    if(evaluationRuns_.Runs()[(size_t)i].candidateId==model.id) {
                        comparisonEvaluationRun_=i;
                        break;
                    }
                }
            }

            TextLine(L"Persistent Evaluation Run",detailX+16,bodyY+208,detailW-32,22,smallFmt_.Get(),brush_.text.Get());
            if(selectedEvaluationRun_>=0 && selectedEvaluationRun_<(int)evaluationRuns_.Runs().size() &&
               evaluationRuns_.Runs()[(size_t)selectedEvaluationRun_].candidateId==model.id) {
                const auto& run=evaluationRuns_.Runs()[(size_t)selectedEvaluationRun_];
                TextLine(L"Run "+Widen(run.id)+L"  |  Overall "+std::to_wstring(run.overallScore),
                    detailX+16,bodyY+234,detailW-32,20,tinyFmt_.Get(),brush_.cyan.Get());

                if(run.previousOverallScore>=0) {
                    const std::wstring delta=(run.regressionDelta>=0?L"+":L"")+std::to_wstring(run.regressionDelta);
                    TextLine(L"Previous "+std::to_wstring(run.previousOverallScore)+L"  |  Delta "+delta,
                        detailX+16,bodyY+255,detailW-32,18,tinyFmt_.Get(),
                        run.regressionDelta<0?brush_.yellow.Get():brush_.green.Get());
                } else {
                    TextLine(L"First persisted run for this candidate",
                        detailX+16,bodyY+255,detailW-32,18,tinyFmt_.Get(),brush_.muted.Get());
                }

                const sentinel::simulation::EvaluationDimension dims[]={
                    sentinel::simulation::EvaluationDimension::PersonaConsistency,
                    sentinel::simulation::EvaluationDimension::PolicyCompliance,
                    sentinel::simulation::EvaluationDimension::StyleConsistency,
                    sentinel::simulation::EvaluationDimension::MemoryRecall,
                    sentinel::simulation::EvaluationDimension::TriggerRegression,
                    sentinel::simulation::EvaluationDimension::ResponseDiversity
                };
                float dy=bodyY+282.0f;
                for(auto dim:dims) {
                    const int score=sentinel::simulation::DimensionScore(run,dim);
                    TextLine(Widen(sentinel::simulation::ToString(dim)),
                        detailX+16,dy,104,18,tinyFmt_.Get(),brush_.muted.Get());
                    TextLine(score<0?L"-":std::to_wstring(score),
                        detailX+126,dy,46,18,tinyFmt_.Get(),
                        score>=80?brush_.green.Get():score>=0?brush_.yellow.Get():brush_.muted.Get(),
                        DWRITE_TEXT_ALIGNMENT_TRAILING);
                    const float barX=detailX+184;
                    const float barW=std::max(40.0f,detailW-204.0f);
                    target_->FillRectangle(D2D1::RectF(barX,dy+6,barX+barW,dy+11),brush_.border.Get());
                    if(score>=0) {
                        const float filled=barW*std::clamp(score,0,100)/100.0f;
                        target_->FillRectangle(D2D1::RectF(barX,dy+6,barX+filled,dy+11),
                            score>=80?brush_.green.Get():brush_.yellow.Get());
                    }
                    dy+=22.0f;
                }

                if(!run.warnings.empty() && dy<bodyY+bodyH-76) {
                    std::wstring warning=Widen(run.warnings.front());
                    if(warning.size()>92) warning=warning.substr(0,89)+L"...";
                    TextLine(warning,detailX+16,dy+2,detailW-32,30,tinyFmt_.Get(),brush_.yellow.Get());
                }
            } else {
                TextLine(L"No persistent evaluation run exists for this candidate yet.",
                    detailX+16,bodyY+238,detailW-32,38,tinyFmt_.Get(),brush_.muted.Get());
            }

            const float actionY=bodyY+bodyH-42.0f;
            AddButton(L"model_eval",L"Evaluate",detailX+16,actionY,72,30,true);
            AddButton(L"model_approve",L"Approve",detailX+96,actionY,70,30,false);
            AddButton(L"eval_export_run",L"Export",detailX+174,actionY,68,30,false);
            AddButton(L"eval_export_compare",L"Compare",detailX+250,actionY,76,30,false);
        }
    }

    void DrawDeployment(float w,float h) {
        PageTitle(
            L"Model Lab / Deployment",
            L"Prepare, lock, activate, export, and roll back evaluated SARA runtime packages");

        const float x=kSidebar+28.0f;
        const float y=kHeader+94.0f;
        const float contentW=w-x-28.0f;
        const float gap=12.0f;

        const wchar_t* tabLabels[]={
            L"Overview",L"Train",L"Datasets",L"Personas & LoRAs",
            L"Foundation Forks",L"Jobs",L"Evaluation",L"Deployment"
        };
        const wchar_t* tabIds[]={
            L"ml_overview",L"ml_train",L"ml_datasets",L"ml_personas",
            L"ml_foundations",L"ml_jobs",L"ml_evaluation",L"ml_deployment"
        };
        const float tabGap=6.0f;
        const float tabW=(contentW-tabGap*7.0f)/8.0f;
        for(int i=0;i<8;i++) {
            const float tx=x+i*(tabW+tabGap);
            const bool active=i==7;
            Rounded(tx,y,tabW,36,
                active?brush_.panel2.Get():brush_.sidebar.Get(),
                active?brush_.cyan.Get():brush_.border.Get(),7);
            TextLine(tabLabels[i],tx+5,y+2,tabW-10,30,tinyFmt_.Get(),
                active?brush_.cyan.Get():brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
            buttons_.push_back({{tx,y,tx+tabW,y+36},tabIds[i]});
        }

        auto& packages=deploymentRegistry_.Packages();
        const int activePackage=deploymentRegistry_.ActiveIndex();
        if(!packages.empty() &&
           (selectedDeployment_<0 || selectedDeployment_>=(int)packages.size())) {
            selectedDeployment_=activePackage>=0?activePackage:(int)packages.size()-1;
        }

        // If there is no valid selected model for preparing a package, prefer
        // the active model and otherwise the first approved candidate.
        auto& models=modelRegistry_.Models();
        if(selectedRegistryModel_<0 || selectedRegistryModel_>=(int)models.size()) {
            selectedRegistryModel_=modelRegistry_.ActiveIndex();
            if(selectedRegistryModel_<0) {
                for(size_t i=0;i<models.size();++i) {
                    if(models[i].stage==sentinel::simulation::ModelStage::Approved) {
                        selectedRegistryModel_=(int)i;
                        break;
                    }
                }
            }
        }

        const float heroY=y+48.0f;
        Rounded(x,heroY,contentW,102,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Active Deployment",x+18,heroY+10,260,28,h1Fmt_.Get(),brush_.text.Get());

        if(activePackage>=0 && activePackage<(int)packages.size()) {
            const auto& p=packages[(size_t)activePackage];
            StatusDot(x+22,heroY+58,5,brush_.green.Get());
            TextLine(Widen(p.id),x+38,heroY+45,150,24,smallFmt_.Get(),brush_.cyan.Get());
            TextLine(Widen(p.candidateName),x+198,heroY+45,210,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"Foundation: "+(p.foundationName.empty()?L"default":Widen(p.foundationName)),
                x+418,heroY+45,260,24,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"LoRA: "+(p.adapterName.empty()?L"none":Widen(p.adapterName)),
                x+688,heroY+45,230,24,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Score "+std::to_wstring(p.evaluationScore),
                x+contentW-224,heroY+18,92,22,smallFmt_.Get(),brush_.green.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);
            TextLine(p.versionLocked?L"LOCKED":L"UNLOCKED",
                x+contentW-124,heroY+18,106,22,tinyFmt_.Get(),
                p.versionLocked?brush_.cyan.Get():brush_.yellow.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);
            TextLine(L"Persona "+Widen(p.personaName)+L"  |  Eval "+Widen(p.evaluationRunId),
                x+38,heroY+72,contentW-56,18,tinyFmt_.Get(),brush_.muted.Get());
        } else {
            TextLine(L"No deployment package is active. Evaluate and approve a model, then prepare a package.",
                x+18,heroY+50,contentW-36,30,smallFmt_.Get(),brush_.yellow.Get());
        }

        const float bodyY=heroY+114.0f;
        const float bodyH=std::max(390.0f,h-bodyY-26.0f);
        const float listW=contentW*0.63f-gap*0.5f;
        const float detailW=contentW-listW-gap;
        const float detailX=x+listW+gap;

        Rounded(x,bodyY,listW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Deployment Packages",x+16,bodyY+12,250,28,h1Fmt_.Get(),brush_.text.Get());
        AddButton(L"deployment_prepare",L"Prepare Package",x+listW-132,bodyY+12,116,28,true);

        TextLine(L"PACKAGE",x+18,bodyY+58,94,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"CANDIDATE",x+122,bodyY+58,150,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"SCORE",x+284,bodyY+58,56,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STAGE",x+352,bodyY+58,96,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"LOCK",x+listW-78,bodyY+58,60,18,tinyFmt_.Get(),brush_.muted.Get());

        float rowY=bodyY+80.0f;
        if(packages.empty()) {
            Rounded(x+14,rowY,listW-28,62,brush_.sidebar.Get(),brush_.border.Get(),7);
            TextLine(L"No deployment packages yet. Evaluate and approve a model, then prepare one.",
                x+26,rowY+10,listW-52,42,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(size_t i=0;i<packages.size();++i) {
                const auto& p=packages[i];
                const bool selected=(int)i==selectedDeployment_;
                ID2D1Brush* stageBrush=
                    p.stage==sentinel::simulation::DeploymentStage::Active?brush_.green.Get():
                    p.stage==sentinel::simulation::DeploymentStage::RolledBack?brush_.yellow.Get():
                    p.stage==sentinel::simulation::DeploymentStage::Retired?brush_.muted.Get():
                    brush_.cyan.Get();

                Rounded(x+14,rowY,listW-28,54,
                    selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),7);
                TextLine(Widen(p.id),x+24,rowY+5,88,18,tinyFmt_.Get(),brush_.cyan.Get());
                TextLine(Widen(p.candidateName),x+122,rowY+5,150,18,tinyFmt_.Get(),brush_.text.Get());
                TextLine(std::to_wstring(p.evaluationScore),x+284,rowY+5,56,18,tinyFmt_.Get(),
                    p.evaluationScore>=80?brush_.green.Get():brush_.yellow.Get());
                TextLine(Widen(sentinel::simulation::ToString(p.stage)),x+352,rowY+5,96,18,tinyFmt_.Get(),stageBrush);
                TextLine(p.versionLocked?L"YES":L"NO",
                    x+listW-78,rowY+5,60,18,tinyFmt_.Get(),
                    p.versionLocked?brush_.cyan.Get():brush_.yellow.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);

                std::wstring line=L"Foundation "+(p.foundationName.empty()?L"default":Widen(p.foundationName))+
                    L" | LoRA "+(p.adapterName.empty()?L"none":Widen(p.adapterName))+
                    L" | "+Widen(p.evaluationRunId);
                if(line.size()>94) line=line.substr(0,91)+L"...";
                TextLine(line,x+24,rowY+29,listW-48,16,tinyFmt_.Get(),brush_.muted.Get());

                buttons_.push_back({
                    {x+14,rowY,x+listW-14,rowY+54},
                    L"deployment_select:"+std::to_wstring(i)
                });
                rowY+=60.0f;
                if(rowY+54>bodyY+bodyH-10) break;
            }
        }

        Rounded(detailX,bodyY,detailW,bodyH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Package Inspector",detailX+16,bodyY+12,detailW-32,28,h1Fmt_.Get(),brush_.text.Get());

        if(selectedDeployment_<0 || selectedDeployment_>=(int)packages.size()) {
            TextLine(L"Select a prepared package, or prepare one from the currently selected approved model.",
                detailX+16,bodyY+54,detailW-32,52,smallFmt_.Get(),brush_.muted.Get());

            if(selectedRegistryModel_>=0 && selectedRegistryModel_<(int)models.size()) {
                const auto& m=models[(size_t)selectedRegistryModel_];
                TextLine(L"Candidate",detailX+16,bodyY+124,80,18,tinyFmt_.Get(),brush_.muted.Get());
                TextLine(Widen(m.modelName),detailX+104,bodyY+121,detailW-120,22,smallFmt_.Get(),brush_.cyan.Get());
                TextLine(L"Stage",detailX+16,bodyY+154,80,18,tinyFmt_.Get(),brush_.muted.Get());
                TextLine(Widen(sentinel::simulation::ToString(m.stage)),detailX+104,bodyY+151,detailW-120,22,tinyFmt_.Get(),brush_.text.Get());
                const int evalIndex=evaluationRuns_.LatestIndexForCandidate(m.id);
                TextLine(L"Evaluation",detailX+16,bodyY+184,80,18,tinyFmt_.Get(),brush_.muted.Get());
                TextLine(evalIndex>=0
                    ? L"Run "+Widen(evaluationRuns_.Runs()[(size_t)evalIndex].id)+
                      L" | "+std::to_wstring(evaluationRuns_.Runs()[(size_t)evalIndex].overallScore)
                    : L"No persisted run",
                    detailX+104,bodyY+181,detailW-120,22,tinyFmt_.Get(),
                    evalIndex>=0?brush_.green.Get():brush_.yellow.Get());
            }

            const float actionY=bodyY+bodyH-42.0f;
            AddButton(L"deployment_prepare",L"Prepare Package",detailX+16,actionY,126,30,true);
        } else {
            const auto& p=packages[(size_t)selectedDeployment_];
            TextLine(Widen(p.id),detailX+16,bodyY+48,detailW-32,30,h1Fmt_.Get(),brush_.cyan.Get());
            TextLine(Widen(sentinel::simulation::ToString(p.stage))+
                L"  |  Score "+std::to_wstring(p.evaluationScore),
                detailX+16,bodyY+79,detailW-32,20,tinyFmt_.Get(),
                p.stage==sentinel::simulation::DeploymentStage::Active?brush_.green.Get():brush_.muted.Get());

            TextLine(L"Candidate",detailX+16,bodyY+116,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(p.candidateName),detailX+108,bodyY+113,detailW-124,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Persona",detailX+16,bodyY+146,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(p.personaName),detailX+108,bodyY+143,detailW-124,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Foundation",detailX+16,bodyY+176,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(p.foundationName.empty()?L"default":Widen(p.foundationName),
                detailX+108,bodyY+173,detailW-124,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Adapter",detailX+16,bodyY+206,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(p.adapterName.empty()?L"none":Widen(p.adapterName),
                detailX+108,bodyY+203,detailW-124,22,tinyFmt_.Get(),brush_.text.Get());

            TextLine(L"Evaluation",detailX+16,bodyY+236,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(p.evaluationRunId),detailX+108,bodyY+233,detailW-124,22,tinyFmt_.Get(),brush_.green.Get());

            TextLine(L"Version lock",detailX+16,bodyY+266,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(p.versionLocked?L"LOCKED":L"UNLOCKED",
                detailX+108,bodyY+263,detailW-124,22,tinyFmt_.Get(),
                p.versionLocked?brush_.cyan.Get():brush_.yellow.Get());

            TextLine(L"Previous",detailX+16,bodyY+296,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(p.previousDeploymentId.empty()?L"none":Widen(p.previousDeploymentId),
                detailX+108,bodyY+293,detailW-124,22,tinyFmt_.Get(),brush_.muted.Get());

            Rounded(detailX+14,bodyY+332,detailW-28,74,brush_.sidebar.Get(),brush_.border.Get(),7);
            TextLine(L"Pinned runtime stack",detailX+26,bodyY+342,detailW-52,20,smallFmt_.Get(),brush_.cyan.Get());
            TextLine(L"Model + foundation + persona LoRA + evaluation run are recorded together.",
                detailX+26,bodyY+368,detailW-52,26,tinyFmt_.Get(),brush_.muted.Get());

            const float actionY=bodyY+bodyH-42.0f;
            AddButton(L"deployment_activate",L"Activate",detailX+16,actionY,76,30,true);
            AddButton(L"deployment_rollback",L"Rollback",detailX+100,actionY,76,30,false);
            AddButton(L"deployment_lock",p.versionLocked?L"Unlock":L"Lock",detailX+184,actionY,66,30,false);
            AddButton(L"deployment_export",L"Manifest",detailX+258,actionY,76,30,false);
        }
    }

    void DrawMessaging(float w,float h) {
        PageTitle(L"Channels & Messaging",
            L"Provider-neutral adapter readiness, human-approved routing, attachments, and channel handoff");

        const float x=kSidebar+28.0f;
        const float y=kHeader+104.0f;
        const float gap=14.0f;
        const float contentW=w-x-28.0f;

        const auto adapters=runtime_->channelAdapters.All();
        size_t connectedAdapters=0;
        for(const auto* adapter:adapters)
            if(adapter && adapter->Connected()) ++connectedAdapters;

        auto msgs=messagingAdapter_
            ? messagingAdapter_->Poll("local-sim")
            : std::vector<sentinel::operations::NormalizedMessage>{};

        size_t pendingApprovals=0;
        for(const auto& approval:approvals_)
            if(approval.status==sentinel::operations::ApprovalStatus::Pending)
                ++pendingApprovals;

        const float metricW=(contentW-gap*3.0f)/4.0f;
        Metric(
            x,y,metricW,
            L"Adapters",
            std::to_wstring(adapters.size()),
            L"Registry",
            brush_.cyan.Get(),IconKind::Chat);
        Metric(
            x+metricW+gap,y,metricW,
            L"Connected",
            std::to_wstring(connectedAdapters),
            L"Online",
            connectedAdapters?brush_.green.Get():brush_.yellow.Get(),
            IconKind::Check);
        Metric(
            x+2.0f*(metricW+gap),y,metricW,
            L"Approvals",
            std::to_wstring(pendingApprovals),
            L"Pending",
            pendingApprovals?brush_.yellow.Get():brush_.green.Get(),
            IconKind::Shield);
        Metric(
            x+3.0f*(metricW+gap),y,metricW,
            L"Approved Queue",
            std::to_wstring(msgs.size()),
            L"Local",
            brush_.blue.Get(),IconKind::Document);

        const float mainY=y+126.0f;
        const float mainH=214.0f;
        const float leftW=(contentW-gap)*0.53f;
        const float rightW=contentW-gap-leftW;
        const float rightX=x+leftW+gap;

        Rounded(x,mainY,leftW,mainH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Adapter Registry",x+18,mainY+12,leftW-36,30,h1Fmt_.Get(),brush_.text.Get());

        auto capabilityText=[](sentinel::channels::ChannelCapabilities caps) {
            std::wstring text;
            auto add=[&](const wchar_t* value) {
                if(!text.empty()) text+=L" | ";
                text+=value;
            };
            if(caps.Has(sentinel::channels::Capability::ReceiveText) ||
               caps.Has(sentinel::channels::Capability::SendText)) add(L"TEXT");
            if(caps.Has(sentinel::channels::Capability::ReceiveImage) ||
               caps.Has(sentinel::channels::Capability::SendImage) ||
               caps.Has(sentinel::channels::Capability::ReceiveVideo) ||
               caps.Has(sentinel::channels::Capability::SendVideo)) add(L"MEDIA");
            if(caps.Has(sentinel::channels::Capability::TypingIndicator)) add(L"TYPING");
            if(caps.Has(sentinel::channels::Capability::AutomatedSending)) add(L"AUTO-GATED");
            if(text.empty()) text=L"NO DECLARED CAPABILITIES";
            return text;
        };

        float adapterY=mainY+50.0f;
        if(adapters.empty()) {
            Rounded(x+16,adapterY,leftW-32,52,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"No channel adapters registered.",x+30,adapterY+7,leftW-60,36,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(size_t i=0;i<adapters.size() && i<3;i++) {
                auto* adapter=adapters[i];
                if(!adapter) continue;
                const bool online=adapter->Connected();
                Rounded(x+14,adapterY,leftW-28,54,brush_.sidebar.Get(),brush_.border.Get(),8);
                StatusDot(x+28,adapterY+18,4,online?brush_.green.Get():brush_.yellow.Get());
                TextLine(Widen(adapter->AdapterName()),x+40,adapterY+5,leftW-58,22,smallFmt_.Get(),brush_.text.Get());
                TextLine(
                    capabilityText(adapter->Capabilities()),
                    x+40,adapterY+27,leftW-58,18,tinyFmt_.Get(),brush_.muted.Get());
                TextLine(
                    online?L"ONLINE":L"OFFLINE",
                    x+leftW-86,adapterY+5,58,18,tinyFmt_.Get(),
                    online?brush_.green.Get():brush_.yellow.Get(),
                    DWRITE_TEXT_ALIGNMENT_TRAILING);
                adapterY+=62.0f;
            }
        }

        TextLine(
            L"Third-party transports remain disabled until an agency installs and configures an authorized adapter.",
            x+18,mainY+184,leftW-36,18,tinyFmt_.Get(),brush_.muted.Get());

        Rounded(rightX,mainY,rightW,mainH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Routing Gate",rightX+18,mainY+12,rightW-36,30,h1Fmt_.Get(),brush_.text.Get());

        std::wstring caseLabel=L"No case selected";
        std::wstring subjectLabel=L"No subject selected";
        if(!cases_.empty()) {
            caseLabel=Widen(cases_[selectedCase_].caseNumber);
            const auto caseSubjects=runtime_->subjectIdentity.ListForCase(cases_[selectedCase_].id,1);
            if(!caseSubjects.empty()) subjectLabel=Widen(caseSubjects.front().displayName);
        }

        std::wstring jurisdictionLabel=L"No operating jurisdiction";
        bool jurisdictionReady=false;
        if(!operatingStateCode_.empty()) {
            const auto profile=runtime_->jurisdictionRules.LatestProfile(
                sentinel::channels::RuleLayerType::State,"US",operatingStateCode_);
            if(profile) {
                const wchar_t* review=L"DRAFT";
                if(profile->reviewStatus==sentinel::channels::LegalReviewStatus::LegallyReviewed)
                    review=L"REVIEWED";
                else if(profile->reviewStatus==sentinel::channels::LegalReviewStatus::Active)
                    review=L"ACTIVE";
                else if(profile->reviewStatus==sentinel::channels::LegalReviewStatus::Expired)
                    review=L"EXPIRED";
                jurisdictionReady=profile->AutomationLegallyActive();
                jurisdictionLabel=Widen(operatingStateCode_)+L" | "+review;
                if(!jurisdictionReady) jurisdictionLabel+=L" | auto-send locked";
            } else {
                jurisdictionLabel=Widen(operatingStateCode_)+L" | no rules profile";
            }
        }

        struct GateRow {
            const wchar_t* label;
            std::wstring value;
            bool ready;
        };
        GateRow gates[]={
            {L"Case",caseLabel,!cases_.empty()},
            {L"Subject",subjectLabel,subjectLabel!=L"No subject selected"},
            {L"Jurisdiction",jurisdictionLabel,jurisdictionReady},
            {L"Outbound",L"Human approval required",true}
        };

        float gateY=mainY+50.0f;
        for(const auto& gate:gates) {
            StatusDot(rightX+24,gateY+10,3,gate.ready?brush_.green.Get():brush_.yellow.Get());
            TextLine(gate.label,rightX+36,gateY,rightW*0.31f,20,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(
                gate.value,rightX+rightW*0.36f,gateY-1,rightW*0.58f,22,tinyFmt_.Get(),
                gate.ready?brush_.text.Get():brush_.yellow.Get(),
                DWRITE_TEXT_ALIGNMENT_TRAILING);
            gateY+=30.0f;
        }

        const float routeButtonGap=8.0f;
        const float routeButtonW=(rightW-44.0f-routeButtonGap)/2.0f;
        AddButton(L"dashboard_simulation",L"Simulation Chat",rightX+18,mainY+164,routeButtonW,30,true);
        AddButton(L"dashboard_supervisor",L"Approvals",rightX+26+routeButtonW,mainY+164,routeButtonW,30,false);

        const float bottomY=mainY+mainH+gap;
        const float bottomH=std::max(188.0f,h-bottomY-24.0f);
        const float readinessW=(contentW-gap)*0.53f;
        const float queueX=x+readinessW+gap;
        const float queueW=contentW-readinessW-gap;

        Rounded(x,bottomY,readinessW,bottomH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Channel Readiness",x+18,bottomY+12,readinessW-36,28,h1Fmt_.Get(),brush_.text.Get());

        auto connectedType=[&](sentinel::channels::ChannelType type) {
            const auto found=runtime_->channelAdapters.FindByType(type);
            return std::any_of(found.begin(),found.end(),[](auto* adapter) {
                return adapter && adapter->Connected();
            });
        };

        struct ReadyChannel {
            const wchar_t* label;
            bool ready;
        };
        ReadyChannel channels[]={
            {L"Local Simulation",connectedType(sentinel::channels::ChannelType::LocalSimulation)},
            {L"SMS / MMS / RCS",
                connectedType(sentinel::channels::ChannelType::Sms) ||
                connectedType(sentinel::channels::ChannelType::Mms) ||
                connectedType(sentinel::channels::ChannelType::Rcs)},
            {L"Telegram / Discord",
                connectedType(sentinel::channels::ChannelType::Telegram) ||
                connectedType(sentinel::channels::ChannelType::Discord)},
            {L"Messenger / WhatsApp / Assist",
                connectedType(sentinel::channels::ChannelType::Messenger) ||
                connectedType(sentinel::channels::ChannelType::WhatsApp) ||
                connectedType(sentinel::channels::ChannelType::SnapchatAssist) ||
                connectedType(sentinel::channels::ChannelType::Email)}
        };

        float readyY=bottomY+48.0f;
        for(const auto& channel:channels) {
            StatusDot(x+26,readyY+10,4,channel.ready?brush_.green.Get():brush_.muted.Get());
            TextLine(channel.label,x+40,readyY,readinessW*0.52f,22,smallFmt_.Get(),brush_.text.Get());
            TextLine(
                channel.ready?L"Connected":L"Adapter not configured",
                x+readinessW*0.57f,readyY,readinessW*0.38f,22,tinyFmt_.Get(),
                channel.ready?brush_.green.Get():brush_.muted.Get(),
                DWRITE_TEXT_ALIGNMENT_TRAILING);
            readyY+=29.0f;
        }

        TextLine(
            L"Adapters never bypass jurisdiction or supervisor gates.",
            x+18,bottomY+bottomH-30,readinessW-36,18,tinyFmt_.Get(),brush_.muted.Get());

        Rounded(queueX,bottomY,queueW,bottomH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Approved Local Queue",queueX+18,bottomY+12,queueW-36,28,h1Fmt_.Get(),brush_.text.Get());
        TextLine(
            std::to_wstring(msgs.size())+L" approved / queued",
            queueX+18,bottomY+42,queueW-36,20,tinyFmt_.Get(),brush_.cyan.Get());

        float queueY=bottomY+70.0f;
        if(msgs.empty()) {
            Rounded(queueX+16,queueY,queueW-32,48,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"No approved messages queued.",queueX+28,queueY+5,queueW-56,36,smallFmt_.Get(),brush_.muted.Get());
        } else {
            for(size_t i=0;i<msgs.size() && i<1;i++) {
                Rounded(queueX+16,queueY,queueW-32,44,brush_.sidebar.Get(),brush_.border.Get(),8);
                std::wstring rowText=Widen(msgs[i].text);
                if(!msgs[i].mediaPath.empty())
                    rowText=L"[IMAGE] "+rowText+L" | "+std::filesystem::path(Widen(msgs[i].mediaPath)).filename().wstring();
                TextLine(rowText,queueX+28,queueY+3,queueW-56,36,tinyFmt_.Get(),brush_.text.Get());
                queueY+=50.0f;
            }
        }

        const float actionY=bottomY+bottomH-42.0f;
        const float actionGap=8.0f;
        const float actionW=(queueW-44.0f-actionGap)/2.0f;
        AddButton(L"msg_queue",L"Request Approval",queueX+18,actionY,actionW,30,true);
        AddButton(L"sim_preserve",L"Preserve",queueX+26+actionW,actionY,actionW,30,false);
    }

    void DrawSupervisor(float w,float h) {
        PageTitle(L"Supervisor & Approvals",L"Human review, operational approvals, takeover controls, and SHA-256 action hashes");
        const float x=kSidebar+28.0f;
        const float y=kHeader+104.0f;
        const float gap=14.0f;
        const float contentW=w-x-28.0f;

        size_t pending=0,approved=0,rejected=0;
        for(const auto& a:approvals_) {
            if(a.status==sentinel::operations::ApprovalStatus::Pending) ++pending;
            else if(a.status==sentinel::operations::ApprovalStatus::Approved) ++approved;
            else ++rejected;
        }

        const auto takeoverState=CurrentSupervisorState();
        const bool takeoverActive=takeoverState && takeoverState->investigatorTakeover;

        const float metricW=(contentW-gap*3.0f)/4.0f;
        Metric(
            x,y,metricW,
            L"Pending",std::to_wstring(pending),L"Review",
            pending?brush_.yellow.Get():brush_.green.Get(),IconKind::Shield);
        Metric(
            x+metricW+gap,y,metricW,
            L"Approved",std::to_wstring(approved),L"Allowed",
            brush_.green.Get(),IconKind::Check);
        Metric(
            x+2.0f*(metricW+gap),y,metricW,
            L"Rejected",std::to_wstring(rejected),L"Blocked",
            rejected?brush_.red.Get():brush_.muted.Get(),IconKind::Lock);
        Metric(
            x+3.0f*(metricW+gap),y,metricW,
            L"Takeover",takeoverActive?L"ACTIVE":L"OFF",
            takeoverActive?L"AI locked":L"Supervisor gate",
            takeoverActive?brush_.yellow.Get():brush_.cyan.Get(),IconKind::Shield);

        const float controlY=y+126.0f;
        Rounded(x,controlY,contentW,106,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Human Control",x+18,controlY+10,180,28,h1Fmt_.Get(),brush_.text.Get());

        std::wstring controlStatus;
        ID2D1Brush* controlBrush=brush_.cyan.Get();
        if(cases_.empty()) {
            controlStatus=L"Open a case to use investigator takeover";
            controlBrush=brush_.yellow.Get();
        } else if(takeoverActive) {
            controlStatus=L"Manual control active";
            if(!takeoverState->takeoverActor.empty())
                controlStatus+=L" | "+Widen(takeoverState->takeoverActor);
            controlBrush=brush_.yellow.Get();
        } else {
            controlStatus=L"AI outbound remains supervisor-gated";
        }
        TextLine(controlStatus,x+208,controlY+12,contentW-226,24,tinyFmt_.Get(),controlBrush,DWRITE_TEXT_ALIGNMENT_TRAILING);

        const float innerX=x+18.0f;
        const float buttonGap=8.0f;
        const float buttonW=(contentW-36.0f-buttonGap*3.0f)/4.0f;
        const float buttonY=controlY+52.0f;
        AddButton(L"approval_request",L"Request Suggestion",innerX,buttonY,buttonW,34,false);
        AddButton(L"approval_approve",L"Approve Next",innerX+buttonW+buttonGap,buttonY,buttonW,34,true);
        AddButton(L"approval_reject",L"Reject Next",innerX+2.0f*(buttonW+buttonGap),buttonY,buttonW,34,false);
        AddButton(
            L"supervisor_takeover",
            takeoverActive?L"Release Takeover":L"Activate Takeover",
            innerX+3.0f*(buttonW+buttonGap),buttonY,buttonW,34,
            takeoverActive);

        const float ledgerY=controlY+120.0f;
        const float ledgerH=std::max(190.0f,h-ledgerY-24.0f);
        Rounded(x,ledgerY,contentW,ledgerH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Approval Ledger",x+18,ledgerY+10,240,30,h1Fmt_.Get(),brush_.text.Get());
        TextLine(
            L"Action hashes identify the exact reviewed payload.",
            x+280,ledgerY+12,contentW-298,22,tinyFmt_.Get(),brush_.muted.Get(),
            DWRITE_TEXT_ALIGNMENT_TRAILING);

        float yy=ledgerY+48.0f;
        if(approvals_.empty()) {
            Rounded(x+18,yy,contentW-36,50,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"No approval requests yet.",x+32,yy+6,contentW-64,38,bodyFmt_.Get(),brush_.muted.Get());
        } else {
            const float available=std::max(64.0f,ledgerH-62.0f);
            const size_t maxRows=std::max<size_t>(1,(size_t)(available/66.0f));
            for(size_t row=0;row<approvals_.size() && row<maxRows;row++) {
                const auto& a=approvals_[approvals_.size()-1-row];
                Rounded(x+18,yy,contentW-36,58,brush_.sidebar.Get(),brush_.border.Get(),8);

                const std::wstring state=
                    a.status==sentinel::operations::ApprovalStatus::Pending?L"PENDING":
                    a.status==sentinel::operations::ApprovalStatus::Approved?L"APPROVED":L"REJECTED";
                ID2D1Brush* stateBrush=
                    a.status==sentinel::operations::ApprovalStatus::Pending?brush_.yellow.Get():
                    a.status==sentinel::operations::ApprovalStatus::Approved?brush_.green.Get():brush_.red.Get();

                TextLine(state,x+30,yy+6,88,20,tinyFmt_.Get(),stateBrush);
                TextLine(Widen(a.action),x+128,yy+3,contentW-158,24,smallFmt_.Get(),brush_.text.Get());

                std::wstring detail=L"SHA-256 "+Widen(a.actionHash);
                if(a.status==sentinel::operations::ApprovalStatus::Pending) {
                    detail+=L" | requested by "+Widen(a.requestedBy);
                } else if(!a.reviewedBy.empty()) {
                    detail+=L" | "+Widen(a.reviewedBy);
                }
                TextLine(detail,x+128,yy+30,contentW-158,20,tinyFmt_.Get(),brush_.muted.Get());
                yy+=66.0f;
            }
        }
    }

    void DrawAgency(float w,float h) {
        PageTitle(L"Agency Server",L"Persistent offline sync staging, jurisdiction configuration, and future authenticated agency transport");
        const float x=kSidebar+28.0f;
        const float y=kHeader+104.0f;
        const float contentW=w-x-28.0f;
        const float gap=14.0f;
        const float leftW=(contentW-gap)*0.58f;
        const float rightW=contentW-gap-leftW;
        const float rx=x+leftW+gap;

        const auto queueItems=runtime_->agencyStore.Items(3);
        const auto pendingCount=runtime_->agencyStore.PendingCount();

        const float topH=244.0f;
        Rounded(x,y,leftW,topH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Agency Configuration",x+18,y+12,leftW-36,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Server endpoint",x+20,y+62,116,30,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Agency ID",x+20,y+108,116,30,tinyFmt_.Get(),brush_.muted.Get());

        AddButton(
            L"agency_toggle",
            agencyConfig_.enabled?L"Disable Config":L"Enable Config",
            x+20,y+154,150,36,true);
        StatusDot(x+194,y+172,4,agencyConfig_.enabled?brush_.green.Get():brush_.yellow.Get());
        TextLine(
            agencyConfig_.enabled?L"Configuration persisted":L"Offline / local-only",
            x+206,y+155,leftW-226,24,smallFmt_.Get(),
            agencyConfig_.enabled?brush_.green.Get():brush_.muted.Get());
        TextLine(
            L"Workstation: "+Widen(agencyConfig_.workstationId),
            x+206,y+179,leftW-226,18,tinyFmt_.Get(),brush_.muted.Get());

        Text(
            L"Enabling configuration does not open a network connection. Transport remains inactive until authenticated Agency Server protocol and credentials are implemented.",
            x+20,y+204,leftW-40,30,tinyFmt_.Get(),brush_.muted.Get());

        Rounded(rx,y,rightW,topH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Persistent Sync Queue",rx+18,y+12,rightW-36,30,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Pending",rx+20,y+52,80,22,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(std::to_wstring(pendingCount),rx+104,y+43,70,38,bigFmt_.Get(),brush_.cyan.Get());
        AddButton(L"agency_enqueue",L"Queue Audit Snapshot",rx+20,y+88,rightW-40,32,false);

        float queueY=y+132.0f;
        if(queueItems.empty()) {
            Rounded(rx+18,queueY,rightW-36,48,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"No persistent sync work queued.",rx+30,queueY+7,rightW-60,34,tinyFmt_.Get(),brush_.muted.Get());
        } else {
            for(size_t i=0;i<queueItems.size() && i<2;i++) {
                Rounded(rx+18,queueY,rightW-36,42,brush_.sidebar.Get(),brush_.border.Get(),7);
                const auto& item=queueItems[i];
                const wchar_t* type=
                    item.type==sentinel::agency::SyncItemType::AuditRecord?L"AUDIT":
                    item.type==sentinel::agency::SyncItemType::EvidenceManifest?L"EVIDENCE":
                    item.type==sentinel::agency::SyncItemType::CaseMetadata?L"CASE":
                    item.type==sentinel::agency::SyncItemType::PolicyPackage?L"POLICY":L"MODEL";
                TextLine(type,rx+28,queueY+4,64,18,tinyFmt_.Get(),brush_.cyan.Get());
                std::wstring ref=Widen(item.localReference);
                if(ref.size()>24) ref=ref.substr(0,21)+L"...";
                TextLine(ref,rx+96,queueY+4,rightW-128,18,tinyFmt_.Get(),brush_.text.Get());
                TextLine(
                    item.complete?L"COMPLETE":L"WAITING FOR TRANSPORT",
                    rx+28,queueY+22,rightW-56,16,tinyFmt_.Get(),
                    item.complete?brush_.green.Get():brush_.yellow.Get());
                queueY+=48.0f;
            }
        }

        const float jurisdictionY=y+258.0f;
        const float jurisdictionH=132.0f;
        Rounded(x,jurisdictionY,leftW,jurisdictionH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Operating Jurisdiction",x+18,jurisdictionY+12,leftW-36,28,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"State",x+20,jurisdictionY+50,100,24,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"jurisdiction_apply",L"Apply Rules Profile",x+20,jurisdictionY+86,156,32,true);
        TextLine(jurisdictionStatus_,x+188,jurisdictionY+82,leftW-208,38,tinyFmt_.Get(),brush_.cyan.Get());

        Rounded(rx,jurisdictionY,rightW,jurisdictionH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Transport Safety",rx+18,jurisdictionY+12,rightW-36,28,h1Fmt_.Get(),brush_.text.Get());
        Text(
            L"Queued work remains local. No item is transmitted until a future authenticated transport explicitly consumes and acknowledges it.",
            rx+20,jurisdictionY+48,rightW-40,64,tinyFmt_.Get(),brush_.muted.Get());

        const float responsibilitiesY=y+404.0f;
        const float responsibilitiesH=std::max(132.0f,h-responsibilitiesY-24.0f);
        Rounded(x,responsibilitiesY,contentW,responsibilitiesH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Agency Server Contract Boundary",x+18,responsibilitiesY+12,340,28,h1Fmt_.Get(),brush_.text.Get());

        const wchar_t* items[]={
            L"Encrypted case/evidence synchronization after authenticated transport is implemented",
            L"Central policy and approved model-profile distribution",
            L"Workstation registration, roles, and investigator authorization",
            L"Multi-investigator coordination, backup, acknowledgement, and retry state"
        };
        float iy=responsibilitiesY+50.0f;
        for(auto* item:items) {
            if(iy+22.0f>responsibilitiesY+responsibilitiesH-10.0f) break;
            StatusDot(x+28,iy+10,3,brush_.cyan.Get());
            TextLine(item,x+42,iy,contentW-64,22,smallFmt_.Get(),brush_.text.Get());
            iy+=30.0f;
        }
    }

    void CheckForUpdates() {
        try {
            sentinel::update::UpdateService service;
            const std::string url="https://raw.githubusercontent.com/afterburn25/Sentinel/main/release/update-manifest.json";
            auto info=service.Check(url,SARA_VERSION_STR);
            if(info.newer) {
                updateStatus_=L"Update available: "+Widen(info.version);
                statusText_=L"SARA update available";
            } else {
                updateStatus_=std::wstring(L"Current version ")+Widen(SARA_VERSION_STR)+L" is up to date";
                statusText_=L"No SARA update available";
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
        report << L"Installer verification: "
               << LocalModelVerificationMarkerLabel(LocalModelVerificationMarkerState())
               << L"\n";

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
                ctx.scenario="SARA AI diagnostic";
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
        PageTitle(L"Settings",L"Secure storage, application information, model integrity, and update status");
        const float x=kSidebar+28.0f;
        const float y=kHeader+104.0f;
        const float contentW=w-x-28.0f;
        const float gap=14.0f;
        const float leftW=(contentW-gap)*0.58f;
        const float rightW=contentW-gap-leftW;
        const float rx=x+leftW+gap;

        const auto markerState=LocalModelVerificationMarkerState();
        const bool aiPrerequisites=BundledAiPrerequisitesPresent();
        const bool markerCurrent=markerState==LocalModelMarkerState::MarkerCurrent;

        Rounded(x,y,leftW,244,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Secure Local Store",x+18,y+12,leftW-36,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Location",x+20,y+58,90,24,tinyFmt_.Get(),brush_.muted.Get());
        Text(runtime_->root.wstring(),x+118,y+56,leftW-138,34,tinyFmt_.Get(),brush_.text.Get());

        TextLine(L"Encryption",x+20,y+98,90,24,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"AES-256-GCM",x+118,y+96,leftW-138,26,smallFmt_.Get(),brush_.green.Get());

        TextLine(L"Key protection",x+20,y+136,90,24,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Windows DPAPI workstation master key",x+118,y+134,leftW-138,26,smallFmt_.Get(),brush_.text.Get());

        TextLine(L"Schema",x+20,y+174,90,24,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(
            L"Migration v"+std::to_wstring(runtime_->migrations.CurrentVersion()),
            x+118,y+172,leftW-138,26,smallFmt_.Get(),brush_.cyan.Get());

        TextLine(L"Mode",x+20,y+210,90,24,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(
            agencyConfig_.enabled?L"Offline-first + agency sync staging":L"Offline-first / local-only",
            x+118,y+208,leftW-138,26,smallFmt_.Get(),brush_.cyan.Get());

        Rounded(rx,y,rightW,244,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Application & Local AI",rx+18,y+12,rightW-36,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Version",rx+20,y+54,76,22,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(std::wstring(L"SARA ")+Widen(SARA_VERSION_STR),rx+104,y+52,rightW-124,24,bodyFmt_.Get(),brush_.text.Get());

        TextLine(L"Build",rx+20,y+84,76,22,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Development Release",rx+104,y+82,rightW-124,24,smallFmt_.Get(),brush_.muted.Get());

        TextLine(L"Local AI",rx+20,y+114,76,22,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(
            aiPrerequisites?L"Model + runtime installed":L"Install / repair required",
            rx+104,y+112,rightW-124,24,smallFmt_.Get(),
            aiPrerequisites?brush_.green.Get():brush_.yellow.Get());

        TextLine(L"Verification",rx+20,y+144,76,22,tinyFmt_.Get(),brush_.muted.Get());
        Text(
            LocalModelVerificationMarkerLabel(markerState),
            rx+104,y+142,rightW-124,32,tinyFmt_.Get(),
            markerCurrent?brush_.green.Get():brush_.yellow.Get());

        AddButton(L"check_updates",L"Check for Updates",rx+20,y+184,142,34,false);
        AddButton(L"ai_diagnostics",L"AI Diagnostics",rx+172,y+184,rightW-192,34,true);
        Text(
            updateStatus_,
            rx+20,y+220,rightW-40,18,tinyFmt_.Get(),brush_.muted.Get());

        const float securityY=y+260.0f;
        const float securityH=std::max(280.0f,h-securityY-24.0f);
        Rounded(x,securityY,contentW,securityH,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Release Security",x+18,securityY+12,260,30,h1Fmt_.Get(),brush_.text.Get());

        struct SecurityRow {
            const wchar_t* label;
            std::wstring value;
            ID2D1Brush* brush;
        };
        SecurityRow rows[]={
            {L"Evidence integrity",L"SHA-256 + AES-GCM authentication",brush_.text.Get()},
            {L"Audit integrity",L"Hash-linked ledger; new records bind metadata (audit-v2)",brush_.text.Get()},
            {L"Model installer",L"Protected SHA-256 + marker-assisted upgrade verification",brush_.text.Get()},
            {L"Update transport",L"HTTPS-only manifest checking",brush_.text.Get()},
            {L"Outbound messaging",L"Human approval required",brush_.text.Get()},
            {L"Code signing",L"Pipeline ready; trusted signing identity not configured",brush_.muted.Get()}
        };

        float yy=securityY+54.0f;
        for(const auto& row:rows) {
            if(yy+26.0f>securityY+securityH-10.0f) break;
            TextLine(row.label,x+22,yy,150,26,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(row.value,x+184,yy,contentW-206,26,smallFmt_.Get(),row.brush);
            yy+=36.0f;
        }
    }

    void ShowCaseEditors(bool show) {
        ShowWindow(caseNumberEdit_,show?SW_SHOW:SW_HIDE);
        ShowWindow(caseTitleEdit_,show?SW_SHOW:SW_HIDE);
    }

    void SelectCase(size_t i) {
        if (i>=cases_.size()) return;
        selectedCase_=i; selectedEvidence_=0;
        selectedSubjectId_.clear();
        selectedIdentityLeadId_.clear();
        subjectDraftNew_=false;
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
        if(cases_.empty() || evidence_.empty()) {
            MessageBoxW(hwnd_,L"Select or import evidence first.",L"SARA",MB_OK|MB_ICONINFORMATION);
            return;
        }

        try {
            auto key=runtime_->keys.GetCaseKey(cases_[selectedCase_].id);
            sentinel::EvidenceService service(
                runtime_->root/"evidence",
                runtime_->db,
                runtime_->random,
                runtime_->hash,
                runtime_->cipher,
                runtime_->audit);

            const auto result=service.Verify(
                evidence_[selectedEvidence_],
                key.Span(),
                sentinel::UserId::Random());

            if(result.valid) {
                lastVerify_=
                    L"VALID | AUTHENTICATED | container "+
                    Widen(result.observedContainerHash.ToHex()).substr(0,16)+
                    L"... | plaintext "+
                    Widen(result.observedPlaintextHash.ToHex()).substr(0,16)+L"...";
                statusText_=L"Evidence verification passed";
            } else {
                lastVerify_=L"INVALID | "+Widen(result.detail);
                statusText_=L"Evidence integrity verification failed";
            }

            page_=Page::Verification;
            ShowCaseEditors(false);
        }
        catch(const std::exception& e) {
            lastVerify_=L"ERROR | "+Widen(e.what());
            page_=Page::Verification;
            ShowCaseEditors(false);
            statusText_=L"Evidence verification could not complete";
        }
    }

    void VerifyAudit() {
        bool ok=runtime_->audit.VerifyChain();
        statusText_=ok?L"Audit chain verified":L"Audit integrity failure";
        MessageBoxW(hwnd_,ok?L"Audit chain is VALID. No tampering detected.":L"Audit chain verification FAILED.",
            L"SARA Audit Verification",MB_OK|(ok?MB_ICONINFORMATION:MB_ICONERROR));
    }
};

App* g_app{};

LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
        case WM_CREATE:
            try { g_app=new App(); g_app->Init(hwnd); }
            catch (const std::exception& e) {
                MessageBoxW(hwnd,Widen(e.what()).c_str(),L"SARA Startup Failed",MB_OK|MB_ICONERROR);
                return -1;
            }
            return 0;
        case WM_SIZE: if(g_app) g_app->Resize(); return 0;
        case WM_ERASEBKGND:
            // Direct2D clears and repaints the complete client area in WM_PAINT.
            // Suppress the Win32 class-brush erase so high-frequency typing/
            // selection repaints never flash a black intermediate frame.
            return 1;
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
    // The Direct2D renderer owns the full client background. A class brush
    // would cause an erase/fill before every WM_PAINT and visibly flicker.
    wc.hbrBackground=nullptr;
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
