# -*- coding: utf-8 -*-
"""
Erzeugt data/jk2 - den Befehlssatz fuer Jedi Outcast (JK2) - aus den
Kopfdateien des JK2-Spielcodes in OpenJK (codeJK2/game).

Warum: data/base ist der Befehlssatz von Jedi Academy (Ravens BehavEd-Release,
Byte fuer Byte). Die Kopfdateien darin geben die Auswahllisten vor
(Animationen, Missionsziele, SET_-Felder, Verhalten ...). JK2 hat eigene:
anims.h, objectives.h, bstate.h, Q3_Interface.h und weitere weichen ab
(z. B. BS_FLEE an anderer Stelle, 62 Missionsziele mehr, CHAN_MENU1 statt
CHAN_MUSIC). Mit den JKA-Listen meldet die Pruefung bei den 1356 JK2-Skripten
772 Anmerkungen, mit diesen Listen bleiben die echten Fehler in Ravens
Skripten uebrig.

Was hineinkommt:
  - codeJK2/game/{Q3_Interface,anims,bstate,channels,dmstates,events,g_items,
    objectives,teams,weapons}.h aus OpenJK, Stand KOMMIT (unten), unveraendert
    bis auf EINE Stelle: g_items.h schreibt "enum //# item_e" - der Leser der
    .bhc (wie Ravens BehavEd) sucht "typedef enum //# <name>", also wird daraus
    "typedef enum //# item_e" (dieselbe Schreibweise wie data/base/g_items.h).
  - hitlocs.h, behaved.bhc, supplement.bhc unveraendert aus data/base: der
    Befehlssatz von ICARUS ist in beiden Spielen derselbe (gleiche ID_/TYPE_-
    Nummern, codeJK2/icarus/interpreter.h:36-124 = code/icarus/
    IcarusImplementation.h:150-179), und codeJK2 hat kein hitlocs.h.

Zeilenenden wie data/base: die Kopfdateien CRLF (.gitattributes "-text"),
die .bhc wie in data/base.

Aufruf:
  python tools/gen_jk2daten.py                  # holt die Dateien von GitHub (KOMMIT)
  python tools/gen_jk2daten.py --openjk <ordner>  # aus einem OpenJK-Checkout
  python tools/gen_jk2daten.py --pruefen [...]  # nur vergleichen, nichts schreiben
"""
import argparse
import os
import sys
import urllib.request

KOMMIT = "260c59c2907187af555a676fe0cc798893bf7757"   # OpenJK master, 2026-09-29
QUELLE = "https://raw.githubusercontent.com/JACoders/OpenJK/%s/codeJK2/game/%s"
AUS_CODEJK2 = ["Q3_Interface.h", "anims.h", "bstate.h", "channels.h", "dmstates.h", "events.h",
               "g_items.h", "objectives.h", "teams.h", "weapons.h"]
AUS_BASE = ["hitlocs.h", "behaved.bhc", "supplement.bhc"]

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(WURZEL, "data", "base")
ZIEL = os.path.join(WURZEL, "data", "jk2")


def crlf(daten):
    return daten.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")


def hole(name, openjk):
    if openjk:
        with open(os.path.join(openjk, "codeJK2", "game", name), "rb") as f:
            return f.read()
    with urllib.request.urlopen(QUELLE % (KOMMIT, name), timeout=60) as r:
        return r.read()


def anpassen(name, daten):
    if name == "g_items.h":
        alt = b"\nenum //# item_e"
        if daten.count(alt) != 1:
            sys.exit("g_items.h: 'enum //# item_e' nicht genau einmal gefunden - Quelle geaendert?")
        daten = daten.replace(alt, b"\ntypedef enum //# item_e")
    return daten


def erzeugen(openjk):
    dateien = {}
    for name in AUS_CODEJK2:
        dateien[name] = crlf(anpassen(name, hole(name, openjk)))
    for name in AUS_BASE:
        with open(os.path.join(BASE, name), "rb") as f:
            dateien[name] = f.read()
    return dateien


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--openjk", help="OpenJK-Checkout statt GitHub (Stand %s)" % KOMMIT)
    ap.add_argument("--pruefen", action="store_true", help="nur vergleichen")
    a = ap.parse_args()
    dateien = erzeugen(a.openjk)
    anders = []
    for name, daten in sorted(dateien.items()):
        pfad = os.path.join(ZIEL, name)
        alt = open(pfad, "rb").read() if os.path.exists(pfad) else None
        if alt != daten:
            anders.append(name)
            if not a.pruefen:
                os.makedirs(ZIEL, exist_ok=True)
                with open(pfad, "wb") as f:
                    f.write(daten)
    ueberzaehlig = sorted(set(os.listdir(ZIEL)) - set(dateien)) if os.path.isdir(ZIEL) else []
    if a.pruefen:
        if anders or ueberzaehlig:
            print("data/jk2 weicht ab: %s %s" % (", ".join(anders), " ".join("+" + u for u in ueberzaehlig)))
            return 1
        print("data/jk2 aktuell (%d Dateien, OpenJK %s)" % (len(dateien), KOMMIT[:10]))
        return 0
    print("data/jk2: %d Dateien, %d neu geschrieben (OpenJK %s)" % (len(dateien), len(anders), KOMMIT[:10]))
    if ueberzaehlig:
        print("Hinweis: nicht erzeugt, bitte pruefen: " + " ".join(ueberzaehlig))
    return 0


if __name__ == "__main__":
    sys.exit(main())
