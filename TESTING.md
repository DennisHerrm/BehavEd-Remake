# behaved — testing build

A modern reconstruction of Raven Software's **BehavEd 2.0**, the ICARUS script
editor for Jedi Knight: Jedi Academy.

This is a testing build. It works, but it has not been used in anger by
anyone yet — you would be the first. Everything below is written so you know
what to trust and what to poke at.

---

## Getting started

1. Unzip anywhere. Keep `behaved.exe` and the `data` folder together.
2. Run `behaved.exe`.
3. **Add a game folder first:** `Datei → In .pk3 blättern → Spielordner
   hinzufügen`, then pick any `.pk3` inside your `GameData/base`. Do the same
   for your mod folder. Without this, sounds cannot be found and you cannot
   browse archives.
4. Switch language under `Ansicht → Sprache` (English, Deutsch, 中文, 日本語).
5. Switch look under `Ansicht → Farbgebung`. `BehavEd Classic` gives the
   Windows 95 grey of the 2003 original.

Sample scripts are in `fixtures\` — real Movie Duels missions, including one
with seven nested `else` branches.

---

## What is solid

These are not claims, they are measured against real data on every build:

| | |
|---|---|
| Reading and writing `.icarus` | **1509 of Raven's 1510 scripts come back byte for byte identical.** The one difference is a typo in Raven's own file (a missing space before a bracket). |
| Compiling to `.ibi` | **1011 of 1011 files identical to what `IBIZE.EXE` produces**, measured across the whole Jedi Academy story mode. IBIZE is no longer needed. |
| Editing | Opening any node and closing it with `Ok` without changing anything leaves the file **byte for byte identical**. Checked across 30158 nodes. |
| Engine compatibility | The engine's own reader was reconstructed from the OpenJK source and run over all 25981 blocks. It consumes every block exactly — nothing left over, nothing missing. |
| Command model | The 29 command signatures, 26 type sets and 15 macros are **generated** from `behaved.bhc` and the engine headers, not maintained by hand. They match the original's Events list line for line. |
| Archives | 1015 files unpacked from a `.pk3` and compared byte for byte with the originals. |

So: **your scripts are safe.** Open, edit, save — the parts you did not touch
will not change.

---

## New in this build

* **Multi-selection in the script flow.** `Ctrl`-click adds single items,
  `Shift`-click adds the range in between. Delete, copy and cut then act on
  everything selected, as **one** undo step.
* **Drag to reorder.** Pick a line up and drop it on another.
* **3D map view** with textures, lightmaps and camera gizmos. Load a map via
  *File → Load map*, then switch the left column to *Map*.
  Navigation is 3ds Max style: middle mouse pans, `Alt`+middle orbits, wheel
  zooms, right mouse plus `WASD` flies.
* **Double-click a `camera` line** in the tree and the view jumps there.
* Hovering an entity marker in the map shows its `targetname`.

## Filling a block (affect, if, task, loop)

Three ways, all of which should work:

1. **Select the block line itself** and pick a command. If the block is open
   (or empty), the command goes **inside**; if it is closed and already has
   contents, it goes after it. The context menu tells you which before you
   click.
2. **Select a line already inside the block** and insert — it stays inside.
3. **Drag** a line onto another one.

A freshly inserted `affect` is empty and opens itself, so the next command you
pick lands in it.

## Looking through a camera

Load a map, load a script with `camera` commands, then tick **Through
camera** under the 3D view. Select a `camera` line in the tree — the view
shows what that shot sees, with its zoom and the cinematic bars.

Worth checking against the game: does the framing match what you get in-game?
The field of view is converted the way the engine does it (the number in
`camera ( ZOOM, ... )` is the *horizontal* angle), and the bars are one tenth
of the height top and bottom.

## Load mission

The fastest way in: switch the left column to **Map**, hit **Load mission…**
and pick a `.pk3`. The tool then works out the rest — the map, the entities
next to it, the scripts those entities point to, and the figures from the
scripts.

If the archive holds exactly one playable mission it loads straight away;
otherwise you get a list. Maps without script references are listed as *no
scripts* — those are duel maps, not missions.

Worth checking: does it pick the right map? Some mods ship the same geometry
twice under different names, and only the one with a `.ent` beside it is the
mission.

## The timeline

With a map and a script loaded, the timeline sits under the 3D view. **Play**
runs the camera path in real time, the playhead can be dragged, and with
**Follow** the view moves along — with the shot's zoom and the cinematic bars.
The bar under the playhead shows where things happen: blue for moves, amber
for pans, green for zooms.

Worth checking against the game: does a move take as long as it does in-game,
and does a pan turn the same way? Panning is supposed to take the *shortest*
way around — from 350° to 10° that is +20°, not −340°.

## The tracks

Under the camera bar there is one row per `affect` target — pink marks are
sounds, blue camera commands, green movement. Hovering a mark shows the file
name and the time.

With **Sound** ticked, the sound files play while the timeline runs. That is
the thing to check: does a camera cut land where a line of dialogue starts?
The first sound of a sequence often sits at 0 ms, so listen for that one in
particular.

## What to hammer on

The core is measured. The **user interface** is not — that is where bugs
will be.

* **Open a large script, expand everything, edit in several places, undo it
  all.** Does the tree stay consistent? Does the `*` in the title disappear
  when everything is undone?
* **Delete, clone, cut, paste, move up and down** — especially on block
  commands (`affect`, `task`, `if`) and on macros. Does the right node get
  hit?
* **Multi-selection with a mixed selection**: pick a block command and a
  plain one, delete both, undo. Do exactly those two come back?
* **Drag a block command into its own body.** Nothing should happen — the
  tree would otherwise hang off itself.
* **The map with your own maps**: how many textures does it find? The count
  is under the view. `0 of 164` means the game folder is wrong; anything else
  and the chain works. Which ones failed is named in the log.
* **The Event editor** — double-click a node in the middle pane. Try the
  `Expr!` / `Helper` button and build a `get()` / `tag()` / `random()`.
* **Resize the window, drag the two splitters, scale with Ctrl+mouse wheel**
  (50 %–300 %). Anything overlapping or cut off?
* **`Do`** next to a sound filename — does it play? This is the one part
  built blind: there is no sound device on the machine where the code is
  written, so playback has **never been tested**.
* **Compare against the original.** Open the same script in both, switch
  `Show Types` and `%g floats`, and look for differences in the tree text.

---

## Keyboard

Taken from the original's accelerator table, not invented:

| Key | Action |
|---|---|
| `Enter` | open the Event editor |
| `Insert` | insert the command selected on the left |
| `Delete` | delete |
| `Space` | clone |
| `Backspace` | comment out |
| `Ctrl+Backspace` | uncomment |
| `Ctrl+A` | **Save As** — yes, really. Not "select all". |
| `Ctrl+O` / `Ctrl+S` | open / save |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | copy / cut / paste |
| `F3` / `Shift+F3` | repeat find forwards / backwards |
| `Ctrl+Z` / `Ctrl+Y` | undo / redo — **added, the original has none** |

---

## Known gaps

Nothing here is a bug report waiting to happen — it is a list of things that
are deliberately absent.

* **`Pythonise !` is deliberately absent.** The JKHub tutorial says about it:
  *"Will attempt to write a .python file for your script. **Ignore This
  Button**"* — Raven's own documentation advises against it. Rebuilding
  something the manual warns you off would be the wrong kind of fidelity.
* **`%g floats` may be inverted.** Both the JKHub tutorial and the OpenJK
  wiki say the switch shows *more* decimal places. A screenshot of the
  original shows the opposite: with the button pressed, numbers appear
  trimmed (`wait ( 3000 )`). The screenshot won. **If you can tell which is
  right, say so** — it is one line to change.
* **Writing back into a `.pk3` is not possible**, by design. A script opened
  from an archive has no path; `Save` becomes `Save As`. Writing into
  `assets1.pk3` is the fastest way to wreck an installation.
* **SourceSafe** is gone entirely. That system is long dead.
* **No map geometry**, only entities. Loading a `.bsp` gives you the
  `targetname` list for the editor's text fields, nothing more.
* **Windows 7** is untested. This build needs Windows Vista or newer; a
  dedicated Windows 7 build is possible but has not been made.

---

## Windows will warn you

The first time you run it, Windows SmartScreen will show a blue box saying it
protected your PC. This is expected: the file is **not code-signed**, and
anything downloaded from the internet without a signature gets that treatment.

Click **More info → Run anyway**.

Nothing in the build can remove that warning — only a code-signing
certificate can, and even a normal one needs to build reputation first. The
executable does carry proper version information and a manifest (right-click
→ Properties → Details will show it), and it explicitly does **not** request
administrator rights, but SmartScreen does not care about any of that.

## If something goes wrong

There is a log:

```
%APPDATA%\behaved\behaved.log
```

Press `Win+R`, paste `%APPDATA%\behaved`, Enter.

The **last line names the step that was running** when it died. Every line is
flushed immediately, so nothing is lost even on a hard crash. The previous
run is kept as `behaved.log.vorher` — but only the previous one, so if you
restart twice the evidence is gone.

**Please send both files.** With them, a crash usually takes minutes to find
instead of guesswork. Without them, the only honest answer is "no idea".

Also useful: what you did immediately before, and whether the log says
Direct3D 11 or OpenGL carried.

---

## Two questions worth answering

If you have the original BehavEd running, these are the last two things that
are still guessed rather than known:

1. **Does `%g floats` trim decimals or add them?** Toggle it on a script with
   `wait ( 3000.000 )` and see. The documentation and a screenshot disagree.
2. **Does `Backup` overwrite an existing `.bak` silently, or ask?** We
   overwrite.

## `checks` braucht ein Kopfdateiverzeichnis

Aufruf: `checks <behaved.bhc> <headerdir> [nachtrag]`.

Fünf der Gegenproben lesen die `SET_`-Werte nicht aus der `.bhc`, sondern
aus `Q3_Interface.h` der Engine (die `//## %s=""` -Anmerkungen und die
`//# #sep Parm strings` -Trenner). Ohne `<headerdir>` schlagen sie fehl:

```
FEHL  gueltig: set mit Auswahl
FEHL  V008 Text statt Zahl
FEHL  SET_PARM1 steht unter "Parm strings"
FEHL  die vier Eintraege gibt es
FEHL  set nimmt SET_PARM1, nicht "SET_TYPES"
```

Das sah neun Runden lang wie ein Fehler im Programm aus. Es ist ein
fehlendes Argument. Mit dem Verzeichnis laufen alle durch:

```sh
checks data/base/behaved.bhc <jka>/code/game data/base/supplement.bhc
#  -> alle Gegenproben bestanden (0 Fehlschlaege)
```

`<jka>` ist Ravens Quelltextfreigabe (`github.com/grayj/Jedi-Academy`).
