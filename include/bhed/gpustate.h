// gpustate.h - der Zeichenzustand eines Stapels, backendneutral
//
// Warum es das gibt
// -----------------
// Ein Weg ueber die Grafikkarte braucht je Stapel eine Handvoll Schalter:
// welche Mischformel, ob in den Tiefenpuffer geschrieben wird, welche Seiten
// weggelassen werden, und welche Rechenwege der Shader gehen muss.
//
// Das ist NICHT dasselbe wie BatchState. BatchState beschreibt, was im
// Shaderskript steht; hier steht, was die Grafikkarte davon einstellen muss.
// Dazwischen liegt eine Uebersetzung, und die ist der Teil, den man falsch
// machen kann - deshalb steht sie hier, getrennt, und nicht mitten im
// Direct3D-Code.
//
// Der praktische Grund: behaved hat ZWEI Grafikschichten (Direct3D 11 und
// OpenGL 3.3, siehe gui/backend.h). Eine Uebersetzung, die in der einen
// steckt, muesste in der anderen nachgebaut werden - und dann laufen sie
// auseinander. Genau das ist in diesem Projekt schon zweimal passiert:
//
//   rc383  deformVertexes wurde nur in den Basiseintrag kopiert, nicht in
//          die Zusatzstufe - zwei Wege, eine vergessene Zeile.
//   rc385  der Alphatest galt fuer Figuren, aber ohne die Bedingung, die
//          die Karte hatte.
//
// Und der Grund, warum es hier und nicht im Backend steht: dieser Kopf
// haengt von nichts ab, was Windows braucht. Er laesst sich auf jedem
// Rechner uebersetzen und PRUEFEN - im Gegensatz zum Direct3D-Teil selbst.
#ifndef BHED_GPUSTATE_H
#define BHED_GPUSTATE_H

#include <cstdint>

#include "bhed/mapview.h"

namespace bhed::gpu {

// --- Die Mischformel ----------------------------------------------------
//
// Die Engine kennt beliebige Kombinationen aus srcFactor und dstFactor. In
// den 43 Karten kommen davon gemessen nur eine Handvoll vor, und jede
// Grafikkarte stellt sie direkt ein. Mehr Faelle waeren nicht falsch, nur
// unnoetig - und ein unbekannter Fall soll AUFFALLEN, nicht stillschweigend
// als "irgendwas Aehnliches" durchgehen.
enum class Blend : std::uint8_t {
    Opaque,       // GL_ONE GL_ZERO - schreibt einfach
    Add,          // GL_ONE GL_ONE
    AlphaBlend,   // GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
    Filter,       // GL_DST_COLOR GL_ZERO
    AddAlpha,     // GL_SRC_ALPHA GL_ONE

    // --- Die beiden Nachzuegler ------------------------------------------
    //
    // Gemessen, nicht geraten. Mit den fuenf Faellen oben blieben uebrig:
    //
    //     md_am_sith            2 Stapel  src=ZERO dst=ONE_MINUS_SRC_COLOR
    //     duel_invisible_hand  15 Stapel  src=ONE  dst=SRC_ALPHA
    //     duel_coruscantcity    0
    //
    // Beides sind echte Mischarten, keine Ausreisser - also gehoeren sie
    // dazu. Der lange Schwanz, vor dem shaderscript.h warnt, ist damit fuer
    // die geprueften Karten abgedeckt.
    InvSrcColor,  // GL_ZERO GL_ONE_MINUS_SRC_COLOR
    OneSrcAlpha,  // GL_ONE  GL_SRC_ALPHA

    Unbekannt,    // kommt vor, ist aber nicht abgebildet - siehe unten
};

// --- Welche Rechenwege der Shader gehen muss ----------------------------
//
// Ein Bitfeld statt einzelner Schalter, weil daraus die Auswahl der
// Shadervariante wird: gleiche Bits, gleicher Shader, kein Wechsel.
//
// Gemessen ueber alle 4447 Shader der 43 Karten (die Zahl dahinter):
enum Feature : std::uint32_t {
    kNichts        = 0U,
    kLightmap      = 1U << 0,   // zweite Texturkoordinate
    kTexMod        = 1U << 1,   // 1098 - tcMod, sechs Arten
    kRgbWave       = 1U << 2,   //  730 - rgbGen wave
    kCullNone      = 1U << 3,   //  789 - cull twosided (CullMode::Two)
    kCullBack      = 1U << 4,   //        cull back
    kTexGenEnv     = 1U << 5,   //  373 - tcGen environment
    kAlphaTest     = 1U << 6,   //  174 - alphaFunc
    kPolygonOffset = 1U << 7,   //  173 - polygonOffset
    kAutosprite    = 1U << 8,   //   34 - Ecken zur Kamera drehen
    kDeform        = 1U << 9,   //   29 - deformVertexes wave
    kRgbConst      = 1U << 10,  //        rgbGen const
    kSpecular      = 1U << 11,  //        alphaGen lightingSpecular
    kGlow          = 1U << 12,  // 1227 - glueht (eigener Durchgang)
    kSky           = 1U << 13,  //        Himmelswuerfel statt Textur
    // Die Eckenfarbe ist ROH und darf NICHT aufgehellt werden.
    //
    // BspMesh::Batch::vertexColour. Gebackenes Licht in den Ecken bekommt
    // denselben Faktor wie eine Lightmap (R_ColorShiftLightingBytes,
    // tr_bsp.cpp); eine Effektfarbe aus dem rgb-Block einer .efx ist
    // dagegen schon fertig. Der Rasterer unterscheidet das seit rc3xx
    // (`kVertexBoost = p.rohEcke ? 1.0F : opt.brightness`), der GPU-Weg
    // kannte den Unterschied gar nicht - Effekte kamen dort um den Faktor
    // der Helligkeit zu hell heraus.
    kRohEcke       = 1U << 14,

    // --- alphaGen: was VOR dem Alphakanal der Textur steht ---------------
    //
    // Der Rasterer (mapview.cpp:2950) rechnet
    //
    //     alpha = (textur.a / 255) * vonAlphaGen
    //
    // und `vonAlphaGen` ist
    //
    //     Const      der feste Wert aus `alphaGen const <x>`
    //     Identity   eins - der Eckenwert gilt NICHT
    //     Vertex     der Eckenwert
    //     FromImage  ebenfalls der Eckenwert
    //
    // Der Shader nahm bis rc443 IMMER den Eckenwert. `alphaGen const 0`
    // steht unter anderem in textures/plasma_mustafar/lava.
    //
    // Zwei Bits, weil es drei Faelle sind und der vierte (Vertex und
    // FromImage) der Vorgabezustand ohne Bit ist.
    kAlphaKonst    = 1U << 15,  //        alphaGen const <x>  -> gRgb.a
    kAlphaEins     = 1U << 16,  //        alphaGen identity   -> 1.0
};

struct PipelineState {
    Blend blend = Blend::Opaque;
    // --- Der lange Schwanz: die Faktoren im Original -------------------
    //
    // `Blend` kennt sieben Faelle und `Blend::Unbekannt` fuer alles andere.
    // Der Rasterer hat nur fuenf Schubladen und muss raten; Direct3D nicht -
    // dort laesst sich JEDES Paar unmittelbar einstellen.
    //
    // `shaderscript.cpp` merkt sich die Faktoren genau dafuer: "damit laesst
    // sich der lange Schwanz seltener Kombinationen rechnen, statt ihn in
    // eine der fuenf Schubladen zu zwingen." Der GPU-Weg ist der Abnehmer.
    //
    // Beispiel aus der Karte, textures/plasma_mustafar/lava, zweite Stufe:
    //     blendFunc GL_DST_COLOR GL_SRC_ALPHA
    // Das ergibt `Blend::Unbekannt`, und der GPU-Weg zeichnete es bis rc438
    // DECKEND - die leuchtende erste Stufe wurde damit von einer flachen
    // Kopie ueberdeckt. Das war die zu dunkle Lava.
    BlendFactor srcFactor = BlendFactor::One;
    BlendFactor dstFactor = BlendFactor::Zero;
    std::uint32_t features = kNichts;

    // In den Tiefenpuffer schreiben?
    bool depthWrite = true;
    // Vergleich gegen den Tiefenpuffer. Entspricht DepthFunc aus mapview.h;
    // hier bewusst noch einmal, damit das Backend nichts aus mapview.h
    // braucht.
    bool depthTestEqual = false;   // depthFunc equal
    bool depthTestOff = false;     // depthFunc disable

    // Schwelle fuer alphaFunc, in 0..1. Nur gueltig wenn kAlphaTest gesetzt
    // ist.
    float alphaSchwelle = 0.0F;

    // Wie viele tcMod-Schritte, in ihrer Reihenfolge. Der Shader braucht die
    // Zahl, um die Schleife zu begrenzen.
    int numTexMods = 0;

    // Gleiche Bits und gleiche Mischart heisst: derselbe Zeichenaufruf kann
    // weiterlaufen. Das ist die Zahl, die zaehlt - gemessen kommen je Bild
    // nie mehr als 24 verschiedene vor.
    [[nodiscard]] bool gleichWie(const PipelineState& a) const {
        return blend == a.blend && srcFactor == a.srcFactor &&
               dstFactor == a.dstFactor && features == a.features &&
               depthWrite == a.depthWrite &&
               depthTestEqual == a.depthTestEqual &&
               depthTestOff == a.depthTestOff &&
               numTexMods == a.numTexMods &&
               alphaSchwelle == a.alphaSchwelle;
    }
};

// Uebersetzt den Shaderzustand in das, was die Grafikkarte einstellen muss.
//
// `hatLightmap` kommt aus dem Stapel (Batch::lightmap >= 0) und nicht aus
// dem Zustand - deshalb als eigener Parameter.
// `zusatzstufe` entscheidet ueber die Mischart der GRUNDSTUFE.
//
// Ein JKA-Shader schreibt "Lightmap, dann Textur multiplizieren" als zwei
// Stufen:
//
//     { map $lightmap }
//     { map textures/... blendFunc GL_DST_COLOR GL_ZERO }
//
// behaved fasst beide zu EINEM Zeichenaufruf zusammen und rechnet die
// Lightmap im Shader. Das Multiplizieren ist damit schon erledigt - der
// Aufruf muss DECKEND sein. Bleibt er `Filter`, multipliziert er gegen den
// leeren Bildspeicher, und die Flaeche wird schwarz.
//
// Der Rasterer macht genau diese Unterscheidung (mapview.cpp:1472):
// `Filter` gilt nur bei einer Stufe ueber der ersten; sonst wird `Opaque`
// daraus, und alles ausser Add, Alpha und AddAlpha ebenso.
// `festeFarbe`: die Farbe steht fest (rgbGen const/wave oder identity) -
// keine Eckenfarbe, kein Helligkeitsfaktor. Siehe lichtFuer.
[[nodiscard]] PipelineState pipelineFor(const BatchState& bs, bool hatLightmap,
                                        bool rohEcke = false,
                                        bool zusatzstufe = false,
                                        bool festeFarbe = false);

// --- Woher kommt das Licht einer Stufe? ----------------------------------
//
// Nach der Engine (tr_shader.cpp):
//
//   * Shader OHNE Skript (R_FindShader baut ihn selbst): Lightmap x Textur
//     bei lightmap >= 0, sonst die Eckenfarbe. Das tat behaved immer.
//   * Geskriptet MIT `$lightmap`-Stufe: die Lightmap (fuer die Grundstufe,
//     mit der behaved sie zusammenfasst). Auf -3 ersetzt FinishShader die
//     Lightmap durch `rgbGen exactVertex` - also die Eckenfarbe.
//   * Geskriptet mit `rgbGen vertex/exactVertex/...`: die Eckenfarbe.
//   * Sonst: feste Farbe. rgbGen const/wave, ohne rgbGen identity bzw.
//     identityLighting (ParseStage, tr_shader.cpp:1749). Kein Licht und
//     kein Helligkeitsfaktor: `rgbGen wave` ist EvalWaveForm x
//     identityLight (tr_shade_calc.cpp:700), am Schirm also der Wellenwert
//     selbst - in beiden Overbright-Stellungen.
//
// Eine Effektfarbe (`vertexColour` am Stapel) ist fertig und bleibt roh.
struct Licht {
    bool lightmap = false;   // kLightmap
    bool roh = false;        // kRohEcke, Eckenfarbe ohne Faktor
    bool fest = false;       // feste Farbe aus gRgb, ohne Faktor
};
[[nodiscard]] Licht lichtFuer(const BatchState& bs, int lightmap,
                              bool vertexColour, bool zusatzstufe);

// Der Name einer Mischart, fuer Protokolle und Fehlermeldungen.
[[nodiscard]] const char* blendName(Blend b);

// --- Wo war der GPU-Weg zuletzt? ---------------------------------------
//
// Ein Absturz meldet eine Adresse, und die sagt nichts, solange man kein
// Symbolverzeichnis hat. Ein Wort sagt alles.
//
// Gemeldet zweimal an derselben Stelle (beide Adressen enden auf 35C7),
// beim Laden einer Mission mit eingeschaltetem Schalter. Zwei Versuche, es
// durch Lesen zu finden, sind gescheitert - also soll das Programm es beim
// naechsten Mal selbst sagen.
//
// Kostet einen Zeigerschreibvorgang je Abschnitt. Der Absturzbericht in
// gui/main_win32.cpp liest ihn aus.
void setzeSchritt(const char* was);
[[nodiscard]] const char* letzterSchritt();

}  // namespace bhed::gpu

#endif  // BHED_GPUSTATE_H
