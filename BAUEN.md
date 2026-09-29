# behaved bauen

Es gibt noch **keine Oberfläche** — die existiert bisher nur als Entwurf
(`behaved_ui.html`). Was hier gebaut wird, ist der fertige Kern plus ein
Befehlszeilenwerkzeug und die Prüfstände.

## Wenn der Übersetzer selbst abstürzt

```
INTERNER COMPILERFEHLER in ...\CL.exe
error MSB6006: "CL.exe" wurde mit dem Code -1073741819 beendet.
```

`-1073741819` ist `0xC0000005` — eine **Zugriffsverletzung im Übersetzer**.
Nicht unser Programm ist abgestürzt, sondern MSVC beim Erzeugen des Codes.
Beobachtet mit **MSVC 14.50** (Visual Studio 18).

`build.bat` versucht das jetzt selbst: schlägt der erste Durchlauf fehl,
startet es einen zweiten mit `BEHAVED_MSVC_SAFE_CODEGEN=ON`. Das setzt `/Ob1`
— weniger Einbetten von Funktionen, also kleinere Einheiten in der
Codeerzeugung, und genau dort stürzt sie ab.

Hilft das nicht, lässt sich die Datei eingrenzen. Der Absturz kam in einem
**Bündel**:

```
image_png.cpp  shaderscript.cpp  glm.cpp  gla.cpp
camtrack.cpp   scene.cpp         mission.cpp
```

MSVC übersetzt mehrere Dateien in einem Aufruf, deshalb nennt die Meldung
keine Zeile. Einzeln übersetzt fällt auf, welche es ist:

```
cmake --build build --config Release -- /p:CL_MPCount=1
```

Kostet Zeit, benennt aber die Datei — und damit lässt sich der Ausweg auf
diese eine beschränken, statt das ganze Programm langsamer zu machen.

## Fremde Warnungen schweigen

`third_party` wird als `SYSTEM` eingebunden. `minimp3.h` erzeugt bei `/W4` ein
Dutzend `C4244` („möglicher Datenverlust"); die sind nicht falsch, aber wir
können sie nicht beheben — es ist fremder Code, und ein Eingriff ginge beim
nächsten Abgleich verloren.

Warnungen, die niemand beheben kann, verdecken nur die eigenen.

## Windows, Visual Studio

```
build.bat
```

Das ist alles. Das Skript sucht CMake und Visual Studio, holt Dear ImGui,
falls es fehlt, baut, legt das Ergebnis in `out\` samt Befehlsmodell ab und
lässt am Ende die Prüfer und `ctest` laufen.

| Aufruf | was passiert |
|---|---|
| `build.bat` | bauen und prüfen |
| `build.bat nurbauen` | ohne Tests |
| `build.bat sauber` | `build` und `out` entfernen |

Die großen Prüfstände brauchen einen Ordner mit `.icarus`-Skripten. Lege ihn
unter `data\jascripts` ab, dann werden sie automatisch mitgenommen.

### Was im Skript absichtlich so steht

* **Kein `-G` für den Generator.** Bei efxed stand dort
  `"Visual Studio 17 2022"` fest verdrahtet, und auf einem Rechner mit VS 2026
  schlug jeder Aufruf mit *could not find any instance of Visual Studio* fehl.
  CMake findet die neueste Installation selbst.
* **Kein `-DCMAKE_BUILD_TYPE`.** Der Visual-Studio-Generator wählt die
  Konfiguration erst beim Übersetzen; CMake wirft die Variable sonst mit
  *Manually-specified variables were not used* zurück.
* **`call :main` statt geradeaus.** Springt etwas vorzeitig heraus, landet man
  hinter dem `call` — das Fenster bleibt offen und man kann die Meldung lesen.
* **Python wird der Reihe nach als `py`, `python3`, `python` probiert**, und
  jedes mit `--version` getestet. Windows legt unter
  `%LOCALAPPDATA%\Microsoft\WindowsApps` einen Platzhalter namens `python.exe`
  ab, der nur den Store öffnet; `where` findet ihn, der Aufruf schlägt fehl.
* **`lint_sources.py` läuft VOR dem Übersetzen.** Eine Quelldatei, die in
  `CMakeLists.txt` fehlt, fällt sonst erst beim Binden auf — nach Minuten, mit
  einer LNK2019-Meldung, die den Grund nicht nennt.

## Linux / MinGW

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBEHAVED_SCRIPTS=/pfad/zu/JAscripts
cmake --build build -j
ctest --test-dir build
```

Kreuzbau für Windows, ohne Laufzeitbibliotheken:

```
x86_64-w64-mingw32-g++ -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude \
    src/*.cpp tools/bhed_cli.cpp -o bhed.exe -static
```

## Optionen

| Option | Bedeutung |
|---|---|
| `-DBEHAVED_SCRIPTS=<ordner>` | Ordner mit `.icarus`-Dateien. Ohne ihn laufen nur die zwei Prüfstände, die keinen brauchen. |
| `-DBEHAVED_IBI=<datei>` | Eine echte `.ibi` als Vergleichsmaßstab (z. B. `start_cin2.IBI`). |
| `-DBEHAVED_TESTS=OFF` | Nur Bibliothek und Werkzeug bauen. |

## Was das Werkzeug kann

```
bhed info                          Befehlsmodell zeigen
bhed check   <datei|ordner>        gegen das Modell prüfen  (--strict zeigt Anmerkungen)
bhed tree    <datei>               Baumansicht  (--types  --g  --nofold)
bhed fmt     <datei>               neu formatieren
bhed compile <datei.icarus>        nach .ibi übersetzen
bhed decompile <datei.ibi>         zurück nach .icarus
```

## Prüfmaterial

Unter `data/fixtures` liegen echte Skripte, gegen die bei jedem Lauf geprüft
wird — lesen, schreiben, übersetzen, zurück, wieder übersetzen:

| Datei | warum |
|---|---|
| `intro_jedi.txt` | 549 Zeilen aus Movie Duels, Vorlage für die Baumansicht |
| `gonkability.txt` | **sieben `else`-Zweige, sechs Ebenen tief**, mit `$random()$` und `$get()$` in jeder Bedingung — solche Verschachtelung kommt in Ravens Bestand kaum vor |
| `cin2_jedi.txt`, `cin3_jedi.txt` | zwei weitere Missionsskripte |

## Prüfstände

`ctest` fährt acht Stück. Zwei brauchen nur die Daten im Baum, sechs den
Skriptbestand:

| Name | prüft |
|---|---|
| `gegenproben` | 22 Regeln, davon 6 Fälle, die *nicht* anschlagen dürfen |
| `bildschirmfoto` | 29 Signaturen und 15 Makros gegen ein Foto des Originals |
| `rundlauf` | lesen → schreiben ist bytegleich |
| `modell` | jeder Befehl gegen die `.bhc` |
| `bearbeiten` | Einfügen, Löschen, Klonen, Rückgängig, REM |
| `fuzzer` | 5000 Runden kaputter Eingaben |
| `fuzzer_bearbeiten` | 300 Runden Zufallsbearbeitungen |
| `ibi` | `.ibi` schreiben und lesen |

### Eine benannte Ausnahme

Der Rundlauf bekommt `seccam_planted.icarus` als erlaubte Abweichung
mitgegeben. In dieser Datei fehlt in Zeile 55 das Leerzeichen vor der
schließenden Klammer — Ravens Handarbeit, genau ein Fall unter 30158
Argumentlisten. Ausnahmen müssen einzeln benannt und begründet werden; es
gibt bewusst keinen Schalter, der Abweichungen pauschal durchwinkt.

## Oberfläche

Die Oberfläche liegt in `gui/app.cpp` und braucht **Dear ImGui 1.92** oder
neuer. ImGui ist nicht mitgeliefert:

```
git clone --depth 1 --branch v1.92.9b-docking https://github.com/ocornut/imgui.git
```

Fenster, Nachrichtenschleife und Grafikschnittstelle sind da:
`gui/main_win32.cpp` und `gui/backend_win32.cpp`. Gebaut wird die Oberfläche
nur, wenn ImGui angegeben ist:

```
cmake -S . -B build -DIMGUI_DIR=C:/pfad/zu/imgui
```

Geprüft wird außerdem, dass sie gegen **echtes** ImGui übersetzt:

```
IMGUI=/pfad/zu/imgui sh tools/check_gui.sh
```

Ohne diesen Schritt fällt jeder Schnittstellenwechsel von ImGui erst beim
Bauen unter Windows auf.

### Warum 1.92

In 1.92 wurde das Schriftsystem umgebaut, und für uns zählt vor allem eins:
**Glyphbereiche entfallen.** Die `GetGlyphRangesChineseSimplifiedCommon()`- und
`GetGlyphRangesJapanese()`-Funktionen sind veraltet; Zeichen werden bei Bedarf
nachgeladen. Bei efxed mussten wir die Bereiche noch vorbauen.

Zwei weitere Änderungen betreffen uns:

* `PushFont(font)` heißt jetzt `PushFont(font, size)` — die Größe ist Pflicht.
* `io.FontGlobalScale` heißt jetzt `style.FontScaleMain`, dazu kommt
  `style.FontScaleDpi`. Letzteres ist für deine 144 dpi genau richtig.

### Direct3D 11 nur mit verzögertem Laden

Zwei Grafikschnittstellen: Direct3D 11 und OpenGL 3.3, mit Rückfall. Beim
ersten Bindeversuch kam aber das hier heraus:

```
Feste Importe:  d3d11.dll   D3DCOMPILER_47.dll   dwmapi.dll   GDI32.dll ...
```

Fest gebunden lädt Windows das Programm **gar nicht erst**, wenn eine der
beiden fehlt — der Rückfall auf OpenGL käme also genau in dem Fall nie zum
Zug, für den er gebaut ist. ImGui warnt selbst davor, im Kommentar über
`D3DCOMPILER_DLL_A` in `imgui_impl_dx11.cpp`.

Deshalb: Direct3D wird nur mitgebaut, wenn verzögertes Laden möglich ist.
MSVC kann `/DELAYLOAD`, MinGW nicht — dort entsteht eine reine
OpenGL-Fassung. `tools/check_exe_imports.py` liest die Importtabelle der
fertigen `.exe` und schlägt an, sobald eine der heiklen DLLs fest drinsteht.

```
Feste Importe:
   dwmapi.dll   GDI32.dll   KERNEL32.dll   msvcrt.dll
   OPENGL32.dll   SHELL32.dll   USER32.dll
Importpruefung: ok
```

### DPI

Die Staffelung ist aus efxed übernommen, weil sie dort hart erarbeitet wurde:
`SetProcessDpiAwarenessContext` (Windows 10 1703) → `SetProcessDpiAwareness`
aus shcore.dll (8.1) → `SetProcessDPIAware` (Vista), jeweils über
`GetProcAddress`. Direkt aufgerufen lädt das Programm auf älteren Fassungen
nicht — es gibt keine Fehlermeldung, es startet einfach nicht.

## Makros

Die 15 Makros der `.bhc` (`standOnly`, `walkOnly`, `patrolRun` …) lassen sich
per Doppelklick einfügen. Sie werden ausgeklappt wie im Original:

```
//$"walkOnly"@5
set ( /*!*/ "SET_BEHAVIOR_STATE", /*!*/ "BS_DEFAULT" );
set ( /*!*/ "SET_WALKING", /*!*/ "true" );
...
```

Der Marker nennt Namen und Zeilenzahl, damit die Ausklappung im Baum wieder
zu einem Knoten gefaltet werden kann. Die `%r`-Felder der `.bhc` werden zu
`/*!*/` — gesperrt, weil sie zum Makro gehören.

**Gegengeprüft an Raven:** `walkOnly` hat im `.bhc`-Rumpf fünf Zeilen, und
Ravens Marker sagt `@5`. Unsere Ausklappung ist Zeile für Zeile dieselbe wie
in `taspir2/alora_chat_1.icarus` — das steht als Test fest.

### Im Baum

Belegt durch Bildschirmfotos: das Makro ist **ein Knoten** mit dem
`[]`-Symbol und dem Namen als Beschriftung, die erzeugten Befehle stehen als
**eingerückte Kinder** darunter, mit eigenem Aufklappkästchen. Die Datei
bleibt dabei flach — das Falten ist reine Anzeige.

Gesperrte Felder tragen dort **`(R)`** und behalten ihre Anführungszeichen,
auch bei ausgeschaltetem *Show Types*:

```
standOnly
  set   ( (R)"SET_BEHAVIOR_STATE", (R)"BS_DEFAULT" )
```

In der Datei ist dasselbe Feld `/*!*/`, in der `.bhc` der `%r`-Vorsatz.
Der Buchstabe steht vermutlich für *read-only*.

## Befehle in Blöcke einfügen

Der schwerste Punkt aus dem Fehlerbericht: `affect`, `if` und `task` ließen
sich **nicht befüllen**. `insertAfter` setzt immer *neben* den gewählten
Knoten — für einen Block braucht es `insertInto`.

Die Regel jetzt:

| Auswahl | wohin |
|---|---|
| Blockbefehl, **aufgeklappt** | hinein, an den Anfang |
| Blockbefehl, **zugeklappt** | daneben |
| sonst | hinter die Auswahl |

Mit einer Ausnahme: **ein leerer Block wird immer befüllt**, auch zugeklappt.
Zugeklappt-daneben ist bei einem *vollen* Block richtig — wer ihn eingeklappt
hat, will nicht unbemerkt hineinschreiben. Bei einem leeren gibt es nichts zu
verbergen, und „daneben" wäre dort fast nie gemeint; ein frisch eingefügtes
`affect` ist genau dieser Fall.

Nach dem Einfügen wird der Block aufgeklappt, sonst verschwindet der neue
Befehl darin und man hält es für wirkungslos. Das Rechtsklickmenü sagt vorher,
wohin es geht: *In den Block einfügen* oder *Dahinter einfügen*.

Der häufigere Weg braucht das alles nicht: eine Zeile **im** Block anklicken
und einfügen — `insertAfter` arbeitet auf den Geschwistern, es bleibt also
drin.

## Vorgabewerte

In der `.bhc` steht bei vielen Feldern der **Typname** als Platzhalter, nicht
ein Wert:

```
affect ( DEFAULT, AFFECT_TYPE )    "AFFECT_TYPE" ist kein Wert
set ( SET_TYPES, DEFAULT )         "SET_TYPES" auch nicht
wait ( 1000.0 )                    das hier schon
```

`makeNode` löste das auf, das **Neu-Auswerten im Editor aber nicht** — es
setzte den rohen Text ein, und so landete `AFFECT_TYPE` im Skript. Beide Wege
gehen jetzt über dieselbe Funktion `defaultValueFor`:

| Feld | Vorgabe |
|---|---|
| Typmenge | erster Eintrag: `FLUSH`, `SET_PARM1`, `ENABLE` |
| Zahl mit Platzhalter | `0.000` bzw. `0` |
| Vektor mit Platzhalter | `0.000 0.000 0.000` |
| Text | `DEFAULT` bleibt — Ravens Platzhalter für einen Entity-Namen |

## Tastatur im Baum

| | |
|---|---|
| ↑ ↓ | Auswahl bewegen, über die **angezeigten** Zeilen |
| Bild ↑ ↓ | zehn auf einmal |
| Pos1 / Ende | Anfang und Ende |
| ← → | Block zu- und aufklappen |
| Umschalt + ↑ ↓ | Auswahl erweitern |
| Eingabe | Editor öffnen |

Im Editor bestätigt **Eingabe** und **Escape** bricht ab. Die gewählte Zeile
wird in den sichtbaren Bereich gerollt — sonst wandert die Auswahl unsichtbar
aus dem Fenster.

## Wechsel der Auswahl im Event-Editor

Wer die Liste von `SET_PARM1` auf `SET_HEALTH` stellt, bekam zwar ein
Zahlenfeld statt eines Textfeldes — im Feld stand aber weiter `DEFAULT`. Die
Feldbeschreibungen wurden jedes Bild neu berechnet, die **Werte** nicht.

Die Regel dafür steht in den Kopfdateien und ist nachgemessen:

| Wechsel | Feldart | Vorgabe | |
|---|---|---|---|
| `SET_PARM1` → `SET_PARM2` | `str` → `str` | `""` → `""` | Wert **behalten** |
| `SET_PARM1` → `SET_HEALTH` | `str` → `int` | `""` → `"0"` | Wert **ersetzen** |
| `SET_PARM1` → `SET_ORIGIN` | `str` → `vec` | `""` → `"0.0 0.0 0.0"` | Wert **ersetzen** |

Also: nur zurücksetzen, wenn sich Feldart **oder** Vorgabe geändert hat. Wer
von `SET_PARM1` auf `SET_PARM2` wechselt, behält seinen getippten Wert — beide
wollen dasselbe. Wer auf `SET_HEALTH` wechselt, bekommt die 0, weil `DEFAULT`
dort schlicht falsch wäre.

**`Re-Evaluate` tut jetzt etwas.** Der Knopf war dauerhaft ausgegraut, dabei
ist genau das seine Aufgabe: alle Felder hinter der ersten Auswahl auf die
Vorgabewerte der aktuellen Wahl zurücksetzen — auch die, die man von Hand
geändert hat.

## Überschriften in den Auswahllisten

Die Kopfdateien enthalten dafür eine eigene Anweisung, ausdrücklich für
BehavEd hineingeschrieben:

```
//# #sep Parm strings
SET_PARM1 = 0,//## %s="" # Set entity parm1
```

37 davon gibt es — 9 in `Q3_Interface.h`, **27 allein in `anims.h`**. Jeder
der 261 `SET_TYPES` und aller 1543 Animationsnamen trägt damit eine
Überschrift, und die Klapplisten zeigen sie als Trennlinien. Ohne sie ist eine
Liste mit 1543 Einträgen eine Wand.

Dieselbe Familie wie `//# #eol`, das die Liste abschneidet — beides sind
Anweisungen, die Raven für sein eigenes Werkzeug in die Kopfdateien geschrieben
hat.

## Symbole

Die 40 Symbole stammen aus `BITMAP 132` von `BehavEd.exe` — 20 Paare, gerader
Index normal, ungerader für die ausgewählte Zeile. `tools/gen_icons.py`
erzeugt daraus `gui/icons_gen.h` und `.cpp`; sie werden als eigene Rechtecke
in ImGuis Schriftatlas gepackt.

Der Streifen liegt als **eigene Textur** in der Grafikschnittstelle, nicht als
Rechtecke im Schriftatlas.

Der erste Anlauf ging über `ImFontAtlas::AddCustomRect`. Das kostete einen
Absturz im ersten Bild:

```
ABSTURZ code 0xC0000005 bei 0x00007FF66DF9FB36
  lesend an Adresse 0x0000000000000008
  waehrend: Grafikschnittstelle starten: Direct3D 11
```

Zwei Gründe, beide im ImGui-Quelltext nachlesbar:

* Die Schnittstelle ist dort selbst als `[ALPHA] Custom Rectangles/Glyphs API`
  gekennzeichnet.
* `GetCustomRect()` dereferenziert `TexData` **ungeprüft** — das
  `IM_ASSERT(TexData->Width > 0 …)` steht vor jeder Nullprüfung.
* `io.Fonts->Clear()` beim Neubau der Schriften macht alle Rechteck-Kennungen
  ungültig, ohne dass man es merkt.

Eine eigene Textur hängt an nichts davon. Sie wird beim Wechsel der
Grafikschnittstelle neu angelegt — fällt Direct3D durch und OpenGL übernimmt,
zeigt die alte Kennung sonst auf eine bereits zerstörte Textur.

`tools/lint_imgui_alpha.py` schlägt an, wenn jemand wieder eine als
vorläufig gekennzeichnete ImGui-Schnittstelle benutzt, ohne es mit einem
`// imgui-alpha:`-Kommentar zu begründen.

## Der `.ibi`-Übersetzer

**1011 von 1011 Dateien bytegleich** gegen Ravens `IBIZE.EXE`, gemessen am
gesamten Story-Modus von Jedi Academy. Jede `.ibi` wird gelesen, nach
`.icarus` zurückübersetzt, wieder eingelesen, neu übersetzt und byteweise
verglichen — `tests/ibiround.cpp`.

Bis dahin waren es fünf Anläufe, und jeder hat eine Eigenheit des Formats
aufgedeckt, die man durch Lesen nicht findet:

| Stand | Fund |
|---|---|
| 62 % | Ein Vektor ist **kein 12-Byte-Glied**, sondern ein Marker mit vier Byte plus drei einzelne Floats. Und `TK_INT` kommt in 1011 Dateien **kein einziges Mal** vor — auch `wait ( 2000 )` steht als `2000.0f` da. |
| 88 % | Aufzählungswerte stehen als **float** da (`FLUSH` → `56.0`) — außer bei `sound`, wo der Kanal als **Text** steht. Alle 1111 `TK_IDENTIFIER` im Bestand gehören zu `sound`-Blöcken. |
| 90 % | `%.3f` verliert Werte, die sich damit nicht wiederherstellen lassen. Der Rückübersetzer schreibt jetzt so viele Stellen, wie der Wert braucht. |
| 99 % | Auch die **Vergleichsoperatoren** sind Marker mit vier Byte Inhalt. Ohne sie fehlen vier Byte, und ab da ist die ganze Datei verschoben. |
| 100 % | Ein Float darf **nur dort** zum Aufzählungsnamen werden, wo die Signatur eine `%i`-Typmenge vorsieht. Sonst wird aus `camera ( ZOOM, 53.640, 0 )` ein `camera ( ZOOM, ANGLES, 0 )` — weil 53 zufällig `TYPE_ANGLES` ist. |

Die Marker tragen ihre eigene Kennung als float: `TK_VECTOR` → `14.0`,
`ID_GET` → `36.0`, `ID_TAG` → `49.0`. Ausnahmen: `ID_RANDOM` trägt
`16777216.0` (`Q3_INFINITE`), die Vergleiche tragen `0.0`.

Damit ist `IBIZE.EXE` nicht mehr nötig.

## Klänge abspielen

Der `Do`-Knopf im Event-Editor spielt den Klang ab, dessen Name im Feld steht.
Gesucht wird in den `.pk3` der Spielordner und daneben ausgepackt.

**Mit der Rückfallregel der Engine:** im Skript steht meist `.wav`, in der
`.pk3` liegt aber eine `.mp3`. JKA sucht beide. Genau daran ist das originale
EffectsEd gescheitert — es importiert nur `PlaySoundA`, und das kann kein MP3.

Leser und Abspieler sind aus efxed übernommen:

* `src/sound.cpp` — WAV selbst gelesen (8/16/24/32 Bit PCM und 32-Bit-Float),
  MP3 über `third_party/minimp3.h`. Das ist die einzige fremde Datei im Baum;
  die Begründung steht im Kopf von `bhed/sound.h`.
* `gui/audio_win32.cpp` — Ausgabe über `waveOut`. **Dieser Teil ist
  ungeprüft**, wie schon bei efxed: es gibt keine Tonausgabe auf dem Rechner,
  auf dem der Quelltext entsteht.

Das Gerät wird mit **festem Format** geöffnet (44100 Hz, Stereo) und alles
darauf umgerechnet. Grund, aus efxed im Betrieb erarbeitet: `waveOut` lehnt
ungewöhnliche Raten je nach Treiber ab — eine 44100er-MP3 lief, eine
11025er-WAV nicht. Und ein Gerät, das je Klang neu geöffnet wird, würgt den
vorigen ab.

Der **Leser** ist dagegen vollständig geprüft: zwölf Proben, darunter
abgeschnittene Dateien, ein WAV mit erlogener Datenmenge (darf keinen
Riesenpuffer anfordern) und die Umrechnung 11025 mono → 44100 stereo.

## In `.pk3` blättern

*Datei → In .pk3 blättern…* listet Skripte und Karten aus allen `.pk3` eines
Spielordners. Doppelklick öffnet — eine `.ibi` wird dabei automatisch
zurückübersetzt, weil sie binär vorliegt.

Ein **Archivfilter** grenzt die Liste auf ein einzelnes `.pk3` ein. Bei einer
vollständigen Installation sind es vierzig Archive mit Hunderten Dateien —
ohne den Filter stehen `assets1.pk3` und der eigene Mod durcheinander.

Der Ordner wird über *Spielordner hinzufügen…* gewählt (irgendeine `.pk3`
darin auswählen); mehrere sind möglich, etwa `base` und der Mod-Ordner. Die
Archive werden alphabetisch gelesen, **spätere überdecken frühere** — genau
wie die Engine es macht. Gemerkt wird die Auswahl in den Einstellungen.

Gelesen wird nur das Inhaltsverzeichnis am Ende des Archivs, nicht jeder
Dateikopf. Der Auspacker (`inflate`, RFC 1951) ist aus efxed übernommen: rund
300 Zeilen, keine Fremdbibliothek, gegen bösartige Eingaben abgesichert.

**Mehrere Spielordner** sind möglich (etwa `base` und der Mod-Ordner). Sie
werden in den Einstellungen verwaltet — hinzufügen, entfernen, Reihenfolge
ändern —, und dort steht auch, wie viele Archive und Dateien in jedem
gefunden wurden. Die Reihenfolge zählt: spätere überdecken frühere.

### Zwei Bremsen, die gemessen wurden

Das kurze Hängen beim Einfügen kam von zwei Stellen, beide grob:

| | vorher | nachher |
|---|---|---|
| eine Datei auspacken | 18,3 ms | **0,004 ms** |
| Verzeichnis lesen | 39 ms | **2,6 ms** |
| Namen suchen | 0,46 ms | **~0** |

Der erste Anlauf las für **jede einzelne Datei das ganze Archiv** in den
Speicher. Bei 17 MB waren das 18 ms; eine echte `assets0.pk3` hat 350 MB.
Jetzt wird gezielt zum lokalen Dateikopf gesprungen und nur der Ausschnitt
gelesen. Beim Verzeichnis dasselbe: nur die letzten 64 KiB plus das
Inhaltsverzeichnis, nicht die ganze Datei. Und die Namenssuche läuft über
einen Index statt über alle Einträge.

Zwei Proben halten das fest, damit die Bremse nicht zurückkommt.

Geprüft an einem Archiv mit 1015 Dateien: **alle 1015 ausgepackt und byteweise
identisch** mit den Originalen. Dazu fünf Proben für kaputte Archive —
abgeschnitten an vier Stellen, verbogener Verzeichniszeiger, leere Datei. Ein
`.pk3` aus dem Spielordner ist keine vertrauenswürdige Eingabe.

## Die Kartenansicht

> **Stand rc568: der Software-Rasterer ist entfernt.** Gezeichnet wird nur
> noch ueber Direct3D 11 (`gui/gpumap_win32.cpp`). Massstab bei Bildfehlern
> ist das Spiel und der OpenJK-Quelltext, nicht mehr `mapview.cpp` - dort
> steht nur noch, was beide Wege teilten (Kamera, Texturen, Stapelzustaende,
> `figurKnochen`). Was unten ueber den Rasterer steht, ist Geschichte.

Ein **Software-Zeichner**, kein OpenGL und kein Direct3D. Der Grund ist nicht
Bequemlichkeit, sondern gemessen: das Programm hat zwei Grafikschnittstellen.
Ein Zeichner für die Karte müsste es zweimal geben — zweimal Shader, zweimal
Puffer, und jeder Fehler nur auf einem der beiden Wege sichtbar.

### Divisionen aus dem inneren Kern heraus

Im Zeichenkern standen **zwanzig Divisionen**, deren Nenner je Dreieck
feststehen: die Tiefen `sz[k]` und die Dreiecksfläche. Bei perspektivisch
richtiger Interpolation sind das bis zu zehn Divisionen **je Bildpunkt** — für
Tiefe, Lightmap und Textur.

Eine Division kostet 20 bis 40 Takte, eine Multiplikation drei. Die Kehrwerte
werden jetzt einmal je Dreieck gerechnet.

**Das ist nicht mehr bitgleich**, und das muss man wissen: `a/b` und
`a · (1/b)` runden unterschiedlich. Gemessen an einem 640×400-Bild:
**4 Byte von 1024000 unterscheiden sich, jedes um genau 1** — Rundung im
letzten Bit, unsichtbar. Der Gewinn liegt bei rund 10 %.

Die Probe auf Fadenunabhängigkeit gilt weiter: alle Fäden nehmen denselben
Weg, also bleibt das Bild bei jeder Fadenzahl gleich.

### Warum Boden und Decke fehlten

Der Zeichner verwarf ein Dreieck **ganz**, sobald **eine** Ecke näher als die
Nahebene lag:

```cpp
if (z < 1.0F || z > opt.farPlane) { behind = true; break; }
```

Bei Böden und Decken ist das fatal: sie bestehen aus wenigen, sehr großen
Dreiecken, und sobald man darauf steht, liegt eine Ecke hinter der Kamera —
die Fläche verschwindet. Gemessen an echten Karten sind je Blickrichtung
betroffen:

| | teilweise hinter der Nahebene |
|---|---|
| `yavin2` | 71 Dreiecke |
| `duel_kamino_lp` | 559 |
| `md_tfoaj_jedi` | 163 (1,4 %) |

Richtig ist **Beschneiden**: das Dreieck wird an der Ebene zerschnitten, der
vordere Teil bleibt. Dabei entstehen ein oder zwei Dreiecke — bei zwei muss
die Umlaufrichtung erhalten bleiben, sonst kippt die Rückseitenerkennung und
die Fläche verschwindet doch.

Beschnitten wird **vor** der Projektion, im Blickraum: dort ist die Ebene eine
einfache Bedingung `z >= nah`, und alle Vertexangaben — Texturkoordinaten,
Lightmap, Farbe, Normale — lassen sich linear mischen.

Die Fernebene darf weiter ganz verwerfen: dort ist ohnehin nichts zu sehen.

### Der Zeichner arbeitet in zwei Durchgängen

Die erste Fassung gab jedem Faden einen Streifen — und **jeder Faden lief über
alle Dreiecke** und warf die fremden per Rechteckabgleich weg. Damit wird die
Projektion (drei Skalarprodukte und eine Division je Ecke) so oft gerechnet,
wie es Fäden gibt: bei 54553 Dreiecken und 32 Kernen **5,2 Millionen
Eckentransformationen statt 164000**.

Gemessen sah das so aus:

| | vorher | jetzt |
|---|---|---|
| 720p, 1 Faden | 2,43 ms | 2,00 ms |
| 720p, 4 Fäden | **4,63 ms** | **1,93 ms** |

Vier Fäden waren also **langsamer als einer**. Auf einem Kern ist Gleichstand
das Optimum; der Gewinn zeigt sich erst auf einer Maschine mit mehreren.

Richtig ist die übliche Bauform eines Kachelrasterisierers, und genau davor
warnt auch die Literatur zum Thema („alle Arbeitsgruppen verarbeiten alle
Dreiecke — das skaliert nicht"):

1. jedes Dreieck **einmal** projizieren und in die Streifen einsortieren, die
   sein Rechteck berührt
2. je Streifen nur noch die einsortierten Dreiecke rastern

Die Streifen überschneiden sich nicht, also braucht es keine Sperren.

**Das Ergebnis ist bytegleich** — bei 1, 2, 4 und 8 Fäden dieselbe Prüfsumme
wie vor dem Umbau. Zwei Proben halten das fest, eine davon mit sieben Fäden
auf 240 Zeilen, wo die Streifen nicht aufgehen.

### Der Himmel wird zeilenweise gefüllt

Die Hintergrundfarbe hängt **nur von der Zeile** ab, wurde aber je Bildpunkt
neu gerechnet — samt drei Fließkommamultiplikationen. Bei 1080p sind das zwei
Millionen Rechnungen je Bild für 1080 verschiedene Werte.

Jetzt einmal je Zeile rechnen, vier Byte setzen und die Zeile mit `memcpy`
aus sich selbst verdoppeln. Das Ergebnis ist bytegleich.

### Was die Geschwindigkeit ausmacht

Gemessen an `yavin2` mit 54732 Dreiecken, 1280×720, **ein Kern**:

| | |
|---|---|
| erste Fassung | 20,1 ms |
| ohne Rückseiten | **3,0 ms** |

Faktor 6,7 aus einer einzigen Änderung. Der Grund steht in der Messung: aus
der Kartenmitte sind nur **27 % der Dreiecke der Kamera zugewandt**. Die
übrigen 73 % wurden gezeichnet und gleich wieder überschrieben — und zwar die
teuersten, weil die Rückseiten naher Wände große Bildflächen bedecken.

Dazu zwei weitere Hebel:

* **Faden je Bildstreifen.** Jeder Faden schreibt nur in seinen Streifen,
  deshalb ohne Sperren. Auf 32 Kernen entsprechend schneller.
* **Nur zeichnen, wenn sich etwas geändert hat.** Ein Editor steht die meiste
  Zeit still. Bewegt sich die Kamera nicht und ändert sich das Skript nicht,
  wird das letzte Bild weiterverwendet — im Ruhezustand kostet die Ansicht
  nichts.

### Oberflächenflaggen — ein Fehler, der sich versteckt hatte

Die Werte stehen in `code/game/surfaceflags.h` von OpenJK. Meine erste Fassung
hatte dort **geratene Zahlen**:

| | falsch | richtig |
|---|---|---|
| `SURF_NODRAW` | `0x80` | `0x00200000` |
| `SURF_SKY` | — | `0x00002000` |
| `SURF_SKIP`, `SURF_HINT` | `0x200`, `0x100` | **gibt es in JKA nicht** |

Dass die Ansicht trotzdem richtig aussah, lag allein an einer zweiten Prüfung
über die Shadernamen (`system/clip`, `system/trigger` …). Die Flaggenprüfung
hat **nie** etwas gefunden — ein Fehler, der sich hinter einer zweiten,
funktionierenden Prüfung versteckt hat.

Sichtbar wird der Unterschied bei `duel_kamino_lp.bsp`: statt einer werden
jetzt **20 Hilfsflächen** erkannt.

### Himmelsflächen

Sie umschließen die Karte wie einen Kasten und verdecken beim Blick von außen
alles darin — in `yavin2` sind das die großen grauen Wände in der Übersicht.
Sie werden standardmäßig weggelassen; der Schalter *Himmel* blendet sie ein,
was zum Beurteilen einer Kameraeinstellung im Freien nützlich ist.

Erkannt über `SURF_SKY` **und** den Namen: `textures/skies/cloudlayer_yavin`
trägt die Flagge nicht, gehört aber dazu.

### Texturen

Zwei Wege, wie es die Engine auch macht: erst die Datei zum Shadernamen
direkt suchen (`textures/yavin/stone` → `.jpg`, `.png`, `.tga`), und nur wenn
das nichts ergibt, in den `.shader`-Skripten unter `scripts/` nachsehen,
welches Bild dahintersteckt. Von den Shaderstufen zählt die **erste mit einem
echten Bild** — `$lightmap` und `$whiteimage` werden übersprungen.

Die Leser für TGA, JPEG und PNG sind aus efxed übernommen, alle selbst
geschrieben. Wichtig daran ist die **Formaterkennung am Inhalt, nicht an der
Endung**: in JKA-Mods liegen regelmäßig JPEG-Dateien mit der Endung `.tga`,
weil jemand sie umbenannt statt umgewandelt hat.

Die Bilder werden auf höchstens 256 Punkte verkleinert. Eine Editoransicht
braucht keine 1024er-Textur, und bei siebzig Shadern spart das ein Vielfaches
an Speicher. Der Schalter *Texturen* blendet sie aus — dann sieht man nur das
Licht, was zum Beurteilen einer Kameraeinstellung manchmal besser ist.

**Ungetestet gegen echte Spieldaten.** Hier gibt es keine `assets*.pk3`; die
Leser sind gegen selbstgebaute Dateien geprüft, die Zuordnung Shader → Bild
nur gegen selbstgeschriebene Skripte. Die Statuszeile nennt deshalb, wie
viele Texturen gefunden wurden — steht dort `0 von 70`, stimmt der
Spielordner nicht.

### Fadenkreuz, Namen, Kamerasprung

* **Fadenkreuz in der Bildmitte.** Die Mitte *ist* die Blickrichtung, aber
  ohne Marke schätzt man sie. Schwarz unterlegt, damit es auf hellem wie auf
  dunklem Grund sichtbar bleibt.
* **Entity-Name beim Überfahren.** 977 Zielnamen sind zu viele, um sie alle
  einzublenden; der unter dem Zeiger genügt — und das ist genau der, den man
  gerade in ein `SET_NAVGOAL` schreiben will.
* **Doppelklick auf eine `camera`-Zeile** bringt die Ansicht dorthin. Bei
  einem `MOVE` wird die Blickrichtung aus dem folgenden `PAN` dazugeholt,
  genau wie die Engine die Befehle nacheinander abarbeitet.

## Der anonyme Namensraum beim Aufteilen

`gui/app.cpp` ist in `app.cpp` + `app_view3d.cpp` + `app_internal.h`
aufgeteilt. Dabei gibt es eine Falle, die mich **zweimal** erwischt hat:

Ein anonymer Namensraum ist je Übersetzungseinheit ein **eigener**. Eine
Funktion, die in `app_internal.h` deklariert und in `app.cpp` *innerhalb* von
`namespace { }` definiert wird, sind zwei verschiedene Funktionen — die
Definition liegt woanders als versprochen.

**Mit `-fsyntax-only` fällt das nicht auf.** Es fällt erst beim Binden auf,
und beides passierte hier erst nach dem Packen. Zwei Gegenmaßnahmen:

* `tools/lint_sources.py` vergleicht die Deklarationen in `app_internal.h`
  gegen die Definitionen im anonymen Namensraum von `app.cpp`.
* `tools/check_gui.sh` **bindet** jetzt wirklich, statt nur zu übersetzen.

Beide Prüfungen haben beim Einbau sofort echte Fälle gefunden:
`confirmDiscard`, `openScriptFromMemory` und zwei weitere.

## Mission laden

Ein Knopf, eine `.pk3` — und alles ist eingerichtet: Karte, Entities, Skript,
Figuren. Die Kette steht in den Daten, man muss sie nur ablaufen.

`mission.h` sucht sie zusammen. Zwei Regeln, beide an echten Daten gemessen:

**Die `.ent` hat Vorrang vor den Entities in der `.bsp`.** Movie Duels lässt
die `.bsp` unverändert und legt die Entities daneben — wer die `.bsp` nimmt,
bekommt die Entities der Originalkarte, ohne die Figuren der Mission.

**Eine Karte ohne Skriptverweise ist keine Mission.** In
`zzz_The_Fear_of_a_Jedi.pk3` liegen zwei `.bsp` mit **identischem Inhalt** —
gleiche Prüfsumme — unter den Namen `duel_jt_dojo` und `md_tfoaj_jedi`. Nur
die zweite hat eine `.ent` daneben, und nur die ist die Mission. Die erste ist
die Duellfassung derselben Karte.

Findet sich genau **eine** spielbare Mission, wird sie ohne Nachfrage geladen;
bei mehreren fragt ein Fenster. Das Archiv wird dabei als Spielordner
aufgenommen, damit Texturen und Modelle daraus gefunden werden.

## Was SomaZ' BSP-Entity-Edit besser macht — und was wir übernehmen

`SomaZ/BSP-Entity-Edit` ist der Maßstab für Kartenansichten in dieser Szene.
Der Abgleich mit seinem README und Quelltext:

| dort | bei uns |
|---|---|
| **Anklicken einer Entity im Bild** (`ogl_fbo.py`) | fehlte → **eingebaut** |
| WASD + Leertaste/C, rechte Maustaste zum Drehen | haben wir (rechts + WASD) |
| Vertexfarben | haben wir, dazu Lightmaps und Texturen |
| `H` versteckt die gewählte Entity | haben wir nicht |
| echte GL-Shader (`ogl_shader.py`) | Softwarezeichner, dafür ohne Grafikkarte lauffähig |

### Anklicken, auf unsere Art

SomaZ zeichnet jede Entity in einer eigenen Farbe in einen Nebenpuffer und
liest beim Klick den Bildpunkt. Das ist der richtige Weg für einen
GL-Renderer.

Unser Zeichner ist Software, da geht es einfacher **und angenehmer**: die
Marken merken sich beim Zeichnen ihren Bildschirmort, der Klick nimmt die
nächstgelegene innerhalb eines Radius. Ein Nebenpuffer verlangt einen Treffer
auf den Strich — eine Marke ist ein dünnes Kreuz.

### Der Ebenen-Verwalter für Entities

Nach dem Vorbild von **3ds Max' Layer Explorer**: eine Klasse ist eine Ebene,
das Auge davor schaltet sie um, ein Klick auf den Namen klappt ihre Mitglieder
auf. Eine Karte hat keine Ebenen — aber `classname` trennt genauso sauber.

Zwei Verhaltensweisen aus Max übernommen:

* **Alt-Klick auf ein Auge stellt die Gruppe allein.** In `csTools` heißt das
  *„Alt+click a button to solo it"*.
* **Eine versteckte Ebene versteckt ihre Mitglieder mit**, auch wenn deren
  eigenes Auge offen ist. Autodesk beschreibt es so: *„Any member objects of
  hidden layers show the icon to indicate that the layer controls the object's
  visibility."* Bei uns wird das eigene Auge dann ausgegraut.

Ausgeblendete Entities sind zugleich **nicht anklickbar** — die Marken
entstehen beim Zeichnen, und was nicht gezeichnet wird, kann nicht getroffen
werden. Das ist kein Sonderfall, sondern fällt so heraus.

### Und darüber hinaus

Ein reiner Entity-Editor zeigt die angeklickte Entity. Wir zeigen zusätzlich,
**wo das Skript sie benutzt** — mit Sprungknöpfen zu jeder Fundstelle.

Genau dafür sitzt eine Karte in einem Skripteditor: ein Wegpunkt allein sagt
wenig, die Zeile, die ihn anspricht, sagt alles.

## Wie eine Mission zusammenhängt

Die Karte selbst weiß **nichts** von Skripten. Die Verbindung steht in den
**Entities**:

```
maps/md_tfoaj_jedi.bsp
   └─ Entities (in der .bsp oder daneben als .ent)
        ├─ target_scriptrunner "intro" → usescript   md_tfoaj/intro_jedi
        ├─ target_scriptrunner "cin2"  → usescript   md_tfoaj/cin2_jedi
        └─ NPC_spawner                 → deathscript md_tfoaj/templeguard_defeated
                                            └→ scripts/md_tfoaj/intro_jedi.txt
```

Acht Schlüssel kommen dafür in Frage — `usescript`, `spawnscript`,
`deathscript`, `painscript` und vier weitere. Bei `md_tfoaj_jedi.ent` sind es
sieben Skripte, alle im selben Archiv, und alle sieben lösen auf.

`collectNames` sammelt sie beim Laden ein, ohne Dubletten: dasselbe
`deathscript` steht dort an drei Wachen und soll nur einmal in der Liste
stehen. Die Werkzeugleiste zeigt sie als Klappliste — ein Klick lädt die
Sequenz, ohne dass man den Pfad kennen muss.

**Die Endung sagt nichts.** In Movie Duels heißen die kompilierten Dateien mal
`.IBI`, mal `.ibi`; ob eine Datei kompiliert ist, wird deshalb am **Inhalt**
erkannt (`IBI` als erste drei Byte), nicht am Namen.

## Figuren in der Karte

`scene.h` macht für die Figuren, was `camtrack.h` für die Kamera macht: Ort,
Blickrichtung und Animation zu jeder Zeit.

| woher | was |
|---|---|
| `NPC_spawner` in Karte oder `.ent` | Startort, Blickrichtung, `NPC_type` |
| `set ( SET_ORIGIN, < x y z > )` | ein Sprung |
| `set ( SET_NAVGOAL, "name" )` | läuft zum Wegpunkt |
| `set ( SET_WALKING, true/false )` | Gehen oder Laufen |
| `set ( SET_ANIM_BOTH, "..." )` | Animation, wenn angegeben |

Die Geschwindigkeiten kommen aus `NPC_stats.cpp` der Engine:
**walkSpeed 90, runSpeed 300** Einheiten je Sekunde. 900 Einheiten sind im
Gehen also genau zehn Sekunden — das prüft eine Probe.

Gemessen an `intro_jedi.txt` mit `md_twj_jedi.ent`: **9 von 9 Figuren** mit
Startort, 8 mit `NPC_type`, dazu Movie-Duels-eigene Animationen wie
`BOTH_MD_CIN_43`.

### Vom NPC_type zum Modell

Dreistufig, und jede Stufe kann fehlschlagen:

```
NPC_type       "md_ani_tcwa"    aus dem NPC_spawner
-> playerModel "anakin_tcw"     aus ext_data/npcs/*.npc
-> Modell      models/players/anakin_tcw/model.glm
```

Fehlt eine Stufe, bleibt die Figur **ungezeichnet**. Geraten wird nichts —
eine falsche Figur wäre schlimmer als keine. Die Statuszeile nennt, wie viele
ein Modell haben.

### Die Haltezeit der Animation

`set ( SET_ANIM_HOLDTIME_BOTH, -1 )` heißt **einfrieren**, nicht wiederholen.
In Ravens 1510 Skripten steht das **510 von 721 Mal — 71 %**. Ohne diese
Unterscheidung laufen Gesten und Posen in Endlosschleife, wo sie stehenbleiben
sollten.

Belegt in `Q3_Interface.cpp`: der Befehl setzt eine Task-ID und kehrt mit
`return; //Don't call it back` zurück. Eine positive Zahl hält so viele
Millisekunden, `0` hebt das Halten auf.

Dazu zählt der **Einsatzzeitpunkt**: die Bildnummer wird ab dem `SET_ANIM_BOTH`
gerechnet, nicht ab Null der Zeitleiste — sonst beginnt jede Geste mittendrin.

### Drei weitere Befehle

| | Vorkommen | |
|---|---|---|
| `SET_RUNNING` | 304 | Gegenstück zu `SET_WALKING` |
| `SET_LOOK_TARGET` | 302 | die Figur dreht sich zum Ziel |
| `SET_CROUCHED` | 162 | anderer Grundzustand |

Beim `SET_LOOK_TARGET` gilt: es wirkt nur, **solange die Figur nicht läuft** —
unterwegs schaut sie in Laufrichtung, wie im Spiel. `NULL` löscht es wieder.

### Was das NICHT kann

Drei Grenzen, die man kennen muss:

* **Keine Wegfindung.** Die Engine schickt einen NPC über ein Netz aus
  Wegpunkten um Hindernisse herum; hier geht es geradeaus zum Ziel.

  Gemessen an `intro_jedi.txt` fällt das kaum ins Gewicht: die Strecken sind
  **66 bis 121 Einheiten**, also 0,7 bis 1,3 Sekunden Gehen — ein bis zwei
  Figurenbreiten. Auf so kurzen Wegen ist eine Gerade praktisch das, was auch
  die Wegfindung liefert. Bei längeren Wegen wäre es eine echte Näherung.
* **Keine Kollision.** Eine Figur läuft durch eine Wand, wenn der Wegpunkt
  dahinterliegt.
* **Die Laufanimation wählt im Spiel die Engine** nach dem
  Bewegungszustand. Hier wird sie aus `SET_WALKING` abgeleitet:
  `BOTH_WALK1`, `BOTH_RUN1`, im Stehen `BOTH_STAND1`.

Für das Abstimmen von Kamerafahrten gegen Sprache reicht das. Für eine
Vorschau, die dem Spiel bis auf den Zentimeter gleicht, nicht.

### Gezeichnet wird nach der Karte

`renderActors` benutzt den **Tiefenpuffer der Karte** — deshalb verschwindet
eine Figur richtig hinter einer Wand. Dafür gehört der Tiefenpuffer jetzt zum
`MapImage` statt in die Zeichenfunktion; nebenbei wird er damit nicht mehr je
Bild neu angelegt (bei 1080p 8 MB, gemessen 0,37 ms).

## Die Zeitleiste

`timeline.h` sagt, **wann** welcher Befehl läuft. `camtrack.h` sagt, was
daraus für die Kamera folgt: Ort, Blick und Blickwinkel zu jeder Zeit — auch
**zwischen** zwei Einstellungen, während eine Fahrt läuft.

Alles daran ist an `cg_camera.cpp` nachgemessen:

**Bewegung ist linear.** Zeile 1266:

```
vieworg[i] = origin[i]
           + ((origin2[i] - origin[i]) / move_duration) * (cg.time - move_time)
```

Kein Ein- und Ausschwingen, keine Glättung.

**Ein Schwenk nimmt den kürzesten Weg.** `CGCam_Pan` rechnet zwei Deltas aus
(`delta1` und `delta1 ± 360`) und nimmt das betragsmäßig kleinere. Von 350°
auf 10° sind das **+20°, nicht −340°** — wer das falsch macht, lässt die
Kamera einmal fast ganz herumfahren, und zwar sichtbar über die volle Dauer
des Schwenks. Sechs Proben halten das fest.

**Dauer 0 ist ein Sprung**, kein „sofort fertig interpolieren": `CGCam_Move`
setzt bei `!duration` direkt die Position. Wer hier durch die Dauer teilt,
bekommt eine Division durch null.

### Gegenprobe

`cin2_jedi.txt` ergibt über die Kamerabahn **5,4 s** — genau das „praktische
Ende", das die Zeitleiste unabhängig davon aus dem `wait ( 999999 )` errechnet
hat. Zwei getrennte Wege, dasselbe Ergebnis.

### Abspielen und Zeiger

*Abspielen* läuft die Bahn in Echtzeit ab, der Zeiger lässt sich ziehen, und
mit *Mitfahren* folgt die Ansicht der Kamera — mit ihrem Zoom und den
Kinoblenden. Der Balken unter dem Zeiger zeigt, wo etwas passiert: blau
Fahrten, gelb Schwenke, grün Zoom.

### Die Spuren

Eine Zeile je `affect`-Ziel. Das ist der Teil, für den die Zeitleiste
eigentlich da ist: man sieht, **wann welche Figur spricht**, und kann den
Schnitt darauf abstimmen.

Bei `intro_jedi.txt` sind das zehn Spuren — das Skript selbst mit 79
Kamerabefehlen, dazu `anakin1` (7 Klänge, erster bei 23,0 s), `barriss1`
(6 Klänge, erster bei 20,2 s) und die Wachen. Rosa sind Klänge, blau
Kamerabefehle, grün Bewegungen; beim Überfahren steht der Dateiname da.

### Klänge über ein Zeitfenster, nicht über einen Zeitpunkt

Beim Abspielen springt der Zeiger je Bild um rund 16 ms weiter. Wer prüft
„steht der Zeiger genau auf dem Klang", löst fast nie einen aus.

Die Probe dazu hat gleich einen echten Fehler gefunden: mit
`startMs > vorheriger Stand` ist die Bedingung beim **ersten** Bild `0 > 0` —
also nie wahr, und ein Klang bei 0 ms bliebe stumm. Das trifft den ersten Satz
jeder Sequenz. Der Stand beginnt deshalb bei **−1**.

Beim Ziehen des Zeigers wird der Stand mitgeführt, sonst käme alles
Übersprungene auf einmal.

### Durch die Kamera schauen

Der Schalter *Durch die Kamera* zeigt, was der im Baum gewählte
`camera`-Befehl sieht — mit **seinem** Blickwinkel und den Kinoblenden.

Drei Dinge waren dafür am Quelltext nachzumessen:

**Der Blickwinkel ist waagerecht.** ICARUS gibt in `camera ( ZOOM, ... )` den
waagerechten Winkel an, der Zeichner rechnet mit dem senkrechten. Die Engine
rechnet in `CG_CalcFOVFromX`:

```
x     = breite / tan( fov_x / 2 )
fov_y = atan2( hoehe, x )
```

Wer die Zahl direkt einsetzt, bekommt ein zu enges Bild — und zwar umso mehr,
je breiter das Fenster ist. Bei 4:3 ergibt `fov_x 90` genau **73,74°**
senkrecht, und das prüft eine Probe.

Das ist keine Kleinigkeit: in Ravens 1510 Skripten stehen **461
`ZOOM`-Befehle mit Werten von 6,3 bis 110 Grad**. Häufigster Wert ist 80.

**Die Vorgabe ist 90**, nicht 80 — `CAMERA_DEFAULT_FOV` in `cg_camera.h`.

**Die Kinoblenden sind ein Zehntel der Bildhöhe** je oben und unten
(`bar_height_dest = 480/10`). Wer eine Einstellung beurteilt, muss sehen, was
davon verdeckt wird — sonst setzt man den Kopf einer Figur genau in den
Balken.

### Rückwärts suchen, nicht vorwärts

Ort, Blick und Zoom stehen in **drei getrennten Befehlen**. Gesucht wird ab
der gewählten Zeile rückwärts: die Engine arbeitet die Befehle nacheinander
ab, also gilt der letzte `MOVE`, der letzte `PAN` und der letzte `ZOOM` vor
dieser Stelle.

Wer nur die gewählte Zeile ansieht, bekommt bei einem einzelnen `ZOOM` keine
Position. Gemessen an euren Skripten: **alle 30 `ZOOM`-Befehle** in
`cin2_jedi`, `cin3_jedi` und `intro_jedi` finden rückwärts ein `MOVE`.

### Kamera-Gizmos

Aufgebaut wie eine **Zielkamera in 3ds Max**: ein Körper mit Objektiv, davor
das Ziel mit Kreuz, dazwischen eine Linie, und der Sichtkegel als Pyramide.

Warum ein Ziel und nicht nur eine Richtung: in ICARUS bewegt
`camera ( MOVE, … )` den Ort und `camera ( PAN, … )` den Blick — zwei
getrennte Befehle mit eigenen Dauern. Das **ist** eine Zielkamera; Körper und
Ziel lassen sich unabhängig bewegen.

Die Größe hängt vom Abstand ab, sonst wäre das Gizmo aus der Ferne ein Punkt
und aus der Nähe bildfüllend. Die im Baum gewählte Kamera ist gelb, die
übrigen blau. Gezeichnet wird direkt ins fertige Bild, mit derselben
Projektion wie die Karte.

### Entities in der Ansicht

Nicht alle, sondern die, deren Namen im Skript auftauchen:

| | | |
|---|---|---|
| `waypoint_navgoal` | grün | steht in `SET_NAVGOAL` |
| `target_position` | orange | steht in `SET_LOOK_TARGET` |
| `target_scriptrunner` | violett | startet ein Skript über `usescript` |
| `NPC_spawner` | gelb | die Figuren, die `affect` anspricht |

Gemessen an `md_twj_jedi.ent` aus Movie Duels: 26 Wegpunkte, drei Blickziele,
sechs Skriptstarter, 25 NPC-Punkte. Und ein Befund, der die Kameraarbeit
bestätigt — **kein einziges `ref_tag`**. Die Kameras dieser Karte stehen
tatsächlich als feste Zahlen im Skript, genau wie die Messung an Ravens
Skripten es nahegelegt hat.

### Warum die Karte zu dunkel war

Ich hatte angenommen, die Engine hebe Lightmaps um Faktor 2 an
(`r_mapOverBrightBits`). Nachgesehen in `tr_init.cpp`:

```
r_mapOverBrightBits  Vorgabe "0"
r_overBrightBits     Vorgabe "0"
r_gamma              Vorgabe "1"
```

Die Engine hebt **gar nicht** an — die Karte sieht im Spiel genau so dunkel
aus, wie die Lightmap hergibt. In einem dunklen Hangar erkennt man dann fast
nichts, und für einen Editor ist das unbrauchbar.

Deshalb zwei Regler statt einer festen Zahl:

* **Helligkeit** (0,5× bis 6×) — wie stark die Lightmap angehoben wird.
* **Grundlicht** (0 bis 0,5) — hebt die dunkelsten Stellen an, ohne die hellen
  zu verändern. **0 ist genau wie im Spiel.**

Dazu eine **Bildratenanzeige**: die Zeit fürs Zeichnen der Karte, geglättet,
mit der Millisekundenzahl beim Überfahren. Gemessen wird nur das Zeichnen —
ImGui zeichnet auch dann, wenn die Karte unverändert bleibt.

### Die Fernebene kam aus einer festen Zahl

20000 war zu knapp: `duel_kamino_lp` misst **34816 Einheiten** in der Breite,
Diagonale 48229. Sie kommt jetzt aus der Kartengröße.

Und derselbe Fehler wie an der Nahebene steckte am anderen Ende: ein Dreieck
wurde schon verworfen, wenn **eine** Ecke dahinterlag. Ein großer Boden, der
bis zum Horizont reicht, verschwand damit ganz. Jetzt nur noch, wenn **alle
drei** dahinterliegen.

### Überlauf wird proportional heruntergerechnet

Die Engine macht das in `R_ColorShiftLightingBytes`, mit einem Kommentar, der
es genau sagt:

```
// normalize by color instead of saturating to white
if ( (r|g|b) > 255 ) {
    max = groesster der drei;
    r = r * 255 / max;  g = ...;  b = ...;
}
```

Überschreitet **ein** Kanal 255, werden **alle drei** mit demselben Faktor
heruntergerechnet — die Farbe bleibt erhalten, statt ins Weiß zu kippen.

Gefunden hat das erst eine echte Movie-Duels-Karte: `md_tfoaj_jedi.bsp` hat
Lightmaps mit im Mittel **185 von 255**, davon 60 % über 200. Mit einzelnem
Abschneiden wurde die halbe Karte reinweiß. Auch bei `yavin2` sinkt der Anteil
reinweißer Bildpunkte von **3,0 % auf 0,0 %**.

### Vier Vermutungen zum fehlenden Boden — alle vier gemessen, alle vier falsch

Zur Frage „warum sehe ich nie den Boden" habe ich die naheliegenden Ursachen
der Reihe nach **gemessen** statt sie einzubauen:

| Vermutung | Messung |
|---|---|
| Unsere Namensliste versteckt mehr als `SURF_NODRAW` | **0** zusätzlich versteckte Shader in drei Karten |
| Normalen falsch gelesen | alle Einheitslänge, Mittel zeigt nach oben |
| `cull twosided` je Shader fehlt uns | in Kartenshadern praktisch nicht benutzt (130× nur in Effekten) |
| Fernebene verwirft ganze Dreiecke | **0** betroffene Dreiecke — sie wird aus der Raumdiagonale berechnet |

Damit ist die Ursache **nicht gefunden**. Statt weiter zu raten gibt es jetzt
einen Schalter **„Beidseitig"**: erscheint ein Boden nur damit, ist seine
Umlaufrichtung verdreht — und das lässt sich in einem Klick feststellen statt
in vier Messungen.

### Die Figurenmodelle wurden einmal geladen und nie wieder

`loadActorModels` läuft nur, wenn die Zahl der Einträge nicht zur Zahl der
Figuren passt:

```cpp
if (g_app->actorAssets.size() != g_app->scene.actors.size()) {
    loadActorModels();
}
```

Beim Zurücksetzen nach einem neuen Spielordner hatte ich nur die Merker
gelöscht (`tried = false`) — **die Zahl blieb gleich**. Also lief es nie
wieder, und die Modelle blieben aus, auch nachdem der richtige Ordner
dazugekommen war. Jetzt wird die Liste geleert.

Derselbe Grund ließ den alten Hinweis stehen: `actorWhy` wird in
`loadActorModels` gesetzt, und wenn das nicht läuft, bleibt der Text leer und
die Oberfläche fällt auf den pauschalen Satz zurück.

**Kein Zweig endet mehr ohne Grund.** Sieben Fälle, jeder mit eigenem Text —
auch „das Skript hat keine affect-Blöcke", was gar kein Fehler ist.

### Die NPC-Liste wurde einmal gelesen und nie wieder

`npcMapRead` wurde beim ersten Bedarf gesetzt und **nie zurückgenommen**.
Kommt der Ordner mit den Modellen erst danach dazu — oder wird ein Archiv als
Spielordner aufgenommen, wie es „Mission laden" tut — bleibt die Liste leer,
und keine Figur bekommt ein Modell.

Von außen sieht das aus, als suche das Programm nur in der einen gewählten
`.pk3`. Tatsächlich durchsucht `readFromArchives` immer alle Spielordner; die
Liste, die den NPC-Typ überhaupt erst einem Modell zuordnet, war nur veraltet.

`rescanGamePaths` verwirft jetzt alles, was aus den Spielordnern stammt: die
NPC-Liste, die geladenen Figurenmodelle und den Grundtext. Wer die Quellen
ändert, bekommt die Ableitungen neu.

**Das Protokoll nennt jetzt auch die Wege.** Je Spielordner, wie viele
`.npc` gefunden wurden, und bei einem fehlenden Modell alle drei Namen:

```
kein Modell: md_kanan -> kanan -> models/players/kanan/model.glm
```

Der NPC-Typ steht im Skript, der Modellname in der `.npc`, das Modell in einer
dritten Datei. Ohne alle drei ist nicht zu sagen, wo es hakt.

### Warum keine Figurenmodelle da sind — im Klartext

Statt „Keine Modelle gefunden — Spielordner hinzufügen" steht jetzt der
tatsächliche Grund. Die Kette hat vier Glieder, und jedes kann reißen:

```
NPC_type im Skript → Eintrag in den .npc → models/players/<x>/model.glm → lesbar
```

Der Hinweis nennt, welches gerissen ist und wie oft — „7 NPC-Typen stehen
nicht in den 412 Einträgen der .npc-Dateien" sagt etwas, „Spielordner
hinzufügen" sagt nichts, wenn Spielordner da sind.

### Vertexfarben bekommen denselben Faktor wie Lightmaps

Bei uns standen Vertexfarben auf **80 %** des Helligkeitswerts, Lightmaps auf
100 %. Ein geratener Faktor.

Die Engine macht keinen Unterschied: `R_ColorShiftLightingBytes` wird auf
Vertexfarben **genauso** angewandt wie auf Lightmapdaten — dieselbe Funktion,
in `tr_bsp.cpp` an den Zeilen 424, 508 und 590.

Der Fehler fällt auf, weil Karten **beides mischen**: bei `yavin2` sind 59 %
der Dreiecke vertexbeleuchtet. Die waren gegenüber den lightmapbeleuchteten
daneben um ein Fünftel zu dunkel — und ein Helligkeitssprung zwischen
benachbarten Flächen liest sich wie falsche Beleuchtung.

### Was der Detailregler tut

Er unterteilt **nur gebogene Flächen** (Bézier-Patches). Gemessen an `yavin2`:
12900 Flächen, davon 28 gebogen.

| Detail | Dreiecke |
|---|---|
| 1 | 51643 |
| 4 | 54553 |
| 10 | 70849 |

Bögen, Rohre und Kuppeln werden runder; gerade Wände ändern sich nicht. Höhere
Stufen kosten also nur dort, wo es etwas zu runden gibt.

### Woher das Licht kommt

Zwei Quellen, beide schon in der `.bsp`:

* **Lightmaps** für Flächen mit einer Nummer ≥ 0.
* **Vertexfarben** für Flächen mit `-3` — das ist `LIGHTMAP_BY_VERTEX` aus
  `q_shared.h`, kein Fehlerwert. Bei `yavin2.bsp` betrifft das **59 % aller
  Dreiecke**. Wer sie nur grau zeichnet, verliert mehr als die halbe
  Beleuchtung; der erste Versuch sah genau so aus.

Angehoben wird um Faktor 2 — das ist `r_mapOverBrightBits` des Spiels. Mehr
brennt helle Karten aus: bei `duel_kamino_lp.bsp` liegt die mittlere
Helligkeit der Lightmaps bei 39,7 von 255, bei `yavin2` bei 94,9.

### Bedienung

Der Umschalter über der linken Spalte wechselt zwischen *Ereignisse* und
*Karte*. Während man Kameras setzt, braucht man die Befehlsliste nicht — und
der Skriptablauf bleibt daneben sichtbar, dort landet ja der neue Befehl.

Bedienung wie im Ansichtsfenster von **3ds Max**:

| | |
|---|---|
| mittlere Maustaste | schieben |
| `Alt` + mittlere Maustaste | umkreisen |
| Mausrad | zoomen |
| rechte Maustaste + `W A S D` | fliegen, `Q E` hoch und runter, Umschalt schneller |

Warum nicht nur Fliegen: zum **Setzen** einer Kamera will man ein Ziel
umkreisen und sehen, wie es aus verschiedenen Richtungen aussieht. Fliegen ist
gut, um irgendwo hinzukommen, aber umständlich, um sich um etwas herum zu
bewegen.

Der Drehpunkt liegt eine feste Strecke vor der Kamera. Beim Umkreisen bleibt
er **stehen** — das ist die Eigenschaft, an der solche Steuerungen sonst
scheitern: man dreht sich, und das Ziel wandert langsam aus dem Bild. Zwei
Proben halten es fest (Drehpunkt nach 24 Drehungen unverändert, Blickrichtung
beim Zoomen unverändert).

Der Umschalter ist eine **Modusleiste am linken Rand**, wie bei g2c: zwei
Knöpfe statt Auswahlkreisen, weil die Umschaltung die ganze Spalte wechselt
und nicht bloß eine Einstellung darin. Die Breite kommt aus dem längsten Text
— eine feste Zahl passt bestenfalls für eine Sprache. **Kamera hier einfügen** schreibt zwei Befehle ins
Skript: `camera ( MOVE, < x y z >, 0 )` für den Ort und
`camera ( PAN, < p y r >, < 0 0 0 >, 0 )` für die Blickrichtung — in der
Winkelreihenfolge der Engine.

## Bedienung getrennt von der Ansicht

Die Regler standen im selben Bereich wie das Bild. Dadurch wanderte die
Kamera, sobald man den Zeitregler zog oder durch die Animationsliste rollte —
das Mausrad zoomte, statt zu blättern.

Die 3D-Ansicht liegt jetzt in einem **eigenen Bereich** mit
`ImGuiWindowFlags_NoScrollWithMouse`, und die Maus wirkt nur dort. Darunter,
durch einen Trenner abgesetzt, die Bedienung: Angaben zum Modell, Auswahl der
Animation, Schleife, Tempo, Zeitleiste.

### Was unter der Ansicht steht, und was ins Menü gehört

Unter dem Bild nur, was man beim Ansehen einer Animation **dauernd** braucht:
Abspielen, Filter, Auswahl, Wiederholen, Tempo — alles in **einer** Zeile —
und darunter die Zeitleiste. Zwei Zeilen, damit dem Bild möglichst wenig Höhe
verlorengeht.

Texturen, Kappen und die Angaben zum Modell stehen im Menü **Ansicht**: das
stellt man einmal ein und fasst es dann nicht mehr an.

### Wiederholen ist ein Haken, keine Dreifachwahl

Vorher stand dort *aus der .cfg / immer / einmal*. Das legte nahe, es gäbe
eine zweite Quelle für Animationen — **die gibt es nicht.** Die
`animation.cfg` ist die einzige, und sie legt je Abschnitt fest, ob er
wiederholt (`BOTH_WALK1` hat `loopFrame` 0, `BOTH_DEATH1` hat −1). Der Haken
**erzwingt** nur; aus heißt: wie in der `.cfg`.

Wiederholt wird ab `loopFrame`, nicht ab dem Anfang: manche Animationen haben
einen Vorlauf, der nur einmal laufen soll.

Die Zeitleiste zeigt Bild **und Sekunden** — das ist die Zahl, die beim
Abstimmen gegen eine Kamerafahrt zählt.

### Die Rahmen standen nicht auf einer Höhe

`beginGroupBox` zieht für seine Überschrift eine **Textzeilenhöhe** ab, meine
Werkzeugleiste war aber eine **Knopfzeile** — die ist höher. Mit einer festen
Zahl endet das zwangsläufig verschoben. Die Höhe wird jetzt gemessen:
Cursorstand vorher und nachher, Differenz.

### Ein ImGui-Fehler, der eine echte Lücke zeigte

```
Programmer error: 2 visible items with conflicting ID!
```

Die Dateiliste benutzte den **Dateinamen** als Kennung. `models/players/kyle/
model.glm` steckt aber in `assets1.pk3` **und** in einem Mod-Archiv — gleicher
Name, gleiche Kennung. ImGui hat zu Recht gemeckert. Jetzt ist es die Stelle
im Index, die eindeutig ist.

## Werkzeugleisten in Karte und Modell

Jede der drei Ansichten hat ihre eigenen Knöpfe, und beide 3D-Ansichten sind
**auch dann erreichbar, wenn nichts geladen ist** — vorher war *Karte*
ausgegraut, solange keine Geometrie da war, und man kam nie an die Knöpfe zum
Laden.

| Ansicht | Knöpfe |
|---|---|
| Karte | `Karte öffnen…` · `Aus .pk3…` · `.ent öffnen…` |
| Modell | `.glm öffnen…` · `Aus .pk3…` · `.skin öffnen…` · `.gla laden…` |

Der Grundsatz dahinter: **beim Laden wird das Übliche automatisch
mitgesucht** — zur `.glm` die `model_default.skin` daneben und die im Modell
vermerkte `.gla`, zur `.bsp` die Entities darin. Wer etwas anderes will,
wählt es danach von Hand.

Eine `.gla` von Hand zu wählen ist kein Sonderfall: ein Mod kann eine eigene
mitbringen, und beim Bauen einer neuen Animation will man sie ausprobieren,
bevor sie am gewohnten Ort liegt.

### Die .ent-Datei

Movie Duels arbeitet so: die `.bsp` bleibt unverändert, die Entities kommen
aus einer eigenen Datei daneben. `md_twj_jedi.ent` hat 132 Einträge — und die
sind es, die das Skript anspricht, nicht die der Originalkarte.

Der Leser ist derselbe wie für das Entity-Lump der `.bsp`. Das ist kein
Zufall, sondern dasselbe Format.

## Modelle (.glm)

Die dritte Ansicht neben Ereignissen und Karte. Ein Ghoul2-Modell enthält die
**Geometrie** einer Figur, aber kein Skelett: das steht in einer `.gla`, deren
Pfad im Kopf hinterlegt ist — bei `model.glm` aus Movie Duels
`models/players/_humanoid/_humanoid`.

Der Aufbau ist an der echten Datei nachgemessen:

```
Kennung "2LGM", Fassung 6
53 Knochen, 4 LOD-Stufen, 80 Flächen
LOD 0: 2208 Vertices, 2712 Dreiecke
```

Gelesen wird nur die feinste Stufe. Zwei Eigenheiten des Formats, die man
kennen muss:

* Flächen mit `*`-Vorsatz sind **Verbindungspunkte**, keine Geometrie —
  `*hips_cap_l_leg`, `*r_hand`. 46 der 80 Flächen sind solche Tags.
* Vertices und Texturkoordinaten stehen in **getrennten Blöcken**
  hintereinander, nicht verschränkt.

Die `.skin`-Datei ordnet den Flächen Texturen zu (`hips,models/players/luke/
boots_hips_blue.tga`) — bei `model_blue.skin` sind das 33 von 80.

### Warum alle Varianten gleichzeitig zu sehen waren

Eine `.skin`-Zeile kann statt eines Dateinamens **`*off`** enthalten — dann
wird die Fläche **abgeschaltet**. Mein Leser setzte nur Texturen und las
`*off` als Dateinamen; die Fläche blieb an. Bei einem Modell mit zwei
Kopfvarianten sah man beide ineinander.

Belegt in `tr_ghoul2.cpp`: bei `shader->name == "*off"` ruft die Engine
`G2_SetSurfaceOnOff` mit `G2SURFACEFLAG_OFF`.

Dazu die Flaggen des Formats, aus `mdx_format.h`:

```
G2SURFACEFLAG_ISBOLT  0x1   Verbindungspunkt, keine Geometrie
G2SURFACEFLAG_OFF     0x2   in der Vorgabe abgeschaltet
```

Vorher prüfte ich den **Namen** (`*`-Vorsatz, Endung `_off`) — eine Krücke.
Der Kommentar im Format sagt es selbst: die Flagge *„saves strcmp()ing for
_off in surface names"*. Jetzt zählt die Flagge, der Namensvergleich bleibt
als Rückfall.

Alle Zeichner gehen jetzt über **eine** Regel, `isVisible(showCaps)`.

### Die Hautauswahl

Ein Modell bringt oft mehrere `.skin` mit — `model_default`, `model_red`,
`model_blue`. Sie entscheiden nicht nur über Texturen, sondern darüber,
**welche Flächen sichtbar sind**. Beim Laden werden alle gesammelt und in
einer Klappliste angeboten; daneben steht, wie viele Flächen gerade sichtbar
sind — daran sieht man sofort, ob eine Haut greift.

Beim Wechsel wird das Modell **zurückgesetzt**: eine Haut schaltet Flächen ab,
und die blieben sonst aus, wenn die nächste Haut sie nicht erwähnt.

### Warum das Modell kantig aussah

Der Zeichner mittelte die drei Vertexnormalen und legte **eine** Normale auf
das ganze Dreieck. Jede Fläche bekam damit genau einen Farbton — bei 2712
Dreiecken sieht das aus wie ein Papiermodell.

Die `.glm` bringt für **jeden Vertex** eine eigene Normale mit. Jetzt wird die
Helligkeit je Ecke berechnet und über die Fläche gemittelt. Nachweisbar an der
Zahl verschiedener Helligkeiten im Bild: **190 statt einer je Dreieck**, und
genau das prüft eine Probe.

Beim Animieren muss die Normale mit dem Knochen mitgedreht werden — sonst
bleibt das Licht stehen, während sich die Figur bewegt. Dafür zählt nur der
Drehanteil der Matrix; eine Verschiebung gehört nicht auf eine Richtung.

Der Anfangsabstand lag bei Faktor 2,2 der Modellgröße — eine 66 Einheiten hohe
Figur stand als Streichholz in der Mitte. Jetzt 1,15.

### Warum Texturen fehlten

Auf der Suche nach den fehlenden Modelltexturen kam ein Fehler heraus, der
nichts mit Shadern zu tun hatte: der Archivindex senkte zwar die
**Schreibweise**, wandelte aber keine **Rückstriche** um.

Drei Formen kommen in echten Mods vor, und jede muss denselben Eintrag
treffen:

| in der `.skin` steht | im Archiv steht |
|---|---|
| `models/players/Kyle/Torso.tga` | `models/players/kyle/torso.tga` |
| `models\players\kyle\torso.tga` | dasselbe mit Schrägstrichen |
| `./models/players/kyle/torso.tga` | ohne `./` davor |

Auf Windows fällt das nie auf — im Archiv ist der Name aber bloßer Text. Sechs
Proben halten die Formen jetzt fest.

### Shader auf dem Modell

Eine `.skin` zeigt meistens **direkt** auf eine Bilddatei: `model_blue.skin`
tut das bei allen 34 Einträgen. Bei Lichtschwertern und Glaseffekten steht
dort aber ein Shadername, und das Bild liegt in einer `map`-Zeile des Skripts.

Nachgesehen wird erst, **wenn die direkte Suche nichts ergibt** — bei den
meisten Modellen ist der Umweg unnötig, und die Skripte einzulesen kostet
Zeit. Gesucht wird mit und ohne Endung: in der `.skin` steht oft
`.../blade.tga`, im Skript aber `.../blade`.

### Das Skelett (.gla)

Die `.gla` enthält Skelett und Bewegung. Der Leser ist da, das Format nach
g2cs `src/mdxa.cpp` umgesetzt:

```
Kopf     "2LGA", Name, Maßstab, numFrames, numBones, drei Versätze
Skelett  je Bone: Name, Elternteil, Grundstellung und ihre Umkehrung
Indizes  je Bild und Bone DREI Byte - ein Verweis in den Bonevorrat
Vorrat   je Eintrag VIERZEHN Byte: vier Quaternionanteile, drei Verschiebungen
```

Die Kompression liegt fest und ist verlustbehaftet:

```
Drehung:      wert = roh / 16383 - 2
Verschiebung: wert = roh / 64 - 512
```

Der Vorrat wird **geteilt** — gleiche Bonestellungen in verschiedenen Bildern
verweisen auf denselben Eintrag. Deshalb ist eine `.gla` mit tausenden Bildern
trotzdem klein.

**Die wichtigste Falle**, und die stand als Warnung in g2cs Quelltext: die
Bones müssen in **topologischer** Reihenfolge berechnet werden, nicht nach
Index. In Ravens `_humanoid.gla` haben acht Bones ihren Elternteil **hinter**
sich. Wer nach Index rechnet, benutzt eine noch nicht berechnete
Elternmatrix — und die Figur verdreht sich still.

Die Weltstellung ergibt sich aus:

```
Wurzel:  X(b) = A(b) · B(b)
sonst:   X(b) = X(p) · B(p)⁻¹ · A(b) · B(b)
```

**Gegen `_humanoid.gla` geprüft:** 53 Knochen, 30384 Bilder, 747750 Einträge
im geteilten Vorrat. Und der Befund, der die topologische Reihenfolge von der
Vermutung zur Tatsache macht: **genau 8 Knochen haben ihren Elternteil hinter
sich** — exakt die Zahl aus g2cs Warnung.

### Drei Fehler, die erst das Bild gezeigt hat

Der Leser übersetzte sauber und lieferte plausible Zahlen — trotzdem war die
Figur zerrissen. Nur weil ich sie gezeichnet habe, kam es heraus:

**1. Das Skelettverzeichnis liegt hinter dem Kopf, nicht bei `ofsSkel`.**
Der Wert zeigt auf den *ersten Knochen*; die Versätze darin sind relativ zum
Verzeichnis. Wer `ofsSkel` als Bezug nimmt, liest den Knochennamen als
Versatzliste und bekommt Zahlen wie 1701080941 — das ist `"mode"` als int
gelesen.

**2. Die Knochenverweise im Modell sind LOKAL.** Fünf Bit reichen nur für 32
Werte, ein Skelett hat 53 Knochen. Die Zahl ist ein Index in die
`BoneRefs`-Liste der jeweiligen Fläche. Vorher lag der höchste Verweis bei 9,
danach bei 51.

**3. Die Vertices stehen in Modellraum.** Erst mit der Umkehrung der
Grundstellung in den Knochenraum, dann mit der Weltmatrix zurück:

```
p' = Σ  gewicht(k) · World(bone k) · BasePoseInv(bone k) · p
```

Ohne diesen Schritt sind die Gliedmaßen erkennbar, ziehen aber auseinander.

### Die animation.cfg

```
BOTH_WALK1          30293   30   0   20
Name              Anfang  Zahl Schleife Tempo
```

1683 Abschnitte bei `_humanoid`. Ein `loopFrame` von −1 heißt: einmal
abspielen und stehenbleiben — so ist `BOTH_DEATH1` gebaut, während
`BOTH_WALK1` bei 0 wieder anfängt.

## Kartengeometrie

`bsp.h` liest nur die Entities, `bspgeo.h` den Rest: Vertices, Flächen,
Shadernamen und **die Lightmaps** — die stecken schon in der `.bsp`, eine
Karte lässt sich also mit echtem Licht zeichnen, ohne eine einzige Textur aus
einer `.pk3` zu holen.

Alle Strukturgrößen sind an echten Karten **nachgemessen**, nicht aus einer
Beschreibung übernommen. Das hat sich sofort gelohnt: für `LEAFS` stand in
meiner Vorlage 56 Byte, und bei `yavin2.bsp` ging das auch auf — bei
`duel_kamino_lp.bsp` aber nicht. Richtig sind 48. **Eine Größe, die bei einer
Datei aufgeht, ist noch kein Beleg.**

| | Flächen | Lightmaps | Dreiecke | Zeichenaufrufe |
|---|---|---|---|---|
| `duel_kamino_lp` | 1437 | 20 | 30094 | 21 |
| `yavin2` | 12900 | 40 | 54732 | 41 |

Patches sind Bézier-Flächen aus 3×3-Feldern, die sich Randpunkte teilen —
deshalb sind Breite und Höhe immer ungerade. Sie werden beim Netzbau zerlegt
und alles nach Lightmap zusammengefasst: aus 12900 Flächen werden 41
Zeichenaufrufe.

Nicht gelesen werden Sichtbarkeitsdaten, Lichtgitter und Kollisionskörper. Zum
Zeichnen einer ganzen Karte in einem Editor braucht man das nicht — es geht
nicht um Bildrate, sondern darum, sich zurechtzufinden.

## Karte

*Datei → Karte laden…* liest eine `.bsp` und bietet danach in den Textfeldern
des Event-Editors die `targetname` aus der Karte an, statt dass man sie
abtippt. Bei `SET_CAMERA_GROUP` sind es entsprechend die `cameraGroup`.

Gelesen wird **nur das Entity-Lump** — Lump 0, reiner Text, bei
`duel_kamino_lp.bsp` 15 KB für 111 Entities und 42 Zielnamen. Ein Leser für
Flächen, Patches und Lightmaps wäre ein Vielfaches an Arbeit und bräuchte
einen Zeichner dazu; für einen Skripteditor bringt er nichts.

Unterstützt: `RBSP` (Jedi Academy, Jedi Outcast), `IBSP` (Quake 3), `FBSP`.
Die Fassungsnummer wird nicht geprüft — das Entity-Lump liegt bei allen an
derselben Stelle.

Der Pfad wird gemerkt, aber **nur beim Start wieder geladen, wenn „Letzte
Datei beim Start öffnen" gesetzt ist** — genau wie beim Skript. Vorher geschah
es ungefragt, und das ist aus zwei Gründen falsch: das Programm startete mit
einer Karte, die man vor Tagen zuletzt benutzt hatte, und eine `.bsp` ist groß
(yavin2 hat 13,8 MB), der Start dauerte also spürbar länger, ohne dass man
wusste wofür.

## Skalierung

Nach der Anleitung in ImGuis FAQ, nachdem die erste Fassung zwei Regeln
verletzt hatte und entsprechend aussah.

**Die Schrift wird in der Grundgröße geladen, nicht in der Zielgröße.** Seit
1.92 rastert ImGui dynamisch: Grundgröße in `style.FontSizeBase`, Maßstab in
`style.FontScaleDpi`. Die erste Fassung backte mit `16 * dpiScale` **und**
setzte `FontScaleDpi` — bei 150 % also 36 Punkt statt 24.

**`style.ScaleAllSizes()` ist ausdrücklich „call once!"** Es multipliziert die
Abstände bei jedem Aufruf weiter. Wer den Maßstab ändern will, muss laut FAQ
*„reset the style and call this again"*. Ohne das bleiben die Polster klein,
während die Schrift wächst — dann sieht es gequetscht aus. Deshalb wird bei
jeder Änderung vom Urzustand aus neu aufgebaut: Standardstil → Farbgebung →
`ScaleAllSizes(gesamt)`.

**Kein Neubau der Schriften bei DPI-Wechsel.** `WM_DPICHANGED` setzt nur den
Faktor; ImGui rastert im nächsten Bild selbst neu. Das spart nicht nur Zeit,
es beseitigt auch eine Fehlerquelle: `io.Fonts->Clear()` machte vorher
nebenbei die Symbolkennungen ungültig.

Bedienung: **Strg + Mausrad**, oder *Ansicht → Bedienoberfläche skalieren*
mit einem Regler von 50 % bis 300 % und einem Knopf zurück auf 100 %. Der
Gesamtmaßstab ist Bildschirm × Nutzerwahl, die Nutzerwahl überlebt den
Neustart.

## Wo Messung und Beschreibung sich widersprechen

**`%g floats` kürzt die Zahlen** — so, wie der C-Formatbuchstabe `%g` es tut.
Beide Beschreibungen sagen das Gegenteil:

> JKHub-Tutorial: *„Shows verbose Floating Point Numbers. Sets the editor
> whether or not to show all decimal-places at once."*
>
> OpenJK-Wiki: *„%g floats — Shows all floats with several decimal places
> (default shows only decimal places used)."*

Das Bildschirmfoto entscheidet dagegen: dort ist `%g floats` sichtbar
**gedrückt** (versenkt, mit Fokusrahmen), `Show Types` nicht — und die Zahlen
stehen gekürzt da (`wait ( 3000 )`, `camera ( MOVE, -6048 7162 1214, 0 )`).

Umgesetzt ist, was das Bild zeigt. Falls sich das als falsch erweist, ist es
eine Zeile in `src/tree.cpp`.

## Was noch geraten ist
`Pythonise !` gibt es bewusst nicht. Das BehavEd-Tutorial auf JKHub sagt
dazu: *„Will attempt to write a .python file for your script. **Ignore This
Button**"* — schon Ravens eigene Anleitung rät davon ab. Etwas nachzubauen,
wovon die Anleitung abrät, wäre die falsche Art von Treue.

## Backup und Restore

Belegt durch das BehavEd-Tutorial auf JKHub:

> *Backup: Saves a copy to `<Script Name>.bak`*
> *Restore: Reloads from the backup at `<Script Name>.bak`*

Eine Feinheit, die dort nicht steht und die man leicht falsch macht:
**gesichert wird der Stand auf der Platte, nicht der im Speicher.** Sonst wäre
die Sicherung schon die ungesicherte Fassung, und `Restore` holte genau das
zurück, was man loswerden wollte.

`Restore` fragt vorher nach, auch wenn die Nachfrage sonst abgeschaltet ist —
es wirft den aktuellen Stand weg.

Beides braucht einen Dateinamen. Bei einem nie gespeicherten Skript sagt das
Programm das, statt still nichts zu tun.

## Export

Belegt durch ein Bildschirmfoto: BehavEd schreibt eine Textdatei und öffnet
sie im Standardeditor. Der Inhalt ist echter `.icarus`-Text mit einer
Kopfzeile davor:

```
( Original filename: "" )

//Generated by BehavEd

rem ( "comment" );
move ( < 0 0 0 >, < 0 0 0 >, 1000 );
```

Zwei Details, die man leicht überliest: die Zahlen folgen dem Schalter
**`%g floats`** (`1000` statt `1000.000`), die Zeichenketten behalten aber
ihre Anführungszeichen — anders als in der Baumansicht. Und die Statusliste
meldet `(0) : ( Exported )`, mit laufender Nummer. Beides ist so nachgebaut
und als Test festgehalten.

## Symbol, Manifest und Versionsangaben

`res/behaved.rc` bindet drei Dinge in die `.exe`, die man erst vermisst, wenn
sie fehlen.

**Das Symbol** entsteht über `tools/gen_icon.py` in sieben Größen (16 bis
256) — aus **zwei** Vorlagen, nicht einer:

| | |
|---|---|
| `res/icon_source.png` | `bED`, ab 32 px |
| `res/icon_source_small.png` | nur `b`, für 16 und 24 px |

Unter 24 Pixeln ist `bED` nicht mehr lesbar; drei Zeichen auf sechzehn
Punkten werden zum Fleck. Das ist kein Kniff von uns: in `BehavEd.exe` stehen
genau dieselben zwei Bilder — `ICON 1` mit `bED` in 32×32 und `ICON 2` mit
`b` in 16×16. Dasselbe Problem, dieselbe Lösung, dreiundzwanzig Jahre später. Der `.ico`-Behälter wird selbst geschrieben, nicht
über Pillow: dessen Schreiber hat auf die Anforderung von sieben Größen mit
**einer** 16×16 geantwortet, ohne Fehlermeldung. Gebaut hätte es trotzdem, und
der Explorer hätte eine verwaschene Kachel gezeigt. Deshalb prüft
`tools/check_exe_resources.py` die **fertige Datei** — was drin steht, lässt
sich nur dort feststellen.

**Das Manifest** enthält zwei Dinge, die wichtiger sind, als sie aussehen:

* `requestedExecutionLevel asInvoker` — das Programm fordert **keine**
  Administratorrechte. Wer das tut, löst bei jedem Start die UAC-Nachfrage aus,
  und ein Werkzeug, das ohne Grund Administratorrechte will, ist genau das
  Muster, an dem man Schadsoftware erkennt. Ein Skripteditor braucht sie nicht.
  Der Prüfer schlägt an, wenn jemand das ändert.
* `activeCodePage UTF-8` — sonst kommen Dateinamen mit Umlauten als
  Fragezeichen an.

**Die Versionsangaben** füllen Rechtsklick → Details. Zwei Sprachblöcke
(Deutsch und Englisch), beide unter `VarFileInfo` angemeldet — fehlt der
Eintrag dort, findet Windows den Block nicht, auch wenn er in der Datei steht.

### Eine Falle, die nur MSVC zeigt

Visual Studio erzeugt **selbst** ein Manifest und legt es unter derselben
Nummer ab wie unseres. Der Ressourcenwandler bricht dann ab:

```
CVT1100: Doppelte Ressource. type:MANIFEST, name:1
LNK1123: Fehler bei der Konvertierung in COFF
```

MinGW erzeugt keines — der Kreuzbau lief also durch, und der Fehler kam erst
beim Bauen mit Visual Studio. `/MANIFEST:NO` schaltet das erzeugte ab; unseres
bleibt und ist das bessere, weil es die unterstützten Windows-Fassungen und
die Codepage nennt.

`tools/lint_sources.py` prüft das jetzt mit: wer ein Manifest in die `.rc`
schreibt, ohne `/MANIFEST:NO` zu setzen, bekommt es vor dem Übersetzen
gesagt statt danach.

### Was das gegen SmartScreen ausrichtet

Wenig. Ehrlich gesagt: **nichts Entscheidendes.** Der blaue Kasten
*„Der Computer wurde durch Windows geschützt"* kommt, weil die Datei
**unsigniert** ist und aus dem Internet stammt. Dagegen hilft nur ein
Code-Signing-Zertifikat, und selbst dann braucht ein normales Zertifikat erst
Ruf, bevor die Warnung verschwindet; nur ein EV-Zertifikat wirkt sofort.

Was die Ressourcen bewirken: die Datei sieht nicht mehr aus wie etwas schnell
Zusammengeklicktes, Rechtsklick → Details ist gefüllt, und manche
Virenscanner werten leere Metadaten als kleines Minus. Das ist alles.

Für Testende ist der ehrliche Weg: **Weitere Informationen → Trotzdem
ausführen**.

## Wo das Protokoll liegt

**Neben der `.exe`**, nicht in `%APPDATA%`. Wer es hinschicken soll, findet es
dort, ohne einen versteckten Ordner suchen zu müssen.

Wenn das nicht geht — in `Programme` darf ein Programm nicht schreiben —
bleibt `%APPDATA%` als Rückfall. Beides stumm scheitern zu lassen wäre
schlecht: dann gäbe es bei einem Absturz gar nichts. Der Über-Dialog zeigt den
tatsächlich benutzten Pfad.

## -FLT_MIN als Höhe füllt das FENSTER, nicht die Zeile

Die Ebenenliste reichte bis zum unteren Fensterrand und schob das Band darunter
aus dem Bild. Ursache: `-FLT_MIN` als Höhe eines Kindfensters bedeutet „der
Rest des **Elternfensters**" — und das ist hier das Hauptfenster, nicht die
Zeile mit den Spalten.

Sie bekommt jetzt `Layout::frameH` wie alle anderen. Damit ist die Regel
vollständig: **jede Spalte bekommt `headerH` für den Kopf und `frameH` für den
Rahmen, keine rechnet selbst, und keine nimmt `-FLT_MIN`.**

## Derselbe Absturz, eine Datei weiter

Der Kartenreiter brach mit derselben Zusicherung ab wie damals Escape:

```
Code uses SetCursorPos() to extend window/parent boundaries.
Please submit an item e.g. Dummy() afterwards.
```

Die Stelle: `SetCursorPosY` füllt in `app.cpp` die Kopfzeile auf, dann folgt
`drawEntityLayers()` — und **das kehrte bei leerer Karte sofort zurück**, ohne
ein Element abzuschicken.

Mein Prüfer hatte den Fall nicht gesehen, weil er über **zwei Dateien** geht:
die Cursorbewegung in `app.cpp`, das `return` in `app_view3d.cpp`.

Zwei Konsequenzen:

* Nach jedem Auffüllen der Kopfzeile steht jetzt ein **`Dummy({0,0})` als
  Anker** — sechs Stellen. Was danach kommt, darf vorzeitig zurückkehren, der
  Anker nicht.
* Der Prüfer meldet ab sofort **jeden Funktionsaufruf** direkt nach einem
  `SetCursorPos`. Er kann nicht über Dateien hinweg schauen, also gilt die
  einfachere Regel: erst ein Element, dann alles andere.

Gegengeprüft: nehme ich einen Anker weg, schlägt er an.

## Alles steht von Anfang an

Der Grundsatz, der sich durch die ganze Oberfläche zieht: **was später da
ist, ist auch vorher da — nur ausgegraut.**

Vorher entstanden Ebenenliste, Kartenbedienung und Zeitleiste erst, wenn eine
Karte oder ein Skript geladen war. Beim Laden sprang dann die ganze Anordnung:
die Ansicht wurde kürzer, die Spalten schmaler, alles verrutschte.

Jetzt sind sie immer da:

| | ohne Karte |
|---|---|
| Ebenenliste | leer, „Alle zeigen" ausgegraut |
| Kartenbedienung | alle Regler da, ohne Wirkung |
| Zeitleiste | leerer Zeiger |

Die Höhe des Bandes und die Breite der Spalten stehen damit fest, bevor
irgendetwas geladen wird.

## Die Ebenenliste steht rechts von der Modusleiste

Beim ersten Einbau lag sie **links davon** — ganz am Fensterrand, vor
*Ereignisse / Karte / Modell*. Falsch: die Modusleiste ist die äußerste
Spalte, die Gliederung gehört daneben.

Und ihre Breite wurde **zweimal** abgezogen: einmal von der Gesamtbreite,
einmal innerhalb der linken Spalte. Dadurch wuchs der Skriptbaum, bis er nicht
mehr passte. Sie liegt innerhalb der linken Spalte, also wird sie nur dort
abgezogen.

## Warum sich beim Kartenladen alles verschob

Die Werkzeugleiste der Karte bekommt beim Laden ein Feld **„Scripts (2)"**
dazu. Damit wurde sie breiter als ihre Spalte — und ImGui vergrößert dann den
Inhalt des Hauptfensters. Die Spalten rechts rutschten aus dem Bild.

Die Leiste sitzt jetzt in einem Kasten von **genau der Spaltenbreite**, mit
`NoScrollbar`. Was nicht hineinpasst, wird abgeschnitten statt alles zu
verschieben.

## Die Karte hat ihre eigene Aufteilung

Die Knopfspalte **„Actions" gehört zum Skript, nicht zur Karte** — dort steht
sie nur im Weg. Im Kartenmodus daher:

```
Modusleiste │ Ebenen │ Karte │ Skriptbaum
```

Die Ebenen links, weil man sie liest wie eine Gliederung. Der Skriptbaum
rechts, weil er beim Anklicken einer Entity die Antwort gibt — *wo wird sie
benutzt*.

Alles andere zur Karte steht im **Band darunter**, über die volle Breite:
Ort und Kameraknopf, Helligkeit, Umgebungslicht, Detailgrad, die Schalter, die
angeklickte Entity und die Zeitleiste. Genau wie beim Modell, und aus demselben
Grund: in einer Spalte passt es nicht, über die volle Breite mit Platz.

## Eine Kopfzeilenhöhe für alle vier Spalten

Die eigentliche Ursache der ungleichen Rahmen lag **oben**, nicht unten.

Die vier Spalten haben verschiedene Köpfe:

| Spalte | Kopf |
|---|---|
| Modusleiste | keiner |
| Karte / Modell | eine **Knopfleiste** |
| Skriptbaum | der Text „Script Flow" |
| Aktionen | der Text „Actions" |

Eine Knopfzeile ist **höher als eine Textzeile**. Also begannen die Rahmen auf
verschiedenen Höhen — und kein noch so genaues Abziehen unten konnte das
ausgleichen. Ich hatte es zweimal unten versucht; beide Male blieb ein Rest.

Jetzt bekommt jede Spalte denselben Platz für ihren Kopf, `Layout::headerH`,
so hoch wie eine Knopfzeile. Wer weniger braucht — Text oder gar nichts —
lässt den Rest frei. Damit beginnen und enden alle vier auf derselben Linie.

Die Trennlinien beginnen ebenfalls unter der Kopfzeile und sind genau so lang
wie die Rahmen. Vorher ließen sie eine *Text*zeile frei und waren deshalb zu
lang.

## Eine Höhe für alle vier Spalten

Die vier Spalten — Modusleiste, Ansicht, Skriptbaum, Knopfspalte — rechneten
ihre Rahmenhöhe **jede selbst** aus, immer als „topH minus eine Textzeile".
Vier Rechnungen an vier Stellen, und sie ergaben nicht dasselbe. Sichtbar
wurde es als Rahmen, die unten nicht auf einer Linie enden.

Jetzt liefert `Layout::frameH` den Wert, und niemand rechnet neu. `beginGroupBox`
bekommt die Rahmenhöhe **durchgereicht**, statt selbst eine Zeile abzuziehen.

Ein Sonderfall bleibt: Karte und Modell haben anstelle der Überschriftzeile
eine **Werkzeugleiste**, und die ist höher als eine Textzeile — es sind
Knöpfe. Ihr Rahmen wird um genau diese Differenz kürzer, gemessen über den
Cursorstand, nicht geschätzt.

`lint_imgui_context.py` meldet jetzt jede Stelle, die die Rahmenhöhe wieder
selbst ausrechnet.

## Ein Rollbalken, der die Rahmen verschob

Die Zeitleistenzeile war um **genau einen Elementabstand** zu breit: ich zog
`infoW + ItemSpacing.x` ab, aber `SameLine` setzt noch einen dazwischen. ImGui
gab dem Band daraufhin einen waagerechten Rollbalken — und der nahm der
Ansicht darüber Höhe weg. Genau daran lagen die ungleichen Rahmen.

Zwei Änderungen: die Rechnung stimmt jetzt, und das Band bekommt
`NoScrollbar`. Was nicht hineinpasst, wird abgeschnitten — das fällt auf und
lässt sich beheben, ein stiller Höhenverlust nicht.

## Häute auch aus dem Archiv

Die Häute wurden **nur von der Platte** gesammelt (`listDirectory`). Ein
Modell aus einer `.pk3` — der häufigere Weg — hatte deshalb eine leere Liste,
und die Auswahl fehlte ganz.

Jetzt werden sie über den Archivindex gesucht, und `applySkinByIndex` liest
erst von der Platte, dann aus den Archiven. Ohne das Zweite täte ein Wechsel
stillschweigend nichts.

## Die Hilfszeile ist entfallen

Sie zeigte die Beschreibung dessen, worüber die Maus gerade stand — ganz unten
am Fensterrand, weit weg vom Blick. Seit die Beschreibungen **an der Maus**
erscheinen, war sie doppelt, und sie kostete zwei Textzeilen Höhe, die dem
Band darüber fehlten.

Meldungen, die dort landeten, gehen jetzt in die **Statusliste**. Das ist der
bessere Ort: dort bleiben sie stehen und lassen sich nachlesen, statt beim
nächsten Mausbewegen zu verschwinden.

Aufgeräumt hat dabei der Prüfer: `MsgHelpHere` wurde als ungenutzt gemeldet,
sobald die Zeile weg war.

## Die Bedienleiste ist immer da

Vorher entstand das Band erst mit einem Modell — und dann sprang die ganze
Anordnung, weil die Ansicht darüber plötzlich kürzer wurde. Jetzt steht alles
permanent da, ohne Modell **ausgegraut**. Wer die Bedienung schon sieht, weiß,
was ihn erwartet, und nichts wandert.

Auch die Zeitleiste bleibt stehen, nur leer — sonst wäre die Höhe wieder eine
andere.

## Zwei Zeilen statt vier

Die Bedienleiste des Modells stand in vier Zeilen untereinander und brauchte
damit mehr Höhe, als das Band hat. Folge: ein Rollbalken, und *„Bild 1 von 6"*
stand abgeschnitten am Rand.

Das Band geht über das **ganze Fenster**. Bei zweitausend Punkten Breite passt
nebeneinander, was vorher untereinander stand:

```
Zeile 1   Abspielen | Animation | Wiederholen | Tempo | Haut | Flächen
Zeile 2   Zeitleiste, daneben Bild und Sekunden
```

Und die Höhe wird **gemessen** statt mit einem Faktor geschätzt — genau das
war die Ursache: `GetFrameHeightWithSpacing() * 3.4` war eine Zahl aus der
Luft, `rowH * 2 + ItemSpacing.y * 2` ist die Rechnung.

## Warum der 3D-Rahmen niedriger endete

Das Bild war **genau so groß wie sein Kindfenster** — und Kindfenster haben
einen Innenabstand. Das Bild passte also nicht hinein, ImGui setzte einen
Rollbalken daneben, und der Rahmen endete sichtbar höher als die Spalten
rechts davon.

`PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0})` vor `BeginChild` löst es:
das Bild füllt sein Kind genau aus.

## Die Bedienleiste steht UNTER allen Spalten

Sie lag in der Modellspalte und musste sich deren Breite teilen — bei
schmaler Spalte passte sie nicht hinein, rechts wurde abgeschnitten, und die
Hautauswahl war gar nicht zu erreichen.

Jetzt ist sie ein eigenes Band über die volle Breite, zwischen den drei
Spalten und der Hilfszeile. `computeLayout` zieht die Höhe von `topH` ab, das
Bild verliert also nur, was wirklich gebraucht wird — und für die Karte gilt
dasselbe mit der Zeitleiste.

## Wo Nachsehen sich lohnt — und wo nicht

Nach mehreren Runden zeichnet sich ein Muster ab. Zwei Arten von Prüfung haben
**jedes Mal** einen echten Fehler gefunden:

**Verhalten der Engine, das hier nachgebaut wird.** Jede einzelne Prüfung am
OpenJK-Quelltext saß:

| geprüft | gefunden |
|---|---|
| `cg_camera.cpp` | lineare Interpolation, kürzester Schwenk, Dauer 0 = Sprung |
| `NPC_stats.cpp` | walkSpeed 90, runSpeed 300 |
| `tr_ghoul2.cpp` | `*off` schaltet Flächen ab |
| `mdx_format.h` | die Flächenflaggen statt Namensvergleich |
| `R_ColorShiftLightingBytes` | proportional statt abschneiden |
| `surfaceflags.h` | `SURF_NODRAW` 0x200000, nicht 0x80 |

**Muster einer Bibliothek, die ich von Hand nachbaue.** Der Kachelrasterisierer
und das Auswahlfeld — beide Male war die selbstgebaute Fassung falsch, und
beide Male stand die richtige Lösung dokumentiert bereit.

Nicht gelohnt hat sich Nachsehen bei **eigener Rechenlogik**: Zeitleiste,
Szenenbahnen, Prüfer. Dort helfen Proben, keine Suche.

### Was ImGui sonst noch kann — ein Abgleich

`imgui.h` hat **546 öffentliche Funktionen**, wir benutzen 97. Der Abgleich hat
zwei Dinge gefunden, die wir von Hand nachbauten:

**`ImGuiListClipper`.** Er reicht nur die *sichtbaren* Zeilen durch. Vorher
schnitt ich bei 500 Treffern ab und schrieb „…" — eine Krücke, weil 1543
Animationsnamen untereinander sonst jedes Bild gekostet hätten. Mit dem Clipper
ist die Liste **vollständig**.

Eine Feinheit dabei: `SetItemDefaultFocus` funktioniert mit einem Clipper
nicht mehr, weil der aktuelle Eintrag gar nicht abgeschickt wird, wenn er
außerhalb liegt. Gerollt wird deshalb über `SetScrollY` mit der Zeilennummer.

**`Shortcut()`.** In `imgui.h` steht der Unterschied: `IsKeyPressed` hat
**keine** Nebenwirkung, `Shortcut` meldet eine **Route** an — *„several callers
may register interest in a shortcut, and only one owner gets it"*.

Das löst genau das Problem, das ich vorher mit einem Flag umgangen hatte: der
Editor schloss mit Eingabe, und die Baumtasten sahen dieselbe Taste im selben
Bild und öffneten ihn wieder. Der Behelf `editorAteKey` ist ersatzlos
entfallen.

### Geprüft und bewusst gelassen

| | |
|---|---|
| `BeginMultiSelect` | unsere Strg/Umschalt-Auswahl ist geprüft und läuft; ein Wechsel wäre Risiko ohne Gewinn |
| `BeginTable` | die Spalten sind keine Tabelle, sondern verschiebbare Bereiche |
| `TreeNode` | unser Baum trägt Symbole, Ziehpunkte und Mehrfachauswahl — die fertige Fassung kann das nicht |
| `BeginTabBar` | wir haben keine Reiter |

### Der Maßstab: FontScaleMain

ImGui trennt zwei Maßstäbe, und seit dieser Fassung tun wir das auch:

| | |
|---|---|
| `FontScaleDpi` | der Bildschirm — kommt von Windows |
| `FontScaleMain` | die Einstellung des Benutzers |

In `imgui.h` steht zu `FontScaleMain` ausdrücklich *„may be set by application
once, or exposed to end-user"* — genau dafür ist er da. Vorher stand das
Produkt beider in `FontScaleDpi`; das ergab dieselbe Größe, vermischte aber
zwei Dinge mit verschiedenen Ursachen.

**Der Regler im Menü ist weg.** Ein Schieberegler *in einem Menü* ist ein
schlechtes Bedienelement: das Menü fängt Mausbewegungen ab, und beim Loslassen
schließt es — man zieht blind und trifft selten. Jetzt feste Stufen (75 % bis
300 %), wie Windows, Firefox und Visual Studio sie anbieten. Stufenlos geht
weiterhin mit **Strg + Mausrad**, und dort ist es richtig aufgehoben, weil man
dabei sieht, was passiert.

### Ungenutzte Texte verraten verlorene Bedienung

`lint_i18n.py` meldet jetzt übersetzte Texte, die nirgends benutzt werden.

Anlass: beim Herausziehen der Bedienleiste aus der Modellspalte ging die
**Hautauswahl verloren**. Der Übersetzer meldete nichts — der Code lief ja
weiter, er zeigte das Feld nur nicht mehr. Aufgefallen ist es allein daran,
dass `ModelSkin`, `ModelSkinHint` und `ModelSurfaces` plötzlich nirgends mehr
vorkamen.

Ein ungenutzter Text ist also nicht bloß Ballast, sondern oft die Spur einer
verschwundenen Bedienung.

Beim Aufräumen kam `MsgUnknownValue` wieder zu Ehren: die Warnung, dass ein
Wert nicht in der Typmenge steht, gibt es im Prüfer längst als **V004** — jetzt
steht sie auch neben dem Feld. Sie ist genau seit dem sinnvoll, seit man
beliebige Werte tippen kann.

### Dateidialoge: der dritte Fall

`GetOpenFileNameW` ist die Schnittstelle von vor Windows Vista. Microsoft
schreibt dazu ausdrücklich, sie sei durch den **Common Item Dialog** ersetzt
und der neue solle benutzt werden.

Drei Gründe, die hier zählen:

* Es gibt einen echten **Ordnerdialog** (`FOS_PICKFOLDERS`). Vorher musste man
  eine `.pk3` im gewünschten Ordner auswählen, damit das Programm den Ordner
  davon nehmen konnte — eine Krücke, die im Quelltext auch so kommentiert war.
* Der Dialog ist der, den man aus jedem anderen Programm kennt:
  größenveränderlich, mit Seitenleiste und zuletzt benutzten Orten.
* Der Startordner ist nur ein **Vorschlag** (`SetDefaultFolder` statt
  `SetFolder`). Windows merkt sich je Programm, wo man zuletzt war.

**Ein Fehler kam beim Einbauen gleich mit:** ich hatte in den Kommentar
geschrieben, COM werde beim Start angemeldet — das stimmte nicht.
`CoInitializeEx` fehlte, und ohne das gibt `CoCreateInstance`
`CO_E_NOTINITIALIZED` zurück: **jeder Dateidialog wäre stillschweigend gar
nicht aufgegangen.** Jetzt steht es in `wWinMain`, mit `CoUninitialize` am
Ende.

### Trenner: dasselbe noch einmal

`SplitterBehavior` aus `imgui_internal.h` erledigt genau das, was mein
unsichtbarer Knopf nachbaute — und bringt drei Dinge mit, die dort fehlten:
Überlappungsbehandlung (ein Knopf unter dem Trenner bleibt anklickbar), eine
Verzögerung vor dem Farbwechsel, und Mindestgrößen in Punkten.

Die Anteile bleiben nach außen erhalten: die Einstellungen speichern sie so,
und ein Anteil überlebt eine Größenänderung des Fensters.

## Tippbare Auswahlfelder

Ein reines Auswahlfeld sperrt Werte aus, die die Liste nicht kennt — und
genau die braucht man: Movie Duels bringt in seiner `_humanoid.gla` eigene
Animationen mit (`BOTH_MD_CIN_43` und Verwandte), die in Ravens Kopfdateien
nicht stehen.

### Drei Fehler aus einem selbstgebauten Popup

Der erste Anlauf baute das Aufklappen von Hand — `InputText`, Pfeilknopf,
`OpenPopup`. Er hatte drei Fehler, und alle hatten dieselbe Wurzel: **er
kämpfte gegen die Bibliothek.**

* **Der zweite Klick auf den Pfeil öffnete nichts.** ImGui schließt ein Popup
  beim Klick daneben — und der Pfeil *ist* daneben. Erst schloss es, dann rief
  mein Code `OpenPopup`, und beides hob sich auf.
* **Die Liste erschien hinter dem Editorfenster.** Popups innerhalb eines
  modalen Fensters gehören auf dessen Stapel; ein von Hand geöffnetes landet
  daneben.
* **Sie ließ sich nicht rollen**, weil `OpenPopup` in jedem Bild gerufen
  wurde. Die Anleitung sagt es ausdrücklich: *„don't call every frame!"*

### Die Lösung: ImGuis eigenes BeginCombo

`BeginCombo` macht all das selbst — Auf- und Zuklappen, Stapelordnung,
Rollbalken, Tastaturbedienung. Das Tippen kommt als Textfeld **innerhalb** der
Liste dazu:

| | |
|---|---|
| aufklappen | ganze Liste, Filter leer |
| tippen | filtert |
| Eintrag anklicken | übernimmt ihn |
| Eingabetaste | übernimmt den **getippten** Text, auch wenn er nicht in der Liste steht |

Das letzte ist der Punkt, für den das Ganze da ist. Bei mehr als 500 Treffern
bricht die Liste ab.

## Der Beenden-Knopf tat nichts

Dort stand `(void)fullButton(Str::AppExit);` — das Ergebnis wurde weggeworfen.
Im Menü stand ein leerer Kommentar.

Der Knopf setzt jetzt ein Flag, und die Fensterschicht fragt es nach dem
Zeichnen ab: `ImGui` kennt kein Fenster, `PostMessage(WM_CLOSE)` gehört nach
`main_win32.cpp`. Das Flag wird beim Lesen zurückgesetzt, sonst schlösse das
Fenster in jedem folgenden Bild erneut.

## Kurzhilfen an den Knöpfen

22 Knöpfe haben eine Erklärung, die nach kurzem Verweilen an der Maus
erscheint — `ImGuiHoveredFlags_DelayNormal`, sonst flackert beim Überfahren
der Spalte ständig ein Kasten.

Die Beschreibungen der Ereignisliste erscheinen jetzt ebenfalls an der Maus.
Wer die Liste durchgeht, schaut auf die Liste; der Blick an den unteren
Fensterrand und zurück kostet mehr, als die Beschreibung wert ist. Unten steht
sie weiterhin.

## Enter schloss den Editor und oeffnete ihn sofort wieder

`drawEditor` schließt mit Enter — und danach sieht `handleTreeKeys` **dieselbe
Taste im selben Bild** und öffnet den Editor sofort wieder. Er ließ sich mit
Enter nicht schließen.

Ein Flag `editorAteKey` löst das: der Editor vermerkt, dass er die Taste
verbraucht hat, und die Baumtasten überspringen dieses Bild. Zurückgesetzt
wird es am Anfang jedes Bildes.

Dass die Reihenfolge stimmt, ist kein Zufall: `drawEditor` läuft **vor**
`handleTreeKeys`.

## Ziehen auf einen Block heißt HINEIN

`moveTo` setzt **neben** das Ziel — richtig für einen einfachen Befehl, falsch
für `affect`, `task`, `if` und `else`. Dafür gibt es jetzt `moveInto`, und die
Regel ist dieselbe wie beim Einfügen: aufgeklappt oder leer → hinein,
zugeklappt und gefüllt → daneben.

Zwei Fälle sind abgefangen: ein Block in sich selbst, und ein Block in sein
eigenes Kind. Beides würde den Baum an sich selbst hängen.

## Drei Spalten auf einer Linie

Die Modusleiste (*Ereignisse / Karte / Modell*) hatte **keine Titelzeile**,
die drei Spalten daneben schon — alle sind über `beginGroupBox` gebaut: erst
eine Textzeile mit dem Namen, dann der Rahmen. Dadurch begann die Modusleiste
eine Textzeilenhöhe weiter oben, und die Rahmen lagen sichtbar nicht auf einer
Linie.

Jetzt lässt sie dieselbe Zeile frei und zieht sie von ihrer Höhe ab.

**Dasselbe galt für die Trennlinien.** Sie waren über die volle Höhe gezogen
und ragten deshalb oben um eine Textzeile über die Rahmen hinaus. Auch sie
lassen die Titelzeile jetzt frei.

Eine Feinheit dabei: `IsItemActive` muss **vor** `EndGroup` gelesen werden —
danach bezieht es sich auf die Gruppe statt auf den Knopf, und der Trenner
ließe sich nicht mehr ziehen.

## Die Statusliste ist verstellbar

Vorher feste sechs Zeilen. Wer Prüfmeldungen liest, braucht mehr; wer an der
Karte arbeitet, will sie klein. Ein waagerechter Trenner über der Hilfszeile
stellt sie ein, der Wert wird gespeichert.

Der Trenner ist ein eigener: die Achse und der Mauszeiger sind andere, und das
Vorzeichen ist umgekehrt — die Liste wächst, wenn man **nach oben** zieht.
Untergrenze sind zwei Zeilen, sonst lässt sie sich zuziehen und nicht
wiederfinden.

## Ablaufprotokoll

```
%APPDATA%\behaved\behaved.log
```

Stürzt das Programm ab, liegt dort, was zuletzt lief. Vier Regeln, aus efxed
übernommen, weil sie dort aus echten Abstürzen entstanden sind:

1. **Nach jeder Zeile wird geleert.** Ein gepuffertes Protokoll verliert genau
   die Zeile, auf die es ankommt.
2. **Marken sind fein.** Lieber eine zu viel als eine, die drei Schritte
   zusammenfasst.
3. **Ein Schritt vermerkt seinen Beginn**, bevor er anfängt — nicht sein Ende,
   wenn er fertig ist. Ein Schritt ohne Abschlusszeile ist der Schuldige.
4. **Das vorige Protokoll wird als `behaved.log.vorher` beiseitegelegt**, nicht
   überschrieben. Sonst löscht der Neustartversuch nach dem Absturz gerade den
   Beweis.

Ein Ausschnitt sieht so aus:

```
     behaved        1.0.0
     gebaut         Aug 11 2026 11:57:03
     Windows        10.0.26100
     DPI            144
     Protokoll      C:\Users\...\AppData\Roaming\behaved\behaved.log
     ---
> Start
< Start (0.2 ms)
     Fenster erstellen
> Programmzustand aufbauen
  > Einstellungen laden
  < Einstellungen laden (1.1 ms)
  > Befehlsmodell laden: ...\data\base
  < Befehlsmodell laden (3.4 ms)
> Grafikschnittstelle starten: Direct3D 11
< Grafikschnittstelle starten  FEHLER: nicht verfuegbar (12.0 ms)
> Grafikschnittstelle starten: OpenGL 3.3
     Nachrichtenschleife laeuft
```

Dazu zwei Fänger:

* **Absturzbehandler** (`SetUnhandledExceptionFilter`): schreibt Fehlercode,
  Adresse, bei einem Speicherfehler auch lesend/schreibend und die Adresse,
  und dazu den gerade laufenden Schritt. Er **repariert nichts** und lässt den
  Absturz weiterlaufen — ein Programm, das nach einem Speicherfehler
  weitermacht, richtet mehr Schaden an, als es verhindert.
* **ImGui-Zusicherungen** landen im Protokoll statt nur im Meldungskasten der
  Laufzeitbibliothek. `IM_ASSERT` ist über `gui/imgui_config.h` umgeleitet.
  Anlass war genau so ein Fall: *"No current context. Did you call
  ImGui::CreateContext()?"* — sichtbar nur als Meldungskasten, ohne jede Spur.

## Mehrfachauswahl und Ziehen

`Strg`-Klick wählt einzeln dazu, `Umschalt`-Klick den ganzen Bereich
dazwischen — und zwar über die **angezeigten Zeilen**, nicht über die
Baumpfade: der Nutzer sieht Zeilen und erwartet genau das, was dazwischen
steht.

Löschen, Kopieren und Ausschneiden wirken dann auf alles Gewählte, als **ein**
Rückgängig-Schritt.

### Der Fallstrick dabei

Löscht man von vorn, verschieben sich alle folgenden Wege um eins, und der
zweite Aufruf trifft den falschen Knoten. Es wird etwas gelöscht — nur nicht
das Gewählte, und man merkt es erst später. `removeAll` arbeitet die Wege
deshalb **absteigend** ab.

Die Probe dazu prüft nicht die Anzahl, sondern **welche** Knoten übrig
bleiben: aus `ABCDE` muss beim Löschen von B und D genau `ACE` werden, und das
unabhängig davon, in welcher Reihenfolge man sie angeklickt hat.

Beim **Ziehen** sind zwei Fälle abgefangen, die sonst das Skript zerstören:
ein Block in sich selbst (der Baum hinge an sich selbst), und die Verschiebung
des Zielwegs, sobald die Quelle davor entnommen wird.

## Rückgängig und Wiederholen

`Strg+Z`, `Strg+Y` (auch `Strg+Umschalt+Z`), dazu zwei Knöpfe in der
Aktionen-Spalte und Einträge im Menü *Bearbeiten*.

**Das gibt es im Original nicht** — weder als Knopf noch in der Tastentabelle;
`ACCELERATOR 135` kennt kein `Strg+Z`. 2003 war ein Editor ohne Rückgängig
noch hinnehmbar, heute erwartet es jeder auf diesen Tasten. Bewusste
Ergänzung, im Quelltext an der Stelle vermerkt.

Jeder Schritt trägt einen Namen, deshalb steht im Menü **„Rückgängig:
Löschen"** statt nur „Rückgängig" — man soll vorher wissen, was passiert.

Dabei ist auch der Stern für „ungesichert" richtiggestellt worden. Er war ein
Schalter, der bei der ersten Änderung anging und nie wieder aus. Wer etwas
ändert und es zurücknimmt, hat aber nichts zu speichern — jetzt vergleicht das
Dokument die Tiefe des Rückgängig-Stapels mit dem Stand beim letzten
Speichern.

## Tastenkürzel

Abgeschrieben aus der Ressource `ACCELERATOR 135` von `BehavEd.exe`, 23
Einträge, und durch `tests/keytest.cpp` gegen diese Abschrift geprüft. Zwei
davon überraschen:

* **`Strg+A` ist „Speichern unter"** (Befehl 1007), nicht „alles markieren".
* **`Leertaste` ist „Clone"** (1010).

Der Rest: `Entf` löschen, `Rücktaste` auskommentieren, `Strg+Rücktaste`
wieder entkommentieren, `Eingabe` Event-Editor, `Einfg` einfügen, `Strg+O`
öffnen, `Strg+S` speichern, `Strg+C/X/V`, `Alt+B` Sicherung, `Alt+R`
wiederherstellen, `F3` Suche wiederholen.

Im Quelltext stand vorher „32783 auf Alt+BACK" — die Ressource sagt
`FCONTROL`, also **Strg**+Rücktaste. Korrigiert.

## Einstellungen

Eine Textdatei unter `%APPDATA%\behaved\behaved.cfg`, Schlüssel=Wert.
Bewusst schlicht: so ist Lesen und Schreiben ohne Oberfläche prüfbar, und die
Datei lässt sich von Hand reparieren.

Gespeichert werden die Felder aus Dialog 131 des Originals (Skriptpfad, Ort
von IBIZE.EXE, Command Description File, Quelldateipfad, „Re-open last file at
startup", „Alpha-sort edit pulldowns") sowie Sprache, Farbgebung, Maßstab, die
drei Baumschalter und die Liste zuletzt geöffneter Dateien.

Eine kaputte Datei darf das Fenster nicht unbedienbar machen: ein Maßstab
außerhalb 0,5–4,0 wird verworfen, unbekannte Schlüssel werden übergangen
statt die Datei abzuweisen. Beides ist geprüft.

## Übersetzung

Vier Sprachen: Englisch, Deutsch, 中文, 日本語. Die Tabelle steht in
`tools/gen_i18n.py`; `include/bhed/i18n.h` und `src/i18n.cpp` werden daraus
**erzeugt** und dürfen nicht von Hand geändert werden.

```
python3 tools/gen_i18n.py
```

Der Erzeuger prüft dabei zweierlei: dass keine Kennung doppelt vorkommt, und
dass alle vier Sprachvarianten **dieselben Formatplatzhalter** haben. Schreibt
jemand in einer Sprache `%s`, wo die anderen `%d` haben, ist das undefiniertes
Verhalten — meist ein Absturz, und nur in dieser einen Sprache.

Nicht-ASCII wird als ausdrückliche UTF-8-Bytes geschrieben (`\xe8\x84\x9a`),
nicht roh und auch nicht als `\uXXXX`. Grund: MSVC wandelt Zeichenliterale
ohne `/utf-8` in die ANSI-Codepage um — die chinesischen Texte wären dann
Müll, und zwar nur beim Übersetzen mit MSVC.

`tools/lint_i18n.py` sucht Texte, die fest im Quelltext stehen statt durch
`tr()` zu gehen.

## Farbgebungen

Sechs Stück, alle mit Kontrastprüfung nach WCAG:

| Kennung | was es ist | Kontrast |
|---|---|---|
| `behaved-classic` | das Grau von Windows 95, wie 2003 | 13,7:1 |
| `windows` | das helle Aussehen unter Windows 11 | 13,3:1 |
| `dark` | dunkel | 9,1:1 |
| `midnight` | dunkelblau | 11,0:1 |
| `light` | hell | 13,9:1 |
| `high-contrast` | Schwarz/Weiß/Gelb | 17,4:1 |

Alle erreichen AAA (7:1). Ein Test verlangt mindestens AA (4,5:1), damit eine
neu erfundene Farbgebung nicht unbemerkt unlesbar wird.

## Portierungsprüfer

```
python3 tools/lint_portability.py
```

Prüft, dass jede benutzte `std::`-Funktion ihre Kopfdatei eingebunden hat und
dass kein Zeichen außerhalb ASCII im Quelltext steht. GCC reicht viele
Kopfdateien durch, MSVC nicht — solche Fehler merkt man sonst erst auf dem
anderen Rechner. Beim Einbau fand er 41 Stellen.


## Ein Absturz beim Abbrechen

Escape im Event-Editor brach das Programm ab:

```
IMGUI-ZUSICHERUNG: Code uses SetCursorPos() to extend window/parent
boundaries. Please submit an item e.g. Dummy() afterwards.
```

Der Escape-Zweig stand **direkt hinter** einem `SetCursorPosX` und rief sofort
`EndPopup` und `return` — ohne ein einziges Element dazwischen. ImGui muss
dann annehmen, dass jemand die Fenstergrenzen verschoben hat, ohne etwas
hineinzuzeichnen, und bricht ab.

Zwei Fehler in einem: die Tastenprüfung stand **hinter** der
Cursorverschiebung, und sie verließ die Funktion vorzeitig. Jetzt wird zuerst
geprüft, dann gezeichnet, und Escape wirkt auf denselben Zweig wie der
Abbrechen-Knopf — das Fenster wird regulär fertig.

`tools/lint_imgui_context.py` sucht das Muster jetzt: ein `return`, `EndPopup`
oder `EndChild` innerhalb von acht Zeilen nach einem `SetCursorPos`, ohne dass
dazwischen ein Element abgeschickt wird. **Zur Übersetzungszeit ist das
unsichtbar** — es fällt erst auf, wenn jemand die Taste drückt.

## Kopfdateien sind eigenständig

`tools/lint_headers.py` übersetzt **jede** Kopfdatei einzeln in einer sonst
leeren Datei. Eine Kopfdatei, die nur funktioniert, weil zufällig vorher eine
andere eingebunden wurde, ist eine Falle: sie bricht, sobald jemand die
Reihenfolge ändert — und beim normalen Bauen fällt das **nie** auf.

Geprüft wird dazu, dass jeder Einbindewächter eindeutig ist und dass kein
`using namespace` darin steht. Alle 23 Kopfdateien bestehen; der Prüfer selbst
ist gegengeprüft.

## noexcept, wo es belegbar stimmt

Elf Stellen: `empty()`, `isTag()`, `isDrawn()`, die Matrixrechnung in
`BoneMatrix` und `shortestAngleDelta`. Alles reine Rechnung auf Feldern oder
ein Aufruf, der selbst `noexcept` ist.

Das ist keine Zierde: die Matrizen werden je Bild für 53 Knochen aufgerufen,
und der Übersetzer darf mit `noexcept` einbetten und die Ausnahmetabellen
weglassen. Überall sonst steht es **nicht** — ein `noexcept` auf einer
Funktion, die doch wirft, beendet das Programm sofort.

## Den Direct3D-Zweig auf Linux übersetzen und binden

Bis rc429 galt: „der D3D-Code ist blind geschrieben". Das muss nicht sein.
MinGW-w64 bringt die echten `d3d11.h`, `dxgi.h` und `d3dcompiler.h` mit,
samt Importbibliotheken — und lässt sich ohne Rechte am System einrichten:

```sh
mkdir -p /tmp/deb && cd /tmp/deb
apt-get download mingw-w64-common mingw-w64-x86-64-dev \
                 g++-mingw-w64-x86-64 g++-mingw-w64-x86-64-posix \
                 gcc-mingw-w64-base gcc-mingw-w64-x86-64 \
                 gcc-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64
for f in *.deb; do dpkg-deb -x "$f" ~/mingw; done
ln -sf x86_64-w64-mingw32-g++-posix ~/mingw/usr/bin/x86_64-w64-mingw32-g++
ln -sf x86_64-w64-mingw32-gcc-posix ~/mingw/usr/bin/x86_64-w64-mingw32-gcc
export PATH=~/mingw/usr/bin:$PATH
```

`tools/check_gui.sh` sucht den Übersetzer selbst und prüft dann auch die
Windows-Dateien. Für den Bindeschritt braucht er zusätzlich die
ImGui-Objektdateien unter `/tmp/imgui_*.o`:

```sh
I=$HOME/imgui          # git clone --depth 1 https://github.com/ocornut/imgui
for f in imgui imgui_draw imgui_tables imgui_widgets; do
  x86_64-w64-mingw32-g++ -std=c++20 -O0 -I"$I" -Igui \
    -DIMGUI_USER_CONFIG='"imgui_config.h"' -c "$I/$f.cpp" -o /tmp/imgui_$f.o
done
for f in imgui_impl_win32 imgui_impl_dx11 imgui_impl_opengl3; do
  x86_64-w64-mingw32-g++ -std=c++20 -O0 -I"$I" -I"$I/backends" -Igui \
    -DIMGUI_USER_CONFIG='"imgui_config.h"' -c "$I/backends/$f.cpp" \
    -o /tmp/imgui_$f.o
done
IMGUI=$I sh tools/check_gui.sh
```

Was das **kann**: falsche Feldnamen, falsche Aufzählungswerte, vertauschte
Argumente, fehlende Methoden einer Schnittstelle, Signaturabweichungen
zwischen Kopf und Definition. Also alles, was sonst als Bauprotokoll von
shank zurückkam.

Was es **nicht** kann: ein falsches Bild. Dafür braucht es weiterhin shank.

Für `tools/lint_tidy.py` zusätzlich `clang-tidy-18`, `libclang-cpp18`,
`libllvm18`, `libtinfo6`, `libz3-4`, `libedit2`, `libicu74` auf demselben
Weg, dann
`LD_LIBRARY_PATH=~/clang/usr/lib/x86_64-linux-gnu:~/clang/usr/lib/llvm-18/lib`.

## clang-tidy braucht die eingebauten Kopfdateien

`tools/lint_tidy.py` sucht sie selbst. Wenn clang aus einem eigenen
Auspackort statt aus der Systeminstallation kommt, hilft:

```sh
export BHED_TIDY_INCLUDE=~/clang/usr/lib/llvm-18/lib/clang/18/include
```

Auf Ubuntu stecken sie in `libclang-common-18-dev` — **nicht** in
`clang-tidy` selbst. Ohne sie bricht clang-tidy an `stddef.h` ab und meldet
danach Warnungen aus halb geparstem Quelltext. Der Prüfer erkennt das jetzt
und meldet einen Ausfall statt Befunde; bis rc445 zählte er den Schrott mit
(36 Beanstandungen, alle unecht).

GCCs Satz reicht **nicht**: sein `xmmintrin.h` ruft Befehle auf, die clang
nicht kennt, und `mapview.cpp` scheitert daran.
