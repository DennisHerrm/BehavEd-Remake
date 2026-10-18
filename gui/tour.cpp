// tour.cpp - die Tutorials im Programm: Fenster, Hervorhebung, Auswertung
//
// Paket tour (rc601), siehe gui/tour.h und include/bhed/tour.h.
//
// Der Betreuer wollte Tutorials, bei denen "das Programm selbst zeigt, welche
// Knoepfe man druecken muss, und Schritt fuer Schritt fuehrt" - erst ohne KI.
// Ablauf je Bild (tourBild, aus draw() vor selbsttest()):
//
//   1. den Programmzustand als tour::Kontext aufnehmen (Reiter, Befehle im
//      Skript, Auswahl, Editor, Fenster, Modus ...);
//   2. die Zielangaben des Schritts im Element-Register (gui/elemente.h)
//      aufloesen - fehlt die Vorbedingung, den Oeffner (wie Unreals
//      TabToFocusOrOpen); eine Menuekette zeigt ihr tiefstes offenes Glied;
//   3. die Pruefung des Schritts auswerten (tour::Ablauf) - erledigt ist ein
//      Schritt, wenn der ZUSTAND eintritt, egal ueber welchen Weg (Knopf,
//      Menue, Taste, Ziehen), wie bei den Walkthroughs von VS Code;
//   4. fuer das naechste Bild genau die Beschriftungen anmelden, die die Tour
//      braucht (Stufe "Gefiltert" - ausserhalb einer Tour laeuft kein Haken);
//   5. Fenster und Hervorhebung zeichnen: abgedunkelt bis auf das Ziel und das
//      Fenster (nur gezeichnet - Klicks gehen durch, wie bei Shepherd.js mit
//      useModalOverlay), gelber Rahmen wie die Bilder im Handbuch
//      (tools/handbuch/bilder.py: GELB = (255, 214, 10)), nummeriert, wenn ein
//      Schritt mehrere Ziele hat.
//
// "Zeig es mir" fuehrt nur einen Geisterzeiger zum Ziel - es wird nichts
// geklickt. "Mach es fuer mich" fuehrt die Aktionen eines Schritts aus, der
// als sicher markiert ist (inhalt_touren.py) - direkt am Programmzustand,
// ohne simulierte Maus (die kaempfte mit der echten, selbsttest.cpp oben)
// und nie bei Schritten mit Dateidialog.
#include "tour.h"

#include "app_internal.h"
#include "elemente.h"
#include "bhed/tour.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace bhed::gui {
namespace {

// Die Farbe der Rahmen in den Handbuchbildern (tools/handbuch/bilder.py: GELB).
constexpr ImU32 kGelb = IM_COL32(255, 214, 10, 255);
constexpr ImU32 kGelbBlass = IM_COL32(255, 214, 10, 110);
// Die Kennungen im Schritttext: dasselbe Gelb, etwas dunkler, damit es auch
// auf hellem Grund lesbar bleibt.
constexpr ImVec4 kKennungFarbe{0.93F, 0.68F, 0.05F, 1.0F};
constexpr ImVec4 kGruen{0.35F, 0.80F, 0.40F, 1.0F};

// --- Zustand -------------------------------------------------------------------

struct Zustand {
    bool fensterOffen = false;
    tour::Touren touren;
    bool geladen = false;
    std::string geladenSprache;
    std::string ordner;            // Handbuchordner, in dem touren.json lag
    std::string ladeFehler;
    tour::Ablauf ablauf;
    std::string laufendeId;
    // Was dieses Bild zeigt
    std::vector<ImRect> ziele;
    ImGuiID zielKennung = 0;
    std::string zielInnen;         // Fenster des ersten Ziels (zum Hinrollen)
    bool zielGanzSichtbar = true;
    bool zeigtOeffner = false;
    bool overlay = false;
    std::vector<ImRect> loecher;   // was die Abdunklung ausspart (fuer den Selbsttest)
    bool angemeldet = false;
    bool zielErwartet = false;     // der Schritt hat eine Zielangabe
    // Rueckmeldung
    int letzterSchritt = -1;
    bool warFertig = false;
    double pulsBis = 0.0;
    double schwebtSeit = -1.0;
    bool gerollt = false;
    // Zeig es mir
    double geistStart = -1.0;
    ImVec2 geistVon{};
    // Sichern zaehlen (fuer gespeichert+)
    std::string letzterPfad;
    bool letztSchmutzig = true;
    int letzterReiter = -1;
    int gesichert = 0;
    // Das Fenster
    ImRect fensterRect;
    bool fensterNeuSetzen = true;
    std::string hoverKennung;      // eine {{Kennung}} im Text, ueber der die Maus steht
    // rc601 tour (Review): das Fenster wurde in diesem Bild schon aus einem
    // Modal heraus gezeichnet (tourImModal) - tourBild laesst es dann aus.
    int fensterImModalBild = -1;
    // rc601 tour (Review): die Skriptansicht vor dem Umschalten auf Classic
    // (-1 = nicht umgeschaltet) - kommt beim Schliessen/Ende zurueck.
    int ansichtVorher = -1;
    // Suche im Handbuch (Phase 3)
    char suche[160] = "";
    std::string letzteAnfrage;
    std::vector<tour::Abschnitt> index;
    std::string indexSprache;
    bool indexVersucht = false;
    std::vector<tour::Treffer> treffer;
};

Zustand& z() {
    static Zustand s;
    return s;
}

const char* sprachCode() {
    switch (i18n::currentLanguage()) {
        case i18n::Language::German: return "de";
        case i18n::Language::ChineseSimplified: return "zh";
        case i18n::Language::Japanese: return "ja";
        default: return "en";
    }
}

std::filesystem::path pfadU8(const std::string& s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

bool gibtEsDatei(const std::string& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(pfadU8(p), ec);
}

std::string leseDatei(const std::string& p) {
    std::ifstream f(pfadU8(p), std::ios::binary);
    if (!f) { return {}; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Der Handbuchordner (handbuch/<sprache>/touren.json). Wie handbuchOeffnen:
// neben behaved.exe, beim Entwickeln eine bis drei Ebenen hoeher (build/bin/
// Release -> Projektwurzel). BHED_HANDBUCH geht vor (Selbsttest, Entwickeln).
std::string handbuchOrdner() {
    std::vector<std::string> kandidaten;
    if (const char* e = std::getenv("BHED_HANDBUCH"); e != nullptr && *e != '\0') { kandidaten.emplace_back(e); }
    const std::string exe = platform::executableDirectory();
    for (const char* hoch : {"", "\\..", "\\..\\..", "\\..\\..\\.."}) { kandidaten.push_back(exe + hoch + "\\handbuch"); }
    for (const std::string& k : kandidaten) {
        if (gibtEsDatei(k + "\\en\\touren.json")) { return k; }
    }
    return {};
}

std::string kennungText(const std::string& name) {
    i18n::Str s{};
    return i18n::strVonName(name, &s) ? std::string(tr(s)) : name;
}

// --- Laden ------------------------------------------------------------------------

void speichereStand() {
    Zustand& s = z();
    if (!s.ablauf.aktiv() || g_app == nullptr) { return; }
    g_app->settings.tourStand[s.laufendeId] =
        tour::standText(s.ablauf.schritt(), s.ablauf.anzahl(), s.ablauf.fertig());
}

// touren.json der eingestellten Sprache (sonst Englisch) - beim ersten Bedarf
// und nach einem Sprachwechsel. Eine laufende Tour beginnt danach am selben
// Schritt neu (sie haengt an den Daten der alten Sprache).
void laden() {
    Zustand& s = z();
    const std::string sp = sprachCode();
    if (s.geladen && s.geladenSprache == sp) { return; }
    const int schritt = s.ablauf.aktiv() ? s.ablauf.schritt() : -1;
    const std::string ansicht = s.ablauf.aktiv() ? s.ablauf.ansicht() : std::string();
    s.ablauf.schliesse();
    s.geladen = true;
    s.geladenSprache = sp;
    s.touren = tour::Touren{};
    s.ladeFehler.clear();
    s.ordner = handbuchOrdner();
    if (s.ordner.empty()) {
        s.ladeFehler = "handbuch\\<language>\\touren.json";
        diag::detail("Tour: touren.json nicht gefunden (neben behaved.exe oder bis drei Ebenen hoeher, BHED_HANDBUCH)");
        return;
    }
    std::string datei = s.ordner + "\\" + sp + "\\touren.json";
    if (!gibtEsDatei(datei)) { datei = s.ordner + "\\en\\touren.json"; }
    std::string f;
    if (!tour::ladeTouren(leseDatei(datei), s.touren, &f)) {
        s.ladeFehler = datei + ": " + f;
        diag::detail("Tour: " + s.ladeFehler);
        return;
    }
    diag::detail("Tour: " + std::to_string(s.touren.liste.size()) + " Tutorials aus " + datei);
    if (schritt >= 0) {
        if (const tour::Tour* t = s.touren.finde(s.laufendeId)) { (void)s.ablauf.starte(*t, ansicht, schritt); }
    }
}

// --- Programmzustand ----------------------------------------------------------------

void zaehle(const std::vector<Node>& ns, tour::Kontext& k, int& befehle) {
    for (const Node& n : ns) {
        if (n.kind == Node::Kind::Command) {
            ++k.befehle[n.name];
            ++befehle;
        } else if (n.kind == Node::Kind::Macro) {
            ++befehle;
        } else if (n.kind == Node::Kind::LineComment && remInhalt(n.raw, nullptr)) {
            ++k.rem;
        }
        zaehle(n.children, k, befehle);
    }
}

int knotenZahl(const std::vector<Node>& ns) {
    int z0 = 0;
    for (const Node& n : ns) {
        if (n.kind == Node::Kind::Command || n.kind == Node::Kind::Macro) { ++z0; }
        z0 += knotenZahl(n.children);
    }
    return z0;
}

// Der n-te Befehl dieses Namens in Baumreihenfolge ("*": der erste Befehl).
bool findePfad(const std::vector<Node>& ns, const std::string& befehl, int& rest, Path& weg) {
    for (std::size_t i = 0; i < ns.size(); ++i) {
        weg.push_back(i);
        const Node& n = ns[i];
        if (n.kind == Node::Kind::Command && (befehl == "*" || n.name == befehl)) {
            if (rest == 0) { return true; }
            --rest;
        }
        if (findePfad(n.children, befehl, rest, weg)) { return true; }
        weg.pop_back();
    }
    return false;
}

bool pfadZu(const std::string& befehl, int nte, Path& weg) {
    weg.clear();
    int rest = nte;
    return findePfad(g_app->doc.script().nodes, befehl, rest, weg);
}

bool hatKindIn(const std::vector<Node>& ns, const std::string& block, const std::string& befehl) {
    for (const Node& n : ns) {
        if (n.kind == Node::Kind::Command && n.name == block) {
            for (const Node& c : n.children) {
                if (c.kind == Node::Kind::Command && c.name == befehl) { return true; }
            }
        }
        if (hatKindIn(n.children, block, befehl)) { return true; }
    }
    return false;
}

tour::Kontext kontextBauen() {
    Zustand& s = z();
    tour::Kontext k;
    k.reiter = static_cast<int>(g_app->tabs.size());
    for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
        const Document& d = (static_cast<int>(i) == g_app->activeTab) ? g_app->doc : g_app->tabs[i].doc;
        k.knoten += knotenZahl(d.script().nodes);
        // rc601 tour (Review): stand() statt undoDepth() - die Rueckgaengig-
        // Liste endet bei Document::kMaxUndo (200), danach zaehlte kein
        // Verschieben mehr ("aenderung+"). stand() zaehlt jede Aenderung.
        k.aenderungen += static_cast<int>(d.stand() & 0x3fffffffU);
    }
    int befehle = 0;
    zaehle(g_app->doc.script().nodes, k, befehle);
    k.leer = befehle == 0 && k.rem == 0;
    k.aktivGesichert = !g_app->path.empty() && !g_app->doc.dirty();
    // Ein Sichern: im selben Reiter von "geaendert/ohne Pfad" zu "gesichert".
    if (g_app->activeTab == s.letzterReiter && k.aktivGesichert && (s.letztSchmutzig || s.letzterPfad != g_app->path)) {
        ++s.gesichert;
    }
    s.letzterReiter = g_app->activeTab;
    s.letztSchmutzig = !k.aktivGesichert;
    s.letzterPfad = g_app->path;
    k.gespeichert = s.gesichert;
    // Der Uebersetzen-Knopf blinkt bis kompiliertBis (gruen) bzw.
    // kompiliertFehlerBis (rot): jedes Uebersetzen schiebt einen der beiden.
    k.uebersetzt = std::max(g_app->kompiliertBis, g_app->kompiliertFehlerBis);
    k.auswahlZahl = static_cast<int>(g_app->selection.size());
    if (!g_app->selectedPath.empty()) {
        if (const Node* n = nodeAt(g_app->doc.script(), g_app->selectedPath); n != nullptr && n->kind == Node::Kind::Command) {
            k.auswahl = n->name;
            k.auswahlZahl = std::max(k.auswahlZahl, 1);
        }
    }
    k.editorOffen = g_app->editorOpen;
    k.editorBefehl = (g_app->editorOpen && g_app->editorCmd != nullptr) ? g_app->editorCmd->name : std::string();
    if (g_app->messagesOpen) { k.fenster.emplace_back("messages"); }
    if (g_app->findOpen) { k.fenster.emplace_back("find"); }
    k.modus = g_app->leftMode;
    k.ansicht = g_app->settings.flussAnsicht;
    k.hatKind = [](const std::string& block, const std::string& befehl) {
        return hatKindIn(g_app->doc.script().nodes, block, befehl);
    };
    k.argument = [](const std::string& befehl, int nte, int feld) {
        Path p;
        if (!pfadZu(befehl, nte, p)) { return std::string(); }
        const Node* n = nodeAt(g_app->doc.script(), p);
        return (n != nullptr && feld >= 0 && static_cast<std::size_t>(feld) < n->args.size())
                   ? n->args[static_cast<std::size_t>(feld)].text
                   : std::string();
    };
    return k;
}

// --- Ziele aufloesen ------------------------------------------------------------------

struct Gefunden {
    bool da = false;
    ImRect r;
    const elemente::Element* e = nullptr;
};

ImRect fensterRect(const char* teil) {
    for (ImGuiWindow* w : GImGui->Windows) {
        if (w->WasActive && !w->Hidden && std::strstr(w->Name, teil) != nullptr) { return w->Rect(); }
    }
    return ImRect{};
}

// Auf den sichtbaren Teil beschnitten (wie hb::knopfN im Selbsttest).
Gefunden aus(const elemente::Element* e) {
    Gefunden g;
    if (e == nullptr) { return g; }
    ImRect c = e->rect;
    c.ClipWithFull(e->clip);
    g.r = (c.GetWidth() > 1.0F && c.GetHeight() > 1.0F) ? c : e->rect;
    g.da = g.r.GetWidth() > 1.0F && g.r.GetHeight() > 1.0F;
    g.e = e;
    return g;
}

// Ein Menue der Kette: das oberste in der Menueleiste (bevorzugt mit der
// Fahne Openable, die BeginMenu meldet), tiefere in einem "##Menu"-Fenster.
const elemente::Element* findeMenue(const std::string& label, bool oben) {
    const elemente::Element* erstes = nullptr;
    for (int k = 0;; ++k) {
        const elemente::Element* e = elemente::finde(label, oben ? "" : "##Menu", k, false);
        if (e == nullptr) { break; }
        if (erstes == nullptr) { erstes = e; }
        if ((e->flags & ImGuiItemStatusFlags_Openable) != 0) { return e; }
    }
    return erstes;
}

Gefunden loese(const tour::Ziel& zl) {
    using A = tour::Ziel::Art;
    switch (zl.art) {
        case A::Knopf: return aus(elemente::finde(kennungText(zl.teile[0]), zl.fenster, 0, false));
        case A::KnopfN: return aus(elemente::finde(zl.teile[0], zl.fenster, zl.nte, false));
        case A::Menue: {
            // Das tiefste Glied, das gerade zu sehen ist: erst "File", nach dem
            // Aufklappen "New".
            for (std::size_t k = zl.teile.size(); k-- > 0;) {
                Gefunden g = aus(findeMenue(kennungText(zl.teile[k]), k == 0));
                if (g.da) { return g; }
            }
            return {};
        }
        case A::Gruppe: {
            Gefunden g;
            for (const std::string& t : zl.teile) {
                const Gefunden k = aus(elemente::finde(kennungText(t), zl.fenster, 0, false));
                if (!k.da) { continue; }
                if (!g.da) {
                    g = k;
                } else {
                    g.r.Add(k.r);
                }
            }
            return g;
        }
        case A::Eintrag: {
            // Die Ereignisliste beschriftet "affect  ( ... )".
            const elemente::Element* e = elemente::finde(zl.teile[0] + "  ", "", 0, true);
            return aus(e != nullptr ? e : elemente::finde(zl.teile[0], "events", 0, true));
        }
        case A::Zeile: {
            const elemente::Element* e = elemente::finde(zl.teile[0], "pane0", zl.nte, false);
            if (e == nullptr) { e = elemente::finde(zl.teile[0], "##flowarea", zl.nte, false); }
            Gefunden g = aus(e);
            if (g.da) {
                // Ueber die Breite des Baums, damit der Rahmen die Werte mit
                // umfasst (wie hb::zeile im Selbsttest).
                const ImRect baum = fensterRect("##flowarea");
                if (baum.GetWidth() > 0.0F) { g.r.Max.x = std::max(g.r.Max.x, baum.Min.x + baum.GetWidth() * 0.62F); }
            }
            return g;
        }
        case A::Reiter: {
            // Das "+" ganz oben rechts in der Reiterleiste ("+" heisst auch
            // "Expand all" bei den Baumoptionen).
            const elemente::Element* best = nullptr;
            for (int k = 0;; ++k) {
                const elemente::Element* e = elemente::finde("+", "###main", k, false);
                if (e == nullptr) { break; }
                if (e->rect.Min.y < ImGui::GetFrameHeight() * 3.0F && (best == nullptr || e->rect.Min.x > best->rect.Min.x)) {
                    best = e;
                }
            }
            return aus(best);
        }
        case A::Fenster: {
            Gefunden g;
            g.r = fensterRect(zl.teile[0].c_str());
            g.da = g.r.GetWidth() > 1.0F;
            return g;
        }
        case A::Baum: {
            Gefunden g;
            g.r = fensterRect("##flowarea");
            g.da = g.r.GetWidth() > 1.0F;
            return g;
        }
        case A::Meldungen: {
            const std::string f = std::string(" ") + tr(Str::StatusErrors);
            const std::string w = std::string(" ") + tr(Str::StatusWarnings);
            for (const elemente::Element& e : elemente::alle()) {
                if (e.label.empty() || e.fenster.find("###main") == std::string::npos) { continue; }
                const auto endet = [&e](const std::string& s) {
                    return e.label.size() > s.size() && e.label.compare(e.label.size() - s.size(), s.size(), s) == 0 &&
                           std::isdigit(static_cast<unsigned char>(e.label[0])) != 0;
                };
                if (endet(f) || endet(w)) { return aus(&e); }
            }
            return {};
        }
        case A::Keins: break;
    }
    return {};
}

// Die Beschriftungen, die die Stufe "Gefiltert" fuer diese Ziele behalten muss.
void wuensche(const std::vector<tour::Ziel>& zs, std::vector<std::string>& out) {
    using A = tour::Ziel::Art;
    for (const tour::Ziel& zl : zs) {
        switch (zl.art) {
            case A::Knopf:
            case A::Menue:
            case A::Gruppe:
                for (const std::string& t : zl.teile) { out.push_back(kennungText(t)); }
                break;
            case A::KnopfN:
            case A::Eintrag:
            case A::Zeile:
            case A::Reiter: out.push_back(zl.teile[0]); break;
            case A::Meldungen:
                out.push_back(std::string("*") + tr(Str::StatusErrors));
                out.push_back(std::string("*") + tr(Str::StatusWarnings));
                break;
            default: break;
        }
    }
}

// --- Aktionen (Ueberspringen, "Mach es") --------------------------------------------------

// rc601 tour (Review): den Ereigniseditor schliessen - auch in ImGuis
// Klappfenster-Stapel. Nur editorOpen=false liess das Modal dort stehen
// (drawEditor kehrt dann vor BeginPopupModal zurueck); so schliesst es wie
// Ok/Cancel mit CloseCurrentPopup. Geht auch aus dem Fenster heraus, das
// tourImModal im Begin-Stapel des Editors zeichnet.
void editorZu() {
    g_app->editorOpen = false;
    ImGuiContext& g = *GImGui;
    for (int i = 0; i < g.OpenPopupStack.Size; ++i) {
        const ImGuiWindow* w = g.OpenPopupStack[i].Window;
        if (w != nullptr && std::strstr(w->Name, "###editor") != nullptr) {
            ImGui::ClosePopupToLevel(i, true);
            break;
        }
    }
}

void aktionenAusfuehren(const std::string& text) {
    std::vector<tour::Aktion> as;
    std::string f;
    if (!tour::parseAktionen(text, as, &f)) {
        diag::detail("Tour: " + f);
        return;
    }
    for (const tour::Aktion& a : as) {
        diag::detail("Tour: Aktion " + a.name + (a.arg.empty() ? "" : ":" + a.arg));
        std::string befehl;
        int nte = 0;
        if (a.name == "neuerReiter") {
            neuerReiter();
        } else if (a.name == "befehl") {
            for (const Command& c : g_app->db.commands) {
                if (c.name == a.arg) {
                    fuegeBefehlEin(c);
                    break;
                }
            }
        } else if (a.name == "waehle") {
            tour::befehlUndNummer(a.arg, befehl, nte);
            Path p;
            if (pfadZu(befehl, nte, p)) { selectByPath(p); }
        } else if (a.name == "wert") {
            const std::size_t gl = a.arg.find('=');
            const std::string links = a.arg.substr(0, gl);
            const std::size_t dp = links.rfind(':');
            tour::befehlUndNummer(links.substr(0, dp), befehl, nte);
            const std::size_t feld = static_cast<std::size_t>(std::atoi(links.substr(dp + 1).c_str()));
            Path p;
            if (!pfadZu(befehl, nte, p)) { continue; }
            const Node* alt = nodeAt(g_app->doc.script(), p);
            if (alt == nullptr || feld >= alt->args.size()) { continue; }
            // Ein offener Editor auf derselben Zeile wuerde den Wert mit Ok
            // wieder ueberschreiben.
            if (g_app->editorOpen && g_app->editorPath == p) { editorZu(); }
            Node neu = *alt;
            neu.args[feld].text = a.arg.substr(gl + 1);
            if (g_app->doc.replaceAt(p, std::move(neu))) {
                rebuildTree();
                g_app->camTrackValid = false;
                g_app->mapDirty = true;
            }
        } else if (a.name == "modus") {
            g_app->leftMode = std::atoi(a.arg.c_str());
        } else if (a.name == "ansicht") {
            g_app->settings.flussAnsicht = std::atoi(a.arg.c_str());
        } else if (a.name == "editor") {
            Path p;
            if (!a.arg.empty()) {
                tour::befehlUndNummer(a.arg, befehl, nte);
                (void)pfadZu(befehl, nte, p);
            } else if (const Node* n = g_app->selectedPath.empty() ? nullptr : nodeAt(g_app->doc.script(), g_app->selectedPath);
                       n != nullptr && n->kind == Node::Kind::Command) {
                p = g_app->selectedPath;
            } else {
                (void)pfadZu("*", 0, p);
            }
            if (!p.empty()) { openEditorForNode(p); }
        } else if (a.name == "editorzu") {
            editorZu();
        } else if (a.name == "fenster") {
            if (a.arg == "messages") { g_app->messagesOpen = true; }
            if (a.arg == "find") { g_app->findOpen = true; }
        }
    }
}

// --- Text des Schritts --------------------------------------------------------------------

// Laufender Text mit **fett**, `code`, <kbd>Tasten</kbd> und {{Kennungen}} -
// die Kennungen in der eingestellten Sprache, so beschriftet wie im Programm.
// Umbrochen wird an Leerzeichen; CJK-Zeichen einzeln.
// `yVersatz`: neben einem Knopf auf dessen Textgrundlinie (FramePadding.y).
void zeichneSchrittText(const std::string& roh, float breite, float yVersatz = 0.0F) {
    Zustand& s = z();
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImDrawList* dl = w->DrawList;
    const ImVec2 oben = ImGui::GetCursorScreenPos();
    const ImVec2 start{oben.x, oben.y + yVersatz};
    const float zeile = ImGui::GetTextLineHeightWithSpacing();
    const float rechts = start.x + breite;
    const ImGuiStyle& st = ImGui::GetStyle();
    float x = start.x;
    float y = start.y;
    const bool fensterUnterMaus = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const auto umbruch = [&](float wbreite) {
        if (x + wbreite > rechts && x > start.x) {
            x = start.x;
            y += zeile;
            return true;
        }
        return false;
    };
    for (const tour::Stueck& p : tour::zerlege(roh)) {
        using A = tour::Stueck::Art;
        if (p.art == A::Code || p.art == A::Taste) {
            const ImVec2 g = ImGui::CalcTextSize(p.inhalt.c_str());
            const float rand = st.FramePadding.x * 0.6F;
            (void)umbruch(g.x + rand * 2.0F);
            const ImRect r{x, y, x + g.x + rand * 2.0F, y + ImGui::GetTextLineHeight()};
            if (p.art == A::Code) {
                dl->AddRectFilled(r.Min, r.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0F);
            } else {
                dl->AddRect(r.Min, r.Max, ImGui::GetColorU32(ImGuiCol_Border), 3.0F, 0, 1.0F);
            }
            dl->AddText(ImVec2{x + rand, y}, ImGui::GetColorU32(ImGuiCol_Text), p.inhalt.c_str());
            x = r.Max.x + st.ItemInnerSpacing.x * 0.3F;
            continue;
        }
        const bool kennung = p.art == A::Kennung;
        const std::string text = kennung ? kennungText(p.inhalt) : p.inhalt;
        const ImU32 farbe = kennung ? ImGui::GetColorU32(kKennungFarbe) : ImGui::GetColorU32(ImGuiCol_Text);
        // In Woerter (mit dem folgenden Leerzeichen) und CJK-Zeichen teilen.
        std::size_t i = 0;
        while (i < text.size()) {
            std::size_t e = i;
            const unsigned char c0 = static_cast<unsigned char>(text[i]);
            if (text[i] == '\n') {
                x = start.x;
                y += zeile;
                ++i;
                continue;
            }
            if (c0 >= 0xE0) {
                // Drei- oder Vierbytezeichen (CJK): einzeln umbrechbar.
                e = i + ((c0 >= 0xF0) ? 4U : 3U);
            } else {
                while (e < text.size() && text[e] != ' ' && text[e] != '\n' && static_cast<unsigned char>(text[e]) < 0xE0) { ++e; }
                while (e < text.size() && text[e] == ' ') { ++e; }
            }
            e = std::min(e, text.size());
            const std::string wort = text.substr(i, e - i);
            const float wb = ImGui::CalcTextSize(wort.c_str()).x;
            if (umbruch(wb) && wort[0] == ' ') {
                i = e;
                continue;
            }
            dl->AddText(ImVec2{x, y}, farbe, wort.c_str());
            if (p.fett) { dl->AddText(ImVec2{x + 1.0F, y}, farbe, wort.c_str()); }
            if (kennung) {
                const ImRect r{x, y, x + wb, y + ImGui::GetTextLineHeight()};
                dl->AddLine(ImVec2{r.Min.x, r.Max.y}, ImVec2{r.Max.x, r.Max.y}, kGelbBlass, 1.0F);
                if (fensterUnterMaus && ImGui::IsMouseHoveringRect(r.Min, r.Max, false)) { s.hoverKennung = p.inhalt; }
            }
            x += wb;
            i = e;
        }
    }
    ImGui::SetCursorScreenPos(oben);
    ImGui::Dummy(ImVec2{breite, y - oben.y + zeile});
}

// --- Hervorhebung ---------------------------------------------------------------------------

// Alles ausser den Loechern abdunkeln: waagerechte Streifen zwischen den
// Kanten aller Loecher, in jedem Streifen die Luecken zwischen ihnen fuellen.
void dimmeAusser(ImDrawList* dl, const ImRect& gesamt, const std::vector<ImRect>& loecher, ImU32 farbe) {
    std::vector<float> ys = {gesamt.Min.y, gesamt.Max.y};
    for (const ImRect& h : loecher) {
        ys.push_back(std::clamp(h.Min.y, gesamt.Min.y, gesamt.Max.y));
        ys.push_back(std::clamp(h.Max.y, gesamt.Min.y, gesamt.Max.y));
    }
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    for (std::size_t k = 0; k + 1 < ys.size(); ++k) {
        const float y0 = ys[k];
        const float y1 = ys[k + 1];
        if (y1 - y0 < 0.5F) { continue; }
        std::vector<std::pair<float, float>> xs;
        for (const ImRect& h : loecher) {
            if (h.Min.y < y1 && h.Max.y > y0) { xs.emplace_back(h.Min.x, h.Max.x); }
        }
        std::sort(xs.begin(), xs.end());
        float x = gesamt.Min.x;
        for (const auto& [a, b] : xs) {
            if (a > x) { dl->AddRectFilled(ImVec2{x, y0}, ImVec2{a, y1}, farbe); }
            x = std::max(x, b);
        }
        if (x < gesamt.Max.x) { dl->AddRectFilled(ImVec2{x, y0}, ImVec2{gesamt.Max.x, y1}, farbe); }
    }
}

// `frei`: offene Klappfenster und Hinweise - nicht abgedunkelt, nicht gerahmt.
void zeichneHervorhebung(const std::vector<ImRect>& ziele, const ImRect& fenster, float puls, const std::vector<ImRect>& frei) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float fs = ImGui::GetFontSize();
    const float pad = fs * 0.25F;
    std::vector<ImRect> loecher;
    for (const ImRect& r : ziele) {
        ImRect q = r;
        q.Expand(pad);
        loecher.push_back(q);
    }
    if (fenster.GetWidth() > 0.0F) { loecher.push_back(fenster); }
    loecher.insert(loecher.end(), frei.begin(), frei.end());
    dimmeAusser(dl, ImRect{0.0F, 0.0F, d.x, d.y}, loecher, IM_COL32(0, 0, 0, 80));
    z().loecher = loecher;
    for (std::size_t i = 0; i < ziele.size(); ++i) {
        ImRect q = ziele[i];
        q.Expand(pad);
        const float dicke = 2.5F + 2.5F * puls;
        dl->AddRect(q.Min, q.Max, kGelb, fs * 0.3F, 0, dicke);
        if (ziele.size() > 1) {
            // Nummer darunter, wie die nummerierten Rahmen im Handbuch.
            char nr[8];
            std::snprintf(nr, sizeof(nr), "%d", static_cast<int>(i + 1));
            const ImVec2 m{q.Min.x + fs * 0.6F, q.Max.y + fs * 0.65F};
            dl->AddCircleFilled(m, fs * 0.62F, kGelb);
            const ImVec2 g = ImGui::CalcTextSize(nr);
            dl->AddText(ImVec2{m.x - g.x * 0.5F, m.y - g.y * 0.5F}, IM_COL32(20, 20, 20, 255), nr);
        }
    }
}

// Ein Mauszeiger aus Linien - er klickt nichts, er zeigt nur.
void zeichneGeist(ImVec2 p, float fs) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = fs / 16.0F;
    const ImVec2 form[] = {{0, 0}, {0, 17}, {4.5F, 13}, {7.5F, 19.5F}, {10, 18.5F}, {7, 12}, {12.5F, 12}};
    ImVec2 punkte[7];
    for (int i = 0; i < 7; ++i) { punkte[i] = ImVec2{p.x + form[i].x * k * 1.4F, p.y + form[i].y * k * 1.4F}; }
    dl->AddConcavePolyFilled(punkte, 7, IM_COL32(255, 255, 255, 235));
    dl->AddPolyline(punkte, 7, IM_COL32(0, 0, 0, 255), ImDrawFlags_Closed, 1.5F);
}

// --- Das Fenster ------------------------------------------------------------------------------

void knopfHinweis(Str hinweis) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(hinweis)); }
}

void punkteZeichnen(const tour::Ablauf& a) {
    const float fs = ImGui::GetFontSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float r = fs * 0.22F;
    for (int i = 0; i < a.anzahl(); ++i) {
        const ImVec2 m{o.x + r + static_cast<float>(i) * r * 3.2F, o.y + fs * 0.5F};
        const tour::Ablauf::Stand st = a.stand(i);
        if (i == a.schritt() && !a.fertig()) {
            dl->AddCircle(m, r * 1.35F, kGelb, 0, 2.0F);
        }
        if (st == tour::Ablauf::Stand::Erledigt) {
            dl->AddCircleFilled(m, r, ImGui::GetColorU32(kGruen));
        } else if (st == tour::Ablauf::Stand::Uebersprungen) {
            dl->AddCircleFilled(m, r, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        } else {
            dl->AddCircle(m, r, ImGui::GetColorU32(ImGuiCol_TextDisabled), 0, 1.2F);
        }
    }
    ImGui::Dummy(ImVec2{static_cast<float>(std::max(1, a.anzahl())) * r * 3.2F, fs});
}

void starteVonListe(const std::string& id, int ab) {
    (void)tourStarten(id, ab);
}

// rc601 tour (Review): gemeldet "tourStarten sets g_app->settings.flussAnsicht
// = 0 permanently". Beim Schliessen, am Ende und bei "Alle Tutorials" kommt
// die vorige Ansicht zurueck - aber nur, wenn noch Classic steht: wer
// waehrend der Tour selbst umgeschaltet hat, behaelt seine Wahl.
void ansichtZurueck() {
    Zustand& s = z();
    if (s.ansichtVorher < 0 || g_app == nullptr) { return; }
    if (g_app->settings.flussAnsicht == 0) { g_app->settings.flussAnsicht = s.ansichtVorher; }
    s.ansichtVorher = -1;
}

// Die Schrittzahl, mit der tourStarten die Tour beginnen wuerde (Ansicht wie
// dort: die eingestellte, sonst Classic).
int schrittZahl(const tour::Tour& t) {
    const std::vector<tour::Schritt>* st = t.schritte(tour::ansichtName(g_app->settings.flussAnsicht));
    return st != nullptr ? static_cast<int>(st->size()) : 0;
}

void sucheZeichnen(float breite);

void listeZeichnen(float breite) {
    Zustand& s = z();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + breite);
    ImGui::TextUnformatted(tr(Str::TourListIntro));
    ImGui::PopTextWrapPos();
    if (!s.ladeFehler.empty()) {
        char t[700];
        std::snprintf(t, sizeof(t), tr(Str::TourMissing), s.ladeFehler.c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + breite);
        ImGui::TextColored(ImVec4{0.95F, 0.45F, 0.35F, 1.0F}, "%s", t);
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    // Knapp, damit die Liste auch bei grosser Schrift ganz zu sehen ist: je
    // Tutorial der Knopf und daneben der Titel; die Kurzbeschreibung steht im
    // Hinweis. (Ein Knopf unterhalb des Fensterrands bekaeme von ImGui keine
    // Beschriftung gemeldet - ButtonEx kehrt nach dem ItemAdd zurueck.)
    const float x0 = ImGui::GetCursorPosX();
    for (const tour::Tour& t : s.touren.liste) {
        ImGui::PushID(t.id.c_str());
        int n = 0;
        int anzahl = 0;
        bool fertig = false;
        const auto it = g_app->settings.tourStand.find(t.id);
        const bool hatStand = it != g_app->settings.tourStand.end() && tour::leseStand(it->second, n, anzahl, fertig);
        // "Start##<id>": sichtbar steht nur "Start", die Kennung macht jeden
        // Knopf eindeutig (auch fuer den Selbsttest).
        bool hinweis = false;
        // rc601 tour (Review): "Fortsetzen" nur, wenn es den Schritt noch
        // gibt - ein gemerkter Stand hinter dem Ende einer kuerzer gewordenen
        // Tour liess den Knopf vorher still nichts tun.
        if (hatStand && !fertig && n > 0 && n < schrittZahl(t)) {
            char c[96];
            std::snprintf(c, sizeof(c), tr(Str::TourContinue), n + 1);
            if (ImGui::Button((std::string(c) + "##" + t.id).c_str())) { starteVonListe(t.id, n); }
            hinweis = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
            ImGui::SameLine();
            if (ImGui::Button((std::string(tr(Str::TourRestart)) + "##" + t.id).c_str())) { starteVonListe(t.id, 0); }
            hinweis = hinweis || ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
        } else {
            if (ImGui::Button((std::string(tr(fertig ? Str::TourRestart : Str::TourStart)) + "##" + t.id).c_str())) {
                starteVonListe(t.id, 0);
            }
            hinweis = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
        }
        ImGui::SameLine();
        if (hatStand && fertig) {
            const ImVec2 q = ImGui::GetCursorScreenPos();
            uiIcon(ImGui::GetWindowDrawList(), UiIcon::Haken, ImVec2{std::floor(q.x), std::floor(q.y + ImGui::GetStyle().FramePadding.y)},
                   ImGui::GetFontSize(), ImGui::GetColorU32(kGruen));
            ImGui::Dummy(ImVec2{ImGui::GetFontSize(), ImGui::GetFrameHeight()});
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::TourDoneMark)); }
            ImGui::SameLine();
        }
        // Auf die Grundlinie der Knopfbeschriftung (zeichneSchrittText malt
        // selbst und kennt die Textverschiebung der Zeile nicht).
        const float rest = std::max(ImGui::GetFontSize() * 6.0F, breite - (ImGui::GetCursorPosX() - x0));
        zeichneSchrittText("**" + t.titel + "**", rest, ImGui::GetStyle().FramePadding.y);
        hinweis = hinweis || ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
        if (hinweis && !t.kurz.empty()) {
            ImGui::BeginTooltip();
            zeichneSchrittText(t.kurz, ImGui::GetFontSize() * 22.0F);
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    sucheZeichnen(breite);
    ImGui::Separator();
    if (ImGui::Button(tr(Str::TourClose))) { tourSchliessen(); }
}

void schrittZeichnen(float breite) {
    Zustand& s = z();
    tour::Ablauf& a = s.ablauf;
    const tour::Tour* t = a.tour();
    if (t == nullptr) { return; }
    zeichneSchrittText("**" + t->titel + "**", breite);
    if (a.fertig()) {
        const ImVec2 q = ImGui::GetCursorScreenPos();
        uiIcon(ImGui::GetWindowDrawList(), UiIcon::Haken, ImVec2{std::floor(q.x), std::floor(q.y)}, ImGui::GetFontSize(),
               ImGui::GetColorU32(kGruen));
        ImGui::Dummy(ImVec2{ImGui::GetFontSize(), ImGui::GetFontSize()});
        ImGui::SameLine();
        ImGui::TextColored(kGruen, "%s", tr(Str::TourFinished));
        punkteZeichnen(a);
        ImGui::Separator();
        if (ImGui::Button(tr(Str::TourAllList))) {
            s.ablauf.schliesse();
            s.laufendeId.clear();
            ansichtZurueck();   // rc601 tour (Review)
        }
        ImGui::SameLine();
        if (ImGui::Button(tr(Str::TourGuide))) { handbuchOeffnen(t->seite.c_str()); }
        ImGui::SameLine();
        if (ImGui::Button(tr(Str::TourClose))) { tourSchliessen(); }
        return;
    }
    ImGui::TextDisabled(tr(Str::TourStep), a.schritt() + 1, a.anzahl());
    punkteZeichnen(a);
    ImGui::Separator();
    const tour::Schritt* st = a.aktuell();
    if (st == nullptr) { return; }
    zeichneSchrittText(st->text, breite);
    if (s.zeigtOeffner) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + breite);
        ImGui::TextColored(kKennungFarbe, "%s", tr(Str::TourFirst));
        ImGui::PopTextWrapPos();
    } else if (s.zielErwartet && s.ziele.empty() && ImGui::GetTime() > s.pulsBis - 0.9) {
        // Erst nach einem Moment: im ersten Bild nach einem Schrittwechsel hat
        // das Register die neuen Beschriftungen noch nicht (sie werden fuer das
        // naechste Bild angemeldet).
        ImGui::TextDisabled("%s", tr(Str::TourNotVisible));
    }
    if (a.erfuellt()) {
        const ImVec2 q = ImGui::GetCursorScreenPos();
        uiIcon(ImGui::GetWindowDrawList(), UiIcon::Haken, ImVec2{std::floor(q.x), std::floor(q.y)}, ImGui::GetFontSize(),
               ImGui::GetColorU32(kGruen));
        ImGui::Dummy(ImVec2{ImGui::GetFontSize(), ImGui::GetFontSize()});
        ImGui::SameLine();
        ImGui::TextColored(kGruen, "%s", tr(Str::TourStepDone));
    }
    ImGui::Separator();
    ImGui::BeginDisabled(a.schritt() == 0);
    if (ImGui::Button(tr(Str::TourBack))) {
        a.zurueck();
        speichereStand();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool vonHand = st->fertig == "manuell" || a.erfuellt() || a.zurueckGeholt();
    if (vonHand) {
        if (ImGui::Button(tr(Str::TourNext))) {
            a.weiter();
            speichereStand();
        }
    } else {
        if (ImGui::Button(tr(Str::TourSkip))) {
            const std::string aktionen = a.ueberspringen();
            aktionenAusfuehren(aktionen);
            speichereStand();
        }
        knopfHinweis(Str::TourSkipHint);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::TourShowMe))) {
        s.geistStart = ImGui::GetTime();
        s.geistVon = ImGui::GetItemRectMin();
    }
    knopfHinweis(Str::TourShowMeHint);
    const std::string machEs = a.machEs();
    if (!machEs.empty() && !a.erfuellt()) {
        ImGui::SameLine();
        if (ImGui::Button(tr(Str::TourDoIt))) { aktionenAusfuehren(machEs); }
        knopfHinweis(Str::TourDoItHint);
    }
    if (ImGui::Button(tr(Str::TourGuide))) { handbuchOeffnen(t->seite.c_str()); }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::TourAllList))) {
        speichereStand();
        s.ablauf.schliesse();
        s.laufendeId.clear();
        ansichtZurueck();   // rc601 tour (Review)
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::TourClose))) { tourSchliessen(); }
}

// --- Suche im Handbuch (Phase 3) -------------------------------------------------------------

void sucheZeichnen(float breite) {
    Zustand& s = z();
    const std::string sp = sprachCode();
    if (!s.indexVersucht || s.indexSprache != sp) {
        s.indexVersucht = true;
        s.indexSprache = sp;
        s.index.clear();
        s.letzteAnfrage = "\x01";   // neu suchen
        const std::string ordner = s.ordner.empty() ? handbuchOrdner() : s.ordner;
        if (!ordner.empty()) {
            std::string f;
            std::string js = leseDatei(ordner + "\\" + sp + "\\suche.js");
            if (js.empty()) { js = leseDatei(ordner + "\\en\\suche.js"); }
            if (!tour::ladeSuchindex(js, s.index, &f)) { diag::detail("Tour: Suchindex - " + f); }
        }
    }
    ImGui::SetNextItemWidth(breite);
    ImGui::InputTextWithHint("##toursuche", tr(Str::TourSearchHint), s.suche, sizeof(s.suche));
    if (s.letzteAnfrage != s.suche) {
        s.letzteAnfrage = s.suche;
        s.treffer = tour::suche(s.index, s.suche, 5);
    }
    if (s.suche[0] == '\0') { return; }
    if (s.treffer.empty()) {
        ImGui::TextDisabled("%s", tr(Str::TourSearchNone));
        return;
    }
    for (const tour::Treffer& h : s.treffer) {
        const tour::Abschnitt& ab = s.index[h.abschnitt];
        const std::string id = ab.seite.size() > 5 ? ab.seite.substr(0, ab.seite.size() - 5) : ab.seite;
        const std::string titel = ab.titel + (ab.ueber.empty() || ab.ueber == ab.titel ? "" : " - " + ab.ueber);
        ImGui::PushID(static_cast<int>(h.abschnitt));
        if (s.touren.finde(id) != nullptr) {
            // Eine Seite mit Tour: gleich starten.
            if (ImGui::Button(tr(Str::TourStart))) { starteVonListe(id, 0); }
            ImGui::SameLine();
        }
        if (ImGui::Button(tr(Str::TourSearchGuide))) {
            const std::string ziel = ab.seite + (ab.anker.empty() ? "" : "#" + ab.anker);
            handbuchOeffnen(ziel.c_str());
        }
        ImGui::SameLine();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + breite * 0.7F);
        ImGui::TextUnformatted(titel.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopID();
    }
}

// Das Fenster so stellen, dass es kein Ziel verdeckt: eine der vier Ecken
// der Arbeitsflaeche, zuerst unten rechts.
void fensterStellen(const std::vector<ImRect>& ziele, float breite) {
    Zustand& s = z();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float fs = ImGui::GetFontSize();
    ImGuiWindow* w = ImGui::FindWindowByName("###tour");
    const ImVec2 groesse = (w != nullptr && w->Size.x > 0.0F) ? w->Size : ImVec2{breite, fs * 16.0F};
    const ImRect jetzt = w != nullptr ? w->Rect() : ImRect{};
    bool verdeckt = false;
    for (const ImRect& r : ziele) {
        if (jetzt.Overlaps(r)) { verdeckt = true; }
    }
    // Ragt es aus der Arbeitsflaeche (es ist gewachsen, oder die erste Groesse
    // war geschaetzt), neu stellen.
    const bool ragtHeraus = w != nullptr && (jetzt.Max.y > vp->WorkPos.y + vp->WorkSize.y + 1.0F ||
                                             jetzt.Max.x > vp->WorkPos.x + vp->WorkSize.x + 1.0F ||
                                             jetzt.Min.y < vp->WorkPos.y - 1.0F || jetzt.Min.x < vp->WorkPos.x - 1.0F);
    if (!s.fensterNeuSetzen && !verdeckt && !ragtHeraus) { return; }
    const float rand = fs;
    const float unten = std::max(vp->WorkPos.y, vp->WorkPos.y + vp->WorkSize.y - groesse.y - fs * 2.6F);
    const float oben = std::min(unten, vp->WorkPos.y + fs * 3.5F);
    const float links = vp->WorkPos.x + rand;
    const float rechts = vp->WorkPos.x + vp->WorkSize.x - groesse.x - rand;
    const ImVec2 ecken[] = {{rechts, unten}, {links, unten}, {rechts, oben}, {links, oben}};
    ImVec2 wahl = ecken[0];
    for (const ImVec2& e : ecken) {
        const ImRect kandidat{e, ImVec2{e.x + groesse.x, e.y + groesse.y}};
        bool frei = true;
        for (const ImRect& r : ziele) {
            if (kandidat.Overlaps(r)) { frei = false; }
        }
        if (frei) {
            wahl = e;
            break;
        }
    }
    ImGui::SetNextWindowPos(wahl, ImGuiCond_Always);
    s.fensterNeuSetzen = false;
}

// --- rc601 tour (Review): Fenster, auch aus einem Modal heraus ----------------------------------
//
// Gemeldet: "k-werte-bearbeiten cannot be advanced while the Event editor
// (BeginPopupModal) is open: the tour panel is begun outside the modal's
// begin stack, so ImGui blocks hovering/clicking it". ImGui laesst bei
// offenem Modal die Maus nur an Fenster in dessen Begin-Stapel
// (imgui.cpp:5689, IsWindowWithinBeginStackOf); ein Klick dort schliesst das
// Modal auch nicht (imgui.cpp:13222). Deshalb zeichnet drawEditor das
// Fenster ueber tourImModal() vor seinem EndPopup, und tourBild laesst es in
// diesem Bild aus.
//
// `modal`: das Modal, in dessen Begin-Stapel gezeichnet wird (oder nullptr).
// Gibt false zurueck, wenn das Fenster geschlossen wurde.
bool fensterZeichnen(ImGuiWindow* modal) {
    Zustand& s = z();
    const float fs = ImGui::GetFontSize();
    const float breite = fs * 25.0F;
    fensterStellen(s.ziele, breite);
    // Feste Breite; hoechstens 85 % der Hoehe (darueber rollt das Fenster).
    ImGui::SetNextWindowSizeConstraints(
        ImVec2{breite + ImGui::GetStyle().WindowPadding.x * 2.0F, 0.0F},
        ImVec2{breite + ImGui::GetStyle().WindowPadding.x * 2.0F, ImGui::GetMainViewport()->WorkSize.y * 0.85F});
    bool offen = true;
    s.hoverKennung.clear();
    const std::string titel = std::string(tr(Str::TourWindowTitle)) + "###tour";
    if (ImGui::Begin(titel.c_str(), &offen,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing)) {
        if (modal != nullptr) {
            // Direkt UEBER das Modal stellen (nicht ganz nach vorn - eine
            // offene Auswahlliste des Editors bleibt obenauf). Ohne das lag
            // es dahinter, abgedunkelt und von ihm verdeckt.
            ImGuiWindow* w = ImGui::GetCurrentWindow();
            const int iw = ImGui::FindWindowDisplayIndex(w);
            const int im = ImGui::FindWindowDisplayIndex(modal);
            if (iw >= 0 && im >= 0 && iw < im) {
                ImVector<ImGuiWindow*>& ws = GImGui->Windows;
                ws.erase(ws.Data + iw);
                ws.insert(ws.Data + im, w);   // das Modal steht jetzt bei im - 1
            }
        }
        if (s.ablauf.aktiv()) {
            schrittZeichnen(breite);
        } else {
            listeZeichnen(breite);
        }
        s.fensterRect = ImGui::GetCurrentWindow()->Rect();
    }
    ImGui::End();
    if (!offen) {
        tourSchliessen();
        return false;
    }
    return true;
}

// rc601 tour (Review): gemeldet "the spotlight dims tooltips, context menus,
// open menus and unrelated modals". Die Rechtecke der offenen Klappfenster
// (Menues, Kontextmenues, Auswahllisten) und Hinweise in diesem Bild - sie
// bleiben hell.
std::vector<ImRect> offeneKlappfenster() {
    std::vector<ImRect> out;
    for (ImGuiWindow* w : GImGui->Windows) {
        if (!w->Active || w->Hidden || (w->Flags & ImGuiWindowFlags_Modal) != 0) { continue; }
        if ((w->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) == 0) { continue; }
        out.push_back(w->Rect());
    }
    return out;
}

}  // namespace

// --- Oeffentlich --------------------------------------------------------------------------------

void tourImModal() {
    Zustand& s = z();
    if (g_app == nullptr || (!s.fensterOffen && !s.ablauf.aktiv())) { return; }
    if (g_app->askSaveOpen || g_app->frageOffen) { return; }
    ImGuiWindow* modal = ImGui::GetCurrentWindow();
    if (modal == nullptr || (modal->Flags & ImGuiWindowFlags_Modal) == 0) { return; }
    laden();
    s.fensterImModalBild = ImGui::GetFrameCount();
    (void)fensterZeichnen(modal);
}

void tourFensterOeffnen() {
    Zustand& s = z();
    s.fensterOffen = true;
    s.fensterNeuSetzen = true;
    laden();
}

bool tourStarten(const std::string& id, int ab) {
    Zustand& s = z();
    laden();
    const tour::Tour* t = s.touren.finde(id);
    if (t == nullptr || g_app == nullptr) {
        char m[700];
        std::snprintf(m, sizeof(m), tr(Str::TourMissing), (id + (s.ladeFehler.empty() ? "" : ", " + s.ladeFehler)).c_str());
        addStatus(m);
        return false;
    }
    // Hat die Tour die eingestellte Skriptansicht nicht, gilt Classic - und
    // dann muss auch der Baum zu sehen sein (die Ziele liegen dort).
    std::string ansicht = tour::ansichtName(g_app->settings.flussAnsicht);
    bool gibtAnsicht = false;
    for (const auto& [name, schritte] : t->ansichten) {
        if (name == ansicht) { gibtAnsicht = true; }
    }
    if (!gibtAnsicht) { ansicht = "classic"; }
    // rc601 tour (Review): ein Start hinter dem Ende (gemerkter Stand einer
    // kuerzer gewordenen Tour) scheiterte still - jetzt mit Meldung, und erst
    // danach wird die Ansicht umgestellt.
    if (!s.ablauf.starte(*t, ansicht, ab)) {
        const std::vector<tour::Schritt>* st = t->schritte(ansicht);
        char m[400];
        std::snprintf(m, sizeof(m), tr(Str::TourStartFailed), id.c_str(), ab + 1,
                      st != nullptr ? static_cast<int>(st->size()) : 0);
        addStatus(m);
        diag::detail(std::string("Tour: ") + m);
        return false;
    }
    if (!gibtAnsicht && g_app->settings.flussAnsicht != 0) {
        // Die vorige Ansicht merken (eine schon laufende Tour hat sie
        // vielleicht schon gemerkt - dann gilt die).
        if (s.ansichtVorher < 0) { s.ansichtVorher = g_app->settings.flussAnsicht; }
        g_app->settings.flussAnsicht = 0;
        addStatus(tr(Str::TourViewSwitched));
    }
    s.laufendeId = id;
    s.fensterOffen = true;
    s.fensterNeuSetzen = true;
    s.letzterSchritt = -1;
    s.warFertig = false;
    diag::detail("Tour: Start " + id + " ab Schritt " + std::to_string(ab + 1) + " (" + ansicht + ")");
    speichereStand();
    return true;
}

void tourSchliessen() {
    Zustand& s = z();
    speichereStand();
    if (s.ablauf.aktiv()) { diag::detail("Tour: geschlossen bei Schritt " + std::to_string(s.ablauf.schritt() + 1)); }
    s.ablauf.schliesse();
    s.laufendeId.clear();
    ansichtZurueck();   // rc601 tour (Review)
    s.fensterOffen = false;
    s.ziele.clear();
    s.overlay = false;
    s.geistStart = -1.0;
}

bool tourAnimiert() {
    const Zustand& s = z();
    const double jetzt = ImGui::GetTime();
    return s.ablauf.aktiv() && (jetzt < s.pulsBis || s.geistStart >= 0.0 || s.ablauf.erfuellt());
}

TourSicht tourSicht() {
    const Zustand& s = z();
    TourSicht v;
    v.fensterOffen = s.fensterOffen;
    v.aktiv = s.ablauf.aktiv();
    v.fertig = s.ablauf.fertig();
    v.id = s.laufendeId;
    v.schritt = s.ablauf.aktiv() ? s.ablauf.schritt() : -1;
    v.anzahl = s.ablauf.anzahl();
    v.erfuellt = s.ablauf.erfuellt();
    v.overlay = s.overlay;
    v.loecher = s.loecher;
    v.ziele = s.ziele;
    v.zeigtOeffner = s.zeigtOeffner;
    v.geist = s.geistStart >= 0.0;
    v.angemeldet = s.angemeldet;
    v.touren = static_cast<int>(s.touren.liste.size());
    v.fehler = s.ladeFehler;
    v.ordner = s.ordner;
    return v;
}

void tourBild() {
    Zustand& s = z();
    s.overlay = false;
    s.loecher.clear();
    s.angemeldet = false;
    if (g_app == nullptr || (!s.fensterOffen && !s.ablauf.aktiv())) { return; }
    laden();
    const double jetzt = ImGui::GetTime();
    const float fs = ImGui::GetFontSize();
    // Eine Frage (Speichern? Ja/Nein) hat Vorrang - solange sie steht, ruht
    // die Tour (app.cpp: "eine Frage zur Zeit").
    const bool frage = g_app->askSaveOpen || g_app->frageOffen;
    std::vector<std::string> labels;
    s.ziele.clear();
    s.zielKennung = 0;
    s.zielInnen.clear();
    s.zeigtOeffner = false;
    s.zielErwartet = false;
    s.zielGanzSichtbar = true;

    if (s.ablauf.aktiv() && !s.ablauf.fertig()) {
        const tour::Schritt* st = s.ablauf.aktuell();
        tour::Kontext k = kontextBauen();
        std::vector<tour::Ziel> ziele;
        std::string f;
        (void)tour::parseZiele(st->ziel, ziele, &f);
        s.zielErwartet = !ziele.empty();
        std::vector<tour::Ziel> oeffner;
        if (!st->voraus.empty()) {
            const tour::Voraus v = tour::zerlegeVoraus(st->voraus);
            (void)tour::parseZiele(v.oeffner, oeffner, &f);
            if (!tour::erfuellt(v.pruefung, k, k)) {
                s.zeigtOeffner = !oeffner.empty();
            }
        }
        wuensche(ziele, labels);
        wuensche(oeffner, labels);
        for (const tour::Ziel& zl : (s.zeigtOeffner ? oeffner : ziele)) {
            const Gefunden g = loese(zl);
            if (!g.da) { continue; }
            if (s.ziele.empty() && g.e != nullptr) {
                s.zielKennung = g.e->id;
                s.zielInnen = g.e->innen;
                s.zielGanzSichtbar = elemente::ganzImBild(*g.e);
            }
            s.ziele.push_back(g.r);
        }
        // Ein Ziel in einer gerollten Liste (die Ereignisliste ist lang): einmal
        // je Schritt dorthin rollen.
        if (!s.ziele.empty() && !s.zielGanzSichtbar && !s.gerollt && !s.zielInnen.empty()) {
            if (ImGuiWindow* w = ImGui::FindWindowByName(s.zielInnen.c_str()); w != nullptr && w->ScrollMax.y > 0.0F) {
                const float y = w->Scroll.y + (s.ziele[0].GetCenter().y - w->InnerRect.GetCenter().y);
                ImGui::SetScrollY(w, std::clamp(y, 0.0F, w->ScrollMax.y));
            }
            s.gerollt = true;
        }
        // Schwebt die Maus auf dem (ersten) Ziel? Und wurde es angeklickt?
        bool ueber = false;
        if (!s.ziele.empty()) {
            const ImGuiWindow* hw = GImGui->HoveredWindow;
            const bool ueberFenster = hw != nullptr && hw->RootWindow != nullptr && std::strstr(hw->RootWindow->Name, "###tour") != nullptr;
            ueber = !ueberFenster && ImGui::IsMouseHoveringRect(s.ziele[0].Min, s.ziele[0].Max, false);
        }
        if (ueber) {
            if (s.schwebtSeit < 0.0) { s.schwebtSeit = jetzt; }
        } else {
            s.schwebtSeit = -1.0;
        }
        k.schwebt = s.schwebtSeit >= 0.0 && jetzt - s.schwebtSeit >= 0.4;
        const ImGuiContext& g = *GImGui;
        k.geklickt = s.zielKennung != 0 && g.DeactivatedItemData.ID == s.zielKennung && g.DeactivatedItemData.ElapseFrame >= g.FrameCount;
        if (!frage && s.ablauf.bild(k, jetzt)) {
            diag::detail("Tour: Schritt " + std::to_string(s.ablauf.schritt() + 1) + " erledigt");
        }
        for (const std::string& t : tour::kennungenImText(st->text)) { labels.push_back(kennungText(t)); }
    }
    // Schrittwechsel: Rahmen pulsieren lassen, Fortschritt merken.
    const int schrittJetzt = s.ablauf.aktiv() ? s.ablauf.schritt() : -1;
    if (schrittJetzt != s.letzterSchritt || (s.ablauf.fertig() && !s.warFertig)) {
        s.letzterSchritt = schrittJetzt;
        s.warFertig = s.ablauf.fertig();
        s.pulsBis = jetzt + 1.2;
        s.schwebtSeit = -1.0;
        s.gerollt = false;
        s.geistStart = -1.0;
        s.fensterNeuSetzen = true;
        speichereStand();
        if (s.ablauf.fertig()) { ansichtZurueck(); }   // rc601 tour (Review): Tour durch
    }
    if (!labels.empty()) {
        elemente::wunschLabels(labels);
        s.angemeldet = true;
    }
    if (frage) { return; }

    // --- Fenster -------------------------------------------------------------------------
    // (rc601 tour, Review: schon im Modal gezeichnet? Dann nicht noch einmal.)
    if (s.fensterImModalBild != ImGui::GetFrameCount() && !fensterZeichnen(nullptr)) { return; }

    // --- Hervorhebung ---------------------------------------------------------------------
    // rc601 tour (Review): steht ein Modal, in dem kein Ziel liegt (About,
    // Einstellungen ...), ruht die Abdunklung - sie laege sonst ueber einem
    // Fenster, das mit der Tour nichts zu tun hat.
    bool fremdesModal = false;
    if (const ImGuiWindow* m = ImGui::GetTopMostAndVisiblePopupModal(); m != nullptr) {
        fremdesModal = true;
        for (const ImRect& r : s.ziele) {
            if (m->Rect().Contains(r.GetCenter())) { fremdesModal = false; }
        }
    }
    if (s.ablauf.aktiv() && !s.ablauf.fertig() && !s.ziele.empty() && !fremdesModal) {
        const float puls = jetzt < s.pulsBis
                               ? static_cast<float>(0.5 + 0.5 * std::sin((s.pulsBis - jetzt) * 9.0))
                               : 0.0F;
        zeichneHervorhebung(s.ziele, s.fensterRect, puls, offeneKlappfenster());
        s.overlay = true;
    }
    // Eine {{Kennung}} im Text unter der Maus: ihr Knopf bekommt einen
    // duennen Rahmen.
    if (!s.hoverKennung.empty()) {
        if (const elemente::Element* e = elemente::finde(kennungText(s.hoverKennung), "", 0, false)) {
            ImRect r = e->rect;
            r.Expand(fs * 0.2F);
            ImGui::GetForegroundDrawList()->AddRect(r.Min, r.Max, kGelb, fs * 0.3F, 0, 1.5F);
        }
    }
    // Zeig es mir: in einer Sekunde vom Knopf zum Ziel, dann zweimal ein Ring.
    if (s.geistStart >= 0.0) {
        const double t = jetzt - s.geistStart;
        if (t > 2.4 || s.ziele.empty()) {
            s.geistStart = -1.0;
        } else {
            const ImVec2 nach = s.ziele[0].GetCenter();
            const float a = static_cast<float>(std::min(1.0, t / 1.0));
            const float e = a * a * (3.0F - 2.0F * a);
            const ImVec2 p{s.geistVon.x + (nach.x - s.geistVon.x) * e, s.geistVon.y + (nach.y - s.geistVon.y) * e};
            if (t > 1.0) {
                const float r = static_cast<float>(std::fmod(t - 1.0, 0.7) / 0.7);
                ImGui::GetForegroundDrawList()->AddCircle(nach, fs * (0.4F + 1.6F * r),
                                                         IM_COL32(255, 214, 10, static_cast<int>(255.0F * (1.0F - r))), 0, 3.0F);
            }
            zeichneGeist(p, fs);
        }
    }
}

}  // namespace bhed::gui
