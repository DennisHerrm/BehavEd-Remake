// app_ansichten.cpp - drei Ansichten des Script Flow: Classic, Graph, Spuren
//
// Gewuenscht (11.10.2026): "ich finde alle gut kannst du alle 3 modi
// einbauen und testen? so das alle 3 gehen und man das benutzen kann was
// einem am liebsten gefaellt" - und "das man wenn man will in alle 3
// arbeiten kann durch umschalten".
//
//   Classic  der Skriptbaum wie in BehavEd 2002 (drawTree, unveraendert)
//   Graph    der Missionsfluss: Entities und Skripte als Knoten, was was
//            ausloest als Verbindung (bhed/missionsgraph.h). Vorbild Unreals
//            Blueprint und Hammers Inputs/Outputs.
//   Spuren   das offene Skript ueber die Zeit: Kamera, je Figur eine Spur,
//            Dialog. Vorbild Unreals Sequencer.
//
// Alle drei arbeiten auf DENSELBEN Daten - dem Dokument des Reiters, den
// Entities der Karte, demselben Rueckgaengig-Verlauf und derselben Auswahl
// (selectedPath). Umschalten verliert nichts.
#include "app_internal.h"

#include "bhed/missionsgraph.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace bhed::gui {
namespace {

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

}  // namespace

// --- Der Umschalter (Kopfzeile von Script Flow) --------------------------------
void ansichtsUmschalter() {
    const Str namen[3] = {Str::FlowClassic, Str::FlowGraph, Str::FlowTracks};
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ImGui::GetFontSize() * 0.55F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2.0F, 0.0F});
    for (int k = 0; k < 3; ++k) {
        if (k > 0) { ImGui::SameLine(); }
        const bool an = g_app->settings.flussAnsicht == k;
        if (an) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(tr(namen[k])) && !an) {
            g_app->settings.flussAnsicht = k;
            diag::detail(std::string("Ansicht: ") + tr(namen[k]));
        }
        if (an) { ImGui::PopStyleColor(); }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::FlowViewHint)); }
    }
    ImGui::PopStyleVar(2);
}

// ============================================================================
// Graph
// ============================================================================
namespace {

constexpr float kKnotenB = 190.0F;
constexpr float kKnotenH = 62.0F;

ImVec4 artFarbe(GraphKnoten::Art a) {
    switch (a) {
        case GraphKnoten::Art::Spielerstart: return uiIconFarbe(UiIcon::Mission);
        case GraphKnoten::Art::Skriptstarter: return uiIconFarbe(UiIcon::Liste);
        case GraphKnoten::Art::Skript: return uiIconFarbe(UiIcon::Neu);
        case GraphKnoten::Art::Figur: return uiIconFarbe(UiIcon::Figur);
        case GraphKnoten::Art::Kartenwechsel: return uiIconFarbe(UiIcon::Karte);
        case GraphKnoten::Art::Ausloeser: return uiIconFarbe(UiIcon::Effekt);
        case GraphKnoten::Art::Entity: return uiIconFarbe(UiIcon::Entity);
    }
    return ImVec4{0.8F, 0.8F, 0.8F, 1.0F};
}

UiIcon artIcon(GraphKnoten::Art a) {
    switch (a) {
        case GraphKnoten::Art::Spielerstart: return UiIcon::Mission;
        case GraphKnoten::Art::Skriptstarter: return UiIcon::Effekt;
        case GraphKnoten::Art::Skript: return UiIcon::Skript;
        case GraphKnoten::Art::Figur: return UiIcon::Figur;
        case GraphKnoten::Art::Kartenwechsel: return UiIcon::Karte;
        case GraphKnoten::Art::Ausloeser: return UiIcon::Hinfliegen;
        case GraphKnoten::Art::Entity: return UiIcon::Entity;
    }
    return UiIcon::Entity;
}

// Woran erkennt man, dass der Graph neu gebaut werden muss?
std::string graphSignatur() {
    std::string s = g_app->map.path + "|" + std::to_string(g_app->map.entities.size());
    std::size_t schluessel = 0;
    for (const MapEntity& e : g_app->map.entities) { schluessel += e.keys.size() * 31U + e.targetname.size(); }
    s += "|" + std::to_string(schluessel) + "|" + std::to_string(g_app->entUndo.size()) + "|" + std::to_string(g_app->entRedo.size());
    for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
        const bool aktiv = static_cast<int>(i) == g_app->activeTab;
        const Document& d = aktiv ? g_app->doc : g_app->tabs[i].doc;
        s += "|" + std::to_string(d.undoDepth()) + "/" + std::to_string(d.redoDepth()) + "/" +
             std::to_string(d.script().nodes.size());
    }
    return s + "|" + aktiverSkriptRef();
}

const Script* skriptFuerGraph(const std::string& ref) {
    if (const int r = reiterFuerSkriptRef(ref); r >= 0) {
        return r == g_app->activeTab ? &g_app->doc.script() : &g_app->tabs[static_cast<std::size_t>(r)].doc.script();
    }
    App::GraphZustand& z = g_app->graph;
    const std::string k = klein(ref);
    if (const auto it = z.skriptCache.find(k); it != z.skriptCache.end()) {
        return it->second.get();
    }
    std::string text;
    if (!skriptTextLaden(ref, text)) {
        z.skriptCache[k] = nullptr;
        return nullptr;
    }
    auto sc = std::make_shared<Script>();
    std::vector<Diag> d;
    (void)readScript(text, *sc, d);
    const Script* p = sc.get();
    z.skriptCache[k] = std::move(sc);
    return p;
}

void graphNeuBauen() {
    App::GraphZustand& z = g_app->graph;
    if (z.cacheKarte != g_app->map.path) {
        z.skriptCache.clear();
        z.pos.clear();
        z.cacheKarte = g_app->map.path;
        z.angepasst = false;
    }
    z.g = baueMissionsGraph(g_app->map.entities, skriptFuerGraph, aktiverSkriptRef());
    const float s = ImGui::GetFontSize() / 13.0F;
    ordneGraph(z.g, kKnotenB * s, kKnotenH * s, 80.0F * s, 26.0F * s);
    for (GraphKnoten& k : z.g.knoten) {
        if (const auto it = z.pos.find(k.schluessel); it != z.pos.end()) {
            k.x = it->second.x;
            k.y = it->second.y;
        }
    }
    diag::detail("Graph: " + std::to_string(z.g.knoten.size()) + " Knoten, " + std::to_string(z.g.kanten.size()) +
                 " Verbindungen" + (z.g.gekuerzt ? " (gekuerzt)" : ""));
}

int knotenIndex(const std::string& schluessel) {
    const auto& v = g_app->graph.g.knoten;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i].schluessel == schluessel) { return static_cast<int>(i); }
    }
    return -1;
}

}  // namespace

// Alles in den Blick nehmen.
void graphEinpassen(float breite, float hoehe) {
    App::GraphZustand& z = g_app->graph;
    if (z.g.knoten.empty()) { return; }
    const float s = ImGui::GetFontSize() / 13.0F;
    float x0 = 1e9F;
    float y0 = 1e9F;
    float x1 = -1e9F;
    float y1 = -1e9F;
    for (const GraphKnoten& k : z.g.knoten) {
        x0 = std::min(x0, k.x);
        y0 = std::min(y0, k.y);
        x1 = std::max(x1, k.x + kKnotenB * s);
        y1 = std::max(y1, k.y + kKnotenH * s);
    }
    const float rand = 30.0F * s;
    z.zoom = std::clamp(std::min((breite - rand * 2.0F) / std::max(x1 - x0, 1.0F), (hoehe - rand * 2.0F) / std::max(y1 - y0, 1.0F)),
                        0.55F, 1.25F);   // nie so klein, dass die Schrift verschwindet
    z.panX = rand - x0 * z.zoom + std::max(0.0F, (breite - rand * 2.0F - (x1 - x0) * z.zoom) * 0.5F);
    z.panY = rand - y0 * z.zoom;
}

// Zwei Knoten verbinden: setzt target / *script oder fuegt use / run ins Skript.
// Gibt eine Meldung fuer den Anwender zurueck, leer wenn es nichts zu tun gab.
std::string graphVerbinden(const std::string& vonSchluessel, const std::string& nachSchluessel, const std::string& art) {
    const int a = knotenIndex(vonSchluessel);
    const int b = knotenIndex(nachSchluessel);
    if (a < 0 || b < 0 || a == b) { return {}; }
    const GraphKnoten von = g_app->graph.g.knoten[static_cast<std::size_t>(a)];
    const GraphKnoten nach = g_app->graph.g.knoten[static_cast<std::size_t>(b)];
    char t[400];
    if (von.entity >= 0 && nach.entity >= 0) {
        // Entity -> Entity: target. Das Ziel braucht einen targetname.
        MapEntity& z = g_app->map.entities[static_cast<std::size_t>(nach.entity)];
        std::string name = z.targetname;
        if (name.empty()) {
            name = "ziel_" + (nach.titel.empty() ? std::string("x") : nach.titel);
            for (char& c : name) {
                if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') { c = '_'; }
            }
            entityKeySetzen(nach.entity, "targetname", name);
        }
        entityKeySetzen(von.entity, "target", name);
        std::snprintf(t, sizeof(t), tr(Str::GraphConnected), von.titel.c_str(), nach.titel.c_str(), "target");
        return t;
    }
    if (von.entity >= 0 && !nach.skript.empty()) {
        const std::string schl = art.empty() ? std::string("usescript") : art;
        entityKeySetzen(von.entity, schl, nach.skript);
        std::snprintf(t, sizeof(t), tr(Str::GraphConnected), von.titel.c_str(), nach.titel.c_str(), schl.c_str());
        return t;
    }
    if (!von.skript.empty()) {
        // Skript -> ...: einen Befehl ans Ende des Skripts.
        Node n;
        n.kind = Node::Kind::Command;
        Arg arg;
        arg.kind = Arg::Kind::String;
        if (!nach.skript.empty()) {
            n.name = "run";
            arg.text = nach.skript;
        } else {
            const MapEntity& z = g_app->map.entities[static_cast<std::size_t>(nach.entity)];
            n.name = "use";
            arg.text = z.targetname.empty() ? nach.titel : z.targetname;
        }
        n.args.push_back(arg);
        skriptOeffnen(von.skript, false);
        if (aktiverSkriptRef().empty() || klein(aktiverSkriptRef()) != klein(von.skript) || g_app->doc.script().nodes.empty()) {
            return {};
        }
        const Path letzte{g_app->doc.script().nodes.size() - 1};
        Path at;
        if (g_app->doc.insertAfterAll(letzte, {n}, &at)) {
            g_app->selectedPath = at;
            rebuildTree();
            g_app->camTrackValid = false;
        }
        std::snprintf(t, sizeof(t), tr(Str::GraphConnected), von.titel.c_str(), nach.titel.c_str(), n.name.c_str());
        return t;
    }
    return {};
}

void drawGraphAnsicht() {
    App::GraphZustand& z = g_app->graph;
    const std::string sig = graphSignatur();
    if (sig != z.signatur) {
        z.signatur = sig;
        graphNeuBauen();
    }
    const float fs = ImGui::GetFontSize();
    const float s = fs / 13.0F;
    const ImGuiStyle& st = ImGui::GetStyle();

    // --- Werkzeugleiste ----------------------------------------------------
    const ImVec2 voll = ImGui::GetContentRegionAvail();
    // Die Details rechts nur, wenn Platz ist; sonst kompakt darunter
    // (in der Kartenansicht ist die Spalte schmal).
    const bool detailsRechts = voll.x > fs * 42.0F;
    const float detailB = detailsRechts ? fs * 16.0F : 0.0F;
    const float detailUntenH = detailsRechts ? 0.0F : fs * 7.0F;
    const bool einpassen = iconTextKnopf(tr(Str::GraphFit), UiIcon::Hinfliegen, nullptr);
    ImGui::SameLine();
    if (iconTextKnopf(tr(Str::GraphRelayout), UiIcon::Verschieben, nullptr)) {
        z.pos.clear();
        z.signatur.clear();
        z.angepasst = false;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled(tr(z.g.gekuerzt ? Str::GraphCountCut : Str::GraphCount), static_cast<int>(z.g.knoten.size()),
                        static_cast<int>(z.g.kanten.size()));
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tr(Str::GraphHint)); }

    // --- Flaeche -----------------------------------------------------------
    const ImVec2 flaecheGr{std::max(fs * 10.0F, ImGui::GetContentRegionAvail().x - (detailsRechts ? detailB + st.ItemSpacing.x : 0.0F)),
                           std::max(fs * 6.0F, ImGui::GetContentRegionAvail().y - fs * 1.6F - detailUntenH)};
    if (!z.angepasst || einpassen) {
        graphEinpassen(flaecheGr.x, flaecheGr.y);
        z.angepasst = true;
    }
    const ImVec2 o = ImGui::GetCursorScreenPos();
    z.ursprung = o;
    z.groesse = flaecheGr;
    // Ohne Auswahl: das offene Skript waehlen - so sieht man gleich, wo man ist.
    if (z.auswahl.empty() && !aktiverSkriptRef().empty()) { z.auswahl = "s:" + klein(aktiverSkriptRef()); }
    ImGui::InvisibleButton("##graphflaeche", flaecheGr,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool ueber = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(o, ImVec2{o.x + flaecheGr.x, o.y + flaecheGr.y}, true);
    dl->AddRectFilled(o, ImVec2{o.x + flaecheGr.x, o.y + flaecheGr.y}, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0F);
    // Punktraster
    {
        const float raster = 24.0F * s * z.zoom;
        if (raster > 6.0F) {
            const ImU32 c = ImGui::GetColorU32(ImGuiCol_Border, 0.6F);
            for (float x = std::fmod(z.panX, raster); x < flaecheGr.x; x += raster) {
                for (float y = std::fmod(z.panY, raster); y < flaecheGr.y; y += raster) {
                    dl->AddRectFilled(ImVec2{o.x + x, o.y + y}, ImVec2{o.x + x + 1.5F, o.y + y + 1.5F}, c);
                }
            }
        }
    }
    const auto welt = [&](float x, float y) { return ImVec2{o.x + z.panX + x * z.zoom, o.y + z.panY + y * z.zoom}; };
    const float B = kKnotenB * s * z.zoom;
    const float H = kKnotenH * s * z.zoom;
    const ImVec2 m = ImGui::GetIO().MousePos;

    // Was liegt unter der Maus?
    int ueberKnoten = -1;
    bool ueberAusgang = false;
    if (ueber) {
        for (int i = static_cast<int>(z.g.knoten.size()) - 1; i >= 0; --i) {
            const GraphKnoten& k = z.g.knoten[static_cast<std::size_t>(i)];
            const ImVec2 a = welt(k.x, k.y);
            const ImVec2 aus{a.x + B, a.y + H * 0.5F};
            if ((m.x - aus.x) * (m.x - aus.x) + (m.y - aus.y) * (m.y - aus.y) < (7.0F * s) * (7.0F * s)) {
                ueberKnoten = i;
                ueberAusgang = true;
                break;
            }
            if (m.x >= a.x && m.x <= a.x + B && m.y >= a.y && m.y <= a.y + H) {
                ueberKnoten = i;
                break;
            }
        }
    }

    // --- Kanten (Bezier wie in Blueprint) --------------------------------------
    for (const GraphKante& k : z.g.kanten) {
        const GraphKnoten& a = z.g.knoten[static_cast<std::size_t>(k.von)];
        const GraphKnoten& b = z.g.knoten[static_cast<std::size_t>(k.nach)];
        const ImVec2 p0 = welt(a.x + kKnotenB * s, a.y + kKnotenH * s * 0.5F);
        const ImVec2 p3 = welt(b.x, b.y + kKnotenH * s * 0.5F);
        const float dx = std::max(40.0F * s * z.zoom, std::fabs(p3.x - p0.x) * 0.5F);
        const ImVec2 p1{p0.x + dx, p0.y};
        const ImVec2 p2{p3.x - dx, p3.y};
        const bool hervor = (!z.auswahl.empty() &&
                             (a.schluessel == z.auswahl || b.schluessel == z.auswahl));
        ImVec4 c = k.art == GraphKante::Art::Skript ? uiIconFarbe(UiIcon::Neu) : ImVec4{0.65F, 0.69F, 0.76F, 1.0F};
        c.w = hervor ? 1.0F : 0.75F;
        const float dicke = (hervor ? 2.6F : 1.6F) * s * std::max(0.6F, z.zoom);
        if (k.art == GraphKante::Art::Steuern) {
            // gestrichelt: in Stuecken der Kurve
            ImVec2 vorher = p0;
            for (int t = 1; t <= 24; ++t) {
                const float u = static_cast<float>(t) / 24.0F;
                const float v = 1.0F - u;
                const ImVec2 q{v * v * v * p0.x + 3 * v * v * u * p1.x + 3 * v * u * u * p2.x + u * u * u * p3.x,
                               v * v * v * p0.y + 3 * v * v * u * p1.y + 3 * v * u * u * p2.y + u * u * u * p3.y};
                if (t % 2 == 1) { dl->AddLine(vorher, q, ImGui::GetColorU32(c), dicke); }
                vorher = q;
            }
        } else {
            dl->AddBezierCubic(p0, p1, p2, p3, ImGui::GetColorU32(c), dicke);
        }
        const float sp = 7.0F * s * std::max(0.6F, z.zoom);
        dl->AddTriangleFilled(ImVec2{p3.x, p3.y}, ImVec2{p3.x - sp, p3.y - sp * 0.6F}, ImVec2{p3.x - sp, p3.y + sp * 0.6F},
                              ImGui::GetColorU32(c));
        if (z.zoom > 0.45F) {
            const ImVec2 mitte{(p0.x + 3 * p1.x + 3 * p2.x + p3.x) / 8.0F, (p0.y + 3 * p1.y + 3 * p2.y + p3.y) / 8.0F};
            const ImVec2 g = ImGui::CalcTextSize(k.label.c_str());
            dl->AddRectFilled(ImVec2{mitte.x - g.x * 0.5F - 3.0F, mitte.y - g.y * 0.5F - 1.0F},
                              ImVec2{mitte.x + g.x * 0.5F + 3.0F, mitte.y + g.y * 0.5F + 1.0F},
                              ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0F);
            dl->AddText(ImVec2{mitte.x - g.x * 0.5F, mitte.y - g.y * 0.5F}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                        k.label.c_str());
        }
    }

    // --- Knoten -------------------------------------------------------------------
    for (std::size_t i = 0; i < z.g.knoten.size(); ++i) {
        const GraphKnoten& k = z.g.knoten[i];
        const ImVec2 a = welt(k.x, k.y);
        const ImVec2 b{a.x + B, a.y + H};
        const ImVec4 farbe = artFarbe(k.art);
        const bool gewaehlt = k.schluessel == z.auswahl;
        const bool aktivSkript = !k.skript.empty() && klein(k.skript) == klein(aktiverSkriptRef());
        dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_WindowBg), 8.0F * s * z.zoom);
        ImVec4 rand = farbe;
        rand.w = static_cast<int>(i) == ueberKnoten ? 1.0F : 0.7F;
        dl->AddRect(a, b, ImGui::GetColorU32(gewaehlt ? ImVec4{1.0F, 0.65F, 0.16F, 1.0F} : rand), 8.0F * s * z.zoom, 0,
                    (gewaehlt ? 2.6F : (aktivSkript ? 2.0F : 1.2F)) * s);
        if (z.zoom > 0.3F) {
            ImGui::PushFont(nullptr, fs * std::clamp(z.zoom, 0.6F, 1.2F));
            // Icon und die beiden Zeilen als GRUPPE in die Mitte des Knotens -
            // linksbuendig blieb rechts eine leere Flaeche, und der Inhalt
            // wirkte verschoben (shank, 11.10.). Zu lange Namen werden
            // abgeschnitten statt umgebrochen.
            const float g = std::floor(16.0F * s * z.zoom);
            const float innen = 10.0F * s * z.zoom;
            const float luecke = 8.0F * s * z.zoom;
            const float platz = std::max(B - innen * 2.0F - g - luecke, 10.0F);
            const float tw = std::min(std::max(ImGui::CalcTextSize(k.titel.c_str()).x, ImGui::CalcTextSize(k.unter.c_str()).x), platz);
            const float gx = std::floor(a.x + std::max(innen, (B - (g + luecke + tw)) * 0.5F));
            uiIcon(dl, artIcon(k.art), ImVec2{gx, std::floor(a.y + (H - g) * 0.5F)}, g, ImGui::GetColorU32(farbe));
            const float tx = gx + g + luecke;
            const float zeile = ImGui::GetFontSize();
            dl->PushClipRect(ImVec2{tx, a.y}, ImVec2{std::min(tx + platz, b.x - innen * 0.5F), b.y}, true);
            dl->AddText(ImVec2{tx, std::floor(a.y + H * 0.5F - zeile)}, ImGui::GetColorU32(ImGuiCol_Text), k.titel.c_str());
            dl->AddText(ImVec2{tx, std::floor(a.y + H * 0.5F)}, ImGui::GetColorU32(ImGuiCol_TextDisabled), k.unter.c_str());
            dl->PopClipRect();
            ImGui::PopFont();
        }
        // Ausgang (zum Verbinden ziehen) und Eingang
        const ImVec2 aus{b.x, a.y + H * 0.5F};
        dl->AddCircleFilled(aus, 4.5F * s, ImGui::GetColorU32(static_cast<int>(i) == ueberKnoten && ueberAusgang
                                                                   ? ImVec4{1.0F, 0.9F, 0.4F, 1.0F}
                                                                   : farbe));
        dl->AddCircle(ImVec2{a.x, a.y + H * 0.5F}, 3.5F * s, ImGui::GetColorU32(farbe), 0, 1.4F * s);
    }

    // --- Bedienen -------------------------------------------------------------------
    const ImGuiIO& io = ImGui::GetIO();
    if (ueber && io.MouseWheel != 0.0F) {
        const float alt = z.zoom;
        z.zoom = std::clamp(z.zoom * (io.MouseWheel > 0.0F ? 1.12F : 1.0F / 1.12F), 0.2F, 2.0F);
        // um die Maus herum
        z.panX = (m.x - o.x) - ((m.x - o.x) - z.panX) * (z.zoom / alt);
        z.panY = (m.y - o.y) - ((m.y - o.y) - z.panY) * (z.zoom / alt);
    }
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
        z.panX += io.MouseDelta.x;
        z.panY += io.MouseDelta.y;
    }
    if (ueber && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (ueberKnoten >= 0) {
            const GraphKnoten& k = z.g.knoten[static_cast<std::size_t>(ueberKnoten)];
            z.auswahl = k.schluessel;
            if (ueberAusgang) {
                z.verbindetVon = k.schluessel;
            } else {
                z.zieht = k.schluessel;
            }
            // Auswahl abgleichen: eine Entity auch in der Karte waehlen.
            if (k.entity >= 0) { g_app->pickedEntity = k.entity; }
        } else {
            z.auswahl.clear();
        }
    }
    if (ueber && ueberKnoten >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const GraphKnoten k = z.g.knoten[static_cast<std::size_t>(ueberKnoten)];
        if (!k.skript.empty()) {
            skriptOeffnen(k.skript, false);
        } else if (k.entity >= 0) {
            g_app->leftMode = 1;
            g_app->pickedEntity = k.entity;
            zurEntityFliegen(static_cast<std::size_t>(k.entity));
        }
        z.zieht.clear();
        z.verbindetVon.clear();
    }
    if (!z.zieht.empty() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const int i = knotenIndex(z.zieht);
        if (i >= 0) {
            GraphKnoten& k = z.g.knoten[static_cast<std::size_t>(i)];
            k.x += io.MouseDelta.x / z.zoom;
            k.y += io.MouseDelta.y / z.zoom;
            z.pos[k.schluessel] = ImVec2{k.x, k.y};
        }
    }
    if (!z.verbindetVon.empty()) {
        const int i = knotenIndex(z.verbindetVon);
        if (i >= 0) {
            const GraphKnoten& k = z.g.knoten[static_cast<std::size_t>(i)];
            const ImVec2 p0 = welt(k.x + kKnotenB * s, k.y + kKnotenH * s * 0.5F);
            dl->AddBezierCubic(p0, ImVec2{p0.x + 60.0F * s, p0.y}, ImVec2{m.x - 60.0F * s, m.y}, m,
                               ImGui::GetColorU32(ImVec4{1.0F, 0.9F, 0.4F, 1.0F}), 2.2F * s);
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (!z.verbindetVon.empty() && ueberKnoten >= 0 &&
            z.g.knoten[static_cast<std::size_t>(ueberKnoten)].schluessel != z.verbindetVon) {
            const GraphKnoten& von = z.g.knoten[static_cast<std::size_t>(knotenIndex(z.verbindetVon))];
            const GraphKnoten& nach = z.g.knoten[static_cast<std::size_t>(ueberKnoten)];
            if (von.entity >= 0 && !nach.skript.empty() && von.art == GraphKnoten::Art::Figur) {
                // Bei einer Figur fragen, welches Skript-Feld.
                z.verbindungVon = von.schluessel;
                z.verbindungNach = nach.schluessel;
                ImGui::OpenPopup("##verbindungsart");
            } else {
                const std::string t = graphVerbinden(von.schluessel, nach.schluessel, std::string());
                if (!t.empty()) { meldung(t, std::string()); }
            }
        }
        z.zieht.clear();
        z.verbindetVon.clear();
    }
    if (ImGui::BeginPopup("##verbindungsart")) {
        ImGui::TextDisabled("%s", tr(Str::GraphConnectAs));
        for (const char* schl : {"spawnscript", "deathscript", "painscript", "awakescript", "angerscript", "fleescript", "usescript"}) {
            if (ImGui::MenuItem(schl)) {
                const std::string t = graphVerbinden(z.verbindungVon, z.verbindungNach, schl);
                if (!t.empty()) { meldung(t, std::string()); }
            }
        }
        ImGui::EndPopup();
    }
    if (z.g.knoten.empty()) {
        const char* t = tr(Str::GraphEmpty);
        const ImVec2 g = ImGui::CalcTextSize(t);
        dl->AddText(ImVec2{o.x + (flaecheGr.x - g.x) * 0.5F, o.y + (flaecheGr.y - g.y) * 0.5F},
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), t);
    }
    dl->PopClipRect();
    ImGui::PushTextWrapPos(o.x - ImGui::GetWindowPos().x + flaecheGr.x);
    ImGui::TextDisabled("%s", tr(Str::GraphHint));
    ImGui::PopTextWrapPos();

    // --- Details des gewaehlten Knotens -------------------------------------------------
    if (detailsRechts) {
        ImGui::SetCursorScreenPos(ImVec2{o.x + flaecheGr.x + st.ItemSpacing.x, o.y});
    }
    ImGui::BeginChild("##graphdetails", detailsRechts ? ImVec2{detailB, flaecheGr.y} : ImVec2{-1.0F, -1.0F},
                      ImGuiChildFlags_Borders);
    const int sel = knotenIndex(z.auswahl);
    if (sel < 0) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextDisabled("%s", tr(Str::GraphPickHint));
        ImGui::PopTextWrapPos();
    } else {
        const GraphKnoten k = z.g.knoten[static_cast<std::size_t>(sel)];
        ImGui::TextColored(artFarbe(k.art), "%s", k.entity >= 0 ? g_app->map.entities[static_cast<std::size_t>(k.entity)].classname.c_str()
                                                                  : tr(Str::GraphScript));
        ImGui::PushFont(nullptr, fs * 1.25F);
        ImGui::TextUnformatted(k.titel.c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("%s", k.unter.c_str());
        ImGui::Separator();
        if (k.entity >= 0) {
            const MapEntity& e = g_app->map.entities[static_cast<std::size_t>(k.entity)];
            if (ImGui::BeginTable("##graphkeys", 2, ImGuiTableFlags_SizingStretchProp)) {
                int n = 0;
                for (const auto& [kk, vv] : e.keys) {
                    if (kk == "classname" || n++ > 9) { continue; }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", kk.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(vv.c_str());
                }
                ImGui::EndTable();
            }
            if (iconTextKnopf(tr(Str::GraphShowInMap), UiIcon::Hinfliegen, nullptr)) {
                g_app->leftMode = 1;
                g_app->pickedEntity = k.entity;
                zurEntityFliegen(static_cast<std::size_t>(k.entity));
            }
        } else if (!k.skript.empty()) {
            ImGui::TextDisabled("%s", k.skript.c_str());
            if (iconTextKnopf(tr(Str::EntOpenScript), UiIcon::Skript, nullptr)) { skriptOeffnen(k.skript, true); }
        }
        ImGui::Separator();
        ImGui::TextUnformatted(tr(Str::GraphLinks));
        for (const GraphKante& kk : z.g.kanten) {
            if (kk.nach == sel) {
                ImGui::TextDisabled(tr(Str::GraphLinkIn), kk.label.c_str(), z.g.knoten[static_cast<std::size_t>(kk.von)].titel.c_str());
            }
            if (kk.von == sel) {
                ImGui::TextDisabled(tr(Str::GraphLinkOut), kk.label.c_str(), z.g.knoten[static_cast<std::size_t>(kk.nach)].titel.c_str());
            }
        }
    }
    ImGui::EndChild();
}

// ============================================================================
// Spuren
// ============================================================================
namespace {

struct SpurBlock {
    double start = 0.0;
    double ende = 0.0;
    std::string text;
    Path pfad;          // leer: aus einem anderen Skript
};
struct Spur {
    std::string name;
    UiIcon icon = UiIcon::Skript;
    std::vector<SpurBlock> bloecke;
};

std::vector<Spur> spurenBauen() {
    std::vector<Spur> aus;
    // Kamera: Einstellungen von MOVE zu MOVE.
    {
        Spur k{tr(Str::TracksCamera), UiIcon::Kamera, {}};
        const auto& seg = g_app->camTrack.segments;
        std::vector<const CamSegment*> moves;
        for (const CamSegment& c : seg) {
            if (c.kind == CamSegment::Kind::Move) { moves.push_back(&c); }
        }
        double ende = g_app->timeline.practicalEndMs;
        for (const CamSegment& c : seg) {
            if (c.kind == CamSegment::Kind::Disable) { ende = std::min(ende, c.startMs > 0.0 ? c.startMs : ende); }
        }
        for (std::size_t i = 0; i < moves.size(); ++i) {
            SpurBlock b;
            b.start = moves[i]->startMs;
            b.ende = i + 1 < moves.size() ? moves[i + 1]->startMs : std::max(ende, b.start + 1.0);
            char t[96];
            std::snprintf(t, sizeof(t), tr(Str::TracksShot), static_cast<int>(i) + 1);
            b.text = t;
            if (moves[i]->endMs > moves[i]->startMs + 1.0) { b.text += std::string(" · ") + tr(Str::TracksMoving); }
            b.pfad = moves[i]->path;
            k.bloecke.push_back(b);
        }
        aus.push_back(std::move(k));
    }
    Spur dialog{tr(Str::TracksDialog), UiIcon::Liste, {}};
    std::map<std::string, Spur> figuren;
    Spur skript{tr(Str::TracksScript), UiIcon::Skript, {}};
    for (const TimelineTrack& tr0 : g_app->timeline.tracks) {
        for (const TimelineEvent& ev : tr0.events) {
            if (ev.kind == TimelineEvent::Kind::Camera) { continue; }
            // Pausen nur auf der Skript-Spur - dort ist ihre Laenge die Zeit
            // zwischen den Dingen, und ihre Kante laesst sich ziehen.
            if (ev.kind == TimelineEvent::Kind::Wait && !tr0.entity.empty()) { continue; }
            SpurBlock b;
            b.start = ev.startMs;
            b.ende = std::max(ev.endMs, ev.startMs);
            b.text = ev.label;
            b.pfad = ev.path;
            if (ev.kind == TimelineEvent::Kind::Sound && klein(ev.kanal).rfind("chan_voice", 0) == 0) {
                b.text = fileName(ev.sound);
                b.ende = std::max(b.ende, b.start + 2500.0);
                dialog.bloecke.push_back(b);
                continue;
            }
            if (tr0.entity.empty()) {
                skript.bloecke.push_back(b);
            } else {
                Spur& f = figuren[tr0.entity];
                f.name = tr0.entity;
                f.icon = UiIcon::Figur;
                f.bloecke.push_back(b);
            }
        }
    }
    for (auto& [n, f] : figuren) {
        (void)n;
        aus.push_back(std::move(f));
    }
    if (!dialog.bloecke.empty()) { aus.push_back(std::move(dialog)); }
    if (!skript.bloecke.empty()) { aus.push_back(std::move(skript)); }
    return aus;
}

std::string zeitText(double ms) {
    char t[32];
    const int z = static_cast<int>(std::max(0.0, ms) / 100.0);
    std::snprintf(t, sizeof(t), "%02d:%02d.%d", z / 600, (z / 10) % 60, z % 10);
    return t;
}

}  // namespace

void drawSpurenAnsicht() {
    // Die Zeitleiste stammt aus dem ICARUS-Nachbau (prepareScene) - dieselbe
    // wie unter der Karte.
    if (!g_app->camTrackValid) {
        prepareScene();
    }
    const float fs = ImGui::GetFontSize();
    const ImGuiStyle& st = ImGui::GetStyle();
    const double laenge = std::max(1000.0, g_app->timeline.practicalEndMs > 0.0 ? g_app->timeline.practicalEndMs
                                                                                : g_app->timeline.durationMs);
    // Abspielen auch ohne Kartenansicht (dort laeuft die Zeit in drawTimeline).
    if (g_app->playing && g_app->leftMode != 1) {
        g_app->playMs += static_cast<double>(ImGui::GetIO().DeltaTime) * 1000.0;
        if (g_app->playMs >= laenge) {
            g_app->playMs = laenge;
            g_app->playing = false;
        }
    }

    // --- Werkzeugleiste -------------------------------------------------------
    if (iconTextKnopf(tr(g_app->playing ? Str::TracksPause : Str::TracksPlay), g_app->playing ? UiIcon::Pause : UiIcon::Abspielen,
                      nullptr)) {
        if (!g_app->playing && g_app->playMs >= laenge - 1.0) { g_app->playMs = 0.0; }
        g_app->playing = !g_app->playing;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", zeitText(g_app->playMs).c_str());
    ImGui::SameLine(0.0F, 4.0F);
    ImGui::TextDisabled(tr(Str::TracksOf), zeitText(laenge).c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fs * 7.0F);
    ImGui::SliderFloat("##spurenzoom", &g_app->spurenZoom, 0.5F, 6.0F, tr(Str::TracksZoom));

    const std::vector<Spur> spuren = spurenBauen();
    bool leer = true;
    for (const Spur& sp : spuren) { leer = leer && sp.bloecke.empty(); }
    if (leer) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr(Str::TracksEmpty));
        return;
    }

    // --- Spuren -------------------------------------------------------------------
    const float zeileH = fs * 2.2F;
    const float linealH = fs * 1.6F;
    const float namenB = fs * 9.0F;
    const float detailH = fs * 4.5F;
    const float hoehe = std::max(fs * 6.0F, ImGui::GetContentRegionAvail().y - detailH);
    ImGui::BeginChild("##spurennamen", ImVec2{namenB, hoehe}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy(ImVec2{0.0F, linealH});
    for (const Spur& sp : spuren) {
        const ImVec2 q = ImGui::GetCursorScreenPos();
        const float g = fs;
        uiIcon(ImGui::GetWindowDrawList(), sp.icon, ImVec2{std::floor(q.x + 2.0F), std::floor(q.y + (zeileH - g) * 0.5F)}, g,
               ImGui::GetColorU32(uiIconFarbe(sp.icon)));
        ImGui::GetWindowDrawList()->AddText(nullptr, 0.0F, ImVec2{q.x + g + 8.0F, q.y + (zeileH - fs) * 0.5F},
                                            ImGui::GetColorU32(ImGuiCol_Text), sp.name.c_str(), nullptr, namenB - g - 12.0F);
        ImGui::Dummy(ImVec2{namenB, zeileH});
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0F, 0.0F);
    const float sichtB = ImGui::GetContentRegionAvail().x;
    const double msJePx = std::max(0.5, laenge / std::max(1.0, static_cast<double>(sichtB - fs)) / g_app->spurenZoom);
    const float inhaltB = static_cast<float>(laenge / msJePx) + fs * 2.0F;
    ImGui::BeginChild("##spurenzeit", ImVec2{sichtB, hoehe}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto xVon = [&](double ms) { return o.x + static_cast<float>(ms / msJePx); };
    // Lineal
    ImGui::InvisibleButton("##lineal", ImVec2{inhaltB, linealH});
    if (ImGui::IsItemActive()) {
        g_app->playing = false;
        g_app->playMs = std::clamp(static_cast<double>(ImGui::GetIO().MousePos.x - o.x) * msJePx, 0.0, laenge);
        g_app->mapDirty = true;
    }
    {
        double schritt = 1000.0;
        while (schritt / msJePx < fs * 3.0) { schritt *= 2.0; }
        for (double t = 0.0; t <= laenge + 1.0; t += schritt) {
            const float x = xVon(t);
            dl->AddLine(ImVec2{x, o.y + linealH - 6.0F}, ImVec2{x, o.y + linealH}, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            char z[16];
            std::snprintf(z, sizeof(z), "%g s", t / 1000.0);
            dl->AddText(ImVec2{x + 3.0F, o.y + 1.0F}, ImGui::GetColorU32(ImGuiCol_TextDisabled), z);
        }
    }
    // Spuren mit Bloecken
    g_app->spurenBloecke.clear();
    const ImVec4 farben[] = {uiIconFarbe(UiIcon::Kamera), uiIconFarbe(UiIcon::Figur), uiIconFarbe(UiIcon::Liste),
                             uiIconFarbe(UiIcon::Neu)};
    for (std::size_t s = 0; s < spuren.size(); ++s) {
        const Spur& sp = spuren[s];
        const ImVec2 zeileA = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(ImVec2{zeileA.x, zeileA.y + 2.0F}, ImVec2{zeileA.x + inhaltB, zeileA.y + zeileH - 2.0F},
                          ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0F);
        const ImVec4 grund = sp.icon == UiIcon::Kamera ? farben[0]
                             : sp.icon == UiIcon::Figur ? farben[1]
                             : sp.icon == UiIcon::Liste ? farben[2]
                                                        : farben[3];
        for (std::size_t bi = 0; bi < sp.bloecke.size(); ++bi) {
            const SpurBlock& b = sp.bloecke[bi];
            const float x0 = xVon(b.start);
            const float x1 = std::max(xVon(b.ende), x0 + 6.0F);
            const ImVec2 a{x0 + 1.0F, zeileA.y + 4.0F};
            const ImVec2 e{x1 - 1.0F, zeileA.y + zeileH - 4.0F};
            const bool gewaehlt = !b.pfad.empty() && b.pfad == g_app->selectedPath;
            g_app->spurenBloecke.emplace_back(b.pfad, ImRect{a, e});
            ImVec4 f = grund;
            f.x *= 0.45F;
            f.y *= 0.45F;
            f.z *= 0.45F;
            f.w = 1.0F;
            dl->AddRectFilled(a, e, ImGui::GetColorU32(f), 4.0F);
            dl->AddRect(a, e, ImGui::GetColorU32(gewaehlt ? ImVec4{1.0F, 0.65F, 0.16F, 1.0F} : grund), 4.0F, 0,
                        gewaehlt ? 2.4F : 1.0F);
            dl->PushClipRect(a, e, true);
            dl->AddText(ImVec2{a.x + 5.0F, a.y + (e.y - a.y - fs) * 0.5F}, ImGui::GetColorU32(ImGuiCol_Text), b.text.c_str());
            dl->PopClipRect();
            // Anklicken waehlt die Zeile und setzt die Zeit; die rechte Kante
            // aendert die Dauer (kantenGriff, wie unter der Karte).
            ImGui::SetCursorScreenPos(a);
            char id[48];
            std::snprintf(id, sizeof(id), "##blk%zu_%zu", s, bi);
            ImGui::InvisibleButton(id, ImVec2{std::max(e.x - a.x, 2.0F), e.y - a.y});
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !g_app->tlDragging) {
                ImGui::SetTooltip(tr(Str::TracksBlockTip), b.text.c_str(), zeitText(b.start).c_str(), zeitText(b.ende).c_str());
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !g_app->tlGriffKandidat.da) {
                if (!b.pfad.empty()) { selectByPath(b.pfad); }
                g_app->playing = false;
                g_app->playMs = b.start;
                g_app->mapDirty = true;
            }
            if (!b.pfad.empty()) {
                (void)kantenGriff(b.pfad, b.start, b.ende, a, e, msJePx);
            }
        }
        ImGui::SetCursorScreenPos(ImVec2{zeileA.x, zeileA.y + zeileH});
    }
    ImGui::Dummy(ImVec2{inhaltB, 1.0F});
    // Eine gepackte Kante beginnt jetzt zu ziehen (wie in drawTimeline).
    if (g_app->tlGriffKandidat.da && !g_app->tlDragging) {
        const App::TlKandidat k = g_app->tlGriffKandidat;
        g_app->tlDragging = true;
        g_app->tlDragPath = k.pfad;
        g_app->tlDragStartMs = k.startMs;
        g_app->tlDragEndMs = k.endMs;
    }
    g_app->tlGriffKandidat = App::TlKandidat{};
    // Abspielmarke
    {
        const float x = xVon(g_app->playMs);
        dl->AddLine(ImVec2{x, o.y}, ImVec2{x, o.y + linealH + zeileH * static_cast<float>(spuren.size())},
                    IM_COL32(255, 90, 90, 255), 2.0F);
        dl->AddTriangleFilled(ImVec2{x - 6.0F, o.y}, ImVec2{x + 6.0F, o.y}, ImVec2{x, o.y + 8.0F}, IM_COL32(255, 90, 90, 255));
    }
    ImGui::EndChild();

    // --- Die gewaehlte Zeile --------------------------------------------------------
    ImGui::BeginChild("##spurendetail", ImVec2{-1.0F, -1.0F}, ImGuiChildFlags_Borders);
    const Node* n = g_app->selectedPath.empty() ? nullptr : nodeAt(g_app->doc.script(), g_app->selectedPath);
    if (n == nullptr || n->kind != Node::Kind::Command) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextDisabled("%s", tr(Str::TracksHint));
        ImGui::PopTextWrapPos();
    } else {
        std::string zeile = n->name + " (";
        for (std::size_t i = 0; i < n->args.size(); ++i) {
            zeile += (i == 0 ? " " : ", ") + n->args[i].text;
        }
        zeile += " )";
        ImGui::TextColored(ImVec4{1.0F, 0.65F, 0.16F, 1.0F}, "%s", zeile.c_str());
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextDisabled("%s", tr(Str::TracksHint));
        ImGui::PopTextWrapPos();
    }
    (void)st;
    ImGui::EndChild();
}

// --- Rahmen: Kopfzeile + gewaehlte Ansicht --------------------------------------
void drawFlussAnsicht(float breite, float hoehe, float kopfH) {
    ImGui::BeginGroup();
    const float top = ImGui::GetCursorPosY();
    ImGui::TextUnformatted(tr(Str::GroupScriptFlow));
    ImGui::SameLine();
    ansichtsUmschalter();
    ImGui::SetCursorPosY(top + kopfH);
    ImGui::Dummy(ImVec2{0.0F, 0.0F});
    if (ImGui::BeginChild("##flussansicht", ImVec2{breite, std::max(1.0F, hoehe - kopfH)}, ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (g_app->settings.flussAnsicht == 1) {
            drawGraphAnsicht();
        } else {
            drawSpurenAnsicht();
        }
    }
    ImGui::EndChild();
    ImGui::EndGroup();
}

}  // namespace bhed::gui
