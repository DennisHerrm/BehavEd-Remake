# BehavEd-Remake

Ein Nachbau von **BehavEd**, dem ICARUS-Skripteditor aus dem Jedi-Academy-SDK
von Raven – mit einer modernen Oberfläche und einer 3D-Vorschau, die
Zwischensequenzen so abspielt wie das Spiel (Jedi Academy / Movie Duels).

- Skripte (`.txt` / `.ibi`) öffnen, bearbeiten, kompilieren – Befehle,
  Makros und Dialoge wie im Original.
- Kartenansicht: BSP-Karten aus den Spielarchiven, Figuren mit Animationen,
  Effekte (`.efx`), Klang, Kamerafahrten, Zeitleiste zum Bearbeiten.
- ICARUS-Nachbau (`src/ablauf.cpp`): Skripte laufen im 50-ms-Takt der Engine,
  samt `affect`, `run`, `use`, Spawnern und Signalen.

## Bauen

Windows, Visual Studio 2026 (18) mit C++, CMake.

```
git clone --recurse-submodules https://github.com/DennisHerrm/BehavEd-Remake.git
cd BehavEd-Remake
build.bat
```

Einzelheiten und bekannte Stolpersteine stehen in [BAUEN.md](BAUEN.md), die
Tests in [TESTING.md](TESTING.md), die Änderungen in
[AENDERUNGEN.md](AENDERUNGEN.md).

## Fremde Bestandteile

- `imgui/` – [Dear ImGui](https://github.com/ocornut/imgui) (MIT), als Submodul.
- `third_party/minimp3.h` – minimp3 (CC0).
- `data/` enthält Dateien aus dem Jedi-Academy-SDK und aus der Original-
  `BehavEd.exe` (Bilder, Befehlsbeschreibung `behaved.bhc`, Header wie
  `Q3_Interface.h` und `anims.h`). Die Rechte daran liegen bei Raven Software
  bzw. Activision.

Noch keine Lizenz festgelegt – das Repository ist privat. Die fertigen
Programme liegen öffentlich in
[BehavEd-Remake-Releases](https://github.com/DennisHerrm/BehavEd-Remake-Releases);
von dort holt sich das Programm auch seine Updates (File → *Check for updates…*).
