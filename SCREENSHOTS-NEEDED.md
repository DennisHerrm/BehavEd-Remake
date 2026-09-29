# BehavEd screenshots needed

We are building a modern replacement for Raven's BehavEd 2.0 (the ICARUS
script editor for Jedi Knight: Jedi Academy). Everything about the layout is
being reconstructed from the original rather than guessed, so screenshots of
the real program are the reference we check against.

## Please read first — this matters more than it sounds

**Turn off Windows display scaling for BehavEd.** The last batch was taken at
150 %, which made every window 1.5× too large. We can compute that out, but
only if we know it happened. Either:

* take the screenshots on a display set to 100 %, or
* right-click `BehavEd.exe` → Properties → Compatibility → *Change high DPI
  settings* → tick *Override high DPI scaling behaviour*, Scaling performed by:
  *Application*, or
* **just tell us the scaling percentage** and we will divide it out.

Other requests:

* **PNG, not JPEG.** JPEG blurs single-pixel borders, which is exactly what we
  measure.
* **Do not crop.** Include the window title bar — it is a known height and we
  use it to verify the scale before measuring anything else.
* One window per image is fine; no need to arrange anything.

---

## A. Tree view behaviour (highest priority)

We have a real Movie Duels script loaded (`intro_jedi.txt`) and can compare
line by line. What we still cannot see:

**A1 — A script containing a macro.**
Macros are the entries at the bottom of the Events list: `standOnly`,
`walkOnly`, `runOnly`, `patrolRun`, `default` and so on. When BehavEd expands
one into a script it writes a marker line like

```
//$"walkOnly"@5
```

followed by the commands it generated. **We need to see how the tree displays
that.** Is it one collapsible node named `walkOnly`, or five separate `set`
lines? Any Raven script under `scripts/taspir2/` contains these, or just
double-click `walkOnly` in the Events list of a new script and screenshot the
result.

**A2 — The same macro block, expanded.**
If A1 shows a single node, please expand it and take a second shot.

**A3 — A commented-out block.**
Select any block command (`affect`, `task`, `if`) and press the `REM` button
(or Backspace). BehavEd turns it into `//(BHVDREM)` lines. We want to see how
those look in the tree — icon, indentation, colour. Then press
`Ctrl+Backspace` to undo it and screenshot that too, if it looks different.

**A4 — `Show Types` switched ON, on the same script as your last batch.**
We now believe this switch changes the whole notation, not just the type name:
with it off the tree shows `set ( SET_MORELIGHT, true )`, with it on
`set ( <SET_TYPES> "SET_MORELIGHT", <BOOL_TYPES> "true" )`. One screenshot of
`intro_jedi.txt` with the button pressed would confirm it.

---

## B. Dialogs we have never seen

**B1 — `Prefs`** (the Preferences dialog). All fields visible.

**B2 — `About`**.

**B3 — `Find`** (the Find dialog, from the `Find` button).

**B4 — `MRU`** (click the MRU button — the recently-used file list).

**B5 — The Event editor with helper rows open.**
Open any `set` command, click `Expr!` or `Helper`, and screenshot the expanded
form with the `Get` / `Tag` / `Rnd` rows visible. We have one of these already
but only for `set`; a second example for a different command would help.

---

## C. Error and edge behaviour

**C1 — What happens on a broken script.**
Open a `.icarus` file with a deliberate syntax error (delete a closing brace,
for example) and screenshot whatever BehavEd says. We want our error messages
to be no worse than the original's.

**C2 — Unsaved changes.**
Make any edit, then click `New` or close the window. Screenshot the
"save changes?" prompt — exact wording and button labels.

**C3 — `Compile !` output.**
Press it on a working script and screenshot the Status list at the bottom.
Then press it on a script with an error, and screenshot that too. The Status
list is the one part of the main window we have never seen with content in it.

---

## D. Nice to have, not urgent

**D1 — The `Export` and `Backup` / `Restore` buttons.**
We have no idea what these do. A screenshot of whatever appears when you press
them would settle it.

**D2 — `Pythonise !`**.
Same — no idea. Even a screenshot of an error message is informative.

**D3 — The language buttons** (`English` / `Deutsch` / `Francais` at the
bottom right). Press `Deutsch` and screenshot the main window. We want to know
whether BehavEd actually translates its own interface or only affects the
speech-extraction tools.

**D4 — The tree with a very deeply nested script** (three or four levels of
`affect` inside `task` inside `if`). We want to see the indentation step and
how the connecting lines are drawn.

---

## What we do NOT need

* Anything from the SourceSafe row — that system is long gone.
* `Find uncached scripted audio` / `Find uncached voice files` /
  `Extract Speech 4 translation` / `.PRE Cross-Language check` — these depend
  on Raven's internal asset tree and cannot be reproduced.

---

## If you want to go further

Two things would be worth more than any screenshot:

1. **Run `IBIZE.EXE` over a folder of `.icarus` files and send the resulting
   `.IBI` files.** We have written our own compiler for that format and can
   currently only verify it against a single 30-byte sample. With a few
   hundred real outputs we could compare byte for byte.

2. **The `.bhc` file your BehavEd uses**, if it differs from the one we have
   (Settings → Command Description File). Different mods sometimes ship
   modified command lists.
