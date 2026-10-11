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
#include "app_internal.h"

#include "bhed/moveranim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

std::vector<AnimSchluessel> schluesselJetzt(const std::string& name) {
    const auto at = findeAnimBlock(g_app->doc.script(), name);
    return at ? leseAnimBlock(g_app->doc.script(), *at) : std::vector<AnimSchluessel>{};
}

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
}

bool animSchreiben(const std::vector<AnimSchluessel>& k) {
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) { return false; }
    const std::string name = animNameVon(*e);
    if (name.empty()) { return false; }
    if (!g_app->doc.ersetzeSkript(mitAnimBlock(g_app->doc.script(), name, k), "animation")) { return false; }
    rebuildTree();
    g_app->camTrackValid = false;
    g_app->mapDirty = true;
    return true;
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
    for (std::size_t i = 1; i < k.size(); ++i) {
        if (da[i] != 0 && da[i - 1] != 0) { dl->AddLine(s[i - 1], s[i], IM_COL32(255, 200, 80, 220), 2.0F * px); }
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

void drawAnimFenster() {
    if (!g_app->animFensterOffen) { return; }
    const MapEntity* e = animEntityPtr();
    if (e == nullptr) {
        g_app->animFensterOffen = false;
        return;
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
    ImGui::TextDisabled(tr(Str::AnimHint), g_app->shownName.empty() ? "unnamed.txt" : g_app->shownName.c_str());
    if (e->classname.rfind("func_", 0) == 0 && e->find("origin") == nullptr) {
        ImGui::TextColored(ImVec4{1.0F, 0.7F, 0.35F, 1.0F}, "%s", tr(Str::AnimNoOrigin));
    }
    ImGui::PopTextWrapPos();
    std::vector<AnimSchluessel> k = schluesselJetzt(name);
    // Knoepfe
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{sp * 0.5F, ImGui::GetStyle().ItemSpacing.y});
    if (iconTextKnopf(tr(Str::AnimAddKey), UiIcon::Plus, tr(Str::AnimAddKeyHint))) { animSchluesselAmZeiger(); }
    ImGui::SameLine();
    const bool hat = g_app->animSchluessel >= 0 && static_cast<std::size_t>(g_app->animSchluessel) < k.size();
    if (iconKnopf(tr(Str::AnimDeleteKey), UiIcon::Loeschen, nullptr, false, hat)) {
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
    ImGui::SameLine(0.0F, sp * 1.5F);
    {
        const std::string* lin = e->find("linear");
        const bool linear = lin != nullptr && *lin == "1";
        if (iconKnopf(tr(Str::AnimLinear), UiIcon::Pfad, tr(Str::AnimLinearHint), linear, true, true)) {
            entityKeySetzen(g_app->animEntity, "linear", linear ? "0" : "1");
            g_app->camTrackValid = false;
        }
    }
    ImGui::SameLine(0.0F, sp * 1.5F);
    if (iconKnopf(tr(Str::AnimRemove), UiIcon::Schliessen, tr(Str::AnimRemoveHint), false, !k.empty())) {
        animSchreiben({});
        g_app->animSchluessel = -1;
    }
    ImGui::PopStyleVar();
    // Die Schluessel als Tabelle
    if (k.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fs * 27.0F);
        ImGui::TextDisabled("%s", tr(Str::AnimNoKeys));
        ImGui::PopTextWrapPos();
    } else if (ImGui::BeginTable("##animschluessel", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
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
        ImGui::TextDisabled("%s", tr(Str::AnimDragHint));
    }
    ImGui::End();
}

}  // namespace bhed::gui
