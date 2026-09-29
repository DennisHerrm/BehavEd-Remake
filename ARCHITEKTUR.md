# behaved — Aufbau und Entscheidungen

## Was schon gemessen ist

| Frage | Antwort | Beleg |
|---|---|---|
| 64 Bit | keine Annahme über Zeigergrößen; `std::size_t` durchgehend, kein `int` für Längen, `std::from_chars` statt `atoi` | ASan + UBSan sauber über 1510 Skripte und 20 000 Fuzz-Runden |
| Threads im Kern | nicht nötig | ein Skript öffnen ≈ 0,05 ms; Modell laden 3 ms; alle 1510 Skripte 78 ms |
| Threads später | ja, für `.pk3`-Suche und Modellauflösung | aus efxed übernehmbar: `jobs` (243 Zeilen, ThreadSanitizer-geprüft, deterministisch) |
| GPU | nur für die Ansicht, nicht für den Kern | Skripteditor rechnet nichts, was auf die GPU gehört. Erst der Kartenteil braucht sie — dafür liegen in efxed zwei Schnittstellen bereit (D3D11, GL 3.3 mit Rückfall) |

## Warum kein Thread im Kern

Gemessen, nicht vermutet:

```
Modell laden :  3.0 ms   (26 Typmengen, 2085 Einträge aus 11 Kopfdateien)
Dateien lesen: 11.4 ms   (1510 Dateien, 1,6 MiB)
Zerlegen     : 44.6 ms
Schreiben    :  3.6 ms
Prüfen       : 19.6 ms
```

Ein Faden aufzumachen kostet mehr, als er hier spart. Anders wird es erst, wenn
`.pk3`-Archive nach Klängen, Modellen und Texturen durchsucht werden — das ist
E/A-lastig und lohnt sich. Dafür kommt der geprüfte Fadenpool aus efxed.

Eine Warnung an mich selbst: die erste Messung sagte 112 ms fürs Modell laden.
Das war **kalter Plattenzugriff auf ein eingehängtes Verzeichnis**, keine
Rechenzeit. Aus einer einzelnen Messung nichts folgern.

## Werkzeuge, die mitlaufen

* `-Wall -Wextra -Werror`, C++20, GCC und MSVC
* AddressSanitizer + UndefinedBehaviorSanitizer über den gesamten Bestand
* Fuzzer: 20 000 Runden mutierter Skripte, kein Absturz, immer Festpunkt
* clang-tidy mit `bugprone-*`, `cppcoreguidelines-*`, `performance-*`,
  `modernize-*`, `readability-*` — keine Speicher- oder Lebensdauerbefunde
* `[[nodiscard]]` an allen Ein-/Ausgabefunktionen; hat beim Einbau sofort vier
  Stellen gefunden, an denen ein Rückgabewert ignoriert wurde

## Grundsätze

1. **Die Befehlsliste wird erzeugt, nicht gepflegt.** `.bhc` + Engine-Kopfdateien.
2. **Der Editor weist nichts ab.** Unbekannte Werte (`SET_COLLIDABLE_ROFFS`,
   `BOTH_TALKGESTURE2`) sind Anmerkungen und überleben den Rundlauf unverändert.
3. **Die Datei bleibt, wie sie war.** Leerzeilen und Kommentare sind Knoten.
   1509 von 1510 Skripten sind nach lesen+schreiben bytegleich; der eine
   Unterschied ist Ravens eigener Tippfehler.
4. **Falten ist Anzeige, nicht Format.** Makros und Blöcke werden im Baum
   gefaltet, die Datei bleibt flach.
5. **Jede Regel hat eine Gegenprobe.** 22 Prüfungen, davon 6 Fälle, die *nicht*
   anschlagen dürfen.

## Aufbau

```
include/bhed/script.h    Baummodell der .icarus-Datei
include/bhed/commands.h  Befehle, Typmengen, Parameter
include/bhed/validate.h  Prüfregeln V001..V011
include/bhed/tree.h      Zeilen der Baumansicht, Faltung
src/                     die vier Umsetzungen
tests/roundtrip.cpp      bytegleicher Rundlauf über den Raven-Bestand
tests/checkall.cpp       Modellprüfung über den Raven-Bestand
tests/checks.cpp         Gegenproben
tests/fuzz.cpp           kaputte Eingaben
tests/treedump.cpp       Baum als Text
tools/                   Ressourcenleser, Erzeuger, verify.sh
```
