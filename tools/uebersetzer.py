#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Findet einen brauchbaren Uebersetzer - oder sagt sauber, dass es keinen gibt.

Warum es das gibt
-----------------
`tools/lint_headers.py` rief `g++` ohne jede Pruefung auf. Auf shanks
Rechner gibt es kein g++, nur MSVC. Ergebnis beim Bauen von rc548:

    Traceback (most recent call last):
      File "tools/lint_headers.py", line 33, in <module>
        proc = subprocess.run(
    FileNotFoundError: [WinError 2] Das System kann die angegebene Datei
    nicht finden

Aufgefallen ist es erst jetzt, weil er gerade Python installiert hat:
vorher wurden die Pruefer uebersprungen, jetzt laufen sie. Der Fehler lag
also die ganze Zeit da und wartete.

Ein Pruefwerkzeug, das auf dem Rechner des Benutzers mit einem Rueckverfolg
abbricht, ist schlimmer als eines, das dort gar nicht laeuft: es sieht aus
wie ein Fehler im Programm.

Was hier passiert
-----------------
Gesucht wird in dieser Reihenfolge:

  g++, clang++, c++    GNU-Art:  -fsyntax-only -std=c++20 -I<pfad>
  cl                   MSVC-Art: /Zs /std:c++20 /I<pfad>

`/Zs` heisst bei MSVC "nur die Syntax pruefen": es entstehen keine
Ausgabedateien, Fehlermeldungen gehen nach standard output. Genau das, was
`-fsyntax-only` bei GCC tut.

`cl` steht nur dann im Suchpfad, wenn die Eingabeaufforderung von Visual
Studio eingerichtet ist - und nur dann stimmen auch die Pfade zur
Standardbibliothek. Findet sich `cl` nicht, ist das also die richtige
Antwort und kein Unfall.

Wird gar keiner gefunden, liefert `finde()` None. Der Aufrufer soll das
melden und die Pruefung UEBERSPRINGEN, nicht abbrechen: die Textpruefungen
(Einbindewaechter, `using namespace`) laufen ja auch ohne Uebersetzer.
"""

import shutil
import subprocess
import sys


class Uebersetzer:
    """Ein gefundener Uebersetzer samt der Art, ihn aufzurufen."""

    def __init__(self, programm, art):
        self.programm = programm
        self.art = art          # 'gnu' oder 'msvc'

    def syntaxbefehl(self, quelle, includes):
        """Befehlszeile, die NUR die Syntax prueft."""
        if self.art == "msvc":
            befehl = [self.programm, "/nologo", "/Zs", "/std:c++20", "/EHsc"]
            befehl += ["/I" + p for p in includes]
            befehl.append(str(quelle))
            return befehl
        befehl = [self.programm, "-std=c++20", "-fsyntax-only"]
        befehl += ["-I" + p for p in includes]
        befehl.append(str(quelle))
        return befehl

    def __str__(self):
        return "%s (%s)" % (self.programm, self.art)


def finde():
    """Der erste brauchbare Uebersetzer, oder None."""
    for name in ("g++", "clang++", "c++"):
        if shutil.which(name) is not None:
            return Uebersetzer(name, "gnu")
    if shutil.which("cl") is not None:
        return Uebersetzer("cl", "msvc")
    return None


def fehlt_melden(was):
    """Einheitliche Meldung, wenn kein Uebersetzer da ist."""
    print("%s: uebersprungen - kein Uebersetzer gefunden" % was)
    print("   gesucht: g++, clang++, c++, cl")
    print("   Unter Windows steht `cl` nur in der "
          "Entwickler-Eingabeaufforderung von Visual Studio im Suchpfad.")
    print("   Das ist KEIN Fehler im Programm - die Pruefung braucht einen "
          "Uebersetzer und laeuft ohne ihn nicht.")


def lauf(befehl):
    """subprocess.run, aber ohne Rueckverfolg, wenn das Programm fehlt.

    Gibt (gefunden, returncode, ausgabe) zurueck.
    """
    try:
        r = subprocess.run(befehl, capture_output=True, text=True,
                           check=False)
    except (FileNotFoundError, OSError) as e:
        return False, -1, str(e)
    # MSVC schreibt Fehler nach stdout, GCC nach stderr - beides mitnehmen.
    return True, r.returncode, (r.stderr or "") + (r.stdout or "")


if __name__ == "__main__":
    u = finde()
    if u is None:
        fehlt_melden("Uebersetzersuche")
        sys.exit(0)
    print("gefunden: %s" % u)
    sys.exit(0)
