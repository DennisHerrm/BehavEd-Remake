"""Ausfuehrliche Schrittfolgen fuer die Tutorials - jeder Schritt mit Bild.

shank: "bei vielen Sachen koennte man viel mehr erklaeren mit mehr Bildern,
mach es ausfuehrlicher" und "da fehlt ein Bild, damit man weiss, was man
machen muss". Diese Schritte ersetzen beim Bau die Schritte der Seite mit
derselben id (inhalt.py ruft anwenden() auf). Die Bilder entstehen im
Selbsttest (Abschnitt 10e und folgende in selbsttest.cpp), jeder mit einem
gelben Rahmen auf dem, was man anklickt.
Englisch zuerst.
"""
from inhalt_basis import T


def S(text, bild, unter):
    return (T(text), bild, T(unter))


SCHRITTE = {
    # ------------------------------------------------------------- Skripte
    "k-neues-skript": [
        S("**Open an empty tab.** Click the **+** at the end of the tab bar (or **{{MenuFile}} → {{FileNew}}**). A new tab `unnamed.txt` opens with an empty script. The mode bar on the left must be on **{{ViewEvents}}** - then the **{{GroupEvents}}** list with every command is on the left.",
          "s_neu_plus", "The + at the end of the tab bar opens an empty script."),
        S("**Find affect in the list.** The **{{GroupEvents}}** list shows every command of the game with its values. `affect` is near the top. Hover over it to read what it does.",
          "s_neu_affect_liste", "affect in the Events list."),
        S("**Double-click it.** `affect` appears in the script tree in the middle. It is already open (the box in front shows a minus), because a block holds other commands.",
          "s_neu_affect_baum", "The new affect block in the tree."),
        S("**Name the figure.** Double-click the new line (or select it and press <kbd>Enter</kbd>). The **{{EditorTitle}}** opens. Type the name of the figure into the first field - with a map loaded, the small arrow next to it lists every name of the map. Leave `FLUSH` in the second field and press **{{EditorOk}}**.",
          "s_neu_editor", "The event editor for affect: the name field and Ok."),
        S("**Select the block, then pick the next command.** Click the `affect` line once (1). Then double-click `wait` in the list (2). Because the selected block is open, the command goes to the start of the block.",
          "s_neu_wait_liste", "1: the selected affect block. 2: wait in the list."),
        S("**Check the result.** `wait` now sits inside the block, indented under `affect`. Commands you insert after it follow it, in order, inside the same block.",
          "s_neu_ergebnis", "wait inside the affect block."),
        S("**Save.** Press **{{FileSave}}** in the action column on the right (or <kbd>Ctrl</kbd>+<kbd>S</kbd>). The first time, behaved asks for a name - script names are lower case without spaces, for example `my_first.txt`.",
          "s_neu_speichern", "Save in the action column."),
    ],
    "k-befehl-einfuegen": [
        S("**Double-click.** Select the line after which the command should come (1), then double-click the command in the **{{GroupEvents}}** list (2). The new line appears right below the selected one - here a `wait` after `camera ( MOVE )`. That is how the original BehavEd worked.",
          "s_ein_doppel", "1: the selected line. 2: the command in the list."),
        S("**Insert key.** Click a command in the list once - it stays highlighted (1). Select a line in the tree (2) and press <kbd>Insert</kbd>. Handy when you add the same command several times: select, <kbd>Insert</kbd>, select, <kbd>Insert</kbd> ...",
          "s_ein_einfg", "1: the command stays selected in the list. 2: where it goes."),
        S("**Drag.** Drag a command from the list (1) onto a line of the tree (2). While you drag, a line shows where it lands: above a line, below it, or - on the middle of an open block - inside the block.",
          "s_ein_ziehen", "Drag from the list (1) onto the tree (2)."),
        S("**Right-click.** Right-click a line in the tree: the submenus **{{GroupEvents}}** and **Macros** list every command; the one you click is inserted after that line - or into the block, if you right-clicked an open block.",
          "s_ein_kontext", "The context menu of a line with its Events submenu."),
    ],
    "k-werte-bearbeiten": [
        S("**Open the editor.** Double-click a line in the tree or select it and press <kbd>Enter</kbd>. The help text of the command stands on top; below it, every value has its own field.",
          "h_editor", "The event editor for a camera command."),
        S("**Fields for values.** Numbers, vectors and text are typed into their fields. A vector like a position has three numbers separated by spaces.",
          "s_wert_felder", "The value fields of camera ( MOVE ): a position and a time."),
        S("**Fields with fixed choices.** Fields like `SET_TYPES` or `true` / `false` are lists you can also type into - start typing to filter the list. A value that is not in the list is allowed (mods bring their own), but it is marked **unknown**: check it for a typo.",
          "s_wert_liste", "The two list fields of set ( SET_WALKING, true )."),
        S("**Files.** File fields have **{{EditorBrowse}}** to pick a file of the game and **{{EditorPlay}}** to play a sound or open a script. With a map loaded, an arrow next to name fields picks a name from the map.",
          "s_wert_datei", "A sound command: browse and play."),
        S("**Expressions.** Some fields have **{{EditorHelper}}**: it builds an expression for you - **{{EditorGet}}** reads a variable (`get`), **{{EditorTag}}** the place or angles of a `ref_tag` (`tag`), **{{EditorRnd}}** a random number (`random`).",
          "s_wert_helper", "Helper next to a field of the camera command."),
        S("**Free text.** Other fields have **{{EditorExpr}}**: it turns the field into free text, for anything the lists do not offer. **{{EditorRevert}}** goes back to a plain value.",
          "s_wert_expr", "Expr! next to the fields of affect."),
        S("**Done.** **{{EditorOk}}** (<kbd>Enter</kbd>) writes the values into the script, **{{EditorCancel}}** (<kbd>Esc</kbd>) leaves everything as it was. Under each field you see the value it had before (**{{EditorWas}}**).",
          "s_wert_ok", "Ok and Cancel."),
    ],
    "k-zeilen-ordnen": [
        S("**Select.** Click a line in the tree. <kbd>Ctrl</kbd>+click adds more lines, <kbd>Shift</kbd>+click selects everything between. Every button of the action column works on all selected lines at once.",
          "s_auswahl", "Two selected lines."),
        S("**Move.** Drag the selection to a new place - or press **{{ActMoveUp}}** / **{{ActMoveDown}}** in the action column (<kbd>Alt</kbd>+<kbd>Up</kbd> / <kbd>Alt</kbd>+<kbd>Down</kbd>) to move it one line. It stays inside its block. A block moves with everything inside it.",
          "s_verschieben", "Move up and Move down."),
        S("**Duplicate.** **{{ActClone}}** (<kbd>Space</kbd>) inserts a copy of the selection right below - the quickest way to a second camera shot or a second walk.",
          "s_clone", "Duplicate."),
        S("**Copy between scripts.** **{{ActCopy}}** (<kbd>Ctrl</kbd>+<kbd>C</kbd>) copies the selection. Switch to another tab, select a line and press **{{ActPaste}}** (<kbd>Ctrl</kbd>+<kbd>V</kbd>): the commands are inserted below that line.",
          "s_copy", "Copy and Paste."),
        S("**Switch off without deleting.** **{{ActRem}}** (<kbd>Backspace</kbd>) turns the selected lines into comments: they stay in the script, but the game skips them. Press it again (or <kbd>Ctrl</kbd>+<kbd>Backspace</kbd>) to switch them back on.",
          "s_rem", "REM: the two lines are now comments."),
    ],
    "k-rueckgaengig": [
        S("**Undo and redo.** <kbd>Ctrl</kbd>+<kbd>Z</kbd> undoes the last change, <kbd>Ctrl</kbd>+<kbd>Y</kbd> redoes it. In the **{{MenuEdit}}** menu the entry names the step - here *Undo: edit*.",
          "s_undo_menue", "The Edit menu names the step that Undo takes back."),
        S("**See the whole history.** Right-click the **Undo** button in the action column (or **{{MenuEdit}} → {{UndoListMenu}}**). The list shows every step, newest at the top.",
          "k_undo", "The undo history."),
        S("**Go back several steps.** Click a step in the list and press **{{UndoUpToHere}}**: that step and all newer ones are undone.",
          "s_undo_bis", "Undo up to here."),
        S("**Undo one old step only.** Click a step and press **{{UndoOnlyThis}}**: only that change is undone, everything you did after it stays.",
          "k_undo", "Undo only this step."),
    ],
    "k-suchen-ersetzen": [
        S("**Open all scripts.** For a mission, **{{MenuFile}} → {{LoadMission}}** opens every script in its own tab - then a search can reach all of them.",
          "s_lade_menue", "Load mission in the File menu."),
        S("**Open Find.** Press **{{ActFind}}** in the action column (<kbd>Ctrl</kbd>+<kbd>F</kbd>). The window stays open while you work in the tree. Its fields:",
          "k_suchen", "The Find window."),
        S("**Search a value.** Type the old name into **{{FindContains}}**. **{{FindNextBtn}}** (<kbd>F3</kbd>) and **{{FindPrevBtn}}** (<kbd>Shift</kbd>+<kbd>F3</kbd>) jump from hit to hit. Tick **{{FindWhole}}** so that `kyle` does not also hit `kyle2`.",
          "s_find_wert", "Item containing, the buttons to step through the hits, and Whole-string match only."),
        S("**Search a command.** **{{FindNamed}}** finds commands by name, for example every `camera`. Both fields together find only commands that match both.",
          "s_find_name", "Item called."),
        S("**Replace.** Type the new name into **{{FindReplaceWith}}** and press **{{FindReplaceAllDocs}}**. Every open script is changed; each one gets its own undo step.",
          "s_find_ersetzen", "Replace with and Replace All in All Opened Documents."),
    ],
    "k-lesezeichen": [
        S("**Set a bookmark.** Select a line and press <kbd>Ctrl</kbd>+<kbd>F2</kbd> - or **{{MenuEdit}} → {{MenuBookmarks}} → {{ActBookmarkToggle}}**.",
          "s_lese_menue", "Toggle bookmark in the Edit menu."),
        S("**See it.** A mark appears in the margin next to the line. Clicking the margin sets or removes a bookmark too.",
          "s_lese_gesetzt", "The bookmarked line."),
        S("**Jump.** <kbd>F2</kbd> goes to the next bookmark, <kbd>Shift</kbd>+<kbd>F2</kbd> to the previous one. Bookmarks are kept per file.",
          "s_lese_weiter", "Next and Previous bookmark."),
        S("**Split the view.** Open a second script, then **{{MenuFile}} → {{SplitToggle}} → 2**. The second script shows beside the first.",
          "k_geteilt", "Two scripts side by side."),
        S("**Copy across.** The right side is read-only, but you can drag lines from it into your script on the left - they are copied. To edit the right one, click its tab.",
          "s_sig_beide", "Left: your script. Right: the other one, read-only."),
    ],
    "k-uebersetzen": [
        S("**Compile.** Press **{{Compile}}** at the bottom of the action column. It saves the `.txt` and writes the `.ibi` the game runs next to it. When it worked, the button flashes green.",
          "t1_6", "The Compile button."),
        S("**When it fails.** The button flashes red, and the first wrong line is selected in the tree - here an `if` with the comparison `~`, which ICARUS does not know.",
          "s_comp_fehler", "Compile failed: the button and the selected wrong line."),
        S("**Read the messages.** Click the counter in the status line at the bottom: the **{{MessagesTitle}}** window lists every message with its line and what is wrong.",
          "s_comp_meldung", "The counters in the status line open the Messages window."),
        S("**Fix and compile again.** Double-click the wrong line, correct the value - here the comparison: `=`, `<`, `>` or `!` - press **{{EditorOk}}** and **{{Compile}}** again.",
          "s_comp_editor", "The wrong value in the event editor."),
    ],
    "k-tasks": [
        S("**Why.** An `affect` block runs beside the script - the script does not wait for it. That is why Kyle and Rosh, each in his own block, already walk at the same time.",
          "s_task_affects", "Two affect blocks: they run side by side."),
        S("**The problem.** Inside a block, `set ( SET_NAVGOAL )` only *starts* the walk; the next command follows at once - the figure would turn or talk while still walking. **The fix:** put the walk into a `task`. Insert `task`, name it `walk`, and move `SET_WALKING` and `SET_NAVGOAL` into it.",
          "s_task_task", "The task walk with the walk inside."),
        S("**dowait.** After the task insert `dowait ( \"walk\" )`. It starts the task and waits until it is done - until the figure has arrived. Everything after it happens when the figure stands.",
          "s_task_dowait", "dowait waits for the walk."),
        S("**Two things at once.** `do` starts a task and goes on at once. Two tasks - `walk` and `look` - started with two `do`, and `wait ( \"walk\" )` later waits for the first one.",
          "s_task_do", "do, do and wait."),
        S("**Check it in the timeline.** Switch to **{{ViewMap}}**, expand the figure's row in the timeline: what follows the `dowait` starts where the walk ends.",
          "s_task_zeit", "Kyle's walk in the timeline."),
    ],
    "k-signale": [
        S("**Where to wait.** In the script that should pause - here the intro - insert `waitsignal` and type a name, for example `door_open`.",
          "s_sig_wait", "waitsignal in the intro (left)."),
        S("**Where to give the signal.** In the other script, at the moment the first should go on, insert `signal ( \"door_open\" )`.",
          "s_sig_signal", "signal in the other script (right)."),
        S("**Check the names.** Both must be spelled exactly the same. Only one script can wait for a signal of a given name. The split view (**{{MenuFile}} → {{SplitToggle}} → 2**) shows both at once.",
          "s_sig_beide", "The same name on both sides."),
        S("**See it in the timeline.** Switch on **{{TlWaits}}** in the timeline: waits become their own blocks. The timeline cannot know how long a `waitsignal` waits in the game - it places everything after it as if the signal came at once.",
          "s_sig_zeit", "Waits as blocks in the timeline."),
    ],
    "k-makros": [
        S("**Find them.** The macros are at the end of the **{{GroupEvents}}** list - scroll down. Each shows how many commands it holds; hover over one to read what it does.",
          "s_makro_liste", "standOnly and the other macros at the end of the Events list."),
        S("**Insert.** Double-click or drag a macro like a command. `standOnly`, for example, makes a figure stand and ignore enemies - three `set` commands in one line.",
          "k_makro", "A macro in the tree."),
        S("**Fold or unfold.** **{{TreeFoldMacros}}** in the action column shows a macro as one line. Switch it off to see and change the single commands.",
          "s_makro_falten", "Fold Macros and a macro in the tree."),
    ],
    "k-variablen": [
        S("**Declare once.** In a script that runs only once - the intro, a spawn script - insert `declare`. In the editor choose `FLOAT` and type a name, for example `seen`. A declared variable starts at 0; a map can hold 16 of them.",
          "s_var_declare", "declare: the type and the name."),
        S("**Ask with if.** In the script that may run more often, insert `if`. On the first value press **{{EditorHelper}}** and build `get ( FLOAT, \"seen\" )` with **{{EditorGet}}**; as comparison `=`, as the other value `0`.",
          "k_var_if", "The event editor for if: the variable and the value."),
        S("**Do it inside, or else.** Put the commands of the scene into the `if` block. An `else` right after it runs when the scene was already seen.",
          "s_var_block", "The if and the else block."),
        S("**Remember.** At the end of the `if` block insert `set` with the variable name `seen` and the value `1` - next time the `if` is false.",
          "s_var_set", "set ( \"seen\", \"1\" ) inside the if."),
        S("**Free it** with `free ( \"seen\" )` when it is no longer needed, so the name is available again.",
          "s_var_free", "free at the end."),
    ],
    "k-ibi": [
        S("**Find it.** **{{MenuFile}} → {{Pk3Browse}}**, choose **{{Pk3Scripts}}** and type part of the name into **{{Pk3Filter}}**, for example `taspir1/intro`.",
          "k_pk3", "The files in the game's archives."),
        S("**Open it.** Double-click the entry: the `.ibi` opens as a script in the current tab - commands, values and blocks, exactly as Raven wrote them. Open an empty tab first (**+**) if you want to keep the script you are working on.",
          "k_ibi", "A compiled script of the game, opened as text."),
        S("**From disk.** **{{MenuFile}} → {{FileOpen}}** opens `.ibi` files from a folder the same way.",
          "s_ibi_menue", "Open in the File menu."),
        S("**Change it.** A script from an archive has no file yet - the first **{{FileSave}}** asks where to put it. To ship a changed version of a game scene, see [[k-szene-aendern]].",
          "s_neu_speichern", "Save in the action column."),
    ],
    "k-pk3": [
        S("**Open the browser.** **{{MenuFile}} → {{Pk3Browse}}**. It lists the files of every game folder you added.",
          "k_pk3", "The Files in .pk3 window."),
        S("**Choose the kind.** **{{Pk3Scripts}}**, **{{Pk3Maps}}** or **{{Pk3Models}}**. The **{{Pk3Archive}}** list narrows it to one `.pk3`.",
          "s_pk3_art", "The kind of file and the archive."),
        S("**Search.** Type into **{{Pk3Filter}}** - part of a name is enough.",
          "s_pk3_filter", "The filter."),
        S("**Open.** Double-click an entry: a script replaces the one in the current tab (behaved asks first if it has unsaved changes), a map loads, a model opens with its skin in the **{{ViewModel}}** tab.",
          "s_pk3_eintrag", "Double-click an entry to open it."),
    ],
    "k-aenderungen": [
        S("**The margin.** Change a value: a coloured bar appears left of the line - orange for changed and not saved, green for changed and saved, blue for back to the original. **{{MenuView}} → {{ViewChangeHistory}}** switches it on or off.",
          "k_rand", "Changed lines in the margin."),
        S("**What was it before?** Hover over the bar: the original line is shown.",
          "s_aend_rand", "The original line over the margin."),
        S("**Back to the original.** Right-click the line → **{{ActRevertOriginal}}**: only this command goes back, everything else stays.",
          "s_aend_kontext", "Revert to original in the context menu."),
        S("**Identical commands.** **{{MenuView}} → {{ViewHighlightSame}}** marks every command that looks exactly like the selected one - handy for a camera that is used several times.",
          "s_aend_gleich", "Highlight identical commands in the View menu."),
    ],
    "k-backup": [
        S("**Automatic copies.** Every save and every compile keeps a copy - the last ten, `backup1.txt` (newest) to `backup10.txt`. **{{MenuFile}} → {{FileBackupFolder}}** opens the folder.",
          "k_dateimenue", "Open backup folder in the File menu."),
        S("**Your own backup.** **{{FileBackup}}** (<kbd>Alt</kbd>+<kbd>B</kbd>) writes `<script>.bak` next to the script - before a big change.",
          "s_backup", "Backup."),
        S("**Restore it.** **{{FileRestore}}** (<kbd>Alt</kbd>+<kbd>R</kbd>) goes back to the `.bak`, after a question.",
          "s_restore", "Restore."),
        S("**Smaller steps.** For the last few changes the undo history is quicker: right-click **Undo** ([[k-rueckgaengig]]).",
          "k_undo", "The undo history."),
    ],
    "k-meldungen": [
        S("**The counters.** The status line at the bottom counts errors and notes. Click a counter to open the **{{MessagesTitle}}** window.",
          "s_meld_zaehler", "The counters at the bottom left."),
        S("**Read them.** Each message names its code and the command. A misspelt value such as `SET_WALKNG` is reported as *not in SET_TYPES (kept unchanged)* - behaved keeps it, because mods bring their own values, but it is far more often a typo.",
          "k_meldung", "V004: a value that is not in its list."),
        S("**Fix and check.** Double-click the line, correct the value in the editor and press **{{EditorOk}}** - the message disappears. Messages never change the script by themselves.",
          "s_meld_editor", "The misspelt value in the editor."),
    ],
}


def anwenden(seiten):
    """Ersetzt die Schritte der Seiten mit eigenen ausfuehrlichen Schritten
    und gibt Schritten des Projekts ohne Bild ihr Bild (inhalt_schritte2)."""
    import inhalt_schritte2
    import inhalt_modi
    alle = dict(SCHRITTE)
    alle.update(inhalt_schritte2.SCHRITTE)
    for s in seiten:
        neu = alle.get(s["id"])
        for i, b in enumerate(s["bloecke"]):
            if b[0] != "steps":
                continue
            if neu and s["id"] in inhalt_modi.MODI:
                # Drei Fassungen: Classic hier, Graph und Tracks aus inhalt_modi.
                fassungen = {"classic": [("steps", neu)]}
                for m, schritte in inhalt_modi.MODI[s["id"]].items():
                    fassungen[m] = [("steps", schritte)]
                s["bloecke"][i] = ("modus", fassungen)
                if s.get("meta"):
                    m = s["meta"]
                    s["meta"] = T(m.get("en") + " · Classic, Graph, Tracks", m.get("de") + " · Classic, Graph, Spuren",
                                  m.get("zh") + " · 经典、图表、轨道", m.get("ja") + " · クラシック・グラフ・トラック")
            elif neu:
                s["bloecke"][i] = ("steps", neu)
            else:
                schritte = list(b[1])
                for n, x in enumerate(schritte, 1):
                    zusatz = inhalt_schritte2.BILDER.get((s["id"], n))
                    if zusatz and not isinstance(x, tuple):
                        schritte[n - 1] = (x, zusatz[0], zusatz[1])
                s["bloecke"][i] = ("steps", schritte)
            break
