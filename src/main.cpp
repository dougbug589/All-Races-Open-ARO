// main.cpp - ARO (All Races Open) for Need for Speed: Most Wanted (2012).
// Loaded by an ASI loader from the game's scripts folder.
#include <windows.h>
#include <stdio.h>
#include "core.h"
#include "races.h"
#include "opponents.h"

namespace menu { void start(); }
namespace probe { void start(); }

static DWORD WINAPI worker(void*) {
    char ini[MAX_PATH], log[MAX_PATH];
    snprintf(ini, sizeof ini, "%sARO.ini", core::modDir());
    snprintf(log, sizeof log, "%sARO.log", core::modDir());
    core::loadSettings(ini);
    core::logInit(log, true);
    core::log("ARO - All Races Open. Waiting for the game code to unpack...");
    // NFS13.exe is decrypted by its DRM stub after DLLs load: wait until the code we need is there.
    for (int i = 0; i < 12000 && !core::sigMatches(game::SetSlotVehicle.addr, game::SetSlotVehicle.sig); i++) Sleep(10);
    if (!core::sigMatches(game::SetSlotVehicle.addr, game::SetSlotVehicle.sig)) {
        core::log("This game version isn't supported (needs the Steam/Origin NFS13.exe, 13,684,056 bytes). Nothing was changed.");
        return 0;
    }
    races::install();
    opponents::install();
    if (core::cfg.menu) menu::start();
    if (core::cfg.devProbe) probe::start();
    core::log("ready");
    for (;;) { races::tick(); Sleep(250); }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    return TRUE;
}
