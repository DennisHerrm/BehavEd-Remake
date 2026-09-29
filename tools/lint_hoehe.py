#!/usr/bin/env python3
"""Wird die Fensterhoehe gerechnet, wo ImGui sie kennt?

Warum das ein eigener Pruefer ist:

Die Hoehe der drei Spalten wurde aus fuenf Summanden zusammengesetzt:

    l.topH = avail.y - l.statusH - l.toolH - pad - tabH;

Jeder davon war eine Gelegenheit, sich zu vertun, und zwei waren falsch:
`statusH` zog den Rahmen einer Statuszeile ab, die keinen mehr hat, und
`pad` war der WAAGERECHTE Abstand, senkrecht abgezogen. Drei Runden
(rc469, rc470, rc471) gingen dafuer drauf, und shank sah dreimal dieselbe
Luecke.

ImGui kennt die verbleibende Hoehe genau. Eine NEGATIVE Hoehe in
`BeginChild` heisst "alles Uebrige, minus diesem Betrag" - so baut ImGui
seine eigene Konsole (imgui_demo.cpp:9174). Dann muss niemand rechnen.

Geprueft wird: `Layout::frameH` wird nirgends auf einen positiven Wert
geklemmt. Genau das hat die Redewendung frueher aufgehoben - `std::max(...,
0.0F)` macht aus "alles Uebrige minus X" ein "null".
"""
import re
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent
APP = HIER.parent / "gui" / "app.cpp"


def main() -> int:
    if not APP.exists():
        print("Hoehenpruefung: app.cpp fehlt")
        return 1
    t = APP.read_text(encoding="utf-8")
    t = re.sub(r"//[^\n]*", "", t)

    fehler = 0
    for m in re.finditer(r"std::max\(\s*l\.frameH[^)]*\)", t):
        print("  %s - klemmt frameH auf null. Der Wert ist NEGATIV gemeint: "
              '"alles Uebrige minus diesem Betrag".' % m.group(0))
        fehler += 1
    for m in re.finditer(r"l\.frameH\s*=\s*([^;]+);", t):
        rhs = m.group(1)
        if "-" not in rhs and "fuss" not in rhs.lower():
            print("  frameH = %s - sollte negativ sein (alles Uebrige minus "
                  "Fuss und Kopfzeile)." % rhs.strip())
            fehler += 1

    if fehler != 0:
        print("Hoehenpruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("Hoehenpruefung: ok (frameH bleibt negativ)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
