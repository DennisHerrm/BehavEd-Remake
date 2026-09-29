#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Findet Felder, deren Anfangsbelegung nur das erste Element trifft.

Warum es das gibt
-----------------
In `gui/gpumap_win32.cpp` stand:

    float g_messWert[kAbschnitte] = {-1.0F};

Gemeint war "alle acht auf -1". Gemacht wird: Element 0 auf -1, die
uebrigen sieben auf 0. So steht es im Standard - fehlende Werte in einer
Klammerliste werden wertinitialisiert, und das ist bei float die Null.

Die Folge stand in shanks Protokoll zu rc530, bei 0 Zeichenaufrufen:

    Zeiten der Grafikkarte (ms, -1 = keine Messung):
               Karte deckend       -1.000      <- ehrlich
               Karte gemischt       0.000      <- gelogen
               Effekte deckend      0.000
               ... fuenf weitere 0.000

Es wurde nichts gemessen. Sieben von acht Abschnitten meldeten trotzdem
eine Zahl, die sich wie ein Messwert liest: "gemessen und schnell" statt
"nie gemessen". Genau die Sorte Loch, die im Handout unter Abschnitt 2
steht - ein Protokoll mit Loch ist schlimmer als keines.

Kein Uebersetzer meldet das. Geprueft mit
`-Wall -Wextra -Wmissing-field-initializers -pedantic -Wconversion
-Wshadow`: keine Warnung. `-Wmissing-field-initializers` gilt fuer
Strukturen, nicht fuer Felder.

Was geprueft wird
-----------------
Zeilen der Form

    <Typ> <name>[<groesse>] = { <werte> };

Gemeldet wird, wenn

  * weniger Werte als Elemente dagestehen  UND
  * der erste Wert nicht Null ist.

Der zweite Punkt ist Absicht: `= {0}` und `= {}` heissen ueberall
"alles auf Null", und das stimmt auch. Falsch wird es erst, wenn der
gemeinte Grundwert etwas anderes ist als das, was der Standard
nachschiebt.

Ist die Groesse ein Name statt einer Zahl (`kAbschnitte`), wird sie im
selben Verzeichnisbaum als `constexpr ... = <zahl>` gesucht. Findet sich
nichts, wird die Zeile gemeldet, sobald genau ein Wert dasteht - dann ist
sie ohnehin nicht nachvollziehbar.

Gegentest
---------
Anders als drei der aelteren Pruefer hat dieser einen echten: mit
`--gegentest` baut er den Fehler in einer eigenen Textprobe wieder ein und
prueft, dass er anschlaegt. Eine Regel ohne Nachweis ist notierte Schuld.
"""

import os
import re
import sys

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORTE = ("src", "gui", "include")

# <Typ> <name>[<groesse>] = { ... };   alles auf einer Zeile
ZEILE = re.compile(
    r"""^\s*
        (?:static\s+|const\s+|constexpr\s+|inline\s+|thread_local\s+)*
        (?P<typ>float|double|int|long|short|char|bool|unsigned|
                std::uint8_t|std::uint16_t|std::uint32_t|std::uint64_t|
                std::int8_t|std::int16_t|std::int32_t|std::int64_t|
                uint8_t|uint16_t|uint32_t|uint64_t|
                UINT|UINT64|INT|FLOAT|BOOL|size_t|std::size_t)
        \s+(?P<name>\w+)
        \s*\[\s*(?P<groesse>[A-Za-z_]\w*|\d+)\s*\]
        \s*=\s*\{(?P<werte>[^{}]*)\}\s*;""",
    re.VERBOSE,
)

KONST = re.compile(
    r"(?:constexpr|const)\s+(?:int|std::size_t|size_t|unsigned)\s+"
    r"(\w+)\s*=\s*(\d+)\s*;"
)

NULLWERT = re.compile(r"^(?:0|0\.0*[fF]?|0[fF]|0\.0|nullptr|false|'\\0')$")


def dateien(wurzel):
    for ort in ORTE:
        pfad = os.path.join(wurzel, ort)
        for tief, _, namen in os.walk(pfad):
            if "third_party" in tief:
                continue
            for n in sorted(namen):
                if n.endswith((".cpp", ".h", ".hpp")):
                    yield os.path.join(tief, n)


def konstanten(wurzel):
    """Alle `constexpr int NAME = 7;` im Baum, fuer Feldgroessen mit Namen."""
    tabelle = {}
    for pfad in dateien(wurzel):
        with open(pfad, encoding="utf-8", errors="replace") as f:
            for name, wert in KONST.findall(f.read()):
                tabelle.setdefault(name, int(wert))
    return tabelle


def werte_zaehlen(text):
    """Zaehlt die Werte einer Klammerliste. Kommas in Klammern zaehlen nicht."""
    text = text.strip()
    if not text:
        return 0
    tiefe = 0
    anzahl = 1
    for zeichen in text:
        if zeichen in "([":
            tiefe += 1
        elif zeichen in ")]":
            tiefe -= 1
        elif zeichen == "," and tiefe == 0:
            anzahl += 1
    return anzahl


def pruefe_text(text, tabelle, quelle):
    """Gibt die Fundstellen als Liste von (zeilennummer, meldung) zurueck."""
    funde = []
    for nr, zeile in enumerate(text.splitlines(), 1):
        if "//" in zeile:
            zeile = zeile.split("//", 1)[0]
        treffer = ZEILE.match(zeile)
        if treffer is None:
            continue
        werte = treffer.group("werte")
        anzahl = werte_zaehlen(werte)
        if anzahl == 0:
            continue                      # `= {}` heisst ueberall alles Null
        erster = werte.split(",")[0].strip()
        if NULLWERT.match(erster):
            continue                      # `= {0}` meint auch alles Null
        gr = treffer.group("groesse")
        groesse = int(gr) if gr.isdigit() else tabelle.get(gr)
        if groesse is None:
            if anzahl > 1:
                continue                  # Groesse unbekannt, aber mehrere
                                          # Werte - nicht der Fehler von rc530
            groesse = -1                  # unbekannt UND genau ein Wert
        if groesse == -1 or anzahl < groesse:
            wieviele = "?" if groesse == -1 else str(groesse)
            funde.append(
                (nr,
                 "%s:%d: %s[%s] bekommt %d Wert(e) fuer %s Elemente - "
                 "die uebrigen werden NULL, nicht %s"
                 % (quelle, nr, treffer.group("name"), gr,
                    anzahl, wieviele, erster))
            )
    return funde


def gegentest():
    """Den Fehler von rc530 nachbauen und pruefen, dass er auffaellt."""
    proben = [
        # (Text, soll anschlagen?)
        ("float g_messWert[kAbschnitte] = {-1.0F};", True),
        ("float mess[8] = {-1.0F};", True),
        ("int paneTabs[kMaxSplit] = {-1, -1, -1, -1};", False),
        ("float g_bildDaten[40] = {0.0F};", False),
        ("float leer[8] = {};", False),
        ("const float sx[4] = {-1.0F, 1.0F, 1.0F, -1.0F};", False),
        ("float halb[4] = {-1.0F, 1.0F};", True),
        ("float g_messWert[kAbschnitte] = {-1.0F};  // erklaerender Text", True),
    ]
    tabelle = {"kAbschnitte": 8, "kMaxSplit": 4}
    schlecht = 0
    for text, erwartet in proben:
        traf = len(pruefe_text(text, tabelle, "<probe>")) > 0
        zeichen = "ok  " if traf == erwartet else "FEHL"
        if traf != erwartet:
            schlecht += 1
        print("  %s  %-58s erwartet %s, bekam %s"
              % (zeichen, text[:58], erwartet, traf))
    if schlecht:
        print("Gegentest: %d von %d Proben falsch" % (schlecht, len(proben)))
        return 1
    print("Gegentest: %d von %d Proben richtig" % (len(proben), len(proben)))
    return 0


def main():
    if "--gegentest" in sys.argv:
        return gegentest()
    tabelle = konstanten(WURZEL)
    alle = []
    for pfad in dateien(WURZEL):
        with open(pfad, encoding="utf-8", errors="replace") as f:
            text = f.read()
        kurz = os.path.relpath(pfad, WURZEL)
        alle.extend(m for _, m in pruefe_text(text, tabelle, kurz))
    if alle:
        print("lint_feldinit: %d Stelle(n), die nur das erste Element belegen"
              % len(alle))
        for m in alle:
            print("  " + m)
        return 1
    print("lint_feldinit: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
