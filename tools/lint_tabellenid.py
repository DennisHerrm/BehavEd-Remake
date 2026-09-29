#!/usr/bin/env python3
"""Teilen sich mehrere Anordnungen eine Tabellen-ID?

Warum das ein eigener Pruefer ist:

`TableSetupColumn(label, flags, init_width_or_weight)` - der Parameter
heisst INIT_width_or_weight und gilt beim ANLEGEN. Danach steht die Breite
im Tabellenzustand, und der haengt an der Tabellen-ID:

    imgui_tables.cpp:358   table = g.Tables.GetOrAddByKey(id);

Eine ID, ein Zustand. Alle drei Modi von behaved benutzten "##spalten" und
teilten sich deshalb die gezogene Breite - egal, was in den Einstellungen
stand. rc494 hat drei eigene Einstellungswerte eingefuehrt und den Fehler
NICHT behoben, weil er woanders sass.

Geprueft wird: ein `BeginTable` mit fester Zeichenkette als ID darf nicht
in einer Funktion stehen, die nach `leftMode` verzweigt. Wer je Modus etwas
anderes zeichnet, braucht je Modus eine eigene ID.
"""
import re
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent


def main() -> int:
    fehler = 0
    for datei in sorted((HIER.parent / "gui").glob("*.cpp")):
        t = re.sub(r"//[^\n]*", "", datei.read_text(encoding="utf-8"))
        for m in re.finditer(r'BeginTable\(\s*"([^"]+)"', t):
            # Wird in den 60 Zeilen davor nach dem Modus verzweigt?
            start = t.rfind("\n", 0, m.start())
            fenster = t[max(0, m.start() - 3000) : m.start()]
            if "leftMode" not in fenster:
                continue
            zeile = t[: m.start()].count("\n") + 1
            print(
                '  %s:%d - BeginTable("%s") mit fester ID, obwohl in der '
                "Naehe nach `leftMode` verzweigt wird. Eine ID, ein Zustand "
                "(imgui_tables.cpp:358) - die Spaltenbreiten waeren geteilt."
                % (datei.name, zeile, m.group(1))
            )
            fehler += 1
    if fehler:
        print("Tabellen-ID-Pruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("Tabellen-ID-Pruefung: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
