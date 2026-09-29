#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Haelt die Flagmeldung der Effekte ehrlich.

Warum es diese Pruefung braucht
-------------------------------
Im Protokoll des Nutzers stand, Stand rc357:

    Flags, die behaved NICHT auswertet (der Zeichner beachtet derzeit
    kein einziges):
       6x impactFx - kein Aufpralleffekt
       1x useAlpha - Alpha nicht als Helligkeit
       2x usePhysics - keine Schwerkraft

Alle drei waren zu diesem Zeitpunkt laengst gebaut - `usePhysics` seit
rc319, `useAlpha` und `impactFx` danach. Die Meldung war beim Schreiben
richtig und ist mit jedem Einbau ein Stueck falscher geworden, weil niemand
sie mitgezogen hat.

Eine Meldung, die das Gegenteil der Wahrheit sagt, ist schlimmer als keine.
Sie schickt den Leser hinter einem Fehler her, den es nicht gibt - und
verschweigt die, die es gibt.

Das ist kein Fluechtigkeitsfehler, den man sich merkt. Es ist genau die
Sorte, gegen die es einen Pruefer braucht.

Wie sie arbeitet
----------------
In `gui/app_view3d.cpp` steht eine Tabelle:

    {efx::kFlagUseAlpha, "useAlpha", "...", true},

Das letzte Feld behauptet, ob das Flag ausgewertet WIRD. Diese Behauptung
laesst sich pruefen: ein ausgewertetes Flag muss in `src/` mindestens
einmal vorkommen - dort steht der Zeichner. Ein nicht ausgewertetes darf es
nicht.

Die Tabelle selbst und der Leser (`src/efx_effect.cpp`, der die Namen den
Bits zuordnet) zaehlen nicht mit: dort steht jedes Flag, gebaut oder nicht.

Was sie NICHT kann
------------------
Sie prueft, ob ein Flag ANGEFASST wird, nicht ob es RICHTIG angefasst wird.
Ein `if (flags & kFlagX) {}` mit leerem Rumpf wuerde durchgehen. Das ist die
Grenze einer Textpruefung, und sie ist hier akzeptabel: der Fehler, den es
gab, war ein vergessener Haken, kein falscher Rumpf.
"""

import re
import sys
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent
TABELLE = WURZEL / "gui" / "app_view3d.cpp"
# Der Leser ordnet Namen den Bits zu - dort steht jedes Flag, auch ein
# ungebautes. Er darf also nicht als Nachweis zaehlen.
NICHT_ALS_NACHWEIS = {"efx_effect.cpp"}


def zeichnerdateien():
    for p in sorted((WURZEL / "src").glob("*.cpp")):
        if p.name not in NICHT_ALS_NACHWEIS:
            yield p


def main():
    if not TABELLE.exists():
        print("Effektflagpruefung: " + TABELLE.name + " fehlt")
        return 1
    text = TABELLE.read_text(encoding="utf-8", errors="replace")

    # {efx::kFlagUseAlpha, "useAlpha", "irgendein Text", true},
    eintraege = re.findall(
        r'\{\s*efx::(kFlag\w+)\s*,\s*"([^"]*)"\s*,\s*"[^"]*"\s*,'
        r'\s*Stand::(\w+)\s*\}',
        text,
    )
    if not eintraege:
        print("Effektflagpruefung: keine Flagtabelle gefunden - Aufbau "
              "geaendert? Dann gehoert dieser Pruefer nachgezogen.")
        return 1

    benutzt = {}
    for p in zeichnerdateien():
        inhalt = p.read_text(encoding="utf-8", errors="replace")
        for bit, _name, _ in eintraege:
            if re.search(r"\befx::" + bit + r"\b", inhalt):
                benutzt.setdefault(bit, []).append(p.name)

    klagen = []
    erlaubt = {"Gebaut", "Fehlt", "OhneWirkung"}
    for bit, name, stand in eintraege:
        wo = benutzt.get(bit, [])
        if stand not in erlaubt:
            klagen.append(name + " hat den unbekannten Stand " + stand)
            continue
        if stand == "Gebaut" and not wo:
            klagen.append(
                name + " (" + bit + ") ist als GEBAUT eingetragen, kommt "
                "aber in keiner Datei unter src/ vor")
        # `Fehlt` und `OhneWirkung` bedeuten beide: der Zeichner fasst es
        # nicht an. Der Unterschied liegt darin, OB etwas fehlt - und das
        # kann kein Textpruefer entscheiden.
        if stand in ("Fehlt", "OhneWirkung") and wo:
            klagen.append(
                name + " (" + bit + ") ist als " + stand.upper() +
                " eingetragen, wird aber in " + ", ".join(wo) +
                " ausgewertet")

    for k in klagen:
        print(TABELLE.name + ": " + k)
    if klagen:
        print("Effektflagpruefung: " + str(len(klagen)) +
              " Beanstandung(en)")
        return 1
    print("Effektflagpruefung: ok (" + str(len(eintraege)) + " Flags, " +
          str(sum(1 for _, _, st in eintraege if st == "Gebaut")) +
          " gebaut, " +
          str(sum(1 for _, _, st in eintraege if st == "Fehlt")) +
          " fehlend, " +
          str(sum(1 for _, _, st in eintraege if st == "OhneWirkung")) +
          " ohne Wirkung)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
