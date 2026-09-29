#!/usr/bin/env python3
"""Wird jedes Merkmalsbit auch irgendwo angewandt?

Warum das ein eigener Pruefer ist:

`kSpecular` erzeugte eine Zeile, die nachweislich nie etwas tat
(`c.a = saturate(c.a * gRgb.a)` mit gRgb.a == 1). `kPolygonOffset` war
gesetzt, gepackt und im Shadernamen - und wurde NIRGENDS angewandt, weder
im Shader noch im Rasterzustand. 173 Vorkommen in den Shadern, und der
Vorsprung fuer Brandspuren und Schilder fehlte einfach.

Beides faellt im Bild nur auf, wenn man weiss, wonach man sucht. Im
Quelltext faellt es sofort auf, wenn man danach FRAGT.

Geprueft wird: kommt jedes in gpustate.h erklaerte Bit mindestens einmal in
einer AUSWERTENDEN Stelle vor - also in gpushader.cpp (erzeugt Quelltext),
gpumap_win32.cpp (stellt einen Zustand ein) oder gpudraw.cpp (waehlt aus)?
"""
import re
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent
KOPF = HIER.parent / "include" / "bhed" / "gpustate.h"
AUSWERTER = [
    HIER.parent / "src" / "gpushader.cpp",
    HIER.parent / "gui" / "gpumap_win32.cpp",
    HIER.parent / "src" / "gpudraw.cpp",
]

# Bits, die absichtlich keinen Quelltext erzeugen. Jeder Eintrag braucht
# einen Grund - eine Ausnahmeliste ohne Begruendung ist eine stillgelegte
# Pruefung.
ERLAUBT = {
    "kSpecular": "alphaGen lightingSpecular wird nicht gerechnet - der "
                 "Rasterer auch nicht (AlphaGen::Unsupported). Das Bit haelt "
                 "nur fest, dass die Flaeche ein Glanzlicht will.",
}

BIT = re.compile(r"^\s*(k[A-Za-z0-9]+)\s*=\s*1U\s*<<", re.M)


def main() -> int:
    if not KOPF.exists():
        print("Merkmalsbits: %s fehlt" % KOPF)
        return 1
    bits = BIT.findall(KOPF.read_text(encoding="utf-8"))
    if not bits:
        print("Merkmalsbits: keine Bits gefunden - hat sich der Kopf "
              "geaendert?")
        return 1

    text = ""
    for d in AUSWERTER:
        if not d.exists():
            continue
        t = d.read_text(encoding="utf-8")
        # --- Die Kuerzeltabelle zaehlt NICHT als Verwendung --------------
        #
        # In `shaderName` steht `{kPolygonOffset, "_po"}`. Das ist ein
        # Eintrag im NAMEN, keine Anwendung. Die erste Fassung dieses
        # Pruefers zaehlte ihn mit - und meldete deshalb "ok" fuer genau
        # den Fehler, fuer den er geschrieben wurde. Gegen den echten Fall
        # geprueft, nicht angenommen.
        t = re.sub(r"static const Eintrag kListe\[\][^;]*;", "", t,
                   flags=re.S)
        # --- Und ERKLAERUNGEN zaehlen auch nicht ------------------------
        #
        # Die zweite Fassung strich die Kuerzeltabelle heraus und meldete
        # weiter "ok" - weil in gpumap_win32.cpp ein Kommentar steht, der
        # den Namen des Bits nennt. Ein Pruefer, der Prosa mitzaehlt, prueft
        # nichts.
        t = re.sub(r"//[^\n]*", "", t)
        t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
        text += t

    fehler = 0
    for b in bits:
        # Die Erklaerung selbst zaehlt nicht - nur die Verwendung.
        if re.search(r"\b" + b + r"\b", text):
            continue
        if b in ERLAUBT:
            continue
        print('  "%s" ist erklaert, wird aber nirgends angewandt '
              "(weder Shader noch Zustand noch Auswahl)" % b)
        fehler += 1

    if fehler != 0:
        print("Merkmalsbitpruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("Merkmalsbitpruefung: ok (%d Bits, %d mit Begruendung ausgenommen)"
          % (len(bits), len(ERLAUBT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
