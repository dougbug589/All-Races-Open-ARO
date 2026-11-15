// probe.cpp - live memory probe for reverse engineering (developer tool, not for release builds).
// Listens on 127.0.0.1:47777. One line per command, reply ends with a line containing a single ".".
//   r <addr> <len>              hex dump
//   d <addr> <count>            dwords as hex + int + float
//   w <addr> <hexbytes>         write bytes
//   wf <addr> <float>           write float      wi <addr> <int>   write int
//   p <addr> <off> [off...]     follow pointer chain, print each step
//   scan f <value> <tol>        new scan for a float (4-byte aligned, all readable memory)
//   scan i <value>              new scan for an int
//   next eq <v> [tol] | changed | unchanged | inc | dec   filter the previous results
//   list [n]                    show current results (address, value)
//   info                        race object, player position pointer, etc.
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <math.h>
#include "core.h"
#include "races.h"
#include "opponents.h"

static bool readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION m;
    const char* a = (const char*)p;
    while (n) {
        if (!VirtualQuery(a, &m, sizeof m)) return false;
        if (m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !m.Protect) return false;
        size_t left = (const char*)m.BaseAddress + m.RegionSize - a;
        if (left >= n) return true;
        a += left; n -= left;
    }
    return true;
}

// All memory access goes through Read/WriteProcessMemory: a bad or freed address returns an error
// instead of crashing the game.
static bool rd(unsigned a, void* dst, size_t n) { SIZE_T got = 0; return ReadProcessMemory(GetCurrentProcess(), (void*)a, dst, n, &got) && got == n; }
static bool wr(unsigned a, const void* src, size_t n) {
    DWORD old; if (!VirtualProtect((void*)a, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    SIZE_T put = 0; BOOL ok = WriteProcessMemory(GetCurrentProcess(), (void*)a, src, n, &put);
    VirtualProtect((void*)a, n, old, &old); return ok && put == n;
}
static bool rd32(unsigned a, unsigned* v) { return rd(a, v, 4); }

struct Hit { unsigned addr; unsigned prev; };
static std::vector<Hit> g_hits;
static bool g_isFloat = true;

static void out(std::string& o, const char* fmt, ...) {
    char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); o += b;
}

static void scanAll(bool isF, float fv, float tol, int iv, std::string& o) {
    g_hits.clear(); g_isFloat = isF;
    MEMORY_BASIC_INFORMATION m; unsigned a = 0x10000;
    size_t bytes = 0;
    while (a < 0xFFFE0000u && VirtualQuery((void*)a, &m, sizeof m)) {
        unsigned base = (unsigned)m.BaseAddress, size = (unsigned)m.RegionSize;
        bool ok = m.State == MEM_COMMIT && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                  (m.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY));
        if (ok) {
            bytes += size;
            static unsigned chunk[65536];
            for (unsigned off = 0; off < size; off += sizeof chunk) {
              unsigned len = size - off < sizeof chunk ? size - off : (unsigned)sizeof chunk;
              if (!rd(base + off, chunk, len)) continue;
              unsigned n = len / 4;
              for (unsigned i = 0; i < n; i++) {
                unsigned v = chunk[i]; bool hit;
                if (isF) { float f; memcpy(&f, &v, 4); hit = fabsf(f - fv) <= tol; }
                else hit = (int)v == iv;
                if (hit) { g_hits.push_back({ base + off + i * 4, v }); if (g_hits.size() > 4000000) break; }
              }
            }
        }
        if (base + size <= a) break;
        a = base + size;
    }
    out(o, "%u hits in %u MB of writable memory\n", (unsigned)g_hits.size(), (unsigned)(bytes >> 20));
}

static void filter(const char* how, float fv, float tol, std::string& o) {
    std::vector<Hit> keep;
    for (const Hit& h : g_hits) {
        unsigned v; if (!rd32(h.addr, &v)) continue; bool k = false;
        float f, pf; memcpy(&f, &v, 4); memcpy(&pf, &h.prev, 4);
        if (!strcmp(how, "eq")) k = g_isFloat ? fabsf(f - fv) <= tol : (int)v == (int)fv;
        else if (!strcmp(how, "changed")) k = v != h.prev;
        else if (!strcmp(how, "unchanged")) k = v == h.prev;
        else if (!strcmp(how, "inc")) k = g_isFloat ? f > pf : (int)v > (int)h.prev;
        else if (!strcmp(how, "dec")) k = g_isFloat ? f < pf : (int)v < (int)h.prev;
        if (k) keep.push_back({ h.addr, v });
    }
    g_hits.swap(keep);
    out(o, "%u hits left\n", (unsigned)g_hits.size());
}

static void handle(char* line, std::string& o) {
    char* argv[40]; int argc = 0;
    for (char* t = strtok(line, " \t\r\n"); t && argc < 40; t = strtok(nullptr, " \t\r\n")) argv[argc++] = t;
    if (!argc) return;
    auto U = [&](int i) { return (unsigned)strtoul(argv[i], nullptr, 0); };
    const char* c = argv[0];
    if (!strcmp(c, "r") && argc >= 3) {
        unsigned a = U(1), n = U(2); if (n > 4096) n = 4096;
        unsigned char buf[4096]; if (!rd(a, buf, n)) { out(o, "unreadable\n"); return; }
        for (unsigned i = 0; i < n; i += 16) {
            out(o, "%08X ", a + i);
            for (unsigned j = 0; j < 16 && i + j < n; j++) out(o, "%02X ", buf[i + j]);
            out(o, "\n");
        }
    } else if (!strcmp(c, "d") && argc >= 3) {
        unsigned a = U(1), n = U(2); if (n > 1024) n = 1024;
        unsigned buf[1024]; if (!rd(a, buf, n * 4)) { out(o, "unreadable\n"); return; }
        for (unsigned i = 0; i < n; i++) {
            unsigned v = buf[i]; float f; memcpy(&f, &v, 4);
            out(o, "+%03X %08X %11d %g\n", i * 4, v, (int)v, f);
        }
    } else if (!strcmp(c, "w") && argc >= 3) {
        unsigned a = U(1); size_t n = strlen(argv[2]) / 2; unsigned char buf[512];
        if (n > sizeof buf) n = sizeof buf;
        for (size_t i = 0; i < n; i++) { char h[3] = { argv[2][2 * i], argv[2][2 * i + 1], 0 }; buf[i] = (unsigned char)strtoul(h, nullptr, 16); }
        out(o, wr(a, buf, n) ? "wrote %u bytes\n" : "write failed (%u bytes)\n", (unsigned)n);
    } else if ((!strcmp(c, "wf") || !strcmp(c, "wi")) && argc >= 3) {
        unsigned a = U(1); unsigned v;
        if (c[1] == 'f') { float f = (float)atof(argv[2]); memcpy(&v, &f, 4); } else v = (unsigned)strtol(argv[2], nullptr, 0);
        out(o, wr(a, &v, 4) ? "ok\n" : "write failed\n");
    } else if (!strcmp(c, "p") && argc >= 2) {
        unsigned a = U(1);
        for (int i = 2; i <= argc; i++) {
            unsigned v; if (!rd32(a, &v)) { out(o, "%08X unreadable\n", a); return; }
            out(o, "[%08X] = %08X\n", a, v);
            if (i < argc) a = v + U(i);
        }
    } else if (!strcmp(c, "scan") && argc >= 3) {
        if (argv[1][0] == 'f') scanAll(true, (float)atof(argv[2]), argc > 3 ? (float)atof(argv[3]) : 0.01f, 0, o);
        else scanAll(false, 0, 0, (int)strtol(argv[2], nullptr, 0), o);
    } else if (!strcmp(c, "next") && argc >= 2) {
        filter(argv[1], argc > 2 ? (float)atof(argv[2]) : 0, argc > 3 ? (float)atof(argv[3]) : 0.01f, o);
    } else if (!strcmp(c, "list")) {
        unsigned n = argc > 1 ? U(1) : 50;
        for (unsigned i = 0; i < g_hits.size() && i < n; i++) {
            unsigned a = g_hits[i].addr; unsigned v = 0; rd32(a, &v); float f; memcpy(&f, &v, 4);
            out(o, "%08X %08X %d %g\n", a, v, (int)v, f);
        }
    } else if (!strcmp(c, "info")) {
        unsigned root = 0; rd32(0x0117F0F0, &root); out(o, "game root [0117F0F0]: %08X\n", root);
        out(o, "player car: %d\n", races::playerCar());
    } else out(o, "unknown command\n");
}

static DWORD WINAPI probeThread(void*) {
    WSADATA wd; if (WSAStartup(MAKEWORD(2, 2), &wd)) return 0;
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in sa = {}; sa.sin_family = AF_INET; sa.sin_port = htons(47777); sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    BOOL yes = TRUE; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    if (bind(ls, (sockaddr*)&sa, sizeof sa) || listen(ls, 1)) { core::log("[probe] could not listen on 127.0.0.1:47777"); return 0; }
    core::log("[probe] listening on 127.0.0.1:47777");
    for (;;) {
        SOCKET s = accept(ls, nullptr, nullptr); if (s == INVALID_SOCKET) continue;
        std::string buf; char tmp[1024]; int n;
        while ((n = recv(s, tmp, sizeof tmp, 0)) > 0) {
            buf.append(tmp, n);
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, nl); buf.erase(0, nl + 1);
                std::string o;
                handle(&line[0], o);
                o += ".\n";
                send(s, o.data(), (int)o.size(), 0);
            }
        }
        closesocket(s);
    }
}

static LONG CALLBACK crashLog(EXCEPTION_POINTERS* x) {
    static int n = 0; DWORD code = x->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO) return EXCEPTION_CONTINUE_SEARCH;
    if (n++ >= 5) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = x->ContextRecord; char b[600]; int k = snprintf(b, sizeof b,
        "[crash?] code %08lX at %08lX (addr %08lX) thread %lu eax=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX; stack:",
        code, (unsigned long)x->ExceptionRecord->ExceptionAddress, (unsigned long)x->ExceptionRecord->ExceptionInformation[1],
        GetCurrentThreadId(), c->Eax, c->Ecx, c->Edx, c->Esi, c->Edi);
    unsigned esp = c->Esp; int got = 0;
    for (unsigned p = esp; got < 16 && p < esp + 0x800 && k < 560; p += 4) {
        unsigned v; if (!rd32(p, &v)) break;
        if (v > 0x401000 && v < 0xF82000) { k += snprintf(b + k, sizeof b - k, " %08X", v); got++; }
    }
    core::log("%s", b); return EXCEPTION_CONTINUE_SEARCH;
}

namespace probe {
void start() {
    AddVectoredExceptionHandler(0, crashLog); CreateThread(nullptr, 0, probeThread, nullptr, 0, nullptr); }
}  // namespace probe
