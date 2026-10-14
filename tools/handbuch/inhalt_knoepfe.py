"""Nachschlagen: jeder Knopf - Modusleiste, Aktionsspalte, Kartenleiste,
Platzier-Kacheln, Inspektor, Kamera, Zeitleiste, Modell.

shank: "vielleicht auch eine Erklaerung, was jeder Button kann und jeder
Befehl" - "das gleiche auch bei Maps und Models". Die Icons schneidet der
Selbsttest aus dem Programm aus ({{icon:Name}} = UiIcon::Name), die
Beschriftungen kommen aus gen_i18n.py ({{Kennung}}). Englisch zuerst.
"""
from inhalt_basis import T


def k(icon, kennung, was):
    """Eine Zeile: Icon und Beschriftung des Knopfs, was er tut."""
    knopf = ("{{icon:%s}} " % icon if icon else "") + "**{{%s}}**" % kennung
    return [T(knopf), T(was)]


def tabelle(zeilen):
    return ("table", [T("Button"), T("What it does")], zeilen)


SEITE = {
    "id": "knoepfe",
    "gruppe": "ref",
    "titel": T("Every button"),
    "kurz": T("Each button of the program - the mode bar, the action column, the map toolbar, the placement tiles, the inspector, the camera, the timeline and the model view - with what it does."),
    "bloecke": [
        ("p", T("Hover over any button in the program to see the same explanation as a tooltip. Buttons with a coloured frame are switches: on or off.")),

        ("h2", "modus", T("The mode bar")),
        ("p", T("The strip at the far left switches what the window shows. The script stays the same in every mode.")),
        ("table", [T("Mode"), T("What it shows")], [
            [T("**{{ViewEvents}}**"), T("The **{{GroupEvents}}** list of every command and macro, the script tree, the action column - for writing scripts. [[skripte]]")],
            [T("**{{ViewMap}}**"), T("The mission: the entity list and inspector, the map in 3D with the timeline below - for cameras, figures, entities and saving the mission. [[karte]]")],
            [T("**{{ViewModel}}**"), T("A character model with its animations - to find an animation and put it into the script. [[modell]]")],
        ]),

        ("h2", "aktionen", T("The action column (Events mode)")),
        ("img", "t1_6", T("The action column, as in the original BehavEd."), "schmal"),
        ("table", [T("Button"), T("What it does")], [
            [T("**{{ActDelete}}**"), T("Removes the selected lines (<kbd>Delete</kbd>).")],
            [T("**{{ActClone}}**"), T("Inserts a copy right below (<kbd>Space</kbd>).")],
            [T("**{{ActCopy}}** / **{{ActCut}}** / **{{ActPaste}}**"), T("Clipboard (<kbd>Ctrl</kbd>+<kbd>C</kbd> / <kbd>X</kbd> / <kbd>V</kbd>). Paste goes below the selection; text from other programs is accepted after a question.")],
            [T("**{{ActRem}}**"), T("Turns lines into comments and back (<kbd>Backspace</kbd>).")],
            [T("**{{ActFind}}**"), T("The Find window (<kbd>Ctrl</kbd>+<kbd>F</kbd>): search by command or value, replace in one or all scripts. [[k-suchen-ersetzen]]")],
            [T("**{{ActMoveUp}}** / **{{ActMoveDown}}**"), T("Moves the selection one line, inside its block (<kbd>Alt</kbd>+<kbd>Up</kbd>/<kbd>Down</kbd>).")],
            [T("**{{ActUndo}}** / **{{ActRedo}}**"), T("Undo and redo. Right-click either one for the full history. [[k-rueckgaengig]]")],
            [T("**{{FileNew}}** / **{{FileOpen}}**"), T("An empty script / open a `.txt` or a compiled `.ibi`.")],
            [T("**{{FileMru}}**"), T("Recently opened files (<kbd>Alt</kbd>+<kbd>M</kbd>).")],
            [T("**{{FileAppend}}**"), T("Adds another script to the end of this one.")],
            [T("**{{FileSave}}** / **{{ActSaveAll}}** / **{{FileSaveAs}}**"), T("Save this script, every changed tab, or under a new name.")],
            [T("**{{FileExport}}**"), T("Writes the script as plain text and opens it.")],
            [T("**{{FileBackup}}** / **{{FileRestore}}**"), T("Keeps a copy next to the script (`.bak`) / goes back to it (<kbd>Alt</kbd>+<kbd>B</kbd> / <kbd>Alt</kbd>+<kbd>R</kbd>).")],
            [T("**{{TreeShowTypes}}**"), T("Shows the value types in the tree.")],
            [T("**{{TreeGFloats}}**"), T("Shows numbers short: `1000` instead of `1000.000`.")],
            [T("**{{TreeFoldMacros}}**"), T("Shows a macro as one line.")],
            [T("**+** / **-**"), T("Expands or collapses every block.")],
            [T("**{{Compile}}**"), T("Saves the script and writes the `.ibi` the game runs; green when it worked, red with the messages when not. [[k-uebersetzen]]")],
        ]),

        ("h2", "kartenleiste", T("The map toolbar (Mission mode)")),
        ("img", "r_leiste_karte", T("The toolbar above the map."), "breit"),
        tabelle([
            k("Neu", "MissionNew", "Starts a mission on any map: player start, script runner, intro script and mission folder are created. [[tut-p1]]"),
            k("Oeffnen", "LoadMission", "Opens a mission from a `.pk3`: map, entities, every script in tabs, the figures. [[missionen]]"),
            k("Speichern", "MissionSave", "Saves every script, writes the `.ent`, compiles and packs the `.pk3` - after checking the mission. [[tut-p8]]"),
            k("Mission", "MissionNoneShort", "The name of the open mission; a dot means unsaved changes. Hover for its folder."),
            k("Karte", "MapMore", "Opens a map from disk or from a `.pk3`, a mission folder, or a separate `.ent` file."),
            k("", "MapScripts", "The scripts the map's entities point to - pick one to open it."),
            k("Entity", "MapEntities", "Shows the entity markers: walk targets, marks, runners, spawners."),
            k("Namen", "MapNames", "Writes the names of entities and figures into the view."),
            k("Figur", "MapActors", "Shows the script's figures, moved by the timeline."),
            k("Effekt", "MapEffects", "Shows the effects of the map (`fx_runner`)."),
            k("Leuchten", "MapGlow", "The glow of shaders marked `glow`, as `r_DynamicGlow`."),
            k("Licht", "MapDynLights", "Lightsabers and effect lights light up walls and figures."),
            k("Nebel", "MapFog", "The fog of the map, as the game draws it."),
            k("Himmel", "MapSky", "The sky; switched off you can look into the map from outside."),
            k("Kamera", "MapThroughCam", "Shows what the camera sees - the selected camera command, or the camera at the playhead. [[k-ansichten]]"),
            k("Oben", "MapTopView", "Looks straight down; a cut height hides ceilings. [[k-draufsicht]]"),
            k("Pfad", "WegTitel", "The NPC waypoint window: show, create and check the network. [[wegpunkte]]"),
            k("Raster", "Grid", "Snaps entities to 8 to 64 units while dragging and placing."),
            k("Einstellungen", "MapViewSettings", "Brightness, ambient light, detail, shadows, highlighting the active script's entities. [[k-hell]]"),
            k("Statistik", "MapStats", "Frame rate, textures, triangles and graphics card values."),
        ]),

        ("h2", "kacheln", T("The placement tiles")),
        ("p", T("The strip on the left edge of the map. Click a tile, then click on the floor - or drag the tile into the view. <kbd>Shift</kbd> places several, <kbd>Esc</kbd> stops. [[k-entity-setzen]]")),
        tabelle([
            k("Figur", "EntKindNpc", "A character that spawns: set `NPC_type` and a name."),
            k("Wegpunkt", "EntKindNav", "A place a figure walks to (`SET_NAVGOAL`)."),
            k("Marke", "EntKindTag", "A mark with a direction - for cameras (`tag`) and look targets."),
            k("Zielen", "EntKindPos", "A point to look at or to aim at."),
            k("Skript", "EntKindRunner", "Runs a script when it is used - by a trigger, the player start or another script."),
            k("Effekt", "EntKindFx", "Plays an effect file (`.efx`)."),
            k("Ton", "EntKindSpeaker", "Plays a sound, once or in a loop."),
            k("Pfad", "EntKindWaypoint", "A point of the NPC navigation network."),
            k("Mission", "EntKindStart", "Where the player spawns."),
        ]),

        ("h2", "inspektor", T("The entity inspector")),
        ("img", "k_inspektor", T("The inspector under the entity list.")),
        tabelle([
            k("Hinfliegen", "EntityFlyTo", "Flies the view to the entity."),
            k("Boden", "EntDropFloor", "Puts it on the floor below, like `DROPTOFLOOR` in the game."),
            k("Duplizieren", "EntDuplicate", "A copy - of every selected entity. <kbd>Alt</kbd>+drag in the view does the same."),
            k("Loeschen", "EntDelete", "Removes it."),
            k("Animieren", "EntAnimate", "Opens the Animation window for doors, platforms, ships and effects. [[animation]]"),
            k("Rueckgaengig", "EditUndo", "Undoes the last entity change (entities have their own history)."),
            k("Wiederholen", "EditRedo", "Redoes it."),
            k("Liste", "EntRaw", "Shows every key as plain text."),
            k("", "EntAddKey", "Adds a key."),
            k("", "EntOpenScript", "Next to a script key: opens the script - and creates it in the mission folder if it does not exist yet."),
        ]),

        ("h2", "kamera", T("The camera bar")),
        ("p", T("Appears in the view when the camera is selected. [[kamera]]")),
        ("img", "h_kamera_verschieben", T("The camera bar above the selected shot.")),
        tabelle([
            k("Verschieben", "GizmoMove", "Arrows: move the shot along one axis, the box in the middle in the view plane."),
            k("Drehen", "GizmoRotate", "Rings: turn the shot."),
            k("", "GizmoSpaceWorld", "World or Local: the arrows follow the map axes or the camera."),
            k("Zurueck", "CamPrevShot", "The previous shot."),
            k("Vor", "CamNextShot", "The next shot."),
            k("Zielen", "CamSetToView", "Puts the shot where you are looking."),
            k("Steuern", "CamPilot", "Sit in the shot and fly it; <kbd>Enter</kbd> sets a key. [[k-pilot]]"),
            k("Kamera", "MapThroughCam", "Look through the camera."),
            k("Bildschirm", "CamPreview", "A small picture of the camera's view in the corner."),
            k("Plus", "CamNewShot", "A new shot at your view, after the selected one."),
            k("Loeschen", "CamDeleteShot", "Removes the shot."),
        ]),

        ("h2", "zeitleiste", T("The timeline")),
        ("img", "r_transport", T("The transport bar of the timeline."), "breit"),
        tabelle([
            k("ZumAnfang", "TlRewind", "Back to the start of the script."),
            k("Abspielen", "TlPlay", "Plays the scene - camera, figures, sound - in game time."),
            k("Schleife", "TlLoop", "Starts again at the end."),
            k("Ton", "TlAudio", "Plays the sounds while the timeline runs."),
            k("Folgen", "TlFollow", "Moves the view along with the camera."),
            k("ZoomMinus", "TlZoomOut", "Shows more time."),
            k("ZoomPlus", "TlZoomIn", "Shows less time, in more detail."),
            k("Zeilen", "TlTracks", "One row per figure: when each one walks, speaks and acts."),
            k("Ruhig", "TlIdleTracks", "Also shows rows where nothing happens."),
            k("Pause", "TlWaits", "Shows the waits as their own blocks."),
            k("Kamera", "MapInsertCamera", "Inserts a camera shot at the playhead."),
        ]),
        ("p", T("The time display switches between seconds and frames when you click it; the list next to the play button sets the speed. Drag blocks to move them in time - [[zeitleiste]].")),

        ("h2", "modell", T("The model toolbar (Model mode)")),
        ("img", "r_leiste_modell", T("The toolbar above the model."), "breit"),
        tabelle([
            k("Oeffnen", "OpenGlm", "Opens a `.glm` model from disk."),
            k("Paket", "OpenGlmPk3", "Opens a model from the game's archives."),
            k("Figur", "NpcBrowser", "Every character of the game - click one to load it as the game shows it."),
            k("Namen", "OpenSkin", "Loads a `.skin` for the model."),
            k("Liste", "ModelLoadGla", "Loads another skeleton / animation file (`.gla`)."),
            k("Schleife", "ModelTurntable", "Turns the model slowly around itself."),
            k("Pfad", "ModelBones", "Shows the skeleton."),
            k("Bruch", "ModelCaps", "Shows the caps of the surfaces where limbs attach."),
            k("Leuchten", "ModelTextures", "Textures on or off."),
            k("Einpassen", "ModelResetView", "The whole model in view, from the front."),
            k("Duplizieren", "ActCopy", "In the animation list: copies the animation name."),
            k("Skript", "ModelAnimToScript", "Puts `set ( SET_ANIM_BOTH, ... )` for the animation into the script. [[k-animation-abspielen]]"),
        ]),
    ],
}

SEITEN = [SEITE]
