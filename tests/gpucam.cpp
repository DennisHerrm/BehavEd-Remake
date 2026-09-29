// Probe fuer die Kameramatrix.
//
// Die Frage, die sie beantwortet
// ------------------------------
// Landet eine Weltecke ueber die Matrix an derselben Bildschirmstelle wie
// ueber die Handrechnung des Rasterers (mapview.cpp:1758)?
//
// Wenn nicht, stehen die Bilder aus den zwei Wegen an verschiedenen Stellen,
// und der Vergleich - der einzige Weg, den GPU-Weg abzunehmen - waere
// wertlos. Man saehe zwei verschiedene Bilder und wuesste nicht, welches
// falsch ist.
//
// Zwei Fallen sind hier abgesichert:
//   - `halfH` steht im Rasterer in BEIDEN Zeilen, das Seitenverhaeltnis
//     wirkt also auf die Breite.
//   - `sy` hat ein Minus.

#include "bhed/gpucam.h"

#include <cmath>
#include <cstdio>

using namespace bhed;
using namespace bhed::gpu;

namespace {

int fehler = 0;

void erwarte(const char* was, bool ok) {
    std::printf("  %-4s  %s\n", ok ? "ok" : "FEHL", was);
    if (!ok) {
        ++fehler;
    }
}

constexpr float kPi = 3.14159265358979323846F;

// Die Handrechnung des Rasterers, woertlich aus mapview.cpp.
void wieDerRasterer(const Camera& cam, int W, int H, const float p[3],
                    float* sx, float* sy, float* z) {
    float fwd[3];
    float rgt[3];
    float upv[3];
    Camera c = cam;
    c.forward(fwd);
    c.right(rgt);
    c.up(upv);
    const float q[3] = {p[0] - cam.pos[0], p[1] - cam.pos[1],
                        p[2] - cam.pos[2]};
    const float focal = 1.0F / std::tan(cam.fovY * kPi / 180.0F * 0.5F);
    const float halfW = static_cast<float>(W) * 0.5F;
    const float halfH = static_cast<float>(H) * 0.5F;
    const float qz = q[0] * fwd[0] + q[1] * fwd[1] + q[2] * fwd[2];
    const float qr = q[0] * rgt[0] + q[1] * rgt[1] + q[2] * rgt[2];
    const float qu = q[0] * upv[0] + q[1] * upv[1] + q[2] * upv[2];
    *z = qz;
    *sx = halfW + qr * focal / qz * halfH;
    *sy = halfH - qu * focal / qz * halfH;
}

// Dieselbe Ecke ueber die Matrix.
void ueberDieMatrix(const float m[16], int W, int H, const float p[3],
                    float* sx, float* sy, float* w) {
    const float x = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
    const float y = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
    const float ww = m[12] * p[0] + m[13] * p[1] + m[14] * p[2] + m[15];
    *w = ww;
    const float ndcX = x / ww;
    const float ndcY = y / ww;
    *sx = (ndcX + 1.0F) * 0.5F * static_cast<float>(W);
    *sy = (1.0F - ndcY) * 0.5F * static_cast<float>(H);
}

bool nah(float a, float b, float toleranz) {
    return std::fabs(a - b) < toleranz;
}

}  // namespace

int main() {
    std::printf("gpucam\n");

    // Ein Bildausschnitt, der NICHT quadratisch ist - sonst faellt die
    // Verwechslung von halfW und halfH gar nicht auf.
    const int W = 1280;
    const int H = 720;

    Camera cam;
    cam.pos[0] = 100.0F;
    cam.pos[1] = -200.0F;
    cam.pos[2] = 50.0F;
    cam.angles[0] = 10.0F;
    cam.angles[1] = 30.0F;
    cam.angles[2] = 0.0F;
    cam.fovY = 80.0F;

    float m[16];
    baueViewProj(cam, W, H, 4.0F, 20000.0F, m);

    // Mehrere Punkte, verteilt - ein einzelner koennte zufaellig passen.
    const float punkte[][3] = {
        {300.0F, 400.0F, 60.0F},   {-500.0F, 200.0F, 0.0F},
        {120.0F, 900.0F, 300.0F},  {0.0F, 0.0F, 0.0F},
        {1000.0F, 1500.0F, -200.0F},
    };
    int gleich = 0;
    for (const auto& p : punkte) {
        float ax = 0.0F;
        float ay = 0.0F;
        float az = 0.0F;
        wieDerRasterer(cam, W, H, p, &ax, &ay, &az);
        if (az <= 1.0F) {
            continue;   // hinter der Kamera, nicht vergleichbar
        }
        float bx = 0.0F;
        float by = 0.0F;
        float bw = 0.0F;
        ueberDieMatrix(m, W, H, p, &bx, &by, &bw);
        char z[160];
        std::snprintf(z, sizeof(z),
                      "Punkt (%.0f %.0f %.0f): Rasterer %.2f/%.2f, "
                      "Matrix %.2f/%.2f",
                      static_cast<double>(p[0]), static_cast<double>(p[1]),
                      static_cast<double>(p[2]), static_cast<double>(ax),
                      static_cast<double>(ay), static_cast<double>(bx),
                      static_cast<double>(by));
        // Ein Zehntel Bildpunkt. Enger waere Rundungsrauschen, weiter waere
        // ein sichtbarer Versatz.
        erwarte(z, nah(ax, bx, 0.1F) && nah(ay, by, 0.1F));
        erwarte("  und w ist die Tiefe des Rasterers", nah(az, bw, 0.01F));
        ++gleich;
    }
    erwarte("mindestens drei Punkte waren vergleichbar", gleich >= 3);

    // --- Das Seitenverhaeltnis wirkt auf die BREITE ------------------------
    //
    // Wer y skaliert statt x, bekommt ein Bild mit falschem Bildwinkel, das
    // trotzdem plausibel aussieht - bis man es danebenlegt.
    {
        float breit[16];
        float schmal[16];
        baueViewProj(cam, 1600, 400, 4.0F, 20000.0F, breit);
        baueViewProj(cam, 400, 400, 4.0F, 20000.0F, schmal);
        erwarte("die y-Skalierung haengt NICHT vom Format ab",
                nah(breit[4], schmal[4], 1e-6F));
        erwarte("die x-Skalierung schon",
                !nah(breit[0], schmal[0], 1e-6F));
    }

    // --- Die Tiefe geht von 0 bis 1, wie Direct3D es will ------------------
    {
        float fwd[3];
        Camera c = cam;
        c.forward(fwd);
        const float nah1 = 4.0F;
        const float fern = 20000.0F;
        float mm[16];
        baueViewProj(cam, W, H, nah1, fern, mm);
        // Ein Punkt genau auf der Nahebene, einer auf der Fernebene.
        for (int fall = 0; fall < 2; ++fall) {
            const float d = (fall == 0) ? nah1 : fern;
            const float p[3] = {cam.pos[0] + fwd[0] * d,
                                cam.pos[1] + fwd[1] * d,
                                cam.pos[2] + fwd[2] * d};
            const float zz = mm[8] * p[0] + mm[9] * p[1] + mm[10] * p[2] + mm[11];
            const float ww = mm[12] * p[0] + mm[13] * p[1] + mm[14] * p[2] + mm[15];
            const float ndcZ = zz / ww;
            erwarte((fall == 0) ? "Nahebene ist 0" : "Fernebene ist 1",
                    nah(ndcZ, (fall == 0) ? 0.0F : 1.0F, 0.001F));
        }
    }

    // --- Unsinnige Groessen stuerzen nicht ---------------------------------
    {
        float mm[16];
        baueViewProj(cam, 0, 0, 4.0F, 20000.0F, mm);
        bool allesNull = true;
        for (const float v : mm) {
            if (v != 0.0F) {
                allesNull = false;
            }
        }
        erwarte("Breite oder Hoehe null gibt eine Nullmatrix", allesNull);
        baueViewProj(cam, W, H, 4.0F, 20000.0F, nullptr);
        erwarte("nullptr wird abgefangen", true);
    }

    // --- Zwei Matrizen multiplizieren ---------------------------------------
    //
    // Damit wird die Stellung eines Movers in die Kameramatrix gerechnet.
    {
        // Eine Verschiebung um (10, 20, 30) als "Mover".
        float welt[16] = {1,0,0,10, 0,1,0,20, 0,0,1,30, 0,0,0,1};
        float mvp[16];
        multipliziere(m, welt, mvp);
        const float lokal[3] = {3.0F, -4.0F, 5.0F};
        // Ueber die zusammengesetzte Matrix ...
        const float ax = mvp[0]*lokal[0] + mvp[1]*lokal[1] + mvp[2]*lokal[2] + mvp[3];
        const float aw = mvp[12]*lokal[0] + mvp[13]*lokal[1] + mvp[14]*lokal[2] + mvp[15];
        // ... muss dasselbe herauskommen wie ueber die verschobene Ecke.
        const float v[3] = {lokal[0] + 10.0F, lokal[1] + 20.0F, lokal[2] + 30.0F};
        const float bx = m[0]*v[0] + m[1]*v[1] + m[2]*v[2] + m[3];
        const float bw = m[12]*v[0] + m[13]*v[1] + m[14]*v[2] + m[15];
        erwarte("Kamera mal Moverstellung ist die verschobene Ecke",
                nah(ax, bx, 0.01F) && nah(aw, bw, 0.01F));

        // Das Ziel darf auf eine der Quellen zeigen.
        float kopie[16];
        for (int i = 0; i < 16; ++i) { kopie[i] = m[i]; }
        multipliziere(kopie, welt, kopie);
        erwarte("Ziel gleich Quelle geht gut", nah(kopie[3], mvp[3], 0.01F));

        multipliziere(nullptr, welt, mvp);
        erwarte("nullptr wird abgefangen", true);
    }

    if (fehler != 0) {
        std::printf("FEHLGESCHLAGEN (%d)\n", fehler);
        return 1;
    }
    std::printf("alle Gegenproben bestanden (0 Fehlschlaege)\n");
    return 0;
}
