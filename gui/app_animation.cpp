// app_animation.cpp - Mover selbst animieren (Schiffe, Tueren, Plattformen)
//
// Gewuenscht (11.10.2026): "das man solche sachen selbst animieren kann" -
// etwa das fliegende Schiff. Der Kern (Schluessel <-> affect-Block) steht in
// bhed/moveranim.h. Hier die Oberflaeche:
//
//   - Inspektor: "Animieren" bei beweglichen Entities (func_*,
//     misc_model_breakable, fx_runner)
//   - Fenster: Schluessel mit Zeit, Ort, Winkel; "Schluessel am Zeiger",
//     loeschen, hinspringen; weich oder gleichmaessig (Mover-Key "linear")
//   - Karte: die Bahn durch die Schluessel, Rauten zum Ziehen (in der
//     Bildebene; Umschalt nur senkrecht)
//   - jede Aenderung schreibt den Block neu: ein Rueckgaengig-Schritt, und
//     die Zeitleiste spielt die Bewegung sofort
//
// Seit rc599 auch als ROFF-Datei ("ich will das man auch direkt eine Roff
// datei bauen und aendern kann"): dieselben Schluessel, gespeichert als
// scripts/<name>.rof, die das Skript mit play ( "PLAY_ROFF", ... ) abspielt.
// Die Schluessel stehen als Kommentarzeile vor dem play (bhed/roffanim.h);
// die Datei wird daraus gebacken - fuer die Vorschau sofort, auf die Platte
// beim Speichern des Skripts. Eine fertige Datei aus den Spielarchiven wird
// in Schluessel zerlegt und als Kopie unter neuem Namen bearbeitet.
#include "app_internal.h"

#include "bhed/moveranim.h"
#include "bhed/roffanim.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace bhed::gui {
namespace {

std::string animNameVon(const MapEntity& e) {
    if (const std::string* s = e.find("script_targetname"); s != nullptr && !s->empty()) { return *s; }
    return e.targetname;
}

const MapEntity* animEntityPtr() {
    const int i = g_app->animEntity;
    if (i < 0 || static_cast<std::size_t>(i) >= g_app->map.entities.size()) { return nullptr; }
    return &g_app->map.entities[static_cast<std::size_t>(i)];
}

bool roffModus() { return !g_app->animRoff.empty(); }

// Wo steht die Entity zur Zeit ms? Aus der Szene (bewegt), sonst ihr Ursprung.
void stellungBei(const MapEntity& e, double ms, std::array<float, 3>& ort, std::array<float, 3>& winkel) {
    float o[3] = {0.0F, 0.0F, 0.0F};
    entityOrt(e, o);
    ort = {o[0], o[1], o[2]};
    winkel = {0.0F, 0.0F, 0.0F};
    if (const std::string* a = e.find("angles")) {
        std::istringstream is(*a);
        is >> winkel[0] >> winkel[1] >> winkel[2];
    } else if (const std::string* a1 = e.find("angle")) {
        winkel[1] = static_cast<float>(std::atof(a1->c_str()));
    }
    const std::string name = animNameVon(e);
    for (const Actor& ac : g_app->scene.actors) {
        if (ac.name != name || ac.steps.empty()) { continue; }
        const ActorState st = ac.at(ms);
        ort = {st.pos[0], st.pos[1], st.pos[2]};
        winkel = {st.angles[0], st.angles[1], st.angles[2]};
        break;
    }
}

// --- ROFF-Modus (rc599) ------------------------------------------------------

// Was zur bearbeiteten Bahn gehoert: die Schluessel aus der Marke, wann das
// play laeuft und wo die Entity dann steht (dort beginnt die Bahn).
struct RoffZustand {
    RoffAnim anim;
    Path marke;
    bool eigenerBlock = false;
    std::size_t blockIndex = 0;      // Marke des eigenen Blocks (oberste Ebene)
    std::string blockName;           // wen der affect-Block heute nennt
    // Wann das play laeuft (aus der Szene - ein Spielbild nach dem wait
    // davor) und was im eigenen Block als wait steht. Geschrieben wird
    // immer der Unterschied aufs wait, sonst rutschte die Bahn mit jeder
    // Aenderung ein Spielbild weiter.
    double playMs = 0.0;
    double blockWait = 0.0;
    float start[3]{};
    float startW[3]{};
};

// Das play dieser Datei in der Szene: wann und ab wo.
const ActorStep* roffSchritt(const std::string& entName, const std::string& datei) {
    for (const Actor& ac : g_app->scene.actors) {
        if (ac.name != entName) { continue; }
        for (const ActorStep& st : ac.steps) {
            if (st.kind == ActorStep::Kind::Roff && gleicheRoff(st.text, datei)) { return &st; }
        }
    }
    return nullptr;
}

bool roffZustand(const MapEntity& e, RoffZustand& z) {
    const Script& s = g_app->doc.script();
    const auto f = findeRoffMarke(s, g_app->animRoff);
    if (!f) { return false; }
    z.anim = f->anim;
    z.marke = f->path;
    const std::string name = animNameVon(e);
    // Nach der Datei gesucht: heisst die Entity inzwischen anders, bleibt es
    // ihr eigener Block (rc599).
    const auto blk = findeRoffBlockNachDatei(s, g_app->animRoff);
    z.eigenerBlock = blk && f->path.size() == 1U && f->path[0] == *blk;
    if (z.eigenerBlock) {
        RoffAnim tmp;
        (void)leseRoffBlock(s, *blk, tmp, z.blockWait);
        z.blockIndex = *blk;
        const Node& aff = s.nodes[*blk + 1];
        z.blockName = aff.args.empty() ? name : aff.args[0].text;
    }
    const ActorStep* st = roffSchritt(name, z.anim.datei);
    if (st == nullptr && !z.blockName.empty()) { st = roffSchritt(z.blockName, z.anim.datei); }
    if (st != nullptr) {
        z.playMs = st->startMs;
        for (int a = 0; a < 3; ++a) {
            z.start[a] = st->from[a];
            z.startW[a] = st->fromAng[a];
        }
        return true;
    }
    // Noch nicht in der Szene (gerade angelegt): aus dem Block.
    if (!z.eigenerBlock) { return false; }
    z.playMs = z.blockWait + kRoffSpielBildMs;
    std::array<float, 3> o{};
    std::array<float, 3> w{};
    stellungBei(e, z.playMs, o, w);
    for (int a = 0; a < 3; ++a) {
        z.start[a] = o[static_cast<std::size_t>(a)];
        z.startW[a] = w[static_cast<std::size_t>(a)];
    }
    return true;
}

std::vector<AnimSchluessel> roffSchluesselAbsolut(const RoffZustand& z) {
    std::vector<AnimSchluessel> aus;
    const double t0 = z.playMs + kRoffSpielBildMs;
    for (const RoffSchluessel& k : z.anim.schluessel) {
        AnimSchluessel a;
        a.ms = t0 + k.ms;
        for (int c = 0; c < 3; ++c) {
            a.ort[static_cast<std::size_t>(c)] = z.start[c] + k.ort[c];
            a.winkel[static_cast<std::size_t>(c)] = z.startW[c] + k.winkel[c];
        }
        aus.push_back(a);
    }
    return aus;
}

std::string zahlen3(float a, float b, float c) {
    char t[96];
    std::snprintf(t, sizeof(t), "%g %g %g", static_cast<double>(a), static_cast<double>(b), static_cast<double>(c));
    return t;
}

// Ein Skript mit der geaenderten Marke (eigener Block oder vor dem play).
Script mitRoff(const Script& s, const RoffZustand& z, const std::string& entName, double playMs) {
    if (z.eigenerBlock) { return mitRoffBlockBei(s, z.blockIndex, entName, playMs, &z.anim); }
    Path play = z.marke;
    ++play.back();
    return mitRoffMarkeVorPlay(s, play, z.anim);
}

bool skriptErsetzen(const Script& neu, const char* was) {
    if (!g_app->doc.ersetzeSkript(neu, was)) { return false; }
    rebuildTree();
    g_app->camTrackValid = false;
    g_app->mapDirty = true;
    return true;
}

// Aus absoluten Schluesseln (Fenster, Rauten) die Marke neu schreiben.
bool roffSchreiben(std::vector<AnimSchluessel> k) {
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) { return false; }
    const std::string name = animNameVon(*e);
    RoffZustand z;
    if (!roffZustand(*e, z)) { return false; }
    if (k.empty()) {
        if (!z.eigenerBlock) { return false; }
        if (!skriptErsetzen(mitRoffBlockBei(g_app->doc.script(), z.blockIndex, name, 0.0, nullptr), "ROFF")) { return false; }
        g_app->animRoff.clear();
        return true;
    }
    std::stable_sort(k.begin(), k.end(), [](const AnimSchluessel& a, const AnimSchluessel& b) { return a.ms < b.ms; });
    // Der eigene Block beginnt mit dem ersten Schluessel; ein fremdes play
    // bleibt, wo es steht.
    const double t0Alt = z.playMs + kRoffSpielBildMs;
    const double play = z.eigenerBlock ? std::max(0.0, z.blockWait + (k.front().ms - t0Alt)) : z.blockWait;
    const double t0 = z.eigenerBlock ? t0Alt + (play - z.blockWait) : t0Alt;
    // Der erste Schluessel ist der Start - dort, wo die Entity beim play
    // steht. Wird er gezogen, zieht die Entity mit (ihr Ursprung), sonst
    // sprang die Bahn im ersten Bild dorthin.
    float d = 0.0F;
    for (int c = 0; c < 3; ++c) {
        d += std::fabs(k.front().ort[static_cast<std::size_t>(c)] - z.start[c]) +
             std::fabs(k.front().winkel[static_cast<std::size_t>(c)] - z.startW[c]);
    }
    if (d > 0.01F) {
        const int ei = g_app->animEntity;
        if (z.eigenerBlock && e->find("origin") != nullptr) {
            float o[3] = {0.0F, 0.0F, 0.0F};
            (void)entityOrt(*e, o);
            const auto& ko = k.front().ort;
            const auto& kw = k.front().winkel;
            (void)entityKeySetzen(ei, "origin",
                                  zahlen3(o[0] + ko[0] - z.start[0], o[1] + ko[1] - z.start[1], o[2] + ko[2] - z.start[2]));
            if (e->find("angles") != nullptr || std::fabs(kw[0]) > 0.01F || std::fabs(kw[2]) > 0.01F) {
                (void)entityKeySetzen(ei, "angles", zahlen3(kw[0], kw[1], kw[2]));
            } else {
                char t[32];
                std::snprintf(t, sizeof(t), "%g", static_cast<double>(kw[1]));
                (void)entityKeySetzen(ei, "angle", t);
            }
            for (int c = 0; c < 3; ++c) {
                z.start[c] = ko[static_cast<std::size_t>(c)];
                z.startW[c] = kw[static_cast<std::size_t>(c)];
            }
        } else {
            for (int c = 0; c < 3; ++c) {
                k.front().ort[static_cast<std::size_t>(c)] = z.start[c];
                k.front().winkel[static_cast<std::size_t>(c)] = z.startW[c];
            }
        }
    }
    z.anim.schluessel.clear();
    for (const AnimSchluessel& a : k) {
        RoffSchluessel r;
        r.ms = std::max(0.0, a.ms - t0);
        for (int c = 0; c < 3; ++c) {
            r.ort[c] = a.ort[static_cast<std::size_t>(c)] - z.start[c];
            r.winkel[c] = a.winkel[static_cast<std::size_t>(c)] - z.startW[c];
        }
        z.anim.schluessel.push_back(r);
    }
    return skriptErsetzen(mitRoff(g_app->doc.script(), z, name, play), "ROFF");
}

std::vector<AnimSchluessel> schluesselJetzt(const std::string& name) {
    if (roffModus()) {
        const MapEntity* e = animEntityPtr();
        RoffZustand z;
        return (e != nullptr && roffZustand(*e, z)) ? roffSchluesselAbsolut(z) : std::vector<AnimSchluessel>{};
    }
    const auto at = findeAnimBlock(g_app->doc.script(), name);
    return at ? leseAnimBlock(g_app->doc.script(), *at) : std::vector<AnimSchluessel>{};
}

// Der Skriptordner der Mission bzw. des offenen Skripts ("md_am_sith"),
// fuer den Vorschlag scripts/<ordner>/roffs/<name>.
std::string skriptOrdner() {
    if (g_app->missionAktiv && !g_app->mission.ordner.empty()) { return g_app->mission.ordner; }
    std::string p = g_app->skriptPfad.empty() ? g_app->path : g_app->skriptPfad;
    for (char& c : p) { c = (c == '\\') ? '/' : c; }
    std::string l = p;
    for (char& c : l) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    std::size_t at = l.rfind("/scripts/");
    if (at != std::string::npos) {
        at += 9;
    } else if (l.rfind("scripts/", 0) == 0) {
        at = 8;
    } else if (!g_app->skriptPfad.empty()) {
        at = 0;
    } else {
        return {};
    }
    const std::size_t ende = p.find('/', at);
    return ende == std::string::npos ? std::string{} : p.substr(at, ende - at);
}

// Ein Name, den weder ein Archiv noch eine andere Marke schon traegt.
std::string freierRoffName(const std::string& wunsch) {
    std::string n = wunsch;
    for (int i = 2; i < 1000; ++i) {
        std::string daten;
        bool belegt = readFromArchives(roffPfad(n), daten) || findeRoffMarke(g_app->doc.script(), n).has_value();
        for (std::size_t t = 0; !belegt && t < g_app->tabs.size(); ++t) {
            if (static_cast<int>(t) == g_app->activeTab) { continue; }
            belegt = findeRoffMarke(g_app->tabs[t].doc.script(), n).has_value();
        }
        if (!belegt) { return n; }
        n = wunsch + "_" + std::to_string(i);
    }
    return n;
}

bool gueltigerRoffName(const std::string& n) {
    if (n.empty() || n.front() == '/' || n.find("..") != std::string::npos) { return false; }
    for (char c : n) {
        const bool ok = (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_' || c == '-' || c == '/';
        if (!ok) { return false; }
    }
    return true;
}

// Aus dem move-Block eine neue Bahn (oder eine leere am Zeiger) - und zurueck.
bool zuRoffWechseln() {
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) { return false; }
    const std::string name = animNameVon(*e);
    if (name.empty()) { return false; }
    std::vector<AnimSchluessel> k;
    if (const auto at = findeAnimBlock(g_app->doc.script(), name)) { k = leseAnimBlock(g_app->doc.script(), *at); }
    if (k.empty()) {
        AnimSchluessel a;
        a.ms = std::max(kRoffSpielBildMs, g_app->playMs);
        stellungBei(*e, a.ms, a.ort, a.winkel);
        k.push_back(a);
    }
    std::stable_sort(k.begin(), k.end(), [](const AnimSchluessel& a, const AnimSchluessel& b) { return a.ms < b.ms; });
    const std::string ordner = skriptOrdner();
    RoffAnim anim;
    anim.datei = freierRoffName((ordner.empty() ? std::string("behaved") : ordner) + "/roffs/" + name);
    anim.weich = true;
    // Die Bahn beginnt, wo die Entity steht. Den Ort des ersten Schluessels
    // bekommt sie als Ursprung (der move-Block setzte sie dorthin).
    const int ei = g_app->animEntity;
    if (e->find("origin") != nullptr) {
        const auto& o = k.front().ort;
        (void)entityKeySetzen(ei, "origin", zahlen3(o[0], o[1], o[2]));
    }
    // Das play laeuft ein Spielbild nach dem wait, das erste Bild noch eines
    // spaeter - so faellt es auf die Zeit des ersten Schluessels.
    const double play = std::max(0.0, k.front().ms - 2.0 * kRoffSpielBildMs);
    for (const AnimSchluessel& a : k) {
        RoffSchluessel r;
        r.ms = a.ms - k.front().ms;
        for (int c = 0; c < 3; ++c) {
            r.ort[c] = a.ort[static_cast<std::size_t>(c)] - k.front().ort[static_cast<std::size_t>(c)];
            r.winkel[c] = a.winkel[static_cast<std::size_t>(c)] - k.front().winkel[static_cast<std::size_t>(c)];
        }
        anim.schluessel.push_back(r);
    }
    Script neu = mitAnimBlock(g_app->doc.script(), name, {});
    neu = mitRoffBlock(neu, name, play, &anim);
    if (!skriptErsetzen(neu, "ROFF")) { return false; }
    g_app->animRoff = anim.datei;
    g_app->animSchluessel = -1;
    return true;
}

bool zuMoveWechseln() {
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) { return false; }
    const std::string name = animNameVon(*e);
    RoffZustand z;
    if (!roffZustand(*e, z) || !z.eigenerBlock) { return false; }
    const std::vector<AnimSchluessel> k = roffSchluesselAbsolut(z);
    Script neu = mitRoffBlockBei(g_app->doc.script(), z.blockIndex, name, 0.0, nullptr);
    neu = mitAnimBlock(neu, name, k);
    if (!skriptErsetzen(neu, "animation")) { return false; }
    g_app->animRoff.clear();
    g_app->animSchluessel = -1;
    return true;
}

// Die Bahnen, die in der Szene auf dieser Entity laufen.
std::vector<const ActorStep*> roffSchritteVon(const std::string& entName) {
    std::vector<const ActorStep*> aus;
    for (const Actor& ac : g_app->scene.actors) {
        if (ac.name != entName) { continue; }
        for (const ActorStep& st : ac.steps) {
            if (st.kind != ActorStep::Kind::Roff) { continue; }
            bool schon = false;
            for (const ActorStep* x : aus) { schon = schon || gleicheRoff(x->text, st.text); }
            if (!schon) { aus.push_back(&st); }
        }
    }
    return aus;
}

}  // namespace

bool entityAnimierbar(const MapEntity& e) {
    const std::string& c = e.classname;
    return c.rfind("func_", 0) == 0 || c == "misc_model_breakable" || c == "fx_runner";
}

void animStarten(int entity) {
    if (entity < 0 || static_cast<std::size_t>(entity) >= g_app->map.entities.size()) { return; }
    MapEntity& e = g_app->map.entities[static_cast<std::size_t>(entity)];
    if (animNameVon(e).empty()) {
        // Ohne Namen kann kein Skript sie ansprechen.
        entityKeySetzen(entity, "script_targetname", freierEntityName("mover"));
    }
    g_app->animEntity = entity;
    g_app->animSchluessel = -1;
    g_app->animZug = false;
    g_app->animFensterOffen = true;
    // Hat sie schon eine eigene ROFF-Bahn? Dann gleich die.
    g_app->animRoff.clear();
    const Script& s = g_app->doc.script();
    if (const auto blk = findeRoffBlock(s, animNameVon(g_app->map.entities[static_cast<std::size_t>(entity)]))) {
        RoffAnim a;
        if (leseRoffMarke(s.nodes[*blk].raw, a)) { g_app->animRoff = a.datei; }
    }
}

bool animSchreiben(const std::vector<AnimSchluessel>& k) {
    if (roffModus()) { return roffSchreiben(k); }
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) { return false; }
    const std::string name = animNameVon(*e);
    if (name.empty()) { return false; }
    return skriptErsetzen(mitAnimBlock(g_app->doc.script(), name, k), "animation");
}

int animSchluesselAmZeiger() {
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) { return -1; }
    std::vector<AnimSchluessel> k = schluesselJetzt(animNameVon(*e));
    AnimSchluessel neu;
    neu.ms = std::max(0.0, g_app->playMs);
    stellungBei(*e, neu.ms, neu.ort, neu.winkel);
    // Schon ein Schluessel zu dieser Zeit? Dann ihn waehlen statt zu doppeln.
    for (std::size_t i = 0; i < k.size(); ++i) {
        if (std::fabs(k[i].ms - neu.ms) < 1.0) {
            g_app->animSchluessel = static_cast<int>(i);
            return static_cast<int>(i);
        }
    }
    k.push_back(neu);
    std::stable_sort(k.begin(), k.end(), [](const AnimSchluessel& a, const AnimSchluessel& b) { return a.ms < b.ms; });
    animSchreiben(k);
    for (std::size_t i = 0; i < k.size(); ++i) {
        if (std::fabs(k[i].ms - neu.ms) < 0.5) { g_app->animSchluessel = static_cast<int>(i); }
    }
    return g_app->animSchluessel;
}

// --- Gizmo am Zeiger (rc599) ------------------------------------------------------
//
// Zeigt das Fenster diese Entity und steht der Zeiger ab dem ersten
// Schluessel, setzt das Gizmo einen Schluessel (oder aendert den, der dort
// schon steht) - wie der Auto-Key der Kamera.

bool animKeyMoeglich(int ei, double ms) {
    if (!g_app->animFensterOffen || g_app->animEntity != ei || g_app->camViewActive) { return false; }
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) { return false; }
    const std::vector<AnimSchluessel> k = schluesselJetzt(animNameVon(*e));
    return !k.empty() && ms >= k.front().ms - 1.0;
}

bool animKeySetzen(double ms, const float ort[3], const float winkel[3]) {
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) { return false; }
    std::vector<AnimSchluessel> k = schluesselJetzt(animNameVon(*e));
    int nr = -1;
    for (std::size_t i = 0; i < k.size(); ++i) {
        if (std::fabs(k[i].ms - ms) < 1.0) { nr = static_cast<int>(i); }
    }
    if (nr < 0) {
        AnimSchluessel a;
        a.ms = ms;
        k.push_back(a);
        nr = static_cast<int>(k.size()) - 1;
    }
    for (int c = 0; c < 3; ++c) {
        k[static_cast<std::size_t>(nr)].ort[static_cast<std::size_t>(c)] = ort[c];
        k[static_cast<std::size_t>(nr)].winkel[static_cast<std::size_t>(c)] = winkel[c];
    }
    std::stable_sort(k.begin(), k.end(), [](const AnimSchluessel& a, const AnimSchluessel& b) { return a.ms < b.ms; });
    if (!animSchreiben(k)) { return false; }
    for (std::size_t i = 0; i < k.size(); ++i) {
        if (std::fabs(k[i].ms - ms) < 0.5) { g_app->animSchluessel = static_cast<int>(i); }
    }
    char t[96];
    std::snprintf(t, sizeof(t), tr(Str::AnimKeyGizmo), ms / 1000.0);
    addStatus(t);
    return true;
}

// --- ROFF: Marken der offenen Skripte, Speichern (rc599) -------------------------

std::shared_ptr<const Roff> roffAusMarken(const std::string& datei) {
    const auto suche = [&datei](const Script& s) -> std::shared_ptr<const Roff> {
        const auto f = findeRoffMarke(s, datei);
        if (!f) { return nullptr; }
        const std::string schluessel = roffMarke(f->anim);
        if (const auto it = g_app->roffAusMarke.find(schluessel); it != g_app->roffAusMarke.end()) { return it->second; }
        if (g_app->roffAusMarke.size() > 64U) { g_app->roffAusMarke.clear(); }
        auto r = std::make_shared<const Roff>(roffAusAnim(f->anim));
        g_app->roffAusMarke[schluessel] = r;
        return r;
    };
    if (auto r = suche(g_app->doc.script())) { return r; }
    for (std::size_t t = 0; t < g_app->tabs.size(); ++t) {
        if (static_cast<int>(t) == g_app->activeTab) { continue; }
        if (auto r = suche(g_app->tabs[t].doc.script())) { return r; }
    }
    return nullptr;
}

std::string roffZielOrdner(const std::string& skriptDatei) {
    // Liegt das Skript in einem scripts-Ordner, gehoert die Datei dorthin -
    // so findet das Spiel beide auf demselben Weg.
    std::string p = skriptDatei;
    for (char& c : p) { c = (c == '\\') ? '/' : c; }
    std::string l = p;
    for (char& c : l) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    const std::size_t at = l.rfind("/scripts/");
    if (at != std::string::npos) { return p.substr(0, at + 8); }
    if (g_app->missionAktiv && !g_app->mission.projektOrdner.empty()) {
        return g_app->mission.projektOrdner + "/scripts";
    }
    // Sonst in den Mod-Ordner (Movie Duels vor dem Grundspiel).
    std::string ordner;
    for (const GamePath& gp : g_app->gamePaths) {
        std::string d = gp.directory;
        for (char& c : d) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        const bool md = (d.size() >= 3 && (d.compare(d.size() - 3, 3, "\\md") == 0 || d.compare(d.size() - 3, 3, "/md") == 0)) ||
                        d.find("movie duels") != std::string::npos;
        if (ordner.empty() || md) { ordner = gp.directory; }
    }
    return ordner.empty() ? std::string{} : ordner + "/scripts";
}

int roffDateienSchreiben(const std::string& skriptDatei, const Script& s) {
    const std::vector<RoffMarkeFund> marken = alleRoffMarken(s);
    if (marken.empty()) { return 0; }
    const std::string wurzel = roffZielOrdner(skriptDatei);
    if (wurzel.empty()) {
        addStatus(tr(Str::AnimRoffNoFolder));
        return 0;
    }
    int geschrieben = 0;
    for (const RoffMarkeFund& m : marken) {
        if (!gueltigerRoffName(m.anim.datei)) { continue; }
        const std::string daten = schreibeRoff(roffAusAnim(m.anim));
        const std::filesystem::path ziel = std::filesystem::u8path(wurzel + "/" + m.anim.datei + ".rof");
        std::error_code ec;
        if (std::filesystem::exists(ziel, ec)) {
            std::ifstream f(ziel, std::ios::binary);
            const std::string alt((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (alt == daten) { continue; }
        }
        std::filesystem::create_directories(ziel.parent_path(), ec);
        std::ofstream f(ziel, std::ios::binary | std::ios::trunc);
        f.write(daten.data(), static_cast<std::streamsize>(daten.size()));
        const auto u = ziel.u8string();
        const std::string zielText(u.begin(), u.end());
        char z[700];
        if (!f) {
            std::snprintf(z, sizeof(z), tr(Str::AnimRoffWriteFailed), zielText.c_str());
            addStatus(z);
            continue;
        }
        std::snprintf(z, sizeof(z), tr(Str::AnimRoffSaved), zielText.c_str());
        addStatus(z);
        ++geschrieben;
    }
    return geschrieben;
}

bool roffBearbeiten(const std::string& datei) {
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) { return false; }
    const std::string name = animNameVon(*e);
    // Schon eine Marke? Dann nur umschalten.
    if (findeRoffMarke(g_app->doc.script(), datei)) {
        g_app->animRoff = datei;
        g_app->animSchluessel = -1;
        return true;
    }
    const ActorStep* st = roffSchritt(name, datei);
    const std::shared_ptr<const Roff> r = roffFuer(datei);
    if (st == nullptr || !r) { return false; }
    if (!st->fremd.empty() || st->path.empty()) {
        addStatus(tr(Str::AnimRoffOtherScript));
        return false;
    }
    RoffAnim a;
    a.datei = freierRoffName(datei + "_2");
    a.bildMs = r->bildMs;
    a.notizen = roffNotizen(*r);
    const RoffZerlegung zl = zerlegeRoff(*r, st->from, st->fromAng);
    a.weich = zl.weich;
    for (const RoffSchluessel& k : zl.schluessel) {
        RoffSchluessel rel = k;
        for (int c = 0; c < 3; ++c) {
            rel.ort[c] = k.ort[c] - st->from[c];
            rel.winkel[c] = k.winkel[c] - st->fromAng[c];
        }
        a.schluessel.push_back(rel);
    }
    const Script neu = mitRoffMarkeVorPlay(g_app->doc.script(), st->path, a);
    if (!findeRoffMarke(neu, a.datei) || !skriptErsetzen(neu, "ROFF")) { return false; }
    g_app->animRoff = a.datei;
    g_app->animSchluessel = -1;
    char z[600];
    std::snprintf(z, sizeof(z), tr(Str::AnimRoffCopied), datei.c_str(), a.datei.c_str(),
                  static_cast<int>(r->bilder.size()), static_cast<int>(a.schluessel.size()));
    addStatus(z);
    return true;
}

// --- In der Karte: Bahn und Rauten ---------------------------------------------

void animUeberlagerung() {
    if (!g_app->animFensterOffen || g_app->camViewActive) { return; }
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) { return; }
    const std::vector<AnimSchluessel> k = g_app->animZug ? g_app->animVorschau : schluesselJetzt(animNameVon(*e));
    if (k.empty()) { return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ImVec2{g_app->kartenAnsicht[0], g_app->kartenAnsicht[1]},
                     ImVec2{g_app->kartenAnsicht[2], g_app->kartenAnsicht[3]}, true);
    const float px = std::max(1.0F, ImGui::GetFontSize() / 13.0F);
    std::vector<ImVec2> s(k.size());
    std::vector<char> da(k.size(), 0);
    for (std::size_t i = 0; i < k.size(); ++i) {
        const float w[3] = {k[i].ort[0], k[i].ort[1], k[i].ort[2]};
        da[i] = kartenPunktAufSchirm(w, s[i]) ? 1 : 0;
    }
    if (roffModus()) {
        // Die Bahn so, wie die Datei sie faehrt: Bild fuer Bild (weich ein
        // Bogen, nicht die Geraden zwischen den Rauten).
        RoffZustand z;
        const bool weich = roffZustand(*e, z) ? z.anim.weich : true;
        std::vector<RoffSchluessel> rk;
        for (const AnimSchluessel& a : k) {
            RoffSchluessel r;
            r.ms = a.ms;
            for (int c = 0; c < 3; ++c) {
                r.ort[c] = a.ort[static_cast<std::size_t>(c)];
                r.winkel[c] = a.winkel[static_cast<std::size_t>(c)];
            }
            rk.push_back(r);
        }
        const Roff roff = baueRoff(rk, weich, z.anim.bildMs);
        const RoffBahn bahn = roffBahn(roff, rk.front().ort, rk.front().winkel);
        const std::size_t n = std::max<std::size_t>(1U, roff.bilder.size());
        ImVec2 vor{};
        bool vorDa = false;
        for (std::size_t j = 0; j < n; ++j) {
            ImVec2 q{};
            const bool d = kartenPunktAufSchirm(bahn.stand[j].ort, q);
            if (d && vorDa) { dl->AddLine(vor, q, IM_COL32(120, 210, 255, 230), 2.0F * px); }
            vor = q;
            vorDa = d;
        }
    } else {
        for (std::size_t i = 1; i < k.size(); ++i) {
            if (da[i] != 0 && da[i - 1] != 0) { dl->AddLine(s[i - 1], s[i], IM_COL32(255, 200, 80, 220), 2.0F * px); }
        }
    }
    for (std::size_t i = 0; i < k.size(); ++i) {
        if (da[i] == 0) { continue; }
        const bool gew = static_cast<int>(i) == g_app->animSchluessel;
        const float r = (gew ? 7.0F : 5.5F) * px;
        const ImVec2 c = s[i];
        dl->AddQuadFilled(ImVec2{c.x, c.y - r}, ImVec2{c.x + r, c.y}, ImVec2{c.x, c.y + r}, ImVec2{c.x - r, c.y},
                          gew ? IM_COL32(255, 165, 40, 255) : IM_COL32(255, 225, 120, 240));
        dl->AddQuad(ImVec2{c.x, c.y - r}, ImVec2{c.x + r, c.y}, ImVec2{c.x, c.y + r}, ImVec2{c.x - r, c.y},
                    IM_COL32(30, 25, 10, 220), 1.2F * px);
        char t[32];
        std::snprintf(t, sizeof(t), "%d  %.1fs", static_cast<int>(i) + 1, k[i].ms / 1000.0);
        dl->AddText(ImVec2{c.x + r + 3.0F, c.y - ImGui::GetFontSize() * 0.5F}, IM_COL32(255, 230, 160, 255), t);
    }
    dl->PopClipRect();
}

void animMaus() {
    if (!g_app->animFensterOffen || g_app->camViewActive) {
        // Geht das Fenster mitten im Zug zu (oder schaut die Ansicht durch
        // die Kamera), endet der Zug - sonst sah er das Loslassen nie und
        // griff spaeter mit alten Nummern zu (Code-Pruefung rc592).
        g_app->animZug = false;
        return;
    }
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) { return; }
    const ImGuiIO& io = ImGui::GetIO();
    const bool imBild = io.MousePos.x >= g_app->kartenAnsicht[0] && io.MousePos.x < g_app->kartenAnsicht[2] &&
                        io.MousePos.y >= g_app->kartenAnsicht[1] && io.MousePos.y < g_app->kartenAnsicht[3] &&
                        ImGui::IsWindowHovered();
    if (g_app->animZug) {
        // Ziehen: in der Bildebene der Kamera, Umschalt nur senkrecht.
        const Camera& c = g_app->gezeigteKamera;
        float r[3];
        float u[3];
        c.right(r);
        c.up(u);
        const float dx = io.MousePos.x - g_app->animZugMaus[0];
        const float dy = io.MousePos.y - g_app->animZugMaus[1];
        if (g_app->animSchluessel < 0 ||
            static_cast<std::size_t>(g_app->animSchluessel) >= g_app->animVorschau.size()) {
            g_app->animZug = false;
            return;
        }
        AnimSchluessel& k = g_app->animVorschau[static_cast<std::size_t>(g_app->animSchluessel)];
        for (int a = 0; a < 3; ++a) {
            k.ort[static_cast<std::size_t>(a)] = g_app->animZugStart[a];
            if (io.KeyShift) {
                if (a == 2) { k.ort[2] -= dy * g_app->animZugSkala; }
            } else {
                k.ort[static_cast<std::size_t>(a)] += (r[a] * dx - u[a] * dy) * g_app->animZugSkala;
            }
        }
        if (g_app->rasterSchritt > 0) {
            const float rs = static_cast<float>(g_app->rasterSchritt);
            for (float& v : k.ort) { v = std::round(v / rs) * rs; }
        }
        g_app->mapDirty = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            g_app->animZug = false;
            animSchreiben(g_app->animVorschau);
        }
        g_app->wegKlickVerbraucht = true;
        return;
    }
    if (!imBild || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { return; }
    const std::vector<AnimSchluessel> k = schluesselJetzt(animNameVon(*e));
    int best = -1;
    float bestD = ImGui::GetFontSize() * 1.0F;
    bestD *= bestD;
    for (std::size_t i = 0; i < k.size(); ++i) {
        const float w[3] = {k[i].ort[0], k[i].ort[1], k[i].ort[2]};
        ImVec2 sp;
        if (!kartenPunktAufSchirm(w, sp)) { continue; }
        const float d = (sp.x - io.MousePos.x) * (sp.x - io.MousePos.x) + (sp.y - io.MousePos.y) * (sp.y - io.MousePos.y);
        // Liegen Rauten uebereinander (ein Schluessel, der noch am selben Ort
        // steht), gewinnt die zeitlich naechste am Zeiger - die, die man
        // gerade bearbeitet. Vorher immer die erste (Animationstest).
        const bool gleichAuf = best >= 0 && std::fabs(d - bestD) <= 16.0F;
        const bool naeherInZeit =
            gleichAuf && std::fabs(k[i].ms - g_app->playMs) < std::fabs(k[static_cast<std::size_t>(best)].ms - g_app->playMs);
        if ((d < bestD && !gleichAuf) || naeherInZeit) {
            bestD = std::min(bestD, d);
            best = static_cast<int>(i);
        }
    }
    if (best < 0) { return; }
    g_app->animSchluessel = best;
    g_app->animVorschau = k;
    g_app->animZug = true;
    g_app->animZugMaus[0] = io.MousePos.x;
    g_app->animZugMaus[1] = io.MousePos.y;
    for (int a = 0; a < 3; ++a) { g_app->animZugStart[a] = k[static_cast<std::size_t>(best)].ort[static_cast<std::size_t>(a)]; }
    // Welteinheiten je Bildpunkt in der Tiefe des Schluessels
    const Camera& c = g_app->gezeigteKamera;
    float f[3];
    c.forward(f);
    const float tiefe = std::max(16.0F, (k[static_cast<std::size_t>(best)].ort[0] - c.pos[0]) * f[0] +
                                            (k[static_cast<std::size_t>(best)].ort[1] - c.pos[1]) * f[1] +
                                            (k[static_cast<std::size_t>(best)].ort[2] - c.pos[2]) * f[2]);
    const float bildH = std::max(1.0F, g_app->kartenAnsicht[3] - g_app->kartenAnsicht[1]);
    g_app->animZugSkala = 2.0F * tiefe * std::tan(c.fovY * 3.14159265F / 360.0F) / bildH;
    // Zur Zeit des Schluessels springen - so sieht man das Schiff dort.
    g_app->playMs = k[static_cast<std::size_t>(best)].ms;
    g_app->playing = false;
    g_app->wegKlickVerbraucht = true;
}

// --- Das Fenster -----------------------------------------------------------------

namespace {

// Die ROFF-Zeilen des Fensters: Speicherart, Dateiname, weich, fremde Bahnen.
void roffTeil(const MapEntity& e, float fs) {
    const std::string name = animNameVon(e);
    const bool roff = roffModus();
    if (iconKnopf(tr(Str::AnimModeMove), UiIcon::Skript, tr(Str::AnimModeHint), !roff, true, true) && roff) {
        if (!zuMoveWechseln()) { addStatus(tr(Str::AnimRoffNoMove)); }
    }
    ImGui::SameLine();
    if (iconKnopf(tr(Str::AnimModeRoff), UiIcon::Pfad, tr(Str::AnimModeHint), roff, true, true) && !roff) {
        (void)zuRoffWechseln();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", roff ? tr(Str::AnimModeRoff) : tr(Str::AnimModeMove));
    RoffZustand z;
    if (roff && roffZustand(e, z)) {
        static std::string feld;
        static std::string feldFuer;
        if (feldFuer != z.anim.datei && !ImGui::IsAnyItemActive()) {
            feld = z.anim.datei;
            feldFuer = z.anim.datei;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(Str::AnimRoffName));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 16.0F);
        char puffer[256];
        std::snprintf(puffer, sizeof(puffer), "%s", feld.c_str());
        if (ImGui::InputText("##roffname", puffer, sizeof(puffer))) { feld = puffer; }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::AnimRoffNameHint)); }
        if (ImGui::IsItemDeactivatedAfterEdit() && feld != z.anim.datei) {
            if (gueltigerRoffName(feld) && freierRoffName(feld) == feld) {
                RoffAnim neu = z.anim;
                neu.datei = feld;
                RoffZustand z2 = z;
                z2.anim = neu;
                if (skriptErsetzen(mitRoff(g_app->doc.script(), z2, name, z.blockWait), "ROFF")) {
                    g_app->animRoff = feld;
                    feldFuer = feld;
                }
            } else {
                addStatus(tr(Str::AnimRoffBadName));
                feld = z.anim.datei;
            }
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::AnimRoffSmooth), UiIcon::Pfad, tr(Str::AnimRoffSmoothHint), z.anim.weich, true, true)) {
            RoffZustand z2 = z;
            z2.anim.weich = !z.anim.weich;
            (void)skriptErsetzen(mitRoff(g_app->doc.script(), z2, name, z.blockWait), "ROFF");
        }
        const Roff r = roffAusAnim(z.anim);
        ImGui::TextDisabled(tr(Str::AnimRoffInfo), roffPfad(z.anim.datei).c_str(), static_cast<int>(r.bilder.size()),
                            roffLaufzeitMs(r) / 1000.0, static_cast<int>(z.anim.notizen.size()));
    } else if (roff) {
        ImGui::TextDisabled("%s", tr(Str::AnimRoffWaiting));
    }
    // Bahnen aus Dateien, die auf dieser Entity laufen (und noch keine
    // eigenen sind): zum Bearbeiten anbieten.
    bool kopf = false;
    for (const ActorStep* st : roffSchritteVon(name)) {
        if (gleicheRoff(st->text, g_app->animRoff)) { continue; }
        if (!kopf) {
            ImGui::TextDisabled("%s", tr(Str::AnimRoffPlays));
            kopf = true;
        }
        ImGui::PushID(st->text.c_str());
        const bool eigen = findeRoffMarke(g_app->doc.script(), st->text).has_value();
        if (ImGui::SmallButton(tr(Str::AnimRoffEdit))) {
            if (!roffBearbeiten(st->text)) { addStatus(tr(Str::AnimRoffOtherScript)); }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::AnimRoffEditHint)); }
        ImGui::SameLine();
        ImGui::Text("%s  %.1f s%s", st->text.c_str(), st->startMs / 1000.0, eigen ? "  *" : "");
        ImGui::PopID();
    }
}

}  // namespace

void drawAnimFenster() {
    if (!g_app->animFensterOffen) { return; }
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) {
        g_app->animFensterOffen = false;
        return;
    }
    // Der Modus folgt dem Skript, nicht einem gemerkten Zustand: nach Strg+Z
    // ist die Marke weg (zurueck zu move), nach Strg+Y wieder da (rc599 -
    // vorher blieb das Fenster ohne Block im ROFF-Modus haengen, und kein
    // Knopf tat mehr etwas).
    {
        const Script& s = g_app->doc.script();
        if (!g_app->animRoff.empty() && !findeRoffMarke(s, g_app->animRoff)) {
            g_app->animRoff.clear();
            g_app->animSchluessel = -1;
        }
        if (g_app->animRoff.empty()) {
            if (const auto blk = findeRoffBlock(s, animNameVon(*e))) {
                RoffAnim a;
                if (leseRoffMarke(s.nodes[*blk].raw, a)) { g_app->animRoff = a.datei; }
            }
        }
    }
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowSizeConstraints(ImVec2{fs * 28.0F, 0.0F}, ImVec2{fs * 40.0F, fs * 44.0F});
    if (!ImGui::Begin((std::string(tr(Str::AnimTitle)) + "###animation").c_str(), &g_app->animFensterOffen,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    const std::string name = animNameVon(*e);
    {
        const ImVec2 q = ImGui::GetCursorScreenPos();
        uiIcon(ImGui::GetWindowDrawList(), UiIcon::Animieren, q, fs, ImGui::GetColorU32(uiIconFarbe(UiIcon::Animieren)));
        ImGui::Dummy(ImVec2{fs, fs});
        ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x);
    }
    ImGui::TextColored(uiIconFarbe(UiIcon::Animieren), "%s", name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", e->classname.c_str());
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fs * 27.0F);
    if (roffModus()) {
        ImGui::TextDisabled("%s", tr(Str::AnimRoffHint));
    } else {
        ImGui::TextDisabled(tr(Str::AnimHint), g_app->shownName.empty() ? "unnamed.txt" : g_app->shownName.c_str());
    }
    if (e->classname.rfind("func_", 0) == 0 && e->find("origin") == nullptr) {
        ImGui::TextColored(ImVec4{1.0F, 0.7F, 0.35F, 1.0F}, "%s", tr(Str::AnimNoOrigin));
    }
    ImGui::PopTextWrapPos();
    roffTeil(*e, fs);
    std::vector<AnimSchluessel> k = schluesselJetzt(name);
    // Knoepfe
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{sp * 0.5F, ImGui::GetStyle().ItemSpacing.y});
    if (iconTextKnopf(tr(Str::AnimAddKey), UiIcon::Plus, tr(Str::AnimAddKeyHint))) { animSchluesselAmZeiger(); }
    ImGui::SameLine();
    const bool hat = g_app->animSchluessel >= 0 && static_cast<std::size_t>(g_app->animSchluessel) < k.size();
    // Im ROFF-Modus bleibt der erste Schluessel: er ist der Start der Bahn.
    const bool loeschbar = hat && !(roffModus() && k.size() > 1U && g_app->animSchluessel == 0);
    if (iconKnopf(tr(Str::AnimDeleteKey), UiIcon::Loeschen, nullptr, false, loeschbar)) {
        k.erase(k.begin() + g_app->animSchluessel);
        g_app->animSchluessel = -1;
        animSchreiben(k);
    }
    ImGui::SameLine();
    if (iconKnopf(tr(Str::AnimGotoKey), UiIcon::Hinfliegen, tr(Str::AnimGotoKeyHint), false, hat)) {
        g_app->playMs = k[static_cast<std::size_t>(g_app->animSchluessel)].ms;
        g_app->playing = false;
        g_app->mapDirty = true;
    }
    if (!roffModus()) {
        ImGui::SameLine(0.0F, sp * 1.5F);
        const std::string* lin = e->find("linear");
        const bool linear = lin != nullptr && *lin == "1";
        if (iconKnopf(tr(Str::AnimLinear), UiIcon::Pfad, tr(Str::AnimLinearHint), linear, true, true)) {
            entityKeySetzen(g_app->animEntity, "linear", linear ? "0" : "1");
            g_app->camTrackValid = false;
        }
    }
    ImGui::SameLine(0.0F, sp * 1.5F);
    RoffZustand rz;
    const bool entfernbar = !k.empty() && (!roffModus() || (roffZustand(*e, rz) && rz.eigenerBlock));
    if (iconKnopf(tr(Str::AnimRemove), UiIcon::Schliessen, tr(Str::AnimRemoveHint), false, entfernbar)) {
        animSchreiben({});
        g_app->animSchluessel = -1;
    }
    ImGui::PopStyleVar();
    // Die Schluessel als Tabelle
    if (k.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fs * 27.0F);
        ImGui::TextDisabled("%s", tr(Str::AnimNoKeys));
        ImGui::PopTextWrapPos();
    } else if (ImGui::BeginChild("##animschluesselrahmen", ImVec2{0.0F, std::min(fs * 22.0F, fs * 1.75F * static_cast<float>(k.size() + 2U))},
                                 ImGuiChildFlags_AutoResizeX) &&
               ImGui::BeginTable("##animschluessel", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                            ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn(tr(Str::AnimTime));
        ImGui::TableSetupColumn(tr(Str::CamPosition));
        ImGui::TableSetupColumn(tr(Str::CamAngles));
        ImGui::TableHeadersRow();
        static std::vector<AnimSchluessel> bearbeitet;
        static int bearbeitetZeile = -1;
        for (std::size_t i = 0; i < k.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char nr[8];
            std::snprintf(nr, sizeof(nr), "%d", static_cast<int>(i) + 1);
            if (ImGui::Selectable(nr, static_cast<int>(i) == g_app->animSchluessel, ImGuiSelectableFlags_SpanAllColumns |
                                                                                    ImGuiSelectableFlags_AllowOverlap)) {
                g_app->animSchluessel = static_cast<int>(i);
                g_app->playMs = k[i].ms;
                g_app->playing = false;
                g_app->mapDirty = true;
            }
            AnimSchluessel& z = (bearbeitetZeile == static_cast<int>(i) && bearbeitet.size() == k.size()) ? bearbeitet[i] : k[i];
            ImGui::TableNextColumn();
            float sek = static_cast<float>(z.ms / 1000.0);
            ImGui::SetNextItemWidth(fs * 4.5F);
            if (ImGui::DragFloat("##t", &sek, 0.02F, 0.0F, 600.0F, "%.2f s")) {
                if (bearbeitetZeile != static_cast<int>(i)) { bearbeitet = k; bearbeitetZeile = static_cast<int>(i); }
                bearbeitet[i].ms = static_cast<double>(std::max(0.0F, sek)) * 1000.0;
            }
            bool fertig = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(fs * 12.0F);
            float o[3] = {z.ort[0], z.ort[1], z.ort[2]};
            if (ImGui::DragFloat3("##o", o, 1.0F, 0.0F, 0.0F, "%.0f")) {
                if (bearbeitetZeile != static_cast<int>(i)) { bearbeitet = k; bearbeitetZeile = static_cast<int>(i); }
                bearbeitet[i].ort = {o[0], o[1], o[2]};
            }
            fertig = fertig || ImGui::IsItemDeactivatedAfterEdit();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(fs * 10.0F);
            float w[3] = {z.winkel[0], z.winkel[1], z.winkel[2]};
            if (ImGui::DragFloat3("##w", w, 0.5F, 0.0F, 0.0F, "%.0f")) {
                if (bearbeitetZeile != static_cast<int>(i)) { bearbeitet = k; bearbeitetZeile = static_cast<int>(i); }
                bearbeitet[i].winkel = {w[0], w[1], w[2]};
            }
            fertig = fertig || ImGui::IsItemDeactivatedAfterEdit();
            if (fertig && bearbeitetZeile == static_cast<int>(i)) {
                std::vector<AnimSchluessel> neu = bearbeitet;
                bearbeitetZeile = -1;
                animSchreiben(neu);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!k.empty()) {
        ImGui::EndChild();
        ImGui::TextDisabled("%s", tr(Str::AnimDragHint));
    }
    ImGui::End();
}

}  // namespace bhed::gui
