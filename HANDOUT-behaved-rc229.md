# behaved — Übergabe nach rc229

Fortschreibung von `HANDOUT-behaved-rc199.md`. Alles dort Beschriebene gilt
weiter — Umgebung, Bauen, Arbeitsweise, die älteren Fallstricke. Hier steht,
was seither dazugekommen ist, und vor allem: **was diese dreißig Pakete
gekostet haben.**

---

## 1. Die teuerste Lehre: wann ein Protokoll fällig ist

Vier Pakete lang (rc206 bis rc211) habe ich einen Kamerafehler aus
Bildschirmfotos zu erschließen versucht. Jedes Mal fand ich eine plausible
Ursache, baute sie um, und der Anwender meldete dasselbe Bild zurück:

* rc206 — falscher **Bezug** beim Rückschreiben
* rc207 — ein Schlüssel ohne eigenes PAN hatte gar keine **Richtung**
* rc209 — der **Zeiger** stand woanders als der Schlüssel
* rc210 — zwei **grüne** Kameras übereinander
* rc211 — mein Zeitsprung aus rc209 machte alles schlimmer und musste zurück

Dann kam der Vorschlag des Anwenders: *baue ein Protokoll für die Kamera.*
Drei Zeilen später war es entschieden (rc213):

```
   Schluessel  Winkel -19.0  85.0 0.0
   laufend     Winkel -19.0 152.0 0.0
```

Gleicher Ort, verschiedene Winkel — auf den MOVE folgten **zwei** PAN, und
der Durchlauf nahm den letzten statt des ersten.

**Regel daraus:** wenn zwei Erklärungsversuche gescheitert sind, ist der
dritte Versuch kein Umbau, sondern eine Messung. Ein Bildschirmfoto zeigt
das Symptom; nur eine Zahl unterscheidet zwischen zwei Ursachen, die gleich
aussehen.

---

## 2. Muster, die sich mehrfach wiederholt haben

### „Zwei Wege, die dasselbe leisten sollen, und einer tut es nur halb"

Das häufigste Muster der Sitzung — fünfmal:

| | |
|---|---|
| rc189 | Verkleinern dreimal ausgeschrieben |
| rc199 | FOLLOW an einer von drei Stellen |
| rc216 | `clearMap()` räumte auf, `loadMapFromBytes()` nicht |
| rc224 | Ziehquelle nur in den Vergleichsfeldern, nicht im Hauptbaum |
| rc226 | zwei Ziehquellen mit verschiedener Nutzlast |

Gegenmittel, wo möglich: ein Prüfer. `lint_mapstate.py` (neu, der neunte)
vergleicht, was beim Leeren und was beim Wechseln weggeräumt wird.

### Nach NAMEN oder nach NUMMER? (rc217)

Der wichtigste neue Begriff dieser Sitzung.

* **Nach Namen** abgelegte Zwischenspeicher sind über einen Kartenwechsel
  hinweg harmlos: `md3Files`, `md3Meshes`, `texCache`, `skeletons`.
  Derselbe Name meint dieselbe Datei.
* **Nach Nummer** abgelegte sind es nicht: `brushMeshes` (Untermodell-
  nummer), `pickedEntity` (Index in `map.entities`), `pvsShownCluster`,
  `homeTab`, `splitPanes[].tab`. Nummern werden je Karte oder je
  Reiterliste **neu vergeben** — dieselbe Zahl meint danach etwas anderes.

Wer einen nummerierten Zwischenspeicher hinzufügt, trägt ihn in die Liste
in `tools/lint_mapstate.py` ein.

### Ein Anzeigeproblem nicht mit einer Zustandsänderung lösen (rc211)

Die Kamera-Vorschau zeigte etwas anderes als die laufende Kamera. Ich habe
den **Zeiger** springen lassen — also den Programmzustand geändert, um ein
Bild zu berichtigen. Folgen: hinter einem `camera(DISABLE)` verschwand die
Kamera ganz, jeder Schlüssel landete beim selben Zeitpunkt, und nach einem
Rückgängig stand der Zeiger woanders als das Skript.

Richtig war, das **Bild** zu ändern: bei ausgewähltem Schlüssel wird er
selbst gezeichnet. Die Zeit bleibt, wo sie ist.

---

## 3. Was fachlich dazugekommen ist

### Kamera

* **PAN-Richtung** (rc185): der zweite Vektor kodiert nur das Vorzeichen je
  Achse; bei null nimmt die Engine den kürzeren Weg. Über 180° ist der
  gewollte Weg der längere — `panDirectionFor()` setzt das Vorzeichen.
  Grenze: ein PAN fährt höchstens knapp 360°.
* **Der erste Schwenk nach dem MOVE** bestimmt den Schlüssel (rc213), nicht
  der letzte. Ein Schlüssel ohne eigenes PAN **erbt** die vorige Richtung
  (rc207) — dafür zählt der *letzte* Schwenk davor. Zwei verschiedene
  Fragen, zwei verschiedene Antworten.
* **Geschrieben wird, was die Vorschau zeigt** (rc206), absolut aus
  `gizmoAngNow + gizmoAngles`.
* **FOLLOW** (rc199) gilt an allen drei Stellen über `camStateAt()`. Ohne
  Glättung — die Engine dreht weich (`followSpeed`), wir zielen direkt.
* **Gizmo**: die Ringe bleiben beim Ziehen **stehen** (rc205), ein
  Tortenstück zeigt den Betrag. Ein mitdrehender Ring misst gegen sich
  selbst.

### Ladezeit: 8631 → 1327 ms

Eine Messreihe, deren Verlauf lehrreicher ist als das Ergebnis:

| | |
|---|---|
| rc186 | **erst messen** — vier Abschnitte einzeln |
| rc187 | Texturen sind 81 % → Bildtabelle nach Namen (wie `R_FindImageFile`) |
| rc188 | dieselbe Tabelle für die Karte — **brachte nichts**, keine Dopplungen |
| rc189 | also verteilen: 1931 → 550 ms |
| rc190 | Figuren ebenso, **erst vereinigen, dann verteilen** (12 Modelle, 4 Skelette) |
| rc191 | die Texturnamen stehen in der `.skin`, nicht im Modell |
| rc215 | vor dem Lesen nachsehen, nicht danach — 440 ms Leerlauf je Reiterwechsel |

rc188 steht bewusst im Protokoll: ein Griff, der nichts brachte, gehört
genauso dokumentiert wie einer, der wirkte.

### Zeitleiste

Zoom um den Mauspunkt (rc192), Menü mit festen Stufen und „Auf den Zeiger"
(rc194), Beschneiden auf das Zeitfeld (rc194), bündige rechte Kante trotz
Rollleiste (rc195), Obergrenze 32-fach und die Schrittwahl des Lineals
(rc196), **Dauern ziehen** über `durationArgIndex()` (rc197).

### Mehrere Skripte nebeneinander

Das begrifflich Wichtigste (rc221): **Arbeitsbereich und bearbeitetes
Skript sind zwei Fragen.**

```
homeTab    — in welchem Reiter wurde geteilt? (Reiterband, Aufteilung)
activeTab  — welches Skript wird gerade geändert? (leuchtender Rahmen)
```

Vorher war beides ein Wert, und ein Klick in ein Feld verschob das
Reiterband. Dazu: ImGui meldet **jedes Bild**, welcher Reiter ausgewählt
ist — vergleicht man das mit `activeTab`, kämpfen Band und Feldklick
gegeneinander.

Weiteres: nur das fokussierte Feld hebt hervor (rc224), kein Feld zweimal
dasselbe Skript (Riegel in rc222), Rollposition gehört zum **Skript**
(rc228), Ziehen zwischen Feldern kopiert (rc223/226).

### Karte pro Reiter

Verglichen wird die **Kennung** (`MapData::path`), nicht der Plattenpfad —
eine Mission aus einem `.pk3` hat gar keinen (rc214).

---

## 4. ImGui-Wissen, das teuer war

* **Zwei Ziehquellen am selben Element schließen einander aus** (rc226).
  ImGui nimmt die erste; die zweite bekommt nie eine Chance. Vor einer
  neuen Quelle prüfen, ob es dort schon eine gibt.
* Die Quelle muss ein **Element** sein — ein Selectable geht, ein
  Kindfenster nicht.
* Die Nutzlast wird **sofort kopiert**. Nur kleine Werte hineingeben;
  Einzelheiten daneben im Programmzustand halten (`dragFrom`,
  `dragFromTab`).
* Ob die Übergabe geklappt hat, weiß **nur das Ziel**. Also dort einfügen.
* `AlwaysAutoResize` kennt die endgültige Größe erst nach mehreren Bildern.
  Wer die Mitte festhält, muss das Einschwingen abwarten (rc228) — und
  **zwei Bilder reichen nicht immer**, siehe offene Punkte.
* Eine feste Knopfhöhe aus einem Feldmaß (`dy(kEdFieldH)`) schneidet bei
  144 dpi ab (rc227). Höhe 0 nimmt die Höhe, die zum Text passt.
* Kein `continue` in einer ImGui-Schleife — eine offene `PushID` bleibt
  stehen. Hier schon zweimal ein Absturz.

---

## 5. Offene Punkte

### Aus dem Protokoll von rc229

* **Das Ereignisfenster schwingt in neun Schritten ein**, nicht in zweien.
  Sichtbar kaum, sauber nicht. Endgröße 1066 bei 2560 Breite → mittig wäre
  747, erreicht wird 744.
* **Ein Reiter ohne eigene Karte leert die vorhandene.** Für Missionen
  richtig, für ein einzeln geöffnetes Skript nicht — dort kostet es 35 bis
  50 ms je Wechsel, hin und zurück. Naheliegend: ohne eigene Kennung die
  vorhandene stehen lassen.

### Älter, unverändert

`duel_deathstar` ist nachgerechnet und in Ordnung (rc198) — enge Karte,
9,6 % mittlere Sichtbarkeit, 76 % des Kastens Fels. Die Zeile in der
Seitenleiste sagt im Zweifel, woran es liegt.

FOLLOW ohne Glättung; Screen-Ring am Drehgizmo; `animMap`; 88
Effektprimitive ohne Darstellung; `SET_ANIM_BOTH` auf Brush-Modellen; das
Meldungsfenster, das sich beim Anwender manchmal nicht öffnen ließ.

Im Änderungsprotokoll sind **rc149, rc157 und rc163 doppelt vergeben** —
nur die Überschriften.

---

## 6. Das Protokoll als Werkzeug

Was es abdeckt: Laden mit Zeiten; Reiter, Felder und Größen **samt
Selbstprüfung**; Bearbeitungen mit Angabe des Skripts; Fokuswechsel;
Ziehen; Kartenwechsel; Sichtbarkeit je Cluster; das Ereignisfenster;
Rollpositionen; Laden und Sichern der Einstellungen.

Vier Bedingungen prüft das Programm selbst und schreibt „FEHLER":

* zwei Felder auf demselben Reiter
* ein Feld auf einem Reiter, den es nicht gibt
* der Fokus außerhalb der Felder
* mehr Felder als Reiter

Dazu bei jeder Bearbeitung: das fokussierte Feld muss den aktiven Reiter
zeigen. Bricht das, ginge eine Änderung ins falsche Skript — der
schlimmste Fehler dieses Fensters, weil er unbemerkt bleibt, bis man die
falsche Datei sichert.

Was es **nicht** abdeckt: das Zeichnen selbst, Mausbewegungen, das
Auf- und Zuklappen, und die Werte innerhalb eines Befehls beim Tippen.

---

## 7. Stand

```
35 Probenprogramme   tests/*.cpp   (28 ohne Daten, 7 brauchen den Korpus)
 9 Prüfer            tools/lint_*.py   (neu: lint_mapstate.py)
~36 100 Zeilen       src/ + gui/ + include/
Mission laden        1327 ms (vorher 8631)
Reiterwechsel        37 ms   (vorher 440)
```

Neu im Kern, wo Proben sie fassen: `image::mappingName`, `image::shrinkTo`,
`bhed::panDirectionFor`, `bhed::aimAngles`, `bhed::durationArgIndex`.

**Vor jedem Paket:** alle Proben, alle Prüfer, `check_gui.sh` — und die
Objektdateien vorher neu bauen. Ein grüner Lauf mit alten `.o`-Dateien hat
in dieser Sitzung zweimal getäuscht.
