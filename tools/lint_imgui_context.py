#!/usr/bin/env python3
"""
Sucht Funktionen, die VOR ImGui::CreateContext() laufen und - direkt ODER
UEBER UMWEGE - ImGui anfassen.

Anlass: setApp() rief applyTheme(), und applyTheme() rief ImGui::GetStyle().
Beide laufen aus wWinMain vor CreateContext(). Kein Fehler beim Uebersetzen,
sondern eine Zusicherung beim Start:

    Assertion failed! imgui.cpp:3674
    GImGui != NULL && "No current context. Did you call
    ImGui::CreateContext() and ImGui::SetCurrentContext() ?"

Ein Pruefer, der nur direkte Aufrufe sieht, findet das nicht - der erste
Anlauf hier tat genau das und blieb bei der Gegenprobe still. Deshalb wird
der Aufrufgraph aufgebaut und fortgepflanzt.
"""
import re, sys, pathlib
from collections import defaultdict

# Aus wWinMain vor CreateContext() gerufen. Wer hier etwas ergaenzt, muss
# auch main_win32.cpp anschauen.
VOR_KONTEXT = [
    'createApp', 'destroyApp', 'setApp', 'loadSettings', 'saveSettings',
    'loadModel', 'reopenLastIfWanted', 'defaultWindowSize', 'setThemeById',
]

DEF = re.compile(r'^[A-Za-z_][\w:<>*&\[\] ]*?\b(\w+)\s*\([^;]*\)\s*(?:const\s*)?\{')
CALL = re.compile(r'\b(\w+)\s*\(')

root = pathlib.Path(__file__).resolve().parent.parent

bodies = {}          # Name -> Zeilen des Rumpfes
touches_imgui = set()
calls = defaultdict(set)

for f in sorted((root / 'gui').glob('*.cpp')):
    lines = f.read_text(encoding='utf-8').split('\n')
    i = 0
    while i < len(lines):
        m = DEF.match(lines[i])
        if not m:
            i += 1
            continue
        name = m.group(1)
        depth = lines[i].count('{') - lines[i].count('}')
        body = [lines[i]]
        j = i + 1
        while j < len(lines) and depth > 0:
            depth += lines[j].count('{') - lines[j].count('}')
            body.append(lines[j])
            j += 1
        text = '\n'.join(body)
        bodies[name] = text
        if 'ImGui::' in text:
            touches_imgui.add(name)
        for c in CALL.finditer(text):
            calls[name].add(c.group(1))
        i = j

# Fortpflanzen: wer etwas ruft, das ImGui anfasst, fasst es auch an.
changed = True
while changed:
    changed = False
    for fn, callees in calls.items():
        if fn in touches_imgui:
            continue
        if callees & touches_imgui:
            touches_imgui.add(fn)
            changed = True

bad = 0
for name in VOR_KONTEXT:
    if name not in bodies:
        continue
    if name in touches_imgui:
        # Weg zeigen, damit man nicht suchen muss
        weg = [name]
        cur = name
        for _ in range(6):
            nxt = next((c for c in sorted(calls[cur])
                        if c in touches_imgui and c != cur and c in bodies), None)
            if nxt is None or 'ImGui::' in bodies[cur]:
                break
            weg.append(nxt)
            cur = nxt
        print(f"{name}() fasst ImGui an, laeuft aber vor CreateContext()"
              f"   ({' -> '.join(weg)})")
        bad += 1



# --- Frueher Ausstieg nach SetCursorPos -------------------------------------
#
# Anlass: der Escape-Zweig im Event-Editor. Er stand direkt hinter einem
# SetCursorPosX und rief sofort EndPopup und return - ohne ein einziges
# Element dazwischen. ImGui bricht dann mit einer Zusicherung ab:
#
#     Code uses SetCursorPos() to extend window/parent boundaries.
#     Please submit an item e.g. Dummy() afterwards.
#
# Das ist zur UEBERSETZUNGSZEIT unsichtbar und faellt erst auf, wenn jemand
# die Taste drueckt. Hier gefunden.
_SUBMITS = ('Button', 'Text', 'Selectable', 'Checkbox', 'Combo', 'Slider',
            'Input', 'Dummy', 'Image', 'TreeNode', 'MenuItem', 'Separator',
            'ProgressBar', 'ColorEdit', 'RadioButton', 'BeginChild',
            'BeginCombo', 'BeginTable', 'PlotLines')

for _p in sorted(root.glob('gui/*.cpp')):
    _lines = _p.read_text(encoding='utf-8').split('\n')
    for _i, _ln in enumerate(_lines):
        if 'SetCursorPos' not in _ln:
            continue
        # KOMMENTARE zaehlen nicht. Ein Kommentar, der die Falle
        # BESCHREIBT ("SetCursorPosY + fruehes Verlassen ..."), ist keine
        # Verwendung - und genau so hat sich dieser Pruefer selbst
        # angesprochen. Ein Pruefer, der auf Prosa anschlaegt, kostet
        # Vertrauen: beim naechsten Mal sieht man weg.
        if _ln.lstrip().startswith(('//', '*', '/*')):
            continue
        # Eine SCHLEIFE zwischen Sprung und Element zaehlt nicht.
        #
        # rc504: `else` hat keine Parameter. Die Feldschleife nach dem
        # Cursorsprung lief also gar nicht, und ImGui brach ab. Der Pruefer
        # sah das `for` und die Elemente DARIN und war zufrieden - aber eine
        # Schleife garantiert kein Element, sie kann null Durchlaeufe haben.
        #
        # Das ist die Sorte Fehler, die nur der leere Fall zeigt: bei jedem
        # anderen Befehl folgt sofort ein Feld.
        _inSchleife = False
        for _j in range(_i + 1, min(_i + 9, len(_lines))):
            _l = _lines[_j]
            _t = _l.strip()
            if _t.startswith(('for ', 'for(', 'while ', 'while(')):
                _inSchleife = True
            if any(('ImGui::' + _x) in _l for _x in _SUBMITS):
                if _inSchleife:
                    print(f"{_p.name}:{_j + 1}: das Element nach SetCursorPos "
                          f"steht in einer Schleife - die kann null Durchlaeufe "
                          f"haben. Ein Dummy({{0,0}}) als Anker davor setzen")
                    bad += 1
                break
            # Auch ein FUNKTIONSAUFRUF zaehlt nicht als abgeschicktes
            # Element: er kann vorzeitig zurueckkehren, ohne eines zu
            # erzeugen. Genau so ist der Kartenreiter abgestuerzt -
            # SetCursorPosY in app.cpp, drawEntityLayers() in app_view3d.cpp,
            # und dort ein "return" bei leerer Karte.
            #
            # Der Pruefer kann nicht ueber Dateien hinweg schauen, deshalb
            # gilt hier die einfache Regel: nach SetCursorPos gehoert ein
            # Element, bevor irgendetwas anderes passiert. Ein
            # Dummy({0,0}) als Anker genuegt.
            if re.match(r'\s*\w+\(', _l) and 'ImGui::' not in _l:
                print(f"{_p.name}:{_j + 1}: Aufruf direkt nach SetCursorPos - "
                      f"ein Dummy({{0,0}}) als Anker davor setzen")
                bad += 1
                break
            if 'return;' in _l or 'EndPopup()' in _l or 'EndChild()' in _l:
                print(f"{_p.name}:{_j + 1}: Ausstieg nach SetCursorPos, "
                      f"ohne ein Element abzuschicken")
                print("   ImGui bricht dort mit einer Zusicherung ab")
                bad += 1
                break

# --- Die Rahmenhoehe kommt aus EINER Quelle ---------------------------------
#
# Anlass: die vier Spalten - Modusleiste, Ansicht, Skriptbaum, Knopfspalte -
# rechneten ihre Hoehe jede selbst aus, immer als "topH minus eine
# Textzeile". Die vier Rechnungen ergaben nicht dasselbe, und die Rahmen
# endeten unten nicht auf einer Linie. Das faellt nur beim Hinsehen auf,
# nicht beim Uebersetzen.
#
# Jetzt liefert Layout::frameH den Wert, und niemand rechnet neu.
_app = root / 'gui' / 'app.cpp'
if _app.exists():
    for _i, _ln in enumerate(_app.read_text(encoding='utf-8').split('\n'), 1):
        if 'l.topH -' in _ln or 'topH - ImGui::GetTextLineHeight' in _ln:
            if 'l.frameH =' in _ln:
                continue
            print(f"gui/app.cpp:{_i}: rechnet die Rahmenhoehe selbst - "
                  f"Layout::frameH benutzen")
            bad += 1

# --- BeginDisabled ohne EndDisabled -----------------------------------------
#
# Anlass rc174: beim Herausloesen der Einstellungen in eine eigene Funktion
# blieb ein BeginDisabled ohne Gegenstueck stehen. Das uebersetzt sauber,
# alle Proben blieben gruen - und das Programm brach beim ersten Bild ab:
#
#     IMGUI-ZUSICHERUNG: (0) && "Missing EndDisabled()"
#
# ImGui fuehrt dafuer einen Stapel; wer ihn nicht leert, faellt beim
# naechsten Bild darueber. Ein Textumbau kann so etwas jederzeit
# hinterlassen, und keine der bestehenden Pruefungen sah es.
#
# Geprueft wird JE FUNKTION, nicht je Datei: eine Datei kann ausgeglichen
# sein, waehrend zwei Funktionen sich gegenseitig ausgleichen.
#
# Ein UEBERSCHUSS an EndDisabled wird NICHT beanstandet: das ist das Muster
# "vorzeitig verlassen" - vor einem return wird der Stapel geleert, und
# textlich stehen dann mehr Enden als Anfaenge da. Drei solche Stellen gibt
# es hier, alle richtig.
for _p in sorted(root.glob('gui/*.cpp')):
    _lines = _p.read_text(encoding='utf-8').split('\n')
    _fn = None
    _fnline = 0
    _bal = 0
    for _i, _ln in enumerate(_lines, 1):
        # Eine Funktion auf oberster Ebene beginnt in Spalte 0 und endet
        # mit einer offenen Klammer; sie endet bei einem "}" in Spalte 0.
        if re.match(r'^[A-Za-z_].*\)\s*\{\s*$', _ln):
            _fn = _ln.strip()
            _fnline = _i
            _bal = 0
        elif _ln == '}':
            if _fn is not None and _bal > 0:
                print(f"{_p.name}:{_fnline}: {_bal} mal BeginDisabled ohne "
                      f"EndDisabled - ImGui bricht beim naechsten Bild ab")
                bad += 1
            _fn = None
            _bal = 0
        if _fn is not None:
            if 'ImGui::BeginDisabled' in _ln:
                _bal += 1
            elif 'ImGui::EndDisabled' in _ln:
                _bal -= 1

# Warum NUR BeginDisabled und nicht auch PushID/PushStyleColor?
#
# Versucht - und wieder herausgenommen. Die Zaehlung stimmt dort nicht,
# weil Push-Aufrufe in ZWEIGEN stehen:
#
#     PushStyleColor(Button, ...);        // immer
#     PushStyleColor(ButtonActive, ...);  // immer
#     if (fehler) { PushStyleColor(Text, rot); }
#     else        { PushStyleColor(Text, gelb); }
#     ...
#     PopStyleColor(3);                   // richtig: 2 + EINER der beiden
#
# Textlich sind das vier Anfaenge und drei Enden - der Pruefer meldete
# richtigen Code als Fehler. Ein Zaehler, der Zweige nicht kennt, kann das
# nicht unterscheiden, und ein Pruefer, der auf richtigen Code anschlaegt,
# kostet Vertrauen (siehe rc167).
#
# BeginDisabled steht dagegen fast immer geradeaus im Fluss, und die drei
# Stellen mit vorzeitigem Verlassen faengt die Regel "Ueberschuss ist in
# Ordnung" ab.

print("ImGui-Kontextpruefung:", "ok" if bad == 0 else f"{bad} Beanstandung(en)")
sys.exit(1 if bad else 0)
