// app_saber.cpp - Paket saber (rc601): Lichtschwerter beruehren Waende und
// einander, in der Vorschau wie im Spiel.
//
// Gemeldet (Tester, Movie Duels): "There are no saber collisions".
// Entschieden: "Funken und Klang" - die Effekte und Klaenge des Spiels, die
// Animationen laufen weiter wie im Skript. "es soll genauso laufen wie im
// Spiel."
//
// Die Rechnungen stehen rein in bhed/saberkontakt.h. Hier kommt dazu, was
// nur die Anwendung weiss: wo die Klingen zu einem Zeitpunkt stehen (dieselbe
// Knochenrechnung wie die Zeichnung: figurBilderSetzen, figurKnochen,
// figurWeltMatrix, boltMatrix, klingenStrecke), die Welt samt Brush-Modellen
// an ihrem Platz (Spuren gegen die Brushes wie gi.trace), und wohin die
// Ergebnisse gehen: Effekte in die Effektliste, Klaenge in den Raumklang
// (spieleRaumKlang des Pakets ton), die gekuerzte Klinge in die Zeichnung.
//
// Takte: der Client zeichnet und spurt alle 8 ms (com_maxfps 125), der Server
// prueft die Klingen gegeneinander alle 50 ms (sv_fps 20). Die Takte werden
// der Reihe nach gerechnet und zwischengespeichert; nach einem Sprung auf der
// Zeitleiste beginnt die Rechnung kVorlaufTakte Clientbilder vorher mit der
// ungekuerzten Klinge. Das genuegt: eine gekuerzte Klinge waechst mit
// lengthMax je Sekunde nach, und das Kuerzen an einer Wand laeuft in wenigen
// Bildern auf denselben Wert zu - nach einer Sekunde ist der Stand derselbe
// wie bei durchgehender Rechnung (bis auf Rundung). Die Wuerfel haengen am
// Takt, nicht an einer Zufallsfolge: dasselbe Bild zeigt nach dem
// Zurueckspringen dieselben Funken.
#include "app_internal.h"

#include "bhed/gpuskin.h"
#include "bhed/kollision.h"
#include "bhed/saberkontakt.h"
#include "bhed/saberspuren.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace bhed::gui {

int g_saberBlitze = 0;   // gezeichnete Klirrblitze (Selbsttest)
// rc601 saber review 2c: wo der Blitz gezeichnet wurde - im Effektnetz (unter
// den Kinobalken) oder als 2D-Bild nach den Balken wie im Spiel.
int g_saberBlitzeNetz = 0;   // bleibt 0: der Blitz liegt nicht mehr im Effektnetz
int g_saberBlitze2D = 0;
int g_saberBlitzeNachBalken = 0;

namespace {

// --- Die Brushes der Karte (CM_LoadMap) -----------------------------------
//
// rc601 integ: dieselbe Karte wie die Wege des Pakets bewegung
// (kollisionsSpeicher() in app_bewegung.cpp, geladeneKollision) - vorher hier
// ein zweites Mal geladen (saberKollisionLaden).
std::shared_ptr<const KollisionsKarte> saberKarte() { return geladeneKollision(g_app->map.path); }

// Ein Brush-Modell, wie SV_ClipMoveToEntities es zu einem Zeitpunkt sieht.
struct BrushLage {
    int modell = 0;
    float ort[3]{};
    float winkel[3]{};
    float radius = 0.0F;
};

// Wo die Brush-Modelle zum Zeitpunkt ms stehen - dieselben Quellen wie die
// Zeichnung (drawMapView): Skriptfiguren mit Brush-Modell ueber Actor::at,
// Tueren und Co. ueber die Mover-Simulation, sonst der Ursprung der Entity.
// Trigger sind nicht fest (InitTrigger: contents = CONTENTS_TRIGGER) und
// fehlen hier, ebenso Modelle ohne Entity (nicht verlinkt).
std::vector<BrushLage> brushLagenBei(double ms) {
    std::vector<BrushLage> aus;
    const BspGeometry& geo = g_app->geo;
    std::map<int, std::array<float, 3>> entOrt;
    for (const MapEntity& e : g_app->map.entities) {
        const std::string* mdl = e.find("model");
        if (mdl == nullptr || mdl->size() < 2 || (*mdl)[0] != '*') {
            continue;
        }
        const int nr = std::atoi(mdl->c_str() + 1);
        if (nr <= 0 || e.classname.rfind("trigger_", 0) == 0) {
            continue;
        }
        std::array<float, 3> o{0.0F, 0.0F, 0.0F};
        if (!e.origin.empty()) {
            std::istringstream is(e.origin);
            is >> o[0] >> o[1] >> o[2];
        }
        entOrt[nr] = o;
    }
    std::map<int, const Actor*> skript;
    for (const Actor& a : g_app->scene.actors) {
        if (a.brushModel > 0 && a.haveStart) {
            skript[a.brushModel] = &a;
        }
    }
    for (std::size_t k = 1; k < geo.models.size(); ++k) {
        const int nr = static_cast<int>(k);
        if (entOrt.count(nr) == 0U) {
            continue;
        }
        BrushLage b;
        b.modell = nr;
        const auto sk = skript.find(nr);
        if (sk != skript.end()) {
            const ActorState st = sk->second->at(ms);
            if (!st.visible) {
                continue;
            }
            for (int i = 0; i < 3; ++i) {
                b.ort[i] = st.pos[i];
                b.winkel[i] = st.angles[i];
            }
        } else {
            const MoverSim::Stand m = g_app->moverSim.at(nr, ms);
            if (m.bekannt) {
                if (!m.sichtbar) {
                    continue;
                }
                for (int i = 0; i < 3; ++i) {
                    b.ort[i] = m.origin[i];
                    b.winkel[i] = m.angles[i];
                }
            } else {
                const std::array<float, 3>& o = entOrt[nr];
                for (int i = 0; i < 3; ++i) { b.ort[i] = o[static_cast<std::size_t>(i)]; }
            }
        }
        const BspGeometry::SubModel& sm = geo.models[k];
        float r2 = 0.0F;
        for (int i = 0; i < 3; ++i) {
            const float m = std::max(std::fabs(sm.mins[i]), std::fabs(sm.maxs[i]));
            r2 += m * m;
        }
        b.radius = std::sqrt(r2) + 1.0F;
        aus.push_back(b);
    }
    return aus;
}

// gi.trace fuer einen Punkt (SV_Trace): erst die Welt, dann die
// Brush-Entities; die naechste Beruehrung gewinnt, startsolid/allsolid
// sammeln sich (SV_ClipMoveToEntities, sv_world.cpp).
SaberSpur weltSpur(const KollisionsKarte& k, const std::vector<BrushLage>& brushes, const float a[3],
                   const float b[3], std::uint32_t maske) {
    static const float kNull[3] = {0.0F, 0.0F, 0.0F};
    const auto wandle = [](const KollisionsSpur& w) {
        SaberSpur t;
        t.fraction = w.fraction;
        t.startsolid = w.startsolid;
        t.allsolid = w.allsolid;
        t.inhalt = w.inhalt;
        t.flaeche = w.flaeche;
        for (int i = 0; i < 3; ++i) {
            t.endpos[i] = w.endpos[static_cast<std::size_t>(i)];
            t.normale[i] = w.normale[static_cast<std::size_t>(i)];
        }
        return t;
    };
    SaberSpur r = wandle(kollisionsSpur(k, a, kNull, kNull, b, maske));
    if (r.allsolid) {
        return r;
    }
    float lo[3];
    float hi[3];
    for (int i = 0; i < 3; ++i) {
        lo[i] = std::min(a[i], b[i]) - 1.0F;
        hi[i] = std::max(a[i], b[i]) + 1.0F;
    }
    for (const BrushLage& br : brushes) {
        if (r.allsolid) {
            break;
        }
        bool weit = false;
        for (int i = 0; i < 3; ++i) {
            if (br.ort[i] + br.radius < lo[i] || br.ort[i] - br.radius > hi[i]) { weit = true; }
        }
        if (weit) {
            continue;
        }
        SaberSpur t = wandle(kollisionsSpur(k, a, kNull, kNull, b, maske, br.modell, br.ort, br.winkel));
        t.brushModell = true;
        if (t.allsolid) {
            r.allsolid = true;
        } else if (t.startsolid) {
            r.startsolid = true;
        }
        if (t.fraction < r.fraction) {
            const bool alt = r.startsolid;
            const bool altAll = r.allsolid;
            r = t;
            if (alt) { r.startsolid = true; }
            if (altAll) { r.allsolid = true; }
        }
    }
    return r;
}

// --- Proben: wo die Klingen zu einem Zeitpunkt stehen ---------------------

int klingenSchluessel(int figur, int fach, int klinge) { return (figur << 8) | (fach << 4) | klinge; }

struct KlingeProbe {
    int fach = 0;
    int klinge = 0;
    float lSzene = 0.0F;
    float mp[3]{};
    float md[3]{};
};
struct FigurProbe {
    SaberFigur f;
    std::vector<KlingeProbe> klingen;
    bool angriff = false;   // PM_SaberInSpecialAttack(torsoAnim)
    int stil = 0;           // ps.saberAnimLevel (rc601 saber review 2b)
};

std::string gross(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
    return s;
}

const NpcDef* npcFuer(const std::string& typ) {
    auto it = g_app->npcMap.find(typ);
    if (it == g_app->npcMap.end()) {
        std::string k = typ;
        for (char& c : k) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        it = g_app->npcMap.find(k);
    }
    return it == g_app->npcMap.end() ? nullptr : &it->second;
}

// Eine Figur zum Zeitpunkt ms. false: der Client zeichnet sie nicht (kein
// Modell, unsichtbar, fort) oder sie haelt kein Lichtschwert.
bool figurProbe(std::size_t ai, double ms, FigurProbe& out) {
    if (ai >= g_app->scene.actors.size() || ai >= g_app->actorAssets.size()) {
        return false;
    }
    const Actor& a = g_app->scene.actors[ai];
    App::ActorAssets& assets = g_app->actorAssets[ai];
    if (assets.model.empty() || !a.haveStart || a.brushModel > 0) {
        return false;
    }
    const ActorState st = a.at(ms);
    // Eine unsichtbare Figur zeichnet der Client nicht (EF_NODRAW) - ihre
    // Klinge waechst nicht, wird nicht gekuerzt und schreibt keine Lage.
    if (!st.visible || st.removed || !haeltSaber(st)) {
        return false;
    }
    ActorDraw d;
    d.figur = static_cast<int>(ai);
    d.model = &assets.model;
    figurBilderSetzen(a, assets, st, ms, d);
    if (d.anim == nullptr) {
        return false;
    }
    for (int k = 0; k < 3; ++k) { d.pos[k] = st.pos[k]; }
    d.yaw = st.angles[1];
    npcSkalaFuer(a.npcType, d.skala);
    anbautenFuer(a, st, ms, d);
    figurKopfGier(st, d);
    d.yawOffset = static_cast<float>(g_app->actorYaw);
    std::vector<BoneMatrix> welt;
    std::vector<BoneMatrix> hilf;
    if (!figurKnochen(d, welt, hilf) || welt.empty()) {
        return false;
    }
    float fw[16];
    figurWeltMatrix(d, fw);

    SaberFigur& f = out.f;
    f = SaberFigur{};
    out.klingen.clear();
    f.figur = static_cast<int>(ai);
    f.npc = !a.npcType.empty() && gross(a.npcType) != "PLAYER";
    f.verhalten = st.verhalten;
    f.saberKollisionen = st.saberKollisionen;
    f.torsoAnim = gross(st.torsoAnimation.empty() ? st.animation : st.torsoAnimation);
    for (int k = 0; k < 3; ++k) { f.ort[k] = st.pos[k]; }
    f.gier = st.angles[1];
    f.dual = st.dualSabers;
    if (const NpcDef* nd = npcFuer(a.npcType)) {
        f.klasse = gross(nd->klasse);
        f.team = gross(nd->team);
    }
    out.angriff = saberSonderAngriff(f.torsoAnim);
    out.stil = saberStilJetzt(a, st);
    for (int n = 0; n < 2; ++n) {
        const SaberZustand& s = st.saber[static_cast<std::size_t>(n)];
        SaberSchwert& sw = f.schwert[static_cast<std::size_t>(n)];
        sw.numBlades = std::min(s.numBlades, kMaxKlingen);
        if (s.art >= 0 && static_cast<std::size_t>(s.art) < a.saberArten.size()) {
            const SaberArt& art = a.saberArten[static_cast<std::size_t>(s.art)];
            sw.typ = art.typ;
            sw.bladeStyle2Start = art.bladeStyle2Start;
            sw.ohneKlinge = art.ohneKlinge;
            sw.ohneKlinge2 = art.ohneKlinge2;
            sw.zweihaendig = art.zweihaendig;
            sw.werte = art.kontakt;
        }
        for (int b = 0; b < sw.numBlades; ++b) {
            const KlingenZustand& kz = s.klinge[static_cast<std::size_t>(b)];
            SaberKlinge& kl = sw.klinge[static_cast<std::size_t>(b)];
            kl.aktiv = kz.an;
            kl.laengeMax = kz.max;
            kl.radius = (kz.radius > 0.0F) ? kz.radius : 3.0F;
        }
    }
    // Die Lage der Klingen: Griff am Handbolzen, Klinge N am Bolzen des
    // Griffs - wie die Zeichnung (drawMapView, "Was die Figur in den Haenden
    // haelt"). muzzleDir ist die Einheitsrichtung zur Spitze.
    for (int ai2 = 0; ai2 < d.anbauten; ++ai2) {
        const AnbauDraw& an = d.anbau[static_cast<std::size_t>(ai2)];
        if (an.model == nullptr || an.saberFach < 0 || an.saberFach > 1) {
            continue;
        }
        BoneMatrix hand{};
        const int handSurf = surfaceIndex(*d.model, an.bolzen);
        if (!boltMatrix(*d.model, handSurf, welt, *d.anim, hand)) {
            continue;
        }
        float gw[16];
        gpu::baueGriffWelt(fw, hand, gw);
        const auto nachWelt = [&gw](const float in[3], float o[3]) {
            for (int r = 0; r < 3; ++r) {
                o[r] = gw[r * 4 + 0] * in[0] + gw[r * 4 + 1] * in[1] + gw[r * 4 + 2] * in[2] + gw[r * 4 + 3];
            }
        };
        const int n = an.saberFach;
        const SaberSchwert& sw = f.schwert[static_cast<std::size_t>(n)];
        for (int b = 0; b < sw.numBlades; ++b) {
            if (sw.zweiteArt(b) ? sw.ohneKlinge2 : sw.ohneKlinge) {
                continue;
            }
            const SaberKlinge& kl = sw.klinge[static_cast<std::size_t>(b)];
            float wl[3];
            float tl[3];
            if (!klingenStrecke(*an.model, an.lage, b, std::max(kl.laengeMax, 1.0F), wl, tl)) {
                continue;
            }
            KlingeProbe kp;
            kp.fach = n;
            kp.klinge = b;
            kp.lSzene = klingenLaenge(st, n, b, ms);
            float wt[3];
            nachWelt(wl, kp.mp);
            nachWelt(tl, wt);
            float l = 0.0F;
            for (int i = 0; i < 3; ++i) {
                kp.md[i] = wt[i] - kp.mp[i];
                l += kp.md[i] * kp.md[i];
            }
            l = std::sqrt(l);
            if (l <= 0.0F) {
                continue;
            }
            for (float& c : kp.md) { c /= l; }
            out.klingen.push_back(kp);
        }
    }
    return true;
}

// --- Der Zwischenspeicher der Takte ----------------------------------------

// rc601 saber review: nach einem Sprung beginnt die Rechnung nicht mehr mit
// einem "eingeschwungenen" Ersatzstand, sondern am letzten Schnappschuss
// (alle kSchnappTakte Clientbilder) vor dem Fenster der Brandspuren - also
// genau so, als liefe die Szene vom Anfang durch. Der Ersatzstand (1 s Vorlauf
// mit der ungekuerzten Klinge) traf die Laenge nur bis aufs letzte Bit: eine
// fast stehende Klinge zeichnet ihre Brandspur dann anders gedreht (die
// Richtung von oldPos nach endpos ist bei 1e-4 Abstand reines Rundungsrauschen)
// - gemessen im Selbsttest "Vorspulen gleich Abspielen".
constexpr long long kSpurFensterTakte = kSpurGesamtZeit / kSaberClientTaktMs;   // 1250 (10 s)
constexpr long long kVorlaufTakte = kSpurFensterTakte + 125;                    // so weit rechnet ein Sprung vor
constexpr long long kSchnappTakte = 125;                                         // 1 s
constexpr double kEffektRueckblickMs = 1000.0;   // so weit zurueck stehen Effekte in der Liste
constexpr double kKlirrenBehaltenMs = 3000.0;

struct ClientBild {
    std::vector<SaberFigur> figuren;                 // Lage und gekuerzte Laenge (nur die letzten Bilder)
    std::vector<std::pair<int, float>> zeichen;      // Schluessel -> Zeichenlaenge (< 0: nicht gezeichnet)
    std::vector<SaberWandEreignis> ereignisse;
    std::vector<SpurPoly> marken;                    // rc601 saber review: CG_AllocMark dieses Bildes
};

struct SaberSpeicher {
    std::uint64_t schluessel = 0;
    bool gueltig = false;
    long long letzter = LLONG_MIN;
    std::map<long long, ClientBild> bilder;
    std::map<int, SaberWandKlinge> klingen;
    std::map<int, SaberWandFigur> wandFiguren;
    std::map<int, SaberFigur> server;                // Stand nach dem letzten Serverbild
    std::map<long long, std::vector<SaberKlirren>> klirren;   // je Serverbild
    // Leistung
    double rechenMs = 0.0;
    long long takte = 0;
    long long probenFiguren = 0;
    double spurenMs = 0.0;            // davon CG_CreateSaberMarks
    long long spurenPolys = 0;
    long long erzeugung = 0;          // zaehlt das Neuaufsetzen (fuer spurenStand)
    // Der ganze Stand nach Clientbild t (samt Bild t, das der Server des
    // naechsten Bildes noch liest) - alle kSchnappTakte Bilder.
    struct Schnappschuss {
        std::map<int, SaberWandKlinge> klingen;
        std::map<int, SaberWandFigur> wandFiguren;
        std::map<int, SaberFigur> server;
        ClientBild bild;
    };
    std::map<long long, Schnappschuss> schnapp;
};
SaberSpeicher& speicher() {
    static SaberSpeicher s;
    return s;
}

std::uint64_t speicherSchluessel() {
    std::uint64_t h = 1469598103934665603ULL;
    const auto dazu = [&h](std::uint64_t x) { h = (h ^ x) * 1099511628211ULL; };
    dazu(static_cast<std::uint64_t>(g_app->szeneBauten));
    dazu(static_cast<std::uint64_t>(g_app->actorAssets.size()));
    dazu(static_cast<std::uint64_t>(g_app->scene.actors.size()));
    dazu(static_cast<std::uint64_t>(g_app->geo.surfaces.size()));
    dazu(saberMovieDuelsRegeln() ? 1U : 2U);
    dazu(static_cast<std::uint64_t>(g_app->actorYaw + 1000));
    dazu(reinterpret_cast<std::uintptr_t>(saberKarte().get()));
    for (const char c : g_app->map.path) { dazu(static_cast<unsigned char>(c)); }
    return h;
}

// --- Die Flaechen der Karte fuer die Brandspuren (rc601 saber review) -------
struct SpurKarte {
    std::string pfad;
    std::size_t flaechen = 0;
    std::shared_ptr<const SpurWelt> w;
};
const SpurWelt* spurWelt() {
    static SpurKarte s;
    const BspGeometry& geo = g_app->geo;
    if (geo.surfaces.empty() || geo.nodes.empty()) {
        return nullptr;
    }
    if (!s.w || s.pfad != g_app->map.path || s.flaechen != geo.surfaces.size()) {
        const auto t0 = std::chrono::steady_clock::now();
        s.w = std::make_shared<const SpurWelt>(spurWeltAus(geo));
        s.pfad = g_app->map.path;
        s.flaechen = geo.surfaces.size();
        std::size_t gitter = 0;
        for (const SpurFlaeche& f : s.w->flaechen) { gitter += f.art == SpurFlaeche::Art::Gitter ? 1U : 0U; }
        diag::detail("Schwerter: Flaechen fuer Brandspuren - " + std::to_string(s.w->flaechen.size()) + " Flaechen, " +
                     std::to_string(gitter) + " Gitter (R_SubdividePatchToGrid), " +
                     std::to_string(static_cast<int>(
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count())) +
                     " ms");
    }
    return s.w->leer() ? nullptr : s.w.get();
}

bool bereit() {
    return g_app != nullptr && !g_app->scene.actors.empty() &&
           g_app->actorAssets.size() == g_app->scene.actors.size();
}

SaberSpurFn weltFuer(double ms, std::shared_ptr<const std::vector<BrushLage>>& halter) {
    std::shared_ptr<const KollisionsKarte> k = saberKarte();
    if (!k || g_app->map.entities.empty() ||
        g_app->map.entities.front().classname != "worldspawn") {
        return SaberSpurFn{};
    }
    halter = std::make_shared<const std::vector<BrushLage>>(brushLagenBei(ms));
    std::shared_ptr<const std::vector<BrushLage>> b = halter;
    return [k, b](const float x[3], const float y[3], std::uint32_t maske) { return weltSpur(*k, *b, x, y, maske); };
}

// Ein Serverbild (WP_SaberUpdate, WP_SabersDamageTrace, WP_SaberUpdateOldBladeData
// fuer jede Figur der Reihe nach) mit der Lage des letzten Clientbildes.
void serverSchritt(SaberSpeicher& sp, long long s, const ClientBild& bild, const SaberSpurFn& welt) {
    std::vector<SaberFigur> fig = bild.figuren;
    for (SaberFigur& f : fig) {
        const auto p = sp.server.find(f.figur);
        if (p == sp.server.end()) {
            continue;   // hatAlt false: "just started up the saber?"
        }
        for (std::size_t n = 0; n < 2; ++n) {
            for (std::size_t b = 0; b < 8; ++b) {
                SaberKlinge& kl = f.schwert[n].klinge[b];
                const SaberKlinge& alt = p->second.schwert[n].klinge[b];
                for (int i = 0; i < 3; ++i) {
                    kl.mpAlt[i] = alt.mpAlt[i];
                    kl.mdAlt[i] = alt.mdAlt[i];
                }
                kl.laengeAlt = alt.laengeAlt;
            }
        }
        f.kasten = p->second.kasten;
        f.hatAlt = p->second.hatAlt;
    }
    std::vector<SaberKlirren> aus;
    saberServerTakt(fig, static_cast<double>(s * kSaberServerTaktMs), welt, aus);
    sp.server.clear();
    for (const SaberFigur& f : fig) { sp.server[f.figur] = f; }
    if (!aus.empty()) {
        sp.klirren[s] = std::move(aus);
    }
}

// rc601 saber review: der Ort, von dem aus der Server den Snapshot baut
// (SV_BuildClientSnapshot, sv_snapshot.cpp:549 ff. OJ): "if in camera mode use
// camera position instead" (CG_CAMERA_POS), sonst das Auge des Spielers
// (origin + viewheight; stehend 36 = DEFAULT_MAXS_2 40 +
// STANDARD_VIEWHEIGHT_OFFSET -4). Nur Figuren im Snapshot zeichnet der Client
// (CG_AddPacketEntities) - nur fuer sie laeuft CG_AddSaberBladeGo: Wachsen,
// Kuerzen, Funken, Klang, Brandspur und das Schreiben der Klingenlage, die
// der Server liest. false: weder Kamera noch Spieler - alle zaehlen.
bool snapshotAuge(double ms, float auge[3]) {
    if (g_app->camTrackValid && !g_app->camTrack.segments.empty()) {
        const CameraState c = g_app->camTrack.at(ms);
        if (c.enabled) {
            for (int k = 0; k < 3; ++k) { auge[k] = c.pos[k]; }
            return true;
        }
    }
    for (const Actor& a : g_app->scene.actors) {
        if (a.haveStart && gross(a.name) == "PLAYER") {
            const ActorState st = a.at(ms);
            for (int k = 0; k < 3; ++k) { auge[k] = st.pos[k]; }
            auge[2] += 36.0F;
            return true;
        }
    }
    return false;
}

// Ein Clientbild t (Zeit t * 8 ms) fuer alle Figuren.
void clientSchritt(SaberSpeicher& sp, long long t) {
    const double ms = static_cast<double>(t * kSaberClientTaktMs);
    std::shared_ptr<const std::vector<BrushLage>> halter;
    const SaberSpurFn welt = weltFuer(ms, halter);
    ClientBild bild;
    float auge[3];
    const bool mitAuge = snapshotAuge(ms, auge);
    for (std::size_t ai = 0; ai < g_app->scene.actors.size(); ++ai) {
        FigurProbe p;
        if (!figurProbe(ai, ms, p)) {
            continue;
        }
        if (mitAuge) {
            // SV_LinkEntity: absmin/absmax des Kastens, um eine Einheit
            // erweitert (sv_world.cpp:296 ff. MD) - wie tonKastenSichtbar.
            float mn[3];
            float mx[3];
            for (int k = 0; k < 3; ++k) {
                mn[k] = p.f.ort[k] + p.f.mins[k] - 1.0F;
                mx[k] = p.f.ort[k] + p.f.maxs[k] + 1.0F;
            }
            if (!kastenImSnapshot(g_app->geo, auge, mn, mx)) {
                // Nicht im Snapshot: der Client zeichnet die Figur nicht, ihre
                // Klingen behalten Laenge und Lage des letzten gezeichneten
                // Bildes (nie gezeichnet: Laenge 0) - der Server liest sie
                // trotzdem (WP_SaberUpdate, WP_SaberDamageTrace laufen fuer jede
                // Figur). Ort, Haltung und Verhalten sind die des Servers.
                SaberFigur& f = p.f;
                const auto alt = sp.server.find(f.figur);
                for (std::size_t n = 0; n < 2; ++n) {
                    for (std::size_t b = 0; b < 8; ++b) {
                        SaberKlinge& kl = f.schwert[n].klinge[b];
                        const auto wk = sp.klingen.find(klingenSchluessel(f.figur, static_cast<int>(n), static_cast<int>(b)));
                        kl.laenge = (wk != sp.klingen.end()) ? wk->second.laenge : 0.0F;
                        for (int i = 0; i < 3; ++i) {
                            kl.mp[i] = (alt != sp.server.end()) ? alt->second.schwert[n].klinge[b].mp[i] : 0.0F;
                            kl.md[i] = (alt != sp.server.end()) ? alt->second.schwert[n].klinge[b].md[i] : 0.0F;
                        }
                    }
                }
                bild.figuren.push_back(std::move(f));
                continue;
            }
        }
        ++sp.probenFiguren;
        SaberFigur& f = p.f;
        SaberWandFigur& wf = sp.wandFiguren[f.figur];
        for (const KlingeProbe& kp : p.klingen) {
            const int key = klingenSchluessel(f.figur, kp.fach, kp.klinge);
            SaberSchwert& sw = f.schwert[static_cast<std::size_t>(kp.fach)];
            SaberKlinge& kl = sw.klinge[static_cast<std::size_t>(kp.klinge)];
            auto wk = sp.klingen.find(key);
            if (wk == sp.klingen.end()) {
                // Neu im Blick: mit der Laenge der Szene beginnen (ungekuerzt).
                SaberWandKlinge neu;
                neu.laenge = kp.lSzene;
                wk = sp.klingen.emplace(key, neu).first;
            }
            const bool zweite = sw.zweiteArt(kp.klinge);
            SaberWandEingabe in;
            in.ms = ms;
            for (int i = 0; i < 3; ++i) {
                in.wurzelOrt[i] = f.ort[i];
                in.org[i] = kp.mp[i];
                in.achse[i] = kp.md[i];
            }
            in.laengeMax = kl.laengeMax;
            in.aktiv = kl.aktiv;
            in.numBlades = std::max(sw.numBlades, 1);
            in.keineWandspuren = zweite ? sw.werte.keineWandspuren2 : sw.werte.keineWandspuren;
            in.keinLeerlauf = zweite ? sw.werte.keinLeerlauf2 : sw.werte.keinLeerlauf;
            in.angriff = p.angriff;
            in.stil = p.stil;
            in.superBruchSieg = saberSuperBruchSieg(f.torsoAnim);
            in.saat = saberSaat(static_cast<std::uint64_t>(t), static_cast<std::uint64_t>(key), 0x77616c6cULL);
            const std::size_t vorher = bild.ereignisse.size();
            const SaberWandBild wb = saberWandTakt(wk->second, wf, in, welt, bild.ereignisse);
            for (std::size_t e = vorher; e < bild.ereignisse.size(); ++e) { bild.ereignisse[e].figur = f.figur; }
            bild.zeichen.emplace_back(key, wb.gezeichnet ? wb.zeichenLaenge : -1.0F);
            for (int i = 0; i < 3; ++i) {
                kl.mp[i] = kp.mp[i];
                kl.md[i] = kp.md[i];
            }
            kl.laenge = wk->second.laenge;
        }
        bild.figuren.push_back(std::move(f));
    }
    // rc601 saber review: CG_CreateSaberMarks fuer jede Brandspur dieses
    // Bildes, in der Reihenfolge der Klingen (CG_AllocMark).
    if (const SpurWelt* sw = spurWelt()) {
        const auto t0 = std::chrono::steady_clock::now();
        std::uint64_t nr = 0;
        for (const SaberWandEreignis& e : bild.ereignisse) {
            if (e.art != SaberWandEreignis::Art::Spur) {
                continue;
            }
            saberSpurenErzeugen(*sw, e.von, e.ort, e.normale, static_cast<int>(t * kSaberClientTaktMs),
                                saberSaat(static_cast<std::uint64_t>(t), nr++, 0x7370757aULL), bild.marken);
        }
        sp.spurenPolys += static_cast<long long>(bild.marken.size());
        sp.spurenMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    // Die Serverbilder, deren Zeit in dieses Clientbild faellt.
    const long long von = (t - 1) * kSaberClientTaktMs;
    const long long bis = t * kSaberClientTaktMs;
    sp.bilder[t] = std::move(bild);
    for (long long s = von / kSaberServerTaktMs; s * kSaberServerTaktMs <= bis; ++s) {
        const long long sMs = s * kSaberServerTaktMs;
        if (sMs <= von) {
            continue;
        }
        const long long c = sMs / kSaberClientTaktMs;   // das letzte Clientbild <= Serverzeit
        const auto cb = sp.bilder.find(c);
        if (cb != sp.bilder.end()) {
            serverSchritt(sp, s, cb->second, welt);
        }
    }
    // Die Lagen braucht nur der Server (Bilder t-1 und t) - aeltere Bilder
    // behalten Zeichenlaengen, Ereignisse und Marken.
    const auto alt = sp.bilder.find(t - 2);
    if (alt != sp.bilder.end()) {
        std::vector<SaberFigur>().swap(alt->second.figuren);
    }
}

// Den laufenden Stand wegwerfen, die Schnappschuesse behalten (Sprung).
void standVergessen(SaberSpeicher& sp) {
    ++sp.erzeugung;
    sp.gueltig = false;
    sp.letzter = LLONG_MIN;
    sp.bilder.clear();
    sp.klingen.clear();
    sp.wandFiguren.clear();
    sp.server.clear();
    sp.klirren.clear();
}

void allesVergessen(SaberSpeicher& sp) {
    standVergessen(sp);
    sp.schnapp.clear();
}

// Nach einem Sprung: vom letzten Schnappschuss <= grenze weiterrechnen (oder
// vom Anfang der Szene). Liefert das erste zu rechnende Clientbild.
long long neuAufsetzen(SaberSpeicher& sp, long long grenze) {
    standVergessen(sp);
    auto it = sp.schnapp.upper_bound(grenze);
    if (it == sp.schnapp.begin()) {
        return 0;
    }
    --it;
    sp.klingen = it->second.klingen;
    sp.wandFiguren = it->second.wandFiguren;
    sp.server = it->second.server;
    sp.bilder[it->first] = it->second.bild;
    return it->first + 1;
}

void rechneBis(double ms) {
    // Nur fuer Vorher-Proben (Selbsttest "Bugs erst nachstellen"):
    // BHED_SABER_AUS=1 laesst alles weg, was dieses Paket rechnet - die
    // Vorschau verhaelt sich dann wie vor rc601 (keine Kuerzung, keine
    // Funken, kein Klirren).
    static const bool aus = std::getenv("BHED_SABER_AUS") != nullptr;
    if (aus || !bereit()) {
        return;
    }
    SaberSpeicher& sp = speicher();
    const std::uint64_t key = speicherSchluessel();
    if (!sp.gueltig || key != sp.schluessel) {
        allesVergessen(sp);
        sp.schluessel = key;
    }
    const long long ziel = static_cast<long long>(std::floor(std::max(ms, 0.0) / kSaberClientTaktMs));
    // Schon gerechnet - auch nach einem Sprung zurueck, solange das Fenster der
    // Brandspuren (10 s) noch ganz im Speicher steht.
    const long long fensterVon = std::max(0LL, ziel - kSpurFensterTakte);
    if (sp.gueltig && ziel <= sp.letzter && sp.bilder.count(ziel) != 0U && sp.bilder.begin()->first <= fensterVon) {
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    long long von = 0;
    if (sp.gueltig && ziel > sp.letzter && ziel - sp.letzter <= kVorlaufTakte) {
        von = sp.letzter + 1;
    } else {
        von = neuAufsetzen(sp, ziel - kSpurFensterTakte - 1);
    }
    for (long long t = von; t <= ziel; ++t) {
        clientSchritt(sp, t);
        ++sp.takte;
        if (t % kSchnappTakte == 0 && sp.schnapp.count(t) == 0U) {
            SaberSpeicher::Schnappschuss& s = sp.schnapp[t];
            s.klingen = sp.klingen;
            s.wandFiguren = sp.wandFiguren;
            s.server = sp.server;
            s.bild = sp.bilder[t];
        }
    }
    sp.gueltig = true;
    sp.letzter = ziel;
    // Alte Bilder weg - die Brandspuren brauchen 10 s, der Effektrueckblick 1 s.
    while (!sp.bilder.empty() && sp.bilder.begin()->first < ziel - kVorlaufTakte - 2) {
        sp.bilder.erase(sp.bilder.begin());
    }
    // Klirren so lange wie die Bilder (vorher: 3 s hinter ms - nach einem Sprung
    // zurueck fehlten dann Klirren, Effekt und Blitz).
    const double aeltestesMs =
        sp.bilder.empty() ? ms : static_cast<double>(sp.bilder.begin()->first * kSaberClientTaktMs);
    while (!sp.klirren.empty() &&
           static_cast<double>(sp.klirren.begin()->first * kSaberServerTaktMs) < aeltestesMs - kKlirrenBehaltenMs) {
        sp.klirren.erase(sp.klirren.begin());
    }
    sp.rechenMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

const efx::Effect* saberEffekt(const std::string& name) { return name.empty() ? nullptr : effectByName(name); }

unsigned saatVon(double ms, const float ort[3]) {
    std::uint64_t h = saberSaat(static_cast<std::uint64_t>(ms * 8.0), static_cast<std::uint64_t>(ort[0] * 16.0F),
                                static_cast<std::uint64_t>(ort[1] * 16.0F), static_cast<std::uint64_t>(ort[2] * 16.0F));
    return static_cast<unsigned>(h ^ (h >> 32)) | 1U;
}

// Die Klangprimitive eines Effekts (FxScheduler.cpp:1954 ff. MD): fest am Ort,
// FX_SND_LESS_ATTENUATION auf CHAN_LESS_ATTEN, sonst CHAN_AUTO.
void effektKlaenge(const std::string& name, const float ort[3], double ms, unsigned saat, bool snapshot) {
    const efx::Effect* fx = saberEffekt(name);
    if (fx == nullptr) {
        return;
    }
    for (const efx::Primitive& pr : fx->primitives) {
        if (pr.type != efx::PrimitiveType::Sound || pr.sounds.empty()) {
            continue;
        }
        const std::string& datei = pr.sounds[saat % pr.sounds.size()];
        const sound::Kanal k = ((pr.spawnFlags & efx::kSpawnSoundLessAttenuation) != 0U) ? sound::Kanal::LessAtten
                                                                                          : sound::Kanal::Auto;
        (void)spieleRaumKlang(datei, ort, k, ms, snapshot);
    }
}

// Was die Zeichnung in diesem Bild an Klingen gezeichnet hat (Selbsttest).
struct GezeichnetSpeicher {
    int bild = -1;
    std::vector<SaberGezeichnet> liste;
};
GezeichnetSpeicher& gezeichnet() {
    static GezeichnetSpeicher g;
    return g;
}

// --- Der Markenspeicher zum Zeitpunkt (rc601 saber review) -----------------
//
// Das Spiel fuehrt EINEN Speicher (cg_marks.cpp) und ruft je Bild erst
// CG_AllocMark (beim Zeichnen der Klingen) und dann CG_AddMarks. Hier wird er
// aus den Marken der Clientbilder nachgespielt: beim Abspielen Bild fuer Bild
// weiter, nach einem Sprung (oder zurueck) neu aus den letzten 10 s. Das
// ergibt denselben Inhalt: was vor dem Fenster entstand, ist nach 10 s fort
// (CG_AddMarks), und solange es lebte, fiel beim Vollwerden zuerst es selbst
// (es ist das aelteste) - die Marken im Fenster trifft es nicht anders.
struct SpurAnzeige {
    bool gueltig = false;
    std::uint64_t schluessel = 0;
    long long erzeugung = -1;
    long long t = LLONG_MIN;
    SpurSpeicher speicher;
};
SpurAnzeige& spurAnzeige() {
    static SpurAnzeige a;
    return a;
}
const SpurSpeicher* spurenStand(long long ziel) {
    SpurAnzeige& a = spurAnzeige();
    const SaberSpeicher& sp = speicher();
    if (!sp.gueltig || ziel < 0) {
        return nullptr;
    }
    const bool weiter = a.gueltig && a.schluessel == sp.schluessel && a.erzeugung == sp.erzeugung && ziel >= a.t &&
                        ziel - a.t <= kSpurFensterTakte;
    if (!weiter) {
        a.speicher = SpurSpeicher{};
        a.t = ziel - kSpurFensterTakte - 1;
        a.schluessel = sp.schluessel;
        a.erzeugung = sp.erzeugung;
        a.gueltig = true;
    }
    for (long long t = a.t + 1; t <= ziel; ++t) {
        const auto it = sp.bilder.find(t);
        if (it != sp.bilder.end()) {
            for (const SpurPoly& p : it->second.marken) { a.speicher.neu() = p; }
        }
        a.speicher.bild(static_cast<int>(t * kSaberClientTaktMs));   // CG_AddMarks
    }
    a.t = ziel;
    return &a.speicher;
}
long long taktVon(double ms) { return static_cast<long long>(std::floor(std::max(ms, 0.0) / kSaberClientTaktMs)); }

}  // namespace

// =============================================================================
// Oeffentlich (app_internal.h, "rc601 saber")
// =============================================================================

void saberKarteGeladen() {
    // rc601 integ: Karte und Meldung kommen von kollisionLaden (Wege).
    allesVergessen(speicher());
}

void saberRechneBis(double ms) { rechneBis(ms); }

std::vector<EffectInstance> saberEffekteBei(double nowMs) {
    std::vector<EffectInstance> aus;
    rechneBis(nowMs);
    const SaberSpeicher& sp = speicher();
    if (!sp.gueltig) {
        return aus;
    }
    const long long von = static_cast<long long>(std::floor((nowMs - kEffektRueckblickMs) / kSaberClientTaktMs));
    const long long bis = static_cast<long long>(std::floor(nowMs / kSaberClientTaktMs));
    for (auto it = sp.bilder.lower_bound(von); it != sp.bilder.end() && it->first <= bis; ++it) {
        for (const SaberWandEreignis& e : it->second.ereignisse) {
            if (e.art != SaberWandEreignis::Art::Funken && e.art != SaberWandEreignis::Art::Kochen) {
                continue;
            }
            const efx::Effect* fx = saberEffekt(e.name);
            if (fx == nullptr) {
                continue;
            }
            // theFxScheduler.PlayEffect( name, origin, normal ): die Achse
            // aus der Flaechennormale; saber/boil ohne Normale (0,0,1).
            EffectInstance inst;
            inst.effect = fx;
            for (int i = 0; i < 3; ++i) {
                inst.origin[i] = e.ort[i];
                inst.normal[i] = e.normale[i];
            }
            inst.startMs = e.ms;
            inst.seed = saatVon(e.ms, e.ort);
            aus.push_back(inst);
        }
    }
    const long long sVon = static_cast<long long>(std::floor((nowMs - kEffektRueckblickMs) / kSaberServerTaktMs));
    for (auto it = sp.klirren.lower_bound(sVon); it != sp.klirren.end(); ++it) {
        if (static_cast<double>(it->first * kSaberServerTaktMs) > nowMs) {
            break;
        }
        for (const SaberKlirren& k : it->second) {
            const efx::Effect* fx = saberEffekt(k.effekt);
            if (fx == nullptr) {
                continue;
            }
            // G_PlayEffect( id, position, normal ) mit dem geloeschten
            // saberHitNormal: die Achse ist (0,0,0) - MakeNormalVectors eines
            // Nullvektors, Versaetze und Geschwindigkeiten fallen in sich
            // zusammen (cg_event.cpp:822 OJ, FxScheduler.cpp:1494 ff.).
            EffectInstance inst;
            inst.effect = fx;
            for (int i = 0; i < 3; ++i) {
                inst.origin[i] = k.ort[i];
                inst.normal[i] = k.normale[i];
            }
            inst.startMs = k.ms;
            inst.seed = saatVon(k.ms, k.ort);
            aus.push_back(inst);
        }
    }
    return aus;
}

void saberTonEreignisse(double vonMs, double bisMs) {
    if (bisMs <= vonMs) {
        return;
    }
    rechneBis(bisMs);
    const SaberSpeicher& sp = speicher();
    if (!sp.gueltig) {
        return;
    }
    const long long von = static_cast<long long>(std::floor(vonMs / kSaberClientTaktMs));
    for (auto it = sp.bilder.lower_bound(von); it != sp.bilder.end(); ++it) {
        const double tMs = static_cast<double>(it->first * kSaberClientTaktMs);
        if (tMs > bisMs) {
            break;
        }
        if (tMs <= vonMs) {
            continue;
        }
        for (const SaberWandEreignis& e : it->second.ereignisse) {
            if (e.art == SaberWandEreignis::Art::Klang) {
                // saberhitwall/saberstrikewall: cgi_S_StartSound( lerpOrigin,
                // clientNum, CHAN_ITEM ); hitwater: an der Stelle, CHAN_AUTO.
                // Beide direkt aus cgame - kein Snapshot.
                const bool wasser = e.name.find("hitwater") != std::string::npos;
                if (!wasser && e.figur >= 0 && static_cast<std::size_t>(e.figur) < g_app->scene.actors.size()) {
                    // rc601 saber review: CHAN_ITEM auf der Entity - ein neuer
                    // Klang schneidet den vorigen derselben Figur ab
                    // (S_PickChannel); fester Ort (origin gegeben).
                    KlangWunsch w;
                    w.datei = e.name;
                    w.quelle = "saberhitwall";
                    w.kanal = sound::Kanal::Item;
                    for (int i = 0; i < 3; ++i) { w.ort[i] = e.ort[i]; }
                    w.schluessel = tonKanalSchluessel(g_app->scene.actors[static_cast<std::size_t>(e.figur)].name,
                                                      sound::Kanal::Item);
                    w.abMs = (e.ms < g_app->audioUpTo) ? std::max(0.0, g_app->playMs - e.ms) : 0.0;
                    (void)tonStarte(w);
                    continue;
                }
                (void)spieleRaumKlang(e.name, e.ort, wasser ? sound::Kanal::Auto : sound::Kanal::Item, e.ms, false);
            } else if (e.art == SaberWandEreignis::Art::Funken || e.art == SaberWandEreignis::Art::Kochen) {
                effektKlaenge(e.name, e.ort, e.ms, saatVon(e.ms, e.ort), false);
            }
        }
    }
    const long long sVon = static_cast<long long>(std::floor(vonMs / kSaberServerTaktMs));
    for (auto it = sp.klirren.lower_bound(sVon); it != sp.klirren.end(); ++it) {
        const double sMs = static_cast<double>(it->first * kSaberServerTaktMs);
        if (sMs > bisMs) {
            break;
        }
        if (sMs <= vonMs) {
            continue;
        }
        for (const SaberKlirren& k : it->second) {
            // G_Sound( ent, ... ): Temp-Entity EV_GENERAL_SOUND am Ursprung
            // des Angreifers (g_utils.cpp:1162 OJ) - nur im Snapshot.
            (void)spieleRaumKlang(k.klang, k.klangOrt, sound::Kanal::Auto, k.ms, true);
            // Der Effekt kommt als EV_PLAY_EFFECT ueber den Snapshot.
            effektKlaenge(k.effekt, k.ort, k.ms, saatVon(k.ms, k.ort), true);
        }
    }
}

bool saberKlingeZeichnen(int figur, int fach, int klinge, const float wA[3], float wB[3], float laengeSzene) {
    if (figur < 0 || fach < 0) {
        return true;
    }
    const SaberSpeicher& sp = speicher();
    bool zeichnen = true;
    if (sp.gueltig) {
        const long long t = static_cast<long long>(std::floor(std::max(g_app->playMs, 0.0) / kSaberClientTaktMs));
        const auto b = sp.bilder.find(t);
        if (b != sp.bilder.end()) {
            const int key = klingenSchluessel(figur, fach, klinge);
            for (const auto& z : b->second.zeichen) {
                if (z.first != key) {
                    continue;
                }
                if (z.second < 0.0F) {
                    zeichnen = false;
                } else if (laengeSzene > 0.0F) {
                    // CG_DoSaber( org, axis, length ): die Laenge VOR dem
                    // Kuerzen dieses Bildes, samt Massstab der Figur.
                    const float f = z.second / laengeSzene;
                    for (int i = 0; i < 3; ++i) { wB[i] = wA[i] + (wB[i] - wA[i]) * f; }
                }
                break;
            }
        }
    }
    GezeichnetSpeicher& g = gezeichnet();
    const int bild = ImGui::GetFrameCount();
    if (g.bild != bild) {
        g.bild = bild;
        g.liste.clear();
    }
    if (zeichnen) {
        SaberGezeichnet e;
        e.figur = figur;
        e.fach = fach;
        e.klinge = klinge;
        for (int i = 0; i < 3; ++i) {
            e.wurzel[i] = wA[i];
            e.spitze[i] = wB[i];
        }
        g.liste.push_back(e);
    }
    return zeichnen;
}

float saberZeichenLaengeBei(int figur, int fach, int klinge, double ms, float laengeSzene) {
    const SaberSpeicher& sp = speicher();
    if (figur < 0 || fach < 0 || !sp.gueltig) {
        return laengeSzene;
    }
    const long long t = static_cast<long long>(std::floor(std::max(ms, 0.0) / kSaberClientTaktMs));
    const auto b = sp.bilder.find(t);
    if (b == sp.bilder.end()) {
        return laengeSzene;
    }
    const int key = klingenSchluessel(figur, fach, klinge);
    for (const auto& z : b->second.zeichen) {
        if (z.first == key) {
            return z.second < 0.0F ? 0.0F : z.second;
        }
    }
    return laengeSzene;
}

// --- CG_SaberClashFlare (cg_view.cpp:2028 ff. MD, 1520 ff. OJ) ---------------
//
// Das Spiel merkt sich EINEN Blitz: g_saberFlashTime = level.time - 50 und
// g_saberFlashPos beim letzten Klirren ohne noClashFlare (wp_saber.cpp:9353
// MD). Gezeichnet wird er 150 ms lang ab dieser Zeit (also 100 ms ab dem
// Klirren), als 2D-Bild "gfx/effects/saberFlare" an der Bildschirmstelle des
// Treffers (CG_WorldCoordToScreenCoord, auf ganze Punkte des 640x480-Schirms
// abgeschnitten), Farbe 0.8 - nicht, wenn er hinter der Blickrichtung liegt
// (DotProduct(dif, viewaxis[0]) < 0.2, dif NICHT normiert) oder eine Wand
// dazwischen steht (CG_Trace mit CONTENTS_SOLID). Groesse v*600, v = (1 - t/150)
// * ((1 - len/800) * 2 + 0.35), len hoechstens 800. Movie Duels rechnet die
// Breite mit widthRatioCoef (640*H / (480*B), ein Quadrat auf jedem
// Seitenverhaeltnis), OpenJK nicht (gestreckt auf Breitbild).
//
// rc601 saber review 2c: CG_Draw2D zeichnet ihn NACH den Kinobalken ("still
// draw the saber clash flare, but nothing else", cg_draw.cpp:10204 MD, 3964
// OJ) - also als 2D-Bild ueber der Ansicht, nicht mehr als Viereck im
// Effektnetz unter den Balken.
namespace {
struct BlitzAnsicht {
    bool da = false;
    Camera kam;
    int w = 0;
    int h = 0;
    double ms = 0.0;
};
std::array<BlitzAnsicht, 2>& blitzAnsichten() {
    static std::array<BlitzAnsicht, 2> a;
    return a;
}
void* blitzTextur() {
    // Neu anlegen nach destroyTextures (Geraeteverlust, Wechsel auf OpenGL).
    static bool versucht = false;
    static void* tex = nullptr;
    static unsigned stand = 0;
    if (versucht && stand != render::texturStand()) {
        versucht = false;
        tex = nullptr;
    }
    if (!versucht) {
        versucht = true;
        stand = render::texturStand();
        TextureSet::Tex t;
        if (loadTextureFor("gfx/effects/saberFlare", t) && !t.empty() && t.width > 0 && t.height > 0) {
            tex = render::createTexture(t.rgba.data(), t.width, t.height);
        }
    }
    return tex;
}
}  // namespace

void saberBlitzAnsicht(int ansicht, const Camera* kam, int w, int h, double nowMs) {
    if (ansicht < 0 || ansicht > 1) {
        return;
    }
    BlitzAnsicht& v = blitzAnsichten()[static_cast<std::size_t>(ansicht)];
    v.da = kam != nullptr && w > 0 && h > 0;
    if (v.da) {
        v.kam = *kam;
        v.w = w;
        v.h = h;
        v.ms = nowMs;
    }
}

void saberBlitzZeichnen2D(int ansicht, ImDrawList* dl, ImVec2 a, ImVec2 b, bool nachBalken) {
    if (ansicht < 0 || ansicht > 1 || dl == nullptr || b.x <= a.x || b.y <= a.y) {
        return;
    }
    const BlitzAnsicht& ans = blitzAnsichten()[static_cast<std::size_t>(ansicht)];
    const SaberSpeicher& sp = speicher();
    if (!ans.da || !sp.gueltig) {
        return;
    }
    const double nowMs = ans.ms;
    const SaberKlirren* letzter = nullptr;
    for (auto it = sp.klirren.rbegin(); it != sp.klirren.rend() && letzter == nullptr; ++it) {
        if (static_cast<double>(it->first * kSaberServerTaktMs) > nowMs) {
            continue;
        }
        for (const SaberKlirren& k : it->second) {
            if (k.blitz) {
                letzter = &k;   // der letzte Treffer dieses Serverbildes setzt die globale Stelle
            }
        }
    }
    if (letzter == nullptr) {
        return;
    }
    constexpr double kMaxZeit = 150.0;   // max_time
    const double t = nowMs - (letzter->ms - 50.0);
    if (t <= 0.0 || t >= kMaxZeit) {
        return;
    }
    float vorn[3];
    float rechts[3];
    float oben[3];
    ans.kam.forward(vorn);
    ans.kam.right(rechts);
    ans.kam.up(oben);
    float dif[3];
    for (int i = 0; i < 3; ++i) { dif[i] = letzter->ort[i] - ans.kam.pos[i]; }
    if (dif[0] * vorn[0] + dif[1] * vorn[1] + dif[2] * vorn[2] < 0.2F) {
        return;
    }
    {
        std::shared_ptr<const std::vector<BrushLage>> halter;
        const SaberSpurFn welt = weltFuer(nowMs, halter);
        if (welt && welt(ans.kam.pos, letzter->ort, kInhaltFest).fraction < 1.0F) {
            return;
        }
    }
    // CG_WorldCoordToScreenCoordFloat (cg_draw.cpp:7239 MD, 2798 OJ): viewaxis[1]
    // zeigt nach LINKS, daher "-" rechts.
    const float seiten = static_cast<float>(ans.w) / static_cast<float>(ans.h);
    const float py = std::tan(std::clamp(ans.kam.fovY, 1.0F, 179.0F) * 3.14159265F / 360.0F);
    const float px = py * seiten;   // tan(fov_x/2) zum Seitenverhaeltnis des Bildes
    const float z = dif[0] * vorn[0] + dif[1] * vorn[1] + dif[2] * vorn[2];
    const float links = -(dif[0] * rechts[0] + dif[1] * rechts[1] + dif[2] * rechts[2]);
    const float hoch = dif[0] * oben[0] + dif[1] * oben[1] + dif[2] * oben[2];
    const int x = static_cast<int>(320.0F - links * 320.0F / (z * px));
    const int y = static_cast<int>(240.0F - hoch * 240.0F / (z * py));
    float len = std::sqrt(dif[0] * dif[0] + dif[1] * dif[1] + dif[2] * dif[2]);   // VectorNormalize( dif )
    if (len > 800.0F) { len = 800.0F; }
    const float v = static_cast<float>(1.0 - t / kMaxZeit) * ((1.0F - len / 800.0F) * 2.0F + 0.35F);
    // cgs.widthRatioCoef = (SCREEN_WIDTH * vidHeight) / (SCREEN_HEIGHT * vidWidth) (MD)
    const float coef = saberMovieDuelsRegeln() ? (640.0F * static_cast<float>(ans.h)) / (480.0F * static_cast<float>(ans.w))
                                              : 1.0F;
    const float x0 = static_cast<float>(x) - v * 300.0F * coef;
    const float y0 = static_cast<float>(y) - v * 300.0F;
    const float bw = v * 600.0F * coef;
    const float bh = v * 600.0F;
    void* tex = blitzTextur();
    if (tex == nullptr) {
        return;   // kein Bild - das Spiel zeichnete den Ersatzshader; hier nichts
    }
    // CG_DrawPic: 640x480 auf die Ansicht (cgs.screenXScale/screenYScale)
    const float sx = (b.x - a.x) / 640.0F;
    const float sy = (b.y - a.y) / 480.0F;
    const ImVec2 p0{a.x + x0 * sx, a.y + y0 * sy};
    const ImVec2 p1{a.x + (x0 + bw) * sx, a.y + (y0 + bh) * sy};
    dl->PushClipRect(a, b, true);
    render::zeichneAdditiv(dl, tex, p0, p1, IM_COL32(204, 204, 204, 255));   // R_SetColor( 0.8, 0.8, 0.8 )
    dl->PopClipRect();
    ++g_saberBlitze;
    ++g_saberBlitze2D;
    if (nachBalken) {
        ++g_saberBlitzeNachBalken;
    }
}

// --- CG_AddMarks: die Brandspuren ins Effektnetz (rc601 saber review) -------
//
// cgi_R_AddPolyToScene( markShader, numVerts, verts ) je lebender Marke, die
// Farben aus CG_AddMarks (spurFarbe). Als Faecher wie RB_SurfacePolychain;
// Shader, Mischart, polygonOffset und cull aus den Shaderdateien
// (gfx/damage/rivetmark, gfx/effects/saberDamageGlow).
int g_saberSpurenGezeichnet = 0;
bool saberSpurenDa(double nowMs) {
    static const bool aus = std::getenv("BHED_SABER_AUS") != nullptr;
    if (aus) {
        return false;
    }
    const SpurSpeicher* s = spurenStand(taktVon(nowMs));
    return s != nullptr && s->zahl() > 0;
}
void saberSpurenAnhaengen(BspMesh& netz, double nowMs) {
    if (!saberSpurenDa(nowMs)) {
        return;
    }
    const long long ziel = taktVon(nowMs);
    const SpurSpeicher* s = spurenStand(ziel);
    static efx::Effect s_spurFx = [] {
        efx::Effect e;
        efx::Primitive pr;
        pr.shaders.push_back("gfx/damage/rivetmark");        // cgs.media.rivetMarkShader (cg_main.cpp:1535 OJ)
        pr.shaders.push_back("gfx/effects/saberDamageGlow");  // cg_players.cpp:5917 OJ, bdecal_saberglow MD
        e.primitives.push_back(pr);
        return e;
    }();
    ladeEffektShader(s_spurFx);
    const char* namen[2] = {"gfx/damage/rivetmark", "gfx/effects/saberdamageglow"};
    const std::vector<const SpurPoly*> leb = s->lebende();
    const int cgZeit = static_cast<int>(ziel * kSaberClientTaktMs);
    for (int sh = 0; sh < 2; ++sh) {
        const auto sl = g_app->effectShaders.find(namen[sh]);
        if (sl == g_app->effectShaders.end() || sl->second <= 0) {
            continue;   // kein Bild - das Spiel zeichnete den Ersatzshader; hier nichts
        }
        BspMesh::Batch bt;
        bt.lightmap = -1;
        bt.vertexColour = true;
        bt.shader = sl->second;
        bt.firstIndex = static_cast<std::uint32_t>(netz.indexes.size());
        for (const SpurPoly* p : leb) {
            if (p->shader != sh || p->zahl < 3) {
                continue;
            }
            std::uint8_t farbe[4];
            spurFarbe(*p, cgZeit, farbe);
            const auto base = static_cast<std::uint32_t>(netz.verts.size());
            float n[3];
            {
                const float a[3] = {p->xyz[1][0] - p->xyz[0][0], p->xyz[1][1] - p->xyz[0][1], p->xyz[1][2] - p->xyz[0][2]};
                const float b[3] = {p->xyz[2][0] - p->xyz[0][0], p->xyz[2][1] - p->xyz[0][1], p->xyz[2][2] - p->xyz[0][2]};
                n[0] = a[1] * b[2] - a[2] * b[1];
                n[1] = a[2] * b[0] - a[0] * b[2];
                n[2] = a[0] * b[1] - a[1] * b[0];
            }
            for (int j = 0; j < p->zahl; ++j) {
                BspVertex vx;
                for (int c = 0; c < 3; ++c) {
                    vx.xyz[c] = p->xyz[j][c];
                    vx.normal[c] = n[c];
                }
                vx.st[0] = p->st[j][0];
                vx.st[1] = p->st[j][1];
                for (int c = 0; c < 4; ++c) { vx.colour[c] = farbe[c]; }
                netz.verts.push_back(vx);
            }
            for (int j = 1; j + 1 < p->zahl; ++j) {
                netz.indexes.push_back(base);
                netz.indexes.push_back(base + static_cast<std::uint32_t>(j));
                netz.indexes.push_back(base + static_cast<std::uint32_t>(j + 1));
            }
            ++g_saberSpurenGezeichnet;
        }
        bt.numIndexes = static_cast<std::uint32_t>(netz.indexes.size()) - bt.firstIndex;
        if (bt.numIndexes > 0U) {
            netz.batches.push_back(bt);
        }
    }
}

std::vector<SpurPoly> saberSpurenBei(double ms) {
    std::vector<SpurPoly> aus;
    rechneBis(ms);
    if (const SpurSpeicher* s = spurenStand(taktVon(ms))) {
        for (const SpurPoly* p : s->lebende()) { aus.push_back(*p); }
    }
    return aus;
}

void saberVergessen() {
    allesVergessen(speicher());
    spurAnzeige().gueltig = false;
}

std::vector<SaberGezeichnet> saberGezeichnetesBild() {
    // Was im zuletzt gezeichneten Bild stand (die Ansicht zeichnet nur neu,
    // wenn sich etwas aendert - rc596 "idle").
    return gezeichnet().liste;
}

std::vector<SaberKlirren> saberKlirrenBis(double ms) {
    std::vector<SaberKlirren> aus;
    for (const auto& [s, liste] : speicher().klirren) {
        if (static_cast<double>(s * kSaberServerTaktMs) > ms) {
            break;
        }
        aus.insert(aus.end(), liste.begin(), liste.end());
    }
    return aus;
}

std::vector<SaberWandEreignis> saberWandEreignisseBis(double ms) {
    std::vector<SaberWandEreignis> aus;
    for (const auto& [t, b] : speicher().bilder) {
        if (static_cast<double>(t * kSaberClientTaktMs) > ms) {
            break;
        }
        aus.insert(aus.end(), b.ereignisse.begin(), b.ereignisse.end());
    }
    return aus;
}

SaberZaehlung saberZaehlen(double bisMs) {
    SaberZaehlung z;
    if (!bereit()) {
        return z;
    }
    SaberSpeicher& sp = speicher();
    allesVergessen(sp);
    sp.schluessel = speicherSchluessel();
    const long long probenVorher = sp.probenFiguren;
    const auto t0 = std::chrono::steady_clock::now();
    const long long ende = static_cast<long long>(std::floor(std::max(bisMs, 0.0) / kSaberClientTaktMs));
    for (long long t = 0; t <= ende; ++t) {
        clientSchritt(sp, t);
        ++z.takte;
        for (const SaberWandEreignis& e : sp.bilder[t].ereignisse) {
            z.funken += e.art == SaberWandEreignis::Art::Funken ? 1 : 0;
            z.wandKlaenge += e.art == SaberWandEreignis::Art::Klang ? 1 : 0;
        }
        // Nur die letzten Bilder behalten - hier zaehlt nur die Summe.
        while (!sp.bilder.empty() && sp.bilder.begin()->first < t - 8) {
            sp.bilder.erase(sp.bilder.begin());
        }
    }
    for (const auto& [s, liste] : sp.klirren) {
        z.klirren += static_cast<int>(liste.size());
        if (z.erstesKlirrenMs < 0.0 && !liste.empty()) {
            z.erstesKlirrenMs = static_cast<double>(s * kSaberServerTaktMs);
        }
        // rc601 integ: jedes Klirren mit Figurenstand ins Protokoll - so
        // laesst sich vergleichen, warum zwei Staende verschieden oft klirren
        // (NOTIZEN_integ, Klirren in md_dwtdl).
        const auto stand = [](int i, double ms) {
            if (i < 0 || static_cast<std::size_t>(i) >= g_app->scene.actors.size()) { return std::string("?"); }
            const Actor& a = g_app->scene.actors[static_cast<std::size_t>(i)];
            const ActorState st = a.at(ms);
            char b[240];
            std::snprintf(b, sizeof(b), "%s (%.1f %.1f %.1f, Gier %.1f, %s/%s)", a.name.c_str(),
                          static_cast<double>(st.pos[0]), static_cast<double>(st.pos[1]), static_cast<double>(st.pos[2]),
                          static_cast<double>(st.angles[1]), st.animation.c_str(), st.torsoAnimation.c_str());
            return std::string(b);
        };
        for (const SaberKlirren& k : liste) {
            diag::detail("Schwert-Klirren bei " + std::to_string(static_cast<int>(k.ms)) + " ms: " + stand(k.figur, k.ms) +
                         " gegen " + stand(k.gegner, k.ms));
        }
    }
    z.figurenProben = sp.probenFiguren - probenVorher;
    z.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    allesVergessen(sp);
    return z;
}

std::string saberLeistung() {
    const SaberSpeicher& sp = speicher();
    char t[240];
    std::snprintf(t, sizeof(t),
                  "Schwertkontakte: %lld Clientbilder in %.1f ms (%.3f ms je Bild), %lld Figurenproben, "
                  "Brandspuren %lld Marken in %.1f ms",
                  sp.takte, sp.rechenMs, sp.takte > 0 ? sp.rechenMs / static_cast<double>(sp.takte) : 0.0,
                  sp.probenFiguren, sp.spurenPolys, sp.spurenMs);
    return t;
}

}  // namespace bhed::gui
