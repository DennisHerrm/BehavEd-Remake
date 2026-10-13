// app_mission.cpp - Missionen im Map-Tab bauen
//
// Gewuenscht (11.10.2026): "ich will halt das sich in maps alles intuitiv
// anfuehlt und modern und benutzer freundlich fuer missionen machen" - und
// "das man das spiel eigentlich nicht mehr braucht".
//
// Ausloeser war eine eigene Mission (md_tpa_jedi), die nur mit
// Hilfsskripten fertig wurde: Entities setzen und die .ent schreiben, die
// Spawnflags im Spielcode nachlesen, die Bodenhoehe aus der BSP rechnen,
// alles packen. Hier steht, was dafuer jetzt im Programm ist:
//
//   New mission...   Karte waehlen, Namen geben - Spielerstart, Scriptrunner
//                    und Intro-Skript entstehen, dazu ein Missionsordner.
//   Inspektor        die gewaehlte Entity mit Feldern, Spawnflags als
//                    Haekchen mit Namen (entdefs.h), "Auf den Boden".
//   Setzen/Ziehen    "+ Entity" setzt unter das Fadenkreuz, ein kurzer
//                    Rechtsklick an die Mausstelle; eine gewaehlte Entity
//                    laesst sich in der Ansicht ueber den Boden ziehen.
//   Save mission...  Skripte speichern und uebersetzen, .ent schreiben, .pk3
//                    packen (missionbau.h).
//
// Vorbilder: Hammers Objekteigenschaften (SmartEdit, Flags als Haekchen),
// Radiants Entity-Inspektor, Unreals "auf die Oberflaeche ziehen".
#include "app_internal.h"
#include "update.h"

#include "bhed/entdefs.h"
#include "bhed/missionbau.h"
#include "bhed/roffanim.h"
#include "bhed/scene.h"

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace bhed::gui {
namespace {

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

// UTF-8 aus einem Pfad (u8string ist seit C++20 char8_t).
std::string utf8(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

bool endetAuf(const std::string& s, const std::string& e) {
    return s.size() >= e.size() && klein(s.substr(s.size() - e.size())) == e;
}

// --- Text-Eingabe mit std::string, uebernommen erst beim Verlassen ---------
//
// Jede Taste als eigener Rueckgaengig-Schritt waere laestig; uebernommen wird,
// wenn das Feld den Fokus verliert oder Enter kommt - wie in Hammer.
int waechst(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* text = static_cast<std::string*>(d->UserData);
        text->resize(static_cast<std::size_t>(d->BufTextLen));
        d->Buf = text->data();
    }
    return 0;
}

bool textFeld(const char* id, const std::string& aktuell, std::string& neu, const char* hinweis = nullptr) {
    static std::unordered_map<ImGuiID, std::string> puffer;
    static ImGuiID aktiv = 0;
    const ImGuiID gid = ImGui::GetID(id);
    std::string& p = puffer[gid];
    if (aktiv != gid) {
        p = aktuell;
    }
    const ImGuiInputTextFlags fl = ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue;
    const bool enter = (hinweis != nullptr)
                           ? ImGui::InputTextWithHint(id, hinweis, p.data(), p.capacity() + 1, fl, waechst, &p)
                           : ImGui::InputText(id, p.data(), p.capacity() + 1, fl, waechst, &p);
    if (ImGui::IsItemActive()) {
        aktiv = gid;
    } else if (aktiv == gid) {
        aktiv = 0;
    }
    if ((ImGui::IsItemDeactivatedAfterEdit() || enter) && p != aktuell) {
        neu = p;
        return true;
    }
    return false;
}

// --- Strahlen in die Karte ---------------------------------------------------
bool spur(const float a[3], const float b[3], TraceTreffer& t) {
    if (g_app->geo.empty()) {
        return false;
    }
    t = traceRay(g_app->geo, a, b);
    return t.hit;
}

// Wie weit liegen die FUESSE (Unterkante des Kastens aus entdefs) ueber dem
// Boden? false, wenn darunter nichts ist.
bool bodenAbstand(const MapEntity& e, float& abstand, float* bodenZ = nullptr) {
    float o[3];
    if (!entityOrt(e, o)) {
        return false;
    }
    const ent::Def* d = ent::def(e.classname);
    const float unten = (d != nullptr && !d->brush) ? d->mins[2] : 0.0F;
    const float oben = (d != nullptr && !d->brush) ? d->maxs[2] : 8.0F;
    // Von der OBERKANTE nach unten: steckt die Entity im Boden, trifft der
    // Strahl dessen Oberseite - genau der Fall von Maul (11.10.).
    const float a[3] = {o[0], o[1], o[2] + std::max(oben, 1.0F)};
    const float b[3] = {o[0], o[1], o[2] - 8192.0F};
    TraceTreffer t;
    if (!spur(a, b, t)) {
        return false;
    }
    abstand = (o[2] + unten) - t.point[2];
    if (bodenZ != nullptr) { *bodenZ = t.point[2]; }
    return true;
}

// Wie hoch ueber einer getroffenen Flaeche der Ursprung liegen soll.
float hoeheUeberBoden(const std::string& klasse) {
    // Blick- und Kameramarken auf Augenhoehe, nicht an den Fuessen.
    if (klasse == "ref_tag" || klasse == "target_position" || klasse == "info_notnull") {
        return 56.0F;
    }
    const ent::Def* d = ent::def(klasse);
    if (d == nullptr || d->brush) {
        return 16.0F;
    }
    return -d->mins[2] + 1.0F;
}

std::string ortText(const float p[3]) {
    char b[96];
    std::snprintf(b, sizeof(b), "%d %d %d", static_cast<int>(std::lround(p[0])), static_cast<int>(std::lround(p[1])),
                  static_cast<int>(std::lround(p[2])));
    return b;
}

// Den Ursprung auf eine getroffene Flaeche setzen: auf einem Boden um die
// Kastenunterkante darueber, an einer Wand ein Stueck davor.
void aufFlaeche(const std::string& klasse, const TraceTreffer& t, float out[3]) {
    float boden[3] = {t.point[0], t.point[1], t.point[2]};
    if (t.normal[2] <= 0.7F) {
        // Eine Wand: ein Stueck davor (breiter als die halbe Figur), dann auf
        // den Boden darunter - sonst schwebte die Figur vor der Wand.
        float vor[3];
        for (int k = 0; k < 3; ++k) { vor[k] = t.point[k] + t.normal[k] * 28.0F; }
        const float unten[3] = {vor[0], vor[1], vor[2] - 8192.0F};
        TraceTreffer u;
        if (!spur(vor, unten, u) || u.normal[2] <= 0.7F) {
            for (int k = 0; k < 3; ++k) { out[k] = vor[k]; }
            return;
        }
        for (int k = 0; k < 3; ++k) { boden[k] = u.point[k]; }
    }
    out[0] = boden[0];
    out[1] = boden[1];
    out[2] = boden[2] + hoeheUeberBoden(klasse);
}

// Der Strahl durch einen Bildschirmpunkt der Kartenansicht (Umkehrung der
// Projektion in drawMapView: sx = halbB + rechts * f/z * halbH).
void strahlDurch(float sx, float sy, float start[3], float ende[3]) {
    const Camera& c = g_app->gezeigteKamera;
    float fwd[3];
    float rgt[3];
    float upv[3];
    c.forward(fwd);
    c.right(rgt);
    c.up(upv);
    const float x1 = g_app->kartenAnsicht[0];
    const float y1 = g_app->kartenAnsicht[1];
    const float halbB = (g_app->kartenAnsicht[2] - x1) * 0.5F;
    const float halbH = std::max((g_app->kartenAnsicht[3] - y1) * 0.5F, 1.0F);
    const float f = 1.0F / std::tan(c.fovY * 3.14159265F / 360.0F);
    const float a = (sx - x1 - halbB) / (f * halbH);
    const float b = (y1 + halbH - sy) / (f * halbH);
    for (int k = 0; k < 3; ++k) {
        start[k] = c.pos[k];
        ende[k] = c.pos[k] + (fwd[k] + a * rgt[k] + b * upv[k]) * 8192.0F;
    }
    // Draufsicht: was ueber der Schnitthoehe liegt, ist weggeschnitten - der
    // Strahl beginnt erst dort, sonst traefe ein Klick das unsichtbare Dach.
    if (g_app->draufsicht && start[2] > g_app->draufSchnitt && ende[2] < start[2]) {
        const float t = (start[2] - g_app->draufSchnitt) / (start[2] - ende[2]);
        for (int k = 0; k < 3; ++k) { start[k] += (ende[k] - start[k]) * t; }
    }
}

// --- Aendern mit Rueckgaengig -----------------------------------------------
void nachAenderung() {
    ++g_app->entStand;
    // Nummern, die nach dem Umbau ins Leere zeigen, vergessen.
    entityNummernPruefen();
    collectNames(g_app->map);
    merkeEntities();
    g_app->entitiesUngespeichert.insert(g_app->map.path);
    // Figuren stehen auf ihren Spawnern - die Szene neu aufbauen.
    g_app->camTrackValid = false;
    g_app->mapDirty = true;
}

void aenderung(std::vector<MapEntity> vorher) {
    if (g_app->entUndoKarte != g_app->map.path) {
        g_app->entUndo.clear();
        g_app->entRedo.clear();
        g_app->entUndoKarte = g_app->map.path;
    }
    g_app->entUndo.push_back(std::move(vorher));
    if (g_app->entUndo.size() > 200) {
        g_app->entUndo.erase(g_app->entUndo.begin());
    }
    g_app->entRedo.clear();
    g_app->kameraSchriebSkript = false;
    nachAenderung();
}

// Die Entity, die der Inspektor zeigt: die gewaehlte, sonst der Spawner der
// gewaehlten Figur.
int inspektorZiel() {
    const int n = static_cast<int>(g_app->map.entities.size());
    if (g_app->pickedEntity >= 0 && g_app->pickedEntity < n) {
        return g_app->pickedEntity;
    }
    if (g_app->selectedActor >= 0 && static_cast<std::size_t>(g_app->selectedActor) < g_app->scene.actors.size()) {
        const std::string& name = g_app->scene.actors[static_cast<std::size_t>(g_app->selectedActor)].name;
        for (int i = 0; i < n; ++i) {
            const MapEntity& e = g_app->map.entities[static_cast<std::size_t>(i)];
            if (e.classname.rfind("NPC_", 0) == 0 && klein(entitySkriptName(e)) == klein(name)) {
                return i;
            }
        }
    }
    return -1;
}

std::string freierName(const std::string& prefix) {
    std::set<std::string> vergeben;
    for (const MapEntity& e : g_app->map.entities) {
        vergeben.insert(klein(e.targetname));
        vergeben.insert(klein(entitySkriptName(e)));
    }
    for (int k = 1;; ++k) {
        const std::string n = prefix + std::to_string(k);
        if (vergeben.count(klein(n)) == 0) { return n; }
    }
}

bool istSkriptFeld(const std::string& k) {
    const std::string l = klein(k);
    return l.size() > 6 && l.compare(l.size() - 6, 6, "script") == 0;
}

// Je Klasse das eine Feld, ohne das sie nichts tut.
std::vector<const char*> wichtigeFelder(const std::string& c) {
    if (c.rfind("NPC_", 0) == 0 || c.rfind("npc_", 0) == 0) { return {"NPC_type"}; }
    if (c == "target_scriptrunner") { return {"usescript"}; }
    if (c == "target_level_change") { return {"mapname"}; }
    if (c == "fx_runner") { return {"fxFile"}; }
    if (c == "target_speaker") { return {"noise"}; }
    if (c == "target_play_music") { return {"music"}; }
    if (c.rfind("misc_model", 0) == 0) { return {"model"}; }
    return {};
}

// Ein Text der Entity-Beschreibungen in der eingestellten Sprache.
const char* entText(const char* const (&t)[ent::kSprachen]) {
    return ent::uebersetzt(t, static_cast<int>(i18n::currentLanguage()));
}

bool istNpcKlasse(const std::string& c) { return c.rfind("NPC_", 0) == 0 || c.rfind("npc_", 0) == 0; }

// Der ICARUS-Pfad eines Reiters ("md_x/intro"), ohne "scripts/" davor.
std::string skriptRef(const std::string& pfad, const std::string& skriptPfad) {
    std::string r;
    if (!skriptPfad.empty()) {
        r = skriptPfad;
        for (char& c : r) { if (c == '\\') { c = '/'; } }
        if (klein(r).rfind("scripts/", 0) == 0) { r = r.substr(8); }
        return r;
    }
    std::string p = pfad;
    for (char& c : p) { if (c == '\\') { c = '/'; } }
    const auto s = klein(p).rfind("/scripts/");
    if (s == std::string::npos) { return {}; }
    r = p.substr(s + 9);
    const auto punkt = r.find_last_of('.');
    if (punkt != std::string::npos && r.find('/', punkt) == std::string::npos) { r.resize(punkt); }
    return r;
}

}  // namespace

int moverUnterMaus(float sx, float sy) {
    float a[3];
    float b[3];
    strahlDurch(sx, sy, a, b);
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float bestT = 2.0F;
    int best = -1;
    for (const App::PickMover& pm : g_app->pickMover) {
        if (pm.mesh == nullptr || pm.entity < 0) { continue; }
        const float* m = pm.welt;
        const auto& v = pm.mesh->verts;
        const auto& ix = pm.mesh->indexes;
        auto welt = [&](std::uint32_t i, float o[3]) {
            const float* p = v[i].xyz;
            for (int r = 0; r < 3; ++r) { o[r] = m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3]; }
        };
        for (std::size_t t = 0; t + 2 < ix.size(); t += 3) {
            if (ix[t] >= v.size() || ix[t + 1] >= v.size() || ix[t + 2] >= v.size()) { continue; }
            float p0[3];
            float p1[3];
            float p2[3];
            welt(ix[t], p0);
            welt(ix[t + 1], p1);
            welt(ix[t + 2], p2);
            // Moeller-Trumbore, beidseitig.
            const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
            const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
            const float h[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
            const float det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
            if (std::fabs(det) < 1e-9F) { continue; }
            const float f = 1.0F / det;
            const float s[3] = {a[0] - p0[0], a[1] - p0[1], a[2] - p0[2]};
            const float u = f * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]);
            if (u < 0.0F || u > 1.0F) { continue; }
            const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
            const float w = f * (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]);
            if (w < 0.0F || u + w > 1.0F) { continue; }
            const float tt = f * (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]);
            if (tt > 0.0F && tt < bestT) {
                bestT = tt;
                best = pm.entity;
            }
        }
    }
    if (best < 0) { return -1; }
    // Eine Wand davor? Dann gilt der Klick ihr, nicht dem Modell dahinter.
    // Himmel und Unsichtbares verdecken nichts - steht die Kamera ausserhalb
    // der Himmelskiste (md_am_sith), traf der Strahl sonst zuerst den Himmel.
    float von[3] = {a[0], a[1], a[2]};
    float schon = 0.0F;
    for (int versuch = 0; versuch < 8; ++versuch) {
        TraceTreffer tr;
        if (!spur(von, b, tr)) { break; }
        const float anteil = schon + (1.0F - schon) * tr.fraction;
        if (anteil >= bestT - 0.002F) { break; }
        bool zaehlt = true;
        if (tr.surface >= 0 && static_cast<std::size_t>(tr.surface) < g_app->geo.surfaces.size()) {
            const int si = g_app->geo.surfaces[static_cast<std::size_t>(tr.surface)].shader;
            if (si >= 0 && static_cast<std::size_t>(si) < g_app->geo.shaders.size()) {
                const BspShader& sh = g_app->geo.shaders[static_cast<std::size_t>(si)];
                zaehlt = !sh.isSky() && sh.isDrawn();
            }
        }
        if (zaehlt) { return -1; }
        // Ein Stueck hinter der Flaeche weiter.
        schon = std::min(1.0F, anteil + 0.0005F);
        for (int k = 0; k < 3; ++k) { von[k] = a[k] + d[k] * schon; }
    }
    return best;
}

// Der Reiter mit dem Skript "md_x/intro" - aus einem Archiv ODER von der
// Platte (scripts/md_x/intro.txt im Missionsordner). -1 = keiner.
int reiterFuerSkriptRef(const std::string& ref) {
    for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
        const bool aktiv = static_cast<int>(i) == g_app->activeTab;
        const std::string r = aktiv ? skriptRef(g_app->path, g_app->skriptPfad)
                                    : skriptRef(g_app->tabs[i].path, g_app->tabs[i].skriptPfad);
        if (!r.empty() && klein(r) == klein(ref)) { return static_cast<int>(i); }
    }
    return -1;
}

namespace {
int reiterFuerRef(const std::string& ref) { return reiterFuerSkriptRef(ref); }

std::string pfadVerbinden(const std::string& a, const std::string& b) {
    if (a.empty()) { return b; }
    std::string r = a;
    if (r.back() != '\\' && r.back() != '/') { r += '\\'; }
    std::string bb = b;
    for (char& c : bb) { if (c == '/') { c = '\\'; } }
    return r + bb;
}

bool schreibe(const std::string& pfad, const std::string& inhalt) {
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::u8path(pfad);
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) { return false; }
    f.write(inhalt.data(), static_cast<std::streamsize>(inhalt.size()));
    return static_cast<bool>(f);
}

bool gibtEs(const std::string& pfad) {
    std::error_code ec;
    return !pfad.empty() && std::filesystem::exists(std::filesystem::u8path(pfad), ec);
}

}  // namespace

// Der ICARUS-Pfad des aktiven Reiters ("md_x/intro").
std::string aktiverSkriptRef() { return skriptRef(g_app->path, g_app->skriptPfad); }

// Der Text eines Skripts, das nicht offen ist: Missionsordner, Archive (.txt,
// sonst .IBI zurueckuebersetzt).
bool skriptTextLaden(const std::string& ref, std::string& text) {
    if (g_app->missionAktiv && !g_app->mission.projektOrdner.empty()) {
        const std::string datei = pfadVerbinden(g_app->mission.projektOrdner, "scripts/" + ref + ".txt");
        if (gibtEs(datei)) {
            text = slurp(datei);
            return true;
        }
    }
    if (readFromArchives("scripts/" + ref + ".txt", text)) {
        return true;
    }
    std::string ibi;
    if (readFromArchives("scripts/" + ref + ".IBI", ibi)) {
        std::vector<IbiBlock> bl;
        std::vector<Diag> d;
        Script sc;
        if (readIbi(ibi, bl, d) && decompile(bl, g_app->db, sc, d)) {
            text = writeScript(sc);
            return true;
        }
    }
    return false;
}

// Ein Skript zu einem Verweis oeffnen - offen, im Missionsordner, in den
// Archiven - oder neu anlegen.
void skriptOeffnen(const std::string& ref, bool anlegen) {
    if (const int r = reiterFuerRef(ref); r >= 0) {
        activateTab(r);
        return;
    }
    const std::string datei = g_app->missionAktiv && !g_app->mission.projektOrdner.empty()
                                  ? pfadVerbinden(g_app->mission.projektOrdner, "scripts/" + ref + ".txt")
                                  : std::string();
    if (gibtEs(datei)) {
        ladeSkriptDatei(datei);
        return;
    }
    std::string text;
    if (readFromArchives("scripts/" + ref + ".txt", text) || readFromArchives("scripts/" + ref + ".IBI", text)) {
        openScriptFromMemory(text, "scripts/" + ref + ".txt");
        return;
    }
    if (!anlegen) {
        return;
    }
    const std::string vorlage = "//Generated by BehavEd\n\nrem ( \"" + ref + "\" );\n";
    if (!datei.empty() && schreibe(datei, vorlage)) {
        ladeSkriptDatei(datei);
    } else {
        openScriptFromMemory(vorlage, "scripts/" + ref + ".txt");
    }
}

namespace {

// --- NPC-Typen fuer die Auswahl ------------------------------------------------
const std::vector<std::string>& npcTypen() {
    static std::vector<std::string> typen;
    static std::size_t stand = static_cast<std::size_t>(-1);
    if (!g_app->npcMap.empty() && stand != g_app->npcMap.size()) {
        typen.clear();
        for (const auto& [t, d] : g_app->npcMap) { typen.push_back(t); }
        stand = g_app->npcMap.size();
    } else if (g_app->npcMap.empty() && typen.empty()) {
        // Noch keine Szene gebaut - die .npc-Dateien selbst lesen.
        NpcMap m;
        for (const GamePath& gp : g_app->gamePaths) {
            for (const FoundFile& f : findByExtension(gp, {".npc"})) {
                std::string text;
                if (readFromArchives(f.name, text)) { parseNpcFile(text, m); }
            }
        }
        for (const auto& [t, d] : m) { typen.push_back(t); }
    }
    return typen;
}

// --- Was man neu setzen kann ---------------------------------------------------
struct Art {
    Str titel;
    const char* klasse;
    const char* prefix;   // fuer den Namen, leer = keiner
};
const Art kArten[] = {
    {Str::EntKindNpc, "NPC_spawner", "npc"},
    {Str::EntKindNav, "waypoint_navgoal", "goal"},
    {Str::EntKindTag, "ref_tag", "tag"},
    {Str::EntKindPos, "target_position", "look"},
    {Str::EntKindRunner, "target_scriptrunner", "runner"},
    {Str::EntKindFx, "fx_runner", "fx"},
    {Str::EntKindSpeaker, "target_speaker", "sound"},
    {Str::EntKindMusic, "target_play_music", "music"},
    {Str::EntKindLevel, "target_level_change", "end_level"},
    {Str::EntKindStart, "info_player_start", ""},
};

}  // namespace

// Eine neue Entity an `ort`, mit Namen und den Feldern, ohne die sie nichts
// tut. Gibt den Index zurueck.
int neueEntityAn(const std::string& klasse, const float ort[3]) {
    std::vector<MapEntity> vorher = g_app->map.entities;
    MapEntity e;
    setKey(e, "classname", klasse);
    setKey(e, "origin", ortText(ort));
    // Zur Kamera blickend - so sieht man die Figur von vorn.
    const float dx = g_app->gezeigteKamera.pos[0] - ort[0];
    const float dy = g_app->gezeigteKamera.pos[1] - ort[1];
    if (klasse != "ref_tag" && klasse != "target_position") {
        const int gier = static_cast<int>(std::lround(std::atan2(dy, dx) * 180.0F / 3.14159265F));
        setKey(e, "angle", std::to_string((gier + 360) % 360));
    }
    std::string prefix;
    for (const Art& a : kArten) {
        if (klasse == a.klasse) { prefix = a.prefix; }
    }
    if (prefix.empty() && klasse != "info_player_start") { prefix = "ent"; }
    if (!prefix.empty()) {
        const std::string name = freierName(prefix);
        if (istNpcKlasse(klasse)) {
            setKey(e, "NPC_targetname", name);
        } else {
            setKey(e, "targetname", name);
        }
    }
    if (klasse == "target_scriptrunner") {
        const std::string ordner = g_app->missionAktiv ? g_app->mission.ordner : std::string("my_mission");
        setKey(e, "usescript", ordner + "/" + entitySkriptName(e));
    }
    if (klasse == "ref_tag") {
        // Blickrichtung der Kamera - "Kamera auf diese Marke" uebernimmt sie.
        char w[64];
        std::snprintf(w, sizeof(w), "%d %d 0", static_cast<int>(g_app->gezeigteKamera.angles[0]),
                      static_cast<int>(g_app->gezeigteKamera.angles[1]));
        setKey(e, "angles", w);
    }
    g_app->map.entities.push_back(e);
    aenderung(std::move(vorher));
    g_app->pickedEntity = static_cast<int>(g_app->map.entities.size()) - 1;
    g_app->selectedActor = -1;
    g_app->cameraSelected = false;
    // Neue Klasse? Dann nicht ausgeblendet lassen.
    g_app->hiddenClasses.erase(klasse);
    return g_app->pickedEntity;
}

// Unter dem Fadenkreuz (Mitte der Ansicht) oder an einem Bildschirmpunkt.
int neueEntityHier(const std::string& klasse, bool amMauspunkt) {
    float a[3];
    float b[3];
    if (amMauspunkt && g_app->rechtsklickBild[0] >= 0.0F) {
        strahlDurch(g_app->rechtsklickBild[0], g_app->rechtsklickBild[1], a, b);
    } else {
        strahlDurch((g_app->kartenAnsicht[0] + g_app->kartenAnsicht[2]) * 0.5F,
                    (g_app->kartenAnsicht[1] + g_app->kartenAnsicht[3]) * 0.5F, a, b);
    }
    float ort[3];
    TraceTreffer t;
    if (spur(a, b, t)) {
        aufFlaeche(klasse, t, ort);
    } else {
        // Nichts getroffen (Himmel, keine Karte): 128 vor der Kamera.
        for (int k = 0; k < 3; ++k) { ort[k] = a[k] + (b[k] - a[k]) * (128.0F / 8192.0F); }
    }
    return neueEntityAn(klasse, ort);
}

void entityNeuMenue(bool amMauspunkt) {
    for (const Art& a : kArten) {
        if (ImGui::MenuItem(tr(a.titel))) {
            neueEntityHier(a.klasse, amMauspunkt);
        }
    }
    if (ImGui::BeginMenu(tr(Str::EntAllClasses))) {
        static char filter[48] = "";
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0F);
        ImGui::InputTextWithHint("##entklassen", tr(Str::EntitySearch), filter, sizeof(filter));
        const std::string f = klein(filter);
        ImGui::BeginChild("##klassenliste", ImVec2{ImGui::GetFontSize() * 16.0F, ImGui::GetFontSize() * 18.0F});
        for (std::size_t i = 0; i < ent::anzahl(); ++i) {
            const ent::Def& d = ent::at(i);
            if (d.brush) { continue; }   // braucht ein Brush-Modell der Karte
            if (!f.empty() && klein(d.klasse).find(f) == std::string::npos) { continue; }
            if (ImGui::Selectable(d.klasse)) {
                neueEntityHier(d.klasse, amMauspunkt);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && entText(d.text)[0] != '\0') {
                ImGui::SetTooltip("%s", entText(d.text));
            }
        }
        ImGui::EndChild();
        ImGui::EndMenu();
    }
}

bool entityLoeschen(int i) {
    if (i < 0 || static_cast<std::size_t>(i) >= g_app->map.entities.size()) { return false; }
    std::vector<MapEntity> vorher = g_app->map.entities;
    g_app->map.entities.erase(g_app->map.entities.begin() + i);
    g_app->hiddenEntities.clear();   // Indizes verschoben
    g_app->pickedEntity = -1;
    g_app->selectedActor = -1;
    entityNummernNachLoeschen(i);
    aenderung(std::move(vorher));
    return true;
}

void entityNummernVergessen() {
    g_app->entAuswahl.clear();
    g_app->animEntity = -1;
    g_app->animFensterOffen = false;
    g_app->animZug = false;
    g_app->animSchluessel = -1;
    g_app->wegVon = -1;
    g_app->wegLetzter = -1;
}

void entityNummernNachLoeschen(int i) {
    // Eine Nummer hinter der geloeschten rueckt eins vor; die geloeschte
    // selbst ist weg.
    const auto nach = [i](int& n) {
        if (n == i) {
            n = -1;
        } else if (n > i) {
            --n;
        }
    };
    std::vector<int> neu;
    for (int a : g_app->entAuswahl) {
        nach(a);
        if (a >= 0) { neu.push_back(a); }
    }
    g_app->entAuswahl = std::move(neu);
    const int anim = g_app->animEntity;
    nach(g_app->animEntity);
    if (anim >= 0 && g_app->animEntity < 0) {
        g_app->animFensterOffen = false;
        g_app->animZug = false;
        g_app->animSchluessel = -1;
    }
    nach(g_app->wegVon);
    nach(g_app->wegLetzter);
}

void entityNummernPruefen() {
    const int n = static_cast<int>(g_app->map.entities.size());
    std::vector<int> neu;
    for (int a : g_app->entAuswahl) {
        if (a >= 0 && a < n) { neu.push_back(a); }
    }
    g_app->entAuswahl = std::move(neu);
    if (g_app->animEntity >= n) {
        g_app->animEntity = -1;
        g_app->animFensterOffen = false;
        g_app->animZug = false;
        g_app->animSchluessel = -1;
    }
    if (g_app->wegVon >= n) { g_app->wegVon = -1; }
    if (g_app->wegLetzter >= n) { g_app->wegLetzter = -1; }
}

int entityDuplizieren(int i) {
    if (i < 0 || static_cast<std::size_t>(i) >= g_app->map.entities.size()) { return -1; }
    MapEntity e = g_app->map.entities[static_cast<std::size_t>(i)];
    float o[3];
    if (entityOrt(e, o)) {
        o[0] += 32.0F;
        o[1] += 32.0F;
    } else {
        o[0] = o[1] = o[2] = 0.0F;
    }
    std::vector<MapEntity> vorher = g_app->map.entities;
    setKey(e, "origin", ortText(o));
    // Der Name muss eindeutig bleiben, sonst sprechen Skripte beide an.
    const char* nameKey = istNpcKlasse(e.classname) ? "NPC_targetname" : "targetname";
    if (const std::string* n = e.find(nameKey); n != nullptr && !n->empty()) {
        std::string stamm = *n;
        while (!stamm.empty() && std::isdigit(static_cast<unsigned char>(stamm.back())) != 0) { stamm.pop_back(); }
        setKey(e, nameKey, freierName(stamm.empty() ? std::string("ent") : stamm));
    }
    g_app->map.entities.push_back(e);
    aenderung(std::move(vorher));
    g_app->pickedEntity = static_cast<int>(g_app->map.entities.size()) - 1;
    return g_app->pickedEntity;
}

// --- Mehrfachauswahl (rc590) ----------------------------------------------------

std::vector<int> entityAuswahl() {
    std::vector<int> aus;
    const int n = static_cast<int>(g_app->map.entities.size());
    if (g_app->pickedEntity >= 0 && g_app->pickedEntity < n) { aus.push_back(g_app->pickedEntity); }
    for (const int i : g_app->entAuswahl) {
        if (i >= 0 && i < n && std::find(aus.begin(), aus.end(), i) == aus.end()) { aus.push_back(i); }
    }
    return aus;
}

bool entityIstGewaehlt(int i) {
    // Dasselbe wie die Suche in entityAuswahl(), ohne die Liste je Aufruf neu
    // zu bauen - das lief je Markierung und Listenzeile in jedem Bild,
    // quadratisch in der Auswahl (Code-Pruefung rc592).
    if (i < 0 || i >= static_cast<int>(g_app->map.entities.size())) { return false; }
    if (i == g_app->pickedEntity) { return true; }
    return std::find(g_app->entAuswahl.begin(), g_app->entAuswahl.end(), i) != g_app->entAuswahl.end();
}

void entityAuswahlSetzen(int i) {
    g_app->entAuswahl.clear();
    g_app->pickedEntity = i;
    g_app->mapDirty = true;
}

void entityAuswahlUmschalten(int i) {
    if (i < 0) { return; }
    std::vector<int> a = entityAuswahl();
    const auto it = std::find(a.begin(), a.end(), i);
    if (it != a.end()) {
        a.erase(it);
    } else {
        a.push_back(i);
    }
    g_app->pickedEntity = a.empty() ? -1 : a.front();
    g_app->entAuswahl.assign(a.empty() ? a.end() : a.begin() + 1, a.end());
    g_app->selectedActor = -1;
    g_app->cameraSelected = false;
    g_app->mapDirty = true;
}

bool entityAuswahlLoeschen() {
    std::vector<int> a = entityAuswahl();
    if (a.empty()) { return false; }
    if (a.size() == 1U) { return entityLoeschen(a.front()); }
    std::vector<MapEntity> vorher = g_app->map.entities;
    std::sort(a.begin(), a.end(), std::greater<int>());
    for (const int i : a) { g_app->map.entities.erase(g_app->map.entities.begin() + i); }
    g_app->hiddenEntities.clear();
    g_app->pickedEntity = -1;
    g_app->entAuswahl.clear();
    g_app->selectedActor = -1;
    aenderung(std::move(vorher));
    return true;
}

namespace {

// Eine Kopie mit freiem Namen, ohne eigenen Rueckgaengig-Schritt.
int kopieAnhaengen(int i, float dx, float dy) {
    MapEntity e = g_app->map.entities[static_cast<std::size_t>(i)];
    float o[3];
    if (entityOrt(e, o)) {
        o[0] += dx;
        o[1] += dy;
        setKey(e, "origin", ortText(o));
    }
    const char* nameKey = istNpcKlasse(e.classname) ? "NPC_targetname" : "targetname";
    if (const std::string* n = e.find(nameKey); n != nullptr && !n->empty()) {
        std::string stamm = *n;
        while (!stamm.empty() && std::isdigit(static_cast<unsigned char>(stamm.back())) != 0) { stamm.pop_back(); }
        setKey(e, nameKey, freierName(stamm.empty() ? std::string("ent") : stamm));
    }
    g_app->map.entities.push_back(e);
    return static_cast<int>(g_app->map.entities.size()) - 1;
}

}  // namespace

int entityAuswahlDuplizieren() {
    const std::vector<int> a = entityAuswahl();
    if (a.empty()) { return -1; }
    if (a.size() == 1U) { return entityDuplizieren(a.front()); }
    std::vector<MapEntity> vorher = g_app->map.entities;
    std::vector<int> neu;
    for (const int i : a) { neu.push_back(kopieAnhaengen(i, 32.0F, 32.0F)); }
    g_app->pickedEntity = neu.front();
    g_app->entAuswahl.assign(neu.begin() + 1, neu.end());
    aenderung(std::move(vorher));
    return neu.front();
}

bool entityAufBoden(int i) {
    if (i < 0 || static_cast<std::size_t>(i) >= g_app->map.entities.size()) { return false; }
    MapEntity& e = g_app->map.entities[static_cast<std::size_t>(i)];
    float abstand = 0.0F;
    float o[3];
    if (!entityOrt(e, o) || !bodenAbstand(e, abstand)) { return false; }
    if (std::fabs(abstand - 1.0F) < 0.5F) { return true; }   // steht schon
    std::vector<MapEntity> vorher = g_app->map.entities;
    o[2] -= abstand - 1.0F;   // eine Einheit Luft, wie die Engine (DROPTOFLOOR)
    setKey(e, "origin", ortText(o));
    aenderung(std::move(vorher));
    return true;
}

bool entityKeySetzen(int i, const std::string& key, const std::string& wert) {
    if (i < 0 || static_cast<std::size_t>(i) >= g_app->map.entities.size()) { return false; }
    std::vector<MapEntity> vorher = g_app->map.entities;
    setKey(g_app->map.entities[static_cast<std::size_t>(i)], key, wert);
    if (vorher[static_cast<std::size_t>(i)].keys == g_app->map.entities[static_cast<std::size_t>(i)].keys) {
        return false;
    }
    aenderung(std::move(vorher));
    return true;
}

bool entityRueckgaengig() {
    if (g_app->entUndoKarte != g_app->map.path || g_app->entUndo.empty()) { return false; }
    g_app->entRedo.push_back(g_app->map.entities);
    g_app->map.entities = std::move(g_app->entUndo.back());
    g_app->entUndo.pop_back();
    g_app->hiddenEntities.clear();
    g_app->entAuswahl.clear();
    if (g_app->pickedEntity >= static_cast<int>(g_app->map.entities.size())) { g_app->pickedEntity = -1; }
    entityNummernPruefen();
    nachAenderung();
    return true;
}

bool entityWiederholen() {
    if (g_app->entUndoKarte != g_app->map.path || g_app->entRedo.empty()) { return false; }
    g_app->entUndo.push_back(g_app->map.entities);
    g_app->map.entities = std::move(g_app->entRedo.back());
    g_app->entRedo.pop_back();
    g_app->hiddenEntities.clear();
    g_app->entAuswahl.clear();
    if (g_app->pickedEntity >= static_cast<int>(g_app->map.entities.size())) { g_app->pickedEntity = -1; }
    entityNummernPruefen();
    nachAenderung();
    return true;
}

bool missionUngespeichert() {
    if (!g_app->missionAktiv) {
        return false;
    }
    if (g_app->entitiesUngespeichert.count(g_app->mission.karte) != 0U) {
        return true;
    }
    const std::string ordner = klein(g_app->mission.ordner) + "/";
    for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
        const bool aktiv = static_cast<int>(i) == g_app->activeTab;
        const bool schmutzig = aktiv ? g_app->doc.dirty() : g_app->tabs[i].doc.dirty();
        if (!schmutzig) {
            continue;
        }
        const std::string r = aktiv ? skriptRef(g_app->path, g_app->skriptPfad)
                                    : skriptRef(g_app->tabs[i].path, g_app->tabs[i].skriptPfad);
        if (klein(r).rfind(ordner, 0) == 0) {
            return true;
        }
    }
    return false;
}

bool entityFokus() {
    if (g_app->cameraSelected && g_app->kameraSchriebSkript) { return false; }
    return g_app->leftMode == 1 && (g_app->entFokusBild || g_app->entFokusListe) && !ImGui::GetIO().WantTextInput;
}

bool entityUndoMoeglich() { return g_app->entUndoKarte == g_app->map.path && !g_app->entUndo.empty(); }
bool entityRedoMoeglich() { return g_app->entUndoKarte == g_app->map.path && !g_app->entRedo.empty(); }

// --- Ansicht: Hervorheben, Ziehen, Gizmo (wie in Unreal) ---------------------
//
// shank (11.10.): "auch feature das du wenn du ueber was drueber hoverst in
// der karte es umrandet wird usw. kannst dir ja inspiration von unreal engine
// holen". Und "Verschieben geht nur ueber den Boden" -> Gizmo.
//
//   ueberfahren   heller Kasten in der Groesse der Klasse, mit Namen
//   gewaehlt      oranger Kasten, Pfeil der Blickrichtung, Gizmo:
//                 rote/gruene/blaue Achse verschieben entlang X/Y/Z,
//                 gelber Ring dreht (angle)
//   ziehen        die gewaehlte Entity anfassen und ueber den Boden ziehen
//
// Gezeichnet wird ueber dem Bild mit ImGui (nicht in den Tiefenpuffer der
// Karte): wie Unreals Auswahlumriss bleibt es auch hinter Waenden sichtbar.
namespace {

struct Proj {
    float fwd[3];
    float rgt[3];
    float upv[3];
    float pos[3];
    float f = 1.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    float halbB = 1.0F;
    float halbH = 1.0F;
};

Proj projektion() {
    Proj p;
    const Camera& c = g_app->gezeigteKamera;
    c.forward(p.fwd);
    c.right(p.rgt);
    c.up(p.upv);
    for (int k = 0; k < 3; ++k) { p.pos[k] = c.pos[k]; }
    p.f = 1.0F / std::tan(c.fovY * 3.14159265F / 360.0F);
    p.x1 = g_app->kartenAnsicht[0];
    p.y1 = g_app->kartenAnsicht[1];
    p.halbB = (g_app->kartenAnsicht[2] - p.x1) * 0.5F;
    p.halbH = std::max((g_app->kartenAnsicht[3] - p.y1) * 0.5F, 1.0F);
    return p;
}

// Weltpunkt -> Bildschirm. false hinter der Kamera.
bool aufSchirm(const Proj& p, const float w[3], ImVec2& out, float* tiefe = nullptr) {
    const float d[3] = {w[0] - p.pos[0], w[1] - p.pos[1], w[2] - p.pos[2]};
    const float z = d[0] * p.fwd[0] + d[1] * p.fwd[1] + d[2] * p.fwd[2];
    if (z < 1.0F) { return false; }
    const float x = d[0] * p.rgt[0] + d[1] * p.rgt[1] + d[2] * p.rgt[2];
    const float y = d[0] * p.upv[0] + d[1] * p.upv[1] + d[2] * p.upv[2];
    out = ImVec2{p.x1 + p.halbB + x * p.f / z * p.halbH, p.y1 + p.halbH - y * p.f / z * p.halbH};
    if (tiefe != nullptr) { *tiefe = z; }
    return true;
}

// Abstand eines Punkts zu einer Strecke (Bildschirm).
float abstandZuStrecke(ImVec2 q, ImVec2 a, ImVec2 b) {
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float l2 = vx * vx + vy * vy;
    float t = l2 > 0.0F ? ((q.x - a.x) * vx + (q.y - a.y) * vy) / l2 : 0.0F;
    t = std::clamp(t, 0.0F, 1.0F);
    const float dx = a.x + vx * t - q.x;
    const float dy = a.y + vy * t - q.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Kasten der Klasse (mins/maxs), sonst 16er Wuerfel.
void kasten(const MapEntity& e, float mins[3], float maxs[3]) {
    const ent::Def* d = ent::def(e.classname);
    for (int k = 0; k < 3; ++k) {
        mins[k] = (d != nullptr && !d->brush) ? d->mins[k] : -8.0F;
        maxs[k] = (d != nullptr && !d->brush) ? d->maxs[k] : 8.0F;
    }
}

float winkelVon(const MapEntity& e) {
    const std::string* a = e.find("angle");
    if (a != nullptr) { return std::strtof(a->c_str(), nullptr); }
    // Ohne "angle": die Gier aus "angles" (Schiffe, Modelle, rc599).
    if (const std::string* w = e.find("angles")) {
        float p = 0.0F;
        float y = 0.0F;
        float r = 0.0F;
        if (std::sscanf(w->c_str(), "%f %f %f", &p, &y, &r) >= 2) { return y; }
    }
    return 0.0F;
}

// --- Drehen um alle drei Achsen (rc599) -------------------------------------
//
// shank: "ich kann die Sachen wie Charaktere, Schiffe und Kameras nicht mehr
// drehen, weil kein Dreh-Gizmo mehr da ist ... bei Move ein Move-Target und
// bei Drehen ein Rotation-Target". Im Drehmodus drei Ringe wie in Unreal:
// blau um die Hochachse (Gier), gruen um die Querachse (Nicken), rot um die
// Laengsachse (Rollen). Nicken und Rollen nur, wo die Engine sie liest
// ("angles": Brush-Modelle, Kartenmodelle) - eine Figur hat nur ihre Gier.
void winkel3Von(const MapEntity& e, float w[3]) {
    w[0] = 0.0F;
    w[1] = winkelVon(e);
    w[2] = 0.0F;
    if (const std::string* a = e.find("angles")) {
        float p = 0.0F;
        float y = w[1];
        float r = 0.0F;
        if (std::sscanf(a->c_str(), "%f %f %f", &p, &y, &r) == 3) {
            w[0] = p;
            w[1] = y;
            w[2] = r;
        }
    }
}

bool kannKippen(const MapEntity& e) {
    if (istNpcKlasse(e.classname) || e.classname == "info_player_start") { return false; }
    const std::string* mdl = e.find("model");
    return e.find("angles") != nullptr || (mdl != nullptr && !mdl->empty()) || e.classname.rfind("misc_model", 0) == 0;
}

// Die Winkel schreiben: "angles", wenn die Entity sie hat oder sie gekippt
// ist, sonst wie bisher "angle".
void setzeWinkel3(MapEntity& e, const float w[3]) {
    const auto norm = [](float v) { return static_cast<int>(std::lround(std::fmod(std::fmod(v, 360.0F) + 360.0F, 360.0F))) % 360; };
    const bool gekippt = std::fabs(w[0]) > 0.01F || std::fabs(w[2]) > 0.01F;
    if (e.find("angles") != nullptr || gekippt) {
        char t[64];
        std::snprintf(t, sizeof(t), "%d %d %d", norm(w[0]), norm(w[1]), norm(w[2]));
        setKey(e, "angles", t);
    }
    if (e.find("angle") != nullptr || !(e.find("angles") != nullptr || gekippt)) {
        setKey(e, "angle", std::to_string(norm(w[1])));
    }
}

// Ein Punkt auf dem Ring einer Achse (0 Gier, 1 Nicken, 2 Rollen) um o.
void ringPunkt(const float o[3], int achse, float gierGrad, float radius, float a, float out[3]) {
    const float g = gierGrad * 3.14159265F / 180.0F;
    const float vor[3] = {std::cos(g), std::sin(g), 0.0F};
    const float rechts[3] = {std::sin(g), -std::cos(g), 0.0F};
    const float hoch[3] = {0.0F, 0.0F, 1.0F};
    const float xw[3] = {1.0F, 0.0F, 0.0F};
    const float yw[3] = {0.0F, 1.0F, 0.0F};
    const float* u = achse == 0 ? xw : (achse == 1 ? vor : rechts);
    const float* v = achse == 0 ? yw : hoch;
    for (int k = 0; k < 3; ++k) { out[k] = o[k] + (u[k] * std::cos(a) + v[k] * std::sin(a)) * radius; }
}

// Die Drehachse eines Rings in Weltrichtung (fuer den Drehsinn auf dem Schirm).
void ringAchse(int achse, float gierGrad, float out[3]) {
    const float g = gierGrad * 3.14159265F / 180.0F;
    if (achse == 0) {
        out[0] = 0.0F;
        out[1] = 0.0F;
        out[2] = 1.0F;
    } else if (achse == 1) {   // Nicken: um die Querachse (rechts)
        out[0] = std::sin(g);
        out[1] = -std::cos(g);
        out[2] = 0.0F;
    } else {                   // Rollen: um die Laengsachse (vorn)
        out[0] = std::cos(g);
        out[1] = std::sin(g);
        out[2] = 0.0F;
    }
}

bool hatRichtung(const MapEntity& e) {
    return e.find("angle") != nullptr || istNpcKlasse(e.classname) || e.classname == "info_player_start";
}

// Weltlaenge, die auf dem Schirm etwa `px` Bildpunkte ergibt (Gizmo-Groesse).
float laengeFuerPx(const Proj& p, const float o[3], float px) {
    const float d[3] = {o[0] - p.pos[0], o[1] - p.pos[1], o[2] - p.pos[2]};
    const float z = std::max(d[0] * p.fwd[0] + d[1] * p.fwd[1] + d[2] * p.fwd[2], 1.0F);
    return px * z / (p.f * p.halbH);
}

// Welcher Griff des Gizmos liegt unter der Maus? 0 keiner.
int gizmoGriff(const Proj& p, const float o[3], float L, ImVec2 m, bool drehen = false, bool kippen = false,
               float gierGrad = 0.0F) {
    ImVec2 so;
    if (!aufSchirm(p, o, so)) { return 0; }
    const float reich = ImGui::GetFontSize() * 0.45F;
    if (drehen) {
        // Die Ringe: 4 Gier, 5 Nicken, 6 Rollen. Der naechste gewinnt.
        int best = 0;
        float bestD = reich;
        for (int achse = 0; achse < (kippen ? 3 : 1); ++achse) {
            ImVec2 vorher;
            bool hat = false;
            for (int k = 0; k <= 48; ++k) {
                float w[3];
                ringPunkt(o, achse, gierGrad, L * 0.8F, static_cast<float>(k) / 48.0F * 6.2831853F, w);
                ImVec2 s;
                if (!aufSchirm(p, w, s)) { hat = false; continue; }
                if (hat) {
                    const float d = abstandZuStrecke(m, vorher, s);
                    if (d < bestD) {
                        bestD = d;
                        best = 4 + achse;
                    }
                }
                vorher = s;
                hat = true;
            }
        }
        return best;
    }
    for (int k = 0; k < 3; ++k) {
        float e[3] = {o[0], o[1], o[2]};
        e[k] += L;
        ImVec2 se;
        // Erst ab einem Viertel der Laenge: die Mitte gehoert der Entity
        // selbst (anfassen und ueber den Boden ziehen).
        if (aufSchirm(p, e, se) &&
            abstandZuStrecke(m, ImVec2{so.x + (se.x - so.x) * 0.25F, so.y + (se.y - so.y) * 0.25F}, se) < reich) {
            return k + 1;
        }
    }
    return 0;
}

constexpr float kGizmoPx = 70.0F;

}  // namespace

bool zugKippen(int ei, float& dNicken, float& dRollen) {
    const App::EntZug& z = g_app->entZug;
    dNicken = 0.0F;
    dRollen = 0.0F;
    if (!z.aktiv || ei < 0 || z.index != ei || static_cast<std::size_t>(ei) >= g_app->map.entities.size()) { return false; }
    float w[3];
    winkel3Von(g_app->map.entities[static_cast<std::size_t>(ei)], w);
    dNicken = w[0] - z.startWinkel3[0];
    dRollen = w[2] - z.startWinkel3[2];
    return true;
}

void entityGizmoLeiste() {
    const int ziel = inspektorZiel();
    if (ziel < 0 || g_app->cameraSelected || g_app->camViewActive || !g_app->showEntities) { return; }
    const float fs = ImGui::GetFontSize();
    const ImVec2 merk = ImGui::GetCursorScreenPos();
    // Unter dem Schild mit dem Reiternamen.
    ImGui::SetCursorScreenPos(ImVec2{g_app->kartenAnsicht[0] + fs * 0.6F, g_app->kartenAnsicht[1] + fs * 2.6F});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.10F, 0.11F, 0.14F, 0.88F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{fs * 0.3F, fs * 0.25F});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{fs * 0.2F, fs * 0.2F});
    if (ImGui::BeginChild("##entgizmoleiste", ImVec2{0.0F, 0.0F},
                          ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders |
                              ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar)) {
        if (iconKnopf(tr(Str::GizmoMove), UiIcon::Verschieben, tr(Str::EntGizmoAxis),
                      g_app->gizmoMode != App::GizmoMode::Rotate, true, true)) {
            g_app->gizmoMode = App::GizmoMode::Move;
            g_app->mapDirty = true;
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::GizmoRotate), UiIcon::Drehen, tr(Str::EntGizmoRing),
                      g_app->gizmoMode == App::GizmoMode::Rotate, true, true)) {
            g_app->gizmoMode = App::GizmoMode::Rotate;
            g_app->mapDirty = true;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(merk);
    ImGui::Dummy(ImVec2{0.0F, 0.0F});
}

bool zugVersatz(int ei, float d[3], float& dGier) {
    const App::EntZug& z = g_app->entZug;
    d[0] = d[1] = d[2] = 0.0F;
    dGier = 0.0F;
    if (!z.aktiv || ei < 0 || z.index != ei || static_cast<std::size_t>(ei) >= g_app->map.entities.size()) { return false; }
    const MapEntity& e = g_app->map.entities[static_cast<std::size_t>(ei)];
    float o[3];
    if (!entityOrt(e, o)) { return false; }
    for (int k = 0; k < 3; ++k) { d[k] = o[k] - z.startOrt[k]; }
    dGier = winkelVon(e) - z.startWinkel;
    return true;
}

void entityZiehen(float rasterSkal) {
    App::EntZug& z = g_app->entZug;
    const ImGuiIO& io = ImGui::GetIO();
    const int ziel = inspektorZiel();
    const bool imBild = io.MousePos.x >= g_app->kartenAnsicht[0] && io.MousePos.x < g_app->kartenAnsicht[2] &&
                        io.MousePos.y >= g_app->kartenAnsicht[1] && io.MousePos.y < g_app->kartenAnsicht[3];
    g_app->entFokusBild = imBild && ImGui::IsWindowHovered();
    const Proj p = projektion();

    // --- Was liegt unter der Maus? (fuer die Hervorhebung) -------------------
    g_app->entHover = -1;
    g_app->entGizmoHover = 0;
    float zielOrt[3] = {0.0F, 0.0F, 0.0F};
    // Gezeigt (und angefasst) wird, wo die Entity gerade steht (rc597).
    const bool zielHatOrt = ziel >= 0 && entityOrtAnzeige(g_app->map.entities[static_cast<std::size_t>(ziel)], zielOrt);
    float zielVersatz[3] = {0.0F, 0.0F, 0.0F};
    if (ziel >= 0) { entityVersatz(ziel, zielVersatz); }
    const bool gizmoDa = zielHatOrt && g_app->showEntities && !g_app->camViewActive;
    const float L = gizmoDa ? laengeFuerPx(p, zielOrt, kGizmoPx) : 0.0F;
    if (g_app->entFokusBild && !z.kandidat && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (gizmoDa) {
            const MapEntity& ze = g_app->map.entities[static_cast<std::size_t>(ziel)];
            g_app->entGizmoHover = gizmoGriff(p, zielOrt, L, io.MousePos, g_app->gizmoMode == App::GizmoMode::Rotate,
                                              kannKippen(ze), entityGierAnzeige(ze));
        }
        if (g_app->entGizmoHover == 0) {
            const float mx = (io.MousePos.x - g_app->kartenAnsicht[0]) * rasterSkal;
            const float my = (io.MousePos.y - g_app->kartenAnsicht[1]) * rasterSkal;
            const float reich = ImGui::GetFontSize() * 1.2F;
            float best = reich * reich;
            for (const App::PickedMarker& mk : g_app->markers) {
                const float d = (mk.sx - mx) * (mk.sx - mx) + (mk.sy - my) * (mk.sy - my);
                if (d < best) {
                    best = d;
                    g_app->entHover = static_cast<int>(mk.entity);
                }
            }
            // Eine Figur: ihr Spawner.
            if (g_app->entHover < 0) {
                const float r2 = reich * 1.6F;
                float bestF = r2 * r2;
                int figur = -1;
                for (const App::ActorMark& am : g_app->actorMarks) {
                    const float d = (am.sx - mx) * (am.sx - mx) + (am.sy - my) * (am.sy - my);
                    if (d < bestF) {
                        bestF = d;
                        figur = am.index;
                    }
                }
                if (figur >= 0 && static_cast<std::size_t>(figur) < g_app->scene.actors.size()) {
                    const std::string& name = g_app->scene.actors[static_cast<std::size_t>(figur)].name;
                    for (std::size_t i = 0; i < g_app->map.entities.size(); ++i) {
                        const MapEntity& e = g_app->map.entities[i];
                        if (istNpcKlasse(e.classname) && klein(entitySkriptName(e)) == klein(name)) {
                            g_app->entHover = static_cast<int>(i);
                            break;
                        }
                    }
                }
            }
        }
    }

    // --- Anfassen --------------------------------------------------------------
    if (!z.kandidat && ziel >= 0 && g_app->entFokusBild && g_app->gizmoAxis < 0 && !g_app->wegKlickVerbraucht &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !g_app->camViewActive) {
        int modus = -1;
        if (g_app->entGizmoHover != 0) {
            modus = g_app->entGizmoHover;
        } else {
            const float mx = (io.MousePos.x - g_app->kartenAnsicht[0]) * rasterSkal;
            const float my = (io.MousePos.y - g_app->kartenAnsicht[1]) * rasterSkal;
            const float reich = ImGui::GetFontSize() * 1.2F;
            bool getroffen = false;
            for (const App::PickedMarker& mk : g_app->markers) {
                if (static_cast<int>(mk.entity) == ziel) {
                    getroffen = getroffen || ((mk.sx - mx) * (mk.sx - mx) + (mk.sy - my) * (mk.sy - my) < reich * reich);
                }
            }
            if (g_app->selectedActor >= 0) {
                for (const App::ActorMark& am : g_app->actorMarks) {
                    if (am.index == g_app->selectedActor) {
                        const float r2 = reich * 1.4F;
                        getroffen = getroffen || ((am.sx - mx) * (am.sx - mx) + (am.sy - my) * (am.sy - my) < r2 * r2);
                    }
                }
            }
            if (getroffen) { modus = 0; }
        }
        if (modus >= 0) {
            z.kandidat = true;
            z.aktiv = false;
            z.index = ziel;
            z.modus = modus;
            z.vorher = g_app->map.entities;
            // Alt+Ziehen kopiert erst (wie in Unreal) - im selben Schritt.
            if (io.KeyAlt && modus <= 3) {
                std::vector<int> neu;
                for (const int i : entityAuswahl()) { neu.push_back(kopieAnhaengen(i, 0.0F, 0.0F)); }
                if (!neu.empty()) {
                    g_app->pickedEntity = neu.front();
                    g_app->entAuswahl.assign(neu.begin() + 1, neu.end());
                    z.index = neu.front();
                }
            }
            z.gruppe.clear();
            z.gruppeWinkel.clear();
            for (const int i : entityAuswahl()) {
                float o[3];
                if (i == z.index || !entityOrt(g_app->map.entities[static_cast<std::size_t>(i)], o)) { continue; }
                z.gruppe.emplace_back(i, std::array<float, 3>{o[0], o[1], o[2]});
                z.gruppeWinkel.push_back(winkelVon(g_app->map.entities[static_cast<std::size_t>(i)]));
            }
            for (int k = 0; k < 3; ++k) {
                z.versatz[k] = zielVersatz[k];
                z.startOrt[k] = zielOrt[k] - zielVersatz[k];
            }
            z.gierVersatz = entityGierVersatz(ziel);
            z.startWinkel = winkelVon(g_app->map.entities[static_cast<std::size_t>(ziel)]);
            winkel3Von(g_app->map.entities[static_cast<std::size_t>(ziel)], z.startWinkel3);
            z.startMaus[0] = io.MousePos.x;
            z.startMaus[1] = io.MousePos.y;
            z.achsLaenge = L;
        }
    }
    // --- Rahmen ziehen (rc590) ----------------------------------------------------
    //
    // Ins Leere gedrueckt und gezogen: alle Entities im Rahmen. Mit Strg oder
    // Umschalt kommen sie zur Auswahl hinzu.
    if (!z.kandidat && !g_app->boxKandidat && !g_app->wegKlickVerbraucht && g_app->entFokusBild &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        g_app->entHover < 0 && g_app->entGizmoHover == 0 && !g_app->kameraHover && g_app->gizmoAxis < 0 &&
        g_app->wegWerkzeug == 0 && g_app->platzierKlasse.empty() && !g_app->camViewActive && g_app->showEntities &&
        !g_app->cameraSelected &&
        !(g_app->gizmoShown && g_app->gizmoHover >= 0)) {
        g_app->boxKandidat = true;
        g_app->boxAktiv = false;
        g_app->boxZusatz = io.KeyCtrl || io.KeyShift;
        g_app->boxStart[0] = io.MousePos.x;
        g_app->boxStart[1] = io.MousePos.y;
    }
    if (g_app->boxKandidat) {
        if (!g_app->boxAktiv && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0F)) { g_app->boxAktiv = true; }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (g_app->boxAktiv) {
                const float x0 = std::min(g_app->boxStart[0], io.MousePos.x);
                const float x1 = std::max(g_app->boxStart[0], io.MousePos.x);
                const float y0 = std::min(g_app->boxStart[1], io.MousePos.y);
                const float y1 = std::max(g_app->boxStart[1], io.MousePos.y);
                std::vector<int> neu = g_app->boxZusatz ? entityAuswahl() : std::vector<int>{};
                for (const App::PickedMarker& mk : g_app->markers) {
                    const float sx = g_app->kartenAnsicht[0] + mk.sx / std::max(rasterSkal, 0.001F);
                    const float sy = g_app->kartenAnsicht[1] + mk.sy / std::max(rasterSkal, 0.001F);
                    const int i = static_cast<int>(mk.entity);
                    if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1 && std::find(neu.begin(), neu.end(), i) == neu.end()) {
                        neu.push_back(i);
                    }
                }
                g_app->pickedEntity = neu.empty() ? -1 : neu.front();
                g_app->entAuswahl.assign(neu.empty() ? neu.end() : neu.begin() + 1, neu.end());
                g_app->selectedActor = -1;
                g_app->cameraSelected = false;
                g_app->mapDirty = true;
                if (neu.size() > 1U) {
                    char t[96];
                    std::snprintf(t, sizeof(t), tr(Str::EntSelectedN), static_cast<int>(neu.size()));
                    addStatus(t);
                }
            }
            g_app->boxKandidat = false;
            g_app->boxAktiv = false;
        }
    }
    if (!z.kandidat) {
        return;
    }
    if (z.index < 0 || static_cast<std::size_t>(z.index) >= g_app->map.entities.size()) {
        z = App::EntZug{};
        return;
    }
    if (!z.aktiv && ImGui::IsMouseDragging(ImGuiMouseButton_Left, z.modus == 0 ? -1.0F : 1.0F)) {
        z.aktiv = true;
        // rc599: ein Schluessel zur eingestellten Zeit - zuerst der des
        // Animationsfensters, dann (Drehen) Schiff im move oder Figur.
        if (z.gruppe.empty()) {
            int art = 0;
            if (animKeyMoeglich(z.index, g_app->playMs)) {
                art = 1;
            } else if (z.modus >= 4) {
                int fig = -1;
                Path pf;
                bool lauf = false;
                if (bahnPunktMoeglich(z.index, g_app->playMs, fig, pf, lauf) && !lauf) {
                    art = 2;
                    z.zwPfad = pf;
                } else if (fig = figurFuerEntity(z.index); fig >= 0 && istNpcKlasse(g_app->map.entities[static_cast<std::size_t>(z.index)].classname)) {
                    for (const ActorStep& st : g_app->scene.actors[static_cast<std::size_t>(fig)].steps) {
                        if (st.startMs > g_app->playMs) { break; }
                        if (!st.path.empty() && st.fremd.empty()) { art = 3; }
                    }
                }
            }
            const int figur = art != 0 ? figurFuerEntity(z.index) : -1;
            if (figur >= 0) {
                z.dkArt = art;
                z.zwFigur = figur;
                z.zwT = g_app->playMs;
                z.zwSteps = g_app->scene.actors[static_cast<std::size_t>(figur)].steps;
                const ActorState st = g_app->scene.actors[static_cast<std::size_t>(figur)].at(z.zwT);
                for (int k = 0; k < 3; ++k) {
                    z.dkOrt[k] = st.pos[k];
                    z.dkBasisOrt[k] = st.pos[k];
                    z.dkBasis[k] = st.angles[k];
                    z.dkWinkel[k] = st.angles[k];
                }
            }
        }
        // Mitten in einer Bewegung: ein neuer Bahnpunkt statt den Ursprung zu
        // verschieben (rc597, "wenn ich das Schiff mittendrin bewege, macht
        // er keinen neuen Punkt, sondern aendert immer den Anfangspunkt").
        if (z.modus <= 3 && z.gruppe.empty() && z.dkArt == 0 &&
            bahnPunktMoeglich(z.index, g_app->playMs, z.zwFigur, z.zwPfad, z.zwLauf)) {
            z.zwischen = true;
            z.zwT = g_app->playMs;
            z.zwSteps = g_app->scene.actors[static_cast<std::size_t>(z.zwFigur)].steps;
            for (int k = 0; k < 3; ++k) { z.zwOrt[k] = z.startOrt[k] + z.versatz[k]; }
        }
    }
    MapEntity& e = g_app->map.entities[static_cast<std::size_t>(z.index)];
    // Der neue Bahnpunkt folgt der Maus: die Bahn der Figur im Speicher teilen.
    const auto zwischenVorschau = [&](const float ort[3]) {
        for (int k = 0; k < 3; ++k) { z.zwOrt[k] = ort[k]; }
        z.zwBewegt = true;
        Actor& a = g_app->scene.actors[static_cast<std::size_t>(z.zwFigur)];
        a.steps = z.zwSteps;
        for (std::size_t i = 0; i < a.steps.size(); ++i) {
            ActorStep& st = a.steps[i];
            if (st.path != z.zwPfad || (st.kind != ActorStep::Kind::WalkTo && st.kind != ActorStep::Kind::Glide)) { continue; }
            ActorStep zweiter = st;
            for (int k = 0; k < 3; ++k) {
                st.to[k] = ort[k];
                zweiter.from[k] = ort[k];
            }
            st.endMs = z.zwT;
            zweiter.startMs = z.zwT;
            a.steps.insert(a.steps.begin() + static_cast<std::ptrdiff_t>(i) + 1, zweiter);
            break;
        }
        g_app->mapDirty = true;
    };
    // Der Schluessel zur Zeit (rc599): Ort und Winkel als zwei Schritte in
    // die Bahn dieser Figur im Speicher - geschrieben wird beim Loslassen.
    const auto dkVorschau = [&]() {
        z.dkBewegt = true;
        Actor& a = g_app->scene.actors[static_cast<std::size_t>(z.zwFigur)];
        a.steps = z.zwSteps;
        std::size_t at = 0;
        while (at < a.steps.size() && a.steps[at].startMs <= z.zwT) { ++at; }
        ActorStep tp;
        tp.kind = ActorStep::Kind::Teleport;
        tp.startMs = z.zwT;
        tp.endMs = z.zwT;
        ActorStep fc = tp;
        fc.kind = ActorStep::Kind::Face;
        for (int k = 0; k < 3; ++k) {
            tp.to[k] = z.dkOrt[k];
            fc.to[k] = z.dkWinkel[k];
        }
        a.steps.insert(a.steps.begin() + static_cast<std::ptrdiff_t>(at), {tp, fc});
        g_app->mapDirty = true;
    };
    const auto winkelSetzen = [&](MapEntity& ent, const float w3[3]) {
        if (z.dkArt == 0) {
            setzeWinkel3(ent, w3);
            return;
        }
        for (int k = 0; k < 3; ++k) { z.dkWinkel[k] = z.dkBasis[k] + std::remainder(w3[k] - z.startWinkel3[k], 360.0F); }
        dkVorschau();
    };
    if (z.aktiv && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (z.modus == 0 && z.dkArt == 1) {
            // Schluessel des Animationsfensters: in der waagrechten Ebene, in
            // der die Entity zu dieser Zeit steht.
            float a0[3];
            float b0[3];
            float a1[3];
            float b1[3];
            strahlDurch(z.startMaus[0], z.startMaus[1], a0, b0);
            strahlDurch(io.MousePos.x, io.MousePos.y, a1, b1);
            const float h = z.dkBasisOrt[2];
            if (std::fabs(b0[2] - a0[2]) > 1e-3F && std::fabs(b1[2] - a1[2]) > 1e-3F) {
                const float t0 = (h - a0[2]) / (b0[2] - a0[2]);
                const float t1 = (h - a1[2]) / (b1[2] - a1[2]);
                if (t0 > 0.0F && t1 > 0.0F) {
                    for (int k = 0; k < 2; ++k) {
                        z.dkOrt[k] = z.dkBasisOrt[k] + (a1[k] + (b1[k] - a1[k]) * t1) - (a0[k] + (b0[k] - a0[k]) * t0);
                    }
                    dkVorschau();
                }
            }
        } else if (z.modus == 0) {
            // Ueber die Flaeche unter der Maus.
            float a[3];
            float b[3];
            strahlDurch(io.MousePos.x, io.MousePos.y, a, b);
            TraceTreffer t;
            if (spur(a, b, t)) {
                float o[3];
                aufFlaeche(e.classname, t, o);
                for (int k = 0; k < 3; ++k) { o[k] -= z.versatz[k]; }
                if (g_app->rasterSchritt > 0 && !io.KeyCtrl) {
                    const float r = static_cast<float>(g_app->rasterSchritt);
                    o[0] = std::round(o[0] / r) * r;
                    o[1] = std::round(o[1] / r) * r;
                }
                if (z.zwischen && z.zwLauf) {
                    const float k2[3] = {o[0] + z.versatz[0], o[1] + z.versatz[1], o[2] + z.versatz[2]};
                    zwischenVorschau(k2);
                } else if (!z.zwischen) {
                    setKey(e, "origin", ortText(o));
                    g_app->mapDirty = true;
                }
            }
            // Ein Schiff mitten im Flug: in der waagrechten Ebene, in der es
            // gerade fliegt - nicht auf den Boden.
            if (z.zwischen && !z.zwLauf && std::fabs(b[2] - a[2]) > 1e-3F) {
                const float tt = (z.zwOrt[2] - a[2]) / (b[2] - a[2]);
                if (tt > 0.0F && tt < 1.0F) {
                    const float k2[3] = {a[0] + (b[0] - a[0]) * tt, a[1] + (b[1] - a[1]) * tt, z.zwOrt[2]};
                    zwischenVorschau(k2);
                }
            }
        } else if (z.modus <= 3) {
            // Entlang einer Achse: die Mausbewegung auf die Achse auf dem
            // Schirm abbilden, dann in Welteinheiten.
            const int k = z.modus - 1;
            const float gezeigt[3] = {z.startOrt[0] + z.versatz[0], z.startOrt[1] + z.versatz[1], z.startOrt[2] + z.versatz[2]};
            float ende[3] = {gezeigt[0], gezeigt[1], gezeigt[2]};
            ende[k] += z.achsLaenge;
            ImVec2 s0;
            ImVec2 s1;
            if (aufSchirm(p, gezeigt, s0) && aufSchirm(p, ende, s1)) {
                const float ax = s1.x - s0.x;
                const float ay = s1.y - s0.y;
                const float l2 = ax * ax + ay * ay;
                if (l2 > 4.0F) {
                    const float t = ((io.MousePos.x - z.startMaus[0]) * ax + (io.MousePos.y - z.startMaus[1]) * ay) / l2;
                    float o[3] = {z.startOrt[0], z.startOrt[1], z.startOrt[2]};
                    o[k] += t * z.achsLaenge;
                    // Raster: eingestellt (Leiste) - Strg schaltet es aus; ohne
                    // eingestelltes rastet Strg auf 8, wie in Radiant.
                    const float r = g_app->rasterSchritt > 0 ? (io.KeyCtrl ? 0.0F : static_cast<float>(g_app->rasterSchritt))
                                                             : (io.KeyCtrl ? 8.0F : 0.0F);
                    if (r > 0.0F) { o[k] = std::round(o[k] / r) * r; }
                    if (z.dkArt == 1) {
                        for (int q = 0; q < 3; ++q) { z.dkOrt[q] = z.dkBasisOrt[q] + (o[q] - z.startOrt[q]); }
                        dkVorschau();
                    } else if (z.zwischen) {
                        const float k2[3] = {o[0] + z.versatz[0], o[1] + z.versatz[1], o[2] + z.versatz[2]};
                        zwischenVorschau(k2);
                    } else {
                        setKey(e, "origin", ortText(o));
                        g_app->mapDirty = true;
                    }
                }
            }
        } else {
            // Ring: der Winkel der Maus um den Mittelpunkt auf dem Schirm.
            ImVec2 s0;
            const float gezeigt[3] = {z.startOrt[0] + z.versatz[0], z.startOrt[1] + z.versatz[1], z.startOrt[2] + z.versatz[2]};
            if (aufSchirm(p, gezeigt, s0)) {
                const float a0 = std::atan2(-(z.startMaus[1] - s0.y), z.startMaus[0] - s0.x);
                const float a1 = std::atan2(-(io.MousePos.y - s0.y), io.MousePos.x - s0.x);
                // Von oben gesehen dreht die Maus im Uhrzeigersinn = Winkel kleiner;
                // schaut die Kamera von unten, kehrt sich das um.
                const float sinn = p.upv[2] >= 0.0F ? 1.0F : -1.0F;
                if (z.modus == 4) {
                    float w = z.startWinkel + (a1 - a0) * 180.0F / 3.14159265F * sinn;
                    w = std::fmod(std::fmod(w, 360.0F) + 360.0F, 360.0F);
                    if (io.KeyCtrl) { w = std::fmod(std::round(w / 15.0F) * 15.0F, 360.0F); }
                    float w3[3] = {z.startWinkel3[0], w, z.startWinkel3[2]};
                    winkelSetzen(e, w3);
                } else {
                    // Nicken (5) oder Rollen (6): der Drehsinn haengt davon ab,
                    // ob die Achse zur Kamera zeigt.
                    float achse[3];
                    ringAchse(z.modus - 4, z.startWinkel3[1], achse);
                    const float zurKamera = achse[0] * (p.pos[0] - gezeigt[0]) + achse[1] * (p.pos[1] - gezeigt[1]) +
                                            achse[2] * (p.pos[2] - gezeigt[2]);
                    float d = (a1 - a0) * 180.0F / 3.14159265F * (zurKamera >= 0.0F ? 1.0F : -1.0F);
                    // Positives Nicken senkt die Nase (Engine); die Drehung um
                    // "rechts" hebt sie - also umgekehrt.
                    if (z.modus == 5) { d = -d; }
                    float w3[3] = {z.startWinkel3[0], z.startWinkel3[1], z.startWinkel3[2]};
                    w3[z.modus == 5 ? 0 : 2] += d;
                    if (io.KeyCtrl) {
                        const int i = z.modus == 5 ? 0 : 2;
                        w3[i] = std::round(w3[i] / 15.0F) * 15.0F;
                    }
                    winkelSetzen(e, w3);
                }
                g_app->mapDirty = true;
            }
        }
    }
    // Die Gruppe zieht mit: derselbe Versatz; beim Ring dreht sie um den
    // Hauptpunkt, und jeder dreht seinen Blick mit.
    if (z.aktiv && !z.gruppe.empty()) {
        float jetzt[3];
        if (entityOrt(g_app->map.entities[static_cast<std::size_t>(z.index)], jetzt)) {
            const float d[3] = {jetzt[0] - z.startOrt[0], jetzt[1] - z.startOrt[1], jetzt[2] - z.startOrt[2]};
            const float dw = winkelVon(g_app->map.entities[static_cast<std::size_t>(z.index)]) - z.startWinkel;
            const float c = std::cos(dw * 3.14159265F / 180.0F);
            const float sn = std::sin(dw * 3.14159265F / 180.0F);
            for (std::size_t g = 0; g < z.gruppe.size(); ++g) {
                const int i = z.gruppe[g].first;
                if (i < 0 || static_cast<std::size_t>(i) >= g_app->map.entities.size()) { continue; }
                MapEntity& ge = g_app->map.entities[static_cast<std::size_t>(i)];
                const auto& s0 = z.gruppe[g].second;
                float o[3] = {s0[0] + d[0], s0[1] + d[1], s0[2] + d[2]};
                if (z.modus == 4) {
                    const float rx = s0[0] - z.startOrt[0];
                    const float ry = s0[1] - z.startOrt[1];
                    o[0] = z.startOrt[0] + rx * c - ry * sn;
                    o[1] = z.startOrt[1] + rx * sn + ry * c;
                    o[2] = s0[2];
                    if (hatRichtung(ge)) {
                        const float w = std::fmod(std::fmod(z.gruppeWinkel[g] + dw, 360.0F) + 360.0F, 360.0F);
                        setKey(ge, "angle", std::to_string(static_cast<int>(std::lround(w)) % 360));
                    }
                }
                setKey(ge, "origin", ortText(o));
            }
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && z.dkArt != 0) {
        if (z.zwFigur >= 0 && static_cast<std::size_t>(z.zwFigur) < g_app->scene.actors.size()) {
            g_app->scene.actors[static_cast<std::size_t>(z.zwFigur)].steps = z.zwSteps;
        }
        if (z.dkBewegt) {
            if (z.dkArt == 1) {
                (void)animKeySetzen(z.zwT, z.dkOrt, z.dkWinkel);
            } else if (z.dkArt == 2) {
                (void)bahnPunktSchreiben(z.zwFigur, z.zwPfad, false, z.dkOrt, z.zwT, z.dkWinkel);
            } else {
                (void)figurBlickSetzen(z.zwFigur, z.zwT, z.dkWinkel[1]);
            }
        }
        g_app->camTrackValid = false;
        g_app->mapDirty = true;
        z = App::EntZug{};
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && z.zwischen) {
        if (z.zwBewegt) { (void)bahnPunktSchreiben(z.zwFigur, z.zwPfad, z.zwLauf, z.zwOrt, z.zwT); }
        g_app->camTrackValid = false;   // die Bahn aus dem Skript neu
        g_app->mapDirty = true;
        z = App::EntZug{};
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const bool geaendert = z.vorher.size() != g_app->map.entities.size() ||
                               (z.index < static_cast<int>(z.vorher.size()) &&
                                z.vorher[static_cast<std::size_t>(z.index)].keys != e.keys);
        if (z.aktiv && geaendert) {
            aenderung(std::move(z.vorher));
            addStatus(tr(Str::EntMoved));
        } else if (!z.aktiv && z.vorher.size() != g_app->map.entities.size()) {
            // Alt gedrueckt, aber nicht gezogen: die Kopien wieder weg.
            g_app->map.entities = std::move(z.vorher);
            g_app->entAuswahl.clear();
            g_app->pickedEntity = z.index < static_cast<int>(g_app->map.entities.size()) ? g_app->pickedEntity : -1;
        }
        z = App::EntZug{};
    }
}

// =============================================================================
// Griffe auf der Bahn (rc597)
// =============================================================================
//
// shank: "kann ich irgendwie den Anfangspunkt und den Endpunkt des bewegten
// Characters setzen, im Editor selbst beim Ziehen? ... das ich den Endpunkt
// nur anders setzen will oder den Startpunkt, und das auch bei den Schiffen".
//
// Auf der Bahn der gewaehlten Figur (oder des Schiffs) liegen Griffe:
//   gruen  der Start   - der Spawner bzw. der Ursprung des Schiffs
//   gelb   Laufziele   - die Entity, die ein SET_NAVGOAL ansteuert
//   blau   Flugziele   - der Ort eines move ( <ort>, ... )
// Ziehen verschiebt nur diesen einen Punkt. Start und Laufziele sind
// Entities: die Szene rechnet waehrend des Ziehens laufend neu, die Figur
// laeuft im Bild mit. Ein Flugziel steht im Skript und wird beim Loslassen
// geschrieben. Benutzt noch jemand ein Laufziel, bekommt dieser Lauf eine
// eigene Kopie (gewaehlt: "Eigenes Ziel anlegen").
namespace {

struct BahnGriff {
    int art = 0;
    float ort[3] = {0.0F, 0.0F, 0.0F};
    float von[3] = {0.0F, 0.0F, 0.0F};
    Path pfad;
    int entity = -1;
    double startMs = 0.0;
    double endMs = 0.0;
    bool gehen = false;
    std::string name;
};

int bahnFigur() {
    if (g_app->camViewActive || !g_app->showEntities) { return -1; }
    int f = g_app->selectedActor;
    if (f < 0 || static_cast<std::size_t>(f) >= g_app->scene.actors.size()) {
        f = g_app->pickedEntity >= 0 ? figurFuerEntity(g_app->pickedEntity) : -1;
    }
    return f;
}

// Wie findEntity in src/scene.cpp: erst targetname, dann NPC_targetname, nur
// mit Ursprung.
int zielEntity(const std::string& name) {
    if (name.empty()) { return -1; }
    const auto& ents = g_app->map.entities;
    for (int durchgang = 0; durchgang < 2; ++durchgang) {
        for (std::size_t i = 0; i < ents.size(); ++i) {
            const MapEntity& e = ents[i];
            if (e.origin.empty()) { continue; }
            const std::string* n = durchgang == 0 ? &e.targetname : e.find("NPC_targetname");
            if (n != nullptr && *n == name) { return static_cast<int>(i); }
        }
    }
    return -1;
}

bool istBrushEntity(int ei) {
    if (ei < 0 || static_cast<std::size_t>(ei) >= g_app->map.entities.size()) { return false; }
    const std::string* mdl = g_app->map.entities[static_cast<std::size_t>(ei)].find("model");
    return mdl != nullptr && mdl->size() > 1 && (*mdl)[0] == '*';
}

std::vector<BahnGriff> bahnGriffe() {
    std::vector<BahnGriff> g;
    const int f = bahnFigur();
    if (f < 0) { return g; }
    const Actor& a = g_app->scene.actors[static_cast<std::size_t>(f)];
    if (!a.haveStart) { return g; }
    BahnGriff s;
    s.art = 0;
    for (int k = 0; k < 3; ++k) { s.ort[k] = s.von[k] = a.start[k]; }
    if (a.brushModel > 0) {
        for (std::size_t i = 0; i < g_app->map.entities.size(); ++i) {
            const std::string* mdl = g_app->map.entities[i].find("model");
            if (mdl != nullptr && *mdl == "*" + std::to_string(a.brushModel)) { s.entity = static_cast<int>(i); }
        }
    } else {
        s.entity = entityFuerFigur(f);
    }
    if (s.entity >= 0) { g.push_back(s); }
    for (const ActorStep& st : a.steps) {
        if (st.path.empty() || !st.fremd.empty()) { continue; }
        const Node* n = nodeAt(g_app->doc.script(), st.path);
        if (n == nullptr) { continue; }
        BahnGriff z;
        for (int k = 0; k < 3; ++k) {
            z.ort[k] = st.to[k];
            z.von[k] = st.from[k];
        }
        z.pfad = st.path;
        z.startMs = st.startMs;
        z.endMs = st.endMs;
        if (st.kind == ActorStep::Kind::WalkTo && n->args.size() >= 2) {
            z.art = 1;
            z.name = n->args[1].text;
            z.entity = zielEntity(z.name);
            z.gehen = st.text == "BOTH_WALK1";
            if (z.entity >= 0) { g.push_back(z); }
        } else if (st.kind == ActorStep::Kind::Glide && n->name == "move" && !n->args.empty() &&
                   n->args[0].kind == Arg::Kind::Vector) {
            z.art = 2;
            g.push_back(z);
        }
    }
    return g;
}

// Wie oft kommt der Name vor - in allen offenen Skripten (jedes Argument) und
// als Ziel einer Entity (target..target4)?
int namensNutzung(const std::string& name) {
    const std::string k = klein(name);
    int n = 0;
    std::function<void(const std::vector<Node>&)> zaehle = [&](const std::vector<Node>& knoten) {
        for (const Node& nd : knoten) {
            for (const Arg& a : nd.args) {
                if (klein(a.text) == k) { ++n; }
            }
            zaehle(nd.children);
        }
    };
    zaehle(g_app->doc.script().nodes);
    for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
        if (static_cast<int>(i) != g_app->activeTab) { zaehle(g_app->tabs[i].doc.script().nodes); }
    }
    for (const MapEntity& e : g_app->map.entities) {
        for (const char* key : {"target", "target2", "target3", "target4"}) {
            const std::string* t = e.find(key);
            if (t != nullptr && klein(*t) == k) { ++n; }
        }
    }
    return n;
}

void nachSkriptGeaendert() {
    rebuildTree();
    g_app->camTrackValid = false;
    g_app->mapDirty = true;
}

// Eine eigene Kopie des Laufziels fuer diesen SET_NAVGOAL.
int zielKopieren(int ei, const Path& pfad) {
    const Node* alt = nodeAt(g_app->doc.script(), pfad);
    if (alt == nullptr || alt->args.size() < 2 || ei < 0) { return ei; }
    MapEntity neuE = g_app->map.entities[static_cast<std::size_t>(ei)];
    std::string stamm = neuE.targetname;
    while (!stamm.empty() && std::isdigit(static_cast<unsigned char>(stamm.back())) != 0) { stamm.pop_back(); }
    if (stamm.empty()) { stamm = "navgoal"; }
    const std::string neuName = freierEntityName(stamm);
    const std::string altName = neuE.targetname;
    setKey(neuE, "targetname", neuName);
    g_app->map.entities.push_back(std::move(neuE));
    Node n = *alt;
    n.args[1].text = neuName;
    g_app->doc.replaceAt(pfad, n);
    nachSkriptGeaendert();
    char t[300];
    std::snprintf(t, sizeof(t), tr(Str::BahnZielKopie), altName.c_str(), neuName.c_str());
    addStatus(t);
    return static_cast<int>(g_app->map.entities.size()) - 1;
}

}  // namespace

int bahnGriffZahl() { return static_cast<int>(bahnGriffe().size()); }

int bahnGriffArt(int nr) {
    const auto g = bahnGriffe();
    return nr >= 0 && static_cast<std::size_t>(nr) < g.size() ? g[static_cast<std::size_t>(nr)].art : -1;
}

bool bahnGriffPunkt(int nr, ImVec2& out) {
    const auto g = bahnGriffe();
    if (nr < 0 || static_cast<std::size_t>(nr) >= g.size()) { return false; }
    return aufSchirm(projektion(), g[static_cast<std::size_t>(nr)].ort, out);
}

namespace {
// set-Zeilen direkt vor einem SET_NAVGOAL setzen: vorhandene umstellen, sonst
// neu davor. Ein Rueckgaengig-Schritt.
void setzeVorNavgoal(const Path& navgoal, const std::vector<std::pair<std::string, std::string>>& werte, const char* was) {
    if (navgoal.empty()) { return; }
    Script s = g_app->doc.script();
    std::vector<Node>* geschw = &s.nodes;
    for (std::size_t k = 0; k + 1 < navgoal.size(); ++k) {
        if (navgoal[k] >= geschw->size()) { return; }
        geschw = &(*geschw)[navgoal[k]].children;
    }
    std::size_t i = navgoal.back();
    if (i >= geschw->size()) { return; }
    for (const auto& [typ, wert] : werte) {
        bool da = false;
        for (std::size_t j = i; j > 0 && !da; --j) {
            Node& v = (*geschw)[j - 1];
            if (v.kind != Node::Kind::Command || v.name != "set") { break; }
            if (v.args.size() >= 2 && v.args[0].text == typ) {
                v.args[1].text = wert;
                da = true;
            }
        }
        if (da) { continue; }
        Node n;
        n.kind = Node::Kind::Command;
        n.name = "set";
        Arg a0;
        a0.kind = Arg::Kind::String;
        a0.text = typ;
        a0.typeset = "SET_TYPES";
        Arg a1;
        a1.kind = Arg::Kind::String;
        a1.text = wert;
        n.args = {a0, a1};
        geschw->insert(geschw->begin() + static_cast<std::ptrdiff_t>(i), std::move(n));
        ++i;
    }
    g_app->doc.ersetzeSkript(std::move(s), was);
    nachSkriptGeaendert();
}
}  // namespace

void blickSetzen(const Path& navgoal, const std::string& ziel) {
    setzeVorNavgoal(navgoal, {{"SET_WATCHTARGET", ziel}}, tr(ziel == "NULL" ? Str::BahnBlickFrei : Str::BahnAnsehen));
}

void rueckwaertsLaufen(const Path& navgoal) {
    // Den Lauf suchen: von wo nach wo, und wer.
    for (const Actor& a : g_app->scene.actors) {
        for (const ActorStep& st : a.steps) {
            if (st.kind != ActorStep::Kind::WalkTo || st.path != navgoal) { continue; }
            float d[3] = {st.to[0] - st.from[0], st.to[1] - st.from[1], 0.0F};
            const float l = std::sqrt(d[0] * d[0] + d[1] * d[1]);
            if (l < 1.0F) { return; }
            // Die Marke liegt hinter dem Start, entgegen der Laufrichtung -
            // die Figur schaut den ganzen Weg zurueck.
            const float ort[3] = {st.from[0] - d[0] / l * 400.0F, st.from[1] - d[1] / l * 400.0F, st.from[2] + 40.0F};
            std::vector<MapEntity> vorher = g_app->map.entities;
            MapEntity e;
            setKey(e, "classname", "ref_tag");
            setKey(e, "origin", ortText(ort));
            const std::string name = freierEntityName(a.name + "_blick");
            setKey(e, "targetname", name);
            g_app->map.entities.push_back(std::move(e));
            aenderung(std::move(vorher));
            setzeVorNavgoal(navgoal, {{"SET_WATCHTARGET", name}}, tr(Str::BahnRueckwaerts));
            return;
        }
    }
}

void gangartSetzen(const Path& navgoal, bool gehen) {
    if (navgoal.empty()) { return; }
    Script s = g_app->doc.script();
    std::vector<Node>* geschw = &s.nodes;
    for (std::size_t k = 0; k + 1 < navgoal.size(); ++k) {
        if (navgoal[k] >= geschw->size()) { return; }
        geschw = &(*geschw)[navgoal[k]].children;
    }
    std::size_t i = navgoal.back();
    if (i >= geschw->size()) { return; }
    // Vorhandene set-Zeilen direkt davor umstellen, sonst eine neue davor.
    const auto setze = [&](const char* typ, const char* wert) {
        for (std::size_t j = i; j > 0; --j) {
            Node& v = (*geschw)[j - 1];
            if (v.kind != Node::Kind::Command || v.name != "set") { break; }
            if (v.args.size() >= 2 && v.args[0].text == typ) {
                v.args[1].text = wert;
                return;
            }
        }
        Node n;
        n.kind = Node::Kind::Command;
        n.name = "set";
        Arg a0;
        a0.kind = Arg::Kind::String;
        a0.text = typ;
        a0.typeset = "SET_TYPES";
        Arg a1;
        a1.kind = Arg::Kind::String;
        a1.text = wert;
        n.args = {a0, a1};
        geschw->insert(geschw->begin() + static_cast<std::ptrdiff_t>(i), std::move(n));
        ++i;
    };
    if (gehen) {
        // Gehen heisst in der Engine: walking und NICHT running.
        setze("SET_RUNNING", "false");
        setze("SET_WALKING", "true");
    } else {
        setze("SET_RUNNING", "true");
    }
    g_app->doc.ersetzeSkript(std::move(s), tr(gehen ? Str::BahnGehen : Str::BahnRennen));
    nachSkriptGeaendert();
}

// --- Bahnpunkt mitten in einer Bewegung (rc597) -------------------------------
namespace {
// Einen Befehl in eine Folge von Geschwistern genau zur Zeit `ziel` setzen.
// Die Zeit laeuft ab `ab` (Index) bei `t0`; ein wait(W) dauert W plus ein
// Spielbild. Faellt `ziel` in ein wait, wird es in zwei geteilt - die Summe
// bleibt, alle anderen Befehle behalten ihre Zeit.
bool befehlZurZeit(std::vector<Node>& g, std::size_t ab, double t0, double ziel, const Node& n) {
    constexpr double kSpielbild = 50.0;
    const auto zahl = [](double v) {
        char t[48];
        std::snprintf(t, sizeof(t), "%.3f", v);
        return std::string(t);
    };
    const auto warte = [&](double w) {
        Node x;
        x.kind = Node::Kind::Command;
        x.name = "wait";
        Arg a;
        a.kind = Arg::Kind::Number;
        a.text = zahl(w);
        x.args = {a};
        return x;
    };
    double t = t0;
    for (std::size_t j = ab; j < g.size(); ++j) {
        const Node& x = g[j];
        if (x.name == "waitsignal" || x.name == "dowait" ||
            (x.name == "wait" && (x.args.empty() || x.args[0].kind != Arg::Kind::Number))) {
            return false;   // die Zeit danach ist nicht bekannt
        }
        if (x.name != "wait") { continue; }
        const double w = std::atof(x.args[0].text.c_str());
        if (t + w + kSpielbild <= ziel) {
            t += w + kSpielbild;
            continue;
        }
        // ziel liegt in diesem wait: teilen.
        const double a = ziel - t - kSpielbild;   // erster Teil
        const double b = w - (ziel - t);          // zweiter Teil
        std::vector<Node> ein;
        if (a >= 0.0) { ein.push_back(warte(a)); }
        ein.push_back(n);
        if (b >= 0.0) { ein.push_back(warte(b)); }
        g.erase(g.begin() + static_cast<std::ptrdiff_t>(j));
        g.insert(g.begin() + static_cast<std::ptrdiff_t>(j), ein.begin(), ein.end());
        return true;
    }
    // Hinter allem: warten, dann der Befehl.
    if (ziel - t - kSpielbild >= 0.0) { g.push_back(warte(ziel - t - kSpielbild)); }
    g.push_back(n);
    return true;
}
}  // namespace

bool figurBlickSetzen(int figur, double ms, float gier) {
    if (figur < 0 || static_cast<std::size_t>(figur) >= g_app->scene.actors.size()) { return false; }
    const Actor& a = g_app->scene.actors[static_cast<std::size_t>(figur)];
    // Der letzte Befehl dieser Figur vor der Zeit - in seiner Folge kommt
    // der neue dazu; mitten im Laufen erst beim Ankommen (beim Laufen dreht
    // die Engine sie in Laufrichtung).
    const ActorStep* ref = nullptr;
    double ziel = ms;
    for (const ActorStep& st : a.steps) {
        if (st.startMs > ms) { break; }
        if (!st.path.empty() && st.fremd.empty()) { ref = &st; }
        if (st.kind == ActorStep::Kind::WalkTo && ms < st.endMs) { ziel = st.endMs; }
    }
    if (ref == nullptr) { return false; }
    Script s = g_app->doc.script();
    std::vector<Node>* geschw = &s.nodes;
    for (std::size_t k = 0; k + 1 < ref->path.size(); ++k) {
        if (ref->path[k] >= geschw->size()) { return false; }
        geschw = &(*geschw)[ref->path[k]].children;
    }
    if (ref->path.back() >= geschw->size()) { return false; }
    Node n;
    n.kind = Node::Kind::Command;
    n.name = "set";
    Arg a0;
    a0.kind = Arg::Kind::String;
    a0.text = "SET_ANGLES";
    a0.typeset = "SET_TYPES";
    Arg a1;
    a1.kind = Arg::Kind::String;
    const int g = static_cast<int>(std::lround(std::fmod(std::fmod(gier, 360.0F) + 360.0F, 360.0F))) % 360;
    a1.text = "0 " + std::to_string(g) + " 0";
    n.args = {a0, a1};
    if (!befehlZurZeit(*geschw, ref->path.back() + 1, ref->startMs, ziel, n)) {
        addStatus(tr(Str::BahnBlickZeitUnbekannt));
        return false;
    }
    g_app->doc.ersetzeSkript(std::move(s), tr(Str::BahnBlickUndo));
    nachSkriptGeaendert();
    char t[160];
    std::snprintf(t, sizeof(t), tr(ziel > ms + 1.0 ? Str::BahnBlickAnkunft : Str::BahnBlickGesetzt), g, ziel / 1000.0);
    addStatus(t);
    return true;
}

bool bahnPunktMoeglich(int ei, double ms, int& figur, Path& pfad, bool& lauf) {
    figur = figurFuerEntity(ei);
    if (figur < 0) { return false; }
    const Actor& a = g_app->scene.actors[static_cast<std::size_t>(figur)];
    for (const ActorStep& st : a.steps) {
        const bool l = st.kind == ActorStep::Kind::WalkTo;
        if ((!l && st.kind != ActorStep::Kind::Glide) || st.path.empty() || !st.fremd.empty()) { continue; }
        if (!(ms > st.startMs + 50.0 && ms < st.endMs - 25.0)) { continue; }
        if (!a.at(ms).visible) { return false; }   // eine unsichtbare Figur greift man nicht
        const Node* n = nodeAt(g_app->doc.script(), st.path);
        if (n == nullptr) { return false; }
        if (l ? (n->args.size() < 2) : (n->name != "move" || n->args.size() < 2 || n->args[0].kind != Arg::Kind::Vector)) {
            return false;
        }
        pfad = st.path;
        lauf = l;
        return true;
    }
    return false;
}

bool bahnPunktSchreiben(int figur, const Path& pfad, bool lauf, const float kOrt[3], double ms, const float* kWinkel) {
    if (figur < 0 || static_cast<std::size_t>(figur) >= g_app->scene.actors.size() || pfad.empty()) { return false; }
    // Der Schritt aus der UNVERAENDERTEN Szene (die Vorschau hat die Bahn im
    // Speicher geteilt - die Szene ist beim Loslassen noch die gezogene).
    const ActorStep* stp = nullptr;
    ActorStep merk;
    for (const ActorStep& st : g_app->scene.actors[static_cast<std::size_t>(figur)].steps) {
        if (st.path == pfad && (st.kind == ActorStep::Kind::WalkTo || st.kind == ActorStep::Kind::Glide)) {
            if (stp == nullptr) {
                merk = st;
                stp = &merk;
            } else {
                // Zweite Haelfte der Vorschau: ihr Ende ist das echte Ende.
                merk.endMs = st.endMs;
                for (int k = 0; k < 3; ++k) { merk.to[k] = st.to[k]; }
            }
        }
    }
    if (stp == nullptr) { return false; }
    const double t0 = merk.startMs;
    const double ende = merk.endMs;
    Script s = g_app->doc.script();
    std::vector<Node>* geschw = &s.nodes;
    for (std::size_t k = 0; k + 1 < pfad.size(); ++k) {
        if (pfad[k] >= geschw->size()) { return false; }
        geschw = &(*geschw)[pfad[k]].children;
    }
    const std::size_t i = pfad.back();
    if (i >= geschw->size()) { return false; }
    std::vector<Node>& g = *geschw;
    // Wie lange dauert der eingefuegte Teil? Schiff: bis ms (die Dauer steht
    // im move). Figur: so lange sie mit ihrem Tempo bis K braucht.
    double teil = ms - t0;
    if (lauf) {
        const float dab = std::sqrt((merk.to[0] - merk.from[0]) * (merk.to[0] - merk.from[0]) +
                                    (merk.to[1] - merk.from[1]) * (merk.to[1] - merk.from[1]) +
                                    (merk.to[2] - merk.from[2]) * (merk.to[2] - merk.from[2]));
        const float dak = std::sqrt((kOrt[0] - merk.from[0]) * (kOrt[0] - merk.from[0]) +
                                    (kOrt[1] - merk.from[1]) * (kOrt[1] - merk.from[1]) +
                                    (kOrt[2] - merk.from[2]) * (kOrt[2] - merk.from[2]));
        if (dab < 1.0F || ende <= t0) { return false; }
        teil = static_cast<double>(dak) / (static_cast<double>(dab) / (ende - t0));
    }
    constexpr double kSpielbild = 50.0;   // ein wait endet ein Bild spaeter
    if (teil < kSpielbild + 1.0) { return false; }
    // Das naechste wait derselben Ebene nimmt den eingefuegten Teil auf.
    std::size_t waitNr = g.size();
    for (std::size_t j = i + 1; j < g.size(); ++j) {
        if (g[j].name == "waitsignal" || g[j].name == "dowait") { break; }
        if (g[j].name == "wait") {
            if (!g[j].args.empty() && g[j].args[0].kind == Arg::Kind::Number) { waitNr = j; }
            break;
        }
    }
    double waitWert = 0.0;
    if (waitNr < g.size()) {
        waitWert = std::atof(g[waitNr].args[0].text.c_str());
        if (!lauf && waitWert + 1e-3 < teil) { return false; }
    }
    const auto zahl = [](double v) {
        char t[48];
        std::snprintf(t, sizeof(t), "%.3f", v);
        return std::string(t);
    };
    const auto vektor = [](const float* v) {
        char t[96];
        std::snprintf(t, sizeof(t), "%.3f %.3f %.3f", static_cast<double>(v[0]), static_cast<double>(v[1]),
                      static_cast<double>(v[2]));
        return std::string(t);
    };
    std::vector<MapEntity> vorher = g_app->map.entities;
    Node erster = g[i];
    erster.kennung = 0;
    Node zweiter = g[i];
    if (lauf) {
        // Ein neues Laufziel bei K - eine Kopie des alten Ziels (gleiche Art).
        MapEntity ziel;
        const int alt = [&] {
            for (std::size_t q = 0; q < g_app->map.entities.size(); ++q) {
                if (g_app->map.entities[q].targetname == g[i].args[1].text) { return static_cast<int>(q); }
            }
            return -1;
        }();
        if (alt >= 0) {
            ziel = g_app->map.entities[static_cast<std::size_t>(alt)];
        } else {
            setKey(ziel, "classname", "waypoint_navgoal");
        }
        std::string stamm = g[i].args[1].text;
        while (!stamm.empty() && std::isdigit(static_cast<unsigned char>(stamm.back())) != 0) { stamm.pop_back(); }
        if (stamm.empty()) { stamm = "navgoal"; }
        const std::string name = freierEntityName(stamm);
        setKey(ziel, "targetname", name);
        setKey(ziel, "origin", ortText(kOrt));
        g_app->map.entities.push_back(std::move(ziel));
        erster.args[1].text = name;
    } else {
        // Schiff: bis ms nach K, mit der Stellung, die es dort hat.
        erster.args[0].text = vektor(kOrt);
        if (erster.args.size() >= 3 && erster.args[1].kind == Arg::Kind::Vector) {
            const ActorState jetzt = g_app->scene.actors[static_cast<std::size_t>(figur)].at(ms);
            erster.args[1].text = vektor(kWinkel != nullptr ? kWinkel : jetzt.angles);
        } else if (kWinkel != nullptr) {
            // move ( <ort>, dauer ) ohne Winkel: fuer den gedrehten Punkt
            // die Form mit Winkeln (rc599).
            Arg w;
            w.kind = Arg::Kind::Vector;
            w.text = vektor(kWinkel);
            erster.args.insert(erster.args.begin() + 1, w);
        }
        erster.args.back().text = zahl(teil);
        zweiter.args.back().text = zahl(ende - ms);
    }
    Node warte;
    warte.kind = Node::Kind::Command;
    warte.name = "wait";
    Arg wa;
    wa.kind = Arg::Kind::Number;
    wa.text = zahl(teil - kSpielbild);
    warte.args = {wa};
    if (lauf) {
        // Erst zum neuen Ziel, und wenn sie dort ankommt, weiter zum alten.
        const Node altesZiel = g[i];
        g[i] = erster;
        if (!befehlZurZeit(g, i + 1, t0, t0 + teil, altesZiel)) {
            g_app->map.entities = std::move(vorher);
            return false;
        }
        aenderung(std::move(vorher));
        g_app->doc.ersetzeSkript(std::move(s), tr(Str::BahnZwischenUndo));
        nachSkriptGeaendert();
        char tf[96];
        std::snprintf(tf, sizeof(tf), tr(Str::BahnZwischen), ms / 1000.0);
        addStatus(tf);
        return true;
    }
    std::vector<Node> neu;
    neu.reserve(g.size() + 2U);
    for (std::size_t q = 0; q < i; ++q) { neu.push_back(g[q]); }
    neu.push_back(erster);
    const std::size_t bis = waitNr < g.size() ? waitNr : g.size();
    for (std::size_t q = i + 1; q < bis; ++q) { neu.push_back(g[q]); }
    neu.push_back(warte);
    neu.push_back(zweiter);
    if (waitNr < g.size()) {
        Node w = g[waitNr];
        w.args[0].text = zahl(waitWert - teil);
        neu.push_back(w);
        for (std::size_t q = waitNr + 1; q < g.size(); ++q) { neu.push_back(g[q]); }
    }
    g = std::move(neu);
    if (lauf) { aenderung(std::move(vorher)); }
    g_app->doc.ersetzeSkript(std::move(s), tr(Str::BahnZwischenUndo));
    nachSkriptGeaendert();
    char t[96];
    std::snprintf(t, sizeof(t), tr(Str::BahnZwischen), ms / 1000.0);
    addStatus(t);
    return true;
}

void gangartMenueInhalt() {
    if (ImGui::MenuItem(tr(Str::BahnGehen))) { gangartSetzen(g_app->bahnMenuePfad, true); }
    if (ImGui::MenuItem(tr(Str::BahnRennen))) { gangartSetzen(g_app->bahnMenuePfad, false); }
    ImGui::Separator();
    if (ImGui::MenuItem(tr(Str::BahnRueckwaerts))) { rueckwaertsLaufen(g_app->bahnMenuePfad); }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tr(Str::BahnBlickHinweis)); }
    if (ImGui::BeginMenu(tr(Str::BahnAnsehen))) {
        // Die anderen Figuren der Szene - wen man beim Laufen ansieht.
        std::string selbst;
        for (const Actor& a : g_app->scene.actors) {
            for (const ActorStep& st : a.steps) {
                if (st.kind == ActorStep::Kind::WalkTo && st.path == g_app->bahnMenuePfad) { selbst = a.name; }
            }
        }
        for (const Actor& a : g_app->scene.actors) {
            if (a.name == selbst || a.brushModel > 0 || !a.haveStart) { continue; }
            if (ImGui::MenuItem(a.name.c_str())) { blickSetzen(g_app->bahnMenuePfad, a.name); }
        }
        ImGui::EndMenu();
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tr(Str::BahnBlickHinweis)); }
    if (ImGui::MenuItem(tr(Str::BahnBlickFrei))) { blickSetzen(g_app->bahnMenuePfad, "NULL"); }
}

void bahnGriffeMaus() {
    App::BahnZug& z = g_app->bahnZug;
    const ImGuiIO& io = ImGui::GetIO();
    g_app->bahnGriffHover = -1;
    const std::vector<BahnGriff> griffe = bahnGriffe();
    if (griffe.empty() && !z.kandidat) { return; }
    const Proj p = projektion();
    const bool imBild = io.MousePos.x >= g_app->kartenAnsicht[0] && io.MousePos.x < g_app->kartenAnsicht[2] &&
                        io.MousePos.y >= g_app->kartenAnsicht[1] && io.MousePos.y < g_app->kartenAnsicht[3] &&
                        ImGui::IsWindowHovered();
    if (!z.kandidat && imBild) {
        const float r = ImGui::GetFontSize() * 0.6F;
        float best = r * r;
        for (std::size_t i = 0; i < griffe.size(); ++i) {
            ImVec2 s;
            if (!aufSchirm(p, griffe[i].ort, s)) { continue; }
            const float d = (s.x - io.MousePos.x) * (s.x - io.MousePos.x) + (s.y - io.MousePos.y) * (s.y - io.MousePos.y);
            if (d < best) {
                best = d;
                g_app->bahnGriffHover = static_cast<int>(i);
            }
        }
    }
    const int h = g_app->bahnGriffHover;
    // Rechtsklick auf ein Laufziel: Gehen oder Rennen.
    if (h >= 0 && griffe[static_cast<std::size_t>(h)].art == 1 && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] <= io.MouseDragThreshold * io.MouseDragThreshold) {
        g_app->bahnMenuePfad = griffe[static_cast<std::size_t>(h)].pfad;
        g_app->bahnMenueBild = ImGui::GetFrameCount();
        ImGui::OpenPopup("##gangart");
    }
    if (h >= 0 && !z.kandidat && !g_app->wegKlickVerbraucht && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const BahnGriff& gr = griffe[static_cast<std::size_t>(h)];
        z = App::BahnZug{};
        z.kandidat = true;
        z.art = gr.art;
        z.entity = gr.entity;
        z.pfad = gr.pfad;
        z.vorher = g_app->map.entities;
        for (int k = 0; k < 3; ++k) {
            z.startOrt[k] = z.ort[k] = gr.ort[k];
            z.von[k] = gr.von[k];
        }
        z.startMaus[0] = io.MousePos.x;
        z.startMaus[1] = io.MousePos.y;
        // Die Zeile im Skript (und damit der Balken unten) gleich mit.
        if (!z.pfad.empty()) { selectByPath(z.pfad); }
    }
    if (!z.kandidat) { return; }
    g_app->wegKlickVerbraucht = true;   // solange gezogen wird, gehoert die Maus den Griffen
    if (!z.aktiv && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0F)) {
        z.aktiv = true;
        if (z.art == 1 && z.entity >= 0 &&
            namensNutzung(g_app->map.entities[static_cast<std::size_t>(z.entity)].targetname) > 1) {
            z.entity = zielKopieren(z.entity, z.pfad);
        }
        z.figur = bahnFigur();
        if (z.figur >= 0) {
            const Actor& a = g_app->scene.actors[static_cast<std::size_t>(z.figur)];
            z.stepsVorher = a.steps;
            for (int k = 0; k < 3; ++k) { z.startVorher[k] = a.start[k]; }
        }
        z.szeneBautenBeimZug = g_app->szeneBauten;
    }
    if (z.aktiv && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        float a[3];
        float b[3];
        strahlDurch(io.MousePos.x, io.MousePos.y, a, b);
        if (z.art == 2 || (z.art == 0 && istBrushEntity(z.entity))) {
            // Schiffe fliegen: in der waagrechten Ebene des Griffs, mit
            // Umschalt senkrecht.
            if (io.KeyShift) {
                z.ort[2] = z.startOrt[2] + (z.startMaus[1] - io.MousePos.y) * laengeFuerPx(p, z.startOrt, 1.0F);
            } else if (std::fabs(b[2] - a[2]) > 1e-3F) {
                const float t = (z.ort[2] - a[2]) / (b[2] - a[2]);
                if (t > 0.0F && t < 1.0F) {
                    z.ort[0] = a[0] + (b[0] - a[0]) * t;
                    z.ort[1] = a[1] + (b[1] - a[1]) * t;
                }
            }
        } else if (z.entity >= 0) {
            TraceTreffer t;
            if (spur(a, b, t)) {
                aufFlaeche(g_app->map.entities[static_cast<std::size_t>(z.entity)].classname, t, z.ort);
            }
        }
        if (z.art != 2 && z.entity >= 0) {
            setKey(g_app->map.entities[static_cast<std::size_t>(z.entity)], "origin", ortText(z.ort));
        }
        // Die Bahn dieser Figur im Speicher nachziehen - jedes Bild, ohne die
        // Szene neu zu bauen. Nur der gezogene Punkt wandert: der Anfang des
        // ersten Stuecks (Start) bzw. das Ende dieses Stuecks und der Anfang
        // des naechsten (Ziel). Die Dauern rechnet erst das Loslassen neu.
        if (z.figur >= 0 && static_cast<std::size_t>(z.figur) < g_app->scene.actors.size() && !z.stepsVorher.empty()) {
            Actor& a = g_app->scene.actors[static_cast<std::size_t>(z.figur)];
            a.steps = z.stepsVorher;
            const auto gleich = [](const float* p, const float* q) {
                return std::fabs(p[0] - q[0]) + std::fabs(p[1] - q[1]) + std::fabs(p[2] - q[2]) < 1.0F;
            };
            if (z.art == 0) {
                for (int k = 0; k < 3; ++k) { a.start[k] = z.ort[k]; }
                for (ActorStep& st : a.steps) {
                    // Ein Teleport auf den Spawnort (beim Erscheinen) geht mit.
                    if (st.kind == ActorStep::Kind::Teleport && gleich(st.to, z.startVorher)) {
                        for (int k = 0; k < 3; ++k) { st.to[k] = z.ort[k]; }
                        continue;
                    }
                    if (st.kind == ActorStep::Kind::WalkTo || st.kind == ActorStep::Kind::Glide) {
                        if (gleich(st.from, z.startVorher)) {
                            for (int k = 0; k < 3; ++k) { st.from[k] = z.ort[k]; }
                        }
                        break;
                    }
                }
            } else {
                bool danach = false;
                for (ActorStep& st : a.steps) {
                    const bool bewegung = st.kind == ActorStep::Kind::WalkTo || st.kind == ActorStep::Kind::Glide;
                    if (!bewegung) { continue; }
                    if (danach) {
                        if (gleich(st.from, z.startOrt)) {
                            for (int k = 0; k < 3; ++k) { st.from[k] = z.ort[k]; }
                        }
                        break;
                    }
                    if (st.path == z.pfad) {
                        for (int k = 0; k < 3; ++k) { st.to[k] = z.ort[k]; }
                        danach = true;
                    }
                }
            }
        }
        g_app->mapDirty = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (z.aktiv) {
            if (z.art == 2) {
                const Node* alt = nodeAt(g_app->doc.script(), z.pfad);
                if (alt != nullptr && !alt->args.empty()) {
                    Node n = *alt;
                    char t[96];
                    std::snprintf(t, sizeof(t), "%.3f %.3f %.3f", static_cast<double>(z.ort[0]), static_cast<double>(z.ort[1]),
                                  static_cast<double>(z.ort[2]));
                    n.args[0].text = t;
                    g_app->doc.replaceAt(z.pfad, n);
                    nachSkriptGeaendert();
                }
            } else {
                aenderung(std::move(z.vorher));
            }
            g_app->camTrackValid = false;   // jetzt einmal richtig, mit den neuen Dauern
        }
        z = App::BahnZug{};
    }
}

void bahnGriffeZeichnen() {
    if (ImGui::BeginPopup("##gangart")) {
        gangartMenueInhalt();
        ImGui::EndPopup();
    }
    const std::vector<BahnGriff> griffe = bahnGriffe();
    if (griffe.empty()) { return; }
    const App::BahnZug& z = g_app->bahnZug;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ImVec2{g_app->kartenAnsicht[0], g_app->kartenAnsicht[1]},
                     ImVec2{g_app->kartenAnsicht[2], g_app->kartenAnsicht[3]}, true);
    const Proj p = projektion();
    const float px = std::max(1.0F, ImGui::GetFontSize() / 13.0F);
    const float r = 6.0F * px;
    // --- Die Bahn selbst, kraeftig und obenauf ---------------------------
    //
    // Laufen und Fliegen gehen in geraden Stuecken von Punkt zu Punkt - also
    // genau durch die Griffe. Grau voraus, gelb schon zurueckgelegt.
    {
        const int f = bahnFigur();
        if (f >= 0) {
            const Actor& a = g_app->scene.actors[static_cast<std::size_t>(f)];
            const ActorState jetzt = a.at(g_app->playMs);
            const auto linie = [&](const float* von, const float* bis, ImU32 farbe) {
                ImVec2 s0;
                ImVec2 s1;
                if (aufSchirm(p, von, s0) && aufSchirm(p, bis, s1)) {
                    dl->AddLine(s0, s1, IM_COL32(10, 12, 16, 170), 4.5F * px);
                    dl->AddLine(s0, s1, farbe, 2.2F * px);
                }
            };
            const ImU32 grau = IM_COL32(170, 175, 185, 230);
            const ImU32 gelb = IM_COL32(240, 200, 60, 255);
            for (const ActorStep& st : a.steps) {
                if (st.kind != ActorStep::Kind::WalkTo && st.kind != ActorStep::Kind::Glide) { continue; }
                if (g_app->playMs >= st.endMs) {
                    linie(st.from, st.to, gelb);
                } else if (g_app->playMs <= st.startMs) {
                    linie(st.from, st.to, grau);
                } else {
                    linie(st.from, jetzt.pos, gelb);
                    linie(jetzt.pos, st.to, grau);
                }
            }
        }
    }
    for (std::size_t i = 0; i < griffe.size(); ++i) {
        const BahnGriff& gr = griffe[i];
        // Der gezogene Griff steht, wo die Maus ihn hinstellt.
        const bool gezogen = z.aktiv && z.art == gr.art && (gr.art == 0 ? z.entity >= 0 : z.pfad == gr.pfad);
        const float* ort = gezogen ? z.ort : gr.ort;
        ImVec2 s;
        if (!aufSchirm(p, ort, s)) { continue; }
        // Ausserhalb der Ansicht: ein Pfeil am Rand zeigt, wo der Griff liegt.
        const float rand = 14.0F * px;
        const float x0 = g_app->kartenAnsicht[0] + rand;
        const float y0 = g_app->kartenAnsicht[1] + rand;
        const float x1 = g_app->kartenAnsicht[2] - rand;
        const float y1 = g_app->kartenAnsicht[3] - rand;
        if (s.x < x0 || s.x > x1 || s.y < y0 || s.y > y1) {
            const ImVec2 m{(x0 + x1) * 0.5F, (y0 + y1) * 0.5F};
            const ImVec2 rd{s.x - m.x, s.y - m.y};
            const float tx = rd.x != 0.0F ? ((rd.x > 0.0F ? x1 : x0) - m.x) / rd.x : 1e9F;
            const float ty = rd.y != 0.0F ? ((rd.y > 0.0F ? y1 : y0) - m.y) / rd.y : 1e9F;
            const float t = std::min(tx, ty);
            const ImVec2 e{m.x + rd.x * t, m.y + rd.y * t};
            const float l = std::sqrt(rd.x * rd.x + rd.y * rd.y);
            const ImVec2 d{rd.x / l, rd.y / l};
            const ImVec2 n{-d.y, d.x};
            const ImU32 pf = gr.art == 0 ? IM_COL32(90, 220, 120, 255)
                           : gr.art == 1 ? IM_COL32(240, 200, 60, 255)
                                         : IM_COL32(90, 190, 255, 255);
            const float g2 = 9.0F * px;
            dl->AddTriangleFilled(ImVec2{e.x + d.x * g2, e.y + d.y * g2}, ImVec2{e.x - d.x * g2 + n.x * g2 * 0.7F, e.y - d.y * g2 + n.y * g2 * 0.7F},
                                  ImVec2{e.x - d.x * g2 - n.x * g2 * 0.7F, e.y - d.y * g2 - n.y * g2 * 0.7F}, pf);
            continue;
        }
        if (gezogen && gr.art != 0) {
            ImVec2 v;
            if (aufSchirm(p, gr.von, v)) { dl->AddLine(v, s, IM_COL32(255, 255, 255, 160), 1.5F * px); }
        }
        const ImU32 farbe = gr.art == 0 ? IM_COL32(90, 220, 120, 255)
                          : gr.art == 1 ? IM_COL32(240, 200, 60, 255)
                                        : IM_COL32(90, 190, 255, 255);
        const bool ueber = g_app->bahnGriffHover == static_cast<int>(i) || gezogen;
        dl->AddRectFilled(ImVec2{s.x - r, s.y - r}, ImVec2{s.x + r, s.y + r}, farbe);
        dl->AddRect(ImVec2{s.x - r, s.y - r}, ImVec2{s.x + r, s.y + r},
                    ueber ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 22, 28, 220), 0.0F, 0, (ueber ? 2.0F : 1.2F) * px);
        if (ueber && !z.aktiv) {
            ImGui::BeginTooltip();
            if (gr.art == 0) {
                ImGui::TextUnformatted(tr(Str::BahnStart));
            } else if (gr.art == 1) {
                ImGui::Text(tr(Str::BahnZiel), gr.name.c_str());
                ImGui::TextDisabled("%s, %.1f s - %s", tr(gr.gehen ? Str::BahnGehtJetzt : Str::BahnRenntJetzt),
                                    (gr.endMs - gr.startMs) / 1000.0, tr(Str::BahnZielMenue));
            } else {
                ImGui::TextUnformatted(tr(Str::BahnFlug));
            }
            ImGui::EndTooltip();
        }
    }
    dl->PopClipRect();
}

// Fuer den Selbsttest: wo liegt ein Griff des Gizmos auf dem Schirm?
// griff 1..3 = Achse (bei drei Vierteln), 4 = Ring (vorn).
bool entityGizmoPunkt(int griff, ImVec2& out) {
    const int ziel = inspektorZiel();
    float o[3];
    if (ziel < 0 || !entityOrtAnzeige(g_app->map.entities[static_cast<std::size_t>(ziel)], o)) { return false; }
    const Proj p = projektion();
    const float L = laengeFuerPx(p, o, kGizmoPx);
    float w[3] = {o[0], o[1], o[2]};
    if (griff >= 1 && griff <= 3) {
        w[griff - 1] += L * 0.75F;
    } else if (griff == 4) {
        // Der Punkt des Rings, der der Kamera am naechsten liegt.
        const float a = std::atan2(p.pos[1] - o[1], p.pos[0] - o[0]);
        w[0] += std::cos(a) * L * 0.8F;
        w[1] += std::sin(a) * L * 0.8F;
    } else {
        // Nicken (5) / Rollen (6): bei 45 Grad - oben schneiden sich die
        // beiden Ringe, waagrecht schneidet sie der Gier-Ring.
        ringPunkt(o, griff - 4, entityGierAnzeige(g_app->map.entities[static_cast<std::size_t>(ziel)]), L * 0.8F,
                  0.7853982F, w);
    }
    return aufSchirm(p, w, out);
}

void skriptFokusAuffrischen() {
    // Alle Argumente des Skripts, klein, ohne Anfuehrungszeichen.
    std::unordered_set<std::string> namen;
    std::function<void(const std::vector<Node>&)> sammle = [&](const std::vector<Node>& knoten) {
        for (const Node& n : knoten) {
            for (const Arg& a : n.args) {
                std::string t = a.text;
                if (t.size() >= 2 && t.front() == '"' && t.back() == '"') { t = t.substr(1, t.size() - 2); }
                if (!t.empty()) { namen.insert(klein(t)); }
            }
            sammle(n.children);
        }
    };
    sammle(g_app->doc.script().nodes);
    const auto& ents = g_app->map.entities;
    g_app->entImSkript.assign(ents.size(), 0);
    for (std::size_t i = 0; i < ents.size(); ++i) {
        const MapEntity& e = ents[i];
        const std::string a = klein(entitySkriptName(e));
        const std::string b = klein(e.targetname);
        g_app->entImSkript[i] = static_cast<char>((!a.empty() && namen.count(a) != 0U) || (!b.empty() && namen.count(b) != 0U));
    }
}

void skriptSchild() {
    if (g_app->camViewActive || g_app->map.empty()) {
        g_app->skriptSchildText.clear();
        return;
    }
    const std::string name = !g_app->shownName.empty() ? g_app->shownName : fileName(g_app->path);
    g_app->skriptSchildText = name;
    if (name.empty()) { return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float px = std::max(1.0F, ImGui::GetFontSize() / 13.0F);
    const float g = std::floor(ImGui::GetFontSize());
    const ImVec2 groesse = ImGui::CalcTextSize(name.c_str());
    const ImVec2 a{g_app->kartenAnsicht[0] + 8.0F * px, g_app->kartenAnsicht[1] + 8.0F * px};
    const ImVec2 b{a.x + g + 14.0F * px + groesse.x, a.y + std::max(g, groesse.y) + 8.0F * px};
    const ImU32 akzent = ImGui::GetColorU32(ImGuiCol_TabSelectedOverline);
    dl->AddRectFilled(a, b, IM_COL32(16, 18, 24, 190), 4.0F * px);
    dl->AddRect(a, b, akzent, 4.0F * px, 0, 1.2F * px);
    uiIcon(dl, UiIcon::Skript, ImVec2{a.x + 5.0F * px, std::floor(a.y + 4.0F * px)}, g, akzent);
    dl->AddText(ImVec2{a.x + g + 9.0F * px, a.y + 4.0F * px}, IM_COL32(235, 238, 245, 255), name.c_str());
}

void entityUeberlagerung(float rasterSkal) {
    if (!g_app->showEntities || g_app->camViewActive) {
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ImVec2{g_app->kartenAnsicht[0], g_app->kartenAnsicht[1]},
                     ImVec2{g_app->kartenAnsicht[2], g_app->kartenAnsicht[3]}, true);
    const Proj p = projektion();
    const float px = std::max(1.0F, ImGui::GetFontSize() / 13.0F);
    // --- Icons ueber den Entities (rc588) ------------------------------------
    //
    // shank: "vielleicht auch verschiedene map icons fuer die entities so das
    // man die schon beim unterscheiden kann sowohl links als auch auf der
    // map". Wie die Sprites in Unreal: ein kleines Schild mit dem Icon der
    // Gruppe in ihrer Farbe, ueber dem Kreuz am Ursprung. Dieselben Icons
    // stehen links in der Liste.
    {
        const float skal = rasterSkal > 0.0F ? rasterSkal : 1.0F;
        const float g = std::floor(ImGui::GetFontSize() * 0.95F);
        const float r = g * 0.72F;
        for (const App::PickedMarker& mk : g_app->markers) {
            if (mk.entity >= g_app->map.entities.size()) { continue; }
            const MapEntity& e = g_app->map.entities[mk.entity];
            const int kat = entityKategorie(e);
            // Wegpunkte (Gruppe 9) zeichnet das Wegnetz selbst - ein Schild
            // ueber jedem von Hunderten waere nur Unruhe.
            if (kat < 0 || kat == 9) { continue; }
            const EntityKat& ek = entityKategorieInfo(kat);
            const ImVec2 c{g_app->kartenAnsicht[0] + mk.sx / skal, g_app->kartenAnsicht[1] + mk.sy / skal - r * 1.6F};
            // Hinter einer Wand: blass und ohne Rand - man ahnt sie, aber
            // sie draengen sich nicht vor (vorher alle gleich kraeftig, durch
            // jede Wand hindurch).
            const bool gewaehlt = entityIstGewaehlt(static_cast<int>(mk.entity)) ||
                                  g_app->entHover == static_cast<int>(mk.entity);
            // Woran man arbeitet (rc597): was das aktive Skript benutzt, kraeftig
            // und mit hellem Ring; alles andere blasser.
            const bool fokus = g_app->skriptFokus && mk.entity < g_app->entImSkript.size();
            const bool imSkript = fokus && g_app->entImSkript[mk.entity] != 0;
            int deck = (mk.verdeckt && !gewaehlt) ? 55 : 255;
            if (fokus && !imSkript && !gewaehlt) { deck = std::min(deck, 80); }
            dl->AddCircleFilled(c, r, IM_COL32(18, 20, 26, deck * 190 / 255));
            if (imSkript) {
                dl->AddCircle(c, r + 2.0F * px, IM_COL32(255, 255, 255, mk.verdeckt ? 90 : 200), 0, 1.6F * px);
            }
            if (deck == 255) { dl->AddCircle(c, r, IM_COL32(ek.r, ek.g, ek.b, imSkript ? 255 : 150), 0, (imSkript ? 1.8F : 1.2F) * px); }
            uiIcon(dl, ek.icon, ImVec2{std::floor(c.x - g * 0.5F), std::floor(c.y - g * 0.5F)}, g,
                   IM_COL32(ek.r, ek.g, ek.b, deck));
        }
    }
    const auto zeichneKasten = [&](const MapEntity& e, ImU32 farbe, float dicke) {
        float o[3];
        if (!entityOrtAnzeige(e, o)) { return false; }
        float mn[3];
        float mx[3];
        kasten(e, mn, mx);
        ImVec2 s[8];
        for (int k = 0; k < 8; ++k) {
            const float w[3] = {o[0] + ((k & 1) != 0 ? mx[0] : mn[0]), o[1] + ((k & 2) != 0 ? mx[1] : mn[1]),
                                o[2] + ((k & 4) != 0 ? mx[2] : mn[2])};
            if (!aufSchirm(p, w, s[k])) { return false; }
        }
        static const int kanten[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                          {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& k : kanten) { dl->AddLine(s[k[0]], s[k[1]], farbe, dicke); }
        return true;
    };
    const auto zeichneRichtung = [&](const MapEntity& e, ImU32 farbe, float dicke) {
        float o[3];
        if (!hatRichtung(e) || !entityOrtAnzeige(e, o)) { return; }
        float mn[3];
        float mx[3];
        kasten(e, mn, mx);
        // Wohin das Modell gerade schaut (rc597), nicht nur der Schluessel.
        const float a = entityGierAnzeige(e) * 3.14159265F / 180.0F;
        const float l = std::max(32.0F, mx[0] - mn[0]) * 1.2F;
        const float m[3] = {o[0], o[1], o[2] + (mn[2] + mx[2]) * 0.5F};
        const float spitze[3] = {m[0] + std::cos(a) * l, m[1] + std::sin(a) * l, m[2]};
        const float s1[3] = {m[0] + std::cos(a) * l * 0.75F - std::sin(a) * l * 0.15F,
                             m[1] + std::sin(a) * l * 0.75F + std::cos(a) * l * 0.15F, m[2]};
        const float s2[3] = {m[0] + std::cos(a) * l * 0.75F + std::sin(a) * l * 0.15F,
                             m[1] + std::sin(a) * l * 0.75F - std::cos(a) * l * 0.15F, m[2]};
        ImVec2 a0;
        ImVec2 a1;
        ImVec2 b1;
        ImVec2 b2;
        if (aufSchirm(p, m, a0) && aufSchirm(p, spitze, a1)) {
            dl->AddLine(a0, a1, farbe, dicke);
            if (aufSchirm(p, s1, b1) && aufSchirm(p, s2, b2)) {
                dl->AddTriangleFilled(a1, b1, b2, farbe);
            }
        }
    };
    const auto namensSchild = [&](const MapEntity& e, ImU32 farbe) {
        float o[3];
        ImVec2 s;
        if (!entityOrtAnzeige(e, o)) { return; }
        float mn[3];
        float mx[3];
        kasten(e, mn, mx);
        const float oben[3] = {o[0], o[1], o[2] + mx[2]};
        if (!aufSchirm(p, oben, s)) { return; }
        const std::string n = entitySkriptName(e);
        const std::string t = (n.empty() ? e.classname : n + "  (" + e.classname + ")") + "\n" + ortText(o);
        const ImVec2 g = ImGui::CalcTextSize(t.c_str());
        const ImVec2 a{s.x - g.x * 0.5F - 4.0F * px, s.y - g.y - 8.0F * px};
        dl->AddRectFilled(a, ImVec2{a.x + g.x + 8.0F * px, a.y + g.y + 4.0F * px}, IM_COL32(15, 17, 22, 210), 3.0F * px);
        dl->AddText(ImVec2{a.x + 4.0F * px, a.y + 2.0F * px}, farbe, t.c_str());
    };

    const int ziel = inspektorZiel();
    const int n = static_cast<int>(g_app->map.entities.size());
    // Ueberfahren (Ansicht oder Liste) - hell.
    for (const int h : {g_app->entHover, g_app->entListeHover}) {
        if (h >= 0 && h < n && h != ziel) {
            const MapEntity& e = g_app->map.entities[static_cast<std::size_t>(h)];
            if (zeichneKasten(e, IM_COL32(255, 255, 255, 190), 1.5F * px)) {
                namensSchild(e, IM_COL32(235, 238, 245, 255));
            }
        }
    }
    // Die weiteren gewaehlten (Mehrfachauswahl): orange, duenner, ohne Gizmo.
    for (const int i : g_app->entAuswahl) {
        if (i >= 0 && i < n && i != ziel) {
            zeichneKasten(g_app->map.entities[static_cast<std::size_t>(i)], IM_COL32(255, 165, 40, 210), 1.6F * px);
        }
    }
    // Der Rahmen, der gerade gezogen wird.
    if (g_app->boxAktiv) {
        const ImVec2 a{g_app->boxStart[0], g_app->boxStart[1]};
        const ImVec2 b = ImGui::GetIO().MousePos;
        dl->AddRectFilled(ImVec2{std::min(a.x, b.x), std::min(a.y, b.y)}, ImVec2{std::max(a.x, b.x), std::max(a.y, b.y)},
                          IM_COL32(255, 165, 40, 40));
        dl->AddRect(ImVec2{std::min(a.x, b.x), std::min(a.y, b.y)}, ImVec2{std::max(a.x, b.x), std::max(a.y, b.y)},
                    IM_COL32(255, 165, 40, 220), 0.0F, 0, 1.5F * px);
    }
    // Gewaehlt - orange, mit Richtung und Gizmo.
    if (ziel >= 0 && ziel < n) {
        const MapEntity& e = g_app->map.entities[static_cast<std::size_t>(ziel)];
        const ImU32 orange = IM_COL32(255, 165, 40, 255);
        zeichneKasten(e, orange, 2.5F * px);
        zeichneRichtung(e, orange, 2.0F * px);
        if (g_app->entHover == ziel || g_app->entZug.kandidat) { namensSchild(e, orange); }
        float o[3];
        if (entityOrtAnzeige(e, o)) {
            const App::EntZug& z = g_app->entZug;
            const float L = (z.kandidat && z.modus > 0) ? z.achsLaenge : laengeFuerPx(p, o, kGizmoPx);
            ImVec2 so;
            if (aufSchirm(p, o, so)) {
                const ImU32 achsFarbe[3] = {IM_COL32(235, 70, 70, 255), IM_COL32(80, 210, 90, 255),
                                            IM_COL32(70, 130, 255, 255)};
                const int aktiv = z.kandidat ? z.modus : g_app->entGizmoHover;
                const bool drehen = g_app->gizmoMode == App::GizmoMode::Rotate;
                // Drehen: die Ringe (Gier blau, Nicken gruen, Rollen rot) -
                // Verschieben: die Pfeile.
                if (drehen) {
                    const ImU32 ringFarbe[3] = {IM_COL32(70, 130, 255, 255), IM_COL32(80, 210, 90, 255),
                                                IM_COL32(235, 70, 70, 255)};
                    const float gier = entityGierAnzeige(e);
                    for (int achse = 0; achse < (kannKippen(e) ? 3 : 1); ++achse) {
                        const bool an = aktiv == 4 + achse;
                        const ImU32 f = an ? IM_COL32(255, 240, 120, 255) : ringFarbe[achse];
                        ImVec2 vorher;
                        bool hat = false;
                        for (int k = 0; k <= 48; ++k) {
                            float w[3];
                            ringPunkt(o, achse, gier, L * 0.8F, static_cast<float>(k) / 48.0F * 6.2831853F, w);
                            ImVec2 s;
                            if (!aufSchirm(p, w, s)) { hat = false; continue; }
                            if (hat) { dl->AddLine(vorher, s, f, (an ? 3.5F : 2.5F) * px); }
                            vorher = s;
                            hat = true;
                        }
                    }
                }
                for (int k = 0; k < 3 && !drehen; ++k) {
                    float w[3] = {o[0], o[1], o[2]};
                    w[k] += L;
                    ImVec2 se;
                    if (!aufSchirm(p, w, se)) { continue; }
                    ImU32 c = achsFarbe[k];
                    if (aktiv == k + 1) { c = IM_COL32(255, 245, 140, 255); }
                    dl->AddLine(so, se, c, (aktiv == k + 1 ? 3.5F : 2.5F) * px);
                    // Pfeilspitze
                    const float dx = se.x - so.x;
                    const float dy = se.y - so.y;
                    const float l = std::sqrt(dx * dx + dy * dy);
                    if (l > 4.0F) {
                        const float ux = dx / l;
                        const float uy = dy / l;
                        const float sp = 9.0F * px;
                        dl->AddTriangleFilled(ImVec2{se.x + ux * sp, se.y + uy * sp},
                                              ImVec2{se.x - uy * sp * 0.5F, se.y + ux * sp * 0.5F},
                                              ImVec2{se.x + uy * sp * 0.5F, se.y - ux * sp * 0.5F}, c);
                    }
                }
                dl->AddCircleFilled(so, 3.5F * px, IM_COL32(255, 255, 255, 230));
                // Hinweis beim Ueberfahren eines Griffs.
                if (!z.kandidat && g_app->entGizmoHover != 0) {
                    ImGui::SetTooltip("%s", tr(g_app->entGizmoHover >= 4 ? Str::EntGizmoRing : Str::EntGizmoAxis));
                }
            }
        }
    }
    dl->PopClipRect();
}

// --- Der Inspektor -------------------------------------------------------------
void drawEntityInspector(float hoehe) {
    // Aufbau wie Unreals "Details" (rc586): oben fest der Kopf mit Klasse und
    // einer Icon-Leiste, darunter ein Suchfeld, dann - nur dieser Teil rollt -
    // die Gruppen Allgemein, Spawnflags, Felder. In jeder Gruppe steht die
    // Beschriftung links und der Wert rechts; vorher stand die Beschriftung
    // ueber jedem Feld, und man rollte doppelt so weit.
    ImGui::BeginChild("entinspektor", ImVec2{-1.0F, hoehe}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    g_app->entFokusListe = g_app->entFokusListe || ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const int i = inspektorZiel();
    if (i < 0) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextDisabled("%s", tr(g_app->map.entities.empty() ? Str::MissionKeine : Str::EntPickHint));
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        return;
    }
    // Kopie: Felder unten aendern map.entities, die Anzeige bleibt fuer
    // dieses Bild beim alten Stand.
    const MapEntity e = g_app->map.entities[static_cast<std::size_t>(i)];
    const ent::Def* d = ent::def(e.classname);
    ImGui::PushID(i);

    // --- Kopf: Klasse in ihrer Farbe, Erklaerung als Hinweis ----------------
    const ImVec4 farbe = d != nullptr ? ImVec4{0.45F + d->farbe[0] * 0.55F, 0.45F + d->farbe[1] * 0.55F,
                                              0.45F + d->farbe[2] * 0.55F, 1.0F}
                                      : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    {
        const float g = ImGui::GetFontSize();
        const ImVec2 q = ImGui::GetCursorScreenPos();
        uiIcon(ImGui::GetWindowDrawList(), UiIcon::Entity, q, g, ImGui::GetColorU32(farbe));
        ImGui::Dummy(ImVec2{g, g});
        ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x);
    }
    ImGui::TextColored(farbe, "%s", e.classname.c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0F);
        ImGui::TextUnformatted(d != nullptr && entText(d->text)[0] != '\0' ? entText(d->text) : tr(Str::EntUnknownClass));
        if (d != nullptr && d->brush) { ImGui::TextDisabled("%s", tr(Str::EntBrushClass)); }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    if (g_app->entitiesUngespeichert.count(g_app->map.path) != 0U) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr(Str::EntUnsavedMark));
    }
    if (const std::size_t mehr = entityAuswahl().size(); mehr > 1U) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{1.0F, 0.65F, 0.16F, 1.0F}, tr(Str::EntMoreSelected), static_cast<int>(mehr - 1));
    }

    // --- Icon-Leiste --------------------------------------------------------
    {
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{sp * 0.4F, ImGui::GetStyle().ItemSpacing.y});
        float o0[3];
        const bool ort = entityOrt(e, o0);
        if (iconKnopf(tr(Str::EntityFlyTo), UiIcon::Hinfliegen, nullptr, false, ort)) {
            zurEntityFliegen(static_cast<std::size_t>(i));
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::EntDropFloor), UiIcon::Boden, tr(Str::EntDropFloorHint), false, ort)) {
            if (!entityAufBoden(i)) { addStatus(tr(Str::EntNoFloor)); }
        }
        ImGui::SameLine();
        // Mit Mehrfachauswahl gelten beide fuer alle gewaehlten.
        if (iconKnopf(tr(Str::EntDuplicate), UiIcon::Duplizieren, tr(Str::EntDuplicateHint))) { entityAuswahlDuplizieren(); }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::EntDelete), UiIcon::Loeschen, nullptr)) { entityAuswahlLoeschen(); }
        // Animieren (rc592): Schiffe, Tueren, Plattformen selbst bewegen.
        if (entityAnimierbar(e)) {
            ImGui::SameLine();
            if (iconKnopf(tr(Str::EntAnimate), UiIcon::Animieren, tr(Str::EntAnimateHint), g_app->animFensterOffen &&
                                                                                             g_app->animEntity == i,
                          true, true)) {
                if (g_app->animFensterOffen && g_app->animEntity == i) {
                    g_app->animFensterOffen = false;
                } else {
                    animStarten(i);
                }
            }
        }
        ImGui::SameLine(0.0F, sp * 1.2F);
        if (iconKnopf(tr(Str::EditUndo), UiIcon::Rueckgaengig, nullptr, false, entityUndoMoeglich())) {
            entityRueckgaengig();
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::EditRedo), UiIcon::Wiederholen, nullptr, false, entityRedoMoeglich())) {
            entityWiederholen();
        }
        ImGui::SameLine(0.0F, sp * 1.2F);
        if (iconKnopf(tr(Str::EntRaw), UiIcon::Liste, tr(Str::EntRawHint), g_app->entRawAnsicht, true, true)) {
            g_app->entRawAnsicht = !g_app->entRawAnsicht;
        }
        ImGui::PopStyleVar();
    }

    // --- Suchfeld -----------------------------------------------------------
    static char suche[48] = "";
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##feldsuche", tr(Str::EntSearchFields), suche, sizeof(suche));
    const std::string such = klein(suche);
    const auto passt = [&such](const std::string& n) { return such.empty() || klein(n).find(such) != std::string::npos; };

    // --- Der rollende Teil --------------------------------------------------
    ImGui::BeginChild("entfelder", ImVec2{-1.0F, -1.0F}, ImGuiChildFlags_None);
    const float hilfeBreite = ImGui::GetFontSize() * 24.0F;
    const auto hinweis = [hilfeBreite](const char* titel, const char* hilfe) {
        if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { return; }
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(hilfeBreite);
        ImGui::TextUnformatted(titel);
        if (hilfe != nullptr && hilfe[0] != '\0') { ImGui::TextDisabled("%s", hilfe); }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    };
    // Tabellen mit zwei Spalten (Beschriftung | Wert); Zeilen beginnt `zeile`.
    constexpr ImGuiTableFlags kTabelle = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
    const auto spalten = [] {
        ImGui::TableSetupColumn("##k", ImGuiTableColumnFlags_WidthStretch, 0.42F);
        ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch, 0.58F);
    };
    const auto zeile = [&hinweis](const char* label, const char* titel, const char* hilfe) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        hinweis(titel, hilfe);
        ImGui::TableSetColumnIndex(1);
    };
    std::set<std::string> gezeigt = {"classname", "origin", "angle", "spawnflags"};
    // Die Gruppenkoepfe im Ton der Eingabefelder - das Blau der Auswahl war
    // fuer drei Ueberschriften zu laut.
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));

    // Eine Feldzeile: Wert, bei Skripten "Open", beim NPC-Typ die Auswahl, in
    // der Rohansicht ein x zum Entfernen.
    const auto zeigeFeld = [&](const std::string& name, const std::string& hilfe) {
        ImGui::PushID(name.c_str());
        const std::string* wert = e.find(name);
        zeile(name.c_str(), name.c_str(), hilfe.c_str());
        const bool skript = istSkriptFeld(name);
        const bool npcTyp = klein(name) == "npc_type";
        const ImGuiStyle& st = ImGui::GetStyle();
        float knoepfe = 0.0F;
        if (skript) { knoepfe += ImGui::CalcTextSize(tr(Str::EntOpenScript)).x + st.FramePadding.x * 2.0F + st.ItemSpacing.x; }
        if (npcTyp) { knoepfe += ImGui::CalcTextSize(tr(Str::EntPickNpc)).x + st.FramePadding.x * 2.0F + st.ItemSpacing.x; }
        if (g_app->entRawAnsicht) { knoepfe += ImGui::GetFrameHeight() + st.ItemSpacing.x; }
        ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - knoepfe, ImGui::GetFontSize() * 3.0F));
        std::string neu;
        if (textFeld("##wert", wert != nullptr ? *wert : std::string(), neu)) {
            entityKeySetzen(i, name, neu);
        }
        if (skript) {
            ImGui::SameLine();
            const bool vorhanden = wert != nullptr && !wert->empty();
            ImGui::BeginDisabled(!vorhanden);
            if (ImGui::SmallButton(tr(Str::EntOpenScript)) && vorhanden) { skriptOeffnen(*wert, true); }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", tr(Str::EntOpenScriptHint));
            }
        }
        if (npcTyp) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr(Str::EntPickNpc))) { ImGui::OpenPopup("npcwahl"); }
            if (ImGui::BeginPopup("npcwahl")) {
                static char filter[48] = "";
                if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); }
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0F);
                ImGui::InputTextWithHint("##npcfilter", tr(Str::EntNpcSearch), filter, sizeof(filter));
                const std::string f = klein(filter);
                ImGui::BeginChild("##npcliste", ImVec2{ImGui::GetFontSize() * 14.0F, ImGui::GetFontSize() * 16.0F});
                for (const std::string& t : npcTypen()) {
                    if (!f.empty() && klein(t).find(f) == std::string::npos) { continue; }
                    if (ImGui::Selectable(t.c_str(), wert != nullptr && klein(*wert) == klein(t))) {
                        entityKeySetzen(i, name, t);
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndChild();
                ImGui::EndPopup();
            }
        }
        if (g_app->entRawAnsicht) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr(Str::EntRemoveKey))) { entityKeySetzen(i, name, std::string()); }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::EntRemoveKeyHint)); }
        }
        if (npcTyp && (wert == nullptr || wert->empty())) {
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextColored(ImVec4{1.0F, 0.75F, 0.3F, 1.0F}, "%s", tr(Str::EntNpcTypeMissing));
            ImGui::PopTextWrapPos();
        }
        ImGui::PopID();
    };

    // --- Allgemein ------------------------------------------------------------
    const char* nameKey = istNpcKlasse(e.classname) ? "NPC_targetname" : "targetname";
    std::vector<std::pair<std::string, std::string>> wichtig;   // Name, Hilfe
    for (const char* w : wichtigeFelder(e.classname)) {
        std::string hilfe;
        for (std::size_t k = 0; d != nullptr && k < d->feldAnzahl; ++k) {
            if (klein(d->felder[k].name) == klein(w)) { hilfe = entText(d->felder[k].text); }
        }
        wichtig.emplace_back(w, hilfe);
    }
    float o[3] = {0.0F, 0.0F, 0.0F};
    const bool hatOrt = entityOrt(e, o);
    const bool zeigeOrt = hatOrt || (d != nullptr && !d->brush);
    gezeigt.insert(klein(nameKey));
    for (const auto& [w, h] : wichtig) {
        (void)h;
        gezeigt.insert(klein(w));
    }
    bool allgemeinPasst = passt(tr(Str::EntName)) || passt(nameKey) || passt("origin") || passt("angle");
    for (const auto& x : wichtig) { allgemeinPasst = allgemeinPasst || passt(x.first); }
    if (allgemeinPasst &&
        ImGui::CollapsingHeader((std::string(tr(Str::EntSecGeneral)) + "###allgemein").c_str(), ImGuiTreeNodeFlags_DefaultOpen) &&
        ImGui::BeginTable("##allgemein", 2, kTabelle)) {
        spalten();
        // Name - unter ihm sprechen die Skripte die Entity an.
        if (passt(tr(Str::EntName)) || passt(nameKey)) {
            zeile(tr(Str::EntName), nameKey, tr(Str::EntNameHint));
            const std::string* n = e.find(nameKey);
            std::string neu;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (textFeld("##name", n != nullptr ? *n : std::string(), neu)) {
                entityKeySetzen(i, nameKey, neu);
            }
        }
        // Das Feld, ohne das die Entity nichts tut (NPC_type, usescript ...).
        for (const auto& [w, h] : wichtig) {
            if (passt(w)) { zeigeFeld(w, h); }
        }
        if (zeigeOrt && (passt("origin") || passt(tr(Str::EntOrigin)))) {
            // Ort - live beim Ziehen, ein Rueckgaengig-Schritt am Ende.
            static std::vector<MapEntity> vorOrt;
            zeile(tr(Str::EntOrigin), "origin", nullptr);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat3("##ort", o, 1.0F, -65536.0F, 65536.0F, "%.0f")) {
                setKey(g_app->map.entities[static_cast<std::size_t>(i)], "origin", ortText(o));
                g_app->mapDirty = true;
            }
            if (ImGui::IsItemActivated()) { vorOrt = g_app->map.entities; }
            if (ImGui::IsItemDeactivatedAfterEdit() && vorOrt.size() == g_app->map.entities.size()) {
                aenderung(std::move(vorOrt));
                vorOrt.clear();
            }
            // Wie hoch stehen die Fuesse ueber dem Boden?
            float abstand = 0.0F;
            zeile(tr(Str::EntFloor), tr(Str::EntFloor), tr(Str::EntDropFloorHint));
            if (hatOrt && bodenAbstand(e, abstand)) {
                if (abstand < -0.5F) {
                    ImGui::TextColored(ImVec4{1.0F, 0.45F, 0.35F, 1.0F}, tr(Str::EntBelowFloor), static_cast<double>(-abstand));
                } else {
                    ImGui::TextDisabled(tr(Str::EntAboveFloor), static_cast<double>(abstand));
                }
            } else {
                ImGui::TextDisabled("%s", tr(Str::EntNoFloor));
            }
        }
        if (zeigeOrt && (passt("angle") || passt(tr(Str::EntAngle)))) {
            float winkel = e.find("angle") != nullptr ? std::strtof(e.find("angle")->c_str(), nullptr) : 0.0F;
            static std::vector<MapEntity> vorWinkel;
            zeile(tr(Str::EntAngle), "angle", nullptr);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##winkel", &winkel, 1.0F, -360.0F, 720.0F, "%.0f")) {
                setKey(g_app->map.entities[static_cast<std::size_t>(i)], "angle",
                       std::to_string(static_cast<int>(std::lround(winkel))));
                g_app->mapDirty = true;
            }
            if (ImGui::IsItemActivated()) { vorWinkel = g_app->map.entities; }
            if (ImGui::IsItemDeactivatedAfterEdit() && vorWinkel.size() == g_app->map.entities.size()) {
                aenderung(std::move(vorWinkel));
                vorWinkel.clear();
            }
        }
        ImGui::EndTable();
    }

    // --- Spawnflags als Haekchen mit Namen ---------------------------------------
    if (d != nullptr && d->flagAnzahl > 0) {
        bool eines = passt("spawnflags") || passt(tr(Str::EntFlags));
        for (std::size_t k = 0; k < d->flagAnzahl && !eines; ++k) { eines = passt(d->flags[k].name); }
        if (eines &&
            ImGui::CollapsingHeader((std::string(tr(Str::EntFlags)) + "###flags").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            const bool alle = such.empty() || passt("spawnflags") || passt(tr(Str::EntFlags));
            const int flags = e.find("spawnflags") != nullptr ? std::atoi(e.find("spawnflags")->c_str()) : 0;
            int bekannt = 0;
            // Zwei Spalten, wenn der laengste Name hineinpasst.
            float laengste = 0.0F;
            for (std::size_t k = 0; k < d->flagAnzahl; ++k) { laengste = std::max(laengste, ImGui::CalcTextSize(d->flags[k].name).x); }
            const float breite = ImGui::GetContentRegionAvail().x;
            const bool zweiSpalten = d->flagAnzahl > 2 && laengste + ImGui::GetFrameHeight() +
                                                                ImGui::GetStyle().ItemInnerSpacing.x * 2.0F < breite * 0.5F;
            const bool tabelle = zweiSpalten && ImGui::BeginTable("##flagtabelle", 2);
            for (std::size_t k = 0; k < d->flagAnzahl; ++k) {
                const ent::Flag& f = d->flags[k];
                bekannt |= f.bit;
                if (!alle && !passt(f.name)) { continue; }
                if (tabelle) { ImGui::TableNextColumn(); }
                bool an = (flags & f.bit) != 0;
                char label[96];
                std::snprintf(label, sizeof(label), "%s##f%d", f.name, f.bit);
                if (ImGui::Checkbox(label, &an)) {
                    const int neu = an ? (flags | f.bit) : (flags & ~f.bit);
                    entityKeySetzen(i, "spawnflags", neu == 0 ? std::string() : std::to_string(neu));
                }
                char titel[96];
                std::snprintf(titel, sizeof(titel), "%s (%d)", f.name, f.bit);
                hinweis(titel, entText(f.text));
            }
            if (tabelle) { ImGui::EndTable(); }
            if ((flags & ~bekannt) != 0) {
                ImGui::TextDisabled(tr(Str::EntOtherBits), flags & ~bekannt);
            }
        }
    }

    // --- Felder: erst die aus der Beschreibung, dann die uebrigen -----------------
    std::vector<std::pair<std::string, std::string>> felder;   // Name, Hilfe
    if (!g_app->entRawAnsicht && d != nullptr) {
        for (std::size_t k = 0; k < d->feldAnzahl; ++k) {
            const std::string n = d->felder[k].name;
            if (gezeigt.count(klein(n)) != 0U) { continue; }
            gezeigt.insert(klein(n));
            felder.emplace_back(n, entText(d->felder[k].text));
        }
    }
    for (const auto& [k, v] : e.keys) {
        (void)v;
        if (g_app->entRawAnsicht ? klein(k) == "classname" : gezeigt.count(klein(k)) != 0U) { continue; }
        gezeigt.insert(klein(k));
        felder.emplace_back(k, std::string());
    }
    felder.erase(std::remove_if(felder.begin(), felder.end(), [&](const auto& f) { return !passt(f.first); }), felder.end());
    if ((!felder.empty() || such.empty()) &&
        ImGui::CollapsingHeader((std::string(tr(Str::EntSecFields)) + "###felder").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("##felder", 2, kTabelle)) {
            spalten();
            for (const auto& [name, hilfe] : felder) { zeigeFeld(name, hilfe); }
            ImGui::EndTable();
        }
        // Ein Feld dazu.
        static char neuerKey[48] = "";
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6F);
        const bool enter = ImGui::InputTextWithHint("##neuerkey", tr(Str::EntKeyName), neuerKey, sizeof(neuerKey),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::SmallButton(tr(Str::EntAddKey)) || enter) && neuerKey[0] != '\0') {
            // Ohne Wert gibt es den Schluessel nicht - "1" als Anfang, sichtbar und aenderbar.
            if (e.find(neuerKey) == nullptr) { entityKeySetzen(i, neuerKey, "1"); }
            neuerKey[0] = '\0';
        }
    }
    ImGui::PopStyleColor(3);
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextDisabled("%s", tr(Str::EntDragHint));
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::EndChild();
}

// --- Mission: Ordner, Zielarchiv ----------------------------------------------
std::string missionenWurzel() {
    if (const char* t = std::getenv("BHED_MISSIONEN"); t != nullptr && t[0] != '\0') { return t; }
    const std::string docs = platform::dokumenteOrdner();
    return docs.empty() ? std::string() : pfadVerbinden(docs, "behaved\\missions");
}

std::string missionStandardOrdner(const std::string& name) {
    const std::string w = missionenWurzel();
    return w.empty() ? std::string() : pfadVerbinden(w, name);
}

namespace {
// Wohin das fertige Archiv gehoert: in den Movie-Duels-Ordner, sonst in den
// letzten Spielordner (Mods stehen hinter base).
std::string standardZielPk3(const std::string& name) {
    std::string ordner;
    for (const GamePath& gp : g_app->gamePaths) {
        const std::string d = klein(gp.directory);
        if (ordner.empty() || endetAuf(d, "\\md") || endetAuf(d, "/md") || d.find("movie duels") != std::string::npos) {
            ordner = gp.directory;
        }
    }
    return ordner.empty() ? std::string() : pfadVerbinden(ordner, "zz_" + name + ".pk3");
}

std::string standardMissionsName(const std::string& karte) {
    std::string b = fileName(karte);
    if (endetAuf(b, ".bsp")) { b.resize(b.size() - 4); }
    for (const char* weg : {"duel_", "md_", "mp/"}) {
        if (klein(b).rfind(weg, 0) == 0) { b = b.substr(std::string(weg).size()); }
    }
    std::string r;
    for (const char c : b) { r += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_'; }
    return "md_" + r + "_jedi";
}

std::string ordnerAusName(const std::string& name) {
    std::string o = name;
    for (const char* weg : {"_jedi", "_sith"}) {
        if (endetAuf(o, weg) && o.size() > 5) { o.resize(o.size() - 5); }
    }
    return o;
}

// Alle Karten der Spielordner, sortiert.
const std::vector<std::string>& alleKarten() {
    static std::vector<std::string> karten;
    static std::size_t stand = 0;
    std::size_t n = 0;
    for (const GamePath& gp : g_app->gamePaths) { n += gp.archives.size(); }
    if (n != stand) {
        std::set<std::string> s;
        for (const GamePath& gp : g_app->gamePaths) {
            for (const Pk3& arc : gp.archives) {
                for (const Pk3Entry& e : arc.entries) {
                    const std::string l = klein(e.name);
                    if (l.rfind("maps/", 0) == 0 && endetAuf(l, ".bsp")) { s.insert(e.name); }
                }
            }
        }
        karten.assign(s.begin(), s.end());
        stand = n;
    }
    return karten;
}
}  // namespace

bool neueMissionAnlegen(const std::string& karte, const std::string& name, const std::string& ordner,
                        std::string* fehler) {
    const auto nein = [fehler](const std::string& t) {
        if (fehler != nullptr) { *fehler = t; }
        return false;
    };
    if (!gueltigerMissionsName(name) || !gueltigerMissionsName(ordner)) {
        return nein(tr(Str::MissionBadName));
    }
    const std::string projekt = missionStandardOrdner(name);
    if (projekt.empty()) {
        return nein("no documents folder");
    }
    if (gibtEs(projekt)) {
        char t[700];
        std::snprintf(t, sizeof(t), tr(Str::MissionExists), projekt.c_str());
        return nein(t);
    }
    // Die Grundkarte: die schon geladene oder eine aus den Archiven.
    if (karte.empty() && g_app->map.empty()) {
        return nein(tr(Str::MissionNoMap));
    }
    const std::string id = "maps/" + name + ".bsp";
    {
        std::string schon;
        if (readFromArchives(id, schon)) {
            char t[400];
            std::snprintf(t, sizeof(t), tr(Str::MissionNameTaken), id.c_str());
            return nein(t);
        }
    }
    std::string quelle = karte;
    if (quelle.empty()) {
        quelle = g_app->settings.mapPath.empty() ? g_app->map.path : g_app->settings.mapPath;
    }
    std::string bytes;
    const bool vonPlatte = quelle.find(':') != std::string::npos || quelle.find('\\') != std::string::npos;
    if (vonPlatte) {
        bytes = slurp(quelle);
    } else if (!kartenBytes(quelle, bytes)) {
        bytes.clear();
    }
    MapData basisKarte;
    if (bytes.empty() || !readBsp(bytes, basisKarte)) {
        return nein(quelle);
    }
    // Ist die Quelle selbst eine neue Mission, gilt deren Grundkarte.
    if (!vonPlatte) {
        if (const auto q = g_app->kartenQuelle.find(quelle); q != g_app->kartenQuelle.end()) { quelle = q->second; }
    }
    // Die Entities der .bsp - nicht die einer frueher geladenen .ent.
    const NeueMission nm = neueMission(basisKarte.entities, ordner, name);
    const std::string intro = pfadVerbinden(projekt, "scripts/" + nm.introRef + ".txt");
    if (!schreibe(intro, nm.introText) ||
        !schreibe(pfadVerbinden(projekt, "maps/" + name + ".ent"), writeEntities(nm.entities))) {
        return nein(projekt);
    }
    g_app->kartenQuelle[id] = quelle;
    neuerReiter();
    vergissEntities(id);
    loadMapFromBytes(bytes, id);
    g_app->settings.mapPath.clear();
    g_app->map.entities = nm.entities;
    collectNames(g_app->map);
    merkeEntities();
    g_app->entUndo.clear();
    g_app->entRedo.clear();
    g_app->entitiesUngespeichert.erase(g_app->map.path);
    g_app->pickedEntity = -1;
    g_app->hiddenEntities.clear();

    g_app->mission = App::MissionsProjekt{};
    g_app->mission.name = name;
    g_app->mission.ordner = ordner;
    g_app->mission.karte = id;
    g_app->mission.projektOrdner = projekt;
    g_app->mission.zielPk3 = standardZielPk3(name);
    g_app->missionAktiv = true;

    missionsDateiSchreiben();
    ladeSkriptDatei(intro);
    g_app->camTrackValid = false;
    g_app->mapDirty = true;
    g_app->leftMode = 1;
    char t[300];
    std::snprintf(t, sizeof(t), tr(Str::MissionCreated), name.c_str());
    meldung(t, projekt);
    return true;
}

// Nach "Load mission...": Name, Ordner und Quelle merken - dann geht "Save
// mission..." auch fuer eine vorhandene Mission.
void missionAusGeladener(const Mission& m) {
    g_app->mission = App::MissionsProjekt{};
    g_app->mission.name = m.name;
    for (const Mission::Script& s : m.scripts) {
        const auto strich = s.ref.find('/');
        if (strich != std::string::npos) {
            g_app->mission.ordner = s.ref.substr(0, strich);
            break;
        }
    }
    if (g_app->mission.ordner.empty()) { g_app->mission.ordner = ordnerAusName(m.name); }
    g_app->mission.karte = m.mapFile;
    g_app->mission.archiv = g_app->missionArchive;
    g_app->mission.projektOrdner = missionStandardOrdner(m.name);
    g_app->mission.zielPk3 = standardZielPk3(m.name);
    g_app->missionAktiv = true;
    g_app->entUndo.clear();
    g_app->entRedo.clear();
    g_app->entitiesUngespeichert.erase(m.mapFile);
}

// --- Zusammenstellen und speichern ----------------------------------------------
namespace {
struct Zusammenstellung {
    MissionInhalt inhalt;
    std::vector<Pk3Datei> dateien;
    std::vector<MissionBefund> befunde;
    std::vector<int> reiter;   // die Reiter, deren Skripte dazugehoeren
};

Zusammenstellung zusammenstellen() {
    Zusammenstellung z;
    const App::MissionsProjekt& mp = g_app->mission;
    z.inhalt.name = mp.name;
    // Die Entities: gemerkt (bearbeitet / .ent), sonst die geladenen.
    if (const auto it = g_app->entitiesJeKarte.find(mp.karte); it != g_app->entitiesJeKarte.end()) {
        z.inhalt.entities = it->second;
    } else if (g_app->map.path == mp.karte) {
        z.inhalt.entities = g_app->map.entities;
    }
    // Die Geometrie unter dem Missionsnamen.
    (void)kartenBytes(mp.karte, z.inhalt.bsp);
    // Skripte: alles, worauf die Entities zeigen, und jeder offene Reiter im
    // Skriptordner der Mission.
    std::set<std::string> refs;
    for (const std::string& r : skriptVerweise(z.inhalt.entities)) { refs.insert(klein(r)); }
    for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
        const bool aktiv = static_cast<int>(i) == g_app->activeTab;
        const std::string r = aktiv ? skriptRef(g_app->path, g_app->skriptPfad)
                                    : skriptRef(g_app->tabs[i].path, g_app->tabs[i].skriptPfad);
        if (!r.empty() && klein(r).rfind(klein(mp.ordner) + "/", 0) == 0) { refs.insert(klein(r)); }
    }
    for (const std::string& r : refs) {
        const int reiter = reiterFuerRef(r);
        std::string text;
        std::string ref = r;
        if (reiter >= 0) {
            const bool aktiv = reiter == g_app->activeTab;
            const Document& doc = aktiv ? g_app->doc : g_app->tabs[static_cast<std::size_t>(reiter)].doc;
            text = writeScript(doc.script());
            ref = aktiv ? skriptRef(g_app->path, g_app->skriptPfad)
                        : skriptRef(g_app->tabs[static_cast<std::size_t>(reiter)].path,
                                    g_app->tabs[static_cast<std::size_t>(reiter)].skriptPfad);
            z.reiter.push_back(reiter);
        } else {
            const std::string datei = pfadVerbinden(mp.projektOrdner, "scripts/" + r + ".txt");
            if (gibtEs(datei)) {
                text = slurp(datei);
            } else if (!readFromArchives("scripts/" + r + ".txt", text)) {
                std::string ibi;
                if (readFromArchives("scripts/" + r + ".IBI", ibi)) {
                    std::vector<IbiBlock> bl;
                    std::vector<Diag> d;
                    Script sc;
                    if (readIbi(ibi, bl, d) && decompile(bl, g_app->db, sc, d)) { text = writeScript(sc); }
                }
            }
        }
        if (!text.empty()) { z.inhalt.skripte.push_back({ref, text}); }
    }
    // Eigene ROFF-Bahnen der Skripte (rc599) als scripts/<name>.rof.
    std::set<std::string> roffSchon;
    for (const auto& sk : z.inhalt.skripte) {
        Script sc;
        std::vector<Diag> d;
        if (!readScript(sk.text, sc, d)) { continue; }
        for (const RoffMarkeFund& m : alleRoffMarken(sc)) {
            const std::string pfad = roffPfad(m.anim.datei);
            if (!roffSchon.insert(klein(pfad)).second) { continue; }
            z.inhalt.weitere.push_back({pfad, schreibeRoff(roffAusAnim(m.anim))});
        }
    }
    // Weitere Dateien: was im Missionsordner liegt (Klaenge, .str, Bilder) ...
    std::set<std::string> schon;
    if (gibtEs(mp.projektOrdner)) {
        std::error_code ec;
        const std::filesystem::path wurzel = std::filesystem::u8path(mp.projektOrdner);
        for (auto it = std::filesystem::recursive_directory_iterator(wurzel, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) { continue; }
            const auto relU = std::filesystem::relative(it->path(), wurzel, ec).generic_u8string();
            const std::string rel(relU.begin(), relU.end());
            const std::string l = klein(rel);
            // Was hier ohnehin entsteht, nicht doppelt.
            if (l.rfind("maps/", 0) == 0 || l.rfind("scripts/", 0) == 0 || endetAuf(l, ".tmp")) { continue; }
            schon.insert(l);
            z.inhalt.weitere.push_back({rel, slurp(utf8(it->path()))});
        }
    }
    // ... und aus dem Archiv einer geladenen Mission, was zu ihr gehoert.
    if (!mp.archiv.empty()) {
        Pk3 arc;
        if (readPk3Directory(mp.archiv, arc)) {
            const std::string n = klein(mp.name);
            const std::string o = "/" + klein(mp.ordner) + "/";
            for (const Pk3Entry& e : arc.entries) {
                const std::string l = klein(e.name);
                if (l.empty() || l.back() == '/' || schon.count(l) != 0U || l.rfind("maps/", 0) == 0 ||
                    l.rfind("scripts/", 0) == 0) {
                    continue;
                }
                if (l.find(n) == std::string::npos && l.find(o) == std::string::npos) { continue; }
                std::string inhalt;
                if (readPk3File(arc, e, inhalt)) {
                    z.inhalt.weitere.push_back({e.name, inhalt});
                    schon.insert(l);
                }
            }
        }
    }
    z.dateien = missionDateien(z.inhalt, g_app->db, z.befunde);
    return z;
}

std::string befundText(const MissionBefund& b) {
    Str s = Str::MissionS001;
    if (b.code == "S002") { s = Str::MissionS002; }
    else if (b.code == "S003") { s = Str::MissionS003; }
    else if (b.code == "S004") { s = Str::MissionS004; }
    else if (b.code == "S005") { s = Str::MissionS005; }
    else if (b.code == "S006") { s = Str::MissionS006; }
    std::string arg = b.arg;
    if (b.code == "S002") {
        // Die Meldung des Uebersetzers in der eingestellten Sprache, wie bei
        // Compile (doCompile).
        const char* muster = nullptr;
        if (b.diagCode == "C001") { muster = tr(Str::CmsgC001); }
        else if (b.diagCode == "C002") { muster = tr(Str::CmsgC002); }
        else if (b.diagCode == "C003") { muster = tr(Str::CmsgC003); }
        else if (b.diagCode == "C004") { muster = tr(Str::CmsgC004); }
        else if (b.diagCode == "C005") { muster = tr(Str::CmsgC005); }
        else if (b.diagCode == "C006") { muster = tr(Str::CmsgC006); }
        char m[400];
        if (muster != nullptr) {
            std::snprintf(m, sizeof(m), muster, b.diagArg.c_str());
        } else {
            std::snprintf(m, sizeof(m), "%s", b.diagText.c_str());
        }
        arg += ": ";
        arg += m;
    }
    char t[700];
    std::snprintf(t, sizeof(t), tr(s), arg.c_str());
    return t;
}

std::string groesse(std::size_t n) {
    char t[32];
    if (n >= 1024U * 1024U) {
        std::snprintf(t, sizeof(t), "%.1f MB", static_cast<double>(n) / (1024.0 * 1024.0));
    } else {
        std::snprintf(t, sizeof(t), "%.0f KB", static_cast<double>(n) / 1024.0);
    }
    return t;
}
}  // namespace

bool missionSpeichernNach(const std::string& pk3, std::string* fehler) {
    const auto nein = [fehler](const std::string& t) {
        if (fehler != nullptr) { *fehler = t; }
        char m[700];
        std::snprintf(m, sizeof(m), tr(Str::MissionSaveFailed), t.c_str());
        meldung(m, std::string(), true);
        return false;
    };
    if (!g_app->missionAktiv) { return nein(tr(Str::MissionKeine)); }
    Zusammenstellung z = zusammenstellen();
    for (const MissionBefund& b : z.befunde) {
        if (b.fehler) { return nein(befundText(b)); }
    }
    const App::MissionsProjekt& mp = g_app->mission;
    // 1. Die Skripte der Reiter in den Missionsordner, als normale Dateien -
    //    danach sind die Reiter gespeichert und haben einen Pfad.
    for (const int r : z.reiter) {
        const bool aktiv = r == g_app->activeTab;
        const std::string ref = aktiv ? skriptRef(g_app->path, g_app->skriptPfad)
                                      : skriptRef(g_app->tabs[static_cast<std::size_t>(r)].path,
                                                  g_app->tabs[static_cast<std::size_t>(r)].skriptPfad);
        const std::string ziel = pfadVerbinden(mp.projektOrdner, "scripts/" + ref + ".txt");
        const std::string& jetzt = aktiv ? g_app->path : g_app->tabs[static_cast<std::size_t>(r)].path;
        // Ein Skript, das schon woanders auf der Platte liegt, bleibt dort -
        // es wird nur gesichert. Eines ohne Datei bekommt die im Ordner.
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(ziel).parent_path(), ec);
        if (!reiterSpeichernUnter(r, jetzt.empty() ? ziel : std::string())) {
            return nein(ziel);
        }
    }
    // 2. .ent und .IBI in den Ordner (die Quelle der Skripte steht schon da).
    for (const Pk3Datei& f : z.dateien) {
        const std::string l = klein(f.name);
        if (endetAuf(l, ".ent") || endetAuf(l, ".ibi")) {
            if (!schreibe(pfadVerbinden(mp.projektOrdner, f.name), f.inhalt)) { return nein(f.name); }
        }
    }
    // 3. Das Archiv.
    std::string err;
    if (!writePk3(pk3, z.dateien, &err)) { return nein(err); }
    g_app->mission.zielPk3 = pk3;
    g_app->entitiesUngespeichert.erase(mp.karte);
    missionsDateiSchreiben();
    char m[700];
    std::snprintf(m, sizeof(m), tr(Str::MissionSaved), pk3.c_str());
    meldung(m, directoryOf(pk3));
    diag::info(std::string("Mission gespeichert: ") + pk3 + " (" + std::to_string(z.dateien.size()) + " Dateien)");
    return true;
}

// --- Missionsordner: mission.txt, wieder oeffnen, zuletzt benutzt (rc586) -----
//
// Eine neue Mission lebt bis zum ersten "Save mission" nur im Missionsordner -
// nach einem Neustart kam man bisher nicht mehr an sie heran (Load mission
// liest .pk3). mission.txt merkt sich, woraus sie besteht; "Open mission
// folder..." und die Liste auf der Startflaeche lesen sie.
namespace {
std::string missionsDatei(const std::string& ordner) { return pfadVerbinden(ordner, "mission.txt"); }
}  // namespace

void missionsDateiSchreiben() {
    if (!g_app->missionAktiv || g_app->mission.projektOrdner.empty()) {
        return;
    }
    const App::MissionsProjekt& m = g_app->mission;
    std::string quelle = m.karte;
    if (const auto q = g_app->kartenQuelle.find(m.karte); q != g_app->kartenQuelle.end()) { quelle = q->second; }
    const std::string text = "# BehavEd-Remake mission\nname=" + m.name + "\nordner=" + m.ordner + "\nkarte=" + quelle +
                             "\narchiv=" + m.archiv + "\n";
    (void)schreibe(missionsDatei(m.projektOrdner), text);
    g_app->settings.noteRecentMission("ordner|" + m.projektOrdner);
}

std::vector<std::pair<std::string, std::string>> letzteMissionen() {
    std::vector<std::tuple<std::filesystem::file_time_type, std::string, std::string>> gefunden;
    const std::string wurzel = missionenWurzel();
    std::error_code ec;
    if (wurzel.empty() || !std::filesystem::is_directory(std::filesystem::u8path(wurzel), ec)) {
        return {};
    }
    for (const auto& d : std::filesystem::directory_iterator(std::filesystem::u8path(wurzel), ec)) {
        if (!d.is_directory(ec)) { continue; }
        const std::filesystem::path m = d.path() / "mission.txt";
        if (!std::filesystem::exists(m, ec)) { continue; }
        gefunden.emplace_back(std::filesystem::last_write_time(m, ec), utf8(d.path().filename()), utf8(d.path()));
    }
    std::sort(gefunden.begin(), gefunden.end(), [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });
    std::vector<std::pair<std::string, std::string>> aus;
    for (const auto& g : gefunden) {
        if (aus.size() >= 8) { break; }
        aus.emplace_back(std::get<1>(g), std::get<2>(g));
    }
    return aus;
}

bool missionAusOrdnerLaden(const std::string& ordner, std::string* fehler) {
    const auto nein = [fehler](const std::string& t) {
        if (fehler != nullptr) { *fehler = t; }
        meldung(t, std::string(), true);
        return false;
    };
    std::map<std::string, std::string> werte;
    {
        std::istringstream is(slurp(missionsDatei(ordner)));
        std::string z;
        while (std::getline(is, z)) {
            if (!z.empty() && z.back() == '\r') { z.pop_back(); }
            const auto gleich = z.find('=');
            if (z.empty() || z[0] == '#' || gleich == std::string::npos) { continue; }
            werte[z.substr(0, gleich)] = z.substr(gleich + 1);
        }
    }
    const std::string name = werte["name"];
    if (!gueltigerMissionsName(name)) {
        return nein(missionsDatei(ordner));
    }
    const std::string id = "maps/" + name + ".bsp";
    if (g_app->entitiesUngespeichert.count(id) != 0U) {
        if (!g_app->frageOffen) {
            char t[600];
            std::snprintf(t, sizeof(t), tr(Str::MissionUnsavedReload), name.c_str());
            frage(t, [ordner, id] {
                g_app->entitiesUngespeichert.erase(id);
                (void)missionAusOrdnerLaden(ordner, nullptr);
            }, [] {});
        }
        return false;
    }
    std::string pruefe;
    if (!readFromArchives(id, pruefe) && !werte["karte"].empty() && werte["karte"] != id) {
        g_app->kartenQuelle[id] = werte["karte"];
    }
    std::string bytes;
    if (!kartenBytes(id, bytes)) {
        return nein(werte["karte"].empty() ? id : werte["karte"]);
    }
    neuerReiter();
    vergissEntities(id);
    loadMapFromBytes(bytes, id);
    g_app->settings.mapPath.clear();
    std::vector<MapEntity> ents;
    parseEntities(slurp(pfadVerbinden(ordner, "maps/" + name + ".ent")), ents);
    if (!ents.empty()) {
        g_app->map.entities = std::move(ents);
        collectNames(g_app->map);
    }
    merkeEntities();
    g_app->mission = App::MissionsProjekt{};
    g_app->mission.name = name;
    g_app->mission.ordner = werte["ordner"].empty() ? ordnerAusName(name) : werte["ordner"];
    g_app->mission.karte = id;
    g_app->mission.archiv = werte["archiv"];
    g_app->mission.projektOrdner = ordner;
    g_app->mission.zielPk3 = standardZielPk3(name);
    g_app->missionAktiv = true;
    g_app->entUndo.clear();
    g_app->entRedo.clear();
    g_app->entitiesUngespeichert.erase(id);
    g_app->pickedEntity = -1;
    g_app->hiddenEntities.clear();
    // Die Skripte des Ordners, das Intro zuerst.
    std::vector<std::string> skripte;
    std::error_code ec;
    const std::filesystem::path sw = std::filesystem::u8path(pfadVerbinden(ordner, "scripts"));
    for (auto it = std::filesystem::recursive_directory_iterator(sw, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec) && endetAuf(utf8(it->path()), ".txt")) { skripte.push_back(utf8(it->path())); }
    }
    std::sort(skripte.begin(), skripte.end(), [](const std::string& a, const std::string& b) {
        const bool ia = klein(fileName(a)).find("intro") != std::string::npos;
        const bool ib = klein(fileName(b)).find("intro") != std::string::npos;
        return ia != ib ? ia : a < b;
    });
    int erstes = -1;
    for (const std::string& s : skripte) {
        ladeSkriptDatei(s);
        if (erstes < 0) { erstes = g_app->activeTab; }
    }
    if (erstes >= 0) { activateTab(erstes); }
    g_app->camTrackValid = false;
    g_app->mapDirty = true;
    g_app->leftMode = 1;
    g_app->settings.noteRecentMission("ordner|" + ordner);
    char t[300];
    std::snprintf(t, sizeof(t), tr(Str::MissionOpened), name.c_str());
    meldung(t, ordner);
    return true;
}

std::vector<LetzteMission> letzteMissionenAlle() {
    std::vector<LetzteMission> aus;
    std::set<std::string> schon;
    const auto dazu = [&](const std::string& eintrag, const std::string& name, const std::string& ort) {
        if (!schon.insert(klein(eintrag)).second || aus.size() >= Settings::kMaxRecentMissions) { return; }
        aus.push_back(LetzteMission{eintrag, name, ort});
    };
    for (const std::string& e : g_app->settings.recentMissions) {
        if (e.rfind("ordner|", 0) == 0) {
            const std::string p = e.substr(7);
            dazu(e, fileName(p), p);
        } else if (e.rfind("pk3|", 0) == 0) {
            const std::string rest = e.substr(4);
            const auto strich = rest.rfind('|');
            if (strich == std::string::npos) { continue; }
            dazu(e, rest.substr(strich + 1), fileName(rest.substr(0, strich)));
        }
    }
    for (const auto& [name, pfad] : letzteMissionen()) { dazu("ordner|" + pfad, name, pfad); }
    return aus;
}

void letzteMissionOeffnen(const std::string& eintrag) {
    const auto weg = [&eintrag](const std::string& was) {
        auto& v = g_app->settings.recentMissions;
        v.erase(std::remove(v.begin(), v.end(), eintrag), v.end());
        char t[700];
        std::snprintf(t, sizeof(t), tr(Str::RecentMissionGone), was.c_str());
        meldung(t, std::string(), true);
    };
    if (eintrag.rfind("ordner|", 0) == 0) {
        const std::string ordner = eintrag.substr(7);
        if (!gibtEs(missionsDatei(ordner))) {
            weg(ordner);
            return;
        }
        (void)missionAusOrdnerLaden(ordner, nullptr);
        return;
    }
    if (eintrag.rfind("pk3|", 0) == 0) {
        const std::string rest = eintrag.substr(4);
        const auto strich = rest.rfind('|');
        if (strich == std::string::npos) { return; }
        const std::string archiv = rest.substr(0, strich);
        if (!gibtEs(archiv)) {
            weg(archiv);
            return;
        }
        missionenAusArchiv(archiv, rest.substr(strich + 1));
    }
}

void missionsOrdnerWaehlen() {
    const std::string p = platform::pickFolder(tr(Str::MissionOpenFolder), missionenWurzel());
    if (!p.empty()) { (void)missionAusOrdnerLaden(p, nullptr); }
}

// --- Die Startflaeche: ohne Karte ----------------------------------------------
//
// Vorher stand im leeren Map-Tab nur "No map loaded". Wie Unreals
// Projektbrowser: drei grosse Wege hinein und die zuletzt bearbeiteten
// Missionen.
void drawStartflaeche(float breite, float hoehe) {
    ImGui::BeginChild("##startflaeche", ImVec2{breite, hoehe}, ImGuiChildFlags_Borders);
    const float fs = ImGui::GetFontSize();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float spalte = std::min(ImGui::GetContentRegionAvail().x - fs * 2.0F, fs * 46.0F);
    const float links = std::max(fs, (ImGui::GetContentRegionAvail().x - spalte) * 0.5F);
    ImGui::SetCursorPosY(std::max(fs, hoehe * 0.10F));
    ImGui::SetCursorPosX(links);
    ImGui::PushFont(nullptr, fs * 1.6F);
    ImGui::TextUnformatted(tr(Str::StartTitle));
    ImGui::PopFont();
    ImGui::SetCursorPosX(links);
    ImGui::PushTextWrapPos(links + spalte);
    ImGui::TextDisabled("%s", tr(Str::StartSub));
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2{0.0F, fs * 0.8F});

    // Drei Karten
    struct Karte {
        Str titel;
        Str unter;
        UiIcon icon;
        int wer;
    };
    const Karte karten[] = {
        {Str::StartCardNew, Str::StartCardNewSub, UiIcon::Neu, 0},
        {Str::StartCardLoad, Str::StartCardLoadSub, UiIcon::Paket, 1},
        {Str::StartCardMap, Str::StartCardMapSub, UiIcon::Karte, 2},
    };
    const float kb = (spalte - st.ItemSpacing.x * 2.0F) / 3.0F;
    const float kh = fs * 8.0F;
    ImGui::SetCursorPosX(links);
    for (int k = 0; k < 3; ++k) {
        if (k > 0) { ImGui::SameLine(); }
        const Karte& c = karten[k];
        const bool geklickt = ImGui::InvisibleButton(tr(c.titel), ImVec2{kb, kh});
        const bool ueber = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        dl->AddRectFilled(a, b, ImGui::GetColorU32(ueber ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), fs * 0.5F);
        const ImVec4 farbe = uiIconFarbe(c.icon);
        if (ueber) { dl->AddRect(a, b, ImGui::GetColorU32(farbe), fs * 0.5F, 0, 2.0F); }
        const float ig = fs * 2.4F;
        uiIcon(dl, c.icon, ImVec2{std::floor(a.x + fs), std::floor(a.y + fs)}, ig, ImGui::GetColorU32(farbe));
        dl->AddText(ImVec2{a.x + fs, a.y + fs * 1.6F + ig}, ImGui::GetColorU32(ImGuiCol_Text), tr(c.titel));
        dl->AddText(nullptr, 0.0F, ImVec2{a.x + fs, a.y + fs * 2.9F + ig}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    tr(c.unter), nullptr, kb - fs * 2.0F);
        if (geklickt) {
            if (c.wer == 0) { g_app->neueMissionOffen = true; }
            if (c.wer == 1) { loadMissionArchive(); }
            if (c.wer == 2) { doLoadMap(); }
        }
    }
    ImGui::Dummy(ImVec2{0.0F, fs * 1.0F});

    // Zuletzt bearbeitet
    ImGui::SetCursorPosX(links);
    ImGui::TextUnformatted(tr(Str::StartRecent));
    ImGui::SameLine();
    if (iconKnopf(tr(Str::MissionOpenFolder), UiIcon::Oeffnen, tr(Str::MissionOpenFolderHint))) {
        missionsOrdnerWaehlen();
    }
    static std::vector<LetzteMission> liste;
    static double gelesen = -100.0;
    if (ImGui::GetTime() - gelesen > 2.0) {   // nicht in jedem Bild die Platte fragen
        liste = letzteMissionenAlle();
        gelesen = ImGui::GetTime();
    }
    if (liste.empty()) {
        ImGui::SetCursorPosX(links);
        ImGui::TextDisabled("%s", tr(Str::StartNoRecent));
    }
    for (const LetzteMission& lm : liste) {
        const std::string& name = lm.name;
        const std::string& pfad = lm.ort;
        ImGui::SetCursorPosX(links);
        ImGui::PushID(lm.eintrag.c_str());
        const ImVec2 q = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##letzte", false, ImGuiSelectableFlags_None, ImVec2{spalte, ImGui::GetFrameHeight()})) {
            const std::string e = lm.eintrag;
            letzteMissionOeffnen(e);
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float y = q.y + (ImGui::GetFrameHeight() - fs) * 0.5F;
        uiIcon(dl, UiIcon::Mission, ImVec2{std::floor(q.x + 4.0F), std::floor(y)}, fs, ImGui::GetColorU32(uiIconFarbe(UiIcon::Mission)));
        dl->AddText(ImVec2{q.x + fs * 1.8F, y}, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
        dl->AddText(ImVec2{q.x + fs * 1.8F + ImGui::CalcTextSize(name.c_str()).x + fs, y},
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), pfad.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// --- Kurze Einblendungen unten rechts -------------------------------------------
void meldung(const std::string& text, const std::string& ordner, bool warnung) {
    App::Einblendung e;
    e.text = text;
    e.ordner = ordner;
    e.warnung = warnung;
    e.bis = ImGui::GetTime() + (warnung ? 8.0 : 5.0);
    g_app->einblendungen.push_back(e);
    if (g_app->einblendungen.size() > 3) { g_app->einblendungen.erase(g_app->einblendungen.begin()); }
    addStatus(text);
}

void drawEinblendungen() {
    const double jetzt = ImGui::GetTime();
    auto& v = g_app->einblendungen;
    v.erase(std::remove_if(v.begin(), v.end(), [jetzt](const App::Einblendung& e) { return e.bis < jetzt; }), v.end());
    if (v.empty()) { return; }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float fs = ImGui::GetFontSize();
    float unten = vp->WorkPos.y + vp->WorkSize.y - fs * 2.6F;
    for (std::size_t k = v.size(); k-- > 0;) {
        App::Einblendung& e = v[k];
        const float alpha = static_cast<float>(std::clamp((e.bis - jetzt) / 0.6, 0.0, 1.0));
        ImGui::SetNextWindowPos(ImVec2{vp->WorkPos.x + vp->WorkSize.x - fs, unten}, ImGuiCond_Always, ImVec2{1.0F, 1.0F});
        ImGui::SetNextWindowBgAlpha(0.95F * alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, fs * 0.5F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{fs * 0.8F, fs * 0.6F});
        char id[32];
        std::snprintf(id, sizeof(id), "##einblendung%zu", k);
        ImGui::Begin(id, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
        const UiIcon icon = e.warnung ? UiIcon::Warnung : UiIcon::Haken;
        const ImVec2 q = ImGui::GetCursorScreenPos();
        uiIcon(ImGui::GetWindowDrawList(), icon, ImVec2{std::floor(q.x), std::floor(q.y)}, fs,
               ImGui::GetColorU32(uiIconFarbe(icon)));
        ImGui::Dummy(ImVec2{fs, fs});
        ImGui::SameLine();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fs * 26.0F);
        ImGui::TextUnformatted(e.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SameLine();
        if (iconKnopf(tr(Str::ToastClose), UiIcon::Schliessen, nullptr)) { e.bis = 0.0; }
        if (!e.ordner.empty()) {
            if (iconTextKnopf(tr(Str::ToastExplorer), UiIcon::Explorer, nullptr)) {
                // Im Selbsttest kein Explorer-Fenster auf dem Bildschirm des Anwenders.
                if (std::getenv("BHED_EDITORTEST") != nullptr) {
                    diag::detail("Einblendung: Explorer fuer " + e.ordner + " (Selbsttest, nicht geoeffnet)");
                } else {
                    platform::openInExplorer(e.ordner);
                }
            }
        }
        unten = ImGui::GetWindowPos().y - fs * 0.5F;
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
}

// --- Die beiden Fenster ---------------------------------------------------------
void drawNeueMission() {
    if (!g_app->neueMissionOffen) { return; }
    static char name[64] = "";
    static char ordner[64] = "";
    static char filter[64] = "";
    static std::string karte;
    static std::string fehler;
    static bool ordnerVonHand = false;
    if (ImGui::IsPopupOpen("##neuemission") == false) {
        ImGui::OpenPopup("##neuemission");
        karte = g_app->map.empty() ? std::string() : g_app->map.path;
        const std::string n = standardMissionsName(karte.empty() ? std::string("my_mission") : karte);
        std::snprintf(name, sizeof(name), "%s", n.c_str());
        std::snprintf(ordner, sizeof(ordner), "%s", ordnerAusName(n).c_str());
        filter[0] = '\0';
        fehler.clear();
        ordnerVonHand = false;
    }
    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 34.0F, 0.0F}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##neuemission", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { return; }
    ImGui::TextUnformatted(tr(Str::MissionNewTitle));
    ImGui::Separator();
    ImGui::TextDisabled("%s", tr(Str::MissionMap));
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##kartensuche", tr(Str::MissionMapSearch), filter, sizeof(filter));
    ImGui::BeginChild("##karten", ImVec2{-1.0F, ImGui::GetFontSize() * 12.0F}, ImGuiChildFlags_Borders);
    const std::string f = klein(filter);
    if (!g_app->map.empty()) {
        char l[300];
        std::snprintf(l, sizeof(l), tr(Str::MissionMapCurrent), g_app->map.path.c_str());
        if (ImGui::Selectable(l, karte == g_app->map.path)) {
            karte = g_app->map.path;
            if (!ordnerVonHand) {
                const std::string n = standardMissionsName(karte);
                std::snprintf(name, sizeof(name), "%s", n.c_str());
                std::snprintf(ordner, sizeof(ordner), "%s", ordnerAusName(n).c_str());
            }
        }
    }
    for (const std::string& k : alleKarten()) {
        if (!f.empty() && klein(k).find(f) == std::string::npos) { continue; }
        if (ImGui::Selectable(k.c_str(), karte == k)) {
            karte = k;
            if (!ordnerVonHand) {
                const std::string n = standardMissionsName(karte);
                std::snprintf(name, sizeof(name), "%s", n.c_str());
                std::snprintf(ordner, sizeof(ordner), "%s", ordnerAusName(n).c_str());
            }
        }
    }
    ImGui::EndChild();
    ImGui::TextDisabled("%s", tr(Str::MissionName));
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputText("##missionsname", name, sizeof(name)) && !ordnerVonHand) {
        std::snprintf(ordner, sizeof(ordner), "%s", ordnerAusName(name).c_str());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::MissionNameHint)); }
    ImGui::TextDisabled("%s", tr(Str::MissionFolder));
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputText("##skriptordner", ordner, sizeof(ordner))) { ordnerVonHand = true; }
    {
        char t[700];
        std::snprintf(t, sizeof(t), tr(Str::MissionProject), missionStandardOrdner(name).c_str());
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 33.0F);
        ImGui::TextDisabled("%s", t);
        ImGui::TextWrapped("%s", tr(Str::MissionNewHint));
        ImGui::PopTextWrapPos();
    }
    if (!fehler.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 33.0F);
        ImGui::TextColored(ImVec4{1.0F, 0.45F, 0.35F, 1.0F}, "%s", fehler.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();
    if (ImGui::Button(tr(Str::MissionCreate))) {
        const std::string k = (karte == g_app->map.path) ? std::string() : karte;
        if (karte.empty()) {
            fehler = tr(Str::MissionNoMap);
        } else if (neueMissionAnlegen(k, name, ordner, &fehler)) {
            g_app->neueMissionOffen = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::MissionCancel)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        g_app->neueMissionOffen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawMissionSpeichern() {
    if (!g_app->missionSpeichernOffen) { return; }
    static Zusammenstellung z;
    static char ziel[512] = "";
    static std::string fehler;
    if (!ImGui::IsPopupOpen("##missionspeichern")) {
        ImGui::OpenPopup("##missionspeichern");
        z = g_app->missionAktiv ? zusammenstellen() : Zusammenstellung{};
        std::snprintf(ziel, sizeof(ziel), "%s", g_app->mission.zielPk3.c_str());
        fehler.clear();
    }
    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 40.0F, 0.0F}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##missionspeichern", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { return; }
    const float wrap = ImGui::GetFontSize() * 39.0F;
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(tr(Str::MissionSaveTitle));
    ImGui::Separator();
    if (!g_app->missionAktiv) {
        ImGui::TextWrapped("%s", tr(Str::MissionKeine));
    } else {
        ImGui::Text("%s  (%s)", g_app->mission.name.c_str(), g_app->mission.ordner.c_str());
        char t[700];
        std::snprintf(t, sizeof(t), tr(Str::MissionProject), g_app->mission.projektOrdner.c_str());
        ImGui::TextDisabled("%s", t);
        ImGui::TextDisabled("%s", tr(Str::MissionTarget));
        ImGui::SetNextItemWidth(wrap - ImGui::CalcTextSize(tr(Str::MissionChoose)).x - ImGui::GetFontSize() * 2.0F);
        ImGui::InputText("##zielpk3", ziel, sizeof(ziel));
        ImGui::SameLine();
        if (ImGui::Button(tr(Str::MissionChoose))) {
            const std::string p = platform::saveFileDialog(tr(Str::MissionTarget), "Game archives (*.pk3)|*.pk3",
                                                           directoryOf(ziel), fileName(ziel));
            if (!p.empty()) { std::snprintf(ziel, sizeof(ziel), "%s", p.c_str()); }
        }
        std::size_t summe = 0;
        for (const Pk3Datei& d : z.dateien) { summe += d.inhalt.size(); }
        char zeile[120];
        std::snprintf(zeile, sizeof(zeile), tr(Str::MissionFiles), static_cast<int>(z.dateien.size()),
                      groesse(summe).c_str());
        ImGui::TextDisabled("%s", zeile);
        ImGui::BeginChild("##missionsdateien", ImVec2{wrap, ImGui::GetFontSize() * 8.0F}, ImGuiChildFlags_Borders);
        for (const Pk3Datei& d : z.dateien) {
            ImGui::TextUnformatted(d.name.c_str());
            ImGui::SameLine(wrap - ImGui::GetFontSize() * 5.0F);
            ImGui::TextDisabled("%s", groesse(d.inhalt.size()).c_str());
        }
        ImGui::EndChild();
        bool fehlerDa = false;
        for (const MissionBefund& b : z.befunde) {
            fehlerDa = fehlerDa || b.fehler;
            ImGui::TextColored(b.fehler ? ImVec4{1.0F, 0.45F, 0.35F, 1.0F} : ImVec4{1.0F, 0.8F, 0.35F, 1.0F}, "%s",
                               befundText(b).c_str());
        }
        if (fehlerDa) { ImGui::TextWrapped("%s", tr(Str::MissionHasErrors)); }
        if (!fehler.empty()) { ImGui::TextColored(ImVec4{1.0F, 0.45F, 0.35F, 1.0F}, "%s", fehler.c_str()); }
        ImGui::Separator();
        ImGui::BeginDisabled(fehlerDa || ziel[0] == '\0');
        if (ImGui::Button(tr(Str::MissionSaveNow))) {
            if (missionSpeichernNach(ziel, &fehler)) {
                g_app->missionSpeichernOffen = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (ImGui::Button(tr(Str::MissionCancel)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        g_app->missionSpeichernOffen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
}

// Beim Beenden: ungespeicherte Entities? Dann fragen (confirmQuit).
// true: weiter beenden; false: es wurde gefragt, die Antwort kommt.
bool missionBeendenFrage() {
    if (g_app->entitiesUngespeichert.empty() || g_app->entFrageBeantwortet) { return true; }
    if (g_app->frageOffen) { return false; }
    char t[700];
    std::snprintf(t, sizeof(t), tr(Str::MissionUnsavedQuit), g_app->entitiesUngespeichert.begin()->c_str());
    frage(t,
          [] {
              g_app->entFrageBeantwortet = true;
              if (confirmQuit()) {
                  g_app->quitConfirmed = true;
                  g_app->wantQuit = true;
              }
          },
          [] {
              updater::neustartVergessen();
              g_app->missionSpeichernOffen = g_app->missionAktiv;
          });
    return false;
}

// --- Fuer andere Teile der Oberflaeche (Wegpunkte, Draufsicht) ----------------

bool kartenPunktAufSchirm(const float w[3], ImVec2& out) {
    const Proj p = projektion();
    return aufSchirm(p, w, out);
}

bool kartenStrahlUnterMaus(float sx, float sy, TraceTreffer& t) {
    float a[3];
    float b[3];
    strahlDurch(sx, sy, a, b);
    return spur(a, b, t);
}

void entitiesGeaendert(std::vector<MapEntity> vorher) { aenderung(std::move(vorher)); }

std::string freierEntityName(const std::string& prefix) { return freierName(prefix); }

int entityPlatzieren(const std::string& klasse, float sx, float sy) {
    float a[3];
    float b[3];
    strahlDurch(sx, sy, a, b);
    float ort[3];
    TraceTreffer t;
    if (spur(a, b, t)) {
        aufFlaeche(klasse, t, ort);
    } else {
        for (int k = 0; k < 3; ++k) { ort[k] = a[k] + (b[k] - a[k]) * (128.0F / 8192.0F); }
    }
    if (g_app->rasterSchritt > 0) {
        const float r = static_cast<float>(g_app->rasterSchritt);
        ort[0] = std::round(ort[0] / r) * r;
        ort[1] = std::round(ort[1] / r) * r;
    }
    const int neu = neueEntityAn(klasse, ort);
    g_app->entAuswahl.clear();
    return neu;
}

}  // namespace bhed::gui
