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
#include "bhed/scene.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
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

private:
    const AblaufWelt& welt_;
    Ablauf& aus_;
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
        geladen_.push_back(s);
        skriptNach_[k] = i;
        return i;
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
        const int i = static_cast<int>(wesen_.size());
        wesen_.push_back(std::move(w));
        wesenVonEnt_[&e] = i;
        return i;
    }

    // Schluessel der Karte in die Buchfuehrung uebernehmen.
    static void bereite(Wesen& w, const MapEntity& e) {
        if (!e.origin.empty()) {
            w.ortBekannt = leseVek(e.origin, w.ort);
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
        } else if (e == nullptr) {
            // Unbekannt: im Spiel liefe affect ins Leere. Wir legen sie
            // trotzdem an (sonst kann man ein neues Skript ohne Karte gar
            // nicht ansehen), aber nur, wenn keine Karte da ist.
            w->gibt = (welt_.karte == nullptr || welt_.karte->entities.empty());
            w->figur = w->gibt;
        } else if (spawner) {
            w->figur = true;
            // Ein Spawner OHNE targetname erzeugt die Figur beim Laden der
            // Karte (SP_NPC_spawner, NPC_spawn.cpp:3032). Mit targetname
            // erst, wenn er benutzt wird - es sei denn, das geschah schon
            // VOR dieser Zwischensequenz (vorhanden_, zweiter Lauf).
            w->gibt = e->targetname.empty() || vorhanden_.count(k) != 0;
        }
        if (e != nullptr) {
            bereite(*w, *e);
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
            erledige(a.id);
            a = Aufgabe{};
        }
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
            if (npc.empty()) {
                return;
            }
            const int fi = wesen(npc);
            gespawnt.insert(klein(npc));
            // NPC_Spawn_Do legt die Figur unsichtbar an (EF_NODRAW) und
            // ruft NPC_Begin ein FRAMETIME (100 ms, g_local.h:48) spaeter:
            // erst dort meldet InitEntity sie bei ICARUS an, und erst dort
            // laeuft das spawnscript (NPC_spawn.cpp:1910, 1956).
            plane(Geplant{jetzt_ + delay + 100.0, 0, Geplant::Art::Spawn, npc, fi, ausloeser});
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
            const float t = welt_.tempo(w.name, gehen);
            if (t > 0.0F) {
                return t;
            }
        }
        return gehen ? 90.0F : 300.0F;   // NPC_stats.cpp
    }

    // Der Radius einer waypoint_navgoal (Schluessel "radius", Vorgabe 12,
    // SP_waypoint_navgoal, g_nav.cpp:262).
    double markenRadius(const std::string& name) const {
        if (welt_.karte != nullptr) {
            for (const MapEntity& e : welt_.karte->entities) {
                if (gleich(e.targetname, name)) {
                    const std::string r = wertVon(e, "radius");
                    if (!r.empty() && zahl(r) > 0.0) {
                        return zahl(r);
                    }
                    break;
                }
            }
        }
        return 12.0;
    }

    // Eine Marke im Sinne von TAG_Add: ref_tag (g_ref.cpp) und
    // waypoint_navgoal (g_nav.cpp, RTF_NAVGOAL). Ohne Gross/klein.
    bool istMarke(const std::string& name) const {
        if (welt_.karte == nullptr) {
            return true;   // ohne Karte: wie bisher als Weg rechnen
        }
        for (const MapEntity& e : welt_.karte->entities) {
            if (gleich(e.targetname, name) &&
                (e.classname == "ref_tag" || e.classname.rfind("waypoint_navgoal", 0) == 0)) {
                return true;
            }
        }
        return false;
    }

    bool ortVon(const std::string& name, float out[3]) const {
        if (welt_.karte == nullptr) {
            return false;
        }
        if (gleich(name, "player")) {
            // Der Spieler: wo er gerade steht, sonst am Startpunkt.
            const auto it = wesenNach_.find("player");
            if (it != wesenNach_.end() && wesen_[static_cast<std::size_t>(it->second)]->ortBekannt) {
                for (int a = 0; a < 3; ++a) { out[a] = wesen_[static_cast<std::size_t>(it->second)]->ort[a]; }
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
            if (!w.figur || wert.empty() || gleich(wert, "NULL")) {
                return true;
            }
            float ziel[3];
            if (!ortVon(wert, ziel)) {
                hinweis("Wegpunkt \"" + wert + "\" fehlt in der Karte");
                return true;
            }
            // Nur eine MARKE (ref_tag, waypoint_navgoal - TAG_GetOrigin2)
            // haelt die Aufgabe offen. Ein gewoehnliches Entity als Ziel
            // (etwa "player") setzt goalEntity und gibt qfalse zurueck
            // (Q3_SetNavGoal, Q3_Interface.cpp:2142 ff.): die Figur geht
            // los, aber dowait wartet NICHT auf ihr Ankommen.
            if (!istMarke(wert)) {
                for (int a = 0; a < 3; ++a) { w.ort[a] = ziel[a]; }
                w.ortBekannt = true;
                return true;
            }
            const bool geh = w.gehend && !w.rennend;
            const float v = tempo(w, geh);
            if (!(v > 0.0F)) {
                // Tempo 0: die Figur bleibt stehen, die Aufgabe offen.
                setzeFach(w, kWeg, id, 0.0, true);
                return false;
            }
            double d = 0.0;
            if (w.ortBekannt) {
                float s = 0.0F;
                for (int a = 0; a < 3; ++a) {
                    const float q = ziel[a] - w.ort[a];
                    s += q * q;
                }
                const double weg = std::sqrt(static_cast<double>(s));
                const double ux = (weg > 0.0) ? (ziel[0] - w.ort[0]) / weg : 1.0;
                const double uy = (weg > 0.0) ? (ziel[1] - w.ort[1]) / weg : 0.0;
                d = gehDauerMs(weg, v, ux, uy, markenRadius(wert));
            }
            for (int a = 0; a < 3; ++a) { w.ort[a] = ziel[a]; }
            w.ortBekannt = true;
            setzeFach(w, kWeg, id, t + d, false);
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
        if (k == "set_origin" || k == "set_teleport_dest") {
            float o[3];
            if (leseVek(wert, o)) {
                for (int a = 0; a < 3; ++a) { w.ort[a] = o[a]; }
                w.ortBekannt = true;
            } else if (n.args[1].kind == Arg::Kind::Expr) {
                // $tag( "name", ORIGIN )$
                const std::string& x = n.args[1].text;
                const auto q1 = x.find('"');
                const auto q2 = (q1 == std::string::npos) ? q1 : x.find('"', q1 + 1);
                if (q2 != std::string::npos && ortVon(x.substr(q1 + 1, q2 - q1 - 1), o)) {
                    for (int a = 0; a < 3; ++a) { w.ort[a] = o[a]; }
                    w.ortBekannt = true;
                }
            }
            return true;
        }
        if (k == "set_copy_origin") {
            const int zi = wesen(wert, false);
            if (zi >= 0 && wesen_[static_cast<std::size_t>(zi)]->ortBekannt) {
                for (int a = 0; a < 3; ++a) { w.ort[a] = wesen_[static_cast<std::size_t>(zi)]->ort[a]; }
                w.ortBekannt = true;
            }
            return true;
        }
        if (k == "set_walking") { w.gehend = gleich(wert, "true"); if (w.gehend) { w.rennend = false; } return true; }
        if (k == "set_running") { w.rennend = gleich(wert, "true"); if (w.rennend) { w.gehend = false; } return true; }
        if (k == "set_walkspeed") { w.gehTempo = static_cast<float>(zahl(wert)); return true; }
        if (k == "set_runspeed") { w.laufTempo = static_cast<float>(zahl(wert)); return true; }
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
            if (n == nullptr) {
                if (w.warte != Wesen::kNichts) {
                    continue;   // dowait oder zurueckgeholtes Warten
                }
                break;
            }
            if (++schutz > 20000) {
                hinweis(w.name + ": zu viele Befehle in einem Bild (Endlosschleife?)");
                w.stapel.clear();
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
        w->gibt = true;
        if (w->ent != nullptr && !w->ent->origin.empty()) {
            w->ortBekannt = leseVek(w->ent->origin, w->ort);
        }
        traeger_ = static_cast<int>(wesen_.size());
        wesenNach_[klein(w->name)] = traeger_;
        wesen_.push_back(std::move(w));
    }
    schiebeEin(traeger_, start.nodes, s0, Path{}, true);

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
        // Ende des Startskripts?
        const Wesen& tr = *wesen_[static_cast<std::size_t>(traeger_)];
        if (ende < 0.0 && tr.stapel.empty() && tr.warte == Wesen::kNichts) {
            ende = t;
        }
        // Schluss, wenn nur noch Endloses laeuft - nach dem Nachlauf.
        if (aktivEndlich()) {
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
        if (bild > 0.0) {
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
    flachen();
}

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

}  // namespace bhed
