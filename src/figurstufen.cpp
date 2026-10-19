// figurstufen.cpp - siehe bhed/figurstufen.h (rc602).
#include "bhed/figurstufen.h"

#include "bhed/saberspuren.h"   // spurRsqrt = Q_rsqrt (rc603 Durchsicht)
#include "bhed/wave.h"

#include <algorithm>
#include <cmath>

namespace bhed {

std::vector<FigurStufenArt> figurStufen(const ShaderInfo& s) {
    std::vector<FigurStufenArt> aus;
    for (std::size_t i = 0; i < s.stages.size(); ++i) {
        const ShaderInfo::Stage& st = s.stages[i];
        // rc603 Durchsicht: $lightmap und $whiteimage zeichnen tr.whiteImage
        // (siehe bhed/figurstufen.h).
        const bool weiss = st.lightmap || st.weiss;
        if (!weiss && st.image.empty() && st.animImages.empty()) {
            continue;
        }
        FigurStufenArt a;
        a.stufe = i;
        a.weiss = weiss;
        a.glow = st.glow;
        a.umgebung = st.texGen == TexGen::Environment;
        // --- rgbGen (ComputeColors, tr_shade.cpp:1403 MD) -----------------
        switch (st.rgbArt) {
            case RgbGenArt::LightingDiffuse:
            case RgbGenArt::LightingDiffuseEntity:
                a.farbe = FigurFarbe::Licht;
                break;
            case RgbGenArt::Const:
                a.farbe = FigurFarbe::Fest;
                for (int k = 0; k < 3; ++k) { a.rgb[k] = std::clamp(st.rgbConst[k], 0.0F, 1.0F); }
                break;
            case RgbGenArt::Wave:
                a.farbe = FigurFarbe::Welle;
                a.rgbWelle = st.rgbWave;
                break;
            case RgbGenArt::OneMinusEntity:
            case RgbGenArt::OneMinusVertex:
                a.farbe = FigurFarbe::Schwarz;
                break;
            default:
                // Keins: identity bzw. identityLighting (ParseStage:1713) -
                // beide am Schirm die Bildfarbe selbst (siehe lichtFuer,
                // gpustate.h). Entity: shaderRGBA der Figur ist weiss.
                // Vertex/ExactVertex: RB_SurfaceGhoul schreibt keine
                // Eckenfarben - als weiss angenommen.
                a.farbe = FigurFarbe::Weiss;
                break;
        }
        // --- alphaGen ------------------------------------------------------
        if (st.alphaWave.active()) {
            a.alpha = FigurAlpha::Welle;
            a.alphaWelle = st.alphaWave;
        } else if (st.alphaGen == AlphaGen::LightingSpecular) {
            a.alpha = FigurAlpha::Spekular;
        } else if (st.alphaGen == AlphaGen::Const && st.alphaConst >= 0.0F) {
            a.alpha = FigurAlpha::Fest;
            a.a = std::clamp(st.alphaConst, 0.0F, 1.0F);
        } else if (st.alphaGen == AlphaGen::OneMinusVertex) {
            a.alpha = FigurAlpha::Fest;   // 255 - 255
            a.a = 0.0F;
        } else {
            a.alpha = FigurAlpha::Eins;
        }
        aus.push_back(a);
    }
    return aus;
}

float figurSpekularAlpha(const float n[3], const float licht[3], const float zumAuge[3]) {
    const float d = 2.0F * (n[0] * licht[0] + n[1] * licht[1] + n[2] * licht[2]);
    const float r[3] = {n[0] * d - licht[0], n[1] * d - licht[1], n[2] * d - licht[2]};
    // rc603 Durchsicht: ilength = Q_rsqrt(DotProduct(viewer, viewer))
    // (tr_shade_calc.cpp:1027 MD / 1082 OJ) - nicht die genaue Wurzel.
    const float ilength = spurRsqrt(zumAuge[0] * zumAuge[0] + zumAuge[1] * zumAuge[1] + zumAuge[2] * zumAuge[2]);
    float l = r[0] * zumAuge[0] + r[1] * zumAuge[1] + r[2] * zumAuge[2];
    l *= ilength;
    if (l < 0.0F) { return 0.0F; }
    l = l * l;
    l = l * l;
    // b = l * 255 (int), > 255 -> 255
    int b = static_cast<int>(l * 255.0F);
    if (b > 255) { b = 255; }
    return static_cast<float>(b) / 255.0F;
}

void figurUmgebungsUV(const float nLokal[3], const float zumAugeLokal[3], float uv[2]) {
    float v[3] = {zumAugeLokal[0], zumAugeLokal[1], zumAugeLokal[2]};
    // rc603 Durchsicht: VectorNormalizeFast (q_math.c:1132 MD / 1147 OJ) -
    // v *= Q_rsqrt(v.v), nicht die genaue Wurzel.
    const float il = spurRsqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    v[0] *= il;
    v[1] *= il;
    v[2] *= il;
    const float d = nLokal[0] * v[0] + nLokal[1] * v[1] + nLokal[2] * v[2];
    // st[0] = normal[0]*d - 0.5*viewer[0]: 0.5 ist ein double-Literal.
    uv[0] = static_cast<float>(static_cast<double>(nLokal[0] * d) - 0.5 * static_cast<double>(v[0]));
    uv[1] = static_cast<float>(static_cast<double>(nLokal[1] * d) - 0.5 * static_cast<double>(v[1]));
}

float figurWelle(const Wave& w, float zeitSekunden) {
    return std::clamp(waveValue(w, zeitSekunden), 0.0F, 1.0F);
}

}  // namespace bhed
