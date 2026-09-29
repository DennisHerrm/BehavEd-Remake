#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Wacht darueber, dass die erzeugten i18n-Dateien zu ihrem Erzeuger passen.

Warum es das gibt
-----------------
`include/bhed/i18n.h` und `src/i18n.cpp` tragen im Kopf die Zeile

    Erzeugt von tools/gen_i18n.py - nicht von Hand aendern.

Trotzdem standen in rc536 **vierundzwanzig** Eintraege nur dort und nicht
in `tools/gen_i18n.py`: MacroRowHint, MenuDebug, DebugLayout, MapGlow,
FxHalfRes, ModelSurfaceDump, VmsgV001/005/008/009/011 und die uebrigen
Debug-Texte. Sie waren von Hand nachgetragen worden.

Solange niemand den Erzeuger laufen liess, fiel das nicht auf. Beim
naechsten Lauf waren sie weg, und der Bau unter MSVC brach mit
vierundzwanzig

    error C2065: "MacroRowHint": nichtdeklarierter Bezeichner

ab. Auf der Linux-Seite faellt es NICHT auf: `gui/app.cpp` braucht ImGui
und wird dort gar nicht uebersetzt - die 28 Pruefer und alle Proben waren
gruen, waehrend das Programm unter Windows nicht mehr baute.

Genau diese Luecke schliesst dieser Pruefer. Er laeuft unter Linux und
findet, was sonst erst shank findet.

Zwei Pruefungen
---------------
1. **Erzeugtes passt zum Erzeuger.** `gen_i18n.py` wird in einem
   Wegwerf-Verzeichnis laufen gelassen und das Ergebnis Byte fuer Byte
   mit dem verglichen, was im Baum liegt. Weicht es ab, hat jemand von
   Hand geaendert - oder vergessen, den Erzeuger laufen zu lassen.

2. **Jedes benutzte `Str::X` gibt es auch.** Faengt den umgekehrten Fall:
   jemand schreibt `tr(Str::NeuerText)`, ohne den Eintrag anzulegen.

Gegentest
---------
`--gegentest` nimmt einen Eintrag aus einer Kopie des Erzeugers heraus und
prueft, dass Pruefung 1 anschlaegt. Eine Regel ohne Nachweis ist notierte
Schuld.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ERZEUGT = ("include/bhed/i18n.h", "src/i18n.cpp")
QUELLORTE = ("gui", "src", "tests")

BENUTZT = re.compile(r"Str::([A-Za-z0-9_]+)")
EINTRAG = re.compile(r"^\s+([A-Za-z0-9_]+),\s*$", re.M)


def erzeuge_in(wurzel):
    """gen_i18n.py in `wurzel` laufen lassen. Gibt (ok, meldung) zurueck."""
    lauf = subprocess.run([sys.executable, "tools/gen_i18n.py"],
                          cwd=wurzel, capture_output=True, text=True)
    if lauf.returncode != 0:
        return False, "gen_i18n.py scheiterte: " + (lauf.stderr or "")[:300]
    return True, ""


def wegwerfbaum(gen_pfad=None):
    """Kopie des Baums, in der erzeugt werden darf. Gibt den Pfad zurueck."""
    ziel = tempfile.mkdtemp(prefix="i18n-pruefung-")
    os.makedirs(os.path.join(ziel, "tools"))
    os.makedirs(os.path.join(ziel, "include", "bhed"))
    os.makedirs(os.path.join(ziel, "src"))
    shutil.copy(gen_pfad or os.path.join(WURZEL, "tools", "gen_i18n.py"),
                os.path.join(ziel, "tools", "gen_i18n.py"))
    return ziel


def pruefe_erzeugt():
    """Pruefung 1: stimmt das Erzeugte mit dem Erzeuger ueberein?"""
    ziel = wegwerfbaum()
    try:
        ok, meldung = erzeuge_in(ziel)
        if not ok:
            return [meldung]
        fehler = []
        for rel in ERZEUGT:
            neu = os.path.join(ziel, rel)
            alt = os.path.join(WURZEL, rel)
            if not os.path.exists(neu):
                fehler.append("%s wurde nicht erzeugt" % rel)
                continue
            a = open(alt, "rb").read()
            b = open(neu, "rb").read()
            if a != b:
                # Sagen, WAS abweicht - nicht nur dass.
                anames = set(EINTRAG.findall(a.decode("utf-8", "replace")))
                bnames = set(EINTRAG.findall(b.decode("utf-8", "replace")))
                nur_datei = sorted(anames - bnames)
                nur_gen = sorted(bnames - anames)
                text = ("%s weicht vom Erzeuger ab (%d vs %d Bytes)"
                        % (rel, len(a), len(b)))
                if nur_datei:
                    text += ("\n      nur in der Datei, NICHT im Erzeuger: "
                             + ", ".join(nur_datei[:12]))
                    text += ("\n      -> beim naechsten gen_i18n.py-Lauf "
                             "sind sie weg, und MSVC bricht ab")
                if nur_gen:
                    text += ("\n      nur im Erzeuger: "
                             + ", ".join(nur_gen[:12])
                             + "\n      -> gen_i18n.py laufen lassen")
                if not nur_datei and not nur_gen:
                    text += "\n      gleiche Namen, anderer Text"
                fehler.append(text)
        return fehler
    finally:
        shutil.rmtree(ziel, ignore_errors=True)


def pruefe_benutzt():
    """Pruefung 2: gibt es jedes benutzte Str::X?"""
    kopf = open(os.path.join(WURZEL, "include", "bhed", "i18n.h"),
                encoding="utf-8").read()
    block = kopf[kopf.index("enum class Str {"):]
    block = block[:block.index("};")]
    vorhanden = set(EINTRAG.findall(block)) | {"Count"}

    benutzt = {}
    for ort in QUELLORTE:
        pfad = os.path.join(WURZEL, ort)
        for tief, _, namen in os.walk(pfad):
            if "third_party" in tief:
                continue
            for n in sorted(namen):
                if not n.endswith((".cpp", ".h", ".hpp")):
                    continue
                voll = os.path.join(tief, n)
                text = open(voll, encoding="utf-8", errors="replace").read()
                for nr, zeile in enumerate(text.splitlines(), 1):
                    for name in BENUTZT.findall(zeile):
                        benutzt.setdefault(
                            name, "%s:%d" % (os.path.relpath(voll, WURZEL), nr))
    fehlt = sorted(n for n in benutzt if n not in vorhanden)
    return ["Str::%s benutzt in %s, steht aber nicht in i18n.h"
            % (n, benutzt[n]) for n in fehlt]


def gegentest():
    """Einen Eintrag aus einer KOPIE des Erzeugers nehmen - muss auffallen."""
    quelle = os.path.join(WURZEL, "tools", "gen_i18n.py")
    text = open(quelle, encoding="utf-8").read()
    # den ersten Tabelleneintrag entfernen
    m = re.search(r'^    \("([A-Za-z0-9_]+)",.*?\),\n', text, re.M | re.S)
    if m is None:
        print("  FEHL  kein Tabelleneintrag zum Entfernen gefunden")
        return 1
    entfernt = m.group(1)
    verstuemmelt = text[:m.start()] + text[m.end():]

    ziel = wegwerfbaum()
    try:
        with open(os.path.join(ziel, "tools", "gen_i18n.py"), "w",
                  encoding="utf-8") as f:
            f.write(verstuemmelt)
        ok, meldung = erzeuge_in(ziel)
        if not ok:
            # Auch das ist ein Anschlagen - der Erzeuger selbst merkt es.
            print("  ok    Eintrag %s entfernt -> gen_i18n.py scheitert"
                  % entfernt)
            return 0
        neu = open(os.path.join(ziel, "include", "bhed", "i18n.h"),
                   "rb").read()
        alt = open(os.path.join(WURZEL, "include", "bhed", "i18n.h"),
                   "rb").read()
        if neu == alt:
            print("  FEHL  Eintrag %s entfernt, Ergebnis trotzdem gleich"
                  % entfernt)
            return 1
        print("  ok    Eintrag %s entfernt -> Abweichung wird erkannt"
              % entfernt)
        return 0
    finally:
        shutil.rmtree(ziel, ignore_errors=True)


def main():
    if "--gegentest" in sys.argv:
        return gegentest()
    fehler = pruefe_erzeugt() + pruefe_benutzt()
    if fehler:
        print("i18n-Erzeugungspruefung: %d Beanstandung(en)" % len(fehler))
        for f in fehler:
            print("  " + f)
        return 1
    print("i18n-Erzeugungspruefung: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
