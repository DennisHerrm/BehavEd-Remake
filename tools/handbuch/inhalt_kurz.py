"""Kurze Tutorials: je eine kleine Sache, in drei bis sechs Schritten.

shank: "dann brauche ich mehrere Tutorials, wirklich viele und wenn es auch
nur Kleinigkeiten sind, dass Leute verstehen, wie das Programm funktioniert".
Englisch zuerst; die anderen Sprachen folgen (T faellt bis dahin auf
Englisch zurueck).

Jede Seite: id k-..., gruppe tut_*, titel, kurz, meta (Bereich, Schritte),
titelbild, bloecke (meist ein "steps" und ein "Gut zu wissen").
"""
from inhalt_basis import T


def tut(sid, gruppe, titel, kurz, meta, bild, schritte, tipps=None, weiter=None):
    bloecke = [("h2", "schritte", T("Steps", "Schritte", "步骤", "手順")), ("steps", schritte)]
    if tipps:
        bloecke.append(("h2", "tipps", T("Good to know", "Gut zu wissen", "须知", "知っておくと便利")))
        bloecke.append(("list", tipps))
    if weiter:
        bloecke.append(("note", weiter))
    return {"id": sid, "gruppe": gruppe, "titel": titel, "kurz": kurz, "meta": meta,
            "titelbild": bild, "bloecke": bloecke}


# ============================================================ Skripte
SKRIPT = [
    tut("k-neues-skript", "tut_skript",
        T("Write a script from scratch"),
        T("A new empty script, an affect block for one figure and a first command inside it."),
        T("Scripts · five steps"), "k_neu_3",
        [
            (T("**Start an empty script.** Click the **+** at the end of the tab bar (or **{{MenuFile}} → {{FileNew}}**). A tab `unnamed.txt` opens; switch the mode bar to **{{ViewEvents}}**."),
             "k_neu_1", T("A new tab and the Events list.")),
            T("**Insert an affect block.** In the **{{GroupEvents}}** list on the left, double-click `affect`. It appears in the tree, already open, because a block holds commands."),
            (T("**Name the figure.** Double-click the new line (or press <kbd>Enter</kbd>) to open the **{{EditorTitle}}**. Type the name of the figure into the first field - with a map loaded, the arrow next to it lists every name of the map. Leave `FLUSH` and press **{{EditorOk}}**."),
             "k_neu_2", T("The event editor for affect.")),
            T("**Put a command inside.** Click the `affect` line once, then double-click `wait` in the list: because the selected block is open, the command goes to the start of the block. Commands inserted after that follow it, in order."),
            (T("**Save.** **{{FileSave}}** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) asks for a name the first time. Script names are lower case without spaces, for example `my_first.txt`."),
             "k_neu_3", T("The finished little script.")),
        ],
        [
            T("With a closed block selected, a new command goes after the block, not into it."),
            T("Only `affect`, `task`, `if`, `else` and `loop` can hold commands."),
        ]),

    tut("k-befehl-einfuegen", "tut_skript",
        T("Three ways to add a command"),
        T("Double-click, drag and drop, or the Insert key - and how to put a command exactly where you want it."),
        T("Scripts · four steps"), "h_skriptbaum",
        [
            T("**Double-click.** Select the line after which the command should come, then double-click the command in the **{{GroupEvents}}** list. That is how the original BehavEd worked."),
            T("**Insert key.** Click a command in the list once (it stays highlighted), select a line in the tree and press <kbd>Insert</kbd>. Handy when you add the same command several times."),
            (T("**Drag.** Drag the command from the list into the tree. While you drag, a line shows where it lands: above a line, below it, or - on the middle of a block - inside the block."),
             "h_skriptbaum", T("The Events list on the left, the script tree in the middle.")),
            T("**Right-click.** Right-click a line: the submenus **{{GroupEvents}}** and **Macros** insert after that line - or into the block, if you right-clicked an open block."),
        ],
        [
            T("Hover over a command in the list to read what it does."),
            T("Macros at the end of the list add several commands at once, for example `walkOnly`."),
        ]),

    tut("k-werte-bearbeiten", "tut_skript",
        T("Change the values of a command"),
        T("The event editor: lists, numbers, file fields, names from the map - and expressions such as get, tag and random."),
        T("Scripts · five steps"), "h_editor",
        [
            (T("**Open the editor.** Double-click a line or press <kbd>Enter</kbd>. The help text of the command is on top, each value has its own field."),
             "h_editor", T("The event editor for a camera command.")),
            T("**Pick from a list.** Fields with fixed choices are lists you can also type into: start typing to filter. A value that is not in the list is allowed (mods bring their own), but it is marked **unknown** - check for a typo."),
            T("**Files and names.** File fields have **{{EditorBrowse}}** to browse and **{{EditorPlay}}** to play a sound or open a script. With a map loaded, an arrow next to name fields picks a name from the map."),
            T("**An expression instead of a value.** **{{EditorExpr}}** turns a field into free text. **{{EditorHelper}}** builds one for you: **{{EditorGet}}** reads a variable (`get`), **{{EditorTag}}** the place or angles of a `ref_tag` (`tag`), **{{EditorRnd}}** a random number (`random`). **{{EditorRevert}}** goes back to a plain value."),
            T("**Done.** **{{EditorOk}}** (<kbd>Enter</kbd>) writes the values, **{{EditorCancel}}** (<kbd>Esc</kbd>) leaves everything as it was. Under each field you see the value it had before (**{{EditorWas}}**)."),
        ]),

    tut("k-zeilen-ordnen", "tut_skript",
        T("Move, copy and comment out lines"),
        T("Rearrange a script: select several lines, drag them, duplicate them, and switch lines off without deleting them."),
        T("Scripts · five steps"), "h_skriptbaum",
        [
            T("**Select.** Click a line; <kbd>Ctrl</kbd>+click adds lines, <kbd>Shift</kbd>+click selects a range. Every action works on everything selected."),
            T("**Move.** Drag the selection to a new place, or press <kbd>Alt</kbd>+<kbd>Up</kbd> / <kbd>Alt</kbd>+<kbd>Down</kbd> to move it one line - it stays inside its block. A block moves with everything inside it."),
            T("**Duplicate.** **{{ActClone}}** (<kbd>Space</kbd>) inserts a copy right below - the quickest way to a second camera shot or a second walk."),
            T("**Copy between scripts.** **{{ActCopy}}** (<kbd>Ctrl</kbd>+<kbd>C</kbd>), switch tabs, select a line, **{{ActPaste}}** (<kbd>Ctrl</kbd>+<kbd>V</kbd>). Pasting puts the commands below the selected line."),
            T("**Switch off without deleting.** **{{ActRem}}** (<kbd>Backspace</kbd>) turns the lines into comments; the game skips them. Press it again (or <kbd>Ctrl</kbd>+<kbd>Backspace</kbd>) to switch them back on."),
        ],
        [T("**{{ActDelete}}** (<kbd>Delete</kbd>) removes lines for good - but <kbd>Ctrl</kbd>+<kbd>Z</kbd> brings them back.")]),

    tut("k-rueckgaengig", "tut_skript",
        T("Undo anything - even one step from the middle"),
        T("Undo and redo, the full history, and undoing one single old step while keeping everything after it."),
        T("Scripts · four steps"), "k_undo",
        [
            T("**Undo and redo.** <kbd>Ctrl</kbd>+<kbd>Z</kbd> undoes the last change, <kbd>Ctrl</kbd>+<kbd>Y</kbd> redoes it. The **Undo** entry in the **{{MenuEdit}}** menu names the step, for example *Undo: delete*."),
            (T("**See the history.** Right-click the **Undo** button in the action column (or **{{MenuEdit}} → {{UndoListMenu}}**). The list shows every step, newest at the top."),
             "k_undo", T("The undo history.")),
            T("**Go back several steps.** Click a step and press **{{UndoUpToHere}}**: that step and all newer ones are undone."),
            T("**Undo one old step only.** Click a step and press **{{UndoOnlyThis}}**: only that change is undone, everything you did after it stays."),
        ],
        [
            T("Entities on the map have their own undo: <kbd>Ctrl</kbd>+<kbd>Z</kbd> acts on them while the mouse is over the map or the entity list."),
            T("To take back a command to the state it had when the file was opened, right-click it → **{{ActRevertOriginal}}**."),
        ]),

    tut("k-suchen-ersetzen", "tut_skript",
        T("Find and replace in all open scripts"),
        T("Rename a figure in every script of a mission at once - with the Find window."),
        T("Scripts · five steps"), "k_suchen",
        [
            T("**Open all scripts.** For a mission, **{{LoadMission}}** opens every script in its own tab."),
            (T("**Open Find.** **{{ActFind}}** (<kbd>Ctrl</kbd>+<kbd>F</kbd>). The window stays open while you work."),
             "k_suchen", T("The Find window.")),
            T("**Search a value.** Type the old name into **{{FindContains}}**. **{{FindNextBtn}}** (<kbd>F3</kbd>) and **{{FindPrevBtn}}** (<kbd>Shift</kbd>+<kbd>F3</kbd>) step through the hits. Tick **{{FindWhole}}** so that `kyle` does not also hit `kyle2`."),
            T("**Search a command.** **{{FindNamed}}** finds commands by name, for example every `camera`. Both fields together find only commands that match both."),
            T("**Replace.** Type the new name into **{{FindReplaceWith}}** and press **{{FindReplaceAllDocs}}**. Each script gets one undo step."),
        ],
        [T("Right-click a line → **{{ActFindSimilar}}** fills the Find window for you: every command of this kind, or every command with one of its values.")]),

    tut("k-lesezeichen", "tut_skript",
        T("Bookmarks and the split view"),
        T("Jump between places in a long script, and compare two scripts side by side."),
        T("Scripts · four steps"), "k_geteilt",
        [
            T("**Set a bookmark.** Select a line and press <kbd>Ctrl</kbd>+<kbd>F2</kbd> (**{{MenuEdit}} → {{MenuBookmarks}}**). A mark appears next to the line."),
            T("**Jump.** <kbd>F2</kbd> goes to the next bookmark, <kbd>Shift</kbd>+<kbd>F2</kbd> to the previous one. Bookmarks are kept per file."),
            (T("**Split the view.** Open a second script, then **{{MenuFile}} → {{SplitToggle}} → 2**. The second script shows beside the first."),
             "k_geteilt", T("Two scripts side by side.")),
            T("**Copy across.** The right side is read-only, but you can drag lines from it into your script - they are copied. To edit the right one, click its tab."),
        ],
        [T("The split view works in the **{{ViewEvents}}** mode; the map and model views need the room.")]),

    tut("k-uebersetzen", "tut_skript",
        T("Compile and fix errors"),
        T("Turn the script into the .ibi the game runs, read the messages and jump to the line that is wrong."),
        T("Scripts · four steps"), "t1_6",
        [
            (T("**Compile.** Press **{{Compile}}** at the bottom of the action column. It saves the `.txt` and writes the `.ibi` next to it. The button flashes green when it worked."),
             "t1_6", T("The Compile button.")),
            T("**When it fails.** The button flashes red, the status line says *Compile failed*, and the first wrong line is selected in the tree."),
            T("**Read the messages.** Click the red or yellow counter in the status line at the bottom: the **{{MessagesTitle}}** window lists every message. Each one names the line and what is wrong."),
            T("**Fix and compile again.** Usually it is a value in the wrong form - text without quotes, a missing `}`, a comparison other than `= < > !`. Fix it in the editor and press **{{Compile}}** again."),
        ],
        [
            T("For a mission, **{{MissionSave}}** compiles every script at once and refuses to pack while one does not compile."),
            T("Every save and compile keeps a copy: **{{MenuFile}} → {{FileBackupFolder}}** shows the last ten."),
        ]),

    tut("k-tasks", "tut_skript",
        T("Wait until a figure has arrived"),
        T("task, dowait and do: let a figure finish its walk before it turns or speaks - or let it do two things at once."),
        T("Scripts · five steps"), "k_neu_3",
        [
            T("**Why.** An `affect` block runs beside the script - the script does not wait for it, so two figures in two blocks already walk at the same time. But inside a block, `set ( SET_NAVGOAL )` only *starts* the walk: the next command follows at once, and the figure would turn or talk while still walking."),
            T("**Put the walk into a task.** Inside the figure's `affect` block insert `task` and name it, for example `walk`. Move the `SET_NAVGOAL` (and `SET_WALKING`, if you use it) into the task block."),
            (T("**dowait.** After the task insert `dowait ( \"walk\" )`. It starts the task and waits until it is done - until the figure has arrived. Everything after it happens when the figure stands."),
             "k_neu_3", T("Two affect blocks, each with a walk task and dowait.")),
            T("**Two things at once.** `do ( \"walk\" )` starts a task and goes on at once. Declare a second task - for example `look` with a `SET_LOOK_TARGET` - and start both with `do`. Later, `wait ( \"walk\" )` waits for the first one."),
            T("**Check it in the timeline.** Play the scene: what follows a `dowait` starts where the walk block ends. Drag the walk target, and the rest moves with it."),
        ],
        [T("Commands marked with `*` in their help text (such as `SET_NAVGOAL`) finish their task only when the action is done - that is what makes `dowait` wait.")]),

    tut("k-signale", "tut_skript",
        T("Let two scripts wait for each other"),
        T("signal and waitsignal: one script pauses until the other says go."),
        T("Scripts · four steps"), "h_zeitleiste",
        [
            T("**Where to wait.** In the script that should pause, insert `waitsignal` and type a name, for example `door_open`."),
            T("**Where to give the signal.** In the other script, at the moment it should go on, insert `signal ( \"door_open\" )`."),
            T("**Check the names.** Both must be spelled the same. Only one script can wait for a signal of a given name."),
            (T("**See it in the timeline.** Expand the figure's group in the timeline: the row **{{TlKatAblauf}}** shows waits and signals as blocks."),
             "h_zeitleiste", T("The timeline: expand a figure's group to see its rows.")),
        ],
        [T("The timeline cannot know how long a `waitsignal` waits in the game - everything after it is placed as if the signal came at once.")]),

    tut("k-makros", "tut_skript",
        T("Use macros"),
        T("walkOnly, runOnly, standOnly and the patrols: several set commands in one line."),
        T("Scripts · three steps"), "k_makro",
        [
            T("**Find them.** The macros are at the end of the **{{GroupEvents}}** list, each with the number of commands it holds. Hover over one to read what it does."),
            (T("**Insert.** Double-click or drag a macro like a command. `walkOnly`, for example, makes a figure walk and ignore enemies and alerts - five `set` commands."),
             "k_makro", T("A macro in the tree.")),
            T("**Fold or unfold.** **{{TreeFoldMacros}}** in the action column shows a macro as one line. Switch it off to see and change the single commands."),
        ]),
]

# ============================================================ Kamera
KAMERA = [
    tut("k-pilot", "tut_kamera",
        T("Fly a camera shot yourself"),
        T("Pilot mode: sit in the camera, fly with the mouse and WASD, and set keys along the timeline."),
        T("Camera · five steps"), "a_k_pilot",
        [
            T("**Select a shot.** Click the camera on the map or a shot in the **{{CamShots}}** list on the left."),
            T("**Start Pilot.** Press **{{CamPilot}}** on the camera bar. You now look through this shot."),
            (T("**Fly.** Hold the right mouse button to look around, <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> move, <kbd>Q</kbd> / <kbd>E</kbd> go down and up, <kbd>Shift</kbd> is faster."),
             "a_k_pilot", T("Flying a shot in pilot mode.")),
            T("**Set keys along the way.** Move the playhead in the timeline, fly to where the camera should be at that moment and press <kbd>Enter</kbd> (**{{CamPilotKey}}**). Repeat for more keys."),
            T("**Finish.** **{{CamPilotApply}}** writes the view into the script; <kbd>Esc</kbd> (**{{CamPilotCancel}}**) leaves it as it was."),
        ]),

    tut("k-kamera-ansicht", "tut_kamera",
        T("Put a shot where you are looking"),
        T("Look around until the picture is right, then move a shot - or a new one - exactly there."),
        T("Camera · four steps"), "h_kamera",
        [
            T("**Fly to the view you want.** Right mouse button + <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd>."),
            (T("**A new shot.** **{{CamNewShot}}** adds `camera ( MOVE )` and `camera ( PAN )` at your view, after the selected shot."),
             "h_kamera", T("The camera panel with its shots.")),
            T("**Or move an existing one.** Select a shot and press **{{CamSetToView}}**: position and direction are replaced, one undo step."),
            T("**Check.** **{{MapThroughCam}}** on the map toolbar shows what the selected shot sees, with its zoom and the black cinema bars."),
        ],
        [T("Save views you come back to: <kbd>Ctrl</kbd>+<kbd>1</kbd> … <kbd>9</kbd> stores the view, <kbd>1</kbd> … <kbd>9</kbd> jumps back. They are kept per map.")]),

    tut("k-kamera-marke", "tut_kamera",
        T("Aim the camera at a mark on the map"),
        T("Use a ref_tag as the camera position - move the mark, and the shot follows."),
        T("Camera · four steps"), "k_marke",
        [
            T("**Place a mark.** Click the tile **{{EntKindTag}}** on the left edge of the view, then click where the camera should stand. Give it a **{{EntName}}** in the inspector, such as `cam_door`."),
            (T("**Camera to the mark.** Right-click the mark in the entity list → **{{EntityCamToTag}}**. Two camera commands are inserted that use the mark's place and angles."),
             "k_marke", T("The context menu of a mark.")),
            T("**Move the mark.** Drag the mark in the view or change its **{{EntOrigin}}**: the shot moves with it."),
            T("**Turn it.** The mark's facing is the camera's direction. If the mark aims at a target, move the target to change the view."),
        ]),

    tut("k-zoom-blende", "tut_kamera",
        T("Zoom, fade to black and shake"),
        T("The camera commands ZOOM, FADE and SHAKE - and where to see them in the timeline."),
        T("Camera · four steps"), "h_zeitleiste",
        [
            T("**Insert a camera command.** Double-click `camera` in the **{{GroupEvents}}** list, then open it with <kbd>Enter</kbd>. The first field chooses what it does."),
            T("**Zoom.** Choose `ZOOM`: the first number is the field of view (80 is normal, smaller is closer), the second the time in milliseconds to get there."),
            T("**Fade.** Choose `FADE`: start colour and opacity, end colour and opacity (0 to 1), and the time. `0 0 0`, `0` to `0 0 0`, `1` fades to black."),
            (T("**Shake.** Choose `SHAKE`: strength 0 to 16 and the duration in milliseconds. The timeline shows zoom, fade and shake in their own rows under **{{TlKamera}}**."),
             "h_zeitleiste", T("The camera rows of the timeline.")),
        ]),

    tut("k-neu-timen", "tut_kamera",
        T("Retime a cutscene in the timeline"),
        T("Make a shot longer, start a line of dialogue later, move a whole part - by dragging blocks."),
        T("Timeline · five steps"), "a_k_timen",
        [
            T("**Play it.** Press ▶ under the map and watch. Switch **Sound** on to hear the dialogue you are matching the cuts against."),
            (T("**Move a block.** Drag a block left or right. Everything after it moves along, so the rest stays in sync. Hold <kbd>Alt</kbd> to move only this block."),
             "a_k_timen", T("Dragging a block in the timeline: everything after it moves along.")),
            T("**Change a duration.** Drag the right edge of a block - a camera move, a wait - to make it shorter or longer."),
            T("**Step exactly.** With the mouse over the timeline, <kbd>Left</kbd> / <kbd>Right</kbd> move the playhead by 50 ms, with <kbd>Shift</kbd> by one second."),
            T("**Check the script.** Every drag changes a value in the script (a wait, a time). Click a block to see its line."),
        ],
        [T("A warning sign on a camera block means it overlaps the next one - in the game the next move would start from where this one began, and the camera jumps.")]),

    tut("k-ansichten", "tut_kamera",
        T("Look through the camera while you work"),
        T("Through camera, the small preview picture, follow mode and the speed of playback."),
        T("Camera · four steps"), "t1_5",
        [
            T("**Through camera.** **{{MapThroughCam}}** on the map toolbar shows the picture of the selected shot, with zoom and cinema bars."),
            (T("**The small preview.** **{{CamPreview}}** on the camera bar puts a small picture of the camera's view in the bottom right corner - you keep flying freely. Click the picture to look through it."),
             "t1_5", T("Looking through the camera while playing.")),
            T("**Follow.** **{{TlFollow}}** in the timeline moves your view along with the camera while it plays."),
            T("**Slow motion.** The speed list next to the play button plays at 0.25x to 2x. Sound plays only at 1x."),
        ]),
]

# ============================================================ Figuren
FIGUREN = [
    tut("k-gangart", "tut_figur",
        T("Walk, run or walk backwards"),
        T("Change how a figure moves - right on its path in the map."),
        T("Figures · four steps"), "k_gangart",
        [
            T("**Show the path.** Click the figure on the map or its name in the timeline. Its path appears: grey ahead of the playhead, yellow where it already walked."),
            (T("**Right-click a walk target** on the path. The menu offers **{{BahnGehen}}**, **{{BahnRennen}}** and **{{BahnRueckwaerts}}**."),
             "k_gangart", T("The menu of a walk target.")),
            T("**Pick one.** behaved writes `SET_WALKING` / `SET_RUNNING` before this walk. It counts from this walk on - later walks keep it unless you change them too."),
            T("**Watch.** Play the timeline: the figure is faster, and the walk block in the timeline is shorter."),
        ]),

    tut("k-ansehen", "tut_figur",
        T("Make a figure look at someone"),
        T("Look at another figure while walking, turn towards a target, or face the walking direction again."),
        T("Figures · four steps"), "k_gangart",
        [
            T("**While walking.** Right-click a walk target on the path → **{{BahnAnsehen}}** → pick a figure. The figure walks and keeps its head on the other one."),
            T("**Standing.** Right-click the target entity in the entity list → **{{EntityAsWatch}}** (`SET_WATCHTARGET`) or **{{EntityAsLook}}** (`SET_LOOK_TARGET`). The command goes into the script after the selected line."),
            T("**Back to normal.** Right-click the walk target → **{{BahnBlickFrei}}**."),
            (T("**Check it.** Play the timeline. The category **{{TlKatBlick}}** in the figure's group shows when it looks where."),
             "k_gangart", T("Look at while walking lists the figures of the scene.")),
        ]),

    tut("k-blickrichtung", "tut_figur",
        T("Turn a figure at a moment"),
        T("Set which way a figure faces at a certain time - with the rotate ring on the map."),
        T("Figures · four steps"), "h_gizmo_drehen",
        [
            T("**Go to the moment.** Move the playhead in the timeline to where the figure should face another way."),
            T("**Select the figure** on the map and switch the gizmo bar to **{{GizmoRotate}}**."),
            (T("**Turn.** Drag the ring. When you let go, behaved writes the facing at this time into the script (a `SET_ANGLES`), and the status line says *Facing … degrees at … s*."),
             "h_gizmo_drehen", T("The rotate ring.")),
            T("**During a walk.** Turning while the figure walks sets the facing for when it arrives."),
        ],
        [T("If the time is not known - a `waitsignal` or `dowait` before - behaved says so instead of guessing.")]),

    tut("k-animation-abspielen", "tut_figur",
        T("Let a figure play an animation"),
        T("Find an animation in the model view, watch it, and put it into the script with one click."),
        T("Figures · five steps"), "h_modell",
        [
            T("**Load the figure's model.** Switch to **{{ViewModel}}** and press **{{NpcBrowser}}**: click a character - its model and skin load as in the game."),
            (T("**Find an animation.** Type into **{{ModelAnimSearch}}**, for example `TALK`. **BOTH**, **TORSO**, **LEGS** filter by body part."),
             "h_modell", T("The model view with its animation list.")),
            T("**Watch it.** Click an animation: it plays. **{{ModelLoop}}**, the speed and the frame buttons help to look closely."),
            T("**Into the script.** Select a line in your script first (inside the figure's `affect` block), then double-click the animation or press **{{ModelAnimToScript}}**: `set ( SET_ANIM_BOTH, \"…\" )` is inserted after that line."),
            T("**See it on the map.** Switch back to **{{ViewMap}}** and play the timeline: the figure plays the animation at that point."),
        ]),

    tut("k-dialog", "tut_figur",
        T("Add a line of dialogue"),
        T("A sound on a figure's voice channel, listened to and timed against the camera."),
        T("Figures · four steps"), "h_spuren",
        [
            T("**Insert sound.** Inside the figure's `affect` block, insert `sound`. In the editor choose the channel `CHAN_VOICE` and pick the file with **{{EditorBrowse}}**; **{{EditorPlay}}** plays it."),
            T("**Hear it in the scene.** Switch **{{TlAudio}}** on in the timeline and play: the line sounds at its moment."),
            (T("**See it.** The **{{FlowTracks}}** view (top of the script column) has a **{{TracksDialog}}** row - every voice line as a block, so you see where the cuts fall."),
             "h_spuren", T("The Tracks view.")),
            T("**Time it.** A `wait` after the sound keeps the next command until the line is over. Drag the wait's edge in the timeline to fit."),
        ]),
]

# ============================================================ Karte
KARTE = [
    tut("k-entity-setzen", "tut_karte",
        T("Place, move and turn entities"),
        T("Put a walk target on the floor, move it on the grid, turn it, copy it."),
        T("Map · five steps"), "a_k_setzen",
        [
            (T("**Place.** Click a tile on the left edge of the view - for example **{{EntKindNav}}** - then click on the floor. Hold <kbd>Shift</kbd> to place several. <kbd>Esc</kbd> stops."),
             "a_k_setzen", T("Placing walk targets.")),
            T("**Move.** Drag the entity over the floor, or drag a coloured arrow to move it along one axis. <kbd>Ctrl</kbd> snaps to 8 units."),
            T("**Turn.** Switch the gizmo bar to **{{GizmoRotate}}** and drag the ring; <kbd>Ctrl</kbd> turns in 15 degree steps."),
            T("**Copy.** <kbd>Alt</kbd>+drag makes a copy and moves it. Or press **{{EntDuplicate}}** in the inspector."),
            T("**Onto the floor.** **{{EntDropFloor}}** in the inspector puts it exactly on the floor below - the inspector shows how high the feet are."),
        ],
        [T("**{{Grid}}** on the map toolbar snaps everything to 8 to 64 units while dragging and placing.")]),

    tut("k-entity-finden", "tut_karte",
        T("Find an entity and fly to it"),
        T("Search the entity list, fly there, hide what is in the way."),
        T("Map · four steps"), "h_entity",
        [
            T("**Search.** Type a name or class into the search box above the entity list, for example `navgoal` or `kyle`."),
            T("**Fly there.** Double-click the row: the view flies to the entity and selects it."),
            (T("**Hide groups.** Click the eye in front of a group to hide it in the view; <kbd>Alt</kbd>+click shows only that group. **{{EntityShowAll}}** brings everything back."),
             "h_entity", T("The entity list with its groups.")),
            T("**Names on the map.** **{{MapNames}}** on the map toolbar writes every name into the view."),
        ]),

    tut("k-entity-felder", "tut_karte",
        T("Edit an entity's keys"),
        T("Name, position, spawnflags and all other keys in the inspector - and the NPC type search."),
        T("Map · four steps"), "k_inspektor",
        [
            T("**Select the entity.** Click it in the view or in the list. The inspector under the list shows its class in colour; hover over it for a description from the game code."),
            (T("**General.** **{{EntName}}** is how scripts address it, **{{EntOrigin}}** and **{{EntAngle}}** where it stands and faces."),
             "k_inspektor", T("The inspector.")),
            T("**Keys.** Under **{{EntSecFields}}**, change values or add one with **{{EntAddKey}}**. For a character, the **...** next to `NPC_type` searches every NPC of the game. Script keys have **{{EntOpenScript}}** to open the script."),
            T("**Everything as text.** **{{EntRaw}}** shows all keys as plain text, like Hammer without SmartEdit."),
        ],
        [T("Entity changes are kept with the mission: **{{MissionSave}}** writes them into the `.ent` file.")]),

    tut("k-draufsicht", "tut_karte",
        T("Look at the map from above"),
        T("Top view with a cut height - to see the floor plan of a level, one floor at a time."),
        T("Map · three steps"), "k_oben",
        [
            (T("**Switch on {{MapTopView}}** on the map toolbar. The view looks straight down; the wheel zooms, right or middle mouse and <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> move."),
             "k_oben", T("The top view.")),
            T("**Cut away ceilings.** Drag the **cut height** box at the top right: everything higher is not drawn. Lower it to look at a lower floor."),
            T("**Work as usual.** Selecting, dragging and placing work in the top view too - handy for paths and waypoints."),
        ]),

    tut("k-hell", "tut_karte",
        T("See better in dark maps"),
        T("Brightness, ambient light and switching effects, fog or sky off while you work."),
        T("Map · three steps"), "h_ansicht_einst",
        [
            (T("**Open {{MapViewSettings}}** (the gear on the map toolbar). **{{MapBrightness}}** lightens everything, **{{MapMinLight}}** lifts only the darkest areas - 0 is exactly like the game."),
             "h_ansicht_einst", T("The view settings.")),
            T("**Switch things off.** **{{MapFog}}**, **{{MapEffects}}**, **{{MapGlow}}** and **{{MapSky}}** on the toolbar switch those parts off. Without the sky you can look into the map from outside."),
            T("**Only this script.** **{{MapScriptFocus}}** in the view settings dims every entity the active script does not use."),
        ]),
]

# ============================================================ Animation
ANIMATION = [
    tut("k-tuer", "tut_anim",
        T("A door that opens"),
        T("Animate a door or platform with move commands: two keys, and the script moves it."),
        T("Animation · five steps"), "h_animation",
        [
            T("**Select the door.** Click it in the view - doors and platforms are `func_` entities. In the inspector press **{{EntAnimate}}**."),
            (T("**Mode.** In the **{{AnimTitle}}** window keep **{{AnimModeMove}}**: the movement becomes `move` commands in the script."),
             "h_animation", T("The Animation window.")),
            T("**First key.** Put the playhead where the door should start moving and press **{{AnimAddKey}}** - this is the closed position."),
            T("**Second key.** Move the playhead later (for example 2 seconds) and drag the door up or aside in the view: while the window is open, dragging sets a key at the playhead."),
            T("**Play.** The door now opens in the timeline. **{{AnimLinear}}** makes it move at constant speed instead of easing out."),
        ],
        [T("Animated entities need a name; behaved gives the door one if it has none.")]),

    tut("k-roff-bearbeiten", "tut_anim",
        T("Change a ROFF file of the game"),
        T("Turn a ROFF flight from the game into keys, change it, and save it as your own copy."),
        T("Animation · four steps"), "a_roff",
        [
            T("**Select the entity** that plays the ROFF (a ship, a boulder) and press **{{EntAnimate}}**."),
            T("**Edit.** Under **{{AnimRoffPlays}}** press **{{AnimRoffEdit}}** next to the file. behaved turns the frames into a few keys - the game file stays untouched, you edit a copy with its own name."),
            (T("**Change the keys.** Drag the diamonds in the view, change times in the table, add keys with **{{AnimAddKey}}**."),
             "a_roff", T("Editing ROFF keys.")),
            T("**Save.** Saving the script writes `scripts/<name>.rof`; **{{MissionSave}}** puts it into the `.pk3`."),
        ],
        weiter=T("Building a ROFF from scratch: [[tut-p5]]. The details: [[animation#roff]].")),
]

# ============================================================ Missionen
MISSION = [
    tut("k-graph", "tut_mission",
        T("Connect a character to a script"),
        T("In the Graph view: drag a wire from a character to a script, and choose when it runs."),
        T("Missions · four steps"), "h_graph",
        [
            T("**Open the Graph.** Above the script column, switch from **{{FlowClassic}}** to **{{FlowGraph}}**. Every entity and script of the mission is a node."),
            (T("**Drag a wire.** Drag from the dot on the right of a character node to a script node."),
             "h_graph", T("The Graph view.")),
            T("**Choose when.** A menu asks **{{GraphConnectAs}}**: `spawnscript` (when it appears), `deathscript`, `usescript` and more. behaved writes the key into the entity."),
            T("**New script.** Drag a command from the **{{GroupEvents}}** list onto empty space in the graph: a new script with that command is made."),
        ]),

    tut("k-mission-weiter", "tut_mission",
        T("Continue and pack a mission"),
        T("Open your mission again, check it and pack it into a .pk3 for the game."),
        T("Missions · four steps"), "h_mission_speichern",
        [
            T("**Open it again.** **{{MenuFile}} → {{RecentMissions}}**, or the list on the start page of the **{{ViewMap}}** tab, opens map, entities and every script with one click. **{{MissionOpenFolder}}** opens one from its folder."),
            T("**Work.** Change scripts and entities. A dot next to the mission name means unsaved changes."),
            (T("**Save mission.** **{{MissionSave}}** shows every file it will pack and checks the mission: errors in red (they must be fixed), warnings in orange."),
             "h_mission_speichern", T("The Save mission window.")),
            T("**Pack.** Press **{{MissionSaveNow}}**: scripts are saved and compiled, the `.ent` written and the `.pk3` packed. Start the game with `devmap <name>`."),
        ]),

    tut("k-wegpunkte", "tut_mission",
        T("Give NPCs paths to walk"),
        T("Create the NPC waypoint network automatically, check it, and add paths by hand."),
        T("Missions · four steps"), "h_wegpunkte",
        [
            T("**Open the window.** **{{WegTitel}}** on the map toolbar. **{{WegZeigen}}** shows the network: green by hand, blue automatic, orange jumps."),
            (T("**Create automatically.** Press **{{WegErzeugen}}**: waypoints are spread over the walkable floor and connected. The spacing slider sets how dense (the original levels use about 230)."),
             "h_wegpunkte", T("The NPC waypoints window.")),
            T("**Check.** **{{WegPruefen}}** finds what the game would reject - click a problem to fly there."),
            T("**By hand.** **{{WegSetzen}}**: click the floor to place a path; **{{WegVerbinden}}** connects two points (<kbd>Ctrl</kbd>: a jump)."),
        ],
        weiter=T("More in [[wegpunkte]].")),
]

# ============================================================ Modell
MODELL = [
    tut("k-modell", "tut_mission",
        T("Look at a character model"),
        T("Load any character of the game, turn it, look at its skins and its skeleton."),
        T("Model · four steps"), "h_modell",
        [
            T("**Switch to {{ViewModel}}** in the mode bar."),
            T("**Pick a character.** **{{NpcBrowser}}** lists every character of the game - type to search, click to load it with its skin."),
            (T("**Look around.** Left mouse turns, middle mouse moves, the wheel zooms. **{{ModelTurntable}}** turns it by itself, **{{ModelResetView}}** brings it back to the front."),
             "h_modell", T("The model view.")),
            T("**Details.** **{{ModelSkin}}** switches skins, **{{ModelBones}}** shows the skeleton, **{{ModelTextures}}** switches textures off."),
        ]),
]

GRUPPEN = [
    ("tut_skript", T("Tutorials: scripts")),
    ("tut_kamera", T("Tutorials: camera and timeline")),
    ("tut_figur", T("Tutorials: figures")),
    ("tut_karte", T("Tutorials: map and entities")),
    ("tut_anim", T("Tutorials: animation")),
    ("tut_mission", T("Tutorials: missions and models")),
]

SEITEN = SKRIPT + KAMERA + FIGUREN + KARTE + ANIMATION + MISSION + MODELL
