"""Die Skript-Tutorials in den Ansichten Graph und Tracks (rc601).

shank: "ich brauche mehr Tutorials, wie das im Graph-Modus und Timeline-Modus
im Event ablaeuft - am liebsten, dass man wie bei Language auch fuer Classic,
Graph und Timeline umschalten kann, dass jedes Tutorial in jedem Modus
erstellt werden kann". Die Classic-Schritte stehen in inhalt_schritte.py; hier
die beiden anderen Fassungen. anwenden() in inhalt_schritte.py setzt sie als
Baustein ("modus", ...) zusammen - der Umschalter oben auf der Seite waehlt.

Bilder: vg_* (Graph) und vt_* (Tracks) aus Abschnitt 10h des Selbsttests;
wo der Schritt in jeder Ansicht gleich aussieht (der Editor, Speichern),
dasselbe Bild wie in Classic.
"""
from inhalt_basis import T


def S(text, bild, unter, legende=None):
    if legende:
        return (T(text), bild, T(unter), [(T(n), T(d)) for n, d in legende])
    return (T(text), bild, T(unter))


GRAPH_LISTE = ("**Where the commands are.** In **{{FlowGraph}}**, click a script node (the open script is selected at the start). "
               "On the right, under **{{GraphCommands}}**, every command of that script is listed - blocks indented. "
               "Click a line to select it, double-click to edit it, right-click for the menu.")
TRACKS_LISTE = ("**Where the commands are.** **{{FlowTracks}}** shows the script over time: the camera, one row per figure, the waits. "
                "On the right, **{{TracksList}}** lists every command - also those that take no time, such as `affect`, `task` or `declare`.")

MODI = {
    # ------------------------------------------------------------------ neues Skript
    "k-neues-skript": {
        "graph": [
            S("**Switch to Graph.** Click **{{FlowGraph}}** above the script area. The graph shows the mission: the player start, the scripts and the entities that start them.",
              "vg_umschalten", "The view switch above the script area: Graph."),
            S("**Drag affect onto free space.** Drag `affect` from the **{{GroupEvents}}** list (1) onto an empty spot of the graph (2). A new script is created in the mission folder, with `affect` as its first command, and opens in its own tab.",
              "vg_neu_ziehen", "1: affect in the Events list. 2: free space in the graph."),
            S("**The new node.** The new script appears as a node where you dropped it, and it is selected. On the right, **{{GraphCommands}}** shows its one line: `affect`.",
              "vg_neu_knoten", "1: the new script node. 2: its commands on the right."),
            S("**Name the figure.** Double-click the `affect` line on the right. The **{{EditorTitle}}** opens - the same as in Classic. Type the name of the figure into the first field and press **{{EditorOk}}**.",
              "s_neu_editor", "The event editor for affect: the name field and Ok."),
            S("**Add the next command into the block.** Click the `affect` line (1), then double-click `wait` in the list (2). Because `affect` is an open block, `wait` goes inside it.",
              "vg_neu_wait", "1: the selected affect line. 2: wait in the Events list."),
            S("**Check the result.** `wait` stands indented under `affect` - it is inside the block. The node counts the commands of the script.",
              "vg_neu_ergebnis", "wait inside the affect block, in the list on the right."),
            S("**Save.** **{{FileSave}}** in the action column (or <kbd>Ctrl</kbd>+<kbd>S</kbd>). The script already has its place in the mission folder.",
              "s_neu_speichern", "Save in the action column."),
        ],
        "tracks": [
            S("**Open an empty tab and switch to Tracks.** Click **+** at the end of the tab bar, then **{{FlowTracks}}** above the script area. The timeline says that nothing is on it yet - and on the right, **{{TracksList}}** says *No commands yet*.",
              "vt_neu_leer", "1: Tracks. 2: the empty command list on the right."),
            S("**Drag affect into the list.** Drag `affect` from the **{{GroupEvents}}** list (1) into the command list on the right (2). An `affect` block has no time of its own, so it shows in the list, not yet in the timeline.",
              "vt_neu_ziehen", "1: affect in the Events list. 2: the command list."),
            S("**Name the figure.** Double-click the `affect` line in the list. The **{{EditorTitle}}** opens: type the name of the figure into the first field and press **{{EditorOk}}**.",
              "s_neu_editor", "The event editor for affect: the name field and Ok."),
            S("**Add a walk into the block.** Click the `affect` line (1), then double-click `set` in the **{{GroupEvents}}** list (2) - it goes inside the open block. In the editor choose `SET_NAVGOAL` and the walk target `nav_kyle`.",
              "vt_neu_wait", "1: the affect line. 2: set in the Events list."),
            S("**Now it is on the timeline.** The figure got its own row, and the walk is a block on it - as long as the figure needs to get there. Commands without time (`affect`, the block itself) stay in the list.",
              "vt_neu_ergebnis", "Kyle's row with the walk, and the list on the right."),
            S("**Save.** **{{FileSave}}** in the action column (or <kbd>Ctrl</kbd>+<kbd>S</kbd>). The first time, behaved asks for a name.",
              "s_neu_speichern", "Save in the action column."),
        ],
    },
    # ------------------------------------------------------------------ einfuegen
    "k-befehl-einfuegen": {
        "graph": [
            S(GRAPH_LISTE, "vg_liste", "The open script's node (1) and its commands (2)."),
            S("**Double-click in the Events list.** Click a line on the right (1), then double-click a command in the **{{GroupEvents}}** list (2). It is inserted after that line - into the block, if the line is an open block.",
              "vg_ein_doppel", "1: the selected line. 2: the command in the Events list."),
            S("**Drag onto a line.** Drag a command from the **{{GroupEvents}}** list (1) onto a line of **{{GraphCommands}}** (2): it lands right after that line.",
              "vg_ein_ziehen", "1: the command. 2: the line it goes after."),
            S("**Drag onto the node.** Dropped on a script node in the graph (2), the command goes to the end of that script. Dropped on free space, it starts a new script.",
              "vg_ein_knoten", "1: the command. 2: the script node - the command goes to its end."),
            S("**Right-click.** Right-click a line on the right: the submenus **{{GroupEvents}}** and **Macros** insert after that line - the same menu as in Classic.",
              "vg_ein_kontext", "The menu of a line in the Graph, with its Events submenu."),
        ],
        "tracks": [
            S(TRACKS_LISTE, "vt_liste", "1: the rows of the timeline. 2: the command list."),
            S("**Double-click in the Events list.** Click a block or a line of the list (1), then double-click a command in the **{{GroupEvents}}** list (2). It goes right after the selected command.",
              "vt_ein_doppel", "1: the selected block. 2: the command in the Events list."),
            S("**Drag into a row at a moment.** Drag a command from the **{{GroupEvents}}** list (1) into a row of the timeline (2). A yellow line shows the moment. The command is placed after the last command of that row that starts before the line - in a figure's row, inside that figure's `affect` block.",
              "vt_ein_spur", "1: the command. 2: the moment in Kyle's row."),
            S("**Drag into the list.** For commands without time - an `affect`, a `task`, a `declare` - drag into the list on the right (2): after the line you drop on, or at the end on free space.",
              "vt_ein_liste", "1: the command. 2: the line in the command list."),
            S("**Right-click.** Right-click a block (or a line of the list): **{{GroupEvents}}** and **Macros** insert after it.",
              "vt_ein_kontext", "The menu of a block, with its Events submenu."),
        ],
    },
    # ------------------------------------------------------------------ Werte
    "k-werte-bearbeiten": {
        "graph": [
            S("**Open the editor.** In **{{FlowGraph}}**, double-click a line under **{{GraphCommands}}** (or select it and choose **Edit...** from its right-click menu). The **{{EditorTitle}}** is the same in every view.",
              "vg_wert_doppel", "Double-click a line of the list to edit it."),
            S("**Fields for values.** Numbers, vectors and text are typed into their fields. A vector like a position has three numbers separated by spaces.",
              "s_wert_felder", "The value fields of camera ( MOVE ): a position and a time."),
            S("**Fields with fixed choices.** Fields like `SET_TYPES` or `true` / `false` are lists you can also type into. A value that is not in the list is allowed, but marked **unknown**.",
              "s_wert_liste", "The two list fields of set ( SET_WALKING, true )."),
            S("**Expressions and free text.** **{{EditorHelper}}** builds `get`, `tag` and `random`; **{{EditorExpr}}** turns a field into free text.",
              "s_wert_helper", "Helper next to a field of the camera command."),
            S("**Done.** **{{EditorOk}}** writes the values, **{{EditorCancel}}** leaves everything as it was. The line on the right shows the new values at once.",
              "s_wert_ok", "Ok and Cancel."),
        ],
        "tracks": [
            S("**Open the editor.** In **{{FlowTracks}}**, double-click a block - or a line of the command list. The **{{EditorTitle}}** is the same in every view.",
              "vt_wert_doppel", "Double-click a block to edit its command."),
            S("**Change a time by dragging.** Drag the right edge of a block: a `wait`, a camera move or a walk gets longer or shorter. The value in the script changes with it.",
              "vt_wert_kante", "The right edge of the wait block."),
            S("**Fields for values.** Numbers, vectors and text are typed into their fields.",
              "s_wert_felder", "The value fields of camera ( MOVE ): a position and a time."),
            S("**Fields with fixed choices.** Lists you can also type into; an unknown value is allowed but marked.",
              "s_wert_liste", "The two list fields of set ( SET_WALKING, true )."),
            S("**Done.** **{{EditorOk}}** writes the values; the block moves or changes its length at once.",
              "s_wert_ok", "Ok and Cancel."),
        ],
    },
    # ------------------------------------------------------------------ ordnen
    "k-zeilen-ordnen": {
        "graph": [
            S("**Select.** Click a line under **{{GraphCommands}}**. The action column on the right works on it, as in Classic.",
              "vg_ord_wahl", "The selected line."),
            S("**Right-click for the rest.** The menu of a line has everything: **{{ActEdit}}**, **{{ActClone}}**, **{{ActCopy}}**, **{{ActCut}}**, **{{ActPaste}}**, **{{MoveUp}}**, **{{MoveDown}}**, **{{ActRem}}** and **{{ActDelete}}**.",
              "vg_ord_menue", "The menu of a line in the Graph.",
              [("Edit...", "Opens the event editor."), ("Duplicate", "A copy right below."), ("Copy / Cut / Paste", "Through the clipboard - also into another script."),
               ("Move up / Move down", "One line up or down, inside its block."), ("REM", "Switches the line off (comment) or on again."), ("Delete", "Removes the line.")]),
            S("**Or the action column.** **{{ActMoveUp}}** / **{{ActMoveDown}}**, **{{ActClone}}**, **{{ActCopy}}**, **{{ActPaste}}** and **{{ActRem}}** on the right do the same for the selected line.",
              "s_verschieben", "Move up and Move down in the action column."),
        ],
        "tracks": [
            S("**Select.** Click a block in the timeline - or a line of the command list.",
              "vt_ord_wahl", "The selected block."),
            S("**Right-click for the rest.** The menu of a block has **{{ActEdit}}**, **{{ActClone}}**, **{{ActCopy}}**, **{{ActCut}}**, **{{ActPaste}}**, **{{MoveUp}}**, **{{MoveDown}}**, **{{ActRem}}** and **{{ActDelete}}**.",
              "vt_ord_menue", "The menu of a block in Tracks.",
              [("Edit...", "Opens the event editor."), ("Duplicate", "A copy right after it - on the timeline it follows the first."), ("Copy / Cut / Paste", "Through the clipboard."),
               ("Move up / Move down", "One line up or down in the script - that changes the order in time."), ("REM", "Switches the command off or on."), ("Delete", "Removes it.")]),
            S("**Moving changes the time.** Moving a `wait` up or down shifts everything after it - watch the blocks move.",
              "vt_ord_nachher", "After Move up: the blocks after it start earlier."),
        ],
    },
    # ------------------------------------------------------------------ Tasks
    "k-tasks": {
        "graph": [
            S(GRAPH_LISTE, "vg_liste", "The open script's node (1) and its commands (2)."),
            S("**Build the task.** Click the `affect` line, double-click `task` in the **{{GroupEvents}}** list and name it `walk` in the editor. Then click the `task` line and double-click `set` twice - for `SET_WALKING` and `SET_NAVGOAL`.",
              "vg_task_liste", "The task walk with the walk inside, in the list."),
            S("**dowait after it.** Drag `dowait` from the **{{GroupEvents}}** list onto the `task` line: a command dropped on a line goes *after* it - after the whole block. Name the task `walk` in the editor.",
              "vg_task_dowait", "dowait walk after the task."),
            S("**Check it in Tracks.** Switch to **{{FlowTracks}}**: what follows the `dowait` starts where the walk ends.",
              "vt_task_spur", "Kyle's row: the look starts after the walk."),
        ],
        "tracks": [
            S(TRACKS_LISTE, "vt_liste", "1: the rows of the timeline. 2: the command list."),
            S("**Build the task in the list.** A `task` has no time of its own - build it in the command list: click the `affect` line, double-click `task`, name it `walk`; click the `task` line and double-click `set` twice for `SET_WALKING` and `SET_NAVGOAL`.",
              "vt_task_liste", "The task walk in the command list."),
            S("**dowait.** Drag `dowait` onto the `task` line of the list - it goes after the block - and name the task `walk`. In the figure's row, everything after it now starts where the walk ends.",
              "vt_task_spur", "Kyle's row: the look starts after the walk."),
            S("**Two at once.** With `do` instead of `dowait`, the next command starts at once - its block moves to the start of the walk.",
              "vt_task_do", "With do: the look starts with the walk."),
        ],
    },
    # ------------------------------------------------------------------ Signale
    "k-signale": {
        "graph": [
            S("**Both scripts in the graph.** The graph shows which entity starts which script. Click the script that should wait - here the intro - and insert `waitsignal` into its list (select a line, double-click `waitsignal`). Name the signal `door_open`.",
              "vg_sig_intro", "The intro node and its waitsignal."),
            S("**The other script.** Click the other script's node. If it is not open, **{{GraphEditHere}}** opens it; then insert `signal ( \"door_open\" )` in its list.",
              "vg_sig_andere", "The other script: Edit here opens it."),
            S("**Check the names.** Both must be spelled exactly the same - only one script can wait for a given name.",
              "s_sig_beide", "The same name on both sides."),
        ],
        "tracks": [
            S("**waitsignal in Tracks.** Insert `waitsignal` into the script that waits (double-click in the **{{GroupEvents}}** list after selecting a block). It shows as a short block in the **Script** row.",
              "vt_sig_spur", "waitsignal in the Script row."),
            S("**The timeline cannot know the wait.** How long a `waitsignal` waits depends on the game - everything after it is placed as if the signal came at once.",
              "vt_sig_spur", "Everything after the waitsignal follows it directly."),
            S("**signal in the other script.** Open the other script's tab and insert `signal ( \"door_open\" )` there - in its list or after one of its blocks.",
              "s_sig_beide", "The same name on both sides."),
        ],
    },
    # ------------------------------------------------------------------ Makros
    "k-makros": {
        "graph": [
            S("**Find them.** The macros are at the end of the **{{GroupEvents}}** list.",
              "s_makro_liste", "standOnly and the other macros at the end of the Events list."),
            S("**Drag one into the list.** Drag a macro (1) onto a line under **{{GraphCommands}}** (2). It shows as one line, `$ standOnly` - its `set` commands are inside.",
              "vg_makro", "1: the macro. 2: the line it goes after."),
            S("**Or right-click.** The menu of a line has a **Macros** submenu.",
              "vg_ein_kontext", "The menu of a line, with its Macros submenu."),
        ],
        "tracks": [
            S("**Find them.** The macros are at the end of the **{{GroupEvents}}** list.",
              "s_makro_liste", "standOnly and the other macros at the end of the Events list."),
            S("**Drag one into the list.** A macro sets values and takes no time, so it goes into the command list on the right: drag it (1) onto a line (2).",
              "vt_makro", "1: the macro. 2: the line in the command list."),
            S("**Or right-click.** The menu of a block or a line has a **Macros** submenu.",
              "vt_ein_kontext", "The menu of a block, with its Macros submenu."),
        ],
    },
    # ------------------------------------------------------------------ Variablen
    "k-variablen": {
        "graph": [
            S("**Declare.** Select the first line under **{{GraphCommands}}** and double-click `declare` in the **{{GroupEvents}}** list. In the editor choose `FLOAT` and type the name `seen`.",
              "vg_var_liste", "declare, if and else in the list of the Graph."),
            S("**Ask with if.** Insert `if` the same way. In the editor, **{{EditorHelper}}** → **{{EditorGet}}** builds `get ( FLOAT, \"seen\" )`; comparison `=`, value `0`.",
              "k_var_if", "The event editor for if: the variable and the value."),
            S("**Fill the blocks.** Select the `if` line and insert the scene's commands - they go inside the open block. `else` right after the `if` runs when the scene was seen before; `set ( \"seen\", \"1\" )` at the end of the `if` remembers it.",
              "vg_var_liste", "The if and else blocks, indented in the list."),
        ],
        "tracks": [
            S("**Build it in the list.** `declare`, `if` and `else` take no time - build them in the command list on the right, the same way as in the Graph.",
              "vt_var_liste", "declare, if and else in the command list of Tracks."),
            S("**Ask with if.** In the editor, **{{EditorHelper}}** → **{{EditorGet}}** builds `get ( FLOAT, \"seen\" )`; comparison `=`, value `0`.",
              "k_var_if", "The event editor for if: the variable and the value."),
            S("**What the timeline shows.** The timeline plays the commands inside the `if` as if it were true - it cannot know the value in the game.",
              "vt_var_liste", "The commands inside the if on the timeline."),
        ],
    },
}
