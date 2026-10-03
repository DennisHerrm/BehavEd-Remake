// selbsttest.cpp - behaved prueft seinen Ereigniseditor und seine Knoepfe
//
// Aufruf:  set BHED_EDITORTEST=1  und behaved.exe starten.
//
// Ohne die Umgebungsvariable tut diese Datei nichts ausser einer Abfrage
// beim ersten Bild.
//
// Zwei Teile, nacheinander, danach beendet sich das Programm selbst:
//
//   1. RUHE    - der Ereigniseditor wird fuer eine Reihe von Befehlen
//                geoeffnet, und jedes sichtbare Bild, in dem sich Stelle
//                oder Groesse aendert, zaehlt als Ruck (rc568).
//   2. KNOEPFE - der Test klickt mit simulierter Maus auf Knoepfe, tippt in
//                Felder und prueft danach das SKRIPT: steht dort, was der
//                Knopf verspricht? Gefunden wird jeder Knopf ueber seine
//                Beschriftung und an der Stelle geklickt, an der ImGui ihn
//                gezeichnet hat. Ein Knopf, der abgeschnitten oder verdeckt
//                ist, faellt damit ebenfalls auf.
//
// Alles steht im Detailprotokoll: `Selbsttest ...` und `Knopftest ...`.
//
// Woher der Test die Knoepfe kennt: ImGui meldet mit
// IMGUI_ENABLE_TEST_ENGINE (gui/imgui_config.h) jedes Element mit Kennung,
// Beschriftung und Rechteck an die Haken unten - aber nur, solange
// `TestEngineHookItems` gesetzt ist. Das geschieht ausschliesslich hier.
//
// Was NICHT geklickt wird: alles, was einen Windows-Dateidialog oeffnet
// oder Dateien schreibt (Open, Append, Save, Save As, Save all, Export,
// Backup, Restore, Compile). Ein Dateidialog haelt die Nachrichtenschleife
// an, und der Test kaeme nie weiter.

#include "update.h"
#include "app_internal.h"
#include "backend.h"
#include "gpumap.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace bhed::gui {

namespace {

// --- Was ImGui in diesem Bild gezeichnet hat -----------------------------

struct Element {
    ImGuiID id = 0;
    std::string label;      // leer, wenn nur ItemAdd kam
    ImRect rect;
    ImRect clip;            // sichtbarer Bereich beim Zeichnen (Fenster/Zelle)
    std::string fenster;    // Name des obersten Fensters
    std::string innen;      // Name des Fensters, in dem es steht (Kindfenster)
};

std::vector<Element> g_elemente;

// Die simulierte Mausstelle. Sie wird nicht nur einmal gemeldet, sondern in
// JEDEM Bild noch einmal, unmittelbar nach der Win32-Anbindung (siehe
// selbsttestVorBild). Grund: hat das Fenster den Fokus und steht die echte
// Maus nicht darueber, meldet ImGui_ImplWin32_UpdateMouseData jedes Bild die
// echte Stelle aus GetCursorPos - und die kam nach unserer und gewann. Ein
// Lauf mit Fokus hatte so 82 Fehlklicks ("unter der Maus liegt kein
// Fenster"), einer ohne Fokus keinen einzigen.
bool g_mausAn = false;
ImVec2 g_maus{};

void setzeMaus(ImVec2 p) {
    g_mausAn = true;
    g_maus = p;
    ImGui::GetIO().AddMousePosEvent(p.x, p.y);
}

// Die Beschriftung ohne den "##..."-Teil - so, wie sie auf dem Knopf steht.
// "###" haengt eine feste Kennung an, "##" eine versteckte.
std::string sichtbarerText(const char* label) {
    const char* ende = std::strstr(label, "##");
    return ende == nullptr ? std::string(label) : std::string(label, ende);
}

}  // namespace

void selbsttestMerkeElement(ImGuiContext* ctx, ImGuiID id, const ImRect& bb) {
    if (ctx->CurrentWindow == nullptr) { return; }
    Element e;
    e.id = id;
    e.rect = bb;
    e.clip = ctx->CurrentWindow->ClipRect;
    e.fenster = ctx->CurrentWindow->RootWindow != nullptr
                    ? ctx->CurrentWindow->RootWindow->Name
                    : ctx->CurrentWindow->Name;
    e.innen = ctx->CurrentWindow->Name;
    g_elemente.push_back(std::move(e));
}

void selbsttestMerkeText(ImGuiContext* ctx, ImGuiID id, const char* label) {
    if (label == nullptr) { return; }
    // Meist kam ItemAdd kurz davor - dann nur die Beschriftung nachtragen.
    // Von hinten suchen: bei einer aufgeklappten Klappliste liegen die
    // Elemente der Liste schon dahinter.
    for (auto it = g_elemente.rbegin(); it != g_elemente.rend(); ++it) {
        if (it->id == id) {
            it->label = label;
            return;
        }
    }
    selbsttestMerkeElement(ctx, id, ctx->LastItemData.Rect);
    g_elemente.back().label = label;
}

namespace {

// --- Kleine Werkzeuge -----------------------------------------------------

// Findet das `nte` Element mit dieser Beschriftung. Verglichen wird mit dem
// ganzen Text ("##v" findet Eingabefelder) und mit dem sichtbaren ("Ok").
// `fenster` ist ein Teil des Fensternamens, leer = egal.
const Element* finde(const std::string& label, const std::string& fenster,
                     int nte, bool praefix, const std::string& nachbar = "");

// Nur Treffer, die auf derselben Zeile stehen wie `nachbar`. "+" gibt es
// zweimal: den Reiterknopf oben und "Expand all" neben "-".
bool aufZeileVon(const Element& e, const std::string& nachbar,
                 const std::string& fenster) {
    if (nachbar.empty()) { return true; }
    const Element* n = finde(nachbar, fenster, 0, false);
    return n != nullptr && std::fabs(n->rect.Min.y - e.rect.Min.y) < 1.0F;
}

const Element* finde(const std::string& label, const std::string& fenster,
                     int nte, bool praefix, const std::string& nachbar) {
    int gezaehlt = 0;
    for (const Element& e : g_elemente) {
        if (e.label.empty()) { continue; }
        if (!fenster.empty() && e.fenster.find(fenster) == std::string::npos &&
            e.innen.find(fenster) == std::string::npos) {
            continue;
        }
        const std::string sicht = sichtbarerText(e.label.c_str());
        const bool passt =
            praefix ? (sicht.rfind(label, 0) == 0 || e.label.rfind(label, 0) == 0)
                    : (e.label == label || sicht == label);
        if (!passt) { continue; }
        if (!aufZeileVon(e, nachbar, fenster)) { continue; }
        if (gezaehlt == nte) { return &e; }
        ++gezaehlt;
    }
    return nullptr;
}

const Element* findeKennung(ImGuiID id) {
    for (const Element& e : g_elemente) {
        if (e.id == id) { return &e; }
    }
    return nullptr;
}

// Liegt das Element ganz im sichtbaren Bereich?
//
// Spiel: der halbe Zeilenabstand. Listeneintraege (Selectable) ragen in
// ImGui absichtlich um so viel ueber ihren Bereich hinaus - der Test meldete
// "INSERT" sonst als abgeschnitten (6 Punkte bei 144 dpi). Ein Knopf, dem
// ein Stueck fehlt, faellt weiterhin auf.
bool ganzSichtbar(const Element& e) {
    const ImVec2 sp = ImGui::GetStyle().ItemSpacing;
    const float sx = sp.x * 0.5F + 0.5F;
    const float sy = sp.y * 0.5F + 0.5F;
    return e.clip.GetWidth() > 0.0F && e.clip.GetHeight() > 0.0F &&
           e.rect.Min.x >= e.clip.Min.x - sx &&
           e.rect.Min.y >= e.clip.Min.y - sy &&
           e.rect.Max.x <= e.clip.Max.x + sx &&
           e.rect.Max.y <= e.clip.Max.y + sy;
}

// "aktiv: <Beschriftung>, unter der Maus: <Beschriftung>" - fuer Meldungen,
// wenn ein Klick nicht ankam.
std::string wasImGuiSieht() {
    ImGuiContext& g = *GImGui;
    auto name = [](ImGuiID id) -> std::string {
        if (id == 0) { return "-"; }
        const Element* e = findeKennung(id);
        if (e == nullptr) { return "?"; }
        return e->label.empty() ? "(ohne Namen) in " + e->innen : e->label + " in " + e->innen;
    };
    return "aktiv: " + name(g.ActiveId) + ", unter der Maus: " + name(g.HoveredId) +
           ", Fenster " + (g.HoveredWindow != nullptr ? g.HoveredWindow->Name : "-");
}

// Eine Datei schreiben (spew in app.cpp liegt im namenlosen Namensraum).
bool spewDatei(const std::string& pfad, const std::string& text) {
    std::FILE* f = std::fopen(pfad.c_str(), "wb");
    if (f == nullptr) { return false; }
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

int g_ok = 0;
int g_fehler = 0;

void meldeOk(const std::string& text) {
    ++g_ok;
    diag::detail("Knopftest OK: " + text);
}

void meldeFehler(const std::string& text) {
    ++g_fehler;
    diag::detail("Knopftest FEHLER: " + text);
}

void pruefeWahr(bool bedingung, const std::string& text) {
    if (bedingung) { meldeOk(text); } else { meldeFehler(text); }
}

std::string skriptText() { return writeScript(g_app->doc.script()); }

// Weg zum `nte` Befehl dieses Namens auf oberster Ebene.
bool wegZu(const std::string& befehl, int nte, Path& weg) {
    int gefunden = 0;
    const std::vector<Node>& n = g_app->doc.script().nodes;
    for (std::size_t i = 0; i < n.size(); ++i) {
        if (n[i].kind == Node::Kind::Command && n[i].name == befehl) {
            if (gefunden == nte) {
                weg = Path{i};
                return true;
            }
            ++gefunden;
        }
    }
    return false;
}

const Node* knoten(const std::string& befehl, int nte) {
    Path weg;
    return wegZu(befehl, nte, weg) ? nodeAt(g_app->doc.script(), weg) : nullptr;
}

int anzahl(const std::string& befehl) {
    int z = 0;
    for (const Node& n : g_app->doc.script().nodes) {
        if (n.kind == Node::Kind::Command && n.name == befehl) { ++z; }
    }
    return z;
}

std::string argText(const std::string& befehl, int nte, std::size_t feld) {
    const Node* k = knoten(befehl, nte);
    if (k == nullptr || feld >= k->args.size()) { return "<fehlt>"; }
    return k->args[feld].text;
}

std::string wert(std::size_t feld) {
    return feld < g_app->editorValues.size() ? g_app->editorValues[feld]
                                             : std::string("<fehlt>");
}

bool istAusdruck(std::size_t feld) {
    return feld < g_app->editorIsExpr.size() && g_app->editorIsExpr[feld] != 0;
}

// --- Schritte -------------------------------------------------------------
//
// Ein Schritt laeuft ueber mehrere Bilder: `tun(b)` bekommt die Nummer des
// Bildes innerhalb des Schritts und meldet mit true, dass er fertig ist.
// Mausereignisse brauchen ein Bild, bis ImGui sie sieht - deshalb Stelle,
// Druecken und Loslassen in getrennten Bildern.

struct Schritt {
    std::string text;
    std::function<bool(int)> tun;
};

// Schritte hinter dem laufenden einhaengen (Definition weiter unten).
void schritteEinfuegen(std::vector<Schritt> neu);
// Warten, bis eine Bedingung gilt (Definition weiter unten).
Schritt warteBis(const std::string& was, std::function<bool()> bedingung, int maxBilder);
bool leseVektor(const std::string& text, float out[3]);
// Die Frage mit Nein beantworten (Definition weiter unten).
Schritt antworteNein();

// "Yes/No query" des Benutzers - der Test setzt es an und stellt es am Ende
// zurueck, weil die Einstellungen beim Beenden gespeichert werden.
bool g_queryVorher = true;

Schritt klick(const std::string& label, const std::string& fenster = "",
              int nte = 0, bool praefix = false,
              const std::string& nachbar = "", int maustaste = 0,
              float griffAnteil = 0.5F) {
    auto ziel = std::make_shared<ImVec2>();
    auto kennung = std::make_shared<ImGuiID>(0);
    const std::string was = "Klick \"" + label + "\"" +
                            (nte > 0 ? " #" + std::to_string(nte) : std::string());
    return {was, [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                if (b == 0) {
                    const Element* e =
                        finde(label, fenster, nte, praefix, nachbar);
                    if (e == nullptr) {
                        meldeFehler(was + ": nicht gefunden");
                        return true;
                    }
                    // Mehrdeutige Beschriftung? Dann alle Treffer zeigen -
                    // sonst klickt der Test still den falschen.
                    if (label.rfind("##", 0) != 0 && nte == 0 &&
                        finde(label, fenster, 1, praefix, nachbar) != nullptr) {
                        for (int k = 0;; ++k) {
                            const Element* x =
                                finde(label, fenster, k, praefix, nachbar);
                            if (x == nullptr) { break; }
                            char z[220];
                            std::snprintf(z, sizeof(z),
                                          "Knopftest: \"%s\" #%d in \"%s\" bei %.0f/%.0f",
                                          x->label.c_str(), k, x->fenster.c_str(),
                                          static_cast<double>(x->rect.Min.x),
                                          static_cast<double>(x->rect.Min.y));
                            diag::detail(z);
                        }
                    }
                    if (!ganzSichtbar(*e)) {
                        char z[200];
                        std::snprintf(z, sizeof(z),
                                      ": nicht ganz sichtbar (%.0f..%.0f, "
                                      "sichtbar %.0f..%.0f)",
                                      static_cast<double>(e->rect.Min.x),
                                      static_cast<double>(e->rect.Max.x),
                                      static_cast<double>(e->clip.Min.x),
                                      static_cast<double>(e->clip.Max.x));
                        meldeFehler(was + z);
                    }
                    // Waagerecht wahlweise nicht mittig: bei Eingabefeldern
                    // mit Beschriftung rechts daneben liegt die Mitte auf
                    // der Beschriftung, und die nimmt keinen Klick.
                    *ziel = ImVec2{e->rect.Min.x + e->rect.GetWidth() * griffAnteil,
                                   e->rect.GetCenter().y};
                    *kennung = e->id;
                    setzeMaus(*ziel);
                    return false;
                }
                if (b == 1) { io.AddMouseButtonEvent(maustaste, true); return false; }
                if (b == 2) {
                    // Jetzt ist die Maustaste unten angekommen: das Element
                    // muss aktiv sein. Sonst traf der Klick etwas anderes.
                    // Rechtsklick aktiviert nichts - dort zaehlt, dass die
                    // Maus ueber dem Element steht.
                    ImGuiContext& g = *GImGui;
                    const bool getroffen = (maustaste == 0)
                                               ? g.ActiveId == *kennung
                                               : g.HoveredIdPreviousFrame == *kennung;
                    if (!getroffen) {
                        meldeFehler(was + ": Klick kam nicht an (" + wasImGuiSieht() + ")");
                    }
                    io.AddMouseButtonEvent(maustaste, false);
                    return false;
                }
                return b >= 5;
            }};
}

// --- Ziehen mit der Maus -----------------------------------------------
//
// Packen, ueber die Ziehschwelle bewegen, zum Ziel fahren, dort ein paar
// Bilder stehen (das Ziel muss den Zug erst annehmen), loslassen.
Schritt ziehe(const std::string& von, const std::string& vonFenster,
              const std::string& nach, const std::string& nachFenster,
              int vonNte = 0, int nachNte = 0, float griffAnteil = 0.5F,
              bool ablegen = true, ImVec2 zielVersatz = ImVec2{0.0F, 0.0F},
              bool vonPraefix = false) {
    auto start = std::make_shared<ImVec2>();
    const std::string was = "Ziehe \"" + von + "\" (" + vonFenster + ") auf \"" +
                            nach + "\" (" + nachFenster + ")";
    return {was, [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                if (b == 0) {
                    const Element* e = finde(von, vonFenster, vonNte, vonPraefix);
                    if (e == nullptr) {
                        meldeFehler(was + ": Quelle nicht gefunden");
                        return true;
                    }
                    *start = ImVec2{e->rect.Min.x + e->rect.GetWidth() * griffAnteil,
                                    e->rect.GetCenter().y};
                    setzeMaus(*start);
                    return false;
                }
                if (b == 1) { io.AddMouseButtonEvent(0, true); return false; }
                if (b == 2) {
                    diag::detail("Knopftest: Zug gepackt - " + wasImGuiSieht());
                }
                if (b >= 2 && b <= 6) {
                    // In kleinen Schritten ueber die Ziehschwelle.
                    setzeMaus(ImVec2{start->x + 4.0F * static_cast<float>(b - 1),
                                     start->y + 2.0F * static_cast<float>(b - 1)});
                    return false;
                }
                if (b >= 7 && b <= 12) {
                    const Element* z = finde(nach, nachFenster, nachNte, false);
                    if (z == nullptr) {
                        if (b == 12) {
                            meldeFehler(was + ": Ziel nicht gefunden");
                            io.AddMouseButtonEvent(0, false);
                            return true;
                        }
                        return false;
                    }
                    const ImVec2 m = z->rect.GetCenter();
                    setzeMaus(ImVec2{m.x + zielVersatz.x, m.y + zielVersatz.y});
                    return false;
                }
                if (b == 13) {
                    // Ein Teiler ist kein Ziehen-und-Ablegen - dort gibt es
                    // keine Nutzlast, nur das gepackte Element.
                    if (ablegen && !GImGui->DragDropActive) {
                        meldeFehler(was + ": kein Zug aktiv vor dem Loslassen (" +
                                    wasImGuiSieht() + ")");
                    } else {
                        diag::detail(std::string("Knopftest: Nutzlast \"") +
                                     GImGui->DragDropPayload.DataType + "\", " +
                                     wasImGuiSieht());
                    }
                    io.AddMouseButtonEvent(0, false);
                    return false;
                }
                return b >= 17;
            }};
}

// --- Ruhe des Hauptfensters -----------------------------------------------
//
// `bilder` lang jedes Bild die Rechtecke aller Elemente im Hauptfenster
// vergleichen. Nach `schonung` Bildern darf sich keines mehr bewegen.
Schritt stabil(const std::string& wann, int bilder = 60, int schonung = 3) {
    struct Stand {
        std::vector<std::pair<ImGuiID, ImRect>> vorher;
        int unruhig = 0;
        std::string erster;
    };
    auto st = std::make_shared<Stand>();
    return {"Ruhe " + wann, [=](int b) {
                std::vector<std::pair<ImGuiID, ImRect>> jetzt;
                for (const Element& e : g_elemente) {
                    if (e.id == 0 || e.fenster.find("###main") == std::string::npos) {
                        continue;
                    }
                    jetzt.emplace_back(e.id, e.rect);
                }
                std::sort(jetzt.begin(), jetzt.end(),
                          [](const auto& x, const auto& y) { return x.first < y.first; });
                if (b > schonung && !st->vorher.empty()) {
                    bool anders = jetzt.size() != st->vorher.size();
                    for (std::size_t q = 0; q < jetzt.size() && !anders; ++q) {
                        const ImRect& r0 = st->vorher[q].second;
                        const ImRect& r1 = jetzt[q].second;
                        if (jetzt[q].first != st->vorher[q].first ||
                            std::fabs(r0.Min.x - r1.Min.x) > 0.5F ||
                            std::fabs(r0.Min.y - r1.Min.y) > 0.5F ||
                            std::fabs(r0.Max.x - r1.Max.x) > 0.5F ||
                            std::fabs(r0.Max.y - r1.Max.y) > 0.5F) {
                            anders = true;
                            if (st->erster.empty()) {
                                const Element* e = findeKennung(jetzt[q].first);
                                char z[200];
                                std::snprintf(z, sizeof(z),
                                              "Bild %d: \"%s\" %.0f/%.0f-%.0f/%.0f -> %.0f/%.0f-%.0f/%.0f", b,
                                              e != nullptr ? e->label.c_str() : "?",
                                              static_cast<double>(r0.Min.x), static_cast<double>(r0.Min.y),
                                              static_cast<double>(r0.Max.x), static_cast<double>(r0.Max.y),
                                              static_cast<double>(r1.Min.x), static_cast<double>(r1.Min.y),
                                              static_cast<double>(r1.Max.x), static_cast<double>(r1.Max.y));
                                st->erster = z;
                                if (e != nullptr) {
                                    st->erster += " in \"" + e->fenster + "\"";
                                    if (ImGuiWindow* fw = ImGui::FindWindowByName(e->fenster.c_str()); fw != nullptr) {
                                        char zz[160];
                                        std::snprintf(zz, sizeof(zz), " (Fenster %.0fx%.0f, Rollleiste %d, Inhalt %.0f)",
                                                      static_cast<double>(fw->Size.x), static_cast<double>(fw->Size.y),
                                                      fw->ScrollbarY ? 1 : 0, static_cast<double>(fw->ContentSize.y));
                                        st->erster += zz;
                                    }
                                }
                            }
                        }
                    }
                    if (anders) { ++st->unruhig; }
                }
                st->vorher = std::move(jetzt);
                if (b < bilder) { return false; }
                char z[160];
                std::snprintf(z, sizeof(z), "%s: %d unruhige Bilder von %d",
                              wann.c_str(), st->unruhig, bilder - schonung);
                pruefeWahr(st->unruhig == 0,
                           std::string(z) + (st->erster.empty() ? "" : ", zuerst " + st->erster));
                return true;
            }};
}

// Klick auf den Schliessknopf in der Titelzeile des Ereignisfensters. Er
// hat keine Beschriftung; seine Kennung ist "#CLOSE" im Fenster.
Schritt klickSchliessen() {
    auto ziel = std::make_shared<ImVec2>();
    return {"Klick Schliessknopf", [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                if (b == 0) {
                    ImGuiWindow* w = ImGui::FindWindowByName("###editor");
                    const Element* e =
                        (w != nullptr) ? findeKennung(ImHashStr("#CLOSE", 0, w->ID))
                                       : nullptr;
                    if (e == nullptr) {
                        meldeFehler("Schliessknopf nicht gefunden");
                        return true;
                    }
                    *ziel = e->rect.GetCenter();
                    setzeMaus(*ziel);
                    return false;
                }
                if (b == 1) { io.AddMouseButtonEvent(0, true); return false; }
                if (b == 2) { io.AddMouseButtonEvent(0, false); return false; }
                return b >= 5;
            }};
}

Schritt taste(ImGuiKey k, const std::string& name) {
    return {"Taste " + name, [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                if (b == 0) { io.AddKeyEvent(k, true); return false; }
                if (b == 1) { io.AddKeyEvent(k, false); return false; }
                return b >= 4;
            }};
}

// Eine Taste mit Modifikator (Strg, Alt) - oder ohne (ImGuiKey_None).
Schritt tasteMit(ImGuiKey mod, ImGuiKey k, const std::string& name) {
    return {"Taste " + name, [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                if (b == 0) {
                    if (mod != ImGuiKey_None) { io.AddKeyEvent(mod, true); }
                    return false;
                }
                if (b == 1) { io.AddKeyEvent(k, true); return false; }
                if (b == 2) { io.AddKeyEvent(k, false); return false; }
                if (b == 3) {
                    if (mod != ImGuiKey_None) { io.AddKeyEvent(mod, false); }
                    return false;
                }
                return b >= 6;
            }};
}

// Alles im aktiven Feld markieren und ersetzen.
Schritt tippe(const std::string& text) {
    return {"Tippe \"" + text + "\"", [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                if (b == 0) {
                    io.AddKeyEvent(ImGuiMod_Ctrl, true);
                    io.AddKeyEvent(ImGuiKey_A, true);
                    return false;
                }
                if (b == 1) {
                    io.AddKeyEvent(ImGuiKey_A, false);
                    io.AddKeyEvent(ImGuiMod_Ctrl, false);
                    return false;
                }
                // Markiertes loeschen - sonst liesse sich ein Feld nicht
                // leeren (leerer Text ersetzt nichts).
                if (b == 2) { io.AddKeyEvent(ImGuiKey_Backspace, true); return false; }
                if (b == 3) { io.AddKeyEvent(ImGuiKey_Backspace, false); return false; }
                if (b == 4) { io.AddInputCharactersUTF8(text.c_str()); return false; }
                return b >= 7;
            }};
}

Schritt pruefe(const std::string& text, std::function<bool()> bedingung) {
    return {"Pruefe " + text, [=](int) {
                pruefeWahr(bedingung(), text);
                return true;
            }};
}

Schritt tu(const std::string& text, std::function<void()> was) {
    return {text, [=](int b) {
                if (b == 0) { was(); }
                return b >= 3;
            }};
}

Schritt oeffne(const std::string& befehl, int nte) {
    const std::string was = "Oeffne " + befehl + " #" + std::to_string(nte);
    return {was, [=](int b) {
                if (b == 0) {
                    Path weg;
                    if (!wegZu(befehl, nte, weg)) {
                        meldeFehler(was + ": Befehl nicht im Skript");
                        return true;
                    }
                    openEditorForNode(weg);
                    return false;
                }
                ImGuiWindow* w = ImGui::FindWindowByName("###editor");
                if (w != nullptr && w->Active && !w->Hidden && b >= 3) { return true; }
                if (b > 30) {
                    meldeFehler(was + ": Editor erscheint nicht");
                    return true;
                }
                return false;
            }};
}

Schritt waehle(const std::string& befehl, int nte) {
    return tu("Waehle " + befehl + " #" + std::to_string(nte), [=]() {
        Path weg;
        if (wegZu(befehl, nte, weg)) { selectByPath(weg); }
    });
}

// --- Die Klickpruefung ----------------------------------------------------

// --- Verhalten wie im Original: Suche, Zwischenablage, Speichernfrage ----
//
// Allein aufrufbar mit BHED_EDITORTEST=original (wenige Sekunden).
std::vector<Schritt> originalSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    auto vorher = std::make_shared<std::string>();
    auto zahl = std::make_shared<int>(0);
    // ---- Suche wie im Original ------------------------------------------
    // "called" nur der Befehlsname, "containing" jedes Argument einzeln,
    // Weitersuchen ab der Auswahl, am Ende Umlauf.
    auto treffer = std::make_shared<std::vector<Path>>();
    auto gesehen = std::make_shared<std::vector<Path>>();
    add(waehle("rem", 0));
    add(klick(tr(Str::ActFind)));
    add(pruefe("Suche: Whole-string ist vorgewaehlt", [] { return g_app->findWhole; }));
    add(tu("Messung nach dem Oeffnen", [] {
        diag::detail("Knopftest: Suchfenster offen " + std::to_string(g_app->findOpen) +
                     ", " + wasImGuiSieht());
    }));
    add(tippe("wait"));   // das Feld "called" hat beim Oeffnen den Fokus
    add(tu("Messung nach dem Tippen", [] {
        diag::detail("Knopftest: " + wasImGuiSieht());
    }));
    add(Schritt{"Suche: alle Treffer von \"wait\" der Reihe nach", [=](int) {
        FindOptions o;
        o.named = "wait";
        *treffer = findAll(g_app->doc.script(), o);
        gesehen->clear();
        std::vector<Schritt> neu;
        for (std::size_t k = 0; k <= treffer->size(); ++k) {
            neu.push_back(klick(tr(Str::FindNextBtn), "###find"));
            neu.push_back(tu("Treffer merken",
                             [=] { gesehen->push_back(g_app->selectedPath); }));
        }
        neu.push_back(tu("Messung Suche", [=] {
            std::string z = "Knopftest: gemerkt called=\"" + g_app->findLetzte.named +
                            "\" containing=\"" + g_app->findLetzte.contains + "\", Treffer " +
                            std::to_string(treffer->size()) + ", gesehen:";
            for (const Path& p : *gesehen) {
                z += " [";
                for (std::size_t q = 0; q < p.size(); ++q) {
                    z += (q ? "," : "") + std::to_string(p[q]);
                }
                z += "]";
            }
            diag::detail(z);
        }));
        neu.push_back(pruefe("Suche \"called wait\": jeder Treffer ab rem der Reihe nach, "
                             "dann Umlauf zum ersten", [=] {
            if (treffer->size() < 2 || gesehen->size() != treffer->size() + 1) {
                return false;
            }
            for (std::size_t k = 0; k < treffer->size(); ++k) {
                if ((*gesehen)[k] != (*treffer)[k]) { return false; }
            }
            return gesehen->back() == treffer->front();
        }));
        neu.push_back(klick(tr(Str::FindPrevBtn), "###find"));
        neu.push_back(pruefe("Suche zurueck: vom ersten Treffer Umlauf zum letzten",
                             [=] { return g_app->selectedPath == treffer->back(); }));
        schritteEinfuegen(std::move(neu));
        return true;
    }});
    add(tippe(""));   // "called" leeren
    add(klick(tr(Str::FindContains), "###find", 0, false, "", 0, 0.15F));
    add(tippe("100.000"));
    add(waehle("rem", 0));
    add(klick(tr(Str::FindNextBtn), "###find"));
    add(pruefe("Suche \"containing 100.000\": trifft ein wait ( 100.000 ) im Block", [] {
        const Node* n = nodeAt(g_app->doc.script(), g_app->selectedPath);
        return n != nullptr && n->name == "wait" && !n->args.empty() &&
               n->args[0].text == "100.000" && g_app->selectedPath.size() == 2;
    }));
    // Nach "Find next" hat der Knopf den Fokus - erst wieder ins Feld.
    add(klick(tr(Str::FindContains), "###find", 0, false, "", 0, 0.15F));
    add(tippe("wait"));
    add(tu("Auswahl merken", [=] { *gesehen = {g_app->selectedPath}; }));
    add(klick(tr(Str::FindNextBtn), "###find"));
    add(pruefe("Suche \"containing wait\" findet nichts - der Befehlsname zaehlt dort nicht",
               [=] { return g_app->findHits.empty() && g_app->selectedPath == gesehen->front(); }));
    add(tu("Suchfenster schliessen", [] { g_app->findOpen = false; }));

    // ---- Windows-Zwischenablage wie im Original --------------------------
    add(waehle("print", 0));
    add(klick(tr(Str::ActCopy)));
    add(pruefe("Copy schreibt \"//(BHVD)\" + Zeile in die Windows-Zwischenablage", [] {
        const char* c = ImGui::GetClipboardText();
        const std::string s = c != nullptr ? c : "";
        return s.rfind("//(BHVD)\r\nprint (", 0) == 0;
    }));
    add(tu("Auswahl aufheben", [] {
        g_app->selection.clear();
        g_app->selectedPath.clear();
        g_app->selected = -1;
    }));
    add(klick(tr(Str::ActCopy)));
    add(pruefe("Copy ohne Auswahl fragt \"Copy entire script?\"", [] {
        return g_app->frageOffen && g_app->frageText == tr(Str::AskCopyAll);
    }));
    add(klick(tr(Str::AnswerYes), "###frage"));
    add(pruefe("... und legt das ganze Skript in die Zwischenablage", [] {
        const char* c = ImGui::GetClipboardText();
        const std::string s = c != nullptr ? c : "";
        // Das Testskript beginnt mit einer Leerzeile - sie gehoert dazu.
        return s.rfind("//(BHVD)\r\n", 0) == 0 && s.find("rem (") != std::string::npos &&
               s.find("print (") != std::string::npos &&
               g_app->doc.clipboard().size() == g_app->doc.script().nodes.size();
    }));
    add(tu("Skript merken", [=] { *vorher = skriptText(); *zahl = anzahl("print"); }));
    add(tu("Text wie von BehavEd in die Zwischenablage",
           [] { ImGui::SetClipboardText("//(BHVD)\r\nprint ( \"von aussen\" );\r\n"); }));
    add(waehle("print", 0));
    add(klick(tr(Str::ActPaste)));
    add(pruefe("Paste von \"//(BHVD)\"-Text: ohne Frage eingefuegt", [=] {
        bool da = false;
        for (const Node& n : g_app->doc.script().nodes) {
            if (n.name == "print" && !n.args.empty() && n.args[0].text == "von aussen") { da = true; }
        }
        return !g_app->frageOffen && da;
    }));
    add(klick(tr(Str::EditUndo)));
    add(tu("Fremden Text in die Zwischenablage",
           [] { ImGui::SetClipboardText("wait ( 333.000 );"); }));
    add(klick(tr(Str::ActPaste)));
    add(pruefe("Paste fremden Texts fragt \"You seem to be pasting ...\"", [] {
        return g_app->frageOffen && g_app->frageText == tr(Str::AskPasteForeign);
    }));
    add(klick(tr(Str::AnswerNo), "###frage"));
    add(pruefe("... \"No\" fuegt nichts ein", [=] { return skriptText() == *vorher; }));
    add(klick(tr(Str::ActPaste)));
    add(klick(tr(Str::AnswerYes), "###frage"));
    add(pruefe("... \"Yes\" liest den Text als Skript und fuegt wait ( 333.000 ) ein", [] {
        bool da = false;
        for (const Node& n : g_app->doc.script().nodes) {
            if (n.name == "wait" && !n.args.empty() && n.args[0].text == "333.000") { da = true; }
        }
        return da;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pruefe("Strg+Z nimmt das Einfuegen zurueck", [=] { return skriptText() == *vorher; }));

    // ---- Speichernfrage auch ohne "Yes/No query" --------------------------
    add(tu("Yes/No query aus, Skript aendern", [] {
        g_app->settings.queryOnDiscard = false;
        Path weg;
        if (wegZu("print", 0, weg)) { (void)g_app->doc.cloneAt(weg); rebuildTree(); }
    }));
    add(tu("Etwas anstossen, das Aenderungen verwerfen koennte",
           [] { withUnsaved([] { diag::detail("Knopftest: withUnsaved ohne Frage weiter"); }); }));
    add(pruefe("Ungespeicherte Aenderungen: Speichernfrage kommt auch ohne Yes/No query",
               [] { return g_app->askSaveOpen; }));
    add(klick(tr(Str::EditorCancel), tr(Str::AppTitle)));
    add(tu("Yes/No query wieder an, Aenderung zuruecknehmen", [] {
        g_app->settings.queryOnDiscard = true;
        (void)g_app->doc.undo();
        rebuildTree();
    }));
    add(pruefe("Skript wieder wie vorher", [=] { return skriptText() == *vorher; }));

    // ---- Numpad + / - und Strg+T (ACCELERATOR 135) ----------------------
    auto zeilen = std::make_shared<std::size_t>(0);
    add(tu("Alles zu, erstes if waehlen", [=] {
        g_app->expanded.closeAll();
        rebuildTree();
        Path weg;
        if (wegZu("if", 0, weg)) { selectByPath(weg); }
        *zeilen = g_app->rows.size();
    }));
    add(tasteMit(ImGuiKey_None, ImGuiKey_KeypadAdd, "Num+"));
    add(pruefe("Num+ klappt den gewaehlten Block samt Inhalt auf", [=] {
        return g_app->rows.size() > *zeilen;
    }));
    add(tasteMit(ImGuiKey_None, ImGuiKey_KeypadSubtract, "Num-"));
    add(pruefe("Num- klappt ihn wieder zu", [=] { return g_app->rows.size() == *zeilen; }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_KeypadAdd, "Strg+Num+"));
    add(tu("Zeilen nach Strg+Num+ merken", [=] {
        diag::detail("Knopftest: Zeilen zu " + std::to_string(*zeilen) + ", alles auf " +
                     std::to_string(g_app->rows.size()));
    }));
    add(pruefe("Strg+Num+ klappt ALLES auf (mehr als nur ein Block)", [=] {
        return g_app->rows.size() > *zeilen + 1;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_KeypadSubtract, "Strg+Num-"));
    add(pruefe("Strg+Num- klappt alles zu", [=] { return g_app->rows.size() == *zeilen; }));
    add(tu("Skript merken", [=] { *vorher = skriptText(); *zahl = anzahl("print"); }));
    add(waehle("print", 0));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_T, "Strg+T"));
    add(pruefe("Strg+T schneidet aus wie Strg+X", [=] { return anzahl("print") == *zahl - 1; }));
    add(klick(tr(Str::EditUndo)));
    add(pruefe("Undo nach Strg+T: Skript wie vorher", [=] { return skriptText() == *vorher; }));

    // ---- Backup / Restore wie im Original -------------------------------
    auto datei = std::make_shared<std::string>(platform::executableDirectory() +
                                               "\\selbsttest-backup.txt");
    add(tu("Pfad setzen, alte Sicherungen weg", [=] {
        std::remove((*datei).c_str());
        std::remove((platform::executableDirectory() + "\\selbsttest-backup.bak").c_str());
        g_app->path = *datei;
        *vorher = skriptText();
    }));
    add(klick(tr(Str::FileRestore)));
    add(pruefe("Restore ohne Sicherung: keine Frage (\"No Backup file available...\")",
               [] { return !g_app->frageOffen; }));
    add(klick(tr(Str::FileBackup)));
    add(pruefe("Backup schreibt x.bak (nicht x.txt.bak) mit dem aktuellen Stand", [=] {
        const std::string bak = platform::executableDirectory() + "\\selbsttest-backup.bak";
        return slurp(bak) == skriptText();
    }));
    add(tu("Skript aendern", [] {
        Path weg;
        if (wegZu("print", 0, weg)) { (void)g_app->doc.cloneAt(weg); rebuildTree(); }
    }));
    add(klick(tr(Str::FileRestore)));
    add(pruefe("Restore fragt immer: \"This RESTORE will overwrite ...\"", [] {
        return g_app->frageOffen && g_app->frageText == tr(Str::RestoreAsk);
    }));
    add(klick(tr(Str::AnswerYes), "###frage"));
    add(pruefe("Restore stellt den gesicherten Stand her", [=] { return skriptText() == *vorher; }));
    add(klick(tr(Str::EditUndo)));
    add(pruefe("Undo nach Restore: der geaenderte Stand ist wieder da",
               [=] { return anzahl("print") == *zahl + 1; }));
    add(klick(tr(Str::EditUndo)));
    add(tu("Sicherung wegraeumen", [=] {
        std::remove((platform::executableDirectory() + "\\selbsttest-backup.bak").c_str());
    }));

    // ---- Compile mit Fehler: keine .ibi ----------------------------------
    add(tu("Unbekannten Befehl einfuegen", [=] {
        std::remove((platform::executableDirectory() + "\\selbsttest-backup.ibi").c_str());
        Node n;
        n.kind = Node::Kind::Command;
        n.name = "gibtsnicht";
        Arg a;
        a.kind = Arg::Kind::Number;
        a.text = "1";
        n.args.push_back(a);
        (void)g_app->doc.insertAfter(Path{}, n);
        rebuildTree();
    }));
    add(pruefe("Compile-Knopf ist gross (mindestens doppelte Knopfhoehe)", [=] {
        const Element* e = finde(tr(Str::Compile), "", 0, false);
        return e != nullptr && e->rect.GetHeight() >= ImGui::GetFrameHeight() * 2.0F;
    }));
    add(klick(tr(Str::Compile)));
    add(pruefe("Compile mit Fehler schreibt KEINE .ibi", [=] {
        std::error_code ec;
        const bool ibi = std::filesystem::exists(
            std::filesystem::u8path(platform::executableDirectory() + "\\selbsttest-backup.ibi"), ec);
        return !ibi;
    }));
    add(pruefe("Compile mit Fehler: Knopf zeigt kurz \"Failed\"",
               [] { return g_app->kompiliertFehlerBis > ImGui::GetTime() &&
                           finde(tr(Str::Compile), "", 0, false) == nullptr; }));
    add(klick(tr(Str::EditUndo)));
    // Der Knopf behaelt seine Kennung "###compile" in jedem Zustand.
    add(klick("###compile"));
    add(pruefe("Compile ohne Fehler schreibt die .ibi", [=] {
        std::error_code ec;
        return std::filesystem::exists(
            std::filesystem::u8path(platform::executableDirectory() + "\\selbsttest-backup.ibi"), ec);
    }));
    add(pruefe("Compile ohne Fehler: Knopf zeigt \"Compiled\" mit Haken",
               [] { return g_app->kompiliertBis > ImGui::GetTime() &&
                           finde(tr(Str::Compile), "", 0, false) == nullptr; }));
    add(warteBis("Knopf zurueck auf \"Compile!\"",
                 [] { return ImGui::GetTime() > g_app->kompiliertBis &&
                             finde(tr(Str::Compile), "", 0, false) != nullptr; }, 3000));
    add(pruefe("nach 1,5 s steht wieder \"Compile!\" da",
               [] { return finde(tr(Str::Compile), "", 0, false) != nullptr; }));
    add(tu("Testdateien wegraeumen, Pfad leeren", [=] {
        std::remove((*datei).c_str());
        std::remove((platform::executableDirectory() + "\\selbsttest-backup.ibi").c_str());
        auto& r = g_app->settings.recent;
        r.erase(std::remove(r.begin(), r.end(), *datei), r.end());
        g_app->path.clear();
    }));

    // ---- Alpha-sort -----------------------------------------------------
    add(tu("Alpha-sort an", [] { g_app->settings.alphaSortPulldowns = true; }));
    add(oeffne("set", 0));
    add(klick("##combo", "###editor"));
    add(pruefe("Alpha-sort: die Klappliste steht alphabetisch", [] {
        std::vector<std::pair<float, std::string>> z;
        for (const Element& e : g_elemente) {
            if (e.fenster.find("##Combo") == std::string::npos) { continue; }
            if (e.label.empty() || e.label.rfind("##", 0) == 0) { continue; }
            if (e.label.find("###") != std::string::npos) { continue; }
            z.emplace_back(e.rect.Min.y, e.label);
        }
        std::sort(z.begin(), z.end());
        if (z.size() < 3) { return false; }
        for (std::size_t q = 1; q < z.size(); ++q) {
            std::string a = z[q - 1].second;
            std::string b = z[q].second;
            for (char& ch : a) { ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); }
            for (char& ch : b) { ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); }
            if (b < a) {
                diag::detail("Knopftest: nicht sortiert: " + z[q - 1].second + " vor " + z[q].second);
                return false;
            }
        }
        return true;
    }));
    // EIN Escape schliesst die Liste (wie eine Windows-Klappliste).
    add(tasteMit(ImGuiKey_None, ImGuiKey_Escape, "Escape (Liste zu)"));
    add(pruefe("Escape schliesst die Klappliste mit einem Druck", [] {
        for (const Element& e : g_elemente) {
            if (e.fenster.find("##Combo") != std::string::npos) { return false; }
        }
        return g_app->editorOpen;
    }));
    add(klick(tr(Str::EditorCancel), "###editor"));
    add(tu("Alpha-sort aus", [] { g_app->settings.alphaSortPulldowns = false; }));

    // ---- Prefs: Cancel spielt zurueck, Ok uebernimmt ---------------------
    auto merk = std::make_shared<bool>(false);
    add(tu("Alpha-sort merken", [=] { *merk = g_app->settings.alphaSortPulldowns; }));
    add(klick(tr(Str::AppPrefs)));
    add(klick(tr(Str::PrefsAlphaSort), tr(Str::PrefsTitle)));
    add(pruefe("Prefs: Haekchen umgeschaltet", [=] {
        return g_app->settings.alphaSortPulldowns != *merk;
    }));
    add(klick(tr(Str::EditorCancel), tr(Str::PrefsTitle)));
    add(pruefe("Prefs Cancel: alter Stand zurueck",
               [=] { return !g_app->prefsOpen && g_app->settings.alphaSortPulldowns == *merk; }));
    add(klick(tr(Str::AppPrefs)));
    add(klick(tr(Str::PrefsAlphaSort), tr(Str::PrefsTitle)));
    add(klick(tr(Str::EditorOk), tr(Str::PrefsTitle)));
    add(pruefe("Prefs Ok: Aenderung bleibt",
               [=] { return !g_app->prefsOpen && g_app->settings.alphaSortPulldowns != *merk; }));
    add(tu("Alpha-sort zurueck", [=] { g_app->settings.alphaSortPulldowns = *merk; }));

    // ---- Alt-Kuerzel -----------------------------------------------------
    add(tasteMit(ImGuiMod_Alt, ImGuiKey_F, "Alt+F"));
    add(pruefe("Alt+F oeffnet die Suche (\"&Find\")", [] { return g_app->findOpen; }));
    add(tu("Suchfenster schliessen", [] { g_app->findOpen = false; }));

    // ---- Schreibgeschuetzte Datei ----------------------------------------
    auto nurLesen = std::make_shared<std::string>(platform::executableDirectory() +
                                                  "\\selbsttest-nurlesen.txt");
    add(tu("Schreibgeschuetzte Datei anlegen", [=] {
        std::error_code ec;
        const std::filesystem::path fp = std::filesystem::u8path(*nurLesen);
        std::filesystem::permissions(fp, std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add, ec);
        (void)spewDatei(*nurLesen, "//Generated by BehavEd\r\n");
        // ALLE Schreibrechte weg: erst dann setzt MSVC das Windows-Attribut
        // "Schreibgeschuetzt".
        std::filesystem::permissions(fp, std::filesystem::perms::owner_write |
                                             std::filesystem::perms::group_write |
                                             std::filesystem::perms::others_write,
                                     std::filesystem::perm_options::remove, ec);
        g_app->path = *nurLesen;
    }));
    add(klick(tr(Str::FileSave)));
    add(pruefe("Speichern einer schreibgeschuetzten Datei fragt nach dem Entsperren", [] {
        return g_app->frageOffen &&
               g_app->frageText.find("write-protected") != std::string::npos;
    }));
    add(klick(tr(Str::AnswerYes), "###frage"));
    add(pruefe("... \"Yes\": Schutz aufgehoben und gespeichert", [=] {
        return slurp(*nurLesen) == skriptText();
    }));
    add(tu("Testdatei wegraeumen", [=] {
        std::error_code ec;
        std::filesystem::permissions(std::filesystem::u8path(*nurLesen),
                                     std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add, ec);
        std::remove((*nurLesen).c_str());
        auto& r = g_app->settings.recent;
        r.erase(std::remove(r.begin(), r.end(), *nurLesen), r.end());
        g_app->path.clear();
    }));

    // ---- REM wie im Original ---------------------------------------------
    // Blockstruktur bleibt im Baum, Ruecktaste schaltet um, Un-REM nimmt nur
    // die eigene Anweisung zurueck.
    auto remWeg = std::make_shared<Path>();
    auto sucheZeile = [](const std::string& anfang) -> const Row* {
        for (const Row& r : g_app->rows) {
            if (r.what == Row::What::Comment && r.text.rfind(anfang, 0) == 0) { return &r; }
        }
        return nullptr;
    };
    add(tu("Alles zu, erstes if waehlen, Skript merken", [=] {
        g_app->expanded.closeAll();
        rebuildTree();
        Path weg;
        if (wegZu("if", 0, weg)) { selectByPath(weg); }
        *vorher = skriptText();
        *zahl = anzahl("if");
    }));
    add(tasteMit(ImGuiKey_None, ImGuiKey_Backspace, "Ruecktaste (REM)"));
    add(pruefe("REM: das if ist auskommentiert", [=] { return anzahl("if") == *zahl - 1; }));
    add(pruefe("REM im Baum: EINE klappbare Zeile \"/////////////  if ...\", keine {-Zeile", [=] {
        const Row* kopf = sucheZeile("/////////////  if");
        const bool klammer = sucheZeile("/////////////  {") != nullptr ||
                             sucheZeile("/////////////  }") != nullptr;
        return kopf != nullptr && kopf->childCount > 0 && !kopf->open && !klammer;
    }));
    add(tasteMit(ImGuiKey_None, ImGuiKey_KeypadAdd, "Num+ (REM-Block auf)"));
    add(pruefe("REM-Block aufgeklappt: sein wait steht eingerueckt darunter", [=] {
        const Row* kopf = sucheZeile("/////////////  if");
        const Row* kind = sucheZeile("/////////////  wait");
        return kopf != nullptr && kind != nullptr && kind->depth == kopf->depth + 1;
    }));
    add(tasteMit(ImGuiKey_None, ImGuiKey_Backspace, "Ruecktaste (Un-REM)"));
    add(pruefe("Ruecktaste auf der REM-Zeile: if zurueck und ausgewaehlt, Skript wie vorher", [=] {
        const Node* n = nodeAt(g_app->doc.script(), g_app->selectedPath);
        return skriptText() == *vorher && n != nullptr && n->name == "if";
    }));
    add(waehle("set", 0));
    add(tasteMit(ImGuiKey_None, ImGuiKey_Backspace, "Ruecktaste (REM set)"));
    add(tu("REM-Zeile von set merken", [=] { *remWeg = g_app->selectedPath; }));
    add(waehle("wait", 0));
    add(tasteMit(ImGuiKey_None, ImGuiKey_Backspace, "Ruecktaste (REM wait)"));
    add(tu("Zurueck auf die REM-Zeile von set", [=] {
        g_app->selectedPath = *remWeg;
        g_app->selection.assign(1, *remWeg);
        rebuildTree();
    }));
    add(tasteMit(ImGuiKey_None, ImGuiKey_Backspace, "Ruecktaste (Un-REM set)"));
    add(pruefe("Un-REM auf set laesst den getrennt auskommentierten Nachbarn wait REM", [=] {
        return anzahl("set") == 1 && anzahl("wait") == 0;
    }));
    add(tu("Dreimal Rueckgaengig (REM set, REM wait, Un-REM set)", [] {
        (void)g_app->doc.undo();
        (void)g_app->doc.undo();
        (void)g_app->doc.undo();
        rebuildTree();
    }));
    add(pruefe("Rueckgaengig: Skript wie vorher", [=] { return skriptText() == *vorher; }));

    return s;
}

std::vector<Schritt> knopfSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    // Zwischen den Schritten gemerkte Zustaende.
    auto vorher = std::make_shared<std::string>();
    auto zeilen = std::make_shared<std::size_t>(0);
    auto zahl = std::make_shared<int>(0);
    const std::string ed = "###editor";

    // ---- Ereigniseditor: Ok, Abbrechen, Escape, Eingabe, Schliessen -----
    add(oeffne("print", 0));
    add(klick("##v", ed));
    add(tippe("Hallo Test"));
    add(pruefe("print: Feld zeigt den getippten Text",
               [] { return wert(0) == "Hallo Test"; }));
    add(klick(tr(Str::EditorOk), ed));
    add(pruefe("print: Ok schliesst den Editor", [] { return !g_app->editorOpen; }));
    add(pruefe("print: Ok schreibt den Wert ins Skript",
               [] { return argText("print", 0, 0) == "Hallo Test"; }));

    add(tu("Skript merken", [=] { *vorher = skriptText(); }));
    add(oeffne("print", 0));
    add(klick("##v", ed));
    add(tippe("Wegwerfen"));
    add(klick(tr(Str::EditorCancel), ed));
    add(pruefe("print: Cancel schliesst den Editor", [] { return !g_app->editorOpen; }));
    add(pruefe("print: Cancel laesst das Skript unveraendert",
               [=] { return skriptText() == *vorher; }));

    add(oeffne("print", 0));
    add(klick("##v", ed));
    add(tippe("Escape-Text"));
    add(taste(ImGuiKey_Escape, "Escape"));
    add(pruefe("print: Escape schliesst den Editor", [] { return !g_app->editorOpen; }));
    add(pruefe("print: Escape laesst das Skript unveraendert",
               [=] { return skriptText() == *vorher; }));

    add(oeffne("print", 0));
    add(klickSchliessen());
    add(pruefe("print: Schliessknopf schliesst den Editor",
               [] { return !g_app->editorOpen; }));
    add(pruefe("print: Schliessknopf laesst das Skript unveraendert",
               [=] { return skriptText() == *vorher; }));

    add(oeffne("print", 0));
    add(klick("##v", ed));
    add(tippe("Per Eingabe"));
    add(taste(ImGuiKey_Enter, "Eingabe"));
    add(pruefe("print: Eingabe schliesst den Editor", [] { return !g_app->editorOpen; }));
    add(pruefe("print: Eingabe schreibt den Wert ins Skript",
               [] { return argText("print", 0, 0) == "Per Eingabe"; }));

    // ---- Expr! und Revert ------------------------------------------------
    add(oeffne("print", 0));
    add(klick(tr(Str::EditorExpr), ed));
    add(pruefe("print: Expr! schaltet auf Ausdruck", [] { return istAusdruck(0); }));
    add(pruefe("print: Expr! zeigt die Helferzeilen (Get/Tag/Rnd)", [=] {
        return finde(tr(Str::EditorGet), ed, 0, false) != nullptr &&
               finde(tr(Str::EditorTag), ed, 0, false) != nullptr &&
               finde(tr(Str::EditorRnd), ed, 0, false) != nullptr;
    }));
    add(klick(tr(Str::EditorRevert), ed));
    add(pruefe("print: Revert schaltet zurueck", [] { return !istAusdruck(0); }));
    add(pruefe("print: Revert blendet die Helferzeilen aus",
               [=] { return finde(tr(Str::EditorGet), ed, 0, false) == nullptr; }));
    add(klick(tr(Str::EditorCancel), ed));

    // ---- Helferknoepfe Get / Tag / Rnd -----------------------------------
    add(tu("Skript merken", [=] { *vorher = skriptText(); }));
    add(oeffne("if", 0));
    add(pruefe("if/3: alle drei Felder sind Ausdruecke",
               [] { return istAusdruck(0) && istAusdruck(1) && istAusdruck(2); }));
    add(klick(tr(Str::EditorRnd), ed, 0));
    add(pruefe("if/3: Rnd schreibt random( ... ) in Feld 0",
               [] { return wert(0).rfind("random(", 0) == 0; }));
    add(klick(tr(Str::EditorGet), ed, 0));
    add(pruefe("if/3: Get schreibt get( ... ) in Feld 0",
               [] { return wert(0).rfind("get(", 0) == 0; }));
    add(klick(tr(Str::EditorTag), ed, 0));
    add(pruefe("if/3: Tag schreibt tag( ... ) in Feld 0",
               [] { return wert(0).rfind("tag(", 0) == 0; }));
    // Genau die Schreibweise des Originals (am laufenden BehavEd abgelesen,
    // 27.09.) - mit den Vorgaben FLOAT / SET_PARM1 / targetname / ORIGIN /
    // 0 .. 1. Die Werte werden erst gesetzt: behaved merkt sich die letzte
    // Helferwahl je Befehl, die Vorbelegung haengt also von frueheren
    // Laeufen ab.
    add(tu("Helfer auf die Vorgaben", [] {
        g_app->helpGetType[0] = "FLOAT";
        g_app->helpGetName[0] = "SET_PARM1";
        g_app->helpTagName[0] = "targetname";
        g_app->helpTagType[0] = "ORIGIN";
        g_app->helpRangeLow[0] = 0.0F;
        g_app->helpRangeHigh[0] = 1.0F;
    }));
    add(klick(tr(Str::EditorGet), ed, 0));
    add(pruefe("if/3: Get wie im Original: get( FLOAT, \"SET_PARM1\")",
               [] { return wert(0) == "get( FLOAT, \"SET_PARM1\")"; }));
    add(klick(tr(Str::EditorTag), ed, 0));
    add(pruefe("if/3: Tag wie im Original: tag( \"targetname\", ORIGIN)",
               [] { return wert(0) == "tag( \"targetname\", ORIGIN)"; }));
    add(klick(tr(Str::EditorRnd), ed, 0));
    add(pruefe("if/3: Rnd wie im Original: random( 0, 1 )",
               [] { return wert(0) == "random( 0, 1 )"; }));
    add(tu("Bereich 5 .. 12.5", [] {
        g_app->helpRangeLow[0] = 5.0F;
        g_app->helpRangeHigh[0] = 12.5F;
    }));
    add(klick(tr(Str::EditorRnd), ed, 0));
    add(pruefe("if/3: Rnd mit 5 .. 12.5 wie im Original: random( 5, 12.5 )",
               [] { return wert(0) == "random( 5, 12.5 )"; }));
    add(klick(tr(Str::EditorTag), ed, 0));
    add(klick(tr(Str::EditorRnd), ed, 2));
    add(pruefe("if/3: Rnd der dritten Spalte trifft Feld 2, nicht Feld 0",
               [] { return wert(2).rfind("random(", 0) == 0 &&
                           wert(0).rfind("tag(", 0) == 0; }));
    add(klick(tr(Str::EditorCancel), ed));
    add(pruefe("if/3: Cancel verwirft die Helferwerte",
               [=] { return skriptText() == *vorher; }));

    // ---- Re-Evaluate -----------------------------------------------------
    add(oeffne("set", 0));
    add(klick("##v", ed, 0));
    add(tippe("geaendert"));
    add(pruefe("set: Feld 1 zeigt den getippten Text",
               [] { return wert(1) == "geaendert"; }));
    auto auswahl0 = std::make_shared<std::string>();
    add(tu("Auswahl merken", [=] { *auswahl0 = wert(0); }));
    add(klick(tr(Str::EditorReEvaluate), ed));
    add(pruefe("set: Re-Evaluate setzt Feld 1 auf die Vorgabe zurueck",
               [] { return wert(1) != "geaendert"; }));
    add(pruefe("set: Re-Evaluate laesst die Auswahl (Feld 0) stehen",
               [=] { return wert(0) == *auswahl0; }));
    add(klick(tr(Str::EditorCancel), ed));

    // ---- Klappliste ------------------------------------------------------
    add(oeffne("affect", 0));
    add(pruefe("affect: Feld 1 steht auf FLUSH", [] { return wert(1) == "FLUSH"; }));
    add(klick("##combo", ed));
    add(klick("INSERT", "", 0, true));
    add(pruefe("affect: Auswahl INSERT steht im Feld",
               [] { return wert(1) == "INSERT"; }));
    add(klick(tr(Str::EditorOk), ed));
    add(pruefe("affect: Ok schreibt INSERT ins Skript",
               [] { return argText("affect", 0, 1) == "INSERT"; }));

    // ---- Befehle ohne Felder und mit Dateifeld ---------------------------
    add(tu("Skript merken", [=] { *vorher = skriptText(); }));
    add(oeffne("else", 0));
    add(klick(tr(Str::EditorOk), ed));
    add(pruefe("else: Ok ohne Felder stuerzt nicht und aendert nichts",
               [=] { return !g_app->editorOpen && skriptText() == *vorher; }));

    add(oeffne("sound", 0));
    // "..." (Browse) NICHT klicken: seit rc568 oeffnet es wie im Original
    // einen Windows-Dateidialog, und der haelt den Lauf an, bis jemand ihn
    // schliesst. Geprueft wird nur, dass der Knopf da und ganz sichtbar ist.
    add(pruefe("sound: Knopf \"...\" (Browse) ist da und ganz sichtbar", [=] {
        const Element* e = finde(tr(Str::EditorBrowse), ed, 0, false);
        return e != nullptr && ganzSichtbar(*e);
    }));
    add(klick(tr(Str::EditorPlay), ed));
    add(pruefe("sound: Play laesst den Editor offen",
               [] { return g_app->editorOpen; }));
    add(klick(tr(Str::EditorCancel), ed));

    add(oeffne("camera", 0));
    add(klick(tr(Str::EditorOk), ed));
    add(pruefe("camera PAN: Ok ohne Aenderung laesst das Skript gleich",
               [=] { return skriptText() == *vorher; }));

    // ---- Aktionsspalte ---------------------------------------------------
    add(tu("Skript merken", [=] { *vorher = skriptText(); }));
    add(waehle("print", 0));
    add(klick(tr(Str::ActClone)));
    add(pruefe("Clone: print zweimal im Skript", [] { return anzahl("print") == 2; }));
    add(klick(tr(Str::EditUndo)));
    add(pruefe("Undo: Skript wie vorher", [=] { return skriptText() == *vorher; }));
    add(klick(tr(Str::EditRedo)));
    add(pruefe("Redo: print wieder zweimal", [] { return anzahl("print") == 2; }));
    add(klick(tr(Str::EditUndo)));
    add(pruefe("Undo nach Redo: Skript wie vorher",
               [=] { return skriptText() == *vorher; }));

    add(waehle("wait", 0));
    add(tu("Anzahl merken", [=] { *zahl = anzahl("wait"); }));
    add(klick(tr(Str::ActDelete)));
    add(pruefe("Delete: ein wait weniger", [=] { return anzahl("wait") == *zahl - 1; }));
    add(klick(tr(Str::EditUndo)));
    add(pruefe("Undo nach Delete: Skript wie vorher",
               [=] { return skriptText() == *vorher; }));

    add(waehle("print", 0));
    add(klick(tr(Str::ActCopy)));
    add(klick(tr(Str::ActPaste)));
    add(pruefe("Copy + Paste: print zweimal", [] { return anzahl("print") == 2; }));
    add(klick(tr(Str::EditUndo)));
    add(pruefe("Undo nach Paste: Skript wie vorher",
               [=] { return skriptText() == *vorher; }));

    add(waehle("print", 0));
    add(klick(tr(Str::ActCut)));
    add(pruefe("Cut: print weg", [] { return anzahl("print") == 0; }));
    add(klick(tr(Str::EditUndo)));
    add(pruefe("Undo nach Cut: Skript wie vorher",
               [=] { return skriptText() == *vorher; }));

    add(waehle("print", 0));
    add(klick(tr(Str::ActRem)));
    add(pruefe("Rem: print ist auskommentiert", [] { return anzahl("print") == 0; }));
    add(klick(tr(Str::EditUndo)));
    add(pruefe("Undo nach Rem: Skript wie vorher",
               [=] { return skriptText() == *vorher; }));

    add(waehle("set", 0));
    add(tu("Stelle merken", [=] {
        Path weg;
        *zahl = wegZu("set", 0, weg) ? static_cast<int>(weg[0]) : -1;
    }));
    add(klick(tr(Str::MoveUp)));
    add(pruefe("Up: set eine Stelle hoeher", [=] {
        Path weg;
        return wegZu("set", 0, weg) && static_cast<int>(weg[0]) == *zahl - 1;
    }));
    add(klick(tr(Str::MoveDown)));
    add(pruefe("Down: set wieder an alter Stelle",
               [=] { return skriptText() == *vorher; }));

    add(klick(tr(Str::ActFind)));
    add(pruefe("Find: Suchfenster offen", [] { return g_app->findOpen; }));
    add(tu("Suchfenster schliessen", [] { g_app->findOpen = false; }));

    // Beim Laden sind Bloecke zu. Erst alles auf, dann alles zu - und die
    // Zahlen ins Protokoll, damit ein Fehler sagt, was er gesehen hat.
    add(tu("Zeilen merken", [=] {
        *zeilen = g_app->rows.size();
        diag::detail("Knopftest: Zeilen vorher " + std::to_string(*zeilen));
    }));
    add(klick(tr(Str::TreeExpandAll), "", 0, false, tr(Str::TreeCollapseAll)));
    add(pruefe("Expand all: mehr Zeilen im Baum", [=] {
        const Node* k = knoten("if", 0);
        diag::detail("Knopftest: Zeilen nach Expand " +
                     std::to_string(g_app->rows.size()) + ", if#0 Kinder " +
                     std::to_string(k != nullptr ? k->children.size() : 0U) +
                     ", Kennung " + std::to_string(k != nullptr ? k->kennung : 0U) +
                     ", offen " +
                     std::to_string(k != nullptr && g_app->expanded.isOpen(k->kennung)));
        return g_app->rows.size() > *zeilen;
    }));
    add(klick(tr(Str::TreeCollapseAll)));
    add(pruefe("Collapse all: wieder so wenige Zeilen wie vorher", [=] {
        diag::detail("Knopftest: Zeilen nach Collapse " +
                     std::to_string(g_app->rows.size()));
        return g_app->rows.size() == *zeilen;
    }));

    add(klick(tr(Str::TreeShowTypes)));
    add(pruefe("Show Types: eingeschaltet", [] { return g_app->treeOpt.showTypes; }));
    add(klick(tr(Str::TreeShowTypes)));
    add(pruefe("Show Types: wieder aus", [] { return !g_app->treeOpt.showTypes; }));
    add(tu("gFloats merken", [=] { *zahl = g_app->treeOpt.gFloats ? 1 : 0; }));
    add(klick(tr(Str::TreeGFloats)));
    add(pruefe("%g floats: umgeschaltet",
               [=] { return (g_app->treeOpt.gFloats ? 1 : 0) != *zahl; }));
    add(klick(tr(Str::TreeGFloats)));
    add(pruefe("%g floats: zurueck",
               [=] { return (g_app->treeOpt.gFloats ? 1 : 0) == *zahl; }));
    add(tu("Makros merken", [=] { *zahl = g_app->treeOpt.foldMacros ? 1 : 0; }));
    add(klick(tr(Str::TreeFoldMacros)));
    add(pruefe("Fold macros: umgeschaltet",
               [=] { return (g_app->treeOpt.foldMacros ? 1 : 0) != *zahl; }));
    add(klick(tr(Str::TreeFoldMacros)));
    add(pruefe("Fold macros: zurueck",
               [=] { return (g_app->treeOpt.foldMacros ? 1 : 0) == *zahl; }));

    add(klick(tr(Str::AppPrefs)));
    add(pruefe("Prefs: Einstellungen offen", [] { return g_app->prefsOpen; }));
    add(tu("Einstellungen schliessen", [] { g_app->prefsOpen = false; }));
    add(klick(tr(Str::AppAbout)));
    add(pruefe("About: Fenster offen", [] { return g_app->aboutOpen; }));
    add(tu("About schliessen", [] { g_app->aboutOpen = false; }));

    add(pruefe("Skript nach allen Aktionen wie vorher",
               [=] { return skriptText() == *vorher; }));

    // Suche, Windows-Zwischenablage, Speichernfrage: originalSchritte().
    for (Schritt& x : originalSchritte()) { s.push_back(std::move(x)); }

    // New zuletzt: es oeffnet einen neuen Reiter.
    add(tu("Reiter merken", [=] { *zahl = static_cast<int>(g_app->tabs.size()); }));
    add(klick(tr(Str::FileNew)));
    add(pruefe("New: fragt \"New?\" (Yes/No query an)", [] {
        return g_app->frageOffen && g_app->frageText == tr(Str::AskNew);
    }));
    add(klick(tr(Str::AnswerYes), "###frage"));
    add(pruefe("New: ein leeres Skript im neuen Reiter", [=] {
        return static_cast<int>(g_app->tabs.size()) == *zahl + 1 &&
               g_app->doc.script().nodes.empty();
    }));

    return s;
}

// --- Jeder Befehl: eintragen, speichern, laden, pruefen, aendern ---------
//
// shank: "bei jedem Event parameter testen alles einzutragen dann speichern
// und wieder oeffnen jeden paramter zu aendern filter auszuprobieren."
//
// Fuer JEDEN Befehl der .bhc (auch beide Fassungen von if, set, ...):
//
//   1. einfuegen wie ein Klick in die Ereignisliste, Editor oeffnen
//   2. jedes Feld fuellen: Textfelder antippen und tippen, Klapplisten
//      aufklappen, im Suchfeld FILTERN, pruefen, dass nur Passendes
//      uebrig ist, und den Treffer anklicken
//   3. Ok - im Skript muss jeder Wert stehen
//
// Danach fuer alle zusammen: Pfad setzen, den echten Save-Knopf druecken,
// die Datei neu laden (byte-gleich?), jeden Befehl wieder oeffnen und jeden
// Wert vergleichen. Dann Runde 2 mit anderen Werten, noch einmal speichern,
// laden, vergleichen.
//
// Welche Felder es gibt, steht erst NACH der Wahl im ersten Feld fest
// (`set` mit SET_ANIM_BOTH hat andere Felder als mit SET_PARM1, `camera`
// PAN andere als ENABLE). Deshalb wird Feld fuer Feld entschieden: ein
// Schritt schaut nach, was im Editor steht, und haengt die Schritte fuer
// genau dieses Feld und den naechsten Nachschauer hinter sich ein.

std::vector<Schritt>* g_liste = nullptr;
std::size_t g_einfuegeAn = 0;

void schritteEinfuegen(std::vector<Schritt> neu) {
    if (g_liste == nullptr) { return; }
    const std::size_t n = neu.size();
    g_liste->insert(g_liste->begin() + static_cast<std::ptrdiff_t>(g_einfuegeAn),
                    std::make_move_iterator(neu.begin()),
                    std::make_move_iterator(neu.end()));
    g_einfuegeAn += n;
}

struct BefehlsPlan {
    std::vector<const Command*> befehle;
    // Je Befehl: fester Eintrag fuer die erste Klappliste (leer = frei
    // gewaehlt). Runde 3 geht damit jeden Eintrag einmal durch.
    std::vector<std::string> zwang;
    std::vector<std::vector<std::string>> erwartet;   // je Befehl, je Feld
    std::string datei;
};

std::string klein(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string befehlsName(const BefehlsPlan& p, std::size_t i) {
    std::string n = p.befehle[i]->name + "/" +
                    std::to_string(p.befehle[i]->params.size());
    if (i < p.zwang.size() && !p.zwang[i].empty()) { n += " " + p.zwang[i]; }
    return n + " (#" + std::to_string(i) + ")";
}

// Der i-te Befehl auf oberster Ebene. Leerzeilen zaehlen nicht - nach dem
// Laden stehen hinter Bloecken welche.
bool befehlsWeg(std::size_t i, Path& weg) {
    std::size_t gefunden = 0;
    const std::vector<Node>& n = g_app->doc.script().nodes;
    for (std::size_t k = 0; k < n.size(); ++k) {
        if (n[k].kind != Node::Kind::Command) { continue; }
        if (gefunden == i) {
            weg = Path{k};
            return true;
        }
        ++gefunden;
    }
    return false;
}

std::size_t befehlsZahl() {
    std::size_t z = 0;
    for (const Node& n : g_app->doc.script().nodes) {
        if (n.kind == Node::Kind::Command) { ++z; }
    }
    return z;
}

// Was in ein Textfeld getippt wird. Runde 0 und 1 verschieden, damit das
// Aendern sichtbar ist. Formate wie beim Speichern: Zahlen mit drei
// Nachkommastellen, Vektoren ohne Klammern.
std::string testwert(const Param& p, int runde, std::size_t i, std::size_t k,
                     bool ausdruck) {
    const bool a = (runde == 0);
    if (p.kind == Param::Kind::Op) { return a ? ">" : "<"; }
    if (ausdruck) { return a ? "random( 1.000, 2.000 )" : "random( 3.000, 4.000 )"; }
    switch (p.kind) {
        case Param::Kind::Int:    return a ? "7" : "12";
        case Param::Kind::Float:  return a ? "1.500" : "2.250";
        case Param::Kind::Vector: return a ? "1.000 2.000 3.000" : "4.000 5.000 6.000";
        default: break;
    }
    if (!p.filter.empty()) { return a ? "sound/test_a.mp3" : "sound/test_b.mp3"; }
    return std::string(a ? "test_a_" : "test_b_") + std::to_string(i) + "_" +
           std::to_string(k);
}

std::string vergleiche(const std::vector<std::string>& soll,
                       const std::vector<std::string>& ist) {
    if (soll.size() != ist.size()) {
        return "Feldzahl " + std::to_string(ist.size()) + " statt " +
               std::to_string(soll.size());
    }
    for (std::size_t k = 0; k < soll.size(); ++k) {
        if (soll[k] != ist[k]) {
            return "Feld " + std::to_string(k) + ": \"" + ist[k] + "\" statt \"" +
                   soll[k] + "\"";
        }
    }
    return {};
}

std::vector<std::string> argTexte(const Node* n) {
    std::vector<std::string> v;
    if (n != nullptr) {
        for (const Arg& a : n->args) { v.push_back(a.text); }
    }
    return v;
}

Schritt oeffneBefehl(std::shared_ptr<BefehlsPlan> plan, std::size_t i) {
    const std::string was = "Oeffne " + befehlsName(*plan, i);
    return {was, [=](int b) {
                if (b == 0) {
                    Path weg;
                    if (!befehlsWeg(i, weg)) {
                        meldeFehler(was + ": nicht im Skript");
                        return true;
                    }
                    openEditorForNode(weg);
                    return false;
                }
                ImGuiWindow* w = ImGui::FindWindowByName("###editor");
                if (w != nullptr && w->Active && !w->Hidden && b >= 3) { return true; }
                if (b > 30) {
                    meldeFehler(was + ": Editor erscheint nicht");
                    return true;
                }
                return false;
            }};
}

// Sieht nach, welches Feld `k` gerade ist, und haengt die Schritte dafuer
// samt dem Nachschauer fuer `k + 1` hinter sich ein.
Schritt fuelleAb(std::shared_ptr<BefehlsPlan> plan, std::size_t i, int runde,
                 std::size_t k) {
    const std::string was = "Fuelle " + befehlsName(*plan, i) + " Feld " +
                            std::to_string(k);
    return {was, [=](int) {
        if (!g_app->editorOpen || g_app->editorCmd == nullptr) {
            meldeFehler(was + ": Editor ist zu");
            return true;
        }
        const std::vector<Param> f =
            editorFields(*g_app->editorCmd, g_app->db, g_app->editorValues);
        std::vector<std::string>& erw = plan->erwartet[i];
        if (k >= f.size()) {
            erw.resize(f.size());
            return true;
        }
        if (erw.size() < f.size()) { erw.resize(f.size()); }
        // Das wievielte Textfeld / die wievielte Klappliste ist Feld k?
        int nText = 0;
        int nListe = 0;
        for (std::size_t q = 0; q < k; ++q) {
            if (f[q].kind == Param::Kind::TypeSet) { ++nListe; } else { ++nText; }
        }
        const Param& p = f[k];
        const std::string ed = "###editor";
        const std::string feld = befehlsName(*plan, i) + " Feld " + std::to_string(k);
        std::vector<Schritt> neu;
        const TypeSet* ts = (p.kind == Param::Kind::TypeSet)
                                ? g_app->db.typeset(p.typeset)
                                : nullptr;
        if (p.locked) {
            erw[k] = wert(k);
        } else if (ts != nullptr && !ts->entries.empty()) {
            // Ein Eintrag, der noch nicht gewaehlt ist - fest aus Befehl,
            // Feld und Runde, damit jeder Lauf dasselbe tut.
            const std::size_t n = ts->entries.size();
            std::size_t idx = (i * 31U + k * 7U + static_cast<std::size_t>(runde) * 13U) % n;
            if (ts->entries[idx].name == wert(k) && n > 1) { idx = (idx + 1U) % n; }
            const bool gezwungen =
                k == 0 && i < plan->zwang.size() && !plan->zwang[i].empty();
            const std::string ziel = gezwungen ? plan->zwang[i] : ts->entries[idx].name;
            // Gefiltert wird klein geschrieben: die Suche soll Gross- und
            // Kleinschreibung nicht unterscheiden.
            const std::string filter = klein(ziel);
            neu.push_back(klick("##combo", ed, nListe));
            neu.push_back(tippe(filter));
            neu.push_back(pruefe(feld + ": Filter \"" + filter + "\" zeigt nur Passendes",
                                 [=] {
                bool zielDa = false;
                int zeilen = 0;
                for (const Element& e : g_elemente) {
                    if (e.fenster.find("##Combo") == std::string::npos) { continue; }
                    if (e.label.empty() || e.label.rfind("##", 0) == 0) { continue; }
                    if (e.label.find("###eigen") != std::string::npos) { continue; }
                    ++zeilen;
                    if (e.label == ziel) { zielDa = true; }
                    if (klein(e.label).find(filter) == std::string::npos) {
                        diag::detail("Knopftest: Filter liess \"" + e.label + "\" stehen");
                        return false;
                    }
                }
                return zielDa && zeilen > 0 &&
                       static_cast<std::size_t>(zeilen) <= n;
            }));
            neu.push_back(klick(ziel, "##Combo"));
            neu.push_back(pruefe(feld + ": \"" + ziel + "\" gewaehlt",
                                 [=] { return wert(k) == ziel; }));
            neu.push_back(tu(feld + " merken", [=] { plan->erwartet[i][k] = ziel; }));
        } else {
            const std::string w = testwert(p, runde, i, k, istAusdruck(k));
            neu.push_back(klick("##v", ed, nText));
            neu.push_back(tippe(w));
            neu.push_back(pruefe(feld + ": \"" + w + "\" steht im Feld", [=] {
                if (wert(k) != w) {
                    diag::detail("Knopftest: im Feld steht \"" + wert(k) + "\", " +
                                 wasImGuiSieht());
                }
                return wert(k) == w;
            }));
            neu.push_back(tu(feld + " merken", [=] { plan->erwartet[i][k] = w; }));
        }
        neu.push_back(fuelleAb(plan, i, runde, k + 1));
        schritteEinfuegen(std::move(neu));
        return true;
    }};
}

// Alle Befehle wieder oeffnen und jeden Wert mit dem Erwarteten vergleichen.
void pruefeAlle(std::vector<Schritt>& s, std::shared_ptr<BefehlsPlan> plan,
                const std::string& wann) {
    for (std::size_t i = 0; i < plan->befehle.size(); ++i) {
        s.push_back(oeffneBefehl(plan, i));
        s.push_back(pruefe(befehlsName(*plan, i) + ": " + wann, [=] {
            const std::string d = vergleiche(plan->erwartet[i], g_app->editorValues);
            if (!d.empty()) { diag::detail("Knopftest: " + d); }
            return d.empty();
        }));
        s.push_back(klick(tr(Str::EditorCancel), "###editor"));
    }
}

// Speichern ueber den echten Save-Knopf, dann neu laden.
void speichernUndLaden(std::vector<Schritt>& s, std::shared_ptr<BefehlsPlan> plan,
                       const std::string& runde) {
    auto inhalt = std::make_shared<std::string>();
    s.push_back(tu("Pfad setzen", [=] { g_app->path = plan->datei; }));
    s.push_back(klick(tr(Str::FileSave)));
    s.push_back(pruefe("Save (" + runde + "): Datei geschrieben, Skript gilt als gespeichert",
                       [=] {
        *inhalt = slurp(plan->datei);
        return !inhalt->empty() && !g_app->doc.dirty();
    }));
    s.push_back(tu("Datei neu laden", [=] { ladeSkriptDatei(plan->datei); }));
    s.push_back(pruefe("Laden (" + runde + "): Skript byte-gleich mit der Datei",
                       [=] { return writeScript(g_app->doc.script()) == *inhalt; }));
    s.push_back(pruefe("Laden (" + runde + "): alle Befehle da", [=] {
        return befehlsZahl() == plan->befehle.size();
    }));
}

std::vector<Schritt> befehlSchritte() {
    std::vector<Schritt> s;
    auto plan = std::make_shared<BefehlsPlan>();
    for (const Command& c : g_app->db.commands) { plan->befehle.push_back(&c); }
    plan->erwartet.resize(plan->befehle.size());
    plan->datei = platform::executableDirectory() + "\\selbsttest-befehle.txt";
    diag::detail("Knopftest: Befehlstest mit " +
                 std::to_string(plan->befehle.size()) + " Befehlen");

    s.push_back(tu("Leeres Skript fuer den Befehlstest", [] {
        openScriptFromMemory("//Generated by BehavEd\n", "befehle.txt");
        g_app->selectedPath.clear();
        g_app->selection.clear();
    }));
    // Runde 1: einfuegen und fuellen.
    for (std::size_t i = 0; i < plan->befehle.size(); ++i) {
        const Command* c = plan->befehle[i];
        s.push_back(tu("Einfuegen " + befehlsName(*plan, i),
                       [=] { fuegeBefehlEin(*c); }));
        s.push_back(oeffneBefehl(plan, i));
        s.push_back(fuelleAb(plan, i, 0, 0));
        s.push_back(klick(tr(Str::EditorOk), "###editor"));
        s.push_back(pruefe(befehlsName(*plan, i) + ": Ok schreibt alle Werte", [=] {
            Path weg;
            const Node* n = befehlsWeg(i, weg) ? nodeAt(g_app->doc.script(), weg) : nullptr;
            const std::string d = vergleiche(plan->erwartet[i], argTexte(n));
            if (!d.empty()) { diag::detail("Knopftest: " + d); }
            return n != nullptr && n->name == c->name && d.empty();
        }));
    }
    speichernUndLaden(s, plan, "Runde 1");
    pruefeAlle(s, plan, "nach Speichern und Laden alle Werte (Runde 1)");

    // Runde 2: jeden Wert aendern.
    for (std::size_t i = 0; i < plan->befehle.size(); ++i) {
        s.push_back(oeffneBefehl(plan, i));
        s.push_back(fuelleAb(plan, i, 1, 0));
        s.push_back(klick(tr(Str::EditorOk), "###editor"));
        s.push_back(pruefe(befehlsName(*plan, i) + ": Ok schreibt alle geaenderten Werte",
                           [=] {
            Path weg;
            const Node* n = befehlsWeg(i, weg) ? nodeAt(g_app->doc.script(), weg) : nullptr;
            const std::string d = vergleiche(plan->erwartet[i], argTexte(n));
            if (!d.empty()) { diag::detail("Knopftest: " + d); }
            return d.empty();
        }));
    }
    speichernUndLaden(s, plan, "Runde 2");
    pruefeAlle(s, plan, "nach Speichern und Laden alle geaenderten Werte (Runde 2)");

    // Aufraeumen: Testdatei weg, auch aus der Liste der zuletzt geoeffneten.
    s.push_back(tu("Testdatei entfernen", [=] {
        std::remove(plan->datei.c_str());
        auto& r = g_app->settings.recent;
        r.erase(std::remove(r.begin(), r.end(), plan->datei), r.end());
    }));

    // Runde 3: JEDER Eintrag der ersten Klappliste jedes Befehls.
    //
    // Runde 1 und 2 waehlen je Befehl einen Eintrag. `set` hat aber rund
    // dreihundert SET_-Typen, `camera` ein Dutzend Unterbefehle, und jeder
    // bringt eigene Felder mit (Zahl, Text, Vektor, wieder eine Klappliste).
    // Hier wird jeder davon einmal gewaehlt, alle Folgefelder gefuellt,
    // gespeichert, geladen und verglichen.
    auto alle = std::make_shared<BefehlsPlan>();
    for (const Command& c : g_app->db.commands) {
        if (c.params.empty() || c.params[0].kind != Param::Kind::TypeSet ||
            c.params[0].locked) {
            continue;
        }
        const TypeSet* ts = g_app->db.typeset(c.params[0].typeset);
        if (ts == nullptr) { continue; }
        for (const TypeEntry& e : ts->entries) {
            alle->befehle.push_back(&c);
            alle->zwang.push_back(e.name);
        }
    }
    alle->erwartet.resize(alle->befehle.size());
    alle->datei = platform::executableDirectory() + "\\selbsttest-eintraege.txt";
    diag::detail("Knopftest: Eintragstest mit " +
                 std::to_string(alle->befehle.size()) + " Eintraegen");
    s.push_back(tu("Leeres Skript fuer den Eintragstest", [] {
        openScriptFromMemory("//Generated by BehavEd\n", "eintraege.txt");
        g_app->selectedPath.clear();
        g_app->selection.clear();
    }));
    for (std::size_t i = 0; i < alle->befehle.size(); ++i) {
        const Command* c = alle->befehle[i];
        s.push_back(tu("Einfuegen " + befehlsName(*alle, i),
                       [=] { fuegeBefehlEin(*c); }));
        s.push_back(oeffneBefehl(alle, i));
        s.push_back(fuelleAb(alle, i, 0, 0));
        s.push_back(klick(tr(Str::EditorOk), "###editor"));
        s.push_back(pruefe(befehlsName(*alle, i) + ": Ok schreibt alle Werte", [=] {
            Path weg;
            const Node* n = befehlsWeg(i, weg) ? nodeAt(g_app->doc.script(), weg) : nullptr;
            const std::string d = vergleiche(alle->erwartet[i], argTexte(n));
            if (!d.empty()) { diag::detail("Knopftest: " + d); }
            return n != nullptr && d.empty();
        }));
    }
    speichernUndLaden(s, alle, "Eintraege");
    pruefeAlle(s, alle, "nach Speichern und Laden alle Werte (Eintraege)");
    s.push_back(tu("Testdatei entfernen", [=] {
        std::remove(alle->datei.c_str());
        auto& r = g_app->settings.recent;
        r.erase(std::remove(r.begin(), r.end(), alle->datei), r.end());
    }));
    return s;
}

// --- Geteilte Ansicht ------------------------------------------------------
//
// shank: "teste noch split view von scripts, dass die Fenster stabil bleiben
// und dann auch, dass man von einem script zum anderen ... rueber ziehen kann,
// copieren und einfuegen, die Sachen oeffnen kann, die Werte alle richtig
// gespeichert sind."
//
// Zwei eigene Skripte A und B. B ist aktiv und steht im Hauptfeld (Feld 0),
// A im Nebenfeld (Feld 1). Geprueft wird jeweils das SKRIPT beider Reiter,
// nicht das Bild.

const char* const kSplitA =
    "//Generated by BehavEd\n"
    "\n"
    "print ( \"A1\" );\n"
    "wait ( 111.000 );\n"
    "affect ( \"a_ent\", /*@AFFECT_TYPE*/ FLUSH )\n"
    "{\n"
    "\twait ( 112.000 );\n"
    "}\n"
    "\n";

const char* const kSplitB =
    "//Generated by BehavEd\n"
    "\n"
    "print ( \"B1\" );\n"
    "set ( /*@SET_TYPES*/ \"SET_PARM1\", \"b_wert\" );\n";

struct Geteilt {
    int a = -1;           // Reiter von A
    int b = -1;           // Reiter von B
    int c = -1;           // leeres Skript im Hauptfeld
    int d = -1;           // leeres Skript im Nebenfeld
    std::string dateiA;
    std::string dateiB;
    std::string textA;    // was gespeichert werden sollte
    std::string textB;
};

const Script& skriptVon(int tab) {
    if (tab == g_app->activeTab || tab < 0 ||
        tab >= static_cast<int>(g_app->tabs.size())) {
        return g_app->doc.script();
    }
    return g_app->tabs[static_cast<std::size_t>(tab)].doc.script();
}

// Die Befehlsnamen auf oberster Ebene, als "print wait set".
std::string folge(int tab) {
    std::string s;
    for (const Node& n : skriptVon(tab).nodes) {
        if (n.kind != Node::Kind::Command) { continue; }
        if (!s.empty()) { s += ' '; }
        s += n.name;
    }
    return s;
}

std::string erstesArg(int tab, const std::string& befehl) {
    for (const Node& n : skriptVon(tab).nodes) {
        if (n.kind == Node::Kind::Command && n.name == befehl && !n.args.empty()) {
            return n.args[0].text;
        }
    }
    return "<fehlt>";
}

// In einer aufgeklappten Liste waehlen - notfalls erst rollen.
//
// Eine Klappliste zeigt acht Eintraege. Steht der gesuchte weiter unten,
// ist er nicht gezeichnet und damit nicht zu finden; ein Mensch wuerde das
// Mausrad drehen. Genau das: ueber die Liste fahren, drei Rasten nach unten,
// neu suchen. Hoechstens zehnmal.
Schritt waehleInListe(const std::string& label, bool praefix, int versuch = 0,
                      const std::string& fenster = "##Combo") {
    return {"Waehle \"" + label + "\" in der Liste", [=](int b) {
                if (finde(label, fenster, 0, praefix) != nullptr) {
                    schritteEinfuegen({klick(label, fenster, 0, praefix)});
                    return true;
                }
                if (versuch >= 10) {
                    meldeFehler("Liste: \"" + label + "\" auch nach dem Rollen nicht da");
                    return true;
                }
                if (b == 0) {
                    const Element* irgendeins = nullptr;
                    for (const Element& e : g_elemente) {
                        if (e.fenster.find(fenster) != std::string::npos) {
                            irgendeins = &e;
                            break;
                        }
                    }
                    if (irgendeins == nullptr) {
                        meldeFehler("Liste: keine Liste offen fuer \"" + label + "\"");
                        return true;
                    }
                    setzeMaus(irgendeins->rect.GetCenter());
                    return false;
                }
                if (b == 1) {
                    ImGui::GetIO().AddMouseWheelEvent(0.0F, -3.0F);
                    return false;
                }
                if (b < 4) { return false; }
                schritteEinfuegen({waehleInListe(label, praefix, versuch + 1, fenster)});
                return true;
            }};
}

// Menue File -> Split view -> n
void splitMenue(std::vector<Schritt>& s, int n) {
    s.push_back(klick(tr(Str::MenuFile)));
    s.push_back(klick(tr(Str::SplitToggle), "##Menu"));
    s.push_back(klick(std::to_string(n), "##Menu"));
}

std::vector<Schritt> geteiltSchritte() {
    std::vector<Schritt> s;
    auto g = std::make_shared<Geteilt>();
    g->dateiA = platform::executableDirectory() + "\\selbsttest-split_a.txt";
    g->dateiB = platform::executableDirectory() + "\\selbsttest-split_b.txt";

    s.push_back(tu("Skript A oeffnen", [=] {
        openScriptFromMemory(kSplitA, "split_a.txt");
        g->a = g_app->activeTab;
    }));
    s.push_back(tu("Skript B oeffnen", [=] {
        openScriptFromMemory(kSplitB, "split_b.txt");
        g->b = g_app->activeTab;
        diag::detail("Knopftest: A = Reiter " + std::to_string(g->a) +
                     ", B = Reiter " + std::to_string(g->b));
    }));

    // ---- Zwei Felder --------------------------------------------------
    splitMenue(s, 2);
    s.push_back(pruefe("Split 2: zwei Felder, Fokus im ersten, B aktiv", [=] {
        diag::detail("Knopftest: Felder " + std::to_string(g_app->splitCount) +
                     ", Fokus " + std::to_string(g_app->focusPane) + ", aktiv " +
                     std::to_string(g_app->activeTab));
        return g_app->splitCount == 2 && g_app->focusPane == 0 &&
               g_app->activeTab == g->b;
    }));
    s.push_back(stabil("Split 2 nach dem Umschalten"));
    // A ins Nebenfeld holen - ueber die Auswahl oben im Feld.
    s.push_back(klick("##splitpick1", "/pane1_"));
    s.push_back(waehleInListe("split_a.txt", false));
    s.push_back(pruefe("Nebenfeld zeigt A", [=] { return g_app->splitPanes[1].tab == g->a; }));
    s.push_back(stabil("Split 2 mit A im Nebenfeld"));
    s.push_back(pruefe("Ausgang: A = \"print wait affect\", B = \"print set\"", [=] {
        return folge(g->a) == "print wait affect" && folge(g->b) == "print set";
    }));

    // ---- Ziehen Nebenfeld -> Hauptfeld ---------------------------------
    s.push_back(ziehe("wait", "/pane1_", "print", "/pane0_"));
    s.push_back(pruefe("Ziehen A -> B: wait hinter print in B, A unveraendert", [=] {
        diag::detail("Knopftest: A = " + folge(g->a) + " | B = " + folge(g->b));
        return folge(g->b) == "print wait set" && erstesArg(g->b, "wait") == "111.000" &&
               folge(g->a) == "print wait affect";
    }));
    // ---- Ziehen Hauptfeld -> Nebenfeld ---------------------------------
    s.push_back(ziehe("set", "/pane0_", "print", "/pane1_"));
    s.push_back(pruefe("Ziehen B -> A: set hinter print in A, B unveraendert", [=] {
        diag::detail("Knopftest: A = " + folge(g->a) + " | B = " + folge(g->b));
        return folge(g->a) == "print set wait affect" &&
               erstesArg(g->a, "set") == "SET_PARM1";
    }));
    s.push_back(stabil("Split 2 nach dem Ziehen"));

    // ---- Kopieren im Nebenfeld (Rechtsklick), Einfuegen im Hauptfeld ---
    s.push_back(klick("affect", "/pane1_", 0, false, "", 1));
    s.push_back(klick(tr(Str::ActCopy), "##Popup"));
    s.push_back(klick("print", "/pane0_"));
    s.push_back(klick(tr(Str::ActPaste)));
    s.push_back(pruefe("Kopieren aus A, Einfuegen in B: affect samt Rumpf hinter print", [=] {
        diag::detail("Knopftest: A = " + folge(g->a) + " | B = " + folge(g->b));
        bool rumpf = false;
        for (const Node& n : skriptVon(g->b).nodes) {
            if (n.name == "affect") {
                rumpf = n.hasBlock && n.children.size() == 1 &&
                        n.children[0].name == "wait" && !n.children[0].args.empty() &&
                        n.children[0].args[0].text == "112.000";
            }
        }
        return folge(g->b).rfind("print affect", 0) == 0 && rumpf &&
               erstesArg(g->b, "affect") == "a_ent";
    }));

    // ---- Kopieren im Hauptfeld, Fokuswechsel, Einfuegen in A ----------
    // Erst eine andere Zeile: print ist vom Einfuegen noch ausgewaehlt, und
    // ein zweiter Klick auf eine ausgewaehlte Zeile hebt die Auswahl auf.
    s.push_back(klick("set", "/pane0_"));
    s.push_back(klick("print", "/pane0_"));
    s.push_back(pruefe("print in B ausgewaehlt", [=] {
        const Node* n = nodeAt(g_app->doc.script(), g_app->selectedPath);
        return n != nullptr && n->name == "print";
    }));
    s.push_back(klick(tr(Str::ActCopy)));
    s.push_back(klick("wait", "/pane1_"));   // Klick ins Nebenfeld -> Fokus
    s.push_back(pruefe("Fokuswechsel: Feld 1 hat den Fokus, A ist aktiv", [=] {
        return g_app->focusPane == 1 && g_app->activeTab == g->a;
    }));
    s.push_back(stabil("Split 2 nach dem Fokuswechsel"));
    s.push_back(klick(tr(Str::ActPaste)));
    s.push_back(pruefe("Einfuegen in A: ein zweites print (\"B1\")", [=] {
        diag::detail("Knopftest: A = " + folge(g->a) + " | B = " + folge(g->b));
        int prints = 0;
        bool b1 = false;
        for (const Node& n : skriptVon(g->a).nodes) {
            if (n.name == "print") {
                ++prints;
                if (!n.args.empty() && n.args[0].text == "B1") { b1 = true; }
            }
        }
        return prints == 2 && b1;
    }));
    s.push_back(pruefe("Nach dem Fokuswechsel: B unversehrt im Nebenfeld", [=] {
        return g_app->splitPanes[0].tab == g->b &&
               folge(g->b).rfind("print affect", 0) == 0;
    }));

    // ---- Wert im Editor aendern, im jeweils anderen Skript pruefen -----
    s.push_back(oeffne("wait", 0));
    s.push_back(klick("##v", "###editor"));
    s.push_back(tippe("222.000"));
    s.push_back(klick(tr(Str::EditorOk), "###editor"));
    s.push_back(pruefe("Editor in A: wait = 222.000, B behaelt 111.000", [=] {
        return erstesArg(g->a, "wait") == "222.000" &&
               erstesArg(g->b, "wait") == "111.000";
    }));

    // ---- Speichern mit "Save all", neu laden ---------------------------
    s.push_back(tu("Pfade fuer A und B setzen", [=] {
        for (int tab : {g->a, g->b}) {
            const std::string& d = (tab == g->a) ? g->dateiA : g->dateiB;
            if (tab == g_app->activeTab) {
                g_app->path = d;
            } else {
                g_app->tabs[static_cast<std::size_t>(tab)].path = d;
            }
        }
        g->textA = writeScript(skriptVon(g->a));
        g->textB = writeScript(skriptVon(g->b));
    }));
    s.push_back(klick(tr(Str::ActSaveAll)));
    s.push_back(pruefe("Save all: beide Dateien genau wie die Skripte", [=] {
        return slurp(g->dateiA) == g->textA && slurp(g->dateiB) == g->textB;
    }));
    s.push_back(stabil("Split 2 nach Save all"));
    s.push_back(tu("B neu laden", [=] { ladeSkriptDatei(g->dateiB); }));
    s.push_back(pruefe("B neu geladen: byte-gleich, Werte da", [=] {
        return writeScript(g_app->doc.script()) == g->textB &&
               erstesArg(g_app->activeTab, "wait") == "111.000" &&
               erstesArg(g_app->activeTab, "affect") == "a_ent";
    }));
    s.push_back(oeffne("set", 0));
    s.push_back(pruefe("B neu geladen: set im Editor = SET_PARM1 / b_wert", [] {
        return wert(0) == "SET_PARM1" && wert(1) == "b_wert";
    }));
    s.push_back(klick(tr(Str::EditorCancel), "###editor"));
    s.push_back(tu("A neu laden", [=] { ladeSkriptDatei(g->dateiA); }));
    s.push_back(pruefe("A neu geladen: byte-gleich, wait = 222.000", [=] {
        return writeScript(g_app->doc.script()) == g->textA &&
               erstesArg(g_app->activeTab, "wait") == "222.000";
    }));

    // ---- Freie Flaeche und leere Skripte --------------------------------
    //
    // shank, mit Bildern: "ich kann nichts in ein leeres Feld ziehen ... auch
    // nichts in das leere Feld unter den anderen, dass es darunter
    // eingefuegt wird."
    //
    // Zwei leere Skripte C (Hauptfeld) und D (Nebenfeld), dazu A mit
    // Inhalt. Gezogen wird jeweils auf eine Stelle weit unter der
    // Skriptauswahl des Feldes - dort steht keine Zeile.
    s.push_back(tu("Leeres Skript D oeffnen", [=] {
        openScriptFromMemory("//Generated by BehavEd\n", "split_d.txt");
        g->d = g_app->activeTab;
    }));
    s.push_back(tu("Leeres Skript C oeffnen", [=] {
        openScriptFromMemory("//Generated by BehavEd\n", "split_c.txt");
        g->c = g_app->activeTab;
        diag::detail("Knopftest: C = Reiter " + std::to_string(g->c) +
                     ", D = Reiter " + std::to_string(g->d) + ", Fokus " +
                     std::to_string(g_app->focusPane));
    }));
    // Ein neuer Reiter bringt seinen eigenen, ungeteilten Arbeitsbereich mit
    // (addTab) - also fuer C wieder zwei Felder.
    splitMenue(s, 2);
    s.push_back(klick("##splitpick1", "/pane1_"));
    s.push_back(waehleInListe("selbsttest-split_a", true));
    s.push_back(pruefe("Hauptfeld zeigt das leere C, Nebenfeld A", [=] {
        return g_app->focusPane == 0 && g_app->activeTab == g->c &&
               g_app->splitPanes[1].tab == g->a && folge(g->c).empty();
    }));
    const ImVec2 tief{0.0F, 250.0F};
    s.push_back(ziehe("wait", "/pane1_", "##splitpick0", "/pane0_", 0, 0, 0.5F, true, tief));
    s.push_back(pruefe("Ziehen A -> leeres C: wait steht in C", [=] {
        diag::detail("Knopftest: C = " + folge(g->c));
        return folge(g->c) == "wait" && erstesArg(g->c, "wait") == "222.000";
    }));
    s.push_back(ziehe("set", "/pane1_", "##splitpick0", "/pane0_", 0, 0, 0.5F, true, tief));
    s.push_back(pruefe("Ziehen A -> freie Flaeche unter C: set ans Ende", [=] {
        diag::detail("Knopftest: C = " + folge(g->c));
        return folge(g->c) == "wait set";
    }));
    s.push_back(ziehe("flush  ", "", "##splitpick0", "/pane0_", 0, 0, 0.5F, true, tief, true));
    s.push_back(pruefe("Ereignisliste -> freie Flaeche unter C: flush ans Ende", [=] {
        diag::detail("Knopftest: C = " + folge(g->c));
        return folge(g->c) == "wait set flush";
    }));
    s.push_back(ziehe("wait", "/pane0_", "##splitpick0", "/pane0_", 0, 0, 0.5F, true, tief));
    s.push_back(pruefe("Im selben Skript auf die freie Flaeche: wait ans Ende verschoben", [=] {
        diag::detail("Knopftest: C = " + folge(g->c));
        return folge(g->c) == "set flush wait";
    }));
    s.push_back(klick("##splitpick1", "/pane1_"));
    s.push_back(waehleInListe("split_d.txt", false));
    s.push_back(pruefe("Nebenfeld zeigt das leere D", [=] {
        return g_app->splitPanes[1].tab == g->d && folge(g->d).empty();
    }));
    s.push_back(ziehe("set", "/pane0_", "##splitpick1", "/pane1_", 0, 0, 0.5F, true, tief));
    s.push_back(pruefe("Ziehen C -> leeres D: set steht in D, C unveraendert", [=] {
        diag::detail("Knopftest: C = " + folge(g->c) + " | D = " + folge(g->d));
        return folge(g->d) == "set" && folge(g->c) == "set flush wait";
    }));
    s.push_back(ziehe("flush  ", "", "##splitpick1", "/pane1_", 0, 0, 0.5F, true, tief, true));
    s.push_back(pruefe("Ereignisliste -> freie Flaeche unter D: flush ans Ende", [=] {
        diag::detail("Knopftest: D = " + folge(g->d));
        return folge(g->d) == "set flush";
    }));
    s.push_back(ziehe("print  ", "", "set", "/pane1_", 0, 0, 0.5F, true, ImVec2{0.0F, 0.0F}, true));
    s.push_back(pruefe("Ereignisliste -> Zeile im Nebenfeld: print hinter set", [=] {
        diag::detail("Knopftest: D = " + folge(g->d));
        return folge(g->d) == "set print flush";
    }));
    s.push_back(ziehe("wait", "/pane0_", "set", "/pane1_"));
    s.push_back(pruefe("Ziehen C -> Zeile in D: wait hinter set", [=] {
        diag::detail("Knopftest: D = " + folge(g->d));
        return folge(g->d) == "set wait print flush";
    }));
    s.push_back(stabil("Split 2 nach dem Ablegen auf freie Flaechen"));

    // ---- Drei und vier Felder, Teiler ziehen ---------------------------
    splitMenue(s, 3);
    s.push_back(pruefe("Split 3: drei Felder", [] { return g_app->splitCount == 3; }));
    s.push_back(stabil("Split 3 nach dem Umschalten"));
    // Nebenfeld -> Nebenfeld: A in Feld 2 holen, dann aus D (Feld 1)
    // hinueber ziehen - einmal auf eine Zeile, einmal auf die freie Flaeche.
    s.push_back(klick("##splitpick2", "/pane2_"));
    s.push_back(waehleInListe("selbsttest-split_a", true));
    s.push_back(pruefe("Feld 1 zeigt D, Feld 2 zeigt A", [=] {
        return g_app->splitPanes[1].tab == g->d && g_app->splitPanes[2].tab == g->a;
    }));
    auto vorA = std::make_shared<std::string>();
    s.push_back(tu("A merken", [=] { *vorA = folge(g->a); }));
    s.push_back(ziehe("flush", "/pane1_", "print", "/pane2_"));
    s.push_back(pruefe("Nebenfeld D -> Zeile im Nebenfeld A: flush hinter print", [=] {
        diag::detail("Knopftest: A = " + folge(g->a));
        return folge(g->a).rfind("print flush", 0) == 0;
    }));
    s.push_back(ziehe("print", "/pane1_", "##splitpick2", "/pane2_", 0, 0, 0.5F, true,
                      ImVec2{0.0F, 250.0F}));
    s.push_back(pruefe("Nebenfeld D -> freie Flaeche in A: print ans Ende", [=] {
        diag::detail("Knopftest: A = " + folge(g->a));
        const std::string f = folge(g->a);
        return f.size() >= 5 && f.compare(f.size() - 5, 5, "print") == 0;
    }));
    s.push_back(stabil("Split 3 nach dem Ziehen zwischen Nebenfeldern"));
    splitMenue(s, 4);
    s.push_back(pruefe("Split 4: vier Felder", [] { return g_app->splitCount == 4; }));
    s.push_back(stabil("Split 4 nach dem Umschalten"));
    auto frac = std::make_shared<float>(0.0F);
    s.push_back(tu("Teilerstand merken", [=] { *frac = g_app->splitFracX; }));
    // In der Luecke greifen: die rechte Haelfte des Griffs liegt unter dem
    // rechten Feld, und dort nimmt das Feld die Maus.
    s.push_back(ziehe("##splitx", "###main", "##splitpick1", "/pane1_", 0, 0, 0.25F, false));
    s.push_back(pruefe("Teiler ziehen verschiebt die Felder", [=] {
        return std::fabs(g_app->splitFracX - *frac) > 0.01F;
    }));
    s.push_back(stabil("Split 4 nach dem Teilerzug"));
    splitMenue(s, 1);
    s.push_back(pruefe("Zurueck auf ein Feld", [] { return g_app->splitCount == 1; }));
    s.push_back(stabil("Ein Feld nach dem Zurueckschalten"));

    s.push_back(tu("Split-Testdateien entfernen", [=] {
        std::remove(g->dateiA.c_str());
        std::remove(g->dateiB.c_str());
        auto& r = g_app->settings.recent;
        r.erase(std::remove(r.begin(), r.end(), g->dateiA), r.end());
        r.erase(std::remove(r.begin(), r.end(), g->dateiB), r.end());
    }));
    return s;
}

// --- Karte: Mission laden, Layout, Kamera, Zeitleiste ----------------------
//
// shank: "load mission und nimm die mission hier (md_am_sith) ... wenn ich
// vor und zurueck scrolle bewegen sich die Tabs ... wir sollten alles in maps
// jetzt testen, dass alles geht und alles richtig ist, Cameras richtig
// bewegt und rotiert werden usw."
//
// --- Abgleich mit dem Original: jeder Befehl, jedes Feld, jeder Helfer ---
//
// BHED_EDITORTEST=vergleich. Laedt dieselben 27 Befehle, die das Original
// in scratchpad/original/reeval/alle.txt bekommen hat, oeffnet jeden, und
// schreibt je Zustand eine Zeile "Vergleich ..." ins Protokoll: Felder mit
// Art, Wert, Typangabe und Knopf, dazu alle beschrifteten Elemente des
// Fensters. Danach jeden Expr!/Helper-Knopf einzeln (und wieder zurueck).
// Keine Pruefungen - die Zeilen werden neben die des Originals gelegt.
const char* const kVergleich =
    "//Generated by BehavEd\n"
    "rem ( \"comment\" );\n"
    "flush (  );\n"
    "if ( $get( FLOAT, \"SET_HEALTH\")$, $=$, $1$ )\n{\n}\n"
    "else (  )\n{\n}\n"
    "loop ( -1 )\n{\n}\n"
    "affect ( \"npc_kyle1\", /*@AFFECT_TYPE*/ FLUSH )\n{\n}\n"
    "run ( \"areis/intro2\" );\n"
    "wait ( 1000.000 );\n"
    "waitsignal ( \"kyleNav1\" );\n"
    "signal ( \"luke1 look at kyle1\" );\n"
    "sound ( /*@CHANNELS*/ CHAN_VOICE, \"sound/chars/kyle/01kyk006.mp3\" );\n"
    "move ( < 0.000 0.000 0.000 >, < 0.000 0.000 0.000 >, 1000.000 );\n"
    "rotate ( < 0.000 0.000 0.000 >, 4000.000 );\n"
    "use ( \"students_stand\" );\n"
    "kill ( \"break_glass\" );\n"
    "remove ( \"npc_student1\" );\n"
    "print ( \"text\" );\n"
    "declare ( /*@DECLARE_TYPE*/ STRING, \"strParameter2\" );\n"
    "free ( \"spokeWithPrisoners1\" );\n"
    "set ( /*@SET_TYPES*/ \"SET_WALKING\", \"true\" );\n"
    "set ( \"variablename\", \"value\" );\n"
    "camera ( /*@CAMERA_COMMANDS*/ ENABLE );\n"
    "task ( \"kyle1_talk1\" )\n{\n}\n"
    "do ( \"kyle1 line 2\" );\n"
    "wait ( \"kyle1 line 2\" );\n"
    "dowait ( \"kyle1 line 1\" );\n"
    "play ( /*@PLAY_TYPES*/ \"PLAY_ROFF\", \"kor2/roffs/lsend_ceiling01\" );\n";

const char* artHinweis(const Param& p) {
    switch (p.kind) {
        case Param::Kind::String: return "<str>";
        case Param::Kind::Int: return "<int>";
        case Param::Kind::Float: return "<float>";
        case Param::Kind::Vector: return "<vec>";
        case Param::Kind::Range: return "<range>";
        case Param::Kind::TypeSet: return "<enum>";
        default: return "<expr>";
    }
}

std::string beschreibeEditor(const std::string& wann) {
    std::string z = "Vergleich " + wann + ":";
    if (!g_app->editorOpen || g_app->editorCmd == nullptr) { return z + " Editor zu"; }
    const std::vector<Param> f =
        editorFields(*g_app->editorCmd, g_app->db, g_app->editorValues);
    for (std::size_t i = 0; i < f.size(); ++i) {
        const bool ex = istAusdruck(i);
        z += " [" + std::to_string(i) + " " +
             (f[i].kind == Param::Kind::TypeSet ? "Combo" : "Edit") + " '" + wert(i) +
             "' " + (ex ? "<expr>" : artHinweis(f[i])) + "]";
    }
    // Vorbelegung der Helferzeilen je Feld im Ausdrucksmodus.
    for (std::size_t i = 0; i < f.size() && i < g_app->helpGetType.size(); ++i) {
        if (!istAusdruck(i)) { continue; }
        char zahl[64];
        std::snprintf(zahl, sizeof(zahl), "%g,%g", static_cast<double>(g_app->helpRangeLow[i]),
                      static_cast<double>(g_app->helpRangeHigh[i]));
        z += " H" + std::to_string(i) + "(" + g_app->helpGetType[i] + "," +
             g_app->helpGetName[i] + "," + g_app->helpTagType[i] + "," + zahl + ")";
    }
    z += " |";
    for (const Element& e : g_elemente) {
        if (e.label.empty() || e.fenster.find("###editor") == std::string::npos) { continue; }
        const std::string s = sichtbarerText(e.label.c_str());
        z += " " + (s.empty() ? e.label : s);
    }
    return z;
}

// Ein Knoten des Vergleichs: Weg und lesbarer Name ("set#3", "walkOnly/2").
struct VergleichsZiel {
    Path weg;
    std::string name;
};

void sammleZiele(const std::vector<Node>& ns, Path& weg, const std::string& vor,
                 std::vector<VergleichsZiel>& out) {
    for (std::size_t i = 0; i < ns.size(); ++i) {
        const Node& n = ns[i];
        if (n.kind != Node::Kind::Command && n.kind != Node::Kind::Macro) { continue; }
        weg.push_back(i);
        const std::string name = vor + (n.kind == Node::Kind::Macro ? "[" + n.name + "]" : n.name) +
                                 "@" + std::to_string(i);
        out.push_back({weg, name});
        sammleZiele(n.children, weg, name + "/", out);
        weg.pop_back();
    }
}

// Den Editor fuer einen WEG oeffnen - auch fuer Zeilen in Makros und fuer
// Makrokoepfe (dort oeffnet das Original keinen Editor).
Schritt oeffneWeg(const Path& weg, const std::string& name) {
    return {"Oeffne " + name, [=](int b) {
                if (b == 0) {
                    openEditorForNode(weg);
                    return false;
                }
                if (!g_app->editorOpen) {
                    if (b > 10) {
                        diag::detail("Vergleich " + name + " AUF: kein Editor");
                        return true;
                    }
                    return false;
                }
                ImGuiWindow* w = ImGui::FindWindowByName("###editor");
                return w != nullptr && w->Active && !w->Hidden && b >= 3;
            }};
}

std::vector<Schritt> vergleichSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    // BHED_VERGLEICHSDATEI: ein Skript, das das ORIGINAL gespeichert hat
    // (mit allen Makros). Sonst die 27 Befehle von oben.
    const char* datei = std::getenv("BHED_VERGLEICHSDATEI");
    const std::string pfad = datei != nullptr ? datei : "";
    add(tu("Vergleichsskript laden", [pfad] {
        if (pfad.empty()) {
            openScriptFromMemory(kVergleich, "vergleich.txt");
        } else {
            ladeSkriptDatei(pfad);
        }
    }));
    add({"Ziele sammeln", [](int b) {
             if (b < 3) { return false; }
             std::vector<VergleichsZiel> ziele;
             Path weg;
             sammleZiele(g_app->doc.script().nodes, weg, "", ziele);
             diag::detail("Vergleich: " + std::to_string(ziele.size()) + " Zeilen");
             std::vector<Schritt> neu;
             for (const VergleichsZiel& z : ziele) {
                 const std::string name = z.name;
                 neu.push_back(oeffneWeg(z.weg, name));
                 neu.push_back({"Beschreibe " + name, [name](int) {
                     if (!g_app->editorOpen || g_app->editorCmd == nullptr) { return true; }
                     diag::detail(beschreibeEditor(name + " AUF"));
                     // Je Feld mit Knopf: umschalten, beschreiben, zurueck.
                     std::vector<Schritt> knoepfe;
                     const std::vector<Param> f =
                         editorFields(*g_app->editorCmd, g_app->db, g_app->editorValues);
                     for (std::size_t i = 0; i < f.size(); ++i) {
                         if (feldKnopf(*g_app->editorCmd, i) == FeldKnopf::Keiner) { continue; }
                         const std::string k = name + " Feld " + std::to_string(i);
                         knoepfe.push_back(tu("Knopf " + k, [i, n2 = f.size()] {
                             if (g_app->editorIsExpr.size() < n2) {
                                 g_app->editorIsExpr.resize(n2, 0);
                             }
                             g_app->editorIsExpr[i] = 1;
                         }));
                         knoepfe.push_back({"Beschreibe " + k, [k](int) {
                                                diag::detail(beschreibeEditor(k + " KNOPF"));
                                                return true;
                                            }});
                         knoepfe.push_back(
                             tu("Zurueck " + k, [i] { g_app->editorIsExpr[i] = 0; }));
                     }
                     knoepfe.push_back({"Schliessen " + name, [](int b2) {
                         if (b2 == 0) { g_app->editorOpen = false; }
                         return b2 >= 3;
                     }});
                     schritteEinfuegen(std::move(knoepfe));
                     return true;
                 }});
             }
             schritteEinfuegen(std::move(neu));
             return true;
         }});
    return s;
}

// --- Ziehen in LANGEN Listen: die Ansicht bleibt, das Gezogene ist markiert
//
// shank, 27.09.: "Dragging a command scrolls the view up ... The view should
// not scroll and the command(s) I moved should probably be highlighted."
// Nur bei langen Listen - bei kurzen gibt es nichts zu rollen. Und: "du
// musst das auch testen, wenn split view an ist".
//
// Allein aufrufbar mit BHED_EDITORTEST=ziehen.
std::string langesSkript(int basis = 1000) {
    std::string s = "//Generated by BehavEd\n";
    char z[64];
    for (int k = 1; k <= 150; ++k) {
        std::snprintf(z, sizeof(z), "wait ( %d.000 );\n", basis + k);
        s += z;
    }
    return s;
}

struct ZiehProbe {
    std::string fenster;          // ImGui-Name des Baumfensters
    float scrollVorher = 0.0F;
    float abweichung = 0.0F;      // groesste Abweichung nach dem Ablegen
    float nachbarVorher = 0.0F;   // Rollposition des anderen Feldes
    float nachbarAbweichung = 0.0F;
    std::string nachbarFenster;
    std::vector<Kennung> erwartet;
    std::vector<Path> wegeVorher;
    ImVec2 von{}, nach{};
    bool ok = false;
};

ImGuiWindow* baumFenster(const std::string& teil) {
    for (const Element& e : g_elemente) {
        if (e.innen.find(teil) != std::string::npos && e.label == "wait") {
            return ImGui::FindWindowByName(e.innen.c_str());
        }
    }
    return nullptr;
}

// Eine halbe Sekunde nichts tun. Zwei Zuege kurz hintereinander an
// derselben Stelle wertet ImGui als Doppelklick - und der oeffnet den
// Editor, der dann jeden weiteren Klick abfaengt.
Schritt pause(double sekunden) {
    auto bis = std::make_shared<double>(-1.0);
    return {"Pause", [=](int b) {
                if (b == 0) { *bis = ImGui::GetTime() + sekunden; }
                return ImGui::GetTime() >= *bis;
            }};
}

// Das Baumfenster auf einen Anteil seines Rollbereichs stellen (1 = unten).
Schritt rolleBaum(const std::string& teil, float anteil) {
    return {"Rolle " + teil + " auf " + std::to_string(anteil), [=](int b) {
                ImGuiWindow* w = baumFenster(teil);
                if (w == nullptr) {
                    if (b > 20) { meldeFehler("Rolle: Baumfenster " + teil + " nicht gefunden"); return true; }
                    return false;
                }
                if (b < 3) { ImGui::SetScrollY(w, w->ScrollMax.y * anteil); return false; }
                return b >= 12;
            }};
}

// Eine sichtbare "wait"-Zeile auf eine andere ziehen - gezaehlt von unten
// im sichtbaren Bereich. Danach 40 Bilder lang die Rollposition messen.
Schritt zieheSichtbar(std::shared_ptr<ZiehProbe> p, const std::string& teil,
                      int vonAbUnten, int nachAbUnten, const std::string& nachbar = "") {
    const std::string was = "Ziehe in " + teil + " Zeile -" + std::to_string(vonAbUnten) +
                            " auf -" + std::to_string(nachAbUnten);
    return {was, [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                if (b == 0) {
                    std::vector<const Element*> sicht;
                    for (const Element& e : g_elemente) {
                        if (e.label != "wait" || e.innen.find(teil) == std::string::npos) { continue; }
                        if (e.rect.Min.y >= e.clip.Min.y && e.rect.Max.y <= e.clip.Max.y) {
                            sicht.push_back(&e);
                        }
                    }
                    std::sort(sicht.begin(), sicht.end(), [](const Element* a, const Element* c) {
                        return a->rect.Min.y < c->rect.Min.y;
                    });
                    const int n = static_cast<int>(sicht.size());
                    if (n <= std::max(vonAbUnten, nachAbUnten)) {
                        meldeFehler(was + ": zu wenig sichtbare Zeilen (" + std::to_string(n) + ")");
                        p->ok = false;
                        return true;
                    }
                    const Element* q = sicht[static_cast<std::size_t>(n - 1 - vonAbUnten)];
                    const Element* z = sicht[static_cast<std::size_t>(n - 1 - nachAbUnten)];
                    p->fenster = q->innen;
                    p->von = ImVec2{q->rect.Min.x + 10.0F, q->rect.GetCenter().y};
                    p->nach = ImVec2{z->rect.Min.x + 10.0F, z->rect.GetCenter().y};
                    ImGuiWindow* w = ImGui::FindWindowByName(p->fenster.c_str());
                    p->scrollVorher = (w != nullptr) ? w->Scroll.y : 0.0F;
                    p->abweichung = 0.0F;
                    p->nachbarAbweichung = 0.0F;
                    p->nachbarFenster.clear();
                    if (!nachbar.empty()) {
                        if (ImGuiWindow* nw = baumFenster(nachbar)) {
                            p->nachbarFenster = nw->Name;
                            p->nachbarVorher = nw->Scroll.y;
                        }
                    }
                    p->ok = true;
                    setzeMaus(p->von);
                    return false;
                }
                if (!p->ok) { return true; }
                if (b == 1) { io.AddMouseButtonEvent(0, true); return false; }
                if (b >= 2 && b <= 6) {
                    setzeMaus(ImVec2{p->von.x + 3.0F * static_cast<float>(b - 1),
                                     p->von.y - 2.0F * static_cast<float>(b - 1)});
                    return false;
                }
                if (b == 7) {
                    // Was gezogen wird: die Mehrfachauswahl oder die eine Zeile.
                    const std::vector<Path> m =
                        g_app->selection.size() > 1 ? g_app->selection
                                                    : std::vector<Path>{g_app->dragFrom};
                    p->erwartet = kennungenVon(m);
                    p->wegeVorher = m;
                }
                if (b >= 7 && b <= 12) {
                    const float f = static_cast<float>(b - 6) / 6.0F;
                    setzeMaus(ImVec2{p->von.x + (p->nach.x - p->von.x) * f,
                                     p->von.y + (p->nach.y - p->von.y) * f});
                    return false;
                }
                if (b == 13) {
                    if (!GImGui->DragDropActive) {
                        meldeFehler(was + ": kein Zug aktiv vor dem Loslassen (" + wasImGuiSieht() + ")");
                    }
                    io.AddMouseButtonEvent(0, false);
                    return false;
                }
                // Jedes Bild nach dem Loslassen: steht die Ansicht?
                if (ImGuiWindow* w = ImGui::FindWindowByName(p->fenster.c_str())) {
                    p->abweichung = std::max(p->abweichung, std::fabs(w->Scroll.y - p->scrollVorher));
                }
                if (!p->nachbarFenster.empty()) {
                    if (ImGuiWindow* nw = ImGui::FindWindowByName(p->nachbarFenster.c_str())) {
                        p->nachbarAbweichung =
                            std::max(p->nachbarAbweichung, std::fabs(nw->Scroll.y - p->nachbarVorher));
                    }
                }
                return b >= 55;
            }};
}

void pruefeZug(std::vector<Schritt>& s, std::shared_ptr<ZiehProbe> p, const std::string& wo,
               std::size_t anzahl, bool nachbar = false) {
    s.push_back(pruefe(wo + ": Ansicht springt nicht (Rollposition bleibt)", [=] {
        char z[160];
        std::snprintf(z, sizeof(z), "Knopftest: %s Rollposition vorher %.0f, groesste Abweichung %.1f",
                      wo.c_str(), static_cast<double>(p->scrollVorher), static_cast<double>(p->abweichung));
        diag::detail(z);
        return p->ok && p->scrollVorher > 1.0F && p->abweichung < 1.0F;
    }));
    s.push_back(pruefe(wo + ": das Gezogene ist markiert (" + std::to_string(anzahl) + ")", [=] {
        if (p->erwartet.size() != anzahl || g_app->selection.size() != anzahl) {
            diag::detail("Knopftest: erwartet " + std::to_string(p->erwartet.size()) +
                         ", markiert " + std::to_string(g_app->selection.size()));
            return false;
        }
        const std::vector<Kennung> jetzt = kennungenVon(g_app->selection);
        for (const Kennung k : p->erwartet) {
            if (std::find(jetzt.begin(), jetzt.end(), k) == jetzt.end()) { return false; }
        }
        return true;
    }));
    s.push_back(pruefe(wo + ": und es ist wirklich verschoben", [=] {
        return !g_app->selection.empty() && !p->wegeVorher.empty() &&
               g_app->selection.front() != p->wegeVorher.front();
    }));
    if (nachbar) {
        s.push_back(pruefe(wo + ": das andere Feld springt auch nicht", [=] {
            char z[160];
            std::snprintf(z, sizeof(z), "Knopftest: Nachbarfeld %s Abweichung %.1f",
                          p->nachbarFenster.c_str(), static_cast<double>(p->nachbarAbweichung));
            diag::detail(z);
            return !p->nachbarFenster.empty() && p->nachbarAbweichung < 1.0F;
        }));
    }
}

// Die drei untersten Zeilen des Skripts markieren (bei ganz unten gerollt
// sichtbar).
Schritt markiereUntereDrei() {
    return tu("Die drei untersten Zeilen markieren", [] {
        const std::size_t n = g_app->doc.script().nodes.size();
        g_app->selection = {Path{n - 3}, Path{n - 2}, Path{n - 1}};
        g_app->selectedPath = Path{n - 2};
        rebuildTree();
    });
}

std::vector<Schritt> ziehSchritte() {
    std::vector<Schritt> s;
    auto p = std::make_shared<ZiehProbe>();
    auto tab = std::make_shared<int>(-1);
    // Zwei verschiedene lange Skripte: das Nebenfeld zeigt das zweite. Ein
    // Reiter in zwei Feldern ist kein vorgesehener Zustand (das Programm
    // meldet ihn selbst als Fehler).
    s.push_back(tu("Zweites langes Skript oeffnen (fuer das Nebenfeld)", [=] {
        openScriptFromMemory(langesSkript(5000).c_str(), "lang_b.txt");
        *tab = g_app->activeTab;
    }));
    s.push_back(tu("Langes Skript oeffnen (150 Zeilen)", [=] {
        openScriptFromMemory(langesSkript().c_str(), "lang.txt");
        g_app->selection.clear();
        g_app->selectedPath.clear();
    }));
    s.push_back(pause(0.4));

    // ---- Eine Ansicht ------------------------------------------------
    s.push_back(rolleBaum("/pane0_", 1.0F));
    s.push_back(pause(0.5));
    s.push_back(zieheSichtbar(p, "/pane0_", 1, 5));
    pruefeZug(s, p, "Ganz unten, eine Zeile", 1);
    s.push_back(rolleBaum("/pane0_", 0.5F));
    s.push_back(pause(0.5));
    s.push_back(zieheSichtbar(p, "/pane0_", 2, 8));
    pruefeZug(s, p, "Mitte, eine Zeile", 1);
    s.push_back(rolleBaum("/pane0_", 1.0F));
    s.push_back(markiereUntereDrei());
    s.push_back(pause(0.5));
    s.push_back(zieheSichtbar(p, "/pane0_", 1, 7));
    pruefeZug(s, p, "Ganz unten, drei Zeilen", 3);
    s.push_back(klick(tr(Str::EditUndo)));
    s.push_back(klick(tr(Str::EditUndo)));
    s.push_back(klick(tr(Str::EditUndo)));

    // ---- Geteilte Ansicht: beide Felder zeigen das lange Skript ---------
    splitMenue(s, 2);
    s.push_back(tu("Nebenfeld zeigt das zweite lange Skript, Teiler in die Mitte", [=] {
        g_app->splitPanes[1].tab = *tab;
        // Im Gesamtlauf hat der Split-Test den Teiler verschoben; ein zu
        // schmales Feld zeigt nur Symbole ohne Namen.
        diag::detail("Knopftest: Teiler stand bei " + std::to_string(g_app->splitFracX));
        g_app->splitFracX = 0.5F;
        g_app->splitFracY = 0.5F;
    }));
    s.push_back(pause(0.4));
    s.push_back(rolleBaum("/pane0_", 1.0F));
    s.push_back(rolleBaum("/pane1_", 0.6F));
    s.push_back(pause(0.5));
    s.push_back(zieheSichtbar(p, "/pane0_", 1, 5, "/pane1_"));
    pruefeZug(s, p, "Split, Feld 1, eine Zeile", 1, true);
    s.push_back(markiereUntereDrei());
    s.push_back(pause(0.5));
    s.push_back(zieheSichtbar(p, "/pane0_", 1, 6, "/pane1_"));
    pruefeZug(s, p, "Split, Feld 1, drei Zeilen", 3, true);
    // Fokus ins zweite Feld: dort ziehen, das erste darf nicht springen.
    s.push_back(tu("Fokus ins zweite Feld", [] { g_app->focusRequest = 1; }));
    s.push_back(pause(0.4));
    s.push_back(rolleBaum("/pane1_", 1.0F));
    s.push_back(pause(0.5));
    s.push_back(zieheSichtbar(p, "/pane1_", 1, 5, "/pane0_"));
    pruefeZug(s, p, "Split, Feld 2, eine Zeile", 1, true);
    s.push_back(tu("Fokus zurueck ins erste Feld", [] { g_app->focusRequest = 0; }));
    s.push_back(pause(0.4));
    splitMenue(s, 1);
    s.push_back(pause(0.4));
    return s;
}

// --- Breit: mehrere Reiter, Bloecke, Klick-dann-Ziehen, Reiter schliessen --
//
// shank, 27.09.: "beim allersten mal ziehen springt er einmal danach ist es
// gut - du musst testen an mehreren Stellen und in mehreren Tabs ob es
// irgendwann passiert". Ursache war ein liegengebliebenes "zur Auswahl
// rollen" aus dem Reiterwechsel: der erste KLICK auf eine Zeile zog sie in
// die Mitte. Der erste Ziehtest hat das nicht gesehen, weil er ohne Klick
// zog. Hier wird deshalb wie von Hand gearbeitet: anklicken, kurz warten,
// ziehen - und ab dem Mausdruck jedes Bild gemessen.
std::string realesSkript(int basis) {
    std::string s = "//Generated by BehavEd\n";
    char z[160];
    auto waits = [&](int von, int bis) {
        for (int k = von; k <= bis; ++k) {
            std::snprintf(z, sizeof(z), "wait ( %d.000 );\n", basis + k);
            s += z;
        }
    };
    waits(1, 30);
    s += "affect ( \"blockA\", /*@AFFECT_TYPE*/ FLUSH )\n{\n";
    for (int j = 1; j <= 20; ++j) {
        std::snprintf(z, sizeof(z), "\tset ( /*@SET_TYPES*/ \"SET_PARM1\", \"a%d\" );\n", j);
        s += z;
    }
    s += "}\n\n";
    waits(31, 60);
    s += "affect ( \"blockB\", /*@AFFECT_TYPE*/ FLUSH )\n{\n";
    for (int j = 1; j <= 20; ++j) {
        std::snprintf(z, sizeof(z),
                      "\tcamera ( /*@CAMERA_COMMANDS*/ MOVE, < %d.000 0.000 0.000 >, 0 );\n", j);
        s += z;
    }
    s += "}\n\n";
    waits(61, 90);
    s += "//$\"walkOnly\"@5\n"
         "set ( /*!*/ \"SET_BEHAVIOR_STATE\", /*!*/ \"BS_DEFAULT\" );\n"
         "set ( /*!*/ \"SET_WALKING\", /*!*/ \"true\" );\n"
         "set ( /*!*/ \"SET_RUNNING\", /*!*/ \"false\" );\n"
         "set ( /*!*/ \"SET_IGNOREALERTS\", /*!*/ \"true\" );\n"
         "set ( /*!*/ \"SET_LOOK_FOR_ENEMIES\", /*!*/ \"false\" );\n";
    waits(91, 120);
    return s;
}

// Eine sichtbare Zeile: Name und die wievielte von oben (>= 0) bzw. von
// unten (-1 = die letzte).
struct Wahl {
    std::string name;
    int nte = 0;
};

const Element* sichtbareZeile(const std::string& teil, const Wahl& w) {
    std::vector<const Element*> v;
    for (const Element& e : g_elemente) {
        if (e.label != w.name || e.innen.find(teil) == std::string::npos) { continue; }
        if (e.rect.Min.y >= e.clip.Min.y && e.rect.Max.y <= e.clip.Max.y) { v.push_back(&e); }
    }
    std::sort(v.begin(), v.end(),
              [](const Element* a, const Element* b) { return a->rect.Min.y < b->rect.Min.y; });
    const int n = static_cast<int>(v.size());
    const int i = w.nte >= 0 ? w.nte : n + w.nte;
    return (i >= 0 && i < n) ? v[static_cast<std::size_t>(i)] : nullptr;
}

// Die `nte` Zeile dieses Namens in g_app->rows auf `anteil` der Fensterhoehe
// rollen (0 = oben, 0.5 = Mitte).
Schritt rolleZuZeile(const std::string& teil, const std::string& name, int nte, float anteil) {
    return {"Rolle " + teil + " zu " + name + " #" + std::to_string(nte), [=](int b) {
                ImGuiWindow* w = baumFenster(teil);
                if (w == nullptr) {
                    if (b > 30) { meldeFehler("Rolle: Baumfenster " + teil + " fehlt"); return true; }
                    return false;
                }
                if (b < 3) {
                    int gez = 0;
                    int zeile = -1;
                    for (std::size_t i = 0; i < g_app->rows.size(); ++i) {
                        if (g_app->rows[i].name == name && gez++ == nte) { zeile = static_cast<int>(i); break; }
                    }
                    // Zeilenschritt aus zwei sichtbaren Zeilen
                    float schritt = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
                    const float y = static_cast<float>(zeile) * schritt -
                                    w->InnerRect.GetHeight() * anteil;
                    ImGui::SetScrollY(w, std::clamp(y, 0.0F, w->ScrollMax.y));
                    return false;
                }
                return b >= 12;
            }};
}

struct Handlung {
    std::string fenster;
    float start = 0.0F;
    float abweichung = 0.0F;
    std::vector<Kennung> erwartet;
    std::vector<Path> wegeVorher;
    ImVec2 von{}, nach{};
    int phase = 0;
    int phaseBild = 0;
    double warteBis = 0.0;
    bool ok = false;
    // Die Action-Leiste: Breite des Delete-Knopfs und ob die Spalte eine
    // Rollleiste hat. shank, 27.09.: "manchmal beim verschieben springt die
    // action leiste und wird groesser".
    float leisteMin = 1e9F, leisteMax = 0.0F;
    int rollleisteAn = 0, rollleisteAus = 0;
    // Der UNTERSTE Knopf: rutscht etwas in der Leiste nach unten (eine
    // eingeschobene Zeile), sieht man es nur hier - Delete steht oben.
    float exitYMin = 1e9F, exitYMax = -1e9F;
    // Verliert das Fenster waehrend der Handlung den Fokus (Klick in ein
    // anderes Programm), setzt ImGui die Maustasten zurueck und der Zug
    // bricht ab - von aussen, nicht durch behaved. Dann wird wiederholt.
    bool fokusWeg = false;
    int wiederholt = 0;
};

void messeLeiste(Handlung& h) {
    if (const Element* e = finde(tr(Str::ActDelete), "buttons", 0, false)) {
        h.leisteMin = std::min(h.leisteMin, e->rect.GetWidth());
        h.leisteMax = std::max(h.leisteMax, e->rect.GetWidth());
        if (ImGuiWindow* w = ImGui::FindWindowByName(e->innen.c_str())) {
            (w->ScrollbarY ? h.rollleisteAn : h.rollleisteAus)++;
        }
    }
    if (const Element* e = finde(tr(Str::AppExit), "buttons", 0, false)) {
        h.exitYMin = std::min(h.exitYMin, e->rect.Min.y);
        h.exitYMax = std::max(h.exitYMax, e->rect.Min.y);
    }
}

// Anklicken (optional), warten, ziehen (optional). Gemessen wird ab dem
// ERSTEN Mausdruck bis 40 Bilder nach dem Loslassen.
Schritt handlung(std::shared_ptr<Handlung> h, const std::string& teil, Wahl quelle,
                 Wahl ziel, bool klickZuerst, bool ziehen, const std::string& was) {
    return {was, [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                auto messen = [&] {
                    if (ImGuiWindow* w = ImGui::FindWindowByName(h->fenster.c_str())) {
                        h->abweichung = std::max(h->abweichung, std::fabs(w->Scroll.y - h->start));
                    }
                    messeLeiste(*h);
                };
                if (b == 0) {
                    const Element* q = sichtbareZeile(teil, quelle);
                    const Element* z = ziehen ? sichtbareZeile(teil, ziel) : q;
                    if (q == nullptr || z == nullptr) {
                        meldeFehler(was + ": Zeile nicht sichtbar (" + quelle.name + "/" + ziel.name + ")");
                        h->ok = false;
                        return true;
                    }
                    h->fenster = q->innen;
                    h->von = ImVec2{q->rect.Min.x + 12.0F, q->rect.GetCenter().y};
                    h->nach = ImVec2{z->rect.Min.x + 12.0F, z->rect.GetCenter().y};
                    ImGuiWindow* w = ImGui::FindWindowByName(h->fenster.c_str());
                    h->start = (w != nullptr) ? w->Scroll.y : 0.0F;
                    h->abweichung = 0.0F;
                    h->leisteMin = 1e9F;
                    h->leisteMax = 0.0F;
                    h->rollleisteAn = 0;
                    h->rollleisteAus = 0;
                    h->exitYMin = 1e9F;
                    h->exitYMax = -1e9F;
                    h->fokusWeg = false;
                    messeLeiste(*h);
                    h->erwartet.clear();
                    h->wegeVorher.clear();
                    h->phase = klickZuerst ? 0 : 2;
                    h->phaseBild = 0;
                    h->ok = true;
                    setzeMaus(h->von);
                    return false;
                }
                if (!h->ok) { return true; }
                messen();
                if (ImGui::GetIO().AppFocusLost) { h->fokusWeg = true; }
                const int pb = h->phaseBild++;
                switch (h->phase) {
                    case 0:   // Klick
                        if (pb == 0) { io.AddMouseButtonEvent(0, true); return false; }
                        if (pb == 1) { io.AddMouseButtonEvent(0, false); return false; }
                        h->phase = 1;
                        h->phaseBild = 0;
                        h->warteBis = ImGui::GetTime() + 0.45;   // kein Doppelklick
                        return false;
                    case 1:   // warten, dabei messen
                        if (ImGui::GetTime() < h->warteBis) { return false; }
                        if (!ziehen) { h->phase = 4; h->phaseBild = 0; return false; }
                        h->phase = 2;
                        h->phaseBild = 0;
                        setzeMaus(h->von);
                        return false;
                    case 2:   // Ziehen
                        if (pb == 0) { setzeMaus(h->von); return false; }
                        if (pb == 1) { io.AddMouseButtonEvent(0, true); return false; }
                        if (pb == 2 || pb == 4) {
                            char z[260];
                            std::snprintf(z, sizeof(z),
                                          "Knopftest: Druck Bild %d - Maus ImGui %.0f/%.0f, soll %.0f/%.0f, "
                                          "Taste %d, %s",
                                          pb, static_cast<double>(io.MousePos.x),
                                          static_cast<double>(io.MousePos.y),
                                          static_cast<double>(h->von.x), static_cast<double>(h->von.y),
                                          io.MouseDown[0] ? 1 : 0, wasImGuiSieht().c_str());
                            diag::detail(z);
                        }
                        if (pb >= 2 && pb <= 6) {
                            setzeMaus(ImVec2{h->von.x + 3.0F * static_cast<float>(pb - 1),
                                             h->von.y + ((h->nach.y > h->von.y) ? 2.0F : -2.0F) *
                                                            static_cast<float>(pb - 1)});
                            return false;
                        }
                        if (pb == 7) {
                            const std::vector<Path> m =
                                g_app->selection.size() > 1 ? g_app->selection
                                                            : std::vector<Path>{g_app->dragFrom};
                            h->erwartet = kennungenVon(m);
                            h->wegeVorher = m;
                        }
                        if (pb >= 7 && pb <= 14) {
                            const float f = static_cast<float>(pb - 6) / 8.0F;
                            setzeMaus(ImVec2{h->von.x + (h->nach.x - h->von.x) * f,
                                             h->von.y + (h->nach.y - h->von.y) * f});
                            return false;
                        }
                        if (!GImGui->DragDropActive) {
                            meldeFehler(was + ": kein Zug aktiv (" + wasImGuiSieht() + ")");
                        }
                        io.AddMouseButtonEvent(0, false);
                        h->phase = 4;
                        h->phaseBild = 0;
                        return false;
                    default:  // nachmessen
                        if (pb < 40) { return false; }
                        if (h->fokusWeg && h->wiederholt < 3) {
                            // Ein abgebrochener Zug hat nichts verschoben -
                            // dieselbe Handlung einfach noch einmal.
                            ++h->wiederholt;
                            diag::detail("Knopftest: " + was +
                                         " - Fenster verlor den Fokus, Handlung wird wiederholt");
                            h->ok = false;
                            schritteEinfuegen({pause(0.5),
                                               handlung(h, teil, quelle, ziel, klickZuerst, ziehen, was)});
                        } else {
                            h->wiederholt = 0;
                        }
                        return true;
                }
            }};
}

void pruefeHandlung(std::vector<Schritt>& s, std::shared_ptr<Handlung> h, const std::string& was,
                    bool verschoben) {
    s.push_back(pruefe(was + ": Ansicht springt nicht", [=] {
        char z[200];
        std::snprintf(z, sizeof(z), "Knopftest: %s - Rollposition %.0f, groesste Abweichung %.1f",
                      was.c_str(), static_cast<double>(h->start), static_cast<double>(h->abweichung));
        diag::detail(z);
        return h->ok && h->abweichung < 1.0F;
    }));
    s.push_back(pruefe(was + ": Action-Leiste bleibt gleich breit", [=] {
        char z[200];
        std::snprintf(z, sizeof(z),
                      "Knopftest: %s - Delete-Knopf %.0f..%.0f breit, Exit bei y %.0f..%.0f, "
                      "Rollleiste an %d / aus %d Bilder",
                      was.c_str(), static_cast<double>(h->leisteMin),
                      static_cast<double>(h->leisteMax), static_cast<double>(h->exitYMin),
                      static_cast<double>(h->exitYMax), h->rollleisteAn, h->rollleisteAus);
        diag::detail(z);
        return h->leisteMax > 0.0F && h->leisteMax - h->leisteMin < 0.5F &&
               h->exitYMax - h->exitYMin < 0.5F &&
               (h->rollleisteAn == 0 || h->rollleisteAus == 0);
    }));
    if (verschoben) {
        s.push_back(pruefe(was + ": das Gezogene ist markiert und verschoben", [=] {
            if (h->erwartet.empty() || g_app->selection.size() != h->erwartet.size()) { return false; }
            const std::vector<Kennung> jetzt = kennungenVon(g_app->selection);
            for (const Kennung k : h->erwartet) {
                if (std::find(jetzt.begin(), jetzt.end(), k) == jetzt.end()) { return false; }
            }
            return g_app->selection.front() != h->wegeVorher.front();
        }));
    }
}

// Rollposition des Reiters merken / nach dem Wechsel pruefen.
struct ReiterRolle {
    int tab = -1;
    int uid = -1;
    float scroll = 0.0F;
};

Schritt merkeRolle(std::shared_ptr<ReiterRolle> r, const std::string& teil) {
    return tu("Rollposition merken", [=] {
        r->tab = g_app->activeTab;
        r->uid = tabKennung(g_app->activeTab);
        ImGuiWindow* w = baumFenster(teil);
        r->scroll = (w != nullptr) ? w->Scroll.y : -1.0F;
        diag::detail("Knopftest: Reiter " + std::to_string(r->tab) + " (uid " +
                     std::to_string(r->uid) + ") steht bei " + std::to_string(r->scroll));
    });
}

// Zum Reiter mit dieser festen Kennung wechseln und pruefen, dass er seine
// eigene Rollposition zurueckbekommt und 30 Bilder lang behaelt.
void wechsleUndPruefe(std::vector<Schritt>& s, std::shared_ptr<ReiterRolle> r,
                      const std::string& teil, const std::string& was) {
    auto maxAbw = std::make_shared<float>(0.0F);
    s.push_back(tu("Wechsel zu Reiter uid " + was, [=] {
        for (int i = 0; i < static_cast<int>(g_app->tabs.size()); ++i) {
            if (tabKennung(i) == r->uid) { g_app->tabRequest = i; }
        }
    }));
    s.push_back({"Rollposition nach dem Wechsel messen", [=](int b) {
                     if (b < 3) { *maxAbw = 0.0F; return false; }
                     if (ImGuiWindow* w = baumFenster(teil)) {
                         *maxAbw = std::max(*maxAbw, std::fabs(w->Scroll.y - r->scroll));
                     }
                     return b >= 33;
                 }});
    s.push_back(pruefe(was + ": Reiter kommt mit SEINER Rollposition zurueck und bleibt dort", [=] {
        char z[160];
        std::snprintf(z, sizeof(z), "Knopftest: %s erwartet %.0f, groesste Abweichung %.1f",
                      was.c_str(), static_cast<double>(r->scroll), static_cast<double>(*maxAbw));
        diag::detail(z);
        return tabKennung(g_app->activeTab) == r->uid && *maxAbw < 1.0F;
    }));
}

// Der Satz Handlungen fuer einen Reiter in einem Feld.
void handlungenFuerReiter(std::vector<Schritt>& s, const std::string& teil, const std::string& wer) {
    auto h = std::make_shared<Handlung>();
    auto alles = [&](Wahl q, Wahl z, bool klick, bool ziehen, const std::string& was, bool verschoben) {
        s.push_back(pause(0.5));
        s.push_back(handlung(h, teil, q, z, klick, ziehen, wer + ": " + was));
        pruefeHandlung(s, h, wer + ": " + was, verschoben);
    };
    // Ganz unten
    s.push_back(rolleBaum(teil, 1.0F));
    alles({"wait", -1}, {"wait", -1}, true, false, "unten nur anklicken", false);
    alles({"wait", -2}, {"wait", -6}, true, true, "unten anklicken, dann nach oben ziehen", true);
    alles({"wait", -9}, {"wait", -3}, false, true, "unten ohne Klick nach unten ziehen", true);
    // Drei markierte Zeilen: dabei erschien frueher "3 selected" MITTEN in
    // der Action-Leiste und schob alles darunter eine Zeile tiefer.
    s.push_back(markiereUntereDrei());
    alles({"wait", -2}, {"wait", -8}, false, true, "unten drei markierte Zeilen ziehen", true);
    // Mitte, Block A sichtbar
    s.push_back(rolleZuZeile(teil, "affect", 0, 0.3F));
    alles({"set", 2}, {"wait", 0}, true, true, "Kind aus Block A herausziehen", true);
    alles({"wait", -1}, {"affect", 0}, true, true, "Zeile in Block A hineinziehen", true);
    alles({"set", -1}, {"set", 0}, true, true, "innerhalb Block A nach oben", true);
    alles({"set", 1}, {"set", -2}, false, true, "innerhalb Block A nach unten", true);
    // Oben
    s.push_back(rolleBaum(teil, 0.0F));
    alles({"wait", 3}, {"wait", 0}, true, true, "ganz oben anklicken und ziehen", true);
    // Block B, weiter unten, Kamerazeilen
    s.push_back(rolleZuZeile(teil, "affect", 1, 0.5F));
    alles({"camera", 4}, {"camera", 0}, true, true, "Kamerazeile in Block B nach oben", true);
    alles({"camera", 1}, {"wait", -1}, true, true, "Kamerazeile aus Block B nach unten", true);
}

std::vector<Schritt> ziehBreitSchritte() {
    std::vector<Schritt> s;
    auto rA = std::make_shared<ReiterRolle>();
    auto rB = std::make_shared<ReiterRolle>();
    auto rC = std::make_shared<ReiterRolle>();
    s.push_back(tu("Drei lange Skripte mit Bloecken oeffnen", [] {
        openScriptFromMemory(realesSkript(1000).c_str(), "reiter_a.txt");
        g_app->expanded.openAll();
        rebuildTree();
    }));
    s.push_back(pause(0.4));
    handlungenFuerReiter(s, "/pane0_", "Reiter A");
    s.push_back(rolleBaum("/pane0_", 0.2F));
    s.push_back(merkeRolle(rA, "/pane0_"));

    s.push_back(tu("Reiter B oeffnen", [] {
        openScriptFromMemory(realesSkript(2000).c_str(), "reiter_b.txt");
        g_app->expanded.openAll();
        rebuildTree();
    }));
    s.push_back(pause(0.4));
    handlungenFuerReiter(s, "/pane0_", "Reiter B");
    s.push_back(rolleBaum("/pane0_", 0.55F));
    s.push_back(merkeRolle(rB, "/pane0_"));

    s.push_back(tu("Reiter C oeffnen", [] {
        openScriptFromMemory(realesSkript(3000).c_str(), "reiter_c.txt");
        g_app->expanded.openAll();
        rebuildTree();
    }));
    s.push_back(pause(0.4));
    handlungenFuerReiter(s, "/pane0_", "Reiter C");
    s.push_back(rolleBaum("/pane0_", 0.9F));
    s.push_back(merkeRolle(rC, "/pane0_"));

    // Hin und her: jeder kommt mit seiner Position zurueck, und der erste
    // Klick danach springt nicht.
    wechsleUndPruefe(s, rA, "/pane0_", "A");
    {
        auto h = std::make_shared<Handlung>();
        s.push_back(pause(0.5));
        s.push_back(handlung(h, "/pane0_", {"wait", 5}, {"wait", 5}, true, false,
                             "Reiter A nach dem Wechsel: erster Klick"));
        pruefeHandlung(s, h, "Reiter A nach dem Wechsel: erster Klick", false);
        s.push_back(pause(0.5));
        s.push_back(handlung(h, "/pane0_", {"wait", 8}, {"wait", 2}, true, true,
                             "Reiter A nach dem Wechsel: erster Zug"));
        pruefeHandlung(s, h, "Reiter A nach dem Wechsel: erster Zug", true);
    }
    s.push_back(merkeRolle(rA, "/pane0_"));
    wechsleUndPruefe(s, rC, "/pane0_", "C");
    wechsleUndPruefe(s, rB, "/pane0_", "B");
    wechsleUndPruefe(s, rA, "/pane0_", "A (zweites Mal)");

    // Reiter B schliessen: C rueckt eine Nummer auf und darf B's Position
    // NICHT erben.
    s.push_back(tu("Reiter B schliessen", [=] {
        for (int i = 0; i < static_cast<int>(g_app->tabs.size()); ++i) {
            if (tabKennung(i) == rB->uid) { reiterSchliessen(i); break; }
        }
    }));
    s.push_back(antworteNein());
    s.push_back(pause(0.3));
    wechsleUndPruefe(s, rC, "/pane0_", "C nach dem Schliessen von B");
    {
        auto h = std::make_shared<Handlung>();
        s.push_back(pause(0.5));
        s.push_back(handlung(h, "/pane0_", {"wait", 4}, {"wait", 1}, true, true,
                             "Reiter C nach dem Schliessen: Klick und Zug"));
        pruefeHandlung(s, h, "Reiter C nach dem Schliessen: Klick und Zug", true);
    }

    // Geteilte Ansicht: A links, C rechts, in beiden Feldern arbeiten.
    // Erst auf A wechseln, DANN teilen: die Aufteilung gehoert dem Reiter,
    // in dem sie eingeschaltet wird ("Arbeitsbereich") - ein Wechsel auf
    // einen anderen Reiter danach verlaesst sie.
    s.push_back(tu("Zu Reiter A", [=] {
        for (int i = 0; i < static_cast<int>(g_app->tabs.size()); ++i) {
            if (tabKennung(i) == rA->uid) { g_app->tabRequest = i; }
        }
    }));
    s.push_back(pause(0.3));
    splitMenue(s, 2);
    s.push_back(tu("Feld 2 zeigt C, Teiler Mitte", [=] {
        g_app->splitFracX = 0.5F;
        for (int i = 0; i < static_cast<int>(g_app->tabs.size()); ++i) {
            if (tabKennung(i) == rC->uid) { g_app->splitPanes[1].tab = i; }
        }
    }));
    s.push_back(pause(0.5));
    handlungenFuerReiter(s, "/pane0_", "Split Feld 1");
    s.push_back(tu("Fokus ins zweite Feld", [] { g_app->focusRequest = 1; }));
    s.push_back(pause(0.5));
    handlungenFuerReiter(s, "/pane1_", "Split Feld 2");
    s.push_back(tu("Fokus zurueck", [] { g_app->focusRequest = 0; }));
    s.push_back(pause(0.3));
    splitMenue(s, 1);
    s.push_back(pause(0.3));
    return s;
}

// --- Lesezeichen und Aenderungsrand (wie Notepad++) ------------------------
//
// Allein aufrufbar mit BHED_EDITORTEST=lesezeichen.
Kennung kennungAn(const Path& p) {
    g_app->doc.vergibKennungen();
    const Node* n = nodeAt(g_app->doc.script(), p);
    return n != nullptr ? n->kennung : 0;
}

// Weg der `n`ten Zeile dieses Namens im Baum (0 = die erste).
Path wegDerZeile(const std::string& name, int n) {
    int z = 0;
    for (const Row& row : g_app->rows) {
        if (row.name == name && z++ == n) { return row.path; }
    }
    return Path{};
}

int markeVon(Kennung k) {
    for (std::size_t i = 0; i < g_app->rows.size() && i < g_app->zeilenMarke.size(); ++i) {
        if (g_app->rows[i].kennung == k) { return g_app->zeilenMarke[i]; }
    }
    return -1;
}

std::string markenText() {
    std::string s;
    for (std::size_t i = 0; i < g_app->zeilenMarke.size(); ++i) {
        if (g_app->zeilenMarke[i] != 0) {
            s += std::to_string(i) + ":" + std::to_string(g_app->zeilenMarke[i]) + " ";
        }
    }
    return s.empty() ? "(keine)" : s;
}

// In den Rand links neben der `nte` sichtbaren Zeile dieses Namens klicken.
Schritt klickRand(const std::string& name, int nte) {
    auto ziel = std::make_shared<ImVec2>();
    return {"Klick in den Rand neben " + name + " #" + std::to_string(nte), [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                if (b == 0) {
                    const Element* e = sichtbareZeile("/pane0_", {name, nte});
                    if (e == nullptr) { meldeFehler("Rand: Zeile nicht sichtbar"); return true; }
                    *ziel = ImVec2{g_app->rinneX0 + ImGui::GetFontSize() * 0.4F, e->rect.GetCenter().y};
                    setzeMaus(*ziel);
                    return false;
                }
                if (b == 1) { io.AddMouseButtonEvent(0, true); return false; }
                if (b == 2) { io.AddMouseButtonEvent(0, false); return false; }
                return b >= 5;
            }};
}

// Ein Foto des fertigen Bildes, wenn BHED_FOTOS gesetzt ist (Ordner).
Schritt foto(const std::string& name) {
    return {"Foto " + name, [=](int b) {
                const char* ordner = std::getenv("BHED_FOTOS");
                if (ordner == nullptr) { return true; }
                if (b == 2) { render::fotoAnfordern(std::string(ordner) + "\\" + name + ".bmp"); }
                return b >= 5;
            }};
}

// --- Die Vektorsymbole zur Ansicht: alle Groessen, beide Farbsaetze ---------
std::vector<Schritt> iconSchritte() {
    std::vector<Schritt> s;
    static const char* const namen[] = {
        "I_BRACE", "I_EVENT", "I_MACRO", "I_SOUND", "I_CAMERA", "I_ROTATE", "I_REMOVE",
        "I_SET", "I_MOVE", "I_IF", "I_LOOP", "I_DO", "I_WAIT", "I_DOWAIT", "I_SIGNAL",
        "I_WAITSIGNAL", "I_FLUSH", "I_WAITCLOCK"};
    auto fenster = [](bool hell) {
        ImGui::SetNextWindowPos(ImVec2{0.0F, 0.0F});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, hell ? ImVec4{0.95F, 0.95F, 0.95F, 1.0F}
                                                      : ImVec4{0.12F, 0.12F, 0.12F, 1.0F});
        ImGui::PushStyleColor(ImGuiCol_Text, hell ? ImVec4{0, 0, 0, 1} : ImVec4{1, 1, 1, 1});
        ImGui::Begin("##iconprobe", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float groessen[] = {16.0F, 24.0F, 32.0F, 48.0F, 96.0F};
        float y = 20.0F;
        int n = 0;
        for (const char* name : namen) {
            const float spalte = (n % 2 == 0) ? 20.0F : 1280.0F;
            if (n % 2 == 0 && n > 0) { y += 110.0F; }
            float x = spalte;
            dl->AddText(ImVec2{x, y}, ImGui::GetColorU32(ImGuiCol_Text), name);
            x += 200.0F;
            for (const float g : groessen) {
                (void)vektorIcon(dl, name, ImVec2{x, y + 96.0F - g}, g, false);
                x += g + 16.0F;
            }
            (void)vektorIcon(dl, name, ImVec2{x + 20.0F, y}, 96.0F, true);   // zweiter Satz
            ++n;
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
    };
    for (int hell = 0; hell < 2; ++hell) {
        s.push_back({hell ? "Symbole hell" : "Symbole dunkel", [=](int b) {
                         fenster(hell != 0);
                         if (b == 3) {
                             if (const char* ordner = std::getenv("BHED_FOTOS")) {
                                 render::fotoAnfordern(std::string(ordner) + (hell ? "\\icons_hell.bmp"
                                                                                   : "\\icons_dunkel.bmp"));
                             }
                         }
                         return b >= 8;
                     }});
    }
    return s;
}

std::vector<Schritt> lesezeichenSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    auto datei = std::make_shared<std::string>(platform::executableDirectory() +
                                               "\\selbsttest-lesezeichen.txt");
    auto k3 = std::make_shared<Kennung>(0);
    auto k8 = std::make_shared<Kennung>(0);
    auto kz = std::make_shared<Kennung>(0);
    add(tu("Testdatei schreiben und oeffnen", [=] {
        std::remove((*datei).c_str());
        g_app->settings.lesezeichen.erase(*datei);
        spewDatei(*datei, realesSkript(7000));
        ladeSkriptDatei(*datei);
        g_app->expanded.openAll();
        rebuildTree();
    }));
    add(pause(0.3));
    add(rolleBaum("/pane0_", 0.0F));
    add(pruefe("Nach dem Laden: kein Aenderungsrand", [] {
        diag::detail("Knopftest: Rand nach dem Laden " + markenText());
        for (std::uint8_t m : g_app->zeilenMarke) { if (m != 0) { return false; } }
        return !g_app->zeilenMarke.empty();
    }));

    // ---- Lesezeichen setzen: Strg+F2 und Klick in den Rand --------------
    auto w3 = std::make_shared<Path>();
    auto w8 = std::make_shared<Path>();
    add(tu("Dritte wait-Zeile waehlen", [=] {
        *w3 = wegDerZeile("wait", 2);
        *w8 = wegDerZeile("wait", 8);
        selectByPath(*w3);
        *k3 = kennungAn(*w3);
        *k8 = kennungAn(*w8);
    }));
    add(pause(0.2));
    add(rolleBaum("/pane0_", 0.0F));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_F2, "Strg+F2"));
    add(pruefe("Strg+F2 setzt ein Lesezeichen auf die gewaehlte Zeile",
               [=] { return hatLesezeichen(g_app->activeTab, *k3); }));
    add(klickRand("wait", 8));
    add(pruefe("Klick in den Rand setzt ein Lesezeichen auf diese Zeile",
               [=] { return hatLesezeichen(g_app->activeTab, *k8); }));
    add(pruefe("Der Klick in den Rand waehlt die Zeile NICHT (wie Notepad++)",
               [=] { return g_app->selectedPath == *w3; }));
    add(pruefe("Unveraendertes Dokument: beide Lesezeichen sofort gemerkt", [=] {
        const auto it = g_app->settings.lesezeichen.find(*datei);
        diag::detail("Knopftest: gemerkt \"" +
                     (it == g_app->settings.lesezeichen.end() ? std::string("-") : it->second) + "\"");
        return it != g_app->settings.lesezeichen.end() &&
               std::count(it->second.begin(), it->second.end(), ',') == 1;
    }));

    // ---- F2 / Umschalt+F2 mit Umlauf -----------------------------------
    add(tu("Ganz oben waehlen", [] { selectByPath(Path{0}); }));
    add(tasteMit(ImGuiKey_None, ImGuiKey_F2, "F2"));
    add(pruefe("F2 springt zum ersten Lesezeichen", [=] { return g_app->selectedPath == *w3; }));
    add(tasteMit(ImGuiKey_None, ImGuiKey_F2, "F2"));
    add(pruefe("F2 springt zum naechsten", [=] { return g_app->selectedPath == *w8; }));
    add(tasteMit(ImGuiKey_None, ImGuiKey_F2, "F2"));
    add(pruefe("F2 am Ende beginnt wieder oben", [=] { return g_app->selectedPath == *w3; }));
    add(tasteMit(ImGuiMod_Shift, ImGuiKey_F2, "Umschalt+F2"));
    add(pruefe("Umschalt+F2 am Anfang springt ans Ende", [=] { return g_app->selectedPath == *w8; }));
    add(foto("1_lesezeichen"));
    add(klickRand("wait", 8));
    add(pruefe("Zweiter Klick in den Rand entfernt das Lesezeichen",
               [=] { return !hatLesezeichen(g_app->activeTab, *k8); }));
    add(klickRand("wait", 8));

    // ---- Ueber Schliessen und Oeffnen erhalten -------------------------
    add(tu("Reiter schliessen und Datei wieder oeffnen", [=] {
        reiterSchliessen(g_app->activeTab);
        ladeSkriptDatei(*datei);
        g_app->expanded.openAll();
        rebuildTree();
    }));
    add(pause(0.3));
    add(pruefe("Nach dem Wiederoeffnen stehen die Lesezeichen wieder auf denselben Zeilen", [=] {
        *k3 = kennungAn(*w3);
        *k8 = kennungAn(*w8);
        int zahl = 0;
        const auto it = g_app->lesezeichen.find(tabKennung(g_app->activeTab));
        if (it != g_app->lesezeichen.end()) { zahl = static_cast<int>(it->second.size()); }
        return zahl == 2 && hatLesezeichen(g_app->activeTab, *k3) &&
               hatLesezeichen(g_app->activeTab, *k8);
    }));

    // ---- Aenderungsrand: orange, gruen, blau ---------------------------
    add(rolleBaum("/pane0_", 0.0F));
    add(tu("Zeile 10 waehlen", [=] { selectByPath(Path{10}); *kz = kennungAn(Path{10}); }));
    add(klick(tr(Str::MoveDown)));
    add(pruefe("Nach dem Verschieben: genau eine Zeile orange (geaendert)", [=] {
        diag::detail("Knopftest: Rand nach Move down " + markenText());
        int orange = 0, sonst = 0;
        for (std::uint8_t m : g_app->zeilenMarke) { (m == 1 ? orange : (m != 0 ? sonst : orange)) += (m != 0); }
        return orange == 1 && sonst == 0;
    }));
    add(foto("2_orange_verschoben"));
    add(pruefe("Lesezeichen bleiben beim Bearbeiten an ihren Zeilen",
               [=] { return hatLesezeichen(g_app->activeTab, *k3) && hatLesezeichen(g_app->activeTab, *k8); }));
    add(klick(tr(Str::FileSave)));
    add(pruefe("Nach dem Speichern: dieselbe Zeile gruen (geaendert und gespeichert)", [=] {
        diag::detail("Knopftest: Rand nach Save " + markenText());
        int gruen = 0, sonst = 0;
        for (std::uint8_t m : g_app->zeilenMarke) { if (m == 2) { ++gruen; } else if (m != 0) { ++sonst; } }
        return gruen == 1 && sonst == 0;
    }));
    add(foto("3_gruen_gespeichert"));
    add(klick(tr(Str::EditUndo)));
    add(pruefe("Rueckgaengig nach dem Speichern: blassblau (wieder wie beim Oeffnen)", [=] {
        diag::detail("Knopftest: Rand nach Undo " + markenText());
        int blau = 0, sonst = 0;
        for (std::uint8_t m : g_app->zeilenMarke) { if (m == 3) { ++blau; } else if (m != 0) { ++sonst; } }
        return blau == 1 && sonst == 0;
    }));
    add(tu("Eine Zeile waehlen", [=] { selectByPath(wegDerZeile("wait", 4)); }));
    add(klick(tr(Str::ActClone)));
    add(pruefe("Neu eingefuegte Zeile (Duplicate) ist orange", [=] {
        diag::detail("Knopftest: Rand nach Duplicate " + markenText());
        return std::count(g_app->zeilenMarke.begin(), g_app->zeilenMarke.end(), 1) == 1;
    }));
    add(foto("4_blau_und_orange"));
    add(tu("Aenderungsrand ausschalten (View)", [] {
        g_app->settings.changeHistory = false;
        rebuildTree();
    }));
    add(pruefe("Ausgeschaltet: kein Rand", [] {
        for (std::uint8_t m : g_app->zeilenMarke) { if (m != 0) { return false; } }
        return true;
    }));
    add(tu("Aenderungsrand wieder an", [] {
        g_app->settings.changeHistory = true;
        rebuildTree();
    }));

    // ---- Alle entfernen ueber das Menue --------------------------------
    add(klick(tr(Str::MenuEdit)));
    add(klick(tr(Str::MenuBookmarks), "##Menu"));
    add(klick(tr(Str::ActBookmarkClear), "##Menu"));
    add(pruefe("Edit > Bookmarks > Clear all: keine Lesezeichen, nichts mehr gemerkt", [=] {
        return !hatLesezeichen(g_app->activeTab, *k3) && !hatLesezeichen(g_app->activeTab, *k8) &&
               g_app->settings.lesezeichen.count(*datei) == 0;
    }));
    add(tasteMit(ImGuiKey_None, ImGuiKey_F2, "F2 ohne Lesezeichen"));
    add(klick(tr(Str::EditUndo)));
    add(tu("Aufraeumen: Testdatei weg, Reiter schliessen", [=] {
        g_app->doc.markSaved();
        reiterSchliessen(g_app->activeTab);
        std::remove((*datei).c_str());
        g_app->settings.lesezeichen.erase(*datei);
        auto& rc = g_app->settings.recent;
        rc.erase(std::remove(rc.begin(), rc.end(), *datei), rc.end());
    }));
    add(pause(0.3));
    return s;
}

// --- Maushelfer fuer die Kartenansicht ------------------------------------
// Ein Bildpunkt des GERASTERTEN Bildes (Marker, Schluessel) auf den Schirm.
ImVec2 bildZuSchirm(float sx, float sy) {
    const float w = static_cast<float>(std::max(1, g_app->mapImage.width));
    const float h = static_cast<float>(std::max(1, g_app->mapImage.height));
    return ImVec2{g_app->kartenAnsicht[0] + sx / w * (g_app->kartenAnsicht[2] - g_app->kartenAnsicht[0]),
                  g_app->kartenAnsicht[1] + sy / h * (g_app->kartenAnsicht[3] - g_app->kartenAnsicht[1])};
}
ImVec2 ansichtMitte() {
    return ImVec2{(g_app->kartenAnsicht[0] + g_app->kartenAnsicht[2]) * 0.5F,
                  (g_app->kartenAnsicht[1] + g_app->kartenAnsicht[3]) * 0.5F};
}

// Klick (oder Doppelklick) an eine berechnete Stelle.
Schritt klickAn(const std::string& was, std::function<ImVec2()> ort, int taste = 0, bool doppelt = false) {
    auto z = std::make_shared<ImVec2>();
    return {"Klick " + was, [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                if (b == 0) { *z = ort(); setzeMaus(*z); return false; }
                if (b == 2) { io.AddMouseButtonEvent(taste, true); return false; }
                if (b == 3) { io.AddMouseButtonEvent(taste, false); return false; }
                if (doppelt && b == 4) { io.AddMouseButtonEvent(taste, true); return false; }
                if (doppelt && b == 5) { io.AddMouseButtonEvent(taste, false); return false; }
                return b >= 9;
            }};
}

// Mit gedrueckter Taste von einer Stelle um (dx, dy) ziehen.
Schritt ziehenAn(const std::string& was, std::function<ImVec2()> ort, float dx, float dy, int taste,
                 ImGuiKey mod = ImGuiKey_None) {
    auto z = std::make_shared<ImVec2>();
    return {"Ziehe " + was, [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                if (b == 0) { *z = ort(); setzeMaus(*z); return false; }
                if (b == 1 && mod != ImGuiKey_None) { io.AddKeyEvent(mod, true); return false; }
                if (b == 2) { io.AddMouseButtonEvent(taste, true); return false; }
                if (b >= 3 && b <= 14) {
                    const float f = static_cast<float>(b - 2) / 12.0F;
                    setzeMaus(ImVec2{z->x + dx * f, z->y + dy * f});
                    return false;
                }
                if (b == 15) { io.AddMouseButtonEvent(taste, false); return false; }
                if (b == 16 && mod != ImGuiKey_None) { io.AddKeyEvent(mod, false); return false; }
                return b >= 20;
            }};
}

// Ziehen und danach `halte` Bilder STILL festhalten, erst dann loslassen.
// So faellt auf, wenn ein Griff bei ruhiger Maus weiterlaeuft (die
// Spaltenteiler taten das bis zum 28.09.: jedes Bild kamen die Zellraender
// dazu).
// `spur`: wenn gesetzt, die kleinste und groesste Luecke rechts neben der
// letzten Hauptspalte ueber ALLE Bilder des Zuges (App::spaltenLuecke).
// `schritte`: in wie vielen Bildern die Maus den Weg zuruecklegt (1 = ein
// Ruck, wie eine schnelle echte Maus bei wenigen Bildern je Sekunde).
Schritt ziehenHalten(const std::string& was, std::function<ImVec2()> ort, float dx, float dy, int halte,
                     std::shared_ptr<std::pair<float, float>> spur = nullptr, int schritte = 12) {
    auto z = std::make_shared<ImVec2>();
    return {"Ziehe und halte " + was, [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                if (spur && b >= 3) {
                    spur->first = std::min(spur->first, g_app->spaltenLuecke);
                    spur->second = std::max(spur->second, g_app->spaltenLuecke);
                }
                if (b == 0) {
                    *z = ort();
                    setzeMaus(*z);
                    if (spur) { *spur = {g_app->spaltenLuecke, g_app->spaltenLuecke}; }
                    return false;
                }
                if (b == 2) { io.AddMouseButtonEvent(0, true); return false; }
                if (b >= 3 && b <= 14) {
                    const float f = std::min(1.0F, static_cast<float>(b - 2) / static_cast<float>(std::max(1, schritte)));
                    setzeMaus(ImVec2{z->x + dx * f, z->y + dy * f});
                    return false;
                }
                if (b == 15 + halte) { io.AddMouseButtonEvent(0, false); return false; }
                return b >= 20 + halte;
            }};
}

// Mausrad an einer Stelle.
Schritt radAn(const std::string& was, std::function<ImVec2()> ort, float klicks) {
    return {"Rad " + was, [=](int b) {
                if (b == 0) { setzeMaus(ort()); return false; }
                if (b == 2) { ImGui::GetIO().AddMouseWheelEvent(0.0F, klicks); return false; }
                return b >= 8;
            }};
}

// Eine Taste `bilder` Bilder lang halten, Maus ueber der Ansicht.
Schritt halteTaste(const std::string& was, ImGuiKey k, int bilder) {
    return {"Halte " + was, [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                if (b == 0) { setzeMaus(ansichtMitte()); return false; }
                if (b == 1) { io.AddKeyEvent(k, true); return false; }
                if (b == 1 + bilder) { io.AddKeyEvent(k, false); return false; }
                return b >= bilder + 5;
            }};
}

// Einmal durchzeichnen lassen (mapDirty abgearbeitet), dann pruefen.
Schritt gezeichnet(const std::string& was) {
    return warteBis(was + " - neu gezeichnet", [] { return !g_app->mapDirty; }, 600);
}

// Die freie Kamera auf den ersten MOVE-Schluessel richten.
void kameraVorDenSchluessel() {
    for (const CamSegment& sg : g_app->camTrack.segments) {
        if (sg.kind != CamSegment::Kind::Move) { continue; }
        g_app->cam.pos[0] = sg.to[0] - 240.0F;
        g_app->cam.pos[1] = sg.to[1];
        g_app->cam.pos[2] = sg.to[2] + 80.0F;
        g_app->cam.angles[0] = 18.0F;
        g_app->cam.angles[1] = 0.0F;
        g_app->cam.angles[2] = 0.0F;
        break;
    }
    g_app->mapDirty = true;
}

Path ersterKameraweg(const char* art) {
    for (std::size_t i = 0; i < g_app->doc.script().nodes.size(); ++i) {
        const Node& n = g_app->doc.script().nodes[i];
        if (n.name == "camera" && !n.args.empty() && n.args[0].text == art) { return Path{i}; }
    }
    return Path{};
}

std::size_t diagZeilen() { return diag::lines().size(); }

// --- Karte: jede Funktion (27.09.) ------------------------------------------
//
// shank: "schau bitte jedes feature bei map an ... ein umfangreiches map
// testen nach fehlern". Laeuft nach karteSchritte() (Mission md_am_sith ist
// geladen). Seit rc568 gibt es nur noch den Weg ueber die Grafikkarte.
int ueberlagerungsPunkte() {
    int n = 0;
    const std::vector<std::uint8_t>& px = g_app->gizmoBild.rgba;
    for (std::size_t i = 3; i < px.size(); i += 4) {
        if (px[i] != 0) { ++n; }
    }
    return n;
}

// Ein neues Bild anfordern und zwanzig Bilder warten. Hiess bis rc568
// `zeichenweg(bool)` und schaltete zwischen Grafikkarte und Rasterer um.
Schritt neuZeichnen() {
    return {"Neu zeichnen", [=](int b) {
                if (b == 0) {
                    g_app->gpuFehler.clear();
                    g_app->mapDirty = true;
                }
                return b >= 20;
            }};
}

std::vector<Schritt> kartenBreitSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    add(tu("Freie Ansicht vor dem ersten Kameraschluessel", [=] {
        g_app->throughCamera = false;
        g_app->playing = false;
        g_app->showEntities = true;
        // Die freie Kamera 240 Einheiten hinter und 80 ueber den ersten
        // MOVE-Schluessel, Blick darauf: dort muessen Kameramodell und Bahn
        // gross im Bild stehen.
        for (const CamSegment& sg : g_app->camTrack.segments) {
            if (sg.kind != CamSegment::Kind::Move) { continue; }
            g_app->cam.pos[0] = sg.to[0] - 240.0F;
            g_app->cam.pos[1] = sg.to[1];
            g_app->cam.pos[2] = sg.to[2] + 80.0F;
            g_app->cam.angles[0] = 18.0F;   // leicht nach unten
            g_app->cam.angles[1] = 0.0F;    // nach +x
            g_app->cam.angles[2] = 0.0F;
            break;
        }
        g_app->mapDirty = true;
    }));

    // ---- Ueberlagerung (Entitykreuze, Kamera, Bahnen) --------------------
    add(neuZeichnen());
    add(pruefe("Entitykreuze sind da und anklickbar (markers)", [=] {
        diag::detail("Knopftest: markers " + std::to_string(g_app->markers.size()) +
                     ", Ueberlagerung " + std::to_string(ueberlagerungsPunkte()) + " Punkte, GPU-Aufrufe " +
                     std::to_string(g_app->gpuAufrufe));
        return !g_app->markers.empty();
    }));
    add(pruefe("Die Ueberlagerung hat Inhalt und liegt ueber dem Bild", [] {
        return g_app->gpuAufrufe > 0 && g_app->gizmoZeigen && ueberlagerungsPunkte() > 50;
    }));
    add(foto("karte_gpu_frei"));
    add(tu("Kamera auswaehlen (Bahn und Schluessel zeigen)", [] {
        g_app->cameraSelected = true;
        g_app->mapDirty = true;
    }));
    add(pause(0.3));
    add(pruefe("Ausgewaehlte Kamera zeigt ihre Schluessel (keyMarks)", [=] {
        diag::detail("Knopftest: keyMarks " + std::to_string(g_app->keyMarks.size()));
        return !g_app->keyMarks.empty();
    }));
    add(pruefe("Mit ausgewaehlter Kamera mehr Ueberlagerung (Bahn, Schluessel)", [] {
        diag::detail("Knopftest: GPU-Ueberlagerung mit Kamera " + std::to_string(ueberlagerungsPunkte()));
        return ueberlagerungsPunkte() > 200;
    }));
    add(foto("karte_gpu_kamera"));
    add(tu("Kamera abwaehlen", [] { g_app->cameraSelected = false; g_app->mapDirty = true; }));
    add(pause(0.3));

    // ======== Alle Funktionen, auf dem Weg des Benutzers ======================
    auto logAb = std::make_shared<std::size_t>(0);
    add(tu("Protokollstand merken, Kamera vor den Schluessel", [=] {
        *logAb = diagZeilen();
        kameraVorDenSchluessel();
    }));
    add(gezeichnet("Ausgangsbild"));

    // ---- Seitenleiste: jeder Schalter an/aus, jedes Mal ein neues Bild ------
    struct Schalter { Str text; bool* wert; };
    const std::vector<Schalter> schalter = {
        {Str::MapEntities, &g_app->showEntities}, {Str::MapEffects, &g_app->showEffects},
        {Str::MapGlow, &g_app->showGlow},         {Str::MapActors, &g_app->showActors},
        {Str::MapSky, &g_app->showSky},           {Str::MapNames, &g_app->zeigeNamen},
        {Str::MapDynLights, &g_app->showDynLights}, {Str::MapFog, &g_app->showFog}};
    for (const Schalter& sw : schalter) {
        const std::string name = tr(sw.text);
        bool* wert = sw.wert;
        auto vorher = std::make_shared<bool>(false);
        add(tu("Stand " + name + " merken", [=] { *vorher = *wert; }));
        add(klick(name));
        add(gezeichnet(name + " umgeschaltet"));
        add(pruefe("Seitenleiste: " + name + " schaltet um und zeichnet neu",
                   [=] { return *wert != *vorher && g_app->gpuFehler.empty(); }));
        add(klick(name));
        add(gezeichnet(name + " zurueck"));
        add(pruefe("Seitenleiste: " + name + " zurueck", [=] { return *wert == *vorher; }));
    }
    // Die vier Schattenarten (cg_shadows 0..3): jede zeichnet ohne Fehler und
    // ohne Beanstandung der Debugschicht - Art 2 benutzt Geometrie-Shader und
    // Stencil, Art 3 einen eigenen Vertex-Shader.
    auto d3dVorher = std::make_shared<std::size_t>(0);
    add(tu("Schattenart: Stand merken", [=] { *d3dVorher = g_app->d3dMeldungen.size(); }));
    for (int art = 0; art <= 3; ++art) {
        const std::string n = std::to_string(art);
        add(tu("Schattenart " + n, [=] { g_app->schattenArt = art; g_app->mapDirty = true; }));
        add(gezeichnet("Schattenart " + n));
        add(pruefe("Seitenleiste: Schattenart " + n + " zeichnet ohne Fehler", [=] {
            return g_app->gpuFehler.empty() && g_app->d3dMeldungen.size() == *d3dVorher;
        }));
    }
    add(tu("Schattenart zurueck auf rund", [] { g_app->schattenArt = 1; g_app->mapDirty = true; }));
    add(gezeichnet("Schattenart rund"));
    // Wirkung einzeln
    add(tu("Entities aus", [] { g_app->showEntities = false; g_app->mapDirty = true; }));
    add(gezeichnet("ohne Entities"));
    add(pruefe("Entities aus: keine Kreuze und nichts anklickbar", [] { return g_app->markers.empty(); }));
    add(tu("Entities an", [] { g_app->showEntities = true; g_app->mapDirty = true; }));
    auto stapelMitHimmel = std::make_shared<std::size_t>(0);
    add(tu("Stapel mit Himmel merken, Himmel aus", [=] {
        *stapelMitHimmel = g_app->mesh.batches.size();
        g_app->showSky = true;
    }));
    add(klick(tr(Str::MapSky)));
    add(gezeichnet("Himmel aus"));
    add(pruefe("Sky aus baut das Netz neu (weniger Stapel)", [=] {
        diag::detail("Knopftest: Stapel mit Himmel " + std::to_string(*stapelMitHimmel) + ", ohne " +
                     std::to_string(g_app->mesh.batches.size()));
        return !g_app->showSky && g_app->mesh.batches.size() < *stapelMitHimmel && g_app->mapBereit;
    }));
    add(klick(tr(Str::MapSky)));
    add(gezeichnet("Himmel an"));
    add(pruefe("Glow liefert Gluehen (GPU-Aufrufe)", [] {
        diag::detail("Knopftest: gpuGluehen " + std::to_string(g_app->gpuGluehen));
        return !g_app->showGlow || g_app->gpuGluehen > 0;
    }));
    add(pruefe("Figuren: Modelle geladen", [] {
        int mit = 0;
        for (const App::ActorAssets& a : g_app->actorAssets) { if (!a.model.empty()) { ++mit; } }
        diag::detail("Knopftest: Figuren " + std::to_string(g_app->scene.actors.size()) + ", mit Modell " +
                     std::to_string(mit));
        return mit > 0;
    }));
    // Detail: Regler nach links ziehen baut das Netz neu
    auto detail0 = std::make_shared<int>(0);
    add(tu("Detail merken", [=] { *detail0 = g_app->meshDetail; }));
    add(ziehenAn("Detail-Regler nach links", [] {
        const Element* e = finde(tr(Str::MapDetail), "", 0, false);
        return e != nullptr ? ImVec2{e->rect.Max.x - 4.0F, e->rect.GetCenter().y} : ImVec2{0, 0};
    }, -400.0F, 0.0F, 0));
    add(gezeichnet("Detail geaendert"));
    add(pruefe("Detail-Regler aendert die Stufe und das Netz steht wieder", [=] {
        diag::detail("Knopftest: Detail " + std::to_string(*detail0) + " -> " + std::to_string(g_app->meshDetail));
        return g_app->meshDetail != *detail0 && g_app->mapBereit && !g_app->mesh.batches.empty();
    }));
    add(tu("Detail zurueck", [=] { g_app->meshDetail = *detail0; }));
    add(ziehenAn("Detail-Regler nach rechts", [] {
        const Element* e = finde(tr(Str::MapDetail), "", 0, false);
        return e != nullptr ? ImVec2{e->rect.Min.x + 4.0F, e->rect.GetCenter().y} : ImVec2{0, 0};
    }, 600.0F, 0.0F, 0));
    add(gezeichnet("Detail wieder hoch"));

    // ---- Navigation in der freien Ansicht --------------------------------
    auto pos0 = std::make_shared<std::array<float, 3>>();
    auto ang0 = std::make_shared<std::array<float, 3>>();
    auto merke = [=]() {
        return tu("Kamera merken", [=] {
            for (int k = 0; k < 3; ++k) { (*pos0)[k] = g_app->cam.pos[k]; (*ang0)[k] = g_app->cam.angles[k]; }
        });
    };
    auto bewegt = [=](const std::string& was, bool ort, bool winkel) {
        return pruefe("Navigation: " + was, [=] {
            float dp = 0, da = 0;
            for (int k = 0; k < 3; ++k) {
                dp += std::fabs(g_app->cam.pos[k] - (*pos0)[k]);
                da += std::fabs(g_app->cam.angles[k] - (*ang0)[k]);
            }
            char z[160];
            std::snprintf(z, sizeof(z), "Knopftest: %s - Ort %.1f, Winkel %.1f", was.c_str(),
                          static_cast<double>(dp), static_cast<double>(da));
            diag::detail(z);
            return (!ort || dp > 1.0F) && (!winkel || da > 0.5F);
        });
    };
    add(merke()); add(halteTaste("W", ImGuiKey_W, 20)); add(bewegt("W faehrt vor", true, false));
    add(merke()); add(halteTaste("D", ImGuiKey_D, 20)); add(bewegt("D faehrt seitwaerts", true, false));
    add(merke()); add(halteTaste("E", ImGuiKey_E, 20)); add(bewegt("E faehrt hoch", true, false));
    add(merke()); add(ziehenAn("rechte Maustaste (Umsehen)", ansichtMitte, 120.0F, 40.0F, 1));
    add(bewegt("rechte Maustaste dreht den Blick", false, true));
    add(merke()); add(ziehenAn("mittlere Maustaste (Schieben)", ansichtMitte, 120.0F, 60.0F, 2));
    add(bewegt("mittlere Maustaste schiebt", true, false));
    add(merke()); add(radAn("Zoom", ansichtMitte, 3.0F));
    add(bewegt("Mausrad zoomt", true, false));
    add(merke()); add(ziehenAn("Alt + mittlere Maustaste (Umkreisen)", ansichtMitte, 150.0F, 0.0F, 2, ImGuiMod_Alt));
    add(bewegt("Alt + mittlere Maustaste umkreist", true, true));

    // ---- Anklicken: Entity, Kamera, Schluessel ----------------------------
    add(tu("Kamera zurueck vor den Schluessel", [] { kameraVorDenSchluessel(); g_app->cameraSelected = false; }));
    add(gezeichnet("vor dem Schluessel"));
    auto zielEntity = std::make_shared<std::size_t>(0);
    add(klickAn("auf ein Entitykreuz", [=] {
        // Das Kreuz, das am weitesten von Kamera, Figuren und Schluesseln
        // weg liegt - die haben beim Klick Vorrang.
        if (g_app->markers.empty()) { return ImVec2{0, 0}; }
        float best = -1.0F;
        const App::PickedMarker* wahl = nullptr;
        for (const auto& m : g_app->markers) {
            float d = 1e9F;
            auto nah = [&](float x, float y) { d = std::min(d, (m.sx - x) * (m.sx - x) + (m.sy - y) * (m.sy - y)); };
            nah(g_app->cameraSx, g_app->cameraSy);
            for (const auto& am : g_app->actorMarks) { nah(am.sx, am.sy); }
            for (const auto& km : g_app->keyMarks) { nah(km.sx, km.sy); }
            for (const auto& o : g_app->markers) { if (&o != &m) { nah(o.sx, o.sy); } }
            const bool drin = m.sx > 20 && m.sy > 20 && m.sx < g_app->mapImage.width - 20 &&
                              m.sy < g_app->mapImage.height - 20;
            if (drin && d > best) { best = d; wahl = &m; }
        }
        if (wahl == nullptr) { return ImVec2{0, 0}; }
        *zielEntity = wahl->entity;
        return bildZuSchirm(wahl->sx, wahl->sy);
    }));
    add(pruefe("Klick auf ein Entitykreuz waehlt diese Entity", [=] {
        diag::detail("Knopftest: pickedEntity " + std::to_string(g_app->pickedEntity) + ", erwartet " +
                     std::to_string(*zielEntity));
        return g_app->pickedEntity == static_cast<int>(*zielEntity);
    }));
    add(klickAn("auf die Kamera", [] { return bildZuSchirm(g_app->cameraSx, g_app->cameraSy); }));
    add(pruefe("Klick auf die Kamera waehlt sie (Bahn und Schluessel)", [] {
        return g_app->cameraSelected && !g_app->keyMarks.empty();
    }));
    add(gezeichnet("Kamera gewaehlt"));
    auto zielKey = std::make_shared<Path>();
    add(klickAn("auf einen Schluessel", [=] {
        if (g_app->keyMarks.size() < 2) { return ImVec2{0, 0}; }
        const auto& k = g_app->keyMarks[1];
        *zielKey = k.path;
        return bildZuSchirm(k.sx, k.sy);
    }));
    add(pruefe("Klick auf einen Schluessel waehlt ihn und seine Skriptzeile",
               [=] { return !zielKey->empty() && g_app->selectedPath == *zielKey; }));

    // ---- Gizmo: Rechtsklick-Menue, Verschieben, Zurueckschreiben, Undo ------
    add(klickAn("rechts kurz (Gizmomenue)", ansichtMitte, 1));
    add(pruefe("Rechtsklick oeffnet das Gizmomenue", [] {
        return finde(tr(Str::GizmoMove), "##Popup", 0, false) != nullptr;
    }));
    add(klick(tr(Str::GizmoMove), "##Popup"));
    add(pruefe("Gizmo: Modus Verschieben", [] { return g_app->gizmoMode == App::GizmoMode::Move; }));
    auto moveVorher = std::make_shared<std::string>();
    add(tu("Gizmo um 32 Einheiten verschieben (wie gezogen)", [=] {
        const Path w = !g_app->keyMarks.empty() && g_app->selectedKey >= 0 &&
                               static_cast<std::size_t>(g_app->selectedKey) < g_app->keyMarks.size()
                           ? g_app->keyMarks[static_cast<std::size_t>(g_app->selectedKey)].path
                           : Path{};
        const Node* n = w.empty() ? nullptr : nodeAt(g_app->doc.script(), w);
        *moveVorher = (n != nullptr && n->args.size() > 1) ? n->args[1].text : "";
        g_app->gizmoOffset[0] = 32.0F;
        g_app->mapDirty = true;
    }));
    add(gezeichnet("Gizmo-Vorschau"));
    add(klick(tr(Str::GizmoApply)));
    add(pruefe("Write to script: der MOVE-Vektor ist geaendert", [=] {
        const Path w = g_app->keyMarks.empty() || g_app->selectedKey < 0 ? Path{}
                       : g_app->keyMarks[static_cast<std::size_t>(g_app->selectedKey)].path;
        const Node* n = w.empty() ? nullptr : nodeAt(g_app->doc.script(), w);
        const std::string jetzt = (n != nullptr && n->args.size() > 1) ? n->args[1].text : "";
        diag::detail("Knopftest: MOVE vorher \"" + *moveVorher + "\" nachher \"" + jetzt + "\"");
        return !moveVorher->empty() && jetzt != *moveVorher && g_app->gizmoOffset[0] == 0.0F;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pruefe("Strg+Z nimmt das Zurueckschreiben zurueck", [=] {
        const Path w = g_app->keyMarks.empty() || g_app->selectedKey < 0 ? Path{}
                       : g_app->keyMarks[static_cast<std::size_t>(g_app->selectedKey)].path;
        const Node* n = w.empty() ? nullptr : nodeAt(g_app->doc.script(), w);
        return n != nullptr && n->args.size() > 1 && n->args[1].text == *moveVorher;
    }));
    add(klickAn("rechts kurz (Gizmomenue)", ansichtMitte, 1));
    add(klick(tr(Str::GizmoSelect), "##Popup"));
    add(klickAn("ins Leere", [] { return ImVec2{g_app->kartenAnsicht[0] + 20.0F, g_app->kartenAnsicht[1] + 20.0F}; }));
    add(pruefe("Klick ins Leere hebt Kamera- und Entitywahl auf",
               [] { return !g_app->cameraSelected && g_app->pickedEntity < 0; }));

    // ---- Insert camera here ----------------------------------------------
    auto anzahl0 = std::make_shared<std::size_t>(0);
    add(tu("Erste Skriptzeile waehlen", [=] {
        selectByPath(Path{0});
        *anzahl0 = g_app->doc.script().nodes.size();
    }));
    add(klick(tr(Str::MapInsertCamera)));
    add(pruefe("Insert camera here: MOVE und PAN mit der Kamerastelle dahinter", [=] {
        const auto& ns = g_app->doc.script().nodes;
        if (ns.size() != *anzahl0 + 2 || ns.size() < 3) { return false; }
        const Node& a = ns[1];
        const Node& b = ns[2];
        float v[3];
        const bool ok = a.name == "camera" && a.args.size() >= 2 && a.args[0].text == "MOVE" &&
                        b.name == "camera" && b.args.size() >= 2 && b.args[0].text == "PAN" &&
                        leseVektor(a.args[1].text, v) && std::fabs(v[0] - g_app->cam.pos[0]) < 1.0F;
        return ok;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pruefe("Strg+Z nimmt die eingefuegte Kamera zurueck",
               [=] { return g_app->doc.script().nodes.size() == *anzahl0; }));

    // ---- Entityliste links -------------------------------------------------
    auto marker0 = std::make_shared<std::size_t>(0);
    add(tu("Kreuze merken", [=] { kameraVorDenSchluessel(); }));
    add(gezeichnet("Kreuze"));
    add(tu("Anzahl Kreuze merken", [=] { *marker0 = g_app->markers.size(); }));
    add(klick("O", "", 0));
    add(gezeichnet("Klasse ausgeblendet"));
    add(pruefe("Auge einer Klasse blendet sie aus (weniger Kreuze)", [=] {
        diag::detail("Knopftest: Kreuze " + std::to_string(*marker0) + " -> " + std::to_string(g_app->markers.size()) +
                     ", ausgeblendete Klassen " + std::to_string(g_app->hiddenClasses.size()));
        return !g_app->hiddenClasses.empty() && g_app->markers.size() < *marker0;
    }));
    add(klick(tr(Str::EntityShowAll)));
    add(gezeichnet("alle wieder"));
    add(pruefe("Show all zeigt alles wieder", [=] {
        return g_app->hiddenClasses.empty() && g_app->hiddenEntities.empty() && g_app->markers.size() == *marker0;
    }));

    // ---- Zeitleiste --------------------------------------------------------
    add(tu("Zeitleiste auf Anfang", [] { g_app->playMs = 0.0; g_app->playing = false; }));
    add(klick(tr(Str::TlPlay), "###main"));
    add(pause(0.6));
    add(pruefe("Play: die Zeit laeuft", [] { return g_app->playing && g_app->playMs > 100.0; }));
    add(klick(tr(Str::TlStop), "###main"));
    add(pruefe("Stop haelt an", [] { return !g_app->playing; }));
    auto ms0 = std::make_shared<double>(0);
    add(tu("Zeit merken", [=] { *ms0 = g_app->playMs; }));
    add(pause(0.3));
    add(pruefe("Angehalten bleibt die Zeit stehen", [=] { return g_app->playMs == *ms0; }));
    add(klick(tr(Str::TlRewind), "###main"));
    add(pruefe("To start: Zeit auf null", [] { return g_app->playMs < 0.5; }));
    for (const auto& [sw, wert] : std::vector<std::pair<Str, bool*>>{
             {Str::TlAudio, &g_app->playAudio}, {Str::TlFollow, &g_app->followCam}, {Str::TlTracks, &g_app->showTracks}}) {
        auto v0 = std::make_shared<bool>(false);
        const std::string name = tr(sw);
        bool* w = wert;
        add(tu("Stand " + name, [=] { *v0 = *w; }));
        add(klick(name, "###main"));
        add(pruefe("Zeitleiste: " + name + " schaltet um", [=] { return *w != *v0; }));
        add(klick(name, "###main"));
        add(pruefe("Zeitleiste: " + name + " zurueck", [=] { return *w == *v0; }));
    }
    auto einheit0 = std::make_shared<bool>(false);
    add(tu("Einheit merken", [=] { *einheit0 = g_app->timelineFrames; }));
    add(klick(g_app->timelineFrames ? tr(Str::TlUnitFrames) : tr(Str::TlUnitSeconds), "###main"));
    add(pruefe("Frames/Seconds schaltet die Anzeige um", [=] { return g_app->timelineFrames != *einheit0; }));
    add(tu("Einheit zurueck", [=] { g_app->timelineFrames = *einheit0; }));
    add(klick(tr(Str::TlZoomMenu), "###main", 0, true));
    add(klick("4x", "##Popup", 0, true));
    add(pruefe("Zoom-Menue: 4x", [] { return std::fabs(g_app->timelineZoom - 4.0F) < 0.01F; }));
    add(klick(tr(Str::TlZoomMenu), "###main", 0, true));
    add(klick(tr(Str::TlFit), "##Popup"));
    add(pruefe("Zoom-Menue: All", [] { return std::fabs(g_app->timelineZoom - 1.0F) < 0.01F; }));
    // Der Zeiger steht seit 27.09. im Lineal: dort ziehen setzt die Zeit.
    add(ziehenAn("Abspielzeiger (im Lineal)", [] {
        const Element* e = finde("##zeitlineal", "###main", 0, false);
        return e != nullptr ? ImVec2{e->rect.Min.x + 6.0F, e->rect.GetCenter().y} : ImVec2{0, 0};
    }, 300.0F, 0.0F, 0));
    add(pruefe("Abspielzeiger ziehen setzt die Zeit (angehalten)", [] {
        diag::detail("Knopftest: playMs nach dem Ziehen " + std::to_string(g_app->playMs));
        return g_app->playMs > 100.0 && !g_app->playing;
    }));

    // ---- Reiterwechsel: die Karte bleibt, jeder Reiter hat seine Kamera ----
    auto reiter0 = std::make_shared<int>(-1);
    add(tu("Zu einem anderen Missionsreiter", [=] {
        *reiter0 = g_app->activeTab;
        g_app->tabRequest = (g_app->activeTab + 1) % static_cast<int>(g_app->tabs.size());
    }));
    add(pause(0.4));
    add(pruefe("Reiterwechsel: Karte weiter geladen und gezeichnet",
               [] { return !g_app->geo.empty() && g_app->mapBereit; }));
    add(tu("Zurueck zum ersten Reiter", [=] { g_app->tabRequest = *reiter0; }));
    add(pause(0.4));

    // ---- Through camera "am Abspielzeiger": keine falschen Klickziele ----
    add(tu("Keine Kamerazeile waehlen, Through camera an", [] {
        selectByPath(Path{0});
        g_app->playMs = 2000.0;
        g_app->throughCamera = true;
        g_app->mapDirty = true;
    }));
    add(gezeichnet("Kamerablick am Zeiger"));
    add(pruefe("Through camera am Zeiger: Kamerablick aktiv, keine Gizmos/Klickziele der freien Kamera", [] {
        return g_app->camViewActive && g_app->markers.empty() && g_app->keyMarks.empty() &&
               g_app->cameraSx < 0.0F;
    }));
    add(tu("Through camera aus", [] { g_app->throughCamera = false; g_app->mapDirty = true; }));
    add(gezeichnet("frei"));

    // ---- Effekte haengen an der Karte, nicht an den Figuren ---------------
    auto effekteMit = std::make_shared<std::size_t>(0);
    auto reiterMit = std::make_shared<int>(-1);
    add(tu("Effekte merken", [=] {
        // Ohne die fx_runner, die erst ein use des Skripts startet
        // (START_OFF/ONESHOT, MoverSim::fxStarts) und ohne die Brucheffekte
        // (bruchFxZahl) - die haengen am Skript und fehlen in einem anderen
        // Reiter zu Recht.
        *effekteMit = g_app->effectRunners.size() - g_app->moverSim.fxStarts().size() - g_app->bruchFxZahl;
        *reiterMit = g_app->activeTab;
        diag::detail("Knopftest: Effekte mit Figuren " + std::to_string(*effekteMit) + ", Figuren " +
                     std::to_string(g_app->scene.actors.size()));
        // Ein Reiter derselben Mission, dessen Skript keine Figuren hat.
        for (int i = 0; i < static_cast<int>(g_app->tabs.size()); ++i) {
            if (i == g_app->activeTab) { continue; }
            const Script& sc = g_app->tabs[static_cast<std::size_t>(i)].doc.script();
            bool figuren = false;
            for (const Node& n : sc.nodes) { if (n.name == "affect") { figuren = true; } }
            if (!figuren && !sc.nodes.empty()) { g_app->tabRequest = i; break; }
        }
    }));
    add(pause(0.6));
    add(pruefe("Reiter ohne Figuren: die Effekte der Karte (ohne die per use gestarteten) sind trotzdem geladen", [=] {
        diag::detail("Knopftest: Reiter " + std::to_string(g_app->activeTab) + ", Figuren " +
                     std::to_string(g_app->scene.actors.size()) + ", Effekte " +
                     std::to_string(g_app->effectRunners.size()));
        return g_app->activeTab == *reiterMit ||
               g_app->effectRunners.size() - g_app->moverSim.fxStarts().size() - g_app->bruchFxZahl == *effekteMit;
    }));
    add(tu("Zurueck zum Reiter mit Figuren", [=] { g_app->tabRequest = *reiterMit; }));
    add(pause(0.5));

    // ---- Klaenge nach dem Ende der Kamerabahn? ---------------------------
    add(pruefe("Befund: Klaenge hinter dem Abspielende (werden nie gespielt)", [] {
        int danach = 0;
        const double ende = g_app->camTrack.durationMs;
        for (const TimelineTrack& tr : g_app->timeline.tracks) {
            for (const TimelineEvent& e : tr.events) {
                if (e.kind == TimelineEvent::Kind::Sound && e.startMs > ende + 1.0 &&
                    e.startMs < g_app->timeline.practicalEndMs + 1.0) {
                    if (++danach <= 5) {
                        diag::detail("Knopftest: Klang nach dem Kameraende: " + e.sound + " bei " +
                                     std::to_string(e.startMs) + " ms (Ende " + std::to_string(ende) + ")");
                    }
                }
            }
        }
        diag::detail("Knopftest: Kameraende " + std::to_string(ende) + " ms, praktisches Ende " +
                     std::to_string(g_app->timeline.practicalEndMs) + " ms, Klaenge danach " +
                     std::to_string(danach));
        return danach == 0;
    }));

    // ---- Karte verlassen beim Abspielen haelt an --------------------------
    add(tu("Abspielen und in die Skriptansicht wechseln", [] {
        g_app->playMs = 0.0;
        g_app->playing = true;
    }));
    add(pause(0.3));
    add(tu("Modus Events", [] { g_app->leftMode = 0; }));
    add(pause(0.2));
    add(pruefe("Karte verlassen haelt das Abspielen an (keine stehende Uhr mit laufendem Ton)",
               [] { return !g_app->playing; }));
    add(tu("Zurueck in die Kartenansicht", [] { g_app->leftMode = 1; }));
    add(pause(0.3));

    // ---- Debug-Menue ohne RenderDoc ---------------------------------------
    add(pause(0.3));
    // Im Hauptfenster: ImGuis unsichtbares Fenster "Debug##Default" traegt
    // denselben Namen und liegt bei 60/60 - mitten auf der Entityliste.
    add(klick(tr(Str::MenuDebug), "###main"));
    add(pruefe("Debug > Show layout numbers ist da (ohne RenderDoc bedienbar)", [] {
        return finde(tr(Str::DebugLayout), "##Menu", 0, false) != nullptr;
    }));
    add(klick(tr(Str::DebugLayout), "##Menu"));
    add(pruefe("Show layout numbers oeffnet das Fenster", [] { return g_app->layoutOpen; }));
    add(tu("Layoutfenster zu", [] { g_app->layoutOpen = false; }));

    // ---- Entityliste: Klasse aufklappen, Mitglied waehlen -----------------
    auto mitglied = std::make_shared<std::string>();
    auto mitgliedIndex = std::make_shared<int>(-1);
    add(tu("Erstes benanntes waypoint_navgoal suchen", [=] {
        for (std::size_t i = 0; i < g_app->map.entities.size(); ++i) {
            const MapEntity& e = g_app->map.entities[i];
            if (e.classname == "waypoint_navgoal" && !e.targetname.empty()) {
                *mitglied = e.targetname;
                *mitgliedIndex = static_cast<int>(i);
                break;
            }
        }
        g_app->pickedEntity = -1;
    }));
    add(klick(tr(Str::EntityKatNav), "", 0, true));
    add(pruefe("Klick auf die Gruppe \"Laufziele\" klappt sie auf",
               [] { return g_app->expandedClass == "kat1"; }));
    add({"Klick auf das Mitglied", [=](int b) {
             static Schritt k;
             if (b == 0) { k = klick(*mitglied); }
             return k.tun(b);
         }});
    add(pruefe("Klick auf ein Mitglied der Liste waehlt diese Entity", [=] {
        diag::detail("Knopftest: Mitglied " + *mitglied + " (" + std::to_string(*mitgliedIndex) + "), gewaehlt " +
                     std::to_string(g_app->pickedEntity));
        return *mitgliedIndex >= 0 && g_app->pickedEntity == *mitgliedIndex;
    }));
    // Rechtsklick auf das Mitglied: "Als Laufziel einfuegen" setzt ein
    // set ( SET_NAVGOAL, <name> ) hinter die gewaehlte Zeile.
    auto zeilen0 = std::make_shared<std::size_t>(0);
    add(tu("Zeilen vor dem Einfuegen zaehlen", [=] { *zeilen0 = g_app->rows.size(); }));
    add({"Rechtsklick auf das Mitglied", [=](int b) {
             static Schritt k;
             if (b == 0) { k = klick(*mitglied, "", 0, false, "", 1); }
             return k.tun(b);
         }});
    add(pause(0.2));
    add(klick(tr(Str::EntityAsNavgoal)));
    add(pause(0.3));
    add(pruefe("Rechtsklick > Als Laufziel einfuegen setzt SET_NAVGOAL mit dem Namen ein", [=] {
        bool da = false;
        std::function<void(const std::vector<Node>&)> such = [&](const std::vector<Node>& ns) {
            for (const Node& n : ns) {
                if (n.name == "set" && n.args.size() >= 2 && n.args[0].text == "SET_NAVGOAL" &&
                    n.args[1].text == *mitglied) {
                    da = true;
                }
                such(n.children);
            }
        };
        such(g_app->doc.script().nodes);
        diag::detail("Knopftest: Zeilen " + std::to_string(*zeilen0) + " -> " + std::to_string(g_app->rows.size()));
        return da && g_app->rows.size() == *zeilen0 + 1;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.2));
    add(pruefe("Strg+Z nimmt das eingefuegte Laufziel zurueck", [=] { return g_app->rows.size() == *zeilen0; }));
    // Doppelklick fliegt hin: die freie Kamera steht danach nahe der Entity.
    add({"Doppelklick auf das Mitglied", [=](int b) {
             static Schritt k;
             if (b == 0) {
                 const Element* el = finde(*mitglied, "", 0, false, "");
                 const ImVec2 c = (el != nullptr) ? el->rect.GetCenter() : ImVec2{0.0F, 0.0F};
                 k = klickAn("Doppelklick auf " + *mitglied, [c] { return c; }, 0, true);
             }
             return k.tun(b);
         }});
    add(pause(0.3));
    add(pruefe("Doppelklick auf eine Entity fliegt die Kamera hin", [=] {
        if (*mitgliedIndex < 0) { return false; }
        float p[3];
        if (!entityOrt(g_app->map.entities[static_cast<std::size_t>(*mitgliedIndex)], p)) { return false; }
        const float dx = p[0] - g_app->cam.pos[0];
        const float dy = p[1] - g_app->cam.pos[1];
        const float d = std::sqrt(dx * dx + dy * dy);
        diag::detail("Knopftest: Abstand Kamera - Entity " + std::to_string(d));
        return d < 250.0F;
    }));
    add(tu("Klasse wieder zu", [] { g_app->expandedClass.clear(); }));

    // ---- Gizmo drehen und zurueckschreiben --------------------------------
    add(tu("Kamera vor den Schluessel, Kamera waehlen", [] {
        kameraVorDenSchluessel();
        g_app->cameraSelected = false;
    }));
    add(gezeichnet("vor dem Schluessel"));
    add(klickAn("auf die Kamera", [] { return bildZuSchirm(g_app->cameraSx, g_app->cameraSy); }));
    add(klickAn("rechts kurz (Gizmomenue)", ansichtMitte, 1));
    add(klick(tr(Str::GizmoRotate), "##Popup"));
    add(pruefe("Gizmo: Modus Drehen", [] { return g_app->gizmoMode == App::GizmoMode::Rotate; }));
    auto panVorher = std::make_shared<std::string>();
    auto panWeg = std::make_shared<Path>();
    add(tu("Um 15 Grad drehen (wie gezogen)", [=] {
        // Das PAN nach dem gewaehlten Schluessel (bis zum naechsten MOVE).
        panWeg->clear();
        if (g_app->selectedKey >= 0 && static_cast<std::size_t>(g_app->selectedKey) < g_app->keyMarks.size()) {
            Path w = g_app->keyMarks[static_cast<std::size_t>(g_app->selectedKey)].path;
            const auto& ns = g_app->doc.script().nodes;
            for (std::size_t i = w.empty() ? 0 : w.back() + 1; w.size() == 1 && i < ns.size(); ++i) {
                if (ns[i].name != "camera" || ns[i].args.empty()) { continue; }
                if (ns[i].args[0].text == "MOVE") { break; }
                if (ns[i].args[0].text == "PAN") { *panWeg = Path{i}; break; }
            }
        }
        const Node* n = panWeg->empty() ? nullptr : nodeAt(g_app->doc.script(), *panWeg);
        *panVorher = (n != nullptr && n->args.size() > 1) ? n->args[1].text : "";
        g_app->gizmoAngles[1] = 15.0F;
        g_app->mapDirty = true;
    }));
    add(gezeichnet("Drehvorschau"));
    add(klick(tr(Str::GizmoApply)));
    add(pruefe("Write to script (Drehung): das PAN nach dem Schluessel ist geaendert", [=] {
        const Node* n = panWeg->empty() ? nullptr : nodeAt(g_app->doc.script(), *panWeg);
        const std::string jetzt = (n != nullptr && n->args.size() > 1) ? n->args[1].text : "";
        diag::detail("Knopftest: PAN vorher \"" + *panVorher + "\" nachher \"" + jetzt + "\"");
        return !panVorher->empty() && jetzt != *panVorher;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pruefe("Strg+Z nimmt die Drehung zurueck", [=] {
        const Node* n = panWeg->empty() ? nullptr : nodeAt(g_app->doc.script(), *panWeg);
        return n != nullptr && n->args.size() > 1 && n->args[1].text == *panVorher;
    }));
    add(klickAn("rechts kurz (Gizmomenue)", ansichtMitte, 1));
    add(klick(tr(Str::GizmoSelect), "##Popup"));
    add(tu("Kamera abwaehlen", [] { g_app->cameraSelected = false; g_app->mapDirty = true; }));

    // ---- Karte schliessen, ueber den .pk3-Browser neu laden ---------------
    add(klick(tr(Str::MenuFile), "###main"));
    add(klick(tr(Str::MapClose), "##Menu"));
    add(pause(0.3));
    add(pruefe("File > Close map: keine Karte mehr, Hinweis 'No map loaded'", [] {
        return g_app->geo.empty() && g_app->map.empty() && g_app->markers.empty();
    }));
    add(klick(tr(Str::OpenMapPk3)));
    add(pause(0.3));
    add(pruefe("From .pk3... oeffnet den Browser mit Karten", [] { return g_app->pk3Open && g_app->pk3Kind == 1; }));
    add(klick(tr(Str::Pk3Filter)));
    add(tippe("md_am_sith"));
    add(pause(0.3));
    add(klick("maps/md_am_sith.bsp", "", 0, false, "", 0, 0.3F));
    add({"Doppelklick auf maps/md_am_sith.bsp", [](int b) {
             static Schritt k;
             if (b == 0) { k = klickAn("maps/md_am_sith.bsp", [] {
                                 const Element* e = finde("maps/md_am_sith.bsp", "", 0, false);
                                 return e != nullptr ? e->rect.GetCenter() : ImVec2{0, 0};
                             }, 0, true); }
             return k.tun(b);
         }});
    add(warteBis("Karte aus dem Browser geladen", [] { return !g_app->geo.empty() && g_app->mapBereit; }, 2000));
    add(tu("Browser zu", [] { g_app->pk3Open = false; }));
    add(pause(0.5));
    add(gezeichnet("neu geladene Karte"));
    add(pruefe("Neu geladene Karte zeichnet ohne Fehler", [] {
        diag::detail("Knopftest: nach dem Neuladen mapDirty " + std::to_string(g_app->mapDirty) + ", mapBereit " +
                     std::to_string(g_app->mapBereit) + ", gpuFehler \"" + g_app->gpuFehler + "\", Stapel " +
                     std::to_string(g_app->mesh.batches.size()));
        return !g_app->mapDirty && g_app->gpuFehler.empty() && g_app->mapBereit;
    }));

    // ---- Protokoll: keine Fehler waehrend der Kartenfunktionen ------------
    add(pruefe("Protokoll: kein Schritt mit FEHLER waehrend der Kartenfunktionen", [=] {
        const auto& z = diag::lines();
        int fehler = 0;
        for (std::size_t i = *logAb; i < z.size(); ++i) {
            if (z[i].find("  FEHLER: ") != std::string::npos) {
                if (++fehler <= 10) { diag::detail("Knopftest: im Protokoll: " + z[i]); }
            }
        }
        return fehler == 0;
    }));
    return s;
}

// --- Schwebende Fenster: Klick ausserhalb, wieder oeffnen ---------------
//
// shank: "If there is a window open and I click outside of the window, it
// disappears, but I am unable to open/view it again" - bei Notes, Keyboard
// shortcuts, Find. Das Hauptfenster schob sich bei jedem Klick nach vorn
// (ohne ImGuiWindowFlags_NoBringToFrontOnFocus), der Offen-Merker blieb
// stehen, und der Knopf setzte nur, was schon gesetzt war.
//
// Geprueft wird die Z-Reihenfolge selbst (GImGui->Windows, hinten nach
// vorn), nicht ein Bild: das Fenster muss nach dem Klick ausserhalb VOR dem
// Hauptfenster liegen, und nach Schliessen und Oeffnen wieder da sein.
namespace {
int fensterRang(const char* id) {
    ImGuiWindow* w = ImGui::FindWindowByName(id);
    if (w == nullptr) {
        return -1;
    }
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.Windows.Size; ++i) {
        if (g.Windows[i] == w->RootWindow) {
            return i;
        }
    }
    return -1;
}

bool fensterSichtbarVorn(const char* id) {
    ImGuiWindow* w = ImGui::FindWindowByName(id);
    const int r = fensterRang(id);
    const int h = fensterRang("###main");
    return w != nullptr && w->WasActive && !w->Hidden && r > h && h >= 0;
}

// Ein Punkt im Hauptfenster, der NICHT im Fenster `id` liegt: die
// Statuszeile unten, sonst ein Stueck weiter links.
ImVec2 ausserhalbVon(const char* id) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGuiWindow* w = ImGui::FindWindowByName(id);
    const ImVec2 kandidaten[] = {
        {vp->WorkPos.x + vp->WorkSize.x * 0.62F, vp->WorkPos.y + vp->WorkSize.y * 0.985F},
        {vp->WorkPos.x + vp->WorkSize.x * 0.35F, vp->WorkPos.y + vp->WorkSize.y * 0.985F},
        {vp->WorkPos.x + vp->WorkSize.x * 0.90F, vp->WorkPos.y + vp->WorkSize.y * 0.60F},
    };
    for (const ImVec2& k : kandidaten) {
        if (w == nullptr || !w->Rect().Contains(k)) {
            return k;
        }
    }
    return kandidaten[0];
}
}  // namespace

// Nach dem Zusammendruecken: nicht kleiner als benutzbar. Mindestens rund
// 20 Zeichen breit und 7 Zeilen hoch, und nichts seitlich abgeschnitten
// (ScrollMax.x > 0 hiesse: der Inhalt ist breiter als das Fenster). Lange
// Listen duerfen senkrecht rollen.
bool fensterNichtZuKlein(ImGuiWindow* w, const std::string& n) {
    if (w == nullptr) { return false; }
    const float fs = ImGui::GetFontSize();
    char z[240];
    std::snprintf(z, sizeof(z),
                  "Knopftest: %s zusammengedrueckt: %.0f x %.0f (min %.0f x %.0f), Rollbereich x %.0f",
                  n.c_str(), w->Size.x, w->Size.y, fs * 20.0F, fs * 7.0F, w->ScrollMax.x);
    diag::detail(z);
    // Ein Fenster, das sich selbst an den Inhalt anpasst (Find, Layout),
    // laesst sich gar nicht kleiner ziehen - es besteht, wenn nichts
    // abgeschnitten ist. Waagerecht rollen zu koennen gilt als nicht
    // abgeschnitten.
    const bool rollt = (w->Flags & ImGuiWindowFlags_HorizontalScrollbar) != 0;
    const bool abgeschnitten = w->ScrollMax.x > 0.5F && !rollt;
    if ((w->Flags & ImGuiWindowFlags_AlwaysAutoResize) != 0) {
        return !abgeschnitten;
    }
    return w->Size.x >= fs * 20.0F && w->Size.y >= fs * 7.0F && !abgeschnitten;
}

Schritt zusammendruecken(const std::string& n, std::function<ImGuiWindow*()> fenster) {
    return tu(n + ": auf 40 x 40 zusammendruecken", [=] {
        if (ImGuiWindow* w = fenster()) {
            ImGui::SetWindowSize(w, ImVec2{40.0F, 40.0F});
        }
    });
}

// --- Kuerzelfenster: eine Zeile finden und anklicken -----------------------
//
// Jeder Knopf traegt "###k_<name>" (app.cpp). Seine Beschriftung meldet ImGui
// aber nur, solange er SICHTBAR ist - also wird das Fenster von oben nach
// unten gerollt, bis die Zeile ganz im Bild steht.
const Element* findeEndung(const std::string& endung, const std::string& fenster) {
    for (const Element& e : g_elemente) {
        if (e.label.size() < endung.size() ||
            e.label.compare(e.label.size() - endung.size(), endung.size(), endung) != 0) {
            continue;
        }
        if (!fenster.empty() && e.fenster.find(fenster) == std::string::npos &&
            e.innen.find(fenster) == std::string::npos) {
            continue;
        }
        return &e;
    }
    return nullptr;
}

Schritt kuerzelZeileSuchen(const std::string& name) {
    return {"Kuerzelzeile " + name + " ins Bild rollen", [=](int b) {
                ImGuiWindow* w = ImGui::FindWindowByName("###keys");
                if (w == nullptr) { return b > 60; }
                const Element* e = findeEndung("###k_" + name, "###keys");
                if (e != nullptr && e->rect.Min.y >= w->InnerRect.Min.y &&
                    e->rect.Max.y <= w->InnerRect.Max.y - 4.0F && b > 1) {
                    return true;
                }
                if (b == 0) {
                    ImGui::SetScrollY(w, 0.0F);
                } else if (b % 2 == 0) {
                    ImGui::SetScrollY(w, w->Scroll.y + w->InnerRect.GetHeight() * 0.4F);
                }
                if (b > 120) {
                    diag::detail("Knopftest: Kuerzelzeile " + name + " nicht gefunden");
                    return true;
                }
                return false;
            }};
}

Schritt kuerzelZeileKlicken(const std::string& name) {
    return klickAn("Kuerzelzeile " + name, [=] {
        const Element* e = findeEndung("###k_" + name, "###keys");
        return e != nullptr ? e->rect.GetCenter() : ImVec2{0.0F, 0.0F};
    });
}

// Eine Taste mit beliebig vielen Modifikatoren (Strg, Umschalt, Alt).
Schritt tasteKombi(ImGuiKeyChord chord, const std::string& name) {
    return {"Taste " + name, [=](int b) {
                ImGuiIO& io = ImGui::GetIO();
                const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
                const bool strg = (chord & ImGuiMod_Ctrl) != 0;
                const bool umsch = (chord & ImGuiMod_Shift) != 0;
                const bool alt = (chord & ImGuiMod_Alt) != 0;
                if (b == 0) {
                    if (strg) { io.AddKeyEvent(ImGuiMod_Ctrl, true); }
                    if (umsch) { io.AddKeyEvent(ImGuiMod_Shift, true); }
                    if (alt) { io.AddKeyEvent(ImGuiMod_Alt, true); }
                    return false;
                }
                if (b == 1) { io.AddKeyEvent(key, true); return false; }
                if (b == 2) { io.AddKeyEvent(key, false); return false; }
                if (b == 3) {
                    if (strg) { io.AddKeyEvent(ImGuiMod_Ctrl, false); }
                    if (umsch) { io.AddKeyEvent(ImGuiMod_Shift, false); }
                    if (alt) { io.AddKeyEvent(ImGuiMod_Alt, false); }
                    return false;
                }
                return b >= 6;
            }};
}

// --- JEDE Belegung aendern und ausprobieren ------------------------------
//
// shank: "kannst du in die tests einbauen, dass nicht nur Suche in den
// Hotkeys geaendert und getestet wird, sondern jedes andere auch".
//
// Je Aktion: die Zeile im Kuerzelfenster ANKLICKEN, eine neue, eindeutige
// Kombination DRUECKEN, pruefen, dass sie aufgenommen ist, das Fenster
// schliessen, die neue Kombination druecken und die WIRKUNG pruefen, dann
// die alte druecken - die darf nichts mehr ausloesen. Am Ende "Reset all to
// defaults" und alle Vorgaben wieder da.
//
// Open und Save as oeffnen einen Windows-Dialog, Save all saehe andere
// Reiter - fuer die drei meldet behaved im Test nur, was ausgeloest wurde
// (App::kuerzelNurMelden).
//
// Gefunden damit: Backup und Restore liessen sich belegen, die Belegung tat
// aber nichts.
const char* const kKuerzelSkript =
    "rem ( \"kuerzel\" );\n"
    "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 10.000 20.000 40.000 >, 0 );\n"
    "wait ( 100.000 );\n"
    "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 10.000 20.000 40.000 >, 0 );\n"
    "wait ( 200.000 );\n"
    "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 50.000 20.000 40.000 >, 0 );\n"
    "affect ( \"x\", /*@AFFECT_TYPE*/ FLUSH )\n"
    "{\n"
    "\twait ( 300.000 );\n"
    "}\n"
    "wait ( 400.000 );\n"
    "//(BHVDREM)  wait ( 999.000 );\n";

std::string kuerzelDatei() {
    return (std::filesystem::current_path() / "selbsttest_kuerzel.txt").string();
}

void kuerzelSkriptZurueck(const Path& auswahl) {
    Script sc;
    std::vector<Diag> d;
    (void)readScript(std::string("//Generated by BehavEd\n") + kKuerzelSkript, sc, d);
    (void)g_app->doc.replaceAll(sc, "kuerzeltest");
    g_app->selection.clear();
    selectByPath(auswahl);
    g_app->findOpen = false;
    g_app->editorOpen = false;
    g_app->keysOpen = false;
    g_app->letzteKuerzelAktion = keys::Action::Unhandled;
}

std::string kuerzelBak() {
    return (std::filesystem::current_path() / "selbsttest_kuerzel.bak").string();
}

std::size_t kuerzelKnoten() { return g_app->doc.script().nodes.size(); }

std::vector<Schritt> kuerzelAlleSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    auto vorgaben = std::make_shared<std::map<int, ImGuiKeyChord>>();
    add(tu("Kuerzeltest: eigenes Skript mit Datei, Vorgaben merken", [=] {
        g_app->settings.keyBindings.clear();   // von den Vorgaben aus
        {
            std::ofstream f(kuerzelDatei(), std::ios::binary);
            f << "//Generated by BehavEd\n" << kKuerzelSkript;
        }
        std::error_code ec;
        std::filesystem::remove(kuerzelBak(), ec);
        openScriptFromMemory(std::string("//Generated by BehavEd\n") + kKuerzelSkript,
                             "selbsttest_kuerzel.txt");
        g_app->path = kuerzelDatei();
        g_app->kuerzelNurMelden = true;
        for (const keys::Bindable& b : keys::bindable()) {
            (*vorgaben)[static_cast<int>(b.action)] = kuerzelVon(b.action);
        }
    }));
    add(pause(0.5));

    const ImGuiKeyChord gruppen[3] = {ImGuiMod_Ctrl | ImGuiMod_Shift, ImGuiMod_Ctrl | ImGuiMod_Alt,
                                      ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiMod_Alt};
    int nr = 0;
    for (const keys::Bindable& b : keys::bindable()) {
        const keys::Action a = b.action;
        const std::string name = b.name;
        const ImGuiKeyChord neu = gruppen[(nr / 12) % 3] | static_cast<ImGuiKey>(ImGuiKey_F1 + nr % 12);
        ++nr;
        const std::string neuName = ImGui::GetKeyChordName(neu);

        // --- Belegen, ueber das Fenster ----------------------------------
        add(tu(name + ": Kuerzelfenster oeffnen", [] { fensterZeigen(g_app->keysOpen, "###keys"); }));
        add(pause(0.25));
        add(kuerzelZeileSuchen(name));
        add(kuerzelZeileKlicken(name));
        add(pause(0.15));
        add(pruefe(name + ": die Zeile wartet auf eine Taste", [] { return g_app->kuerzelWartet; }));
        add(tasteKombi(neu, neuName));
        add(pause(0.15));
        add(pruefe(name + ": neue Belegung " + neuName + " aufgenommen", [=] {
            return kuerzelVon(a) == neu;
        }));

        // --- Vorbereiten, ausloesen, Wirkung pruefen -----------------------
        auto vorher = std::make_shared<std::size_t>(0);
        std::function<void()> vorbereiten;
        std::function<bool()> wirkung;
        switch (a) {
            case keys::Action::Delete:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{2}); };
                wirkung = [] { return kuerzelKnoten() == 8U; };
                break;
            case keys::Action::Clone:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{2}); };
                wirkung = [] { return kuerzelKnoten() == 10U; };
                break;
            case keys::Action::Copy:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{2}); };
                wirkung = [] { return kuerzelKnoten() == 9U && g_app->letzteKuerzelAktion == keys::Action::Copy; };
                break;
            case keys::Action::Cut:
            case keys::Action::CutAlt:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{2}); };
                wirkung = [] { return kuerzelKnoten() == 8U; };
                break;
            case keys::Action::Paste:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{2}); kopieren(false); };
                wirkung = [] { return kuerzelKnoten() == 10U; };
                break;
            case keys::Action::CommentOut:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{2}); };
                wirkung = [] {
                    const auto& ns = g_app->doc.script().nodes;
                    return ns.size() > 2 && ns[2].kind == Node::Kind::LineComment;
                };
                break;
            case keys::Action::Uncomment:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{8}); };
                wirkung = [] {
                    const auto& ns = g_app->doc.script().nodes;
                    return ns.size() > 8 && ns[8].kind == Node::Kind::Command && ns[8].name == "wait";
                };
                break;
            case keys::Action::Find:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{2}); };
                wirkung = [] { return g_app->findOpen; };
                break;
            case keys::Action::FindRepeat:
                vorbereiten = [] {
                    kuerzelSkriptZurueck(Path{0});
                    g_app->findLetzte = FindOptions{};
                    g_app->findLetzte.named = "wait";
                };
                wirkung = [] { return g_app->selectedPath == Path{2}; };
                break;
            case keys::Action::FindPrevious:
                vorbereiten = [] {
                    kuerzelSkriptZurueck(Path{4});
                    g_app->findLetzte = FindOptions{};
                    g_app->findLetzte.named = "wait";
                };
                wirkung = [] { return g_app->selectedPath == Path{2}; };
                break;
            case keys::Action::ExpandNode:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{6}); g_app->expanded.closeAll(); rebuildTree(); };
                wirkung = [] { return g_app->expanded.isOpen(kennungAn(Path{6})); };
                break;
            case keys::Action::CollapseNode:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{6}); g_app->expanded.openAll(); rebuildTree(); };
                wirkung = [] { return !g_app->expanded.isOpen(kennungAn(Path{6})); };
                break;
            case keys::Action::ExpandAll:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{0}); g_app->expanded.closeAll(); rebuildTree(); };
                wirkung = [] { return g_app->expanded.isOpen(kennungAn(Path{6})); };
                break;
            case keys::Action::CollapseAll:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{0}); g_app->expanded.openAll(); rebuildTree(); };
                wirkung = [] { return !g_app->expanded.isOpen(kennungAn(Path{6})); };
                break;
            case keys::Action::MoveUp:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{3}); };
                wirkung = [] {
                    const auto& ns = g_app->doc.script().nodes;
                    return ns.size() > 3 && ns[2].name == "camera" && ns[3].name == "wait";
                };
                break;
            case keys::Action::MoveDown:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{2}); };
                wirkung = [] {
                    const auto& ns = g_app->doc.script().nodes;
                    return ns.size() > 3 && ns[2].name == "camera" && ns[3].name == "wait";
                };
                break;
            case keys::Action::Undo:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{2}); (void)g_app->doc.removeAt(Path{2}); rebuildTree(); };
                wirkung = [] { return kuerzelKnoten() == 9U; };
                break;
            case keys::Action::Redo:
                vorbereiten = [] {
                    kuerzelSkriptZurueck(Path{2});
                    (void)g_app->doc.removeAt(Path{2});
                    (void)g_app->doc.undo();
                    rebuildTree();
                };
                wirkung = [] { return kuerzelKnoten() == 8U; };
                break;
            case keys::Action::BookmarkToggle:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{2}); };
                wirkung = [] { return hatLesezeichen(g_app->activeTab, kennungAn(Path{2})); };
                break;
            case keys::Action::BookmarkNext:
                vorbereiten = [] {
                    kuerzelSkriptZurueck(Path{4});
                    lesezeichenUmschalten({Path{4}});
                    selectByPath(Path{0});
                };
                wirkung = [] { return g_app->selectedPath == Path{4}; };
                break;
            case keys::Action::BookmarkPrev:
                vorbereiten = [] {
                    kuerzelSkriptZurueck(Path{4});
                    lesezeichenUmschalten({Path{4}});
                    selectByPath(Path{7});
                };
                wirkung = [] { return g_app->selectedPath == Path{4}; };
                break;
            case keys::Action::EditItem:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{2}); };
                wirkung = [] { return g_app->editorOpen; };
                break;
            case keys::Action::InsertItem:
                vorbereiten = [] {
                    kuerzelSkriptZurueck(Path{2});
                    for (std::size_t c = 0; c < g_app->db.commands.size(); ++c) {
                        if (g_app->db.commands[c].name == "wait") {
                            g_app->selectedCommand = static_cast<int>(c);
                            break;
                        }
                    }
                };
                wirkung = [] { return kuerzelKnoten() == 10U; };
                break;
            case keys::Action::Save:
                vorbereiten = [] {
                    kuerzelSkriptZurueck(Path{2});
                    (void)g_app->doc.removeAt(Path{7});   // anders als die Datei
                    rebuildTree();
                };
                wirkung = [] {
                    std::ifstream f(kuerzelDatei(), std::ios::binary);
                    std::ostringstream ss;
                    ss << f.rdbuf();
                    return !g_app->doc.dirty() && ss.str().find("400.000") == std::string::npos;
                };
                break;
            case keys::Action::Backup:
                vorbereiten = [] {
                    kuerzelSkriptZurueck(Path{2});
                    std::error_code ec;
                    std::filesystem::remove(kuerzelBak(), ec);
                };
                wirkung = [] { return std::filesystem::exists(kuerzelBak()); };
                break;
            case keys::Action::Restore:
                vorbereiten = [] {
                    kuerzelSkriptZurueck(Path{2});
                    std::ofstream f(kuerzelBak(), std::ios::binary);
                    f << "//Generated by BehavEd\n" << kKuerzelSkript;
                };
                wirkung = [] { return g_app->frageOffen; };   // fragt, wie das Original
                break;
            case keys::Action::Open:
            case keys::Action::SaveAs:
            case keys::Action::SaveAll:
            default:
                vorbereiten = [] { kuerzelSkriptZurueck(Path{2}); };
                wirkung = [a] { return g_app->letzteKuerzelAktion == a; };
                break;
        }
        add(tu(name + ": vorbereiten", [=] { vorbereiten(); *vorher = kuerzelKnoten(); }));
        add(pause(0.2));
        add(tasteKombi(neu, neuName));
        add(pause(0.2));
        add(pruefe(name + ": " + neuName + " loest die Aktion aus und wirkt", [=] {
            const auto& ns = g_app->doc.script().nodes;
            std::string letzte;
            if (!ns.empty()) {
                letzte = ns.back().name + "/" + std::to_string(static_cast<int>(ns.back().kind)) + "/" +
                         ns.back().raw;
            }
            diag::detail("Knopftest: " + name + " - Knoten vorher " + std::to_string(*vorher) + ", nachher " +
                         std::to_string(kuerzelKnoten()) + ", ausgeloest " +
                         std::to_string(static_cast<int>(g_app->letzteKuerzelAktion)) + ", letzter Knoten " +
                         letzte + ", Auswahl " +
                         (g_app->selectedPath.empty() ? std::string("-")
                                                      : std::to_string(g_app->selectedPath.back())));
            return g_app->letzteKuerzelAktion == a && wirkung();
        }));
        if (a == keys::Action::Restore) {
            add(klick(tr(Str::AnswerNo)));
            add(pause(0.2));
            add(pruefe("Restore: Nein laesst das Skript stehen", [] {
                return !g_app->frageOffen && kuerzelKnoten() == 9U;
            }));
        }
        // --- Die alte Belegung loest nichts mehr aus ---------------------
        const ImGuiKeyChord alt = kuerzelVon(a) == neu ? (*vorgaben)[static_cast<int>(a)] : ImGuiKey_None;
        (void)alt;
        add(tu(name + ": aufraeumen", [] {
            g_app->findOpen = false;
            g_app->editorOpen = false;
            kuerzelSkriptZurueck(Path{2});
        }));
        add(pause(0.15));
        add({name + ": alte Belegung druecken", [=](int b) {
                 // Die Vorgabe dieser Aktion - erst beim Lauf bekannt.
                 static ImGuiKeyChord merke = ImGuiKey_None;
                 if (b == 0) {
                     merke = (*vorgaben)[static_cast<int>(a)];
                     g_app->letzteKuerzelAktion = keys::Action::Unhandled;
                     if (merke == ImGuiKey_None) { return true; }
                 }
                 ImGuiIO& io = ImGui::GetIO();
                 const auto key = static_cast<ImGuiKey>(merke & ~ImGuiMod_Mask_);
                 const bool strg = (merke & ImGuiMod_Ctrl) != 0;
                 const bool umsch = (merke & ImGuiMod_Shift) != 0;
                 const bool alt2 = (merke & ImGuiMod_Alt) != 0;
                 if (b == 0) {
                     if (strg) { io.AddKeyEvent(ImGuiMod_Ctrl, true); }
                     if (umsch) { io.AddKeyEvent(ImGuiMod_Shift, true); }
                     if (alt2) { io.AddKeyEvent(ImGuiMod_Alt, true); }
                     return false;
                 }
                 if (b == 1) { io.AddKeyEvent(key, true); return false; }
                 if (b == 2) { io.AddKeyEvent(key, false); return false; }
                 if (b == 3) {
                     if (strg) { io.AddKeyEvent(ImGuiMod_Ctrl, false); }
                     if (umsch) { io.AddKeyEvent(ImGuiMod_Shift, false); }
                     if (alt2) { io.AddKeyEvent(ImGuiMod_Alt, false); }
                     return false;
                 }
                 return b >= 6;
             }});
        add(pruefe(name + ": die alte Belegung loest es nicht mehr aus", [=] {
            return g_app->letzteKuerzelAktion != a;
        }));
        add(tu(name + ": aufraeumen", [] {
            g_app->findOpen = false;
            g_app->editorOpen = false;
            g_app->frageOffen = false;
        }));
    }

    // --- Reset all to defaults: alles wieder wie vorher -----------------
    add(tu("Kuerzelfenster nach vorn", [] { fensterZeigen(g_app->keysOpen, "###keys"); }));
    add(pause(0.3));
    add(tu("Kuerzelfenster ans Ende rollen", [] {
        if (ImGuiWindow* w = ImGui::FindWindowByName("###keys")) {
            ImGui::SetScrollY(w, w->ScrollMax.y + 1000.0F);
        }
    }));
    add(pause(0.3));
    add(klick(tr(Str::KeysReset), "###keys"));
    add(pause(0.2));
    add(pruefe("Reset all to defaults: JEDE Aktion hat wieder ihre Vorgabe", [=] {
        int anders = 0;
        for (const keys::Bindable& b : keys::bindable()) {
            if (kuerzelVon(b.action) != (*vorgaben)[static_cast<int>(b.action)]) {
                diag::detail(std::string("Knopftest: nach Reset anders: ") + b.name);
                ++anders;
            }
        }
        return anders == 0;
    }));
    add(tu("Kuerzeltest aufraeumen: Fenster zu, Dateien weg", [] {
        g_app->keysOpen = false;
        g_app->kuerzelNurMelden = false;
        g_app->doc.markSaved();
        std::error_code ec;
        std::filesystem::remove(kuerzelDatei(), ec);
        std::filesystem::remove(kuerzelBak(), ec);
        std::filesystem::remove(kuerzelDatei() + ".bak", ec);
    }));
    add(pause(0.3));
    return s;
}

// --- "Find similar" und "Highlight identical commands" ----------------------
// --- Wuensche vom 27.09. ------------------------------------------------------
//
// * Ein ANGEKLICKTER Block (affect, if, else, loop, task) nimmt neue Befehle
//   aus der Ereignisliste und aus der Ablage auf - ans Ende. Ein Block, der
//   nur angewaehlt ist, weil er gerade eingefuegt wurde, nimmt NICHTS auf
//   (die Klebrigkeit aus rc532 bleibt draussen).
// * Compiler-Meldungen in der eingestellten Sprache, mit dem Befehl davor, und
//   die schuldige Zeile wird angewaehlt ("! =" -> Hinweis auf "!").
// * "Last compile: <Datum Uhrzeit>" in der Statuszeile.
std::vector<Schritt> wunschSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    const auto befehl = [](const char* name) -> const Command* {
        for (const Command& c : g_app->db.commands) {
            if (c.name == name) { return &c; }
        }
        return nullptr;
    };
    const auto kinder = [](const Path& p) -> std::size_t {
        const Node* n = nodeAt(g_app->doc.script(), p);
        return n != nullptr ? n->children.size() : 0U;
    };
    add(tu("Wunsch: Skript mit einem affect-Block", [] {
        openScriptFromMemory("//Generated by BehavEd\n"
                             "affect ( \"blockA\", /*@AFFECT_TYPE*/ FLUSH )\n{\n\twait ( 100.000 );\n}\n"
                             "wait ( 200.000 );\n",
                             "wunsch.txt");
        g_app->expanded.openAll();   // hinein nur in einen AUFGEKLAPPTEN Block
        rebuildTree();
        g_app->selectedPath.clear();
        g_app->selection.clear();
        g_app->auswahlDurchEinfuegen.clear();
    }));
    add(pause(0.4));
    add(klick("affect", "##flowarea"));
    add(pause(0.2));
    add(pruefe("Wunsch: der Block ist per Klick angewaehlt", [] { return g_app->selectedPath == Path{0}; }));
    add(tu("Wunsch: print aus der Ereignisliste", [=] {
        if (const Command* c = befehl("print")) { fuegeBefehlEin(*c); }
    }));
    add(pause(0.2));
    add(pruefe("Wunsch: angeklickter, offener Block - der Befehl landet DARIN, am Anfang (wie das Original)", [=] {
        const Node* n = nodeAt(g_app->doc.script(), Path{0, 0});
        diag::detail("Knopftest: Wunsch - Auswahl " + std::to_string(g_app->selectedPath.size()) + " Ebenen, Kinder " +
                     std::to_string(kinder(Path{0})));
        return n != nullptr && n->name == "print" && g_app->selectedPath == (Path{0, 0});
    }));
    add(tu("Wunsch: noch ein wait", [=] {
        if (const Command* c = befehl("wait")) { fuegeBefehlEin(*c); }
    }));
    add(pause(0.2));
    add(pruefe("Wunsch: der naechste folgt dahinter, ebenfalls im Block", [=] {
        return kinder(Path{0}) == 3U && g_app->selectedPath == (Path{0, 1});
    }));
    // Die Klebrigkeit aus rc532 darf NICHT zurueckkommen.
    add(tu("Wunsch: das wait unten waehlen", [] { selectByPath(Path{1}); }));
    add(pause(0.2));
    add(tu("Wunsch: loop aus der Ereignisliste", [=] {
        if (const Command* c = befehl("loop")) { fuegeBefehlEin(*c); }
    }));
    add(tu("Wunsch: gleich danach ein wait", [=] {
        if (const Command* c = befehl("wait")) { fuegeBefehlEin(*c); }
    }));
    add(pause(0.2));
    add(pruefe("Wunsch: ein gerade eingefuegter Block nimmt den naechsten Befehl NICHT auf", [=] {
        std::string namen;
        for (const Node& n : g_app->doc.script().nodes) { namen += " " + (n.name.empty() ? std::string("-") : n.name); }
        diag::detail("Knopftest: Wunsch - oberste Ebene:" + namen);
        const Node* l = nodeAt(g_app->doc.script(), Path{2});
        const Node* w = nodeAt(g_app->doc.script(), Path{3});
        return l != nullptr && l->name == "loop" && l->children.empty() && w != nullptr && w->name == "wait";
    }));
    // Einfuegen aus der Ablage in einen angeklickten Block.
    add(tu("Wunsch: ein wait in die Ablage", [] {
        Node n;
        n.kind = Node::Kind::Command;
        n.name = "wait";
        Arg a;
        a.kind = Arg::Kind::Number;
        a.text = "300.000";
        n.args.push_back(a);
        g_app->doc.setClipboard(std::vector<Node>{n});
        g_app->ablageText.clear();
        ImGui::SetClipboardText("");
    }));
    add(klick("affect", "##flowarea"));
    add(pause(0.2));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_V, "Strg+V"));
    add(pause(0.3));
    add(pruefe("Wunsch: Einfuegen in den angeklickten Block - an den Anfang", [=] {
        const Node* n = nodeAt(g_app->doc.script(), Path{0, 0});
        diag::detail("Knopftest: Wunsch - affect hat " + std::to_string(kinder(Path{0})) + " Kinder");
        return kinder(Path{0}) == 4U && n != nullptr && n->name == "wait" && !n->args.empty() &&
               n->args[0].text == "300.000";
    }));

    // --- Compiler-Meldung: "! =" ---------------------------------------------
    add(tu("Wunsch: Skript mit if ( ..., ! =, ... )", [] {
        openScriptFromMemory("//Generated by BehavEd\n"
                             "if ( $get( FLOAT, \"SET_PARM1\")$, $! =$, $0$ )\n{\n\tprint ( \"a\" );\n}\n",
                             "wunsch2.txt");
        g_app->selectedPath.clear();
        g_app->selection.clear();
        g_app->statusLines.clear();
    }));
    add(pause(0.4));
    add(klick(tr(Str::Compile)));
    add(pause(0.4));
    add(pruefe("Wunsch: die Meldung nennt den Befehl und sagt, was gilt (Sprache der Oberflaeche)", [] {
        char soll[300];
        std::snprintf(soll, sizeof(soll), tr(Str::CmsgC004), "! =");
        // Die Statuszeilen tragen vorn ihren Zaehler "(n) : ".
        const std::string erwartet = std::string(") : if : ") + soll;
        bool da = false;
        for (const std::string& z : g_app->statusLines) {
            diag::detail("Knopftest: Wunsch - Status \"" + z + "\"");
            if (z.size() >= erwartet.size() &&
                z.compare(z.size() - erwartet.size(), erwartet.size(), erwartet) == 0) { da = true; }
        }
        return da;
    }));
    add(pruefe("Wunsch: nach dem Fehlschlag ist die schuldige Zeile angewaehlt", [] {
        return g_app->selectedPath == Path{0};
    }));

    // --- "Last compile" in der Statuszeile -----------------------------------
    auto datei = std::make_shared<std::string>(platform::executableDirectory() + "\\selbsttest_zeit.txt");
    add(tu("Wunsch: Skript mit Pfad (im Programmordner) laden", [=] {
        std::remove((platform::executableDirectory() + "\\selbsttest_zeit.ibi").c_str());
        std::ofstream f(*datei, std::ios::binary);
        f << "//Generated by BehavEd\r\n\r\nwait ( 100.000 );\r\n";
        f.close();
        ladeSkriptDatei(*datei);
    }));
    // 2,5 s: nach dem missglueckten Kompilieren oben zeigt der Knopf zwei
    // Sekunden lang "Compile failed" statt "Compile!".
    add(pause(2.5));
    add(pruefe("Wunsch: ohne .ibi steht keine Zeit da", [] { return g_app->letzteKompilierung.empty(); }));
    add(klick(tr(Str::Compile)));
    add(pause(1.5));
    add(pruefe("Wunsch: nach dem Kompilieren steht \"Last compile\" mit heutigem Datum unten", [] {
        const std::time_t jetzt = std::time(nullptr);
        std::tm lokal{};
        (void)localtime_s(&lokal, &jetzt);
        char heute[16];
        std::strftime(heute, sizeof(heute), "%Y-%m-%d", &lokal);
        diag::detail("Knopftest: Wunsch - Statuszeile \"" + g_app->letzteKompilierung + "\"");
        return g_app->letzteKompilierung.find(heute) != std::string::npos;
    }));
    add(tu("Wunsch: Dateien aufraeumen", [=] {
        std::remove(datei->c_str());
        std::remove((platform::executableDirectory() + "\\selbsttest_zeit.ibi").c_str());
    }));
    // Die Hervorhebung gleicher Befehle - fuers Auge (BHED_FOTOS): sie soll
    // erst am Namen beginnen, nicht unter dem Symbol, und blass sein.
    add(tu("Wunsch: wiederholte Kameras, Hervorhebung an", [] {
        openScriptFromMemory(std::string("//Generated by BehavEd\n") + kKuerzelSkript, "hervorhebung.txt");
        g_app->settings.highlightSame = true;
        selectByPath(Path{1});
    }));
    add(pause(0.5));
    add(foto("gleiche_hervorhebung"));
    add(tu("Wunsch: Hervorhebung wieder aus", [] { g_app->settings.highlightSame = false; }));
    return s;
}

// --- Ziehen im Script Flow: drei Zonen (27.09.) ------------------------------
//
// shank: "kannst du dieselbe Logik auch machen ... wenn man im Script Flow
// selbst rumschiebt? entweder darunter, darueber oder darauf". Echte
// Mauszuege auf das obere Drittel (davor), die Mitte (hinein bei einem Block,
// sonst dahinter) und das untere Drittel (dahinter; unter einem offenen Block
// mit Inhalt: an seinen Anfang).
std::string baumFlach() {
    std::string out;
    const std::function<void(const std::vector<Node>&)> geh = [&](const std::vector<Node>& ns) {
        for (const Node& n : ns) {
            if (n.kind == Node::Kind::Macro) {
                if (!out.empty()) { out += " "; }
                out += "$" + n.name + "@" + std::to_string(n.count);
                continue;
            }
            if (n.kind != Node::Kind::Command) { continue; }
            if (!out.empty()) { out += " "; }
            out += n.name;
            if (n.hasBlock) {
                out += "{";
                geh(n.children);
                out += " }";
            }
        }
    };
    geh(g_app->doc.script().nodes);
    return out;
}

std::vector<Schritt> zonenSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    add(tu("Zonen: Skript laden, alles aufgeklappt", [] {
        openScriptFromMemory("//Generated by BehavEd\n"
                             "affect ( \"blockA\", /*@AFFECT_TYPE*/ FLUSH )\n{\n"
                             "\tprint ( \"p\" );\n\tkill ( \"k\" );\n}\n"
                             "signal ( \"s\" );\nremove ( \"r\" );\n",
                             "zonen.txt");
        g_app->expanded.openAll();
        rebuildTree();
        g_app->selectedPath.clear();
        g_app->selection.clear();
    }));
    add(pause(0.4));
    const float d = ImGui::GetFontSize() * 0.42F;   // gut ins obere/untere Drittel
    const auto zug = [&](const std::string& von, const std::string& nach, float dy, const std::string& soll,
                         const std::string& was) {
        add(ziehe(von, "##flowarea", nach, "##flowarea", 0, 0, 0.5F, true, ImVec2{0.0F, dy}));
        add(pause(0.3));
        add(pruefe("Zonen: " + was, [=] {
            const std::string ist = baumFlach();
            diag::detail("Knopftest: Zonen - " + was + ": " + ist);
            return ist == soll;
        }));
    };
    zug("signal", "affect", -d, "signal affect{ print kill } remove", "oben auf dem Block = DAVOR");
    zug("signal", "affect", 0.0F, "affect{ signal print kill } remove", "Mitte des Blocks = HINEIN (an den Anfang)");
    zug("signal", "kill", d, "affect{ print kill signal } remove", "unten auf einer Zeile im Block = DAHINTER");
    zug("remove", "kill", 0.0F, "affect{ print kill remove signal }", "Mitte einer Zeile ohne Block = DAHINTER");
    zug("kill", "print", -d, "affect{ kill print remove signal }", "oben auf einer Zeile = DAVOR");
    zug("signal", "affect", d, "affect{ signal kill print remove }",
        "unten an einem OFFENEN Block mit Inhalt = an seinen Anfang (da steht die Linie)");
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.2));
    add(pruefe("Zonen: Strg+Z nimmt den letzten Zug zurueck", [] {
        return baumFlach() == "affect{ kill print remove signal }";
    }));

    // Makros (standOnly & Co.): im Original ein Behaelter wie ein Block -
    // "wenn man es auf das Plus zieht, wird es reingesetzt, und zwar ganz
    // oben" (shank, am Original getestet).
    add(tu("Zonen: Skript mit einem Makro laden", [] {
        openScriptFromMemory("//Generated by BehavEd\n"
                             "signal ( \"s\" );\n//$\"standOnly\"@2\nset ( \"a\", \"1\" );\nset ( \"b\", \"1\" );\n"
                             "remove ( \"r\" );\n",
                             "zonen_makro.txt");
        g_app->expanded.openAll();
        rebuildTree();
        g_app->selectedPath.clear();
        g_app->selection.clear();
    }));
    add(pause(0.4));
    zug("remove", "standOnly", 0.0F, "signal $standOnly@3 remove set set",
        "Mitte der Makrozeile = HINEIN, ganz oben (das Makro zaehlt mit)");
    zug("remove", "standOnly", -d, "signal remove $standOnly@2 set set", "oben auf der Makrozeile = DAVOR (wieder heraus)");
    zug("signal", "standOnly", d, "remove $standOnly@3 signal set set",
        "unten an einem OFFENEN Makro mit Inhalt = an seinen Anfang");
    zug("standOnly", "remove", -d, "$standOnly@3 signal set set remove",
        "die Makrozeile zieht ihre Befehle mit");
    // Hinzufuegen aus der Ereignisliste: offenes Makro angeklickt = hinein,
    // ganz oben; zugeklapptes = dahinter (am Original nachgesehen).
    const auto befehl = [](const char* name) -> const Command* {
        for (const Command& c : g_app->db.commands) {
            if (c.name == name) { return &c; }
        }
        return nullptr;
    };
    add(pause(0.4));
    add(tu("Zonen: die Makrozeile anwaehlen", [] {
        g_app->auswahlDurchEinfuegen.clear();
        selectByPath(Path{0});
    }));
    add(pause(0.2));
    add(tu("Zonen: print hinzufuegen (Makro offen)", [=] {
        g_app->doc.vergibKennungen();
        if (const Node* m = nodeAt(g_app->doc.script(), g_app->selectedPath)) { g_app->expanded.setOpen(m->kennung, true); }
        if (const Command* c = befehl("print")) { fuegeBefehlEin(*c); }
    }));
    add(pause(0.2));
    add(pruefe("Zonen: offenes Makro angeklickt - Hinzufuegen setzt hinein, ganz oben", [] {
        diag::detail("Knopftest: Zonen - Hinzufuegen offen: " + baumFlach());
        return baumFlach() == "$standOnly@4 print signal set set remove" && g_app->selectedPath == Path{1};
    }));
    add(klick("standOnly", "##flowarea"));
    add(pause(0.2));
    add(tu("Zonen: Makro zuklappen, kill hinzufuegen", [=] {
        g_app->doc.vergibKennungen();
        if (const Node* m = nodeAt(g_app->doc.script(), g_app->selectedPath)) { g_app->expanded.setOpen(m->kennung, false); }
        rebuildTree();
        if (const Command* c = befehl("kill")) { fuegeBefehlEin(*c); }
    }));
    add(pause(0.2));
    add(pruefe("Zonen: zugeklapptes Makro angeklickt - Hinzufuegen setzt DAHINTER", [] {
        diag::detail("Knopftest: Zonen - Hinzufuegen zu: " + baumFlach());
        return baumFlach() == "$standOnly@4 print signal set set kill remove";
    }));
    return s;
}

// --- "Revert to original" im Rechtsklickmenue (27.09.) --------------------------
std::vector<Schritt> revertSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    const auto zeileVon = [](const Path& p) -> int {
        for (std::size_t i = 0; i < g_app->rows.size(); ++i) {
            if (g_app->rows[i].path == p) { return static_cast<int>(i); }
        }
        return -1;
    };
    add(tu("Revert: Skript laden", [] {
        openScriptFromMemory("//Generated by BehavEd\nwait ( 100.000 );\nwait ( 200.000 );\nprint ( \"p\" );\n",
                             "revert.txt");
        g_app->selectedPath.clear();
        g_app->selection.clear();
    }));
    add(pause(0.4));
    add(tu("Revert: das zweite wait aendern (wie im Editor)", [] {
        if (const Node* n = nodeAt(g_app->doc.script(), Path{1})) {
            Node neu = *n;
            neu.args[0].text = "999.000";
            (void)g_app->doc.replaceAt(Path{1}, neu);
            rebuildTree();
        }
    }));
    add(pause(0.3));
    add(pruefe("Revert: der geaenderte Befehl ist orange markiert", [=] {
        const int z = zeileVon(Path{1});
        return z >= 0 && static_cast<std::size_t>(z) < g_app->zeilenMarke.size() &&
               g_app->zeilenMarke[static_cast<std::size_t>(z)] == kMarkeGeaendert;
    }));
    add(pruefe("Revert: ein unveraenderter Befehl bietet nichts zum Zuruecksetzen", [] {
        return zuruecksetzbar({Path{2}}).empty() && zuruecksetzbar({Path{0}}).empty();
    }));
    // --- Die Vorschau: was war das Original? ----------------------------
    add(pruefe("Revert-Vorschau: der geaenderte Befehl zeigt sein Original (200)", [] {
        const std::string o = originalZeile(Path{1});
        diag::detail("Knopftest: Revert-Vorschau - \"" + o + "\"");
        return o.find("wait") != std::string::npos && o.find("200") != std::string::npos &&
               o.find("999") == std::string::npos;
    }));
    add(pruefe("Revert-Vorschau: ein unveraenderter Befehl zeigt nichts", [] {
        return originalZeile(Path{0}).empty() && originalZeile(Path{2}).empty();
    }));
    add(tu("Revert-Vorschau: Maus auf den Rand der geaenderten Zeile", [=] {
        g_app->randVorschauZeile = -1;
        const int z = zeileVon(Path{1});
        if (z >= 0 && static_cast<std::size_t>(z) < g_app->randMitte.size()) {
            setzeMaus(g_app->randMitte[static_cast<std::size_t>(z)]);
        }
    }));
    add(pause(0.8));
    add(foto("revert_vorschau_rand"));
    add(pruefe("Revert-Vorschau: Hovern ueber dem Rand zeigt den Hinweis mit dem Original", [=] {
        return g_app->randVorschauZeile == zeileVon(Path{1}) && g_app->vorschauGezeichnet > 0;
    }));
    add(tu("Revert-Vorschau: Maus weg, Zaehler zuruecksetzen", [] {
        setzeMaus(ImVec2{5.0F, 5.0F});
        g_app->vorschauGezeichnet = 0;
    }));
    add(pause(0.2));
    add(klick("wait", "##flowarea", 1, false, "", 1));   // Rechtsklick auf das zweite wait
    add(pause(0.3));
    add(foto("revert_vorschau_menue"));
    add(pruefe("Revert-Vorschau: das Rechtsklickmenue zeigt das Original unter \"Revert to original\"", [] {
        return g_app->vorschauGezeichnet > 0;
    }));
    add(klick(tr(Str::ActRevertOriginal), "##Popup"));
    add(pause(0.3));
    add(pruefe("Revert: der Befehl steht wieder wie beim Oeffnen, ohne Marke", [=] {
        const Node* n = nodeAt(g_app->doc.script(), Path{1});
        const int z = zeileVon(Path{1});
        const bool ohneMarke = z >= 0 && static_cast<std::size_t>(z) < g_app->zeilenMarke.size() &&
                               g_app->zeilenMarke[static_cast<std::size_t>(z)] == kMarkeKeine;
        diag::detail("Knopftest: Revert - jetzt " + (n != nullptr && !n->args.empty() ? n->args[0].text : "?") +
                     ", Marke " + (z >= 0 ? std::to_string(g_app->zeilenMarke[static_cast<std::size_t>(z)]) : "?"));
        return n != nullptr && !n->args.empty() && n->args[0].text == "200.000" && ohneMarke;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.2));
    add(pruefe("Revert: Strg+Z holt die Aenderung zurueck (ein Schritt)", [] {
        const Node* n = nodeAt(g_app->doc.script(), Path{1});
        return n != nullptr && !n->args.empty() && n->args[0].text == "999.000";
    }));

    // Blau: geaendert, GESPEICHERT (gruen), dann zurueck auf den Stand beim
    // Oeffnen - so wie in Notepad++. Ohne das Speichern dazwischen gibt es
    // kein Blau: dann ist die Zeile einfach wieder wie die Datei.
    auto datei = std::make_shared<std::string>(platform::executableDirectory() + "\\selbsttest_revert.txt");
    add(tu("Revert: Skript mit Pfad laden", [=] {
        std::ofstream f(*datei, std::ios::binary);
        f << "//Generated by BehavEd\r\n\r\nwait ( 100.000 );\r\nwait ( 200.000 );\r\n";
        f.close();
        ladeSkriptDatei(*datei);
    }));
    add(pause(0.5));
    // Die Datei beginnt mit einer Leerzeile - also den Weg des ZWEITEN wait
    // suchen, statt ihn anzunehmen.
    auto zweites = std::make_shared<Path>();
    add(tu("Revert: das zweite wait aendern und speichern", [=] {
        int n = 0;
        for (std::size_t i = 0; i < g_app->doc.script().nodes.size(); ++i) {
            if (g_app->doc.script().nodes[i].name == "wait" && ++n == 2) { *zweites = Path{i}; }
        }
        if (const Node* k = nodeAt(g_app->doc.script(), *zweites)) {
            Node neu = *k;
            neu.args[0].text = "999.000";
            (void)g_app->doc.replaceAt(*zweites, neu);
            rebuildTree();
        }
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_S, "Strg+S"));
    add(pause(0.4));
    add(pruefe("Revert: nach dem Speichern ist die Zeile gruen", [=] {
        const int z = zeileVon(*zweites);
        return z >= 0 && g_app->zeilenMarke[static_cast<std::size_t>(z)] == kMarkeGespeichert;
    }));
    add(klick("wait", "##flowarea", 1, false, "", 1));
    add(pause(0.3));
    add(klick(tr(Str::ActRevertOriginal), "##Popup"));
    add(pause(0.3));
    add(pruefe("Revert: zurueckgesetzt nach dem Speichern = BLAU (wieder wie beim Oeffnen)", [=] {
        const int z = zeileVon(*zweites);
        diag::detail("Knopftest: Revert - Marke nach Speichern und Zuruecksetzen " +
                     (z >= 0 ? std::to_string(g_app->zeilenMarke[static_cast<std::size_t>(z)]) : "?"));
        return z >= 0 && g_app->zeilenMarke[static_cast<std::size_t>(z)] == kMarkeZurueck;
    }));
    add(tu("Revert: aufraeumen", [=] {
        g_app->doc.markSaved();   // keine Speichernfrage beim Beenden
        std::remove(datei->c_str());
    }));
    return s;
}

// --- Der Auto-Updater gegen das echte GitHub (BHED_EDITORTEST=update) -------
//
// Nur aus einer KOPIE des Programmordners starten: der Test installiert
// wirklich (mit BHED_UPDATE_LOKAL=rc500 gilt das aktuelle Release als neuer).
// Neu gestartet wird nicht.
std::vector<Schritt> updateSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    add(tu("Update: nachsehen", [] { updater::pruefen(false); }));
    add(warteBis("GitHub hat geantwortet", [] { return updater::zustand().stand != updater::Stand::Pruefe; }, 3000));
    add(pause(0.3));
    add(foto("update_gefunden"));
    add(pruefe("Update: ein neueres Release gefunden (gegen BHED_UPDATE_LOKAL)", [] {
        const updater::Zustand z = updater::zustand();
        diag::detail("Knopftest: Update - Stand " + std::to_string(static_cast<int>(z.stand)) + ", Release " +
                     z.release.tag + ", Meldung " + z.meldung + ", " + std::to_string(z.release.assets.size()) +
                     " Dateien");
        return z.stand == updater::Stand::Verfuegbar && !z.release.tag.empty() &&
               bhed::update::zipAsset(z.release) != nullptr;
    }));
    add(tu("Update: installieren", [] { updater::installieren(); }));
    add(warteBis("Update installiert", [] {
        const auto st = updater::zustand().stand;
        return st == updater::Stand::Fertig || st == updater::Stand::Fehler;
    }, 6000));
    add(pause(0.3));
    add(foto("update_fertig"));
    add(pruefe("Update: installiert, die alte .exe beiseitegelegt, data/ da", [] {
        const updater::Zustand z = updater::zustand();
        const std::string ordner = platform::executableDirectory();
        const bool alt = std::filesystem::exists(std::filesystem::u8path(ordner + "/behaved.exe.alt"));
        const bool exe = std::filesystem::exists(std::filesystem::u8path(ordner + "/behaved.exe"));
        const bool daten = std::filesystem::exists(std::filesystem::u8path(ordner + "/data/base/anims.h"));
        diag::detail("Knopftest: Update - Stand " + std::to_string(static_cast<int>(z.stand)) + ", " +
                     std::to_string(z.dateien) + " Dateien, alt " + (alt ? "ja" : "nein") + ", exe " +
                     (exe ? "ja" : "nein") + ", data " + (daten ? "ja" : "nein") + ", Meldung " + z.meldung);
        return z.stand == updater::Stand::Fertig && z.dateien > 0 && alt && exe && daten;
    }));
    return s;
}

// --- Undo: Meldung unten, Sprung zum Befehl, Undo-Liste wie in 3ds Max ----
// --- Reiter schliessen darf die Arbeit im aktiven Reiter nicht verlieren ---
//
// shank, 02.10.: "I closed some scripts/tabs I was no longer using and went
// back to continue working on the script but it had reset. All my changes
// were lost with no undo/redo history." Nachgestellt: drei Reiter, im
// mittleren bearbeiten, dann einen anderen (unveraenderten) schliessen -
// links und rechts davon.
std::vector<Schritt> reiterSchliessenSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    const auto findeReiter = [](const std::string& name) {
        // Der aktive Reiter lebt in g_app, nicht in seiner geparkten Kopie.
        for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
            const std::string& n = (static_cast<int>(i) == g_app->activeTab) ? g_app->shownName
                                                                             : g_app->tabs[i].shownName;
            if (n == name) { return static_cast<int>(i); }
        }
        return -1;
    };
    const auto aktivName = [] { return g_app->shownName; };
    const auto aendern = [](const std::string& wert) {
        if (const Node* n = nodeAt(g_app->doc.script(), Path{0}); n != nullptr && !n->args.empty()) {
            Node neu = *n;
            neu.args[0].text = wert;
            (void)g_app->doc.replaceAt(Path{0}, neu);
            rebuildTree();
        }
    };
    const auto wertJetzt = [] {
        const Node* n = nodeAt(g_app->doc.script(), Path{0});
        return (n != nullptr && !n->args.empty()) ? n->args[0].text : std::string("?");
    };
    for (int richtung = 0; richtung < 2; ++richtung) {
        const std::string z = std::to_string(richtung);
        const std::string links = "reiter_links" + z + ".txt";
        const std::string mitte = "reiter_mitte" + z + ".txt";
        const std::string rechts = "reiter_rechts" + z + ".txt";
        add(tu("Reiter " + z + ": drei Skripte oeffnen, im mittleren arbeiten", [=] {
            openScriptFromMemory("//Generated by BehavEd\nwait ( 1.000 );\n", links);
            openScriptFromMemory("//Generated by BehavEd\nwait ( 2.000 );\n", mitte);
            openScriptFromMemory("//Generated by BehavEd\nwait ( 3.000 );\n", rechts);
            std::string liste;
            for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
                liste += " [" + std::to_string(i) + "]" +
                         (static_cast<int>(i) == g_app->activeTab ? "*" + g_app->shownName : g_app->tabs[i].shownName);
            }
            diag::detail("Knopftest: Reiter - Liste" + liste + ", mitte bei " + std::to_string(findeReiter(mitte)));
            activateTab(findeReiter(mitte));
            diag::detail("Knopftest: Reiter - nach activateTab aktiv " + std::to_string(g_app->activeTab) + " " +
                         g_app->shownName);
        }));
        add(pause(0.3));
        add(tu("Reiter " + z + ": den mittleren zweimal aendern", [=] {
            aendern("222.000");
            aendern("333.000");
        }));
        add(pause(0.3));
        add(tu("Reiter " + z + ": einen ANDEREN, unveraenderten Reiter schliessen", [=] {
            const int weg = findeReiter(richtung == 0 ? links : rechts);
            diag::detail("Knopftest: Reiter - schliesse " + (richtung == 0 ? links : rechts) + " (" +
                         std::to_string(weg) + "), aktiv " + std::to_string(g_app->activeTab) + " " + aktivName());
            reiterSchliessen(weg);
        }));
        add(pause(0.3));
        add(pruefe("Reiter " + z + ": der aktive Reiter ist noch derselbe, mit beiden Aenderungen und Undo", [=] {
            diag::detail("Knopftest: Reiter - danach aktiv " + aktivName() + ", Wert " + wertJetzt() + ", Undo " +
                         std::to_string(g_app->doc.undoDepth()));
            return aktivName() == mitte && wertJetzt() == "333.000" && g_app->doc.undoDepth() == 2;
        }));
        add(tu("Reiter " + z + ": weg- und zurueckwechseln", [=] {
            activateTab(findeReiter(richtung == 0 ? rechts : links));
            activateTab(findeReiter(mitte));
        }));
        add(pause(0.3));
        add(pruefe("Reiter " + z + ": nach dem Hin- und Herwechseln immer noch alles da", [=] {
            return aktivName() == mitte && wertJetzt() == "333.000" && g_app->doc.undoDepth() == 2;
        }));
        add(tu("Reiter " + z + ": aufraeumen", [=] {
            for (const std::string& n : {links, mitte, rechts}) {
                const int i = findeReiter(n);
                if (i >= 0) {
                    g_app->tabs[static_cast<std::size_t>(i)].doc.markSaved();
                    if (i == g_app->activeTab) { g_app->doc.markSaved(); }
                }
            }
            for (const std::string& n : {links, mitte, rechts}) {
                const int i = findeReiter(n);
                if (i >= 0) { reiterSchliessen(i); }
            }
        }));
        add(pause(0.3));
    }

    // ---- shank, 03.10.: "it was like I just opened the file again" ---------
    //
    // (1) Dieselbe Datei noch einmal oeffnen darf keinen zweiten Reiter mit
    //     dem Stand von der Platte anlegen.
    auto datei = std::make_shared<std::string>(platform::executableDirectory() + "/selbsttest_doppelt.txt");
    auto reiterVorher = std::make_shared<std::size_t>(0);
    add(tu("Doppelt: Datei schreiben, oeffnen, zweimal aendern", [=] {
        std::ofstream f(std::filesystem::u8path(*datei), std::ios::binary);
        f << "//Generated by BehavEd\r\nwait ( 7.000 );\r\n";
        f.close();
        ladeSkriptDatei(*datei);
        aendern("71.000");
        aendern("72.000");
        *reiterVorher = g_app->tabs.size();
    }));
    add(pause(0.3));
    add(tu("Doppelt: weg in einen anderen Reiter, dann dieselbe Datei noch einmal oeffnen", [=] {
        openScriptFromMemory("//Generated by BehavEd\nwait ( 8.000 );\n", "doppelt_anderer.txt");
        ladeSkriptDatei(*datei);
    }));
    add(pause(0.3));
    add(pruefe("Doppelt: kein zweiter Reiter, zurueck im alten - mit Aenderungen und Undo", [=] {
        diag::detail("Knopftest: Doppelt - Reiter " + std::to_string(g_app->tabs.size()) + " (vorher " +
                     std::to_string(*reiterVorher) + " + 1 anderer), Wert " + wertJetzt() + ", Undo " +
                     std::to_string(g_app->doc.undoDepth()) + ", Pfad " + g_app->path);
        return g_app->tabs.size() == *reiterVorher + 1 && wertJetzt() == "72.000" &&
               g_app->doc.undoDepth() == 2 && g_app->homeTab == g_app->activeTab;
    }));
    // (2) Ein Skript aus der Mission (Archiv) zum zweiten Mal laden.
    add(tu("Doppelt: Mission-Skript laden und aendern, dann noch einmal laden", [=] {
        oeffneSkriptAusSpeicher("//Generated by BehavEd\nwait ( 9.000 );\n", "md_selbst/doppelt_mission.txt");
        aendern("91.000");
        activateTab(findeReiter("doppelt_anderer.txt"));
        oeffneSkriptAusSpeicher("//Generated by BehavEd\nwait ( 9.000 );\n", "md_selbst/doppelt_mission.txt");
    }));
    add(pause(0.3));
    add(pruefe("Doppelt: Mission-Skript nur einmal offen, Aenderung noch da", [=] {
        int anzahl = 0;
        for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
            const std::string& n = (static_cast<int>(i) == g_app->activeTab) ? g_app->shownName
                                                                             : g_app->tabs[i].shownName;
            if (n == "doppelt_mission.txt") { ++anzahl; }
        }
        diag::detail("Knopftest: Doppelt - Mission-Skript " + std::to_string(anzahl) + " mal offen, aktiv " +
                     g_app->shownName + ", Wert " + wertJetzt());
        return anzahl == 1 && g_app->shownName == "doppelt_mission.txt" && wertJetzt() == "91.000" &&
               g_app->doc.undoDepth() == 1;
    }));

    // (3) Einen ANDEREN, UNGESICHERTEN Reiter schliessen und "Nein" sagen:
    //     danach muss man wieder im Arbeitsreiter stehen, nicht im Nachbarn.
    for (int antwort = 0; antwort < 2; ++antwort) {
        const std::string z = std::to_string(antwort);
        const std::string arbeit = "arbeit" + z + ".txt";
        const std::string fremd = "fremd" + z + ".txt";
        const std::string nachbar = "nachbar" + z + ".txt";
        add(tu("Ungesichert " + z + ": drei Reiter, der mittlere wird geaendert", [=] {
            openScriptFromMemory("//Generated by BehavEd\nwait ( 1.000 );\n", arbeit);
            openScriptFromMemory("//Generated by BehavEd\nwait ( 2.000 );\n", fremd);
            aendern("22.000");
            openScriptFromMemory("//Generated by BehavEd\nwait ( 3.000 );\n", nachbar);
            activateTab(findeReiter(arbeit));
            aendern("11.000");
            aendern("12.000");
        }));
        add(pause(0.3));
        add(tu("Ungesichert " + z + ": den geaenderten fremden Reiter schliessen", [=] {
            reiterSchliessen(findeReiter(fremd));
        }));
        add(pause(0.3));
        add(pruefe("Ungesichert " + z + ": die Frage kommt", [] { return g_app->askSaveOpen; }));
        add(klick(tr(antwort == 0 ? Str::AnswerNo : Str::EditorCancel), tr(Str::AppTitle)));
        add(pause(0.3));
        add(pruefe(std::string("Ungesichert ") + z + (antwort == 0 ? ": nach Nein" : ": nach Abbrechen") +
                       " wieder im Arbeitsreiter, mit beiden Aenderungen und Undo",
                   [=] {
                       diag::detail("Knopftest: Ungesichert - aktiv " + g_app->shownName + " (" +
                                    std::to_string(g_app->activeTab) + "), Band " + std::to_string(g_app->homeTab) +
                                    ", Wert " + wertJetzt() + ", Undo " + std::to_string(g_app->doc.undoDepth()) +
                                    ", fremder noch offen: " + (findeReiter(fremd) >= 0 ? "ja" : "nein"));
                       const bool fremdOffen = findeReiter(fremd) >= 0;
                       return g_app->shownName == arbeit && wertJetzt() == "12.000" &&
                              g_app->doc.undoDepth() == 2 && g_app->homeTab == g_app->activeTab &&
                              fremdOffen == (antwort == 1);
                   }));
        add(tu("Ungesichert " + z + ": aufraeumen", [=] {
            for (const std::string& n : {arbeit, fremd, nachbar}) {
                const int i = findeReiter(n);
                if (i >= 0) {
                    g_app->tabs[static_cast<std::size_t>(i)].doc.markSaved();
                    if (i == g_app->activeTab) { g_app->doc.markSaved(); }
                }
            }
            for (const std::string& n : {arbeit, fremd, nachbar}) {
                const int i = findeReiter(n);
                if (i >= 0) { reiterSchliessen(i); }
            }
        }));
        add(pause(0.3));
    }
    add(tu("Doppelt: aufraeumen", [=] {
        for (const std::string& n : {std::string("selbsttest_doppelt.txt"), std::string("doppelt_anderer.txt"),
                                     std::string("doppelt_mission.txt")}) {
            for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
                const bool aktiv = static_cast<int>(i) == g_app->activeTab;
                const std::string& pfad = aktiv ? g_app->path : g_app->tabs[i].path;
                const std::string& nm = aktiv ? g_app->shownName : g_app->tabs[i].shownName;
                if (nm == n || (!pfad.empty() && pfad.find(n) != std::string::npos)) {
                    g_app->tabs[i].doc.markSaved();
                    if (aktiv) { g_app->doc.markSaved(); }
                    reiterSchliessen(static_cast<int>(i));
                    break;
                }
            }
        }
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(*datei), ec);
    }));
    add(pause(0.3));

    // ======== Code-Pruefung 03.10.: weitere Wege zum Datenverlust ==========
    const auto befehl = [](const char* name) -> const Command* {
        for (const Command& c : g_app->db.commands) {
            if (c.name == name) { return &c; }
        }
        return nullptr;
    };
    const auto knotenZahl = [] { return g_app->doc.script().nodes.size(); };

    // (4) Speichern, Strg+Z, eine ANDERE Aenderung: das Skript ist
    //     ungesichert - Stern, und Schliessen fragt nach.
    auto dirtyDatei = std::make_shared<std::string>(platform::executableDirectory() + "/selbsttest_stern.txt");
    add(tu("Stern: Datei oeffnen, print einfuegen", [=] {
        std::ofstream f(std::filesystem::u8path(*dirtyDatei), std::ios::binary);
        f << "//Generated by BehavEd\r\nwait ( 5.000 );\r\n";
        f.close();
        ladeSkriptDatei(*dirtyDatei);
        if (const Command* c = befehl("print")) { fuegeBefehlEin(*c); }
        diag::detail("Knopftest: Stern - nach print " + std::to_string(knotenZahl()) + " Befehle, ungesichert " +
                     (g_app->doc.dirty() ? "ja" : "nein"));
    }));
    add(pause(0.3));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_S, "Strg+S"));
    add(pause(0.3));
    add(pruefe("Stern: nach Strg+S gesichert", [] { return !g_app->doc.dirty(); }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.3));
    add(tu("Stern: statt des print den wait-Wert aendern (andere Aenderung)", [=] {
        aendern("55.000");
        diag::detail("Knopftest: Stern - jetzt " + std::to_string(knotenZahl()) + " Befehle, Wert " + wertJetzt() +
                     ", Undo " + std::to_string(g_app->doc.undoDepth()) + ", ungesichert " +
                     (g_app->doc.dirty() ? "ja" : "nein"));
    }));
    add(pause(0.3));
    add(pruefe("Stern: Speichern - Strg+Z - andere Aenderung gilt als UNGESICHERT", [] {
        return g_app->doc.dirty();
    }));
    add(tu("Stern: den Reiter schliessen (wie das X)", [] { reiterSchliessen(g_app->activeTab); }));
    add(pause(0.3));
    add(pruefe("Stern: Schliessen fragt nach, statt die Aenderung wegzuwerfen", [] { return g_app->askSaveOpen; }));
    add(tu("Stern: falls gefragt - Abbrechen", [] {
        if (g_app->askSaveOpen) {
            schritteEinfuegen({klick(tr(Str::EditorCancel), tr(Str::AppTitle)), pause(0.3)});
        }
    }));
    add(pruefe("Stern: das Skript ist noch offen, mit der Aenderung", [=] {
        const int i = findeReiter("selbsttest_stern.txt");
        bool offen = false;
        for (std::size_t k = 0; k < g_app->tabs.size(); ++k) {
            const bool aktiv = static_cast<int>(k) == g_app->activeTab;
            const std::string& pfad = aktiv ? g_app->path : g_app->tabs[k].path;
            if (pfad.find("selbsttest_stern.txt") != std::string::npos) { offen = true; }
        }
        diag::detail(std::string("Knopftest: Stern - noch offen: ") + (offen ? "ja" : "nein") + " (" +
                     std::to_string(i) + "), Wert " + wertJetzt());
        return offen && wertJetzt() == "55.000";
    }));
    add(tu("Stern: aufraeumen", [=] {
        for (std::size_t k = 0; k < g_app->tabs.size(); ++k) {
            const bool aktiv = static_cast<int>(k) == g_app->activeTab;
            const std::string& pfad = aktiv ? g_app->path : g_app->tabs[k].path;
            if (pfad.find("selbsttest_stern.txt") != std::string::npos) {
                g_app->tabs[k].doc.markSaved();
                if (aktiv) { g_app->doc.markSaved(); }
                reiterSchliessen(static_cast<int>(k));
                break;
            }
        }
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(*dirtyDatei), ec);
        std::filesystem::remove_all(
            std::filesystem::u8path(platform::executableDirectory() + "/backup/selbsttest_stern"), ec);
    }));
    add(pause(0.3));

    // (5) Geteilte Ansicht: der Fokus steht im FREMDEN Reiter, der wird
    //     geschlossen - danach muss man im Arbeitsreiter stehen.
    add(tu("Geteilt: Arbeitsreiter und zwei weitere, im Arbeitsreiter aendern", [=] {
        openScriptFromMemory("//Generated by BehavEd\nwait ( 1.000 );\n", "g_arbeit.txt");
        openScriptFromMemory("//Generated by BehavEd\nwait ( 2.000 );\n", "g_fremd.txt");
        openScriptFromMemory("//Generated by BehavEd\nwait ( 3.000 );\n", "g_dritter.txt");
        activateTab(findeReiter("g_arbeit.txt"));
        aendern("41.000");
        aendern("42.000");
    }));
    add(pause(0.3));
    splitMenue(s, 2);
    add(tu("Geteilt: zweites Feld zeigt den fremden Reiter, Fokus dorthin", [=] {
        g_app->splitPanes[1].tab = findeReiter("g_fremd.txt");
        g_app->focusRequest = 1;
    }));
    add(pause(0.4));
    add(pruefe("Geteilt: der fremde Reiter ist jetzt aktiv, das Band zeigt den Arbeitsreiter", [=] {
        diag::detail("Knopftest: Geteilt - aktiv " + g_app->shownName + " (" + std::to_string(g_app->activeTab) +
                     "), Band " + std::to_string(g_app->homeTab) + ", Felder " + std::to_string(g_app->splitCount));
        return g_app->shownName == "g_fremd.txt" && g_app->homeTab == findeReiter("g_arbeit.txt");
    }));
    add(tu("Geteilt: den fremden (fokussierten) Reiter schliessen", [] { reiterSchliessen(g_app->activeTab); }));
    add(pause(0.4));
    add(pruefe("Geteilt: danach im Arbeitsreiter, mit Aenderungen und Undo", [=] {
        diag::detail("Knopftest: Geteilt - danach aktiv " + g_app->shownName + " (" +
                     std::to_string(g_app->activeTab) + "), Band " + std::to_string(g_app->homeTab) + ", Wert " +
                     wertJetzt() + ", Undo " + std::to_string(g_app->doc.undoDepth()));
        return g_app->shownName == "g_arbeit.txt" && wertJetzt() == "42.000" && g_app->doc.undoDepth() == 2 &&
               g_app->homeTab == g_app->activeTab;
    }));
    splitMenue(s, 1);
    add(tu("Geteilt: aufraeumen", [=] {
        for (const char* n : {"g_arbeit.txt", "g_fremd.txt", "g_dritter.txt"}) {
            const int i = findeReiter(n);
            if (i >= 0) {
                g_app->tabs[static_cast<std::size_t>(i)].doc.markSaved();
                if (i == g_app->activeTab) { g_app->doc.markSaved(); }
                reiterSchliessen(i);
            }
        }
    }));
    add(pause(0.3));

    add(pause(0.3));
    return s;
}

// --- Beenden nach gescheitertem "Ja, speichern" ------------------------------
//
// Eigene Funktion, nur im Modus `reiter`: der Test setzt alle anderen Reiter
// auf gesichert, damit nur nach seiner Datei gefragt wird - im Gesamtlauf
// braucht das Beenden am Ende aber das geaenderte Knopftest-Skript.
std::vector<Schritt> schutzBeendenSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    const auto findeReiter = [](const std::string& name) {
        for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
            const std::string& n = (static_cast<int>(i) == g_app->activeTab) ? g_app->shownName
                                                                             : g_app->tabs[i].shownName;
            if (n == name) { return static_cast<int>(i); }
        }
        return -1;
    };
    const auto aendern = [](const std::string& wert) {
        if (const Node* n = nodeAt(g_app->doc.script(), Path{0}); n != nullptr && !n->args.empty()) {
            Node neu = *n;
            neu.args[0].text = wert;
            (void)g_app->doc.replaceAt(Path{0}, neu);
            rebuildTree();
        }
    };
    // (6) Beenden: "Ja" bei einer schreibgeschuetzten Datei, den Schutz NICHT
    //     aufheben - das Speichern ging nicht. Ein zweites Beenden muss
    //     wieder fragen (vorher: die Kette blieb hinter dem Reiter stehen, und
    //     beim naechsten Beenden schloss das Programm ungefragt).
    //     Als LETZTES: mit dem alten Fehler endet hier das Programm.
    auto schutzDatei = std::make_shared<std::string>(platform::executableDirectory() + "/selbsttest_schutz.txt");
    add(tu("Schutz: Arbeitsreiter, dazu eine schreibgeschuetzte Datei mit Aenderung", [=] {
        openScriptFromMemory("//Generated by BehavEd\nwait ( 1.000 );\n", "s_arbeit.txt");
        std::error_code ec;
        std::filesystem::permissions(std::filesystem::u8path(*schutzDatei), std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add, ec);
        std::ofstream f(std::filesystem::u8path(*schutzDatei), std::ios::binary);
        f << "//Generated by BehavEd\r\nwait ( 6.000 );\r\n";
        f.close();
        ladeSkriptDatei(*schutzDatei);
        aendern("66.000");
        // Unter Windows wird eine Datei erst schreibgeschuetzt, wenn ALLE
        // Schreibrechte weg sind (MSVC setzt dann FILE_ATTRIBUTE_READONLY).
        std::filesystem::permissions(std::filesystem::u8path(*schutzDatei),
                                     std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                                         std::filesystem::perms::others_write,
                                     std::filesystem::perm_options::remove, ec);
        activateTab(findeReiter("s_arbeit.txt"));
        // Alle uebrigen Reiter sauber, damit nur nach diesem gefragt wird.
        for (std::size_t k = 0; k < g_app->tabs.size(); ++k) {
            const bool aktiv = static_cast<int>(k) == g_app->activeTab;
            const std::string& pfad = aktiv ? g_app->path : g_app->tabs[k].path;
            if (pfad.find("selbsttest_schutz.txt") == std::string::npos) {
                g_app->tabs[k].doc.markSaved();
                if (aktiv) { g_app->doc.markSaved(); }
            }
        }
        g_app->settings.queryOnDiscard = false;   // ohne die Frage "Exit?"
    }));
    add(pause(0.3));
    add(tu("Schutz: Beenden anfordern", [] { g_app->wantQuit = true; }));
    add(pause(0.4));
    add(pruefe("Schutz: es wird nach der geschuetzten Datei gefragt", [] {
        diag::detail("Knopftest: Schutz - Frage offen " + std::string(g_app->askSaveOpen ? "ja" : "nein") +
                     ", aktiv " + g_app->shownName + " " + g_app->path);
        return g_app->askSaveOpen && g_app->path.find("selbsttest_schutz.txt") != std::string::npos;
    }));
    add(klick(tr(Str::AnswerYes), tr(Str::AppTitle)));
    add(pause(0.4));
    add(pruefe("Schutz: behaved bietet an, den Schreibschutz aufzuheben", [] { return g_app->frageOffen; }));
    add(klick(tr(Str::AnswerNo), "###frage"));
    add(pause(0.4));
    add(pruefe("Schutz: nach gescheitertem Speichern ist die Kette zurueckgesetzt, man steht im Arbeitsreiter", [] {
        diag::detail("Knopftest: Schutz - quitAskFrom " + std::to_string(g_app->quitAskFrom) + ", aktiv " +
                     g_app->shownName + " (" + std::to_string(g_app->activeTab) + "), Band " +
                     std::to_string(g_app->homeTab));
        return g_app->quitAskFrom == 0 && g_app->shownName == "s_arbeit.txt" &&
               g_app->homeTab == g_app->activeTab;
    }));
    add(tu("Schutz: noch einmal beenden", [] { g_app->wantQuit = true; }));
    add(pause(0.4));
    add(pruefe("Schutz: das zweite Beenden fragt WIEDER nach der Datei (statt still zu schliessen)", [] {
        diag::detail("Knopftest: Schutz - zweites Beenden, Frage offen " +
                     std::string(g_app->askSaveOpen ? "ja" : "nein"));
        return g_app->askSaveOpen;
    }));
    add(klick(tr(Str::EditorCancel), tr(Str::AppTitle)));
    add(pause(0.3));
    add(tu("Schutz: aufraeumen", [=] {
        g_app->settings.queryOnDiscard = true;
        g_app->quitAskFrom = 0;
        for (std::size_t k = 0; k < g_app->tabs.size(); ++k) {
            const bool aktiv = static_cast<int>(k) == g_app->activeTab;
            const std::string& pfad = aktiv ? g_app->path : g_app->tabs[k].path;
            const std::string& nm = aktiv ? g_app->shownName : g_app->tabs[k].shownName;
            if (pfad.find("selbsttest_schutz.txt") != std::string::npos || nm == "s_arbeit.txt") {
                g_app->tabs[k].doc.markSaved();
                if (aktiv) { g_app->doc.markSaved(); }
            }
        }
        for (const char* n : {"s_arbeit.txt"}) {
            const int i = findeReiter(n);
            if (i >= 0) { reiterSchliessen(i); }
        }
        for (std::size_t k = 0; k < g_app->tabs.size(); ++k) {
            const bool aktiv = static_cast<int>(k) == g_app->activeTab;
            const std::string& pfad = aktiv ? g_app->path : g_app->tabs[k].path;
            if (pfad.find("selbsttest_schutz.txt") != std::string::npos) {
                reiterSchliessen(static_cast<int>(k));
                break;
            }
        }
        std::error_code ec;
        std::filesystem::permissions(std::filesystem::u8path(*schutzDatei), std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add, ec);
        std::filesystem::remove(std::filesystem::u8path(*schutzDatei), ec);
    }));
    add(pause(0.3));
    return s;
}

// --- Nur ein behaved: ein zweiter Start reicht an den laufenden weiter --------
//
// BHED_EDITORTEST=einzel mit BHED_EINZEL=<Name>: diese Instanz startet ihre
// eigene exe noch einmal (ohne BHED_EDITORTEST, wie ein Doppelklick im
// Explorer). Die zweite muss sofort mit 0 enden, und die Datei muss HIER
// ankommen - ohne zweiten Reiter, ohne die Aenderungen zu verlieren.
std::vector<Schritt> einzelSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    auto datei = std::make_shared<std::string>(platform::executableDirectory() + "\\selbsttest_einzel.txt");
    auto anfang = std::make_shared<std::size_t>(0);
    auto griff = std::make_shared<std::uintptr_t>(0);
    auto start = std::make_shared<std::chrono::steady_clock::time_point>();
    auto code = std::make_shared<long>(-3);
    const auto wertJetzt = [] {
        const Node* n = nodeAt(g_app->doc.script(), Path{0});
        return (n != nullptr && !n->args.empty()) ? n->args[0].text : std::string("?");
    };
    const auto aendern = [](const std::string& wert) {
        if (const Node* n = nodeAt(g_app->doc.script(), Path{0}); n != nullptr && !n->args.empty()) {
            Node neu = *n;
            neu.args[0].text = wert;
            (void)g_app->doc.replaceAt(Path{0}, neu);
            rebuildTree();
        }
    };
    const auto zweiterStart = [&](const std::string& was, std::function<std::string()> argument) {
        add(tu("Einzel: zweiter Start " + was, [=] {
            *griff = platform::starteZweiteInstanz(argument());
            *start = std::chrono::steady_clock::now();
            *code = -3;
            diag::detail("Knopftest: Einzel - zweiter Start " + was + ", Griff " + (*griff != 0 ? "da" : "FEHLT"));
        }));
        add(warteBis("Einzel: zweiter Start endet", [=] {
            const long st = platform::zweiteInstanzStand(*griff);
            const double sek = std::chrono::duration<double>(std::chrono::steady_clock::now() - *start).count();
            if (st != -1 || sek > 12.0) {
                *code = st;
                platform::zweiteInstanzSchliessen(*griff, true);   // laeuft sie noch: nur DIESEN Kindprozess
                *griff = 0;
                diag::detail("Knopftest: Einzel - zweiter Start nach " + std::to_string(sek) + " s: " +
                             (st == -1 ? std::string("lief noch, beendet") : "Code " + std::to_string(st)));
                return true;
            }
            return false;
        }, 100000));
        add(pause(0.5));
        add(pruefe("Einzel: der zweite Start " + was + " endet sofort von selbst (Code 0)",
                   [=] { return *code == 0; }));
    };
    add(tu("Einzel: Testdatei schreiben, Reiter zaehlen", [=] {
        std::ofstream f(std::filesystem::u8path(*datei), std::ios::binary);
        f << "//Generated by BehavEd\r\nwait ( 4.000 );\r\n";
        f.close();
        *anfang = g_app->tabs.size();
        diag::detail("Knopftest: Einzel - " + std::to_string(*anfang) + " Reiter am Anfang");
    }));
    zweiterStart("mit der Datei", [=] { return *datei; });
    add(pruefe("Einzel: die Datei ist im laufenden Fenster als neuer Reiter offen", [=] {
        diag::detail("Knopftest: Einzel - Reiter " + std::to_string(g_app->tabs.size()) + ", aktiv " + g_app->path);
        return g_app->tabs.size() == *anfang + 1 && g_app->path.find("selbsttest_einzel.txt") != std::string::npos;
    }));
    add(tu("Einzel: dort zweimal aendern, dann in einen anderen Reiter", [=] {
        aendern("44.000");
        aendern("45.000");
        activateTab(0);
    }));
    add(pause(0.3));
    zweiterStart("mit derselben Datei", [=] { return *datei; });
    add(pruefe("Einzel: kein zweiter Reiter - zurueck im alten, mit Aenderungen und Undo", [=] {
        diag::detail("Knopftest: Einzel - Reiter " + std::to_string(g_app->tabs.size()) + ", aktiv " + g_app->path +
                     ", Wert " + wertJetzt() + ", Undo " + std::to_string(g_app->doc.undoDepth()));
        return g_app->tabs.size() == *anfang + 1 &&
               g_app->path.find("selbsttest_einzel.txt") != std::string::npos && wertJetzt() == "45.000" &&
               g_app->doc.undoDepth() == 2;
    }));
    zweiterStart("ohne Datei", [] { return std::string(); });
    add(pruefe("Einzel: ohne Datei kommt kein Reiter dazu", [=] { return g_app->tabs.size() == *anfang + 1; }));
    add(tu("Einzel: aufraeumen", [=] {
        for (std::size_t k = 0; k < g_app->tabs.size(); ++k) {
            const bool aktiv = static_cast<int>(k) == g_app->activeTab;
            const std::string& pfad = aktiv ? g_app->path : g_app->tabs[k].path;
            if (pfad.find("selbsttest_einzel.txt") != std::string::npos) {
                g_app->tabs[k].doc.markSaved();
                if (aktiv) { g_app->doc.markSaved(); }
                reiterSchliessen(static_cast<int>(k));
                break;
            }
        }
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(*datei), ec);
    }));
    add(pause(0.3));
    return s;
}

// --- Rollende Sicherung: die letzten 10 Speicherungen ---------------------
std::vector<Schritt> sicherungSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    auto datei = std::make_shared<std::string>(platform::executableDirectory() + "/selbsttest_sicherung.txt");
    auto ordner = std::make_shared<std::string>(platform::executableDirectory() + "/backup/selbsttest_sicherung");
    add(tu("Sicherung: alte Sicherungen weg, Skript laden", [=] {
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::u8path(*ordner), ec);
        std::ofstream f(std::filesystem::u8path(*datei), std::ios::binary);
        f << "//Generated by BehavEd\r\n\r\nwait ( 100.000 );\r\n";
        f.close();
        ladeSkriptDatei(*datei);
    }));
    add(pause(0.4));
    for (int k = 1; k <= 12; ++k) {
        add(tu("Sicherung: aendern und speichern " + std::to_string(k), [=] {
            for (std::size_t i = 0; i < g_app->doc.script().nodes.size(); ++i) {
                if (g_app->doc.script().nodes[i].name != "wait") { continue; }
                Node neu = g_app->doc.script().nodes[i];
                neu.args[0].text = std::to_string(k) + ".000";
                (void)g_app->doc.replaceAt(Path{i}, neu);
                rebuildTree();
                break;
            }
        }));
        add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_S, "Strg+S"));
        add(pause(0.15));
    }
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_S, "Strg+S ohne Aenderung"));
    add(pause(0.3));
    add(pruefe("Sicherung: genau 10 Kopien, backup1 = neueste (12), backup10 = dritte (3)", [=] {
        const auto lese = [&](int n) { return slurp(*ordner + "/backup" + std::to_string(n) + ".txt"); };
        int anzahl = 0;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(std::filesystem::u8path(*ordner), ec)) {
            (void)e;
            ++anzahl;
        }
        const std::string b1 = lese(1);
        const std::string b10 = lese(10);
        diag::detail("Knopftest: Sicherung - " + std::to_string(anzahl) + " Dateien, backup1 hat 12: " +
                     (b1.find("12.000") != std::string::npos ? "ja" : "nein") + ", backup10 hat 3: " +
                     (b10.find("( 3.000") != std::string::npos ? "ja" : "nein"));
        return anzahl == 10 && b1.find("12.000") != std::string::npos && b10.find("( 3.000") != std::string::npos &&
               lese(2).find("11.000") != std::string::npos;
    }));
    add(tu("Sicherung: aufraeumen", [=] {
        g_app->doc.markSaved();
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::u8path(*ordner), ec);
        std::filesystem::remove(std::filesystem::u8path(*datei), ec);
    }));
    return s;
}

std::vector<Schritt> undoListeSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    const auto aendern = [](const Path& p, const std::string& wert) {
        if (const Node* n = nodeAt(g_app->doc.script(), p)) {
            Node neu = *n;
            neu.args[0].text = wert;
            (void)g_app->doc.replaceAt(p, neu);
            rebuildTree();
        }
    };
    add(tu("Undo-Liste: Skript laden und dreimal aendern", [=] {
        openScriptFromMemory("//Generated by BehavEd\nwait ( 1000.000 );\nwait ( 2000.000 );\nprint ( \"p\" );\n",
                             "undoliste.txt");
        g_app->selectedPath.clear();
        g_app->selection.clear();
        aendern(Path{1}, "3000.000");
        aendern(Path{2}, "q");
        aendern(Path{0}, "1500.000");
    }));
    add(pause(0.3));
    add(foto("undo_status_vorher"));
    add(pruefe("Undo-Liste: schon VOR dem ersten Undo steht unten der Eintrag mit dem naechsten Schritt", [] {
        diag::detail("Knopftest: Undo-Status vorher \"" + g_app->undoStatusText + "\"");
        return g_app->undoStatusText.find("(3)") != std::string::npos &&
               g_app->undoStatusText.find("1500") != std::string::npos;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.3));
    add(foto("undo_meldung"));
    add(pruefe("Undo-Liste: die Meldung unten nennt den Befehl (1500 -> 1000)", [] {
        diag::detail("Knopftest: Undo-Meldung \"" + g_app->undoMeldung + "\"");
        return g_app->undoMeldung.find("1500") != std::string::npos &&
               g_app->undoMeldung.find("1000") != std::string::npos;
    }));
    add(pruefe("Undo-Liste: der geaenderte Befehl ist ausgewaehlt", [] {
        return g_app->selectedPath == Path{0};
    }));
    add(tu("Undo-Liste: Liste oeffnen", [] { g_app->undoListeAnfrage = 1; }));
    add(pause(0.3));
    add(foto("undo_liste"));
    add(pruefe("Undo-Liste: zwei Schritte, der neueste (print q) oben", [] {
        const auto& e = g_app->undoListeEintraege;
        diag::detail("Knopftest: Undo-Liste " + std::to_string(e.size()) + " Eintraege" +
                     (e.empty() ? "" : ", oben \"" + e.front() + "\""));
        return e.size() == 2 && e[0].find("q") != std::string::npos && e[1].find("3000") != std::string::npos;
    }));
    add(tu("Undo-Liste: beide markieren", [] { g_app->undoListeMarke = 1; }));
    add(pause(0.2));
    add(klick(tr(Str::UndoUpToHere), "##Popup"));
    add(pause(0.3));
    add(pruefe("Undo-Liste: Undo nimmt beide markierten Schritte zurueck (alles wie beim Laden)", [] {
        const Node* a = nodeAt(g_app->doc.script(), Path{1});
        const Node* b = nodeAt(g_app->doc.script(), Path{2});
        diag::detail("Knopftest: Undo-Meldung nach der Liste \"" + g_app->undoMeldung + "\"");
        return g_app->doc.undoDepth() == 0 && a != nullptr && a->args[0].text == "2000.000" && b != nullptr &&
               b->args[0].text.find("p") != std::string::npos && g_app->undoMeldung.find("2") != std::string::npos;
    }));
    // --- Die offene Liste folgt Strg+Z / Strg+Y ------------------------
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Y, "Strg+Y"));
    add(pause(0.2));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Y, "Strg+Y"));
    add(pause(0.2));
    add(tu("Undo-Liste: Liste oeffnen (zwei Schritte)", [] { g_app->undoListeAnfrage = 1; }));
    add(pause(0.3));
    add(pruefe("Undo-Liste: offen mit zwei Eintraegen", [] { return g_app->undoListeEintraege.size() == 2; }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z bei offener Liste"));
    add(pause(0.3));
    add(pruefe("Undo-Liste: nach Strg+Z bei offener Liste nur noch ein Eintrag", [] {
        diag::detail("Knopftest: Undo-Liste nach Strg+Z " + std::to_string(g_app->undoListeEintraege.size()) +
                     " Eintraege, Undo " + std::to_string(g_app->doc.undoDepth()));
        return g_app->undoListeEintraege.size() == g_app->doc.undoDepth() && g_app->doc.undoDepth() == 1;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Y, "Strg+Y bei offener Liste"));
    add(pause(0.3));
    add(pruefe("Undo-Liste: nach Strg+Y wieder zwei", [] { return g_app->undoListeEintraege.size() == 2; }));
    add(klick(tr(Str::EditorCancel), "##Popup"));
    add(pause(0.2));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Y, "Strg+Y"));
    add(pause(0.2));

    // --- Einen Schritt aus der MITTE zuruecknehmen -----------------------
    //
    // Drei Schritte: wait1 3000, print q, wait0 1500. Zurueck nur "print q";
    // die beiden waits bleiben.
    add(pruefe("Einzeln: drei Schritte stehen an", [] { return g_app->doc.undoDepth() == 3; }));
    add(tu("Einzeln: Liste oeffnen, den mittleren Eintrag waehlen", [] {
        g_app->undoListeAnfrage = 1;
    }));
    add(pause(0.3));
    add(tu("Einzeln: mittleren Eintrag markieren", [] { g_app->undoListeMarke = 1; }));
    add(pause(0.2));
    add(foto("undo_einzeln"));
    add(klick(tr(Str::UndoOnlyThis), "##Popup"));
    add(pause(0.3));
    add(pruefe("Einzeln: nur print ist zurueck (p), beide waits behalten ihre neuen Werte", [] {
        const Node* w0 = nodeAt(g_app->doc.script(), Path{0});
        const Node* w1 = nodeAt(g_app->doc.script(), Path{1});
        const Node* pr = nodeAt(g_app->doc.script(), Path{2});
        diag::detail("Knopftest: Einzeln - " + (w0 ? w0->args[0].text : std::string("?")) + " / " +
                     (w1 ? w1->args[0].text : std::string("?")) + " / " + (pr ? pr->args[0].text : std::string("?")) +
                     ", Undo " + std::to_string(g_app->doc.undoDepth()) + ", Meldung " + g_app->undoMeldung);
        return w0 != nullptr && w0->args[0].text == "1500.000" && w1 != nullptr && w1->args[0].text == "3000.000" &&
               pr != nullptr && pr->args[0].text.find('p') != std::string::npos &&
               pr->args[0].text.find('q') == std::string::npos && g_app->doc.undoDepth() == 4;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.2));
    add(pruefe("Einzeln: Strg+Z nimmt das Einzel-Undo selbst wieder zurueck (print q)", [] {
        const Node* pr = nodeAt(g_app->doc.script(), Path{2});
        return pr != nullptr && pr->args[0].text.find('q') != std::string::npos && g_app->doc.undoDepth() == 3;
    }));
    add(tu("Einzeln: eine Zeile loeschen, dann eine andere aendern", [] {
        (void)g_app->doc.removeAt(Path{1});
        rebuildTree();
        if (const Node* n = nodeAt(g_app->doc.script(), Path{0})) {
            Node neu = *n;
            neu.args[0].text = "4000.000";
            (void)g_app->doc.replaceAt(Path{0}, neu);
        }
        rebuildTree();
    }));
    add(pause(0.2));
    add(tu("Einzeln: das Loeschen (zweitneuester Schritt) einzeln zuruecknehmen", [] {
        std::string bericht;
        const bool ok = einzelnenSchrittZuruecknehmen(g_app->doc.undoDepth() - 2, &bericht);
        diag::detail("Knopftest: Einzeln geloescht - " + std::string(ok ? "ok " : "NEIN ") + bericht);
        rebuildTree();
    }));
    add(pause(0.2));
    add(pruefe("Einzeln: die geloeschte Zeile ist wieder an ihrem Platz, die spaetere Aenderung bleibt", [] {
        const Node* w0 = nodeAt(g_app->doc.script(), Path{0});
        const Node* w1 = nodeAt(g_app->doc.script(), Path{1});
        return w0 != nullptr && w0->args[0].text == "4000.000" && w1 != nullptr && w1->name == "wait" &&
               w1->args[0].text == "3000.000" && g_app->doc.script().nodes.size() == 3;
    }));

    add(tu("Undo-Liste: Redo-Liste oeffnen", [] { g_app->undoListeAnfrage = 2; }));
    add(pause(0.3));
    add(pruefe("Undo-Liste: die Redo-Liste ist so lang wie der Redo-Stapel", [] {
        return g_app->undoListeRedo && g_app->undoListeEintraege.size() == g_app->doc.redoDepth();
    }));
    auto redoVorher = std::make_shared<std::size_t>(0);
    add(tu("Undo-Liste: Redo-Tiefe merken", [=] { *redoVorher = g_app->doc.redoDepth(); }));
    add(klick(tr(Str::EditorCancel), "##Popup"));
    add(pause(0.2));
    add(pruefe("Undo-Liste: Cancel aendert nichts", [=] { return g_app->doc.redoDepth() == *redoVorher; }));
    add(tu("Undo-Liste: eine neue Aenderung", [=] { aendern(Path{0}, "7000.000"); }));
    add(pause(0.2));
    add(pruefe("Undo-Liste: nach einer neuen Aenderung ist die alte Meldung weg", [] {
        return g_app->undoMeldung.empty();
    }));
    add(tu("Undo-Liste: aufraeumen", [] { g_app->doc.markSaved(); }));
    return s;
}

// --- Die Zeitleiste mit echter Maus (BHED_EDITORTEST=zeitleiste) ------------
//
// Bisher geprueft: die Knoepfe darueber und der Zeiger. Nie: Klick auf einen
// Block, Ziehen an einer Blockkante, Mausrad ueber Lineal und Spuren,
// mittlere Taste, Klick auf Spurnamen.
std::vector<Schritt> zeitleisteSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    const auto xVon = [](double ms) {
        return g_app->tlFeldX + static_cast<float>((ms - g_app->tlSichtStart) / g_app->tlSichtSpanne) * g_app->tlFeldW;
    };
    const auto yVon = [](const std::string& zeile) {
        for (const auto& [n, y] : g_app->tlZeilenY) {
            if (n == zeile) { return y; }
        }
        return -1000.0F;
    };
    const auto argVon = [](const Path& p) {
        const Node* n = nodeAt(g_app->doc.script(), p);
        return (n != nullptr && !n->args.empty()) ? std::atof(n->args.back().text.c_str()) : -1.0;
    };
    // 0 affect ruhig, 1 ENABLE, 2 MOVE 0, 3 MOVE 3000, 4 PAN 2000, 5 wait 3000, 6 MOVE 2000, 7 wait 2000
    add(tu("Zeitleiste: Kameraskript laden, Leiste hoch", [] {
        openScriptFromMemory("//Generated by BehavEd\n"
                             "affect ( \"ruhig\", /*@AFFECT_TYPE*/ FLUSH )\n{\n"
                             "\tset ( /*@SET_TYPES*/ \"SET_BEHAVIOR_STATE\", \"BS_DEFAULT\" );\n}\n"
                             "camera ( /*@CAMERA_COMMANDS*/ ENABLE );\n"
                             "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 0.000 0.000 100.000 >, 0.000 );\n"
                             "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 500.000 0.000 100.000 >, 3000.000 );\n"
                             "camera ( /*@CAMERA_COMMANDS*/ PAN, < 0.000 90.000 0.000 >, < 0.000 0.000 0.000 >, 2000.000 );\n"
                             "wait ( 3000.000 );\n"
                             "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 500.000 500.000 100.000 >, 2000.000 );\n"
                             "wait ( 2000.000 );\n",
                             "zeitleiste.txt");
        g_app->zeigeRuhigeSpuren = false;
        g_app->leftMode = 1;
        g_app->showTracks = true;
        g_app->timelineFrac = 0.40F;
        g_app->timelineZoom = 1.0;
        g_app->timelineViewStartMs = 0.0;
        g_app->timelineFrames = false;
        g_app->playing = false;
        g_app->playMs = 0.0;
    }));
    add(pause(0.6));
    add(pruefe("Zeitleiste: Zeilen fuer MOVE, PAN und Script sind da", [=] {
        std::string z;
        for (const auto& [n, y] : g_app->tlZeilenY) { z += " [" + n + "]"; }
        diag::detail("Knopftest: Zeitleiste - Zeilen" + z + ", Feld " + std::to_string(g_app->tlFeldX) + " +" +
                     std::to_string(g_app->tlFeldW) + ", Spanne " + std::to_string(g_app->tlSichtSpanne));
        return yVon("camera MOVE") > 0.0F && yVon("camera PAN") > 0.0F && yVon(tr(Str::TlScript)) > 0.0F;
    }));
    // 1. Klick auf einen MOVE-Block (der zweite, 3000-5000 ms)
    add(klickAn("MOVE-Block bei 4 s", [=] { return ImVec2{xVon(3800.0), yVon("camera MOVE")}; }));
    add(pause(0.2));
    add(pruefe("Zeitleiste: Klick auf einen Kamerablock springt an seinen Anfang und waehlt die Kamera", [] {
        diag::detail("Knopftest: Zeitleiste - nach Klick auf MOVE: playMs " + std::to_string(g_app->playMs) +
                     ", Kamera " + std::to_string(g_app->cameraSelected ? 1 : 0) + ", Zeile " +
                     (g_app->selectedPath.empty() ? std::string("-") : std::to_string(g_app->selectedPath.back())));
        // 3050: das wait ( 3000 ) davor endet im Spiel ein Bild spaeter
        // (ICARUS-Nachbau, 50-ms-Takt).
        return std::fabs(g_app->playMs - 3050.0) < 1.0 && g_app->cameraSelected;
    }));
    add(pruefe("Zeitleiste: ... und waehlt seine Zeile im Skript (wie bei den Spuren)", [] {
        return g_app->selectedPath == Path{6};
    }));
    // 2. Klick auf das zweite wait in der Script-Zeile
    add(klickAn("wait-Block bei 4 s", [=] { return ImVec2{xVon(3800.0), yVon(tr(Str::TlScript))}; }));
    add(pause(0.2));
    add(pruefe("Zeitleiste: Klick auf einen Block der Script-Zeile waehlt seine Zeile und springt hin", [] {
        return g_app->selectedPath == Path{7} && std::fabs(g_app->playMs - 3050.0) < 1.0;
    }));
    // 3. Kante des wait (3000-5000) nach rechts ziehen
    auto msJePx = std::make_shared<double>(0.0);
    add(tu("Zeitleiste: Massstab merken", [=] { *msJePx = g_app->tlSichtSpanne / std::max(1.0F, g_app->tlFeldW); }));
    // Das wait laeuft von 3050 bis 5100 (3050 + 2000 + ein Spielbild).
    add(ziehenAn("Kante des wait um 80 Punkte nach rechts",
                 [=] { return ImVec2{xVon(5100.0) - 4.0F, yVon(tr(Str::TlScript))}; }, 80.0F, 0.0F, 0));
    add(pause(0.3));
    add(pruefe("Zeitleiste: Kante ziehen verlaengert das wait um die gezogene Strecke", [=] {
        const double neu = argVon(Path{7});
        const double soll = 2000.0 + 80.0 * (*msJePx);
        diag::detail("Knopftest: Zeitleiste - wait jetzt " + std::to_string(neu) + ", erwartet etwa " +
                     std::to_string(soll));
        return neu > 2000.0 && std::fabs(neu - soll) < soll * 0.15;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.3));
    add(pruefe("Zeitleiste: Strg+Z nimmt das Ziehen zurueck", [=] { return std::fabs(argVon(Path{7}) - 2000.0) < 0.5; }));
    // 3b. Einen Block VERSCHIEBEN (Ripple): den zweiten MOVE (ab 3050) in der
    //     Mitte greifen und 100 Punkte nach rechts ziehen - das wait davor
    //     (Zeile 5) wird laenger, im 50-ms-Raster.
    add(ziehenAn("zweiten MOVE-Block um 100 Punkte nach rechts verschieben",
                 [=] { return ImVec2{xVon(3900.0), yVon("camera MOVE")}; }, 100.0F, 0.0F, 0));
    add(pause(0.3));
    add(pruefe("Zeitleiste: Block verschieben verlaengert das wait davor (Ripple, 50-ms-Raster)", [=] {
        const double neu = argVon(Path{5});
        const double soll = 3000.0 + std::round(100.0 * (*msJePx) / 50.0) * 50.0;
        diag::detail("Knopftest: Zeitleiste - wait davor jetzt " + std::to_string(neu) + ", erwartet etwa " +
                     std::to_string(soll));
        return neu > 3000.0 && std::fabs(neu - soll) <= 100.0 && std::fmod(neu, 50.0) == 0.0;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.3));
    add(pruefe("Zeitleiste: Strg+Z nimmt das Verschieben zurueck", [=] { return std::fabs(argVon(Path{5}) - 3000.0) < 0.5; }));
    // 3c. Bildschritt per Tastatur, solange die Maus ueber der Leiste steht.
    add(tu("Maus ueber die Zeitleiste, Zeiger auf 1000 ms", [=] {
        setzeMaus(ImVec2{xVon(4500.0), yVon(tr(Str::TlScript))});
        g_app->playMs = 1000.0;
        g_app->playing = false;
    }));
    add(pause(0.2));
    add(taste(ImGuiKey_RightArrow, "Pfeil rechts"));
    add(pruefe("Zeitleiste: Pfeil rechts geht ein Spielbild (50 ms) weiter",
               [] { return std::fabs(g_app->playMs - 1050.0) < 0.5; }));
    add(tasteMit(ImGuiMod_Shift, ImGuiKey_RightArrow, "Umschalt+Pfeil rechts"));
    add(pruefe("Zeitleiste: Umschalt+Pfeil rechts geht eine Sekunde weiter",
               [] { return std::fabs(g_app->playMs - 2050.0) < 0.5; }));
    add(taste(ImGuiKey_Home, "Pos1"));
    add(pruefe("Zeitleiste: Pos1 springt an den Anfang", [] { return g_app->playMs < 0.5; }));
    // 4. Kante eines Kamerablocks ziehen (MOVE 0-3000 -> ueber den naechsten MOVE hinaus)
    add(ziehenAn("Kante des ersten MOVE um 120 Punkte nach rechts",
                 [=] { return ImVec2{xVon(3000.0) - 1.0F, yVon("camera MOVE")}; }, 120.0F, 0.0F, 0));
    add(pause(0.3));
    add(pruefe("Zeitleiste: Kante eines Kamerablocks ziehen aendert seine Dauer", [=] {
        const double neu = argVon(Path{3});
        diag::detail("Knopftest: Zeitleiste - MOVE jetzt " + std::to_string(neu) + " ms (der naechste MOVE beginnt bei 3000)");
        return neu > 3000.0;
    }));
    add(pause(0.2));
    add(pruefe("Zeitleiste: der MOVE, der in den naechsten hineinlaeuft, ist rot markiert", [] {
        diag::detail("Knopftest: Zeitleiste - rot markiert " + std::to_string(g_app->tlUeberlappungen));
        return g_app->tlUeberlappungen >= 1;
    }));
    add(foto("zeitleiste_ueberlappung"));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.3));
    add(pruefe("Zeitleiste: nach Strg+Z keine Ueberlappung mehr", [] { return g_app->tlUeberlappungen == 0; }));
    // 5. Mausrad ueber dem Lineal, dann mittlere Taste
    add(radAn("Lineal", [] { return ImVec2{g_app->tlFeldX + g_app->tlFeldW * 0.5F, g_app->tlLinealY}; }, 3.0F));
    add(pause(0.2));
    add(pruefe("Zeitleiste: Mausrad ueber dem Lineal vergroessert", [] {
        diag::detail("Knopftest: Zeitleiste - Zoom " + std::to_string(g_app->timelineZoom));
        return g_app->timelineZoom > 1.2;
    }));
    auto start0 = std::make_shared<double>(0.0);
    add(tu("Zeitleiste: Ausschnitt merken", [=] { *start0 = g_app->timelineViewStartMs; }));
    add(ziehenAn("Lineal mit der mittleren Taste", [] {
        return ImVec2{g_app->tlFeldX + g_app->tlFeldW * 0.5F, g_app->tlLinealY};
    }, -150.0F, 0.0F, 2));
    add(pause(0.2));
    add(pruefe("Zeitleiste: mittlere Taste auf dem Lineal verschiebt den Ausschnitt", [=] {
        diag::detail("Knopftest: Zeitleiste - Ausschnitt " + std::to_string(*start0) + " -> " +
                     std::to_string(g_app->timelineViewStartMs));
        return g_app->timelineViewStartMs > *start0 + 1.0;
    }));
    add(tu("Zeitleiste: Zoom zurueck", [] { g_app->timelineZoom = 1.0; g_app->timelineViewStartMs = 0.0; }));
    add(pause(0.2));
    // 6. Strg + Mausrad ueber den Spuren
    add(Schritt{"Strg + Mausrad ueber den Spuren", [=](int b) {
                    ImGuiIO& io = ImGui::GetIO();
                    if (b == 0) { setzeMaus(ImVec2{g_app->tlFeldX + g_app->tlFeldW * 0.5F, yVon("camera PAN")}); return false; }
                    if (b == 1) { io.AddKeyEvent(ImGuiMod_Ctrl, true); return false; }
                    if (b == 2 || b == 3) { io.AddMouseWheelEvent(0.0F, 1.0F); return false; }
                    if (b == 5) { io.AddKeyEvent(ImGuiMod_Ctrl, false); return false; }
                    return b >= 7;
                }});
    add(pruefe("Zeitleiste: Strg + Mausrad ueber den Spuren vergroessert", [] {
        diag::detail("Knopftest: Zeitleiste - Zoom nach Strg+Rad " + std::to_string(g_app->timelineZoom));
        return g_app->timelineZoom > 1.2;
    }));
    add(tu("Zeitleiste: Zoom zurueck", [] { g_app->timelineZoom = 1.0; g_app->timelineViewStartMs = 0.0; }));
    add(pause(0.2));
    // 7. Klick auf den Namen "camera PAN"
    add(tu("Zeitleiste: Kamera abwaehlen", [] { g_app->cameraSelected = false; }));
    add(klickAn("Name camera PAN", [=] { return ImVec2{g_app->tlFeldX - 40.0F, yVon("camera PAN")}; }));
    add(pause(0.2));
    add(pruefe("Zeitleiste: Klick auf einen Kamera-Spurnamen waehlt die Kamera", [] { return g_app->cameraSelected; }));
    // 8. Klick ins Lineal: springt der Zeiger? (Lieferung fuer die Vorschlaege)
    add(tu("Zeitleiste: Zeiger auf 0", [] { g_app->playMs = 0.0; }));
    add(klickAn("Lineal bei 5 s", [=] { return ImVec2{xVon(5000.0), g_app->tlLinealY}; }));
    add(pause(0.2));
    add(pruefe("Zeitleiste: Klick ins Lineal setzt den Zeiger", [] {
        diag::detail("Knopftest: Zeitleiste - nach Klick ins Lineal bei 5 s: playMs " + std::to_string(g_app->playMs));
        return std::fabs(g_app->playMs - 5000.0) < 60.0 && !g_app->playing;
    }));
    // Ruhige Spuren
    add(pruefe("Zeitleiste: eine ruhige Spur ist ausgeblendet", [=] { return yVon("ruhig") < 0.0F; }));
    add(tu("Zeitleiste: ruhige Spuren zeigen", [] { g_app->zeigeRuhigeSpuren = true; }));
    add(pause(0.2));
    add(pruefe("Zeitleiste: mit dem Schalter ist sie wieder da", [=] { return yVon("ruhig") > 0.0F; }));
    add(tu("Zeitleiste: ruhige Spuren wieder aus", [] { g_app->zeigeRuhigeSpuren = false; }));

    // --- Die Kante folgt der Maus ------------------------------------
    auto kante = std::make_shared<float>(0.0F);
    add(tu("Zeitleiste: Hoehe merken", [=] {
        g_app->timelineFrac = 0.22F;
    }));
    add(pause(0.3));
    add(tu("Zeitleiste: Hoehe gemerkt", [=] { *kante = g_app->tlHoeheIst; }));
    add(ziehenHalten("Zeitleisten-Kante 60 nach oben, 20 Bilder still",
                     [] { return ImVec2{g_app->tlKanteX, g_app->tlKanteY}; }, 0.0F, -60.0F, 20));
    add(pause(0.3));
    add(pruefe("Zeitleiste: die Kante folgt der Maus (genau 60 hoeher)", [=] {
        diag::detail("Knopftest: Zeitleiste vorher " + std::to_string(*kante) + ", nachher " +
                     std::to_string(g_app->tlHoeheIst));
        return std::fabs(g_app->tlHoeheIst - (*kante + 60.0F)) <= 3.0F;
    }));

    add(tu("Zeitleiste: aufraeumen", [] {
        g_app->timelineFrac = 0.22F;
        g_app->doc.markSaved();
    }));
    return s;
}

std::vector<Schritt> aehnlichSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    add(tu("Skript mit wiederholten Kameras laden", [] {
        openScriptFromMemory(std::string("//Generated by BehavEd\n") + kKuerzelSkript, "aehnlich.txt");
        selectByPath(Path{0});
        g_app->findOpen = false;
        g_app->settings.highlightSame = false;
    }));
    add(pause(0.5));
    // Rechtsklick auf die ERSTE Kamera (Zeile 1).
    add(klick("camera", "##flowarea", 0, false, "", 1));
    add(pause(0.3));
    add(klick(tr(Str::ActFindSimilar), "##Popup"));
    add(pause(0.3));
    add(klick("10.000 20.000 40.000", "##Menu", 0, true));
    add(pause(0.4));
    add(pruefe("Find similar: das Suchfenster ist offen und vorn", [] {
        return g_app->findOpen && fensterSichtbarVorn("###find");
    }));
    add(pruefe("Find similar: Name und Wert eingetragen, zwei Treffer", [] {
        diag::detail("Knopftest: Find similar - called \"" + g_app->findLetzte.named + "\", containing \"" +
                     g_app->findLetzte.contains + "\", Treffer " + std::to_string(g_app->findHits.size()));
        return g_app->findLetzte.named == "camera" && g_app->findLetzte.contains == "10.000 20.000 40.000" &&
               g_app->findHits.size() == 2U && g_app->findWhole;
    }));
    add(tasteMit(ImGuiKey_None, ImGuiKey_F3, "F3"));
    add(pause(0.2));
    add(pruefe("... und F3 springt zur zweiten gleichen Kamera", [] { return g_app->selectedPath == Path{3}; }));
    add(tu("Find zu", [] { g_app->findOpen = false; }));
    add(pause(0.2));
    // Nur nach dem Namen
    add(klick("camera", "##flowarea", 0, false, "", 1));
    add(pause(0.3));
    add(klick(tr(Str::ActFindSimilar), "##Popup"));
    add(pause(0.3));
    add(klick("Every", "##Menu", 0, true));
    add(pause(0.4));
    add(pruefe("Find similar: \"Every camera\" findet alle drei Kameras", [] {
        return g_app->findLetzte.named == "camera" && g_app->findLetzte.contains.empty() &&
               g_app->findHits.size() == 3U;
    }));
    add(tu("Find zu", [] { g_app->findOpen = false; }));
    add(pause(0.2));

    // --- Gleiche Befehle hervorheben, ueber das View-Menue ----------------
    add(tu("Erste Kamera waehlen", [] { selectByPath(Path{1}); }));
    add(pause(0.2));
    add(pruefe("Ohne die Option: nichts hervorgehoben", [] { return g_app->gleicheSichtbar == 0; }));
    add(klick(tr(Str::MenuView), "###main"));
    add(klick(tr(Str::ViewHighlightSame), "##Menu"));
    add(pause(0.3));
    add(pruefe("View > Highlight identical commands: die Option ist an", [] { return g_app->settings.highlightSame; }));
    add(pruefe("... genau die EINE gleiche Kamera ist hinterlegt (nicht die andere Stellung)", [] {
        diag::detail("Knopftest: gleiche hervorgehoben " + std::to_string(g_app->gleicheSichtbar));
        return g_app->gleicheSichtbar == 1;
    }));
    add(tu("Ein wait waehlen", [] { selectByPath(Path{2}); }));
    add(pause(0.2));
    add(pruefe("wait ( 100 ) hat keinen Zwilling - nichts hervorgehoben", [] { return g_app->gleicheSichtbar == 0; }));
    add(klick(tr(Str::MenuView), "###main"));
    add(klick(tr(Str::ViewHighlightSame), "##Menu"));
    add(pause(0.2));
    add(pruefe("View > Highlight identical commands wieder aus", [] { return !g_app->settings.highlightSame; }));
    add(tu("Aufraeumen", [] { g_app->doc.markSaved(); }));
    return s;
}

// --- Die Hauptteiler bis an die Enden ziehen (27.09.) --------------------------
//
// shank: "das Script-Flow-Fenster laesst sich komplett ueber das Fenster vom
// Renderer ziehen ... das gleiche auch mit Actions und Events und auch bei
// Models - da muss alles getestet und gefixt werden". In jedem Modus beide
// Teiler weit ueber beide Enden hinaus ziehen; jede Spalte muss ihre
// Mindestbreite behalten, in der Kartenansicht das Bild sichtbar bleiben.
std::vector<Schritt> teilerSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    auto vorher = std::make_shared<std::array<float, 5>>();
    add(tu("Teiler: Aufteilung merken", [=] {
        *vorher = {g_app->settings.splitEvents, g_app->settings.splitMap, g_app->settings.splitModel,
                   g_app->settings.splitButtons, static_cast<float>(g_app->leftMode)};
    }));
    const char* namen[] = {"Events", "Map", "Model"};
    for (int modus = 0; modus < 3; ++modus) {
        const std::string mn = namen[modus];
        add(tu("Teiler: Modus " + mn, [=] { g_app->leftMode = modus; }));
        add(pause(0.5));
        const auto zeile = [=](const char* was) {
            char z[300];
            std::snprintf(z, sizeof(z),
                          "Knopftest: Teiler %s %s - Breiten %.0f | %.0f | %.0f, Grenzen links %.0f-%.0f, Flow ab %.0f, "
                          "Actions %.0f-%.0f",
                          mn.c_str(), was, static_cast<double>(g_app->spaltenB[0]), static_cast<double>(g_app->spaltenB[1]),
                          static_cast<double>(g_app->spaltenB[2]), static_cast<double>(g_app->spaltenMin[0]),
                          static_cast<double>(g_app->spaltenMax[0]), static_cast<double>(g_app->spaltenMin[1]),
                          static_cast<double>(g_app->spaltenMin[2]), static_cast<double>(g_app->spaltenMax[2]));
            diag::detail(z);
            std::snprintf(z, sizeof(z), "Knopftest: Teiler %s %s - Wunsch %.0f %.0f %.0f, gegeben %.0f %.0f %.0f, Ziel %.0f / %.0f",
                          mn.c_str(), was, static_cast<double>(g_app->spaltenReq[0]), static_cast<double>(g_app->spaltenReq[1]),
                          static_cast<double>(g_app->spaltenReq[2]), static_cast<double>(g_app->spaltenGiven[0]),
                          static_cast<double>(g_app->spaltenGiven[1]), static_cast<double>(g_app->spaltenGiven[2]),
                          static_cast<double>(g_app->spaltenZiel[0]), static_cast<double>(g_app->spaltenZiel[2]));
            diag::detail(z);
        };
        const bool mitActions = (modus != 1);
        add(tu("Teiler " + mn + ": was liegt unter dem Teiler", [] {
            ImGuiContext& g = *GImGui;
            diag::detail("Knopftest: Teiler - unter der Maus vorher: " +
                         std::string(g.HoveredWindow != nullptr ? g.HoveredWindow->Name : "-") + ", Teiler bei " +
                         std::to_string(g_app->teilerX[0]) + "/" + std::to_string(g_app->teilerY));
        }));
        // --- Genau: der Teiler folgt der Maus, auch beim Stillhalten ------
        auto genau = std::make_shared<std::array<float, 2>>();
        add(tu("Teiler " + mn + ": Breiten fuer den Genauigkeitstest merken", [=] {
            (*genau)[0] = g_app->spaltenGiven[0];
            (*genau)[1] = g_app->spaltenGiven[2];
        }));
        add(ziehenHalten(mn + ": linker Teiler 80 nach rechts, 20 Bilder still",
                         [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, 80.0F, 0.0F, 20));
        add(pause(0.3));
        add(pruefe("Teiler " + mn + ": links genau 80 breiter (folgt der Maus, laeuft nicht weg)", [=] {
            zeile("80 nach rechts");
            const float soll = std::min((*genau)[0] + 80.0F, g_app->spaltenMax[0]);
            (*genau)[0] = g_app->spaltenGiven[0];   // Ausgang fuer den Rueckweg
            return std::fabs(g_app->spaltenGiven[0] - soll) <= 3.0F;
        }));
        add(ziehenHalten(mn + ": linker Teiler 80 zurueck",
                         [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, -80.0F, 0.0F, 20));
        add(pause(0.3));
        add(pruefe("Teiler " + mn + ": 80 zurueck macht links genau 80 schmaler", [=] {
            const float soll = std::max((*genau)[0] - 80.0F, g_app->spaltenMin[0]);
            return std::fabs(g_app->spaltenGiven[0] - soll) <= 3.0F;
        }));
        if (modus != 1) {
            add(ziehenHalten(mn + ": rechter Teiler 60 nach links, 20 Bilder still",
                             [] { return ImVec2{g_app->teilerX[1], g_app->teilerY}; }, -60.0F, 0.0F, 20));
            add(pause(0.3));
            add(pruefe("Teiler " + mn + ": Actions genau 60 breiter", [=] {
                zeile("rechts 60 nach links");
                const float soll = std::min((*genau)[1] + 60.0F, g_app->spaltenMax[2]);
                return std::fabs(g_app->spaltenGiven[2] - soll) <= 3.0F;
            }));
            add(ziehenHalten(mn + ": rechter Teiler 60 zurueck",
                             [] { return ImVec2{g_app->teilerX[1], g_app->teilerY}; }, 60.0F, 0.0F, 20));
            add(pause(0.3));
            // shank, 28.09.: "wenn ich die rechte Seite nach links ziehe,
            // verrutscht die rechte Seite nach links" - rechts blieb eine
            // Luecke, bis das Fenster minimiert wurde.
            // Die Luecke in Grundstellung ist der Fensterrahmen - sie darf
            // sich beim Ziehen nicht aendern, in KEINEM Bild.
            auto grund = std::make_shared<float>(0.0F);
            auto spur = std::make_shared<std::pair<float, float>>();
            add(tu("Teiler " + mn + ": Luecke in Grundstellung merken", [=] { *grund = g_app->spaltenLuecke; }));
            add(ziehenHalten(mn + ": rechter Teiler 300 nach links, 30 Bilder still",
                             [] { return ImVec2{g_app->teilerX[1], g_app->teilerY}; }, -300.0F, 0.0F, 30, spur));
            add(pause(0.5));
            add(pruefe("Teiler " + mn + ": die Luecke rechts neben Actions bleibt beim Ziehen gleich (jedes Bild)", [=] {
                zeile("rechts 300 nach links");
                diag::detail("Knopftest: Teiler " + mn + " Luecke Grund " + std::to_string(*grund) + ", beim Ziehen " +
                             std::to_string(spur->first) + ".." + std::to_string(spur->second) + ", danach " +
                             std::to_string(g_app->spaltenLuecke));
                return std::fabs(spur->first - *grund) <= 1.0F && std::fabs(spur->second - *grund) <= 1.0F &&
                       std::fabs(g_app->spaltenLuecke - *grund) <= 1.0F;
            }));
            add(ziehenHalten(mn + ": rechter Teiler 300 zurueck",
                             [] { return ImVec2{g_app->teilerX[1], g_app->teilerY}; }, 300.0F, 0.0F, 30, spur));
            add(pause(0.5));
            add(pruefe("Teiler " + mn + ": auch beim Zurueckziehen bleibt die Luecke gleich", [=] {
                diag::detail("Knopftest: Teiler " + mn + " zurueck: Luecke " + std::to_string(spur->first) + ".." +
                             std::to_string(spur->second));
                return std::fabs(spur->first - *grund) <= 1.0F && std::fabs(spur->second - *grund) <= 1.0F &&
                       std::fabs(g_app->spaltenLuecke - *grund) <= 1.0F;
            }));
            // Ruckartig: der ganze Weg in EINEM Bild, hin und zurueck, dreimal.
            for (int ruck = 0; ruck < 3; ++ruck) {
                add(ziehenHalten(mn + ": rechter Teiler ruckartig 300 nach links",
                                 [] { return ImVec2{g_app->teilerX[1], g_app->teilerY}; }, -300.0F, 0.0F, 10, spur, 1));
                add(pause(0.2));
                add(pruefe("Teiler " + mn + ": ruckartig nach links - Luecke bleibt gleich", [=] {
                    diag::detail("Knopftest: Teiler " + mn + " Ruck links: Luecke " + std::to_string(spur->first) + ".." +
                                 std::to_string(spur->second) + ", Ueberhang " + std::to_string(g_app->spaltenUeberhang));
                    return std::fabs(spur->second - *grund) <= 1.0F && std::fabs(g_app->spaltenLuecke - *grund) <= 1.0F;
                }));
                add(ziehenHalten(mn + ": rechter Teiler ruckartig 300 zurueck",
                                 [] { return ImVec2{g_app->teilerX[1], g_app->teilerY}; }, 300.0F, 0.0F, 10, spur, 1));
                add(pause(0.2));
                add(pruefe("Teiler " + mn + ": ruckartig zurueck - Luecke bleibt gleich", [=] {
                    diag::detail("Knopftest: Teiler " + mn + " Ruck zurueck: Luecke " + std::to_string(spur->first) +
                                 ".." + std::to_string(spur->second) + ", Ueberhang " +
                                 std::to_string(g_app->spaltenUeberhang));
                    return std::fabs(spur->second - *grund) <= 1.0F && std::fabs(g_app->spaltenLuecke - *grund) <= 1.0F;
                }));
            }
            // Die Ursache bei shank: das Randmass lief bis an seine Klemme
            // (6 Zeichen) und bestaetigte sich dann jedes Bild selbst. Hier
            // wird es einmal kuenstlich verfaelscht - es muss sich von selbst
            // erholen, und rechts darf keine Luecke bleiben.
            add(tu("Teiler " + mn + ": Randmass kuenstlich auf 6 Zeichen", [] {
                g_app->spaltenUeberhang = ImGui::GetFontSize() * 6.0F;
            }));
            add(pause(0.5));
            add(pruefe("Teiler " + mn + ": ein falsches Randmass erholt sich von selbst (keine Luecke bleibt)", [=] {
                diag::detail("Knopftest: Teiler " + mn + " nach Verfaelschen: Luecke " +
                             std::to_string(g_app->spaltenLuecke) + ", Ueberhang " +
                             std::to_string(g_app->spaltenUeberhang));
                return std::fabs(g_app->spaltenLuecke - *grund) <= 1.0F;
            }));
            // shank, 28.09.: "wenn die linke Spalte zu nah an der rechten
            // Actions ist, dann passiert das" - der rechte Teiler schob die
            // linke Spalte weg, und sie sprang spaeter von selbst zurueck.
            // Kein Teiler darf eine andere Spalte verschieben.
            auto fest = std::make_shared<std::array<float, 3>>();
            add(ziehenHalten(mn + ": linker Teiler ganz nach rechts (Script Flow auf Mindestbreite)",
                             [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, 4000.0F, 0.0F, 10));
            add(pause(0.3));
            add(tu("Teiler " + mn + ": Breiten merken", [=] {
                for (int k = 0; k < 3; ++k) { (*fest)[k] = g_app->spaltenGiven[k]; }
            }));
            add(ziehenHalten(mn + ": dann rechter Teiler 300 nach links",
                             [] { return ImVec2{g_app->teilerX[1], g_app->teilerY}; }, -300.0F, 0.0F, 20));
            add(pause(0.3));
            add(pruefe("Teiler " + mn + ": rechter Teiler schiebt die linke Spalte nicht weg (stoesst an)", [=] {
                zeile("links voll, rechts 300 nach links");
                return std::fabs(g_app->spaltenGiven[0] - (*fest)[0]) <= 1.0F &&
                       std::fabs(g_app->spaltenGiven[2] - (*fest)[2]) <= 1.0F &&
                       g_app->spaltenGiven[1] >= g_app->spaltenMin[1] - 1.0F;
            }));
            add(ziehenHalten(mn + ": linker Teiler zurueck in die Mitte",
                             [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, -4000.0F, 0.0F, 10));
            add(pause(0.3));
            add(ziehenHalten(mn + ": rechter Teiler ganz nach links (Actions breit)",
                             [] { return ImVec2{g_app->teilerX[1], g_app->teilerY}; }, -4000.0F, 0.0F, 10));
            add(pause(0.3));
            add(ziehenHalten(mn + ": linker Teiler ganz nach rechts",
                             [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, 4000.0F, 0.0F, 10));
            add(pause(0.3));
            add(tu("Teiler " + mn + ": Breiten merken (2)", [=] {
                for (int k = 0; k < 3; ++k) { (*fest)[k] = g_app->spaltenGiven[k]; }
            }));
            add(ziehenHalten(mn + ": linker Teiler noch 300 weiter nach rechts",
                             [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, 300.0F, 0.0F, 20));
            add(pause(0.3));
            add(pruefe("Teiler " + mn + ": linker Teiler schiebt Actions nicht weg (stoesst an)", [=] {
                zeile("Actions breit, links 300 weiter");
                return std::fabs(g_app->spaltenGiven[2] - (*fest)[2]) <= 1.0F &&
                       std::fabs(g_app->spaltenGiven[0] - (*fest)[0]) <= 1.0F;
            }));
            add(ziehenHalten(mn + ": linker Teiler zurueck",
                             [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, -4000.0F, 0.0F, 10));
            add(ziehenHalten(mn + ": rechter Teiler zurueck",
                             [] { return ImVec2{g_app->teilerX[1], g_app->teilerY}; }, 4000.0F, 0.0F, 10));
            add(pause(0.3));
            // Und der linke Teiler: auch er darf rechts nichts aufreissen.
            add(ziehenHalten(mn + ": linker Teiler 200 nach rechts, 20 Bilder still",
                             [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, 200.0F, 0.0F, 20, spur));
            add(pause(0.5));
            add(pruefe("Teiler " + mn + ": linker Teiler laesst die Luecke rechts gleich", [=] {
                diag::detail("Knopftest: Teiler " + mn + " links: Luecke " + std::to_string(spur->first) + ".." +
                             std::to_string(spur->second));
                return std::fabs(spur->first - *grund) <= 1.0F && std::fabs(spur->second - *grund) <= 1.0F;
            }));
            add(ziehenHalten(mn + ": linker Teiler 200 zurueck",
                             [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, -200.0F, 0.0F, 20));
            add(pause(0.3));
        } else {
            // Map: zwei Spalten, dieselbe Tabelle - die Luecke laege rechts
            // neben dem Script Flow.
            auto grund = std::make_shared<float>(0.0F);
            auto spur = std::make_shared<std::pair<float, float>>();
            add(tu("Teiler Map: Luecke in Grundstellung merken", [=] { *grund = g_app->spaltenLuecke; }));
            add(ziehenHalten("Map: linker Teiler ruckartig 300 nach links",
                             [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, -300.0F, 0.0F, 20, spur, 1));
            add(pause(0.3));
            add(pruefe("Teiler Map: die Luecke rechts bleibt beim Ziehen gleich (jedes Bild)", [=] {
                diag::detail("Knopftest: Teiler Map Luecke Grund " + std::to_string(*grund) + ", beim Ziehen " +
                             std::to_string(spur->first) + ".." + std::to_string(spur->second));
                return std::fabs(spur->second - *grund) <= 1.0F && std::fabs(g_app->spaltenLuecke - *grund) <= 1.0F;
            }));
            add(ziehenHalten("Map: linker Teiler 300 zurueck",
                             [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, 300.0F, 0.0F, 20));
            add(pause(0.3));
            add(tu("Teiler Map: Randmass kuenstlich auf 6 Zeichen", [] {
                g_app->spaltenUeberhang = ImGui::GetFontSize() * 6.0F;
            }));
            add(pause(0.5));
            add(pruefe("Teiler Map: ein falsches Randmass erholt sich von selbst (keine Luecke bleibt)", [=] {
                diag::detail("Knopftest: Teiler Map nach Verfaelschen: Luecke " + std::to_string(g_app->spaltenLuecke) +
                             ", Ueberhang " + std::to_string(g_app->spaltenUeberhang));
                return std::fabs(g_app->spaltenLuecke - *grund) <= 1.0F;
            }));
        }
        auto vorZug = std::make_shared<float>(0.0F);
        add(tu("Teiler " + mn + ": Breite links merken", [=] { *vorZug = g_app->spaltenGiven[0]; }));
        // linker Teiler ganz nach links
        add(ziehenAn(mn + ": linker Teiler weit nach links",
                     [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, -4000.0F, 0.0F, 0));
        add(pause(0.3));
        // Verglichen wird der INHALT der Spalten (WidthGiven) - die Grenzen
        // gelten fuer ihn, die gemessene Spalte hat noch ihre Raender.
        add(pruefe("Teiler " + mn + ": der linke Teiler laesst sich ziehen (die Spalte wird schmaler)", [=] {
            return g_app->spaltenGiven[0] < *vorZug - 20.0F || *vorZug <= g_app->spaltenMin[0] + 1.0F;
        }));
        add(pruefe("Teiler " + mn + ": links bleibt die Mindestbreite", [=] {
            zeile("nach links");
            return g_app->spaltenGiven[0] >= g_app->spaltenMin[0] - 1.0F;
        }));
        // linker Teiler ganz nach rechts
        add(ziehenAn(mn + ": linker Teiler weit nach rechts",
                     [] { return ImVec2{g_app->teilerX[0], g_app->teilerY}; }, 4000.0F, 0.0F, 0));
        add(pause(0.3));
        add(pruefe("Teiler " + mn + ": Script Flow (und Actions) behalten ihre Mindestbreite", [=] {
            zeile("nach rechts");
            return g_app->spaltenGiven[1] >= g_app->spaltenMin[1] - 1.0F &&
                   (!mitActions || g_app->spaltenGiven[2] >= g_app->spaltenMin[2] - 1.0F);
        }));
        if (modus == 1) {
            add(pruefe("Teiler Map: das Kartenbild bleibt sichtbar", [] {
                const float bw = g_app->dbgBildW;   // auch ohne geladene Karte gemessen
                diag::detail("Knopftest: Teiler Map - Kartenbild " + std::to_string(bw) + " breit");
                return bw > ImGui::GetFontSize() * 10.0F;
            }));
        }
        if (mitActions) {
            add(ziehenAn(mn + ": rechter Teiler weit nach rechts",
                         [] { return ImVec2{g_app->teilerX[1], g_app->teilerY}; }, 4000.0F, 0.0F, 0));
            add(pause(0.3));
            add(pruefe("Teiler " + mn + ": Actions behaelt die Mindestbreite", [=] {
                zeile("rechts nach rechts");
                return g_app->spaltenGiven[2] >= g_app->spaltenMin[2] - 1.0F;
            }));
            add(ziehenAn(mn + ": rechter Teiler weit nach links",
                         [] { return ImVec2{g_app->teilerX[1], g_app->teilerY}; }, -4000.0F, 0.0F, 0));
            add(pause(0.3));
            add(pruefe("Teiler " + mn + ": Script Flow behaelt die Mindestbreite, Actions hoechstens 45 %", [=] {
                zeile("rechts nach links");
                return g_app->spaltenGiven[1] >= g_app->spaltenMin[1] - 1.0F &&
                       g_app->spaltenGiven[2] <= g_app->spaltenMax[2] + 1.0F &&
                       g_app->spaltenGiven[0] >= g_app->spaltenMin[0] - 1.0F;
            }));
        }
        add(foto("teiler_" + mn));
    }
    add(tu("Teiler: Aufteilung zurueck", [=] {
        g_app->settings.splitEvents = (*vorher)[0];
        g_app->settings.splitMap = (*vorher)[1];
        g_app->settings.splitModel = (*vorher)[2];
        g_app->settings.splitButtons = (*vorher)[3];
        g_app->leftMode = static_cast<int>((*vorher)[4]);
    }));
    add(pause(0.3));
    return s;
}

std::vector<Schritt> fensterSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    struct Fenster {
        const char* name;
        const char* id;
        bool App::*offen;
    };
    const Fenster alle[] = {
        {"Find", "###find", &App::findOpen},
        {"Notes (Messages)", "###messages", &App::messagesOpen},
        {"Keyboard shortcuts", "###keys", &App::keysOpen},
        {"Paths", "###paths", &App::pathsOpen},
        {"Interplay", "###interplay", &App::interplayOpen},
    };
    for (const Fenster& f : alle) {
        const std::string n = f.name;
        const char* id = f.id;
        bool App::*offen = f.offen;
        add(tu(n + ": oeffnen", [=] { fensterZeigen(g_app->*offen, id); }));
        add(pause(0.3));
        add(pruefe(n + ": offen und vor dem Hauptfenster", [=] {
            return g_app->*offen && fensterSichtbarVorn(id);
        }));
        add(foto("fenster_" + std::string(id + 3)));
        add(tu(n + ": Groesse messen", [=] {
            if (ImGuiWindow* w = ImGui::FindWindowByName(id)) {
                char z[240];
                std::snprintf(z, sizeof(z),
                              "Knopftest: Fenster %s Groesse %.0f x %.0f, Inhalt %.0f x %.0f, Rollbereich %.0f / %.0f",
                              id, w->Size.x, w->Size.y, w->ContentSize.x, w->ContentSize.y,
                              w->ScrollMax.x, w->ScrollMax.y);
                diag::detail(z);
            }
        }));
        for (int mal = 1; mal <= 3; ++mal) {
            add(klickAn(n + ": Klick ausserhalb", [=] { return ausserhalbVon(id); }));
            add(pause(0.25));
            add(pruefe(n + ": nach Klick ausserhalb (" + std::to_string(mal) +
                           ") weiter sichtbar und vorn", [=] {
                diag::detail("Knopftest: " + n + " Rang " + std::to_string(fensterRang(id)) +
                             ", Hauptfenster " + std::to_string(fensterRang("###main")));
                return g_app->*offen && fensterSichtbarVorn(id);
            }));
        }
        add(zusammendruecken(n, [=] { return ImGui::FindWindowByName(id); }));
        add(pause(0.3));
        add(pruefe(n + ": laesst sich nicht zu klein ziehen",
                   [=] { return fensterNichtZuKlein(ImGui::FindWindowByName(id), n); }));
        add(tu(n + ": schliessen", [=] { g_app->*offen = false; }));
        add(pause(0.25));
        add(tu(n + ": wieder oeffnen", [=] { fensterZeigen(g_app->*offen, id); }));
        add(pause(0.3));
        add(pruefe(n + ": wieder offen und vorn", [=] {
            return g_app->*offen && fensterSichtbarVorn(id);
        }));
        add(tu(n + ": zu", [=] { g_app->*offen = false; }));
        add(pause(0.2));
    }

    // ---- Die modalen Menuefenster: passt der Inhalt hinein? -------------
    struct Modal { const char* name; Str titel; bool App::*offen; };
    const Modal modale[] = {
        {"Prefs", Str::PrefsTitle, &App::prefsOpen},
        {"About", Str::AboutTitle, &App::aboutOpen},
        {"Pk3", Str::Pk3Title, &App::pk3Open},
    };
    for (const Modal& m : modale) {
        const std::string n = m.name;
        const Str titel = m.titel;
        bool App::*offen = m.offen;
        add(tu(n + ": oeffnen", [=] { g_app->*offen = true; }));
        add(pause(0.4));
        add(foto("menue_" + n));
        add(pruefe(n + ": Inhalt passt ins Fenster (nicht zu klein)", [=] {
            ImGuiWindow* w = ImGui::FindWindowByName(tr(titel));
            if (w == nullptr) { return false; }
            char z[240];
            std::snprintf(z, sizeof(z),
                          "Knopftest: Menuefenster %s Groesse %.0f x %.0f, Inhalt %.0f x %.0f, Rollbereich %.0f / %.0f",
                          n.c_str(), w->Size.x, w->Size.y, w->ContentSize.x, w->ContentSize.y,
                          w->ScrollMax.x, w->ScrollMax.y);
            diag::detail(z);
            return w->ScrollMax.x <= 0.5F;
        }));
        add(zusammendruecken(n, [=] { return ImGui::FindWindowByName(tr(titel)); }));
        add(pause(0.3));
        add(pruefe(n + ": laesst sich nicht zu klein ziehen",
                   [=] { return fensterNichtZuKlein(ImGui::FindWindowByName(tr(titel)), n); }));
        add(tu(n + ": zu", [=] { g_app->*offen = false; }));
        add(pause(0.3));
    }
    // Der Ereignis-Editor (modal, passt sich dem Inhalt an).
    add(tu("Ereignis-Editor: oeffnen", [] {
        const auto& ns = g_app->doc.script().nodes;
        for (std::size_t i = 0; i < ns.size(); ++i) {
            if (ns[i].kind == Node::Kind::Command && ns[i].name == "set") {
                selectByPath(Path{i});
                openEditorForNode(Path{i});
                break;
            }
        }
    }));
    add(pause(0.4));
    add(zusammendruecken("Ereignis-Editor", [] { return ImGui::GetTopMostPopupModal(); }));
    add(pause(0.3));
    add(pruefe("Ereignis-Editor: laesst sich nicht zu klein ziehen", [] {
        return g_app->editorOpen &&
               fensterNichtZuKlein(ImGui::GetTopMostPopupModal(), "Ereignis-Editor");
    }));
    add(tu("Ereignis-Editor: zu", [] { g_app->editorOpen = false; }));
    add(pause(0.3));
    // Debug > Layout ebenso.
    add(tu("Layout: oeffnen", [] { g_app->layoutOpen = true; }));
    add(pause(0.3));
    add(zusammendruecken("Layout", [] { return ImGui::FindWindowByName(tr(Str::DebugLayoutTitle)); }));
    add(pause(0.3));
    add(pruefe("Layout: laesst sich nicht zu klein ziehen", [] {
        return fensterNichtZuKlein(ImGui::FindWindowByName(tr(Str::DebugLayoutTitle)), "Layout");
    }));
    add(tu("Layout: zu", [] { g_app->layoutOpen = false; }));
    add(pause(0.2));
    add(pruefe("Interplay: Titel und Inhalt passen (Mindestgroesse)", [] {
        ImGuiWindow* w = ImGui::FindWindowByName("###interplay");
        return w != nullptr && w->Size.x >= ImGui::GetFontSize() * 29.0F &&
               w->ScrollMax.x <= 0.5F;
    }));

    // ---- Ersetzen im Find-Fenster (wie Notepad++) -------------------------
    //
    // Eine Kamerastellung, die an zwei Stellen wiederverwendet wird, dazu
    // eine andere. Getippt wird in die echten Felder, geklickt auf die
    // echten Knoepfe.
    add(tu("Skript mit wiederverwendeter Kamera laden", [] {
        openScriptFromMemory(
            "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 1299.000 -2714.000 1082.000 >, 0 );\n"
            "camera ( /*@CAMERA_COMMANDS*/ PAN, < -1.000 -173.000 0.000 >, < 0.000 0.000 0.000 >, 0 );\n"
            "wait ( 7700.000 );\n"
            "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 1308.000 -2710.000 1080.000 >, 0 );\n"
            "wait ( 6667.000 );\n"
            "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 1299.000 -2714.000 1082.000 >, 0 );\n"
            "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 1299.000 -2714.000 1082.000 >, 0 );\n",
            "ersetzen.txt");
        selectByPath(Path{0});
        g_app->findWhole = true;
        g_app->findInSelection = false;
        g_app->findErsatz.clear();
        fensterZeigen(g_app->findOpen, "###find");
    }));
    add(pause(0.4));
    auto vec = [](std::size_t i) {
        const auto& ns = g_app->doc.script().nodes;
        return (i < ns.size() && ns[i].args.size() > 1) ? ns[i].args[1].text : std::string();
    };
    add(klick(tr(Str::FindNamed), "###find", 0, false, "", 0, 0.2F));
    add(tippe("camera"));
    add(klick(tr(Str::FindContains), "###find", 0, false, "", 0, 0.2F));
    add(tippe("1299.000 -2714.000 1082.000"));
    add(klick(tr(Str::FindReplaceWith), "###find", 0, false, "", 0, 0.2F));
    add(tippe("1300 -2700 1090"));
    add(pause(0.2));
    // Replace: die Auswahl steht auf Zeile 0, einem Treffer - ersetzen.
    add(klick(tr(Str::FindReplaceBtn), "###find"));
    add(pause(0.3));
    add(pruefe("Replace ersetzt den gewaehlten Treffer (wie BehavEd geschrieben)", [=] {
        diag::detail("Knopftest: nach Replace Zeile 0 = " + vec(0));
        return vec(0) == "1300.000 -2700.000 1090.000" &&
               vec(5) == "1299.000 -2714.000 1082.000";
    }));
    add(pruefe("... und springt zum naechsten Treffer", [] {
        return g_app->selectedPath == Path{5};
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.2));
    add(pruefe("Strg+Z nimmt das eine Ersetzen zurueck",
               [=] { return vec(0) == "1299.000 -2714.000 1082.000"; }));
    add(tu("Find wieder nach vorn", [] { fensterZeigen(g_app->findOpen, "###find"); }));
    add(pause(0.3));
    add(klick(tr(Str::FindReplaceAll), "###find"));
    add(pause(0.3));
    add(pruefe("Replace All: alle drei wiederverwendeten Stellungen, die andere bleibt", [=] {
        return vec(0) == "1300.000 -2700.000 1090.000" &&
               vec(5) == "1300.000 -2700.000 1090.000" &&
               vec(6) == "1300.000 -2700.000 1090.000" &&
               vec(3) == "1308.000 -2710.000 1080.000";
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.2));
    add(pruefe("EIN Strg+Z holt alle drei zurueck", [=] {
        return vec(0) == "1299.000 -2714.000 1082.000" &&
               vec(5) == "1299.000 -2714.000 1082.000" &&
               vec(6) == "1299.000 -2714.000 1082.000";
    }));
    // In selection: nur Zeile 6 waehlen
    add(tu("Zeile 6 waehlen, In selection an", [] {
        selectByPath(Path{6});
        g_app->findInSelection = true;
        fensterZeigen(g_app->findOpen, "###find");
    }));
    add(pause(0.3));
    add(klick(tr(Str::FindReplaceAll), "###find"));
    add(pause(0.3));
    add(pruefe("In selection: nur die gewaehlte Zeile", [=] {
        return vec(6) == "1300.000 -2700.000 1090.000" &&
               vec(0) == "1299.000 -2714.000 1082.000" &&
               vec(5) == "1299.000 -2714.000 1082.000";
    }));
    add(tu("Aufraeumen: In selection aus, Find zu", [] {
        g_app->findInSelection = false;
        g_app->findErsatz.clear();
        g_app->findOpen = false;
    }));
    add(pause(0.2));

    // ---- Kuerzel: Strg+F aufnehmen (gemeldet: "Ctrl+ModCtrl") ------------
    add(tu("Kuerzelfenster oeffnen", [] { fensterZeigen(g_app->keysOpen, "###keys"); }));
    add(pause(0.3));
    add(kuerzelZeileSuchen("Find"));
    add(kuerzelZeileKlicken("Find"));
    add(pause(0.2));
    add(pruefe("Die Find-Zeile wartet auf eine Taste", [] { return g_app->kuerzelWartet; }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_G, "Strg+G"));
    add(pause(0.2));
    add(pruefe("Strg+G wird als Strg+G aufgenommen, nicht als Ctrl+ModCtrl", [] {
        diag::detail(std::string("Knopftest: Find liegt jetzt auf ") +
                     ImGui::GetKeyChordName(kuerzelVon(keys::Action::Find)));
        return kuerzelVon(keys::Action::Find) == (ImGuiMod_Ctrl | ImGuiKey_G);
    }));
    // Aufnahme durch Klick ausserhalb abbrechen
    add(kuerzelZeileSuchen("Find"));
    add(kuerzelZeileKlicken("Find"));
    add(pause(0.2));
    add(klickAn("Klick ausserhalb", [] { return ausserhalbVon("###keys"); }));
    add(pause(0.25));
    add(pruefe("Klick ausserhalb bricht die Aufnahme ab", [] { return !g_app->kuerzelWartet; }));
    add(pruefe("... und laesst die Belegung stehen",
               [] { return kuerzelVon(keys::Action::Find) == (ImGuiMod_Ctrl | ImGuiKey_G); }));
    // Reset all to defaults
    add(tu("Kuerzelfenster nach vorn", [] { fensterZeigen(g_app->keysOpen, "###keys"); }));
    add(pause(0.3));
    // Der Knopf steht unter der langen Liste - ans Ende rollen.
    add(tu("Kuerzelfenster ans Ende rollen", [] {
        if (ImGuiWindow* w = ImGui::FindWindowByName("###keys")) {
            ImGui::SetScrollY(w, w->ScrollMax.y + 1000.0F);
        }
    }));
    add(pause(0.3));
    add(klick(tr(Str::KeysReset), "###keys"));
    add(pause(0.2));
    add(pruefe("Reset all to defaults: Find wieder auf Strg+F",
               [] { return kuerzelVon(keys::Action::Find) == (ImGuiMod_Ctrl | ImGuiKey_F); }));
    add(tu("Kuerzelfenster zu", [] { g_app->keysOpen = false; g_app->findOpen = false; }));
    add(pause(0.3));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_F, "Strg+F"));
    add(pause(0.3));
    add(pruefe("Nach Reset wirken die Kuerzel: Strg+F oeffnet Find",
               [] { return g_app->findOpen && fensterSichtbarVorn("###find"); }));
    add(tu("Find zu", [] { g_app->findOpen = false; }));
    add(pause(0.2));
    add(pruefe("Save all hat ein Kuerzel (Strg+Umschalt+S)", [] {
        return kuerzelVon(keys::Action::SaveAll) == (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S);
    }));
    add(pruefe("Move up/down haben ihre Kuerzel (Alt+Pfeil) fuer das Menue", [] {
        return kuerzelName(keys::Action::MoveUp) != nullptr &&
               kuerzelName(keys::Action::MoveDown) != nullptr;
    }));
    return s;
}

// --- Die Kamera: Zeitleiste, Gizmo, Zurueckschreiben (27.09.) --------------
//
// shank: "wenn ich sie selected habe und die timeline bewege, dann geht das
// nicht, und auch manchmal geht das write to script nicht richtig ... ich habe
// das Gefuehl, die wird bisschen buggy, wenn man da mehr dran rum macht".
//
// Eigenes Skript mit drei MOVE-Stellungen zu drei Zeiten; nur die erste hat
// ein eigenes PAN. Braucht eine geladene Karte (md_am_sith).
const char* const kKameraSkript =
    "//Generated by BehavEd\n"
    "camera ( /*@CAMERA_COMMANDS*/ ENABLE );\n"
    "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 0.000 6400.000 100.000 >, 0 );\n"
    "camera ( /*@CAMERA_COMMANDS*/ PAN, < 0.000 90.000 0.000 >, < 0.000 0.000 0.000 >, 0 );\n"
    "wait ( 2000.000 );\n"
    "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 200.000 6400.000 100.000 >, 1000 );\n"
    "wait ( 2000.000 );\n"
    "camera ( /*@CAMERA_COMMANDS*/ MOVE, < 400.000 6400.000 100.000 >, 1000 );\n"
    "wait ( 2000.000 );\n";

// Der Vektor der Zeile `zeile` (Oberebene), "" wenn es keinen gibt.
std::string kameraVektor(std::size_t zeile) {
    const auto& ns = g_app->doc.script().nodes;
    return (zeile < ns.size() && ns[zeile].args.size() > 1) ? ns[zeile].args[1].text : std::string();
}

std::vector<Schritt> kameraSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    add(tu("Kamerapruefung: Skript mit drei MOVE-Stellungen", [] {
        openScriptFromMemory(kKameraSkript, "kamera.txt");
        prepareScene();
        g_app->throughCamera = false;
        g_app->playing = false;
        g_app->playMs = 0.0;
        g_app->cameraSelected = false;
        g_app->selectedKey = -1;
        g_app->gizmoMode = App::GizmoMode::Select;
        for (int k = 0; k < 3; ++k) { g_app->gizmoOffset[k] = 0.0F; g_app->gizmoAngles[k] = 0.0F; }
        // Freie Kamera schraeg davor, alle drei Stellungen im Bild.
        g_app->cam.pos[0] = 200.0F; g_app->cam.pos[1] = 6000.0F; g_app->cam.pos[2] = 260.0F;
        g_app->cam.angles[0] = 20.0F; g_app->cam.angles[1] = 90.0F; g_app->cam.angles[2] = 0.0F;
        g_app->mapDirty = true;
    }));
    add(pause(0.8));
    add(gezeichnet("Kamerapruefung: Ausgangsbild"));
    add(pruefe("Drei Schluessel aus dem Skript, drei Klickplaetze", [] {
        return kameraSchluessel(g_app->doc.script()).size() == 3U;
    }));
    add(klickAn("auf die Kamera (Zeit 0)", [] { return bildZuSchirm(g_app->cameraSx, g_app->cameraSy); }));
    add(pause(0.3));
    add(pruefe("Kamera gewaehlt, der Schluessel der Zeit 0 (der erste)", [] {
        diag::detail("Knopftest: Kamera gewaehlt " + std::to_string(g_app->cameraSelected) + ", Schluessel " +
                     std::to_string(g_app->selectedKey) + ", keyMarks " + std::to_string(g_app->keyMarks.size()));
        return g_app->cameraSelected && g_app->selectedKey == 0 && g_app->keyMarks.size() == 3U;
    }));
    add(klickAn("rechts kurz (Gizmomenue)", ansichtMitte, 1));
    add(klick(tr(Str::GizmoMove), "##Popup"));
    add(pause(0.3));

    // ---- Die Zeitleiste bewegen: die Kamera geht mit, der Schluessel auch -
    auto sx0 = std::make_shared<float>(0.0F);
    add(tu("Kameraort bei 0 ms merken", [=] { *sx0 = g_app->cameraSx; }));
    struct Zeitpunkt { double ms; int schluessel; };
    for (const Zeitpunkt z : {Zeitpunkt{2500.0, 1}, Zeitpunkt{4500.0, 2}, Zeitpunkt{6000.0, 2},
                              Zeitpunkt{1000.0, 0}, Zeitpunkt{3500.0, 1}}) {
        add(tu("Zeitleiste auf " + std::to_string(static_cast<int>(z.ms)) + " ms",
               [=] { g_app->playMs = z.ms; g_app->mapDirty = true; }));
        add(pause(0.25));
        add(gezeichnet("Zeitleiste bewegt"));
        add(pruefe("Zeitleiste " + std::to_string(static_cast<int>(z.ms)) + " ms: Schluessel " +
                       std::to_string(z.schluessel) + " wird bearbeitet, die Kamera ist mitgegangen",
                   [=] {
                       diag::detail("Knopftest: bei " + std::to_string(static_cast<int>(z.ms)) + " ms Schluessel " +
                                    std::to_string(g_app->selectedKey) + ", Kamera x " +
                                    std::to_string(g_app->cameraSx) + " (bei 0 ms " + std::to_string(*sx0) + ")");
                       const bool bewegt = z.ms < 1500.0 || std::fabs(g_app->cameraSx - *sx0) > 10.0F;
                       return g_app->selectedKey == z.schluessel && bewegt;
                   }));
    }
    add(tu("Zeitleiste auf 2500 ms (Kamera faehrt zum zweiten Schluessel)",
           [] { g_app->playMs = 2500.0; g_app->mapDirty = true; }));
    add(pause(0.25));
    add(gezeichnet("unterwegs"));
    add(pruefe("Unterwegs: Geisterkamera und Linie zum Zielpunkt, dort sitzt das Gizmo", [] {
        const auto& k = g_app->keyMarks[1];
        return g_app->geisterKamera && g_app->selectedKey == 1 &&
               std::fabs(g_app->gizmoSx[0][0] - k.sx) < 3.0F;
    }));
    add(tu("Zeitleiste auf 6000 ms (Kamera steht am dritten Schluessel)",
           [] { g_app->playMs = 6000.0; g_app->mapDirty = true; }));
    add(pause(0.3));
    add(gezeichnet("am Ende"));
    add(pruefe("Im Stand: keine Geisterkamera (die Kamera steht selbst am Punkt)",
               [] { return !g_app->geisterKamera; }));
    add(pruefe("Die Kamera steht auf ihrem Schluessel, das Gizmo sitzt daran", [] {
        if (g_app->keyMarks.size() < 3U) { return false; }
        const auto& k = g_app->keyMarks[2];
        diag::detail("Knopftest: Kamera " + std::to_string(g_app->cameraSx) + "/" + std::to_string(g_app->cameraSy) +
                     ", Schluessel " + std::to_string(k.sx) + "/" + std::to_string(k.sy) + ", Gizmo " +
                     std::to_string(g_app->gizmoSx[0][0]) + "/" + std::to_string(g_app->gizmoSy[0][0]));
        return std::fabs(g_app->cameraSx - k.sx) < 3.0F && std::fabs(g_app->cameraSy - k.sy) < 3.0F &&
               g_app->gizmoShown && std::fabs(g_app->gizmoSx[0][0] - k.sx) < 3.0F;
    }));

    // ---- Echte Zuege mit der Maus am Gizmo ---------------------------------
    //
    // Gegriffen wird bei zwei Dritteln der Achse (nicht an der Spitze, dort
    // liegt der Ring des Drehmodus nicht, aber der Rand des Kaestchens).
    auto achsenGriff = [](int ax) {
        return [ax] {
            const float x = g_app->gizmoSx[ax][0] + (g_app->gizmoSx[ax][1] - g_app->gizmoSx[ax][0]) * 0.66F;
            const float y = g_app->gizmoSy[ax][0] + (g_app->gizmoSy[ax][1] - g_app->gizmoSy[ax][0]) * 0.66F;
            return bildZuSchirm(x, y);
        };
    };
    auto zVorher = std::make_shared<float>(0.0F);
    add(ziehenAn("an der Z-Achse 60 Punkte hoch", achsenGriff(2), 0.0F, -60.0F, 0));
    add(pause(0.2));
    add(pruefe("Zug an Z: nur Z bewegt sich, nach oben", [=] {
        diag::detail("Knopftest: nach Zug Z Versatz " + std::to_string(g_app->gizmoOffset[0]) + " " +
                     std::to_string(g_app->gizmoOffset[1]) + " " + std::to_string(g_app->gizmoOffset[2]));
        *zVorher = g_app->gizmoOffset[2];
        return g_app->gizmoOffset[2] > 5.0F && g_app->gizmoOffset[0] == 0.0F && g_app->gizmoOffset[1] == 0.0F &&
               g_app->gizmoAxis < 0;
    }));
    add(gezeichnet("nach dem ersten Zug"));
    add(ziehenAn("noch einmal an Z, 40 Punkte hoch", achsenGriff(2), 0.0F, -40.0F, 0));
    add(pause(0.2));
    add(pruefe("Zweiter Zug addiert sich (kein Zuruecksetzen, kein Wegdriften)", [=] {
        return g_app->gizmoOffset[2] > *zVorher + 3.0F && g_app->gizmoOffset[0] == 0.0F &&
               g_app->gizmoOffset[1] == 0.0F;
    }));
    add(gezeichnet("nach dem zweiten Zug"));
    add(ziehenAn("an Z wieder 100 Punkte hinunter", achsenGriff(2), 0.0F, 100.0F, 0));
    add(pause(0.2));
    add(pruefe("Zurueckziehen fuehrt zurueck (etwa auf null, nicht darueber hinaus gesperrt)", [=] {
        diag::detail("Knopftest: nach Zurueckziehen Z " + std::to_string(g_app->gizmoOffset[2]));
        return g_app->gizmoOffset[2] < *zVorher;
    }));
    add(gezeichnet("zurueckgezogen"));
    auto xyVorher = std::make_shared<float>(0.0F);
    add(tu("Stand merken", [=] { *xyVorher = g_app->gizmoOffset[2]; }));
    add(ziehenAn("an der X-Achse entlang", [=] {
            const float dx = g_app->gizmoSx[0][1] - g_app->gizmoSx[0][0];
            const float dy = g_app->gizmoSy[0][1] - g_app->gizmoSy[0][0];
            (void)dx; (void)dy;
            return achsenGriff(0)();
        }, 50.0F, 0.0F, 0));
    add(pause(0.2));
    add(pruefe("Zug an X: X bewegt sich, Y bleibt, Z bleibt stehen", [=] {
        diag::detail("Knopftest: nach Zug X Versatz " + std::to_string(g_app->gizmoOffset[0]) + " " +
                     std::to_string(g_app->gizmoOffset[1]) + " " + std::to_string(g_app->gizmoOffset[2]));
        return std::fabs(g_app->gizmoOffset[0]) > 1.0F && g_app->gizmoOffset[1] == 0.0F &&
               g_app->gizmoOffset[2] == *xyVorher;
    }));
    add(gezeichnet("nach Zug an X"));
    add(ziehenAn("am Kaestchen in der Mitte (frei in der Bildebene)",
                 [] { return bildZuSchirm(g_app->gizmoSx[0][0], g_app->gizmoSy[0][0]); }, 30.0F, -30.0F, 0));
    add(pause(0.2));
    add(pruefe("Das Kaestchen verschiebt in der Bildebene, der Zug endet sauber", [=] {
        diag::detail("Knopftest: nach Zug Kaestchen " + std::to_string(g_app->gizmoOffset[0]) + " " +
                     std::to_string(g_app->gizmoOffset[1]) + " " + std::to_string(g_app->gizmoOffset[2]));
        return g_app->gizmoAxis < 0 && (g_app->gizmoOffset[1] != 0.0F || g_app->gizmoOffset[2] != *xyVorher);
    }));
    add(gezeichnet("nach Zug am Kaestchen"));
    add(klick(tr(Str::GizmoReset)));
    add(pause(0.2));
    add(pruefe("Reset verwirft die Vorschau ganz", [] {
        return g_app->gizmoOffset[0] == 0.0F && g_app->gizmoOffset[1] == 0.0F && g_app->gizmoOffset[2] == 0.0F;
    }));
    add(pruefe("... und am Skript hat sich nichts geaendert", [] {
        return kameraVektor(6) == "400.000 6400.000 100.000";
    }));

    // ---- Eine ungeschriebene Aenderung bleibt am Schluessel -------------
    add(tu("Dritten Schluessel um 50 nach oben ziehen (Vorschau)", [] {
        g_app->gizmoOffset[2] = 50.0F;
        g_app->mapDirty = true;
    }));
    add(tu("Zeitleiste zurueck auf 500 ms", [] { g_app->playMs = 500.0; g_app->mapDirty = true; }));
    add(pause(0.3));
    add(pruefe("Mit ungeschriebener Aenderung bleibt der Schluessel (nichts geht verloren)", [] {
        return g_app->selectedKey == 2 && g_app->gizmoOffset[2] == 50.0F;
    }));
    add(klick(tr(Str::GizmoApply)));
    add(pause(0.2));
    add(pruefe("Write to script trifft den DRITTEN MOVE, die anderen bleiben", [] {
        diag::detail("Knopftest: MOVE1 \"" + kameraVektor(1) + "\", MOVE2 \"" + kameraVektor(4) + "\", MOVE3 \"" +
                     kameraVektor(6) + "\"");
        return kameraVektor(6) == "400.000 6400.000 150.000" && kameraVektor(1) == "0.000 6400.000 100.000" &&
               kameraVektor(4) == "200.000 6400.000 100.000" && g_app->gizmoOffset[2] == 0.0F;
    }));
    add(tu("Zeitleiste auf 2500 ms", [] { g_app->playMs = 2500.0; g_app->mapDirty = true; }));
    add(pause(0.3));
    add(pruefe("Nach dem Schreiben geht der Schluessel wieder mit der Zeit (zweiter)",
               [] { return g_app->selectedKey == 1; }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.2));
    add(pruefe("Strg+Z nimmt das Schreiben zurueck", [] { return kameraVektor(6) == "400.000 6400.000 100.000"; }));

    // ---- Ein Schluessel HINTER der Kamera verschiebt die Nummern nicht ----
    // Schraeg zur Bahn, damit der zweite und dritte Schluessel nicht auf
    // denselben Bildpunkt fallen; der erste liegt hinter der Kamera.
    add(tu("Freie Kamera neben die Bahn, Blick schraeg nach vorn (erster dahinter)", [] {
        g_app->cam.pos[0] = 100.0F; g_app->cam.pos[1] = 6250.0F; g_app->cam.pos[2] = 140.0F;
        g_app->cam.angles[0] = 10.0F; g_app->cam.angles[1] = 30.0F; g_app->cam.angles[2] = 0.0F;
        g_app->mapDirty = true;
    }));
    add(pause(0.4));
    add(gezeichnet("erster Schluessel hinter der Kamera"));
    add(pruefe("Der erste Schluessel liegt ausserhalb, sein Platz bleibt (3 Klickplaetze)", [] {
        return g_app->keyMarks.size() == 3U && !g_app->keyMarks[0].imBild && g_app->keyMarks[2].imBild;
    }));
    add(klickAn("auf den dritten Schluessel", [] {
        const auto& k = g_app->keyMarks[2];
        return bildZuSchirm(k.sx, k.sy);
    }));
    add(pause(0.3));
    add(gezeichnet("dritter Schluessel gewaehlt"));
    add(pruefe("Angeklickt: der DRITTE Schluessel, und das Gizmo sitzt an ihm", [] {
        const auto& k = g_app->keyMarks[2];
        diag::detail("Knopftest: gewaehlt " + std::to_string(g_app->selectedKey) + ", Gizmo " +
                     std::to_string(g_app->gizmoSx[0][0]) + ", Schluessel " + std::to_string(k.sx));
        return g_app->selectedKey == 2 && std::fabs(g_app->gizmoSx[0][0] - k.sx) < 3.0F;
    }));
    add(tu("Vorschau: 30 nach oben", [] { g_app->gizmoOffset[2] = 30.0F; g_app->mapDirty = true; }));
    add(pause(0.2));
    add(klick(tr(Str::GizmoApply)));
    add(pause(0.2));
    add(pruefe("Geschrieben in den DRITTEN MOVE (vorher: in einen anderen oder gar nicht)", [] {
        return kameraVektor(6) == "400.000 6400.000 130.000" && kameraVektor(4) == "200.000 6400.000 100.000";
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.2));

    // ---- Drehen: Schluessel ohne eigenes PAN -> PAN wird eingefuegt -------
    add(klickAn("rechts kurz (Gizmomenue)", ansichtMitte, 1));
    add(klick(tr(Str::GizmoRotate), "##Popup"));
    add(pause(0.2));
    add(tu("Dritter Schluessel, 30 Grad gieren (Vorschau)", [] {
        g_app->selectedKey = 2;
        g_app->gizmoZeitBezug = g_app->playMs;
        g_app->gizmoAngles[1] = 30.0F;
        g_app->mapDirty = true;
    }));
    add(pause(0.2));
    add(klick(tr(Str::GizmoApply)));
    add(pause(0.2));
    add(pruefe("Ohne eigenes PAN: eins direkt hinter dem MOVE, Blick 90 + 30 = 120", [] {
        const auto& ns = g_app->doc.script().nodes;
        const bool ok = ns.size() > 7 && ns[7].name == "camera" && ns[7].args.size() >= 4 &&
                        ns[7].args[0].text == "PAN" && ns[7].args[1].text == "0.000 120.000 0.000";
        diag::detail("Knopftest: nach dem Drehen Zeile 7 = " +
                     (ns.size() > 7 ? ns[7].name + " " + (ns[7].args.size() > 1 ? ns[7].args[0].text + " " + ns[7].args[1].text : "") : "-"));
        return ok && g_app->gizmoAngles[1] == 0.0F;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.2));
    add(pruefe("Strg+Z nimmt das eingefuegte PAN wieder heraus", [] {
        return g_app->doc.script().nodes.size() == 8U;
    }));
    add(tu("Erster Schluessel, 10 Grad nicken (hat ein eigenes PAN)", [] {
        g_app->selectedKey = 0;
        g_app->gizmoZeitBezug = g_app->playMs;
        g_app->gizmoAngles[0] = 10.0F;
        g_app->mapDirty = true;
    }));
    add(pause(0.2));
    add(klick(tr(Str::GizmoApply)));
    add(pause(0.2));
    add(pruefe("Mit eigenem PAN: genau dieses PAN wird geaendert", [] {
        return kameraVektor(2) == "10.000 90.000 0.000" && g_app->doc.script().nodes.size() == 8U;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(pause(0.2));
    add(klickAn("rechts kurz (Gizmomenue)", ansichtMitte, 1));
    add(klick(tr(Str::GizmoMove), "##Popup"));
    add(pause(0.2));

    // ---- Zurueckschreiben beim Blick durch die Kamera ---------------------
    add(tu("Through camera an, zweiter Schluessel, Vorschau 20 nach +y", [] {
        g_app->throughCamera = true;
        g_app->selectedKey = 1;
        g_app->gizmoZeitBezug = g_app->playMs;
        g_app->gizmoOffset[1] = 20.0F;
        g_app->mapDirty = true;
    }));
    add(pause(0.3));
    add(gezeichnet("durch die Kamera"));
    add(klick(tr(Str::GizmoApply)));
    add(pause(0.2));
    add(pruefe("Write to script geht auch beim Blick durch die Kamera (keyMarks sind dort leer)", [] {
        diag::detail("Knopftest: durch die Kamera, keyMarks " + std::to_string(g_app->keyMarks.size()) +
                     ", MOVE2 \"" + kameraVektor(4) + "\"");
        return kameraVektor(4) == "200.000 6420.000 100.000";
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(tu("Through camera aus", [] { g_app->throughCamera = false; g_app->mapDirty = true; }));
    add(pause(0.3));

    // ---- Abspielen: der Schluessel laeuft mit ------------------------------
    add(tu("Von 1800 ms an abspielen", [] { g_app->playMs = 1800.0; g_app->playing = true; }));
    add(pause(1.2));
    add(tu("Anhalten", [] { g_app->playing = false; g_app->mapDirty = true; }));
    add(pause(0.3));
    add(pruefe("Nach dem Abspielen steht der Schluessel der neuen Zeit", [] {
        diag::detail("Knopftest: nach dem Abspielen " + std::to_string(static_cast<int>(g_app->playMs)) +
                     " ms, Schluessel " + std::to_string(g_app->selectedKey));
        return g_app->playMs > 2000.0 && g_app->selectedKey == schluesselFuerZeit(g_app->playMs) &&
               g_app->selectedKey >= 1;
    }));

    // ---- Die bearbeitete Zeile verschwindet --------------------------------
    add(tu("Dritten MOVE loeschen, waehrend er bearbeitet wird", [] {
        g_app->playMs = 6000.0;
        g_app->gizmoZeitBezug = -1.0;
        g_app->mapDirty = true;
    }));
    add(pause(0.3));
    add(tu("... jetzt loeschen", [] {
        (void)g_app->doc.removeAt(Path{6});
        rebuildTree();
        prepareScene();
        g_app->mapDirty = true;
    }));
    add(pause(0.4));
    add(gezeichnet("nach dem Loeschen"));
    add(pruefe("Kein Absturz, der Schluessel zeigt auf einen, den es gibt", [] {
        return g_app->selectedKey >= 0 && g_app->selectedKey < 2;
    }));
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z"));
    add(tu("Kamerapruefung aufraeumen", [] {
        g_app->gizmoMode = App::GizmoMode::Select;
        g_app->cameraSelected = false;
        g_app->selectedKey = -1;
        g_app->doc.markSaved();
        g_app->mapDirty = true;
    }));
    add(pause(0.3));
    return s;
}

// --- Abläufe mit Vorgeschichte (BHED_EDITORTEST=ablauf) ---------------------
//
// Der Texturfehler vom 27.09. ("alle Texturen oder UV-Maps falsch") trat erst
// ab der ZWEITEN Karte auf; jeder Test, der frisch lud, sah ihn nicht. Hier
// wird eine Karte frisch geladen und fotografiert (Bild + Kennzahlen), dann
// laufen Folgen von Schritten - Kartenwechsel hin und zurueck, neu laden,
// Detail, Himmel, Spielordner neu einlesen, Reiter mit eigener Karte,
// Bearbeiten und Undo ueber einen Kartenwechsel - und nach jeder muss das
// Bild dem ersten gleichen. Effekte und Figuren sind aus, die Zeit steht auf
// 0: dann ist das Bild bestimmt.
namespace {
struct AblaufBild {
    std::vector<std::uint8_t> px;
    int w = 0;
    int h = 0;
    std::string kennzahlen;
};

std::string ablaufKennzahlen() {
    const gpu::SpeicherStand sp = gpu::speicherStand();
    char z[400];
    std::snprintf(z, sizeof(z),
                  "Karte %s, Dreiecke %zu, Texturen %d/%d, Modelle %zu, Brush-Netze %zu, Modellnetze %zu, "
                  "Effekte %zu, Lightmaps %zu",
                  g_app->map.path.c_str(), g_app->mesh.indexes.size() / 3U, g_app->textures.found,
                  g_app->textures.found + g_app->textures.missing, g_app->mapModels.size(),
                  g_app->brushMeshes.size(), g_app->md3Meshes.size(), g_app->effectRunners.size(),
                  g_app->geo.lightmaps.size());
    (void)sp;
    return z;
}
}  // namespace

std::vector<Schritt> ablaufSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    const std::string kA = "maps/duel_carbon.bsp";      // externe Lightmaps, Mover
    const std::string kB = "maps/hoth2.bsp";            // viele Kartenmodelle, Himmel
    const std::string kC = "maps/md_ga_jedi.bsp";       // Modelle, Effekte, ref_tags
    auto ort = std::make_shared<std::array<float, 3>>(std::array<float, 3>{0, 0, 0});
    auto ref = std::make_shared<AblaufBild>();

    // Karte laden (wie der Benutzer: danach baut die Zeitleiste die Szene).
    auto laden = [&](const std::string& karte) {
        add(tu("Ablauf: " + karte + " laden", [=] {
            g_app->leftMode = 1;   // die Szene baut die sichtbare Kartenansicht
            if (!loadMapFromArchive(karte)) {
                diag::detail("Ablauf: " + karte + " nicht geladen");
            }
        }));
        add(warteBis("Karte bereit", [] { return g_app->mapBereit; }, 900));
        add(warteBis("Szene aufgebaut", [] { return g_app->camTrackValid; }, 900));
    };
    // Immer dieselbe Ansicht, zwei Bilder (das erste nach einem Umbau sagt weniger).
    auto ansicht = [&](const std::string& was) {
        add(tu("Ablauf: Ansicht fuer " + was, [=] {
            g_app->leftMode = 1;
            g_app->throughCamera = false;
            g_app->showEffects = false;
            g_app->showActors = false;
            g_app->playing = false;
            g_app->playMs = 0;
            for (int k = 0; k < 3; ++k) { g_app->cam.pos[k] = (*ort)[static_cast<std::size_t>(k)]; }
            g_app->cam.angles[0] = 10.0F;
            g_app->cam.angles[1] = 90.0F;
            g_app->cam.angles[2] = 0.0F;
            g_app->mapDirty = true;
        }));
        add(gezeichnet(was));
        add(tu("Ablauf: noch ein Bild", [] { g_app->mapDirty = true; }));
        add(gezeichnet(was + " (zweites Bild)"));
    };
    auto vergleiche = [&](const std::string& was) {
        add(pruefe("Ablauf: " + was + " - Bild und Kennzahlen wie frisch geladen", [=] {
            AblaufBild jetzt;
            if (!gpu::leseFarbe(jetzt.px, jetzt.w, jetzt.h)) {
                diag::detail("Ablauf: " + was + " - Bild nicht lesbar");
                return false;
            }
            const std::string kz = ablaufKennzahlen();
            if (jetzt.w != ref->w || jetzt.h != ref->h || ref->px.empty()) {
                diag::detail("Ablauf: " + was + " - Bildgroesse anders");
                return false;
            }
            double summe = 0.0;
            std::size_t anders = 0;
            const std::size_t n = static_cast<std::size_t>(jetzt.w) * static_cast<std::size_t>(jetzt.h);
            for (std::size_t i = 0; i < n; ++i) {
                int groesste = 0;
                for (int k = 0; k < 3; ++k) {
                    const int d = std::abs(static_cast<int>(jetzt.px[i * 4U + static_cast<std::size_t>(k)]) -
                                           static_cast<int>(ref->px[i * 4U + static_cast<std::size_t>(k)]));
                    summe += d;
                    groesste = std::max(groesste, d);
                }
                if (groesste > 24) { ++anders; }
            }
            const double mittel = summe / (static_cast<double>(n) * 3.0);
            const double anteil = 100.0 * static_cast<double>(anders) / static_cast<double>(n);
            char z[300];
            std::snprintf(z, sizeof(z), "Ablauf: %s | mittlere Abweichung %.3f, %.3f %% der Bildpunkte deutlich anders",
                          was.c_str(), mittel, anteil);
            diag::detail(z);
            const bool gleicheZahlen = kz == ref->kennzahlen;
            if (!gleicheZahlen) {
                diag::detail("Ablauf: " + was + " | Kennzahlen jetzt: " + kz);
                diag::detail("Ablauf: " + was + " | Kennzahlen frisch: " + ref->kennzahlen);
            }
            if (const char* ordner = std::getenv("BHED_FOTOS")) {
                (void)ordner;
            }
            return gleicheZahlen && mittel < 0.5 && anteil < 0.2 && gpu::veralteteKartenBilder() == 0 &&
                   g_app->gpuFehler.empty();
        }));
    };

    // --- 0. Frisch: A laden, Ort bestimmen, fotografieren -------------------
    laden(kA);
    add(tu("Ablauf: Ort am Startpunkt", [=] {
        for (const MapEntity& e : g_app->map.entities) {
            if ((e.classname == "info_player_start" || e.classname == "info_player_deathmatch") && !e.origin.empty()) {
                (void)std::sscanf(e.origin.c_str(), "%f %f %f", &(*ort)[0], &(*ort)[1], &(*ort)[2]);
                (*ort)[2] += 48.0F;
                break;
            }
        }
    }));
    ansicht("frisch");
    add(pruefe("Ablauf: frisches Bild gelesen", [=] {
        ref->kennzahlen = ablaufKennzahlen();
        diag::detail("Ablauf: frisch | " + ref->kennzahlen);
        return gpu::leseFarbe(ref->px, ref->w, ref->h) && !ref->px.empty() && g_app->gpuFehler.empty();
    }));
    // Bestimmtheit: dasselbe Bild noch einmal, ohne etwas zu tun.
    ansicht("ohne Aenderung");
    vergleiche("ohne Aenderung");

    // --- 1. Kartenwechsel hin und zurueck ------------------------------------
    laden(kB);
    ansicht("B");
    laden(kC);
    ansicht("C");
    laden(kA);
    ansicht("zurueck zu A");
    vergleiche("A -> B -> C -> A");

    // --- 2. Dieselbe Karte neu laden ------------------------------------------
    laden(kA);
    ansicht("A neu geladen");
    vergleiche("A neu geladen");

    // --- 3. Detail runter und wieder hoch -------------------------------------
    auto detail0 = std::make_shared<int>(0);
    add(tu("Ablauf: Detail merken", [=] { *detail0 = g_app->meshDetail; }));
    add(ziehenAn("Detail-Regler nach links", [] {
        const Element* e = finde(tr(Str::MapDetail), "", 0, false);
        return e != nullptr ? ImVec2{e->rect.Max.x - 4.0F, e->rect.GetCenter().y} : ImVec2{0, 0};
    }, -400.0F, 0.0F, 0));
    ansicht("Detail runter");
    add(ziehenAn("Detail-Regler nach rechts", [] {
        const Element* e = finde(tr(Str::MapDetail), "", 0, false);
        return e != nullptr ? ImVec2{e->rect.Min.x + 4.0F, e->rect.GetCenter().y} : ImVec2{0, 0};
    }, 400.0F, 0.0F, 0));
    ansicht("Detail wieder hoch");
    add(pruefe("Ablauf: Detail wieder auf dem Anfangswert", [=] {
        diag::detail("Ablauf: Detail " + std::to_string(*detail0) + " -> " + std::to_string(g_app->meshDetail));
        return g_app->meshDetail == *detail0;
    }));
    vergleiche("Detail runter und hoch");

    // --- 4. Himmel aus und an -------------------------------------------------
    add(tu("Ablauf: Himmel ist an", [] { g_app->showSky = true; }));
    add(klick(tr(Str::MapSky)));
    ansicht("Himmel aus");
    add(klick(tr(Str::MapSky)));
    ansicht("Himmel wieder an");
    vergleiche("Himmel aus und an");

    // --- 5. Spielordner neu einlesen -----------------------------------------
    add(tu("Ablauf: Spielordner neu einlesen", [] { rescanGamePaths(); }));
    add(warteBis("Szene aufgebaut", [] { return g_app->camTrackValid; }, 900));
    ansicht("Spielordner neu");
    vergleiche("Spielordner neu eingelesen");

    // --- 6. Szene mehrmals neu aufbauen (wie nach Skriptaenderungen) ----------
    auto netze0 = std::make_shared<int>(0);
    add(tu("Ablauf: Mover-Puffer merken", [=] { *netze0 = gpu::speicherStand().moverNetze; }));
    for (int i = 0; i < 5; ++i) {
        add(tu("Ablauf: Szene neu " + std::to_string(i + 1), [] { g_app->camTrackValid = false; g_app->mapDirty = true; }));
        add(warteBis("Szene aufgebaut", [] { return g_app->camTrackValid; }, 900));
        add(gezeichnet("Szene neu " + std::to_string(i + 1)));
    }
    ansicht("nach fuenf Szenenaufbauten");
    add(pruefe("Ablauf: fuenf Szenenaufbauten - Mover-Puffer wachsen nicht", [=] {
        const int n = gpu::speicherStand().moverNetze;
        diag::detail("Ablauf: Mover-Puffer " + std::to_string(*netze0) + " -> " + std::to_string(n));
        return n <= *netze0;
    }));
    vergleiche("fuenf Szenenaufbauten");

    // --- 7. Bearbeiten, Kartenwechsel, Undo ---------------------------------
    auto knoten0 = std::make_shared<std::size_t>(0);
    add(tu("Ablauf: Befehl einfuegen", [=] {
        *knoten0 = g_app->doc.script().nodes.size();
        Node n;
        n.kind = Node::Kind::Command;
        n.name = "wait";
        Arg a;
        a.kind = Arg::Kind::Number;
        a.text = "100";
        n.args.push_back(a);
        (void)g_app->doc.insertAfter(Path{}, n);
        rebuildTree();
    }));
    laden(kB);
    ansicht("B nach Bearbeiten");
    add(tasteMit(ImGuiMod_Ctrl, ImGuiKey_Z, "Strg+Z (Undo)"));
    add(pause(0.3));
    laden(kA);
    ansicht("A nach Undo");
    add(pruefe("Ablauf: Undo nimmt den Befehl zurueck, auch ueber den Kartenwechsel", [=] {
        return g_app->doc.script().nodes.size() == *knoten0;
    }));
    vergleiche("Bearbeiten, Kartenwechsel, Undo");

    // --- 8. Reiter mit eigener Karte ------------------------------------------
    auto reiter0 = std::make_shared<int>(0);
    add(tu("Ablauf: Reiter merken", [=] { *reiter0 = g_app->activeTab; }));
    add(tu("Ablauf: neuer Reiter", [] { neuerReiter(); }));
    add(pause(0.5));
    laden(kB);
    ansicht("B im neuen Reiter");
    add(tu("Ablauf: zurueck zum ersten Reiter", [=] { g_app->tabRequest = *reiter0; }));
    add(pause(1.0));
    add(warteBis("Karte bereit", [] { return g_app->mapBereit; }, 900));
    add(warteBis("Szene aufgebaut", [] { return g_app->camTrackValid; }, 900));
    ansicht("erster Reiter wieder");
    add(pruefe("Ablauf: der erste Reiter hat seine Karte zurueck", [=] {
        diag::detail("Ablauf: Reiter " + std::to_string(g_app->activeTab) + ", Karte " + g_app->map.path);
        return g_app->activeTab == *reiter0 && g_app->map.path == kA;
    }));
    vergleiche("Reiterwechsel mit eigener Karte");
    add(tu("Ablauf: fertig", [] {
        g_app->showEffects = true;
        g_app->showActors = true;
    }));
    return s;
}

// --- JEDE Karte laden und pruefen (BHED_EDITORTEST=allekarten) --------------
//
// shank: "ist die map richtig rein geladen? hast du das geprueft so wie bei
// den models?". tests/meshmasse prueft die GEOMETRIE aller Karten; dieser
// Lauf nimmt den echten Weg des Programms - Entities, Texturen und Shader,
// Kartenmodelle, Effekte, Zeichnen ueber die Grafikkarte - fuer jede .bsp in
// den Archiven.
//
// Fehler (von behaved): laedt nicht, zeichnet nicht, Direct3D beschwert sich,
// oder eine "fehlende" Textur / ein "fehlendes" Modell liegt DOCH in einem
// Archiv. Was wirklich in keinem Archiv liegt, ist ein Befund ueber die
// Installation, kein Fehler von behaved - es wird aufgelistet.
bool liegtInEinemArchiv(const std::string& pfad, const std::vector<std::string>& endungen) {
    std::string ohne = pfad;
    for (char& c : ohne) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    for (char& c : ohne) { if (c == '\\') { c = '/'; } }
    const std::size_t punkt = ohne.find_last_of('.');
    const std::size_t strich = ohne.find_last_of('/');
    if (punkt != std::string::npos && (strich == std::string::npos || punkt > strich)) {
        ohne = ohne.substr(0, punkt);
    }
    for (const GamePath& gp : g_app->gamePaths) {
        for (const Pk3& a : gp.archives) {
            for (const std::string& e : endungen) {
                if (a.find(ohne + e) != nullptr) {
                    return true;
                }
            }
        }
    }
    return false;
}

std::string speicherZeile(const gpu::SpeicherStand& s) {
    char z[300];
    std::snprintf(z, sizeof(z),
                  "Bilder %d, Lightmaps %d, Mover-Netze %d (Karte %zu + %zu Modelle), Figuren %d, Figurenbilder %d, "
                  "Grafik %.1f MB, Prozess %.1f MB",
                  s.bilder, s.lightmaps, s.moverNetze, g_app->brushMeshes.size(), g_app->md3Meshes.size(), s.figuren,
                  s.figurBilder,
                  static_cast<double>(s.grafikBytes) / 1048576.0, static_cast<double>(s.prozessBytes) / 1048576.0);
    std::size_t texBytes = 0;
    for (const auto& [k, tx] : g_app->texCache) {
        texBytes += tx.rgba.size();
        for (const auto& m : tx.mips) { texBytes += m.rgba.size(); }
    }
    std::size_t klangBytes = 0;
    for (const auto& [k, kp] : g_app->klangPuffer) { klangBytes += kp.size() * 2U; }
    char z2[300];
    std::snprintf(z2, sizeof(z2), " | texCache %zu (%.1f MB), modelCache %zu, soundCache %zu, klangPuffer %.1f MB, effects %zu",
                  g_app->texCache.size(), static_cast<double>(texBytes) / 1048576.0, g_app->modelCache.size(),
                  g_app->soundCache.size(), static_cast<double>(klangBytes) / 1048576.0, g_app->effects.size());
    return std::string(z) + z2;
}

std::vector<Schritt> alleKartenSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    auto stand = std::make_shared<std::map<std::string, int>>();
    add(tu("Alle Karten: Kartenansicht links", [] { g_app->leftMode = 1; }));
    add(pause(0.3));
    add({"Alle Karten: Liste sammeln", [=](int) {
             std::set<std::string> gesehen;
             std::vector<std::string> liste;
             for (const GamePath& gp : g_app->gamePaths) {
                 for (const FoundFile& f : findByExtension(gp, {".bsp"})) {
                     std::string k = f.name;
                     for (char& c : k) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                     if (gesehen.insert(k).second) { liste.push_back(f.name); }
                 }
             }
             std::sort(liste.begin(), liste.end());
             // BHED_KARTEN="duel_carbon,duel_tower": nur Karten, deren Name
             // eines davon enthaelt - zum Nachpruefen einzelner Befunde.
             if (const char* nur = std::getenv("BHED_KARTEN")) {
                 std::vector<std::string> teile;
                 std::istringstream is(nur);
                 std::string teil;
                 while (std::getline(is, teil, ',')) {
                     if (!teil.empty()) { teile.push_back(teil); }
                 }
                 std::vector<std::string> gefiltert;
                 for (const std::string& k : liste) {
                     for (const std::string& tl : teile) {
                         if (k.find(tl) != std::string::npos) { gefiltert.push_back(k); break; }
                     }
                 }
                 liste = std::move(gefiltert);
             }
             // BHED_RUNDEN=2: dieselbe Liste mehrmals - waechst der Speicher in
             // der zweiten Runde weiter, ist es ein Leck; sonst nur der Heap,
             // der nach der groessten Karte nicht alles zurueckgibt.
             if (const char* r = std::getenv("BHED_RUNDEN")) {
                 const int runden = std::max(1, std::atoi(r));
                 const std::vector<std::string> eine = liste;
                 for (int i = 1; i < runden; ++i) { liste.insert(liste.end(), eine.begin(), eine.end()); }
             }
             diag::detail("Kartenpruefung: " + std::to_string(liste.size()) + " Karten");
             std::vector<Schritt> neu;
             for (const std::string& karte : liste) {
                 auto meld = std::make_shared<std::size_t>(0);
                 neu.push_back(tu("Karte " + karte + " laden", [=] {
                     *meld = g_app->d3dMeldungen.size();
                     if (!loadMapFromArchive(karte)) {
                         diag::detail("Kartenpruefung: " + karte + " - loadMapFromArchive gescheitert");
                     }
                     // Die freie Kamera an den Startpunkt.
                     for (const MapEntity& e : g_app->map.entities) {
                         if ((e.classname == "info_player_start" || e.classname == "info_player_deathmatch") &&
                             !e.origin.empty()) {
                             float o[3] = {0, 0, 0};
                             (void)std::sscanf(e.origin.c_str(), "%f %f %f", &o[0], &o[1], &o[2]);
                             g_app->cam.pos[0] = o[0];
                             g_app->cam.pos[1] = o[1];
                             g_app->cam.pos[2] = o[2] + 48.0F;
                             break;
                         }
                     }
                     g_app->throughCamera = false;
                     g_app->mapDirty = true;
                 }));
                 neu.push_back(warteBis("Karte bereit", [] { return g_app->mapBereit; }, 900));
                 // Die Szene (Kartenmodelle, Effekte) baut die Zeitleiste auf,
                 // sobald camTrackValid fehlt - wie beim Benutzer.
                 neu.push_back(warteBis("Szene aufgebaut", [] { return g_app->camTrackValid; }, 900));
                 neu.push_back(gezeichnet(karte));
                 // Noch ein Bild: das erste nach dem Laden sagt ueber die
                 // Karte weniger als ein ruhiges.
                 neu.push_back(tu("Karte " + karte + ": erstes Bild", [=] {
                     if (!g_app->gpuFehler.empty()) {
                         diag::detail("Kartenpruefung: " + karte + " | erstes Bild: \"" + g_app->gpuFehler + "\"");
                     }
                     g_app->mapDirty = true;
                 }));
                 neu.push_back(gezeichnet(karte + " (zweites Bild)"));
                 neu.push_back(pruefe("Karte " + karte + ": geladen, gezeichnet, ohne Beschwerde", [=] {
                     int fehlendeInArchiv = 0;
                     std::string liste1;
                     for (const std::string& tx : g_app->fehlendeTexturen) {
                         std::string k = tx;
                         for (char& c : k) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                         if (k == "noshader" || k.rfind("textures/system/", 0) == 0) { continue; }
                         const bool da = liegtInEinemArchiv(tx, {".tga", ".jpg", ".jpeg", ".png", ".dds"});
                         if (da) { ++fehlendeInArchiv; }
                         if (liste1.size() < 300) { liste1 += (da ? " [DA!] " : " ") + tx; }
                         ++(*stand)["Textur fehlt"];
                     }
                     std::string liste2;
                     for (const std::string& md : g_app->fehlendeModelle) {
                         const bool da = liegtInEinemArchiv(md, {".md3", ".glm"});
                         if (da) { ++fehlendeInArchiv; }
                         if (liste2.size() < 300) { liste2 += (da ? " [DA!] " : " ") + md; }
                         ++(*stand)["Modell fehlt"];
                     }
                     const std::size_t neueMeldungen = g_app->d3dMeldungen.size() - *meld;
                     if (!g_app->mapBereit || g_app->geo.empty() || !g_app->gpuFehler.empty() ||
                         fehlendeInArchiv != 0) {
                         diag::detail("Kartenpruefung: " + karte + " | bereit " +
                                      std::to_string(g_app->mapBereit ? 1 : 0) + ", Geometrie " +
                                      std::to_string(g_app->geo.empty() ? 0 : 1) + ", im Archiv doch da " +
                                      std::to_string(fehlendeInArchiv) + ", GPU-Fehler \"" +
                                      g_app->gpuFehler + "\"");
                     }
                     char z[900];
                     std::snprintf(z, sizeof(z),
                                   "Kartenpruefung: %s | %zu Entities, %zu Dreiecke, Texturen %d/%d, "
                                   "Modelle %zu (fehlen %zu), Effekte %zu, GPU %d Aufrufe, %d uebersprungen, "
                                   "D3D-Meldungen %zu%s%s%s%s",
                                   karte.c_str(), g_app->map.entities.size(), g_app->mesh.indexes.size() / 3U,
                                   g_app->textures.found, g_app->textures.found + g_app->textures.missing,
                                   g_app->mapModels.size(), g_app->fehlendeModelle.size(),
                                   g_app->effectRunners.size(), g_app->gpuAufrufe, g_app->gpuUebersprungen,
                                   neueMeldungen, liste1.empty() ? "" : " | fehlende Texturen:",
                                   liste1.c_str(), liste2.empty() ? "" : " | fehlende Modelle:", liste2.c_str());
                     diag::detail(z);
                     // Bilder der VORIGEN Karte auf der Grafikkarte? (Der Fehler
                     // "alle Texturen oder UV-Maps falsch" nach einem Wechsel.)
                     const int veraltet = gpu::veralteteKartenBilder();
                     if (veraltet != 0) {
                         diag::detail("Kartenpruefung: " + karte + " | " + std::to_string(veraltet) +
                                      " Bilder der vorigen Karte im Grafikspeicher");
                     }
                     // Speicher: die Puffer der Mover-Netze duerfen nicht ueber die
                     // Netze der Karte hinaus wachsen (vorher: bis zum Beenden).
                     const gpu::SpeicherStand sp = gpu::speicherStand();
                     diag::detail("Speicher: " + karte + " | " + speicherZeile(sp));
                     // Mover-Puffer gibt es fuer Brush-Netze UND Kartenmodelle (md3).
                     const bool netzeOk = sp.moverNetze <= static_cast<int>(g_app->brushMeshes.size() +
                                                                             g_app->md3Meshes.size());
                     if (!netzeOk) {
                         diag::detail("Kartenpruefung: " + karte + " | mehr Mover-Puffer als Netze");
                     }
                     const bool ok = g_app->mapBereit && !g_app->geo.empty() && g_app->gpuAufrufe > 0 &&
                                     g_app->gpuFehler.empty() && neueMeldungen == 0 && fehlendeInArchiv == 0 &&
                                     veraltet == 0 && netzeOk;
                     ++(*stand)[ok ? "ok" : "FEHLER"];
                     return ok;
                 }));
             }
             // --- Liegt etwas liegen? Die erste Karte noch einmal ------------
             //
             // Nach allen anderen Karten die erste erneut laden und den Speicher
             // mit ihrem ersten Mal vergleichen. Was an Karten haengt, muss
             // wieder frei sein; nach Namen abgelegte Zwischenspeicher (Figuren,
             // Klaenge) duerfen wachsen, darum der grosszuegige Rahmen beim
             // Prozess.
             if (liste.size() >= 2U) {
                 const std::string erste = liste.front();
                 auto vorher = std::make_shared<gpu::SpeicherStand>();
                 auto vorherText = std::make_shared<std::string>();
                 // Gemessen wird direkt NACH der Pruefung der ersten Karte.
                 for (std::size_t i = 0; i < neu.size(); ++i) {
                     if (neu[i].text == "Pruefe Karte " + erste + ": geladen, gezeichnet, ohne Beschwerde") {
                         neu.insert(neu.begin() + static_cast<std::ptrdiff_t>(i + 1),
                                    tu("Speicher: erste Karte gemessen", [=] {
                                        *vorher = gpu::speicherStand();
                                        *vorherText = speicherZeile(*vorher);
                                    }));
                         break;
                     }
                 }
                 neu.push_back(tu("Erste Karte erneut laden", [=] {
                     (void)loadMapFromArchive(erste);
                     g_app->mapDirty = true;
                 }));
                 neu.push_back(warteBis("Karte bereit", [] { return g_app->mapBereit; }, 900));
                 neu.push_back(warteBis("Szene aufgebaut", [] { return g_app->camTrackValid; }, 900));
                 neu.push_back(gezeichnet(erste + " erneut"));
                 neu.push_back(tu("Noch ein Bild", [] { g_app->mapDirty = true; }));
                 neu.push_back(gezeichnet(erste + " erneut (zweites Bild)"));
                 neu.push_back(pruefe("Speicher: nach allen Karten nichts liegen geblieben", [=] {
                     const gpu::SpeicherStand n = gpu::speicherStand();
                     diag::detail("Speicher vorher: " + *vorherText);
                     diag::detail("Speicher nachher: " + speicherZeile(n));
                     const double grafik = static_cast<double>(n.grafikBytes - vorher->grafikBytes) / 1048576.0;
                     const double prozess = static_cast<double>(n.prozessBytes - vorher->prozessBytes) / 1048576.0;
                     char z[200];
                     std::snprintf(z, sizeof(z), "Speicher: Zuwachs Grafik %.1f MB, Prozess %.1f MB", grafik, prozess);
                     diag::detail(z);
                     const bool zaehlerOk = n.bilder <= vorher->bilder + 8 && n.lightmaps <= vorher->lightmaps &&
                                            n.moverNetze <= vorher->moverNetze;
                     const bool grafikOk = vorher->grafikBytes < 0 || grafik < 64.0;
                     // Grosszuegig: Figuren- und Klangspeicher duerfen wachsen,
                     // und der Heap gibt nicht alles zurueck. Vorher waren es
                     // nach 8 Karten 806 MB.
                     const bool prozessOk = vorher->prozessBytes < 0 || prozess < 512.0;
                     return zaehlerOk && grafikOk && prozessOk;
                 }));
             }
             neu.push_back(tu("Alle Karten: Zusammenfassung", [=] {
                 std::string z = "Kartenpruefung fertig:";
                 for (const auto& [k, v] : *stand) { z += " " + k + " " + std::to_string(v) + ","; }
                 diag::detail(z);
             }));
             schritteEinfuegen(std::move(neu));
             return true;
         }});
    return s;
}

// --- Lavafall (md_am_sith): mehrere Zeitpunkte ------------------------------
//
// BHED_EDITORTEST=lava, Fotos mit BHED_FOTOS=<ordner>. Nah von shanks Stelle
// (-98 6922 176) mit Blick nach +y auf den Fall, fern von hoch darueber; je
// Weg sechs Zeitpunkte nah, zwei fern und eins ohne Gluehen. Damit wurden
// gefunden: gDeformSpread an der falschen Stelle im Stapelpuffer (der Fall
// pulsierte nur), das Verkleinern des Gluehpuffers mit einem Abgriff statt
// Kastenmittel (kantig, fern schlimmer) und das Abschneiden je Kanal statt
// proportional (gelb statt orange).
std::vector<Schritt> lavaSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    add(tu("Freie Kamera, kein Abspielen", [] {
        g_app->throughCamera = false;
        g_app->playing = false;
        g_app->mapDirty = true;
    }));
    {
        const bool gpu = true;   // bis rc568 auch der Rasterer zum Vergleich
        add(neuZeichnen());
        for (const int fern : {0, 1}) {
            add(tu(fern ? "fern" : "nah", [=] {
                // Fern: hoch ueber der Plattform, Blick schraeg hinunter zum
                // Fall - dort ist er klein im Bild, und das Gluehen muss
                // trotzdem weich bleiben.
                g_app->cam.pos[0] = -98.0F; g_app->cam.pos[1] = 6922.0F;
                g_app->cam.pos[2] = fern ? 1600.0F : 176.0F;
                g_app->cam.angles[0] = fern ? 74.9F : 0.0F;
                g_app->cam.angles[1] = 90.0F; g_app->cam.angles[2] = 0.0F;
            }));
            for (int ms : {0, 250, 500, 750, 1000, 1250}) {
                if (fern && ms > 250) { break; }
                add(tu("Zeit " + std::to_string(ms), [=] { g_app->playMs = ms; g_app->mapDirty = true; }));
                add(warteBis("gezeichnet", [] { return !g_app->mapDirty; }, 600));
                add(pause(0.2));
                add(foto(std::string(gpu ? "g" : "r") + (fern ? "fern_" : "nah_") + std::to_string(ms)));
            }
        }
        add(tu("nah, ohne Gluehen", [] {
            g_app->cam.pos[1] = 6922.0F; g_app->playMs = 0; g_app->showGlow = false; g_app->mapDirty = true;
        }));
        add(warteBis("gezeichnet", [] { return !g_app->mapDirty; }, 600));
        add(pause(0.2));
        add(foto(std::string(gpu ? "g" : "r") + "ohne_0"));
        add(tu("Gluehen wieder an", [] { g_app->showGlow = true; g_app->mapDirty = true; }));
    }
    return s;
}

// --- Holotisch (md_am_sith): Effekte ohne Gluehen --------------------------
//
// BHED_EDITORTEST=holo, Fotos mit BHED_FOTOS. shanks Stelle -151 3180 108,
// vier Blickrichtungen, Gluehen aus. Gefunden: die gemischten Effekte
// schrieben ihr Alpha ins GPU-Bild, und beim Anzeigen schien der
// Fensterhintergrund als dunkles Quadrat durch.
std::vector<Schritt> holoSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    add(tu("Freie Kamera, Gluehen aus", [] {
        g_app->throughCamera = false;
        g_app->playing = false;
        g_app->playMs = 3690.0;
        g_app->showGlow = false;
        g_app->mapDirty = true;
    }));
    {
        const bool gpu = true;   // bis rc568 auch der Rasterer zum Vergleich
        add(neuZeichnen());
        for (int w : {0, 90, 180, 270}) {
            add(tu("Blick " + std::to_string(w), [=] {
                g_app->cam.pos[0] = -151.0F; g_app->cam.pos[1] = 3180.0F; g_app->cam.pos[2] = 108.0F;
                g_app->cam.angles[0] = 12.0F; g_app->cam.angles[1] = static_cast<float>(w);
                g_app->cam.angles[2] = 0.0F;
                g_app->mapDirty = true;
            }));
            add(warteBis("gezeichnet", [] { return !g_app->mapDirty; }, 600));
            add(pause(0.3));
            add(foto(std::string(gpu ? "holo_g_" : "holo_r_") + std::to_string(w)));
        }
        // Grosser Tisch von oben, kleiner Tisch dahinter aus der Naehe.
        const float orte[3][5] = {{-172, 3120, 150, 45, 270}, {-192, 2700, 90, 10, 270}, {-192, 2600, 64, 0, 270}};
        for (int k = 0; k < 3; ++k) {
            // Die Werte kopieren, nicht den Zeiger: `orte` lebt nur bis zum
            // Ende dieser Funktion, der Schritt laeuft viel spaeter.
            const std::array<float, 5> o{orte[k][0], orte[k][1], orte[k][2], orte[k][3], orte[k][4]};
            add(tu(k == 0 ? "Grosser Tisch von oben" : k == 1 ? "Kleiner Tisch nah" : "Palpatine von vorn", [=] {
                g_app->cam.pos[0] = o[0]; g_app->cam.pos[1] = o[1]; g_app->cam.pos[2] = o[2];
                g_app->cam.angles[0] = o[3]; g_app->cam.angles[1] = o[4]; g_app->cam.angles[2] = 0.0F;
                g_app->mapDirty = true;
            }));
            add(warteBis("gezeichnet", [] { return !g_app->mapDirty; }, 600));
            add(pause(0.3));
            add(foto(std::string(gpu ? "holo_g_" : "holo_r_") + (k == 0 ? "oben" : k == 1 ? "klein" : "palp")));
        }
    }
    add(tu("Gluehen wieder an", [] { g_app->showGlow = true; g_app->mapDirty = true; }));
    return s;
}

// --- Beliebige Stelle fotografieren (nur GPU-Weg) --------------------------
//
// BHED_EDITORTEST=ort, Fotos mit BHED_FOTOS. Die Stelle kommt aus
//   BHED_ORT="x y z"            Kameraposition
//   BHED_BLICK="gier nick ..."  Paare aus Gier- und Nickwinkel (Grad)
//   BHED_ZEIT="ms ms ..."       Zeitpunkte auf der Zeitleiste
//   BHED_GLOW=0                 Gluehen aus
//   BHED_KARTE="maps/x.bsp"     vorher diese Karte aus den Archiven laden
//                               (ohne BHED_ORT: am info_player_start)
// Jede Kombination wird ein Foto ort_<gier>_<nick>_<ms>.
std::vector<Schritt> ortSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    auto pos = std::make_shared<std::array<float, 3>>(std::array<float, 3>{0, 0, 0});
    if (const char* o = std::getenv("BHED_ORT")) {
        std::sscanf(o, "%f %f %f", &(*pos)[0], &(*pos)[1], &(*pos)[2]);
    }
    // BHED_ZEITLEISTE=0.45: die Zeitleiste so hoch (Anteil am Fenster).
    if (const char* zl = std::getenv("BHED_ZEITLEISTE")) {
        const float anteil = static_cast<float>(std::atof(zl));
        add(tu("Zeitleiste hoeher", [=] { g_app->timelineFrac = anteil; }));
    }
    if (const char* k = std::getenv("BHED_KARTE")) {
        const std::string karte = k;
        const bool ortGegeben = std::getenv("BHED_ORT") != nullptr;
        add(tu("Karte " + karte, [=] {
            g_app->leftMode = 1;
            if (!loadMapFromArchive(karte)) {
                diag::detail("Knopftest: Karte " + karte + " nicht geladen");
            }
            if (ortGegeben) { return; }
            for (const MapEntity& e : g_app->map.entities) {
                if ((e.classname == "info_player_start" || e.classname == "info_player_deathmatch") &&
                    !e.origin.empty()) {
                    (void)std::sscanf(e.origin.c_str(), "%f %f %f", &(*pos)[0], &(*pos)[1], &(*pos)[2]);
                    (*pos)[2] += 48.0F;
                    break;
                }
            }
        }));
        add(warteBis("Karte bereit", [] { return g_app->mapBereit; }, 900));
        add(pause(0.5));
    }
    std::vector<std::pair<float, float>> blicke;
    if (const char* b = std::getenv("BHED_BLICK")) {
        std::istringstream is(b);
        float g = 0, n = 0;
        while (is >> g >> n) { blicke.emplace_back(g, n); }
    }
    if (blicke.empty()) {
        for (int w = 0; w < 360; w += 45) { blicke.emplace_back(static_cast<float>(w), 0.0F); }
    }
    std::vector<int> zeiten;
    if (const char* z = std::getenv("BHED_ZEIT")) {
        std::istringstream is(z);
        int ms = 0;
        while (is >> ms) { zeiten.push_back(ms); }
    }
    if (zeiten.empty()) { zeiten.push_back(0); }
    const bool glow = !(std::getenv("BHED_GLOW") != nullptr && std::getenv("BHED_GLOW")[0] == '0');
    // BHED_REITER="ani1_door1": vorher auf den Reiter wechseln, dessen Name
    // das enthaelt (die Szene wird dabei neu gebaut).
    if (const char* reiter = std::getenv("BHED_REITER")) {
        const std::string such = reiter;
        add(tu("Reiter " + such, [=] {
            for (int i = 0; i < static_cast<int>(g_app->tabs.size()); ++i) {
                const auto& tab = g_app->tabs[static_cast<std::size_t>(i)];
                const std::string name = tab.path + "|" + tab.shownName;
                diag::detail("Knopftest: Reiter " + std::to_string(i) + " = " + name);
                if (name.find(such) != std::string::npos) {
                    g_app->tabRequest = i;
                    break;
                }
            }
        }));
        add(pause(1.0));
        add(tu("Szene neu", [] { prepareScene(); g_app->mapDirty = true; }));
        add(pause(0.5));
    }
    // BHED_SKRIPT=<Datei>: dieses Skript in einem neuen Reiter oeffnen (die
    // Karte bleibt) - fuer eigene Proben, etwa eine Klinge an einer Figur.
    if (const char* datei = std::getenv("BHED_SKRIPT")) {
        const std::string pfad = datei;
        add(tu("Skript " + pfad, [=] {
            std::ifstream f(pfad, std::ios::binary);
            std::ostringstream ss;
            ss << f.rdbuf();
            openScriptFromMemory(ss.str(), "probe.txt");
        }));
        add(pause(1.0));
        add(tu("Szene neu", [] { prepareScene(); g_app->mapDirty = true; }));
        add(pause(0.5));
    }
    add(tu("Freie Kamera", [=] {
        g_app->throughCamera = false;
        g_app->playing = false;
        g_app->showGlow = glow;
        g_app->mapDirty = true;
    }));
    add(neuZeichnen());
    for (const int ms : zeiten) {
        for (const auto& [g, n] : blicke) {
            add(tu("Blick", [=] {
                for (int k = 0; k < 3; ++k) { g_app->cam.pos[k] = (*pos)[static_cast<std::size_t>(k)]; }
                g_app->cam.angles[0] = n;
                g_app->cam.angles[1] = g;
                g_app->cam.angles[2] = 0.0F;
                g_app->playMs = ms;
                g_app->mapDirty = true;
            }));
            add(warteBis("gezeichnet", [] { return !g_app->mapDirty; }, 600));
            add(pause(0.3));
            add(foto("ort_" + std::to_string(static_cast<int>(g)) + "_" + std::to_string(static_cast<int>(n)) +
                     "_" + std::to_string(ms)));
        }
    }
    add(tu("Gluehen wieder an", [] { g_app->showGlow = true; g_app->mapDirty = true; }));
    // BHED_MODELL=<Pfad im Archiv>: danach das Modellfenster fotografieren.
    if (const char* m = std::getenv("BHED_MODELL")) {
        const std::string pfad = m;
        add(tu("Modell " + pfad, [=] {
            std::string fehler;
            if (!oeffneModellAusArchiv(pfad, &fehler)) {
                diag::detail("Knopftest: Modell nicht geladen: " + fehler);
            }
        }));
        add(pause(1.5));
        add(foto("modell"));
        add(tu("Modell mit Kappen", [] { g_app->showCaps = true; g_app->modelDirty = true; }));
        add(pause(0.5));
        add(foto("modell_kappen"));
        add(tu("Kappen aus, zurueck zur Karte", [] {
            g_app->showCaps = false;
            g_app->modelDirty = true;
            g_app->leftMode = 1;
        }));
    }
    return s;
}

// --- JEDE Zwischensequenz durchspielen (BHED_EDITORTEST=szenen) -------------
//
// shank: "dann jede Cutscene hier einmal durchladen zum Testen ...
// durchlaufen lassen, dass wirklich alles geht". BHED_SZENEN nennt eine
// Liste, je Zeile "maps/karte.bsp<TAB>ordner/skript". Die Karte wird nur bei
// einem Wechsel neu geladen, das Skript wie aus einer Mission geoeffnet
// (openScriptFromMemory mit seinem ICARUS-Pfad - so findet der Nachbau den
// Traeger und die Unterskripte). Dann laeuft die Szene im Zeitraffer durch
// die Szenenkamera: je BHED_SCHRITT ms (Vorgabe 500) ein Bild, mit
// playing=true, damit auch jeder Klang ausgeloest wird (stumm, wie im
// ganzen Selbsttest). Ins Protokoll kommt je Szene eine Zeile
// "Szenenlauf: ..." - Traeger, Dauer, Figuren ohne Modell, fehlende
// Klaenge, Bildzeiten, D3D-Meldungen, Hinweise des Nachbaus.
std::vector<Schritt> szenenSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    add(tu("Szenen: Kartenansicht links", [] {
        g_app->leftMode = 1;
        // BHED_SCHATTEN=0..3: die Schattenart (cg_shadows) fuer den Lauf.
        if (const char* art = std::getenv("BHED_SCHATTEN")) {
            g_app->schattenArt = std::clamp(std::atoi(art), 0, 3);
        }
    }));
    add(pause(0.3));
    add({"Szenen: Liste lesen", [=](int) {
             std::vector<std::pair<std::string, std::string>> liste;
             if (const char* datei = std::getenv("BHED_SZENEN")) {
                 std::ifstream f(datei);
                 std::string zeile;
                 while (std::getline(f, zeile)) {
                     if (!zeile.empty() && zeile.back() == '\r') { zeile.pop_back(); }
                     const auto tab = zeile.find('\t');
                     if (tab == std::string::npos || zeile[0] == '#') { continue; }
                     liste.emplace_back(zeile.substr(0, tab), zeile.substr(tab + 1));
                 }
             }
             const double schritt = std::getenv("BHED_SCHRITT") ? std::max(50.0, std::atof(std::getenv("BHED_SCHRITT"))) : 500.0;
             diag::detail("Szenenlauf: " + std::to_string(liste.size()) + " Szenen, Schritt " +
                          std::to_string(static_cast<int>(schritt)) + " ms");
             std::vector<Schritt> neu;
             auto aktuelleKarte = std::make_shared<std::string>();
             for (const auto& [karte, skript] : liste) {
                 struct Stand {
                     std::size_t meld = 0;
                     double dauer = 0.0;
                     double t = 0.0;
                     double bildMax = 0.0;
                     double bildSumme = 0.0;
                     int bilder = 0;
                     std::chrono::steady_clock::time_point seit;
                     bool geladen = false;
                 };
                 auto st = std::make_shared<Stand>();
                 const std::string k = karte;
                 const std::string sk = skript;
                 neu.push_back(tu("Szene " + sk + ": Karte " + k, [=] {
                     st->meld = g_app->d3dMeldungen.size();
                     if (*aktuelleKarte != k) {
                         if (!loadMapFromArchive(k)) {
                             diag::detail("Szenenlauf: " + sk + " | Karte " + k + " nicht geladen");
                         }
                         *aktuelleKarte = k;
                     }
                 }));
                 neu.push_back(warteBis("Karte bereit", [] { return g_app->mapBereit; }, 1200));
                 neu.push_back(tu("Szene " + sk + ": Skript oeffnen", [=] {
                     std::string data;
                     std::string gefunden;
                     for (const char* ext : {".txt", ".icarus", ".ibi", ".IBI"}) {
                         if (readFromArchives("scripts/" + sk + ext, data)) {
                             gefunden = ext;
                             break;
                         }
                     }
                     if (gefunden.empty()) {
                         diag::detail("Szenenlauf: " + sk + " | Skript nicht im Archiv");
                         return;
                     }
                     // Nicht bei jedem Skript einen neuen Reiter: den vorigen
                     // schliessen, sobald mehr als einer offen ist.
                     while (g_app->tabs.size() > 1) {
                         reiterSchliessen(static_cast<int>(g_app->tabs.size()) - 1);
                     }
                     g_app->doc = Document{Script{}};
                     g_app->path.clear();
                     g_app->shownName.clear();
                     g_app->skriptPfad.clear();
                     openScriptFromMemory(data, sk + gefunden);
                     g_app->camTrackValid = false;
                     g_app->throughCamera = true;
                     g_app->playing = false;
                     g_app->playMs = 0.0;
                     g_app->audioUpTo = -1.0;
                     st->geladen = true;
                 }));
                 neu.push_back(warteBis("Szene aufgebaut", [] { return g_app->camTrackValid; }, 1200));
                 neu.push_back({"Szene " + sk + ": durchspielen", [=](int b) {
                                    if (!st->geladen) {
                                        return true;
                                    }
                                    if (b == 0) {
                                        st->dauer = std::max(g_app->camTrack.durationMs,
                                                             std::min(g_app->timeline.practicalEndMs, 600000.0));
                                        st->t = 0.0;
                                        g_app->playing = true;
                                        g_app->playMs = 0.0;
                                        g_app->mapDirty = true;
                                        st->seit = std::chrono::steady_clock::now();
                                        return false;
                                    }
                                    // Jeder Aufruf ist ein gezeichnetes Bild (beim
                                    // Abspielen setzt die Ansicht mapDirty selbst
                                    // jedes Bild neu - darauf zu warten hiesse
                                    // ewig warten).
                                    const double ms = std::chrono::duration<double, std::milli>(
                                                          std::chrono::steady_clock::now() - st->seit)
                                                          .count();
                                    st->bildMax = std::max(st->bildMax, ms);
                                    st->bildSumme += ms;
                                    ++st->bilder;
                                    st->t += schritt;
                                    if (st->t > st->dauer + 1.0) {
                                        g_app->playing = false;
                                        return true;
                                    }
                                    g_app->playing = true;
                                    g_app->playMs = st->t;
                                    g_app->mapDirty = true;
                                    st->seit = std::chrono::steady_clock::now();
                                    return false;
                                }});
                 // BHED_SZENEN_FOTO="0.3 0.6": Fotos an diesen Anteilen der
                 // Dauer (nur mit BHED_FOTOS), durch die Szenenkamera.
                 if (const char* fo = std::getenv("BHED_SZENEN_FOTO")) {
                     std::istringstream is(fo);
                     double anteil = 0.0;
                     while (is >> anteil) {
                         const double an = anteil;
                         neu.push_back(tu("Szene " + sk + ": zu " + std::to_string(an), [=] {
                             g_app->playing = false;
                             g_app->throughCamera = true;
                             g_app->playMs = st->dauer * an;
                             g_app->mapDirty = true;
                         }));
                         neu.push_back(warteBis("gezeichnet", [] { return !g_app->mapDirty; }, 600));
                         neu.push_back(pause(0.3));
                         std::string name = sk;
                         for (char& c : name) { if (c == '/') { c = '_'; } }
                         neu.push_back(foto("szene_" + name + "_" + std::to_string(static_cast<int>(an * 100.0))));
                     }
                 }
                 neu.push_back(tu("Szene " + sk + ": Bericht", [=] {
                     if (!st->geladen) {
                         return;
                     }
                     int ohneModell = 0;
                     std::string ohneListe;
                     for (std::size_t i = 0; i < g_app->scene.actors.size(); ++i) {
                         const Actor& a = g_app->scene.actors[i];
                         if (a.brushModel > 0 || !a.haveStart) { continue; }
                         // Ein Kartenmodell (misc_model), das das Skript bewegt,
                         // hat sein Modell dort - es ist keine Figur.
                         if (std::find(g_app->modellFigur.begin(), g_app->modellFigur.end(), static_cast<int>(i)) !=
                             g_app->modellFigur.end()) {
                             continue;
                         }
                         // Ohne NPC_type: nur, was im Spiel etwas zeigt (misc_*,
                         // NPC_*). Ein target_scriptrunner oder info_null, den das
                         // Skript per affect anspricht, hat nie ein Modell.
                         // Ein Typ ohne .npc wird im Spiel nie gespawnt (prepareScene).
                         if (!a.npcType.empty() && !g_app->npcMap.empty()) {
                             std::string typ = a.npcType;
                             for (char& c : typ) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                             if (g_app->npcMap.count(typ) == 0) { continue; }
                         }
                         if (a.npcType.empty()) {
                             bool sichtbar = false;
                             for (const MapEntity& e : g_app->map.entities) {
                                 const std::string n = entitySkriptName(e);
                                 if (n.size() == a.name.size() &&
                                     std::equal(n.begin(), n.end(), a.name.begin(), [](char x, char y) {
                                         return std::tolower(static_cast<unsigned char>(x)) ==
                                                std::tolower(static_cast<unsigned char>(y));
                                     })) {
                                     sichtbar = e.classname.rfind("misc_", 0) == 0 || e.classname.rfind("NPC_", 0) == 0;
                                     break;
                                 }
                             }
                             if (!sichtbar) { continue; }
                         }
                         const bool hat = i < g_app->actorAssets.size() && !g_app->actorAssets[i].model.surfaces.empty();
                         if (!hat) {
                             ++ohneModell;
                             if (ohneListe.size() < 200) { ohneListe += " " + a.name + "(" + a.npcType + ")"; }
                         }
                     }
                     std::set<std::string> klaenge;
                     std::string fehltListe;
                     int fehlt = 0;
                     for (const TimelineTrack& tr2 : g_app->timeline.tracks) {
                         for (const TimelineEvent& e : tr2.events) {
                             if (e.kind != TimelineEvent::Kind::Sound || e.sound.empty() || !klaenge.insert(e.sound).second) {
                                 continue;
                             }
                             if (soundLengthMs(e.sound) <= 0.0) {
                                 ++fehlt;
                                 if (fehltListe.size() < 300) { fehltListe += " " + e.sound; }
                             }
                         }
                     }
                     const bhed::Ablauf& ab = g_app->ablauf;
                     char z[1400];
                     std::snprintf(z, sizeof(z),
                                   "Szenenlauf: %s [%s] | Traeger \"%s\" | Dauer %.1f s | %d Befehle aus %zu Skript(en) | "
                                   "Figuren %zu (ohne Modell %d%s) | Effekte %zu | Klaenge %zu (fehlen %d%s) | "
                                   "Bild max %.0f ms, Mittel %.0f ms (%d Bilder) | D3D %zu | Hinweise %zu | GPU \"%s\"",
                                   sk.c_str(), k.c_str(), ab.traeger.c_str(), st->dauer / 1000.0, ab.befehle,
                                   ab.skripte.size(), g_app->scene.actors.size(), ohneModell, ohneListe.c_str(),
                                   g_app->effectRunners.size(), klaenge.size(), fehlt, fehltListe.c_str(), st->bildMax,
                                   st->bilder > 0 ? st->bildSumme / st->bilder : 0.0, st->bilder,
                                   g_app->d3dMeldungen.size() - st->meld, ab.hinweise.size(), g_app->gpuFehler.c_str());
                     diag::detail(z);
                     for (const std::string& h : ab.hinweise) {
                         diag::detail("Szenenlauf: " + sk + " | Hinweis: " + h);
                     }
                 }));
             }
             schritteEinfuegen(std::move(neu));
             return true;
         }});
    return s;
}

// Allein aufrufbar mit BHED_EDITORTEST=karte. Braucht die Spielordner aus
// den Einstellungen (dort liegt das Archiv mit maps/md_am_sith.bsp).

Schritt warteBis(const std::string& was, std::function<bool()> bedingung, int maxBilder) {
    return {"Warte: " + was, [=](int b) {
                if (bedingung()) {
                    diag::detail("Knopftest: " + was + " nach " + std::to_string(b) + " Bildern");
                    return true;
                }
                if (b >= maxBilder) {
                    meldeFehler(was + ": nicht nach " + std::to_string(maxBilder) + " Bildern");
                    return true;
                }
                return false;
            }};
}

// "x y z" -> drei Zahlen. false, wenn es kein blanker Vektor ist (Ausdruck).
bool leseVektor(const std::string& text, float out[3]) {
    return std::sscanf(text.c_str(), "%f %f %f", &out[0], &out[1], &out[2]) == 3;
}

bool leseZahl(const std::string& text, float& out) {
    return std::sscanf(text.c_str(), "%f", &out) == 1;
}

float winkelAbstand(float a, float b) {
    float d = std::fmod(a - b, 360.0F);
    if (d > 180.0F) { d -= 360.0F; }
    if (d < -180.0F) { d += 360.0F; }
    return std::fabs(d);
}

// Wie weit das Hauptfenster rollen kann und wo es steht.
ImGuiWindow* hauptfenster() { return ImGui::FindWindowByName("###main"); }

std::vector<Schritt> karteSchritte() {
    std::vector<Schritt> s;
    auto add = [&](Schritt x) { s.push_back(std::move(x)); };
    auto archiv = std::make_shared<std::string>();
    auto reiter = std::make_shared<ImRect>();

    add(tu("Archiv mit maps/md_am_sith.bsp suchen", [=] {
        for (const GamePath& gp : g_app->gamePaths) {
            for (const Pk3& arc : gp.archives) {
                if (arc.find("maps/md_am_sith.bsp") != nullptr) {
                    *archiv = arc.path;
                }
            }
        }
        diag::detail("Knopftest: Archiv \"" + *archiv + "\"");
    }));
    add(pruefe("Archiv mit md_am_sith gefunden", [=] { return !archiv->empty(); }));
    add(tu("Missionen des Archivs (wie nach dem Dateidialog von Load mission...)",
           [=] { if (!archiv->empty()) { missionenAusArchiv(*archiv); } }));
    add(pruefe("Missionsauswahl ist offen", [] { return g_app->missionPickOpen; }));
    add(waehleInListe("md_am_sith", false, 0, "##missionpick"));
    add(warteBis("Mission geladen (Karte und Skripte)", [] {
        return !g_app->map.empty() && g_app->tabs.size() > 1 && g_app->camTrackValid;
    }, 1200));
    add(pruefe("md_am_sith: Karte mit Entities, mehrere Skripte", [] {
        diag::detail("Knopftest: " + std::to_string(g_app->map.entities.size()) +
                     " Entities, " + std::to_string(g_app->tabs.size()) + " Reiter, " +
                     std::to_string(g_app->camTrack.segments.size()) + " Kamerasegmente");
        return g_app->map.entities.size() > 100 && g_app->tabs.size() >= 10;
    }));
    add(warteBis("Kartenansicht gezeichnet", [] {
        return g_app->kartenAnsicht[2] > g_app->kartenAnsicht[0] + 100.0F;
    }, 300));

    // ---- Das Hauptfenster darf nicht rollen ---------------------------
    add(tu("Rollbereich des Hauptfensters messen", [=] {
        ImGuiWindow* w = hauptfenster();
        if (w == nullptr) { return; }
        char z[240];
        std::snprintf(z, sizeof(z),
                      "Knopftest: Hauptfenster %.0fx%.0f, Inhalt %.0f hoch, "
                      "rollbar %.0f, steht bei %.0f",
                      static_cast<double>(w->Size.x), static_cast<double>(w->Size.y),
                      static_cast<double>(w->ContentSize.y),
                      static_cast<double>(w->ScrollMax.y), static_cast<double>(w->Scroll.y));
        diag::detail(z);
        const Element* tab = finde(".ibi", "###main", 0, false);
        for (const Element& e : g_elemente) {
            if (e.label.find(".ibi") != std::string::npos &&
                e.fenster.find("###main") != std::string::npos) {
                tab = &e;
                break;
            }
        }
        if (tab != nullptr) { *reiter = tab->rect; }
    }));
    add(pruefe("Hauptfenster hat nichts zu rollen (Inhalt passt)", [] {
        ImGuiWindow* w = hauptfenster();
        return w != nullptr && w->ScrollMax.y < 0.5F;
    }));
    auto rad = [=](const std::string& wo, std::function<ImVec2()> stelle) {
        return Schritt{"Mausrad ueber " + wo, [=](int b) {
                           ImGuiIO& io = ImGui::GetIO();
                           if (b == 0) { setzeMaus(stelle()); return false; }
                           if (b == 2 || b == 3 || b == 4) { io.AddMouseWheelEvent(0.0F, -1.0F); return false; }
                           if (b == 6 || b == 7 || b == 8) { io.AddMouseWheelEvent(0.0F, 1.0F); return false; }
                           return b >= 11;
                       }};
    };
    auto reiterFest = [=](const std::string& wo) {
        return pruefe("Nach dem Mausrad ueber " + wo +
                          ": Hauptfenster steht, Reiter an ihrem Platz", [=] {
            ImGuiWindow* w = hauptfenster();
            const Element* tab = nullptr;
            for (const Element& e : g_elemente) {
                if (e.label.find(".ibi") != std::string::npos &&
                    e.fenster.find("###main") != std::string::npos) {
                    tab = &e;
                    break;
                }
            }
            const bool fest = tab != nullptr &&
                              std::fabs(tab->rect.Min.y - reiter->Min.y) < 0.5F;
            if (!fest || w == nullptr || w->Scroll.y > 0.5F) {
                char z[200];
                std::snprintf(z, sizeof(z), "Knopftest: Hauptfenster steht bei %.0f, Reiter %.0f -> %.0f",
                              w != nullptr ? static_cast<double>(w->Scroll.y) : -1.0,
                              static_cast<double>(reiter->Min.y),
                              tab != nullptr ? static_cast<double>(tab->rect.Min.y) : -1.0);
                diag::detail(z);
            }
            return fest && w != nullptr && w->Scroll.y < 0.5F;
        });
    };
    add(rad("der Kartenansicht", [] {
        return ImVec2{(g_app->kartenAnsicht[0] + g_app->kartenAnsicht[2]) * 0.5F,
                      (g_app->kartenAnsicht[1] + g_app->kartenAnsicht[3]) * 0.5F};
    }));
    add(reiterFest("der Kartenansicht"));
    add(rad("der Zeitleiste", [] {
        const Element* e = finde("##zeitlineal", "###main", 0, false);
        return e != nullptr ? e->rect.GetCenter() : ImVec2{0, 0};
    }));
    add(reiterFest("der Zeitleiste"));
    add(stabil("Karte nach dem Laden"));

    // ---- Kamera: Skript -> Bahn -> Zeitpunkt ---------------------------
    add(Schritt{"Kamerabahn gegen das Skript pruefen", [=](int) {
        const CameraTrack& tr = g_app->camTrack;
        int moves = 0, pans = 0, zooms = 0, fehler = 0, uebersprungen = 0;
        auto fehl = [&](const std::string& z) {
            ++fehler;
            if (fehler <= 8) { diag::detail("Knopftest: Kamera: " + z); }
        };
        for (const CamSegment& sg : tr.segments) {
            const Node* n = nodeAt(g_app->doc.script(), sg.path);
            if (n == nullptr || n->name != "camera" || n->args.size() < 2) { continue; }
            const double dauer = sg.endMs - sg.startMs;
            float ms = 0.0F;
            const bool dauerDa = leseZahl(n->args.back().text, ms);
            char z[300];
            if (sg.kind == CamSegment::Kind::Move) {
                ++moves;
                float soll[3];
                if (leseVektor(n->args[1].text, soll)) {
                    for (int k = 0; k < 3; ++k) {
                        if (std::fabs(sg.to[k] - soll[k]) > 0.01F) {
                            std::snprintf(z, sizeof(z), "MOVE %s: Ziel %.1f statt %.1f (Achse %d)",
                                          n->args[1].text.c_str(),
                                          static_cast<double>(sg.to[k]), static_cast<double>(soll[k]), k);
                            fehl(z);
                            break;
                        }
                    }
                } else {
                    ++uebersprungen;   // Ausdruck ($tag(...)$) - ueber die Karte aufgeloest
                }
                if (dauerDa && std::fabs(dauer - ms) > 0.5) {
                    std::snprintf(z, sizeof(z), "MOVE: Dauer %.0f statt %.0f ms", dauer, static_cast<double>(ms));
                    fehl(z);
                }
                // Kein anderer MOVE, der hineinfunkt?
                bool frei = true;
                for (const CamSegment& o : tr.segments) {
                    if (&o != &sg && o.kind == CamSegment::Kind::Move &&
                        o.startMs >= sg.startMs && o.startMs < sg.endMs + 0.5) {
                        frei = false;
                    }
                }
                if (frei) {
                    const double spaet = (dauer > 1.0) ? sg.endMs - 0.5 : sg.startMs + 1.0;
                    const CameraState ende = tr.at(spaet);
                    for (int k = 0; k < 3; ++k) {
                        if (std::fabs(ende.pos[k] - sg.to[k]) > 1.0F) {
                            std::snprintf(z, sizeof(z), "MOVE bei %.0f ms: steht bei %.1f statt %.1f (Achse %d)",
                                          spaet, static_cast<double>(ende.pos[k]),
                                          static_cast<double>(sg.to[k]), k);
                            fehl(z);
                            break;
                        }
                    }
                    if (dauer > 1.0) {
                        const CameraState mitte = tr.at(sg.startMs + dauer * 0.5);
                        for (int k = 0; k < 3; ++k) {
                            const float halb = (sg.from[k] + sg.to[k]) * 0.5F;
                            if (std::fabs(mitte.pos[k] - halb) > 1.0F) {
                                std::snprintf(z, sizeof(z), "MOVE Mitte: %.1f statt %.1f (Achse %d) - nicht linear?",
                                              static_cast<double>(mitte.pos[k]), static_cast<double>(halb), k);
                                fehl(z);
                                break;
                            }
                        }
                    }
                }
            } else if (sg.kind == CamSegment::Kind::Pan) {
                ++pans;
                float soll[3];
                if (leseVektor(n->args[1].text, soll)) {
                    for (int k = 0; k < 3; ++k) {
                        if (winkelAbstand(sg.to[k], soll[k]) > 0.01F) {
                            std::snprintf(z, sizeof(z), "PAN %s: Ziel %.1f statt %.1f (Achse %d)",
                                          n->args[1].text.c_str(),
                                          static_cast<double>(sg.to[k]), static_cast<double>(soll[k]), k);
                            fehl(z);
                            break;
                        }
                    }
                } else {
                    ++uebersprungen;
                }
                if (dauerDa && std::fabs(dauer - ms) > 0.5) {
                    std::snprintf(z, sizeof(z), "PAN: Dauer %.0f statt %.0f ms", dauer, static_cast<double>(ms));
                    fehl(z);
                }
                bool frei = true;
                for (const CamSegment& o : tr.segments) {
                    if (&o != &sg && (o.kind == CamSegment::Kind::Pan ||
                                      o.kind == CamSegment::Kind::Follow) &&
                        o.startMs >= sg.startMs && o.startMs < sg.endMs + 0.5) {
                        frei = false;
                    }
                }
                if (frei) {
                    const double spaet = (dauer > 1.0) ? sg.endMs - 0.5 : sg.startMs + 1.0;
                    const CameraState ende = tr.at(spaet);
                    for (int k = 0; k < 3; ++k) {
                        if (winkelAbstand(ende.angles[k], sg.to[k]) > 0.5F) {
                            std::snprintf(z, sizeof(z), "PAN bei %.0f ms: Winkel %.1f statt %.1f (Achse %d)",
                                          spaet, static_cast<double>(ende.angles[k]),
                                          static_cast<double>(sg.to[k]), k);
                            fehl(z);
                            break;
                        }
                    }
                }
            } else if (sg.kind == CamSegment::Kind::Zoom) {
                ++zooms;
                float soll = 0.0F;
                // ZOOM ohne Dauer mitten in einem laufenden setzt nur den
                // AUSGANGSWERT; der laufende Zoom geht zu seinem Ziel weiter
                // (CGCam_Zoom loescht CAMERA_ZOOMING nicht). Dann steht der
                // Skriptwert in from, nicht in to.
                const float gesetzt = (sg.lerpStartMs >= 0.0) ? sg.from[0] : sg.to[0];
                if (leseZahl(n->args[1].text, soll) && std::fabs(gesetzt - soll) > 0.01F) {
                    std::snprintf(z, sizeof(z), "ZOOM: %.1f statt %.1f",
                                  static_cast<double>(gesetzt), static_cast<double>(soll));
                    fehl(z);
                }
                // Wie bei MOVE: nur pruefen, wenn kein spaeterer ZOOM
                // hineinfunkt (in der Engine ersetzt er den laufenden).
                bool frei = true;
                for (const CamSegment& o : tr.segments) {
                    if (&o != &sg && o.kind == CamSegment::Kind::Zoom &&
                        o.startMs >= sg.startMs && o.startMs < sg.endMs + 0.5) {
                        frei = false;
                    }
                }
                // Dauer <= 0: in der Engine sofort am Ziel (CGCam_Update beendet,
                // sobald start + dauer < jetzt - bei -1 schon im ersten Bild).
                const CameraState ende = tr.at(dauer > 1.0 ? sg.endMs - 0.5 : sg.startMs + 1.0);
                if (frei && std::fabs(ende.fovX - sg.to[0]) > 0.5F) {
                    std::snprintf(z, sizeof(z),
                                  "ZOOM %.0f-%.0f ms von %.1f auf %.1f: am Ende fov %.1f "
                                  "(Skript: ZOOM, %s, %s)",
                                  sg.startMs, sg.endMs, static_cast<double>(sg.from[0]),
                                  static_cast<double>(sg.to[0]), static_cast<double>(ende.fovX),
                                  n->args[1].text.c_str(), n->args.back().text.c_str());
                    fehl(z);
                }
            }
        }
        char z[240];
        std::snprintf(z, sizeof(z),
                      "Kamera gegen Skript: %d MOVE, %d PAN, %d ZOOM geprueft "
                      "(%d Ziele aus Ausdruecken nicht gegen den Text), %d Abweichungen",
                      moves, pans, zooms, uebersprungen, fehler);
        pruefeWahr(fehler == 0 && moves + pans > 0, z);
        return true;
    }});

    // ---- Kamera: Zeitpunkt -> Ansicht (Through camera) ----------------
    auto zeitpunkt = std::make_shared<double>(-1.0);
    add(tu("Zeitpunkt mitten in einem MOVE, Through camera an", [=] {
        for (const CamSegment& sg : g_app->camTrack.segments) {
            if (sg.kind == CamSegment::Kind::Move && sg.endMs - sg.startMs > 500.0) {
                *zeitpunkt = sg.startMs + (sg.endMs - sg.startMs) * 0.5;
                break;
            }
        }
        g_app->playing = false;
        g_app->throughCamera = true;
        if (*zeitpunkt >= 0.0) { g_app->playMs = *zeitpunkt; }
        g_app->mapDirty = true;
    }));
    add(warteBis("drei Bilder gezeichnet", [] { return true; }, 3));
    add(tasteMit(ImGuiKey_None, ImGuiKey_None, "(Bild abwarten)"));
    add(pruefe("Through camera: die Ansicht steht genau auf der Kamera des Skripts", [=] {
        const CameraState st = camStateAt(g_app->playMs);
        const Camera& c = g_app->gezeigteKamera;
        float abw = 0.0F;
        for (int k = 0; k < 3; ++k) {
            abw = std::max(abw, std::fabs(c.pos[k] - st.pos[k]));
            abw = std::max(abw, winkelAbstand(c.angles[k], st.angles[k]));
        }
        char z[240];
        std::snprintf(z, sizeof(z),
                      "Knopftest: bei %.0f ms Ansicht %.1f %.1f %.1f / %.1f %.1f %.1f, Kamera %.1f %.1f %.1f / %.1f %.1f %.1f",
                      g_app->playMs,
                      static_cast<double>(c.pos[0]), static_cast<double>(c.pos[1]), static_cast<double>(c.pos[2]),
                      static_cast<double>(c.angles[0]), static_cast<double>(c.angles[1]), static_cast<double>(c.angles[2]),
                      static_cast<double>(st.pos[0]), static_cast<double>(st.pos[1]), static_cast<double>(st.pos[2]),
                      static_cast<double>(st.angles[0]), static_cast<double>(st.angles[1]), static_cast<double>(st.angles[2]));
        diag::detail(z);
        return *zeitpunkt >= 0.0 && abw < 0.01F;
    }));
    add(tu("Through camera aus", [] { g_app->throughCamera = false; }));

    // ---- Zeitleiste: Play, Stop, To start ------------------------------
    auto start = std::make_shared<double>(0.0);
    add(tu("An den Anfang", [] { g_app->playMs = 0.0; g_app->playing = false; }));
    add(klick(tr(Str::TlPlay), "###main"));
    add(tu("Zeit merken", [=] { *start = ImGui::GetTime(); }));
    add(warteBis("eine Sekunde abspielen", [=] { return ImGui::GetTime() - *start > 1.0; }, 2000));
    add(pruefe("Play: die Zeit laeuft mit der Uhr (+-25 %)", [=] {
        const double soll = (ImGui::GetTime() - *start) * 1000.0;
        diag::detail("Knopftest: abgespielt " + std::to_string(g_app->playMs) + " ms in " +
                     std::to_string(soll) + " ms");
        return g_app->playing && g_app->playMs > soll * 0.75 && g_app->playMs < soll * 1.25 + 100.0;
    }));
    add(pruefe("Beim Abspielen folgt die Ansicht der Kamera", [] {
        const CameraState st = camStateAt(g_app->playMs);
        const Camera& c = g_app->gezeigteKamera;
        float abw = 0.0F;
        for (int k = 0; k < 3; ++k) {
            abw = std::max(abw, std::fabs(c.pos[k] - st.pos[k]));
        }
        // ein Bild Versatz ist erlaubt - die Zeit laeuft weiter
        return abw < 50.0F;
    }));
    add(klick(tr(Str::TlStop), "###main"));
    add(pruefe("Stop haelt an", [] { return !g_app->playing; }));
    add(klick(tr(Str::TlRewind), "###main"));
    add(pruefe("To start: zurueck auf 0", [] { return g_app->playMs < 0.5; }));
    add(stabil("Karte nach Play/Stop"));
    return s;
}

// Beenden: das Knopftest-Skript ist geaendert (print, affect), also muss die
// Speichernfrage kommen - und "No" muss das Programm beenden. Ob es das tut,
// zeigt sich daran, dass der Lauf von selbst endet.
// Jede weitere Speichernfrage mit "No" beantworten, bis das Programm zu ist.
Schritt antworteNein() {
    return {"Speichernfrage beantworten", [](int b) {
                // Notbremse: ist das Fenster minimiert, hat es keine Flaeche,
                // und "No" ist nicht zu finden - am 27.09. klickte der Test so
                // 164 000 Mal ins Leere, bis die Zeitgrenze kam. Nach 200
                // Versuchen (mehr als Reiter offen sein koennen) ohne Speichern
                // beenden und den Grund nennen.
                static int versuche = 0;
                if ((g_app->askSaveOpen || g_app->frageOffen) && ++versuche > 200) {
                    const ImVec2 gr = ImGui::GetIO().DisplaySize;
                    diag::detail("Knopftest: Speichernfrage nicht beantwortbar (Fenster " +
                                 std::to_string(static_cast<int>(gr.x)) + "x" +
                                 std::to_string(static_cast<int>(gr.y)) +
                                 (gr.x < 50.0F || gr.y < 50.0F ? ", minimiert" : "") +
                                 ") - beende ohne Speichern");
                    g_app->askSaveOpen = false;
                    g_app->frageOffen = false;
                    g_app->quitConfirmed = true;
                    g_app->wantQuit = true;
                    return true;
                }
                if (g_app->askSaveOpen) {
                    schritteEinfuegen({klick(tr(Str::AnswerNo), tr(Str::AppTitle)),
                               antworteNein()});
                    return true;
                }
                // "Exit?" (Yes/No query) mit Ja - wir wollen ja beenden.
                if (g_app->frageOffen) {
                    schritteEinfuegen({klick(tr(Str::AnswerYes), "###frage"), antworteNein()});
                    return true;
                }
                return b >= 60;
            }};
}

// `voll`: nach der Klickpruefung ist ein Skript geaendert - dann MUSS die
// Speichernfrage kommen. Im Teillauf (BHED_EDITORTEST=geteilt) nicht.
std::vector<Schritt> endeSchritte(bool voll) {
    std::vector<Schritt> s;
    const Schritt zusammenfassung = tu("Zusammenfassung", [] {
        g_app->settings.queryOnDiscard = g_queryVorher;
        char z[120];
        std::snprintf(z, sizeof(z), "Knopftest fertig: %d OK, %d FEHLER",
                      g_ok, g_fehler);
        diag::detail(z);
    });
    if (voll) {
        s.push_back(tu("Beenden anfordern", [] { g_app->wantQuit = true; }));
        s.push_back(pruefe("Beenden: Speichernfrage erscheint",
                           [] { return g_app->askSaveOpen; }));
        s.push_back(zusammenfassung);
    } else {
        // Ohne geaendertes Skript geht das Programm sofort zu - also vorher.
        s.push_back(zusammenfassung);
        s.push_back(tu("Beenden anfordern", [] { g_app->wantQuit = true; }));
    }
    s.push_back(antworteNein());
    return s;
}

// --- Teil 1: Ruhe beim Oeffnen --------------------------------------------

struct RuheFall {
    const char* name;      // nur fuers Protokoll
    const char* befehl;    // Befehlsname im Testskript
    int nte;               // der wievielte Befehl dieses Namens (ab 0)
    bool ausdruckAn;       // nach dem Einschwingen Expr! druecken
    bool ziehen;           // mit simulierter Maus an der Titelzeile ziehen
};

constexpr RuheFall kRuheFaelle[] = {
    {"if/3", "if", 0, false, false},
    {"if/1", "if", 1, false, false},
    {"set/2", "set", 0, false, false},
    {"if/1+Expr", "if", 1, true, false},
    {"if/3+Zug", "if", 0, false, true},
    {"camera/PAN", "camera", 0, false, false},
    {"camera/ENA", "camera", 1, false, false},
    {"sound", "sound", 0, false, false},
    {"affect", "affect", 0, false, false},
    {"else", "else", 0, false, false},
    {"print", "print", 0, false, false},
    {"wait", "wait", 0, false, false},
    {"if/3+Expr", "if", 0, true, false},
};
constexpr int kRuheFallZahl =
    static_cast<int>(sizeof(kRuheFaelle) / sizeof(kRuheFaelle[0]));
constexpr int kRuheRunden = 2;     // 0: erstes Oeffnen, 1: zweites
constexpr int kBilderOffen = 60;
constexpr int kBilderPause = 6;
constexpr int kVorlauf = 30;       // Fenster, Schriften, Einstellungen

// Beide Fassungen von `if`, dazu Befehle ohne Helfer zum Vergleich.
const char* const kSkript =
    "//Generated by BehavEd\n"
    "\n"
    "rem ( \"Selbsttest\" );\n"
    "if ( $get( FLOAT, \"rand\")$, $>$, $1$ )\n"
    "{\n"
    "\twait ( 100.000 );\n"
    "}\n"
    "\n"
    "if ( $random( 0, 1 ) > 0.500000$ )\n"
    "{\n"
    "\twait ( 100.000 );\n"
    "}\n"
    "\n"
    // Mit Markierung, wie BehavEd sie schreibt: ohne /*@SET_TYPES*/
    // oeffnet das Original set als Variablen-set (zwei Textfelder).
    "set ( /*@SET_TYPES*/ \"SET_PARM1\", \"abc\" );\n"
    "wait ( 1000.000 );\n"
    "camera ( /*@CAMERA_COMMANDS*/ PAN, < -10.000 107.000 0.000 >, "
    "< 0.000 0.000 0.000 >, 0 );\n"
    "camera ( /*@CAMERA_COMMANDS*/ ENABLE );\n"
    "sound ( CHAN_VOICE_GLOBAL, \"sound/md_twj/s3_kick.mp3\" );\n"
    "affect ( \"anakin2\", /*@AFFECT_TYPE*/ FLUSH )\n"
    "{\n"
    "\twait ( 100.000 );\n"
    "}\n"
    "\n"
    "if ( $get( FLOAT, \"rand\")$, $>$, $2$ )\n"
    "{\n"
    "\twait ( 100.000 );\n"
    "}\n"
    "\n"
    "else (  )\n"
    "{\n"
    "\twait ( 100.000 );\n"
    "}\n"
    "\n"
    "print ( \"Ahsoka trusted you\" );\n";

struct Ruhe {
    int fall = 0;
    int runde = 0;
    int imFall = 0;        // Bilder im laufenden Fall
    bool vorSichtbar = false;
    ImVec2 vorPos{};
    ImVec2 vorGroesse{};
    int sichtbar = 0;
    int ruck = 0;
    int ruckGesamt = 0;
    ImVec2 griff{};        // Ziehen: wo die Maus die Titelzeile packt
    ImVec2 vorZiehen{};    // Ziehen: Fensterstelle davor
    int nachLoslassen = 0; // Ziehen: Aenderungen nach dem Loslassen
    std::string bericht;
};

// Ein Bild des Ruhetests. Liefert true, wenn alle Faelle durch sind.
bool ruheBild(Ruhe& t) {
    if (t.runde >= kRuheRunden) { return true; }
    const RuheFall& f = kRuheFaelle[t.fall];
    if (t.imFall == 0) {
        Path weg;
        if (!wegZu(f.befehl, f.nte, weg)) {
            diag::detail(std::string("Selbsttest: Befehl fehlt fuer ") + f.name);
            t.runde = kRuheRunden;
            return true;
        }
        openEditorForNode(weg);
        t.vorSichtbar = false;
        t.sichtbar = 0;
        t.ruck = 0;
    }
    ++t.imFall;

    if (t.imFall <= kBilderOffen) {
        if (f.ausdruckAn && t.imFall == 30 && !g_app->editorIsExpr.empty()) {
            // Expr! auf Feld 0 umschalten - wie ein Klick. Das Wachsen
            // danach ist gewollt; gezaehlt wird, wie oft es SPRINGT.
            g_app->editorIsExpr[0] = g_app->editorIsExpr[0] != 0 ? 0 : 1;
            diag::detail("Selbsttest: Expr! umgeschaltet");
        }
        ImGuiWindow* w = ImGui::FindWindowByName("###editor");
        const bool zu = (w == nullptr || !w->Active || w->Hidden);
        // Ziehen wie mit der Maus: Titelzeile packen, zehn Bilder lang je
        // fuenf Punkte nach rechts, loslassen. Das Fenster muss um fuenfzig
        // Punkte wandern und danach dort bleiben.
        if (f.ziehen && !zu) {
            ImGuiIO& io = ImGui::GetIO();
            if (t.imFall == 20) {
                t.griff = ImVec2{w->Pos.x + w->Size.x * 0.5F,
                                 w->Pos.y + w->TitleBarHeight * 0.5F};
                t.vorZiehen = w->Pos;
                setzeMaus(t.griff);
            } else if (t.imFall == 21) {
                io.AddMouseButtonEvent(0, true);
            } else if (t.imFall >= 22 && t.imFall <= 31) {
                setzeMaus(ImVec2{t.griff.x + 5.0F * static_cast<float>(t.imFall - 21),
                                 t.griff.y});
            } else if (t.imFall == 32) {
                io.AddMouseButtonEvent(0, false);
            }
        }
        if (!zu) {
            const bool anders =
                t.vorSichtbar &&
                (std::fabs(w->Pos.x - t.vorPos.x) >= 0.5F ||
                 std::fabs(w->Pos.y - t.vorPos.y) >= 0.5F ||
                 std::fabs(w->Size.x - t.vorGroesse.x) >= 0.5F ||
                 std::fabs(w->Size.y - t.vorGroesse.y) >= 0.5F);
            if (!t.vorSichtbar || anders) {
                char z[260];
                std::snprintf(z, sizeof(z),
                              "Selbsttest %s R%d Bild %d: %.0f/%.0f %.0fx%.0f "
                              "expr0 %d zelle %.0f bestellt %.0f reihe %.0f",
                              f.name, t.runde, t.imFall,
                              static_cast<double>(w->Pos.x),
                              static_cast<double>(w->Pos.y),
                              static_cast<double>(w->Size.x),
                              static_cast<double>(w->Size.y),
                              istAusdruck(0) ? 1 : 0,
                              static_cast<double>(g_app->dbgZelle0),
                              static_cast<double>(g_app->dbgBestellt0),
                              static_cast<double>(g_app->editorRowW));
                diag::detail(z);
            }
            if (anders) { ++t.ruck; }
            if (anders && f.ziehen && t.imFall > 35) { ++t.nachLoslassen; }
            ++t.sichtbar;
            t.vorSichtbar = true;
            t.vorPos = w->Pos;
            t.vorGroesse = w->Size;
        }
        return false;
    }
    if (t.imFall == kBilderOffen + 1) {
        char z[200];
        if (f.ziehen) {
            ImGuiWindow* w = ImGui::FindWindowByName("###editor");
            const ImVec2 weg = (w != nullptr)
                                   ? ImVec2{w->Pos.x - t.vorZiehen.x,
                                            w->Pos.y - t.vorZiehen.y}
                                   : ImVec2{0.0F, 0.0F};
            std::snprintf(z, sizeof(z),
                          "Selbsttest %-10s R%d: gezogen um %.0f/%.0f "
                          "(erwartet 50/0), %d Aenderungen nach dem Loslassen\n",
                          f.name, t.runde, static_cast<double>(weg.x),
                          static_cast<double>(weg.y), t.nachLoslassen);
            // Das Ziehen selbst ist gewollt - gezaehlt wird nur, was danach
            // noch wackelt.
            t.ruck = t.nachLoslassen;
            t.nachLoslassen = 0;
        } else {
            std::snprintf(z, sizeof(z),
                          "Selbsttest %-10s R%d: %2d Rucke in %d sichtbaren Bildern\n",
                          f.name, t.runde, t.ruck, t.sichtbar);
        }
        t.bericht += z;
        // Ein gewolltes Wachsen nach Expr! ist ein Sprung - er zaehlt mit,
        // soll aber einer bleiben.
        t.ruckGesamt += t.ruck;
        g_app->editorOpen = false;
        return false;
    }
    if (t.imFall > kBilderOffen + kBilderPause) {
        t.imFall = 0;
        if (++t.fall >= kRuheFallZahl) {
            t.fall = 0;
            ++t.runde;
        }
    }
    return false;
}

// --- Ablauf ---------------------------------------------------------------

struct Ablauf {
    int bild = 0;
    int teil = 0;          // 0 Vorlauf, 1 Ruhe, 2 Knoepfe, 3 fertig
    Ruhe ruhe;
    std::vector<Schritt> schritte;
    std::size_t schritt = 0;
    int imSchritt = 0;
    int fehlerVorher = 0;  // Protokollfehler vor dem Test (Zusicherungen)
};

Ablauf g_ablauf;

}  // namespace

void selbsttestVorBild() {
    if (g_mausAn) {
        ImGui::GetIO().AddMousePosEvent(g_maus.x, g_maus.y);
    }
}

void selbsttest() {
    static const char* const modus = std::getenv("BHED_EDITORTEST");
    if (modus == nullptr || g_app == nullptr) { return; }
    // BHED_EDITORTEST=geteilt: nur die geteilte Ansicht (schnell, zum
    // Wiederholen). Jeder andere Wert: alles.
    static const bool nurGeteilt = std::strcmp(modus, "geteilt") == 0;
    static const bool nurOriginal = std::strcmp(modus, "original") == 0;
    static const bool nurKarte = std::strcmp(modus, "karte") == 0;
    static const bool nurVergleich = std::strcmp(modus, "vergleich") == 0;
    static const bool nurZiehen = std::strcmp(modus, "ziehen") == 0;
    static const bool nurLesezeichen = std::strcmp(modus, "lesezeichen") == 0;
    static const bool nurIcons = std::strcmp(modus, "icons") == 0;
    static const bool nurLava = std::strcmp(modus, "lava") == 0;
    static const bool nurHolo = std::strcmp(modus, "holo") == 0;
    static const bool nurOrt = std::strcmp(modus, "ort") == 0;
    static const bool nurKamera = std::strcmp(modus, "kamera") == 0;
    static const bool nurAlleKarten = std::strcmp(modus, "allekarten") == 0;
    static const bool nurAblauf = std::strcmp(modus, "ablauf") == 0;
    static const bool nurZeitleiste = std::strcmp(modus, "zeitleiste") == 0;
    static const bool nurFenster = std::strcmp(modus, "fenster") == 0;
    static const bool nurUpdate = std::strcmp(modus, "update") == 0;
    static const bool nurKuerzel = std::strcmp(modus, "kuerzel") == 0;
    static const bool nurSzenen = std::strcmp(modus, "szenen") == 0;
    // Nur die Reiterpruefungen (Schliessen, doppelt Oeffnen, Ungesichertes) -
    // schnell nachzustellen, ohne den ganzen Lauf.
    static const bool nurReiter = std::strcmp(modus, "reiter") == 0;
    // Ein zweiter Start reicht an diesen weiter (braucht BHED_EINZEL).
    static const bool nurEinzel = std::strcmp(modus, "einzel") == 0;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    g.TestEngineHookItems = true;
    Ablauf& a = g_ablauf;
    ++a.bild;

    if (a.teil == 0) {
        if (a.bild == 1) {
            // Der Test erwartet die Zusatzfragen ("New?", "Exit?"). Den
            // Stand des Benutzers merken - er wird beim Beenden gespeichert.
            g_queryVorher = g_app->settings.queryOnDiscard;
            g_app->settings.queryOnDiscard = true;
            // Kein Ton aus dem Lautsprecher, solange getestet wird.
            g_app->stumm = true;
        }
        // Eine Startfrage ("Auto-load most recent file?", nach einem Absturz
        // "didn't exit BehavEd legally") wuerde jeden Klick des Tests
        // abfangen - mit Nein beantworten, der Test laedt seine Skripte
        // selbst.
        if (g_app->frageOffen && a.bild < kVorlauf) {
            diag::detail("Selbsttest: Startfrage mit Nein beantwortet: " + g_app->frageText);
            g_app->frageOffen = false;
            g_app->frageJa = nullptr;
            g_app->frageNein = nullptr;
        }
        if (a.bild == kVorlauf) {
            openScriptFromMemory(kSkript, "selbsttest.txt");
            diag::detail("Selbsttest: Skript geladen");
        }
        if (a.bild >= kVorlauf + 5) {
            a.teil = 1;
            if (nurZeitleiste) {
                a.schritte = zeitleisteSchritte();
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurAblauf) {
                a.schritte = ablaufSchritte();
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurEinzel) {
                a.schritte = einzelSchritte();
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurReiter) {
                a.schritte = reiterSchliessenSchritte();
                for (Schritt& x : schutzBeendenSchritte()) { a.schritte.push_back(std::move(x)); }
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurSzenen) {
                a.schritte = szenenSchritte();
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurAlleKarten) {
                a.schritte = alleKartenSchritte();
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurKuerzel) {
                a.schritte = aehnlichSchritte();
                for (Schritt& x : wunschSchritte()) { a.schritte.push_back(std::move(x)); }
                for (Schritt& x : zonenSchritte()) { a.schritte.push_back(std::move(x)); }
                for (Schritt& x : revertSchritte()) { a.schritte.push_back(std::move(x)); }
                for (Schritt& x : undoListeSchritte()) { a.schritte.push_back(std::move(x)); }
                for (Schritt& x : reiterSchliessenSchritte()) { a.schritte.push_back(std::move(x)); }
                for (Schritt& x : sicherungSchritte()) { a.schritte.push_back(std::move(x)); }
                for (Schritt& x : kuerzelAlleSchritte()) { a.schritte.push_back(std::move(x)); }
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurUpdate) {
                a.schritte = updateSchritte();
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurFenster) {
                a.schritte = teilerSchritte();
                for (Schritt& x : fensterSchritte()) { a.schritte.push_back(std::move(x)); }
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurIcons) {
                a.schritte = iconSchritte();
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurLesezeichen) {
                a.schritte = lesezeichenSchritte();
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurZiehen) {
                a.schritte = ziehSchritte();
                for (Schritt& x : ziehBreitSchritte()) { a.schritte.push_back(std::move(x)); }
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurVergleich) {
                a.schritte = vergleichSchritte();
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurKarte || nurLava || nurHolo || nurOrt || nurKamera) {
                a.schritte = karteSchritte();
                if (nurLava || nurHolo || nurOrt || nurKamera) {
                    // Nur laden (die ersten Schritte von karteSchritte bis
                    // "Kartenansicht gezeichnet"), dann Lava oder Holotisch.
                    a.schritte.resize(std::min<std::size_t>(a.schritte.size(), 8));
                    for (Schritt& x : (nurLava ? lavaSchritte() : nurHolo ? holoSchritte()
                                       : nurKamera ? kameraSchritte() : ortSchritte())) {
                        a.schritte.push_back(std::move(x));
                    }
                } else {
                    for (Schritt& x : kartenBreitSchritte()) { a.schritte.push_back(std::move(x)); }
                    for (Schritt& x : kameraSchritte()) { a.schritte.push_back(std::move(x)); }
                }
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurOriginal) {
                openScriptFromMemory(kSkript, "original.txt");
                a.schritte = originalSchritte();
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            } else if (nurGeteilt) {
                a.schritte = geteiltSchritte();
                for (Schritt& x : endeSchritte(false)) { a.schritte.push_back(std::move(x)); }
                a.teil = 2;
            }
        }
    } else if (a.teil == 1) {
        if (ruheBild(a.ruhe)) {
            char z[120];
            std::snprintf(z, sizeof(z), "Selbsttest Ruhe fertig: %d Rucke gesamt",
                          a.ruhe.ruckGesamt);
            diag::detail(a.ruhe.bericht);
            diag::detail(z);
            // Frisches Skript fuer die Knoepfe - der Ruhetest hat nichts
            // geaendert, aber die Klickpruefung soll nicht davon abhaengen.
            openScriptFromMemory(kSkript, "knopftest.txt");
            a.schritte = knopfSchritte();
            for (Schritt& x : befehlSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : geteiltSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : ziehSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : ziehBreitSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : lesezeichenSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : fensterSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : aehnlichSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : wunschSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : zonenSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : revertSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : undoListeSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : reiterSchliessenSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : sicherungSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : zeitleisteSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : teilerSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : kuerzelAlleSchritte()) { a.schritte.push_back(std::move(x)); }
            for (Schritt& x : endeSchritte(true)) { a.schritte.push_back(std::move(x)); }
            a.teil = 2;
        }
    } else if (a.teil == 2) {
        if (a.schritt < a.schritte.size()) {
            // Kopien, keine Verweise: ein Schritt darf weitere hinter sich
            // einfuegen (fuelleAb), und dabei zieht der Vektor um.
            const std::string text = a.schritte[a.schritt].text;
            // Dauerbeobachtung der Action-Leiste: jede Aenderung von Ort
            // oder Groesse des Delete-Knopfs, mit dem laufenden Schritt.
            // shank, 27.09.: "manchmal beim verschieben springt die action
            // leiste und wird groesser".
            {
                static ImRect vorher;
                static bool gesehen = false;
                if (const Element* e = finde(tr(Str::AppExit), "buttons", 0, false)) {
                    const ImRect& jetzt = e->rect;
                    if (gesehen && (std::fabs(jetzt.Min.x - vorher.Min.x) > 0.5F ||
                                    std::fabs(jetzt.Min.y - vorher.Min.y) > 0.5F ||
                                    std::fabs(jetzt.GetWidth() - vorher.GetWidth()) > 0.5F ||
                                    std::fabs(jetzt.GetHeight() - vorher.GetHeight()) > 0.5F)) {
                        char z[300];
                        std::snprintf(z, sizeof(z),
                                      "Leiste: Exit %.0f/%.0f %.0fx%.0f -> %.0f/%.0f %.0fx%.0f "
                                      "(Schritt \"%s\", Bild %d)",
                                      static_cast<double>(vorher.Min.x), static_cast<double>(vorher.Min.y),
                                      static_cast<double>(vorher.GetWidth()), static_cast<double>(vorher.GetHeight()),
                                      static_cast<double>(jetzt.Min.x), static_cast<double>(jetzt.Min.y),
                                      static_cast<double>(jetzt.GetWidth()), static_cast<double>(jetzt.GetHeight()),
                                      text.c_str(), a.imSchritt);
                        diag::detail(z);
                    }
                    vorher = jetzt;
                    gesehen = true;
                }
                // Tooltips (Hinweise beim Draufzeigen) mitschreiben - sie
                // gehen neben der Leiste auf, wenn die Testmaus auf einem
                // Knopf stehen bleibt.
                static bool tipVorher = false;
                bool tip = false;
                for (ImGuiWindow* w : GImGui->Windows) {
                    if (w->Active && !w->Hidden && (w->Flags & ImGuiWindowFlags_Tooltip) != 0 &&
                        std::strncmp(w->Name, "##Tooltip", 9) == 0) {
                        tip = true;
                        if (!tipVorher) {
                            char z[260];
                            std::snprintf(z, sizeof(z),
                                          "Tooltip: %.0f/%.0f %.0fx%.0f (Schritt \"%s\", %s)",
                                          static_cast<double>(w->Pos.x), static_cast<double>(w->Pos.y),
                                          static_cast<double>(w->Size.x), static_cast<double>(w->Size.y),
                                          text.c_str(), wasImGuiSieht().c_str());
                            diag::detail(z);
                        }
                    }
                }
                tipVorher = tip;
                // Fokuswechsel des Programmfensters: verliert es den Fokus
                // (Klick in ein anderes Programm), setzt ImGui die
                // Maustasten zurueck - ein laufender Zug bricht dann ab.
                static bool fokusVorher = true;
                const bool fokus = !ImGui::GetIO().AppFocusLost;
                if (fokus != fokusVorher) {
                    diag::detail(std::string("Fokus des Programmfensters: ") +
                                 (fokus ? "zurueck" : "VERLOREN") + " (Schritt \"" + text + "\")");
                    fokusVorher = fokus;
                }
            }
            const std::function<bool(int)> tun = a.schritte[a.schritt].tun;
            g_liste = &a.schritte;
            g_einfuegeAn = a.schritt + 1U;
            if (a.imSchritt == 0) { diag::detail("Knopftest: " + text); }
            if (tun(a.imSchritt)) {
                ++a.schritt;
                a.imSchritt = 0;
            } else {
                ++a.imSchritt;
                // Kein Schritt darf ewig laufen - aber Warten (Laden,
                // Abspielen) darf bei ~400 Bildern je Sekunde ein paar
                // Sekunden dauern.
                if (a.imSchritt > 4000) {
                    meldeFehler(text + ": haengt");
                    ++a.schritt;
                    a.imSchritt = 0;
                }
            }
        } else {
            // Nach "No" haette das Programm schon zu sein.
            meldeFehler("Beenden: Programm laeuft nach \"No\" weiter");
            a.teil = 3;
            g_app->quitConfirmed = true;
            g_app->wantQuit = true;
        }
    }
    g_elemente.clear();
}

}  // namespace bhed::gui

// --- Die Haken, die ImGui mit IMGUI_ENABLE_TEST_ENGINE aufruft -----------
//
// Sie laufen nur, solange `TestEngineHookItems` gesetzt ist - also nur im
// Selbsttest. Aussen im globalen Namensraum, so deklariert sie
// imgui_internal.h.

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb,
                                 const ImGuiLastItemData* /*item_data*/) {
    bhed::gui::selbsttestMerkeElement(ctx, id, bb);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label,
                                  ImGuiItemStatusFlags /*flags*/) {
    bhed::gui::selbsttestMerkeText(ctx, id, label);
}

void ImGuiTestEngineHook_Log(ImGuiContext* /*ctx*/, const char* /*fmt*/, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* /*ctx*/, ImGuiID /*id*/) {
    return nullptr;
}
