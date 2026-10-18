// navweg.cpp - Paket bewegung (rc601): Laufziele ohne Weg im Wegnetz.
//
// Gemeldet: "NPCs don't walk to waypoints correctly." Siehe bhed/scene.h,
// navZieleOhneWeg: behaved laeuft immer gerade aufs Ziel, im Spiel geht ein
// NPC ab 400 Einheiten (ohne benachbarte Knoten) ueber das Wegnetz - und hat
// er dort keinen Weg, bleibt er stehen. Hier wird das nur ERKANNT; die Wege
// ueber das Netz selbst fehlen noch (NOTIZEN_bewegung.md).
//
// Quellen (Movie Duels code/game/g_navigator.cpp, gleich OpenJK bis auf die
// Zeilen): STEER::SafeToGoTo :4337, NAV::GoTo(actor, target) :981-1068,
// NAV::GetNearestNode :2345, VIEW_RANGE = 550 :127. NPC_MoveToGoal: OpenJK
// code/game/NPC_move.cpp:762-822.
#include "bhed/scene.h"
#include "bhed/wegpunkte.h"

#include <cmath>
#include <numeric>
#include <vector>

namespace bhed {
namespace {

constexpr float kSichtweite = 550.0F;   // VIEW_RANGE (g_navigator.cpp:127)

// Der naechste Knoten wie NAV::GetNearestNode (g_navigator.cpp:2345 ff.):
//   * ohne eine einzige Kante gibt es keinen ("if (mGraph.size_edges() > 0)",
//     sonst WAYPOINT_NONE);
//   * mehr als VIEW_RANGE/4 hoeher oder tiefer zaehlt nicht, ueber 30 wird
//     der Hoehenunterschied quadratisch aufgeschlagen;
//   * weiter als VIEW_RANGE zaehlt nicht;
//   * ein Knoten ohne Kante (WN_ISLAND) zaehlt dreifach.
// Was fehlt: die Sicht- und Gehproben danach (ViewTrace/MoveTrace) und die
// Zellen der Karte (mCells) - hier zaehlt jeder Knoten in Reichweite.
int naechsterKnoten(const WegNetz& netz, const std::vector<int>& grad, const std::array<float, 3>& p) {
    if (netz.kanten.empty()) {
        return -1;
    }
    int best = -1;
    float bestD = 0.0F;
    for (std::size_t i = 0; i < netz.knoten.size(); ++i) {
        const std::array<float, 3>& o = netz.knoten[i].ort;
        const float dx = o[0] - p[0];
        const float dy = o[1] - p[1];
        const float dz = std::fabs(o[2] - p[2]);
        float d = dx * dx + dy * dy + dz * dz;
        if (dz > kSichtweite / 4.0F) {
            continue;
        }
        if (dz > 30.0F) {
            d += dz * dz;
        }
        if (d > kSichtweite * kSichtweite) {
            continue;
        }
        if (grad[i] == 0) {
            d *= 3.0F;
        }
        if (best < 0 || d < bestD) {
            best = static_cast<int>(i);
            bestD = d;
        }
    }
    return best;
}

int wurzel(std::vector<int>& eltern, int i) {
    while (eltern[static_cast<std::size_t>(i)] != i) {
        eltern[static_cast<std::size_t>(i)] = eltern[static_cast<std::size_t>(eltern[static_cast<std::size_t>(i)])];
        i = eltern[static_cast<std::size_t>(i)];
    }
    return i;
}

// Die Gebiete des Netzes: wer ueber Kanten zusammenhaengt, hat einen Weg
// (A* findet ihn, NAV::FindPath). Die Kanten gelten in beide Richtungen
// (die Engine verbindet so, wegpunkte.h); Sprungkanten zaehlen mit.
struct Gebiete {
    std::vector<int> eltern;
    std::vector<int> grad;
};

Gebiete gebiete(const WegNetz& netz) {
    Gebiete g;
    g.eltern.resize(netz.knoten.size());
    std::iota(g.eltern.begin(), g.eltern.end(), 0);
    g.grad.assign(netz.knoten.size(), 0);
    for (const WegKante& k : netz.kanten) {
        if (k.a < 0 || k.b < 0 || static_cast<std::size_t>(k.a) >= netz.knoten.size() ||
            static_cast<std::size_t>(k.b) >= netz.knoten.size()) {
            continue;
        }
        ++g.grad[static_cast<std::size_t>(k.a)];
        ++g.grad[static_cast<std::size_t>(k.b)];
        const int ra = wurzel(g.eltern, k.a);
        const int rb = wurzel(g.eltern, k.b);
        if (ra != rb) {
            g.eltern[static_cast<std::size_t>(ra)] = rb;
        }
    }
    return g;
}

bool wegIn(const WegNetz& netz, Gebiete& g, const std::array<float, 3>& von, const std::array<float, 3>& ziel,
           int* zielKnoten) {
    // NAV::GoTo(actor, target): der Knoten des Ziels ist GetNearestNode des
    // Ziels - ein waypoint_navgoal steht selbst als Knoten dort.
    const int z = naechsterKnoten(netz, g.grad, ziel);
    if (zielKnoten != nullptr) {
        *zielKnoten = z;
    }
    const int a = naechsterKnoten(netz, g.grad, von);
    if (a < 0 || z < 0) {
        return false;   // "No Waypoint Near" -> STEER::Blocked
    }
    return wurzel(g.eltern, a) == wurzel(g.eltern, z);
}

}  // namespace

bool wegImNetz(const WegNetz& netz, const std::array<float, 3>& von, const std::array<float, 3>& ziel, int* zielKnoten) {
    Gebiete g = gebiete(netz);
    return wegIn(netz, g, von, ziel, zielKnoten);
}

std::vector<NavOhneWeg> navZieleOhneWeg(const Scene& s, const WegNetz& netz) {
    // rc601 (bewegung, Wege wie im Spiel): nicht mehr geschaetzt. Die Szene
    // hat jede Figur Denkbild fuer Denkbild bewegt (planeLauf, bhed/navlauf.h)
    // und dabei gesehen, dass sie mit Ziel und Tempo stehen bleibt - STEER
    // darf nicht gerade hin, NAV::GoTo findet keinen Weg, also STEER::Stop in
    // jedem Denkbild (Actor::stehtOhneWeg). Im Spiel bleibt sie genauso
    // stehen, und ein dowait auf den Lauf endet nie.
    std::vector<NavOhneWeg> aus;
    for (std::size_t i = 0; i < s.actors.size(); ++i) {
        for (const Actor::StehtOhneWeg& h : s.actors[i].stehtOhneWeg) {
            NavOhneWeg w;
            w.figur = i;
            w.ms = h.ms;
            w.abstand = h.abstand;
            w.von = h.von;
            w.ziel = h.ziel;
            for (std::size_t k = 0; k < netz.knoten.size(); ++k) {
                const std::array<float, 3>& o = netz.knoten[k].ort;
                if (std::fabs(o[0] - h.ziel[0]) < 1.0F && std::fabs(o[1] - h.ziel[1]) < 1.0F &&
                    std::fabs(o[2] - h.ziel[2]) < 1.0F) {
                    w.zielKnoten = static_cast<int>(k);
                    break;
                }
            }
            aus.push_back(w);
        }
    }
    return aus;
}

}  // namespace bhed
