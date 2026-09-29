#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Prueft, dass jede Einstellung geschrieben UND gelesen wird.

Warum es das gibt
-----------------
Gemeldet: "jedes Mal wenn ich das Programm starte ist die Leiste rechts
wieder ausgestreckt, statt so wie ich es eingestellt habe."

Die Ursache stand im Protokoll, man musste nur beide Zeilen nebeneinander
halten:

    Anordnung geladen:   Karte 0.550     <- der VORGABEwert
    Anordnung gesichert: Karte 0.943

Jede Sitzung sicherte 0.943 und lud wieder 0.550. Der Schreiber kannte den
Schluessel `splitMap`, der Leser nicht. Der Wert wurde also brav in die Datei
geschrieben und beim naechsten Start ignoriert.

Ein solcher Fehler ist von aussen kaum zu sehen: die Datei sieht richtig aus,
das Protokoll meldet "gesichert (ok)", und nur wer beide Zahlen vergleicht,
merkt etwas. Er faellt auch keinem Bildvergleich auf, weil er nichts am Bild
aendert - nur am naechsten Start.

Deshalb ein eigener Pruefer: die beiden Listen muessen deckungsgleich sein.

Was geprueft wird
-----------------
  put(o, "name", ...)     im Schreiber
  key == "name"           im Leser

Ein Schluessel, der nur auf einer Seite vorkommt, ist eine Beanstandung -
egal auf welcher. Nur-gelesen ist genauso falsch wie nur-geschrieben: dann
wird eine Einstellung nie gespeichert.
"""

import pathlib
import re
import sys

HIER = pathlib.Path(__file__).resolve().parent
QUELLE = HIER.parent / "src" / "settings.cpp"
KOPF = HIER.parent / "include" / "bhed" / "settings.h"


def main():
    if not QUELLE.exists():
        print("Einstellungen: %s fehlt" % QUELLE)
        return 1
    s = QUELLE.read_text(encoding="utf-8")

    schreibt = set(re.findall(r'put\(o,\s*"([A-Za-z0-9_]+)"', s))
    liest = set(re.findall(r'key\s*==\s*"([A-Za-z0-9_]+)"', s))

    if not schreibt or not liest:
        print("Einstellungen: keine Schluessel gefunden - hat sich die "
              "Schreibweise geaendert?")
        return 1

    nur_geschrieben = sorted(schreibt - liest)
    nur_gelesen = sorted(liest - schreibt)

    # --- Und die dritte Frage, die bis rc429 niemand stellte --------------
    #
    # Die beiden Mengen oben pruefen, ob Geschriebenes auch gelesen wird.
    # Sie koennen NICHT sehen, ob ein Feld ueberhaupt vorkommt: `mapOnGpu`
    # stand neun Runden lang in Settings, wurde nie geschrieben, nie
    # gelesen - und dieser Pruefer meldete "ok (29 Schluessel auf beiden
    # Seiten)". Der Schalter stand nach jedem Start wieder auf aus.
    #
    # Deshalb geht der Vergleich jetzt von der STRUKTUR aus. Was in
    # settings.h steht, muss in settings.cpp vorkommen.
    felder = set()
    if KOPF.exists():
        k = KOPF.read_text(encoding="utf-8")
        anfang = k.find("struct Settings {")
        if anfang >= 0:
            # Bis zur ersten eingebetteten Struktur oder zum Ende - die
            # geschachtelten Typen (KeyBinding) haben eigene Felder, die
            # nicht einzeln in der Datei stehen.
            ende = k.find("struct KeyBinding", anfang)
            if ende < 0:
                ende = k.find("\n};", anfang)
            rumpf = k[anfang:ende if ende > 0 else len(k)]
            for zeile in rumpf.splitlines():
                zeile = zeile.split("//")[0].strip()
                m = re.match(
                    r'^(?:bool|int|float|double|std::string)\s+'
                    r'([A-Za-z_][A-Za-z0-9_]*)\s*(?:=|;)', zeile)
                if m:
                    felder.add(m.group(1))

    # Felder, die absichtlich NICHT in der Datei stehen. Jeder Eintrag
    # braucht einen Grund - eine Ausnahmeliste ohne Begruendung ist eine
    # stillgelegte Pruefung.
    ausgenommen = set()

    fehlt_ganz = sorted(felder - schreibt - liest - ausgenommen)
    for k in fehlt_ganz:
        print("  \"%s\" steht in settings.h, aber weder im Schreiben noch "
              "im Lesen (wird nie gespeichert)" % k)

    for k in nur_geschrieben:
        print("  \"%s\" wird geschrieben, aber nie gelesen "
              "(geht bei jedem Start verloren)" % k)
    for k in nur_gelesen:
        print("  \"%s\" wird gelesen, aber nie geschrieben "
              "(wird nie gespeichert)" % k)

    fehler = len(nur_geschrieben) + len(nur_gelesen) + len(fehlt_ganz)
    if fehler:
        print("Einstellungen: %d Beanstandung(en)" % fehler)
        return 1
    print("Einstellungen: ok (%d Schluessel auf beiden Seiten, "
          "%d Felder in settings.h abgedeckt)" % (len(schreibt), len(felder)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
