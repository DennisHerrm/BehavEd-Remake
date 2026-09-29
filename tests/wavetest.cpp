// wavetest.cpp - die Wellenformen der Shader.
//
// Geprueft gegen tr_shade_calc.cpp, Zeile 30:
//
//     WAVEVALUE( table, base, amplitude, phase, freq )
//       = base + table[ (phase + zeit * freq) * FUNCTABLE_SIZE & MASK ]
//               * amplitude
//
// Also base + f(Nachkommateil von (phase + zeit*freq)) * amplitude.
#include "bhed/wave.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_fails = 0;

void expect(const char* what, bool ok) {
    if (!ok) {
        ++g_fails;
    }
    std::printf("  %-4s %s\n", ok ? "ok" : "FEHL", what);
}

bool near(float a, float b, float eps = 0.002F) {
    return std::fabs(a - b) < eps;
}

bhed::Wave mk(bhed::WaveFunc f, float base, float amp, float phase,
              float freq) {
    bhed::Wave w;
    w.func = f;
    w.base = base;
    w.amplitude = amp;
    w.phase = phase;
    w.frequency = freq;
    return w;
}

}  // namespace

int main() {
    using bhed::WaveFunc;
    using bhed::waveValue;

    // --- Die Namen -------------------------------------------------------
    expect("sin", bhed::waveFuncFromName("sin") == WaveFunc::Sin);
    expect("triangle", bhed::waveFuncFromName("triangle") == WaveFunc::Triangle);
    expect("square", bhed::waveFuncFromName("square") == WaveFunc::Square);
    expect("sawtooth", bhed::waveFuncFromName("sawtooth") == WaveFunc::Sawtooth);
    expect("inversesawtooth",
           bhed::waveFuncFromName("inversesawtooth") == WaveFunc::InverseSawtooth);
    expect("ein unbekannter Name ergibt None - die Engine warnt nur",
           bhed::waveFuncFromName("gibtsnicht") == WaveFunc::None);

    // --- sin --------------------------------------------------------------
    //
    // base 0, amplitude 1, phase 0, freq 1: bei t = 0 ist der Sinus 0, bei
    // t = 0,25 ist er 1, bei t = 0,75 ist er -1.
    {
        const bhed::Wave w = mk(WaveFunc::Sin, 0.0F, 1.0F, 0.0F, 1.0F);
        expect("sin bei t=0 ist 0", near(waveValue(w, 0.0F), 0.0F));
        expect("sin bei t=0.25 ist 1", near(waveValue(w, 0.25F), 1.0F));
        expect("sin bei t=0.75 ist -1", near(waveValue(w, 0.75F), -1.0F));
        // Nach einer ganzen Periode wieder dasselbe.
        expect("sin ist periodisch",
               near(waveValue(w, 0.25F), waveValue(w, 5.25F)));
    }

    // --- base und amplitude ----------------------------------------------
    //
    // Die Lava von Mustafar schreibt "rgbGen wave sin 0.8 0.1 0 0.3": sie
    // pendelt also zwischen 0,7 und 0,9.
    {
        const bhed::Wave w = mk(WaveFunc::Sin, 0.8F, 0.1F, 0.0F, 0.3F);
        float lo = 9.0F;
        float hi = -9.0F;
        for (int k = 0; k < 400; ++k) {
            const float v = waveValue(w, static_cast<float>(k) * 0.01F);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        std::printf("     Lava: pendelt zwischen %.3f und %.3f\n",
                    static_cast<double>(lo), static_cast<double>(hi));
        expect("base und amplitude ergeben 0.7 bis 0.9",
               near(lo, 0.7F, 0.01F) && near(hi, 0.9F, 0.01F));
    }

    // --- die uebrigen Formen ---------------------------------------------
    {
        const bhed::Wave sq = mk(WaveFunc::Square, 0.0F, 1.0F, 0.0F, 1.0F);
        expect("square ist auf der ersten Haelfte 1",
               near(waveValue(sq, 0.1F), 1.0F));
        expect("und auf der zweiten -1", near(waveValue(sq, 0.6F), -1.0F));

        const bhed::Wave sa = mk(WaveFunc::Sawtooth, 0.0F, 1.0F, 0.0F, 1.0F);
        expect("sawtooth steigt von 0 auf 1",
               near(waveValue(sa, 0.0F), 0.0F) &&
                   near(waveValue(sa, 0.9F), 0.9F));

        const bhed::Wave is = mk(WaveFunc::InverseSawtooth, 0.0F, 1.0F, 0.0F,
                                 1.0F);
        expect("inversesawtooth faellt von 1 auf 0",
               near(waveValue(is, 0.0F), 1.0F) &&
                   near(waveValue(is, 0.9F), 0.1F));

        const bhed::Wave tr = mk(WaveFunc::Triangle, 0.0F, 1.0F, 0.0F, 1.0F);
        expect("triangle ist in der Mitte am hoechsten",
               near(waveValue(tr, 0.5F), 1.0F) &&
                   near(waveValue(tr, 0.0F), 0.0F) &&
                   near(waveValue(tr, 0.25F), 0.5F));
    }

    // --- phase und frequency ---------------------------------------------
    {
        // Eine Phase von 0.25 verschiebt den Sinus um eine Viertelperiode.
        const bhed::Wave ohne = mk(WaveFunc::Sin, 0.0F, 1.0F, 0.0F, 1.0F);
        const bhed::Wave mit = mk(WaveFunc::Sin, 0.0F, 1.0F, 0.25F, 1.0F);
        expect("phase verschiebt die Welle",
               near(waveValue(mit, 0.0F), waveValue(ohne, 0.25F)));
        // Doppelte Frequenz heisst halbe Zeit fuer dieselbe Stelle.
        const bhed::Wave schnell = mk(WaveFunc::Sin, 0.0F, 1.0F, 0.0F, 2.0F);
        expect("frequency staucht die Zeit",
               near(waveValue(schnell, 0.125F), waveValue(ohne, 0.25F)));
    }

    // --- Sonderfaelle ----------------------------------------------------
    {
        bhed::Wave keine;
        expect("ohne Form kommt null heraus", waveValue(keine, 3.0F) == 0.0F);
        expect("und active() sagt es", !keine.active());

        // noise wuerde im Spiel wuerfeln. Wir nehmen die Mitte, damit die
        // Vorschau wiederholbar bleibt - dieselbe Wahl wie bei $random$.
        const bhed::Wave n = mk(WaveFunc::Noise, 0.5F, 0.4F, 0.0F, 1.0F);
        expect("noise ergibt die Mitte, und zwar immer dieselbe",
               near(waveValue(n, 0.0F), 0.7F) &&
                   waveValue(n, 0.0F) == waveValue(n, 99.0F));
    }

    std::printf("\n%s (%d Fehlschlaege)\n",
                g_fails == 0 ? "alle Wellenproben bestanden" : "FEHLGESCHLAGEN",
                g_fails);
    return g_fails == 0 ? 0 : 1;
}
