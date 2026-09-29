#!/usr/bin/env python3
"""Hat jeder Pruefcode aus validate.cpp eine Uebersetzung?

Warum das ein eigener Pruefer ist:

shank meldete "still get some German words in the messages window". Es waren
FUENF von zehn Codes ohne Uebersetzung: V001, V005, V008, V009, V011. Die
Oberflaeche lief auf Englisch, das Meldungsfenster antwortete auf Deutsch.

Dazu eine Verwechslung: `Str::VmsgV007` trug den Text von V009 - die Nummer
im Namen und die Nummer im Code waren auseinandergelaufen. Der Name war
richtig, verglichen wurde die falsche Nummer, also griff die Uebersetzung
nie.

Geprueft wird: jeder in validate.cpp ausgegebene Code hat in app.cpp einen
Zweig, und jeder Zweig zeigt auf eine Zeichenkette mit derselben Nummer im
Namen.
"""
import re
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent
VALID = HIER.parent / "src" / "validate.cpp"
APP = HIER.parent / "gui" / "app.cpp"


def main() -> int:
    if not VALID.exists() or not APP.exists():
        print("Pruefcodepruefung: validate.cpp oder app.cpp fehlt")
        return 1
    codes = sorted(set(re.findall(r'"(V\d{3})"',
                                  VALID.read_text(encoding="utf-8"))))
    app = APP.read_text(encoding="utf-8")
    zweige = dict(re.findall(r'i\.code == "(V\d{3})"\s*\)\s*\{\s*'
                             r'muster = tr\(Str::Vmsg(V\d{3})\)', app))

    fehler = 0
    for c in codes:
        if c not in zweige:
            print('  %s wird ausgegeben, hat aber keine Uebersetzung' % c)
            fehler += 1
        elif zweige[c] != c:
            print('  %s zeigt auf Str::Vmsg%s - die Nummern laufen '
                  "auseinander" % (c, zweige[c]))
            fehler += 1

    if not codes:
        print("Pruefcodepruefung: keine Codes gefunden - hat sich "
              "validate.cpp geaendert?")
        return 1
    if fehler != 0:
        print("Pruefcodepruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("Pruefcodepruefung: ok (%d Codes, jeder mit passender "
          "Uebersetzung)" % len(codes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
