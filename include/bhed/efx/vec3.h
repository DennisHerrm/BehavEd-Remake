// Der Vektortyp, den die Effektsimulation braucht.
//
// efxed hat dafuer camera.h mit 195 Zeilen samt Matrizen und Projektion.
// Hier wird davon nichts gebraucht als der Vektor selbst - die Kamera hat
// behaved schon. Also nur dieses Stueck uebernehmen, nicht den Rest: eine
// zweite Kameraklasse im selben Programm waere die naechste Quelle fuer
// Abweichungen.
#pragma once

#include <cmath>

namespace bhed::efx::camera {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(const Vec3& a, const Vec3& b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(const Vec3& v, float s) {
    return Vec3{v.x * s, v.y * s, v.z * s};
}
inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalise(const Vec3& v) {
    const float l = length(v);
    return l > 0.0F ? v * (1.0F / l) : v;
}

}  // namespace bhed::efx::camera
