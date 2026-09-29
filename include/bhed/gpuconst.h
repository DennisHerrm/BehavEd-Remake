// gpuconst.h - die Konstanten je Stapel, so gepackt wie HLSL sie liest
//
// Warum das eine eigene Datei ist
// -------------------------------
// HLSL packt `cbuffer` nach festen Regeln: alles wird in Bloecke zu vier
// float einsortiert, und ein Wert darf keinen Blockrand ueberschreiten. Wer
// die Struktur auf der C++-Seite "so aehnlich" hinschreibt, bekommt eine
// Verschiebung - und die sieht man nicht als Absturz, sondern als Bild, in
// dem eine Textur langsam wandert, weil sie ihre Werte aus dem falschen Feld
// liest.
//
// Diese Klasse von Fehlern ist besonders unangenehm, weil sie erst auf dem
// Rechner mit Grafikkarte auftritt und dort schwer einzukreisen ist. Also
// wird sie hier gepackt, wo man sie pruefen kann (tests/gpuconst.cpp), und
// der Direct3D-Teil kopiert nur noch einen fertigen Block.
//
// Der Aufbau muss zu `kKonstanten` in src/gpushader.cpp passen:
//
//     cbuffer JeStapel : register(b1) {
//         float4 gTexMatrix;
//         float4 gTexOffTurb;
//         float4 gRgb;
//         float  gAlphaSchwelle;
//         float  gDeform[4];
//         float  gDeformSpread;
//     };
#ifndef BHED_GPUCONST_H
#define BHED_GPUCONST_H

#include <cstddef>

#include "bhed/gpustate.h"
#include "bhed/mapview.h"

namespace bhed::gpu {

// Die Groesse des Blocks in float.
//
//   gTexMatrix           =  4
//   gTexOffTurb          =  4
//   gRgb                 =  4
//   gAlphaSchwelle       =  1  -> beginnt einen neuen Viererblock
//   gDeform              =  4  -> float4, beginnt auf der Blockgrenze 16
//   gDeformSpread        =  1  -> gleich dahinter, 20
//
// Bis rc567 stand dort `float gDeform[4]`, und hier war mit vier
// Viererbloecken gerechnet (Spread bei 32). Das stimmte nur halb: das
// LETZTE Element eines float-Feldes belegt nur .x seines Blocks, und das
// naechste Skalar packt HLSL in .y dahinter - gDeformSpread lag bei 29.
// Nachgemessen mit D3DReflect (gpumap_win32.cpp, pruefeStapelLagen). Seitdem
// ist es ein float4: keine Packregel mehr, die man falsch erraten kann.
//
// --- Warum aus 32 float acht wurden ----------------------------------
//
// Bis rc442 stand hier `float4 gTexMod[8]`: EIN Viererblock je Schritt der
// tcMod-Kette, die Art in .w, und der Shader lief die Kette zur Laufzeit ab.
// Das kostete 32 der 60 float und hatte einen Schritt, der nicht hineinpasste:
// `tcMod transform` braucht sechs Werte und stand deshalb auf null (13
// Vorkommen in 43 Karten).
//
// rd-rend2 macht es anders (`ComputeTexMods`, tr_shade.cpp:177): ALLE
// Schritte werden auf der CPU zu EINER affinen Abbildung zusammengerechnet -
// eine 2x2-Matrix und eine Verschiebung. Uebrig bleiben:
//
//   gTexMatrix   float4   die 2x2-Matrix (m00, m10, m01, m11)
//   gTexOffTurb  float4   Verschiebung x, y  +  Amplitude, Phase von turb
//
// Acht float statt zweiunddreissig, fuer BELIEBIG VIELE Schritte - und
// `transform` passt hinein wie jeder andere Schritt auch.
inline constexpr std::size_t kTexMatrixAt = 0U;
inline constexpr std::size_t kTexOffTurbAt = 4U;
inline constexpr std::size_t kRgbAt = 8U;
inline constexpr std::size_t kAlphaAt = kRgbAt + 4U;          // 12
// Ein float4 darf keine Blockgrenze ueberschreiten: nach gAlphaSchwelle
// (12) waere 13 frei, HLSL schiebt auf 16.
inline constexpr std::size_t kDeformAt = 16U;
inline constexpr std::size_t kDeformSpreadAt = kDeformAt + 4U;  // 20
// Die Stellung eines Movers steht NICHT hier: sie wird vor dem Zeichnen in
// die Kameramatrix multipliziert (siehe gpushader.cpp). Eine eigene Matrix
// im Stapelpuffer hatte 16 Felder gekostet und ein Bild erzeugt, in dem alle
// Ecken in einen Punkt liefen.
inline constexpr std::size_t kKonstFloats = kDeformSpreadAt + 4U;  // 24

// Die Art eines tcMod-Schritts stand hier als Aufzaehlung, weil der Shader
// sie in .w des jeweiligen Viererblocks las. Seit rc443 wird die Kette auf
// der CPU zusammengerechnet - der Shader kennt keine Arten mehr, und die
// Aufzaehlung mit ihr ist weg. Die Zuordnung steht jetzt allein im
// `switch` in packeKonstanten, an einer Stelle statt an dreien.

// Fuellt `ziel` (mindestens kKonstFloats Werte) aus dem Zustand.
//
// Alles, was nicht gesetzt ist, wird auf Null geschrieben - nicht
// uebersprungen. Ein nicht beschriebenes Feld enthaelt sonst den Wert des
// vorigen Stapels, und das ist ein Fehler, der nur manchmal auftritt.
// `zeitSekunden` ist die Zeit DIESES Stapels (timeSeconds - Batch::shaderTime).
//
// Seit rc443 wird die tcMod-Kette hier auf der CPU ausgerechnet statt im
// Shader abgelaufen - scroll, rotate und stretch haengen von der Zeit ab,
// also braucht sie der Packer.
void packeKonstanten(const BatchState& bs, float* ziel, float zeitSekunden);

}  // namespace bhed::gpu

#endif  // BHED_GPUCONST_H
