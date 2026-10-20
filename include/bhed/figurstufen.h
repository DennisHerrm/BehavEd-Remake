// figurstufen.h - rc602: die Shaderstufen einer Figurenflaeche wie im Spiel.
//
// Gefunden mit der Kartenpruefung: auf 161 von 278 Karten tragen die Figuren
// Shader mit mehreren Stufen - Glanz (alphaGen lightingSpecular, "detail"),
// Chrom (tcGen environment), eine getoente Grundstufe (rgbGen const, die
// Kampfdroiden), blinkende Stufen (rgbGen wave), animMap. behaved zeichnete
// nur das Bild mit dem Hautnamen und die glow-Stufen.
//
// Die Engine nimmt fuer einen Hautnamen ZUERST das Shaderskript
// (R_FindShader: ScanAndLoadShaderFiles-Eintrag vor R_FindImageFile) und
// zeichnet dessen Stufen nacheinander (RB_IterateStagesGeneric), je Stufe die
// Farbe aus ComputeColors (tr_shade.cpp:1403 ff. MD) und die
// Texturkoordinaten aus ComputeTexCoords. Hier steht, was davon an einer
// Figur gilt - rein, ohne Grafikkarte, fuer den Unit-Test.
#pragma once

#include "bhed/shaderscript.h"

#include <cstddef>
#include <vector>

namespace bhed {

enum class FigurFarbe : std::uint8_t {
    Licht,     // CGEN_LIGHTING_DIFFUSE(_ENTITY): RB_CalcDiffuseColor
    Weiss,     // identity, identityLighting, entity (shaderRGBA weiss),
               // vertex/exactVertex (G2-Flaechen haben keine Eckenfarben)
    Fest,      // CGEN_CONST
    Welle,     // CGEN_WAVEFORM: RB_CalcWaveColor (zur Zeit ausgewertet)
    Schwarz,   // oneMinusEntity / oneMinusVertex auf Weiss
};
enum class FigurAlpha : std::uint8_t {
    Eins,      // AGEN_IDENTITY (und vertex auf Weiss)
    Spekular,  // AGEN_LIGHTING_SPECULAR: RB_CalcSpecularAlpha
    Fest,      // AGEN_CONST
    Welle,     // AGEN_WAVEFORM
};

struct FigurStufenArt {
    std::size_t stufe = 0;   // Index in ShaderInfo::stages
    FigurFarbe farbe = FigurFarbe::Weiss;
    float rgb[3]{1.0F, 1.0F, 1.0F};   // Fest
    Wave rgbWelle;                    // Welle
    FigurAlpha alpha = FigurAlpha::Eins;
    float a = 1.0F;                   // Fest
    Wave alphaWelle;                  // Welle
    bool umgebung = false;            // tcGen environment
    bool glow = false;
    // rc603 Durchsicht: das Bild ist tr.whiteImage (8x8 weiss, tr_image.cpp:1276
    // MD / 1328 OJ) - "map $whiteimage" und an Figuren auch "map $lightmap"
    // (siehe figurStufen).
    bool weiss = false;
};

// Die Stufen, die an einer Figur gezeichnet werden, in Reihenfolge.
//
// rc603 Durchsicht: auch "map $whiteimage" und "map $lightmap". ParseStage
// (tr_shader.cpp:1202-1220 MD, 1239-1250 OJ) bindet fuer $whiteimage
// tr.whiteImage und fuer $lightmap bei lightmapIndex < 0 ebenfalls
// tr.whiteImage - Figurenshader laden mit lightmapsNone (R_FindShader in
// tr_skin.cpp:337 / tr_ghoul2.cpp:3673 MD, tr_skin.cpp:327 / tr_ghoul2.cpp:3751
// OJ). Die Stufe wird wie jede andere gezeichnet
// (RB_IterateStagesGeneric). Bis rc603 fiel sie weg: das Visier der
// Klonkrieger (weiss mal rgbGen const 0 0 0, darauf der Glanz) war statt
// schwarz durchsichtig.
[[nodiscard]] std::vector<FigurStufenArt> figurStufen(const ShaderInfo& s);

// rc604: ein Bild fehlt. ParseStage bricht beim ersten Bild ab, das
// R_FindImageFile nicht findet (map/clampmap/animMap, tr_shader.cpp:1265-1269,
// 1290-1294, 1331-1335 OJ), ParseShader gibt qfalse (:2205-2209), R_FindShader
// setzt defaultShader und baut den Shader mit den Stufen DAVOR (:3489-3493,
// FinishShader); ohne Stufe ist er ein reiner Nebelshader (numUnfoggedPasses
// 0, SS_FOG, :3265-3270) und zeichnet nichts. Ueber die Haut (.skin,
// tr_skin.cpp:327) gilt genau dieser Shader; ohne Haut nimmt R_LoadMDXM einen
// defaultShader nicht (tr_ghoul2.cpp:3760-3763) - die Flaeche behaelt
// shaderIndex 0, tr.defaultShader mit dem Kasten "*default".
struct FigurShaderErsatz {
    std::size_t stufen = 0;    // so viele Stufen des Skripts bleiben
    bool ersatzbild = false;   // tr.defaultShader (figurErsatzbild) statt des Skripts
    bool unsichtbar = false;   // keine Stufe: nichts gezeichnet
};
[[nodiscard]] FigurShaderErsatz figurShaderMitFehlendemBild(std::size_t ersteFehlende, std::size_t anzahl,
                                                            bool ausHaut) noexcept;
// R_CreateDefaultImage (tr_image.cpp:1282-1310 OJ): 16 x 16 RGBA, alle Bytes
// 32, der Rand 255 - "a box, to allow you to see the mapping coordinates".
[[nodiscard]] std::vector<unsigned char> figurErsatzbild();

// RB_CalcSpecularAlpha (tr_shade_calc.cpp:994 MD) fuer EINE Ecke, 0..1:
// reflected = n * 2 (n.l) - l; a = (reflected . zumAuge / |zumAuge|)^4,
// unter 0 -> 0, ueber 1 -> 1 (b = l * 255, auf 255 begrenzt).
[[nodiscard]] float figurSpekularAlpha(const float n[3], const float licht[3], const float zumAuge[3]);

// RB_CalcEnvironmentTexCoords (tr_shade_calc.cpp:868 MD, "the normal way")
// in den Koordinaten der Entity: viewer = normiert(Auge - Ecke),
// d = n . viewer, st = (n.x d - 0.5 viewer.x, n.y d - 0.5 viewer.y).
void figurUmgebungsUV(const float nLokal[3], const float zumAugeLokal[3], float uv[2]);

// EvalWaveForm (tr_shade_calc.cpp) fuer rgbGen/alphaGen wave, auf 0..1
// begrenzt (RB_CalcWaveColor/RB_CalcWaveAlpha klemmen).
[[nodiscard]] float figurWelle(const Wave& w, float zeitSekunden);

}  // namespace bhed
