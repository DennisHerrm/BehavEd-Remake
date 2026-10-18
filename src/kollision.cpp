// kollision.cpp - Paket bewegung (rc601, Wege wie im Spiel): Spuren gegen die
// Brushes der Karte. Warum: bhed/kollision.h. Portiert aus Movie Duels
// code/qcommon/cm_load.cpp und cm_trace.cpp (gleich OpenJK bis auf die
// vermerkte Stelle in CM_TraceThroughTree).
#include "bhed/kollision.h"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>
#include <cmath>
#include <cstring>

namespace bhed {
namespace {

constexpr float kSurfaceClipEpsilon = 0.125F;   // SURFACE_CLIP_EPSILON (cm_local.h)
constexpr int kLumpShaders = 1;
constexpr int kLumpPlanes = 2;
constexpr int kLumpNodes = 3;
constexpr int kLumpLeafs = 4;
constexpr int kLumpLeafBrushes = 6;
constexpr int kLumpModels = 7;
constexpr int kLumpBrushes = 8;
constexpr int kLumpBrushSides = 9;
constexpr int kLumpLeafSurfaces = 5;   // rc601 (bewegung, teil 3)
constexpr int kLumpDrawVerts = 10;
constexpr int kLumpSurfaces = 13;

std::int32_t i32(const std::string& b, std::size_t at) {
    std::int32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}
float f32(const std::string& b, std::size_t at) {
    float v = 0.0F;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}
struct Lump {
    std::size_t ofs = 0;
    std::size_t len = 0;
};
Lump lump(const std::string& b, int i) {
    Lump l;
    const std::size_t at = 8 + static_cast<std::size_t>(i) * 8;
    if (at + 8 > b.size()) { return l; }
    const std::int32_t o = i32(b, at);
    const std::int32_t n = i32(b, at + 4);
    if (o < 0 || n < 0 || static_cast<std::size_t>(o) + static_cast<std::size_t>(n) > b.size()) { return l; }
    l.ofs = static_cast<std::size_t>(o);
    l.len = static_cast<std::size_t>(n);
    return l;
}
float punkt(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// AngleVectors (q_math.c)
void winkelVektoren(const float w[3], float vorn[3], float rechts[3], float oben[3]) {
    const float grad = 3.14159265358979323846F / 180.0F;
    const float sy = std::sin(w[1] * grad);
    const float cy = std::cos(w[1] * grad);
    const float sp = std::sin(w[0] * grad);
    const float cp = std::cos(w[0] * grad);
    const float sr = std::sin(w[2] * grad);
    const float cr = std::cos(w[2] * grad);
    vorn[0] = cp * cy;
    vorn[1] = cp * sy;
    vorn[2] = -sp;
    rechts[0] = (-1 * sr * sp * cy + -1 * cr * -sy);
    rechts[1] = (-1 * sr * sp * sy + -1 * cr * cy);
    rechts[2] = -1 * sr * cp;
    oben[0] = (cr * sp * cy + -sr * -sy);
    oben[1] = (cr * sp * sy + -sr * cy);
    oben[2] = cr * cp;
}

// Die Zaehlmarke je Brush (b->checkcount, cmg.checkcount) - je Thread.
struct Marken {
    const KollisionsKarte* karte = nullptr;
    std::vector<std::uint32_t> stand;
    std::vector<std::uint32_t> patchStand;   // rc601 (bewegung, teil 3): patch->checkcount
    std::uint32_t zaehler = 0;
};
Marken& marken(const KollisionsKarte& k) {
    thread_local Marken m;
    if (m.karte != &k || m.stand.size() != k.pinsel.size() || m.patchStand.size() != k.patches.size()) {
        m.karte = &k;
        m.stand.assign(k.pinsel.size(), 0U);
        m.patchStand.assign(k.patches.size(), 0U);
        m.zaehler = 0;
    }
    return m;
}

struct Arbeit {   // traceWork_t
    const KollisionsKarte* k = nullptr;
    Marken* m = nullptr;
    float start[3]{};
    float end[3]{};
    float size[2][3]{};
    float offsets[8][3]{};
    float maxOffset = 0.0F;
    float extents[3]{};
    float bounds[2][3]{};
    bool isPoint = false;
    std::uint32_t contents = 0;
    KollisionsSpur trace;
};

bool schonGeprueft(Arbeit& tw, int b) {
    std::uint32_t& s = tw.m->stand[static_cast<std::size_t>(b)];
    if (s == tw.m->zaehler) { return true; }
    s = tw.m->zaehler;
    return false;
}

// CM_TestBoxInBrush (cm_trace.cpp:39)
void kastenImPinsel(Arbeit& tw, const KollisionsKarte::Pinsel& b) {
    if (b.seiten <= 0) { return; }
    if (tw.bounds[0][0] > b.maxs[0] || tw.bounds[0][1] > b.maxs[1] || tw.bounds[0][2] > b.maxs[2] ||
        tw.bounds[1][0] < b.mins[0] || tw.bounds[1][1] < b.mins[1] || tw.bounds[1][2] < b.mins[2]) {
        return;
    }
    for (int i = 6; i < b.seiten; ++i) {
        const KollisionsKarte::Ebene& e = tw.k->ebenen[static_cast<std::size_t>(tw.k->seiten[static_cast<std::size_t>(b.ersteSeite + i)].ebene)];
        const float dist = e.abstand - punkt(tw.offsets[e.vorzeichen], e.normale);
        const float d1 = punkt(tw.start, e.normale) - dist;
        if (d1 > 0) { return; }
    }
    tw.trace.startsolid = true;
    tw.trace.allsolid = true;
    tw.trace.fraction = 0;
    tw.trace.inhalt = b.inhalt;
}

// CM_TraceThroughBrush (cm_trace.cpp:390)
void durchPinsel(Arbeit& tw, const KollisionsKarte::Pinsel& b) {
    float enterFrac = -1.0F;
    float leaveFrac = 1.0F;
    const KollisionsKarte::Ebene* clip = nullptr;
    int leadside = -1;   // rc601 saber: fuer trace->surfaceFlags (cm_trace.cpp)
    if (b.seiten <= 0) { return; }
    if (tw.bounds[0][0] > b.maxs[0] || tw.bounds[0][1] > b.maxs[1] || tw.bounds[0][2] > b.maxs[2] ||
        tw.bounds[1][0] < b.mins[0] || tw.bounds[1][1] < b.mins[1] || tw.bounds[1][2] < b.mins[2]) {
        return;
    }
    bool getout = false;
    bool startout = false;
    for (int i = 0; i < b.seiten; ++i) {
        const KollisionsKarte::Ebene& e = tw.k->ebenen[static_cast<std::size_t>(tw.k->seiten[static_cast<std::size_t>(b.ersteSeite + i)].ebene)];
        const float dist = e.abstand - punkt(tw.offsets[e.vorzeichen], e.normale);
        const float d1 = punkt(tw.start, e.normale) - dist;
        const float d2 = punkt(tw.end, e.normale) - dist;
        if (d2 > 0) { getout = true; }
        if (d1 > 0) { startout = true; }
        if (d1 > 0 && (d2 >= kSurfaceClipEpsilon || d2 >= d1)) { return; }
        if (d1 <= 0 && d2 <= 0) { continue; }
        if (d1 > d2) {
            float f = (d1 - kSurfaceClipEpsilon) / (d1 - d2);
            if (f < 0) { f = 0; }
            if (f > enterFrac) {
                enterFrac = f;
                clip = &e;
                leadside = b.ersteSeite + i;
            }
        } else {
            float f = (d1 + kSurfaceClipEpsilon) / (d1 - d2);
            if (f > 1) { f = 1; }
            if (f < leaveFrac) { leaveFrac = f; }
        }
    }
    if (!startout) {
        tw.trace.startsolid = true;
        tw.trace.inhalt |= b.inhalt;
        if (!getout) {
            tw.trace.allsolid = true;
            tw.trace.fraction = 0;
        }
        return;
    }
    if (enterFrac < leaveFrac) {
        if (enterFrac > -1 && enterFrac < tw.trace.fraction) {
            if (enterFrac < 0) { enterFrac = 0; }
            tw.trace.fraction = enterFrac;
            if (clip != nullptr) {
                for (int a = 0; a < 3; ++a) { tw.trace.normale[static_cast<std::size_t>(a)] = clip->normale[a]; }
            }
            tw.trace.inhalt = b.inhalt;
            // rc601 saber: trace->surfaceFlags = cm.shaders[leadside->shaderNum].surfaceFlags
            // (CM_TraceThroughBrush, cm_trace.cpp) - CG_AddSaberBladeGo fragt SURF_NOIMPACT.
            if (leadside >= 0) {
                const int sh = tw.k->seiten[static_cast<std::size_t>(leadside)].shader;
                if (sh >= 0 && static_cast<std::size_t>(sh) < tw.k->shaderFlaeche.size()) {
                    tw.trace.flaeche = tw.k->shaderFlaeche[static_cast<std::size_t>(sh)];
                }
            }
        }
    }
}

// =============================================================================
// rc601 (bewegung, teil 3): gekruemmte Flaechen (Patches) - cm_patch.cpp und
// cm_polylib.cpp (Movie Duels code/qcommon, = OpenJK) 1:1.
//
// Auftrag: "Curved surfaces (patches) block traces like CM_TraceThroughPatch
// (cm_patch.cpp: CM_GeneratePatchCollide, facets/borders)". Bis teil 3 hielt
// keine Spur an einem Patch an (Boegen, Rampen aus Kurven, Treppen als Patch).
// =============================================================================
constexpr int kMaxPatchVerts = 1024;          // MAX_PATCH_VERTS (cm_load.cpp:545)
constexpr int kMaxGrid = 129;                 // CM_MAX_GRID_SIZE
constexpr int kMaxFacets = 4096;              // MAX_FACETS
constexpr int kMaxPatchPlanes = 8192;         // MAX_PATCH_PLANES
constexpr double kSubdivideDistance = 16;     // SUBDIVIDE_DISTANCE
constexpr double kPlaneTriEpsilon = 0.1;      // PLANE_TRI_EPSILON
constexpr double kWrapPointEpsilon = 0.1;     // WRAP_POINT_EPSILON
constexpr double kPointEpsilon = 0.1;         // POINT_EPSILON
constexpr double kNormalEpsilon = 0.00015;    // NORMAL_EPSILON
constexpr double kDistEpsilon = 0.0235;       // DIST_EPSILON
constexpr float kMaxMapBounds = 65535.0F;     // MAX_MAP_BOUNDS (cm_polylib.h)
constexpr float kWorldSize = 65536.0F;        // WORLD_SIZE (MAX_WORLD_COORD 64*1024)
constexpr int kSeiteVorn = 0;                 // SIDE_FRONT
constexpr int kSeiteHinten = 1;               // SIDE_BACK
constexpr int kSeiteAuf = 2;                  // SIDE_ON

using V3 = std::array<float, 3>;
float vNorm(float* v) {   // VectorNormalize
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l != 0.0F) {
        const float il = 1.0F / l;
        v[0] *= il;
        v[1] *= il;
        v[2] *= il;
    }
    return l;
}
void vKreuz(const float* a, const float* b, float* c) {
    c[0] = a[1] * b[2] - a[2] * b[1];
    c[1] = a[2] * b[0] - a[0] * b[2];
    c[2] = a[0] * b[1] - a[1] * b[0];
}

// --- cm_polylib.cpp ---
struct Wicklung {
    std::vector<V3> p;
};
void wicklungGrenzen(const Wicklung& w, float mins[3], float maxs[3]) {   // WindingBounds
    mins[0] = mins[1] = mins[2] = kWorldSize;
    maxs[0] = maxs[1] = maxs[2] = -kWorldSize;
    for (const V3& q : w.p) {
        for (int j = 0; j < 3; ++j) {
            const float v = q[static_cast<std::size_t>(j)];
            if (v < mins[j]) { mins[j] = v; }
            if (v > maxs[j]) { maxs[j] = v; }
        }
    }
}
Wicklung grundWicklung(const float* normal, float dist) {   // BaseWindingForPlane
    float max = -kMaxMapBounds;
    int x = -1;
    for (int i = 0; i < 3; ++i) {
        const float v = std::fabs(normal[i]);
        if (v > max) {
            x = i;
            max = v;
        }
    }
    float vup[3] = {0.0F, 0.0F, 0.0F};
    if (x == 0 || x == 1) { vup[2] = 1.0F; } else { vup[0] = 1.0F; }
    const float v = punkt(vup, normal);
    for (int i = 0; i < 3; ++i) { vup[i] += -v * normal[i]; }
    {   // VectorNormalize2
        const float l = std::sqrt(vup[0] * vup[0] + vup[1] * vup[1] + vup[2] * vup[2]);
        if (l != 0.0F) {
            const float il = 1.0F / l;
            for (float& c : vup) { c *= il; }
        }
    }
    float org[3];
    for (int i = 0; i < 3; ++i) { org[i] = normal[i] * dist; }
    float vright[3];
    vKreuz(vup, normal, vright);
    for (int i = 0; i < 3; ++i) {
        vup[i] *= kMaxMapBounds;
        vright[i] *= kMaxMapBounds;
    }
    Wicklung w;
    w.p.resize(4);
    for (int i = 0; i < 3; ++i) {
        const auto u = static_cast<std::size_t>(i);
        w.p[0][u] = org[i] - vright[i] + vup[i];
        w.p[1][u] = org[i] + vright[i] + vup[i];
        w.p[2][u] = org[i] + vright[i] - vup[i];
        w.p[3][u] = org[i] - vright[i] - vup[i];
    }
    return w;
}
// ChopWindingInPlace; false: nichts blieb (*inout = nullptr)
bool wicklungSchneiden(Wicklung& in, const float* normal, float dist, float epsilon) {
    const std::size_t n = in.p.size();
    std::vector<float> dists(n + 1);
    std::vector<int> sides(n + 1);
    int counts[3] = {0, 0, 0};
    for (std::size_t i = 0; i < n; ++i) {
        float dot = punkt(in.p[i].data(), normal);
        dot -= dist;
        dists[i] = dot;
        if (dot > epsilon) {
            sides[i] = kSeiteVorn;
        } else if (dot < -epsilon) {
            sides[i] = kSeiteHinten;
        } else {
            sides[i] = kSeiteAuf;
        }
        counts[sides[i]]++;
    }
    sides[n] = sides[0];
    dists[n] = dists[0];
    if (counts[0] == 0) {
        in.p.clear();
        return false;
    }
    if (counts[1] == 0) { return true; }
    Wicklung f;
    for (std::size_t i = 0; i < n; ++i) {
        const V3 p1 = in.p[i];
        if (sides[i] == kSeiteAuf) {
            f.p.push_back(p1);
            continue;
        }
        if (sides[i] == kSeiteVorn) { f.p.push_back(p1); }
        if (sides[i + 1] == kSeiteAuf || sides[i + 1] == sides[i]) { continue; }
        const V3 p2 = in.p[(i + 1) % n];
        const float dot = dists[i] / (dists[i] - dists[i + 1]);
        V3 mid{};
        for (int j = 0; j < 3; ++j) {
            const auto u = static_cast<std::size_t>(j);
            if (normal[j] == 1.0F) {
                mid[u] = dist;
            } else if (normal[j] == -1.0F) {
                mid[u] = -dist;
            } else {
                mid[u] = p1[u] + dot * (p2[u] - p1[u]);
            }
        }
        f.p.push_back(mid);
    }
    in = std::move(f);
    return true;
}

// --- cm_patch.cpp: Gitter ---
struct Gitter {   // cGrid_t
    int breite = 0;
    int hoehe = 0;
    bool rundBreite = false;
    bool rundHoehe = false;
    std::vector<V3> punkte = std::vector<V3>(static_cast<std::size_t>(kMaxGrid * kMaxGrid));
    V3& at(int i, int j) { return punkte[static_cast<std::size_t>(i * kMaxGrid + j)]; }   // [width][height]
};
bool brauchtTeilung(const V3& a, const V3& b, const V3& c) {   // CM_NeedsSubdivision
    float delta[3];
    for (int i = 0; i < 3; ++i) {
        const auto u = static_cast<std::size_t>(i);
        const float lmid = static_cast<float>(0.5 * (a[u] + c[u]));
        const float cmid = static_cast<float>(0.5 * (0.5 * (a[u] + b[u]) + 0.5 * (b[u] + c[u])));
        delta[i] = cmid - lmid;
    }
    const float dist = delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
    return dist >= kSubdivideDistance * kSubdivideDistance;
}
void teilen(const V3& a, const V3& b, const V3& c, V3& o1, V3& o2, V3& o3) {   // CM_Subdivide
    for (std::size_t i = 0; i < 3; ++i) {
        o1[i] = static_cast<float>(0.5 * (a[i] + b[i]));
        o3[i] = static_cast<float>(0.5 * (b[i] + c[i]));
        o2[i] = static_cast<float>(0.5 * (o1[i] + o3[i]));
    }
}
void gitterTauschen(Gitter& g) {   // CM_TransposeGrid
    if (g.breite > g.hoehe) {
        for (int i = 0; i < g.hoehe; ++i) {
            for (int j = i + 1; j < g.breite; ++j) {
                if (j < g.hoehe) {
                    std::swap(g.at(i, j), g.at(j, i));
                } else {
                    g.at(i, j) = g.at(j, i);
                }
            }
        }
    } else {
        for (int i = 0; i < g.breite; ++i) {
            for (int j = i + 1; j < g.hoehe; ++j) {
                if (j < g.breite) {
                    std::swap(g.at(j, i), g.at(i, j));
                } else {
                    g.at(j, i) = g.at(i, j);
                }
            }
        }
    }
    std::swap(g.breite, g.hoehe);
    std::swap(g.rundBreite, g.rundHoehe);
}
void gitterRund(Gitter& g) {   // CM_SetGridWrapWidth
    int i = 0;
    for (; i < g.hoehe; ++i) {
        int j = 0;
        for (; j < 3; ++j) {
            const float d = g.at(0, i)[static_cast<std::size_t>(j)] - g.at(g.breite - 1, i)[static_cast<std::size_t>(j)];
            if (d < -kWrapPointEpsilon || d > kWrapPointEpsilon) { break; }
        }
        if (j != 3) { break; }
    }
    g.rundBreite = i == g.hoehe;
}
void spaltenTeilen(Gitter& g) {   // CM_SubdivideGridColumns
    for (int i = 0; i < g.breite - 2;) {
        int j = 0;
        for (; j < g.hoehe; ++j) {
            if (brauchtTeilung(g.at(i, j), g.at(i + 1, j), g.at(i + 2, j))) { break; }
        }
        if (j == g.hoehe) {
            for (j = 0; j < g.hoehe; ++j) {
                for (int k = i + 2; k < g.breite; ++k) { g.at(k - 1, j) = g.at(k, j); }
            }
            g.breite--;
            i++;
            continue;
        }
        for (j = 0; j < g.hoehe; ++j) {
            const V3 prev = g.at(i, j);
            const V3 mid = g.at(i + 1, j);
            const V3 next = g.at(i + 2, j);
            for (int k = g.breite - 1; k > i + 1; --k) { g.at(k + 2, j) = g.at(k, j); }
            teilen(prev, mid, next, g.at(i + 1, j), g.at(i + 2, j), g.at(i + 3, j));
        }
        g.breite += 2;
    }
}
bool punkteGleich(const V3& a, const V3& b) {   // CM_ComparePoints
    for (std::size_t i = 0; i < 3; ++i) {
        const float d = a[i] - b[i];
        if (d < -kPointEpsilon || d > kPointEpsilon) { return false; }
    }
    return true;
}
void doppelteSpaltenWeg(Gitter& g) {   // CM_RemoveDegenerateColumns
    for (int i = 0; i < g.breite - 1; ++i) {
        int j = 0;
        for (; j < g.hoehe; ++j) {
            if (!punkteGleich(g.at(i, j), g.at(i + 1, j))) { break; }
        }
        if (j != g.hoehe) { continue; }
        for (j = 0; j < g.hoehe; ++j) {
            for (int k = i + 2; k < g.breite; ++k) { g.at(k - 1, j) = g.at(k, j); }
        }
        g.breite--;
        i--;
    }
}

// --- cm_patch.cpp: Ebenen und Facetten ---
struct PatchBau {
    std::vector<KollisionsKarte::PatchEbene> ebenen;
    std::vector<KollisionsKarte::Facette> facetten;
    std::vector<int> gitterEbenen = std::vector<int>(static_cast<std::size_t>(kMaxGrid * kMaxGrid * 2), -1);
    int& ge(int i, int j, int tri) { return gitterEbenen[static_cast<std::size_t>((i * kMaxGrid + j) * 2 + tri)]; }
};
int vorzeichenBits(const float* n) {   // CM_SignbitsForNormal
    int bits = 0;
    for (int j = 0; j < 3; ++j) {
        if (n[j] < 0) { bits |= 1 << j; }
    }
    return bits;
}
bool ebeneAusPunkten(float plane[4], const V3& a, const V3& b, const V3& c) {   // CM_PlaneFromPoints
    float d1[3];
    float d2[3];
    for (std::size_t i = 0; i < 3; ++i) {
        d1[i] = b[i] - a[i];
        d2[i] = c[i] - a[i];
    }
    vKreuz(d2, d1, plane);
    if (vNorm(plane) == 0.0F) { return false; }
    plane[3] = punkt(a.data(), plane);
    return true;
}
bool ebeneGleich(const KollisionsKarte::PatchEbene& p, const float plane[4], int* flipped) {   // CM_PlaneEqual
    if (std::fabs(p.ebene[0] - plane[0]) < kNormalEpsilon && std::fabs(p.ebene[1] - plane[1]) < kNormalEpsilon &&
        std::fabs(p.ebene[2] - plane[2]) < kNormalEpsilon && std::fabs(p.ebene[3] - plane[3]) < kDistEpsilon) {
        *flipped = 0;
        return true;
    }
    const float inv[4] = {-plane[0], -plane[1], -plane[2], -plane[3]};
    if (std::fabs(p.ebene[0] - inv[0]) < kNormalEpsilon && std::fabs(p.ebene[1] - inv[1]) < kNormalEpsilon &&
        std::fabs(p.ebene[2] - inv[2]) < kNormalEpsilon && std::fabs(p.ebene[3] - inv[3]) < kDistEpsilon) {
        *flipped = 1;
        return true;
    }
    return false;
}
void vektorEinrasten(float* n) {   // CM_SnapVector
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(n[i] - 1) < kNormalEpsilon) {
            n[0] = n[1] = n[2] = 0.0F;
            n[i] = 1;
            break;
        }
        if (std::fabs(n[i] - -1) < kNormalEpsilon) {
            n[0] = n[1] = n[2] = 0.0F;
            n[i] = -1;
            break;
        }
    }
}
int neueEbene(PatchBau& pb, const float plane[4]) {
    if (static_cast<int>(pb.ebenen.size()) >= kMaxPatchPlanes) { return -1; }   // Com_Error MAX_PATCH_PLANES
    KollisionsKarte::PatchEbene e;
    for (int i = 0; i < 4; ++i) { e.ebene[i] = plane[i]; }
    e.vorzeichen = vorzeichenBits(plane);
    pb.ebenen.push_back(e);
    return static_cast<int>(pb.ebenen.size()) - 1;
}
int findeEbene2(PatchBau& pb, const float plane[4], int* flipped) {   // CM_FindPlane2
    for (std::size_t i = 0; i < pb.ebenen.size(); ++i) {
        if (ebeneGleich(pb.ebenen[i], plane, flipped)) { return static_cast<int>(i); }
    }
    *flipped = 0;
    return neueEbene(pb, plane);
}
int findeEbene(PatchBau& pb, const V3& p1, const V3& p2, const V3& p3) {   // CM_FindPlane
    float plane[4];
    if (!ebeneAusPunkten(plane, p1, p2, p3)) { return -1; }
    for (std::size_t i = 0; i < pb.ebenen.size(); ++i) {
        const float* e = pb.ebenen[i].ebene;
        if (punkt(plane, e) < 0) { continue; }
        float d = punkt(p1.data(), e) - e[3];
        if (d < -kPlaneTriEpsilon || d > kPlaneTriEpsilon) { continue; }
        d = punkt(p2.data(), e) - e[3];
        if (d < -kPlaneTriEpsilon || d > kPlaneTriEpsilon) { continue; }
        d = punkt(p3.data(), e) - e[3];
        if (d < -kPlaneTriEpsilon || d > kPlaneTriEpsilon) { continue; }
        return static_cast<int>(i);
    }
    return neueEbene(pb, plane);
}
int punktSeite(const PatchBau& pb, const V3& p, int nr) {   // CM_PointOnPlaneSide
    if (nr == -1) { return kSeiteAuf; }
    const float* e = pb.ebenen[static_cast<std::size_t>(nr)].ebene;
    const float d = punkt(p.data(), e) - e[3];
    if (d > kPlaneTriEpsilon) { return kSeiteVorn; }
    if (d < -kPlaneTriEpsilon) { return kSeiteHinten; }
    return kSeiteAuf;
}
int gitterEbene(PatchBau& pb, int i, int j, int tri) {   // CM_GridPlane
    int p = pb.ge(i, j, tri);
    if (p != -1) { return p; }
    p = pb.ge(i, j, tri == 0 ? 1 : 0);
    return p;   // sonst "CM_GridPlane unresolvable": -1
}
int kantenEbene(PatchBau& pb, Gitter& g, int i, int j, int k) {   // CM_EdgePlaneNum
    V3 up{};
    const auto hoch = [&](const V3& p1, int pn) {
        const float* e = pb.ebenen[static_cast<std::size_t>(pn)].ebene;
        for (std::size_t q = 0; q < 3; ++q) { up[q] = p1[q] + 4 * e[q]; }
    };
    int p = 0;
    switch (k) {
        case 0: {
            const V3 p1 = g.at(i, j);
            const V3 p2 = g.at(i + 1, j);
            p = gitterEbene(pb, i, j, 0);
            if (p == -1) { return -1; }
            hoch(p1, p);
            return findeEbene(pb, p1, p2, up);
        }
        case 2: {
            const V3 p1 = g.at(i, j + 1);
            const V3 p2 = g.at(i + 1, j + 1);
            p = gitterEbene(pb, i, j, 1);
            if (p == -1) { return -1; }
            hoch(p1, p);
            return findeEbene(pb, p2, p1, up);
        }
        case 3: {
            const V3 p1 = g.at(i, j);
            const V3 p2 = g.at(i, j + 1);
            p = gitterEbene(pb, i, j, 1);
            if (p == -1) { return -1; }
            hoch(p1, p);
            return findeEbene(pb, p2, p1, up);
        }
        case 1: {
            const V3 p1 = g.at(i + 1, j);
            const V3 p2 = g.at(i + 1, j + 1);
            p = gitterEbene(pb, i, j, 0);
            if (p == -1) { return -1; }
            hoch(p1, p);
            return findeEbene(pb, p1, p2, up);
        }
        case 4: {
            const V3 p1 = g.at(i + 1, j + 1);
            const V3 p2 = g.at(i, j);
            p = gitterEbene(pb, i, j, 0);
            if (p == -1) { return -1; }
            hoch(p1, p);
            return findeEbene(pb, p1, p2, up);
        }
        case 5: {
            const V3 p1 = g.at(i, j);
            const V3 p2 = g.at(i + 1, j + 1);
            p = gitterEbene(pb, i, j, 1);
            if (p == -1) { return -1; }
            hoch(p1, p);
            return findeEbene(pb, p1, p2, up);
        }
        default: break;
    }
    return -1;
}
void randInnen(PatchBau& pb, KollisionsKarte::Facette& f, Gitter& g, int i, int j, int which) {   // CM_SetBorderInward
    V3 pts[4];
    int n = 0;
    if (which == -1) {
        pts[0] = g.at(i, j);
        pts[1] = g.at(i + 1, j);
        pts[2] = g.at(i + 1, j + 1);
        pts[3] = g.at(i, j + 1);
        n = 4;
    } else if (which == 0) {
        pts[0] = g.at(i, j);
        pts[1] = g.at(i + 1, j);
        pts[2] = g.at(i + 1, j + 1);
        n = 3;
    } else {
        pts[0] = g.at(i + 1, j + 1);
        pts[1] = g.at(i, j + 1);
        pts[2] = g.at(i, j);
        n = 3;
    }
    for (int k = 0; k < f.zahl; ++k) {
        int front = 0;
        int back = 0;
        for (int l = 0; l < n; ++l) {
            const int side = punktSeite(pb, pts[l], f.raender[k]);
            if (side == kSeiteVorn) { front++; }
            if (side == kSeiteHinten) { back++; }
        }
        if (front != 0 && back == 0) {
            f.innen[k] = true;
        } else if (back != 0 && front == 0) {
            f.innen[k] = false;
        } else if (front == 0 && back == 0) {
            f.raender[k] = -1;
        } else {
            f.innen[k] = false;   // "mixed plane sides"
        }
    }
}
bool facetteGueltig(const PatchBau& pb, const KollisionsKarte::Facette& f) {   // CM_ValidateFacet
    if (f.flaeche == -1) { return false; }
    float plane[4];
    for (int q = 0; q < 4; ++q) { plane[q] = pb.ebenen[static_cast<std::size_t>(f.flaeche)].ebene[q]; }
    Wicklung w = grundWicklung(plane, plane[3]);
    bool da = true;
    for (int j = 0; j < f.zahl && da; ++j) {
        if (f.raender[j] == -1) { return false; }
        for (int q = 0; q < 4; ++q) { plane[q] = pb.ebenen[static_cast<std::size_t>(f.raender[j])].ebene[q]; }
        if (!f.innen[j]) {
            for (int q = 0; q < 3; ++q) { plane[q] = 0.0F - plane[q]; }
            plane[3] = -plane[3];
        }
        da = wicklungSchneiden(w, plane, plane[3], 0.1F);
    }
    if (!da) { return false; }
    float b0[3];
    float b1[3];
    wicklungGrenzen(w, b0, b1);
    for (int j = 0; j < 3; ++j) {
        if (b1[j] - b0[j] > kMaxMapBounds) { return false; }
        if (b0[j] >= kMaxMapBounds) { return false; }
        if (b1[j] <= -kMaxMapBounds) { return false; }
    }
    return true;
}
void facettenKanten(PatchBau& pb, KollisionsKarte::Facette& f) {   // CM_AddFacetBevels (ADDBEVELS)
    constexpr int kMaxRand = 4 + 6 + 16;
    float plane[4];
    int flipped = 0;
    for (int q = 0; q < 4; ++q) { plane[q] = pb.ebenen[static_cast<std::size_t>(f.flaeche)].ebene[q]; }
    Wicklung w = grundWicklung(plane, plane[3]);
    bool da = true;
    for (int j = 0; j < f.zahl && da; ++j) {
        if (f.raender[j] == f.flaeche) { continue; }
        for (int q = 0; q < 4; ++q) { plane[q] = pb.ebenen[static_cast<std::size_t>(f.raender[j])].ebene[q]; }
        if (!f.innen[j]) {
            for (int q = 0; q < 3; ++q) { plane[q] = 0.0F - plane[q]; }
            plane[3] = -plane[3];
        }
        da = wicklungSchneiden(w, plane, plane[3], 0.1F);
    }
    if (!da) { return; }
    float mins[3];
    float maxs[3];
    wicklungGrenzen(w, mins, maxs);
    // Achsparallele Kanten
    for (int axis = 0; axis < 3; ++axis) {
        for (int dir = -1; dir <= 1; dir += 2) {
            plane[0] = plane[1] = plane[2] = 0.0F;
            plane[axis] = static_cast<float>(dir);
            plane[3] = dir == 1 ? maxs[axis] : -mins[axis];
            if (ebeneGleich(pb.ebenen[static_cast<std::size_t>(f.flaeche)], plane, &flipped)) { continue; }
            int i = 0;
            for (; i < f.zahl; ++i) {
                if (ebeneGleich(pb.ebenen[static_cast<std::size_t>(f.raender[i])], plane, &flipped)) { break; }
            }
            if (i == f.zahl && f.zahl < kMaxRand) {
                f.raender[f.zahl] = findeEbene2(pb, plane, &flipped);
                f.nichtAnpassen[f.zahl] = false;
                f.innen[f.zahl] = flipped != 0;
                f.zahl++;
            }
        }
    }
    // Kanten an den Raendern
    const std::size_t np = w.p.size();
    for (std::size_t j = 0; j < np; ++j) {
        const std::size_t k1 = (j + 1) % np;
        float vec[3];
        for (std::size_t q = 0; q < 3; ++q) { vec[q] = w.p[j][q] - w.p[k1][q]; }
        if (vNorm(vec) < 0.5F) { continue; }
        vektorEinrasten(vec);
        int k = 0;
        for (; k < 3; ++k) {
            if (vec[k] == -1 || vec[k] == 1) { break; }
        }
        if (k < 3) { continue; }
        for (int axis = 0; axis < 3; ++axis) {
            for (int dir = -1; dir <= 1; dir += 2) {
                float vec2[3] = {0.0F, 0.0F, 0.0F};
                vec2[axis] = static_cast<float>(dir);
                vKreuz(vec, vec2, plane);
                if (vNorm(plane) < 0.5F) { continue; }
                plane[3] = punkt(w.p[j].data(), plane);
                std::size_t l = 0;
                for (; l < np; ++l) {
                    const float d = punkt(w.p[l].data(), plane) - plane[3];
                    if (d > 0.1F) { break; }
                }
                if (l < np) { continue; }
                if (ebeneGleich(pb.ebenen[static_cast<std::size_t>(f.flaeche)], plane, &flipped)) { continue; }
                int i = 0;
                for (; i < f.zahl; ++i) {
                    if (ebeneGleich(pb.ebenen[static_cast<std::size_t>(f.raender[i])], plane, &flipped)) { break; }
                }
                if (i == f.zahl && f.zahl < kMaxRand) {
                    f.raender[f.zahl] = findeEbene2(pb, plane, &flipped);
                    f.nichtAnpassen[f.zahl] = false;
                    f.innen[f.zahl] = flipped != 0;
                    Wicklung w2 = w;
                    float neu[4];
                    for (int q = 0; q < 4; ++q) { neu[q] = pb.ebenen[static_cast<std::size_t>(f.raender[f.zahl])].ebene[q]; }
                    if (!f.innen[f.zahl]) {
                        for (int q = 0; q < 3; ++q) { neu[q] = -neu[q]; }
                        neu[3] = -neu[3];
                    }
                    if (!wicklungSchneiden(w2, neu, neu[3], 0.1F)) { continue; }   // "invalid bevel"
                    f.zahl++;
                }
            }
        }
    }
    // #ifndef BSPC: die Flaeche selbst als letzter Rand
    if (f.zahl < kMaxRand) {
        f.raender[f.zahl] = f.flaeche;
        f.nichtAnpassen[f.zahl] = false;
        f.innen[f.zahl] = true;
        f.zahl++;
    }
}
void patchAusGitter(Gitter& g, KollisionsKarte::Patch& pf) {   // CM_PatchCollideFromGrid
    PatchBau pb;
    enum { kOben = 0, kRechts = 1, kUnten = 2, kLinks = 3 };
    for (int i = 0; i < g.breite - 1; ++i) {
        for (int j = 0; j < g.hoehe - 1; ++j) {
            pb.ge(i, j, 0) = findeEbene(pb, g.at(i, j), g.at(i + 1, j), g.at(i + 1, j + 1));
            pb.ge(i, j, 1) = findeEbene(pb, g.at(i + 1, j + 1), g.at(i, j + 1), g.at(i, j));
        }
    }
    for (int i = 0; i < g.breite - 1; ++i) {
        for (int j = 0; j < g.hoehe - 1; ++j) {
            int r[4] = {-1, -1, -1, -1};
            bool na[4] = {false, false, false, false};
            if (j > 0) {
                r[kOben] = pb.ge(i, j - 1, 1);
            } else if (g.rundHoehe) {
                r[kOben] = pb.ge(i, g.hoehe - 2, 1);
            }
            na[kOben] = r[kOben] == pb.ge(i, j, 0);
            if (r[kOben] == -1 || na[kOben]) { r[kOben] = kantenEbene(pb, g, i, j, 0); }
            if (j < g.hoehe - 2) {
                r[kUnten] = pb.ge(i, j + 1, 0);
            } else if (g.rundHoehe) {
                r[kUnten] = pb.ge(i, 0, 0);
            }
            na[kUnten] = r[kUnten] == pb.ge(i, j, 1);
            if (r[kUnten] == -1 || na[kUnten]) { r[kUnten] = kantenEbene(pb, g, i, j, 2); }
            if (i > 0) {
                r[kLinks] = pb.ge(i - 1, j, 0);
            } else if (g.rundBreite) {
                r[kLinks] = pb.ge(g.breite - 2, j, 0);
            }
            na[kLinks] = r[kLinks] == pb.ge(i, j, 1);
            if (r[kLinks] == -1 || na[kLinks]) { r[kLinks] = kantenEbene(pb, g, i, j, 3); }
            if (i < g.breite - 2) {
                r[kRechts] = pb.ge(i + 1, j, 1);
            } else if (g.rundBreite) {
                r[kRechts] = pb.ge(0, j, 1);
            }
            na[kRechts] = r[kRechts] == pb.ge(i, j, 0);
            if (r[kRechts] == -1 || na[kRechts]) { r[kRechts] = kantenEbene(pb, g, i, j, 1); }
            if (static_cast<int>(pb.facetten.size()) >= kMaxFacets) { break; }   // Com_Error MAX_FACETS
            KollisionsKarte::Facette f;
            if (pb.ge(i, j, 0) == pb.ge(i, j, 1)) {
                if (pb.ge(i, j, 0) == -1) { continue; }
                f.flaeche = pb.ge(i, j, 0);
                f.zahl = 4;
                f.raender[0] = r[kOben];
                f.nichtAnpassen[0] = na[kOben];
                f.raender[1] = r[kRechts];
                f.nichtAnpassen[1] = na[kRechts];
                f.raender[2] = r[kUnten];
                f.nichtAnpassen[2] = na[kUnten];
                f.raender[3] = r[kLinks];
                f.nichtAnpassen[3] = na[kLinks];
                randInnen(pb, f, g, i, j, -1);
                if (facetteGueltig(pb, f)) {
                    facettenKanten(pb, f);
                    pb.facetten.push_back(f);
                }
            } else {
                f.flaeche = pb.ge(i, j, 0);
                f.zahl = 3;
                f.raender[0] = r[kOben];
                f.nichtAnpassen[0] = na[kOben];
                f.raender[1] = r[kRechts];
                f.nichtAnpassen[1] = na[kRechts];
                f.raender[2] = pb.ge(i, j, 1);
                if (f.raender[2] == -1) {
                    f.raender[2] = r[kUnten];
                    if (f.raender[2] == -1) { f.raender[2] = kantenEbene(pb, g, i, j, 4); }
                }
                randInnen(pb, f, g, i, j, 0);
                if (facetteGueltig(pb, f)) {
                    facettenKanten(pb, f);
                    pb.facetten.push_back(f);
                }
                if (static_cast<int>(pb.facetten.size()) >= kMaxFacets) { break; }
                KollisionsKarte::Facette f2;
                f2.flaeche = pb.ge(i, j, 1);
                f2.zahl = 3;
                f2.raender[0] = r[kUnten];
                f2.nichtAnpassen[0] = na[kUnten];
                f2.raender[1] = r[kLinks];
                f2.nichtAnpassen[1] = na[kLinks];
                f2.raender[2] = pb.ge(i, j, 0);
                if (f2.raender[2] == -1) {
                    f2.raender[2] = r[kOben];
                    if (f2.raender[2] == -1) { f2.raender[2] = kantenEbene(pb, g, i, j, 5); }
                }
                randInnen(pb, f2, g, i, j, 1);
                if (facetteGueltig(pb, f2)) {
                    facettenKanten(pb, f2);
                    pb.facetten.push_back(f2);
                }
            }
        }
    }
    pf.ebenen = std::move(pb.ebenen);
    pf.facetten = std::move(pb.facetten);
}
// CM_GeneratePatchCollide. false: ungueltige Groesse (Com_Error).
bool erzeugePatch(int width, int height, const std::vector<V3>& points, KollisionsKarte::Patch& pf) {
    if (width <= 2 || height <= 2 || (width & 1) == 0 || (height & 1) == 0 || width > kMaxGrid || height > kMaxGrid) {
        return false;
    }
    auto g = std::make_unique<Gitter>();
    g->breite = width;
    g->hoehe = height;
    for (int i = 0; i < width; ++i) {
        for (int j = 0; j < height; ++j) { g->at(i, j) = points[static_cast<std::size_t>(j * width + i)]; }
    }
    gitterRund(*g);
    spaltenTeilen(*g);
    doppelteSpaltenWeg(*g);
    gitterTauschen(*g);
    gitterRund(*g);
    spaltenTeilen(*g);
    doppelteSpaltenWeg(*g);
    for (int a = 0; a < 3; ++a) {
        pf.mins[a] = 99999.0F;   // ClearBounds
        pf.maxs[a] = -99999.0F;
    }
    for (int i = 0; i < g->breite; ++i) {
        for (int j = 0; j < g->hoehe; ++j) {
            for (int a = 0; a < 3; ++a) {
                const float v = g->at(i, j)[static_cast<std::size_t>(a)];
                if (v < pf.mins[a]) { pf.mins[a] = v; }
                if (v > pf.maxs[a]) { pf.maxs[a] = v; }
            }
        }
    }
    patchAusGitter(*g, pf);
    for (int a = 0; a < 3; ++a) {
        pf.mins[a] -= 1.0F;
        pf.maxs[a] += 1.0F;
    }
    return true;
}

// --- cm_patch.cpp: Spuren ---
// CM_TracePointThroughPatchCollide
void punktDurchPatch(Arbeit& tw, const KollisionsKarte::Patch& pc) {
    if (pc.facetten.empty()) { return; }
    thread_local std::vector<char> vorn;
    thread_local std::vector<float> schnitt;
    vorn.assign(pc.ebenen.size(), 0);
    schnitt.assign(pc.ebenen.size(), 0.0F);
    for (std::size_t i = 0; i < pc.ebenen.size(); ++i) {
        const KollisionsKarte::PatchEbene& p = pc.ebenen[i];
        const float offset = punkt(tw.offsets[p.vorzeichen], p.ebene);
        const float d1 = punkt(tw.start, p.ebene) - p.ebene[3] + offset;
        const float d2 = punkt(tw.end, p.ebene) - p.ebene[3] + offset;
        vorn[i] = d1 > 0.0F ? 1 : 0;
        if (d1 == d2) {
            schnitt[i] = kWorldSize;
        } else {
            const float frac = d1 / (d1 - d2);
            schnitt[i] = frac > 0.0F ? frac : kWorldSize;
        }
    }
    for (const KollisionsKarte::Facette& f : pc.facetten) {
        if (vorn[static_cast<std::size_t>(f.flaeche)] == 0) { continue; }
        const float s = schnitt[static_cast<std::size_t>(f.flaeche)];
        if (s < 0.0F) { continue; }
        if (s > tw.trace.fraction) { continue; }
        int j = 0;
        for (; j < f.zahl; ++j) {
            const int k = f.raender[j];
            if (((vorn[static_cast<std::size_t>(k)] != 0) ^ f.innen[j]) != 0) {
                if (schnitt[static_cast<std::size_t>(k)] > s) { break; }
            } else {
                if (schnitt[static_cast<std::size_t>(k)] < s) { break; }
            }
        }
        if (j == f.zahl) {
            const KollisionsKarte::PatchEbene& p = pc.ebenen[static_cast<std::size_t>(f.flaeche)];
            const float offset = punkt(tw.offsets[p.vorzeichen], p.ebene);
            const float d1 = punkt(tw.start, p.ebene) - p.ebene[3] + offset;
            const float d2 = punkt(tw.end, p.ebene) - p.ebene[3] + offset;
            tw.trace.fraction = (d1 - kSurfaceClipEpsilon) / (d1 - d2);
            if (tw.trace.fraction < 0.0F) { tw.trace.fraction = 0.0F; }
            for (std::size_t q = 0; q < 3; ++q) { tw.trace.normale[q] = p.ebene[q]; }
        }
    }
}
// CM_CheckFacetPlane
bool facettenEbene(const float* plane, const float* start, const float* end, float* enter, float* leave, bool* hit) {
    *hit = false;
    const float d1 = punkt(start, plane) - plane[3];
    const float d2 = punkt(end, plane) - plane[3];
    if (d1 > 0 && (d2 >= kSurfaceClipEpsilon || d2 >= d1)) { return false; }
    if (d1 <= 0 && d2 <= 0) { return true; }
    if (d1 > d2) {
        float f = (d1 - kSurfaceClipEpsilon) / (d1 - d2);
        if (f < 0) { f = 0; }
        if (f > *enter) {
            *enter = f;
            *hit = true;
        }
    } else {
        float f = (d1 + kSurfaceClipEpsilon) / (d1 - d2);
        if (f > 1) { f = 1; }
        if (f < *leave) { *leave = f; }
    }
    return true;
}
// CM_TraceThroughPatchCollide (mit ADDBEVELS)
void durchPatch(Arbeit& tw, const KollisionsKarte::Patch& pc) {
    for (int i = 0; i < 3; ++i) {
        if (tw.bounds[0][i] > pc.maxs[i] || tw.bounds[1][i] < pc.mins[i]) { return; }
    }
    if (tw.isPoint) {
        punktDurchPatch(tw, pc);
        return;
    }
    float best[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    for (const KollisionsKarte::Facette& f : pc.facetten) {
        float enter = -1.0F;
        float leave = 1.0F;
        int hitnum = -1;
        bool hit = false;
        const KollisionsKarte::PatchEbene& s = pc.ebenen[static_cast<std::size_t>(f.flaeche)];
        float plane[4] = {s.ebene[0], s.ebene[1], s.ebene[2], s.ebene[3]};
        plane[3] -= punkt(tw.offsets[s.vorzeichen], plane);
        if (!facettenEbene(plane, tw.start, tw.end, &enter, &leave, &hit)) { continue; }
        if (hit) {
            for (int q = 0; q < 4; ++q) { best[q] = plane[q]; }
        }
        int j = 0;
        for (; j < f.zahl; ++j) {
            const KollisionsKarte::PatchEbene& p = pc.ebenen[static_cast<std::size_t>(f.raender[j])];
            if (f.innen[j]) {
                plane[0] = -p.ebene[0];
                plane[1] = -p.ebene[1];
                plane[2] = -p.ebene[2];
                plane[3] = -p.ebene[3];
            } else {
                for (int q = 0; q < 4; ++q) { plane[q] = p.ebene[q]; }
            }
            const float offset = punkt(tw.offsets[p.vorzeichen], plane);
            plane[3] += std::fabs(offset);
            if (!facettenEbene(plane, tw.start, tw.end, &enter, &leave, &hit)) { break; }
            if (hit) {
                hitnum = j;
                for (int q = 0; q < 4; ++q) { best[q] = plane[q]; }
            }
        }
        if (j < f.zahl) { continue; }
        if (hitnum == f.zahl - 1) { continue; }   // "never clip against the back side"
        if (enter < leave && enter >= 0) {
            if (enter < tw.trace.fraction) {
                if (enter < 0) { enter = 0; }
                tw.trace.fraction = enter;
                for (std::size_t q = 0; q < 3; ++q) { tw.trace.normale[q] = best[q]; }
            }
        }
    }
}
// CM_PositionTestInPatchCollide
bool patchImKasten(Arbeit& tw, const KollisionsKarte::Patch& pc) {
    for (int i = 0; i < 3; ++i) {
        if (tw.bounds[0][i] > pc.maxs[i] || tw.bounds[1][i] < pc.mins[i]) { return false; }
    }
    thread_local std::vector<int> kreuz;
    kreuz.assign(pc.ebenen.size(), 0);
    for (std::size_t i = 0; i < pc.ebenen.size(); ++i) {
        const KollisionsKarte::PatchEbene& p = pc.ebenen[i];
        const float d = punkt(tw.start, p.ebene) - p.ebene[3];
        const float offset = std::fabs(punkt(tw.offsets[p.vorzeichen], p.ebene));
        kreuz[i] = d < -offset ? 0 : (d > offset ? 1 : 2);   // BOX_FRONT, BOX_BACK, BOX_CROSS
    }
    for (const KollisionsKarte::Facette& f : pc.facetten) {
        if (kreuz[static_cast<std::size_t>(f.flaeche)] != 2) { continue; }
        int j = 0;
        for (; j < f.zahl; ++j) {
            const int c = kreuz[static_cast<std::size_t>(f.raender[j])];
            if (c == 2) { continue; }
            if ((c ^ (f.innen[j] ? 1 : 0)) != 0) { break; }
        }
        if (j == f.zahl) { return true; }
    }
    return false;
}
// CM_TraceToLeaf / CM_TestInLeaf: die Patches eines Blatts (Flaechen-Indizes).
void patchesImBlatt(Arbeit& tw, int erste, int zahl, bool position) {
    for (int i = 0; i < zahl; ++i) {
        const int fl = tw.k->blattFlaechen[static_cast<std::size_t>(erste + i)];
        if (fl < 0 || fl >= static_cast<int>(tw.k->flaechePatch.size())) { continue; }
        const int pn = tw.k->flaechePatch[static_cast<std::size_t>(fl)];
        if (pn < 0) { continue; }
        std::uint32_t& s = tw.m->patchStand[static_cast<std::size_t>(pn)];
        if (s == tw.m->zaehler) { continue; }   // "already checked this patch in another leaf"
        s = tw.m->zaehler;
        const KollisionsKarte::Patch& p = tw.k->patches[static_cast<std::size_t>(pn)];
        if ((p.inhalt & tw.contents) == 0U) { continue; }
        if (position) {
            if (patchImKasten(tw, p)) {
                tw.trace.startsolid = tw.trace.allsolid = true;
                tw.trace.fraction = 0.0F;
                tw.trace.inhalt = p.inhalt;
                return;
            }
        } else {
            // CM_TraceThroughPatch
            const float alt = tw.trace.fraction;
            durchPatch(tw, p);
            if (tw.trace.fraction < alt) { tw.trace.inhalt = p.inhalt; tw.trace.flaeche = p.flaeche; }   // rc601 saber: surfaceFlags
            if (tw.trace.fraction == 0.0F) { return; }
        }
    }
}
// Die Patches einer Liste von Flaechen (Brush-Modell: firstSurface..).
void patchesDerFlaechen(Arbeit& tw, int erste, int zahl, bool position) {
    for (int i = 0; i < zahl; ++i) {
        const int fl = erste + i;
        if (fl < 0 || fl >= static_cast<int>(tw.k->flaechePatch.size())) { continue; }
        const int pn = tw.k->flaechePatch[static_cast<std::size_t>(fl)];
        if (pn < 0) { continue; }
        std::uint32_t& s = tw.m->patchStand[static_cast<std::size_t>(pn)];
        if (s == tw.m->zaehler) { continue; }
        s = tw.m->zaehler;
        const KollisionsKarte::Patch& p = tw.k->patches[static_cast<std::size_t>(pn)];
        if ((p.inhalt & tw.contents) == 0U) { continue; }
        if (position) {
            if (patchImKasten(tw, p)) {
                tw.trace.startsolid = tw.trace.allsolid = true;
                tw.trace.fraction = 0.0F;
                tw.trace.inhalt = p.inhalt;
                return;
            }
        } else {
            const float alt = tw.trace.fraction;
            durchPatch(tw, p);
            if (tw.trace.fraction < alt) { tw.trace.inhalt = p.inhalt; tw.trace.flaeche = p.flaeche; }   // rc601 saber: surfaceFlags
            if (tw.trace.fraction == 0.0F) { return; }
        }
    }
}

void bisBlatt(Arbeit& tw, int erste, int zahl, bool position) {
    // CM_TraceToLeaf / CM_TestInLeaf (cm_trace.cpp:553, 245) - nur Brushes
    for (int i = 0; i < zahl; ++i) {
        const int nr = tw.k->blattPinsel[static_cast<std::size_t>(erste + i)];
        if (nr < 0 || nr >= static_cast<int>(tw.k->pinsel.size())) { continue; }
        if (schonGeprueft(tw, nr)) { continue; }
        const KollisionsKarte::Pinsel& b = tw.k->pinsel[static_cast<std::size_t>(nr)];
        if ((b.inhalt & tw.contents) == 0U) { continue; }
        if (position) {
            kastenImPinsel(tw, b);
            if (tw.trace.allsolid) { return; }
        } else {
            durchPinsel(tw, b);
            if (tw.trace.fraction == 0.0F) { return; }
        }
    }
}

// CM_TraceThroughTree (cm_trace.cpp:629). Movie Duels setzt fuer nicht
// achsparallele Ebenen bei einem Kasten offset = 2048 (statt maxOffset).
// rc601 (bewegung, teil 3): OpenJK cm_trace.cpp:660-661 tut dasselbe (maxOffset,
// dann 2048) - kein Unterschied der Regelwerke.
void durchBaum(Arbeit& tw, int num, float p1f, float p2f, const float p1[3], const float p2[3]) {
    if (tw.trace.fraction <= p1f) { return; }
    if (num < 0) {
        const KollisionsKarte::Blatt& bl = tw.k->blaetter[static_cast<std::size_t>(-1 - num)];
        bisBlatt(tw, bl.erstePinsel, bl.pinsel, false);
        // rc601 (bewegung, teil 3): "trace against all patches in the leaf"
        if (tw.trace.fraction != 0.0F) { patchesImBlatt(tw, bl.ersteFlaeche, bl.flaechen, false); }
        return;
    }
    const KollisionsKarte::Knoten& node = tw.k->knoten[static_cast<std::size_t>(num)];
    const KollisionsKarte::Ebene& plane = tw.k->ebenen[static_cast<std::size_t>(node.ebene)];
    float t1 = 0.0F;
    float t2 = 0.0F;
    float offset = 0.0F;
    if (plane.typ < 3) {
        t1 = p1[plane.typ] - plane.abstand;
        t2 = p2[plane.typ] - plane.abstand;
        offset = tw.extents[plane.typ];
    } else {
        t1 = punkt(plane.normale, p1) - plane.abstand;
        t2 = punkt(plane.normale, p2) - plane.abstand;
        offset = tw.isPoint ? 0.0F : 2048.0F;
    }
    if (t1 >= offset + 1 && t2 >= offset + 1) {
        durchBaum(tw, node.kinder[0], p1f, p2f, p1, p2);
        return;
    }
    if (t1 < -offset - 1 && t2 < -offset - 1) {
        durchBaum(tw, node.kinder[1], p1f, p2f, p1, p2);
        return;
    }
    int seite = 0;
    float frac = 0.0F;
    float frac2 = 0.0F;
    if (t1 < t2) {
        const float idist = 1.0F / (t1 - t2);
        seite = 1;
        frac2 = (t1 + offset + kSurfaceClipEpsilon) * idist;
        frac = (t1 - offset + kSurfaceClipEpsilon) * idist;
    } else if (t1 > t2) {
        const float idist = 1.0F / (t1 - t2);
        seite = 0;
        frac2 = (t1 - offset - kSurfaceClipEpsilon) * idist;
        frac = (t1 + offset + kSurfaceClipEpsilon) * idist;
    } else {
        seite = 0;
        frac = 1;
        frac2 = 0;
    }
    frac = std::clamp(frac, 0.0F, 1.0F);
    float midf = p1f + (p2f - p1f) * frac;
    float mid[3];
    for (int a = 0; a < 3; ++a) { mid[a] = p1[a] + frac * (p2[a] - p1[a]); }
    durchBaum(tw, node.kinder[seite], p1f, midf, p1, mid);
    frac2 = std::clamp(frac2, 0.0F, 1.0F);
    midf = p1f + (p2f - p1f) * frac2;
    for (int a = 0; a < 3; ++a) { mid[a] = p1[a] + frac2 * (p2[a] - p1[a]); }
    durchBaum(tw, node.kinder[seite ^ 1], midf, p2f, mid, p2);
}

// BoxOnPlaneSide (q_math.c)
int kastenSeite(const float mins[3], const float maxs[3], const KollisionsKarte::Ebene& p) {
    if (p.typ < 3) {
        if (p.abstand <= mins[p.typ]) { return 1; }
        if (p.abstand >= maxs[p.typ]) { return 2; }
        return 3;
    }
    float d1 = 0.0F;
    float d2 = 0.0F;
    for (int a = 0; a < 3; ++a) {
        if (p.normale[a] < 0) {
            d1 += p.normale[a] * mins[a];
            d2 += p.normale[a] * maxs[a];
        } else {
            d1 += p.normale[a] * maxs[a];
            d2 += p.normale[a] * mins[a];
        }
    }
    int s = 0;
    if (d1 >= p.abstand) { s = 1; }
    if (d2 < p.abstand) { s |= 2; }
    return s;
}
// CM_BoxLeafnums_r
void blaetterIm(const KollisionsKarte& k, const float mins[3], const float maxs[3], int num, std::vector<int>& aus) {
    while (true) {
        if (num < 0) {
            if (aus.size() < 1024) { aus.push_back(-1 - num); }
            return;
        }
        const KollisionsKarte::Knoten& n = k.knoten[static_cast<std::size_t>(num)];
        const int s = kastenSeite(mins, maxs, k.ebenen[static_cast<std::size_t>(n.ebene)]);
        if (s == 1) {
            num = n.kinder[0];
        } else if (s == 2) {
            num = n.kinder[1];
        } else {
            blaetterIm(k, mins, maxs, n.kinder[0], aus);
            num = n.kinder[1];
        }
    }
}

// CM_BoxTrace (cm_trace.cpp:794)
KollisionsSpur kastenSpur(const KollisionsKarte& k, const float start[3], const float ende[3], const float mins[3],
                          const float maxs[3], int modell, std::uint32_t maske) {
    Arbeit tw;
    tw.k = &k;
    Marken& m = marken(k);
    tw.m = &m;
    ++m.zaehler;
    tw.trace.fraction = 1.0F;
    if (k.knoten.empty()) {
        for (int a = 0; a < 3; ++a) { tw.trace.endpos[static_cast<std::size_t>(a)] = ende[a]; }
        return tw.trace;
    }
    tw.contents = maske;
    float off[3];
    for (int a = 0; a < 3; ++a) {
        off[a] = (mins[a] + maxs[a]) * 0.5F;
        tw.size[0][a] = mins[a] - off[a];
        tw.size[1][a] = maxs[a] - off[a];
        tw.start[a] = start[a] + off[a];
        tw.end[a] = ende[a] + off[a];
    }
    tw.maxOffset = tw.size[1][0] + tw.size[1][1] + tw.size[1][2];
    for (int s = 0; s < 8; ++s) {
        tw.offsets[s][0] = tw.size[(s & 1) != 0 ? 1 : 0][0];
        tw.offsets[s][1] = tw.size[(s & 2) != 0 ? 1 : 0][1];
        tw.offsets[s][2] = tw.size[(s & 4) != 0 ? 1 : 0][2];
    }
    for (int a = 0; a < 3; ++a) {
        if (tw.start[a] < tw.end[a]) {
            tw.bounds[0][a] = tw.start[a] + tw.size[0][a];
            tw.bounds[1][a] = tw.end[a] + tw.size[1][a];
        } else {
            tw.bounds[0][a] = tw.end[a] + tw.size[0][a];
            tw.bounds[1][a] = tw.start[a] + tw.size[1][a];
        }
    }
    const KollisionsKarte::Modell* mod =
        (modell > 0 && modell < static_cast<int>(k.modelle.size())) ? &k.modelle[static_cast<std::size_t>(modell)] : nullptr;
    // Ein Brush-Modell: alle seine Brushes in einem "Blatt" (CMod_LoadSubmodels).
    std::vector<int> eigene;
    const auto modellBlatt = [&](bool position) {
        for (int i = 0; i < mod->zahl; ++i) {
            const int nr = mod->erster + i;
            if (nr < 0 || nr >= static_cast<int>(k.pinsel.size())) { continue; }
            if (schonGeprueft(tw, nr)) { continue; }
            const KollisionsKarte::Pinsel& b = k.pinsel[static_cast<std::size_t>(nr)];
            if ((b.inhalt & tw.contents) == 0U) { continue; }
            if (position) {
                kastenImPinsel(tw, b);
                if (tw.trace.allsolid) { return; }
            } else {
                durchPinsel(tw, b);
                if (tw.trace.fraction == 0.0F) { return; }
            }
        }
        // rc601 (bewegung, teil 3): die Patches des Modells (CMod_LoadSubmodels
        // haengt seine Flaechen an das Blatt des Modells).
        patchesDerFlaechen(tw, mod->ersteFlaeche, mod->flaechenZahl, position);
    };
    if (start[0] == ende[0] && start[1] == ende[1] && start[2] == ende[2]) {
        if (mod != nullptr) {
            modellBlatt(true);
        } else {
            // CM_PositionTest
            float lo[3];
            float hi[3];
            for (int a = 0; a < 3; ++a) {
                lo[a] = tw.start[a] + tw.size[0][a] - 1.0F;
                hi[a] = tw.start[a] + tw.size[1][a] + 1.0F;
            }
            std::vector<int> blaetter;
            blaetterIm(k, lo, hi, 0, blaetter);
            ++m.zaehler;
            for (const int bl : blaetter) {
                const KollisionsKarte::Blatt& b = k.blaetter[static_cast<std::size_t>(bl)];
                bisBlatt(tw, b.erstePinsel, b.pinsel, true);
                if (tw.trace.allsolid) { break; }
                patchesImBlatt(tw, b.ersteFlaeche, b.flaechen, true);   // rc601 (bewegung, teil 3)
                if (tw.trace.allsolid) { break; }
            }
        }
    } else {
        if (tw.size[0][0] == 0 && tw.size[0][1] == 0 && tw.size[0][2] == 0) {
            tw.isPoint = true;
            tw.extents[0] = tw.extents[1] = tw.extents[2] = 0.0F;
        } else {
            tw.isPoint = false;
            for (int a = 0; a < 3; ++a) { tw.extents[a] = tw.size[1][a]; }
        }
        if (mod != nullptr) {
            modellBlatt(false);
        } else {
            durchBaum(tw, 0, 0.0F, 1.0F, tw.start, tw.end);
        }
    }
    if (tw.trace.fraction == 1.0F) {
        for (int a = 0; a < 3; ++a) { tw.trace.endpos[static_cast<std::size_t>(a)] = ende[a]; }
    } else {
        for (int a = 0; a < 3; ++a) {
            tw.trace.endpos[static_cast<std::size_t>(a)] = start[a] + tw.trace.fraction * (ende[a] - start[a]);
        }
    }
    return tw.trace;
}

}  // namespace

bool leseKollision(const std::string& b, KollisionsKarte& aus) {
    aus = KollisionsKarte{};
    if (b.size() < 8 + 18 * 8) { return false; }
    const std::string magic = b.substr(0, 4);
    if (magic != "RBSP" && magic != "IBSP" && magic != "FBSP") { return false; }
    const std::size_t seitenGroesse = magic == "IBSP" ? 8 : 12;
    const Lump sh = lump(b, kLumpShaders);
    for (std::size_t at = sh.ofs; at + 72 <= sh.ofs + sh.len; at += 72) {
        aus.shaderFlaeche.push_back(static_cast<std::uint32_t>(i32(b, at + 64)));
        aus.shaderInhalt.push_back(static_cast<std::uint32_t>(i32(b, at + 68)));
    }
    const Lump pl = lump(b, kLumpPlanes);
    for (std::size_t at = pl.ofs; at + 16 <= pl.ofs + pl.len; at += 16) {
        KollisionsKarte::Ebene e;
        int bits = 0;
        for (int j = 0; j < 3; ++j) {
            e.normale[j] = f32(b, at + static_cast<std::size_t>(j) * 4);
            if (e.normale[j] < 0) { bits |= 1 << j; }
        }
        e.abstand = f32(b, at + 12);
        // PlaneTypeForNormal
        e.typ = e.normale[0] == 1.0F ? 0 : (e.normale[1] == 1.0F ? 1 : (e.normale[2] == 1.0F ? 2 : 3));
        e.vorzeichen = bits;
        aus.ebenen.push_back(e);
    }
    const Lump bs = lump(b, kLumpBrushSides);
    for (std::size_t at = bs.ofs; at + seitenGroesse <= bs.ofs + bs.len; at += seitenGroesse) {
        KollisionsKarte::Seite s;
        s.ebene = i32(b, at);
        s.shader = i32(b, at + 4);
        if (s.ebene < 0 || s.ebene >= static_cast<int>(aus.ebenen.size())) { s.ebene = 0; }
        aus.seiten.push_back(s);
    }
    const Lump br = lump(b, kLumpBrushes);
    for (std::size_t at = br.ofs; at + 12 <= br.ofs + br.len; at += 12) {
        KollisionsKarte::Pinsel p;
        p.ersteSeite = i32(b, at);
        p.seiten = i32(b, at + 4);
        p.shader = i32(b, at + 8);
        if (p.ersteSeite < 0 || p.seiten < 0 || p.ersteSeite + p.seiten > static_cast<int>(aus.seiten.size()) ||
            p.shader < 0 || p.shader >= static_cast<int>(aus.shaderInhalt.size())) {
            p.seiten = 0;
        } else {
            p.inhalt = aus.shaderInhalt[static_cast<std::size_t>(p.shader)];
        }
        if (p.seiten >= 6) {
            // CM_BoundBrush (cm_load.cpp:271): die ersten sechs Seiten sind achsparallel
            const auto d = [&](int i) {
                return aus.ebenen[static_cast<std::size_t>(aus.seiten[static_cast<std::size_t>(p.ersteSeite + i)].ebene)].abstand;
            };
            p.mins[0] = -d(0);
            p.maxs[0] = d(1);
            p.mins[1] = -d(2);
            p.maxs[1] = d(3);
            p.mins[2] = -d(4);
            p.maxs[2] = d(5);
        } else {
            p.seiten = 0;
        }
        aus.pinsel.push_back(p);
    }
    const Lump nd = lump(b, kLumpNodes);
    for (std::size_t at = nd.ofs; at + 36 <= nd.ofs + nd.len; at += 36) {
        KollisionsKarte::Knoten n;
        n.ebene = i32(b, at);
        n.kinder[0] = i32(b, at + 4);
        n.kinder[1] = i32(b, at + 8);
        if (n.ebene < 0 || n.ebene >= static_cast<int>(aus.ebenen.size())) { n.ebene = 0; }
        aus.knoten.push_back(n);
    }
    const Lump lf = lump(b, kLumpLeafs);
    for (std::size_t at = lf.ofs; at + 48 <= lf.ofs + lf.len; at += 48) {
        KollisionsKarte::Blatt l;
        l.cluster = i32(b, at);
        l.erstePinsel = i32(b, at + 40);
        l.pinsel = i32(b, at + 44);
        l.ersteFlaeche = i32(b, at + 32);   // rc601 (bewegung, teil 3)
        l.flaechen = i32(b, at + 36);
        aus.blaetter.push_back(l);
    }
    const Lump lb = lump(b, kLumpLeafBrushes);
    for (std::size_t at = lb.ofs; at + 4 <= lb.ofs + lb.len; at += 4) { aus.blattPinsel.push_back(i32(b, at)); }
    for (KollisionsKarte::Blatt& l : aus.blaetter) {
        if (l.erstePinsel < 0 || l.pinsel < 0 || l.erstePinsel + l.pinsel > static_cast<int>(aus.blattPinsel.size())) {
            l.pinsel = 0;
        }
    }
    const Lump md = lump(b, kLumpModels);
    for (std::size_t at = md.ofs; at + 40 <= md.ofs + md.len; at += 40) {
        KollisionsKarte::Modell m;
        for (int j = 0; j < 3; ++j) {
            m.mins[j] = f32(b, at + static_cast<std::size_t>(j) * 4) - 1.0F;   // "spread the mins / maxs by a pixel"
            m.maxs[j] = f32(b, at + 12 + static_cast<std::size_t>(j) * 4) + 1.0F;
        }
        m.erster = i32(b, at + 32);
        m.zahl = i32(b, at + 36);
        m.ersteFlaeche = i32(b, at + 24);   // rc601 (bewegung, teil 3)
        m.flaechenZahl = i32(b, at + 28);
        aus.modelle.push_back(m);
    }
    // rc601 (bewegung, teil 3): CMod_LoadLeafSurfaces und CMod_LoadPatches
    // (cm_load.cpp:436-600 MD). dsurface_t ist in RBSP 148 Bytes, mapVert_t 80
    // (vier Lightmaps); in IBSP 104 und 44. MST_PATCH = 2.
    {
        const Lump ls = lump(b, kLumpLeafSurfaces);
        for (std::size_t at = ls.ofs; at + 4 <= ls.ofs + ls.len; at += 4) { aus.blattFlaechen.push_back(i32(b, at)); }
        for (KollisionsKarte::Blatt& l : aus.blaetter) {
            if (l.ersteFlaeche < 0 || l.flaechen < 0 || l.ersteFlaeche + l.flaechen > static_cast<int>(aus.blattFlaechen.size())) {
                l.flaechen = 0;
            }
        }
        const bool rbsp = magic != "IBSP";
        const std::size_t flGr = rbsp ? 148 : 104;
        const std::size_t ptGr = rbsp ? 80 : 44;
        const std::size_t breiteAb = rbsp ? 140 : 96;
        const Lump fl = lump(b, kLumpSurfaces);
        const Lump dv = lump(b, kLumpDrawVerts);
        const std::size_t punkte = dv.len / ptGr;
        for (std::size_t at = fl.ofs; at + flGr <= fl.ofs + fl.len; at += flGr) {
            aus.flaechePatch.push_back(-1);
            if (i32(b, at + 8) != 2) { continue; }   // MST_PATCH
            const int width = i32(b, at + breiteAb);
            const int height = i32(b, at + breiteAb + 4);
            const int erster = i32(b, at + 12);
            const int shader = i32(b, at);
            const int c = width * height;
            if (c <= 0 || c > kMaxPatchVerts || erster < 0 || static_cast<std::size_t>(erster + c) > punkte || shader < 0 ||
                shader >= static_cast<int>(aus.shaderInhalt.size())) {
                continue;
            }
            std::vector<V3> pts(static_cast<std::size_t>(c));
            for (int j = 0; j < c; ++j) {
                const std::size_t q = dv.ofs + static_cast<std::size_t>(erster + j) * ptGr;
                pts[static_cast<std::size_t>(j)] = {f32(b, q), f32(b, q + 4), f32(b, q + 8)};
            }
            KollisionsKarte::Patch p;
            p.inhalt = aus.shaderInhalt[static_cast<std::size_t>(shader)];
            p.flaeche = aus.shaderFlaeche[static_cast<std::size_t>(shader)];
            if (!erzeugePatch(width, height, pts, p)) { continue; }
            aus.flaechePatch.back() = static_cast<int>(aus.patches.size());
            aus.patches.push_back(std::move(p));
        }
    }
    // Kinder, die aus dem Baum zeigen, kappen.
    for (KollisionsKarte::Knoten& n : aus.knoten) {
        for (int& c : n.kinder) {
            if (c >= 0 && c >= static_cast<int>(aus.knoten.size())) { c = -1; }
            if (c < 0 && -1 - c >= static_cast<int>(aus.blaetter.size())) { c = aus.blaetter.empty() ? 0 : -1; }
        }
    }
    if (aus.blaetter.empty()) { aus.knoten.clear(); }
    return !aus.knoten.empty();
}

KollisionsSpur kollisionsSpur(const KollisionsKarte& k, const float start[3], const float mins[3], const float maxs[3],
                              const float ende[3], std::uint32_t maske, int modell, const float ursprung[3],
                              const float winkel[3]) {
    if (modell <= 0) { return kastenSpur(k, start, ende, mins, maxs, 0, maske); }
    // CM_TransformedBoxTrace (cm_trace.cpp:965)
    float off[3];
    float sym[2][3];
    float s[3];
    float e[3];
    for (int a = 0; a < 3; ++a) {
        off[a] = (mins[a] + maxs[a]) * 0.5F;
        sym[0][a] = mins[a] - off[a];
        sym[1][a] = maxs[a] - off[a];
        s[a] = start[a] + off[a] - (ursprung != nullptr ? ursprung[a] : 0.0F);
        e[a] = ende[a] + off[a] - (ursprung != nullptr ? ursprung[a] : 0.0F);
    }
    const bool gedreht = winkel != nullptr && (winkel[0] != 0.0F || winkel[1] != 0.0F || winkel[2] != 0.0F);
    float vorn[3];
    float rechts[3];
    float oben[3];
    if (gedreht) {
        winkelVektoren(winkel, vorn, rechts, oben);
        const float ts[3] = {s[0], s[1], s[2]};
        s[0] = punkt(ts, vorn);
        s[1] = -punkt(ts, rechts);
        s[2] = punkt(ts, oben);
        const float te[3] = {e[0], e[1], e[2]};
        e[0] = punkt(te, vorn);
        e[1] = -punkt(te, rechts);
        e[2] = punkt(te, oben);
    }
    KollisionsSpur t = kastenSpur(k, s, e, sym[0], sym[1], modell, maske);
    if (gedreht && t.fraction != 1.0F) {
        const float a[3] = {-winkel[0], -winkel[1], -winkel[2]};
        winkelVektoren(a, vorn, rechts, oben);
        const float n[3] = {t.normale[0], t.normale[1], t.normale[2]};
        t.normale[0] = punkt(n, vorn);
        t.normale[1] = -punkt(n, rechts);
        t.normale[2] = punkt(n, oben);
    }
    for (int a = 0; a < 3; ++a) {
        t.endpos[static_cast<std::size_t>(a)] = start[a] + t.fraction * (ende[a] - start[a]);
    }
    return t;
}


}  // namespace bhed
