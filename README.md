# All Races Open [ARO]

A mod for **Need for Speed: Most Wanted (2012)** on PC.

- **Every race for every car.** All races appear on the map for whatever car you drive; free-roam
  markers show the 20 races nearest to you and follow you around.
- **Parts stay fair.** Parts are only awarded when a race is run in one of the cars it was made for;
  in any other car you still earn Speed Points.
- **F5 race menu.** Every race with its real name and type, search, filters (your car's races, your
  class's races, race type) and sorting. Pin up to 8 races to the top of **EasyDrive › Races**.
- **Opponents you choose.** Race opponents get random cars from your class, from any class you pick,
  or a **real-world match**: rivals with the closest real power-to-weight to your car.
- Most Wanted races keep their boss car. Parked jack-spot cars and intro cutscenes are left alone.

## Screenshots

| | |
|---|---|
| ![F5 menu: every race with name, type and distance](assets/menu.png) | ![Filter: your car's own races](assets/my-car-races.png) |
| ![Pin up to 8 races](assets/pinning.png) | ![Pinned races at the top of EasyDrive](assets/easydrive-pins.png) |

## Requirements

- NFS Most Wanted (2012), Steam/Origin `NFS13.exe` (13,684,056 bytes). Other builds are detected and
  ARO does nothing.
- An ASI loader, e.g. [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
  (32-bit, as `dinput8.dll` in the game folder).
- Proton/Linux: add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the game's launch options.

## Install

Copy `ARO.asi` and `ARO.ini` into the game's `scripts` folder. Settings are in `ARO.ini`; ARO writes
`ARO.log` next to it. Play offline: don't take a modded game into online events.

## Build

`./build.sh` (needs `mingw-w64-gcc`). `tools/gen_cars.py "<game folder>"` regenerates
`src/cars.inc` from the game's own vehicle list and English text.

## Credits and licence

ARO is MIT licensed (see `LICENSE`). It includes [Dear ImGui](https://github.com/ocornut/imgui) and the
inflate part of [miniz](https://github.com/richgel999/miniz), both MIT (licences in `third_party/`).
Written with the help of Claude (Anthropic).
