"""Eine Seite je Befehl (rc601).

shank: "dass du jedes Event einmal durchgehst und Bilder machst, was jedes
Event-Element kann und wie es in den 3 Modi aussieht und genutzt wird" und
"auch mit Bildern, was jedes Command kann und wo man was eintraegt".

Jede Seite: was der Befehl tut, der Editor mit nummerierten Feldern und
einer Legende (was wohin gehoert), ein Beispiel, wie er in Classic, Graph und
Tracks aussieht (Umschalter oben), und was man wissen sollte. Die Bilder
b_<key>_ed / _c / _g / _t entstehen im Selbsttest, Abschnitt 10g - mit
denselben Beispielskripten, die hier als Beispiel stehen.
"""
from inhalt_basis import T, Tf

# Ob der Befehl in den Spuren einen Block hat (sonst steht er nur in der Liste).
_SPUR_BLOCK = ("**In Tracks** it is a block on the timeline, as long as it takes. Click it to select its line, double-click to edit it, "
               "drag its right edge to change the time, right-click for the menu.")
_SPUR_LISTE = ("**In Tracks** it takes no time of its own, so it has no block - it stands in the **{{TracksList}}** on the right. "
               "Click, double-click and right-click work there as in the Graph.")
_SPUR_FIGUR = ("**In Tracks** it shows in the row of the figure it belongs to (the name in its `affect`), as long as it takes. "
               "Double-click the block to edit it, right-click for the menu.")
_SPUR_PUNKT = ("**In Tracks** it takes no time: it is a short mark on the **Script** row at the moment it runs. "
               "Click it to select its line, double-click to edit it, right-click for the menu. It is also in the **{{TracksList}}** on the right.")
_SPUR_TASK = ("**In Tracks** it has no block of its own - it is in the **{{TracksList}}** on the right. You see what it does on the figure's row: "
              "what follows it starts where the task ends.")
_CLASSIC = ("**In Classic** it is one line of the script tree. Double-click it (or select it and press <kbd>Enter</kbd>) to open the editor; "
            "right-click for the menu; drag it to move it.")
_GRAPH = ("**In Graph** the script is a node; select it and its commands are listed on the right under **{{GraphCommands}}**. "
          "Double-click the line to edit it, right-click for the menu, drop a command from the **{{GroupEvents}}** list on it to insert after it.")
_BLOCK_CLASSIC = ("**In Classic** it is a line with a box in front: click the box to open or close the block. Commands inside are indented. "
                  "A command inserted while the block is open and selected goes inside it.")
_BLOCK_GRAPH = ("**In Graph** the block and its contents are listed on the right, the contents indented. "
                "Select the block line and double-click a command in the **{{GroupEvents}}** list to put it inside.")


def _seite(key, titel, kurz, was, felder, beispiel, spur, tipps, classic=_CLASSIC, graph=_GRAPH, bild_unter=None):
    befehl = titel.split(" ")[0]
    bloecke = [
        ("h2", "was", T("What it does")),
        ("p", T(was)),
        ("h2", "felder", T("Where to enter what")),
        ("img", "b_%s_ed" % key, T(bild_unter) if bild_unter else Tf("The event editor for `%s` - the numbers match the table below.", befehl)),
    ]
    if felder:
        bloecke.append(("legende", [(T(n), T(d)) for n, d in felder]))
    else:
        bloecke.append(("p", T("This command has no values - the editor only shows its help text. Press **{{EditorOk}}**.")))
    bloecke += [
        ("h2", "beispiel", T("Example")),
        ("p", T(beispiel)),
        ("h2", "ansichten", T("In Classic, Graph and Tracks")),
        ("p", T("The switch at the top of the page chooses the view. All three work on the same script - switching loses nothing.")),
        ("modus", {
            "classic": [("p", T(classic)), ("img", "b_%s_c" % key, Tf("`%s` in the script tree.", befehl))],
            "graph": [("p", T(graph)), ("img", "b_%s_g" % key, Tf("`%s` in the list of the selected script node.", befehl))],
            "tracks": [("p", T(spur)), ("img", "b_%s_t" % key, Tf("`%s` in Tracks.", befehl))],
        }),
    ]
    if tipps:
        bloecke += [("h2", "tipps", T("Good to know")), ("list", [T(t) for t in tipps])]
    return {"id": "cmd-" + key.replace("_", "-"), "gruppe": "befehle", "titel": T(titel), "kurz": T(kurz), "bloecke": bloecke}


_KAMERA_1 = ("Camera command", "The first field chooses what the camera does - here `%s`. Changing it changes the fields below.")

SEITEN_BEFEHLE = [
    # ----------------------------------------------------------- Ablauf
    _seite("affect", "affect - commands for one entity",
           "Sends the commands of its block to one entity: a figure, a door, a ship.",
           "Everything inside an `affect` block is done by the entity named in it - Kyle walks, the door moves, the ship plays its ROFF. "
           "The script itself does **not** wait for the block: two `affect` blocks one after the other run at the same time. "
           "With `FLUSH` the entity first drops what it was doing; with `INSERT` the new commands are added to what it already does.",
           [("Name", "The name of the entity - `NPC_targetname` of a figure, `targetname` or `script_targetname` of anything else. With a map loaded, the arrow lists every name of the map."),
            ("Type", "`FLUSH` - forget earlier commands and do these. `INSERT` - add these to what the entity is already doing.")],
           "`affect ( \"kyle\", FLUSH )` with `set ( SET_WALKING, true )` and `set ( SET_NAVGOAL, \"nav_kyle\" )` inside - Kyle walks to the walk target `nav_kyle`.",
           "**In Tracks** an `affect` block has no block of its own: it gives its figure a **row**, and the commands inside it are the blocks on that row. "
           "The `affect` line itself is in the **{{TracksList}}** on the right.",
           ["A name that is not on the map does nothing in the game - check the arrow list.",
            "Want the script to wait until a figure is done? Put the action into a `task` and use `dowait` - see [[k-tasks]]."],
           classic=_BLOCK_CLASSIC, graph=_BLOCK_GRAPH),
    _seite("flush", "flush - forget waiting commands",
           "Clears every command the entity has not done yet.",
           "Inside an `affect` block, `flush` throws away all commands still waiting for this entity - from this script or an earlier one. "
           "It is what `affect ( ..., FLUSH )` does at its start; on its own you use it in the middle of a block to start over.",
           [],
           "`flush ( )` at the top of Kyle's block, then the new walk.",
           _SPUR_LISTE,
           ["`flush` does not stop a figure that is already walking - give it a new goal or `SET_NAVGOAL` with an empty value."]),
    _seite("if", "if - only when a condition holds",
           "Runs its block only if a comparison is true.",
           "`if` compares two values. If the comparison holds, the commands in its block run; otherwise they are skipped (and an `else` right after it runs instead). "
           "Usually one side is a variable read with `get` - for example *has the player seen this scene before?*",
           [("First value", "A number, a text or an expression. **{{EditorHelper}}** → **{{EditorGet}}** reads a variable: `get ( FLOAT, \"seen\" )`."),
            ("Comparison", "`=` equal, `<` less, `>` greater, `!` not equal."),
            ("Second value", "What the first value is compared with - here `0`.")],
           "`if ( get( FLOAT, \"seen\" ), =, 0 )` - true the first time, when `seen` is still 0.",
           _SPUR_LISTE + " The timeline plays the commands inside as if the condition were true - it cannot know the value in the game.",
           ["Declare the variable first with `declare` - see [[cmd-declare]] and [[k-variablen]].",
            "ICARUS only knows `=`, `<`, `>` and `!` - an unknown comparison stops the compile."],
           classic=_BLOCK_CLASSIC, graph=_BLOCK_GRAPH),
    _seite("else", "else - otherwise",
           "Runs its block when the `if` right before it was false.",
           "`else` belongs to the `if` directly in front of it. Its block runs when that `if` did not. There must be nothing between the end of the `if` block and the `else`.",
           [],
           "After `if ( get( FLOAT, \"seen\" ), =, 0 ) { ... }` an `else ( ) { print ( \"Welcome back\" ) }` greets a returning player.",
           _SPUR_LISTE,
           ["An `else` without an `if` right before it is an error when you compile."],
           classic=_BLOCK_CLASSIC, graph=_BLOCK_GRAPH),
    _seite("loop", "loop - repeat a block",
           "Repeats the commands of its block a number of times, or forever.",
           "Everything inside a `loop` block runs again and again - three times with `3`, forever with `-1`. "
           "Use it for a guard's patrol, a blinking light or a repeated shake. Inside a forever loop, put a `wait` - otherwise the game freezes.",
           [("Count", "How often the block runs. `-1` means forever.")],
           "`loop ( 3 )` with a camera shake and `wait ( 1000 )` inside - three shakes, a second apart.",
           "**In Tracks** the commands inside the loop are blocks, repeated as often as the loop runs (a forever loop is shown a few times). The `loop` line itself is in the **{{TracksList}}**.",
           ["A forever loop in the intro never ends - the script never gets past it. Put it in an `affect` block of its own or in a separate script."],
           classic=_BLOCK_CLASSIC, graph=_BLOCK_GRAPH),
    _seite("run", "run - start another script",
           "Starts another script on the entity that runs this one.",
           "`run` starts the named script right away, and this script goes on at once. The script is named like the file below `scripts/`, without `.txt` or `.ibi`.",
           [("Script", "The path below `scripts/` - for example `taspir_tut/kyle_spawn`. **{{EditorBrowse}}** picks a file of the game, **{{EditorPlay}}** opens it.")],
           "`run ( \"taspir_tut/kyle_spawn\" )` starts Kyle's spawn script.",
           _SPUR_LISTE,
           ["To wait until the other script reaches a point, let it `signal` and use `waitsignal` here - see [[k-signale]]."]),
    _seite("wait", "wait - pause for a time",
           "Pauses the script for a number of milliseconds.",
           "The script stops here for the given time; then it goes on with the next command. 1000 is one second. "
           "Most of a cutscene's timing is waits: the camera starts a move, the script waits until the move is done, then the next shot.",
           [("Time", "Milliseconds: `1000` = one second. **{{EditorHelper}}** → **{{EditorRnd}}** builds `random ( 1000, 3000 )` for a different time each run.")],
           "`wait ( 2000 )` - two seconds, then the camera is switched off.",
           _SPUR_BLOCK + " Waits are on the **Script** row.",
           ["Inside an `affect` block, a `wait` delays only that entity - the script goes on.",
            "To wait until a figure has arrived, a fixed time is a guess - use a `task` and `dowait` ([[cmd-dowait]])."]),
    _seite("wait_task", "wait - until a task is done",
           "Pauses until a task started with `do` is finished.",
           "With a task name instead of a time, `wait` pauses until that task is done - for a walk: until the figure has arrived. "
           "Together with `do` it lets a figure do two things at once and still wait for one of them.",
           [("Task", "The name of a `task` of this entity that was started with `do`.")],
           "`do ( \"walk\" )`, then something else, then `wait ( \"walk\" )` - the script goes on when Kyle has arrived.",
           _SPUR_TASK,
           ["`dowait` is the short form of `do` + `wait` - [[cmd-dowait]]."],
           bild_unter="The event editor for `wait` with a task name."),
    _seite("waitsignal", "waitsignal - wait for another script",
           "Pauses until some other script calls `signal` with the same name.",
           "Two scripts can wait for each other: this one stops at `waitsignal ( \"door_open\" )` until another script reaches `signal ( \"door_open\" )`. "
           "Only one script can wait for a given name at a time.",
           [("Signal", "Any name - it must be spelled exactly like the one in `signal`.")],
           "The intro waits with `waitsignal ( \"door_open\" )` until the door script says `signal ( \"door_open\" )`.",
           _SPUR_LISTE + " The timeline cannot know how long it waits in the game - everything after it follows directly.",
           ["See [[k-signale]] for both sides together."]),
    _seite("signal", "signal - wake a waiting script",
           "Lets the script that waits for this name go on.",
           "`signal` is the other half of `waitsignal`: when this line runs, the script waiting for the same name continues.",
           [("Signal", "The same name as in `waitsignal`.")],
           "The door script: `use ( \"bldg2_ext_door\" )`, `wait ( 1500 )`, `signal ( \"door_open\" )`.",
           _SPUR_PUNKT,
           ["A signal that nobody waits for is lost - the waiting script must already be at its `waitsignal`."]),
    _seite("sound", "sound - play a sound or a line",
           "Plays a sound on a channel of the entity.",
           "Inside a figure's `affect` block, `sound` makes that figure speak or make a noise. On the channel `CHAN_VOICE` the mouth moves with the line, "
           "and the subtitle of the game is shown.",
           [("Channel", "`CHAN_VOICE` for dialogue (lips move), `CHAN_AUTO` or `CHAN_BODY` for other sounds."),
            ("File", "The sound below `sound/` - **{{EditorBrowse}}** picks one from the game, **{{EditorPlay}}** plays it.")],
           "`sound ( CHAN_VOICE, \"sound/chars/kyle/01kyk001.mp3\" )` in Kyle's block.",
           "**In Tracks** a `CHAN_VOICE` sound is a block on the **Dialogue** row, as long as the file plays. Double-click it to edit it.",
           ["Dialogue: [[k-dialog]]."]),
    _seite("move", "move - move an entity",
           "Moves a door, a platform or a ship to a position in so many milliseconds.",
           "`move` slides the entity of the current `affect` block to a new position - and, if given, to new angles - over the given time. "
           "The **Animate** window writes these for you from keys you set in the view.",
           [("Position", "Where it goes: three numbers `x y z`, relative to where the entity was built in the map."),
            ("Angles", "Pitch, yaw and roll at the end - `0 0 0` keeps them."),
            ("Time", "Milliseconds for the whole move.")],
           "`move ( < 0 0 128 >, < 0 0 0 >, 2000 )` in the door's block - the door rises 128 units in two seconds.",
           _SPUR_LISTE + " The view plays the movement, but a door or a platform has no row of its own in Tracks.",
           ["A door that opens: [[k-tuer]]."]),
    _seite("rotate", "rotate - turn an entity",
           "Turns the entity to angles in so many milliseconds.",
           "`rotate` turns the entity of the current `affect` block to the given angles (pitch, yaw, roll) over the given time.",
           [("Angles", "Pitch, yaw and roll - `0 90 0` turns it a quarter round."), ("Time", "Milliseconds.")],
           "`rotate ( < 0 90 0 >, 1000 )` in the door's block.",
           _SPUR_LISTE + " The view plays the movement, but a door or a platform has no row of its own in Tracks.",
           []),
    _seite("use", "use - use an entity",
           "Uses an entity by name, as if the player pressed it.",
           "`use` triggers the entity: a door opens, a trigger fires, a spawner spawns its figure, a script runner starts its script.",
           [("Name", "The `targetname` of the entity - the arrow lists the names of the map.")],
           "`use ( \"bldg2_ext_door\" )` opens the door of the building.",
           _SPUR_PUNKT,
           ["The second `use` in the list takes an expression: `use ( get( STRING, \"door\" ) )` uses whatever name the variable holds."]),
    _seite("kill", "kill - kill an entity",
           "Kills the entity with this name.",
           "A figure dies (and its `deathscript` runs), a breakable object breaks.",
           [("Name", "The name of the entity.")],
           "`kill ( \"rosh\" )`.",
           _SPUR_PUNKT,
           ["To make a figure just disappear, use `remove`."]),
    _seite("remove", "remove - take an entity out of the game",
           "Removes the entity with this name - no death, it is simply gone.",
           "Use it to clean up after a cutscene: figures that should not be there when the player gets control.",
           [("Name", "The name of the entity.")],
           "`remove ( \"rosh\" )`.",
           _SPUR_PUNKT,
           []),
    _seite("print", "print - text on the screen",
           "Shows a text in the middle of the screen.",
           "A line of text for the player - a time jump, a hint, a place name.",
           [("Text", "The text to show.")],
           "`print ( \"Three days later...\" )`.",
           _SPUR_PUNKT,
           ["Text in the game's string files (`@@`) is translated by the game."]),
    _seite("rem", "rem - a comment",
           "A note in the script - it does nothing in the game.",
           "`rem` keeps a note for you or the next person: what this part of the script does. **{{ActRem}}** turns any line into a comment and back.",
           [("Comment", "Any text.")],
           "`rem ( \"The ship takes off here\" )`.",
           _SPUR_PUNKT,
           []),
    # ----------------------------------------------------------- Variablen
    _seite("declare", "declare - create a variable",
           "Declares a variable that scripts can set and read.",
           "A variable remembers something during the level - for example whether a scene was already seen. Declare it once, in a script that runs once. "
           "It starts at 0 or empty. A map can hold 16 variables.",
           [("Type", "`FLOAT` for numbers, `STRING` for text, `VECTOR` for three numbers."),
            ("Name", "Any name - you use it again in `set`, `get` and `free`.")],
           "`declare ( FLOAT, \"seen\" )`.",
           _SPUR_PUNKT,
           ["See [[k-variablen]] for declare, if, set and free together."]),
    _seite("free", "free - give a variable back",
           "Frees a declared variable so the name and its place can be used again.",
           "Only 16 variables fit; `free` gives one back when it is no longer needed.",
           [("Name", "The name used in `declare`.")],
           "`free ( \"seen\" )`.",
           _SPUR_PUNKT,
           []),
    _seite("set", "set - change a field of an entity",
           "Changes a value of the entity: how it walks, where it goes, what it looks at, its animation, its health ...",
           "`set` with a `SET_` field is the most used command of all. Inside an `affect` block it changes that entity: `SET_NAVGOAL` makes a figure walk to a walk target, "
           "`SET_WALKING` makes it walk instead of run, `SET_LOOK_TARGET` turns its head, `SET_ANIM_BOTH` plays an animation. Every field is in [[befehle#set]].",
           [("Field", "Which value - a `SET_` name from the list. Type part of it to filter."),
            ("Value", "The new value. Its kind depends on the field: `true` / `false`, a number, a name from the map, an animation.")],
           "`set ( SET_WALKING, true )` and `set ( SET_NAVGOAL, \"nav_kyle\" )` - Kyle walks to `nav_kyle`.",
           _SPUR_FIGUR + " A walk (`SET_NAVGOAL`) lasts until the figure arrives; most other fields take no time.",
           ["A field marked `*` in its help text (like `SET_NAVGOAL`) can be waited for with `dowait`.",
            "A value that is not in the list is allowed (mods bring their own) but marked **unknown** - check it for a typo."]),
    _seite("set_var", "set - change a variable",
           "Gives a declared variable a new value.",
           "The second `set` in the list sets a variable by its name instead of an entity field.",
           [("Variable", "The name used in `declare`."), ("Value", "The new value, for example `1`.")],
           "`set ( \"seen\", \"1\" )` at the end of the scene - next time the `if` is false.",
           _SPUR_PUNKT,
           [],
           bild_unter="The event editor for `set` with a variable."),
    # ----------------------------------------------------------- Tasks
    _seite("task", "task - a named group of commands",
           "Declares a group of commands that runs later, with `do` or `dowait`.",
           "A `task` does nothing where it stands. It gives a name to its block, so that `do` can start it and `wait` or `dowait` can wait until it is finished - "
           "the only way to wait until a figure has really arrived somewhere.",
           [("Name", "Any name, unique inside this `affect` block - for example `walk`.")],
           "`task ( \"walk\" )` with `SET_WALKING` and `SET_NAVGOAL` inside, then `dowait ( \"walk\" )`.",
           _SPUR_LISTE + " The commands inside appear on the figure's row when the task is started.",
           ["See [[k-tasks]]."],
           classic=_BLOCK_CLASSIC, graph=_BLOCK_GRAPH),
    _seite("do", "do - start a task",
           "Starts a task and goes on at once.",
           "`do` starts the named task, and the next command follows right away - so a figure can walk and look around at the same time.",
           [("Task", "The name of a `task` in this block.")],
           "`do ( \"walk\" )`, `do ( \"look\" )`, then `wait ( \"walk\" )`.",
           _SPUR_TASK,
           []),
    _seite("dowait", "dowait - start a task and wait for it",
           "Starts a task and waits until it is finished.",
           "`dowait` is `do` and `wait` in one line. For a walk task it waits until the figure has arrived - everything after it happens when the figure stands.",
           [("Task", "The name of a `task` in this block.")],
           "`dowait ( \"walk\" )`, then `set ( SET_LOOK_TARGET, \"rosh\" )` - Kyle looks at Rosh when he has arrived.",
           _SPUR_TASK,
           []),
    _seite("play", "play - play a ROFF file",
           "Plays a recorded movement (ROFF) on the entity of the current `affect` block.",
           "A ROFF file is a movement over time - a ship that lifts off and flies away, a falling pillar. **Animate** writes one for you from keys.",
           [("Type", "`PLAY_ROFF`."), ("File", "The ROFF below `scripts/` - without `.rof`.")],
           "`play ( PLAY_ROFF, \"taspir_tut/roffs/ravensClaw\" )` in the ship's block.",
           _SPUR_FIGUR,
           ["Make and change ROFF files: [[k-roff-bearbeiten]]."]),
]

# ----------------------------------------------------------- camera ( ... )
def _kamera(key, cmd, titel, kurz, was, felder, beispiel, spur, tipps):
    return _seite(key, "camera %s - %s" % (cmd, titel), kurz, was,
                  [(_KAMERA_1[0], _KAMERA_1[1] % cmd)] + felder, beispiel, spur, tipps,
                  bild_unter="The event editor for `camera ( %s )`." % cmd)


_KAM_SPUR = ("**In Tracks** the camera row shows the shots - from one `MOVE` to the next. This command is listed in the **{{TracksList}}**; "
             "the timeline under the map shows it in its own camera row.")

SEITEN_KAMERA = [
    _kamera("cam_enable", "ENABLE", "the cutscene starts", "Puts the game into camera mode.",
            "From here on the player's view is replaced by the script's camera, the HUD disappears and the player cannot move. Every cutscene starts with it.",
            [], "`camera ( ENABLE )` - the first camera line of the intro.",
            "**In Tracks** it has no block of its own - it is in the **{{TracksList}}**. The **Camera** row starts with the first `MOVE` after it.",
            ["Without a `MOVE` and `PAN` right after it, the camera stays where the player was."]),
    _kamera("cam_disable", "DISABLE", "back to the game", "Takes the game out of camera mode.",
            "The player gets control back, the HUD returns. The last line of every cutscene.",
            [], "`camera ( DISABLE )` after the last wait.",
            "**In Tracks** it has no block of its own - it is in the **{{TracksList}}**. The last shot on the **Camera** row ends here.",
            ["Forgetting it leaves the player stuck in the cutscene."]),
    _kamera("cam_move", "MOVE", "place or fly the camera", "Moves the camera to a position - at once or over time.",
            "With time `0` the camera jumps there (a cut); with a time it flies there. Together with `PAN` it makes a shot.",
            [("Position", "`x y z` - or **{{EditorHelper}}** → **{{EditorTag}}** for a camera mark (`ref_tag`) on the map: `tag ( \"cam1\", ORIGIN )`."),
             ("Time", "Milliseconds for the flight; `0` jumps.")],
            "`camera ( MOVE, < -750 -2700 1000 >, 3000 )` - three seconds' flight.",
            "**In Tracks** every `MOVE` starts a new shot on the **Camera** row; a move with time is marked *moving*. Double-click the shot to edit its `MOVE`.",
            ["**Insert camera here** in the timeline writes `MOVE` and `PAN` from the view - [[k-kamera-ansicht]]."]),
    _kamera("cam_pan", "PAN", "turn the camera", "Turns the camera to angles - at once or over time.",
            "`PAN` sets where the camera looks: pitch (up/down), yaw (left/right), roll. With a time it turns slowly.",
            [("Angles", "Pitch, yaw, roll the camera ends at."),
             ("Direction", "Which way to turn - `0 0 0` takes the shortest way."),
             ("Time", "Milliseconds; `0` turns at once.")],
            "`camera ( PAN, < 4 120 0 >, < 0 0 0 >, 3000 )`.",
            _KAM_SPUR, []),
    _kamera("cam_zoom", "ZOOM", "change the field of view", "Changes how much the camera sees.",
            "A smaller field of view zooms in, a larger one zooms out. Normal is 90 (after `ENABLE`); 80 is the player's own view before `ENABLE` and after `DISABLE`.",
            [("Field of view", "Degrees - Raven's scripts use 6.3 (far zoomed in) to 110 (wide). Normal: 90."), ("Time", "Milliseconds to get there.")],
            "`camera ( ZOOM, 40, 2000 )` - zoom in over two seconds.",
            _KAM_SPUR, ["How wide a zoom looks also depends on the game screen (aspect ratio, cg_fovAspectAdjust): [[kamera#vorschau]]."]),
    _kamera("cam_roll", "ROLL", "tilt the picture", "Rolls the camera to an angle.",
            "Tilts the picture to the side - for a dizzy or dramatic shot.",
            [("Angle", "The roll angle the camera ends at (not added to the current roll)."), ("Time", "Milliseconds; with a time it takes the shorter way there.")],
            "`camera ( ROLL, 15, 1000 )`.",
            _KAM_SPUR, []),
    _kamera("cam_track", "TRACK", "ride a camera path", "Puts the camera on a track of the map and moves along it.",
            "A track is a chain of path corners in the map. The camera rides along it at a speed.",
            [("Track", "The `targetname` of the first path corner."), ("Speed", "Units per second."),
             ("Lerp", "`1`: glide to the start of the track first; `0`: jump there.")],
            "`camera ( TRACK, \"cam_track\", 100, 1 )`.",
            _KAM_SPUR, ["Camera paths: [[k-blickpunkte]]."]),
    _kamera("cam_follow", "FOLLOW", "keep figures in view", "Turns the camera to follow entities of a camera group.",
            "The camera keeps looking at every entity whose `cameraGroup` matches - it turns as they move.",
            [("Camera group", "The `cameraGroup` value of the entities to follow (or a name)."), ("Angle speed", "How fast the camera may turn."),
             ("Lerp", "`1`: turn smoothly to the start angle; `0`: snap.")],
            "`camera ( FOLLOW, \"kyle\", 50, 1 )`.",
            _KAM_SPUR, ["See [[k-kamera-folgen]]."]),
    _kamera("cam_distance", "DISTANCE", "keep a distance", "Keeps the camera at a distance from its camera group.",
            "Used with `FOLLOW`: the camera also moves to stay this far from the group.",
            [("Distance", "Units."), ("Lerp", "`1`: move smoothly; `0`: jump.")],
            "`camera ( DISTANCE, 200, 1 )`.",
            _KAM_SPUR, []),
    _kamera("cam_fade", "FADE", "fade to or from a colour", "Fades the picture from one colour and opacity to another.",
            "Black at the end of a scene, white for a flash, or back from black at the start.",
            [("Start colour", "Red, green, blue from 0 to 1 - `0 0 0` is black."), ("Start opacity", "0 = clear picture, 1 = full colour."),
             ("End colour", "Red, green, blue at the end."), ("End opacity", "0 to 1."), ("Time", "Milliseconds.")],
            "`camera ( FADE, < 0 0 0 >, 0, < 0 0 0 >, 1, 1500 )` - fade to black in 1.5 seconds.",
            _KAM_SPUR, ["Zoom, fade and shake: [[k-zoom-blende]]."]),
    _kamera("cam_shake", "SHAKE", "shake the picture", "Shakes the camera for a while.",
            "An explosion, an engine starting, a ship taking off.",
            [("Intensity", "0 to 16."), ("Time", "Milliseconds.")],
            "`camera ( SHAKE, 4, 2000 )`.",
            _KAM_SPUR, []),
    _kamera("cam_path", "PATH", "fly a recorded path", "Plays a ROFF file as the camera's path.",
            "A recorded camera flight - made in an editor or with **Animate** - is played as the camera's movement. The path starts where the camera is; BehavEd-Remake plays it like the game, including the notes `cut`, `fov`, `fovzoom` and `fovaccel`. A `MOVE` ends the path.",
            [("File", "The ROFF file below `scripts/`, without `.rof`.")],
            "`camera ( PATH, \"taspir_tut/roffs/campath\" )`.",
            _KAM_SPUR, []),
]

# Die Uebersicht der Befehle - verlinkt jede Seite.
UEBERSICHT = {
    "id": "cmd",
    "gruppe": "befehle",
    "titel": T("Commands one by one"),
    "kurz": T("One page per command: what it does, where to enter what in the editor, an example, and how it looks in Classic, Graph and Tracks."),
    "bloecke": [
        ("p", T("The full list of every `SET_` field, every camera command and every macro is on [[befehle]]. Here, each command of the **{{GroupEvents}}** list has its own page with pictures.")),
        ("index", [
            (T("Flow"), ["cmd-affect", "cmd-flush", "cmd-if", "cmd-else", "cmd-loop", "cmd-run"]),
            (T("Waiting"), ["cmd-wait", "cmd-wait-task", "cmd-waitsignal", "cmd-signal"]),
            (T("Entities"), ["cmd-set", "cmd-sound", "cmd-move", "cmd-rotate", "cmd-use", "cmd-kill", "cmd-remove", "cmd-play"]),
            (T("Tasks"), ["cmd-task", "cmd-do", "cmd-dowait"]),
            (T("Variables and text"), ["cmd-declare", "cmd-set-var", "cmd-free", "cmd-print", "cmd-rem"]),
            (T("Camera"), ["cmd-" + s for s in ("cam-enable", "cam-move", "cam-pan", "cam-zoom", "cam-roll", "cam-track", "cam-follow",
                                                  "cam-distance", "cam-fade", "cam-shake", "cam-path", "cam-disable")]),
        ]),
    ],
}

SEITEN = [UEBERSICHT] + SEITEN_BEFEHLE + SEITEN_KAMERA
