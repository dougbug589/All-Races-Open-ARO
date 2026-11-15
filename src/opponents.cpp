// opponents.cpp - gives race opponents other cars.
//
// The game asks SetSlotVehicle(slot, car, paint, flag) for every racer car it spawns: race grids,
// intro cutscenes, and also parked jack-spot cars in free roam. ARO only swaps real race opponents:
//   - never the player's slot or the car the player is driving (cutscenes use other slots),
//   - never Most Wanted boss racers (when KeepMostWantedBoss=1),
//   - never cars spawned from callers listed as "keep" (jack spots, see LogCallers in the log).
#include "opponents.h"
#include "core.h"
#include "cars.h"
#include "races.h"
#include <string.h>
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
static void noteCaller(unsigned ret, int slot, int car) {
    for (int i = 0; i < g_nCallers; i++) if (g_callers[i].ret == ret) { g_callers[i].count++; return; }
    if (g_nCallers < 32) g_callers[g_nCallers++] = { ret, 1 };
    if (core::cfg.logCallers) log("[callers] new caller %08X (slot %d, car %d %s)", ret, slot, car, cars::find(car) ? cars::find(car)->name : "?");
}

// ---- slot memory: the game re-applies a slot's car at times; keep our choice
struct SlotPick { int original, chosen, paint; DWORD at; };
static SlotPick g_slots[32];

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
    noteCaller(ret, slot, car);
    if (isPlayerSlot(self, slot)) {
        if (car != g_player) { const cars::Car* c = cars::find(car); log("[player] %s (%s)", c ? c->name : "?", c ? cars::className(c->cls) : "?"); }
        g_player = car; g_slots[slot] = {};
        return oSet(self, slot, car, paint, flag);
    }
    learnPaint(paint, car);
    const cars::Car* orig = cars::find(car);
    const cars::Car* me = cars::find(g_player);
    const char* keep = nullptr;
    if (!core::cfg.randomOpponents) keep = "off";
    else if (!orig) keep = "not a racer car";
    else if (car == g_player) keep = "your car (cutscene)";
    else if (core::cfg.keepMostWanted && *(int*)(self + slot * kSlotStride + kSlotDriver) == kBossDriver) keep = "Most Wanted boss";
    else if (!me) keep = "your car is unknown";
    if (keep) { g_slots[slot] = {}; log("[opponents] slot %d keeps %d (%s) - %s", slot, car, orig ? orig->name : "?", keep); return oSet(self, slot, car, paint, flag); }

    DWORD now = GetTickCount();
    SlotPick& s = g_slots[slot];
    if (s.chosen && car == s.chosen) { s.at = now; return oSet(self, slot, car, s.paint, flag); }
    if (s.chosen && car == s.original && now - s.at < 30000) { s.at = now; return oSet(self, slot, s.chosen, s.paint, flag); }

    if (now - g_lastPick > 8000) g_nRecent = 0;
    g_lastPick = now;
    int m = g_mode, pick = 0;
    if (m == RealWorld) pick = pickRealWorld(g_player);
    if (!pick) pick = pickByClass(m >= 0 && m < cars::ClassCount ? m : me->cls, g_player);
    if (!pick) pick = car;
    if (g_nRecent < 16) g_recent[g_nRecent++] = pick;
    int newPaint = core::cfg.randomPaint ? randomPaint() : 0;
    if (pick == car && !newPaint) newPaint = paint;
    s = { car, pick, newPaint, now };
    log("[opponents] slot %d: %s -> %s (caller %08X)", slot, orig->name, cars::find(pick) ? cars::find(pick)->name : "?", ret);
    return oSet(self, slot, pick, newPaint, flag);
}

bool install() {
    g_rng ^= GetTickCount() * 2654435761u;
    oSet = (SetSlotFn)core::hook(SetSlotVehicle, 5, (void*)hkSet, "opponent cars");
    return oSet != nullptr;
}

}  // namespace opponents
