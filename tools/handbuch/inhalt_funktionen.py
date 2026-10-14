"""Einstieg: was das Programm alles kann - ein Rundgang mit Bildern.

shank: "und Erklaerungen, was das Programm alles kann?". Jeder Abschnitt
zeigt einen Bereich und fuehrt zu den Tutorials, die ihn ueben. Die Bilder
stammen aus der Taspir-Mission des grossen Tutorials. Englisch zuerst.
"""
from inhalt_basis import T

SEITE = {
    "id": "funktionen",
    "gruppe": "start",
    "titel": T("What BehavEd can do"),
    "kurz": T("A tour of the whole program: writing scripts, seeing them on the map, cameras, figures, animation, missions and the tools around them."),
    "bloecke": [
        ("p", T("BehavEd-Remake writes ICARUS scripts - the scripts behind every cutscene and every scripted moment of Jedi Academy and its mods. It writes them exactly like Raven's original BehavEd, file for file, and then shows you what they do: on the map, in 3D, over time. Each section below links to the tutorials that practise it.")),

        ("h2", "skripte", T("Write scripts like the original")),
        ("img", "h_skriptbaum", T("The command list, the script tree and the action column."), "breit"),
        ("list", [
            T("Every command of the game in one list - double-click, drag or press <kbd>Insert</kbd> to add it. [[k-befehl-einfuegen]]"),
            T("Each value in its own field: lists of allowed values, names picked from the map, sounds you can play, expressions with `get`, `tag` and `random`. [[k-werte-bearbeiten]]"),
            T("Copy, move, comment out, undo every step - even one single step from the middle of the history. [[k-zeilen-ordnen]], [[k-rueckgaengig]]"),
            T("Compile to the `.ibi` the game runs, with clear messages that jump to the wrong line. [[k-uebersetzen]]"),
            T("Every command and every `SET_` field explained: [[befehle]]."),
        ]),

        ("h2", "ansichten", T("Three views of the same script")),
        ("imgs", [("h_graph", T("Graph: what starts what.")), ("h_spuren", T("Tracks: the script over time."))]),
        ("p", T("**{{FlowClassic}}** is the tree of the original. **{{FlowGraph}}** shows the mission as nodes - the player start, runners, characters, scripts - and lets you connect them by dragging. **{{FlowTracks}}** lays the script out over time, with the camera, every figure and the dialogue in rows. [[skriptfluss]], [[k-graph]]")),

        ("h2", "karte", T("The map in 3D - as the game draws it")),
        ("img", "h_karte", T("The landing pad of taspir1 with the mission's entities."), "breit"),
        ("p", T("Maps load straight from the game's `.pk3` files, with their shaders, lightmaps, fog, glow, effects, sky portals and mirrors. Fly through them, look from above, save views on number keys. Place entities with tiles, drag and turn them on the grid, edit every key in the inspector. [[karte]], [[k-entity-setzen]], [[k-entity-felder]], [[k-draufsicht]]")),

        ("h2", "kamera", T("Cameras you can see and fly")),
        ("anim", "a_kamera", T("A camera path played through the camera.")),
        ("p", T("Each `camera ( MOVE )` and `PAN` is a shot on the map with its path. Move and turn shots with the gizmo, put them where you are looking, or sit in a shot and fly it in pilot mode. Zoom, fade, shake and follow play in the view exactly as in the game. [[kamera]], [[k-pilot]], [[k-kamera-ansicht]], [[k-zoom-blende]]")),

        ("h2", "figuren", T("Figures that walk their paths")),
        ("img", "h_figur_bahn", T("A figure in the middle of its walk, with its path.")),
        ("p", T("Characters spawn with their real models and animations and walk where the script sends them. Their paths have handles: drag a walk target, insert a point mid-walk, switch walk, run or walk backwards, set where a figure looks and which way it faces. [[figuren]], [[k-gangart]], [[k-ansehen]], [[k-blickrichtung]]")),

        ("h2", "zeit", T("A timeline for the whole scene")),
        ("anim", "a_k_timen", T("Dragging a block in the timeline: everything after it moves along.")),
        ("p", T("Play the scene with sound, at any speed. Every command is a block in time - drag it and everything after moves along, drag its edge to change a duration. [[zeitleiste]], [[k-neu-timen]]")),

        ("h2", "animation", T("Animation: move commands and ROFF files")),
        ("anim", "a_roff", T("The Raven's Claw taking off on a ROFF path.")),
        ("p", T("Set keys over time for a door, a platform or a ship. behaved writes them as `move` commands or builds a ROFF file, and it turns the game's own ROFF files into keys you can change - as a copy, never touching the original. [[animation]], [[tut-p5]], [[k-tuer]], [[k-roff-bearbeiten]]")),

        ("h2", "missionen", T("Missions: from nothing to a .pk3")),
        ("img", "h_mission_speichern", T("Saving a mission: every file, every check.")),
        ("p", T("Start a mission on any map with one click, open the missions of the game or a mod, continue them from their folder. **{{MissionSave}}** checks everything, compiles every script and packs one `.pk3` with map, entities, scripts and ROFF files - ready for `devmap`. NPC waypoint networks are created and checked on the way. [[missionen]], [[tut-projekt]], [[k-wegpunkte]]")),

        ("h2", "modell", T("Character models and their animations")),
        ("img", "h_modell", T("A character in the model view, with its animation list.")),
        ("p", T("Every character of the game with its skin, every animation with its frames - click to play, double-click to put it into the script. [[modell]], [[k-animation-abspielen]], [[k-modell]]")),

        ("h2", "werkzeuge", T("The tools around it")),
        ("list", [
            T("Find and replace in one script or in all open ones. [[k-suchen-ersetzen]]"),
            T("Tabs, bookmarks and a split view to compare scripts and copy across. [[k-lesezeichen]]"),
            T("Backups on every save, a backup folder, revert any command to how it was. [[speichern]]"),
            T("Four languages, six themes, any interface size, every shortcut rebindable. [[einstellungen]], [[tasten]]"),
            T("Checks for updates and installs them. [[einstellungen#updates]]"),
        ]),
    ],
}

SEITEN = [SEITE]
