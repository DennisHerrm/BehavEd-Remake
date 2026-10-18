// elemente.cpp - siehe elemente.h (Paket tour, rc601)
//
// Der Inhalt stand bis rc600 in gui/selbsttest.cpp (Element, g_elemente,
// selbsttestMerkeElement/-Text, finde, die ImGui-Haken). selbsttest.cpp
// behaelt seine Namen als duenne Weiterleitungen, damit die Schrittlisten
// dort unveraendert bleiben.
#include "elemente.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace bhed::gui::elemente {
namespace {

std::vector<Element> g_liste;
// Index in g_liste: Beschriftung kam vor dem ItemAdd (Reiter), Rechteck folgt.
std::vector<std::size_t> g_wartend;
Modus g_modus = Modus::Aus;

// Gefiltert: ein kleiner Eintrag je ItemAdd, ohne Zeichenketten. Der Puffer
// wird jedes Bild geleert, behaelt aber seinen Speicher.
struct Roh {
    ImGuiID id = 0;
    ImRect rect;
    ImRect clip;
    ImGuiWindow* fenster = nullptr;
};
std::vector<Roh> g_roh;
std::vector<std::string> g_filter;          // gilt in diesem Bild

// Die Anmeldungen fuer das naechste Bild.
bool g_wunschVoll = false;
std::vector<std::string> g_wunschLabels;

const char* wurzelName(const ImGuiWindow* w) {
    return w->RootWindow != nullptr ? w->RootWindow->Name : w->Name;
}

// Ein Wunsch passt, wenn die Beschriftung mit ihm BEGINNT ("Save" behaelt
// "Save as"; ganzer Text und sichtbarer Teil vor "##" sind damit beide
// abgedeckt). Ein Wunsch mit "*" vorn passt auf das ENDE: die Zaehler der
// Statuszeile heissen "3 errors" - die Zahl wechselt, das Wort nicht.
bool passtFilter(const char* label) {
    const std::size_t n = std::strlen(label);
    for (const std::string& w : g_filter) {
        if (w[0] == '*') {
            const std::size_t m = w.size() - 1;
            if (n >= m && std::memcmp(label + n - m, w.c_str() + 1, m) == 0) { return true; }
        } else if (std::strncmp(label, w.c_str(), w.size()) == 0) {
            return true;
        }
    }
    return false;
}

// Ein wartender Eintrag bekommt sein Rechteck.
bool wartendFuellen(ImGuiID id, const ImRect& bb, const ImGuiWindow* w) {
    for (std::size_t k = 0; k < g_wartend.size(); ++k) {
        if (g_wartend[k] >= g_liste.size() || g_liste[g_wartend[k]].id != id) { continue; }
        Element& e = g_liste[g_wartend[k]];
        e.rect = bb;
        e.clip = w->ClipRect;
        e.fenster = wurzelName(w);
        e.innen = w->Name;
        e.wartet = false;
        g_wartend.erase(g_wartend.begin() + static_cast<std::ptrdiff_t>(k));
        return true;
    }
    return false;
}

void neuesElement(ImGuiID id, const ImRect& bb, const ImRect& clip, const ImGuiWindow* w) {
    Element e;
    e.id = id;
    e.rect = bb;
    e.clip = clip;
    e.fenster = wurzelName(w);
    e.innen = w->Name;
    g_liste.push_back(std::move(e));
}

// Beschriftung ohne Rechteck: wartet auf das ItemAdd derselben Kennung.
void wartendAnlegen(ImGuiID id, const char* label, ImGuiItemStatusFlags flags, const ImGuiWindow* w) {
    Element e;
    e.id = id;
    e.label = label;
    e.flags = flags;
    e.fenster = wurzelName(w);
    e.innen = w->Name;
    e.wartet = true;
    g_liste.push_back(std::move(e));
    g_wartend.push_back(g_liste.size() - 1);
}

}  // namespace

std::vector<Element>& alle() { return g_liste; }
Modus modus() { return g_modus; }

void wunschVoll() { g_wunschVoll = true; }

void wunschLabels(const std::vector<std::string>& praefixe) {
    for (const std::string& p : praefixe) {
        if (!p.empty()) { g_wunschLabels.push_back(p); }
    }
}

void bildEnde() {
    g_liste.clear();
    g_wartend.clear();
    g_roh.clear();
    g_filter.clear();
    if (g_wunschVoll) {
        g_modus = Modus::Voll;
    } else if (!g_wunschLabels.empty()) {
        g_modus = Modus::Gefiltert;
        g_filter.swap(g_wunschLabels);
    } else {
        g_modus = Modus::Aus;
    }
    g_wunschVoll = false;
    g_wunschLabels.clear();
    if (ImGuiContext* g = ImGui::GetCurrentContext(); g != nullptr) {
        g->TestEngineHookItems = (g_modus != Modus::Aus);
    }
}

void merkeElement(ImGuiContext* ctx, ImGuiID id, const ImRect& bb) {
    if (g_modus == Modus::Aus || ctx->CurrentWindow == nullptr) { return; }
    ImGuiWindow* w = ctx->CurrentWindow;
    if (!g_wartend.empty() && wartendFuellen(id, bb, w)) { return; }
    if (g_modus == Modus::Gefiltert) {
        g_roh.push_back(Roh{id, bb, w->ClipRect, w});
        return;
    }
    neuesElement(id, bb, w->ClipRect, w);
}

void merkeText(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
    if (label == nullptr || g_modus == Modus::Aus || ctx->CurrentWindow == nullptr) { return; }
    if (g_modus == Modus::Gefiltert && !passtFilter(label)) { return; }
    // Meist kam ItemAdd kurz davor - dann nur die Beschriftung nachtragen.
    // Von hinten suchen: bei einer aufgeklappten Klappliste liegen die
    // Elemente der Liste schon dahinter.
    for (auto it = g_liste.rbegin(); it != g_liste.rend(); ++it) {
        if (it->id == id) {
            it->label = label;
            it->flags = flags;
            return;
        }
    }
    if (g_modus == Modus::Gefiltert) {
        for (auto it = g_roh.rbegin(); it != g_roh.rend(); ++it) {
            if (it->id != id) { continue; }
            neuesElement(id, it->rect, it->clip, it->fenster);
            g_liste.back().label = label;
            g_liste.back().flags = flags;
            return;
        }
    }
    // ctx->LastItemData gehoert nur dann zu diesem Element, wenn es dieselbe
    // Kennung hat. Sonst kam die Beschriftung vor dem ItemAdd (Reiter) - bis
    // rc600 bekam der Eintrag hier das Rechteck des VORIGEN Elements.
    if (ctx->LastItemData.ID != id) {
        wartendAnlegen(id, label, flags, ctx->CurrentWindow);
        return;
    }
    neuesElement(id, ctx->LastItemData.Rect, ctx->CurrentWindow->ClipRect, ctx->CurrentWindow);
    g_liste.back().label = label;
    g_liste.back().flags = flags;
}

// --- Suchen -----------------------------------------------------------------

std::string sichtbarerText(const char* label) {
    const char* ende = std::strstr(label, "##");
    return ende == nullptr ? std::string(label) : std::string(label, ende);
}

// Nur Treffer, die auf derselben Zeile stehen wie `nachbar`. "+" gibt es
// zweimal: den Reiterknopf oben und "Expand all" neben "-".
bool aufZeileMit(const Element& e, const std::string& nachbar, const std::string& fenster) {
    if (nachbar.empty()) { return true; }
    const Element* n = finde(nachbar, fenster, 0, false);
    return n != nullptr && std::fabs(n->rect.Min.y - e.rect.Min.y) < 1.0F;
}

const Element* finde(const std::string& label, const std::string& fenster, int nte, bool praefix,
                     const std::string& nachbar) {
    int gezaehlt = 0;
    for (const Element& e : g_liste) {
        if (e.label.empty()) { continue; }
        if (!fenster.empty() && e.fenster.find(fenster) == std::string::npos &&
            e.innen.find(fenster) == std::string::npos) {
            continue;
        }
        const std::string sicht = sichtbarerText(e.label.c_str());
        const bool passt = praefix ? (sicht.rfind(label, 0) == 0 || e.label.rfind(label, 0) == 0)
                                   : (e.label == label || sicht == label);
        if (!passt) { continue; }
        if (!aufZeileMit(e, nachbar, fenster)) { continue; }
        if (gezaehlt == nte) { return &e; }
        ++gezaehlt;
    }
    return nullptr;
}

const Element* findeKennung(ImGuiID id) {
    for (const Element& e : g_liste) {
        if (e.id == id) { return &e; }
    }
    return nullptr;
}

// Spiel: der halbe Zeilenabstand. Listeneintraege (Selectable) ragen in
// ImGui absichtlich um so viel ueber ihren Bereich hinaus - der Test meldete
// "INSERT" sonst als abgeschnitten (6 Punkte bei 144 dpi). Ein Knopf, dem
// ein Stueck fehlt, faellt weiterhin auf.
bool ganzImBild(const Element& e) {
    const ImVec2 sp = ImGui::GetStyle().ItemSpacing;
    const float sx = sp.x * 0.5F + 0.5F;
    const float sy = sp.y * 0.5F + 0.5F;
    return e.clip.GetWidth() > 0.0F && e.clip.GetHeight() > 0.0F && e.rect.Min.x >= e.clip.Min.x - sx &&
           e.rect.Min.y >= e.clip.Min.y - sy && e.rect.Max.x <= e.clip.Max.x + sx &&
           e.rect.Max.y <= e.clip.Max.y + sy;
}

}  // namespace bhed::gui::elemente

// --- Die Haken, die ImGui mit IMGUI_ENABLE_TEST_ENGINE aufruft -----------
//
// Sie laufen nur, solange `TestEngineHookItems` gesetzt ist (elemente::bildEnde
// setzt es nach der angemeldeten Stufe). Aussen im globalen Namensraum, so
// deklariert sie imgui_internal.h. Bis rc600 am Ende von selbsttest.cpp.

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb,
                                 const ImGuiLastItemData* /*item_data*/) {
    bhed::gui::elemente::merkeElement(ctx, id, bb);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
    bhed::gui::elemente::merkeText(ctx, id, label, flags);
}

void ImGuiTestEngineHook_Log(ImGuiContext* /*ctx*/, const char* /*fmt*/, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* /*ctx*/, ImGuiID /*id*/) { return nullptr; }
