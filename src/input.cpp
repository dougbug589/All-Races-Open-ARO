// input.cpp - block the game's input while the ARO menu is open.
//
// The game reads gamepads with XInput (xinput1_3.dll) and the keyboard/mouse with DirectInput 8.
//  - XInput: ARO points xinput1_3's exported XInputGetState at its own function before the game
//    resolves it, and hands the game an idle pad while blocked.
//  - DirectInput: ARO patches the device vtable (shared by every device) so GetDeviceState and
//    GetDeviceData report nothing pressed while blocked.
#include "input.h"
#include "core.h"
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <string.h>

namespace input {

static volatile bool g_block = false;
void block(bool on) { g_block = on; }

// ---------------------------------------------------------------- XInput
typedef DWORD(WINAPI* XGetStateFn)(DWORD, XINPUT_STATE*);
static XGetStateFn oXGetState;

static DWORD WINAPI hkXGetState(DWORD i, XINPUT_STATE* s) {
    DWORD r = oXGetState(i, s);
    if (g_block && r == ERROR_SUCCESS && s) memset(&s->Gamepad, 0, sizeof s->Gamepad);
    return r;
}

// XInputGetStateEx (hidden export, ordinal 100): same signature, also reports the Guide button.
static XGetStateFn oXGetStateEx;
static DWORD WINAPI hkXGetStateEx(DWORD i, XINPUT_STATE* s) {
    DWORD r = oXGetStateEx(i, s);
    if (g_block && r == ERROR_SUCCESS && s) memset(&s->Gamepad, 0, sizeof s->Gamepad);
    return r;
}

// Asking XInput about an empty controller slot is slow on Windows (it searches for the device),
// so a slot that had no pad is only checked again every 2 seconds.
static DWORD g_padRetry[4];
bool padState(int index, XINPUT_STATE* out) {
    if (!oXGetState || index < 0 || index > 3) return false;
    DWORD now = GetTickCount();
    if (g_padRetry[index] && now - g_padRetry[index] < 2000) return false;
    if (oXGetState((DWORD)index, out) == ERROR_SUCCESS) { g_padRetry[index] = 0; return true; }
    g_padRetry[index] = now ? now : 1;
    return false;
}

// Point an export of a loaded DLL at our function (export table holds RVAs from the DLL base).
static void* patchExport(HMODULE dll, const char* name, void* fn) {
    uint8_t* base = (uint8_t*)dll;
    auto* dos = (IMAGE_DOS_HEADER*)base;
    auto* nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress) return nullptr;
    auto* ex = (IMAGE_EXPORT_DIRECTORY*)(base + dir.VirtualAddress);
    auto* names = (DWORD*)(base + ex->AddressOfNames);
    auto* ords = (WORD*)(base + ex->AddressOfNameOrdinals);
    auto* funcs = (DWORD*)(base + ex->AddressOfFunctions);
    for (DWORD i = 0; i < ex->NumberOfNames; i++) {
        if (strcmp((const char*)(base + names[i]), name)) continue;
        DWORD* slot = &funcs[ords[i]];
        void* original = base + *slot;
        DWORD old; VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        *slot = (DWORD)((uint8_t*)fn - base);
        VirtualProtect(slot, 4, old, &old);
        return original;
    }
    return nullptr;
}

// ---------------------------------------------------------------- DirectInput
typedef HRESULT(WINAPI* GetStateFn)(IDirectInputDevice8A*, DWORD, LPVOID);
typedef HRESULT(WINAPI* GetDataFn)(IDirectInputDevice8A*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
static GetStateFn oGetState; static GetDataFn oGetData;
static volatile BYTE g_keys[256];   // last real keyboard state (for anyHeld)

static HRESULT WINAPI hkGetState(IDirectInputDevice8A* d, DWORD size, LPVOID data) {
    HRESULT r = oGetState(d, size, data);
    if (SUCCEEDED(r) && size == 256) memcpy((void*)g_keys, data, 256);
    if (g_block && SUCCEEDED(r) && data) memset(data, 0, size);   // nothing pressed, no movement
    return r;
}
static HRESULT WINAPI hkGetData(IDirectInputDevice8A* d, DWORD objSize, LPDIDEVICEOBJECTDATA buf, LPDWORD count, DWORD flags) {
    HRESULT r = oGetData(d, objSize, buf, count, flags);
    if (g_block && SUCCEEDED(r) && count && !(flags & DIGDD_PEEK)) *count = 0;   // events consumed, none reported
    return r;
}

static void hookDirectInput() {
    HMODULE di = GetModuleHandleA("dinput8.dll"); if (!di) di = LoadLibraryA("dinput8.dll");
    typedef HRESULT(WINAPI* CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    auto create = (CreateFn)GetProcAddress(di, "DirectInput8Create");
    if (!create) { core::log("[input] DirectInput not found - keyboard can't be blocked"); return; }
    IDirectInput8A* dinput = nullptr;
    if (FAILED(create(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A, (void**)&dinput, nullptr)) || !dinput) {
        core::log("[input] DirectInput8Create failed"); return;
    }
    IDirectInputDevice8A* kb = nullptr;
    if (SUCCEEDED(dinput->CreateDevice(GUID_SysKeyboard, &kb, nullptr)) && kb) {
        void** vt = *(void***)kb;
        DWORD old; VirtualProtect(&vt[9], 8, PAGE_EXECUTE_READWRITE, &old);
        oGetState = (GetStateFn)vt[9]; vt[9] = (void*)hkGetState;     // IDirectInputDevice8::GetDeviceState
        oGetData = (GetDataFn)vt[10]; vt[10] = (void*)hkGetData;      // IDirectInputDevice8::GetDeviceData
        VirtualProtect(&vt[9], 8, old, &old);
        kb->Release();
        core::log("[input] DirectInput hooked");
    }
    dinput->Release();
}

bool anyHeld() {
    XINPUT_STATE s;
    for (int i = 0; i < 4; i++) if (padState(i, &s) && (s.Gamepad.wButtons || s.Gamepad.bLeftTrigger > 30 || s.Gamepad.bRightTrigger > 30)) return true;
    for (int k = 0; k < 256; k++) if (g_keys[k] & 0x80) return true;
    for (int vk = 0x08; vk < 0xFF; vk++) if (vk != VK_CAPITAL && vk != VK_NUMLOCK && vk != VK_SCROLL && (GetAsyncKeyState(vk) & 0x8000)) return true;
    return false;
}

// The game may have looked XInput up before ARO loaded: replace every copy of the original
// function pointers inside the game's own image (import table or cached pointers).
static int replacePointers(void* from, void* to) {
    if (!from) return 0;
    uint8_t* base = (uint8_t*)GetModuleHandleA(nullptr);
    auto* nt = (IMAGE_NT_HEADERS32*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    uint32_t size = nt->OptionalHeader.SizeOfImage; int n = 0;
    for (uint32_t page = 0; page < size; page += 0x1000) {
      if (!core::readable(base + page, 0x1000)) continue;            // one check per 4 KB page
      for (uint32_t off = page; off < page + 0x1000 && off + 4 <= size; off += 4) {
        uint32_t* p = (uint32_t*)(base + off);
        if (*p != (uint32_t)from) continue;
        DWORD old; VirtualProtect(p, 4, PAGE_READWRITE, &old); *p = (uint32_t)to; VirtualProtect(p, 4, old, &old); n++;
      }
    }
    return n;
}

void patchGame() {
    int a = replacePointers((void*)oXGetState, (void*)hkXGetState);
    int b = replacePointers((void*)oXGetStateEx, (void*)hkXGetStateEx);
    core::log("[input] game XInput pointers redirected: XInputGetState %d, XInputGetStateEx %d", a, b);
}

void install() {
    HMODULE x = LoadLibraryA("xinput1_3.dll");
    if (x) {
        oXGetState = (XGetStateFn)patchExport(x, "XInputGetState", (void*)hkXGetState);
        oXGetStateEx = (XGetStateFn)GetProcAddress(x, (LPCSTR)100);
        core::log(oXGetState ? "[input] XInput hooked" : "[input] XInputGetState not found");
    }
    hookDirectInput();
}

}  // namespace input
