#!/usr/bin/env python3
"""Wird ein Abstand abgezogen, den `SameLine` gar nicht setzt?

Warum das ein eigener Pruefer ist:

Die Feldzeile im Ereignisfenster rechnete

    feldBreite = zelleW - knopfB - ItemSpacing.x

und setzte den Knopf dann mit `SameLine(0, 0)` - also OHNE Abstand. Sie
endete deshalb genau um `ItemSpacing.x` zu frueh, waehrend die Helferzeile
darunter bis zur Zellkante lief. Bei 144 dpi sind das die vierzehn Punkte,
die shank ueber sechs Runden gemeldet hat.

Abziehen ODER setzen - nicht eins von beidem.

Geprueft wird: wo eine Breite `ItemSpacing.x` abzieht, muss in den zwanzig
Zeilen danach ein `SameLine` mit einem Abstand ungleich null stehen.
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
            if not re.search(r"-\s*ImGui::GetStyle\(\)\.ItemSpacing\.x", zeile):
                continue
            fenster = "\n".join(zeilen[nr : nr + 20])
            if not re.search(r"SameLine\(", fenster):
                continue
            # Gibt es ein SameLine MIT Abstand?
            # `SameLine()` OHNE Argumente setzt den Vorgabeabstand, also
            # ItemSpacing.x - das ist korrekt und keine Beanstandung. Nur
            # `SameLine(x, 0)` setzt ausdruecklich KEINEN.
            mitAbstand = re.search(r"SameLine\(\s*\)", fenster) or re.search(
                r"SameLine\(\s*[^,)]*,\s*(?!0\s*\))[^)]+\)", fenster)
            if mitAbstand:
                continue
            print(
                "  %s:%d - zieht ItemSpacing.x ab, aber das folgende "
                "`SameLine` setzt keinen Abstand. Abziehen ODER setzen."
                % (datei.name, nr + 1)
            )
            fehler += 1
    if fehler:
        print("Abstands-Pruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("Abstands-Pruefung: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
