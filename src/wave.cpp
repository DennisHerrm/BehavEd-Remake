// wave.cpp - siehe bhed/wave.h
#include "bhed/wave.h"

#include <cmath>

namespace bhed {
namespace {

constexpr float kPi = 3.14159265358979F;

// Der Anteil einer Periode, immer in [0, 1).
float fraction(float x) noexcept { return x - std::floor(x); }

// Die fuenf Formen aus TableForFunc(). Die Engine legt sie als Tabellen
// mit 1024 Eintraegen an (tr_init.cpp, R_InitFuncTable); die Formeln
// darunter sind dieselben.
float shape(WaveFunc f, float t) noexcept {
    switch (f) {
        case WaveFunc::Sin:
            return std::sin(t * 2.0F * kPi);
        case WaveFunc::Triangle:
            // Steigt auf der ersten Haelfte von 0 auf 1, faellt auf der
            // zweiten zurueck.
            return (t < 0.5F) ? (t * 2.0F) : (2.0F - t * 2.0F);
        case WaveFunc::Square:
            return (t < 0.5F) ? 1.0F : -1.0F;
        case WaveFunc::Sawtooth:
            return t;
        case WaveFunc::InverseSawtooth:
            return 1.0F - t;
        case WaveFunc::Noise:
        case WaveFunc::None:
        default:
            return 0.0F;
    }
}

}  // namespace

WaveFunc waveFuncFromName(std::string_view name) noexcept {
    if (name == "sin") { return WaveFunc::Sin; }
    if (name == "triangle") { return WaveFunc::Triangle; }
    if (name == "square") { return WaveFunc::Square; }
    if (name == "sawtooth") { return WaveFunc::Sawtooth; }
    if (name == "inversesawtooth") { return WaveFunc::InverseSawtooth; }
    if (name == "noise") { return WaveFunc::Noise; }
    return WaveFunc::None;
}

float waveValue(const Wave& w, float timeSeconds) noexcept {
    if (w.func == WaveFunc::None) {
        return 0.0F;
    }
    if (w.func == WaveFunc::Noise) {
        // Die Mitte statt eines Wuerfels - warum, steht in wave.h.
        return w.base + w.amplitude * 0.5F;
    }
    // WAVEVALUE aus tr_shade_calc.cpp, Zeile 30.
    const float t = fraction(w.phase + timeSeconds * w.frequency);
    return w.base + shape(w.func, t) * w.amplitude;
}

}  // namespace bhed
