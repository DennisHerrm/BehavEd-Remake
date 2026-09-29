#!/usr/bin/env python3
"""Sagt das Protokoll, welches Paket es ist?

Warum das ein eigener Pruefer ist:

Zu rc448 kam ein Protokoll zurueck, in dem der Bericht fehlte. Der Aufruf
stand richtig im Quelltext, es gab keinen anderen Ausstieg - und trotzdem
liess sich nicht entscheiden, WELCHES rc448 gelaufen war. Es hatte zwei
gegeben: eines mit einem Menuepunkt, eines mit dem Bericht ohne Knopf.

Im Kopf stand `behaved 1.0.0` und eine Bauzeit. Beides aendert sich auch
dann, wenn sich nichts aendert, und keines von beiden nennt die Fassung.
Eine Auskunft, die die einzige Frage nicht beantwortet, fuer die es sie
gibt.

Geprueft wird: `kFassung` in include/bhed/fassung.h stimmt mit der obersten
Ueberschrift in AENDERUNGEN.md ueberein. Damit kann man das eine nicht
aendern und das andere vergessen.
"""
import re
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent
KOPF = HIER.parent / "include" / "bhed" / "fassung.h"
LISTE = HIER.parent / "AENDERUNGEN.md"


def main() -> int:
    if not KOPF.exists() or not LISTE.exists():
        print("Fassungspruefung: fassung.h oder AENDERUNGEN.md fehlt")
        return 1

    m = re.search(r'kFassung\s*=\s*"([^"]+)"', KOPF.read_text(encoding="utf-8"))
    if m is None:
        print("Fassungspruefung: kFassung nicht gefunden")
        return 1
    imKopf = m.group(1)

    o = re.search(r"^##\s+(rc\d+)", LISTE.read_text(encoding="utf-8"), re.M)
    if o is None:
        print("Fassungspruefung: keine Ueberschrift der Form '## rcNNN' in "
              "AENDERUNGEN.md")
        return 1
    inListe = o.group(1)

    if imKopf != inListe:
        print("  fassung.h sagt %s, AENDERUNGEN.md sagt %s"
              % (imKopf, inListe))
        print("Fassungspruefung: 1 Beanstandung")
        return 1
    print("Fassungspruefung: ok (%s)" % imKopf)
    return 0


if __name__ == "__main__":
    sys.exit(main())
