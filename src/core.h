// core.h - logging, settings, memory helpers and inline hooks.
#pragma once
#include <windows.h>
#include <stdint.h>
#include "game.h"

namespace core {

void logInit(const char* path, bool enabled);
void log(const char* fmt, ...);

struct Settings {
    bool openRaces = true;        // every race for every car
    bool randomOpponents = true;  // swap race opponents' cars
    bool randomPaint = true;
    bool keepMostWanted = true;   // Most Wanted races keep their boss car
    bool uniqueOpponents = true;
    int maxSwaps = 7;             // per race: opponents past this many keep their original cars (big grids)
    bool notMyCar = true;         // opponents never get the car you drive
    bool fixNewBadge = true;
    bool menu = true;             // F5 overlay
    int menuKey = 0x74;           // virtual-key code that opens the menu (VK_F5); MenuKey= in ARO.ini
    char menuKeyName[16] = "F5";
    bool devProbe = false;        // developer memory probe on 127.0.0.1:47777
    bool logCallers = true;       // log who asks for a racer car (jack spot vs race)
};
extern Settings cfg;
void loadSettings(const char* iniPath);
const char* modDir();              // folder of ARO.asi, with trailing backslash
const char* gameDir();             // folder of NFS13.exe, with trailing backslash

// memory
bool readable(const void* p, size_t n = 4);
inline bool readable(uint32_t a, size_t n = 4) { return readable((const void*)a, n); }
template <class T> T at(uint32_t a) { return *(T*)a; }
uint32_t deref(uint32_t a);        // 0 if unreadable
bool sigMatches(uint32_t addr, const char* hexSig);

// Inline hook: overwrite `len` bytes at `addr` (whole, position-independent instructions)
// with a jump to `detour`. Returns a callable copy of the original start, or nullptr.
void* hook(const game::Fn& fn, int len, void* detour, const char* name);

}  // namespace core
