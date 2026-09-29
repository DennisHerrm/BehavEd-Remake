// wave.h - die Wellenformen der Shader.
//
// Ein JKA-Shader laesst Dinge wogen und pulsen. Beides steht in derselben
// Form da:
//
//     <func> <base> <amplitude> <phase> <frequency>
//
// und beides wird mit derselben Rechnung ausgewertet. Sie steht in
// code/rd-vanilla/tr_shade_calc.cpp, Zeile 30:
//
//     #define WAVEVALUE( table, base, amplitude, phase, freq )
//         ((base) + table[ Q_ftol( ( ( (phase) + floatTime * (freq) )
//             * FUNCTABLE_SIZE ) ) & FUNCTABLE_MASK ] * (amplitude))
//
// Also: base + f(phase + zeit * frequenz) * amplitude, wobei f eine der
// fuenf Formen ueber einer Periode von 1 ist. Die Engine schlaegt in einer
// Tabelle mit 1024 Eintraegen nach; wir rechnen direkt. Das Ergebnis
// unterscheidet sich um die Tabellenaufloesung, und die braucht eine
// Vorschau nicht.
//
// Die Formen selbst sind die von TableForFunc(): sin, triangle, square,
// sawtooth, inversesawtooth - dazu "noise", das die Engine gesondert
// behandelt und wir als konstante Mitte nehmen (siehe unten).
#ifndef BHED_WAVE_H
#define BHED_WAVE_H

#include <cstdint>
#include <string_view>

namespace bhed {

enum class WaveFunc : std::uint8_t {
    None,
    Sin,
    Triangle,
    Square,
    Sawtooth,
    InverseSawtooth,
    Noise,
};

struct Wave {
    WaveFunc func = WaveFunc::None;
    float base = 0.0F;
    float amplitude = 0.0F;
    float phase = 0.0F;
    float frequency = 0.0F;

    [[nodiscard]] bool active() const noexcept {
        return func != WaveFunc::None;
    }
};

// "sin", "triangle", "square", "sawtooth", "inversesawtooth", "noise".
// Alles andere ergibt None - so wie die Engine dann mit einer Warnung
// weitermacht, statt den Shader zu verwerfen.
[[nodiscard]] WaveFunc waveFuncFromName(std::string_view name) noexcept;

// Der Wert der Welle zu einem Zeitpunkt, in Sekunden.
//
// noise wuerde im Spiel wuerfeln. Fuer eine Vorschau nehmen wir die MITTE
// (base + amplitude/2): wiederholbar, und die Zeitleiste sieht nicht bei
// jedem Blick anders aus. Dieselbe Wahl wie bei $random$ in den Skripten,
// beim Kamerawackeln und bei den Partikeln.
[[nodiscard]] float waveValue(const Wave& w, float timeSeconds) noexcept;

}  // namespace bhed
#endif
