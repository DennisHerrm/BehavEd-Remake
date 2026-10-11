// wegpunkte.cpp - Wegnetz lesen, erzeugen, pruefen. Warum so, steht in
// bhed/wegpunkte.h; die Engine-Stellen in code/game/g_navigator.cpp.
#include "bhed/wegpunkte.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace bhed {
namespace {

// g_navigator.cpp, enum am Anfang von NAV
constexpr float kMaxAutoLaenge = 500.0F;    // MAX_EDGE_AUTO_LEN
constexpr float kMaxEtage = 60.0F;          // MAX_EDGE_FLOOR_DIST
constexpr int kNachbarn = 4;                // MIN_WAY_NEIGHBORS
constexpr int kMaxNichtWeg = 1;             // MAX_NONWP_NEIGHBORS

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

int flags(const MapEntity& e) {
    const std::string* f = e.find("spawnflags");
    return f != nullptr ? std::atoi(f->c_str()) : 0;
}

float abstand(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool istWegKlasseKlein(const std::string& k) {
    return k == "waypoint" || k == "waypoint_small" || k.rfind("waypoint_navgoal", 0) == 0 || k == "point_combat";
}

}  // namespace

bool istWegKlasse(const std::string& klasse) { return istWegKlasseKlein(klein(klasse)); }

WegNetz baueWegNetz(const std::vector<MapEntity>& ents, const WegSicht& sicht) {
    WegNetz netz;
    std::map<std::string, std::vector<int>> namen;   // targetname -> Knoten (auch mehrere gleichen Namens)
    for (std::size_t i = 0; i < ents.size(); ++i) {
        const MapEntity& e = ents[i];
        const std::string k = klein(e.classname);
        if (!istWegKlasseKlein(k) || e.origin.empty()) { continue; }
        WegKnoten n;
        n.art = (k == "point_combat") ? WegKnoten::Art::Kampf
                : (k.rfind("waypoint_navgoal", 0) == 0) ? WegKnoten::Art::Ziel
                                                        : WegKnoten::Art::Wegpunkt;
        n.entity = static_cast<int>(i);
        std::istringstream is(e.origin);
        is >> n.ort[0] >> n.ort[1] >> n.ort[2];
        n.name = e.targetname;
        n.keinAuto = (flags(e) & 4) != 0;
        if (!n.name.empty()) { namen[n.name].push_back(static_cast<int>(netz.knoten.size())); }
        netz.knoten.push_back(std::move(n));
    }
    std::set<std::pair<int, int>> da;   // ungerichtet: (min, max)
    const auto verbinde = [&](int a, int b, bool designer, bool sprung) {
        if (a == b) { return; }
        const auto schl = std::make_pair(std::min(a, b), std::max(a, b));
        if (da.count(schl) != 0U) {
            if (sprung) {
                for (WegKante& k : netz.kanten) {
                    if (std::min(k.a, k.b) == schl.first && std::max(k.a, k.b) == schl.second) { k.sprung = true; }
                }
            }
            return;
        }
        da.insert(schl);
        netz.kanten.push_back(WegKante{a, b, designer, sprung});
    };
    // Designerkanten: target, target2, target3, target4, targetJump (das letzte = Sprung)
    static const char* const kZiele[5] = {"target", "target2", "target3", "target4", "targetJump"};
    for (std::size_t i = 0; i < netz.knoten.size(); ++i) {
        const MapEntity& e = ents[static_cast<std::size_t>(netz.knoten[i].entity)];
        for (int z = 0; z < 5; ++z) {
            const std::string* t = e.find(kZiele[z]);
            if (t == nullptr || t->empty()) { continue; }
            const auto it = namen.find(*t);
            if (it == namen.end()) {
                netz.fehlendeZiele.emplace_back(static_cast<int>(i), *t);
                continue;
            }
            for (const int ziel : it->second) { verbinde(static_cast<int>(i), ziel, true, z == 4); }
        }
    }
    // Automatisch: nur Kampf- und Zielpunkte, zu den naechsten vier.
    const std::size_t designerKanten = netz.kanten.size();
    for (std::size_t i = 0; i < netz.knoten.size(); ++i) {
        const WegKnoten& at = netz.knoten[i];
        if (at.keinAuto || at.art == WegKnoten::Art::Wegpunkt) { continue; }
        std::vector<std::pair<float, int>> kandidaten;
        int nichtWeg = 0;
        for (std::size_t j = 0; j < netz.knoten.size(); ++j) {
            if (j == i) { continue; }
            const WegKnoten& n = netz.knoten[j];
            const auto schl = std::make_pair(std::min<int>(static_cast<int>(i), static_cast<int>(j)),
                                             std::max<int>(static_cast<int>(i), static_cast<int>(j)));
            if (da.count(schl) != 0U) { continue; }
            if (std::fabs(n.ort[2] - at.ort[2]) > kMaxEtage) { continue; }
            const float d = abstand(n.ort, at.ort);
            if (d > kMaxAutoLaenge) { continue; }
            if (n.art != WegKnoten::Art::Wegpunkt) {
                ++nichtWeg;
                if (nichtWeg > kMaxNichtWeg || d > kMaxAutoLaenge / 2.0F) { continue; }
            }
            kandidaten.emplace_back(d, static_cast<int>(j));
        }
        std::sort(kandidaten.begin(), kandidaten.end());
        int genommen = 0;
        for (const auto& [d, j] : kandidaten) {
            if (genommen >= kNachbarn) { break; }
            if (sicht && !sicht(at.ort, netz.knoten[static_cast<std::size_t>(j)].ort)) { continue; }
            verbinde(static_cast<int>(i), j, false, false);
            ++genommen;
        }
    }
    (void)designerKanten;
    return netz;
}

// --- Erzeugen -------------------------------------------------------------------

std::vector<NeuerWegpunkt> erzeugeWegpunkte(const std::vector<std::array<float, 3>>& starts, const WegStrahl& strahl,
                                            const WegErzeugung& p) {
    std::vector<NeuerWegpunkt> aus;
    if (!strahl) { return aus; }
    const float S = std::max(32.0F, p.abstand);
    using Zelle = std::tuple<int, int, int>;
    std::map<Zelle, int> zellen;
    std::deque<int> offen;
    const auto zelle = [&](const std::array<float, 3>& b) {
        return Zelle{static_cast<int>(std::lround(b[0] / S)), static_cast<int>(std::lround(b[1] / S)),
                     static_cast<int>(std::lround(b[2] / 96.0F))};
    };
    // Boden unter einem Punkt (bis `tief` hinunter), mit begehbarer Neigung.
    const auto boden = [&](const std::array<float, 3>& von, float tief, std::array<float, 3>& b) {
        std::array<float, 3> bis = von;
        bis[2] -= tief;
        std::array<float, 3> n{};
        if (!strahl(von, bis, b, n)) { return false; }
        return n[2] >= p.steilsterBoden;
    };
    const auto frei = [&](std::array<float, 3> a, std::array<float, 3> b) {
        std::array<float, 3> t{};
        std::array<float, 3> n{};
        return !strahl(a, b, t, n);
    };
    // Kopffreiheit: ein Wegpunktkasten reicht 45 ueber den Ursprung (Boden + 25).
    const auto kopf = [&](const std::array<float, 3>& b) {
        return frei({b[0], b[1], b[2] + 2.0F}, {b[0], b[1], b[2] + 70.0F});
    };
    // Platz fuer den Kasten (-20..20 waagerecht, Unterkante 1 ueber dem Boden):
    // sonst verwirft ihn das Spiel als "in solid" - neben einer Wand ebenso wie
    // neben einer Stufe.
    const auto platz = [&](const std::array<float, 3>& b) {
        for (const float h : {4.0F, 35.0F, 66.0F}) {
            const std::array<float, 3> m{b[0], b[1], b[2] + h};
            for (int r = 0; r < 4; ++r) {
                const float dx = (r == 0) ? 21.0F : (r == 1) ? -21.0F : 0.0F;
                const float dy = (r == 2) ? 21.0F : (r == 3) ? -21.0F : 0.0F;
                if (!frei(m, {m[0] + dx, m[1] + dy, m[2]})) { return false; }
            }
        }
        return true;
    };
    const auto neu = [&](const std::array<float, 3>& b) {
        NeuerWegpunkt w;
        w.boden = b;
        w.ort = {b[0], b[1], b[2] + 25.0F};
        aus.push_back(w);
        const int idx = static_cast<int>(aus.size()) - 1;
        zellen[zelle(b)] = idx;
        offen.push_back(idx);
        return idx;
    };
    const auto verbinde = [&](int a, int b) {
        if (a == b) { return; }
        auto& na = aus[static_cast<std::size_t>(a)].nachbarn;
        if (std::find(na.begin(), na.end(), b) != na.end()) { return; }
        na.push_back(b);
        aus[static_cast<std::size_t>(b)].nachbarn.push_back(a);
    };
    for (const auto& s : starts) {
        std::array<float, 3> b{};
        if (!boden({s[0], s[1], s[2] + 16.0F}, 512.0F, b) || !kopf(b) || !platz(b)) { continue; }
        if (zellen.count(zelle(b)) != 0U) { continue; }
        // Auf das Raster: so liegen Netze verschiedener Startpunkte deckungsgleich.
        neu(b);
        if (static_cast<int>(aus.size()) >= p.maxKnoten) { break; }
    }
    static const int kRichtung[8][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
    while (!offen.empty() && static_cast<int>(aus.size()) < p.maxKnoten) {
        const int i = offen.front();
        offen.pop_front();
        const std::array<float, 3> b = aus[static_cast<std::size_t>(i)].boden;
        for (const auto& r : kRichtung) {
            const float schritt = (r[0] != 0 && r[1] != 0) ? S : S;   // diagonal: dieselbe Rasterweite je Achse
            const std::array<float, 3> ziel{b[0] + static_cast<float>(r[0]) * schritt, b[1] + static_cast<float>(r[1]) * schritt,
                                            b[2]};
            // Wand? Auf Knie- und Brusthoehe frei (die Stufe darunter darf es geben).
            if (!frei({b[0], b[1], b[2] + p.stufe + 4.0F}, {ziel[0], ziel[1], b[2] + p.stufe + 4.0F}) ||
                !frei({b[0], b[1], b[2] + 56.0F}, {ziel[0], ziel[1], b[2] + 56.0F})) {
                continue;
            }
            std::array<float, 3> zb{};
            if (!boden({ziel[0], ziel[1], b[2] + p.stufe + 2.0F}, p.stufe * 2.0F + 2.0F, zb)) { continue; }
            if (std::fabs(zb[2] - b[2]) > p.stufe) { continue; }
            if (!kopf(zb) || !platz(zb)) { continue; }
            // Unterwegs kein Loch: der Boden in der Mitte darf nicht tiefer als eine Stufe liegen.
            std::array<float, 3> mb{};
            const std::array<float, 3> mitte{(b[0] + zb[0]) * 0.5F, (b[1] + zb[1]) * 0.5F,
                                             std::max(b[2], zb[2]) + p.stufe * 0.5F};
            if (!boden(mitte, p.stufe * 2.0F, mb) || std::min(b[2], zb[2]) - mb[2] > p.stufe) { continue; }
            const auto it = zellen.find(zelle(zb));
            if (it != zellen.end()) {
                verbinde(i, it->second);
                continue;
            }
            if (static_cast<int>(aus.size()) >= p.maxKnoten) { break; }
            const int j = neu(zb);
            verbinde(i, j);
        }
    }
    return aus;
}

std::vector<std::vector<int>> verteileZiele(const std::vector<NeuerWegpunkt>& wp, int felder) {
    std::vector<std::vector<int>> ziele(wp.size());
    // Gerade Kanten zuerst, damit bei vollen Feldern eher Diagonalen wegfallen.
    std::vector<std::tuple<int, int, int>> kanten;   // (diagonal, a, b)
    for (std::size_t a = 0; a < wp.size(); ++a) {
        for (const int b : wp[a].nachbarn) {
            if (static_cast<int>(a) < b) {
                const auto& pa = wp[a].boden;
                const auto& pb = wp[static_cast<std::size_t>(b)].boden;
                const bool diag = std::fabs(pa[0] - pb[0]) > 1.0F && std::fabs(pa[1] - pb[1]) > 1.0F;
                kanten.emplace_back(diag ? 1 : 0, static_cast<int>(a), b);
            }
        }
    }
    std::stable_sort(kanten.begin(), kanten.end(),
                     [](const auto& x, const auto& y) { return std::get<0>(x) < std::get<0>(y); });
    for (const auto& [diag, a, b] : kanten) {
        (void)diag;
        auto& za = ziele[static_cast<std::size_t>(a)];
        auto& zb = ziele[static_cast<std::size_t>(b)];
        if (static_cast<int>(za.size()) >= felder && static_cast<int>(zb.size()) >= felder) { continue; }
        if (za.size() <= zb.size() && static_cast<int>(za.size()) < felder) {
            za.push_back(b);
        } else if (static_cast<int>(zb.size()) < felder) {
            zb.push_back(a);
        } else {
            za.push_back(b);
        }
    }
    return ziele;
}

// --- Pruefen -------------------------------------------------------------------

std::vector<WegBefund> pruefeWegNetz(const WegNetz& netz, const std::vector<MapEntity>& ents, const WegStrahl& strahl) {
    std::vector<WegBefund> aus;
    std::vector<int> grad(netz.knoten.size(), 0);
    for (const WegKante& k : netz.kanten) {
        ++grad[static_cast<std::size_t>(k.a)];
        ++grad[static_cast<std::size_t>(k.b)];
    }
    char z[200];
    for (std::size_t i = 0; i < netz.knoten.size(); ++i) {
        const WegKnoten& n = netz.knoten[i];
        const MapEntity& e = ents[static_cast<std::size_t>(n.entity)];
        const std::string nm = n.name.empty() ? std::string("(") + e.classname + ")" : n.name;
        if (grad[i] == 0 && n.art == WegKnoten::Art::Wegpunkt) {
            std::snprintf(z, sizeof(z), "%s: not connected to any other point", nm.c_str());
            aus.push_back(WegBefund{WegBefund::Art::Allein, static_cast<int>(i), z});
        }
        if (strahl && n.art != WegKnoten::Art::Kampf) {
            const int f = flags(e);
            std::array<float, 3> t{};
            std::array<float, 3> nn{};
            // Im Festen? Der Kasten reicht von -24 bis +45 (Kampfpunkte und
            // SOLID_OK prueft die Engine nicht).
            if ((f & 1) == 0) {
                const std::array<float, 3> unten{n.ort[0], n.ort[1], n.ort[2] - 22.0F};
                const std::array<float, 3> oben{n.ort[0], n.ort[1], n.ort[2] + 44.0F};
                bool fest = strahl(unten, oben, t, nn) || strahl(oben, unten, t, nn);
                // Ganz im Festen trifft eine kurze Strecke nichts. Ein langer
                // Strahl trifft dann eine Flaeche von INNEN: nach oben ihre
                // Oberseite (Normale nach oben), nach unten ihre Unterseite.
                if (!fest && strahl(n.ort, {n.ort[0], n.ort[1], n.ort[2] + 8192.0F}, t, nn) && nn[2] > 0.5F) {
                    fest = true;
                }
                if (!fest && strahl(n.ort, {n.ort[0], n.ort[1], n.ort[2] - 8192.0F}, t, nn) && nn[2] < -0.5F) {
                    fest = true;
                }
                for (int r = 0; r < 4 && !fest; ++r) {
                    const float dx = (r == 0) ? 19.0F : (r == 1) ? -19.0F : 0.0F;
                    const float dy = (r == 2) ? 19.0F : (r == 3) ? -19.0F : 0.0F;
                    fest = strahl(n.ort, {n.ort[0] + dx, n.ort[1] + dy, n.ort[2]}, t, nn);
                }
                if (fest) {
                    std::snprintf(z, sizeof(z), "%s: in solid - the game removes it (\"Waypoint in solid\")", nm.c_str());
                    aus.push_back(WegBefund{WegBefund::Art::ImFesten, static_cast<int>(i), z});
                }
            }
            // Schwebend? Die Engine sucht den Boden bis 90 darunter (1,5 x 60).
            if ((f & 2) == 0) {
                const std::array<float, 3> tief{n.ort[0], n.ort[1], n.ort[2] - kMaxEtage * 1.5F};
                if (!strahl(n.ort, tief, t, nn)) {
                    std::snprintf(z, sizeof(z), "%s: floating - no floor within 90 units below", nm.c_str());
                    aus.push_back(WegBefund{WegBefund::Art::Schwebend, static_cast<int>(i), z});
                }
            }
        }
    }
    for (const auto& [k, name] : netz.fehlendeZiele) {
        const WegKnoten& n = netz.knoten[static_cast<std::size_t>(k)];
        std::snprintf(z, sizeof(z), "%s: target \"%s\" does not exist", n.name.empty() ? "?" : n.name.c_str(),
                      name.c_str());
        aus.push_back(WegBefund{WegBefund::Art::ZielFehlt, k, z});
    }
    return aus;
}

}  // namespace bhed
