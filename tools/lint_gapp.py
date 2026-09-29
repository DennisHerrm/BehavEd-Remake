#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Prueft, dass jedes benutzte `g_app->NAME` in `struct App` steht.

Warum es das gibt
-----------------
`gui/app.cpp` braucht ImGui und wird auf der Linux-Seite ueberhaupt nicht
uebersetzt. Wer hier blind daran arbeitet, bekommt vom Uebersetzer keine
Rueckmeldung - der Fehler taucht erst bei shank unter MSVC auf, eine
Runde spaeter.

Genau so ist rc536 kaputtgegangen: vierundzwanzig fehlende `Str::`-Werte,
alle Proben gruen, alle Pruefer gruen, und der Bau brach trotzdem ab.
`tools/lint_i18n_erzeugt.py` deckt seither die Uebersetzungstexte ab.

Dieser hier deckt den zweiten haeufigen Fall ab: ein Feldname im
Programmzustand, den es so nicht gibt - vertippt, umbenannt oder nie
angelegt. Das ist der wahrscheinlichste Fehler, den man beim blinden
Bearbeiten von `app.cpp` einbaut.

Was geprueft wird
-----------------
Jedes `g_app->NAME` in gui/ muss als Feld oder Verfahren in `struct App`
in `gui/app_internal.h` stehen. Geerbtes gibt es dort nicht, also reicht
die eine Struktur.

Gegentest
---------
`--gegentest` prueft gegen einen erfundenen Namen, dass der Pruefer
anschlaegt. Eine Regel ohne Nachweis ist notierte Schuld.
"""

import os
import re
import sys

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KOPF = os.path.join(WURZEL, "gui", "app_internal.h")

BENUTZT = re.compile(r"g_app->([A-Za-z_]\w*)")
# Feld oder Verfahren am Zeilenanfang der Struktur:
#   std::vector<Row> rows;
#   bool dragging = false;
#   void tuWas();
# Ein Feld oder Verfahren in der Struktur. Die Formen, die wirklich
# vorkommen - der erste Anlauf kannte nur die Haelfte davon und meldete
# sechsundzwanzig Felder als fehlend, die es sehr wohl gab:
#
#   Document doc{Script{}};          Klammer-Anfangswert
#   float gizmoAngles[3]{};          Feld MIT Klammer-Anfangswert
#   float gizmoAxisDir[3][3]{};      und zweidimensional
#   char pk3Filter[64] = {};         Feld mit Gleichheitszeichen
#   std::function<void()> askSaveThen;   Vorlage im Typ
#   bool dragging = false;
#   void tuWas();
#
# Deshalb: Name, dann beliebig viele [..], dann eines von = { ; (
FELD = re.compile(r"\b(\w+)\s*(?:\[[^\]]*\]\s*)*(?:=|\{|;|\()")


def struktur_felder():
    text = open(KOPF, encoding="utf-8").read()
    anfang = text.index("struct App {")
    # bis zur schliessenden Klammer der Struktur, ueber die Tiefe gezaehlt
    tiefe, i = 0, text.index("{", anfang)
    ende = i
    for j in range(i, len(text)):
        if text[j] == "{":
            tiefe += 1
        elif text[j] == "}":
            tiefe -= 1
            if tiefe == 0:
                ende = j
                break
    block = text[i:ende]
    namen = set(FELD.findall(block))
    # Verschachtelte Strukturen bringen eigene Felder mit; deren Namen
    # gehoeren nicht hierher, aber die INSTANZEN schon - die faengt der
    # Ausdruck oben mit ab.
    return namen, block


def dateien():
    for tief, _, namen in os.walk(os.path.join(WURZEL, "gui")):
        if "third_party" in tief:
            continue
        for n in sorted(namen):
            if n.endswith((".cpp", ".h", ".hpp")):
                yield os.path.join(tief, n)


def pruefe(zusatz=None):
    felder, _ = struktur_felder()
    treffer = {}
    for pfad in dateien():
        text = open(pfad, encoding="utf-8", errors="replace").read()
        for nr, zeile in enumerate(text.splitlines(), 1):
            if zeile.lstrip().startswith("//"):
                continue
            for name in BENUTZT.findall(zeile):
                treffer.setdefault(
                    name, "%s:%d" % (os.path.relpath(pfad, WURZEL), nr))
    if zusatz:
        treffer[zusatz] = "<gegentest>"
    fehlt = sorted(n for n in treffer if n not in felder)
    return fehlt, treffer, len(felder)


def main():
    if "--gegentest" in sys.argv:
        fehlt, treffer, _ = pruefe(zusatz="gibtEsNichtImProgrammzustand")
        if "gibtEsNichtImProgrammzustand" in fehlt:
            print("  ok    erfundener Feldname wird erkannt")
            return 0
        print("  FEHL  erfundener Feldname faellt NICHT auf")
        return 1

    fehlt, treffer, anzahl = pruefe()
    if fehlt:
        print("g_app-Pruefung: %d Feld(er) benutzt, die es nicht gibt"
              % len(fehlt))
        for n in fehlt:
            print("  g_app->%s in %s" % (n, treffer[n]))
        return 1
    print("g_app-Pruefung: ok (%d benutzte Felder, %d in struct App)"
          % (len(treffer), anzahl))
    return 0


if __name__ == "__main__":
    sys.exit(main())
