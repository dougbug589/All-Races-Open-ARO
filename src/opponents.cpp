// opponents.cpp - gives race opponents other cars.
//
// The game asks SetSlotVehicle(slot, car, paint, flag) for every racer car it spawns: race grids,
// intro cutscenes, and also parked jack-spot cars in free roam. ARO only swaps real race opponents:
//   - never the player's slot or the car the player is driving (cutscenes use other slots),
//   - never Most Wanted boss racers (when KeepMostWantedBoss=1),
//   - never cars that aren't entered in a race (participant id -1: jack spots and parked cars).
#include "opponents.h"
#include "core.h"
#include "cars.h"
#include "races.h"
#include <string.h>
#include <stdio.h>
#include <vector>

using namespace game;
using core::log;

namespace opponents {

static volatile int g_mode = SameClass;
int mode() { return g_mode; }
void setMode(int m) { g_mode = m; log("[opponents] mode %d", m); }

static unsigned g_rng = 0x2545F491u;
static unsigned rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

// ---- paints: learned from the game. A paint the game puts on two different cars is a generic one.
struct PaintSeen { int paint; int car; bool generic; };
static std::vector<PaintSeen> g_paints;
static void learnPaint(int paint, int car) {
    if (!paint) return;
    for (PaintSeen& p : g_paints) if (p.paint == paint) { if (p.car != car && !p.generic) { p.generic = true; log("[paint] learned generic paint %d", paint); } return; }
    if (g_paints.size() < 256) g_paints.push_back({ paint, car, false });
}
static int randomPaint() {
    int n = 0; for (auto& p : g_paints) if (p.generic) n++;
    if (!n) return 0;
    int k = rnd() % n; for (auto& p : g_paints) if (p.generic && !k--) return p.paint;
    return 0;
}

// ---- picking
static int g_recent[16]; static int g_nRecent; static DWORD g_lastPick;
static bool recent(int id) { for (int i = 0; i < g_nRecent; i++) if (g_recent[i] == id) return true; return false; }

static int pickByClass(int cls, int player) {
    int n; const cars::Car* all = cars::all(&n);
    for (int pass = 0; pass < 3; pass++) {
        std::vector<int> pool;
        for (int i = 0; i < n; i++) {
            if (all[i].cls != cls) continue;
            if (pass < 1 && core::cfg.uniqueOpponents && recent(all[i].id)) continue;
            if (pass < 2 && core::cfg.notMyCar && all[i].id == player) continue;
            pool.push_back(all[i].id);
        }
        if (!pool.empty()) return pool[rnd() % pool.size()];
    }
    return 0;
}

// closest real-world power-to-weight; random among the 6 closest
static int pickRealWorld(int player) {
    int target = cars::powerToWeight(player); if (!target) return 0;
    int n; const cars::Car* all = cars::all(&n);
    struct C { int id, gap; }; std::vector<C> c;
    for (int i = 0; i < n; i++) {
        int pw = cars::powerToWeight(all[i].id); if (!pw) continue;
        if (core::cfg.notMyCar && all[i].id == player) continue;
        if (core::cfg.uniqueOpponents && recent(all[i].id)) continue;
        c.push_back({ all[i].id, pw > target ? pw * 100 / target : target * 100 / pw });
    }
    for (size_t i = 1; i < c.size(); i++) { C v = c[i]; size_t j = i; while (j && c[j - 1].gap > v.gap) { c[j] = c[j - 1]; j--; } c[j] = v; }
    size_t k = c.size() < 6 ? c.size() : 6;
    return k ? c[rnd() % k].id : 0;
}

// ---- callers
struct Caller { unsigned ret; int count; };
static Caller g_callers[32]; static int g_nCallers;
// Return addresses on the stack (values that point just after a call instruction in the game code).
static int stackTrace(const void* sp, unsigned* out, int max) {
    const unsigned* p = (const unsigned*)sp; int n = 0;
    for (int i = 0; i < 512 && n < max; i++) {
        if (!core::readable(p + i)) break;
        unsigned v = p[i];
        if (v < 0x401000 || v > 0xF82000 || !core::readable(v - 6, 6)) continue;
        const uint8_t* c = (const uint8_t*)v;
        if (c[-5] == 0xE8 || (c[-6] == 0xFF && (c[-5] == 0x15 || c[-5] == 0x90 || c[-5] == 0x92)) || (c[-2] == 0xFF && (c[-1] & 0xF8) == 0xD0) || (c[-3] == 0xFF && (c[-2] & 0xF8) == 0x50))
            out[n++] = v;
    }
    return n;
}
static void noteCaller(unsigned ret, int slot, int car, uint8_t* self, const void* sp) {
    if (!core::cfg.logCallers) return;
    unsigned st[8]; int n = stackTrace(sp, st, 8);
    unsigned key = 0; for (int i = 0; i < n; i++) key = key * 31 + st[i];
    for (int i = 0; i < g_nCallers; i++) if (g_callers[i].ret == key) { g_callers[i].count++; return; }
    if (g_nCallers < 32) g_callers[g_nCallers++] = { key, 1 };
    uint8_t* sl = self + 0x3050 + slot * kSlotStride;
    char b[256]; int k = 0; for (int i = 0; i < n; i++) k += snprintf(b + k, sizeof b - k, " %08X", st[i]);
    log("[callers] slot %d car %d (%s) state %d flags %08X pid %d driver %d | stack:%s", slot, car,
        cars::find(car) ? cars::find(car)->name : "?", *(int*)(sl + 0x1A74), *(unsigned*)(sl + 0x1A40),
        *(int*)(sl + 0x1A80), *(int*)(sl + 0x1A34), b);
}

// ---- choices per race participant: the game re-requests a racer's original car when it respawns
// (wreck, reset); that racer must get the same car back, never a new one mid-race.
struct Pick { int racer, original, chosen, paint; DWORD at; };
static Pick g_picks[64]; static int g_nPicks;
static Pick* findPick(int racer, int original) {
    for (int i = 0; i < g_nPicks; i++)
        if (g_picks[i].racer == racer && (g_picks[i].original == original || g_picks[i].chosen == original)) return &g_picks[i];
    return nullptr;
}
static Pick* newPick(int racer) {
    for (int i = 0; i < g_nPicks; i++) if (g_picks[i].racer == racer) return &g_picks[i];   // same racer, new race
    if (g_nPicks < 64) return &g_picks[g_nPicks++];
    Pick* oldest = &g_picks[0]; for (int i = 1; i < g_nPicks; i++) if (g_picks[i].at < oldest->at) oldest = &g_picks[i];
    return oldest;
}

static bool isPlayerSlot(uint8_t* self, int slot) {
    if (slot == 0) return true;
    int pid = *(int*)(self + slot * kSlotStride + kSlotPlayerId);
    if (pid == -1) return false;
    u32 net = core::deref(kNetPtr); if (!net) return false;
    typedef bool (__thiscall* ValidFn)(void*, int); typedef int (__thiscall* RemoteFn)(void*, int);
    return ((ValidFn)NetIsValid.addr)((void*)net, pid) && ((RemoteFn)NetIsRemote.addr)((void*)net, pid) == 0;
}

typedef int (__thiscall* SetSlotFn)(void*, int, int, int, int);
static SetSlotFn oSet;
static int g_player;

static int __thiscall hkSet(uint8_t* self, int slot, int car, int paint, int flag) {
    unsigned ret = (unsigned)__builtin_return_address(0);
    if (slot < 0 || slot >= 32) return oSet(self, slot, car, paint, flag);
    noteCaller(ret, slot, car, self, __builtin_frame_address(0));
    if (isPlayerSlot(self, slot)) {
        if (car != g_player) { const cars::Car* c = cars::find(car); log("[player] %s (%s)", c ? c->name : "?", c ? cars::className(c->cls) : "?"); }
        g_player = car;
        return oSet(self, slot, car, paint, flag);
    }
    learnPaint(paint, car);
    const cars::Car* orig = cars::find(car);
    const cars::Car* me = cars::find(g_player);
    const char* keep = nullptr;
    if (!core::cfg.randomOpponents) keep = "off";
    else if (!orig) keep = "not a racer car";
    else if (car == g_player) keep = "your car (cutscene)";
    else if (*(int*)(self + slot * kSlotStride + kSlotPlayerId) == -1) keep = "not in a race (jack spot / parked car)";
    else if (core::cfg.keepMostWanted && *(int*)(self + slot * kSlotStride + kSlotDriver) == kBossDriver) keep = "Most Wanted boss";
    else if (!me) keep = "your car is unknown";
    if (keep) { log("[opponents] slot %d keeps %d (%s) - %s", slot, car, orig ? orig->name : "?", keep); return oSet(self, slot, car, paint, flag); }

    DWORD now = GetTickCount();
    int racer = *(int*)(self + slot * kSlotStride + kSlotPlayerId);
    int state = *(int*)(self + slot * kSlotStride + kSlotState);              // 1 = fresh spawn, 3 = respawn
    if (Pick* p = findPick(racer, car)) {
        // respawns always get the same car; a fresh spawn reuses it only right after (intro cutscene -> race)
        if (state != 1 || now - p->at < 60 * 1000) { p->at = now; return oSet(self, slot, p->chosen, p->paint, flag); }
    }

    if (now - g_lastPick > 8000) g_nRecent = 0;
    g_lastPick = now;
    int m = g_mode, pick = 0;
    if (m == RealWorld) pick = pickRealWorld(g_player);
    if (!pick) pick = pickByClass(m >= 0 && m < cars::ClassCount ? m : me->cls, g_player);
    if (!pick) pick = car;
    if (g_nRecent < 16) g_recent[g_nRecent++] = pick;
    int newPaint = core::cfg.randomPaint ? randomPaint() : 0;
    if (pick == car && !newPaint) newPaint = paint;
    *newPick(racer) = { racer, car, pick, newPaint, now };
    log("[opponents] slot %d (racer %d): %s -> %s", slot, *(int*)(self + slot * kSlotStride + kSlotPlayerId), orig->name, cars::find(pick) ? cars::find(pick)->name : "?");
    return oSet(self, slot, pick, newPaint, flag);
}

bool install() {
    g_rng ^= GetTickCount() * 2654435761u;
    oSet = (SetSlotFn)core::hook(SetSlotVehicle, 5, (void*)hkSet, "opponent cars");
    return oSet != nullptr;
}

}  // namespace opponents
