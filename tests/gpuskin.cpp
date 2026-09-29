// Probe fuer die Vorbereitung der Figuren.
//
// Was sie faengt
// --------------
// Zwei Dinge, die auf der Grafikkarte sehr haesslich aussehen und dort kaum
// zuzuordnen sind:
//
//   Eine Knochennummer ausserhalb der Liste. Der Rasterer prueft das und
//   ueberspringt den Eintrag (mapview.cpp:3858). Ein Shader kann das nicht -
//   er liest einfach daneben.
//
//   Gewichte, die sich nicht zu eins summieren. Der Rasterer merkt es nicht,
//   weil er ohnehin nur addiert. Auf der Grafikkarte wird daraus eine Figur,
//   die zu klein oder zu gross ist.
//
// Und die Einheitsmatrix fuer unbenutzte Knochen: eine Nullmatrix zieht jede
// Ecke, die versehentlich darauf zeigt, in den Ursprung - das sind die
// Zacken quer durch die Karte, die man von kaputtem Skinning kennt.

#include "bhed/gpushader.h"
#include "bhed/gpuskin.h"

#include <string>

#include <cmath>
#include <cstddef>
#include <cstdint>
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

bool nah(float a, float b) { return std::fabs(a - b) < 1e-5F; }

GlmSurface eineEcke(std::uint8_t b0, float w0, std::uint8_t b1, float w1) {
    GlmSurface sf;
    GlmVertex v{};
    v.xyz[0] = 1.0F;
    v.xyz[1] = 2.0F;
    v.xyz[2] = 3.0F;
    v.normal[2] = 1.0F;
    v.st[0] = 0.25F;
    v.st[1] = 0.75F;
    v.bones[0] = b0;
    v.weights[0] = w0;
    v.bones[1] = b1;
    v.weights[1] = w1;
    sf.verts.push_back(v);
    return sf;
}

}  // namespace

int main() {
    std::printf("gpuskin\n");

    // --- Der Aufbau der Ecke -----------------------------------------------
    //
    // sizeof wird geprueft, damit ein eingeschobenes Feld auffaellt statt
    // verzerrte Figuren zu erzeugen.
    erwarte("SkinVertex ist 64 Byte", sizeof(SkinVertex) == 64U);

    // --- Ort, Normale und Koordinaten kommen unveraendert durch -------------
    //
    // Die Grafikkarte will die ROHEN Ecken - die Verformung rechnet sie
    // selbst. Wer hier schon verformt, tut es zweimal.
    {
        std::vector<SkinVertex> aus;
        packeEcken(eineEcke(3, 1.0F, 0, 0.0F), aus);
        erwarte("eine Ecke kommt an", aus.size() == 1);
        erwarte("der Ort bleibt roh",
                nah(aus[0].pos[0], 1.0F) && nah(aus[0].pos[2], 3.0F));
        erwarte("die Normale auch", nah(aus[0].normal[2], 1.0F));
        erwarte("und die Texturkoordinate",
                nah(aus[0].uv[0], 0.25F) && nah(aus[0].uv[1], 0.75F));
        erwarte("der Knochen steht drin", nah(aus[0].bones[0], 3.0F));
    }

    // --- Anhaengen, nicht ersetzen ------------------------------------------
    //
    // Eine Figur hat mehrere Flaechen, und sie teilen sich einen Puffer.
    {
        std::vector<SkinVertex> aus;
        packeEcken(eineEcke(1, 1.0F, 0, 0.0F), aus);
        packeEcken(eineEcke(2, 1.0F, 0, 0.0F), aus);
        erwarte("zwei Flaechen -> zwei Ecken", aus.size() == 2);
        erwarte("die zweite hat ihren eigenen Knochen",
                nah(aus[1].bones[0], 2.0F));
    }

    // --- Gewichte werden normiert -------------------------------------------
    {
        std::vector<SkinVertex> aus;
        packeEcken(eineEcke(1, 0.3F, 2, 0.3F), aus);   // Summe 0,6
        erwarte("die Summe ist eins",
                nah(aus[0].weights[0] + aus[0].weights[1], 1.0F));
        erwarte("und das Verhaeltnis bleibt",
                nah(aus[0].weights[0], aus[0].weights[1]));
    }

    // --- Eine Knochennummer ausserhalb der Liste ----------------------------
    //
    // Ein Shader liest sonst daneben. Der Eintrag bekommt Gewicht 0.
    {
        std::vector<SkinVertex> aus;
        packeEcken(eineEcke(200, 0.5F, 1, 0.5F), aus);
        erwarte("der ungueltige Knochen bekommt Gewicht 0",
                nah(aus[0].weights[0], 0.0F));
        erwarte("der gueltige bekommt alles",
                nah(aus[0].weights[1], 1.0F));
    }

    // --- Gar kein gueltiger Knochen -----------------------------------------
    //
    // Die Ecke bleibt, wo sie ist, statt in den Ursprung zu fallen.
    {
        std::vector<SkinVertex> aus;
        packeEcken(eineEcke(200, 0.5F, 201, 0.5F), aus);
        erwarte("ohne gueltigen Knochen: Gewicht 1 auf Knochen 0",
                nah(aus[0].weights[0], 1.0F) && nah(aus[0].bones[0], 0.0F));
    }

    // --- Die Matrizenliste ---------------------------------------------------
    {
        std::vector<float> buf(static_cast<std::size_t>(kMaxKnochen) * 12U,
                               -99.0F);
        std::vector<BoneMatrix> m(2);
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                m[0].m[r][c] = static_cast<float>(r * 4 + c);
                m[1].m[r][c] = static_cast<float>(100 + r * 4 + c);
            }
        }
        packeKnochen(m, buf.data());
        erwarte("die erste Matrix steht vorn", nah(buf[0], 0.0F) &&
                                                   nah(buf[11], 11.0F));
        erwarte("die zweite dahinter", nah(buf[12], 100.0F));

        // Der Rest: EINHEITSmatrix, nicht Null.
        const std::size_t drei = 2U * 12U;
        erwarte("unbenutzte Knochen sind die Einheitsmatrix",
                nah(buf[drei + 0], 1.0F) && nah(buf[drei + 5], 1.0F) &&
                    nah(buf[drei + 10], 1.0F));
        erwarte("und ihre Verschiebung ist null",
                nah(buf[drei + 3], 0.0F) && nah(buf[drei + 7], 0.0F));
        bool nichtsOffen = true;
        for (const float v : buf) {
            if (v == -99.0F) {
                nichtsOffen = false;
            }
        }
        erwarte("kein Platz bleibt unbeschrieben", nichtsOffen);
    }

    // --- Mehr Knochen als Platz ----------------------------------------------
    {
        std::vector<float> buf(static_cast<std::size_t>(kMaxKnochen) * 12U, 0.0F);
        std::vector<BoneMatrix> m(kMaxKnochen + 10);
        packeKnochen(m, buf.data());
        erwarte("mehr Knochen als Platz stuerzt nicht", true);
        packeKnochen(m, nullptr);
        erwarte("nullptr wird abgefangen", true);
    }

    // --- Die Stellung der Figur --------------------------------------------
    //
    // Gegen die Handrechnung des Rasterers (mapview.cpp:3924). Der Zuschlag
    // von 90 Grad muss drinstecken - ohne ihn stuenden alle Figuren um einen
    // Viertelkreis verdreht.
    {
        const float pos[3] = {10.0F, 20.0F, 30.0F};
        float m[16];
        baueFigurWelt(35.0F, pos, m);
        const float yaw = (35.0F + 90.0F) * 3.14159265F / 180.0F;
        const float cs = std::cos(yaw);
        const float sn = std::sin(yaw);
        const float lokal[3] = {1.0F, 2.0F, 3.0F};
        // Handrechnung
        const float ex = lokal[0] * cs - lokal[1] * sn + pos[0];
        const float ey = lokal[0] * sn + lokal[1] * cs + pos[1];
        const float ez = lokal[2] + pos[2];
        // Ueber die Matrix
        const float mx = m[0]*lokal[0] + m[1]*lokal[1] + m[2]*lokal[2] + m[3];
        const float my = m[4]*lokal[0] + m[5]*lokal[1] + m[6]*lokal[2] + m[7];
        const float mz = m[8]*lokal[0] + m[9]*lokal[1] + m[10]*lokal[2] + m[11];
        erwarte("x wie der Rasterer", nah(mx, ex));
        erwarte("y wie der Rasterer", nah(my, ey));
        erwarte("z wie der Rasterer", nah(mz, ez));
        erwarte("die letzte Zeile ist 0,0,0,1",
                nah(m[12], 0.0F) && nah(m[15], 1.0F));
        baueFigurWelt(0.0F, nullptr, m);
        erwarte("ohne Ort stuerzt es nicht", true);
        baueFigurWelt(0.0F, pos, nullptr);
        erwarte("nullptr wird abgefangen", true);
    }

    // --- Die Stellung eines Movers ------------------------------------------
    //
    // Gegen die Handrechnung des Rasterers (placeVert, mapview.cpp:1262).
    // Erst Gier um Z, dann Nick und Roll, alles um den DREHPUNKT, danach der
    // Versatz.
    {
        const float pivot[3] = {5.0F, -3.0F, 2.0F};
        const float offset[3] = {10.0F, 20.0F, 30.0F};
        const float lokal[3] = {7.0F, 1.0F, -4.0F};

        for (int fall = 0; fall < 2; ++fall) {
            const bool taumelt = (fall == 1);
            const float yaw = 25.0F;
            const float pitch = taumelt ? 12.0F : 0.0F;
            const float roll = taumelt ? -8.0F : 0.0F;

            // Die Handrechnung, Zeile fuer Zeile aus placeVert.
            const float cs = std::cos(yaw * 3.14159265F / 180.0F);
            const float sn = std::sin(yaw * 3.14159265F / 180.0F);
            const float pc = taumelt ? std::cos(pitch * 3.14159265F / 180.0F) : 1.0F;
            const float ps = taumelt ? std::sin(pitch * 3.14159265F / 180.0F) : 0.0F;
            const float rc = taumelt ? std::cos(roll * 3.14159265F / 180.0F) : 1.0F;
            const float rs = taumelt ? std::sin(roll * 3.14159265F / 180.0F) : 0.0F;
            const float dx = lokal[0] - pivot[0];
            const float dy = lokal[1] - pivot[1];
            float ax = dx * cs - dy * sn;
            float ay = dx * sn + dy * cs;
            float az = lokal[2] - pivot[2];
            if (taumelt) {
                // Die Matrix der Engine (AnglesToAxis, q_math.c): Spalten
                // vorn, links, oben - erst Rollen, dann Nicken, dann Gier.
                const float dz = lokal[2] - pivot[2];
                ax = dx * (pc * cs) + dy * (rs * ps * cs - rc * sn) + dz * (rc * ps * cs + rs * sn);
                ay = dx * (pc * sn) + dy * (rs * ps * sn + rc * cs) + dz * (rc * ps * sn - rs * cs);
                az = dx * (-ps) + dy * (rs * pc) + dz * (rc * pc);
            }
            const float ex = pivot[0] + ax + offset[0];
            const float ey = pivot[1] + ay + offset[1];
            const float ez = pivot[2] + az + offset[2];

            float m[16];
            baueMoverWelt(pivot, offset, yaw, pitch, roll, taumelt, m);
            const float mx = m[0]*lokal[0] + m[1]*lokal[1] + m[2]*lokal[2] + m[3];
            const float my = m[4]*lokal[0] + m[5]*lokal[1] + m[6]*lokal[2] + m[7];
            const float mz = m[8]*lokal[0] + m[9]*lokal[1] + m[10]*lokal[2] + m[11];
            char z[96];
            std::snprintf(z, sizeof(z), "Mover%s: x wie der Rasterer",
                          taumelt ? " (taumelnd)" : "");
            erwarte(z, nah(mx, ex));
            std::snprintf(z, sizeof(z), "Mover%s: y wie der Rasterer",
                          taumelt ? " (taumelnd)" : "");
            erwarte(z, nah(my, ey));
            std::snprintf(z, sizeof(z), "Mover%s: z wie der Rasterer",
                          taumelt ? " (taumelnd)" : "");
            erwarte(z, nah(mz, ez));
        }
        float m[16];
        baueMoverWelt(nullptr, nullptr, 0.0F, 0.0F, 0.0F, false, m);
        erwarte("ohne Drehpunkt und Versatz ist es die Einheitsmatrix",
                nah(m[0], 1.0F) && nah(m[5], 1.0F) && nah(m[10], 1.0F) &&
                    nah(m[3], 0.0F));
        baueMoverWelt(nullptr, nullptr, 0.0F, 0.0F, 0.0F, false, nullptr);
        erwarte("nullptr wird abgefangen", true);
        // modelscale_vec eines Kartenmodells: erst je Achse skalieren, dann
        // drehen (CG_CreateMiscEntFromGent skaliert axis[i]).
        {
            const float skala[3] = {0.5F, 0.25F, 2.0F};
            const float ort[3] = {100.0F, 200.0F, 0.0F};
            float s[16];
            baueMoverWelt(nullptr, ort, 90.0F, 0.0F, 0.0F, false, s, skala);
            // Punkt (10, 8, 3): skaliert (5, 2, 6), um 90 Grad gedreht (-2, 5, 6),
            // verschoben (98, 205, 6).
            const float x = s[0] * 10.0F + s[1] * 8.0F + s[2] * 3.0F + s[3];
            const float y = s[4] * 10.0F + s[5] * 8.0F + s[6] * 3.0F + s[7];
            const float z = s[8] * 10.0F + s[9] * 8.0F + s[10] * 3.0F + s[11];
            erwarte("modelscale_vec: erst skalieren, dann drehen, dann verschieben",
                    nah(x, 98.0F) && nah(y, 205.0F) && nah(z, 6.0F));
        }
    }

    // --- Der Shader und dieser Packer meinen denselben Puffer -------------
    //
    // kMaxKnochen steht in gpuskin.h, die Puffergroesse im HLSL. Zwei Zahlen,
    // eine Wahrheit - laufen sie auseinander, schreibt der Packer in Zeilen,
    // die der Shader nicht liest, und die Figur verformt sich nur teilweise.
    {
        const std::string f = figurVertexShaderHlsl(0U);
        char z[64];
        std::snprintf(z, sizeof(z), "gKnochen[%d]", kMaxKnochen * 3);
        erwarte("der Shaderpuffer passt zu kMaxKnochen",
                f.find(z) != std::string::npos);
    }

    if (fehler != 0) {
        std::printf("FEHLGESCHLAGEN (%d)\n", fehler);
        return 1;
    }
    std::printf("alle Gegenproben bestanden (0 Fehlschlaege)\n");
    return 0;
}
