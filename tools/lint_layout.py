#!/usr/bin/env python3
"""Stimmt die Zahl der Eingabeelemente mit dem Feld ueberein?

Warum das ein eigener Pruefer ist:

`CreateInputLayout(ein, 5, ...)` stand neben einem Feld `ein[]`, das sechs
Eintraege hatte - ein abgebrochener Lauf hatte `TEXCOORD 2` fuer die
Autosprite-Mitte ergaenzt. Beim Zuruecknehmen habe ich das Feld in `Ecke`
entfernt und diesen Eintrag uebersehen.

Der Vertexshader kennt kein TEXCOORD2, `CreateInputLayout` schlug fehl,
`p.layout` blieb null - und JEDER Zeichenaufruf wurde uebersprungen. Auf
shanks Bild: "GPU: 0 Aufrufe, 938 uebersprungen", keine Karte mehr, 1 fps.

Der Uebersetzer sagt dazu nichts: die 5 ist gueltig, nur falsch.

Geprueft wird: jeder `CreateInputLayout`-Aufruf nimmt seine Anzahl aus dem
Feld (`std::size(...)`) statt aus einer danebengeschriebenen Zahl.
"""
import re
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent
QUELLE = HIER.parent / "gui" / "gpumap_win32.cpp"

AUFRUF = re.compile(r"CreateInputLayout\(\s*(\w+)\s*,\s*([^,]+),")


def main() -> int:
    if not QUELLE.exists():
        print("Layoutpruefung: %s fehlt" % QUELLE)
        return 1
    t = QUELLE.read_text(encoding="utf-8")
    t = re.sub(r"//[^\n]*", "", t)

    fehler = 0
    n = 0
    for m in AUFRUF.finditer(t):
        n += 1
        feld = m.group(1)
        anzahl = m.group(2).strip()
        if "std::size" not in anzahl:
            print('  CreateInputLayout(%s, %s, ...) - die Anzahl steht neben '
                  "dem Feld statt darin" % (feld, anzahl))
            fehler += 1
            continue
        if feld not in anzahl:
            print('  CreateInputLayout(%s, %s, ...) - std::size zaehlt ein '
                  "ANDERES Feld" % (feld, anzahl))
            fehler += 1

    if n == 0:
        print("Layoutpruefung: kein CreateInputLayout gefunden - hat sich die "
              "Datei geaendert?")
        return 1
    if fehler != 0:
        print("Layoutpruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("Layoutpruefung: ok (%d Aufrufe, Anzahl jeweils aus dem Feld)" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
