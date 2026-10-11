// app_modell.cpp - der Model-Tab: Leiste, Animationsliste, NPC-Browser, Abspielen
//
// Gewuenscht (11.10.2026): "Model-Tab modernisieren" - dieselbe Bedienung wie
// in der Karte und der Zeitleiste (rc588):
//
//   oben     Icons: .glm oeffnen, aus .pk3, Skin, .gla, NPC-Browser,
//            Drehteller, Knochen
//   links    die Animationen als Liste mit Suche und Gruppen (BOTH_, TORSO_,
//            LEGS_, FACE_ ...), Laenge und Schleife; Klick spielt ab, dazu
//            "Kopieren" und "Ins Skript" (set ( "SET_ANIM_BOTH", ... ))
//   unten    Transportleiste wie die Zeitleiste: Anfang, Bild zurueck,
//            Abspielen, Bild vor, grosse Bildanzeige, Schleife, Tempo,
//            Haut, Flaechen, Schieberegler
#include "app_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace bhed::gui {
namespace {

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

// Die Gruppe einer Animation am Namen - so sortiert sie auch die animation.cfg.
const char* animGruppe(const std::string& name) {
    static const char* const kGruppen[] = {"BOTH_", "TORSO_", "LEGS_", "FACE_"};
    for (const char* g : kGruppen) {
        if (name.rfind(g, 0) == 0) { return g; }
    }
    return "";
}

// SET_ANIM_UPPER fuer TORSO_, SET_ANIM_LOWER fuer LEGS_, sonst SET_ANIM_BOTH
// (Q3_Interface.h: "Torso and head anim", "Legs anim", "same anim on torso
// and legs").
const char* animSetzen(const std::string& name) {
    if (name.rfind("TORSO_", 0) == 0) { return "SET_ANIM_UPPER"; }
    if (name.rfind("LEGS_", 0) == 0) { return "SET_ANIM_LOWER"; }
    return "SET_ANIM_BOTH";
}

}  // namespace

void animWaehlen(int i, bool abspielen) {
    if (i < 0 || static_cast<std::size_t>(i) >= g_app->animList.size()) { return; }
    const AnimEntry& e = g_app->animList[static_cast<std::size_t>(i)];
    g_app->animIndex = i;
    g_app->animFrame = e.firstFrame;
    g_app->animTime = 0.0F;
    g_app->animText = e.name;
    g_app->animPlaying = abspielen;
    g_app->modelDirty = true;
}

bool animInsSkript(int i) {
    if (i < 0 || static_cast<std::size_t>(i) >= g_app->animList.size()) { return false; }
    const std::string& name = g_app->animList[static_cast<std::size_t>(i)].name;
    const std::string text = std::string("set ( /*@SET_TYPES*/ \"") + animSetzen(name) + "\", \"" + name + "\" );\n";
    Script s;
    std::vector<Diag> diag;
    if (!readScript(text, s, diag) || s.nodes.empty()) { return false; }
    Path at;
    bool ok = false;
    if (g_app->selectedPath.empty()) {
        ok = g_app->doc.insertAfterAll(Path{}, {s.nodes.front()}, &at);
    } else {
        ok = g_app->doc.insertAfterAll(g_app->selectedPath, {s.nodes.front()}, &at);
    }
    if (!ok) { return false; }
    g_app->selectedPath = at;
    rebuildTree();
    g_app->camTrackValid = false;
    char z[200];
    std::snprintf(z, sizeof(z), tr(Str::ModelAnimInserted), name.c_str());
    meldung(z, "");
    return true;
}

// --- Die Animationsliste (linke Spalte) -------------------------------------------
void drawAnimListe(float hoehe) {
    ImGui::BeginChild("animliste", ImVec2{-1.0F, hoehe}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (g_app->animList.empty()) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextDisabled("%s", tr(g_app->model.empty() ? Str::ModelNone : Str::ModelNoAnim));
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        return;
    }
    static char suche[64] = "";
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##animsuche", tr(Str::ModelAnimSearch), suche, sizeof(suche));
    const std::string such = klein(suche);
    // Gruppenfilter als Knoepfe
    static int gruppe = 0;   // 0 alle, 1 BOTH, 2 TORSO, 3 LEGS, 4 FACE, 5 andere
    {
        static const char* const kNamen[] = {"*", "BOTH", "TORSO", "LEGS", "FACE"};
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ImGui::GetFontSize() * 0.35F, 2.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2.0F, 2.0F});
        for (int k = 0; k < 5; ++k) {
            if (k > 0) { ImGui::SameLine(); }
            const bool an = gruppe == k;
            if (an) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }
            if (ImGui::SmallButton(k == 0 ? tr(Str::ModelAnimAll) : kNamen[k])) { gruppe = k; }
            if (an) { ImGui::PopStyleColor(); }
        }
        ImGui::PopStyleVar(2);
    }
    const int sel = g_app->animIndex;
    // Kopieren / ins Skript fuer die gewaehlte
    {
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{sp * 0.4F, ImGui::GetStyle().ItemSpacing.y});
        const bool hat = sel >= 0 && static_cast<std::size_t>(sel) < g_app->animList.size();
        if (iconKnopf(tr(Str::ActCopy), UiIcon::Duplizieren, tr(Str::HintCopy), false, hat)) {
            const std::string& n = g_app->animList[static_cast<std::size_t>(sel)].name;
            ImGui::SetClipboardText(n.c_str());
            char kop[200];
            std::snprintf(kop, sizeof(kop), tr(Str::ModelAnimCopied), n.c_str());
            addStatus(kop);
        }
        ImGui::SameLine();
        if (iconTextKnopf(tr(Str::ModelAnimToScript), UiIcon::Skript, tr(Str::ModelAnimToScriptHint), hat)) {
            animInsSkript(sel);
        }
        ImGui::PopStyleVar();
    }
    ImGui::Separator();
    ImGui::BeginChild("animzeilen", ImVec2{-1.0F, -1.0F}, ImGuiChildFlags_None);
    int gezeigt = 0;
    for (std::size_t i = 0; i < g_app->animList.size(); ++i) {
        const AnimEntry& e = g_app->animList[i];
        const std::string g = animGruppe(e.name);
        if (gruppe == 1 && g != "BOTH_") { continue; }
        if (gruppe == 2 && g != "TORSO_") { continue; }
        if (gruppe == 3 && g != "LEGS_") { continue; }
        if (gruppe == 4 && g != "FACE_") { continue; }
        if (!such.empty() && klein(e.name).find(such) == std::string::npos) { continue; }
        ++gezeigt;
        ImGui::PushID(static_cast<int>(i));
        const bool gewaehlt = static_cast<int>(i) == sel;
        if (ImGui::Selectable(e.name.c_str(), gewaehlt, ImGuiSelectableFlags_AllowDoubleClick)) {
            animWaehlen(static_cast<int>(i), true);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { animInsSkript(static_cast<int>(i)); }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip(tr(Str::ModelAnimRowHint), e.numFrames,
                              static_cast<double>(e.numFrames) / static_cast<double>(std::max(e.fps, 1)), e.fps);
        }
        // Rechts: Laenge und - wenn sie sich wiederholt - das Schleifen-Icon
        char laenge[32];
        std::snprintf(laenge, sizeof(laenge), "%.1fs",
                      static_cast<double>(e.numFrames) / static_cast<double>(std::max(e.fps, 1)));
        const float ih = ImGui::GetTextLineHeight();
        const float lw = ImGui::CalcTextSize(laenge).x + ih + 4.0F;
        ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - lw, ImGui::GetFontSize() * 6.0F));
        ImGui::TextDisabled("%s", laenge);
        if (e.loopFrame >= 0) {
            ImGui::SameLine(0.0F, 4.0F);
            const ImVec2 q = ImGui::GetCursorScreenPos();
            uiIcon(ImGui::GetWindowDrawList(), UiIcon::Schleife, ImVec2{std::floor(q.x), std::floor(q.y)}, ih,
                   ImGui::GetColorU32(uiIconFarbe(UiIcon::Schleife)));
            ImGui::Dummy(ImVec2{ih, ih});
        }
        if (gewaehlt && g_app->animScrollen) {
            ImGui::SetScrollHereY(0.4F);
            g_app->animScrollen = false;
        }
        ImGui::PopID();
    }
    if (gezeigt == 0) { ImGui::TextDisabled("%s", tr(Str::ModelAnimNone)); }
    ImGui::EndChild();
    ImGui::EndChild();
}

// --- Die Leiste ueber der Ansicht ----------------------------------------------------
void drawModelToolbar() {
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{sp * 0.5F, ImGui::GetStyle().ItemSpacing.y});
    if (iconKnopf(tr(Str::OpenGlm), UiIcon::Oeffnen, tr(Str::OpenGlmHint))) { openModelFile(); }
    ImGui::SameLine();
    if (iconKnopf(tr(Str::OpenGlmPk3), UiIcon::Paket, tr(Str::OpenGlmPk3Hint))) {
        if (g_app->gamePaths.empty()) { rescanGamePaths(); }
        g_app->pk3Kind = 2;
        refreshPk3List();
        g_app->pk3Open = true;
    }
    ImGui::SameLine();
    if (iconTextKnopf(tr(Str::NpcBrowser), UiIcon::Figur, tr(Str::NpcBrowserHint))) {
        g_app->npcBrowserOffen = !g_app->npcBrowserOffen;
    }
    ImGui::SameLine(0.0F, sp * 1.5F);
    ImGui::BeginDisabled(g_app->model.empty());
    if (iconKnopf(tr(Str::OpenSkin), UiIcon::Namen, tr(Str::OpenSkinHint))) { openSkinFile(); }
    ImGui::SameLine();
    if (iconKnopf(tr(Str::ModelLoadGla), UiIcon::Liste, tr(Str::ModelLoadGlaHint))) { openSkeletonFile(); }
    ImGui::SameLine(0.0F, sp * 1.5F);
    if (iconKnopf(tr(Str::ModelTurntable), UiIcon::Schleife, tr(Str::ModelTurntableHint), g_app->modellDrehteller, true,
                  true)) {
        g_app->modellDrehteller = !g_app->modellDrehteller;
    }
    ImGui::SameLine();
    if (iconKnopf(tr(Str::ModelBones), UiIcon::Pfad, tr(Str::ModelBonesHint), g_app->modellKnochen, true, true)) {
        g_app->modellKnochen = !g_app->modellKnochen;
    }
    ImGui::SameLine();
    if (iconKnopf(tr(Str::ModelCaps), UiIcon::Bruch, tr(Str::ModelCapsHint), g_app->showCaps, true, true)) {
        g_app->showCaps = !g_app->showCaps;
        g_app->modelDirty = true;
    }
    ImGui::SameLine();
    if (iconKnopf(tr(Str::ModelTextures), UiIcon::Leuchten, tr(Str::ModelTexturesHint), g_app->showModelTextures, true,
                  true)) {
        g_app->showModelTextures = !g_app->showModelTextures;
        g_app->modelDirty = true;
    }
    ImGui::SameLine(0.0F, sp * 1.5F);
    if (iconKnopf(tr(Str::ModelResetView), UiIcon::Einpassen, tr(Str::ModelResetViewHint))) { modellAnsichtEinpassen(); }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    if (!g_app->model.empty()) {
        ImGui::SameLine(0.0F, sp * 1.5F);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", fileName(g_app->modelPath).c_str());
    }
}

void modellAnsichtEinpassen() {
    if (g_app->model.empty()) { return; }
    for (int k = 0; k < 3; ++k) {
        g_app->modelCam.pos[k] = (g_app->model.mins[k] + g_app->model.maxs[k]) * 0.5F;
    }
    float size = 0.0F;
    for (int k = 0; k < 3; ++k) { size = std::max(size, g_app->model.maxs[k] - g_app->model.mins[k]); }
    g_app->modelOrbit.distance = std::max(size * 1.15F, 24.0F);
    g_app->modelCam.angles[0] = 0.0F;
    g_app->modelCam.angles[1] = 180.0F;
    g_app->modelOrbit.turn(g_app->modelCam, 0.0F, 0.0F);
    g_app->modelDirty = true;
}

// --- NPC-Browser ------------------------------------------------------------------------
//
// Alle NPC-Typen der .npc-Dateien; ein Klick laedt Modell und Haut der Figur,
// wie das Spiel sie zeigt (playerModel, customSkin).
bool npcLaden(const std::string& typ) {
    if (g_app->npcMap.empty()) { (void)npcTypenListe(); }
    const auto it = g_app->npcMap.find(klein(typ));
    if (it == g_app->npcMap.end() || it->second.playerModel.empty()) { return false; }
    const std::string pfad = "models/players/" + it->second.playerModel + "/model.glm";
    std::string fehler;
    if (!oeffneModellAusArchiv(pfad, &fehler)) {
        meldung(pfad + ": " + fehler, "", true);
        return false;
    }
    if (!it->second.customSkin.empty()) {
        const std::string skin = "model_" + it->second.customSkin + ".skin";
        for (std::size_t i = 0; i < g_app->modelSkins.size(); ++i) {
            if (klein(g_app->modelSkins[i]) == klein(skin)) { applySkinByIndex(static_cast<int>(i)); }
        }
    }
    g_app->npcGeladen = typ;
    g_app->modelDirty = true;
    return true;
}

const std::vector<std::string>& npcTypenListe() {
    static std::vector<std::string> typen;
    static std::size_t stand = static_cast<std::size_t>(-1);
    if (g_app->npcMap.empty()) {
        for (const GamePath& gp : g_app->gamePaths) {
            for (const FoundFile& f : findByExtension(gp, {".npc"})) {
                std::string text;
                if (readFromArchives(f.name, text)) { parseNpcFile(text, g_app->npcMap); }
            }
        }
    }
    if (stand != g_app->npcMap.size()) {
        typen.clear();
        for (const auto& [t, d] : g_app->npcMap) {
            if (!d.playerModel.empty()) { typen.push_back(t); }
        }
        stand = g_app->npcMap.size();
    }
    return typen;
}

void drawNpcBrowser() {
    if (!g_app->npcBrowserOffen) { return; }
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2{fs * 22.0F, fs * 30.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin((std::string(tr(Str::NpcBrowser)) + "###npcbrowser").c_str(), &g_app->npcBrowserOffen)) {
        ImGui::End();
        return;
    }
    static char suche[64] = "";
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##npcsuche", tr(Str::NpcSearch), suche, sizeof(suche));
    const std::string such = klein(suche);
    const auto& typen = npcTypenListe();
    ImGui::TextDisabled(tr(Str::NpcCount), static_cast<int>(typen.size()));
    ImGui::BeginChild("##npcliste", ImVec2{-1.0F, -1.0F}, ImGuiChildFlags_Borders);
    for (const std::string& t : typen) {
        if (!such.empty() && klein(t).find(such) == std::string::npos) {
            const auto it = g_app->npcMap.find(t);
            if (it == g_app->npcMap.end() || klein(it->second.playerModel).find(such) == std::string::npos) { continue; }
        }
        const auto it = g_app->npcMap.find(t);
        ImGui::PushID(t.c_str());
        if (ImGui::Selectable(t.c_str(), klein(g_app->npcGeladen) == t)) { npcLaden(t); }
        if (it != g_app->npcMap.end()) {
            ImGui::SameLine(fs * 11.0F);
            ImGui::TextDisabled("%s%s%s", it->second.playerModel.c_str(), it->second.customSkin.empty() ? "" : " / ",
                                it->second.customSkin.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

// --- In der Ansicht: Drehteller und Knochen ----------------------------------------------
void modellDrehteller() {
    if (!g_app->modellDrehteller || g_app->model.empty()) { return; }
    g_app->modelOrbit.turn(g_app->modelCam, -ImGui::GetIO().DeltaTime * 30.0F, 0.0F);
}

void modellUeberlagerung(ImVec2 lo, ImVec2 groesse) {
    g_app->modellKnochenLinien = 0;
    if (!g_app->modellKnochen || g_app->anim.bones.empty() || g_app->anim.numFrames <= 0) { return; }
    std::vector<BoneMatrix> welt;
    g_app->anim.worldMatrices(std::clamp(g_app->animFrame, 0, g_app->anim.numFrames - 1), welt);
    const Camera& c = g_app->modelCam;
    float f[3];
    float r[3];
    float u[3];
    c.forward(f);
    c.right(r);
    c.up(u);
    const float fy = 1.0F / std::tan(c.fovY * 3.14159265F / 360.0F);
    const float halbB = groesse.x * 0.5F;
    const float halbH = groesse.y * 0.5F;
    const auto schirm = [&](const float w[3], ImVec2& out) {
        const float d[3] = {w[0] - c.pos[0], w[1] - c.pos[1], w[2] - c.pos[2]};
        const float z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
        if (z < 1.0F) { return false; }
        const float x = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
        const float y = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
        out = ImVec2{lo.x + halbB + x * fy * halbH / z, lo.y + halbH - y * fy * halbH / z};
        return true;
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(lo, ImVec2{lo.x + groesse.x, lo.y + groesse.y}, true);
    const std::size_t n = std::min(welt.size(), g_app->anim.bones.size());
    for (std::size_t i = 0; i < n; ++i) {
        const float p[3] = {welt[i].m[0][3], welt[i].m[1][3], welt[i].m[2][3]};
        ImVec2 sp;
        if (!schirm(p, sp)) { continue; }
        dl->AddCircleFilled(sp, 2.5F, IM_COL32(255, 220, 90, 230));
        const int par = g_app->anim.bones[i].parent;
        if (par >= 0 && static_cast<std::size_t>(par) < n) {
            const float q[3] = {welt[static_cast<std::size_t>(par)].m[0][3], welt[static_cast<std::size_t>(par)].m[1][3],
                                welt[static_cast<std::size_t>(par)].m[2][3]};
            ImVec2 sq;
            if (schirm(q, sq)) {
                dl->AddLine(sp, sq, IM_COL32(255, 200, 60, 200), 1.5F);
                ++g_app->modellKnochenLinien;
            }
        }
    }
    dl->PopClipRect();
}

// --- Die Leiste unter allen drei Spalten -------------------------------------------------
void drawModelPanel() {
    const bool have = !g_app->model.empty();
    const float fs = ImGui::GetFontSize();
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const bool animDa = have && !g_app->anim.empty() && g_app->animIndex >= 0 &&
                        static_cast<std::size_t>(g_app->animIndex) < g_app->animList.size();
    const AnimEntry* e = animDa ? &g_app->animList[static_cast<std::size_t>(g_app->animIndex)] : nullptr;

    // --- Abspielen (die Uhr) -----------------------------------------------------
    if (e != nullptr && g_app->animPlaying) {
        const bool wantLoop = (g_app->loopMode == 1) || (g_app->loopMode == 0 && e->loopFrame >= 0);
        g_app->animTime += ImGui::GetIO().DeltaTime * g_app->animSpeed * static_cast<float>(std::max(e->fps, 1));
        while (g_app->animTime >= 1.0F) {
            g_app->animTime -= 1.0F;
            ++g_app->animFrame;
            if (g_app->animFrame >= e->firstFrame + e->numFrames) {
                if (wantLoop) {
                    // Ab loopFrame, nicht ab dem Anfang: manche Animationen
                    // haben einen Vorlauf, der nur einmal laufen soll.
                    const int from = (g_app->loopMode == 1 && e->loopFrame < 0) ? 0 : std::max(e->loopFrame, 0);
                    g_app->animFrame = e->firstFrame + from;
                } else {
                    g_app->animFrame = e->firstFrame + e->numFrames - 1;
                    g_app->animPlaying = false;
                }
            }
            g_app->modelDirty = true;
        }
    }

    // --- Zeile 1: Transportleiste ------------------------------------------------
    ImGui::BeginDisabled(e == nullptr);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{fs * 0.2F, ImGui::GetStyle().ItemSpacing.y});
    if (iconKnopf(tr(Str::TlRewind), UiIcon::ZumAnfang, nullptr) && e != nullptr) {
        g_app->animFrame = e->firstFrame;
        g_app->animTime = 0.0F;
        g_app->modelDirty = true;
    }
    ImGui::SameLine();
    if (iconKnopf(tr(Str::ModelPrevFrame), UiIcon::Zurueck, nullptr) && e != nullptr) {
        g_app->animFrame = std::max(e->firstFrame, g_app->animFrame - 1);
        g_app->animPlaying = false;
        g_app->modelDirty = true;
    }
    ImGui::SameLine();
    if (iconKnopf(g_app->animPlaying ? tr(Str::ModelStop) : tr(Str::ModelPlay),
                  g_app->animPlaying ? UiIcon::Pause : UiIcon::Abspielen, nullptr, g_app->animPlaying)) {
        if (!g_app->animPlaying && e != nullptr && g_app->animFrame >= e->firstFrame + e->numFrames - 1) {
            g_app->animFrame = e->firstFrame;
        }
        g_app->animPlaying = !g_app->animPlaying;
    }
    ImGui::SameLine();
    if (iconKnopf(tr(Str::ModelNextFrame), UiIcon::Vor, nullptr) && e != nullptr) {
        g_app->animFrame = std::min(e->firstFrame + e->numFrames - 1, g_app->animFrame + 1);
        g_app->animPlaying = false;
        g_app->modelDirty = true;
    }
    // Bild und Zeit, gross
    ImGui::SameLine(0.0F, fs * 0.6F);
    {
        const int rel = e != nullptr ? g_app->animFrame - e->firstFrame : 0;
        char jetzt[32];
        char gesamt[64];
        std::snprintf(jetzt, sizeof(jetzt), "%d", e != nullptr ? rel + 1 : 0);
        std::snprintf(gesamt, sizeof(gesamt), " / %d   %.2f s", e != nullptr ? e->numFrames : 0,
                      e != nullptr ? static_cast<double>(rel) / static_cast<double>(std::max(e->fps, 1)) : 0.0);
        const ImVec2 t1 = ImGui::CalcTextSize(jetzt);
        const ImVec2 t2 = ImGui::CalcTextSize(gesamt);
        const float hh = ImGui::GetFrameHeight();
        ImGui::Dummy(ImVec2{t1.x + t2.x + fs * 0.8F, hh});
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding + 2.0F);
        dl->AddText(ImVec2{a.x + fs * 0.4F, a.y + (hh - t1.y) * 0.5F}, ImGui::GetColorU32(ImGuiCol_Text), jetzt);
        dl->AddText(ImVec2{a.x + fs * 0.4F + t1.x, a.y + (hh - t1.y) * 0.5F}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    gesamt);
    }
    ImGui::SameLine(0.0F, fs * 0.6F);
    // Schleife: aus der animation.cfg (aus), erzwungen (an)
    if (iconKnopf(tr(Str::ModelLoop), UiIcon::Schleife, tr(Str::ModelLoopHint), g_app->loopMode == 1, true, true)) {
        g_app->loopMode = g_app->loopMode == 1 ? 0 : 1;
    }
    ImGui::SameLine();
    {
        static const float kTempi[] = {0.1F, 0.25F, 0.5F, 1.0F, 1.5F, 2.0F, 3.0F};
        char tempo[16];
        std::snprintf(tempo, sizeof(tempo), "%gx", static_cast<double>(g_app->animSpeed));
        ImGui::SetNextItemWidth(fs * 3.4F);
        if (ImGui::BeginCombo("##modeltempo", tempo, ImGuiComboFlags_NoArrowButton)) {
            for (const float v : kTempi) {
                char n[16];
                std::snprintf(n, sizeof(n), "%gx", static_cast<double>(v));
                if (ImGui::Selectable(n, std::fabs(v - g_app->animSpeed) < 0.001F)) { g_app->animSpeed = v; }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tr(Str::ModelSpeed)); }
    }
    ImGui::PopStyleVar();
    ImGui::EndDisabled();
    // Die gewaehlte Animation, ihr Name
    if (e != nullptr) {
        ImGui::SameLine(0.0F, fs * 0.8F);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(e->name.c_str());
    }
    // Haut und Flaechen rechts daneben
    if (have && !g_app->modelSkins.empty()) {
        ImGui::SameLine(0.0F, fs * 1.2F);
        ImGui::SetNextItemWidth(fs * 11.0F);
        const std::string current =
            (g_app->modelSkinIndex >= 0 && static_cast<std::size_t>(g_app->modelSkinIndex) < g_app->modelSkins.size())
                ? g_app->modelSkins[static_cast<std::size_t>(g_app->modelSkinIndex)]
                : std::string("-");
        if (ImGui::BeginCombo(tr(Str::ModelSkin), current.c_str())) {
            for (std::size_t i = 0; i < g_app->modelSkins.size(); ++i) {
                const bool selS = (static_cast<int>(i) == g_app->modelSkinIndex);
                if (ImGui::Selectable(g_app->modelSkins[i].c_str(), selS)) { applySkinByIndex(static_cast<int>(i)); }
                if (selS) { ImGui::SetItemDefaultFocus(); }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) { ImGui::SetTooltip("%s", tr(Str::ModelSkinHint)); }
        ImGui::SameLine();
        int shown = 0;
        int total = 0;
        for (const GlmSurface& sf : g_app->model.surfaces) {
            if (sf.isTag()) { continue; }
            ++total;
            if (sf.isVisible(g_app->showCaps)) { ++shown; }
        }
        ImGui::TextDisabled(tr(Str::ModelSurfaces), shown, total);
        // Klick: alle Flaechen ins Detailprotokoll (siehe Watt Tambor, rc3xx).
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tr(Str::ModelSurfaceDump)); }
        if (ImGui::IsItemClicked()) { modellFlaechenProtokoll(); }
    }

    // --- Zeile 2: Schieberegler ----------------------------------------------------
    if (e == nullptr) {
        int none = 0;
        ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##frameoff", &none, 0, 0, "");
        ImGui::EndDisabled();
        return;
    }
    int rel = g_app->animFrame - e->firstFrame;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##frame", &rel, 0, std::max(e->numFrames - 1, 0), "")) {
        g_app->animFrame = e->firstFrame + rel;
        g_app->animPlaying = false;
        g_app->modelDirty = true;
    }
    (void)sp;
}

// Was der Zeichner von jeder Flaeche sieht - ins Detailprotokoll.
//
// Gemeldet: "bei Watt Tambor fehlt der Brustpanzer". Die Suche danach hat
// zwoelf Runden gebraucht, weil von aussen nicht zu sehen war, WELCHE Zahl
// nicht stimmt. Diese Ausgabe stellt alle nebeneinander.
void modellFlaechenProtokoll() {
    diag::info("Flaechen von \"" + g_app->model.name + "\":");
    for (std::size_t i = 0; i < g_app->model.surfaces.size(); ++i) {
        const GlmSurface& sf = g_app->model.surfaces[i];
        if (sf.isTag()) { continue; }
        float mn[3] = {1e9F, 1e9F, 1e9F};
        float mx[3] = {-1e9F, -1e9F, -1e9F};
        for (const GlmVertex& v : sf.verts) {
            for (int k = 0; k < 3; ++k) {
                mn[k] = std::min(mn[k], v.xyz[k]);
                mx[k] = std::max(mx[k], v.xyz[k]);
            }
        }
        std::set<int> knochen;
        for (const GlmVertex& v : sf.verts) {
            for (int k = 0; k < 4; ++k) {
                if (v.weights[k] > 0.0001F) { knochen.insert(static_cast<int>(v.bones[k])); }
            }
        }
        std::string kn;
        for (const int k : knochen) { kn += std::to_string(k) + " "; }
        const bool hatTextur = i < g_app->modelTextures.bySurface.size() && !g_app->modelTextures.bySurface[i].empty();
        char zeile[320];
        std::snprintf(zeile, sizeof zeile,
                      "   %-22s Ecken %-5zu Dreiecke %-5zu sichtbar %d Textur %d Flagge 0x%02x  x %.1f..%.1f y %.1f..%.1f "
                      "z %.1f..%.1f  Knochen %s",
                      sf.name.c_str(), sf.verts.size(), sf.indexes.size() / 3,
                      static_cast<int>(sf.isVisible(g_app->showCaps)), static_cast<int>(hatTextur),
                      static_cast<unsigned>(sf.flags), static_cast<double>(sf.verts.empty() ? 0.0F : mn[0]),
                      static_cast<double>(sf.verts.empty() ? 0.0F : mx[0]),
                      static_cast<double>(sf.verts.empty() ? 0.0F : mn[1]),
                      static_cast<double>(sf.verts.empty() ? 0.0F : mx[1]),
                      static_cast<double>(sf.verts.empty() ? 0.0F : mn[2]),
                      static_cast<double>(sf.verts.empty() ? 0.0F : mx[2]), kn.c_str());
        diag::info(zeile);
    }
}

}  // namespace bhed::gui
