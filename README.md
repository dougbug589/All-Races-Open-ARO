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

## Install

1. **ASI loader:** download `Ultimate-ASI-Loader.zip` (the 32-bit one, not `_x64`) from
   [ThirteenAG's Ultimate ASI Loader releases](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest)
   and put its `dinput8.dll` in the game folder, next to `NFS13.exe`.
2. **ARO:** copy the `scripts` folder (with `ARO.asi` and `ARO.ini` inside) into the game folder.
   If the game already has a `scripts` folder, just merge them.
3. **Linux / Steam Deck (Proton) only:** set the game's launch options to
   `WINEDLLOVERRIDES="dinput8=n,b" %command%` so Proton uses the loader.

Press **F5** in free roam for the menu. Settings are in `ARO.ini`; ARO writes `ARO.log` next to it.
Play offline: don't take a modded game into online events.

## Build

`./build.sh` (needs `mingw-w64-gcc`) builds `dist/scripts/ARO.asi`; `dist/scripts/` is ready to copy into the game folder. `tools/gen_cars.py "<game folder>"` regenerates
`src/cars.inc` from the game's own vehicle list and English text.

## Credits and licence

ARO is MIT licensed (see `LICENSE`). It includes [Dear ImGui](https://github.com/ocornut/imgui) and the
inflate part of [miniz](https://github.com/richgel999/miniz), both MIT (licences in `third_party/`).
Written with the help of Claude (Anthropic).
