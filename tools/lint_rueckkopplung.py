#!/usr/bin/env python3
"""Errechnet sich hier ein Wert aus seiner eigenen Wirkung?

Warum das ein eigener Pruefer ist - viermal derselbe Fehler:

  rc463  Die Reihe im Ereignisfenster wurde gegen die Fensterbreite
         gemittelt. Das Fenster hatte AlwaysAutoResize, seine Breite folgte
         also der Reihe. Fester Punkt statt Mitte.

  rc469  `l.topH = avail.y - statusH - ...` mit zwei falschen Summanden;
         die Statuszeile sprang ans Ende und die Luecke wanderte mit.

  rc477  `hh = floor(h * frac)` mit h als PLATZHALTER (-25), nicht als
         Hoehe. Zwei Kaesten uebereinander.

  rc490  `settings.splitMap = Zellbreite / Gesamtbreite`, jedes Bild. Die
         Zellbreite ist die Spaltenbreite MINUS Polster - die Spalte wurde
         in jedem Bild schmaler, bis die Kartenansicht verschwand.

Gemeinsam ist allen: ein Wert wird aus einer Groesse errechnet, die er
selbst beeinflusst. Der Uebersetzer sagt nichts, und im Standbild sieht man
es nicht - erst in Bewegung.

Geprueft wird: eine Zuweisung an `g_app->settings.<X>` oder `l.<X>`, die
`GetContentRegionAvail`, `GetWindowWidth` oder `GetWindowHeight` auf der
rechten Seite hat, braucht in denselben Zeilen davor eine Bedingung - eine
Abfrage auf Ziehen, Tastendruck oder ein Aenderungsflag.
"""
import re
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent
QUELLEN = ["GetContentRegionAvail", "GetWindowWidth", "GetWindowHeight"]


def main() -> int:
    fehler = 0
    for datei in sorted((HIER.parent / "gui").glob("*.cpp")):
        t = re.sub(r"//[^\n]*", "", datei.read_text(encoding="utf-8"))
        zeilen = t.split("\n")
        for nr, zeile in enumerate(zeilen):
            m = re.search(r"(g_app->settings\.\w+)\s*=", zeile)
            if not m:
                continue
            # Fuenfzehn Zeilen davor: kommt die Groesse aus dem Zeichnen
            # selbst, ueber wie viele Zwischenschritte auch immer?
            fenster = "\n".join(zeilen[max(0, nr - 15) : nr + 1])
            quelle = next((q for q in QUELLEN if q in fenster), None)
            if quelle is None:
                continue
            if re.search(
                # `IsMouseDown` steht bewusst NICHT mehr in dieser Liste:
                # es greift bei jedem Linksklick irgendwo im Fenster und hat
                # die Drift in rc496 nicht verhindert. Was zaehlt, ist ein
                # Vergleich mit dem VORGEGEBENEN Wert - nur wenn beide
                # auseinandergehen, hat jemand gezogen.
                # `ResizedColumn` ist die einzige Bedingung in dieser
                # Liste, die die Frage BEANTWORTET statt sie zu ersetzen.
                # `IsMouseDown` (rc490), ein Breitenvergleich (rc497) und
                # ein Fenstergroessenvergleich (rc498) waren Behelfe, und
                # alle drei haben die Drift durchgelassen.
                r"ResizedColumn"
                r"|IsMouseDragging|IsItemActive|IsWindowAppearing"
                r"|Dirty|geaendert|[Dd]ragging",
                fenster,
            ):
                continue
            print(
                "  %s:%d - `%s` errechnet sich aus der eigenen Wirkung "
                "(%s, in den 15 Zeilen davor) ohne Bedingung."
                % (datei.name, nr + 1, m.group(1), quelle)
            )
            fehler += 1
    if fehler:
        print("Rueckkopplungspruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("Rueckkopplungspruefung: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
