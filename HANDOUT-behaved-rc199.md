# behaved — Übergabe nach rc199

Für die nächste Sitzung. Enthält: wie hier gearbeitet wird, was in dieser
Sitzung geschah, was nachgeschlagen wurde, was offen ist — und vor allem
die Fallstricke, die diese Sitzung gekostet hat.

Das Vorgängerdokument (`HANDOUT-behaved-rc164.md`) gilt weiter für alles,
was hier nicht widerrufen wird. Zwei Punkte darin sind **überholt**:

* Der Gizmo-Drehmodus und das Rückschreiben sind **fertig**, nicht
  „angefangen". Der Kommentar, der sie als abgeblendet beschrieb, ist weg.
* Die Umgebung: `nohup` überlebt hier die Aufrufgrenzen **nicht**. Lange
  Läufe brauchen einen Aufruf mit `timeout`.

---

## 1. Umgebung

```
Quellbaum          /home/claude/work/behaved/behaved/
OpenJK (Engine)    /home/claude/work/openjk/     git clone, enthält code/, codemp/, codeJK2/
ImGui (fixiert)    /home/claude/work/imgui/      v1.92.9b-docking
Missionsdaten      NICHT im Paket - bei Bedarf hochladen lassen
```

Beides lässt sich in ein bis zwei Minuten frisch klonen; der erste Versuch
im Hintergrund schlägt oft fehl, im Vordergrund mit `timeout` klappt es.

### Bauen und prüfen

```bash
# Kern (~2 min)
g++ -std=c++20 -O1 -Wall -Wextra -Wshadow -Werror -Iinclude -Ithird_party \
    -pthread -c src/*.cpp

# Alle Proben - MIT dem Löschen vorher, siehe Fallstrick 3
for t in tests/*.cpp; do b=$(basename $t .cpp); rm -f /tmp/$b
  g++ -std=c++20 -O1 -Wshadow -Werror -Iinclude -Ithird_party -pthread \
      $t *.o -o /tmp/$b && /tmp/$b; done

# Acht Prüfer
for l in tools/lint_*.py; do IMGUI=/home/claude/work/imgui python3 $l; done

# Oberfläche
IMGUI=/home/claude/work/imgui sh tools/check_gui.sh
```

Argumente der datengetriebenen Proben:

```
checkall   data/fixtures data/base/behaved.bhc data/base
checks     data/base/behaved.bhc data/base data/supplement.bhc
ibiround   data/fixtures data/base/behaved.bhc data/base data/supplement.bhc
edittest   dieselben vier
bspgeotest <pfad zu einer .bsp>
```

---

## 2. Wie hier gearbeitet wird

Unverändert gültig aus rc164: **messen statt vermuten**, **gegen den
OpenJK-Quelltext prüfen statt aus dem Gedächtnis**, **Gegenprobe zu jeder
Probe**, **Antworten und Kommentare auf Deutsch ohne Umlaute im Quelltext**.

Diese Sitzung hat drei Regeln dazugelegt:

### Eine Probe, die nur die Rechnung prüft, geht am Fehler vorbei

rc199: die Winkelrechnung von FOLLOW ist richtig — aber sie stand nur an
einer von drei Stellen. Die Probe prüft deshalb über den **Rückweg**
(Winkel → `forward()` → zeigt es auf das Ziel?), nicht die Formel.

rc183: der geparkte Reiter merkte sich „keine Karte" richtig; die **lebende**
Karte blieb trotzdem stehen. Die Probe prüft beide Werte.

### Was am Ding hängt, gehört an das Ding

rc164 (Kennungen), rc166 (Überschriften am Ort statt am Fokus), rc167 (blau,
weil angeklickt). Dieselbe Lehre auf drei Ebenen.

### Ein Prüfer, der auf richtigen Code anschlägt, ist schlimmer als keiner

rc167 und rc174. Beide Male habe ich eine Prüfung eingebaut, die
falschen Alarm gab, und beide Male wieder herausgenommen.

---

## 3. Was in dieser Sitzung geschah (rc165 → rc199)

### Geteilte Ansicht und Reiter

| | |
|---|---|
| rc165 | Auswahl wandert mit dem Klick; Feld-Auswahl hängt am Weg, nicht an der Zeilennummer |
| rc166 | „Script Flow" bleibt links; Aufteilung gehört dem Reiter, nicht dem Programm |
| rc167 | Nochmal anklicken hebt auf; Klick ins Leere ebenso — in allen vier Feldern |
| rc179 | Jedes Feld ein eigenes Skript; Trennlinien ziehbar; Kamera in Stufen |
| rc183 | Neuer Reiter räumt die Karte weg (Absicht war da, Umsetzung fehlte) |
| rc184 | Teilen nur so weit, wie es Skripte gibt |

### Zeichner, Kamera, Karte

| | |
|---|---|
| rc168 | Gizmo bewegt und dreht die Kamera **wirklich** — Modell war herausgelöst worden |
| rc170 | Welt/Lokal umschaltbar; `anglesFromBasis` + Rodrigues, mit Händigkeitsprobe |
| rc171 | **Raumschiff stand 7557 Einheiten daneben**: Brush-Modelle mit origin-Brush |
| rc172 | Blende deckt jetzt wirklich (sie wurde vor den Figuren gemalt) |
| rc181 | Laufweg der Figuren; Anklicken in Ansicht und Zeitleiste |
| rc182 | Klick ins Leere hebt alles auf |
| rc185 | **PAN-Richtung** beim Rückschreiben — über 180° fuhr die Engine andersherum |
| rc198 | duel_deathstar nachgerechnet; Cluster-Anzeige in der Seitenleiste |
| rc199 | FOLLOW gilt an allen drei Stellen |

### Aufteilung und Zeitleiste

| | |
|---|---|
| rc173 | Einstellungen in eine Seitenleiste rechts; Zeitleiste höhenverstellbar, Bilder/Sekunden |
| rc176 | Eine Kante für Lineal, Zeiger und Spuren |
| rc178 | Blöcke abgesetzt, beschriftet, anklickbar; `selectByPath` aus drei Abschriften |
| rc192–196 | Zoomen um den Mauspunkt, Menü, Beschneiden, bündige Kanten, Schrittweite |
| rc197 | **Dauern ziehen** — `durationArgIndex()` im Kern |

### Die Messreihe: Laden von 8631 auf 1327 ms

| | |
|---|---|
| rc186 | **Erst messen.** Vier Abschnitte einzeln aufsummiert |
| rc187 | Ergebnis: Texturen sind 81 % → Bildtabelle nach Namen (wie `R_FindImageFile`) |
| rc188 | Dieselbe Tabelle für die Karte — **brachte nichts** (keine Dopplungen) |
| rc189 | Also verteilen: Kartentexturen 1931 → 550 ms |
| rc190 | Figuren ebenso, in drei Stufen (erst vereinigen, dann verteilen) |
| rc191 | Die Texturnamen stehen in der `.skin` — Stufe 1 las sie nicht |

### Abstürze und Bindefehler, die ich verursacht habe

| | |
|---|---|
| rc174 | `BeginDisabled` ohne Gegenstück → Absturz beim Start |
| rc175 | Seitenleiste hinter einem frühen `return` → unsichtbar ohne Karte |
| rc177 | Zwei Fassungen der Seitenleiste → der leere Fall sah anders aus |
| rc180 | `selectByPath` im anonymen Namensraum → LNK2019 |

---

## 4. Was ich nachgeschlagen habe

| Suche | Ergebnis |
|---|---|
| OpenJK `cg_camera.cpp` | MOVE linear; **PAN nimmt bei Richtung null den kürzeren Weg**; FOLLOW = exponentielle Glättung auf den Gruppenmittelpunkt; JKA hat nur vier Funktionen mehr als JKO |
| OpenJK `codeJK2/icarus/` | Enthält **Interpreter und Tokenizer** — die Referenz für Quelltext→IBI. IBI-Version 1.57 in beiden Spielen |
| OpenJK `tr_image.cpp` | `AllocatedImages` nach Namen; `GenerateImageMappingName`: klein, ohne Endung |
| OpenJK `qfiles.h` | RBSP Version 1 für JKA **und** JKO |
| 3ds Max Rotate-Gizmo | Trackball, Scheibe zeigt den Betrag, äußerer Ring = Bildschirmdrehung; **wechselt bei ~90° auf Linear Roll** |
| 3ds Max Reference Coordinate System | **Je Umformung eigen** — Verschieben umstellen ändert Drehen nicht |
| ImGui Drag-and-Drop zwischen Kindfenstern | Quelle muss das Element sein, nicht das Kind; verschachtelte Ziele sind heikel |
| Gimbal Lock | Trifft uns nur bei Trackball-Freidrehung, nicht bei einer Achse je Zug |
| Software- gegen GPU-Rasterisierer | Laine/Karras: Faktor 2–8 langsamer; WARP als Gegenbeispiel. Bestätigt unsere Wahl |

---

## 5. Offene Punkte

### Wartet auf shank

* **duel_deathstar.** rc198 zeigt jetzt Cluster und Sichtbarkeit in der
  Seitenleiste. Nachgerechnet und in Ordnung: `leafAt` (19996/20000),
  Bitrechnung wie `R_ClusterPVS`, Flächenzuordnung vollständig. Die Karte
  ist mit 9,6 % mittlerer Sichtbarkeit einfach eng (md_am_sith: 20,6 %),
  und 76 % ihres Kastens sind Fels. **Fehlt Geometrie, brauche ich die
  Zeile von der Stelle.**
* **Meldungsfenster** ließ sich manchmal nicht öffnen — hier nie
  nachstellbar.

### Angefangen

* **FOLLOW ohne Glättung.** Wir zielen direkt, die Engine dreht weich
  (`followSpeed`, bildratenabhängig). Eine bildratenunabhängige Nachbildung
  wäre ein eigener Schritt.
* **Drehgizmo**: Screen-Ring und Scheibe als Rückmeldung fehlen.
* **Zeitleiste**: Ziehkante statt Prozentregler; Skriptfenster auf Symbole
  einklappen (beide seit rc173).

### Älter, unverändert

`animMap`; 88 Effektprimitive ohne Darstellung; `SET_ANIM_BOTH` auf
Brush-Modellen (das Fahrwerk); efxed `forceFeedback`; MinGW fehlt hier.

### Aufräumen

Im Änderungsprotokoll sind **rc149, rc157 und rc163 doppelt vergeben** —
nur die Überschriften. Und „Mission suchen" ist mit 841 ms inzwischen der
größte Ladeposten; dort wurde nie gemessen.

---

## 6. Fallstricke, die diese Sitzung gekostet haben

### „Zwei Fassungen desselben Dings laufen auseinander" — viermal

rc177 (Seitenleiste), rc178 (`selectByPath` dreimal), rc189 (Verkleinern
dreimal), rc199 (FOLLOW an einer von drei Stellen). Jedes Mal war die
zweite Fassung *fast* richtig, und jedes Mal fiel es erst im Bild auf.

**Lehre:** wer eine Rechnung an einer zweiten Stelle braucht, zieht sie
heraus — nicht später, sondern sofort.

### Der anonyme Namensraum, in beide Richtungen

rc174: Deklaration darin, Definition draußen → „declared static but never
defined". rc180: Definition darin, Aufrufer draußen → LNK2019. `nm` zeigt
es sofort: kleines `t` heißt TU-lokal.

### Ein grünes Programm, das gar nicht neu übersetzt wurde

Der Übersetzungslauf schlug fehl, das alte `/tmp/probe` lag noch da und
lief. Grün, aber bedeutungslos. **Vor dem Übersetzen löschen und danach
prüfen, dass die Datei wirklich existiert** — nicht auf den Rückgabewert
einer Rohrleitung vertrauen.

### Prüfer haben Lücken, die man nur beim Reinfallen sieht

`lint_link.py` zerlegte `nm` mit `split(None, 2)` und sah damit **nur
Namen ohne Leerzeichen** — `selectByPath(std::vector<...>)` fiel durch.
`lint_imgui_context.py` schlug auf Kommentartext an. Beide behoben, beide
mit Gegenprobe.

### Kein `continue`, kein `return` in einer ImGui-Schleife

rc167: der erste Entwurf sprang mit offenem `PushID` heraus. Dasselbe
Muster hat hier schon zweimal zum Absturz geführt.

---

## 7. Stand

```
35 Probenprogramme   tests/*.cpp   (28 laufen ohne Daten, 7 brauchen den Korpus)
 8 Prüfer            tools/lint_*.py
~34 900 Zeilen       src/ + gui/ + include/
Laden einer Mission  1327 ms (vorher 8631)
```

Neu in dieser Sitzung: `anglestest`, dazu Erweiterungen in `gizmotest`,
`focustest`, `camtracktest`, `imagetest`, `mapviewtest`, `bspgeotest`,
`edittest` — und `image::mappingName`, `image::shrinkTo`,
`bhed::panDirectionFor`, `bhed::aimAngles`, `bhed::durationArgIndex`,
`rotateAboutAxis`, `anglesFromBasis` im Kern, wo Proben sie fassen.

**Vor jedem Paket:** alle Proben, alle Prüfer, `check_gui.sh`.
