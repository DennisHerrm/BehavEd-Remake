// schwerkraft.cpp - Paket bewegung (rc601): NPCs fallen, steigen Stufen und
// stehen auf dem Boden wie in Pmove.
//
// Gemeldet: "NPCs aren't affected by gravity."
//
// Warum und was, steht in bhed/scene.h bei schwerkraftEinrechnen. Hier die
// Rechnung: je Figur ein Durchgang ueber die Pmove-Takte (50 ms), der nur
// Z fuehrt - X/Y kommen weiter aus den Schritten der Szene (Weg, Sprung,
// ROFF). Das Ergebnis steht in Actor::hoehe; Actor::at mittelt dazwischen
// linear, wie cgame zwischen zwei Bildern eines NPC.
//
// Quellen: MovieDuels-SJE-SP-Rend2 code/game (Zeilen dort; OpenJK ist an
// diesen Stellen gleich, ausser es steht dabei).
#include "bhed/scene.h"
#include "bhed/bspgeo.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bhed {

namespace {

// PM_GroundTrace sucht den Boden 0,25 unter dem Kasten (bg_pmove.cpp:7423).
// Liegt er naeher, steht die Figur; sonst ist sie in der Luft.
constexpr float kBodenSpalt = 0.25F;
// Der steilste begehbare Hang: MIN_WALK_NORMAL 0.7 (bg_public.h:90), also
// tan(acos(0.7)) = 1.0202. Bergab klappt PM_WalkMove Richtung und
// Geschwindigkeit auf die Bodenebene (bg_pmove.cpp:4340-4341, 4522) - die
// Figur sinkt in einem Takt hoechstens so viel, wie sie waagrecht vorankommt
// mal diese Zahl, und PM_GroundTrace findet den Boden wieder. Was steiler
// abfaellt (eine Stufe, eine Kante), laesst sie kurz fallen - so geht sie
// auch im Spiel Treppen hinab ("if we didn't do the trace, the player would
// be backflipping down staircases", PM_GroundTraceMissed).
constexpr float kHangGrenze = 1.0202F;
// So tief wird unter den Fuessen nach Boden gesucht. Findet sich keiner,
// bleibt die Figur auf ihrer Hoehe - eher fehlt dann behaved ein Stueck
// Boden (ein unsichtbarer Clip-Brush, ein Modell), als dass das Spiel sie
// ins Nichts fallen liesse.
constexpr float kSuchTiefe = 4096.0F;
// NPC_Begin ruft ClientThink sofort, "to drop exactly to the floor"
// (NPC_spawn.cpp:2048-2061), mit leerem ucmd: serverTime 0 wird auf
// level.time - 1000 gekappt (g_active.cpp:10089-10091), commandTime ist 0 -
// pml.msec wird auf 200 gekappt (bg_pmove.cpp:33781-33788). Ein Schritt von
// 200 ms gleich beim Spawnen.
constexpr double kSpawnSchrittMs = 200.0;
// Das erste NPC_Think kommt FRAMETIME + Q_irand(0, 100) spaeter
// (NPC_spawn.cpp:2016) - zufaellig 100 bis 200 ms; hier die Mitte. Sein
// ucmd traegt serverTime = level.time - 50 (NPC.cpp:2147), commandTime
// steht noch bei level.time - 1000 des Spawns: wieder 200 ms. Danach 50.
constexpr double kErstesDenkenMs = 150.0;
// rc601 (bewegung, Kartenstart): NPC_Begin einer beim Laden der Karte
// gespawnten Figur, relativ zum Skriptanfang (siehe backen, Zweig !imAblauf):
// 1550 gegen 1750 Spielzeit.
constexpr double kKartenSpawnMs = -200.0;
// Q3_SetBState: wer BS_NOCLIP verlaesst, wird 0,125 gehoben ("need to rise
// up out of the floor after noclipping", Q3_Interface.cpp:2654-2659).
constexpr float kNoclipHeben = 0.125F;

struct Pmove {
    float z = 0.0F;
    float vz = 0.0F;
    bool amBoden = false;
    // Der zuletzt gefundene Boden. Neu gefragt wird, wenn die Figur sich
    // bewegt, hoeher steht als bei der Frage, gesprungen ist oder der Boden
    // selbst sich bewegen kann.
    bool gefragt = false;
    bool da = false;
    float boden = 0.0F;
    float fx = 0.0F;
    float fy = 0.0F;
    float fOben = 0.0F;
    // rc601 (bewegung, R6): die Stufe dieser Figur (PM_StepSlideMove
    // step_size je Klasse) und maxs[2] (fuer die Decke, R8).
    float stufe = kStufe;
    float oben = 40.0F;
};

bool bodenFragen(const SchwerkraftHilfe& h, Pmove& p, float x, float y, float hb, double t, float& boden,
                 bool& beweglich) {
    const float oben = p.z - kFussTiefe + p.stufe;   // PM_StepSlideMove: up[2] += step_size (R6: je Klasse)
    beweglich = h.beweglich && h.beweglich(x, y, t);
    const bool neu = !p.gefragt || std::fabs(x - p.fx) > 0.05F || std::fabs(y - p.fy) > 0.05F ||
                     oben > p.fOben + 0.01F || beweglich;
    if (neu) {
        p.da = bodenUnter(h.strahl, x, y, oben, oben - p.stufe - kSuchTiefe, hb, t, p.boden);
        p.gefragt = true;
        p.fx = x;
        p.fy = y;
        p.fOben = oben;
    }
    boden = p.boden;
    return p.da;
}

// Ein Pmove-Schritt von dt Sekunden, nur in Z. `waagrecht`: wie weit die
// Figur seit dem letzten Takt in X/Y kam.
void pmoveSchritt(const SchwerkraftHilfe& h, Pmove& p, float x, float y, float waagrecht, float hb, double t,
                  float dt, float g) {
    if (g < 0.0F) {
        // rc601 (bewegung, R8): Schwerkraft unter 0. PM_WalkMove
        // (bg_pmove.cpp:4259-4273 MD): am Boden velocity[2] -= gravity *
        // frametime und ab in die Luft (PM_AirMove); dort PM_StepSlideMove(1)
        // -> PM_SlideMove mit dem Trapez (end_velocity[2] -= gravity *
        // frametime) - die Figur steigt. Stoesst der Kopf (maxs[2]) an eine
        // Flaeche, bleibt sie dort (PM_ClipVelocity an der Decke nimmt vz),
        // SURFACE_CLIP_EPSILON 0,125 darunter.
        if (p.amBoden) {
            p.vz -= g * dt;
            p.amBoden = false;
        }
        const float vNeu = p.vz - g * dt;
        const float zNeu = p.z + 0.5F * (p.vz + vNeu) * dt;
        const float kopfAlt = p.z + p.oben;
        const float kopfNeu = zNeu + p.oben;
        std::array<float, 3> treffer{};
        if (kopfNeu > kopfAlt && h.strahl && h.strahl({x, y, kopfNeu}, {x, y, kopfAlt}, t, treffer)) {
            p.z = treffer[2] - p.oben - 0.125F;
            p.vz = 0.0F;
            p.gefragt = false;
            return;
        }
        p.z = zNeu;
        p.vz = vNeu;
        p.gefragt = false;
        return;
    }
    float boden = 0.0F;
    bool beweglich = false;
    if (!bodenFragen(h, p, x, y, hb, t, boden, beweglich)) {
        p.amBoden = false;
        p.vz = 0.0F;
        return;
    }
    const float fuss = p.z - kFussTiefe;
    // Steckt der Kasten im Boden und bewegt sich nicht, bleibt er stecken:
    // PM_GroundTrace meldet allsolid, PM_CorrectAllSolid rueckt hoechstens
    // eine Einheit (bg_pmove.cpp:6323 ff.), PM_SlideMove bewegt nichts.
    // Gestiegen wird nur beim Gehen (PM_StepSlideMove, wenn die Bewegung
    // anstoesst) - oder ein Mover schiebt (G_MoverPush).
    if (boden > fuss + kBodenSpalt && waagrecht < 0.01F && !beweglich) {
        p.vz = 0.0F;
        p.amBoden = false;
        return;
    }
    // Am Boden: hoechstens eine Stufe hinauf (der Strahl beginnt STEPSIZE
    // ueber den Fuessen) oder einen begehbaren Hang hinab; auf einem Boden,
    // der sich bewegt, faehrt sie mit hinab, wie G_MoverPush den mitnimmt,
    // der darauf steht (bis eine Stufe je Takt). In der Luft nur, was
    // PM_GroundTrace in 0,25 findet.
    float halt = kBodenSpalt;
    if (p.amBoden) {
        halt = std::max(kBodenSpalt, kHangGrenze * waagrecht);
        if (beweglich) {
            halt += p.stufe;
        }
    }
    if (boden >= fuss - halt) {
        p.z = boden + kFussTiefe;
        p.vz = 0.0F;
        p.amBoden = true;
        return;
    }
    // PM_SlideMove (bg_slidemove.cpp:97-99): end_velocity[2] -= gravity *
    // frametime; bewegt wird mit dem Mittel aus alter und neuer.
    //
    // rc601 (bewegung, Review): ps.gravity statt fest 800 - g_gravity der
    // Karte oder SET_GRAVITY der Figur (siehe backen). Bei 0 zieht nichts
    // (eine Figur ohne Fallgeschwindigkeit bleibt genau, wo sie ist; in der
    // Luft bremst PM_Friction nicht - es wirkt nur am Boden). Unter 0: oben.
    const float vNeu = p.vz - g * dt;
    const float zNeu = p.z + 0.5F * (p.vz + vNeu) * dt;
    if (zNeu - kFussTiefe <= boden) {
        // Gelandet: PM_SlideMove bleibt am Boden haengen, PM_GroundTrace
        // setzt groundEntityNum (PM_CrashLand).
        p.z = boden + kFussTiefe;
        p.vz = 0.0F;
        p.amBoden = true;
        return;
    }
    p.z = zNeu;
    p.vz = vNeu;
    p.amBoden = false;
}

double anteil(const ActorStep& w, double t) {
    const double span = w.endMs - w.startMs;
    if (span <= 0.0) {
        return t >= w.startMs ? 1.0 : 0.0;
    }
    return std::clamp((t - w.startMs) / span, 0.0, 1.0);
}

// --- rc601 (bewegung, Review): Ort, BS_NOCLIP und "fort" je Takt ---------
//
// backen fragte dafuer je Figur und Takt das ganze Actor::at - Animationen,
// Waffen, Blickziel, Drehung. Das war der groesste Posten der Schwerkraft
// (40 Figuren ueber 75 s: 46 ms je Durchgang; prepareScene rechnet bis zu
// zwei, nach jeder Aenderung im Missions-Tab). Hier ein Durchlauf durch die
// Schritte im Gleichschritt mit den Takten, mit GENAU der Rechnung von
// Actor::at (scene.cpp):
//   * at(ms) nimmt die Schritte in ihrer Reihenfolge bis zum ersten, der nach
//     ms beginnt. Die Takte werden nur spaeter, also waechst dieses Stueck
//     nur - derselbe Schnitt wie in at().
//   * Den Ort setzen nur Teleport, WalkTo und Roff mit gelesener Bahn, jeder
//     alle drei Achsen - es gilt der letzte. move (Glide) tut auf einer Figur
//     nichts (Lerp2Pos, "NOT a mover"), und gebacken werden nur Figuren.
//   * BS_NOCLIP: der letzte Verhalten-Schritt; fort: ein Gone.
// SchwerkraftHilfe::nachActorAt rechnet zum Vergleich ueber Actor::at; die
// Probe "Review P1" (tests/bewegungtest.cpp) haelt beide Bit fuer Bit gleich.
struct OrtStand {
    float pos[3]{};
    bool bsNoclip = false;
    bool removed = false;
};

class OrtsLauf {
public:
    OrtsLauf(const Actor& a, bool nachActorAt) : a_(a), nachAt_(nachActorAt) {}

    [[nodiscard]] OrtStand bei(double ms) {
        OrtStand o;
        if (nachAt_) {
            const ActorState st = a_.at(ms);
            for (int k = 0; k < 3; ++k) { o.pos[k] = st.pos[k]; }
            o.bsNoclip = st.verhalten == "BS_NOCLIP";
            o.removed = st.removed;
            return o;
        }
        while (i_ < a_.steps.size() && !(a_.steps[i_].startMs > ms)) {
            const ActorStep& s = a_.steps[i_++];
            switch (s.kind) {
                case ActorStep::Kind::Teleport:
                case ActorStep::Kind::WalkTo:
                    ort_ = &s;
                    break;
                case ActorStep::Kind::Roff:
                    if (s.roff && s.roffBahn) { ort_ = &s; }
                    break;
                case ActorStep::Kind::Verhalten:
                    verhalten_ = &s.text;
                    break;
                case ActorStep::Kind::Gone:
                    removed_ = true;
                    break;
                default:
                    break;
            }
        }
        o.bsNoclip = verhalten_ != nullptr && *verhalten_ == "BS_NOCLIP";
        o.removed = removed_;
        if (ort_ == nullptr) {
            for (int k = 0; k < 3; ++k) { o.pos[k] = a_.start[k]; }
        } else if (ort_->kind == ActorStep::Kind::Teleport) {
            for (int k = 0; k < 3; ++k) { o.pos[k] = ort_->to[k]; }
        } else if (ort_->kind == ActorStep::Kind::WalkTo) {
            // Wortgleich mit Actor::at, Kind::WalkTo (rc601 Wege: die Bahn
            // je Denkbild, sonst linear - beide ueber wegOrtBei).
            wegOrtBei(*ort_, ms, o.pos);
        } else {
            // Wie Actor::at, Kind::Roff.
            const RoffStand r = roffStand(*ort_->roff, *ort_->roffBahn, ms - ort_->startMs, ort_->roffFigur);
            for (int k = 0; k < 3; ++k) { o.pos[k] = r.ort[k]; }
        }
        return o;
    }

private:
    const Actor& a_;
    bool nachAt_ = false;
    std::size_t i_ = 0;
    const ActorStep* ort_ = nullptr;
    const std::string* verhalten_ = nullptr;
    bool removed_ = false;
};

std::vector<float> backen(const Actor& a, const SchwerkraftHilfe& h, float kartenSchwerkraft,
                          const std::vector<std::pair<double, float>>& kartenAb) {
    const std::size_t n = static_cast<std::size_t>(std::max(0.0, h.endeMs) / kPmoveTaktMs) + 2U;
    std::vector<float> hz(n, a.start[2]);
    const float hb = a.halbBreite >= 0.0F ? a.halbBreite : kNpcHalbBreite;
    std::vector<const ActorStep*> spruenge;
    std::vector<const ActorStep*> bahnen;
    std::vector<const ActorStep*> wege;
    for (const ActorStep& st : a.steps) {
        if (st.kind == ActorStep::Kind::Teleport && st.echterSprung) {
            spruenge.push_back(&st);
        } else if (st.kind == ActorStep::Kind::Roff && st.roffFigur && st.roff && st.roffBahn) {
            bahnen.push_back(&st);
        } else if (st.kind == ActorStep::Kind::WalkTo) {
            wege.push_back(&st);
        }
    }
    std::stable_sort(spruenge.begin(), spruenge.end(),
                     [](const ActorStep* x, const ActorStep* y) { return x->startMs < y->startMs; });
    // rc601 (bewegung, Review): ps.gravity. Erst g_gravity der Karte
    // (ClientThink_real setzt es vor jedem Pmove, g_active.cpp:10115-10147),
    // ab einem SET_GRAVITY der eigene Wert (SVF_CUSTOM_GRAVITY) - wie ein
    // Sprung im Skript des Takts, gilt also ab dem naechsten Pmove.
    std::vector<std::pair<double, float>> eigen = a.eigeneSchwerkraft;
    std::stable_sort(eigen.begin(), eigen.end(),
                     [](const std::pair<double, float>& x, const std::pair<double, float>& y) { return x.first < y.first; });
    std::size_t gi = 0;
    float g = kartenSchwerkraft;
    // rc601 (bewegung, R8): g_gravity kann sich spaeter aendern
    // (target_gravity_change GLOBAL) - fuer jede Figur ohne SVF_CUSTOM_GRAVITY
    // ab dem naechsten ClientThink.
    bool eigeneAktiv = false;
    const auto weltBei = [&](double t) {
        float w = kartenSchwerkraft;
        for (const auto& [ms, wert] : kartenAb) {
            if (ms <= t + 1e-6) { w = wert; }
        }
        return w;
    };

    // Kein Pmove (client->noclip): BS_NOCLIP, oder ein JSF_AMBUSH-Jedi haengt
    // noch (NPC_spawn.cpp:665-670, 971-976: "hang").
    const auto ohnePmove = [&a](const OrtStand& st, double t) {
        const bool haengt = a.haengtAnfangs && (a.haengtBisMs < 0.0 || t < a.haengtBisMs - 0.5);
        return haengt || st.bsNoclip;
    };
    OrtsLauf ortsLauf(a, h.nachActorAt);

    // rc601 (bewegung, teil 3): "one source of truth for the height". Wo ein
    // Laufplan gilt (die Stuecke eines Weges, der Vorlauf ab Spawn/SET_ORIGIN),
    // kommt die Hoehe aus dem Plan - dort rechnet PmoveSingle mit dem Kasten
    // der Figur (PM_GroundTrace, PM_AirMove, PM_StepSlideMove). Danach geht es
    // mit dem Stand des Plans weiter; eine danach ruhende Figur bleibt, wo der
    // Plan sie abgesetzt hat.
    struct PlanZeit {
        const LaufPlan* plan = nullptr;
        double ab = 0.0;
        double bis = 0.0;
    };
    std::vector<PlanZeit> planZeiten;
    for (const ActorStep* w : wege) {
        if (w->lauf && w->lauf->hoeheGilt) { planZeiten.push_back({w->lauf.get(), w->startMs, w->endMs}); }
    }
    for (const auto& [vp, bis] : a.vorlaeufe) {
        if (vp && vp->hoeheGilt) { planZeiten.push_back({vp.get(), vp->abMs - 0.5, bis}); }
    }
    const auto planBei = [&planZeiten](double t) -> const LaufPlan* {
        const LaufPlan* gef = nullptr;
        for (const PlanZeit& z : planZeiten) {
            if (t > z.ab + 0.25 && t <= z.bis + 0.5) { gef = z.plan; }
        }
        return gef;
    };
    bool planRuhe = false;

    Pmove p;
    bool gespawnt = false;
    bool noclipVorher = false;      // Stand nach dem Skript des letzten Takts
    bool bsNoclipVorher = false;
    double naechstesPmove = 0.0;    // das erste NPC_Think nach dem Spawn
    bool langerSchritt = false;     // ... rechnet noch 200 ms
    const ActorStep* bahn = nullptr;
    float bahnVersatz = 0.0F;
    float vx = 0.0F;
    float vy = 0.0F;
    std::size_t si = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const double t = static_cast<double>(k) * kPmoveTaktMs;
        const OrtStand st = ortsLauf.bei(t);
        const float x = st.pos[0];
        const float y = st.pos[1];
        if (!gespawnt) {
            const bool imAblauf = a.erscheintMs >= 0.0;
            if (imAblauf && t < a.erscheintMs - 0.5) {
                hz[k] = st.pos[2];   // es gibt sie noch nicht
                while (si < spruenge.size() && spruenge[si]->startMs <= t + 1e-6) { ++si; }
                // rc601 (bewegung, Review): ebenso SET_GRAVITY - es gibt keine
                // Entity, die es treffen koennte.
                while (gi < eigen.size() && eigen[gi].first <= t + 1e-6) { ++gi; }
                continue;
            }
            gespawnt = true;
            p = Pmove{};
            p.stufe = a.stufe;       // rc601 (bewegung, R6)
            p.oben = a.hoeheOben;    // rc601 (bewegung, R8)
            if (!imAblauf) {
                // rc601 (bewegung, Kartenstart): beim Laden der Karte gespawnt
                // (NPC_Spawn_Go) - und NICHT laengst gelandet. Bis rc601 stand
                // hier "gilt beim Skriptanfang als gelandet". Die Engine:
                //
                //   SV_SpawnServer (sv_init.cpp:295-363 MD): sv.time = 1000,
                //     Entities spawnen; vier Bilder zu 100 ms (1000-1300),
                //     ConnectNavs, ein Bild (1400) - sv.time danach 1500.
                //   Der Spawner denkt bei level.time + START_TIME_REMOVE_ENTS
                //     + 50 = 1350 (NPC_spawn.cpp:3101), also im Bild 1400;
                //     NPC_Spawn_Do stellt NPC_Begin auf + FRAMETIME = 1500.
                //   Der Client laedt die Karte (blockiert). Das erste Com_Frame
                //     danach kappt msec auf 200 (Com_ModifyMsec, common.cpp:
                //     1426-1432): SV_Frame rechnet 1550-1700, NPC_Begin bei 1550.
                //   Erst in CL_Frame dieses Bilds schickt der Client sein erstes
                //     usercmd; das naechste Com_Frame liest es (Com_EventLoop vor
                //     SV_Frame): SV_ClientEnterWorld -> ClientBegin ->
                //     ClientSpawn -> G_UseTargets des info_player_start -> der
                //     target_scriptrunner startet das Skript, dessen erste
                //     Befehle im naechsten Bild laufen (G_RunFrame, ICARUS
                //     Update): 1750.
                //
                // NPC_Begin liegt also 200 ms vor dem Skriptanfang. Dann wie
                // eine Figur, die im Ablauf erscheint: G_CheckInSolid, ein
                // Pmove von 200 ms in NPC_Begin, das erste NPC_Think 150 ms
                // spaeter (FRAMETIME + Q_irand(0, 100), die Mitte) wieder mit
                // 200 ms, ab dem Skriptanfang alle 50 ms. Ein haengender
                // CEILING-Jedi bleibt, wo er ist.
                p.z = a.start[2];
                if (!a.haengtAnfangs) {
                    float boden = 0.0F;
                    if ((a.spawnflags & 128) == 0 &&
                        bodenUnter(h.strahl, a.start[0], a.start[1], p.z, p.z - kFussTiefe, hb, kKartenSpawnMs, boden)) {
                        p.z = boden + kFussTiefe;
                    }
                    pmoveSchritt(h, p, a.start[0], a.start[1], 0.0F, hb, kKartenSpawnMs,
                                 static_cast<float>(kSpawnSchrittMs / 1000.0), g);
                    pmoveSchritt(h, p, a.start[0], a.start[1], 0.0F, hb, kKartenSpawnMs + kErstesDenkenMs,
                                 static_cast<float>(kSpawnSchrittMs / 1000.0), g);
                    // Das Denkbild beim Skriptanfang selbst.
                    pmoveSchritt(h, p, x, y, 0.0F, hb, t, static_cast<float>(kPmoveTaktMs / 1000.0), g);
                    naechstesPmove = t;
                    langerSchritt = false;
                }
            } else {
                p.z = st.pos[2];
                if (!a.haengtAnfangs) {
                    // G_CheckInSolid (g_utils.cpp:1635-1674): der Kasten ab dem
                    // Ursprung 24 hinab; trifft er etwas, kommt der Ursprung
                    // dorthin plus 24. Nicht mit SFB_STARTINSOLID
                    // (NPC_spawn.cpp:2030-2034).
                    float boden = 0.0F;
                    if ((a.spawnflags & 128) == 0 &&
                        bodenUnter(h.strahl, x, y, p.z, p.z - kFussTiefe, hb, t, boden)) {
                        p.z = boden + kFussTiefe;
                    }
                    pmoveSchritt(h, p, x, y, 0.0F, hb, t, static_cast<float>(kSpawnSchrittMs / 1000.0), g);
                    naechstesPmove = t + kErstesDenkenMs;
                    langerSchritt = true;
                }
            }
            bsNoclipVorher = st.bsNoclip;
        } else {
            // --- Pmove dieses Takts, mit dem Stand VOR dem Skript ---------
            const ActorStep* b = nullptr;
            for (const ActorStep* r : bahnen) {
                // NPC_ApplyRoff statt ClientThink, solange next_roff_time
                // laeuft - ab dem Denkbild nach dem play-Befehl.
                if (r->startMs < t - 0.5 && t <= r->endMs + 0.5) { b = r; }
            }
            if (b != nullptr) {
                if (b != bahn) {
                    // Die Bahn rechnet ab dem AKTUELLEN Ort (G_Roff addiert auf
                    // s.pos.trBase, g_roff.cpp:596-602) - auch in Z.
                    bahn = b;
                    bahnVersatz = p.z - a.at(b->startMs).pos[2];
                }
                p.z = st.pos[2] + bahnVersatz;
                p.amBoden = false;
                p.gefragt = false;
            } else if (noclipVorher) {
                bahn = nullptr;
                // PM_NoclipMove: frei im Raum, die Linie zum Ziel wie bisher -
                // ab der AKTUELLEN Hoehe, damit am Ende das Ziel erreicht ist.
                for (const ActorStep* w : wege) {
                    if (w->startMs > t + 0.5 || w->endMs < t - kPmoveTaktMs) { continue; }
                    const double f0 = anteil(*w, t - kPmoveTaktMs);
                    const double f1 = anteil(*w, t);
                    if (f0 < 1.0 && f1 > f0) {
                        p.z += (w->to[2] - p.z) * static_cast<float>((f1 - f0) / (1.0 - f0));
                    }
                }
                p.vz = 0.0F;
                p.amBoden = false;
                p.gefragt = false;
            } else {
                bahn = nullptr;
                if (t + 0.5 >= naechstesPmove) {
                    const double dt = langerSchritt ? kSpawnSchrittMs : kPmoveTaktMs;
                    langerSchritt = false;
                    const float waag = std::hypot(x - vx, y - vy);
                    const bool beweglich = h.beweglich && h.beweglich(x, y, t);
                    // rc601 (bewegung, teil 3): vom Plan abgesetzt und still -
                    // nichts zieht sie weg.
                    if (!(planRuhe && waag < 0.01F && !beweglich)) {
                        planRuhe = false;
                        pmoveSchritt(h, p, x, y, waag, hb, t, static_cast<float>(dt / 1000.0), g);
                    }
                }
            }
            const bool bsNoclip = st.bsNoclip;
            if (bsNoclipVorher && !bsNoclip) {
                p.z += kNoclipHeben;
                p.gefragt = false;
            }
            bsNoclipVorher = bsNoclip;
        }
        // --- Das Skript dieses Takts (ICARUS am Ende von NPC_Think) ---------
        //
        // set ( SET_ORIGIN ) ruft G_SetOrigin (Q3_Interface.cpp:8792): der Ort
        // gilt genau, ps.velocity bleibt. (Das +1 und das Loeschen der
        // Geschwindigkeit in Q3_SetOrigin gehoeren zu SET_COPY_ORIGIN,
        // Q3_Interface.cpp:1309-1341, 1413-1419.)
        while (si < spruenge.size() && spruenge[si]->startMs <= t + 1e-6) {
            planRuhe = false;   // rc601 (bewegung, teil 3)
            p.z = spruenge[si]->to[2];
            p.amBoden = false;
            p.gefragt = false;
            ++si;
        }
        // rc601 (bewegung, Review): set ( SET_GRAVITY ) dieses Takts.
        while (gi < eigen.size() && eigen[gi].first <= t + 1e-6) {
            g = eigen[gi].second;
            eigeneAktiv = true;
            ++gi;
        }
        if (!eigeneAktiv && !kartenAb.empty()) { g = weltBei(t); }
        noclipVorher = ohnePmove(st, t);
        if (st.removed) {
            // Freigegeben (Gone): kein Pmove mehr. Die Hoehe bleibt stehen.
            for (std::size_t r = k; r < n; ++r) { hz[r] = p.z; }
            break;
        }
        vx = x;
        vy = y;
        // rc601 (bewegung, teil 3): im Plan die Hoehe des Plans.
        if (bahn == nullptr && !noclipVorher) {
            if (const LaufPlan* lp = planBei(t)) {
                const NavVec o = ortBei(*lp, t);
                const LaufZustand zs = zustandBei(*lp, t);
                p.z = o[2];
                p.vz = zs.tempoVek[2];
                p.amBoden = zs.amBoden;
                p.gefragt = false;
                planRuhe = zs.amBoden && zs.tempoVek[0] == 0.0F && zs.tempoVek[1] == 0.0F && zs.tempoVek[2] == 0.0F;
            }
        } else {
            planRuhe = false;
        }
        hz[k] = p.z;
    }
    return hz;
}

// Moeller-Trumbore, beide Seiten: der Anteil entlang o + f*d, < 0 kein Treffer.
float strahlDreieck(const float o[3], const float d[3], const float* p0, const float* p1, const float* p2) {
    const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    const float q[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    const float det = e1[0] * q[0] + e1[1] * q[1] + e1[2] * q[2];
    if (std::fabs(det) < 1e-12F) {
        return -1.0F;
    }
    const float inv = 1.0F / det;
    const float s[3] = {o[0] - p0[0], o[1] - p0[1], o[2] - p0[2]};
    const float u = (s[0] * q[0] + s[1] * q[1] + s[2] * q[2]) * inv;
    if (u < -1e-5F || u > 1.0F + 1e-5F) {
        return -1.0F;
    }
    const float r[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    const float v = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) * inv;
    if (v < -1e-5F || u + v > 1.0F + 1e-5F) {
        return -1.0F;
    }
    const float f = (e2[0] * r[0] + e2[1] * r[1] + e2[2] * r[2]) * inv;
    return (f >= 0.0F && f <= 1.0F) ? f : -1.0F;
}

bool batchFest(const BspMesh::Batch& b, const std::vector<bool>& fest) {
    if (fest.empty()) {
        return true;
    }
    return b.shader >= 0 && static_cast<std::size_t>(b.shader) < fest.size() && fest[static_cast<std::size_t>(b.shader)];
}

}  // namespace

bool bodenUnter(const BodenStrahl& strahl, float x, float y, float zOben, float zUnten, float hb, double ms,
                float& bodenZ) {
    if (!strahl || !(zOben > zUnten)) {
        return false;
    }
    const float ecken[5][2] = {{0.0F, 0.0F}, {-hb, -hb}, {hb, -hb}, {hb, hb}, {-hb, hb}};
    bool gefunden = false;
    float best = zUnten;
    for (const auto& e : ecken) {
        const std::array<float, 3> von{x + e[0], y + e[1], zOben};
        const std::array<float, 3> bis{x + e[0], y + e[1], zUnten};
        std::array<float, 3> t{};
        if (!strahl(von, bis, ms, t)) {
            continue;
        }
        if (t[2] > zOben + 0.01F || t[2] < zUnten - 0.01F) {
            continue;
        }
        if (!gefunden || t[2] > best) {
            best = t[2];
            gefunden = true;
        }
    }
    if (gefunden) {
        bodenZ = best;
    }
    return gefunden;
}

void schwerkraftEinrechnen(Scene& s, const SchwerkraftHilfe& h) {
    for (Actor& a : s.actors) {
        a.hoehe.clear();
    }
    if (!h.strahl || !(h.endeMs > 0.0)) {
        return;
    }
    for (Actor& a : s.actors) {
        // Brush-Modelle sind keine clients; ohne NPC_type keine Figur; ohne
        // Startort nichts zu rechnen; Flieger und Fahrzeuge fallen nicht.
        if (a.brushModel > 0 || a.npcType.empty() || !a.haveStart || a.ohneSchwerkraft) {
            continue;
        }
        a.hoehe = backen(a, h, s.schwerkraft, s.schwerkraftAb);
    }
}

std::vector<bool> festeShader(const BspGeometry& geo) {
    std::vector<bool> fest(geo.shaders.size(), false);
    for (std::size_t i = 0; i < geo.shaders.size(); ++i) {
        fest[i] = (geo.shaders[i].contentFlags & kFesteInhalte) != 0U;
    }
    return fest;
}

bool bodenTreffer(const BspGeometry& geo, const float von[3], const float bis[3], float treffer[3]) {
    // traceRay gibt den ERSTEN Treffer; ist er nicht fest (Wasser, Nebel,
    // nonsolid), ein Stueck dahinter weitersuchen.
    float a[3] = {von[0], von[1], von[2]};
    const float d[3] = {bis[0] - von[0], bis[1] - von[1], bis[2] - von[2]};
    const float laenge = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(laenge > 0.0F)) {
        return false;
    }
    for (int runde = 0; runde < 64; ++runde) {
        const TraceTreffer t = traceRay(geo, a, bis);
        if (!t.hit) {
            return false;
        }
        bool fest = true;
        if (t.surface >= 0 && static_cast<std::size_t>(t.surface) < geo.surfaces.size()) {
            const int sh = geo.surfaces[static_cast<std::size_t>(t.surface)].shader;
            if (sh >= 0 && static_cast<std::size_t>(sh) < geo.shaders.size()) {
                fest = (geo.shaders[static_cast<std::size_t>(sh)].contentFlags & kFesteInhalte) != 0U;
            }
        }
        if (fest) {
            for (int k = 0; k < 3; ++k) { treffer[k] = t.point[k]; }
            return true;
        }
        for (int k = 0; k < 3; ++k) { a[k] = t.point[k] + d[k] / laenge * 0.5F; }
        const float weit = (a[0] - von[0]) * d[0] + (a[1] - von[1]) * d[1] + (a[2] - von[2]) * d[2];
        if (weit >= laenge * laenge) {
            return false;
        }
    }
    return false;
}

bool netzTreffer(const BspMesh& netz, const std::vector<bool>& fest, const float von[3], const float bis[3],
                 float treffer[3]) {
    const float d[3] = {bis[0] - von[0], bis[1] - von[1], bis[2] - von[2]};
    float beste = 2.0F;
    for (const BspMesh::Batch& b : netz.batches) {
        if (!batchFest(b, fest)) {
            continue;
        }
        const std::size_t ende = std::min<std::size_t>(netz.indexes.size(), std::size_t{b.firstIndex} + b.numIndexes);
        for (std::size_t i = b.firstIndex; i + 2 < ende; i += 3) {
            const std::uint32_t i0 = netz.indexes[i];
            const std::uint32_t i1 = netz.indexes[i + 1];
            const std::uint32_t i2 = netz.indexes[i + 2];
            if (i0 >= netz.verts.size() || i1 >= netz.verts.size() || i2 >= netz.verts.size()) {
                continue;
            }
            const float f = strahlDreieck(von, d, netz.verts[i0].xyz, netz.verts[i1].xyz, netz.verts[i2].xyz);
            if (f >= 0.0F && f < beste) {
                beste = f;
            }
        }
    }
    if (beste > 1.0F) {
        return false;
    }
    for (int k = 0; k < 3; ++k) { treffer[k] = von[k] + d[k] * beste; }
    return true;
}

BodenRaster baueBodenRaster(const BspMesh& netz, const std::vector<bool>& fest, float zelle) {
    BodenRaster r;
    r.zelle = zelle > 1.0F ? zelle : 64.0F;
    float lo[2] = {1.0e30F, 1.0e30F};
    float hi[2] = {-1.0e30F, -1.0e30F};
    for (const BspMesh::Batch& b : netz.batches) {
        if (!batchFest(b, fest)) {
            continue;
        }
        const std::size_t ende = std::min<std::size_t>(netz.indexes.size(), std::size_t{b.firstIndex} + b.numIndexes);
        for (std::size_t i = b.firstIndex; i + 2 < ende; i += 3) {
            const std::uint32_t ix[3] = {netz.indexes[i], netz.indexes[i + 1], netz.indexes[i + 2]};
            if (ix[0] >= netz.verts.size() || ix[1] >= netz.verts.size() || ix[2] >= netz.verts.size()) {
                continue;
            }
            std::array<float, 9> d{};
            for (std::size_t e = 0; e < 3; ++e) {
                for (std::size_t k = 0; k < 3; ++k) {
                    d[e * 3 + k] = netz.verts[ix[e]].xyz[k];
                }
            }
            // Senkrechte Dreiecke trifft ein senkrechter Strahl nicht.
            const float ux = d[3] - d[0];
            const float uy = d[4] - d[1];
            const float wx = d[6] - d[0];
            const float wy = d[7] - d[1];
            if (std::fabs(ux * wy - uy * wx) < 1e-6F) {
                continue;
            }
            for (std::size_t e = 0; e < 3; ++e) {
                for (std::size_t k = 0; k < 2; ++k) {
                    lo[k] = std::min(lo[k], d[e * 3 + k]);
                    hi[k] = std::max(hi[k], d[e * 3 + k]);
                }
            }
            r.dreiecke.push_back(d);
        }
    }
    if (r.dreiecke.empty()) {
        return r;
    }
    // Hoechstens etwa vier Millionen Zellen.
    for (;;) {
        r.nx = static_cast<int>((hi[0] - lo[0]) / r.zelle) + 1;
        r.ny = static_cast<int>((hi[1] - lo[1]) / r.zelle) + 1;
        if (static_cast<long long>(r.nx) * r.ny <= 4000000LL) { break; }
        r.zelle *= 2.0F;
    }
    r.x0 = lo[0];
    r.y0 = lo[1];
    const auto zellen = static_cast<std::size_t>(r.nx) * static_cast<std::size_t>(r.ny);
    const auto bereich = [&r](const std::array<float, 9>& d, int& ax, int& ay, int& bx, int& by) {
        const float mx = std::min({d[0], d[3], d[6]});
        const float my = std::min({d[1], d[4], d[7]});
        const float gx = std::max({d[0], d[3], d[6]});
        const float gy = std::max({d[1], d[4], d[7]});
        ax = std::clamp(static_cast<int>((mx - r.x0) / r.zelle), 0, r.nx - 1);
        ay = std::clamp(static_cast<int>((my - r.y0) / r.zelle), 0, r.ny - 1);
        bx = std::clamp(static_cast<int>((gx - r.x0) / r.zelle), 0, r.nx - 1);
        by = std::clamp(static_cast<int>((gy - r.y0) / r.zelle), 0, r.ny - 1);
    };
    const auto zelleVon = [&r](int xx, int yy) {
        return static_cast<std::size_t>(yy) * static_cast<std::size_t>(r.nx) + static_cast<std::size_t>(xx);
    };
    std::vector<std::uint32_t> zahl(zellen, 0U);
    for (const auto& d : r.dreiecke) {
        int ax = 0;
        int ay = 0;
        int bx = 0;
        int by = 0;
        bereich(d, ax, ay, bx, by);
        for (int yy = ay; yy <= by; ++yy) {
            for (int xx = ax; xx <= bx; ++xx) { ++zahl[zelleVon(xx, yy)]; }
        }
    }
    r.anfang.assign(zellen + 1U, 0U);
    for (std::size_t z = 0; z < zellen; ++z) { r.anfang[z + 1U] = r.anfang[z] + zahl[z]; }
    r.liste.assign(r.anfang[zellen], 0U);
    std::vector<std::uint32_t> pos(r.anfang.begin(), r.anfang.end() - 1);
    for (std::size_t i = 0; i < r.dreiecke.size(); ++i) {
        int ax = 0;
        int ay = 0;
        int bx = 0;
        int by = 0;
        bereich(r.dreiecke[i], ax, ay, bx, by);
        for (int yy = ay; yy <= by; ++yy) {
            for (int xx = ax; xx <= bx; ++xx) { r.liste[pos[zelleVon(xx, yy)]++] = static_cast<std::uint32_t>(i); }
        }
    }
    return r;
}

bool rasterTreffer(const BodenRaster& r, float x, float y, float zOben, float zUnten, float& z) {
    if (r.dreiecke.empty() || r.nx <= 0 || r.ny <= 0) {
        return false;
    }
    const int cx = static_cast<int>(std::floor((x - r.x0) / r.zelle));
    const int cy = static_cast<int>(std::floor((y - r.y0) / r.zelle));
    if (cx < 0 || cy < 0 || cx >= r.nx || cy >= r.ny) {
        return false;
    }
    const std::size_t zi = static_cast<std::size_t>(cy) * static_cast<std::size_t>(r.nx) + static_cast<std::size_t>(cx);
    bool gefunden = false;
    float best = zUnten;
    for (std::uint32_t j = r.anfang[zi]; j < r.anfang[zi + 1U]; ++j) {
        const std::array<float, 9>& d = r.dreiecke[r.liste[j]];
        // Baryzentrisch in X/Y, Raender eingeschlossen.
        const float ux = d[3] - d[0];
        const float uy = d[4] - d[1];
        const float wx = d[6] - d[0];
        const float wy = d[7] - d[1];
        const float det = ux * wy - uy * wx;
        const float px = x - d[0];
        const float py = y - d[1];
        const float s = (px * wy - py * wx) / det;
        const float t = (ux * py - uy * px) / det;
        constexpr float kRand = 1e-5F;
        if (s < -kRand || t < -kRand || s + t > 1.0F + kRand) {
            continue;
        }
        const float hz = d[2] + s * (d[5] - d[2]) + t * (d[8] - d[2]);
        if (hz > zOben + 0.001F || hz < zUnten - 0.001F) {
            continue;
        }
        if (!gefunden || hz > best) {
            best = hz;
            gefunden = true;
        }
    }
    if (gefunden) {
        z = best;
    }
    return gefunden;
}

void schwerkraftWechsel(Scene& s, const MapData& map, const std::vector<SchwerkraftBenutzung>& benutzt) {
    // target_gravity_change_use (g_target.cpp:937-952 MD), SP_target_gravity_
    // change (:960-965): "gravity" ist G_SpawnFloat (Vorgabe 0).
    for (const SchwerkraftBenutzung& b : benutzt) {
        for (const MapEntity& e : map.entities) {
            if (e.targetname != b.name) { continue; }
            std::string kl = e.classname;
            for (char& c : kl) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            if (kl != "target_gravity_change") { continue; }
            const std::string* g = e.find("gravity");
            const float wert = g != nullptr ? static_cast<float>(std::atof(g->c_str())) : 0.0F;
            const std::string* sf = e.find("spawnflags");
            const bool global = sf != nullptr && (std::atoi(sf->c_str()) & 1) != 0;
            if (global) {
                // gi.cvar_set( "g_gravity", va( "%f", self->speed ) )
                s.schwerkraftAb.emplace_back(b.ms, wert);
            } else {
                // activator->client->ps.gravity = floor( self->speed ) und
                // SVF_CUSTOM_GRAVITY - nur fuer einen client.
                for (Actor& a : s.actors) {
                    if (!b.ausloeser.empty() && a.name == b.ausloeser && !a.npcType.empty() && a.brushModel <= 0) {
                        a.eigeneSchwerkraft.emplace_back(b.ms, std::floor(wert));
                    }
                }
            }
        }
    }
    std::stable_sort(s.schwerkraftAb.begin(), s.schwerkraftAb.end(),
                     [](const std::pair<double, float>& x, const std::pair<double, float>& y) { return x.first < y.first; });
}

}  // namespace bhed
