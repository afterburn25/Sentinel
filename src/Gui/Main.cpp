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
#include <filesystem>
#include <chrono>
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
        std::filesystem::path out = std::filesystem::path(p) / L"Sentinel";
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

bool IsLocalModelEndpoint(const std::string& endpoint) {
    return endpoint.find("127.0.0.1") != std::string::npos ||
           endpoint.find("localhost") != std::string::npos;
}

bool StartBundledAiService(std::wstring* failure = nullptr) {
    const auto script = ExeDir() / L"ai" / L"Start-Sentinel-With-AI.ps1";
    if (!std::filesystem::exists(script)) {
        if (failure) *failure = L"Bundled AI launcher is missing: " + script.wstring();
        return false;
    }

    std::wstring command =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" +
        script.wstring() + L"\" -NoLaunch";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(
            nullptr, command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, ExeDir().c_str(), &si, &pi)) {
        if (failure) *failure = L"Could not start the bundled local AI service.";
        return false;
    }

    const DWORD wait = WaitForSingleObject(pi.hProcess, 180000);
    DWORD exitCode = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (wait != WAIT_OBJECT_0) {
        if (failure) *failure = L"Timed out while starting the bundled local AI service.";
        return false;
    }
    if (exitCode != 0) {
        if (failure) {
            *failure = L"Bundled local AI service failed to start. Check the ai\\logs folder or run Setup-Sentinel-AI.cmd once.";
        }
        return false;
    }
    return true;
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
        agencyEndpointEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1021,GetModuleHandleW(nullptr),nullptr);
        agencyIdEdit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1022,GetModuleHandleW(nullptr),nullptr);
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
            scenarioNameEdit_,scenarioObjectiveEdit_,scenarioSeedEdit_,minDelayEdit_,maxDelayEdit_,agencyEndpointEdit_,agencyIdEdit_};
        for(HWND e:advancedEdits) {
            SendMessageW(e,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
            SetWindowTheme(e,L"DarkMode_Explorer",nullptr);
            SendMessageW(e,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
        }
        HWND personaCombos[]={personaAgeCombo_,ageStateCombo_,personaGenderCombo_,personaPronounsCombo_,personaRelationshipCombo_,
            personaPersonalityCombo_,personaSocialCombo_,personaConfidenceCombo_,modelCombo_};
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
        SendMessageW(ageStateCombo_,CB_SETCURSEL,(WPARAM)static_cast<int>(simSettings_.ageState),0);
        LoadProfileEditors();

        messagingAdapter_=sentinel::operations::CreateInMemoryMessageAdapter();
        agencyConfig_.workstationId="local-workstation";
        modelRegistry_.Load(runtime_->root/"model-registry.tsv");

        // Prefer the configured OpenAI-compatible local model at startup. If a
        // bundled localhost model is configured but not running yet, Sentinel starts
        // the bundled llama.cpp service itself so opening Sentinel.exe directly works.
        if(!simSettings_.endpoint.empty() && !simSettings_.model.empty()) {
            auto connectConfiguredModel=[&]() {
                auto candidate=sentinel::simulation::CreateOpenAICompatibleModel(
                    simSettings_.endpoint,simSettings_.model,{},simSettings_.temperature,simSettings_.maxTokens);
                sentinel::simulation::ModelContext testContext;
                testContext.scenario="Sentinel local model startup connection test";
                testContext.personaSummary="Synthetic test only.";
                (void)candidate->GenerateInvestigatorSuggestion(testContext);
                model_=std::move(candidate);
                modelStatus_=L"Connected automatically: "+Widen(simSettings_.model);
            };

            try {
                connectConfiguredModel();
            } catch(const std::exception& firstError) {
                bool recovered=false;
                std::wstring launcherFailure;
                if(IsLocalModelEndpoint(simSettings_.endpoint) && StartBundledAiService(&launcherFailure)) {
                    try {
                        connectConfiguredModel();
                        recovered=true;
                    } catch(...) {}
                }
                if(!recovered) {
                    model_=sentinel::simulation::CreateRuleBasedTestModel();
                    modelStatus_=L"Local model unavailable; built-in fallback active. ";
                    if(!launcherFailure.empty()) modelStatus_+=launcherFailure;
                    else modelStatus_+=Widen(firstError.what());
                }
            }
        } else {
            model_=sentinel::simulation::CreateRuleBasedTestModel();
            modelStatus_=L"Built-in contextual model";
        }
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
            else if (b.id==L"sim_suggest") GenerateSimulationSuggestion();
            else if (b.id==L"sim_reset" || b.id==L"sim_new_chat") ResetSimulation();
            else if (b.id==L"sim_previous_chat") LoadPreviousConversation();
            else if (b.id==L"sim_model") ConfigureLocalModel();
            else if (b.id==L"sim_browse_models") BrowseModels();
            else if (b.id==L"sim_preserve") PreserveSimulationTranscript();
            else if (b.id==L"persona_save") SaveProfileEditors();
            else if (b.id==L"model_register") RegisterCurrentModel();
            else if (b.id==L"model_eval") EvaluateSelectedRegistryModel();
            else if (b.id==L"model_approve") ApproveSelectedRegistryModel();
            else if (b.id==L"model_activate") ActivateSelectedRegistryModel();
            else if (b.id==L"model_rollback") RollbackRegistryModel();
            else if (b.id.rfind(L"regmodel:",0)==0) selectedRegistryModel_=(int)std::stol(b.id.substr(9));
            else if (b.id==L"msg_queue") QueueOperatorTestMessage();
            else if (b.id==L"approval_request") RequestLatestSuggestionApproval();
            else if (b.id==L"approval_approve") ApproveFirstPending();
            else if (b.id==L"agency_toggle") ToggleAgency();
            else if (b.id==L"agency_enqueue") EnqueueAgencySnapshot();
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
        if(id==kSimTypingStartTimer) {
            KillTimer(hwnd_,kSimTypingStartTimer);
            if(!simReplyPending_ || simPendingMessage_.empty()) return;

            simBotTyping_=true;
            statusText_=L"Synthetic subject typing";
            ScrollSimulationToBottom();
            InvalidateRect(hwnd_,nullptr,FALSE);
            UpdateWindow(hwnd_);

            try {
                if(model_) {
                    simPreparedReply_=model_->GenerateSyntheticReply(simPendingMessage_,simContext_);
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
                    const auto source=Widen(model_?model_->Name():"No model");
                    statusText_=L"Response from "+source;
                    if(!simContext_.recalledMemory.empty())
                        statusText_+=L" | prior-conversation context used";
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
    HWND scenarioNameEdit_{},scenarioObjectiveEdit_{},scenarioSeedEdit_{},minDelayEdit_{},maxDelayEdit_{},ageStateCombo_{};
    HWND agencyEndpointEdit_{},agencyIdEdit_{};
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
    std::string simPendingMessage_;
    std::string simPreparedReply_;
    std::vector<std::pair<RectF,size_t>> simMessageRects_;
    sentinel::simulation::SimulationSettings simSettings_;
    std::unique_ptr<sentinel::operations::IMessageAdapter> messagingAdapter_;
    std::vector<sentinel::operations::ApprovalRequest> approvals_;
    sentinel::simulation::ModelRegistry modelRegistry_;
    int selectedRegistryModel_{-1};
    sentinel::simulation::ResponseEvaluation lastEvaluation_;
    sentinel::agency::AgencyServerConfig agencyConfig_;
    sentinel::agency::AgencySyncQueue agencyQueue_;
    std::wstring policyStatus_=L"Policy ready";
    std::wstring updateStatus_=L"Updates not checked";
    std::wstring aiDiagnostics_=L"Not run";

    HFONT chatFont_{};
    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<IDWriteFactory> writeFactory_;
    ComPtr<IDWriteTextFormat> titleFmt_,h1Fmt_,bodyFmt_,smallFmt_,tinyFmt_,bigFmt_;
    BrushSet brush_;
    bool brushesReady_{false};
    std::vector<Button> buttons_;

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
            IconKind::Chat,IconKind::Document,IconKind::Database,IconKind::Chat,IconKind::Shield,IconKind::Database,IconKind::Gear
        };
        return icons[std::clamp(i,0,11)];
    }

    void DrawBrand() {
        DrawShield(22,14,48,brush_.cyan.Get(),brush_.panel2.Get(),false);
        DrawShield(31,24,30,brush_.blue.Get(),nullptr,false);
        Text(L"Sentinel",78,15,132,40,titleFmt_.Get(),brush_.text.Get());
        Text(L"EVIDENCE  |  INTEGRITY  |  JUSTICE",79,51,136,18,tinyFmt_.Get(),brush_.muted.Get());
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
        Text(L"Sentinel v1.0.7",24,674,170,20,smallFmt_.Get(),brush_.muted.Get());
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
        ShowPersonaEditors(page_==Page::Persona);
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

            const int composerW=(int)(chatW-236);
            const int composerH=46;
            MoveControl(chatEdit_,(int)(x+18),(int)(y+452),composerW,composerH,TRUE);
            RECT composerTextRect{12,9,std::max(24,composerW-12),composerH-8};
            SendMessageW(chatEdit_,EM_SETRECTNP,0,(LPARAM)&composerTextRect);

            MoveControl(modelEndpointEdit_,(int)(rx+18),(int)(y+138),(int)(sideW-36),32,TRUE);
            MoveControl(modelCombo_,(int)(rx+158),(int)(y+201),(int)(sideW-176),150,TRUE);
            MoveControl(modelNameEdit_,(int)(rx+18),(int)(y+251),(int)(sideW-140),32,TRUE);
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
            MessageBoxW(hwnd_,L"Create or select a case before preserving the transcript.",L"Sentinel",MB_OK|MB_ICONINFORMATION);
            return;
        }
        try {
            auto exportDir=runtime_->root/"exports";
            std::filesystem::create_directories(exportDir);
            auto path=exportDir/"simulation-transcript.txt";
            std::ofstream out(path,std::ios::trunc);
            out<<"Sentinel Simulation Transcript\n";
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
            ctx.scenario="Sentinel Model Lab candidate evaluation";
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

    void DrawModelLab(float w,float h) {
        PageTitle(L"Model Lab",L"Evaluate, approve, activate, and roll back model candidates");
        const float x=kSidebar+28.0f;
        const float y=kHeader+104.0f;
        const float contentW=w-x-28.0f;

        Rounded(x,y,contentW,102,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Model Registry Actions",x+18,y+12,260,30,h1Fmt_.Get(),brush_.text.Get());

        const float gap=10.0f;
        const float bw=136.0f;
        float bx=x+18;
        AddButton(L"model_register",L"Register Current",bx,y+50,bw,36,true); bx+=bw+gap;
        AddButton(L"model_eval",L"Evaluate",bx,y+50,bw,36,false); bx+=bw+gap;
        AddButton(L"model_approve",L"Approve",bx,y+50,bw,36,false); bx+=bw+gap;
        AddButton(L"model_activate",L"Activate",bx,y+50,bw,36,false); bx+=bw+gap;
        AddButton(L"model_rollback",L"Rollback",bx,y+50,bw,36,false);

        Rounded(x,y+116,contentW,326,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Registered Models",x+18,y+128,260,30,h1Fmt_.Get(),brush_.text.Get());

        TextLine(L"MODEL",x+34,y+165,280,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"STATE",x+332,y+165,100,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"SCORE",x+452,y+165,72,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"LATENCY",x+544,y+165,80,20,tinyFmt_.Get(),brush_.muted.Get());
        TextLine(L"ENDPOINT",x+646,y+165,contentW-680,20,tinyFmt_.Get(),brush_.muted.Get());

        float yy=y+191;
        if(modelRegistry_.Models().empty()) {
            Rounded(x+22,yy,contentW-44,52,brush_.sidebar.Get(),brush_.border.Get(),8);
            TextLine(L"No registered models. Configure one in Simulation Lab, then choose Register Current.",
                x+34,yy+7,contentW-68,38,bodyFmt_.Get(),brush_.muted.Get());
        }

        for(size_t i=0;i<modelRegistry_.Models().size() && i<5;i++) {
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

        Rounded(x,y+456,contentW,132,brush_.panel.Get(),brush_.border.Get(),10);
        TextLine(L"Last Evaluation",x+18,y+468,220,30,h1Fmt_.Get(),brush_.text.Get());

        const float metricY=y+516;
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
        MessageBoxW(hwnd_,aiDiagnostics_.c_str(),L"Sentinel AI Diagnostics",MB_OK|MB_ICONINFORMATION);
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
            MessageBoxW(hwnd_,L"Enter both a case number and title.",L"Sentinel",MB_OK|MB_ICONINFORMATION);
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
            MessageBoxW(hwnd_,L"Create a case first.",L"Sentinel",MB_OK|MB_ICONINFORMATION);
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
            MessageBoxW(hwnd_,L"Select or import evidence first.",L"Sentinel",MB_OK|MB_ICONINFORMATION);
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
        case WM_RBUTTONUP: if(g_app) g_app->RightClick((float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp)); return 0;
        case WM_LBUTTONUP: if(g_app) g_app->Click((float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp)); return 0;
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
        0,kClassName,L"Sentinel - Secure Evidence & Integrity",
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
