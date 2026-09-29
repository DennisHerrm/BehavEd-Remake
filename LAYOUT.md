# Der Fensteraufbau — Befund und Vorschlag

Geschrieben nach rc471, nachdem **drei** Runden (rc469, rc470, rc471) an
derselben Lücke gescheitert sind und shank zu Recht sagt, es sehe
zusammengeschustert aus.

Es *ist* zusammengeschustert. Hier steht, warum, was der übliche Weg wäre,
und was ich vorschlage.

---

## 1. Warum es schiefgeht

Der Aufbau rechnet Breiten und Höhen selbst aus:

```cpp
l.rest      = avail.x - l.buttonsW - gaps;
l.eventsW   = ...
l.flowW     = ...
l.topH      = avail.y - l.fussH - tabH;
l.frameH    = -(l.fussH + l.headerH);
```

Sechs Spalten- und Höhenwerte, aus einem Dutzend Summanden. Jeder Summand
ist eine Gelegenheit, sich zu vertun, und in drei Runden waren es:

* ein Rahmen, den die Statuszeile seit ihrem Umbau nicht mehr hat
* `ItemSpacing.**x**` senkrecht abgezogen
* eine Klemmung auf null, die die negative Höhe aufhob

Jede Korrektur hat die Lücke nur verschoben. Das ist kein Zufall: **wer die
Geometrie selbst rechnet, muss jede Änderung an jeder Stelle nachziehen.**

Die Recherche nennt genau das als bekannte Schwäche dieser
Bedienoberfläche: *„Cursor-based layout fights responsive design. The flow
model is paragraph-style, not constraint-based."*

---

## 2. Was der übliche Weg wäre

### 2.1 Docking — geht hier nicht

Der von ImGui vorgesehene Weg für einen Editor mit festen Bereichen ist ein
**DockSpace**: man legt einen Andockbereich über das ganze Fenster, und die
Bereiche sind eigene Fenster darin. Größen, Trennlinien, Reiter und das
Speichern der Anordnung macht ImGui.

**Nicht möglich mit der angehefteten Fassung.** `1.93.0 WIP` aus dem
Hauptzweig kennt `ImGuiConfigFlags_DockingEnable` nicht — Docking liegt in
einem eigenen Zweig. Der Wechsel wäre ein Austausch der
Bedienoberflächen-Bibliothek und damit eine eigene, größere Sache.

### 2.2 Tabellen — geht, und ist da

Seit ImGui 1.80 gibt es **Tabellen**, und sie decken laut Recherche genau
das ab, wofür man früher eigene Spaltenrechnung brauchte: *„sorting,
resizing, freezing, scrolling, custom cells."*

Für behaveds drei Spalten hieße das:

```cpp
if (ImGui::BeginTable("layout", 3,
                      ImGuiTableFlags_Resizable |
                      ImGuiTableFlags_BordersInnerV |
                      ImGuiTableFlags_NoSavedSettings)) {
    ImGui::TableSetupColumn("Events",     ImGuiTableColumnFlags_WidthStretch, 0.22f);
    ImGui::TableSetupColumn("ScriptFlow", ImGuiTableColumnFlags_WidthStretch, 0.58f);
    ImGui::TableSetupColumn("Actions",    ImGuiTableColumnFlags_WidthFixed,   buttonsW);
    ImGui::TableNextRow();
    ...
}
```

Was damit **wegfällt**:

* `l.eventsW`, `l.flowW`, `l.buttonsW`, `l.rest`, `l.total` und die
  Aufteilungsbrüche — ImGui rechnet die Breiten.
* Die von Hand gezeichneten Trennlinien und ihr Ziehen — Tabellen bringen
  Trennlinien mit, samt Ziehen und Doppelklick zum Zurücksetzen.
* Die Frage, ob die Spalten gleich hoch enden. Eine Tabellenzeile ist
  **eine** Zeile; alle Zellen enden dort, wo sie endet.

behaved benutzt Tabellen schon — die Tastenbelegung (`app.cpp:1858`). Es
wäre also nichts Neues im Programm, nur an der richtigen Stelle.

---

## 2.3 Drei Modi sind drei Oberflächen — nicht eine mit Schaltern

shanks Diagnose nach rc478: „ich glaube wir müssen alle 3 Modi als separate
UIs betrachten."

Gemessen, damit es keine Meinung bleibt:

```
leftMode kommt vor:   gui/app.cpp        24 mal
                      gui/app_view3d.cpp  2 mal
davon in computeLayout                    7 mal
davon in drawEventsList                   6 mal
```

`drawEventsList` heisst so, zeichnet aber je nach Modus etwas völlig
anderes:

| Modus | Linke Spalte enthält |
|---|---|
| Events | Modusleiste, Ereignisliste |
| Map | Modusleiste, Ebenenliste, Kartenansicht, Kamerazeile, Zeitleiste |
| Model | Modusleiste, Modellansicht, Abspielzeile |

Und `computeLayout` streut die Unterschiede über sieben Abfragen: andere
Aufteilung, andere Kopfzeilenhöhe, Knopfspalte ja oder nein, andere
Mindestbreite.

**Das ist der Grund, warum jede Korrektur an einem Modus die anderen beiden
trifft.** In den letzten Runden mehrfach passiert: die Tabellengewichte
(rc474 zerlegte die Karte), die Kopfzeilenhöhe (rc478 galt für alle drei),
die Platzhalter im Script Flow (rc477).

### Was daraus folgt

Nicht eine Anordnung mit Sonderfällen, sondern **drei Anordnungen mit
gemeinsamem Rahmen**:

```
Gemeinsam    Menüleiste, Reiter, Statuszeile, Fusshöhe, Tabelle
Je Modus     die Spalten darin: wie viele, wie breit, was steht drin
```

Konkret:

* `Layout` bekommt drei Erzeuger — `layoutEvents()`, `layoutMap()`,
  `layoutModel()` — statt sieben Abfragen in einer Funktion.
* `drawEventsList` zerfällt in `drawEventsPanel`, `drawMapPanel`,
  `drawModelPanel`. Jede zeichnet **eine** Sache.
* Die Kopfzeilenhöhe ist dann keine Ausnahme mehr, sondern eine Eigenschaft
  des jeweiligen Modus.

Danach kann jeder Modus einzeln gestaltet werden, ohne die anderen zu
gefährden — und `tools/layoutcheck.cpp` bekommt drei Aufrufe statt einem.

---

## 3. Was ich vorschlage

**Nicht noch eine Runde flicken.** Drei haben nicht gereicht, und die
vierte hätte dieselbe Aussicht.

Stattdessen der Reihe nach, jede Stufe für sich baubar und prüfbar:

1. **Die drei Spalten in eine Tabelle.** Größte Wirkung, überschaubarer
   Umfang: `drawEvents`, `drawFlowArea` und die Knopfleiste wandern in
   Zellen. Die Breitenrechnung fällt weg.
2. **Die Fußzeile über die negative Höhe.** Steht seit rc471 richtig; sie
   bleibt, und die Tabelle bekommt `-fussH` als Höhe.
3. **Die Modusleiste als vierte, feste Spalte** statt eines eigenen
   Kindfensters daneben.
4. **Erst danach** über Rahmen und Abstände reden. Solange die Geometrie
   von Hand gerechnet wird, ist jede Feinabstimmung auf Sand gebaut.

Stufe 1 allein löst, worüber shank sich beschwert: die Spalten enden
zwangsläufig gleich, weil sie in derselben Tabellenzeile stehen.

---

## 4. Was das kostet

Ehrlich geschätzt: Stufe 1 ist ein Umbau von `drawFrame`, `computeLayout`
und drei Zeichenfunktionen — mehrere hundert Zeilen, mit einer echten
Gefahr, dabei etwas anderes zu verschieben.

Dagegen steht, dass die letzten drei Runden nichts gebracht haben und die
vierte vermutlich auch nicht.

Ich schlage vor, Stufe 1 als **eigene Runde** zu machen, ohne andere
Änderungen daneben, damit ein Vergleich der Bilder etwas aussagt.

---

## 5. Was die Recherche sonst noch sagt

Zwei Punkte, die für das Aussehen mehr bringen als jede Feinabstimmung:

* **Gleiche Abstände überall.** ImGui hat dafür `ImGuiStyleVar`. Wo im
  Quelltext feste Zahlen wie `dx(91)` oder `dy(10)` stehen, entsteht mit der
  Zeit ein Flickenteppich. behaved hat davon reichlich.
* **Nicht jede Zahl einstellbar machen.** Die Spaltenbreiten sind
  gespeicherte Einstellungen (`splitButtons`, `splitEvents`). Mit einer
  Tabelle bräuchte man sie nicht — ImGui speichert Spaltenbreiten selbst,
  wenn man `NoSavedSettings` weglässt.
