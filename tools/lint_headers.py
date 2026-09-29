#!/usr/bin/env python3
"""
Prueft, ob jede Kopfdatei fuer sich allein uebersetzbar ist.

Eine Kopfdatei, die nur funktioniert, weil zufaellig vorher eine andere
eingebunden wurde, ist eine Falle: sie bricht, sobald jemand die Reihenfolge
aendert oder sie als erste einbindet. Das faellt beim normalen Bauen NICHT
auf - dort ist die Reihenfolge ja immer dieselbe.

Der Test dafuer ist billig und eindeutig: eine Datei erzeugen, die genau
diese eine Kopfdatei einbindet, und uebersetzen lassen.

Nebenbei geprueft:
  * Einbindewaechter vorhanden und eindeutig
  * kein "using namespace" im oeffentlichen Bereich
"""
import pathlib
import re
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import uebersetzer   # noqa: E402

root = pathlib.Path(__file__).resolve().parent.parent
headers = sorted((root / 'include' / 'bhed').glob('*.h'))
bad = 0
guards = {}

# Ohne Uebersetzer laesst sich "eigenstaendig uebersetzbar" nicht pruefen -
# die Textpruefungen darunter aber sehr wohl. Also nicht abbrechen.
#
# Vorher stand hier ein blankes `subprocess.run(['g++', ...])`. Auf einem
# Rechner ohne g++ - also auf jedem Windows-Rechner mit Visual Studio -
# brach das Bauen mit einem Python-Rueckverfolg ab, und die Batchdatei
# meldete dazu die Ursache eines ANDEREN Pruefers.
uebers = uebersetzer.finde()
if uebers is None:
    uebersetzer.fehlt_melden("Kopfdateipruefung (Uebersetzungsteil)")

for h in headers:
    rel = f"bhed/{h.name}"
    with tempfile.NamedTemporaryFile('w', suffix='.cpp', delete=False) as f:
        f.write(f'#include "{rel}"\nint main() {{}}\n')
        tmp = f.name
    if uebers is not None:
        befehl = uebers.syntaxbefehl(
            tmp, [str(root / "include"), str(root / "third_party")])
        gefunden, rc, ausgabe = uebersetzer.lauf(befehl)
        if not gefunden:
            # Zwischen finde() und hier verschwunden - kann passieren, soll
            # aber nicht das Bauen kosten.
            uebersetzer.fehlt_melden("Kopfdateipruefung (Uebersetzungsteil)")
            uebers = None
        elif rc != 0:
            first = next((l for l in ausgabe.split('\n')
                          if 'error' in l.lower()), '')
            print(f"{rel}: nicht eigenstaendig uebersetzbar")
            print(f"   {first[:100]}")
            bad += 1
    pathlib.Path(tmp).unlink()

    text = h.read_text(encoding='utf-8')

    # Einbindewaechter: #pragma once oder ein Makro.
    m = re.search(r'#ifndef\s+(\w+)', text)
    if '#pragma once' not in text and m is None:
        print(f"{rel}: kein Einbindewaechter")
        bad += 1
    elif m is not None:
        if m.group(1) in guards:
            print(f"{rel}: Waechter {m.group(1)} schon in {guards[m.group(1)]}")
            bad += 1
        guards[m.group(1)] = rel

    # "using namespace" in einer Kopfdatei zwingt es jedem auf, der sie
    # einbindet - und das laesst sich nicht rueckgaengig machen.
    for i, line in enumerate(text.split('\n'), 1):
        if line.lstrip().startswith('using namespace'):
            print(f"{rel}:{i}: using namespace in einer Kopfdatei")
            bad += 1

print("Kopfdateipruefung:", "ok" if bad == 0 else f"{bad} Beanstandung(en)")
sys.exit(1 if bad else 0)
