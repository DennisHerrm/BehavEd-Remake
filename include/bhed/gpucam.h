// gpucam.h - die Kameramatrix fuer den GPU-Weg
//
// Warum eine eigene Datei
// -----------------------
// Der Rasterer projiziert je Ecke von Hand (mapview.cpp:1758):
//
//     sx = halfW + dot(q, R) * focal / z * halfH
//     sy = halfH - dot(q, U) * focal / z * halfH
//
// Die Grafikkarte will stattdessen EINE Matrix. Beide muessen dasselbe
// Ergebnis liefern - sonst stehen die Bilder aus den zwei Wegen an
// verschiedenen Stellen, und man vergleicht Aepfel mit Birnen.
//
// Zwei Dinge daran sind leicht zu uebersehen:
//
//   1. `halfH` steht in BEIDEN Zeilen. Das Seitenverhaeltnis wirkt also auf
//      die Breite, nicht auf die Hoehe - das ist die Quake-Sichtweise, und
//      wer stattdessen die uebliche Formel nimmt, bekommt ein Bild mit
//      falschem Bildwinkel, das trotzdem "richtig aussieht", bis man es
//      danebenlegt.
//
//   2. `sy` hat ein MINUS. Der Bildschirm zaehlt nach unten, die Welt nach
//      oben.
//
// Deshalb steht die Matrix hier, getrennt, und wird gegen die Formel des
// Rasterers geprueft (tests/gpucam.cpp).
#ifndef BHED_GPUCAM_H
#define BHED_GPUCAM_H

#include "bhed/mapview.h"

namespace bhed::gpu {

// Baut die Matrix, die eine Weltecke nach NDC bringt (-1..1 in x und y,
// 0..1 in z, wie Direct3D es erwartet).
//
// `ziel` nimmt 16 float, ZEILENWEISE. Der Shader muss sie deshalb als
// `row_major` deklarieren - sonst liest Direct3D sie spaltenweise, und das
// Ergebnis ist eine transponierte Matrix. Das ist kein Absturz, sondern ein
// Bild, das aussieht, als stuende die Kamera woanders.
void baueViewProj(const Camera& cam, int breite, int hoehe, float nearPlane,
                  float farPlane, float* ziel);

// Zwei 4x4-Matrizen multiplizieren, zeilenweise: aus = a * b.
//
// Damit wird die Stellung eines Movers VOR dem Zeichnen in die Kameramatrix
// gerechnet, statt eine zweite Matrix in den Shader zu geben. Eine Matrix
// weniger im Konstantenpuffer ist eine weniger, die falsch liegen kann.
//
// `aus` darf auf `a` oder `b` zeigen - es wird zwischengespeichert.
void multipliziere(const float* a, const float* b, float* aus);

}  // namespace bhed::gpu

#endif  // BHED_GPUCAM_H
