// navlauf.cpp - Paket bewegung (rc601): Laufen und Wege wie im Spiel. Warum
// so: bhed/navlauf.h. Die Stellen beziehen sich auf MovieDuels-SJE-SP-Rend2
// (code/game, code/Ragl, code/Ratl, code/Ravl); OpenJK code/game ist bis auf
// die vermerkten Stellen gleich.
#include "bhed/navlauf.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace bhed {
namespace {

// --- Konstanten ----------------------------------------------------------------
// g_navigator.cpp:101-168
constexpr float kNeighboringDist = 200.0F;            // NEIGHBORING_DIST
constexpr float kSafeNeighboringPointDist = 400.0F;   // SAFE_NEIGHBORINGPOINT_DIST
constexpr int kNumNodes = 1024;                       // NUM_NODES
constexpr int kNumEdges = 3 * kNumNodes;              // NUM_EDGES
constexpr int kEdgesPerNode = 20;                     // NUM_EDGES_PER_NODE
constexpr int kNumRegions = kNumNodes / 3;            // NUM_REGIONS
constexpr int kNumCells = 32;                         // NUM_CELLS
constexpr int kNodesPerCell = 60;                     // NUM_NODES_PER_CELL
constexpr int kNumTargets = 5;                        // NUM_TARGETS
constexpr float kCellRange = 1000.0F;                 // CELL_RANGE
constexpr float kViewRange = 550.0F;                  // VIEW_RANGE
constexpr float kBiasNonWaypoint = 500.0F;            // BIAS_NONWAYPOINT
constexpr int kMaxPathSize = kNumNodes / 7;           // MAX_PATH_SIZE
constexpr float kZCullOffset = 60.0F;                 // Z_CULL_OFFSET
constexpr float kMaxEdgeFloorDist = 60.0F;            // MAX_EDGE_FLOOR_DIST
constexpr float kMaxEdgeAutoLen = 500.0F;             // MAX_EDGE_AUTO_LEN
constexpr int kMinWayNeighbors = 4;                   // MIN_WAY_NEIGHBORS
constexpr int kMaxNonWpNeighbors = 1;                 // MAX_NONWP_NEIGHBORS
constexpr float kScMediumRadius = 20.0F;              // SC_MEDIUM_RADIUS
constexpr float kScMediumHeight = 60.0F;              // SC_MEDIUM_HEIGHT
constexpr float kScLargeRadius = 60.0F;               // SC_LARGE_RADIUS
constexpr float kScLargeHeight = 120.0F;              // SC_LARGE_HEIGHT
// STEER (g_navigator.cpp:170-180)
constexpr float kSteerNeighborRange = 60.0F;          // NEIGHBOR_RANGE
constexpr int kSideLockedTimerMitte = 1250;           // Q_irand(500, SIDE_LOCKED_TIMER 2000), Mitte
// g_nav.h:28-29
constexpr float kMaxRadiusCheck = 1024.0F;            // MAX_RADIUS_CHECK
constexpr int kYawIterations = 16;                    // YAW_ITERATIONS
// bg_public.h:71-92, g_client.cpp:92-94
constexpr float kDefaultMaxs0 = 16.0F;                // DEFAULT_MAXS_0
constexpr float kDefaultMins2 = -24.0F;               // DEFAULT_MINS_2
constexpr float kDefaultMaxs2 = 40.0F;                // DEFAULT_MAXS_2
constexpr float kCrouchMaxs2 = 16.0F;                 // CROUCH_MAXS_2
constexpr float kStepSize = 18.0F;                    // STEPSIZE
constexpr float kMinWalkNormal = 0.7F;                // MIN_WALK_NORMAL
// bg_pmove.cpp:207-219, bg_local.h:32, NPC_spawn.cpp:1928 (ps.friction = 6)
constexpr float kPmStopSpeed = 100.0F;
constexpr float kPmAccelerate = 12.0F;
constexpr float kNpcFriction = 6.0F;
constexpr float kOverclip = 1.001F;
// b_local.h MIN_ANGLE_ERROR
constexpr float kMinAngleErr = 0.01F;
// FindPath: Q_irand(3000, 6000) - fest die Mitte (siehe navlauf.h)
constexpr int kAStarWiederMs = 4500;

constexpr int kSideNone = 0;    // Ravl/CVec.h:90 ESide
constexpr int kSideLeft = 1;
constexpr int kSideRight = 2;

constexpr float kGrad = 57.29577951308232F;

// rc601 (bewegung, Durchsicht 2): navSchnellwegeAn, navPlanZaehler.
std::atomic<bool> gSchnellwege{true};
std::atomic<long> gPlaeneKalt{0};
std::atomic<long> gPlaeneWarm{0};
std::atomic<long long> gPlaeneKaltUs{0};

// --- Vektoren (Ravl/CVec.cpp, q_math) ------------------------------------------
NavVec plus(const NavVec& a, const NavVec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
NavVec minus(const NavVec& a, const NavVec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
NavVec mal(const NavVec& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float punkt(const NavVec& a, const NavVec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float laenge(const NavVec& a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
float laenge2(const NavVec& a) { return a[0] * a[0] + a[1] * a[1] + a[2] * a[2]; }
float abstand(const NavVec& a, const NavVec& b) { return laenge(minus(a, b)); }
float abstand2(const NavVec& a, const NavVec& b) { return laenge2(minus(a, b)); }
bool istNull(const NavVec& a) { return a[0] == 0.0F && a[1] == 0.0F && a[2] == 0.0F; }
// CVec3::Norm (CVec.cpp:621): ohne Pruefung auf Null.
float norm(NavVec& a) {
    const float l = laenge(a);
    a = {a[0] / l, a[1] / l, a[2] / l};
    return l;
}
// CVec3::SafeNorm (CVec.cpp:632)
float sicherNorm(NavVec& a) {
    const float d = laenge(a);
    if (d > 1E-10F) {
        a = {a[0] / d, a[1] / d, a[2] / d};
        return d;
    }
    a = {0.0F, 0.0F, 0.0F};
    return 0.0F;
}
// CVec3::Truncate (CVec.cpp:667)
float kappen(NavVec& a, float maxlen) {
    float len = laenge(a);
    if (len > maxlen && len > 1E-10F) {
        len = maxlen / len;
        a = {a[0] * len, a[1] * len, a[2] * len};
        return maxlen;
    }
    return len;
}
// CVec3::ProjectToLine (CVec.h:1017): this auf die Gerade, Rueckgabe der Anteil.
float aufGerade(NavVec& p, const NavVec& a, const NavVec& b) {
    const NavVec u = minus(b, a);
    const NavVec q = minus(p, a);
    const float scale = punkt(q, u) / laenge2(u);
    p = plus(mal(u, scale), a);
    return scale;
}
float aufStrecke(NavVec& p, const NavVec& a, const NavVec& b) {
    const float scale = aufGerade(p, a, b);
    if (scale < 0.0F) {
        p = a;
    } else if (scale > 1.0F) {
        p = b;
    }
    return scale;
}
float abstandZurStrecke(const NavVec& p, const NavVec& a, const NavVec& b) {
    NavVec q = p;
    aufStrecke(q, a, b);
    return abstand(p, q);
}
// CVec3::VecToAng (CVec.cpp:731) - nur die Gier, 0..360.
float gierVon(const NavVec& v) {
    if (v[1] == 0.0F && v[0] == 0.0F) { return 0.0F; }
    if (v[0] != 0.0F) {
        float yaw = std::atan2(v[1], v[0]) * kGrad;
        if (yaw < 0.0F) { yaw += 360.0F; }
        return yaw;
    }
    return v[1] > 0.0F ? 90.0F : 270.0F;
}
// vectoangles (q_math.c:616-646 MD) - nur die Gier. Anders gerundet als
// VecToAng: atan2f * 180 in float, dann / M_PI in double.
float qGier(const NavVec& v) {
    if (v[1] == 0.0F && v[0] == 0.0F) { return 0.0F; }
    float yaw = 0.0F;
    if (v[0] != 0.0F) {
        yaw = static_cast<float>(static_cast<double>(std::atan2(v[1], v[0]) * 180.0F) / 3.14159265358979323846);
    } else {
        yaw = v[1] > 0.0F ? 90.0F : 270.0F;
    }
    if (yaw < 0.0F) { yaw += 360.0F; }
    return yaw;
}
// AngleVectors (q_math.c:1291-1321 MD) mit Nick und Rollen 0: der Winkel in
// double gerechnet, dann sinf/cosf. ucmd = floor(Dot * 127) haengt an jeder
// Rundung (bei Gier 90 ist cosf nicht 0, sondern -4,4e-8).
void qAchsen(float gier, NavVec& vorn, NavVec& rechts) {
    const auto winkel = static_cast<float>(static_cast<double>(gier) * (3.14159265358979323846 * 2 / 360));
    const float sy = std::sin(winkel);
    const float cy = std::cos(winkel);
    vorn = {cy, sy, -0.0F};
    rechts = {sy, -cy, -0.0F};
}
// AngleNormalize360 / AngleNormalize180 / AngleDelta (q_math.c)
float winkel360(float a) {
    return (360.0F / 65536.0F) * static_cast<float>(static_cast<int>(a * (65536.0F / 360.0F)) & 65535);
}
float winkelDelta(float a1, float a2) {
    float a = std::fmod(a1 - a2, 360.0F);
    if (a > 180.0F) {
        a -= 360.0F;
    } else if (a < -180.0F) {
        a += 360.0F;
    }
    return a;
}
// CVec3::Reposition (CVec.cpp:1129): erst um z drehen, dann verschieben.
NavVec umsetzen(const NavVec& v, const NavVec& verschiebung, float grad) {
    NavVec r = v;
    if (grad != 0.0F) {
        const float w = grad / kGrad;
        r[0] = v[0] * std::cos(w) - v[1] * std::sin(w);
        r[1] = v[0] * std::sin(w) + v[1] * std::cos(w);
    }
    return plus(r, verschiebung);
}
bool groesserAlle(const NavVec& a, const NavVec& b) { return a[0] > b[0] && a[1] > b[1] && a[2] > b[2]; }
bool kleinerAlle(const NavVec& a, const NavVec& b) { return a[0] < b[0] && a[1] < b[1] && a[2] < b[2]; }

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}
int ganz(const MapEntity& e, const char* key, int vorgabe = 0) {
    const std::string* v = e.find(key);
    return v != nullptr ? std::atoi(v->c_str()) : vorgabe;
}
float zahl(const MapEntity& e, const char* key, float vorgabe = 0.0F) {
    const std::string* v = e.find(key);
    return v != nullptr ? static_cast<float>(std::atof(v->c_str())) : vorgabe;
}
std::string text(const MapEntity& e, const char* key) {
    const std::string* v = e.find(key);
    return v != nullptr ? *v : std::string();
}

// ratl::vector_vs::sort (ratl/vector_vs.h:332): ein Heapsort - NICHT stabil.
// Gleiche Kosten kommen in genau der Reihenfolge heraus wie in der Engine.
template <class T, class Kleiner>
void heapSortieren(std::vector<T>& a, Kleiner kleiner) {
    const int n = static_cast<int>(a.size());
    const auto eltern = [](int i) { return (i - 1) / 2; };
    for (int heap = 1; heap < n; ++heap) {
        int pos = heap;
        int cmp = eltern(pos);
        while (kleiner(a[static_cast<std::size_t>(cmp)], a[static_cast<std::size_t>(pos)])) {
            std::swap(a[static_cast<std::size_t>(cmp)], a[static_cast<std::size_t>(pos)]);
            pos = cmp;
            cmp = eltern(pos);
        }
    }
    const auto groesstesKind = [&](int i, int groesse) {
        const int l = 2 * i + 1;
        const int r = 2 * i + 2;
        if (l < groesse) {
            if (r < groesse) {
                return kleiner(a[static_cast<std::size_t>(r)], a[static_cast<std::size_t>(l)]) ? l : r;
            }
            return l;
        }
        return i;
    };
    for (int heap = n - 1; heap > 0; --heap) {
        std::swap(a[0], a[static_cast<std::size_t>(heap)]);
        int pos = 0;
        int cmp = groesstesKind(pos, heap);
        while (kleiner(a[static_cast<std::size_t>(pos)], a[static_cast<std::size_t>(cmp)])) {
            std::swap(a[static_cast<std::size_t>(cmp)], a[static_cast<std::size_t>(pos)]);
            pos = cmp;
            cmp = groesstesKind(pos, heap);
        }
    }
}

// --- Spuren ------------------------------------------------------------------
NavSpur frei(const NavVec& end) {
    NavSpur s;
    s.endpos = end;
    return s;
}
NavSpur spurDurch(const NavSpurFn& f, const NavVec& a, const NavVec& mins, const NavVec& maxs, const NavVec& b,
                  int ign, std::uint32_t maske) {
    return f ? f(a, mins, maxs, b, ign, maske) : frei(b);
}
bool durch(const NavSpur& s) { return !s.allsolid && !s.startsolid && s.fraction == 1.0F; }
// ViewTrace / ViewNavTrace (g_navigator.cpp:819-851): ein Strahl.
bool sichtFrei(const NavSpurFn& f, const NavVec& a, const NavVec& b, bool botclip, NavSpur* aus = nullptr) {
    const std::uint32_t c = kNavSolid | kNavTerrain | kNavMonsterClip | (botclip ? kNavBotClip : 0U);
    const NavSpur s = spurDurch(f, a, {0, 0, 0}, {0, 0, 0}, b, kNavKeineEntity, c);
    if (aus != nullptr) { *aus = s; }
    return durch(s);
}
// MoveTrace (g_navigator.cpp:853-909)
bool gehFrei(const NavSpurFn& f, const NavVec& a, const NavVec& b, const NavVec& mins, const NavVec& maxs, int ign,
             bool donotenter, bool nochmal, bool ohneKoerper, NavSpur* aus = nullptr) {
    std::uint32_t c = kNavMaskNpcSolid;
    if (donotenter) { c |= kNavBotClip; }
    if (ohneKoerper) { c &= ~kNavBody; }
    NavSpur s = spurDurch(f, a, mins, maxs, b, ign, c);
    if (aus != nullptr) { *aus = s; }
    if (durch(s)) { return true; }
    if (donotenter && nochmal && (s.allsolid || s.startsolid)) {
        c &= ~kNavBotClip;
        s = spurDurch(f, a, mins, maxs, b, ign, c);
        if (aus != nullptr) { *aus = s; }
        if (durch(s)) { return true; }
    }
    return false;
}

}  // namespace

// =============================================================================
// Das Netz
// =============================================================================

int NavNetz::kanteZwischen(int a, int b) const {
    // ragl::graph_vs::get_edge_across (graph_vs.h:594)
    if (a <= 0 || b <= 0 || a >= static_cast<int>(links.size())) { return 0; }
    for (const Nachbar& n : links[static_cast<std::size_t>(a)]) {
        if (n.knoten == b) { return n.kante != 0 ? n.kante : -1; }
    }
    return 0;
}

int NavNetz::zahlKanten() const {
    // Nach dem Bau gezaehlt (baueNavNetz) - NAV::GetNearestNode fragt es in
    // jedem Denkbild ("if (mGraph.size_edges() > 0)").
    if (kantenZahl >= 0) { return kantenZahl; }
    int n = 0;
    for (std::size_t i = 1; i < kanteBenutzt.size(); ++i) {
        if (kanteBenutzt[i]) { ++n; }
    }
    return n;
}

namespace {
// ratl::grid2_vs (grid_vs.h:61 ff.)
void gitterKappen(const NavNetz& n, float& x, float& y) {
    if (x < n.zMin[0]) { x = n.zMin[0]; }
    if (x > n.zMax[0] - 1.0F) { x = n.zMax[0] - 1.0F; }
    if (y < n.zMin[1]) { y = n.zMin[1]; }
    if (y > n.zMax[1] - 1.0F) { y = n.zMax[1] - 1.0F; }
}
int zellIndex(const NavNetz& n, float x, float y) {
    if (n.zMass[0] == 0.0F || n.zMass[1] == 0.0F || n.zellen.empty()) { return -1; }
    gitterKappen(n, x, y);
    const int xi = static_cast<int>((x - n.zMin[0]) / n.zMass[0]);
    const int yi = static_cast<int>((y - n.zMin[1]) / n.zMass[1]);
    if (xi < 0 || yi < 0 || xi >= kNumCells || yi >= kNumCells) { return -1; }
    return xi + yi * kNumCells;
}
void gitterErweitern(NavNetz& n, float x, float y) {
    const float p[2] = {x, y};
    for (int i = 0; i < 2; ++i) {
        if (p[i] < n.zMin[i] || n.zMin[i] == 12345.0F) { n.zMin[i] = p[i]; }
        if (p[i] > n.zMax[i] || n.zMax[i] == 12345.0F) { n.zMax[i] = p[i]; }
    }
    n.zMass[0] = (n.zMax[0] - n.zMin[0]) / static_cast<float>(kNumCells);
    n.zMass[1] = (n.zMax[1] - n.zMin[1]) / static_cast<float>(kNumCells);
}
}  // namespace

const NavNetz::Zelle* NavNetz::zelleBei(float x, float y) const {
    const int i = zellIndex(*this, x, y);
    return i < 0 ? nullptr : &zellen[static_cast<std::size_t>(i)];
}

int NavNetz::knotenNamens(const std::string& name) const {
    for (std::size_t i = 1; i < knoten.size(); ++i) {
        if (!name.empty() && klein(knoten[i].name) == klein(name)) { return static_cast<int>(i); }
    }
    return 0;
}

namespace {

// --- Kanten und Nachbarn (ragl/graph_vs.h:649-850) ----------------------------
int verbinden(NavNetz& n, const NavKante& k, int a, int b) {
    // connect_node (graph_vs.h:696)
    if (a == b || a <= 0 || b <= 0) { return 0; }
    for (const NavNetz::Nachbar& x : n.links[static_cast<std::size_t>(a)]) {
        if (x.knoten == b) { return x.kante; }
    }
    for (const NavNetz::Nachbar& x : n.links[static_cast<std::size_t>(b)]) {
        if (x.knoten == a) { return x.kante; }
    }
    if (static_cast<int>(n.links[static_cast<std::size_t>(a)].size()) >= kEdgesPerNode ||
        static_cast<int>(n.links[static_cast<std::size_t>(b)].size()) >= kEdgesPerNode) {
        return 0;   // "adjacency list full"
    }
    if (static_cast<int>(n.kanten.size()) >= kNumEdges) { return 0; }   // "global edge pool full"
    const int e = static_cast<int>(n.kanten.size());
    n.kanten.push_back(k);
    n.kanteBenutzt.push_back(true);
    n.links[static_cast<std::size_t>(a)].push_back({static_cast<short>(e), static_cast<short>(b)});
    n.links[static_cast<std::size_t>(b)].push_back({static_cast<short>(e), static_cast<short>(a)});
    return e;
}
void kanteStreichen(NavNetz& n, int a, int b) {
    // remove_edge (graph_vs.h:808): erase_swap in beiden Listen
    for (int seite = 0; seite < 2; ++seite) {
        const int von = seite == 0 ? a : b;
        const int zu = seite == 0 ? b : a;
        auto& l = n.links[static_cast<std::size_t>(von)];
        for (std::size_t i = 0; i < l.size(); ++i) {
            if (l[i].knoten == zu) {
                if (l[i].kante != 0 && n.kanteBenutzt[static_cast<std::size_t>(l[i].kante)]) {
                    n.kanteBenutzt[static_cast<std::size_t>(l[i].kante)] = false;
                }
                if (i != l.size() - 1) { std::swap(l[i], l.back()); }
                l.pop_back();
                break;
            }
        }
    }
}
// CGraphUser::setup_edge (g_navigator.cpp:690)
NavKante neueKante(const NavNetz& n, int a, int b) {
    NavKante k;
    k.a = a;
    k.b = b;
    k.laenge = abstand(n.knoten[static_cast<std::size_t>(a)].punkt, n.knoten[static_cast<std::size_t>(b)].punkt);
    k.flags = NavKante::Gueltig;
    return k;
}

// --- Die Zellen fuellen (ragl/graph_vs.h:297-470) ------------------------------
struct SortEintrag {
    float kosten = 0.0F;
    short nummer = 0;
};
void zellenFuellen(NavNetz& n, bool kanten) {
    // I. jede Zelle mit dem, was in ihr liegt
    const std::size_t anzahl = kanten ? n.kanten.size() : n.knoten.size();
    for (std::size_t i = 1; i < anzahl; ++i) {
        if (kanten && !n.kanteBenutzt[i]) { continue; }
        NavVec mitte{};
        if (kanten) {
            const NavKante& k = n.kanten[i];
            mitte = mal(plus(n.knoten[static_cast<std::size_t>(k.a)].punkt, n.knoten[static_cast<std::size_t>(k.b)].punkt),
                        0.5F);
        } else {
            mitte = n.knoten[i].punkt;
        }
        const int z = zellIndex(n, mitte[0], mitte[1]);
        if (z < 0) { continue; }
        auto& liste = kanten ? n.zellen[static_cast<std::size_t>(z)].kanten : n.zellen[static_cast<std::size_t>(z)].knoten;
        liste.push_back(static_cast<short>(i));
        if (static_cast<int>(liste.size()) >= kNodesPerCell) { break; }   // "full" beendet die Schleife
    }
    // scale_by_largest_axis (grid_vs.h): range / groessere Zellweite
    float range = kCellRange;
    range /= (n.zMass[0] > n.zMass[1]) ? n.zMass[0] : n.zMass[1];
    const int iRange = static_cast<int>(range) + 1;
    std::vector<std::vector<short>> neu(static_cast<std::size_t>(kNumCells * kNumCells));
    std::vector<SortEintrag> sort;
    for (int x = 0; x < kNumCells; ++x) {
        for (int y = 0; y < kNumCells; ++y) {
            sort.clear();
            const float cx = static_cast<float>(x) * n.zMass[0] + n.zMin[0] + n.zMass[0] * 0.5F;
            const float cy = static_cast<float>(y) * n.zMass[1] + n.zMin[1] + n.zMass[1] * 0.5F;
            // riterator: Zeilen y von unten, darin x (grid_vs.h:420 ff.)
            const int x0 = std::max(0, x - iRange);
            const int x1 = std::min(kNumCells - 1, x + iRange);
            const int y0 = std::max(0, y - iRange);
            const int y1 = std::min(kNumCells - 1, y + iRange);
            for (int yy = y0; yy <= y1; ++yy) {
                for (int xx = x0; xx <= x1; ++xx) {
                    const NavNetz::Zelle& c = n.zellen[static_cast<std::size_t>(xx + yy * kNumCells)];
                    const auto& liste = kanten ? c.kanten : c.knoten;
                    for (std::size_t i = 0; i < liste.size() && static_cast<int>(sort.size()) < kNodesPerCell * 25; ++i) {
                        NavVec p{};
                        if (kanten) {
                            const NavKante& k = n.kanten[static_cast<std::size_t>(liste[i])];
                            p = mal(plus(n.knoten[static_cast<std::size_t>(k.a)].punkt,
                                         n.knoten[static_cast<std::size_t>(k.b)].punkt),
                                    0.5F);
                        } else {
                            p = n.knoten[static_cast<std::size_t>(liste[i])].punkt;
                        }
                        const float dx = cx - p[0];
                        const float dy = cy - p[1];
                        sort.push_back({dx * dx + dy * dy, liste[i]});
                    }
                }
            }
            heapSortieren(sort, [](const SortEintrag& a, const SortEintrag& b) { return a.kosten < b.kosten; });
            auto& ziel = neu[static_cast<std::size_t>(x + y * kNumCells)];
            for (std::size_t i = 0; i < sort.size() && static_cast<int>(i) < kNodesPerCell; ++i) {
                ziel.push_back(sort[i].nummer);
            }
        }
    }
    for (std::size_t i = 0; i < neu.size(); ++i) {
        (kanten ? n.zellen[i].kanten : n.zellen[i].knoten) = std::move(neu[i]);
    }
}

// --- is_valid / can_be_invalid / cost (g_navigator.cpp:531-688) ---------------
struct Akteur {
    bool da = false;          // mActor gesetzt
    int groesse = 1;          // NAV::ClassifyEntSize (1 MEDIUM, 2 LARGE)
    // rc601 (bewegung, teil 3): die Mover zur Zeit des Denkbilds (is_valid
    // liest moverState und contents zur Laufzeit) und ein Merker, ob eine
    // Tuer-/Wandkante gefragt wurde (fuer den Zwischenspeicher der Plaene).
    const NavMoverPlan* mover = nullptr;
    double t = 0.0;
    bool* gefragt = nullptr;
};
bool kanteGueltig(const NavKante& k, const Akteur& akt, int endpunkt) {
    if (akt.da && k.hat(NavKante::Fliegen)) { return false; }    // ohne SCF_NAV_CAN_FLY
    if (akt.da && k.hat(NavKante::Springen)) { return false; }   // Spruenge rechnet behaved nicht
    if (akt.da && k.groesse() < akt.groesse && endpunkt != -1) { return false; }
    if (k.entity != kNavKeineEntity) {
        if (k.hat(NavKante::TuerSperrt) || k.hat(NavKante::WandSperrt)) {
            // rc601 (bewegung, teil 3): is_valid zur Laufzeit (g_navigator.cpp:
            // 584-635 MD) mit dem Stand des Movers zu diesem Denkbild.
            if (akt.gefragt != nullptr) { *akt.gefragt = true; }
            const NavMoverPlan::Stand* s = akt.mover != nullptr ? akt.mover->standVon(k.entity, akt.t) : nullptr;
            if (k.hat(NavKante::TuerSperrt)) {
                // "const bool Closed = StartOpen ? ent->moverState == MOVER_POS2
                // : ent->moverState == MOVER_POS1;" - ohne Plan wie beim
                // Spawnen (InitMover: MOVER_POS1).
                const int zustand = s != nullptr ? s->zustand : 0;
                const bool zu = k.tuerStartOffen ? zustand == 1 : zustand == 0;
                if (!zu) { return true; }
                // Besitzer inaktiv/nur fuer den Spieler/USE_BUTTON (beim Bau
                // entschieden), die Tuer selbst als Besitzer verriegelt
                // (MOVER_LOCKED, zur Zeit), MOVER_GOODIE: NPCs haben keinen
                // Schluessel (INV_GoodieKeyCheck) - nur mit Akteur.
                const bool verriegelt = s != nullptr ? s->verriegelt : k.verriegeltAnfang;
                if (k.gesperrt || (k.besitzerIstTuer && verriegelt)) { return false; }
                if (akt.da && k.goodie) { return false; }
                return true;
            }
            // "If This Is A Wall, Check If It Has Contents Now"
            const bool fest = s != nullptr ? s->fest : k.gesperrt;
            return !fest;
        }
    }
    return k.hat(NavKante::Gueltig);
}
float kantenKosten(const NavNetz& n, const NavKante& k, int b) {
    // cost(Edge, B): ohne Gefahr (keine Alarme in Zwischensequenzen)
    if (n.knoten[static_cast<std::size_t>(b)].typ == NavKnoten::Wegpunkt) { return k.laenge; }
    return k.laenge + kBiasNonWaypoint;
}

// --- Regionen (ragl/graph_region.h) --------------------------------------------
void regionZuweisen(NavNetz& n, int knoten) {
    // assign() rekursiv ueber alle Knoten in Nummernfolge - hier mit Stapel,
    // in derselben Reihenfolge (Tiefe zuerst, je Ebene aufsteigend).
    std::vector<std::pair<int, int>> stapel;   // (Knoten, naechster zu pruefender Index)
    n.region[static_cast<std::size_t>(knoten)] = n.regionZahl;
    stapel.emplace_back(knoten, 0);
    while (!stapel.empty()) {
        auto& [k, i] = stapel.back();
        bool weiter = false;
        for (; i < kNumNodes; ++i) {
            if (i >= static_cast<int>(n.region.size())) { break; }
            if (n.region[static_cast<std::size_t>(i)] != -1) { continue; }
            const int e = n.kanteZwischen(k, i);
            if (e != 0 && !(e > 0 && n.kanten[static_cast<std::size_t>(e)].hat(NavKante::KannUngueltig))) {
                n.region[static_cast<std::size_t>(i)] = n.regionZahl;
                const int naechster = i;
                ++i;
                stapel.emplace_back(naechster, 0);
                weiter = true;
                break;
            }
        }
        if (!weiter) { stapel.pop_back(); }
    }
}
bool regionKanteGueltig(const NavNetz& n, int von, int ziel, const Akteur& akt, std::vector<bool>& zu) {
    zu[static_cast<std::size_t>(von)] = true;
    if (von == ziel) { return true; }
    for (int nr = 0; nr < n.regionZahl; ++nr) {
        const short e = n.regionLinks[static_cast<std::size_t>(von * kNumRegions + nr)];
        if (e == -1 || zu[static_cast<std::size_t>(nr)]) { continue; }
        if (von <= n.reserviert) {
            if (regionKanteGueltig(n, nr, ziel, akt, zu)) { return true; }
        } else if (e >= 0) {
            for (const short kante : n.regionKanten[static_cast<std::size_t>(e)]) {
                if (kanteGueltig(n.kanten[static_cast<std::size_t>(kante)], akt, nr == ziel ? -1 : 0)) {
                    if (regionKanteGueltig(n, nr, ziel, akt, zu)) { return true; }
                    break;
                }
            }
        }
    }
    return false;
}
bool gleicheRegionKante(const NavNetz& n, int a, int b, const Akteur& akt) {
    // has_valid_edge (graph_region.h:111)
    const int ra = n.region[static_cast<std::size_t>(a)];
    const int rb = n.region[static_cast<std::size_t>(b)];
    if (ra == rb) { return true; }
    if (ra < 0 || rb < 0) { return false; }
    // rc601 (bewegung, Durchsicht 2): das Ergebnis haengt nur am Netz, den
    // beiden Regionen und dem Akteur (ohne / Groesse) - gemerkt in
    // NavNetz::regionGueltig. Dieselbe Rechnung beim ersten Mal.
    const int r = n.regionZahl;
    std::atomic<signed char>* merk = nullptr;
    // rc601 (bewegung, teil 3): die Regionsverbindungen sind die Kanten, die
    // ungueltig werden koennen (Tueren, Waende) - ihre Gueltigkeit haengt am
    // Stand der Mover. Gemerkt wird nur fuer den Stand beim Spawnen.
    if (akt.gefragt != nullptr) { *akt.gefragt = true; }
    const bool wieAnfang = akt.mover == nullptr || akt.mover->kantenUnveraendert(akt.t);
    if (wieAnfang && gSchnellwege.load(std::memory_order_relaxed) && n.regionGueltig && ra < r && rb < r) {
        const int art = akt.da ? std::clamp(akt.groesse, 1, 2) : 0;
        const std::size_t i = (static_cast<std::size_t>(art) * static_cast<std::size_t>(r) + static_cast<std::size_t>(ra)) *
                                  static_cast<std::size_t>(r) +
                              static_cast<std::size_t>(rb);
        if (i < n.regionGueltig->size()) {
            merk = &(*n.regionGueltig)[i];
            const signed char v = merk->load(std::memory_order_relaxed);
            if (v != 0) { return v == 2; }
        }
    }
    std::vector<bool> zu(static_cast<std::size_t>(kNumRegions), false);
    const bool ja = regionKanteGueltig(n, ra, rb, akt, zu);
    if (merk != nullptr) { merk->store(static_cast<signed char>(ja ? 2 : 1), std::memory_order_relaxed); }
    return ja;
}

}  // namespace

// =============================================================================
// baueNavNetz
// =============================================================================
std::shared_ptr<NavNetz> baueNavNetz(const std::vector<MapEntity>& ents, const NavBauHilfe& h) {
    auto netz = std::make_shared<NavNetz>();
    NavNetz& n = *netz;
    n.knoten.resize(1);
    n.kanten.resize(1);
    n.kanteBenutzt.assign(1, false);
    n.links.resize(1);
    n.zellen.assign(static_cast<std::size_t>(kNumCells * kNumCells), {});
    const bool mitSpur = static_cast<bool>(h.spur);
    // rc601 (bewegung, teil 3): die Brush-Entities so, wie die Engine sie zur
    // Zeit jeder Spur hat. Beim Spawnen eines Wegpunkts (SP_waypoint ->
    // G_CheckInSolid, waypoint_getRadius, NAV::SpawnedPoint; g_nav.cpp:162-195
    // MD) gibt es nur die Entities VOR ihm (G_SpawnEntitiesFromString spawnt in
    // Kartenfolge); eine Tuer mit START_OPEN steht dann auf pos1 (InitMover,
    // g_mover.cpp:1171-1172 MD), func_wall/func_usable mit START_OFF haben
    // keine contents. Den Stand beim Netzbau (PHASE 0) hat h.spur.
    const bool zeitgenau = mitSpur && static_cast<bool>(h.spurVersetzt) && static_cast<bool>(h.tuerStellungen);
    std::vector<int> pinselEnts;
    NavVersetzt startOffen;
    std::vector<int> startAus;
    if (zeitgenau) {
        for (std::size_t i = 0; i < ents.size(); ++i) {
            const std::string mdl = text(ents[i], "model");
            if (mdl.size() < 2 || mdl[0] != '*') { continue; }
            pinselEnts.push_back(static_cast<int>(i));
            const std::string kl = klein(ents[i].classname);
            const int sf = ganz(ents[i], "spawnflags");
            if ((sf & 1) == 0) { continue; }
            NavVec p1{};
            NavVec p2{};
            if (kl == "func_door" && h.tuerStellungen(static_cast<int>(i), p1, p2)) {
                startOffen.push_back(NavVersetztEintrag{static_cast<int>(i), p1, true});
            } else if (kl == "func_wall" || kl == "func_usable") {
                startAus.push_back(static_cast<int>(i));
            }
        }
    }
    const auto spawnSpur = [&](int ich) -> NavSpurFn {
        if (!zeitgenau) { return h.spur; }
        NavVersetzt v;
        for (const int b : pinselEnts) {
            if (b > ich) { v.push_back(NavVersetztEintrag{b, {}, false}); }   // noch nicht gespawnt
        }
        for (const NavVersetztEintrag& o : startOffen) {
            if (o.entity < ich) { v.push_back(o); }
        }
        for (const int w : startAus) {
            if (w < ich) { v.push_back(NavVersetztEintrag{w, {}, false}); }
        }
        return [&h, v](const NavVec& a, const NavVec& mi, const NavVec& ma, const NavVec& b, int ign, std::uint32_t m) {
            return h.spurVersetzt(a, mi, ma, b, ign, m, v);
        };
    };

    // G_CheckInSolid (g_utils.cpp:1641-1674): Kasten ab dem Ursprung um
    // mins[2] hinab (mins[2] dabei 0); mit fix auf das Ende gesetzt und noch
    // einmal ohne fix.
    const auto imFesten = [&](NavVec& ort, const NavVec& mins, const NavVec& maxs, bool fix, int ich,
                              std::uint32_t maske) {
        if (!mitSpur) { return false; }
        NavVec m0 = mins;
        m0[2] = 0.0F;
        NavVec ende = ort;
        ende[2] += mins[2];
        const NavSpurFn spur = spawnSpur(ich);   // rc601 (bewegung, teil 3)
        NavSpur s = spur(ort, m0, maxs, ende, ich, maske);
        if (s.allsolid || s.startsolid) { return true; }
        if (s.fraction < 1.0F) {
            if (fix) {
                ort = s.endpos;
                ort[2] -= mins[2];
                ende = ort;
                ende[2] += mins[2];
                s = spur(ort, m0, maxs, ende, ich, maske);
                return s.allsolid || s.startsolid || s.fraction < 1.0F;
            }
            return true;
        }
        return false;
    };

    // NAV::SpawnedPoint (g_navigator.cpp:2218)
    const auto gesetzt = [&](const MapEntity& e, int idx, const NavVec& ort, float radius, int typ) {
        // "Max Nav points reached" (g_navigator.cpp:2220 MD: size_nodes() >=
        // NUM_NODES). rc602 kartenpruefung: size_nodes() ist mNodes.size()-1
        // (graph_vs.h:572), und der Pool fasst NUM_NODES Plaetze EINSCHLIESSLICH
        // des Platzhalters [0] (pool_vs<TNODE, MAXNODES>, graph_vs.h:211/540) -
        // er ist schon bei 1023 echten Punkten voll; der 1024. liefe im Spiel
        // in ein volles mNodes.alloc() (assert). Vorher nahm behaved 1024 echte
        // Knoten und schrieb hinter n.region (NUM_NODES Plaetze) - Absturz beim
        // Wechsel auf duel_jt_outside (1100 Wegpunkte).
        if (static_cast<int>(n.knoten.size()) >= kNumNodes) { return; }
        if (mitSpur) {
            NavVec stop = ort;
            stop[2] += 5.0F;
            if (!gehFrei(spawnSpur(idx), ort, stop, {-kScMediumRadius, -kScMediumRadius, 0.0F},
                         {kScMediumRadius, kScMediumRadius, kScMediumHeight}, kNavKeineEntity, true, false, false)) {
                return;   // "ERROR: Nav(%d) in solid"
            }
        }
        NavKnoten k;
        k.punkt = ort;
        k.radius = radius;
        k.typ = typ;
        const int sf = ganz(e, "spawnflags");
        k.zumBoden = typ == NavKnoten::Wegpunkt && (sf & 2) != 0;
        k.keinAuto = (sf & 4) != 0;
        static const char* const kZiele[kNumTargets] = {"target", "target2", "target3", "target4", "targetJump"};
        for (int z = 0; z < kNumTargets; ++z) { k.ziele[static_cast<std::size_t>(z)] = text(e, kZiele[z]); }
        k.name = e.targetname;
        k.entity = idx;
        n.knoten.push_back(std::move(k));
        n.links.emplace_back();
        gitterErweitern(n, ort[0], ort[1]);
    };

    for (std::size_t i = 0; i < ents.size(); ++i) {
        const MapEntity& e = ents[i];
        const std::string k = klein(e.classname);
        if (e.origin.empty()) { continue; }
        NavVec ort{};
        {
            std::istringstream is(e.origin);
            is >> ort[0] >> ort[1] >> ort[2];
        }
        const int sf = ganz(e, "spawnflags");
        const int ich = static_cast<int>(i);
        if (k == "waypoint") {
            // SP_waypoint (g_nav.cpp:169-200)
            NavVec mins{-kDefaultMaxs0, -kDefaultMaxs0, kDefaultMins2};
            NavVec maxs{kDefaultMaxs0, kDefaultMaxs0, kDefaultMaxs2};
            if ((sf & 2) != 0) { ort[2] += 128.0F; }
            if ((sf & 1) == 0 && imFesten(ort, mins, maxs, true, ich, kNavMaskDeadSolid)) {
                maxs[2] = kCrouchMaxs2;
                if (imFesten(ort, mins, maxs, true, ich, kNavMaskDeadSolid)) { continue; }   // "Waypoint in solid!"
            }
            // waypoint_getRadius (g_nav.cpp:139-151)
            float minDist = kMaxRadiusCheck + 1.0F;
            if (mitSpur) {
                const NavSpurFn spur = spawnSpur(ich);   // rc601 (bewegung, teil 3)
                const NavVec tmins{-kDefaultMaxs0, -kDefaultMaxs0, kDefaultMins2 + kStepSize};
                const NavVec tmaxs{kDefaultMaxs0, kDefaultMaxs0, kDefaultMaxs2};
                for (int y = 0; y < kYawIterations; ++y) {
                    const float yaw = 360.0F / static_cast<float>(kYawIterations) * static_cast<float>(y);
                    const float w = yaw / kGrad;
                    const NavVec ziel{ort[0] + std::cos(w) * minDist, ort[1] + std::sin(w) * minDist, ort[2]};
                    const NavSpur s = spur(ort, tmins, tmaxs, ziel, kNavKeineEntity,
                                             kNavSolid | kNavMonsterClip | kNavBotClip);
                    const float d = minDist * s.fraction;
                    if (d < minDist) { minDist = d; }
                }
            }
            gesetzt(e, ich, ort, minDist + kDefaultMaxs0, NavKnoten::Wegpunkt);
        } else if (k == "waypoint_small") {
            // SP_waypoint_small (g_nav.cpp:206-238)
            NavVec mins{-2.0F, -2.0F, kDefaultMins2};
            NavVec maxs{2.0F, 2.0F, kDefaultMaxs2};
            if ((sf & 1) == 0 && imFesten(ort, mins, maxs, true, ich, kNavMaskDeadSolid)) {
                maxs[2] = kCrouchMaxs2;
                if (imFesten(ort, mins, maxs, true, ich, kNavMaskDeadSolid)) { continue; }
            }
            gesetzt(e, ich, ort, 2.0F, NavKnoten::Wegpunkt);
        } else if (k == "waypoint_navgoal") {
            // SP_waypoint_navgoal (g_nav.cpp:253-279): der Knoten am
            // currentOrigin (ohne die +0.125, die nur s.origin und der TAG
            // bekommen), Radius ent->radius (Schluessel "radius", sonst 0).
            gesetzt(e, ich, ort, zahl(e, "radius"), NavKnoten::Ziel);
        } else if (k == "point_combat") {
            // SP_point_combat (NPC_combat.cpp:3347-3379): +0.125, mins/maxs 0
            ort[2] += 0.125F;
            gesetzt(e, ich, ort, zahl(e, "radius"), NavKnoten::Kampf);
        }
    }

    // --- NAV::LoadFromEntitiesAndSaveToFile (g_navigator.cpp:1469) ------------
    const int zahlKnoten = static_cast<int>(n.knoten.size()) - 1;
    n.region.assign(static_cast<std::size_t>(kNumNodes), -1);
    if (zahlKnoten <= 3) {
        n.kantenZahl = 0;
        return netz;   // "if (mGraph.size_nodes() <= 3) return true;" - kein Netz
    }
    // PHASE 0 (Tueren mit START_OPEN zu, Waende an, g_navigator.cpp:1482-1533
    // MD): so spurt h.spur (rc601 bewegung, teil 3 - vorher die Entities an
    // ihrem Platz beim Laden; START_OFF-Waende fehlten den Kanten ganz).
    zellenFuellen(n, false);

    // Drop To Floor And Mark Floating
    if (mitSpur) {
        for (std::size_t i = 1; i < n.knoten.size(); ++i) {
            NavKnoten& at = n.knoten[i];
            const NavVec dach = at.punkt;
            NavVec boden = at.punkt;
            if (at.zumBoden) { boden[2] -= kMaxEdgeFloorDist; }
            boden[2] -= kMaxEdgeFloorDist * 1.5F;
            NavSpur s;
            const bool amBoden = !sichtFrei(h.spur, dach, boden, false, &s);
            if (at.zumBoden) {
                at.punkt = s.endpos;
                at.punkt[2] += 5.0F;
            } else if (!amBoden && (at.typ == NavKnoten::Wegpunkt || at.typ == NavKnoten::Ziel)) {
                at.schwebend = true;
            }
        }
    }

    std::map<std::string, std::vector<int>> namen;   // TNameToNodeMap (hstring: mit Gross/klein)
    for (std::size_t i = 1; i < n.knoten.size(); ++i) {
        if (!n.knoten[i].name.empty()) { namen[n.knoten[i].name].push_back(static_cast<int>(i)); }
    }
    for (std::size_t ai = 1; ai < n.knoten.size(); ++ai) {
        const int at = static_cast<int>(ai);
        // Connect To Hand Designed Targets
        for (int z = 0; z < kNumTargets; ++z) {
            const std::string ziel = n.knoten[ai].ziele[static_cast<std::size_t>(z)];
            if (ziel.empty()) { continue; }
            n.knoten[ai].ziele[static_cast<std::size_t>(z)].clear();
            const auto it = namen.find(ziel);
            if (it == namen.end()) { continue; }   // "nav unable to locate target"
            for (const int tgt : it->second) {
                if (at == tgt) { continue; }
                const int e = n.kanteZwischen(at, tgt);
                if (e != 0) {
                    if (e > 0) {
                        if (z == kNumTargets - 1) { n.kanten[static_cast<std::size_t>(e)].flags |= NavKante::Springen; }
                        n.kanten[static_cast<std::size_t>(e)].flags |= NavKante::VomDesigner;
                    }
                    continue;
                }
                NavKante k = neueKante(n, at, tgt);
                k.flags |= NavKante::VomDesigner;
                if (z == kNumTargets - 1) { k.flags |= NavKante::Springen; }
                verbinden(n, k, at, tgt);
            }
        }
        // Auto Connect: Kampf- und Zielpunkte zu den naechsten Wegpunkten
        const NavKnoten& atK = n.knoten[ai];
        if (atK.keinAuto || !(atK.typ == NavKnoten::Kampf || atK.typ == NavKnoten::Ziel)) { continue; }
        const NavNetz::Zelle* zelle = n.zelleBei(atK.punkt[0], atK.punkt[1]);
        if (zelle == nullptr) { continue; }
        struct Vergleich {   // ratl::ratl_compare
            float kosten = 0.0F;
            int nummer = 0;
        };
        std::array<Vergleich, kMinWayNeighbors> naechste{};
        int hoechste = 0;
        int nichtWp = 0;
        for (const short tgtS : zelle->knoten) {
            const int tgt = tgtS;
            if (tgt == at || n.kanteZwischen(at, tgt) != 0) { continue; }
            const NavKnoten& nk = n.knoten[static_cast<std::size_t>(tgt)];
            if (std::fabs(nk.punkt[2] - atK.punkt[2]) > kMaxEdgeFloorDist) { continue; }
            const float kosten = abstand(nk.punkt, atK.punkt);
            if (kosten > kMaxEdgeAutoLen) { continue; }
            if (nk.typ == NavKnoten::Kampf || nk.typ == NavKnoten::Ziel) {
                ++nichtWp;
                if (nichtWp > kMaxNonWpNeighbors) { continue; }
                if (kosten > kMaxEdgeAutoLen / 2.0F) { continue; }
            }
            if (naechste[static_cast<std::size_t>(hoechste)].nummer != 0 &&
                naechste[static_cast<std::size_t>(hoechste)].kosten < kosten) {
                continue;
            }
            if (h.pvs && !h.pvs(nk.punkt, atK.punkt)) { continue; }
            naechste[static_cast<std::size_t>(hoechste)].nummer = tgt;
            naechste[static_cast<std::size_t>(hoechste)].kosten = abstand(nk.punkt, atK.punkt);
            for (int i = 0; i < kMinWayNeighbors; ++i) {
                if (naechste[static_cast<std::size_t>(i)].nummer == 0) {
                    hoechste = i;
                    break;
                }
                if (naechste[static_cast<std::size_t>(i)].kosten > naechste[static_cast<std::size_t>(hoechste)].kosten) {
                    hoechste = i;
                }
            }
        }
        for (const Vergleich& v : naechste) {
            if (v.nummer != 0) { verbinden(n, neueKante(n, at, v.nummer), at, v.nummer); }
        }
    }

    // rc601 (bewegung, teil 3, B6): Ketten wie G_FindTeams (g_main.cpp:674-722
    // MD): der erste mit einem "team" ist Meister, jeder spaetere mit demselben
    // (strcmp) wird Sklave und vorn in die Kette gehaengt; sein targetname geht
    // an den Meister ("make sure that targets only point at the master").
    std::vector<int> meisterVon(ents.size(), -1);
    std::vector<std::vector<int>> kette(ents.size());
    std::vector<std::string> teamZiel(ents.size());
    for (std::size_t i = 0; i < ents.size(); ++i) { teamZiel[i] = ents[i].targetname; }
    for (std::size_t i = 0; i < ents.size(); ++i) {
        const std::string team = text(ents[i], "team");
        if (team.empty() || meisterVon[i] >= 0) { continue; }
        meisterVon[i] = static_cast<int>(i);
        kette[i].push_back(static_cast<int>(i));
        for (std::size_t j = i + 1; j < ents.size(); ++j) {
            if (meisterVon[j] >= 0 || text(ents[j], "team") != team) { continue; }
            kette[i].insert(kette[i].begin() + 1, static_cast<int>(j));
            meisterVon[j] = static_cast<int>(i);
            if (!teamZiel[j].empty()) {
                teamZiel[i] = teamZiel[j];
                teamZiel[j].clear();
            }
        }
    }

    // PHASE III: Kanten pruefen (TestEdge, g_navigator.cpp:1197-1466)
    const auto pruefe = [&](int e) -> bool {
        NavKante& kante = n.kanten[static_cast<std::size_t>(e)];
        const NavKnoten& a = n.knoten[static_cast<std::size_t>(kante.a)];
        const NavKnoten& b = n.knoten[static_cast<std::size_t>(kante.b)];
        const bool gross = kante.groesse() == 2;
        const float radius = gross ? kScLargeRadius : kScMediumRadius;
        const float hoehe = gross ? kScLargeHeight : kScMediumHeight;
        const NavVec mins{-radius, -radius, 0.0F};
        const NavVec maxs{radius, radius, hoehe};
        if ((a.typ == NavKnoten::Wegpunkt && a.radius < radius) || (b.typ == NavKnoten::Wegpunkt && b.radius < radius)) {
            return false;   // "Size Too Big"
        }
        if (!mitSpur) { return true; }
        NavSpur s;
        bool kannGehen = gehFrei(h.spur, a.punkt, b.punkt, mins, maxs, kNavKeineEntity, true, false, false, &s);
        int getroffen = s.entity;
        if (a.schwebend || b.schwebend) {
            kante.flags |= NavKante::Fliegen;
            if (!a.schwebend || !b.schwebend) { kante.flags |= NavKante::KannUngueltig; }
        }
        if (!kannGehen && !s.startsolid && getroffen >= 0 && getroffen < static_cast<int>(ents.size())) {
            const MapEntity& ent = ents[static_cast<std::size_t>(getroffen)];
            const std::string kl = klein(ent.classname);
            if (kl == "func_door") {
                kante.flags |= NavKante::TuerSperrt;
            } else if (kl == "func_wall" || kl == "func_static" || kl == "func_usable") {
                kante.flags |= NavKante::WandSperrt;
            } else if (kl == "func_glass" || kl == "func_breakable" || kl == "misc_model_breakable") {
                kante.flags |= NavKante::ZerbrechlichSperrt;
            } else {
                return kannGehen;   // "Unable To Ignore Ent, Going A Size Down"
            }
            int besitzEnt = getroffen;   // "ent" der Engine, nach einer Tuer der Meister
            if (kante.hat(NavKante::TuerSperrt)) {
                // rc601 (bewegung, teil 3, B6): wie g_navigator.cpp:1313-1352 MD -
                // den Meister der Kette suchen, die ganze Kette oeffnen ("door_is_
                // start_open ? slave->pos1 : slave->pos2"), MIT allen Entities
                // noch einmal spuren, dann die Kette "schliessen" (pos1 bzw.
                // pos2). Bis teil 3 wurde hier nur die getroffene Tuer
                // uebergangen - eine Kette aus zwei Fluegeln oder eine Tuer,
                // die auch offen im Weg steht, kam anders heraus.
                const int meister = meisterVon[static_cast<std::size_t>(getroffen)] >= 0
                                        ? meisterVon[static_cast<std::size_t>(getroffen)]
                                        : getroffen;
                const bool startOffen = (ganz(ents[static_cast<std::size_t>(meister)], "spawnflags") & 1) != 0;
                NavVersetzt offen;
                bool alle = static_cast<bool>(h.tuerStellungen) && static_cast<bool>(h.spurVersetzt);
                const std::vector<int> glieder =
                    kette[static_cast<std::size_t>(meister)].empty() ? std::vector<int>{meister}
                                                                      : kette[static_cast<std::size_t>(meister)];
                for (const int glied : glieder) {
                    NavVec p1{};
                    NavVec p2{};
                    if (!alle || !h.tuerStellungen(glied, p1, p2)) {
                        alle = false;
                        break;
                    }
                    offen.push_back(NavVersetztEintrag{glied, startOffen ? p1 : p2, true});
                }
                if (alle) {
                    // MoveTrace(a.mPoint, b.mPoint, mins, maxs, 0, true, false)
                    const NavSpurFn versetzt = [&h, &offen](const NavVec& s0, const NavVec& mi, const NavVec& ma,
                                                            const NavVec& e0, int ign, std::uint32_t m) {
                        return h.spurVersetzt(s0, mi, ma, e0, ign, m, offen);
                    };
                    // Danach "Close the door again" (pos2 bzw. pos1) - das
                    // ist wieder der Stand von PHASE 0, mit dem `spur` spurt.
                    kannGehen = gehFrei(versetzt, a.punkt, b.punkt, mins, maxs, kNavKeineEntity, true, false, false);
                } else {
                    kannGehen = gehFrei(h.spur, a.punkt, b.punkt, mins, maxs, getroffen, true, false, false);
                }
                if (kannGehen) {
                    getroffen = meister;   // "ent = master; ent_hit = master->s.number;"
                    besitzEnt = meister;
                }
            } else if (kante.hat(NavKante::ZerbrechlichSperrt)) {
                kannGehen = true;
            } else {
                kannGehen = gehFrei(h.spur, a.punkt, b.punkt, mins, maxs, getroffen, true, false, false);
            }
            if (kannGehen) {
                kante.entity = getroffen;
                kante.flags |= NavKante::KannUngueltig;
                if (kante.hat(NavKante::TuerSperrt)) {
                    // rc601 (bewegung, teil 3, B6): der Besitzer wie
                    // g_navigator.cpp:1406-1444 MD. "ent" ist jetzt der Meister
                    // mit dem targetname nach G_FindTeams.
                    const MapEntity& tuer = ents[static_cast<std::size_t>(besitzEnt)];
                    const std::string& tn = teamZiel[static_cast<std::size_t>(besitzEnt)];
                    const int tsf = ganz(tuer, "spawnflags");
                    int besitzer = besitzEnt;      // "at.mOwnerNum = ent->s.number;"
                    bool ausloeserTuer = false;    // trigger_door (G_FindDoorTrigger)
                    const auto istAusloeser = [&](int j) {
                        const std::string kj = klein(ents[static_cast<std::size_t>(j)].classname);
                        return kj == "trigger_multiple" || kj == "trigger_once";
                    };
                    // G_Find(from, fieldofs, match): ab der Entity nach `from`,
                    // Q_stricmp.
                    const auto finde = [&](int ab, const char* feld) {
                        for (std::size_t j = static_cast<std::size_t>(ab + 1); j < ents.size(); ++j) {
                            if (klein(text(ents[j], feld)) == klein(tn)) { return static_cast<int>(j); }
                        }
                        return -1;
                    };
                    if (!tn.empty()) {
                        int o = finde(-1, "target");
                        if (o >= 0 && istAusloeser(o)) {
                            besitzer = o;
                        } else {
                            o = finde(o, "target2");   // G_Find(owner, FOFS(target2), ...) - nach dem ersten Fund
                            if (o >= 0 && istAusloeser(o)) { besitzer = o; }
                        }
                    } else {
                        // G_FindDoorTrigger (g_mover.cpp:1373-1416 MD): der
                        // trigger_door des Meisters. Den spawnt SP_func_door
                        // (g_mover.cpp:1599-1611 MD) fuer einen verriegelten
                        // Meister oder einen ohne targetname/health/PLAYER_USE/
                        // FORCE_ACTIVATE.
                        const bool gesundheit = ganz(tuer, "health") != 0;
                        ausloeserTuer = (tsf & 16) != 0 || !(gesundheit || (tsf & 64) != 0 || (tsf & 2) != 0);
                    }
                    kante.besitzer = ausloeserTuer ? kNavKeineEntity : besitzer;
                    kante.tuerStartOffen = (tsf & 1) != 0;
                    // is_valid (g_navigator.cpp:596-622 MD) fuer eine
                    // GESCHLOSSENE Tuer: der Teil, der sich nicht aendert.
                    if (ausloeserTuer) {
                        // trigger_door: spawnflags und svFlags 0.
                        kante.gesperrt = false;
                    } else if (besitzer == besitzEnt) {
                        // SVF_INACTIVE (InitMover: MOVER_INACTIVE 128),
                        // MOVER_PLAYER_USE 64, MOVER_FORCE_ACTIVATE 2; MOVER_LOCKED
                        // 16 zur Zeit (verriegelt), MOVER_GOODIE 32 nur mit Akteur.
                        kante.gesperrt = (tsf & (128 | 64 | 2)) != 0;
                        kante.besitzerIstTuer = true;
                        kante.verriegeltAnfang = (tsf & 16) != 0;
                        kante.goodie = (tsf & 32) != 0;
                    } else {
                        // trigger_multiple/_once: SVF_INACTIVE (InitTrigger,
                        // g_trigger.cpp:50-53 MD: spawnflags 128), PLAYERONLY 1,
                        // USE_BUTTON 4; MOVER_GOODIE 32.
                        const int osf = ganz(ents[static_cast<std::size_t>(besitzer)], "spawnflags");
                        kante.gesperrt = (osf & (128 | 1 | 4)) != 0;
                        kante.goodie = (osf & 32) != 0;
                    }
                } else if (kante.hat(NavKante::ZerbrechlichSperrt)) {
                    kante.flags &= ~NavKante::Gueltig;   // "not valid until broken"
                } else {
                    // Wand: gueltig, solange sie nicht fest ist
                    // (!(ent->contents & CONTENTS_SOLID)): func_wall und
                    // func_usable mit START_OFF sind es nicht.
                    const bool aus = (kl == "func_wall" || kl == "func_usable") && (ganz(ent, "spawnflags") & 1) != 0;
                    kante.gesperrt = !aus;
                }
            }
        }
        return kannGehen;
    };
    std::vector<int> streichen;
    for (std::size_t e = 1; e < n.kanten.size(); ++e) {
        if (!n.kanteBenutzt[e]) { continue; }
        NavKante& kante = n.kanten[e];
        if (kante.hat(NavKante::Springen)) {
            kante.flags |= NavKante::GroesseGross | NavKante::KannUngueltig | NavKante::VomDesigner;
            continue;
        }
        kante.flags |= NavKante::GroesseGross;
        bool kann = pruefe(static_cast<int>(e));
        if (!kann) {
            kante.flags &= ~NavKante::GroesseGross;
            kante.flags |= NavKante::GroesseMittel;
            kann = pruefe(static_cast<int>(e));
        }
        if (!kann) { streichen.push_back(static_cast<int>(e)); }
    }
    for (const int e : streichen) {
        const NavKante k = n.kanten[static_cast<std::size_t>(e)];
        kanteStreichen(n, k.a, k.b);
    }
    // Detect Point Islands
    for (std::size_t i = 1; i < n.knoten.size(); ++i) {
        if (n.links[i].empty()) { n.knoten[i].insel = true; }
    }
    // PHASE IV: Regionen (Insel-Region reserviert, dann find_regions)
    n.regionZahl = 0;
    n.reserviert = 0;
    ++n.reserviert;
    ++n.regionZahl;
    const int inselRegion = n.regionZahl;
    for (std::size_t i = 1; i < n.knoten.size(); ++i) {
        if (n.knoten[i].insel) { n.region[i] = inselRegion; }
    }
    for (std::size_t i = 1; i < n.knoten.size(); ++i) {
        if (n.region[i] == -1) {
            if (n.regionZahl >= kNumRegions - 1) { break; }   // "Too Many Regions!"
            ++n.regionZahl;
            regionZuweisen(n, static_cast<int>(i));
        }
    }
    ++n.regionZahl;   // "Size is actually 1 greater than the number of regions"
    n.regionLinks.assign(static_cast<std::size_t>(kNumRegions * kNumRegions), static_cast<short>(-1));
    // rc601 (bewegung, Durchsicht 2): find_region_edges laeuft ueber alle
    // Knotenpaare (a, b) und nimmt die mit einer Kante - hier ueber die
    // Nachbarn von a, aufsteigend nach b: dieselben Paare in derselben
    // Reihenfolge, ohne N*N Mal get_edge_across.
    std::vector<int> nachbarB;
    for (int a = 0; a < static_cast<int>(n.knoten.size()); ++a) {
        const int ra = n.region[static_cast<std::size_t>(a)];
        if (ra == -1) { continue; }
        nachbarB.clear();
        if (gSchnellwege.load(std::memory_order_relaxed)) {
            if (a < static_cast<int>(n.links.size())) {
                for (const NavNetz::Nachbar& x : n.links[static_cast<std::size_t>(a)]) { nachbarB.push_back(x.knoten); }
            }
            std::sort(nachbarB.begin(), nachbarB.end());
            nachbarB.erase(std::unique(nachbarB.begin(), nachbarB.end()), nachbarB.end());
        } else {
            for (int b = 0; b < static_cast<int>(n.knoten.size()); ++b) {
                if (n.kanteZwischen(a, b) != 0) { nachbarB.push_back(b); }
            }
        }
        for (const int b : nachbarB) {
            if (b < 0 || b >= static_cast<int>(n.knoten.size())) { continue; }
            const int rb = n.region[static_cast<std::size_t>(b)];
            const bool reserviert = ra <= n.reserviert || rb <= n.reserviert;
            if (rb == -1 || rb == ra || n.kanteZwischen(a, b) == 0) { continue; }
            short& link = n.regionLinks[static_cast<std::size_t>(ra * kNumRegions + rb)];
            if (link == -1) {
                if (reserviert) {
                    link = -2;
                    n.regionLinks[static_cast<std::size_t>(rb * kNumRegions + ra)] = -2;
                } else {
                    const auto nr = static_cast<short>(n.regionKanten.size());
                    n.regionKanten.push_back({static_cast<short>(n.kanteZwischen(a, b))});
                    link = nr;
                    n.regionLinks[static_cast<std::size_t>(rb * kNumRegions + ra)] = nr;
                }
            } else if (!reserviert && link >= 0) {
                n.regionKanten[static_cast<std::size_t>(link)].push_back(static_cast<short>(n.kanteZwischen(a, b)));
            }
        }
    }
    // PHASE V: die Kanten in die Zellen
    zellenFuellen(n, true);
    n.kantenZahl = n.zahlKanten();
    // rc601 (bewegung, Durchsicht 2): das Gedaechtnis fuer has_valid_edge.
    if (n.regionZahl > 0) {
        const auto r = static_cast<std::size_t>(n.regionZahl);
        n.regionGueltig = std::make_shared<std::vector<std::atomic<signed char>>>(3U * r * r);
    }
    return netz;
}

// =============================================================================
// A* (ragl/graph_vs.h:1516, handle_heap, search_node)
// =============================================================================
namespace {
struct SuchKnoten {
    int knoten = -1;
    int elternBesuch = -1;
    float zumZiel = -1.0F;
    float vomStart = 0.0F;
    [[nodiscard]] float schaetzung() const { return vomStart + zumZiel; }
};
// operator<: cost_estimate() > t.cost_estimate() - der Heap hat das
// GUENSTIGSTE oben.
bool heapKleiner(const SuchKnoten& a, const SuchKnoten& b) { return a.schaetzung() > b.schaetzung(); }

struct OffeneListe {
    std::vector<SuchKnoten> daten;
    std::vector<int> pos;   // je Knoten, -1 nicht drin
    explicit OffeneListe(std::size_t knoten) : pos(knoten, -1) {}
    [[nodiscard]] bool benutzt(int h) const {
        const int p = pos[static_cast<std::size_t>(h)];
        return p != -1 && daten[static_cast<std::size_t>(p)].knoten == h;
    }
    void tauschen(int a, int b) {
        if (a == b) { return; }
        pos[static_cast<std::size_t>(daten[static_cast<std::size_t>(a)].knoten)] = b;
        pos[static_cast<std::size_t>(daten[static_cast<std::size_t>(b)].knoten)] = a;
        std::swap(daten[static_cast<std::size_t>(a)], daten[static_cast<std::size_t>(b)]);
    }
    void hoch(int p) {
        while (p != 0 && heapKleiner(daten[static_cast<std::size_t>((p - 1) / 2)], daten[static_cast<std::size_t>(p)])) {
            tauschen((p - 1) / 2, p);
            p = (p - 1) / 2;
        }
    }
    [[nodiscard]] int groesstesKind(int i) const {
        const int n = static_cast<int>(daten.size());
        const int l = 2 * i + 1;
        const int r = 2 * i + 2;
        if (l < n) {
            if (r < n) {
                return heapKleiner(daten[static_cast<std::size_t>(r)], daten[static_cast<std::size_t>(l)]) ? l : r;
            }
            return l;
        }
        return i;
    }
    void runter(int p) {
        int k = groesstesKind(p);
        while (k != p && heapKleiner(daten[static_cast<std::size_t>(p)], daten[static_cast<std::size_t>(k)])) {
            tauschen(k, p);
            p = k;
            k = groesstesKind(p);
        }
    }
    void push(const SuchKnoten& s) {
        daten.push_back(s);
        pos[static_cast<std::size_t>(s.knoten)] = static_cast<int>(daten.size()) - 1;
        hoch(static_cast<int>(daten.size()) - 1);
    }
    void pop() {
        const int letzte = static_cast<int>(daten.size()) - 1;
        tauschen(0, letzte);
        pos[static_cast<std::size_t>(daten[static_cast<std::size_t>(letzte)].knoten)] = -1;
        daten.pop_back();
        if (!daten.empty()) { runter(0); }
    }
    void neuSortieren(int h) {
        const int p = pos[static_cast<std::size_t>(h)];
        hoch(p);
        runter(pos[static_cast<std::size_t>(h)]);
    }
};

// Ergebnis: die Knoten vom ZIEL zum Start (path_begin .. path_inc), leer ohne Erfolg.
std::vector<int> aStern(const NavNetz& n, int start, int ende, const Akteur& akt) {
    const std::size_t zahl = n.knoten.size();
    std::vector<SuchKnoten> besucht;
    std::vector<int> knotenBesuch(zahl, -1);
    std::vector<bool> zu(zahl, false);
    OffeneListe offen(zahl);
    int vorher = -1;   // mPrevIndex
    // get_next() fuer den Start
    SuchKnoten next;
    next.knoten = start;
    next.zumZiel = 0.0F;
    next.vomStart = 0.0F;
    next.elternBesuch = -1;
    zu[static_cast<std::size_t>(start)] = true;
    offen.push(next);
    const auto erfolg = [&]() { return ende != 0 && vorher != -1 && vorher == ende; };
    while (!offen.daten.empty() && !erfolg()) {
        // visit(open.top())
        const SuchKnoten oben = offen.daten[0];
        vorher = oben.knoten;
        besucht.push_back(oben);
        knotenBesuch[static_cast<std::size_t>(vorher)] = static_cast<int>(besucht.size()) - 1;
        zu[static_cast<std::size_t>(vorher)] = true;
        next.elternBesuch = static_cast<int>(besucht.size()) - 1;
        offen.pop();
        for (const NavNetz::Nachbar& nb : n.links[static_cast<std::size_t>(vorher)]) {
            const int kante = nb.kante;
            if (kante == -1 || kanteGueltig(n.kanten[static_cast<std::size_t>(kante)], akt, ende)) {
                const int ni = nb.knoten;
                next.knoten = ni;
                next.zumZiel = abstand(n.knoten[static_cast<std::size_t>(ni)].punkt, n.knoten[static_cast<std::size_t>(ende)].punkt);
                next.vomStart = kantenKosten(n, n.kanten[static_cast<std::size_t>(kante)], ni);
                if (next.elternBesuch != -1) { next.vomStart += besucht[static_cast<std::size_t>(next.elternBesuch)].vomStart; }
                const float kosten = next.schaetzung();
                if (offen.benutzt(ni)) {
                    SuchKnoten& alt = offen.daten[static_cast<std::size_t>(offen.pos[static_cast<std::size_t>(ni)])];
                    if (kosten < alt.schaetzung()) {
                        alt = next;
                        offen.neuSortieren(ni);
                    }
                } else if (zu[static_cast<std::size_t>(ni)]) {
                    const int vb = knotenBesuch[static_cast<std::size_t>(ni)];
                    const float bisher = vb != -1 ? besucht[static_cast<std::size_t>(vb)].schaetzung() : -1.0F;
                    if (kosten < bisher) {
                        knotenBesuch[static_cast<std::size_t>(ni)] = -1;   // reopen_next_index
                        zu[static_cast<std::size_t>(ni)] = false;
                        offen.push(next);
                    }
                } else {
                    offen.push(next);
                }
            }
        }
    }
    std::vector<int> pfad;
    if (!erfolg()) { return pfad; }
    for (int v = static_cast<int>(besucht.size()) - 1; v != -1; v = besucht[static_cast<std::size_t>(v)].elternBesuch) {
        pfad.push_back(besucht[static_cast<std::size_t>(v)].knoten);
    }
    return pfad;
}
}  // namespace

bool navAStar(const NavNetz& n, int a, int b, std::vector<int>* pfad) {
    if (a <= 0 || b <= 0 || a >= static_cast<int>(n.knoten.size()) || b >= static_cast<int>(n.knoten.size())) {
        return false;
    }
    Akteur akt;
    akt.da = true;
    akt.groesse = 1;
    if (static_cast<int>(n.region.size()) > 0 && n.regionZahl > 0 && !gleicheRegionKante(n, a, b, akt)) { return false; }
    std::vector<int> r = aStern(n, a, b, akt);
    if (r.empty()) { return false; }
    if (pfad != nullptr) {
        pfad->assign(r.rbegin(), r.rend());
    }
    return true;
}

// =============================================================================
// Laufen
// =============================================================================
namespace {

int timerDone(int bis, int jetzt) { return bis == kNavNie || bis < jetzt; }   // TIMER_Done (G_Timer.cpp:357)

struct Lauf {
    const NavWelt& welt;
    const NavNetz* netz;
    const LaufWerte& w;
    LaufZustand& z;
    int jetzt = 0;   // level.time
    Akteur akt;
    NavVec mins{};
    NavVec maxs{};
    // SSteerUser
    NavVec sPos{};
    NavVec sVel{};
    float sSpeed = 0.0F;
    float sMaxSpeed = 0.0F;
    NavVec sSteering{};
    NavVec sSeek{};
    float sDistanz = 0.0F;
    NavVec sProjFwd{};
    NavVec sProjSide{};
    int sIgnore = 0;
    // Ergebnis des Denkbilds
    int stueck = 0;
    NavVec richtung{};
    // rc601 (bewegung, teil 3): wohin die Spuren dieses Denkbilds reichten
    // (LaufTakt::spurMin ...) und ob is_valid eine Tuer-/Wandkante fragte.
    NavVec spMin{1.0e30F, 1.0e30F, 1.0e30F};
    NavVec spMax{-1.0e30F, -1.0e30F, -1.0e30F};
    NavVec kpMin{1.0e30F, 1.0e30F, 1.0e30F};
    NavVec kpMax{-1.0e30F, -1.0e30F, -1.0e30F};
    bool kantenGefragt = false;
    // Jede Spur dieses Denkbilds geht hierueber: mit der Zeit (die Mover dann,
    // NavWelt::spurZeit) und in die Huelle.
    NavSpurFn spurF;
    NavSpur spurMitZeit(const NavVec& a, const NavVec& mi, const NavVec& ma, const NavVec& b, int ign, std::uint32_t m) {
        // SV_Trace (sv_world.cpp): clip.boxmins/boxmaxs = der Kasten um Start
        // und Ende, je 1 weiter - nur Entities darin werden geprueft.
        for (std::size_t k = 0; k < 3; ++k) {
            const float lo = std::min(a[k], b[k]) + mi[k] - 1.0F;
            const float hi = std::max(a[k], b[k]) + ma[k] + 1.0F;
            spMin[k] = std::min(spMin[k], lo);
            spMax[k] = std::max(spMax[k], hi);
            if ((m & kNavBody) != 0U) {
                kpMin[k] = std::min(kpMin[k], lo);
                kpMax[k] = std::max(kpMax[k], hi);
            }
        }
        NavSpur r = welt.spurZeit ? welt.spurZeit(a, mi, ma, b, ign, m, akt.t) : (welt.spur ? welt.spur(a, mi, ma, b, ign, m) : frei(b));
        // rc601 (bewegung, teil 3): die anderen Figuren - wie die Entities in
        // SV_Trace nach der Welt, die naechste Beruehrung gewinnt.
        if (welt.koerper != nullptr && (m & kNavBody) != 0U && !r.allsolid && r.fraction > 0.0F) {
            for (const NavKoerperKasten& k : *welt.koerper) {
                if (kNavKoerperBasis + k.id == ign) { continue; }
                NavVec lo{};
                NavVec hi{};
                for (std::size_t i = 0; i < 3; ++i) {
                    lo[i] = k.ort[i] + k.mins[i];
                    hi[i] = k.ort[i] + k.maxs[i];
                }
                const NavSpur t = kastenGegenKasten(a, mi, ma, b, lo, hi);
                if (t.allsolid) {
                    if (!r.allsolid) { r.entity = kNavKoerperBasis + k.id; }
                    r.allsolid = true;
                } else if (t.startsolid) {
                    if (!r.startsolid) { r.entity = kNavKoerperBasis + k.id; }
                    r.startsolid = true;
                }
                if (t.fraction < r.fraction) {
                    const bool alt = r.startsolid;
                    r.fraction = t.fraction;
                    r.endpos = t.endpos;
                    r.normal = t.normal;
                    r.contents = kNavBody;
                    r.entity = kNavKoerperBasis + k.id;
                    if (t.allsolid) { r.allsolid = true; }
                    if (alt) { r.startsolid = true; }
                }
                if (r.allsolid) { break; }
            }
        }
        return r;
    }
    // CM_TraceThroughBrush (cm_trace.cpp MD) gegen den Kasten einer Figur
    // (CM_TempBoxModel: sechs achsparallele Ebenen), mit dem Kasten der Spur
    // und SURFACE_CLIP_EPSILON 0,125.
    static NavSpur kastenGegenKasten(const NavVec& a, const NavVec& mi, const NavVec& ma, const NavVec& b, const NavVec& lo,
                                     const NavVec& hi) {
        constexpr float kEps = 0.125F;
        NavSpur r;
        r.endpos = b;
        float rein = -1.0F;
        float raus = 1.0F;
        bool startDraussen = false;
        bool endeDraussen = false;
        NavVec normale{};
        for (std::size_t ax = 0; ax < 3; ++ax) {
            for (int sg = 0; sg < 2; ++sg) {
                const float n = sg == 0 ? 1.0F : -1.0F;
                // dist - DotProduct(offsets[signbits], normal)
                const float dist = sg == 0 ? hi[ax] - mi[ax] : -lo[ax] + ma[ax];
                const float d1 = n * a[ax] - dist;
                const float d2 = n * b[ax] - dist;
                if (d2 > 0.0F) { endeDraussen = true; }
                if (d1 > 0.0F) { startDraussen = true; }
                if (d1 > 0.0F && (d2 >= kEps || d2 >= d1)) { return r; }
                if (d1 <= 0.0F && d2 <= 0.0F) { continue; }
                if (d1 > d2) {
                    float f = (d1 - kEps) / (d1 - d2);
                    if (f < 0.0F) { f = 0.0F; }
                    if (f > rein) {
                        rein = f;
                        normale = {0.0F, 0.0F, 0.0F};
                        normale[ax] = n;
                    }
                } else {
                    float f = (d1 + kEps) / (d1 - d2);
                    if (f > 1.0F) { f = 1.0F; }
                    if (f < raus) { raus = f; }
                }
            }
        }
        if (!startDraussen) {
            r.startsolid = true;
            if (!endeDraussen) {
                r.allsolid = true;
                r.fraction = 0.0F;
                r.endpos = a;
            }
            return r;
        }
        if (rein < raus && rein > -1.0F && rein < 1.0F) {
            if (rein < 0.0F) { rein = 0.0F; }
            r.fraction = rein;
            r.normal = normale;
            for (std::size_t i = 0; i < 3; ++i) { r.endpos[i] = a[i] + rein * (b[i] - a[i]); }
        }
        return r;
    }

    Lauf(const NavWelt& wl, const LaufWerte& wr, LaufZustand& zs) : welt(wl), netz(wl.netz.get()), w(wr), z(zs) {
        spurF = [this](const NavVec& a, const NavVec& mi, const NavVec& ma, const NavVec& b, int ign, std::uint32_t m) {
            return spurMitZeit(a, mi, ma, b, ign, m);
        };
        akt.mover = wl.mover.get();
        akt.gefragt = &kantenGefragt;
        mins = {-w.halbBreite, -w.halbBreite, kDefaultMins2};
        maxs = {w.halbBreite, w.halbBreite, w.hoeheOben};
        akt.da = true;
        // NAV::ClassifyEntSize (g_navigator.cpp:3654)
        akt.groesse = (w.halbBreite > kScMediumRadius || w.hoeheOben > kScMediumHeight) ? 2 : 1;
    }
    [[nodiscard]] bool hatNetz() const { return netz != nullptr && netz->zahlKanten() > 0; }
    [[nodiscard]] const NavKnoten& knoten(int h) const { return netz->knoten[static_cast<std::size_t>(h)]; }
    [[nodiscard]] const NavKante& kante(int h) const { return netz->kanten[static_cast<std::size_t>(h)]; }

    // --- NAV ----------------------------------------------------------------
    [[nodiscard]] bool gleicheRegion(int a, int b) const {
        // NAV::InSameRegion(A, B) (g_navigator.cpp:3149) - ohne Akteur
        if (netz == nullptr || netz->regionZahl <= 0) { return true; }
        if (a == 0 || b == 0) { return false; }
        if (a == b) { return true; }
        if (a < 0) { a = kante(-a).a; }
        if (b < 0) { b = kante(-b).a; }
        Akteur ohne;
        // rc601 (bewegung, teil 3): ohne mActor, aber mit dem Stand der Mover.
        ohne.mover = akt.mover;
        ohne.t = akt.t;
        ohne.gefragt = akt.gefragt;
        return gleicheRegionKante(*netz, a, b, ohne);
    }
    // NAV::GetNearestNode(position, previous, goal, ...) (g_navigator.cpp:2345)
    [[nodiscard]] int naechster(const NavVec& pos, int vorher, int ziel) const {
        if (!hatNetz()) { return 0; }
        const NavNetz::Zelle* c = netz->zelleBei(pos[0], pos[1]);
        if (c == nullptr || (c->knoten.empty() && c->kanten.empty())) { return 0; }
        struct Sort {
            int h = 0;
            float d = 0.0F;
            bool imRadius = false;
        };
        // mNearestNavSort: ein Puffer je Thread statt einer neuen Liste je
        // Aufruf (das wird in jedem Denkbild gefragt).
        thread_local std::vector<Sort> liste;
        liste.clear();
        liste.reserve(static_cast<std::size_t>(kNodesPerCell));
        for (std::size_t i = 0; i < c->knoten.size() && static_cast<int>(liste.size()) < kNodesPerCell; ++i) {
            const int h = c->knoten[i];
            const NavKnoten& k = knoten(h);
            Sort s;
            s.h = h;
            s.d = abstand2(k.punkt, pos);
            s.imRadius = s.d < k.radius * k.radius;
            const float zOff = std::fabs(k.punkt[2] - pos[2]);
            if (zOff > kViewRange / 4.0F) { continue; }
            if (zOff > 30.0F) { s.d += zOff * zOff; }
            if (s.d > kViewRange * kViewRange) { continue; }
            if (k.insel) { s.d *= 3.0F; }
            if (vorher != 0 && vorher != h && !gleicheRegion(vorher, h)) { s.d += 100.0F * 100.0F; }
            if (vorher > 0 && vorher != h && netz->kanteZwischen(vorher, h) == 0) { s.d += 200.0F * 200.0F; }
            if (ziel != 0 && ziel != h && !gleicheRegion(ziel, h)) { s.d += 300.0F * 300.0F; }
            liste.push_back(s);
        }
        // Abkuerzung, Ergebnis gleich: ist der naechste Eintrag eindeutig (kein
        // anderer gleich weit) und im Radius oder sichtbar, liefert ihn auch
        // die sortierte Liste zuerst - sonst die volle Sortierung.
        if (!liste.empty()) {
            std::size_t best = 0;
            bool eindeutig = true;
            for (std::size_t i = 1; i < liste.size(); ++i) {
                if (liste[i].d < liste[best].d) {
                    best = i;
                    eindeutig = true;
                } else if (liste[i].d == liste[best].d) {
                    eindeutig = false;
                }
            }
            if (eindeutig) {
                const Sort& s = liste[best];
                if (s.imRadius || sichtFrei(spurF, pos, knoten(s.h).punkt, true)) { return s.h; }
            }
        }
        heapSortieren(liste, [](const Sort& a, const Sort& b) { return a.d < b.d; });
        for (const Sort& s : liste) {
            if (s.imRadius) { return s.h; }
            if (sichtFrei(spurF, pos, knoten(s.h).punkt, true)) { return s.h; }
        }
        liste.clear();
        for (std::size_t i = 0; i < c->kanten.size() && static_cast<int>(liste.size()) < kNodesPerCell; ++i) {
            const int h = c->kanten[i];
            const NavKante& k = kante(h);
            const NavVec mitte = mal(plus(knoten(k.a).punkt, knoten(k.b).punkt), 0.5F);
            Sort s;
            s.h = h;
            s.d = abstand2(mitte, pos);
            const float zOff = std::fabs(mitte[2] - pos[2]);
            if (zOff > kViewRange / 4.0F) { continue; }
            if (zOff > 30.0F) { s.d += zOff * zOff; }
            if (s.d > kViewRange * kViewRange) { continue; }
            liste.push_back(s);
        }
        heapSortieren(liste, [](const Sort& a, const Sort& b) { return a.d < b.d; });
        for (const Sort& s : liste) {
            const NavKante& k = kante(s.h);
            NavVec p = pos;
            const float anteil = aufGerade(p, knoten(k.a).punkt, knoten(k.b).punkt);
            if (anteil > 0.0F && anteil < 1.0F && sichtFrei(spurF, pos, p, true)) { return -s.h; }
        }
        return 0;
    }
    // GetNearestNode(ent, forceRecalcNow, goal) (g_navigator.cpp:2316) fuer die Figur
    int figurKnoten(bool sofort, int ziel) {
        if (z.wegpunkt == 0 || sofort || jetzt > z.wegpunktBis) {
            if (z.wegpunkt != 0) { z.letzterWegpunkt = z.wegpunkt; }
            z.wegpunkt = naechster(z.ort, z.wegpunkt, ziel);
            z.wegpunktBis = jetzt + 1000;
        }
        return z.wegpunkt;
    }
    // ... und fuer tempGoal
    int zielKnoten(bool sofort) {
        if (z.zielWegpunkt == 0 || sofort || jetzt > z.zielWegpunktBis) {
            if (z.zielWegpunkt != 0) { z.zielLetzterWegpunkt = z.zielWegpunkt; }
            z.zielWegpunkt = naechster(w.ziel, z.zielWegpunkt, 0);
            z.zielWegpunktBis = jetzt + 1000;
        }
        return z.zielWegpunkt;
    }
    [[nodiscard]] NavVec knotenOrt(int h) const {
        // NAV::GetNodePosition (g_navigator.cpp:3620)
        if (h == 0 || netz == nullptr) { return {0, 0, 0}; }
        return h > 0 ? knoten(h).punkt : knoten(kante(-h).a).punkt;
    }
    [[nodiscard]] bool nachbarn(int a, int b) const {
        // NAV::OnNeighboringPoints (g_navigator.cpp:3189)
        if (a == b) { return true; }
        if (a <= 0 || b <= 0 || netz == nullptr) { return false; }
        const int e = netz->kanteZwischen(a, b);
        return e > 0 && !kante(e).hat(NavKante::Springen) && !kante(e).hat(NavKante::Fliegen) &&
               kante(e).laenge < kSafeNeighboringPointDist;
    }
    [[nodiscard]] bool imSicherenRadius(const NavVec& at, int atNode, int zielNode) const {
        // NAV::InSafeRadius (g_navigator.cpp:3251)
        if (atNode <= 0 || netz == nullptr) { return false; }
        if (abstand(at, knotenOrt(atNode)) < knoten(atNode).radius) { return true; }
        if (zielNode > 0 && atNode != zielNode) {
            if (abstand(at, knotenOrt(zielNode)) < knoten(zielNode).radius) { return true; }
            const int e = netz->kanteZwischen(atNode, zielNode);
            if (e > 0) {
                const NavKante& k = kante(e);
                if (!k.hat(NavKante::Fliegen) && !k.hat(NavKante::Springen) && kanteGueltig(k, akt, zielNode)) {
                    const float d = abstandZurStrecke(at, knoten(k.a).punkt, knoten(k.b).punkt);
                    return k.hat(NavKante::GroesseGross) ? d < kScLargeRadius : d < kScMediumRadius;
                }
            }
        }
        return false;
    }
    // MoveTrace(actor, goal, IgnoreAllEnts) (g_navigator.cpp:911)
    bool figurGehFrei(const NavVec& ziel, bool ohneKoerper, NavSpur* aus = nullptr) const {
        NavVec m = mins;
        m[2] += kStepSize;
        return gehFrei(spurF, z.ort, ziel, m, maxs, kNavKeineEntity, true, true, ohneKoerper, aus);
    }
    [[nodiscard]] bool hatPfad(int ziel) const {
        // NAV::HasPath (g_navigator.cpp:3491)
        return z.pfadDa && z.pfadErfolg && z.pfadGroesse > 0 && (ziel == 0 || z.pfadEnde == ziel);
    }
    void pfadLoeschen() {
        z.pfadDa = false;
        z.pfad.reset();
        z.pfadGroesse = 0;
    }
    [[nodiscard]] const NavPfadPunkt& pfadPunkt(int i) const { return (*z.pfad)[static_cast<std::size_t>(i)]; }

    // NAV::FindPath (g_navigator.cpp:2698)
    bool pfadFinden(int ziel) {
        if (ziel == 0) { return false; }
        int start = figurKnoten(true, ziel);
        if (start == 0) { return false; }
        if (start < 0) { start = kante(-start).a; }   // Q_irand(0,1): fest mNodeA
        if (ziel < 0) { ziel = kante(-ziel).a; }
        if (!z.pfadDa) {
            z.pfadDa = true;
            z.pfadEnde = 0;
            z.pfadErfolg = false;
            z.pfadAStarBis = 0;
            z.pfad.reset();
            z.pfadGroesse = 0;
        }
        if (z.pfadEnde == ziel && jetzt < z.pfadAStarBis) { return z.pfadErfolg; }
        z.pfadEnde = ziel;
        if (netz->regionZahl > 0 && !gleicheRegionKante(*netz, start, ziel, akt)) {
            z.pfadErfolg = false;
            return false;
        }
        const std::vector<int> knotenPfad = aStern(*netz, start, ziel, akt);
        z.pfadAStarBis = jetzt + kAStarWiederMs;
        z.pfadErfolg = !knotenPfad.empty();
        if (!z.pfadErfolg) { return false; }
        NavVec at = z.ort;
        float atZeit = static_cast<float>(jetzt);
        float atTempo = w.maxTempo;   // wie STEER::Activate (SCF_RUNNING/WALKING, BUTTON_WALKING)
        atTempo *= 0.001F;
        atTempo *= 0.25F;
        const float minR = std::min(mins[0], mins[1]);
        const float maxR = std::max(maxs[0], maxs[1]);
        float radius = std::max(std::fabs(minR), maxR);
        if (radius > 20.0F) { radius = 20.0F; }
        auto pfad = std::make_shared<std::vector<NavPfadPunkt>>();
        NavPfadPunkt pp;
        for (const int k : knotenPfad) {
            if (static_cast<int>(pfad->size()) >= kMaxPathSize) { break; }
            pp.knoten = k;
            pp.punkt = knoten(k).punkt;
            pp.tempo = atTempo;
            pp.abbremsRadius = 0.0F;
            float erreicht = std::max(radius * 3.0F, knoten(k).radius * 0.40F);
            if (knoten(k).schwebend) { erreicht = 20.0F; }
            pp.erreichtRadius2 = erreicht * erreicht;
            pp.abstand = 0.0F;
            pp.eta = 0.0F;
            pfad->push_back(pp);
        }
        // "Last Point On The Path Always Gets A Slowing Radius" - mit dem
        // Radius von PPoint.mNode, also dem zuletzt eingetragenen Punkt (dem
        // START), nicht dem Ziel: so steht es in der Engine.
        (*pfad)[0].abbremsRadius = std::max(70.0F, knoten(pp.knoten).radius * 0.75F);
        const float er0 = std::max(radius, knoten(pp.knoten).radius * 0.15F);
        (*pfad)[0].erreichtRadius2 = er0 * er0;
        const int kantenZahl = static_cast<int>(pfad->size()) - 1;
        for (int e = 0; e < kantenZahl; ++e) {
            const NavVec a = (*pfad)[static_cast<std::size_t>(e)].punkt;
            const NavVec b = (*pfad)[static_cast<std::size_t>(e + 1)].punkt;
            NavVec aufKante = at;
            const float anteil = aufStrecke(aufKante, a, b);
            const float d = abstand(aufKante, at);
            if (anteil > 0.1F && anteil < 0.9F) {
                if (d < radius || (d < radius * 20.0F && gehFrei(spurF, at, aufKante, mins, maxs, kNavKeineEntity, true,
                                                               true, false))) {
                    pfad->resize(static_cast<std::size_t>(e + 2));
                    (*pfad)[static_cast<std::size_t>(e + 1)].punkt = aufKante;
                    break;
                }
            }
        }
        for (int i = static_cast<int>(pfad->size()) - 1; i > -1; --i) {
            NavPfadPunkt& p = (*pfad)[static_cast<std::size_t>(i)];
            NavVec zuNaechstem = minus(p.punkt, at);
            if (std::fabs(zuNaechstem[2]) > kZCullOffset) { zuNaechstem[2] = 0.0F; }
            p.abstand = norm(zuNaechstem);
            p.eta = p.abstand / p.tempo;
            p.eta += atZeit;
            if (i != 0 && !knoten(p.knoten).schwebend) {
                NavVec weiter = minus((*pfad)[static_cast<std::size_t>(i - 1)].punkt, p.punkt);
                if (std::fabs(weiter[2]) > kZCullOffset) { weiter[2] = 0.0F; }
                const float weiterAbstand = norm(weiter);
                const float dot = punkt(weiter, zuNaechstem);
                if ((weiterAbstand > 150.0F && p.abstand > 150.0F && dot < 0.64F) || (weiterAbstand > 30.0F && dot < 0.5F)) {
                    p.abbremsRadius = std::max(40.0F, knoten(p.knoten).radius);   // "Force A Stop Here"
                }
            }
            atZeit = p.eta;
            at = p.punkt;
        }
        z.pfad = pfad;
        z.pfadGroesse = static_cast<int>(pfad->size());
        return z.pfadErfolg;
    }
    // NAV::UpdatePath (g_navigator.cpp:3406)
    bool pfadAktualisieren(int ziel) {
        if (!z.pfadDa || !z.pfadErfolg || z.pfadGroesse == 0) { return false; }
        bool erreichtIrgendwas = false;
        bool drin = false;
        do {
            const NavPfadPunkt& p = pfadPunkt(z.pfadGroesse - 1);
            NavVec d = minus(p.punkt, z.ort);
            if (std::fabs(z.ort[2] - p.punkt[2]) < kZCullOffset) { d[2] = 0.0F; }
            drin = laenge2(d) < p.erreichtRadius2;
            if (drin) {
                erreichtIrgendwas = true;
                --z.pfadGroesse;
            }
        } while (drin && z.pfadGroesse > 0);
        if (z.pfadGroesse == 0) { return false; }
        if (erreichtIrgendwas && ziel != 0 && z.pfadEnde != ziel) { return false; }
        if (pfadPunkt(z.pfadGroesse - 1).eta < static_cast<float>(jetzt)) {
            const int ende = z.pfadEnde;
            if (ende == 0) {
                pfadLoeschen();
                return false;
            }
            z.pfadEnde = 0;
            if (!pfadFinden(ende)) {
                z.pfadEnde = ende;
                return false;
            }
            return true;
        }
        return true;
    }

    // --- STEER ----------------------------------------------------------------
    void aktivieren() {
        // STEER::Activate (g_navigator.cpp:3942)
        sPos = z.ort;
        sVel = z.tempoVek;
        sSpeed = laenge(sVel);
        sMaxSpeed = w.maxTempo;
        sSteering = {0, 0, 0};
        z.tempo = 0;   // actor->client->ps.speed = 0
        sProjFwd = plus(sPos, mal(sVel, 1.0F));
        sProjSide = umsetzen(mal(sVel, 0.3F), sPos, z.seiteAusweichen == kSideLeft ? 40.0F : -40.0F);
    }
    void stop(float gewicht = 1.0F) {
        // STEER::Stop (g_navigator.cpp:4601)
        sDistanz = 0.0F;
        sSteering = plus(sSteering, mal(minus(NavVec{0, 0, 0}, sVel), gewicht));
    }
    float seek(const NavVec& ort, float abbremsen, float gewicht = 1.0F, float wunsch = 0.0F) {
        // STEER::Seek (g_navigator.cpp:4655)
        sSeek = ort;
        NavVec dv = minus(sSeek, sPos);
        if (std::fabs(dv[2]) < 10.0F) { dv[2] = 0.0F; }
        sDistanz = sicherNorm(dv);
        if (sDistanz > 0.0F) {
            float tempo = wunsch != 0.0F ? wunsch : sMaxSpeed;
            if (abbremsen != 0.0F && sDistanz < abbremsen) { tempo *= sDistanz / abbremsen; }
            dv = mal(dv, tempo);
        } else {
            dv = {0, 0, 0};
        }
        sSteering = plus(sSteering, mal(minus(dv, sVel), gewicht));
        richtung = ort;
        return sDistanz;
    }
    [[nodiscard]] bool erreicht(const NavVec& ziel, float r) const {
        // STEER::Reached (g_navigator.cpp:5415): absmin/absmax mit dem
        // Epsilon von SV_LinkEntity (sv_world.cpp:290-301)
        const NavVec absMin{z.ort[0] + mins[0] - 1.0F, z.ort[1] + mins[1] - 1.0F, z.ort[2] + mins[2] - 1.0F};
        const NavVec absMax{z.ort[0] + maxs[0] + 1.0F, z.ort[1] + maxs[1] + 1.0F, z.ort[2] + maxs[2] + 1.0F};
        return abstand2(ziel, z.ort) < r * r || (groesserAlle(ziel, absMin) && kleinerAlle(ziel, absMax));
    }
    // TestCollision (g_navigator.cpp:5092) - gegen die Welt und Brush-Entities;
    // andere Figuren kennt die Spur nicht.
    bool kollision(const NavVec& projTempo, float projSpeed, int seite, float gewicht) {
        NavSpur s;
        bool sicher = seite == kSideNone ? figurGehFrei(sProjFwd, false, &s) : figurGehFrei(sProjSide, false, &s);
        // "Doors Are Always Safe" (die Ignore-Entity ist eine Figur - die
        // Spur kennt keine Figuren).
        if (s.entity >= 0 && welt.istTuer && welt.istTuer(s.entity)) { sicher = true; }
        if (s.entity != kNavKeineEntity && s.entity != kNavWeltEntity && s.entity == sIgnore) { sicher = true; }
        NavVec normale = s.normal;
        NavVec kontakt = s.endpos;
        if (!sicher && seite == kSideNone) {
            // rc601 (bewegung, teil 3): "Contact moving / standing"
            // (g_navigator.cpp:5118-5163 MD) - die Spur traf eine Figur.
            const NavKoerperKasten* gegen = nullptr;
            if (s.entity >= kNavKoerperBasis && welt.koerper != nullptr) {
                for (const NavKoerperKasten& k : *welt.koerper) {
                    if (kNavKoerperBasis + k.id == s.entity) { gegen = &k; }
                }
            }
            if (gegen != nullptr) {
                const float kontaktTempo = laenge(gegen->tempo);
                if (kontaktTempo > 0.01F) {
                    if (kontaktTempo < projSpeed) {
                        NavVec meine = projTempo;
                        NavVec seine = gegen->tempo;
                        norm(seine);
                        norm(meine);
                        const float aehnlich = std::fabs(punkt(meine, seine));
                        if (aehnlich > 0.5F) {
                            NavVec wunsch = sVel;
                            kappen(wunsch, kontaktTempo);   // mDesiredVelocity.Truncate(ContactSpeed)
                            sSteering = plus(sSteering, mal(minus(wunsch, projTempo), aehnlich));
                            sIgnore = s.entity;   // "So The Side Trace Does Not Care About This Guy"
                            sicher = true;
                        }
                    }
                } else {
                    const NavVec naechste = hatPfad(0) ? pfadPunkt(z.pfadGroesse - 1).punkt : sSeek;
                    NavVec absMin{};
                    NavVec absMax{};
                    for (std::size_t i = 0; i < 3; ++i) {
                        // absmin/absmax mit dem Epsilon von SV_LinkEntity
                        absMin[i] = gegen->ort[i] + gegen->mins[i] - 1.0F;
                        absMax[i] = gegen->ort[i] + gegen->maxs[i] + 1.0F;
                    }
                    if (groesserAlle(naechste, absMin) && kleinerAlle(naechste, absMax)) {
                        sSteering = minus(sSteering, projTempo);
                        sIgnore = s.entity;
                        sicher = true;   // "We Don't Want To Try And Steer Around"
                    }
                }
            }
            if (!sicher && normale[2] > 0.0F && normale[2] < kMinWalkNormal) { sicher = true; }
            if (!sicher) {
                NavVec projRichtung = projTempo;
                norm(projRichtung);
                // ContactNormal.Cross(CVec3::mZ)
                const NavVec n0 = normale;
                normale = {n0[1] * 1.0F - n0[2] * 0.0F, n0[2] * 0.0F - n0[0] * 1.0F, n0[0] * 0.0F - n0[1] * 0.0F};
                if (z.letzteSeite != kSideNone && z.seiteBis < jetzt) {
                    z.letzteSeite = kSideNone;
                    z.seiteBis = jetzt + kSideLockedTimerMitte;
                }
                if (z.letzteSeite == kSideRight || (z.letzteSeite == kSideNone && punkt(normale, projRichtung) < 0.0F)) {
                    normale = mal(normale, -1.0F);
                    z.letzteSeite = kSideRight;
                } else {
                    z.letzteSeite = kSideLeft;
                }
                normale[2] = 0.0F;
                normale = mal(normale, projSpeed * gewicht * 0.5F);
                kontakt = plus(kontakt, normale);
                // STEER::Seek(actor, ContactPoint, weight): das Gewicht landet
                // im Parameter slowingDistance - so steht es in der Engine.
                const NavVec merk = richtung;
                seek(kontakt, gewicht);
                richtung = merk;
            }
        } else if (sicher && seite == kSideNone) {
            z.letzteSeite = kSideNone;
        }
        return !sicher;
    }
    float ausweichen() {
        // STEER::AvoidCollisions (g_navigator.cpp:5293)
        sIgnore = -5;
        NavVec projSteering = sSteering;
        NavVec projVel = sVel;
        float projSpeed = sSpeed;
        const float newtons = kappen(projSteering, 150.0F);
        if (newtons > 1E-10F) {
            projVel = plus(projVel, projSteering);
            projSpeed = kappen(projVel, sMaxSpeed);
        }
        if (projSpeed > 0.01F) {
            const NavVec seitlich = umsetzen(projVel, {0, 0, 0}, z.seiteAusweichen == kSideLeft ? 40.0F : -40.0F);
            sProjFwd = plus(sPos, mal(projVel, 1.0F));
            sProjSide = plus(sPos, mal(seitlich, 0.3F));
            const bool vorn = kollision(projVel, projSpeed, kSideNone, 1.0F);
            const bool seite = kollision(projVel, projSpeed, z.seiteAusweichen, 0.5F);
            if (!seite) { z.seiteAusweichen = z.seiteAusweichen == kSideLeft ? kSideRight : kSideLeft; }
            if (vorn || seite) { return projSpeed; }
        }
        return 0.0F;
    }
    // STEER::SafeToGoTo (g_navigator.cpp:4337)
    bool sicherHin(const NavVec& zielPos, int zielNode) {
        const int figurNode = figurKnoten(true, zielNode);
        const float d = abstand(z.ort, zielPos);
        if (d < 110.0F && std::fabs(zielPos[2] - z.ort[2]) < 50.0F) { return true; }
        if (d < 500.0F) {
            if (nachbarn(figurNode, zielNode) && imSicherenRadius(z.ort, figurNode, zielNode) &&
                imSicherenRadius(zielPos, zielNode, figurNode)) {
                return true;
            }
            if (d < 400.0F) {
                if (!timerDone(z.safeDauerBis, jetzt)) { return true; }
                if (timerDone(z.safeCheckBis, jetzt)) {
                    z.safeCheckBis = jetzt + 1500;
                    if (figurGehFrei(zielPos, true)) { z.safeDauerBis = jetzt + 2000; }
                }
            }
        }
        return false;
    }
    // STEER::GoTo(actor, target, reachedRadius) (g_navigator.cpp:4407)
    bool steerHin() {
        if (erreicht(w.ziel, w.zielRadius)) {
            pfadLoeschen();
            stop();
            return true;
        }
        const int zn = zielKnoten(false);
        if (!sicherHin(w.ziel, zn)) { return false; }
        pfadLoeschen();
        stueck = 0;
        seek(w.ziel, w.zielRadius * 4.0F);   // Persue: tempGoal hat keinen client
        (void)ausweichen();
        return true;
    }
    // STEER::Path (g_navigator.cpp:4931)
    float steerPfad() {
        if (!hatPfad(0)) { return 0.0F; }
        const NavPfadPunkt& next = pfadPunkt(z.pfadGroesse - 1);
        const int cur = figurKnoten(false, 0);
        const int e = cur > 0 ? netz->kanteZwischen(cur, next.knoten) : -cur;
        const bool springen = e > 0 && kante(e).hat(NavKante::Springen);
        if (springen) {
            // NPC_TryJump rechnet behaved nicht: wie ein misslungener Sprung.
            stop();
            return 0.0F;
        }
        stueck = next.knoten;
        return seek(next.punkt, next.abbremsRadius);
    }
    // NAV::GoTo(actor, target) (g_navigator.cpp:981)
    bool navHin() {
        int zn = zielKnoten(true);
        if (zn == 0) { zn = z.zielLetzterWegpunkt; }
        if (zn == 0) { return false; }
        if (zn < 0) { zn = kante(-zn).a; }   // Q_irand(0,1): fest mNodeA
        bool hat = hatPfad(0);
        if (hat) { hat = pfadAktualisieren(zn); }
        if (!hat) { hat = pfadFinden(zn); }
        if (hat) {
            hat = steerPfad() != 0.0F;
            if (hat) { (void)ausweichen(); }
        }
        return hat;
    }
    // STEER::DeActivate (g_navigator.cpp:4091)
    void deaktivieren(bool& bewegt, bool& gehTaste) {
        const float newtons = kappen(sSteering, 150.0F);   // mMaxForce, mMass 1
        if (newtons > 1E-10F) {
            sVel = plus(sVel, sSteering);
            sSpeed = kappen(sVel, sMaxSpeed);
        }
        if (sSpeed < 10.0F) { sSpeed = 0.0F; }
        NavVec moveDir = sVel;
        float gier = z.pfadGier;
        if (sSpeed > 0.0F && !istNull(moveDir)) {
            norm(moveDir);
            gier = gierVon(sVel);
        }
        zBewegung = moveDir;
        z.tempo = static_cast<int>(sSpeed);   // ps.speed ist int
        z.pfadGier = gier;
        z.wunschGier = winkel360(gier);
        bewegt = false;
        gehTaste = false;
        ucmdVor = 0;    // rc601 (bewegung, teil 3): fuer PM_AirMove/PM_CrashLand
        ucmdSeit = 0;
        if (sSpeed > 0.0F) {
            NavVec vorn{};
            NavVec rechts{};
            qAchsen(z.gier, vorn, rechts);   // AngleVectors( currentAngles )
            const float fDot = std::clamp(punkt(vorn, moveDir) * 127.0F, -127.0F, 127.0F);
            const float rDot = std::clamp(punkt(rechts, moveDir) * 127.0F, -127.0F, 127.0F);
            const auto fwd = static_cast<signed char>(std::floor(fDot));
            const auto rgt = static_cast<signed char>(std::floor(rDot));
            bewegt = fwd != 0 || rgt != 0;
            ucmdVor = fwd;
            ucmdSeit = rgt;
            gehTaste = sSpeed < w.gehTempo + 5.0F;
            if (fwd < 0) {
                // "Slow Down If Going Backwards"
                z.tempo = static_cast<int>(static_cast<float>(z.tempo) * 0.75F);
                sSpeed *= 0.75F;
            }
        }
    }
    NavVec zBewegung{};   // ps.moveDir dieses Denkbilds

    // NPC_UpdateAngles (NPC_utils.cpp:340-367): Gier
    void drehen() {
        float fehler = winkelDelta(z.gier, z.wunschGier);
        if (std::fabs(fehler) > kMinAngleErr) {
            const float abbau = w.drehRate * 0.05F;   // (60 + 3*yawSpeed) * 50/1000
            if (fehler < 0.0F) {
                fehler += abbau;
                if (fehler > 0.0F) { fehler = 0.0F; }
            } else {
                fehler -= abbau;
                if (fehler < 0.0F) { fehler = 0.0F; }
            }
        }
        // rc601 (bewegung, teil 3): ANGLE2SHORT/SHORT2ANGLE (navGierQuant).
        z.gier = navGierQuant(z.wunschGier + fehler);
    }

    // --- rc601 (bewegung, Durchsicht 2): Pmove gegen die Waende ---------------
    //
    // Bis zur Durchsicht 2 bewegte das Pmove des Plans die Figur ohne Waende
    // (z.ort += vel * frametime) - STEER lenkt zwar mit AvoidCollisions
    // vorher ab, aber wo es eine Wand trifft (ein Ziel unter 110 hinter einer
    // Wand, enge Ecken des Wegnetzes), ging die Figur in der Vorschau
    // hindurch. Im Spiel schieben PM_StepSlideMove/PM_SlideMove
    // (bg_slidemove.cpp MD) den Kasten der Figur (mins/maxs, pm->tracemask =
    // clipmask MASK_NPCSOLID, NPC_spawn.cpp:1897, g_active.cpp:11036) an der
    // Wand entlang und steigen Stufen bis step_size hinauf.
    //
    // Was hier fehlt, steht in NOTIZEN §9: andere Figuren (CONTENTS_BODY
    // kennt die Spur nicht), Patches, und das Fallen ueber mehr als eine
    // Stufe (der Plan haelt dann die Hoehe; gezeichnet wird die Schwerkraft).
    // rc601 (bewegung, teil 3): "Falling more than one step inside the walk
    // plan" - PmoveSingle (bg_pmove.cpp:33950-34110 MD) mit PM_GroundTrace vor
    // und nach der Bewegung, PM_WalkMove am Boden, PM_AirMove in der Luft
    // (pm_airaccelerate 4, keine Reibung, PM_StepSlideMove(1) mit der
    // Schwerkraft), PM_CrashLand beim Aufsetzen, PM_CorrectAllSolid statt
    // des Anhebens um eine Stufe. Bis teil 3 hielt der Plan die Hoehe, wo der
    // Boden mehr als eine Stufe tiefer lag, und hob eine steckende Figur an.
    NavVec bodenNormale{0.0F, 0.0F, 1.0F};   // pml.groundTrace.plane.normal
    bool gehend = false;          // pml.walking
    bool bodenFlaeche = false;    // pml.groundPlane
    NavVec vorherTempo{};         // pml.previous_velocity
    float frameZeit = 0.05F;      // pml.frametime
    int frameMs = 50;             // pml.msec
    float schwer = 800.0F;        // ps.gravity
    signed char ucmdVor = 0;      // ucmd.forwardmove aus STEER::DeActivate
    signed char ucmdSeit = 0;     // ucmd.rightmove
    [[nodiscard]] NavSpur kastenSpur(const NavVec& a, const NavVec& b, std::uint32_t maske) const {
        return spurDurch(spurF, a, mins, maxs, b, kNavKeineEntity, maske);
    }
    // PM_ClipVelocity (bg_pmove.cpp:743 MD; die g_stepSlideFix-Ecke gilt nur
    // fuer Spieler).
    static void abgleiten(NavVec& v, const NavVec& n, float overbounce) {
        float backoff = punkt(v, n);
        backoff = backoff < 0.0F ? backoff * overbounce : backoff / overbounce;
        for (std::size_t i = 0; i < 3; ++i) { v[i] -= n[i] * backoff; }
    }
    // PM_GroundTrace (bg_pmove.cpp:7385-7560 MD) fuer einen NPC zu Fuss (kein
    // Fahrzeug, kein Wandlauf, kein Halten durch Rancor/Wampa).
    void bodenSpur() {
        NavVec tief = z.ort;
        tief[2] -= 0.25F;
        const NavSpur tr = kastenSpur(z.ort, tief, kNavMaskNpcSolid);
        if (tr.allsolid) {
            // PM_CorrectAllSolid (bg_pmove.cpp:6323-6333 MD): "FIXME: jitter
            // around" - nur kein Boden, kein Gehen.
            z.amBoden = false;
            bodenFlaeche = false;
            gehend = false;
            return;
        }
        if (tr.fraction == 1.0F || schwer <= 0.0F) {
            // "if the trace didn't hit anything, we are in free fall" -
            // PM_GroundTraceMissed setzt groundEntityNum = ENTITYNUM_NONE.
            z.amBoden = false;
            bodenFlaeche = false;
            gehend = false;
            return;
        }
        bodenNormale = tr.normal;
        // "check if getting thrown off the ground" (ohne PMF_TIME_KNOCKBACK)
        if (z.tempoVek[2] > 100.0F && punkt(z.tempoVek, tr.normal) > 10.0F) {
            z.amBoden = false;
            bodenFlaeche = false;
            gehend = false;
            return;
        }
        if (tr.normal[2] < kMinWalkNormal) {
            // "slopes that are too steep will not be considered onground"
            z.amBoden = false;
            bodenFlaeche = true;
            gehend = false;
            return;
        }
        bodenFlaeche = true;
        gehend = true;
        if (!z.amBoden) {
            // "just hit the ground": PM_CrashLand; "don't do landing time if
            // we were just going down a slope"; ohne Vorwaerts/Seitwaerts im
            // ucmd "velocity[2] = 0".
            if (vorherTempo[2] < -200.0F) { z.pmZeit = 250; }   // PMF_TIME_LAND
            if (ucmdVor == 0 && ucmdSeit == 0) { z.tempoVek[2] = 0.0F; }
        }
        z.amBoden = true;
    }
    // PM_SlideMove(grav_mod) (bg_slidemove.cpp:47-357 MD) fuer einen NPC.
    bool schieben(float gravMod) {
        NavVec& vel = z.tempoVek;
        // Do-not-enter-Brushes zaehlen nur fuer einen NPC, der geht.
        std::uint32_t inhalt = kNavMaskNpcSolid | (gehend ? kNavBotClip : 0U);
        constexpr int kMaxEbenen = 5;   // MAX_CLIP_PLANES
        NavVec ebenen[kMaxEbenen]{};
        NavVec primal = vel;
        NavVec endTempo = vel;
        if (gravMod != 0.0F) {
            endTempo[2] -= schwer * frameZeit * gravMod;
            vel[2] = (vel[2] + endTempo[2]) * 0.5F;
            primal[2] = endTempo[2];
            if (bodenFlaeche) { abgleiten(vel, bodenNormale, kOverclip); }   // PM_GroundSlideOkay: ja
        }
        float zeitRest = frameZeit;
        int zahl = 0;
        if (bodenFlaeche) { ebenen[zahl++] = bodenNormale; }
        {
            // "never turn against original velocity" (VectorNormalize2)
            NavVec v = vel;
            const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (l != 0.0F) {
                const float il = 1.0F / l;
                v = {v[0] * il, v[1] * il, v[2] * il};
            } else {
                v = {0.0F, 0.0F, 0.0F};
            }
            ebenen[zahl++] = v;
        }
        int stoss = 0;
        for (; stoss < 4; ++stoss) {
            const NavVec ende = plus(z.ort, mal(vel, zeitRest));
            NavSpur tr = kastenSpur(z.ort, ende, inhalt);
            if ((tr.contents & kNavBotClip) != 0U && (inhalt & kNavBotClip) != 0U) {
                if (tr.allsolid || tr.startsolid) {
                    inhalt &= ~kNavBotClip;
                    tr = kastenSpur(z.ort, ende, inhalt);
                } else if (tr.normal[2] > 0.0F) {
                    tr = kastenSpur(z.ort, ende, inhalt & ~kNavBotClip);
                }
            }
            if (tr.allsolid) {
                vel[2] = 0.0F;   // "don't build up falling damage, but allow sideways acceleration"
                return true;
            }
            if (tr.fraction > 0.0F) { z.ort = tr.endpos; }
            if (tr.fraction == 1.0F) { break; }
            zeitRest -= zeitRest * tr.fraction;
            if (zahl >= kMaxEbenen) {
                vel = {0.0F, 0.0F, 0.0F};
                return true;
            }
            const NavVec normale = tr.normal;
            int i = 0;
            for (; i < zahl; ++i) {
                if (punkt(normale, ebenen[i]) > 0.99F) {
                    vel = plus(normale, vel);   // "nudge velocity out along it"
                    break;
                }
            }
            if (i < zahl) { continue; }
            ebenen[zahl++] = normale;
            for (i = 0; i < zahl; ++i) {
                if (punkt(vel, ebenen[i]) >= 0.1F) { continue; }
                NavVec clip = vel;
                NavVec endClip = endTempo;
                abgleiten(clip, ebenen[i], kOverclip);
                abgleiten(endClip, ebenen[i], kOverclip);
                bool tot = false;
                for (int j = 0; j < zahl && !tot; ++j) {
                    if (j == i) { continue; }
                    if (punkt(clip, ebenen[j]) >= 0.1F) { continue; }
                    abgleiten(clip, ebenen[j], kOverclip);
                    abgleiten(endClip, ebenen[j], kOverclip);
                    if (punkt(clip, ebenen[i]) >= 0.0F) { continue; }
                    // "slide the original velocity along the crease"
                    const NavVec& a = ebenen[i];
                    const NavVec& b = ebenen[j];
                    NavVec dir{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
                    (void)vektorNorm(dir);
                    clip = mal(dir, punkt(dir, vel));
                    NavVec dir2{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
                    (void)vektorNorm(dir2);
                    endClip = mal(dir2, punkt(dir2, endTempo));
                    for (int k = 0; k < zahl; ++k) {
                        if (k == i || k == j) { continue; }
                        if (punkt(clip, ebenen[k]) >= 0.1F) { continue; }
                        vel = {0.0F, 0.0F, 0.0F};   // "stop dead at a triple plane interaction"
                        return true;
                    }
                }
                vel = clip;
                endTempo = endClip;
                break;
            }
        }
        if (gravMod != 0.0F) { vel = endTempo; }
        if (z.pmZeit != 0) { vel = primal; }   // "don't change velocity if in a timer"
        return stoss != 0;
    }
    // PM_StepSlideMove(grav_mod) (bg_slidemove.cpp:360-628 MD, g_stepSlideFix 1).
    void stufenSchieben(float gravMod) {
        NavVec& vel = z.tempoVek;
        const NavVec startO = z.ort;
        const NavVec startV = vel;
        if (!schieben(gravMod)) { return; }   // "we got exactly where we wanted to go first try"
        const float stufe = w.stufe;   // R6: je Klasse
        NavVec unten = startO;
        unten[2] -= stufe;
        NavSpur tr = kastenSpur(startO, unten, kNavMaskNpcSolid);
        if (vel[2] > 0.0F && (tr.fraction == 1.0F || tr.normal[2] < 0.7F)) { return; }
        if (vel[0] == 0.0F && vel[1] == 0.0F) { return; }
        const NavVec untenO = z.ort;
        const NavVec untenV = vel;
        NavVec oben = startO;
        oben[2] += stufe;
        tr = kastenSpur(startO, oben, kNavMaskNpcSolid);
        if (tr.allsolid || tr.startsolid || tr.fraction == 0.0F) { return; }   // "can't step up"
        const NavVec obenEnde = tr.endpos;
        z.ort = obenEnde;
        vel = startV;
        (void)schieben(gravMod);
        const NavVec schiebeWeg = minus(untenO, startO);
        const NavVec stufenWeg = minus(obenEnde, z.ort);
        if (std::fabs(stufenWeg[0]) < 0.1F && std::fabs(stufenWeg[1]) < 0.1F && laenge2(schiebeWeg) > laenge2(stufenWeg)) {
            z.ort = untenO;   // "slideMove was better, use it"
            vel = untenV;
            return;
        }
        unten = z.ort;
        unten[2] -= stufe;
        tr = kastenSpur(z.ort, unten, kNavMaskNpcSolid);
        if (!tr.allsolid) {
            z.ort = tr.endpos;
            if (tr.fraction < 1.0F) { abgleiten(vel, tr.normal, kOverclip); }
        } else {
            z.ort = untenO;
            vel = untenV;
        }
    }
    // PM_Friction (bg_pmove.cpp:790-940 MD): am Boden (nicht SURF_SLICK) mit
    // Reibung; in der Luft nur "if (speed < 1) vel[0] = vel[1] = 0".
    void reibung(bool amBodenReiben) {
        NavVec& vel = z.tempoVek;
        NavVec vec = vel;
        if (gehend) { vec[2] = 0.0F; }   // "ignore slope movement"
        const float speed = laenge(vec);
        if (speed < 1.0F) {
            vel[0] = 0.0F;
            vel[1] = 0.0F;
            return;
        }
        if (!amBodenReiben) { return; }
        const float control = speed < kPmStopSpeed ? kPmStopSpeed : speed;
        const float drop = control * kNpcFriction * frameZeit;
        float neu = speed - drop;
        if (neu < 0.0F) { neu = 0.0F; }
        neu /= speed;
        vel = mal(vel, neu);
    }
    // PM_Accelerate (bg_pmove.cpp:949 MD)
    void beschleunigen(const NavVec& wishdir, float wishspeed, float accel) {
        NavVec& vel = z.tempoVek;
        const float aktuell = punkt(vel, wishdir);
        const float dazu = wishspeed - aktuell;
        if (dazu <= 0.0F) { return; }
        float acc = accel * frameZeit * wishspeed;
        if (acc > dazu) { acc = dazu; }
        vel = plus(vel, mal(wishdir, acc));
    }
    // PM_WalkMove (bg_pmove.cpp:4251-4530 MD) fuer NPCs mit moveDir.
    void gehBewegung() {
        NavVec& vel = z.tempoVek;
        reibung(true);
        // wishdir = moveDir, wishspeed = ps.speed (4360-4377)
        beschleunigen(zBewegung, static_cast<float>(z.tempo), kPmAccelerate);
        // PM_ClipVelocity auf den Boden, Laenge behalten (4515-4522)
        const float l = laenge(vel);
        abgleiten(vel, bodenNormale, kOverclip);
        const float l2 = laenge(vel);
        if (l2 > 0.0F) { vel = mal(vel, l / l2); }
        if (vel[0] == 0.0F && vel[1] == 0.0F) { return; }   // "don't do anything if standing still"
        stufenSchieben(0.0F);
    }
    // PM_AirMove (bg_pmove.cpp:4058-4245 MD): wishvel aus ucmd mit
    // PM_CmdScale (ps.speed), pml.forward/right flach, pm_airaccelerate,
    // pm_airDecelRate gegen die Richtung, dann PM_StepSlideMove(1).
    void luftBewegung() {
        reibung(false);
        int maxm = std::abs(static_cast<int>(ucmdVor));
        if (std::abs(static_cast<int>(ucmdSeit)) > maxm) { maxm = std::abs(static_cast<int>(ucmdSeit)); }
        float scale = 0.0F;
        if (maxm != 0) {
            const float total = std::sqrt(static_cast<float>(ucmdVor * ucmdVor + ucmdSeit * ucmdSeit));
            scale = static_cast<float>(z.tempo) * static_cast<float>(maxm) / (127.0F * total);
        }
        NavVec vorn{};
        NavVec rechts{};
        qAchsen(z.gier, vorn, rechts);
        vorn[2] = 0.0F;
        rechts[2] = 0.0F;
        (void)vektorNorm(vorn);
        (void)vektorNorm(rechts);
        // fmove/smove sind die ROHEN ucmd-Werte, PM_CmdScale kommt erst mit
        // wishspeed (VectorNormalize(wishdir) * scale - wie PM_NoclipMove).
        NavVec wishvel{vorn[0] * static_cast<float>(ucmdVor) + rechts[0] * static_cast<float>(ucmdSeit),
                       vorn[1] * static_cast<float>(ucmdVor) + rechts[1] * static_cast<float>(ucmdSeit), 0.0F};
        NavVec wishdir = wishvel;
        float wishspeed = vektorNorm(wishdir);
        wishspeed *= scale;
        if (punkt(z.tempoVek, wishdir) < 0.0F) { wishspeed *= 1.35F; }   // pm_airDecelRate
        beschleunigen(wishdir, wishspeed, 4.0F);                          // pm_airaccelerate
        if (bodenFlaeche) { abgleiten(z.tempoVek, bodenNormale, kOverclip); }
        stufenSchieben(1.0F);
    }

    // Pmove: PmoveSingle (mit Spuren: Boden, Luft, Schwerkraft - siehe oben;
    // ohne Spuren der Boden aus welt.boden wie bis teil 2).
    void pmove(float dt = 0.05F, int ms = 50) {
        frameZeit = dt;
        frameMs = ms;
        schwer = w.schwerkraft;
        NavVec& vel = z.tempoVek;
        const bool amBoden = static_cast<bool>(welt.boden);
        const bool mitWaenden = amBoden && (static_cast<bool>(welt.spur) || static_cast<bool>(welt.spurZeit)) && w.fallen;
        if (mitWaenden) {
            const NavVec vorher = z.ort;
            vorherTempo = vel;               // pml.previous_velocity
            bodenSpur();                     // PM_GroundTrace
            // PM_DropTimers (bg_pmove.cpp:33160-33175 MD)
            if (z.pmZeit != 0) { z.pmZeit = frameMs >= z.pmZeit ? 0 : z.pmZeit - frameMs; }
            if (gehend) {
                gehBewegung();
            } else {
                luftBewegung();   // ps.gravity > 0; FLY_NORMAL/PM_FlyMove rechnet behaved nicht (w.fallen)
            }
            if (z.ort != vorher) { bodenSpur(); }
            return;
        }
        // --- ohne Spuren (oder ohne Fallen): wie bis teil 2 ---------------------
        // PM_Friction (bg_pmove.cpp:790): pml.walking -> vec[2] = 0
        {
            NavVec vec = vel;
            if (amBoden) { vec[2] = 0.0F; }
            const float speed = laenge(vec);
            if (speed < 1.0F) {
                vel[0] = 0.0F;
                vel[1] = 0.0F;
            } else {
                const float control = speed < kPmStopSpeed ? kPmStopSpeed : speed;
                const float drop = control * kNpcFriction * dt;
                float neu = speed - drop;
                if (neu < 0.0F) { neu = 0.0F; }
                neu /= speed;
                vel = mal(vel, neu);
            }
        }
        beschleunigen(zBewegung, static_cast<float>(z.tempo), kPmAccelerate);
        if (amBoden) {
            const float l = laenge(vel);
            float backoff = vel[2];
            backoff = backoff < 0.0F ? backoff * kOverclip : backoff / kOverclip;
            vel[2] -= backoff;
            const float l2 = laenge(vel);
            if (l2 > 0.0F) { vel = mal(vel, l / l2); }
            if (vel[0] == 0.0F && vel[1] == 0.0F) { return; }
        } else if (istNull(vel)) {
            return;
        }
        z.ort = plus(z.ort, mal(vel, dt));
        if (amBoden) {
            // Auf dem Boden bleiben (ohne Spuren gibt es nur welt.boden).
            float b = 0.0F;
            if (welt.boden(z.ort[0], z.ort[1], z.ort[2] + w.stufe, z.ort[2] - w.stufe - 1.0F, b)) {
                z.ort[2] = b - kDefaultMins2;
            }
        }
    }

    // --- BS_NOCLIP -------------------------------------------------------------
    // VectorNormalize (q_math): mit dem Kehrwert malnehmen, Null bleibt Null.
    static float vektorNorm(NavVec& v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l != 0.0F) {
            const float il = 1.0F / l;
            v = {v[0] * il, v[1] * il, v[2] * il};
        }
        return l;
    }
    // NPC_BSNoClip (NPC_behavior.cpp:1182-1212 MD), Zweig mit Ziel: desiredYaw
    // = vectoangles(Ziel - Ort), ucmd aus den Achsen von currentAngles VOR
    // dem Drehen (Nick 0 - NPC_BSNoClip setzt kein desiredPitch).
    void ohneKoerperZiel(signed char& vor, signed char& seit, signed char& hoch) {
        NavVec dir = minus(w.ziel, z.ort);
        z.wunschGier = qGier(dir);
        NavVec vorn{};
        NavVec rechts{};
        qAchsen(z.gier, vorn, rechts);
        (void)vektorNorm(dir);
        vor = static_cast<signed char>(std::floor(punkt(vorn, dir) * 127.0F));
        seit = static_cast<signed char>(std::floor(punkt(rechts, dir) * 127.0F));
        hoch = static_cast<signed char>(std::floor(dir[2] * 127.0F));
    }
    // PM_NoclipMove (bg_pmove.cpp:4860-4939 MD) mit pml.forward/right aus
    // ps.viewangles NACH NPC_UpdateAngles. Kein Boden, keine Waende.
    void pmoveOhneKoerper(signed char vor, signed char seit, signed char hoch) {
        const float dt = 0.05F;
        NavVec& vel = z.tempoVek;
        // Reibung: pm_friction * 1.5 ("extra friction"), control mindestens
        // pm_stopspeed
        const float speed = laenge(vel);
        if (speed < 1.0F) {
            vel = {0.0F, 0.0F, 0.0F};
        } else {
            constexpr float kReibung = 6.0F * 1.5F;
            const float control = speed < kPmStopSpeed ? kPmStopSpeed : speed;
            const float drop = control * kReibung * dt;
            float neu = speed - drop;
            if (neu < 0.0F) { neu = 0.0F; }
            neu /= speed;
            vel = mal(vel, neu);
        }
        // PM_CmdScale (bg_pmove.cpp:980-1003): ps.speed * max / (127 * total)
        int maxm = std::abs(static_cast<int>(vor));
        if (std::abs(static_cast<int>(seit)) > maxm) { maxm = std::abs(static_cast<int>(seit)); }
        if (std::abs(static_cast<int>(hoch)) > maxm) { maxm = std::abs(static_cast<int>(hoch)); }
        float scale = 0.0F;
        if (maxm != 0) {
            const float total = std::sqrt(static_cast<float>(vor * vor + seit * seit + hoch * hoch));
            scale = static_cast<float>(z.tempo) * static_cast<float>(maxm) / (127.0F * total);
        }
        NavVec vorn{};
        NavVec rechts{};
        qAchsen(z.gier, vorn, rechts);   // pml.forward/right aus ps.viewangles
        NavVec wishvel{};
        for (std::size_t i = 0; i < 3; ++i) {
            wishvel[i] = vorn[i] * static_cast<float>(vor) + rechts[i] * static_cast<float>(seit);
        }
        wishvel[2] += static_cast<float>(hoch);
        NavVec wishdir = wishvel;
        float wishspeed = vektorNorm(wishdir);
        wishspeed *= scale;
        // PM_Accelerate (bg_pmove.cpp:949) mit pm_accelerate
        const float aktuell = punkt(vel, wishdir);
        const float dazu = wishspeed - aktuell;
        if (dazu > 0.0F) {
            float acc = kPmAccelerate * dt * wishspeed;
            if (acc > dazu) { acc = dazu; }
            vel = plus(vel, mal(wishdir, acc));
        }
        // VectorMA (ps.origin, frametime, velocity)
        z.ort = plus(z.ort, mal(vel, dt));
    }
};

}  // namespace

std::shared_ptr<const NavWelt> navWeltAusEntities(const std::vector<MapEntity>& ents) {
    // Nach dem Inhalt der Entities, die das Netz bilden.
    std::uint64_t h = 1469598103934665603ULL;
    const auto dazu = [&h](const std::string& t) {
        for (const char c : t) { h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ULL; }
        h = (h ^ 0xffU) * 1099511628211ULL;
    };
    for (const MapEntity& e : ents) {
        const std::string k = klein(e.classname);
        if (k != "waypoint" && k != "waypoint_small" && k != "waypoint_navgoal" && k != "point_combat") { continue; }
        dazu(k);
        dazu(e.origin);
        dazu(e.targetname);
        for (const char* key : {"target", "target2", "target3", "target4", "targetJump", "spawnflags", "radius"}) {
            dazu(text(e, key));
        }
    }
    static std::mutex m;
    static std::map<std::uint64_t, std::shared_ptr<const NavWelt>> speicher;
    const std::lock_guard<std::mutex> lock(m);
    const auto it = speicher.find(h);
    if (it != speicher.end()) { return it->second; }
    auto w = std::make_shared<NavWelt>();
    w->netz = baueNavNetz(ents, NavBauHilfe{});
    w->kennung = h | 1ULL;
    if (speicher.size() > 64) { speicher.clear(); }
    speicher[h] = w;
    return w;
}

int navNaechsterKnoten(const NavWelt& welt, const NavVec& pos, int vorher, int ziel) {
    LaufWerte w;
    LaufZustand z;
    Lauf l(welt, w, z);
    return l.naechster(pos, vorher, ziel);
}

namespace {
// --- Zwischenspeicher der Plaene ----------------------------------------------
std::uint64_t mische(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}
std::uint64_t bits(float f) {
    std::uint32_t u = 0;
    static_assert(sizeof(u) == sizeof(f));
    std::memcpy(&u, &f, sizeof(u));
    return u;
}
std::uint64_t planSchluessel(const NavWelt& welt, const LaufZustand& z, const LaufWerte& w, double ab, double grenze) {
    std::uint64_t h = welt.kennung;
    for (const float f : z.ort) { h = mische(h, bits(f)); }
    for (const float f : z.tempoVek) { h = mische(h, bits(f)); }
    for (const float f : {z.gier, z.wunschGier, z.pfadGier, w.zielRadius, w.maxTempo, w.gehTempo, w.drehRate,
                          w.halbBreite, w.hoeheOben, w.stufe, static_cast<float>(w.gangSchalter)}) {
        h = mische(h, bits(f));
    }
    for (const float f : w.ziel) { h = mische(h, bits(f)); }
    for (const int i : {z.tempo, z.seiteAusweichen, z.letzteSeite, z.seiteBis, z.safeCheckBis, z.safeDauerBis, z.wegpunkt,
                        z.letzterWegpunkt, z.wegpunktBis, z.zielWegpunkt, z.zielLetzterWegpunkt, z.zielWegpunktBis,
                        z.pfadEnde, z.pfadAStarBis, z.pfadGroesse, static_cast<int>(z.pfadDa), static_cast<int>(z.pfadErfolg),
                        static_cast<int>(w.zielGesetzt), static_cast<int>(w.ohneKoerper)}) {
        h = mische(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(i)));
    }
    // rc601 (bewegung, teil 3): Boden, Fallen, Vorlauf.
    for (const int i : {static_cast<int>(z.amBoden), z.pmZeit, static_cast<int>(w.fallen), static_cast<int>(w.spawnVorlauf),
                        static_cast<int>(w.spawnImFesten)}) {
        h = mische(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(i)));
    }
    h = mische(h, bits(w.schwerkraft));
    h = mische(h, static_cast<std::uint64_t>(std::llround(std::max(-1.0e12, w.zielAbMs) * 16.0)));
    // rc601 integ: Takt des Verhaltens (OpenJK) und was er weiterbenutzt.
    if (w.verhaltenTaktMs > static_cast<int>(kNavDenkMs)) {
        h = mische(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(w.verhaltenTaktMs)));
        h = mische(h, static_cast<std::uint64_t>(std::llround(std::max(-1.0e12, z.denkNullMs) * 16.0)));
        for (const float f : z.letzteBewegung) { h = mische(h, bits(f)); }
        for (const int i : {static_cast<int>(z.letzterVor), static_cast<int>(z.letzterSeit), static_cast<int>(z.letzteGehTaste)}) {
            h = mische(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(i)));
        }
    }
    // Der Pfad mit seinem Inhalt (nicht dem Zeiger): ein Plan nach einem
    // Weg ueber das Netz beginnt fast immer mit einem Pfad.
    if (z.pfad) {
        for (const NavPfadPunkt& pp : *z.pfad) {
            for (const float f : {pp.punkt[0], pp.punkt[1], pp.punkt[2], pp.tempo, pp.abbremsRadius, pp.erreichtRadius2,
                                  pp.abstand, pp.eta}) {
                h = mische(h, bits(f));
            }
            h = mische(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(pp.knoten)));
        }
    }
    h = mische(h, static_cast<std::uint64_t>(std::llround(ab * 16.0)));
    h = mische(h, static_cast<std::uint64_t>(std::llround(grenze)));
    return h;
}
// rc601 (bewegung, teil 3): ein Eintrag merkt sich, mit welchem Stand der
// Mover er gerechnet wurde.
struct PlanEintrag {
    std::shared_ptr<const LaufPlan> plan;
    std::shared_ptr<const NavMoverPlan> mover;
};
struct PlanSpeicher {
    std::mutex m;
    std::list<std::pair<std::uint64_t, PlanEintrag>> lru;
    std::unordered_map<std::uint64_t, decltype(lru)::iterator> index;
};
PlanSpeicher& planSpeicher() {
    static PlanSpeicher s;
    return s;
}

// rc601 (bewegung, teil 3): Gilt ein Plan, der mit den Movern `alt` gerechnet
// wurde, auch mit `neu`? Ja, wenn in keinem Denkbild eine Spur einen Mover
// beruehrt, der zu dieser Zeit anders steht (beide Stellungen, mit dem Radius
// um den Ursprung - wie die grobe Pruefung der Spur), und kein Denkbild, das
// is_valid einer Tuer-/Wandkante fragte, einen anderen moverState/contents
// sieht. Eine Spur haengt nur an dem, was ihren Kasten beruehrt; is_valid nur
// an diesen Zustaenden - also rechnete die Engine dann Bit fuer Bit dasselbe.
bool planGiltMit(const LaufPlan& p, const NavMoverPlan* alt, const NavMoverPlan* neu, const NavNetz* netz) {
    if (alt == neu) { return true; }
    if ((alt == nullptr || alt->mover.empty()) && (neu == nullptr || neu->mover.empty())) { return true; }
    if (alt != nullptr && neu != nullptr && alt->kennung != 0 && alt->kennung == neu->kennung) { return true; }
    // Alle Mover, die in einem der beiden Plaene stehen.
    std::vector<int> ents;
    for (const NavMoverPlan* mp : {alt, neu}) {
        if (mp == nullptr) { continue; }
        for (const NavMoverPlan::Mover& m : mp->mover) {
            if (std::find(ents.begin(), ents.end(), m.entity) == ents.end()) { ents.push_back(m.entity); }
        }
    }
    const auto standIn = [](const NavMoverPlan* mp, int e, double t, const NavMoverPlan::Stand*& s, float& r) {
        if (mp == nullptr) { return false; }
        const NavMoverPlan::Stand* x = mp->standVon(e, t);
        if (x == nullptr) { return false; }
        s = x;
        r = mp->mover[static_cast<std::size_t>(mp->nachEntity[static_cast<std::size_t>(e)])].radius;
        return true;
    };
    // is_valid liest den Stand nur fuer die Entity einer Tuer-/Wandkante.
    std::vector<char> anKante(ents.size(), 0);
    if (netz != nullptr) {
        for (std::size_t i = 1; i < netz->kanten.size(); ++i) {
            const NavKante& kn = netz->kanten[i];
            if (!(kn.hat(NavKante::TuerSperrt) || kn.hat(NavKante::WandSperrt))) { continue; }
            for (std::size_t j = 0; j < ents.size(); ++j) {
                if (ents[j] == kn.entity) { anKante[j] = 1; }
            }
        }
    }
    for (const LaufTakt& k : p.takte) {
        for (std::size_t ei = 0; ei < ents.size(); ++ei) {
            const int e = ents[ei];
            const NavMoverPlan::Stand* sa = nullptr;
            const NavMoverPlan::Stand* sn = nullptr;
            float ra = 0.0F;
            float rn = 0.0F;
            const bool inAlt = standIn(alt, e, k.t, sa, ra);
            const bool inNeu = standIn(neu, e, k.t, sn, rn);
            // Wer nur in einem Plan steht, steht im anderen wie am Anfang.
            if (!inAlt) {
                const NavMoverPlan::Mover& m = neu->mover[static_cast<std::size_t>(neu->nachEntity[static_cast<std::size_t>(e)])];
                sa = &m.anfang;
                ra = m.radius;
            }
            if (!inNeu) {
                const NavMoverPlan::Mover& m = alt->mover[static_cast<std::size_t>(alt->nachEntity[static_cast<std::size_t>(e)])];
                sn = &m.anfang;
                rn = m.radius;
            }
            if (sa->gleich(*sn)) { continue; }
            if (k.kantenGefragt && anKante[ei] != 0 &&
                (sa->zustand != sn->zustand || sa->fest != sn->fest || sa->verriegelt != sn->verriegelt)) {
                return false;
            }
            const NavMoverPlan* mp = inNeu ? neu : alt;
            const NavMoverPlan::Mover& mv = mp->mover[static_cast<std::size_t>(mp->nachEntity[static_cast<std::size_t>(e)])];
            for (const auto& [s, r] : {std::pair{sa, ra}, std::pair{sn, rn}}) {
                if (!s->fest) { continue; }   // ohne contents hielt er keine Spur auf
                // Ohne Drehung der Kasten des Modells (absmin/absmax mit dem
                // Epsilon 1 aus SV_LinkEntity), sonst die Kugel um den Ursprung.
                const bool gedreht = s->winkel[0] != 0.0F || s->winkel[1] != 0.0F || s->winkel[2] != 0.0F || !mv.kastenDa;
                bool trifft = true;
                for (std::size_t a = 0; a < 3; ++a) {
                    const float lo = gedreht ? s->ort[a] - r : s->ort[a] + mv.kastenMin[a] - 1.0F;
                    const float hi = gedreht ? s->ort[a] + r : s->ort[a] + mv.kastenMax[a] + 1.0F;
                    if (hi < k.spurMin[a] || lo > k.spurMax[a]) { trifft = false; }
                }
                if (trifft) { return false; }
            }
        }
    }
    return true;
}
}  // namespace

// --- rc601 (bewegung, teil 3): NavMoverPlan ------------------------------------
const NavMoverPlan::Stand& NavMoverPlan::bei(const Mover& m, double t) const {
    if (m.takte.empty()) { return m.anfang; }
    const double f = std::floor((t + 0.5) / kNavDenkMs);
    if (f <= 0.0) { return m.takte.front(); }
    const auto i = static_cast<std::size_t>(f);
    return i < m.takte.size() ? m.takte[i] : m.takte.back();
}
const NavMoverPlan::Stand* NavMoverPlan::standVon(int entity, double t) const {
    if (entity < 0 || static_cast<std::size_t>(entity) >= nachEntity.size()) { return nullptr; }
    const int i = nachEntity[static_cast<std::size_t>(entity)];
    if (i < 0) { return nullptr; }
    return &bei(mover[static_cast<std::size_t>(i)], t);
}
bool NavMoverPlan::kantenUnveraendert(double t) const {
    if (kantenWieAnfang.empty()) { return true; }
    const double f = std::floor((t + 0.5) / kNavDenkMs);
    const std::size_t i = f <= 0.0 ? 0U : std::min(static_cast<std::size_t>(f), kantenWieAnfang.size() - 1U);
    return kantenWieAnfang[i];
}
void NavMoverPlan::fertig(std::size_t entities) {
    nachEntity.assign(entities, -1);
    std::size_t takte = 0;
    std::uint64_t h = 1469598103934665603ULL;
    const auto dazu = [&h](const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) { h = (h ^ b[i]) * 1099511628211ULL; }
    };
    for (std::size_t i = 0; i < mover.size(); ++i) {
        const Mover& m = mover[i];
        if (m.entity >= 0 && static_cast<std::size_t>(m.entity) < entities) { nachEntity[static_cast<std::size_t>(m.entity)] = static_cast<int>(i); }
        takte = std::max(takte, m.takte.size());
        dazu(&m.entity, sizeof(m.entity));
        for (const Stand& s : m.takte) {
            dazu(s.ort.data(), sizeof(float) * 3);
            dazu(s.winkel.data(), sizeof(float) * 3);
            const unsigned char z[3] = {static_cast<unsigned char>(s.fest), static_cast<unsigned char>(s.zustand),
                                        static_cast<unsigned char>(s.verriegelt)};
            dazu(z, 3);
        }
    }
    kantenWieAnfang.assign(std::max<std::size_t>(takte, 1U), true);
    for (std::size_t f = 0; f < kantenWieAnfang.size(); ++f) {
        for (const Mover& m : mover) {
            const Stand& s = m.takte.empty() ? m.anfang : m.takte[std::min(f, m.takte.size() - 1U)];
            if (s.zustand != m.anfang.zustand || s.fest != m.anfang.fest || s.verriegelt != m.anfang.verriegelt) {
                kantenWieAnfang[f] = false;
                break;
            }
        }
    }
    kennung = mover.empty() ? 0U : (h | 1U);
}

namespace {
// rc601 (bewegung, teil 3): die Rechnung eines Plans Denkbild fuer Denkbild,
// damit mehrere Figuren im selben Takt rechnen koennen (navKoerperLauf) -
// planeLauf ruft schritt() bis fertig.
struct LaufRechner {
    const NavWelt& welt;
    LaufPlan plan;
    LaufZustand z;
    LaufWerte w;
    bool zielDa = false;
    double t = 0.0;
    double grenzeMs = 0.0;
    int ersteMs = 50;
    bool ersterZielTakt = true;
    NavVec ruheOrt{};
    double ruheSeit = 0.0;
    bool fertig = false;
    bool ohneNie = false;   // kein Abbruch, wenn sie lange steht (andere Figuren koennen den Weg freigeben)
    LaufRechner(const NavWelt& wl, const LaufZustand& start, const LaufWerte& werte, double abMs, double grenze)
        : welt(wl), grenzeMs(grenze) {
        plan.abMs = abMs;
        plan.anfang = start;
        plan.hoeheGilt = welt.boden && (welt.spur || welt.spurZeit) && werte.fallen && !werte.ohneKoerper;
        z = start;
        w = werte;
        zielDa = w.zielGesetzt;
        z.vorlaufErledigt();   // rc601 (bewegung, teil 3): dieser Plan hat ihn
        // Das erste Denkbild nach abMs auf dem 50-ms-Raster.
        t = kNavDenkMs * (std::floor((abMs + 0.5) / kNavDenkMs) + 1.0);
        // rc601 (bewegung, teil 3): Vorlauf ab dem Spawn - G_CheckInSolid
        // (g_utils.cpp:1641-1674: der Kasten ab dem Ursprung um mins[2] hinab,
        // mins[2] dabei 0; trifft er, kommt der Ursprung dorthin), ein Pmove von
        // 200 ms in NPC_Begin, das erste NPC_Think 150 ms spaeter mit 200 ms.
        if (w.spawnVorlauf && !w.ohneKoerper) {
            Lauf l0(welt, w, z);
            l0.jetzt = static_cast<int>(std::lround(abMs));
            l0.akt.t = abMs;
            if (w.spawnImFesten && (welt.spur || welt.spurZeit)) {
                NavVec m0 = l0.mins;
                m0[2] = 0.0F;
                NavVec ende = z.ort;
                ende[2] += l0.mins[2];
                const NavSpur s = spurDurch(l0.spurF, z.ort, m0, l0.maxs, ende, kNavKeineEntity, kNavMaskNpcSolid);
                if (!s.allsolid && !s.startsolid && s.fraction < 1.0F) {
                    z.ort = s.endpos;
                    z.ort[2] -= l0.mins[2];
                }
            }
            l0.zBewegung = {0, 0, 0};
            l0.pmove(0.2F, 200);
            LaufTakt takt;
            takt.t = abMs;
            takt.msec = 200;   // rc603 fix603
            takt.ort = z.ort;
            takt.tempo = z.tempo;
            takt.zustandWunschGier = z.wunschGier;
            takt.spurMin = l0.spMin;
            takt.spurMax = l0.spMax;
            takt.koerperMin = l0.kpMin;
            takt.koerperMax = l0.kpMax;
            plan.takte.push_back(takt);
            plan.zustaende.push_back(z);
            t = abMs + 150.0;
            ersteMs = 200;
        }
        ruheOrt = z.ort;
        ruheSeit = t;
        fertig = !(t <= grenzeMs + 0.5);
    }
    void schritt() {
        if (fertig) { return; }
        Lauf l(welt, w, z);
        l.jetzt = static_cast<int>(std::lround(t));
        l.akt.t = t;   // rc601 (bewegung, teil 3): die Mover in diesem Denkbild
        bool bewegt = false;
        bool gehTaste = false;
        l.zBewegung = {0, 0, 0};   // NPC_Think: VectorClear(ps.moveDir)
        // rc601 (bewegung, teil 3): vor zielAbMs nur der Vorlauf (kein Ziel).
        const bool zielZeit = t > w.zielAbMs + 0.5;
        // rc601 integ: laeuft in diesem Denkbild das Verhalten (nextBStateThink
        // <= level.time)? Movie Duels immer; OpenJK alle 100 ms ab dem ersten
        // NPC_Think nach dem Spawn (denkNullMs), siehe npcVerhaltenTaktMs.
        const bool mitTakt = w.verhaltenTaktMs > static_cast<int>(kNavDenkMs);
        const bool verhalten = !mitTakt || [&] {
            const double r = std::fmod(t - z.denkNullMs, static_cast<double>(w.verhaltenTaktMs));
            const double rr = r < 0.0 ? r + static_cast<double>(w.verhaltenTaktMs) : r;
            return rr < 0.5 || rr > static_cast<double>(w.verhaltenTaktMs) - 0.5;
        }();
        const bool zielJetzt = zielDa && zielZeit && verhalten;
        // UpdateGoal (NPC_goal.cpp:173)
        if (zielJetzt && l.erreicht(w.ziel, w.zielRadius)) {
            zielDa = false;
            plan.ankunftMs = t;
            if (ersterZielTakt) { plan.zielAnfangsErreicht = true; }
        }
        if (zielZeit) { ersterZielTakt = false; }
        signed char vor = 0;
        signed char seit = 0;
        signed char hoch = 0;
        if (!verhalten) {
            // rc601 integ: OpenJK ohne Verhalten in diesem Denkbild
            // (NPC.cpp:2502-2526): "VectorCopy(oldMoveDir, ps.moveDir)",
            // NPC_UpdateAngles(qtrue, qtrue) mit dem alten desiredYaw und
            // ClientThink mit NPCInfo->last_ucmd. ps.speed bleibt (STEER
            // setzt es nur im Verhalten; ClientAlterSpeed laeuft fuer NPCs
            // mit USENEWNAVSYSTEM nicht, g_active.cpp:5227-5230).
            l.zBewegung = z.letzteBewegung;
            l.ucmdVor = z.letzterVor;
            l.ucmdSeit = z.letzterSeit;
            bewegt = z.letzterVor != 0 || z.letzterSeit != 0;
            gehTaste = z.letzteGehTaste;
            if (w.ohneKoerper) {
                // NPC_BSNoClip lief nicht: der letzte ucmd (forward/right/up)
                // geht in PM_NoclipMove, hier nur vor/seit (upmove fuehrt der
                // Zustand nicht - Naeherung, siehe NOTIZEN_integ).
                vor = z.letzterVor;
                seit = z.letzterSeit;
            }
        } else if (w.ohneKoerper) {
            // NPC_BSNoClip: mit Ziel ucmd gerade darauf, sonst "Cut
            // velocity" (VectorClear(ps.velocity)). ps.speed bleibt, wie es
            // ist - STEER laeuft nicht.
            if (zielDa && zielZeit) {
                l.ohneKoerperZiel(vor, seit, hoch);
            } else {
                z.tempoVek = {0.0F, 0.0F, 0.0F};
            }
        } else if (zielDa && zielZeit) {
            // NPC_MoveToGoal (NPC_move.cpp:892-955)
            l.aktivieren();
            bool ok = l.steerHin();
            if (!ok) {
                ok = l.navHin();
                if (!ok) { l.stop(); }
            }
            l.deaktivieren(bewegt, gehTaste);
        }
        // NPC_BSCinematic: ein Watchtarget ueberschreibt desiredYaw (nur im
        // Verhalten - rc601 integ, OpenJK-Takt)
        if (w.fremdGier && !w.ohneKoerper && zielZeit && verhalten) {
            float g = 0.0F;
            if (w.fremdGier(t, z.ort, g)) { z.wunschGier = g; }
        }
        l.drehen();
        if (w.ohneKoerper) {
            // PmoveSingle: PM_NOCLIP -> PM_NoclipMove, PM_DropTimers, return -
            // kein PM_Footsteps, die Beine behalten ihre Animation (bewegt
            // bleibt false).
            l.pmoveOhneKoerper(vor, seit, hoch);
        } else {
            l.pmove(static_cast<float>(ersteMs) * 0.001F, ersteMs);
        }
        // rc603 fix603: LaufTakt::msec (PM_NoclipMove rechnet hier fest 50).
        const int bildMs = w.ohneKoerper ? 50 : ersteMs;
        ersteMs = 50;
        if (mitTakt) {
            // rc601 integ: oldMoveDir fuer das naechste Denkbild; last_ucmd nur
            // aus einem Denkbild mit Verhalten (NPC.cpp:2279 memcpy nach
            // NPC_ExecuteBState). Ohne Ziel ist der ucmd dort leer.
            z.letzteBewegung = l.zBewegung;
            if (verhalten) {
                z.letzterVor = w.ohneKoerper ? vor : l.ucmdVor;
                z.letzterSeit = w.ohneKoerper ? seit : l.ucmdSeit;
                z.letzteGehTaste = gehTaste;
            }
        }
        LaufTakt takt;
        takt.t = t;
        takt.msec = bildMs;
        takt.ort = z.ort;
        takt.tempo = z.tempo;
        takt.bewegt = bewegt;
        takt.gehTaste = gehTaste;
        takt.gehAnim = w.gangSchalter == 2 ? false : (w.gangSchalter == 1 ? true : gehTaste);
        takt.stueck = l.stueck;
        takt.richtung = l.richtung;
        takt.zustandWunschGier = z.wunschGier;
        takt.spurMin = l.spMin;   // rc601 (bewegung, teil 3)
        takt.spurMax = l.spMax;
        takt.koerperMin = l.kpMin;
        takt.koerperMax = l.kpMax;
        takt.kantenGefragt = l.kantenGefragt;
        plan.takte.push_back(takt);
        plan.zustaende.push_back(z);
        // Ende? (In BS_NOCLIP gibt es keinen Boden - alle drei Achsen.)
        // rc601 (bewegung, teil 3): mit Spuren steht nur, wer auf dem Boden
        // ist und nicht faellt.
        const bool mitFallen = welt.boden && (welt.spur || welt.spurZeit) && w.fallen && !w.ohneKoerper;
        const bool steht = z.tempoVek[0] == 0.0F && z.tempoVek[1] == 0.0F &&
                           (mitFallen ? (z.amBoden && z.tempoVek[2] == 0.0F)
                                      : ((welt.boden && !w.ohneKoerper) || z.tempoVek[2] == 0.0F));
        if (!zielZeit) {
            // Vorlauf: liegt sie still, aendert sich bis zum Befehl nichts mehr -
            // weiter mit dem Denkbild nach zielAbMs.
            if (steht) {
                const double weiter = kNavDenkMs * (std::floor((w.zielAbMs + 0.5) / kNavDenkMs) + 1.0);
                t = std::max(t + kNavDenkMs, weiter);
                ruheOrt = z.ort;
                ruheSeit = t;
                fertig = !(t <= grenzeMs + 0.5);
                return;
            }
            t += kNavDenkMs;
            fertig = !(t <= grenzeMs + 0.5);
            return;
        }
        if (!zielDa) {
            if (steht) {
                plan.ruheMs = t;
                fertig = true;
                return;
            }
        } else {
            if (steht && z.ort == ruheOrt) {
                // Mit Ziel, ohne Bewegung: so lange, dass sich weder die
                // Spur-Timer (1,5/2 s) noch der A*-Speicher (4,5 s) aendern.
                if (!ohneNie && t - ruheSeit >= 7000.0) {
                    plan.nie = true;
                    fertig = true;
                    return;
                }
            } else {
                ruheOrt = z.ort;
                ruheSeit = t;
            }
        }
        t += kNavDenkMs;
        fertig = !(t <= grenzeMs + 0.5);
    }
};

}  // namespace

LaufPlan planeLauf(const NavWelt& welt, const LaufZustand& start, const LaufWerte& werte, double abMs, double grenzeMs) {
    const bool speichern = welt.kennung != 0 && !werte.fremdGier;
    std::uint64_t schluessel = 0;
    if (speichern) {
        schluessel = planSchluessel(welt, start, werte, abMs, grenzeMs);
        PlanSpeicher& ps = planSpeicher();
        const std::lock_guard<std::mutex> lock(ps.m);
        const auto it = ps.index.find(schluessel);
        // rc601 (bewegung, teil 3): nur, wenn der Plan mit dem Stand der Mover
        // jetzt nachweislich derselbe ist (planGiltMit).
        if (it != ps.index.end() &&
            planGiltMit(*it->second->second.plan, it->second->second.mover.get(), welt.mover.get(), welt.netz.get())) {
            ps.lru.splice(ps.lru.begin(), ps.lru, it->second);
            gPlaeneWarm.fetch_add(1, std::memory_order_relaxed);
            return *it->second->second.plan;
        }
    }
    const auto uhrAb = std::chrono::steady_clock::now();
    LaufRechner rechner(welt, start, werte, abMs, grenzeMs);
    while (!rechner.fertig) { rechner.schritt(); }
    LaufPlan plan = std::move(rechner.plan);
    gPlaeneKalt.fetch_add(1, std::memory_order_relaxed);
    gPlaeneKaltUs.fetch_add(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - uhrAb).count(),
        std::memory_order_relaxed);
    if (speichern) {
        PlanSpeicher& ps = planSpeicher();
        const std::lock_guard<std::mutex> lock(ps.m);
        const auto alt = ps.index.find(schluessel);
        if (alt != ps.index.end()) {
            // rc601 (bewegung, teil 3): mit anderen Movern neu gerechnet.
            ps.lru.erase(alt->second);
            ps.index.erase(alt);
        }
        {
            ps.lru.emplace_front(schluessel, PlanEintrag{std::make_shared<const LaufPlan>(plan), welt.mover});
            ps.index[schluessel] = ps.lru.begin();
            while (ps.lru.size() > 4096) {
                ps.index.erase(ps.lru.back().first);
                ps.lru.pop_back();
            }
        }
    }
    return plan;
}

LaufZustand zustandBei(const LaufPlan& p, double t) {
    LaufZustand z = p.anfang;
    for (std::size_t i = 0; i < p.takte.size(); ++i) {
        if (p.takte[i].t > t + 0.5) { break; }
        z = p.zustaende[i];
    }
    return z;
}

NavVec ortBei(const LaufPlan& p, double t) {
    NavVec vorher = p.anfang.ort;
    double tv = p.abMs;
    for (const LaufTakt& k : p.takte) {
        if (k.t >= t) {
            // Vor dem ersten Denkbild bewegt sich nichts (das Pmove kommt erst
            // mit ihm); dazwischen linear.
            const double t0 = std::max(tv, k.t - kNavDenkMs);
            if (t <= t0) { return vorher; }
            const auto f = static_cast<float>((t - t0) / (k.t - t0));
            return plus(vorher, mal(minus(k.ort, vorher), f));
        }
        vorher = k.ort;
        tv = k.t;
    }
    return vorher;
}

// --- rc601 (bewegung, Durchsicht 2) -------------------------------------------
void navSchnellwegeAn(bool an) { gSchnellwege.store(an, std::memory_order_relaxed); }
// rc601 (bewegung, teil 3): siehe bhed/navlauf.h.
namespace {
std::atomic<bool> gMovieDuels{true};
}  // namespace
// rc601 integ: siehe bhed/navlauf.h.
int npcVerhaltenTaktMs(bool movieDuels, const std::string& waffe, int spielstufe, const std::string& rang) {
    // Movie Duels NPC.cpp:2359-2368: FRAMETIME/4 bzw. FRAMETIME/2 - beides
    // greift in jedem NPC_Think (nextthink = level.time + FRAMETIME/2).
    if (movieDuels) { return 50; }
    // OpenJK NPC.cpp:2481-2488: "Jedi think faster on hard difficulty,
    // except low-rank (reborn)". TranslateRankName (NPC_stats.cpp:222-268,
    // Q_stricmp): RANK_LT (4), RANK_LT_COMM, RANK_COMMANDER, RANK_CAPTAIN
    // liegen ueber RANK_LT_JG (3); alles andere ist RANK_CIVILIAN (0).
    const std::string r = klein(rang);
    const bool hoch = r == "lt" || r == "ltcomm" || r == "commander" || r == "captain";
    if (klein(waffe) == "wp_saber" && spielstufe >= 2 && hoch) { return 50; }
    return 100;
}

void navMovieDuelsRegeln(bool an) { gMovieDuels.store(an, std::memory_order_relaxed); }
bool navMovieDuelsRegeln() { return gMovieDuels.load(std::memory_order_relaxed); }

NavPlanZaehler navPlanZaehler() {
    NavPlanZaehler z;
    z.kalt = gPlaeneKalt.load(std::memory_order_relaxed);
    z.warm = gPlaeneWarm.load(std::memory_order_relaxed);
    z.msKalt = static_cast<double>(gPlaeneKaltUs.load(std::memory_order_relaxed)) / 1000.0;
    return z;
}

float gierBei(const LaufPlan& p, double t) {
    float vorher = p.anfang.gier;
    double tv = p.abMs;
    for (std::size_t i = 0; i < p.takte.size(); ++i) {
        const LaufTakt& k = p.takte[i];
        if (k.t >= t) {
            const double t0 = std::max(tv, k.t - kNavDenkMs);
            if (t <= t0) { return vorher; }
            const auto f = static_cast<float>((t - t0) / (k.t - t0));
            // LerpAngle (q_math.c): der kuerzere Weg.
            float nach = p.zustaende[i].gier;
            if (nach - vorher > 180.0F) { nach -= 360.0F; }
            if (nach - vorher < -180.0F) { nach += 360.0F; }
            return vorher + f * (nach - vorher);
        }
        vorher = p.zustaende[i].gier;
        tv = k.t;
    }
    return vorher;
}

double planGenauAb(const LaufPlan& p, double t) {
    float vorher = p.anfang.gier;
    for (std::size_t i = 0; i < p.takte.size(); ++i) {
        const LaufTakt& k = p.takte[i];
        if (k.t > t + 0.5 && std::fabs(winkelDelta(vorher, p.zustaende[i].wunschGier)) <= kMinAngleErr) { return k.t; }
        vorher = p.zustaende[i].gier;
    }
    return -1.0;
}

// =============================================================================
// rc601 (bewegung, teil 3): alle Figuren im selben Takt (navKoerperLauf)
// =============================================================================
std::uint64_t navPlanSchluessel(const NavWelt& welt, const LaufZustand& z, const LaufWerte& w, double abMs, double grenzeMs) {
    return planSchluessel(welt, z, w, abMs, grenzeMs);
}

namespace {
// Den Beginn eines Abschnitts aus dem Stand der Figur im gemeinsamen Takt:
// was der Befehl geaendert hat (der Abschnitt weicht vom Plan ohne die
// anderen an dieser Stelle ab), bleibt; der Rest kommt aus dem Takt.
LaufZustand abschnittsBeginn(const LaufZustand& satz, const LaufZustand& alleinBeiSchnitt, const LaufZustand& jetzt) {
    LaufZustand z = satz;
    const auto nimm = [](auto& ziel, const auto& s, const auto& a, const auto& j) {
        if (s == a) { ziel = j; }
    };
    nimm(z.ort, satz.ort, alleinBeiSchnitt.ort, jetzt.ort);
    nimm(z.tempoVek, satz.tempoVek, alleinBeiSchnitt.tempoVek, jetzt.tempoVek);
    nimm(z.tempo, satz.tempo, alleinBeiSchnitt.tempo, jetzt.tempo);
    nimm(z.gier, satz.gier, alleinBeiSchnitt.gier, jetzt.gier);
    nimm(z.wunschGier, satz.wunschGier, alleinBeiSchnitt.wunschGier, jetzt.wunschGier);
    nimm(z.pfadGier, satz.pfadGier, alleinBeiSchnitt.pfadGier, jetzt.pfadGier);
    nimm(z.seiteAusweichen, satz.seiteAusweichen, alleinBeiSchnitt.seiteAusweichen, jetzt.seiteAusweichen);
    nimm(z.letzteSeite, satz.letzteSeite, alleinBeiSchnitt.letzteSeite, jetzt.letzteSeite);
    nimm(z.seiteBis, satz.seiteBis, alleinBeiSchnitt.seiteBis, jetzt.seiteBis);
    nimm(z.safeCheckBis, satz.safeCheckBis, alleinBeiSchnitt.safeCheckBis, jetzt.safeCheckBis);
    nimm(z.safeDauerBis, satz.safeDauerBis, alleinBeiSchnitt.safeDauerBis, jetzt.safeDauerBis);
    nimm(z.wegpunkt, satz.wegpunkt, alleinBeiSchnitt.wegpunkt, jetzt.wegpunkt);
    nimm(z.letzterWegpunkt, satz.letzterWegpunkt, alleinBeiSchnitt.letzterWegpunkt, jetzt.letzterWegpunkt);
    nimm(z.wegpunktBis, satz.wegpunktBis, alleinBeiSchnitt.wegpunktBis, jetzt.wegpunktBis);
    nimm(z.zielWegpunkt, satz.zielWegpunkt, alleinBeiSchnitt.zielWegpunkt, jetzt.zielWegpunkt);
    nimm(z.zielLetzterWegpunkt, satz.zielLetzterWegpunkt, alleinBeiSchnitt.zielLetzterWegpunkt, jetzt.zielLetzterWegpunkt);
    nimm(z.zielWegpunktBis, satz.zielWegpunktBis, alleinBeiSchnitt.zielWegpunktBis, jetzt.zielWegpunktBis);
    nimm(z.amBoden, satz.amBoden, alleinBeiSchnitt.amBoden, jetzt.amBoden);
    nimm(z.pmZeit, satz.pmZeit, alleinBeiSchnitt.pmZeit, jetzt.pmZeit);
    if (satz.pfadDa == alleinBeiSchnitt.pfadDa && satz.pfad == alleinBeiSchnitt.pfad &&
        satz.pfadGroesse == alleinBeiSchnitt.pfadGroesse) {
        z.pfadDa = jetzt.pfadDa;
        z.pfadEnde = jetzt.pfadEnde;
        z.pfadErfolg = jetzt.pfadErfolg;
        z.pfadAStarBis = jetzt.pfadAStarBis;
        z.pfad = jetzt.pfad;
        z.pfadGroesse = jetzt.pfadGroesse;
    }
    return z;
}
bool beruehrt(const NavVec& lo, const NavVec& hi, const NavKoerperKasten& k) {
    // SV_AreaEntities: absmin/absmax der Figur (SV_LinkEntity, +-1) gegen den
    // Kasten der Spur (der schon +-1 hat).
    for (std::size_t i = 0; i < 3; ++i) {
        if (k.ort[i] + k.maxs[i] + 1.0F < lo[i] || k.ort[i] + k.mins[i] - 1.0F > hi[i]) { return false; }
    }
    return true;
}
}  // namespace

NavKoerperErgebnis navKoerperLauf(const NavWelt& welt, const std::vector<NavFigurKoerper>& figuren,
                                  const std::vector<NavAbschnitt>& abschnitte) {
    NavKoerperErgebnis aus;
    aus.plaene.resize(abschnitte.size());
    for (std::size_t i = 0; i < abschnitte.size(); ++i) { aus.plaene[i] = abschnitte[i].allein; }
    if (figuren.size() < 2 || abschnitte.empty()) { return aus; }
    NavWelt kw = welt;
    kw.kennung = 0;
    std::vector<NavKoerperKasten> kaesten;
    kw.koerper = &kaesten;

    enum class Art : std::uint8_t { Keiner, Folgt, Rechnet, Fertig };
    struct Stand {
        std::vector<std::size_t> abs;   // Abschnitte nach abMs
        std::size_t naechster = 0;      // Index in abs
        int aktuell = -1;               // Abschnitt
        Art art = Art::Keiner;
        std::size_t k = 0;              // naechster Takt des Plans ohne die anderen
        std::unique_ptr<LaufRechner> rechner;
        std::shared_ptr<LaufPlan> bau;  // Plan im gemeinsamen Takt (nur wenn abgewichen)
        bool abgewichen = false;
        bool hatZustand = false;
        LaufZustand z;                  // Stand nach dem letzten Denkbild
        double zT = -1.0e300;
    };
    std::vector<Stand> st(figuren.size());
    double tAb = 1.0e300;
    for (std::size_t i = 0; i < abschnitte.size(); ++i) {
        const NavAbschnitt& a = abschnitte[i];
        if (a.figur < 0 || static_cast<std::size_t>(a.figur) >= figuren.size() || !a.allein) { continue; }
        st[static_cast<std::size_t>(a.figur)].abs.push_back(i);
        tAb = std::min(tAb, a.abMs);
    }
    for (Stand& s : st) {
        std::stable_sort(s.abs.begin(), s.abs.end(),
                         [&](std::size_t x, std::size_t y) { return abschnitte[x].abMs < abschnitte[y].abMs; });
    }
    std::vector<std::size_t> reihe(figuren.size());
    for (std::size_t i = 0; i < reihe.size(); ++i) { reihe[i] = i; }
    std::stable_sort(reihe.begin(), reihe.end(), [&](std::size_t x, std::size_t y) { return figuren[x].reihe < figuren[y].reihe; });

    // Wo ist Figur f, und ist sie ein Koerper?
    const auto kastenVon = [&](std::size_t f, double t, NavKoerperKasten& k) {
        const NavFigurKoerper& fg = figuren[f];
        if (t < fg.daAbMs - 0.5 || t >= fg.daBisMs - 0.5) { return false; }
        const Stand& s = st[f];
        double best = -1.0e300;
        bool da = false;
        if (s.hatZustand) {
            k.ort = s.z.ort;
            k.tempo = s.z.tempoVek;
            best = s.zT;
            da = true;
        }
        for (const auto& [ms, ort] : fg.orte) {
            if (ms <= t + 0.5 && ms > best) {
                best = ms;
                k.ort = ort;
                k.tempo = {0.0F, 0.0F, 0.0F};
                da = true;
            }
        }
        if (!da) { return false; }
        k.id = static_cast<int>(f);
        k.mins = {-fg.halbBreite, -fg.halbBreite, kDefaultMins2};
        k.maxs = {fg.halbBreite, fg.halbBreite, fg.hoeheOben};
        return true;
    };
    const auto andere = [&](std::size_t ich, double t) {
        kaesten.clear();
        for (std::size_t f = 0; f < figuren.size(); ++f) {
            if (f == ich) { continue; }
            NavKoerperKasten k;
            if (kastenVon(f, t, k)) { kaesten.push_back(k); }
        }
    };
    const auto frei = [&](const LaufTakt& tk) {
        for (const NavKoerperKasten& k : kaesten) {
            if (beruehrt(tk.koerperMin, tk.koerperMax, k)) { return false; }
        }
        return true;
    };
    // Den Abschnitt ab hier selbst rechnen: ein Rechner ab dem Stand der Figur.
    const auto abweichen = [&](Stand& s, double t) {
        const NavAbschnitt& a = abschnitte[static_cast<std::size_t>(s.aktuell)];
        const LaufPlan& allein = *a.allein;
        // rc603 fix603: pml.msec des Denkbilds t - dieselbe Bildzeit wie im Plan
        // ohne die anderen (LaufTakt::msec). Bis fix603 war es die Zeit seit dem
        // letzten Takt: nach dem Stillstehen bis zum Befehl (eine Luecke in den
        // Takten) rechnete das erste Laufdenkbild ein Pmove ueber Sekunden -
        // dooku5 in md_gd_jedi (cin4_jedi) spurte 1020 weit statt 7,5. Im Spiel
        // denkt ein NPC jedes Bild (NPC_Think: nextthink = level.time +
        // FRAMETIME/2, NPC.cpp:2213 MD / 2369 OpenJK; ucmd.serverTime =
        // level.time - 50, NPC.cpp:2147 MD / 2278 OpenJK, ohne Verhalten
        // last_ucmd.serverTime ebenso, 2514 OpenJK), pml.msec ist nie mehr als
        // 200 (bg_pmove.cpp:33781-33789 MD / 14756-14761 OpenJK).
        int bildMs = 50;
        if (s.k < allein.takte.size() && std::fabs(allein.takte[s.k].t - t) < 0.5) { bildMs = allein.takte[s.k].msec; }
        // rc603 fix603: schon der Vorlauf von NPC_Begin (der Takt bei abMs)
        // beruehrt einen anderen - ihn rechnet der Rechner wie planeLauf
        // (G_CheckInSolid, Pmove von 200 ms, das erste NPC_Think 150 ms spaeter
        // mit 200 ms), mit den Kaesten der anderen.
        const bool beimSpawn = !s.abgewichen && s.k == 0 && a.werte.spawnVorlauf && !a.werte.ohneKoerper &&
                               !allein.takte.empty() && std::fabs(allein.takte[0].t - a.abMs) < 0.5 &&
                               std::fabs(a.abMs - t) < 0.5;
        if (beimSpawn) {
            s.bau = std::make_shared<LaufPlan>();
            s.bau->abMs = allein.abMs;
            s.bau->anfang = allein.anfang;
            s.bau->hoeheGilt = allein.hoeheGilt;
            s.abgewichen = true;
            s.rechner = std::make_unique<LaufRechner>(kw, allein.anfang, a.werte, a.abMs, a.grenzeMs);
            s.rechner->ohneNie = true;
            for (std::size_t i = 0; i < s.rechner->plan.takte.size(); ++i) {
                s.bau->takte.push_back(s.rechner->plan.takte[i]);
                s.bau->zustaende.push_back(s.rechner->plan.zustaende[i]);
            }
            if (!s.rechner->plan.zustaende.empty()) {
                s.z = s.rechner->plan.zustaende.back();
                s.zT = s.rechner->plan.takte.back().t;
                s.hatZustand = true;
            }
            s.art = Art::Rechnet;
            return;
        }
        if (!s.abgewichen) {
            s.bau = std::make_shared<LaufPlan>();
            const LaufPlan& al = *a.allein;
            s.bau->abMs = al.abMs;
            s.bau->anfang = al.anfang;
            s.bau->hoeheGilt = al.hoeheGilt;
            for (std::size_t i = 0; i < s.k && i < al.takte.size(); ++i) {
                s.bau->takte.push_back(al.takte[i]);
                s.bau->zustaende.push_back(al.zustaende[i]);
            }
            if (al.ankunftMs >= 0.0 && al.ankunftMs < t - 0.5) {
                s.bau->ankunftMs = al.ankunftMs;
                s.bau->zielAnfangsErreicht = al.zielAnfangsErreicht;
            }
            s.abgewichen = true;
        }
        LaufWerte w = a.werte;
        w.spawnVorlauf = false;
        const bool angekommen = s.bau->ankunftMs >= 0.0;
        if (angekommen) { w.zielGesetzt = false; }
        const double vorher = s.hatZustand ? s.zT : a.abMs;
        s.rechner = std::make_unique<LaufRechner>(kw, s.hatZustand ? s.z : s.bau->anfang, w, vorher, a.grenzeMs);
        s.rechner->ohneNie = true;
        s.rechner->t = t;
        s.rechner->ersteMs = bildMs;
        s.rechner->ersterZielTakt = !(vorher > a.werte.zielAbMs + 0.5) && !angekommen;
        s.rechner->fertig = !(t <= a.grenzeMs + 0.5);
        s.art = Art::Rechnet;
    };
    const auto abschliessen = [&](Stand& s) {
        if (s.aktuell < 0) { return; }
        if (s.abgewichen && s.bau) {
            aus.plaene[static_cast<std::size_t>(s.aktuell)] = s.bau;
            ++aus.geaendert;
        }
        s.rechner.reset();
        s.bau.reset();
        s.abgewichen = false;
    };

    double t = kNavDenkMs * std::floor((tAb + 0.5) / kNavDenkMs);
    int ruhig = 0;
    for (int schritt = 0; schritt < 200000; ++schritt, t += kNavDenkMs) {
        bool tutWas = false;
        for (const std::size_t f : reihe) {
            Stand& s = st[f];
            // Ein neuer Abschnitt beginnt mit dem Denkbild nach seinem abMs.
            // rc603 fix603: einer mit dem Vorlauf von NPC_Begin schon bei abMs -
            // sein erster Takt (G_CheckInSolid, Pmove von 200 ms) liegt dort und
            // wird wie jeder andere gegen die Kaesten geprueft. Bis fix603 ging
            // er ungeprueft durch, und der Stand blieb der vor dem Spawn: wich
            // die Figur beim ersten NPC_Think ab, begann sie wieder oben.
            const auto beginnt = [&](const NavAbschnitt& a) {
                if (a.abMs < t - 0.5) { return true; }
                return a.werte.spawnVorlauf && !a.werte.ohneKoerper && a.allein && !a.allein->takte.empty() &&
                       std::fabs(a.allein->takte.front().t - a.abMs) < 0.5 && std::fabs(a.abMs - t) < 0.5;
            };
            while (s.naechster < s.abs.size() && beginnt(abschnitte[s.abs[s.naechster]])) {
                const std::size_t ai = s.abs[s.naechster];
                const NavAbschnitt& a = abschnitte[ai];
                LaufZustand beginn = a.start;
                bool folgt = true;
                if (s.aktuell >= 0 && !a.sprung && s.hatZustand) {
                    const NavAbschnitt& vor = abschnitte[static_cast<std::size_t>(s.aktuell)];
                    const LaufZustand alleinBei = zustandBei(*vor.allein, a.abMs);
                    LaufZustand jetzt = s.z;
                    if (!s.abgewichen) { jetzt = alleinBei; }
                    beginn = abschnittsBeginn(a.start, alleinBei, jetzt);
                    folgt = !s.abgewichen;
                }
                abschliessen(s);
                s.aktuell = static_cast<int>(ai);
                s.k = 0;
                ++s.naechster;
                s.art = Art::Folgt;
                s.z = beginn;
                s.zT = a.abMs;
                s.hatZustand = true;
                if (!folgt) {
                    // Schon abgewichen: dieser Abschnitt beginnt anders als der
                    // Plan ohne die anderen - gleich selbst rechnen.
                    s.bau = std::make_shared<LaufPlan>();
                    s.bau->abMs = a.abMs;
                    s.bau->anfang = beginn;
                    s.bau->hoeheGilt = a.allein->hoeheGilt;
                    s.abgewichen = true;
                    s.z = beginn;
                    s.zT = a.abMs;
                    s.hatZustand = true;
                    LaufWerte w = a.werte;
                    s.rechner = std::make_unique<LaufRechner>(kw, beginn, w, a.abMs, a.grenzeMs);
                    s.rechner->ohneNie = true;
                    s.art = Art::Rechnet;
                }
            }
            if (s.aktuell < 0 || s.art == Art::Fertig || s.art == Art::Keiner) { continue; }
            const NavAbschnitt& a = abschnitte[static_cast<std::size_t>(s.aktuell)];
            if (t > a.schnittMs + 0.5) { continue; }   // der naechste Abschnitt uebernimmt
            andere(f, t);
            if (s.art == Art::Folgt) {
                const LaufPlan& al = *a.allein;
                // rc603 fix603: Takte zwischen den Denkbildern des gemeinsamen
                // Takts (ein Vorlauf ab einer Zeit ausserhalb des 50-ms-Rasters)
                // gelten wie gerechnet - der Stand ist danach ihrer, nicht der
                // von vorher.
                while (s.k < al.takte.size() && al.takte[s.k].t < t - 0.5) {
                    s.z = al.zustaende[s.k];
                    s.zT = al.takte[s.k].t;
                    s.hatZustand = true;
                    ++s.k;
                }
                if (s.k < al.takte.size()) {
                    if (std::fabs(al.takte[s.k].t - t) > 0.5) { tutWas = true; continue; }   // Luecke im Plan (Vorlauf)
                    if (frei(al.takte[s.k])) {
                        s.z = al.zustaende[s.k];
                        s.zT = t;
                        s.hatZustand = true;
                        ++s.k;
                        tutWas = true;
                        continue;
                    }
                    abweichen(s, t);
                } else {
                    // Der Plan ist zu Ende. In Ruhe ohne Ziel aendert sich nichts
                    // mehr; steht sie mit Ziel fest (nie), nur, wenn jemand die
                    // Huelle ihres letzten Denkbilds beruehrt.
                    if (al.takte.empty() || al.ruheMs >= 0.0 || (!al.nie && al.ankunftMs >= 0.0)) {
                        s.art = Art::Fertig;
                        continue;
                    }
                    if (frei(al.takte.back())) { continue; }
                    abweichen(s, t);
                }
            }
            if (s.art == Art::Rechnet) {
                LaufRechner& r = *s.rechner;
                if (r.fertig) {
                    s.art = Art::Fertig;
                    if (s.bau) {
                        s.bau->ruheMs = r.plan.ruheMs;
                    }
                    continue;
                }
                if (r.t > t + 0.5) { tutWas = true; continue; }
                const std::size_t vor = r.plan.takte.size();
                r.schritt();
                for (std::size_t i = vor; i < r.plan.takte.size(); ++i) {
                    s.bau->takte.push_back(r.plan.takte[i]);
                    s.bau->zustaende.push_back(r.plan.zustaende[i]);
                }
                if (r.plan.ankunftMs >= 0.0 && s.bau->ankunftMs < 0.0) {
                    s.bau->ankunftMs = r.plan.ankunftMs;
                    if (r.plan.zielAnfangsErreicht) { s.bau->zielAnfangsErreicht = true; }
                }
                if (!r.plan.zustaende.empty()) {
                    s.z = r.plan.zustaende.back();
                    s.zT = r.plan.takte.back().t;
                    s.hatZustand = true;
                }
                if (r.fertig) {
                    s.bau->ruheMs = r.plan.ruheMs;
                    s.art = Art::Fertig;
                }
                tutWas = true;
            }
        }
        bool offen = false;
        for (const Stand& s : st) {
            if (s.naechster < s.abs.size() || s.art == Art::Folgt || s.art == Art::Rechnet) { offen = true; }
        }
        if (!offen) { break; }
        // Nur noch fest stehende Figuren mit Ziel (nie) und keiner bewegt sich:
        // dann aendert sich nichts mehr.
        ruhig = tutWas ? 0 : ruhig + 1;
        bool kommt = false;
        for (const Stand& s : st) { kommt = kommt || s.naechster < s.abs.size(); }
        if (!kommt && ruhig > 2) { break; }
    }
    for (Stand& s : st) { abschliessen(s); }
    return aus;
}

}  // namespace bhed
