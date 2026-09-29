#!/usr/bin/env python3
"""Eine Funktion, die mehrere Reihen groessenaendert, muss NUR WACHSEN.

Warum das ein eigener Pruefer ist:

`helferGross(std::size_t n)` setzte sechs Reihen auf `n`. Aufgerufen wurde
sie mit `i + 1` - fuer die erste Spalte also mit 1. Ein `resize(1)` auf
eine Reihe mit drei Eintraegen wirft die hinteren beiden WEG, in jedem
Bild.

Was man in Spalte 2 oder 3 auswaehlte, war beim naechsten Bild wieder die
Vorgabe. Die Klappliste ging auf, man konnte sie bedienen, und nichts blieb
stehen. Der Uebersetzer sagt dazu nichts.

Vier Runden habe ich stattdessen an der Ausrichtung geschraubt.

Geprueft wird: jede Funktion, deren Rumpf zwei oder mehr `.resize(<param>`
auf denselben Parameter enthaelt, muss davor eine Schranke haben, die bei
zu kleinem Wert abbricht.
"""
import re
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent


def main() -> int:
    fehler = 0
    for datei in sorted(list((HIER.parent / "gui").glob("*.cpp")) +
                        list((HIER.parent / "src").glob("*.cpp"))):
        t = re.sub(r"//[^\n]*", "", datei.read_text(encoding="utf-8"))
        for m in re.finditer(
            r"\w[\w:<>, ]*\s+(\w+)\(\s*[\w:]+\s+(\w+)\s*\)\s*\{", t
        ):
            name, par = m.group(1), m.group(2)
            # Rumpf grob bis zur passenden schliessenden Klammer
            i, tiefe = m.end() - 1, 0
            while i < len(t):
                if t[i] == "{":
                    tiefe += 1
                elif t[i] == "}":
                    tiefe -= 1
                    if tiefe == 0:
                        break
                i += 1
            rumpf = t[m.end() : i]
            treffer = re.findall(r"\.resize\(\s*%s\b" % re.escape(par), rumpf)
            if len(treffer) < 2:
                continue
            if re.search(
                r"if\s*\(\s*%s\s*<=\s*[^)]*\.size\(\)\s*\)\s*\{?\s*return"
                % re.escape(par),
                rumpf,
            ):
                continue
            zeile = t[: m.start()].count("\n") + 1
            print(
                "  %s:%d - %s() aendert %d Reihen auf `%s`, ohne Schranke. "
                "`resize` SCHRUMPFT auch: ein kleineres `%s` wirft die "
                "hinteren Eintraege weg."
                % (datei.name, zeile, name, len(treffer), par, par)
            )
            fehler += 1
    if fehler:
        print("Resize-Pruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("Resize-Pruefung: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
