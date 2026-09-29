#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Prueft tools/bildvergleich.py an bekannten Faellen.

Warum ein eigener Pruefer
-------------------------
`bildvergleich.py` ist das Werkzeug, mit dem der GPU-Umbau abgenommen werden
soll (siehe UMBAU-GPU.md). Ein Vergleichswerkzeug, das selbst nicht geprueft
ist, ist schlimmer als keines: es sagt "innerhalb der Toleranz" und niemand
sieht nach.

Besonders wichtig ist der letzte Fall. Die Punktetoleranz ist die Stufe, die
einen echten Fehler verstecken kann - deshalb muss belegt sein, dass sie
GENAU an der Grenze umschlaegt und nicht einen daneben.
"""

import pathlib
import subprocess
import sys
import tempfile

HIER = pathlib.Path(__file__).resolve().parent
WERKZEUG = HIER / "bildvergleich.py"


def main():
    b, h = 8, 8
    gleich = bytes([10, 20, 30, 255] * (b * h))

    weit = bytearray(gleich)
    weit[0] = 200                      # ein Bildpunkt weit daneben

    knapp = bytearray(gleich)
    knapp[4] = 12                      # +2 im Rotkanal, ein Bildpunkt

    zwei = bytearray(gleich)
    zwei[0] = 200
    zwei[4] = 200                      # zwei Bildpunkte weit daneben

    fehler = 0
    with tempfile.TemporaryDirectory() as d:
        p = pathlib.Path(d)
        (p / "a.raw").write_bytes(gleich)
        (p / "weit.raw").write_bytes(bytes(weit))
        (p / "knapp.raw").write_bytes(bytes(knapp))
        (p / "zwei.raw").write_bytes(bytes(zwei))

        def lauf(zweit, *extra):
            r = subprocess.run(
                [sys.executable, str(WERKZEUG), str(p / "a.raw"),
                 str(p / zweit), str(b), str(h), *extra],
                capture_output=True, text=True)
            return r.returncode

        faelle = [
            ("gleiche Bilder bestehen", ("a.raw",), 0),
            ("ein Ausreisser faellt durch", ("weit.raw",), 1),
            ("mit --punkte 1 besteht er", ("weit.raw", "--punkte", "1"), 0),
            ("kleine Abweichung faellt durch", ("knapp.raw",), 1),
            ("mit --kanal 2 besteht sie", ("knapp.raw", "--kanal", "2"), 0),
            # Die Grenze, genau und einen daneben.
            ("zwei Ausreisser, --punkte 1: durchgefallen",
             ("zwei.raw", "--punkte", "1"), 1),
            ("zwei Ausreisser, --punkte 2: bestanden",
             ("zwei.raw", "--punkte", "2"), 0),
        ]
        for name, args, erwartet in faelle:
            hat = lauf(*args)
            if hat != erwartet:
                print("  FEHL %s (erwartet %d, war %d)"
                      % (name, erwartet, hat))
                fehler += 1

    if fehler:
        print("Bildvergleich: %d Beanstandung(en)" % fehler)
        return 1
    print("Bildvergleich: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
