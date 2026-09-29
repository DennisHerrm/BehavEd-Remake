#!/usr/bin/env python3
"""Verlaesst ein `return` die Funktion zwischen BeginTable und EndTable?

Warum das ein eigener Pruefer ist:

rc501 stuerzte beim Start ab:

    IMGUI-ZUSICHERUNG: (0) && "Missing EndTable()"

`drawEventsList` hat ZWEI Ausstiege - einen fuer Karte und Modell, einen am
Ende. Beim Umbau auf eine Tabelle habe ich nur den letzten gesehen und in
den Kommentar geschrieben, es gaebe keinen anderen. Gesucht hatte ich nicht.

Der Uebersetzer sagt dazu nichts: `return` ist gueltig, das fehlende
`EndTable` faellt erst zur Laufzeit auf - und dann sofort und hart.

Geprueft wird: in jeder Funktion, die `BeginTable` aufruft, muss auf jedes
`return` nach dem BeginTable ein `EndTable()` in den drei Zeilen davor
kommen. Der Ausstieg direkt bei fehlgeschlagenem BeginTable ist
ausgenommen - dort gibt es keine offene Tabelle.
"""
import re
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent


def main() -> int:
    fehler = 0
    for datei in sorted((HIER.parent / "gui").glob("*.cpp")):
        t = re.sub(r"//[^\n]*", "", datei.read_text(encoding="utf-8"))
        zeilen = t.split("\n")
        offen = False
        seitBegin = 0
        for nr, zeile in enumerate(zeilen):
            if "BeginTable(" in zeile:
                offen = True
                # Der Aufruf kann ueber mehrere Zeilen laufen. Der Ausstieg
                # bei GESCHEITERTEM BeginTable steht direkt hinter der
                # schliessenden Klammer - ab dort wird gezaehlt, nicht ab
                # dem Namen.
                tiefe = zeile.count("(") - zeile.count(")")
                seitBegin = nr
                k = nr
                while tiefe > 0 and k + 1 < len(zeilen):
                    k += 1
                    tiefe += zeilen[k].count("(") - zeilen[k].count(")")
                seitBegin = k
                continue
            if "EndTable()" in zeile:
                offen = False
                continue
            if not offen or "return" not in zeile:
                continue
            # Der Ausstieg unmittelbar nach einem gescheiterten BeginTable
            # zaehlt nicht - dort ist keine Tabelle offen.
            if nr - seitBegin <= 3:
                continue
            umfeld = "\n".join(zeilen[max(0, nr - 3) : nr])
            if "EndTable()" in umfeld:
                continue
            print(
                "  %s:%d - `return` zwischen BeginTable und EndTable, ohne "
                "EndTable() davor. ImGui bricht dann zur Laufzeit ab: "
                '"Missing EndTable()".' % (datei.name, nr + 1)
            )
            fehler += 1
    if fehler:
        print("EndTable-Pruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("EndTable-Pruefung: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
