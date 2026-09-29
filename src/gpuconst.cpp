// gpuconst.cpp - siehe bhed/gpuconst.h

#include "bhed/gpuconst.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "bhed/wave.h"

namespace bhed::gpu {


// Der Anteil einer Periode, wie `fraction` in wave.cpp - RB_CalcScroll
// kuerzt so, damit die Koordinaten nicht unbegrenzt wachsen.
float kuerze(float x) { return x - std::floor(x); }

void packeKonstanten(const BatchState& bs, float* ziel,
                     float zeitSekunden) {
    if (ziel == nullptr) {
        return;
    }
    // Erst alles auf Null. Ein nicht beschriebenes Feld enthielte sonst den
    // Wert des vorigen Stapels - ein Fehler, der nur manchmal auftritt und
    // sich deshalb kaum einkreisen laesst.
    std::fill(ziel, ziel + kKonstFloats, 0.0F);

    // --- gTexMod: je Schritt ein Viererblock, Art in .w -------------------
    // --- Die tcMod-Kette zu EINER Abbildung zusammenrechnen --------------
    //
    // Nach `ComputeTexMods` aus rd-rend2 (tr_shade.cpp:177). Jeder Schritt
    // liefert eine affine 2x3-Abbildung
    //
    //     | m0  m2  m4 |        neu.x = st.x*m0 + st.y*m2 + m4
    //     | m1  m3  m5 |        neu.y = st.x*m1 + st.y*m3 + m5
    //
    // und die Schritte werden der Reihe nach hintereinandergehaengt. Uebrig
    // bleiben vier Werte fuer die Matrix und zwei fuer die Verschiebung -
    // fuer BELIEBIG VIELE Schritte.
    //
    // `turb` ist der einzige, der nicht in eine Matrix passt: er verschiebt
    // je nach ORT, nicht linear in st. Er steht deshalb daneben, mit
    // Amplitude und Phase, und wird im Shader danach aufgeschlagen - genau
    // wie in rend2.
    //
    // Damit erledigt sich `tcMod transform`: es IST eine 2x3-Abbildung und
    // passt hinein wie jeder andere Schritt. Bis rc442 stand es auf null.
    float mat[4] = {1.0F, 0.0F, 0.0F, 1.0F};   // m0, m1, m2, m3
    float off[2] = {0.0F, 0.0F};               // m4, m5
    float turbAmp = 0.0F;
    float turbPhase = 0.0F;

    const int n = std::min(bs.numTexMods, kMaxTexMods);
    for (int i = 0; i < n; ++i) {
        const TexMod& m = bs.texMods[i];
        // Der Schritt als 2x3, in derselben Anordnung wie rend2:
        //   s[0] s[2] s[4]
        //   s[1] s[3] s[5]
        float s6[6] = {1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F};
        switch (m.kind) {
            case TexModKind::Turb:
                // RB_CalcTurbulentFactors: now = phase + zeit * frequenz,
                // amplitude unveraendert. `base` geht NICHT ein.
                turbAmp = m.wave.amplitude;
                turbPhase = m.wave.phase + zeitSekunden * m.wave.frequency;
                continue;   // keine Matrix - nicht anhaengen
            case TexModKind::Scroll: {
                // RB_CalcScrollTexMatrix kuerzt auf [0,1), damit die
                // Koordinaten nicht unbegrenzt wachsen.
                const float sx = kuerze(m.a[0] * zeitSekunden);
                const float sy = kuerze(m.a[1] * zeitSekunden);
                s6[4] = sx;
                s6[5] = sy;
                break;
            }
            case TexModKind::Scale:
                s6[0] = m.a[0];
                s6[3] = m.a[1];
                break;
            case TexModKind::Stretch: {
                // RB_CalcStretchTexMatrix: p = 1 / wellenwert, und die
                // Skalierung geht um die Mitte (0,5) statt um den Ursprung.
                const float w = waveValue(m.wave, zeitSekunden);
                const float p = (w != 0.0F) ? (1.0F / w) : 1.0F;
                s6[0] = p;
                s6[3] = p;
                s6[4] = 0.5F - 0.5F * p;
                s6[5] = 0.5F - 0.5F * p;
                break;
            }
            case TexModKind::Rotate: {
                // RB_CalcRotateTexMatrix: NEGATIVE Grad je Sekunde, und die
                // Drehung geht um die Mitte.
                const float grad = -m.a[0] * zeitSekunden;
                const float bog = grad * 0.01745329251F;
                const float c = std::cos(bog);
                const float si = std::sin(bog);
                s6[0] = c;
                s6[1] = si;
                s6[2] = -si;
                s6[3] = c;
                s6[4] = 0.5F - 0.5F * c + 0.5F * si;
                s6[5] = 0.5F - 0.5F * si - 0.5F * c;
                break;
            }
            case TexModKind::Transform:
                // RB_CalcTransformTexMatrix. Die sechs Werte stehen in a[],
                // Matrix in a[0..3], Verschiebung in a[4], a[5].
                s6[0] = m.a[0];
                s6[1] = m.a[1];
                s6[2] = m.a[2];
                s6[3] = m.a[3];
                s6[4] = m.a[4];
                s6[5] = m.a[5];
                break;
            default:
                continue;
        }
        // Anhaengen, genau in der Reihenfolge von rend2.
        const float n0 = s6[0] * mat[0] + s6[2] * mat[1];
        const float n1 = s6[1] * mat[0] + s6[3] * mat[1];
        const float n2 = s6[0] * mat[2] + s6[2] * mat[3];
        const float n3 = s6[1] * mat[2] + s6[3] * mat[3];
        const float o0 = s6[0] * off[0] + s6[2] * off[1] + s6[4];
        const float o1 = s6[1] * off[0] + s6[3] * off[1] + s6[5];
        mat[0] = n0;
        mat[1] = n1;
        mat[2] = n2;
        mat[3] = n3;
        off[0] = o0;
        off[1] = o1;
    }
    ziel[kTexMatrixAt + 0] = mat[0];
    ziel[kTexMatrixAt + 1] = mat[1];
    ziel[kTexMatrixAt + 2] = mat[2];
    ziel[kTexMatrixAt + 3] = mat[3];
    ziel[kTexOffTurbAt + 0] = off[0];
    ziel[kTexOffTurbAt + 1] = off[1];
    ziel[kTexOffTurbAt + 2] = turbAmp;
    ziel[kTexOffTurbAt + 3] = turbPhase;

    // --- gRgb: Farbe und alphaConst ---------------------------------------
    //
    // --- rgbGen const und rgbGen wave ------------------------------------
    //
    // Beide ERSETZEN die Eckenfarbe. Hier stand "const setzt die Farbe,
    // wave multipliziert sie" - das war falsch, und der Shader machte
    // daraus zwei verschiedene Zeilen (`=` und `*=`).
    //
    // Der Rasterer schreibt es ausdruecklich hin (mapview.cpp:1818):
    // "rgbGen wave ERSETZT die Vertexfarbe, sie multipliziert sie nicht.
    // RB_CalcWaveColor() schreibt den Wert direkt in alle drei Kanaele."
    //
    // Was das ausmacht, an der Lava von md_am_sith gemessen: die Ecken dort
    // haben im Mittel 103 von 255, also 0,40. Mit `rgbGen wave sin 0.8 ...`
    // rechnete der GPU-Weg 0,40 x 0,8 = 0,32, der Rasterer 0,8. Faktor
    // zweieinhalb - genau die zu dunkle Lava.
    //
    // Und WAVE GEHT VOR: steht beides in einer Stufe, gewinnt die Welle
    // (mapview.cpp:1833). Hier stand es andersherum.
    if (bs.haveRgbWave) {
        ziel[kRgbAt + 0] = bs.rgbGlow;
        ziel[kRgbAt + 1] = bs.rgbGlow;
        ziel[kRgbAt + 2] = bs.rgbGlow;
    } else if (bs.haveRgbConst) {
        ziel[kRgbAt + 0] = bs.rgbConst[0];
        ziel[kRgbAt + 1] = bs.rgbConst[1];
        ziel[kRgbAt + 2] = bs.rgbConst[2];
    } else {
        ziel[kRgbAt + 0] = 1.0F;
        ziel[kRgbAt + 1] = 1.0F;
        ziel[kRgbAt + 2] = 1.0F;
    }
    // alphaConst: negativ heisst "nicht gesetzt", dann volle Deckung.
    ziel[kRgbAt + 3] = (bs.alphaConst >= 0.0F) ? bs.alphaConst : 1.0F;

    // --- Die Schwelle fuer alphaFunc --------------------------------------
    //
    // Sie kommt aus der Pipeline, nicht aus dem Zustand - dort ist sie schon
    // auf 0..1 gebracht. Hier wird sie aus demselben Weg geholt, damit es
    // nicht zwei Umrechnungen gibt.
    ziel[kAlphaAt] = pipelineFor(bs, false).alphaSchwelle;

    // --- deformVertexes ---------------------------------------------------
    //
    // Vier Werte in EINEM float4 (gDeform), der Spread gleich dahinter.
    // Frueher ein `float gDeform[4]` mit Schritt vier - und der Spread lag
    // dabei nicht, wo er hier hingeschrieben wurde (siehe gpuconst.h).
    if (bs.deformWave.active()) {
        ziel[kDeformAt + 0] = bs.deformWave.base;
        ziel[kDeformAt + 1] = bs.deformWave.amplitude;
        ziel[kDeformAt + 2] = bs.deformWave.phase;
        ziel[kDeformAt + 3] = bs.deformWave.frequency;
        ziel[kDeformSpreadAt] = bs.deformSpread;
    }
}

}  // namespace bhed::gpu
