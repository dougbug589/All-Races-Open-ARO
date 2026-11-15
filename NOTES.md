# ARO – reverse-engineering notes

All addresses are for the Steam/Origin `NFS13.exe` (13,684,056 bytes); the code is decrypted at
runtime, so ARO waits for it before hooking. Every hooked function is checked against its expected
first bytes (`src/game.h`) and skipped with a log line if it doesn't match.

## How things were found
- A memory image of the decrypted exe, disassembled with `objdump`, plus `tools/xref.py` (finds
  calls/jumps/pointers to an address).
- The developer probe (`DevProbe=1`, `tools/probe.py`, `tools/probelib.py`): read/write/scan the
  running game's memory from Linux.
- `tools/gen_cars.py`: cars and classes from `VEHICLES/VEHICLELIST.BNDL` (class key at entry+0x18:
  0x108F58 Sports, 0x108F50 Muscle, 0x108F5F Exotic, 0x108F67 Grand Tourer, 0x108F69 Everyday,
  0x108F66 SUV, 0x108F68 Race), names from `UI/LANGUAGE/0001.BNDL`.

## Language bundles
One zlib stream: u32 version=1, u32 count, u32 idsOffset (16), u32 pairsOffset; `count` sorted u32
string ids; `count` (offset, length) pairs to UTF-16LE text.

## EasyDrive
- List builder `0x80E0F0`: walks every event, filter `0x806CB0`, sorts into two 32-entry lists.
- Row builder `0x806E70`: 8 visible rows at obj+0x250 (event id, name id, list), count obj+0x2B0.
  The menu screen has 8 rows and does not scroll.

## Race records
Class hash +0x08 = 0x47CC3B90. +0x14 goal text, +0x18 event id, +0x1C mode (730000 race,
955569 speed run, 390185 ambush), +0x5C event-table key, +0x60 rules, +0x84 name text,
+0xA4 type text. A Most Wanted race's goal reads "BEAT THE <car>".

## Dead ends
- Changing an opponent's driver profile (slot +0x1A34, +0xC1C, +0xBB8) doesn't make the AI faster.
- Race-record fields +0x2C/+0x34 look like hours but don't change a race's time of day.
- Overriding one of +0xB0/+0xD4/+0xDC/+0x38 crashes race loading.
