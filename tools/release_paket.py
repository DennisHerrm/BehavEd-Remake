#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Baut und prueft das Release-Paket (rc601 release).

Warum es das gibt
-----------------
shank, 17.10.2026: "bei jedem Release sollte es auch eine .exe geben, die
gebaut wird, dass die Leute sie runterladen und nutzen koennen". Bis rc600
entstanden Zip und Notizen von Hand, ausserhalb des Repos. Dabei ging ab rc598
die Zeile "Install: unzip and start BehavEd-Remake/behaved.exe" verloren, und
niemand pruefte die ausgelieferte .exe: sie brauchte die VC-Laufzeit, die
nicht im Zip lag (tools/check_exe_imports.py am rc600: "Importpruefung: 16
Beanstandung(en)").

Jetzt baut .github/workflows/release.yml bei jeder Marke v1.0.0-rcNNN das
Paket mit diesem Werkzeug und prueft es, bevor etwas hochgeladen wird.

Aufrufe
-------
  release_paket.py fassung [--marke v1.0.0-rcNNN]
      kFassung ausgeben. Mit --marke: Abbruch, wenn die Marke nicht
      v1.0.0-<kFassung> heisst - sonst boete der Updater das Release ewig neu
      an (istNeuer vergleicht die rc-Nummer der Marke mit kFassung).
  release_paket.py zip <behaved.exe> <ziel.zip>
      BehavEd-Remake/behaved.exe, BehavEd-Remake/data/base/* und jede Datei,
      die git unter handbuch/ fuehrt (git ls-files: Seiten, die nur im
      Arbeitsordner liegen, bleiben draussen). Deflate, '/' als Trenner,
      sortiert, keine Ordnereintraege - die Form von rc600.
  release_paket.py pruefe <zip> --fassung rcNNN [--exe <eigenstaendige.exe>]
      alles, was Updater und Anwender vom Zip erwarten (siehe pruefe()).
  release_paket.py notizen rcNNN <ziel.md>
      die Release-Notizen: zuerst die Download-Zeilen, dann der [en]-Block der
      obersten Fassung in tools/changelog.txt - oder release-notes/rcNNN.md,
      wenn es die Datei gibt. Schreibt selbst UTF-8 (eine Umleitung durch
      PowerShell machte aus Gedankenstrich und Pfeil Fragezeichen).
  release_paket.py pruefsummen <ziel.txt> <datei>...
      SHA256SUMS im Format von sha256sum, LF, nach Namen sortiert.
  release_paket.py selbsttest
      prueft das Werkzeug an kuenstlichen Paketen (ctest release_paket).

Ueberall gilt --wurzel <ordner> statt des Repos, in dem dieses Werkzeug liegt.

Nur die Standardbibliothek. git wird vorher mit shutil.which gesucht - ein
fehlendes Programm soll eine klare Meldung geben, keinen Rueckverfolg
(tools/lint_fremdprogramm.py).
"""

import argparse
import hashlib
import importlib.util
import io
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import warnings
import zipfile

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(HIER)
OBER = "BehavEd-Remake"
SPRACHEN = ("en", "de", "zh", "ja")
# Die Grenzen des Lesers, mit dem der Updater das Zip oeffnet (src/pk3.cpp):
# die Eintragszahl steht dort als u16, Groessen und Versatz als u32 (kein
# ZIP64), und er entpackt nur "gespeichert" (0) und Deflate (8).
PACKVERFAHREN = (0, 8)
MAX_EINTRAEGE = 0xFFFF
MAX_GROESSE = 0xFFFFFFFF


class Fehler(Exception):
    pass


def _importpruefung():
    # Ueber den Pfad laden, nicht ueber sys.path: das Werkzeug soll auch mit
    # "python -I" laufen (dann steht tools/ nicht im Suchpfad).
    spec = importlib.util.spec_from_file_location(
        "check_exe_imports", os.path.join(HIER, "check_exe_imports.py"))
    modul = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(modul)
    return modul


# --- Fassung und Marke ------------------------------------------------------

def fassung_lesen(wurzel):
    pfad = os.path.join(wurzel, "include", "bhed", "fassung.h")
    with open(pfad, encoding="utf-8") as f:
        m = re.search(r'kFassung\s*=\s*"(rc\d+)"', f.read())
    if m is None:
        raise Fehler("kFassung nicht gefunden in %s" % pfad)
    return m.group(1)


def marke_passt(marke, rc):
    return marke == "v1.0.0-" + rc


# --- Das Zip ------------------------------------------------------------------

def handbuch_dateien(wurzel):
    git = shutil.which("git")
    if git is None:
        raise Fehler("git nicht gefunden - ohne 'git ls-files' laesst sich nicht "
                     "sagen, welche Handbuchseiten ins Paket gehoeren")
    r = subprocess.run([git, "-C", wurzel, "ls-files", "-z", "--", "handbuch"],
                       capture_output=True)
    if r.returncode != 0:
        raise Fehler("git ls-files: " + r.stderr.decode("utf-8", "replace").strip())
    return sorted(n for n in r.stdout.decode("utf-8").split("\0") if n)


def _zeitstempel():
    # SOURCE_DATE_EPOCH (gesetzt im Ablauf auf die Zeit des Commits) macht das
    # Zip wiederholbar: zweimal gebaut, zweimal dieselben Bytes.
    wert = os.environ.get("SOURCE_DATE_EPOCH", "").strip()
    if not wert.isdigit():
        return None
    return max(time.gmtime(int(wert))[:6], (1980, 1, 1, 0, 0, 0))


def zip_bauen(exe, ziel, wurzel, handbuch=None):
    eintraege = [(OBER + "/behaved.exe", exe)]
    # rc604: data/jk2 (Befehlssatz fuer Jedi Outcast) neben data/base.
    for ordner in ("base", "jk2"):
        daten = os.path.join(wurzel, "data", ordner)
        if not os.path.isdir(daten):
            continue
        for n in sorted(os.listdir(daten)):
            if os.path.isfile(os.path.join(daten, n)):
                eintraege.append((OBER + "/data/" + ordner + "/" + n, os.path.join(daten, n)))
    if handbuch is None:
        handbuch = handbuch_dateien(wurzel)
    if not handbuch:
        raise Fehler("handbuch/: keine Datei unter Versionsverwaltung - das Zip "
                     "waere ohne Handbuch")
    for rel in handbuch:
        quelle = os.path.join(wurzel, *rel.split("/"))
        if not os.path.isfile(quelle):
            raise Fehler("%s steht in git, fehlt aber im Arbeitsordner" % rel)
        eintraege.append((OBER + "/" + rel, quelle))
    # Sortiert, aber behaved.exe ZULETZT: der Updater spielt die Eintraege der
    # Reihe nach ein und bricht beim ersten Fehler ab. Scheitert er vor der
    # .exe, laeuft weiter die alte Fassung - und bietet das Update beim
    # naechsten Start wieder an. Stuende die .exe vorn, meldete sich nach
    # einem halben Update schon die neue Fassung, und data/ oder handbuch/
    # blieben halb alt.
    eintraege.sort(key=lambda e: (e[0] == OBER + "/behaved.exe", e[0]))
    ordner = os.path.dirname(os.path.abspath(ziel))
    os.makedirs(ordner, exist_ok=True)
    fest = _zeitstempel()
    with zipfile.ZipFile(ziel, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, quelle in eintraege:
            with open(quelle, "rb") as f:
                inhalt = f.read()
            zeit = fest or max(time.localtime(os.path.getmtime(quelle))[:6], (1980, 1, 1, 0, 0, 0))
            info = zipfile.ZipInfo(name, date_time=zeit)
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, inhalt, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    return len(eintraege)


# --- Die Pruefung -------------------------------------------------------------

def _pe_maengel(exe, daten, imp):
    """Was an der behaved.exe im Zip nicht stimmt (Liste von Saetzen)."""
    maengel = []
    if len(exe) < 0x40 or exe[:2] != b"MZ":
        return ["behaved.exe ist keine Windows-exe (MZ fehlt)"]
    pe = struct.unpack_from("<I", exe, 0x3C)[0]
    if pe + 26 > len(exe) or exe[pe:pe + 4] != b"PE\0\0":
        return ["behaved.exe: PE-Kopf fehlt"]
    maschine = struct.unpack_from("<H", exe, pe + 4)[0]
    if maschine != 0x8664:
        maengel.append("behaved.exe ist keine 64-Bit-exe (Machine 0x%04x, erwartet 0x8664 = x64)" % maschine)
    if struct.unpack_from("<H", exe, pe + 24)[0] != 0x20B:
        maengel.append("behaved.exe: kein PE32+-Kopf")
    try:
        dlls = imp.imports_aus_daten(exe)
    except (struct.error, ValueError, IndexError, UnicodeDecodeError) as e:
        return maengel + ["behaved.exe: Importtabelle nicht lesbar (%s)" % e]
    for dll in sorted(dlls, key=str.lower):
        klein = dll.lower()
        if klein in imp.VERBOTEN:
            maengel.append("behaved.exe bindet %s fest - ohne die DLL startet das Programm nicht "
                           "(Rueckfall auf OpenGL kaeme nie zum Zug)" % dll)
        elif klein not in imp.ERLAUBT:
            maengel.append("behaved.exe braucht %s - die liegt nicht auf jedem Windows "
                           "(VC-Laufzeit? CMAKE_MSVC_RUNTIME_LIBRARY)" % dll)
    fehlend = [n for n, b in daten if b and b not in exe]
    if fehlend:
        maengel.append("behaved.exe enthaelt die eingebauten Daten nicht (%s) - allein liefe "
                       "sie nicht (bhed/eingebaut.h)" % ", ".join(fehlend))
    return maengel


def pruefe(zip_pfad, rc, wurzel, exe_pfad=None, ausgabe=print):
    """Gibt die Liste der Beanstandungen zurueck (leer = in Ordnung)."""
    maengel = []

    def melde(text):
        maengel.append(text)
        ausgabe("  FEHL  " + text)

    def gut(text):
        ausgabe("  ok    " + text)

    if not re.fullmatch(r"rc\d+", rc or ""):
        melde("Fassung %r hat nicht die Form rcNNN" % rc)
        return maengel
    soll = "BehavEd-Remake-%s-windows-x64.zip" % rc
    name = os.path.basename(zip_pfad)
    if name == soll:
        gut("Name " + name)
    else:
        melde("Name %s - erwartet %s (Marke des Releases, -windows-x64)" % (name, soll))

    try:
        z = zipfile.ZipFile(zip_pfad)
    except (zipfile.BadZipFile, OSError) as e:
        melde("kein lesbares Zip: %s" % e)
        return maengel
    with z:
        infos = z.infolist()
        if len(infos) >= MAX_EINTRAEGE:
            melde("%d Eintraege - der Updater liest hoechstens %d (src/pk3.cpp, u16)"
                  % (len(infos), MAX_EINTRAEGE - 1))
        if os.path.getsize(zip_pfad) >= MAX_GROESSE:
            melde("das Zip ist groesser als 4 GB (ZIP64 kann der Updater nicht)")
        # orig_filename: unter Windows macht zipfile aus "\" in filename ein
        # "/" - der Rueckstrich waere so unsichtbar.
        roh = [i.orig_filename for i in infos]
        gesehen = set()
        doppelt = set()
        for n in roh:
            (doppelt if n in gesehen else gesehen).add(n)
        for n in sorted(doppelt):
            melde("doppelter Eintrag %s (der Updater nimmt dann irgendeinen)" % n)
        schlecht = 0
        for i in infos:
            n = i.orig_filename
            teile = n.split("/")
            if "\\" in n:
                melde("Rueckstrich im Namen %r - im Zip trennt '/'" % n)
            if n.startswith("/") or re.match(r"^[A-Za-z]:", n):
                melde("absoluter Name %r - der Updater ueberspringt ihn" % n)
            if ".." in teile:
                melde("'..' im Namen %r - der Updater ueberspringt ihn" % n)
            if n.endswith("/"):
                melde("Ordnereintrag %r" % n)
            elif "" in teile:
                melde("leerer Namensteil in %r" % n)
            if i.compress_type not in PACKVERFAHREN:
                melde("%s: Packverfahren %d - der Updater entpackt nur 0 und 8 (src/pk3.cpp)"
                      % (n, i.compress_type))
            if i.flag_bits & 0x1:
                melde("%s ist verschluesselt" % n)
            if i.file_size >= MAX_GROESSE or i.compress_size >= MAX_GROESSE:
                melde("%s braucht ZIP64 (groesser als 4 GB)" % n)
            schlecht += 0
        oben = sorted({n.replace("\\", "/").split("/")[0] for n in roh})
        lose = [n for n in roh if "/" not in n.replace("\\", "/")]
        if oben == [OBER] and not lose:
            gut("genau ein oberster Ordner %s/ (%d Eintraege)" % (OBER, len(infos)))
        else:
            melde("erwartet genau einen obersten Ordner %s/, gefunden: %s"
                  % (OBER, ", ".join(oben[:6]) + (" ..." if len(oben) > 6 else "")))

        nach_name = {}
        for i in infos:
            nach_name.setdefault(i.orig_filename, i)

        def lies(rel):
            info = nach_name.get(OBER + "/" + rel)
            if info is None or info.compress_type not in PACKVERFAHREN:
                return None
            try:
                return z.read(info)
            except (zipfile.BadZipFile, OSError, NotImplementedError, RuntimeError) as e:
                melde("%s nicht lesbar: %s" % (rel, e))
                return None

        exe = lies("behaved.exe")
        if exe is None:
            melde("behaved.exe fehlt")
        elif roh[-1] != OBER + "/behaved.exe":
            melde("behaved.exe ist nicht der letzte Eintrag - scheitert das Update danach, meldet sich "
                  "schon die neue Fassung, und der Rest bleibt halb alt (zip_bauen legt sie zuletzt)")
        else:
            gut("behaved.exe ist der letzte Eintrag (ein abgebrochenes Update laesst die alte Fassung)")
        for ordner in ("base", "jk2"):   # rc604: auch data/jk2 (Jedi Outcast)
            daten_ordner = os.path.join(wurzel, "data", ordner)
            if ordner != "base" and not os.path.isdir(daten_ordner):
                continue
            daten = []
            for n in sorted(os.listdir(daten_ordner)):
                p = os.path.join(daten_ordner, n)
                if not os.path.isfile(p):
                    continue
                with open(p, "rb") as f:
                    daten.append((n, f.read()))
            abweichend = []
            for n, b in daten:
                drin = lies("data/%s/%s" % (ordner, n))
                if drin is None:
                    melde("data/%s/%s fehlt" % (ordner, n))
                elif drin != b:
                    abweichend.append(n)
            if abweichend:
                melde("data/%s weicht vom Repo ab: %s" % (ordner, ", ".join(abweichend)))
            elif daten and all(nach_name.get(OBER + "/data/%s/%s" % (ordner, n)) for n, _ in daten):
                gut("data/%s: %d Dateien, Byte fuer Byte wie im Repo" % (ordner, len(daten)))
        seiten = ["handbuch/index.html"] + ["handbuch/%s/index.html" % s for s in SPRACHEN]
        fehlen = [s for s in seiten if OBER + "/" + s not in nach_name]
        if fehlen:
            melde("Handbuch unvollstaendig, es fehlt: %s" % ", ".join(fehlen))
        else:
            gut("Handbuch: Startseite und alle vier Sprachen")
        if exe is not None:
            m = _pe_maengel(exe, daten, _importpruefung())
            for t in m:
                melde(t)
            if not m:
                gut("behaved.exe: x64, nur Windows-DLLs fest gebunden, Daten eingebaut")
        if exe_pfad is not None:
            soll_exe = "BehavEd-Remake-%s-windows-x64.exe" % rc
            if os.path.basename(exe_pfad) != soll_exe:
                melde("eigenstaendige exe heisst %s - erwartet %s" % (os.path.basename(exe_pfad), soll_exe))
            try:
                with open(exe_pfad, "rb") as f:
                    allein = f.read()
            except OSError as e:
                allein = None
                melde("eigenstaendige exe nicht lesbar: %s" % e)
            if allein is not None and exe is not None:
                if allein == exe:
                    gut("eigenstaendige exe gleicht behaved.exe im Zip")
                else:
                    melde("eigenstaendige exe weicht von behaved.exe im Zip ab")
    ausgabe("Paketpruefung: " + ("ok" if not maengel else "%d Beanstandung(en)" % len(maengel)))
    return maengel


# --- Die Notizen --------------------------------------------------------------

# Die ersten Zeilen jedes Releases. Die Updateanzeige im Programm zeigt den
# Text unformatiert (gui/update_win32.cpp, TextUnformatted) - er muss also
# auch ohne Markdown gut lesbar sein. Gedankenstrich, Pfeil und Auslassung als
# \u-Folgen, damit diese Datei reines ASCII bleibt.
KOPF = (
    "**Download:** `BehavEd-Remake-{rc}-windows-x64.zip` (under **Assets** below) \u2013 "
    "Windows 10/11, 64-bit.\n"
    "\n"
    "- Unzip it (right-click \u2192 Extract All\u2026), open the `BehavEd-Remake` folder and "
    "start `behaved.exe`. Nothing is installed; keep `behaved.exe` next to its `data` folder.\n"
    "- Just the program: `BehavEd-Remake-{rc}-windows-x64.exe` also runs on its own "
    "(the offline guide is only in the zip).\n"
    "- Already using BehavEd-Remake? It updates itself (Help \u2192 Check for updates\u2026).\n"
    "- \"Source code (zip)\" and \"Source code (tar.gz)\" are for developers \u2013 they "
    "contain no program.\n"
    "- Windows SmartScreen may warn because the program is not code-signed: "
    "More info \u2192 Run anyway.\n"
)


def changelog_en(wurzel, rc):
    pfad = os.path.join(wurzel, "tools", "changelog.txt")
    oben = None
    sprache = None
    block = []
    with open(pfad, encoding="utf-8") as f:
        for zeile in f.read().splitlines():
            m = re.match(r"^## (\S+)", zeile)
            if m:
                if oben is not None:
                    break
                oben = m.group(1)
                continue
            if oben is None or zeile.startswith("#"):
                continue
            m = re.match(r"^\[(en|de|zh|ja)\]\s*$", zeile)
            if m:
                sprache = m.group(1)
                continue
            if sprache == "en":
                block.append(zeile)
    if oben != rc:
        raise Fehler("oberste Fassung in tools/changelog.txt ist %s, nicht %s" % (oben, rc))
    while block and not block[-1].strip():
        block.pop()
    if not block:
        raise Fehler("tools/changelog.txt: %s hat keinen [en]-Block" % rc)
    return "\n".join(block)


def notizen_text(rc, wurzel):
    kfassung = fassung_lesen(wurzel)
    if kfassung != rc:
        raise Fehler("kFassung ist %s, nicht %s" % (kfassung, rc))
    eigen = os.path.join(wurzel, "release-notes", rc + ".md")
    if os.path.isfile(eigen):
        with open(eigen, encoding="utf-8") as f:
            rumpf = f.read().strip("\n")
    else:
        rumpf = "## What's new in %s\n\n%s" % (rc, changelog_en(wurzel, rc))
    return KOPF.format(rc=rc) + "\n" + rumpf + "\n"


def notizen(rc, ziel, wurzel):
    text = notizen_text(rc, wurzel)
    with open(ziel, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return text


# --- Pruefsummen ----------------------------------------------------------------

def pruefsummen(ziel, dateien):
    zeilen = []
    for p in sorted(dateien, key=lambda d: os.path.basename(d)):
        h = hashlib.sha256()
        with open(p, "rb") as f:
            for stueck in iter(lambda: f.read(1 << 20), b""):
                h.update(stueck)
        zeilen.append("%s  %s\n" % (h.hexdigest(), os.path.basename(p)))
    with open(ziel, "w", encoding="ascii", newline="\n") as f:
        f.writelines(zeilen)
    return zeilen


# --- Selbsttest -----------------------------------------------------------------

def _probe_exe(dlls, maschine=0x8664, anhang=b""):
    """Die kleinste PE32+-Datei, die imports() lesen kann: DOS-Kopf, PE-Kopf,
    ein Abschnitt mit der Importtabelle - und hinten die Bytes `anhang`."""
    pe = 0x40
    optgroesse = 240                       # PE32+: 112 Byte + 16 Verzeichnisse
    kopf = bytearray(0x200)
    kopf[0:2] = b"MZ"
    struct.pack_into("<I", kopf, 0x3C, pe)
    kopf[pe:pe + 4] = b"PE\0\0"
    struct.pack_into("<HH", kopf, pe + 4, maschine, 1)
    struct.pack_into("<H", kopf, pe + 20, optgroesse)
    struct.pack_into("<H", kopf, pe + 24, 0x20B)
    rva = 0x1000
    tabelle = 20 * (len(dlls) + 1)
    namen = b""
    abschnitt = bytearray(tabelle)
    for k, dll in enumerate(dlls):
        struct.pack_into("<IIIII", abschnitt, 20 * k, 0, 0, 0, rva + tabelle + len(namen), 0)
        namen += dll.encode("ascii") + b"\0"
    abschnitt += namen
    roh = (len(abschnitt) + 0x1FF) & ~0x1FF
    abschnitt += bytes(roh - len(abschnitt))
    struct.pack_into("<II", kopf, pe + 24 + 112 + 8, rva, tabelle)
    struct.pack_into("<8sIIII", kopf, pe + 24 + optgroesse, b".idata", len(abschnitt), rva, roh, 0x200)
    return bytes(kopf) + bytes(abschnitt) + anhang


def _zip_aus(pfad, eintraege):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")      # "Duplicate name" ist hier Absicht
        with zipfile.ZipFile(pfad, "w", zipfile.ZIP_DEFLATED) as z:
            for name, inhalt in eintraege:
                z.writestr(zipfile.ZipInfo(name, date_time=(2026, 10, 17, 12, 0, 0)), inhalt,
                           compress_type=zipfile.ZIP_DEFLATED)


def _packverfahren_setzen(pfad, name, verfahren):
    """Traegt fuer einen Eintrag ein anderes Packverfahren ein (lokaler Kopf
    und Inhaltsverzeichnis) - zipfile selbst schreibt kein Deflate64 (9)."""
    b = bytearray(open(pfad, "rb").read())
    with zipfile.ZipFile(io.BytesIO(bytes(b))) as z:
        info = z.getinfo(name)
    struct.pack_into("<H", b, info.header_offset + 8, verfahren)
    p = b.find(b"PK\x01\x02")
    while p >= 0:
        laenge, extra, kommentar = struct.unpack_from("<HHH", b, p + 28)
        if bytes(b[p + 46:p + 46 + laenge]) == name.encode("ascii"):
            struct.pack_into("<H", b, p + 10, verfahren)
        p = b.find(b"PK\x01\x02", p + 46 + laenge + extra + kommentar)
    open(pfad, "wb").write(bytes(b))


def selbsttest():
    ergebnisse = []

    def erwarte(was, ok, maengel=None):
        print(("  ok    " if ok else "  FEHL  ") + was)
        for t in (maengel or [])[:3]:
            print("          -> " + t)
        ergebnisse.append(bool(ok))

    def still(_text):
        pass

    def schreibe(pfad, inhalt):
        os.makedirs(os.path.dirname(pfad), exist_ok=True)
        with open(pfad, "wb") as f:
            f.write(inhalt if isinstance(inhalt, bytes) else inhalt.encode("utf-8"))

    with tempfile.TemporaryDirectory(prefix="bhed_release_") as tmp:
        wurzel = os.path.join(tmp, "repo")
        rc = "rc900"
        schreibe(os.path.join(wurzel, "include", "bhed", "fassung.h"),
                 'inline constexpr const char* kFassung = "%s";\n' % rc)
        schreibe(os.path.join(wurzel, "tools", "changelog.txt"),
                 "# Kommentar\n\n## rc900 2026-10-17\n[en]\n- New: a thing.\n- Fixed: another one.\n"
                 "[de]\n- Neu: etwas.\n[zh]\n- \u65b0\n[ja]\n- \u65b0\u3057\u3044\n\n"
                 "## rc899 2026-10-16\n[en]\n- Old.\n")
        daten = {"anims.h": b"enum {BOTH_A};\r\n" * 30, "behaved.bhc": b"// bhc\r\n" * 50,
                 "supplement.bhc": b"// supp\n" * 20}
        for n, b in daten.items():
            schreibe(os.path.join(wurzel, "data", "base", n), b)
        handbuch = (["handbuch/assets/stil.css", "handbuch/index.html"] +
                    ["handbuch/%s/index.html" % s for s in SPRACHEN])
        for h in handbuch:
            schreibe(os.path.join(wurzel, *h.split("/")), "<!doctype html>" + h)
        schreibe(os.path.join(wurzel, "handbuch", "en", "entwurf.html"), "nur im Arbeitsordner")
        alle_daten = b"".join(daten[n] for n in sorted(daten))
        exe = os.path.join(tmp, "behaved.exe")
        schreibe(exe, _probe_exe(["KERNEL32.dll", "USER32.dll", "WINHTTP.dll"], anhang=alle_daten))

        # --- Fassung und Marke
        erwarte("fassung: kFassung = rc900", fassung_lesen(wurzel) == rc)
        erwarte("marke: v1.0.0-rc900 passt zu rc900", marke_passt("v1.0.0-rc900", rc))
        erwarte("marke: v1.0.0-rc901 zu rc900 wird abgewiesen (Updater boete es ewig an)",
                not marke_passt("v1.0.0-rc901", rc) and not marke_passt("rc900", rc))
        erwarte("aufruf: fassung --marke v1.0.0-rc901 endet mit 1",
                main(["fassung", "--marke", "v1.0.0-rc901", "--wurzel", wurzel], still) == 1)

        # --- Das Zip bauen
        gut_zip = os.path.join(tmp, "BehavEd-Remake-rc900-windows-x64.zip")
        git = shutil.which("git")
        if git is not None:
            subprocess.run([git, "init", "-q", wurzel], check=True, capture_output=True)
            subprocess.run([git, "-C", wurzel, "add", "--"] + handbuch, check=True, capture_output=True)
            n = zip_bauen(exe, gut_zip, wurzel)
        else:
            print("          (git fehlt - das Handbuch kommt aus der festen Liste)")
            n = zip_bauen(exe, gut_zip, wurzel, handbuch=handbuch)
        with zipfile.ZipFile(gut_zip) as z:
            namen = z.namelist()
            verfahren = {i.compress_type for i in z.infolist()}
        erwarte("zip: %d Eintraege, sortiert, behaved.exe zuletzt, Deflate, ohne Ordnereintraege" % n,
                n == 1 + len(daten) + len(handbuch) and namen[:-1] == sorted(namen[:-1])
                and namen[-1] == OBER + "/behaved.exe" and verfahren == {8}
                and not any(x.endswith("/") for x in namen))
        erwarte("zip: alles unter BehavEd-Remake/, behaved.exe dabei",
                all(x.startswith(OBER + "/") for x in namen) and OBER + "/behaved.exe" in namen)
        erwarte("zip: eine Seite, die git nicht kennt, bleibt draussen",
                OBER + "/handbuch/en/entwurf.html" not in namen)
        m = pruefe(gut_zip, rc, wurzel, ausgabe=still)
        erwarte("pruefe: das gebaute Zip besteht", not m, m)
        allein = os.path.join(tmp, "BehavEd-Remake-rc900-windows-x64.exe")
        shutil.copyfile(exe, allein)
        m = pruefe(gut_zip, rc, wurzel, exe_pfad=allein, ausgabe=still)
        erwarte("pruefe --exe: die eigenstaendige exe gleicht der im Zip", not m, m)

        # --- Kaputte Pakete: jedes muss auffallen
        with zipfile.ZipFile(gut_zip) as z:
            grund = [(i.filename, z.read(i)) for i in z.infolist()]

        def kaputt(was, eintraege, stichwort, name="BehavEd-Remake-rc900-windows-x64.zip",
                   nachher=None, exe_pfad=None):
            ordner = tempfile.mkdtemp(dir=tmp)
            pfad = os.path.join(ordner, name)
            _zip_aus(pfad, eintraege)
            if nachher is not None:
                nachher(pfad)
            m = pruefe(pfad, rc, wurzel, exe_pfad=exe_pfad, ausgabe=still)
            treffer = [t for t in m if stichwort in t]
            erwarte("pruefe weist ab: " + was, bool(treffer), treffer + [t for t in m if stichwort not in t])

        def ersetze(name, inhalt):
            return [(n, inhalt if n == name else b) for n, b in grund]

        kaputt("Name ohne -windows-x64 (wie rc600)", grund, "erwartet BehavEd-Remake-rc900-windows-x64.zip",
               name="BehavEd-Remake-rc900.zip")
        kaputt("zwei oberste Ordner", grund + [("Anderes/liesmich.txt", b"x")], "obersten Ordner")
        kaputt("eine Datei ganz oben", grund + [("liesmich.txt", b"x")], "obersten Ordner")
        kaputt("Rueckstrich im Namen", grund + [(OBER + "/handbuch/a#b.html", b"x")], "Rueckstrich",
               nachher=lambda p: open(p, "r+b").write(open(p, "rb").read().replace(b"a#b", b"a\\b")))
        kaputt("'..' im Namen", grund + [(OBER + "/../boese.exe", b"x")], "'..'")
        kaputt("absoluter Name", grund + [("/abs/boese.exe", b"x")], "absoluter Name")
        kaputt("Laufwerksbuchstabe", grund + [("C:/boese.exe", b"x")], "absoluter Name")
        kaputt("Packverfahren 9 (Deflate64)", grund + [(OBER + "/handbuch/gross.bin", b"y" * 100)],
               "Packverfahren 9",
               nachher=lambda p: _packverfahren_setzen(p, OBER + "/handbuch/gross.bin", 9))
        kaputt("Ordnereintrag", grund + [(OBER + "/data/", b"")], "Ordnereintrag")
        kaputt("doppelter Eintrag", grund + [(OBER + "/handbuch/index.html", b"zwei")], "doppelter Eintrag")
        kaputt("behaved.exe vorn statt zuletzt (wie sortiert)",
               [e for e in grund if e[0].endswith("behaved.exe")] + [e for e in grund if not e[0].endswith("behaved.exe")],
               "nicht der letzte Eintrag")
        kaputt("behaved.exe fehlt", [(n, b) for n, b in grund if not n.endswith("behaved.exe")],
               "behaved.exe fehlt")
        kaputt("Handbuch ohne Japanisch", [(n, b) for n, b in grund if "/ja/" not in n], "handbuch/ja/index.html")
        kaputt("data/base weicht ab", ersetze(OBER + "/data/base/anims.h", b"anders"), "weicht vom Repo ab")
        kaputt("data/base unvollstaendig", [(n, b) for n, b in grund if not n.endswith("supplement.bhc")],
               "data/base/supplement.bhc fehlt")
        kaputt("exe mit VC-Laufzeit (wie rc600)",
               ersetze(OBER + "/behaved.exe",
                       _probe_exe(["KERNEL32.dll", "MSVCP140.dll", "VCRUNTIME140.dll"], anhang=alle_daten)),
               "MSVCP140.dll")
        kaputt("exe mit fest gebundenem d3d11.dll",
               ersetze(OBER + "/behaved.exe", _probe_exe(["KERNEL32.dll", "d3d11.dll"], anhang=alle_daten)),
               "d3d11.dll")
        kaputt("32-Bit-exe",
               ersetze(OBER + "/behaved.exe", _probe_exe(["KERNEL32.dll"], maschine=0x14C, anhang=alle_daten)),
               "keine 64-Bit-exe")
        kaputt("exe ohne eingebaute Daten (wie rc600)",
               ersetze(OBER + "/behaved.exe", _probe_exe(["KERNEL32.dll"])), "eingebauten Daten")
        kaputt("keine exe", ersetze(OBER + "/behaved.exe", b"kein Programm"), "keine Windows-exe")
        fremd = os.path.join(tmp, "anders", "BehavEd-Remake-rc900-windows-x64.exe")
        schreibe(fremd, _probe_exe(["KERNEL32.dll"], anhang=alle_daten + b"!"))
        kaputt("eigenstaendige exe weicht ab", grund, "weicht von behaved.exe im Zip ab", exe_pfad=fremd)
        falsch = os.path.join(tmp, "behaved-rc900.exe")
        shutil.copyfile(exe, falsch)
        kaputt("eigenstaendige exe falsch benannt", grund, "erwartet BehavEd-Remake-rc900-windows-x64.exe",
               exe_pfad=falsch)
        m = pruefe(os.path.join(tmp, "behaved.exe"), rc, wurzel, ausgabe=still)
        erwarte("pruefe weist ab: gar kein Zip", any("kein lesbares Zip" in t for t in m), m)

        # --- Die Notizen
        ziel = os.path.join(tmp, "notizen.md")
        text = notizen(rc, ziel, wurzel)
        erste = text.splitlines()[0]
        erwarte("notizen: die erste Zeile ist die Download-Zeile mit dem Zip",
                erste.startswith("**Download:** `BehavEd-Remake-rc900-windows-x64.zip`"))
        erwarte("notizen: nennen die eigenstaendige exe, 'Source code' und SmartScreen",
                "BehavEd-Remake-rc900-windows-x64.exe" in text and "Source code" in text
                and "SmartScreen" in text)
        erwarte("notizen: der [en]-Block von rc900, nicht [de] und nicht rc899",
                "- New: a thing." in text and "- Fixed: another one." in text and "Neu: etwas" not in text
                and "- Old." not in text)
        with open(ziel, "rb") as f:
            roh = f.read()
        erwarte("notizen: UTF-8 mit LF (Gedankenstrich und Pfeil heil)",
                b"\r\n" not in roh and "\u2013".encode("utf-8") in roh and "\u2192".encode("utf-8") in roh)
        try:
            notizen_text("rc901", wurzel)
            erwarte("notizen: rc901 zu kFassung rc900 wird abgewiesen", False)
        except Fehler:
            erwarte("notizen: rc901 zu kFassung rc900 wird abgewiesen", True)
        schreibe(os.path.join(wurzel, "release-notes", "rc900.md"), "## Handgeschrieben\n\n- Text.\n")
        text2 = notizen_text(rc, wurzel)
        erwarte("notizen: release-notes/rc900.md geht vor, die Download-Zeile bleibt vorn",
                text2.startswith("**Download:**") and "## Handgeschrieben" in text2 and "New: a thing" not in text2)
        schreibe(os.path.join(wurzel, "tools", "changelog.txt"), "## rc899 2026-10-16\n[en]\n- Old.\n")
        os.remove(os.path.join(wurzel, "release-notes", "rc900.md"))
        try:
            notizen_text(rc, wurzel)
            erwarte("notizen: oberste Fassung im Changelog != rc900 wird abgewiesen", False)
        except Fehler:
            erwarte("notizen: oberste Fassung im Changelog != rc900 wird abgewiesen", True)

        # --- Pruefsummen
        summen = os.path.join(tmp, "SHA256SUMS.txt")
        zeilen = pruefsummen(summen, [gut_zip, allein])
        with open(summen, "rb") as f:
            roh = f.read()
        erwarte("pruefsummen: sha256sum-Format, nach Namen sortiert, LF",
                len(zeilen) == 2 and all(re.fullmatch(r"[0-9a-f]{64}  \S+\n", z) for z in zeilen)
                and zeilen[0].endswith("windows-x64.exe\n") and b"\r" not in roh
                and hashlib.sha256(open(gut_zip, "rb").read()).hexdigest() in zeilen[1])

    falsch = ergebnisse.count(False)
    print("Selbsttest release_paket: " + ("%d von %d richtig" % (len(ergebnisse), len(ergebnisse))
                                          if not falsch else "%d von %d FALSCH" % (falsch, len(ergebnisse))))
    return 1 if falsch else 0


# --- Aufruf -----------------------------------------------------------------------

def main(argv=None, ausgabe=print):
    p = argparse.ArgumentParser(description="Release-Paket von BehavEd-Remake bauen und pruefen")
    p.add_argument("--wurzel", default=WURZEL, help="Repo-Ordner (Vorgabe: der dieses Werkzeugs)")
    unter = p.add_subparsers(dest="befehl", required=True)
    f = unter.add_parser("fassung")
    f.add_argument("--marke")
    z = unter.add_parser("zip")
    z.add_argument("exe")
    z.add_argument("ziel")
    q = unter.add_parser("pruefe")
    q.add_argument("zip")
    q.add_argument("--fassung", required=True)
    q.add_argument("--exe")
    n = unter.add_parser("notizen")
    n.add_argument("rc")
    n.add_argument("ziel")
    s = unter.add_parser("pruefsummen")
    s.add_argument("ziel")
    s.add_argument("dateien", nargs="+")
    unter.add_parser("selbsttest")
    # --wurzel darf auch hinter dem Befehl stehen.
    argv = list(sys.argv[1:] if argv is None else argv)
    if "--wurzel" in argv:
        k = argv.index("--wurzel")
        argv = argv[k:k + 2] + argv[:k] + argv[k + 2:]
    a = p.parse_args(argv)
    wurzel = os.path.abspath(a.wurzel)
    try:
        if a.befehl == "fassung":
            rc = fassung_lesen(wurzel)
            if a.marke is not None and not marke_passt(a.marke, rc):
                ausgabe("Marke %s passt nicht zu kFassung %s - erwartet v1.0.0-%s (sonst boete der "
                        "Updater das Release ewig an)" % (a.marke, rc, rc))
                return 1
            ausgabe(rc)
            return 0
        if a.befehl == "zip":
            anzahl = zip_bauen(a.exe, a.ziel, wurzel)
            ausgabe("Zip: %s (%d Eintraege)" % (a.ziel, anzahl))
            return 0
        if a.befehl == "pruefe":
            return 1 if pruefe(a.zip, a.fassung, wurzel, exe_pfad=a.exe, ausgabe=ausgabe) else 0
        if a.befehl == "notizen":
            notizen(a.rc, a.ziel, wurzel)
            ausgabe("Notizen: %s" % a.ziel)
            return 0
        if a.befehl == "pruefsummen":
            for zeile in pruefsummen(a.ziel, a.dateien):
                ausgabe(zeile.rstrip("\n"))
            return 0
        if a.befehl == "selbsttest":
            return selbsttest()
    except (Fehler, OSError) as e:
        ausgabe("release_paket: %s" % e)
        return 2
    return 2


if __name__ == "__main__":
    sys.exit(main())
