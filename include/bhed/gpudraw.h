// gpudraw.h - aus Stapeln eine Liste von Zeichenaufrufen machen
//
// Die Aufteilung
// --------------
// Ein Weg ueber die Grafikkarte zerfaellt in zwei Teile:
//
//   WAS gezeichnet wird - welche Stapel, in welcher Reihenfolge, mit welchem
//   Zustand, und welche davon sich zusammenfassen lassen.
//
//   WIE es hingeht - Puffer anlegen, Shader uebersetzen, Zustaende setzen,
//   DrawIndexed rufen.
//
// Der zweite Teil braucht Direct3D und laesst sich auf diesem Rechner nicht
// pruefen. Der erste ist reine Datenverarbeitung - und dort sitzen die
// Fehler, die man spaeter im Bild sucht und nicht findet: eine falsche
// Reihenfolge, ein vergessener Zustandswechsel, ein Stapel der zweimal
// gezeichnet wird.
//
// Deshalb steht der erste Teil hier, getrennt und geprueft
// (tests/gpudraw.cpp).
//
// Die Reihenfolge ist das Wichtigste
// ----------------------------------
// Deckende Flaechen zuerst, gemischte danach - und die gemischten von hinten
// nach vorn. Wer das vertauscht, bekommt Rauch, der hinter Felsen liegt, oder
// Lava, die durch Waende scheint. Genau diese Klasse von Fehlern hat in
// diesem Projekt mehrere Runden gekostet (rc377: der Teilchenpuffer hatte die
// Szenentiefe nie).
#ifndef BHED_GPUDRAW_H
#define BHED_GPUDRAW_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "bhed/bspgeo.h"
#include "bhed/gpustate.h"
#include "bhed/mapview.h"

namespace bhed::gpu {

// Ein Zeichenaufruf: ein Zustand, ein Bereich im Indexpuffer, zwei Texturen.
struct DrawCall {
    PipelineState state;
    std::uint32_t firstIndex = 0;
    std::uint32_t numIndexes = 0;
    // Platz im Texturensatz. -1 heisst: keine.
    int bild = -1;
    int lightmap = -1;
    // Der Stapel, aus dem er stammt - fuer Protokolle und zum Nachschlagen
    // der tcMod-Werte.
    std::size_t batch = 0;
    // --- Eine Stufe UEBER der ersten? ---------------------------------
    //
    // Ein JKA-Shader hat mehrere Stufen: Grundbild, Lightmap, und darueber
    // das additive Leuchten oder ein zweites Bild. Die Engine geht sie der
    // Reihe nach durch (tr_shade.cpp:1624) und ueberspringt im
    // Gluehdurchgang die ohne `glow` - Stufe fuer Stufe, nicht Flaeche fuer
    // Flaeche.
    //
    // Der GPU-Weg zeichnete bis rc435 nur die ERSTE Stufe. Das additive
    // Leuchten von `table_blue` und die vier- bis fuenfstufigen Bildschirme
    // sitzen in den Stufen darueber; deshalb fand der Gluehdurchgang nur
    // zwei Aufrufe, waehrend der Rasterer 61529 Bildpunkte gluehen liess.
    //
    // `bild` zeigt dann auf den EIGENEN Platz der Stufe, nicht auf den des
    // Stapels - genau wie `texId = jobs[jobIdx].stage` im Rasterer
    // (mapview.cpp:1396).
    bool zusatzstufe = false;
};

// Baut die Liste fuer ein Bild.
//
// `zeitSekunden` geht in die zeitabhaengigen Wellen. Stapel ohne brauchbare
// Textur werden UEBERSPRUNGEN, nicht mit einer Ersatztextur gezeichnet - ein
// fehlendes Bild soll auffallen, nicht wie Lava aussehen (das war der
// Fehlschlag aus rc368).
[[nodiscard]] std::vector<DrawCall> buildDrawCalls(const BspMesh& mesh,
                                                   const TextureSet* textures,
                                                   float zeitSekunden);

// Wie viele ZUSTANDSWECHSEL enthaelt die Liste? Aufeinanderfolgende Aufrufe
// mit gleichem Zustand kosten keinen.
//
// Das ist die Zahl, um die es beim Umbau geht: gemessen kommen je Bild nie
// mehr als 24 verschiedene Zustaende vor, aber nur wenn die Liste so sortiert
// ist, dass gleiche beieinanderliegen.
[[nodiscard]] std::size_t countStateChanges(const std::vector<DrawCall>& calls);

// Die Aufrufe, die GLUEHEN - aus einer fertigen Liste herausgefiltert.
//
// Der Gluehdurchgang zeichnet dieselben Stapel ein zweites Mal, aber nur die
// leuchtenden, in einen kleineren Puffer. Der Rasterer macht es genauso
// (`stufeGlueht`, mapview.cpp:1206).
//
// Getrennt und nicht als Schalter in buildDrawCalls, weil die Reihenfolge
// dieselbe bleiben muss: derselbe Stapel soll im Gluehdurchgang an derselben
// relativen Stelle stehen wie im Hauptdurchgang. Sonst leuchtet etwas an
// einer Stelle, an der es im Hauptbild verdeckt ist.
[[nodiscard]] std::vector<DrawCall> nurGluehende(
    const std::vector<DrawCall>& calls);

// --- deformVertexes autosprite fuer den GPU-Weg ---------------------------
//
// Die Engine klappt jedes Viereck einer Autosprite-Flaeche zur Laufzeit zu
// einem kamerazugewandten Plaettchen zusammen (AutospriteDeform,
// tr_shade_calc.cpp): Mitte der vier Ecken, Radius = halbe Diagonale x
// 0,707, neue Ecken entlang der LINKS- und HOCH-Achse der Kamera, neue
// Texturkoordinaten 0..1 und eine feste Wicklung 0,1,3 / 3,1,2
// (RB_AddQuadStamp). Der Rasterer macht es genauso (mapview.cpp,
// autospriteQuad).
//
// Der Vertexshader sieht nur eine Ecke auf einmal. Deshalb steht beim
// Hochladen, was er braucht: die Mitte als Position, die Richtung der Ecke
// (+-1 links, +-1 hoch) und den Radius statt der Normalen, die neuen
// Texturkoordinaten. Die Kameraachsen kommen aus dem Bildpuffer.
struct SpriteEcke {
    std::uint32_t ecke = 0;   // Nummer in mesh.verts
    float mitte[3]{};
    float links = 0.0F;       // +1 oder -1
    float hoch = 0.0F;        // +1 oder -1
    float radius = 0.0F;
    float st[2]{};
};

// `istSprite[b]` sagt, ob Stapel b einen Autosprite-Shader hat. `indizes`
// kommt als Kopie von mesh.indexes herein; die Abschnitte der Sprite-Stapel
// werden darin mit der Wicklung der Engine neu geschrieben (gleiche Anzahl:
// sechs je Viereck). Abschnitt = eine Flaeche der Karte (mesh.runs), ohne
// Tabelle der ganze Stapel. Passt ein Abschnitt nicht in Vierecke (die
// Engine bricht dort ab), bleibt er unveraendert - wie im Rasterer.
[[nodiscard]] std::vector<SpriteEcke> baueAutosprites(
    const BspMesh& mesh, const std::vector<char>& istSprite,
    std::vector<std::uint32_t>& indizes);

// --- Die Lage: deckend oder gemischt -----------------------------------
//
// `buildDrawCalls` sortiert deckend vor gemischt - aber nur INNERHALB EINES
// NETZES. Der GPU-Weg zeichnet nacheinander Karte, Effekte, Mover, Figuren;
// jeder Durchgang ist in sich richtig sortiert, und trotzdem landen die
// gemischten Kartenflaechen vor den deckenden Tueren. Genau das ist der
// Fehler "Tueren verschwinden hinter durchsichtigen Flaechen".
//
// Die Engine hat dieses Problem nicht, weil sie gar nicht nach Kategorien
// zeichnet: `R_AddDrawSurf` legt Weltflaechen, Mover und Figuren in EINE
// Liste, gibt jedem Eintrag einen Sortierschluessel aus der Sortierstufe des
// Shaders (SS_OPAQUE, SS_DECAL, SS_SEE_THROUGH, SS_BLEND0 ...) und sortiert
// einmal ueber alles (`R_RadixSort`, tr_main.cpp).
//
// Diesen Schluessel hier nachzubauen hiesse, den GPU-Weg von Grund auf
// umzustellen: jeder Aufruf muesste seine Weltmatrix mitfuehren, statt sie
// vor dem Zeichnen in die Kameramatrix zu rechnen. Das ist die richtige
// Bauform, aber nicht in einem Zug auf einem Rechner, auf dem man das Bild
// nicht sehen kann.
//
// Deshalb der Schritt davor, der denselben Fehler behebt: das BILD laeuft
// zweimal ueber alle Quellen - erst alles Deckende, dann alles Gemischte.
// Das ist derselbe Sortierschluessel, nur mit zwei Stufen statt acht, und es
// braucht keine einzige Zeile im Direct3D-Teil.
//
// Die Reihenfolge INNERHALB einer Lage bleibt unangetastet - bei den
// gemischten haengt das Bild daran (siehe oben).
enum class Lage : std::uint8_t {
    Alles,      // wie bisher - fuer den Gluehdurchgang und zum Vergleichen
    Deckend,
    Gemischt,
};

[[nodiscard]] std::vector<DrawCall> nurLage(const std::vector<DrawCall>& calls,
                                            Lage lage);

}  // namespace bhed::gpu

#endif  // BHED_GPUDRAW_H
