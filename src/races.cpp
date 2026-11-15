// races.cpp - every race for every car.
//
// The EasyDrive event table lists, per race, which cars may run it. ARO gives every race a longer
// list (the original cars plus every other racer car), and remembers the original lists so that:
//   - parts are only awarded when the race is run in one of its original cars,
//   - each car's own race list, the EasyDrive filter and the "NEW" badge keep the original meaning,
//   - free-roam markers show the 20 races nearest to you (the game holds only 20).
#include "races.h"
#include "core.h"
#include "cars.h"
#include <string.h>
#include <vector>

using namespace game;
using core::log;

namespace races {

// ---------------------------------------------------------------- event table
struct Opened {
    int key;
    std::vector<int> original;   // original car ids
    uint8_t* record;             // table record
    uint8_t* theirs;             // the game's car array
    uint8_t* ours;               // our longer copy
    int ourCount;
};
static std::vector<Opened> g_opened;
static CRITICAL_SECTION g_lock;

static Opened* opened(int key) {
    for (Opened& o : g_opened) if (o.key == key) return &o;
    return nullptr;
}

bool originalFor(int key, int car) {
    Opened* o = opened(key); if (!o) return true;
    for (int c : o->original) if (c == car) return true;
    return false;
}

static uint8_t* eventTable(int* count) {
    for (u32 ptr : { kRootPtr, kRootPtrAlt }) {
        u32 root = core::deref(ptr); if (!root) continue;
        u32 h = core::deref(root + kRootEventTable), off = core::deref(root + kRootEventTable + 4);
        u32 base = h ? core::deref(h + 0x28) : 0; if (!base) continue;
        uint8_t* t = (uint8_t*)(base + off);
        if (!core::readable(t, 0x16)) continue;
        int n = *(u16*)(t + 0x14); u32 recs = *(u32*)(t + 0x10);
        if (n <= 0 || n > 512 || !core::readable(recs, n * 4)) continue;
        *count = n; return t;
    }
    return nullptr;
}

// Give every table record our longer car list. Runs again whenever the game (re)loads the table.
static void openTable() {
    int n; uint8_t* t = eventTable(&n); if (!t) return;
    uint8_t** recs = *(uint8_t***)(t + 0x10);
    int carCount; const cars::Car* all = cars::all(&carCount);
    int changed = 0;
    for (int i = 0; i < n; i++) {
        uint8_t* r = recs[i]; if (!core::readable(r, 0x18)) return;
        int key = *(int*)(r + 0x0C); uint8_t* arr = *(uint8_t**)(r + 0x10); int cnt = *(u16*)(r + 0x14);
        Opened* o = opened(key);
        if (o && arr == o->ours) continue;                         // still ours
        if (cnt <= 0 || cnt > 32 || !core::readable(arr, cnt * kCarEntrySize)) continue;
        if (!o) {
            if (g_opened.size() >= g_opened.capacity()) return;     // never reallocate: hooks read it
            Opened fresh{ key, {}, r, arr, nullptr, 0 };
            for (int k = 0; k < cnt; k++) fresh.original.push_back(*(int*)(arr + k * kCarEntrySize + 0x0C));
            g_opened.push_back(std::move(fresh));
            o = &g_opened.back();
        }
        // build: original entries first, then one entry per other racer car (copied from the first,
        // with no part reward)
        std::vector<int> extra;
        for (int c = 0; c < carCount; c++) {
            bool have = false; for (int k = 0; k < cnt; k++) if (*(int*)(arr + k * kCarEntrySize + 0x0C) == all[c].id) have = true;
            if (!have) extra.push_back(all[c].id);
        }
        int total = cnt + (int)extra.size();
        uint8_t* mine = (uint8_t*)VirtualAlloc(nullptr, total * kCarEntrySize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!mine) return;
        memcpy(mine, arr, cnt * kCarEntrySize);
        for (size_t e = 0; e < extra.size(); e++) {
            uint8_t* slot = mine + (cnt + e) * kCarEntrySize;
            memcpy(slot, arr, kCarEntrySize);
            *(int*)(slot + 0x0C) = extra[e];
            *(u16*)(slot + 0x24) = 0;
        }
        DWORD old; VirtualProtect(r + 0x10, 8, PAGE_READWRITE, &old);
        *(uint8_t**)(r + 0x10) = mine;                               // bigger array first,
        *(u16*)(r + 0x14) = (u16)total;                              // then the larger count
        if (old != PAGE_READWRITE && old != PAGE_EXECUTE_READWRITE) VirtualProtect(r + 0x10, 8, old, &old);
        o->record = r; o->theirs = arr; o->ours = mine; o->ourCount = total;
        changed++;
    }
    if (changed) log("[races] opened %d races to all %d racer cars", changed, carCount);
}

// ---------------------------------------------------------------- game helpers
typedef int* (__thiscall* LookupFn)(void*, int*, int);
typedef bool (__thiscall* HasAttrFn)(void*, int);
typedef bool (__thiscall* Bool1Fn)(void*, int);
typedef bool (__thiscall* Bool2Fn)(void*, int, int);
typedef void* (__thiscall* ObjFn)(void*);
typedef void* (__thiscall* CarIdFn)(void*, int*);
typedef float* (__thiscall* PosFn)(void*, int);

static uint8_t* lookupObj(u32 root) { u32 db = core::deref(root + kRootEventDb); return db ? (uint8_t*)core::deref(db + 4) : nullptr; }

static uint8_t* recordOf(void* lk, int evId) {
    int out[2] = { 0, 0 };
    ((LookupFn)EventLookup.addr)(lk, out, evId);
    if (!out[0] || !core::readable(out[0] + 0x28)) return nullptr;
    u32 base = *(u32*)(out[0] + 0x28); if (!base) return nullptr;
    return (uint8_t*)(base + out[1]);
}

// all event records: lookup object +0x11C/+0x120
static uint8_t** eventRecords(uint8_t* lk, int* n) {
    if (!lk || !core::readable(lk + 0x120)) return nullptr;
    u32 h = *(u32*)(lk + 0x11C), off = *(u32*)(lk + 0x120);
    u32 base = h ? core::deref(h + 0x28) : 0; if (!base) return nullptr;
    uint8_t* d = (uint8_t*)(base + off); if (!core::readable(d + 0x10, 8)) return nullptr;
    *n = *(u16*)(d + 0x14);
    uint8_t** arr = *(uint8_t***)(d + 0x10);
    return (*n > 0 && *n < 2048 && core::readable(arr, *n * 4)) ? arr : nullptr;
}
static uint8_t** eventRecordsSlot(uint8_t* lk) {    // address of the array pointer (for temporary swaps)
    u32 h = *(u32*)(lk + 0x11C), off = *(u32*)(lk + 0x120);
    return (uint8_t**)(*(u32*)(h + 0x28) + off + 0x10);
}

static bool isEasyDriveEvent(uint8_t* ev) {
    u32 coll = core::deref(*(u32*)ev + 0x28) + *(u32*)(ev + 4);
    return ((HasAttrFn)HasAttr.addr)((void*)coll, kAttrEasyDriveEvent);
}

static int g_player = 0;
int playerCar() { return g_player; }

static int carOf(u32 root) {
    u32 owner = core::deref(root + kRootPlayerCar); if (!owner) return 0;
    void* obj = ((ObjFn)GetCarOwner.addr)((void*)owner); if (!obj) return 0;
    int out[4] = {}; ((CarIdFn)GetCarId.addr)(obj, out); return out[0];
}

// ---------------------------------------------------------------- hooks
// Parts: only for the race's original cars.
typedef void (__thiscall* AwardFn)(void*);
static AwardFn oAward;
static void __thiscall hkAward(uint8_t* self) {
    int ev = *(int*)(self + 0x2CE0), parts = *(int*)(self + 0x38B4);
    if (core::cfg.openRaces && ev && parts > 0) {
        uint8_t* rec = recordOf(*(void**)(self + 4), ev);
        if (rec) {
            int key = *(int*)(rec + 0x5C);
            int car = g_player ? g_player : carOf(core::deref(kRootPtr));
            if (opened(key) && !originalFor(key, car)) {
                *(int*)(self + 0x38B4) = 0;
                log("[races] race %d finished in a car it wasn't made for: %d part reward(s) skipped", ev, parts);
            }
        }
    }
    oAward(self);
}

// A car's own races (up to 10), from the original lists. Used by part unlocks and EasyDrive.
static void __thiscall hkCarEvents(uint8_t* self, uint8_t* out, int car) {
    uint8_t* root = *(uint8_t**)(self + 0xA4);
    int n; uint8_t** evs = eventRecords(lookupObj((u32)root), &n);
    if (!evs) return;
    for (int i = 0; i < n; i++) {
        uint8_t* ev = evs[i];
        if (!isEasyDriveEvent(ev) || !ev[0xEA]) continue;
        int key = *(int*)(ev + 0x5C);
        bool member = false;
        if (Opened* o = opened(key)) { for (int c : o->original) if (c == car) member = true; }
        else {                                          // not opened (yet): read the game's own list
            int tn; uint8_t* t = eventTable(&tn);
            for (int r = 0; t && r < tn; r++) {
                uint8_t* rec = (*(uint8_t***)(t + 0x10))[r];
                if (*(int*)(rec + 0x0C) != key) continue;
                uint8_t* arr = *(uint8_t**)(rec + 0x10); int c = *(u16*)(rec + 0x14);
                for (int k = 0; k < c; k++) if (*(int*)(arr + k * kCarEntrySize + 0x0C) == car) member = true;
            }
        }
        int& count = *(int*)(out + 0x28);
        if (member && count < 10) ((uint8_t**)out)[count++] = ev;
    }
}

// Event/car membership. When the EasyDrive filter asks, answer from the original lists.
static volatile bool g_wideCheck = false;            // set while ARO checks a pinned race
typedef bool (__thiscall* MemberFn)(void*, int, int, int);
static MemberFn oMember;
static bool __thiscall hkMember(void* self, int key, int car, int flag) {
    if ((u32)__builtin_return_address(0) == kRetEasyDriveFilter && !g_wideCheck && opened(key))
        return originalFor(key, car);
    return oMember(self, key, car, flag);
}

// "NEW" badge: recompute from the original lists after the game refreshes the car's race state.
typedef void (__thiscall* VoidFn)(void*);
static VoidFn oNewFlag;
static void __thiscall hkNewFlag(uint8_t* self) {
    oNewFlag(self);
    if (!core::cfg.fixNewBadge || g_opened.empty()) return;
    uint8_t* root = *(uint8_t**)(self + 0xA4); if (!root) return;
    int car = carOf((u32)root), n;
    uint8_t** evs = eventRecords(lookupObj((u32)root), &n); if (!evs) return;
    bool any = false;
    for (int i = 0; i < n && !any; i++) {
        uint8_t* ev = evs[i];
        if (!isEasyDriveEvent(ev) || ev[0xEB]) continue;
        int id = *(int*)(ev + 0x18);
        if (!((Bool1Fn)EventUnlocked.addr)(root + kRootUnlocks, id)) continue;
        int key = *(int*)(ev + 0x5C);
        if (!opened(key) || !originalFor(key, car)) continue;
        if (((Bool1Fn)EventIsNew.addr)(self, id)) any = true;
    }
    self[0x229] = any;
}

// Lookups that crash when asked about a race the game has no data for.
static bool known(void* lk, int ev) { return recordOf(lk, ev) != nullptr; }
typedef int (__thiscall* Ev3Fn)(void*, void*, int, int);
typedef int (__thiscall* Ev2Fn)(void*, void*, int);
static Ev3Fn oDifficulty, oRewardType; static Ev2Fn oMinDiff;
static int __thiscall hkDifficulty(void* lk, void* root, int ev, int car) { return known(lk, ev) ? oDifficulty(lk, root, ev, car) : -1; }
static int __thiscall hkRewardType(void* lk, void* root, int ev, int car) { return known(lk, ev) ? oRewardType(lk, root, ev, car) : 0; }
static int __thiscall hkMinDiff(void* lk, void* root, int ev) { return known(lk, ev) ? oMinDiff(lk, root, ev) : -1; }

// ---------------------------------------------------------------- free-roam markers (20 slots)
static float* playerPos(u32 root) { u32 p = core::deref(root + kRootPlayer); return p ? (float*)(p + kPlayerPos) : nullptr; }
static float distSq(void* lk, int evId, const float* pp) {
    int out[2] = { 0, 0 };
    typedef int* (__thiscall* LocFn)(void*, int*, int);
    ((LocFn)EventLocation.addr)(lk, out, evId);
    u32 mgr = core::deref(kPosMgrPtr); if (!mgr) return 3e38f;
    float* p = ((PosFn)PositionOf.addr)((void*)mgr, out[0]); if (!p) return 3e38f;
    float dx = p[0] - pp[0], dy = p[1] - pp[1], dz = p[2] - pp[2];
    return dx * dx + dy * dy + dz * dz;
}
static u32 markersRoot(uint8_t* m) {
    u32 a = core::deref((u32)m + 0x18); u32 b = a ? core::deref(a + 0x28) : 0; return b ? core::deref(b + 4) : 0;
}

static std::vector<uint8_t*> g_sorted;
static VoidFn oPlace;
static void __thiscall hkPlace(uint8_t* m) {
    u32 root = markersRoot(m); float* pp = root ? playerPos(root) : nullptr;
    uint8_t* lk = root ? lookupObj(root) : nullptr; int n;
    uint8_t** evs = lk ? eventRecords(lk, &n) : nullptr;
    if (!core::cfg.openRaces || !pp || !evs) { oPlace(m); return; }
    std::vector<float> d(n);
    g_sorted.assign(evs, evs + n);
    for (int i = 0; i < n; i++) d[i] = distSq(lk, *(int*)(evs[i] + 0x18), pp);
    for (int i = 1; i < n; i++) {                       // nearest first
        uint8_t* e = g_sorted[i]; float v = d[i]; int j = i - 1;
        while (j >= 0 && d[j] > v) { g_sorted[j + 1] = g_sorted[j]; d[j + 1] = d[j]; j--; }
        g_sorted[j + 1] = e; d[j + 1] = v;
    }
    if ((u32)__builtin_return_address(0) == kRetMarkersRefresh) {
        // refresh: the game flagged every marker for removal; keep the ones still among the nearest 20
        int count = *(int*)(m + 0x18C);
        for (int k = 0; k < count && k < 64; k++) {
            uint8_t* ent = m + 0x4C + k * 0x10; int id = *(int*)ent;
            for (int s = 0; s < n && s < 20; s++) if (*(int*)(g_sorted[s] + 0x18) == id) { ent[0xC] = 0; break; }
        }
        ((VoidFn)MarkersRemoveFar.addr)(m);
        count = *(int*)(m + 0x18C);
        for (int k = 0; k < count; k++) m[0x4C + k * 0x10 + 0xC] = 1;
    }
    uint8_t** slot = eventRecordsSlot(lk); uint8_t** saved = (uint8_t**)*slot;
    *slot = (uint8_t*)g_sorted.data();
    oPlace(m);
    *slot = (uint8_t*)saved;
}

static void snapshot(u32 root);
static float g_center[3]; static bool g_haveCenter = false;
typedef int (__thiscall* IntFn)(void*);
static IntFn oUpdate;
static int __thiscall hkUpdate(uint8_t* m) {
    u32 root = markersRoot(m);
    if (core::cfg.openRaces && root) {
        if (float* p = playerPos(root)) {
            float dx = p[0] - g_center[0], dy = p[1] - g_center[1], dz = p[2] - g_center[2];
            if (!g_haveCenter || dx * dx + dy * dy + dz * dz > 100.f * 100.f) {   // every 100 m
                memcpy(g_center, p, sizeof g_center);
                if (g_haveCenter) *(int*)(m + 0x1E0) = -1;                        // rebuild this frame
                g_haveCenter = true;
            }
        }
    }
    if (root) snapshot(root);
    return oUpdate(m);
}

// ---------------------------------------------------------------- pins (EasyDrive rows)
static volatile int g_pins[kMaxPins]; static volatile int g_nPins = 0;
int pins(int* out) { int n = g_nPins; for (int i = 0; i < n; i++) out[i] = g_pins[i]; return n; }
bool pinned(int evId) { for (int i = 0; i < g_nPins; i++) if (g_pins[i] == evId) return true; return false; }
void togglePin(int evId) {
    int n = g_nPins;
    for (int i = 0; i < n; i++) if (g_pins[i] == evId) {
        for (int j = i; j < n - 1; j++) g_pins[j] = g_pins[j + 1];
        g_nPins = n - 1; log("[pins] removed %d", evId); return;
    }
    if (n < kMaxPins) { g_pins[n] = evId; g_nPins = n + 1; log("[pins] added %d", evId); }
}
void clearPins() { g_nPins = 0; }

// The EasyDrive race menu shows 8 rows (this+0x250, 12 bytes each: event, name id, list; count +0x2B0).
static VoidFn oRows;
static void __thiscall hkRows(uint8_t* self) {
    oRows(self);
    int np = g_nPins; if (!np) return;
    int cnt = *(int*)(self + 0x2B0); if (cnt < 0 || cnt > 8) return;
    u32 root = core::deref(kRootPtr); uint8_t* lk = root ? lookupObj(root) : nullptr; if (!lk) return;
    int car = carOf(root);
    int rows[8][3], n = 0;
    for (int p = 0; p < np && n < 8; p++) {
        int ev = g_pins[p]; bool dup = false;
        for (int j = 0; j < n; j++) if (rows[j][0] == ev) dup = true;
        uint8_t* rec = dup ? nullptr : recordOf(lk, ev); if (!rec) continue;
        g_wideCheck = true; bool ok = ((Bool1Fn)EdFilter.addr)(self, ev); g_wideCheck = false;
        if (!ok) { log("[pins] race %d can't be listed right now (locked?)", ev); continue; }
        void* ed = (void*)(root + kRootEdTable);
        bool done = car && ((Bool2Fn)EdCompleted1.addr)(ed, ev, car) && ((Bool2Fn)EdCompleted2.addr)(ed, ev, car);
        rows[n][0] = ev; rows[n][1] = *(int*)(rec + 0x88); rows[n][2] = done ? 2 : 0; n++;
    }
    for (int i = 0; i < cnt && n < 8; i++) {
        int* r = (int*)(self + 0x250 + i * 12); bool dup = false;
        for (int j = 0; j < n; j++) if (rows[j][0] == r[0]) dup = true;
        if (!dup) { rows[n][0] = r[0]; rows[n][1] = r[1]; rows[n][2] = r[2]; n++; }
    }
    for (int i = 0; i < n; i++) memcpy(self + 0x250 + i * 12, rows[i], 12);
    *(int*)(self + 0x2B0) = n;
}

// ---------------------------------------------------------------- race list snapshot (game thread)
static Race g_snap[2][256]; static volatile int g_snapN[2], g_cur;
static DWORD g_snapAt;
static void snapshot(u32 root) {
    DWORD now = GetTickCount(); if (now - g_snapAt < 500) return; g_snapAt = now;
    uint8_t* lk = lookupObj(root); int n; uint8_t** evs = eventRecords(lk, &n); if (!evs) return;
    float* pp = playerPos(root); int car = carOf(root); if (car) g_player = car;
    int w = g_cur ^ 1, k = 0;
    for (int i = 0; i < n && k < 256; i++) {
        uint8_t* ev = evs[i];
        if (!core::readable(ev, 0xEC) || *(u32*)(ev + 0x08) != kRaceRecordHash) continue;
        Opened* o = opened(*(int*)(ev + 0x5C)); if (!o) continue;
        Race& r = g_snap[w][k++];
        r.evId = *(int*)(ev + 0x18); r.key = o->key; r.mode = *(int*)(ev + 0x1C);
        r.nameId = *(int*)(ev + 0x84); r.typeId = *(int*)(ev + 0xA4); r.goalId = *(int*)(ev + 0x14);
        r.distKm = -1; if (pp) { float d = distSq(lk, r.evId, pp); if (d < 1e37f) r.distKm = __builtin_sqrtf(d) / 1000.f; }
        r.nCars = 0; for (int c : o->original) if (r.nCars < 6) r.cars[r.nCars++] = c;
        r.mine = car && originalFor(o->key, car);
    }
    g_snapN[w] = k; g_cur = w;
}
int list(Race* out, int max) { int c = g_cur, n = g_snapN[c]; if (n > max) n = max; memcpy(out, g_snap[c], n * sizeof(Race)); return n; }

// ---------------------------------------------------------------- setup
bool install() {
    InitializeCriticalSection(&g_lock);
    g_opened.reserve(1024);
    if (!core::cfg.openRaces) return true;
    oAward = (AwardFn)core::hook(AwardParts, 5, (void*)hkAward, "parts filter");
    if (!oAward) { core::cfg.openRaces = false; return false; }
    oDifficulty = (Ev3Fn)core::hook(EventDifficulty, 7, (void*)hkDifficulty, "difficulty guard");
    oMinDiff    = (Ev2Fn)core::hook(EventMinDiff, 7, (void*)hkMinDiff, "min difficulty guard");
    oRewardType = (Ev3Fn)core::hook(EventRewardType, 7, (void*)hkRewardType, "reward guard");
    core::hook(CarEventList, 6, (void*)hkCarEvents, "car race list");
    oMember  = (MemberFn)core::hook(EventMember, 9, (void*)hkMember, "EasyDrive membership");
    oNewFlag = (VoidFn)core::hook(NewEventsFlag, 5, (void*)hkNewFlag, "NEW badge");
    oPlace   = (VoidFn)core::hook(MarkersPlace, 6, (void*)hkPlace, "map markers");
    oUpdate  = (IntFn)core::hook(MarkersUpdate, 5, (void*)hkUpdate, "marker refresh");
    oRows    = (VoidFn)core::hook(EdRowsBuild, 6, (void*)hkRows, "EasyDrive pins");
    return true;
}

void tick() { if (core::cfg.openRaces) openTable(); }

}  // namespace races
