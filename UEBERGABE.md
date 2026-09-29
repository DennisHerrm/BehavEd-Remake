# behaved — Übergabe für den nächsten Chat

**Stand: rc429, 2026-09-02.** Der GPU-Umbau ist funktionsfähig, aber nicht
fertig. Diese Datei ist alles, was der nächste Chat braucht.

---

## 1. Worum es geht

`behaved` ist ein Ersatz für BehavEd (ICARUS-Skripteditor für Jedi Academy /
Movie Duels), C++20 mit ImGui. Er zeigt die Karte in 3D an.

> **Stand rc568: der Software-Rasterer ist entfernt.** Gezeichnet wird nur
> noch ueber Direct3D 11 (`gui/gpumap_win32.cpp`). Massstab bei Bildfehlern
> ist das Spiel und der OpenJK-Quelltext, nicht mehr `mapview.cpp` - dort
> steht nur noch, was beide Wege teilten (Kamera, Texturen, Stapelzustaende,
> `figurKnochen`). Was unten ueber den Rasterer steht, ist Geschichte.

Bis rc386 wurde die 3D-Ansicht **allein von einem eigenen Software-Rasterer**
gezeichnet (`src/mapview.cpp`). Der war der Engpass: rund **12–15 fps** beim
Bewegen der Kamera.

Seit rc387 gibt es daneben einen **Direct3D-11-Weg**, umschaltbar über
`View → Karte über die Grafikkarte (Versuch)`. Er läuft jetzt bei
**300–1000 fps**.

**Wichtig:** Der Rasterer bleibt. Der GPU-Weg ist ein Schalter *daneben*,
damit man beide Bilder vergleichen kann. Das ist die einzige Abnahme, die es
gibt — es wird blind entwickelt (siehe Abschnitt 3).

---

## 2. Was fertig ist

```
                Rasterer   GPU     Bemerkung
Karte              ja      ja
Mover              ja      ja      Tueren, Plattformen, Schiffe
Effekte            ja      ja      Aussehen weicht noch ab (siehe offen)
Himmel             ja      ja
Figuren            ja      ja      seit rc429 an der richtigen Stelle
Gluehen            ja      ja
```

Gemessen von shank, `md_am_sith`, Detail 10:

```
Rasterer:  12-15 fps in Bewegung
GPU:       300-1000 fps, 240 fps in der Statuszeile (Oberflaeche im Leerlauf)
```

---

## 3. Die entscheidende Einschränkung

**Auf diesem Rechner läuft kein Windows und kein Direct3D.** Der ganze
D3D-Code ist blind geschrieben. Gebaut und getestet wird ausschließlich von
shank, der jedes Paket herunterlädt, baut und ein Bild plus Protokoll
zurückschickt.

Daraus folgt die Arbeitsweise, die sich bewährt hat:

* **Alles, was ohne D3D prüfbar ist, wird geprüft** — Matrizen, Packer,
  Shader-Erzeugung, Zeichenlisten. 35 datenlose Proben.
* **Der D3D-Zweig wird syntaktisch geprüft und gebunden** über
  `tools/d3dstub/` (Ersatzköpfe). Das fand zweimal Fehler, die sonst shanks
  Bau gekostet hätten.
* **Was nur im Bild sichtbar ist, findet shank.** Seine Beschreibungen sind
  die halbe Diagnose gewesen — mehrfach.

---

## 4. Werkzeuge, die dafür gebaut wurden

| Werkzeug | Zweck |
|---|---|
| `tools/d3dstub/` | Ersatzköpfe für Direct3D. Erlaubt Syntaxprüfung **und** Binden des D3D-Zweigs auf Linux. |
| `tools/d3dstub/linkcheck.cpp` | Nimmt die Adresse jeder in `gpumap.h` erklärten Funktion — findet Signaturabweichungen, die sonst erst der Windows-Binder meldet. |
| `tools/lint_cbuffer.py` | Rechnet die Feldlagen der HLSL-Konstantenpuffer nach. **Unverzichtbar** — siehe Abschnitt 6. |
| `gpu::setzeSchritt()` | Marke, die der Absturzbericht ausgibt: „GPU-Weg zuletzt: …". |
| Anzeige in der Seitenleiste | `GPU: N Aufrufe, M uebersprungen` und `Karte/Mover/Figuren/Gluehen` getrennt. |
| D3D-Debugschicht | Seit rc425 **immer an**; `BHED_D3D_DEBUG=0` schaltet ab. |

Prüflauf (`tools/check_gui.sh` plus die Proben):

```
35 datenfreie Proben          0 Fehler
43 Karten x 3 Pruefer         43 ok
IBI-Rundlauf 202 Dateien      202 bytegleich
13 Linter                     0 Beanstandungen
Oberflaeche gegen ImGui 1.93  3 Dateien uebersetzt
D3D-Zweig                     syntaktisch ok UND bindet
```

---

## 5. Was noch offen ist

### Am GPU-Weg

1. **Türen verschwinden hinter durchsichtigen Flächen.** Zeichenreihenfolge:
   Mover kommen nach der Karte, aber die gemischten Kartenflächen sind da
   schon durch. Der Rasterer sortiert alles zusammen. — *Nicht angefangen.*

2. **`worldMatrices` ohne `vorrang`.** Der Rasterer ruft
   `worldMatrices(frame, vorrang, world)` mit den Knochen, bei denen Ober-
   und Unterkörper verschiedene Bilder haben. Der GPU-Weg ruft die einfache
   Überladung. Das ergibt eine **falsche Haltung**, keinen falschen Ort.
   Im Kopf von `gla.h` steht: „Ohne sie mischte ein Bein gegen einen
   Oberkörper." — *Gefunden, nicht behoben.*

3. **Effekte sehen anders aus als im Rasterer.** Erwartbar: der Rasterer legt
   sie seit rc374 in halber Auflösung auf und zieht sie mit Tiefenwahl hoch;
   der GPU-Weg zeichnet sie voll mit. Was genau abweicht, ist ungeklärt.

4. **`mapOnGpu` wird nicht in den Einstellungen gespeichert** — der Schalter
   steht bei jedem Start auf aus. `tools/lint_settings.py` findet das nicht:
   er prüft, ob Geschriebenes gelesen wird, nicht ob eine neue Einstellung
   überhaupt geschrieben wird.

5. **`tcMod transform`** (13 Vorkommen in 43 Karten) braucht sechs Werte,
   passt nicht in ein float4, steht vorerst auf Null.

### Ältere Darstellungsfehler (vom Rasterer, unabhängig vom GPU-Weg)

6. **Feuerfontäne** (`mustafar/volcano.efx`) sieht falsch aus. Eingegrenzt,
   nicht behoben: Texturen sind da, Vermutung ist ein Band, das seine Textur
   einmal über 280 Einheiten trägt. Ungeprüft — es fehlt die Kameraposition
   aus shanks Bild.

7. **`head_glass` hat keine Texturdatei** — die Glaskuppel über Watt Tambors
   Gesicht fehlt.

8. **Kraftfeld-Textur** `textures/plasma_tfed/ffield` fehlt in den Archiven
   auf diesem Rechner → rote Schilde nicht reproduzierbar.

---

## 6. Fehler, die sich wiederholt haben — bitte lesen

Diese Sitzung hat rund fünfzehn Runden für Dinge gebraucht, die schneller
hätten gehen können. Die Muster:

**HLSL-Feldlagen.** Ein `float3` darf keine 16-Byte-Grenze überschreiten; ein
`float x[4]` belegt vier ganze Blöcke, nicht vier Werte. Zweimal falsch
gerechnet (rc392 gefangen, rc402 nicht — Ergebnis war ein Himmel aus roten
Strahlen). **Immer `tools/lint_cbuffer.py` fragen, nie schätzen.**

**Ein Puffer für mehrere Aufrufe.** `JeBild` (b0) gilt für das ganze Bild.
`zeichneMover` schrieb dort erst Nullachsen (rc420), später die kombinierte
Matrix (rc429) — beides verschob alles, was danach kam. **Wer b0 schreibt,
muss wissen, wer danach zeichnet.**

**Zwei Bedingungen, die dasselbe entscheiden.** `brauchtEcken` und
`vbZuKlein` liefen dreimal auseinander. Jetzt gleich, plus Meldung falls
doch.

**Eine Auskunft, die zu spät oder am falschen Ort steht, lügt.** Die Marke
`setzeSchritt` stand erst hinter der Absturzstelle, dann zu grob; die
Zählung stand im geschlossenen View-Menü; einmal wurde eine Zuweisung
überhaupt nicht eingefügt und meldete „Karte 0". **Jede eingefügte Zeile mit
`grep` nachprüfen.**

**Nicht auf Verdacht reparieren.** Mehrfach die falsche Stelle behoben, weil
eine Erklärung plausibel klang. Was geholfen hat, war jedes Mal: den eigenen
Ablauf Zeile für Zeile lesen, oder eine Zahl einbauen und shank fragen.

---

## 7. Was online nachgeschlagen wurde (und was es brachte)

shank hat mehrfach darum gebeten; ich habe zu lange abgewunken. Ergebnis:

* **`CreateBuffer` / `pInitialData`** — bestätigt: darf bei
  `D3D11_USAGE_IMMUTABLE` nicht NULL sein. Führte zur Absicherung in rc425.
* **Ressource gleichzeitig Ein- und Ausgang** — die Laufzeit setzt die
  Shader-Ressource dann **stillschweigend auf NULL**. Führte zu zwei
  Korrekturen im Glühdurchgang (rc427).
* **Renderziel und Tiefenpuffer müssen gleich groß sein** — führte direkt zum
  Fund „Türen verschwinden beim Bewegen" (rc427).

**Was Suchen nicht bringt:** die eigentlichen Fehler waren immer unsere
eigenen. `0xC0000005` und falsche Bilder stehen in keinem Forum. Nützlich
sind gezielte Fragen nach **Regeln der Schnittstelle**, nicht nach Symptomen.

---

## 8. Wo was liegt

```
include/bhed/gpustate.h    PipelineState, Merkmalsbits, setzeSchritt()
include/bhed/gpushader.h   HLSL-Erzeugung (Karte, Figuren, Vollbild, Gluehen)
include/bhed/gpuconst.h    Konstantenpacker, Feldlagen (60 float)
include/bhed/gpudraw.h     Zeichenliste, nurGluehende()
include/bhed/gpucam.h      baueViewProj(), multipliziere()
include/bhed/gpuskin.h     Ecken, Knochen, baueFigurWelt(), baueMoverWelt()
gui/gpumap.h               Schnittstelle des Backends
gui/gpumap_win32.cpp       DER BLINDE TEIL - alles Direct3D
gui/app_view3d.cpp:~6900   zeichneMitRasterer, der Umschaltpunkt
src/mapview.cpp            der Rasterer - die MASSGEBLICHE Fassung
AENDERUNGEN.md             jede Runde einzeln, mit Begründungen
```

**Regel:** Wenn GPU-Weg und Rasterer sich unterscheiden, hat der **Rasterer
recht**. Er ist geprüft und läuft seit hunderten Runden.

---

## 9. Vorherige Sitzungen

Die Protokolle liegen unter `/mnt/transcripts/`, Katalog in `journal.txt`.
Für den GPU-Umbau relevant:

* `2026-09-02-02-58-09-behaved-shader-gpu-rc363-rc393.txt` — Beginn des
  GPU-Wegs, Messungen, Effektkorrekturen
* Diese Sitzung — rc394 bis rc429

---

## 10. Wenn du weitermachst

Vorschlag für die Reihenfolge:

1. **`vorrang` an `worldMatrices`** — klein, klar, verbessert die Haltung.
2. **Zeichenreihenfolge** für Mover gegen gemischte Kartenflächen.
3. **`mapOnGpu` speichern** und den Einstellungs-Prüfer erweitern.
4. Erst danach die Effekte — dort ist unklar, was „richtig" heißt.

Und: **shank baut jedes Paket selbst.** Kein Paket abliefern, das nicht durch
`tools/check_gui.sh` (inklusive Binden des D3D-Zweigs) und die 35 Proben
gelaufen ist. Ein Paket, das bei ihm nicht übersetzt, kostet eine ganze
Runde.
