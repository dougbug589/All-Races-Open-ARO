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

## Download

Get **`ARO.zip`** from the [latest release](https://github.com/dougbug589/All-Races-Open-ARO/releases/latest).
It contains a `scripts` folder with `ARO.asi` and `ARO.ini`.

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

## Known limitations and plans

### Opponent pace (AI that matches your car)
ARO picks fair **cars** for your opponents, but it can't yet make the **drivers** match your car.
In testing, a Bugatti opponent drove no faster than the Aston Martin it replaced, and real-world
rivals for a fast car still fell behind. What we found so far:

- **The AI's pace isn't tied to its car.** Giving opponents faster cars changes little.
- **It isn't the driver profile either.** Each racer has a driver profile, and Most Wanted bosses use
  a stronger one. Giving every opponent the boss profile, before or during a race, made no difference.
- **The limit is probably a race-wide rule**, such as the catch-up ("rubber band") system or a pace cap
  in each race's AI settings. Those settings are stored as numeric IDs with no readable names, so
  finding them means tracing the AI's code by hand.
- **Experiments are risky.** Changing unknown race settings crashed the game while a race loaded.

So AI that drives to your car's level is the main goal for a future version, but there's no
release date: it depends on finding where the game decides how fast the AI may go.

### Other plans
- Custom **number of laps** and **number of opponents** per race (needs the same kind of research;
  some races may have a fixed number of grid spots).
- The game's **EasyDrive › Races** list shows at most 8 rows and doesn't scroll, so ARO's pins are
  limited to 8. The F5 menu always lists every race.

## Build

`./build.sh` (needs `mingw-w64-gcc`) builds `dist/scripts/ARO.asi`; `dist/scripts/` is ready to copy into the game folder. `tools/gen_cars.py "<game folder>"` regenerates
`src/cars.inc` from the game's own vehicle list and English text.

## Credits and licence

ARO is MIT licensed (see `LICENSE`). It includes [Dear ImGui](https://github.com/ocornut/imgui) and the
inflate part of [miniz](https://github.com/richgel999/miniz), both MIT (licences in `third_party/`).
Written with the help of Claude (Anthropic).
