#!/usr/bin/env python3
"""Prueft: was zur KARTE gehoert, wird beim WECHSEL auch weggeraeumt.

Anlass rc216. clearMap() raeumte die aufgestellten Modelle, aber
loadMapFromBytes() - der Weg beim WECHSEL - tat es nicht. Also blieb das
Schiff der vorigen Karte stehen, und zwar bis zum Neustart.

Beide Wege muessen dieselben Listen leeren. Ausgenommen sind reine
DATEICACHES: die haengen am Dateinamen, nicht an der Karte, und sie
wegzuwerfen waere nur Verschwendung.
"""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
quelle = (root / 'gui' / 'app.cpp').read_text(encoding='utf-8')

# Dateicaches: absichtlich NICHT beim Wechsel geleert.
erlaubt = {'md3Files', 'md3Meshes', 'settings.mapPath'}


def geleert(funktion: str) -> set:
    i = quelle.find(funktion)
    if i < 0:
        return set()
    # bis zur ersten Zeile, die auf Spalte 0 mit } endet
    j = quelle.find('\n}\n', i)
    koerper = quelle[i:j]
    return set(re.findall(r'g_app->([A-Za-z_.]+)\.clear\(\)', koerper))


beim_leeren = geleert('void clearMap() {')
beim_wechsel = geleert('void loadMapFromBytes(')

if not beim_leeren:
    print('lint_mapstate: clearMap() nicht gefunden')
    sys.exit(1)

# --- Was auf NUMMERN zeigt, muss beim Wechsel zurueckgesetzt werden -------
#
# Nach Namen abgelegte Caches sind harmlos: derselbe Name meint dieselbe
# Datei. Nummern dagegen werden je Karte NEU vergeben - "Netz 5" oder
# "Entity 12" meint nach dem Wechsel etwas voellig anderes.
#
# rc217: genau daran lag es. brushMeshes liegt nach Untermodellnummer ab,
# und die Netze der vorigen Karte standen in der neuen an fremder Stelle.
nach_nummer = ['brushMeshes', 'pickedEntity']
i = quelle.find('void loadMapFromBytes(')
j = quelle.find('\n}\n', i)
wechselkoerper = quelle[i:j]
offen = [n for n in nach_nummer if f'g_app->{n}' not in wechselkoerper]
if offen:
    print('Zeigt auf NUMMERN der Karte, wird beim Wechsel aber nicht'
          ' zurueckgesetzt:')
    for o in offen:
        print(f'   g_app->{o}')
    print('   (Nummern werden je Karte neu vergeben - siehe rc217)')
    sys.exit(1)

fehlt = beim_leeren - beim_wechsel - erlaubt
if fehlt:
    print('Beim KARTENWECHSEL nicht weggeraeumt, obwohl clearMap() es tut:')
    for f in sorted(fehlt):
        print(f'   g_app->{f}')
    print('   (so blieb in rc215 das Schiff der vorigen Karte stehen)')
    print(f'Kartenzustandspruefung: {len(fehlt)} Beanstandung(en)')
    sys.exit(1)

print('Kartenzustandspruefung: ok')
