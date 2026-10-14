"""Kurze Tutorials, dritte Runde (rc600): Missionslogik, vorhandene Skripte,
Karte, Zeit und Ansichten, Modelle.

shank: "hast du noch Tutorial-Ideen, die unser Programm kann?" - "dann mach
alle". Zwei Ideen sind bewusst NICHT dabei: camera PATH/TRACK und eine Figur,
die dem Spieler folgt (SET_LEADER) - beides spielt behaved in der Vorschau
nicht ab, und das Handbuch soll nichts zeigen, was man dann nicht sieht.

Fakten: NPC_spawner mit targetname wartet auf ein use (OpenJK NPC_spawn.cpp,
"If targeted, will only spawn a NPC when triggered"); die Pruefungen S001 bis
S006 aus gen_i18n.py; Schluessel aus tools/entdefs.txt. Englisch zuerst.
"""
from inhalt_basis import T
from inhalt_kurz import tut

LOGIK = [
    tut("k-tod", "tut_mission",
        T("When the enemy dies, the door opens"),
        T("A deathscript on a figure: the moment it falls, a script of yours runs - here it opens a door."),
        T("Missions · four steps"), "k_tod_insp",
        [
            (T("**Give the figure a deathscript.** Select the enemy in the **{{ViewMap}}** tab. In the inspector add the key `deathscript` with a script name of your mission, for example `taspir_tut/rosh_dies` - or, in the **{{FlowGraph}}** view, drag a wire from the figure to a script and choose `deathscript` under **{{GraphConnectAs}}**."),
             "k_tod_insp", T("The enemy's deathscript in the inspector.")),
            T("**Create the script.** Press **{{EntOpenScript}}** next to the key: the script is created in the mission folder and opened."),
            (T("**Open the door.** In it, insert `use` with the door's name - right-click the door in the entity list → **{{EntityUse}}** does it for you. A `wait` before it lets the moment breathe."),
             "k_tod_baum", T("The deathscript: a short wait, then the door.")),
            T("**Other moments.** The same works with `painscript` (hit), `awakescript` (notices the player), `angerscript` (starts fighting), `fleescript` and `spawnscript`."),
        ]),

    tut("k-ansprechen", "tut_mission",
        T("The player talks to a figure"),
        T("usescript and SET_PLAYER_USABLE: pressing the use key on a figure starts its script - a conversation."),
        T("Missions · four steps"), "k_use_baum",
        [
            T("**Make it usable.** In the figure's spawn script insert `set ( SET_PLAYER_USABLE, true )` - otherwise the use key does nothing."),
            T("**The script for the talk.** Add the key `usescript` to the figure (or connect it in the **{{FlowGraph}}** view as `usescript`) and press **{{EntOpenScript}}**."),
            (T("**Write the talk.** Sounds on `CHAN_VOICE`, a `SET_LOOK_TARGET` at the player, perhaps a short camera. The figure says its lines every time the player presses use."),
             "k_use_baum", T("The spawn script makes the figure usable.")),
            T("**Only once?** Set `SET_PLAYER_USABLE` to `false` at the end of the usescript - or use a variable ([[k-variablen]])."),
        ]),

    tut("k-zusammenspiel", "tut_mission",
        T("Let several scripts work together"),
        T("run starts a script, signal and waitsignal hand over - and the split view shows both at once."),
        T("Missions · four steps"), "k_zusammen",
        [
            T("**Split the work.** Long scenes are easier in parts: the intro starts the next part with `run ( \"taspir_tut/talk\" )` - the name as under `scripts/`, without ending."),
            T("**Hand over.** Where the intro must wait for the other part, insert `waitsignal ( \"talk_done\" )`; at the end of the other script `signal ( \"talk_done\" )`."),
            (T("**See both.** **{{MenuFile}} → {{SplitToggle}} → 2** shows the two scripts side by side; drag lines across to copy them."),
             "k_zusammen", T("Two scripts that hand over with signal and waitsignal.")),
            T("**Keep the overview.** The **{{FlowGraph}}** view draws `run` connections between the scripts of the mission."),
        ]),

    tut("k-pruefen", "tut_mission",
        T("Fix what Save mission reports"),
        T("The checks of Save mission - what each message means, which ones stop the packing, and how to fix them."),
        T("Missions · six steps"), "k_pruefen",
        [
            (T("**Open the check.** **{{MissionSave}}** lists every file it will pack and, below, what it found. Errors in red stop the packing until they are fixed; warnings in orange let you pack anyway - but read them."),
             "k_pruefen", T("Save mission with two missing scripts and a target of the original map.")),
            T("**S001 (warning) - a script is missing.** An entity names a script that does not exist yet; in the game nothing would happen. Select the entity and press **{{EntOpenScript}}** next to the key: the script is created."),
            T("**S002 (error) - a script does not compile.** Open it and press **{{Compile}}**: the wrong line is selected ([[k-uebersetzen]])."),
            T("**S003 (warning) - no player start.** The mission cannot start. Place one with the tile **{{EntKindStart}}**."),
            T("**S005 (warning) - a target points to nothing.** A `target` names an entity that does not exist - often a leftover of the original map, like the one in the picture."),
            T("**S004 and S006.** A missing `ENDMARKER` is added for you (warning); an invalid mission name is an error - letters, digits and `_` only."),
        ]),
]

VORHANDEN = [
    tut("k-ibi", "tut_skript",
        T("Open a compiled script of the game"),
        T("The game only ships .ibi files. behaved turns them back into a script you can read and change."),
        T("Scripts · four steps"), "k_ibi",
        [
            (T("**Find it.** **{{MenuFile}} → {{Pk3Browse}}**, choose **{{Pk3Scripts}}** and type part of the name into **{{Pk3Filter}}**, for example `taspir1/intro`."),
             "k_pk3", T("The files in the game's archives.")),
            (T("**Open it.** Double-click the entry: the `.ibi` opens as a script in the current tab - commands, values and blocks, exactly as Raven wrote them. Open an empty tab first (**+**) if you want to keep the script you are working on; with unsaved changes behaved asks before."),
             "k_ibi", T("A compiled script of the game, opened as text.")),
            T("**From disk.** **{{MenuFile}} → {{FileOpen}}** opens `.ibi` files from a folder the same way."),
            T("**Change it.** A script from an archive has no file yet - the first save asks where to put it. To ship a changed version of a game scene, see [[k-szene-aendern]]."),
        ]),

    tut("k-pk3", "tut_skript",
        T("Find files in the game archives"),
        T("Scripts, maps and models of the game and every mod, searchable in one window."),
        T("Scripts · four steps"), "k_pk3",
        [
            (T("**Open the browser.** **{{MenuFile}} → {{Pk3Browse}}**. It lists the files of every game folder you added."),
             "k_pk3", T("The Files in .pk3 window.")),
            T("**Choose the kind.** **{{Pk3Scripts}}**, **{{Pk3Maps}}** or **{{Pk3Models}}**. The **{{Pk3Archive}}** list narrows it to one `.pk3`."),
            T("**Search.** Type into **{{Pk3Filter}}** - part of a name is enough."),
            T("**Open.** Double-click: a script replaces the one in the current tab (behaved asks first if it has unsaved changes), a map loads, a model opens with its skin in the **{{ViewModel}}** tab."),
        ]),

    tut("k-aenderungen", "tut_skript",
        T("See what you have changed"),
        T("The change margin, going back to the original of one command, and finding identical commands."),
        T("Scripts · four steps"), "k_rand",
        [
            (T("**The margin.** Change a value: a coloured bar appears left of the line - orange for changed and not saved, green for changed and saved, blue for back to the original. **{{MenuView}} → {{ViewChangeHistory}}** switches it on or off."),
             "k_rand", T("Changed lines in the margin.")),
            T("**What was it before?** Hover over the bar: the original line is shown."),
            T("**Back to the original.** Right-click the line → **{{ActRevertOriginal}}**: only this command goes back, everything else stays."),
            T("**Identical commands.** **{{MenuView}} → {{ViewHighlightSame}}** marks every command that looks exactly like the selected one - handy for a camera that is used several times."),
        ]),

    tut("k-backup", "tut_skript",
        T("Get an old version back"),
        T("Automatic copies on every save, your own backup, and restoring it."),
        T("Scripts · four steps"), "k_dateimenue",
        [
            (T("**Automatic copies.** Every save and every compile keeps a copy - the last ten, `backup1.txt` (newest) to `backup10.txt`. **{{MenuFile}} → {{FileBackupFolder}}** opens the folder."),
             "k_dateimenue", T("The File menu.")),
            T("**Your own backup.** **{{FileBackup}}** (<kbd>Alt</kbd>+<kbd>B</kbd>) writes `<script>.bak` next to the script - before a big change."),
            T("**Restore it.** **{{FileRestore}}** (<kbd>Alt</kbd>+<kbd>R</kbd>) goes back to the `.bak`, after a question."),
            T("**Smaller steps.** For the last few changes the undo history is quicker: [[k-rueckgaengig]]."),
        ]),

    tut("k-meldungen", "tut_skript",
        T("Understand the warnings"),
        T("The Messages window: values that are not in their list, unknown fields, notes while reading a file."),
        T("Scripts · three steps"), "k_meldung",
        [
            T("**The counters.** The status line counts errors and notes. Click a counter to open the **{{MessagesTitle}}** window."),
            (T("**Read them.** Each message names its code and the command. A misspelt value such as `SET_WALKNG` is reported as *not in SET_TYPES (kept unchanged)* - behaved keeps it, because mods bring their own values, but it is far more often a typo."),
             "k_meldung", T("V004: a value that is not in its list.")),
            T("**Fix and check.** Correct the value in the editor - the message disappears. Messages never change the script by themselves."),
        ]),
]

SZENE = [
    tut("k-gruppe", "tut_karte",
        T("Move many entities at once"),
        T("Select with a frame, move and turn the group, copy it."),
        T("Map · four steps"), "k_rahmen",
        [
            (T("**Draw a frame.** In the **{{ViewMap}}** tab drag over empty space in the view: every entity inside is selected. <kbd>Ctrl</kbd> or <kbd>Shift</kbd> adds to it."),
             "k_rahmen", T("Several entities selected with a frame.")),
            T("**Move the group.** Drag one of them, or use the arrows: all move together."),
            T("**Turn the group.** Switch the gizmo bar to **{{GizmoRotate}}** and drag the ring."),
            T("**Copy it.** <kbd>Alt</kbd>+drag copies the whole group - **{{EntDuplicate}}** in the inspector does the same."),
        ]),

    tut("k-blickpunkte", "tut_kamera",
        T("Remember viewpoints and camera paths"),
        T("Number keys for views you need again - and only the camera path you are working on."),
        T("Camera · three steps"), "k_bahnen",
        [
            T("**Store a view.** With the mouse over the map press <kbd>Ctrl</kbd>+<kbd>1</kbd> … <kbd>9</kbd>; the status line confirms it. <kbd>1</kbd> … <kbd>9</kbd> jumps back. The views are kept per map."),
            (T("**All paths or one.** In the camera panel, **{{CamPathAll}}** draws every camera move of the script, **{{CamPathActive}}** only the one around the playhead - clearer in long scenes."),
             "k_bahnen", T("Every camera path of the scene.")),
            T("**Fly to a shot.** **{{CamFlyToShot}}** puts your view next to the selected shot."),
        ]),

    tut("k-spawnflags", "tut_karte",
        T("A figure that appears on cue"),
        T("Spawner keys and spawnflags: a character that waits until the script calls it."),
        T("Map · four steps"), "k_spawnflags",
        [
            T("**Two names.** A character spawner has `NPC_targetname` - the name of the figure, used by `affect` - and may have `targetname` - the name of the spawner itself."),
            (T("**Wait for the cue.** Give the spawner a `targetname`, for example `rosh_spawner`. In the game it then spawns its figure only when it is used."),
             "k_spawnflags", T("A spawner's keys and spawnflags.")),
            T("**Call it.** In the script: `use ( \"rosh_spawner\" )` - from then on the figure exists and `affect ( \"rosh\" )` reaches it. (The preview shows the figure from the start.)"),
            T("**Spawnflags.** The checkboxes under **{{EntFlags}}** set the class's options; hover over one for what it does."),
        ]),

    tut("k-ent", "tut_karte",
        T("Load a separate .ent file"),
        T("Swap the entities of a map without touching the map - the way many mods add their own scenes."),
        T("Map · three steps"), "k_kartenmenue",
        [
            (T("**Open it.** **{{MapMore}}** → **{{OpenEnt}}** loads an `.ent` file: its entities replace the ones of the map."),
             "k_kartenmenue", T("The Maps & files menu.")),
            T("**Work as usual.** The entities appear in the list and the view; change them like any other."),
            T("**Saved with the mission.** **{{MissionSave}}** writes the entities into `maps/<name>.ent` in the `.pk3` - the game loads it instead of the map's own."),
        ]),
]

ZEIT = [
    tut("k-spuren", "tut_kamera",
        T("Time a scene in the Tracks view"),
        T("The script over time, right in the script column: camera, figures and dialogue as rows."),
        T("Timeline · four steps"), "h_spuren",
        [
            (T("**Switch to Tracks.** Above the script column choose **{{FlowTracks}}**: rows for **{{TracksCamera}}**, each figure, **{{TracksDialog}}** and **{{TracksScript}}**."),
             "h_spuren", T("The Tracks view.")),
            T("**Jump to a line.** Click a block: its line is selected."),
            T("**Change a duration.** Drag the right edge of a block."),
            T("**Move through time.** Drag in the ruler; **{{TracksPlay}}** plays."),
        ]),

    tut("k-zeitleiste-plus", "tut_kamera",
        T("The timeline in detail"),
        T("Waits as blocks, quiet rows, frames instead of seconds, speed - and the warning for overlapping camera moves."),
        T("Timeline · five steps"), "k_waits",
        [
            (T("**Waits as blocks.** **{{TlWaits}}** shows every `wait` as its own block - drag it to change it directly."),
             "k_waits", T("Waits shown as blocks.")),
            T("**Quiet rows.** **{{TlIdleTracks}}** also shows the rows where nothing happens after the start."),
            T("**Frames.** Click the time display to switch between seconds and frames."),
            T("**Speed.** The list next to the play button plays at 0.25x to 2x; sound only at 1x."),
            T("**Overlap warning.** A warning sign on a camera block: it overlaps the next move. In the game the next one starts from where this one began, and the camera jumps - shorten one of them."),
        ]),
]

MODELLE = [
    tut("k-skins", "tut_mission",
        T("Skins and skeletons of a model"),
        T("Another skin, another skeleton, and the surfaces a skin switches on or off."),
        T("Model · four steps"), "k_modell_leiste",
        [
            (T("**Switch skins.** In the **{{ViewModel}}** tab the **{{ModelSkin}}** list shows the skins that come with the model; **{{OpenSkin}}** loads another one."),
             "k_modell_leiste", T("The playback bar with the skin list.")),
            T("**Surfaces.** The counter of surfaces shows how many the skin draws; click it to list them all in the detail log."),
            T("**Caps.** **{{ModelCaps}}** shows the `_off` surfaces that close the holes where limbs attach."),
            T("**Another skeleton.** **{{ModelLoadGla}}** loads a different `.gla` with its animations."),
        ]),
]

SEITEN = LOGIK + VORHANDEN + SZENE + ZEIT + MODELLE
