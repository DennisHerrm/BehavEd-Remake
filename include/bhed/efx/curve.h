// Kurvenauswertung: wie sich ein Wert ueber die Lebensdauer veraendert.
//
// Das ist das Stueck, auf dem die ganze Vorschau steht - Groesse, Farbe, Alpha,
// Laenge laufen alle darueber. Die Formeln stehen Zeile fuer Zeile in
// `code/cgame/FxPrimitives.cpp` (`CParticle::UpdateSize` und die
// gleichgebauten `UpdateRGB`, `UpdateAlpha`) und in `FxUtil.cpp`, wo der
// Parameter vor dem Abspielen umgerechnet wird.
//
// Zwei Dinge daran sind ueberraschend genug, dass man sie kennen muss:
//
// **Ohne Flag bleibt der Wert auf `start`.** `perc1` beginnt bei 1.0 und wird
// nur veraendert, wenn ein Flag gesetzt ist. Am Ende steht
// `start*perc1 + end*(1-perc1)` - ohne Flag also durchgehend `start`. Wer
// `end` setzt und sich wundert, dass nichts passiert, hat `linear` vergessen.
//
// **`parm` ist keine Zahl zwischen 0 und 1.** Vor dem Abspielen wird sie
// umgerechnet, und zwar je nach Kurvenart verschieden:
//
//     wave:              parm * PI * 0.001          (eine Frequenz)
//     nonlinear, clamp:  parm * 0.01 * life + jetzt (ein Zeitpunkt)
//
// Bei `wave` ist das Ergebnis eine Kreisfrequenz, bei den anderen beiden ein
// absoluter Zeitpunkt in Millisekunden. Dieselbe Zahl in der Datei bedeutet
// also zweierlei.
#pragma once

#include <cstdint>

namespace bhed::efx::curve {

// Die Flags, wie sie in der Datei stehen. Die Bitwerte sind die des
// Groessenkanals; die anderen Kanaele benutzen dieselbe Reihenfolge auf anderen
// Bits, deshalb rechnet der Auswerter mit dieser normierten Form.
enum Flags : uint32_t {
    kLinear    = 1u << 0,
    kNonLinear = 1u << 1,
    kWave      = 1u << 2,
    kClamp     = kNonLinear | kWave,  // dieselben zwei Bits zusammen
    kRandom    = 1u << 3,
};

// --- Aus den Dateibits (effect.h, kCurve*) in diese Form -------------
//
// Die Datei kennt die Bits der Engine (FxPrimitives.h: FX_*_LINEAR,
// _RAND, _NONLINEAR, _WAVE, _CLAMP = NONLINEAR|WAVE, je Kanal verschoben):
// linear 1, random 2, nonlinear 4, wave 8. Der Auswerter hier rechnet mit
// linear 1, nonlinear 2, wave 4, random 8.
//
// Bis rc568 wurden die Bits UNGEWANDELT uebernommen (efxdraw.cpp,
// curveOf). Nur `linear` stimmte: `nonlinear` lief als Welle, `clamp` als
// Welle mit Zufall. Die Lava-Geysire in md_am_sith (`length ... flags
// linear clamp`) schwangen dadurch, statt schnell auszuwachsen und zu
// halten, und jedes `alpha ... flags linear nonlinear` flackerte.
inline uint32_t ausDatei(int dateiFlags) {
    uint32_t f = 0;
    if ((dateiFlags & 0x1) != 0) { f |= kLinear; }
    if ((dateiFlags & 0x2) != 0) { f |= kRandom; }
    if ((dateiFlags & 0x4) != 0) { f |= kNonLinear; }
    if ((dateiFlags & 0x8) != 0) { f |= kWave; }
    return f;
}

// Ein Wert ueber die Zeit.
struct Curve {
    float start = 0.0f;
    float end = 0.0f;
    float parm = 0.0f;   // wie in der Datei, noch nicht umgerechnet
    uint32_t flags = 0;
};

// Rechnet `parm` in das um, womit die Engine arbeitet.
//
// startMs ist der Zeitpunkt, zu dem die Primitive erscheint, lifeMs ihre
// Lebensdauer. Bei `wave` gehen beide nicht ein.
float resolveParm(const Curve& curve, float startMs, float lifeMs);

// Der Anteil, mit dem `start` gewichtet wird: 1 heisst ganz `start`, 0 heisst
// ganz `end`. `resolvedParm` kommt aus resolveParm.
//
// randomValue ist die Zufallszahl fuer `random` - hereingereicht statt intern
// gezogen, damit sich das Ergebnis pruefen laesst.
float bias(const Curve& curve, float nowMs, float startMs, float endMs,
           float resolvedParm, float randomValue = 1.0f);

// Der Wert selbst.
float evaluate(const Curve& curve, float nowMs, float startMs, float endMs,
               float resolvedParm, float randomValue = 1.0f);

}  // namespace bhed::efx::curve
