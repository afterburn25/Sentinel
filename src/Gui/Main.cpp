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
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClassName[] = L"SentinelNativeWindow";
constexpr int kSidebar = 220;
constexpr int kHeader = 78;
constexpr UINT_PTR kSimReplyTimer = 4101;
constexpr int kSimVisibleRows = 6;

enum class Page { Dashboard, Cases, Evidence, Audit, Verification, Simulation, Settings };
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
    sentinel::KeyManager keys;
    sentinel::SqliteCaseRepository caseRepo;
    sentinel::CaseService cases;
    sentinel::AuditService audit;

    Runtime()
        : root(AppDataRoot()),
          cipher(random),
          migrations(db),
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
    App() : runtime_(std::make_unique<Runtime>()) {}

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
        chatEdit_ = CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL,
            0,0,0,0,hwnd_,(HMENU)1003,GetModuleHandleW(nullptr),nullptr);
        modelEndpointEdit_ = CreateWindowExW(0,L"EDIT",L"http://127.0.0.1:1234/v1/chat/completions",
            WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1004,GetModuleHandleW(nullptr),nullptr);
        modelNameEdit_ = CreateWindowExW(0,L"EDIT",L"local-model",
            WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,hwnd_,(HMENU)1005,GetModuleHandleW(nullptr),nullptr);
        modelCombo_ = CreateWindowExW(0,L"COMBOBOX",L"",
            WS_CHILD|WS_VSCROLL|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,(HMENU)1007,GetModuleHandleW(nullptr),nullptr);
        simScroll_ = CreateWindowExW(0,L"SCROLLBAR",L"",WS_CHILD|SBS_VERT,
            0,0,0,0,hwnd_,(HMENU)1006,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(caseNumberEdit_,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        SendMessageW(caseTitleEdit_,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        SendMessageW(chatEdit_,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        SendMessageW(modelEndpointEdit_,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        SendMessageW(modelNameEdit_,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        SendMessageW(modelCombo_,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
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
        model_=sentinel::simulation::CreateRuleBasedTestModel();
        modelStatus_=L"Built-in test model";
        ResetSimulation();
        ShowCaseEditors(false);
        ShowChatEditor(false);
        return S_OK;
    }

    void Resize() {
        if (!target_) return;
        RECT rc{}; GetClientRect(hwnd_,&rc);
        target_->Resize(D2D1::SizeU(rc.right,rc.bottom));
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
            case Page::Settings: DrawSettings(w,h); break;
        }

        HRESULT hr=target_->EndDraw();
        if (hr==D2DERR_RECREATE_TARGET) { target_.Reset(); brushesReady_=false; }
    }

    void Click(float x,float y) {
        if (x<kSidebar && y>kHeader) {
            int idx=(int)((y-kHeader-24)/60);
            if (idx>=0&&idx<7) {
                page_=(Page)idx;
                ShowCaseEditors(page_==Page::Cases);
                ShowChatEditor(page_==Page::Simulation);
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
            else if (b.id==L"sim_reset") ResetSimulation();
            else if (b.id==L"sim_model") ConfigureLocalModel();
            else if (b.id==L"sim_browse_models") BrowseModels();
            else if (b.id.rfind(L"copy:",0)==0) CopySimulationMessage((size_t)std::stoul(b.id.substr(5)));
            else if (b.id.rfind(L"case:",0)==0) SelectCase((size_t)std::stoul(b.id.substr(5)));
            else if (b.id.rfind(L"ev:",0)==0) SelectEvidence((size_t)std::stoul(b.id.substr(3)));
            InvalidateRect(hwnd_,nullptr,FALSE);
            return;
        }
    }

    HBRUSH EditBrush() {
        static HBRUSH brush = CreateSolidBrush(RGB(15,32,48));
        return brush;
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
        if(id!=kSimReplyTimer) return;
        KillTimer(hwnd_,kSimReplyTimer);
        if(!simBotTyping_) return;
        try {
            if(model_) {
                auto reply=model_->GenerateSyntheticReply(simPendingMessage_,simContext_);
                simContext_.history.push_back({sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,reply});
                statusText_=L"Model response received";
            }
        } catch(const std::exception& e) {
            simContext_.history.push_back({sentinel::simulation::ChatTurn::Speaker::ModelSuggestion,
                std::string("Model error: ")+e.what()});
            statusText_=L"Model request failed";
        }
        simBotTyping_=false;
        simPendingMessage_.clear();
        ScrollSimulationToBottom();
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

private:
    struct Button { RectF rect; std::wstring id; };

    HWND hwnd_{},caseNumberEdit_{},caseTitleEdit_{},chatEdit_{},modelEndpointEdit_{},modelNameEdit_{},modelCombo_{},simScroll_{};
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
    int simFirstVisible_{0};
    bool simBotTyping_{false};
    std::string simPendingMessage_;
    std::vector<std::pair<RectF,size_t>> simMessageRects_;

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

    void Rounded(float x,float y,float w,float h,ID2D1Brush* fill,ID2D1Brush* stroke=nullptr,float radius=8) {
        auto rr=D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),radius,radius);
        target_->FillRoundedRectangle(rr,fill);
        if (stroke) target_->DrawRoundedRectangle(rr,stroke,1);
    }

    void Badge(const std::wstring& s,float x,float y,ID2D1Brush* color,float width=76) {
        Rounded(x,y,width,24,brush_.panel2.Get(),color,12);
        Text(s,x+10,y+3,width-18,20,smallFmt_.Get(),color);
    }

    void AddButton(const std::wstring& id,const std::wstring& label,float x,float y,float w,float h,bool primary=false) {
        Rounded(x,y,w,h,primary?brush_.blue.Get():brush_.panel2.Get(),primary?brush_.cyan.Get():brush_.border.Get(),7);
        Text(label,x+12,y+(h-22)/2,w-24,24,bodyFmt_.Get(),brush_.text.Get());
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
        static const IconKind icons[]={IconKind::Home,IconKind::Folder,IconKind::Database,IconKind::Document,IconKind::Shield,IconKind::Chat,IconKind::Gear};
        return icons[std::clamp(i,0,6)];
    }

    void DrawBrand() {
        DrawShield(22,14,48,brush_.cyan.Get(),brush_.panel2.Get(),false);
        DrawShield(31,24,30,brush_.blue.Get(),nullptr,false);
        Text(L"Sentinel",78,15,132,40,titleFmt_.Get(),brush_.text.Get());
        Text(L"EVIDENCE  |  INTEGRITY  |  JUSTICE",79,51,136,18,tinyFmt_.Get(),brush_.muted.Get());
    }

    void DrawSidebar() {
        static const wchar_t* names[]={L"Dashboard",L"Cases",L"Evidence",L"Audit Log",L"Verification",L"Simulation Lab",L"Settings"};
        for (int i=0;i<7;i++) {
            float y=(float)kHeader+24+i*60;
            if ((int)page_==i) {
                target_->FillRectangle(D2D1::RectF(0,y-7,(float)kSidebar,y+45),brush_.panel2.Get());
                target_->FillRectangle(D2D1::RectF(0,y-7,4,y+45),brush_.cyan.Get());
                Rounded(20,y-1,40,36,brush_.sidebar.Get(),brush_.border.Get(),8);
            }
            DrawIcon(NavIcon(i),27,y+3,26,((int)page_==i)?brush_.cyan.Get():brush_.muted.Get());
            Text(names[i],70,y+5,130,28,bodyFmt_.Get(),((int)page_==i)?brush_.cyan.Get():brush_.text.Get());
        }
        Text(L"Sentinel v0.4.3 GUI Alpha",24,760,170,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"Secure Local Mode",24,782,170,20,smallFmt_.Get(),brush_.green.Get());
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
        Text(title,kSidebar+28,kHeader+22,360,40,titleFmt_.Get(),brush_.text.Get());
        Text(sub,kSidebar+30,kHeader+57,650,22,bodyFmt_.Get(),brush_.muted.Get());
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
        MoveWindow(caseNumberEdit_,(int)x,(int)y,160,34,TRUE);
        MoveWindow(caseTitleEdit_,(int)(x+180),(int)y,300,34,TRUE);
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
        PageTitle(L"Simulation Lab",L"Safe synthetic conversation testing and model evaluation");
        float x=kSidebar+28,y=kHeader+102;
        float right=350.0f;
        float chatW=w-x-right-44;

        Rounded(x,y,chatW,520,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Synthetic Conversation",x+18,y+14,260,28,h1Fmt_.Get(),brush_.text.Get());
        Badge(L"SIMULATION",x+chatW-116,y+16,brush_.cyan.Get(),96);

        const float transcriptTop=y+58;
        const float transcriptBottom=y+432;
        const int total=(int)simContext_.history.size();
        const int maxStart=std::max(0,total-kSimVisibleRows);
        simFirstVisible_=std::clamp(simFirstVisible_,0,maxStart);
        const int end=std::min(total,simFirstVisible_+kSimVisibleRows);
        const int visible=std::max(0,end-simFirstVisible_);
        simMessageRects_.clear();
        float yy=transcriptBottom-visible*58.0f;
        for(int i=simFirstVisible_;i<end;++i) {
            const auto& turn=simContext_.history[(size_t)i];
            bool investigator=turn.speaker==sentinel::simulation::ChatTurn::Speaker::Investigator;
            bool suggestion=turn.speaker==sentinel::simulation::ChatTurn::Speaker::ModelSuggestion;
            float bubbleW=std::min(chatW-112.0f,560.0f);
            float bx=investigator?x+chatW-bubbleW-34:x+20;
            ID2D1Brush* fill=investigator?brush_.panel2.Get():brush_.sidebar.Get();
            ID2D1Brush* border=suggestion?brush_.yellow.Get():(investigator?brush_.blue.Get():brush_.border.Get());
            Rounded(bx,yy,bubbleW,48,fill,border,9);
            Text(investigator?L"Investigator":suggestion?L"Model / System":L"Synthetic Subject",bx+12,yy+5,bubbleW-72,15,tinyFmt_.Get(),
                suggestion?brush_.yellow.Get():(investigator?brush_.cyan.Get():brush_.green.Get()));
            Text(L"Copy",bx+bubbleW-46,yy+5,34,15,tinyFmt_.Get(),brush_.muted.Get());
            buttons_.push_back({{bx+bubbleW-52,yy+2,bx+bubbleW-8,yy+18},L"copy:"+std::to_wstring(i)});
            Text(Widen(turn.text),bx+12,yy+20,bubbleW-24,24,smallFmt_.Get(),brush_.text.Get());
            simMessageRects_.push_back({{bx,yy,bx+bubbleW,yy+48},(size_t)i});
            yy+=58;
        }
        if(simBotTyping_) {
            Rounded(x+20,transcriptBottom-42,190,32,brush_.sidebar.Get(),brush_.border.Get(),16);
            StatusDot(x+38,transcriptBottom-26,3,brush_.green.Get());
            Text(L"Synthetic subject is typing...",x+50,transcriptBottom-35,150,20,tinyFmt_.Get(),brush_.muted.Get());
        }

        MoveWindow(simScroll_,(int)(x+chatW-20),(int)transcriptTop,14,(int)(transcriptBottom-transcriptTop),TRUE);
        UpdateSimulationScrollbar();

        Rounded(x+18,y+448,chatW-240,48,brush_.sidebar.Get(),brush_.border.Get(),10);
        MoveWindow(chatEdit_,(int)(x+30),(int)(y+465),(int)(chatW-264),22,TRUE);
        AddButton(L"sim_send",L"Send",x+chatW-210,y+452,190,40,true);

        float rx=x+chatW+14;
        Rounded(rx,y,right,318,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Scenario / Model",rx+18,y+14,right-36,28,h1Fmt_.Get(),brush_.text.Get());
        Text(L"Mode",rx+18,y+56,86,18,tinyFmt_.Get(),brush_.muted.Get());
        Text(L"Synthetic only",rx+110,y+54,210,20,bodyFmt_.Get(),brush_.green.Get());
        Text(L"Persona",rx+18,y+86,86,18,tinyFmt_.Get(),brush_.muted.Get());
        Text(L"Alex - fictional test profile",rx+110,y+84,210,20,smallFmt_.Get(),brush_.text.Get());
        Text(L"Policy",rx+18,y+116,86,18,tinyFmt_.Get(),brush_.muted.Get());
        StatusDot(rx+115,y+125,4,brush_.green.Get());
        Text(L"Simulation-safe",rx+126,y+115,170,20,smallFmt_.Get(),brush_.green.Get());

        Text(L"OpenAI-compatible endpoint",rx+18,y+148,right-36,18,tinyFmt_.Get(),brush_.muted.Get());
        MoveWindow(modelEndpointEdit_,(int)(rx+18),(int)(y+168),(int)(right-36),32,TRUE);

        AddButton(L"sim_browse_models",L"Browse Models",rx+18,y+208,118,32,false);
        Text(L"Available models",rx+148,y+207,110,18,tinyFmt_.Get(),brush_.muted.Get());
        MoveWindow(modelCombo_,(int)(rx+148),(int)(y+226),(int)(right-166),140,TRUE);

        Text(L"Manual model",rx+18,y+250,100,18,tinyFmt_.Get(),brush_.muted.Get());
        MoveWindow(modelNameEdit_,(int)(rx+18),(int)(y+269),(int)(right-142),32,TRUE);
        AddButton(L"sim_model",L"Connect",rx+right-114,y+269,96,32,true);

        StatusDot(rx+24,y+315,4,modelStatus_.find(L"Connected")!=std::wstring::npos?brush_.green.Get():brush_.yellow.Get());
        Text(modelStatus_,rx+36,y+304,right-54,34,tinyFmt_.Get(),brush_.text.Get());

        Rounded(rx,y+346,right,174,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Model Suggestion",rx+18,y+360,right-36,28,h1Fmt_.Get(),brush_.text.Get());
        Rounded(rx+18,y+400,right-36,60,brush_.sidebar.Get(),brush_.border.Get(),8);
        Text(simSuggestion_,rx+28,y+410,right-56,42,tinyFmt_.Get(),brush_.text.Get());
        AddButton(L"sim_suggest",L"Generate",rx+18,y+472,118,32,false);
        AddButton(L"sim_reset",L"Reset",rx+146,y+472,92,32,false);
        Text(L"Responses are delayed in Simulation Lab to mimic natural pacing.",rx+18,y+505,right-36,14,tinyFmt_.Get(),brush_.muted.Get());
    }

    void ShowChatEditor(bool show) {
        if(chatEdit_) ShowWindow(chatEdit_,show?SW_SHOW:SW_HIDE);
        if(modelEndpointEdit_) ShowWindow(modelEndpointEdit_,show?SW_SHOW:SW_HIDE);
        if(modelNameEdit_) ShowWindow(modelNameEdit_,show?SW_SHOW:SW_HIDE);
        if(modelCombo_) ShowWindow(modelCombo_,show?SW_SHOW:SW_HIDE);
        if(simScroll_) ShowWindow(simScroll_,show?SW_SHOW:SW_HIDE);
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

    void ResetSimulation() {
        simContext_.scenario="Neutral synthetic conversation test";
        simContext_.personaSummary="Alex is a fictional synthetic test persona.";
        simContext_.history.clear();
        simContext_.history.push_back({sentinel::simulation::ChatTurn::Speaker::SyntheticSubject,
            "Simulation ready. Send a test message to begin."});
        simSuggestion_=L"No suggestion generated yet";
        simBotTyping_=false;
        simPendingMessage_.clear();
        KillTimer(hwnd_,kSimReplyTimer);
        if(chatEdit_) SetWindowTextW(chatEdit_,L"");
        ScrollSimulationToBottom();
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void SendSimulationMessage() {
        if(simBotTyping_) return;
        wchar_t buffer[2048]{};
        GetWindowTextW(chatEdit_,buffer,2048);
        std::wstring message=buffer;
        while(!message.empty() && (message.back()==L'\r' || message.back()==L'\n' || message.back()==L' ')) message.pop_back();
        if(message.empty()) return;
        auto utf8=Narrow(message);
        simContext_.history.push_back({sentinel::simulation::ChatTurn::Speaker::Investigator,utf8});
        SetWindowTextW(chatEdit_,L"");
        simSuggestion_=L"No suggestion generated yet";
        simPendingMessage_=utf8;
        simBotTyping_=true;
        ScrollSimulationToBottom();
        int delay=std::clamp(2200+(int)utf8.size()*58,3000,8500);
        SetTimer(hwnd_,kSimReplyTimer,(UINT)delay,nullptr);
        statusText_=L"Synthetic subject typing";
        SetFocus(chatEdit_);
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
            simSuggestion_=Widen(suggestion);
            statusText_=L"Test suggestion generated";
        } catch(const std::exception& e) {
            simSuggestion_=L"Model error: "+Widen(e.what());
            statusText_=L"Model request failed";
        }
    }

    static LRESULT CALLBACK ChatEditSubclassProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref) {
        auto* app=reinterpret_cast<App*>(ref);
        if(msg==WM_KEYDOWN && wp==VK_RETURN) {
            if(app) app->SendSimulationMessage();
            return 0;
        }
        return DefSubclassProc(hwnd,msg,wp,lp);
    }

    void DrawSettings(float w,float h) {
        PageTitle(L"Settings",L"Local secure-store and application configuration");
        float x=kSidebar+28,y=kHeader+110;
        Rounded(x,y,w-x-28,150,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Secure Local Store",x+18,y+16,300,28,h1Fmt_.Get(),brush_.text.Get());
        Text(L"Location",x+22,y+60,100,20,smallFmt_.Get(),brush_.muted.Get());
        Text(runtime_->root.wstring(),x+130,y+58,w-x-180,24,bodyFmt_.Get(),brush_.text.Get());
        Text(L"Encryption",x+22,y+94,100,20,smallFmt_.Get(),brush_.muted.Get());
        Text(L"AES-256-GCM | DPAPI protected workstation master key",x+130,y+92,w-x-180,24,bodyFmt_.Get(),brush_.green.Get());

        Rounded(x,y+168,w-x-28,145,brush_.panel.Get(),brush_.border.Get(),8);
        Text(L"Application",x+18,y+184,300,28,h1Fmt_.Get(),brush_.text.Get());
        Text(L"Sentinel 0.4.3 Native GUI Alpha",x+22,y+230,400,24,bodyFmt_.Get(),brush_.text.Get());
        Text(L"Offline-first. No agency server configured.",x+22,y+264,430,24,bodyFmt_.Get(),brush_.muted.Get());
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
        case WM_CTLCOLOREDIT: {
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
