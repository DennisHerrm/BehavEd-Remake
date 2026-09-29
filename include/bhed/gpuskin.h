// gpuskin.h - Figuren fuer die Grafikkarte vorbereiten
//
// Die Aufteilung, wie bei der Karte
// ---------------------------------
// Eine Figur zu zeichnen zerfaellt in zwei Teile:
//
//   Die Ecken und die Knochenmatrizen AUFBEREITEN - reine Datenverarbeitung,
//   und hier geprueft (tests/gpuskin.cpp).
//
//   Sie an die Grafikkarte GEBEN - Direct3D, nicht pruefbar.
//
// Warum das lohnt: der Rasterer rechnet die Verformung je Ecke auf der CPU
// (mapview.cpp:3856) und schickt fertige Weltkoordinaten weiter. Eine
// Grafikkarte will das Gegenteil: unveraenderte Ecken plus eine Liste von
// Matrizen, und sie rechnet selbst.
//
// Die Umrechnung dazwischen ist der Teil, den man falsch machen kann - und
// sie ist reine Arithmetik.
//
// Vier Knochen je Ecke
// --------------------
// GlmVertex traegt vier Knochennummern und vier Gewichte. Das passt genau auf
// das, was Grafikkarten seit zwanzig Jahren koennen, und muss deshalb nicht
// umgebaut werden - nur umsortiert.
#ifndef BHED_GPUSKIN_H
#define BHED_GPUSKIN_H

#include <cstddef>
#include <array>
#include <cstdint>
#include <vector>

#include "bhed/gla.h"
#include "bhed/glm.h"

namespace bhed::gpu {

// Eine Ecke, wie der Shader sie erwartet.
//
// Die Reihenfolge muss zum Eingabelayout im Backend passen. Sie steht
// deshalb hier und nicht dort - `sizeof` wird geprueft, damit ein
// eingeschobenes Feld auffaellt statt verzerrte Figuren zu erzeugen.
struct SkinVertex {
    float pos[3];
    float normal[3];
    float uv[2];
    // Als float und nicht als Byte: ein Byte-Format muesste im Shader erst
    // zurueckgerechnet werden, und die vier Werte kosten je Ecke sechzehn
    // Byte, die bei einer Figur mit 3000 Ecken nicht ins Gewicht fallen.
    float bones[4];
    float weights[4];
};

// Wie viele Knochen die Matrizenliste hoechstens traegt.
//
// Das Skelett von JKA hat 53 Knochen (siehe Protokoll: "_humanoid: 53
// Knochen"). 128 laesst Luft und passt noch bequem in einen
// Konstantenpuffer: 128 * 3 float4 = 6 KiB.
inline constexpr int kMaxKnochen = 128;

// Die Ecken einer Flaeche umsortieren.
//
// `aus` wird angehaengt, nicht geleert - eine Figur hat mehrere Flaechen,
// und sie teilen sich einen Puffer.
void packeEcken(const GlmSurface& sf, std::vector<SkinVertex>& aus);

// Die Knochenmatrizen in die Form bringen, die der Shader liest: je Knochen
// DREI float4 (die vierte Zeile ist immer 0,0,0,1 und wird weggelassen).
//
// `ziel` nimmt kMaxKnochen * 12 float. Knochen ueber die Zahl der
// vorhandenen hinaus werden auf die Einheitsmatrix gesetzt - NICHT auf Null.
// Eine Nullmatrix zieht jede Ecke, die versehentlich darauf zeigt, in den
// Ursprung, und das sieht aus wie ein Dreieck, das quer durch die Karte
// gespannt ist. Die Einheitsmatrix laesst sie stehen, wo sie ist.
void packeKnochen(const std::vector<BoneMatrix>& matrizen, float* ziel);

// Die Knochenmatrizen einer Figur, fertig zum Packen.
//
// `world` sind die Weltmatrizen des Bildes (aus GlaAnimation::worldMatrices),
// `bones` das Skelett. Gerechnet wird genau das, was der Rasterer rechnet
// (mapview.cpp:3895):
//
//     skinM[b] = world[b] * bones[b].basePoseInv
//
// Als eigene Funktion und nicht zweimal hingeschrieben: eine zweite Fassung
// dieser Zeile waere eine zweite Wahrheit darueber, wie eine Figur steht -
// und der Unterschied faellt erst als leicht verbogene Figur auf, was man
// fuer eine schlechte Animation halten koennte.
// Die Stellung einer Figur in der Welt, als 4x4 zeilenweise.
//
// Woertlich wie der Rasterer (mapview.cpp:3924):
//
//     out[0] = local[0]*cs - local[1]*sn + pos[0]
//     out[1] = local[0]*sn + local[1]*cs + pos[1]
//     out[2] = local[2]                  + pos[2]
//
// Der Zuschlag von 90 Grad steckt mit drin (kActorYawFix) - er gehoert zur
// Ausrichtung der Modelle, nicht zur Kamera, und wer ihn vergisst, bekommt
// Figuren, die alle um einen Viertelkreis verdreht stehen.
void baueFigurWelt(float yawGrad, const float pos[3], float* ziel);

// Die Stellung eines Griffs in der Hand: Figurstellung mal Bolzenmatrix.
//
// Der Bolzen "*r_hand" steht im Bezug der Figur, VOR ihrer Drehung und
// Verschiebung - wie im Rasterer (mapview.cpp, legeAb). `figurWelt` kommt
// aus baueFigurWelt, `ziel` wird wieder 4x4 zeilenweise.
void baueGriffWelt(const float* figurWelt, const BoneMatrix& hand, float* ziel);

// Ein Band einer Klinge: vier Weltecken, quer zur Klinge UND quer zum Blick
// (RT_LINE/RT_SABER_GLOW drehen sich zur Kamera, cg_players.cpp:5746).
// Ecke 0 und 3 liegen auf der einen Kante, 1 und 2 auf der anderen.
// `rechts` ist die Kameraachse fuer den Fall, dass die Klinge genau zum
// Auge zeigt.
void baueKlingenband(const float wurzel[3], const float spitze[3],
                     const float auge[3], const float rechts[3], float radius,
                     float ecken[4][3]);

// Die Stellung eines Movers - Tueren, Plattformen, Schiffe.
//
// Woertlich wie placeVert (mapview.cpp:1262): erst Gier um Z, dann - nur
// wenn der Mover taumelt - Nick und Roll, alles um den DREHPUNKT, danach der
// Versatz.
//
// Mover waren beim GPU-Umbau schlicht uebersehen worden: sie stehen in
// renderMap als zusaetzliche Quellen, und nachgebaut hatte ich nur den
// Hauptdurchgang. Gemeldet als "die Tueren verschwinden".
//
// `skala` (optional, je Achse): Kartenmodelle tragen "modelscale" oder
// "modelscale_vec" - die Engine skaliert die drei Achsen der Stellung
// (CG_CreateMiscEntFromGent). nullptr heisst 1 1 1.
void baueMoverWelt(const float pivot[3], const float offset[3], float yawGrad,
                   float pitchGrad, float rollGrad, bool taumelt, float* ziel,
                   const float* skala = nullptr);

void baueKnochenmatrizen(const std::vector<BoneMatrix>& world,
                         const std::vector<GlaBone>& bones,
                         std::vector<BoneMatrix>& aus);

}  // namespace bhed::gpu

#endif  // BHED_GPUSKIN_H
