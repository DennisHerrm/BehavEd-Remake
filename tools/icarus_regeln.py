#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Prueft die Regeln aus dem offiziellen ICARUS-Handbuch gegen echte Skripte.

Quelle ist ICARUS_Manual.doc selbst, nicht das OpenJK-Wiki. Die
Sternchen-Liste in NICHT_SOFORT ist gegen das Handbuch abgeglichen
(Zeilen 941-1254 der Textfassung) und stimmt vollstaendig ueberein;
dazu kommt camera(PATH,...), das dort ebenfalls ein Sternchen traegt.

Zweck: herausfinden, welche Regeln UEBERHAUPT anschlagen wuerden, bevor
irgendjemand sie in behaved einbaut. Eine Regel, die auf 900 Zeilen
Raven-Skript nie greift, ist eine andere Sache als eine, die sofort trifft.

Die Regeln stammen aus dem OpenJK-Wiki, Abschnitte "Tasks", "Sound Command",
"Variables", "Flow Control".
"""
import re
import sys
import glob
import os

# Befehle, die NICHT sofort fertig sind - im Handbuch mit * markiert.
# Ein task ohne einen davon ist sofort fertig und wartet auf nichts.
NICHT_SOFORT = {
    "SET_TELEPORT_DEST", "SET_DPITCH", "SET_DYAW", "SET_VIEWTARGET",
    "SET_NAVGOAL", "SET_LOCATION", "SET_ANIM_UPPER", "SET_ANIM_LOWER",
    "SET_ANIM_BOTH", "SET_ANIM_HOLDTIME_LOWER", "SET_ANIM_HOLDTIME_UPPER",
    "SET_ANIM_HOLDTIME_BOTH", "SET_SOLID", "SET_ENDFRAME",
    "BS_ADVANCE_FIGHT", "BS_JUMP",
}
# Auf diesen Kanaelen kann man auf das Ende warten - auf anderen nicht.
WARTBARE_KANAELE = {"CHAN_VOICE", "CHAN_VOICE_ATTEN", "CHAN_VOICE_GLOBAL"}


def saeubern(text):
    """Kommentare weg, aber die BehavEd-Typmarken /*@TYP*/ bleiben egal."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def zerlege(text):
    """Sehr einfache Zerlegung in (tiefe, kopf, hat_block) je Anweisung."""
    zeilen = []
    tiefe = 0
    puffer = ""
    for zeichen in text:
        if zeichen == "{":
            kopf = puffer.strip()
            if kopf:
                zeilen.append((tiefe, kopf, True))
            puffer = ""
            tiefe += 1
        elif zeichen == "}":
            kopf = puffer.strip()
            if kopf:
                zeilen.append((tiefe, kopf, False))
            puffer = ""
            tiefe -= 1
            zeilen.append((tiefe, "}", False))
        elif zeichen == ";":
            kopf = puffer.strip()
            if kopf:
                zeilen.append((tiefe, kopf, False))
            puffer = ""
        else:
            puffer += zeichen
    return zeilen


def pruefe(pfad):
    text = saeubern(open(pfad, encoding="utf-8", errors="replace").read())
    zeilen = zerlege(text)
    funde = []

    # --- Regel 1: task in einer Schleife wird mehrfach angelegt -----------
    # Handbuch: "define them only once ... loop { task {} } is incorrect"
    schleifentiefe = []
    for tiefe, kopf, block in zeilen:
        w = kopf.split("(")[0].strip().lower()
        while schleifentiefe and schleifentiefe[-1] >= tiefe:
            schleifentiefe.pop()
        if w == "loop" and block:
            schleifentiefe.append(tiefe)
        elif w == "task" and block and schleifentiefe:
            funde.append(("R1 task in loop", kopf[:60]))

    # --- Regel 2: wait("name") ohne do/dowait desselben Namens -----------
    # Handbuch: "If you use the wait without using the do command first,
    # your script will NEVER CONTINUE"
    getan = set()
    gewartet = []
    aufgabe = set()
    for _, kopf, block in zeilen:
        w = kopf.split("(")[0].strip().lower()
        namen = re.findall(r'"([^"]*)"', kopf)
        if w == "task" and block and namen:
            aufgabe.add(namen[0])
        elif w in ("do", "dowait") and namen:
            getan.add(namen[0])
        if w in ("wait", "dowait") and namen:
            gewartet.append(namen[0])
    for n in gewartet:
        if n not in getan:
            funde.append(("R2 wait ohne do", n))

    # --- Regel 3: do/wait auf eine Aufgabe, die es hier nicht gibt -------
    # Handbuch: "A task is unique to the entity it is defined on"
    for _, kopf, _ in zeilen:
        w = kopf.split("(")[0].strip().lower()
        namen = re.findall(r'"([^"]*)"', kopf)
        if w in ("do", "dowait") and namen and namen[0] not in aufgabe:
            funde.append(("R3 do ohne task", namen[0]))

    # --- Regel 4: task, in dem nichts steht, worauf man warten kann ------
    # Handbuch: "you have to have at least one of these commands in it,
    # otherwise your task will complete as soon as you start it"
    stapel = []
    for tiefe, kopf, block in zeilen:
        w = kopf.split("(")[0].strip().lower()
        while stapel and stapel[-1][0] >= tiefe:
            t, name, wartbar = stapel.pop()
            if not wartbar:
                funde.append(("R4 task ohne Warter", name))
        if w == "task" and block:
            namen = re.findall(r'"([^"]*)"', kopf)
            stapel.append([tiefe, namen[0] if namen else "?", False])
        elif stapel:
            gross = kopf.upper()
            if any(s in gross for s in NICHT_SOFORT):
                stapel[-1][2] = True
            elif w == "sound" and any(k in gross for k in WARTBARE_KANAELE):
                stapel[-1][2] = True
            elif w in ("wait", "dowait"):
                stapel[-1][2] = True
            elif "PATH" in gross and w == "camera":
                stapel[-1][2] = True
    for _, name, wartbar in stapel:
        if not wartbar:
            funde.append(("R4 task ohne Warter", name))

    # --- Regel 5: sound auf nicht wartbarem Kanal in einem task ----------
    # Handbuch: "You cannot wait for sounds played on any of the other
    # channels."
    in_task = 0
    for tiefe, kopf, block in zeilen:
        w = kopf.split("(")[0].strip().lower()
        if w == "task" and block:
            in_task = tiefe
        elif kopf == "}" and tiefe <= in_task:
            in_task = -1
        if w == "sound" and in_task >= 0:
            m = re.search(r"CHAN_\w+", kopf)
            if m and m.group(0) not in WARTBARE_KANAELE:
                funde.append(("R5 unwartbarer Kanal in task", m.group(0)))

    # --- Regel 6: Variablen benutzt, die nie declare bekommen haben ------
    # Handbuch: "All variables must be declared before being used."
    erklaert = set()
    for _, kopf, _ in zeilen:
        w = kopf.split("(")[0].strip().lower()
        namen = re.findall(r'"([^"]*)"', kopf)
        if w == "declare" and namen:
            erklaert.add(namen[0])
    for _, kopf, _ in zeilen:
        for typ, name in re.findall(
                r'get\s*\(\s*(FLOAT|STRING|VECTOR)\s*,\s*"([^"]*)"', kopf):
            if not name.startswith("SET_") and name not in erklaert:
                funde.append(("R6 get auf undeklariert", "%s %s" % (typ, name)))

    # --- Regel 8: zwei verschiedene Aufgaben mit demselben Namen --------
    # Handbuch: "The TASKNAME is anything you want, though you should not
    # use the same TASKNAME for two different tasks."
    gesehen = {}
    for tiefe, kopf, block in zeilen:
        w = kopf.split("(")[0].strip().lower()
        if w == "task" and block:
            namen = re.findall(r'"([^"]*)"', kopf)
            if namen:
                gesehen[namen[0]] = gesehen.get(namen[0], 0) + 1
    for name, wie_oft in gesehen.items():
        if wie_oft > 1:
            funde.append(("R8 Aufgabenname doppelt",
                          "%s (%dx)" % (name, wie_oft)))

    # --- Regel 7: else ohne unmittelbar vorangehendes if -----------------
    #
    # ACHTUNG, eigener Fehler beim ersten Anlauf: hier stand "merke den
    # zuletzt gesehenen Befehl". Das ist falsch - zwischen `if` und `else`
    # liegt der ganze Rumpf des if. Der erste Lauf meldete deshalb sieben
    # Verstoesse in gonkability.txt, und alle sieben waren richtig
    # geschriebene if/else.
    #
    # Richtig: je Ebene merken, welcher Block dort zuletzt GESCHLOSSEN
    # wurde.
    zuletzt_zu = {}      # Tiefe -> Name des zuletzt geschlossenen Blocks
    offen = {}           # Tiefe -> Name des dort offenen Blocks
    for tiefe, kopf, block in zeilen:
        w = kopf.split("(")[0].strip().lower()
        if kopf == "}":
            zuletzt_zu[tiefe] = offen.get(tiefe)
            continue
        if w == "else" and zuletzt_zu.get(tiefe) not in ("if", "else"):
            funde.append(("R7 else ohne if", kopf[:40]))
        if block:
            offen[tiefe] = w
        else:
            zuletzt_zu[tiefe] = w     # einfacher Befehl trennt if und else
    return funde


def main():
    muster = sys.argv[1:] or ["data/fixtures/*.txt"]
    dateien = []
    for m in muster:
        dateien.extend(sorted(glob.glob(m)))
    if not dateien:
        print("keine Dateien gefunden")
        return 1
    gesamt = {}
    for pfad in dateien:
        funde = pruefe(pfad)
        print("%-34s %3d Befund(e)" % (os.path.basename(pfad), len(funde)))
        for regel, was in funde:
            gesamt[regel] = gesamt.get(regel, 0) + 1
            print("     %-30s %s" % (regel, was))
    print("\n--- Zusammenfassung ueber %d Datei(en) ---" % len(dateien))
    if not gesamt:
        print("  keine der acht Regeln schlaegt an")
    for regel in sorted(gesamt):
        print("  %-32s %4d" % (regel, gesamt[regel]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
