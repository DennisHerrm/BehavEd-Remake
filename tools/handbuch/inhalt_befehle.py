"""Nachschlagen: jeder Befehl - die Befehle der Events-Liste, die Kamera-
Unterbefehle, die Makros und jedes SET_-Feld.

shank: "vielleicht auch eine Erklaerung, was jeder Button kann und jeder
Befehl". Die Befehle der Liste sind von Hand erklaert; Kamera, Makros und
SET_-Felder liest der Bau aus data/base/behaved.bhc und Q3_Interface.h -
derselben Quelle, aus der das Programm seine Befehle kennt. Deren Texte
stammen von Raven; der Editor zeigt sie englisch, das Handbuch (rc601)
uebersetzt sie in uebersetzung/<sprache>/befehle.json.
"""
import os
import re

from inhalt_basis import T

_DATEN = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "data", "base")


def _lies(name):
    with open(os.path.join(_DATEN, name), encoding="latin-1") as f:
        return f.read()


_ART = {"s": "text", "f": "number", "d": "whole number", "v": "vector", "t": "list"}


def _wert(spec):
    """'%s=""' -> 'text', '%t="BOOL_TYPES"' -> 'BOOL_TYPES'."""
    m = re.match(r'%([sfdvt])(?:="([^"]*)")?', spec.strip())
    if not m:
        return ""
    if m.group(1) == "t" and m.group(2):
        return "`%s`" % m.group(2)
    return _ART[m.group(1)]


def _set_felder():
    """Jedes SET_ aus setType_e: (Gruppe, Name, Wert, Beschreibung)."""
    text = _lies("Q3_Interface.h")
    teil = text[text.index("setType_e"):text.index("} setType_t")]
    gruppe = ""
    aus = []
    for zeile in teil.splitlines():
        z = zeile.strip()
        m = re.match(r"//# #sep (.*)", z)
        if m:
            gruppe = m.group(1).strip()
            continue
        # Bis zum LETZTEN "#" (rc601): die Skript-Felder tragen eines im
        # Dateifilter ("W:\gamease\scripts\!!#*.txt") - vorher stand
        # '*.txt" # Script to run ...' in der Beschreibung. Ein "//"-Kommentar
        # dahinter gehoert nicht dazu (SET_SPAWNSCRIPT).
        m = re.match(r"(SET_\w+)\s*(?:=\s*\d+)?\s*,\s*//##\s*(.*)#\s*(.*)$", z)
        if m:
            aus.append((gruppe, m.group(1), _wert(m.group(2)), m.group(3).split("//")[0].strip()))
    return aus


def _kamera():
    text = _lies("behaved.bhc")
    teil = text[text.index('<%i="CAMERA_COMMANDS">'):]
    teil = teil[:teil.index("}")]
    aus = []
    for zeile in teil.splitlines():
        # Bis zum LETZTEN "#": PATH traegt eines im Dateifilter (!!#*.rof).
        m = re.match(r"\s*(\w+)\s*//##(.*)#\s*(.*)$", zeile)
        if m:
            werte = ", ".join(w for w in (_wert(x) for x in re.findall(r'%[sfdvt](?:="[^"]*")?', m.group(2))) if w)
            aus.append((m.group(1), werte, m.group(3).split("//")[0].strip()))
    return aus


def _makros():
    text = _lies("behaved.bhc")
    teil = text[text.index("//#=== Macros"):]
    aus = []
    for m in re.finditer(r'^"(\w+)"[ \t]*//#[ \t]*([^\n]*)\n\s*\{([^}]*)\}', teil, re.M):
        n = len(re.findall(r"^\s*\w+\s*\(", m.group(3), re.M))
        aus.append((m.group(1), m.group(2).strip(), n))
    return aus


_SEITE = {"flush": "cmd-flush", "if / else": "cmd-if", "loop": "cmd-loop", "affect": "cmd-affect", "run": "cmd-run", "wait": "cmd-wait",
          "waitsignal": "cmd-waitsignal", "signal": "cmd-signal", "sound": "cmd-sound", "move": "cmd-move", "rotate": "cmd-rotate",
          "use": "cmd-use", "kill": "cmd-kill", "remove": "cmd-remove", "print": "cmd-print", "rem": "cmd-rem", "declare / free": "cmd-declare",
          "set": "cmd-set", "camera": "cmd-cam-move", "task": "cmd-task", "do": "cmd-do", "dowait": "cmd-dowait", "play": "cmd-play"}


def _verlinkt(name):
    """Der Befehlsname als Link auf seine eigene Seite (rc601)."""
    return "[[%s|%s]]" % (_SEITE[name], name) if name in _SEITE else name


def _zeile(*zellen):
    return [T(z if z.strip() else "-") for z in zellen]


BEFEHLE = [
    ("flush", "Clears every command still waiting for this entity - what an `affect` block with `FLUSH` does at its start.", "`flush ( )`"),
    ("if / else", "Runs the block only if the comparison holds (`=`, `<`, `>`, `!` for *not equal*). `else` must follow right after an `if`.", "`if ( $get( FLOAT, \"count\")$, $>$, $2$ )`"),
    ("loop", "Repeats its block; the number says how often, `-1` means forever.", "`loop ( 3 )`"),
    ("affect", "Sends the commands of its block to one entity - a figure, a ship, a door - by its name. `FLUSH` replaces what the entity was doing, `INSERT` adds to it. The script itself does not wait for the block.", "`affect ( \"kyle\", FLUSH )`"),
    ("run", "Starts another script on the entity that runs this one.", "`run ( \"taspir_tut/kyle_spawn\" )`"),
    ("wait", "Pauses for so many milliseconds - or, with a name, until that task is finished.", "`wait ( 2000 )`"),
    ("waitsignal", "Pauses until some other script calls `signal` with the same name. Only one script can wait for a given signal.", "`waitsignal ( \"door_open\" )`"),
    ("signal", "Wakes the script that waits for this name.", "`signal ( \"door_open\" )`"),
    ("sound", "Plays a sound on a channel of the entity - `CHAN_VOICE` for dialogue, so the mouth moves.", "`sound ( CHAN_VOICE, \"sound/chars/kyle/...\" )`"),
    ("move", "Moves an entity (a door, a platform, a ship) to a position - and, if given, to new angles - in so many milliseconds. The Animate window writes these for you ([[k-tuer]]).", "`move ( < 0 0 128 >, < 0 0 0 >, 2000 )`"),
    ("rotate", "Turns an entity to angles (pitch, yaw, roll) in so many milliseconds.", "`rotate ( < 0 90 0 >, 1000 )`"),
    ("use", "Uses an entity by name, as if the player pressed it - opens a door, fires a trigger.", "`use ( \"door1\" )`"),
    ("kill", "Kills the entity with this name.", "`kill ( \"stormie1\" )`"),
    ("remove", "Removes the entity with this name from the game.", "`remove ( \"stormie1\" )`"),
    ("print", "Shows a text in the middle of the screen.", "`print ( \"Hello\" )`"),
    ("rem", "A comment - does nothing in the game. **{{ActRem}}** turns any line into one.", "`rem ( \"camera starts here\" )`"),
    ("declare / free", "Declares a variable (`FLOAT`, `STRING`, `VECTOR`; at most 16 per map) - `free` gives the name back.", "`declare ( FLOAT, \"count\" )`"),
    ("set", "Changes a value: an entity field (`SET_NAVGOAL`, `SET_ANIM_BOTH`, ... - see the full list below) or a declared variable.", "`set ( SET_WALKING, \"true\" )`"),
    ("camera", "Controls the cutscene camera; the first value chooses what it does - see the next section.", "`camera ( ENABLE )`"),
    ("task", "Declares a named group of commands - it does not run yet.", "`task ( \"walk\" )`"),
    ("do", "Starts a task and goes on at once.", "`do ( \"walk\" )`"),
    ("dowait", "Starts a task and waits until it is done - for a walk: until the figure has arrived.", "`dowait ( \"walk\" )`"),
    ("play", "Plays a ROFF file on the entity of the current `affect` block - a recorded movement.", "`play ( PLAY_ROFF, \"taspir_tut/roffs/ravensClaw\" )`"),
]

SEITE = {
    "id": "befehle",
    "gruppe": "ref",
    "titel": T("Every command"),
    "kurz": T("What each command of the Events list does, every camera command, every macro and every SET_ field - with the value it expects."),
    "bloecke": [
        ("p", T("Commands are inserted from the **{{GroupEvents}}** list (double-click, drag or <kbd>Insert</kbd>) and changed in the **{{EditorTitle}}** (double-click the line). Hover over a command in the list to read the help text the game's designers wrote for it. How to put them together: [[skripte]].")),
        ("h2", "liste", T("The commands of the Events list")),
        ("p", T("Each command has its own page with pictures of the editor and of all three views: [[cmd]].")),
        ("table", [T("Command"), T("What it does"), T("Example")], [_zeile(_verlinkt(b[0]), *b[1:]) for b in BEFEHLE]),
        ("note", T("A command marked with `*` in its help text (for example `SET_NAVGOAL`) finishes its task only when the action is done - that is what lets `dowait` wait for it.")),
        ("h2", "kamera", T("camera ( ... ) - the camera commands")),
        ("p", T("The first value of `camera` chooses the command. behaved shows every one of them in the timeline under **{{TlKamera}}** and plays it in the view. Times are in milliseconds.")),
        ("table", [T("Command"), T("Values"), T("What it does (help text of the game)")], [_zeile("`%s`" % n, w or "-", d) for n, w, d in _kamera()]),
        ("h2", "makros", T("Macros")),
        ("p", T("At the end of the **{{GroupEvents}}** list. A macro inserts several `set` commands at once; **{{TreeFoldMacros}}** shows it as one line.")),
        ("table", [T("Macro"), T("What it does"), T("Commands")], [_zeile("`%s`" % n, d, str(k)) for n, d, k in _makros()]),
        ("h2", "set", T("set ( SET_... ) - every field")),
        ("p", T("All fields `set` can change, grouped as in the game's own list (`Q3_Interface.h`). The description is the one the editor shows - Raven's own words. Type part of a name into the search box of this guide to find a field.")),
        ("table", [T("Group"), T("Field"), T("Value"), T("What it does")], [_zeile(g, "`%s`" % n, w, d) for g, n, w, d in _set_felder()]),
        ("h2", "ausdruecke", T("Expressions: get, tag, random")),
        ("p", T("Instead of a fixed value, a field can hold an expression. In the editor, **{{EditorHelper}}** builds them:")),
        ("list", [
            T("**{{EditorGet}}** - `get ( FLOAT, \"name\" )` reads a variable declared with `declare`."),
            T("**{{EditorTag}}** - `tag ( \"cam_door\", ORIGIN )` takes the position (or `ANGLES`) of a `ref_tag` on the map. That is how cameras stand on marks."),
            T("**{{EditorRnd}}** - `random ( 1000, 3000 )` a random number between two values, for example a wait."),
        ]),
    ],
}

SEITEN = [SEITE]
