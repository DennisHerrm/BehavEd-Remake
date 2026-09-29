# Umbau auf die Grafikkarte — Plan

> **Stand rc568: der Software-Rasterer ist entfernt.** Gezeichnet wird nur
> noch ueber Direct3D 11 (`gui/gpumap_win32.cpp`). Massstab bei Bildfehlern
> ist das Spiel und der OpenJK-Quelltext, nicht mehr `mapview.cpp` - dort
> steht nur noch, was beide Wege teilten (Kamera, Texturen, Stapelzustaende,
> `figurKnochen`). Was unten ueber den Rasterer steht, ist Geschichte.

Stand rc372. Dieses Papier ist die Landkarte, nicht der Code. Es hält fest,
in welcher Reihenfolge umgebaut wird und **warum diese Reihenfolge** — damit
niemand (auch nicht ich in einer späteren Runde) in der Mitte anfängt.

## Wozu überhaupt

**Achtung, die Zahlen in der ersten Fassung dieses Papiers waren falsch.**
Sie stammten aus einem Messwerkzeug, das die Effekte mit einem ZWEITEN
`renderMap`-Aufruf zeichnete. Der löscht das Bild und setzt den Tiefenpuffer
zurück (`mapview.cpp:901`) — es wurde also nichts verdeckt und jeder
Bildpunkt geschrieben. Die App macht es richtig und hängt das Effektnetz als
weitere Quelle an denselben Durchgang (`app_view3d.cpp:7082`).

Richtig gemessen, jeweils zweiter Durchgang gegen zweiten Durchgang
(`tools/bench.sh`):

```
im Vulkanschacht    Karte  35,7 ms    mit Effekten  1838,7 ms
neben dem Vulkan    Karte  38,0 ms    mit Effekten   263,4 ms
aus der Ferne       Karte  55,1 ms    mit Effekten    46,3 ms
```

**Die Effektkosten hängen extrem von der Kameralage ab.** Aus der Ferne
kosten sie nichts; steht die Kamera in der Wolke, das Fünfzigfache. Eine
einzelne Zahl ohne Kameraangabe ist wertlos — genau daran bin ich zweimal
gescheitert, einmal zu hoch und einmal zu niedrig.

Der Grund steht in der Fläche. Im Vulkanschacht decken die 1131
Effektdreiecke im Mittel **71 685 Bildpunkte**, das größte volle **307 200**
— das ganze Bild. Das ist kein Fehler: wer in einer Rauchwolke steht, sieht
Rauch bildfüllend. Es ist aber der Fall, in dem jede Beschleunigung wirken
muss.

**Nur die Teilchen.** Die Karte ist mit 36 ms kein Problem und bleibt, wo sie
ist. Wer beides umbaut, hat zwei Rasterer zu pflegen, die nie ganz gleich
aussehen.

## Was der Umbau kostet, und warum das die erste Frage ist

behaved läuft heute ohne Treiber, ohne Grafikkarte, überall gleich. Genau
das trägt die Prüfläufe:

* rc370, Faktor 29 beim Spurtest — abgenommen mit `0 Bytes anders`
* rc371, zwei Beschleunigungen — dieselbe Zusicherung
* rc369, eine Keulung, die sichtbare Effekte wegnahm — **verhindert**, weil
  die Bytes es zeigten

Auf einer Grafikkarte gilt das nicht mehr. Aras Pranckevičius (Unity):
auf vielen Plattformen muss man einige falsche Bildpunkte zulassen, weil
viele Endkunden-Grafikkarten nicht vollständig deterministisch sind.
Chromium betreibt dafür einen eigenen Dienst mit mehreren zugelassenen
Bildern je Test.

Das ist der eigentliche Preis. Nicht die Arbeit — die Prüfbarkeit.

## Die Reihenfolge

### Stufe 0 — Toleranzvergleich (rc372, **erledigt**)

`tools/bildvergleich.py`. Drei Stufen: je Kanal, je Bildpunkt, je Bild.
Vorgabe 0/0/0, also weiterhin bytegleich — die Toleranz wird nur dort
eingeschaltet, wo sie nötig ist.

Wichtig ist die Ausgabe der **Lage**: Rechteck plus ein Raster über
Bildachtel. Eine gebündelte Abweichung ist ein Fehler, eine verstreute ist
Rundungsrauschen. Diesen Unterschied muss man sehen können, sonst versteckt
die dritte Stufe genau das, was sie finden soll.

### Stufe 1 — Teilchen in halber Auflösung (angefangen, nicht eingehängt)

`src/fxbuffer.cpp` und `include/bhed/fxbuffer.h` sind geschrieben und
übersetzen: Netz nach Mischart trennen, additive Stapel in halber Auflösung,
Auflegen mit Tiefenwahl. **Von niemandem aufgerufen**, und das mit Absicht —
der Entwurf setzt noch einen zweiten Durchgang voraus, und den gibt es in der
App nicht. Er muss sich in `renderMap` einklinken.

Was er bringt, mit den richtigen Zahlen ehrlich gerechnet: vier Mal weniger
Bildpunkte, also im Vulkanschacht von 1839 ms auf etwa 470. Das ist viel und
trotzdem nicht genug. Wer davon 60 Bilder je Sekunde erwartet, wird
enttäuscht.

Die zwei Fallen stehen ausführlich in `src/fxbuffer.cpp`:

* **Der Tiefenpuffer.** Naiv verkleinert gibt es entweder Löcher an Kanten
  oder Rauch, der über Felskanten hinausblutet. Der Ausweg heißt
  *nearest-depth upsampling*.
* **Die Mischart.** Nur ADDITIVE Stapel dürfen in den kleinen Puffer -
  additives Auflegen ist linear, die Reihenfolge egal. Bei `alpha`
  entscheidet sie. In `md_am_sith` sind alle 260 Effektstapel additiv.

### Stufe 2 — es gibt sie schon, nur eine Ebene tiefer

**Der Plan war falsch.** Ich wollte `renderMap` eine Dreiecksliste ausgeben
lassen. Die gibt es intern (`Prepared`), aber sie steht in
BILDSCHIRMkoordinaten:

```cpp
struct Prepared { float sx[3]; float sy[3]; float sz[3]; ... };
```

Für eine Grafikkarte ist das genau falsch herum — sie will Weltkoordinaten
und rechnet die Projektion selbst. Eine Liste mit eingebackener Projektion
wäre wertlos.

Der richtige Schnitt liegt eine Ebene früher, und dort ist er schon da:

```cpp
struct Batch {                    // bspgeo.h:244
    int lightmap;  int shader;
    std::uint32_t firstIndex, numIndexes, firstRun, numRuns;
};
```

Ecken, Indizes, nach Shader gruppiert — genau die Form, die eine Grafikkarte
erwartet, und heute schon die EINGABE von `renderMap`.

Was fehlt, ist nicht eine Zwischenschicht, sondern eine
**Zustandsbeschreibung je Stapel**.

### Wie viele Zustände sind das? (gemessen)

`ZUSTAND=1 refrender <karte>` zählt sie. Über die Episode-3-Karten:

```
duel_invisible_hand      660 Stapel ->  24 Zustaende
duel_coruscantcity      2048 Stapel ->  20 Zustaende
md_am_sith               421 Stapel ->  14 Zustaende
duel_jeditemple         1560 Stapel ->  14 Zustaende
duel_jt_outside          521 Stapel ->   3 Zustaende
duel_executor_bridge     334 Stapel ->   3 Zustaende
```

**Nie mehr als 24.** In md_am_sith fallen 339 der 421 Stapel auf zwei
Zustände (mit und ohne Lightmap, sonst alles Vorgabe).

Das ist die eigentlich gute Nachricht für den Umbau: eine Grafikkarte bekäme
zwei Dutzend Zustandswechsel je Bild statt vierhundert. Und es heißt, dass
die Zustandsbeschreibung klein sein darf — sie muss sechzehn Merkmale
tragen, aber nur ein paar Dutzend Kombinationen davon kommen vor.

**0 Stapel ohne Textur** in jeder geprüften Karte: die Zuordnung ist
lückenlos, es gibt keinen Sonderfall zu behandeln.

### Was mitmuss, gezählt über alle 4447 Shader der 43 Karten

```
2548 x  mehrstufig        1098 x  tcMod (6 Arten)
1227 x  glow               789 x  cull != front
 730 x  rgbGen wave        373 x  tcGen environment
 174 x  alphaFunc          173 x  polygonOffset
  34 x  autosprite          29 x  deformVertexes wave
```

Zum Vergleich shanks Statuszeile: **106 318 Dreiecke, 421 Zeichenaufrufe.**
Die Menge ist für eine Grafikkarte nichts; der Aufwand steckt vollständig in
diesen sechzehn Merkmalen. Und jedes hat in diesem Projekt schon einmal einen
Fehler erzeugt — `deformVertexes` erst in rc383.

Reihenfolge nach Häufigkeit, nicht nach Bequemlichkeit:

1. **Mehrstufigkeit und Mischarten** (2548) — ohne das sieht nichts richtig aus.
2. **tcMod** (1098, sechs Arten) — reine Koordinatenrechnung, gehört in einen
   Vertex-Shader, der billigste große Brocken.
3. **rgbGen wave** (730) und **cull** (789) — je ein Zustand, kein Rechenweg.
4. **tcGen environment** (373) — braucht die Kameralage im Shader.
5. **deformVertexes** (29) und **autosprite** (34) — verändern Ecken, wenige
   Vorkommen, aber ein echter Vertex-Shader nötig.

**Abnahme:** dieselben Stapel durch den vorhandenen Rasterer müssen
BYTEGLEICH dasselbe Bild ergeben. `tools/bildvergleich.py` mit 0/0/0.

### Stufe 3 — Direct3D 11, nur für Teilchen

**Nicht** D3D12 oder Vulkan. D3D11 fasst Zustände in groben Objekten
zusammen; D3D12 legt das Pipeline-State-Objekt direkt offen, und diese
Feinsteuerung braucht ein Vorschauwerkzeug nicht. Die Oberfläche rendert
ohnehin schon über D3D11 — dieselbe Schnittstelle mitzubenutzen ist der
kleinste Schritt und macht keine zweite auf.

Der Tiefenpuffer muss dabei einmal je Bild hinüber. Das ist der Posten, der
den Gewinn auffressen kann, und er gehört **gemessen, bevor** der Rest
gebaut wird.

## Was NICHT zu tun ist

* Den Rasterer stückweise ersetzen. Ein Zwischenzustand mit Karte auf der
  CPU und Rauch auf der GPU schiebt den Tiefenpuffer jedes Bild hin und her.
* Mit D3D anfangen statt mit dem Toleranzvergleich. Ohne ihn merkt man
  Fehler erst am Bildschirm — der Zustand, aus dem uns die Bytegleichheit
  herausgeholt hat.
* Die dritte Toleranzstufe großzügig setzen, um Ruhe zu haben. Sie kann
  einen fehlenden Brustpanzer verstecken.
