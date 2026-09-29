#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Vergleicht zwei Bilder mit gestufter Toleranz.

Warum es das gibt
-----------------
Alles, was in diesem Projekt an Zeichnerarbeit abgesichert ist, haengt an
einem Satz: "0 Bytes anders". Er hat in rc370 den Faktor 29 beim Spurtest
abgenommen, in rc371 zwei Beschleunigungen, und in rc369 hat er eine Keulung
verhindert, die sichtbare Effekte wegnahm.

Auf einer Grafikkarte gilt dieser Satz nicht mehr. Aras Pranckevicius, der
bei Unity das Grafik-Testen aufgebaut hat, schreibt, man muesse auf vielen
Plattformen einige falsche Bildpunkte zulassen, weil viele
Endkunden-Grafikkarten nicht vollstaendig deterministisch sind. Chromium
betreibt fuer dasselbe Problem einen eigenen Dienst (Gold) mit mehreren
zugelassenen Bildern je Test.

Bevor also eine Zeile Direct3D geschrieben wird, braucht es einen Vergleich,
der "gleich genug" beantworten kann - sonst merkt man Fehler erst am
Bildschirm, und das ist genau der Zustand, aus dem uns die Bytegleichheit
herausgeholt hat.

Die drei Stufen
---------------
Ein einzelner Schwellwert taugt nicht: er ist entweder so streng, dass
Rundungsunterschiede ihn ausloesen, oder so lasch, dass ein fehlender
Brustpanzer durchgeht. Bewaehrt hat sich eine Staffelung:

  1. je KANAL    - um wie viel darf ein einzelner Farbwert abweichen?
  2. je BILDPUNKT- ein Bildpunkt gilt als abweichend, wenn EIN Kanal die
                   Kanaltoleranz reisst
  3. je BILD     - wie viele abweichende Bildpunkte sind erlaubt?

Die dritte Stufe ist die wichtige und die gefaehrliche. Sie faengt das
Flimmern an Kanten ab, das jede Grafikkarte anders macht - und sie kann
einen echten Fehler verstecken, wenn man sie zu gross waehlt. Deshalb meldet
dieses Werkzeug IMMER auch, WO die Abweichungen liegen: ein Fehler, der sich
auf eine Stelle ballt, sieht anders aus als Rundungsrauschen ueber das ganze
Bild, und diesen Unterschied kann man sehen, statt ihn zu erraten.

Vorgabe: 0/0/0 - also bytegleich. So bleibt es fuer den Rasterer auf dem
Hauptprozessor bei der bisherigen Strenge, und die Toleranz wird nur dort
eingeschaltet, wo sie noetig ist.

Aufruf
------
    bildvergleich.py <a.raw> <b.raw> <breite> <hoehe>
                     [--kanal N] [--punkte N] [--anteil P]

    --kanal   erlaubte Abweichung je Farbwert (0..255), Vorgabe 0
    --punkte  erlaubte Zahl abweichender Bildpunkte, Vorgabe 0
    --anteil  dasselbe als Anteil in Prozent; ueberschreibt --punkte

Die .raw-Dateien sind RGBA, vier Byte je Bildpunkt - das Format, das
refrender und der Bildschirmabzug schreiben.

Rueckgabe 0, wenn alles innerhalb der Toleranz liegt, sonst 1.
"""

import sys
from pathlib import Path


def lade(pfad, breite, hoehe):
    d = Path(pfad).read_bytes()
    noetig = breite * hoehe * 4
    if len(d) != noetig:
        raise SystemExit(
            "%s: %d Byte, erwartet %d (%dx%d RGBA)"
            % (pfad, len(d), noetig, breite, hoehe))
    return d


def main(argv):
    if len(argv) < 5:
        print(__doc__.split("Aufruf\n------\n", 1)[1].strip())
        return 2
    a_pfad, b_pfad = argv[1], argv[2]
    breite, hoehe = int(argv[3]), int(argv[4])

    kanal = 0
    punkte = 0
    anteil = None
    i = 5
    while i < len(argv):
        if argv[i] == "--kanal" and i + 1 < len(argv):
            kanal = int(argv[i + 1]); i += 2
        elif argv[i] == "--punkte" and i + 1 < len(argv):
            punkte = int(argv[i + 1]); i += 2
        elif argv[i] == "--anteil" and i + 1 < len(argv):
            anteil = float(argv[i + 1]); i += 2
        else:
            print("unbekannte Angabe: " + argv[i])
            return 2
    if anteil is not None:
        punkte = int(breite * hoehe * anteil / 100.0)

    a = lade(a_pfad, breite, hoehe)
    b = lade(b_pfad, breite, hoehe)

    # Zaehlen und gleichzeitig merken, WO es abweicht. Die Lage ist der
    # eigentliche Wert dieser Ausgabe: gebuendelte Abweichungen sind ein
    # Fehler, verstreute sind Rauschen.
    schlimm = 0
    abweichend = 0
    x0, x1, y0, y1 = breite, -1, hoehe, -1
    # Grobes Raster, damit man die Verteilung sieht, ohne ein Bild zu
    # schreiben.
    felder = [[0] * 8 for _ in range(8)]
    for y in range(hoehe):
        zeile = y * breite * 4
        for x in range(breite):
            p = zeile + x * 4
            d = 0
            for k in range(3):
                d = max(d, abs(a[p + k] - b[p + k]))
            if d > kanal:
                abweichend += 1
                schlimm = max(schlimm, d)
                if x < x0: x0 = x
                if x > x1: x1 = x
                if y < y0: y0 = y
                if y > y1: y1 = y
                felder[y * 8 // hoehe][x * 8 // breite] += 1

    gesamt = breite * hoehe
    print("Bildvergleich %dx%d" % (breite, hoehe))
    print("  Toleranz: %d je Kanal, %d Bildpunkte erlaubt (%.3f %%)"
          % (kanal, punkte, 100.0 * punkte / gesamt if gesamt else 0.0))
    print("  abweichend: %d von %d (%.3f %%), groesste Abweichung %d"
          % (abweichend, gesamt, 100.0 * abweichend / gesamt if gesamt else 0.0,
             schlimm))
    if abweichend:
        print("  Rechteck: x %d..%d  y %d..%d" % (x0, x1, y0, y1))
        print("  Verteilung (Achtel des Bildes):")
        for zeile in felder:
            print("    " + " ".join("%6d" % v for v in zeile))
    if abweichend > punkte:
        print("AUSSERHALB DER TOLERANZ")
        return 1
    print("innerhalb der Toleranz")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
