#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Findet Aufrufe eigener Funktionen, die es gar nicht gibt.

Warum es diese Pruefung braucht
-------------------------------
`-fsyntax-only` sieht eine DEKLARATION und ist zufrieden. Ob die Funktion
irgendwo auch definiert wurde, faellt erst beim BINDEN auf.

Genau das ist passiert: `confirmDiscard()` wurde beim Umbau auf die eigene
Speichern-Frage durch `withUnsaved()` ersetzt. In `app.cpp` waren alle
Aufrufe umgestellt, in `app_view3d.cpp` einer nicht - und die Deklaration
blieb im Kopf stehen. Uebersetzt hat es sauber. Erst der Bindeschritt haette
es gemeldet, und der lief hier nicht, weil MinGW fehlt.

Wie sie arbeitet
----------------
Jede GUI-Datei wird zu einer Objektdatei uebersetzt. Dann werden alle
DEFINIERTEN Namen (nm -g, gross T/W) aus allen Dateien eingesammelt, und
jeder UNDEFINIERTE Name (nm -u) aus unserem eigenen Namensraum muss darin
vorkommen.

Fremde Namen - ImGui, die Standardbibliothek, Windows - werden nicht
geprueft: die kommen aus Bibliotheken, die hier nicht vorliegen. Es geht
allein um `bhed::`, und dort ist die Frage eindeutig beantwortbar.

Die Windows-Dateien bleiben aussen vor, weil sie sich hier nicht uebersetzen
lassen. Sie definieren nur die Fensterschicht; was sie AUFRUFEN, deckt der
Rest ab.
"""

import os
import re
import shutil
import subprocess
import sys

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMGUI = os.environ.get("IMGUI", os.path.expanduser("~/imgui"))

# Diese Dateien brauchen die Windows-Kopfdateien und lassen sich hier nicht
# uebersetzen.
NUR_WINDOWS = ("main_win32.cpp", "platform_win32.cpp",
               "backend_win32.cpp", "audio_win32.cpp")


def uebersetze(pfad, ziel):
    befehl = [
        "g++", "-std=c++20", "-O0", "-c",
        "-I" + os.path.join(WURZEL, "include"),
        "-I" + os.path.join(WURZEL, "third_party"),
        "-I" + os.path.join(WURZEL, "gui"),
        "-I" + IMGUI,
        "-I" + os.path.join(IMGUI, "backends"),
        '-DIMGUI_USER_CONFIG="imgui_config.h"',
        pfad, "-o", ziel,
    ]
    r = subprocess.run(befehl, capture_output=True, text=True, timeout=600)
    return r.returncode == 0, r.stderr


def namen(objekt, art):
    """art: 'u' fuer undefiniert, 'g' fuer definiert."""
    schalter = "-uC" if art == "u" else "-gC"
    r = subprocess.run(["nm", schalter, objekt], capture_output=True, text=True)
    heraus = set()
    for zeile in r.stdout.splitlines():
        teile = zeile.split(None, 2)
        if art == "u":
            # "                 U name"
            #
            # NICHT ueber split(None, 2) mit teile[-2]: ein entzifferter
            # Name enthaelt oft LEERZEICHEN -
            #
            #   U bhed::gui::selectByPath(std::vector<unsigned long, ...>)
            #
            # Dann steht in teile[-2] nicht "U", sondern ein Stueck des
            # Namens, und die Zeile fiel durch. Genau daran ist rc179 beim
            # Anwender nicht gebunden worden, waehrend dieser Pruefer
            # "keine fehlende Definition" meldete: er sah nur Namen OHNE
            # Leerzeichen.
            treffer = re.match(r"\s*U\s+(\S.*)$", zeile)
            if treffer:
                heraus.add(treffer.group(1))
        else:
            # "0000000000000000 T name"  - T, W und V sind Definitionen
            if len(teile) >= 3 and teile[1] in ("T", "W", "V", "B", "D"):
                heraus.add(teile[2])
    return heraus


def main():
    # Ohne die GNU-Werkzeuge geht das hier nicht.
    #
    # Diese Pruefung uebersetzt jede Datei einzeln und liest die Symbole mit
    # `nm` aus. Beides gibt es auf einem Windows-Rechner mit Visual Studio
    # nicht - dort brach rc548 mit einem Python-Rueckverfolg ab, weil
    # `subprocess.run` ungeschuetzt war.
    #
    # `nm` hat unter MSVC keine Entsprechung, die dasselbe leistet
    # (`dumpbin /symbols` schreibt ein anderes Format). Also hier nicht
    # nachbauen, sondern sauber ueberspringen: der Linux-Lauf deckt es ab.
    for werkzeug in ("g++", "nm"):
        if shutil.which(werkzeug) is None:
            print("Bindepruefung: uebersprungen - %s nicht gefunden" % werkzeug)
            print("   Sie braucht die GNU-Werkzeuge (g++ und nm) und laeuft "
                  "auf der Linux-Seite.")
            return 0

    if not os.path.isfile(os.path.join(IMGUI, "imgui.h")):
        print("ImGui nicht gefunden in %s - uebersprungen" % IMGUI)
        return 0

    guiDir = os.path.join(WURZEL, "gui")
    quellen = sorted(
        os.path.join(guiDir, n)
        for n in os.listdir(guiDir)
        if n.endswith(".cpp") and n not in NUR_WINDOWS
    )
    # Der Kern definiert vieles, was die Oberflaeche ruft.
    srcDir = os.path.join(WURZEL, "src")
    quellen += sorted(
        os.path.join(srcDir, n) for n in os.listdir(srcDir) if n.endswith(".cpp")
    )

    objekte = []
    for q in quellen:
        ziel = "/tmp/lnk_%s.o" % os.path.basename(q).replace(".cpp", "")
        ok, fehler = uebersetze(q, ziel)
        if not ok:
            print("uebersetzt nicht: %s" % os.path.basename(q))
            print(fehler.splitlines()[0] if fehler else "")
            return 1
        objekte.append(ziel)

    definiert = set()
    for o in objekte:
        definiert |= namen(o, "g")

    fehlend = {}
    for o in objekte:
        for n in namen(o, "u"):
            if not n.startswith("bhed::"):
                continue
            # Was in den ausgeschlossenen Windows-Dateien steht, kann hier
            # nicht definiert sein. Die drei Bereiche liegen genau dort:
            # der Ton (audio_win32), die Systemwege (platform_win32) und
            # die Symbolbilder (icons_gen wird zwar uebersetzt, seine
            # Felder stehen aber in der erzeugten Fassung).
            if (n.startswith("bhed::platform::") or
                    n.startswith("bhed::gui::Audio::") or
                    n.startswith("bhed::icons::") or
                    # Die Texturen der Grafikschnittstelle stehen in
                    # backend_win32.cpp - erst durch die behobene Zerlegung
                    # oben ueberhaupt sichtbar geworden, weil ihre Namen
                    # Leerzeichen enthalten.
                    n.startswith("bhed::render::")):
                continue
            if n in definiert:
                continue
            fehlend.setdefault(n, []).append(os.path.basename(o))

    for o in objekte:
        try:
            os.remove(o)
        except OSError:
            pass

    if fehlend:
        print("%d eigene Funktion(en) werden gerufen, aber nirgends definiert:"
              % len(fehlend))
        for n, wo in sorted(fehlend.items()):
            print("   %s" % n)
            print("      gerufen aus: %s" % ", ".join(sorted(set(wo))))
        print("   (das faellt sonst erst beim Binden auf)")
        return 1

    print("Bindeprobe: %d Dateien, keine fehlende Definition" % len(objekte))
    return 0


if __name__ == "__main__":
    sys.exit(main())
