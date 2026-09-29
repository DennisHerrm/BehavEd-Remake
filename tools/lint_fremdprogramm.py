#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Findet Pruefwerkzeuge, die ein fremdes Programm ungeschuetzt starten.

Warum es das gibt
-----------------
`tools/lint_headers.py` rief `g++` ohne jede Pruefung auf. Auf shanks
Rechner gibt es kein g++, nur Visual Studio. Beim Bauen von rc548:

    Traceback (most recent call last):
      File "tools/lint_headers.py", line 33, in <module>
        proc = subprocess.run(
    FileNotFoundError: [WinError 2] Das System kann die angegebene Datei
    nicht finden

Gewartet hat dieser Fehler die ganze Zeit. Aufgefallen ist er erst, als
shank Python installierte: vorher uebersprang `build.bat` die Pruefer
mangels Python, jetzt liefen sie.

Ein Pruefwerkzeug, das auf dem Rechner des Benutzers mit einem
Rueckverfolg abbricht, ist schlimmer als eines, das dort gar nicht laeuft:
es sieht aus wie ein Fehler IM PROGRAMM. shank hat entsprechend an der
falschen Stelle gesucht.

Was geprueft wird
-----------------
Jede Datei unter `tools/`, die `subprocess.run` (oder `Popen`,
`check_output`, `call`) mit einem festen Programmnamen aufruft, muss
vorher entweder

  * `shutil.which(<name>)` fragen, oder
  * `tools/uebersetzer.py` benutzen (das tut es selbst), oder
  * `FileNotFoundError` / `OSError` abfangen.

`sys.executable` ist ausgenommen: das ist der laufende Python selbst und
kann nicht fehlen.

Gegentest
---------
`--gegentest` prueft an einer Textprobe, dass ein ungeschuetzter Aufruf
auffaellt und ein geschuetzter nicht.
"""

import os
import re
import sys

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORDNER = os.path.join(WURZEL, "tools")

# subprocess.run([...]) / Popen([...]) / check_output([...]) / call([...])
AUFRUF = re.compile(
    r"subprocess\.(?:run|Popen|check_output|check_call|call)\s*\(\s*\[?\s*"
    r"([\"'])([^\"']+)\1")

# Diese Namen sind kein fremdes Programm.
HARMLOS = {"sys.executable"}


def geschuetzt(text):
    """Sichert sich die Datei irgendwo gegen ein fehlendes Programm ab?"""
    return ("shutil.which" in text
            or "uebersetzer.finde" in text
            or "import uebersetzer" in text
            or "FileNotFoundError" in text)


def pruefe_text(text, quelle):
    if geschuetzt(text):
        return []
    funde = []
    for nr, zeile in enumerate(text.splitlines(), 1):
        if zeile.lstrip().startswith("#"):
            continue
        m = AUFRUF.search(zeile)
        if m is None:
            continue
        programm = m.group(2)
        if programm in HARMLOS:
            continue
        funde.append(
            "%s:%d startet \"%s\" ungeschuetzt - fehlt es, bricht der Bau "
            "mit einem Rueckverfolg ab" % (quelle, nr, programm))
    return funde


def gegentest():
    proben = [
        ('r = subprocess.run(["g++", "-c", x])', True),
        ('import shutil\nif shutil.which("g++"):\n'
         '    r = subprocess.run(["g++", "-c", x])', False),
        ('import uebersetzer\nr = subprocess.run(["g++"])', False),
        ('try:\n    subprocess.run(["nm", o])\nexcept FileNotFoundError:\n'
         '    pass', False),
        ('r = subprocess.run([sys.executable, "x.py"])', False),
        ('subprocess.run(["nm", "-u", o])', True),
    ]
    schlecht = 0
    for text, erwartet in proben:
        traf = len(pruefe_text(text, "<probe>")) > 0
        ok = traf == erwartet
        if not ok:
            schlecht += 1
        print("  %s  %-46s erwartet %s, bekam %s"
              % ("ok  " if ok else "FEHL",
                 text.replace("\n", " ; ")[:46], erwartet, traf))
    if schlecht:
        print("Gegentest: %d von %d falsch" % (schlecht, len(proben)))
        return 1
    print("Gegentest: %d von %d richtig" % (len(proben), len(proben)))
    return 0


def main():
    if "--gegentest" in sys.argv:
        return gegentest()
    alle = []
    for name in sorted(os.listdir(ORDNER)):
        if not name.endswith(".py"):
            continue
        pfad = os.path.join(ORDNER, name)
        with open(pfad, encoding="utf-8", errors="replace") as f:
            alle.extend(pruefe_text(f.read(), "tools/" + name))
    if alle:
        print("Fremdprogramm-Pruefung: %d ungeschuetzte(r) Aufruf(e)"
              % len(alle))
        for z in alle:
            print("  " + z)
        return 1
    print("Fremdprogramm-Pruefung: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
