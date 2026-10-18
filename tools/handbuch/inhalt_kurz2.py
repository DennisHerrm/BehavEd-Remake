"""Kurze Tutorials, zweite Runde (rc600): Trigger, Tueren, Kampf, Variablen,
Patrouille, Kamera folgt, Video, Spieler sperren, Lichtschwert, Levelwechsel,
Effekt und Musik, Spielszene aendern, Multiplayer, Oberflaeche.

shank: "mach alle" (zur Liste der Tutorial-Ideen) - und: "du zeigst, wie
man den ehemaligen Map-Tab dafuer benutzt?" Ja: jedes beginnt im Tab
{{ViewMap}} (frueher "Map"). Englisch zuerst.

Fakten geprueft an data/base/Q3_Interface.h (SET_-Felder), tools/entdefs.txt
(Schluessel der Entities aus dem Spielcode) und OpenJK g_trigger.cpp
(trigger_entdist prueft nur, wenn es benutzt wird - darum hier kein
"Punkt-Trigger").
"""
from inhalt_basis import T
from inhalt_kurz import tut

SKRIPT2 = [
    tut("k-variablen", "tut_skript",
        T("Only the first time: variables and if"),
        T("Remember that something has happened - declare a variable, set it, ask for it with if."),
        T("Scripts · five steps"), "k_var",
        [
            T("**Declare once.** In a script that runs only once - the intro, a spawn script - insert `declare`, choose `FLOAT` and type a name, for example `seen`. A declared variable starts at 0; a map can hold 16 of them."),
            (T("**Ask with if.** In the script that may run more often (a trigger's script, see [[k-trigger]]) insert `if`. In the editor press **{{EditorHelper}}** on the first value and build `get ( FLOAT, \"seen\" )` with **{{EditorGet}}**; as comparison choose `=`, as the other value `0`."),
             "k_var_if", T("The event editor for if: get ( FLOAT, \"seen\" ), =, 0.")),
            T("**Do it inside.** Put the commands of the scene into the `if` block. An `else` right after it runs when the scene was already seen."),
            (T("**Remember.** At the end of the `if` block insert `set` with the variable name `seen` and the value `1` - next time the `if` is false."),
             "k_var", T("The finished if / else with declare and set.")),
            T("**Free it** with `free ( \"seen\" )` when it is no longer needed, so the name is available again."),
        ],
        [T("Comparisons are `=`, `<`, `>` and `!` (not equal) - behaved warns about anything else.")]),

    tut("k-patrouille", "tut_skript",
        T("A guard walks his round"),
        T("A loop around walk targets: the figure walks from point to point, waits, and starts again - forever."),
        T("Scripts · five steps"), "k_runde",
        [
            T("**Place the round.** In the **{{ViewMap}}** tab place three or four walk targets with the tile **{{EntKindNav}}** (<kbd>Shift</kbd> places several) and name them `round1`, `round2`, `round3`."),
            T("**The guard.** Place a character (**{{EntKindNpc}}**), name it `guard`, and give it an `affect ( \"guard\", FLUSH )` block in the script. Inside, insert the macro `walkOnly` first."),
            T("**The loop.** Insert `loop` and keep `-1` - forever."),
            (T("**One leg per point.** Inside the loop: a `task` with `set ( SET_NAVGOAL, round1 )`, then `dowait` for it, then `wait ( 2000 )`. Copy these three lines for each point and change the name. Play the timeline - the guard walks the round."),
             "k_runde_baum", T("The guard's loop: a task, dowait and wait for each point.")),
            (T("**Run instead.** Replace `walkOnly` with `runOnly` - or right-click a walk target on the path and choose **{{BahnRennen}}**."),
             "k_runde", T("The guard and his three walk targets on the pad.")),
        ],
        [T("The macros `patrolRun`, `patrolNoChase` and `patrolWalkNoChase` set how a patrolling guard reacts to enemies.")]),
]

KAMERA2 = [
    tut("k-kamera-folgen", "tut_kamera",
        T("Let the camera follow a figure"),
        T("camera FOLLOW turns the camera after everyone of a camera group - and DISTANCE keeps it close."),
        T("Camera · four steps"), "k_folgen_blick",
        [
            T("**Put the figure into a group.** In the figure's `affect` block insert `set ( SET_CAMERA_GROUP, \"hero\" )`."),
            T("**Follow.** After the camera shot insert `camera` and choose `FOLLOW`: the group `hero`, a turning speed (for example `100`) and `1` to blend into it. The camera now keeps turning towards the figure."),
            T("**Keep a distance.** `camera ( DISTANCE, 200, 1 )` keeps the camera 200 units from the group, moving with it."),
            (T("**Watch it.** Turn on **{{MapThroughCam}}** and play the timeline: the view keeps turning towards the figure. In the timeline, the **Follow** row under **{{TlKamera}}** shows how long it lasts."),
             "k_folgen_blick", T("Through the camera: it keeps turning towards Kyle.")),
        ],
        [T("`SET_CAMERA_GROUP_Z_OFS` aims higher or lower on the followed figure.")]),

    tut("k-video-text", "tut_kamera",
        T("Show a video or a line of text"),
        T("SET_VIDEO_PLAY for a .roq film in the middle of a scene, print for a short text on screen."),
        T("Camera · three steps"), "k_video",
        [
            (T("**A video.** Insert `set` and choose `SET_VIDEO_PLAY`; as the value type the name of a film in `video/` without its ending - Jedi Academy plays `.roq` files this way, for example its intro `jk0101_sw`."),
             "k_video_edit", T("SET_VIDEO_PLAY in the event editor.")),
            T("**Fade it.** `SET_VIDEO_FADE_IN` and `SET_VIDEO_FADE_OUT` (`true`) set before the video let it fade."),
            (T("**Text.** `print ( \"Three days later...\" )` shows a line in the middle of the screen."),
             "k_video", T("Video, fade and a line of text in the tree.")),
        ],
        [T("behaved does not play videos in the timeline; it shows the command as a block so the timing stays visible.")]),
]

FIGUREN2 = [
    tut("k-kampf", "tut_figur",
        T("Start a fight after the cutscene"),
        T("Keep an enemy calm while the camera runs, then let him attack the player."),
        T("Figures · five steps"), "k_kampf",
        [
            T("**Calm at first.** Give the enemy a spawn script (key `spawnscript`, **{{EntOpenScript}}** creates it) with the macro `standOnly` - he stands and ignores enemies."),
            T("**The scene.** The cutscene runs as usual; the enemy can walk and talk like any figure."),
            T("**End the camera.** At the end of the intro: `camera ( DISABLE )` - the player plays again."),
            (T("**Attack.** Right after it, an `affect ( \"enemy\", FLUSH )` block with `set ( SET_IGNOREENEMIES, false )`, `set ( SET_LOOK_FOR_ENEMIES, true )` and `set ( SET_ENEMY, \"player\" )`. The macro `default` resets all behaviour in one line."),
             "k_kampf", T("After camera ( DISABLE ): the enemy turns on the player.")),
            T("**More than one.** Copy the block for each enemy, or put the lines into a script of their own and start it with `run`."),
        ],
        [T("`SET_BEHAVIOR_STATE` with `BS_DEFAULT` gives the figure back to its normal AI after scripted walking.")]),

    tut("k-spieler-sperren", "tut_figur",
        T("Hold the player during a talk"),
        T("Lock the player in place - without a cutscene camera - and free him again."),
        T("Figures · three steps"), "k_sperren",
        [
            T("**Lock.** Insert `affect ( \"player\", FLUSH )` and inside it `set ( SET_PLAYER_LOCKED, true )`. `SET_LOCK_PLAYER_WEAPONS` keeps the weapon as well."),
            T("**Talk.** The other figures speak and move as usual; the player watches from his own view."),
            (T("**Free.** At the end the same block with `false` - otherwise the player stays frozen."),
             "k_sperren", T("Lock and free the player around a talk.")),
        ],
        [T("With `camera ( ENABLE )` the game takes the controls away by itself; the lock is for moments without a cutscene camera.")]),

    tut("k-lichtschwert", "tut_figur",
        T("Ignite a lightsaber in a scene"),
        T("Give a figure its saber and switch the blade on and off at the right moment."),
        T("Figures · three steps"), "k_saber",
        [
            T("**The weapon.** In the figure's `affect` block (or its spawn script): `set ( SET_WEAPON, WP_SABER )`."),
            (T("**On.** Where it should ignite: `set ( SET_SABERACTIVE, true )`. In the timeline the blade appears at that moment - with its sound when **{{TlAudio}}** is on and the camera can see the figure, as in the game."),
             "k_saber", T("Kyle with his blade ignited, in the view.")),
            (T("**Off.** `set ( SET_SABERACTIVE, false )`."),
             "k_saber_baum", T("The saber commands in Kyle's affect block.")),
        ],
        [T("`SET_SABER1` gives the figure another saber by its name from the game's saber files."),
         T("**Walls.** A blade that touches a wall is shortened there, with sparks, sound and a burn mark - [[figuren#schwerter]]."),
         T("**Clash.** For two blades to clash in Movie Duels, put both NPCs in `BS_CINEMATIC` and add `set ( SET_SABERCOLLISIONS, true )` before the attack animation. Under OpenJK rules only the animations `BOTH_CIN_16` and `BOTH_CIN_17` clash.")]),
]

KARTE2 = [
    tut("k-trigger", "tut_karte",
        T("Start a scene when the player walks somewhere"),
        T("Point a trigger of the map at a script runner of your own - the scene starts when the player enters the area."),
        T("Map · five steps"), "k_trigger",
        [
            T("**A runner.** In the **{{ViewMap}}** tab click the tile **{{EntKindRunner}}** and click near the place. behaved names it and sets its `usescript` to a script in your mission folder."),
            T("**Its script.** In the inspector press **{{EntOpenScript}}** next to `usescript`: the script is created and opened. Write the scene in it."),
            T("**Find a trigger.** Triggers are brushes of the map: type `trigger` into the search box of the entity list and pick one where the player passes. Fly there with a double-click."),
            (T("**Point it at the runner.** In the trigger's inspector set its key `target` to the runner's name. The **{{FlowGraph}}** view now shows the arrow from trigger to runner to script."),
             "k_trigger", T("The trigger's keys: target now names the runner.")),
            T("**Once or every time.** A runner runs once; its key `count` set to `-1` runs it every time. For only-the-first-time logic see [[k-variablen]]."),
        ],
        [T("New trigger areas need brushes, which are made in a map editor - behaved can change every trigger of a map, but not draw a new one. A script can also start a runner directly with `use`.")]),

    tut("k-tuer-use", "tut_karte",
        T("Open a door from the script"),
        T("use opens a door, fires a trigger or starts a runner - by its name."),
        T("Map · four steps"), "k_tuer_menue",
        [
            T("**Find the door.** Doors are in the group **Movable (func_*)** of the entity list. Click one to see it in the inspector."),
            T("**Its name.** The door needs a `targetname`. A door with a name waits to be used instead of opening when someone walks up - which is what a scripted door should do."),
            (T("**Insert use.** Select the line in the script after which the door should open, then right-click the door in the entity list → **{{EntityUse}}**: `use ( \"door1\" )` is inserted."),
             "k_tuer_menue", T("Right-click a door in the entity list: Insert use.")),
            T("**Hear it.** The door plays its own sounds (its `soundSet`). Play the timeline to check the moment."),
        ],
        [T("Moving the door yourself instead - other distance, other speed: [[k-tuer]].")]),

    tut("k-levelwechsel", "tut_karte",
        T("Go to the next map at the end"),
        T("A level change entity, used by the last line of the script."),
        T("Map · three steps"), "k_level",
        [
            (T("**Place it.** **{{EntNew}}** → **{{EntKindLevel}}**. In the inspector set `mapname` to the next map (for example `taspir2`) and give it a **{{EntName}}** such as `next_map`."),
             "k_level", T("The level change with mapname and a name.")),
            T("**Use it.** At the end of the script: `use ( \"next_map\" )` (right-click the entity → **{{EntityUse}}**)."),
            T("**Check it.** **{{MissionSave}}** warns when a target points to nothing; play the mission to see the loading screen."),
        ]),

    tut("k-effekt-musik", "tut_karte",
        T("Add an effect and change the music"),
        T("An fx_runner that starts on cue, and a music change from the script."),
        T("Map · four steps"), "k_effekt",
        [
            T("**An effect.** Click the tile **{{EntKindFx}}** and click where it should appear. In the inspector set `fxFile` to an effect of the game (a path under `effects/`, for example `env/fire`), give it a name, and tick `STARTOFF` so it waits."),
            (T("**Start it.** In the script: `use ( \"fire1\" )` at the right moment. **{{MapEffects}}** on the toolbar shows effects in the view."),
             "k_effekt", T("A fire effect on the landing pad.")),
            T("**Music.** **{{EntNew}}** → **{{EntKindMusic}}**, set `music` to a music file and a name; `use` it from the script to switch the music."),
            T("**Dynamic music.** `set ( SET_MUSIC_STATE, ... )` switches the game's own music between its states, for example to action."),
        ]),
]

MISSION2 = [
    tut("k-szene-aendern", "tut_mission",
        T("Change a cutscene of the game"),
        T("Open a mission of Jedi Academy, change a camera shot of its intro, and pack it as your own version."),
        T("Missions · five steps"), "h_mission_wahl",
        [
            (T("**Open it.** **{{MenuFile}} → {{LoadMission}}**, choose `assets0.pk3` in the game's `base` folder and pick a map, for example `taspir1`. The map and all its scripts open in tabs."),
             "h_mission_wahl", T("The missions of an archive.")),
            T("**Find the scene.** In the **{{ViewMap}}** tab, **Scripts (N)** lists the scripts the map uses - pick the intro. The timeline shows its camera."),
            T("**Change a shot.** Select a shot in the **{{CamShots}}** list, fly to a better view and press **{{CamSetToView}}** - or change a time in the timeline."),
            T("**Pack it.** **{{MissionSave}}** writes the changed scripts with the map into `zz_<map>.pk3`. Archives load in alphabetical order, so `zz_` wins over the game's own files."),
            T("**Undo it.** Delete the `zz_` file from the game folder and the game plays its original again - the game files are never touched."),
        ]),

    tut("k-multiplayer", "tut_mission",
        T("Check a script for multiplayer"),
        T("Switch the target game and read what works differently on a multiplayer server."),
        T("Missions · three steps"), "k_mp",
        [
            T("**Switch.** **{{MenuView}} → {{ViewTarget}}** → multiplayer. An **MP** badge appears in the status line."),
            (T("**Read the notes.** Click the notes counter in the status line: the **{{MessagesTitle}}** window lists every command that does nothing or works differently on a server - checked against the multiplayer game code."),
             "k_mp", T("The Messages window with the multiplayer notes.")),
            T("**Switch back** to single player when you are done; the script itself is not changed."),
        ]),
]

OBERFLAECHE = [
    tut("k-oberflaeche", "tut_mission",
        T("Set up the program your way"),
        T("Shortcuts, theme, language, interface size and the column layout."),
        T("Settings · five steps"), "k_tasten",
        [
            (T("**Shortcuts.** **{{MenuEdit}} → {{KeysTitle}}**: click a shortcut, press the new key. <kbd>Esc</kbd> cancels, <kbd>Backspace</kbd> clears; a clash shows which action already has the key."),
             "k_tasten", T("The Keyboard shortcuts window.")),
            T("**Theme and language.** **{{MenuView}} → {{ViewTheme}}** (six themes, among them BehavEd Classic and High Contrast) and **{{ViewLanguage}}**."),
            T("**Size.** **{{MenuView}} → {{ViewDpiScale}}**, or <kbd>Ctrl</kbd>+mouse wheel anywhere outside the timeline."),
            T("**Columns.** Drag the dividers between the columns; each mode keeps its own widths. **{{MenuDebug}} → Restore default layout** brings them back."),
            T("**The guide.** <kbd>F1</kbd> opens this guide - also rebindable."),
        ],
        weiter=T("All settings: [[einstellungen]]. All keys: [[tasten]].")),
]

GRUPPEN_ZUSATZ = {}
SEITEN = SKRIPT2 + KAMERA2 + FIGUREN2 + KARTE2 + MISSION2 + OBERFLAECHE
