// bspgeo.h - die Geometrie einer JKA-Karte lesen
//
// bsp.h liest nur die Entities. Hier kommt der Rest dazu, so weit er zum
// Zeichnen gebraucht wird: Vertices, Indizes, Flaechen, Shadernamen und die
// Lightmaps.
//
// Alle Strukturgroessen sind an echten Karten NACHGEMESSEN, nicht aus einer
// Beschreibung uebernommen. Das hat sich gleich gelohnt: fuer LEAFS stand in
// meiner Vorlage 56 Byte, und bei yavin2.bsp ging das auch auf - bei
// duel_kamino_lp.bsp aber nicht. Richtig sind 48. Eine Groesse, die bei EINER
// Datei aufgeht, ist noch kein Beleg.
//
// Was hier NICHT drin ist und auch nicht hingehoert: Sichtbarkeitsdaten
// (VISIBILITY), das Lichtgitter und die Kollisionskoerper. Zum Zeichnen einer
// ganzen Karte in einem Editor braucht man das nicht - es geht nicht um
// Bildrate, sondern darum, sich zurechtzufinden.
#ifndef BHED_BSPGEO_H
#define BHED_BSPGEO_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bhed {

struct BspVertex {
    float xyz[3]{};
    float st[2]{};            // Texturkoordinate
    float lightmap[2]{};      // erste der vier Lightmap-Koordinaten
    float normal[3]{};
    std::uint8_t colour[4]{}; // erste der vier Farbstufen
};

struct BspSurface {
    enum class Type : std::uint8_t { Bad, Planar, Patch, TriangleSoup, Flare };

    Type type = Type::Bad;
    int shader = 0;
    int firstVert = 0;
    int numVerts = 0;
    int firstIndex = 0;
    int numIndexes = 0;
    // Nummer der Lightmap. Negative Werte sind keine Fehler, sondern
    // Kennungen aus dem Spiel (q_shared.h):
    //   -1  LIGHTMAP_NONE        gar keine Beleuchtung
    //   -2  LIGHTMAP_WHITEIMAGE  voll hell
    //   -3  LIGHTMAP_BY_VERTEX   das Licht steckt in den VERTEXFARBEN
    //   -4  LIGHTMAP_2D          Bildschirmelement, keine Weltflaeche
    // Bei yavin2.bsp sind 59 % der Dreiecke -3. Wer die nur grau zeichnet,
    // verliert mehr als die Haelfte der Beleuchtung.
    int lightmap = -1;
    int patchWidth = 0;       // nur bei Patch
    int patchHeight = 0;

    // --- Der Kasten um diese Flaeche -----------------------------------
    //
    // Einmal beim Lesen gerechnet, damit der Spurtest ihn nicht jedes Mal
    // aufbauen muss.
    //
    // Wofuer: traceRay lief bis rc369 durch JEDES Dreieck JEDER Flaeche im
    // Blatt, ohne vorher zu fragen, ob der Strahl die Flaeche ueberhaupt
    // trifft. Bei einer Lavaflaeche mit tausenden Dreiecken ist das der
    // ganze Aufwand.
    //
    // Gemessen an md_am_sith: das Effektnetz brauchte 351,8 ms MIT und
    // 0,6 ms OHNE Spurtests - Faktor 600 fuer ein Bild, in dem kein
    // einziges Effektdreieck landet.
    float mins[3]{};
    float maxs[3]{};
};

struct BspShader {
    std::string name;
    std::uint32_t surfaceFlags = 0;
    std::uint32_t contentFlags = 0;



    // Flaechen, die im Spiel unsichtbar sind: Clip, Trigger und alles mit
    // NODRAW. Sie stehen in der Karte, sollen aber nicht gezeichnet werden -
    // sonst steht man vor einer Wand aus Hilfsflaechen und sieht den Raum
    // nicht.
    [[nodiscard]] bool isDrawn() const noexcept;

    // Himmelsflaechen. Sie umschliessen die Karte wie ein Kasten und
    // verdecken beim Blick von aussen alles darin. Im Spiel zeichnet die
    // Engine dort die Himmelsbox; in einer Editoransicht ist es besser, sie
    // wegzulassen und in den Hintergrund zu schauen.
    [[nodiscard]] bool isSky() const noexcept;
};

struct BspGeometry {
    std::vector<BspVertex> verts;
    std::vector<std::uint32_t> indexes;
    std::vector<BspSurface> surfaces;
    std::vector<BspShader> shaders;

    // Lightmaps, jeweils 128x128 RGB. Sie stecken SCHON in der .bsp - eine
    // Karte laesst sich also mit echtem Licht zeichnen, ohne eine einzige
    // Textur aus einer .pk3 zu holen.
    static constexpr int kLightmapSize = 128;
    std::vector<std::vector<std::uint8_t>> lightmaps;
    // Breite und Hoehe je Lightmap. Die aus der .bsp sind 128x128; externe
    // (ladeExterneLightmaps) haben die Groesse ihres Bildes - q3map2 schreibt
    // sie mit -lightmapsize oft 512 oder 1024 gross. Fehlt ein Eintrag,
    // gilt 128.
    std::vector<int> lightmapBreite;
    std::vector<int> lightmapHoehe;
    [[nodiscard]] int lightmapW(std::size_t i) const {
        return (i < lightmapBreite.size() && lightmapBreite[i] > 0) ? lightmapBreite[i] : kLightmapSize;
    }
    [[nodiscard]] int lightmapH(std::size_t i) const {
        return (i < lightmapHoehe.size() && lightmapHoehe[i] > 0) ? lightmapHoehe[i] : kLightmapSize;
    }

    // Die Untermodelle ("Brush-Modelle").
    //
    // Modell 0 ist die Welt selbst, 1..n sind die beweglichen Teile: Tueren,
    // Plattformen, die Rampe des Falcon. Eine Entity verweist darauf mit
    // "model" "*12".
    //
    // Aufbau aus code/qcommon/qfiles.h, dmodel_t:
    //     float mins[3], maxs[3];
    //     int   firstSurface, numSurfaces;
    //     int   firstBrush,  numBrushes;
    // gelesen aus Lump 7 (LUMP_MODELS), genau wie R_LoadSubmodels() es tut.
    struct SubModel {
        float mins[3]{};
        float maxs[3]{};
        int firstSurface = 0;
        int numSurfaces = 0;
    };
    std::vector<SubModel> models;

    // --- Das Lichtgitter --------------------------------------------------
    //
    // Gemeldet als Unterschied zwischen Spiel und Programm: die Figuren
    // standen flach und gleich hell da, wo das Spiel eine dunkle Hoehle mit
    // hellem Ausgang zeigt.
    //
    // Der Grund: Flaechen der KARTE haben Lightmaps (Lump 14), Figuren
    // nicht. Die Engine beleuchtet sie aus einem Gitter, das der
    // Kartenkompilierer ueber den ganzen Raum legt - Lump 15 die Werte,
    // Lump 17 die Verweise darauf.
    //
    // Der Satz je Gitterpunkt ist mgrid_t (tr_local.h:742):
    //
    //     byte ambientLight[MAXLIGHTMAPS][3];   // 12
    //     byte directLight [MAXLIGHTMAPS][3];   // 12
    //     byte styles      [MAXLIGHTMAPS];      //  4
    //     byte latLong[2];                      //  2
    //                                           // = 30 Byte
    //
    // MAXLIGHTMAPS ist bei RBSP 4. An md_ga_jedi.bsp nachgemessen: 92070
    // Byte in Lump 15, das sind genau 3069 Saetze zu 30 Byte - es geht
    // restlos auf.
    struct LightGridPoint {
        std::uint8_t ambient[4][3]{};
        std::uint8_t direct[4][3]{};
        std::uint8_t styles[4]{};
        std::uint8_t latLong[2]{};
    };
    std::vector<LightGridPoint> lightGrid;
    // Lump 17: je Gitterzelle ein Verweis in lightGrid. Getrennt, weil viele
    // Zellen denselben Wert teilen - in md_ga_jedi sind es 251750 Verweise
    // auf 3069 Saetze.
    std::vector<std::uint16_t> lightArray;
    // Aus dem worldspawn-Schluessel "gridsize", Vorgabe 64/64/128
    // (R_LoadLightGrid, tr_bsp.cpp:1149 ff.).
    float lightGridSize[3] = {64.0F, 64.0F, 128.0F};
    float lightGridOrigin[3]{};
    int lightGridBounds[3]{};

    // --- Sichtbarkeit -----------------------------------------------------
    //
    // Eine .bsp bringt aus, was von wo aus zu sehen ist. Der Kompilierer
    // rechnet das einmal aus, das Spiel schlaegt nur nach - und zeichnet
    // deshalb nie die ganze Karte, sondern nur den sichtbaren Teil.
    //
    // Aufbau aus code/qcommon/qfiles.h und R_LoadVisibility():
    //   Lump 16: int numClusters, int clusterBytes, dann die Bittabelle
    //   Lump  3: dnode_t   { int planeNum; int children[2]; int mins[3]; int maxs[3]; }
    //   Lump  4: dleaf_t   { int cluster; int area; int mins[3]; int maxs[3];
    //                        int firstLeafSurface; int numLeafSurfaces; ... }
    //   Lump  5: leafSurfaces (int je Eintrag)
    //   Lump  2: dplane_t  { float normal[3]; float dist; }
    //
    // Negative Kindnummern in einem Knoten sind Blaetter: -(blatt + 1).
    struct Plane {
        float normal[3]{};
        float dist = 0.0F;
    };
    struct Node {
        int plane = 0;
        int children[2]{};
    };
    struct Leaf {
        int cluster = -1;
        int firstSurface = 0;
        int numSurfaces = 0;
        float mins[3]{};
        float maxs[3]{};
    };
    std::vector<Plane> planes;
    std::vector<Node> nodes;
    std::vector<Leaf> leafs;
    std::vector<int> leafSurfaces;
    int numClusters = 0;
    int clusterBytes = 0;
    std::vector<std::uint8_t> vis;   // numClusters * clusterBytes

    // Welche Cluster beruehrt eine Flaeche?
    //
    // Eine Flaeche liegt oft in MEHREREN Blaettern - ein Boden zieht sich
    // durch den halben Raum, und der Baum zerlegt ihn. Gezaehlt an
    // duel_mustafar: 8836 von 12531 Flaechen liegen in genau einem
    // Cluster, 3695 in mehreren, eine sogar in 99.
    //
    // Deshalb genuegt es NICHT, je Flaeche einen Cluster zu merken. Eine
    // Flaeche ist sichtbar, sobald AUCH NUR EINER ihrer Cluster sichtbar
    // ist. Wer nur den ersten prueft, laesst Boeden verschwinden.
    //
    // Der Unterschied ist messbar: mit allen Clustern bleiben auf
    // duel_mustafar im Mittel 16,1 % der Dreiecke sichtbar, mit der
    // vorsichtigen Regel (mehrdeutige immer zeichnen) 52,0 %.
    //
    // Gespeichert als eine lange Liste plus Bereich je Flaeche - das ist
    // ein Feld statt zwoelftausend kleiner Listen.
    std::vector<int> surfaceClusters;          // alle Cluster hintereinander
    std::vector<std::uint32_t> surfaceFirst;   // je Flaeche: Anfang darin
    std::vector<std::uint32_t> surfaceCount;   // je Flaeche: wie viele

    // Ist diese Flaeche von jenem Cluster aus sichtbar?
    [[nodiscard]] bool surfaceVisible(std::size_t surface, int fromCluster) const;

    // In welchem Blatt liegt dieser Punkt? -1, wenn es keinen Baum gibt.
    [[nodiscard]] int leafAt(const float p[3]) const;

    // Sieht man von diesem Cluster aus jenen? Ohne Tabelle: immer ja.
    [[nodiscard]] bool clusterVisible(int from, int to) const;

    // Ausdehnung, fuer die Anfangsstellung der Kamera.
    float mins[3]{};
    float maxs[3]{};

    [[nodiscard]] bool empty() const noexcept { return surfaces.empty(); }

    // --- Der Beschleuniger fuer traceRay ---------------------------------
    //
    // Gemessen an md_ta_sith (intro_sith, vier bluesparks-Runner an der
    // Luftschleuse): 400000 Spurtests je Bild zu je 2,2 Mikrosekunden. Der
    // Grund ist EINE Flaeche - das Modell der Luftschleuse, eine
    // Dreieckssuppe aus 720 Dreiecken. Ihr Kasten umschliesst den ganzen
    // Gang, also prueft jeder Funkenschritt im Mittel 300 ihrer Dreiecke.
    //
    // Die Hilfe (bspgeo.cpp, SpurHilfe) legt je grosser Flaeche einen
    // Baum ueber die Dreiecke und laesst nur die weg, die BEWEISBAR nicht
    // getroffen werden koennen - das Ergebnis bleibt bitgleich mit
    // traceRayEinfach. Sie wird beim ersten Spurtest gebaut und haengt an
    // DIESER Geometrie: eine Kopie baut neu (der Halter kopiert nichts),
    // und wer verts/indexes/surfaces/shaders neu anlegt oder in der Groesse
    // aendert, bekommt ebenfalls eine neue. NUR wer Ecken oder Flaechen AN
    // ORT UND STELLE ueberschreibt, muss danach spurHilfe.vergiss() rufen.
    struct SpurHilfe;
    class SpurHilfeHalter {
    public:
        SpurHilfeHalter() = default;
        ~SpurHilfeHalter();
        SpurHilfeHalter(const SpurHilfeHalter&) noexcept {}
        SpurHilfeHalter(SpurHilfeHalter&&) noexcept {}
        SpurHilfeHalter& operator=(const SpurHilfeHalter&) noexcept {
            vergiss();
            return *this;
        }
        SpurHilfeHalter& operator=(SpurHilfeHalter&&) noexcept {
            vergiss();
            return *this;
        }
        void vergiss() noexcept;
        // Nur fuer bspgeo.cpp.
        std::atomic<const SpurHilfe*> aktuell{nullptr};
        std::vector<std::shared_ptr<const SpurHilfe>> gebaut;   // unter Sperre
    };
    mutable SpurHilfeHalter spurHilfe;
};

// Fertige Dreiecke, wie sie an die Grafikkarte gehen: Patches sind dabei
// schon in Dreiecke zerlegt.
struct BspMesh {
    std::vector<BspVertex> verts;
    std::vector<std::uint32_t> indexes;
    // Je Zeichenaufruf: welche Lightmap, welcher Shader, welcher Bereich.
    struct Batch {
        int lightmap = -1;
        int shader = 0;
        std::uint32_t firstIndex = 0;
        std::uint32_t numIndexes = 0;
        // Abschnitte innerhalb dieses Zeichenaufrufs, je einer Flaeche.
        std::uint32_t firstRun = 0;
        std::uint32_t numRuns = 0;
        // --- Die Tiefe nach vorn stauchen -----------------------------
        //
        // Fuer `depthHack` (5 Vorkommen in den 376 Effektdateien): ein
        // Muendungsblitz soll vor der Wand liegen, auch wenn er sie
        // rechnerisch durchdringt.
        //
        // Die Engine schaltet dafuer NICHT den Tiefentest ab, sondern
        // staucht den Tiefenbereich (tr_backend.cpp:818):
        //
        //     qglDepthRange (0, .3);
        //
        // Der Effekt wird also nach vorn gezogen und gewinnt fast jeden
        // Vergleich - aber er verschwindet weiterhin hinter etwas, das
        // wirklich davor liegt. Das ist der Unterschied zu "ohne Tiefe",
        // und er ist sichtbar: zwei Bloecke mit depthHack verdecken sich
        // gegenseitig noch richtig.
        //
        // 1.0 heisst: unveraendert.
        float depthScale = 1.0F;

        // --- Der Zeitursprung dieses Zeichenaufrufs -------------------
        //
        // Fuer `setShaderTime` (41 Vorkommen in 32 der 391
        // Effektdateien).
        //
        // Die Engine rechnet ALLES Zeitabhaengige gegen `tess.shaderTime`,
        // nicht gegen die absolute Zeit:
        //
        //     tess.shaderTime = backEnd.refdef.floatTime
        //                     - shader->timeOffset;
        //
        // und bei FX_SET_SHADER_TIME setzt der Effekt diesen Ursprung auf
        // SEINEN Beginn (FxPrimitives.h:72, "by having the effects system
        // set the shader time, we can make animating textures start at the
        // correct time").
        //
        // Das betrifft nicht nur die Bildfolge: rgbGen wave, deformVertexes
        // wave und die ganze tcMod-Kette haengen an derselben Zahl. Eine
        // Explosion, deren Bildfolge in der Mitte anfaengt, ist der
        // sichtbare Fall; die uebrigen sind derselbe Fehler in leiser.
        //
        // Deshalb steht der Ursprung hier am BATCH und nicht an der Textur:
        // eine Textur wird zwischen allen Effekten geteilt, ein
        // Zeichenaufruf gehoert genau einem.
        //
        // 0 heisst: absolute Zeit, wie fuer die Karte.
        float shaderTime = 0.0F;

        // --- Die Farbe steckt in den ECKEN, unveraendert ----------------
        //
        // Fuer Effekte.
        //
        // Der Zeichner nimmt die Eckenfarbe bisher nur bei
        // `lightmap == -3` (LIGHTMAP_BY_VERTEX) und multipliziert sie dann
        // mit `opt.brightness` - denn dort IST sie gebackenes Licht, und
        // die Engine wendet R_ColorShiftLightingBytes darauf an.
        //
        // Bei einem Effekt ist sie das nicht. Sie kommt aus dem rgb-Block
        // der .efx und ist bereits die fertige Farbe; sie aufzuhellen waere
        // falsch. Deshalb ein eigener Schalter statt `-3`.
        //
        // Was ohne ihn geschah: `efxdraw.cpp:342` setzte `lightmap = -1`
        // ("Partikel haben kein gebackenes Licht") - richtig gemeint, aber
        // der Zeichner faellt dann auf einen FESTEN Grauwert zurueck
        // (200, 205, 214) und verwirft die Eckenfarbe.
        //
        // Gemessen an md_am_sith: der Zaehler im Effektzeichner meldete
        // orange (0.95 0.24 0.01), im Bild standen 6415 Bildpunkte auf
        // (224, 224, 224) - reines Grau, in allen drei Kanaelen derselbe
        // Wert. Additive Saettigung von Orange haette Gelb ergeben. Grau
        // kann nur heissen: die Farbe kam nie an.
        //
        // Gemeldet als "die Spritzer sind weiss".
        bool vertexColour = false;
    };

    // Ein Abschnitt: die Dreiecke EINER Flaeche der Karte.
    //
    // Wozu, wenn es doch schon Zeichenaufrufe gibt: die sind nach Shader
    // und Lightmap gruppiert und reichen quer ueber die ganze Karte. Zum
    // Keulen taugen sie deshalb nicht - gemessen an duel_mustafar umfasst
    // eine Aufrufhuelle im Mittel 6,4 % des Kartenvolumens, liegt aber
    // ueberall verstreut.
    //
    // Die Sichtbarkeitstabelle arbeitet dagegen je FLAECHE. Also merkt
    // sich jeder Aufruf, welche Flaeche welche Dreiecke beigesteuert hat,
    // und der Zeichner ueberspringt die unsichtbaren Abschnitte - ohne die
    // Gruppierung nach Shader aufzugeben, die die Aufrufzahl klein haelt.
    struct Run {
        std::uint32_t firstIndex = 0;
        std::uint32_t numIndexes = 0;
        std::uint32_t surface = 0;   // Nummer in BspGeometry::surfaces
    };
    std::vector<Run> runs;
    std::vector<Batch> batches;
};

// Nur die Flaechen EINES Untermodells zu Dreiecken machen.
//
// Gedacht fuer die beweglichen Teile: eine Tuer, eine Plattform, ein
// Raumschiff. index 0 waere die ganze Welt - dafuer gibt es buildMesh.
//
// WICHTIG fuer den Aufrufer: buildMesh(geo) enthaelt die Untermodelle
// SCHON MIT. Wer ein Teil bewegt zeichnen will, muss die Welt deshalb als
// buildModelMesh(geo, 0) bauen - sonst steht das Teil zweimal da, einmal
// bewegt und einmal an seinem gebauten Platz. In Ruhe faellt das nicht
// auf, weil beide Kopien deckungsgleich liegen; erst beim ersten
// Verschieben sieht man den Doppelgaenger. Genau so trennt es auch die
// Engine: die Welt ist Modell 0, alles andere haengt an einer Entity.
// Was an einer Stelle im Raum an Licht ankommt.
//
// Alle Werte 0..255, wie die Engine sie fuehrt. `dir` zeigt ZUR
// Lichtquelle und ist auf Laenge eins gebracht.
struct GridLight {
    float ambient[3] = {0, 0, 0};
    float directed[3] = {0, 0, 0};
    float dir[3] = {0, 0, 1};
    // Ob ueberhaupt ein brauchbarer Gitterpunkt gefunden wurde. Sonst
    // stehen oben die Vorgaben, und der Aufrufer soll bei seiner alten
    // Schattierung bleiben statt alles schwarz zu zeichnen.
    bool ok = false;
};

// Das Licht an einer Stelle aus dem Gitter holen.
//
// Zeile fuer Zeile nach R_SetupEntityLightingGrid (tr_light.cpp:135 ff.):
// die Stelle in Gitterschritte umrechnen, dann ueber die ACHT umliegenden
// Punkte mitteln, jeder mit dem Gewicht seines Anteils. Punkte, deren
// erster Stil LS_NONE ist, werden uebersprungen - der Kommentar dort sagt
// "ignore samples in walls".
//
// Die Lichtrichtung steckt als zwei Winkelbytes in latLong und wird so
// dekodiert (ebenda:269 ff.):
//
//     x = cos(lat) * sin(lng)
//     y = sin(lat) * sin(lng)
//     z = cos(lng)
// --- Ein Strahl gegen die Karte ----------------------------------------
//
// Gebraucht fuer den Einschlag der Geschosse: `blaster/wall_impact.efx`
// liegt vor, aber ohne zu wissen, WO ein Schuss auftrifft, gibt es keinen
// Ort, an dem man ihn spielen koennte.
//
// Geprueft wird gegen die DREIECKE der Flaechen, nicht gegen Brushes: die
// Brush-Lumps liest behaved nicht, die Dreiecke sind ohnehin da. Fuer
// einen Einschlagsort ist das genau genug - es geht um ein paar Einheiten,
// nicht um Spielmechanik.
//
// Vorgefiltert wird ueber den BSP-Baum: nur die Blaetter, durch die der
// Strahl laeuft, werden geprueft. Ohne das waeren es bei md_ga_jedi 2269
// Flaechen je Schuss.
struct TraceTreffer {
    bool hit = false;
    float point[3]{};    // wo getroffen wurde
    float normal[3]{};   // Normale der getroffenen Flaeche
    float fraction = 1.0F;   // 0..1 entlang der Strecke
    int surface = -1;
};

// `start` und `end` in Weltkoordinaten. Gibt den ERSTEN Treffer zurueck.
//
// Laeuft ueber BspGeometry::spurHilfe und liefert BITGLEICH dasselbe wie
// traceRayEinfach - Treffer, Punkt, Normale, Anteil, Flaeche, auch bei
// gleich weiten Treffern (es gewinnt wie dort der erste in der Reihenfolge
// Blatt, Flaeche, Dreieck). Warum das trotz weggelassener Dreiecke gilt,
// steht in bspgeo.cpp bei SpurHilfe.
[[nodiscard]] TraceTreffer traceRay(const BspGeometry& geo, const float start[3],
                                const float end[3]);

// Die Vorlage: jedes Dreieck jeder Flaeche, deren Kasten die Strecke
// beruehrt. Langsam, aber offensichtlich richtig - fuer die Proben, die
// traceRay dagegen halten (bspgeotest, efxtest).
[[nodiscard]] TraceTreffer traceRayEinfach(const BspGeometry& geo,
                                           const float start[3],
                                           const float end[3]);

// Nur fuer Proben: solange eingeschaltet, geht traceRay den Weg der Vorlage.
// So laesst sich ein ganzes Effektnetz einmal ueber die Hilfe und einmal
// ueber die Vorlage bauen und Byte fuer Byte vergleichen (efxtest).
void setzeSpurVorlage(bool an) noexcept;

[[nodiscard]] GridLight sampleLightGrid(const BspGeometry& geo,
                                        const float origin[3]);

[[nodiscard]] BspMesh buildModelMesh(const BspGeometry& geo, int index,
                                     int level = 4, bool skipSky = true);
// ^ DIESELBE Vorgabe wie buildMesh, und das war sie nicht immer.
//
// Gefunden mit duel_jt_outside aus Episode 3: dort deckt Untermodell 0 die
// GANZE Karte ab (es gibt keine beweglichen Teile), und trotzdem gab
// buildModelMesh 12 Dreiecke MEHR als buildMesh.
//
// Der Grund: `buildMesh` liess den Himmel weg (Vorgabe true),
// `buildModelMesh` nicht (Vorgabe false). Zwei Funktionen, die dasselbe
// tun sollen, mit gegenlaeufigen Vorgaben - das faellt erst auf, wenn
// jemand beide auf denselben Bereich loslaesst. In sechzehn Runden gegen
// md_ga_jedi ist es nie aufgefallen, weil dort die Bereiche verschieden
// sind.
//
// Nachgemessen, bevor ich es angeglichen habe: ueber 17 Karten und mehr
// als 300 bewegliche Teile enthaelt **kein einziges** Himmelsflaechen. Die
// Aenderung ist also nachweislich unsichtbar - sie raeumt eine Falle weg,
// ohne ein Bild zu veraendern.

[[nodiscard]] bool readBspGeometry(const std::string& bytes, BspGeometry& out,
                                   std::string* error = nullptr);

// Patches sind Bezier-Flaechen mit 3x3-Kontrollpunkten je Feld. Diese
// Funktion zerlegt sie in Dreiecke und fasst alles nach Lightmap zusammen.
//
// level ist die Unterteilung je Feld: 1 heisst nur die Ecken, 8 ist glatt
// genug fuer eine Editoransicht.
//
// skipSky laesst die Himmelsflaechen weg. Sie umschliessen die Karte wie ein
// Kasten; von aussen sieht man sonst nur die Schale.
[[nodiscard]] BspMesh buildMesh(const BspGeometry& geo, int level = 6,
                                bool skipSky = true);

// --- Externe Lightmaps (q3map2 -external) ----------------------------------
//
// R_FindLightmap, tr_shader.cpp:3370: zeigt eine Flaeche auf eine Lightmap,
// die nicht in der .bsp steht, laedt die Engine "maps/<karte>/lm_%04d.tga"
// (R_FindImageFile probiert dabei auch .jpg und .png). Findet sie nichts,
// wird die Flaeche nach Ecken beleuchtet (lightmapsVertex, -3).
//
// Gefunden mit tests/meshmasse.cpp (27.09.): zwoelf Karten der Mod bringen
// ihre Lightmaps so mit (duel_carbon, duel_exegol, duel_tower, md_ail_jedi
// ...). behaved kannte den Weg nicht - ihre Flaechen zeigten auf Lightmaps,
// die es nicht gab.
//
// `kartenOrdner` ist "maps/<karte>" (tr.worldDir), `lies` holt eine Datei
// aus den Spielordnern. VOR buildMesh rufen: die Stapel haengen an der
// Lightmapnummer. Rueckgabe: wie viele Lightmaps geladen wurden.
int ladeExterneLightmaps(BspGeometry& geo, const std::string& kartenOrdner,
                         const std::function<bool(const std::string&, std::string&)>& lies);

}  // namespace bhed
#endif
