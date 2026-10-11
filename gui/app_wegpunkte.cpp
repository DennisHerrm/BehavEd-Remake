// app_wegpunkte.cpp - das Wegnetz der NPCs in der Kartenansicht
//
// Gewuenscht (11.10.2026): "npc waypoints erstellen ... bzw. er die automatisch
// erstellt". Der Teil ohne Oberflaeche steht in bhed/wegpunkte.h (dort auch,
// wie die Engine das Netz baut). Hier:
//
//   - das Netz in der Ansicht: Wegpunkte, Ziele, Kampfpunkte, Kanten (gruen
//     vom Designer, blau automatisch, orange Sprung), Befunde rot umrandet
//   - Setzen: Klick auf den Boden setzt einen Wegpunkt und haengt ihn an den
//     vorigen (eine Kette, wie die Designer es tun); Umschalt beginnt neu
//   - Verbinden: erst einen, dann einen zweiten Punkt anklicken - target in
//     einem freien Feld (Strg: targetJump); schon verbunden = trennen
//   - Erzeugen: vom Spielerstart, den Figuren und Zielen aus ueber den Boden
//   - Pruefen: im Festen, schwebend, allein, target fehlt
#include "app_internal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace bhed::gui {
namespace {

constexpr const char* kErzeugtPrefix = "wpauto_";
const char* const kZielFelder[4] = {"target", "target2", "target3", "target4"};

bool wegKlasse(const MapEntity& e) { return istWegKlasse(e.classname); }

std::size_t pruefsumme() {
    std::size_t h = std::hash<std::string>{}(g_app->map.path);
    for (const MapEntity& e : g_app->map.entities) {
        if (!wegKlasse(e)) { continue; }
        for (const auto& [k, v] : e.keys) {
            h = h * 1099511628211ULL ^ std::hash<std::string>{}(k);
            h = h * 1099511628211ULL ^ std::hash<std::string>{}(v);
        }
    }
    return h;
}

WegStrahl kartenStrahl() {
    return [](const std::array<float, 3>& a, const std::array<float, 3>& b, std::array<float, 3>& t,
              std::array<float, 3>& n) {
        if (g_app->geo.empty()) { return false; }
        const TraceTreffer r = traceRay(g_app->geo, a.data(), b.data());
        if (!r.hit) { return false; }
        for (int k = 0; k < 3; ++k) {
            t[static_cast<std::size_t>(k)] = r.point[k];
            n[static_cast<std::size_t>(k)] = r.normal[k];
        }
        return true;
    };
}

// Freie Sichtlinie auf Brusthoehe (fuer die automatischen Kanten).
WegSicht kartenSicht() {
    return [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        if (g_app->geo.empty()) { return true; }
        const float pa[3] = {a[0], a[1], a[2] + 16.0F};
        const float pb[3] = {b[0], b[1], b[2] + 16.0F};
        return !traceRay(g_app->geo, pa, pb).hit;
    };
}

int entityMitName(const std::string& name) {
    for (std::size_t i = 0; i < g_app->map.entities.size(); ++i) {
        if (g_app->map.entities[i].targetname == name && wegKlasse(g_app->map.entities[i])) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Hat e ein target-Feld auf `name`? Dann welches (0..3, 4 = targetJump), sonst -1.
int zielFeld(const MapEntity& e, const std::string& name) {
    for (int z = 0; z < 4; ++z) {
        if (const std::string* t = e.find(kZielFelder[z]); t != nullptr && *t == name) { return z; }
    }
    if (const std::string* t = e.find("targetJump"); t != nullptr && *t == name) { return 4; }
    return -1;
}

void keyWeg(MapEntity& e, const std::string& key) {
    e.keys.erase(std::remove_if(e.keys.begin(), e.keys.end(), [&](const auto& kv) { return kv.first == key; }),
                 e.keys.end());
}

// Einen Namen geben, wenn er fehlt (eine Kante braucht den targetname).
std::string nameVon(int i) {
    MapEntity& e = g_app->map.entities[static_cast<std::size_t>(i)];
    if (e.targetname.empty()) { setKey(e, "targetname", freierEntityName("wp")); }
    return e.targetname;
}

}  // namespace

// --- Netz -----------------------------------------------------------------------

void wegNetzAktuell() {
    const std::size_t s = pruefsumme();
    if (s == g_app->wegNetzSchl && !g_app->wegNetz.knoten.empty()) { return; }
    g_app->wegNetzSchl = s;
    g_app->wegNetz = baueWegNetz(g_app->map.entities, kartenSicht());
    g_app->wegGeprueft = false;
    g_app->wegBefunde.clear();
}

// --- Bearbeiten (je ein Rueckgaengig-Schritt) ------------------------------------

bool wegVerbinden(int von, int nach, bool sprung) {
    const int n = static_cast<int>(g_app->map.entities.size());
    if (von < 0 || nach < 0 || von >= n || nach >= n || von == nach) { return false; }
    std::vector<MapEntity> vorher = g_app->map.entities;
    const std::string nameNach = nameVon(nach);
    const std::string nameVonE = nameVon(von);
    MapEntity& a = g_app->map.entities[static_cast<std::size_t>(von)];
    MapEntity& b = g_app->map.entities[static_cast<std::size_t>(nach)];
    // Schon verbunden (in welcher Richtung auch immer)? Dann trennen.
    const int fa = zielFeld(a, nameNach);
    const int fb = zielFeld(b, nameVonE);
    if (fa >= 0 || fb >= 0) {
        if (fa >= 0) { keyWeg(a, fa == 4 ? "targetJump" : kZielFelder[fa]); }
        if (fb >= 0) { keyWeg(b, fb == 4 ? "targetJump" : kZielFelder[fb]); }
        entitiesGeaendert(std::move(vorher));
        return true;
    }
    if (sprung) {
        setKey(a, "targetJump", nameNach);
        entitiesGeaendert(std::move(vorher));
        return true;
    }
    // Ein freies Feld - erst beim ersten Punkt, sonst andersherum beim zweiten.
    for (MapEntity* e : {&a, &b}) {
        const std::string& ziel = (e == &a) ? nameNach : nameVonE;
        for (const char* f : kZielFelder) {
            const std::string* t = e->find(f);
            if (t == nullptr || t->empty()) {
                setKey(*e, f, ziel);
                entitiesGeaendert(std::move(vorher));
                return true;
            }
        }
    }
    g_app->map.entities = std::move(vorher);
    meldung(tr(Str::WegVoll), "", true);
    return false;
}

int wegSetzen(const float boden[3], int verbindeMit) {
    std::vector<MapEntity> vorher = g_app->map.entities;
    MapEntity e;
    setKey(e, "classname", "waypoint");
    char o[96];
    // Kasten -24..45: Unterkante eine Einheit ueber dem Boden.
    std::snprintf(o, sizeof(o), "%d %d %d", static_cast<int>(std::lround(boden[0])),
                  static_cast<int>(std::lround(boden[1])), static_cast<int>(std::lround(boden[2] + 25.0F)));
    setKey(e, "origin", o);
    setKey(e, "targetname", freierEntityName("wp"));
    const std::string neuName = e.targetname;
    g_app->map.entities.push_back(e);
    const int neu = static_cast<int>(g_app->map.entities.size()) - 1;
    if (verbindeMit >= 0 && verbindeMit < neu) {
        MapEntity& v = g_app->map.entities[static_cast<std::size_t>(verbindeMit)];
        if (v.targetname.empty()) { setKey(v, "targetname", freierEntityName("wp")); }
        bool gesetzt = false;
        for (const char* f : kZielFelder) {
            const std::string* t = v.find(f);
            if (t == nullptr || t->empty()) {
                setKey(v, f, neuName);
                gesetzt = true;
                break;
            }
        }
        if (!gesetzt) { setKey(g_app->map.entities[static_cast<std::size_t>(neu)], "target", v.targetname); }
    }
    entitiesGeaendert(std::move(vorher));
    g_app->hiddenClasses.erase("waypoint");
    return neu;
}

int wegGenerierteEntfernen() {
    std::vector<MapEntity> vorher = g_app->map.entities;
    std::set<std::string> weg;
    for (const MapEntity& e : g_app->map.entities) {
        if (e.targetname.rfind(kErzeugtPrefix, 0) == 0) { weg.insert(e.targetname); }
    }
    if (weg.empty()) { return 0; }
    auto& v = g_app->map.entities;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const MapEntity& e) { return weg.count(e.targetname) != 0U; }),
            v.end());
    // Verweise anderer Wegpunkte auf die erzeugten gehen mit.
    for (MapEntity& e : v) {
        if (!wegKlasse(e)) { continue; }
        for (const char* f : {"target", "target2", "target3", "target4", "targetJump"}) {
            if (const std::string* t = e.find(f); t != nullptr && weg.count(*t) != 0U) { keyWeg(e, f); }
        }
    }
    g_app->pickedEntity = -1;
    g_app->hiddenEntities.clear();
    entitiesGeaendert(std::move(vorher));
    return static_cast<int>(weg.size());
}

int wegErzeugen(float abstand, bool alteWeg, std::string* bericht) {
    if (g_app->geo.empty()) { return 0; }
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<MapEntity> vorher = g_app->map.entities;
    // Alte erzeugte zuerst weg - im SELBEN Rueckgaengig-Schritt.
    if (alteWeg && wegGenerierteEntfernen() > 0 && !g_app->entUndo.empty()) {
        g_app->entUndo.pop_back();   // ein Schritt fuer alles (vorher steht davor)
    }
    // Startpunkte: Spielerstart, Figuren, Ziele, vorhandene Wegpunkte, die Kamera.
    std::vector<std::array<float, 3>> starts;
    for (const MapEntity& e : g_app->map.entities) {
        const std::string& c = e.classname;
        const bool start = c.rfind("info_player", 0) == 0 || c.rfind("NPC_", 0) == 0 || c.rfind("npc_", 0) == 0 ||
                           c.rfind("waypoint_navgoal", 0) == 0;
        float o[3];
        if (start && entityOrt(e, o)) { starts.push_back({o[0], o[1], o[2]}); }
    }
    starts.push_back({g_app->cam.pos[0], g_app->cam.pos[1], g_app->cam.pos[2]});
    WegErzeugung p;
    p.abstand = abstand;
    const std::vector<NeuerWegpunkt> wp = erzeugeWegpunkte(starts, kartenStrahl(), p);
    const std::vector<std::vector<int>> ziele = verteileZiele(wp, 4);
    // Namen: wpauto_1 ... (frei, falls die Karte schon solche hat)
    int nr = 1;
    std::set<std::string> belegt;
    for (const MapEntity& e : g_app->map.entities) { belegt.insert(e.targetname); }
    std::vector<std::string> namen(wp.size());
    for (std::size_t i = 0; i < wp.size(); ++i) {
        std::string n;
        do { n = std::string(kErzeugtPrefix) + std::to_string(nr++); } while (belegt.count(n) != 0U);
        namen[i] = n;
    }
    int kanten = 0;
    for (std::size_t i = 0; i < wp.size(); ++i) {
        MapEntity e;
        setKey(e, "classname", "waypoint");
        char o[96];
        std::snprintf(o, sizeof(o), "%d %d %d", static_cast<int>(std::lround(wp[i].ort[0])),
                      static_cast<int>(std::lround(wp[i].ort[1])), static_cast<int>(std::lround(wp[i].ort[2])));
        setKey(e, "origin", o);
        setKey(e, "targetname", namen[i]);
        for (std::size_t z = 0; z < ziele[i].size() && z < 4; ++z) {
            setKey(e, kZielFelder[z], namen[static_cast<std::size_t>(ziele[i][z])]);
            ++kanten;
        }
        g_app->map.entities.push_back(e);
    }
    entitiesGeaendert(std::move(vorher));
    g_app->hiddenClasses.erase("waypoint");
    g_app->zeigeWegnetz = true;
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (bericht != nullptr) {
        char z[200];
        std::snprintf(z, sizeof(z), tr(Str::WegErzeugt), static_cast<int>(wp.size()), kanten, s);
        *bericht = z;
    }
    return static_cast<int>(wp.size());
}

// --- In der Ansicht ----------------------------------------------------------------

void wegUeberlagerung() {
    if (!g_app->zeigeWegnetz || g_app->camViewActive || g_app->geo.empty()) { return; }
    wegNetzAktuell();
    const WegNetz& netz = g_app->wegNetz;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ImVec2{g_app->kartenAnsicht[0], g_app->kartenAnsicht[1]},
                     ImVec2{g_app->kartenAnsicht[2], g_app->kartenAnsicht[3]}, true);
    const float px = std::max(1.0F, ImGui::GetFontSize() / 13.0F);
    std::vector<ImVec2> schirm(netz.knoten.size());
    std::vector<char> da(netz.knoten.size(), 0);
    for (std::size_t i = 0; i < netz.knoten.size(); ++i) {
        const auto& o = netz.knoten[i].ort;
        if (g_app->draufsicht && o[2] - 24.0F > g_app->draufSchnitt) { continue; }   // weggeschnitten
        const float w[3] = {o[0], o[1], o[2] - 20.0F};   // nahe am Boden zeichnen
        da[i] = kartenPunktAufSchirm(w, schirm[i]) ? 1 : 0;
    }
    for (const WegKante& k : netz.kanten) {
        const auto a = static_cast<std::size_t>(k.a);
        const auto b = static_cast<std::size_t>(k.b);
        if (da[a] == 0 || da[b] == 0) { continue; }
        const ImU32 f = k.sprung ? IM_COL32(255, 160, 50, 230)
                        : k.designer ? IM_COL32(90, 220, 130, 210)
                                     : IM_COL32(100, 160, 255, 170);
        if (k.sprung) {
            // gestrichelt
            const ImVec2 p0 = schirm[a];
            const ImVec2 p1 = schirm[b];
            for (int s = 0; s < 12; s += 2) {
                const float t0 = static_cast<float>(s) / 12.0F;
                const float t1 = static_cast<float>(s + 1) / 12.0F;
                dl->AddLine(ImVec2{p0.x + (p1.x - p0.x) * t0, p0.y + (p1.y - p0.y) * t0},
                            ImVec2{p0.x + (p1.x - p0.x) * t1, p0.y + (p1.y - p0.y) * t1}, f, 2.0F * px);
            }
        } else {
            dl->AddLine(schirm[a], schirm[b], f, 1.6F * px);
        }
    }
    std::set<int> befundKnoten;
    for (const WegBefund& b : g_app->wegBefunde) { befundKnoten.insert(b.knoten); }
    for (std::size_t i = 0; i < netz.knoten.size(); ++i) {
        if (da[i] == 0) { continue; }
        const WegKnoten& n = netz.knoten[i];
        const ImVec2 c = schirm[i];
        const bool gewaehlt = n.entity == g_app->pickedEntity || n.entity == g_app->wegVon;
        switch (n.art) {
            case WegKnoten::Art::Wegpunkt:
                dl->AddCircleFilled(c, 3.5F * px, gewaehlt ? IM_COL32(255, 165, 40, 255) : IM_COL32(110, 220, 240, 255));
                break;
            case WegKnoten::Art::Ziel:
                dl->AddQuadFilled(ImVec2{c.x, c.y - 5.0F * px}, ImVec2{c.x + 5.0F * px, c.y}, ImVec2{c.x, c.y + 5.0F * px},
                                  ImVec2{c.x - 5.0F * px, c.y}, gewaehlt ? IM_COL32(255, 165, 40, 255) : IM_COL32(90, 220, 120, 255));
                break;
            case WegKnoten::Art::Kampf:
                dl->AddRectFilled(ImVec2{c.x - 3.0F * px, c.y - 3.0F * px}, ImVec2{c.x + 3.0F * px, c.y + 3.0F * px},
                                  gewaehlt ? IM_COL32(255, 165, 40, 255) : IM_COL32(240, 100, 90, 230));
                break;
        }
        if (befundKnoten.count(static_cast<int>(i)) != 0U) {
            dl->AddCircle(c, 8.0F * px, IM_COL32(255, 70, 70, 255), 0, 2.0F * px);
        }
    }
    // Verbinden: Gummiband vom ersten Punkt zur Maus.
    if (g_app->wegWerkzeug == 2 && g_app->wegVon >= 0) {
        for (std::size_t i = 0; i < netz.knoten.size(); ++i) {
            if (netz.knoten[i].entity == g_app->wegVon && da[i] != 0) {
                dl->AddLine(schirm[i], ImGui::GetIO().MousePos,
                            ImGui::GetIO().KeyCtrl ? IM_COL32(255, 160, 50, 230) : IM_COL32(90, 220, 130, 230), 2.0F * px);
            }
        }
    }
    dl->PopClipRect();
}

void wegWerkzeugMaus(float rasterSkal) {
    (void)rasterSkal;
    g_app->wegKlickVerbraucht = false;
    if (g_app->wegWerkzeug == 0 || g_app->geo.empty() || g_app->camViewActive) { return; }
    const ImGuiIO& io = ImGui::GetIO();
    const bool imBild = io.MousePos.x >= g_app->kartenAnsicht[0] && io.MousePos.x < g_app->kartenAnsicht[2] &&
                        io.MousePos.y >= g_app->kartenAnsicht[1] && io.MousePos.y < g_app->kartenAnsicht[3] &&
                        ImGui::IsWindowHovered();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        g_app->wegVon = -1;
        g_app->wegLetzter = -1;
        if (g_app->wegWerkzeug != 0) { g_app->wegWerkzeug = 0; }
        return;
    }
    if (!imBild || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { return; }
    g_app->wegKlickVerbraucht = true;
    if (g_app->wegWerkzeug == 1) {
        TraceTreffer t;
        if (!kartenStrahlUnterMaus(io.MousePos.x, io.MousePos.y, t) || t.normal[2] < 0.5F) {
            meldung(tr(Str::WegKeinBoden), "", true);
            return;
        }
        const int vorige = io.KeyShift ? -1 : g_app->wegLetzter;
        g_app->wegLetzter = wegSetzen(t.point, vorige);
        g_app->pickedEntity = g_app->wegLetzter;
        g_app->zeigeWegnetz = true;
        return;
    }
    // Verbinden: der naechste Netzknoten unter der Maus.
    wegNetzAktuell();
    int best = -1;
    float bestD = ImGui::GetFontSize() * 1.2F;
    bestD *= bestD;
    for (const WegKnoten& n : g_app->wegNetz.knoten) {
        const float w[3] = {n.ort[0], n.ort[1], n.ort[2] - 20.0F};
        ImVec2 s;
        if (!kartenPunktAufSchirm(w, s)) { continue; }
        const float dx = s.x - io.MousePos.x;
        const float dy = s.y - io.MousePos.y;
        if (dx * dx + dy * dy < bestD) {
            bestD = dx * dx + dy * dy;
            best = n.entity;
        }
    }
    if (best < 0) {
        g_app->wegVon = -1;
        return;
    }
    if (g_app->wegVon < 0) {
        g_app->wegVon = best;
        g_app->pickedEntity = best;
        return;
    }
    wegVerbinden(g_app->wegVon, best, io.KeyCtrl);
    // Weiter vom zweiten Punkt aus - so verbindet man eine Kette Klick fuer Klick.
    g_app->wegVon = best;
    g_app->pickedEntity = best;
}

// --- Das Fenster ---------------------------------------------------------------------

void drawWegFenster() {
    if (!g_app->wegFensterOffen) {
        g_app->wegWerkzeug = 0;
        return;
    }
    // Waechst mit dem Inhalt (Ergebniszeile, Befundliste) - mit fester Hoehe
    // war "Netz pruefen" abgeschnitten (Wegpunkttest).
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowSizeConstraints(ImVec2{fs * 24.0F, 0.0F}, ImVec2{fs * 32.0F, FLT_MAX});
    if (!ImGui::Begin((std::string(tr(Str::WegTitel)) + "###wegpunkte").c_str(), &g_app->wegFensterOffen,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    wegNetzAktuell();
    const WegNetz& netz = g_app->wegNetz;
    int wp = 0;
    int ziele = 0;
    int kampf = 0;
    for (const WegKnoten& n : netz.knoten) {
        wp += n.art == WegKnoten::Art::Wegpunkt ? 1 : 0;
        ziele += n.art == WegKnoten::Art::Ziel ? 1 : 0;
        kampf += n.art == WegKnoten::Art::Kampf ? 1 : 0;
    }
    ImGui::TextDisabled(tr(Str::WegStand), wp, ziele, kampf, static_cast<int>(netz.kanten.size()));
    if (iconTextKnopf(tr(Str::WegZeigen), UiIcon::Pfad, tr(Str::WegZeigenHint))) {
        g_app->zeigeWegnetz = !g_app->zeigeWegnetz;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", g_app->zeigeWegnetz ? tr(Str::WegAn) : tr(Str::WegAus));
    ImGui::SeparatorText(tr(Str::WegWerkzeuge));
    if (iconKnopf(tr(Str::WegSetzen), UiIcon::Wegpunkt, tr(Str::WegSetzenHint), g_app->wegWerkzeug == 1, true, true)) {
        g_app->wegWerkzeug = g_app->wegWerkzeug == 1 ? 0 : 1;
        g_app->wegLetzter = -1;
        g_app->zeigeWegnetz = true;
    }
    ImGui::SameLine();
    if (iconKnopf(tr(Str::WegVerbinden), UiIcon::Pfad, tr(Str::WegVerbindenHint), g_app->wegWerkzeug == 2, true, true)) {
        g_app->wegWerkzeug = g_app->wegWerkzeug == 2 ? 0 : 2;
        g_app->wegVon = -1;
        g_app->zeigeWegnetz = true;
    }
    // Feste Umbruchbreite: im mitwachsenden Fenster schaukelte sich ein
    // Umbruch "bis zum Rand" mit der Fensterbreite auf.
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fs * 23.0F);
    ImGui::TextDisabled("%s", g_app->wegWerkzeug == 1   ? tr(Str::WegSetzenHint)
                              : g_app->wegWerkzeug == 2 ? tr(Str::WegVerbindenHint)
                                                        : tr(Str::WegWerkzeugHint));
    ImGui::PopTextWrapPos();
    ImGui::SeparatorText(tr(Str::WegErzeugenTitel));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0F);
    ImGui::SliderFloat("##wegabstand", &g_app->wegAbstand, 96.0F, 384.0F, tr(Str::WegAbstand));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::WegAbstandHint)); }
    ImGui::SameLine();
    if (iconTextKnopf(tr(Str::WegErzeugen), UiIcon::Erzeugen, tr(Str::WegErzeugenHint), !g_app->geo.empty())) {
        wegErzeugen(g_app->wegAbstand, true, &g_app->wegMeldung);
        meldung(g_app->wegMeldung, "");
    }
    if (iconTextKnopf(tr(Str::WegEntfernen), UiIcon::Loeschen, tr(Str::WegEntfernenHint))) {
        const int n = wegGenerierteEntfernen();
        char z[120];
        std::snprintf(z, sizeof(z), tr(Str::WegEntfernt), n);
        g_app->wegMeldung = z;
    }
    if (!g_app->wegMeldung.empty()) { ImGui::TextDisabled("%s", g_app->wegMeldung.c_str()); }
    ImGui::SeparatorText(tr(Str::WegPruefenTitel));
    if (iconTextKnopf(tr(Str::WegPruefen), UiIcon::Haken, tr(Str::WegPruefenHint))) {
        g_app->wegBefunde = pruefeWegNetz(netz, g_app->map.entities, kartenStrahl());
        g_app->wegGeprueft = true;
        g_app->zeigeWegnetz = true;
    }
    if (g_app->wegGeprueft) {
        if (g_app->wegBefunde.empty()) {
            ImGui::TextColored(ImVec4{0.45F, 0.85F, 0.5F, 1.0F}, "%s", tr(Str::WegAllesGut));
        } else {
            ImGui::TextColored(ImVec4{1.0F, 0.6F, 0.35F, 1.0F}, tr(Str::WegBefunde), static_cast<int>(g_app->wegBefunde.size()));
            ImGui::BeginChild("##wegbefunde", ImVec2{-1.0F, ImGui::GetTextLineHeightWithSpacing() * 8.0F},
                              ImGuiChildFlags_Borders);
            for (std::size_t i = 0; i < g_app->wegBefunde.size(); ++i) {
                const WegBefund& b = g_app->wegBefunde[i];
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(b.text.c_str())) {
                    if (b.knoten >= 0 && static_cast<std::size_t>(b.knoten) < netz.knoten.size()) {
                        const int e = netz.knoten[static_cast<std::size_t>(b.knoten)].entity;
                        g_app->pickedEntity = e;
                        zurEntityFliegen(static_cast<std::size_t>(e));
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

// --- Draufsicht -----------------------------------------------------------------------

void draufsichtSchalten(bool an) {
    if (an == g_app->draufsicht) { return; }
    if (an) {
        g_app->vorDraufsicht = g_app->cam;
        // Schnitt: etwas ueber dem Boden unter der Kamera (eine Etage).
        float boden = g_app->cam.pos[2] - 64.0F;
        if (!g_app->geo.empty()) {
            const float a[3] = {g_app->cam.pos[0], g_app->cam.pos[1], g_app->cam.pos[2]};
            const float b[3] = {g_app->cam.pos[0], g_app->cam.pos[1], g_app->cam.pos[2] - 4096.0F};
            const TraceTreffer t = traceRay(g_app->geo, a, b);
            if (t.hit) { boden = t.point[2]; }
        }
        g_app->draufSchnitt = boden + 160.0F;
        g_app->cam.pos[2] = g_app->draufSchnitt + 1200.0F;
        g_app->cam.angles[0] = 89.0F;
        g_app->cam.angles[2] = 0.0F;
        g_app->throughCamera = false;
    } else {
        g_app->cam = g_app->vorDraufsicht;
    }
    g_app->draufsicht = an;
    g_app->mapDirty = true;
}

}  // namespace bhed::gui
