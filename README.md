# BehavEd-Remake

A remake of **BehavEd**, the ICARUS script editor from Raven's Jedi Academy
SDK – with a modern interface and a 3D preview that plays cutscenes the way the
game does (Jedi Academy / Movie Duels).

- Open, edit and compile scripts (`.txt` / `.ibi`) – commands, macros and
  dialogs as in the original.
- Map view: BSP maps straight from the game archives, characters with
  animations, effects (`.efx`), sound, camera moves and an editable timeline.
- ICARUS re-implementation (`src/ablauf.cpp`): scripts run on the engine's
  50 ms tick, including `affect`, `run`, `use`, spawners and signals.
- Undo history with selective undo, rolling backups of the last 10 saves,
  split views, bookmarks, change markers with revert-to-original.
- Built-in updater: new releases are offered at startup.
- English, German, Chinese and Japanese.

## Download

Get the latest build from
[Releases](https://github.com/DennisHerrm/BehavEd-Remake/releases/latest):
unzip it and start `BehavEd-Remake/behaved.exe` (Windows 10/11). Add your
Jedi Academy / Movie Duels game folders under **Paths**. The program updates
itself from here (File → *Check for updates…*).

## Building

Windows, Visual Studio 2026 (18) with C++, CMake.

```
git clone --recurse-submodules https://github.com/DennisHerrm/BehavEd-Remake.git
cd BehavEd-Remake
build.bat
```

Details and known pitfalls are in [BAUEN.md](BAUEN.md), the tests in
[TESTING.md](TESTING.md), the change log in [AENDERUNGEN.md](AENDERUNGEN.md)
(the developer documentation and code comments are in German).

## Third-party content

- `imgui/` – [Dear ImGui](https://github.com/ocornut/imgui) (MIT), as a submodule.
- `third_party/minimp3.h` – minimp3 (CC0).
- `data/` contains files from the Jedi Academy SDK and from the original
  `BehavEd.exe` (images, the command description `behaved.bhc`, headers such as
  `Q3_Interface.h` and `anims.h`). These are the property of Raven Software /
  Activision.

No license has been chosen yet.
