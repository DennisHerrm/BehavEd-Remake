"""Das grosse Tutorial: eine eigene Mission auf taspir1, von Grund auf.

shank: "einmal eine komplett eigene Cutscene erstellen fuer das Base Game -
eine eigene frische Mission als Tutorial, wo du alle Dinge zeigst, was das
Programm so kann". Und: "das ist ein Tool fuer die gesamte JKA-Community".

Die Bilder entstehen im Selbsttest (BHED_EDITORTEST=handbuch) genau in
dieser Reihenfolge: dieselbe Mission, dieselbe Karte, Schritt fuer Schritt.
Zuerst auf yavin1 versucht - die Felsen der Schlucht verdeckten fast jeden
Blick; die Landeplattform von taspir1 ist offen und gut zu lesen.
Englisch zuerst.
"""
from inhalt_basis import T, Tf


def teil(sid, nr, titel, kurz, bild, schritte, danach=None):
    bloecke = [("h2", "schritte", T("Steps", "Schritte", "步骤", "手順")), ("steps", schritte)]
    if danach:
        bloecke += danach
    return {"id": sid, "gruppe": "tut", "titel": titel, "kurz": kurz, "titelbild": bild,
            "meta": Tf("Part %d of 8 · Taspir mission", nr), "bloecke": bloecke}


UEBERSICHT = {
    "id": "tut-projekt",
    "gruppe": "tut",
    "titel": T("Your own mission: take-off on Taspir"),
    "kurz": T("A complete little mission for Jedi Academy, built from nothing: Kyle and Rosh walk across a landing pad, the camera follows them, the Raven's Claw lifts off and flies past the camera, the picture shakes and fades to black - and the game plays it."),
    "titelbild": "t1_ergebnis",
    "meta": T("Eight parts · base game · about 30 minutes"),
    "bloecke": [
        ("anim", "a_kamera", T("The finished cutscene, seen through its camera.")),
        ("p", T("This tutorial uses only the base game: the map `taspir1` - the landing pad at the start of the Taspir III mission - with Kyle's ship, the Raven's Claw, and the characters Kyle and Rosh. You need nothing but Jedi Academy and BehavEd. Every picture on the following pages was taken while exactly this mission was built, step by step.")),
        ("fluss", T("The mission in eight parts"),
         [T("Create the mission"), T("Place the characters"), T("Let them walk"), T("The camera"),
          T("The ship takes off"), T("Cut, shake, fade"), T("A second script"), T("Save and play")],
         [[T("player start + script runner")], [T("NPC spawners, walk targets")], [T("affect, task, dowait")],
          [T("shots, travel time, pilot")], [T("a ROFF file")], [T("SHAKE, PAN, FADE")], [T("spawnscript, macro")],
          [T("zz_taspir_tut.pk3"), T("devmap taspir_tut")]]),
        ("h2", "teile", T("The parts")),
        ("cards", ["tut-p1", "tut-p2", "tut-p3", "tut-p4", "tut-p5", "tut-p6", "tut-p7", "tut-p8"]),
        ("h2", "vorher", T("Before you start")),
        ("list", [
            T("Add the game folder once: **{{MenuEdit}} → {{PathsTitle}}** → **{{Pk3AddPath}}** → the `base` folder of Jedi Academy (the one with `assets0.pk3`). See [[erste-schritte#spielordner]]."),
            T("If the window is new to you, read [[fenster]] first - five minutes."),
            T("Everything can be undone with <kbd>Ctrl</kbd>+<kbd>Z</kbd>. The game files are never changed: your mission goes into its own folder and its own `.pk3`."),
        ]),
    ],
}

TEILE = [
    teil("tut-p1", 1, T("1 · Create the mission"),
         T("A new mission on taspir1: map, player start, script runner and an intro script are made for you."),
         "t4_2",
         [
             (T("**Open the start page.** Switch the mode bar to **{{ViewMap}}**. Without a map, the tab shows the start page."),
              "h_start", T("The start page of the Mission tab.")),
             (T("**New mission.** Click **{{StartCardNew}}**. In the list type `taspir1` and pick it. As **{{MissionName}}** type `taspir_tut` - letters, digits and `_`. The **{{MissionFolder}}** is filled in by itself. Press **{{MissionCreate}}**."),
              "h_mission_neu", T("The New mission window."),
             [
                 (T("Map list"), T("Type to search, click the map the mission is built on.")),
                 (T("**{{MissionName}}**"), T("Letters, digits and `_`. The game loads it with `devmap <name>`.")),
                 (T("**{{MissionFolder}}**"), T("The folder under `scripts/` for the mission's scripts.")),
                 (T("**{{MissionCreate}}** / **{{MissionCancel}}**"), T("Creates the mission - or closes the window.")),
             ]),
             (T("**Look at what you got.** The map opens on the landing pad. behaved has added a `target_scriptrunner` next to the player start: when the player spawns, the script `taspir_tut/intro` runs. It is open in a tab - a short camera shot behind the player start."),
              "t4_2", T("The new mission: the player start is selected, the intro script is open on the right.")),
             T("**Switch off the original intro.** taspir1 has its own start: its player start already triggers a runner with `taspir1/preIntro`, and the same trigger now starts yours too - both would play at once. Type `t271` into the search box of the entity list - that is the runner of the original (its `usescript` is `taspir1/preIntro`) - select it and press <kbd>Delete</kbd>. The **{{FlowGraph}}** view shows such connections at a glance ([[skriptfluss#graph]])."),
         ],
         [("note", T("Every map of the game works this way. Its own entities stay; remove what you do not want - it changes only your mission, never the game."))]),

    teil("tut-p2", 2, T("2 · Place the characters"),
         T("Kyle and Rosh at the south edge of the pad, and a walk target in front of the ship for each of them."),
         "k_inspektor",
         [
             (T("**Look at the pad.** Hold the right mouse button and fly with <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> until you see the pad from the south, a little from above."),
              "a_k_setzen", T("Placing the two characters and their walk targets.")),
             T("**Place Kyle.** Click the tile **{{EntKindNpc}}** on the left edge of the view, then click near the south edge of the pad. A character marker appears and is selected."),
             (T("**Name him.** In the inspector under the entity list set **{{EntName}}** to `kyle` and `NPC_type` to `Kyle` (the **...** button lists every character of the game). **{{EntDropFloor}}** puts him exactly on the ground."),
              "k_inspektor", T("The inspector with Kyle selected."),
             [
                 (T("**{{EntityFlyTo}}**"), T("Flies the view to the entity.")),
                 (T("**{{EntDropFloor}}**"), T("Puts it exactly on the floor below.")),
                 (T("**{{EntDuplicate}}** / **{{EntDelete}}**"), T("Copies or removes it - every selected entity.")),
                 (T("Undo / Redo"), T("The history of entity changes, separate from the script's.")),
                 (T("**{{EntRaw}}**"), T("Shows every key as plain text.")),
                 (T("Filter fields"), T("Shows only the keys whose name contains the text.")),
                 (T("**...**"), T("Next to `NPC_type`: searches every character of the game.")),
             ]),
             T("**Place Rosh** the same way beside him: **{{EntName}}** `rosh`, `NPC_type` `rosh_penin`."),
             (T("**Two walk targets.** Click the tile **{{EntKindNav}}** and click in front of the ship - name it `nav_kyle`. A second one beside it: `nav_rosh`."),
              "t4_3", T("The new entities in the list and on the map.")),
         ],
         [("note", T("Hold <kbd>Shift</kbd> while clicking to place several entities of one kind. More in [[entities#setzen]]."))]),

    teil("tut-p3", 3, T("3 · Let them walk"),
         T("An affect block for each character: walk to the target, wait until there, then look at the other one."),
         "h_figur_bahn",
         [
             (T("**Write the walk for Kyle.** In the intro script, after the camera lines, insert `affect` (double-click it in the **{{GroupEvents}}** list) and give it the name `kyle`. Inside it insert `task` named `walk`, and inside the task `set ( SET_WALKING, true )` and `set ( SET_NAVGOAL, nav_kyle )`. After the task: `dowait ( walk )` and `set ( SET_LOOK_TARGET, rosh )`."),
              "k_neu_3", T("The intro script.")),
             T("**The same for Rosh** - copy the block (**{{ActCopy}}**, **{{ActPaste}}**) and change the names: `rosh`, `nav_rosh`, look target `kyle`."),
             (T("**Watch them walk.** Drag the playhead in the timeline. Both walk at the same time - an `affect` block does not hold up the script. Click Kyle to see his path: grey ahead, yellow behind."),
              "h_figur_bahn", T("Kyle in the middle of his walk.")),
             (T("**Walk or run.** Right-click the walk target at the end of his path: **{{BahnGehen}}**, **{{BahnRennen}}**, **{{BahnRueckwaerts}}** or **{{BahnAnsehen}}** change how he gets there."),
              "k_gangart", T("The menu of a walk target.")),
             (T("**A detour.** Grab Kyle in the middle of his walk and drag him aside: behaved inserts a new walk target there, and his path bends around it."),
              "t2_6", T("A path point inserted in the middle of the walk.")),
         ],
         [("note", T("Why the task? `SET_NAVGOAL` only starts the walk. `dowait` waits until the task is done - until he has arrived - so he looks at Rosh only when he stands. More in [[k-tasks]]."))]),

    teil("tut-p4", 4, T("4 · The camera"),
         T("A wide shot of the pad, a move down behind the two, set from the view and flown by hand."),
         "h_kamera",
         [
             T("**The first shot** is already there - the template behind the player start. Select it in the **{{CamShots}}** list, fly out to a wide view of the pad from the south-east and press **{{CamSetToView}}**: the shot is now where you are looking."),
             (T("**A second shot.** Fly down behind Kyle and Rosh, low, looking at the ship. Press **{{CamNewShot}}** and set its **{{CamDuration}}** to `4` seconds: the camera flies from the wide shot to this one while the two walk."),
              "h_kamera", T("The camera with its shots and its path.")),
             (T("**Fine-tune in the view.** The camera bar has **{{GizmoMove}}** (arrows) and **{{GizmoRotate}}** (rings). Releasing writes the change into the script."),
              "h_kamera_verschieben", T("Moving a shot with the arrows.")),
             (T("**Or fly it.** **{{CamPilot}}** puts you into the shot: right mouse looks, <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> move. <kbd>Enter</kbd> sets a key at the playhead, **{{CamPilotApply}}** keeps it, <kbd>Esc</kbd> throws it away."),
              "a_k_pilot", T("Flying a shot in pilot mode.")),
             (T("**Look through it.** **{{CamPreview}}** shows a small picture of what the camera sees; click it to look through the camera."),
              "t1_5", T("The camera preview in the corner of the view.")),
         ]),

    teil("tut-p5", 5, T("5 · The ship takes off"),
         T("Animate the Raven's Claw with four keys and save the flight as a ROFF file."),
         "t3_5",
         [
             (T("**Select the ship.** The Raven's Claw on the pad is a `misc_model_breakable` named `ravensClaw`. Click it on the map, then **{{EntAnimate}}** in the inspector."),
              "t3_1", T("The ship selected, the Animation window open.")),
             (T("**ROFF file.** In the **{{AnimTitle}}** window choose **{{AnimModeRoff}}**. The script gets a block that plays the file, and the first key is where the ship stands now."),
              "t3_2", T("The Animation window in ROFF mode."),
             [
                 (T("**{{AnimModeMove}}** / **{{AnimModeRoff}}**"), T("How the movement is stored: as `move` commands in the script, or as a ROFF file the script plays.")),
                 (T("**{{AnimRoffName}}**"), T("The name of the ROFF, as in `play ( PLAY_ROFF, ... )`; saved as `scripts/<name>.rof` next to the script.")),
                 (T("**{{AnimRoffSmooth}}**"), T("On: one curve through all keys, easing in and out. Off: straight from key to key at constant speed.")),
                 (T("**{{AnimAddKey}}**"), T("Adds a key at the playhead, with the entity where it is now.")),
                 (T("**{{AnimDeleteKey}}** / **{{AnimGotoKey}}**"), T("Removes the selected key / moves the timeline to it.")),
                 (T("**{{AnimRemove}}**"), T("Removes the animation block from the script.")),
                 (T("Key table"), T("Time, position and direction of every key. Type new values, or click a row to jump there.")),
             ]),
             (T("**Set the keys.** At 5 s press **{{AnimAddKey}}** - the ship still stands. At 6.5 s drag it straight up a little: a key is set at the playhead. At 8.5 s drag it forward and higher, and tip the nose up with the ring. At 10.5 s it is far away and high."),
              "t3_4", T("The keys of the flight, with the rotate ring.")),
             (T("**Watch.** Play the timeline: the ship lifts off and flies away. **{{AnimRoffSmooth}}** makes one curve through the keys."),
              "a_roff", T("The Raven's Claw taking off.")),
             T("**Saved with the script.** Saving writes `scripts/taspir_tut/roffs/ravensClaw.rof` next to the script; **{{MissionSave}}** packs it into the `.pk3`."),
         ],
         [("note", T("Animating with move commands instead of a file: [[k-tuer]]. Changing a ROFF of the game: [[k-roff-bearbeiten]]."))]),

    teil("tut-p6", 6, T("6 · Cut, shake, follow, fade"),
         T("A third shot from outside the pad, a shake at lift-off, the camera turning after the ship, a fade to black - and the timing fixed in the timeline."),
         "t1_ergebnis",
         [
             T("**Cut.** After the second camera move insert a `wait` of 5 seconds, then a new shot from outside the pad, looking at the ship, with **{{CamDuration}}** `0` - a cut."),
             T("**Shake.** Insert `camera` and choose `SHAKE`: strength `4`, `2000` milliseconds - the engines start."),
             T("**Follow the ship.** Two `camera ( PAN )` commands with a duration turn the camera up after the ship while it climbs; a `wait` after each lets them finish."),
             T("**Fade out.** `camera` with `FADE`: from `0 0 0` and `0` to `0 0 0` and `1` in `1500` ms - black. Then `camera ( DISABLE )` gives control back to the player."),
             (T("**Fix the timing.** Play and watch. Drag blocks in the timeline: everything after moves along. Drag a right edge to change a duration."),
              "a_k_timen", T("Dragging a block in the timeline: everything after it moves along.")),
             (T("**The whole scene.** Turn on **{{MapThroughCam}}** and play from the start."),
              "a_kamera", T("The finished cutscene through the camera.")),
         ]),

    teil("tut-p7", 7, T("7 · A second script"),
         T("Kyle gets a spawn script with a macro - and the split view shows both scripts side by side."),
         "k_geteilt",
         [
             T("**A spawnscript key.** Select Kyle, add the key `spawnscript` with the value `taspir_tut/kyle_spawn` (**{{EntAddKey}}** in the inspector)."),
             T("**Open creates it.** Press **{{EntOpenScript}}** next to the key: the script does not exist yet, so behaved creates it in the mission folder and opens it in a tab."),
             (T("**A macro.** Double-click `standOnly` at the end of the **{{GroupEvents}}** list: three `set` commands that make Kyle stand and ignore enemies. Add `set ( SET_WEAPON, WP_SABER )`."),
              "k_makro", T("The spawn script with the standOnly macro.")),
             (T("**Side by side.** **{{MenuFile}} → {{SplitToggle}} → 2** shows the intro beside the spawn script. Drag lines from one to the other to copy them."),
              "k_geteilt", T("Two scripts side by side.")),
         ]),

    teil("tut-p8", 8, T("8 · Save and play"),
         T("Check the mission, pack it into a .pk3 and start it in the game."),
         "h_mission_speichern",
         [
             (T("**Save mission.** Press **{{MissionSave}}** in the map toolbar. The window lists every file it will pack and checks the mission: errors in red must be fixed, warnings in orange may stay."),
              "h_mission_speichern", T("The Save mission window."),
             [
                 (T("**{{MissionTarget}}**"), T("Where the `.pk3` is written; **{{MissionChoose}}** picks another place.")),
                 (T("**{{MissionSaveNow}}** / **{{MissionCancel}}**"), T("Saves, compiles and packs - or closes the window. Above them: every file of the package and the checks.")),
             ]),
             (T("**Pack.** Press **{{MissionSaveNow}}**: scripts are saved and compiled, the `.ent` with your entities and the ROFF are written, and `zz_taspir_tut.pk3` goes into the game folder."),
              "t4_5", T("The message after saving.")),
             T("**Play.** Start Jedi Academy, open the console (<kbd>Shift</kbd>+<kbd>~</kbd>) and type `devmap taspir_tut`. The cutscene runs as soon as you spawn."),
             (T("**Next time.** **{{MenuFile}} → {{RecentMissions}}** opens the whole mission again - map, entities and both scripts."),
              "h_menue_missionen", T("Recent missions in the File menu.")),
         ],
         [("note", T("Share the `.pk3` with others: it holds everything the mission needs. Players put it into their `base` folder."))]),
]

SEITEN = [UEBERSICHT] + TEILE
