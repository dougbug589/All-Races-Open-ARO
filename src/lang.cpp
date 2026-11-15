// lang.cpp - reads the game's own text (UI/LANGUAGE/000N.BNDL) so the menu can show real race names.
// The bundle holds one zlib stream; inside: u32 version, u32 count, u32 idsOffset, u32 pairsOffset,
// then count sorted u32 string ids and count (offset, length) pairs pointing at UTF-16LE text.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniz.h"
#include "core.h"

static unsigned char* g_tbl; static size_t g_len; static unsigned g_n, g_idOff, g_pairOff;

namespace lang {
bool load(const char* file) {
    char path[600]; snprintf(path, sizeof path, "%sUI\\LANGUAGE\\%s", core::gameDir(), file);
    FILE* f = fopen(path, "rb"); if (!f) { char b[700]; snprintf(b, sizeof b, "[lang] cannot open %s", path); core::log("%s", b); return false; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* raw = (unsigned char*)malloc(sz); fread(raw, 1, sz, f); fclose(f);
    for (long i = 0; i + 2 < sz && !g_tbl; i++) {
        if (raw[i] != 0x78 || (raw[i + 1] != 0xDA && raw[i + 1] != 0x9C)) continue;
        size_t outLen = 0;
        void* out = tinfl_decompress_mem_to_heap(raw + i, sz - i, &outLen, TINFL_FLAG_PARSE_ZLIB_HEADER);
        if (out && outLen > 16) {
            unsigned* h = (unsigned*)out;
            if (h[0] == 1 && h[1] > 0 && h[2] == 16 && (size_t)h[3] + (size_t)h[1] * 8 <= outLen) {
                g_tbl = (unsigned char*)out; g_len = outLen; g_n = h[1]; g_idOff = h[2]; g_pairOff = h[3];
                break;
            }
        }
        if (out) free(out);
    }
    free(raw);
    char b[160]; snprintf(b, sizeof b, "[lang] %s: %u strings", file, g_tbl ? g_n : 0); core::log("%s", b);
    return g_tbl != nullptr;
}

// Returns UTF-8 text for a string id, or nullptr.
bool get(unsigned id, char* out, int outSize) {
    if (!g_tbl || !id) return false;
    const unsigned* ids = (const unsigned*)(g_tbl + g_idOff);
    unsigned lo = 0, hi = g_n;
    while (lo < hi) { unsigned m = (lo + hi) / 2; if (ids[m] < id) lo = m + 1; else hi = m; }
    if (lo >= g_n || ids[lo] != id) return false;
    const unsigned* pr = (const unsigned*)(g_tbl + g_pairOff) + lo * 2;
    unsigned off = pr[0], len = pr[1];
    if ((size_t)off + (size_t)len * 2 > g_len) return false;
    int n = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)(g_tbl + off), (int)len, out, outSize - 1, nullptr, nullptr);
    if (n <= 0) return false;
    out[n] = 0;
    while (n > 0 && (out[n - 1] == 0 || out[n - 1] == ' ')) out[--n] = 0;
    return n > 0;
}

}  // namespace lang
