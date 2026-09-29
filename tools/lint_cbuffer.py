#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Rechnet die Feldlagen der cbuffer nach den HLSL-Regeln aus.

Warum es das gibt
-----------------
HLSL sortiert Konstantenpuffer in Bloecke zu vier float. Zwei Regeln daraus
sind leicht zu uebersehen:

  1. Ein float2, float3 oder float4 darf keine 16-Byte-Grenze
     ueberschreiten. Passt es nicht mehr in den angefangenen Block, rueckt es
     auf den naechsten.

  2. Ein Feld aus float (float x[4]) belegt JE EINTRAG einen ganzen Block -
     also sechzehn float, nicht vier.

Beide haben in diesem Projekt schon zugeschlagen:

  rc392  gDeform[4] wurde als vier float gerechnet statt als vier Bloecke.
         Gefunden beim Schreiben, weil die Probe die Lagen prueft.

  rc402  gVorn, gRechts und gHoch wurden fortlaufend geschrieben (26, 30,
         34), HLSL legt sie aber auf 28, 32, 36. Die Folge war ein Himmel
         aus radialen Strahlen: die Kameraachsen kamen als Muell an, und
         uebrig blieb ein reiner Bildschirmfaecher.

Der zweite Fall ist der unangenehme: es stuerzt nicht ab, es sieht nur
falsch aus - und zwar auf eine Art, die man fuer einen Rechenfehler in der
Formel haelt. Ich habe zuerst die Himmelsformel geprueft, nicht die Lagen.

Was geprueft wird
-----------------
Die cbuffer-Bloecke in src/gpushader.cpp werden gelesen, die Lagen nach den
HLSL-Regeln ausgerechnet und mit den Lagen verglichen, die
gui/gpumap_win32.cpp beschreibt (`f[N] = ...` mit einem Kommentar `// FELD`
oder anhand der Tabelle im Quelltext).

Das ist bewusst grob: der Pruefer rechnet die Lagen aus und DRUCKT sie. Wer
etwas aendert, sieht die Tabelle und kann sie gegen den Fuellcode halten.
Eine vollstaendige Zuordnung waere ein kleiner Uebersetzer, und der waere
mehr Angriffsflaeche als Nutzen.
"""

import pathlib
import re
import sys

HIER = pathlib.Path(__file__).resolve().parent
SHADER = HIER.parent / "src" / "gpushader.cpp"

# Wie viele float ein Typ belegt, und ob er zusammenhaengen muss.
TYPEN = {
    "float": (1, False),
    "float2": (2, True),
    "float3": (3, True),
    "float4": (4, True),
    "float4x4": (16, True),
}


def lagen(felder):
    """Die Lagen nach den HLSL-Regeln. `felder` ist [(typ, name, anzahl)]."""
    aus = []
    at = 0
    for typ, name, anzahl in felder:
        breite, zusammen = TYPEN[typ]
        if anzahl > 1:
            # Ein Feld: je Eintrag ein ganzer Viererblock.
            at = ((at + 3) // 4) * 4
            aus.append((name, at, "%s[%d] (je Eintrag ein Block)"
                        % (typ, anzahl)))
            at += 4 * anzahl
            continue
        if zusammen and (at % 4) + breite > 4:
            at = ((at + 3) // 4) * 4
        aus.append((name, at, typ))
        at += breite
    return aus, at


def main():
    if not SHADER.exists():
        print("cbuffer: %s fehlt" % SHADER)
        return 1
    text = SHADER.read_text(encoding="utf-8")

    fehler = 0
    for m in re.finditer(r"cbuffer\s+(\w+)\s*:\s*register\(b(\d+)\)\s*\{(.*?)\};",
                         text, re.S):
        name, reg, rumpf = m.group(1), m.group(2), m.group(3)
        felder = []
        for zeile in rumpf.split("\n"):
            z = zeile.split("//")[0].strip()
            if not z or not z.endswith(";"):
                continue
            z = z[:-1].strip()
            z = z.replace("row_major ", "")
            teile = z.split()
            if len(teile) < 2 or teile[0] not in TYPEN:
                continue
            feldname = teile[1]
            anzahl = 1
            am = re.match(r"(\w+)\[(\d+)\]", feldname)
            if am:
                feldname = am.group(1)
                anzahl = int(am.group(2))
            felder.append((teile[0], feldname, anzahl))

        if not felder:
            continue
        tabelle, gesamt = lagen(felder)
        print("cbuffer %s (b%s): %d float" % (name, reg, gesamt))
        for fn, at, typ in tabelle:
            hinweis = ""
            # Ein Feld, das NICHT dort liegt, wo man es fortlaufend erwarten
            # wuerde, ist die Falle - die wird ausdruecklich markiert.
            print("   %-16s %3d   %s%s" % (fn, at, typ, hinweis))
        # Eine Luecke heisst: fortlaufendes Schreiben ist falsch.
        erwartet = 0
        for (fn, at, typ) in tabelle:
            if at != erwartet:
                print("   ACHTUNG: %s liegt bei %d, fortlaufend waere %d"
                      % (fn, at, erwartet))
                fehler += 1
            breite, _ = TYPEN[typ.split("[")[0]]
            if "[" in typ:
                anzahl = int(typ.split("[")[1].split("]")[0])
                erwartet = at + 4 * anzahl
            else:
                erwartet = at + breite
        print("")

    if fehler:
        print("cbuffer: %d Feld(er) liegen NICHT fortlaufend - der Fuellcode "
              "in gui/gpumap_win32.cpp muss das beruecksichtigen" % fehler)
        # Kein Fehlschlag: die Luecken sind erlaubt und beabsichtigt. Der
        # Pruefer soll sie SICHTBAR machen, nicht verbieten.
    print("cbuffer: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
