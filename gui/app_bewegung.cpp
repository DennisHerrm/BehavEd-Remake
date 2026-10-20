// app_bewegung.cpp - Paket bewegung (rc601): die Schwerkraft in der Vorschau.
//
// Gemeldet: "NPCs aren't affected by gravity."
//
// Gerechnet wird in src/schwerkraft.cpp (schwerkraftEinrechnen). Hier kommt
// der Boden dazu, und zwar so, wie die Figur ihn im Spiel spuert:
//
//   * die Welt der Karte - als Raster gegen das UNTERTEILTE Netz (die
//     Patches so, wie man sie sieht; traceRay nimmt nur ihr Kontrollgitter)
//     und nur mit festen Shadern (MASK_NPCSOLID): Wasser, Nebel und
//     nonsolid traegt nicht;
//   * die Brush-Entities (func_*) dort, wo sie zu dieser Zeit stehen - genau
//     wie der Zeichner sie stellt (app_view3d.cpp, "Wo steht welches
//     Brush-Modell?"): ein Brush-Modell der Szene am AKTUELLEN Ursprung mit
//     allen drei Winkeln und nur, solange es sichtbar ist; Tueren & Co. aus
//     der Mover-Simulation; alle anderen an ihrem "origin". Nicht "Ort minus
//     Startort" - die Geometrie liegt relativ zum Ursprung.
//
// Was fehlt (und im Spiel traegt): unsichtbare Clip-Brushes (playerclip/
// monsterclip ohne Flaeche) - behaved liest die Brushes nicht als Koerper.
// Modelle (misc_model) tragen hier ueber ihre Dreiecke, im Spiel nur ueber
// ihre Clip-Brushes.
#include "app_internal.h"

#include "bhed/bspgeo.h"
#include "bhed/gegenstaende.h"   // rc604: spawntBeiSchwierigkeit
#include "bhed/gpuskin.h"
#include "bhed/kollision.h"
#include "bhed/navlauf.h"
#include "bhed/mover.h"
#include "bhed/scene.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace bhed::gui {
// rc604 fix604: Raster, Matrix- und Stellungsspeicher der Schwerkraft mit
// Movern (aus: der alte Weg, fuer den Vergleich im Selbsttest).
std::atomic<bool> gSchwerkraftSchnell{true};
namespace {

// Ein Brush-Modell, das tragen kann: sein Netz im eigenen Raum.
struct BodenModell {
    int modell = 0;
    std::size_t entity = 0;
    BspMesh netz;
    float radius = 0.0F;   // um den eigenen Nullpunkt, fuer Drehungen
    NetzRaster raster;     // rc604 fix604: senkrechte Strahlen (netzTrefferRaster)
};

// Was nur von der Geometrie der Karte abhaengt - einmal je Karte gebaut.
struct BodenWelt {
    const BspGeometry* geo = nullptr;
    std::string karte;
    std::size_t flaechen = 0;
    std::size_t modelle = 0;
    std::vector<bool> fest;
    BodenRaster raster;
    std::vector<std::unique_ptr<BodenModell>> netze;   // nach Modellnummer, leer = traegt nicht
};

BodenWelt& bodenWelt() {
    static BodenWelt w;
    const BspGeometry& geo = g_app->geo;
    if (w.geo == &geo && w.karte == g_app->map.path && w.flaechen == geo.surfaces.size() &&
        w.modelle == geo.models.size()) {
        return w;
    }
    const auto t0 = std::chrono::steady_clock::now();
    w = BodenWelt{};
    w.geo = &geo;
    w.karte = g_app->map.path;
    w.flaechen = geo.surfaces.size();
    w.modelle = geo.models.size();
    if (geo.empty()) {
        return w;
    }
    w.fest = festeShader(geo);
    // Stufe 8: feiner als die Vorgabe der Ansicht, die Patches liegen so auf
    // Bruchteile einer Einheit an ihrer Kurve.
    constexpr int kDetail = 8;
    w.raster = baueBodenRaster(buildModelMesh(geo, 0, kDetail, true), w.fest);
    w.netze.resize(geo.models.size());
    std::size_t brushes = 0;
    for (std::size_t i = 0; i < g_app->map.entities.size(); ++i) {
        const MapEntity& e = g_app->map.entities[i];
        const std::string* mdl = e.find("model");
        if (mdl == nullptr || mdl->size() < 2 || (*mdl)[0] != '*') {
            continue;
        }
        // Nur, was im Spiel fest ist: Tueren, Plattformen, Zuege, Waende,
        // statische und zerbrechliche Teile (func_*). Ausloeser (trigger_*)
        // und Hilfsgebilde tragen niemanden.
        const std::string& k = e.classname;
        if (k.rfind("func_", 0) != 0 || k == "func_group") {
            continue;
        }
        // rc604: notsingle/Schwierigkeit - die Entity gibt es im Spiel nicht
        // (G_SpawnGEntityFromSpawnVars, code/game/g_spawn.cpp:1096-1101 OpenJK).
        if (!spawntBeiSchwierigkeit(e, regelwerkSpielstufe())) {
            continue;
        }
        const int nr = std::atoi(mdl->c_str() + 1);
        if (nr <= 0 || static_cast<std::size_t>(nr) >= geo.models.size() || w.netze[static_cast<std::size_t>(nr)]) {
            continue;
        }
        auto bm = std::make_unique<BodenModell>();
        bm->modell = nr;
        bm->entity = i;
        bm->netz = buildModelMesh(geo, nr, kDetail, true);
        if (bm->netz.indexes.empty()) {
            continue;
        }
        float r2 = 0.0F;
        for (const BspVertex& v : bm->netz.verts) {
            r2 = std::max(r2, v.xyz[0] * v.xyz[0] + v.xyz[1] * v.xyz[1] + v.xyz[2] * v.xyz[2]);
        }
        bm->radius = std::sqrt(r2);
        // rc604 fix604: ein Strahl, den modellTreffer fragt, liegt hoechstens
        // radius + 1 vom Nullpunkt (im Raum des Modells, ungekippt) - etwas Luft.
        bm->raster = baueNetzRaster(bm->netz, w.fest, 32.0F, bm->radius + 4.0F);
        w.netze[static_cast<std::size_t>(nr)] = std::move(bm);
        ++brushes;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    diag::detail("Schwerkraft: Boden der Karte - " + std::to_string(w.raster.dreiecke.size()) + " Dreiecke im Raster (" +
                 std::to_string(w.raster.nx) + "x" + std::to_string(w.raster.ny) + "), " + std::to_string(brushes) +
                 " Brush-Modelle, " + std::to_string(static_cast<int>(ms)) + " ms");
    return w;
}

// Die Stellung eines Brush-Modells zu einer Zeit.
struct Stellung {
    bool da = false;
    float ort[3]{};
    float winkel[3]{};
};

// Alles fuer einen Durchgang: wo jedes tragende Brush-Modell zu jedem
// Takt steht, und wo und wann sich etwas bewegt.
struct BodenLage {
    double endeMs = 0.0;
    bool mitMovern = false;
    // Je Modellnummer: der Szenen-Schauspieler (Index) oder -1.
    std::vector<int> szenenFigur;
    // Je Modellnummer und Takt: die Stellung (nur fuer Brush-Modelle der
    // Szene - Actor::at ist zu teuer, um es je Strahl zu fragen).
    std::vector<std::vector<Stellung>> takte;
    // Je Modellnummer: wohin es in X/Y ueberhaupt kommt (lo x, lo y, hi x,
    // hi y) - ein Strahl daneben fragt es gar nicht erst.
    std::vector<std::array<float, 4>> huelle;
    // Wo und wann sich ein Boden bewegt (beweglich).
    struct Bereich {
        float lo[2];
        float hi[2];
        double von;
        double bis;
    };
    std::vector<Bereich> bewegt;
};

Stellung stellungBei(const BodenLage& lage, const BodenWelt& w, int nr, double ms) {
    Stellung s;
    const auto n = static_cast<std::size_t>(nr);
    const int fi = n < lage.szenenFigur.size() ? lage.szenenFigur[n] : -1;
    if (fi >= 0) {
        const std::vector<Stellung>& t = lage.takte[n];
        if (t.empty()) {
            return s;
        }
        const std::size_t k = std::min(static_cast<std::size_t>(std::max(0.0, ms) / kPmoveTaktMs + 0.5), t.size() - 1U);
        return t[k];
    }
    if (lage.mitMovern) {
        const MoverSim::Stand st = g_app->moverSim.at(nr, ms);
        if (st.bekannt) {
            s.da = st.sichtbar;
            for (int k = 0; k < 3; ++k) {
                s.ort[k] = st.origin[k];
                s.winkel[k] = st.angles[k];
            }
            return s;
        }
    }
    // Ohne Skript und Simulation: am "origin" der Entity (q3map2 zieht ihn
    // von der Geometrie ab, die Engine addiert ihn beim Zeichnen).
    const BodenModell& bm = *w.netze[n];
    if (bm.entity < g_app->map.entities.size()) {
        const MapEntity& e = g_app->map.entities[bm.entity];
        if (!e.origin.empty()) {
            std::istringstream is(e.origin);
            is >> s.ort[0] >> s.ort[1] >> s.ort[2];
        }
    }
    s.da = true;
    return s;
}

BodenLage bodenLage(const BodenWelt& w, double endeMs, bool mitMovern) {
    BodenLage lage;
    lage.endeMs = endeMs;
    lage.mitMovern = mitMovern;
    lage.szenenFigur.assign(w.netze.size(), -1);
    lage.takte.resize(w.netze.size());
    lage.huelle.assign(w.netze.size(), {1.0e30F, 1.0e30F, -1.0e30F, -1.0e30F});
    const std::size_t n = static_cast<std::size_t>(endeMs / kPmoveTaktMs) + 2U;
    const auto huelleDazu = [&lage, &w](std::size_t nr, const Stellung& s) {
        std::array<float, 4>& h = lage.huelle[nr];
        const float r = w.netze[nr]->radius + 1.0F;
        h[0] = std::min(h[0], s.ort[0] - r);
        h[1] = std::min(h[1], s.ort[1] - r);
        h[2] = std::max(h[2], s.ort[0] + r);
        h[3] = std::max(h[3], s.ort[1] + r);
    };
    const auto bereichVon = [&](const BodenModell& bm, const Stellung& s, float lo[2], float hi[2]) {
        for (int k = 0; k < 2; ++k) {
            lo[k] = std::min(lo[k], s.ort[k] - bm.radius);
            hi[k] = std::max(hi[k], s.ort[k] + bm.radius);
        }
    };
    for (std::size_t i = 0; i < g_app->scene.actors.size(); ++i) {
        const Actor& a = g_app->scene.actors[i];
        if (a.brushModel <= 0 || !a.haveStart || static_cast<std::size_t>(a.brushModel) >= w.netze.size() ||
            !w.netze[static_cast<std::size_t>(a.brushModel)]) {
            continue;
        }
        const auto nr = static_cast<std::size_t>(a.brushModel);
        lage.szenenFigur[nr] = static_cast<int>(i);
        std::vector<Stellung>& t = lage.takte[nr];
        t.resize(n);
        float lo[2] = {1.0e30F, 1.0e30F};
        float hi[2] = {-1.0e30F, -1.0e30F};
        double von = -1.0;
        double bis = -1.0;
        for (std::size_t k = 0; k < n; ++k) {
            const ActorState st = a.at(static_cast<double>(k) * kPmoveTaktMs);
            t[k].da = st.visible;
            for (int q = 0; q < 3; ++q) {
                t[k].ort[q] = st.pos[q];
                t[k].winkel[q] = st.angles[q];
            }
            huelleDazu(nr, t[k]);
            if (k > 0) {
                const Stellung& v = t[k - 1];
                bool anders = v.da != t[k].da;
                for (int q = 0; q < 3; ++q) {
                    anders = anders || std::fabs(v.ort[q] - t[k].ort[q]) > 0.01F || std::fabs(v.winkel[q] - t[k].winkel[q]) > 0.01F;
                }
                if (anders) {
                    const double tk = static_cast<double>(k) * kPmoveTaktMs;
                    if (von < 0.0) { von = tk - kPmoveTaktMs; }
                    bis = tk;
                    bereichVon(*w.netze[nr], v, lo, hi);
                    bereichVon(*w.netze[nr], t[k], lo, hi);
                }
            }
        }
        if (von >= 0.0) {
            lage.bewegt.push_back({{lo[0], lo[1]}, {hi[0], hi[1]}, von - kPmoveTaktMs, bis + kPmoveTaktMs});
        }
    }
    // Was die Szene nicht bewegt: Mover (nur im zweiten Durchgang) oder fest
    // an seinem "origin".
    for (std::size_t nr = 1; nr < w.netze.size(); ++nr) {
        if (!w.netze[nr] || lage.szenenFigur[nr] >= 0) {
            continue;
        }
        const int modell = static_cast<int>(nr);
        Stellung v = stellungBei(lage, w, modell, 0.0);
        huelleDazu(nr, v);
        if (!mitMovern || !g_app->moverSim.at(modell, 0.0).bekannt) {
            continue;
        }
        // Abgetastet wie die Szenen-Modelle: wo und wann sich etwas tut.
        float lo[2] = {1.0e30F, 1.0e30F};
        float hi[2] = {-1.0e30F, -1.0e30F};
        double von = -1.0;
        double bis = -1.0;
        for (std::size_t k = 1; k < n; ++k) {
            const double tk = static_cast<double>(k) * kPmoveTaktMs;
            const Stellung s = stellungBei(lage, w, modell, tk);
            bool anders = v.da != s.da;
            for (int q = 0; q < 3; ++q) {
                anders = anders || std::fabs(v.ort[q] - s.ort[q]) > 0.01F || std::fabs(v.winkel[q] - s.winkel[q]) > 0.01F;
            }
            if (anders) {
                if (von < 0.0) { von = tk - kPmoveTaktMs; }
                bis = tk;
                bereichVon(*w.netze[nr], v, lo, hi);
                bereichVon(*w.netze[nr], s, lo, hi);
                huelleDazu(nr, s);
            }
            v = s;
        }
        if (von >= 0.0) {
            lage.bewegt.push_back({{lo[0], lo[1]}, {hi[0], hi[1]}, von - kPmoveTaktMs, bis + kPmoveTaktMs});
        }
    }
    return lage;
}

// Ein senkrechter Strahl gegen ein Brush-Modell in seiner Stellung: den
// Strahl in den Raum des Modells drehen (die Umkehrung von baueMoverWelt),
// dort treffen, zurueckdrehen.
// rc604 fix604: die Matrix einer Stellung (baueMoverWelt) - gleiche Stellung
// (Bit fuer Bit), gleiche Matrix; die fuenf Strahlen einer Frage und ein
// stehender Mover fragen sie sonst immer neu.
struct MatrixMerk {
    bool da = false;
    float ort[3]{};
    float winkel[3]{};
    float m[16]{};
};

bool modellTreffer(const BodenModell& bm, const std::vector<bool>& fest, const Stellung& s,
                   const std::array<float, 3>& von, const std::array<float, 3>& bis, float& z,
                   MatrixMerk* merk = nullptr) {
    const float dx = von[0] - s.ort[0];
    const float dy = von[1] - s.ort[1];
    if (dx * dx + dy * dy > (bm.radius + 1.0F) * (bm.radius + 1.0F)) {
        return false;
    }
    float eigen[16];
    float* m = eigen;
    if (merk != nullptr && merk->da && std::memcmp(merk->ort, s.ort, sizeof(s.ort)) == 0 &&
        std::memcmp(merk->winkel, s.winkel, sizeof(s.winkel)) == 0) {
        m = merk->m;
    } else {
        const bool taumelt = s.winkel[0] != 0.0F || s.winkel[2] != 0.0F;
        gpu::baueMoverWelt(nullptr, s.ort, s.winkel[1], s.winkel[0], s.winkel[2], taumelt, m);
        if (merk != nullptr) {
            merk->da = true;
            std::memcpy(merk->ort, s.ort, sizeof(s.ort));
            std::memcpy(merk->winkel, s.winkel, sizeof(s.winkel));
            std::memcpy(merk->m, m, sizeof(eigen));
            m = merk->m;
        }
    }
    float a[3];
    float b[3];
    for (int c = 0; c < 3; ++c) {
        a[c] = 0.0F;
        b[c] = 0.0F;
        for (int r = 0; r < 3; ++r) {
            a[c] += m[r * 4 + c] * (von[static_cast<std::size_t>(r)] - s.ort[r]);
            b[c] += m[r * 4 + c] * (bis[static_cast<std::size_t>(r)] - s.ort[r]);
        }
    }
    float t[3];
    // rc604 fix604: senkrecht im Raum des Modells (keine Kippung) ueber das
    // Raster, sonst wie bisher - Bit fuer Bit dasselbe (netzTrefferRaster).
    const bool getroffen = (merk != nullptr && gSchwerkraftSchnell.load(std::memory_order_relaxed))
                               ? netzTrefferRaster(bm.netz, bm.raster, fest, a, b, t)
                               : netzTreffer(bm.netz, fest, a, b, t);
    if (!getroffen) {
        return false;
    }
    z = m[8] * t[0] + m[9] * t[1] + m[10] * t[2] + s.ort[2];
    return true;
}

}  // namespace

// Die Schwerkraft der Figuren einrechnen (scene.h, schwerkraftEinrechnen).
// mitMovern: die Mover-Simulation ist schon gebaut und darf als Boden
// dienen (zweiter Durchgang in prepareScene).
void schwerkraftVorschau(bool mitMovern) {
    if (g_app->geo.empty()) {
        for (Actor& a : g_app->scene.actors) { a.hoehe.clear(); }
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const BodenWelt& w = bodenWelt();
    // So weit wie das Laengste von Szene, Kamera und Zeitleiste, und 5 s
    // darueber (wie die Abtastung der Tuerausloeser) - dahinter blieben die
    // Figuren sonst auf ihrer letzten Hoehe stehen, waehrend X/Y weiterlaeuft.
    const double ende = std::max({g_app->scene.durationMs, g_app->camTrack.durationMs, g_app->timeline.practicalEndMs}) + 5000.0;
    auto lage = std::make_shared<BodenLage>(bodenLage(w, ende, mitMovern));
    long long strahlen = 0;
    SchwerkraftHilfe h;
    h.endeMs = ende;
    // rc604 fix604: je Modell die Matrix der letzten Stellung und die Stellung
    // der letzten Zeit (die fuenf Strahlen einer Frage haben dieselbe).
    const bool schnell = gSchwerkraftSchnell.load(std::memory_order_relaxed);
    auto matrizen = std::make_shared<std::vector<MatrixMerk>>(w.netze.size());
    struct StellungMerk {
        double ms = 0.0;
        bool da = false;
        Stellung s;
    };
    auto stellungen = std::make_shared<std::vector<StellungMerk>>(w.netze.size());
    h.strahl = [&w, lage, &strahlen, schnell, matrizen, stellungen](const std::array<float, 3>& von,
                                                                   const std::array<float, 3>& bis, double ms,
                                                                   std::array<float, 3>& treffer) {
        ++strahlen;
        bool gefunden = false;
        float best = bis[2];
        float z = 0.0F;
        if (rasterTreffer(w.raster, von[0], von[1], von[2], bis[2], z)) {
            best = z;
            gefunden = true;
        }
        for (std::size_t nr = 1; nr < w.netze.size(); ++nr) {
            if (!w.netze[nr]) { continue; }
            const std::array<float, 4>& hu = lage->huelle[nr];
            if (von[0] < hu[0] || von[1] < hu[1] || von[0] > hu[2] || von[1] > hu[3]) { continue; }
            Stellung s;
            if (schnell) {
                StellungMerk& sm = (*stellungen)[nr];
                if (!sm.da || std::memcmp(&sm.ms, &ms, sizeof(ms)) != 0) {
                    sm.s = stellungBei(*lage, w, static_cast<int>(nr), ms);
                    sm.ms = ms;
                    sm.da = true;
                }
                s = sm.s;
            } else {
                s = stellungBei(*lage, w, static_cast<int>(nr), ms);
            }
            if (!s.da) { continue; }
            if (modellTreffer(*w.netze[nr], w.fest, s, von, bis, z, schnell ? &(*matrizen)[nr] : nullptr) &&
                z <= von[2] + 0.01F && z >= bis[2] - 0.01F &&
                (!gefunden || z > best)) {
                best = z;
                gefunden = true;
            }
        }
        if (gefunden) {
            treffer = {von[0], von[1], best};
        }
        return gefunden;
    };
    h.beweglich = [lage](float x, float y, double ms) {
        constexpr float kRand = 40.0F;   // halbe Breite einer Figur und etwas Luft
        for (const BodenLage::Bereich& b : lage->bewegt) {
            if (ms >= b.von && ms <= b.bis && x >= b.lo[0] - kRand && x <= b.hi[0] + kRand && y >= b.lo[1] - kRand &&
                y <= b.hi[1] + kRand) {
                return true;
            }
        }
        return false;
    };
    schwerkraftEinrechnen(g_app->scene, h);
    std::size_t figuren = 0;
    for (const Actor& a : g_app->scene.actors) {
        if (!a.hoehe.empty()) { ++figuren; }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    diag::detail("Schwerkraft" + std::string(mitMovern ? " (mit Movern)" : "") + ": " + std::to_string(figuren) +
                 " Figuren bis " + std::to_string(static_cast<int>(ende)) + " ms, " + std::to_string(strahlen) +
                 " Strahlen, " + std::to_string(lage->bewegt.size()) + " bewegte Boeden, " +
                 std::to_string(static_cast<int>(ms)) + " ms");
}

// Lohnt der zweite Durchgang mit der Mover-Simulation? Nur, wenn ein Mover
// mit tragender Flaeche sich bewegt, dreht oder erscheint.
bool schwerkraftMitMovernNoetig() {
    if (g_app->geo.empty()) {
        return false;
    }
    const BodenWelt& w = bodenWelt();
    for (const MoverSim::Mover& m : g_app->moverSim.mover()) {
        if (m.modell <= 0 || static_cast<std::size_t>(m.modell) >= w.netze.size() || !w.netze[static_cast<std::size_t>(m.modell)]) {
            continue;
        }
        const bool dreht = m.drehung[0] != 0.0F || m.drehung[1] != 0.0F || m.drehung[2] != 0.0F;
        if (!m.bahn.empty() || dreht || m.bobPeriodeMs > 0.0 || !m.umschalten.empty() || !m.sichtAb.empty() ||
            !m.drehBahn.empty() || !m.sichtbarAnfang) {
            return true;
        }
    }
    return false;
}


// =====================================================================
// Paket bewegung (rc601, Wege wie im Spiel): die Welt fuer NAV und STEER
// =====================================================================
//
// Der Betreuer: "das mit dem Laufen soll genauso gehen wie ingame bitte".
// planeLauf (bhed/navlauf.h) rechnet jedes Denkbild wie die Engine - und die
// Engine fragt dabei Spuren: STEER::SafeToGoTo eine MoveTrace mit dem Kasten
// der Figur, NAV::GetNearestNode eine ViewNavTrace zu jedem Knoten,
// STEER::AvoidCollisions zwei MoveTraces je Denkbild, beim Bau des Wegnetzes
// TestEdge und waypoint_getRadius. Hier kommen sie aus den BRUSHES der Karte
// (bhed/kollision.h: CM_BoxTrace) samt den festen Brush-Entities an ihrem
// Platz beim Laden (SV_ClipMoveToEntities), gi.inPVS aus der Sichttabelle und
// der Boden fuer die Hoehe aus dem Raster der Schwerkraft.
namespace {

struct KollisionsSpeicher {
    std::string karte;
    std::shared_ptr<const KollisionsKarte> k;
    bool jk2 = false;   // rc604 fix604: mit den Regeln von Jedi Outcast gelesen (SURF_SLICK)
};
KollisionsSpeicher& kollisionsSpeicher() {
    static KollisionsSpeicher s;
    return s;
}

// Ein Brush-Modell der Karte, wie SV_ClipMoveToEntities es sieht.
//
// rc601 (bewegung, teil 3): mit zwei Staenden - zur Laufzeit beim Spawnen
// (InitMover: eine func_door steht auf pos1, bei START_OPEN also OFFEN;
// func_wall/func_usable mit START_OFF ohne contents) und in PHASE 0 des
// Netzbaus (g_navigator.cpp:1482-1533 MD: START_OPEN-Tueren zu auf pos2,
// START_OFF-Waende an). Vorher gab es nur einen Stand (den Ursprung aus der
// Karte), und START_OFF-Waende fehlten ganz.
struct NavBrush {
    int entity = -1;
    int modell = 0;
    float ort[3]{};          // Laufzeit beim Spawnen
    float winkel[3]{};
    float ortPhase0[3]{};    // PHASE 0
    bool festLaufzeit = true;
    bool festPhase0 = true;
    std::uint32_t inhalt = 0;
    float radius = 0.0F;   // um den Ursprung, fuer die grobe Pruefung
};

// gi.trace (SV_Trace, sv_world.cpp): erst die Welt, dann die Entities; die
// naechste Beruehrung gewinnt, startsolid/allsolid sammeln sich. `stellung`
// sagt je Brush-Modell, wo es gerade steht und ob es contents hat.
using NavStellung = std::function<bool(const NavBrush& b, float ort[3], float winkel[3])>;
NavSpur navSpurMit(const KollisionsKarte& k, const std::vector<NavBrush>& brushes, const NavVec& start, const NavVec& mins,
                   const NavVec& maxs, const NavVec& ende, int ignoriere, std::uint32_t maske, const NavStellung& stellung) {
    NavSpur r;
    const KollisionsSpur w = kollisionsSpur(k, start.data(), mins.data(), maxs.data(), ende.data(), maske);
    r.allsolid = w.allsolid;
    r.startsolid = w.startsolid;
    r.fraction = w.fraction;
    r.endpos = w.endpos;
    r.normal = w.normale;
    r.contents = w.inhalt;
    r.entity = w.fraction != 1.0F ? kNavWeltEntity : kNavKeineEntity;
    if (w.fraction == 0.0F) { return r; }
    float lo[3];
    float hi[3];
    for (int a = 0; a < 3; ++a) {
        lo[a] = std::min(start[static_cast<std::size_t>(a)], ende[static_cast<std::size_t>(a)]) + mins[static_cast<std::size_t>(a)] - 1.0F;
        hi[a] = std::max(start[static_cast<std::size_t>(a)], ende[static_cast<std::size_t>(a)]) + maxs[static_cast<std::size_t>(a)] + 1.0F;
    }
    for (const NavBrush& b : brushes) {
        if (r.allsolid) { break; }
        if (b.entity == ignoriere || (b.inhalt & maske) == 0U) { continue; }
        float ort[3];
        float winkel[3];
        if (!stellung(b, ort, winkel)) { continue; }
        bool weit = false;
        for (int a = 0; a < 3; ++a) {
            if (ort[a] + b.radius < lo[a] || ort[a] - b.radius > hi[a]) { weit = true; }
        }
        if (weit) { continue; }
        const KollisionsSpur t = kollisionsSpur(k, start.data(), mins.data(), maxs.data(), ende.data(), maske, b.modell, ort, winkel);
        if (t.allsolid) {
            if (!r.allsolid) { r.entity = b.entity; }
            r.allsolid = true;
        } else if (t.startsolid) {
            if (!r.startsolid) { r.entity = b.entity; }
            r.startsolid = true;
        }
        if (t.fraction < r.fraction) {
            const bool alt = r.startsolid;
            r.fraction = t.fraction;
            r.endpos = t.endpos;
            r.normal = t.normale;
            r.contents = t.inhalt;
            r.entity = b.entity;
            if (t.allsolid) { r.allsolid = true; }
            if (alt) { r.startsolid = true; }
        }
    }
    return r;
}

// rc601 (bewegung, teil 3): die Welt ohne Mover-Plan (Spawn-Stand) und der
// Plan, den die letzte Mover-Simulation lieferte.
struct NavWeltSpeicher {
    std::uint64_t schluessel = 0;
    std::shared_ptr<const NavWelt> basis;
    std::shared_ptr<const KollisionsKarte> karte;
    std::shared_ptr<const std::vector<NavBrush>> brushes;
    std::shared_ptr<const NavMoverPlan> plan;    // gehoert zu `schluessel`
    std::shared_ptr<const NavWelt> mitPlan;      // basis + plan
};
NavWeltSpeicher& navWeltSpeicher() {
    static NavWeltSpeicher s;
    return s;
}

std::uint64_t navWeltSchluessel() {
    std::uint64_t h = 1469598103934665603ULL;
    const auto dazu = [&h](const std::string& t) {
        for (const char c : t) { h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ULL; }
        h = (h ^ 0xffU) * 1099511628211ULL;
    };
    dazu(g_app->map.path);
    for (const MapEntity& e : g_app->map.entities) {
        dazu(e.classname);
        dazu(e.origin);
        dazu(e.targetname);
        for (const auto& [key, wert] : e.keys) {
            dazu(key);
            dazu(wert);
        }
    }
    h ^= static_cast<std::uint64_t>(g_app->geo.surfaces.size()) * 0x9e3779b97f4a7c15ULL;
    // rc604 fix604 (JK2 Stufe 3): mit den Regeln von Jedi Outcast ein anderes Netz.
    if (navJk2Regeln()) { dazu("jk2"); }
    return h | 1ULL;
}

}  // namespace

void kollisionLaden(const std::string& bsp, const std::string& karte) {
    auto k = std::make_shared<KollisionsKarte>();
    const auto t0 = std::chrono::steady_clock::now();
    KollisionsSpeicher& s = kollisionsSpeicher();
    s.karte = karte;
    s.jk2 = regelwerkJediOutcast();
    if (leseKollision(bsp, *k)) {
        s.k = std::move(k);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        diag::detail("Wege: Kollision der Karte - " + std::to_string(s.k->pinsel.size()) + " Brushes, " +
                     std::to_string(s.k->knoten.size()) + " Knoten, " + std::to_string(s.k->patches.size()) + " Patches, " + std::to_string(static_cast<int>(ms)) + " ms");
    } else {
        s.k.reset();
    }
}

// rc601 integ: die Lichtschwerter (Paket saber) luden die Karte vorher ein
// zweites Mal (saberKollisionLaden) - jetzt teilen sie diese.
namespace {
// rc604 fix604 (JK2 Stufe 3): leseKollision haengt am Regelwerk (SURF_SLICK
// unter Jedi Outcast nicht fest, code/qcommon/cm_load.cpp:272-278) - wechselt
// es, wird die Karte neu gelesen.
void kollisionRegelwerkPruefen() {
    KollisionsSpeicher& s = kollisionsSpeicher();
    if (!s.k || s.jk2 == regelwerkJediOutcast() || s.karte != g_app->map.path) { return; }
    std::string bsp;
    if (!kartenBytes(s.karte, bsp)) { bsp = slurp(s.karte); }
    if (bsp.empty()) {
        s.jk2 = regelwerkJediOutcast();
        return;
    }
    kollisionLaden(bsp, s.karte);
}
}  // namespace

std::shared_ptr<const KollisionsKarte> geladeneKollision(const std::string& karte) {
    kollisionRegelwerkPruefen();
    const KollisionsSpeicher& s = kollisionsSpeicher();
    return (s.k && s.karte == karte) ? s.k : nullptr;
}

namespace {
// Die Welt fuer die Wege ohne Mover-Plan (alles am Spawn-Platz).
std::shared_ptr<const NavWelt> navWeltBasis() {
    NavWeltSpeicher& sp = navWeltSpeicher();
    const std::uint64_t schluessel = navWeltSchluessel();
    if (sp.basis && schluessel == sp.schluessel) {
        return sp.basis;
    }
    sp.plan.reset();
    sp.mitPlan.reset();
    sp.karte.reset();
    sp.brushes.reset();
    const auto t0 = std::chrono::steady_clock::now();
    kollisionRegelwerkPruefen();   // rc604 fix604
    const KollisionsSpeicher& ks = kollisionsSpeicher();
    // Entities ohne worldspawn vorn gehoeren nicht zu dieser .bsp (erfundene
    // Karten der Selbsttests) - dann keine Spuren gegen ihre Brushes.
    const bool zurKarte = !g_app->map.entities.empty() && g_app->map.entities.front().classname == "worldspawn";
    if (g_app->geo.empty() || !ks.k || ks.karte != g_app->map.path || !zurKarte) {
        // Ohne Geometrie: das Netz aus den Entities, ohne Spuren.
        sp.basis = navWeltAusEntities(g_app->map.entities);
        sp.schluessel = schluessel;
        return sp.basis;
    }
    const std::shared_ptr<const KollisionsKarte> k = ks.k;
    // Die Brush-Entities (func_*): wie in bodenWelt. rc601 (bewegung, teil 3):
    // func_wall/func_usable mit START_OFF jetzt auch (ohne contents bis zum
    // use, in PHASE 0 an), eine func_door mit ihren Stellungen pos1/pos2.
    auto brushes = std::make_shared<std::vector<NavBrush>>();
    for (std::size_t i = 0; i < g_app->map.entities.size(); ++i) {
        const MapEntity& e = g_app->map.entities[i];
        const std::string* mdl = e.find("model");
        if (mdl == nullptr || mdl->size() < 2 || (*mdl)[0] != '*') { continue; }
        const std::string& kl = e.classname;
        if (kl.rfind("func_", 0) != 0 || kl == "func_group") { continue; }
        if (!spawntBeiSchwierigkeit(e, regelwerkSpielstufe())) { continue; }   // rc604, wie in bodenWelt
        const std::string* sf = e.find("spawnflags");
        const int flags = sf != nullptr ? std::atoi(sf->c_str()) : 0;
        const int nr = std::atoi(mdl->c_str() + 1);
        if (nr <= 0 || nr >= static_cast<int>(k->modelle.size())) { continue; }
        NavBrush b;
        b.entity = static_cast<int>(i);
        b.modell = nr;
        if (!e.origin.empty()) {
            std::istringstream is(e.origin);
            is >> b.ort[0] >> b.ort[1] >> b.ort[2];
        }
        if (const std::string* w = e.find("angles")) {
            std::istringstream is(*w);
            is >> b.winkel[0] >> b.winkel[1] >> b.winkel[2];
        } else if (const std::string* w1 = e.find("angle")) {
            // func_door/func_plat: "angle" ist die Richtung der Bewegung,
            // nicht die Drehung des Modells.
            if (kl != "func_door" && kl != "func_plat" && kl != "func_button") { b.winkel[1] = static_cast<float>(std::atof(w1->c_str())); }
        }
        if (kl == "func_door") {
            // SP_func_door: G_SetMovedir loescht die Winkel; InitMover stellt
            // sie auf pos1 (bei START_OPEN die offene Stellung), PHASE 0 auf
            // pos2 (bei START_OPEN die geschlossene).
            b.winkel[0] = b.winkel[1] = b.winkel[2] = 0.0F;
            float p1[3];
            float p2[3];
            if (tuerStellungen(e, g_app->geo, p1, p2)) {
                for (int a = 0; a < 3; ++a) {
                    b.ort[a] = p1[a];
                    b.ortPhase0[a] = (flags & 1) != 0 ? p2[a] : p1[a];
                }
            } else {
                for (int a = 0; a < 3; ++a) { b.ortPhase0[a] = b.ort[a]; }
            }
        } else {
            for (int a = 0; a < 3; ++a) { b.ortPhase0[a] = b.ort[a]; }
        }
        // func_wall/func_usable START_OFF: contents 0 bis zum use; in PHASE 0
        // "ent->contents = ent->spawnContents".
        b.festLaufzeit = !((kl == "func_wall" || kl == "func_usable") && (flags & 1) != 0);
        b.festPhase0 = true;
        const KollisionsKarte::Modell& m = k->modelle[static_cast<std::size_t>(nr)];
        for (int j = 0; j < m.zahl; ++j) {
            const int p = m.erster + j;
            if (p >= 0 && p < static_cast<int>(k->pinsel.size())) { b.inhalt |= k->pinsel[static_cast<std::size_t>(p)].inhalt; }
        }
        float r2 = 0.0F;
        for (int c = 0; c < 8; ++c) {
            const float x = (c & 1) != 0 ? m.maxs[0] : m.mins[0];
            const float y = (c & 2) != 0 ? m.maxs[1] : m.mins[1];
            const float z = (c & 4) != 0 ? m.maxs[2] : m.mins[2];
            r2 = std::max(r2, x * x + y * y + z * z);
        }
        b.radius = std::sqrt(r2);
        brushes->push_back(b);
    }
    auto welt = std::make_shared<NavWelt>();
    const NavStellung laufzeit = [](const NavBrush& b, float ort[3], float winkel[3]) {
        for (int a = 0; a < 3; ++a) {
            ort[a] = b.ort[a];
            winkel[a] = b.winkel[a];
        }
        return b.festLaufzeit;
    };
    welt->spur = [k, brushes, laufzeit](const NavVec& start, const NavVec& mins, const NavVec& maxs, const NavVec& ende,
                                        int ignoriere, std::uint32_t maske) {
        return navSpurMit(*k, *brushes, start, mins, maxs, ende, ignoriere, maske, laufzeit);
    };
    // Fuer den Netzbau: PHASE 0, und mit einzelnen Entities anders.
    NavBauHilfe bau;
    const NavStellung phase0 = [](const NavBrush& b, float ort[3], float winkel[3]) {
        for (int a = 0; a < 3; ++a) {
            ort[a] = b.ortPhase0[a];
            winkel[a] = b.winkel[a];
        }
        return b.festPhase0;
    };
    bau.spur = [k, brushes, phase0](const NavVec& start, const NavVec& mins, const NavVec& maxs, const NavVec& ende,
                                    int ignoriere, std::uint32_t maske) {
        return navSpurMit(*k, *brushes, start, mins, maxs, ende, ignoriere, maske, phase0);
    };
    bau.spurVersetzt = [k, brushes, phase0](const NavVec& start, const NavVec& mins, const NavVec& maxs, const NavVec& ende,
                                            int ignoriere, std::uint32_t maske, const NavVersetzt& v) {
        const NavStellung anders = [&v, &phase0](const NavBrush& b, float ort[3], float winkel[3]) {
            for (const NavVersetztEintrag& x : v) {
                if (x.entity != b.entity) { continue; }
                for (int a = 0; a < 3; ++a) {
                    ort[a] = x.ort[static_cast<std::size_t>(a)];
                    winkel[a] = b.winkel[a];
                }
                return x.fest;
            }
            return phase0(b, ort, winkel);
        };
        return navSpurMit(*k, *brushes, start, mins, maxs, ende, ignoriere, maske, anders);
    };
    bau.tuerStellungen = [](int e, NavVec& p1, NavVec& p2) {
        if (e < 0 || static_cast<std::size_t>(e) >= g_app->map.entities.size()) { return false; }
        float a[3];
        float b[3];
        if (!tuerStellungen(g_app->map.entities[static_cast<std::size_t>(e)], g_app->geo, a, b)) { return false; }
        p1 = {a[0], a[1], a[2]};
        p2 = {b[0], b[1], b[2]};
        return true;
    };
    const BspGeometry* geo = &g_app->geo;
    bau.pvs = [geo](const NavVec& a, const NavVec& b) {
        // gi.inPVS: Cluster von a sieht Cluster von b (die Areas der Engine
        // liest behaved nicht).
        const int la = geo->leafAt(a.data());
        const int lb = geo->leafAt(b.data());
        if (la < 0 || lb < 0) { return true; }
        return geo->clusterVisible(geo->leafs[static_cast<std::size_t>(la)].cluster,
                                   geo->leafs[static_cast<std::size_t>(lb)].cluster);
    };
    // Der Boden fuer die Hoehe waehrend des Laufens: das Raster der Schwerkraft
    // (die Patches so, wie man sie sieht).
    const BodenWelt* bw = &bodenWelt();
    auto strahl = std::make_shared<BodenStrahl>([bw](const std::array<float, 3>& von, const std::array<float, 3>& bis,
                                                     double, std::array<float, 3>& treffer) {
        float z = 0.0F;
        if (!rasterTreffer(bw->raster, von[0], von[1], von[2], bis[2], z)) { return false; }
        treffer = {von[0], von[1], z};
        return true;
    });
    welt->boden = [strahl](float x, float y, float zOben, float zUnten, float& bodenZ) {
        return bodenUnter(*strahl, x, y, zOben, zUnten, kNpcHalbBreite, 0.0, bodenZ);
    };
    auto ents = std::make_shared<std::vector<std::string>>();
    for (const MapEntity& e : g_app->map.entities) { ents->push_back(e.classname); }
    welt->istTuer = [ents](int e) {
        return e >= 0 && e < static_cast<int>(ents->size()) && (*ents)[static_cast<std::size_t>(e)] == "func_door";
    };
    // rc604 fix604 (JK2 Stufe 3): Jedi Outcast laedt maps/<karte>.nav, wenn die
    // Pruefsumme zur .bsp passt (codeJK2/game/g_main.cpp:690, CNavigator::Load).
    if (navJk2Regeln()) {
        std::string bsp;
        if (!kartenBytes(g_app->map.path, bsp)) { bsp = slurp(g_app->map.path); }
        std::string name = g_app->map.path;
        const std::size_t schnitt = name.find_last_of("/\\");
        if (schnitt != std::string::npos) { name = name.substr(schnitt + 1); }
        if (name.size() > 4 && (name.compare(name.size() - 4, 4, ".bsp") == 0 || name.compare(name.size() - 4, 4, ".BSP") == 0)) {
            name.resize(name.size() - 4);
        }
        if (!bsp.empty() && !name.empty() && readFromArchives("maps/" + name + ".nav", bau.jk2Nav)) {
            bau.jk2SummeDa = true;
            bau.jk2KartenSumme = navBlockPruefsumme(bsp);
        }
        welt->pvs = bau.pvs;
    }
    welt->netz = baueNavNetz(g_app->map.entities, bau);
    welt->kennung = schluessel;
    sp.basis = welt;
    sp.karte = k;
    sp.brushes = brushes;
    sp.schluessel = schluessel;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (welt->netz->jk2) {
        diag::detail("Wege: Jedi-Outcast-Netz - " + std::to_string(welt->netz->jk2->knoten.size()) + " Knoten" +
                     (welt->netz->jk2->ausDatei ? " aus der .nav" : " aus den Wegpunkten") + ", " +
                     std::to_string(welt->netz->jk2->fehl.size()) + " gesperrte Verbindungen");
    }
    diag::detail("Wege: Wegnetz mit Spuren - " + std::to_string(welt->netz->knoten.size() - 1U) + " Knoten, " +
                 std::to_string(welt->netz->zahlKanten()) + " Kanten, " + std::to_string(brushes->size()) +
                 " Brush-Entities, " + std::to_string(static_cast<int>(ms)) + " ms");
    return sp.basis;
}
}  // namespace

std::shared_ptr<const NavWelt> navWeltVorschau() {
    const std::shared_ptr<const NavWelt> basis = navWeltBasis();
    NavWeltSpeicher& sp = navWeltSpeicher();
    if (!sp.plan || sp.plan->mover.empty() || !sp.karte || !sp.brushes) {
        return basis;
    }
    if (sp.mitPlan) {
        return sp.mitPlan;
    }
    // rc601 (bewegung, teil 3): dieselbe Welt, nur spurt jedes Denkbild gegen
    // die Mover dort, wo sie zu seiner Zeit stehen (NavWelt::spurZeit), und
    // is_valid liest den moverState aus dem Plan.
    auto welt = std::make_shared<NavWelt>(*basis);
    const std::shared_ptr<const KollisionsKarte> k = sp.karte;
    const std::shared_ptr<const std::vector<NavBrush>> brushes = sp.brushes;
    const std::shared_ptr<const NavMoverPlan> plan = sp.plan;
    welt->spurZeit = [k, brushes, plan](const NavVec& start, const NavVec& mins, const NavVec& maxs, const NavVec& ende,
                                        int ignoriere, std::uint32_t maske, double t) {
        const NavStellung zurZeit = [&plan, t](const NavBrush& b, float ort[3], float winkel[3]) {
            if (const NavMoverPlan::Stand* s = plan->standVon(b.entity, t)) {
                for (int a = 0; a < 3; ++a) {
                    ort[a] = s->ort[static_cast<std::size_t>(a)];
                    winkel[a] = s->winkel[static_cast<std::size_t>(a)];
                }
                return s->fest;
            }
            for (int a = 0; a < 3; ++a) {
                ort[a] = b.ort[a];
                winkel[a] = b.winkel[a];
            }
            return b.festLaufzeit;
        };
        return navSpurMit(*k, *brushes, start, mins, maxs, ende, ignoriere, maske, zurZeit);
    };
    welt->mover = plan;
    sp.mitPlan = welt;
    return sp.mitPlan;
}

// rc601 (bewegung, teil 3): siehe app_internal.h.
bool navMoverPlanAusSim(double endeMs) {
    NavWeltSpeicher& sp = navWeltSpeicher();
    (void)navWeltBasis();
    auto plan = std::make_shared<NavMoverPlan>();
    if (sp.brushes) {
        const auto takte = static_cast<std::size_t>(std::max(0.0, endeMs) / kNavDenkMs) + 2U;
        const MoverSim& ms = g_app->moverSim;
        // Nur wer sich ueberhaupt bewegt, umschaltet oder eine Tuer ist, wird
        // je Takt abgefragt; die anderen einmal (sie koennen anders stehen als
        // die feste Welt annimmt).
        std::vector<char> bewegt;
        for (const MoverSim::Mover& mv : ms.mover()) {
            if (mv.modell < 0) { continue; }
            if (static_cast<std::size_t>(mv.modell) >= bewegt.size()) { bewegt.resize(static_cast<std::size_t>(mv.modell) + 1U, 0); }
            const bool dreht = mv.drehAnfang || !mv.drehUm.empty();
            if (!mv.bahn.empty() || dreht || mv.bobPeriodeMs > 0.0 || !mv.umschalten.empty() || !mv.sichtAb.empty() ||
                !mv.drehBahn.empty() || ms.tuerZustand(mv.modell, 0.0) >= 0) {
                bewegt[static_cast<std::size_t>(mv.modell)] = 1;
            }
        }
        // rc604 fix604: Brush-Entities, die ein Skript bewegt (move/rotate:
        // CQuake3GameInterface::Lerp2Pos setzt die Trajektorie und linkt die
        // Entity, Q3_Interface.cpp:8561-8669 MD / 8034 OpenJK; G_RunMover
        // bewegt sie jedes Bild) oder entfernt (remove: Q3_RemoveEnt, bei einer
        // Entity ohne client G_FreeEntity 100 ms spaeter, :7180-7224 MD /
        // 6751 OpenJK - danach keine contents). Die Szene fuehrt sie als
        // Figur mit brushModel; die Mover-Simulation kennt nur die use der
        // Karte. Bis fix604 standen sie fuer die Wege am Spawnort und blieben
        // nach remove fest: md_dd_sith end_sith - emperor2 lief zwei Minuten
        // gegen wall_chunk, den das Skript weggeworfen und entfernt hatte.
        struct SzenenBrush {
            const Actor* a = nullptr;
            bool bewegt = false;     // Ort/Winkel aus der Szene
            bool entfernt = false;   // irgendwann unsichtbar (remove)
        };
        std::map<int, SzenenBrush> szene;
        for (const Actor& a : g_app->scene.actors) {
            if (a.brushModel <= 0 || !a.haveStart) { continue; }
            SzenenBrush sb;
            sb.a = &a;
            const ActorState a0 = a.at(0.0);
            for (std::size_t f = 0; f < takte; ++f) {
                const ActorState st = a.at(static_cast<double>(f) * kNavDenkMs);
                for (int q = 0; q < 3; ++q) {
                    sb.bewegt = sb.bewegt || st.pos[q] != a0.pos[q] || st.angles[q] != a0.angles[q];
                }
                sb.entfernt = sb.entfernt || !st.visible;
            }
            if (sb.bewegt || sb.entfernt) { szene[a.brushModel] = sb; }
        }
        for (const NavBrush& b : *sp.brushes) {
            const MoverSim::Stand s0 = ms.at(b.modell, 0.0);
            const auto si = szene.find(b.modell);
            const SzenenBrush* sb = si != szene.end() ? &si->second : nullptr;
            if (!s0.bekannt && sb == nullptr) { continue; }   // die Mover-Simulation kennt ihn nicht: steht
            const MapEntity& e = g_app->map.entities[static_cast<std::size_t>(b.entity)];
            const bool tuer = e.classname == "func_door";
            NavMoverPlan::Mover m;
            m.entity = b.entity;
            m.radius = b.radius;
            if (sp.karte && b.modell > 0 && static_cast<std::size_t>(b.modell) < sp.karte->modelle.size()) {
                const KollisionsKarte::Modell& km = sp.karte->modelle[static_cast<std::size_t>(b.modell)];
                m.kastenDa = true;
                m.kastenMin = {km.mins[0], km.mins[1], km.mins[2]};
                m.kastenMax = {km.maxs[0], km.maxs[1], km.maxs[2]};
            }
            m.anfang.ort = {b.ort[0], b.ort[1], b.ort[2]};
            m.anfang.winkel = {b.winkel[0], b.winkel[1], b.winkel[2]};
            m.anfang.fest = b.festLaufzeit;
            m.anfang.zustand = 0;
            if (tuer) {
                const std::string* sf = e.find("spawnflags");
                m.anfang.verriegelt = sf != nullptr && (std::atoi(sf->c_str()) & 16) != 0;
            }
            const auto standBei = [&](double t) {
                const MoverSim::Stand s = ms.at(b.modell, t);
                NavMoverPlan::Stand x;
                x.ort = {s.origin[0], s.origin[1], s.origin[2]};
                x.winkel = {s.angles[0], s.angles[1], s.angles[2]};
                x.fest = s.sichtbar;   // func_wall START_OFF bis zum use, func_breakable nach dem Bruch: keine contents
                if (!s.bekannt) {
                    x.ort = m.anfang.ort;
                    x.winkel = m.anfang.winkel;
                    x.fest = m.anfang.fest;
                }
                if (sb != nullptr) {   // rc604 fix604
                    const ActorState st = sb->a->at(t);
                    if (sb->bewegt) {
                        x.ort = {st.pos[0], st.pos[1], st.pos[2]};
                        x.winkel = {st.angles[0], st.angles[1], st.angles[2]};
                    }
                    if (sb->entfernt && !st.visible && !sb->a->at(t - 100.0).visible) { x.fest = false; }
                }
                bool riegel = m.anfang.verriegelt;
                const int z = tuer ? ms.tuerZustand(b.modell, t, &riegel) : -1;
                x.zustand = static_cast<signed char>(z < 0 ? 0 : z);
                x.verriegelt = z < 0 ? m.anfang.verriegelt : riegel;
                return x;
            };
            bool anders = false;
            const bool jeTakt = (static_cast<std::size_t>(b.modell) < bewegt.size() && bewegt[static_cast<std::size_t>(b.modell)] != 0) ||
                                sb != nullptr;
            const std::size_t zahl = jeTakt ? takte : 1U;
            m.takte.reserve(zahl);
            for (std::size_t f = 0; f < zahl; ++f) {
                m.takte.push_back(standBei(static_cast<double>(f) * kNavDenkMs));
                anders = anders || !m.takte.back().gleich(m.anfang);
            }
            if (!anders) { continue; }
            // Hinten gleiche Takte weglassen (danach gilt der letzte).
            while (m.takte.size() > 1U && m.takte[m.takte.size() - 2U].gleich(m.takte.back())) { m.takte.pop_back(); }
            plan->mover.push_back(std::move(m));
        }
    }
    plan->fertig(g_app->map.entities.size());
    const std::uint64_t alt = sp.plan ? sp.plan->kennung : 0U;
    if (plan->kennung == alt) {
        return false;
    }
    sp.plan = plan->mover.empty() ? nullptr : std::shared_ptr<const NavMoverPlan>(plan);
    sp.mitPlan.reset();
    diag::detail("Wege: Mover zur Zeit - " + std::to_string(plan->mover.size()) + " Brush-Entities bewegen sich oder stehen anders als beim Spawnen");
    return true;
}

// rc601 (bewegung, Durchsicht 2): siehe app_internal.h.
void schwierigkeitWaehlen(int n) {
    // rc601 integ: -1 = wieder die Vorgabe des Regelwerks (MD 2, OpenJK 0).
    n = std::clamp(n, -1, 3);
    if (n == g_app->settings.schwierigkeit) { return; }
    g_app->settings.schwierigkeit = n;
    (void)regelwerkPruefen();   // rc601 integ: g_spskill auch fuer die Schwertrechnung
    // Neu aufbauen (prepareScene, ueber die Zeitleiste): Drehrate und Tempo
    // beim Spawnen haengen daran, und mit ihnen Wege, dowait und Zeiten.
    g_app->camTrackValid = false;
}

}  // namespace bhed::gui
