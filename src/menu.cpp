// menu.cpp - ARO's in-game menu (Dear ImGui on the game's D3D11 swap chain).
// F5 opens/closes it. Works under Proton/DXVK: we hook IDXGISwapChain::Present and
// ResizeBuffers through the swap chain vtable (shared by every swap chain of that class).
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "core.h"
#include "cars.h"
#include "races.h"
#include "opponents.h"
#include "input.h"
namespace lang { bool load(const char* file); bool get(unsigned id, char* out, int outSize); }

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

typedef HRESULT(__stdcall* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(__stdcall* ResizeFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
static PresentFn oPresent;
static ResizeFn oResize;
static HWND g_hwnd;
static WNDPROC oWndProc;
static ID3D11Device* g_dev;
static ID3D11DeviceContext* g_ctx;
static ID3D11RenderTargetView* g_rtv;
static bool g_init = false, g_failed = false;
static volatile bool g_open = false;
static DWORD g_startTick;

#define logf core::log

static bool isInputMsg(UINT m) {
    return (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || (m >= WM_KEYFIRST && m <= WM_KEYLAST) || m == WM_INPUT;
}

static volatile bool g_justOpened = false, g_releaseGuard = false;
static void setOpen(bool open) {
    if (open == g_open) return;
    g_open = open;
    if (open) g_justOpened = true; else g_releaseGuard = true;   // keep the game blind until keys/buttons are let go
    if (g_init) ImGui::GetIO().MouseDrawCursor = open;
}

static LRESULT CALLBACK WndProcHook(HWND h, UINT m, WPARAM w, LPARAM l) {
    if ((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && (int)w == core::cfg.menuKey && !(l & (1 << 30))) { setOpen(!g_open); return 0; }
    if (g_open && g_init) {
        ImGui_ImplWin32_WndProcHandler(h, m, w, l);
        if (isInputMsg(m)) return 0;   // keep menu clicks/keys away from the game while it is open
    }
    return CallWindowProcW(oWndProc, h, m, w, l);
}

static void createRTV(IDXGISwapChain* sc) {
    ID3D11Texture2D* bb = nullptr;
    if (SUCCEEDED(sc->GetBuffer(0, IID_ID3D11Texture2D, (void**)&bb)) && bb) {
        g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
        bb->Release();
    }
}

static void initImGui(IDXGISwapChain* sc) {
    if (FAILED(sc->GetDevice(IID_ID3D11Device, (void**)&g_dev)) || !g_dev) { g_failed = true; logf("[overlay] no D3D11 device"); return; }
    g_dev->GetImmediateContext(&g_ctx);
    DXGI_SWAP_CHAIN_DESC sd; sc->GetDesc(&sd);
    g_hwnd = sd.OutputWindow;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImFont* f = nullptr;
    const char* fonts[] = { "C:\\windows\\Fonts\\arial.ttf", "C:\\windows\\Fonts\\tahoma.ttf", "C:\\windows\\Fonts\\segoeui.ttf" };
    for (const char* p : fonts) {
        if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) { f = io.Fonts->AddFontFromFileTTF(p, 20.0f); if (f) break; }
    }
    if (!f) io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.FontSizeBase = 20.0f; st.WindowRounding = 6; st.FrameRounding = 4; st.Colors[ImGuiCol_WindowBg].w = 0.94f;
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    oWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
    createRTV(sc);
    lang::load("0001.BNDL");
    g_init = true;
    logf("[overlay] ready on %ux%u window - press %s in game", sd.BufferDesc.Width, sd.BufferDesc.Height, core::cfg.menuKeyName);
}

// ---------------- menu ----------------

static races::Race g_ev[256];
static int g_order[256];
static char g_filter[64];
static int g_show = 0;       // 0 all, 1 my car's races, 2 my class's races
static int g_typeSel = 0;    // 0 all, else index into g_types
static int g_sortMode = 0;   // 0 distance, 1 name

struct NameCache { int evId; char name[64]; char type[32]; };
static NameCache g_nc[256]; static int g_nnc = 0;
static const char* g_types[] = { "All types", "Sprint", "Circuit", "Speed Run", "Ambush", "Drift Attack", "Smash And Grab", "Most Wanted", "Other" };
static const int kTypeCount = sizeof g_types / sizeof g_types[0];

static void titleCase(char* s) {
    bool start = true;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 'A' && c <= 'Z' && !start) *s = (char)(c - 'A' + 'a');
        start = !(isalnum(c) || c == '\'' || c >= 0x80);
    }
}

static int typeIndex(const char* t) {
    if (!strcmp(t, "Most Wanted")) return 7;
    for (int i = 1; i <= 6; i++) if (strstr(t, g_types[i])) return i;
    if (strstr(t, "Speed")) return 3;
    return kTypeCount - 1;
}

static const char* carName(int id) { const cars::Car* c = cars::find(id); return c ? c->name : nullptr; }
static int carClass(int id) { const cars::Car* c = cars::find(id); return c ? c->cls : -1; }
// "BEAT THE LEXUS LFA" names a car: first two words of a racer car's name
static bool namesCar(const char* t) {
    int n; const cars::Car* all = cars::all(&n);
    for (int i = 0; i < n; i++) {
        char want[64]; int k = 0, words = 0;
        for (const char* p = all[i].name; *p && k < 63; p++) { if (*p == ' ' && ++words == 2) break; want[k++] = (char)toupper((unsigned char)*p); }
        want[k] = 0;
        if (words >= 1 && !_strnicmp(t, want, k) && (t[k] == ' ' || !t[k])) return true;
    }
    return false;
}
static const NameCache& names(const races::Race& e) {
    for (int i = 0; i < g_nnc; i++) if (g_nc[i].evId == e.evId) return g_nc[i];
    NameCache& c = g_nc[g_nnc < 256 ? g_nnc++ : 256 - 1];
    c.evId = e.evId;
    if (!lang::get((unsigned)e.nameId, c.name, sizeof c.name)) snprintf(c.name, sizeof c.name, "Event %d", e.evId);
    titleCase(c.name);
    char obj[96] = "";
    if (e.mode == 955569) snprintf(c.type, sizeof c.type, "Speed Run");
    else if (e.mode == 390185) snprintf(c.type, sizeof c.type, "Ambush");
    else if (lang::get((unsigned)e.goalId, obj, sizeof obj) && !strncmp(obj, "BEAT THE ", 9) && namesCar(obj + 9)) snprintf(c.type, sizeof c.type, "Most Wanted");
    else if (lang::get((unsigned)e.typeId, c.type, sizeof c.type)) {
        titleCase(c.type);
        char* r = strstr(c.type, " Race"); if (r && !r[5]) *r = 0;   // "Sprint Race" -> "Sprint"
    } else snprintf(c.type, sizeof c.type, "-");
    return c;
}

static bool icontains(const char* hay, const char* nd) {
    if (!*nd) return true;
    for (const char* h = hay; *h; h++) {
        const char *a = h, *b = nd;
        while (*a && *b && (tolower((unsigned char)*a) == tolower((unsigned char)*b))) { a++; b++; }
        if (!*b) return true;
    }
    return false;
}

static void carsText(const races::Race& e, char* out, int n) {
    out[0] = 0; int used = 0;
    for (int i = 0; i < e.nCars; i++) {
        const char* nm = carName(e.cars[i]);
        used += snprintf(out + used, n - used, "%s%s", i ? ", " : "", nm ? nm : "?");
        if (used >= n) break;
    }
}

static bool forClass(const races::Race& e, int cls) {
    for (int i = 0; i < e.nCars; i++) if (carClass(e.cars[i]) == cls) return true;
    return false;
}

static void DrawMenu() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.55f, io.DisplaySize.y * 0.82f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (g_justOpened) ImGui::SetNextWindowFocus();
    char title[96]; snprintf(title, sizeof title, "ARO - All Races Open  (%s or L3+R3 to close)###aro", core::cfg.menuKeyName);
    if (!ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    if (g_justOpened) { g_justOpened = false; ImGui::SetNavCursorVisible(true); }
    // Esc / gamepad B closes the menu when nothing else (a dropdown, the search box) wants it
    bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!popup && !ImGui::IsAnyItemActive() &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))) setOpen(false);

    int car = races::playerCar();
    const char* myName = carName(car);
    int carCls = carClass(car);
    int pw = cars::powerToWeight(car);
    ImGui::TextDisabled("Keyboard: arrows/Tab move, Enter/Space select, Esc close   Gamepad: D-pad move, A select, B close, L3+R3 open/close");
    ImGui::Text("Your car: %s", myName ? myName : "unknown");
    ImGui::SameLine(); ImGui::TextDisabled("%s%s", carCls >= 0 ? cars::className(carCls) : "", pw ? "" : "");
    if (pw) { ImGui::SameLine(); ImGui::TextDisabled("· about %d hp per tonne", pw); }

    // Opponents
    ImGui::SeparatorText("Opponents");
    int oc = opponents::mode();
    char same[64]; snprintf(same, sizeof same, "Same class as my car (%s)", carCls >= 0 ? cars::className(carCls) : "?");
    const char* rw = "Real-world match (similar power-to-weight)";
    const char* cur = oc == -1 ? same : oc == -2 ? rw : cars::className(oc);
    ImGui::SetNextItemWidth(420);
    if (ImGui::BeginCombo("##opp", cur)) {
        if (ImGui::Selectable(same, oc == -1)) opponents::setMode(-1);
        if (ImGui::Selectable(rw, oc == -2)) opponents::setMode(-2);
        ImGui::Separator();
        for (int c = 0; c < cars::ClassCount; c++) {
            char lbl[64]; snprintf(lbl, sizeof lbl, "%s class", cars::className(c));
            if (ImGui::Selectable(lbl, oc == c)) opponents::setMode(c);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Applies to the next race you start.\nReal-world match picks rivals whose real power-to-weight is closest to your car's.\nMost Wanted races keep their boss car.");

    // Pins
    ImGui::SeparatorText("Pinned to EasyDrive > Races");
    int pins[races::kMaxPins]; int np = races::pins(pins);
    int n = races::list(g_ev, 256);
    if (!np) ImGui::TextDisabled("Tick races below (up to %d). They appear first in EasyDrive > Races.", races::kMaxPins);
    for (int i = 0; i < np; i++) {
        const char* nm = "?"; for (int k = 0; k < n; k++) if (g_ev[k].evId == pins[i]) nm = names(g_ev[k]).name;
        char lbl[96]; snprintf(lbl, sizeof lbl, "%s  x", nm);
        if (i) {                                   // wrap onto a new line when the tag doesn't fit
            float w = ImGui::CalcTextSize(lbl).x + ImGui::GetStyle().FramePadding.x * 2;
            float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            ImGui::SameLine();
            if (ImGui::GetCursorScreenPos().x + w > right) ImGui::NewLine();
        }
        ImGui::PushID(1000 + i);
        if (ImGui::SmallButton(lbl)) races::togglePin(pins[i]);
        ImGui::PopID();
    }
    if (np) {
        float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine();
        if (ImGui::GetCursorScreenPos().x + ImGui::CalcTextSize("Clear all").x + 12 > right) ImGui::NewLine();
        if (ImGui::SmallButton("Clear all")) races::clearPins();
    }

    // Races
    ImGui::SeparatorText("Races");
    ImGui::SetNextItemWidth(230);
    ImGui::InputTextWithHint("##filter", "Search...", g_filter, sizeof g_filter);
    ImGui::SameLine(); ImGui::SetNextItemWidth(190);
    char myClassLbl[48]; snprintf(myClassLbl, sizeof myClassLbl, "%s races", carCls >= 0 ? cars::className(carCls) : "My class");
    const char* shows[] = { "All races", "My car's races", myClassLbl };
    ImGui::Combo("##show", &g_show, shows, 3);
    ImGui::SameLine(); ImGui::SetNextItemWidth(150);
    ImGui::Combo("##type", &g_typeSel, g_types, kTypeCount);
    ImGui::SameLine(); ImGui::SetNextItemWidth(150);
    ImGui::Combo("##sort", &g_sortMode, "Nearest first\0By name\0");

    static bool logged = false;
    if (!logged && n > 0) {
        logged = true;
        for (int i = 0; i < n; i++) { const NameCache& nc = names(g_ev[i]); char b[200];
            char obj[96] = ""; lang::get((unsigned)g_ev[i].goalId, obj, sizeof obj);
            snprintf(b, sizeof b, "[races] %d | %s | %s | mode %d | goal: %s", g_ev[i].evId, nc.name, nc.type, g_ev[i].mode, obj); core::log("%s", b); }
    }
    int m = 0;
    for (int i = 0; i < n; i++) {
        const races::Race& e = g_ev[i];
        const NameCache& nc = names(e);
        if (g_show == 1 && !e.mine) continue;
        if (g_show == 2 && (carCls < 0 || !forClass(e, carCls))) continue;
        if (g_typeSel && typeIndex(nc.type) != g_typeSel) continue;
        if (!icontains(nc.name, g_filter) && !icontains(nc.type, g_filter)) continue;
        g_order[m++] = i;
    }
    auto less = [&](int x, int y) {
        const NameCache &a = names(g_ev[x]), &b = names(g_ev[y]);
        if (g_sortMode == 1) return _stricmp(a.name, b.name) < 0;
        float dx = g_ev[x].distKm < 0 ? 1e9f : g_ev[x].distKm, dy = g_ev[y].distKm < 0 ? 1e9f : g_ev[y].distKm;
        return dx < dy;
    };
    for (int a = 1; a < m; a++) { int v = g_order[a], b = a - 1; while (b >= 0 && less(v, g_order[b])) { g_order[b + 1] = g_order[b]; b--; } g_order[b + 1] = v; }
    ImGui::TextDisabled("%d of %d races  ·  orange = your car's own races  ·  hover a race to see its original cars", m, n);
    if (ImGui::BeginTable("races", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Pin", ImGuiTableColumnFlags_WidthFixed, 44);
        ImGui::TableSetupColumn("Race", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableHeadersRow();
        for (int r = 0; r < m; r++) {
            const races::Race& e = g_ev[g_order[r]];
            const NameCache& nc = names(e);
            ImGui::TableNextRow(); ImGui::PushID(e.evId);
            ImGui::TableSetColumnIndex(0);
            bool pinned = races::pinned(e.evId);
            if (ImGui::Checkbox("##pin", &pinned)) races::togglePin(e.evId);
            if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible) {   // keyboard/gamepad: show the race's original cars
                char cars[400]; carsText(e, cars, sizeof cars); ImGui::SetTooltip("%s\nOriginally for: %s", nc.name, cars[0] ? cars : "?");
            }
            ImGui::TableSetColumnIndex(1);
            if (e.mine) ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.2f, 1), "%s", nc.name); else ImGui::TextUnformatted(nc.name);
            if (ImGui::IsItemHovered()) { char cars[400]; carsText(e, cars, sizeof cars); ImGui::SetTooltip("Originally for: %s", cars[0] ? cars : "?"); }
            ImGui::TableSetColumnIndex(2);
            if (typeIndex(nc.type) == 7) ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1), "%s", nc.type); else ImGui::TextUnformatted(nc.type);
            ImGui::TableSetColumnIndex(3);
            if (e.distKm >= 0) ImGui::Text("%.1f km", e.distKm); else ImGui::TextDisabled("-");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// Gamepad: L3 + R3 (both sticks pressed) toggles the menu. Also keeps the game's input blocked
// while the menu is open and until every key/button used to close it has been released.
static void pollToggle() {
    static bool wasDown = false;
    bool down = false; XINPUT_STATE st;
    for (int i = 0; i < 4; i++)
        if (input::padState(i, &st) && (st.Gamepad.wButtons & (XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB)) == (XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB)) down = true;
    if (down && !wasDown) setOpen(!g_open);
    wasDown = down;
    if (g_releaseGuard && !g_open && !input::anyHeld()) g_releaseGuard = false;
    input::block(g_open || g_releaseGuard);
}

static HRESULT __stdcall hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!g_init && !g_failed) initImGui(sc);
    pollToggle();
    if (g_init) {
        DXGI_SWAP_CHAIN_DESC sd; sc->GetDesc(&sd);
        if (sd.OutputWindow == g_hwnd) {
            if (!g_rtv) createRTV(sc);
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            if (g_open) DrawMenu();
            else if (GetTickCount() - g_startTick < 15000) {
                ImGui::SetNextWindowPos(ImVec2(20, 60));
                ImGui::SetNextWindowBgAlpha(0.6f);
                ImGui::Begin("##hint", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
                ImGui::Text("ARO loaded - press %s (or L3+R3 on a gamepad)", core::cfg.menuKeyName);
                ImGui::End();
            }
            ImGui::Render();
            if (g_rtv) {
                ID3D11RenderTargetView* oldRtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
                ID3D11DepthStencilView* oldDsv = nullptr;
                g_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRtv, &oldDsv);
                g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                g_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRtv, oldDsv);
                for (auto* r : oldRtv) if (r) r->Release();
                if (oldDsv) oldDsv->Release();
            }
        }
    }
    return oPresent(sc, sync, flags);
}

static HRESULT __stdcall hkResize(IDXGISwapChain* sc, UINT c, UINT w, UINT h, DXGI_FORMAT f, UINT fl) {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    return oResize(sc, c, w, h, f, fl);
}

static bool hookSwapChain() {
    WNDCLASSEXW wc = { sizeof wc }; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"ORDummyWnd";
    RegisterClassExW(&wc);
    HWND dw = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!dw) { logf("[overlay] could not create helper window"); return false; }
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1; sd.BufferDesc.Width = 64; sd.BufferDesc.Height = 64;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = dw; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = nullptr; ID3D11Device* d = nullptr; ID3D11DeviceContext* c = nullptr;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &sd, &sc, &d, nullptr, &c);
    if (FAILED(hr) || !sc) { logf("[overlay] helper swap chain failed (0x%08lX)", (unsigned long)hr); DestroyWindow(dw); return false; }
    void** vt = *(void***)sc;
    DWORD old;
    VirtualProtect(&vt[8], sizeof(void*) * 6, PAGE_EXECUTE_READWRITE, &old);
    oPresent = (PresentFn)vt[8]; vt[8] = (void*)hkPresent;
    oResize = (ResizeFn)vt[13]; vt[13] = (void*)hkResize;
    VirtualProtect(&vt[8], sizeof(void*) * 6, old, &old);
    sc->Release(); c->Release(); d->Release(); DestroyWindow(dw);
    logf("[overlay] swap chain hooked");
    return true;
}

static BOOL CALLBACK findGameWindow(HWND h, LPARAM p) {
    DWORD pid; GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h)) { *(HWND*)p = h; return FALSE; }
    return TRUE;
}

static DWORD WINAPI overlayThread(void*) {
    // wait until the game has a visible window and has been running a few seconds
    for (int i = 0; i < 600; i++) {
        HWND w = nullptr;
        EnumWindows(findGameWindow, (LPARAM)&w);
        if (w) break;
        Sleep(500);
    }
    Sleep(3000);
    g_startTick = GetTickCount();
    hookSwapChain();
    return 0;
}

namespace menu {
void start() {
    CreateThread(nullptr, 0, overlayThread, nullptr, 0, nullptr);
}
}  // namespace menu
