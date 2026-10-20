// navlauf.h - Paket bewegung (rc601): Laufen und Wege wie im Spiel.
//
// Gemeldet: "NPCs don't walk to waypoints correctly." Und vom Betreuer, als
// statt der Wege nur eine Warnung kam: "das mit dem Laufen soll genauso gehen
// wie ingame bitte".
//
// Hier steht, was die Engine in JEDEM Denkbild eines NPC mit einem Laufziel
// tut, Zeile fuer Zeile aus Movie Duels (MovieDuels-SJE-SP-Rend2, code/game;
// wo OpenJK abweicht, steht es dabei):
//
//   NPC_Think (NPC.cpp:2213) alle 50 ms
//     UpdateGoal (NPC_goal.cpp:173)    angekommen? -> NPC_ReachedGoal:
//                                      Ziel weg, TID_MOVE_NAV erledigt
//     NPC_MoveToGoal (NPC_move.cpp:892)
//       STEER::Activate                Ort, ps.velocity, mMaxSpeed lesen
//       STEER::GoTo (g_navigator.cpp:4407)
//         Reached / SafeToGoTo / Persue->Seek / AvoidCollisions
//       sonst NAV::GoTo (g_navigator.cpp:981)
//         GetNearestNode, HasPath/UpdatePath/FindPath (A*), STEER::Path
//       sonst STEER::Stop - die Figur bremst, das Ziel BLEIBT: ohne Weg steht
//         sie fuer immer, und ein dowait auf TID_MOVE_NAV wird nie fertig
//       STEER::DeActivate              ps.speed, ps.moveDir, desiredYaw
//     NPC_UpdateAngles                 Drehen mit (60 + 3*yawSpeed) Grad/s
//     ClientThink -> Pmove (50 ms)     PM_Friction, PM_WalkMove/PM_Accelerate
//
// Das Wegnetz baut die Engine beim Laden der Karte (NAV::SpawnedPoint je
// Wegpunkt, NAV::LoadFromEntitiesAndSaveToFile nach den ersten Bildern) -
// baueNavNetz tut dasselbe. Was die Engine dabei per Spur prueft (gi.trace,
// gi.inPVS), fragt dieser Teil ueber Rueckrufe; ohne sie ist alles frei.
//
// Zufall der Engine (Q_irand) - hier jeweils fest und so vermerkt:
//   * NAV::FindPath: puser.mLastAStarTime = level.time + Q_irand(3000, 6000)
//     -> 4500 (die Mitte).
//   * Liegt der naechste "Knoten" auf einer Kante (negative Nummer), nimmt
//     die Engine zufaellig eines der Enden -> immer mNodeA.
//   * TestCollision: lastAvoidSteerSideDebouncer = level.time +
//     Q_irand(500, SIDE_LOCKED_TIMER 2000) -> 1250.
#ifndef BHED_NAVLAUF_H
#define BHED_NAVLAUF_H

#include "bhed/bsp.h"

#include <array>
#include <cctype>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bhed {

using NavVec = std::array<float, 3>;

// --- Spuren (gi.trace) -------------------------------------------------------
//
// Was die Navigation von einem trace_t liest.
inline constexpr int kNavKeineEntity = -1;   // ENTITYNUM_NONE
inline constexpr int kNavWeltEntity = -2;    // ENTITYNUM_WORLD
struct NavSpur {
    bool allsolid = false;
    bool startsolid = false;
    float fraction = 1.0F;
    NavVec endpos{};
    NavVec normal{};
    int entity = kNavKeineEntity;   // sonst Index in MapData::entities oder kNavWeltEntity
    std::uint32_t contents = 0;
};
// gi.trace(start, mins, maxs, end, passEntityNum, contentmask). `ignoriere`
// ist ein Index in MapData::entities oder kNavKeineEntity.
using NavSpurFn = std::function<NavSpur(const NavVec& start, const NavVec& mins, const NavVec& maxs,
                                        const NavVec& end, int ignoriere, std::uint32_t maske)>;
// gi.inPVS(a, b).
using NavPvsFn = std::function<bool(const NavVec& a, const NavVec& b)>;

// surfaceflags.h:32-45, bg_public.h:998-1001 (MD = OpenJK)
inline constexpr std::uint32_t kNavSolid = 0x00000001U;
inline constexpr std::uint32_t kNavPlayerClip = 0x00000010U;
inline constexpr std::uint32_t kNavMonsterClip = 0x00000020U;
inline constexpr std::uint32_t kNavBotClip = 0x00000040U;
inline constexpr std::uint32_t kNavBody = 0x00000100U;
inline constexpr std::uint32_t kNavTerrain = 0x00001000U;
inline constexpr std::uint32_t kNavMaskNpcSolid = kNavSolid | kNavMonsterClip | kNavBody | kNavTerrain;
inline constexpr std::uint32_t kNavMaskDeadSolid = kNavSolid | kNavPlayerClip | kNavTerrain;

// --- Das Wegnetz (g_navigator.cpp: CWayNode, CWayEdge, TGraph) ---------------
//
// Knoten und Kanten zaehlen wie in ragl::graph_vs ab 1 (Platz 0 ist ein
// Platzhalter), Kanten werden in derselben Reihenfolge angelegt und
// gestrichen - die Reihenfolge der Nachbarn bestimmt, wie A* bei gleichen
// Kosten entscheidet.
struct NavKnoten {
    enum Typ : int { Keiner = 0, Wegpunkt = 1, Kampf = 2, Ziel = 3 };   // NAV::EPointType
    NavVec punkt{};
    float radius = 0.0F;
    int typ = Keiner;
    bool insel = false;          // WN_ISLAND
    bool schwebend = false;      // WN_FLOATING
    bool zumBoden = false;       // WN_DROPTOFLOOR
    bool keinAuto = false;       // WN_NOAUTOCONNECT
    std::string name;
    std::array<std::string, 5> ziele;   // target..target4, targetJump
    int entity = -1;             // Index in MapData::entities
};
struct NavKante {
    // EWayEdgeFlags als Bits (g_navigator.cpp:248-266)
    enum Flag : std::uint32_t {
        GroesseMittel = 1U << 1, GroesseGross = 1U << 2,
        TuerSperrt = 1U << 3, WandSperrt = 1U << 4, ZerbrechlichSperrt = 1U << 5,
        Gueltig = 1U << 6, AmRand = 1U << 7, Fliegen = 1U << 8, Springen = 1U << 9,
        KannUngueltig = 1U << 10, VomDesigner = 1U << 11,
    };
    int a = 0;
    int b = 0;
    float laenge = 0.0F;         // mDistance
    std::uint32_t flags = 0;
    int entity = kNavKeineEntity;   // mEntityNum
    int besitzer = kNavKeineEntity; // mOwnerNum
    // Fuer is_valid ohne laufende Spiellogik schon beim Bau entschieden (siehe
    // baueNavNetz): eine Tuer/Wand an dieser Kante sperrt.
    bool gesperrt = false;
    // rc601 (bewegung, teil 3): is_valid zur Laufzeit (g_navigator.cpp:584-628
    // MD). Eine Tuerkante sperrt nur, solange die Tuer ZU ist ("Closed =
    // StartOpen ? moverState == MOVER_POS2 : moverState == MOVER_POS1") -
    // `gesperrt` sagt dann, ob der Besitzer sie nicht oeffnet (ohne
    // MOVER_LOCKED der Tuer selbst, das ein use aufhebt: `besitzerIstTuer`
    // mit NavMoverPlan::Stand::verriegelt).
    bool tuerStartOffen = false;     // spawnflags & 1 des Meisters
    bool besitzerIstTuer = false;    // owner == ent
    bool verriegeltAnfang = false;   // MOVER_LOCKED des Meisters beim Spawnen
    bool goodie = false;             // owner->spawnflags & MOVER_GOODIE
    [[nodiscard]] bool hat(Flag f) const noexcept { return (flags & f) != 0U; }
    // CWayEdge::Size: MEDIUM wenn gesetzt, sonst LARGE
    [[nodiscard]] int groesse() const noexcept { return hat(GroesseMittel) ? 1 : 2; }
};

// rc604 fix604 (JK2 Stufe 3): das Wegnetz von Jedi Outcast - CNavigator
// (codeJK2/game/g_navigator.cpp). Knoten zaehlen ab 0 (m_nodes), Kanten
// stehen je Knoten in der Reihenfolge von CNode::AddEdge, raenge[id] ist
// m_ranks (CalculatePath, -1 kein Weg). Das Spiel laedt maps/<karte>.nav,
// wenn dessen Pruefsumme zur .bsp passt (CNavigator::Load, g_main.cpp:690);
// sonst baut es das Netz aus den Wegpunkten (SP_waypoint, NAV_CalculatePaths
// - nur die Verbindungen target..target4, HardConnect, g_nav.cpp:1725-1829).
struct NavNetzJk2 {
    struct Kante {          // CNode::edge_t
        int id = 0;
        int kosten = 0;
        unsigned char flags = 0;   // EFLAG_BLOCKED 1
    };
    struct Knoten {
        NavVec punkt{};
        int flags = 0;
        int radius = 0;
        std::vector<Kante> kanten;
        std::vector<int> raenge;
    };
    struct Fehl {           // failedEdge_t (MAX_FAILED_EDGES 32)
        int a = -1;
        int b = -1;
        int entity = 1023;  // ENTITYNUM_NONE
    };
    std::vector<Knoten> knoten;
    std::vector<Fehl> fehl;         // die belegten Plaetze, in ihrer Reihenfolge
    bool ausDatei = false;          // aus maps/<karte>.nav
    // G_EntIsUnlockedDoor je Entity (Index in MapData::entities) beim Spawnen:
    // eine func_door, die sich von selbst oeffnet (g_mover.cpp:1328-1390).
    // MOVER_LOCKED zur Zeit steht im Mover-Plan (NavMoverPlan::Stand).
    std::vector<signed char> tuerFrei;
    [[nodiscard]] int kanteFehl(int a, int b) const;   // EdgeFailed: Platz oder -1
};

struct NavNetz {
    std::vector<NavKnoten> knoten;   // [0] Platzhalter
    std::vector<NavKante> kanten;    // [0] Platzhalter
    std::vector<bool> kanteBenutzt;
    struct Nachbar {
        short kante = 0;
        short knoten = 0;
    };
    std::vector<std::vector<Nachbar>> links;   // je Knoten, hoechstens 20

    // TGraphCells (ragl/graph_vs.h:219 ff., ratl/grid_vs.h): 32 x 32 Zellen
    // ueber die Ausdehnung der Knoten, je Zelle hoechstens 60 Knoten und 60
    // Kanten (die naechsten zur Zellmitte im Umkreis von CELL_RANGE).
    struct Zelle {
        std::vector<short> knoten;
        std::vector<short> kanten;
    };
    std::vector<Zelle> zellen;   // 32 * 32, x + y * 32
    float zMin[2]{12345.0F, 12345.0F};
    float zMax[2]{12345.0F, 12345.0F};
    float zMass[2]{0.0F, 0.0F};

    // TGraphRegion (ragl/graph_region.h)
    std::vector<int> region;     // je Knoten, -1 keine
    int regionZahl = 0;
    int reserviert = 0;
    std::vector<short> regionLinks;               // NUM_REGIONS^2
    std::vector<std::vector<short>> regionKanten;  // Pool
    // rc601 (bewegung, Durchsicht 2): has_valid_edge je (Akteur ohne/MEDIUM/
    // LARGE, Region A, Region B), beim ersten Fragen gerechnet und gemerkt
    // (0 offen, 1 nein, 2 ja). GetNearestNode fragt es fuer jeden Kandidaten
    // in jedem Denkbild - vorher jedes Mal eine Tiefensuche mit neuer Liste.
    // Das Netz aendert sich nach dem Bau nicht mehr; geteilt, damit NavNetz
    // kopierbar bleibt (wer eine Kopie aendert, setzt es zurueck).
    std::shared_ptr<std::vector<std::atomic<signed char>>> regionGueltig;
    // rc604 fix604: das Netz von Jedi Outcast (nur mit navJk2Regeln gebaut).
    std::shared_ptr<const NavNetzJk2> jk2;

    int kantenZahl = -1;                                   // nach dem Bau, -1: zaehlen
    [[nodiscard]] int kanteZwischen(int a, int b) const;   // get_edge_across
    [[nodiscard]] int zahlKanten() const;                  // size_edges
    [[nodiscard]] const Zelle* zelleBei(float x, float y) const;
    // Fuer die Meldungen der Oberflaeche: Knoten zum Namen.
    [[nodiscard]] int knotenNamens(const std::string& name) const;
};

// rc601 (bewegung, teil 3): eine Spur, bei der einige Brush-Entities an
// einem anderen Ort stehen oder keine contents haben (Index in
// MapData::entities).
struct NavVersetztEintrag {
    int entity = -1;
    NavVec ort{};
    bool fest = true;   // false: haelt keine Spur auf (noch nicht gespawnt, START_OFF)
};
using NavVersetzt = std::vector<NavVersetztEintrag>;
using NavSpurVersetztFn = std::function<NavSpur(const NavVec& start, const NavVec& mins, const NavVec& maxs,
                                                const NavVec& end, int ignoriere, std::uint32_t maske,
                                                const NavVersetzt& versetzt)>;

struct NavBauHilfe {
    // leer: nichts im Weg. rc601 (bewegung, teil 3): die Spur mit dem Stand von
    // PHASE 0 des Netzbaus (g_navigator.cpp:1482-1533 MD): Tueren mit
    // START_OPEN zu (pos2), func_wall/func_usable mit START_OFF an.
    NavSpurFn spur;
    NavPvsFn pvs;     // leer: alles sichtbar
    // rc601 (bewegung, teil 3): TestEdge oeffnet eine Tuerkette und spurt
    // noch einmal (g_navigator.cpp:1313-1343 MD). `tuerStellungen` liefert
    // pos1/pos2 einer func_door wie SP_func_door (g_mover.cpp:1553-1566 MD,
    // START_OPEN schon vertauscht); `spurVersetzt` spurt wie `spur`, nur mit
    // den angegebenen Entities anders. Damit sehen auch die Spuren beim
    // Spawnen der Wegpunkte die Karte so, wie sie dann ist (nur die Entities
    // davor, START_OPEN auf pos1, START_OFF ohne contents). Fehlt eines,
    // spurt alles mit `spur`, die zweite Tuerspur ohne die getroffene Tuer.
    std::function<bool(int entity, NavVec& pos1, NavVec& pos2)> tuerStellungen;
    NavSpurVersetztFn spurVersetzt;
    // rc604 fix604 (JK2): maps/<karte>.nav (leer: keine) und die Pruefsumme der
    // .bsp (Com_BlockChecksum, navBlockPruefsumme) - passt sie, gilt die Datei.
    std::string jk2Nav;
    bool jk2SummeDa = false;
    int jk2KartenSumme = 0;
};

// Das Netz so, wie die Engine es beim Laden baut: SP_waypoint,
// SP_waypoint_small, SP_waypoint_navgoal, SP_point_combat (g_nav.cpp:121-279,
// NPC_combat.cpp:3347) mit NAV::SpawnedPoint (g_navigator.cpp:2218), dann
// NAV::LoadFromEntitiesAndSaveToFile (g_navigator.cpp:1469-2080).
[[nodiscard]] std::shared_ptr<NavNetz> baueNavNetz(const std::vector<MapEntity>& ents, const NavBauHilfe& h);

// --- rc601 (bewegung, teil 3): Tueren und Mover zur Zeit ----------------------
//
// Auftrag: "Doors and movers at their state at that time". Die Engine spurt
// gegen jeden Mover dort, wo er in DIESEM Bild steht (G_RunMover vor den NPCs:
// die Karte spawnt ihre Mover vor den Figuren, also mit kleinerer Nummer), und
// is_valid fragt bei jedem A* den moverState der Tuer (g_navigator.cpp:584-628
// MD). Bis teil 3 standen alle Brush-Entities fuer die Spuren an ihrem
// Spawn-Platz, und Tuerkanten waren beim Bau entschieden - eine Figur lief
// gegen eine Tuer, die laengst offen war, und blieb davor stehen.
//
// Der Plan kommt aus der Mover-Simulation der Oberflaeche (MoverSim, je
// 50 ms). Darin steht nur, was sich irgendwann von dem unterscheidet, was die
// feste Welt (NavWelt::spur) annimmt.
struct NavMoverPlan {
    struct Stand {
        NavVec ort{};
        NavVec winkel{};
        bool fest = true;              // CONTENTS_SOLID (func_wall START_OFF, zerbrochen: nein)
        signed char zustand = 0;       // moverState_t: 0 POS1, 1 POS2, 2 1TO2, 3 2TO1
        bool verriegelt = false;       // MOVER_LOCKED (UnLockDoors hebt es auf)
        [[nodiscard]] bool gleich(const Stand& o) const noexcept {
            return ort == o.ort && winkel == o.winkel && fest == o.fest && zustand == o.zustand &&
                   verriegelt == o.verriegelt;
        }
    };
    struct Mover {
        int entity = -1;               // Index in MapData::entities
        Stand anfang;                  // wie die feste Welt ihn sieht
        float radius = 0.0F;           // um den Ursprung (wie die grobe Pruefung der Spur)
        // Der Kasten des Modells um seinen Ursprung (ungedreht), fuer die Frage,
        // ob eine Spur ihn beruehren kann. kastenDa false: nur der Radius.
        bool kastenDa = false;
        NavVec kastenMin{};
        NavVec kastenMax{};
        std::vector<Stand> takte;      // je 50 ms ab 0 ms; danach gilt der letzte
    };
    std::vector<Mover> mover;
    std::vector<int> nachEntity;       // Entity -> Index in mover, -1
    std::uint64_t kennung = 0;         // 0: nichts bewegt sich
    // Je Takt: alle Tuer-/Wandzustaende (zustand, fest, verriegelt) wie am
    // Anfang? (Fuer den Zwischenspeicher von has_valid_edge.)
    std::vector<bool> kantenWieAnfang;
    [[nodiscard]] const Stand& bei(const Mover& m, double t) const;
    [[nodiscard]] const Stand* standVon(int entity, double t) const;
    [[nodiscard]] bool kantenUnveraendert(double t) const;
    // Nach dem Fuellen von mover: nachEntity, kantenWieAnfang, kennung.
    void fertig(std::size_t entities);
};
// Die Spur mit der Zeit des Denkbilds (die Mover an ihrem Platz dann).
using NavSpurZeitFn = std::function<NavSpur(const NavVec& start, const NavVec& mins, const NavVec& maxs,
                                            const NavVec& end, int ignoriere, std::uint32_t maske, double t)>;

// --- rc601 (bewegung, teil 3): andere Figuren (CONTENTS_BODY) ----------------
// Eine Figur als Kasten fuer die Spuren der anderen (SV_ClipMoveToEntities mit
// CM_TempBoxModel, sv_world.cpp): Ort, mins/maxs, ps.velocity (TestCollision
// fragt "Contact moving / standing", g_navigator.cpp:5118-5163 MD).
inline constexpr int kNavKoerperBasis = 1 << 20;   // NavSpur::entity ab hier: Figur Nummer entity - Basis
struct NavKoerperKasten {
    int id = 0;
    NavVec ort{};
    NavVec mins{};
    NavVec maxs{};
    NavVec tempo{};
};

// Was NAV:: und STEER:: zur Laufzeit wissen.
struct NavWelt {
    std::shared_ptr<const NavNetz> netz;   // leer: kein Wegnetz (size_edges() == 0)
    NavSpurFn spur;                        // leer: nichts im Weg
    // Der Boden unter (x, y) zwischen zOben und zUnten, fuer die Hoehe der
    // Figur waehrend des Laufens (Pmove steht auf dem Boden). Leer: die
    // Figur bewegt sich frei im Raum (siehe planeLauf).
    std::function<bool(float x, float y, float zOben, float zUnten, float& bodenZ)> boden;
    // Ist die Entity (Index in MapData::entities) eine func_door? TestCollision
    // haelt Tueren immer fuer sicher (g_navigator.cpp:5110-5115).
    std::function<bool(int entity)> istTuer;
    // Fuer den Zwischenspeicher der Plaene: gleiche Kennung = gleiche Welt.
    // 0: kein Zwischenspeicher.
    std::uint64_t kennung = 0;
    // rc601 (bewegung, teil 3): die Mover zur Zeit. Mit `spurZeit` spurt ein
    // Denkbild gegen die Mover dort, wo sie zu seiner Zeit stehen; `mover`
    // sagt, welche das sind, und is_valid liest daraus den moverState. Die
    // Kennung von `mover` gehoert NICHT in `kennung`: ein Plan aus dem
    // Zwischenspeicher gilt weiter, wenn keine seiner Spuren einen Mover
    // beruehrt, der jetzt anders steht (siehe planeLauf).
    NavSpurZeitFn spurZeit;
    std::shared_ptr<const NavMoverPlan> mover;
    // rc601 (bewegung, teil 3): die anderen Figuren in diesem Denkbild (nur im
    // gemeinsamen Takt aller Figuren, navKoerperLauf). Spuren mit
    // CONTENTS_BODY in der Maske treffen sie.
    const std::vector<NavKoerperKasten>* koerper = nullptr;
    // rc604 fix604: die clipmask der rechnenden Figur ohne CONTENTS_BODY (nur
    // im gemeinsamen Takt): Q3_SetSolid(false) nimmt CONTENTS_BODY aus der
    // clipmask (Q3_Interface.cpp:6106-6112 MD / 5662-5668 OpenJK), NPC_Begin
    // mit SFB_NOTSOLID setzt MASK_NPCSOLID & ~CONTENTS_BODY (NPC_spawn.cpp:
    // 1895-1904 MD / 1152-1161 OpenJK). Pmove spurt mit pm.tracemask =
    // ent->clipmask (g_active.cpp:11036 MD / 5393 OpenJK) - die anderen Figuren halten das Pmove nicht
    // auf. STEER/NAV spuren weiter mit MASK_NPCSOLID (MoveTrace,
    // g_navigator.cpp:860 MD) und weichen ihnen aus.
    bool pmOhneKoerper = false;
    // rc604 fix604 (JK2): gi.inPVS fuer NAV_ClearPathToPoint und
    // GetBestPathBetweenEnts (leer: alles sichtbar).
    NavPvsFn pvs;
};

// --- Laufen ------------------------------------------------------------------
inline constexpr double kNavDenkMs = 50.0;     // NPC_Think: nextthink = level.time + FRAMETIME/2
// rc601 (bewegung, teil 3): NPC_Begin einer beim Laden der Karte gespawnten
// Figur, relativ zum Skriptanfang (wie schwerkraft.cpp kKartenSpawnMs:
// sv_init.cpp:295-363, NPC_spawn.cpp:3101, common.cpp:1426-1432 MD).
inline constexpr double kNavKartenSpawnMs = -200.0;
inline constexpr int kNavNie = -2147483647;    // ein Timer, der nie gesetzt wurde

struct NavPfadPunkt {   // SPathPoint (g_navigator.cpp:385)
    NavVec punkt{};
    float tempo = 0.0F;
    float abbremsRadius = 0.0F;
    float erreichtRadius2 = 0.0F;   // quadriert, wie mReachedRadius
    float abstand = 0.0F;
    float eta = 0.0F;
    int knoten = 0;
};

// Alles, was die Engine von einem Denkbild zum naechsten an der Figur
// mitnimmt und das Laufen beeinflusst.
struct LaufZustand {
    NavVec ort{};                 // currentOrigin
    NavVec tempoVek{};            // ps.velocity
    int tempo = 0;                // ps.speed (int!)
    float gier = 0.0F;            // ps.viewangles[YAW] = currentAngles[YAW]
    float wunschGier = 0.0F;      // NPCInfo->desiredYaw
    float pfadGier = 0.0F;        // NPCInfo->lastPathAngles[YAW]
    int seiteAusweichen = 0;      // NPCInfo->avoidSide (Side_None/Left/Right)
    int letzteSeite = 0;          // lastAvoidSteerSide
    int seiteBis = 0;             // lastAvoidSteerSideDebouncer
    int safeCheckBis = kNavNie;   // TIMER "SafeToGoToCHECK"
    int safeDauerBis = kNavNie;   // TIMER "SafeToGoToDURATION"
    // ent->waypoint, lastWaypoint, noWaypointTime der Figur
    int wegpunkt = 0;
    int letzterWegpunkt = 0;
    int wegpunktBis = 0;
    // dasselbe fuer NPCInfo->tempGoal (das Laufziel)
    int zielWegpunkt = 0;
    int zielLetzterWegpunkt = 0;
    int zielWegpunktBis = 0;
    // SPathUser
    bool pfadDa = false;
    int pfadEnde = 0;
    bool pfadErfolg = false;
    int pfadAStarBis = 0;
    std::shared_ptr<const std::vector<NavPfadPunkt>> pfad;
    int pfadGroesse = 0;          // so viele vorne aus `pfad` gelten noch
    // Ein neues Laufziel (Q3_SetNavGoal -> NPC_SetMoveGoal): tempGoal->waypoint
    // = WAYPOINT_NONE, noWaypointTime = 0, lastWaypoint = WAYPOINT_NONE.
    void neuesZiel() noexcept {
        zielWegpunkt = 0;
        zielLetzterWegpunkt = 0;
        zielWegpunktBis = 0;
    }
    // rc601 (bewegung, teil 3): PmoveSingle mit Boden und Luft.
    bool amBoden = false;         // ps.groundEntityNum != ENTITYNUM_NONE
    int pmZeit = 0;               // ps.pm_time (PMF_TIME_LAND nach hartem Aufsetzen)
    // Wann der Ort zuletzt ohne Plan gesetzt wurde (Spawn, SET_ORIGIN): der
    // naechste Plan beginnt DORT - die Figur faellt im Spiel sofort, nicht
    // erst mit dem naechsten Laufziel (LaufWerte::zielAbMs). kNavKeinOrt:
    // nicht gesetzt.
    double gesetztMs = -1.0e300;
    bool gesetztSpawn = false;    // ... beim Spawnen (G_CheckInSolid, NPC_Begin)
    bool gesetztImFesten = true;  // G_CheckInSolid pruefen (nicht mit SFB_STARTINSOLID)
    // rc601 integ: das erste NPC_Think nach dem Spawn (NPC_Begin + FRAMETIME
    // + Q_irand(0,100), die Mitte 150 ms). Dort laeuft das Verhalten zum
    // ersten Mal (nextBStateThink beginnt bei 0) - der Bezug fuer den Takt
    // des Verhaltens (LaufWerte::verhaltenTaktMs).
    double denkNullMs = kNavKartenSpawnMs + 150.0;
    // rc601 integ: was ein Denkbild OHNE Verhalten weiterbenutzt (OpenJK,
    // NPC.cpp:2512-2520): ps.moveDir des vorigen Denkbilds (oldMoveDir) und
    // NPCInfo->last_ucmd (forwardmove, rightmove, BUTTON_WALKING). Nur
    // gefuehrt, wenn verhaltenTaktMs > 50.
    NavVec letzteBewegung{};
    signed char letzterVor = 0;
    signed char letzterSeit = 0;
    bool letzteGehTaste = false;
    // rc604 fix604 (JK2): NPCInfo->currentSpeed (NPC_Accelerate), distToGoal,
    // consecutiveBlockedMoves. Die Wegpunkte (wegpunkt ...) zaehlen dann
    // Knoten + 1 (0: WAYPOINT_NONE).
    int jk2Tempo = 0;
    float jk2ZielAbstand = 0.0F;
    int jk2Blockiert = 0;
    // ent->failedWaypoints (Knoten + 1, 0 frei; MAX_FAILED_NODES 8) und
    // failedWaypointCheckTime (CNavigator::AddFailedNode/CheckFailedNodes).
    std::array<short, 8> jk2FehlKnoten{};
    int jk2FehlPruefBis = 0;
    // rc605: NPC-Spruenge (NPC_move.cpp:109-718 MD / 59-616 OpenJK) und der
    // Merker "blockiert" von STEER (g_navigator.cpp:4216-4310 MD).
    struct Sprung {
        bool springt = false;         // ps.pm_flags & PMF_JUMPING
        bool laeuft = false;          // NPCInfo->jumpTime
        NavVec ziel{};                // NPCInfo->jumpDest
        float maxXY = 750.0F;         // jumpMaxXYDist
        float maxZ = -450.0F;         // jumpMazZDist
        int zurueckBis = 0;           // jumpBackupTime
        int zurueckSperreBis = kNavNie;   // TIMER "jumpBackupDebounce"
        int naechsterAb = 0;          // jumpNextCheckTime
        float zStart = 0.0F;          // ps.forceJumpZStart
        bool blockiert = false;       // aiFlags & NPCAI_BLOCKED
        int blockiertSeit = 0;        // blockedDebounceTime
        bool blockiertEnt = false;    // blockedTargetEntity (das tempGoal)
        NavVec blockiertOrt{};        // blockedTargetPosition
        // rc605: BS_JUMP (NPC_BSJump, NPC_behavior.cpp:717-962 MD) - jumpState
        // (0 JS_WAITING, 1 JS_FACING, 2 JS_CROUCHING, 3 JS_JUMPING, 4
        // JS_LANDING), ps.legsAnimTimer und die zuletzt gesetzte Animation
        // (1 BOTH_CROUCH1, 2 BOTH_INAIR1, 3 BOTH_LAND1) mit ihrem Beginn.
        bool bsAktiv = false;
        int bsZustand = 0;
        int beinTimer = 0;
        int bsAnim = 0;
        int bsAnimAb = 0;
        // rc605 FU4: die Sprungkette auf Beinen und Oberkoerper (SprungAnim-
        // Codes) - ps.legsAnim/torsoAnim, solange sie aus NPC_JumpAnimation,
        // PM_JumpForDir, NPC_BSJump oder PM_CrashLand stammen (0: PM_Footsteps
        // bzw. der Oberkoerper folgt den Beinen), ihr Beginn, ps.torsoAnimTimer
        // und ob die Figur seit dem letzten Boden ueber den "kickoff"-Zweig von
        // PM_GroundTrace abhob (dann ist jumpZStart 0, bg_pmove.cpp:7475-7512 MD).
        int beinAnim = 0;
        int beinAnimAb = 0;
        int torsoAnim = 0;
        int torsoAnimAb = 0;
        int torsoTimer = 0;
        bool abgehoben = false;
        bool operator==(const Sprung&) const = default;
    };
    Sprung sprung;
    [[nodiscard]] bool hatVorlauf() const noexcept { return gesetztMs > -1.0e299; }
    void vorlaufErledigt() noexcept {
        gesetztMs = -1.0e300;
        gesetztSpawn = false;
    }
    // G_SetOrigin auf einen NPC loescht den Pfad (NAV::ClearPath), die
    // Geschwindigkeit bleibt.
    void springen(const NavVec& wohin) noexcept {
        ort = wohin;
        pfadDa = false;
        pfad.reset();
        pfadGroesse = 0;
    }
};

// Was ueber einen Plan hinweg fest steht. Aendert sich eines davon, schneidet
// der Aufrufer den Plan zur Zeit der Aenderung ab und plant ab dem Zustand
// dort neu (zustandBei) - so wie die Engine jedes Denkbild neu liest.
struct LaufWerte {
    bool zielGesetzt = false;     // NPCInfo->goalEntity
    NavVec ziel{};                // goalEntity->currentOrigin
    float zielRadius = 12.0F;     // NPCInfo->goalRadius
    float maxTempo = 0.0F;        // STEER::Activate mMaxSpeed (Gangart schon gewaehlt)
    float gehTempo = 0.0F;        // stats.walkSpeed (BUTTON_WALKING-Schwelle)
    float drehRate = 330.0F;      // NPC_UpdateAngles: 60 + 3*yawSpeed Grad/s
    float halbBreite = 16.0F;     // mins/maxs[0..1]
    float hoeheOben = 40.0F;      // maxs[2]; mins[2] ist immer -24
    // NPC_ApplyScriptFlags (NPC.cpp:786-813): 2 SCF_RUNNING loescht
    // BUTTON_WALKING, 1 SCF_WALKING setzt es, 0 laesst STEER entscheiden.
    int gangSchalter = 0;
    float stufe = 18.0F;          // PM_StepSlideMove step_size (je Klasse)
    // BS_NOCLIP (client->noclip, Q3_SetBState): NPC_BSNoClip statt
    // NPC_MoveToGoal - ucmd gerade aufs Ziel, kein STEER, kein NAV - und
    // PM_NoclipMove statt PM_WalkMove (ohne Waende, ohne Boden, ohne
    // PM_Footsteps). Das Tempo ist das ps.speed, das die Figur gerade hat
    // (LaufZustand::tempo).
    bool ohneKoerper = false;
    // desiredYaw von aussen nach NPC_MoveToGoal (NPC_BSCinematic mit
    // Watchtarget, NPC_behavior.cpp:304-315). false: STEER gilt.
    std::function<bool(double t, const NavVec& ort, float& gier)> fremdGier;
    // rc601 (bewegung, teil 3): ps.gravity (g_gravity der Karte, SET_GRAVITY
    // der Figur) und ob die Figur ueberhaupt faellt (false: Flieger, Figuren
    // ohne Schwerkraft - PM_FlyMove rechnet behaved nicht; der Plan haelt dann
    // den Boden wie bis teil 2).
    float schwerkraft = 800.0F;
    bool fallen = true;
    // Das Ziel gilt erst NACH dem Denkbild zielAbMs (der Befehl laeuft am Ende
    // von NPC_Think). Davor rechnet der Plan nur Pmove - der Vorlauf ab dem
    // Spawn oder SET_ORIGIN (LaufZustand::gesetztMs).
    double zielAbMs = -1.0e300;
    // abMs ist NPC_Begin: G_CheckInSolid, ein Pmove von 200 ms, das erste
    // Denkbild 150 ms spaeter wieder mit 200 ms (FRAMETIME + Q_irand(0,100),
    // die Mitte; Com_ModifyMsec), danach alle 50 ms - wie schwerkraft.cpp.
    bool spawnVorlauf = false;
    bool spawnImFesten = true;
    // rc601 integ: wie oft NPC_ExecuteBState laeuft (nextBStateThink) - nur
    // dann STEER/NAV (NPC_MoveToGoal), UpdateGoal und das Watchtarget; in den
    // Denkbildern dazwischen dreht die Figur (NPC_UpdateAngles) und Pmove
    // laeuft mit dem letzten ucmd. Movie Duels 50 = jedes Denkbild
    // (NPC.cpp:2359-2368 MD), OpenJK 100 (NPC.cpp:2481-2488) - siehe
    // npcVerhaltenTaktMs.
    int verhaltenTaktMs = 50;
    // rc604 fix604 (JK2): NAVGOAL_USE_RADIUS im goalRadius (ein
    // waypoint_navgoal mit "radius", g_nav.cpp:1356) und SVF_NAVGOAL des
    // tempGoal (ein TAG, NPC_SetMoveGoal isNavGoal, Q3_Interface.cpp:1988).
    bool zielMitRadius = false;
    bool zielNavgoal = true;
    // s.weapon == WP_SABER (NAV_TestBestNode, NAVNEW_MoveToGoal: Jedi springen
    // nicht ueber 64 hinauf).
    bool schwert = false;
    // rc605: SCF_NAV_CAN_JUMP ohne SCF_NO_ACROBATICS (NPC_CanTryJump,
    // NPC_move.cpp:327-342 MD): jede Figur mit FP_LEVITATION > 0 in der .npc
    // und CLASS_HOWLER (NPC_spawn.cpp:588-602 MD / 386-400 OpenJK). Nur dann
    // gelten Sprungkanten (is_valid, g_navigator.cpp:551-556) und STEER
    // versucht NPC_TryJump. weltSchwerkraft ist g_gravity (EvaluateTrajectory
    // TR_GRAVITY, bg_misc.cpp:323-327 MD) neben ps.gravity (schwerkraft).
    bool kannSpringen = false;
    // rc605: SET_NO_ACROBATICS (Q3_SetNoAcrobatics, Q3_Interface.cpp:5623-5647
    // MD): NPC_CanTryJump verweigert jeden Sprung (NPC_move.cpp:330 MD); die
    // Sprungkanten bleiben gueltig (is_valid fragt nur SCF_NAV_CAN_JUMP).
    bool keineAkrobatik = false;
    float weltSchwerkraft = 800.0F;
    // rc605: Verhaltenszustand BS_JUMP (NPC_BSJump statt NPC_MoveToGoal) und
    // die Laengen von BOTH_CROUCH1 und BOTH_LAND1 dieser Figur in ms
    // (ps.legsAnimTimer nach NPC_SetAnim ... SETANIM_FLAG_HOLD).
    bool bsJump = false;
    int hockeMs = 0;
    int landMs = 0;
    // rc605 FU4: NPC_JumpAnimation dieser Figur (npcSprungAnimArt: 0
    // BOTH_FORCEJUMP1, 1 eine PM_FlippingAnim wie BOTH_FLIP_F, 2 BOTH_JUMP1) und
    // ihre Laenge (ps.torsoAnimTimer nach SETANIM_FLAG_HOLD); landMs gilt auch
    // fuer NPC_TryJump (PM_CrashLand), forceLandMs ist BOTH_FORCELAND1.
    int sprungAnimArt = 0;
    int sprungAnimMs = 0;
    int forceLandMs = 0;
};

// rc601 integ: der Takt des Verhaltens eines NPC in ms (LaufWerte::
// verhaltenTaktMs). Movie Duels: FRAMETIME/2, mit aktiver Klinge FRAMETIME/4 -
// NPC_Think laeuft aber nur alle 50 ms, also jedes Denkbild (NPC.cpp:2359-
// 2368 MD). OpenJK: FRAMETIME = 100, nur "Jedi think faster on hard
// difficulty, except low-rank (reborn)": s.weapon == WP_SABER &&
// g_spskill >= 2 && rank > RANK_LT_JG -> FRAMETIME/2 (NPC.cpp:2481-2488).
// rang ist der "rank" der .npc (TranslateRankName, NPC_stats.cpp:222-268:
// unbekannt/fehlt = RANK_CIVILIAN); waffe der WP_-Name in der Hand.
[[nodiscard]] int npcVerhaltenTaktMs(bool movieDuels, const std::string& waffe, int spielstufe, const std::string& rang);

// rc605: die Sprunganimation von NPC_JumpAnimation (NPC_move.cpp:413-511 MD /
// 343-368 OpenJK) aus .npc-Klasse ("CLASS_..."), Waffe in der Hand und Rang;
// g_ActivateAnimationStyle ist 0 (CVAR_ROM, g_main.cpp:991 MD). CLASS_ALORA
// waehlt mit Q_irand(0, 3) zwischen BOTH_ALORA_FLIP_1..3 und BOTH_FLIP_F -
// hier die Mitte der Konvention: BOTH_ALORA_FLIP_2 (Q_irand(0,3) != 0 in drei
// von vier Faellen).
[[nodiscard]] std::string npcSprungAnim(bool movieDuels, const std::string& klasse, const std::string& waffe, const std::string& rang);
// rc605: SCF_NAV_CAN_JUMP beim Spawnen (NPC_spawn.cpp:588-602 MD).
[[nodiscard]] bool npcKannSpringen(const std::string& klasse, int levitation);
// rc605 FU4: LaufWerte::sprungAnimArt zum Namen aus npcSprungAnim.
[[nodiscard]] int npcSprungAnimArt(const std::string& anim);
// rc605 FU4: Codes der Sprungkette (LaufTakt::beinAnim/torsoAnim).
enum SprungAnim : int {
    kSaKeine = 0,       // Beine: PM_Footsteps waehlt; Oberkoerper: folgt den Beinen
    kSaSprung = 1,      // NPC_JumpAnimation (npcSprungAnim)
    kSaJump1 = 2,       // BOTH_JUMP1 (PM_JumpForDir)
    kSaLand1 = 3,       // BOTH_LAND1
    kSaCrouch1 = 4,     // BOTH_CROUCH1 (NPC_BSJump)
    kSaInAir1 = 5,      // BOTH_INAIR1 (NPC_BSJump)
    kSaForceLand1 = 6,  // BOTH_FORCELAND1
    kSaStand1 = 7,      // BOTH_STAND1 (PM_Footsteps im Stand waehrend BS_JUMP)
};

struct LaufTakt {
    double t = 0.0;               // das Denkbild
    NavVec ort{};                 // nach dem Pmove dieses Denkbilds
    int tempo = 0;                // ps.speed nach STEER::DeActivate
    bool bewegt = false;          // ucmd.forwardmove/rightmove != 0
    bool gehTaste = false;        // BUTTON_WALKING aus STEER::DeActivate
    // ... und nach NPC_ApplyScriptFlags: PM_Footsteps waehlt damit BOTH_WALK1
    // oder BOTH_RUN1 (R7 der Durchsicht).
    bool gehAnim = false;
    int stueck = 0;               // wechselt, wenn der Weg um eine Ecke geht
    NavVec richtung{};            // wohin gesteuert wird (Seek-Punkt)
    float zustandWunschGier = 0.0F;   // desiredYaw nach diesem Denkbild
    // rc601 (bewegung, teil 3): wohin die Spuren dieses Denkbilds reichten -
    // der Kasten um alle gezogenen Kaesten, +1 wie SV_Trace/SV_AreaEntities
    // (sv_world.cpp). Nur was diesen Kasten beruehrt, kann eine Spur aendern:
    // so laesst sich beweisen, dass ein Mover, der anders steht, oder eine
    // andere Figur dieses Denkbild nicht aendert.
    NavVec spurMin{1.0e30F, 1.0e30F, 1.0e30F};
    NavVec spurMax{-1.0e30F, -1.0e30F, -1.0e30F};
    NavVec koerperMin{1.0e30F, 1.0e30F, 1.0e30F};   // nur Spuren mit CONTENTS_BODY
    NavVec koerperMax{-1.0e30F, -1.0e30F, -1.0e30F};
    bool kantenGefragt = false;   // is_valid einer Tuer-/Wandkante (oder has_valid_edge) gefragt
    // rc603 fix603: pml.msec des Pmove dieses Denkbilds (bg_pmove.cpp:
    // 33781-33789 MD, 14756-14761 OpenJK: ucmd.serverTime -
    // ps.commandTime, 1..200). NPC_Think
    // laeuft jedes Bild, also 50; nur NPC_Begin und das erste NPC_Think danach
    // rechnen 200 (LaufWerte::spawnVorlauf). Der gemeinsame Takt rechnet ein
    // Denkbild, in dem er abweicht, mit derselben Bildzeit.
    int msec = 50;
    // rc605: in diesem Denkbild sprang die Figur ab (NPC_TryJump -> NPC_Jump
    // setzte ps.velocity vor dem Pmove) - sprungTempo ist dieses shotVel.
    bool sprung = false;
    NavVec sprungTempo{};
    // rc605: BS_JUMP - die Animation von NPC_BSJump nach diesem Denkbild (0 keine,
    // 1 BOTH_CROUCH1, 2 BOTH_INAIR1, 3 BOTH_LAND1) und seit wann.
    int bsAnim = 0;
    double bsAnimAb = 0.0;
    // rc605 FU4: Beine und Oberkoerper der Sprungkette nach diesem Denkbild
    // (SprungAnim; torsoAnim 0: folgt den Beinen) und seit wann.
    int beinAnim = 0;
    double beinAnimAb = 0.0;
    int torsoAnim = 0;
    double torsoAnimAb = 0.0;
    // rc605 (Probe): 1 STEER::GoTo, 2 NAV::GoTo, 3 STEER::Stop; +10 STEER::Blocked
    // in diesem Denkbild (0: kein NPC_MoveToGoal).
    int weg = 0;
};

struct LaufPlan {
    double abMs = 0.0;            // die Aenderung (SET_NAVGOAL ...)
    LaufZustand anfang;
    std::vector<LaufTakt> takte;
    std::vector<LaufZustand> zustaende;   // nach jedem Takt
    double ankunftMs = -1.0;      // UpdateGoal -> NPC_ReachedGoal (TID_MOVE_NAV)
    double ruheMs = -1.0;         // danach steht die Figur (Geschwindigkeit 0)
    bool nie = false;             // kommt nie an: steht und bleibt stehen
    bool zielAnfangsErreicht = false;   // schon im ersten Denkbild da
    // rc601 (bewegung, teil 3): mit Spuren, Boden und Schwerkraft gerechnet -
    // dann ist Z des Plans die Hoehe der Figur (schwerkraft.cpp nimmt sie).
    bool hoeheGilt = false;
};

// Ein Plan ab abMs: das erste Denkbild ist das naechste 50-ms-Bild NACH abMs
// (das Skript laeuft am Ende von NPC_Think, NPC.cpp:2412-2415). Gerechnet
// wird bis zur Ankunft und dem Stillstand danach, bis die Figur nachweislich
// stehen bleibt (nie), oder bis grenzeMs.
[[nodiscard]] LaufPlan planeLauf(const NavWelt& welt, const LaufZustand& start, const LaufWerte& werte, double abMs,
                                 double grenzeMs);
// Der Zustand nach dem letzten Denkbild bis t (sonst der Anfang).
[[nodiscard]] LaufZustand zustandBei(const LaufPlan& p, double t);
// Der Ort zur Zeit t: zwischen zwei Denkbildern linear (wie cgame mittelt).
[[nodiscard]] NavVec ortBei(const LaufPlan& p, double t);

// --- rc601 (bewegung, Durchsicht 2): Befehle wirken im Denkbild ---------------
// ICARUS laeuft am Ende von NPC_Think (NPC.cpp:2412-2415 MD), alle 50 ms. Ein
// Befehl zur Zeit t (SET_NAVGOAL, ein Tempo, SET_ORIGIN ...) wird also im
// ersten Denkbild ab t ausgefuehrt - NACH dessen Bewegung; die naechste
// Bewegung mit dem Neuen kommt ein Denkbild spaeter. Bis zur Durchsicht 2
// schnitten Szene und Ablauf einen Plan genau bei t, mit einem Ort ZWISCHEN
// zwei Denkbildern, und rechneten ab dem naechsten Denkbild schon mit dem
// Neuen. Innerhalb 0,5 ms gilt t als dieses Denkbild.
// rc601 (bewegung, teil 3): viewangles nach einem Denkbild - ANGLE2SHORT
// ("(int)((x)*65536/360) & 65535", q_math.h), als short (PM_UpdateViewAngles:
// "short temp = cmd->angles[i] + ps->delta_angles[i]") und SHORT2ANGLE
// ("(x)*(360.0/65536)"). Ergebnis in [-180, 180).
[[nodiscard]] inline float navGierQuant(float x) {
    const int s = static_cast<int>(x * 65536 / 360) & 65535;
    const auto kurz = static_cast<short>(static_cast<unsigned short>(s));
    return static_cast<float>(kurz * (360.0 / 65536));
}
[[nodiscard]] inline double navDenkbildAb(double t) {
    const double k = std::ceil((t - 0.5) / kNavDenkMs);
    return kNavDenkMs * k;
}

// --- rc601 (bewegung, Durchsicht 2): die Gier aus dem Plan --------------------
// Die Gier zur Zeit t, wie cgame sie zeigt: zwischen zwei Denkbildern
// LerpAngle (q_math.c) - der kuerzere Weg -, vor dem ersten Denkbild die
// Gier des Anfangs. Dieselbe Zeitregel wie ortBei.
[[nodiscard]] float gierBei(const LaufPlan& p, double t);
// TID_ANGLE_FACE in einem Plan: NPC_UpdateAngles meldet "exact" im ersten
// Denkbild, in dem |AngleDelta(viewangles, desiredYaw)| VOR dem Drehen
// hoechstens MIN_ANGLE_ERROR ist (NPC_utils.cpp:340-391 MD) - desiredYaw ist
// dann schon das dieses Denkbilds (STEER, Watchtarget). Die Zeit dieses
// Denkbilds nach t, oder -1, wenn es innerhalb des Plans nicht kommt.
[[nodiscard]] double planGenauAb(const LaufPlan& p, double t);

// Die Welt ohne Karte: das Netz aus den Entities, keine Spuren (alles frei),
// kein Boden. So rechnen Aufrufer ohne Geometrie (Proben, der erste Aufbau).
// Zwischengespeichert nach dem Inhalt der Wegpunkt-Entities.
[[nodiscard]] std::shared_ptr<const NavWelt> navWeltAusEntities(const std::vector<MapEntity>& ents);

// rc601 (bewegung, teil 3): Movie Duels oder reines JKA (OpenJK)? Der
// Betreuer will beide Regelwerke; rc601 integ: gesetzt vom zentralen Schalter
// (bhed/regelwerk.h, regelwerkAnwenden) - ohne ihn Movie Duels. Wo beide Quellen gleich sind, gilt der
// Schalter nicht; die Tabelle steht in NOTIZEN_bewegung.md §10.
void navMovieDuelsRegeln(bool an);
[[nodiscard]] bool navMovieDuelsRegeln();
// rc604 fix604 (JK2 Stufe 3): Jedi Outcast - NPC_MoveToGoal mit dem alten
// Navigator (codeJK2/game/NPC_move.cpp, g_navnew.cpp, g_navigator.cpp) und
// dem Tempo aus ClientThink_real (g_active.cpp:2358-2484) statt STEER/NAV.
// Gesetzt vom zentralen Schalter (regelwerkAnwendenSpiel).
void navJk2Regeln(bool an);
[[nodiscard]] bool navJk2Regeln();
// Com_BlockChecksum (md4.c): MD4, die vier Worte verknuepft - die Pruefsumme,
// mit der CNavigator::Load eine .nav zur .bsp prueft.
[[nodiscard]] int navBlockPruefsumme(const std::string& bytes);
// CNavigator::Load (g_navigator.cpp:567-628) ohne Pruefsummenvergleich; leer,
// wenn die Datei nicht taugt. `summe` bekommt die gespeicherte Pruefsumme.
[[nodiscard]] std::shared_ptr<NavNetzJk2> navJk2Laden(const std::string& bytes, int& summe);

// rc601 (bewegung, Durchsicht 2): Abkuerzungen, die am Ergebnis nichts
// aendern (Zwischenspeicher fuer has_valid_edge je Regionspaar). Fuer die
// Probe, die das Bit fuer Bit zeigt: false rechnet alles wie die Engine
// Schritt fuer Schritt. Vorgabe true.
void navSchnellwegeAn(bool an);
// rc601 (bewegung, Durchsicht 2): wie viele Plaene seit dem Start gerechnet
// (kalt) bzw. aus dem Zwischenspeicher genommen wurden (warm) und wie lange
// das Rechnen dauerte - fuer die Zeile im Protokoll nach prepareScene.
struct NavPlanZaehler {
    long kalt = 0;
    long warm = 0;
    double msKalt = 0.0;
};
[[nodiscard]] NavPlanZaehler navPlanZaehler();

// --- rc601 (bewegung, teil 3): alle Figuren im selben Takt ---------------------
//
// Auftrag: "Other NPCs as obstacles: the engine moves every NPC each frame
// against the others (CONTENTS_BODY in MASK_NPCSOLID, PM_SlideMove against
// their boxes, STEER::AvoidCollisions / SafeToGoTo considering entities)".
// G_RunFrame laesst jede Entity in Nummernfolge denken (g_main.cpp); eine
// Figur sieht die vor ihr also schon nach deren Bewegung in diesem Bild, die
// nach ihr noch vom Bild davor.
//
// Ablauf und Szene planen je Figur (planeLauf, ohne die anderen). Diese
// Rechnung nimmt alle Plaene einer Ablauf-Rechnung (NavAbschnitt) und laesst
// die Figuren Denkbild fuer Denkbild zusammen laufen. Ein Denkbild des Plans
// ohne die anderen wird uebernommen, solange die Figur ihm folgt und kein
// anderer Kasten die Huelle seiner Spuren (LaufTakt::koerperMin/Max)
// beruehrt - dann rechnete die Engine Bit fuer Bit dasselbe. Sonst rechnet
// sie ab dort selbst, mit den Kaesten der anderen (NavWelt::koerper).
struct NavAbschnitt {
    int figur = 0;                 // Index in den Figuren
    double abMs = 0.0;             // wie planeLauf
    double grenzeMs = 0.0;
    double schnittMs = 1.0e300;    // ab hier gilt der naechste Abschnitt dieser Figur
    LaufZustand start;
    LaufWerte werte;
    bool sprung = false;           // beginnt nicht, wo der vorige aufhoerte (Spawn, SET_ORIGIN)
    std::shared_ptr<const LaufPlan> allein;   // planeLauf ohne die anderen
};
struct NavFigurKoerper {
    int reihe = 0;                 // G_RunFrame: Entity-Nummernfolge
    float halbBreite = 16.0F;
    float hoeheOben = 40.0F;
    double daAbMs = -1.0e300;      // CONTENTS_BODY ab (Spawn) ...
    double daBisMs = 1.0e300;      // ... bis (remove, kill: dann CONTENTS_CORPSE)
    std::vector<std::pair<double, NavVec>> orte;   // Ort ohne Plan ab einer Zeit (Spawn, SET_ORIGIN)
    // rc604 fix604: contents und clipmask je Zeit. Bis fix604 war jede Figur
    // von daAbMs bis daBisMs ein Koerper - auch eine unsichtbare (col1 in
    // md_ga_sith intro_sith hielt dok1 sieben Sekunden auf).
    bool sfbNichtFest = false;     // NPC_spawner spawnflags 64 (SFB_NOTSOLID): contents 0 ab NPC_Begin
    std::vector<std::pair<double, signed char>> fest;   // Wechsel ab einer Zeit (kNavFest...), nach der Zeit
};
// NavFigurKoerper::fest - was ein Skriptbefehl an contents/clipmask aendert.
// SET_INVISIBLE true: contents = 0 (Q3_SetInvisible, Q3_Interface.cpp:
// 3101-3128 MD / 2901-2928 OpenJK; SET_INVISIBLE false stellt nichts zurueck).
inline constexpr signed char kNavFestUnsichtbar = 0;
// SET_SOLID false: contents CONTENTS_CORPSE (unsichtbar: 0), clipmask ohne
// CONTENTS_BODY (Q3_SetSolid, Q3_Interface.cpp:6094-6113 MD / 5651-5667 OpenJK).
inline constexpr signed char kNavFestNicht = 1;
// SET_SOLID true: contents CONTENTS_BODY, clipmask mit CONTENTS_BODY - wenn
// SpotWouldTelefrag2 keinen anderen Koerper findet; sonst versucht es
// SolidifyOwner jedes FRAMETIME (100 ms) wieder (Q3_Interface.cpp:6031-6093 MD
// / 5589-5650 OpenJK; SpotWouldTelefrag2 g_client.cpp:320-341 MD / 158 OpenJK).
// Den Befehl selbst haelt das Skript so lange an (TID_RESIZE, Q3_Interface.cpp:
// 9496-9504 MD) - rc605 (fix605): das rechnet der Ablauf mit seinen Orten nach
// (ablauf.cpp, telefrag/solidify).
inline constexpr signed char kNavFestJa = 2;
struct NavKoerperErgebnis {
    std::vector<std::shared_ptr<const LaufPlan>> plaene;   // je Abschnitt
    int geaendert = 0;             // so viele weichen vom Plan ohne die anderen ab
};
[[nodiscard]] NavKoerperErgebnis navKoerperLauf(const NavWelt& welt, const std::vector<NavFigurKoerper>& figuren,
                                                const std::vector<NavAbschnitt>& abschnitte);
// Der Schluessel des Zwischenspeichers fuer einen Plan (dieselben Eingaben =
// derselbe Plan) - Ablauf und Szene finden damit den Plan aus navKoerperLauf.
[[nodiscard]] std::uint64_t navPlanSchluessel(const NavWelt& welt, const LaufZustand& z, const LaufWerte& w, double abMs,
                                              double grenzeMs);
// ... mit dem Namen der Figur (zwei Figuren mit gleichen Eingaben sind im
// gemeinsamen Takt nicht dieselbe).
[[nodiscard]] inline std::uint64_t navKoerperSchluessel(const std::string& figur, std::uint64_t plan) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const char c : figur) { h = (h ^ static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(c)))) * 1099511628211ULL; }
    return h ^ (plan + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}

// --- Einzelne Teile der Engine, fuer Proben und die Oberflaeche ---------------
// NAV::GetNearestNode(position, previous, goal, ignoreEnt, allowZOffset)
[[nodiscard]] int navNaechsterKnoten(const NavWelt& welt, const NavVec& pos, int vorher, int ziel);
// NAV::FindPath bis auf den Pfad: gibt es ein A* vom Knoten a zum Knoten b?
// `pfad` (optional) bekommt die Knoten von a nach b. rc605: `springen` =
// SCF_NAV_CAN_JUMP - dann gelten Sprungkanten (is_valid, g_navigator.cpp:551-556 MD).
[[nodiscard]] bool navAStar(const NavNetz& netz, int a, int b, std::vector<int>* pfad = nullptr, bool springen = false);

}  // namespace bhed
#endif
