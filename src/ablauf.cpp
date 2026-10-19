// ablauf.cpp - ICARUS-Nachbau. Warum und was, steht in bhed/ablauf.h.
//
// Die Stellen im Engine-Quelltext (MovieDuels-SJE-SP, code/icarus und
// code/game), nach denen hier gebaut ist:
//
//   CTaskManager::Go            TaskManager.cpp:689   ein Befehl nach dem
//                                                     anderen, bis einer
//                                                     blockiert
//   CTaskManager::Wait          TaskManager.cpp:1039  Zeit, Aufgabe (Name)
//   CTaskManager::WaitSignal    TaskManager.cpp:1125
//   CSequencer::CheckAffect     Sequencer.cpp:1752    Ziel sofort weiter
//   CSequencer::Affect          Sequencer.cpp:2153    FLUSH / INSERT
//   CSequencer::CheckDo         Sequencer.cpp:1927    do laeuft EINGEREIHT,
//                                                     nicht nebenher
//   CSequencer::CheckLoop       Sequencer.cpp:1580
//   CSequencer::Run             Sequencer.cpp:350     wie INSERT
//   CQuake3GameInterface::Set   Q3_Interface.cpp:8790 welche set-Befehle
//                                                     eine Aufgabe offen
//                                                     halten (Q3_TaskIDSet)
//   PlayIcarusSound             Q3_Interface.cpp:8435 CHAN_VOICE* halten
//                                                     die Aufgabe, bis die
//                                                     Stimme verklungen ist
//   G_UseTargets2               g_utils.cpp:651
//   target_scriptrunner_use     g_target.cpp:849
//   target_relay_use            g_target.cpp:451
//   Use_Target_Delay            g_target.cpp:85
//   target_counter_use          g_target.cpp:624
//   multi_trigger               g_trigger.cpp:113
//   NPC_Spawn                   NPC_spawn.cpp:2843
#include "bhed/ablauf.h"

#include "bhed/clock.h"
#include "bhed/gegenstaende.h"   // rc603: spawntBeiSchwierigkeit (Kartenstart)
#include "bhed/scene.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace bhed {

namespace {

std::string klein(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool gleich(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// Skriptpfade vergleichen wie die Engine sie laedt: ohne Gross/klein,
// Schraegstriche egal, ohne "scripts/" und Endung.
std::string skriptSchluessel(std::string p) {
    p = klein(std::move(p));
    for (char& c : p) {
        if (c == '\\') { c = '/'; }
    }
    if (p.rfind("scripts/", 0) == 0) { p = p.substr(8); }
    for (const char* end : {".ibi", ".txt", ".icarus"}) {
        const std::string e = end;
        if (p.size() > e.size() && p.compare(p.size() - e.size(), e.size(), e) == 0) {
            p.resize(p.size() - e.size());
        }
    }
    return p;
}

bool istNpcSpawner(const MapEntity& e) { return e.classname.rfind("NPC_", 0) == 0; }

// rc603: Kartenstart, Zeiten relativ zum Start der Karte (sv.time 1000).
// SV_SpawnServer laesst die Spiellogik fuenf Bilder zu 100 ms laufen (0, 100,
// 200, 300, 400; sv_init.cpp:313-318 und 350-352 OpenJK, :321-325 und 360-362
// Movie Duels), danach steht sv.time auf 1500, und SV_Frame erhoeht VOR jedem
// Bild um frameMsec (sv_main.cpp:507-513): das erste Bild im Spiel ist 550.
constexpr double kLetztesLadebildMs = 400.0;
constexpr double kErstesSpielbildMs = 550.0;
// Wann der Spieler ins Spiel kommt (ClientBegin -> ClientSpawn -> G_UseTargets
// der Startposition), haengt am Laden des Clients - frueher als nach dem
// ersten Bild im Spiel aber nicht: die Intros sprechen gleich zu Beginn
// Figuren an, die erst NPC_Begin bei ICARUS anmeldet (InitEntity,
// NPC_spawn.cpp:1245), und das Ziel eines affect sucht ICARUS schon beim Run
// (ParseAffect, Sequencer.cpp:693). t2_trip: intro -> affect "introjaden",
// ein NPC_Player ohne targetname (Kartenstart-Spawner, NPC_Begin bei 550).
// Also: am Ende des Bildes 550.
constexpr double kSpielerBeginnMs = kErstesSpielbildMs;

double zahl(const std::string& t, double vorgabe = 0.0) {
    if (t.empty()) {
        return vorgabe;
    }
    char* ende = nullptr;
    const double v = std::strtod(t.c_str(), &ende);
    return (ende == t.c_str()) ? vorgabe : v;
}

bool leseVek(const std::string& t, float out[3]) {
    std::istringstream is(t);
    float a = 0.0F;
    float b = 0.0F;
    float c = 0.0F;
    if (!(is >> a >> b >> c)) {
        return false;
    }
    out[0] = a;
    out[1] = b;
    out[2] = c;
    return true;
}

const std::string* schluessel(const MapEntity& e, const char* key) {
    for (const auto& [k, v] : e.keys) {
        if (gleich(k, key)) {
            return &v;
        }
    }
    return nullptr;
}

std::string wertVon(const MapEntity& e, const char* key) {
    const std::string* v = schluessel(e, key);
    return (v == nullptr) ? std::string{} : *v;
}

// rc603 (Durchsicht): gibt die Spawnfunktion die Entity gleich wieder frei?
// Dann startet sie kein spawnscript: G_SpawnGEntityFromSpawnVars fragt
// ValidEntity erst NACH G_CallSpawn (g_spawn.cpp:1108-1114 OpenJK,
// :1271-1278 Movie Duels), und G_FreeEntity hat die Entity da schon geleert
// (memset, g_utils.cpp:988) - kein behaviorSet, kein script_targetname.
//   * SP_waypoint, SP_waypoint_small, SP_waypoint_navgoal: immer
//     (g_nav.cpp:172-300 OpenJK, :162-283 Movie Duels; NAV::SpawnedPoint,
//     dann G_FreeEntity).
//   * SP_info_null (auch "func_group"): mit spawnflags 1 sofort, sonst erst
//     bei START_TIME_REMOVE_ENTS - dann laeuft das spawnscript
//     (g_misc.cpp:54-65 OpenJK, :50-63 Movie Duels).
//   * SP_light ohne targetname (g_misc.cpp:164-169 OpenJK, :165-172 MD).
// In den Daten: t1_fatal waypoint_navgoal "riposte1" (t1_fatal/riposte1),
// vjun1 waypoint_navgoal "haztroopernav" (common/nav_goto).
bool freiBeimSpawnen(const MapEntity& e) {
    const std::string& cn = e.classname;
    if (gleich(cn, "waypoint") || gleich(cn, "waypoint_small") || gleich(cn, "waypoint_navgoal")) {
        return true;
    }
    if (gleich(cn, "info_null") || gleich(cn, "func_group")) {
        return (std::atoi(wertVon(e, "spawnflags").c_str()) & 1) != 0;
    }
    if (gleich(cn, "light")) {
        return e.targetname.empty();
    }
    return false;
}

// Die Aufgabenfaecher einer Entity (taskID_t, g_public.h). Ein neuer
// Befehl desselben Fachs erledigt den alten SOFORT (Q3_TaskIDSet ruft
// Q3_TaskIDComplete auf das alte).
enum Fach : int { kStimme = 0, kWeg, kBlick, kOben, kUnten, kBeide, kFaecher };

struct Aufgabe {
    int id = -1;
    double fertig = 0.0;   // wann sie von selbst fertig wird
    bool nie = false;      // haelt ewig (holdtime -1)
};

struct Rahmen {
    enum class Art : std::uint8_t { Folge, Task, Schleife };
    const std::vector<Node>* liste = nullptr;
    std::size_t i = 0;
    int skript = 0;
    Path basis;            // Pfad des Knotens, dessen Kinder `liste` sind
    Art art = Art::Folge;
    int runden = 0;        // Schleife: noch so viele (-1 = endlos)
    int gruppe = -1;       // Task: welche Gruppe
    bool dannWarten = false;   // dowait: nach dem Task auf die Gruppe warten
    // Kam per affect aus einer Endlosschleife (vjun2: jede Runde schickt
    // "fallenspark" einen neuen Block) - dann ist auch dieser Block endlos.
    bool ausEndlos = false;
    const Node* dowaitKnoten = nullptr;
    AblaufHerkunft dowaitHerkunft;
    // INSERT und run holen ein laufendes wait zurueck (CSequencer::Recall);
    // es beginnt danach VON VORN. Hier liegt es, bis der eingeschobene
    // Block fertig ist.
    bool hatZurueck = false;
    int zurueckArt = 0;
    double zurueckDauer = 0.0;
    std::string zurueckName;
    const Node* zurueckKnoten = nullptr;
    AblaufHerkunft zurueckHerkunft;
};

struct Eintrag {
    double ms = 0.0;
    double ende = -1.0;       // bei Wartebefehlen: wann fertig (-1 = nie)
    int wesen = 0;
    const Node* knoten = nullptr;
    AblaufHerkunft herkunft;
    bool warten = false;
    std::string text;         // bei eingefuegten Befehlen: der Zielname
    int folge = 0;
};

struct Wesen {
    std::string name;
    int reihe = 0;                      // Reihenfolge im Bild (Entity-Nummer)
    const MapEntity* ent = nullptr;
    bool figur = false;
    bool gibt = true;
    std::vector<Rahmen> stapel;
    // Blockiert?
    enum Warte : int { kNichts = 0, kZeit, kSignal, kGruppe };
    int warte = kNichts;
    double stempel = 0.0;
    double dauer = 0.0;
    std::string warteName;
    const Node* warteKnoten = nullptr;
    AblaufHerkunft warteHerkunft;
    int warteEintrag = -1;
    // Tasks, die in den geladenen Bloecken erklaert sind: Name -> Knoten.
    struct TaskDef {
        const Node* knoten = nullptr;
        int skript = 0;
        Path path;
    };
    std::map<std::string, TaskDef> tasks;
    std::map<std::string, int> gruppeVon;   // Taskname -> zuletzt gestartete Gruppe
    std::vector<int> gruppen;               // verschachtelte laufende Tasks
    std::array<Aufgabe, kFaecher> fach{};
    // Die Welt, soweit die Zeiten davon abhaengen.
    float ort[3]{};
    bool ortBekannt = false;
    bool gehend = false;
    bool rennend = false;
    float gehTempo = -1.0F;
    float laufTempo = -1.0F;
    std::map<std::string, std::string> parm;
    std::map<std::string, std::string> verhalten;   // "deathscript" -> Pfad
    // target_scriptrunner / target_relay / trigger: Zaehler und Sperre.
    int zaehler = 0;
    bool zaehlerGesetzt = false;
    double gesperrtBis = -1.0;
    bool inaktiv = false;
    bool verbraucht = false;
    // Der letzte Block kam aus einer Endlosschleife: auch was er anstiess
    // (ein move, der noch laeuft), zaehlt nicht als Taetigkeit mit Ende.
    bool endlosGefuettert = false;
    // G_FreeEntity: ein misc_model_breakable mit "no dmodel" nach dem Bruch.
    // G_Find findet es nicht mehr, ein use geht ins Leere.
    bool entfernt = false;
    // --- Paket bewegung (rc601): das Laufziel, wie in scene.cpp ----------
    //
    // NPCInfo->goalEntity mit dem Weg dorthin. Der Weg wird wie in der
    // Szene gerechnet (planeLauf je Denkbild, bhed/navlauf.h, auch BS_NOCLIP)
    // - sonst endete dowait zu einer anderen Zeit, als die Figur ankommt.
    bool zielGesetzt = false;
    bool zielMarke = true;      // TAG: die Aufgabe des SET_NAVGOAL haelt (TID_MOVE_NAV)
    float zielOrt[3]{};
    double zielRadius = 12.0;
    bool wegLaeuft = false;     // von wegVon nach zielOrt in [wegAb, wegBis]
    float wegVon[3]{};
    double wegAb = 0.0;
    double wegBis = 0.0;
    bool wegInFahrt = false;
    double fahrtBis = -1.0;     // bis hierhin lief ein abgeschnittener Weg
    int wegId = -1;             // die Aufgabe dieses Ziels im Fach kWeg
    // rc601 (bewegung, Wege wie im Spiel): der laufende Plan (planeLauf) und
    // der Zustand der Figur fuer NAV/STEER/Pmove (bhed/navlauf.h).
    std::shared_ptr<const LaufPlan> plan;
    LaufZustand lauf;
    bool gierAusPlan = false;
    // --- Paket bewegung (rc601): Verhalten und Drehen ----------------------
    // SET_BEHAVIOR_STATE, gross ("" = BS_DEFAULT). spawnflags 32 am
    // NPC_spawner: BS_CINEMATIC (NPC_SetMiscDefaultData, NPC_spawn.cpp:315).
    std::string verhaltenZustand;
    // Der Spieler selbst ist kein NPC (kein Spawner, Name "player").
    bool echterSpieler = false;
    // Sicher ein client (NPC aus einem Spawner, oder der Spieler) - nicht nur
    // ohne Karte als Figur angenommen. Lerp2Pos/Lerp2Angles lehnen nur
    // clients ab.
    bool istClient = false;
    // Der Gierwinkel, so weit TID_ANGLE_FACE davon abhaengt (SET_DYAW,
    // SET_VIEWTARGET): wie GierLauf in der Szene.
    GierLauf gier;
    // rc601 (bewegung, Durchsicht 2): stats.yawSpeed als Grad/s und die Waffe
    // in der Hand ("" = die beim Spawnen) - fuer AblaufWelt::drehrateNpc.
    float yawRate = -1.0F;
    std::string waffe;
    bool watchAn = false;
    bool watchSelbst = false;
    float watchOrt[3]{};
    // rc601 (bewegung, teil 3): SET_GRAVITY (< 0: g_gravity der Karte) und ob
    // der Vorlauf ab dem Spawn schon eingetragen ist (wie scene.cpp beim
    // ersten Plan).
    float schwerkraft = -1.0F;
    bool laufBereit = false;
    // rc601 (bewegung, teil 3): fuer den gemeinsamen Takt aller Figuren
    // (navKoerperLauf) - ihre Plaene (Ablauf::laufAbschnitte), wann sie
    // verschwindet (remove/kill) und wohin SET_ORIGIN sie setzte.
    int koerperNr = -1;            // Index in Ablauf::laufFiguren
    int abschnitt = -1;          // ihr letzter Abschnitt
    double schnitt = -1.0e300;   // dort endete er (wegAnhalten)
    double totMs = 1.0e300;
    std::vector<std::pair<double, NavVec>> orteLog;
    // rc603 fix603: der Ort aus der Karte (origin des Spawners/der Entity),
    // falls bekannt - dort steht eine Figur ohne Plan, bis orteLog sie
    // versetzt. Der echte Spieler hat keinen.
    bool anfangBekannt = false;
    NavVec anfang{};
};

struct Geplant {
    double ms = 0.0;
    int folge = 0;
    enum class Art : std::uint8_t { Benutzen, Skript, Spawn } art = Art::Benutzen;
    std::string name;         // Benutzen: targetname; Skript: Pfad
    int wesen = -1;           // Skript/Spawn: auf wem
    int ausloeser = -1;
};

class Nachbau {
public:
    Nachbau(const AblaufWelt& w, Ablauf& a, std::set<std::string> vorhanden)
        : welt_(w), aus_(a), vorhanden_(std::move(vorhanden)) {}

    // Figuren, deren Spawner in diesem Ablauf benutzt wurde (klein).
    std::set<std::string> gespawnt;
    // Figuren, die angesprochen wurden, als es sie nicht gab (klein).
    std::set<std::string> vermisst;

    void lauf(const Script& start, const std::string& startPfad);
    // rc603: die Karte vom Laden an (simuliereKartenstart).
    void kartenstartLauf();

private:
    void takte();
    void spielerBeginnt();
    bool kartenstartGeplant() const;
    const AblaufWelt& welt_;
    Ablauf& aus_;
    // rc601 (bewegung, Wege): Wegnetz und Spuren, beim ersten Laufziel geholt.
    mutable std::shared_ptr<const NavWelt> nav_;
    // Figuren, die schon vor dem Ablauf im Spiel stehen (zweiter Lauf).
    std::set<std::string> vorhanden_;
    std::vector<std::unique_ptr<Wesen>> wesen_;
    std::map<std::string, int> wesenNach_;   // klein(name) -> Index
    std::map<std::pair<int, int>, int> starts_;   // (Entity, Skript) -> wie oft gestartet
    std::map<const MapEntity*, int> wesenVonEnt_;
    std::vector<Eintrag> eintraege_;
    std::set<std::string> signale_;
    std::map<std::string, std::string> variablen_;   // klein -> Wert
    std::set<std::string> deklariert_;
    std::vector<Geplant> geplant_;
    std::vector<const Script*> geladen_;             // parallel zu aus_.skripte
    std::map<std::string, int> skriptNach_;
    std::set<std::string> hinweisGesehen_;
    int naechsteId_ = 1;
    std::map<int, std::vector<int>> gruppenAufgaben_;   // Gruppe -> Aufgaben
    std::set<int> erledigt_;
    int naechsteGruppe_ = 1;
    int folge_ = 0;
    double jetzt_ = 0.0;
    int traeger_ = -1;
    int tiefe_ = 0;
    // rc603: Kartenstart (simuliereKartenstart) statt eines Startskripts.
    bool kartenstart_ = false;
    bool spielerDa_ = false;   // ClientSpawn ist gelaufen
    int startFolge_ = 0;

    void hinweis(const std::string& h) {
        if (hinweisGesehen_.insert(h).second && aus_.hinweise.size() < 200) {
            aus_.hinweise.push_back(h);
        }
    }

    int skriptIndex(const std::string& pfad, const Script* s) {
        const std::string k = skriptSchluessel(pfad);
        const auto it = skriptNach_.find(k);
        if (it != skriptNach_.end()) {
            return it->second;
        }
        const int i = static_cast<int>(aus_.skripte.size());
        aus_.skripte.push_back(pfad);
        aus_.skriptBeginn.push_back(-1.0);   // rc603
        aus_.skriptFolge.push_back(-1);
        geladen_.push_back(s);
        skriptNach_[k] = i;
        return i;
    }

    // rc603: eine Figur ohne NPC_targetname (nur im Kartenstart): fuer ICARUS
    // namenlos, ihr spawnscript laeuft aber (ValidEntity). Der Name ist nur
    // die Kennung hier - kein Skript kann ihn ansprechen.
    std::string namenloseFigur(const MapEntity& e) {
        const std::string name = "\x02npc" + std::to_string(reiheVon(&e));
        if (wesenNach_.find(klein(name)) == wesenNach_.end()) {
            auto f = std::make_unique<Wesen>();
            f->name = name;
            f->ent = &e;
            f->reihe = reiheVon(&e);
            f->figur = true;
            f->istClient = true;
            f->gibt = false;
            bereite(*f, e);
            wesenNach_[klein(name)] = static_cast<int>(wesen_.size());
            wesen_.push_back(std::move(f));
        }
        return name;
    }

    // rc603 (Durchsicht): die Figur eines weiteren Spawners, der denselben
    // NPC_targetname traegt wie ein frueherer (nur im Kartenstart). Jeder
    // Spawner erzeugt seine eigene Figur, und NPC_Begin startet IHR
    // spawnscript (G_ActivateBehavior( ent, BSET_SPAWN ), NPC_spawn.cpp:1290
    // OpenJK, :2038 Movie Duels) - t1_fatal: drei NPC_ImpWorker "npc_corpse1"
    // mit spawncorpse, spawncorpse2 und spawncorpse3. Vorher bekam der zweite
    // und dritte die Figur des ersten, ihr Skript startete nie.
    // Den Namen behaelt bei ICARUS die zuletzt angemeldete Figur:
    // AssociateEntity schreibt m_EntityList[name] = s.number ueber
    // (Q3_Interface.cpp:7605 OpenJK, :8135 Movie Duels). Alle melden sich im
    // selben Bild an (NPC_Begin bei 550, in der Reihenfolge der Spawner) -
    // affect und kill treffen danach die des spaeteren Spawners.
    int weitereFigur(const MapEntity& e, const std::string& name) {
        auto f = std::make_unique<Wesen>();
        f->name = name;
        f->ent = &e;
        f->reihe = reiheVon(&e);
        f->figur = true;
        f->istClient = true;
        f->gibt = false;   // erst ab NPC_Begin (Geplant::Art::Spawn)
        bereite(*f, e);
        drehrateVorgabe(*f);
        const int i = static_cast<int>(wesen_.size());
        wesen_.push_back(std::move(f));
        wesenNach_[klein(name)] = i;
        return i;
    }

    // rc603: das Skript `idx` laeuft jetzt los - der erste Start zaehlt (die
    // Reihenfolge der Reiter beim Laden einer Mission, skripteNachStart).
    void notiereStart(int idx) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= aus_.skriptBeginn.size()) {
            return;
        }
        if (aus_.skriptBeginn[static_cast<std::size_t>(idx)] < 0.0) {
            aus_.skriptBeginn[static_cast<std::size_t>(idx)] = jetzt_;
            aus_.skriptFolge[static_cast<std::size_t>(idx)] = startFolge_++;
        }
    }

    const Script* ladeSkript(const std::string& pfad, int* index) {
        const std::string k = skriptSchluessel(pfad);
        const auto it = skriptNach_.find(k);
        if (it != skriptNach_.end()) {
            *index = it->second;
            return geladen_[static_cast<std::size_t>(it->second)];
        }
        const Script* s = welt_.skript ? welt_.skript(k) : nullptr;
        if (s == nullptr) {
            hinweis("Skript \"" + pfad + "\" nicht gefunden");
            return nullptr;
        }
        *index = skriptIndex(k, s);
        return s;
    }

    // --- Entities ---------------------------------------------------------
    const MapEntity* findeKarte(const std::string& name, bool* spawner) const {
        *spawner = false;
        if (welt_.karte == nullptr) {
            return nullptr;
        }
        // NPC_targetname zuerst: die erzeugte Figur traegt ihn als eigenen
        // targetname (NPC_spawn.cpp:1688).
        for (const MapEntity& e : welt_.karte->entities) {
            if (!istNpcSpawner(e)) {
                continue;
            }
            const std::string* t = schluessel(e, "NPC_targetname");
            if (t != nullptr && gleich(*t, name)) {
                *spawner = true;
                return &e;
            }
        }
        for (const MapEntity& e : welt_.karte->entities) {
            if (!istNpcSpawner(e) && gleich(icarusName(e), name)) {
                return &e;
            }
        }
        return nullptr;
    }

    // Unter welchem Namen ICARUS eine Karten-Entity kennt.
    //
    // CQuake3GameInterface::ValidEntity (Q3_Interface.cpp:8096): nur wer
    // einen script_targetname hat, ODER ein Verhaltensskript (usescript,
    // spawnscript ...) - dann gilt der targetname. Eine Tuer, die nur einen
    // targetname traegt, ist fuer affect NICHT zu finden (GetByName sucht
    // in m_EntityList, und dort steht sie nicht). use dagegen sucht ueber
    // targetname (G_UseTargets2) und findet sie.
    //
    // Gefunden am 27.9. an 278 affect-Zielen: md_dd_jedi "falcon2",
    // md_dotf_jedi "al"/"ar", kor2 "script_bigdoor_l" sind func_static mit
    // script_targetname und OHNE targetname - bisher bewegten sie sich nie.
    static std::string icarusName(const MapEntity& e) {
        const std::string* st = schluessel(e, "script_targetname");
        if (st != nullptr && !st->empty()) {
            return *st;
        }
        if (e.targetname.empty()) {
            return {};
        }
        for (const auto& [k, v] : e.keys) {
            const std::string kk = klein(k);
            if (!v.empty() && kk.size() > 6 && kk.compare(kk.size() - 6, 6, "script") == 0) {
                return e.targetname;
            }
        }
        return {};
    }

    // Die Buchfuehrung einer Karten-Entity (Zaehler, Sperren, usescript),
    // angesprochen ueber use. Ist sie auch fuer ICARUS da, dieselbe wie
    // unter ihrem Namen.
    int wesenFuer(const MapEntity& e) {
        const auto it = wesenVonEnt_.find(&e);
        if (it != wesenVonEnt_.end()) {
            return it->second;
        }
        const std::string ic = istNpcSpawner(e) ? std::string{} : icarusName(e);
        if (!ic.empty()) {
            const int wi = wesen(ic);
            if (wi >= 0 && wesen_[static_cast<std::size_t>(wi)]->ent == &e) {
                wesenVonEnt_[&e] = wi;
                return wi;
            }
        }
        auto w = std::make_unique<Wesen>();
        w->name = e.targetname.empty() ? e.classname : e.targetname;
        w->ent = &e;
        w->reihe = reiheVon(&e);
        w->gibt = !ic.empty();   // nur mit ICARUS-Namen hat sie einen Ablauf
        bereite(*w, e);
        drehrateVorgabe(*w);
        const int i = static_cast<int>(wesen_.size());
        wesen_.push_back(std::move(w));
        wesenVonEnt_[&e] = i;
        return i;
    }

    // rc601 (bewegung, R5): die Drehrate beim Spawnen wie in der Szene (.npc
    // yawSpeed, feste Rate des Rocket Troopers / WP_EMPLACED_GUN).
    void drehrateVorgabe(Wesen& w) const {
        if (welt_.drehrateNpc && welt_.drehrateYaw) {
            // rc601 (bewegung, Durchsicht 2): je Waffe (npcDrehrate).
            const float y = welt_.drehrateYaw(w.name);
            if (y > 0.0F) {
                w.yawRate = y;
                w.gier.rate = welt_.drehrateNpc(w.name, w.waffe, y);
            }
            return;
        }
        if (welt_.drehrate) {
            const float r = welt_.drehrate(w.name);
            if (r > 0.0F) { w.gier.rate = r; }
        }
    }

    // Schluessel der Karte in die Buchfuehrung uebernehmen.
    static void bereite(Wesen& w, const MapEntity& e) {
        if (!e.origin.empty()) {
            w.ortBekannt = leseVek(e.origin, w.ort);
            w.anfangBekannt = w.ortBekannt;   // rc603 fix603
            w.anfang = {w.ort[0], w.ort[1], w.ort[2]};
        }
        // rc601 (bewegung): Startwinkel ("angles" oder "angle", der spaetere
        // gewinnt - G_ParseField) und der Verhaltenszustand beim Spawnen.
        if (istNpcSpawner(e)) {
            for (const auto& [k, v] : e.keys) {
                const std::string kk = klein(k);
                float a[3] = {0.0F, 0.0F, 0.0F};
                if (kk == "angles" && leseVek(v, a)) {
                    w.gier.setzen(0.0, a[1]);
                } else if (kk == "angle") {
                    w.gier.setzen(0.0, static_cast<float>(zahl(v)));
                }
            }
            const std::string sf = wertVon(e, "spawnflags");
            if (!sf.empty() && (std::atoi(sf.c_str()) & 32) != 0) {
                w.verhaltenZustand = "BS_CINEMATIC";
            }
        }
        for (const char* key : {"spawnscript", "usescript", "deathscript", "painscript", "awakescript",
                                "angerscript", "fleescript", "lostenemyscript", "victoryscript",
                                "blockedscript", "attackscript", "delayedscript"}) {
            const std::string v = wertVon(e, key);
            if (!v.empty()) {
                w.verhalten[key] = v;
            }
        }
        if (e.classname == "target_scriptrunner" || e.classname == "target_relay" ||
            e.classname == "trigger_once" || e.classname == "trigger_multiple" ||
            e.classname == "target_counter" || e.classname == "target_delay") {
            const std::string c = wertVon(e, "count");
            w.zaehler = c.empty() ? 0 : std::atoi(c.c_str());
            const std::string sf = wertVon(e, "spawnflags");
            const int flags = sf.empty() ? 0 : std::atoi(sf.c_str());
            if ((flags & 128) != 0 && e.classname != "target_counter") {
                w.inaktiv = true;   // INACTIVE
            }
        }
    }

    int reiheVon(const MapEntity* e) const {
        if (e == nullptr || welt_.karte == nullptr) {
            return 1 << 20;
        }
        return static_cast<int>(e - welt_.karte->entities.data()) + 1;
    }

    // Die Entity dieses Namens; legt sie beim ersten Mal an. -1, wenn es
    // sie im Spiel (noch) nicht gibt.
    int wesen(const std::string& name, bool anlegen = true) {
        const std::string k = klein(name);
        const auto it = wesenNach_.find(k);
        if (it != wesenNach_.end()) {
            return it->second;
        }
        if (!anlegen) {
            return -1;
        }
        bool spawner = false;
        const MapEntity* e = findeKarte(name, &spawner);
        auto w = std::make_unique<Wesen>();
        w->name = name;
        w->ent = e;
        w->reihe = reiheVon(e);
        if (k == "player") {
            w->figur = true;
            w->reihe = 0;
            w->gibt = true;
            w->echterSpieler = (e == nullptr);   // rc601 (bewegung): kein NPC_Player-Spawner
            w->istClient = true;
        } else if (e == nullptr) {
            // Unbekannt: im Spiel liefe affect ins Leere. Wir legen sie
            // trotzdem an (sonst kann man ein neues Skript ohne Karte gar
            // nicht ansehen), aber nur, wenn keine Karte da ist.
            w->gibt = (welt_.karte == nullptr || welt_.karte->entities.empty());
            w->figur = w->gibt;
        } else if (spawner) {
            w->figur = true;
            w->istClient = true;   // rc601 (bewegung)
            // Ein Spawner OHNE targetname erzeugt die Figur beim Laden der
            // Karte (SP_NPC_spawner, NPC_spawn.cpp:3032). Mit targetname
            // erst, wenn er benutzt wird - es sei denn, das geschah schon
            // VOR dieser Zwischensequenz (vorhanden_, zweiter Lauf).
            w->gibt = e->targetname.empty() || vorhanden_.count(k) != 0;
            // rc603: im Kartenstart gibt es sie erst, wenn ihr Spawner
            // gedacht hat (NPC_spawn.cpp:2049 OpenJK: START_TIME_REMOVE_ENTS
            // + 50) - dann laeuft auch ihr spawnscript (kartenstartLauf).
            if (kartenstart_ && vorhanden_.count(k) == 0) {
                w->gibt = false;
            }
        }
        if (e != nullptr) {
            bereite(*w, *e);
            drehrateVorgabe(*w);
        }
        const int i = static_cast<int>(wesen_.size());
        wesen_.push_back(std::move(w));
        wesenNach_[k] = i;
        if (e != nullptr && !spawner) {
            wesenVonEnt_.emplace(e, i);
        }
        return i;
    }

    // --- Aufgaben und Gruppen ---------------------------------------------
    int neueAufgabe(Wesen& w) {
        const int id = naechsteId_++;
        if (!w.gruppen.empty()) {
            gruppenAufgaben_[w.gruppen.back()].push_back(id);
        }
        return id;
    }

    void erledige(int id) {
        if (id >= 0) {
            erledigt_.insert(id);
        }
    }

    bool gruppeFertig(int g) const {
        const auto it = gruppenAufgaben_.find(g);
        if (it == gruppenAufgaben_.end()) {
            return true;
        }
        return std::all_of(it->second.begin(), it->second.end(),
                           [this](int id) { return erledigt_.count(id) != 0; });
    }

    void setzeFach(Wesen& w, int f, int id, double fertig, bool nie) {
        if (w.fach[static_cast<std::size_t>(f)].id >= 0) {
            erledige(w.fach[static_cast<std::size_t>(f)].id);   // Q3_TaskIDSet: das alte ist fertig
        }
        w.fach[static_cast<std::size_t>(f)] = Aufgabe{id, fertig, nie};
    }

    void leereFach(Wesen& w, int f) { w.fach[static_cast<std::size_t>(f)] = Aufgabe{}; }

    // Laeuft in diesem Fach etwas, das die Gruppe noch aufhaelt?
    void pruefeFaecher(Wesen& w, double t) {
        for (std::size_t f = 0; f < w.fach.size(); ++f) {
            Aufgabe& a = w.fach[f];
            if (a.id < 0 || a.nie || a.fertig > t) {
                continue;
            }
            // Animation: BEIDE erst, wenn oben und unten fertig sind
            // (bg_panimate.cpp:5922 ff.). Bei uns enden beide zugleich.
            //
            // rc601 (bewegung): ist es der Weg zum Ziel, ist die Figur
            // angekommen - NPC_ReachedGoal loescht das Ziel (NPC_goal.cpp:
            // 141-164).
            if (f == static_cast<std::size_t>(kWeg) && a.id == w.wegId) {
                angekommen(w);
            }
            erledige(a.id);
            a = Aufgabe{};
        }
    }

    // --- Paket bewegung (rc601): der Weg zum Ziel --------------------------
    //
    // rc601 (bewegung, Wege wie im Spiel): wie in scene.cpp mit planeLauf
    // (bhed/navlauf.h) - DIESELBE Rechnung Denkbild fuer Denkbild, damit
    // dowait genau in dem Denkbild endet, in dem die Figur der Szene ankommt
    // (UpdateGoal -> NPC_ReachedGoal -> TID_MOVE_NAV). Findet die Figur keinen
    // Weg, bleibt die Aufgabe offen - im Spiel haengt ein dowait dann ewig.
    static constexpr double kGleichzeitig = 0.5;   // ms: dasselbe Bild

    static void angekommen(Wesen& w) {
        w.zielGesetzt = false;
        w.wegLaeuft = false;
        w.wegId = -1;
    }

    // Wo die Figur zur Zeit t ist: aus dem laufenden Plan, sonst w.ort.
    static void ortJetzt(const Wesen& w, double t, float o[3]) {
        if (!w.plan) {
            for (int a = 0; a < 3; ++a) { o[a] = w.ort[a]; }
            return;
        }
        const NavVec p = ortBei(*w.plan, t);
        for (int a = 0; a < 3; ++a) { o[a] = p[static_cast<std::size_t>(a)]; }
    }

    // rc601 (bewegung, Durchsicht 2): der Dreh-Regler (w.gier) aus dem Plan.
    // Waehrend eines Plans dreht NPC_UpdateAngles nach dem desiredYaw, das
    // STEER je Denkbild setzt - der Plan rechnet es (LaufZustand::gier,
    // wunschGier). Vorher nahm der Ablauf die Gerade vom Start zum Ziel an:
    // nach einem Lauf ueber Eck endete ein dowait auf SET_DYAW zu frueh.
    // Uebernommen wird der Zustand nach dem letzten Denkbild bis t.
    static void planGierSync(Wesen& w, double t) {
        if (!w.plan) {
            return;
        }
        const LaufPlan& p = *w.plan;
        double ts = p.abMs;
        const LaufZustand* z = &p.anfang;
        for (std::size_t i = 0; i < p.takte.size(); ++i) {
            if (p.takte[i].t > t + kGleichzeitig) { break; }
            ts = p.takte[i].t;
            z = &p.zustaende[i];
        }
        w.gier.setzen(ts, z->gier);
        w.gier.zielAb(ts, z->wunschGier);
    }

    // Den laufenden Plan zur Zeit t anhalten; w.ort und w.lauf sind danach
    // der Ort und der Zustand jetzt. Die Rueckgabe ist ohne Bedeutung.
    static bool wegAnhalten(Wesen& w, double tBefehl) {
        if (!w.plan) {
            return false;
        }
        // rc601 (bewegung, Durchsicht 2): im Denkbild des Befehls, wie scene.cpp.
        const double t = navDenkbildAb(tBefehl);
        w.schnitt = t;   // rc601 (bewegung, teil 3)
        planGierSync(w, t);   // rc601 (bewegung, Durchsicht 2)
        const std::shared_ptr<const LaufPlan> plan = w.plan;
        w.plan.reset();
        if (t <= plan->abMs + kGleichzeitig) {
            w.lauf = plan->anfang;   // im selben Bild: noch kein Denkbild
        } else {
            w.lauf = zustandBei(*plan, t);
            w.lauf.ort = ortBei(*plan, t);
            w.gierAusPlan = true;
        }
        for (int a = 0; a < 3; ++a) { w.ort[a] = w.lauf.ort[static_cast<std::size_t>(a)]; }
        if (w.zielGesetzt && plan->ankunftMs >= 0.0 && t >= plan->ankunftMs - kGleichzeitig) {
            angekommen(w);
        }
        w.wegLaeuft = false;
        return false;
    }

    // Ab w.ort zur Zeit t planen (STEER::Activate: mMaxSpeed aus Gangart und
    // Tempo, g_navigator.cpp:3983-3997). Tempo 0: die Figur steht, das Ziel
    // bleibt. Schon im Radius: im ersten Denkbild angekommen.
    // rc601 (bewegung, teil 3): einen Plan fuer den gemeinsamen Takt merken.
    void abschnittMerken(Wesen& w, double ab, double grenze, const LaufZustand& z, const LaufWerte& werte, bool sprung,
                         const std::shared_ptr<const LaufPlan>& allein, std::uint64_t schluessel) const {
        if (w.koerperNr < 0) {
            w.koerperNr = static_cast<int>(aus_.laufFiguren.size());
            aus_.laufFiguren.emplace_back();
            aus_.laufFigurNamen.push_back(w.name);
        }
        if (w.abschnitt >= 0) {
            NavAbschnitt& vor = aus_.laufAbschnitte[static_cast<std::size_t>(w.abschnitt)];
            vor.schnittMs = std::min(vor.schnittMs, std::max(w.schnitt, ab));
        }
        NavAbschnitt a;
        a.figur = w.koerperNr;
        a.abMs = ab;
        a.grenzeMs = grenze;
        a.start = z;
        a.werte = werte;
        a.sprung = sprung;
        a.allein = allein;
        w.abschnitt = static_cast<int>(aus_.laufAbschnitte.size());
        w.schnitt = 1.0e300;
        aus_.laufAbschnitte.push_back(std::move(a));
        aus_.laufSchluessel.push_back(schluessel);
    }
    // Am Ende: die Kaesten, die Zeiten und Orte jeder Figur.
    void koerperAbschliessen() {
        for (const auto& wp : wesen_) {
            Wesen& w = *wp;
            if (w.koerperNr < 0) { continue; }
            if (w.abschnitt >= 0 && w.schnitt < 1.0e299) {
                NavAbschnitt& vor = aus_.laufAbschnitte[static_cast<std::size_t>(w.abschnitt)];
                vor.schnittMs = std::min(vor.schnittMs, std::max(w.schnitt, vor.abMs));
            }
            NavFigurKoerper& f = aus_.laufFiguren[static_cast<std::size_t>(w.koerperNr)];
            f.reihe = w.reihe;
            f.halbBreite = welt_.halbBreite ? welt_.halbBreite(w.name) : 16.0F;
            f.hoeheOben = welt_.hoeheOben ? welt_.hoeheOben(w.name) : 40.0F;
            const auto it = aus_.erscheint.find(w.name);
            f.daAbMs = it != aus_.erscheint.end() ? it->second : -1.0e300;
            f.daBisMs = w.totMs;
            f.orte = w.orteLog;
            // Vor dem ersten Plan steht sie, wo dieser beginnt.
            for (const NavAbschnitt& a : aus_.laufAbschnitte) {
                if (a.figur == w.koerperNr) {
                    f.orte.insert(f.orte.begin(), {-1.0e299, a.start.ort});
                    break;
                }
            }
        }
        // Figuren ohne Plan stehen den anderen auch im Weg: ihr Ort.
        for (const auto& wp : wesen_) {
            Wesen& w = *wp;
            // rc603 fix603: nicht w.ort - das ist ihr Ort am ENDE des Ablaufs
            // (etwa nach einem SET_COPY_ORIGIN), und er galt ab Beginn. Im
            // Spiel steht ihr Kasten bis zum Versetzen (G_SetOrigin +
            // linkentity, Q3_SetOrigin Q3_Interface.cpp:1309) am Ort aus der
            // Karte; ohne den (echter Spieler) erst ab dem ersten Versetzen.
            if (w.koerperNr >= 0 || !w.istClient || !w.gibt || (!w.anfangBekannt && w.orteLog.empty())) { continue; }
            NavFigurKoerper f;
            f.reihe = w.reihe;
            f.halbBreite = welt_.halbBreite ? welt_.halbBreite(w.name) : 16.0F;
            f.hoeheOben = welt_.hoeheOben ? welt_.hoeheOben(w.name) : 40.0F;
            const auto it = aus_.erscheint.find(w.name);
            f.daAbMs = it != aus_.erscheint.end() ? it->second : -1.0e300;
            f.daBisMs = w.totMs;
            f.orte = w.orteLog;
            if (w.anfangBekannt) { f.orte.insert(f.orte.begin(), {-1.0e299, w.anfang}); }
            w.koerperNr = static_cast<int>(aus_.laufFiguren.size());
            aus_.laufFiguren.push_back(std::move(f));
            aus_.laufFigurNamen.push_back(w.name);
        }
    }
    // rc601 (bewegung, teil 3): Fallen/Vorlauf dieser Figur (AblaufWelt::vorlauf).
    int vorlaufArt(const Wesen& w) const {
        if (welt_.vorlauf) { return welt_.vorlauf(w.name); }
        return (w.istClient && !w.echterSpieler) ? 1 : -1;
    }
    // Wie scene.cpp beim ersten Plan: der Vorlauf beginnt bei NPC_Begin.
    void laufBereiten(Wesen& w) const {
        if (w.laufBereit) { return; }
        w.laufBereit = true;
        const int art = vorlaufArt(w);
        if (art < 0) { return; }
        const auto it = aus_.erscheint.find(w.name);
        w.lauf.gesetztMs = it != aus_.erscheint.end() ? it->second : kNavKartenSpawnMs;
        w.lauf.gesetztSpawn = true;
        w.lauf.denkNullMs = w.lauf.gesetztMs + 150.0;   // rc601 integ: erstes NPC_Think
        w.lauf.gesetztImFesten = art == 1;
    }
    void wegPlanen(Wesen& w, double tBefehl, bool fahrt) const {
        // rc601 (bewegung, Durchsicht 2): der Plan beginnt im Denkbild des
        // Befehls, wie scene.cpp.
        const double t = navDenkbildAb(tBefehl);
        (void)fahrt;
        w.wegLaeuft = false;
        if (!w.ortBekannt) {
            // Ohne Ort laesst sich keine Strecke rechnen: wie bisher sofort da.
            if (w.zielGesetzt) {
                for (int a = 0; a < 3; ++a) { w.ort[a] = w.zielOrt[a]; }
                w.ortBekannt = true;
                w.zielGesetzt = false;
                w.orteLog.push_back({t, NavVec{w.ort[0], w.ort[1], w.ort[2]}});   // rc603 fix603: erst ab hier dort
            }
            return;
        }
        laufBereiten(w);   // rc601 (bewegung, teil 3)
        LaufZustand z = w.lauf;
        const NavVec von{w.ort[0], w.ort[1], w.ort[2]};
        // G_SetOrigin: Pfad weg, Tempo bleibt. rc601 (bewegung, teil 3): eine
        // andere Hoehe allein ist kein Sprung - die kommt aus dem Fallen im
        // Plan (ein SET_ORIGIN traegt gesetztMs).
        if (z.ort[0] != von[0] || z.ort[1] != von[1] || (z.ort[2] != von[2] && z.hatVorlauf())) { z.springen(von); }
        if (!w.gierAusPlan) {
            planGierSync(w, t);   // rc601 (bewegung, Durchsicht 2)
            z.gier = w.gier.bei(t);
            z.wunschGier = z.gier;
        }
        w.gierAusPlan = false;
        const bool bewegtSich = z.tempoVek[0] != 0.0F || z.tempoVek[1] != 0.0F || z.tempoVek[2] != 0.0F;
        if (!w.zielGesetzt && !bewegtSich) {
            w.lauf = z;
            return;
        }
        LaufWerte werte;
        werte.zielGesetzt = w.zielGesetzt;
        for (int a = 0; a < 3; ++a) { werte.ziel[static_cast<std::size_t>(a)] = w.zielOrt[a]; }
        werte.zielRadius = static_cast<float>(w.zielRadius);
        werte.maxTempo = tempo(w, gehtLangsam(w));
        werte.gehTempo = tempo(w, true);
        werte.drehRate = w.gier.rate;
        werte.halbBreite = welt_.halbBreite ? welt_.halbBreite(w.name) : 16.0F;
        werte.hoeheOben = welt_.hoeheOben ? welt_.hoeheOben(w.name) : 40.0F;
        werte.stufe = welt_.stufe ? welt_.stufe(w.name) : 18.0F;
        werte.gangSchalter = w.rennend ? 2 : (w.gehend ? 1 : 0);
        werte.ohneKoerper = w.verhaltenZustand == "BS_NOCLIP";   // NPC_BSNoClip + PM_NoclipMove
        if (kino(w) && w.watchAn) {
            const bool selbst = w.watchSelbst;
            const NavVec ziel{w.watchOrt[0], w.watchOrt[1], w.watchOrt[2]};
            werte.fremdGier = [selbst, ziel](double, const NavVec& ich, float& g) {
                const float dx = ziel[0] - ich[0];
                const float dy = ziel[1] - ich[1];
                g = (selbst || (dx == 0.0F && dy == 0.0F)) ? 0.0F : std::atan2(dy, dx) * 180.0F / 3.14159265F;
                return true;
            };
        }
        if (!nav_) {
            nav_ = welt_.nav ? welt_.nav
                             : navWeltAusEntities(welt_.karte != nullptr ? welt_.karte->entities : std::vector<MapEntity>{});
        }
        const double grenze = std::min(welt_.grenzeMs, t + 120000.0);
        // rc601 (bewegung, teil 3): Schwerkraft und Vorlauf wie scene.cpp.
        werte.fallen = vorlaufArt(w) != -2;
        werte.schwerkraft = 800.0F;
        if (welt_.karte != nullptr && !welt_.karte->entities.empty()) {
            if (const std::string* g = welt_.karte->entities.front().find("gravity")) {
                werte.schwerkraft = static_cast<float>(std::atof(g->c_str()));
            }
        }
        if (w.schwerkraft >= 0.0F) { werte.schwerkraft = w.schwerkraft; }
        // rc601 integ: Takt des Verhaltens (OpenJK 100 ms, Movie Duels 50).
        werte.verhaltenTaktMs = welt_.verhaltenTakt ? welt_.verhaltenTakt(w.name, w.waffe)
                                                    : (navMovieDuelsRegeln() ? 50 : 100);
        double planAb = t;
        if (z.hatVorlauf() && z.gesetztMs < t - kGleichzeitig && !werte.ohneKoerper) {
            planAb = z.gesetztMs;
            werte.zielAbMs = t;
            werte.spawnVorlauf = z.gesetztSpawn;
            werte.spawnImFesten = z.gesetztImFesten;
        }
        const std::shared_ptr<const LaufPlan> allein = std::make_shared<const LaufPlan>(planeLauf(*nav_, z, werte, planAb, grenze));
        w.plan = allein;
        // rc601 (bewegung, teil 3): der Plan aus dem gemeinsamen Takt aller
        // Figuren (navKoerperLauf), wenn es fuer genau diese Eingaben einen gibt.
        const std::uint64_t koerperSchluessel = navKoerperSchluessel(w.name, navPlanSchluessel(*nav_, z, werte, planAb, grenze));
        if (welt_.koerperPlaene) {
            const auto it = welt_.koerperPlaene->find(koerperSchluessel);
            if (it != welt_.koerperPlaene->end()) { w.plan = it->second; }
        }
        abschnittMerken(w, planAb, grenze, z, werte, z.ort != w.lauf.ort || planAb < t - kGleichzeitig || w.abschnitt < 0, allein,
                        koerperSchluessel);
        w.lauf = w.plan->takte.empty() ? z : w.plan->zustaende.back();
        if (w.zielGesetzt && w.plan->ankunftMs >= 0.0) {
            w.wegLaeuft = true;
            w.wegBis = w.plan->ankunftMs;
        }
        // STEER setzt desiredYaw auf die Laufrichtung - ausser ein
        // Watchtarget in BS_CINEMATIC ueberschreibt es. rc601 (bewegung,
        // Durchsicht 2): die Gier rechnet jetzt der Plan je Denkbild, w.gier
        // holt sie bei Bedarf (planGierSync).
        planGierSync(w, t);
    }

    // --- rc601 (bewegung): Verhalten und Drehen ------------------------------
    static bool kino(const Wesen& w) { return w.verhaltenZustand == "BS_CINEMATIC"; }

    // Mit walkSpeed? SCF_RUNNING geht vor; sonst SCF_WALKING - oder ohne
    // Schalter das Verhalten: NPC_BSDefault setzt BUTTON_WALKING vor
    // NPC_MoveToGoal (AI_Default.cpp:1066-1081), NPC_BSCinematic nicht
    // (STEER::Activate, g_navigator.cpp:3990-3997).
    static bool gehtLangsam(const Wesen& w) {
        if (w.rennend) {
            return false;
        }
        return kino(w) ? w.gehend : true;
    }

    static float laufGier(const Wesen& w) {
        const NavVec von = w.plan ? w.plan->anfang.ort : NavVec{w.ort[0], w.ort[1], w.ort[2]};
        return std::atan2(w.zielOrt[1] - von[1], w.zielOrt[0] - von[0]) * 180.0F / 3.14159265F;
    }

    static bool laeuftBei(const Wesen& w, double t) { return w.wegLaeuft && t < w.wegBis; }

    static float watchGier(const Wesen& w, double t) {
        if (w.watchSelbst) {
            return 0.0F;   // auf sich selbst: vectoangles eines Nullvektors
        }
        float ich[3];
        ortJetzt(w, t, ich);
        const float dx = w.watchOrt[0] - ich[0];
        const float dy = w.watchOrt[1] - ich[1];
        return (dx == 0.0F && dy == 0.0F) ? 0.0F : std::atan2(dy, dx) * 180.0F / 3.14159265F;
    }

    // Was desiredYaw im naechsten Denkbild wirklich ist.
    static float wirktGier(const Wesen& w, double t, float sonst) {
        if (kino(w) && w.watchAn) {
            return watchGier(w, t);
        }
        if (laeuftBei(w, t)) {
            return laufGier(w);
        }
        return sonst;
    }

    // Die Aufgabe des Ziels nach einer neuen Planung stellen - DIESELBE
    // Aufgabe, kein setzeFach (das wuerde sie erledigen). Hat inzwischen ein
    // move oder PLAY_ROFF das Fach uebernommen (TID_MOVE_NAV ist geteilt),
    // bleibt dessen Zeit.
    void wegAufgabeStellen(Wesen& w, double t) {
        Aufgabe& a = w.fach[static_cast<std::size_t>(kWeg)];
        if (w.wegId < 0 || a.id != w.wegId) {
            return;
        }
        if (w.wegLaeuft) {
            a.fertig = w.wegBis;
            a.nie = false;
        } else if (w.zielGesetzt) {
            a.fertig = 0.0;
            a.nie = true;   // Tempo 0: haelt, bis sich etwas aendert
        } else {
            a.fertig = t;   // gleich angekommen: im naechsten Bild fertig
            a.nie = false;
            w.wegId = -1;
        }
    }

    // ps.speed setzen (SET_WALKSPEED/SET_RUNSPEED): mit Ziel den Plan zur
    // Zeit t schneiden, setzen, neu planen; sonst gilt es ab dem Zustand,
    // in dem die Figur zur Ruhe kommt (ohne Ziel liest Pmove ps.speed nicht).
    void psTempoSetzen(Wesen& w, double t, int wert) {
        if (!w.zielGesetzt) {
            w.lauf.tempo = wert;
            return;
        }
        const bool fahrt = wegAnhalten(w, t);
        w.lauf.tempo = wert;
        if (!w.zielGesetzt) {
            return;   // schon angekommen
        }
        wegPlanen(w, t, fahrt);
        wegAufgabeStellen(w, t);
    }

    // Gangart, Tempo oder Ort haben sich geaendert: den Rest neu planen.
    void wegNeuPlanen(Wesen& w, double t) {
        if (!w.zielGesetzt) {
            return;
        }
        const bool fahrt = wegAnhalten(w, t);
        if (!w.zielGesetzt) {
            return;   // schon angekommen
        }
        wegPlanen(w, t, fahrt);
        wegAufgabeStellen(w, t);
    }

    // --- Laden eines Blocks auf eine Entity -------------------------------
    void meldeTasks(Wesen& w, const std::vector<Node>& liste, int skript, const Path& basis) {
        for (std::size_t i = 0; i < liste.size(); ++i) {
            const Node& n = liste[i];
            if (n.kind != Node::Kind::Command || n.name != "task" || n.args.empty()) {
                continue;
            }
            Path p = basis;
            p.push_back(i);
            w.tasks[klein(n.args[0].text)] = Wesen::TaskDef{&n, skript, p};
            // Tasks in Tasks gehoeren derselben Entity.
            meldeTasks(w, n.children, skript, p);
        }
    }

    void zurueckholen(Wesen& w, Rahmen& neu) {
        // CSequencer::Recall: das laufende Warten geht zurueck in den
        // Ablauf und beginnt spaeter von vorn.
        if (w.warte == Wesen::kNichts) {
            return;
        }
        if (w.warteEintrag >= 0) {
            eintraege_[static_cast<std::size_t>(w.warteEintrag)].ende = jetzt_;
        }
        neu.hatZurueck = true;
        neu.zurueckArt = w.warte;
        neu.zurueckDauer = w.dauer;
        neu.zurueckName = w.warteName;
        neu.zurueckKnoten = w.warteKnoten;
        neu.zurueckHerkunft = w.warteHerkunft;
        w.warte = Wesen::kNichts;
        w.warteEintrag = -1;
    }

    void schiebeEin(int wi, const std::vector<Node>& liste, int skript, const Path& basis, bool flush,
                    bool ausEndlos = false) {
        Wesen& w = *wesen_[static_cast<std::size_t>(wi)];
        Rahmen r;
        r.liste = &liste;
        r.skript = skript;
        r.basis = basis;
        r.ausEndlos = ausEndlos;
        w.endlosGefuettert = ausEndlos;
        if (flush) {
            if (w.warteEintrag >= 0) {
                eintraege_[static_cast<std::size_t>(w.warteEintrag)].ende = jetzt_;
            }
            w.stapel.clear();
            w.gruppen.clear();
            w.warte = Wesen::kNichts;
            w.warteEintrag = -1;
        } else {
            zurueckholen(w, r);
        }
        meldeTasks(w, liste, skript, basis);
        w.stapel.push_back(std::move(r));
    }

    void starteSkript(int wi, const std::string& pfad, int ausloeser) {
        (void)ausloeser;
        if (wi < 0) {
            return;
        }
        Wesen& w = *wesen_[static_cast<std::size_t>(wi)];
        if (!w.gibt) {
            return;
        }
        int idx = 0;
        const Script* s = ladeSkript(pfad, &idx);
        if (s == nullptr) {
            return;
        }
        notiereStart(idx);   // rc603
        // Startet sich ein Skript auf derselben Entity immer wieder selbst
        // (md_toaj cin3_race: am Ende "race_time_up" -> derselbe
        // Skriptstarter), ist das eine Endlosschleife ueber use.
        const int starts = ++starts_[std::make_pair(wi, idx)];
        // CSequencer::Run: wie INSERT, der neue Ablauf laeuft sofort.
        schiebeEin(wi, s->nodes, idx, Path{}, false, starts > 3);
        aktualisiere(wi, jetzt_);
    }

    // --- use ----------------------------------------------------------------
    void plane(Geplant g) {
        g.folge = folge_++;
        geplant_.push_back(std::move(g));
    }

    void benutzeZiele(const MapEntity& e, int ausloeser, const char* key = "target") {
        const std::string t = wertVon(e, key);
        if (!t.empty()) {
            benutze(t, ausloeser, 0);
        }
    }

    // G_UseTargets2: jede Entity dieses targetname wird benutzt.
    void benutze(const std::string& name, int ausloeser, int tiefe) {
        if (tiefe > 32 || name.empty()) {
            return;
        }
        const std::string wer = (ausloeser >= 0 && static_cast<std::size_t>(ausloeser) < wesen_.size())
                                    ? wesen_[static_cast<std::size_t>(ausloeser)]->name
                                    : std::string{};
        aus_.benutzt.push_back(Ablauf::Benutzung{jetzt_, name, wer});
        if (welt_.karte == nullptr) {
            return;
        }
        bool traf = false;
        for (const MapEntity& e : welt_.karte->entities) {
            if (!gleich(e.targetname, name)) {
                continue;
            }
            traf = true;
            benutzeEine(e, ausloeser, tiefe);
        }
        // Eine Figur, die schon im Spiel steht, heisst wie ihr Spawner
        // (NPC_targetname). NPC_Use startet ihr usescript.
        if (!traf) {
            const int wi = wesen(name, false);
            if (wi >= 0 && wesen_[static_cast<std::size_t>(wi)]->figur) {
                Wesen& w = *wesen_[static_cast<std::size_t>(wi)];
                const auto it = w.verhalten.find("usescript");
                if (it != w.verhalten.end() && w.gibt) {
                    starteSkript(wi, it->second, ausloeser);
                }
            }
        }
    }

    void benutzeEine(const MapEntity& e, int ausloeser, int tiefe) {
        const std::string& cn = e.classname;
        const int wi = wesenFuer(e);
        Wesen& w = *wesen_[static_cast<std::size_t>(wi)];
        const std::string sf = wertVon(e, "spawnflags");
        const int flags = sf.empty() ? 0 : std::atoi(sf.c_str());

        if (cn == "target_scriptrunner") {
            if (w.inaktiv || w.gesperrtBis > jetzt_ || w.verbraucht) {
                return;
            }
            // count: wie oft; Vorgabe 1, -1 = immer (SP_target_scriptrunner).
            if (!w.zaehlerGesetzt) {
                w.zaehlerGesetzt = true;
                if (w.zaehler == 0) { w.zaehler = 1; }
            }
            if (w.zaehler > 0) {
                --w.zaehler;
                if (w.zaehler == 0) { w.verbraucht = true; }
            }
            const double wait = zahl(wertVon(e, "wait")) * 1000.0;
            if (wait > 0.0) { w.gesperrtBis = jetzt_ + wait; }
            const double delay = zahl(wertVon(e, "delay")) * 1000.0;
            const std::string skript = wertVon(e, "usescript");
            if (skript.empty()) {
                return;
            }
            // runonactivator: auf dem, der benutzt hat.
            const int auf = ((flags & 1) != 0 && ausloeser >= 0) ? ausloeser : wi;
            if (delay > 0.0) {
                plane(Geplant{jetzt_ + delay, 0, Geplant::Art::Skript, skript, auf, ausloeser});
            } else {
                starteSkript(auf, skript, ausloeser);
            }
            return;
        }
        // Alle anderen fangen mit G_ActivateBehavior( BSET_USE ) an.
        const auto usescript = [&] {
            const auto it = w.verhalten.find("usescript");
            if (it != w.verhalten.end()) {
                starteSkript(wi, it->second, ausloeser);
            }
        };
        if (istNpcSpawner(e)) {
            // NPC_Spawn: nach `delay` Sekunden (aufgerundet auf ms).
            const std::string npc = wertVon(e, "NPC_targetname");
            if (!w.zaehlerGesetzt) {
                w.zaehlerGesetzt = true;
                const std::string c = wertVon(e, "count");
                w.zaehler = c.empty() ? 1 : std::atoi(c.c_str());
                if (w.zaehler == 0) { w.zaehler = 1; }
            }
            if (w.zaehler == 0) {
                return;
            }
            if (w.zaehler > 0) { --w.zaehler; }
            const double delay = std::ceil(1000.0 * zahl(wertVon(e, "delay")));
            std::string figurName = npc;
            if (npc.empty()) {
                // rc603: ohne NPC_targetname ist die Figur fuer ICARUS
                // namenlos, ihr spawnscript laeuft aber doch - ValidEntity
                // (Q3_Interface.cpp:7581-7588 OpenJK) nimmt jede Entity mit
                // einem Verhaltensskript. Im Kartenstart (Reihenfolge der
                // Skripte) zaehlt dieser Start; ansprechen kann sie keiner.
                if (!kartenstart_ || wertVon(e, "spawnscript").empty()) {
                    return;
                }
                figurName = namenloseFigur(e);
            }
            const int fi = wesen(figurName);
            gespawnt.insert(klein(figurName));
            // NPC_Spawn_Do legt die Figur unsichtbar an (EF_NODRAW) und
            // ruft NPC_Begin ein FRAMETIME (100 ms, g_local.h:48) spaeter:
            // erst dort meldet InitEntity sie bei ICARUS an, und erst dort
            // laeuft das spawnscript (NPC_spawn.cpp:1910, 1956).
            plane(Geplant{jetzt_ + delay + 100.0, 0, Geplant::Art::Spawn, figurName, fi, ausloeser});
            return;
        }
        if (cn == "target_relay") {
            if (w.inaktiv || w.gesperrtBis > jetzt_ || w.verbraucht) {
                return;
            }
            const double delay = zahl(wertVon(e, "delay")) * 1000.0;
            const double wait = zahl(wertVon(e, "wait")) * 1000.0;
            usescript();
            const std::string t = wertVon(e, "target");
            if (delay > 0.0) {
                plane(Geplant{jetzt_ + delay, 0, Geplant::Art::Benutzen, t, -1, ausloeser});
            } else {
                benutze(t, ausloeser, tiefe + 1);
            }
            if (wait < 0.0) { w.verbraucht = true; }
            else if (wait > 0.0) { w.gesperrtBis = jetzt_ + wait; }
            return;
        }
        if (cn == "target_delay") {
            usescript();
            // "delay", sonst "wait", Vorgabe 1 s (SP_target_delay).
            double s = zahl(wertVon(e, "delay"), -1.0);
            if (s < 0.0) { s = zahl(wertVon(e, "wait"), 1.0); }
            if (s == 0.0) { s = 1.0; }
            plane(Geplant{jetzt_ + s * 1000.0, 0, Geplant::Art::Benutzen, wertVon(e, "target"), -1, ausloeser});
            return;
        }
        if (cn == "target_counter") {
            if (!w.zaehlerGesetzt) {
                w.zaehlerGesetzt = true;
                if (w.zaehler == 0) { w.zaehler = 2; }
            }
            if (w.zaehler == 0) {
                return;
            }
            --w.zaehler;
            if (w.zaehler != 0) {
                benutzeZiele(e, ausloeser, "target2");
                return;
            }
            usescript();
            benutze(wertVon(e, "target"), ausloeser, tiefe + 1);
            return;
        }
        if (cn == "target_activate" || cn == "target_deactivate") {
            const std::string t = wertVon(e, "target");
            if (welt_.karte != nullptr && !t.empty()) {
                for (const MapEntity& z : welt_.karte->entities) {
                    if (gleich(z.targetname, t)) {
                        wesen_[static_cast<std::size_t>(wesenFuer(z))]->inaktiv = (cn == "target_deactivate");
                    }
                }
            }
            return;
        }
        if (cn == "trigger_once" || cn == "trigger_multiple" || cn == "trigger_always") {
            if (w.inaktiv || w.verbraucht || w.gesperrtBis > jetzt_) {
                return;
            }
            const double delay = zahl(wertVon(e, "delay")) * 1000.0;
            if (cn == "trigger_once") { w.verbraucht = true; }
            const double wait = zahl(wertVon(e, "wait")) * 1000.0;
            if (wait > 0.0) { w.gesperrtBis = jetzt_ + delay + wait; }
            if (delay > 0.0) {
                // multi_trigger_run nach der Verzoegerung: usescript und Ziele.
                plane(Geplant{jetzt_ + delay, 0, Geplant::Art::Benutzen, "\x01" + e.targetname, -1, ausloeser});
            } else {
                usescript();
                benutzeZiele(e, ausloeser);
            }
            return;
        }
        if (cn == "func_breakable") {
            // funcBBrushUse (g_breakable.cpp:201). Das Bild macht MoverSim;
            // die ZIELE feuert der Nachbau, denn eines davon kann ein
            // Skript starten - MoverSim bekommt sie dann einzeln
            // (MoverUse::aufgeloest).
            if (w.entfernt || w.verbraucht) {
                return;   // zerbrochen: G_FreeEntity ein Bild spaeter, G_Find findet es nicht mehr
            }
            usescript();
            const std::string t = wertVon(e, "target");
            if ((flags & 64) != 0) {   // USE_NOT_BREAK: nur die Ziele
                benutze(t, ausloeser, tiefe + 1);
                return;
            }
            w.verbraucht = true;
            // "delay" ist ein F_INT (g_spawn.cpp:401): ganze Sekunden;
            // funcBBrushDieGo feuert die Ziele erst nach dem Warten. (Ein
            // zweites use in der Wartezeit schiebt den Bruch in der Engine
            // hinaus - hier nicht nachgebaut.)
            const std::string d = wertVon(e, "delay");
            const int sek = d.empty() ? 0 : std::atoi(d.c_str());
            if (sek > 0) {
                plane(Geplant{jetzt_ + sek * 1000.0, 0, Geplant::Art::Benutzen, t, -1, ausloeser});
            } else {
                benutze(t, ausloeser, tiefe + 1);
            }
            return;
        }
        if (cn == "misc_model_breakable") {
            // misc_model_use (g_breakable.cpp:723)
            if (w.entfernt) {
                return;
            }
            const std::string grav = wertVon(e, "gravity");
            if (!grav.empty() && zahl(grav) != 0.0 && !wertVon(e, "throwtarget").empty()) {
                return;   // target4: wirft sich auf sein Ziel, kein Bruch
            }
            const std::string gesundheit = wertVon(e, "health");
            const bool hatGesundheit = !gesundheit.empty() && std::atoi(gesundheit.c_str()) != 0;
            if (w.verbraucht && hatGesundheit) {
                benutze(wertVon(e, "target3"), ausloeser, tiefe + 1);   // "used while broken"
                return;
            }
            usescript();
            if ((flags & 64) != 0) {
                return;   // USE_NOT_BREAK
            }
            // misc_model_breakable_die: erst die Ziele, dann BSET_DEATH -
            // ausser mit "no dmodel" (8), dann wird es freigegeben.
            w.verbraucht = true;
            benutze(wertVon(e, "target"), ausloeser, tiefe + 1);
            if ((flags & 8) != 0) {
                w.entfernt = true;
            } else {
                const auto it = w.verhalten.find("deathscript");
                if (it != w.verhalten.end()) {
                    starteSkript(wi, it->second, ausloeser);
                }
            }
            return;
        }
        // Tueren, Waende, Effekte, Lautsprecher, Musik: das Bild macht
        // MoverSim, der Klang die Ansicht. Hier nur das usescript.
        usescript();
    }

    // --- Befehle ----------------------------------------------------------
    int zeichne(int wi, const Node* n, const AblaufHerkunft& h, bool warten) {
        Eintrag e;
        e.ms = jetzt_;
        e.wesen = wi;
        e.knoten = n;
        e.herkunft = h;
        e.warten = warten;
        e.folge = folge_++;
        eintraege_.push_back(std::move(e));
        ++aus_.befehle;
        return static_cast<int>(eintraege_.size()) - 1;
    }

    std::string wertArg(const Wesen& w, const Arg& a) const {
        if (a.kind != Arg::Kind::Expr) {
            return a.text;
        }
        // $get( FLOAT, "name" )$ - Variable oder Wert der Entity.
        const std::string t = a.text;
        const auto q1 = t.find('"');
        const auto q2 = (q1 == std::string::npos) ? q1 : t.find('"', q1 + 1);
        if (t.rfind("get", 0) == 0 && q2 != std::string::npos) {
            const std::string name = t.substr(q1 + 1, q2 - q1 - 1);
            const auto v = variablen_.find(klein(name));
            if (v != variablen_.end()) {
                return v->second;
            }
            const auto p = w.parm.find(klein(name));
            if (p != w.parm.end()) {
                return p->second;
            }
            return "0";
        }
        if (t.rfind("random", 0) == 0) {
            // Die Mitte, wie readMs - wiederholbar.
            return std::to_string(readMs("$" + t + "$"));
        }
        return t;
    }

    bool bedingung(const Wesen& w, const Node& n) const {
        if (n.args.size() < 3) {
            return true;
        }
        const std::string a = wertArg(w, n.args[0]);
        const std::string op = n.args[1].text;
        const std::string b = wertArg(w, n.args[2]);
        char* ea = nullptr;
        char* eb = nullptr;
        const double x = std::strtod(a.c_str(), &ea);
        const double y = std::strtod(b.c_str(), &eb);
        const bool zahlen = ea != a.c_str() && eb != b.c_str();
        if (op == "=") { return zahlen ? std::fabs(x - y) < 1e-4 : gleich(a, b); }
        if (op == "!") { return zahlen ? std::fabs(x - y) >= 1e-4 : !gleich(a, b); }
        if (op == ">") { return zahlen && x > y; }
        if (op == "<") { return zahlen && x < y; }
        return true;
    }

    float tempo(const Wesen& w, bool gehen) const {
        const float eigen = gehen ? w.gehTempo : w.laufTempo;
        if (eigen >= 0.0F) {
            return eigen;
        }
        if (welt_.tempo) {
            // rc601 (bewegung): 0 ist ein gueltiges Tempo aus der .npc
            // (NPC_ParseParms verwirft nur n < 0) - nur negativ heisst
            // unbekannt.
            const float t = welt_.tempo(w.name, gehen);
            if (t >= 0.0F) {
                return t;
            }
        }
        return gehen ? 90.0F : 300.0F;   // NPC_stats.cpp
    }

    // rc601 (bewegung): markenRadius und istMarke sind in navZielAufloesen
    // (scene.cpp) aufgegangen - dieselbe Aufloesung fuer Szene und Ablauf,
    // mit ref_tag 16 und ohne Gross/klein (TAG_Find).

    bool ortVon(const std::string& name, float out[3]) const {
        if (welt_.karte == nullptr) {
            return false;
        }
        if (gleich(name, "player")) {
            // Der Spieler: wo er gerade steht, sonst am Startpunkt.
            const auto it = wesenNach_.find("player");
            if (it != wesenNach_.end() && wesen_[static_cast<std::size_t>(it->second)]->ortBekannt) {
                // rc601 (bewegung): auch mitten auf einem Weg.
                ortJetzt(*wesen_[static_cast<std::size_t>(it->second)], jetzt_, out);
                return true;
            }
            for (const MapEntity& e : welt_.karte->entities) {
                if (e.classname == "info_player_start" && !e.origin.empty()) {
                    return leseVek(e.origin, out);
                }
            }
        }
        for (const MapEntity& e : welt_.karte->entities) {
            if (!e.origin.empty() && gleich(e.targetname, name)) {
                return leseVek(e.origin, out);
            }
        }
        for (const MapEntity& e : welt_.karte->entities) {
            const std::string* t = schluessel(e, "NPC_targetname");
            if (!e.origin.empty() && t != nullptr && gleich(*t, name)) {
                return leseVek(e.origin, out);
            }
        }
        return false;
    }

    // Ein Befehl, der kein Block ist. Gibt false zurueck, wenn er blockiert.
    void fuehreAus(int wi, const Node& n, const AblaufHerkunft& h) {
        Wesen& w = *wesen_[static_cast<std::size_t>(wi)];
        const std::string& c = n.name;
        const double t = jetzt_;

        if (c == "wait" || c == "waitsignal") {
            const int e = zeichne(wi, &n, h, true);
            w.warteEintrag = e;
            w.warteKnoten = &n;
            w.warteHerkunft = h;
            w.stempel = t;
            if (c == "waitsignal") {
                w.warte = Wesen::kSignal;
                w.warteName = n.args.empty() ? std::string{} : klein(n.args[0].text);
            } else if (!n.args.empty() && n.args[0].kind == Arg::Kind::String) {
                w.warte = Wesen::kGruppe;
                w.warteName = klein(n.args[0].text);
            } else {
                w.warte = Wesen::kZeit;
                w.dauer = n.args.empty() ? 0.0 : readMs(n.args[0].kind == Arg::Kind::Expr
                                                            ? "$" + n.args[0].text + "$"
                                                            : n.args[0].text);
            }
            return;
        }
        zeichne(wi, &n, h, false);
        const int id = neueAufgabe(w);
        bool fertig = true;

        if (c == "signal") {
            if (!n.args.empty()) { signale_.insert(klein(n.args[0].text)); }
        } else if (c == "declare") {
            if (n.args.size() >= 2) {
                deklariert_.insert(klein(n.args[1].text));
                variablen_[klein(n.args[1].text)] = "0";
            }
        } else if (c == "free") {
            if (!n.args.empty()) { deklariert_.erase(klein(n.args[0].text)); }
        } else if (c == "sound") {
            // CHAN_VOICE, CHAN_VOICE_ATTEN, CHAN_VOICE_GLOBAL halten die
            // Aufgabe, bis die Stimme verklungen ist (G_CheckTasksCompleted).
            if (n.args.size() >= 2 && klein(n.args[0].text).rfind("chan_voice", 0) == 0) {
                const double d = welt_.klangDauer ? welt_.klangDauer(n.args[1].text) : 0.0;
                if (d > 0.0) {
                    setzeFach(w, kStimme, id, t + d, false);
                    fertig = false;
                }
            }
        } else if ((c == "move" || c == "rotate") && w.istClient) {
            // rc601 (bewegung): Lerp2Pos/Lerp2Angles lehnen jeden client ab
            // ("NOT a mover", Q3_Interface.cpp:8571, 8686) und kehren VOR
            // Q3_TaskIDSet zurueck; CTaskManager::Move/Rotate melden selbst
            // nichts (TaskManager.cpp:1206, 1425). Die Aufgabe wird nie fertig
            // - ein dowait darauf haengt im Spiel. Die Faecher bleiben, wie sie
            // waren.
            hinweis(w.name + ": " + c + " auf einer Figur - tut im Spiel nichts, die Aufgabe wird nie fertig");
            fertig = false;
        } else if (c == "move" || c == "rotate") {
            // Lerp2Pos / Lerp2Angles: fertig nach der Dauer.
            const double d = n.args.empty() ? 0.0 : readMs(n.args.back().kind == Arg::Kind::Expr
                                                               ? "$" + n.args.back().text + "$"
                                                               : n.args.back().text);
            if (d > 0.0) {
                setzeFach(w, c == "move" ? kWeg : kBlick, id, t + d, false);
                fertig = false;
            }
        } else if (c == "play") {
            // CQuake3GameInterface::Play (Q3_Interface.cpp:10683) kennt nur
            // PLAY_ROFF. Findet G_LoadRoff die Datei, haelt die Aufgabe
            // TID_MOVE_NAV (Q3_TaskIDSet, ebenda:10698) - bis G_Roff das
            // letzte Bild anwendet und Q3_TaskIDComplete ruft
            // (g_roff.cpp:626). Wie bei move erledigt das neue das alte.
            //
            // CTaskManager::Play (TaskManager.cpp:1603) meldet SELBST nie
            // Completed. Ein anderer Typ oder eine fehlende Datei lassen die
            // Aufgabe darum fuer immer offen - ein dowait darauf haengt im
            // Spiel genauso.
            const bool roffTyp = n.args.size() >= 2 && gleich(n.args[0].text, "PLAY_ROFF");
            if (!roffTyp) {
                hinweis(w.name + ": play ohne PLAY_ROFF - die Aufgabe wird nie fertig");
                fertig = false;
            } else if (welt_.roffDauer) {
                const std::string datei = wertArg(w, n.args[1]);
                const double d = welt_.roffDauer(datei);
                if (d >= 0.0) {
                    setzeFach(w, kWeg, id, t + d, false);
                    fertig = false;
                } else if (d <= kRoffFehlt) {
                    hinweis(w.name + ": ROFF \"" + datei + "\" fehlt - play wird nie fertig (wie im Spiel)");
                    fertig = false;
                }
            }
        } else if (c == "use") {
            if (!n.args.empty()) { benutze(n.args[0].text, wi, 0); }
        } else if (c == "kill" || c == "remove") {
            if (!n.args.empty()) {
                const std::string ziel = gleich(n.args[0].text, "self") ? w.name : n.args[0].text;
                const int zi = wesen(ziel, false);
                if (zi >= 0) {
                    Wesen& z = *wesen_[static_cast<std::size_t>(zi)];
                    z.totMs = std::min(z.totMs, jetzt_);   // rc601 (bewegung, teil 3): kein CONTENTS_BODY mehr
                    if (c == "remove") {
                        z.gibt = false;
                        z.stapel.clear();
                        z.warte = Wesen::kNichts;
                    } else {
                        const auto it = z.verhalten.find("deathscript");
                        if (it != z.verhalten.end()) {
                            starteSkript(zi, it->second, wi);
                        }
                    }
                }
            }
        } else if (c == "set" && n.args.size() >= 2) {
            fertig = fuehreSetAus(w, wi, n, id);
        }
        if (fertig) {
            erledige(id);
        }
    }

    bool fuehreSetAus(Wesen& w, int wi, const Node& n, int id) {
        (void)wi;
        const std::string typ = n.args[0].text;
        const std::string wert = wertArg(w, n.args[1]);
        const std::string k = klein(typ);
        const double t = jetzt_;
        if (deklariert_.count(k) != 0) {
            variablen_[k] = wert;
            return true;
        }
        if (k.rfind("set_parm", 0) == 0) {
            w.parm[k] = wert;
            return true;
        }
        if (k == "set_navgoal") {
            if (!w.figur || w.echterSpieler) {
                return true;   // rc601 (bewegung): "tried to set a navgoal on a non-NPC"
            }
            // rc601 (bewegung): Q3_SetNavGoal wie in scene.cpp
            // (Q3_Interface.cpp:2106 ff.).
            //
            //   "null"/"NULL": goalEntity = NULL und Q3_TaskIDComplete(
            //       TID_MOVE_NAV) - die Figur haelt an, und was in dem Fach
            //       liegt, ist erledigt. Bis rc600 uebergangen: dowait lief
            //       bis zur alten Ankunft weiter.
            //   unbekannt: "can't find NAVGOAL", das ALTE Ziel bleibt.
            //   TAG (ref_tag, waypoint_navgoal): Q3_TaskIDSet(TID_MOVE_NAV),
            //       die Aufgabe haelt bis zur Ankunft.
            //   ein anderes Entity (G_Find): goalEntity, aber Q3_SetNavGoal
            //       gibt qfalse - dowait wartet NICHT auf die Ankunft.
            //
            // Die Strecke gilt ab dem AKTUELLEN Ort (bis rc600 ab dem vorigen
            // Ziel, auch wenn die Figur dort nie ankam).
            if (gleich(wert, "NULL")) {
                (void)wegAnhalten(w, t);
                w.zielGesetzt = false;
                wegPlanen(w, t, false);   // rc601 (bewegung, Wege): sie rollt aus
                if (w.fach[static_cast<std::size_t>(kWeg)].id >= 0) {
                    erledige(w.fach[static_cast<std::size_t>(kWeg)].id);
                    leereFach(w, kWeg);
                }
                w.wegId = -1;
                return true;
            }
            if (welt_.karte == nullptr) {
                hinweis("Wegpunkt \"" + wert + "\" fehlt in der Karte");
                return true;
            }
            const NavZiel z = navZielAufloesen(*welt_.karte, wert);
            if (!z.gefunden) {
                hinweis("Wegpunkt \"" + wert + "\" fehlt in der Karte");
                return true;
            }
            const bool fahrt = wegAnhalten(w, t);
            w.zielGesetzt = true;
            w.zielMarke = z.marke;
            for (int a = 0; a < 3; ++a) { w.zielOrt[a] = z.ort[a]; }
            w.zielRadius = z.radius;
            w.lauf.neuesZiel();   // rc601 (bewegung, Wege): NPC_SetMoveGoal, tempGoal->waypoint = NONE
            wegPlanen(w, t, fahrt);
            if (!z.marke) {
                w.wegId = -1;
                return true;
            }
            if (w.wegLaeuft) {
                setzeFach(w, kWeg, id, w.wegBis, false);
            } else if (w.zielGesetzt) {
                setzeFach(w, kWeg, id, 0.0, true);   // Tempo 0: steht, die Aufgabe bleibt offen
            } else {
                setzeFach(w, kWeg, id, t, false);    // schon da: im naechsten Bild fertig
            }
            w.wegId = w.zielGesetzt ? id : -1;
            return false;
        }
        if (k == "set_anim_both" || k == "set_anim_upper" || k == "set_anim_lower") {
            if (!w.figur) {
                return true;
            }
            const double d = welt_.animDauer ? welt_.animDauer(w.name, wert) : -1.0;
            if (d < 0.0) {
                return true;   // unbekannt: wie eine fehlende Animation, sofort fertig
            }
            if (k == "set_anim_upper") {
                leereFach(w, kBeide);
                setzeFach(w, kOben, id, t + d, false);
            } else if (k == "set_anim_lower") {
                leereFach(w, kBeide);
                setzeFach(w, kUnten, id, t + d, false);
            } else {
                setzeFach(w, kOben, id, t + d, false);
                setzeFach(w, kUnten, id, t + d, false);
                setzeFach(w, kBeide, id, t + d, false);
            }
            return false;
        }
        if (k == "set_anim_holdtime_both" || k == "set_anim_holdtime_upper" || k == "set_anim_holdtime_lower") {
            if (!w.figur) {
                return true;
            }
            const double d = zahl(wert);
            const bool nie = d < 0.0;
            if (k == "set_anim_holdtime_upper") {
                leereFach(w, kBeide);
                setzeFach(w, kOben, id, t + d, nie);
            } else if (k == "set_anim_holdtime_lower") {
                leereFach(w, kBeide);
                setzeFach(w, kUnten, id, t + d, nie);
            } else {
                setzeFach(w, kBeide, id, t + d, nie);
                setzeFach(w, kOben, id, t + d, nie);
                setzeFach(w, kUnten, id, t + d, nie);
            }
            return false;
        }
        if (k == "set_gravity") {
            // rc601 (bewegung, teil 3): Q3_SetGravity (Q3_Interface.cpp:3788-3807 MD)
            w.schwerkraft = static_cast<float>(std::atof(wert.c_str()));
        }
        if (k == "set_origin" || k == "set_teleport_dest") {
            // rc601 (bewegung): G_SetOrigin laesst goalEntity und die
            // Geschwindigkeit stehen (g_utils.cpp:1585) - ein laufender Weg
            // geht vom neuen Ort aus weiter, ohne neuen Anlauf.
            // rc601 (bewegung, Wege): auch ohne Ziel, wenn sie noch ausrollt.
            const bool hatte = static_cast<bool>(w.plan);
            const bool fahrt = hatte && wegAnhalten(w, t);
            float o[3];
            // rc601 (bewegung, teil 3): G_SetOrigin am Ende dieses Denkbilds -
            // der naechste Plan beginnt hier (wie scene.cpp).
            laufBereiten(w);
            if (vorlaufArt(w) != -2 && !w.echterSpieler) {
                w.lauf.gesetztMs = navDenkbildAb(t);
                w.lauf.gesetztSpawn = false;
            }
            if (leseVek(wert, o)) {
                for (int a = 0; a < 3; ++a) { w.ort[a] = o[a]; }
                w.ortBekannt = true;
                w.orteLog.push_back({navDenkbildAb(t), NavVec{o[0], o[1], o[2]}});   // rc601 (bewegung, teil 3)
            } else if (n.args[1].kind == Arg::Kind::Expr) {
                // $tag( "name", ORIGIN )$
                const std::string& x = n.args[1].text;
                const auto q1 = x.find('"');
                const auto q2 = (q1 == std::string::npos) ? q1 : x.find('"', q1 + 1);
                if (q2 != std::string::npos && ortVon(x.substr(q1 + 1, q2 - q1 - 1), o)) {
                    for (int a = 0; a < 3; ++a) { w.ort[a] = o[a]; }
                    w.ortBekannt = true;
                    w.orteLog.push_back({navDenkbildAb(t), NavVec{o[0], o[1], o[2]}});   // rc603 fix603
                }
            }
            if (w.zielGesetzt || hatte) {
                wegPlanen(w, t, fahrt);
                wegAufgabeStellen(w, t);
            }
            return true;
        }
        if (k == "set_copy_origin") {
            // Q3_SetCopyOrigin -> Q3_SetOrigin (Q3_Interface.cpp:1413, 1309):
            // hier wird die Geschwindigkeit geloescht - ein Weg beginnt neu.
            if (w.plan) {
                (void)wegAnhalten(w, t);
            }
            w.lauf.tempoVek = {0.0F, 0.0F, 0.0F};   // rc601 (bewegung, Wege): Q3_SetOrigin loescht ps.velocity
            const int zi = wesen(wert, false);
            if (zi >= 0 && wesen_[static_cast<std::size_t>(zi)]->ortBekannt) {
                ortJetzt(*wesen_[static_cast<std::size_t>(zi)], t, w.ort);
                w.ortBekannt = true;
                // rc603 fix603: G_SetOrigin + linkentity zur Zeit des Befehls -
                // erst ab hier steht ihr Kasten dort (navKoerperLauf).
                w.orteLog.push_back({navDenkbildAb(t), NavVec{w.ort[0], w.ort[1], w.ort[2]}});
            }
            if (w.zielGesetzt) {
                wegPlanen(w, t, false);
                wegAufgabeStellen(w, t);
            }
            return true;
        }
        // rc601 (bewegung): SCF_WALKING und SCF_RUNNING sind zwei eigene
        // Schalter (Q3_SetWalking/Q3_SetRunning, Q3_Interface.cpp:5183,
        // 5216) - bis rc600 loeschte hier der eine den anderen. RUNNING geht
        // vor (STEER::Activate). Das Tempo ist atoi (Q3_Interface.cpp:9070).
        // Jede Aenderung gilt fuer einen laufenden Weg sofort.
        // --- rc601 (bewegung): Verhalten, Watchtarget, Drehen -----------------
        //
        // Wie in scene.cpp: SET_BEHAVIOR_STATE (Q3_SetBState) nur fuer NPCs;
        // die Gangart haengt daran. Ein Weg geht im naechsten Denkbild anders
        // weiter.
        if (k == "set_behavior_state") {
            if (!w.figur || w.echterSpieler) {
                return true;
            }
            planGierSync(w, t);   // rc601 (bewegung, Durchsicht 2)
            std::string bs = wert;
            for (char& ch : bs) { ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); }
            const bool vorher = kino(w);
            w.verhaltenZustand = bs;
            if (!vorher && kino(w) && w.watchAn) {
                w.gier.zielAb(t, watchGier(w, t));
            }
            wegNeuPlanen(w, t);
            return true;
        }
        // Q3_SetWatchTarget (Q3_Interface.cpp:3264-3296): NULL/NONE loescht
        // den Zeiger (desiredYaw bleibt), sonst G_Find - findet es nichts,
        // bleibt das alte Ziel; der eigene Name findet sich selbst.
        if (k == "set_watchtarget") {
            if (!w.figur || w.echterSpieler) {
                return true;
            }
            planGierSync(w, t);   // rc601 (bewegung, Durchsicht 2)
            if (gleich(wert, "NULL") || gleich(wert, "NONE")) {
                const bool war = kino(w) && w.watchAn;
                w.watchAn = false;
                w.watchSelbst = false;
                if (war && laeuftBei(w, t)) {
                    w.gier.zielAb(t, laufGier(w));
                }
                return true;
            }
            float o[3];
            if (gleich(wert, w.name)) {
                w.watchAn = true;
                w.watchSelbst = true;
            } else if (welt_.karte != nullptr && engineFindet(*welt_.karte, wert, o)) {
                w.watchAn = true;
                w.watchSelbst = false;
                for (int a = 0; a < 3; ++a) { w.watchOrt[a] = o[a]; }
            } else {
                return true;   // "can't find WatchTarget": das alte bleibt
            }
            if (kino(w)) {
                w.gier.zielAb(t, watchGier(w, t));
            }
            wegNeuPlanen(w, t);   // rc601 (bewegung, Wege): wie scene.cpp
            return true;
        }
        // Q3_SetYawSpeed: atof, ungeprueft (Q3_Interface.cpp:9085, 3669).
        if (k == "set_yawspeed" && welt_.drehrateNpc && w.figur && !w.echterSpieler) {
            // rc601 (bewegung, Durchsicht 2): stats.yawSpeed aendert sich; ob
            // es wirkt, sagt die Waffe in der Hand (npcDrehrate).
            planGierSync(w, t);
            w.yawRate = turnRateFor(static_cast<float>(std::atof(wert.c_str())));
            w.gier.rateAb(t, welt_.drehrateNpc(w.name, w.waffe, w.yawRate));
            wegNeuPlanen(w, t);
            return true;
        }
        if (k == "set_weapon" && welt_.drehrateNpc && w.figur && !w.echterSpieler) {
            // rc601 (bewegung, Durchsicht 2): NPC_UpdateAngles liest
            // NPC->s.weapon je Denkbild (NPC_utils.cpp:274-290 MD). Der Befehl
            // bleibt sonst ohne Wirkung auf den Ablauf.
            std::string wa = wert;
            for (char& ch : wa) { ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); }
            if (wa == "DROP") { wa = "drop"; }
            if (wa != "drop" && wa.compare(0, 3, "WP_") != 0) { return true; }
            w.waffe = wa;
            const float y = w.yawRate > 0.0F ? w.yawRate : w.gier.rate;
            const float neu = welt_.drehrateNpc(w.name, w.waffe, y);
            if (neu != w.gier.rate) {   // sonst bleibt der Plan derselbe
                planGierSync(w, t);
                w.gier.rateAb(t, neu);
                wegNeuPlanen(w, t);
            }
            return true;
        }
        if (k == "set_yawspeed") {
            if (w.figur && !w.echterSpieler && !(welt_.drehrateFest && welt_.drehrateFest(w.name))) {
                planGierSync(w, t);   // rc601 (bewegung, Durchsicht 2)
                w.gier.rateAb(t, turnRateFor(static_cast<float>(std::atof(wert.c_str()))));
                wegNeuPlanen(w, t);   // rc601 (bewegung, Wege): wie scene.cpp
            }
            return true;
        }
        // SET_ANGLES: SetClientViewAngle (ein Sprung) und Q3_SetDYaw.
        if (k == "set_angles") {
            float a[3];
            if (w.figur && !w.echterSpieler && leseVek(wert, a)) {
                w.gier.setzen(t, a[1]);
                const float wirkt = wirktGier(w, t, a[1]);
                if (wirkt != a[1]) {
                    w.gier.zielAb(t, wirkt);
                }
                // rc601 (bewegung, Wege): wie scene.cpp - mit der neuen Gier
                // weiter (die Gier der Rechnung springt mit).
                if (w.zielGesetzt && w.plan) {
                    (void)wegAnhalten(w, t);
                    w.gierAusPlan = false;
                    if (w.zielGesetzt) {
                        wegPlanen(w, t, false);
                        wegAufgabeStellen(w, t);
                    }
                }
            }
            return true;
        }
        // SET_DYAW, SET_VIEWTARGET, SET_DPITCH halten TID_ANGLE_FACE, bis
        // NPC_UpdateAngles den Winkel "exact" meldet (Set, Q3_Interface.cpp:
        // 9023-9043; NPC_utils.cpp:388). Auf etwas anderem als einem NPC
        // meldet das niemand - die Aufgabe wird nie fertig.
        if (k == "set_dyaw" || k == "set_viewtarget" || k == "set_dpitch") {
            if (!w.figur || w.echterSpieler) {
                setzeFach(w, kBlick, id, 0.0, true);
                return false;
            }
            planGierSync(w, t);   // rc601 (bewegung, Durchsicht 2): desiredYaw jetzt
            float wunsch = w.gier.ziel;
            float nick = 0.0F;
            if (k == "set_dyaw") {
                wunsch = static_cast<float>(std::atof(wert.c_str()));
            } else if (k == "set_dpitch") {
                nick = static_cast<float>(std::atof(wert.c_str()));
            } else {
                // Q3_SetViewTarget: vom Auge (viewheight 36) zum Ziel - einer
                // Figur ins Auge, sonst zum Ursprung. Ohne Ziel bleibt desiredYaw.
                float o[3];
                bool zielFigur = false;
                if (welt_.karte != nullptr && engineFindet(*welt_.karte, wert, o, &zielFigur)) {
                    float ich[3];
                    ortJetzt(w, t, ich);
                    const float dx = o[0] - ich[0];
                    const float dy = o[1] - ich[1];
                    const float dz = (o[2] + (zielFigur ? 36.0F : 0.0F)) - (ich[2] + 36.0F);
                    wunsch = std::atan2(dy, dx) * 180.0F / 3.14159265F;
                    nick = -std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 180.0F / 3.14159265F;
                }
            }
            // rc601 (bewegung, Durchsicht 2): laeuft oder rollt die Figur noch,
            // gilt das neue desiredYaw im Plan (wie scene.cpp) - STEER
            // ueberschreibt es, solange das Ziel steht; TID_ANGLE_FACE endet im
            // ersten Denkbild mit "exact" (planGenauAb), sonst vom letzten
            // Zustand des Plans aus.
            if (k != "set_dpitch" && w.plan && !w.plan->takte.empty() && w.plan->takte.back().t > t + kGleichzeitig) {
                (void)wegAnhalten(w, t);
                w.lauf.wunschGier = wunsch;
                const bool rollt = w.lauf.tempoVek[0] != 0.0F || w.lauf.tempoVek[1] != 0.0F || w.lauf.tempoVek[2] != 0.0F;
                if (w.zielGesetzt || rollt) {
                    w.gierAusPlan = true;
                    wegPlanen(w, t, false);
                    wegAufgabeStellen(w, t);
                }
                if (w.plan) {
                    const LaufPlan& np = *w.plan;
                    double ende = planGenauAb(np, t);
                    if (ende < 0.0 && !np.takte.empty()) {
                        const LaufZustand& zl = np.zustaende.back();
                        const double h = drehHalteMs(zl.gier, zl.wunschGier, w.gier.rate);
                        ende = h < 0.0 ? -1.0 : np.takte.back().t + h;
                    }
                    if (ende >= 0.0) {
                        setzeFach(w, kBlick, id, ende, false);
                    } else {
                        setzeFach(w, kBlick, id, 0.0, true);
                    }
                    return false;
                }
                // Ohne neuen Plan steht die Figur: der Regler von hier.
                w.gier.setzen(t, w.lauf.gier);
            }
            const float wirkt = (k == "set_dpitch") ? w.gier.ziel : wirktGier(w, t, wunsch);
            // rc601 (bewegung, teil 3): die Gier Denkbild fuer Denkbild mit
            // ANGLE2SHORT (wie scene.cpp), der Nick wie bisher.
            const double haltGier = (k == "set_dpitch") ? drehHalteMs(0.0F, w.gier.rate)
                                                        : drehHalteMs(w.gier.bei(t), wirkt, w.gier.rate);
            const double haltNick = drehHalteMs(std::fabs(winkel180(nick)), w.gier.rate);
            const double halt = (haltGier < 0.0 || haltNick < 0.0) ? -1.0 : std::max(haltGier, haltNick);
            if (k != "set_dpitch" && !(kino(w) && w.watchAn) && !laeuftBei(w, t)) {
                w.gier.zielAb(t, wunsch);
            }
            if (halt < 0.0) {
                setzeFach(w, kBlick, id, 0.0, true);
            } else {
                setzeFach(w, kBlick, id, t + halt, false);
            }
            return false;
        }
        if (k == "set_walking") { w.gehend = gleich(wert, "true"); wegNeuPlanen(w, t); return true; }
        if (k == "set_running") { w.rennend = gleich(wert, "true"); wegNeuPlanen(w, t); return true; }
        // ... und beide setzen ps.speed (Q3_Interface.cpp:3621-3657 MD) - das
        // Tempo in BS_NOCLIP (PM_CmdScale).
        if (k == "set_walkspeed") {
            w.gehTempo = static_cast<float>(std::atoi(wert.c_str()));
            psTempoSetzen(w, t, std::atoi(wert.c_str()));
            return true;
        }
        if (k == "set_runspeed") {
            w.laufTempo = static_cast<float>(std::atoi(wert.c_str()));
            psTempoSetzen(w, t, std::atoi(wert.c_str()));
            return true;
        }
        if (k == "set_inactive") { w.inaktiv = gleich(wert, "true"); return true; }
        // SET_SPAWNSCRIPT, SET_DEATHSCRIPT, SET_USESCRIPT ...
        if (k.size() > 10 && k.compare(k.size() - 6, 6, "script") == 0 && k.rfind("set_", 0) == 0 &&
            k != "set_cinematic_skipscript") {
            const std::string was = k.substr(4);
            if (wert.empty() || gleich(wert, "NULL")) {
                w.verhalten.erase(was);
            } else {
                w.verhalten[was] = wert;
            }
            return true;
        }
        return true;
    }

    // --- Der Taktgeber einer Entity (CTaskManager::Go) ----------------------
    // Den naechsten Befehl holen, Bloecke unterwegs aufloesen.
    const Node* naechster(int wi, AblaufHerkunft* h) {
        Wesen& w = *wesen_[static_cast<std::size_t>(wi)];
        int schutz = 0;
        while (!w.stapel.empty()) {
            if (++schutz > 100000) {
                hinweis(w.name + ": Endlosschleife ohne wait abgebrochen");
                w.stapel.clear();
                return nullptr;
            }
            Rahmen& r = w.stapel.back();
            if (r.i >= r.liste->size()) {
                if (r.art == Rahmen::Art::Schleife) {
                    if (r.runden > 0) { --r.runden; }
                    if (r.runden != 0) {
                        r.i = 0;
                        continue;
                    }
                }
                Rahmen fertig = std::move(w.stapel.back());
                w.stapel.pop_back();
                if (fertig.art == Rahmen::Art::Task && !w.gruppen.empty()) {
                    w.gruppen.pop_back();
                }
                if (fertig.dannWarten) {
                    // dowait: jetzt auf die Gruppe warten.
                    const int e = zeichne(wi, fertig.dowaitKnoten, fertig.dowaitHerkunft, true);
                    w.warte = Wesen::kGruppe;
                    w.warteName = fertig.dowaitKnoten != nullptr && !fertig.dowaitKnoten->args.empty()
                                      ? klein(fertig.dowaitKnoten->args[0].text)
                                      : std::string{};
                    w.warteKnoten = fertig.dowaitKnoten;
                    w.warteHerkunft = fertig.dowaitHerkunft;
                    w.stempel = jetzt_;
                    w.warteEintrag = e;
                    return nullptr;   // der Aufrufer prueft das Warten
                }
                if (fertig.hatZurueck) {
                    // Das zurueckgeholte Warten beginnt von vorn.
                    w.warte = fertig.zurueckArt;
                    w.dauer = fertig.zurueckDauer;
                    w.warteName = fertig.zurueckName;
                    w.warteKnoten = fertig.zurueckKnoten;
                    w.warteHerkunft = fertig.zurueckHerkunft;
                    w.stempel = jetzt_;
                    w.warteEintrag = zeichne(wi, fertig.zurueckKnoten, fertig.zurueckHerkunft, true);
                    return nullptr;
                }
                continue;
            }
            const std::size_t i = r.i++;
            const Node& n = (*r.liste)[i];
            if (n.kind != Node::Kind::Command) {
                continue;
            }
            Path p = r.basis;
            p.push_back(i);
            const int skript = r.skript;
            const std::string& c = n.name;
            if (c == "task" || c == "else") {
                continue;   // task ist eine Erklaerung; else wurde beim if erledigt
            }
            if (c == "affect") {
                if (n.args.empty()) {
                    continue;
                }
                const std::string ziel = gleich(n.args[0].text, "self") ? w.name : n.args[0].text;
                const bool flush = n.args.size() < 2 || !gleich(n.args[1].text, "INSERT");
                const int zi = wesen(ziel);
                if (zi < 0 || !wesen_[static_cast<std::size_t>(zi)]->gibt) {
                    vermisst.insert(klein(ziel));
                    hinweis("affect \"" + ziel + "\": gibt es zu diesem Zeitpunkt nicht - der Block entfaellt");
                    continue;
                }
                if (zi == wi) {
                    // affect auf sich selbst: der Block ersetzt/unterbricht
                    // den eigenen Ablauf - hier einfach eingereiht.
                    Rahmen neu;
                    neu.liste = &n.children;
                    neu.skript = skript;
                    neu.basis = p;
                    meldeTasks(w, n.children, skript, p);
                    if (flush) {
                        w.stapel.clear();
                        w.gruppen.clear();
                    }
                    w.stapel.push_back(std::move(neu));
                    continue;
                }
                schiebeEin(zi, n.children, skript, p, flush, endlos(w));
                // CheckAffect: "ents need to update upon being affected"
                aktualisiere(zi, jetzt_);
                continue;
            }
            if (c == "do" || c == "dowait") {
                if (n.args.empty()) {
                    continue;
                }
                const auto it = w.tasks.find(klein(n.args[0].text));
                if (it == w.tasks.end()) {
                    hinweis(w.name + ": task \"" + n.args[0].text + "\" unbekannt");
                    continue;
                }
                Rahmen neu;
                neu.liste = &it->second.knoten->children;
                neu.skript = it->second.skript;
                neu.basis = it->second.path;
                neu.art = Rahmen::Art::Task;
                neu.gruppe = naechsteGruppe_++;
                w.gruppeVon[klein(n.args[0].text)] = neu.gruppe;
                if (c == "dowait") {
                    neu.dannWarten = true;
                    neu.dowaitKnoten = &n;
                    neu.dowaitHerkunft = AblaufHerkunft{skript, p};
                }
                w.gruppen.push_back(neu.gruppe);
                // Das zaehlt als Befehl (auch im Spiel landet er auf der
                // Leiste) - ohne Dauer.
                zeichne(wi, &n, AblaufHerkunft{skript, p}, false);
                w.stapel.push_back(std::move(neu));
                continue;
            }
            if (c == "loop") {
                int runden = 1;
                if (!n.args.empty()) {
                    const std::string v = wertArg(w, n.args[0]);
                    runden = static_cast<int>(zahl(v, 1.0));
                }
                Rahmen neu;
                neu.liste = &n.children;
                neu.skript = skript;
                neu.basis = p;
                neu.art = Rahmen::Art::Schleife;
                neu.runden = runden;
                w.stapel.push_back(std::move(neu));
                continue;
            }
            if (c == "if") {
                const bool ja = bedingung(w, n);
                // das zugehoerige else: der naechste Befehl gleicher Ebene
                std::size_t j = r.i;
                while (j < r.liste->size() && (*r.liste)[j].kind != Node::Kind::Command) { ++j; }
                const bool hatElse = j < r.liste->size() && (*r.liste)[j].name == "else";
                const Node* zweig = &n;
                Path zp = p;
                if (!ja) {
                    if (hatElse) {
                        zweig = &(*r.liste)[j];
                        zp = r.basis;
                        zp.push_back(j);
                    } else {
                        zweig = nullptr;
                    }
                }
                if (hatElse) { r.i = j + 1; }
                if (zweig != nullptr) {
                    Rahmen neu;
                    neu.liste = &zweig->children;
                    neu.skript = skript;
                    neu.basis = zp;
                    w.stapel.push_back(std::move(neu));
                }
                continue;
            }
            if (c == "run") {
                if (n.args.empty()) {
                    continue;
                }
                zeichne(wi, &n, AblaufHerkunft{skript, p}, false);
                int idx = 0;
                const Script* s = ladeSkript(n.args[0].text, &idx);
                if (s == nullptr) {
                    continue;
                }
                notiereStart(idx);   // rc603
                Rahmen neu;
                neu.liste = &s->nodes;
                neu.skript = idx;
                neu.basis = Path{};
                meldeTasks(w, s->nodes, idx, Path{});
                w.stapel.push_back(std::move(neu));
                continue;
            }
            if (c == "flush") {
                // Flush( m_curSequence ): alles ausser dem laufenden Block
                // verwerfen.
                if (w.stapel.size() > 1) {
                    Rahmen oben = std::move(w.stapel.back());
                    w.stapel.clear();
                    w.stapel.push_back(std::move(oben));
                }
                continue;
            }
            *h = AblaufHerkunft{skript, p};
            return &n;
        }
        return nullptr;
    }

    bool warteFertig(Wesen& w, double t) {
        switch (w.warte) {
        case Wesen::kZeit:
            // TaskManager.cpp:1100 - streng kleiner: erst im Bild danach.
            if (welt_.bildMs > 0.0) {
                return w.stempel + w.dauer < t;
            }
            return w.stempel + w.dauer <= t;
        case Wesen::kSignal:
            if (signale_.count(w.warteName) != 0) {
                signale_.erase(w.warteName);   // ClearSignal
                return true;
            }
            return false;
        case Wesen::kGruppe: {
            const auto it = w.gruppeVon.find(w.warteName);
            if (it == w.gruppeVon.end()) {
                // Wait mit unbekanntem Task: die Engine wartet ewig
                // (GetTaskGroup liefert nullptr, completed bleibt false).
                return false;
            }
            return gruppeFertig(it->second);
        }
        default:
            return true;
        }
    }

    void aktualisiere(int wi, double t) {
        if (tiefe_ > 24) {
            return;
        }
        ++tiefe_;
        Wesen& w = *wesen_[static_cast<std::size_t>(wi)];
        int schutz = 0;
        while (w.gibt) {
            if (w.warte != Wesen::kNichts) {
                if (!warteFertig(w, t)) {
                    break;
                }
                // Ohne Takt (Proben) endet ein wait genau nach seiner Dauer.
                const double ende = (welt_.bildMs > 0.0 || w.warte != Wesen::kZeit) ? t : w.stempel + w.dauer;
                if (w.warteEintrag >= 0) {
                    eintraege_[static_cast<std::size_t>(w.warteEintrag)].ende = ende;
                }
                w.warte = Wesen::kNichts;
                w.warteEintrag = -1;
                if (welt_.bildMs <= 0.0) {
                    jetzt_ = ende;
                }
            }
            AblaufHerkunft h;
            const Node* n = naechster(wi, &h);
            // Der Schutz zaehlt auch das dowait: loop(-1){dowait("t")} mit
            // einem Task, der sofort fertig ist (nur affect), kam sonst ueber
            // das continue unten nie hier vorbei und hing die Vorschau auf
            // (Code-Pruefung 03.10.).
            if (++schutz > 20000) {
                hinweis(w.name + ": zu viele Befehle in einem Bild (Endlosschleife?)");
                w.stapel.clear();
                break;
            }
            if (n == nullptr) {
                if (w.warte != Wesen::kNichts) {
                    continue;   // dowait oder zurueckgeholtes Warten
                }
                break;
            }
            fuehreAus(wi, *n, h);
        }
        --tiefe_;
    }

    static bool endlos(const Wesen& w) {
        return std::any_of(w.stapel.begin(), w.stapel.end(), [](const Rahmen& r) {
            // loop ( -1 ) - und praktisch endlose Zaehlschleifen wie
            // loop ( 15000 ) (zehnmal in den Skripten der Mod).
            return r.ausEndlos || (r.art == Rahmen::Art::Schleife && (r.runden < 0 || r.runden > 100));
        });
    }

    // Tut sich noch etwas, das ein Ende hat? Endlosschleifen (loop ( -1 ),
    // meist Leerlauf-Animationen) und Warten auf ein Signal, das keiner
    // mehr gibt, zaehlen nicht - sonst liefe jede Szene bis zur Grenze.
    bool aktivEndlich() const {
        for (const auto& w : wesen_) {
            if (!w->gibt || endlos(*w) || w->endlosGefuettert) {
                continue;
            }
            // Ein wait ab 60 s ist ein Parkplatz (wait ( 999999 ) am Ende
            // vieler Skripte) - wie practicalEndMs in timeline.h. Darauf zu
            // warten hiesse, bis zur Grenze nichts zu tun.
            if ((w->warte == Wesen::kZeit && w->dauer < 60000.0) ||
                (w->warte == Wesen::kNichts && !w->stapel.empty())) {
                return true;
            }
            // Das Signal ist schon gefallen / die Gruppe ist fertig: geht
            // im naechsten Bild weiter.
            if (w->warte == Wesen::kSignal && signale_.count(w->warteName) != 0) {
                return true;
            }
            if (w->warte == Wesen::kGruppe) {
                const auto g = w->gruppeVon.find(w->warteName);
                if (g != w->gruppeVon.end() && gruppeFertig(g->second)) {
                    return true;
                }
            }
            for (const Aufgabe& a : w->fach) {
                // Wie beim wait: was erst in ueber 60 s fertig wuerde (eine
                // fehlende ROFF-Datei endet nie), ist kein Grund zu warten.
                if (a.id >= 0 && !a.nie && a.fertig - jetzt_ < 60000.0) {
                    return true;
                }
            }
        }
        return !geplant_.empty();
    }

    bool endlosDa() const {
        return std::any_of(wesen_.begin(), wesen_.end(), [](const auto& w) { return w->gibt && endlos(*w); });
    }

    void flachen();
};

void Nachbau::lauf(const Script& start, const std::string& startPfad) {
    const int s0 = skriptIndex(startPfad.empty() ? std::string{"(offen)"} : startPfad, &start);
    // Der Traeger: wer das Skript in der Karte ausfuehrt.
    bool figur = false;
    std::string traeger = (welt_.karte != nullptr && !startPfad.empty())
                              ? ablaufTraeger(*welt_.karte, startPfad, &figur)
                              : std::string{};
    aus_.traeger = traeger;
    aus_.traegerIstFigur = figur;
    {
        auto w = std::make_unique<Wesen>();
        w->name = traeger.empty() ? std::string{"(Skript)"} : traeger;
        bool sp = false;
        w->ent = traeger.empty() ? nullptr : findeKarte(traeger, &sp);
        w->reihe = reiheVon(w->ent);
        w->figur = figur;
        w->istClient = figur;   // rc601 (bewegung): ein NPC_spawner als Traeger
        w->gibt = true;
        if (w->ent != nullptr && !w->ent->origin.empty()) {
            w->ortBekannt = leseVek(w->ent->origin, w->ort);
            w->anfangBekannt = w->ortBekannt;   // rc603 fix603
            w->anfang = {w->ort[0], w->ort[1], w->ort[2]};
        }
        traeger_ = static_cast<int>(wesen_.size());
        wesenNach_[klein(w->name)] = traeger_;
        wesen_.push_back(std::move(w));
    }
    schiebeEin(traeger_, start.nodes, s0, Path{}, true);
    notiereStart(s0);   // rc603: das Startskript laeuft bei 0 los
    takte();
}

// rc603: die Schleife des Taktgebers - aus lauf herausgezogen, damit der
// Kartenstart (kartenstartLauf) dieselbe benutzt. Fuer lauf unveraendert.
void Nachbau::takte() {
    const double bild = welt_.bildMs;
    double ende = -1.0;
    double t = 0.0;
    const double stopp = welt_.grenzeMs;
    double ruhe = -1.0;
    for (int schritt = 0;; ++schritt) {
        jetzt_ = t;
        // 1. Aufgaben, die von selbst fertig werden (Stimme, Weg, Animation)
        for (auto& w : wesen_) {
            pruefeFaecher(*w, t);
        }
        // 2. Geplantes (Verzoegerungen, Spawns)
        std::stable_sort(geplant_.begin(), geplant_.end(), [](const Geplant& a, const Geplant& b) {
            return a.ms < b.ms || (a.ms == b.ms && a.folge < b.folge);
        });
        while (!geplant_.empty() && geplant_.front().ms <= t + 1e-6) {
            const Geplant g = geplant_.front();
            geplant_.erase(geplant_.begin());
            if (g.art == Geplant::Art::Benutzen) {
                if (!g.name.empty() && g.name[0] == '\x01') {
                    // trigger nach seiner Verzoegerung: usescript und Ziele
                    const std::string tn = g.name.substr(1);
                    if (welt_.karte != nullptr) {
                        for (const MapEntity& e : welt_.karte->entities) {
                            if (gleich(e.targetname, tn)) {
                                const int twi = wesenFuer(e);
                                Wesen& tw = *wesen_[static_cast<std::size_t>(twi)];
                                const auto it = tw.verhalten.find("usescript");
                                if (it != tw.verhalten.end()) {
                                    starteSkript(twi, it->second, g.ausloeser);
                                }
                                benutzeZiele(e, g.ausloeser);
                            }
                        }
                    }
                } else {
                    benutze(g.name, g.ausloeser, 1);
                }
            } else if (g.art == Geplant::Art::Skript) {
                starteSkript(g.wesen, g.name, g.ausloeser);
            } else if (g.art == Geplant::Art::Spawn && g.wesen >= 0) {
                Wesen& f = *wesen_[static_cast<std::size_t>(g.wesen)];
                if (!f.gibt) {
                    f.gibt = true;
                    f.stapel.clear();
                    f.warte = Wesen::kNichts;
                    aus_.erscheint[f.name] = t;
                    if (f.ent != nullptr && !f.ent->origin.empty()) {
                        f.ortBekannt = leseVek(f.ent->origin, f.ort);
                        f.anfangBekannt = f.ortBekannt;   // rc603 fix603
                        f.anfang = {f.ort[0], f.ort[1], f.ort[2]};
                    }
                    const auto it = f.verhalten.find("spawnscript");
                    if (it != f.verhalten.end()) {
                        starteSkript(g.wesen, it->second, g.ausloeser);
                    }
                }
            }
        }
        // 3. Jede Entity einmal, in Entity-Reihenfolge (G_RunFrame).
        std::vector<int> reihe(wesen_.size());
        for (std::size_t i = 0; i < reihe.size(); ++i) { reihe[i] = static_cast<int>(i); }
        std::stable_sort(reihe.begin(), reihe.end(), [this](int a, int b) {
            return wesen_[static_cast<std::size_t>(a)]->reihe < wesen_[static_cast<std::size_t>(b)]->reihe;
        });
        for (const int wi : reihe) {
            aktualisiere(wi, t);
        }
        // rc603: Kartenstart - der Spieler kommt ins Spiel (ClientBegin ->
        // ClientSpawn -> G_UseTargets der Startposition, g_client.cpp:2432-
        // 2434), am Ende des ersten Bildes im Spiel (kSpielerBeginnMs).
        if (kartenstart_ && !spielerDa_ && t >= kSpielerBeginnMs - 1e-6) {
            spielerBeginnt();
        }
        // Ende des Startskripts?
        const Wesen& tr = *wesen_[static_cast<std::size_t>(traeger_)];
        if (ende < 0.0 && tr.stapel.empty() && tr.warte == Wesen::kNichts) {
            ende = t;
        }
        // Schluss, wenn nur noch Endloses laeuft - nach dem Nachlauf.
        // rc603: im Kartenstart erst, wenn der Spieler da ist und nichts mehr
        // geplant (Verzoegerungen, Spawns) - vorher tut sich oft noch nichts.
        if (kartenstart_ && (!spielerDa_ || kartenstartGeplant())) {
            ruhe = -1.0;
        } else if (aktivEndlich()) {
            ruhe = -1.0;
        } else {
            if (ruhe < 0.0) { ruhe = t; }
            if (!endlosDa() || t >= ruhe + welt_.nachlaufMs) {
                break;
            }
        }
        if (t >= stopp) {
            std::string wer;
            for (const auto& w : wesen_) {
                if (!w->gibt || endlos(*w)) {
                    continue;
                }
                const bool wartet = w->warte == Wesen::kZeit && w->dauer < 60000.0;
                const bool bereit = w->warte == Wesen::kNichts && !w->stapel.empty();
                bool fach = false;
                for (const Aufgabe& fa : w->fach) {
                    fach = fach || (fa.id >= 0 && !fa.nie);
                }
                if ((wartet || bereit || fach) && wer.size() < 200) {
                    wer += " " + w->name + (wartet ? "(wait)" : bereit ? "(bereit)" : "(Aufgabe)");
                }
            }
            if (!geplant_.empty()) {
                wer += " geplant:" + std::to_string(geplant_.size());
            }
            hinweis("Grenze von " + std::to_string(static_cast<int>(stopp / 1000.0)) + " s erreicht - noch taetig:" + wer);
            break;
        }
        if (kartenstart_ && t < kLetztesLadebildMs - 1e-6) {
            // rc603: die Bilder des Ladens zu 100 ms (sv_init.cpp:316, 351).
            t += 100.0;
        } else if (kartenstart_ && t < kLetztesLadebildMs + 1e-6) {
            // Nach dem letzten Ladebild das erste Bild im Spiel.
            t = kErstesSpielbildMs;
        } else if (bild > 0.0) {
            t += bild;
        } else {
            // Ohne Takt: zum naechsten Zeitpunkt, an dem sich etwas tut.
            double n = stopp;
            for (const auto& w : wesen_) {
                if (w->warte == Wesen::kZeit) { n = std::min(n, w->stempel + w->dauer); }
                for (const Aufgabe& a : w->fach) {
                    if (a.id >= 0 && !a.nie && a.fertig > t) { n = std::min(n, a.fertig); }
                }
            }
            for (const Geplant& g : geplant_) { n = std::min(n, g.ms); }
            if (n <= t) { n = t + 1.0; }
            t = n;
        }
        if (schritt > 1000000) {
            break;
        }
    }
    aus_.endeMs = (ende >= 0.0) ? ende : t;
    aus_.laufMs = t;
    koerperAbschliessen();   // rc601 (bewegung, teil 3)
    flachen();
}

// --- rc603: die Karte vom Laden an (Reihenfolge der Skripte einer Mission) ----
//
// Gewuenscht: "kann er die scripts bei load mission in der Reihenfolge als Tab
// oeffnen, wie sie ingame abgespielt werden?" Dafuer laeuft hier kein
// Startskript, sondern das, was die Engine beim Laden einer Karte selbst
// anstoesst - Zeiten relativ zum Start der Karte (sv.time 1000, sv_init.cpp:288):
//
//   0     G_SpawnGEntityFromSpawnVars: jede Entity ausser NPC_*, die fuer ICARUS
//         gilt, startet ihr spawnscript (g_spawn.cpp:1114-1124 OpenJK,
//         :1278-1289 Movie Duels). Ausgelassen, was bei dieser Schwierigkeit
//         nicht spawnt (SpawnForCurrentDifficultySetting, "notsingle").
//   100   das spawnscript von worldspawn ueber einen eigenen Skriptstarter
//         (g_spawn.cpp:1660-1676 / :1870-1885): nextthink = level.time + 100.
//   300   trigger_always feuert seine Ziele (g_trigger.cpp:620-624 / :692-697).
//   350   NPC-Spawner ohne targetname denken (NPC_spawn.cpp:2049 / :3101):
//         NPC_Spawn_Go ohne "delay" (das gilt nur fuer use), NPC_Begin ein
//         FRAMETIME spaeter mit dem spawnscript.
//   550   ClientSpawn: G_UseTargets/G_UseTargets2 der Startposition
//         (g_client.cpp:2432-2434 / :4092-4094), am Ende des ersten Bildes im
//         Spiel (kSpielerBeginnMs - nach den spawnscripts der Spawner).
//
// Alles Weitere - run, use, Relais, Zaehler, Spawner, kill mit deathscript -
// rechnet der Nachbau wie bei jedem Ablauf. Was nur der Spieler ausloest
// (Trigger beim Betreten, ein Kampf), startet hier nicht.
void Nachbau::kartenstartLauf() {
    kartenstart_ = true;
    aus_.traeger.clear();
    aus_.traegerIstFigur = false;
    {
        // Ein Traeger ohne Ablauf: die Schleife braucht einen.
        auto w = std::make_unique<Wesen>();
        w->name = "(Karte)";
        w->reihe = 1 << 21;
        w->gibt = true;
        traeger_ = static_cast<int>(wesen_.size());
        wesenNach_[klein(w->name)] = traeger_;
        wesen_.push_back(std::move(w));
    }
    jetzt_ = 0.0;
    if (welt_.karte == nullptr) {
        spielerDa_ = true;
        takte();
        return;
    }
    const int stufe = welt_.spielstufe;
    const auto spawnt = [stufe](const MapEntity& e) { return stufe < 0 || spawntBeiSchwierigkeit(e, stufe); };
    const std::vector<MapEntity>& ents = welt_.karte->entities;
    for (std::size_t i = 0; i < ents.size(); ++i) {
        const MapEntity& e = ents[i];
        if (e.classname == "worldspawn") {
            // SP_worldspawn laeuft NICHT ueber G_SpawnGEntityFromSpawnVars -
            // sein spawnscript startet der Skriptstarter bei 100.
            const std::string ws = wertVon(e, "spawnscript");
            if (!ws.empty()) {
                auto w = std::make_unique<Wesen>();
                w->name = "(worldspawn)";
                w->reihe = static_cast<int>(ents.size()) + 1;   // G_Spawn nach allen anderen
                w->gibt = true;
                const int wi = static_cast<int>(wesen_.size());
                wesenNach_[klein(w->name)] = wi;
                wesen_.push_back(std::move(w));
                plane(Geplant{100.0, 0, Geplant::Art::Skript, ws, wi, -1});
            }
            continue;
        }
        if (!spawnt(e)) {
            continue;
        }
        // rc603 (Durchsicht): von der eigenen Spawnfunktion gleich wieder
        // freigegeben - fuer ICARUS gibt es sie nicht (freiBeimSpawnen).
        if (freiBeimSpawnen(e)) {
            continue;
        }
        if (istNpcSpawner(e)) {
            if (!e.targetname.empty()) {
                continue;   // wartet auf use
            }
            // rc603 (Durchsicht): ein Spawner ohne use-Funktion (ohne
            // targetname) benutzt seine Ziele gleich beim Spawnen -
            // NPC_Spawn_Do: if ( ent->e_UseFunc == useF_NULL ) { if (
            // ent->target ) G_UseTargets( ent, ent ); } (NPC_spawn.cpp:
            // 1765-1770 OpenJK, :2805-2811 Movie Duels), im Bild 400, in dem
            // NPC_Spawn_Go laeuft. Unabhaengig von NPC_targetname und
            // spawnscript; der Ausloeser ist der Spawner selbst.
            const std::string ziel = wertVon(e, "target");
            if (!ziel.empty()) {
                plane(Geplant{400.0, 0, Geplant::Art::Benutzen, ziel, -1, wesenFuer(e)});
            }
            // NPC_Spawn_Go bei 350, also im Bild 400; NPC_Begin bei 500 -
            // im ersten Bild danach (550) laeuft das spawnscript.
            std::string name = wertVon(e, "NPC_targetname");
            if (name.empty()) {
                if (wertVon(e, "spawnscript").empty()) {
                    continue;
                }
                name = namenloseFigur(e);
            }
            int fi = wesen(name);
            // rc603 (Durchsicht): gehoert der Name schon der Figur eines
            // ANDEREN Spawners, erzeugt dieser trotzdem seine eigene
            // (weitereFigur) - mit seinem spawnscript.
            if (wesen_[static_cast<std::size_t>(fi)]->ent != &e) {
                fi = weitereFigur(e, name);
            }
            gespawnt.insert(klein(name));
            plane(Geplant{400.0 + 100.0, 0, Geplant::Art::Spawn, name, fi, -1});
            continue;
        }
        if (e.classname == "trigger_always") {
            // G_UseTargets( ent, ent ): der Ausloeser ist der Trigger selbst.
            const std::string t = wertVon(e, "target");
            if (!t.empty()) {
                const int wi = wesenFuer(e);
                plane(Geplant{300.0, 0, Geplant::Art::Benutzen, t, -1, wi});
            }
        }
        const std::string sp = wertVon(e, "spawnscript");
        if (sp.empty()) {
            continue;
        }
        // ValidEntity (Q3_Interface.cpp:7572-7592 OpenJK): jede Entity mit
        // einem Verhaltensskript bekommt ICARUS, auch ohne Namen.
        const int wi = wesenFuer(e);
        wesen_[static_cast<std::size_t>(wi)]->gibt = true;
        starteSkript(wi, sp, -1);
    }
    takte();
}

// ClientSpawn: die Startposition waehlen und ihre Ziele benutzen. Ohne
// spawntarget nimmt SelectRandomDeathmatchSpawnPoint (g_client.cpp:229-280)
// eine Startposition OHNE targetname, per Zufall unter allen - hier die erste
// (die Missionen haben eine).
void Nachbau::spielerBeginnt() {
    spielerDa_ = true;
    if (welt_.karte == nullptr) {
        return;
    }
    const int stufe = welt_.spielstufe;
    const MapEntity* punkt = nullptr;
    for (const MapEntity& e : welt_.karte->entities) {
        if ((e.classname == "info_player_start" || e.classname == "info_player_deathmatch") && e.targetname.empty() &&
            (stufe < 0 || spawntBeiSchwierigkeit(e, stufe))) {
            punkt = &e;
            break;
        }
    }
    if (punkt == nullptr) {
        return;
    }
    const int spieler = wesen("player");
    benutzeZiele(*punkt, spieler, "target");
    benutzeZiele(*punkt, spieler, "target2");
}

bool Nachbau::kartenstartGeplant() const { return !geplant_.empty(); }

void Nachbau::flachen() {
    // Je Entity eine Spur, in der Reihenfolge der Ausfuehrung. Kamerabefehle
    // gehoeren OBEN hin, egal wer sie ausfuehrt: es gibt nur eine Kamera,
    // und camtrack.cpp liest nur die oberste Ebene.
    const bool traegerOben = !aus_.traegerIstFigur;
    std::map<int, std::vector<const Eintrag*>> spuren;
    std::vector<const Eintrag*> oben;
    for (const Eintrag& e : eintraege_) {
        const bool kamera = e.knoten != nullptr && !e.warten && e.knoten->name == "camera";
        if ((traegerOben && e.wesen == traeger_) || kamera) {
            oben.push_back(&e);
        } else {
            spuren[e.wesen].push_back(&e);
        }
    }
    const auto knoten = [](const std::string& name, std::vector<Arg> args) {
        Node n;
        n.kind = Node::Kind::Command;
        n.name = name;
        n.args = std::move(args);
        return n;
    };
    const auto warteKnoten = [&](double ms) {
        Arg a;
        a.kind = Arg::Kind::Number;
        char b[32];
        std::snprintf(b, sizeof(b), "%.3f", ms);
        a.text = b;
        return knoten("wait", {a});
    };
    // Eine Spur schreiben. Zwischen zwei Befehlen steht, was die Zeit
    // wirklich angehalten hat: die waits dieser Spur, jedes mit seiner
    // Herkunft (so bleibt es auf der Zeitleiste ziehbar) - es sei denn, ein
    // Befehl einer anderen Entity faellt mitten hinein. Dann wird geteilt,
    // und die Stuecke sind eingefuegt (ohne Herkunft).
    const auto fuelle = [&](std::vector<const Eintrag*> es, std::vector<Node>& ziel, const Path& basis) {
        std::stable_sort(es.begin(), es.end(), [](const Eintrag* a, const Eintrag* b) {
            return a->ms < b->ms || (a->ms == b->ms && a->folge < b->folge);
        });
        std::vector<const Eintrag*> warten;
        std::vector<const Eintrag*> befehle;
        for (const Eintrag* e : es) {
            if (e->knoten == nullptr) {
                continue;
            }
            if (e->warten) {
                if (e->ende > e->ms + 1e-6) {
                    warten.push_back(e);
                }
                continue;
            }
            const std::string& c = e->knoten->name;
            if (c == "do" || c == "dowait" || c == "run") {
                continue;   // Gerueste: ihr Inhalt steht einzeln da
            }
            befehle.push_back(e);
        }
        const auto neuerPfad = [&] {
            Path p = basis;
            p.push_back(ziel.size());
            return p;
        };
        const auto synth = [&](double ms) {
            if (ms > 1e-6) {
                ziel.push_back(warteKnoten(ms));
            }
        };
        std::size_t wi = 0;
        double uhr = 0.0;
        const auto bis = [&](double zu) {
            while (wi < warten.size() && warten[wi]->ende <= uhr + 1e-6) {
                ++wi;   // schon vorbei (von einem Einschub geteilt)
            }
            while (uhr < zu - 1e-6) {
                if (wi >= warten.size() || warten[wi]->ms >= zu - 1e-6) {
                    synth(zu - uhr);
                    uhr = zu;
                    break;
                }
                const Eintrag* w = warten[wi];
                if (w->ms > uhr + 1e-6) {
                    synth(w->ms - uhr);
                    uhr = w->ms;
                }
                if (w->ms >= uhr - 1e-6 && w->ende <= zu + 1e-6) {
                    const Path p = neuerPfad();
                    ziel.push_back(warteKnoten(w->ende - w->ms));
                    aus_.herkunft[p] = w->herkunft;
                    uhr = w->ende;
                    ++wi;
                } else {
                    // Ein Einschub faellt in dieses wait: bis dahin teilen.
                    const double stueck = std::min(zu, w->ende) - uhr;
                    synth(stueck);
                    uhr += stueck;
                    if (uhr >= w->ende - 1e-6) { ++wi; }
                }
            }
        };
        for (const Eintrag* e : befehle) {
            bis(e->ms);
            Node n = *e->knoten;
            n.children.clear();
            n.hasBlock = false;
            const std::string& c = n.name;
            // self aufloesen: die Auswerter kennen den Traeger nicht.
            if ((c == "remove" || c == "kill") && !n.args.empty() && gleich(n.args[0].text, "self")) {
                n.args[0].text = wesen_[static_cast<std::size_t>(e->wesen)]->name;
            }
            const Path p = neuerPfad();
            ziel.push_back(std::move(n));
            aus_.herkunft[p] = e->herkunft;
        }
        // Die waits nach dem letzten Befehl (die Spur laeuft noch aus).
        double letzt = uhr;
        for (const Eintrag* w : warten) {
            letzt = std::max(letzt, w->ende);
        }
        bis(letzt);
    };
    Script& s = aus_.flach;
    s.header = "//Generated by BehavEd";
    s.nodes.clear();
    // Erst die affect-Bloecke (alle ab 0), dann die Befehle oben.
    std::vector<int> reihe;
    for (const auto& [wi, es] : spuren) {
        if (!es.empty()) {
            reihe.push_back(wi);
        }
    }
    std::stable_sort(reihe.begin(), reihe.end(), [&](int a, int b) {
        return spuren[a].front()->folge < spuren[b].front()->folge;
    });
    for (const int wi : reihe) {
        Arg name;
        name.kind = Arg::Kind::String;
        name.text = wesen_[static_cast<std::size_t>(wi)]->name;
        Arg art;
        art.kind = Arg::Kind::Ident;
        art.text = "FLUSH";
        art.typeset = "AFFECT_TYPE";
        Node a = knoten("affect", {name, art});
        a.hasBlock = true;
        const Path basis{s.nodes.size()};
        fuelle(spuren[wi], a.children, basis);
        s.nodes.push_back(std::move(a));
    }
    fuelle(oben, s.nodes, Path{});
}

}  // namespace

const AblaufHerkunft* Ablauf::woher(const Path& p) const {
    const auto it = herkunft.find(p);
    return (it == herkunft.end()) ? nullptr : &it->second;
}

std::string ablaufTraeger(const MapData& karte, const std::string& startPfad, bool* istFigur) {
    if (istFigur != nullptr) {
        *istFigur = false;
    }
    const std::string gesucht = skriptSchluessel(startPfad);
    if (gesucht.empty()) {
        return {};
    }
    // Bevorzugt, wer das Skript STARTET (usescript, spawnscript), vor
    // Verhaltensskripten wie deathscript.
    const MapEntity* bester = nullptr;
    int rang = 99;
    for (const MapEntity& e : karte.entities) {
        for (const auto& [k, v] : e.keys) {
            const std::string kk = klein(k);
            if (kk.find("script") == std::string::npos || skriptSchluessel(v) != gesucht) {
                continue;
            }
            const int r = (kk == "usescript") ? 0 : (kk == "spawnscript") ? 1 : 2;
            if (r < rang) {
                rang = r;
                bester = &e;
            }
        }
    }
    if (bester == nullptr) {
        return {};
    }
    if (istNpcSpawner(*bester)) {
        const std::string* n = schluessel(*bester, "NPC_targetname");
        if (n != nullptr && !n->empty()) {
            if (istFigur != nullptr) {
                *istFigur = true;
            }
            return *n;
        }
    }
    const std::string* st = schluessel(*bester, "script_targetname");
    return (st != nullptr && !st->empty()) ? *st : bester->targetname;
}

Ablauf simuliereAblauf(const Script& start, const std::string& startPfad, const AblaufWelt& welt) {
    Ablauf a;
    Nachbau n(welt, a, {});
    n.lauf(start, startPfad);
    // Eine Figur, die angesprochen wird, deren Spawner aber in diesem Ablauf
    // NIE benutzt wird, steht im Spiel schon da: das Level hat sie vorher
    // erzeugt (md_ga_jedi: win3 in grievous_jedi). Dann noch einmal mit ihr.
    // Auch wenn der Ablauf sie SPAETER selbst erzeugt: wer sie vorher
    // anspricht, rechnet damit, dass sie schon steht (md_ga
    // aftersnipers_jedi: sbd_as stammt aus dem Kampf davor).
    std::set<std::string> vorher = n.vermisst;
    if (vorher.empty()) {
        return a;
    }
    Ablauf b;
    Nachbau n2(welt, b, vorher);
    n2.lauf(start, startPfad);
    return b;
}

Ablauf simuliereKartenstart(const AblaufWelt& welt) {
    Ablauf a;
    Nachbau n(welt, a, {});
    n.kartenstartLauf();
    return a;
}

std::vector<std::size_t> skripteNachStart(const std::vector<std::string>& pfade, const Ablauf& a) {
    // Erster Start je Skript (ohne Gross/Klein, Endung und "scripts/").
    std::map<std::string, std::pair<double, int>> beginn;
    for (std::size_t i = 0; i < a.skripte.size() && i < a.skriptBeginn.size() && i < a.skriptFolge.size(); ++i) {
        if (a.skriptBeginn[i] >= 0.0) {
            beginn.emplace(skriptSchluessel(a.skripte[i]), std::make_pair(a.skriptBeginn[i], a.skriptFolge[i]));
        }
    }
    struct Platz {
        std::size_t index;
        double ms;
        int folge;
    };
    std::vector<Platz> gestartet;
    std::vector<std::size_t> nie;
    for (std::size_t i = 0; i < pfade.size(); ++i) {
        const auto it = beginn.find(skriptSchluessel(pfade[i]));
        if (it != beginn.end()) {
            gestartet.push_back(Platz{i, it->second.first, it->second.second});
        } else {
            nie.push_back(i);
        }
    }
    // Nach der Zeit, bei gleicher Zeit (dasselbe Bild) nach der Folge der
    // Starts im Nachbau; nie gestartete in ihrer Reihenfolge dahinter.
    std::stable_sort(gestartet.begin(), gestartet.end(), [](const Platz& x, const Platz& y) {
        if (x.ms != y.ms) {
            return x.ms < y.ms;
        }
        return x.folge < y.folge;
    });
    std::vector<std::size_t> out;
    out.reserve(pfade.size());
    for (const Platz& p : gestartet) {
        out.push_back(p.index);
    }
    out.insert(out.end(), nie.begin(), nie.end());
    return out;
}

// --- rc601 (bewegung, teil 3): alle Figuren im selben Takt ---------------------
namespace {
bool gleichePlaene(const std::shared_ptr<const std::map<std::uint64_t, std::shared_ptr<const LaufPlan>>>& a,
                   const std::shared_ptr<const std::map<std::uint64_t, std::shared_ptr<const LaufPlan>>>& b) {
    const std::size_t na = a ? a->size() : 0U;
    const std::size_t nb = b ? b->size() : 0U;
    if (na != nb) { return false; }
    if (na == 0U) { return true; }
    for (const auto& [k, p] : *a) {
        const auto it = b->find(k);
        if (it == b->end()) { return false; }
        const LaufPlan& x = *p;
        const LaufPlan& y = *it->second;
        if (x.takte.size() != y.takte.size() || x.ankunftMs != y.ankunftMs || x.ruheMs != y.ruheMs) { return false; }
        for (std::size_t i = 0; i < x.takte.size(); ++i) {
            if (x.takte[i].t != y.takte[i].t || x.takte[i].ort != y.takte[i].ort) { return false; }
        }
    }
    return true;
}

// --- rc601 integ: dieselben Eingaben fuer navKoerperLauf -----------------------
//
// Gemessen (Probe P4, bewegungtest): die sechste Rechnung des gemeinsamen
// Takts bekam Bit fuer Bit dieselben Abschnitte wie die fuenfte - und
// navKoerperLauf ist eine reine Funktion seiner Eingaben (dieselbe NavWelt in
// allen Durchgaengen). Dann kaemen dieselben Plaene heraus, gleichePlaene
// waere wahr und die Schleife endete; das Ergebnis ist also ohne diese
// Rechnung dasselbe (~23 ms von ~155 ms in P4). Verglichen wird Bit fuer Bit
// (memcmp je Feld: -0.0 ist nicht 0.0, NaN gleich nur bei gleichen Bits),
// der Pfad im Startzustand nach Inhalt, und ein gesetztes fremdGier
// (std::function, nicht vergleichbar) gilt immer als verschieden - im Zweifel
// wird gerechnet. Den Plan ohne die anderen (NavAbschnitt::allein) legt jede
// Ablauf-Rechnung neu an (anderer Zeiger); er ist planeLauf(nav, start, werte,
// abMs, grenzeMs) mit genau den hier verglichenen Eingaben und derselben
// NavWelt (abschnittMerken in Nachbau), also ebenfalls gleich.
std::atomic<bool> gKoerperAbkuerzung{true};

template <class T>
bool gleichBits(const T& a, const T& b) {
    static_assert(std::is_floating_point_v<T> || std::is_same_v<T, NavVec>, "nur float/double/NavVec");
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}
// Der Pfad (NAV::GoTo) haengt als shared_ptr am Zustand; jede Ablauf-Rechnung
// legt ihn neu an - also den Inhalt vergleichen (lauter 4-Byte-Felder ohne
// Luecken, static_assert unten).
bool gleicherPfad(const std::shared_ptr<const std::vector<NavPfadPunkt>>& a,
                  const std::shared_ptr<const std::vector<NavPfadPunkt>>& b) {
    if (a == b) { return true; }
    if (!a || !b || a->size() != b->size()) { return false; }
    return a->empty() || std::memcmp(a->data(), b->data(), a->size() * sizeof(NavPfadPunkt)) == 0;
}
bool gleicherZustand(const LaufZustand& a, const LaufZustand& b) {
    // Neue Felder in LaufZustand gehoeren hier hinein (static_assert unten).
    return gleichBits(a.ort, b.ort) && gleichBits(a.tempoVek, b.tempoVek) && a.tempo == b.tempo &&
           gleichBits(a.gier, b.gier) && gleichBits(a.wunschGier, b.wunschGier) && gleichBits(a.pfadGier, b.pfadGier) &&
           a.seiteAusweichen == b.seiteAusweichen && a.letzteSeite == b.letzteSeite && a.seiteBis == b.seiteBis &&
           a.safeCheckBis == b.safeCheckBis && a.safeDauerBis == b.safeDauerBis && a.wegpunkt == b.wegpunkt &&
           a.letzterWegpunkt == b.letzterWegpunkt && a.wegpunktBis == b.wegpunktBis && a.zielWegpunkt == b.zielWegpunkt &&
           a.zielLetzterWegpunkt == b.zielLetzterWegpunkt && a.zielWegpunktBis == b.zielWegpunktBis &&
           a.pfadDa == b.pfadDa && a.pfadEnde == b.pfadEnde && a.pfadErfolg == b.pfadErfolg &&
           a.pfadAStarBis == b.pfadAStarBis && gleicherPfad(a.pfad, b.pfad) && a.pfadGroesse == b.pfadGroesse &&
           a.amBoden == b.amBoden && a.pmZeit == b.pmZeit && gleichBits(a.gesetztMs, b.gesetztMs) &&
           a.gesetztSpawn == b.gesetztSpawn && a.gesetztImFesten == b.gesetztImFesten &&
           gleichBits(a.denkNullMs, b.denkNullMs) && gleichBits(a.letzteBewegung, b.letzteBewegung) &&   // rc601 integ
           a.letzterVor == b.letzterVor && a.letzterSeit == b.letzterSeit && a.letzteGehTaste == b.letzteGehTaste;
}
bool gleicheWerte(const LaufWerte& a, const LaufWerte& b) {
    // Neue Felder in LaufWerte gehoeren hier hinein (static_assert unten).
    if (a.fremdGier || b.fremdGier) { return false; }
    return a.zielGesetzt == b.zielGesetzt && gleichBits(a.ziel, b.ziel) && gleichBits(a.zielRadius, b.zielRadius) &&
           gleichBits(a.maxTempo, b.maxTempo) && gleichBits(a.gehTempo, b.gehTempo) && gleichBits(a.drehRate, b.drehRate) &&
           gleichBits(a.halbBreite, b.halbBreite) && gleichBits(a.hoeheOben, b.hoeheOben) &&
           a.gangSchalter == b.gangSchalter && gleichBits(a.stufe, b.stufe) && a.ohneKoerper == b.ohneKoerper &&
           gleichBits(a.schwerkraft, b.schwerkraft) && a.fallen == b.fallen && gleichBits(a.zielAbMs, b.zielAbMs) &&
           a.spawnVorlauf == b.spawnVorlauf && a.spawnImFesten == b.spawnImFesten &&
           a.verhaltenTaktMs == b.verhaltenTaktMs;   // rc601 integ
}
#if defined(_MSC_VER) && defined(_M_X64)
// Waechst eine dieser Strukturen, fehlt das neue Feld oben im Vergleich.
static_assert(sizeof(NavPfadPunkt) == 9U * 4U, "NavPfadPunkt mit Luecken oder neuem Feld: gleicherPfad pruefen");
static_assert(sizeof(LaufZustand) == 176U, "neues Feld in LaufZustand: gleicherZustand ergaenzen");
static_assert(sizeof(LaufWerte) == 144U, "neues Feld in LaufWerte: gleicheWerte ergaenzen");
static_assert(sizeof(NavAbschnitt) == 376U, "neues Feld in NavAbschnitt: gleicheEingaben ergaenzen");
static_assert(sizeof(NavFigurKoerper) == 56U, "neues Feld in NavFigurKoerper: gleicheEingaben ergaenzen");
#endif
bool gleicheEingaben(const Ablauf& a, const std::vector<NavAbschnitt>& abschnitte, const std::vector<NavFigurKoerper>& figuren,
                     const std::vector<std::uint64_t>& schluessel) {
    if (a.laufAbschnitte.size() != abschnitte.size() || a.laufFiguren.size() != figuren.size() ||
        a.laufSchluessel != schluessel) {
        return false;
    }
    for (std::size_t i = 0; i < abschnitte.size(); ++i) {
        const NavAbschnitt& x = a.laufAbschnitte[i];
        const NavAbschnitt& y = abschnitte[i];
        if (x.figur != y.figur || !gleichBits(x.abMs, y.abMs) || !gleichBits(x.grenzeMs, y.grenzeMs) ||
            !gleichBits(x.schnittMs, y.schnittMs) || x.sprung != y.sprung ||
            !gleicherZustand(x.start, y.start) || !gleicheWerte(x.werte, y.werte)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < figuren.size(); ++i) {
        const NavFigurKoerper& x = a.laufFiguren[i];
        const NavFigurKoerper& y = figuren[i];
        if (x.reihe != y.reihe || !gleichBits(x.halbBreite, y.halbBreite) || !gleichBits(x.hoeheOben, y.hoeheOben) ||
            !gleichBits(x.daAbMs, y.daAbMs) || !gleichBits(x.daBisMs, y.daBisMs) || x.orte.size() != y.orte.size()) {
            return false;
        }
        for (std::size_t j = 0; j < x.orte.size(); ++j) {
            if (!gleichBits(x.orte[j].first, y.orte[j].first) || !gleichBits(x.orte[j].second, y.orte[j].second)) {
                return false;
            }
        }
    }
    return true;
}
}  // namespace

void ablaufKoerperAbkuerzungAn(bool an) { gKoerperAbkuerzung.store(an, std::memory_order_relaxed); }

Ablauf simuliereAblaufMitKoerpern(const Script& start, const std::string& startPfad, AblaufWelt& welt, int* durchgaenge) {
    // Der gemeinsame Takt haengt an ALLEN Figuren - nichts aus einer frueheren
    // Rechnung uebernehmen.
    welt.koerperPlaene.reset();
    Ablauf a = simuliereAblauf(start, startPfad, welt);
    int n = 1;
    std::shared_ptr<const NavWelt> eigene;
    // rc601 integ: die Eingaben der letzten Rechnung (gleicheEingaben).
    const bool abkuerzen = gKoerperAbkuerzung.load(std::memory_order_relaxed);
    bool vorDa = false;
    std::vector<NavAbschnitt> vorAbschnitte;
    std::vector<NavFigurKoerper> vorFiguren;
    std::vector<std::uint64_t> vorSchluessel;
    for (int i = 0; i < 6; ++i) {
        if (a.laufFiguren.size() < 2) { break; }
        if (abkuerzen && vorDa && gleicheEingaben(a, vorAbschnitte, vorFiguren, vorSchluessel)) { break; }
        if (!welt.nav && !eigene) {
            eigene = navWeltAusEntities(welt.karte != nullptr ? welt.karte->entities : std::vector<MapEntity>{});
        }
        const NavWelt& nw = welt.nav ? *welt.nav : *eigene;
        const NavKoerperErgebnis e = navKoerperLauf(nw, a.laufFiguren, a.laufAbschnitte);
        auto karte = std::make_shared<std::map<std::uint64_t, std::shared_ptr<const LaufPlan>>>();
        for (std::size_t j = 0; j < e.plaene.size() && j < a.laufSchluessel.size(); ++j) {
            if (e.plaene[j] != a.laufAbschnitte[j].allein) { (*karte)[a.laufSchluessel[j]] = e.plaene[j]; }
        }
        const std::shared_ptr<const std::map<std::uint64_t, std::shared_ptr<const LaufPlan>>> neu = karte;
        if (gleichePlaene(neu, welt.koerperPlaene)) { break; }
        welt.koerperPlaene = neu;
        if (abkuerzen) {
            vorAbschnitte = a.laufAbschnitte;
            vorFiguren = a.laufFiguren;
            vorSchluessel = a.laufSchluessel;
            vorDa = true;
        }
        a = simuliereAblauf(start, startPfad, welt);
        ++n;
    }
    if (durchgaenge != nullptr) { *durchgaenge = n; }
    return a;
}

}  // namespace bhed
