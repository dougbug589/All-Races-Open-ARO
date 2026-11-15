// core.cpp - logging, settings, memory helpers and inline hooks.
#include "core.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

namespace core {

Settings cfg;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_logLock;
static char g_modDir[MAX_PATH], g_gameDir[MAX_PATH];

void logInit(const char* path, bool enabled) {
    InitializeCriticalSection(&g_logLock);
    if (enabled) g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void log(const char* fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char b[1024]; va_list a; va_start(a, fmt); int n = vsnprintf(b, sizeof b - 2, fmt, a); va_end(a);
    if (n < 0) return;
    if (n > (int)sizeof b - 3) n = sizeof b - 3;
    b[n++] = '\r'; b[n++] = '\n';
    EnterCriticalSection(&g_logLock);
    DWORD w; WriteFile(g_log, b, n, &w, nullptr); FlushFileBuffers(g_log);
    LeaveCriticalSection(&g_logLock);
}

static bool readBool(const char* ini, const char* key, bool def) {
    return GetPrivateProfileIntA("ARO", key, def ? 1 : 0, ini) != 0;
}

void loadSettings(const char* ini) {
    cfg.openRaces       = readBool(ini, "OpenRaces", true);
    cfg.randomOpponents = readBool(ini, "RandomOpponents", true);
    cfg.randomPaint     = readBool(ini, "RandomPaint", true);
    cfg.keepMostWanted  = readBool(ini, "KeepMostWantedBoss", true);
    cfg.uniqueOpponents = readBool(ini, "UniqueOpponents", true);
    cfg.notMyCar        = readBool(ini, "NeverGiveOpponentsMyCar", true);
    cfg.fixNewBadge     = readBool(ini, "FixNewBadge", true);
    cfg.menu            = readBool(ini, "Menu", true);
    cfg.devProbe        = readBool(ini, "DevProbe", false);
    cfg.logCallers      = readBool(ini, "LogCallers", true);
}

static void folderOf(HMODULE m, char* out) {
    GetModuleFileNameA(m, out, MAX_PATH);
    char* s = strrchr(out, '\\'); if (s) s[1] = 0;
}
const char* modDir() {
    if (!g_modDir[0]) { HMODULE m = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&modDir, &m);
        folderOf(m, g_modDir); }
    return g_modDir;
}
const char* gameDir() { if (!g_gameDir[0]) folderOf(nullptr, g_gameDir); return g_gameDir; }

bool readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION m; const char* a = (const char*)p;
    while (n) {
        if (!VirtualQuery(a, &m, sizeof m) || m.State != MEM_COMMIT) return false;
        if (!m.Protect || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        size_t left = (const char*)m.BaseAddress + m.RegionSize - a;
        if (left >= n) return true;
        a += left; n -= left;
    }
    return true;
}

uint32_t deref(uint32_t a) { return readable(a) ? *(uint32_t*)a : 0; }

static int parseSig(const char* s, uint8_t* out, int max) {
    int n = 0;
    while (*s && n < max) {
        if (*s == ' ') { s++; continue; }
        char h[3] = { s[0], s[1], 0 }; out[n++] = (uint8_t)strtoul(h, nullptr, 16); s += 2;
    }
    return n;
}

bool sigMatches(uint32_t addr, const char* sig) {
    if (!sig) return true;
    uint8_t b[64]; int n = parseSig(sig, b, sizeof b);
    return readable(addr, n) && !memcmp((void*)addr, b, n);
}

void* hook(const game::Fn& fn, int len, void* detour, const char* name) {
    if (len < 5 || !sigMatches(fn.addr, fn.sig)) { log("[hook] %s: game code not recognised at %08X - feature off", name, fn.addr); return nullptr; }
    uint8_t* p = (uint8_t*)fn.addr;
    uint8_t* tramp = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return nullptr;
    memcpy(tramp, p, len);                                   // original first instructions
    tramp[len] = 0xE9; *(int32_t*)(tramp + len + 1) = (int32_t)(p + len - (tramp + len + 5));
    DWORD old; VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9; *(int32_t*)(p + 1) = (int32_t)((uint8_t*)detour - (p + 5));
    for (int i = 5; i < len; i++) p[i] = 0x90;
    VirtualProtect(p, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    log("[hook] %s ok", name);
    return tramp;
}

}  // namespace core
