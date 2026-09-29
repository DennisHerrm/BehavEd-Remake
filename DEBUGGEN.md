# Wie man behaved auf der Grafikkarte fehlersucht

Geschrieben nach rc459, nachdem eine Reihe von Fehlern jeweils zwei bis vier
Runden gekostet hat. Der Zweck: dass die nächsten weniger kosten.

Teils recherchiert (RenderDoc, Microsofts Anleitung zur Debugschicht), teils
aus dieser Entwicklung selbst — die zweite Sorte ist die belastbarere, weil
sie an echten Fehlern gemessen ist.

---

## 1. Die Ausgangslage ist ungewöhnlich

Der Direct3D-Teil wird **blind** entwickelt: hier gibt es keine Windows-
Maschine und keine Grafikkarte. shank baut und schickt Bilder zurück. Eine
Runde dauert deshalb Stunden, nicht Sekunden.

Daraus folgt alles Weitere. Ein Fehlversuch ist nicht billig. Es lohnt sich,
**vor** dem Paket zu messen, statt danach zu fragen.

---

## 2. Was in dieser Entwicklung tatsächlich funktioniert hat

Nach Wirkung geordnet, nicht nach Aufwand.

> **Stand rc568: der Software-Rasterer ist entfernt.** Gezeichnet wird nur
> noch ueber Direct3D 11 (`gui/gpumap_win32.cpp`). Massstab bei Bildfehlern
> ist das Spiel und der OpenJK-Quelltext, nicht mehr `mapview.cpp` - dort
> steht nur noch, was beide Wege teilten (Kamera, Texturen, Stapelzustaende,
> `figurKnochen`). Was unten ueber den Rasterer steht, ist Geschichte.

### 2.1 Der Rasterer als maßgebliche Fassung

Es gibt zwei Wege, die dasselbe Bild erzeugen sollen. Weichen sie ab, hat
**der Rasterer recht** — er ist seit Hunderten von Runden geprüft.

Das klingt selbstverständlich und ist es nicht: es heißt, dass die erste
Frage bei jedem Bildfehler lautet „was macht `mapview.cpp` an dieser
Stelle?" und nicht „was könnte im Shader falsch sein?".

Gefunden wurden so unter anderem:

* `rgbGen wave` ersetzt die Eckenfarbe, multipliziert sie nicht (rc440)
* `depthWrite` gilt bei deckenden Flächen immer (rc435)
* Figuren bekommen **keine** Helligkeit (rc455)
* die Phase wird vor dem Sinus gekürzt (rc441)

### 2.2 Eine Zahl statt einer Vermutung

Die teuersten Runden waren die, in denen ich eine plausible Erklärung
gebaut habe, statt sie vorher zu prüfen.

* rc440: „bei Detail 4 sind zu wenige Ecken da" — gemessen: 7,4 Ecken je
  Wellenlänge, also genug. Die Erklärung war falsch.
* rc452: „die Hologramme sind additive Figurenflächen" — der Bericht zeigte
  44 Figurenflächen, alle deckend. Es waren Kartenflächen.
* rc454: die Beleuchtung eingebaut, und nichts änderte sich, weil
  `0,22…1,0 × 2,0` alles über 0,5 auf Weiß klemmt. **Diese Multiplikation
  hätte ich aufschreiben können, bevor jemand baut.**

Die Regel, die daraus folgt: wenn eine Erklärung eine Zahl enthält, muss
die Zahl vor dem Paket dastehen.

### 2.3 Prüfer statt Aufmerksamkeit

Neunzehn `tools/lint_*.py` laufen vor jedem Paket. Sie sind fast alle als
Antwort auf einen echten Fehler entstanden:

| Prüfer | Der Fehler, der ihn ausgelöst hat |
|---|---|
| `lint_cbuffer` | `float x[4]` belegt in HLSL vier Blöcke, nicht vier Werte |
| `lint_doppelanzeige` | dieselbe Zahl an zwei Stellen, eine davon unsichtbar |
| `lint_merkmalsbits` | `kPolygonOffset` gesetzt, benannt, nie angewandt |
| `lint_d3dbesitz` | Neuanlegen ohne Freigabe |
| `lint_layout` | `CreateInputLayout(ein, 5, …)` bei sechs Einträgen |
| `lint_pruefcodes` | fünf Prüfcodes ohne Übersetzung, einer mit falscher Nummer |
| `lint_fassung` | zwei Pakete namens rc448 |

**Ein Prüfer, den man nicht gegen den Fehler hält, für den man ihn
geschrieben hat, ist eine Beruhigung und keine Prüfung.** `lint_merkmalsbits`
hat beim ersten und zweiten Versuch „ok" gemeldet — einmal, weil ein Eintrag
in einer Namenstabelle als Verwendung zählte, einmal, weil ein Kommentar den
Namen enthielt. Erst die dritte Fassung fand ihn.

### 2.4 Zwei Fassungen derselben Aussage laufen auseinander

Das häufigste Einzelmuster in diesem Programm. Bisher:

* `brauchtEcken` gegen `vbZuKlein` (rc438)
* zwei Fassungen der Anzeige, eine im geschlossenen Menü (rc442)
* zwei Fassungen der Figurenknochen (rc430)
* die Schrittweite einer Ecke, lokal in `zeichneKarte` (rc450)
* der Schutz dreier Glüh-Shader, der an einem vierten Feld hing (rc447)
* `CreateInputLayout(ein, 5, …)` neben einem Feld mit sechs (rc458)

Wenn zwei Stellen dasselbe entscheiden, wird eine davon irgendwann falsch.
Die Antwort ist nie „besser aufpassen", sondern eine gemeinsame Fassung
oder ein Prüfer.

---

## 3. Was das Programm dafür mitbringt

### 3.1 Der Bericht im Protokoll (rc448 ff.)

Wird beim Beenden **und beim Absturz** ohne Zutun geschrieben. Enthält:

```
behaved 1.0.0-rcNNN, gebaut …
Karte        Dreiecke, Stapel, Texturen, Feinheit
Ansicht      Weg, Helligkeit, Grundlicht, Effekte, Gluehen
Grafikkarte  Geraet, Speicher, Merkmalsstufe, ob die Debugschicht laeuft
             Zustaende, Shader mit Namen, Zeiten je Abschnitt, Aufrufe
Auffaellige Kartenstapel   autosprite / glow / unbekannte Mischart
Flaechen der Figuren       je Shader+Skin+Mischart, mit Anzahl
Meldungen der Debugschicht
```

Es gibt **keinen Knopf** dafür. Ein Werkzeug, das man erst einschalten muss,
ist genau dann aus, wenn man es braucht.

### 3.2 Die Debugschicht wird gelesen (rc448)

Sie lief seit rc425 und schrieb an den Windows-Debugger — also ins Leere.
Jetzt wird `ID3D11InfoQueue` in jedem Bild geleert, jede Meldung einmal, mit
Ortsangabe:

```
WARNUNG: … Index buffer has not enough space!   [waehrend: Gluehen: zeichnen]
```

Die erste Meldung, die je gelesen wurde, war ein echter Fehler.

### 3.3 Benannte Gegenstände (rc460)

`SetPrivateData(WKPDID_D3DDebugObjectName, …)` — der von Microsoft
vorgesehene Weg. Ohne ihn heißt jeder Puffer in jeder Meldung und in jeder
RenderDoc-Aufnahme `<unnamed>`.

Genau daran hat rc450 eine Runde gekostet: „Index buffer has not enough
space" sagte nicht, **welcher** Indexpuffer — Karte, Effekte, Mover oder
Figur haben verschiedene. Jetzt heißen sie so.

### 3.4 `BHED_D3D_STATES`

Bisektionsschalter für die Zustände (Tiefe, Keulen). Wenn ein Bildfehler
nach einer Zustandsänderung auftritt, grenzt er ihn in einem Bau ein statt
in dreien.

---

## 4. Was noch fehlt — und was am meisten brächte

### 4.1 RenderDoc (das Größte)

Ein freier Bildaufnehmer für D3D11. Er zeigt **je Zeichenaufruf**: Shader,
Mischzustand, Tiefe, Raster, alle Bindungen, den Inhalt jedes Puffers, und
er kann Zwischenziele als PNG herausschreiben. Er kann einen Pixelshader
schrittweise verfolgen.

Für diese Entwicklung heißt das: statt „auf dem Bild sind schwarze
Vierecke" bekäme ich „Aufruf 412 zeichnet `holo1` mit Mischart X gegen
Ziel Y". Die drei Runden, die die Hologramme gekostet haben, wären eine
gewesen.

Praktisch: shank startet behaved aus RenderDoc, drückt F12, schickt die
`.rdc` — oder besser, weil kleiner: RenderDoc hat eine Python-Schnittstelle,
mit der sich die Zustände als Text herausschreiben lassen.

Damit die Aufnahme etwas taugt, müssen die Shader mit Debugangaben
übersetzt werden — sonst haben die Konstanten keine Namen und der Shader
lässt sich nicht verfolgen. Seit rc461:

```
BHED_D3D_SHADERDEBUG=1
```

setzt `D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION`. Nicht immer an:
der Shader wird größer und langsamer, und behaved soll im Alltag schnell
sein.

**Beide Schalter stehen im Bericht**, sobald sie gesetzt sind:

```
Geraet: Intel(R) UHD Graphics, …, Debugschicht LAEUFT,
        Shader MIT Debugangaben (BHED_D3D_SHADERDEBUG)
```

Sonst stünde dort ein Shader ohne Debugangaben, und niemand wüsste, ob der
Schalter vergessen wurde oder nicht wirkt.

### 4.2 Auf Fehler anhalten — eingebaut seit rc461

```
BHED_D3D_BREAK=1
```

`ID3D11InfoQueue::SetBreakOnSeverity` mit `CORRUPTION` und `ERROR` hält das
Programm an der Stelle an, an der Direct3D etwas beanstandet. Der
Aufrufstapel im Debugger sagt dann unmittelbar, welcher Zeichenaufruf es
war — statt hinterher im Protokoll zu suchen.

**Nur mit angehängtem Debugger.** Ohne einen ist ein Haltepunkt ein
Absturz. Deshalb ausgeschaltet, solange der Schalter nicht steht.

Warnungen lösen nicht aus; davon gibt es zu viele, um bei jeder anzuhalten.

### 4.3 Ein Bildvergleich zwischen den beiden Wegen

`lint_bildvergleich.py` gibt es, aber es vergleicht nicht die zwei
Zeichenwege gegeneinander. Ein Lauf, der dieselbe Kamera einmal mit dem
Rasterer und einmal auf der Karte zeichnet und die Bilder abzieht, würde
jede Abweichung ohne Zutun finden — und zwar dort, wo sie entsteht.

Das ist die einzige der drei Ideen, die auch **hier** laufen könnte, wenn
Wine mit einem Software-Vulkan eingerichtet wird. Dann müsste ich für keine
Vermutung mehr um ein Bild bitten.

---

---

## 4.4 Eine Lücke im Prüfzug: MinGW ist nicht MSVC

`tools/check_gui.sh` übersetzt und **bindet** alle 54 Dateien nach Windows —
aber mit MinGW. shank baut mit MSVC.

Das ist meistens dasselbe, und einmal war es das nicht:
`WKPDID_D3DDebugObjectName` ist in `d3dcommon.h` nur **erklärt**, die
Definition liegt in `dxguid.lib`. MinGW hat sie in seinen Bibliotheken und
band klaglos. Bei shank:

```
error LNK2019: nicht aufgeloestes externes Symbol
               "WKPDID_D3DDebugObjectName"
```

**Ein bestandenes Binden hier beweist nicht, dass es dort bindet.**

`tools/lint_msvclink.py` führt die bekannten Fälle — Symbole aus
`dxguid.lib`, die nur mit dieser Bibliothek auflösen. Die Liste wächst mit
jedem Fall, der durchrutscht; vollständig ist sie nie. Der bessere Weg wäre
`clang-cl`, das MSVCs Regeln nachbildet, aber ohne die Windows-SDK-Header
geht auch das hier nicht.

Bis dahin: **bei jedem neuen Windows-Symbol nachsehen, welche Bibliothek es
braucht** — oder den Wert selbst definieren, wie es `benenne()` jetzt tut.

---

## 5. Die Kurzfassung

1. Bei jeder Abweichung zuerst in `mapview.cpp` nachsehen. Der Rasterer hat
   recht.
2. Enthält die Erklärung eine Zahl, muss die Zahl vor dem Paket dastehen.
3. Jede eingefügte Zeile mit `grep` nachprüfen. Ein Feld aus einer Struktur
   zu entfernen heißt: Struktur, Eingabelayout, Füllung.
4. Für jeden gefundenen Fehler fragen, ob ein Prüfer ihn künftig fängt —
   und den Prüfer gegen den echten Fehler halten.
5. Zwei Stellen, die dasselbe entscheiden, sind ein Fehler in Wartestellung.
6. Eine Auskunft, die zu spät oder am falschen Ort steht, lügt.
7. Der Prüfzug bindet mit MinGW, shank baut mit MSVC. Bei neuen
   Windows-Symbolen erst nachsehen, welche Bibliothek sie brauchen.
