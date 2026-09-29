// gpucam.cpp - siehe bhed/gpucam.h

#include "bhed/gpucam.h"

#include <cmath>

namespace bhed::gpu {

namespace {

constexpr float kPi = 3.14159265358979323846F;

float rad(float grad) { return grad * kPi / 180.0F; }

}  // namespace

void baueViewProj(const Camera& cam, int breite, int hoehe, float nearPlane,
                  float farPlane, float* ziel) {
    if (ziel == nullptr) {
        return;
    }
    for (int i = 0; i < 16; ++i) {
        ziel[i] = 0.0F;
    }
    if (breite <= 0 || hoehe <= 0) {
        return;
    }

    float fwd[3];
    float rgt[3];
    float upv[3];
    Camera c = cam;
    c.forward(fwd);
    c.right(rgt);
    c.up(upv);

    const float focal = 1.0F / std::tan(rad(cam.fovY) * 0.5F);
    const float halfW = static_cast<float>(breite) * 0.5F;
    const float halfH = static_cast<float>(hoehe) * 0.5F;

    // Der Rasterer rechnet:
    //
    //     sx = halfW + dot(q,R) * focal/z * halfH
    //     sy = halfH - dot(q,U) * focal/z * halfH
    //
    // In NDC (-1..1) heisst das:
    //
    //     ndcX = (sx - halfW) / halfW = dot(q,R) * focal/z * (halfH/halfW)
    //     ndcY = (halfH - sy) / halfH = dot(q,U) * focal/z
    //
    // Also: x bekommt den Faktor halfH/halfW, y nicht. Das ist der Punkt,
    // an dem die uebliche Formel (die y skaliert) ein anderes Bild ergibt.
    const float sx = focal * (halfH / halfW);
    const float sy = focal;

    // Die Zeilen der Matrix. `q` ist die Ecke MINUS der Kameraposition,
    // deshalb steht in der vierten Spalte jeweils -dot(eye, achse).
    auto punkt = [&](const float* a) {
        return a[0] * cam.pos[0] + a[1] * cam.pos[1] + a[2] * cam.pos[2];
    };

    // Zeile 0: x
    ziel[0] = rgt[0] * sx;
    ziel[1] = rgt[1] * sx;
    ziel[2] = rgt[2] * sx;
    ziel[3] = -punkt(rgt) * sx;

    // Zeile 1: y
    ziel[4] = upv[0] * sy;
    ziel[5] = upv[1] * sy;
    ziel[6] = upv[2] * sy;
    ziel[7] = -punkt(upv) * sy;

    // Zeile 2: Tiefe. Direct3D erwartet 0 an der Nahebene und 1 an der
    // Fernebene - anders als OpenGL, das -1..1 nimmt. Das ist die uebliche
    // Formel dafuer.
    const float a = farPlane / (farPlane - nearPlane);
    const float b = -nearPlane * farPlane / (farPlane - nearPlane);
    ziel[8] = fwd[0] * a;
    ziel[9] = fwd[1] * a;
    ziel[10] = fwd[2] * a;
    ziel[11] = -punkt(fwd) * a + b;

    // Zeile 3: w = Abstand entlang der Blickrichtung. Das ist genau das `z`,
    // durch das der Rasterer teilt.
    ziel[12] = fwd[0];
    ziel[13] = fwd[1];
    ziel[14] = fwd[2];
    ziel[15] = -punkt(fwd);
}

void multipliziere(const float* a, const float* b, float* aus) {
    if (a == nullptr || b == nullptr || aus == nullptr) {
        return;
    }
    float t[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0F;
            for (int k = 0; k < 4; ++k) {
                sum += a[r * 4 + k] * b[k * 4 + c];
            }
            t[r * 4 + c] = sum;
        }
    }
    // Erst danach schreiben:  darf auf  oder  zeigen.
    for (int i = 0; i < 16; ++i) {
        aus[i] = t[i];
    }
}

}  // namespace bhed::gpu
