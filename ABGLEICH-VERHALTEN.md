# Funktionsabgleich: Original-BehavEd (2003) gegen behaved rc567

Stand 26.09.2026. Nur gelesen, nichts im Projekt oder im SDK geändert.

---

## (a) Methode

**Original** `C:\jka_animations\jedi_academy_sdk\Tools\BehavEd.exe` (PE32, MFC42, Zeitstempel 25.09.2003, VERSION "Version 2.0"):

1. **Ressourcen** mit `pefile` ausgelesen (`res.py`, `dlg.py` → `res.txt`, `dlg.txt`):
   * DIALOG 100/102/131/134/137/139/140/141 mit Steuerelement-IDs, Stilen und Sichtbarkeit.
   * ACCELERATOR 135 (23 Einträge), STRINGTABLE (nur 4 Einträge), VERSION.
   * Es gibt **kein MENU**. Das Programm ist ein reines Dialogfenster mit Knöpfen.
   * **Wichtig:** `tools/originaldialoge.py` kürzt auf `texte[:40]`. Dadurch fehlen in ABGLEICH.md 17 Knöpfe aus Dialog 102: Delete, Export, Pythonise !, `.PRE Cross-Language check`, `Extract Speech 4 translation`, `Find uncached scripted audio`, `Find uncached voice files`, English/Deutsch/Francais, Mike G, der SourceSafe-Knopf „Status“, Progress-Balken u. a.
2. **Zeichenketten**: ASCII aus .rdata/.data (864 Zeilen) und UTF-16 (`strings_ascii.txt`, `strings_utf16.txt`). Aus ihnen stammen Meldungen, Tooltips mit Kürzeln, Registry-Schlüssel, Formatstrings und Dateinamen.
3. **Handbuch** `ICARUS Manual.doc`: Text über die Piece-Table des Word-97-Streams gezogen (`doc.py` → `manual.txt`). Kapitel „Using BehavEd“ steht in den Zeilen 1–64, Helper/Get/Random in den Zeilen 518 und 569.
4. **Laufendes Programm**, nur mit nicht eingreifender Automation:
   * gestartet mit `CreateProcess` und `SW_SHOWMINNOACTIVE`, also minimiert und ohne Fokus;
   * Fenster und Steuerelemente über `EnumChildWindows`;
   * Befehle über `PostMessage(WM_COMMAND, id)`;
   * TreeView und ListView über `TVM_*`/`LVM_*` mit `ReadProcessMemory` gelesen (UIA lieferte bei diesen Controls keine Elemente).
   * Kein SendInput, keine Maus, keine Tastatur, kein Vordergrund.
   * Gearbeitet wurde nur auf Kopien in `scratchpad\original\work\`. Alle drei Starts wurden per WM_CLOSE und „Nein“ beendet, danach lief kein BehavEd-Prozess mehr.
   * **Einschränkung:** In der Registry zeigt `commandPath` auf den nicht existierenden Pfad `C:\Users\shank\Downloads\jedi_academy_sdk\Tools\behavEd.bhc`. Das Umstellen der Registry wurde mir verweigert. BehavEd lief deshalb **ohne Befehlsliste**: Ereignisliste leer, Typsets „MISSING“. Befehlsbezogenes Verhalten (Editor-Kombos, Clone/Delete auf bekannten Befehlen, Speichern von Blöcken) war dadurch nicht beobachtbar. Beobachtbar waren Suche, REM, Auf-/Zuklappen, Kürzel-IDs, Backup-Dateiname, Editor-Grundaufbau und die Helper-Zeile.
5. **behaved**: Quelltext gelesen. Hinweis: `gui/app.cpp` wurde während des Abgleichs von anderer Seite geändert (22:05, jetzt 11786 Zeilen); die Zeilenangaben sind auf diesen Stand nachgezogen und können sich weiter verschieben. Gelesen wurden (`gui/app.cpp`, `src/edit.cpp`, `src/keys.cpp`, `src/tree.cpp`, `src/script.cpp`, `src/bhc.cpp`, `include/bhed/*.h`, `gui/main_win32.cpp`).

**Nebenwirkungen, die du kennen musst:**
* Beim Test von Strg+T (Befehl 32778) hat das Original zweimal **„( Cut )“ in die Windows-Zwischenablage geschrieben**. Damit wurde deren damaliger Inhalt überschrieben; erkennbar ist das am Text `//(BHVD)…`. Danach habe ich keine Zwischenablage-Befehle mehr ausgelöst.
* BehavEd selbst hat `HKCU\Software\BehavEd\mostRecent` und `MRU0` auf den Pfad der Temp-Kopie gesetzt. Die Sicherung vorher liegt in `scratchpad\original\behaved_reg_backup.reg`.

---

## (b) Funktionstabelle

Status: **=** vorhanden und gleichwertig · **≈** vorhanden, aber anders · **✗** fehlt · **obs.** obsolet oder Raven-intern

### Datei

| Funktion (Original) | Beleg Original | Status | behaved (Datei:Zeile) und Unterschied |
|---|---|---|---|
| New (Alt+N) | Knopf 1004 `&New`; Tooltip „New/clear current script (Ctrl+N)“; Abfrage „New?“ | ≈ | app.cpp:3441 `doNew`. Öffnet einen **neuen Reiter**, statt den aktuellen zu leeren (gewollt). Strg+N ist nicht belegt; die Ressource hat es auch nicht, nur der Tooltip nennt es. |
| Open (Strg+O) | Accel 0x0B 'O' → 1005; „Open Script“, „Icarus Scripts\|*%s;\|\|“ | ≈ | app.cpp:3427. Öffnet in einem neuen Reiter und liest zusätzlich .ibi. Filter app.cpp:2875 ohne `*.icarus`, während das Original `unnamed.icarus` und STR 61205 `"*.icarus;"` nutzt. Alle 1510 Skripte in `JAscripts.zip` enden auf `.icarus`. |
| Warnung „nicht von BehavEd geschrieben“; Abbruch bei unbekanntem Befehl | „The following file has NOT been written by BehavEd !!! … do you *really* want to read it in?“; „Function found during loading … not in the command list“ + „(Load aborted)“. Beobachtet: 46 Statuszeilen `(n) : Function found…` | ≈ | behaved liest tolerant und ohne Rückfrage (script.cpp). Das ist kein Mangel, nur eine Abweichung. |
| Append | Knopf 1044; Handbuch: „Append an existing script to the end“; Zeilen „   Appended File (%s) follows...  “ und „=====“ | ≈ | app.cpp:3506. Hängt an, **ohne Trennzeilen**. `readScript` statt IBI-Erkennung, obwohl der Filter .ibi anbietet. |
| Save (Strg+S) | Accel → 1006; „( Save )“ | = | app.cpp:2945. Schreibt immer .txt, CRLF und die Kopfzeile `//Generated by BehavEd` (script.cpp:313). |
| Schreibgeschützte Datei beim Speichern | „…write-protected… Do you want me to un-writeprotect it so you can save over it?“ (SetFileAttributesA) | ✗ | app.cpp:2945ff. zeigt nur `showError(pfad)`. |
| Save As (Strg+A, Alt+V) | Accel 0x0B 'A' → 1007; Tooltip „Save As (Alt+V)“ | = | app.cpp:2966 (Strg+A). Alt+V fehlt. |
| Backup (Alt+B) | Accel 0x13 'B' → 1009; Tooltip „Save directly to <scriptname.bak>“; „You can't backup until you've saved at least once“. **Beobachtet:** aus `remtest.txt` wird `remtest.bak` (Endung ersetzt) | ≈ | app.cpp:3668 `backupPathOf = path + ".bak"` ergibt `remtest.txt.bak`, also einen **anderen Dateinamen**. Außerdem kopiert behaved den Stand auf der Platte, das Original speichert laut Tooltip „directly“ (ob den Speicherstand, ließ sich im Test nicht trennen). Folge: Eine .bak des Originals findet behaved nicht, und umgekehrt. |
| Restore (Alt+R) | Accel → 1012; „This RESTORE will overwrite your current script, proceed?“; „No Backup file available to restore from“ | ≈ | app.cpp:3689. Fragt nur, wenn das Dokument ungesichert ist. **Fehler:** Bei fehlender .bak wird die Meldung formatiert (app.cpp:3698), aber nie ausgegeben. |
| Export | Knopf 1014; Tooltip „Export script to plain text in Notepad“; `Exported_%s` im Temp-Ordner, „( Original filename: "%s" )“, „notepad %s“, „( Exported )“ | = | app.cpp:3646. `Export.txt` im Einstellungsordner, geöffnet mit dem Standardprogramm. |
| MRU (Alt+M) | Knopf 1017; Dialog 139; Registry `MRU0…MRU19` (20 Einträge); Tooltip „(Alt+M)“ | ≈ | app.cpp:6897 als Popup statt Dialog, nur **9 Einträge** (settings.h:177), kein Alt+M. |
| Datei beim Start aus der Kommandozeile | „Error reading file "%s" specified in command line, ignoring...“. Beobachtet: Start mit Pfad öffnet die Datei | ✗ | main_win32.cpp:471: `wWinMain(…, PWSTR, int)` wertet die Kommandozeile nicht aus. Kein „Öffnen mit“, keine Dateizuordnung, kein Ziehen einer Datei auf die EXE; auch kein WM_DROPFILES. |
| Letzte Datei beim Start | Registry `mostRecentLoad`; „Auto-load most recent file: "%s"?“ | ≈ | app.cpp:10227. Lädt **ohne Rückfrage**. |
| Absturzerkennung | Registry `legal_exit`; „It appears you didn't exit BehaveEd legally last time (crash?) Should I still try and open your last file?“ | ✗ | behaved schreibt ein Absturzprotokoll (main_win32.cpp:399), fragt beim nächsten Start aber nichts. |
| Titelzeile mit „ *“ | Beobachtet: `gonkability.txt *` | = | app.cpp:10625 `windowTitle`. |
| Rückfrage bei ungesicherten Änderungen | „Current script has unsaved changes. Save this file first?“. **Beobachtet:** kommt auch bei `yesNoQuery=0` | ≈ | app.cpp:8264: Ist „Yes/No query“ aus, fragt behaved **gar nicht mehr** und verwirft Änderungen still. Im Original schaltet die Option nur die *zusätzlichen* Fragen „Open?/New?/Exit?“. |

### Bearbeiten und Baum

| Funktion | Beleg Original | Status | behaved |
|---|---|---|---|
| Add aus der Ereignisliste (Doppelklick, Einfg) | Handbuch Z.8 und Z.36; Accel VK_INSERT → 1001 | = | app.cpp:2155, 3815 |
| Ziehen aus der Ereignisliste in den Baum | Handbuch Z.2 und Z.36; ImageList_BeginDrag | = | app.cpp:4785ff., 3839 `dropNewAt` |
| Ziehen innerhalb des Baums, Schutz gegen Rekursion | „You can't drag an item into one of it's descendants (recursion!)“ | = | edit.h `Abweisung::ZielImGezogenen` |
| Edit (Enter, Doppelklick) | Accel VK_RETURN → 1003; „EDIT only works with single-item selection!“ | = | app.cpp:1818 (handleTreeKeys), 2154, 8454 |
| Mehrfachauswahl | CTreeCtrlEx (Mihai Filimon, „Special Thanks“) | = | app.cpp:10019 `selectRow` |
| Delete (Entf) | Knopf 1002; Accel VK_DELETE | = | app.cpp:2110, auch für mehrere Knoten |
| Clone (Leertaste) | Knopf 1010; „Clone current line to below itself (SPACE)“; „Smeg off, I'm not cloning dummy blocks...“ | = | edit.cpp:526 |
| Copy (Strg+C) | Knopf 1045; „No line selected, Copy entire script?“ | ≈ | edit.cpp:871. Ohne Auswahl ist der Knopf gesperrt (app.cpp:6800ff. `drawButtonColumns`); ganzes Skript kopieren geht nicht. |
| Cut (Strg+X **und Strg+T**) | Accel 0x8B 'X' → 1047. **Beobachtet:** Strg+T (32778) ergibt ebenfalls „( Cut )“ | ≈ | Strg+X ja. Strg+T steht in keys.cpp:26 als `Unhandled`. |
| **Zwischenablage-Format** | Windows-Zwischenablage CF_TEXT: `//(BHVD)` + Skripttext. **Beobachtet:** `//(BHVD)\r\ndeclare ( /*@DECLARE_TYPE*/ FLOAT, "rand" );`. Beim Einfügen fremden Texts: „You seem to be pasting something that wasn't copied from BehavEd / Proceed?“ | ✗ | edit.h:306–324: Die Zwischenablage ist **programmintern** (`clip_`). Nichts geht an Notepad, Foren, eine zweite behaved-Instanz oder das Original, und nichts kommt von dort. Einfügen von ICARUS-Text aus einem Texteditor ist unmöglich. |
| Paste (Strg+V) unter die aktuelle Zeile | Handbuch Z.13 | = | edit.cpp:911 |
| REM umschalten (Rücktaste) | „Toggle REMark status of current line (BACKSPACE, or Ctrl-BACKSPACE to not un-REM child items)“. **Beobachtet:** REM auf `if` markiert Block und Kinder mit `/////////////  `, **die Baumstruktur bleibt**; zweites Rücktaste nimmt es zurück | ≈ | edit.cpp:714 `commentOut` löst den Block in **flache Textzeilen** `//(BHVDREM)  …` auf, dargestellt als Rohtext (tree.cpp:174). Die Rücktaste kann nur auskommentieren und nicht zurücknehmen (app.cpp:2148); nur der Knopf schaltet um (app.cpp:6862). `uncomment` (edit.cpp:757) nimmt die **ganze zusammenhängende Folge** von REM-Zeilen zurück, auch getrennt auskommentierte Nachbarn. |
| Strg+Rücktaste | Accel 0x0B VK_BACK → 32783 (siehe Tooltip oben) | ≈ | keys.cpp:31 bedeutet dort „Uncomment“. Im Versuch nahm auch das Original die Kinder mit zurück; der Unterschied laut Tooltip ließ sich nicht nachstellen (nicht eindeutig feststellbar). |
| Alles auf-/zuklappen (+/-, Strg+Num+/Num−) | Knöpfe 32772/32773; Tooltip „Expand all tree items (Ctrl+GreyPlus (or without Ctrl = item-only)“ | ≈ | Knöpfe vorhanden (app.cpp:6925). **Tasten fehlen.** |
| **Einzelnen Knoten samt Teilbaum auf-/zuklappen (Num+/Num−)** | Accel VK_ADD (0x6B) → 32774, VK_SUBTRACT (0x6D) → 32776. **Beobachtet:** Num+ auf dem Wurzelknoten zeigt 46 statt 5 Zeilen (rekursiv), Num− klappt den Knoten zu | ✗ | keys.cpp:28/29/43/44 liest 0x6B/0x6D **falsch als 'K'/'M'** und führt sie als `Unhandled`. Pfeil rechts/links klappt nur eine Ebene (app.cpp:1807). |
| Strg+D | Accel → 32779 | – | Kein beobachtbarer Effekt (nicht feststellbar); in behaved `Unhandled`. |
| Undo/Redo | Im Original **nicht vorhanden**, weder Knopf noch Kürzel | + | behaved hat es zusätzlich. |
| Mehrere Dokumente | Im Original eine Datei je Instanz | + | behaved hat Reiter. |

### Suche (Dialog 140)

| Funktion | Beleg Original (beobachtet) | Status | behaved |
|---|---|---|---|
| Suchfenster Strg+F3, F3 weiter, **Umschalt+F3 zurück** | Beobachtet: F3 springt zum nächsten, Umschalt+F3 zum vorigen Treffer | = | app.cpp:2167–2169, Strg+F statt Strg+F3 (gewollt) |
| Vorgabe „Whole-string match only“ | Beobachtet: **angehakt** | ≈ | app_internal.h:1474 `findWhole = false` |
| „… a script item called:“ | Beobachtet: nur der **Befehlsname**, exakt und ohne Groß-/Kleinschreibung (`us` findet nichts, `gonk_allies` findet nichts) | ≈ | edit.cpp:76–87 findet auch **Argumente** und bei nicht angehaktem Schalter Teilstrings. |
| „… any item containing:“ | Beobachtet: je **einzelnem Argument**; mit Schalter exakt (`100`, `rand`), ohne Schalter Teilstring (`gonk_` findet `gonk_allies`); **nicht** der Befehlsname (`use` findet nichts), nicht über Argumentgrenzen hinweg | ≈/Fehler | edit.cpp:89–101 hängt Name und alle Argumente zu **einem** String zusammen. Mit „Whole-string“ wird dieser ganze String verglichen und trifft damit praktisch nie. Zusammen mit der Vorgabe des Originals wäre die Suche dann wertlos. |
| Start ab der Auswahl, mit Umlauf | Beobachtet: von Zeile 6/9/12 aus der jeweils *nächste* Treffer; vom Ende aus Umlauf zum ersten | ≈ | behaved beginnt immer beim ersten Treffer (`findAt = 0`, app.cpp:1643) und läuft dann um. |
| „Search string not found“ in der Statusliste | beobachtet | = | app.cpp:1670 `FindNone` (im Fenster) |
| Letzte Eingabe bleibt im Suchfenster | beobachtet | = | statische Puffer app.cpp:1617 |

### Ereignis-Editor (Dialog 134/137, dynamisch)

| Funktion | Beleg | Status | behaved |
|---|---|---|---|
| Felder je Parameter, Kopfzeile mit Befehlsname, Typzeile `<str>/<float>/<vec>/<expr>` | Beobachtet (`DECLARE`, Combo, Edit, `<str>`); Handbuch Z.51–55 | = | app.cpp:664ff. |
| Hilfezeile „(   no help comment available   )“ | beobachtet | = | app.cpp:792 `EditorNoHelp` |
| Re-Evaluate | beobachtet; Handbuch Z.57 | = | app.cpp:1552 |
| Expr!/Helper → Get, Tag, Rnd | Beobachtet: nach Expr! erscheinen Combo DECLARE_TYPE + Combo SET + `Get`, Combo TAG + `Tag`, `0.0 .... range .... 1.0` + `Rnd`; Rnd ergibt `random( 0, 1 )`. Handbuch Z.60, 518, 569 | = | app.cpp:471 `drawHelperRows`. Die Tag-Zeile hat ein zusätzliches Namensfeld (gewollt). |
| Revert, „..... was “ | Strings | = | app.cpp:1239, 1267 |
| **Browse** für Dateifelder (`!!"…!!#*.txt/.roq/.rof/sound\*.*"`) | „Browse to file“, „All files\|*.*\|\|“; bhc Z.128, Q3_Interface.h:32–48, 81 | ✗ | app.cpp:1198: `if (ImGui::Button(tr(Str::EditorBrowse))) { /* Dateiauswahl */ }`. Der Knopf **tut nichts**. |
| **„run“-Knopf je nach Dateityp**: Skript in neuer BehavEd-Instanz öffnen, .bik/.mp3 mit dem System starten, .wav über PlaySound, Warnung „only exists as an MP3, but was in the script as a WAV“ | „start %s %s“, „\BehavEd.exe“, „Unable to launch BehavEd/BINK…“, PlaySoundA | ≈ | app.cpp:1200 ruft **immer `playSound(v)`** auf, auch für Skript-, .roq- und .rof-Felder. Ton aus .pk3 mit mp3/wav-Rückfall ist besser als im Original. Ein referenziertes Skript (`SET_*SCRIPT`, `run`) zu öffnen, fehlt. |
| Alphabetisch sortierte Klapplisten | Prefs 1047; Registry `alphasort` | ✗ | settings.h:29 und app.cpp:2250: Die Option wird gespeichert, aber **nirgends ausgewertet**. |

### Übersetzen und Status

| Funktion | Beleg | Status | behaved |
|---|---|---|---|
| Compile ! | Knopf 1008; startet `compilePath` (IBIze.exe): „Could not spawn %s!“, „Ok   (%d blocks compiled)“, „Error during compile (return code %d)“; Handbuch Z.30–33 (Fehler im Feedback-Fenster, Smiley bei OK) | ≈ | app.cpp:3717: eigener Compiler statt IBIze. `ibizePath` wird **nie benutzt** (settings.h:25). Die Diagnosen `d` des Compilers werden verworfen; es erscheint nur „Compile failed“ (app.cpp:3753). Die .ibi wird **auch bei Fehlschlag geschrieben** (app.cpp:3733). Die Live-Prüfung `validate` zeigt allerdings Probleme an (app.cpp:8435). |
| Statusliste mit Zeilennummer `(%d) : %s` und Symbol | beobachtet | ≈ | Statuszeile und Meldungsfenster (app.cpp:6968, 8694) |
| Befehlshilfe unter der Liste | 1043 „(Command help appears here)“, „(Command Help):     %s“ | = | app.cpp:2894 `descFor` (mit Ersatztexten) |
| Show Types / %g floats | 1039/1040; Registry `treeviewTypesets`, `treeviewGFloats`; beobachtet `<SET_TYPES> "SET_HEALTH"` | = | tree.cpp:60, 95, 410 |
| Tooltips mit Kürzeln | 20 Tooltip-Strings | = | app.cpp:6760 `hint` |
| Alt-Kürzel der Knöpfe | `&Delete &New &Open &Save Sa&ve &Prefs A&bout E&xit &MRU &Find` | ✗ | keine Entsprechung (ImGui-Knöpfe haben keine Mnemonics). Kleinigkeit. |

### Einstellungen (Dialog 131)

| Funktion | Beleg | Status | behaved |
|---|---|---|---|
| Script Path, IBIZE, Command Description File, Source Files Path | Dialog 131 | = / ≈ | app.cpp:2216. Der IBIZE-Pfad ist ohne Wirkung (siehe Compile). |
| Re-open last file, Yes/No query, File-Open last dir, Coloured Icons | Dialog 131 | = / ≈ | Die Semantik von „Yes/No query“ ist anders (siehe oben). |
| Alpha-sort | Dialog 131 | ✗ | toter Schalter |
| OK/**Cancel** | Dialog 131 | ≈ | app.cpp:2258: nur OK, Änderungen wirken sofort. |
| „Set All Options to TREK/SOF2/JK2/JKA/XMen default“ | Die Strings zeigen **Raven-interne Laufwerke**: `w:\bin\behavEd.bhc`, `k:\util\IBIze.exe`, `\\Ravendata1\VSS\…`, `o:\…`, `x:\…`, `I:\StarTrek…` | obs. | Für Außenstehende wertlos. Ob die Knöpfe auch Endung (.txt/.icarus) oder den XMen-Python-Modus umschalten, ist nicht feststellbar. |

### Raven-intern oder obsolet (im Original vorhanden, in behaved nicht)

| Funktion | Beleg | Status |
|---|---|---|
| Pythonise ! (Skript → `<name>.py` mit `import game`, `threading.Event`, `def main()`) | Knopf 1021; „Export file to <samename>.python“; XMen-Pfad `k:\util\Python22\py22compile.exe` | obs. (nur XMen) |
| Find uncached scripted audio / voice files, .PRE Cross-Language check, Extract Speech 4 translation, English/Deutsch/Francais, Mike G | Knöpfe 1015/1016/32775/32777/1013/1018/1019/1061; `BEHAVED.PRE`, `EXTRA.PRE`, `validdirs.txt`, `q:\documents\LocalisationTextForChia`, `\maps\work\`. Beobachtet: English/Deutsch/Francais/Mike G/Extract **gesperrt** | obs. `JAscripts.zip` enthält keine einzige .pre. Zweck von „Mike G“ nicht feststellbar. |
| IBI-Cache-Bericht (Programmer/Designer-Skripte je Benutzername scork/mgummelt/…) | „%d Programmer scripts…“ | obs. |
| EF-Warnung „female Munro sound“ | Strings | obs. |
| **Input String (Dialog 141)** | Einzige Eingabeaufforderungen im Programm: „Enter Sourcesafe comment for CheckIn:“, „Blank comments not allowed - do it properly!“, „Don't try and weasel out of typing comments…“ | obs. Sehr wahrscheinlich die SourceSafe-Check-in-Abfrage, also **keine Lücke**. |

**SourceSafe (bewusst weggelassen):** Status, Check In, Check Out, Undo Check Out, History, Add, Use SourceSafe?, SS-Skriptpfad, SS-INI, Checkout-Abfrage beim Laden und Speichern.

---

## (c) Echte Lücken, nach Wichtigkeit

1. **Zwischenablage nur intern:** Das Original tauscht Skripttext (`//(BHVD)` + ICARUS-Zeilen, CF_TEXT) über die Windows-Zwischenablage aus. behaved kann weder aus Notepad oder Foren einfügen noch nach außen kopieren.
   → copy/cut zusätzlich `writeNodes` mit Kopf `//(BHVD)\r\n` als CF_TEXT schreiben; Paste liest CF_TEXT über `readScript`, bei fehlendem Kopf mit Rückfrage wie im Original.
2. **Browse-Knopf im Ereignis-Editor tut nichts** (app.cpp:1198), und „Play“ spielt auch Skript-, .roq- und .rof-Felder als Ton.
   → Dateidialog mit Basisordner und Maske aus `p.filter` (`!!"<Basis>!!#<Maske>"`) und relativem Ergebnis; bei `*.txt` statt Play „Öffnen“ in neuem Reiter (`loadPath`).
3. **Suche weicht ab und ist mit „Whole-string“ praktisch kaputt** (edit.cpp:89–101).
   → Enthält-Suche je Argument (exakt oder Teilstring); Name nur gegen den Befehlsnamen; Vorgabe `findWhole = true`; Start hinter der aktuellen Auswahl.
4. **„Yes/No query“ aus = Änderungen gehen ohne Rückfrage verloren** (app.cpp:8264).
   → Die Frage bei ungesicherten Änderungen immer stellen; die Option steuert nur zusätzliche „Open?/New?/Exit?“-Fragen.
5. **REM-Semantik:** Blöcke werden zu flachen Textzeilen; die Rücktaste schaltet nicht zurück; Un-REM erfasst benachbarte, getrennt auskommentierte Zeilen mit.
   → Rücktaste wie der Knopf umschalten lassen (app.cpp:2148); Un-REM auf den ursprünglich auskommentierten Knoten begrenzen, z. B. REM-Zustand als Knotenmerkmal statt Umwandlung in Rohzeilen, Dateiformat unverändert.
6. **Kommandozeile wird ignoriert** (main_win32.cpp:471): kein Doppelklick-Öffnen, kein „Öffnen mit“, kein Ziehen auf die EXE.
   → `CommandLineToArgvW` auswerten und `loadPath` je Argument aufrufen; optional `DragAcceptFiles`/WM_DROPFILES.
7. **Backup/Restore inkompatibel:** `x.txt.bak` statt `x.bak`, Kopie der Platte statt aktuellem Stand; Restore-Meldung „keine .bak“ wird verschluckt (app.cpp:3698).
   → `mitEndung(path, ".bak")` verwenden, den aktuellen Stand schreiben, `addStatus(msg)` ergänzen; Restore immer bestätigen lassen.
8. **Numpad +/− falsch als K/M eingetragen** (keys.cpp:28/29/43/44); Teilbaum auf-/zuklappen und Strg+Num± fehlen; Strg+T als zweites „Cut“ fehlt.
   → VK_ADD/VK_SUBTRACT als `ImGuiKey_KeypadAdd`/`KeypadSubtract` binden: ohne Strg Knoten mit Teilbaum, mit Strg alles; Strg+T → Cut.
9. **Compile:** Fehlerdiagnosen werden nicht angezeigt, .ibi wird auch bei Fehler geschrieben, IBIZE-Pfad ist ein toter Schalter.
   → `d` in die Statusliste schreiben (wie „(Zeile) : Text“), bei `!ok` keine .ibi schreiben; IBIZE-Feld entfernen oder als optionalen externen Compiler anbinden.
10. **Alpha-sort-Schalter ohne Wirkung** (settings.h:29).
    → In `comboEdit`/`typeSetCombo` die Einträge sortiert anzeigen, wenn gesetzt.
11. **Schreibgeschützte Datei:** keine Rückfrage zum Entsperren.
    → Bei gesetztem READONLY fragen und `SetFileAttributesW` aufrufen.
12. **Kleinere Punkte:**
    * Append ohne Trennzeilen und ohne .ibi-Erkennung;
    * Copy ohne Auswahl („ganzes Skript kopieren?“) fehlt;
    * MRU hat 9 statt 20 Einträge;
    * keine Rückfrage vor dem Auto-Laden und keine Absturz-Nachfrage (`legal_exit`);
    * Einstellungen ohne „Abbrechen“;
    * `*.icarus` fehlt im Hauptfilter, obwohl alle 1510 JKA-Skripte so heißen (Entscheidung des Anwenders; nur als Befund notiert);
    * Alt-Mnemonics fehlen.

Nicht als Lücke gewertet: SourceSafe, Input String (SourceSafe-Kommentar), Pythonise, die .PRE- und Lokalisierungswerkzeuge, die Spiel-Vorgabeknöpfe (Raven-Laufwerke) und Strg+D (keine beobachtbare Wirkung).

Nicht feststellbar (ohne .bhc): Clone/Delete-Details bei bekannten Befehlen, Bedeutung von „Reset!“ im Editor, genaue Wirkung von Strg+Rücktaste, Aussehen gespeicherter REM-Blöcke beim Original, Doppelklick in der Statusliste.
