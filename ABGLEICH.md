# behaved gegen das Original

Gemessen, nicht erinnert. Grundlage ist die originale `BehavEd.exe`
(Version 2.0, Raven Software, 1999) — ein 32-Bit-PE, dessen
DIALOG-Ressourcen die vollständige Bedienoberfläche enthalten: jeder Knopf,
jedes Feld, jede Beschriftung.

Wiederholbar mit:

```sh
python3 tools/originaldialoge.py <Pfad zur BehavEd.exe>
```

Das Original hat **7 Dialoge mit 75 beschrifteten Steuerelementen**.
behaved hat 367 Oberflächenzeichenketten. Verglichen wurde Beschriftung
gegen Beschriftung, normalisiert (Klein-/Großschreibung, `&`, Satzzeichen).

---

## 1. Vorhanden und deckungsgleich

Alles aus dem Hauptfenster des Originals (Dialog 102):

```
New   Open   Save   Save As   Prefs   About   Exit
Script Flow   Actions   File   Events   Status
Treeview Options   Show Types   %g floats
Compile !   Clone   Append   Copy   Paste   Cut
Application   REM   +   -
Backup   Restore
```

Aus den übrigen Dialogen:

```
About BehavEd            vorhanden
Event Editor             vorhanden (und in rc465 auf das Original gebracht)
MRU                      vorhanden
Find                     vorhanden - BEIDE Suchweisen und "Whole-string
                         match only" (Str::FindNamed, Str::FindWhole)
Preferences: Script Path, Location of IBIZE.EXE,
             Command Description File, Source Files Path,
             Re-open last file at startup,
             Alpha-sort edit pulldowns,
             Yes/No query on Open/New/Exit,
             File-Open dialog defaults to last dir,
             Use Alternative Coloured Icons        alle vorhanden
```

---

## 2. Bewusst nicht übernommen

### Visual SourceSafe (10 Steuerelemente)

```
Check In   Check Out   Undo Check Out   History   Add
Use SourceSafe?   SourceSafe script path   SourceSafe INI file location
SourceSafe Functions:
```

Microsoft Visual SourceSafe, eingestellt 2005, letzter Support 2017.
Raven hat 1999 damit gearbeitet. Es gibt keinen Grund, das nachzubauen.

### `English`

Ein einzelner Knopf, der zwischen zwei Sprachen umschaltete. behaved hat
`View → Language` mit vier Sprachen.

---

## 3. Im Original vorhanden, in behaved NICHT — und der Reihe nach zu prüfen

### 3.1 Voreinstellungen je Spiel

```
Set All Options to TREK default
Set All Options to SOF2 default
Set All Options to JK2 default
Set All Options to XMen default
Set All Options to JKA default
```

Fünf Knöpfe im Vorgabefenster, die alle Pfade und Schalter auf einen Satz
setzen. behaved hat die Einstellungen einzeln, aber keinen solchen Satz.

**Wert für shank: vermutlich gering** — er arbeitet an Jedi Academy und hat
seine Pfade einmal gesetzt. Aber es ist die einzige Bedienung des Originals,
die fehlt und nicht obsolet ist.

### 3.2 `Input String` (Dialog 141)

Ein allgemeines Eingabefenster mit einer zusätzlichen Hinweiszeile
(„extra text area for optional prompting"). Wofür genau das Original es
benutzt, sagt die Ressource nicht — das steht im Programmtext, nicht in der
Oberfläche.

**Hier fehlt mir die Auskunft.** Ich weiß nicht, an welcher Stelle das
Original danach fragt, und rate es nicht.

---

## 4. Was der Abgleich NICHT sagt

Er vergleicht **Oberflächen**, nicht Verhalten. Dass ein Knopf denselben
Namen trägt, heißt nicht, dass er dasselbe tut. Drei Beispiele aus dieser
Entwicklung, in denen der Name stimmte und das Verhalten nicht:

* `Save` schrieb den Quelltext in die geöffnete `.ibi` statt in eine `.txt`
  (rc459).
* `Compile` speicherte die Quelle nicht mit (rc459).
* `Expr!` zeigte nur einen Helfer statt einen je Spalte (rc465).

Alle drei hat shank gefunden, indem er das Original danebengelegt hat.
**Genau so geht dieser Abgleich weiter**: Oberfläche maschinell, Verhalten
am laufenden Programm.

---

## 5. Was behaved hat und das Original nicht

Nicht Gegenstand der Frage, aber es gehört zur Antwort. Das Original ist ein
Skripteditor; behaved ist zusätzlich eine Vorschau:

```
Kartenansicht mit Rasterer UND Grafikkarte
Figuren mit Animation, Skelett und Klingen
Effekte (.efx) mit Teilchen
Zeitleiste mit Kamerabahn und Ton
Modellansicht (.glm, .md3)
Direktes Lesen von .ibi (das Original braucht die .txt)
Vier Sprachen
Protokoll und Bericht (DEBUGGEN.md)
```

---

## 6. Kurzfassung

Von 75 beschrifteten Steuerelementen des Originals sind **63 vorhanden**,
**10 bewusst weggelassen** (SourceSafe), **2 offen**: die
Spiel-Voreinstellungen und das `Input String`-Fenster.

Die Oberfläche ist damit abgeglichen. Das **Verhalten** ist es nicht — und
das ist die Arbeit, die bleibt.
