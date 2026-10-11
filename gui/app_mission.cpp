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
#include "bhed/scene.h"

#include <algorithm>
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
}

// --- Aendern mit Rueckgaengig -----------------------------------------------
void nachAenderung() {
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
    aenderung(std::move(vorher));
    return true;
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
    if (g_app->pickedEntity >= static_cast<int>(g_app->map.entities.size())) { g_app->pickedEntity = -1; }
    nachAenderung();
    return true;
}

bool entityWiederholen() {
    if (g_app->entUndoKarte != g_app->map.path || g_app->entRedo.empty()) { return false; }
    g_app->entUndo.push_back(g_app->map.entities);
    g_app->map.entities = std::move(g_app->entRedo.back());
    g_app->entRedo.pop_back();
    g_app->hiddenEntities.clear();
    if (g_app->pickedEntity >= static_cast<int>(g_app->map.entities.size())) { g_app->pickedEntity = -1; }
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
    return g_app->leftMode == 1 && (g_app->entFokusBild || g_app->entFokusListe) && !ImGui::GetIO().WantTextInput;
}

bool entityUndoMoeglich() { return g_app->entUndoKarte == g_app->map.path && !g_app->entUndo.empty(); }
bool entityRedoMoeglich() { return g_app->entUndoKarte == g_app->map.path && !g_app->entRedo.empty(); }

// --- Ziehen in der Ansicht --------------------------------------------------
//
// Gerufen aus drawMapView, nachdem das Bild steht (kartenAnsicht gilt). Wer
// die gewaehlte Entity (oder die Figur ihres Spawners) anfasst und zieht,
// schiebt sie ueber die Flaeche unter der Maus - wie "Drag to surface" in
// Unreal. Ein Klick allein waehlt weiter nur.
void entityZiehen(float rasterSkal) {
    App::EntZug& z = g_app->entZug;
    const ImGuiIO& io = ImGui::GetIO();
    const int ziel = inspektorZiel();
    const bool imBild = io.MousePos.x >= g_app->kartenAnsicht[0] && io.MousePos.x < g_app->kartenAnsicht[2] &&
                        io.MousePos.y >= g_app->kartenAnsicht[1] && io.MousePos.y < g_app->kartenAnsicht[3];
    g_app->entFokusBild = imBild && ImGui::IsWindowHovered();
    if (!z.kandidat && ziel >= 0 && imBild && ImGui::IsWindowHovered() && g_app->gizmoAxis < 0 &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !g_app->camViewActive) {
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
        if (getroffen) {
            z.kandidat = true;
            z.aktiv = false;
            z.index = ziel;
            z.vorher = g_app->map.entities;
        }
    }
    if (!z.kandidat) {
        return;
    }
    if (z.index < 0 || static_cast<std::size_t>(z.index) >= g_app->map.entities.size()) {
        z = App::EntZug{};
        return;
    }
    if (!z.aktiv && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        z.aktiv = true;
    }
    MapEntity& e = g_app->map.entities[static_cast<std::size_t>(z.index)];
    if (z.aktiv && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        float a[3];
        float b[3];
        strahlDurch(io.MousePos.x, io.MousePos.y, a, b);
        TraceTreffer t;
        if (spur(a, b, t)) {
            float o[3];
            aufFlaeche(e.classname, t, o);
            setKey(e, "origin", ortText(o));
            g_app->mapDirty = true;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (z.aktiv && z.vorher[static_cast<std::size_t>(z.index)].keys != e.keys) {
            aenderung(std::move(z.vorher));
            addStatus(tr(Str::EntMoved));
        }
        z = App::EntZug{};
    }
}

// --- Der Inspektor -------------------------------------------------------------
void drawEntityInspector(float hoehe) {
    ImGui::BeginChild("entinspektor", ImVec2{-1.0F, hoehe}, ImGuiChildFlags_Borders);
    g_app->entFokusListe = g_app->entFokusListe || ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const int i = inspektorZiel();
    if (i < 0) {
        ImGui::TextWrapped("%s", tr(g_app->map.entities.empty() ? Str::MissionKeine : Str::EntPickHint));
        ImGui::EndChild();
        return;
    }
    // Kopie: Felder unten aendern map.entities, die Anzeige bleibt fuer
    // dieses Bild beim alten Stand.
    const MapEntity e = g_app->map.entities[static_cast<std::size_t>(i)];
    const ent::Def* d = ent::def(e.classname);
    ImGui::PushID(i);

    // Kopf: Klasse in ihrer Farbe, Erklaerung als Hinweis.
    const ImVec4 farbe = d != nullptr ? ImVec4{0.45F + d->farbe[0] * 0.55F, 0.45F + d->farbe[1] * 0.55F,
                                              0.45F + d->farbe[2] * 0.55F, 1.0F}
                                      : ImGui::GetStyleColorVec4(ImGuiCol_Text);
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

    const float breite = ImGui::GetContentRegionAvail().x;
    const auto beschriftung = [](const char* t, const char* hilfe) {
        ImGui::TextDisabled("%s", t);
        if (hilfe != nullptr && hilfe[0] != '\0' && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0F);
            ImGui::TextUnformatted(hilfe);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };
    std::set<std::string> gezeigt = {"classname", "origin", "angle", "spawnflags"};

    // Eine Feldzeile: Beschriftung mit Erklaerung, Wert, bei Skripten "Open",
    // beim NPC-Typ die Auswahl.
    const auto zeigeFeld = [&](const std::string& name, const std::string& hilfe) {
        ImGui::PushID(name.c_str());
        const std::string* wert = e.find(name);
        beschriftung(name.c_str(), hilfe.c_str());
        const bool skript = istSkriptFeld(name);
        const bool npcTyp = klein(name) == "npc_type";
        const float knopf = (skript || npcTyp) ? ImGui::CalcTextSize(tr(Str::EntOpenScript)).x +
                                                     ImGui::GetStyle().FramePadding.x * 2.0F +
                                                     ImGui::GetStyle().ItemSpacing.x
                                               : 0.0F;
        const float loeschen = g_app->entRawAnsicht ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0F;
        ImGui::SetNextItemWidth(std::max(breite - knopf - loeschen, ImGui::GetFontSize() * 4.0F));
        std::string neu;
        if (textFeld("##wert", wert != nullptr ? *wert : std::string(), neu)) {
            entityKeySetzen(i, name, neu);
        }
        if (npcTyp && (wert == nullptr || wert->empty())) {
            ImGui::SameLine(0.0F, 0.0F);
        }
        if (skript) {
            ImGui::SameLine();
            const bool vorhanden = wert != nullptr && !wert->empty();
            ImGui::BeginDisabled(!vorhanden);
            if (ImGui::SmallButton(tr(Str::EntOpenScript)) && vorhanden) { skriptOeffnen(*wert, true); }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::EntOpenScriptHint)); }
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
            if (wert == nullptr || wert->empty()) {
                ImGui::TextColored(ImVec4{1.0F, 0.75F, 0.3F, 1.0F}, "%s", tr(Str::EntNpcTypeMissing));
            }
        }
        if (g_app->entRawAnsicht) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr(Str::EntRemoveKey))) { entityKeySetzen(i, name, std::string()); }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::EntRemoveKeyHint)); }
        }
        ImGui::PopID();
    };

    // Name - unter ihm sprechen die Skripte die Entity an.
    {
        const char* nameKey = istNpcKlasse(e.classname) ? "NPC_targetname" : "targetname";
        gezeigt.insert(klein(nameKey));
        beschriftung(tr(Str::EntName), nameKey);
        const std::string* n = e.find(nameKey);
        std::string neu;
        ImGui::SetNextItemWidth(breite);
        if (textFeld("##name", n != nullptr ? *n : std::string(), neu)) {
            entityKeySetzen(i, nameKey, neu);
        }
    }

    // Das Feld, ohne das die Entity nichts tut, gleich unter dem Namen - der
    // NPC-Typ einer Figur stand sonst unter allen Spawnflags (Foto 11.10.).
    for (const char* w : wichtigeFelder(e.classname)) {
        std::string hilfe;
        for (std::size_t k = 0; d != nullptr && k < d->feldAnzahl; ++k) {
            if (klein(d->felder[k].name) == klein(w)) { hilfe = entText(d->felder[k].text); }
        }
        gezeigt.insert(klein(w));
        zeigeFeld(w, hilfe);
    }

    // Ort - live beim Ziehen, ein Rueckgaengig-Schritt am Ende.
    float o[3] = {0.0F, 0.0F, 0.0F};
    const bool hatOrt = entityOrt(e, o);
    if (hatOrt || (d != nullptr && !d->brush)) {
        static std::vector<MapEntity> vorOrt;
        beschriftung(tr(Str::EntOrigin), "origin");
        ImGui::SetNextItemWidth(breite);
        if (ImGui::DragFloat3("##ort", o, 1.0F, -65536.0F, 65536.0F, "%.0f")) {
            setKey(g_app->map.entities[static_cast<std::size_t>(i)], "origin", ortText(o));
            g_app->mapDirty = true;
        }
        if (ImGui::IsItemActivated()) { vorOrt = g_app->map.entities; }
        if (ImGui::IsItemDeactivatedAfterEdit() && vorOrt.size() == g_app->map.entities.size()) {
            aenderung(std::move(vorOrt));
            vorOrt.clear();
        }
        // Blickrichtung
        float winkel = e.find("angle") != nullptr ? std::strtof(e.find("angle")->c_str(), nullptr) : 0.0F;
        static std::vector<MapEntity> vorWinkel;
        beschriftung(tr(Str::EntAngle), "angle");
        ImGui::SetNextItemWidth(breite);
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
        // Boden
        if (ImGui::Button(tr(Str::EntDropFloor))) {
            if (!entityAufBoden(i)) { addStatus(tr(Str::EntNoFloor)); }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::EntDropFloorHint)); }
        ImGui::SameLine();
        if (ImGui::Button(tr(Str::EntityFlyTo))) { zurEntityFliegen(static_cast<std::size_t>(i)); }
        float abstand = 0.0F;
        if (hatOrt && bodenAbstand(e, abstand)) {
            if (abstand < -0.5F) {
                ImGui::TextColored(ImVec4{1.0F, 0.45F, 0.35F, 1.0F}, tr(Str::EntBelowFloor), static_cast<double>(-abstand));
            } else {
                ImGui::TextDisabled(tr(Str::EntAboveFloor), static_cast<double>(abstand));
            }
        } else if (hatOrt) {
            ImGui::TextDisabled("%s", tr(Str::EntNoFloor));
        }
    }

    // Spawnflags als Haekchen mit Namen.
    if (d != nullptr && d->flagAnzahl > 0) {
        ImGui::Separator();
        beschriftung(tr(Str::EntFlags), "spawnflags");
        const int flags = e.find("spawnflags") != nullptr ? std::atoi(e.find("spawnflags")->c_str()) : 0;
        int bekannt = 0;
        // Zwei Spalten, wenn der laengste Name hineinpasst - die Liste ist
        // bei Figuren sechs Zeilen lang.
        float laengste = 0.0F;
        for (std::size_t k = 0; k < d->flagAnzahl; ++k) { laengste = std::max(laengste, ImGui::CalcTextSize(d->flags[k].name).x); }
        const bool zweiSpalten = d->flagAnzahl > 2 &&
                                 laengste + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x * 2.0F < breite * 0.5F;
        const bool tabelle = zweiSpalten && ImGui::BeginTable("##flags", 2);
        for (std::size_t k = 0; k < d->flagAnzahl; ++k) {
            const ent::Flag& f = d->flags[k];
            bekannt |= f.bit;
            if (tabelle) { ImGui::TableNextColumn(); }
            bool an = (flags & f.bit) != 0;
            char label[96];
            std::snprintf(label, sizeof(label), "%s##f%d", f.name, f.bit);
            if (ImGui::Checkbox(label, &an)) {
                const int neu = an ? (flags | f.bit) : (flags & ~f.bit);
                entityKeySetzen(i, "spawnflags", neu == 0 ? std::string() : std::to_string(neu));
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0F);
                ImGui::Text("%s (%d)", f.name, f.bit);
                if (entText(f.text)[0] != '\0') { ImGui::TextUnformatted(entText(f.text)); }
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        if (tabelle) { ImGui::EndTable(); }
        if ((flags & ~bekannt) != 0) {
            ImGui::TextDisabled(tr(Str::EntOtherBits), flags & ~bekannt);
        }
    }

    // Felder: erst die aus der Beschreibung, dann die uebrigen der Entity.
    ImGui::Separator();
    ImGui::Checkbox(tr(Str::EntRaw), &g_app->entRawAnsicht);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::EntRawHint)); }
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
    for (const auto& [name, hilfe] : felder) { zeigeFeld(name, hilfe); }
    // Ein Feld dazu.
    {
        static char neuerKey[48] = "";
        ImGui::SetNextItemWidth(breite * 0.55F);
        const bool enter = ImGui::InputTextWithHint("##neuerkey", tr(Str::EntKeyName), neuerKey, sizeof(neuerKey),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::SmallButton(tr(Str::EntAddKey)) || enter) && neuerKey[0] != '\0') {
            // Ohne Wert gibt es den Schluessel nicht - "1" als Anfang, sichtbar und aenderbar.
            if (e.find(neuerKey) == nullptr) { entityKeySetzen(i, neuerKey, "1"); }
            neuerKey[0] = '\0';
        }
    }

    ImGui::Separator();
    if (ImGui::Button(tr(Str::EntDuplicate))) { entityDuplizieren(i); }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::EntDelete))) { entityLoeschen(i); }
    ImGui::BeginDisabled(!entityUndoMoeglich());
    if (ImGui::SmallButton(tr(Str::EditUndo))) { entityRueckgaengig(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!entityRedoMoeglich());
    if (ImGui::SmallButton(tr(Str::EditRedo))) { entityWiederholen(); }
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", tr(Str::EntDragHint));
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

    ladeSkriptDatei(intro);
    g_app->camTrackValid = false;
    g_app->mapDirty = true;
    g_app->leftMode = 1;
    char t[300];
    std::snprintf(t, sizeof(t), tr(Str::MissionCreated), name.c_str());
    addStatus(t);
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
        addStatus(m);
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
    char m[700];
    std::snprintf(m, sizeof(m), tr(Str::MissionSaved), pk3.c_str());
    addStatus(m);
    diag::info(std::string("Mission gespeichert: ") + pk3 + " (" + std::to_string(z.dateien.size()) + " Dateien)");
    return true;
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

}  // namespace bhed::gui
