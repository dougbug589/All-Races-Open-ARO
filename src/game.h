// game.h - what ARO knows about NFS13.exe (Steam build, 13,684,056 bytes). See NOTES.md for how each was found.
#pragma once
#include <stdint.h>

namespace game {

using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t;

// ---- globals ----
constexpr u32 kRootPtr      = 0x0117F0F0;  // main game-logic object
constexpr u32 kRootPtrAlt   = 0x01177868;  // second pointer to the event-table owner
constexpr u32 kNetPtr       = 0x010EDAD0;  // network/player registry
constexpr u32 kPosMgrPtr    = 0x010EDAF0;  // world-location manager

// ---- root object offsets ----
constexpr u32 kRootEventTable = 0x14C84;   // {attrib handle, offset} of the EasyDrive event table
constexpr u32 kRootEdTable    = 0x14C08;   // "this" for the EasyDrive classifier calls
constexpr u32 kRootEventDb    = 0x2F3FC;   // -> event database; [db+4] = lookup object
constexpr u32 kRootPlayerCar  = 0x2F3DC;   // -> player vehicle owner
constexpr u32 kRootPlayer     = 0x2F480;   // -> player object; position at +0x2CD50
constexpr u32 kPlayerPos      = 0x2CD50;
constexpr u32 kRootUnlocks    = 0x29200;   // event-unlock state

// ---- event table (per EasyDrive entry) ----
// table: +0x10 record pointer array, +0x14 u16 count
// record: +0x0C key, +0x10 car array, +0x14 u16 car count
// car entry (0x2C bytes): +0x0C car id, +0x24 u16 part reward, +0x2A difficulty
constexpr u32 kCarEntrySize = 0x2C;

// ---- event records (lookup object +0x11C/+0x120 -> table of record pointers) ----
// +0x00 attrib collection, +0x08 class hash, +0x14 goal text id, +0x18 event id, +0x1C mode,
// +0x5C event-table key, +0x84 name text id, +0xA4 type text id, +0xEA listed flag, +0xEB "seen" flag
constexpr u32 kRaceRecordHash = 0x47CC3B90;
constexpr int kModeRace = 730000, kModeSpeedRun = 955569, kModeAmbush = 390185;
constexpr u32 kAttrEasyDriveEvent = 0x5F3F3;

// ---- racer slots (SetSlotVehicle "this") ----
constexpr u32 kSlotStride   = 0x1C40;
constexpr u32 kSlotPlayerId = 0x4AD0;      // this + slot*stride + ...: race participant id (-1 = not racing, 0 = you)
constexpr u32 kSlotDriver   = 0x3050 + 0x1A34;
constexpr u32 kSlotState    = 0x3050 + 0x1A74;  // 1 = fresh spawn, 3 = respawn (seen in logs)
constexpr int kBossDriver   = 1442225;     // Most Wanted boss racer driver profile

// ---- functions (address, expected first bytes) ----
struct Fn { u32 addr; const char* sig; };   // sig: hex bytes the function must start with
constexpr Fn SetSlotVehicle   { 0x00595B60, "83EC10535556 8B742420 8BEE 69ED401C" };
constexpr Fn EventLookup      { 0x006E5DC0, "53558B6C2410" };          // (lk; out[2], evId)
constexpr Fn EventLocation    { 0x006E56F0, nullptr };                 // (lk; out[2], evId)
constexpr Fn PositionOf       { 0x0053C020, nullptr };                 // (posMgr; handle) -> float*
constexpr Fn AwardParts       { 0x006E65E0, "5155568BF1" };            // post-event rewards
constexpr Fn CarEventList     { 0x006ECD70, "83EC0C558BE9" };          // (this; out, car)
constexpr Fn EventMember      { 0x006ECA10, "8B417C8B8980000000" };    // (this; key, car, flag)
constexpr Fn EventDifficulty  { 0x006F0980, "8B44240883EC08" };        // (lk; root, ev, car)
constexpr Fn EventMinDiff     { 0x006F0A20, "8B44240883EC08" };        // (lk; root, ev)
constexpr Fn EventRewardType  { 0x006F0B60, "8B44240883EC08" };        // (lk; root, ev, car)
constexpr Fn NewEventsFlag    { 0x00702710, "83EC2C5355" };            // (this) car-event state
constexpr Fn MarkersPlace     { 0x0074A600, "558BEC83E4F0" };          // (this)
constexpr Fn MarkersUpdate    { 0x00755220, "83EC0C5356" };            // (this)
constexpr Fn MarkersRemoveFar { 0x00755160, "83EC085556" };            // (this)
constexpr Fn EdRowsBuild      { 0x00806E70, "81EC9C000000" };          // EasyDrive rows (8 max)
constexpr Fn EdFilter         { 0x00806CB0, "83EC10" };                // (this; evId) -> bool
constexpr Fn HasAttr          { 0x00AC54C0, nullptr };                 // (coll; key) -> bool
constexpr Fn EventUnlocked    { 0x0068F190, nullptr };                 // (unlocks; evId) -> bool
constexpr Fn EventIsNew       { 0x006F9FA0, nullptr };                 // (this; evId) -> bool
constexpr Fn EdCompleted1     { 0x006F3510, nullptr };                 // (edTable; ev, car) -> bool
constexpr Fn EdCompleted2     { 0x006EC6A0, nullptr };                 // (edTable; ev, car) -> bool
constexpr Fn GetCarOwner      { 0x006BF330, nullptr };                 // (owner) -> obj
constexpr Fn GetCarId         { 0x006BA320, nullptr };                 // (obj; out[4])
constexpr Fn NetIsValid       { 0x006A11C0, nullptr };                 // (net; pid) -> bool
constexpr Fn NetIsRemote      { 0x0069FF70, nullptr };                 // (net; pid) -> int (0 = local)

// return addresses that identify callers
constexpr u32 kRetEasyDriveFilter = 0x00806E15;  // EventMember called by the EasyDrive filter
constexpr u32 kRetMarkersRefresh  = 0x00755362;  // MarkersPlace called during a refresh

}  // namespace game
