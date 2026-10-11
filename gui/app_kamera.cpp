// app_kamera.cpp - Kameraeinstellungen bearbeiten wie in Unreal und Blender
//
// Gewuenscht (11.10.2026): "passen die Kamera sachen eigentlich? ist die
// Kamera editieren und erstellen intuitiv und modern? auch mit dem Gizmo passt
// da alles?" - und auf die Antwort hin "mach das bitte. so das es moderner
// wirkt".
//
// Bis rc587 war die Kamera anders zu bedienen als die Entities: das Gizmo kam
// erst ueber das Rechtsklickmenue, eine Verschiebung galt erst nach einem
// kleinen "Apply" unten im Band, beim Ueberfahren geschah nichts, und neue
// Einstellungen gab es nur ueber einen Textknopf. Jetzt:
//
//   - ausgewaehlt = Gizmo da (Verschieben), Loslassen schreibt ins Skript,
//     ein Strg+Z je Zug - wie bei den Entities seit rc586
//   - Ueberfahren hebt die Kamera hervor und nennt die Einstellung
//   - eine Leiste in der Ansicht (wie Unreals Viewport-Werkzeuge):
//     Verschieben/Drehen, Welt/Lokal, vorige/naechste Einstellung,
//     "Auf Ansicht setzen", "Steuern" (Pilot), Durchblick, neu, loeschen
//   - der Inspektor links zeigt die Einstellungen der Kamera mit Ort, Winkeln
//     und Dauer zum Eintippen
//   - Einstellungen auf einem ref_tag (tag( "cam1", ORIGIN )) verschieben den
//     ref_tag selbst - so arbeitet das Spiel damit
#include "app_internal.h"
#include "gpumap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace bhed::gui {
namespace {

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

// Wie ortText im Missionsbau: ganze Einheiten, so stehen Orte in den .ent.
std::string ganzText(const float p[3]) {
    char b[96];
    std::snprintf(b, sizeof(b), "%d %d %d", static_cast<int>(std::lround(p[0])), static_cast<int>(std::lround(p[1])),
                  static_cast<int>(std::lround(p[2])));
    return b;
}

std::string vek(const float v[3]) {
    char b[96];
    std::snprintf(b, sizeof(b), "%.3f %.3f %.3f", static_cast<double>(v[0]), static_cast<double>(v[1]),
                  static_cast<double>(v[2]));
    return b;
}

// Die Entity, die als Marke dient (ref_tag oder waypoint_navgoal), -1 keine.
int markenEntity(const std::string& name) {
    const std::string n = klein(name);
    for (std::size_t i = 0; i < g_app->map.entities.size(); ++i) {
        const MapEntity& e = g_app->map.entities[i];
        if (klein(e.targetname) == n && (e.classname == "ref_tag" || e.classname.rfind("waypoint_navgoal", 0) == 0)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<KameraSchluessel> schluessel() { return kameraSchluessel(g_app->doc.script()); }

bool gueltig(const std::vector<KameraSchluessel>& ks, int k) {
    return k >= 0 && static_cast<std::size_t>(k) < ks.size();
}

// Wann eine Einstellung beginnt (aus der Kamerabahn), -1 unbekannt.
double startZeit(const Path& p) {
    for (const CamSegment& c : g_app->camTrack.segments) {
        if (c.kind == CamSegment::Kind::Move && c.fremd.empty() && c.path == p) { return c.startMs; }
    }
    return -1.0;
}

// Die Dauer im MOVE (camera ( MOVE, <ort>, dauer )).
double moveDauer(const Path& p) {
    const Node* n = nodeAt(g_app->doc.script(), p);
    if (n == nullptr || n->args.size() < 3) { return 0.0; }
    return std::atof(n->args[2].text.c_str());
}

void vorschauVerwerfen() {
    for (int k = 0; k < 3; ++k) {
        g_app->gizmoOffset[k] = 0.0F;
        g_app->gizmoAngles[k] = 0.0F;
    }
}

void nachSkriptAenderung() {
    g_app->kameraSchriebSkript = true;
    rebuildTree();
    g_app->camTrackValid = false;
    g_app->mapDirty = true;
}

// Einen ref_tag verschieben / drehen (Entity-Rueckgaengig).
bool markeOrt(const std::string& name, const float ort[3]) {
    const int i = markenEntity(name);
    if (i < 0) { return false; }
    return entityKeySetzen(i, "origin", ganzText(ort));
}

bool markeWinkel(const std::string& name, const float ang[3]) {
    const int i = markenEntity(name);
    if (i < 0) { return false; }
    const MapEntity& e = g_app->map.entities[static_cast<std::size_t>(i)];
    // Ein ref_tag mit target schaut IMMER zu seinem Ziel (g_ref.cpp ref_link) -
    // ein eigener Winkel wuerde im Spiel ueberschrieben.
    if (const std::string* t = e.find("target"); t != nullptr && !t->empty()) {
        char z[300];
        std::snprintf(z, sizeof(z), tr(Str::CamTagAimed), name.c_str(), t->c_str());
        meldung(z, "", true);
        return false;
    }
    char b[96];
    std::snprintf(b, sizeof(b), "%.1f %.1f %.1f", static_cast<double>(ang[0]), static_cast<double>(ang[1]),
                  static_cast<double>(ang[2]));
    return entityKeySetzen(i, "angles", b);
}

}  // namespace

// --- Schreiben -------------------------------------------------------------------

bool kameraVorschauSchreiben() {
    const bool verschoben =
        g_app->gizmoOffset[0] != 0.0F || g_app->gizmoOffset[1] != 0.0F || g_app->gizmoOffset[2] != 0.0F;
    const bool gedreht =
        g_app->gizmoAngles[0] != 0.0F || g_app->gizmoAngles[1] != 0.0F || g_app->gizmoAngles[2] != 0.0F;
    if (!verschoben && !gedreht) { return false; }
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, g_app->selectedKey)) {
        vorschauVerwerfen();
        return false;
    }
    const KameraSchluessel& k = ks[static_cast<std::size_t>(g_app->selectedKey)];
    bool ok = true;
    if (verschoben) {
        if (!k.marke.empty()) {
            float neu[3];
            for (int a = 0; a < 3; ++a) { neu[a] = k.pos[a] + g_app->gizmoOffset[a]; }
            ok = markeOrt(k.marke, neu);
            for (int a = 0; a < 3; ++a) { g_app->gizmoOffset[a] = 0.0F; }
        } else {
            ok = gizmoWriteBack();
        }
    }
    if (gedreht) {
        if (!k.panMarke.empty()) {
            float neu[3];
            for (int a = 0; a < 3; ++a) { neu[a] = k.ang[a] + g_app->gizmoAngles[a]; }
            ok = markeWinkel(k.panMarke, neu) && ok;
            for (int a = 0; a < 3; ++a) { g_app->gizmoAngles[a] = 0.0F; }
        } else {
            ok = gizmoWriteAngles() && ok;
        }
    }
    vorschauVerwerfen();
    const bool nurMarke = (!verschoben || !k.marke.empty()) && (!gedreht || !k.panMarke.empty());
    if (nurMarke) {
        rebuildTree();
        g_app->camTrackValid = false;
        g_app->mapDirty = true;
    } else {
        nachSkriptAenderung();
    }
    if (ok) {
        g_app->gizmoDoneFade = 4.5F;
        setStatus(tr(Str::GizmoWrote), 0, 0);
    } else {
        setStatus(tr(Str::GizmoNoMove), 0, 0);
    }
    return ok;
}

bool kameraOrtSetzen(int key, const float ort[3]) {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, key)) { return false; }
    const KameraSchluessel& k = ks[static_cast<std::size_t>(key)];
    const int vorher = g_app->selectedKey;
    g_app->selectedKey = key;
    vorschauVerwerfen();
    for (int a = 0; a < 3; ++a) { g_app->gizmoOffset[a] = ort[a] - k.pos[a]; }
    const bool ok = kameraVorschauSchreiben();
    g_app->selectedKey = vorher;
    return ok;
}

bool kameraWinkelSetzen(int key, const float ang[3]) {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, key)) { return false; }
    const KameraSchluessel& k = ks[static_cast<std::size_t>(key)];
    const int vorher = g_app->selectedKey;
    g_app->selectedKey = key;
    vorschauVerwerfen();
    for (int a = 0; a < 3; ++a) { g_app->gizmoAngles[a] = ang[a] - (k.hasAng ? k.ang[a] : 0.0F); }
    // Genau 0 hiesse "nichts zu tun" - ein PAN fehlt aber vielleicht noch.
    if (!k.hasAng && g_app->gizmoAngles[0] == 0.0F && g_app->gizmoAngles[1] == 0.0F &&
        g_app->gizmoAngles[2] == 0.0F) {
        g_app->gizmoAngles[1] = 0.0001F;
    }
    const bool ok = kameraVorschauSchreiben();
    g_app->selectedKey = vorher;
    return ok;
}

bool kameraDauerSetzen(int key, double ms) {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, key)) { return false; }
    const Node* alt = nodeAt(g_app->doc.script(), ks[static_cast<std::size_t>(key)].path);
    if (alt == nullptr || alt->args.size() < 3) { return false; }
    Node neu = *alt;
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", std::max(0.0, ms));
    if (neu.args[2].text == b) { return false; }
    neu.args[2].text = b;
    if (!g_app->doc.replaceAt(ks[static_cast<std::size_t>(key)].path, neu)) { return false; }
    nachSkriptAenderung();
    return true;
}

// Die Einstellung dorthin, wo man gerade hinschaut. Beides in EINEM Schritt,
// wenn MOVE und PAN Zahlen haben.
bool kameraAufAnsicht(int key) {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, key)) { return false; }
    const KameraSchluessel& k = ks[static_cast<std::size_t>(key)];
    const Node* mv = nodeAt(g_app->doc.script(), k.path);
    const Node* pn = k.panPath.empty() ? nullptr : nodeAt(g_app->doc.script(), k.panPath);
    if (k.marke.empty() && k.panMarke.empty() && mv != nullptr && pn != nullptr && mv->args.size() >= 2 &&
        pn->args.size() >= 2) {
        Node m2 = *mv;
        Node p2 = *pn;
        m2.args[1].kind = Arg::Kind::Vector;
        m2.args[1].text = vek(g_app->cam.pos);
        p2.args[1].kind = Arg::Kind::Vector;
        p2.args[1].text = vek(g_app->cam.angles);
        if (!g_app->doc.replaceMany({{k.path, m2}, {k.panPath, p2}})) { return false; }
        vorschauVerwerfen();
        nachSkriptAenderung();
        g_app->gizmoDoneFade = 4.5F;
        setStatus(tr(Str::GizmoWrote), 0, 0);
        return true;
    }
    const bool a = kameraOrtSetzen(key, g_app->cam.pos);
    const bool b = kameraWinkelSetzen(key, g_app->cam.angles);
    return a || b;
}

// --- Auswaehlen ----------------------------------------------------------------

void kameraSchluesselWaehlen(int key, bool zeitMit) {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, key)) { return; }
    g_app->cameraSelected = true;
    g_app->selectedActor = -1;
    g_app->pickedEntity = -1;
    g_app->selectedKey = key;
    vorschauVerwerfen();
    if (zeitMit) {
        const double t = startZeit(ks[static_cast<std::size_t>(key)].path);
        if (t >= 0.0) {
            // Ans ENDE der Fahrt: dort steht die Kamera auf der Einstellung.
            g_app->playMs = t + moveDauer(ks[static_cast<std::size_t>(key)].path);
            g_app->playing = false;
        }
    }
    g_app->gizmoZeitBezug = g_app->playMs;
    selectByPath(ks[static_cast<std::size_t>(key)].path);
    g_app->mapDirty = true;
}

void kameraNeueEinstellung() {
    // Hinter die gewaehlte Einstellung (hinter ihr PAN), sonst hinter die
    // gewaehlte Zeile - wie bisher.
    const std::vector<KameraSchluessel> vorher = schluessel();
    if (g_app->cameraSelected && gueltig(vorher, g_app->selectedKey)) {
        const KameraSchluessel& k = vorher[static_cast<std::size_t>(g_app->selectedKey)];
        g_app->selectedPath = k.panPath.empty() ? k.path : k.panPath;
    }
    const Path alt = g_app->selectedPath;
    insertCameraHere();
    if (g_app->selectedPath == alt) { return; }   // nichts eingefuegt
    const std::vector<KameraSchluessel> ks = schluessel();
    for (std::size_t i = 0; i < ks.size(); ++i) {
        if (ks[i].panPath == g_app->selectedPath) {
            g_app->cameraSelected = true;
            g_app->selectedActor = -1;
            g_app->pickedEntity = -1;
            g_app->selectedKey = static_cast<int>(i);
            g_app->gizmoZeitBezug = g_app->playMs;
            break;
        }
    }
    vorschauVerwerfen();
    nachSkriptAenderung();
    meldung(tr(Str::CamShotAdded), "");
}

bool kameraEinstellungLoeschen(int key) {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, key)) { return false; }
    const KameraSchluessel& k = ks[static_cast<std::size_t>(key)];
    std::vector<Path> weg{k.path};
    if (!k.panPath.empty()) { weg.push_back(k.panPath); }
    if (!g_app->doc.removeAll(weg)) { return false; }
    g_app->selectedPath.clear();
    g_app->selection.clear();
    const int rest = static_cast<int>(ks.size()) - 1;
    g_app->selectedKey = rest <= 0 ? -1 : std::min(key, rest - 1);
    if (rest <= 0) { g_app->cameraSelected = false; }
    vorschauVerwerfen();
    nachSkriptAenderung();
    return true;
}

// --- Steuern (Pilot) -------------------------------------------------------------
//
// Wie "Pilot" in Unreal und "Kamera an Ansicht binden" in Blender: man sitzt in
// der Einstellung, fliegt frei (rechte Maustaste + WASD) und uebernimmt dann.

void kameraPilotStarten() {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, g_app->selectedKey)) { return; }
    const KameraSchluessel& k = ks[static_cast<std::size_t>(g_app->selectedKey)];
    g_app->pilotVorher = g_app->cam;
    g_app->pilotSchluessel = g_app->selectedKey;
    for (int a = 0; a < 3; ++a) {
        g_app->cam.pos[a] = k.pos[a];
        g_app->cam.angles[a] = k.hasAng ? k.ang[a] : g_app->cam.angles[a];
    }
    g_app->throughCamera = false;
    g_app->kameraPilot = true;
    vorschauVerwerfen();
    g_app->mapDirty = true;
}

void kameraPilotBeenden(bool uebernehmen) {
    if (!g_app->kameraPilot) { return; }
    g_app->kameraPilot = false;
    if (uebernehmen) {
        kameraAufAnsicht(g_app->pilotSchluessel);
        g_app->selectedKey = g_app->pilotSchluessel;
    } else {
        g_app->cam = g_app->pilotVorher;
    }
    g_app->mapDirty = true;
}

// --- In der Ansicht: Hervorheben, Namensschild, Leiste --------------------------

namespace {

void schild(ImDrawList* dl, ImVec2 an, const std::string& oben, const std::string& unten, ImU32 farbe) {
    const float fs = ImGui::GetFontSize();
    const ImVec2 t1 = ImGui::CalcTextSize(oben.c_str());
    const ImVec2 t2 = unten.empty() ? ImVec2{0.0F, 0.0F} : ImGui::CalcTextSize(unten.c_str());
    const float b = std::max(t1.x, t2.x * 0.85F) + fs * 0.8F;
    const float h = t1.y + (unten.empty() ? 0.0F : t2.y * 0.85F) + fs * 0.4F;
    const ImVec2 a{an.x - b * 0.5F, an.y - h - fs * 1.6F};
    dl->AddRectFilled(a, ImVec2{a.x + b, a.y + h}, IM_COL32(20, 22, 28, 215), fs * 0.3F);
    dl->AddRect(a, ImVec2{a.x + b, a.y + h}, farbe, fs * 0.3F);
    dl->AddText(ImVec2{a.x + fs * 0.4F, a.y + fs * 0.2F}, farbe, oben.c_str());
    if (!unten.empty()) {
        dl->AddText(ImGui::GetFont(), fs * 0.85F, ImVec2{a.x + fs * 0.4F, a.y + fs * 0.2F + t1.y},
                    IM_COL32(190, 195, 205, 255), unten.c_str());
    }
}

std::string einstellungsName(int key, int anzahl) {
    char t[96];
    std::snprintf(t, sizeof(t), tr(Str::CamShotOf), key + 1, anzahl);
    return t;
}

}  // namespace

void kameraUeberlagerung(float rasterSkal) {
    const ImGuiIO& io = ImGui::GetIO();
    const float x0 = g_app->kartenAnsicht[0];
    const float y0 = g_app->kartenAnsicht[1];
    const float x1 = g_app->kartenAnsicht[2];
    const float y1 = g_app->kartenAnsicht[3];
    const bool imBild = io.MousePos.x >= x0 && io.MousePos.x < x1 && io.MousePos.y >= y0 && io.MousePos.y < y1 &&
                        ImGui::IsWindowHovered();
    const float fs = ImGui::GetFontSize();
    const float skal = rasterSkal > 0.0F ? rasterSkal : 1.0F;
    const ImVec2 kamSchirm{x0 + g_app->cameraSx / skal, y0 + g_app->cameraSy / skal};
    const std::vector<KameraSchluessel> ks = schluessel();

    // --- Ueberfahren ------------------------------------------------------
    bool hover = false;
    if (imBild && g_app->cameraSx >= 0.0F && !g_app->camViewActive && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
        g_app->gizmoAxis < 0 && !g_app->gizmoRotating && g_app->entHover < 0 && g_app->entGizmoHover == 0 &&
        !(g_app->gizmoShown && g_app->gizmoHover >= 0)) {
        const float dx = io.MousePos.x - kamSchirm.x;
        const float dy = io.MousePos.y - kamSchirm.y;
        const float weit = fs * 1.2F * 2.5F / skal;
        hover = dx * dx + dy * dy < weit * weit;
    }
    if (hover != g_app->kameraHover) {
        g_app->kameraHover = hover;
        g_app->mapDirty = true;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ImVec2{x0, y0}, ImVec2{x1, y1}, true);
    if ((hover || (g_app->cameraSelected && g_app->gizmoAxis < 0 && !g_app->gizmoRotating)) &&
        g_app->cameraSx >= 0.0F && !g_app->camViewActive && !ks.empty()) {
        const int k = g_app->cameraSelected && gueltig(ks, g_app->selectedKey) ? g_app->selectedKey
                                                                                : schluesselFuerZeit(g_app->playMs);
        std::string unten;
        if (gueltig(ks, k)) {
            const KameraSchluessel& s = ks[static_cast<std::size_t>(k)];
            char z[160];
            std::snprintf(z, sizeof(z), "%.0f %.0f %.0f", static_cast<double>(s.pos[0]), static_cast<double>(s.pos[1]),
                          static_cast<double>(s.pos[2]));
            unten = z;
            if (!s.marke.empty()) { unten += "  \xC2\xB7  ref_tag " + s.marke; }
        }
        schild(dl, kamSchirm, std::string(tr(Str::CamName)) + "  \xC2\xB7  " + einstellungsName(std::max(k, 0), static_cast<int>(ks.size())),
               unten, g_app->cameraSelected ? IM_COL32(255, 165, 40, 255) : IM_COL32(235, 238, 245, 255));
    }
    dl->PopClipRect();

    // --- Pilot: Hinweisband oben -------------------------------------------
    if (g_app->kameraPilot) {
        const ImVec2 merk = ImGui::GetCursorScreenPos();
        const std::string text = std::string(tr(Str::CamPiloting)) + "  " +
                                 einstellungsName(g_app->pilotSchluessel, static_cast<int>(ks.size()));
        const float b = ImGui::CalcTextSize(text.c_str()).x + fs * 14.0F;
        ImGui::SetCursorScreenPos(ImVec2{(x0 + x1 - b) * 0.5F, y0 + fs * 0.6F});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.55F, 0.20F, 0.55F, 0.92F});
        if (ImGui::BeginChild("##kamerapilot", ImVec2{b, 0.0F}, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar)) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(text.c_str());
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tr(Str::CamPilotHint)); }
            ImGui::SameLine();
            if (iconTextKnopf(tr(Str::CamPilotApply), UiIcon::Haken, tr(Str::CamPilotHint))) { kameraPilotBeenden(true); }
            ImGui::SameLine();
            if (iconTextKnopf(tr(Str::CamPilotCancel), UiIcon::Schliessen, nullptr) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                kameraPilotBeenden(false);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(merk);
        ImGui::Dummy(ImVec2{0.0F, 0.0F});   // Anker: ImGui will nach dem Cursorsprung ein Element
        return;
    }

    // --- Die Leiste der gewaehlten Kamera ------------------------------------
    if (!g_app->cameraSelected || ks.empty()) { return; }
    const ImVec2 merk = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2{x0 + fs * 0.5F, y0 + fs * 0.5F});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.10F, 0.11F, 0.14F, 0.88F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{fs * 0.3F, fs * 0.25F});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{fs * 0.2F, fs * 0.2F});
    if (ImGui::BeginChild("##kameraleiste", ImVec2{0.0F, 0.0F},
                          ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders |
                              ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar)) {
        const float luecke = fs * 0.6F;
        const bool dreht = g_app->gizmoMode == App::GizmoMode::Rotate;
        if (iconKnopf(tr(Str::GizmoMove), UiIcon::Verschieben, tr(Str::CamMoveHint),
                      g_app->gizmoMode == App::GizmoMode::Move, true, true)) {
            g_app->gizmoMode = App::GizmoMode::Move;
            g_app->mapDirty = true;
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::GizmoRotate), UiIcon::Drehen, tr(Str::CamRotateHint), dreht, true, true)) {
            g_app->gizmoMode = App::GizmoMode::Rotate;
            g_app->mapDirty = true;
        }
        ImGui::SameLine();
        {
            App::GizmoSpace& raum = dreht ? g_app->gizmoRotSpace : g_app->gizmoMoveSpace;
            const bool welt = raum == App::GizmoSpace::World;
            if (ImGui::Button(tr(welt ? Str::GizmoSpaceWorld : Str::GizmoSpaceLocal))) {
                raum = welt ? App::GizmoSpace::Local : App::GizmoSpace::World;
                g_app->mapDirty = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::CamSpaceHint)); }
        }
        ImGui::SameLine(0.0F, luecke);
        const int n = static_cast<int>(ks.size());
        const int k = std::clamp(g_app->selectedKey, 0, n - 1);
        if (iconKnopf(tr(Str::CamPrevShot), UiIcon::Zurueck, nullptr, false, k > 0)) { kameraSchluesselWaehlen(k - 1, true); }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(einstellungsName(k, n).c_str());
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamNextShot), UiIcon::Vor, nullptr, false, k + 1 < n)) { kameraSchluesselWaehlen(k + 1, true); }
        ImGui::SameLine(0.0F, luecke);
        if (iconKnopf(tr(Str::CamSetToView), UiIcon::Zielen, tr(Str::CamSetToViewHint), false, !g_app->camViewActive)) {
            kameraAufAnsicht(k);
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamPilot), UiIcon::Steuern, tr(Str::CamPilotHint))) {
            g_app->selectedKey = k;
            kameraPilotStarten();
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::MapThroughCam), UiIcon::Kamera, tr(Str::MapThroughCamHint), g_app->throughCamera, true, true)) {
            g_app->throughCamera = !g_app->throughCamera;
            g_app->mapDirty = true;
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamPreview), UiIcon::Bildschirm, tr(Str::CamPreviewHint), g_app->kameraBildImBild, true,
                      true)) {
            g_app->kameraBildImBild = !g_app->kameraBildImBild;
            g_app->mapDirty = true;
        }
        ImGui::SameLine(0.0F, luecke);
        if (iconKnopf(tr(Str::CamNewShot), UiIcon::Plus, tr(Str::CamNewShotHint))) { kameraNeueEinstellung(); }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamDeleteShot), UiIcon::Loeschen, nullptr)) { kameraEinstellungLoeschen(k); }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(merk);
    ImGui::Dummy(ImVec2{0.0F, 0.0F});   // Anker: ImGui will nach dem Cursorsprung ein Element
}

// --- Bild-im-Bild (rc592) ------------------------------------------------------
//
// Unten rechts in der Ansicht, mit dem Rahmen in der Farbe der gewaehlten
// Kamera. Ein Klick schaut durch die Kamera (wie der Knopf daneben).
void kameraVorschauZeigen() {
    if (!g_app->vorschauGezeigt || g_app->camViewActive) { return; }
    void* tex = gpu::zielTexturSatz(1);
    if (tex == nullptr || g_app->vorschauW <= 0 || g_app->vorschauH <= 0) { return; }
    const float x0 = g_app->kartenAnsicht[0];
    const float y0 = g_app->kartenAnsicht[1];
    const float x1 = g_app->kartenAnsicht[2];
    const float y1 = g_app->kartenAnsicht[3];
    const float fs = ImGui::GetFontSize();
    // Auf dem Schirm so breit wie ein Drittel der Ansicht - unabhaengig
    // davon, wie fein gerastert wurde.
    const float b = std::clamp((x1 - x0) / 3.0F, 120.0F, 640.0F);
    const float h = b * static_cast<float>(g_app->vorschauH) / static_cast<float>(g_app->vorschauW);
    if (b > (x1 - x0) * 0.6F || h > (y1 - y0) * 0.6F) { return; }   // zu wenig Platz
    const ImVec2 p1{x1 - fs * 0.6F, y1 - fs * 0.6F};
    const ImVec2 p0{p1.x - b, p1.y - h};
    // Ein eigenes Kindfenster wie die Kameraleiste: solange die Maus darauf
    // steht, gilt die Kartenansicht als nicht ueberfahren - ein Klick waehlt
    // dann nicht zugleich etwas dahinter aus.
    const ImVec2 merk = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(p0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0F);
    if (ImGui::BeginChild("##kameravorschau", ImVec2{b, h}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::Image(reinterpret_cast<ImTextureID>(tex), ImVec2{b, h});
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            g_app->throughCamera = true;
            g_app->mapDirty = true;
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tr(Str::CamPreviewClick)); }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(ImVec2{p0.x + 1.0F, p0.y + 1.0F}, ImVec2{p1.x - 1.0F, p1.y - 1.0F}, IM_COL32(255, 165, 40, 255), 3.0F,
                    0, 2.0F);
        const std::vector<KameraSchluessel> ks = schluessel();
        const int k = gueltig(ks, g_app->selectedKey) ? g_app->selectedKey : schluesselFuerZeit(g_app->playMs);
        const std::string titel = std::string(tr(Str::CamName)) + "  \xC2\xB7  " +
                                  einstellungsName(std::max(k, 0), static_cast<int>(ks.size()));
        const ImVec2 ts = ImGui::CalcTextSize(titel.c_str());
        dl->AddRectFilled(ImVec2{p0.x + 2.0F, p0.y + 2.0F}, ImVec2{p0.x + ts.x + fs * 0.8F, p0.y + ts.y + fs * 0.4F},
                          IM_COL32(12, 13, 16, 200));
        dl->AddText(ImVec2{p0.x + fs * 0.4F, p0.y + fs * 0.2F}, IM_COL32(255, 200, 120, 255), titel.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(merk);
    ImGui::Dummy(ImVec2{0.0F, 0.0F});   // Anker nach dem Cursorsprung
}

// --- Inspektor -----------------------------------------------------------------

void drawKameraInspector(float hoehe) {
    ImGui::BeginChild("kamerainspektor", ImVec2{-1.0F, hoehe}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const std::vector<KameraSchluessel> ks = schluessel();
    const int n = static_cast<int>(ks.size());
    const int k = n == 0 ? -1 : std::clamp(g_app->selectedKey, 0, n - 1);
    const float g = ImGui::GetFontSize();
    const ImVec4 farbe = uiIconFarbe(UiIcon::Kamera);
    {
        const ImVec2 q = ImGui::GetCursorScreenPos();
        uiIcon(ImGui::GetWindowDrawList(), UiIcon::Kamera, q, g, ImGui::GetColorU32(farbe));
        ImGui::Dummy(ImVec2{g, g});
        ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x);
    }
    ImGui::TextColored(farbe, "%s", tr(Str::CamName));
    if (k >= 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", einstellungsName(k, n).c_str());
    }
    // Icon-Leiste wie beim Entity-Inspektor.
    {
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{sp * 0.4F, ImGui::GetStyle().ItemSpacing.y});
        if (iconKnopf(tr(Str::CamFlyToShot), UiIcon::Hinfliegen, nullptr, false, k >= 0)) {
            const KameraSchluessel& s = ks[static_cast<std::size_t>(k)];
            for (int a = 0; a < 3; ++a) {
                g_app->cam.pos[a] = s.pos[a];
                if (s.hasAng) { g_app->cam.angles[a] = s.ang[a]; }
            }
            g_app->throughCamera = false;
            g_app->mapDirty = true;
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamSetToView), UiIcon::Zielen, tr(Str::CamSetToViewHint), false, k >= 0)) { kameraAufAnsicht(k); }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamPilot), UiIcon::Steuern, tr(Str::CamPilotHint), false, k >= 0)) {
            g_app->selectedKey = k;
            kameraPilotStarten();
        }
        ImGui::SameLine(0.0F, sp * 1.2F);
        if (iconKnopf(tr(Str::CamNewShot), UiIcon::Plus, tr(Str::CamNewShotHint))) { kameraNeueEinstellung(); }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamDeleteShot), UiIcon::Loeschen, nullptr, false, k >= 0)) { kameraEinstellungLoeschen(k); }
        ImGui::PopStyleVar();
    }
    ImGui::Separator();
    ImGui::BeginChild("kamerafelder", ImVec2{-1.0F, -1.0F}, ImGuiChildFlags_None);
    // --- Einstellungen ------------------------------------------------------
    if (ImGui::CollapsingHeader(tr(Str::CamShots), ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < n; ++i) {
            const KameraSchluessel& s = ks[static_cast<std::size_t>(i)];
            const double t = startZeit(s.path);
            const double d = moveDauer(s.path);
            char z[200];
            int len = std::snprintf(z, sizeof(z), "%s", einstellungsName(i, n).c_str());
            if (t >= 0.0) {
                const int zs = static_cast<int>(t / 100.0);
                len += std::snprintf(z + len, sizeof(z) - static_cast<std::size_t>(len), "   %02d:%02d.%d", zs / 600,
                                     (zs / 10) % 60, zs % 10);
            }
            if (d > 0.5) {
                len += std::snprintf(z + len, sizeof(z) - static_cast<std::size_t>(len), tr(Str::CamMoving), d / 1000.0);
            }
            if (!s.marke.empty()) {
                std::snprintf(z + len, sizeof(z) - static_cast<std::size_t>(len), "   ref_tag %s", s.marke.c_str());
            }
            ImGui::PushID(i);
            if (ImGui::Selectable(z, i == k)) { kameraSchluesselWaehlen(i, true); }
            ImGui::PopID();
        }
        if (n == 0) {
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextDisabled("%s", tr(Str::CamNoShots));
            ImGui::PopTextWrapPos();
        }
    }
    // --- Die gewaehlte Einstellung --------------------------------------------
    if (k >= 0 && ImGui::CollapsingHeader(tr(Str::CamSelectedShot), ImGuiTreeNodeFlags_DefaultOpen)) {
        const KameraSchluessel& s = ks[static_cast<std::size_t>(k)];
        const float lw = g * 5.5F;
        // Werte bleiben waehrend des Ziehens hier stehen und gehen erst beim
        // Loslassen ins Skript - ein Rueckgaengig-Schritt je Aenderung.
        static float ort[3];
        static float ang[3];
        static float dauer = 0.0F;
        static int fuer = -1;
        static bool ortAktiv = false;
        static bool angAktiv = false;
        static bool dauerAktiv = false;
        if (fuer != k || !ortAktiv) { std::copy(s.pos, s.pos + 3, ort); }
        if (fuer != k || !angAktiv) { std::copy(s.ang, s.ang + 3, ang); }
        if (fuer != k || !dauerAktiv) { dauer = static_cast<float>(moveDauer(s.path) / 1000.0); }
        fuer = k;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(Str::CamPosition));
        ImGui::SameLine(lw);
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::DragFloat3("##ort", ort, 1.0F, 0.0F, 0.0F, "%.0f");
        ortAktiv = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) { kameraOrtSetzen(k, ort); }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(Str::CamAngles));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::CamAnglesHint)); }
        ImGui::SameLine(lw);
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::DragFloat3("##winkel", ang, 0.5F, 0.0F, 0.0F, "%.1f");
        angAktiv = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) { kameraWinkelSetzen(k, ang); }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(Str::CamDuration));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::CamDurationHint)); }
        ImGui::SameLine(lw);
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::BeginDisabled(!s.marke.empty() && nodeAt(g_app->doc.script(), s.path) == nullptr);
        ImGui::DragFloat("##dauer", &dauer, 0.05F, 0.0F, 120.0F, "%.2f s");
        dauerAktiv = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) { kameraDauerSetzen(k, static_cast<double>(dauer) * 1000.0); }
        ImGui::EndDisabled();
        if (!s.marke.empty()) {
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextDisabled(tr(Str::CamOnTag), s.marke.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

}  // namespace bhed::gui
