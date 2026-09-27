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
#include "Sentinel/Simulation/EvaluationSuite.hpp"
#include "Sentinel/Simulation/DeploymentRegistry.hpp"
#include "Sentinel/Simulation/SessionStore.hpp"
#include "Sentinel/Simulation/ConversationMemory.hpp"
#include "Sentinel/Simulation/ModelRegistry.hpp"
#include "Sentinel/Simulation/TriggerRules.hpp"
#include "Sentinel/Simulation/TrainingData.hpp"
#include "Sentinel/Operations/Messaging.hpp"
#include "Sentinel/Operations/Supervisor.hpp"
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
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClassName[] = L"SentinelNativeWindow";
constexpr int kSidebar = 220;
constexpr int kHeader = 78;
constexpr UINT_PTR kSimTypingStartTimer = 4101;
constexpr UINT_PTR kSimReplyTimer = 4102;
constexpr int kSimVisibleRows = 4;

enum class Page { Dashboard, Cases, Evidence, Audit, Verification, Simulation, Persona, ModelLab, Messaging, Supervisor, Agency, Settings };
enum class ModelLabSection { Overview, Train, Datasets, Personas, FoundationForks, Jobs, Evaluation, Deployment };
enum class TrainingMode { BehaviorTuning, DatasetTraining, PersonaLoRA, FoundationFork, EvaluationTest };
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

std::filesystem::path AppDataRoot() {
    PWSTR p{};
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&p))) {
        std::filesystem::path out = std::filesystem::path(p) / L"SARA";
        CoTaskMemFree(p);
        return out;
    }
    return std::filesystem::current_path() / "sentinel-data";
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
    sentinel::simulation::ConversationMemoryStore conversationMemory;
    sentinel::KeyManager keys;
    sentinel::SqliteCaseRepository caseRepo;
    sentinel::CaseService cases;
    sentinel::AuditService audit;

    Runtime()
        : root(AppDataRoot()),
          cipher(random),
          migrations(db),
          conversationMemory(db),
          keys(root/"keys"/"master.dpapi",db,dpapi,random,cipher),
          caseRepo(db,&keys,&cipher),
          cases(caseRepo),
          audit(db,hash) {
        std::filesystem::create_directories(root);
        db.Open(root/"sentinel.db");
        migrations.ApplyDirectory(MigrationsDir());
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
    ComPtr<ID2D1SolidColorBrush> bg, panel, panel2, sidebar, border, text, muted, blue, cyan, green, yellow, red;
};

class App {
public:
    static LRESULT CALLBACK ChatEditSubclassProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref) {
        auto* app=reinterpret_cast<App*>(ref);
        if(msg==WM_SETFOCUS) {
            LRESULT result=DefSubclassProc(hwnd,msg,wp,lp);
            CreateCaret(hwnd,nullptr,2,22);
            DWORD start=0,end=0;
            SendMessageW(hwnd,EM_GETSEL,(WPARAM)&start,(LPARAM)&end);
            LRESULT pos=SendMessageW(hwnd,EM_POSFROMCHAR,(WPARAM)end,0);
            int x=(short)LOWORD(pos);
            int y=(short)HIWORD(pos);
            SetCaretPos(std::max(8,x),std::max(7,y));
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
        if(msg==WM_CHAR || msg==WM_KEYUP || msg==WM_LBUTTONUP) {
            LRESULT result=DefSubclassProc(hwnd,msg,wp,lp);
            DWORD start=0,end=0;
            SendMessageW(hwnd,EM_GETSEL,(WPARAM)&start,(LPARAM)&end);
            LRESULT pos=SendMessageW(hwnd,EM_POSFROMCHAR,(WPARAM)end,0);
            SetCaretPos(std::max(8,(int)(short)LOWORD(pos)),std::max(7,(int)(short)HIWORD(pos)));
            ShowCaret(hwnd);
            return result;
        }
        return DefSubclassProc(hwnd,msg,wp,lp);
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
        personaIntelligenceCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1033,GetModuleHandleW(nullptr),nullptr);
        personaSlangCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1034,GetModuleHandleW(nullptr),nullptr);
        personaGrammarCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1035,GetModuleHandleW(nullptr),nullptr);
        personaTypoCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1036,GetModuleHandleW(nullptr),nullptr);
        personaEmojiCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1037,GetModuleHandleW(nullptr),nullptr);
        personaMoodCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1038,GetModuleHandleW(nullptr),nullptr);
        agencyEndpointEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1021,GetModuleHandleW(nullptr),nullptr);
        agencyIdEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1022,GetModuleHandleW(nullptr),nullptr);
        trainingCorrectionEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL,0,0,0,0,hwnd_,(HMENU)1039,GetModuleHandleW(nullptr),nullptr);
        trainingCategoryCombo_=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1044,GetModuleHandleW(nullptr),nullptr);
        trainingReviewTargetEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL,0,0,0,0,hwnd_,(HMENU)1045,GetModuleHandleW(nullptr),nullptr);
        ruleNameEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1040,GetModuleHandleW(nullptr),nullptr);
        rulePatternEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1041,GetModuleHandleW(nullptr),nullptr);
        ruleResponsesEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL,0,0,0,0,hwnd_,(HMENU)1042,GetModuleHandleW(nullptr),nullptr);
        rulePriorityEdit_=CreateWindowExW(0,L"EDIT",L"100",WS_CHILD|WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1043,GetModuleHandleW(nullptr),nullptr);
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
        HWND advancedEdits[]={personaNameEdit_,personaLocationEdit_,personaInterestsEdit_,personaStyleEdit_,
            personaOccupationEdit_,personaEducationEdit_,personaFamilyEdit_,personaBackgroundEdit_,
            scenarioNameEdit_,scenarioObjectiveEdit_,scenarioSeedEdit_,minDelayEdit_,maxDelayEdit_,agencyEndpointEdit_,agencyIdEdit_,
            trainingCorrectionEdit_,trainingReviewTargetEdit_,ruleNameEdit_,rulePatternEdit_,ruleResponsesEdit_,rulePriorityEdit_};
        for(HWND e:advancedEdits) {
            SendMessageW(e,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
            SetWindowTheme(e,L"DarkMode_Explorer",nullptr);
            SendMessageW(e,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
        }
        HWND personaCombos[]={personaAgeCombo_,ageStateCombo_,personaGenderCombo_,personaPronounsCombo_,personaRelationshipCombo_,
            personaPersonalityCombo_,personaSocialCombo_,personaConfidenceCombo_,personaIntelligenceCombo_,personaSlangCombo_,
            personaGrammarCombo_,personaTypoCombo_,personaEmojiCombo_,personaMoodCombo_,trainingCategoryCombo_,modelCombo_};
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
        SendMessageW(trainingCorrectionEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Correction or instruction: e.g. Use shorter messages, or type the preferred response...");
        SendMessageW(trainingReviewTargetEdit_,EM_SETCUEBANNER,TRUE,(LPARAM)L"Edit the approved target response...");
        SetWindowSubclass(chatEdit_,ChatEditSubclassProc,1,reinterpret_cast<DWORD_PTR>(this));
        simSettings_=sentinel::simulation::LoadSimulationSettings(runtime_->root/"simulation.ini");
        SetWindowTextW(modelEndpointEdit_,Widen(simSettings_.endpoint).c_str());
        SetWindowTextW(modelNameEdit_,Widen(simSettings_.model).c_str());

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
        const wchar_t* intelligenceItems[]={L"Simple",L"Average",L"High",L"Very high"};
        const wchar_t* slangItems[]={L"None",L"Low",L"Medium",L"High",L"Very high"};
        const wchar_t* grammarItems[]={L"Polished",L"Natural",L"Casual",L"Loose"};
        const wchar_t* typoItems[]={L"None",L"Low",L"Medium",L"High"};
        const wchar_t* emojiItems[]={L"None",L"Low",L"Medium",L"High",L"Very high"};
        const wchar_t* moodItems[]={L"Neutral",L"Warm",L"Playful",L"Guarded",L"Serious",L"Excited"};
        const wchar_t* correctionCategoryItems[]={L"Behavior",L"Style",L"Tone",L"Length",L"Memory",L"Rule",L"Formatting",L"Other"};
        auto fillCombo=[&](HWND combo,const wchar_t* const* items,size_t count){
            SendMessageW(combo,CB_RESETCONTENT,0,0);
            for(size_t i=0;i<count;i++) SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)items[i]);
        };
        SendMessageW(personaAgeCombo_,CB_RESETCONTENT,0,0);
        for(int age=13;age<=90;++age) {
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
        fillCombo(personaIntelligenceCombo_,intelligenceItems,std::size(intelligenceItems));
        fillCombo(personaSlangCombo_,slangItems,std::size(slangItems));
        fillCombo(personaGrammarCombo_,grammarItems,std::size(grammarItems));
        fillCombo(personaTypoCombo_,typoItems,std::size(typoItems));
        fillCombo(personaEmojiCombo_,emojiItems,std::size(emojiItems));
        fillCombo(personaMoodCombo_,moodItems,std::size(moodItems));
        fillCombo(trainingCategoryCombo_,correctionCategoryItems,std::size(correctionCategoryItems));
        SendMessageW(trainingCategoryCombo_,CB_SETCURSEL,0,0);
        SendMessageW(ageStateCombo_,CB_SETCURSEL,(WPARAM)static_cast<int>(simSettings_.ageState),0);
        LoadProfileEditors();

        messagingAdapter_=sentinel::operations::CreateInMemoryMessageAdapter();
        agencyConfig_.workstationId="local-workstation";
        modelRegistry_.Load(runtime_->root/"model-registry.tsv");
        foundationRegistry_.Load(runtime_->root/"foundation-registry.tsv");
        personaAdapterRegistry_.Load(runtime_->root/"persona-adapters.tsv");
        trainingJobRegistry_.Load(runtime_->root/"training-jobs.tsv");
        triggerRules_.Load(runtime_->root/"trigger-rules.tsv");
        trainingData_.Load(runtime_->root/"training-data.tsv");
        evaluationRuns_.Load(runtime_->root/"evaluation-runs.tsv");
        deploymentRegistry_.Load(runtime_->root/"deployment-registry.tsv");
        if(!evaluationRuns_.Runs().empty()) selectedEvaluationRun_=(int)evaluationRuns_.Runs().size()-1;
        if(!deploymentRegistry_.Packages().empty())
            selectedDeployment_=deploymentRegistry_.ActiveIndex()>=0?deploymentRegistry_.ActiveIndex():(int)deploymentRegistry_.Packages().size()-1;
        trainingCaptured_=(int)trainingData_.Examples().size();
        trainingReviewPending_=(int)trainingData_.Count(sentinel::simulation::TrainingExampleState::Review);
        trainingApproved_=(int)trainingData_.Count(sentinel::simulation::TrainingExampleState::Approved);
        if(foundationRegistry_.Models().empty()) {
            foundationRegistry_.EnsureBase(simSettings_.model.empty()?"Original Base Model":simSettings_.model,"base");
            foundationRegistry_.Save(runtime_->root/"foundation-registry.tsv");
        }
        selectedFoundation_=foundationRegistry_.ActiveIndex()>=0?foundationRegistry_.ActiveIndex():0;
        ResolvePersonaAdapter();

        model_=sentinel::simulation::CreateRuleBasedTestModel();
        modelStatus_=L"Built-in contextual model";
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
            case Page::Messaging: DrawMessaging(w,h); break;
            case Page::Supervisor: DrawSupervisor(w,h); break;
            case Page::Agency: DrawAgency(w,h); break;
            case Page::Settings: DrawSettings(w,h); break;
        }

        HRESULT hr=target_->EndDraw();
        if (hr==D2DERR_RECREATE_TARGET) { target_.Reset(); brushesReady_=false; }
    }

    static std::string CurrentUtcText() {
        auto now=std::chrono::system_clock::now();
        auto t=std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm,&t);
#else
        gmtime_r(&t,&tm);
#endif
        std::ostringstream out;
        out<<std::put_time(&tm,"%Y-%m-%dT%H:%M:%SZ");
        return out.str();
    }

    void AppendTriggerMatchLog(const sentinel::simulation::TriggerMatch& match,const std::string& input,const std::string& response) {
        auto path=runtime_->root/"trigger-matches.tsv";
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path,std::ios::app);
        auto clean=[](std::string s){
            std::replace(s.begin(),s.end(),'\t',' ');
            std::replace(s.begin(),s.end(),'\n',' ');
            std::replace(s.begin(),s.end(),'\r',' ');
            return s;
        };
        out<<CurrentUtcText()<<"\t"<<clean(currentConversationId_)<<"\t"<<clean(simSettings_.persona.name)
           <<"\t"<<clean(match.ruleId)<<"\t"<<clean(match.ruleName)<<"\t"<<(match.terminal?1:0)
           <<"\t"<<clean(input)<<"\t"<<clean(response)<<"\n";
    }

    void ExportModelLabDiagnostics() {
        wchar_t file[MAX_PATH]{};
        wcscpy_s(file,L"SARA-1.0.18-Diagnostics.txt");
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
        out<<"SARA 1.0.18 MODEL LAB DIAGNOSTICS\n";
        out<<"Generated UTC: "<<CurrentUtcText()<<"\n";
        out<<"Persona: "<<simSettings_.persona.name<<"\n";
        out<<"Age: "<<simSettings_.persona.age<<"\n";
        out<<"Personality: "<<simSettings_.persona.personality<<"\n";
        out<<"Writing style: "<<simSettings_.persona.writingStyle<<"\n";
        out<<"Intelligence: "<<simSettings_.persona.intelligenceLevel<<"\n";
        out<<"Slang: "<<simSettings_.persona.slangLevel<<"\n";
        out<<"Grammar: "<<simSettings_.persona.grammarQuality<<"\n";
        out<<"Typos: "<<simSettings_.persona.typoTendency<<"\n";
        out<<"Emoji tendency: "<<simSettings_.persona.emojiTendency<<"\n";
        out<<"Mood: "<<simSettings_.persona.mood<<"\n";
        out<<"Training mode: "<<Narrow(TrainingModeName())<<"\n";
        out<<"Foundation: "<<simContext_.foundationName<<" ["<<simContext_.foundationId<<"]\n";
        out<<"Adapter: "<<simContext_.adapterName<<" ["<<simContext_.adapterId<<"]\n";
        out<<"Model status: "<<Narrow(modelStatus_)<<"\n";
        out<<"Last trigger match: "<<Narrow(lastTriggerMatch_)<<"\n";
        out<<"Trigger rules: "<<triggerRules_.Rules().size()<<"\n";
        out<<"Training examples: "<<trainingData_.Examples().size()<<"\n";
        out<<"Review pending: "<<trainingData_.Count(sentinel::simulation::TrainingExampleState::Review)<<"\n";
        out<<"Approved: "<<trainingData_.Count(sentinel::simulation::TrainingExampleState::Approved)<<"\n";
        out<<"Rejected: "<<trainingData_.Count(sentinel::simulation::TrainingExampleState::Rejected)<<"\n";
        out<<"Dataset snapshots: "<<trainingData_.Snapshots().size()<<"\n";
        out<<"Training jobs: "<<trainingJobRegistry_.Jobs().size()<<"\n";
        out<<"Registered models: "<<modelRegistry_.Models().size()<<"\n";
        out<<"Foundation versions: "<<foundationRegistry_.Models().size()<<"\n";
        out<<"Persona adapters: "<<personaAdapterRegistry_.Adapters().size()<<"\n";
        out<<"Last evaluation score: "<<lastEvaluation_.score<<"\n";
        out<<"Evaluation runs: "<<evaluationRuns_.Runs().size()<<"\n";
        out<<"Policy allowed: "<<(lastEvaluation_.policyAllowed?"yes":"no")<<"\n";
        out<<"Persona consistent: "<<(lastEvaluation_.personaConsistent?"yes":"no")<<"\n\n";

        out<<"TRIGGER RULES\n";
        for(const auto& rule:triggerRules_.Rules())
            out<<rule.id<<" | "<<rule.name<<" | pattern="<<rule.pattern<<" | priority="<<rule.priority
               <<" | "<<(rule.terminal?"terminal":"continue")<<" | responses="<<rule.responses.size()<<"\n";

        out<<"\nRECENT PERSONA CONVERSATION\n";
        size_t historyStart=simContext_.history.size()>20?simContext_.history.size()-20:0;
        for(size_t i=historyStart;i<simContext_.history.size();++i) {
            const auto& turn=simContext_.history[i];
            const char* speaker=turn.speaker==sentinel::simulation::ChatTurn::Speaker::Investigator?"Investigator":
                turn.speaker==sentinel::simulation::ChatTurn::Speaker::SyntheticSubject?"SARA":"Model Suggestion";
            out<<speaker<<": "<<turn.text<<"\n";
        }

        out<<"\nRECENT TRAINING EXAMPLES\n";
        size_t start=trainingData_.Examples().size()>10?trainingData_.Examples().size()-10:0;
        for(size_t i=start;i<trainingData_.Examples().size();++i) {
            const auto& e=trainingData_.Examples()[i];
            out<<e.id<<" | "<<sentinel::simulation::ToString(e.state)<<" | "<<e.category
               <<" | persona="<<e.persona<<" | created="<<e.createdUtc<<" | reviewer="<<e.reviewer<<"\n";
        }
        out.close();
        statusText_=L"Model Lab diagnostics exported";
    }

    std::wstring HoverHelpFor(const std::wstring& id) const {
        if(id==L"sim_emoji") return L"Emoji";
        if(id==L"sim_attach") return L"Attach a file";
        if(id==L"ml_capture") return L"Capture the latest user/SARA pair for human review";
        if(id==L"ml_approve") return L"Approve the latest reviewed training example";
        if(id==L"ml_rules_manage") return L"View, add, edit, prioritize, or delete trigger rules";
        if(id==L"dataset_snapshot") return L"Freeze approved examples into a versioned dataset snapshot";
        if(id==L"dataset_import") return L"Import a portable SARA dataset snapshot and its reviewed examples";
        if(id==L"dataset_export") return L"Export the selected snapshot with its reviewed training records";
        if(id==L"adapter_export") return L"Export selected persona adapter metadata";
        if(id==L"job_new") return L"Queue a new training job from the selected foundation and dataset";
        if(id==L"foundation_new_fork") return L"Create a versioned SARA Foundation descendant without altering the base";
        if(id==L"model_activate") return L"Activate the selected approved candidate";
        if(id==L"model_rollback") return L"Roll back to the previous active model";
        if(id==L"ml_diagnostics_export") return L"Export Model Lab state, persona settings, rules, datasets, jobs, and evaluation diagnostics";
        return {};
    }

    void Hover(float x,float y) {
        std::wstring next;
        for(const auto& b:buttons_) {
            if(b.rect.Contains(x,y)) { next=b.id; break; }
        }
        if(next==hoveredButtonId_) return;
        hoveredButtonId_=next;
        hoverHelp_=HoverHelpFor(next);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void Press(float x,float y) {
        pressedButtonId_.clear();
        for(const auto& b:buttons_) {
            if(b.rect.Contains(x,y)) { pressedButtonId_=b.id; break; }
        }
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void ReleasePress() {
        if(pressedButtonId_.empty()) return;
        pressedButtonId_.clear();
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void Click(float x,float y) {
        if (x<kSidebar && y>kHeader) {
            int idx=(int)((y-kHeader-18)/48);
            if (idx>=0&&idx<12) {
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
            else if (b.id==L"sim_emoji") InsertComposerEmoji();
            else if (b.id==L"sim_attach") SelectComposerAttachment();
            else if (b.id==L"sim_suggest") GenerateSimulationSuggestion();
            else if (b.id==L"sim_reset" || b.id==L"sim_new_chat") ResetSimulation();
            else if (b.id==L"sim_previous_chat") LoadPreviousConversation();
            else if (b.id==L"sim_model") ConfigureLocalModel();
            else if (b.id==L"sim_browse_models") BrowseModels();
            else if (b.id==L"sim_preserve") PreserveSimulationTranscript();
            else if (b.id==L"persona_save") SaveProfileEditors();
            else if (b.id==L"model_register") RegisterCurrentModel();
            else if (b.id==L"model_eval") EvaluateSelectedRegistryModel();
            else if (b.id.rfind(L"evalrun:",0)==0) selectedEvaluationRun_=(int)std::stol(b.id.substr(8));
            else if (b.id.rfind(L"evalcompare:",0)==0) comparisonEvaluationRun_=(int)std::stol(b.id.substr(12));
            else if (b.id==L"eval_export_run") ExportSelectedEvaluationRun();
            else if (b.id==L"eval_export_compare") ExportEvaluationComparison();
            else if (b.id==L"model_approve") ApproveSelectedRegistryModel();
            else if (b.id==L"model_activate") ActivateSelectedRegistryModel();
            else if (b.id==L"model_rollback") RollbackRegistryModel();
            else if (b.id==L"deployment_prepare") PrepareDeploymentPackage();
            else if (b.id==L"deployment_activate") ActivateSelectedDeployment();
            else if (b.id==L"deployment_rollback") RollbackDeployment();
            else if (b.id==L"deployment_lock") ToggleDeploymentLock();
            else if (b.id==L"deployment_export") ExportDeploymentManifest();
            else if (b.id.rfind(L"deployment_select:",0)==0) {
                selectedDeployment_=(int)std::stol(b.id.substr(18));
                statusText_=L"Deployment package selected";
            }
            else if (b.id.rfind(L"mltab:",0)==0) {
                modelLabSection_=(ModelLabSection)std::clamp((int)std::stol(b.id.substr(6)),0,7);
                statusText_=L"Model Lab workspace changed";
            }
            else if (b.id==L"ml_diagnostics_export") ExportModelLabDiagnostics();
            else if (b.id==L"ml_rules_manage") OpenRuleEditor();
            else if (b.id==L"ml_rule_cancel") CloseRuleEditor();
            else if (b.id==L"ml_rule_new") NewTriggerRuleDraft();
            else if (b.id==L"ml_rule_save") SaveTriggerRuleDraft();
            else if (b.id==L"ml_rule_delete") DeleteSelectedTriggerRule();
            else if (b.id==L"ml_rule_terminal") { ruleEditorTerminal_=!ruleEditorTerminal_; statusText_=ruleEditorTerminal_?L"Rule set to stop generation":L"Rule set to continue generation"; }
            else if (b.id.rfind(L"trigger:",0)==0) SelectTriggerRule((int)std::stol(b.id.substr(8)));
            else if (b.id==L"ml_capture") CaptureLatestTrainingExample();
            else if (b.id==L"ml_approve") ApproveTrainingCapture();
            else if (b.id==L"ml_training_mode") {
                trainingMode_=(TrainingMode)(((int)trainingMode_+1)%5);
                ResolvePersonaAdapter();
                statusText_=L"Training mode changed to "+TrainingModeName();
            }
            else if (b.id==L"ml_persona_editor") {
                page_=Page::Persona;
                statusText_=L"Persona editor opened";
            }
            else if (b.id==L"ml_style_save") SaveProfileEditors();
            else if (b.id==L"adapter_new") CreatePersonaAdapter();
            else if (b.id==L"adapter_export") ExportSelectedPersonaAdapter();
            else if (b.id.rfind(L"adapter_select:",0)==0) SelectPersonaAdapter((int)std::stol(b.id.substr(15)));
            else if (b.id.rfind(L"adapter_activate:",0)==0) ActivatePersonaAdapter((size_t)std::stoul(b.id.substr(17)));
            else if (b.id==L"adapter_rollback") RollbackPersonaAdapter();
            else if (b.id==L"foundation_new_fork") CreateFoundationFork();
            else if (b.id==L"foundation_approve") ApproveSelectedFoundation();
            else if (b.id==L"foundation_activate") ActivateSelectedFoundation();
            else if (b.id==L"foundation_rollback") RollbackFoundation();
            else if (b.id==L"dataset_snapshot") CreateDatasetSnapshot();
            else if (b.id==L"dataset_import") ImportDatasetSnapshot();
            else if (b.id==L"dataset_export") ExportSelectedDatasetSnapshot();
            else if (b.id.rfind(L"dataset_select:",0)==0) SelectDatasetSnapshot((int)std::stol(b.id.substr(15)));
            else if (b.id.rfind(L"training_example:",0)==0) SelectTrainingExample((int)std::stol(b.id.substr(17)));
            else if (b.id==L"training_review_save") SaveSelectedTrainingTarget();
            else if (b.id==L"training_review_approve") ReviewSelectedTrainingExample(true);
            else if (b.id==L"training_review_reject") ReviewSelectedTrainingExample(false);
            else if (b.id==L"job_new") CreateTrainingJob();
            else if (b.id.rfind(L"job_select:",0)==0) SelectTrainingJob((int)std::stol(b.id.substr(11)));
            else if (b.id.rfind(L"job_start:",0)==0) StartTrainingJob((size_t)std::stoul(b.id.substr(10)));
            else if (b.id.rfind(L"job_complete:",0)==0) CompleteTrainingJob((size_t)std::stoul(b.id.substr(13)));
            else if (b.id.rfind(L"foundation:",0)==0) {
                selectedFoundation_=std::clamp((int)std::stol(b.id.substr(11)),0,std::max(0,(int)foundationRegistry_.Models().size()-1));
                statusText_=L"Foundation selection changed";
            }
            else if (b.id.rfind(L"regmodel:",0)==0) selectedRegistryModel_=(int)std::stol(b.id.substr(9));
            else if (b.id==L"msg_queue") QueueOperatorTestMessage();
            else if (b.id==L"approval_request") RequestLatestSuggestionApproval();
            else if (b.id==L"approval_approve") ApproveFirstPending();
            else if (b.id==L"agency_toggle") ToggleAgency();
            else if (b.id==L"agency_enqueue") EnqueueAgencySnapshot();
            else if (b.id==L"check_updates") CheckForUpdates();
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
        if(id==kSimTypingStartTimer) {
            KillTimer(hwnd_,kSimTypingStartTimer);
            if(!simReplyPending_ || simPendingMessage_.empty()) return;

            simBotTyping_=true;
            statusText_=L"Synthetic subject typing";
            ScrollSimulationToBottom();
            InvalidateRect(hwnd_,nullptr,FALSE);
            UpdateWindow(hwnd_);

            try {
                auto triggerMatch=triggerRules_.Match(simPendingMessage_,simContext_.personaSummary,simContext_.history.size());
                if(triggerMatch && !triggerMatch->response.empty()) {
                    simPreparedReply_=triggerMatch->response;
                    lastTriggerMatch_=Widen(triggerMatch->ruleName);
                    if(!triggerMatch->terminal && model_) {
                        auto generated=model_->GenerateSyntheticReply(simPendingMessage_,simContext_);
                        if(!generated.empty()) simPreparedReply_+=" "+generated;
                    }
                    AppendTriggerMatchLog(*triggerMatch,simPendingMessage_,simPreparedReply_);
                } else if(model_) {
                    lastTriggerMatch_=L"None";
                    simPreparedReply_=model_->GenerateSyntheticReply(simPendingMessage_,simContext_);
                }
                if(!simPreparedReply_.empty() && simPreparedReply_.rfind("Model error:",0)!=0) {
                    simPreparedReply_=sentinel::simulation::ApplyPersonaWritingVariation(
                        simSettings_.persona,simPreparedReply_,simContext_.history.size()+simPendingMessage_.size());
                }
            } catch(const std::exception& e) {
                simPreparedReply_=std::string("Model error: ")+e.what();
            }

            int typingDelay=1200+(int)simPreparedReply_.size()*42;
            typingDelay=std::clamp(typingDelay,1800,9000);
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
                    statusText_=simContext_.recalledMemory.empty()
                        ? L"Model response received"
                        : L"Model response received with prior-conversation context";
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
        sentinel::simulation::SaveSession(runtime_->root/"simulation-session.tsv",simContext_);
        ScrollSimulationToBottom();
        SetFocus(chatEdit_);
        SendMessageW(chatEdit_,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

private:
    struct Button { RectF rect; std::wstring id; };

    HWND hwnd_{},caseNumberEdit_{},caseTitleEdit_{},chatEdit_{},modelEndpointEdit_{},modelNameEdit_{},modelCombo_{},simScroll_{};
    HWND personaNameEdit_{},personaAgeCombo_{},personaLocationEdit_{},personaInterestsEdit_{},personaStyleEdit_{};
    HWND personaOccupationEdit_{},personaEducationEdit_{},personaFamilyEdit_{},personaBackgroundEdit_{};
    HWND personaGenderCombo_{},personaPronounsCombo_{},personaRelationshipCombo_{},personaPersonalityCombo_{},personaSocialCombo_{},personaConfidenceCombo_{};
    HWND personaIntelligenceCombo_{},personaSlangCombo_{},personaGrammarCombo_{},personaTypoCombo_{},personaEmojiCombo_{},personaMoodCombo_{};
    HWND scenarioNameEdit_{},scenarioObjectiveEdit_{},scenarioSeedEdit_{},minDelayEdit_{},maxDelayEdit_{},ageStateCombo_{};
    HWND agencyEndpointEdit_{},agencyIdEdit_{};
    HWND trainingCorrectionEdit_{},trainingCategoryCombo_{},trainingReviewTargetEdit_{};
    HWND ruleNameEdit_{},rulePatternEdit_{},ruleResponsesEdit_{},rulePriorityEdit_{};
    std::unique_ptr<Runtime> runtime_;
    Page page_{Page::Dashboard};
    ModelLabSection modelLabSection_{ModelLabSection::Overview};
    TrainingMode trainingMode_{TrainingMode::BehaviorTuning};
    std::wstring assignedPersonaLoRA_=L"Auto-resolve";
    int trainingCaptured_{0};
    int trainingReviewPending_{0};
    int trainingApproved_{0};
    int selectedTrainingExample_{-1};
    int selectedDatasetSnapshot_{-1};
    int selectedPersonaAdapter_{-1};
    int selectedTrainingJob_{-1};
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
    std::string simPendingMessage_;
    std::string simPreparedReply_;
    std::vector<std::pair<RectF,size_t>> simMessageRects_;
    sentinel::simulation::SimulationSettings simSettings_;
    std::unique_ptr<sentinel::operations::IMessageAdapter> messagingAdapter_;
    std::vector<sentinel::operations::ApprovalRequest> approvals_;
    sentinel::simulation::ModelRegistry modelRegistry_;
    sentinel::simulation::FoundationRegistry foundationRegistry_;
    sentinel::simulation::PersonaAdapterRegistry personaAdapterRegistry_;
    sentinel::simulation::TrainingJobRegistry trainingJobRegistry_;
    sentinel::simulation::TriggerRuleRegistry triggerRules_;
    sentinel::simulation::TrainingDataRegistry trainingData_;
    int selectedRegistryModel_{-1};
    int selectedFoundation_{0};
    sentinel::simulation::ResponseEvaluation lastEvaluation_;
    sentinel::simulation::EvaluationRunRegistry evaluationRuns_;
    sentinel::simulation::DeploymentRegistry deploymentRegistry_;
    int selectedEvaluationRun_{-1};
    int comparisonEvaluationRun_{-1};
    int selectedDeployment_{-1};
    sentinel::agency::AgencyServerConfig agencyConfig_;
    sentinel::agency::AgencySyncQueue agencyQueue_;
    std::wstring policyStatus_=L"Policy ready";
    std::wstring updateStatus_=L"Updates not checked";
    std::wstring lastTriggerMatch_=L"None";
    bool ruleEditorOpen_{false};
    bool ruleEditorTerminal_{true};
    int selectedTriggerRule_{-1};

    HFONT chatFont_{};
    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<IDWriteFactory> writeFactory_;
    ComPtr<IDWriteTextFormat> titleFmt_,h1Fmt_,bodyFmt_,smallFmt_,tinyFmt_,bigFmt_;
    BrushSet brush_;
    bool brushesReady_{false};
    std::vector<Button> buttons_;
    std::wstring hoveredButtonId_;
    std::wstring pressedButtonId_;
    std::wstring hoverHelp_;

    void CreateResources() {
        if (!target_) {
            RECT rc{}; GetClientRect(hwnd_,&rc);
            factory_->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(hwnd_,D2D1::SizeU(std::max(1L,rc.right),std::max(1L,rc.bottom))),
                &target_);
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

    void Text(const std::wstring& s,float x,float y,float w,float h,IDWriteTextFormat* fmt,ID2D1Brush* br) {
        target_->DrawTextW(s.c_str(),(UINT32)s.size(),fmt,D2D1::RectF(x,y,x+w,y+h),br);
    }

    void TextLine(const std::wstring& s,float x,float y,float w,float h,IDWriteTextFormat* fmt,ID2D1Brush* br,
                  DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING) {
        if(w<=1 || h<=1) return;
        ComPtr<IDWriteTextLayout> layout;
        if(FAILED(writeFactory_->CreateTextLayout(s.c_str(),(UINT32)s.size(),fmt,w,h,&layout)) || !layout) return;
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        layout->SetTextAlignment(align);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};
        ComPtr<IDWriteInlineObject> sign;
        if(SUCCEEDED(writeFactory_->CreateEllipsisTrimmingSign(fmt,&sign)))
            layout->SetTrimming(&trim,sign.Get());
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
        const bool hover=hoveredButtonId_==id;
        const bool pressed=pressedButtonId_==id;
        ID2D1Brush* fill=primary?brush_.blue.Get():brush_.panel2.Get();
        ID2D1Brush* stroke=primary?brush_.cyan.Get():brush_.border.Get();
        if(pressed) fill=brush_.sidebar.Get();
        else if(hover && !primary) fill=brush_.panel.Get();
        if(hover) stroke=brush_.cyan.Get();
        Rounded(x,y,w,h,fill,stroke,7);
        TextLine(label,x+10,y+1,w-20,h-2,smallFmt_.Get(),hover?brush_.cyan.Get():brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
        buttons_.push_back({{x,y,x+w,y+h},id});
    }

    void AddIconButton(const std::wstring& id,IconKind icon,float x,float y,float size,bool primary=false) {
        const bool hover=hoveredButtonId_==id;
        const bool pressed=pressedButtonId_==id;
        ID2D1Brush* fill=primary?brush_.blue.Get():brush_.panel2.Get();
        ID2D1Brush* stroke=primary?brush_.cyan.Get():brush_.border.Get();
        if(pressed) fill=brush_.sidebar.Get();
        else if(hover && !primary) fill=brush_.panel.Get();
        if(hover) stroke=brush_.cyan.Get();
        Rounded(x,y,size,size,fill,stroke,8);
        const float iconSize=size*0.54f;
        DrawIcon(icon,x+(size-iconSize)/2.0f,y+(size-iconSize)/2.0f,iconSize,hover?brush_.cyan.Get():brush_.text.Get());
        buttons_.push_back({{x,y,x+size,y+size},id});
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
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.50f,y+s*0.50f),s*0.36f,s*0.36f),color,t);
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.37f,y+s*0.42f),s*0.035f,s*0.035f),color);
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x+s*0.63f,y+s*0.42f),s*0.035f,s*0.035f),color);
                {
                    ComPtr<ID2D1PathGeometry> geo; factory_->CreatePathGeometry(&geo);
                    ComPtr<ID2D1GeometrySink> sink; geo->Open(&sink);
                    sink->BeginFigure(D2D1::Point2F(x+s*0.34f,y+s*0.58f),D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddBezier(D2D1::BezierSegment(
                        D2D1::Point2F(x+s*0.40f,y+s*0.70f),
                        D2D1::Point2F(x+s*0.60f,y+s*0.70f),
                        D2D1::Point2F(x+s*0.66f,y+s*0.58f)));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN); sink->Close();
                    target_->DrawGeometry(geo.Get(),color,t);
                }
                break;
            case IconKind::Paperclip: {
                ComPtr<ID2D1PathGeometry> geo; factory_->CreatePathGeometry(&geo);
                ComPtr<ID2D1GeometrySink> sink; geo->Open(&sink);
                sink->BeginFigure(D2D1::Point2F(x+s*0.66f,y+s*0.22f),D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddBezier(D2D1::BezierSegment(
                    D2D1::Point2F(x+s*0.83f,y+s*0.38f),
                    D2D1::Point2F(x+s*0.83f,y+s*0.62f),
                    D2D1::Point2F(x+s*0.66f,y+s*0.78f)));
                sink->AddLine(D2D1::Point2F(x+s*0.43f,y+s*0.91f));
                sink->AddBezier(D2D1::BezierSegment(
                    D2D1::Point2F(x+s*0.27f,y+s*0.98f),
                    D2D1::Point2F(x+s*0.12f,y+s*0.83f),
                    D2D1::Point2F(x+s*0.20f,y+s*0.66f)));
                sink->AddLine(D2D1::Point2F(x+s*0.55f,y+s*0.31f));
                sink->AddBezier(D2D1::BezierSegment(
                    D2D1::Point2F(x+s*0.63f,y+s*0.23f),
                    D2D1::Point2F(x+s*0.75f,y+s*0.31f),
                    D2D1::Point2F(x+s*0.67f,y+s*0.40f)));
                sink->AddLine(D2D1::Point2F(x+s*0.37f,y+s*0.70f));
                sink->EndFigure(D2D1_FIGURE_END_OPEN); sink->Close();
                target_->DrawGeometry(geo.Get(),color,t);
                break;
            }
        }
    }

    IconKind NavIcon(int i) const {
        static const IconKind icons[]={
            IconKind::Home,IconKind::Folder,IconKind::Database,IconKind::Document,IconKind::Shield,
            IconKind::Chat,IconKind::Document,IconKind::Database,IconKind::Chat,IconKind::Shield,IconKind::Database,IconKind::Gear
        };
        return icons[std::clamp(i,0,11)];
    }

    void DrawBrand() {
        DrawShield(22,14,48,brush_.cyan.Get(),brush_.panel2.Get(),false);
        DrawShield(31,24,30,brush_.blue.Get(),nullptr,false);
        Text(L"SARA",78,15,132,40,titleFmt_.Get(),brush_.text.Get());
        Text(L"SYNTHETIC ADAPTIVE RESPONSE AGENT",79,51,136,18,tinyFmt_.Get(),brush_.muted.Get());
    }

    void DrawSidebar() {
        static const wchar_t* names[]={
            L"Dashboard",L"Cases",L"Evidence",L"Audit Log",L"Verification",L"Simulation Lab",
            L"Persona & Policy",L"Model Lab",L"Messaging",L"Supervisor",L"Agency Server",L"Settings"
        };
        for (int i=0;i<12;i++) {
            float y=(float)kHeader+18+i*48;
            if ((int)page_==i) {
                target_->FillRectangle(D2D1::RectF(0,y-5,(float)kSidebar,y+39),brush_.panel2.Get());
                target_->FillRectangle(D2D1::RectF(0,y-5,4,y+39),brush_.cyan.Get());
                Rounded(20,y,36,32,brush_.sidebar.Get(),brush_.border.Get(),8);
            }
            DrawIcon(NavIcon(i),26,y+4,23,((int)page_==i)?brush_.cyan.Get():brush_.muted.Get());
            TextLine(names[i],66,y+4,145,28,smallFmt_.Get(),((int)page_==i)?brush_.cyan.Get():brush_.text.Get());
        }
        Text(L"SARA v1.0.18",24,674,170,20,smallFmt_.Get(),brush_.muted.Get());
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
        if(!hoverHelp_.empty())
            TextLine(hoverHelp_,kSidebar+300,56,std::max(180.0f,w-kSidebar-500.0f),20,tinyFmt_.Get(),brush_.cyan.Get());
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

        // Composer: message field + icon-only emoji/attachment controls + primary Send.
        // Icons are vector-drawn so rendering is consistent across Windows installations.
        const float composerY=y+452;
        const float sendW=116.0f;
        const float iconSize=46.0f;
        const float sendX=x+chatW-18-sendW;
        const float attachX=sendX-8-iconSize;
        const float emojiX=attachX-8-iconSize;
        AddIconButton(L"sim_emoji",IconKind::Smile,emojiX,composerY,iconSize,false);
        AddIconButton(L"sim_attach",IconKind::Paperclip,attachX,composerY,iconSize,false);
        AddButton(L"sim_send",L"Send",sendX,composerY,sendW,46,true);

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
        TextLine(L"Available model",rx+158,y+180,110,20,tinyFmt_.Get(),brush_.muted.Get());

        TextLine(L"Manual model",rx+18,y+230,106,20,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"sim_model",L"Connect",rx+sideW-110,y+251,92,32,true);

        StatusDot(rx+24,y+307,4,modelStatus_.find(L"Connected")!=std::wstring::npos?brush_.green.Get():brush_.yellow.Get());
        TextLine(modelStatus_,rx+36,y+294,sideW-54,28,tinyFmt_.Get(),brush_.text.Get());

        TextLine(L"Conversation",rx+18,y+326,90,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(currentConversationTitle_,rx+112,y+323,sideW-130,24,smallFmt_.Get(),brush_.text.Get());
        AddButton(L"sim_previous_chat",L"Previous Chat",rx+18,y+354,142,34,false);
        AddButton(L"sim_new_chat",L"New Chat",rx+170,y+354,112,34,true);

        // Suggestion card
        Rounded(rx,y+400,sideW,140,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Model Suggestion",rx+18,y+410,sideW-36,28,h1Fmt_.Get(),brush_.text.Get());
        Rounded(rx+18,y+444,sideW-36,48,brush_.sidebar.Get(),brush_.border.Get(),8);
        Text(simSuggestion_,rx+28,y+451,sideW-56,34,tinyFmt_.Get(),brush_.text.Get());

        const float bw=(sideW-52)/3.0f;
        AddButton(L"sim_suggest",L"Generate",rx+18,y+500,bw,32,false);
        AddButton(L"sim_reset",L"Reset",rx+26+bw,y+500,bw,32,false);
        AddButton(L"sim_preserve",L"Preserve",rx+34+bw*2,y+500,bw,32,false);
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
            personaNameEdit_,personaAgeCombo_,personaLocationEdit_,personaInterestsEdit_,personaStyleEdit_,
            personaOccupationEdit_,personaEducationEdit_,personaFamilyEdit_,personaBackgroundEdit_,
            personaGenderCombo_,personaPronounsCombo_,personaRelationshipCombo_,personaPersonalityCombo_,personaSocialCombo_,personaConfidenceCombo_,
            scenarioNameEdit_,scenarioObjectiveEdit_,scenarioSeedEdit_,minDelayEdit_,maxDelayEdit_,ageStateCombo_
        };
        for(HWND h:controls) if(h) ShowWindow(h,show?SW_SHOW:SW_HIDE);
    }

    void ShowAgencyEditors(bool show) {
        if(agencyEndpointEdit_) ShowWindow(agencyEndpointEdit_,show?SW_SHOW:SW_HIDE);
        if(agencyIdEdit_) ShowWindow(agencyIdEdit_,show?SW_SHOW:SW_HIDE);
    }

    void ApplyPageControls() {
        ShowCaseEditors(page_==Page::Cases);
        ShowChatEditor(page_==Page::Simulation);
        if(page_==Page::ModelLab && modelLabSection_==ModelLabSection::Train) {
            if(chatEdit_) ShowWindow(chatEdit_,SW_SHOW);
        }
        ShowPersonaEditors(page_==Page::Persona);
        const bool inTrain=page_==Page::ModelLab && modelLabSection_==ModelLabSection::Train;
        const bool showRuleEditor=inTrain && ruleEditorOpen_;
        if(trainingCorrectionEdit_) ShowWindow(trainingCorrectionEdit_,(inTrain && !ruleEditorOpen_)?SW_SHOW:SW_HIDE);
        if(trainingCategoryCombo_) ShowWindow(trainingCategoryCombo_,(inTrain && !ruleEditorOpen_)?SW_SHOW:SW_HIDE);
        const bool inDatasetReview=page_==Page::ModelLab && modelLabSection_==ModelLabSection::Datasets && selectedTrainingExample_>=0;
        if(trainingReviewTargetEdit_) ShowWindow(trainingReviewTargetEdit_,inDatasetReview?SW_SHOW:SW_HIDE);
        HWND ruleControls[]={ruleNameEdit_,rulePatternEdit_,ruleResponsesEdit_,rulePriorityEdit_};
        for(HWND h:ruleControls) if(h) ShowWindow(h,showRuleEditor?SW_SHOW:SW_HIDE);
        const bool showStyleCombos=page_==Page::ModelLab && modelLabSection_==ModelLabSection::Personas;
        HWND styleCombos[]={personaIntelligenceCombo_,personaSlangCombo_,personaGrammarCombo_,personaTypoCombo_,personaEmojiCombo_,personaMoodCombo_};
        for(HWND h:styleCombos) if(h) ShowWindow(h,showStyleCombos?SW_SHOW:SW_HIDE);
        ShowAgencyEditors(page_==Page::Agency);
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

            const int composerW=(int)(chatW-266);
            const int composerH=46;
            MoveControl(chatEdit_,(int)(x+18),(int)(y+452),composerW,composerH,TRUE);
            RECT composerTextRect{12,9,std::max(24,composerW-12),composerH-8};
            SendMessageW(chatEdit_,EM_SETRECTNP,0,(LPARAM)&composerTextRect);

            MoveControl(modelEndpointEdit_,(int)(rx+18),(int)(y+138),(int)(sideW-36),32,TRUE);
            MoveControl(modelCombo_,(int)(rx+158),(int)(y+201),(int)(sideW-176),150,TRUE);
            MoveControl(modelNameEdit_,(int)(rx+18),(int)(y+251),(int)(sideW-140),32,TRUE);
        }

        if(page_==Page::ModelLab && modelLabSection_==ModelLabSection::Train) {
            const float x=kSidebar+28.0f, top=kHeader+102.0f;
            const float bodyY=top+116.0f;
            const float contentW=w-x-28.0f;
            const float rightW=340.0f;
            const float gap=12.0f;
            const float leftW=contentW-rightW-gap;
            if(ruleEditorOpen_) {
                if(chatEdit_) ShowWindow(chatEdit_,SW_HIDE);
                MoveControl(ruleNameEdit_,(int)(x+142),(int)(bodyY+74),(int)(leftW-164),30,TRUE);
                MoveControl(rulePatternEdit_,(int)(x+142),(int)(bodyY+116),(int)(leftW-164),30,TRUE);
                MoveControl(ruleResponsesEdit_,(int)(x+142),(int)(bodyY+158),(int)(leftW-164),94,TRUE);
                MoveControl(rulePriorityEdit_,(int)(x+142),(int)(bodyY+264),110,30,TRUE);
            } else {
                if(chatEdit_) ShowWindow(chatEdit_,SW_SHOW);
                MoveControl(trainingCorrectionEdit_,(int)(x+132),(int)(bodyY+238),(int)(leftW-296),44,TRUE);
                MoveControl(trainingCategoryCombo_,(int)(x+leftW-152),(int)(bodyY+238),136,140,TRUE);
                RECT correctionRect{10,7,std::max(24,(int)(leftW-316)),38};
                SendMessageW(trainingCorrectionEdit_,EM_SETRECTNP,0,(LPARAM)&correctionRect);
                const int composerW=std::max(220,(int)(leftW-212));
                const int composerH=44;
                MoveControl(chatEdit_,(int)(x+16),(int)(bodyY+292),composerW,composerH,TRUE);
                RECT composerTextRect{12,8,std::max(24,composerW-12),composerH-7};
                SendMessageW(chatEdit_,EM_SETRECTNP,0,(LPARAM)&composerTextRect);
            }
        }

        if(page_==Page::ModelLab && modelLabSection_==ModelLabSection::Datasets && selectedTrainingExample_>=0) {
            const float x=kSidebar+28.0f, top=kHeader+102.0f, bodyY=top+116.0f;
            const float contentW=w-x-28.0f, gap=12.0f, rightW=350.0f;
            const float leftW=contentW-rightW-gap;
            const float rx=x+leftW+gap;
            MoveControl(trainingReviewTargetEdit_,(int)(rx+18),(int)(bodyY+142),(int)(rightW-36),70,TRUE);
            RECT reviewRect{10,8,std::max(24,(int)rightW-56),62};
            SendMessageW(trainingReviewTargetEdit_,EM_SETRECTNP,0,(LPARAM)&reviewRect);
        }

        if(page_==Page::ModelLab && modelLabSection_==ModelLabSection::Personas) {
            const float x=kSidebar+28.0f, top=kHeader+102.0f;
            const float bodyY=top+116.0f;
            const float contentW=w-x-28.0f, gap=12.0f, inspectorW=360.0f;
            const float listW=contentW-inspectorW-gap;
            const float rx=x+listW+gap;
            const int comboW=150, comboH=130;
            MoveControl(personaIntelligenceCombo_,(int)(rx+18),(int)(bodyY+244),comboW,comboH,TRUE);
            MoveControl(personaSlangCombo_,(int)(rx+184),(int)(bodyY+244),comboW,comboH,TRUE);
            MoveControl(personaGrammarCombo_,(int)(rx+18),(int)(bodyY+282),comboW,comboH,TRUE);
            MoveControl(personaTypoCombo_,(int)(rx+184),(int)(bodyY+282),comboW,comboH,TRUE);
            MoveControl(personaEmojiCombo_,(int)(rx+18),(int)(bodyY+320),comboW,comboH,TRUE);
            MoveControl(personaMoodCombo_,(int)(rx+184),(int)(bodyY+320),comboW,comboH,TRUE);
        }

        if(page_==Page::Persona) {
            const float x=kSidebar+28.0f, y=kHeader+104.0f, gap=14.0f;
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
            MoveControl(personaStyleEdit_,(int)rf,(int)row,(int)rw,32); row+=42;
            MoveControl(personaFamilyEdit_,(int)rf,(int)row,(int)rw,32); row+=42;
            MoveControl(personaBackgroundEdit_,(int)rf,(int)row,(int)rw,32);

            const float sy=y+396;
            MoveControl(scenarioNameEdit_,(int)(x+92),(int)(sy+50),210,32);
            MoveControl(scenarioObjectiveEdit_,(int)(x+390),(int)(sy+50),(int)std::max(220.0f,contentW-690),32);
            MoveControl(scenarioSeedEdit_,(int)(x+72),(int)(sy+92),74,32);
            MoveControl(minDelayEdit_,(int)(x+250),(int)(sy+92),86,32);
            MoveControl(maxDelayEdit_,(int)(x+370),(int)(sy+92),86,32);
        }

        if(page_==Page::Agency) {
            const float x=kSidebar+28.0f, y=kHeader+104.0f, gap=14.0f;
            const float contentW=w-x-28.0f;
            const float leftW=(contentW-gap)*0.58f;
            MoveControl(agencyEndpointEdit_,(int)(x+142),(int)(y+62),(int)(leftW-164),32);
            MoveControl(agencyIdEdit_,(int)(x+142),(int)(y+108),(int)(leftW-164),32);
        }
    }

    std::wstring EditText(HWND h) const {
        int len=GetWindowTextLengthW(h);
        std::wstring value((size_t)len+1,L'\0');
        GetWindowTextW(h,value.data(),len+1);
        value.resize((size_t)len);
        return value;
    }

    void LoadProfileEditors() {
        SetWindowTextW(personaNameEdit_,Widen(simSettings_.persona.name).c_str());
        SendMessageW(personaAgeCombo_,CB_SETCURSEL,(WPARAM)std::clamp(simSettings_.persona.age-13,0,77),0);
        SetWindowTextW(personaLocationEdit_,Widen(simSettings_.persona.location).c_str());
        SetWindowTextW(personaOccupationEdit_,Widen(simSettings_.persona.occupation).c_str());
        SetWindowTextW(personaEducationEdit_,Widen(simSettings_.persona.education).c_str());
        SetWindowTextW(personaFamilyEdit_,Widen(simSettings_.persona.familyContext).c_str());
        SetWindowTextW(personaBackgroundEdit_,Widen(simSettings_.persona.background).c_str());
        SetWindowTextW(personaInterestsEdit_,Widen(simSettings_.persona.interests).c_str());
        SetWindowTextW(personaStyleEdit_,Widen(simSettings_.persona.writingStyle).c_str());

        SendMessageW(personaGenderCombo_,CB_SETCURSEL,FindComboText(personaGenderCombo_,simSettings_.persona.gender),0);
        SendMessageW(personaPronounsCombo_,CB_SETCURSEL,FindComboText(personaPronounsCombo_,simSettings_.persona.pronouns),0);
        SendMessageW(personaRelationshipCombo_,CB_SETCURSEL,FindComboText(personaRelationshipCombo_,simSettings_.persona.relationshipStatus),0);
        SendMessageW(personaPersonalityCombo_,CB_SETCURSEL,FindComboText(personaPersonalityCombo_,simSettings_.persona.personality),0);
        SendMessageW(personaSocialCombo_,CB_SETCURSEL,FindComboText(personaSocialCombo_,simSettings_.persona.socialStyle),0);
        SendMessageW(personaConfidenceCombo_,CB_SETCURSEL,FindComboText(personaConfidenceCombo_,simSettings_.persona.confidenceLevel),0);
        SendMessageW(personaIntelligenceCombo_,CB_SETCURSEL,FindComboText(personaIntelligenceCombo_,simSettings_.persona.intelligenceLevel),0);
        SendMessageW(personaSlangCombo_,CB_SETCURSEL,FindComboText(personaSlangCombo_,simSettings_.persona.slangLevel),0);
        SendMessageW(personaGrammarCombo_,CB_SETCURSEL,FindComboText(personaGrammarCombo_,simSettings_.persona.grammarQuality),0);
        SendMessageW(personaTypoCombo_,CB_SETCURSEL,FindComboText(personaTypoCombo_,simSettings_.persona.typoTendency),0);
        SendMessageW(personaEmojiCombo_,CB_SETCURSEL,FindComboText(personaEmojiCombo_,simSettings_.persona.emojiTendency),0);
        SendMessageW(personaMoodCombo_,CB_SETCURSEL,FindComboText(personaMoodCombo_,simSettings_.persona.mood),0);

        SetWindowTextW(scenarioNameEdit_,Widen(simSettings_.scenario.name).c_str());
        SetWindowTextW(scenarioObjectiveEdit_,Widen(simSettings_.scenario.objective).c_str());
        SetWindowTextW(scenarioSeedEdit_,std::to_wstring(simSettings_.scenario.seed).c_str());
        SetWindowTextW(minDelayEdit_,std::to_wstring(simSettings_.minDelayMs).c_str());
        SetWindowTextW(maxDelayEdit_,std::to_wstring(simSettings_.maxDelayMs).c_str());
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
            simSettings_.persona.writingStyle=Narrow(EditText(personaStyleEdit_));
            simSettings_.persona.intelligenceLevel=ComboText(personaIntelligenceCombo_);
            simSettings_.persona.slangLevel=ComboText(personaSlangCombo_);
            simSettings_.persona.grammarQuality=ComboText(personaGrammarCombo_);
            simSettings_.persona.typoTendency=ComboText(personaTypoCombo_);
            simSettings_.persona.emojiTendency=ComboText(personaEmojiCombo_);
            simSettings_.persona.mood=ComboText(personaMoodCombo_);
            simSettings_.scenario.name=Narrow(EditText(scenarioNameEdit_));
            simSettings_.scenario.objective=Narrow(EditText(scenarioObjectiveEdit_));
            simSettings_.scenario.seed=(unsigned int)std::max(1,std::stoi(EditText(scenarioSeedEdit_)));
            simSettings_.minDelayMs=std::clamp(std::stoi(EditText(minDelayEdit_)),500,30000);
            simSettings_.maxDelayMs=std::clamp(std::stoi(EditText(maxDelayEdit_)),simSettings_.minDelayMs,60000);
            int ageSel=(int)SendMessageW(ageStateCombo_,CB_GETCURSEL,0,0);
            if(ageSel>=0 && ageSel<=5) simSettings_.ageState=(sentinel::simulation::AgeKnowledgeState)ageSel;
            simSettings_.endpoint=Narrow(EditText(modelEndpointEdit_));
            simSettings_.model=Narrow(EditText(modelNameEdit_));
            sentinel::simulation::SaveSimulationSettings(runtime_->root/"simulation.ini",simSettings_);

            simContext_.scenario=simSettings_.scenario.name+": "+simSettings_.scenario.objective;
            simContext_.personaSummary=simSettings_.persona.name+", age "+std::to_string(simSettings_.persona.age)+
                ", gender "+simSettings_.persona.gender+", pronouns "+simSettings_.persona.pronouns+
                ", location "+simSettings_.persona.location+", occupation "+simSettings_.persona.occupation+
                ", education "+simSettings_.persona.education+", relationship status "+simSettings_.persona.relationshipStatus+
                ", family context "+simSettings_.persona.familyContext+", personality "+simSettings_.persona.personality+
                ", social style "+simSettings_.persona.socialStyle+", confidence "+simSettings_.persona.confidenceLevel+
                ", background "+simSettings_.persona.background+", interests "+simSettings_.persona.interests+
                ", writing style "+simSettings_.persona.writingStyle+".";
            policyStatus_=L"Profile saved. Age state: "+Widen(sentinel::simulation::ToString(simSettings_.ageState));
            ResolvePersonaAdapter();
            statusText_=L"Persona, policy, scenario, and delay settings saved";
        } catch(const std::exception& e) {
            statusText_=L"Profile save failed";
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Save Profile Failed",MB_OK|MB_ICONERROR);
        }
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

    void InsertComposerEmoji() {
        if(!chatEdit_) return;
        DWORD start=0,end=0;
        SendMessageW(chatEdit_,EM_GETSEL,(WPARAM)&start,(LPARAM)&end);
        SendMessageW(chatEdit_,EM_REPLACESEL,TRUE,(LPARAM)L"\U0001F642");
        SetFocus(chatEdit_);
        statusText_=L"Emoji inserted";
    }

    void SelectComposerAttachment() {
        wchar_t file[MAX_PATH]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn);
        ofn.hwndOwner=hwnd_;
        ofn.lpstrFile=file;
        ofn.nMaxFile=MAX_PATH;
        ofn.lpstrFilter=L"All Files\0*.*\0Images\0*.png;*.jpg;*.jpeg;*.gif;*.webp\0\0";
        ofn.nFilterIndex=1;
        ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if(!GetOpenFileNameW(&ofn)) return;
        std::filesystem::path selected(file);
        const std::wstring marker=L" [Attachment: "+selected.filename().wstring()+L"]";
        SendMessageW(chatEdit_,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);
        SendMessageW(chatEdit_,EM_REPLACESEL,TRUE,(LPARAM)marker.c_str());
        SetFocus(chatEdit_);
        statusText_=L"Attachment added to draft";
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
        simContext_.recalledMemory=runtime_->conversationMemory.RecallRelevant(
            utf8,currentConversationId_,12);
        simContext_.history.push_back({sentinel::simulation::ChatTurn::Speaker::Investigator,utf8});
        runtime_->conversationMemory.Append(
            currentConversationId_,
            sentinel::simulation::ChatTurn::Speaker::Investigator,
            utf8);
        sentinel::simulation::SaveSession(runtime_->root/"simulation-session.tsv",simContext_);
        SetWindowTextW(chatEdit_,L"");
        simSuggestion_=L"No suggestion generated yet";
        simPendingMessage_=utf8;
        simPreparedReply_.clear();
        simReplyPending_=true;
        simBotTyping_=false;
        ScrollSimulationToBottom();

        // Human-like pacing: first read/think silently, then show typing.
        int readingDelay=1400+(int)utf8.size()*28;
        readingDelay=std::clamp(readingDelay,1600,5200);
        SetTimer(hwnd_,kSimTypingStartTimer,(UINT)readingDelay,nullptr);
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
            }
            modelStatus_=L"Found "+std::to_wstring(models.size())+L" model(s). Select one and Connect.";
            statusText_=L"Model list loaded";
        } catch(const std::exception& e) {
            modelStatus_=L"Browse failed: "+Widen(e.what());
            statusText_=L"Model discovery failed";
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
            auto candidate=sentinel::simulation::CreateOpenAICompatibleModel(Narrow(we),Narrow(wm));
            sentinel::simulation::ModelContext testContext;
            testContext.scenario="Sentinel local model connection test";
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
                if(a.action.rfind(prefix,0)==0 && messagingAdapter_) {
                    messagingAdapter_->QueueOperatorApproved("local-sim",a.action.substr(prefix.size()));
                }
                statusText_=L"Supervisor approval recorded and message queued";
                return;
            }
        }
        statusText_=L"No pending approvals";
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

        // Scenario & pacing
        const float sy=y+396;
        Rounded(x,sy,contentW,144,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Scenario, Policy & Pacing",x+18,sy+10,300,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"Scenario",x+20,sy+50,68,30,tinyFmt_.Get(),brush_.muted.Get());

        TextLine(L"Objective",x+316,sy+50,70,30,tinyFmt_.Get(),brush_.muted.Get());

        TextLine(L"Seed",x+20,sy+92,48,30,tinyFmt_.Get(),brush_.muted.Get());

        TextLine(L"Typing delay",x+164,sy+92,82,30,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"to",x+342,sy+92,24,30,tinyFmt_.Get(),brush_.muted.Get());

        AddButton(L"persona_save",L"Save Persona & Policy",x+contentW-210,sy+88,190,38,true);

        // Compact policy status
        TextLine(L"Policy",x+474,sy+92,52,30,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(policyStatus_,x+530,sy+88,contentW-760,38,tinyFmt_.Get(),brush_.cyan.Get());
    }

    void ResolvePersonaAdapter() {
        simContext_.foundationId.clear();
        simContext_.foundationName.clear();
        simContext_.adapterId.clear();
        simContext_.adapterName.clear();
        simContext_.trainingMode=Narrow(TrainingModeName());

        int fi=foundationRegistry_.ActiveIndex();
        if(fi<0 && !foundationRegistry_.Models().empty()) fi=0;
        if(fi>=0 && fi<(int)foundationRegistry_.Models().size()) {
            const auto& f=foundationRegistry_.Models()[(size_t)fi];
            simContext_.foundationId=f.id;
            simContext_.foundationName=f.name+" "+f.version;
        }

        int idx=personaAdapterRegistry_.ResolveActiveIndex(simSettings_.persona.name);
        if(idx>=0 && idx<(int)personaAdapterRegistry_.Adapters().size()) {
            const auto& a=personaAdapterRegistry_.Adapters()[(size_t)idx];
            assignedPersonaLoRA_=Widen(a.adapterName+" "+a.version);
            simContext_.adapterId=a.id;
            simContext_.adapterName=a.adapterName+" "+a.version;
        } else {
            assignedPersonaLoRA_=L"No active LoRA";
        }
    }

    void SelectPersonaAdapter(int index) {
        if(index<0 || index>=(int)personaAdapterRegistry_.Adapters().size()) return;
        if(personaAdapterRegistry_.Adapters()[(size_t)index].personaName!=simSettings_.persona.name) return;
        selectedPersonaAdapter_=index;
        statusText_=L"Persona adapter selected";
    }

    void ExportSelectedPersonaAdapter() {
        if(selectedPersonaAdapter_<0 || selectedPersonaAdapter_>=(int)personaAdapterRegistry_.Adapters().size()) {
            int active=personaAdapterRegistry_.ResolveActiveIndex(simSettings_.persona.name);
            if(active<0) { statusText_=L"Select a persona adapter first"; return; }
            selectedPersonaAdapter_=active;
        }
        const auto& a=personaAdapterRegistry_.Adapters()[(size_t)selectedPersonaAdapter_];
        wchar_t file[MAX_PATH]{};
        auto defaultName=Widen(a.personaName+"-"+a.adapterName+"-"+a.version+".sara-adapter");
        wcsncpy_s(file,defaultName.c_str(),_TRUNCATE);
        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hwnd_; ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
        ofn.lpstrFilter=L"SARA Adapter Metadata\0*.sara-adapter\0Text Files\0*.txt\0All Files\0*.*\0\0";
        ofn.lpstrDefExt=L"sara-adapter"; ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
        if(!GetSaveFileNameW(&ofn)) return;
        std::ofstream out(std::filesystem::path(file),std::ios::trunc);
        out<<"SARA_ADAPTER_V1\n";
        out<<"id="<<a.id<<"\n";
        out<<"persona="<<a.personaName<<"\n";
        out<<"name="<<a.adapterName<<"\n";
        out<<"version="<<a.version<<"\n";
        out<<"foundation="<<a.foundationId<<"\n";
        out<<"stage="<<sentinel::simulation::ToString(a.stage)<<"\n";
        statusText_=L"Persona adapter metadata exported";
    }

    void CreatePersonaAdapter() {
        std::string persona=simSettings_.persona.name.empty()?"Default Persona":simSettings_.persona.name;
        std::string foundationId;
        if(foundationRegistry_.ActiveIndex()>=0 && foundationRegistry_.ActiveIndex()<(int)foundationRegistry_.Models().size())
            foundationId=foundationRegistry_.Models()[(size_t)foundationRegistry_.ActiveIndex()].id;
        else if(!foundationRegistry_.Models().empty()) foundationId=foundationRegistry_.Models()[0].id;

        int count=0;
        for(const auto& a:personaAdapterRegistry_.Adapters()) if(a.personaName==persona) ++count;
        auto& adapter=personaAdapterRegistry_.Add(persona,persona+".lora","v"+std::to_string(count+1),foundationId);
        personaAdapterRegistry_.Save(runtime_->root/"persona-adapters.tsv");
        assignedPersonaLoRA_=Widen(adapter.adapterName+" "+adapter.version);
        selectedPersonaAdapter_=(int)personaAdapterRegistry_.Adapters().size()-1;
        statusText_=L"Persona LoRA created in staging";
    }

    void ActivatePersonaAdapter(size_t index) {
        if(deploymentRegistry_.HasActiveLockedDeployment()) {
            statusText_=L"Runtime is version-locked. Use Deployment to change the active stack.";
            return;
        }
        if(index>=personaAdapterRegistry_.Adapters().size()) return;
        const auto persona=personaAdapterRegistry_.Adapters()[index].personaName;
        if(persona!=simSettings_.persona.name) {
            statusText_=L"Adapter belongs to a different persona";
            return;
        }
        personaAdapterRegistry_.Activate(index);
        personaAdapterRegistry_.Save(runtime_->root/"persona-adapters.tsv");
        ResolvePersonaAdapter();
        statusText_=L"Persona LoRA activated";
    }

    void RollbackPersonaAdapter() {
        if(deploymentRegistry_.HasActiveLockedDeployment()) {
            statusText_=L"Runtime is version-locked. Use Deployment rollback.";
            return;
        }
        if(personaAdapterRegistry_.Rollback(simSettings_.persona.name)) {
            personaAdapterRegistry_.Save(runtime_->root/"persona-adapters.tsv");
            ResolvePersonaAdapter();
            statusText_=L"Persona LoRA rollback completed";
        } else statusText_=L"No archived LoRA is available for rollback";
    }

    void SelectDatasetSnapshot(int index) {
        if(index<0 || index>=(int)trainingData_.Snapshots().size()) return;
        selectedDatasetSnapshot_=index;
        selectedTrainingExample_=-1;
        ApplyPageControls();
        statusText_=L"Dataset snapshot selected";
    }

    void ImportDatasetSnapshot() {
        wchar_t file[MAX_PATH]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hwnd_; ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
        ofn.lpstrFilter=L"SARA Dataset Snapshot\0*.sara-dataset\0All Files\0*.*\0\0";
        ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if(!GetOpenFileNameW(&ofn)) return;
        try {
            auto& snap=trainingData_.ImportSnapshot(std::filesystem::path(file));
            trainingData_.Save(runtime_->root/"training-data.tsv");
            selectedDatasetSnapshot_=(int)trainingData_.Snapshots().size()-1;
            selectedTrainingExample_=-1;
            trainingCaptured_=(int)trainingData_.Examples().size();
            trainingReviewPending_=(int)trainingData_.Count(sentinel::simulation::TrainingExampleState::Review);
            trainingApproved_=(int)trainingData_.Count(sentinel::simulation::TrainingExampleState::Approved);
            statusText_=L"Imported dataset snapshot "+Widen(snap.name);
        } catch(const std::exception& e) {
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Dataset Import Failed",MB_OK|MB_ICONERROR);
            statusText_=L"Dataset import failed";
        }
    }

    void ExportSelectedDatasetSnapshot() {
        if(trainingData_.Snapshots().empty()) { statusText_=L"No dataset snapshot is available to export"; return; }
        if(selectedDatasetSnapshot_<0 || selectedDatasetSnapshot_>=(int)trainingData_.Snapshots().size())
            selectedDatasetSnapshot_=(int)trainingData_.Snapshots().size()-1;
        const auto& snap=trainingData_.Snapshots()[(size_t)selectedDatasetSnapshot_];
        wchar_t file[MAX_PATH]{};
        auto defaultName=Widen(snap.name+".sara-dataset");
        wcsncpy_s(file,defaultName.c_str(),_TRUNCATE);
        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hwnd_; ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
        ofn.lpstrFilter=L"SARA Dataset Snapshot\0*.sara-dataset\0All Files\0*.*\0\0";
        ofn.lpstrDefExt=L"sara-dataset"; ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
        if(!GetSaveFileNameW(&ofn)) return;
        try {
            trainingData_.ExportSnapshot((size_t)selectedDatasetSnapshot_,std::filesystem::path(file));
            statusText_=L"Dataset snapshot exported";
        } catch(const std::exception& e) {
            MessageBoxW(hwnd_,Widen(e.what()).c_str(),L"Dataset Export Failed",MB_OK|MB_ICONERROR);
            statusText_=L"Dataset export failed";
        }
    }

    void SelectTrainingExample(int index) {
        if(index<0 || index>=(int)trainingData_.Examples().size()) return;
        selectedTrainingExample_=index;
        selectedDatasetSnapshot_=-1;
        SetWindowTextW(trainingReviewTargetEdit_,Widen(trainingData_.Examples()[(size_t)index].targetResponse).c_str());
        ApplyPageControls();
        statusText_=L"Training example selected for review";
    }

    bool SaveSelectedTrainingTarget() {
        if(selectedTrainingExample_<0 || selectedTrainingExample_>=(int)trainingData_.Examples().size()) return false;
        auto target=Narrow(EditText(trainingReviewTargetEdit_));
        if(target.empty()) {
            statusText_=L"Target response cannot be empty";
            return false;
        }
        auto& e=trainingData_.Examples()[(size_t)selectedTrainingExample_];
        e.targetResponse=target;
        e.correction=target;
        e.reviewer="local-operator";
        trainingData_.Save(runtime_->root/"training-data.tsv");
        statusText_=L"Training target updated";
        return true;
    }

    void ReviewSelectedTrainingExample(bool approve) {
        if(selectedTrainingExample_<0 || selectedTrainingExample_>=(int)trainingData_.Examples().size()) {
            statusText_=L"Select a training example first";
            return;
        }
        if(!SaveSelectedTrainingTarget()) return;
        auto& e=trainingData_.Examples()[(size_t)selectedTrainingExample_];
        e.reviewer="local-operator";
        trainingData_.SetState((size_t)selectedTrainingExample_,approve?
            sentinel::simulation::TrainingExampleState::Approved:
            sentinel::simulation::TrainingExampleState::Rejected);
        trainingData_.Save(runtime_->root/"training-data.tsv");
        trainingReviewPending_=(int)trainingData_.Count(sentinel::simulation::TrainingExampleState::Review);
        trainingApproved_=(int)trainingData_.Count(sentinel::simulation::TrainingExampleState::Approved);
        statusText_=approve?L"Training example approved":L"Training example rejected";
    }

    void CreateDatasetSnapshot() {
        if(trainingData_.Count(sentinel::simulation::TrainingExampleState::Approved)==0) {
            statusText_=L"Approve at least one training example before creating a dataset snapshot";
            return;
        }
        auto& snap=trainingData_.CreateSnapshot("SARA Dataset "+std::to_string(trainingData_.Snapshots().size()+1));
        trainingData_.Save(runtime_->root/"training-data.tsv");
        selectedDatasetSnapshot_=(int)trainingData_.Snapshots().size()-1;
        selectedTrainingExample_=-1;
        statusText_=L"Created dataset snapshot "+Widen(snap.name);
    }

    void SelectTrainingJob(int index) {
        if(index<0 || index>=(int)trainingJobRegistry_.Jobs().size()) return;
        selectedTrainingJob_=index;
        statusText_=L"Training job selected";
    }

    void CreateTrainingJob() {
        std::string foundation="SARA Foundation";
        if(foundationRegistry_.ActiveIndex()>=0 && foundationRegistry_.ActiveIndex()<(int)foundationRegistry_.Models().size()) {
            const auto& f=foundationRegistry_.Models()[(size_t)foundationRegistry_.ActiveIndex()];
            foundation=f.name+" "+f.version;
        } else if(!foundationRegistry_.Models().empty()) {
            const auto& f=foundationRegistry_.Models()[0];
            foundation=f.name+" "+f.version;
        }
        std::string dataset;
        if(!trainingData_.Snapshots().empty()) dataset=trainingData_.Snapshots().back().id;
        else dataset="approved-captures-"+std::to_string(trainingApproved_);
        trainingJobRegistry_.Create(foundation,dataset);
        trainingJobRegistry_.Save(runtime_->root/"training-jobs.tsv");
        selectedTrainingJob_=(int)trainingJobRegistry_.Jobs().size()-1;
        statusText_=L"Training job queued";
    }

    void StartTrainingJob(size_t index) {
        trainingJobRegistry_.SetState(index,"RUNNING",5);
        trainingJobRegistry_.Save(runtime_->root/"training-jobs.tsv");
        statusText_=L"Training job marked running";
    }

    void CompleteTrainingJob(size_t index) {
        trainingJobRegistry_.SetState(index,"COMPLETED",100);
        trainingJobRegistry_.Save(runtime_->root/"training-jobs.tsv");
        statusText_=L"Training job marked completed";
    }

    void CreateFoundationFork() {
        if(foundationRegistry_.Models().empty()) return;
        selectedFoundation_=std::clamp(selectedFoundation_,0,(int)foundationRegistry_.Models().size()-1);
        const int next=(int)foundationRegistry_.Models().size();
        const std::string version="1."+std::to_string(std::max(0,next));
        const std::string name="SARA Foundation";
        auto& created=foundationRegistry_.CreateFork((size_t)selectedFoundation_,name,version);
        selectedFoundation_=(int)foundationRegistry_.Models().size()-1;
        foundationRegistry_.Save(runtime_->root/"foundation-registry.tsv");
        statusText_=L"Created "+Widen(created.name+" "+created.version);
    }

    void ApproveSelectedFoundation() {
        if(selectedFoundation_<0 || selectedFoundation_>=(int)foundationRegistry_.Models().size()) return;
        foundationRegistry_.Approve((size_t)selectedFoundation_);
        foundationRegistry_.Save(runtime_->root/"foundation-registry.tsv");
        statusText_=L"Foundation fork approved";
    }

    void ActivateSelectedFoundation() {
        if(deploymentRegistry_.HasActiveLockedDeployment()) {
            statusText_=L"Runtime is version-locked. Use Deployment to change the foundation.";
            return;
        }
        if(selectedFoundation_<0 || selectedFoundation_>=(int)foundationRegistry_.Models().size()) return;
        foundationRegistry_.Activate((size_t)selectedFoundation_);
        foundationRegistry_.Save(runtime_->root/"foundation-registry.tsv");
        if(foundationRegistry_.ActiveIndex()==selectedFoundation_) {
            ResolvePersonaAdapter();
            statusText_=L"Foundation fork activated";
        }
        else statusText_=L"Approve a non-base foundation fork before activation";
    }

    void RollbackFoundation() {
        if(deploymentRegistry_.HasActiveLockedDeployment()) {
            statusText_=L"Runtime is version-locked. Use Deployment rollback.";
            return;
        }
        if(foundationRegistry_.Rollback()) {
            selectedFoundation_=foundationRegistry_.ActiveIndex();
            foundationRegistry_.Save(runtime_->root/"foundation-registry.tsv");
            ResolvePersonaAdapter();
            statusText_=L"Foundation rollback completed";
        } else statusText_=L"No prior foundation version is available for rollback";
    }

    static std::vector<std::string> SplitRuleResponses(const std::string& text) {
        std::vector<std::string> out;
        size_t start=0;
        while(start<=text.size()) {
            auto pos=text.find("||",start);
            auto item=text.substr(start,pos==std::string::npos?std::string::npos:pos-start);
            while(!item.empty() && (item.front()==' ' || item.front()=='\r' || item.front()=='\n')) item.erase(item.begin());
            while(!item.empty() && (item.back()==' ' || item.back()=='\r' || item.back()=='\n')) item.pop_back();
            if(!item.empty()) out.push_back(item);
            if(pos==std::string::npos) break;
            start=pos+2;
        }
        return out;
    }

    void LoadTriggerRuleEditor(int index) {
        if(index<0 || index>=(int)triggerRules_.Rules().size()) return;
        const auto& rule=triggerRules_.Rules()[(size_t)index];
        SetWindowTextW(ruleNameEdit_,Widen(rule.name).c_str());
        SetWindowTextW(rulePatternEdit_,Widen(rule.pattern).c_str());
        std::string joined;
        for(size_t i=0;i<rule.responses.size();++i) { if(i) joined+=" || "; joined+=rule.responses[i]; }
        SetWindowTextW(ruleResponsesEdit_,Widen(joined).c_str());
        SetWindowTextW(rulePriorityEdit_,std::to_wstring(rule.priority).c_str());
        ruleEditorTerminal_=rule.terminal;
    }

    void OpenRuleEditor() {
        ruleEditorOpen_=true;
        if(selectedTriggerRule_>=0) LoadTriggerRuleEditor(selectedTriggerRule_);
        else NewTriggerRuleDraft();
        ApplyPageControls();
        statusText_=L"Trigger rule manager opened";
    }

    void CloseRuleEditor() {
        ruleEditorOpen_=false;
        ApplyPageControls();
        statusText_=L"Trigger rule manager closed";
    }

    void NewTriggerRuleDraft() {
        selectedTriggerRule_=-1;
        SetWindowTextW(ruleNameEdit_,L"");
        SetWindowTextW(rulePatternEdit_,L"");
        SetWindowTextW(ruleResponsesEdit_,L"");
        SetWindowTextW(rulePriorityEdit_,L"100");
        ruleEditorTerminal_=true;
        ruleEditorOpen_=true;
        ApplyPageControls();
        if(ruleNameEdit_) SetFocus(ruleNameEdit_);
    }

    void SelectTriggerRule(int index) {
        if(index<0 || index>=(int)triggerRules_.Rules().size()) return;
        selectedTriggerRule_=index;
        ruleEditorOpen_=true;
        LoadTriggerRuleEditor(index);
        ApplyPageControls();
        statusText_=L"Trigger rule selected";
    }

    void SaveTriggerRuleDraft() {
        auto name=Narrow(EditText(ruleNameEdit_));
        auto pattern=Narrow(EditText(rulePatternEdit_));
        auto responses=SplitRuleResponses(Narrow(EditText(ruleResponsesEdit_)));
        int priority=100;
        try { priority=std::stoi(Narrow(EditText(rulePriorityEdit_))); } catch(...) {}
        priority=std::clamp(priority,0,9999);
        if(name.empty() || pattern.empty() || responses.empty()) {
            statusText_=L"Rule name, pattern, and at least one response are required";
            return;
        }
        if(selectedTriggerRule_>=0 && selectedTriggerRule_<(int)triggerRules_.Rules().size()) {
            auto& rule=triggerRules_.Rules()[(size_t)selectedTriggerRule_];
            rule.name=name; rule.pattern=pattern; rule.responses=responses; rule.priority=priority; rule.terminal=ruleEditorTerminal_;
        } else {
            triggerRules_.Add(name,pattern,responses,priority,ruleEditorTerminal_);
            selectedTriggerRule_=(int)triggerRules_.Rules().size()-1;
        }
        triggerRules_.Save(runtime_->root/"trigger-rules.tsv");
        statusText_=L"Trigger rule saved and active";
    }

    void DeleteSelectedTriggerRule() {
        if(selectedTriggerRule_<0 || selectedTriggerRule_>=(int)triggerRules_.Rules().size()) {
            statusText_=L"Select a trigger rule first";
            return;
        }
        triggerRules_.Remove((size_t)selectedTriggerRule_);
        triggerRules_.Save(runtime_->root/"trigger-rules.tsv");
        selectedTriggerRule_=-1;
        NewTriggerRuleDraft();
        statusText_=L"Trigger rule deleted";
    }

    std::wstring TrainingModeName() const {
        switch(trainingMode_) {
            case TrainingMode::BehaviorTuning: return L"Behavior Tuning";
            case TrainingMode::DatasetTraining: return L"Dataset Training";
            case TrainingMode::PersonaLoRA: return L"Persona LoRA Training";
            case TrainingMode::FoundationFork: return L"Foundation Fork Training";
            case TrainingMode::EvaluationTest: return L"Evaluation / Test";
        }
        return L"Behavior Tuning";
    }

    void CaptureLatestTrainingExample() {
        if(simContext_.history.size()<2) {
            statusText_=L"A user/model turn pair is required before capture";
            return;
        }

        std::string input,original;
        for(auto it=simContext_.history.rbegin(); it!=simContext_.history.rend(); ++it) {
            if(original.empty() && it->speaker==sentinel::simulation::ChatTurn::Speaker::SyntheticSubject) {
                original=it->text;
                continue;
            }
            if(!original.empty() && it->speaker==sentinel::simulation::ChatTurn::Speaker::Investigator) {
                input=it->text;
                break;
            }
        }
        if(input.empty() || original.empty()) {
            statusText_=L"No complete conversational pair is available to capture";
            return;
        }

        std::string foundationId;
        int fi=foundationRegistry_.ActiveIndex();
        if(fi<0 && !foundationRegistry_.Models().empty()) fi=0;
        if(fi>=0 && fi<(int)foundationRegistry_.Models().size()) foundationId=foundationRegistry_.Models()[(size_t)fi].id;

        std::string adapterId;
        int ai=personaAdapterRegistry_.ResolveActiveIndex(simSettings_.persona.name);
        if(ai>=0 && ai<(int)personaAdapterRegistry_.Adapters().size()) adapterId=personaAdapterRegistry_.Adapters()[(size_t)ai].id;

        auto correction=Narrow(EditText(trainingCorrectionEdit_));
        if(correction.empty()) correction="Accept response as-is";
        const std::string target=(correction=="Accept response as-is")?original:correction;
        auto category=ComboText(trainingCategoryCombo_);
        if(category.empty()) category="Behavior";

        trainingData_.Capture(
            simSettings_.persona.name,foundationId,adapterId,currentConversationId_,
            input,original,correction,target,category);
        trainingData_.Save(runtime_->root/"training-data.tsv");
        SetWindowTextW(trainingCorrectionEdit_,L"");

        trainingCaptured_=(int)trainingData_.Examples().size();
        trainingReviewPending_=(int)trainingData_.Count(sentinel::simulation::TrainingExampleState::Review);
        trainingApproved_=(int)trainingData_.Count(sentinel::simulation::TrainingExampleState::Approved);
        statusText_=L"Training example captured for review";
    }

    void ApproveTrainingCapture() {
        for(size_t i=trainingData_.Examples().size(); i>0; --i) {
            if(trainingData_.Examples()[i-1].state==sentinel::simulation::TrainingExampleState::Review) {
                trainingData_.SetState(i-1,sentinel::simulation::TrainingExampleState::Approved);
                trainingData_.Save(runtime_->root/"training-data.tsv");
                trainingReviewPending_=(int)trainingData_.Count(sentinel::simulation::TrainingExampleState::Review);
                trainingApproved_=(int)trainingData_.Count(sentinel::simulation::TrainingExampleState::Approved);
                statusText_=L"Training example approved for dataset promotion";
                return;
            }
        }
        statusText_=L"No captured training example is awaiting review";
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
            for(const auto& rule:triggerRules_.Rules()) {
                if(!rule.enabled) continue;
                ++triggerTotal;
                auto match=triggerRules_.Match(rule.pattern,simContext_.personaSummary,1);
                const bool responseOk=rule.responses.empty() || (match && !match->response.empty());
                if(match && match->ruleId==rule.id && match->terminal==rule.terminal && responseOk) ++triggerPassed;
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

            auto& run=evaluationRuns_.Create(
                item.id,item.modelName,
                simContext_.foundationId,simContext_.foundationName,
                simContext_.adapterId,simContext_.adapterName,
                std::move(dimensions),std::move(caseResults));

            selectedEvaluationRun_=(int)evaluationRuns_.Runs().size()-1;
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
            statusText_=L"Select two evaluation runs to compare";
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

    void ApproveSelectedRegistryModel() {
        if(selectedRegistryModel_<0 || selectedRegistryModel_>=(int)modelRegistry_.Models().size()) return;
        modelRegistry_.Approve((size_t)selectedRegistryModel_);
        modelRegistry_.Save(runtime_->root/"model-registry.tsv");
        statusText_=L"Candidate model approved";
    }

    void ActivateSelectedRegistryModel() {
        if(deploymentRegistry_.HasActiveLockedDeployment()) {
            statusText_=L"Runtime is version-locked. Use Deployment to activate another package.";
            return;
        }
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
        if(deploymentRegistry_.HasActiveLockedDeployment()) {
            statusText_=L"Runtime is version-locked. Use Deployment rollback.";
            return;
        }
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

    static std::vector<std::byte> DeploymentAuditMetadata(const std::string& text) {
        const auto* begin=reinterpret_cast<const std::byte*>(text.data());
        return std::vector<std::byte>(begin,begin+text.size());
    }

    int FindModelById(std::string_view id) const {
        for(size_t i=0;i<modelRegistry_.Models().size();++i)
            if(modelRegistry_.Models()[i].id==id) return (int)i;
        return -1;
    }

    int FindFoundationById(std::string_view id) const {
        for(size_t i=0;i<foundationRegistry_.Models().size();++i)
            if(foundationRegistry_.Models()[i].id==id) return (int)i;
        return -1;
    }

    int FindAdapterById(std::string_view id) const {
        for(size_t i=0;i<personaAdapterRegistry_.Adapters().size();++i)
            if(personaAdapterRegistry_.Adapters()[i].id==id) return (int)i;
        return -1;
    }

    bool ApplyDeploymentPackage(size_t index) {
        if(index>=deploymentRegistry_.Packages().size()) return false;
        const auto& package=deploymentRegistry_.Packages()[index];

        const int mi=FindModelById(package.candidateId);
        if(mi<0) {
            statusText_=L"Deployment model is no longer registered";
            return false;
        }
        auto& modelItem=modelRegistry_.Models()[(size_t)mi];
        if(modelItem.stage!=sentinel::simulation::ModelStage::Approved &&
           modelItem.stage!=sentinel::simulation::ModelStage::Active) {
            statusText_=L"Deployment model is not approved";
            return false;
        }

        modelRegistry_.Activate((size_t)mi);
        if(modelRegistry_.ActiveIndex()!=mi) return false;
        model_=sentinel::simulation::CreateOpenAICompatibleModel(modelItem.endpoint,modelItem.modelName);
        selectedRegistryModel_=mi;
        SetWindowTextW(modelEndpointEdit_,Widen(modelItem.endpoint).c_str());
        SetWindowTextW(modelNameEdit_,Widen(modelItem.modelName).c_str());
        simSettings_.endpoint=modelItem.endpoint;
        simSettings_.model=modelItem.modelName;
        modelStatus_=L"Deployment: "+Widen(package.id)+L" • "+Widen(modelItem.modelName);

        const int fi=FindFoundationById(package.foundationId);
        if(fi>=0) {
            selectedFoundation_=fi;
            const auto& foundation=foundationRegistry_.Models()[(size_t)fi];
            if(!foundation.immutableBase)
                foundationRegistry_.Activate((size_t)fi);
            simContext_.foundationId=foundation.id;
            simContext_.foundationName=foundation.name+" "+foundation.version;
        } else {
            simContext_.foundationId=package.foundationId;
            simContext_.foundationName=package.foundationName;
        }

        const int ai=FindAdapterById(package.adapterId);
        if(ai>=0) {
            personaAdapterRegistry_.Activate((size_t)ai);
            selectedPersonaAdapter_=ai;
            const auto& adapter=personaAdapterRegistry_.Adapters()[(size_t)ai];
            simContext_.adapterId=adapter.id;
            simContext_.adapterName=adapter.adapterName+" "+adapter.version;
            assignedPersonaLoRA_=Widen(simContext_.adapterName);
        } else {
            simContext_.adapterId=package.adapterId;
            simContext_.adapterName=package.adapterName;
            assignedPersonaLoRA_=package.adapterName.empty()?L"No active LoRA":Widen(package.adapterName);
        }
        simContext_.trainingMode=Narrow(TrainingModeName());

        modelRegistry_.Save(runtime_->root/"model-registry.tsv");
        foundationRegistry_.Save(runtime_->root/"foundation-registry.tsv");
        personaAdapterRegistry_.Save(runtime_->root/"persona-adapters.tsv");
        sentinel::simulation::SaveSimulationSettings(runtime_->root/"simulation.ini",simSettings_);
        return true;
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

        auto& package=deploymentRegistry_.Prepare(
            modelItem.id,modelItem.modelName,
            eval.foundationId,eval.foundationName,
            eval.adapterId,eval.adapterName,
            simSettings_.persona.name,
            eval.id,eval.overallScore);
        selectedDeployment_=(int)deploymentRegistry_.Packages().size()-1;
        deploymentRegistry_.Save(runtime_->root/"deployment-registry.tsv");

        const std::string meta="prepared "+package.id+" model="+package.candidateId+
            " foundation="+package.foundationId+" adapter="+package.adapterId+
            " eval="+package.evaluationRunId;
        runtime_->audit.Append({sentinel::UserId::Random(),sentinel::AuditAction::DeploymentPrepared,
            "deployment",package.id,DeploymentAuditMetadata(meta)});
        statusText_=L"Deployment package prepared: "+Widen(package.id);
    }

    void ActivateSelectedDeployment() {
        if(selectedDeployment_<0 || selectedDeployment_>=(int)deploymentRegistry_.Packages().size()) {
            statusText_=L"Select a deployment package first";
            return;
        }
        if(!ApplyDeploymentPackage((size_t)selectedDeployment_)) return;
        deploymentRegistry_.Activate((size_t)selectedDeployment_);
        deploymentRegistry_.Save(runtime_->root/"deployment-registry.tsv");
        const auto& package=deploymentRegistry_.Packages()[(size_t)selectedDeployment_];

        const std::string meta="activated "+package.id+" score="+std::to_string(package.evaluationScore);
        runtime_->audit.Append({sentinel::UserId::Random(),sentinel::AuditAction::DeploymentActivated,
            "deployment",package.id,DeploymentAuditMetadata(meta)});
        statusText_=L"Deployment activated and runtime stack pinned";
    }

    void RollbackDeployment() {
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
        runtime_->audit.Append({sentinel::UserId::Random(),sentinel::AuditAction::DeploymentRolledBack,
            "deployment",targetId,DeploymentAuditMetadata("rollback to "+targetId)});
        statusText_=L"Deployment rollback completed";
    }

    void ToggleDeploymentLock() {
        if(selectedDeployment_<0 || selectedDeployment_>=(int)deploymentRegistry_.Packages().size()) return;
        const bool next=!deploymentRegistry_.Packages()[(size_t)selectedDeployment_].versionLocked;
        deploymentRegistry_.SetLocked((size_t)selectedDeployment_,next);
        deploymentRegistry_.Save(runtime_->root/"deployment-registry.tsv");
        const auto& package=deploymentRegistry_.Packages()[(size_t)selectedDeployment_];
        runtime_->audit.Append({sentinel::UserId::Random(),sentinel::AuditAction::DeploymentLockChanged,
            "deployment",package.id,DeploymentAuditMetadata(std::string("version_locked=")+(next?"true":"false"))});
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
        ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=hwnd_; ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
        ofn.lpstrFilter=L"SARA Deployment Manifest\0*.json\0All Files\0*.*\0\0";
        ofn.lpstrDefExt=L"json"; ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
        if(!GetSaveFileNameW(&ofn)) return;
        std::ofstream out(std::filesystem::path(file),std::ios::trunc);
        out<<deploymentRegistry_.BuildManifest((size_t)selectedDeployment_);
        runtime_->audit.Append({sentinel::UserId::Random(),sentinel::AuditAction::DeploymentManifestExported,
            "deployment",package.id,DeploymentAuditMetadata("manifest exported")});
        statusText_=L"Deployment manifest exported";
    }

    void DrawModelLabTabs(float x,float y,float contentW) {
        const wchar_t* tabs[]={L"Overview",L"Train",L"Datasets",L"Personas & LoRAs",L"Foundation Forks",L"Jobs",L"Evaluation",L"Deployment"};
        const float tabGap=6.0f;
        const float tabW=(contentW-tabGap*7.0f)/8.0f;
        const int active=(int)modelLabSection_;
        for(int i=0;i<8;i++) {
            const float tx=x+i*(tabW+tabGap);
            const bool selected=i==active;
            Rounded(tx,y,tabW,34,selected?brush_.panel2.Get():brush_.sidebar.Get(),selected?brush_.cyan.Get():brush_.border.Get(),7);
            TextLine(tabs[i],tx+6,y+1,tabW-12,32,tinyFmt_.Get(),selected?brush_.cyan.Get():brush_.text.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
            buttons_.push_back({{tx,y,tx+tabW,y+34},L"mltab:"+std::to_wstring(i)});
        }
    }

    void DrawModelLabContext(float x,float y,float contentW) {
        const float gap=12.0f;
        const float contextW=(contentW-gap*3.0f)/4.0f;
        const std::wstring modelName=foundationRegistry_.ActiveIndex()>=0 && foundationRegistry_.ActiveIndex()<(int)foundationRegistry_.Models().size()
            ? Widen(foundationRegistry_.Models()[(size_t)foundationRegistry_.ActiveIndex()].name+" "+foundationRegistry_.Models()[(size_t)foundationRegistry_.ActiveIndex()].version)
            : (!foundationRegistry_.Models().empty()?Widen(foundationRegistry_.Models()[0].name+" "+foundationRegistry_.Models()[0].version):
               (modelStatus_.find(L"Built-in")!=std::wstring::npos?L"Built-in test model":modelStatus_));
        const std::wstring contextValues[]={
            modelName,
            Widen(simSettings_.persona.name.empty()?std::string("Default Persona"):simSettings_.persona.name),
            assignedPersonaLoRA_,
            TrainingModeName()
        };
        const wchar_t* contextLabels[]={L"FOUNDATION MODEL",L"PERSONA",L"ASSIGNED LORA",L"TRAINING MODE"};
        for(int i=0;i<4;i++) {
            const float cx=x+i*(contextW+gap);
            Rounded(cx,y,contextW,60,brush_.panel.Get(),brush_.border.Get(),9);
            TextLine(contextLabels[i],cx+14,y+6,contextW-28,17,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(contextValues[i],cx+14,y+24,contextW-42,27,smallFmt_.Get(),i==0?brush_.cyan.Get():brush_.text.Get());
            if(i==3) {
                TextLine(L"▼",cx+contextW-28,y+25,18,24,tinyFmt_.Get(),brush_.cyan.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
                buttons_.push_back({{cx,y,cx+contextW,y+60},L"ml_training_mode"});
            }
        }
    }

    void DrawModelLabOverview(float x,float y,float contentW) {
        const float gap=12.0f;
        const float rightW=std::max(330.0f,contentW*0.34f);
        const float leftW=contentW-rightW-gap;

        Rounded(x,y,leftW,286,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Model Registry",x+18,y+10,240,30,h1Fmt_.Get(),brush_.text.Get());
        AddButton(L"ml_diagnostics_export",L"Export Diagnostics",x+leftW-492,y+14,128,32,false);
        TextLine(L"Candidate models, evaluation state, and activation",x+18,y+38,leftW-36,20,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"model_register",L"Register",x+leftW-354,y+14,82,32,false);
        AddButton(L"model_eval",L"Evaluate",x+leftW-262,y+14,82,32,false);
        AddButton(L"model_activate",L"Activate",x+leftW-170,y+14,76,32,true);
        AddButton(L"model_rollback",L"Rollback",x+leftW-84,y+14,70,32,false);

        TextLine(L"MODEL",x+28,y+70,210,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STATE",x+250,y+70,82,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"SCORE",x+344,y+70,62,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"LATENCY",x+418,y+70,76,18,tinyFmt_.Get(),brush_.muted.Get());

        float yy=y+94;
        if(modelRegistry_.Models().empty()) {
            Rounded(x+18,yy,leftW-36,58,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"No registered candidates yet",x+32,yy+6,leftW-64,22,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"Connect a model in Simulation Lab, then register it here.",x+32,yy+28,leftW-64,20,tinyFmt_.Get(),brush_.muted.Get());
        } else {
            for(size_t i=0;i<modelRegistry_.Models().size() && i<3;i++) {
                const auto& m=modelRegistry_.Models()[i];
                const bool selected=(int)i==selectedRegistryModel_;
                Rounded(x+18,yy,leftW-36,50,selected?brush_.panel2.Get():brush_.sidebar.Get(),selected?brush_.cyan.Get():brush_.border.Get(),8);
                TextLine(Widen(m.modelName),x+28,yy+3,210,22,smallFmt_.Get(),brush_.text.Get());
                TextLine(Widen(sentinel::simulation::ToString(m.stage)),x+250,yy+3,82,22,tinyFmt_.Get(),
                    m.stage==sentinel::simulation::ModelStage::Active?brush_.green.Get():brush_.cyan.Get());
                TextLine(std::to_wstring(m.evaluationScore),x+344,yy+3,62,22,tinyFmt_.Get(),brush_.text.Get());
                TextLine(std::to_wstring(m.latencyMs)+L" ms",x+418,yy+3,76,22,tinyFmt_.Get(),brush_.muted.Get());
                TextLine(Widen(m.endpoint),x+28,yy+26,leftW-56,18,tinyFmt_.Get(),brush_.muted.Get());
                buttons_.push_back({{x+18,yy,x+leftW-18,yy+50},L"regmodel:"+std::to_wstring(i)});
                yy+=58;
            }
        }

        const float rx=x+leftW+gap;
        Rounded(rx,y,rightW,132,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Training Pipeline",rx+16,y+10,rightW-32,28,h1Fmt_.Get(),brush_.text.Get());
        const wchar_t* stages[]={L"Capture",L"Review",L"Dataset",L"Train",L"Evaluate",L"Deploy"};
        const float stageGap=(rightW-52)/5.0f;
        for(int i=0;i<6;i++) {
            const float sx=rx+26+i*stageGap;
            target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(sx,y+66),12,12),i==0?brush_.cyan.Get():brush_.border.Get(),2);
            if(i==0) target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx,y+66),6,6),brush_.cyan.Get());
            if(i<5) target_->DrawLine(D2D1::Point2F(sx+13,y+66),D2D1::Point2F(sx+stageGap-13,y+66),brush_.border.Get(),1);
            TextLine(stages[i],sx-28,y+85,56,20,tinyFmt_.Get(),i==0?brush_.cyan.Get():brush_.muted.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        Rounded(rx,y+144,rightW,142,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Evaluation & Deployment",rx+16,y+154,rightW-32,28,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Latest score",rx+18,y+194,92,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(std::to_wstring(lastEvaluation_.score),rx+18,y+212,92,34,bigFmt_.Get(),
            lastEvaluation_.score>=80?brush_.green.Get():lastEvaluation_.score>=50?brush_.yellow.Get():brush_.red.Get());
        TextLine(modelRegistry_.ActiveIndex()>=0?L"Production model active":L"No production deployment",
            rx+126,y+210,rightW-144,26,smallFmt_.Get(),modelRegistry_.ActiveIndex()>=0?brush_.green.Get():brush_.muted.Get());

        const float by=y+300;
        const float metricW=(contentW-gap*3.0f)/4.0f;
        const std::wstring values[]={
            std::to_wstring(trainingCaptured_),
            std::to_wstring(trainingReviewPending_),
            std::to_wstring(trainingApproved_),
            std::to_wstring(lastEvaluation_.score)
        };
        const wchar_t* labels[]={L"CAPTURED",L"NEEDS REVIEW",L"APPROVED",L"LATEST EVAL"};
        const wchar_t* subs[]={L"training examples",L"awaiting approval",L"dataset-ready",L"candidate score"};
        for(int i=0;i<4;i++) {
            const float mx=x+i*(metricW+gap);
            Rounded(mx,by,metricW,82,brush_.panel.Get(),brush_.border.Get(),9);
            TextLine(labels[i],mx+14,by+8,metricW-28,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(values[i],mx+14,by+27,metricW-28,28,bodyFmt_.Get(),brush_.text.Get());
            TextLine(subs[i],mx+14,by+56,metricW-28,17,tinyFmt_.Get(),i==1?brush_.yellow.Get():brush_.cyan.Get());
        }
    }

    void DrawModelLabTrain(float x,float y,float contentW) {
        const float gap=12.0f;
        const float rightW=340.0f;
        const float leftW=contentW-rightW-gap;
        const float panelH=382.0f;

        Rounded(x,y,leftW,panelH,brush_.panel.Get(),brush_.border.Get(),10);
        if(ruleEditorOpen_) {
            TextLine(L"Trigger Rule Manager",x+18,y+10,300,30,h1Fmt_.Get(),brush_.text.Get());
            TextLine(L"Rules execute before normal generation. Terminal rules stop generic fall-through.",x+18,y+38,leftW-36,20,tinyFmt_.Get(),brush_.muted.Get());
            AddButton(L"ml_rule_new",L"New Rule",x+leftW-274,y+14,78,28,false);
            AddButton(L"ml_rule_cancel",L"Back to Trainer",x+leftW-186,y+14,168,28,false);

            TextLine(L"Name",x+20,y+74,108,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Match pattern",x+20,y+116,108,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Responses",x+20,y+158,108,30,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Use || between alternate responses",x+20,y+184,108,58,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Priority",x+20,y+264,108,30,tinyFmt_.Get(),brush_.muted.Get());
            AddButton(L"ml_rule_terminal",ruleEditorTerminal_?L"Terminal: YES":L"Terminal: NO",x+270,y+264,116,30,false);
            AddButton(L"ml_rule_save",L"Save Rule",x+20,y+318,110,34,true);
            AddButton(L"ml_rule_delete",L"Delete",x+142,y+318,90,34,false);

            float listY=y+74;
            const float listX=x+leftW+12;
        } else {
        TextLine(L"Conversational Trainer",x+18,y+10,300,30,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Talk to SARA, correct behavior, and capture reviewed training examples.",x+18,y+38,leftW-36,20,tinyFmt_.Get(),brush_.muted.Get());
        Badge(L"LIVE",x+leftW-76,y+14,brush_.green.Get(),58);

        const float transcriptTop=y+66;
        const float transcriptBottom=y+228;
        const int total=(int)simContext_.history.size();
        const int visible=std::min(3,total);
        float yy=transcriptBottom-visible*68.0f;
        for(int i=std::max(0,total-visible); i<total; ++i) {
            const auto& turn=simContext_.history[(size_t)i];
            const bool operatorTurn=turn.speaker==sentinel::simulation::ChatTurn::Speaker::Investigator;
            const float bubbleW=std::min(leftW-100.0f,560.0f);
            const float bx=operatorTurn?x+leftW-bubbleW-20:x+20;
            Rounded(bx,yy,bubbleW,58,operatorTurn?brush_.panel2.Get():brush_.sidebar.Get(),
                operatorTurn?brush_.blue.Get():brush_.border.Get(),9);
            TextLine(operatorTurn?L"You":L"SARA",bx+12,yy+3,bubbleW-24,18,tinyFmt_.Get(),operatorTurn?brush_.cyan.Get():brush_.green.Get());
            Text(Widen(turn.text),bx+12,yy+21,bubbleW-24,31,tinyFmt_.Get(),brush_.text.Get());
            yy+=68;
        }
        if(total==0) {
            TextLine(L"Start a training conversation below. Corrections stay non-production until reviewed.",
                x+28,transcriptTop+54,leftW-56,30,bodyFmt_.Get(),brush_.muted.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        TextLine(L"Correction / instruction",x+18,y+238,104,44,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Category",x+leftW-152,y+218,136,18,tinyFmt_.Get(),brush_.muted.Get(),DWRITE_TEXT_ALIGNMENT_CENTER);
        const float composerY=y+292;
        const float sendW=88.0f, icon=44.0f;
        const float sendX=x+leftW-16-sendW;
        const float attachX=sendX-8-icon;
        const float emojiX=attachX-8-icon;
        AddIconButton(L"sim_emoji",IconKind::Smile,emojiX,composerY,icon,false);
        AddIconButton(L"sim_attach",IconKind::Paperclip,attachX,composerY,icon,false);
        AddButton(L"sim_send",L"Send",sendX,composerY,sendW,44,true);

        AddButton(L"ml_capture",L"Capture for Review",x+18,y+344,150,28,false);
        AddButton(L"ml_approve",L"Approve Latest",x+178,y+344,118,28,false);
        TextLine(L"Nothing silently changes production weights.",x+310,y+344,leftW-328,28,tinyFmt_.Get(),brush_.muted.Get());

        }
        const float rx=x+leftW+gap;
        Rounded(rx,y,rightW,184,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Review Queue",rx+16,y+10,rightW-32,28,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Captured",rx+18,y+52,84,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(std::to_wstring(trainingCaptured_),rx+112,y+48,70,24,bodyFmt_.Get(),brush_.text.Get());
        TextLine(L"Needs review",rx+18,y+82,84,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(std::to_wstring(trainingReviewPending_),rx+112,y+78,70,24,bodyFmt_.Get(),trainingReviewPending_?brush_.yellow.Get():brush_.green.Get());
        TextLine(L"Approved",rx+18,y+112,84,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(std::to_wstring(trainingApproved_),rx+112,y+108,70,24,bodyFmt_.Get(),brush_.green.Get());
        Text(L"Captured -> Review -> Approved -> Dataset",rx+18,y+142,rightW-36,26,tinyFmt_.Get(),brush_.cyan.Get());

        Rounded(rx,y+196,rightW,88,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Runtime Resolution",rx+16,y+204,rightW-32,26,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Foundation → LoRA → Behavior → Memory → Context",rx+18,y+238,rightW-36,20,tinyFmt_.Get(),brush_.cyan.Get());
        TextLine(assignedPersonaLoRA_,rx+18,y+258,rightW-36,18,tinyFmt_.Get(),brush_.muted.Get());

        Rounded(rx,y+296,rightW,86,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Trigger Rules",rx+16,y+304,rightW-132,24,h1Fmt_.Get(),brush_.text.Get());
        AddButton(L"ml_rules_manage",L"Manage",rx+rightW-104,y+304,86,24,false);
        TextLine(L"Last match: "+lastTriggerMatch_,rx+16,y+330,rightW-32,18,tinyFmt_.Get(),brush_.cyan.Get());
        float ry=y+350;
        int rshown=0;
        for(size_t i=0;i<triggerRules_.Rules().size() && rshown<2;i++,rshown++) {
            const auto& rule=triggerRules_.Rules()[i];
            TextLine(Widen(rule.name)+L"  ["+Widen(rule.pattern)+L"]",rx+18,ry,rightW-112,16,tinyFmt_.Get(),brush_.text.Get());
            TextLine(std::to_wstring(rule.priority)+(rule.terminal?L" • stop":L" • continue"),rx+rightW-98,ry,80,16,tinyFmt_.Get(),
                rule.terminal?brush_.yellow.Get():brush_.green.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);
            buttons_.push_back({{rx+16,ry,rx+rightW-16,ry+16},L"trigger:"+std::to_wstring(i)});
            ry+=16;
        }
        if(triggerRules_.Rules().empty()) {
            TextLine(L"No rules configured.",rx+18,ry,rightW-36,16,tinyFmt_.Get(),brush_.muted.Get());
        }
    }

    void DrawModelLabPersonas(float x,float y,float contentW) {
        const float gap=12.0f;
        const float inspectorW=360.0f;
        const float listW=contentW-inspectorW-gap;

        Rounded(x,y,listW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Personas & LoRAs",x+18,y+12,260,30,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Reusable persona behavior profiles and their assigned adapters.",x+18,y+40,listW-36,20,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"ml_persona_editor",L"Open Persona Editor",x+listW-158,y+14,140,30,true);

        TextLine(L"PERSONA",x+28,y+78,170,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"FOUNDATION",x+210,y+78,170,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"ASSIGNED LORA",x+392,y+78,170,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STATUS",x+574,y+78,listW-602,18,tinyFmt_.Get(),brush_.muted.Get());

        const float rowY=y+102;
        Rounded(x+18,rowY,listW-36,70,brush_.panel2.Get(),brush_.cyan.Get(),8);
        TextLine(Widen(simSettings_.persona.name.empty()?std::string("Default Persona"):simSettings_.persona.name),
            x+28,rowY+7,170,24,smallFmt_.Get(),brush_.text.Get());
        TextLine(Widen(simSettings_.persona.personality.empty()?std::string("Configured behavior profile"):simSettings_.persona.personality),
            x+28,rowY+34,170,20,tinyFmt_.Get(),brush_.muted.Get());

        const std::wstring foundation=selectedRegistryModel_>=0 && selectedRegistryModel_<(int)modelRegistry_.Models().size()
            ? Widen(modelRegistry_.Models()[(size_t)selectedRegistryModel_].modelName)
            : L"SARA Foundation / base";
        TextLine(foundation,x+210,rowY+10,170,24,smallFmt_.Get(),brush_.text.Get());
        TextLine(L"Version-pinned",x+210,rowY+36,170,18,tinyFmt_.Get(),brush_.cyan.Get());

        TextLine(assignedPersonaLoRA_,x+392,rowY+10,170,24,smallFmt_.Get(),brush_.text.Get());
        TextLine(L"Augments foundation",x+392,rowY+36,170,18,tinyFmt_.Get(),brush_.muted.Get());

        Badge(L"ACTIVE",x+574,rowY+20,brush_.green.Get(),74);

        Rounded(x+18,y+188,listW-36,174,brush_.sidebar.Get(),brush_.border.Get(),8);
        TextLine(L"Adapter Versions",x+32,y+198,220,26,h1Fmt_.Get(),brush_.text.Get());
        AddButton(L"adapter_new",L"New Persona LoRA",x+listW-160,y+198,128,28,true);
        AddButton(L"adapter_rollback",L"Rollback",x+listW-258,y+198,88,28,false);
        AddButton(L"adapter_export",L"Export",x+listW-346,y+198,78,28,false);
        float ay=y+236;
        int shown=0;
        for(size_t i=0;i<personaAdapterRegistry_.Adapters().size() && shown<2;i++) {
            const auto& a=personaAdapterRegistry_.Adapters()[i];
            if(a.personaName!=simSettings_.persona.name) continue;
            const bool selected=(int)i==selectedPersonaAdapter_;
            if(selected) Rounded(x+26,ay-4,listW-52,30,brush_.panel2.Get(),brush_.cyan.Get(),6);
            TextLine(Widen(a.adapterName+" "+a.version),x+32,ay,220,22,smallFmt_.Get(),brush_.text.Get());
            TextLine(Widen(sentinel::simulation::ToString(a.stage)),x+264,ay,90,22,tinyFmt_.Get(),
                a.stage==sentinel::simulation::AdapterStage::Active?brush_.green.Get():brush_.cyan.Get());
            TextLine(Widen(a.foundationId),x+366,ay,listW-520,22,tinyFmt_.Get(),brush_.muted.Get());
            if(a.stage!=sentinel::simulation::AdapterStage::Active)
                AddButton(L"adapter_activate:"+std::to_wstring(i),L"Activate",x+listW-126,ay-2,94,26,false);
            buttons_.push_back({{x+26,ay-4,x+listW-132,ay+26},L"adapter_select:"+std::to_wstring(i)});
            ay+=38; ++shown;
        }
        if(shown==0) {
            TextLine(L"No LoRA versions for this persona yet.",x+32,ay,300,22,smallFmt_.Get(),brush_.muted.Get());
        }
        int activeAdapter=personaAdapterRegistry_.ResolveActiveIndex(simSettings_.persona.name);
        TextLine(L"Compare",x+32,y+316,64,18,tinyFmt_.Get(),brush_.muted.Get());
        if(selectedPersonaAdapter_>=0 && selectedPersonaAdapter_<(int)personaAdapterRegistry_.Adapters().size()) {
            const auto& selected=personaAdapterRegistry_.Adapters()[(size_t)selectedPersonaAdapter_];
            TextLine(L"Selected "+Widen(selected.version)+L" • "+Widen(selected.foundationId),x+100,y+312,listW-250,22,tinyFmt_.Get(),brush_.cyan.Get());
        } else TextLine(L"Select an adapter version",x+100,y+312,listW-250,22,tinyFmt_.Get(),brush_.muted.Get());
        if(activeAdapter>=0) {
            const auto& active=personaAdapterRegistry_.Adapters()[(size_t)activeAdapter];
            TextLine(L"Active "+Widen(active.version)+L" • "+Widen(active.foundationId),x+100,y+336,listW-250,20,tinyFmt_.Get(),brush_.green.Get());
        }

        const float rx=x+listW+gap;
        Rounded(rx,y,inspectorW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Persona Inspector",rx+18,y+12,inspectorW-36,30,h1Fmt_.Get(),brush_.text.Get());
        Badge(L"RUNTIME",rx+inspectorW-104,y+16,brush_.cyan.Get(),86);

        TextLine(L"Name",rx+18,y+62,92,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(Widen(simSettings_.persona.name),rx+118,y+58,inspectorW-136,24,smallFmt_.Get(),brush_.text.Get());
        TextLine(L"Age",rx+18,y+92,92,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(std::to_wstring(simSettings_.persona.age),rx+118,y+88,inspectorW-136,24,smallFmt_.Get(),brush_.text.Get());
        TextLine(L"Personality",rx+18,y+122,92,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(Widen(simSettings_.persona.personality),rx+118,y+118,inspectorW-136,24,smallFmt_.Get(),brush_.text.Get());
        TextLine(L"Writing style",rx+18,y+152,92,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(Widen(simSettings_.persona.writingStyle),rx+118,y+148,inspectorW-136,24,smallFmt_.Get(),brush_.text.Get());

        target_->DrawLine(D2D1::Point2F(rx+18,y+184),D2D1::Point2F(rx+inspectorW-18,y+184),brush_.border.Get(),1);
        TextLine(L"Style Tuning",rx+18,y+196,inspectorW-140,24,smallFmt_.Get(),brush_.cyan.Get());
        AddButton(L"ml_style_save",L"Save Style",rx+inspectorW-112,y+194,94,28,true);
        TextLine(L"Intelligence",rx+18,y+228,150,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Slang",rx+184,y+228,150,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Grammar",rx+18,y+266,150,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Typos",rx+184,y+266,150,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Emoji",rx+18,y+304,150,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Mood",rx+184,y+304,150,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Foundation → LoRA → Behavior → Memory → Context",rx+18,y+350,inspectorW-36,18,tinyFmt_.Get(),brush_.cyan.Get());
    }

    void DrawModelLabFoundationForks(float x,float y,float contentW) {
        const float gap=12.0f;
        const float detailW=350.0f;
        const float listW=contentW-detailW-gap;

        Rounded(x,y,listW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Foundation Forks",x+18,y+12,260,30,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Original base models stay immutable; SARA descendants are versioned independently.",x+18,y+40,listW-36,20,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"foundation_new_fork",L"Create Fork",x+listW-114,y+14,96,30,true);

        TextLine(L"NAME / VERSION",x+28,y+78,210,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"PARENT",x+250,y+78,150,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"TYPE",x+412,y+78,92,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STATE",x+514,y+78,listW-540,18,tinyFmt_.Get(),brush_.muted.Get());

        float yy=y+102;
        for(size_t i=0;i<foundationRegistry_.Models().size() && i<4;i++) {
            const auto& m=foundationRegistry_.Models()[i];
            const bool selected=(int)i==selectedFoundation_;
            Rounded(x+18,yy,listW-36,58,selected?brush_.panel2.Get():brush_.sidebar.Get(),
                selected?brush_.cyan.Get():brush_.border.Get(),8);
            TextLine(Widen(m.name+" "+m.version),x+28,yy+4,210,22,smallFmt_.Get(),brush_.text.Get());
            TextLine(Widen(m.parentId.empty()?"—":m.parentId),x+250,yy+4,150,22,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(m.immutableBase?L"IMMUTABLE BASE":L"SARA FORK",x+412,yy+4,92,22,tinyFmt_.Get(),m.immutableBase?brush_.yellow.Get():brush_.cyan.Get());
            TextLine(Widen(sentinel::simulation::ToString(m.stage)),x+514,yy+4,listW-540,22,tinyFmt_.Get(),
                m.stage==sentinel::simulation::FoundationStage::Active?brush_.green.Get():brush_.text.Get());
            TextLine(L"ID "+Widen(m.id),x+28,yy+31,listW-56,18,tinyFmt_.Get(),brush_.muted.Get());
            buttons_.push_back({{x+18,yy,x+listW-18,yy+58},L"foundation:"+std::to_wstring(i)});
            yy+=66;
        }

        const float rx=x+listW+gap;
        Rounded(rx,y,detailW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Selected Foundation",rx+18,y+12,detailW-36,30,h1Fmt_.Get(),brush_.text.Get());

        if(!foundationRegistry_.Models().empty()) {
            selectedFoundation_=std::clamp(selectedFoundation_,0,(int)foundationRegistry_.Models().size()-1);
            const auto& m=foundationRegistry_.Models()[(size_t)selectedFoundation_];
            Badge(m.immutableBase?L"BASE":Widen(sentinel::simulation::ToString(m.stage)),rx+detailW-112,y+16,
                m.immutableBase?brush_.yellow.Get():(m.stage==sentinel::simulation::FoundationStage::Active?brush_.green.Get():brush_.cyan.Get()),94);
            TextLine(L"Name",rx+18,y+64,80,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(m.name),rx+108,y+60,detailW-126,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"Version",rx+18,y+94,80,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(m.version),rx+108,y+90,detailW-126,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"Parent",rx+18,y+124,80,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(m.parentId.empty()?"Original downloaded model":m.parentId),rx+108,y+120,detailW-126,24,smallFmt_.Get(),brush_.text.Get());

            target_->DrawLine(D2D1::Point2F(rx+18,y+156),D2D1::Point2F(rx+detailW-18,y+156),brush_.border.Get(),1);
            if(m.immutableBase) {
                Text(L"This entry is immutable. Create a SARA Foundation fork to train or evolve it without touching the original model.",
                    rx+18,y+174,detailW-36,54,smallFmt_.Get(),brush_.yellow.Get());
            } else {
                AddButton(L"foundation_approve",L"Approve",rx+18,y+176,94,32,false);
                AddButton(L"foundation_activate",L"Activate",rx+122,y+176,94,32,true);
                AddButton(L"foundation_rollback",L"Rollback",rx+226,y+176,106,32,false);
            }

            TextLine(L"Version Safety",rx+18,y+232,detailW-36,24,smallFmt_.Get(),brush_.cyan.Get());
            Text(L"Base remains intact • child lineage retained • previous active fork kept for rollback",
                rx+18,y+258,detailW-36,34,tinyFmt_.Get(),brush_.muted.Get());

            const sentinel::simulation::FoundationModel* parent=nullptr;
            if(!m.parentId.empty()) {
                for(const auto& candidate:foundationRegistry_.Models())
                    if(candidate.id==m.parentId) { parent=&candidate; break; }
            }
            TextLine(L"Compare",rx+18,y+300,detailW-36,20,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"Selected",rx+18,y+324,62,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(m.name+" "+m.version),rx+86,y+320,detailW-104,22,tinyFmt_.Get(),brush_.cyan.Get());
            TextLine(L"Parent",rx+18,y+348,62,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(parent?Widen(parent->name+" "+parent->version):L"Original downloaded model",
                rx+86,y+344,detailW-104,22,tinyFmt_.Get(),parent?brush_.text.Get():brush_.yellow.Get());
        }
    }

    void DrawModelLabDatasets(float x,float y,float contentW) {
        const float gap=12.0f;
        const float rightW=350.0f;
        const float leftW=contentW-rightW-gap;

        Rounded(x,y,leftW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Datasets",x+18,y+12,220,30,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Reviewed training examples and lineage-aware dataset snapshots.",x+18,y+40,leftW-260,20,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"dataset_import",L"Import",x+leftW-232,y+14,70,30,false);
        AddButton(L"dataset_snapshot",L"Create Snapshot",x+leftW-152,y+14,134,30,true);

        TextLine(L"EXAMPLE",x+28,y+78,100,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"PERSONA",x+140,y+78,150,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STATE",x+302,y+78,92,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"INPUT / TARGET",x+406,y+78,leftW-434,18,tinyFmt_.Get(),brush_.muted.Get());

        float yy=y+102;
        int shown=0;
        for(size_t i=trainingData_.Examples().size(); i>0 && shown<4; --i,++shown) {
            const size_t exampleIndex=i-1;
            const auto& e=trainingData_.Examples()[exampleIndex];
            const bool selected=(int)exampleIndex==selectedTrainingExample_;
            Rounded(x+18,yy,leftW-36,58,selected?brush_.panel2.Get():brush_.sidebar.Get(),selected?brush_.cyan.Get():brush_.border.Get(),8);
            TextLine(Widen(e.id),x+28,yy+4,100,22,tinyFmt_.Get(),brush_.text.Get());
            TextLine(Widen(e.persona),x+140,yy+4,150,22,tinyFmt_.Get(),brush_.text.Get());
            TextLine(Widen(sentinel::simulation::ToString(e.state)),x+302,yy+4,92,22,tinyFmt_.Get(),
                e.state==sentinel::simulation::TrainingExampleState::Approved?brush_.green.Get():
                e.state==sentinel::simulation::TrainingExampleState::Review?brush_.yellow.Get():brush_.muted.Get());
            TextLine(Widen(e.input),x+406,yy+2,leftW-434,22,tinyFmt_.Get(),brush_.text.Get());
            TextLine(Widen(e.targetResponse),x+406,yy+28,leftW-434,20,tinyFmt_.Get(),brush_.muted.Get());
            buttons_.push_back({{x+18,yy,x+leftW-18,yy+58},L"training_example:"+std::to_wstring(exampleIndex)});
            yy+=66;
        }
        if(shown==0) {
            Rounded(x+18,yy,leftW-36,60,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"No captured examples yet. Use Train → Capture for Review.",x+34,yy+10,leftW-68,40,bodyFmt_.Get(),brush_.muted.Get());
        }

        const float rx=x+leftW+gap;
        Rounded(rx,y,rightW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(selectedTrainingExample_>=0?L"Review Inspector":L"Dataset Inspector",rx+18,y+12,rightW-36,30,h1Fmt_.Get(),brush_.text.Get());

        if(selectedTrainingExample_>=0 && selectedTrainingExample_<(int)trainingData_.Examples().size()) {
            const auto& selected=trainingData_.Examples()[(size_t)selectedTrainingExample_];
            Badge(Widen(sentinel::simulation::ToString(selected.state)),rx+rightW-110,y+16,
                selected.state==sentinel::simulation::TrainingExampleState::Approved?brush_.green.Get():
                selected.state==sentinel::simulation::TrainingExampleState::Rejected?brush_.red.Get():brush_.yellow.Get(),92);
            TextLine(L"Category",rx+18,y+52,74,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(selected.category),rx+98,y+48,rightW-116,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"Captured",rx+18,y+78,74,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(selected.createdUtc.empty()?"Legacy record":selected.createdUtc),rx+98,y+74,rightW-116,24,tinyFmt_.Get(),brush_.text.Get());
            TextLine(L"Input",rx+18,y+108,74,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(selected.input),rx+98,y+104,rightW-116,26,tinyFmt_.Get(),brush_.text.Get());
            TextLine(L"Target response",rx+18,y+132,rightW-36,18,tinyFmt_.Get(),brush_.muted.Get());
            AddButton(L"training_review_save",L"Save Edit",rx+18,y+222,92,28,false);
            AddButton(L"training_review_approve",L"Approve",rx+120,y+222,92,28,true);
            AddButton(L"training_review_reject",L"Reject",rx+222,y+222,92,28,false);
        } else if(selectedDatasetSnapshot_>=0 && selectedDatasetSnapshot_<(int)trainingData_.Snapshots().size()) {
            const auto& selected=trainingData_.Snapshots()[(size_t)selectedDatasetSnapshot_];
            Badge(L"SNAPSHOT",rx+rightW-108,y+16,brush_.cyan.Get(),90);
            TextLine(L"Name",rx+18,y+54,74,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(selected.name),rx+98,y+50,rightW-116,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"ID",rx+18,y+82,74,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(selected.id),rx+98,y+78,rightW-116,24,tinyFmt_.Get(),brush_.text.Get());
            TextLine(L"Created",rx+18,y+110,74,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(selected.createdUtc.empty()?"Legacy snapshot":selected.createdUtc),rx+98,y+106,rightW-116,24,tinyFmt_.Get(),brush_.text.Get());
            TextLine(L"Parent",rx+18,y+138,74,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(selected.parentId.empty()?"Root / no parent":selected.parentId),rx+98,y+134,rightW-116,24,tinyFmt_.Get(),brush_.cyan.Get());
            TextLine(L"Examples",rx+18,y+166,74,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(std::to_wstring(selected.exampleIds.size()),rx+98,y+162,rightW-116,24,smallFmt_.Get(),brush_.text.Get());
            AddButton(L"dataset_export",L"Export Snapshot",rx+18,y+206,130,30,true);
        } else {
            Text(L"Select a training example to review it, or select a dataset snapshot below to inspect lineage and export it.",
                rx+18,y+54,rightW-36,70,smallFmt_.Get(),brush_.muted.Get());
        }

        target_->DrawLine(D2D1::Point2F(rx+18,y+266),D2D1::Point2F(rx+rightW-18,y+266),brush_.border.Get(),1);
        TextLine(L"Dataset Snapshots",rx+18,y+276,rightW-36,24,smallFmt_.Get(),brush_.cyan.Get());
        float sy=y+306;
        int sshown=0;
        for(size_t i=trainingData_.Snapshots().size(); i>0 && sshown<2; --i,++sshown) {
            const size_t snapshotIndex=i-1;
            const auto& s=trainingData_.Snapshots()[snapshotIndex];
            const bool selected=(int)snapshotIndex==selectedDatasetSnapshot_;
            if(selected) Rounded(rx+14,sy-2,rightW-28,22,brush_.panel2.Get(),brush_.cyan.Get(),5);
            TextLine(Widen(s.name),rx+18,sy,rightW-120,20,tinyFmt_.Get(),brush_.text.Get());
            TextLine(std::to_wstring(s.exampleIds.size())+L" examples",rx+210,sy,rightW-228,20,tinyFmt_.Get(),brush_.cyan.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);
            buttons_.push_back({{rx+14,sy-2,rx+rightW-14,sy+20},L"dataset_select:"+std::to_wstring(snapshotIndex)});
            sy+=24;
        }
        if(sshown==0) TextLine(L"No snapshots yet.",rx+18,sy,rightW-36,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Captured "+std::to_wstring(trainingData_.Examples().size())+L"  •  Approved "+
            std::to_wstring(trainingData_.Count(sentinel::simulation::TrainingExampleState::Approved)),
            rx+18,y+354,rightW-36,20,tinyFmt_.Get(),brush_.muted.Get());
    }

    void DrawModelLabJobs(float x,float y,float contentW) {
        const float gap=12.0f;
        const float inspectorW=360.0f;
        const float listW=contentW-inspectorW-gap;

        Rounded(x,y,listW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Training Jobs",x+18,y+12,260,30,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Persistent run history with exact foundation and dataset lineage.",x+18,y+40,listW-180,20,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"job_new",L"New Training Job",x+listW-152,y+14,134,30,true);

        TextLine(L"JOB",x+28,y+80,100,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"DATASET",x+140,y+80,150,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STATE",x+302,y+80,100,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"PROGRESS",x+414,y+80,90,18,tinyFmt_.Get(),brush_.muted.Get());

        float yy=y+104;
        if(trainingJobRegistry_.Jobs().empty()) {
            Rounded(x+18,yy,listW-36,60,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"No training jobs yet. Queue one from the current foundation and dataset.",
                x+34,yy+8,listW-68,42,bodyFmt_.Get(),brush_.muted.Get());
        } else {
            for(size_t i=0;i<trainingJobRegistry_.Jobs().size() && i<4;i++) {
                const auto& job=trainingJobRegistry_.Jobs()[i];
                const bool selected=(int)i==selectedTrainingJob_;
                Rounded(x+18,yy,listW-36,58,selected?brush_.panel2.Get():brush_.sidebar.Get(),
                    selected?brush_.cyan.Get():brush_.border.Get(),8);
                TextLine(Widen(job.id),x+28,yy+5,100,22,smallFmt_.Get(),brush_.text.Get());
                TextLine(Widen(job.dataset),x+140,yy+5,150,22,tinyFmt_.Get(),brush_.text.Get());
                TextLine(Widen(job.state),x+302,yy+5,100,22,tinyFmt_.Get(),
                    job.state=="COMPLETED"?brush_.green.Get():job.state=="RUNNING"?brush_.cyan.Get():brush_.yellow.Get());
                TextLine(std::to_wstring(job.progress)+L"%",x+414,yy+5,72,22,tinyFmt_.Get(),brush_.text.Get());
                TextLine(Widen(job.createdUtc.empty()?"Legacy run":job.createdUtc),x+28,yy+31,listW-180,18,tinyFmt_.Get(),brush_.muted.Get());
                if(job.state=="QUEUED") AddButton(L"job_start:"+std::to_wstring(i),L"Start",x+listW-94,yy+14,62,28,false);
                else if(job.state=="RUNNING") AddButton(L"job_complete:"+std::to_wstring(i),L"Complete",x+listW-112,yy+14,80,28,true);
                buttons_.push_back({{x+18,yy,x+listW-122,yy+58},L"job_select:"+std::to_wstring(i)});
                yy+=66;
            }
        }

        const float rx=x+listW+gap;
        Rounded(rx,y,inspectorW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Run Inspector",rx+18,y+12,inspectorW-36,30,h1Fmt_.Get(),brush_.text.Get());
        if(selectedTrainingJob_>=0 && selectedTrainingJob_<(int)trainingJobRegistry_.Jobs().size()) {
            const auto& job=trainingJobRegistry_.Jobs()[(size_t)selectedTrainingJob_];
            Badge(Widen(job.state),rx+inspectorW-110,y+16,
                job.state=="COMPLETED"?brush_.green.Get():job.state=="RUNNING"?brush_.cyan.Get():brush_.yellow.Get(),92);
            TextLine(L"Job",rx+18,y+58,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(job.id),rx+110,y+54,inspectorW-128,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"Foundation",rx+18,y+88,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(job.baseModel),rx+110,y+84,inspectorW-128,24,tinyFmt_.Get(),brush_.text.Get());
            TextLine(L"Dataset",rx+18,y+118,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(job.dataset),rx+110,y+114,inspectorW-128,24,tinyFmt_.Get(),brush_.cyan.Get());
            TextLine(L"Created",rx+18,y+154,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(job.createdUtc.empty()?"Legacy run":job.createdUtc),rx+110,y+150,inspectorW-128,22,tinyFmt_.Get(),brush_.text.Get());
            TextLine(L"Started",rx+18,y+182,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(job.startedUtc.empty()?"Not started":job.startedUtc),rx+110,y+178,inspectorW-128,22,tinyFmt_.Get(),brush_.text.Get());
            TextLine(L"Completed",rx+18,y+210,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(job.completedUtc.empty()?"Not completed":job.completedUtc),rx+110,y+206,inspectorW-128,22,tinyFmt_.Get(),brush_.text.Get());
            TextLine(L"Progress",rx+18,y+244,84,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(std::to_wstring(job.progress)+L"%",rx+110,y+240,80,24,smallFmt_.Get(),brush_.text.Get());
            Rounded(rx+18,y+276,inspectorW-36,10,brush_.panel2.Get(),nullptr,5);
            if(job.progress>0) Rounded(rx+18,y+276,(inspectorW-36)*(job.progress/100.0f),10,brush_.cyan.Get(),nullptr,5);
            Text(L"Training execution remains isolated from live inference. This record preserves the exact dataset and foundation used by the run.",
                rx+18,y+308,inspectorW-36,52,tinyFmt_.Get(),brush_.muted.Get());
        } else {
            Text(L"Select a training run on the left to inspect its foundation, dataset lineage, timestamps, and progress.",
                rx+18,y+58,inspectorW-36,70,smallFmt_.Get(),brush_.muted.Get());
        }
    }

    void DrawModelLabEvaluation(float x,float y,float contentW) {
        const float gap=12.0f;

        Rounded(x,y,contentW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Evaluation Suite",x+18,y+10,260,30,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Persistent multidimensional regression testing for candidate models.",x+18,y+38,contentW-200,20,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"eval_export_run",L"Export Run",x+contentW-270,y+14,100,30,false);
        AddButton(L"model_eval",L"Run Evaluation",x+contentW-160,y+14,142,30,true);

        const sentinel::simulation::EvaluationRun* selected=nullptr;
        if(selectedEvaluationRun_>=0 && selectedEvaluationRun_<(int)evaluationRuns_.Runs().size())
            selected=&evaluationRuns_.Runs()[(size_t)selectedEvaluationRun_];
        else if(selectedRegistryModel_>=0 && selectedRegistryModel_<(int)modelRegistry_.Models().size()) {
            int idx=evaluationRuns_.LatestIndexForCandidate(modelRegistry_.Models()[(size_t)selectedRegistryModel_].id);
            if(idx>=0) selected=&evaluationRuns_.Runs()[(size_t)idx];
        }

        if(!selected) {
            Rounded(x+18,y+72,contentW-36,112,brush_.sidebar.Get(),brush_.border.Get(),9);
            TextLine(L"No persisted evaluation run selected.",x+34,y+88,contentW-68,28,bodyFmt_.Get(),brush_.muted.Get());
            TextLine(L"Select a registered candidate and run the suite to create persona, policy, style, memory, trigger, and diversity scores.",
                x+34,y+120,contentW-68,42,smallFmt_.Get(),brush_.muted.Get());
        } else {
            TextLine(Widen(selected->candidateName),x+18,y+66,220,22,smallFmt_.Get(),brush_.text.Get());
            TextLine(Widen(selected->createdUtc),x+248,y+66,176,22,tinyFmt_.Get(),brush_.muted.Get());

            const std::wstring delta=selected->previousOverallScore<0
                ? L"First run"
                : (selected->regressionDelta>=0?L"+":L"")+std::to_wstring(selected->regressionDelta)+L" vs prior";
            Badge(std::to_wstring(selected->overallScore)+L"/100",x+contentW-196,y+62,
                selected->overallScore>=80?brush_.green.Get():selected->overallScore>=60?brush_.yellow.Get():brush_.red.Get(),82);
            TextLine(delta,x+contentW-106,y+63,88,22,tinyFmt_.Get(),
                selected->regressionDelta<0?brush_.yellow.Get():brush_.green.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);

            const float cardsY=y+94;
            const float cardGap=8.0f;
            const float cardW=(contentW-36-cardGap*2)/3.0f;
            const float cardH=60.0f;

            for(size_t i=0;i<selected->dimensions.size() && i<6;i++) {
                const auto& d=selected->dimensions[i];
                const int row=(int)i/3;
                const int col=(int)i%3;
                const float cx=x+18+col*(cardW+cardGap);
                const float cy=cardsY+row*(cardH+8);
                ID2D1Brush* accent=d.passed?brush_.green.Get():(d.score>=60?brush_.yellow.Get():brush_.red.Get());
                Rounded(cx,cy,cardW,cardH,brush_.sidebar.Get(),brush_.border.Get(),8);
                target_->DrawLine(D2D1::Point2F(cx+10,cy+1),D2D1::Point2F(cx+cardW-10,cy+1),accent,1.5f);
                TextLine(Widen(sentinel::simulation::ToString(d.dimension)),cx+12,cy+6,cardW-88,18,tinyFmt_.Get(),brush_.muted.Get());
                TextLine(std::to_wstring(d.score),cx+cardW-66,cy+5,54,22,bodyFmt_.Get(),accent,DWRITE_TEXT_ALIGNMENT_TRAILING);
                TextLine(d.passed?L"PASS":L"REVIEW",cx+12,cy+30,72,18,tinyFmt_.Get(),accent);
                TextLine(Widen(d.details),cx+90,cy+27,cardW-102,24,tinyFmt_.Get(),brush_.text.Get());
            }

            if(!selected->cases.empty()) {
                size_t passed=0;
                for(const auto& cr:selected->cases) if(cr.passed) ++passed;
                TextLine(L"Named cases "+std::to_wstring(passed)+L"/"+std::to_wstring(selected->cases.size())+L" passed",
                    x+18,y+222,contentW-36,18,tinyFmt_.Get(),passed==selected->cases.size()?brush_.green.Get():brush_.yellow.Get());
            }
        }

        const float lowerY=y+232;
        const float leftW=(contentW-gap)*0.55f;
        const float rightW=contentW-gap-leftW;

        Rounded(x,lowerY,leftW,150,brush_.panel2.Get(),brush_.border.Get(),9);
        TextLine(L"Recent Evaluation Runs",x+16,lowerY+8,leftW-32,24,smallFmt_.Get(),brush_.text.Get());

        float ry=lowerY+38;
        int shown=0;
        for(size_t i=evaluationRuns_.Runs().size();i>0 && shown<4;--i,++shown) {
            const size_t idx=i-1;
            const auto& run=evaluationRuns_.Runs()[idx];
            const bool isSelected=(int)idx==selectedEvaluationRun_;
            if(isSelected) Rounded(x+12,ry-2,leftW-24,25,brush_.sidebar.Get(),brush_.cyan.Get(),5);
            TextLine(Widen(run.candidateName),x+20,ry,leftW-220,20,tinyFmt_.Get(),brush_.text.Get());
            TextLine(std::to_wstring(run.overallScore),x+leftW-194,ry,46,20,tinyFmt_.Get(),
                run.overallScore>=80?brush_.green.Get():run.overallScore>=60?brush_.yellow.Get():brush_.red.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);
            TextLine(Widen(run.createdUtc),x+leftW-140,ry,120,20,tinyFmt_.Get(),brush_.muted.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);
            buttons_.push_back({{x+12,ry-2,x+leftW-12,ry+23},L"evalrun:"+std::to_wstring(idx)});
            ry+=26;
        }
        if(shown==0) TextLine(L"No evaluation history yet.",x+20,ry,leftW-40,22,tinyFmt_.Get(),brush_.muted.Get());

        const float rx=x+leftW+gap;
        Rounded(rx,lowerY,rightW,150,brush_.panel2.Get(),brush_.border.Get(),9);
        TextLine(L"Candidate Comparison",rx+16,lowerY+8,rightW-154,24,smallFmt_.Get(),brush_.text.Get());
        AddButton(L"eval_export_compare",L"Export",rx+rightW-100,lowerY+8,82,24,false);

        const sentinel::simulation::EvaluationRun* compare=nullptr;
        if(comparisonEvaluationRun_>=0 && comparisonEvaluationRun_<(int)evaluationRuns_.Runs().size())
            compare=&evaluationRuns_.Runs()[(size_t)comparisonEvaluationRun_];

        float cy=lowerY+38;
        int candidates=0;
        for(size_t i=0;i<modelRegistry_.Models().size() && candidates<3;i++) {
            const auto& model=modelRegistry_.Models()[i];
            int latest=evaluationRuns_.LatestIndexForCandidate(model.id);
            if(latest<0) continue;
            const auto& run=evaluationRuns_.Runs()[(size_t)latest];
            const bool chosen=latest==comparisonEvaluationRun_;
            if(chosen) Rounded(rx+12,cy-2,rightW-24,24,brush_.sidebar.Get(),brush_.blue.Get(),5);
            TextLine(Widen(model.modelName),rx+20,cy,rightW-150,20,tinyFmt_.Get(),brush_.text.Get());
            TextLine(std::to_wstring(run.overallScore),rx+rightW-124,cy,42,20,tinyFmt_.Get(),
                run.overallScore>=80?brush_.green.Get():run.overallScore>=60?brush_.yellow.Get():brush_.red.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);
            if(selected) {
                const int delta=run.overallScore-selected->overallScore;
                TextLine((delta>=0?L"+":L"")+std::to_wstring(delta),rx+rightW-72,cy,52,20,tinyFmt_.Get(),
                    delta<0?brush_.yellow.Get():brush_.muted.Get(),DWRITE_TEXT_ALIGNMENT_TRAILING);
            }
            buttons_.push_back({{rx+12,cy-2,rx+rightW-12,cy+22},L"evalcompare:"+std::to_wstring(latest)});
            cy+=25;
            ++candidates;
        }
        if(candidates==0) TextLine(L"No evaluated candidates.",rx+20,cy,rightW-40,22,tinyFmt_.Get(),brush_.muted.Get());

        if(selected && compare) {
            const auto dims=std::array<sentinel::simulation::EvaluationDimension,3>{
                sentinel::simulation::EvaluationDimension::PersonaConsistency,
                sentinel::simulation::EvaluationDimension::MemoryRecall,
                sentinel::simulation::EvaluationDimension::TriggerRegression
            };
            std::wstring summary;
            for(size_t i=0;i<dims.size();++i) {
                const int a=sentinel::simulation::DimensionScore(*selected,dims[i]);
                const int b=sentinel::simulation::DimensionScore(*compare,dims[i]);
                if(i) summary+=L"  ";
                summary+=Widen(sentinel::simulation::ToString(dims[i]))+L" "+std::to_wstring(a-b);
            }
            TextLine(summary,rx+16,lowerY+116,rightW-32,20,tinyFmt_.Get(),brush_.cyan.Get());
        } else if(selected && !selected->warnings.empty()) {
            TextLine(L"Regression / warnings: "+Widen(selected->warnings.front()),rx+16,lowerY+116,rightW-32,20,tinyFmt_.Get(),brush_.yellow.Get());
        }
    }

    void DrawModelLabDeployment(float x,float y,float contentW) {
        const float gap=12.0f;
        const float inspectorW=390.0f;
        const float listW=contentW-inspectorW-gap;

        Rounded(x,y,listW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Deployment & Rollback",x+18,y+12,290,30,h1Fmt_.Get(),brush_.text.Get());
        TextLine(L"Pin, activate, audit, and roll back the complete SARA runtime stack.",x+18,y+40,listW-194,20,tinyFmt_.Get(),brush_.muted.Get());
        AddButton(L"deployment_prepare",L"Prepare Package",x+listW-160,y+14,142,30,true);

        TextLine(L"PACKAGE",x+28,y+78,110,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"CANDIDATE",x+150,y+78,170,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"EVAL",x+332,y+78,58,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STATE",x+402,y+78,94,18,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"LOCK",x+508,y+78,listW-536,18,tinyFmt_.Get(),brush_.muted.Get());

        float yy=y+102;
        int shown=0;
        for(size_t i=deploymentRegistry_.Packages().size();i>0 && shown<4;--i,++shown) {
            const size_t idx=i-1;
            const auto& p=deploymentRegistry_.Packages()[idx];
            const bool selected=(int)idx==selectedDeployment_;
            Rounded(x+18,yy,listW-36,58,selected?brush_.panel2.Get():brush_.sidebar.Get(),
                selected?brush_.cyan.Get():brush_.border.Get(),8);
            TextLine(Widen(p.id),x+28,yy+4,110,22,smallFmt_.Get(),brush_.text.Get());
            TextLine(Widen(p.candidateName),x+150,yy+4,170,22,tinyFmt_.Get(),brush_.text.Get());
            TextLine(std::to_wstring(p.evaluationScore),x+332,yy+4,58,22,tinyFmt_.Get(),
                p.evaluationScore>=80?brush_.green.Get():p.evaluationScore>=60?brush_.yellow.Get():brush_.red.Get());
            TextLine(Widen(sentinel::simulation::ToString(p.stage)),x+402,yy+4,94,22,tinyFmt_.Get(),
                p.stage==sentinel::simulation::DeploymentStage::Active?brush_.green.Get():
                p.stage==sentinel::simulation::DeploymentStage::RolledBack?brush_.yellow.Get():brush_.cyan.Get());
            TextLine(p.versionLocked?L"LOCKED":L"OPEN",x+508,yy+4,listW-536,22,tinyFmt_.Get(),
                p.versionLocked?brush_.yellow.Get():brush_.muted.Get());
            TextLine(Widen(p.foundationName)+L"  •  "+Widen(p.adapterName.empty()?"No LoRA":p.adapterName),
                x+28,yy+31,listW-56,18,tinyFmt_.Get(),brush_.muted.Get());
            buttons_.push_back({{x+18,yy,x+listW-18,yy+58},L"deployment_select:"+std::to_wstring(idx)});
            yy+=66;
        }
        if(shown==0) {
            Rounded(x+18,yy,listW-36,62,brush_.sidebar.Get(),brush_.border.Get(),8);
            Text(L"No deployment packages yet. Select an approved, evaluated candidate and choose Prepare Package.",
                x+34,yy+10,listW-68,42,smallFmt_.Get(),brush_.muted.Get());
        }

        const float rx=x+listW+gap;
        Rounded(rx,y,inspectorW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Deployment Inspector",rx+18,y+12,inspectorW-36,30,h1Fmt_.Get(),brush_.text.Get());

        if(selectedDeployment_>=0 && selectedDeployment_<(int)deploymentRegistry_.Packages().size()) {
            const auto& p=deploymentRegistry_.Packages()[(size_t)selectedDeployment_];
            ID2D1Brush* stageBrush=p.stage==sentinel::simulation::DeploymentStage::Active?brush_.green.Get():
                p.stage==sentinel::simulation::DeploymentStage::RolledBack?brush_.yellow.Get():brush_.cyan.Get();
            Badge(Widen(sentinel::simulation::ToString(p.stage)),rx+inspectorW-118,y+16,stageBrush,100);

            TextLine(L"Candidate",rx+18,y+62,98,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(p.candidateName),rx+120,y+58,inspectorW-138,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"Evaluation",rx+18,y+92,98,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(p.evaluationRunId)+L"  •  "+std::to_wstring(p.evaluationScore)+L"/100",
                rx+120,y+88,inspectorW-138,24,smallFmt_.Get(),p.evaluationScore>=80?brush_.green.Get():brush_.yellow.Get());
            TextLine(L"Foundation",rx+18,y+122,98,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(p.foundationName),rx+120,y+118,inspectorW-138,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"Persona",rx+18,y+152,98,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(p.personaName),rx+120,y+148,inspectorW-138,24,smallFmt_.Get(),brush_.text.Get());
            TextLine(L"LoRA",rx+18,y+182,98,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(p.adapterName.empty()?L"No adapter":Widen(p.adapterName),rx+120,y+178,inspectorW-138,24,smallFmt_.Get(),brush_.cyan.Get());

            target_->DrawLine(D2D1::Point2F(rx+18,y+214),D2D1::Point2F(rx+inspectorW-18,y+214),brush_.border.Get(),1);
            TextLine(L"Version lock",rx+18,y+226,98,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(p.versionLocked?L"LOCKED — runtime changes route through Deployment":L"Unlocked",
                rx+120,y+222,inspectorW-138,30,tinyFmt_.Get(),p.versionLocked?brush_.yellow.Get():brush_.muted.Get());

            AddButton(L"deployment_activate",L"Activate",rx+18,y+266,90,32,true);
            AddButton(L"deployment_rollback",L"Rollback",rx+118,y+266,90,32,false);
            AddButton(L"deployment_lock",p.versionLocked?L"Unlock":L"Lock",rx+218,y+266,72,32,false);
            AddButton(L"deployment_export",L"Export",rx+300,y+266,72,32,false);

            TextLine(L"Created",rx+18,y+314,72,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(Widen(p.createdUtc),rx+96,y+310,inspectorW-114,20,tinyFmt_.Get(),brush_.text.Get());
            TextLine(L"Activated",rx+18,y+340,72,18,tinyFmt_.Get(),brush_.muted.Get());
            TextLine(p.activatedUtc.empty()?L"Not yet":Widen(p.activatedUtc),rx+96,y+336,inspectorW-114,20,tinyFmt_.Get(),brush_.text.Get());
            if(!p.previousDeploymentId.empty())
                TextLine(L"Previous: "+Widen(p.previousDeploymentId),rx+18,y+360,inspectorW-36,18,tinyFmt_.Get(),brush_.muted.Get());
        } else {
            Text(L"Select a deployment package on the left. Packages pin the candidate model, foundation, persona adapter, and evaluation run as one auditable unit.",
                rx+18,y+62,inspectorW-36,86,smallFmt_.Get(),brush_.muted.Get());
            TextLine(deploymentRegistry_.HasActiveLockedDeployment()?L"Production runtime: LOCKED":L"Production runtime: no active lock",
                rx+18,y+174,inspectorW-36,28,bodyFmt_.Get(),
                deploymentRegistry_.HasActiveLockedDeployment()?brush_.yellow.Get():brush_.cyan.Get());
        }
    }

    void DrawModelLabWorkspacePlaceholder(float x,float y,float contentW,const std::wstring& title,const std::wstring& sub) {
        Rounded(x,y,contentW,382,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(title,x+20,y+14,contentW-40,34,h1Fmt_.Get(),brush_.text.Get());
        TextLine(sub,x+20,y+52,contentW-40,24,bodyFmt_.Get(),brush_.muted.Get());
        Rounded(x+20,y+94,contentW-40,78,brush_.sidebar.Get(),brush_.border.Get(),9);
        TextLine(L"Structured workspace reserved for SARA 1.0.18",x+38,y+104,contentW-76,24,smallFmt_.Get(),brush_.cyan.Get());
        Text(L"This section is now a first-class Model Lab destination and will use the shared foundation, persona, LoRA, review, evaluation, and deployment state.",
            x+38,y+132,contentW-76,34,tinyFmt_.Get(),brush_.muted.Get());
    }

    void DrawModelLab(float w,float h) {
        PageTitle(L"Model Lab / Trainer",L"Train, adapt, evaluate, and deploy SARA from one polished workspace");
        const float x=kSidebar+28.0f;
        const float y=kHeader+102.0f;
        const float contentW=w-x-28.0f;

        DrawModelLabTabs(x,y,contentW);
        DrawModelLabContext(x,y+44,contentW);
        const float bodyY=y+116;

        switch(modelLabSection_) {
            case ModelLabSection::Overview: DrawModelLabOverview(x,bodyY,contentW); break;
            case ModelLabSection::Train: DrawModelLabTrain(x,bodyY,contentW); break;
            case ModelLabSection::Datasets:
                DrawModelLabDatasets(x,bodyY,contentW); break;
            case ModelLabSection::Personas:
                DrawModelLabPersonas(x,bodyY,contentW); break;
            case ModelLabSection::FoundationForks:
                DrawModelLabFoundationForks(x,bodyY,contentW); break;
            case ModelLabSection::Jobs:
                DrawModelLabJobs(x,bodyY,contentW); break;
            case ModelLabSection::Evaluation:
                DrawModelLabEvaluation(x,bodyY,contentW); break;
            case ModelLabSection::Deployment:
                DrawModelLabDeployment(x,bodyY,contentW); break;
        }
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
                TextLine(Widen(msgs[i].text),qx+30,yy+4,queueW-60,38,smallFmt_.Get(),brush_.text.Get());
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

        Rounded(x,y+294,contentW,246,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Planned Server Responsibilities",x+18,y+306,320,30,h1Fmt_.Get(),brush_.text.Get());

        const wchar_t* items[]={
            L"Encrypted case and evidence synchronization",
            L"Central policy and model-profile distribution",
            L"Workstation registration and role administration",
            L"Multi-investigator coordination and redundant backup"
        };
        float iy=y+354;
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
            auto info=service.Check(url,"1.0.18");
            if(info.newer) {
                updateStatus_=L"Update available: "+Widen(info.version);
                statusText_=L"SARA update available";
            } else {
                updateStatus_=L"Current version 1.0.18 is up to date";
                statusText_=L"No SARA update available";
            }
        } catch(const std::exception& e) {
            updateStatus_=L"Update check failed: "+Widen(e.what());
            statusText_=L"Update check failed";
        }
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
        TextLine(L"SARA 1.0.18",rx+104,y+60,rightW-124,30,bodyFmt_.Get(),brush_.text.Get());

        TextLine(L"Build",rx+20,y+102,78,26,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"Development Release",rx+104,y+100,rightW-124,30,smallFmt_.Get(),brush_.muted.Get());

        AddButton(L"check_updates",L"Check for Updates",rx+20,y+146,170,38,false);
        TextLine(updateStatus_,rx+20,y+190,rightW-40,26,tinyFmt_.Get(),brush_.muted.Get());

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
        case WM_GETMINMAXINFO: {
            auto* info=reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize.x=1280;
            info->ptMinTrackSize.y=760;
            return 0;
        }
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
        case WM_MOUSEMOVE: if(g_app) g_app->Hover((float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp)); return 0;
        case WM_LBUTTONDOWN: if(g_app) g_app->Press((float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp)); return 0;
        case WM_RBUTTONUP: if(g_app) g_app->RightClick((float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp)); return 0;
        case WM_LBUTTONUP:
            if(g_app) {
                g_app->ReleasePress();
                g_app->Click((float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp));
            }
            return 0;
        case WM_DESTROY: delete g_app; g_app=nullptr; PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style=CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc=WndProc;
    wc.hInstance=instance;
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName=kClassName;
    wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd=CreateWindowExW(
        0,kClassName,L"SARA - Synthetic Adaptive Response Agent",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,1500,900,
        nullptr,nullptr,instance,nullptr);
    if (!hwnd) return 1;

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
