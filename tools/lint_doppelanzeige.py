#!/usr/bin/env python3
"""Zeigt die Oberflaeche dieselbe Zahl an zwei Stellen an?

Warum das ein eigener Pruefer ist:

Die Aufstellung des GPU-Wegs (`gpuKarte`, `gpuMover`, ...) gab es ZWEIMAL -
einmal im View-Menue und einmal in der Seitenleiste. Das Menue ist beim
Hinsehen zu. Die Zeitmarken aus rc431 und der Zustandsschalter aus rc435
landeten in der unsichtbaren Fassung, und auf den Bildern stand weiter die
alte Zeile. Zwei Runden lang.

Abschnitt 6 der Uebergabe nennt das Muster: "Eine Auskunft, die zu spaet
oder am falschen Ort steht, luegt." Zwei Fassungen derselben Anzeige sind
derselbe Fehler wie zwei Fassungen derselben Bedingung.

Geprueft wird: kommt ein Zaehlerfeld des Programmzustands in MEHR ALS EINEM
ImGui-Text-Aufruf vor? Dann muss dort eine Begruendung stehen.
"""
import re
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent
GUI = HIER.parent / "gui"

# Felder, die eine MESSUNG anzeigen. Beschriftungen und Zustaende duerfen
# mehrfach vorkommen - eine Zahl nicht.
FELDER = [
    "gpuAufrufe", "gpuUebersprungen", "gpuKarte", "gpuMover",
    "gpuFiguren2", "gpuGluehen", "gpuEffekte",
]

# Stellen, an denen die Doppelung gewollt ist. Jeder Eintrag braucht einen
# Grund - eine Ausnahmeliste ohne Begruendung ist eine stillgelegte Pruefung.
ERLAUBT = {
    # "gpuAufrufe": "Kurzfassung im Menue, Aufstellung in der Leiste",
}

TEXT = re.compile(r"ImGui::Text\w*\s*\(")


def main() -> int:
    treffer: dict[str, list[str]] = {f: [] for f in FELDER}
    for datei in sorted(GUI.glob("*.cpp")):
        zeilen = datei.read_text(encoding="utf-8").split("\n")
        for i, zeile in enumerate(zeilen):
            if not TEXT.search(zeile):
                continue
            # Der Aufruf kann ueber mehrere Zeilen gehen.
            block = "\n".join(zeilen[i:i + 6])
            block = block[:block.find(");") + 2] if ");" in block else block
            for f in FELDER:
                if re.search(r"\b" + f + r"\b", block):
                    treffer[f].append("%s:%d" % (datei.name, i + 1))

    fehler = 0
    for f, orte in treffer.items():
        if len(orte) > 1 and f not in ERLAUBT:
            print('  "%s" wird an %d Stellen angezeigt: %s'
                  % (f, len(orte), ", ".join(orte)))
            fehler += 1
    if fehler != 0:
        print("Doppelanzeigepruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("Doppelanzeigepruefung: ok (%d Zaehler, je hoechstens eine Stelle)"
          % len(FELDER))
    return 0


if __name__ == "__main__":
    sys.exit(main())
