#include "bhed/efx/curve.h"

#include <numbers>

#include <cmath>

namespace bhed::efx::curve {
namespace {
// std::numbers::pi_v<float> statt der getippten Ziffernfolge.
//
// clang-tidy meldete den Unterschied: 8,74e-08. Der spielt hier keine
// Rolle, aber eine Zahl, die schon einen Namen hat, sollte man nicht
// abtippen - beim Abtippen verrutscht irgendwann eine Ziffer.
constexpr float kPi = std::numbers::pi_v<float>;
}

float resolveParm(const Curve& curve, float startMs, float lifeMs) {
    const uint32_t kind = curve.flags & kClamp;
    if (kind == kWave) {
        // FxUtil.cpp: sizeParm * PI * 0.001f - eine Kreisfrequenz.
        return curve.parm * kPi * 0.001F;
    }
    if (kind != 0) {
        // nonlinear und clamp: sizeParm * 0.01f * killTime + jetzt.
        // Das Ergebnis ist ein absoluter Zeitpunkt, kein Anteil.
        return curve.parm * 0.01F * lifeMs + startMs;
    }
    return 0.0F;
}

float bias(const Curve& curve, float nowMs, float startMs, float endMs,
           float resolvedParm, float randomValue) {
    // Voll auf start, solange nichts anderes gesagt wird. Genau so steht es im
    // Spielcode: "completely biased towards start if it doesn't get overridden".
    float perc1 = 1.0F;
    float perc2 = 1.0F;

    const float span = endMs - startMs;
    if ((curve.flags & kLinear) != 0U) {
        // Laeuft von 1 auf 0 ueber die Lebensdauer.
        if (span > 0.0F) { perc1 = 1.0F - (nowMs - startMs) / span;
}
    }

    // linear laesst sich mit genau einem der drei anderen kombinieren - sie
    // teilen sich zwei Bits und schliessen sich gegenseitig aus.
    const uint32_t kind = curve.flags & kClamp;
    if (kind == kNonLinear) {
        if (nowMs > resolvedParm) {
            const float rest = endMs - resolvedParm;
            if (rest > 0.0F) { perc2 = 1.0F - (nowMs - resolvedParm) / rest;
}
        }
        // Mit linear zusammen: gleichmaessig gemischt, sonst uebernommen.
        perc1 = ((curve.flags & kLinear) != 0U) ? perc1 * 0.5F + perc2 * 0.5F : perc2;
    } else if (kind == kWave) {
        // Der Parameter ist hier eine Frequenz, kein Zeitpunkt.
        perc1 = perc1 * std::cos((nowMs - startMs) * resolvedParm);
    } else if (kind == kClamp) {
        if (nowMs < resolvedParm) {
            const float rise = resolvedParm - startMs;
            if (rise > 0.0F) { perc2 = (resolvedParm - nowMs) / rise;
}
        } else {
            perc2 = 0.0F;
        }
        perc1 = ((curve.flags & kLinear) != 0U) ? perc1 * 0.5F + perc2 * 0.5F : perc2;
    }

    if ((curve.flags & kRandom) != 0U) {
        // "Random simply modulates the existing value" - es ersetzt den Anteil
        // nicht, es daempft ihn. Ravens Handbuch beschreibt es falsch als
        // "beginnt bei null"; der Quelltext sagt etwas anderes.
        perc1 = randomValue * perc1;
    }
    return perc1;
}

float evaluate(const Curve& curve, float nowMs, float startMs, float endMs,
               float resolvedParm, float randomValue) {
    const float perc = bias(curve, nowMs, startMs, endMs, resolvedParm, randomValue);
    // perc == 1 heisst ganz start, perc == 0 ganz end.
    return curve.start * perc + curve.end * (1.0F - perc);
}

}  // namespace bhed::efx::curve
