#!/usr/bin/env python3
"""
Sucht Oberflaechentexte, die fest im Quelltext stehen statt durch tr() zu
gehen. Ohne den Pruefer bleiben beim Uebersetzen einzelne Knoepfe englisch,
und das faellt erst auf, wenn jemand die Sprache umstellt.

Erlaubt sind:
  * Kennungen fuer ImGui, die mit ## anfangen oder gar nicht angezeigt werden
  * Formatzeichenketten, die nur Platzhalter und Satzzeichen enthalten
  * Produktnamen aus der Ausnahmeliste
"""
import re, sys, pathlib

# Funktionen, deren erstes Zeichenkettenargument der Nutzer SIEHT
SHOWN = ['Text', 'TextUnformatted', 'TextDisabled', 'TextColored', 'Button',
         'SmallButton', 'MenuItem', 'BeginMenu', 'Selectable', 'Checkbox',
         'RadioButton', 'BeginCombo', 'BeginPopupModal', 'BeginTabItem',
         'CollapsingHeader', 'TreeNode', 'SliderFloat', 'InputText',
         'SetTooltip', 'LabelText', 'BulletText']

AUSNAHMEN = {'BehavEd', 'ICARUS', 'OpenJK', 'ImGui', 'Dear ImGui', 'JKA', 'JK2'}

CALL = re.compile(r'ImGui::(' + '|'.join(SHOWN) + r')\s*\(\s*("(?:[^"\\]|\\.)*")')

def harmlos(lit):
    s = lit[1:-1]
    if s.startswith('##') or s == '':
        return True
    # nur Platzhalter, Satzzeichen, Leerzeichen
    # --- Was als Platzhalter zaehlt ------------------------------------
    #
    # Die Regel lautete `%[a-zA-Z]` - ein Prozentzeichen DIREKT vor einem
    # Buchstaben. Damit galt "%d" als Platzhalter, "%-8s" oder "%6.2f" aber
    # nicht: eine Diagnosezeile mit Spaltenbreiten wurde beanstandet,
    # dieselbe Zeile ohne Breiten nicht. Das ist kein Unterschied, den
    # jemand gemeint hat.
    #
    # Der Ausdruck kennt jetzt Kennzeichen, Breite und Genauigkeit. Das
    # Leerzeichen als Kennzeichen (% d) ist BEWUSST nicht dabei - sonst
    # ginge "100% sicher" als Formatzeichenkette durch, und das ist Fliesstext.
    #
    # Nachgemessen ueber alle Zeichenketten in gui/: genau zwei aendern ihr
    # Urteil, beide neue Diagnosezeilen. Keine wird strenger.
    if (re.fullmatch(r'[\s%.\-+#0-9a-zA-Z()\[\]:,]*', s) and
            re.search(r'%[-+#0]*[0-9]*(?:\.[0-9]+)?[a-zA-Z]', s)):
        return True
    if s in AUSNAHMEN:
        return True
    # reine Trennzeichen
    if re.fullmatch(r'[\s\-=_.|:]*', s):
        return True
    return False

root = pathlib.Path(__file__).resolve().parent.parent
bad = 0
for f in sorted((root / 'gui').glob('*.cpp')) + sorted((root / 'gui').glob('*.h')):
    for n, line in enumerate(f.read_text(encoding='utf-8').split('\n'), 1):
        for m in CALL.finditer(line):
            if not harmlos(m.group(2)):
                print(f"{f.relative_to(root)}:{n}: fester Text in ImGui::{m.group(1)}(): {m.group(2)}")
                bad += 1
# --- Uebersetzte Texte, die nirgends benutzt werden ------------------------
#
# Anlass: beim Herausziehen der Bedienleiste aus der Modellspalte ging die
# HAUTAUSWAHL verloren. Der Uebersetzer meldete nichts - der Code lief ja
# weiter, nur zeigte er das Feld nicht mehr. Aufgefallen ist es allein
# daran, dass ModelSkin, ModelSkinHint und ModelSurfaces ploetzlich
# nirgends mehr vorkamen.
#
# Ein ungenutzter Text ist also nicht bloss Ballast, sondern oft die Spur
# einer verschwundenen Bedienung.
# Die Namen kommen aus include/bhed/i18n.h, NICHT aus gen_i18n.py.
#
# Der Erzeuger ist nicht mehr die einzige Quelle: von Hand ergaenzte Texte
# landen direkt in i18n.h und i18n.cpp. Genau die sah dieser Pruefer bisher
# nicht - und damit auch nicht, wenn sie spaeter ungenutzt liegenblieben.
# Aufgefallen bei TlHeight/TlHeightHint, die mit dem Hoehenregler
# verschwanden und trotzdem stehenblieben.
#
# Die Aufzaehlung IST massgeblich: was dort steht, wird uebersetzt.
# NUR die Aufzaehlung Str - i18n.h enthaelt auch Language, und deren Werte
# (English, German, ...) sind Sprachen, keine Texte. Der erste Anlauf hat
# sie mitgezaehlt und vier falsche Beanstandungen erzeugt.
_hdr = (root / 'include' / 'bhed' / 'i18n.h').read_text(encoding='utf-8')
_von = _hdr.find('enum class Str {')
_bis = _hdr.find('};', _von)
_names = re.findall(r'^\s{4}(\w+),\s*$', _hdr[_von:_bis], re.M)
_code = ''
for _f in sorted((root / 'gui').glob('*.cpp')) + sorted((root / 'src').glob('*.cpp')):
    _code += _f.read_text(encoding='utf-8')
_unused = [n for n in _names if f'Str::{n}' not in _code]
if _unused:
    print(f"{len(_unused)} uebersetzte Texte werden nirgends benutzt:")
    for _u in _unused:
        print(f"   {_u}")
    print("   (haeufig die Spur einer Bedienung, die verlorengegangen ist)")
    bad += len(_unused)

print("i18n-Pruefung:", "ok" if bad == 0 else f"{bad} Beanstandung(en)")
sys.exit(1 if bad else 0)
