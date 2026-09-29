#!/usr/bin/env python3
"""Wird eine Restbreite ABGEZOGEN, wo man sie messen kann?

Warum das ein eigener Pruefer ist - dieselbe Ursache, fuenfmal:

  rc473  drawModeBar         Breite gerechnet statt gefragt
  rc474  Actions-Spalte      minButtonsW statt buttonsW
  rc477  Script Flow         mit Platzhaltern weitergerechnet
  rc485  Helferspalten       feste Zahl statt Feldbreite
  rc500  Kartenansicht       `restW = Zelle - 15 Zeichenhoehen - Abstand`

Der letzte Fall zeigt die Falle am deutlichsten: die 15 Zeichenhoehen sind
die Breite, die `drawEntityLayers` seinem KINDFENSTER gibt. Der Gruppe
davor gehoert aber auch eine Kopfzeile, und ImGui nimmt fuer eine Gruppe
das Breiteste ihrer Zeilen. Was die Gruppe wirklich einnimmt, weiss niemand
vorher.

`GetContentRegionAvail()` nach dem Block nennt die Restbreite genau.

Geprueft wird: keine Zuweisung der Form `x -= GetFontSize() * N` oder
`x = ... - GetFontSize() * N`, wo `x` danach als Breite an `BeginChild`
oder `SetNextItemWidth` geht.
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
        for nr, zeile in enumerate(zeilen):
            m = re.search(r"(\w+)\s*-=\s*ImGui::GetFontSize\(\)\s*\*", zeile)
            if not m:
                continue
            name = m.group(1)
            # Wird der Wert danach als Breite benutzt?
            spaeter = "\n".join(zeilen[nr : nr + 60])
            if not re.search(
                r"(BeginChild\([^,]*,\s*ImVec2\{\s*%s|SetNextItemWidth\(\s*%s)"
                % (re.escape(name), re.escape(name)),
                spaeter,
            ):
                continue
            print(
                "  %s:%d - `%s` zieht eine geschaetzte Breite ab und wird "
                "danach als Breite benutzt. `GetContentRegionAvail()` nach "
                "dem Block nennt sie genau." % (datei.name, nr + 1, name)
            )
            fehler += 1
    if fehler:
        print("Breitenabzug-Pruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("Breitenabzug-Pruefung: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
