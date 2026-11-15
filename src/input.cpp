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

bool padState(int index, XINPUT_STATE* out) {
    return oXGetState && oXGetState((DWORD)index, out) == ERROR_SUCCESS;
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

void install() {
    HMODULE x = LoadLibraryA("xinput1_3.dll");
    if (x) {
        oXGetState = (XGetStateFn)patchExport(x, "XInputGetState", (void*)hkXGetState);
        core::log(oXGetState ? "[input] XInput hooked" : "[input] XInputGetState not found");
    }
    hookDirectInput();
}

}  // namespace input
