// races.h - opening races to every car, EasyDrive pins and the race list for the menu.
#pragma once
#include <stdint.h>

namespace races {

bool install();               // hooks; call once the game code is unpacked
void tick();                  // worker thread, ~4x a second: keeps the event table opened

struct Race {
    int evId, key, mode, nameId, typeId, goalId;
    float distKm;             // -1 if unknown
    bool mine;                // originally belongs to the car you are driving
    int nCars; int cars[6];   // original cars
};
int  list(Race* out, int max);          // latest snapshot (copied)
int  playerCar();
bool originalFor(int key, int car);     // is `car` one of the race's original cars?

// pins: shown first in EasyDrive > Races (the game's list holds 8 rows)
constexpr int kMaxPins = 8;
int  pins(int* out);
bool pinned(int evId);
void togglePin(int evId);
void clearPins();

}  // namespace races
