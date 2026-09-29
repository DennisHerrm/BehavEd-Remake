#!/usr/bin/env python3
"""
Prueft, ob jede Quelldatei auch im Bauskript steht.

Uebernommen aus efxed, wo ein Linkerfehler erst beim Anwender auftrat: die
Datei gab es, sie uebersetzte, sie war geprueft - sie stand nur nicht in der
Quellenliste von CMakeLists.txt.

Hier ist dieselbe Luecke offen: der Kreuzbau und tools/verify.sh nehmen
src/*.cpp, also ein Muster, das jede neue Datei automatisch mitzieht.
CMakeLists zaehlt sie einzeln auf. Genau dazwischen faellt eine neue Datei
hindurch, lautlos - hier gruen, bei dir LNK2019.

Die Aufzaehlung in CMakeLists bleibt Absicht: ein GLOB merkt nicht, wenn eine
Datei dazukommt, und CMake laeuft dann nicht neu.
"""
import pathlib, re, sys

root = pathlib.Path(__file__).resolve().parent.parent
cml = (root / 'CMakeLists.txt').read_text(encoding='utf-8')

bad = 0
for folder, note in (('src', 'bhed_core'), ('gui', 'Ziel behaved')):
    for f in sorted((root / folder).glob('*.cpp')):
        rel = f"{folder}/{f.name}"
        if rel not in cml:
            print(f"{rel}: fehlt in CMakeLists.txt ({note})")
            bad += 1

# Umgekehrt: steht dort etwas, das es nicht mehr gibt?
for m in re.finditer(r'\b(src|gui|tests)/([\w.]+\.cpp)', cml):
    p = root / m.group(1) / m.group(2)
    if not p.exists():
        print(f"{m.group(1)}/{m.group(2)}: steht in CMakeLists.txt, gibt es aber nicht")
        bad += 1

# --- Manifest doppelt? -----------------------------------------------------
#
# Anlass: res/behaved.rc bettet ein Manifest ein, MSVC erzeugt zusaetzlich
# eines und legt es unter derselben Nummer ab. Der Ressourcenwandler bricht
# dann ab:
#     CVT1100: Doppelte Ressource. type:MANIFEST, name:1
#     LNK1123: Fehler bei der Konvertierung in COFF
# MinGW erzeugt keines, deshalb faellt es beim Kreuzbau NICHT auf - erst beim
# Bau mit Visual Studio, also beim Anwender.
rc = root / 'res' / 'behaved.rc'
if rc.exists() and 'MANIFEST' in rc.read_text(encoding='utf-8'):
    # Kommentarzeilen ueberspringen. Der erste Anlauf tat das nicht und fand
    # das Wort in der Begruendung darueber - die Gegenprobe blieb still,
    # obwohl die Option entfernt war.
    active = [ln for ln in (root / 'CMakeLists.txt').read_text(encoding='utf-8').split('\n')
              if not ln.lstrip().startswith('#')]
    if not any('/MANIFEST:NO' in ln for ln in active):
        print("res/behaved.rc bettet ein Manifest ein, aber CMakeLists.txt "
              "schaltet das von MSVC erzeugte nicht mit /MANIFEST:NO ab")
        print("   -> CVT1100 beim Bau mit Visual Studio")
        bad += 1

# --- Doppelte Abschnitte in der Dokumentation -----------------------------
#
# Anlass: BAUEN.md hatte 22 doppelte Abschnitte, weil Einfuegeanker wie
# "## Karte" mehrfach vorkamen und jede Einfuegung zweimal traf. Aufgefallen
# ist es erst nach Wochen, weil niemand eine 67000 Zeichen lange Datei
# vollstaendig liest.
import re as _re
import collections as _collections
for _doc in ('BAUEN.md', 'TESTING.md'):
    _p = root / _doc
    if not _p.exists():
        continue
    _heads = _re.findall(r'^#{1,4} (.+)$', _p.read_text(encoding='utf-8'), _re.M)
    _dupes = [h for h, c in _collections.Counter(_heads).items() if c > 1]
    if _dupes:
        print(f"{_doc}: {len(_dupes)} doppelte Ueberschrift(en)")
        for _d in _dupes[:5]:
            print(f"   {_d}")
        bad += 1

print("Quellenpruefung:", "ok" if bad == 0 else f"{bad} Beanstandung(en)")
sys.exit(1 if bad else 0)
