// saberspuren.cpp - Paket saber (rc601, Review). Siehe bhed/saberspuren.h.
//
// Uebertragen Zeile fuer Zeile: R_MarkFragments, R_BoxSurfaces_r,
// R_AddMarkFragments, R_ChopPolyBehindPlane (rd-vanilla/tr_marks.cpp),
// R_SubdividePatchToGrid samt Hilfen (rd-vanilla/tr_curve.cpp), CG_AllocMark,
// CG_FreeMarkPoly, CG_AddMarks (cgame/cg_marks.cpp) und CG_CreateSaberMarks
// (cgame/cg_players.cpp). "OJ" = OpenJK code/, "MD" = MovieDuels-SJE-SP-Rend2,
// beide master; tr_marks.cpp und tr_curve.cpp sind in beiden gleich (nur
// Formatierung).
#include "bhed/saberspuren.h"

#include "bhed/bspgeo.h"
#include "bhed/saberkontakt.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bhed {

namespace {

using V3 = std::array<float, 3>;

float punkt(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void kreuz(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
// VectorNormalize (q_math.c:1141 MD): Laenge zurueck, Nullvektor bleibt.
float normieren(float v[3]) {
    float l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    l = std::sqrt(l);
    if (l != 0.0F) {
        const float il = 1 / l;
        v[0] *= il;
        v[1] *= il;
        v[2] *= il;
    }
    return l;
}
// VectorNormalize2
float normieren2(const float v[3], float o[3]) {
    float l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    l = std::sqrt(l);
    if (l != 0.0F) {
        const float il = 1 / l;
        o[0] = v[0] * il;
        o[1] = v[1] * il;
        o[2] = v[2] * il;
    } else {
        o[0] = o[1] = o[2] = 0.0F;
    }
    return l;
}
// VectorNormalizeFast (q_math.c:1147 OJ)
void normierenSchnell(float v[3]) {
    const float il = spurRsqrt(punkt(v, v));
    v[0] *= il;
    v[1] *= il;
    v[2] *= il;
}
// ProjectPointOnPlane + PerpendicularVector (q_math.c:671/1352 OJ)
void senkrecht(float dst[3], const float src[3]) {
    int pos = 0;
    float minelem = 1.0F;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(src[i]) < minelem) {
            pos = i;
            minelem = std::fabs(src[i]);
        }
    }
    float temp[3] = {0.0F, 0.0F, 0.0F};
    temp[pos] = 1.0F;
    float invDenom = punkt(src, src);
    invDenom = 1.0F / invDenom;
    const float d = punkt(src, temp) * invDenom;
    float n[3];
    for (int i = 0; i < 3; ++i) { n[i] = src[i] * invDenom; }
    for (int i = 0; i < 3; ++i) { dst[i] = temp[i] - d * n[i]; }
    normieren(dst);
}

// Q_flrand(min, max) aus der Saat - der n-te Wurf dieser Marke.
float flrand(std::uint64_t saat, int n, float lo, float hi) {
    const std::uint64_t h = saberSaat(saat, static_cast<std::uint64_t>(n), 0x666c72616e64ULL);
    const float r = static_cast<float>(h >> 40) / 16777216.0F;   // [0, 1)
    return lo + (hi - lo) * r;
}

// --- tr_marks.cpp -----------------------------------------------------------
constexpr int kMaxEckenR = 64;   // MAX_VERTS_ON_POLY im Renderer (tr_marks.cpp:27)
constexpr int kVorn = 0;         // SIDE_FRONT
constexpr int kHinten = 1;       // SIDE_BACK
constexpr int kAuf = 2;          // SIDE_ON

void hackePoly(int numIn, const float (*in)[3], int* numOut, float (*out)[3], const float normal[3], float dist,
               float epsilon) {
    float dists[kMaxEckenR + 4] = {0};
    int sides[kMaxEckenR + 4] = {0};
    int counts[3];
    if (numIn >= kMaxEckenR - 2) {
        *numOut = 0;
        return;
    }
    counts[0] = counts[1] = counts[2] = 0;
    int i = 0;
    for (i = 0; i < numIn; ++i) {
        float dot = punkt(in[i], normal);
        dot -= dist;
        dists[i] = dot;
        if (dot > epsilon) {
            sides[i] = kVorn;
        } else if (dot < -epsilon) {
            sides[i] = kHinten;
        } else {
            sides[i] = kAuf;
        }
        counts[sides[i]]++;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];
    *numOut = 0;
    if (!counts[0]) {
        return;
    }
    if (!counts[1]) {
        *numOut = numIn;
        std::memcpy(out, in, static_cast<std::size_t>(numIn) * sizeof(float[3]));
        return;
    }
    for (i = 0; i < numIn; ++i) {
        const float* p1 = in[i];
        float* clip = out[*numOut];
        if (sides[i] == kAuf) {
            std::memcpy(clip, p1, sizeof(float[3]));
            (*numOut)++;
            continue;
        }
        if (sides[i] == kVorn) {
            std::memcpy(clip, p1, sizeof(float[3]));
            (*numOut)++;
            clip = out[*numOut];
        }
        if (sides[i + 1] == kAuf || sides[i + 1] == sides[i]) {
            continue;
        }
        const float* p2 = in[(i + 1) % numIn];
        const float d = dists[i] - dists[i + 1];
        float dot = 0.0F;
        if (d == 0) {
            dot = 0;
        } else {
            dot = dists[i] / d;
        }
        for (int j = 0; j < 3; ++j) { clip[j] = p1[j] + dot * (p2[j] - p1[j]); }
        (*numOut)++;
    }
}

struct Sammlung {
    std::vector<int> liste;   // surfaces[64]
    float mins[3]{};
    float maxs[3]{};
    float richtung[3]{};
};

void kastenFlaechen(const SpurWelt& w, int knoten, Sammlung& s) {
    constexpr int kListe = 64;
    // while ( node->contents == -1 )
    while (knoten >= 0) {
        const SpurWelt::Knoten& n = w.knoten[static_cast<std::size_t>(knoten)];
        const int seite = spurKastenSeite(s.mins, s.maxs, w.ebenen[static_cast<std::size_t>(n.ebene)]);
        if (seite == 1) {
            knoten = n.kinder[0];
        } else if (seite == 2) {
            knoten = n.kinder[1];
        } else {
            kastenFlaechen(w, n.kinder[0], s);
            knoten = n.kinder[1];
        }
    }
    const int blatt = -(knoten + 1);
    if (blatt < 0 || static_cast<std::size_t>(blatt) >= w.blaetter.size()) {
        return;
    }
    const SpurWelt::Blatt& b = w.blaetter[static_cast<std::size_t>(blatt)];
    for (int c = 0; c < b.zahl; ++c) {
        if (static_cast<int>(s.liste.size()) >= kListe) {
            break;
        }
        const std::size_t li = static_cast<std::size_t>(b.erste + c);
        if (li >= w.blattFlaechen.size()) {
            break;
        }
        const int fi = w.blattFlaechen[li];
        if (fi < 0 || static_cast<std::size_t>(fi) >= w.flaechen.size()) {
            continue;
        }
        const SpurFlaeche& f = w.flaechen[static_cast<std::size_t>(fi)];
        int& gesehen = w.gesehen[static_cast<std::size_t>(fi)];
        if (f.ausgenommen) {
            gesehen = w.zaehler;
        } else if (f.art == SpurFlaeche::Art::Flaeche) {
            const int seite = spurKastenSeite(s.mins, s.maxs, f.ebene);
            if (seite == 1 || seite == 2) {
                gesehen = w.zaehler;
            } else if (punkt(f.ebene.normale, s.richtung) > -0.5) {
                gesehen = w.zaehler;
            }
        } else if (f.art != SpurFlaeche::Art::Gitter && f.art != SpurFlaeche::Art::Dreiecke) {
            gesehen = w.zaehler;
        }
        if (gesehen != w.zaehler) {
            gesehen = w.zaehler;
            s.liste.push_back(fi);
        }
    }
}

struct Ausgabe {
    int maxPunkte = 0;
    float (*puffer)[3] = nullptr;
    int maxBruch = 0;
    SpurBruchstueck* bruch = nullptr;
    int punkte = 0;
    int bruchstuecke = 0;
    // nur zum Vorsortieren (siehe spurBruchstuecke)
    float weitMin[3]{};
    float weitMax[3]{};
};

bool draussen(const float* const* ecken, int zahl, const Ausgabe& a) {
    for (int k = 0; k < 3; ++k) {
        float lo = ecken[0][k];
        float hi = lo;
        for (int j = 1; j < zahl; ++j) {
            lo = std::min(lo, ecken[j][k]);
            hi = std::max(hi, ecken[j][k]);
        }
        if (hi < a.weitMin[k] || lo > a.weitMax[k]) {
            return true;
        }
    }
    return false;
}

void bruchDazu(int numClip, float (*clip)[kMaxEckenR][3], int numPlanes, const float (*normals)[3],
               const float* dists, Ausgabe& a) {
    int pingPong = 0;
    for (int i = 0; i < numPlanes; ++i) {
        hackePoly(numClip, clip[pingPong], &numClip, clip[!pingPong], normals[i], dists[i], 0.5F);
        pingPong ^= 1;
        if (numClip == 0) {
            break;
        }
    }
    if (numClip == 0) {
        return;
    }
    if (numClip + a.punkte > a.maxPunkte) {
        return;   // not enough space for this polygon
    }
    SpurBruchstueck& mf = a.bruch[a.bruchstuecke];
    mf.ersterPunkt = a.punkte;
    mf.zahl = numClip;
    std::memcpy(a.puffer + a.punkte, clip[pingPong], static_cast<std::size_t>(numClip) * sizeof(float[3]));
    a.punkte += numClip;
    a.bruchstuecke++;
}

// --- tr_curve.cpp -----------------------------------------------------------
constexpr int kGitterMax = 65;   // MAX_GRID_SIZE (tr_local.h:593 OJ)
struct Ecke {
    float xyz[3];
};
using Gitter = Ecke[kGitterMax][kGitterMax];

void lerpEcke(const Ecke& a, const Ecke& b, Ecke& o) {
    // "out->xyz[0] = 0.5 * (a->xyz[0] + b->xyz[0]);" - in double gerechnet
    for (int k = 0; k < 3; ++k) { o.xyz[k] = static_cast<float>(0.5 * (a.xyz[k] + b.xyz[k])); }
}
void transponieren(int width, int height, Gitter& ctrl) {
    if (width > height) {
        for (int i = 0; i < height; ++i) {
            for (int j = i + 1; j < width; ++j) {
                if (j < height) {
                    const Ecke temp = ctrl[j][i];
                    ctrl[j][i] = ctrl[i][j];
                    ctrl[i][j] = temp;
                } else {
                    ctrl[j][i] = ctrl[i][j];
                }
            }
        }
    } else {
        for (int i = 0; i < width; ++i) {
            for (int j = i + 1; j < height; ++j) {
                if (j < width) {
                    const Ecke temp = ctrl[i][j];
                    ctrl[i][j] = ctrl[j][i];
                    ctrl[j][i] = temp;
                } else {
                    ctrl[i][j] = ctrl[j][i];
                }
            }
        }
    }
}
void umkehren(int width, int height, Gitter& ctrl) {
    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width / 2; ++j) {
            const Ecke temp = ctrl[i][j];
            ctrl[i][j] = ctrl[i][width - 1 - j];
            ctrl[i][width - 1 - j] = temp;
        }
    }
}
void fehlerUmkehren(float errorTable[2][kGitterMax], int width, int height) {
    float copy[2][kGitterMax];
    std::memcpy(copy, errorTable, sizeof(copy));
    for (int i = 0; i < width; ++i) { errorTable[1][i] = copy[0][i]; }
    for (int i = 0; i < height; ++i) { errorTable[0][i] = copy[1][height - 1 - i]; }
}
void aufDieKurve(Gitter& ctrl, int width, int height) {
    Ecke prev{};
    Ecke next{};
    for (int i = 0; i < width; ++i) {
        for (int j = 1; j < height; j += 2) {
            lerpEcke(ctrl[j][i], ctrl[j + 1][i], prev);
            lerpEcke(ctrl[j][i], ctrl[j - 1][i], next);
            lerpEcke(prev, next, ctrl[j][i]);
        }
    }
    for (int j = 0; j < height; ++j) {
        for (int i = 1; i < width; i += 2) {
            lerpEcke(ctrl[j][i], ctrl[j][i + 1], prev);
            lerpEcke(ctrl[j][i], ctrl[j][i - 1], next);
            lerpEcke(prev, next, ctrl[j][i]);
        }
    }
}

}  // namespace

// --- q_math ------------------------------------------------------------------
float spurRsqrt(float zahl) noexcept {
    const float x2 = zahl * 0.5F;
    std::int32_t i = 0;
    std::memcpy(&i, &zahl, sizeof(i));
    i = 0x5f3759df - (i >> 1);
    float y = 0.0F;
    std::memcpy(&y, &i, sizeof(y));
    y = y * (1.5F - (x2 * y * y));
    return y;
}

SpurEbene spurEbene(const float normale[3], float abstand) noexcept {
    SpurEbene p;
    int bits = 0;
    for (int j = 0; j < 3; ++j) {
        p.normale[j] = normale[j];
        if (normale[j] < 0) { bits |= 1 << j; }
    }
    p.abstand = abstand;
    p.vorzeichen = bits;
    // PlaneTypeForNormal (q_math.c:929 OJ)
    if (normale[0] == 1.0F) {
        p.typ = 0;
    } else if (normale[1] == 1.0F) {
        p.typ = 1;
    } else if (normale[2] == 1.0F) {
        p.typ = 2;
    } else {
        p.typ = 3;
    }
    return p;
}

int spurKastenSeite(const float emins[3], const float emaxs[3], const SpurEbene& p) noexcept {
    if (p.typ < 3) {
        if (p.abstand <= emins[p.typ]) { return 1; }
        if (p.abstand >= emaxs[p.typ]) { return 2; }
        return 3;
    }
    float dist[2] = {0.0F, 0.0F};
    if (p.vorzeichen < 8) {
        for (int i = 0; i < 3; ++i) {
            const int b = (p.vorzeichen >> i) & 1;
            dist[b] += p.normale[i] * emaxs[i];
            dist[!b] += p.normale[i] * emins[i];
        }
    }
    int sides = 0;
    if (dist[0] >= p.abstand) { sides = 1; }
    if (dist[1] < p.abstand) { sides |= 2; }
    return sides;
}

// --- R_SubdividePatchToGrid (tr_curve.cpp:287 OJ) ---------------------------
void spurGitter(int width, int height, const std::vector<std::array<float, 3>>& points, float unterteilung,
                std::vector<std::array<float, 3>>& aus, int& breiteAus, int& hoeheAus) {
    aus.clear();
    breiteAus = hoeheAus = 0;
    if (width < 1 || height < 1 || width > 32 || height > 32 ||   // MAX_PATCH_SIZE (tr_local.h:592 OJ)
        points.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return;
    }
    static thread_local Gitter ctrl;
    float errorTable[2][kGitterMax];
    for (int i = 0; i < width; ++i) {
        for (int j = 0; j < height; ++j) {
            const V3& p = points[static_cast<std::size_t>(j * width + i)];
            for (int k = 0; k < 3; ++k) { ctrl[j][i].xyz[k] = p[static_cast<std::size_t>(k)]; }
        }
    }
    Ecke prev{};
    Ecke next{};
    Ecke mid{};
    for (int dir = 0; dir < 2; ++dir) {
        for (int j = 0; j < kGitterMax; ++j) { errorTable[dir][j] = 0; }
        // horizontal subdivisions
        for (int j = 0; j + 2 < width; j += 2) {
            float maxLen = 0;
            for (int i = 0; i < height; ++i) {
                float midxyz[3];
                float d3[3];
                float projected[3];
                for (int l = 0; l < 3; ++l) {
                    // "(ctrl[i][j].xyz[l] + ctrl[i][j+1].xyz[l] * 2 + ctrl[i][j+2].xyz[l]) * 0.25"
                    midxyz[l] = static_cast<float>(
                        (ctrl[i][j].xyz[l] + ctrl[i][j + 1].xyz[l] * 2 + ctrl[i][j + 2].xyz[l]) * 0.25);
                }
                for (int l = 0; l < 3; ++l) { midxyz[l] = midxyz[l] - ctrl[i][j].xyz[l]; }
                for (int l = 0; l < 3; ++l) { d3[l] = ctrl[i][j + 2].xyz[l] - ctrl[i][j].xyz[l]; }
                normieren(d3);
                const float d = punkt(midxyz, d3);
                for (int l = 0; l < 3; ++l) { projected[l] = d3[l] * d; }
                for (int l = 0; l < 3; ++l) { midxyz[l] = midxyz[l] - projected[l]; }
                const float len = std::sqrt(punkt(midxyz, midxyz));
                if (len > maxLen) { maxLen = len; }
            }
            if (maxLen < 0.1) {
                errorTable[dir][j + 1] = 999;
                continue;
            }
            if (width + 2 > kGitterMax) {
                errorTable[dir][j + 1] = static_cast<float>(1.0 / maxLen);
                continue;
            }
            if (maxLen <= unterteilung) {
                errorTable[dir][j + 1] = static_cast<float>(1.0 / maxLen);
                continue;
            }
            errorTable[dir][j + 2] = static_cast<float>(1.0 / maxLen);
            width += 2;
            for (int i = 0; i < height; ++i) {
                lerpEcke(ctrl[i][j], ctrl[i][j + 1], prev);
                lerpEcke(ctrl[i][j + 1], ctrl[i][j + 2], next);
                lerpEcke(prev, next, mid);
                for (int k = width - 1; k > j + 3; --k) { ctrl[i][k] = ctrl[i][k - 2]; }
                ctrl[i][j + 1] = prev;
                ctrl[i][j + 2] = mid;
                ctrl[i][j + 3] = next;
            }
            j -= 2;
        }
        transponieren(width, height, ctrl);
        const int t = width;
        width = height;
        height = t;
    }
    aufDieKurve(ctrl, width, height);
    for (int i = 1; i < width - 1; ++i) {
        if (errorTable[0][i] != 999) { continue; }
        for (int j = i + 1; j < width; ++j) {
            for (int k = 0; k < height; ++k) { ctrl[k][j - 1] = ctrl[k][j]; }
            errorTable[0][j - 1] = errorTable[0][j];
        }
        width--;
    }
    for (int i = 1; i < height - 1; ++i) {
        if (errorTable[1][i] != 999) { continue; }
        for (int j = i + 1; j < height; ++j) {
            for (int k = 0; k < width; ++k) { ctrl[j - 1][k] = ctrl[j][k]; }
            errorTable[1][j - 1] = errorTable[1][j];
        }
        height--;
    }
    if (height > width) {
        transponieren(width, height, ctrl);
        fehlerUmkehren(errorTable, width, height);
        const int t = width;
        width = height;
        height = t;
        umkehren(width, height, ctrl);
    }
    breiteAus = width;
    hoeheAus = height;
    aus.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int i = 0; i < width; ++i) {
        for (int j = 0; j < height; ++j) {
            V3& v = aus[static_cast<std::size_t>(j * width + i)];
            for (int k = 0; k < 3; ++k) { v[static_cast<std::size_t>(k)] = ctrl[j][i].xyz[k]; }
        }
    }
}

// --- Die Welt aus der .bsp ----------------------------------------------------
SpurWelt spurWeltAus(const BspGeometry& geo) {
    constexpr std::uint32_t kKeinEinschlag = 0x00080000U;   // SURF_NOIMPACT
    constexpr std::uint32_t kKeineSpuren = 0x00100000U;     // SURF_NOMARKS
    constexpr std::uint32_t kNichtZeichnen = 0x00200000U;   // SURF_NODRAW
    constexpr std::uint32_t kNebel = 0x00000008U;           // CONTENTS_FOG (surfaceflags.h:36)
    constexpr float kUnterteilung = 4.0F;                   // r_subdivisions "4"
    SpurWelt w;
    w.ebenen.reserve(geo.planes.size());
    for (const BspGeometry::Plane& p : geo.planes) { w.ebenen.push_back(spurEbene(p.normal, p.dist)); }
    w.knoten.reserve(geo.nodes.size());
    for (const BspGeometry::Node& n : geo.nodes) {
        SpurWelt::Knoten k;
        k.ebene = n.plane;
        k.kinder[0] = n.children[0];
        k.kinder[1] = n.children[1];
        if (k.ebene < 0 || static_cast<std::size_t>(k.ebene) >= w.ebenen.size()) {
            return SpurWelt{};
        }
        w.knoten.push_back(k);
    }
    for (const BspGeometry::Leaf& l : geo.leafs) { w.blaetter.push_back(SpurWelt::Blatt{l.firstSurface, l.numSurfaces}); }
    w.blattFlaechen = geo.leafSurfaces;
    w.flaechen.resize(geo.surfaces.size());
    for (std::size_t si = 0; si < geo.surfaces.size(); ++si) {
        const BspSurface& s = geo.surfaces[si];
        SpurFlaeche& f = w.flaechen[si];
        std::uint32_t sf = 0;
        std::uint32_t cf = 0;
        if (s.shader >= 0 && static_cast<std::size_t>(s.shader) < geo.shaders.size()) {
            sf = geo.shaders[static_cast<std::size_t>(s.shader)].surfaceFlags;
            cf = geo.shaders[static_cast<std::size_t>(s.shader)].contentFlags;
        }
        f.ausgenommen = (sf & (kKeinEinschlag | kKeineSpuren)) != 0U || (cf & kNebel) != 0U;
        const auto ecken = [&](int erste, int zahl) {
            for (int i = 0; i < zahl; ++i) {
                const std::size_t vi = static_cast<std::size_t>(erste) + static_cast<std::size_t>(i);
                if (vi >= geo.verts.size()) {
                    return false;
                }
                const BspVertex& v = geo.verts[vi];
                f.punkte.push_back(V3{v.xyz[0], v.xyz[1], v.xyz[2]});
            }
            return true;
        };
        bool gut = false;
        if (s.type == BspSurface::Type::Planar || s.type == BspSurface::Type::TriangleSoup) {
            gut = ecken(s.firstVert, s.numVerts);
            for (int i = 0; gut && i < s.numIndexes; ++i) {
                const std::size_t ii = static_cast<std::size_t>(s.firstIndex) + static_cast<std::size_t>(i);
                if (ii >= geo.indexes.size() || geo.indexes[ii] >= static_cast<std::uint32_t>(s.numVerts)) {
                    gut = false;
                    break;
                }
                f.indizes.push_back(static_cast<int>(geo.indexes[ii]));
            }
            if (gut && s.type == BspSurface::Type::Planar && !f.punkte.empty()) {
                f.art = SpurFlaeche::Art::Flaeche;
                // cv->plane.dist = DotProduct( cv->points[0], cv->plane.normal )
                f.ebene = spurEbene(s.ebenenNormale, punkt(f.punkte[0].data(), s.ebenenNormale));
            } else if (gut) {
                f.art = SpurFlaeche::Art::Dreiecke;
            }
        } else if (s.type == BspSurface::Type::Patch) {
            // ParseMesh: NODRAW -> SF_SKIP (tr_bsp.cpp:487 OJ)
            if ((sf & kNichtZeichnen) == 0U && ecken(s.firstVert, s.patchWidth * s.patchHeight)) {
                std::vector<V3> kontrolle;
                kontrolle.swap(f.punkte);
                spurGitter(s.patchWidth, s.patchHeight, kontrolle, kUnterteilung, f.punkte, f.breite, f.hoehe);
                gut = f.breite > 1 && f.hoehe > 1;
                if (gut) { f.art = SpurFlaeche::Art::Gitter; }
            }
        }
        if (!gut) {
            f.art = SpurFlaeche::Art::Keine;
            f.punkte.clear();
            f.indizes.clear();
            continue;
        }
        for (int k = 0; k < 3; ++k) {
            f.mins[k] = 1.0e30F;
            f.maxs[k] = -1.0e30F;
        }
        for (const V3& p : f.punkte) {
            for (int k = 0; k < 3; ++k) {
                f.mins[k] = std::min(f.mins[k], p[static_cast<std::size_t>(k)]);
                f.maxs[k] = std::max(f.maxs[k], p[static_cast<std::size_t>(k)]);
            }
        }
    }
    w.gesehen.assign(w.flaechen.size(), 0);
    return w;
}

// --- R_MarkFragments (tr_marks.cpp:254 OJ) ------------------------------------
int spurBruchstuecke(const SpurWelt& welt, int numPoints, const float (*points)[3], const float projection[3],
                     int maxPoints, float (*pointBuffer)[3], int maxFragments, SpurBruchstueck* fragmentBuffer) {
    if (welt.leer() || numPoints <= 0) {
        return 0;
    }
    float normals[kMaxEckenR + 2][3];
    float dists[kMaxEckenR + 2];
    static thread_local float clipPoints[2][kMaxEckenR][3];
    float projectionDir[3];
    float v1[3];
    float v2[3];
    float normal[3];
    welt.zaehler++;   // tr.viewCount++
    if (welt.gesehen.size() != welt.flaechen.size()) {
        welt.gesehen.assign(welt.flaechen.size(), 0);
    }
    normieren2(projection, projectionDir);
    Sammlung s;
    for (int k = 0; k < 3; ++k) {   // ClearBounds
        s.mins[k] = 99999;
        s.maxs[k] = -99999;
    }
    const auto dazu = [&s](const float v[3]) {   // AddPointToBounds
        for (int k = 0; k < 3; ++k) {
            if (v[k] < s.mins[k]) { s.mins[k] = v[k]; }
            if (v[k] > s.maxs[k]) { s.maxs[k] = v[k]; }
        }
    };
    for (int i = 0; i < numPoints; ++i) {
        float temp[3];
        dazu(points[i]);
        for (int k = 0; k < 3; ++k) { temp[k] = points[i][k] + projection[k]; }
        dazu(temp);
        for (int k = 0; k < 3; ++k) { temp[k] = points[i][k] + -20 * projectionDir[k]; }
        dazu(temp);
    }
    if (numPoints > kMaxEckenR) { numPoints = kMaxEckenR; }
    for (int i = 0; i < numPoints; ++i) {
        for (int k = 0; k < 3; ++k) { v1[k] = points[(i + 1) % numPoints][k] - points[i][k]; }
        for (int k = 0; k < 3; ++k) { v2[k] = points[i][k] + projection[k]; }
        for (int k = 0; k < 3; ++k) { v2[k] = points[i][k] - v2[k]; }
        kreuz(v1, v2, normals[i]);
        normierenSchnell(normals[i]);
        dists[i] = punkt(normals[i], points[i]);
    }
    for (int k = 0; k < 3; ++k) { normals[numPoints][k] = projectionDir[k]; }
    dists[numPoints] = punkt(normals[numPoints], points[0]) - 32;
    for (int k = 0; k < 3; ++k) { normals[numPoints + 1][k] = -projectionDir[k]; }
    dists[numPoints + 1] = punkt(normals[numPoints + 1], points[0]) - 20;
    const int numPlanes = numPoints + 2;
    for (int k = 0; k < 3; ++k) { s.richtung[k] = projectionDir[k]; }
    kastenFlaechen(welt, 0, s);

    Ausgabe a;
    a.maxPunkte = maxPoints;
    a.puffer = pointBuffer;
    a.maxBruch = maxFragments;
    a.bruch = fragmentBuffer;
    // Nur zum Vorsortieren: was nach allen Schnitten uebrig bleibt, liegt
    // innerhalb der Seitenebenen (+0,5) und zwischen -32 und +20 entlang der
    // Projektion um points[0]; also hoechstens 33 + (Ausdehnung der Punkte
    // entlang der Projektion) neben dem Kasten der Punkte. Mit 34 + Ausdehnung
    // faellt nichts weg, was ein Bruchstueck ergaebe.
    {
        float ausdehnung = 0.0F;
        const float d0 = punkt(projectionDir, points[0]);
        for (int i = 0; i < numPoints; ++i) {
            ausdehnung = std::max(ausdehnung, std::fabs(punkt(projectionDir, points[i]) - d0));
        }
        const float rand = 34.0F + ausdehnung;
        for (int k = 0; k < 3; ++k) {
            a.weitMin[k] = s.mins[k] - rand;
            a.weitMax[k] = s.maxs[k] + rand;
        }
    }
    for (const int fi : s.liste) {
        const SpurFlaeche& f = welt.flaechen[static_cast<std::size_t>(fi)];
        if (f.art == SpurFlaeche::Art::Gitter) {
            bool weit = false;
            for (int k = 0; k < 3; ++k) {
                weit = weit || f.maxs[k] < a.weitMin[k] || f.mins[k] > a.weitMax[k];
            }
            if (weit) {
                continue;
            }
            const int w = f.breite;
            for (int m = 0; m < f.hoehe - 1; ++m) {
                for (int n = 0; n < w - 1; ++n) {
                    const std::size_t dv = static_cast<std::size_t>(m * w + n);
                    const float* p00 = f.punkte[dv].data();
                    const float* p01 = f.punkte[dv + static_cast<std::size_t>(w)].data();
                    const float* p10 = f.punkte[dv + 1].data();
                    const float* p11 = f.punkte[dv + static_cast<std::size_t>(w) + 1].data();
                    // dv[0], dv[width], dv[1]
                    const float* e1[3] = {p00, p01, p10};
                    if (!draussen(e1, 3, a)) {
                        for (int k = 0; k < 3; ++k) {
                            clipPoints[0][0][k] = p00[k];
                            clipPoints[0][1][k] = p01[k];
                            clipPoints[0][2][k] = p10[k];
                        }
                        for (int k = 0; k < 3; ++k) {
                            v1[k] = clipPoints[0][0][k] - clipPoints[0][1][k];
                            v2[k] = clipPoints[0][2][k] - clipPoints[0][1][k];
                        }
                        kreuz(v1, v2, normal);
                        normierenSchnell(normal);
                        if (punkt(normal, projectionDir) < -0.1) {
                            bruchDazu(3, clipPoints, numPlanes, normals, dists, a);
                            if (a.bruchstuecke == maxFragments) {
                                return a.bruchstuecke;
                            }
                        }
                    }
                    // dv[1], dv[width], dv[width+1]
                    const float* e2[3] = {p10, p01, p11};
                    if (!draussen(e2, 3, a)) {
                        for (int k = 0; k < 3; ++k) {
                            clipPoints[0][0][k] = p10[k];
                            clipPoints[0][1][k] = p01[k];
                            clipPoints[0][2][k] = p11[k];
                        }
                        for (int k = 0; k < 3; ++k) {
                            v1[k] = clipPoints[0][0][k] - clipPoints[0][1][k];
                            v2[k] = clipPoints[0][2][k] - clipPoints[0][1][k];
                        }
                        kreuz(v1, v2, normal);
                        normierenSchnell(normal);
                        if (punkt(normal, projectionDir) < -0.05) {
                            bruchDazu(3, clipPoints, numPlanes, normals, dists, a);
                            if (a.bruchstuecke == maxFragments) {
                                return a.bruchstuecke;
                            }
                        }
                    }
                }
            }
        } else if (f.art == SpurFlaeche::Art::Flaeche) {
            if (punkt(f.ebene.normale, projectionDir) > -0.5) {
                continue;
            }
            for (std::size_t k = 0; k + 2 < f.indizes.size(); k += 3) {
                const float* e[3];
                for (int j = 0; j < 3; ++j) {
                    e[j] = f.punkte[static_cast<std::size_t>(f.indizes[k + static_cast<std::size_t>(j)])].data();
                }
                if (draussen(e, 3, a)) {
                    continue;
                }
                for (int j = 0; j < 3; ++j) {
                    // VectorMA( v, MARKER_OFFSET (0), plane.normal, clipPoints[0][j] )
                    for (int c = 0; c < 3; ++c) { clipPoints[0][j][c] = e[j][c] + 0 * f.ebene.normale[c]; }
                }
                bruchDazu(3, clipPoints, numPlanes, normals, dists, a);
                if (a.bruchstuecke == maxFragments) {
                    return a.bruchstuecke;
                }
            }
        } else if (f.art == SpurFlaeche::Art::Dreiecke) {
            for (std::size_t k = 0; k + 2 < f.indizes.size(); k += 3) {
                const float* p1 = f.punkte[static_cast<std::size_t>(f.indizes[k])].data();
                const float* p2 = f.punkte[static_cast<std::size_t>(f.indizes[k + 1])].data();
                const float* p3 = f.punkte[static_cast<std::size_t>(f.indizes[k + 2])].data();
                const float* e[3] = {p1, p2, p3};
                if (draussen(e, 3, a)) {
                    continue;
                }
                for (int c = 0; c < 3; ++c) {
                    v1[c] = p1[c] - p2[c];
                    v2[c] = p3[c] - p2[c];
                }
                kreuz(v1, v2, normal);
                normierenSchnell(normal);
                if (punkt(normal, projectionDir) < -0.1) {
                    for (int c = 0; c < 3; ++c) {
                        clipPoints[0][0][c] = p1[c] + 0 * normal[c];
                        clipPoints[0][1][c] = p2[c] + 0 * normal[c];
                        clipPoints[0][2][c] = p3[c] + 0 * normal[c];
                    }
                    bruchDazu(3, clipPoints, numPlanes, normals, dists, a);
                    if (a.bruchstuecke == maxFragments) {
                        return a.bruchstuecke;
                    }
                }
            }
        }
    }
    return a.bruchstuecke;
}

// --- cg_marks.cpp ---------------------------------------------------------------
void spurFarbe(const SpurPoly& p, int cgZeit, std::uint8_t aus[4]) noexcept {
    // CG_AddMarks: "t = mp->time + MARK_TOTAL_TIME - cg.time"
    const int t = p.zeit + kSpurGesamtZeit - cgZeit;
    aus[0] = p.farbe[0];
    aus[1] = p.farbe[1];
    aus[2] = p.farbe[2];
    aus[3] = 0;   // Startwert der Ecken (siehe Kopf)
    if (t < kSpurBlendZeit) {
        const int fade = 255 * t / kSpurBlendZeit;
        if (p.alphaFade) {
            aus[3] = static_cast<std::uint8_t>(fade);
        } else {
            const float f = static_cast<float>(t) / kSpurBlendZeit;
            aus[0] = static_cast<std::uint8_t>(static_cast<float>(p.farbe[0]) * f);
            aus[1] = static_cast<std::uint8_t>(static_cast<float>(p.farbe[1]) * f);
            aus[2] = static_cast<std::uint8_t>(static_cast<float>(p.farbe[2]) * f);
        }
    }
}

SpurSpeicher::SpurSpeicher() {
    // CG_InitMarkPolys: alle frei, in Reihenfolge verkettet.
    for (int i = 0; i < kSpurMaxPolys; ++i) { m_freiNach[static_cast<std::size_t>(i)] = i + 1 < kSpurMaxPolys ? i + 1 : -1; }
    m_frei = 0;
}

void SpurSpeicher::frei(int i) {
    const auto u = static_cast<std::size_t>(i);
    const int v = m_vor[u];
    const int n = m_nach[u];
    if (v >= 0) { m_nach[static_cast<std::size_t>(v)] = n; } else { m_kopf = n; }
    if (n >= 0) { m_vor[static_cast<std::size_t>(n)] = v; } else { m_schwanz = v; }
    m_aktiv[u] = false;
    m_freiNach[u] = m_frei;
    m_frei = i;
    --m_zahl;
}

SpurPoly& SpurSpeicher::neu() {
    if (m_frei < 0) {
        // "remove the oldest active entity" - samt allen gleicher Zeit dahinter
        const int zeit = m_polys[static_cast<std::size_t>(m_schwanz)].zeit;
        while (m_schwanz >= 0 && zeit == m_polys[static_cast<std::size_t>(m_schwanz)].zeit) { frei(m_schwanz); }
    }
    const int i = m_frei;
    const auto u = static_cast<std::size_t>(i);
    m_frei = m_freiNach[u];
    m_polys[u] = SpurPoly{};
    // vorn in die Liste (neueste zuerst)
    m_vor[u] = -1;
    m_nach[u] = m_kopf;
    if (m_kopf >= 0) { m_vor[static_cast<std::size_t>(m_kopf)] = i; } else { m_schwanz = i; }
    m_kopf = i;
    m_aktiv[u] = true;
    ++m_zahl;
    return m_polys[u];
}

void SpurSpeicher::bild(int cgZeit) {
    for (int i = m_kopf; i >= 0;) {
        const int naechste = m_nach[static_cast<std::size_t>(i)];
        if (cgZeit > m_polys[static_cast<std::size_t>(i)].zeit + kSpurGesamtZeit) { frei(i); }
        i = naechste;
    }
}

std::vector<const SpurPoly*> SpurSpeicher::lebende() const {
    std::vector<const SpurPoly*> aus;
    aus.reserve(static_cast<std::size_t>(m_zahl));
    for (int i = m_kopf; i >= 0; i = m_nach[static_cast<std::size_t>(i)]) { aus.push_back(&m_polys[static_cast<std::size_t>(i)]); }
    return aus;
}

// --- CG_CreateSaberMarks (cg_players.cpp:13689 MD, 5847 OJ) --------------------
void saberSpurenErzeugen(const SpurWelt& welt, const float start[3], const float end[3], const float normal[3],
                         int cgZeit, std::uint64_t saat, std::vector<SpurPoly>& neu) {
    constexpr int kMaxBruch = 128;    // MAX_MARK_FRAGMENTS
    constexpr int kMaxPunkte = 384;   // MAX_MARK_POINTS
    constexpr float radius = 0.65F;
    const bool md = saberMovieDuelsRegeln();
    float axis[3][3]{};
    for (int k = 0; k < 3; ++k) { axis[1][k] = end[k] - start[k]; }
    if (md) {
        // "Degenerate slash direction -> bail"
        if (normieren(axis[1]) == 0.0F) {
            return;
        }
        for (int k = 0; k < 3; ++k) { axis[0][k] = normal[k]; }
        if (normieren(axis[0]) == 0.0F) {
            axis[0][0] = 0.0F;
            axis[0][1] = 0.0F;
            axis[0][2] = 1.0F;
        }
        kreuz(axis[1], axis[0], axis[2]);
        if (normieren(axis[2]) == 0.0F) {
            senkrecht(axis[2], axis[0]);
        }
    } else {
        normierenSchnell(axis[1]);
        for (int k = 0; k < 3; ++k) { axis[0][k] = normal[k]; }
        kreuz(axis[1], axis[0], axis[2]);
    }
    float originalPoints[4][3];
    for (int i = 0; i < 3; ++i) {
        originalPoints[0][i] = start[i] - radius * axis[1][i] - radius * axis[2][i];
        originalPoints[1][i] = end[i] + radius * axis[1][i] - radius * axis[2][i];
        originalPoints[2][i] = end[i] + radius * axis[1][i] + radius * axis[2][i];
        originalPoints[3][i] = start[i] - radius * axis[1][i] + radius * axis[2][i];
    }
    float projection[3];
    // MD: VectorScale( axis[0], -1, projection ), OJ: VectorScale( normal, -1, projection )
    for (int k = 0; k < 3; ++k) { projection[k] = (md ? axis[0][k] : normal[k]) * -1; }
    static thread_local float markPoints[kMaxPunkte][3];
    SpurBruchstueck markFragments[kMaxBruch];
    int numFragments = spurBruchstuecke(welt, 4, originalPoints, projection, kMaxPunkte, markPoints, kMaxBruch,
                                        markFragments);
    if (md) {
        if (numFragments <= 0) {
            return;
        }
        numFragments = std::min(numFragments, kMaxBruch);
    }
    int wurf = 0;
    for (int i = 0; i < numFragments; ++i) {
        SpurBruchstueck& mf = markFragments[i];
        if (mf.zahl > kSpurMaxEcken) { mf.zahl = kSpurMaxEcken; }
        SpurPoly burn;
        float mid[3];
        for (int k = 0; k < 3; ++k) { mid[k] = (md ? (start[k] + end[k]) : (end[k] + start[k])) * 0.5F; }
        for (int j = 0; j < mf.zahl; ++j) {
            float delta[3];
            for (int k = 0; k < 3; ++k) {
                burn.xyz[j][k] = markPoints[mf.ersterPunkt + j][k];
                delta[k] = burn.xyz[j][k] - mid[k];
            }
            if (md) {
                // "Stable, non-random UV mapping"
                burn.st[j][0] = 0.5F + punkt(delta, axis[1]) * 0.06F;
                burn.st[j][1] = 0.5F + punkt(delta, axis[2]) * 0.12F;
            } else {
                // "0.5 + DotProduct( delta, axis[1] ) * (0.05f + Q_flrand(0.0f, 1.0f) * 0.03f)" - double
                const float r0 = flrand(saat, wurf++, 0.0F, 1.0F);
                burn.st[j][0] = static_cast<float>(0.5 + punkt(delta, axis[1]) * (0.05F + r0 * 0.03F));
                const float r1 = flrand(saat, wurf++, 0.0F, 1.0F);
                burn.st[j][1] = static_cast<float>(0.5 + punkt(delta, axis[2]) * (0.15F + r1 * 0.05F));
            }
        }
        // "save it persistantly, do burn first"
        burn.zeit = cgZeit;
        burn.alphaFade = true;
        burn.shader = 0;
        burn.zahl = mf.zahl;
        burn.farbe[0] = burn.farbe[1] = burn.farbe[2] = burn.farbe[3] = 255;
        // "And now do a glow pass" - Startzeit 8500 ms frueher
        SpurPoly glow = burn;
        glow.zeit = cgZeit - 8500;
        glow.alphaFade = false;
        glow.shader = 1;
        glow.farbe[0] = static_cast<std::uint8_t>(215 + flrand(saat, wurf++, 0.0F, 1.0F) * 40.0F);
        glow.farbe[1] = static_cast<std::uint8_t>(96 + flrand(saat, wurf++, 0.0F, 1.0F) * 32.0F);
        glow.farbe[2] = glow.farbe[3] = static_cast<std::uint8_t>(flrand(saat, wurf++, 0.0F, 1.0F) * 15.0F);
        neu.push_back(burn);
        neu.push_back(glow);
    }
}

}  // namespace bhed
