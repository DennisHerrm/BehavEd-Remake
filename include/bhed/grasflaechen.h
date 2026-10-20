// grasflaechen.h - surfaceSprites wie im Spiel (rc604)
//
// Gras, Buesche, Blumen, Ranken und Regenspritzer auf den Flaechen einer
// Karte. Im Spiel steht dafuer in einer Shaderstufe
//
//     surfaceSprites <vertical|oriented|effect|flattened> <breite> <hoehe>
//                    <dichte> <fadedist>
//     ssFademax / ssFadescale / ssVariance / ssWind / ssWindIdle / ssHangdown
//     ssFaceup / ssAnyangle / ssVertskew / ssFXDuration / ssFXGrow
//     ssFXAlphaRange / ssFXWeather
//
// Die Engine zeichnet diese Stufe NICHT als Schicht auf die Flaeche
// (tr_shade.cpp:1888 MD: "We check for surfacesprites AFTER drawing
// everything else" - continue), sondern nach allen anderen Stufen viele
// kleine Vierecke darauf (tr_shade.cpp:2147 MD -> RB_DrawSurfaceSprites,
// tr_surfacesprites.cpp:1440). Jedes Dreieck der Flaeche bekommt seine
// Bilder an Stellen, die NUR von seinen Ecken abhaengen (randomindex aus der
// Summe der Koordinaten, randomchart) - an derselben Stelle stehen also im
// Spiel und hier dieselben Halme.
//
// Hier steht die Rechnung ohne GPU: aus Dreieck, Shaderstufe und Sicht die
// Vierecke, die SQuickSprite.Add (tr_quicksprite.cpp) bekaeme.
// Zeilenangaben: tr_surfacesprites.cpp aus OpenJK code/rd-vanilla (Movie
// Duels' rd-vanilla ist dieselbe Datei mit umbenannten Variablen).
#pragma once

#include "bhed/shaderscript.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bhed::gras {

// randomchart, tr_surfacesprites.cpp:41.
extern const float kZufall[256];

// Q_rsqrt (q_math.c) - die schnelle Wurzel der Engine, mit ihrem Fehler.
[[nodiscard]] float qRsqrt(float zahl) noexcept;

// Float nach byte wie MSVC: erst nach int abschneiden, dann die unteren
// acht Bit ((byte)(v1[0]+v1[1]+...), tr_surfacesprites.cpp:725).
[[nodiscard]] std::uint8_t alsByte(float v) noexcept;

// Eine Ecke von tess: input->xyz, input->normal[2] und
// input->vertexColors[..][2] (die BLAUE Farbe ist das Licht, :649).
struct Ecke {
    float xyz[3]{};
    float normalZ = 0.0F;
    float licht = 255.0F;
};

// Sicht und Wind eines Bildes - die globalen Werte von
// R_SurfaceSpriteFrameUpdate (:106) und RB_DrawSurfaceSprites (:1471).
struct Rahmen {
    float auge[3]{};               // ssViewOrigin = viewParms.ori.origin
    float links[3]{0.0F, 1.0F, 0.0F};   // ssViewRight = ori.axis[1] - zeigt nach LINKS
    float oben[3]{0.0F, 0.0F, 1.0F};    // ssViewUp = ori.axis[2]
    float reichweite = 1.0F;       // rangescalefactor (:96)
    int zeitMs = 0;                // tr.refdef.time
    float windTempo = 0.0F;        // curWindSpeed
    float grasWind[3]{};           // curWindGrassDir
    float blasWind[3]{};           // curWindBlowVect
    float wetter = 0.0F;           // curWeatherAmount
    // Von rahmenFertig gesetzt: ssfwdvector und ssrightvectors[4].
    float vorn[3]{};
    float rechts[4][3]{};
};

// :163-183 - der waagerechte Blick nach vorn und die vier leicht
// gedrehten "rechts"-Vektoren der senkrechten Bilder.
void rahmenFertig(Rahmen& r) noexcept;

// :129-161 - rangescalefactor bei einem anderen Bildwinkel als dem ersten
// (fov_x waagerecht in Grad; standardFovX: der erste zwischen 50 und 135).
[[nodiscard]] float reichweiteFaktor(float fovX, float standardFovX) noexcept;

// :186-292 im eingeschwungenen Zustand: R_GetWindSpeed/R_GetWindVector
// (tr_WorldEffects.cpp:380/397) liefern den Wind der Wetterzonen;
// windTempo, grasWind und blasWind sind dann das, wogegen die Daempfung
// (r_windDampFactor) laeuft. Boeen (Q_flrand) gibt es nur bei Wind ab 50.
void windSetzen(float tempo, const float richtung[3], Rahmen& r) noexcept;

// Ein Viereck wie SQuickSprite.Add: Ecken unten rechts, oben rechts,
// oben links, unten links (Texturkoordinaten (1,1) (1,0) (0,0) (0,1),
// tr_quicksprite.cpp:38), Farbe (licht, licht, licht, alpha).
struct Quad {
    float ecke[4][3]{};
    std::uint8_t licht = 255;
    std::uint8_t alpha = 255;
};

// Ein moeglicher Halm eines Dreiecks (vertical, flattened, oriented):
// alles, was NICHT von der Sicht abhaengt. Welche davon gezeichnet werden,
// entscheidet der Abstand (alpha) in jedem Bild - genau wie im Spiel, nur
// ohne die Zufallstabelle in jedem Bild neu zu durchlaufen.
struct Kandidat {
    float p[3]{};          // curpoint
    float s = 0.0F;        // fa*|v1|^2 + fb*|v2|^2 + fc*|v3|^2 (fuer alphapos)
    float zufallFade = 0.0F;   // randomchart[randomindex] fuer thisspritesfadestart
    float breite = 0.0F;   // width mit Vorzeichen (vor ssFadescale)
    float hoehe = 0.0F;
    float schief[2]{};     // skew (ssVertskew)
    float licht = 0.0F;    // l1*fa + l2*fb + l3*fc
    std::uint8_t rechts = 0;   // rightvectorcount
};

// Die Schleife von RB_DrawVerticalSurfaceSprites (:631-859) bzw.
// RB_DrawOrientedSurfaceSprites (:981-1113) fuer EIN Dreieck, ohne Sicht.
// Haengt an die Liste an; 0 Kandidaten, wenn die Normalen nicht passen.
void kandidaten(const SurfaceSprite& ss, const Ecke e[3], std::vector<Kandidat>& aus);

// Der Rest derselben Schleife fuer ein Bild: alpha aus dem Abstand,
// ssFadescale, additives Licht, und das Viereck (RB_VerticalSurfaceSprite
// :342 / RB_OrientedSurfaceSprite :845).
void zeichneKandidaten(const SurfaceSprite& ss, const Kandidat* k, std::size_t n, const Rahmen& r, bool additiv,
                       std::vector<Quad>& aus);

// RB_DrawEffectSurfaceSprites (:1203) fuer EIN Dreieck - die Stellen
// wandern mit der Zeit, deshalb in jedem Bild ganz.
void effektDreieck(const SurfaceSprite& ss, const Ecke e[3], const Rahmen& r, bool additiv, std::vector<Quad>& aus);

// Alles fuer ein Dreieck, wie RB_DrawSurfaceSprites (:1492) verteilt.
void dreieck(const SurfaceSprite& ss, const Ecke e[3], const Rahmen& r, bool additiv, std::vector<Quad>& aus);

// Wie weit die Bilder einer Stufe hoechstens reichen: cutdist (:574).
[[nodiscard]] inline float reichweite(const SurfaceSprite& ss, const Rahmen& r) noexcept {
    return ss.fadeMax * r.reichweite;
}

// Zeichnet die Engine die Stufe ueberhaupt als Bilder? RB_StageIteratorGeneric
// fragt nur die Stufen 1 .. numUnfoggedPasses-1 (tr_shade.cpp:2149 MD) -
// eine surfaceSprites-Stufe an Platz 0 wird weder als Schicht noch als
// Bilder gezeichnet. weatherfx nur, wenn es regnet (:1256).
[[nodiscard]] bool stufeZeichnet(const SurfaceSprite& ss, std::size_t stufenNr, float wetter) noexcept;

// --- Patches: die Dreiecke, auf denen die Engine die Halme verteilt ---------
//
// Die Stellen der Halme haengen an den Ecken jedes Dreiecks. Auf einer
// gebogenen Flaeche (Patch) sind das NICHT die Dreiecke, mit denen behaved
// die Flaeche zeichnet, sondern die der Engine: R_SubdividePatchToGrid
// (tr_curve.cpp, beim Laden, r_subdivisions 4) und in jedem Bild
// RB_SurfaceGrid (tr_surface.cpp:1523 MD), das je nach Abstand Zeilen und
// Spalten weglaesst (LodErrorForVolume, r_lodCurveError 250). Movie Duels'
// rd-vanilla naeht Patches NICHT aneinander (kein R_StitchAllPatches).
struct PatchPunkt {
    float xyz[3]{};
    float normal[3]{};
    std::uint8_t farbe[4]{};   // color[0] (die erste Lichtstufe)
};

struct PatchGitter {
    int breite = 0;
    int hoehe = 0;
    std::vector<PatchPunkt> punkte;   // [zeile * breite + spalte] wie srfGridMesh_t::verts
    std::vector<float> fehlerBreite;  // widthLodError
    std::vector<float> fehlerHoehe;   // heightLodError
    float lodMitte[3]{};              // lodOrigin (ParseMesh: Mitte von lightmapVecs[0..1])
    float lodRadius = 0.0F;
    float mins[3]{};                  // meshBounds
    float maxs[3]{};
};

// R_SubdividePatchToGrid (tr_curve.cpp:292 MD). `punkte`: breite x hoehe
// Kontrollpunkte, zeilenweise wie im .bsp.
[[nodiscard]] PatchGitter unterteilePatch(int breite, int hoehe, const PatchPunkt* punkte,
                                          float subdivisions = 4.0F);

// RB_SurfaceGrid (tr_surface.cpp:1523 MD): die benutzten Spalten und Zeilen
// fuer die Sicht `auge`/`vorn` (viewParms.ori.origin, axis[0]).
void gitterLod(const PatchGitter& g, const float auge[3], const float vorn[3], std::vector<int>& spalten,
               std::vector<int>& zeilen, float lodCurveError = 250.0F);

// Die Dreiecke daraus in der Reihenfolge von RB_SurfaceGrid (v2 v3 v1,
// v1 v3 v4 je Zelle), je drei Ecken.
void gitterDreiecke(const PatchGitter& g, const std::vector<int>& spalten, const std::vector<int>& zeilen,
                    std::vector<Ecke>& aus);

}  // namespace bhed::gras
