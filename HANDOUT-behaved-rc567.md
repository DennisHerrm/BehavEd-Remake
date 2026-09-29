# behaved — Handout für den nächsten Chat

> **Stand rc568: der Software-Rasterer ist entfernt.** Gezeichnet wird nur
> noch ueber Direct3D 11 (`gui/gpumap_win32.cpp`). Massstab bei Bildfehlern
> ist das Spiel und der OpenJK-Quelltext, nicht mehr `mapview.cpp` - dort
> steht nur noch, was beide Wege teilten (Kamera, Texturen, Stapelzustaende,
> `figurKnochen`). Was unten ueber den Rasterer steht, ist Geschichte.

**Stand: rc567, 2026-09-10.** Paket: `behaved-1_0_0-rc567.zip`.

Aufbau:

* **Teil A (Abschnitte 1–8)** — der aktuelle Stand, die letzte Sitzung
  rc530–rc567 im Detail, was offen ist.
* **Teil B (Abschnitte 9–12)** — die **ganze Projektgeschichte** über alle
  früheren Chats, gesammelte Recherchen und Stolpersteine aus allen
  Sitzungen.

Die älteren Dokumente im Paket (`HANDOUT-behaved-rc199.md`, `-rc229.md`,
`UEBERGABE.md` = rc429, `UMBAU-GPU.md`, `DEBUGGEN.md`, `ABGLEICH.md`,
`BEFUNDE-rc532.md`, `ARCHITEKTUR.md`) bleiben gültig, wo dieses Handout sie
nicht ausdrücklich überholt. `AENDERUNGEN.md` enthält **jede Runde ab rc347
lückenlos** sowie rc96–rc129; rc130–rc346 stehen dort nicht.

---

## 1. Rahmen — bitte zuerst lesen

* **behaved** ersetzt BehavEd, den ICARUS-Skripteditor für Jedi Academy /
  Movie Duels. C++20 + Dear ImGui, rund 37 000 Zeilen, dazu eine 3D-Ansicht
  (Software-Rasterer und wahlweise Direct3D 11).
* **Es wird blind entwickelt.** Hier (Linux-Container) läuft nur `src/` samt
  Proben. `gui/app.cpp`, `gui/app_view3d.cpp` und `gui/main_win32.cpp`
  brauchen ImGui bzw. Windows und lassen sich **hier nicht übersetzen**.
* **shank** baut unter Windows mit MSVC (Visual Studio 2026, CMake 4.3) und
  schickt `behaved.log` und `behaved-detail.log`, oft mit Bildern. Das
  Detailprotokoll ist das wichtigste Werkzeug.
* ImGui: gebaut wird gegen **1.92.9b-docking** (shank), hier liegt
  `~/imgui-pin` (1.93.0 WIP). `gui/app_internal.h` bindet
  `imgui_internal.h` ein — nötig für `ImGuiTable::ResizedColumn`.
  **Bei einem ImGui-Update bricht das zuerst.**
* DH möchte: Messungen statt Vermutungen, frei online nachschlagen, bei
  Unklarheit direkt nachfragen. Antworten auf Deutsch.

---

## 2. Prüflauf vor jeder Auslieferung

```sh
cd behaved
FLAGS="-std=c++20 -O2 -Wall -Wextra -Iinclude -Ithird_party"
# Probenprogramme (alle mit src/*.cpp)
for t in checks kennungen abweisung blockmove einzelkind sprache \
         rundlauf_kennung tabellen; do g++ $FLAGS src/*.cpp tests/$t.cpp -o /tmp/$t; done
/tmp/checks data/base/behaved.bhc data/base data/supplement.bhc
/tmp/rundlauf_kennung data/fixtures/*.txt          # muss "4 von 4 bytegleich" sagen
/tmp/tabellen data/base/behaved.bhc data/base
g++ -std=c++20 -O2 -Wall -Wextra -Werror tests/helferwerte.cpp -o /tmp/hw && /tmp/hw
# Makrozeilen UNTER ASan - sonst prueft die Probe den Zeiger nicht
g++ -std=c++20 -O1 -g -fsanitize=address -Iinclude -Ithird_party src/*.cpp tests/makrozeilen.cpp -o /tmp/mz && /tmp/mz
# D3D-Zweig blind mit Attrappe
g++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only -DBHED_WITH_D3D11 \
    -Itools/d3dstub -Iinclude -Ithird_party -Igui gui/gpumap_win32.cpp
# alle 31 Pruefer (lint_tidy braucht clang-tidy und faellt hier weg)
for l in tools/lint_*.py; do python3 "$l" >/dev/null || echo "FEHL $l"; done
```

Danach: `kFassung` in `include/bhed/fassung.h` hochzählen, Eintrag oben in
`AENDERUNGEN.md` (sonst schlägt `lint_fassung` an), packen.

**Für blinde Änderungen an `gui/*.cpp` zusätzlich**, weil der Übersetzer
dort nicht hilft:

* `tools/lint_gapp.py` — jedes `g_app->FELD` muss in `struct App` stehen.
* Klammern der geänderten Funktion zählen (Zeichenketten/Kommentare
  ausgeblendet), `Begin*`/`End*` paarweise zählen.
* **Sichtbarkeit prüfen über das tiefste Zwischenniveau**, nicht über die
  reine Klammertiefe — sonst übersieht man, dass ein Block dazwischen
  schließt (rc558: `fensterStabil` war nicht mehr sichtbar).
* `snprintf`-Platzhalter gegen Argumente zählen.
* Kein unbenutzter Bezeichner zurücklassen — MSVC mit Warnungen als Fehler.

---

## 3. Was in dieser Sitzung gemacht wurde

### Skriptbaum: Aufklappen, Ziehen, Bewegen

| rc | Inhalt |
|---|---|
| 530 | Hinweis „Makrozeile" beim Bewegen einer Makro-Befehlszeile |
| 531 | `g_messWert` nur Element 0 initialisiert → 7 falsche GPU-Zeiten; `Row::name` bei Makros leer; `verify.sh` kannte 19 Quellen nicht. Neu: `lint_feldinit.py` |
| 532 | Abgleich gegen Original-BehavEd: 6 umbenannte, 8 entfernte `SET_`-Felder; `V004` schlägt JKA-Namen vor (Levenshtein ≤ 2). Neu: `tools/icarus_regeln.py`, `tests/tabellen.cpp` |
| 533 | Klicken fügt **neben** der Auswahl ein, nie in einen Block (Füllen nur per Ziehen HINEIN) |
| 534 | `moveInto`, Rechtsklick-Bewegen, Makro-Einfügen = 1 Rückgängig-Schritt (`insertAfterAll`) |
| 535 | **Aufklappzustand hängt an Knoten-Kennungen statt an Wegen** (`Node::kennung`, `vergibKennungen()`); `nachZug` entfernt; 27 Proben in `tests/kennungen.cpp`, Rundlaufprobe `tests/rundlauf_kennung.cpp` |
| 536 | Abweisungsgründe auf dem Schirm (`Document::Abweisung`, 8 Proben in `tests/abweisung.cpp`) |

**Die offene Frage aus rc530 ist beantwortet:** shanks Protokoll zeigte
`Tiefe 2`, nicht `MAKROZEILE` — eine Makrozeile in einem echten Block ließ
sich herausziehen. Der `MAKROZEILE`-Hinweis steht nur im **Knopf**-Zweig,
beim Ziehen konnte er nicht erscheinen. In rc537 kam er beim Knopf sichtbar
und präzise (dreimal, genau solange die Zeile im Makrorumpf lag).

**Wie es zu den Kennungen kam (rc534 → rc535):** Gezählt wurden 16
Verfahren, die die Struktur ändern; nur 5 führten den Aufklappzustand nach.
Drei Wege standen zur Wahl:

* **A** `nachEinfuegen`/`nachLoeschen` — kann Rückgängig nicht heilen
  (gemessen), bleibt Flickwerk.
* **C** `bool aufgeklappt` im `Node` — **verworfen**: der Zustand läge in den
  Schnappschüssen, und ein zweites Strg+Z würde einen Block schließen, den der
  Benutzer geöffnet hat. Aufklappen ist keine Bearbeitung.
* **B** Kennungen, vergeben **in einem Durchlauf** in `rebuildTree()` statt an
  den 15 Erzeugungsstellen — gewählt, nach der Qt-Recherche (Abschnitt 5).

**Angeboten, nicht umgesetzt:** Seit rc533 füllt man einen leeren Block nur
noch per Ziehen. Falls shank das zu umständlich findet: nur den *leeren*
Block per Klick füllen und die Auswahl auf dem Block lassen.
| 539 | **heap-use-after-free** in den Makro-Kindzeilen (Zeiger in lokalen Vektor) + Blöcke im Makrorumpf zeigten ihre Kinder nicht. `tests/makrozeilen.cpp` unter ASan |
| 541 | `replaceAt` (Editor-Ok) verlor die Kennung → Block klappte zu. Regression aus rc535 |
| 544 | Duplizieren mit Mehrfachauswahl (`Document::cloneAll`); Rechtsklick wählt die Zeile |

### Protokoll und Diagnose

* **rc538** Jede Statuszeile steht wortgleich im Detailprotokoll. Dazu:
  ein **abgebrochener** Zug räumte den Aufnahme-Merker `dbgZiehWeg` nicht
  auf (nur die drei Ablegestellen taten es) — jetzt am Ende des Baumaufbaus,
  wenn ImGui keinen Zug mehr kennt. In rc539 bestätigt.
* **rc540** Zeile `Sprache: xx (aus …)`; `Settings::language` Vorgabe leer
  statt `"en"`. Befund: **shanks Einstellungsdatei enthält wirklich
  `"en"`** — er muss einmal im Menü Deutsch wählen.
* **rc548** Leser melden, was sie gelesen haben: `GLM gelesen`, `GLA gelesen`
  (Name seit rc550 aus `skeletonName`), `MD3`, `BSP`, `Kartengeometrie`,
  `Spielordner`. **Noch stumm:** `mapview.cpp`, `efxdraw.cpp`, `scene.cpp`,
  Bildlader (dort wäre je Bild zu protokollieren → anderes Verfahren nötig).
  Die Zeile `Figurenmodelle: …` ist **Anzeigetext** und folgt der
  UI-Sprache (bei shank Englisch) — sie ist als solche gekennzeichnet; alle
  anderen Protokollzeilen sind fest deutsch.
* **rc553** `Editor auf` / `Editor Ok` — Werte, Ausdrucksmodus, Helfer.
* **rc555/556** `Teiler geschrieben`, **rc558** `Actions geschrieben`.
* **rc559** `Fensterlage geladen/gesichert`.
* Messzeilen im Editor: `Ereigniseditor Mitte`, `Ereigniseditor Spalte 0`
  (seit rc567 mit `Helfer x/y`), `Ereignisfenster gewachsen`, `Klappliste
  auf/rollt`.

### Erzeuger- und Bauwerkzeuge

* **rc537 — mein schwerster Fehler der Sitzung:** `gen_i18n.py` neu laufen
  lassen löschte 24 von Hand nachgetragene Texte aus `i18n.h/.cpp` → MSVC
  brach mit 24× C2065 ab (Linux merkte nichts, weil `gui/app.cpp` hier nicht
  baut). Wiederhergestellt **im Erzeuger**; ein kaputtes Byte `0x8f` in
  `DebugDump` (ja) behoben. Neu: `lint_i18n_erzeugt.py` (Erzeugtes == Erzeuger,
  jedes `Str::X` existiert).
* **rc538** `lint_gapp.py`.
* **rc549** `lint_headers.py`/`lint_link.py` riefen `g++` ungeschützt →
  Rückverfolg auf Windows, sobald shank Python installiert hatte.
  `tools/uebersetzer.py` (sucht g++/clang++/c++/cl, MSVC mit `/Zs`),
  `lint_fremdprogramm.py`; `build.bat` meldet je Prüfer die eigene Ursache.

### Ereigniseditor (Werte, Helfer, Knöpfe)

* **rc541** Helferzeilen (`help*`) wurden nie geleert → schleppten Werte in
  den nächsten Befehl.
* **rc544/546** „Re-Evaluate" setzt jetzt auch Feld 0 zurück (Schleife begann
  bei `from + 1`) und die Helferwerte — **klappt den Ausdrucksmodus aber nicht
  mehr zu** (rc545 war zu viel).
* **rc545** `editorRowW` wurde nie zurückgesetzt (Breite des vorigen Befehls).
* **rc552** „Save as" wie im Original (Knopf `Save as`, Kurzhilfe
  `Save As (Alt+V)`); **Revert überlebt das Schließen** (`editorWarExpr`,
  `argForParam` bekommt sonst das alte `Kind::Expr` zurück).
* **rc553** Helferzeilen werden aus dem Feldwert zurückgewonnen
  (`get(`, `tag(`, `random(`); Zerlegung doppelt in `tests/helferwerte.cpp` —
  **beide gemeinsam ändern**.
* **rc554** Klapplisten schreiben live ins Feld, **nur** wenn dort schon ein
  Ausdruck der passenden Form steht.
* **rc555** Helferzeilen je Befehl/Feld in den Einstellungen
  (`helfer.if/0=FLOAT|SET_PARM5||ORIGIN|0.000|1.000`), gespeichert bei Ok
  **und** Abbrechen. Laut shank jetzt gut.

### Klappliste (Combo)

* **rc541** Rollen zum aktuellen Eintrag erst im zweiten Bild (im ersten
  klemmt ImGui `SetScrollY` auf `ScrollMax` = 0).
* **rc542** Suchfeld bleibt oben: Liste in eigenem `BeginChild` mit **von Hand
  gerechneter Höhe** (sonst Rückkopplung Fenster↔Kind).
* **rc543** Untergrenze eine Zeile statt drei; Messung erst im zweiten Bild.
* **rc544** Liste ging nicht mehr zu → im Kind explizit `CloseCurrentPopup()`.

### Fenster, Teiler, Lage

* **rc545** Programm ließ sich bei offenem Editor nicht schließen: alle
  Dialoge rufen jedes Bild `OpenPopup` und schlossen sich gegenseitig; die
  Speichernfrage hat jetzt die Ebene für sich. Laut shank behoben. Mechanik: `confirmQuit()` lehnt `WM_CLOSE`
  ab, solange `askSaveOpen` steht, und die Frage kam nie zum Beantworten —
  `WM_CLOSE` wurde für immer abgelehnt. Nur noch der Taskmanager half.
* **rc555–557 Events-Teiler:** Tabelle übernimmt die Spaltenbreite nur beim
  Einrichten (`IsInitializing`); beim 1485er-Startfenster griff die
  Untergrenze 336 und blieb für immer. Lösung: **Fensterbreite in die
  Tabellenkennung** (`##spaltenEvents#<breite>`). Laut shank behoben.
* **rc558 Actions-Teiler:** `splitButtons` wurde nie aus einem Zug
  zurückgeschrieben. Beide Seiten fragen jetzt `ImGuiTable::ResizedColumn`
  (war in rc499 schon da und ging verloren). Laut shank behoben.
* **rc559** Fensterlage/-zustand über `WINDOWPLACEMENT`
  (`windowPlacement=showCmd l t r b`, gesichert bei `WM_CLOSE`, minimiert wird
  nicht wiederhergestellt). Laut Protokoll `gesetzt ja`.

### Zentrierung und „Shaken" des Ereignisfensters

| rc | Befund → Änderung |
|---|---|
| 543 | Fenster wanderte je Wachsen um einen halben Punkt → Versatz gerundet (erzeugte später den Zweierkreis, rc561) |
| 546 | Fenstertitel macht das Fenster 300+ breiter als der Inhalt → Reihe gegen Fensterbreite mitteln |
| 547 | Das schaukelte gedämpft → natürliche Breite einmal messen (`editorNatW`) |
| 549→551 | Dauerkreis von 13 Bildern über die Zellbreite → `zelleW = min(avail, bestellt)` |
| 560 | Drehpunkt 0.5/0.5 mit `ImGuiCond_Always` während des Einschwingens |
| 561 | Zweierkreis 1299↔1300 (285 Änderungen) → Totband 2 Punkte |
| 562 | `SetWindowPos` innerhalb `Begin` → aufgeschoben über `SetNextWindowPos` |
| 563 | Rücken erst bei einem Bild stabiler Größe → keine `gewachsen`-Zeilen mehr |
| 564 | Fenster faltete sich über 4 Bilder auf → **Breite je Befehl lernen** (`breite.if/3=1299`) |
| 565 | Schlüssel mit Feldzahl (zwei `if`-Fassungen) |
| 566 | Zellbreite im Einschwingen rechnen statt messen; `editorSettle = 4` |
| 567 | Messzeile `Helfer x/y` für den Rest (siehe offen) |

Das dreifeldrige `if` ist laut rc566-Protokoll sauber.

### Befunde aus dem ersten Lauf mit echter Karte (rc549, `md_am_sith`)

* **GPU-Zeiten** (erstmals echt, dank rc531): Karte deckend 0,916 ms,
  gemischt 0,068, **Effekte gemischt 5,271** (größter Posten, mehr als alles
  andere zusammen), Mover 0,158/0,024, Figuren 1,433, Glühen 0,403.
* **Ladezeit** der Mission 3827 ms, davon **„Texturen suchen" 2441 ms**
  (9253 Shadereinträge, 102 Texturen; die eine fehlende ist `noshader`,
  harmlos).
* **Drei Skelette fehlen:** `models/players/_humanoid_ani/_humanoid.gla`,
  `_humanoid_bdroid/…`, `_humanoid_md/…`. 27 von 31 Figuren haben ein Modell;
  die vier ohne (`pog1`, `tik1`, `sta2`, `player`) haben keinen NPC-Spawner.
  Nicht weiter untersucht.
* **Aufrufzähler** passen nicht — siehe offene Punkte.

### Kleines

* **rc548** Kopierknopf neben dem Animationsnamen (Modellvorschau), bestätigt.

---

## 4. Was noch offen ist

### Unmittelbar

1. **Einfeldriges `if` rückt noch ein (rc566-Protokoll).** Mitten im Öffnen
   springt `bestellt` von 408 auf 314 und die Höhe von 415 auf 263 — der
   **Helferblock verschwindet**, ohne dass Expr!/Revert gedrückt wurde. Dazu
   eine langsame Drift `FeldEnde 1474, 1475, … 1483`. rc567 schreibt
   `Helfer x/y` in die Spaltenzeile: kippt `x`, ist es der Ausdrucksmodus;
   ändert sich `y`, wechselt die Feldzahl unter dem laufenden Editor
   (`editorFields`). **Auf dieses Protokoll warten, nicht raten.**
2. **`AENDERUNGEN.md` enthält den rc558-Eintrag doppelt** (zwei Fassungen
   untereinander). Die zweite, ältere löschen.
3. **Aufrufzähler im Bericht (rc549/550-Protokoll, unverifiziert):**
   `Aufrufe 938` = Karte 494 + Mover 444; `Effekte 0` und `Figuren 0`,
   obwohl deren GPU-Zeiten 5,271 ms bzw. 1,433 ms betragen. Vermutlich werden
   die Zähler dieser beiden Wege nicht hochgezählt.
4. **Sprache:** shank muss einmal im Menü Deutsch wählen (seine Datei sagt
   `"en"`).

5. **Unbeantwortete Frage aus rc541:** Auf shanks Bildern stand vor „Ok"
   `free ( variablename )` über dem `if`, danach nicht mehr. `replaceAt`
   löscht keine Geschwister; wahrscheinlich zwei Stände. Nie bestätigt — war
   es derselbe Stand, ist es ein eigener Fehler.
6. **Kennungen, noch nicht von shank geprüft:** Löschen über einem offenen
   Block, Einfügen aus der Ablage, Duplizieren eines offenen Blocks.
   (Bestätigt: Einfügen darüber, Rückgängig, Mehrfachauswahl-Ziehen.)
7. **Bekannte Einschränkung:** Ein Block, der in einen **anderen Reiter**
   gezogen wird, bekommt dort eine neue Kennung; sein Aufklappzustand geht
   verloren.
8. **`tools/icarus_regeln.py`** (8 Bedeutungsregeln aus dem Handbuch: Aufgabe
   in Schleife, `wait` ohne `do`, Aufgabe ohne wartbaren Befehl, `sound` auf
   unwartbarem Kanal, `get` auf Unerklärtes, `else` ohne `if`, doppelter
   Aufgabenname …) gegen den **Movie-Duels-Bestand** laufen lassen, bevor
   etwas nach `validate.cpp` wandert. Raven-Skripte: null Befunde.
9. **Empfohlen, nicht gebaut** (`BEFUNDE-rc532.md` §3): Absturzfänger —
   `SetUnhandledExceptionFilter` + `RtlCaptureStackBackTrace` + Modulbasis
   ins Protokoll, hier mit `addr2line` gegen DWARF auflösen (unter Linux
   nachgestellt). Minidumps nur mit Vorsicht: DbgHelp ist nicht threadsicher.
10. **Empfohlen, nicht gebaut:** den echten D3D11-Weg kopflos mit **WARP**
    auf einem GitHub-Actions-Windows-Läufer prüfen (rendern, byteweise
    vergleichen, ohne shank). Erster Schritt: Wegwerfprogramm, ein Dreieck,
    PNG als Artefakt.

### Bedienfragen (notierte Schuld, nicht angefasst)

11. Ablegen **neben** einer Makrozeile landet im Makrorumpf und verschwindet
   bei zugeklapptem Makro (rc539). Korrekt, aber überraschend.

### Aus der GPU-Übergabe (rc429), weiterhin offen

12. Türen verschwinden hinter durchsichtigen Flächen (Zeichenreihenfolge).
13. `worldMatrices` ohne `vorrang` → falsche Haltung.
14. Effekte sehen anders aus als im Rasterer (halbe Auflösung u. a.).
15. `mapOnGpu` wird nicht gespeichert.
16. `tcMod transform` (13 Vorkommen, sechs Werte).
17. Feuerfontäne `mustafar/volcano.efx`; `head_glass`- und Kraftfeld-Textur
    fehlen in den Archiven.

### Technisch

18. GCC 13.3 `-Wmaybe-uninitialized` in `src/mapview.cpp` (`lmCache`) —
    Falschmeldung, bewusst nicht behoben.
19. `UEBERGABE.md` im Paket ist veraltet (rc429) — dieses Handout ersetzt sie
    für alles außer den GPU-Punkten.
20. `BrokenPipeError`, wenn man die Ausgabe eines Prüfers abschneidet
    (`| head`). In `build.bat` tritt das nicht auf.

---

## 5. Was online nachgeschlagen wurde — und was es brachte

**Dear ImGui**
* `BeginChild`: Größe 0 = „restliche Elterngröße"; `EndChild` immer rufen;
  selbstwachsendes Fenster + selbstwachsendes Kind = „auto-fit feedback
  loop" (ImGui-Aufgabenliste) → Listenhöhe von Hand (rc542).
* `Selectable`/`MenuItem` rufen `CloseCurrentPopup()` nur, wenn das
  **aktuelle Fenster** ein Popup ist — im Kind nicht (rc544).
* Tabellen: „Fixed Columns can be enlarged as needed" und „querying the
  content width … would create a feedback loop" (rc551).
* `TableSetupColumn` übernimmt die Breite nur unter
  `if (table->IsInitializing)` (rc557).
* `ImGuiTable::ResizedColumn` ist die einzige Schranke, die fragt statt rät
  (rc558; `imgui_internal.h`).
* Aufgabenliste: „using `SetWindowPos()` inside `Begin()` … reacts a very
  ugly glitch. We should just defer the `SetWindowPos()` call" (rc562).
* `SetNextWindowPos(..., pivot 0.5/0.5)` mittet auf einen Punkt (rc560).
* ocornut: bei `AlwaysAutoResize` ist die Inhaltsbreite im ersten Bild
  undefiniert — „You can't really have both … set an explicit width via
  `SetNextWindowSize(ImVec2(400,0))`" (rc564).
* Fehlerbericht: Flackern zwischen zwei Stellen durch eine Größenschranke
  aus `GetContentRegionAvail()` (rc562).

**Windows / MSVC**
* `WINDOWPLACEMENT` + `Get/SetWindowPlacement`: `length` **muss** gesetzt
  sein; `showCmd` + `rcNormalPosition` decken maximiert samt
  Wiederherstellgröße ab; Windows rückt Fenster außerhalb des Bildschirms
  selbst zurecht (rc559).
* `cl /Zs` = reine Syntaxprüfung ohne Ausgabedateien (rc549).

**Qt / Baumansichten** (rc535): Für „Modell wird umgebaut, Klappzustand
soll überleben" ist die Regel eine **eindeutige Kennung je Eintrag**;
indexbasierte Sicherung gilt als Sonderfall. Ein Qt-Fehlerbericht beschreibt
genau unser Problem: Daten im Zustand „nachher", gemerkte Verweise im Zustand
„vorher" → tote Verweise.

**Werkzeuge und Bau** (rc530/531): ccache — ohne `time_macros` fällt es nur
vom Direkt- aufs Präprozessor-Verfahren zurück (Korrektur einer eigenen
Falschaussage); WARP als spezifikationstreuer Software-Rasterer, seit
Windows 8 in der DXGI-Aufzählung; D3D11-Gerät braucht kein Fenster;
LTO/PGO bewusst **nicht** jetzt; RenderDoc programmgesteuert; Binning/SIMD
für den Rasterer. Einzelheiten in `RECHERCHE-gpu2-runde2.md` (liegt neben dem
Paket, samt `messungen-runde2.tar.gz`).

**ICARUS-Handbuch** (rc531/532): die acht Bedeutungsregeln (s. offen,
Punkt 8); Vorschlagsschwelle für `V004` gemessen — Levenshtein ≤ 2 gibt
null Fehlvorschläge von acht, ≤ 3 schickt `SET_STUCKSCRIPT` zu
`SET_ATTACKSCRIPT`.

**Original-BehavEd**
* Zeichenketten aus `BehavEd.exe`: Knopf `Save as`, Kurzhilfe
  `Save As (Alt+V)` (rc552).

---

## 6. Muster, die sich wiederholt haben — bitte beherzigen

* **Messen, dann ändern.** Jede Änderung, die aus einer Protokollzahl kam,
  hat gehalten. Jede aus einer Vermutung hat mindestens eine Runde gekostet
  (Zentrierung: dreimal daneben, Teiler: zweimal). Bei Unklarheit **eine
  Messzeile einbauen** und shank laufen lassen.
* **Eine Messung im ersten Bild lügt** bei selbstwachsenden Fenstern
  (rc542 „Fensterhöhe 24", rc546 „Reihe 0").
* **Zustand des vorigen Editors sickert durch** — drei Mal (`help*`,
  `editorRowW`, Helferwerte). Beim Öffnen **alles** zurücksetzen, was nicht
  gewollt überdauern soll.
* **Erzeugte Dateien nie von Hand ändern**, und vor dem Erzeugen prüfen, ob
  sie noch zum Erzeuger passen (`lint_i18n_erzeugt`).
* **Werkzeuge müssen auf dem Rechner des Benutzers laufen** oder sauber
  aussteigen — ein Rückverfolg sieht aus wie ein Programmfehler.
* **Zwei Wege, die dasselbe tun, auseinanderlaufen lassen:** Events-Teiler
  hatte einen Rückweg, Actions nie (rc558); Events-Schranke ging verloren,
  und der Prüfer merkte es nicht, weil seine Fünfzehn-Zeilen-Regel nicht
  griff.
* **Name allein ist kein Schlüssel** — `if`, `set`, `use`, `move`, `wait`
  gibt es zweimal (rc565).

---

## 7. Wo was liegt

```
gui/app.cpp            drawEditor (~Z. 660-1700), comboEdit (~7400-7900),
                       openEditorFor (~8500), Hauptlayout/Teiler (~11200-11550),
                       saveSettings, gemerkteFensterlage/merkeFensterlage
gui/app_view3d.cpp     Modellvorschau (Kopierknopf ~4640), Figuren, Bericht
gui/main_win32.cpp     WM_CLOSE (Fensterlage sichern), Start (Fensterlage laden)
gui/app_internal.h     struct App - alle editor*/help*/dbg*-Felder
include/bhed/settings.h  helfer, editorBreite, windowPlacement, split*
src/settings.cpp       key=value je Zeile; Praefixe helfer. / breite.
src/edit.cpp           Document: replaceAt, cloneAll, moveInto, Kennungen
src/tree.cpp           buildTree, Makro-Kindzeilen
tools/gen_i18n.py      EINZIGE Quelle aller Texte
tools/uebersetzer.py   Uebersetzersuche fuer die Pruefer
AENDERUNGEN.md         jede Runde mit Begruendung und Zahlen
```

---

## 8. Wenn du weitermachst

1. Paket entpacken, Prüflauf aus Abschnitt 2 einmal grün laufen lassen.
2. Doppelten rc558-Eintrag in `AENDERUNGEN.md` entfernen.
3. Auf shanks nächstes Protokoll mit `Helfer x/y` warten und Punkt 1 aus
   Abschnitt 4 **daran** entscheiden.
4. Danach die Aufrufzähler (Punkt 3) im Code nachsehen — klein und messbar.
5. GPU-Punkte erst danach, in der Reihenfolge aus `UEBERGABE.md`:
   `vorrang` → Zeichenreihenfolge → `mapOnGpu` speichern → Effekte.


---
---

# Teil B — die ganze Geschichte

## 9. Das Projekt in einem Absatz

behaved ist ein Nachbau von Ravens **BehavEd** (ICARUS-Skripteditor für Jedi
Knight: Jedi Academy), gedacht für die Filmsequenzen im **Movie-Duels**-Mod.
Er liest und schreibt die Skripte **bytegleich** (1509 von 1510 Raven-Skripten;
der eine Unterschied ist Ravens eigener Tippfehler), zeigt sie als Baum wie
das Original, und spielt sie auf der geladenen Karte ab: Kamera, Figuren mit
Skelettanimation, Mover, Effekte, Klang. Das Schwesterwerkzeug **efxed**
(EffectsEd-Nachbau) lieferte Teile, u. a. den geprüften Fadenpool.

Grundsätze aus `ARCHITEKTUR.md`, die bis heute gelten:

1. Die Befehlsliste wird **erzeugt** (`.bhc` + Engine-Kopfdateien), nicht
   gepflegt.
2. Der Editor **weist nichts ab**: Unbekanntes wird angemerkt und überlebt
   den Rundlauf.
3. **Die Datei bleibt, wie sie war.** Leerzeilen und Kommentare sind Knoten.
4. **Falten ist Anzeige, nicht Format** — Makros und Blöcke werden im Baum
   gefaltet, die Datei bleibt flach.
5. **Jede Regel hat eine Gegenprobe.**
6. Bei Abweichung zwischen GPU-Weg und Software-Rasterer **hat der Rasterer
   recht**.

---

## 10. Alle Sitzungen der Reihe nach

| Chat / Quelle | Runden | Datum | Schwerpunkte |
|---|---|---|---|
| (Anfang, u. a. im efxed-Chat vorbereitet) | bis rc61 | Anfang Aug. | Grundgerüst, Leser, Baum. **Nicht einzeln belegt** |
| „Modernes Behave" | rc62–rc94 | bis 13.08. | Kartenansicht, Figuren laden, Missionslader, Hautwahl, Ebenen-Explorer nach 3ds Max, Entity-Anklicken nach SomaZ/BSP-Entity-Edit, Absturzfälle (Escape, Map-Reiter) |
| `AENDERUNGEN.md` | rc96–rc129 | Mitte Aug. | Boden verschwand, eigene Werte in Klapplisten, Ziehen aus der Ereignisliste, `angles`-Schlüssel, 90-Grad-Frage, Tastenbelegung, Mover mit Ort, `move`/`rotate`, Signale steuern die Zeit, Effekte gezeichnet, `camera SHAKE/FOLLOW`, Sequenzuhr `bhed/clock.h`, Hologramme, `.md3`, Detailprotokoll (rc126) |
| „Behaved chat nummer 2" | rc133–rc164 | bis 17.08. | Rasterer: Flimmern (3 Anläufe), paralleles Sammeln, **PVS 1,5–4×**, Cohen-Sutherland (18× weniger Arbeit), 15 → 77 fps. Kameragizmo mit Bahn nach Max-*Trajectories*. **Reiter und 1–4-Felder-Raster** (park-and-swap). Alt+Pfeil, Suchen, „Nein" beim Speichern, SET-Liste lief über den Rand |
| `HANDOUT-behaved-rc199.md` | rc165–rc199 | | Geteilte Ansicht, Gizmo dreht **wirklich**, Raumschiff 7557 Einheiten daneben (origin-Brush), PAN-Richtung über 180°, Zeitleiste zoom/ziehen, **Laden 8631 → 1327 ms** |
| `HANDOUT-behaved-rc229.md` | rc200–rc229 | | Kamera-Irrweg rc206–rc211, erst ein **Kameraprotokoll** entschied es (rc213). Karte je Reiter, mehrere Skripte nebeneinander |
| „behaved chat 3" | rc230–rc241 | bis 23.08. | Ereigniseditor-Zentrierung (schon damals 3 Anläufe), `.icarus` war **erfunden** → `.txt`/`.ibi`, `ownMap` gegen geliehene Karte, 8-Skripte-Grenze weg (md_ga_jedi hat 24), Fenster „Skripte im Zusammenspiel" (`src/interplay.cpp`), **Kamerapunkte als `tag(...)`-Marken** aufgelöst, Drehverlauf `sin(90°·t)` |
| (Handout rc245 erwähnt) | rc242–rc289 | | Animationsüberblendung (`SETANIM_BLEND_DEFAULT` 100 ms), `SET_WATCHTARGET` ab rc245. **Nur mittelbar belegt** |
| „Recherche zu C++ und Game-Entwicklung" | rc290–rc346 | bis 26.08. | Fußrutschen, **Bildzwischenmischung** (behaved mischte nie zwischen Animationsbildern), Kopfdrehung `SET_LOOK_TARGET` (±64°), SSE2 −22 %, Figuren-Fäden **zurückgenommen** (3× langsamer), Sichtkegeltest −48 %. EFX: `usePhysics`, `impactFx`, `deathFx`, `emitFx`, `attachedModel`, `Decal`. **Tonmischer** neu (waveOut spielte nacheinander). 44 echte Karten als Prüfbestand |
| `AENDERUNGEN.md` | rc347–rc372 | | Shader: `glow`, `tcGen environment`, `blendFunc` gerechnet, animMap, tcMod-Kette, Himmel, depthWrite/Func, Autosprite, Effektfarbe. rc372 = **Stufe 0 des GPU-Umbaus** |
| „JKA und JKO Shader-Quellcode analysieren" | rc363–rc429 | bis 02.09. | **Direct3D-11-Weg** komplett: Karte, Mover, Effekte, Himmel, Figuren, Glühen, **300–1000 fps statt 12–15**. Viele `0xC0000005`, HLSL-Feldlagen, Pufferlebensdauer. `UEBERGABE.md` |
| `AENDERUNGEN.md` | rc430–rc501 | | GPU-Zeitmarken, Licht, Mischarten, Bericht im Protokoll, Debugschicht, RenderDoc-Knopf, Speichern/Übersetzen wie BehavEd, **Spaltentabelle** (rc473 ff.), Teilerbreiten, `ResizedColumn` (rc499) |
| „Behaved GPU umbau" | rc502–rc530 | bis 07.09. | Feld-/Helferzeilen ausgerichtet, Klappfenster springen nicht mehr, Befehle **aus Blöcken heraus**, Makro ≠ Block, 26 Prüfer. Regel von shank: **alles Gemessene ins Protokoll** |
| „C++ und GPU-Optimierung lernen" | (rc530) | 07.09. | Recherche GPU 2.0: Vulkan kopflos (llvmpipe), HLSL→SPIR-V **nicht transponieren**, ccache 268×, Fadenpool 6 µs statt 170 µs, D3D11 WARP für kopfloses Prüfen |
| diese Transkriptdatei | rc530–rc536 | 10.09. | siehe Teil A |
| dieser Chat | rc537–rc567 | 10.09. | siehe Teil A |

Die Protokolldatei `/mnt/transcripts/2026-09-10-05-47-32-behaved-gpu2-rc530-rc536.txt`
ist die einzige, die als Volltext vorliegt. Die übrigen Chats sind über die
Chatsuche erreichbar (Titel wie oben).

---

## 11. Was in allen Sitzungen nachgeschlagen wurde

### OpenJK-Quelltext — jedes Mal ein echter Fund

| Datei | Befund |
|---|---|
| `cg_camera.cpp` | **Eine** globale Kamera; MOVE linear; PAN nimmt bei Richtung 0 den kürzeren Weg; FOLLOW = exponentielle Glättung |
| `IcarusImplementation.cpp` | **Eine** globale Signaltabelle `m_signals` |
| `Q3_Interface.cpp`, `bg_misc.cpp:501` | Drehen mit `TR_NONLINEAR_STOP` → Anteil `sin(90°·t)`; nur für Winkel belegt, nicht für Ort |
| `bg_public.h:176` | `SETANIM_BLEND_DEFAULT 100` (Kommentar sagt 350 — veraltet) |
| `tr_ghoul2.cpp:1357, 1531 ff.` | Bildmischung linear auf **lokalen** Matrizen, zwölf Werte, kein Quaternion |
| `cg_players.cpp:3319`, `NPC_stats.cpp:2019` | Kopfdrehung ±80°/1,25 = **±64°**, Körper bleibt stehen |
| `tr_image.cpp` | Bildtabelle nach Namen, klein, ohne Endung → Ladezeit |
| `qfiles.h` | RBSP Fassung 1 für JKA und JKO |
| `codeJK2/icarus/` | Interpreter und Tokenizer; IBI-Version 1.57 |
| `FxScheduler.cpp`, `FxPrimitives.cpp` | Effektregeln (aus efxed übernommen) |

### Bibliotheken und Werkzeuge

* **ImGui:** alle Punkte aus Teil A Abschnitt 5; dazu aus früheren Sitzungen:
  `BeginCombo`-Semantik, `SplitterBehavior`, Ziehen zwischen Kindfenstern
  (Quelle muss ein Element sein), Nutzlast wird sofort kopiert, zwei
  Ziehquellen am selben Element schließen sich aus, feste Knopfhöhe schneidet
  bei 144 dpi ab, `AlwaysAutoResize` kennt die Endgröße erst nach mehreren
  Bildern (schon rc228 bemerkt), Versionsunterschied 1.93.0-WIP gegen
  1.92.9b-docking.
* **Direct3D 11:** `IMMUTABLE` braucht Anfangsdaten; eine Ressource zugleich
  Ein- und Ausgang wird stillschweigend NULL; Renderziel und Tiefenpuffer
  müssen gleich groß sein; Zeitmarken mit `Disjoint`; WARP für kopfloses
  Prüfen.
* **Windows:** `IFileOpenDialog`; `WINDOWPLACEMENT`; `cl /Zs`; MSVC-interner
  Fehler −1073741819.
* **3ds Max** (als Vorbild für Bedienung): *Trajectories* (Bahn nur bei
  Auswahl, weiße Quadrate = Schlüssel), Rotate-Gizmo (Trackball, Wechsel auf
  Linear Roll bei ~90°), Bezugssystem je Umformung eigen, Ebenen-Explorer
  (Alt-Klick = solo).
* **SomaZ/BSP-Entity-Edit** — wie man Entities anklickt.
* **Laine/Karras** — Software- gegen GPU-Rasterisierung (Faktor 2–8).
* **GPU 2.0-Recherche** (`RECHERCHE-gpu2*.md`): C++26-Stand, Bauzeit
  (Ninja → ccache → PCH → Unity), ThreadSanitizer nicht unter MSVC, Tracy mit
  D3D11-GPU-Zeiten, Fadenpool mit Arbeitsdiebstahl, Map/Discard gegen
  UpdateSubresource, Vulkan-Prüfschichten.
* **Original-BehavEd:** `BehavEd.exe`-Zeichenketten, `ICARUS_Manual.doc`
  (von 2002, vor JKA — sechs `SET_`-Felder umbenannt, acht entfernt), elf
  SDK-Kopfdateien, `behaved.bhc`. Alle 13 Datendateien im Paket sind
  bytegleich mit dem SDK.

### Wo Suchen nichts brachte

Bei `0xC0000005`, falschen Bildern und den meisten Layoutfehlern lag die
Ursache jedes Mal im eigenen Code. Nützlich sind gezielte Fragen nach
**Regeln einer Schnittstelle**, nicht nach Symptomen.

---

## 12. Stolpersteine aus allen Sitzungen

Nach Häufigkeit. Jeder davon hat mehrere Runden gekostet.

1. **Aus dem Symptom erschlossen statt gemessen.** Kamera rc206–211 (vier
   Pakete), Zentrierung rc233/234 und rc545–547, Teiler rc490–498 und
   rc555–556. Regel seit rc229: **nach zwei gescheiterten Erklärungen ist der
   dritte Versuch eine Messung, kein Umbau.**
2. **Zwei Fassungen desselben Dings laufen auseinander.** Seitenleiste,
   `selectByPath` dreimal, Verkleinern dreimal, FOLLOW an einer von drei
   Stellen, `brauchtEcken`/`vbZuKlein`, `skipSky` in `buildMesh` gegen
   `buildModelMesh`, Events- gegen Actions-Teiler. **Rechnungen sofort
   herausziehen.**
3. **Etwas messen, das man selbst verschoben hat** (rc233/234, rc546).
4. **Grün, aber nicht neu übersetzt.** Alte `.o`-Dateien oder ein altes
   `/tmp/probe` liefen weiter. Vor dem Übersetzen löschen, danach prüfen,
   dass die Datei existiert.
5. **Proben, die den Fehler festschreiben** statt ihn zu fangen
   (`scenetest.cpp` Kopfdrehung, Annahmen über md_ga_jedi statt über den
   Zeichner). Eine Probe beschreibt Verhalten, nicht die Beispielkarte.
6. **Dateiformate nie von Hand erfinden** (geschweifte gegen eckige Klammern,
   `.icarus`).
7. **`efx::Range{a,b}` setzt `set=false`** → `pick()` gibt 0; drei
   Paketstände lang war alle Geschwindigkeit null. Lebensdauern stehen in
   Millisekunden.
8. **HLSL-Feldlagen** nie schätzen — `tools/lint_cbuffer.py` fragen. Wer
   `b0` schreibt, muss wissen, wer danach zeichnet.
9. **Anonymer Namensraum** in beide Richtungen (Deklaration drin/Definition
   draußen, oder Definition drin/Aufrufer draußen → LNK2019). Kein `continue`
   und kein `return` mit offenem `PushID`.
10. **Namens- gegen nummernbasierte Zwischenspeicher:** nach Namen ist über
    Kartenwechsel sicher, nach Nummer nicht.
11. **Ungefragte Zusätze** (rc501: ein ziehbarer Teiler, den niemand wollte,
    wurde zur Fehlerquelle).
12. **Erzeugte Dateien von Hand ändern** (rc537).

---

## 13. Arbeitsweise mit shank und DH

* shank baut und testet unter Windows, meldet knapp („geht nicht", „und?",
  „alles gut?") und schickt Protokolle und Bilder. **Er erwartet, dass die
  Protokolle gelesen werden**, nicht Rückfragen zu Dingen, die darin stehen.
* **Alles Gemessene gehört ins Protokoll**, nicht nur auf den Schirm — er kann
  Bildschirmwerte nicht weiterreichen.
* Online nachschlagen ist ausdrücklich erwünscht; früher abgewunken zu haben,
  hat Runden gekostet.
* Keine Arbeit auf ihn abwälzen, keine beschönigenden Zusagen, keine
  unbestellten Zusätze.
* Bei echter Unklarheit direkt fragen — dann aber mit konkreten Wahlmöglichkeiten.
* **Hochladen:** zweimal kamen Dateien nicht an (Uploadordner leer). Dann
  einzeln neu hochladen lassen und nicht auf altem Stand weiterarbeiten.
* shank schickt meist `behaved.log` **und** `behaved-detail.log`; das
  Detailprotokoll kommt manchmal nur als Datei, nicht als lesbarer Text —
  dann aus `/mnt/user-data/uploads/` lesen.
* Prüfbestand: 44 echte Karten, 4 Raven-Skripte im Rundlauf,
  `md_ga_jedi` als ergiebigster Fall (507 Entities, 24 Skripte, 64 Figuren,
  Kamera nur über Marken), `md_am_sith` für die GPU-Messungen.
