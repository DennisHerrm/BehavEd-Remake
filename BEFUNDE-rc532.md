# Befunde rc531/rc532 — Skriptfunktionen und Debuggen

Stand 7. September 2026. Grundlage: rc530, shanks zwei Protokolle, das
offizielle ICARUS-Handbuch (OpenJK-Wiki), und eigene Messungen.

---

## 1. Was in rc531 behoben ist

Beides aus shanks eigenem Protokoll bewiesen, beides in `AENDERUNGEN.md`
ausführlich. Kurz:

| Fehler | Ort | Wirkung |
|---|---|---|
| Feld nur zur Hälfte belegt | `gui/gpumap_win32.cpp` | 7 von 8 GPU-Zeiten meldeten 0,000 ms statt „nicht gemessen" |
| `Row::name` bei Makrozeilen leer | `src/tree.cpp` | `Aufnehmen: ""` genau für den Zeilentyp, um den es ging |
| `verify.sh` prüfte 19 Quellen nicht | `tools/verify.sh` | u.a. alle sechs `gpu*.cpp`, `glow.cpp`, `efxdraw.cpp` |

Neu: `tools/lint_feldinit.py` mit echtem Gegentest (8 von 8).

---

## 1a. Was rc532 dazugelegt hat (nach Lieferung der Originaldateien)

DH hat `behaved.bhc`, `BehavEd.exe`, `ICARUS_Manual.doc` und alle elf
Kopfdateien geliefert. Damit ließ sich prüfen statt annehmen.

**Alles Daten im Paket ist bytegleich mit dem Original** — dreizehn
Dateien, kein Abdriften. Die `//# #eol`-Marke wird beachtet: 2085
Einträge sichtbar, 20 verworfen. `tests/tabellen.cpp` hält das fest.

**Der Fund:** `ICARUS_Manual.doc` ist von 2002 und damit älter als Jedi
Academy. Sechs Set-Felder heißen dort anders, jedes Mal um genau einen
Unterstrich:

```
SET_BEHAVIORSTATE  ->  SET_BEHAVIOR_STATE
SET_COPYORIGIN     ->  SET_COPY_ORIGIN
SET_DEFAULTBSTATE  ->  SET_DEFAULT_BSTATE
SET_ENEMYTEAM      ->  SET_ENEMY_TEAM
SET_LOCKEDENEMY    ->  SET_LOCKED_ENEMY
SET_PLAYERTEAM     ->  SET_PLAYER_TEAM
```

`V004` schlägt korrekt an, sagt aber jetzt auch, was gemeint war.
Schwelle gemessen: Abstand ≤2 gibt null Fehlvorschläge, ≤3 einen.
Zwölf neue Gegenproben. Details in `AENDERUNGEN.md`.

---

## 2. Die Skriptfunktionen: was geprüft wird und was nicht

### 2.1 Was heute geprüft wird

`src/validate.cpp` kennt elf Regeln, `V001` bis `V011`. Sie decken die
**Form** ab:

```
V001/V002/V010   Befehl steht nicht in der .bhc, unerwarteter Block { }
V004..V009,V011  Feld erwartet Vektor / Text ohne Anfuehrungszeichen /
                 Vergleich erwartet = < > ! / Zahl erwartet ...
```

Das ist der Teil, den IBIZE auch findet. Er ist vollständig und stimmt —
`checkall` läuft über den ganzen Raven-Bestand.

### 2.2 Was **nicht** geprüft wird

Das offizielle Handbuch beschreibt eine Reihe von Regeln, bei deren
Verletzung das Skript **fehlerfrei übersetzt** und dann im Spiel falsch
läuft oder hängen bleibt. Keine davon steht heute in `validate.cpp`.

Die acht, die sich mechanisch prüfen lassen, stehen jetzt in
`tools/icarus_regeln.py`. Wortlaut jeweils aus `ICARUS_Manual.doc`
selbst — seit rc532 nicht mehr aus dem Wiki:

**R1 — Aufgabe in einer Schleife.**
„define them only once. You may execute the task as many times as you
want, but define it only once." Das Beispiel im Handbuch ist
`loop(50) { task{...} dowait(...) }` und wird ausdrücklich als falsch
bezeichnet: die Aufgabe wird fünfzigmal angelegt.

**R2 — `wait("name")` ohne vorheriges `do`.**
„If you use the wait without using the do command first, your script will
NEVER CONTINUE! In other words, don't do that." Das ist die schlimmste
Sorte: kein Absturz, keine Meldung, das Skript steht einfach.

**R3 — `do`/`dowait` auf eine Aufgabe, die es hier nicht gibt.**
„A task is unique to the entity it is defined on." Wer in einem `affect`
eine Aufgabe abruft, die draußen angelegt wurde, bekommt im Spiel
„cannot find task block". Das Handbuch nennt es „a common error at first".

**R4 — Aufgabe ohne einen Befehl, auf den man warten kann.**
„any command that does NOT complete immediately is marked by an asterisk.
If you are going to make a task, you have to have at least one of these
commands in it, otherwise your task will complete as soon as you start
it." Ein `task`, in dem nur `set("SET_HEALTH", ...)` steht, ist sofort
fertig — das `dowait` wartet auf nichts.

**R5 — Klang auf einem Kanal, auf den man nicht warten kann.**
Wartbar sind nur `CHAN_VOICE`, `CHAN_VOICE_ATTEN` und
`CHAN_VOICE_GLOBAL`. „You cannot wait for sounds played on any of the
other channels." Ein `task` mit `sound(CHAN_AUTO, ...)` sieht richtig aus
und wartet nicht.

**R6 — `get` auf eine Variable ohne `declare`.**
„All variables must be declared before being used." Dazu: es gibt nur
**32 je Typ**, weshalb `free` existiert.

**R7 — `else` ohne unmittelbar vorangehendes `if`.**
Steht so in der `.bhc`: „must immediately follow an if".

**R8 — zwei verschiedene Aufgaben mit demselben Namen.**
„The TASKNAME is anything you want, though you should not use the same
TASKNAME for two different tasks."

### 2.3 Was die Messung sagt

Gegen die vier echten Raven-Skripte im Paket:

```
cin2_jedi.txt          0 Befunde
cin3_jedi.txt          0 Befunde
gonkability.txt        0 Befunde
intro_jedi.txt         0 Befunde
```

Gegen eine absichtlich kaputte Datei: alle acht schlagen an.

**Das heißt: Raven hat sich an die eigenen Regeln gehalten.** Für den
Bestand von 939 Zeilen bringen die Regeln nichts. Der Nutzen liegt bei
**neu geschriebenen** Skripten — Movie Duels, dein eigener Bestand —, wo
niemand das Handbuch auswendig kann.

**Ehrlich zu Grenzen:** `icarus_regeln.py` hat einen eigenen, groben
Zerleger, nicht `src/script.cpp`. Beim ersten Lauf meldete R7 sieben
Verstöße in `gonkability.txt`, und alle sieben waren **richtig
geschriebene** if/else — mein Zerleger merkte sich den zuletzt gesehenen
Befehl statt den zuletzt geschlossenen Block. Behoben und mit
Gegentest belegt. Genau deshalb steht das Werkzeug **neben** dem
Programm und nicht darin: erst gegen deinen Bestand laufen lassen,
dann entscheiden, ob es sich lohnt, die Regeln in `validate.cpp`
aufzunehmen.

**Nächster Schritt, wenn du willst:**
```sh
python3 tools/icarus_regeln.py '/pfad/zu/deinen/movieduels/scripts/*.txt'
```
Erst die Zahl, dann der Umbau.

### 2.4 Zwei Regeln, die sich **nicht** mechanisch prüfen lassen

Der Vollständigkeit halber, damit sie nicht als vergessen dastehen:

* **`affect` wartet nicht.** „affects are not executed in the script you
  are running, they are shoved onto another entity." Das Handbuch nennt
  es „a very important rule and is easy to forget" — aber ein Skript, das
  das falsch annimmt, ist syntaktisch tadellos. Nur ein Mensch sieht das.
* **`flush` löscht laufende Schleifen.** Erwünscht oder nicht, das weiß
  nur der Autor.

---

## 3. Debuggen: was für **dieses** Programm passt

Die Lage ist besonders: du schreibst blind unter Linux, shank baut unter
Windows/MSVC und schickt Protokolle. Werkzeuge, die einen Debugger am
laufenden Programm brauchen, helfen dir also nicht.

### 3.1 Der größte Gewinn: aus `0xC0000005` eine Zeile machen

Nach `UEBERGABE.md` haben Zugriffsverletzungen mehrere Runden gekostet.
Heute liefert ein Absturz nichts als eine Meldung von Windows.

Das lässt sich ändern, ohne dass shank irgendetwas tun muss. Der Weg:

1. `SetUnhandledExceptionFilter` beim Start setzen.
2. Im Filter `RtlCaptureStackBackTrace` — die Rücksprungadressen.
3. **Modulbasis** dazuschreiben (`GetModuleHandle(nullptr)`).
4. Alles ins Protokoll. Fertig — shank schickt wie immer die Datei.
5. Du löst die Adressen **bei dir** auf:
   `addr2line -e behaved.exe -f -C 0x…`

Das habe ich hier durchgespielt. Aus nackten Zahlen im Protokoll:

```
ABSTURZ  Modulbasis 0x400000
  Rahmen  0  absolut 0x40120c   relativ 0x120c
  Rahmen  2  absolut 0x4012a5   relativ 0x12a5
  Rahmen  3  absolut 0x4012b5   relativ 0x12b5
  Rahmen  4  absolut 0x4012c7   relativ 0x12c7
```

wird:

```
0x4012a5  ->  tief_drei(int*)  at sturz.cpp:23     <- hier knallt es
0x4012b5  ->  tief_zwei(int*)  at sturz.cpp:24
0x4012c7  ->  tief_eins(int*)  at sturz.cpp:25
0x4012ef  ->  main             at sturz.cpp:30
```

Die ganze Aufrufkette, aus einer Textdatei.

**Warum das bei euch gut passt:** MinGW legt DWARF-Informationen in die
`.exe`; `addr2line` kann sie direkt lesen. Du brauchst **dieselbe**
unstrippte `behaved.exe`, die shank gestartet hat — die hast du, du hast
sie gebaut. Kein PDB, kein Symbolserver, keine Fremdbibliothek.

**Vorbehalt:** ich habe das unter **Linux** gemessen, nicht unter
Windows — hier gibt es kein MinGW. Der Windows-Teil
(`SetUnhandledExceptionFilter` + `RtlCaptureStackBackTrace`) ist aus der
Dokumentation, nicht aus eigener Messung. Erste Runde deshalb klein: ein
absichtlicher Nullzeiger hinter einem versteckten Menüpunkt, einmal
auslösen, nachsehen ob die Zeilen stimmen.

### 3.2 Minidumps: möglich, aber unangenehm

Der offizielle Weg wäre `MiniDumpWriteDump` aus DbgHelp. Zwei Gründe,
warum ich zuerst zu 3.1 raten würde:

* **DbgHelp ist nicht threadsicher.** Microsoft schreibt ausdrücklich,
  dass ein Programm, das `MiniDumpWriteDump` benutzt, vorher alle Threads
  gleichschalten muss — bei euren fünf Thread-Stellen kein kleines Ding.
* **Im eigenen Prozess wird es oft unbrauchbar.** Microsoft nennt als
  einzig sicheren Ort einen **anderen** Prozess; in einem ausführlichen
  Erfahrungsbericht kamen bei Aufrufen aus dem eigenen Prozess Dumps
  heraus, deren Aufrufkette nicht einmal WinDBG auflösen konnte, unter
  Clang wie unter MSVC. Der Ausweg ist ein zweites kleines Programm, das
  vom Filter gestartet wird und den Dump von außen zieht — dann stimmt
  die Kette.

Dazu zwei Dinge, die man kennen sollte:
* **Stapelüberlauf fängt `SetUnhandledExceptionFilter` nicht.**
* Der Filter sollte `EXCEPTION_CONTINUE_SEARCH` zurückgeben, damit die
  Windows-Fehlerberichterstattung danach normal weiterläuft.

Ein Textprotokoll nach 3.1 kostet dich einen Abend und keine dieser
Fallen. Ein Dump ist der Schritt danach, wenn die Kette allein nicht
reicht — er hat dafür die **Daten** dabei, nicht nur die Stelle.

### 3.3 Was ihr schon habt und was fehlt

| Werkzeug | Stand |
|---|---|
| D3D-Debugschicht | **läuft**, Meldungen im Bericht, `WKPDID_D3DDebugObjectName` gesetzt |
| GPU-Zeitmessung | **da und richtig** — `Disjoint` und `Frequency==0` geprüft, `DONOTFLUSH`, Ringpuffer über mehrere Bilder |
| RenderDoc | Menüpunkt `renderDocAufnehmen()` vorhanden |
| ASan/UBSan | im `verify.sh` bei den beiden Fuzzern |
| 28 Prüfer | laufen, aber **nur bei dir** — shank hat kein Python |
| Absturzkette | **fehlt** — das ist 3.1 |
| TSan über die fünf Thread-Stellen | **fehlt** — geht nur bei dir, MSVC hat keinen ThreadSanitizer |

Zu RenderDoc: die Schnittstelle für den laufenden Prozess kann mehr als
der Menüpunkt heute nutzt. `StartFrameCapture()`/`EndFrameCapture()`
klammern einen Abschnitt; Geräte- und Fensterzeiger dürfen null sein,
wenn es nur ein Gerät und höchstens ein Fenster gibt. Dazu ein
Vorlagenmuster für den Dateinamen mit `{app}`, `{frame}`, `{timestamp}`,
eine Beschriftung und die Option `ApiValidation`, die auf D3D11 die
Fehlersuchschicht einschaltet und deren Meldungen mit in die Aufnahme
schreibt. Damit könnte behaved auf Schalter selbst eine `.rdc` neben das
Protokoll legen — shank schickt beides, du siehst jeden Zeichenaufruf.

### 3.4 Die Regel, die sich in diesem Projekt schon zweimal bewiesen hat

Aus Handout Abschnitt 2, und rc531 ist der dritte und vierte Fall:

> Wer eine Debug-Zeile einbaut, prüft, ob sie auch im **Normalfall**
> kommt — nicht nur im Sonderfall. Und ob sie bei Wiederholung verstummt.

Ergänzung aus rc531:

> **Und ob sie überhaupt etwas hineinschreibt.** `Aufnehmen: ""` und
> `Karte gemischt 0.000` sind beide Zeilen, die pflichtschuldig kommen
> und nichts sagen. Eine Zahl, die man nie gesetzt hat, ist keine Zahl,
> die stimmt.

---

## 4. Was offen bleibt

1. **`MAKROZEILE` nur im Knopfzweig.** shank zieht. Wenn der Hinweis die
   Frage „warum tut es nichts, wenn ich ziehe" beantworten soll, gehört
   er auch in den Ziehzweig — dort aber nur für `ausMakro`-Zeilen, denn
   die Makrozeile **selbst** lässt sich sehr wohl bewegen. Genau das
   zeigt sein Protokoll.
2. **`icarus_regeln.py` gegen deinen echten Bestand laufen lassen.** Erst
   die Zahl, dann entscheiden, ob die Regeln nach `validate.cpp` gehören.
3. **Absturzkette ins Protokoll** (3.1).
4. **`-Werror` und neuere Übersetzer.** Mit GCC 13.3 scheitert der Bau an
   `src/mapview.cpp:2297` (`lmCache`) mit `-Wmaybe-uninitialized`. Die
   Warnung ist falsch — `lmCacheX` steht auf `INT_MIN`, der erste
   Vergleich greift immer —, aber GCC sieht das nicht. Falls dein
   Übersetzer schweigt, betrifft es dich heute nicht; sobald du hochziehst
   oder jemand anders baut, steht `verify.sh` sofort. Ich habe es **nicht**
   angefasst — ein `= {}` wäre die ruhige Lösung, aber das ist deine
   Entscheidung, nicht meine.
5. Die acht GPU-Punkte aus `UEBERGABE.md` Abschnitt 5, unverändert.
