// gpuskin.cpp - siehe bhed/gpuskin.h
//
// Braucht kein Direct3D. Geprueft in tests/gpuskin.cpp.

#include "bhed/gpuskin.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bhed::gpu {

void packeEcken(const GlmSurface& sf, std::vector<SkinVertex>& aus) {
    aus.reserve(aus.size() + sf.verts.size());
    for (const GlmVertex& v : sf.verts) {
        SkinVertex e{};
        for (int k = 0; k < 3; ++k) {
            e.pos[k] = v.xyz[k];
            e.normal[k] = v.normal[k];
        }
        e.uv[0] = v.st[0];
        e.uv[1] = v.st[1];

        // --- Knochen und Gewichte --------------------------------------
        //
        // Zwei Dinge werden hier abgefangen, die sonst erst auf der
        // Grafikkarte auffallen:
        //
        // 1. Eine Knochennummer ausserhalb der Liste. Der Rasterer prueft
        //    das (`if (b >= skinM.size()) continue;`, mapview.cpp:3858) und
        //    ueberspringt den Eintrag. Ein Shader kann das nicht - er liest
        //    einfach daneben. Deshalb hier auf 0 und Gewicht 0.
        //
        // 2. Gewichte, die sich nicht zu eins summieren. Das kommt in
        //    echten Modellen vor, und der Rasterer merkt es nicht, weil er
        //    ohnehin nur addiert. Auf der Grafikkarte wird daraus eine Figur,
        //    die zu klein oder zu gross ist - je nach Summe. Wird normiert.
        float summe = 0.0F;
        for (int k = 0; k < 4; ++k) {
            const int b = static_cast<int>(v.bones[k]);
            const float w = v.weights[k];
            if (b < 0 || b >= kMaxKnochen || w <= 0.0F) {
                e.bones[k] = 0.0F;
                e.weights[k] = 0.0F;
                continue;
            }
            e.bones[k] = static_cast<float>(b);
            e.weights[k] = w;
            summe += w;
        }
        if (summe > 0.0F) {
            for (float& w : e.weights) {
                w /= summe;
            }
        } else {
            // Gar kein gueltiger Knochen: die Ecke bleibt, wo sie ist,
            // statt in den Ursprung zu fallen.
            e.bones[0] = 0.0F;
            e.weights[0] = 1.0F;
        }
        aus.push_back(e);
    }
}

void baueFigurWelt(float yawGrad, const float pos[3], float* ziel) {
    if (ziel == nullptr) {
        return;
    }
    // 90 Grad Zuschlag - siehe kActorYawFix in mapview.cpp:3856.
    const float yaw = (yawGrad + 90.0F) * 3.14159265358979323846F / 180.0F;
    const float cs = std::cos(yaw);
    const float sn = std::sin(yaw);
    for (int i = 0; i < 16; ++i) {
        ziel[i] = 0.0F;
    }
    ziel[0] = cs;   ziel[1] = -sn;  ziel[3] = (pos != nullptr) ? pos[0] : 0.0F;
    ziel[4] = sn;   ziel[5] = cs;   ziel[7] = (pos != nullptr) ? pos[1] : 0.0F;
    ziel[10] = 1.0F;                ziel[11] = (pos != nullptr) ? pos[2] : 0.0F;
    ziel[15] = 1.0F;
}

void baueGriffWelt(const float* figurWelt, const BoneMatrix& hand, float* ziel) {
    if (figurWelt == nullptr || ziel == nullptr) {
        return;
    }
    float h[16] = {hand.m[0][0], hand.m[0][1], hand.m[0][2], hand.m[0][3],
                   hand.m[1][0], hand.m[1][1], hand.m[1][2], hand.m[1][3],
                   hand.m[2][0], hand.m[2][1], hand.m[2][2], hand.m[2][3],
                   0.0F,         0.0F,         0.0F,         1.0F};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float s = 0.0F;
            for (int k = 0; k < 4; ++k) {
                s += figurWelt[r * 4 + k] * h[k * 4 + c];
            }
            ziel[r * 4 + c] = s;
        }
    }
}

void baueKlingenband(const float wurzel[3], const float spitze[3],
                     const float auge[3], const float rechts[3], float radius,
                     float ecken[4][3]) {
    const auto norm = [](float v[3]) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 1e-12F) {
            v[0] /= l; v[1] /= l; v[2] /= l;
        }
    };
    float laengs[3] = {spitze[0] - wurzel[0], spitze[1] - wurzel[1], spitze[2] - wurzel[2]};
    float zumAuge[3] = {wurzel[0] - auge[0], wurzel[1] - auge[1], wurzel[2] - auge[2]};
    norm(laengs);
    norm(zumAuge);
    float quer[3] = {laengs[1] * zumAuge[2] - laengs[2] * zumAuge[1],
                     laengs[2] * zumAuge[0] - laengs[0] * zumAuge[2],
                     laengs[0] * zumAuge[1] - laengs[1] * zumAuge[0]};
    if (quer[0] * quer[0] + quer[1] * quer[1] + quer[2] * quer[2] < 1e-8F) {
        quer[0] = rechts[0]; quer[1] = rechts[1]; quer[2] = rechts[2];
    }
    norm(quer);
    for (int k = 0; k < 3; ++k) {
        ecken[0][k] = wurzel[k] - quer[k] * radius;
        ecken[1][k] = wurzel[k] + quer[k] * radius;
        ecken[2][k] = spitze[k] + quer[k] * radius;
        ecken[3][k] = spitze[k] - quer[k] * radius;
    }
}

void baueMoverWelt(const float pivot[3], const float offset[3], float yawGrad,
                   float pitchGrad, float rollGrad, bool taumelt,
                   float* ziel, const float* skala) {
    if (ziel == nullptr) {
        return;
    }
    constexpr float kPi = 3.14159265358979323846F;
    const float cs = std::cos(yawGrad * kPi / 180.0F);
    const float sn = std::sin(yawGrad * kPi / 180.0F);
    const float pc = taumelt ? std::cos(pitchGrad * kPi / 180.0F) : 1.0F;
    const float ps = taumelt ? std::sin(pitchGrad * kPi / 180.0F) : 0.0F;
    const float rc = taumelt ? std::cos(rollGrad * kPi / 180.0F) : 1.0F;
    const float rs = taumelt ? std::sin(rollGrad * kPi / 180.0F) : 0.0F;

    // Die drei Drehungen als eine 3x3, in derselben Reihenfolge wie
    // placeVert: Gier, dann Nick, dann Roll.
    //
    // Ausmultipliziert und nicht als drei Matrizen: der Rasterer rechnet es
    // ebenfalls in einem Zug, und drei Matrizen zu multiplizieren waere eine
    // zweite Fassung derselben Reihenfolge - genau die Stelle, an der sich
    // Achsen vertauschen.
    float m[3][3];
    // Nach Gier: ax = dx*cs - dy*sn ; ay = dx*sn + dy*cs ; az = dz
    // Nach Nick: bx = ax*pc + az*ps ; az' = -ax*ps + az*pc
    // Nach Roll: cy = ay*rc - az'*rs ; az'' = ay*rs + az'*rc
    // --- Die Reihenfolge der ENGINE: AnglesToAxis (q_math.c) ------------
    //
    // Die Spalten sind vorn, links und oben aus AngleVectors - ausmulti-
    // pliziert ist das Rz(Gier) * Ry(Nick) * Rx(Roll): auf den Punkt
    // wirkt ZUERST das Rollen, dann das Nicken, dann die Gier. Hier stand
    // es andersherum (erst Gier). Bei nur einer Drehung ist das dasselbe;
    // bei Gier 90 + Nick 90 zeigte +X nach (0,1,0) statt nach (0,0,-1).
    m[0][0] = pc * cs;   m[0][1] = rs * ps * cs - rc * sn;  m[0][2] = rc * ps * cs + rs * sn;
    m[1][0] = pc * sn;   m[1][1] = rs * ps * sn + rc * cs;  m[1][2] = rc * ps * sn - rs * cs;
    m[2][0] = -ps;       m[2][1] = rs * pc;                 m[2][2] = rc * pc;
    // Skaliert werden die ACHSEN der Stellung (Spalten), wie die Engine
    // axis[i] mit scale[i] multipliziert - erst skalieren, dann drehen.
    if (skala != nullptr) {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                m[r][c] *= skala[c];
            }
        }
    }

    for (int i = 0; i < 16; ++i) {
        ziel[i] = 0.0F;
    }
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            ziel[r * 4 + c] = m[r][c];
        }
        // Um den Drehpunkt: p' = M*(p - pivot) + pivot + offset
        const float pv = (pivot != nullptr) ? pivot[r] : 0.0F;
        float versch = pv + ((offset != nullptr) ? offset[r] : 0.0F);
        for (int c = 0; c < 3; ++c) {
            versch -= m[r][c] * ((pivot != nullptr) ? pivot[c] : 0.0F);
        }
        ziel[r * 4 + 3] = versch;
    }
    ziel[15] = 1.0F;
}

void baueKnochenmatrizen(const std::vector<BoneMatrix>& world,
                         const std::vector<GlaBone>& bones,
                         std::vector<BoneMatrix>& aus) {
    aus.assign(world.size(), BoneMatrix::identity());
    for (std::size_t b = 0; b < world.size(); ++b) {
        // Ein Knochen ohne Eintrag im Skelett bleibt die EINHEITSmatrix -
        // nicht Null. Der Rasterer liess ihn bisher auf der Vorgabe von
        // BoneMatrix stehen, was dasselbe bewirkt.
        if (b < bones.size()) {
            aus[b] = world[b] * bones[b].basePoseInv;
        }
    }
}

void packeKnochen(const std::vector<BoneMatrix>& matrizen, float* ziel) {
    if (ziel == nullptr) {
        return;
    }
    const std::size_t n =
        std::min(matrizen.size(), static_cast<std::size_t>(kMaxKnochen));
    for (std::size_t i = 0; i < n; ++i) {
        for (int r = 0; r < 3; ++r) {
            for (int c2 = 0; c2 < 4; ++c2) {
                ziel[i * 12U + static_cast<std::size_t>(r * 4 + c2)] =
                    matrizen[i].m[r][c2];
            }
        }
    }
    // Der Rest auf die EINHEITSmatrix, nicht auf Null.
    //
    // Eine Nullmatrix zieht jede Ecke, die versehentlich darauf zeigt, in
    // den Ursprung - und das sieht aus wie ein Dreieck, das quer durch die
    // Karte gespannt ist. Solche Zacken sind ein bekanntes Bild bei
    // fehlerhaftem Skinning, und sie sind schwer zuzuordnen. Die
    // Einheitsmatrix laesst die Ecke stehen, wo sie ist.
    for (std::size_t i = n; i < static_cast<std::size_t>(kMaxKnochen); ++i) {
        float* m = ziel + i * 12U;
        for (int k = 0; k < 12; ++k) {
            m[k] = 0.0F;
        }
        m[0] = 1.0F;
        m[5] = 1.0F;
        m[10] = 1.0F;
    }
}

}  // namespace bhed::gpu
