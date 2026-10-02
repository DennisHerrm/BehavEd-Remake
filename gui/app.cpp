// app.cpp - die Oberflaeche, mit Dear ImGui 1.92.
//
// Die Aufteilung folgt Dialog 102 aus BehavEd.exe. Die Zahlen unten sind
// Dialogeinheiten aus der PE-Ressource, nicht geschaetzt:
//   Fenster 663 x 449, Events 7..198, Script Flow 206..578, Knopfspalte 585
// Umgerechnet wird ueber die Basiseinheiten von MS Sans Serif 8pt (6/13), und
// zusaetzlich ueber die Bildschirmskalierung - bei 144 dpi also 1,5.
//
// Der Event-Editor wird zur Laufzeit aus der Parameterangabe gebaut, so wie
// im Original. Rasterzahlen am Bildschirmfoto gemessen (11.08.2026):
//   Feld 167 x 14 dlu, angehaengter Knopf ~33, Abstand 9, erstes Feld bei 100,
//   Knopfzeile 88 unter der Feldzeile.
//
// Kein Text steht hier fest verdrahtet - alles geht durch i18n::tr(). Der
// Pruefer tools/lint_i18n.py schlaegt an, wenn doch.

#include <filesystem>
#include <chrono>
#include <ctime>
#include <fstream>
#include "bhed/fassung.h"
#include "update.h"
#include "app_internal.h"
#include "gpumap.h"

#include <atomic>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>
#include <sstream>
#include <thread>
#include <string>
#include <vector>

namespace bhed::gui {

// Der eine Zustand. Deklariert in app_internal.h, damit app_view3d.cpp ihn
// ebenfalls sieht.
App* g_app = nullptr;

float dx(float v) { return v * kDluX * g_app->uiScale; }
float dy(float v) { return v * kDluY * g_app->uiScale; }




namespace {



// Vorwaertsangaben: die Dateiverwaltung steht weiter unten, wird aber schon
// von den Tastenkuerzeln gebraucht.
void doOpen();
void doNew();
bool doSave();
bool doSaveAs();
// Ausgewaehlt schlaegt Ueberfahren.
//
// ImGui entscheidet andersherum, imgui_widgets.cpp, Selectable():
//
//     ImU32 col = GetColorU32((held && highlighted) ? ImGuiCol_HeaderActive
//                             : highlighted ? ImGuiCol_HeaderHovered
//                                           : ImGuiCol_Header);
//
// Die Maus gewinnt also immer. Bei getrennten Farben - grau fuer
// ueberfahren, blau fuer gewaehlt - heisst das: faehrt man ueber die
// gewaehlte Zeile, wird sie grau, und man sieht nicht mehr, was markiert
// ist. Fuer gewaehlte Zeilen ziehen wir die Hoverfarbe deshalb auf die
// Auswahlfarbe.
struct KeepSelected {
    bool pushed = false;
    // aktiv == false heisst: diese Liste hat die Auswahl nicht zuletzt
    // bekommen. Dann wird gedaempft gezeichnet, so wie Windows es mit einer
    // Liste ohne Fokus macht - man sieht noch, was gewaehlt war, aber es
    // draengt sich nicht mehr auf.
    explicit KeepSelected(bool selected, bool aktiv = true) : pushed(selected) {
        if (!pushed) {
            return;
        }
        ImVec4 h = ImGui::GetStyleColorVec4(ImGuiCol_Header);
        if (!aktiv) {
            const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
            // Zur Hintergrundfarbe hin verblassen.
            h.x = (h.x + bg.x) * 0.5F;
            h.y = (h.y + bg.y) * 0.5F;
            h.z = (h.z + bg.z) * 0.5F;
            ImGui::PushStyleColor(ImGuiCol_Header, h);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, h);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, h);
            pushed3_ = true;
            return;
        }
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, h);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, h);
    }
    ~KeepSelected() {
        if (pushed) {
            ImGui::PopStyleColor(pushed3_ ? 3 : 2);
        }
    }
    bool pushed3_ = false;

    KeepSelected(const KeepSelected&) = delete;
    KeepSelected& operator=(const KeepSelected&) = delete;
    KeepSelected(KeepSelected&&) = delete;
    KeepSelected& operator=(KeepSelected&&) = delete;
};

void insertCommand(const Command& c);
void moveSelection(bool up);
void insertMacro(const Macro& m);

// --- Aus der Ereignisliste in den Baum ziehen ---------------------------
//
// Die Nutzlast traegt nur, WAS gezogen wird - nicht wohin. Das Ziel weiss
// der Baum selbst, aus der Stelle, an der die Maus losgelassen wird.
//
// Eine Zahl statt eines Zeigers: die Nutzlast wird von ImGui kopiert und
// ueberlebt das Bild, in dem sie gesetzt wurde. Ein Zeiger in
// g_app->db.commands waere gueltig, aber die Nummer ist ehrlicher.
struct NewDrag {
    int kind = 0;    // 0 = Befehl, 1 = Makro
    int index = 0;   // Nummer in db.commands bzw. db.macroBodies
};

// Wohin, bezogen auf die Zielzeile.
enum class DropWhere { Before, Into, After };

// Wohin faellt, was ueber dieser Zeile losgelassen wird? Drei Zonen, fuer
// BEIDE Arten zu ziehen (neu aus der Ereignisliste und Umordnen im Baum):
// oberes Drittel davor, Mitte hinein (nur bei einem Block), unteres Drittel
// dahinter. shank: "kannst du dieselbe Logik auch machen ... wenn man im
// Script Flow selbst rumschiebt? entweder darunter, darueber oder darauf".
//
// Unter einem AUFGEKLAPPTEN Block mit Inhalt steht die Linie genau ueber
// seinem ersten Kind - dort heisst es deshalb auch "hinein, an den Anfang".
// Hinter den ganzen Block kommt man ueber die Zeile nach dem Block (oben).
struct Ablage {
    DropWhere wo = DropWhere::After;
    bool alsLinie = true;    // Linie (dazwischen) statt Rahmen (darauf)
    bool linieOben = false;
};
Ablage ablageZone(float anteil, bool block, bool aufMitInhalt) {
    Ablage z;
    if (anteil < 0.3F) {
        z.wo = DropWhere::Before;
        z.linieOben = true;
    } else if (block && anteil <= 0.7F) {
        z.wo = DropWhere::Into;
        z.alsLinie = false;
    } else if (block && aufMitInhalt) {
        z.wo = DropWhere::Into;   // Linie unter der Zeile = vor dem ersten Kind
    } else {
        z.wo = DropWhere::After;
    }
    return z;
}
void zeichneAblage(const Ablage& z, ImVec2 a, ImVec2 b) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 mark = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
    if (!z.alsLinie) {
        dl->AddRect(a, b, mark, 0.0F, 0, 2.0F);
    } else {
        const float y = z.linieOben ? a.y : b.y;
        dl->AddLine(ImVec2{a.x, y}, ImVec2{b.x, y}, mark, 2.0F);
    }
}

void dropNewAt(const NewDrag& what, const Path& target, DropWhere where);
void moveSelectionByKey(int delta);
void handleTreeKeys();
// Die Symboltexturen stehen weiter unten, werden aber schon vom
// Ueber-Dialog gebraucht.
void* logoTextureId();
extern const char* kScriptFilter;
void loadPath(const std::string& p);
std::string descFor(const Command& c);
void doSaveAll();
ImGuiKeyChord chordFor(keys::Action a);
void doBackup();
void doRestore();
void springeZuTreffer();
void findNext(int step);
void doUndo();
void clearSelection();
void doRedo();
const char* stepName(const char* key);
void addGamePath();
void applyTheme(const theme::Theme& t);

float dx(float v) { return v * kDluX * g_app->uiScale; }
float dy(float v) { return v * kDluY * g_app->uiScale; }

ImVec4 col(const theme::Color& c) { return ImVec4{c.r, c.g, c.b, c.a}; }

// Massstab anwenden - nach der Anleitung in ImGuis FAQ.
//
// Zwei Regeln, die ich zuerst beide verletzt hatte:
//
//  1. Die Schrift wird NICHT in der Zielgroesse gebacken. Seit 1.92 rastert
//     ImGui dynamisch: Grundgroesse in style.FontSizeBase, Massstab in
//     style.FontScaleDpi. Wer beides multipliziert - Schrift mit 16*1,5
//     backen UND FontScaleDpi=1,5 setzen - bekommt 36 Punkt statt 24.
//  2. style.ScaleAllSizes() ist ausdruecklich "call once!". Es multipliziert
//     die Abstaende jedes Mal weiter. Wer den Massstab aendern will, muss
//     laut FAQ "reset the style and call this again". Ohne das bleiben die
//     Polster klein, waehrend die Schrift waechst - dann sieht es gequetscht
//     aus, genau wie gemeldet.
void applyScale(float total) {
    // Vom Urzustand aus, nicht vom bereits skalierten.
    ImGui::GetStyle() = ImGuiStyle{};
    if (g_app->activeTheme != nullptr) {
        applyTheme(*g_app->activeTheme);
    }
    ImGuiStyle& s = ImGui::GetStyle();
    s.ScaleAllSizes(total);
    s.FontSizeBase = 16.0F;

    // ImGui trennt die beiden Massstaebe, und das tun wir jetzt auch:
    //
    //   FontScaleDpi   der Bildschirm - kommt von Windows
    //   FontScaleMain  die Einstellung des Benutzers
    //
    // In imgui.h steht zu FontScaleMain ausdruecklich "may be set by
    // application once, or exposed to end-user" - genau dafuer ist er da.
    // Vorher stand das Produkt in FontScaleDpi; das ergab dieselbe Groesse,
    // vermischte aber zwei Dinge, die verschiedene Ursachen haben.
    s.FontScaleDpi = g_app->dpiScale;
    s.FontScaleMain = g_app->uiScale;
    g_app->appliedScale = total;
}

void applyTheme(const theme::Theme& t) {
    ImGuiStyle& s = ImGui::GetStyle();
    s.Colors[ImGuiCol_WindowBg] = col(t.window);
    s.Colors[ImGuiCol_ChildBg] = col(t.child);
    s.Colors[ImGuiCol_PopupBg] = col(t.window);
    s.Colors[ImGuiCol_Text] = col(t.text);
    s.Colors[ImGuiCol_TextDisabled] = col(t.textDisabled);
    s.Colors[ImGuiCol_Button] = col(t.button);
    s.Colors[ImGuiCol_ButtonHovered] = col(t.buttonHovered);
    s.Colors[ImGuiCol_ButtonActive] = col(t.buttonActive);
    s.Colors[ImGuiCol_FrameBg] = col(t.frame);
    s.Colors[ImGuiCol_FrameBgHovered] = col(t.frameHovered);
    s.Colors[ImGuiCol_FrameBgActive] = col(t.frameHovered);
    s.Colors[ImGuiCol_Border] = col(t.border);
    s.Colors[ImGuiCol_Header] = col(t.header);
    // Ueberfahren ist NICHT dasselbe wie ausgewaehlt.
    //
    // Hier stand dreimal dieselbe Farbe. Damit sah jede Zeile, ueber der
    // gerade die Maus stand, aus wie die gewaehlte - und in einer langen
    // Liste weiss man dann nicht mehr, was wirklich markiert ist.
    //
    // Ueberfahren bekommt jetzt das neutrale Grau der Knopfleiste, gewaehlt
    // behaelt die Betonungsfarbe. Gedrueckt liegt dazwischen.
    s.Colors[ImGuiCol_HeaderHovered] = col(t.buttonHovered);
    s.Colors[ImGuiCol_HeaderActive] = col(t.buttonActive);
    s.Colors[ImGuiCol_CheckMark] = col(t.accent);
    s.Colors[ImGuiCol_TitleBgActive] = col(t.accent);
    s.Colors[ImGuiCol_PlotHistogram] = col(t.accent);
    // Ohne diese blieb die Menueleiste in ImGuis Vorgabefarbe - im hellen
    // Aussehen ein dunkler Balken quer ueber dem Fenster.
    s.Colors[ImGuiCol_MenuBarBg] = col(t.window);
    s.Colors[ImGuiCol_TitleBg] = col(t.window);
    s.Colors[ImGuiCol_TitleBgCollapsed] = col(t.window);
    s.Colors[ImGuiCol_Separator] = col(t.border);
    s.Colors[ImGuiCol_SeparatorHovered] = col(t.accent);
    s.Colors[ImGuiCol_SeparatorActive] = col(t.accent);
    s.Colors[ImGuiCol_ScrollbarBg] = col(t.window);
    s.Colors[ImGuiCol_ScrollbarGrab] = col(t.button);
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = col(t.buttonHovered);
    s.Colors[ImGuiCol_ScrollbarGrabActive] = col(t.buttonActive);
    s.Colors[ImGuiCol_SliderGrab] = col(t.accent);
    s.Colors[ImGuiCol_SliderGrabActive] = col(t.accent);
    s.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4{0.0F, 0.0F, 0.0F, 0.35F};
    // Bei der klassischen Farbgebung steht die Auswahl auf dunkelblau -
    // dort muss der Text weiss sein, sonst ist er unlesbar.
    s.Colors[ImGuiCol_NavCursor] = col(t.accent);

    // Windows 95 hatte harte Ecken; alles andere darf gerundet sein.
    const float r = t.raisedBorders ? 0.0F : 3.0F;
    s.FrameRounding = r;
    s.WindowRounding = r;
    s.ChildRounding = r;
    s.PopupRounding = r;
    s.GrabRounding = r;
    s.FrameBorderSize = 1.0F;
    s.WindowBorderSize = 1.0F;
    s.ChildBorderSize = 1.0F;
}

// Ein Knopf in der rechten Spalte: volle Breite der Gruppe.


// --- Event-Editor ------------------------------------------------------

// Fuer einen VORHANDENEN Knoten: seine Werte, nicht die Vorgaben.

// `editorFields` ist seit rc468 in `src/edit.cpp`.
//
// Sie entscheidet, WAS im Fenster steht - und lag in der Oberflaeche, wo
// sie `g_app` las. Damit war die eine Funktion, die die Fenster aufbaut,
// die einzige, die keine Probe erreichen konnte.
//
// Jetzt nimmt sie die Werte als Argument. `tests/editorall.cpp` baut damit
// die Felder jedes Befehls fuer JEDEN Eintrag jeder Klappliste auf.


const char* hintFor(const Param& p) {
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

// Eine Klappliste aus einer Typmenge.
// `comboEdit` ist bereits in `app_internal.h` deklariert (Zeile 1514) - eine
// zweite Ankuendigung hier waere die zweite Fassung derselben Signatur.
// Eine Klappliste ueber einen Typsatz.
//
// Sie zeichnete frueher ein blankes `BeginCombo` - ohne Filterzeile. Das
// Feld oben im Ereignisfenster hatte eine, die Helferzeilen darunter nicht:
// shank sah dieselbe Liste "Parm strings" einmal mit Suchfeld und einmal
// ohne.
//
// Zwei Fassungen derselben Bedienung. Jetzt stuetzt sich diese hier auf
// `comboEdit`, und damit haben ALLE Typklapplisten den Filter - auch die
// vier Aufrufe in `drawHelperRows` und jeder, der spaeter dazukommt.
bool typeSetCombo(const char* id, const char* set, std::string* value,
                  float width) {
    const TypeSet* ts = g_app->db.typeset(set);
    const ComboEditResult r =
        comboEdit(id, *value, (ts != nullptr) ? &ts->entries : nullptr, width);
    return r.edited || r.picked;
}

// Die Helferzeilen unter einem Feld - Bilder 4 und 6 des Originals.
//
// Sie bauen die drei Ausdruecke, die ICARUS kennt. Die Signatur von get()
// steht in der behaved.bhc selbst, in einer auskommentierten Zeile:
//     //get( %t="DECLARE_TYPE", %s="variablename" );
//     //# OF NO USE BY ITSELF ... but is still usable inside other commands
//
// Abweichung vom Original, bewusst: dort hat die Tag-Zeile nur eine
// Klappliste, der Name kommt von woanders. Hier steht ein eigenes Feld
// dafuer - sonst kann man tag() ohne Umweg ueber die Tastatur nicht bauen.
// Sorgt dafuer, dass alle Reihen des Helfers so viele Eintraege haben wie
// es Felder gibt. EINE Stelle - sonst waeren es sechs Bedingungen, die
// auseinanderlaufen koennen.
void helferGross(std::size_t n) {
    // NUR WACHSEN. `resize` schrumpft auch, und genau daran ist die
    // Bedienung gescheitert:
    //
    // Aufgerufen wird sie mit `i + 1` - fuer Spalte 0 also mit 1. Ein
    // `resize(1)` auf eine Reihe mit drei Eintraegen wirft die hinteren
    // beiden WEG. In jedem Bild.
    //
    // Was man in Spalte 2 oder 3 auswaehlte, war beim naechsten Bild
    // wieder die Vorgabe: shank sah die Klappliste aufgehen, konnte sie
    // bedienen - und nichts blieb stehen. "angles laesst sich nicht
    // auswaehlen."
    //
    // Es war nie die Ausrichtung. Die drei Runden davor haben ein zweites,
    // echtes Problem behoben (die Bloecke ueberlappten wirklich), aber
    // nicht dieses.
    if (n <= g_app->helpGetType.size()) {
        return;
    }
    g_app->helpGetType.resize(n, "FLOAT");
    g_app->helpGetName.resize(n, "SET_PARM1");
    // Wie im Original: Tag schreibt ohne Eingabe tag( "targetname", ORIGIN).
    g_app->helpTagName.resize(n, "targetname");
    g_app->helpTagType.resize(n, "ORIGIN");
    g_app->helpRangeLow.resize(n, 0.0F);
    g_app->helpRangeHigh.resize(n, 1.0F);
}

// Leerzeichen vorn und hinten weg. Es gibt in dieser Datei bisher keine
// solche Hilfe; die Leser in `src/` haben ihre eigene.
std::string ohneRand(const std::string& t) {
    std::size_t a = 0;
    std::size_t b = t.size();
    while (a < b && (t[a] == ' ' || t[a] == '\t')) { ++a; }
    while (b > a && (t[b - 1] == ' ' || t[b - 1] == '\t')) { --b; }
    return t.substr(a, b - a);
}

// Die Helferzeilen aus den Feldwerten zurueckgewinnen.
//
// Der Helfer baut aus vier Zeilen einen Ausdruck und schreibt ihn ins
// Feld. Gespeichert wird das FELD; die vier Zeilen sind reine Bedienung
// und standen beim naechsten Oeffnen wieder auf den Vorgaben - auch wenn
// im Feld `get( FLOAT, "SET_PARM1" )` stand.
//
// Die drei Formen werden an genau einer Stelle gebaut (siehe die Knoepfe
// Get, Tag und Rnd), also lassen sie sich an genau einer Stelle
// zurueckgewinnen. Was nicht passt, bleibt unangetastet: ein von Hand
// getippter Ausdruck soll die Zeilen nicht auf geratene Werte stellen.
// Schluessel fuer die gemerkten Helferzeilen: "if/0", "set/1" ...
std::string helferSchluessel(const std::string& befehl, std::size_t feld) {
    return befehl + "/" + std::to_string(feld);
}

// Die gemerkten Helferzeilen fuer diesen Befehl holen.
//
// Sie stehen in den Einstellungen, nicht im Skript - siehe die Begruendung
// bei `Settings::helfer`. Zuerst diese, DANACH das, was im Feld steht: ein
// `get( ... )` im Feld ist die staerkere Wahrheit, weil es tatsaechlich
// gespeichert wurde.
void helferAusEinstellungen(const std::string& befehl) {
    helferGross(g_app->editorValues.size());
    for (std::size_t i = 0; i < g_app->editorValues.size(); ++i) {
        const auto it = g_app->settings.helfer.find(helferSchluessel(befehl, i));
        if (it == g_app->settings.helfer.end()) {
            continue;
        }
        // Typ|Name|Marke|Art|von|bis
        std::vector<std::string> teile;
        std::string rest = it->second;
        for (;;) {
            const std::size_t k = rest.find('|');
            if (k == std::string::npos) { teile.push_back(rest); break; }
            teile.push_back(rest.substr(0, k));
            rest = rest.substr(k + 1);
        }
        if (teile.size() < 6) {
            continue;   // alte oder kaputte Zeile - lieber nichts anfassen
        }
        g_app->helpGetType[i] = teile[0];
        g_app->helpGetName[i] = teile[1];
        g_app->helpTagName[i] = teile[2];
        g_app->helpTagType[i] = teile[3];
        g_app->helpRangeLow[i] = static_cast<float>(std::atof(teile[4].c_str()));
        g_app->helpRangeHigh[i] = static_cast<float>(std::atof(teile[5].c_str()));
    }
}

// Und zurueckschreiben - beim Ok und beim Abbrechen, damit die Wahl auch
// dann bleibt, wenn am Skript nichts geaendert wurde.
void helferInEinstellungen(const std::string& befehl) {
    for (std::size_t i = 0; i < g_app->editorValues.size() &&
                            i < g_app->helpGetType.size(); ++i) {
        char zahl[64];
        std::snprintf(zahl, sizeof(zahl), "%.3f|%.3f",
                      static_cast<double>(g_app->helpRangeLow[i]),
                      static_cast<double>(g_app->helpRangeHigh[i]));
        g_app->settings.helfer[helferSchluessel(befehl, i)] =
            g_app->helpGetType[i] + "|" + g_app->helpGetName[i] + "|" +
            g_app->helpTagName[i] + "|" + g_app->helpTagType[i] + "|" + zahl;
    }
}

void helferAusWert() {
    helferGross(g_app->editorValues.size());
    for (std::size_t i = 0; i < g_app->editorValues.size(); ++i) {
        const std::string& v = g_app->editorValues[i];
        // Steht im Feld selbst ein SET-Name, ist er der Vorschlag fuer Get -
        // so macht es das Original nach "Expr!": bei den Makrozeilen
        // set ( "SET_BEHAVIOR_STATE", ... ) steht darunter
        // [SET_BEHAVIOR_STATE v] [Get] statt SET_PARM1.
        if (const TypeSet* st = g_app->db.typeset("SET_TYPES");
            st != nullptr && !v.empty() && st->find(v) != nullptr) {
            g_app->helpGetName[i] = v;
            continue;
        }
        // get( TYP, "NAME" )
        if (v.rfind("get(", 0) == 0) {
            const std::size_t k = v.find(',');
            const std::size_t a1 = v.find('"');
            const std::size_t a2 = (a1 == std::string::npos)
                                       ? std::string::npos
                                       : v.find('"', a1 + 1);
            if (k != std::string::npos && a1 != std::string::npos &&
                a2 != std::string::npos && k > 4) {
                g_app->helpGetType[i] = ohneRand(v.substr(4, k - 4));
                g_app->helpGetName[i] = v.substr(a1 + 1, a2 - a1 - 1);
            }
            continue;
        }
        // tag( "NAME", ART )
        if (v.rfind("tag(", 0) == 0) {
            const std::size_t a1 = v.find('"');
            const std::size_t a2 = (a1 == std::string::npos)
                                       ? std::string::npos
                                       : v.find('"', a1 + 1);
            const std::size_t k = v.find(',', (a2 == std::string::npos) ? 0 : a2);
            const std::size_t zu = v.rfind(')');
            if (a1 != std::string::npos && a2 != std::string::npos &&
                k != std::string::npos && zu != std::string::npos && zu > k) {
                g_app->helpTagName[i] = v.substr(a1 + 1, a2 - a1 - 1);
                g_app->helpTagType[i] = ohneRand(v.substr(k + 1, zu - k - 1));
            }
            continue;
        }
        // random( VON, BIS )
        if (v.rfind("random(", 0) == 0) {
            float von = 0.0F;
            float bis = 0.0F;
            if (std::sscanf(v.c_str(), "random( %f , %f )", &von, &bis) == 2) {
                g_app->helpRangeLow[i] = von;
                g_app->helpRangeHigh[i] = bis;
            }
            continue;
        }
    }
}

// Die Helferzeilen EINER Spalte - Bilder 4 und 6 des Originals.
//
// Sie bauen die drei Ausdruecke, die ICARUS kennt. Die Signatur von get()
// steht in der behaved.bhc selbst, in einer auskommentierten Zeile:
//     //get( %t="DECLARE_TYPE", %s="variablename" );
//
// Abweichung vom Original, bewusst: dort hat die Tag-Zeile nur eine
// Klappliste, der Name kommt von woanders. Hier steht ein eigenes Feld
// dafuer - sonst kann man tag() ohne Umweg ueber die Tastatur nicht bauen.
// Die drei Ausdruecke der Helferknoepfe - Schreibweise wie das Original und
// wie Ravens 1510 Skripte (tag( "X", ORIGIN) 950-mal, get( FLOAT, "X")
// 245-mal, random( N, N ) 188-mal): kein Leerzeichen vor der schliessenden
// Klammer bei get und tag, Zahlen ohne angehaengte Nullen (%g).
//
// Bis rc568 schrieb behaved `get( FLOAT, "X" )` und `random( 0.000, 1.000 )`.
// Gueltig, aber anders als alles, was BehavEd je gespeichert hat.
std::string getAusdruck(std::size_t i) {
    return "get( " + g_app->helpGetType[i] + ", \"" + g_app->helpGetName[i] + "\")";
}
std::string tagAusdruck(std::size_t i) {
    return "tag( \"" + g_app->helpTagName[i] + "\", " + g_app->helpTagType[i] + ")";
}

void drawHelperRows(std::size_t i, std::string* target, const Param* nurGet) {
    // --- Die Spalte ist so breit wie IHR Feld ---------------------------
    //
    // Hier stand `GetFontSize() * 10` - eine erfundene Zahl. Zwei solche
    // Klapplisten plus Knopf sind breiter als das Feld darueber, und die
    // Bloecke der Nachbarspalten ueberlappten sich.
    //
    // Sichtbar auf shanks Bild: "SET_PAR FLOAT" - die abgeschnittene
    // Klappliste der ersten Spalte und die der zweiten uebereinander. Und
    // wer oben liegt, bekommt den Klick: "I can only change the first
    // parameter."
    //
    // Also aus der GEMESSENEN Feldbreite ableiten. Eine Zeile traegt zwei
    // Klapplisten und einen Knopf; der Knopf bekommt, was er braucht, der
    // Rest wird geteilt.
    // --- EINE Klappliste je Zeile, wie im Original ----------------------
    //
    // Der eigentliche Unterschied zu Bild 2 war nicht der Abstand, sondern
    // der AUFBAU: im Original steht in jeder Zeile EINE Klappliste ueber
    // die volle Spaltenbreite -
    //
    //     [FLOAT              v]
    //     [SET_PARM1          v] [Get]
    //     [ORIGIN             v] [Tag]
    //     [0.0] .. range .. [1.0] [Rnd]
    //
    // - waehrend ich zwei nebeneinander gequetscht habe. Deshalb war alles
    // eng und "SET_PA" abgeschnitten, obwohl die Spaltenbreite stimmte.
    //
    // Die Spalte darf ausserdem breiter sein als ihr Feld: die
    // Klapplisteneintraege sind laenger als die meisten Werte. Sie
    // bestimmt dann die Fensterbreite mit - genau das, worum shank
    // gebeten hat.
    // Die ZELLE gibt die Breite vor.
    //
    // Seit rc488 stehen die Bloecke in einer Tabelle. Deren Zelle kennt
    // ihre Breite genau - eine eigene Rechnung daneben waere wieder die
    // zweite Fassung, und genau daran ist die Ausrichtung schon zweimal
    // gescheitert.
    //
    // `GetContentRegionAvail` ist hier die einzige Wahrheit; `editorFeldW`
    // hat die Spalte bereits bestimmt, als die Tabelle angelegt wurde.
    const float spalte = ImGui::GetContentRegionAvail().x;
    const float knopfW =
        std::max({ImGui::CalcTextSize(tr(Str::EditorGet)).x,
                  ImGui::CalcTextSize(tr(Str::EditorTag)).x,
                  ImGui::CalcTextSize(tr(Str::EditorRnd)).x}) +
        ImGui::GetStyle().FramePadding.x * 2.0F;
    const float abstand = ImGui::GetStyle().ItemSpacing.x;
    // Breite einer Klappliste, die sich die Zeile mit einem Knopf teilt.
    const float w = std::max(ImGui::GetFontSize() * 4.0F,
                             spalte - knopfW - abstand);

    helferGross(i + 1U);
    ImGui::PushID(static_cast<int>(i));

    // Die Klapplisten schreiben SOFORT ins Feld - wenn dort schon ein
    // Ausdruck dieser Form steht.
    //
    // shank zu rc553: "wenn ich param 7 setze und OK druecke und das
    // Fenster oeffne steht wieder Param 1 drin."
    //
    // Sein Protokoll zeigt, dass der WERT sauber hin und zurueck laeuft:
    //
    //   Editor Ok  "use": [0] "get(STRING,"targetname")"
    //   Editor auf "use": [0] "get(STRING,"targetname")" Helfer=STRING/...
    //
    // Der Wert wurde also gespeichert. Nur eben der alte: die Klapplisten
    // fuetterten bisher NUR den Knopf. Wer den Namen umstellte und dann
    // "Ok" drueckte, hat das Feld nie angefasst - und beim naechsten
    // Oeffnen liest die Rueckgewinnung wieder den alten Namen heraus.
    //
    // Aus seiner Sicht: "die Helferzeilen werden nicht gespeichert." Aus
    // Sicht des Programms: sie wurden nie irgendwohin geschrieben.
    //
    // Jetzt bauen Typ und Name den Ausdruck neu, sobald man sie aendert -
    // aber NUR, wenn im Feld schon ein `get(` steht. Sonst wuerde ein von
    // Hand getippter Ausdruck beim blossen Anfassen einer Klappliste
    // ueberschrieben, und das waere schlimmer als das Ausgangsproblem.
    const bool istGet = target != nullptr && target->rfind("get(", 0) == 0;
    const bool istTag = target != nullptr && target->rfind("tag(", 0) == 0;
    const bool istRnd = target != nullptr && target->rfind("random(", 0) == 0;

    // --- "Helper": nur die Zeilen, die zum Typ des Feldes passen ---------
    //
    // "Only the get fields that match the type of the expected data will be
    // available. If the expected data type is a float you also have the
    // choice of using the random function" (Handbuch). Am Original
    // abgelesen (27.09., set mit SET_PARM1 / SET_HEALTH / SET_ORIGIN):
    //
    //     <str>          [SET_PARM1 v] [Get]
    //     <int>/<float>  [SET_PARM1 v] [Get]   [0.0] .. range .. [1.0] [Rnd]
    //     <vec>          [SET_PARM1 v] [Get]   [ORIGIN v] [Tag]
    //
    // Keine Typliste: der Typ von get() folgt aus dem Feld.
    bool zeigeTyp = true;
    bool zeigeTag = true;
    bool zeigeRnd = true;
    if (nurGet != nullptr) {
        const bool zahl =
            nurGet->kind == Param::Kind::Float || nurGet->kind == Param::Kind::Int;
        const bool vektor = nurGet->kind == Param::Kind::Vector;
        g_app->helpGetType[i] = zahl ? "FLOAT" : (vektor ? "VECTOR" : "STRING");
        zeigeTyp = false;
        zeigeTag = vektor;
        zeigeRnd = zahl;
    }

    // Zeile 1: der Typ, allein und ueber die volle Breite.
    ImGui::SetNextItemWidth(spalte);
    if (zeigeTyp && typeSetCombo("##htype", "DECLARE_TYPE", &g_app->helpGetType[i],
                     spalte) &&
        istGet) {
        *target = getAusdruck(i);
    }

    // Zeile 2: der Name, dahinter Get.
    ImGui::SetNextItemWidth(w);
    if (typeSetCombo("##hname", "SET_TYPES", &g_app->helpGetName[i], w) &&
        istGet) {
        *target = getAusdruck(i);
    }
    ImGui::SameLine(0.0F, abstand);
    if (ImGui::Button(tr(Str::EditorGet), ImVec2{knopfW, 0.0F})) {
        *target = getAusdruck(i);
    }

    if (zeigeTag) {
    // Zeile 3: Tag-Name, Tag-Art und der Knopf - in EINER Zeile.
    //
    // shank: "viele sind so aufgebaut, wo oben eine leere Spalte ist und
    // unten das Tag. Ich will es aber nebeneinander. Das sieht besser aus
    // und ist fuer alle besser."
    //
    // rc486 hatte beide auf eigene Zeilen gelegt, weil das Original in
    // jeder Zeile EINE Klappliste hat. Stimmt - aber dort kommt der
    // Tag-Name gar nicht vor, das Feld ist unsere Zutat. Zwei halbe Zeilen
    // dafuer aufzuwenden war meine Auslegung, nicht die des Originals.
    //
    //     [Tag-Name        ] [ORIGIN      v] [Tag]
    //
    // Die beiden teilen sich, was neben dem Knopf uebrig bleibt.
    const float halb = std::max(ImGui::GetFontSize() * 3.0F,
                                (spalte - knopfW - abstand * 2.0F) * 0.5F);
    ImGui::SetNextItemWidth(halb);
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", g_app->helpTagName[i].c_str());
        if (ImGui::InputText("##htagname", buf, sizeof(buf))) {
            g_app->helpTagName[i] = buf;
            if (istTag) {
                *target = tagAusdruck(i);
            }
        }
    }
    ImGui::SameLine(0.0F, abstand);
    ImGui::SetNextItemWidth(halb);
    if (typeSetCombo("##htagtype", "TAG_TYPE", &g_app->helpTagType[i], halb) &&
        istTag) {
        *target = tagAusdruck(i);
    }
    ImGui::SameLine(0.0F, abstand);
    if (ImGui::Button(tr(Str::EditorTag), ImVec2{knopfW, 0.0F})) {
        *target = tagAusdruck(i);
    }

    }  // zeigeTag

    if (zeigeRnd) {
    // Zeile 5: der Bereich.
    const float textW = ImGui::CalcTextSize(tr(Str::EditorRange)).x;
    const float zahlW = std::max(ImGui::GetFontSize() * 3.0F,
                                 (spalte - knopfW - textW - abstand * 3.0F) *
                                     0.5F);
    ImGui::SetNextItemWidth(zahlW);
    if (ImGui::InputFloat("##hlow", &g_app->helpRangeLow[i], 0.0F, 0.0F,
                          "%.3f") &&
        istRnd) {
        char rb[96];
        std::snprintf(rb, sizeof(rb), "random( %g, %g )",
                      static_cast<double>(g_app->helpRangeLow[i]),
                      static_cast<double>(g_app->helpRangeHigh[i]));
        *target = rb;
    }
    ImGui::SameLine(0.0F, abstand);
    ImGui::TextUnformatted(tr(Str::EditorRange));
    ImGui::SameLine(0.0F, abstand);
    ImGui::SetNextItemWidth(zahlW);
    if (ImGui::InputFloat("##hhigh", &g_app->helpRangeHigh[i], 0.0F, 0.0F,
                          "%.3f") &&
        istRnd) {
        char rb[96];
        std::snprintf(rb, sizeof(rb), "random( %g, %g )",
                      static_cast<double>(g_app->helpRangeLow[i]),
                      static_cast<double>(g_app->helpRangeHigh[i]));
        *target = rb;
    }
    ImGui::SameLine(0.0F, abstand);
    if (ImGui::Button(tr(Str::EditorRnd), ImVec2{knopfW, 0.0F})) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "random( %g, %g )",
                      static_cast<double>(g_app->helpRangeLow[i]),
                      static_cast<double>(g_app->helpRangeHigh[i]));
        *target = buf;
    }
    }  // zeigeRnd
    // Die rechte Kante der Helferzeilen, gemessen - dieselbe Spalte wie
    // oben. Sind beide Zahlen gleich, stimmt die Ausrichtung.
    //
    if (i == 0 && nurGet == nullptr) {
        g_app->dbgHelferEnde = ImGui::GetItemRectMax().x;
        g_app->dbgKnopfW = knopfW;
    }
    ImGui::PopID();
}


// --- Dateifelder: "..." (Browse) und "Play" -------------------------------
//
// Ein Feld mit Dateifilter traegt in der .bhc bzw. den Kopfdateien Ravens
// Pfad dazu, in zwei Schreibweisen:
//
//     !!"W:\game\base\scripts\!!#*.txt"        SET_SPAWNSCRIPT u. a.
//     !!"W:\game\base\!!sound\voice\*.wav;*.mp3"   CHAN_VOICE
//
// Vor dem zweiten "!!" steht der Basisordner, dahinter (nach einem
// moeglichen "#") Unterordner und Maske. Raven hatte das Spiel auf W:\;
// gemeint ist der Teil hinter "\base\" - bei uns die Spielordner.
bool spew(const std::string& path, const std::string& text);

struct DateiFilter {
    std::string basisRel;     // "scripts\" oder ""
    std::string maskeOrdner;  // "sound\voice\" oder ""
    std::string maske;        // "*.wav;*.mp3"
};

std::string kleinSchreiben(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

DateiFilter zerlegeFilter(const std::string& f) {
    DateiFilter d;
    const std::size_t bang = f.find("!!");
    const std::string basis = (bang == std::string::npos) ? std::string() : f.substr(0, bang);
    std::string rest = (bang == std::string::npos) ? f : f.substr(bang + 2);
    if (!rest.empty() && rest[0] == '#') {
        rest.erase(0, 1);
    }
    const std::size_t b = kleinSchreiben(basis).find("\\base\\");
    d.basisRel = (b == std::string::npos) ? std::string() : basis.substr(b + 6);
    const std::size_t s = rest.find_last_of("\\/");
    if (s == std::string::npos) {
        d.maske = rest;
    } else {
        d.maskeOrdner = rest.substr(0, s + 1);
        d.maske = rest.substr(s + 1);
    }
    return d;
}

bool istSkriptFilter(const DateiFilter& d) {
    const std::string m = kleinSchreiben(d.maske);
    return m.find(".txt") != std::string::npos || m.find(".ibi") != std::string::npos;
}

// Wo die Dateien dieses Feldes liegen koennen, mit abschliessendem "\".
// Fuer Skripte zuerst der Skriptpfad aus den Einstellungen.
std::vector<std::string> dateiBasen(const DateiFilter& d) {
    std::vector<std::string> out;
    auto dazu = [&out](std::string p) {
        if (p.empty()) { return; }
        if (p.back() != '\\' && p.back() != '/') { p += '\\'; }
        out.push_back(p);
    };
    if (istSkriptFilter(d)) {
        dazu(g_app->settings.scriptPath);
    }
    for (const GamePath& gp : g_app->gamePaths) {
        std::string p = gp.directory;
        if (!p.empty() && p.back() != '\\' && p.back() != '/') { p += '\\'; }
        dazu(p + d.basisRel);
    }
    return out;
}

bool dateiDa(const std::string& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::exists(std::filesystem::u8path(p), ec);
}

// "..." - Datei waehlen und RELATIV eintragen.
//
// Das Original oeffnete dafuer einen Dateidialog ("Browse to file", "All
// files|*.*"); bei behaved tat der Knopf bisher nichts. Hier startet der
// Dialog im passenden Unterordner, bietet die Maske des Feldes an (und
// alle Dateien), und das Ergebnis steht so da, wie das Spiel es erwartet:
//
//     Klang   sound/voice/kyle/hello.mp3     relativ zum Spielordner
//     Skript  md_twj/s3                      relativ zu scripts\, ohne Endung
//
// Die Engine laedt Skripte als "scripts/<name>" plus .ibi - eine Endung
// im Feld waere falsch.
void durchsuchen(const Param& p, std::string& wert) {
    const DateiFilter d = zerlegeFilter(p.filter);
    const std::vector<std::string> basen = dateiBasen(d);
    std::string start;
    for (const std::string& b : basen) {
        if (dateiDa(b + d.maskeOrdner)) {
            start = b + d.maskeOrdner;
            break;
        }
    }
    if (start.empty() && !basen.empty()) { start = basen.front(); }
    std::string filter;
    if (!d.maske.empty() && d.maske != "*.*") {
        filter = d.maske + "|" + d.maske + "|";
    }
    filter += "All files (*.*)|*.*";
    const std::string wahl = platform::openFileDialog(tr(Str::BrowseTitle),
                                                      filter.c_str(), start);
    if (wahl.empty()) {
        return;
    }
    std::string rel;
    const std::string wahlKlein = kleinSchreiben(wahl);
    for (const std::string& b : basen) {
        const std::string bk = kleinSchreiben(b);
        if (wahlKlein.compare(0, bk.size(), bk) == 0) {
            rel = wahl.substr(b.size());
            break;
        }
    }
    if (rel.empty()) {
        // Ausserhalb der Spielordner: vom Unterordner der Maske an nehmen,
        // falls er im Pfad vorkommt ("...\sound\voice\x.mp3"), sonst den
        // ganzen Pfad - dann sieht man wenigstens, was gewaehlt wurde.
        const std::string marke = kleinSchreiben(
            "\\" + (d.maskeOrdner.empty() ? d.basisRel : d.maskeOrdner));
        const std::size_t at = (marke.size() > 1) ? wahlKlein.rfind(marke) : std::string::npos;
        rel = (at == std::string::npos) ? wahl : wahl.substr(at + 1);
    }
    for (char& ch : rel) {
        if (ch == '\\') { ch = '/'; }
    }
    if (istSkriptFilter(d)) {
        const std::size_t punkt = rel.find_last_of('.');
        const std::size_t strich = rel.find_last_of('/');
        if (punkt != std::string::npos &&
            (strich == std::string::npos || punkt > strich)) {
            rel.erase(punkt);
        }
    }
    diag::detail("Durchsuchen: \"" + wahl + "\" -> \"" + rel + "\"");
    wert = rel;
}

// "Play" - je nach Art des Feldes (Original: der "run"-Knopf).
//
//   Skript   in einer NEUEN behaved-Instanz oeffnen, wie das Original
//            ("start ... BehavEd.exe"). Nicht in einem Reiter dieses
//            Fensters: der Ereigniseditor bearbeitet gerade eine Zeile im
//            aktuellen Skript, und ein Reiterwechsel darunter liesse "Ok"
//            ins falsche Skript schreiben. Liegt es nur in einem Archiv, wird
//            es dafuer als Datei in %TEMP% abgelegt.
//   Video    (.roq, .bik) mit dem Programm, das Windows dafuer vorsieht.
//   Klang    wie bisher im eigenen Mischer, auch aus den Archiven - das
//            kann behaved besser als das Original.
void abspielen(const Param& p, const std::string& wertRoh) {
    const DateiFilter d = zerlegeFilter(p.filter);
    std::string wert = wertRoh;
    for (char& ch : wert) {
        if (ch == '/') { ch = '\\'; }
    }
    const std::string maske = kleinSchreiben(d.maske);
    const bool video = maske.find(".roq") != std::string::npos ||
                       maske.find(".bik") != std::string::npos;
    if (!istSkriptFilter(d) && !video) {
        playSound(wertRoh);
        return;
    }
    std::vector<std::string> kandidaten;
    for (const std::string& b : dateiBasen(d)) {
        if (istSkriptFilter(d)) {
            kandidaten.push_back(b + wert + ".txt");
            kandidaten.push_back(b + wert);
        } else {
            kandidaten.push_back(b + d.maskeOrdner + wert);
            kandidaten.push_back(b + wert);
        }
    }
    for (const std::string& k : kandidaten) {
        if (!dateiDa(k)) { continue; }
        diag::detail("Play: \"" + k + "\"");
        if (istSkriptFilter(d)) {
            if (!platform::startBehavedWith(k)) {
                addStatus("Unable to launch BehavEd on this machine!");
            }
        } else {
            platform::openWithDefaultApp(k);
        }
        return;
    }
    // Skripte liegen bei Movie Duels meist in den Archiven.
    if (istSkriptFilter(d)) {
        std::string imArchiv = "scripts/" + wertRoh + ".txt";
        for (char& ch : imArchiv) {
            if (ch == '\\') { ch = '/'; }
        }
        for (const GamePath& gp : g_app->gamePaths) {
            for (const Pk3& arc : gp.archives) {
                const Pk3Entry* e = arc.find(kleinSchreiben(imArchiv));
                if (e == nullptr) { e = arc.find(imArchiv); }
                std::string inhalt;
                if (e == nullptr || !readPk3File(arc, *e, inhalt)) { continue; }
                std::error_code ec;
                const std::filesystem::path ordner =
                    std::filesystem::temp_directory_path(ec) / "behaved";
                std::filesystem::create_directories(ordner, ec);
                std::string datei = wertRoh;
                for (char& ch : datei) {
                    if (ch == '/' || ch == '\\') { ch = '_'; }
                }
                // u8string() ist seit C++20 ein std::u8string.
                const std::u8string ziel8 = (ordner / (datei + ".txt")).u8string();
                const std::string ziel(ziel8.begin(), ziel8.end());
                if (spew(ziel, inhalt)) {
                    diag::detail("Play: \"" + imArchiv + "\" aus " + arc.path + " -> " + ziel);
                    if (!platform::startBehavedWith(ziel)) {
                        addStatus("Unable to launch BehavEd on this machine!");
                    }
                    return;
                }
            }
        }
    }
    char z[300];
    std::snprintf(z, sizeof(z), "Unable to find file \"%s\"", wertRoh.c_str());
    addStatus(z);
}

void drawEditor() {
    if (!g_app->editorOpen || g_app->editorCmd == nullptr) { return; }
    const Command& c = *g_app->editorCmd;

    // Der Titel nennt den Befehl: "Event editor - set ( variablename, value )"
    // statt immer nur "Event editor". Bei zehn offenen Fenstern hintereinander
    // ist das der Unterschied zwischen Wissen und Raten.
    //
    // "###editor" haengt die Kennung des Fensters fest. Ohne das leitet ImGui
    // sie aus dem Titel ab - und der aendert sich mit jedem Befehl, womit das
    // Popup als NEUES Fenster gaelte und sofort wieder zuginge.
    // Param traegt keinen Namen - die Beschriftung steht in der Signatur.
    // Die zeigt der Baum ohnehin, also nehmen wir sie hier auch: aus
    // "set ( <str>, <str> )" wird der Titel "Event editor - set".
    std::string title = std::string(tr(Str::EditorTitle)) + " - " + c.name;
    {
        const std::string kurz = descFor(c);
        if (!kurz.empty()) {
            title += "  (" + kurz + ")";
        }
    }
    title += "###editor";

    // --- Alles, was die BREITE bestimmt, steht VOR `Begin` fest ----------
    //
    // shank: "wenn ich if oeffne dann shaked der Bildschirm immer."
    //
    // Von rc543 bis rc567 wurde das Fenster aus dem VORIGEN Bild gemittelt:
    // Reihe messen, Versatz daraus, Fenster waechst, nachruecken, Breite
    // lernen. Jede Runde fing einen Fall und liess einen neuen offen. Der
    // Selbsttest (BHED_EDITORTEST) zeigt, warum, beim dreifeldrigen `if`:
    //
    //   Bild 2  verborgen   Reihe 1044  (Zellen noch gerechnet)
    //   Bild 3  1299 breit  Reihe 1160
    //   Bild 4  1414 breit  Reihe 1276  <- Versatz aus 1160, schiesst ueber
    //   Bild 5  1357 breit
    //   Bild 6  1299 breit
    //
    // Die Reihe war im Vorbild schmaler als jetzt, ihr Versatz schob sie
    // nach rechts, das Fenster wuchs, und Beschreibung und Knoepfe (gegen
    // die Fensterbreite gemittelt) liessen es halbierend zurueckschwingen.
    //
    // ImGuis Autor zu genau diesem Fall: bei AlwaysAutoResize ist die
    // Breite im ersten Bild undefiniert - "You can't really have both ...
    // set an explicit width via SetNextWindowSize(ImVec2(400,0))".
    //
    // Hier geht das vollstaendig: jede Spaltenbreite folgt aus Text und
    // Schrift, und die Tabelle setzt feste Spalten genau so, wie bestellt
    // (imgui_tables.cpp: `WidthAuto = InitStretchWeightOrWidth` bei
    // WidthFixed|NoResize, Spaltenabstand 2 * CellPadding.x bei
    // NoPadOuterX ohne Rahmen). Also wird die Fensterbreite AUSGERECHNET und
    // mit `ImGuiCond_Always` vorgegeben. Nichts im Fenster liest mehr die
    // Fensterbreite zurueck - es gibt keinen Kreis, der schwingen koennte.
    // Die Hoehe bleibt automatisch (0).
    const std::vector<Param> fields =
        editorFields(c, g_app->db, g_app->editorValues);
    g_app->editorValues.resize(fields.size());

    // Werte nachziehen, wenn sich eine Auswahl geaendert hat.
    //
    // Nur die Felder DAHINTER, und nur die, bei denen sich auch etwas
    // geaendert hat: wer von SET_PARM1 auf SET_PARM2 wechselt, hat weiter
    // ein Textfeld und soll seinen getippten Wert behalten. Wer auf
    // SET_HEALTH wechselt, bekommt ein Zahlenfeld - und dort waere
    // "DEFAULT" schlicht falsch.
    if (g_app->editorRefreshFrom >= 0) {
        const auto from = static_cast<std::size_t>(g_app->editorRefreshFrom);
        // "Neu auswerten" fasst AUCH das erste Feld an.
        //
        // Beim Nachziehen nach einer Auswahl ist `from + 1` richtig: wer
        // Feld 0 aendert, will die Felder DAHINTER neu haben, nicht sein
        // eigenes. Der Knopf setzt aber `editorRefreshFrom = 0` und meint
        // "alles" - und traf damit Feld 0 nie.
        //
        // shank zu rc542, mit Bild: nach "Re-Evaluate" stehen Feld 2 und 3
        // auf ihren Vorgaben, Feld 1 zeigt weiter `random( 0.000, 1.000 )`.
        const std::size_t ab = g_app->editorForceRefresh ? 0U : from + 1U;
        for (std::size_t k = ab; k < fields.size(); ++k) {
            // Die AUSWAHL selbst fasst "Re-Evaluate" nicht an.
            //
            // shank, 27.09., mit Bild: "If you click the Re-Evaluate button
            // it will reset the selected command to SET_PARM1. It should
            // only reset its parameters." - bei SET_WALKING sprang Feld 0
            // auf den ersten Eintrag der Liste.
            //
            // Das Original (Abgleich, alle Befehle): SET_WALKING, CHAN_VOICE,
            // STRING (declare), ENABLE und PLAY_ROFF bleiben stehen; neu
            // aufgebaut werden nur die Felder dahinter. Also: die fuehrende
            // Klappliste und jede Auswahl mit eigenen Feldern auslassen.
            if (g_app->editorForceRefresh && fields[k].kind == Param::Kind::TypeSet &&
                (feldKnopf(c, k) == FeldKnopf::Keiner ||
                 auswahlMitFeldern(g_app->db, fields[k].typeset))) {
                continue;
            }
            const bool kindChanged =
                k >= g_app->editorKinds.size() ||
                g_app->editorKinds[k] != static_cast<int>(fields[k].kind);
            const std::string want = defaultValueFor(fields[k], g_app->db);
            const bool defChanged =
                k >= g_app->editorDefs.size() || g_app->editorDefs[k] != want;
            if (kindChanged || defChanged || g_app->editorForceRefresh) {
                // defaultValueFor statt fields[k].def: in der .bhc steht bei
                // affect der TYPNAME "AFFECT_TYPE" als Platzhalter. Der rohe
                // Text landete so im Skript - richtig ist FLUSH, der erste
                // Eintrag der Typmenge.
                g_app->editorValues[k] = want;
            }
        }
        g_app->editorRefreshFrom = -1;
        g_app->editorForceRefresh = false;
    }
    // Fuer den naechsten Vergleich merken.
    g_app->editorKinds.assign(fields.size(), 0);
    g_app->editorDefs.assign(fields.size(), std::string{});
    for (std::size_t k = 0; k < fields.size(); ++k) {
        g_app->editorKinds[k] = static_cast<int>(fields[k].kind);
        g_app->editorDefs[k] = defaultValueFor(fields[k], g_app->db);
    }
    // Leere Felder bekommen ihre Vorgabe schon hier - ihre Breite zaehlt.
    for (std::size_t k = 0; k < fields.size(); ++k) {
        if (g_app->editorValues[k].empty()) {
            g_app->editorValues[k] = fields[k].def;
        }
    }

    // Befehlsname, rechtsbuendig bis 91 dlu - dort endet der STATIC aus
    // Dialog 137, und genau dort faengt das erste Feld an.
    std::string upper = c.name;
    for (char& ch : upper) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    const float labelW = dx(91);
    const float tw = ImGui::CalcTextSize(upper.c_str()).x;

    // Die Beschreibung: die des gewaehlten Eintrags, sonst die des Befehls.
    std::string help = descFor(c);
    if (!c.params.empty() && c.params[0].kind == Param::Kind::TypeSet &&
        !g_app->editorValues.empty()) {
        const TypeSet* ts = g_app->db.typeset(c.params[0].typeset);
        if (ts != nullptr) {
            const TypeEntry* e = ts->find(g_app->editorValues[0]);
            if (e != nullptr && !e->desc.empty()) { help = e->desc; }
        }
    }
    if (help.empty()) { help = tr(Str::EditorNoHelp); }

    const ImGuiStyle& edStil = ImGui::GetStyle();
    const float linksrand = edStil.WindowPadding.x;
    const float breiteHilfe =
        ImGui::CalcTextSize(("( " + help + " )").c_str()).x;
    const float btnW = std::max(dx(kEdOkW),
                                ImGui::CalcTextSize(tr(Str::EditorReEvaluate)).x +
                                    edStil.FramePadding.x * 2.0F);
    const float breiteKnoepfe = btnW * 3.0F + edStil.ItemSpacing.x * 2.0F;
    // Das Fenster muss mindestens so breit sein wie sein Titel plus
    // Schliessknopf - sonst schneidet ImGui den Titel ab, und der Rest
    // liest sich linksbuendig (shank zu `rem`).
    const float breiteTitel = ImGui::CalcTextSize(title.c_str()).x +
                              edStil.FramePadding.x * 2.0F +
                              ImGui::GetFontSize() * 2.5F +
                              edStil.WindowPadding.x * 2.0F;

    // --- EINE Spalteneinteilung fuer die Reihe UND die Helfer -----------
    //
    // Die Spaltenbreite ist das Groessere aus Feld und Helfer, und beide
    // setzen sich an dieselbe Stelle. `editorFeldX` ist hier noch relativ
    // zum Tabellenanfang; der Versatz kommt nach `Begin` dazu.
    //
    // Abgerundet, weil die Tabelle es auch tut (`WidthGiven =
    // ImTrunc(WidthRequest)`) - sonst verrechnete sich die Summe unten um
    // einen Punkt je Spalte.
    g_app->editorFeldX.assign(fields.size(), 0.0F);
    g_app->editorFeldW.assign(fields.size(), 0.0F);
    const float spaltenAbstand = edStil.CellPadding.x * 2.0F;
    // Breite einer Klappliste (Typmenge). Sie hat KEINEN Expr!-Knopf, und
    // `comboEdit` setzt seine Breite selbst - dieselbe Formel hier und beim
    // Zeichnen. Vorher reservierte die Spalte die Knopfbreite trotzdem: der
    // Selbsttest mass bei `affect` und `camera ENABLE` 63 Punkte Luft am
    // Ende der Reihe, und die Reihe stand um die Haelfte davon schief.
    auto auswahlBreite = [](const std::string& wert) {
        return std::max(dx(kEdFieldW), ImGui::CalcTextSize(wert.c_str()).x +
                                           ImGui::GetFrameHeight() * 2.0F);
    };
    float tabelleW = 0.0F;
    {
        const float knopfB =
            std::max({ImGui::CalcTextSize(tr(Str::EditorGet)).x,
                      ImGui::CalcTextSize(tr(Str::EditorTag)).x,
                      ImGui::CalcTextSize(tr(Str::EditorRnd)).x,
                      ImGui::CalcTextSize(tr(Str::EditorExpr)).x,
                      ImGui::CalcTextSize(tr(Str::EditorHelper)).x,
                      ImGui::CalcTextSize(tr(Str::EditorRevert)).x}) +
            edStil.FramePadding.x * 2.0F;
        for (std::size_t k = 0; k < fields.size(); ++k) {
            const float ausText =
                ImGui::CalcTextSize(g_app->editorValues[k].c_str()).x +
                ImGui::GetFrameHeight() * 1.5F;
            // Die 17 Zeichenhoehen fuer einen Helferblock zaehlen NUR, wenn
            // dieser Helfer auch offen ist - bei `loop` mit "-1" wurde die
            // Spalte sonst ohne Grund breit.
            const bool helferOffen =
                (k < g_app->editorIsExpr.size()) &&
                (g_app->editorIsExpr[k] != 0);
            // Felder mit Dateifilter (z.B. `sound`) haben ZWEI weitere
            // Knoepfe: Durchsuchen und Abspielen.
            const float zusatzB =
                !fields[k].filter.empty()
                    ? (ImGui::CalcTextSize(tr(Str::EditorBrowse)).x +
                       ImGui::CalcTextSize(tr(Str::EditorPlay)).x +
                       edStil.FramePadding.x * 4.0F)
                    : 0.0F;
            // Eine Klappliste ist nur dann eine Klappliste, wenn sie nicht
            // auf Ausdruck umgestellt ist - dann ist sie ein Textfeld.
            const bool alsListe =
                fields[k].kind == Param::Kind::TypeSet && !helferOffen;
            const bool mitKnopf = feldKnopf(c, k) != FeldKnopf::Keiner;
            const float breit = std::floor(
                alsListe
                    ? auswahlBreite(g_app->editorValues[k]) + (mitKnopf ? knopfB : 0.0F)
                    : std::max({dx(kEdFieldW) + knopfB + zusatzB,
                                ausText + knopfB + zusatzB,
                                helferOffen ? ImGui::GetFontSize() * 17.0F
                                            : 0.0F}));
            if (k > 0) { tabelleW += spaltenAbstand; }
            g_app->editorFeldX[k] = tabelleW;
            g_app->editorFeldW[k] = breit;
            tabelleW += breit;
        }
    }
    // Die Reihe beginnt an der Beschriftung (rechtsbuendig in `labelW`) und
    // endet an der letzten Spalte. Die Tabelle beginnt bei `dx(kEdLeft)`.
    const float istX = labelW - tw;
    const float reiheW = (dx(kEdLeft) - istX) + tabelleW;
    const float fensterW = std::ceil(std::max({breiteTitel,
                                               reiheW + linksrand * 2.0F,
                                               breiteHilfe + linksrand * 2.0F,
                                               breiteKnoepfe + linksrand * 2.0F}));

    ImGui::OpenPopup(title.c_str());
    ImGui::SetNextWindowSize(ImVec2{fensterW, 0.0F}, ImGuiCond_Always);

    // --- Die MITTE festhalten, jedes Bild ------------------------------
    //
    // Mit dem Drehpunkt 0.5/0.5 setzt ImGui das Fenster um einen Punkt
    // herum - und zwar NACH der Groessenrechnung desselben Bildes
    // (imgui.cpp, `window_pos_with_pivot`). Waechst es, waechst es also in
    // alle Richtungen gleich, im selben Bild, ohne Nachruecken.
    //
    // Gemerkt wird die Mitte, nicht eine Stelle. Wer das Fenster zieht,
    // verschiebt die Mitte: waehrend des Ziehens setzen wir nichts, danach
    // gilt die neue Mitte.
    ImGuiWindow* edFenster = ImGui::FindWindowByName("###editor");
    // `MovingWindow` ist das angeklickte Fenster - bei einem Klick in die
    // Klappliste ein Kindfenster. Gezogen wird aber immer das oberste.
    const bool wirdGezogen = edFenster != nullptr &&
                             GImGui->MovingWindow != nullptr &&
                             GImGui->MovingWindow->RootWindow == edFenster;
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        if (g_app->editorMitte.x < 0.0F) {
            g_app->editorMitte = ImVec2{vp->WorkPos.x + vp->WorkSize.x * 0.5F,
                                        vp->WorkPos.y + vp->WorkSize.y * 0.5F};
        }
        if (!wirdGezogen) {
            // Im sichtbaren Bereich halten, soweit es hineinpasst. Die Hoehe
            // ist die des vorigen Bildes - die einzige, die es gibt.
            const float hoehe = (edFenster != nullptr) ? edFenster->Size.y : 0.0F;
            ImVec2 m = g_app->editorMitte;
            if (fensterW < vp->WorkSize.x) {
                m.x = std::clamp(m.x, vp->WorkPos.x + fensterW * 0.5F,
                                 vp->WorkPos.x + vp->WorkSize.x - fensterW * 0.5F);
            }
            if (hoehe > 0.0F && hoehe < vp->WorkSize.y) {
                m.y = std::clamp(m.y, vp->WorkPos.y + hoehe * 0.5F,
                                 vp->WorkPos.y + vp->WorkSize.y - hoehe * 0.5F);
            }
            ImGui::SetNextWindowPos(m, ImGuiCond_Always, ImVec2{0.5F, 0.5F});
        }
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2{0.5F, 0.5F});
    const bool offen =
        ImGui::BeginPopupModal(title.c_str(), &g_app->editorOpen,
                               ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (!offen) {
        return;
    }
    if (wirdGezogen) {
        const ImVec2 o = ImGui::GetWindowPos();
        const ImVec2 gr = ImGui::GetWindowSize();
        g_app->editorMitte = ImVec2{o.x + gr.x * 0.5F, o.y + gr.y * 0.5F};
    }
    if (ImGui::IsWindowAppearing()) {
        const ImGuiViewport* vpl = ImGui::GetMainViewport();
        char ze[300];
        std::snprintf(ze, sizeof(ze),
                      "Ereignisfenster \"%s\" geoeffnet: Breite %.0f (Titel %.0f, "
                      "Reihe %.0f, Hilfe %.0f, Knoepfe %.0f), Mitte %.0f/%.0f, "
                      "Arbeitsflaeche %.0fx%.0f ab %.0f/%.0f",
                      c.name.c_str(), static_cast<double>(fensterW),
                      static_cast<double>(breiteTitel),
                      static_cast<double>(reiheW),
                      static_cast<double>(breiteHilfe),
                      static_cast<double>(breiteKnoepfe),
                      static_cast<double>(g_app->editorMitte.x),
                      static_cast<double>(g_app->editorMitte.y),
                      static_cast<double>(vpl->WorkSize.x),
                      static_cast<double>(vpl->WorkSize.y),
                      static_cast<double>(vpl->WorkPos.x),
                      static_cast<double>(vpl->WorkPos.y));
        diag::detail(ze);
    }

    // --- Die Reihe mittig ------------------------------------------------
    //
    // `sollX` ist die ZIELSTELLE der Beschriftung, `istX` die Stelle, an
    // der sie ohne Versatz stuende. Ist die Reihe das breiteste Element,
    // steht sie am Rand; sonst mittig im Fenster - wie Beschreibung und
    // Knopfleiste, gegen dieselbe, feste Breite.
    const float sollX = std::max(linksrand, (fensterW - reiheW) * 0.5F);
    const float versatz = sollX - istX;
    for (float& fx : g_app->editorFeldX) {
        fx += versatz + dx(kEdLeft);
    }

    ImGui::BeginGroup();   // die Reihe als Ganzes, um sie zu messen
    ImGui::SetCursorPosX(versatz + labelW - tw);
    // Senkrecht auf die Mitte des Feldes daneben.
    //
    // Gemeldet mit Bild: "Text isn't aligned vertically either." Ein
    // reiner Text ist so hoch wie die Schrift, ein Eingabefeld hat oben und
    // unten den Rahmenabstand dazu - nebeneinander gesetzt sitzt der Text
    // deshalb zu hoch.
    //
    // AlignTextToFramePadding schiebt ihn um genau diesen Abstand nach
    // unten; dafuer ist es da.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(upper.c_str());
    // Wo die Reihe WIRKLICH beginnt - an der Beschriftung, nicht am
    // Gruppenanfang.
    //
    // Die Gruppe fasst alles zusammen, was seit BeginGroup gezeichnet
    // wurde, und ihr Kasten beginnt am Fensterrand. Maesse man von dort,
    // steckte der Versatz in der gemessenen Breite - und das Fenster
    // schaukelte sich Bild um Bild weiter auf.
    const float reiheAb = ImGui::GetItemRectMin().x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(versatz + dx(kEdLeft));
    // ANKER. Ohne ihn stuerzt `else` ab.
    //
    // shank: "wenn ich die else Zeile oeffnen will crasht das Programm."
    //
    //   IMGUI-ZUSICHERUNG: Code uses SetCursorPos() to extend
    //   window/parent boundaries. Please submit an item e.g. Dummy()
    //   afterwards.
    //
    // `else` hat KEINE Parameter. Die Feldschleife darunter laeuft also
    // nicht, und nach dem Cursorsprung kommt kein Element mehr - genau der
    // Fall, den ImGui abfaengt.
    //
    // Bei jedem anderen Befehl faellt es nicht auf, weil das erste Feld
    // sofort folgt. Das ist die Sorte Fehler, die nur der leere Fall zeigt.
    //
    // Er bleibt auch nach rc506 noetig: bei `else` und `flush` hat die
    // Tabelle NULL Spalten und wird gar nicht angelegt.
    ImGui::Dummy(ImVec2{0.0F, 0.0F});
    // ... und auf derselben Zeile weiter, sonst begaenne die Tabelle eine
    // Zeile zu tief und die Beschriftung staende wieder allein darueber
    // (rc505).
    ImGui::SameLine(0.0F, 0.0F);

    // --- Feldreihe und Helfer in EINER Tabelle --------------------------
    //
    // shank: "loop ist nicht untereinander, affect ebenfalls nicht, run
    // dasselbe."
    //
    // Die Helferbloecke standen seit rc488 in einer Tabelle, die Feldreihe
    // darueber nicht - sie setzte ihre Spalten weiter von Hand. Zwei
    // Verfahren fuer dieselbe Einteilung, und genau deshalb sassen sie
    // nicht uebereinander.
    //
    // Ich habe das viermal nachgebessert (rc485 Breite, rc486 Nachruecken,
    // rc487 gemeinsame Einteilung, rc505 Anker). Jede Runde besser, keine
    // gelöst - weil beide Zeilen getrennt rechneten.
    //
    // Jetzt EINE Tabelle mit zwei Zeilen:
    //
    //     Zeile 1   [Feld] [Revert]  ·  <expr> ..... was <str>
    //     Zeile 2   FLOAT / SET_PARM1+Get / Tag-Zeile / Bereich
    //
    // Die Ausrichtung ist damit keine Rechnung mehr, sondern eine
    // Eigenschaft der Tabelle: dieselbe Spalte, dieselbe Kante.
    //
    // `editorFeldW` bleibt - es bestimmt die Spaltenbreiten. `editorFeldX`
    // wird nicht mehr zum Setzen gebraucht, aber weiter gefuellt, weil der
    // Debug-Auszug es zeigt.
    //
    // Die Aussenbreite ist die ausgerechnete Summe, nicht "der Rest des
    // Fensters": sonst haengt die Tabelle doch wieder an der Fensterbreite,
    // und im ersten Bild (Fenster noch schmal) klemmt ImGui die Spalten -
    // gemessen: Zelle 401 statt 408.
    const int edSpalten = static_cast<int>(fields.size());
    if (edSpalten > 0 &&
        !ImGui::BeginTable("##editorreihe", edSpalten,
                           ImGuiTableFlags_NoSavedSettings |
                               ImGuiTableFlags_NoPadOuterX,
                           ImVec2{tabelleW, 0.0F})) {
        ImGui::EndGroup();
        return;
    }
    if (edSpalten > 0) {
        for (int k = 0; k < edSpalten; ++k) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed |
                                            ImGuiTableColumnFlags_NoResize,
                                    g_app->editorFeldW[
                                        static_cast<std::size_t>(k)]);
        }
        ImGui::TableNextRow();
    }
    // Eingabe/Escape, WAEHREND ein Textfeld aktiv war - siehe unten beim
    // InputText.
    bool feldEnter = false;
    bool feldEscape = false;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        ImGui::TableNextColumn();
        const Param& p = fields[i];
        std::string& v = g_app->editorValues[i];
        if (v.empty()) { v = p.def; }
        ImGui::PushID(static_cast<int>(i));
        const float x = ImGui::GetCursorPosX();

        ImGui::BeginGroup();
        // Die Spaltenbreite steht schon fest; das Feld bekommt sie minus
        // seinem Knopf. Nicht neu ausrechnen - das war die zweite Fassung.
        const float knopfB =
            std::max({ImGui::CalcTextSize(tr(Str::EditorGet)).x,
                      ImGui::CalcTextSize(tr(Str::EditorTag)).x,
                      ImGui::CalcTextSize(tr(Str::EditorRnd)).x,
                      ImGui::CalcTextSize(tr(Str::EditorExpr)).x,
                      ImGui::CalcTextSize(tr(Str::EditorHelper)).x,
                      ImGui::CalcTextSize(tr(Str::EditorRevert)).x}) +
            ImGui::GetStyle().FramePadding.x * 2.0F;
        const FeldKnopf knopfArt = feldKnopf(c, i);
        const bool isExpr = (i < g_app->editorIsExpr.size() &&
                             g_app->editorIsExpr[i] != 0);
        // Die Zellbreite IST die bestellte Spaltenbreite: feste Spalten
        // ohne Groessenaenderung bekommen genau ihre Vorgabe (abgerundet wie
        // oben), und das Zellenpolster liegt bei NoPadOuterX AUSSERHALB
        // davon (imgui_tables.cpp, `WorkMinX`/`WorkMaxX`).
        //
        // Hier wurde gemessen (rc488), gedeckelt (rc551) und im
        // Einschwingen gerechnet (rc566) - drei Fassungen derselben Zahl, und
        // jede Messung hing an der Fensterbreite des vorigen Bildes. Seit die
        // Tabelle ihre Aussenbreite vorgegeben bekommt, sind Messung und
        // Rechnung gleich; die Rechnung braucht nur kein Vorbild.
        const float zelleW = (i < g_app->editorFeldW.size())
                                 ? g_app->editorFeldW[i]
                                 : ImGui::GetContentRegionAvail().x;
        if (i == 0) {
            // Gemessen, nur zum Vergleich im Protokoll.
            g_app->dbgZelle0 = ImGui::GetContentRegionAvail().x;
            g_app->dbgBestellt0 = zelleW;
            g_app->dbgKnopfB = knopfB;
        }
        // Auch hier die Zusatzknoepfe abziehen, sonst laufen sie ueber die
        // Zellkante.
        const float zusatzHier =
            p.filter.empty()
                ? 0.0F
                : (ImGui::CalcTextSize(tr(Str::EditorBrowse)).x +
                   ImGui::CalcTextSize(tr(Str::EditorPlay)).x +
                   ImGui::GetStyle().FramePadding.x * 4.0F);
        // KEIN ItemSpacing abziehen.
        //
        // Gefunden durch Nachrechnen, nicht durch Raten:
        //
        //   Feldzeile   feldBreite + SameLine(0, 0) + knopfB
        //   Helferzeile w + SameLine(0, abstand) + knopfW
        //
        // Die Helferzeile setzt den Abstand und zieht ihn ab - beides. Die
        // Feldzeile zog ihn ab und setzte ihn NICHT (`SameLine(0, 0)` in
        // Zeile 1017). Sie endete also genau um `ItemSpacing.x` zu frueh,
        // und das sind bei shanks 144 dpi die vierzehn Punkte, die er
        // gemessen hat.
        //
        // Abziehen ODER setzen - nicht eins von beidem. Hier gilt: nicht
        // abziehen, denn `SameLine(0, 0)` steht schon da und die
        // Zusatzknoepfe haengen ebenfalls ohne Abstand daran.
        // Zielnamen aus der Karte: bei SET_CAMERA_GROUP die Kameragruppen,
        // sonst die Zielnamen. Der Pfeil dafuer sitzt in derselben Zelle und
        // muss von der Feldbreite ab - sonst schob er "Expr!" ueber die
        // Zellkante hinaus.
        const std::vector<std::string>* kartenListe = nullptr;
        if (!g_app->map.empty() && p.kind == Param::Kind::String) {
            const bool wantsGroups =
                !g_app->editorValues.empty() &&
                g_app->editorValues[0] == "SET_CAMERA_GROUP";
            kartenListe = wantsGroups ? &g_app->map.cameraGroups
                                      : &g_app->map.targetNames;
            if (kartenListe->empty()) { kartenListe = nullptr; }
        }
        const float pfeilB =
            (kartenListe != nullptr) ? ImGui::GetFrameHeight() : 0.0F;
        const float feldBreite =
            std::max(ImGui::GetFontSize() * 4.0F,
                     zelleW - knopfB - zusatzHier - pfeilB);
        ImGui::SetNextItemWidth(feldBreite);
        // Der Knopf neben dem Feld - fuer beide Arten gleich, siehe
        // feldKnopf. Eine Klappliste auf Ausdruck wird zum Textfeld (wie
        // FLUSH bei affect im Original nach "Expr!").
        // Aufrufer pruefen `knopfArt != FeldKnopf::Keiner` selbst (kein
        // `return` hier: lint_endtable sieht die Tabelle drumherum).
        auto feldKnopfZeichnen = [&]() {
            // Rueckweg immer "Revert". Das Original zeigt "Reset!" nur,
            // solange ein Feld hinter der Auswahl noch NICHT ausgewertet ist
            // (SET_WALKING mit "true" als <str> direkt nach dem Oeffnen);
            // nach "Re-Evaluate" heisst er dort auch "Revert". Bei uns ist
            // jedes Feld schon beim Oeffnen ausgewertet (Klappliste true/
            // false) - es gibt diesen Zwischenzustand nicht.
            const Str beschriftung =
                isExpr ? Str::EditorRevert
                       : (knopfArt == FeldKnopf::Helfer ? Str::EditorHelper
                                                        : Str::EditorExpr);
            if (ImGui::Button(tr(beschriftung), ImVec2{knopfB, 0.0F})) {
                if (i >= g_app->editorIsExpr.size()) {
                    g_app->editorIsExpr.resize(fields.size(), 0);
                }
                if (isExpr) {
                    g_app->editorIsExpr[i] = 0;
                } else {
                    g_app->editorIsExpr[i] = 1;
                }
            }
        };
        if (p.kind == Param::Kind::TypeSet && !isExpr) {
            const TypeSet* ts = g_app->db.typeset(p.typeset);
            // Tippbar, nicht nur waehlbar: Movie Duels bringt eigene
            // Animationen mit, die in Ravens Kopfdateien nicht stehen.
            const ComboEditResult r =
                comboEdit("##v", v, ts != nullptr ? &ts->entries : nullptr,
                          auswahlBreite(v));
            // NUR bei einer echten Auswahl die Folgefelder zuruecksetzen.
            //
            // Bei jedem Tastendruck waere falsch: das Zuruecksetzen holt die
            // Vorgabewerte und ueberschreibt damit auch dieses Feld. Der
            // getippte Text waere sofort wieder weg - genau das machte das
            // Feld unbenutzbar.
            if (r.picked) {
                g_app->editorRefreshFrom = static_cast<int>(i);
            }
            if (knopfArt != FeldKnopf::Keiner) {
                ImGui::SameLine(0, 0);
                feldKnopfZeichnen();
            }
            // Ein Wert, den die Typmenge nicht kennt, ist erlaubt - Movie
            // Duels braucht das fuer seine eigenen Animationen. Aber ein
            // Hinweis gehoert daneben: viel haeufiger ist es ein Tippfehler.
            //
            // Derselbe Fall, den der Pruefer als V004 meldet; hier steht er
            // sofort da statt erst beim Pruefen.
            if (ts != nullptr && !v.empty() && ts->find(v) == nullptr) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", tr(Str::MsgUnknownValue));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", tr(Str::MsgUnknownValueHint));
                }
            }
        } else {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", v.c_str());
            if (ImGui::InputText("##v", buf, sizeof(buf))) { v = buf; }
            // Eingabe bestaetigt und Escape bricht ab - auch wenn der
            // Schreibzeiger noch im Feld steht.
            //
            // Der Knopftest (rc568) tippte in `print` und drueckte Eingabe:
            // der Editor blieb offen, der Wert kam nie ins Skript. Das Feld
            // holt sich beide Tasten selbst (`Shortcut(ImGuiKey_Enter, ..,
            // id)` in InputTextEx), die Route gehoert dann ihm, und das
            // `Shortcut` des Editors weiter unten geht leer aus. Man musste
            // zweimal druecken - ein Windows-Dialog und das Original
            // schliessen beim ersten Mal.
            //
            // Das Feld wird in genau dem Bild verlassen, in dem die Taste
            // kommt. `IsKeyPressed` fragt die Taste selbst, nicht die Route.
            if (ImGui::IsItemDeactivated()) {
                if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                    ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
                    feldEnter = true;
                } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                    feldEscape = true;
                }
            }
            ImGui::SameLine(0, 0);
            if (!p.filter.empty()) {
                if (ImGui::Button(tr(Str::EditorBrowse))) { durchsuchen(p, v); }
                ImGui::SameLine(0, 0);
                if (ImGui::Button(tr(Str::EditorPlay))) { abspielen(p, v); }
                ImGui::SameLine(0, 0);
            }
            // Zielnamen aus der Karte anbieten. Bei SET_CAMERA_GROUP sind
            // es die Kameragruppen - dieselbe Herkunft, andere Spalte.
            if (kartenListe != nullptr) {
                const std::vector<std::string>& list = *kartenListe;
                if (ImGui::ArrowButton("##map", ImGuiDir_Down)) {
                    ImGui::OpenPopup("mappick");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", tr(Str::MapPick));
                }
                if (ImGui::BeginPopup("mappick")) {
                    for (const std::string& t : list) {
                        if (ImGui::Selectable(t.c_str())) {
                            v = t;
                        }
                    }
                    ImGui::EndPopup();
                }
                ImGui::SameLine(0, 0);
            }
            // FESTE Breite, dieselbe wie in den Helferzeilen.
            //
            // shank: "die sind einen Ticken zu lang, dass nicht alles sauber
            // untereinander ist."
            //
            // Die Helferknoepfe (Get, Tag, Rnd) werden mit `ImVec2{knopfW,
            // 0}` gezeichnet und enden damit genau an der Spaltenkante.
            // Dieser hier nahm seine EIGENE Textbreite - "Revert" ist
            // schmaler als das Maximum, also endete die Feldzeile ein paar
            // Punkte frueher als die Helferzeilen darunter.
            //
            // `knopfB` ist bereits das Maximum ueber Get, Tag, Rnd, Expr!
            // und Revert - dieselbe Zahl, aus der auch die Feldbreite
            // folgt. Sie hier auch zu BENUTZEN schliesst die Luecke.
            // `editorIsExpr[i]` ist seit rc465 die einzige Wahrheit.
            // "Helper" stand bis rc568 nur an Dateifeldern; das Original
            // setzt ihn an JEDES Feld hinter einer fuehrenden Klappliste
            // (declare, set, sound, play) - siehe feldKnopf.
            if (knopfArt != FeldKnopf::Keiner) { feldKnopfZeichnen(); }
            // Die rechte Kante der Feldzeile, gemessen. Nur Spalte 0 -
            // mehr braucht der Vergleich nicht.
            if (i == 0) {
                g_app->dbgFeldEnde = ImGui::GetItemRectMax().x;
            }
        }
        // Typangabe unter dem Feld, mittig - so steht es im Original.
        // Ist das Feld auf Ausdruck umgestellt, zeigt es zusaetzlich, was es
        // vorher war: "<expr> ..... was <str>".
        std::string hint = hintFor(p);
        // Ein Textfeld der Signatur, in dem im Skript eine Zahl oder ein
        // Vektor steht, zeigt dessen Art - wie das Original bei
        // set ( "SET_HEALTH", 5 ) ohne Markierung: "<int>" unter der 5,
        // "<vec>" unter < 1 2 3 >. Gespeichert wird ohnehin in dieser Art
        // (argForParam uebernimmt sie vom alten Argument).
        if (p.kind == Param::Kind::String && !g_app->editorInsert) {
            const Node* alt = nodeAt(g_app->doc.script(), g_app->editorPath);
            if (alt != nullptr && i < alt->args.size()) {
                const Arg& a = alt->args[i];
                if (a.kind == Arg::Kind::Vector) {
                    hint = "<vec>";
                } else if (a.kind == Arg::Kind::Number) {
                    hint = a.text.find('.') == std::string::npos ? "<int>" : "<float>";
                }
            }
        }
        if (i < g_app->editorIsExpr.size() && g_app->editorIsExpr[i] != 0) {
            hint = std::string("<expr> ") + tr(Str::EditorWas) + " " + hint;
        }
        const float hw = ImGui::CalcTextSize(hint.c_str()).x;
        ImGui::SetCursorPosX(x + (dx(kEdFieldW) - hw) * 0.5F);
        ImGui::TextDisabled("%s", hint.c_str());
        ImGui::EndGroup();

        if (i + 1 < fields.size()) {
            ImGui::SameLine(0, dx(kEdGap));
        } else {
            // Nach dem LETZTEN Feld noch etwas Luft nach rechts.
            //
            // Gewuenscht: "maybe adjust the width of each box slightly, also
            // have small padding on the right." Ohne das endet die Reihe
            // buendig an der Fensterkante, und es sieht abgeschnitten aus -
            // links steht die Beschriftungsspalte, rechts nichts.
            //
            // Der Abstand zwischen den Spalten kommt jetzt von der
            // Tabelle - das leere Element dafuer ist ueberfluessig.
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();
    // Jetzt ist die Reihe gezeichnet und laesst sich messen - NUR zum
    // Vergleich mit der Rechnung, nichts liest den Wert zurueck. Weicht
    // beides ab, steht die Reihe schief, aber sie schwingt nicht.
    g_app->editorRowW = ImGui::GetItemRectMax().x - reiheAb;
    {
        static float letzteAbweichung = 0.0F;
        const float abw = g_app->editorRowW - reiheW;
        if (std::fabs(abw - letzteAbweichung) > 0.5F) {
            letzteAbweichung = abw;
            char zr[200];
            std::snprintf(zr, sizeof(zr),
                          "Ereigniseditor Reihe \"%s\": gerechnet %.0f, "
                          "gemessen %.0f, Abweichung %.0f",
                          c.name.c_str(), static_cast<double>(reiheW),
                          static_cast<double>(g_app->editorRowW),
                          static_cast<double>(abw));
            diag::detail(zr);
        }
    }

    // --- Zweite ZEILE derselben Tabelle: die Helfer ---------------------
    //
    // Vorher war das eine eigene Tabelle (rc488) unter einer von Hand
    // gesetzten Feldreihe. Zwei Tabellen koennen sich nicht ausrichten;
    // zwei ZEILEN einer Tabelle koennen gar nicht anders.
    if (edSpalten > 0) {
        bool welche = false;
        for (std::size_t i = 0; i < g_app->editorIsExpr.size(); ++i) {
            if (g_app->editorIsExpr[i] != 0) { welche = true; break; }
        }
        if (welche) {
            ImGui::TableNextRow();
            for (int k = 0; k < edSpalten; ++k) {
                ImGui::TableNextColumn();
                const std::size_t idx = static_cast<std::size_t>(k);
                if (idx >= g_app->editorIsExpr.size() ||
                    idx >= g_app->editorValues.size() ||
                    g_app->editorIsExpr[idx] == 0) {
                    continue;
                }
                drawHelperRows(idx, &g_app->editorValues[idx],
                               feldKnopf(c, idx) == FeldKnopf::Helfer ? &fields[idx]
                                                                      : nullptr);
            }
        }
        ImGui::EndTable();

        // --- Die Kanten ins Protokoll ---------------------------------
        //
        // Hier, NICHT in `drawHelperRows`: dort lief die Zeile nur, wenn
        // ausgerechnet Spalte 0 aufgeklappt war. shanks Protokoll hatte
        // deshalb null Editorzeilen - er hatte den Helfer der zweiten
        // Spalte geoeffnet.
        //
        // An dieser Stelle stehen beide Kanten fest, egal welche Spalte
        // offen ist.
        {
            const float diff = g_app->dbgHelferEnde - g_app->dbgFeldEnde;
            if (g_app->dbgHelferEnde > 0.0F &&
                std::abs(diff - g_app->dbgLetzteDiff) > 0.5F) {
                g_app->dbgLetzteDiff = diff;
                char zed[220];
                std::snprintf(zed, sizeof(zed),
                              "Ereigniseditor Spalte 0: Helfer %d/%d, "
                              "Zelle %.0f, "
                              "bestellt %.0f, knopfB "
                              "%.0f, knopfW %.0f, FeldEnde %.0f, HelferEnde "
                              "%.0f, Differenz %.0f",
                              // Steht der Ausdruckshelfer offen, und wie
                              // viele Felder kennt der Editor gerade?
                              //
                              // shanks rc566-Protokoll, einfeldriges `if`,
                              // EIN Oeffnen:
                              //
                              //   Zelle 408  bestellt 408   eingeschwungen
                              //   Zelle 313  bestellt 314   <- springt
                              //   gewachsen 459x415 -> 412x263
                              //
                              // 408 ist `Zeichenhoehe * 17` - der Beitrag
                              // des Helferblocks. 415-263 = 152 sind seine
                              // vier Zeilen. Er verschwindet also MITTEN im
                              // Oeffnen, und mit ihm die bestellte Breite.
                              //
                              // Warum, sagt keine Zahl im Protokoll:
                              // `editorIsExpr` wird nur vom Expr!/Revert-
                              // Knopf gesetzt, und der wurde nicht
                              // gedrueckt. Diese zwei Zahlen sagen es.
                              (!g_app->editorIsExpr.empty() &&
                               g_app->editorIsExpr[0] != 0) ? 1 : 0,
                              static_cast<int>(g_app->editorIsExpr.size()),
                              static_cast<double>(g_app->dbgZelle0),
                              static_cast<double>(g_app->dbgBestellt0),
                              static_cast<double>(g_app->dbgKnopfB),
                              static_cast<double>(g_app->dbgKnopfW),
                              static_cast<double>(g_app->dbgFeldEnde),
                              static_cast<double>(g_app->dbgHelferEnde),
                              static_cast<double>(diff));
                diag::detail(zed);
            }
        }
    }

    // Hilfetext: die Beschreibung des gewaehlten Eintrags, sonst die des
    // Befehls. Beides steht als '#'-Kommentar in .bhc bzw. Kopfdatei.
    ImGui::Dummy(ImVec2{0, dy(10)});
    // Ohne den willkuerlichen Versatz von 8 Punkten, der hier stand: er zog
    // die Zeile nach links, ohne dass etwas dafuer sprach. Mittig heisst
    // mittig, und die Untergrenze ist der Fensterrand - dieselbe Regel wie
    // bei der Knopfzeile.
    //
    // Der Text traegt seine eigenen Klammern; die zaehlen bei CalcTextSize
    // nicht mit, weil sie erst im Format dazukommen. Deshalb wird die
    // Breite MIT Klammern gemessen.
    //
    // Gemittelt gegen `fensterW` - dieselbe ausgerechnete Breite wie die
    // Reihe, nicht `GetWindowWidth()` des laufenden Fensters.
    ImGui::SetCursorPosX(std::max((fensterW - breiteHilfe) * 0.5F, linksrand));
    ImGui::Text("( %s )", help.c_str());

    // Kein fester Abstand mehr. Die erste Fassung setzte hier
    // dy(kEdBtnRow - 40) - ein Mass aus Dialog 137, das fuer MS Sans Serif
    // 8pt gilt. Mit unserer groesseren Schrift schob es die Knopfzeile aus
    // dem Fenster, und man sah von "Ok" nur noch die obere Haelfte.
    ImGui::Dummy(ImVec2{0, ImGui::GetTextLineHeight()});
    ImGui::Separator();
    ImGui::Dummy(ImVec2{0, ImGui::GetStyle().ItemSpacing.y});

    // Knopfzeile mittig unter den Feldern, wie im Original.
    // Eingabe bestaetigt, Escape bricht ab - wie in jedem Dialog.
    //
    // Die Tasten werden VOR der Cursorverschiebung geprueft, und Escape
    // verlaesst die Funktion NICHT vorzeitig.
    //
    // Der erste Anlauf tat beides falsch: erst SetCursorPosX, dann bei
    // Escape sofort EndPopup und return. Damit war der Cursor verschoben,
    // ohne dass ein Element abgeschickt wurde - und ImGui bricht mit einer
    // Zusicherung ab:
    //
    //     Code uses SetCursorPos() to extend window/parent boundaries.
    //     Please submit an item e.g. Dummy() afterwards.
    //
    // Das Fenster wird jetzt regulaer fertig gezeichnet; geschlossen wird
    // erst am Ende.
    // Shortcut() statt IsKeyPressed().
    //
    // Der Unterschied steht in imgui.h: IsKeyPressed hat KEINE Nebenwirkung,
    // Shortcut meldet eine Route an - "several callers may register interest
    // in a shortcut, and only one owner gets it".
    //
    // Das loest genau das Problem, das ich vorher mit einem Flag umgangen
    // habe: der Editor schloss mit Eingabe, und handleTreeKeys sah dieselbe
    // Taste im selben Bild und oeffnete ihn wieder. Mit einer Route bekommt
    // das Fenster mit dem Fokus die Taste, und der Baum sieht sie nicht.
    const bool enterPressed = ImGui::Shortcut(ImGuiKey_Enter) ||
                              ImGui::Shortcut(ImGuiKey_KeypadEnter) ||
                              feldEnter;
    const bool escPressed = ImGui::Shortcut(ImGuiKey_Escape) || feldEscape;

    // Untergrenze ist der FENSTERRAND, nicht die Beschriftungsspalte.
    //
    // Vorher stand hier dx(kEdLeft) - der Abstand, an dem die Eingabefelder
    // beginnen. Bei einem schmalen Fenster (print, use, kill, free, signal:
    // ein einziges Feld) liegt die mittige Stelle DAVOR, und die Grenze
    // schob die Knopfzeile genau dorthin, wo das Feld anfaengt. Deshalb sass
    // sie rechts der Mitte - und zwar umso weiter, je schmaler das Fenster.
    //
    // Bei breiten Befehlen wie camera(PAN) fiel es nicht auf, weil die
    // Mitte dort ohnehin rechts von der Beschriftungsspalte liegt.
    //
    // Der Rand ist die richtige Grenze: er sagt nur "nicht aus dem Fenster
    // heraus" und mischt sich sonst nicht ein.
    ImGui::SetCursorPosX(std::max((fensterW - breiteKnoepfe) * 0.5F, linksrand));

    if (ImGui::Button(tr(Str::EditorOk), ImVec2{btnW, 0.0F}) || enterPressed) {
        Node n;
        n.kind = Node::Kind::Command;
        n.name = c.name;
        n.hasBlock = c.block;
        for (std::size_t i = 0; i < fields.size() && i < g_app->editorValues.size(); ++i) {
            if (i < g_app->editorIsExpr.size() && g_app->editorIsExpr[i] != 0) {
                Arg a;
                a.kind = Arg::Kind::Expr;
                a.text = g_app->editorValues[i];
                n.args.push_back(a);
                continue;
            }
            // Das bisherige Argument mitgeben, sonst schreibt der Editor
            // Schreibweisen um, die der Nutzer gar nicht angefasst hat -
            // genau das, was tests/edittour.cpp fuer den Kern ausschliesst.
            const Node* old = g_app->editorInsert
                                  ? nullptr
                                  : nodeAt(g_app->doc.script(), g_app->editorPath);
            // Das alte Argument NUR mitgeben, wenn der Ausdrucksmodus
            // unveraendert ist.
            //
            // `argForParam` liefert bei gleichem Text das alte Argument
            // unveraendert zurueck - samt seiner Art. Das ist richtig und
            // Absicht: ein Editor darf beim blossen Oeffnen und Schliessen
            // nichts umschreiben.
            //
            // Ein Druck auf "Revert" IST aber ein Anfassen. Der Text
            // bleibt dabei derselbe, also kam das alte Argument mit
            // `Kind::Expr` zurueck, und beim naechsten Oeffnen stand wieder
            // "Expr!" da.
            //
            // shank zu rc551: "if I click the 'Expr!' button and then the
            // 'Revert' button, it works as expected. But if I close the
            // window and open it again, the 'Expr!' button is active
            // despite clicking 'Revert' before."
            //
            // Hat er den Modus umgestellt, wird das Argument neu gebaut.
            // Sonst bleibt alles beim Alten.
            const bool modusGeaendert =
                i < g_app->editorWarExpr.size() &&
                i < g_app->editorIsExpr.size() &&
                g_app->editorWarExpr[i] != g_app->editorIsExpr[i];
            const Arg* prev = (old != nullptr && i < old->args.size() &&
                               !modusGeaendert)
                                  ? &old->args[i]
                                  : nullptr;
            n.args.push_back(argForParam(fields[i], g_app->editorValues[i],
                                         g_app->db, prev));
        }
        if (g_app->editorInsert) {
            if (g_app->doc.insertAfter(g_app->selectedPath, n)) {
                if (!g_app->selectedPath.empty()) {
                    ++g_app->selectedPath.back();   // Auswahl auf das Neue
                }
                rebuildTree();
            }
        } else if (g_app->doc.replaceAt(g_app->editorPath, n)) {
            rebuildTree();
        }
        // Und was er GESCHRIEBEN hat. Zusammen mit der Zeile beim Oeffnen
        // laesst sich damit sagen, ob ein Wert unterwegs verlorengeht.
        {
            std::string z = "Editor Ok \"" + n.name + "\":";
            for (std::size_t i = 0; i < n.args.size(); ++i) {
                z += " [" + std::to_string(i) + "] \"" + n.args[i].text +
                     "\"" +
                     (n.args[i].kind == Arg::Kind::Expr ? " (Ausdruck)" : "");
            }
            diag::detail(z);
        }
        helferInEinstellungen(c.name);
        g_app->editorOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::EditorCancel), ImVec2{btnW, 0.0F}) || escPressed) {
        // Auch beim Abbrechen merken.
        //
        // Die Helferzeilen aendern das Skript nicht - es gibt also nichts
        // abzubrechen. Wer sie einstellt und dann doch Abbrechen drueckt,
        // soll seine Wahl beim naechsten Oeffnen trotzdem vorfinden.
        helferInEinstellungen(c.name);
        g_app->editorOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    // "Neu auswerten": alle Felder hinter der ersten Auswahl auf die
    // Vorgabewerte zuruecksetzen, die zur aktuellen Wahl gehoeren. Vorher
    // war der Knopf dauerhaft ausgegraut - genau das, was er tun soll,
    // passierte nirgends.
    if (ImGui::Button(tr(Str::EditorReEvaluate), ImVec2{btnW, 0.0F})) {
        g_app->editorRefreshFrom = 0;
        g_app->editorForceRefresh = true;
        // Auch den Ausdrucksmodus und die Helferzeilen zuruecknehmen.
        //
        // rc544 setzte nur die WERTE zurueck. Auf shanks Bild steht danach
        // weiter "Revert" am Feld, und darunter FLOAT / SET_PARM1 /
        // ORIGIN / 0.000 .. 1.000 - der Befehl sieht also unveraendert
        // aus, obwohl der Wert zurueckgesetzt wurde.
        //
        // ABER den Ausdrucksmodus stehen lassen.
        //
        // rc545 hat ihn mit zurueckgenommen ("wie frisch eingefuegt"). Das
        // war zu viel: damit klappen die Helferzeilen zu, und wer sie
        // gerade benutzt, verliert seine Ansicht.
        //
        // shank zu rc545: "wenn ich re evaluate druecke springt das fenster
        // wieder zurueck in die form obwohl es vorher aufgeklappt war, das
        // sollte nicht passieren - es sollte die Werte zuruecksetzen sich
        // aber nicht zuklappen."
        //
        // Also nur die WERTE: das Feld selbst (oben) und die Helferzeilen
        // darunter. `helferGross` fuellt gleich wieder mit den Vorgaben
        // FLOAT / SET_PARM1 / ORIGIN / 0.0 .. 1.0 nach - sichtbar, offen,
        // zurueckgesetzt.
        g_app->helpGetType.clear();
        g_app->helpGetName.clear();
        g_app->helpTagName.clear();
        g_app->helpTagType.clear();
        g_app->helpRangeLow.clear();
        g_app->helpRangeHigh.clear();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::EditorReEvalHint));
    }

    ImGui::EndPopup();
}

// --- Suchen (Dialog 140 des Originals) ---------------------------------

// Die Suche - ein Fenster, das OFFEN BLEIBT.
//
// Vorher war es ein modaler Dialog, der beim ersten Treffer zuging. Das ist
// genau das, was beim Suchen stoert: man will den zweiten und dritten
// Treffer sehen, ohne das Fenster jedes Mal neu zu oeffnen.
//
// Notepad++ macht es so, und daran halte ich mich: das Fenster bleibt
// stehen, hat "Weiter" und "Zurueck", und F3 / Umschalt+F3 tun dasselbe.
// Auch der Rest ist von dort uebernommen - Ctrl+F setzt den Schreibzeiger
// zurueck ins Suchfeld, wenn das Fenster schon offen ist, und eine
// erfolglose Suche wird angezeigt statt verschluckt.
// "Find similar": das Suchfenster oeffnen und mit Name und Wert fuellen.
void sucheVorbelegen(const std::string& name, const std::string& text) {
    g_app->findVorName = name;
    g_app->findVorText = text;
    g_app->findVorbelegen = true;
    fensterZeigen(g_app->findOpen, "###find");
}

void drawFind() {
    if (!g_app->findOpen) {
        return;
    }
    // KEIN modaler Dialog mehr: waehrend gesucht wird, soll man im Baum
    // weiterarbeiten koennen.
    ImGui::SetNextWindowSize(ImVec2{dx(230), 0.0F}, ImGuiCond_FirstUseEver);
    const std::string titel = std::string(tr(Str::ActFind)) + "###find";
    if (!ImGui::Begin(titel.c_str(), &g_app->findOpen,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    static char named[128];
    static char contains[128];
    static char ersatz[128];
    // Der Selbsttest setzt den Ersatztext von aussen.
    if (!g_app->findErsatz.empty() && g_app->findErsatz != ersatz) {
        std::snprintf(ersatz, sizeof(ersatz), "%s", g_app->findErsatz.c_str());
    }
    // "Find similar": die Angaben des Befehls eintragen und gleich zaehlen,
    // damit "k of N" sofort dasteht und F3 weitersucht.
    if (g_app->findVorbelegen) {
        g_app->findVorbelegen = false;
        std::snprintf(named, sizeof(named), "%s", g_app->findVorName.c_str());
        std::snprintf(contains, sizeof(contains), "%s", g_app->findVorText.c_str());
        g_app->findWhole = true;
        g_app->findLetzte.named = named;
        g_app->findLetzte.contains = contains;
        g_app->findLetzte.wholeString = true;
        g_app->findHits = findAll(g_app->doc.script(), g_app->findLetzte);
        g_app->findAt = 0;
        for (std::size_t h = 0; h < g_app->findHits.size(); ++h) {
            if (g_app->findHits[h] == g_app->selectedPath) {
                g_app->findAt = static_cast<int>(h);
                break;
            }
        }
    }

    // Ctrl+F bei offenem Fenster: zurueck ins Suchfeld.
    const bool nochmalGeoeffnet =
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F);
    if (ImGui::IsWindowAppearing() || nochmalGeoeffnet) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(dx(160));
    const bool enterInName = ImGui::InputText(
        tr(Str::FindNamed), named, sizeof(named),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SetNextItemWidth(dx(160));
    const bool enterInText = ImGui::InputText(
        tr(Str::FindContains), contains, sizeof(contains),
        ImGuiInputTextFlags_EnterReturnsTrue);
    // --- Ersetzen, wie Notepad++ ----------------------------------------
    //
    // shank: "in the find window, let us replace the item containing text.
    // Would be useful for cameras, as some are reused throughout the
    // cutscene". Ersetzt wird genau, was "Item containing" findet.
    ImGui::SetNextItemWidth(dx(160));
    if (ImGui::InputText(tr(Str::FindReplaceWith), ersatz, sizeof(ersatz))) {
        g_app->findErsatz = ersatz;
    }
    ImGui::Checkbox(tr(Str::FindWhole), &g_app->findWhole);
    ImGui::SameLine();
    ImGui::Checkbox(tr(Str::FindInSelection), &g_app->findInSelection);

    // Neu suchen - wenn sich die Eingabe geaendert hat oder man es verlangt.
    // Die Angaben aus dem Fenster uebernehmen - gesucht wird in findNext.
    auto angabenMerken = [&]() {
        g_app->findLetzte.named = named;
        g_app->findLetzte.contains = contains;
        g_app->findLetzte.wholeString = g_app->findWhole;
    };

    // Hoehe 0: die Hoehe, die zum Text passt.
    //
    // dy(kEdFieldH) ist fuer EINGABEFELDER gedacht, wo sie zur Zeilenhoehe
    // stimmen muss. An einem Knopf faellt sie bei 144 dpi zu hoch aus - im
    // Suchfenster faellt es nicht auf, weil man es ziehen kann, im
    // Nachfragedialog schnitt sie die Knoepfe ab. Hier gleichgezogen, damit
    // beide Fenster dieselbe Knopfhoehe haben.
    // Alle Knoepfe gleich breit - so breit wie die laengste Beschriftung,
    // mindestens kEdOkW. Mit fester Breite stand bei groesserer Schrift
    // "Find previou" da.
    float knopfB = dx(kEdOkW);
    for (const Str s : {Str::FindNextBtn, Str::FindPrevBtn, Str::FindClose,
                        Str::FindReplaceBtn, Str::FindReplaceAll}) {
        knopfB = std::max(knopfB, ImGui::CalcTextSize(tr(s)).x +
                                      ImGui::GetStyle().FramePadding.x * 2.0F);
    }
    // F3 / Umschalt+F3 (bzw. was darauf belegt ist) auch IM Suchfeld, wie
    // in Notepad++. Ist ein Feld aktiv, sperrt handleShortcuts alle
    // Kuerzel (WantTextInput) - dann uebernimmt das Fenster selbst. Ohne
    // aktives Feld tut es handleShortcuts; so wirkt der Druck genau einmal.
    const bool imFeld = ImGui::GetIO().WantTextInput &&
                        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const ImGuiKeyChord f3 = chordFor(keys::Action::FindRepeat);
    const ImGuiKeyChord f3zurueck = chordFor(keys::Action::FindPrevious);
    const bool f3weiter = imFeld && f3 != ImGuiKey_None && ImGui::IsKeyChordPressed(f3);
    const bool f3rueck = imFeld && f3zurueck != ImGuiKey_None && ImGui::IsKeyChordPressed(f3zurueck);
    const bool weiter =
        ImGui::Button(tr(Str::FindNextBtn), ImVec2{knopfB, 0.0F}) ||
        enterInName || enterInText || f3weiter;
    ImGui::SameLine();
    const bool zurueck =
        ImGui::Button(tr(Str::FindPrevBtn), ImVec2{knopfB, 0.0F}) || f3rueck;
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::FindClose), ImVec2{knopfB, 0.0F})) {
        g_app->findOpen = false;
    }

    // Die Ersetzen-Knoepfe. Ohne "Item containing" gibt es nichts zu
    // ersetzen - dann grau.
    ImGui::BeginDisabled(contains[0] == '\0');
    const bool einen =
        ImGui::Button(tr(Str::FindReplaceBtn), ImVec2{knopfB, 0.0F});
    ImGui::SameLine();
    const bool alle =
        ImGui::Button(tr(Str::FindReplaceAll), ImVec2{knopfB, 0.0F});
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tr(Str::FindReplaceHint));
    }
    const bool alleDocs = ImGui::Button(tr(Str::FindReplaceAllDocs));
    ImGui::EndDisabled();
    if (einen || alle || alleDocs) {
        g_app->findLetzte.named = named;
        g_app->findLetzte.contains = contains;
        g_app->findLetzte.wholeString = g_app->findWhole;
        g_app->findErsatz = ersatz;
        if (einen) {
            (void)ersetzeEinen();
        } else {
            (void)ersetzeAlle(alleDocs);
        }
    }

    // Was gerade gilt - so sieht man sofort, ob es Treffer gibt und wo man
    // steht. Notepad++ blinkt stattdessen; eine Zeile ist deutlicher.
    if (g_app->findHits.empty()) {
        ImGui::TextDisabled("%s", tr(Str::FindNone));
    } else {
        ImGui::Text(tr(Str::FindAtOf), g_app->findAt + 1,
                    static_cast<int>(g_app->findHits.size()));
    }

    if (weiter || zurueck) {
        angabenMerken();
        findNext(zurueck ? -1 : 1);
    }
    ImGui::End();
}

// Zum naechsten Treffer springen - F3 im Original.
// Zum aktuellen Treffer springen - SICHTBAR.
//
// Herausgezogen, weil es jetzt drei Aufrufer gibt: die erste Suche, Weiter
// und Zurueck. Ohne die Mehrfachauswahl und scrollToSelected sieht man
// nichts davon, dass etwas gefunden wurde - genau der Fehler aus rc145.
void springeZuTreffer() {
    if (g_app->findHits.empty()) {
        return;
    }
    selectByPath(g_app->findHits[static_cast<std::size_t>(g_app->findAt)]);
}

// Weitersuchen wie im Original: jedes Mal FRISCH und AB DER AUSWAHL.
//
// Am laufenden BehavEd.exe beobachtet: von Zeile 6, 9, 12 aus springt es
// jeweils zum naechsten Treffer DAHINTER, vom Ende aus zum ersten
// (Umlauf). Umschalt+F3 entsprechend rueckwaerts.
//
// Vorher begann behaved immer beim ersten Treffer und lief dann eine
// einmal gemerkte Liste ab. Nach einer Aenderung am Skript zeigte die auf
// alte Stellen.
//
// Wege vergleichen wie die Reihenfolge im Baum: [1] < [1,0] < [2].
void findNext(int step) {
    const FindOptions& o = g_app->findLetzte;
    if (o.named.empty() && o.contains.empty()) {
        fensterZeigen(g_app->findOpen, "###find");   // noch nichts gesucht: erst fragen
        return;
    }
    g_app->findHits = findAll(g_app->doc.script(), o);
    if (g_app->findHits.empty()) {
        addStatus(tr(Str::FindNone));
        return;
    }
    const std::vector<Path>& h = g_app->findHits;
    const Path& ab = g_app->selectedPath;
    const int n = static_cast<int>(h.size());
    int ziel = -1;
    if (step >= 0) {
        for (int i = 0; i < n; ++i) {
            if (ab.empty() || ab < h[static_cast<std::size_t>(i)]) {
                ziel = i;
                break;
            }
        }
        if (ziel < 0) { ziel = 0; }          // Umlauf zum ersten
    } else {
        for (int i = n - 1; i >= 0; --i) {
            if (!ab.empty() && h[static_cast<std::size_t>(i)] < ab) {
                ziel = i;
                break;
            }
        }
        if (ziel < 0) { ziel = n - 1; }      // Umlauf zum letzten
    }
    g_app->findAt = ziel;
    springeZuTreffer();
}

// --- Tastenkuerzel, Tabelle aus ACCELERATOR 135 ------------------------

// Die Kennung des Knotens an einem Weg.
//
// Vergibt vorher, falls noetig: ein GERADE eingefuegter Knoten traegt noch
// 0, und `setOpen(0)` taete nichts. Der Durchlauf ist billig und aendert
// nichts, wenn schon alles vergeben ist.
// Sagen, WARUM eine Bewegung nichts getan hat - auf dem Schirm, nicht nur
// im Protokoll.
//
// shanks rc535-Protokoll: `Baum: "else" steht schon am Ende - nichts
// getan`. Er hat gezogen, es passierte nichts, und der Grund stand in
// einer Datei, die er beim Arbeiten nicht sieht.
//
// Handout Abschnitt 4: ein Werkzeug, das bei einer sinnlosen Geste einfach
// nichts tut, laesst den Benutzer glauben, es sei kaputt.
//
// `Keine` schweigt mit Absicht: nicht jede fehlgeschlagene Bewegung hat
// einen Grund, den man dem Benutzer erklaeren kann.
void zeigeAbweisung() {
    switch (g_app->doc.letzteAbweisung()) {
        case Document::Abweisung::SchonAmEnde:
            addStatus(tr(Str::MoveNoEnd));
            break;
        case Document::Abweisung::SchonGanzOben:
            addStatus(tr(Str::MoveNoTop));
            break;
        case Document::Abweisung::ZielGleichHerkunft:
            addStatus(tr(Str::MoveNoSame));
            break;
        case Document::Abweisung::ZielImGezogenen:
            addStatus(tr(Str::MoveNoSelf));
            break;
        case Document::Abweisung::ZielIstKeinBlock:
            addStatus(tr(Str::MoveNoBlock));
            break;
        case Document::Abweisung::Keine:
        case Document::Abweisung::WegUngueltig:
            break;
    }
}

// --- Nach dem Verschieben das Verschobene markieren ----------------------
//
// shank, 27.09.: "The command(s) I moved should probably be highlighted.
// Otherwise, I lose track of what I just moved." Bis rc568 setzte das
// Ablegen die Auswahl auf die ANKERZEILE (r.path) und leerte die
// Mehrfachauswahl - markiert war danach etwas anderes als das Gezogene.
//
// Wege taugen dafuer nicht, sie aendern sich durch die Bewegung selbst.
// Die Kennung bleibt (Document::vergibKennungen) - also vorher die
// Kennungen merken, nachher ihre neuen Wege suchen.
// (kennungenVon / markiereKennungen: app_internal.h, Definition am Dateiende.)

[[nodiscard]] Kennung kennungFuer(const Path& p) {
    if (p.empty()) {
        return 0;
    }
    g_app->doc.vergibKennungen();
    const Node* n = nodeAt(g_app->doc.script(), p);
    return n != nullptr ? n->kennung : 0;
}

void handleTreeKeys() {
    // Nur wenn kein Dialog offen ist und nichts getippt wird - sonst
    // wandert die Auswahl, waehrend man einen Wert eingibt.
    //
    // dialogAtFrameStart: siehe handleShortcuts. Auch die Pfeiltasten
    // sollen nicht in dem Bild wandern, in dem ein Dialog schliesst.
    if (g_app->dialogAtFrameStart || g_app->findAtFrameStart ||
        g_app->editorOpen || g_app->pk3Open ||
        g_app->prefsOpen || g_app->aboutOpen || g_app->missionPickOpen ||
        g_app->findOpen || g_app->askSaveOpen || g_app->frageOffen ||
        ImGui::GetIO().WantTextInput || g_app->tlUeberMaus) {
        return;
    }
    // Mit ALT gehoeren die Pfeiltasten NICHT hierher.
    //
    // Alt+Pfeil verschiebt Befehle (Str::ActMoveUp/-Down). Ohne diese
    // Abfrage feuerte zusaetzlich die blanke Pfeiltaste, und die setzt die
    // Auswahl ueber selectRow(..., ctrl=false, ...) auf EINE Zeile zurueck.
    // Danach verschob das Kuerzel nur noch diese eine - genau der Fehler,
    // den die Gemeinschaft gemeldet hat: "It skips lines and doesn't work
    // if you have multiple commands selected. Clicking the actual Move up
    // and Move down buttons works perfectly."
    //
    // Der Knopf rief immer schon dasselbe moveSelection() auf. Der
    // Unterschied lag nie im Verschieben, sondern darin, dass die
    // Tastenfassung sich vorher selbst die Auswahl zerschossen hat.
    const bool altGedrueckt = ImGui::GetIO().KeyAlt;
    if (!altGedrueckt && ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        moveSelectionByKey(1);
    }
    if (!altGedrueckt && ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        moveSelectionByKey(-1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true)) {
        moveSelectionByKey(10);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true)) {
        moveSelectionByKey(-10);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
        moveSelectionByKey(-static_cast<int>(g_app->rows.size()));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
        moveSelectionByKey(static_cast<int>(g_app->rows.size()));
    }
    // Links und rechts klappen einen Block zu und auf - wie in jedem Baum.
    if (!g_app->selectedPath.empty()) {
        const Node* n = nodeAt(g_app->doc.script(), g_app->selectedPath);
        if (n != nullptr && n->hasBlock) {
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
                g_app->expanded.setOpen(kennungFuer(g_app->selectedPath), true);
                rebuildTree();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
                g_app->expanded.setOpen(kennungFuer(g_app->selectedPath), false);
                rebuildTree();
            }
        }
    }
    // Ebenfalls ueber eine Route: so kann der Editor sie beanspruchen,
    // wenn er offen ist, ohne dass hier ein Flag noetig waere.
    if (ImGui::Shortcut(ImGuiKey_Enter) && !g_app->selectedPath.empty()) {
        openEditorForNode(g_app->selectedPath);
    }
}

// --- Tastenbelegung ----------------------------------------------------
//
// Ein ImGuiKeyChord ist Taste und Zusatztasten in EINER Zahl
// (ImGuiMod_Ctrl | ImGuiKey_S). Genau in dieser Form wird sie auch
// gespeichert - imgui.h sagt zu GetKeyName ausdruecklich: "not meant to be
// saved persistently nor compared".
//
// Die Vorgaben kommen, wo es sie gibt, aus ACCELERATOR 135 des Originals.
// Was Raven nicht hatte, steht darunter und ist als Zutat gekennzeichnet.
ImGuiKeyChord defaultChord(keys::Action a) {
    // Find auf Strg+F - VOR der Tabelle des Originals. Die Begruendung steht
    // unten beim switch; dort war sie wirkungslos, weil die Tabelle
    // (ACCELERATOR 135: Strg+F3) zuerst griff. Gemeldet: shank wollte Find
    // selbst auf Strg+F legen, und nach "Reset all to defaults" stand
    // wieder Strg+F3 da.
    if (a == keys::Action::Find) {
        return ImGuiMod_Ctrl | ImGuiKey_F;
    }
    struct Bind { ImGuiKey imgui; int vk; };
    static const Bind kVk[] = {
        {ImGuiKey_Delete, 0x2E}, {ImGuiKey_Space, 0x20}, {ImGuiKey_Backspace, 0x08},
        {ImGuiKey_Enter, 0x0D},  {ImGuiKey_Insert, 0x2D}, {ImGuiKey_F3, 0x72},
        {ImGuiKey_A, 'A'}, {ImGuiKey_B, 'B'}, {ImGuiKey_C, 'C'}, {ImGuiKey_O, 'O'},
        {ImGuiKey_R, 'R'}, {ImGuiKey_S, 'S'}, {ImGuiKey_V, 'V'}, {ImGuiKey_X, 'X'},
        {ImGuiKey_T, 'T'},
        {ImGuiKey_KeypadAdd, 0x6B}, {ImGuiKey_KeypadSubtract, 0x6D},
    };
    for (const keys::Shortcut& sc : keys::table()) {
        if (sc.action != a) {
            continue;
        }
        for (const Bind& b : kVk) {
            if (b.vk != sc.key) {
                continue;
            }
            ImGuiKeyChord c = b.imgui;
            if (sc.mod == keys::Mod::Ctrl) { c |= ImGuiMod_Ctrl; }
            if (sc.mod == keys::Mod::Alt) { c |= ImGuiMod_Alt; }
            if (sc.mod == keys::Mod::Shift) { c |= ImGuiMod_Shift; }
            return c;
        }
    }
    // Nicht in der Ressource - unsere Zutaten.
    switch (a) {
        // Die Suche auf Ctrl+F.
        //
        // ACCELERATOR 135 des Originals sagt Ctrl+F3, und die Ressource
        // bleibt sonst ueberall die Vorlage. Hier nicht: Ctrl+F ist seit
        // dreissig Jahren die Suchtaste in Windows, und wer sie drueckt,
        // denkt nicht darueber nach. Ctrl+F3 ist in BehavEd ein Ueberbleibsel
        // aus einer Zeit, in der Ctrl+F noch nicht gesetzt war.
        //
        // keytest vergleicht weiterhin die TABELLE gegen die Ressource -
        // dort steht Ctrl+F3 unveraendert. Nur die wirkende Belegung ist
        // hier eine andere, und das ist eine bewusste Entscheidung, keine
        // Abweichung aus Versehen.
        case keys::Action::Find:     return ImGuiMod_Ctrl | ImGuiKey_F;
        case keys::Action::MoveUp:   return ImGuiMod_Alt | ImGuiKey_UpArrow;
        case keys::Action::MoveDown: return ImGuiMod_Alt | ImGuiKey_DownArrow;
        case keys::Action::Undo:     return ImGuiMod_Ctrl | ImGuiKey_Z;
        case keys::Action::Redo:     return ImGuiMod_Ctrl | ImGuiKey_Y;
        // Wie Notepad++.
        case keys::Action::BookmarkToggle: return ImGuiMod_Ctrl | ImGuiKey_F2;
        case keys::Action::BookmarkNext:   return ImGuiKey_F2;
        case keys::Action::BookmarkPrev:   return ImGuiMod_Shift | ImGuiKey_F2;
        // Wie in jedem Editor mit Reitern.
        case keys::Action::SaveAll: return ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S;
        default: return ImGuiKey_None;
    }
}

// Was JETZT gilt: Vorgabe, sofern die Einstellungen nichts anderes sagen.
ImGuiKeyChord chordFor(keys::Action a) {
    for (const keys::Bindable& b : keys::bindable()) {
        if (b.action != a) {
            continue;
        }
        for (const Settings::KeyBinding& kb : g_app->settings.keyBindings) {
            if (kb.action == b.name) {
                return kb.chord;   // 0 heisst ausdruecklich "keine Taste"
            }
        }
        break;
    }
    return defaultChord(a);
}

// Eine eigene Belegung eintragen oder wieder entfernen.
void setChord(keys::Action a, ImGuiKeyChord c) {
    const char* name = nullptr;
    for (const keys::Bindable& b : keys::bindable()) {
        if (b.action == a) { name = b.name; break; }
    }
    if (name == nullptr) {
        return;
    }
    auto& v = g_app->settings.keyBindings;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i].action != name) {
            continue;
        }
        if (c == defaultChord(a)) {
            v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));  // wieder Vorgabe
        } else {
            v[i].chord = c;
        }
        return;
    }
    if (c != defaultChord(a)) {
        v.push_back({name, c});
    }
}

// Auswahl eine Zeile hoch oder runter.
//
// Frei stehend, nicht mehr als Lambda im Knopfstreifen: seit "Move up" und
// "Move down" eine Taste haben (Alt+Pfeil), braucht auch die
// Tastenbehandlung sie.
// Einen Baumweg als "0/2/1" schreiben - die Ebene sieht man am Schraegstrich.
std::string wegAlsText(const std::vector<std::size_t>& w) {
    std::string t;
    for (std::size_t x : w) {
        if (!t.empty()) {
            t += "/";
        }
        t += std::to_string(x);
    }
    return t.empty() ? std::string("(oben)") : t;
}

void moveSelection(bool up) {
    const Path& sel = g_app->selectedPath;
        std::vector<Path> m = selectionOrCurrent();
        if (m.size() > 1) {
            if (g_app->doc.moveAll(m, up)) {
                g_app->selection = m;
                g_app->selectedPath = m.back();
                rebuildTree();
            }
            return;
        }
        // Die neue Stelle kommt VON der Bewegung, nicht aus einer eigenen
        // Rechnung.
        //
        // Vorher stand hier `--selectedPath.back()`, und das stimmt nur,
        // solange der Knoten seine Ebene behaelt. Seit rc516 kann er den
        // Block verlassen: dann ist `back()` gleich 0, es griff KEIN Zweig,
        // und die Auswahl zeigte hinterher auf den falschen Knoten. Nach
        // unten wurde sogar falsch hochgezaehlt.
        //
        // Dieselbe Lehre wie bei den Spaltenbreiten: erfragen statt
        // herleiten.
        // Sagen, wenn es eine MAKROZEILE ist.
        //
        // shank hat es jetzt viermal versucht, und in jedem seiner
        // Protokolle steht `Tiefe 1` - er bewegt Makrozeilen, keine
        // Blockkinder. Das Modell arbeitet richtig (nachgestellt: ein Block
        // mit einem einzigen Kind laesst sich ueber alle drei Wege
        // herausholen), aber das Programm hat geschwiegen.
        //
        // Ein Werkzeug, das bei einer sinnlosen Geste einfach nichts tut,
        // laesst den Benutzer glauben, es sei kaputt. Es soll sagen, warum.
        for (const Row& zeile : g_app->rows) {
            if (zeile.path == sel && zeile.ausMakro) {
                diag::detail("Bewegen: \"" + zeile.name +
                             "\" ist eine MAKROZEILE (Weg " +
                             wegAlsText(sel) +
                             ") - ein Makro ist kein Block, seine Befehle "
                             "stehen daneben. Es gibt nichts, woraus sie "
                             "herauskommen koennten.");
                break;
            }
        }
        Path nachher;
        const bool ok = up ? g_app->doc.moveUp(sel, &nachher)
                           : g_app->doc.moveDown(sel, &nachher);
        if (!ok) {
            zeigeAbweisung();
            return;
        }
        // Den Aufklapp-Zustand mitziehen.
        //
        // Ohne das klappt ein offener Block zu, sobald ein Befehl an ihm
        // vorbeizieht - der Zustand haengt an Wegen, und die verschieben
        g_app->selectedPath = nachher;
    g_app->selection.assign(1, g_app->selectedPath);
    rebuildTree();
}

// Die Beschriftung der Taste einer Aktion, fuer Menue und Sprechblasen.
const char* chordName(keys::Action a) {
    const ImGuiKeyChord c = chordFor(a);
    return c == ImGuiKey_None ? nullptr : ImGui::GetKeyChordName(c);
}

// Die Beschriftung einer Aktion im Einstellungsfenster.
Str labelFor(keys::Action a) {
    switch (a) {
        case keys::Action::Delete:       return Str::ActDelete;
        case keys::Action::Clone:        return Str::ActClone;
        case keys::Action::Copy:         return Str::ActCopy;
        case keys::Action::Cut:          return Str::ActCut;
        case keys::Action::CutAlt:       return Str::ActCutAlt;
        case keys::Action::ExpandNode:   return Str::ActExpandNode;
        case keys::Action::CollapseNode: return Str::ActCollapseNode;
        case keys::Action::ExpandAll:    return Str::ActExpandAll;
        case keys::Action::CollapseAll:  return Str::ActCollapseAll;
        case keys::Action::Paste:        return Str::ActPaste;
        case keys::Action::CommentOut:   return Str::ActRem;
        case keys::Action::Uncomment:    return Str::ActUnrem;
        case keys::Action::Find:         return Str::ActFind;
        case keys::Action::FindRepeat:   return Str::ActFindNext;
        case keys::Action::FindPrevious: return Str::ActFindPrev;
        case keys::Action::MoveUp:       return Str::ActMoveUp;
        case keys::Action::MoveDown:     return Str::ActMoveDown;
        case keys::Action::Undo:         return Str::ActUndo;
        case keys::Action::Redo:         return Str::ActRedo;
        case keys::Action::BookmarkToggle: return Str::ActBookmarkToggle;
        case keys::Action::BookmarkNext:   return Str::ActBookmarkNext;
        case keys::Action::BookmarkPrev:   return Str::ActBookmarkPrev;
        case keys::Action::EditItem:     return Str::ActEdit2;
        case keys::Action::InsertItem:   return Str::ActInsert;
        case keys::Action::Open:         return Str::ActOpen;
        case keys::Action::Save:         return Str::ActSave;
        case keys::Action::SaveAs:       return Str::ActSaveAs;
        case keys::Action::SaveAll:      return Str::ActSaveAll;
        case keys::Action::Backup:       return Str::ActBackup;
        case keys::Action::Restore:      return Str::ActRestore;
        default:                         return Str::ActDelete;
    }
}

// REM umschalten - EINE Fassung fuer Ruecktaste, Rechtsklick und Knopf.
//
// Auf einer auskommentierten Zeile holt es die Anweisung zurueck, zu der die
// Zeile gehoert (samt ihrem Block, ohne getrennt auskommentierte Nachbarn),
// und waehlt sie aus. Sonst kommentiert es aus. Original: "Toggle REMark
// status of current line (BACKSPACE ...)".
bool remUmschalten(const Path& weg) {
    const Node* n = nodeAt(g_app->doc.script(), weg);
    if (n == nullptr) {
        return false;
    }
    if (n->kind == Node::Kind::LineComment) {
        Path anfang;
        if (!g_app->doc.uncomment(weg, nullptr, &anfang)) {
            return false;
        }
        g_app->selectedPath = anfang;
        g_app->selection.assign(1, anfang);
        rebuildTree();
        return true;
    }
    if (!g_app->doc.commentOut(weg)) {
        return false;
    }
    rebuildTree();
    return true;
}

// Numpad +/- (Original: Befehle 32774 und 32776).
//
// Am laufenden BehavEd.exe beobachtet: Num+ auf dem obersten Knoten zeigt
// 46 statt 5 Zeilen - es klappt den Eintrag SAMT allen Unterpunkten auf,
// wie TVE_EXPAND auf jeder Ebene. Num- klappt nur den Eintrag selbst zu;
// was darunter offen war, bleibt es fuer das naechste Aufklappen.
void klappeTeilbaum(const Path& weg, bool auf) {
    const Node* n = weg.empty() ? nullptr : nodeAt(g_app->doc.script(), weg);
    if (n == nullptr) {
        return;
    }
    g_app->doc.vergibKennungen();
    if (!auf || n->kind == Node::Kind::LineComment) {
        // Ein auskommentierter Block (Kopfzeile "//(BHVDREM)") klappt ueber
        // die Kennung seiner Kopfzeile - siehe tree.cpp.
        g_app->expanded.setOpen(n->kennung, auf);
        rebuildTree();
        return;
    }
    std::function<void(const Node&)> oeffne = [&](const Node& k) {
        if (k.hasBlock || k.kind == Node::Kind::Macro) {
            g_app->expanded.setOpen(k.kennung, true);
        }
        for (const Node& kind : k.children) {
            oeffne(kind);
        }
    };
    oeffne(*n);
    rebuildTree();
}

void handleShortcuts() {
    // Zwei Dinge auf einmal:
    //
    // 1. dialogAtFrameStart - ein Dialog, der GERADE mit Eingabe geschlossen
    //    hat, hat die Taste schon verbraucht. Ohne diese Abfrage machte
    //    Enter im Editor genau das Falsche: zu und sofort wieder auf.
    //    Anders als handleTreeKeys nimmt diese Stelle IsKeyPressed statt
    //    Shortcut, also gibt es keine Route, die das Popup beanspruchen
    //    koennte - die Wache muss es hier selbst tun.
    //
    // 2. prefsOpen, pk3Open, aboutOpen, missionPickOpen standen bisher
    //    nicht in der Liste. Enter loeste damit auch dann "Bearbeiten" aus,
    //    wenn die Einstellungen offen waren.
    // Bei offenem Find-Fenster NUR Rueckgaengig und Wiederholen - nach
    // "Replace All" will man sofort mit Strg+Z zurueck, wie in Notepad++.
    // Alles andere bleibt gesperrt (Enter im Suchfeld darf nicht
    // "Bearbeiten" ausloesen), und Tippen sperrt ohnehin alles.
    const bool nurRueckgaengig = g_app->findOpen || g_app->findAtFrameStart;
    if (ImGui::GetIO().WantTextInput || g_app->dialogAtFrameStart ||
        g_app->kuerzelWartet ||
        g_app->editorOpen || g_app->askSaveOpen ||
        g_app->frageOffen || g_app->prefsOpen ||
        g_app->pk3Open || g_app->aboutOpen || g_app->missionPickOpen) {
        return;   // waehrend einer Eingabe nicht dazwischenfunken
    }
    // Strg+Umschalt+Z gilt weiterhin als Wiederherstellen, auch wenn die
    // Belegung von Redo geaendert wurde - das erwartet jeder.
    // Strg+Umschalt+S sichert alle Reiter - so heisst es in jedem Editor,
    // der Reiter kennt. Vor Strg+S geprueft, weil ImGui den kleineren
    // Akkord sonst zuerst greifen laesst.
    // Strg+Umschalt+S (Alle sichern) steht jetzt in der Belegung - siehe
    // keys::Action::SaveAll. ImGui vergleicht die Zusatztasten genau, Strg+S
    // greift also nicht mit, wenn Umschalt gedrueckt ist.
    // Die Alt-Kuerzel der Knoepfe im Original ("&New", "&Open", "&Save",
    // "Sa&ve", "&Prefs", "E&xit", "&MRU", "&Find", "&Delete"). ImGui
    // unterstreicht keine Buchstaben, die Tasten wirken trotzdem. Alt+B
    // bleibt Backup - so steht es im Accelerator, und der geht in einem
    // Windows-Dialog vor.
    {
        const bool alt = ImGui::GetIO().KeyAlt && !ImGui::GetIO().KeyCtrl &&
                         !nurRueckgaengig;
        if (alt) {
            if (ImGui::IsKeyPressed(ImGuiKey_N, false)) { doNew(); return; }
            if (ImGui::IsKeyPressed(ImGuiKey_O, false)) { doOpen(); return; }
            if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
                if (doSave()) { addStatus(tr(Str::MsgSaved)); }
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
                if (doSaveAs()) { addStatus(tr(Str::MsgSaved)); }
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_P, false)) { g_app->prefsOpen = true; return; }
            if (ImGui::IsKeyPressed(ImGuiKey_M, false)) { g_app->mruOeffnen = true; return; }
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) { fensterZeigen(g_app->findOpen, "###find"); return; }
            if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
                if (confirmQuit()) {
                    g_app->quitConfirmed = true;
                    g_app->wantQuit = true;
                }
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_D, false)) {
                // Wie der Knopf Delete: auf alle ausgewaehlten Zeilen.
                const std::vector<Path> m = selectionOrCurrent();
                const bool ok = m.size() > 1
                                    ? g_app->doc.removeAll(m)
                                    : (!g_app->selectedPath.empty() &&
                                       g_app->doc.removeAt(g_app->selectedPath));
                if (ok) {
                    g_app->selection.clear();
                    g_app->selectedPath.clear();
                    rebuildTree();
                }
                return;
            }
        }
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
        doRedo();
        return;
    }

    const Path& sel = g_app->selectedPath;
    // Ueber die BELEGUNG laufen, nicht ueber eine feste Tastenliste. Damit
    // wirkt jede Aenderung aus dem Einstellungsfenster sofort, und eine
    // Aktion ohne Taste (chord 0) faellt von selbst heraus.
    for (const keys::Bindable& bind : keys::bindable()) {
        // Bei offenem Suchfenster nur, was dort nichts anrichten kann:
        // Rueckgaengig/Wiederholen und - wie in Notepad++ - F3/Umschalt+F3
        // zum Weitersuchen. Gebraucht nach "Find similar": das Fenster ist
        // offen, und F3 soll zum naechsten gleichen Befehl springen.
        if (nurRueckgaengig && bind.action != keys::Action::Undo &&
            bind.action != keys::Action::Redo && bind.action != keys::Action::FindRepeat &&
            bind.action != keys::Action::FindPrevious) {
            continue;
        }
        const ImGuiKeyChord chord = chordFor(bind.action);
        if (chord == ImGuiKey_None || !ImGui::IsKeyChordPressed(chord)) {
            continue;
        }
        g_app->letzteKuerzelAktion = bind.action;
        ++g_app->kuerzelZaehler;
        if (g_app->kuerzelNurMelden &&
            (bind.action == keys::Action::Open || bind.action == keys::Action::SaveAs ||
             bind.action == keys::Action::SaveAll)) {
            continue;   // Selbsttest: gemeldet, nicht ausgefuehrt
        }
        switch (bind.action) {
            case keys::Action::MoveUp:   moveSelection(true); break;
            case keys::Action::MoveDown: moveSelection(false); break;
            case keys::Action::BookmarkToggle: lesezeichenUmschalten(selectionOrCurrent()); break;
            case keys::Action::BookmarkNext:   lesezeichenSpringen(true); break;
            case keys::Action::BookmarkPrev:   lesezeichenSpringen(false); break;
            case keys::Action::Undo: {
                // Erst die Gizmo-Vorschau, dann das Skript.
                //
                // Gemeldet als "undo geht nicht strg + z". Der Grund war,
                // dass eine verschobene Kamera noch gar nicht im Skript
                // steht - es gibt also nichts rueckgaengig zu machen, und
                // doUndo() nahm stattdessen die letzte ECHTE Aenderung
                // zurueck. Verwirrender geht es kaum.
                //
                // Jetzt raeumt Strg+Z zuerst die Vorschau weg. Das ist,
                // was der Anwender in dem Moment meint - und danach wirkt
                // es wieder auf das Skript.
                // BEIDE Vorschauen zaehlen: die Verschiebung UND die
                // Drehung. Gemeldet als "Undo funktioniert nicht dabei" -
                // gedreht war eine Kamera, geprueft wurde nur der Versatz.
                if (g_app->gizmoOffset[0] != 0.0F ||
                    g_app->gizmoOffset[1] != 0.0F ||
                    g_app->gizmoOffset[2] != 0.0F ||
                    g_app->gizmoAngles[0] != 0.0F ||
                    g_app->gizmoAngles[1] != 0.0F ||
                    g_app->gizmoAngles[2] != 0.0F) {
                    for (int k = 0; k < 3; ++k) {
                        g_app->gizmoOffset[k] = 0.0F;
                        g_app->gizmoAngles[k] = 0.0F;
                    }
                    g_app->gizmoAxis = -1;
                    g_app->gizmoRotating = false;
                    g_app->mapDirty = true;
                    break;
                }
                doUndo();
                break;
            }
            case keys::Action::Redo:     doRedo(); break;
            case keys::Action::Delete: {
                const std::vector<Path> m = selectionOrCurrent();
                const bool ok = m.size() > 1 ? g_app->doc.removeAll(m)
                                             : g_app->doc.removeAt(sel);
                if (ok) {
                    g_app->selection.clear();
                    g_app->selectedPath.clear();
                    rebuildTree();
                }
                break;
            }
            case keys::Action::Clone: {
                // Auch mehrere auf einmal - wie Kopieren und Loeschen.
                const std::vector<Path> m = selectionOrCurrent();
                const bool ok = m.size() > 1 ? g_app->doc.cloneAll(m)
                                             : g_app->doc.cloneAt(sel);
                if (ok) { rebuildTree(); }
                break;
            }
            case keys::Action::Copy:
                kopieren(false);
                break;
            case keys::Action::Cut:
            case keys::Action::CutAlt:   // Strg+T - im Original ein zweites Cut
                kopieren(true);
                break;
            case keys::Action::ExpandNode:
                klappeTeilbaum(sel, true);
                break;
            case keys::Action::CollapseNode:
                klappeTeilbaum(sel, false);
                break;
            case keys::Action::ExpandAll:
                g_app->expanded.openAll();
                rebuildTree();
                break;
            case keys::Action::CollapseAll:
                g_app->expanded.closeAll();
                rebuildTree();
                break;
            case keys::Action::Paste:
                einfuegen(sel);
                break;
            case keys::Action::CommentOut: {
                // Umschalten wie der Knopf REM - Original: "Toggle REMark
                // status of current line (BACKSPACE ...)". Vorher konnte die
                // Ruecktaste nur auskommentieren.
                (void)remUmschalten(sel);
                break;
            }
            case keys::Action::Uncomment: {
                // Strg+Ruecktaste: nur zuruecknehmen, nie auskommentieren.
                const Node* n = nodeAt(g_app->doc.script(), sel);
                if (n != nullptr && n->kind == Node::Kind::LineComment) {
                    (void)remUmschalten(sel);
                }
                break;
            }
            case keys::Action::EditItem: openEditorForNode(sel); break;
            case keys::Action::InsertItem:
                // Einfg fuegt den links gewaehlten Befehl ein - dasselbe wie
                // der Doppelklick in der Ereignisliste.
                if (g_app->selectedCommand >= 0 &&
                    g_app->selectedCommand < static_cast<int>(g_app->db.commands.size())) {
                    insertCommand(g_app->db.commands[
                        static_cast<std::size_t>(g_app->selectedCommand)]);
                }
                break;
            case keys::Action::Open: doOpen(); break;
            case keys::Action::Save: (void)doSave(); break;
            case keys::Action::SaveAll: doSaveAll(); break;
            case keys::Action::SaveAs: (void)doSaveAs(); break;
            case keys::Action::Find: fensterZeigen(g_app->findOpen, "###find"); break;
            case keys::Action::FindRepeat: findNext(1); break;
            case keys::Action::FindPrevious: findNext(-1); break;
            // Beide stehen im Kuerzelfenster und liessen sich belegen - die
            // Belegung tat aber nichts, weil sie hier fehlten. Gefunden vom
            // Selbsttest, der jede Belegung aendert und ausprobiert.
            case keys::Action::Backup: doBackup(); break;
            case keys::Action::Restore: doRestore(); break;
            default: break;
        }
    }
}

// --- Einstellungen (Dialog 131 des Originals) --------------------------
//
// Uebernommen sind die Felder, die es heute noch gibt. Weggelassen: alles
// zu SourceSafe (drei Felder und ein Schalter) - das System ist lange fort,
// und die Browse-Knoepfe dafuer waren schon im Original ausgegraut. Ebenso
// die fuenf Knoepfe "Set All Options to TREK/SOF2/JK2/JKA/XMen default":
// dahinter stehen Raven-interne Pfade, die es nirgends mehr gibt.
// Die Felder des Einstellungsfensters zurueckspielen ("Cancel", X).
void prefsZurueck() {
    const Settings& v = g_app->prefsVorher;
    Settings& st = g_app->settings;
    st.scriptPath = v.scriptPath;
    st.commandFile = v.commandFile;
    st.sourcePath = v.sourcePath;
    st.ibizePath = v.ibizePath;
    st.reopenLastFile = v.reopenLastFile;
    st.alphaSortPulldowns = v.alphaSortPulldowns;
    st.alternativeIcons = v.alternativeIcons;
    st.queryOnDiscard = v.queryOnDiscard;
    st.dialogUsesLastDir = v.dialogUsesLastDir;
}

void drawPrefs() {
    if (!g_app->prefsOpen) {
        // Geschlossen, ohne OK (Cancel oder X): alten Stand zurueck.
        if (g_app->prefsHatVorher) {
            prefsZurueck();
            g_app->prefsHatVorher = false;
        }
        return;
    }
    if (!g_app->prefsHatVorher) {
        g_app->prefsVorher = g_app->settings;
        g_app->prefsHatVorher = true;
    }
    ImGui::OpenPopup(tr(Str::PrefsTitle));
    if (!ImGui::BeginPopupModal(tr(Str::PrefsTitle), &g_app->prefsOpen,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    Settings& st = g_app->settings;
    const float w = ImGui::GetFontSize() * 24.0F;

    auto pathRow = [&](Str label, std::string* value, bool folder, const char* filter) {
        ImGui::TextUnformatted(tr(label));
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s", value->c_str());
        ImGui::PushID(tr(label));
        ImGui::SetNextItemWidth(w);
        if (ImGui::InputText("##v", buf, sizeof(buf))) {
            *value = buf;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr(Str::PrefsBrowse))) {
            // Fuer Ordner gibt es keinen eigenen Dialog: eine Datei darin
            // auswaehlen und den Ordner davon nehmen. Das ist weniger
            // elegant als ein Ordnerdialog, funktioniert aber ueberall.
            const std::string p = platform::openFileDialog(tr(label), filter, *value);
            if (!p.empty()) {
                *value = folder ? directoryOf(p) : p;
            }
        }
        ImGui::PopID();
    };

    ImGui::SeparatorText(tr(Str::PrefsDirectories));
    pathRow(Str::PrefsScriptPath, &st.scriptPath, true, kScriptFilter);
    pathRow(Str::PrefsCommandFile, &st.commandFile, false,
            "Command description (*.bhc)|*.bhc|All files (*.*)|*.*");
    pathRow(Str::PrefsSourcePath, &st.sourcePath, true,
            "Engine headers (*.h)|*.h|All files (*.*)|*.*");
    pathRow(Str::PrefsIbizePath, &st.ibizePath, false,
            "IBIZE (IBIZE.EXE)|IBIZE.EXE;*.exe|All files (*.*)|*.*");

    ImGui::TextDisabled("%s", tr(Str::PrefsNote));
    if (ImGui::Button(tr(Str::PrefsReload))) {
        std::string error;
        CommandDb fresh;
        std::vector<LoadDiag> ld;
        const std::string bhc = st.commandFile.empty()
                                    ? g_app->dataDir + "/behaved.bhc"
                                    : st.commandFile;
        const std::string dir = st.sourcePath.empty() ? g_app->dataDir : st.sourcePath;
        if (loadCommandDb(bhc, dir, fresh, ld)) {
            (void)loadCommandDb(directoryOf(bhc) + "/supplement.bhc", dir, fresh, ld);
            g_app->db = std::move(fresh);
            rebuildTree();
            char msg[200];
            std::snprintf(msg, sizeof(msg), tr(Str::MsgModelLoaded),
                          static_cast<int>(g_app->db.commands.size()),
                          static_cast<int>(g_app->db.typesets.size()),
                          static_cast<int>(g_app->db.macros.size()));
        } else {
            platform::showError(bhc, tr(Str::AppTitle));
        }
    }

    ImGui::SeparatorText(tr(Str::GroupApplication));
    ImGui::Checkbox(tr(Str::PrefsReopen), &st.reopenLastFile);
    ImGui::Checkbox(tr(Str::PrefsAlphaSort), &st.alphaSortPulldowns);
    ImGui::Checkbox(tr(Str::PrefsAltIcons), &st.alternativeIcons);
    ImGui::Checkbox(tr(Str::PrefsQuery), &st.queryOnDiscard);
    ImGui::Checkbox(tr(Str::PrefsLastDir), &st.dialogUsesLastDir);

    ImGui::Dummy(ImVec2{0, ImGui::GetStyle().ItemSpacing.y});
    ImGui::Separator();
    if (ImGui::Button(tr(Str::EditorOk), ImVec2{ImGui::GetFontSize() * 5.0F, 0.0F})) {
        g_app->prefsOpen = false;
        g_app->prefsHatVorher = false;   // uebernommen
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    // Wie im Original: OK und Cancel. Cancel spielt den Stand vom Oeffnen
    // zurueck (im naechsten Bild, siehe oben) - das X ebenso.
    if (ImGui::Button(tr(Str::EditorCancel), ImVec2{ImGui::GetFontSize() * 5.0F, 0.0F})) {
        g_app->prefsOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Die Spielordner als EIGENES Fenster.
//
// Sie standen mitten in den Einstellungen, zwischen den Verzeichnissen und
// den Tastenkuerzeln. Wer sie sucht, scrollt daran vorbei - und wer die
// Tasten sucht, scrollt an den Ordnern vorbei. Drei Sachen, drei Fenster.
// --- Was die offenen Skripte miteinander verbindet -----------------------
//
// Seit eine Mission ALLE ihre Skripte oeffnet (rc236), stellt sich die
// Frage, wie sie zusammenspielen. Im OpenJK-Quelltext nachgesehen:
//
//   * Signale liegen in EINER Tabelle auf der Icarus-Instanz, nicht je
//     Skript. Eines sendet, ein anderes wartet - so verzahnt man sie.
//   * Die Kamera ist EINE einzige (camera_t client_camera). Zwei Skripte,
//     die sie gleichzeitig anfassen, ueberschreiben einander.
//
// Diese Uebersicht zeigt beides. Die Auswertung selbst steht im Kern
// (interplay.h) und hat eine eigene Probe - hier wird nur gezeichnet.
void drawInterplay() {
    if (!g_app->interplayOpen) {
        return;
    }
    const std::string t =
        std::string(tr(Str::InterplayTitle)) + "###interplay";
    // Startgroesse und Untergrenze. Ohne beides nahm ImGui seine Vorgabe
    // von rund 160 x 190 Punkten - Titel und Inhalt abgeschnitten. Gemeldet:
    // "manche von den menu fenster sind zu klein".
    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 44.0F,
                                    ImGui::GetFontSize() * 26.0F},
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2{ImGui::GetFontSize() * 30.0F, ImGui::GetFontSize() * 14.0F},
        ImVec2{FLT_MAX, FLT_MAX});
    if (!ImGui::Begin(t.c_str(), &g_app->interplayOpen)) {
        ImGui::End();
        return;
    }

    // Alle offenen Skripte einsammeln. Der AKTIVE Reiter hat das lebende
    // Dokument, die anderen ihr geparktes - dieselbe Unterscheidung wie
    // ueberall sonst.
    std::vector<NamedFacts> alle;
    for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
        const bool aktiv = (static_cast<int>(i) == g_app->activeTab);
        const std::string nm =
            aktiv ? (g_app->shownName.empty() ? std::string("unnamed.txt")
                                              : g_app->shownName)
                  : (g_app->tabs[i].shownName.empty()
                         ? std::string("unnamed.txt")
                         : g_app->tabs[i].shownName);
        const Script& sc = aktiv ? g_app->doc.script() : g_app->tabs[i].doc.script();
        alle.push_back(NamedFacts{nm, scanScript(sc)});
    }

    if (alle.empty()) {
        ImGui::TextUnformatted(tr(Str::InterplayNoScripts));
        ImGui::End();
        return;
    }

    // --- Die Kamera zuerst: sie ist der haerteste Fall -------------------
    ImGui::SeparatorText(tr(Str::InterplayCamera));
    const std::vector<std::string> kam = cameraScripts(alle);
    if (kam.empty()) {
        ImGui::TextDisabled("-");
    } else {
        for (const std::string& k : kam) {
            ImGui::BulletText("%s", k.c_str());
        }
        if (kam.size() > 1) {
            ImGui::TextColored(ImVec4{1.0F, 0.6F, 0.2F, 1.0F}, "%s",
                               tr(Str::InterplayCameraWarn));
        }
    }

    // --- Und die Signale -------------------------------------------------
    ImGui::SeparatorText(tr(Str::InterplaySignals));
    const std::vector<SignalLink> sig = linkSignals(alle);
    if (sig.empty()) {
        ImGui::TextDisabled("-");
    }
    for (const SignalLink& l : sig) {
        std::string von;
        for (const std::string& x : l.senders) {
            von += (von.empty() ? "" : ", ") + x;
        }
        std::string nach;
        for (const std::string& x : l.waiters) {
            nach += (nach.empty() ? "" : ", ") + x;
        }

        // Die halben Faelle stehen hervorgehoben: ein Signal ohne
        // Empfaenger geht ins Leere, eines ohne Sender laesst ein Skript
        // ewig warten. Beides ist ein Hinweis, kein Urteil - der fehlende
        // Teil kann in einem Skript stehen, das gerade nicht offen ist.
        const bool halb = l.senders.empty() || l.waiters.empty();
        if (halb) {
            ImGui::TextColored(ImVec4{1.0F, 0.6F, 0.2F, 1.0F}, "%s",
                               l.name.c_str());
        } else {
            ImGui::TextUnformatted(l.name.c_str());
        }
        ImGui::SameLine();
        if (l.senders.empty()) {
            ImGui::TextDisabled("- %s", tr(Str::InterplayStuck));
        } else if (l.waiters.empty()) {
            ImGui::TextDisabled("- %s (%s)", tr(Str::InterplayOrphan),
                                von.c_str());
        } else {
            // Der Pfeil ist ein Zeichen, kein Wort - er braucht keine
            // Uebersetzung. Der Pruefer sieht aber nur festen Text im
            // Format, also aus den Namen zusammensetzen.
            const std::string zeile = von + "  ->  " + nach;
            ImGui::TextDisabled("%s", zeile.c_str());
        }
    }

    ImGui::End();
}

void drawGamePaths() {
    if (!g_app->pathsOpen) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 58.0F,
                                    ImGui::GetFontSize() * 12.0F},
                             ImGuiCond_FirstUseEver);
    // Eine Untergrenze, sonst laesst sich das Fenster auf Fingernagelgroesse
    // ziehen und ist dann nicht mehr zu bedienen. Dieselbe Vorsorge wie beim
    // Meldungsfenster, hier hatte ich sie vergessen.
    ImGui::SetNextWindowSizeConstraints(
        ImVec2{ImGui::GetFontSize() * 40.0F, ImGui::GetFontSize() * 16.0F},
        ImVec2{FLT_MAX, FLT_MAX});
    const std::string titel = std::string(tr(Str::PathsTitle)) + "###paths";
    // Lange Pfade: waagerecht rollen statt abschneiden. Bei der Untergrenze
    // von 40 Zeichen war die Zeile 342 Punkte breiter als das Fenster.
    if (!ImGui::Begin(titel.c_str(), &g_app->pathsOpen,
                      ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::End();
        return;
    }
    Settings& st = g_app->settings;
    ImGui::SeparatorText(tr(Str::PrefsGamePaths));
    ImGui::TextDisabled("%s", tr(Str::PrefsGamePathsNote));
    // Die Reihenfolge ist keine Kosmetik.
    //
    // Movie Duels ERSETZT Dateien des Originalspiels unter demselben Namen -
    // models/players/_humanoid/_humanoid.gla gibt es zweimal, mit 21376 und
    // mit 30384 Bildern. Wer die Ordner falsch herum stehen hat, laedt die
    // falsche, und ihm fehlt jede Animation jenseits der 21376. Deshalb
    // steht hier, WER GEWINNT, und deshalb laesst sich die Reihenfolge in
    // beide Richtungen aendern.
    for (std::size_t i = 0; i < st.gamePaths.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool letzter = (i + 1 == st.gamePaths.size());

        ImGui::BeginDisabled(i == 0);
        if (ImGui::SmallButton(tr(Str::PrefsUp))) {
            std::swap(st.gamePaths[i], st.gamePaths[i - 1]);
            rescanGamePaths();
            refreshPk3List();
            ImGui::EndDisabled();
            ImGui::PopID();
            break;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        // Nach unten: fehlte bisher. Wer den letzten Ordner nach oben
        // bringen wollte, musste alle anderen einzeln hochschieben.
        ImGui::BeginDisabled(letzter);
        if (ImGui::SmallButton(tr(Str::PrefsDown))) {
            std::swap(st.gamePaths[i], st.gamePaths[i + 1]);
            rescanGamePaths();
            refreshPk3List();
            ImGui::EndDisabled();
            ImGui::PopID();
            break;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton(tr(Str::PrefsRemove))) {
            st.gamePaths.erase(st.gamePaths.begin() + static_cast<std::ptrdiff_t>(i));
            rescanGamePaths();
            refreshPk3List();
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        // Den Ordner im Explorer zeigen. Ein Pfad in einer Zeile sagt nicht,
        // ob wirklich das drinsteht, was man erwartet - ein Blick hinein
        // schon. openWithDefaultApp oeffnet bei einem Ordner den Explorer.
        if (ImGui::SmallButton(tr(Str::PrefsOpenFolder))) {
            platform::openWithDefaultApp(st.gamePaths[i]);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(st.gamePaths[i].c_str());

        // Was in diesem Ordner gefunden wurde - sonst raet man, ob er stimmt.
        if (i < g_app->gamePaths.size()) {
            const GamePath& gp = g_app->gamePaths[i];
            ImGui::SameLine();
            if (gp.archives.empty()) {
                // Ein Ordner ohne Archive ist fast immer ein Tippfehler im
                // Pfad. Das soll auffallen.
                ImGui::TextColored(ImVec4{0.90F, 0.75F, 0.35F, 1.0F}, "%s",
                                   tr(Str::PrefsEmptyPath));
            } else {
                ImGui::TextDisabled(tr(Str::PrefsArchives),
                                    static_cast<int>(gp.archives.size()),
                                    gp.fileCount);
            }
        }
        // Und wer bei Namensgleichheit gewinnt.
        if (letzter && st.gamePaths.size() > 1) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4{0.45F, 0.80F, 0.45F, 1.0F}, "- %s",
                               tr(Str::PrefsWins));
        }
        ImGui::PopID();
    }
    if (ImGui::Button(tr(Str::Pk3AddPath))) { addGamePath(); }

    ImGui::End();
}

// Welche Zeile gerade auf eine Taste wartet. Nicht in der Funktion, damit
// ein geschlossenes Fenster die Aufnahme sicher beenden kann.
keys::Action g_kuerzelZeile = keys::Action::Unhandled;

// Und die Tastenkuerzel ebenso.
void drawKeyBindings() {
    if (!g_app->keysOpen) {
        // Zu heisst: nichts wartet mehr. Sonst bliebe kuerzelWartet stehen
        // und sperrte alle Kuerzel bis zum Neustart.
        g_kuerzelZeile = keys::Action::Unhandled;
        g_app->kuerzelWartet = false;
        return;
    }
    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 42.0F,
                                    ImGui::GetFontSize() * 30.0F},
                             ImGuiCond_FirstUseEver);
    // Eine Untergrenze, sonst laesst sich das Fenster auf Fingernagelgroesse
    // ziehen und ist dann nicht mehr zu bedienen. Dieselbe Vorsorge wie beim
    // Meldungsfenster, hier hatte ich sie vergessen.
    ImGui::SetNextWindowSizeConstraints(
        ImVec2{ImGui::GetFontSize() * 32.0F, ImGui::GetFontSize() * 20.0F},
        ImVec2{FLT_MAX, FLT_MAX});
    const std::string titel = std::string(tr(Str::KeysTitle)) + "###keys";
    if (!ImGui::Begin(titel.c_str(), &g_app->keysOpen)) {
        ImGui::End();
        return;
    }
    // --- Tastenkuerzel ---------------------------------------------------
    //
    // Dear ImGui bringt dafuer nichts Fertiges mit - nachgesehen in imgui.h
    // und in ocornuts Ausgabe 5862, wo genau danach gefragt wird. Was es
    // gibt, sind die Bausteine, und die reichen:
    //
    //   ImGuiKeyChord     Taste und Zusatztasten in EINER Zahl
    //   GetKeyChordName   Beschriftung dafuer
    //   IsKeyChordPressed pruefen, ob sie gedrueckt wurde
    //
    // Zum Aufnehmen der neuen Taste laeuft man ueber die benannten Tasten
    // (ImGuiKey_NamedKey_BEGIN bis _END) und nimmt die erste gedrueckte.
    ImGui::SeparatorText(tr(Str::KeysTitle));
    ImGui::TextWrapped("%s", tr(Str::KeysHint));

    // Welche Zeile wartet gerade auf eine Taste? Nur eine zur Zeit.
    keys::Action& waiting = g_kuerzelZeile;

    if (ImGui::BeginTable("keys", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr(Str::KeysAction));
        ImGui::TableSetupColumn(tr(Str::KeysShortcut));
        ImGui::TableHeadersRow();
        for (const keys::Bindable& b : keys::bindable()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tr(labelFor(b.action)));
            ImGui::TableNextColumn();
            ImGui::PushID(b.name);

            const ImGuiKeyChord now = chordFor(b.action);
            char shown[64];
            if (waiting == b.action) {
                std::snprintf(shown, sizeof(shown), "%s", tr(Str::KeysPress));
            } else if (now == ImGuiKey_None) {
                std::snprintf(shown, sizeof(shown), "%s", tr(Str::KeysNone));
            } else {
                std::snprintf(shown, sizeof(shown), "%s",
                              ImGui::GetKeyChordName(now));
            }
            // Die Kennung haengt an der AKTION, nicht an der Beschriftung
            // ("###k_<name>"): so bleibt der Knopf derselbe, waehrend er
            // "Press a key..." zeigt, und der Selbsttest findet jede Zeile.
            const std::string knopf = std::string(shown) + "###k_" + b.name;
            if (ImGui::Button(knopf.c_str(), ImVec2{ImGui::GetFontSize() * 9.0F, 0.0F})) {
                waiting = (waiting == b.action) ? keys::Action::Unhandled
                                                : b.action;
            }
            // Doppelt belegt? Dann daneben sagen, von wem.
            if (now != ImGuiKey_None && waiting != b.action) {
                for (const keys::Bindable& o : keys::bindable()) {
                    if (o.action == b.action || chordFor(o.action) != now) {
                        continue;
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled(tr(Str::KeysClash), tr(labelFor(o.action)));
                    break;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Klick ausserhalb bricht die Aufnahme ab. Vorher lief sie weiter,
    // auch wenn das Fenster verdeckt war - jeder naechste Tastendruck wurde
    // zur neuen Belegung, und die Kuerzel wirkten "kaputt".
    if (waiting != keys::Action::Unhandled &&
        !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        waiting = keys::Action::Unhandled;
    }
    g_app->kuerzelWartet = (waiting != keys::Action::Unhandled);
    if (waiting != keys::Action::Unhandled) {
        // Esc bricht ab, Rueckschritt loescht die Belegung.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            waiting = keys::Action::Unhandled;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            setChord(waiting, ImGuiKey_None);
            waiting = keys::Action::Unhandled;
        } else {
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                const auto key = static_cast<ImGuiKey>(k);
                // Zusatztasten allein sind keine Belegung.
                //
                // AUCH die Sammeltasten ImGuiKey_ReservedForModCtrl & Co.:
                // sie liegen im Bereich der benannten Tasten und gelten als
                // gedrueckt, sobald Strg gedrueckt ist. Gemeldet: "I'm
                // trying to change the find keyboard shortcut to CTRL + F
                // but I can't" - aufgenommen wurde "Ctrl+ModCtrl", bevor
                // das F ueberhaupt kam. Maustasten ebenso wenig: der Klick
                // auf den Knopf selbst ist keine Belegung.
                if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl ||
                    key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
                    key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt ||
                    key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper ||
                    key == ImGuiKey_ReservedForModCtrl ||
                    key == ImGuiKey_ReservedForModShift ||
                    key == ImGuiKey_ReservedForModAlt ||
                    key == ImGuiKey_ReservedForModSuper ||
                    (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY)) {
                    continue;
                }
                if (!ImGui::IsKeyPressed(key, false)) {
                    continue;
                }
                ImGuiKeyChord c = key;
                if (ImGui::GetIO().KeyCtrl) { c |= ImGuiMod_Ctrl; }
                if (ImGui::GetIO().KeyAlt) { c |= ImGuiMod_Alt; }
                if (ImGui::GetIO().KeyShift) { c |= ImGuiMod_Shift; }
                setChord(waiting, c);
                waiting = keys::Action::Unhandled;
                break;
            }
        }
    }

    if (ImGui::Button(tr(Str::KeysReset))) {
        g_app->settings.keyBindings.clear();
        waiting = keys::Action::Unhandled;
        g_app->kuerzelWartet = false;
    }

    ImGui::End();
}

// AUSSERHALB des namenlosen Namensraums - der Selbsttest ruft sie
// (lint_link.py).
}  // namespace

// Ein Modell aus den Archiven oeffnen - fuer den pk3-Browser und den
// Selbsttest (BHED_MODELL). Gibt false zurueck, wenn es sich nicht lesen
// liess; `fehler` sagt dann, warum.
bool oeffneModellAusArchiv(const std::string& name, std::string* fehler) {
    std::string data;
    if (!readFromArchives(name, data)) {
        if (fehler != nullptr) { *fehler = "nicht in den Archiven"; }
        return false;
    }
    diag::Step ms("Modell laden: " + name);
    GlmModel gm;
    std::string gerr;
    if (!readGlm(data, gm, &gerr)) {
        ms.fail(gerr);
        if (fehler != nullptr) { *fehler = gerr; }
        return false;
    }
    {
        const std::string dir = directoryOf(name);
        // ALLE Haeute im selben Ordner sammeln, nicht nur die
        // Vorgabe.
        //
        // Vorher geschah das nur beim Laden von der PLATTE -
        // aus einem Archiv blieb die Liste leer, und die
        // Auswahl fehlte ganz. Genau der haeufigere Weg.
        g_app->modelSkins.clear();
        g_app->modelSkinIndex = -1;
        g_app->modelDir = dir;
        for (const GamePath& gp : g_app->gamePaths) {
            for (const FoundFile& sf : findByExtension(gp, {".skin"})) {
                if (directoryOf(sf.name) != dir) {
                    continue;
                }
                const std::string base = fileName(sf.name);
                if (std::find(g_app->modelSkins.begin(),
                              g_app->modelSkins.end(), base) ==
                    g_app->modelSkins.end()) {
                    g_app->modelSkins.push_back(base);
                }
            }
        }
        std::sort(g_app->modelSkins.begin(), g_app->modelSkins.end());
        for (const char* which : {"model_default.skin", "model.skin"}) {
            std::string skin;
            if (readFromArchives(dir + "/" + which, skin)) {
                applySkin(skin, gm);
                break;
            }
        }
        g_app->model = std::move(gm);
        g_app->modelPath = name;
        // Kamera vor die Figur, auf halbe Hoehe.
        for (int k = 0; k < 3; ++k) {
            g_app->modelCam.pos[k] =
                (g_app->model.mins[k] + g_app->model.maxs[k]) * 0.5F;
        }
        float size = 0.0F;
        for (int k = 0; k < 3; ++k) {
            size = std::max(size,
                            g_app->model.maxs[k] - g_app->model.mins[k]);
        }
        g_app->modelOrbit.distance = std::max(size * 1.15F, 24.0F);
        g_app->modelCam.angles[0] = 0.0F;
        g_app->modelCam.angles[1] = 180.0F;
        {
            // "vorn" statt "f": weiter oben steht schon ein
            // f fuer die gefundene Datei.
            float vorn[3];
            g_app->modelCam.forward(vorn);
            for (int k = 0; k < 3; ++k) {
                g_app->modelCam.pos[k] =
                    (g_app->model.mins[k] +
                     g_app->model.maxs[k]) * 0.5F -
                    vorn[k] * g_app->modelOrbit.distance;
            }
        }
        g_app->anim = GlaAnimation{};
        g_app->animList.clear();
        g_app->animIndex = -1;
        g_app->modelDirty = true;
        g_app->leftMode = 2;
        loadModelTextures();
        // Das Skelett gleich mitsuchen - der Pfad steht im
        // Modell.
        loadSkeletonForModel();
    }
    return true;
}

namespace {

void drawPk3Browser() {
    if (!g_app->pk3Open) {
        return;
    }
    ImGui::OpenPopup(tr(Str::Pk3Title));
    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 40.0F,
                                    ImGui::GetFontSize() * 26.0F},
                             ImGuiCond_FirstUseEver);
    // Untergrenze: ohne sie liess sich der Browser auf 48 x 48 Punkte
    // zusammenziehen (Fenstertest, "laesst sich nicht zu klein ziehen").
    ImGui::SetNextWindowSizeConstraints(
        ImVec2{ImGui::GetFontSize() * 36.0F, ImGui::GetFontSize() * 18.0F},
        ImVec2{FLT_MAX, FLT_MAX});
    if (!ImGui::BeginPopupModal(tr(Str::Pk3Title), &g_app->pk3Open)) {
        return;
    }

    if (ImGui::Button(tr(Str::Pk3AddPath))) { addGamePath(); }
    ImGui::SameLine();
    for (int k = 0; k < 3; ++k) {
        const Str label = (k == 0) ? Str::Pk3Scripts
                        : (k == 1) ? Str::Pk3Maps
                                   : Str::Pk3Models;
        if (ImGui::RadioButton(tr(label), g_app->pk3Kind == k)) {
            g_app->pk3Kind = k;
            refreshPk3List();
        }
        if (k < 2) {
            ImGui::SameLine();
        }
    }

    // Archivfilter. Nur der Dateiname, nicht der ganze Pfad - sonst ist die
    // Liste breiter als das Fenster.
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0F);
    const std::string shown = g_app->pk3Archive.empty()
                                  ? std::string(tr(Str::Pk3AllArchives))
                                  : fileName(g_app->pk3Archive);
    if (ImGui::BeginCombo(tr(Str::Pk3Archive), shown.c_str())) {
        if (ImGui::Selectable(tr(Str::Pk3AllArchives), g_app->pk3Archive.empty())) {
            g_app->pk3Archive.clear();
            refreshPk3List();
        }
        for (const GamePath& gp : g_app->gamePaths) {
            for (const Pk3& a : gp.archives) {
                const std::string base = fileName(a.path);
                const bool sel = (g_app->pk3Archive == a.path);
                if (ImGui::Selectable(base.c_str(), sel)) {
                    g_app->pk3Archive = a.path;
                    refreshPk3List();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(a.path.c_str());
                    ImGui::TextDisabled(tr(Str::Pk3ArchiveInfo),
                                        static_cast<int>(a.entries.size()));
                    ImGui::EndTooltip();
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    int archives = 0;
    for (const GamePath& gp : g_app->gamePaths) {
        archives += static_cast<int>(gp.archives.size());
    }
    ImGui::Text(tr(Str::Pk3Found), archives, static_cast<int>(g_app->pk3Files.size()));
    if (g_app->gamePaths.empty()) {
        ImGui::TextDisabled("%s", tr(Str::Pk3None));
    }

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0F);
    ImGui::InputText(tr(Str::Pk3Filter), g_app->pk3Filter, sizeof(g_app->pk3Filter));

    // Bei Yavin2 sind es 370 Zielnamen und hier schnell tausende Dateien -
    // ohne Filter findet man nichts.
    std::string needle = g_app->pk3Filter;
    for (char& c : needle) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    ImGui::BeginChild("list", ImVec2{0, -ImGui::GetFrameHeightWithSpacing()},
                      ImGuiChildFlags_Borders);
    for (std::size_t fi = 0; fi < g_app->pk3Files.size(); ++fi) {
        const FoundFile& f = g_app->pk3Files[fi];
        if (!needle.empty()) {
            std::string low = f.name;
            for (char& c : low) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (low.find(needle) == std::string::npos) {
                continue;
            }
        }
        // Die STELLE als Kennung, nicht den Namen: dieselbe Datei kann in
        // mehreren Archiven liegen (models/players/kyle/model.glm steckt in
        // assets1.pk3 UND in einem Mod-Archiv). Gleiche Namen ergaeben
        // gleiche Kennungen, und ImGui meldet zu Recht einen Zusammenstoss.
        ImGui::PushID(static_cast<int>(fi));
        if (ImGui::Selectable(f.name.c_str(), false,
                              ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            std::string data;
            if (readFromArchives(f.name, data)) {
                if (g_app->pk3Kind == 2) {
                    // Ein Modell. Die .skin dazu suchen wir gleich mit: sie
                    // liegt neben dem Modell und heisst model_<name>.skin.
                    std::string gerr;
                    if (!oeffneModellAusArchiv(f.name, &gerr)) {
                        platform::showError(f.name + "\n" + gerr, tr(Str::AppTitle));
                    }
                } else if (g_app->pk3Kind == 1) {
                    // Derselbe Weg wie beim Oeffnen von der Platte:
                    // Entities, Geometrie und Texturen.
                    diag::Step mapStep("Karte aus Archiv: " + f.name);
                    loadMapFromBytes(data, f.name);
                    // Eine Karte im Archiv hat keinen Pfad auf der Platte;
                    // beim naechsten Start liesse sie sich so nicht wieder
                    // finden. Deshalb nicht merken.
                    g_app->settings.mapPath.clear();
                } else {
                    const std::string dat = data;
                    const std::string nm = f.name;
                    withUnsaved([dat, nm] {
                        // .ibi kommt als Binaerdatei - erst zurueckuebersetzen.
                        openScriptFromMemory(dat, nm);
                    });
                }
                g_app->pk3Open = false;
                ImGui::CloseCurrentPopup();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tr(Str::Pk3FromArchive), f.archive.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button(tr(Str::EditorCancel))) {
        g_app->pk3Open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawAbout() {
    if (!g_app->aboutOpen) {
        return;
    }
    ImGui::OpenPopup(tr(Str::AboutTitle));
    if (!ImGui::BeginPopupModal(tr(Str::AboutTitle), &g_app->aboutOpen,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    // Das Logo aus BITMAP 133 des Originals, in der Groesse der Schrift
    // mitwachsend.
    if (void* logo = logoTextureId(); logo != nullptr) {
        const float scale = ImGui::GetFontSize() / 16.0F;
        ImGui::Image(reinterpret_cast<ImTextureID>(logo),
                     ImVec2{static_cast<float>(icons::kLogoWidth) * scale,
                            static_cast<float>(icons::kLogoHeight) * scale});
    }
    ImGui::Text("%s 1.0.0", tr(Str::AppTitle));
    ImGui::TextUnformatted(tr(Str::AboutOriginal));
    ImGui::Separator();

    // Woran das Modell gemessen ist. Das gehoert hierher, weil es die
    // Frage beantwortet, der man einem Nachbau sonst nicht ansieht:
    // stimmt er ueberhaupt?
    ImGui::TextUnformatted(tr(Str::AboutVerified));
    ImGui::BulletText(tr(Str::AboutModel),
                      static_cast<int>(g_app->db.commands.size()),
                      static_cast<int>(g_app->db.typesets.size()),
                      static_cast<int>(g_app->db.macros.size()));
    ImGui::BulletText("%s", tr(Str::AboutResources));
    ImGui::BulletText("%s", tr(Str::AboutIbi));
    ImGui::BulletText("%s", tr(Str::AboutIcarus));

    ImGui::Separator();
    if (ImGui::Button(tr(Str::AboutClose), ImVec2{ImGui::GetFontSize() * 6.0F, 0.0F})) {
        g_app->aboutOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// --- Hauptfenster ------------------------------------------------------

// --- Dateiverwaltung ---------------------------------------------------

// Movie Duels legt seine Skripte als .txt ab - so heisst die Datei auch im
// Bildschirmfoto ("intro_jedi.txt"). Beide Endungen anbieten.
extern const char* kScriptFilter;
// .ibi gehoert dazu.
//
// Gelesen werden sie laengst - openScriptFromMemory erkennt sie am INHALT
// ("IBI" am Anfang), nicht an der Endung, weil sie in Movie Duels mal .IBI
// und mal .ibi heissen. Nur im Oeffnen-Dialog standen sie nicht, und damit
// waren sie ueber "Open" nicht erreichbar: gemeldet als "Can only open .txt
// and .icarus file".
//
// Eine eigene Zeile fuer .ibi zusaetzlich zur gemeinsamen, damit man im
// Dialog gezielt nur die kompilierten sehen kann.
// Es gibt genau ZWEI Arten, und .icarus gehoert nicht dazu.
//
// Vom Anwender richtiggestellt: ".ibi = uebersetztes Skript, das das Spiel
// liest. .txt = so sichert man sein Skript, und behaved kann es wieder
// oeffnen." Mehr nicht.
//
// ".icarus" stand hier als vermeintlicher Name der Sprache - die Sprache
// heisst so, die Dateien nicht. Das Original bot ausserdem .py an; das
// benutzt niemand, und es steht deshalb nur im gemeinsamen Filter.
const char* kScriptFilter =
    "ICARUS scripts (*.txt;*.ibi)|*.txt;*.ibi|"
    "Script source (*.txt)|*.txt|"
    "Compiled scripts (*.ibi)|*.ibi|"
    "All files (*.*)|*.*";




// Die Beschreibung eines Befehls - mit Ersatz, wo die .bhc schweigt.
//
// Vier Befehle haben dort keine: camera, task, do und play. Das ist eine
// Luecke in Ravens Datei, kein Fehler bei uns - das Original zeigt ebenso
// nichts. Weil aber die anderen fuenfundzwanzig eine haben, sieht es nach
// einem Fehler aus, und gerade bei camera erfaehrt man nichts ueber den
// wichtigsten Befehl im Skript.
//
// Was in der .bhc steht, hat immer Vorrang. Der Ersatz greift nur bei
// Leere - eine spaetere .bhc mit eigenen Texten setzt sich also durch.
std::string descFor(const Command& c) {
    if (!c.desc.empty()) {
        return c.desc;
    }
    if (c.name == "camera") { return tr(Str::DescCamera); }
    if (c.name == "task") { return tr(Str::DescTask); }
    if (c.name == "do") { return tr(Str::DescDo); }
    if (c.name == "play") { return tr(Str::DescPlay); }
    return std::string();
}

bool spew(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}

// Eine Zeile in die Statusliste, mit laufender Nummer wie im Original.


bool doSaveAs();

// --- Speichern schreibt die QUELLE, Uebersetzen das Ergebnis -----------
//
// So macht es BehavEd, und so will es shank:
//
//     Speichern   -> .txt   (der lesbare Quelltext)
//     Uebersetzen -> .ibi   UND die .txt dazu
//
// Bis rc458 schrieb `doSave` den Quelltext in `g_app->path` - was immer
// dort stand. Wer eine .ibi geoeffnet hatte, bekam den TEXT in einer Datei
// namens .ibi geschrieben: das Ergebnis war weg, und die Quelle hiess
// falsch. shank: "when I do save as to save my script as a .txt file, it
// doesn't work. It only saves as a .ibi file."
//
// Die .txt wird nicht mehr gebraucht - dieses Programm liest .ibi
// unmittelbar -, aber sie ist im Editor die einzige Fassung, die man in
// einem Texteditor ansehen und aendern kann. Deshalb bleibt sie.
std::string mitEndung(const std::string& pfad, const char* neu) {
    const std::size_t punkt = pfad.find_last_of('.');
    const std::size_t schraeg = pfad.find_last_of("/\\");
    // Ein Punkt im Verzeichnisnamen ist keine Endung.
    const bool hatEndung =
        (punkt != std::string::npos) &&
        (schraeg == std::string::npos || punkt > schraeg);
    return (hatEndung ? pfad.substr(0, punkt) : pfad) + neu;
}

// --- Rollende Sicherung der letzten 10 Speicherungen ----------------------
//
// shank, 02.10.: "Might be a good idea to have a backup of the .txt file
// after each save ... a rolling backup system of the last 10 saves/compiles
// named backup1.txt, backup2.txt etc." - neben der .exe, in
// backup/<Skriptname>/. backup1.txt ist die neueste. Gleicher Inhalt wie die
// letzte Sicherung (Kompilieren ohne Aenderung) gibt keine neue.
std::string sicherungsOrdner(const std::string& quelle) {
    std::string name = fileName(quelle);
    const std::size_t punkt = name.find_last_of('.');
    if (punkt != std::string::npos && punkt > 0) { name.resize(punkt); }
    if (name.empty()) { name = "unnamed"; }
    return platform::executableDirectory() + "/backup/" + name;
}

void sicherungAnlegen(const std::string& quelle, const std::string& inhalt) {
    namespace fs = std::filesystem;
    constexpr int kAnzahl = 10;
    std::error_code ec;
    const std::string ordner = sicherungsOrdner(quelle);
    fs::create_directories(fs::u8path(ordner), ec);
    const auto datei = [&](int n) { return ordner + "/backup" + std::to_string(n) + ".txt"; };
    if (fs::exists(fs::u8path(datei(1)), ec) && slurp(datei(1)) == inhalt) {
        return;
    }
    fs::remove(fs::u8path(datei(kAnzahl)), ec);
    for (int n = kAnzahl - 1; n >= 1; --n) {
        if (fs::exists(fs::u8path(datei(n)), ec)) {
            fs::rename(fs::u8path(datei(n)), fs::u8path(datei(n + 1)), ec);
        }
    }
    if (!spew(datei(1), inhalt)) {
        diag::detail("Sicherung: " + datei(1) + " nicht schreibbar");
        return;
    }
    diag::detail("Sicherung: " + datei(1));
}

void sicherungsOrdnerOeffnen() {
    const std::string o = g_app->path.empty() ? platform::executableDirectory() + "/backup"
                                              : sicherungsOrdner(g_app->path);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(o), ec);
    platform::openInExplorer(o);
}

bool doSave() {
    if (g_app->path.empty()) {
        return doSaveAs();
    }
    // IMMER als .txt. Auch wenn eine .ibi geoeffnet war - dann entsteht die
    // Quelle daneben, mit demselben Namen.
    const std::string quelle = mitEndung(g_app->path, ".txt");
    // Schreibgeschuetzt? Das Original bietet an, den Schutz aufzuheben:
    // "The file "%s" is write-protected ... Do you want me to
    // un-writeprotect it so you can save over it? ('No' will abort the
    // save)". behaved meldete nur einen Fehler.
    {
        std::error_code ec;
        const std::filesystem::path fp = std::filesystem::u8path(quelle);
        if (std::filesystem::exists(fp, ec) &&
            (std::filesystem::status(fp, ec).permissions() &
             std::filesystem::perms::owner_write) == std::filesystem::perms::none) {
            char text[900];
            std::snprintf(text, sizeof(text), tr(Str::AskUnprotect), quelle.c_str());
            frage(text, [fp] {
                std::error_code ec2;
                std::filesystem::permissions(fp, std::filesystem::perms::owner_write,
                                             std::filesystem::perm_options::add, ec2);
                if (ec2) {
                    addStatus("Failed to remove write protect, aborting...");
                    return;
                }
                if (doSave()) {
                    addStatus(tr(Str::MsgSaved));
                }
            }, [] { addStatus("(File was not write-enabled, aborting save)"); });
            return false;
        }
    }
    const std::string inhalt = writeScript(g_app->doc.script());
    if (!spew(quelle, inhalt)) {
        platform::showError(quelle, tr(Str::AppTitle));
        return false;
    }
    sicherungAnlegen(quelle, inhalt);
    // Ab jetzt ist die Quelle die Datei, an der gearbeitet wird. Sonst
    // schriebe das naechste Speichern wieder neben die .ibi.
    if (quelle != g_app->path) {
        g_app->path = quelle;
        g_app->settings.noteRecent(quelle);
    }
    g_app->doc.markSaved();
    // Aenderungsrand: was bis hierher orange war, wird gruen. Und die
    // Lesezeichen gelten ab jetzt fuer diese Fassung der Datei.
    aenderungenGespeichert();
    lesezeichenMerken();
    return true;
}

bool doSaveAs() {
    const std::string p = platform::saveFileDialog(
        tr(Str::FileSaveAs), kScriptFilter,
        g_app->path.empty() ? g_app->settings.scriptPath : directoryOf(g_app->path),
        g_app->path.empty() ? "unnamed.txt" : fileName(g_app->path));
    if (p.empty()) {
        return false;
    }
    g_app->path = p;
    g_app->settings.noteRecent(p);
    return doSave();
}


// Die Karte wegraeumen.
//
// Herausgezogen aus dem Menueeintrag "Karte schliessen", weil es jetzt
// einen zweiten Aufrufer gibt: den Reiterwechsel. Ein Reiter ohne Karte
// soll auch keine zeigen.
//
// Die Zuordnung effectShaders zeigt in textures.byShader; wird die Liste
// geleert, sind die Nummern wertlos, und ein Partikel truege die Textur
// einer Wand. Deshalb muss sie MIT weg.
void clearMap() {
    vergissEffektBahnen();
    g_app->map = MapData{};
    g_app->geo = BspGeometry{};
    g_app->mesh = BspMesh{};
    g_app->textures = TextureSet{};
    gpu::vergissKarte();
    g_app->effectShaders.clear();
    g_app->effectRunners.clear();
    g_app->moverSim = MoverSim{};
    g_app->moverKlaenge.clear();
    g_app->effects.clear();
    g_app->mapModels.clear();
    g_app->nebenModelle.clear();
    g_app->brushMeshes.clear();
    g_app->md3Files.clear();
    g_app->md3Meshes.clear();
    g_app->settings.mapPath.clear();
    // Alles, was an der Karte hing, mit weg: Klickziele, Auswahl (als
    // Index!), Kamera- und Schluesselwahl, die GPU-Ueberlagerung. Ohne
    // Karte zeichnet die Ansicht nur den Platzhalter, also wurde nichts
    // davon mehr ueberschrieben (Kartentest 27.09.).
    g_app->markers.clear();
    g_app->keyMarks.clear();
    g_app->actorMarks.clear();
    g_app->cameraSx = -1.0F;
    g_app->cameraSy = -1.0F;
    g_app->pickedEntity = -1;
    g_app->hiddenEntities.clear();
    g_app->cameraSelected = false;
    g_app->selectedKey = -1;
    g_app->mapBereit = false;
    g_app->gizmoZeigen = false;
    g_app->camTrackValid = false;
    g_app->mapDirty = true;
}

// --- Reiter: parken, holen, umschalten -----------------------------------
//
// parkActive() legt das lebende Dokument in seinen Reiter zurueck,
// takeTab() holt eines heraus. Dazwischen wird der Baum neu gebaut, weil
// rows/selected daran haengen.
void parkActive(bool layoutMerken = true) {
    if (g_app->tabs.empty()) {
        return;
    }
    const std::size_t at = static_cast<std::size_t>(
        std::clamp(g_app->activeTab, 0,
                   static_cast<int>(g_app->tabs.size()) - 1));
    App::Parked& t = g_app->tabs[at];
    t.doc = g_app->doc;
    t.path = g_app->path;
    t.selection = g_app->selection;
    t.selectedPath = g_app->selectedPath;
    t.selected = g_app->selected;
    t.expanded = g_app->expanded;
    t.shownName = g_app->shownName;
    t.skriptPfad = g_app->skriptPfad;
    // Die Karte nur merken, wenn sie IHM gehoert - siehe ownMap.
    if (t.ownMap) {
        t.mapPath = g_app->settings.mapPath;
        t.mapId = g_app->map.path;
    }
    // Die Sicht mitparken.
    for (int k = 0; k < 3; ++k) {
        t.camPos[k] = g_app->cam.pos[k];
        t.camAng[k] = g_app->cam.angles[k];
    }
    t.playMs = g_app->playMs;
    t.camSaved = true;
    // Die Feldaufteilung gehoert dem Reiter - aber NUR beim echten
    // Reiterwechsel. Der Fokuswechsel zwischen Feldern parkt auch, und
    // dabei darf sich die Aufteilung nicht bewegen: sonst klappte ein
    // Klick ins Nachbarfeld die Ansicht um.
    if (layoutMerken) {
        // In den BESITZER, nicht in den aktiven Reiter.
        //
        // Sonst bekommt jeder Reiter, in dem man nur kurz gearbeitet hat,
        // die Aufteilung angehaengt - und ploetzlich sind alle vier
        // geteilt, obwohl nur einer es sein sollte.
        const int besitzer =
            std::clamp(g_app->homeTab, 0,
                       static_cast<int>(g_app->tabs.size()) - 1);
        App::Parked& h = g_app->tabs[static_cast<std::size_t>(besitzer)];
        h.splitCount = g_app->splitCount;
        h.splitFocus = g_app->focusPane;
        for (int i = 0; i < App::kMaxSplit; ++i) {
            h.splitTabs[i] = (i == g_app->focusPane)
                                 ? g_app->activeTab
                                 : g_app->splitPanes[i].tab;
        }
    }
}

void takeTab(int index, bool rollen = true, bool layout = true) {
    if (g_app->tabs.empty()) {
        return;
    }
    const int n = static_cast<int>(g_app->tabs.size());
    g_app->activeTab = std::clamp(index, 0, n - 1);
    App::Parked& t =
        g_app->tabs[static_cast<std::size_t>(g_app->activeTab)];
    g_app->doc = t.doc;
    g_app->path = t.path;
    g_app->selection = t.selection;
    g_app->selectedPath = t.selectedPath;
    g_app->selected = t.selected;
    g_app->expanded = t.expanded;
    g_app->shownName = t.shownName;
    g_app->skriptPfad = t.skriptPfad;

    // Die Aufteilung DIESES Reiters uebernehmen. Ein Reiter, der nie
    // geteilt war, bringt EIN Feld mit - genau das Gewuenschte beim
    // Wechsel "in den nebenan".
    if (layout) {
        const int n2 = static_cast<int>(g_app->tabs.size());
        g_app->splitCount = std::clamp(t.splitCount, 1, App::kMaxSplit);
        g_app->focusPane =
            std::clamp(t.splitFocus, 0, g_app->splitCount - 1);
        for (int i = 0; i < App::kMaxSplit; ++i) {
            g_app->splitPanes[i].tab =
                std::clamp(t.splitTabs[i], 0, std::max(0, n2 - 1));
            g_app->splitPanes[i].dirty = true;
        }
    }

    // Die Karte nachziehen - aber nur, wenn es eine andere ist.
    //
    // Beim Laden einer Mission zeigen alle Reiter auf dieselbe .bsp; dann
    // faellt hier nichts an. Ein neuer, leerer Reiter hat keine, und dann
    // wird die alte weggeraeumt - sonst steht dort die Karte des vorigen
    // Skripts, und das war genau die Meldung.
    // --- Eine Karte, mehrere Skripte -------------------------------------
    //
    // Gewuenscht: "eine Map kann mehrere Skripte haben und drueber laufen,
    // deshalb duerfen sich mehrere Skripte doch eine teilen. Wuerde aber
    // auch gerne behalten, dass man pro Tab einzelne aufmachen kann."
    //
    // Beides geht, wenn man unterscheidet, ob ein Reiter eine EIGENE Karte
    // hat:
    //
    //   Reiter MIT eigener Karte (Mission geladen, .bsp geoeffnet)
    //       -> beim Hinwechseln wird sie geholt. Sein gutes Recht.
    //   Reiter OHNE eigene Karte (Skript einfach so geoeffnet)
    //       -> die vorhandene bleibt stehen. Er hat keine Meinung dazu,
    //          und die des Arbeitsbereichs ist besser als gar keine.
    //
    // Vorher wurde auch der zweite Fall behandelt, als haette er eine
    // Meinung - naemlich "keine" - und die Karte flog weg. Beim
    // Zurueckwechseln wurde sie neu gelesen: 35 bis 50 ms, hin und zurueck,
    // bei jedem Klick.
    //
    // Verglichen wird die KENNUNG, nicht der Plattenpfad: eine Mission aus
    // einem .pk3 hat gar keinen.
    if (!t.mapId.empty() && t.mapId != g_app->map.path) {
        char zeile[300];
        std::snprintf(zeile, sizeof(zeile),
                      "Reiter %d: Karte wechseln von \"%s\" nach \"%s\" "
                      "(Pfad \"%s\")",
                      index, g_app->map.path.c_str(), t.mapId.c_str(),
                      t.mapPath.c_str());
        diag::detail(zeile);
        if (!t.mapPath.empty()) {
            loadMap(t.mapPath);
        } else if (!loadMapFromArchive(t.mapId)) {
            // Aus dem Archiv nicht mehr lesbar - dann lieber gar keine als
            // die falsche.
            clearMap();
        }
    }
    // Und die Sicht wiederherstellen - aber nur, wenn dieser Reiter schon
    // einmal eine hatte. Ein frischer bekommt die Ansicht, die das Laden
    // der Karte einstellt, und nicht die Nullstellung.
    if (t.camSaved) {
        for (int k = 0; k < 3; ++k) {
            g_app->cam.pos[k] = t.camPos[k];
            g_app->cam.angles[k] = t.camAng[k];
        }
        g_app->playMs = t.playMs;
        g_app->playing = false;
        g_app->mapDirty = true;
    }
    rebuildTree();
    // Zur Auswahl rollen: ja beim REITERWECHSEL, nein beim Fokuswechsel.
    //
    // Wechselt man den Reiter, will man sehen, wo man dort stehengeblieben
    // ist. Wechselt man nur den Fokus von einem Feld zum anderen, hat sich
    // im Inhalt nichts geaendert - und ein Sprung zur Auswahl wirkt wie ein
    // Fehler. Genau so wurde es gemeldet: "springt am anderen fenster etwas
    // umher".
    // ... aber NUR, wenn der Reiter noch keine gemerkte Rollposition hat.
    //
    // shank, 27.09.: "beim allersten mal ziehen springt er einmal". Die
    // gemerkte Position stellt rollpositionHolen exakt wieder her; das
    // Zentrieren auf die Auswahl schob sie danach noch einmal weg. Und war
    // im Reiter nichts gewaehlt, blieb das Flag liegen - bis zum ersten
    // Klick auf eine Zeile, die dann in die Mitte sprang.
    if (rollen && g_app->tabScroll.find(tabKennung(index)) == g_app->tabScroll.end()) {
        g_app->scrollToSelected = true;
    }
    g_app->mapDirty = true;
}

// Auf einen anderen Reiter wechseln.
void switchTab(int index, bool rollen = true, bool layout = true) {
    if (index == g_app->activeTab) {
        return;
    }
    // --- Die Zwischenablage folgt dem ANWENDER ---------------------------
    //
    // Gemeldet: Kopieren in einem Skript und Einfuegen in einem anderen geht
    // nicht.
    //
    // Der Grund: die Ablage gehoert dem DOKUMENT, und jeder Reiter hat sein
    // eigenes. Was man links kopiert, liegt rechts nicht.
    //
    // Sie gehoert aber zum Anwender, nicht zur Datei - so hat es jedes
    // Programm, in dem man mit mehreren Dateien arbeitet. Also wird sie beim
    // Wechsel mitgenommen.
    //
    // Nur wenn sie etwas ENTHAELT: sonst loeschte ein Wechsel aus einem
    // frischen Reiter die Ablage des Ziels.
    std::vector<Node> mitnehmen = g_app->doc.clipboard();
    parkActive(layout);
    takeTab(index, rollen, layout);
    if (!mitnehmen.empty()) {
        g_app->doc.setClipboard(std::move(mitnehmen));
    }
    // Das Feld mit dem Fokus zeigt jetzt diesen Reiter - aber NUR beim
    // Fokuswechsel.
    //
    // Wird die Aufteilung mitgeholt (layout), dann steht die Belegung
    // schon in ihr, und dieses Nachziehen wuerde sie zerstoeren: das
    // fokussierte Feld bekaeme den Reiter, den ein anderes Feld ohnehin
    // schon zeigt. Genau so entstand die Dopplung - zweimal dasselbe
    // Skript nebeneinander, "was nicht gehen sollte".
    if (!layout) {
        g_app->splitPanes[std::clamp(g_app->focusPane, 0, App::kMaxSplit - 1)]
            .tab = g_app->activeTab;
    }
    // --- Kein Feld zweimal dasselbe Skript --------------------------------
    //
    // Ein Riegel, kein Kommentar: die Dopplung ist auf mehreren Wegen
    // entstanden (rc179 ueber neue Felder, jetzt ueber den Reiterwechsel),
    // und sie faellt im Bild kaum auf - zwei gleiche Baeume nebeneinander
    // sehen aus wie Absicht.
    //
    // Das FOKUSSIERTE Feld behaelt seinen Reiter; ein anderes, das denselben
    // zeigt, bekommt den ersten freien. Gibt es keinen, bleibt es stehen -
    // dann sind mehr Felder als Reiter offen, und darueber beschwert sich
    // die Selbstpruefung aus rc218.
    {
        const int fp2 = std::clamp(g_app->focusPane, 0, App::kMaxSplit - 1);
        for (int i2 = 0; i2 < g_app->splitCount; ++i2) {
            if (i2 == fp2) {
                continue;
            }
            bool doppelt = false;
            for (int j2 = 0; j2 < g_app->splitCount; ++j2) {
                if (j2 != i2 &&
                    g_app->splitPanes[j2].tab == g_app->splitPanes[i2].tab) {
                    doppelt = true;
                    break;
                }
            }
            if (!doppelt) {
                continue;
            }
            for (int k2 = 0; k2 < static_cast<int>(g_app->tabs.size()); ++k2) {
                bool belegt = false;
                for (int j2 = 0; j2 < g_app->splitCount; ++j2) {
                    if (g_app->splitPanes[j2].tab == k2) {
                        belegt = true;
                        break;
                    }
                }
                if (!belegt) {
                    char z2[200];
                    std::snprintf(z2, sizeof(z2),
                                  "Feld %d zeigte denselben Reiter wie ein "
                                  "anderes - auf Reiter %d umgestellt",
                                  i2, k2);
                    diag::detail(z2);
                    g_app->splitPanes[i2].tab = k2;
                    break;
                }
            }
        }
    }
    for (auto& pn : g_app->splitPanes) {
        pn.dirty = true;
    }
}

// Einen neuen Reiter anlegen und hineinwechseln.
//
// Der aktuelle wird vorher geparkt - er bleibt also erhalten, samt
// Rueckgaengig-Speicher und Auswahl. Genau das war der Wunsch: "Best to
// only have 1 instance of the program open."
void addTab(bool inheritMap) {
    parkActive();
    App::Parked frisch;
    // Ein Reiter, der aus einem geladenen Skript entsteht, erbt die Karte:
    // beim Laden einer Mission gehoeren alle Skripte zu derselben .bsp.
    //
    // Ein Reiter, den man mit dem Pluszeichen anlegt, erbt sie NICHT - er
    // ist ein neues, leeres Blatt. Genau das war die Meldung: "wenn ich
    // einen neuen tab öffne ist die map vom letzten map noch geladen".
    if (inheritMap) {
        frisch.mapPath = g_app->settings.mapPath;
        frisch.mapId = g_app->map.path;
    }
    g_app->tabs.push_back(frisch);
    g_app->activeTab = static_cast<int>(g_app->tabs.size()) - 1;
    // Ein neuer Reiter ist auch der neue Arbeitsbereich - sonst bliebe das
    // Band auf dem alten stehen, obwohl man gerade anderswo landet.
    g_app->homeTab = g_app->activeTab;
    // Die Karte JETZT mit dem neuen Reiter in Einklang bringen.
    //
    // Die Absicht stand schon da - ein neuer Reiter erbt die Karte nicht -,
    // aber sie stand nur in `frisch.mapPath`. Weggeraeumt wurde die alte
    // nie: addTab setzt activeTab von Hand und geht NICHT ueber takeTab(),
    // wo die Karte sonst nachgezogen wird. Also blieb sie stehen, bis man
    // den Reiter einmal verliess und zurueckkam - dann fiel sie ploetzlich
    // weg. Gemeldet als "ich habe einen neuen tab erstellt und die map vom
    // ersten tab ist immer noch drin".
    //
    // Hier und nicht beim Aufrufer, damit beide Wege - das Pluszeichen und
    // "Neu" - dasselbe tun und nicht auseinanderlaufen koennen.
    if (frisch.mapId != g_app->map.path) {
        if (frisch.mapId.empty()) {
            clearMap();
        } else if (!frisch.mapPath.empty()) {
            loadMap(frisch.mapPath);
        }
    }
    // Der neue Reiter bringt sein eigenes, ungeteiltes Fenster mit.
    g_app->splitCount = 1;
    g_app->focusPane = 0;
    g_app->splitPanes[0].tab = g_app->activeTab;
    for (auto& pn : g_app->splitPanes) {
        pn.dirty = true;
    }
}

// Einen Reiter wirklich wegnehmen - ohne zu fragen.
void dropTab(int index) {
    const int n = static_cast<int>(g_app->tabs.size());
    if (index < 0 || index >= n) {
        return;
    }
    // --- Den aktiven Reiter VORHER parken ---------------------------------
    //
    // shank, 02.10.: "I closed some scripts/tabs I was no longer using and
    // went back to continue working on the script but it had reset. All my
    // changes were lost with no undo/redo history."
    //
    // Der aktive Reiter lebt in g_app->doc; seine Kopie in tabs[] ist der
    // Stand beim letzten Reiterwechsel. Unten holt takeTab() den Reiter aus
    // dieser Kopie zurueck - ohne Parken vorher war alles seit dem letzten
    // Wechsel weg, samt Undo. Und lag der geschlossene Reiter LINKS, rutschte
    // die Nummer nicht nach: geholt wurde der Nachbar.
    const bool andererReiter = (index != g_app->activeTab);
    if (andererReiter) {
        parkActive(false);
    }
    g_app->tabs.erase(g_app->tabs.begin() + index);
    if (g_app->tabs.empty()) {
        g_app->tabs.push_back(App::Parked{});
    }
    if (andererReiter && index < g_app->activeTab) {
        --g_app->activeTab;
    }
    g_app->activeTab = std::clamp(g_app->activeTab, 0,
                                  static_cast<int>(g_app->tabs.size()) - 1);
    // Der Besitzer der Aufteilung genauso - er ist eine Reiternummer, und
    // die verschieben sich beim Loeschen (dieselbe Falle wie bei den
    // Feldern, siehe unten).
    if (g_app->homeTab > index) {
        --g_app->homeTab;
    }
    g_app->homeTab = std::clamp(g_app->homeTab, 0,
                                static_cast<int>(g_app->tabs.size()) - 1);
    // Die Aufteilung mit schrumpfen lassen.
    //
    // Bleibt sie stehen, zeigt ein Feld nach dem Schliessen zwangslaeufig
    // dasselbe Skript wie ein anderes - genau die Dopplung, die es nicht
    // geben soll. Die Felder zeigen ausserdem auf Reiternummern, und die
    // haben sich beim Loeschen verschoben.
    const int uebrig = static_cast<int>(g_app->tabs.size());
    g_app->splitCount = std::clamp(g_app->splitCount, 1,
                                   std::max(1, uebrig));
    g_app->focusPane = std::clamp(g_app->focusPane, 0, g_app->splitCount - 1);
    for (auto& pn : g_app->splitPanes) {
        // Wer rechts vom geschlossenen stand, rueckt eins nach links.
        if (pn.tab > index) { --pn.tab; }
        pn.tab = std::clamp(pn.tab, 0, std::max(0, uebrig - 1));
        pn.dirty = true;
    }
    // layout=false: die Aufteilung ist eben schon nachgefuehrt.
    takeTab(g_app->activeTab, true, !andererReiter);
}

// Einen Reiter schliessen - mit Nachfrage, falls noetig.
//
// Der ungesicherte Reiter wird zuerst SICHTBAR gemacht und dann gefragt.
// Man soll sehen, worueber man entscheidet.
void closeTab(int index) {
    const int n = static_cast<int>(g_app->tabs.size());
    if (index < 0 || index >= n) {
        return;
    }
    if (index != g_app->activeTab &&
        g_app->tabs[static_cast<std::size_t>(index)].doc.dirty()) {
        switchTab(index);
    }
    if (index == g_app->activeTab) {
        const int idx = index;
        withUnsaved([idx] { dropTab(idx); });
        return;
    }
    dropTab(index);
}

void loadPath(const std::string& p) {
    diag::Step step("Skript oeffnen: " + p);
    const std::string src = slurp(p);
    if (src.empty()) {
        platform::showError(p, tr(Str::AppTitle));
        return;
    }
    Script sc;
    std::vector<Diag> d;
    // --- Erst schauen, WAS in der Datei steht -----------------------------
    //
    // Gemeldet: eine .ibi laesst sich oeffnen, aber danach ist nichts drin.
    //
    // Der Grund: hier stand nur readScript(). Eine .ibi ist aber keine
    // Textdatei, sondern uebersetzter Bytecode - der Textleser findet darin
    // keinen einzigen Befehl und liefert ein leeres Skript. Kein Absturz,
    // keine Meldung, nur ein leerer Baum.
    //
    // Der Weg ueber den Speicher (aus einer Mission, openScriptFromMemory)
    // konnte es laengst; nur dieser hier nicht. Wieder zwei Wege, die
    // dasselbe leisten sollten, und einer tat es nur halb.
    //
    // Erkannt wird am INHALT, nicht an der Endung: eine .ibi kann anders
    // heissen, und eine .txt kann uebersetzt sein. Genau so macht es der
    // andere Weg auch.
    const bool istIbi = src.size() > 4 && src.compare(0, 3, "IBI") == 0;
    if (istIbi) {
        std::vector<IbiBlock> bl;
        if (readIbi(src, bl, d)) {
            (void)decompile(bl, g_app->db, sc, d);
        }
        diag::detail("Geoeffnet als IBI: " + std::to_string(bl.size()) +
                     " Bloecke -> " + std::to_string(sc.nodes.size()) +
                     " Knoten");
    } else {
        (void)readScript(src, sc, d);
    }
    // In einen NEUEN Reiter, ausser der aktuelle ist noch unberuehrt.
    //
    // Sonst verliert man beim Oeffnen, woran man gerade arbeitet - und
    // genau das war der Grund fuer mehrere Programmfenster: "this is how I
    // usually work with multiple scripts open where I copy and paste
    // commands between them."
    //
    // Ein leerer, ungeaenderter Reiter wird wiederverwendet: wer gerade
    // gestartet hat, will keinen zweiten.
    const bool leerUndSauber =
        g_app->path.empty() && !g_app->doc.dirty() &&
        g_app->doc.script().nodes.empty();
    if (!leerUndSauber) {
        addTab(true);
        takeTab(g_app->activeTab);
    }
    g_app->doc = Document{sc};
    g_app->path = p;
    g_app->shownName.clear();   // der Pfad genuegt
    g_app->skriptPfad.clear();  // kommt aus dem Pfad
    g_app->selectedPath.clear();
    g_app->selection.clear();
    g_app->selected = -1;
    g_app->expanded = Expanded{};
    g_app->settings.noteRecent(p);
    // Frisch geladen: kein Aenderungsrand, die gemerkten Lesezeichen zurueck.
    aenderungsStandNeu();
    lesezeichenLaden();
    rebuildTree();
    setStatus(tr(Str::MsgLoaded), static_cast<int>(g_app->rows.size()), 0);
}

void doOpenOhneFrage() {
    withUnsaved([] {
    const std::string start = (g_app->settings.dialogUsesLastDir &&
                               !g_app->settings.lastDir.empty())
                                  ? g_app->settings.lastDir
                                  : g_app->settings.scriptPath;
    const std::string p = platform::openFileDialog(tr(Str::FileOpen), kScriptFilter, start);
    if (!p.empty()) {
        g_app->settings.lastDir = directoryOf(p);
        loadPath(p);
    }
    });
}

// Oeffnen fragt NICHT mehr "Open?".
//
// Das Original tat es mit "Yes/No query on Open/New/Exit". shank: "I don't
// think a confirmation window is needed for opening a script". Oeffnen
// verwirft nichts - ungespeicherte Aenderungen fragt withUnsaved ohnehin
// ab, und der Dateidialog laesst sich abbrechen. New und Exit fragen
// weiterhin, wenn die Einstellung an ist.
void doOpen() {
    doOpenOhneFrage();
}

void neuesSkript();

// "New?" - nur mit "Yes/No query on Open/New/Exit" (Original).
void doNew() {
    const bool leerUndSauber =
        g_app->path.empty() && g_app->shownName.empty() &&
        !g_app->doc.dirty() && g_app->doc.script().nodes.empty();
    if (leerUndSauber) {
        return;
    }
    if (g_app->settings.queryOnDiscard) {
        frage(tr(Str::AskNew), [] { neuesSkript(); });
        return;
    }
    neuesSkript();
}

void neuesSkript() {
    // In einen NEUEN Reiter, wie beim Oeffnen.
    //
    // Gemeldet als: "If I click the New button in the Actions panel, it
    // should open a new tab with an empty script." Richtig - seit es Reiter
    // gibt, ist das Ueberschreiben des aktuellen die falsche Handlung.
    //
    // Ein leerer, unberuehrter Reiter wird wiederverwendet: wer gerade
    // gestartet hat, braucht keinen zweiten. Und weil dann nichts
    // wegzuwerfen ist, entfaellt auch die Frage.
    const bool leerUndSauber =
        g_app->path.empty() && g_app->shownName.empty() &&
        !g_app->doc.dirty() && g_app->doc.script().nodes.empty();
    if (leerUndSauber) {
        return;
    }
    // Die Karte NICHT erben: ein leeres Skript hat keinen Bezug zu der
    // Karte, die im vorigen Reiter stand.
    addTab(false);
    g_app->doc = Document{Script{}};
    g_app->path.clear();
    g_app->shownName.clear();
    g_app->skriptPfad.clear();
    g_app->selectedPath.clear();
    g_app->selection.clear();
    g_app->selected = -1;
    g_app->expanded = Expanded{};
    aenderungsStandNeu();
    g_app->lesezeichen.erase(tabKennung(g_app->activeTab));
    rebuildTree();
}

// Alle Reiter sichern.
//
// Gewuenscht als "Save All". Der aktive wird zuletzt gesichert, damit man
// am Ende dort steht, wo man angefangen hat - ein Reiterwechsel mitten im
// Sichern waere sonst eine unangenehme Ueberraschung.
//
// Reiter ohne Pfad (aus einem Archiv geladen) werden UEBERSPRUNGEN: dorthin
// laesst sich nicht zurueckschreiben, und ein Dateidialog je Reiter waere
// das Gegenteil von "alle sichern".
void doSaveAll() {
    const int vorher = g_app->activeTab;
    int gesichert = 0;
    int ohnePfad = 0;
    for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
        const bool aktiv = (static_cast<int>(i) == g_app->activeTab);
        const bool schmutzig =
            aktiv ? g_app->doc.dirty() : g_app->tabs[i].doc.dirty();
        const bool hatPfad =
            aktiv ? !g_app->path.empty() : !g_app->tabs[i].path.empty();
        if (!schmutzig) {
            continue;
        }
        if (!hatPfad) {
            ++ohnePfad;
            continue;
        }
        switchTab(static_cast<int>(i));
        if (doSave()) {
            ++gesichert;
        }
    }
    switchTab(vorher);
    setStatus(tr(Str::MsgSavedAll), gesichert, ohnePfad);
}

// "Append" des Originals: ein zweites Skript hinten anhaengen.
void doAppend() {
    const std::string p = platform::openFileDialog(tr(Str::FileAppend), kScriptFilter,
                                                   g_app->settings.scriptPath);
    if (p.empty()) {
        return;
    }
    const std::string daten = slurp(p);
    Script other;
    std::vector<Diag> d;
    // Der Filter bietet .ibi an - also auch .ibi lesen (wie beim Oeffnen).
    if (daten.size() > 4 && daten.compare(0, 3, "IBI") == 0) {
        std::vector<IbiBlock> bl;
        if (readIbi(daten, bl, d)) {
            (void)decompile(bl, g_app->db, other, d);
        }
    } else {
        (void)readScript(daten, other, d);
    }
    // Die Trennzeilen des Originals davor:
    //     "   Appended File (%s) follows...  "
    //     "============================="
    // Hier als Kommentarzeilen - sie gehoeren nicht zum Skriptablauf.
    std::vector<Node> neu;
    {
        Node kopf;
        kopf.kind = Node::Kind::LineComment;
        kopf.raw = "//   Appended File (" + fileName(p) + ") follows...  ";
        Node linie;
        linie.kind = Node::Kind::LineComment;
        linie.raw = "//=============================";
        neu.push_back(std::move(kopf));
        neu.push_back(std::move(linie));
    }
    for (const Node& n : other.nodes) {
        neu.push_back(n);
    }
    Path at;
    if (!g_app->doc.script().nodes.empty()) {
        at.push_back(g_app->doc.script().nodes.size() - 1);
    }
    // EIN Rueckgaengig-Schritt fuer die ganze angehaengte Datei.
    (void)g_app->doc.insertAfterAll(at, neu, nullptr);
    rebuildTree();
}

// --- .pk3 --------------------------------------------------------------
//
// Skripte und Karten liegen fast nie ausgepackt herum, sondern in
// assets*.pk3 oder in der .pk3 der Mod. Ohne diesen Weg muesste man vorher
// von Hand auspacken.



void addGamePath() {
    // Ein echter Ordnerdialog.
    //
    // Vorher musste man eine .pk3 im gewuenschten Ordner auswaehlen, damit
    // das Programm den Ordner davon nehmen konnte - eine Kruecke, die auch
    // so im Quelltext stand. Der Common Item Dialog kann Ordner direkt.
    const std::string p =
        platform::pickFolder(tr(Str::Pk3AddPath), g_app->settings.lastDir);
    if (p.empty()) {
        return;
    }
    addGamePathDirectory(p);
}

// Eine Datei aus dem Archiv holen.

// Den Namen eines Bearbeitungsschritts uebersetzen.
const char* stepName(const char* key) {
    const std::string k = (key != nullptr) ? key : "";
    if (k == "insert") { return tr(Str::StepInsert); }
    if (k == "delete") { return tr(Str::StepDelete); }
    if (k == "clone") { return tr(Str::StepClone); }
    if (k == "moveup" || k == "movedown") { return tr(Str::StepMove); }
    if (k == "comment") { return tr(Str::StepComment); }
    if (k == "uncomment") { return tr(Str::StepUncomment); }
    if (k == "paste") { return tr(Str::StepPaste); }
    if (k == "selective") { return tr(Str::StepSelectiveUndo); }
    return tr(Str::StepEdit);
}

// Weiter unten (Aenderungsmarken): der Inhalt eines Befehls als Text.
std::string eigenerText(const Node& n);
// Was hat sich von a nach b geaendert? Eine Zeile wie
// "wait ( 4000 ) -> wait ( 3000 )", "+ print ( p )", "- wait ( 1 )", bei
// mehreren "(+N)". `ziel` bekommt den Weg des Befehls in b (leer, wenn er nur
// entfernt wurde).
std::string aenderungsText(const Script& a, const Script& b, Path* ziel);

namespace {

// Jeder Befehl eines Skripts mit Kennung, eigenem Text, Weg und Reihenfolge.
struct Flach {
    const Node* knoten = nullptr;
    std::string text;
    Path weg;
    std::size_t ord = 0;
};

void flachen(const std::vector<Node>& ns, Path& weg, std::map<Kennung, Flach>& out, std::size_t& ord) {
    for (std::size_t i = 0; i < ns.size(); ++i) {
        weg.push_back(i);
        const Node& n = ns[i];
        if (n.kind != Node::Kind::Blank && n.kennung != 0) {
            out[n.kennung] = Flach{&n, eigenerText(n), weg, ord++};
        }
        flachen(n.children, weg, out, ord);
        weg.pop_back();
    }
}

std::string kurzZeile(const Node& n) {
    std::string z = rowText(n, g_app->treeOpt);
    constexpr std::size_t kMax = 60;
    if (z.size() > kMax) {
        z = z.substr(0, kMax) + "...";
    }
    return z;
}

// Nach einem Undo/Redo: den geaenderten Befehl waehlen, aufklappen und
// ins Bild holen - sonst sieht man nicht, WAS sich geaendert hat.
void zeigeGeaendert(const Path& ziel) {
    g_app->selectedPath.clear();
    g_app->selection.clear();
    if (!ziel.empty() && nodeAt(g_app->doc.script(), ziel) != nullptr) {
        for (std::size_t t = 1; t < ziel.size(); ++t) {
            g_app->expanded.setOpen(kennungFuer(Path(ziel.begin(), ziel.begin() + static_cast<std::ptrdiff_t>(t))),
                                    true);
        }
        g_app->auswahlDurchEinfuegen = ziel;
        g_app->selectedPath = ziel;
        g_app->selection.assign(1, ziel);
        g_app->scrollToSelected = true;
    }
    rebuildTree();
}

void schritteAusfuehren(int anzahl, bool redo) {
    if (anzahl <= 0) {
        return;
    }
    g_app->doc.vergibKennungen();
    const Script vorher = g_app->doc.script();
    const char* was = redo ? g_app->doc.redoLabel() : g_app->doc.undoLabel();
    int getan = 0;
    for (int k = 0; k < anzahl; ++k) {
        if (!(redo ? g_app->doc.redo() : g_app->doc.undo())) {
            break;
        }
        ++getan;
    }
    if (getan == 0) {
        return;
    }
    // Beschrieben wird die AKTION, die zurueckgenommen (bzw. wiederholt)
    // wird - so wie man sie getan hat: beim Undo vom jetzigen Stand zum
    // vorigen, beim Redo vom vorigen zum jetzigen. Gewaehlt wird der Befehl
    // im jetzigen Stand.
    Path ziel;
    std::string text;
    if (redo) {
        text = aenderungsText(vorher, g_app->doc.script(), nullptr);
        (void)aenderungsText(g_app->doc.script(), vorher, &ziel);
    } else {
        text = aenderungsText(g_app->doc.script(), vorher, &ziel);
    }
    char kopf[400];
    if (getan == 1) {
        std::snprintf(kopf, sizeof(kopf), tr(redo ? Str::RedoOf : Str::UndoOf),
                      text.empty() ? stepName(was) : text.c_str());
        g_app->undoMeldung = kopf;
    } else {
        std::snprintf(kopf, sizeof(kopf), tr(redo ? Str::RedoSteps : Str::UndoSteps), getan);
        g_app->undoMeldung = text.empty() ? std::string(kopf) : std::string(kopf) + " - " + text;
    }
    g_app->undoMeldungRedo = redo;
    g_app->undoMeldungTiefe = g_app->doc.undoDepth();
    diag::detail(std::string("Undo-Meldung: ") + g_app->undoMeldung);
    zeigeGeaendert(ziel);
}

}  // namespace

std::string aenderungsText(const Script& a, const Script& b, Path* ziel) {
    // Beschreibt die AKTION a -> b so, wie man sie getan hat (shank: "er
    // sollte anzeigen was genau geundod wird ... nicht nur irgendwelche
    // sinnlosen Sachen"): "geaendert wait: 2000 -> 3000", "geloescht print
    // ( p )", "eingefuegt ...", "verschoben ...". `ziel` ist der Weg des
    // ersten betroffenen Befehls in a (bei einem eingefuegten: in b).
    if (ziel != nullptr) { ziel->clear(); }
    std::map<Kennung, Flach> fa;
    std::map<Kennung, Flach> fb;
    Path w;
    std::size_t ord = 0;
    flachen(a.nodes, w, fa, ord);
    ord = 0;
    flachen(b.nodes, w, fb, ord);
    // Zahl ohne ueberfluessige Nullen: "2000.000" -> "2000", "0.500" -> "0.5".
    const auto wert = [](std::string s) {
        const auto punkt = s.find('.');
        bool zahl = !s.empty() && punkt != std::string::npos;
        for (std::size_t i = 0; zahl && i < s.size(); ++i) {
            const char c = s[i];
            zahl = (std::isdigit(static_cast<unsigned char>(c)) != 0) || c == '.' || (c == '-' && i == 0);
        }
        if (zahl) {
            while (!s.empty() && s.back() == '0') { s.pop_back(); }
            if (!s.empty() && s.back() == '.') { s.pop_back(); }
        }
        return s;
    };
    struct Fund {
        std::size_t ord;
        std::string text;
        Path weg;
    };
    std::vector<Fund> funde;
    for (const auto& [k, eb] : fb) {
        const auto ea = fa.find(k);
        if (ea == fa.end()) {
            funde.push_back({eb.ord, std::string(tr(Str::ChangeAdded)) + " " + kurzZeile(*eb.knoten), eb.weg});
            continue;
        }
        if (ea->second.text == eb.text) {
            continue;
        }
        const Node& na = *ea->second.knoten;
        const Node& nb = *eb.knoten;
        std::string z = std::string(tr(Str::ChangeEdited)) + " ";
        if (na.name == nb.name && na.args.size() == nb.args.size() && !na.args.empty()) {
            // Nur das geaenderte Argument. Ist es nicht das erste, steht das
            // erste als Name dabei (set ( SET_X, ... ), camera ( MOVE, ... )).
            std::size_t erstes = na.args.size();
            int anders = 0;
            for (std::size_t i = 0; i < na.args.size(); ++i) {
                if (na.args[i].text != nb.args[i].text) {
                    if (erstes == na.args.size()) { erstes = i; }
                    ++anders;
                }
            }
            if (erstes < na.args.size()) {
                z += nb.name;
                if (erstes > 0) { z += " " + nb.args[0].text; }
                z += ": " + wert(na.args[erstes].text) + " -> " + wert(nb.args[erstes].text);
                if (anders > 1) { z += " ..."; }
            } else {
                z += kurzZeile(na) + " -> " + kurzZeile(nb);
            }
        } else {
            z += kurzZeile(na) + " -> " + kurzZeile(nb);
        }
        funde.push_back({ea->second.ord, z, ea->second.weg});
    }
    for (const auto& [k, ea] : fa) {
        if (fb.count(k) == 0) {
            funde.push_back({ea.ord, std::string(tr(Str::ChangeRemoved)) + " " + kurzZeile(*ea.knoten), ea.weg});
        }
    }
    if (funde.empty()) {
        // Nur verschoben: der erste Befehl, dessen Platz sich geaendert hat.
        for (const auto& [k, eb] : fb) {
            const auto ea = fa.find(k);
            if (ea != fa.end() && ea->second.ord != eb.ord &&
                (funde.empty() || ea->second.ord < funde.front().ord)) {
                funde.assign(1, Fund{ea->second.ord, std::string(tr(Str::ChangeMoved)) + " " + kurzZeile(*eb.knoten),
                                     ea->second.weg});
            }
        }
    }
    if (funde.empty()) {
        return {};
    }
    std::sort(funde.begin(), funde.end(), [](const Fund& x, const Fund& y) { return x.ord < y.ord; });
    // Bis zu drei ausgeschrieben, damit man sieht, WAS alles betroffen ist.
    std::string out;
    const std::size_t zeigen = std::min<std::size_t>(funde.size(), 3);
    for (std::size_t i = 0; i < zeigen; ++i) {
        if (i > 0) { out += ";  "; }
        out += funde[i].text;
    }
    if (funde.size() > zeigen) {
        char z[64];
        std::snprintf(z, sizeof(z), tr(Str::RevertPreviewMore), static_cast<int>(funde.size() - zeigen));
        out += "  " + std::string(z);
    }
    if (ziel != nullptr) {
        *ziel = funde.front().weg;
    }
    return out;
}

namespace {

// Wo steht der Knoten mit dieser Kennung? Eltern-Liste und Stelle, sonst null.
std::vector<Node>* sucheKennung(std::vector<Node>& ns, Kennung k, std::size_t& stelle) {
    for (std::size_t i = 0; i < ns.size(); ++i) {
        if (ns[i].kennung == k) {
            stelle = i;
            return &ns;
        }
        if (std::vector<Node>* tief = sucheKennung(ns[i].children, k, stelle)) {
            return tief;
        }
    }
    return nullptr;
}

// Eltern-Kennung (0 = oberste Ebene) und Vorgaenger-Kennung (0 = erster) in a.
bool lageIn(const std::vector<Node>& ns, Kennung k, Kennung eltern, Kennung& outEltern, Kennung& outVor) {
    for (std::size_t i = 0; i < ns.size(); ++i) {
        if (ns[i].kennung == k) {
            outEltern = eltern;
            outVor = 0;
            for (std::size_t j = i; j-- > 0;) {
                if (ns[j].kennung != 0) { outVor = ns[j].kennung; break; }
            }
            return true;
        }
        if (lageIn(ns[i].children, k, ns[i].kennung, outEltern, outVor)) {
            return true;
        }
    }
    return false;
}

}  // namespace

// --- Einen Schritt aus der MITTE zuruecknehmen -----------------------------
//
// shank: "es waere auch nice, dass man in der Liste einen Undo-Befehl
// mittendrin anklicken kann und den dann undo machen kann und nicht der Reihe
// nach muss". Schritt i fuehrte von undoStand(i) (a) zu b. Zurueckgenommen
// wird NUR, was er getan hat, und zwar am JETZIGEN Stand - alles danach bleibt:
//   geaendert   -> der Befehl bekommt seine alten Felder (falls es ihn noch gibt)
//   eingefuegt  -> der Befehl wird entfernt (samt Block)
//   geloescht   -> der Befehl kommt wieder, hinter seinen alten Vorgaenger
//                  (sonst an den Anfang seines alten Blocks)
// Verschiebungen werden nicht zurueckgenommen. Das Ganze ist EIN neuer
// Schritt und laesst sich selbst wieder rueckgaengig machen.
bool schrittEinzelnZurueck(std::size_t i, std::string* bericht) {
    Document& d = g_app->doc;
    d.vergibKennungen();
    if (i >= d.undoDepth()) {
        return false;
    }
    const Script& a = d.undoStand(i);
    const Script& b = (i + 1 < d.undoDepth()) ? d.undoStand(i + 1) : d.script();
    std::map<Kennung, Flach> fa;
    std::map<Kennung, Flach> fb;
    Path w;
    std::size_t ord = 0;
    flachen(a.nodes, w, fa, ord);
    ord = 0;
    flachen(b.nodes, w, fb, ord);
    Script neu = d.script();
    int getan = 0;
    // Eingefuegt -> entfernen.
    for (const auto& [k, eb] : fb) {
        if (fa.count(k) != 0) { continue; }
        std::size_t stelle = 0;
        if (std::vector<Node>* liste = sucheKennung(neu.nodes, k, stelle)) {
            liste->erase(liste->begin() + static_cast<std::ptrdiff_t>(stelle));
            ++getan;
        }
    }
    // Geaendert -> alte Felder.
    for (const auto& [k, eb] : fb) {
        const auto ea = fa.find(k);
        if (ea == fa.end() || ea->second.text == eb.text) { continue; }
        std::size_t stelle = 0;
        if (std::vector<Node>* liste = sucheKennung(neu.nodes, k, stelle)) {
            Node alt = *ea->second.knoten;
            alt.children = std::move((*liste)[stelle].children);
            alt.kennung = k;
            (*liste)[stelle] = std::move(alt);
            ++getan;
        }
    }
    // Geloescht -> wieder einsetzen (in der Reihenfolge von a).
    std::vector<const Flach*> weg;
    for (const auto& [k, ea] : fa) {
        if (fb.count(k) == 0) { weg.push_back(&ea); }
    }
    std::sort(weg.begin(), weg.end(), [](const Flach* x, const Flach* y) { return x->ord < y->ord; });
    for (const Flach* f : weg) {
        // Ein Kind eines ebenfalls geloeschten Blocks kommt mit dem Block.
        Kennung eltern = 0;
        Kennung vor = 0;
        if (!lageIn(a.nodes, f->knoten->kennung, 0, eltern, vor)) { continue; }
        if (eltern != 0 && fb.count(eltern) == 0) { continue; }
        std::size_t stelle = 0;
        if (vor != 0) {
            if (std::vector<Node>* liste = sucheKennung(neu.nodes, vor, stelle)) {
                liste->insert(liste->begin() + static_cast<std::ptrdiff_t>(stelle) + 1, *f->knoten);
                ++getan;
                continue;
            }
        }
        if (eltern != 0) {
            if (std::vector<Node>* liste = sucheKennung(neu.nodes, eltern, stelle)) {
                auto& kinder = (*liste)[stelle].children;
                kinder.insert(kinder.begin(), *f->knoten);
                ++getan;
                continue;
            }
        }
        neu.nodes.insert(neu.nodes.begin(), *f->knoten);
        ++getan;
    }
    if (getan == 0) {
        return false;
    }
    // Benannt wird die AKTION, die zurueckgenommen wird - wie beim normalen Undo.
    const std::string text = aenderungsText(a, b, nullptr);
    if (bericht != nullptr) { *bericht = text; }
    return d.ersetzeSkript(std::move(neu), "selective");
}

void undoSchritte(int anzahl) { schritteAusfuehren(anzahl, false); }
void redoSchritte(int anzahl) { schritteAusfuehren(anzahl, true); }

void doUndo() { undoSchritte(1); }

void doRedo() { redoSchritte(1); }

// --- Die Undo-Liste, wie in 3ds Max -------------------------------------
//
// Ein Klappfenster mit allen Schritten, der neueste oben. Ein Klick auf einen
// Eintrag markiert ihn und alle darueber; "Undo" nimmt sie zurueck, "Cancel"
// schliesst. Doppelklick tut beides auf einmal.
void drawUndoListe() {
    // Die Eintraege aus dem Stapel. Neu gerechnet beim Oeffnen UND immer,
    // wenn sich das Dokument aendert, waehrend die Liste offen ist (shank:
    // "If I have the undo history window open and start doing CTRL Z/Y,
    // the window doesn't update").
    static std::uint64_t gefuellterStand = 0;
    static int gefuellterReiter = -1;
    const bool offen = ImGui::IsPopupOpen("##undoliste");
    const bool neuOeffnen = g_app->undoListeAnfrage != 0;
    if (neuOeffnen || (offen && (g_app->doc.stand() != gefuellterStand || g_app->activeTab != gefuellterReiter))) {
        if (neuOeffnen) {
            g_app->undoListeRedo = (g_app->undoListeAnfrage == 2);
            g_app->undoListeMarke = 0;
        }
        g_app->undoListeAnfrage = 0;
        gefuellterStand = g_app->doc.stand();
        gefuellterReiter = g_app->activeTab;
        g_app->undoListeEintraege.clear();
        Document& d = g_app->doc;
        d.vergibKennungen();
        if (!g_app->undoListeRedo) {
            // Schritt i fuehrte von undoStand(i) zum naechsten Stand.
            const std::size_t n = d.undoDepth();
            for (std::size_t j = 0; j < n; ++j) {
                const std::size_t i = n - 1 - j;
                const Script& nachher = (i + 1 < n) ? d.undoStand(i + 1) : d.script();
                const std::string t = aenderungsText(d.undoStand(i), nachher, nullptr);
                g_app->undoListeEintraege.push_back(t.empty() ? std::string(stepName(d.undoWas(i))) : t);
            }
        } else {
            const std::size_t n = d.redoDepth();
            for (std::size_t j = 0; j < n; ++j) {
                const Script& vorher = (j == 0) ? d.script() : d.redoStand(j - 1);
                const std::string t = aenderungsText(vorher, d.redoStand(j), nullptr);
                g_app->undoListeEintraege.push_back(t.empty() ? std::string(stepName(d.redoWas(j))) : t);
            }
        }
        g_app->undoListeMarke = std::clamp(g_app->undoListeMarke, 0,
                                           std::max(0, static_cast<int>(g_app->undoListeEintraege.size()) - 1));
        if (neuOeffnen) {
            ImGui::OpenPopup("##undoliste");
        }
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2{ImGui::GetFontSize() * 22.0F, 0.0F},
                                        ImVec2{ImGui::GetFontSize() * 64.0F, FLT_MAX});
    if (!ImGui::BeginPopup("##undoliste")) {
        return;
    }
    ImGui::TextDisabled("%s", tr(g_app->undoListeRedo ? Str::RedoListTitle : Str::UndoListTitle));
    const auto& e = g_app->undoListeEintraege;
    // Hoeher als zuerst (shank: "die Liste ist bisschen eng, kann man die
    // hoeher machen?"): mindestens 8, hoechstens 22 Zeilen, und so breit wie
    // der laengste Eintrag (in Grenzen).
    const ImGuiStyle& stil = ImGui::GetStyle();
    const float zeile = ImGui::GetTextLineHeight() + stil.ItemSpacing.y;
    const float hoehe = zeile * static_cast<float>(std::clamp<std::size_t>(e.size(), 8, 22)) +
                        stil.WindowPadding.y * 2.0F;
    float breite = ImGui::GetFontSize() * 30.0F;
    for (const std::string& x : e) {
        breite = std::max(breite, ImGui::CalcTextSize(x.c_str()).x + stil.WindowPadding.x * 2.0F + stil.ScrollbarSize);
    }
    breite = std::min(breite, ImGui::GetFontSize() * 60.0F);
    static bool bisHierZeigen = false;
    bool ausfuehren = false;
    if (ImGui::BeginChild("##undoeintraege", ImVec2{breite, hoehe}, ImGuiChildFlags_Borders)) {
        if (e.empty()) {
            ImGui::TextDisabled("%s", tr(Str::UndoListEmpty));
        }
        // Ein Klick markiert NUR diesen Eintrag (shank: "wenn ich auf einen
        // drauf klicke, markiert es immer alle direkt statt nur den einen").
        // Welche "Undo up to here" mitnaehme, zeigt sich blass, solange die
        // Maus ueber diesem Knopf steht (gemerkt aus dem vorigen Bild).
        for (std::size_t k = 0; k < e.size(); ++k) {
            ImGui::PushID(static_cast<int>(k));
            const bool gewaehlt = static_cast<int>(k) == g_app->undoListeMarke;
            const bool blass = bisHierZeigen && static_cast<int>(k) < g_app->undoListeMarke;
            if (blass) {
                ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_Header);
                c.w *= 0.40F;
                ImGui::PushStyleColor(ImGuiCol_Header, c);
            }
            if (ImGui::Selectable(e[k].c_str(), gewaehlt || blass)) {
                g_app->undoListeMarke = static_cast<int>(k);
            }
            if (blass) {
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    const float knopf = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
    ImGui::BeginDisabled(e.empty());
    if (ImGui::Button(tr(g_app->undoListeRedo ? Str::RedoUpToHere : Str::UndoUpToHere), ImVec2{knopf, 0.0F})) {
        ausfuehren = true;
    }
    ImGui::EndDisabled();
    bisHierZeigen = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    if (bisHierZeigen) {
        ImGui::SetTooltip("%s", tr(g_app->undoListeRedo ? Str::RedoUpToHereHint : Str::UndoUpToHereHint));
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::EditorCancel), ImVec2{-FLT_MIN, 0.0F})) {
        ImGui::CloseCurrentPopup();
    }
    // Nur den angeklickten Schritt zuruecknehmen, die spaeteren bleiben.
    bool nurDieser = false;
    if (!g_app->undoListeRedo) {
        ImGui::BeginDisabled(e.empty());
        if (ImGui::Button(tr(Str::UndoOnlyThis), ImVec2{-FLT_MIN, 0.0F})) {
            nurDieser = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tr(Str::UndoOnlyThisHint));
        }
    }
    if (nurDieser && !e.empty()) {
        const std::size_t n = g_app->doc.undoDepth();
        const std::size_t k = static_cast<std::size_t>(std::max(0, g_app->undoListeMarke));
        std::string bericht;
        if (k < n && schrittEinzelnZurueck(n - 1 - k, &bericht)) {
            char z[400];
            std::snprintf(z, sizeof(z), tr(Str::UndoOf), bericht.empty() ? stepName("selective") : bericht.c_str());
            g_app->undoMeldung = z;
            g_app->undoMeldungRedo = false;
            g_app->undoMeldungTiefe = g_app->doc.undoDepth();
            diag::detail("Undo-Meldung (einzeln): " + g_app->undoMeldung);
            g_app->selectedPath.clear();
            g_app->selection.clear();
            rebuildTree();
        }
        ImGui::CloseCurrentPopup();
    }
    if (ausfuehren && !e.empty()) {
        const int anzahl = g_app->undoListeMarke + 1;
        if (g_app->undoListeRedo) {
            redoSchritte(anzahl);
        } else {
            undoSchritte(anzahl);
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// --- Klaenge -----------------------------------------------------------
//
// Die Rueckfallregel der Engine: im Skript steht meist .wav, in der .pk3
// liegt aber eine .mp3. JKA sucht beide. Wer nur nach dem geschriebenen
// Namen sucht, findet fast nichts - genau daran ist EffectsEd gescheitert.
bool findSoundData(const std::string& name, std::string& out) {
    std::vector<std::string> tries{name};
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        const std::string base = name.substr(0, dot);
        tries.push_back(base + ".mp3");
        tries.push_back(base + ".wav");
    } else {
        tries.push_back(name + ".mp3");
        tries.push_back(name + ".wav");
    }
    for (const std::string& t : tries) {
        if (readFromArchives(t, out)) {
            return true;
        }
        // Auch ausgepackt neben dem Spielordner nachsehen.
        for (const std::string& dir : g_app->settings.gamePaths) {
            const std::string p = dir + "/" + t;
            std::ifstream f(p, std::ios::binary);
            if (f) {
                std::ostringstream ss;
                ss << f.rdbuf();
                out = ss.str();
                return true;
            }
        }
    }
    return false;
}


// Texturen zur geladenen Karte aus den .pk3 holen.
//
// Zwei Wege, wie es die Engine auch macht: erst die Datei zum Shadernamen
// direkt suchen, und nur wenn das nichts ergibt, in den .shader-Skripten
// nachsehen, welches Bild dahintersteckt.
//
// Die Bilder werden auf hoechstens 256 Punkte verkleinert. Eine Editoransicht
// braucht keine 1024er-Textur, und bei siebzig Shadern spart das ein
// Vielfaches an Speicher.

// --- Karte -------------------------------------------------------------
//
// Nur die Entities, nicht die Geometrie. Damit bieten die Textfelder die
// targetname aus der Karte an, statt dass man sie abtippt - das war der
// eigentliche Grund, ueberhaupt eine .bsp zu lesen.
// Eine Karte aus schon gelesenen Bytes uebernehmen.
//
// Getrennt vom Dateipfad, weil eine Karte aus zwei Quellen kommt: von der
// Platte und aus einem .pk3. Vorher gab es dafuer zwei Wege, und der aus dem
// Archiv las NUR die Entities - keine Geometrie, keine Texturen. Aus dem
// Archiv geladene Karten blieben also unsichtbar, und man sah nicht warum.



// "Export" - belegt durch ein Bildschirmfoto des Originals.
//
// BehavEd schreibt eine Textdatei und oeffnet sie im Standardeditor. Der
// Inhalt ist echter .icarus-Text mit einer Kopfzeile davor, die den
// urspruenglichen Dateinamen nennt; die Zahlen folgen dem Schalter
// "%g floats". In der Statusliste erscheint "( Exported )".
void doExport() {
    const std::string dir = platform::settingsDirectory();
    if (dir.empty()) {
        return;
    }
    const std::string out = dir + "/Export.txt";
    const std::string text = writeExport(g_app->doc.script(), g_app->path,
                                         g_app->treeOpt.gFloats);
    if (!spew(out, text)) {
        platform::showError(out, tr(Str::AppTitle));
        return;
    }
    addStatus(tr(Str::ExportDone));
    platform::openWithDefaultApp(out);
}

// "Backup" und "Restore" - belegt durch das BehavEd-Tutorial auf JKHub:
//     Backup:  Saves a copy to <Script Name>.bak
//     Restore: Reloads from the backup at <Script Name>.bak
//
// Beides braucht einen Dateinamen. Bei einem noch nie gespeicherten Skript
// gibt es keinen - dann wird gesagt, was fehlt, statt still nichts zu tun.
// Die Sicherung heisst wie das Skript, nur mit .bak STATT der Endung.
//
// Am laufenden Original beobachtet: aus `remtest.txt` wird `remtest.bak`.
// behaved schrieb `remtest.txt.bak` - eine Sicherung des Originals fand es
// nicht und umgekehrt.
std::string backupPathOf(const std::string& path) { return mitEndung(path, ".bak"); }

// Frueher von behaved geschriebene Sicherungen (`x.txt.bak`) findet Restore
// weiterhin, falls es keine `x.bak` gibt.
std::string alteBackupPathOf(const std::string& path) { return path + ".bak"; }

void doBackup() {
    // Original: "You can't backup until you've saved at least once".
    if (g_app->path.empty()) {
        addStatus(tr(Str::BackupNoFile));
        return;
    }
    // Den AKTUELLEN Stand, wie das Original: "Save directly to
    // <scriptname.bak>". Bis rc567 sicherte behaved hier die Datei auf der
    // Platte - Backup hiess damit etwas anderes als im Original.
    const std::string bak = backupPathOf(g_app->path);
    if (!spew(bak, writeScript(g_app->doc.script()))) {
        platform::showError(bak, tr(Str::AppTitle));
        return;
    }
    char msg[400];
    std::snprintf(msg, sizeof(msg), tr(Str::BackupDone), fileName(bak).c_str());
    addStatus(msg);
}

void doRestore() {
    std::string data;
    if (!g_app->path.empty()) {
        data = slurp(backupPathOf(g_app->path));
        if (data.empty()) {
            data = slurp(alteBackupPathOf(g_app->path));
        }
    }
    // Original: "No Backup file available to restore from". Vorher wurde
    // diese Meldung zusammengesetzt, aber nie ausgegeben.
    if (data.empty()) {
        addStatus(tr(Str::RestoreNone));
        return;
    }
    // Original: IMMER fragen - "This RESTORE will overwrite your current
    // script, proceed?". Vorher nur bei ungespeicherten Aenderungen.
    frage(tr(Str::RestoreAsk), [data] {
        Script sc;
        std::vector<Diag> d;
        (void)readScript(data, sc, d);
        // Als EIN Rueckgaengig-Schritt: Strg+Z holt den Stand vor dem
        // Restore zurueck (behaved hat Undo, das Original nicht).
        (void)g_app->doc.replaceAll(sc);
        g_app->selectedPath.clear();
        g_app->selection.clear();
        rebuildTree();
        addStatus(tr(Str::RestoreDone));
    });
}

void doCompile() {
    std::vector<IbiBlock> blocks;
    std::vector<Diag> d;
    const bool ok = compile(g_app->doc.script(), g_app->db, blocks, d);
    // Die Meldungen des Uebersetzers zeigen - im Original stehen sie in der
    // Statusliste als "(Zeile) : Text". Vorher wurden sie verworfen, und es
    // hiess nur "Compile failed".
    // In der eingestellten Sprache, mit dem Befehl davor - die Zeilennummer
    // kennt der Uebersetzer nicht (vorher stand immer "(0)"). Nach einem
    // Fehlschlag wird die erste schuldige Zeile angewaehlt.
    Path ersterFehler;
    for (const Diag& x : d) {
        const char* muster = nullptr;
        if (x.code == "C001") { muster = tr(Str::CmsgC001); }
        else if (x.code == "C002") { muster = tr(Str::CmsgC002); }
        else if (x.code == "C003") { muster = tr(Str::CmsgC003); }
        else if (x.code == "C004") { muster = tr(Str::CmsgC004); }
        char text[400];
        if (muster != nullptr) {
            std::snprintf(text, sizeof(text), muster, x.arg.c_str());
        } else {
            std::snprintf(text, sizeof(text), "%s", x.message.c_str());
        }
        const Node* wo = x.path.empty() ? nullptr : nodeAt(g_app->doc.script(), x.path);
        char z[480];
        if (wo != nullptr) {
            std::snprintf(z, sizeof(z), "%s : %s", wo->name.c_str(), text);
            if (ersterFehler.empty()) { ersterFehler = x.path; }
        } else {
            std::snprintf(z, sizeof(z), "%s", text);
        }
        addStatus(z);
    }
    if (!ok && !ersterFehler.empty()) {
        selectByPath(ersterFehler);
    }
    if (!ok) {
        // Bei einem Fehler KEINE .ibi schreiben - eine halbe wuerde das Spiel
        // sonst laden. Die Quelle wird trotzdem gesichert, wenn sie einen
        // Pfad hat (erst speichern, dann uebersetzen - wie das Original).
        if (!g_app->path.empty()) {
            (void)doSave();
        }
        addStatus(tr(Str::CompileFailed));
        g_app->kompiliertFehlerBis = ImGui::GetTime() + 2.0;
        g_app->kompiliertBis = 0.0;
        return;
    }
    const std::string bytes = writeIbi(blocks);
    std::string out = g_app->path;
    if (out.empty()) {
        out = platform::saveFileDialog(tr(Str::Compile), "IBI (*.ibi)|*.ibi",
                                       g_app->settings.scriptPath, "unnamed.ibi");
        if (out.empty()) {
            return;
        }
    } else {
        const std::size_t dot = out.find_last_of('.');
        out = (dot == std::string::npos ? out : out.substr(0, dot)) + ".ibi";
    }
    if (!spew(out, bytes)) {
        platform::showError(out, tr(Str::AppTitle));
        return;
    }
    // --- Uebersetzen speichert die Quelle MIT ---------------------------
    //
    // shank: "when I compile a script, it doesn't save the file. When I
    // compile, it should automatically save." Genau so macht es BehavEd.
    //
    // Nach dem Dialog steht der gewaehlte Name in `out` - daraus leitet
    // `doSave` die .txt ab. Ohne diese Zeile wuesste es den Namen nicht.
    if (g_app->path.empty()) {
        g_app->path = out;
    }
    (void)doSave();
    char msg[200];
    std::snprintf(msg, sizeof(msg), tr(Str::CompileOk),
                  static_cast<int>(blocks.size()), static_cast<int>(bytes.size()));
    addStatus(msg);
    g_app->kompiliertBis = ImGui::GetTime() + 1.5;
    g_app->kompiliertFehlerBis = 0.0;
}

// Einen Befehl in den Baum einfuegen - hinter die Auswahl, sonst ans Ende.
//
// Die Regel ist seit rc533 wieder einfach:
//
//   IMMER hinter die Auswahl, auf DERSELBEN Ebene. Nie hinein.
//
// Vorher stand hier eine Sonderregel: ist ein Block gewaehlt und
// aufgeklappt (oder leer), landet der neue Befehl DRIN, an erster Stelle.
// Gut gemeint - "sonst bleiben Bloecke leer" -, aber falsch.
//
// shank zu rc532: "wenn ich links auf mehrere druecke nacheinander, macht
// er die sofort in ein plus untereinander. Das sollte so nicht sein von
// haus aus. Er sollte sie nur dann in das plus setzen wenn ich sie drauf
// ziehe."
//
// Er hat recht, und der Grund ist die KLEBRIGKEIT, nicht die Sonderregel
// selbst. Sie greift genau einmal - danach steht die Auswahl im Block, und
// "hinter die Auswahl" heisst von da an fuer immer "wieder im Block". Sein
// Protokoll zeigt es Schritt fuer Schritt:
//
//   Klick 1  loop      -> Knoten 0, Block, Auswahl [0]
//   Klick 2  wait      -> Block ist leer -> HINEIN, Auswahl [0,0]
//   Klick 3  move      -> hinter [0,0]              = [0,1]   drin
//   Klick 4  signal    -> hinter [0,1]              = [0,2]   drin
//
// Danach hat er vier Befehle einzeln wieder herausgezogen. Das Protokoll
// von rc532 zeigt genau diese vier Zuege.
//
// Und das Original macht es auch nicht: das ICARUS-Handbuch kennt nur
// "Add - Add selected command to the script window". Von Bloecken steht
// dort nichts.
//
// Wie fuellt man jetzt einen Block? Mit der Geste, die es dafuer gibt:
// den Befehl auf den Block ziehen. Das heisst HINEIN und ist seit rc521
// so - in shanks eigenem Protokoll steht die Zeile:
//
//   Ablegen: Ziel "loop", ist Block, Auswahl 1 -> Weg: HINEIN
//
// Eine Geste zum Anlegen, eine zum Einsortieren. Klicken sortiert nicht.
// Nachtrag 27.09.: "If I have a command block (affect, if, else, loop,
// task) selected ... and go to add command from the events panel or paste a
// command, the command should be added to it." Das geht jetzt, OHNE die
// Klebrigkeit von rc532 zurueckzuholen: hinein nur, wenn der Block durch
// einen KLICK angewaehlt ist (zielBlock), nicht weil er gerade eingefuegt
// wurde. Dann ans ENDE des Blocks; der neue Befehl wird angewaehlt, und die
// naechsten landen dahinter - also ebenfalls im Block, in Reihenfolge.
//
// Nachtrag, am laufenden Original nachgesehen (27.09.): hinein nur, wenn der
// Block bzw. das Makro AUFGEKLAPPT ist, und dann an den ANFANG. Ein
// zugeklappter nimmt nichts auf; der Befehl kommt dahinter.
Path zielBlock() {
    const Path& sel = g_app->selectedPath;
    if (sel.empty() || sel == g_app->auswahlDurchEinfuegen || g_app->selection.size() > 1) {
        return {};
    }
    const Node* n = nodeAt(g_app->doc.script(), sel);
    if (n == nullptr) {
        return {};
    }
    const bool behaelter = (n->kind == Node::Kind::Command && n->hasBlock) || n->kind == Node::Kind::Macro;
    return (behaelter && g_app->expanded.isOpen(kennungFuer(sel))) ? sel : Path{};
}

bool insertNodeSomewhere(Node n, Path* resultPath) {
    const Path block = zielBlock();
    if (!block.empty()) {
        // An den Anfang; bei einem Makro steht der Befehl flach direkt hinter
        // der Makrozeile.
        const bool makro = g_app->doc.istMakro(block);
        if (!g_app->doc.insertInto(block, 0, std::move(n))) {
            return false;
        }
        g_app->expanded.setOpen(kennungFuer(block), true);
        Path at = block;
        if (makro) {
            ++at.back();
        } else {
            at.push_back(0);
        }
        g_app->auswahlDurchEinfuegen = at;
        g_app->scrollToSelected = true;
        if (resultPath != nullptr) {
            *resultPath = at;
        }
        return true;
    }
    Path at = g_app->selectedPath;
    // Wohin es faellt, fragt das Dokument: hinter einer Makrozeile hinter das
    // ganze Makro.
    const Path neu = g_app->doc.wegHinter(at);
    if (!g_app->doc.insertAfter(at, std::move(n))) {
        return false;
    }
    at = neu;
    g_app->auswahlDurchEinfuegen = at;
    if (resultPath != nullptr) {
        *resultPath = at;
    }
    return true;
}


void insertCommand(const Command& c) {
    Node n = makeNode(c, g_app->db);
    // Ein neuer Blockbefehl wird gleich aufgeklappt - sonst sieht man
    // nicht, dass er einen Rumpf hat, und weiss nicht, wohin damit.
    const bool isBlock = n.hasBlock;
    Path at;
    if (!insertNodeSomewhere(std::move(n), &at)) {
        return;
    }
    if (isBlock) {
        g_app->expanded.setOpen(kennungFuer(at), true);
    }
    g_app->selectedPath = at;
    g_app->selection.assign(1, at);
    rebuildTree();
}

// Ein Makro einfuegen: Marker plus Rumpf, hinter die Auswahl.
// Was aus der Ereignisliste gezogen wurde, an der Zielstelle einsetzen.
//
// Getrennt von insertCommand: das setzt hinter die AUSWAHL und ist die
// Bedienung des Originals. Hier sagt der Ort der Maus, wohin - deshalb
// nimmt die Funktion Weg und Richtung entgegen und fasst die Auswahl nicht
// an, ausser um das Eingesetzte hervorzuheben.
// Die Knoten, die ein Zug aus der Ereignisliste erzeugt: ein Befehl oder
// die Zeilen eines Makros. Gebraucht vom Hauptbaum (dropNewAt) und von den
// Nebenfeldern - eine Fassung fuer beide.
std::vector<Node> knotenFuerNeu(const NewDrag& what) {
    std::vector<Node> nodes;
    if (what.kind == 0) {
        if (what.index < 0 ||
            static_cast<std::size_t>(what.index) >= g_app->db.commands.size()) {
            return nodes;
        }
        nodes.push_back(makeNode(g_app->db.commands[
                                     static_cast<std::size_t>(what.index)],
                                 g_app->db));
    } else {
        if (what.index < 0 ||
            static_cast<std::size_t>(what.index) >= g_app->db.macroBodies.size()) {
            return nodes;
        }
        // Ein Makro ist Marker PLUS Rumpf, also mehrere Knoten. Sie muessen
        // in der richtigen Reihenfolge landen - siehe dropNewAt.
        nodes = makeMacroNodes(g_app->db.macroBodies[
                                   static_cast<std::size_t>(what.index)],
                               g_app->db);
    }
    return nodes;
}

void dropNewAt(const NewDrag& what, const Path& target, DropWhere where) {
    const std::vector<Node> nodes = knotenFuerNeu(what);
    if (nodes.empty()) {
        return;
    }

    Path at;
    bool ok = false;
    const bool neuMitMakro = std::any_of(nodes.begin(), nodes.end(),
                                         [](const Node& n) { return n.kind == Node::Kind::Macro; });
    if (where == DropWhere::Into && g_app->doc.istMakro(target) && neuMitMakro) {
        where = DropWhere::After;   // ein Makro nicht ins Makro - dahinter
    }
    if (where == DropWhere::Into) {
        // In einen Block: vorne hinein, damit man sieht, dass etwas
        // passiert ist. Ans Ende geschoben waere es bei einem langen Block
        // ausserhalb des sichtbaren Bereichs. Ein Makro ebenso (im Original
        // "ganz oben"); seine Befehle stehen flach hinter der Makrozeile.
        const bool makro = g_app->doc.istMakro(target);
        std::size_t k = 0;
        for (const Node& n : nodes) {
            if (!g_app->doc.insertInto(target, k, n)) { break; }
            ++k;
        }
        ok = k != 0;
        if (ok) {
            g_app->expanded.setOpen(kennungFuer(target), true);
            at = target;
            if (makro) {
                at.back() += k;
            } else {
                at.push_back(k - 1);
            }
        }
    } else if (where == DropWhere::Before) {
        // Rueckwaerts einsetzen: jedes insertBefore schiebt das vorher
        // Eingesetzte weiter nach hinten. Von hinten nach vorne gearbeitet
        // steht am Ende die richtige Reihenfolge da.
        for (std::size_t k = nodes.size(); k-- > 0;) {
            if (!g_app->doc.insertBefore(target, nodes[k])) { break; }
            ok = true;
        }
        at = target;
    } else {
        Path after = target;
        for (const Node& n : nodes) {
            if (!g_app->doc.insertAfter(after, n)) { break; }
            ok = true;
            if (after.empty()) {
                after.push_back(g_app->doc.script().nodes.size() - 1);
            } else {
                ++after.back();
            }
        }
        at = after;
    }
    if (!ok) {
        return;
    }
    // Einen neuen Block gleich aufklappen - sonst sieht man nicht, dass er
    // einen Rumpf hat.
    const Node* placed = at.empty() ? nullptr : nodeAt(g_app->doc.script(), at);
    if (placed != nullptr && placed->hasBlock) {
        g_app->expanded.setOpen(kennungFuer(at), true);
    }
    g_app->selectedPath = at;
    g_app->selection.assign(1, at);
    g_app->auswahlDurchEinfuegen = at;   // siehe zielBlock
}

void insertMacro(const Macro& m) {
    const std::vector<Node> nodes = makeMacroNodes(m, g_app->db);
    // Wie bei insertCommand: hinter die Auswahl, nie hinein. Hier stand
    // dieselbe Sonderregel ("auch Makros gehoeren in einen offenen Block
    // hinein"), und sie hatte dieselbe Wirkung - siehe die Begruendung bei
    // insertNodeSomewhere. Zum Hineinsetzen zieht man auf den Block.
    Path at = g_app->selectedPath;
    // In einen angeklickten Block: ans Ende, Zeile fuer Zeile (siehe
    // zielBlock). Sonst dahinter, als EIN Rueckgaengig-Schritt fuer das ganze
    // Makro, nicht einer je Knoten (shanks rc533-Protokoll: Stapel 7 -> 11
    // beim Einsetzen von `standOnly`).
    const Path block = zielBlock();
    if (!block.empty() && !g_app->doc.istMakro(block)) {
        std::size_t k = 0;
        for (const Node& n : nodes) {
            if (!g_app->doc.insertInto(block, k, n)) { break; }
            ++k;
        }
        if (k == 0) {
            return;
        }
        g_app->expanded.setOpen(kennungFuer(block), true);
        at = block;
        at.push_back(k - 1);
    } else if (!g_app->doc.insertAfterAll(at, nodes, &at)) {
        return;
    }
    g_app->auswahlDurchEinfuegen = at;
    g_app->selectedPath = at;
    g_app->selection.assign(1, at);
    g_app->scrollToSelected = true;
    rebuildTree();
}






// --- Symbole -----------------------------------------------------------
//
// Der Symbolstreifen liegt als EIGENE Textur, nicht als Rechtecke im
// Schriftatlas.
//
// Der erste Anlauf ging ueber ImFontAtlas::AddCustomRect. Das ist in ImGui
// 1.92 als "[ALPHA] Custom Rectangles/Glyphs API" gekennzeichnet, und zwei
// Dinge sind daran toedlich:
//   * GetCustomRect() dereferenziert TexData ungeprueft
//     (IM_ASSERT(TexData->Width > 0 ...) steht VOR jeder Nullpruefung),
//   * io.Fonts->Clear() beim Neubau der Schriften macht alle Kennungen
//     ungueltig, ohne dass man es merkt.
// Ergebnis war ein Absturz im ersten Bild:
//     0xC0000005, lesend an 0x0000000000000008
// Eine eigene Textur haengt an nichts davon.
struct IconTexture {
    void* id = nullptr;
    bool tried = false;
};

IconTexture g_iconTex;

// Beim Wechsel der Grafikschnittstelle zeigt die Kennung auf eine bereits
// zerstoerte Textur. Faellt Direct3D durch und OpenGL uebernimmt, muss sie
// deshalb neu angelegt werden.
IconTexture g_logoTex;

void* logoTextureId() { return g_logoTex.id; }

void resetIcons() {
    g_iconTex.id = nullptr;
    g_iconTex.tried = false;
    g_logoTex.id = nullptr;
    g_logoTex.tried = false;
}

void ensureIcons() {
    if (g_iconTex.tried) {
        return;
    }
    g_iconTex.tried = true;
    // Ein waagerechter Streifen: 40 Symbole zu 16x16 nebeneinander.
    static std::vector<unsigned char> strip;
    strip.assign(static_cast<std::size_t>(icons::kCount) * icons::kSize *
                     icons::kSize * 4U, 0U);
    const int stripW = icons::kCount * icons::kSize;
    for (int i = 0; i < icons::kCount; ++i) {
        for (int y = 0; y < icons::kSize; ++y) {
            for (int x = 0; x < icons::kSize; ++x) {
                const std::size_t src =
                    ((static_cast<std::size_t>(i) * icons::kSize + static_cast<std::size_t>(y)) *
                         icons::kSize + static_cast<std::size_t>(x)) * 4U;
                const std::size_t dst =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(stripW) +
                     static_cast<std::size_t>(i * icons::kSize + x)) * 4U;
                for (int c = 0; c < 4; ++c) {
                    strip[dst + static_cast<std::size_t>(c)] =
                        icons::kPixels[src + static_cast<std::size_t>(c)];
                }
            }
        }
    }
    g_iconTex.id = render::createTexture(strip.data(), stripW, icons::kSize);

    if (!g_logoTex.tried) {
        g_logoTex.tried = true;
        g_logoTex.id = render::createTexture(icons::kLogoPixels, icons::kLogoWidth,
                                             icons::kLogoHeight);
    }
}

// Auswahl setzen, mit Strg und Umschalt wie ueberall sonst.
//
//   ohne Taste  : nur dieser Knoten
//   Strg        : diesen dazu oder weg
//   Umschalt    : alles zwischen dem zuletzt gewaehlten und diesem
void moveSelectionByKey(int delta);
void handleTreeKeys();

// Ein Symbol zeichnen. Die Groesse folgt der Schrift, damit es bei jeder
// Skalierung zur Zeile passt.
void drawIcon(const char* name, bool /*selected*/) {
    const float size = ImGui::GetFontSize();
    // Erst die Vektorzeichnung (gui/vektoricons.cpp): scharf in jeder Groesse.
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (vektorIcon(ImGui::GetWindowDrawList(), name, at, size,
                       g_app->settings.alternativeIcons)) {
            ImGui::Dummy(ImVec2{size, size});
            return;
        }
    }
    const int base = icons::indexOf(name);
    if (g_iconTex.id == nullptr || base < 0) {
        ImGui::Dummy(ImVec2{size, size});
        return;
    }
    // Jedes Symbol liegt zweimal im Streifen, hell und dunkel. Das ist ein
    // ZWEITER FARBSATZ, keine Auswahlfassung - belegt durch den Schalter
    // "Use Alternative Coloured Icons" (id 1052) in Dialog 131 des
    // Originals. Meine erste Deutung als Normal/Ausgewaehlt war falsch.
    const int index = (g_app->settings.alternativeIcons && base + 1 < icons::kCount)
                          ? base + 1
                          : base;
    const float step = 1.0F / static_cast<float>(icons::kCount);
    const ImVec2 uv0{static_cast<float>(index) * step, 0.0F};
    const ImVec2 uv1{static_cast<float>(index + 1) * step, 1.0F};
    ImGui::Image(reinterpret_cast<ImTextureID>(g_iconTex.id), ImVec2{size, size}, uv0, uv1);
}

// --- Anordnung ---------------------------------------------------------
//
// Die erste Fassung setzte jedes Bedienelement starr auf seine Koordinate aus
// Dialog 102 - 12 dlu hohe Knoepfe, 191 dlu breite Liste. Das ging schief:
// Ravens Mass gilt fuer MS Sans Serif 8pt, unsere Schrift ist deutlich
// groesser, und die Beschriftungen ueberlappten. Ausserdem wuchs beim
// Vergroessern des Fensters nur das Fenster, nicht der Inhalt.
//
// Jetzt gilt: die VERHAELTNISSE kommen aus der Ressource, die absoluten
// Groessen aus der Schrift.
//   Events    191 / 663 = 28,8 %
//   Flow      372 / 663 = 56,1 %
//   Knoepfe    71 / 663 = 10,7 %, mindestens so breit wie der laengste Text
// Die Knopfhoehe ist ImGui::GetFrameHeight(), nicht 12 dlu.

struct Layout {
    float total = 0.0F;   // nutzbare Breite insgesamt
    float minButtonsW = 0.0F;
    float rest = 0.0F;    // Breite von Events + Flow zusammen
    float eventsW;
    float flowW;
    float buttonsW;
    float topH;        // Events / Flow / Knoepfe, samt Kopfzeile
    // Die Hoehe der KOPFZEILE - fuer alle vier Spalten dieselbe.
    //
    // Die Spalten haben unterschiedliche Koepfe: "Script Flow" und "Actions"
    // sind blosser Text, Karte und Modell haben eine Knopfleiste, die
    // Modusleiste hat gar keinen. Eine Knopfzeile ist HOEHER als eine
    // Textzeile - deshalb begannen die Rahmen auf verschiedenen Hoehen, und
    // kein noch so genaues Abziehen unten konnte das ausgleichen.
    //
    // Jetzt bekommt jede Spalte denselben Platz fuer ihren Kopf, egal was
    // darin steht. Wer weniger braucht, laesst den Rest frei.
    float headerH;
    // Die Hoehe des RAHMENS, also ohne die Ueberschriftzeile.
    //
    // EIN Wert fuer alle vier Spalten - Modusleiste, Ansicht, Skriptbaum und
    // Knopfspalte. Vorher rechnete jede ihre eigene aus, immer als
    // "topH minus eine Textzeile", und die vier Rechnungen ergaben nicht
    // dasselbe. Sichtbar wurde es als Rahmen, die unten nicht auf einer
    // Linie enden.
    //
    // Wer eine Spalte hinzufuegt, nimmt diesen Wert und rechnet nicht neu.
    // NEGATIV: "alles Uebrige minus diesem Betrag" - siehe die Erklaerung
    // bei der Berechnung. Wer sie als Hoehe weiterreicht, muss sie so
    // weitergeben, wie sie ist.
    float frameH;
    float fussH;       // Bedienleiste + Statuszeile + Abstaende
    float toolH;       // Bedienleiste unter allen drei Spalten
    float statusH;
    // Das Fenster ist zu klein fuer alles, was hineinsoll.
    //
    // Gemeldet vom Programm selbst: "Fenster 24x-374 - FEHLER: das Band ist
    // hoeher als das Fenster." Eine NEGATIVE Hoehe.
    //
    // frameH entsteht als Rest: verfuegbare Hoehe minus Statuszeile, minus
    // Bedienleiste, minus Reiterleiste, minus Kopfzeile. Schrumpft das
    // Fenster weit genug - beim Minimieren, oder waehrend Windows die
    // Groesse aendert -, wird der Rest negativ, und jede Spalte bekommt
    // eine negative Hoehe.
    //
    // frameH und topH sind deshalb jetzt bei null abgefangen. Damit die
    // Meldung nicht mit verschwindet, wird der Fall HIER festgehalten -
    // sonst haette ich den Fehler nur unsichtbar gemacht statt behoben.
    bool tooSmall = false;
};

// Ein waagerechter Trenner. Dieselbe Bauform, andere Achse.
//
// Das Vorzeichen ist umgekehrt: die Statusliste waechst, wenn man nach OBEN
// zieht. Deshalb ist size1 der Bereich DARUEBER und size2 die Liste.
bool splitterH(const char* id, float width, float* fraction, float total,
               float minFraction, float maxFraction) {
    if (total <= 1.0F) {
        return false;
    }
    const float h = ImGui::GetStyle().ItemSpacing.y * 1.5F;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiID wid = window->GetID(id);

    // size2 ist die Statusliste, size1 alles darueber.
    float size2 = *fraction * total;
    float size1 = total - size2;

    const float useW = (width > 0.0F) ? width
                                      : ImGui::GetContentRegionAvail().x;
    ImRect bb;
    bb.Min = window->DC.CursorPos;
    bb.Max = ImVec2{bb.Min.x + std::max(useW, 1.0F), bb.Min.y + h};

    const bool held = ImGui::SplitterBehavior(bb, wid, ImGuiAxis_Y, &size1, &size2,
                                              (1.0F - maxFraction) * total,
                                              minFraction * total,
                                              2.0F, 0.10F);
    ImGui::Dummy(ImVec2{std::max(useW, 1.0F), h});

    if (held) {
        *fraction = std::clamp(size2 / total, minFraction, maxFraction);
        return true;
    }
    return false;
}

// `splitter()` ist seit rc473 weg.
//
// Die Trennlinien zwischen den Spalten wurden von Hand gezeichnet, mit
// eigenem Ziehen, eigenen Grenzen und zwei gespeicherten
// Aufteilungsbruechen. Eine ImGui-Tabelle bringt das alles mit -
// `ImGuiTableFlags_Resizable` und `BordersInnerV` -, und sie merkt sich
// die Breiten selbst.
//
// Rund siebzig Zeilen weniger, und zwei Zahlen weniger, die auseinander
// laufen koennen.


Layout computeLayout() {
    const ImGuiStyle& st = ImGui::GetStyle();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float pad = st.ItemSpacing.x;

    // Die Knopfspalte muss den laengsten Text tragen. In den Uebersetzungen
    // sind die Woerter unterschiedlich lang ("Wiederherstellen" gegen
    // "Restore"), deshalb gemessen statt geraten.
    float widest = 0.0F;
    for (const Str id : {Str::ActDelete, Str::ActClone, Str::ActPaste, Str::ActRem,
                         Str::ActFind, Str::FileNew, Str::FileAppend, Str::FileSave,
                         Str::FileSaveAs, Str::FileExport, Str::FileBackup,
                         Str::FileRestore, Str::AppExit, Str::Compile}) {
        widest = std::max(widest, ImGui::CalcTextSize(tr(id)).x);
    }
    // --- Die UEBERSCHRIFTEN zaehlen mit --------------------------------
    //
    // "Treeview Options" und "Application" standen nicht in der Liste. Sie
    // sind aber laenger als jeder Knopf darunter, und auf shanks Bildern
    // waren sie abgeschnitten: "Treeview Option", "Fold Macro:".
    //
    // Der Fehler ist aelter als die Tabelle aus rc473 - auf den Bildern zu
    // rc472 stand dasselbe. Der kopflose Pruefstand hat gezeigt, dass die
    // Tabellenzelle KEINE Breite kostet (Spalte 156 -> innen 140, genau wie
    // ein Kindfenster von 156). Damit war klar, dass es hier liegt.
    for (const Str id : {Str::GroupFile, Str::GroupTreeview,
                         Str::GroupApplication}) {
        widest = std::max(widest, ImGui::CalcTextSize(tr(id)).x);
    }
    // --- Ankreuzfelder tragen ein Kaestchen vor sich her ----------------
    //
    // `CalcTextSize` misst nur den Text. Ein Ankreuzfeld ist Kaestchen plus
    // Abstand plus Text - das Kaestchen ist so hoch wie eine Zeile, also
    // `GetFrameHeight()` breit.
    const float kaestchen =
        ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
    for (const Str id : {Str::TreeShowTypes, Str::TreeGFloats,
                         Str::TreeFoldMacros}) {
        widest = std::max(widest, ImGui::CalcTextSize(tr(id)).x + kaestchen);
    }
    Layout l{};
    // Der gemessene Text ist die UNTERGRENZE, die eingestellte Breite darf
    // darueber liegen. Sonst kann man die Spalte nicht breiter ziehen, und
    // im Deutschen werden "Ausschneiden" und "Einstellungen" abgeschnitten.
    l.minButtonsW = widest + st.FramePadding.x * 2.0F + st.ScrollbarSize + pad;
    // Bei der KARTE entfaellt die Knopfspalte: "Actions" gehoert zum Skript,
    // nicht zur Karte. Ihr Platz geht an Karte und Skriptbaum.
    l.buttonsW = (g_app->leftMode == 1)
                     ? 0.0F
                     : std::max(l.minButtonsW,
                                avail.x * g_app->settings.splitButtons);
    // Die Ebenenspalte wird NICHT hier abgezogen: sie liegt innerhalb der
    // linken Spalte, und drawEventsList zieht sie dort von seiner Breite ab.
    // Zweimal abziehen liesse den Skriptbaum wachsen, bis er nicht mehr
    // passt.
    const float gaps = (g_app->leftMode == 1) ? pad * 2.0F : pad * 3.0F;
    const float rest = std::max(avail.x - l.buttonsW - gaps, 200.0F);
    l.total = avail.x;
    l.rest = rest;
    // Im Kartenmodus braucht die linke Spalte deutlich mehr Platz: eine
    // 3D-Ansicht in einem Streifen von 200 Pixeln ist unbenutzbar. Deshalb
    // ein eigener Anteil je Modus.
    l.eventsW = rest * (g_app->leftMode != 0 ? g_app->settings.splitMap
                                             : g_app->settings.splitEvents);
    // Im Kartenmodus muss die Ebenenspalte noch hineinpassen, sonst bleibt
    // fuer die Ansicht nichts uebrig.
    if (g_app->leftMode == 1) {
        l.eventsW = std::max(l.eventsW, ImGui::GetFontSize() * 26.0F);
        // Untergrenze der Skriptspalte: gerade so viel, dass Kaestchen und
        // Symbol hineinpassen.
        //
        // Vorher waren es feste 200 Bildpunkte. Gewuenscht war aber
        // ausdruecklich, weiter hineinziehen zu koennen, "das man nur die
        // Symbole hat wie bei Blender" - und dafuer muss die Grenze tiefer.
        // Was dann noch dasteht, sind Kaestchen, Symbol und ein Rest fuer
        // die Einrueckung.
        l.eventsW = std::min(l.eventsW, rest - ImGui::GetFontSize() * 6.0F);
    }
    l.flowW = rest - l.eventsW;

    // Die Statuszeile ist EINE Zeile.
    //
    // Sie war einmal ein Feld mit einstellbarer Hoehe, Rahmen und
    // Ueberschrift - und nahm damit dauerhaft Platz weg, auch wenn nichts
    // zu melden war. Jetzt zaehlt sie nur; wer nachlesen will, klickt den
    // Zaehler an und bekommt ein Fenster. Dasselbe Muster wie in efxed.
    //
    // Der Anteil aus den Einstellungen (splitStatus) wird nicht mehr
    // gebraucht; er bleibt in der Datei stehen, damit eine aeltere Fassung
    // sie noch lesen kann.
    // GENAU die Hoehe der Statuszeile, nicht mehr.
    //
    // Hier stand zusaetzlich `+ FramePadding.y * 2`. Das war einmal
    // richtig, als die Zeile in einem Rahmen sass; sie ist heute ein
    // blosser Text. Die zwei Randbreiten wurden also abgezogen, ohne dass
    // jemand sie braucht - und blieben als Luecke stehen.
    //
    // Seit rc469 springt die Statuszeile ans Fensterende, und damit wanderte
    // die Luecke von UNTER sie nach UEBER sie: die drei Spalten enden zu
    // frueh. shank: "die 3 fenster sollten dann oberhalb enden und keinen
    // platz lassen".
    // Dieselbe Hoehe, die drawStatus fuer sich nimmt (Knoepfe darin).
    l.statusH = std::max(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetFrameHeight());
    // Die Bedienleiste unter ALLEN drei Spalten - nicht in der Modellspalte.
    //
    // Vorher stand sie innerhalb der Modellansicht und musste sich deren
    // Breite teilen. Dadurch passte sie bei schmaler Spalte nicht hinein.
    // Ueber die volle Breite ist reichlich Platz, und das Bild verliert nur
    // die Hoehe, die wirklich gebraucht wird.
    l.toolH = 0.0F;
    const float rowH = ImGui::GetFrameHeightWithSpacing();
    // IMMER Platz lassen, auch ohne Modell.
    //
    // Vorher entstand das Band erst mit einem Modell - und dann sprang die
    // ganze Anordnung, weil die Ansicht darueber ploetzlich kuerzer wurde.
    // Wer die Bedienung schon sieht, weiss auch, was ihn erwartet.
    if (g_app->leftMode == 2) {
        // Modell: zwei Zeilen - Bedienung und Zeitleiste. Beide sind
        // Elementzeilen, also genau zwei Mal rowH, plus die Aussenraender
        // des Bandes.
        l.toolH = rowH * 2.0F + st.ItemSpacing.y * 2.0F;
    } else if (g_app->leftMode == 1) {
        // IMMER Platz lassen, auch ohne Karte - sonst springt die Anordnung
        // beim Laden.
        //
        // Die Regler und Kaestchen sind seit rc173 in der Seitenleiste
        // rechts. Hier bleiben: eine Zeile Bedienung, eine Zeile Abspielen,
        // die angeklickte Entity - und die ZEITLEISTE, deren Hoehe der
        // Anwender einstellt.
        //
        // Hochziehbar wie in Blender: dort zieht man die Kante zwischen
        // Ansicht und Zeitleiste, und die Spuren werden sichtbar. Der
        // Anteil steht in timelineFrac; die Grenzen sorgen dafuer, dass
        // weder die Ansicht noch die Leiste verschwindet.
        // IMMER derselbe Platz, auch ohne Karte.
        //
        // In rc175 bekam der leere Fall nur eine Zeile - damit unten kein
        // leeres Feld klafft. Seit rc177 steht dort aber ein Platzhalter in
        // der Groesse des Bildes, und dann ist die Sonderbehandlung
        // schaedlich: die ganze Anordnung sprang, sobald eine Karte geladen
        // wurde. Gewuenscht war ausdruecklich, dass der leere Fall aussieht
        // wie der volle.
        const float zeit =
            std::clamp(avail.y * g_app->timelineFrac, rowH * 3.0F,
                       std::max(rowH * 3.0F, avail.y * 0.6F));
        l.toolH = rowH * 3.0F + st.ItemSpacing.y * 2.0F + zeit;
        g_app->tlHoeheIst = zeit;
        g_app->tlPlatz = avail.y;
    }

    // Die Statuszeile sitzt GANZ unten, ohne Abstand darunter.
    //
    // Vorher wurde hier zusaetzlich eine Textzeile abgezogen - der Rest
    // einer Ueberschrift, die es nicht mehr gibt. Dadurch blieb unter der
    // Statuszeile ein leerer Streifen stehen, und sie sass eben NICHT ganz
    // unten. In efxed ist sie die letzte Zeile des Fensters; genau so soll
    // es hier auch sein.
    // Die Reiterleiste kostet eine Zeile - sonst laeuft alles unten hinaus.
    const float tabH = g_app->tabs.empty()
                           ? 0.0F
                           : ImGui::GetFrameHeightWithSpacing();
    // --- NEGATIVE Hoehe statt Abzugsrechnung ----------------------------
    //
    // shank, nach zwei Anlaeufen: "eventuell muss das Fenster sauberer
    // aufgebaut werden?" - ja, und ImGui hat dafuer eine eigene Redewendung.
    //
    // Eine NEGATIVE Hoehe in `BeginChild` heisst "alles Uebrige, minus
    // diesem Betrag". Genau so baut ImGui seine eigene Konsole
    // (imgui_demo.cpp:9174):
    //
    //     const float footer = style.SeparatorSize + style.ItemSpacing.y
    //                          + ImGui::GetFrameHeightWithSpacing();
    //     ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer), ...)
    //
    // Der Vorteil ist nicht die Kuerze, sondern dass es NICHT RECHNEN muss:
    // ImGui kennt die verbleibende Hoehe genau, ich muss sie nicht aus
    // avail.y, statusH, toolH, tabH und zwei Abstaenden zusammensetzen. Jede
    // dieser Zahlen war eine Gelegenheit, sich zu vertun - und zwei davon
    // waren falsch (rc470: ein Rahmen, den es nicht mehr gibt, und ein
    // waagerechter Abstand senkrecht abgezogen).
    //
    // `frameH` traegt jetzt einen negativen Wert. Alle vier Spalten - die
    // Modusleiste, Events, Script Flow und die Knoepfe - bekommen denselben,
    // und damit enden sie zwangslaeufig auf DERSELBEN Linie. Nicht "sollten"
    // - koennen gar nicht anders.
    // EIN Abstand, nicht zwei.
    //
    // Am kopflosen Pruefstand (tools/layoutcheck.cpp) gemessen: mit zwei
    // Abstaenden endeten die Spalten bei 1426, die Statuszeile begann bei
    // 1455 - neunundzwanzig Bildpunkte ungenutzt. Genau `fussH`.
    // Ueber der Zeitleiste sitzt die Ziehkante (##tlkante, max(4,
    // ItemSpacing.y) hoch) samt einem Zeilenabstand dahinter. Sie stand in
    // keiner Rechnung: in der Kartenansicht war der Inhalt dadurch 14 Punkte
    // hoeher als das Fenster (Selbsttest), die Statuszeile unten abgeschnitten
    // und das Hauptfenster rollbar.
    const float kanteH = (g_app->leftMode == 1)
                             ? std::max(4.0F, st.ItemSpacing.y) + st.ItemSpacing.y
                             : 0.0F;
    l.fussH = l.toolH + kanteH + l.statusH + st.ItemSpacing.y;
    l.topH = avail.y - l.fussH - tabH;
    // So hoch wie eine Knopfzeile - das ist der hoechste Kopf, den es gibt.
    // --- Die Kopfzeile ist nur so hoch, wie sie sein MUSS --------------
    //
    // Sie stand fest auf `GetFrameHeightWithSpacing()` - der Hoehe eines
    // Knopfes. Bei Map und Model ist das richtig: dort stehen dort Knoepfe
    // ("Load mission...", "Open .glm...").
    //
    // Im Modus Events steht nur Text. Die Differenz zwischen Knopfhoehe und
    // Textzeile blieb als Luecke zwischen der Ueberschrift "Events" und dem
    // Kasten darunter stehen - shank: "warum ist da so viel Abstand
    // zwischen Events-Text und dem Fenster unten?"
    //
    // Also die Hoehe danach richten, was tatsaechlich darin steht.
    l.headerH = (g_app->leftMode == 0)
                    ? ImGui::GetTextLineHeightWithSpacing()
                    : ImGui::GetFrameHeightWithSpacing();
    // NEGATIV, und ohne die Kopfzeile.
    //
    // Am kopflosen Pruefstand gemessen (tools/layoutcheck.cpp): die
    // Kopfzeilen stehen UEBER der Tabelle, nicht in ihr. Wer sie hier
    // abzieht, zieht sie zweimal ab.
    l.frameH = -l.fussH;
    // Nichts Negatives weitergeben - siehe tooSmall.
    // `frameH` ist jetzt IMMER negativ - die Pruefung auf zu klein muss
    // deshalb an `topH` haengen, nicht an ihr.
    l.tooSmall = (l.topH < l.headerH);
    l.topH = std::max(l.topH, 0.0F);
    return l;
}

// Ueberschrift plus umrandeter Bereich - ImGui kennt keine Gruppenrahmen.
// Ein beschrifteter Rahmen. size.y ist die Hoehe des RAHMENS, nicht die von
// Ueberschrift plus Rahmen.
//
// Vorher zog die Funktion selbst eine Textzeile ab. Das taten die anderen
// Spalten auch, jede auf ihre Art - und die Ergebnisse wichen voneinander ab.
// Jetzt kommt der Wert von aussen, aus Layout::frameH, und ist fuer alle
// gleich.
// Ein beschrifteter Rahmen. size.y ist die Hoehe des RAHMENS.
//
// Die Beschriftung bekommt die volle Kopfzeilenhoehe, auch wenn der Text
// weniger braucht - sonst beginnt dieser Rahmen hoeher als der daneben, der
// eine Knopfleiste im Kopf hat.

// Die Rollposition gehoert zum SKRIPT, nicht zum Kasten.
//
// Das fokussierte Feld wird als Hauptbaum gezeichnet, die anderen als
// Vergleichsbaeume - verschiedene Fenster mit je eigener Rollposition. Beim
// Fokuswechsel wandert ein Skript vom einen ins andere und landet dort, wo
// DIESES Fenster zuletzt stand. Gemeldet als "das erste Fenster springt
// ungewollt umher".
//
// Also je Reiter merken und beim Wechsel zurueckstellen. Aufzurufen INNEN,
// gleich nachdem der rollende Kasten begonnen hat.
void rollpositionHolen(const std::string& kasten, int tab) {
    // Verglichen und gemerkt wird die feste Kennung des Reiters, nicht seine
    // Nummer: nach dem Schliessen eines Reiters traegt der Nachruecker
    // dieselbe Nummer - und bekam bisher weder seine eigene Position noch
    // ueberhaupt ein Zuruecksetzen, weil "Nummer gleich" als "nichts
    // gewechselt" galt.
    const int uid = tabKennung(tab);
    int& letzter = g_app->lastTabInBox[kasten];
    if (letzter != uid) {
        letzter = uid;
        const auto es = g_app->tabScroll.find(uid);
        const float wohin = (es == g_app->tabScroll.end()) ? 0.0F : es->second;
        // Auch das war blind, als es sprang.
        char zs[200];
        std::snprintf(zs, sizeof(zs),
                      "Rollposition: Kasten %s zeigt jetzt Reiter %d, "
                      "zurueck auf %.0f",
                      kasten.c_str(), tab, (double)wohin);
        diag::detail(zs);
        ImGui::SetScrollY(wohin);
    }
}

void rollpositionMerken(int tab) {
    g_app->tabScroll[tabKennung(tab)] = ImGui::GetScrollY();
}


bool beginGroupBox(const char* label, const char* id, ImVec2 size,
                   float headerH) {
    ImGui::BeginGroup();
    const float top = ImGui::GetCursorPosY();
    ImGui::TextUnformatted(label);
    if (headerH > 0.0F) {
        ImGui::SetCursorPosY(top + headerH);
    ImGui::Dummy(ImVec2{0.0F, 0.0F});
    }
    return ImGui::BeginChild(id, size, ImGuiChildFlags_Borders);
}

void endGroupBox() {
    ImGui::EndChild();
    ImGui::EndGroup();
}




// Eine .ent-Datei laden.
//
// Movie Duels arbeitet so: die .bsp bleibt unveraendert, die Entities kommen
// aus einer eigenen Datei daneben. md_twj_jedi.ent hat 132 Eintraege, und
// die sind es, die das Skript anspricht - nicht die der Originalkarte.









// Modusleiste am linken Rand, wie bei g2c.
//
// Zwei Knoepfe statt Auswahlkreisen: die Umschaltung wechselt die GANZE
// linke Spalte, nicht nur eine Einstellung darin. Am Rand ist das sichtbar
// genug, um nicht versehentlich im falschen Modus zu arbeiten.
//
// Die Breite kommt aus dem TEXT, nicht aus einer festen Zahl. Bei g2c war
// "XSI -> GLA" auf Englisch abgeschnitten, waehrend dieselbe Zahl fuer die
// kuerzeren chinesischen Beschriftungen zu breit gewesen waere.
float drawModeBar(float height, float headerH) {
    const struct {
        int mode;
        Str label;
        Str hint;
    } modes[] = {
        {0, Str::ViewEvents, Str::ModeEventsHint},
        {1, Str::ViewMap, Str::ModeMapHint},
        {2, Str::ViewModel, Str::ModeModelHint},
    };

    float textW = 0.0F;
    for (const auto& m : modes) {
        textW = std::max(textW, ImGui::CalcTextSize(tr(m.label)).x);
    }
    const ImGuiStyle& st = ImGui::GetStyle();
    const float barW = textW + st.FramePadding.x * 2.0F +
                       st.WindowPadding.x * 2.0F + ImGui::GetFontSize() * 0.5F;

    // Eine Zeile Platz fuer die Ueberschrift lassen, obwohl die Leiste keine
    // hat.
    //
    // Alle drei Spalten daneben sind ueber beginGroupBox gebaut: erst eine
    // Textzeile mit dem Namen, dann der Rahmen. Ohne diese Zeile beginnt die
    // Modusleiste eine Textzeilenhoehe weiter oben - sichtbar, weil die
    // Rahmen dann nicht auf einer Linie liegen.
    // Kein Kopf, aber derselbe Platz dafuer.
    ImGui::BeginGroup();
    const float top = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(top + headerH);
    ImGui::Dummy(ImVec2{0.0F, 0.0F});
    ImGui::BeginChild("modebar", ImVec2{barW, height},
                      ImGuiChildFlags_Borders);
    for (const auto& m : modes) {
        const bool active = (g_app->leftMode == m.mode);
        // Alle drei immer erreichbar. Vorher war "Karte" ausgegraut, solange
        // nichts geladen war - dann kam man nie an die Knoepfe zum Laden.
        const bool possible = true;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.20F, 0.42F, 0.68F, 1.0F});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4{0.24F, 0.48F, 0.76F, 1.0F});
        }
        ImGui::BeginDisabled(!possible);
        if (ImGui::Button(tr(m.label), ImVec2{-1, ImGui::GetFontSize() * 2.6F})) {
            g_app->leftMode = m.mode;
        }
        ImGui::EndDisabled();
        if (active) {
            ImGui::PopStyleColor(2);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(m.hint));
        }
        ImGui::Spacing();
    }
    ImGui::EndChild();
    ImGui::EndGroup();
    return barW;
}

// --- Die LINKE Spalte, je nach Modus --------------------------------
//
// Der Name ist alt und irrefuehrend: die Funktion zeichnet je nach Modus
// etwas voellig anderes.
//
//   Events   Modusleiste, Ereignisliste
//   Map      Modusleiste, Ebenenliste, Kartenansicht, Kamerazeile,
//            Zeitleiste
//   Model    Modusleiste, Modellansicht, Abspielzeile
//
// shank nach rc478: "ich glaube wir muessen alle 3 Modi als separate UIs
// betrachten." Gemessen gibt ihm das recht - `leftMode` kommt in dieser
// Datei vierundzwanzig Mal vor, sechs davon hier drin und sieben in
// `computeLayout`.
//
// DAS ist der Grund, warum jede Korrektur an einem Modus die anderen beiden
// trifft: die Tabellengewichte zerlegten die Karte (rc474), die
// Kopfzeilenhoehe galt fuer alle drei (rc478), die Platzhalter im Script
// Flow ebenso (rc477).
//
// Der Umbau steht in LAYOUT.md, Abschnitt 2.3: drei Erzeuger fuer `Layout`
// und drei Zeichenfunktionen statt Abfragen. Er ist NICHT in dieser Runde
// gemacht - er ist gross, und die letzten Runden haben gezeigt, was
// passiert, wenn ich hier blind umbaue.
//
// Was hier steht, ist die Verzweigung an EINER Stelle statt verstreut:
// wer einen Modus gestalten will, sieht sofort, wo er anfaengt.
void drawEventsList(const Layout& l) {
    // --- Die linke Spalte ist selbst eine TABELLE -----------------------
    //
    // shank: "ich glaube Tabelle ist besser."
    //
    // rc500 hat die Luecke behoben, indem eine geschaetzte Subtraktion
    // durch eine Messung ersetzt wurde. Das half an DIESER Stelle; die
    // Bauart blieb - Modusleiste, Ebenen und Inhalt standen weiter per
    // `SameLine` nebeneinander.
    //
    // Als Tabelle gibt es die Rechnung gar nicht. Jede Zelle klemmt ihren
    // Inhalt und faengt ihre Klicks; Luecken und Ueberlappen sind
    // ausgeschlossen statt unwahrscheinlich - dieselbe Lehre wie im
    // Hauptfenster (rc473) und bei den Helferspalten (rc488).
    //
    // Die Ebenenliste bekommt dabei einen ziehbaren Teiler, den es vorher
    // nicht gab.
    //
    // EIGENE ID JE MODUS: eine ID, ein Zustand (rc495).
    const bool mitEbenen = (g_app->leftMode == 1);
    const char* const innenId = (g_app->leftMode == 1)   ? "##linksMap"
                                : (g_app->leftMode == 2) ? "##linksModel"
                                                         : "##linksEvents";
    if (!ImGui::BeginTable(innenId, mitEbenen ? 3 : 2,
                           ImGuiTableFlags_Resizable |
                               ImGuiTableFlags_NoSavedSettings |
                               ImGuiTableFlags_NoPadOuterX,
                           ImVec2{0.0F, 0.0F})) {
        return;
    }
    // Die Modusleiste ist so breit wie ihre Beschriftungen und wird nicht
    // gezogen - wie die Actions-Spalte im Hauptfenster (rc481).
    ImGui::TableSetupColumn("mode",
                            ImGuiTableColumnFlags_WidthFixed |
                                ImGuiTableColumnFlags_NoResize,
                            std::max(g_app->modeBarW,
                                     ImGui::GetFontSize() * 4.0F));
    if (mitEbenen) {
        // NoResize, und das ist kein Rueckschritt.
        //
        // rc501 hat der Ebenenliste einen ziehbaren Teiler gegeben - den
        // hatte niemand verlangt, ich fand ihn nur gut. Auf shanks Bild ist
        // die Spalte dadurch rund 760 breit statt der angeforderten 315:
        // die Liste darin bleibt 315, der Rest ist Luecke IN der Zelle.
        //
        // Der Grund steht in der Recherche zu rc481: eine ZIEHBARE Spalte
        // nimmt `init_width_or_weight` nur beim Anlegen, danach gewinnt
        // ImGuis Selbstanpassung. Eine feste ohne Ziehen nimmt sie in jedem
        // Bild - und genau das braucht diese Liste, deren Inhalt eine feste
        // Breite hat.
        //
        // Vor dem Umbau war sie ebenfalls nicht ziehbar. Es geht also
        // nichts verloren; es kommt nur nichts dazu.
        ImGui::TableSetupColumn("ebenen",
                                ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_NoResize,
                                ImGui::GetFontSize() * 15.0F);
    }
    ImGui::TableSetupColumn("inhalt", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    g_app->modeBarW = drawModeBar(l.frameH, l.headerH);
    if (mitEbenen) {
        ImGui::TableNextColumn();
    }

    // Bei der Karte die Ebenenliste - RECHTS von der Modusleiste, links von
    // der Ansicht.
    //
    // Sie ist IMMER da, auch ohne geladene Karte. Vorher entstand sie erst
    // mit den Entities, und dann sprang die ganze Anordnung beim Laden. Wer
    // die Gliederung schon sieht, weiss, was ihn erwartet.
    if (g_app->leftMode == 1) {
        ImGui::BeginGroup();
        const float top = ImGui::GetCursorPosY();
        ImGui::TextUnformatted(tr(Str::EntityLayers));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("%s", tr(Str::EntityLayersHint));
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(g_app->map.entities.empty());
        if (ImGui::SmallButton(tr(Str::EntityShowAll))) {
            g_app->hiddenClasses.clear();
            g_app->hiddenEntities.clear();
            g_app->mapDirty = true;
        }
        ImGui::EndDisabled();
        ImGui::SetCursorPosY(top + l.headerH);
        // Ein leeres Element als Anker: ImGui verlangt nach SetCursorPos ein
        // abgeschicktes Element, sonst bricht es ab. Was danach kommt, kann
        // vorzeitig zurueckkehren - der Anker nicht.
        ImGui::Dummy(ImVec2{0.0F, 0.0F});
        drawEntityLayers(l.frameH);
        ImGui::EndGroup();
    }

    // Dritte Zelle: der eigentliche Inhalt.
    ImGui::TableNextColumn();

    // --- Die Restbreite MESSEN, nicht abziehen --------------------------
    //
    // Hier stand:
    //
    //     restW = Zellbreite;
    //     if (Modus == Karte) restW -= 15 Zeichenhoehen + Abstand;
    //
    // Die 15 Zeichenhoehen sind die Breite, die `drawEntityLayers` seinem
    // Kindfenster gibt. Der GRUPPE davor gehoert aber auch die Kopfzeile
    // ("Entities", "Show all"), und ImGui nimmt fuer eine Gruppe das
    // Breiteste ihrer Zeilen. Was die Gruppe wirklich einnimmt, weiss
    // niemand vorher - abgezogen wurde eine geschaetzte Zahl.
    //
    // Genau daraus entstand shanks Luecke: "neben dem Map-Fenster und dem
    // Script Flow ist eine riesige Luecke."
    //
    // Nach `SameLine` steht die wirkliche Restbreite fest, und
    // `GetContentRegionAvail` nennt sie. Dieselbe Regel wie bei der
    // Actions-Spalte (rc474), der Modusleiste (rc473), dem Script Flow
    // (rc477) und den Helferspalten (rc485): messen statt rechnen.
    // Die ZELLE gibt die Breite vor - keine Subtraktion mehr noetig, die
    // Tabelle hat den Platz schon verteilt.
    const float restW = std::max(ImGui::GetContentRegionAvail().x, 80.0F);
    g_app->dbgRestW = restW;
    // Was nach der Modusleiste in der Zelle uebrig ist. Frueher stand hier
    // `l.eventsW`, die selbst gerechnete Spaltenbreite - jetzt fragen wir
    // die Zelle.
    g_app->dbgZelleW = ImGui::GetContentRegionAvail().x;

    // Die Werkzeugleiste steht AUSSERHALB des Rahmens, auf gleicher Hoehe
    // wie die Ueberschrift "Script Flow" daneben. Vorher lag sie darin, und
    // der Inhalt rutschte gegenueber dem Skriptfenster nach unten.
    if (g_app->leftMode == 1 || g_app->leftMode == 2) {
        ImGui::BeginGroup();
        // Die Hoehe der Werkzeugleiste MESSEN, nicht schaetzen: die rechte
        // Spalte zieht ueber beginGroupBox eine Textzeilenhoehe ab, eine
        // Knopfzeile ist aber hoeher. Wer hier eine feste Zahl einsetzt,
        // bekommt zwei Rahmen, die nicht auf gleicher Hoehe enden - genau
        // das war der Fall.
        // Die Werkzeugleiste FUELLT die Kopfzeile - sie ist genau eine
        // Knopfzeile hoch, und danach bemisst sich l.headerH. Kein Abziehen
        // noetig: der Rahmen ist derselbe wie ueberall.
        // Die Werkzeugleiste in einen Kasten von genau der Spaltenbreite.
        //
        // Ohne das waechst sie ueber die Spalte hinaus, sobald ein Knopf
        // dazukommt - beim Laden einer Karte etwa das Feld "Scripts (2)".
        // ImGui vergroessert dann den Inhalt des Hauptfensters, und die
        // Spalten rechts rutschen aus dem Bild.
        const float top = ImGui::GetCursorPosY();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
        ImGui::BeginChild("toolbar", ImVec2{restW, l.headerH},
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        if (g_app->leftMode == 1) {
            drawMapToolbar();
        } else {
            drawModelToolbar();
        }
        ImGui::EndChild();
        ImGui::SetCursorPosY(top + l.headerH);
        // Ein leeres Element als Anker: ImGui verlangt nach SetCursorPos ein
        // abgeschicktes Element, sonst bricht es ab. Was danach kommt, kann
        // vorzeitig zurueckkehren - der Anker nicht.
        ImGui::Dummy(ImVec2{0.0F, 0.0F});
        if (ImGui::BeginChild(g_app->leftMode == 1 ? "mapview" : "modelview",
                              ImVec2{restW, l.frameH},
                              ImGuiChildFlags_Borders)) {
            if (g_app->leftMode == 1) {
                drawMapView();
            } else {
                drawModelView();
            }
        }
        ImGui::EndChild();
        ImGui::EndGroup();
        // Auch HIER die Tabelle schliessen.
        //
        // Genau das hat rc501 zum Absturz gebracht:
        //
        //   IMGUI-ZUSICHERUNG: (0) && "Missing EndTable()"
        //
        // Ich hatte im Kommentar am Funktionsende behauptet, ein `return`
        // dazwischen gaebe es nur bei fehlgeschlagenem BeginTable. Das war
        // falsch - dieses hier stand schon immer da, und ich habe es beim
        // Umbau nicht gesucht, sondern angenommen.
        ImGui::EndTable();
        return;
    }

    if (beginGroupBox(tr(Str::GroupEvents), "events",
                      ImVec2{restW, l.frameH}, l.headerH)) {
        for (std::size_t i = 0; i < g_app->db.commands.size(); ++i) {
            const Command& c = g_app->db.commands[i];
            const std::string label = c.name + "  " + signatureText(c);
            ImGui::PushID(static_cast<int>(i));
            drawIcon(c.icon.empty() ? (c.block ? "I_BRACE" : "I_EVENT") : c.icon.c_str(),
                     false);
            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
            // Doppelklick FUEGT EIN - er oeffnet nicht den Editor. So macht
            // es das Original: der Befehl landet mit seinen Vorgabewerten im
            // Baum, und erst ein Doppelklick DORT oeffnet den Event-Editor.
            const bool sel = (static_cast<int>(i) == g_app->selectedCommand);
            const KeepSelected keep(sel, g_app->selectionOwner == 1);
            if (ImGui::Selectable(label.c_str(), sel,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                const bool doppelt =
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                // Ein zweiter Klick auf DASSELBE schaltet die Auswahl ab.
                //
                // Vorher blieb sie kleben: einmal angeklickt, und die Zeile
                // war blau, bis man eine andere waehlte. Beim Doppelklick
                // gilt das nicht - der fuegt ein, und dabei soll die
                // Auswahl stehen bleiben.
                if (sel && !doppelt) {
                    g_app->selectedCommand = -1;
                } else {
                    g_app->selectedCommand = static_cast<int>(i);
                    g_app->selectionOwner = 1;
                }
                if (doppelt) {
                    insertCommand(c);
                }
            }
            // Ziehen statt Doppelklick. Der Doppelklick bleibt - er setzt
            // hinter die Auswahl, wie das Original. Ziehen sagt dagegen
            // GENAU, wohin: auch zwischen zwei bestehende Zeilen.
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                const NewDrag d{0, static_cast<int>(i)};
                ImGui::SetDragDropPayload("bhed_new", &d, sizeof(d));
                drawIcon(c.icon.empty() ? (c.block ? "I_BRACE" : "I_EVENT")
                                        : c.icon.c_str(),
                         false);
                ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::TextUnformatted(c.name.c_str());
                ImGui::EndDragDropSource();
            }
            // descFor statt c.desc: vier Befehle haben in der .bhc keine
            // Beschreibung (camera, task, do, play), und dafuer gibt es
            // einen Ersatz - siehe descFor().
            const std::string hilfe = descFor(c);
            if (!hilfe.empty()) {
                // AN DER MAUS zeigen, nicht nur unten in der Hilfszeile.
                //
                // Wer die Liste durchgeht, schaut auf die Liste - der Blick
                // an den unteren Fensterrand und zurueck kostet mehr, als
                // die Beschreibung wert ist.
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s", hilfe.c_str());
                }
            }
            ImGui::PopID();
        }
        for (std::size_t mi = 0; mi < g_app->db.macroBodies.size(); ++mi) {
            const Macro& m = g_app->db.macroBodies[mi];
            ImGui::PushID(m.name.c_str());
            drawIcon("I_MACRO", false);
            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
            const std::string label =
                m.name + "  (" + std::to_string(m.body.size()) + ")";
            if (ImGui::Selectable(label.c_str(), false,
                                  ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                insertMacro(m);
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                const NewDrag d{1, static_cast<int>(mi)};
                ImGui::SetDragDropPayload("bhed_new", &d, sizeof(d));
                drawIcon("I_MACRO", false);
                ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::TextUnformatted(m.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered() && !m.desc.empty()) {
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s", m.desc.c_str());
                }
            }
            ImGui::PopID();
        }

        // Dieselbe Regel wie im Baum: ins Leere geklickt heisst nichts
        // gewaehlt. Vorher blieb der einmal angeklickte Befehl fuer immer
        // blau, egal wohin man danach klickte.
        const ImVec2 rest = ImGui::GetContentRegionAvail();
        if (rest.y > 1.0F) {
            ImGui::InvisibleButton("##leer", ImVec2{std::max(rest.x, 1.0F), rest.y});
            if (ImGui::IsItemClicked() && !ImGui::GetIO().KeyShift &&
                !ImGui::GetIO().KeyCtrl) {
                g_app->selectedCommand = -1;
            }
        }
        // Und ein Klick IRGENDWO in die Liste, der keinen Eintrag trifft.
        //
        // Die Leerflaeche oben gibt es nur, wenn die Liste kuerzer ist als
        // das Fenster. Bei einer vollen Liste - und die Ereignisliste ist
        // immer voll - blieb die Auswahl haengen, egal wohin man klickte.
        // Deshalb zusaetzlich: Fenster getroffen, kein Eintrag darunter,
        // keine Zusatztaste.
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered() && !ImGui::GetIO().KeyShift &&
            !ImGui::GetIO().KeyCtrl) {
            g_app->selectedCommand = -1;
        }
    }
    endGroupBox();
    // Die Tabelle der linken Spalte schliessen. Sie wurde ganz oben in
    // dieser Funktion geoeffnet.
    //
    // ACHTUNG: es gibt einen ZWEITEN Ausstieg weiter oben (Karte und
    // Modell), und der schliesst sie ebenfalls. Wer hier einen dritten
    // einbaut, muss es auch tun - `lint_endtable.py` prueft das.
    ImGui::EndTable();
}


// Ist diese Zeile ausgewaehlt?

// Die Auswahl fuer die Bearbeitung. Leer heisst: nichts gewaehlt.

// Auswahl mit den Pfeiltasten bewegen.
//
// Ueber die ANGEZEIGTEN Zeilen, nicht ueber die Baumpfade: der Nutzer sieht
// Zeilen und erwartet, dass die naechste Zeile drankommt - auch wenn sie
// eine Ebene tiefer liegt.
void moveSelectionByKey(int delta) {
    if (g_app->rows.empty()) {
        return;
    }
    int now = g_app->selected;
    if (now < 0) {
        now = (delta > 0) ? -1 : static_cast<int>(g_app->rows.size());
    }
    const int next = std::clamp(now + delta, 0,
                                static_cast<int>(g_app->rows.size()) - 1);
    if (next == now) {
        return;
    }
    selectRow(static_cast<std::size_t>(next), false,
              ImGui::GetIO().KeyShift);
    g_app->mapDirty = true;
    g_app->scrollToSelected = true;
}

// Die Spaltenmasse kommen aus dem Kern (bhed/tree.h) - dort stehen sie mit
// den gemessenen Werten und werden von einer Probe festgehalten. Hier kommt
// nur noch die eine Sache dazu, die ImGui braucht: kein Befehlsname darf in
// die Klammerspalte laufen.
TreeMetrics treeMetrics() {
    TreeMetrics m = treeMetricsFor(ImGui::GetFontSize());
    float longest = 0.0F;
    for (const Command& c : g_app->db.commands) {
        longest = std::max(longest, ImGui::CalcTextSize(c.name.c_str()).x);
    }
    m.nameW = std::max(m.nameW, longest + ImGui::GetStyle().ItemInnerSpacing.x * 2.0F);
    return m;
}

// Eine gepunktete Linie, wie sie eine Win32-Baumansicht zieht: ein Punkt,
// ein Punkt Luecke. Am Original nachgemessen - dort sitzt jeder zweite
// Bildpunkt. Deshalb ein Rechteck je Punkt und keine durchgezogene Linie.
void dottedLine(ImDrawList* dl, float x0, float y0, float x1, float y1,
                ImU32 col) {
    if (y0 == y1) {                                   // waagerecht
        const auto start = static_cast<int>(std::ceil(x0));
        for (int x = start; x < static_cast<int>(x1); x += 2) {
            dl->AddRectFilled(ImVec2{static_cast<float>(x), y0},
                              ImVec2{static_cast<float>(x) + 1.0F, y0 + 1.0F}, col);
        }
        return;
    }
    const auto start = static_cast<int>(std::ceil(y0));
    for (int y = start; y < static_cast<int>(y1); y += 2) {
        dl->AddRectFilled(ImVec2{x0, static_cast<float>(y)},
                          ImVec2{x0 + 1.0F, static_cast<float>(y) + 1.0F}, col);
    }
}

// Das Aufklappkaestchen des Originals: ein Quadrat mit Rahmen, darin ein
// Minus, bei zugeklappten Bloecken zusaetzlich ein senkrechter Strich.
void expanderBox(ImDrawList* dl, ImVec2 centre, float side, bool open,
                 ImU32 border, ImU32 sign) {
    const float h = std::floor(side * 0.5F);
    const ImVec2 a{centre.x - h, centre.y - h};
    const ImVec2 b{centre.x + h + 1.0F, centre.y + h + 1.0F};
    // Gefuellt, nicht durchsichtig: sonst laeuft die gepunktete Linie mitten
    // durch das Quadrat. Im Original ist es innen leer.
    dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_WindowBg));
    dl->AddRect(a, b, border);
    const float pad = 2.0F;
    dl->AddRectFilled(ImVec2{a.x + pad, centre.y},
                      ImVec2{b.x - pad, centre.y + 1.0F}, sign);
    if (!open) {
        dl->AddRectFilled(ImVec2{centre.x, a.y + pad},
                          ImVec2{centre.x + 1.0F, b.y - pad}, sign);
    }
}

// Die rechte Haelfte der geteilten Ansicht.
//
// Bewusst SCHLICHT: Name und Argumente, Einrueckung, anklickbar zum
// Kopieren. Kein Ziehen, kein Bearbeiten, keine Mehrfachauswahl.
//
// Warum nicht derselbe Baum wie links: der schreibt in g_app->rows,
// g_app->selection und g_app->selected, und daran haengt alles - Einfuegen,
// Loeschen, Rueckgaengig. Zwei davon nebeneinander hiesse, jede dieser
// Stellen zu verdoppeln. Die rechte Haelfte soll aber gar nicht
// bearbeiten; sie soll zeigen und hergeben. Dafuer genuegt das hier.

// Einen Knoten von einem Feld in ein anderes ziehen.
//
// Kopieren, nicht verschieben. Das ist die vorsichtige Wahl: ein Zug, der
// im Quellskript etwas loescht, kann bei einem Fehlgriff Arbeit vernichten,
// und Rueckgaengig muesste dann in ZWEI Dokumenten zurueckgenommen werden,
// um den alten Stand herzustellen. Wer verschieben will, loescht danach von
// Hand - das ist ein Handgriff und jederzeit umkehrbar.
//
// Eingefuegt wird HINTER der Zeile, auf die man loslaesst - so, wie man es
// beim Ablegen erwartet. `nachRow == -1` heisst: auf die freie Flaeche
// darunter, also ans Ende des Skripts (auch eines leeren).
void knotenZiehen(const App::DragNode& von, int nachPane, int nachRow);

// Der Knoten hinter einer "bhed_split"-Nutzlast (Feld, Zeile).
const Node* knotenAusNebenfeld(const ImGuiPayload* nutzlast) {
    if (nutzlast == nullptr ||
        nutzlast->DataSize != static_cast<int>(sizeof(int) * 2)) {
        return nullptr;
    }
    int nutz[2] = {0, 0};
    std::memcpy(nutz, nutzlast->Data, sizeof(nutz));
    const int vonPane = std::clamp(nutz[0], 0, App::kMaxSplit - 1);
    const App::SplitPane& quelle = g_app->splitPanes[vonPane];
    const std::size_t at = static_cast<std::size_t>(std::max(nutz[1], 0));
    if (at >= quelle.rows.size() || quelle.tab < 0 ||
        quelle.tab >= static_cast<int>(g_app->tabs.size())) {
        return nullptr;
    }
    const Script& src =
        (quelle.tab == g_app->activeTab)
            ? g_app->doc.script()
            : g_app->tabs[static_cast<std::size_t>(quelle.tab)].doc.script();
    return nodeAt(src, quelle.rows[at].path);
}

// Die freie Flaeche eines Feldes als Ablageziel.
//
// shank, mit zwei Bildern: "ich kann nichts in ein leeres Feld ziehen ...
// auch nichts in das leere Feld unter den anderen, dass es darunter
// eingefuegt wird."
//
// Bisher nahm nur eine ZEILE einen Zug an. Wo keine Zeile ist - unter der
// letzten, oder in einem leeren Skript ("No script loaded") -, gab es kein
// Ziel, und der Zug verpuffte.
//
// ImGui-Weg (Diskussion #1771): nach EndChild ist das ganze Kindfenster ein
// Element, und BeginDragDropTarget danach macht die GANZE Flaeche zum Ziel.
// Die Zeilen darin bleiben vorn: bei ueberlappenden Zielen nimmt
// AcceptDragDropPayload das mit der KLEINEREN Flaeche (imgui.cpp,
// `DragDropAcceptIdCurrRectSurface`). Auf einer Zeile gilt also weiter
// "hinter diese Zeile", daneben "ans Ende".
//
// `zielPane` -1 ist der Hauptbaum (fokussiertes Feld, aktiver Reiter).
void ablageFlaeche(int zielPane) {
    if (!ImGui::BeginDragDropTarget()) {
        return;
    }
    const int nTabs = static_cast<int>(g_app->tabs.size());
    const int zielTab =
        (zielPane < 0)
            ? g_app->activeTab
            : g_app->splitPanes[std::clamp(zielPane, 0, App::kMaxSplit - 1)].tab;
    if (nTabs > 0 && (zielTab < 0 || zielTab >= nTabs)) {
        ImGui::EndDragDropTarget();
        return;
    }
    Document& ziel = (nTabs == 0 || zielTab == g_app->activeTab)
                         ? g_app->doc
                         : g_app->tabs[static_cast<std::size_t>(zielTab)].doc;
    bool geaendert = false;
    std::string was;

    // Neu aus der Ereignisliste.
    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("bhed_new")) {
        if (pl->DataSize == static_cast<int>(sizeof(NewDrag))) {
            NewDrag neu{};
            std::memcpy(&neu, pl->Data, sizeof(neu));
            if (&ziel == &g_app->doc) {
                const std::size_t vorher = g_app->doc.script().nodes.size();
                dropNewAt(neu, Path{}, DropWhere::After);
                geaendert = g_app->doc.script().nodes.size() != vorher;
            } else {
                for (const Node& n : knotenFuerNeu(neu)) {
                    geaendert = ziel.insertAfter(Path{}, n) || geaendert;
                }
            }
            was = "neu aus der Ereignisliste";
        }
    }
    // Kopie aus einem Nebenfeld.
    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("bhed_split")) {
        if (const Node* nd = knotenAusNebenfeld(pl)) {
            const Node kopie = *nd;
            geaendert = ziel.insertAfter(Path{}, kopie);
            was = "Kopie aus Nebenfeld \"" + kopie.name + "\"";
        }
        g_app->dragFrom.clear();
        g_app->dragFromTab = -1;
    }
    // Eine Zeile aus dem Hauptbaum oder einem Nebenfeld.
    if (ImGui::AcceptDragDropPayload("bhed_node") != nullptr) {
        const bool imselben = zielPane < 0 && g_app->dragging &&
                              g_app->dragFromPane < 0 &&
                              g_app->dragFromTab == g_app->activeTab;
        if (imselben) {
            // Umordnen ans Ende: hinter die letzte Zeile der obersten Ebene.
            const std::size_t n = g_app->doc.script().nodes.size();
            const std::vector<Path> m = selectionOrCurrent();
            const std::vector<Kennung> bewegt = kennungenVon(
                m.size() > 1 ? m : std::vector<Path>{g_app->dragFrom});
            if (n > 0) {
                const Path letzte{n - 1};
                if (m.size() > 1) {
                    geaendert = g_app->doc.moveAllTo(m, letzte);
                } else if (!g_app->dragFrom.empty() && g_app->dragFrom != letzte) {
                    geaendert = g_app->doc.moveTo(g_app->dragFrom, letzte, nullptr);
                }
                if (geaendert) {
                    markiereKennungen(bewegt);
                } else {
                    zeigeAbweisung();
                }
            }
            was = "ans Ende verschoben";
        } else if (!g_app->dragFrom.empty()) {
            App::DragNode dn;
            dn.pane = g_app->dragFromPane;
            dn.tab = g_app->dragFromTab;
            dn.row = -1;
            knotenZiehen(dn, zielPane, -1);   // schreibt selbst ins Protokoll
            if (zielPane >= 0) {
                g_app->splitPanes[zielPane].dirty = true;
            }
        }
        g_app->dragging = false;
        g_app->dbgZiehWeg.clear();
        g_app->dragFrom.clear();
        g_app->dragFromTab = -1;
    }
    ImGui::EndDragDropTarget();
    if (!geaendert) {
        return;
    }
    diag::detail("Ablegen auf freier Flaeche von Feld " + std::to_string(zielPane) +
                 " (Reiter " + std::to_string(zielTab) + "): " + was + " - ans Ende");
    for (App::SplitPane& sp : g_app->splitPanes) {
        if (sp.tab == zielTab) { sp.dirty = true; }
    }
    if (&ziel == &g_app->doc) {
        rebuildTree();
    }
}

void knotenZiehen(const App::DragNode& von, int nachPane, int nachRow) {
    // -1 heisst HAUPTBAUM.
    //
    // Er gehoert dem fokussierten Feld und zeigt den aktiven Reiter; seine
    // Zeilen stehen in g_app->rows, nicht in einem Feld. Beide Seiten
    // koennen der Hauptbaum sein - man zieht aus ihm heraus UND in ihn
    // hinein.
    const int n = static_cast<int>(g_app->tabs.size());
    const std::vector<Row>& qz =
        (von.pane < 0) ? g_app->rows
                       : g_app->splitPanes[std::clamp(von.pane, 0,
                                                      App::kMaxSplit - 1)]
                             .rows;
    const std::vector<Row>& zz =
        (nachPane < 0) ? g_app->rows
                       : g_app->splitPanes[std::clamp(nachPane, 0,
                                                      App::kMaxSplit - 1)]
                             .rows;
    const int zielTab =
        (nachPane < 0)
            ? g_app->activeTab
            : g_app->splitPanes[std::clamp(nachPane, 0, App::kMaxSplit - 1)]
                  .tab;
    if (von.pane >= App::kMaxSplit || nachPane >= App::kMaxSplit) {
        return;
    }
    if (von.tab < 0 || von.tab >= n || zielTab < 0 || zielTab >= n) {
        return;
    }
    if (nachRow < -1 || nachRow >= static_cast<int>(zz.size())) {
        return;
    }
    // Der QUELLWEG kommt aus dragFrom, nicht aus einer Zeilennummer.
    //
    // Zeilennummern gelten nur im jeweiligen Baum: derselbe Knoten steht im
    // Hauptbaum an einer anderen Stelle als im Vergleichsfeld, weil dort
    // andere Zweige auf- oder zugeklappt sind. Der Weg dagegen ist
    // eindeutig.
    if (g_app->dragFrom.empty()) {
        return;
    }
    (void)qz;

    // Aus welchem Dokument? Der aktive Reiter hat das LEBENDE, die anderen
    // ihr geparktes - dieselbe Unterscheidung wie ueberall sonst.
    const Script& quellSkript =
        (von.tab == g_app->activeTab)
            ? g_app->doc.script()
            : g_app->tabs[static_cast<std::size_t>(von.tab)].doc.script();
    const Node* nd = nodeAt(quellSkript, g_app->dragFrom);
    if (nd == nullptr) {
        return;
    }
    const Node kopie = *nd;
    // Leerer Weg: insertAfter haengt ans Ende.
    const Path zielWeg =
        (nachRow < 0) ? Path{} : zz[static_cast<std::size_t>(nachRow)].path;

    Document& ziel =
        (zielTab == g_app->activeTab)
            ? g_app->doc
            : g_app->tabs[static_cast<std::size_t>(zielTab)].doc;
    const bool ok = ziel.insertAfter(zielWeg, kopie);

    // Ins Protokoll, mit allem, was zum Nachvollziehen noetig ist. Beim
    // Ziehen zwischen zwei Dokumenten sieht man im Bild nur das Ergebnis -
    // nicht, WOHER es kam.
    char z[320];
    std::snprintf(z, sizeof(z),
                  "Ziehen: \"%s\" aus Feld %d (Reiter %d) nach Feld %d "
                  "(Reiter %d), hinter Zeile %d (-1 = ans Ende) - %s",
                  nd->name.c_str(),
                  von.pane, von.tab, nachPane, zielTab, nachRow,
                  ok ? "eingefuegt" : "ABGELEHNT");
    diag::detail(z);
    if (!ok) {
        return;
    }

    // Der Baum des Ziels muss neu - und wenn das Ziel das lebende Dokument
    // ist, auch der Hauptbaum.
    if (nachPane >= 0) {
        g_app->splitPanes[nachPane].dirty = true;
    }
    if (zielTab == g_app->activeTab) {
        rebuildTree();
    }
}

// Breite des Randes links im Baum: Platz fuer den Lesezeichenpunkt und den
// Aenderungsstreifen daneben.
float rinnenBreite() {
    return std::floor(ImGui::GetFontSize() * 1.15F);
}

// Eine Zeile im Rand: rechts der Aenderungsstreifen (orange / gruen /
// blassblau), links davon der Lesezeichenpunkt - die Anordnung aus
// Notepad++ (Lesezeichen, dann Aenderungsrand, dann der Text).
void zeichneRinne(ImDrawList* dl, float x0, float breite, float y, float hoehe,
                  std::uint8_t marke, bool lesezeichen) {
    const float streifen = std::max(3.0F, std::floor(ImGui::GetFontSize() * 0.2F));
    if (marke != kMarkeKeine) {
        const ImU32 farbe = (marke == kMarkeGeaendert)     ? IM_COL32(255, 145, 30, 255)
                            : (marke == kMarkeGespeichert) ? IM_COL32(70, 185, 70, 255)
                                                           : IM_COL32(120, 180, 235, 255);
        dl->AddRectFilled(ImVec2{x0 + breite - streifen - 1.0F, y},
                          ImVec2{x0 + breite - 1.0F, y + hoehe}, farbe);
    }
    if (lesezeichen) {
        const float r = std::floor(ImGui::GetFontSize() * 0.32F);
        const ImVec2 m{x0 + (breite - streifen) * 0.5F, y + ImGui::GetFontSize() * 0.5F};
        dl->AddCircleFilled(m, r, IM_COL32(45, 110, 225, 255));
        dl->AddCircleFilled(ImVec2{m.x - r * 0.3F, m.y - r * 0.3F}, r * 0.35F,
                            IM_COL32(160, 200, 255, 255));   // Glanzpunkt wie in Notepad++
        dl->AddCircle(m, r, IM_COL32(20, 60, 150, 255));
    }
}

void drawSplitTree(const Layout& l, int pane, float breite, float hoehe) {
    App::SplitPane& sp = g_app->splitPanes[pane];
    const int n = static_cast<int>(g_app->tabs.size());
    sp.tab = std::clamp(sp.tab, 0, std::max(0, n - 1));

    // Dieselbe Kennung wie im fokussierten Zustand - siehe drawTree().
    const std::string kennung = "pane" + std::to_string(pane);
    // Ein Klick irgendwohin in dieses Feld gibt ihm den Fokus - danach ist
    // es das bearbeitbare. So macht es VS Code auch: die angeklickte Gruppe
    // bekommt den Schreibzeiger.
    //
    // Angefordert, nicht sofort ausgefuehrt: mitten im Zeichnen das
    // Dokument zu tauschen zoege dem gerade gezeichneten Baum den Boden
    // weg. Das Raster fuehrt es nach dem Zeichnen aus.
    auto fokusAnfordern = [pane] {
        if (g_app->focusPane != pane) {
            g_app->focusRequest = pane;
        }
    };
    // Die Ueberschrift haengt am ORT, nicht am Fokus.
    //
    // Gemeldet: "Script Flow sollte oben links bleiben und nicht immer
    // springen wenn ich auf ein Compare druecke". Vorher trug das
    // FOKUSSIERTE Feld den Titel "Script Flow" - klickte man ins Feld
    // rechts, wanderte die Ueberschrift mit, und links stand ploetzlich
    // "Compare". Die Aufteilung sah nach jedem Klick anders aus.
    //
    // Jetzt heisst Feld 0 immer "Script Flow" und die uebrigen immer
    // "Compare", egal wo der Fokus steht. Welches Feld bearbeitbar ist,
    // zeigt der leuchtende Rahmen - dafuer ist er da.
    if (beginGroupBox(tr(pane == 0 ? Str::GroupScriptFlow : Str::SplitTitle),
                      kennung.c_str(), ImVec2{breite, hoehe}, l.headerH)) {
        // Welcher Reiter? Ein Klappfeld ueber der Liste.
        ImGui::SetNextItemWidth(-FLT_MIN);
        std::string aktuell = "-";
        if (n > 0) {
            const std::size_t at = static_cast<std::size_t>(sp.tab);
            const bool aktiv = (sp.tab == g_app->activeTab);
            const std::string& tp = aktiv ? g_app->path : g_app->tabs[at].path;
            const std::string& tn =
                aktiv ? g_app->shownName : g_app->tabs[at].shownName;
            aktuell = !tp.empty() ? fileName(tp)
                      : !tn.empty() ? tn
                                    : std::string("unnamed.txt");
        }
        const std::string pickName =
            std::string("##splitpick") + std::to_string(pane);
        const bool pickAuf = ImGui::BeginCombo(pickName.c_str(), aktuell.c_str());
        if (GImGui->TestEngineHookItems) {
            selbsttestMerkeText(GImGui, ImGui::GetID(pickName.c_str()),
                                pickName.c_str());
        }
        if (pickAuf) {
            for (int i = 0; i < n; ++i) {
                const std::size_t at = static_cast<std::size_t>(i);
                const bool aktiv = (i == g_app->activeTab);
                const std::string& tp = aktiv ? g_app->path : g_app->tabs[at].path;
                const std::string& tn =
                    aktiv ? g_app->shownName : g_app->tabs[at].shownName;
                std::string nm = !tp.empty() ? fileName(tp)
                                 : !tn.empty() ? tn
                                               : std::string("unnamed.txt");
                nm += "###sp" + std::to_string(pane) + "_" + std::to_string(i);
                // Was schon in einem ANDEREN Feld steht, ist abgeblendet.
                // Dasselbe Skript zweimal nebeneinander ist verwirrend und
                // wird nicht gebraucht - "being able to view the same
                // script side by side is also a little confusing and not
                // needed".
                bool anderswo = (i == g_app->activeTab && pane != g_app->focusPane);
                for (int q = 0; q < g_app->splitCount && !anderswo; ++q) {
                    if (q != pane && q != g_app->focusPane &&
                        g_app->splitPanes[q].tab == i) {
                        anderswo = true;
                    }
                }
                ImGui::BeginDisabled(anderswo);
                if (ImGui::Selectable(nm.c_str(), i == sp.tab)) {
                    sp.tab = i;
                    sp.dirty = true;
                    sp.selected = -1;
                }
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }

        // Den Baum bauen, wenn noetig.
        //
        // Der aktive Reiter wird IMMER neu gebaut: er aendert sich unter
        // der Hand, sobald man links etwas bearbeitet. Die geparkten
        // aendern sich nicht, solange sie geparkt sind.
        const bool zeigtAktiven = (sp.tab == g_app->activeTab);
        if (sp.dirty || zeigtAktiven) {
            const Script& sc = zeigtAktiven
                                   ? g_app->doc.script()
                                   : g_app->tabs[static_cast<std::size_t>(
                                                     sp.tab)]
                                         .doc.script();
            TreeOptions o = g_app->treeOpt;
            o.expanded = &sp.expanded;
            buildTree(sc, g_app->db, o, sp.rows);
            sp.marke = zeilenMarken(sp.tab, sc, sp.rows);
            sp.dirty = false;
            // Die Auswahl des Feldes haengt am WEG, nicht an der
            // Zeilennummer - dieselbe Regel wie in rebuildTree(). Der
            // aktive Reiter wird jede Runde neu gebaut; eine gemerkte
            // NUMMER zeigte nach jeder Einfuegung auf die falsche Zeile
            // und wanderte sichtbar durch die Liste.
            sp.selected = -1;
            if (!sp.path.empty()) {
                for (std::size_t ri = 0; ri < sp.rows.size(); ++ri) {
                    if (sp.rows[ri].path == sp.path) {
                        sp.selected = static_cast<int>(ri);
                        break;
                    }
                }
            }
        }

        if (sp.rows.empty()) {
            ImGui::TextDisabled("%s", tr(Str::MsgNoScript));
        }
        // Gezeichnet wie links: gepunktete Linien, Kaestchen mit + und -,
        // Symbol, Name und Argumente in zwei Spalten.
        //
        // Dieselben Bausteine wie im linken Baum - dottedLine, expanderBox,
        // drawIcon - und dieselben Masse aus treeMetrics(). Waeren es
        // eigene, liefen die beiden Haelften mit der Zeit auseinander.
        const TreeMetrics tm = treeMetrics();
        const float rinne = rinnenBreite();
        ImGui::Indent(rinne);
        const float baseX = ImGui::GetCursorScreenPos().x;
        const float step = tm.indent;
        const ImU32 lineCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        std::vector<bool> more;
        rollpositionHolen(kennung, sp.tab);
        for (std::size_t i = 0; i < sp.rows.size(); ++i) {
            const Row& r = sp.rows[i];
            if (more.size() <= static_cast<std::size_t>(r.depth) + 1U) {
                more.resize(static_cast<std::size_t>(r.depth) + 2U, false);
            }
            more[static_cast<std::size_t>(r.depth)] = !r.lastChild;

            ImGui::PushID(static_cast<int>(i));
            const float rowY = ImGui::GetCursorScreenPos().y;
            const float rowH = ImGui::GetTextLineHeight();
            zeichneRinne(ImGui::GetWindowDrawList(), baseX - rinne, rinne, rowY,
                         rowH + ImGui::GetStyle().ItemSpacing.y,
                         i < sp.marke.size() ? sp.marke[i] : kMarkeKeine,
                         hatLesezeichen(sp.tab, r.kennung));
            if (r.depth != 0) {
                ImGui::Indent(step * static_cast<float>(r.depth));
            }
            // Erst ein Platzhalter fuer das Kaestchen, DANN SameLine.
            //
            // Ohne ihn steht das SameLine am Zeilenanfang - und ImGui setzt
            // die Zeile dann neben die VORIGE. Genau das war zu sehen: der
            // ganze Baum auf einer einzigen Zeile, von der nur der Anfang
            // ins Bild passte.
            //
            // Der linke Baum macht es genauso; ich hatte den Platzhalter
            // beim Nachbauen uebersehen, weil er dort in einem else-Zweig
            // steht.
            ImGui::Dummy(ImVec2{tm.box, rowH});
            ImGui::SameLine(0, tm.iconAt + std::floor(tm.box * 0.5F) - tm.box);
            // Nur das Feld mit dem FOKUS zeigt seine Auswahl blau.
            //
            // Gemeldet: "ich kann Befehle in mehreren Ansichten
            // hervorheben, und dann weiss ich nicht, welches Skript ich
            // gerade bearbeite."
            //
            // Zu Recht: bearbeitet wird immer nur EINES, und drei blaue
            // Zeilen behaupten das Gegenteil. Die Auswahl der anderen
            // Felder bleibt gemerkt - sie kommt zurueck, sobald man wieder
            // hineinklickt - aber sie wird nicht hervorgehoben.
            const bool sel = (static_cast<int>(i) == sp.selected) &&
                             (pane == g_app->focusPane);
            drawIcon(r.icon.c_str(), sel);
            ImGui::SameLine(0, tm.nameAt - tm.iconAt - ImGui::GetFontSize());
            const char* label = r.name.empty() ? r.text.c_str() : r.name.c_str();
            const ImVec2 nameMin = ImGui::GetCursorScreenPos();
            const bool angeklickt = ImGui::Selectable(
                label, sel, ImGuiSelectableFlags_AllowDoubleClick);

            // --- Ziehen: QUELLE ------------------------------------------
            //
            // Das Selectable ist die Quelle - ein Element mit eigener
            // Kennung, genau wie es die ImGui-Doku verlangt. Ein
            // Kindfenster taugt dafuer nicht (Fehlerbericht 7539: dort
            // liefert BeginDragDropSource schlicht false).
            //
            // Uebergeben wird NUR Feld, Reiter und Zeilennummer. ImGui
            // kopiert die Nutzlast sofort in einen eigenen Puffer; ein
            // Zeiger darin waere beim Loslassen laengst ungueltig.
            if (ImGui::BeginDragDropSource()) {
                // DIESELBE Nutzlast wie im Hauptbaum ("bhed_node"), damit
                // beide Seiten einander verstehen. Die Einzelheiten stehen
                // nicht darin, sondern in dragFrom/dragFromTab: ImGui
                // kopiert die Nutzlast, und drei Zahlen doppelt zu fuehren
                // waere eine zweite Wahrheit.
                g_app->dragFrom = r.path;
                g_app->dragFromTab = sp.tab;
                g_app->dragFromPane = pane;
                const std::size_t tiefe = r.path.size();
                ImGui::SetDragDropPayload("bhed_node", &tiefe, sizeof(tiefe));
                ImGui::TextUnformatted(label);
                ImGui::EndDragDropSource();
            }
            // --- Ziehen: ZIEL --------------------------------------------
            //
            // Hier wird eingefuegt, nicht in der Quelle: die Quelle erfaehrt
            // gar nicht, ob die Uebergabe geklappt hat (Fehlerbericht 8225).
            if (ImGui::BeginDragDropTarget()) {
                // Alle drei Arten, die es gibt. Nimmt die Zeile eine davon
                // nicht an, fiele der Zug auf die Flaeche darunter durch
                // (ablageFlaeche) und landete am ENDE statt hinter dieser
                // Zeile. Ein Zug aus einem Nebenfeld kommt als "bhed_split"
                // an, nicht als "bhed_node" - von zwei Quellen am selben
                // Element gilt die zweite.
                const bool zeile = ImGui::AcceptDragDropPayload("bhed_node") != nullptr ||
                                   ImGui::AcceptDragDropPayload("bhed_split") != nullptr;
                if (zeile) {
                    App::DragNode dn;
                    dn.pane = g_app->dragFromPane;
                    dn.tab = g_app->dragFromTab;
                    dn.row = -1;   // der WEG zaehlt, nicht die Zeilennummer
                    knotenZiehen(dn, pane, static_cast<int>(i));
                    g_app->dragging = false;
                    g_app->dragFrom.clear();
                    g_app->dragFromTab = -1;
                }
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("bhed_new")) {
                    if (pl->DataSize == static_cast<int>(sizeof(NewDrag)) &&
                        sp.tab >= 0 && sp.tab < n) {
                        NewDrag neu{};
                        std::memcpy(&neu, pl->Data, sizeof(neu));
                        Document& ziel =
                            (sp.tab == g_app->activeTab)
                                ? g_app->doc
                                : g_app->tabs[static_cast<std::size_t>(sp.tab)].doc;
                        Path hinter = r.path;
                        for (const Node& nd : knotenFuerNeu(neu)) {
                            if (!ziel.insertAfter(hinter, nd)) { break; }
                            ++hinter.back();
                        }
                        sp.dirty = true;
                        diag::detail("Ablegen im Nebenfeld " + std::to_string(pane) +
                                     ": neu aus der Ereignisliste hinter \"" + r.name + "\"");
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (angeklickt) {
                fokusAnfordern();
                // Dieselbe Zeile nochmal: Auswahl aufheben - in JEDEM der
                // vier Felder, nicht nur im lebenden.
                if (sel && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    sp.selected = -1;
                    sp.path.clear();
                } else {
                sp.selected = static_cast<int>(i);
                sp.path = r.path;
                // KEIN eigenes Fenster mehr zum Ansehen.
                //
                // Als die Vergleichsfelder nur zeigen konnten, war ein
                // kleines Fenster die richtige Antwort: der Editor
                // bearbeitet das AKTIVE Dokument, und dort stand ein
                // anderes.
                //
                // Inzwischen holt derselbe Klick den FOKUS in dieses Feld -
                // damit wird sein Skript das aktive, und der volle Editor
                // unten gilt ihm. Das kleine Fenster zeigte daneben nur
                // dieselben Werte noch einmal, unbearbeitbar. Gemeldet als
                // "es kommt nur ein kleines fenster hoch statt das
                // vollstaendige - alle 4 sollten bearbeitbar sein".
                //
                // Die Auswahl wandert beim Fokuswechsel mit (rc165), also
                // steht nach dem Klick genau diese Zeile im Editor.
                }
            }
            // --- Doppelklick: den ganzen Text zeigen ---------------------
            //
            // Die rechte Haelfte ist schmal, und lange Argumente werden
            // abgeschnitten - "ich kann auch keine der Trigger doppel
            // klicken um die genauen bezeichnungen zu sehen". Links oeffnet
            // ein Doppelklick den Bearbeiter; hier waere das falsch, weil
            // sich rechts nichts aendern laesst. Also ein Fenster, das
            // ZEIGT.
            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                g_app->splitShowText = r.name + "  " + r.args;
                ImGui::OpenPopup("##splittext");
            }
            // Und schon beim Ueberfahren als Kurzhinweis - das ist der
            // schnellere Weg, wenn man nur nachsehen will.
            if (ImGui::IsItemHovered() && !r.args.empty() &&
                !ImGui::IsDragDropActive()) {
                ImGui::SetTooltip("%s %s", r.name.c_str(), r.args.c_str());
            }
            // --- Ziehen nach LINKS --------------------------------------
            //
            // Die Nutzlast ist die Zeilennummer in splitRows. Der Knoten
            // selbst wird erst beim Ablegen geholt: waehrend des Ziehens
            // koennte sich der rechte Baum neu bauen, und ein Zeiger darauf
            // waere dann ins Leere gerichtet.
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                // Spalte UND Zeile mitgeben - seit rc158 gibt es bis zu
                // drei Vergleichsspalten, und ohne die Spaltennummer weiss
                // die Gegenseite nicht, aus welcher der Knoten kommt.
                const int nutz[2] = {pane, static_cast<int>(i)};
                ImGui::SetDragDropPayload("bhed_split", nutz, sizeof(nutz));
                ImGui::TextUnformatted(label);
                if (!r.args.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", r.args.c_str());
                }
                ImGui::EndDragDropSource();
            }
            // Rechtsklick: in die Ablage des LINKEN Dokuments legen. Danach
            // fuegt Strg+V dort ein - genau der Weg, um den es geht.
            if (ImGui::BeginPopupContextItem("szeile")) {
                sp.selected = static_cast<int>(i);
                sp.path = r.path;
                if (ImGui::MenuItem(tr(Str::ActCopy))) {
                    const Script& src =
                        zeigtAktiven ? g_app->doc.script()
                                     : g_app->tabs[static_cast<std::size_t>(
                                                       sp.tab)]
                                           .doc.script();
                    if (const Node* nd = nodeAt(src, r.path)) {
                        g_app->doc.setClipboard({*nd});
                        ablageNachWindows();
                        setStatus(tr(Str::MsgCopied), 1, 0);
                    }
                }
                ImGui::EndPopup();
            }
            // Die Argumente in ihrer eigenen Spalte - GEZEICHNET, nicht
            // gesetzt, genau wie links. Mit SetCursorPos geraet die Zeile
            // aus dem Tritt; der Auswahlbalken reicht ohnehin bis zum
            // rechten Rand und deckt sie mit ab.
            //
            // Und dieselbe Regel des Originals: die Klammer beginnt nameW
            // hinter dem Namensanfang, rueckt aber nach rechts, wenn der
            // Name laenger ist - sonst ueberschreiben sie sich.
            if (!r.args.empty()) {
                const float argX = std::max(
                    nameMin.x + tm.nameW,
                    nameMin.x + ImGui::CalcTextSize(label).x +
                        ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2{argX, nameMin.y},
                    ImGui::GetColorU32(ImGuiCol_Text), r.args.c_str());
            }
            // Linien und Kaestchen.
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float top = rowY;
                const float bottom =
                    rowY + rowH + ImGui::GetStyle().ItemSpacing.y;
                const float mid = std::floor(rowY + rowH * 0.5F);
                const float half = std::floor(tm.box * 0.5F);
                for (int k = 0; k < r.depth; ++k) {
                    if (!more[static_cast<std::size_t>(k)]) {
                        continue;
                    }
                    const float x = std::floor(
                        baseX + step * static_cast<float>(k) + half);
                    dottedLine(dl, x, top, x, bottom, lineCol);
                }
                const float x = std::floor(
                    baseX + step * static_cast<float>(r.depth) + half);
                dottedLine(dl, x, top, x, mid, lineCol);
                if (!r.lastChild) {
                    dottedLine(dl, x, mid, x, bottom, lineCol);
                }
                dottedLine(dl, x + (r.childCount != 0 ? half + 1.0F : 1.0F),
                           mid, x + tm.iconAt, mid, lineCol);
                if (r.childCount != 0) {
                    expanderBox(dl, ImVec2{x, mid}, tm.box, r.open, lineCol,
                                ImGui::GetColorU32(ImGuiCol_Text));
                    // Und es laesst sich druecken. Der Bereich ist etwas
                    // groesser als das Kaestchen - neun Bildpunkte trifft
                    // niemand zuverlaessig.
                    const float griff = tm.box;
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                        ImGui::IsWindowHovered()) {
                        const ImVec2 mp = ImGui::GetIO().MousePos;
                        if (std::fabs(mp.x - x) <= griff &&
                            std::fabs(mp.y - mid) <= griff) {
                            sp.expanded.toggle(r.kennung);
                            sp.dirty = true;
                        }
                    }
                }
            }
            if (r.depth != 0) {
                ImGui::Unindent(step * static_cast<float>(r.depth));
            }
            // Das Fenster mit dem vollen Text - je Zeile eines, damit die
            // Kennung eindeutig bleibt.
            if (ImGui::BeginPopup("##splittext")) {
                ImGui::TextUnformatted(g_app->splitShowText.c_str());
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::Unindent(rinne);
        // Auch ein Klick auf freie Flaeche im Feld zaehlt - man will das
        // Feld auswaehlen koennen, ohne eine Zeile zu treffen.
        //
        // Und er hebt die Auswahl DIESES Feldes auf: "wenn ich irgendwo
        // anders drauf druecke sollte das auch nicht mehr gehighlighted
        // sein". Getroffen wird hier nur, was KEINE Zeile ist - ein Klick
        // auf eine Zeile hat das Selectable schon verbraucht.
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            !ImGui::IsAnyItemHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            fokusAnfordern();
            sp.selected = -1;
            sp.path.clear();
        }
        rollpositionMerken(sp.tab);
    }
    endGroupBox();
    ablageFlaeche(pane);
}

void drawTree(const Layout& l, int pane, float breite, float hoehe);
void drawSplitTree(const Layout& l, int pane, float breite, float hoehe);

// Der Skriptbereich als RASTER.
//
// So wie in VS Code, und so wie du es beschrieben hast: bei drei Feldern
// nicht drei nebeneinander, sondern eines links und zwei rechts
// uebereinander. Bei vier ein Zweimalzwei.
//
//   1        2            3              4
//   +---+    +---+---+    +---+---+      +---+---+
//   |   |    |   |   |    |   | 0 |      |   | 0 |
//   | H |    | H | 0 |    | H +---+      | H +---+
//   |   |    |   |   |    |   | 1 |      +---+---+
//   +---+    +---+---+    +---+---+      | 1 | 2 |
//                                        +---+---+
//
// H ist der Hauptbaum - er bleibt immer der erste. Warum, steht in rc150:
// an rows, selection und selected haengt jede Bearbeitung.
//
// Gezeichnet wird mit ausdruecklichen Bildschirmorten statt mit SameLine.
// SameLine setzt neben das ZULETZT Gezeichnete, und in einem Raster ist
// das die falsche Bezugsgroesse - beim Sprung in die zweite Reihe muesste
// man rueckwaerts rechnen. Ein Ursprung plus Versatz ist hier einfacher zu
// lesen und stimmt in jeder Aufteilung.
void drawFlowArea(const Layout& l) {
    const int felder = (g_app->leftMode == 0)
                           ? std::clamp(g_app->splitCount, 1, App::kMaxSplit)
                           : 1;
    // Das Ganze in EINEM Kindfenster.
    //
    // Das ist der Kern der Behebung: SetCursorScreenPos verschiebt zwar den
    // Zeiger, sagt dem Elternfenster aber NICHT, wie viel Platz belegt
    // wurde. ImGui rechnet danach mit dem letzten gezeichneten Ding weiter -
    // und das war bei einem Raster das Feld unten rechts. Ergebnis: die
    // Aktionsspalte verrutschte, die Ueberschriften lagen uebereinander,
    // Zeitleiste und Meldungszeile fielen aus dem Bild.
    //
    // Ein Kindfenster fester Groesse ist genau EIN Ding von genau dieser
    // Groesse. Innen wird frei angeordnet, aussen merkt niemand etwas -
    // alles danach rechnet wieder richtig.
    // 0 heisst "die ganze Zelle". Die Tabelle hat die Breite schon
    // bestimmt; `l.flowW` waere die zweite Fassung derselben Zahl.
    const float w = 0.0F;
    // Die Kopfzeile steht ueber der Tabelle, nicht in der Zelle - hier
    // stand `l.frameH + l.headerH` und zog sie ein zweites Mal ab.
    const float h = l.frameH;
    // Ohne Innenabstand: sonst begaenne der Inhalt um die Polsterung
    // versetzt, und die Ueberschriften saessen nicht mehr auf derselben
    // Hoehe wie die der Nachbarspalten.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    // --- Der aeussere Kasten malt KEINEN Hintergrund ---------------------
    //
    // Hier liegen zwei Kindfenster ineinander: dieses als Traeger, und darin
    // die eigentlichen Felder mit ihren Rahmen. Jedes Kindfenster malt
    // `ImGuiCol_ChildBg`, und die Farbe ist halbdurchsichtig - zweimal
    // uebereinander ergibt einen dunkleren Ton als bei den Nachbarspalten.
    //
    // shank: "warum ist das Script Flow so dunkel hinterlegt?" Weil es
    // doppelt hinterlegt war.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
    ImGui::BeginChild("##flowarea", ImVec2{w, h}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    // Die Farbe gleich zurueck: sie galt nur fuer das Anlegen dieses
    // Kastens, nicht fuer die Felder darin - die sollen ihren Hintergrund
    // behalten.
    ImGui::PopStyleColor();
    // --- Die ECHTE Groesse, nicht die Platzhalter -----------------------
    //
    // `w` und `h` sind seit dem Tabellenumbau Platzhalter fuer `BeginChild`:
    // 0 heisst "die ganze Zelle", negativ heisst "alles Uebrige minus
    // diesem Betrag". Als ZAHLEN sind sie unbrauchbar.
    //
    // Hier unten wurde damit weitergerechnet:
    //
    //     hw = floor(w * splitFracX)   ->  0
    //     hh = floor(h * splitFracY)   ->  negativ
    //
    // Das innere Feld bekam damit eine falsche Hoehe und endete zu frueh -
    // shank: "das Script Flow Fenster in der Mitte ist so als waere es
    // 2 Fenster in einem statt einem".
    //
    // Innerhalb des Kindfensters steht die wirkliche Groesse fest, und
    // `GetContentRegionAvail` nennt sie. Gemessen statt hergeleitet.
    const float wIst = ImGui::GetContentRegionAvail().x;
    const float hIst = ImGui::GetContentRegionAvail().y;
    const ImVec2 ursprung = ImGui::GetCursorScreenPos();
    // Die Trennlinien liegen dort, wo der Anwender sie hingezogen hat.
    // Grenzen, damit kein Feld auf null schrumpft und unerreichbar wird.
    const float hw =
        std::floor(wIst * std::clamp(g_app->splitFracX, 0.15F, 0.85F));
    // Jedes Feld braucht seine eigene Ueberschriftszeile, also wird die
    // Hoehe der Kaesten aus der verfuegbaren abzueglich Ueberschrift
    // gerechnet. Bei einer Reihe ist das die volle, bei zweien die halbe.
    const float hh =
        std::floor(hIst * std::clamp(g_app->splitFracY, 0.15F, 0.85F));

    // Die Aufteilung ist in tests/gridtest.cpp nachgebaut und dort mit
    // sieben Aussagen festgehalten - lueckenlos, ueberschneidungsfrei, und
    // die Anordnung wie oben gezeichnet. Wer sie hier aendert, aendert die
    // Probe mit.
    // Ein fester Zwischenraum zwischen den Feldern.
    //
    // Ohne ihn stossen die Kaesten stumpf aneinander, und die Ueberschrift
    // des rechten sitzt direkt am Rand des linken - auf dem Bild sah es
    // aus, als lieferen sie ineinander. Ein Abstand von der Breite eines
    // Zeichenabstands trennt sie sichtbar, ohne Platz zu verschwenden.
    const float luecke = ImGui::GetStyle().ItemSpacing.x;

    struct Feld {
        float x;
        float y;
        float w;
        float h;
    };
    Feld felderPos[App::kMaxSplit];
    switch (felder) {
        case 2:
            felderPos[0] = {0.0F, 0.0F, hw - luecke, hIst};
            felderPos[1] = {hw, 0.0F, wIst - hw, hIst};
            break;
        case 3:
            felderPos[0] = {0.0F, 0.0F, hw - luecke, hIst};
            felderPos[1] = {hw, 0.0F, wIst - hw, hh - luecke};
            felderPos[2] = {hw, hh, wIst - hw, hIst - hh};
            break;
        case 4:
            felderPos[0] = {0.0F, 0.0F, hw - luecke, hh - luecke};
            felderPos[1] = {hw, 0.0F, wIst - hw, hh - luecke};
            felderPos[2] = {0.0F, hh, hw - luecke, hIst - hh};
            felderPos[3] = {hw, hh, wIst - hw, hIst - hh};
            break;
        default:
            felderPos[0] = {0.0F, 0.0F, wIst, hIst};
            break;
    }

    // Das Feld mit dem FOKUS zeigt das lebende Dokument und ist voll
    // bearbeitbar; die uebrigen zeigen ihren Reiter zum Lesen und
    // Uebernehmen. Wer in ein anderes klickt, gibt ihm den Fokus - danach
    // ist DIESES das bearbeitbare.
    const int fokus = std::clamp(g_app->focusPane, 0, felder - 1);
    for (int i = 0; i < felder; ++i) {
        const Feld& f = felderPos[i];
        ImGui::SetCursorScreenPos(ImVec2{ursprung.x + f.x, ursprung.y + f.y});
        if (i == fokus) {
            drawTree(l, i, f.w, f.h - l.headerH);
        } else if (!g_app->tabs.empty()) {
            drawSplitTree(l, i, f.w, f.h - l.headerH);
        }
    }
    // --- Die Trennlinien zum Ziehen --------------------------------------
    //
    // Ein unsichtbarer Knopf auf der Linie, der den Mauszeiger aendert und
    // beim Ziehen den Anteil verschiebt. Genauso macht es Blender mit den
    // Kanten zwischen Bereichen.
    //
    // NACH den Feldern gezeichnet, damit er obenauf liegt; sonst faengt das
    // Kindfenster darunter den Klick ab.
    const float griff = std::max(4.0F, ImGui::GetStyle().ItemSpacing.x);
    if (felder >= 2) {
        ImGui::SetCursorScreenPos(
            ImVec2{ursprung.x + hw - griff, ursprung.y});
        ImGui::InvisibleButton("##splitx", ImVec2{griff * 2.0F, hIst});
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (ImGui::IsItemActive() && wIst > 1.0F) {
            g_app->splitFracX =
                std::clamp(g_app->splitFracX +
                               ImGui::GetIO().MouseDelta.x / wIst,
                           0.15F, 0.85F);
            for (auto& pn : g_app->splitPanes) {
                pn.dirty = true;
            }
        }
    }
    if (felder >= 3) {
        // Bei drei Feldern trennt die waagerechte Linie nur die RECHTE
        // Haelfte, bei vieren die ganze Breite.
        const float x0 = (felder == 3) ? hw : 0.0F;
        ImGui::SetCursorScreenPos(
            ImVec2{ursprung.x + x0, ursprung.y + hh - griff});
        ImGui::InvisibleButton("##splity", ImVec2{wIst - x0, griff * 2.0F});
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }
        if (ImGui::IsItemActive() && hIst > 1.0F) {
            g_app->splitFracY =
                std::clamp(g_app->splitFracY +
                               ImGui::GetIO().MouseDelta.y / hIst,
                           0.15F, 0.85F);
            for (auto& pn : g_app->splitPanes) {
                pn.dirty = true;
            }
        }
    }

    // Den angeforderten REITERWECHSEL zuerst - er kommt aus dem Klappfeld
    // des fokussierten Feldes und ist ein echter Reiterwechsel, also MIT
    // Rollen zur Auswahl.
    if (g_app->tabRequest >= 0) {
        const int ziel = std::clamp(
            g_app->tabRequest, 0, static_cast<int>(g_app->tabs.size()) - 1);
        g_app->tabRequest = -1;
        switchTab(ziel);
    }
    // Den angeforderten Fokuswechsel JETZT ausfuehren, nachdem alles
    // gezeichnet ist.
    //
    // Das lebende Dokument wechselt dabei den Reiter: das bisherige Feld
    // wird geparkt, der Reiter des neuen hervorgeholt. Zeigt das neue Feld
    // ohnehin denselben Reiter, bleibt alles stehen - dann wandert nur der
    // Fokus.
    if (g_app->focusRequest >= 0) {
        const int neu = std::clamp(g_app->focusRequest, 0, App::kMaxSplit - 1);
        g_app->focusRequest = -1;
        if (neu != g_app->focusPane && neu < felder && !g_app->tabs.empty()) {
            // Der Reiter, den das bisherige Feld zeigte, bleibt ihm
            // erhalten - dafuer merkt es sich ihn.
            App::SplitPane& alt = g_app->splitPanes[g_app->focusPane];
            alt.tab = g_app->activeTab;
            // Seine Auswahl BEKOMMT es nicht nachgeschoben.
            //
            // In rc165 wanderte die lebende Auswahl beim Fokuswechsel in
            // das abgebende Feld. Damit erschien dort ein Balken, den
            // niemand angeklickt hatte - gemeldet als "wird es
            // gehighlighted wenn ich von einem fenster zum anderen springe,
            // das zuckt". Jetzt gilt: eine Zeile ist blau, WEIL man sie
            // angeklickt hat, und aus keinem anderen Grund. Ein Feld ohne
            // Klick bleibt leer.
            //
            // Das Feld behaelt, was zuletzt IN IHM angeklickt wurde - das
            // steht schon in sp.path und wird hier nicht angeruehrt.
            g_app->focusPane = neu;
            const int zielTab =
                std::clamp(g_app->splitPanes[neu].tab, 0,
                           static_cast<int>(g_app->tabs.size()) - 1);
            // Kein Rollen, KEINE Aufteilung: nur der Fokus wandert, der
            // Inhalt bleibt, und die Felder bleiben stehen.
            //
            // Ins Protokoll, weil hier entschieden wird, WELCHES Skript die
            // naechste Aenderung bekommt. Geht spaeter eine Bearbeitung ins
            // falsche Skript, steht hier der Grund.
            {
                char z[240];
                const std::string zn =
                    (static_cast<std::size_t>(zielTab) < g_app->tabs.size() &&
                     !g_app->tabs[static_cast<std::size_t>(zielTab)]
                          .shownName.empty())
                        ? g_app->tabs[static_cast<std::size_t>(zielTab)]
                              .shownName
                        : std::string("unnamed.txt");
                std::snprintf(z, sizeof(z),
                              "Fokus: Feld %d -> Feld %d, jetzt aktiv Reiter "
                              "%d (%s)",
                              alt.tab == g_app->activeTab ? g_app->focusPane
                                                          : -1,
                              neu, zielTab, zn.c_str());
                diag::detail(z);
            }
            switchTab(zielTab, false, false);
            // Die im neuen Feld ANGEKLICKTE Zeile wird jetzt die lebende
            // Auswahl. Vorher blieb die alte Auswahl des Dokuments stehen,
            // und der Klick schien nichts zu tun - erst dieser Schritt
            // macht den Klick zu dem, was er in VS Code ist: Fokus UND
            // Schreibzeiger.
            App::SplitPane& nsp = g_app->splitPanes[neu];
            g_app->selection.clear();
            if (!nsp.path.empty() &&
                nodeAt(g_app->doc.script(), nsp.path) != nullptr) {
                g_app->selectedPath = nsp.path;
                g_app->selection.push_back(nsp.path);
            } else {
                // Im neuen Feld ist nichts gewaehlt - dann ist auch im
                // Dokument nichts gewaehlt. Sonst leuchtete nach dem
                // Wechsel eine Zeile auf, die zu einem ANDEREN Feld
                // gehoerte.
                g_app->selectedPath.clear();
            }
            rebuildTree();
            for (auto& pn : g_app->splitPanes) {
                pn.dirty = true;
            }
        }
    }

    ImGui::EndChild();
}

void drawTree(const Layout& l, int pane, float breite, float hoehe) {
    // Der Rahmen des fokussierten Feldes leuchtet - bei vier gleich
    // aussehenden Kaesten muss man sehen, welcher der bearbeitbare ist.
    const bool mehrere = (g_app->leftMode == 0 && g_app->splitCount > 1);
    if (mehrere) {
        ImGui::PushStyleColor(ImGuiCol_Border,
                              ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    // Die Kennung haengt am FELD, nicht am Zustand.
    //
    // Ein Kindfenster in ImGui ist ein eigenstaendiger Rollbereich, und
    // angesprochen wird es ueber seine Kennung - "use child windows to
    // begin into a self-contained independent scrolling/clipping region".
    // Was daran haengt, haengt an der Kennung: der Rollstand vor allem.
    //
    // Vorher hiess das fokussierte Feld "tree" und ein unfokussiertes
    // "splitN". Beim Fokuswechsel tauschten zwei Felder ihre Kennungen -
    // und damit ihre Rollbereiche. Fuer ImGui waren das andere Fenster,
    // also fingen beide wieder oben an. Das war das Umherspringen.
    //
    // Jetzt heisst jedes Feld immer gleich, egal ob es gerade das lebende
    // Dokument zeigt oder nicht. Der Rollstand bleibt, wo er war.
    const std::string kennung = "pane" + std::to_string(pane);
    const bool offen =
        beginGroupBox(tr(pane == 0 ? Str::GroupScriptFlow : Str::SplitTitle),
                      kennung.c_str(), ImVec2{breite, hoehe}, l.headerH);
    if (mehrere) {
        ImGui::PopStyleColor();
    }
    if (offen) {
        // Bei mehreren Feldern steht auch HIER das Klappfeld mit dem
        // Reiternamen - aus zwei Gruenden. Erstens sieht das fokussierte
        // Feld damit aus wie die anderen; vorher verschob der Fokuswechsel
        // den Inhalt um genau die Hoehe des Klappfelds, und die Zeilen
        // sprangen. Zweitens wechselt man so den Reiter eines Feldes,
        // ohne es erst zum Vergleichsfeld machen zu muessen - wie in VS
        // Code, wo jede Gruppe ihre eigene Reiterleiste hat.
        //
        // Der Wechsel ist ANGEFORDERT, nicht sofort: switchTab mitten im
        // Zeichnen zoege dem Baum darunter den Boden weg.
        if (mehrere) {
            const int nT = static_cast<int>(g_app->tabs.size());
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string aktuell = !g_app->path.empty() ? fileName(g_app->path)
                                  : !g_app->shownName.empty()
                                      ? g_app->shownName
                                      : std::string("unnamed.txt");
            const std::string pickName =
                std::string("##splitpick") + std::to_string(pane);
            const bool pickAuf =
                ImGui::BeginCombo(pickName.c_str(), aktuell.c_str());
            // Fuer den Selbsttest: BeginCombo meldet seine Beschriftung nur
            // beim Aufklappen (siehe comboEdit).
            if (GImGui->TestEngineHookItems) {
                selbsttestMerkeText(GImGui, ImGui::GetID(pickName.c_str()),
                                    pickName.c_str());
            }
            if (pickAuf) {
                for (int i = 0; i < nT; ++i) {
                    const std::size_t at = static_cast<std::size_t>(i);
                    const bool aktiv = (i == g_app->activeTab);
                    const std::string& tp =
                        aktiv ? g_app->path : g_app->tabs[at].path;
                    const std::string& tn =
                        aktiv ? g_app->shownName : g_app->tabs[at].shownName;
                    std::string nm = !tp.empty() ? fileName(tp)
                                     : !tn.empty() ? tn
                                                   : std::string(
                                                         "unnamed.txt");
                    nm += "###sp" + std::to_string(pane) + "_" +
                          std::to_string(i);
                    if (ImGui::Selectable(nm.c_str(), aktiv) && !aktiv) {
                        g_app->tabRequest = i;
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (g_app->rows.empty()) {
            ImGui::TextDisabled("%s", tr(Str::MsgNoScript));
        }
        const TreeMetrics tm = treeMetrics();
        // Der Rand links fuer Lesezeichen und Aenderungsmarken (Notepad++).
        const float rinne = rinnenBreite();
        ImGui::Indent(rinne);
        g_app->rinneX0 = ImGui::GetCursorScreenPos().x - rinne;
        // Linke Kante des Inhalts. Alle Spalten rechnen von hier, nicht vom
        // Fenster - sonst wandern die Linien beim Scrollen quer.
        const float baseX = ImGui::GetCursorScreenPos().x;
        // Welche Ebenen laufen unter dieser Zeile weiter? Ein Kellerstapel
        // ueber die Zeilen; die flache Liste ist dafuer in der richtigen
        // Reihenfolge.
        std::vector<bool> more;
        const ImU32 lineCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        rollpositionHolen(kennung, g_app->activeTab);
        // Der Befehl, dessen Gleiche hervorgehoben werden (View-Menue).
        const Node* vorbild = nullptr;
        g_app->gleicheSichtbar = 0;
        if (g_app->settings.highlightSame && !g_app->selectedPath.empty()) {
            vorbild = nodeAt(g_app->doc.script(), g_app->selectedPath);
        }
        for (std::size_t i = 0; i < g_app->rows.size(); ++i) {
            const Row& r = g_app->rows[i];
            ImGui::PushID(static_cast<int>(i));
            // --- Gleiche Befehle, schwach hinterlegt ---------------------
            //
            // shank: "if I click on a command, it faintly highlights all
            // commands that are the same". Dieselbe Farbe wie die Auswahl,
            // nur blass - so gehoert es erkennbar dazu, ohne wie ausgewaehlt
            // auszusehen.
            if (vorbild != nullptr && r.path != g_app->selectedPath && !isSelected(r.path)) {
                const Node* dieser = nodeAt(g_app->doc.script(), r.path);
                if (dieser != nullptr && gleicherBefehl(*vorbild, *dieser)) {
                    const ImVec2 o = ImGui::GetCursorScreenPos();
                    const float h2 = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
                    ImVec4 farbe = ImGui::GetStyleColorVec4(ImGuiCol_Header);
                    // Blass - "I want it to be a subtle highlight" (vorher 0,35).
                    farbe.w *= 0.2F;
                    // Ab derselben Kante wie die Auswahl (das Selectable am
                    // Namen), NICHT unter Linien und Symbol: "The highlight
                    // for exact commands goes behind the icon".
                    const float abX = baseX + tm.indent * static_cast<float>(r.depth) +
                                      std::floor(tm.box * 0.5F) + tm.nameAt -
                                      ImGui::GetStyle().ItemSpacing.x * 0.5F;
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImVec2{abX, o.y - ImGui::GetStyle().ItemSpacing.y * 0.5F},
                        ImVec2{ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
                               o.y + h2 - ImGui::GetStyle().ItemSpacing.y * 0.5F},
                        ImGui::GetColorU32(farbe));
                    ++g_app->gleicheSichtbar;
                }
            }
            const float step = tm.indent;
            // Oberkante der Zeile. Die Linien brauchen die GANZE Zeilenhoehe,
            // nicht die des Kaestchens - sonst klaffen sie zwischen den
            // Zeilen auseinander.
            const float rowY = ImGui::GetCursorScreenPos().y;
            const float rowH = ImGui::GetFontSize();
            {
                // Rand: Lesezeichen und Aenderungsmarke; ein Klick hinein
                // setzt oder entfernt das Lesezeichen (Notepad++).
                const float schritt = rowH + ImGui::GetStyle().ItemSpacing.y;
                zeichneRinne(ImGui::GetWindowDrawList(), baseX - rinne, rinne, rowY, schritt,
                             i < g_app->zeilenMarke.size() ? g_app->zeilenMarke[i] : kMarkeKeine,
                             hatLesezeichen(g_app->activeTab, r.kennung));
                if (g_app->randMitte.size() != g_app->rows.size()) { g_app->randMitte.assign(g_app->rows.size(), ImVec2{}); }
                if (i < g_app->randMitte.size()) {
                    g_app->randMitte[i] = ImVec2{baseX - rinne * 0.5F, rowY + schritt * 0.5F};
                }
                const ImVec2 mp = ImGui::GetIO().MousePos;
                const bool imRand = ImGui::IsWindowHovered() && mp.x >= baseX - rinne &&
                                    mp.x < baseX && mp.y >= rowY && mp.y < rowY + schritt;
                if (imRand && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    lesezeichenUmschalten({r.path});
                }
                // Der Hinweis erst nach kurzem Verweilen - sofort waere er
                // bei jeder Mausbewegung ueber den Rand im Weg.
                static double imRandSeit = -1.0;
                static std::size_t imRandZeile = static_cast<std::size_t>(-1);
                if (imRand) {
                    if (imRandZeile != i) { imRandZeile = i; imRandSeit = ImGui::GetTime(); }
                    // Auf einer geaenderten Zeile: das Original zeigen
                    // (shank: "when you hover over the yellow or green
                    // rectangle on the side"). Schneller als der blosse
                    // Hinweis - dafuer zeigt man ja hin.
                    const std::uint8_t marke = i < g_app->zeilenMarke.size() ? g_app->zeilenMarke[i] : kMarkeKeine;
                    const double warten = (marke != kMarkeKeine) ? 0.3 : 0.6;
                    if (ImGui::GetTime() - imRandSeit > warten) {
                        ImGui::BeginTooltip();
                        if (marke != kMarkeKeine) {
                            bool neu = false;
                            const std::string original = originalZeile(r.path, &neu);
                            if (!original.empty()) {
                                originalVorschau(zuruecksetzbar({r.path}));
                                ImGui::Separator();
                            } else if (neu) {
                                ImGui::TextDisabled("%s", tr(Str::RevertPreviewNew));
                                ImGui::Separator();
                            }
                        }
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0F);
                        ImGui::TextUnformatted(tr(Str::HintGutter));
                        ImGui::PopTextWrapPos();
                        ImGui::EndTooltip();
                        g_app->randVorschauZeile = static_cast<int>(i);
                    }
                } else if (imRandZeile == i) {
                    imRandZeile = static_cast<std::size_t>(-1);
                }
            }
            more.resize(static_cast<std::size_t>(r.depth) + 1U, false);
            more[static_cast<std::size_t>(r.depth)] = !r.lastChild;
            if (r.depth != 0) {
                ImGui::Indent(step * static_cast<float>(r.depth));
            }
            // Das Kaestchen bekommt IMMER seine Spalte, auch wenn die Zeile
            // keine Kinder hat. Vorher fehlte der Platz bei blattlosen
            // Zeilen, und ihre Symbole standen weiter links als die der
            // Bloecke daneben - genau die Unordnung, die im Original nicht
            // da ist.
            bool rebuilt = false;
            // Volle Zeilenhoehe, nicht nur die des Quadrats: sonst sitzt
            // die Klickflaeche oben und das gezeichnete Kaestchen in der
            // Mitte, und man trifft es nicht.
            if (r.childCount != 0) {
                if (ImGui::InvisibleButton("##auf", ImVec2{tm.box, rowH})) {
                    g_app->expanded.toggle(r.kennung);
                    rebuilt = true;
                }
            } else {
                ImGui::Dummy(ImVec2{tm.box, rowH});
            }
            // iconAt und nameAt sind ab SPALTENMITTE gemessen (so steht es
            // im Bildschirmfoto), das Kaestchen beginnt aber an der linken
            // Kante - also die halbe Kaestchenbreite dazu. Ohne das steht
            // alles vier Punkte zu weit links; eine Probe faengt es.
            ImGui::SameLine(0, tm.iconAt + std::floor(tm.box * 0.5F) - tm.box);
            if (!rebuilt) {
                const bool sel = isSelected(r.path);
                // Nach einem Tastendruck die Zeile in den sichtbaren
                // Bereich holen - sonst wandert die Auswahl unsichtbar aus
                // dem Fenster.
                if (sel && g_app->scrollToSelected &&
                    static_cast<int>(i) == g_app->selected) {
                    ImGui::SetScrollHereY(0.5F);
                    g_app->scrollToSelected = false;
                }
                drawIcon(r.icon.c_str(), sel);
                ImGui::SameLine(0, tm.nameAt - tm.iconAt - ImGui::GetFontSize());
                // Beschriftet wird nur der NAME. Die Argumente kommen
                // gleich danach in ihrer eigenen Spalte auf denselben
                // Balken gezeichnet - das Selectable reicht ohnehin bis zum
                // rechten Rand, also deckt die Hervorhebung beides ab.
                // Ist die Spalte so schmal, dass Text ohnehin nur noch
                // abgeschnitten dastuende? Dann NUR das Symbol.
                //
                // Die Grenze richtet sich an der Schrift aus, nicht an
                // einer festen Zahl - bei 144 dpi ist alles breiter. Der
                // volle Text kommt beim Ueberfahren, es geht also nichts
                // verloren.
                const bool nurSymbole =
                    ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 8.0F;
                std::string schmal;
                if (nurSymbole) {
                    // Ein Selectable BRAUCHT eine Kennung, auch ohne Text -
                    // sonst fallen alle Zeilen auf dieselbe zusammen und
                    // die Auswahl springt. Deshalb die Zeilennummer hinter
                    // "##", also unsichtbar.
                    schmal = "##schmal" + std::to_string(i);
                }
                const char* label =
                    nurSymbole ? schmal.c_str()
                               : (r.name.empty() ? r.text.c_str()
                                                 : r.name.c_str());
                const KeepSelected keep(sel, g_app->selectionOwner != 1);
                // In der Symbolansicht steht der volle Text beim
                // Ueberfahren. Ohne ihn waere die schmale Spalte ein
                // Ratespiel - so ist sie eine Uebersicht, aus der man
                // jederzeit nachsehen kann.
                const bool zeigeHinweis = nurSymbole;
                const bool zeileGeklickt = ImGui::Selectable(
                    label, sel, ImGuiSelectableFlags_AllowDoubleClick);

                // Das Ziehen im Hauptbaum steht weiter unten - es gab es
                // hier schon (zum Umordnen), und ZWEI Quellen am selben
                // Element schliessen einander aus: ImGui nimmt die erste,
                // die zweite bekommt nie eine Chance. Genau daran ist das
                // Ziehen zwischen den Feldern in rc224 gescheitert.
                if (zeileGeklickt) {
                    // Dieselbe Zeile nochmal: Auswahl aufheben.
                    //
                    // "wenn ich ... eines gehighlighted habe also blau ist
                    // und ich es nochmal anklicke soll es nicht mehr
                    // ausgewaehlt sein". Ohne Strg und ohne Umschalt, und
                    // nur, wenn genau DIESE eine Zeile gewaehlt ist -
                    // sonst nimmt man einer Mehrfachauswahl mit einem
                    // Klick alles weg, was man gerade zusammengesucht hat.
                    const bool nurDiese =
                        sel && g_app->selection.size() == 1U &&
                        !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift &&
                        !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                    //
                    // KEIN continue und kein return hier: die Schleife hat
                    // ein PushID und Einrueckungen offen, und ein Sprung
                    // heraus laesst sie stehen. Genau dieses Muster
                    // ("SetCursorPosY + fruehes Verlassen") hat in dieser
                    // Anwendung schon zweimal zum Absturz gefuehrt.
                    if (nurDiese) {
                        clearSelection();
                        g_app->mapDirty = true;
                    } else {
                        selectRow(i, ImGui::GetIO().KeyCtrl,
                                  ImGui::GetIO().KeyShift);
                        g_app->mapDirty = true;  // gewaehlte Kamera zeigen
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            // Doppelklick oeffnet den EDITOR - bei jedem
                            // Befehl, auch bei einer Kamerazeile.
                            //
                            // Vorher sprang er bei Kamerabefehlen in die
                            // Kartenansicht. Gemeldet: "wenn ich in Events
                            // doppelklick auf einen camera trigger mache,
                            // springt er zur Map statt diese zu oeffnen."
                            //
                            // Zu Recht. Ein Doppelklick heisst ueberall
                            // "aufmachen"; dass ausgerechnet ein Befehl
                            // etwas anderes tut, muss man wissen. Zur
                            // Kamera kommt man weiterhin ueber die
                            // Zeitleiste und ueber die Schluessel in der
                            // Ansicht - dort ist es ein Klick, kein
                            // Ratespiel.
                            openEditorForNode(r.path);
                        }
                    }
                }
                const ImVec2 nameMin = ImGui::GetItemRectMin();

                // --- Ziehen zum Umordnen ---------------------------------
                //
                // ImGui bringt das mit; wir muessen nur den Weg als Nutzlast
                // mitgeben und beim Ablegen verschieben.
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                    // Zieht man an einer Zeile, die zur Auswahl gehoert,
                    // wandert die GANZE Auswahl mit. Sonst nur diese eine -
                    // und die wird dann auch zur neuen Auswahl.
                    if (!isSelected(r.path)) {
                        g_app->selection.assign(1, r.path);
                        g_app->selectedPath = r.path;
                    }
                    g_app->dragFrom = r.path;
                    g_app->dragFromTab = g_app->activeTab;
                    g_app->dragFromPane = -1;   // aus dem Hauptbaum
                    g_app->dragging = true;
                    // Das AUFNEHMEN protokollieren, nicht nur das Ablegen.
                    //
                    // Bisher stand nur da, was abgelegt wurde. Ein Zug, der
                    // aufgenommen und dann fallengelassen wurde, hinterliess
                    // nichts - und shanks Protokoll zeigte deshalb nur
                    // Zuege von Tiefe 1 nach Tiefe 1. Ob er ueberhaupt je
                    // ein Kind eines Blocks aufgenommen hat, war daraus
                    // nicht zu erkennen.
                    //
                    // Nur bei WECHSEL, sonst stuende die Zeile in jedem
                    // Bild des Ziehens.
                    // Der Merker verhindert, dass die Zeile in JEDEM Bild
                    // des Ziehens steht. Er wird am Ende jedes Zuges
                    // geloescht - sonst schweigt das nAECHSTE Aufnehmen,
                    // wenn es zufaellig vom selben Weg startet.
                    //
                    // shanks rc533-Protokoll, Stapel 15:
                    //   Ablegen: Ziel "use" ...
                    //   Baum: "move" gezogen, 2 -> 3
                    // Kein `Aufnehmen` davor. Vorher war "use" von Weg 2
                    // aufgenommen worden; nach dem Tausch lag "move" auf
                    // demselben Weg 2, und der Merker schwieg.
                    if (g_app->dbgZiehWeg != r.path) {
                        g_app->dbgZiehWeg = r.path;
                        char zz[200];
                        std::snprintf(zz, sizeof(zz),
                                      "Aufnehmen: \"%s\" von Weg %s, Tiefe %d",
                                      r.name.c_str(),
                                      wegAlsText(r.path).c_str(),
                                      static_cast<int>(r.path.size()));
                        diag::detail(zz);
                    }
                    const std::size_t n = r.path.size();
                    ImGui::SetDragDropPayload("bhed_node", &n, sizeof(n));
                    const std::size_t count = g_app->selection.size();
                    if (count > 1) {
                        ImGui::Text(tr(Str::MsgSelected), static_cast<int>(count));
                    } else {
                        ImGui::TextUnformatted(r.text.c_str());
                    }
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    // --- Neues aus der Ereignisliste ---------------------
                    //
                    // AcceptPeekOnly ist AcceptBeforeDelivery zusammen mit
                    // AcceptNoDrawDefaultRect (imgui.h). Damit sieht man die
                    // Nutzlast SCHON WAEHREND des Ziehens - nur so laesst
                    // sich die Einfuegelinie zeichnen, bevor die Maustaste
                    // losgeht. Das Rechteck von ImGui waere hier falsch: es
                    // umrahmt die ganze Zeile und sagt "hinein", wo wir
                    // "dazwischen" meinen.
                    //
                    // Erst IsDelivery() bedeutet wirklich abgelegt.
                    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(
                            "bhed_new", ImGuiDragDropFlags_AcceptPeekOnly)) {
                        const ImVec2 a = ImGui::GetItemRectMin();
                        const ImVec2 b = ImGui::GetItemRectMax();
                        const float h = std::max(b.y - a.y, 1.0F);
                        const float t = (ImGui::GetIO().MousePos.y - a.y) / h;

                        // Drei Zonen - siehe ablageZone. Eine Makrozeile ist ein
                        // Behaelter wie ein Block (im Original: echte Kinder).
                        const Node* drop = nodeAt(g_app->doc.script(), r.path);
                        const bool makroZeile = drop != nullptr && drop->kind == Node::Kind::Macro;
                        const bool block = drop != nullptr && (drop->hasBlock || makroZeile);
                        const bool mitInhalt = drop != nullptr && (makroZeile ? drop->count > 0 : !drop->children.empty());
                        const Ablage zone = ablageZone(
                            t, block, block && mitInhalt && g_app->expanded.isOpen(r.kennung));
                        const DropWhere where = zone.wo;
                        zeichneAblage(zone, a, b);

                        if (pl->IsDelivery() && pl->DataSize == sizeof(NewDrag)) {
                            NewDrag what{};
                            std::memcpy(&what, pl->Data, sizeof(what));
                            dropNewAt(what, r.path, where);
                            rebuilt = true;
                        }
                    }
                    // Aus der RECHTEN Haelfte herueber.
                    //
                    // Dasselbe Ablegen wie bei einem eigenen Knoten, nur
                    // kommt der Knoten aus einem anderen Dokument. Er wird
                    // KOPIERT, nicht verschoben - das rechte Dokument
                    // bleibt unberuehrt, und das ist richtig so: rechts
                    // wird nicht bearbeitet.
                    if (const ImGuiPayload* nutzlast =
                            ImGui::AcceptDragDropPayload("bhed_split")) {
                        if (const Node* nd = knotenAusNebenfeld(nutzlast)) {
                            const Node kopie = *nd;
                            if (g_app->doc.insertAfter(r.path, kopie)) {
                                rebuilt = true;
                                setStatus(tr(Str::MsgCopied), 1, 0);
                            }
                        }
                    }
                    // --- Umordnen im Baum: dieselben drei Zonen ---------
                    //
                    // Vorher: ImGuis Rahmen um die ganze Zeile (auch bei
                    // einem set, wo es kein Hinein gibt), und wohin es fiel,
                    // entschied eine versteckte Regel - Block auf und leer
                    // oder offen: hinein, sonst immer DAHINTER. Jetzt zeigt
                    // die Linie oder der Rahmen, was geschieht, und genau das
                    // geschieht (Document::moveAllAt).
                    if (const ImGuiPayload* knotenPl = ImGui::AcceptDragDropPayload(
                            "bhed_node", ImGuiDragDropFlags_AcceptPeekOnly)) {
                        const ImVec2 a = ImGui::GetItemRectMin();
                        const ImVec2 b = ImGui::GetItemRectMax();
                        const float h = std::max(b.y - a.y, 1.0F);
                        const float anteil = (ImGui::GetIO().MousePos.y - a.y) / h;
                        const Node* drop = nodeAt(g_app->doc.script(), r.path);
                        const bool makroZeile = drop != nullptr && drop->kind == Node::Kind::Macro;
                        const bool block = drop != nullptr && (drop->hasBlock || makroZeile);
                        const bool mitInhalt = drop != nullptr && (makroZeile ? drop->count > 0 : !drop->children.empty());
                        const Ablage zone = ablageZone(
                            anteil, block, block && mitInhalt && g_app->expanded.isOpen(r.kennung));
                        const bool ausFremdemSkript =
                            g_app->dragFromTab >= 0 &&
                            g_app->dragFromTab != g_app->activeTab;
                        if (g_app->dragging) {
                            zeichneAblage(zone, a, b);
                        }
                        if (knotenPl->IsDelivery() && g_app->dragging) {
                        // Kommt der Knoten aus einem ANDEREN Skript, wird
                        // er kopiert statt verschoben - verschieben wuerde
                        // ihn drueben loeschen, und das ist beim Ziehen
                        // zwischen zwei Dateien nie gemeint.
                        //
                        // KEIN continue hier: in einer ImGui-Schleife ist ein
                        // frueher Ausstieg gefaehrlich - eine offene PushID
                        // bliebe stehen. Also if/else.
                        if (ausFremdemSkript) {
                            App::DragNode dn;
                            dn.pane = g_app->dragFromPane;
                            dn.tab = g_app->dragFromTab;
                            dn.row = -1;
                            knotenZiehen(dn, -1, static_cast<int>(i));
                            g_app->dragging = false;
                            g_app->dbgZiehWeg.clear();
                            g_app->dragFrom.clear();
                            g_app->dragFromTab = -1;
                        } else {
                            const std::vector<Path> m = selectionOrCurrent();
                            const std::vector<Path> ziehen =
                                m.size() > 1 ? m : std::vector<Path>{g_app->dragFrom};
                            const std::vector<Kennung> bewegt = kennungenVon(ziehen);
                            const Document::Stelle stelle =
                                zone.wo == DropWhere::Before ? Document::Stelle::Davor
                                : zone.wo == DropWhere::Into ? Document::Stelle::Hinein
                                                             : Document::Stelle::Dahinter;
                            {
                                char zd[240];
                                std::snprintf(zd, sizeof(zd),
                                              "Ablegen: Ziel \"%s\"%s, %d Zeile(n) -> %s",
                                              drop != nullptr ? drop->name.c_str() : "(kein)",
                                              block ? ", ist Block" : "",
                                              static_cast<int>(ziehen.size()),
                                              stelle == Document::Stelle::Davor ? "DAVOR"
                                              : stelle == Document::Stelle::Hinein ? "HINEIN"
                                                                                  : "DAHINTER");
                                diag::detail(zd);
                            }
                            Path gelandet;
                            const bool ok = g_app->doc.moveAllAt(ziehen, r.path, stelle, &gelandet);
                            if (ok) {
                                if (stelle == Document::Stelle::Hinein) {
                                    Path blockWeg = gelandet;
                                    if (makroZeile) {
                                        // flach: das Makro steht direkt davor
                                        --blockWeg.back();
                                    } else {
                                        blockWeg.pop_back();
                                    }
                                    g_app->expanded.setOpen(kennungFuer(blockWeg), true);
                                }
                                markiereKennungen(bewegt);
                                rebuilt = true;
                            } else {
                                zeigeAbweisung();
                            }
                            g_app->dragging = false;
                            g_app->dbgZiehWeg.clear();
                        }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // --- Zweite Spalte: die Argumente ------------------------
                //
                // Gezeichnet statt gesetzt, damit sie ohne SetCursorPos
                // auskommen - der Auswahlbalken reicht ohnehin bis zum
                // rechten Rand und deckt sie mit ab.
                //
                // Nachgestellt wird die Regel des Originals: die Klammer
                // beginnt immer nameW hinter dem Namensanfang. Ist ein Name
                // laenger als die Spalte, rueckt sie fuer diese eine Zeile
                // nach rechts, statt sich zu ueberschreiben.
                // In der Symbolansicht gar keine Argumente: sie stuenden
                // ueber den Rand hinaus, und beschnitten waeren sie nur ein
                // Streifen Buchstabenreste.
                if (zeigeHinweis && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(r.text.c_str());
                    ImGui::EndTooltip();
                }
                if (!r.args.empty() && !nurSymbole) {
                    const float argX = std::max(
                        nameMin.x + tm.nameW,
                        nameMin.x + ImGui::CalcTextSize(label).x +
                            ImGui::GetStyle().ItemInnerSpacing.x);
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2{argX, nameMin.y},
                        ImGui::GetColorU32(ImGuiCol_Text), r.args.c_str());
                }
            }

            // --- Die gepunkteten Linien ------------------------------------
            //
            // Nach den Elementen gezeichnet, aber unter ihnen sichtbar: sie
            // liegen links vom Text, wo nichts anderes steht.
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                // Von Oberkante bis Oberkante der naechsten Zeile, damit die
                // Senkrechten ohne Luecke ineinander uebergehen.
                const float top = rowY;
                const float bottom = rowY + rowH + ImGui::GetStyle().ItemSpacing.y;
                const float mid = std::floor(rowY + rowH * 0.5F);
                const float half = std::floor(tm.box * 0.5F);
                // Senkrechte je Ebene, die weiterlaeuft.
                for (int k = 0; k < r.depth; ++k) {
                    if (!more[static_cast<std::size_t>(k)]) {
                        continue;
                    }
                    const float x = std::floor(baseX + step * static_cast<float>(k) + half);
                    dottedLine(dl, x, top, x, bottom, lineCol);
                }
                // Die eigene Spalte: von oben bis zur Mitte immer, darunter
                // nur, wenn noch ein Geschwister folgt. Genau so hoert die
                // Linie im Original beim letzten Kind auf halber Hoehe auf.
                const float x = std::floor(baseX + step * static_cast<float>(r.depth) + half);
                dottedLine(dl, x, top, x, mid, lineCol);
                if (!r.lastChild) {
                    dottedLine(dl, x, mid, x, bottom, lineCol);
                }
                // Waagerechter Stummel zum Symbol.
                dottedLine(dl, x + (r.childCount != 0 ? half + 1.0F : 1.0F), mid,
                           x + tm.iconAt, mid, lineCol);
                if (r.childCount != 0) {
                    // Ein MAKRO bekommt eine blassere Marke als ein Block.
                    //
                    // Bis hierher sahen beide gleich aus, und shank hat
                    // dreimal versucht, einen Befehl aus `standOnly`
                    // herauszuziehen. Er hat sich nicht geirrt - die
                    // Anzeige hat ihn in die Irre gefuehrt. Ein Makro ist
                    // kein Block: seine Zeilen stehen DANEBEN, nicht darin.
                    const ImU32 markeCol =
                        (r.what == Row::What::Macro)
                            ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                            : ImGui::GetColorU32(ImGuiCol_Text);
                    expanderBox(dl, ImVec2{x, mid}, tm.box, r.open, lineCol,
                                markeCol);
                }
            }
            if (r.depth != 0) {
                ImGui::Unindent(step * static_cast<float>(r.depth));
            }
            // Rechtsklick auf eine Zeile: dasselbe wie die Knoepfe rechts,
            // nur dort, wo die Hand gerade ist.
            // Auf einer Makrozeile sagen, was Sache ist.
            //
            // Die blassere Marke faellt nur auf, wenn man beide nebeneinander
            // sieht. Der Satz sagt es geradeheraus - genau dann, wenn jemand
            // ueberlegt, diese Zeile irgendwohin zu ziehen.
            if ((r.ausMakro || r.what == Row::What::Macro) &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s", tr(Str::MacroRowHint));
            }
            if (ImGui::BeginPopupContextItem("zeile")) {
                // Der Rechtsklick WAEHLT die Zeile auch - wie der Linksklick.
                //
                // Bisher wurden nur `selected` und `selectedPath` gesetzt.
                // Der Balken haengt aber an `g_app->selection` (siehe
                // isSelected), und die blieb unberuehrt: das Menue ging auf,
                // markiert war weiter die alte Zeile.
                //
                // shank zu rc542: "I think right-clicking on a command
                // should select/highlight it (like left-clicking does)."
                //
                // Steht die Zeile schon in einer Mehrfachauswahl, bleibt
                // diese, wie sie ist - sonst zerschiesse ich genau die
                // Auswahl, auf die sich der Menuepunkt beziehen soll.
                if (!isSelected(r.path)) {
                    selectRow(i, false, false);
                } else {
                    g_app->selected = static_cast<int>(i);
                    g_app->selectedPath = r.path;
                }
                if (ImGui::MenuItem(tr(Str::ActEdit), chordName(keys::Action::EditItem))) {
                    openEditorForNode(r.path);
                }
                // --- Auf das Original zuruecksetzen ----------------------
                //
                // shank: "What if there is an option when I right-click a
                // command I changed to revert it to what it was when I
                // opened the file? Sort of like an undo, but for a specific
                // command." Ein Rueckgaengig-Schritt; der Rest des Skripts
                // bleibt, wie er ist.
                {
                    const std::vector<std::pair<Path, Node>> zurueck = zuruecksetzbar(selectionOrCurrent());
                    // Die Vorschau grau rechts daneben, in EINER Zeile - im
                    // Feld, in dem sonst das Tastenkuerzel steht (shank:
                    // "have grey text next to revert to original").
                    const std::string kurz = originalKurz(zurueck);
                    if (!kurz.empty()) {
                        ++g_app->vorschauGezeichnet;
                    }
                    if (ImGui::MenuItem(tr(Str::ActRevertOriginal), kurz.empty() ? nullptr : kurz.c_str(), false,
                                        !zurueck.empty())) {
                        if (g_app->doc.replaceMany(zurueck)) {
                            rebuilt = true;
                            char z[160];
                            std::snprintf(z, sizeof(z), tr(Str::MsgRevertedN), static_cast<int>(zurueck.size()));
                            addStatus(z);
                        }
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("%s", tr(Str::ActRevertOriginalHint));
                    }
                }
                if (ImGui::MenuItem(tr(Str::ActBookmarkToggle),
                                    chordName(keys::Action::BookmarkToggle))) {
                    lesezeichenUmschalten(selectionOrCurrent());
                }
                // --- Aehnliche suchen ------------------------------------
                //
                // shank: "When I right click a command, have an option to
                // find commands. Basically an auto fill for the find window,
                // rather than having to edit the command, copy its values,
                // and then paste it into the find window."
                //
                // Die Suche kennt EIN "containing" - also je Wert ein
                // Eintrag, dazu "jeder <befehl>" nur nach dem Namen.
                if (const Node* fn = nodeAt(g_app->doc.script(), r.path);
                    fn != nullptr && fn->kind == Node::Kind::Command) {
                    if (ImGui::BeginMenu(tr(Str::ActFindSimilar))) {
                        char jeder[160];
                        std::snprintf(jeder, sizeof(jeder), tr(Str::FindSimilarAny),
                                      fn->name.c_str());
                        if (ImGui::MenuItem(jeder)) {
                            sucheVorbelegen(fn->name, "");
                        }
                        ImGui::Separator();
                        for (std::size_t ai = 0; ai < fn->args.size(); ++ai) {
                            const Arg& a = fn->args[ai];
                            if (a.text.empty()) {
                                continue;
                            }
                            std::string zeile = a.text.size() > 60
                                                    ? a.text.substr(0, 57) + "..."
                                                    : a.text;
                            zeile += "##aehnlich" + std::to_string(ai);
                            if (ImGui::MenuItem(zeile.c_str())) {
                                sucheVorbelegen(fn->name, a.text);
                            }
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem(tr(Str::ActClone), chordName(keys::Action::Clone))) {
                    const std::vector<Path> mc = selectionOrCurrent();
                    const bool ok = mc.size() > 1 ? g_app->doc.cloneAll(mc)
                                                  : g_app->doc.cloneAt(r.path);
                    if (ok) { rebuilt = true; }
                }
                if (ImGui::MenuItem(tr(Str::ActCopy), chordName(keys::Action::Copy))) {
                    kopiereWeg(r.path, false);
                }
                if (ImGui::MenuItem(tr(Str::ActCut), chordName(keys::Action::Cut))) {
                    kopiereWeg(r.path, true);
                    rebuilt = true;
                }
                // Immer anklickbar: der Inhalt kann auch aus der
                // Windows-Zwischenablage kommen.
                if (ImGui::MenuItem(tr(Str::ActPaste), chordName(keys::Action::Paste))) {
                    einfuegen(r.path);
                    rebuilt = true;
                }
                ImGui::Separator();
                {
                    std::vector<Path> m = selectionOrCurrent();
                    // Auch hier den Aufklapp-Zustand nachfuehren.
                    //
                    // Der Knopf-Weg tut das seit rc530, dieses Menue nicht -
                    // dieselbe Geste, zwei Wege, einer davon vergessen. Das
                    // ist genau shanks urspruengliche Meldung ("wenn ich
                    // move up mache, geht das + zu"), nur ueber den
                    // Rechtsklick statt ueber den Knopf.
                    if (ImGui::MenuItem(tr(Str::MoveUp), chordName(keys::Action::MoveUp))) {
                        Path nachher;
                        const bool ok = m.size() > 1
                                            ? g_app->doc.moveAll(m, true)
                                            : g_app->doc.moveUp(r.path, &nachher);
                        if (ok) {
                            if (m.size() > 1) { g_app->selection = m; }
                            rebuilt = true;
                        } else {
                            zeigeAbweisung();
                        }
                    }
                    if (ImGui::MenuItem(tr(Str::MoveDown), chordName(keys::Action::MoveDown))) {
                        std::vector<Path> m2 = selectionOrCurrent();
                        Path nachher2;
                        const bool ok = m2.size() > 1
                                            ? g_app->doc.moveAll(m2, false)
                                            : g_app->doc.moveDown(r.path, &nachher2);
                        if (ok) {
                            if (m2.size() > 1) { g_app->selection = m2; }
                            rebuilt = true;
                        } else {
                            zeigeAbweisung();
                        }
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem(tr(Str::ActRem), chordName(keys::Action::CommentOut))) {
                    if (remUmschalten(r.path)) { rebuilt = true; }
                }
                if (ImGui::MenuItem(tr(Str::ActDelete), chordName(keys::Action::Delete))) {
                    if (g_app->doc.removeAt(r.path)) { rebuilt = true; }
                }
                // Neue Befehle einfuegen, ohne nach links zu greifen.
                ImGui::Separator();
                // Bei einem Block sagen, wohin der neue Befehl geht - sonst
                // probiert man es aus und raet.
                {
                    const Node* nn2 = nodeAt(g_app->doc.script(), r.path);
                    if (nn2 != nullptr && nn2->hasBlock) {
                        const bool into = g_app->expanded.isOpen(r.kennung) ||
                                          nn2->children.empty();
                        ImGui::TextDisabled("%s", into ? tr(Str::InsertInto)
                                                       : tr(Str::InsertAfter));
                    }
                }
                if (ImGui::BeginMenu(tr(Str::GroupEvents))) {
                    for (const Command& c : g_app->db.commands) {
                        if (ImGui::MenuItem((c.name + "  " + signatureText(c)).c_str())) {
                            insertCommand(c);
                            rebuilt = true;
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(tr(Str::MsgMacros))) {
                    for (const Macro& mm : g_app->db.macroBodies) {
                        if (ImGui::MenuItem(mm.name.c_str())) {
                            insertMacro(mm);
                            rebuilt = true;
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
            if (rebuilt) {
                rebuildTree();
                // Die restlichen Zeilen als LEERE Hoehe stehen lassen.
                //
                // shank, 27.09.: "Dragging a command scrolls the view up" -
                // in langen Listen sprang die Ansicht nach jedem Ablegen
                // nach oben. Hier stand nur `break`: die Liste endete in
                // diesem Bild an der abgelegten Zeile, ImGui nahm die
                // kuerzere Inhaltshoehe als neuen Rollbereich und klemmte
                // die Rollposition darauf. Bei kurzen Listen ohne Rollen
                // fiel es nicht auf.
                //
                // Die neue Liste wird erst im naechsten Bild gezeichnet
                // (die Wege von `r` gelten nicht mehr); bis dahin haelt ein
                // Platzhalter die volle Hoehe. Der Zeilenschritt ist
                // derselbe wie bei den Linien unten.
                const float schritt = rowH + ImGui::GetStyle().ItemSpacing.y;
                const float bisUnten =
                    rowY + schritt * static_cast<float>(g_app->rows.size() - i);
                ImGui::SetCursorScreenPos(ImVec2{baseX, bisUnten});
                ImGui::Dummy(ImVec2{0.0F, 0.0F});
                break;
            }
        }

        ImGui::Unindent(rinne);
        // Ein "zur Auswahl rollen" ohne Auswahl verfaellt - sonst wartet es
        // auf den naechsten Klick und laesst die Ansicht dann springen.
        if (g_app->scrollToSelected && g_app->selected < 0) {
            g_app->scrollToSelected = false;
        }

        // --- Die freie Flaeche unter der letzten Zeile ---------------------
        //
        // Ohne die geht bei einem LEEREN Skript gar nichts: es gibt keine
        // Zeile, auf die man ziehen koennte, also kaeme nie ein erster
        // Befehl hinein. Derselbe Fall, der schon beim Doppelklick
        // zugeschlagen hat, nur eine Bedienung weiter.
        //
        // Hierhin gezogen heisst "ans Ende der obersten Ebene" - das ist die
        // Bedeutung, die insertAfter einem leeren Weg gibt.
        const ImVec2 rest = ImGui::GetContentRegionAvail();
        if (rest.y > 1.0F) {
            ImGui::InvisibleButton("##ende", ImVec2{std::max(rest.x, 1.0F), rest.y});
            // Ins Leere geklickt heisst: nichts mehr gewaehlt.
            //
            // So macht es der Explorer, und so erwartet es jeder. Vorher
            // blieb die Markierung ewig stehen - man wurde sie nur los,
            // indem man etwas anderes anklickte.
            //
            // Umschalt und Strg NICHT: wer damit klickt, will erweitern,
            // auch wenn er dabei danebentrifft.
            if (ImGui::IsItemClicked() && !ImGui::GetIO().KeyShift &&
                !ImGui::GetIO().KeyCtrl) {
                g_app->selection.clear();
                g_app->selectedPath.clear();
                g_app->selected = -1;
                g_app->mapDirty = true;
            }
            if (ImGui::BeginDragDropTarget()) {
                // --- Einen BESTEHENDEN Befehl hierher ziehen ------------
                //
                // shank: "das Rausziehen klappt nur, wenn ich es zu einem
                // anderen Befehl ziehe. Wenn ich es unterhalb in das leere
                // Fenster ziehe, springt es nicht raus. Muss es da ein
                // ziehbares Feld geben?"
                //
                // Ja - und die Flaeche gab es schon, sie nahm aber nur
                // `bhed_new` an: neue Befehle aus der Ereignisliste. Fuer
                // `bhed_node`, also einen Befehl AUS DEM BAUM, war keine
                // Annahme da. Der Zug verfiel lautlos.
                //
                // Hier bedeutet Ablegen: ans ENDE der obersten Ebene. Damit
                // gibt es endlich eine Stelle, die "heraus aus allem"
                // heisst - ohne dass man einen passenden Nachbarn suchen
                // muss.
                if (ImGui::AcceptDragDropPayload("bhed_node") != nullptr &&
                    g_app->dragging && !g_app->dragFrom.empty()) {
                    const Path zuvor = g_app->dragFrom;
                    char zf[160];
                    std::snprintf(zf, sizeof(zf),
                                  "Ablegen: freie Flaeche -> ans Ende der "
                                  "obersten Ebene (von %s, Tiefe %d)",
                                  wegAlsText(zuvor).c_str(),
                                  static_cast<int>(zuvor.size()));
                    diag::detail(zf);
                    // Aus einem Block: NEBEN den Block, nicht ans Ende
                    // von allem.
                    //
                    // shank: "er laesst mich es zwar nach unten ziehen,
                    // dafuer setzt er es in dem Plus einfach nur ganz nach
                    // unten statt raus neben der Funktion mit dem Plus."
                    //
                    // Richtig. "Ans Ende der obersten Ebene" ist ein grobes
                    // Werkzeug: wer einen Befehl aus einem `affect`
                    // herauszieht, will ihn direkt dahinter haben, nicht
                    // dreissig Zeilen weiter unten. Er muesste ihn danach
                    // von Hand wieder hochschieben.
                    //
                    // Steckt er in einem Block, ist das Ziel also der Platz
                    // HINTER dem Block - dieselbe Bewegung wie bei
                    // `Move down` am Blockende (rc516) und beim Ablegen auf
                    // dem Block selbst (rc521). Drei Gesten, ein Ergebnis.
                    //
                    // Nur wer schon oben steht, wandert ans Ende: dort gibt
                    // es keinen Block, aus dem er heraus koennte.
                    bool ok2 = false;
                    Path zielW;
                    // `zielW` ist der ANKER, nicht das Ziel: moveTo setzt
                    // den Knoten NEBEN ihn. Wohin er wirklich kommt, sagt
                    // jetzt `gelandet` - siehe edit.h.
                    Path gelandet;
                    if (zuvor.size() > 1) {
                        zielW.assign(zuvor.begin(), zuvor.end() - 1);
                        ok2 = g_app->doc.moveTo(zuvor, zielW, &gelandet);
                    } else {
                        ok2 = g_app->doc.moveToEnd(zuvor, &gelandet);
                    }
                    if (ok2) {
                        g_app->selection.clear();
                        g_app->selectedPath.clear();
                        rebuildTree();
                    } else {
                        zeigeAbweisung();
                    }
                    g_app->dragging = false;
                    g_app->dbgZiehWeg.clear();
                    g_app->dragFrom.clear();
                    g_app->dragFromTab = -1;
                }
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(
                        "bhed_new", ImGuiDragDropFlags_AcceptPeekOnly)) {
                    const ImVec2 a = ImGui::GetItemRectMin();
                    const ImVec2 b = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2{a.x, a.y}, ImVec2{b.x, a.y},
                        ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0F);
                    if (pl->IsDelivery() && pl->DataSize == sizeof(NewDrag)) {
                        NewDrag what{};
                        std::memcpy(&what, pl->Data, sizeof(what));
                        dropNewAt(what, Path{}, DropWhere::After);
                        rebuildTree();
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
    }
    // Ein Zug, der ABGEBROCHEN wurde - aufgenommen, aber nirgends
    // abgelegt -, muss den Merker genauso aufraeumen wie ein abgelegter.
    //
    // shanks rc537-Protokoll:
    //
    //     Aufnehmen: "standOnly" von Weg 5, Tiefe 1
    //     Baum: "print" nach OBEN, 9 -> 8        <- kein Ablegen
    //
    // Er hat losgezogen und dann losgelassen. Bisher wurde `dbgZiehWeg`
    // nur an den drei ABLEGE-Stellen geloescht (rc534). Bleibt er stehen,
    // schweigt das naechste `Aufnehmen`, wenn es zufaellig vom selben Weg
    // startet - genau das Loch, das rc534 schliessen sollte, einen Fall
    // zu kurz.
    if (g_app->dragging && !ImGui::IsDragDropActive()) {
        g_app->dragging = false;
        g_app->dbgZiehWeg.clear();
        g_app->dragFrom.clear();
        g_app->dragFromTab = -1;
    }
    rollpositionMerken(g_app->activeTab);
    endGroupBox();
    ablageFlaeche(-1);
}

// Ein Knopf ueber die volle Spaltenbreite, Hoehe aus der Schrift.
// Eine Kurzhilfe an der Maus, nach kurzem Verweilen.
//
// ImGuiHoveredFlags_DelayNormal wartet, statt sofort aufzuploppen - sonst
// flackert beim Ueberfahren der Knopfspalte staendig ein Kasten.
void hint(Str id, keys::Action key = keys::Action::Unhandled) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                              ImGuiHoveredFlags_AllowWhenDisabled)) {
        return;
    }
    // Das Kuerzel dazu, wenn es eines gibt. Gefragt wird die WIRKLICHE
    // Belegung, nicht eine fest getippte - wer sie im Einstellungsfenster
    // umlegt, sieht es hier sofort.
    const char* k = (key == keys::Action::Unhandled) ? nullptr : chordName(key);
    if (k != nullptr) {
        ImGui::SetTooltip("%s  (%s)", tr(id), k);
    } else {
        ImGui::SetTooltip("%s", tr(id));
    }
}

bool fullButton(Str id, Str hintId = Str::Count,
                keys::Action key = keys::Action::Unhandled) {
    const bool r = ImGui::Button(tr(id), ImVec2{-FLT_MIN, 0.0F});
    if (hintId != Str::Count) {
        hint(hintId, key);
    }
    return r;
}

// Zwei Knoepfe nebeneinander, wie im Original (Copy/Cut, Open/MRU, Prefs/About).
bool halfButton(Str id, bool second, Str hintId = Str::Count,
                keys::Action key = keys::Action::Unhandled) {
    const float w = (ImGui::GetContentRegionAvail().x -
                     ImGui::GetStyle().ItemSpacing.x) * 0.5F;
    const bool r = ImGui::Button(tr(id), ImVec2{second ? -FLT_MIN : w, 0.0F});
    if (hintId != Str::Count) {
        hint(hintId, key);
    }
    if (!second) {
        ImGui::SameLine();
    }
    return r;
}

// --- Der Compile-Knopf: gross, farbig, mit Rueckmeldung --------------------
//
// shank, 27.09.: "Maybe the compile button should be bigger (like it is in
// the original)? It is the most-used button" - im Original ist er 31 dlu
// hoch, die uebrigen 12 (Dialog 102). Und: nach dem Klick kurz "Compiled"
// mit Haken, danach zurueck, weil das Uebersetzen sofort fertig ist.
//
// Der Haken ist gezeichnet (RenderCheckMark), nicht als Zeichen gesetzt:
// die Oberflaechenschrift hat kein U+2713, und ein Fragezeichen-Kaestchen
// waere schlimmer als gar nichts.
//
// Die Beschriftung "Compile!" bleibt als Kennung sichtbar ("###compile"
// haelt die Kennung fest, wenn sie wechselt) - Selbsttest und Tastatur
// finden den Knopf weiterhin unter seinem Namen.
bool compileKnopf() {
    const double jetzt = ImGui::GetTime();
    const bool fertig = jetzt < g_app->kompiliertBis;
    const bool fehler = !fertig && jetzt < g_app->kompiliertFehlerBis;
    const float h = std::floor(ImGui::GetFrameHeight() * 2.2F);

    ImVec4 grund = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    if (fertig) {
        grund = ImVec4{0.18F, 0.55F, 0.30F, 1.0F};
    } else if (fehler) {
        grund = ImVec4{0.70F, 0.20F, 0.20F, 1.0F};
    }
    auto heller = [](ImVec4 c, float f) {
        return ImVec4{c.x + (1.0F - c.x) * f, c.y + (1.0F - c.y) * f,
                      c.z + (1.0F - c.z) * f, c.w};
    };
    auto dunkler = [](ImVec4 c, float f) {
        return ImVec4{c.x * (1.0F - f), c.y * (1.0F - f), c.z * (1.0F - f), c.w};
    };
    ImGui::PushStyleColor(ImGuiCol_Button, grund);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, heller(grund, 0.15F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, dunkler(grund, 0.15F));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0F, 1.0F, 1.0F, 1.0F});
    const std::string beschriftung =
        std::string((fertig || fehler) ? "" : tr(Str::Compile)) + "###compile";
    const bool geklickt = ImGui::Button(beschriftung.c_str(), ImVec2{-FLT_MIN, h});
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    if (fertig || fehler) {
        // Haken bzw. Kreuz vor dem Text, beides zusammen mittig.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const char* text = tr(fertig ? Str::CompileDone : Str::CompileFailedShort);
        const float tw = ImGui::CalcTextSize(text).x;
        const float zs = ImGui::GetFontSize() * 0.85F;
        const float luft = ImGui::GetStyle().ItemInnerSpacing.x * 1.5F;
        const float x0 = std::floor((a.x + b.x - (zs + luft + tw)) * 0.5F);
        const float ym = (a.y + b.y) * 0.5F;
        const ImU32 weiss = IM_COL32(255, 255, 255, 255);
        if (fertig) {
            ImGui::RenderCheckMark(dl, ImVec2{x0, ym - zs * 0.5F}, weiss, zs);
        } else {
            const float k = zs * 0.35F;
            const ImVec2 m{x0 + zs * 0.5F, ym};
            dl->AddLine(ImVec2{m.x - k, m.y - k}, ImVec2{m.x + k, m.y + k}, weiss, 2.0F);
            dl->AddLine(ImVec2{m.x - k, m.y + k}, ImVec2{m.x + k, m.y - k}, weiss, 2.0F);
        }
        dl->AddText(ImVec2{x0 + zs + luft, ym - ImGui::GetFontSize() * 0.5F}, weiss, text);
    }
    ImGui::PopStyleColor(4);
    hint(Str::HintCompile);
    return geklickt;
}

void drawButtonColumns(const Layout& l) {
    ImGui::BeginGroup();
    const float top = ImGui::GetCursorPosY();
    ImGui::TextUnformatted(tr(Str::GroupActions));
    // "N selected" steht in der UEBERSCHRIFT, rechtsbuendig.
    //
    // shank, 27.09.: "manchmal beim verschieben springt die action leiste"
    // - "ich glaube das springen war eher das ... selected angezeigt wird".
    // Bis rc568 stand die Zeile ZWISCHEN Copy/Cut und Paste; sobald mehr als
    // eine Zeile markiert war, rutschten alle Knoepfe darunter eine Zeile
    // nach unten, und beim Abwaehlen wieder hoch. Seit das Ziehen die
    // gezogenen Zeilen markiert, geschah das bei jedem Zug mit mehreren.
    // In der Kopfzeile ist Platz, und nichts verschiebt sich.
    {
        const std::size_t anzahl = selectionOrCurrent().size();
        if (anzahl > 1) {
            char zahl[64];
            std::snprintf(zahl, sizeof(zahl), tr(Str::MsgSelected), static_cast<int>(anzahl));
            const float rechts = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
                                     rechts - ImGui::CalcTextSize(zahl).x));
            ImGui::TextDisabled("%s", zahl);
        }
    }
    // Auf die gemeinsame Kopfzeilenhoehe auffuellen.
    ImGui::SetCursorPosY(top + l.headerH);
        // Ein leeres Element als Anker: ImGui verlangt nach SetCursorPos ein
        // abgeschicktes Element, sonst bricht es ab. Was danach kommt, kann
        // vorzeitig zurueckkehren - der Anker nicht.
        ImGui::Dummy(ImVec2{0.0F, 0.0F});
    // Breite 0: die Tabellenzelle gibt sie vor.
    ImGui::BeginChild("buttons", ImVec2{0.0F, l.frameH},
                      ImGuiChildFlags_Borders);

    const Path& sel = g_app->selectedPath;
    const std::vector<Path> many = selectionOrCurrent();
    const bool has = !many.empty();
    const bool multi = many.size() > 1;

    ImGui::BeginDisabled(!has);
    // Bei Mehrfachauswahl auf alle wirken; sonst wie bisher.
    if (fullButton(Str::ActDelete, Str::HintDelete, keys::Action::Delete)) {
        const bool ok = multi ? g_app->doc.removeAll(many) : g_app->doc.removeAt(sel);
        if (ok) {
            g_app->selection.clear();
            g_app->selectedPath.clear();
            rebuildTree();
        }
    }
    if (fullButton(Str::ActClone, Str::HintClone, keys::Action::Clone)) {
        // `multi`/`many` sind dieselben, die Kopieren und Loeschen schon
        // benutzen - Duplizieren war die einzige Aktion, die sie ignorierte.
        if (multi ? g_app->doc.cloneAll(many) : g_app->doc.cloneAt(sel)) {
            rebuildTree();
        }
    }
    ImGui::EndDisabled();
    // Copy auch ohne Auswahl - dann fragt es, ob das ganze Skript kopiert
    // werden soll (Original: "No line selected, Copy entire script?").
    if (halfButton(Str::ActCopy, false, Str::HintCopy, keys::Action::Copy)) {
        kopieren(false);
    }
    ImGui::BeginDisabled(!has);
    if (halfButton(Str::ActCut, true, Str::HintCut, keys::Action::Cut)) {
        kopieren(true);
    }
    ImGui::EndDisabled();

    // Immer anklickbar: der Inhalt kann auch aus der Windows-Zwischenablage
    // kommen. Ohne Auswahl haengt es ans Ende, wie insertAfter.
    if (fullButton(Str::ActPaste, Str::HintPaste, keys::Action::Paste)) {
        einfuegen(sel);
    }

    ImGui::BeginDisabled(!has);
    if (fullButton(Str::ActRem, Str::HintRem, keys::Action::CommentOut)) {
        // Zweiter Klick auf einen bereits auskommentierten Block nimmt es
        // zurueck - im Original liegt das auf Strg+Ruecktaste.
        (void)remUmschalten(sel);
    }
    ImGui::EndDisabled();
    if (fullButton(Str::ActFind, Str::HintFind, keys::Action::Find)) { fensterZeigen(g_app->findOpen, "###find"); }

    // Verschieben. Das Original hat dafuer keine Knoepfe; man musste
    // ausschneiden und woanders einfuegen.
    // Verschieben wirkt auf die ganze Auswahl. Die Wege werden dabei
    // mitgefuehrt, damit die Auswahl beim Schieben nicht verlorengeht.
    ImGui::BeginDisabled(!has);
    // moveSelection steht jetzt frei (siehe oben).
    if (halfButton(Str::MoveUp, false, Str::HintMoveUp, keys::Action::MoveUp)) { moveSelection(true); }
    if (halfButton(Str::MoveDown, true, Str::HintMoveDown, keys::Action::MoveDown)) { moveSelection(false); }
    ImGui::EndDisabled();

    // Rueckgaengig und Wiederholen als Knoepfe.
    //
    // Das Original hat sie nicht - weder als Knopf noch in der Tastentabelle
    // (ACCELERATOR 135 kennt kein Strg+Z). Ein Editor ohne sichtbares
    // Rueckgaengig ist heute aber keiner mehr, deshalb hier ergaenzt.
    // Rechtsklick auf einen der beiden: die Liste aller Schritte (3ds Max).
    ImGui::BeginDisabled(g_app->doc.undoDepth() == 0);
    if (halfButton(Str::EditUndo, false, Str::HintUndo, keys::Action::Undo)) { doUndo(); }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { g_app->undoListeAnfrage = 1; }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(g_app->doc.redoDepth() == 0);
    if (halfButton(Str::EditRedo, true, Str::HintRedo, keys::Action::Redo)) { doRedo(); }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { g_app->undoListeAnfrage = 2; }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && g_app->doc.redoDepth() != 0) {
        ImGui::SetTooltip(tr(Str::RedoOf), stepName(g_app->doc.redoLabel()));
    }

    ImGui::Separator();
    ImGui::TextUnformatted(tr(Str::GroupFile));
    if (fullButton(Str::FileNew, Str::HintNew)) { doNew(); }
    if (halfButton(Str::FileOpen, false, Str::HintOpen, keys::Action::Open)) { doOpen(); }
    if (halfButton(Str::FileMru, true, Str::HintMru) || g_app->mruOeffnen) {
        g_app->mruOeffnen = false;   // Alt+M
        ImGui::OpenPopup("mru");
    }
    if (ImGui::BeginPopup("mru")) {
        if (g_app->settings.recent.empty()) {
            ImGui::TextDisabled("%s", tr(Str::MsgNoRecent));
        }
        for (const std::string& r : g_app->settings.recent) {
            if (ImGui::MenuItem(r.c_str())) {
                const std::string rp = r;
                withUnsaved([rp] { loadPath(rp); });
            }
        }
        ImGui::EndPopup();
    }
    if (fullButton(Str::FileAppend, Str::HintAppend)) { doAppend(); }
    if (fullButton(Str::FileSave, Str::HintSave, keys::Action::Save) && doSave()) { addStatus(tr(Str::MsgSaved)); }
    if (fullButton(Str::ActSaveAll, Str::HintSaveAll)) { doSaveAll(); }
    if (fullButton(Str::FileSaveAs, Str::HintSaveAs, keys::Action::SaveAs) && doSaveAs()) { addStatus(tr(Str::MsgSaved)); }
    if (fullButton(Str::FileExport, Str::HintExport)) { doExport(); }
    if (fullButton(Str::FileBackup, Str::HintBackup, keys::Action::Backup)) { doBackup(); }
    if (fullButton(Str::FileRestore, Str::HintRestore, keys::Action::Restore)) { doRestore(); }

    ImGui::Separator();
    ImGui::TextUnformatted(tr(Str::GroupTreeview));
    if (ImGui::Checkbox(tr(Str::TreeShowTypes), &g_app->treeOpt.showTypes)) { rebuildTree(); }
    if (ImGui::Checkbox(tr(Str::TreeGFloats), &g_app->treeOpt.gFloats)) { rebuildTree(); }
    if (ImGui::Checkbox(tr(Str::TreeFoldMacros), &g_app->treeOpt.foldMacros)) { rebuildTree(); }
    // Die Knoepfe + und - des Originals: alles auf- bzw. zuklappen.
    //     "+/-: Expands/Collapses event groups in the editor."
    if (halfButton(Str::TreeExpandAll, false)) {
        g_app->expanded.openAll();
        rebuildTree();
    }
    if (halfButton(Str::TreeCollapseAll, true)) {
        g_app->expanded.closeAll();
        rebuildTree();
    }

    ImGui::Separator();
    if (compileKnopf()) { doCompile(); }

    ImGui::Separator();
    ImGui::TextUnformatted(tr(Str::GroupApplication));
    if (halfButton(Str::AppPrefs, false, Str::HintPrefs)) { g_app->prefsOpen = true; }
    if (halfButton(Str::AppAbout, true)) { g_app->aboutOpen = true; }
    // Beenden. Der Knopf setzt nur ein Flag - schliessen muss die
    // Fensterschicht, die das Fenster kennt.
    //
    // Vorher stand hier (void)fullButton(...): das Ergebnis wurde
    // weggeworfen, der Knopf tat also nichts.
    if (fullButton(Str::AppExit) && confirmQuit()) {
        g_app->quitConfirmed = true;
        g_app->wantQuit = true;
    }

    ImGui::EndChild();
    ImGui::EndGroup();
}

// Die Statuszeile - eine Zeile ganz unten, wie in efxed.
//
// Vorher war es ein eigenes Feld mit Rahmen, Ueberschrift und einer Liste
// aller Meldungen. Das kostete standig Hoehe, auch wenn nichts zu melden
// war - und bei einer langen Liste fraess es den halben Bildschirm.
//
// Jetzt: eine Zeile. Die Zaehler sind ANKLICKBAR und oeffnen ein Fenster
// mit den Meldungen. Wer nichts nachlesen will, verliert eine Zeile Hoehe
// statt eines Viertels des Fensters.
//
// Das Muster stammt aus efxed (drawStatusBar in gui/app_panels.cpp) und
// gilt dort seit laengerem als die bessere Loesung - genau deshalb hier
// uebernommen und nicht neu erfunden.
void drawStatus(const Layout& l) {
    (void)l;
    // --- GANZ nach unten ------------------------------------------------
    //
    // Sie wurde bisher gezeichnet, wo der Inhalt darueber gerade endete.
    // Fuellt der nicht die ganze Hoehe, bleibt darunter Luft: auf shanks
    // Bild steht "Ready ... 103 fps" mit einem leeren Streifen darunter.
    //
    // Eine Statuszeile gehoert an den Rand. Der Sprung ist nach unten
    // begrenzt - steht der Inhalt schon tiefer, bleibt sie, wo sie ist,
    // statt sich zu ueberlagern.
    {
        // Knopfhoehe, nicht Textzeile: die Zaehler (Fehler/Warnungen) sind
        // Knoepfe und damit hoeher als eine Textzeile. Mit der Textzeile
        // ragten sie unten um 2-3 Punkte hinaus, und das Hauptfenster wurde
        // rollbar (Selbsttest, Kartenansicht mit zehn Skripten).
        const float zeile = std::max(ImGui::GetTextLineHeightWithSpacing(),
                                     ImGui::GetFrameHeight());
        const float unten = ImGui::GetWindowHeight() -
                            ImGui::GetStyle().WindowPadding.y - zeile;
        if (unten > ImGui::GetCursorPosY()) {
            ImGui::SetCursorPosY(unten);
        }
    }
    int errs = 0;
    int warns = 0;
    for (const Issue& i : g_app->issues) {
        if (i.level == Issue::Level::Error) { ++errs; } else { ++warns; }
    }

    // Ein Zaehler als Knopf ohne Knopfaussehen: anklickbar, aber ruhig.
    const auto zaehler = [&](int n, const char* text, bool fehler) {
        if (n == 0) {
            return;
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0, 0, 0, 0});
        if (fehler) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4{0.90F, 0.35F, 0.35F, 1.0F});
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4{0.90F, 0.75F, 0.35F, 1.0F});
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d %s", n, text);
        if (ImGui::SmallButton(buf)) {
            fensterZeigen(g_app->messagesOpen, "###messages");
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::StatusClickHint));
        }
        ImGui::SameLine();
    };

    zaehler(errs, tr(Str::StatusErrors), true);
    zaehler(warns, tr(Str::StatusWarnings), false);
    if (errs == 0 && warns == 0) {
        ImGui::TextDisabled("%s", tr(Str::StatusReady));
        ImGui::SameLine();
    }

    // Ein leiser Trenner zwischen den Gruppen (shank: "Notes | Undo history
    // ---- Script info | Compiled: time/date").
    const auto trenner = [] {
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    };

    // Was das letzte Undo/Redo getan hat - anklickbar: oeffnet die
    // Undo-Liste. Hat sich das Dokument seitdem geaendert (neue Bearbeitung,
    // anderer Reiter), ist die Meldung veraltet und verschwindet.
    if (!g_app->undoMeldung.empty() && g_app->doc.undoDepth() != g_app->undoMeldungTiefe) {
        g_app->undoMeldung.clear();
    }
    // Sonst - sobald es etwas zurueckzunehmen gibt - IMMER ein Eintrag fuer die
    // Undo-Liste, mit dem Schritt, den das naechste Undo zuruecknaehme (shank:
    // "ich dachte, die Undo History wird immer unten angezeigt, so dass man
    // sie schnell oeffnen kann, am besten bevor man das erste Undo gemacht
    // hat"). Der Text wird nur neu gerechnet, wenn sich der Stapel aendert.
    std::string eintrag = g_app->undoMeldung;
    bool redoListe = g_app->undoMeldungRedo;
    if (eintrag.empty() && g_app->doc.undoDepth() > 0) {
        static std::string letzterText;
        static std::uint64_t letzterStand = static_cast<std::uint64_t>(-1);
        static int letzterReiter = -1;
        const std::size_t n = g_app->doc.undoDepth();
        // Nach dem Stand des Dokuments, nicht nach der Adresse des letzten
        // Schritts: die kann nach Undo und neuer Bearbeitung gleich bleiben,
        // und dann stand unten ein veralteter Text ("Undo history (6): edit").
        if (g_app->doc.stand() != letzterStand || g_app->activeTab != letzterReiter) {
            letzterStand = g_app->doc.stand();
            letzterReiter = g_app->activeTab;
            g_app->doc.vergibKennungen();
            std::string t = aenderungsText(g_app->doc.undoStand(n - 1), g_app->doc.script(), nullptr);
            if (t.empty()) { t = stepName(g_app->doc.undoWas(n - 1)); }
            char z[400];
            std::snprintf(z, sizeof(z), tr(Str::UndoHistoryStatus), static_cast<int>(n), t.c_str());
            letzterText = z;
        }
        eintrag = letzterText;
        redoListe = false;
    }
    if (!eintrag.empty()) {
        trenner();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.55F, 0.75F, 1.0F, 1.0F});
        if (ImGui::SmallButton(eintrag.c_str())) {
            g_app->undoListeAnfrage = redoListe ? 2 : 1;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::UndoListHint));
        }
        g_app->undoStatusText = eintrag;
        ImGui::SameLine();
    } else {
        g_app->undoStatusText.clear();
    }

    // Ein gefundenes Update (gruen, anklickbar) - siehe gui/update_win32.cpp.
    updater::zeichneStatus();

    // Rechts die Bildrate, wie in efxed - davor, wann die .ibi dieses
    // Skripts zuletzt geschrieben wurde ("how about when you compile, at the
    // bottom it gives the date and time of the last compile?"). Aus der Datei
    // gelesen, damit es auch nach einem Neustart stimmt; einmal je Sekunde.
    static std::string letztePfad;
    static double letzteAbfrage = -10.0;
    std::string& letzteZeit = g_app->letzteKompilierung;
    if (g_app->path != letztePfad || ImGui::GetTime() - letzteAbfrage > 1.0) {
        letztePfad = g_app->path;
        letzteAbfrage = ImGui::GetTime();
        letzteZeit.clear();
        if (!g_app->path.empty()) {
            std::string ibi = g_app->path;
            const std::size_t dot = ibi.find_last_of('.');
            const std::size_t slash = ibi.find_last_of("/\\");
            if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
                ibi = ibi.substr(0, dot);
            }
            ibi += ".ibi";
            std::error_code ec;
            const auto wann = std::filesystem::last_write_time(std::filesystem::u8path(ibi), ec);
            if (!ec) {
                const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(wann);
                const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
                std::tm lokal{};
                if (localtime_s(&lokal, &tt) == 0) {
                    char b[40];
                    std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &lokal);
                    char z[120];
                    std::snprintf(z, sizeof(z), tr(Str::StatusLastCompile), b);
                    letzteZeit = z;
                }
            }
        }
    }
    // --- Rechts: Skript-Info | Compiled: ... [| fps nur auf der Karte] -----
    //
    // shank: "Don't really need an FPS counter for scripting, maybe just have
    // it on the map window" - die Bildrate nur im Karten-Reiter.
    std::vector<std::string> teile;
    if (!g_app->statusLines.empty()) {
        teile.push_back(g_app->statusLines.back());
    }
    if (!letzteZeit.empty()) {
        teile.push_back(letzteZeit);
    }
    if (g_app->leftMode == 1) {
        char fps[32];
        std::snprintf(fps, sizeof(fps), "%.0f fps", static_cast<double>(ImGui::GetIO().Framerate));
        teile.emplace_back(fps);
    }
    const ImGuiStyle& st = ImGui::GetStyle();
    const float strich = ImGui::CalcTextSize("|").x + st.ItemSpacing.x * 2.0F;
    float breite = 0.0F;
    for (std::size_t i = 0; i < teile.size(); ++i) {
        breite += ImGui::CalcTextSize(teile[i].c_str()).x + (i > 0 ? strich : 0.0F);
    }
    const float frei = ImGui::GetContentRegionAvail().x;
    if (!teile.empty() && frei > breite + st.ItemSpacing.x) {
        ImGui::SameLine(ImGui::GetCursorPosX() + frei - breite);
        for (std::size_t i = 0; i < teile.size(); ++i) {
            if (i > 0) {
                ImGui::TextDisabled("|");
                ImGui::SameLine();
            }
            ImGui::TextDisabled("%s", teile[i].c_str());
            if (teile[i] == letzteZeit && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr(Str::StatusLastCompileHint));
            }
            if (i + 1 < teile.size()) { ImGui::SameLine(); }
        }
    } else {
        ImGui::NewLine();
    }
}

// Das Fenster mit den Meldungen.
//
// Hier steht, was der Zaehler nur zaehlt. Frueher stand beides zugleich da
// und nahm dauerhaft Platz weg.
void drawMessages() {
    if (!g_app->messagesOpen) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 40.0F,
                                    ImGui::GetFontSize() * 18.0F},
                             ImGuiCond_FirstUseEver);
    // Eine Untergrenze, wie in efxed: ohne sie laesst sich das Fenster auf
    // Fingernagelgroesse ziehen und ist dann nicht mehr zu bedienen.
    ImGui::SetNextWindowSizeConstraints(
        ImVec2{ImGui::GetFontSize() * 20.0F, ImGui::GetFontSize() * 8.0F},
        ImVec2{FLT_MAX, FLT_MAX});
    // "###messages" haengt die Fensterkennung fest, unabhaengig von der
    // Sprache der Ueberschrift.
    const std::string titel = std::string(tr(Str::MessagesTitle)) + "###messages";
    // Lange Meldungen rollen waagerecht, statt abgeschnitten zu werden -
    // der Fenstertest fand bei der Untergrenze 9 Punkte zu wenig.
    if (!ImGui::Begin(titel.c_str(), &g_app->messagesOpen,
                      ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::End();
        return;
    }
    if (g_app->issues.empty() && g_app->statusLines.empty()) {
        ImGui::TextDisabled("%s", tr(Str::MessagesNone));
    } else {
        int errs = 0;
        int warns = 0;
        for (const Issue& i : g_app->issues) {
            if (i.level == Issue::Level::Error) { ++errs; } else { ++warns; }
        }
        ImGui::Text(tr(Str::MsgIssues), errs, warns);
        ImGui::Separator();
    }
    for (const std::string& line : g_app->statusLines) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (!g_app->issues.empty()) {
        ImGui::Separator();
        // Nur lesen, nicht springen.
        //
        // Ein Klick, der zur beanstandeten Zeile fuehrt, waere das
        // Naheliegende - aber Issue traegt nur "where" als Text
        // ("affect/task/set"), keinen Weg in den Baum. Den zu erraten hiesse
        // ihn zu erfinden. Wenn das gewuenscht ist, gehoert ein Path in
        // Issue, und zwar dort, wo die Meldung entsteht.
        // Aufbau wie in efxed: der Schweregrad farbig davor, dann der
        // Ort gedaempft, dann die Meldung im normalen Text. So liest man
        // die Meldung und nicht die Farbe.
        for (const Issue& i : g_app->issues) {
            const bool fehler = i.level == Issue::Level::Error;
            ImGui::TextColored(fehler ? ImVec4{0.90F, 0.35F, 0.35F, 1.0F}
                                      : ImVec4{0.90F, 0.75F, 0.35F, 1.0F},
                               "%s", fehler ? tr(Str::StatusErrors)
                                            : tr(Str::StatusWarnings));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", i.code.c_str());
            ImGui::SameLine();
            if (!i.where.empty()) {
                ImGui::TextDisabled("%s", i.where.c_str());
                ImGui::SameLine();
            }
            // Uebersetzt, wenn wir zu dem Code einen Satz haben.
            //
            // Der Kern schreibt seine Meldungen auf Deutsch - er kennt keine
            // Uebersetzung, und fuer checks und validate auf der
            // Befehlszeile ist das genau richtig. In der Oberflaeche soll
            // aber alles in einer Sprache stehen; gemeldet wurde "the text
            // is in German".
            //
            // Kennen wir den Code nicht, steht der Text des Kerns da. Das
            // ist der Rueckfall und kein Fehler: eine neue Pruefung wirkt
            // sofort, auch bevor jemand einen Satz dafuer geschrieben hat.
            const char* muster = nullptr;
            if (i.code == "V002") { muster = tr(Str::VmsgV002); }
            else if (i.code == "V004") { muster = tr(Str::VmsgV004); }
            else if (i.code == "V006") { muster = tr(Str::VmsgV006); }
            else if (i.code == "V007") { muster = tr(Str::VmsgV007); }
            else if (i.code == "V010") { muster = tr(Str::VmsgV010); }
            // Fuenf von zehn Pruefcodes hatten keine Uebersetzung: V001,
            // V005, V008, V009, V011. Deshalb standen im Meldungsfenster
            // deutsche Saetze, auch wenn die Oberflaeche auf Englisch lief.
            //
            // `VmsgV007` trug ausserdem den Text von V009 - die Nummer im
            // Namen und die Nummer im Code waren auseinander. Sie hiess
            // richtig, verglichen wurde nur die falsche.
            else if (i.code == "V001") { muster = tr(Str::VmsgV001); }
            else if (i.code == "V005") { muster = tr(Str::VmsgV005); }
            else if (i.code == "V008") { muster = tr(Str::VmsgV008); }
            else if (i.code == "V009") { muster = tr(Str::VmsgV009); }
            else if (i.code == "V011") { muster = tr(Str::VmsgV011); }
            if (muster != nullptr) {
                ImGui::TextWrapped(muster, i.arg1.c_str(), i.arg2.c_str());
            } else {
                ImGui::TextWrapped("%s", i.message.c_str());
            }
        }
    }
    ImGui::End();
}

void drawMenu() {
    if (!ImGui::BeginMenuBar()) { return; }
    if (ImGui::BeginMenu(tr(Str::MenuFile))) {
        if (ImGui::MenuItem(tr(Str::FileNew))) { doNew(); }
        if (ImGui::MenuItem(tr(Str::FileOpen), "Ctrl+O")) { doOpen(); }
        if (ImGui::MenuItem(tr(Str::FileSave), "Ctrl+S")) { (void)doSave(); }
        // Strg+A ist im Original "Speichern unter", nicht "alles markieren".
        if (ImGui::MenuItem(tr(Str::FileSaveAs), "Ctrl+A")) { (void)doSaveAs(); }
        if (ImGui::BeginMenu(tr(Str::MsgRecent), !g_app->settings.recent.empty())) {
            for (const std::string& r : g_app->settings.recent) {
                if (ImGui::MenuItem(r.c_str())) {
                const std::string rp = r;
                withUnsaved([rp] { loadPath(rp); });
            }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr(Str::Pk3Browse))) {
            if (g_app->gamePaths.empty()) { rescanGamePaths(); }
            refreshPk3List();
            g_app->pk3Open = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr(Str::LoadMission))) { loadMissionArchive(); }
        if (ImGui::MenuItem(tr(Str::MapLoad))) { doLoadMap(); }
        if (ImGui::MenuItem(tr(Str::OpenEnt))) { openEntFile(); }
        if (ImGui::MenuItem(tr(Str::MapClose), nullptr, false, !g_app->map.empty())) {
            clearMap();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr(Str::OpenGlm))) { openModelFile(); }
        if (ImGui::MenuItem(tr(Str::OpenSkin), nullptr, false, !g_app->model.empty())) {
            openSkinFile();
        }
        if (ImGui::MenuItem(tr(Str::ModelLoadGla), nullptr, false,
                            !g_app->model.empty())) {
            openSkeletonFile();
        }
        ImGui::Separator();
        // Wie viele Spalten? Ein Untermenue mit 1 bis 4 - wie das
        // Farbgebungs- und Sprachmenue daneben.
        // Bei Karte und Modell wirkt es nicht - dann abgeblendet, mit
        // einem Hinweis, statt still nichts zu tun.
        const bool imSkriptmodus = (g_app->leftMode == 0);
        ImGui::BeginDisabled(!imSkriptmodus);
        if (ImGui::BeginMenu(tr(Str::SplitToggle))) {
            // Nicht mehr Felder als SKRIPTE.
            //
            // "Das split view sollte nur gehen wenn es neue tabs gibt und
            // nicht sich selbst duplizieren." Genau so: mit einem offenen
            // Skript gibt es nichts zu vergleichen, und ein zweites Feld
            // koennte nur dasselbe noch einmal zeigen.
            //
            // Die Zahl der Reiter IST die Obergrenze; darueber sind die
            // Eintraege abgeblendet, mit einem Hinweis, warum.
            const int offene = static_cast<int>(g_app->tabs.size());
            for (int n = 1; n <= App::kMaxSplit; ++n) {
                char be[8];
                std::snprintf(be, sizeof(be), "%d", n);
                const bool moeglich = (n <= std::max(1, offene));
                ImGui::BeginDisabled(!moeglich);
                const bool gewaehlt =
                    ImGui::MenuItem(be, nullptr, g_app->splitCount == n);
                ImGui::EndDisabled();
                if (!moeglich && ImGui::IsItemHovered(
                                     ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", tr(Str::SplitNeedsTabs));
                }
                if (gewaehlt) {
                    const int vorher = g_app->splitCount;
                    g_app->splitCount = n;
                    // Jedem NEUEN Feld ein eigenes Skript geben.
                    //
                    // Vorher stand in jedem Feld `tab = 0`. Ein Klick ins
                    // zweite Feld schaltete deshalb auf das ERSTE Skript um
                    // - gemeldet als "if I click the second window it
                    // changes to the first script". Und nebeneinander stand
                    // dann dreimal dasselbe, was niemand braucht.
                    //
                    // Also: das erste noch nicht gezeigte Skript nehmen.
                    // Gibt es keines mehr - weniger Reiter als Felder -,
                    // bleibt es beim bisherigen; dann ist eine Dopplung
                    // unvermeidlich.
                    const int nT = static_cast<int>(g_app->tabs.size());
                    for (int i = vorher; i < n; ++i) {
                        int frei = -1;
                        for (int k = 0; k < nT && frei < 0; ++k) {
                            bool belegt = (k == g_app->activeTab);
                            for (int q = 0; q < i && !belegt; ++q) {
                                if (g_app->splitPanes[q].tab == k) {
                                    belegt = true;
                                }
                            }
                            if (!belegt) {
                                frei = k;
                            }
                        }
                        if (frei >= 0) {
                            g_app->splitPanes[i].tab = frei;
                        }
                    }
                    // Der Fokus muss in einem Feld liegen, das es noch
                    // gibt: von vier auf eins herunter ist sonst Feld 3
                    // fokussiert und keines davon sichtbar.
                    g_app->focusPane = std::clamp(g_app->focusPane, 0, n - 1);
                    for (auto& pn : g_app->splitPanes) {
                        pn.dirty = true;
                    }
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndDisabled();
        if (!imSkriptmodus &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tr(Str::SplitOnlyScript));
        }
        if (imSkriptmodus &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            // Zwei Saetze, zwei Zeilen - aber NICHT ueber ein Format mit
            // Zeilenschaltung: ein mehrzeiliges Format ist mit
            // -Werror=format ein Fehler. Die Bindeprobe hat es sofort
            // gemeldet, noch bevor ich es gebaut habe.
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tr(Str::SplitHint));
            ImGui::EndTooltip();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr(Str::FileBackupFolder))) {
            sicherungsOrdnerOeffnen();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::FileBackupFolderHint));
        }
        // Auto-Updater: von Hand nachsehen, und ob es beim Start geschehen soll.
        if (ImGui::MenuItem(tr(Str::UpdMenu))) {
            updater::pruefen(false);
        }
        if (ImGui::MenuItem(tr(Str::UpdAutoCheck), nullptr, g_app->settings.updateCheck)) {
            g_app->settings.updateCheck = !g_app->settings.updateCheck;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr(Str::AppExit)) && confirmQuit()) {
            g_app->quitConfirmed = true;
            g_app->wantQuit = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr(Str::MenuEdit))) {
        if (ImGui::BeginMenu(tr(Str::MenuBookmarks))) {
            if (ImGui::MenuItem(tr(Str::ActBookmarkToggle), chordName(keys::Action::BookmarkToggle))) {
                lesezeichenUmschalten(selectionOrCurrent());
            }
            if (ImGui::MenuItem(tr(Str::ActBookmarkNext), chordName(keys::Action::BookmarkNext))) {
                lesezeichenSpringen(true);
            }
            if (ImGui::MenuItem(tr(Str::ActBookmarkPrev), chordName(keys::Action::BookmarkPrev))) {
                lesezeichenSpringen(false);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr(Str::ActBookmarkClear))) { lesezeichenAlleWeg(); }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // Die Tastenbelegung war nur ueber den Knopf "Prefs" im
        // Aktionsstreifen zu finden. Wer sie sucht, sucht sie im Menue.
        // Drei Wege ins selbe Fenster, absichtlich.
        //
        // Wer die Spielordner sucht, sucht nicht unter "Einstellungen" -
        // er sucht unter "Spielordner". Dasselbe gilt fuer die
        // Tastenkuerzel. Ein Menueeintrag je Sache kostet nichts und spart
        // das Suchen.
        // Drei Sachen, drei Fenster, drei Eintraege.
        if (ImGui::MenuItem(tr(Str::PathsTitle))) {
            fensterZeigen(g_app->pathsOpen, "###paths");
        }
        if (ImGui::MenuItem(tr(Str::KeysTitle))) {
            fensterZeigen(g_app->keysOpen, "###keys");
        }
        if (ImGui::MenuItem(tr(Str::AppPrefs))) {
            g_app->prefsOpen = true;
        }
        ImGui::Separator();
        // Der Eintrag nennt, was er zuruecknimmt: "Rueckgaengig: Loeschen".
        char label[120];
        if (g_app->doc.undoDepth() != 0) {
            std::snprintf(label, sizeof(label), tr(Str::UndoOf),
                          stepName(g_app->doc.undoLabel()));
        } else {
            std::snprintf(label, sizeof(label), "%s", tr(Str::EditUndo));
        }
        // Die WIRKLICHE Belegung zeigen, nicht eine fest getippte: sonst
        // steht im Menue "Ctrl+Z", waehrend die Taste laengst umgelegt ist.
        if (ImGui::MenuItem(label, chordName(keys::Action::Undo), false,
                            g_app->doc.undoDepth() != 0)) {
            doUndo();
        }
        if (g_app->doc.redoDepth() != 0) {
            std::snprintf(label, sizeof(label), tr(Str::RedoOf),
                          stepName(g_app->doc.redoLabel()));
        } else {
            std::snprintf(label, sizeof(label), "%s", tr(Str::EditRedo));
        }
        if (ImGui::MenuItem(label, chordName(keys::Action::Redo), false,
                            g_app->doc.redoDepth() != 0)) {
            doRedo();
        }
        if (ImGui::MenuItem(tr(Str::UndoListMenu), nullptr, false, g_app->doc.undoDepth() != 0)) {
            g_app->undoListeAnfrage = 1;
        }
        ImGui::EndMenu();
    }
    // --- Ein eigenes Menue fuer die Fehlersuche -------------------------
    //
    // Die Protokollschalter und der GPU-Weg standen unter "View", zwischen
    // Dingen, die das Bild einstellen. shank: "ich glaube es waere ein neues
    // Menue besser statt bei view kannst du debug machen".
    //
    // Er hat recht - "View" beantwortet die Frage "wie sieht es aus",
    // "Debug" die Frage "was ist da los". Zwei verschiedene Fragen.
    if (ImGui::BeginMenu(tr(Str::MenuDebug))) {

        // Auf Zuruf, nicht dauernd: bei 62 Figuren und sechzig Bildern je
        // Sekunde waeren das 3720 Zeilen in der Sekunde.
        if (ImGui::MenuItem(tr(Str::ViewDumpActors), nullptr, false,
                            !g_app->scene.actors.empty())) {
            dumpActorsAtPlayhead();
        }

        // Als SCHALTER, nicht als Knopf - siehe App::logFrameCost.
        if (ImGui::MenuItem(tr(Str::ViewFrameCost), nullptr,
                            g_app->logFrameCost)) {
            g_app->logFrameCost = !g_app->logFrameCost;
            g_app->frameCostTick = 0;
            diag::info(g_app->logFrameCost
                           ? "Zeit je Bild: Protokoll AN - jetzt die Kamera "
                             "bewegen, jedes 20. Bild wird notiert"
                           : "Zeit je Bild: Protokoll aus");
        }

        if (ImGui::MenuItem(tr(Str::ViewLookRotation), nullptr,
                            g_app->lookRotation)) {
            g_app->lookRotation = !g_app->lookRotation;
            g_app->mapDirty = true;
        }

        ImGui::Separator();

        // --- Eine Bildaufnahme aus dem Programm heraus ------------------
        //
        // RenderDoc kann sich NICHT nachtraeglich an ein laufendes Programm
        // haengen - es legt seine Bibliothek beim Start hinein. Ein Knopf,
        // der RenderDoc startet, brauchte also einen Neustart und brachte
        // nichts.
        //
        // Was geht: laeuft behaved schon unter RenderDoc, liegt
        // `renderdoc.dll` im Prozess, und ueber `RENDERDOC_GetAPI` laesst
        // sich eine Aufnahme ausloesen - genau wie F12, nur aus dem Menue.
        //
        // Dafuer braucht es KEINE zusaetzliche Bibliothek: gebraucht wird
        // eine einzige Funktion, und die wird hier von Hand erklaert.
        {
            const bool da = platform::renderDocDa();
            // Nur die AUFNAHME braucht RenderDoc. Das BeginDisabled stand
            // vor allen vier Eintraegen - Layoutzahlen, Protokollauszug und
            // "Aufteilung zuruecksetzen" waren ohne RenderDoc grau
            // (Kartentest 27.09.).
            if (ImGui::MenuItem(tr(Str::DebugLayout), nullptr,
                                g_app->layoutOpen)) {
                g_app->layoutOpen = !g_app->layoutOpen;
            }
            // Auf Knopfdruck alles hineinschreiben, auch ohne Aenderung.
            //
            // Der Auszug haengt sonst an `groessenAnders` - wer nichts
            // verstellt, bekommt also nichts. Genau dann will man ihn aber
            // manchmal: "so sieht es JETZT aus".
            if (ImGui::MenuItem(tr(Str::DebugDump))) {
                g_app->dumpAngefordert = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr(Str::DebugDumpHint));
            }

        // --- Aufteilung auf die Vorgaben zuruecksetzen -------------------
        //
        // Nach dieser Sitzung stehen in shanks Einstellungen Werte, die aus
        // Fehlern stammen: die Rueckkopplung aus rc484 hat `splitMap`
        // kleingeschrumpft, und eine Untergrenze hebt ihn zwar beim
        // Benutzen an, schreibt ihn aber nicht zurueck.
        //
        // Eine `imgui.ini` gibt es NICHT - `main_win32.cpp:251` setzt
        // `io.IniFilename = nullptr`. Mein Hinweis in rc482, sie zu
        // loeschen, ging ins Leere; es gibt nichts zu loeschen. Alles
        // Gespeicherte liegt in `settingsFile`.
        //
        // Zurueckgesetzt werden nur die AUFTEILUNGSWERTE. Pfade,
        // Spracheinstellung und alles andere bleiben - wer die Aufteilung
        // richten will, soll nicht seine Pfade verlieren.
        if (ImGui::MenuItem(tr(Str::DebugReset))) {
            const Settings vorgabe;
            g_app->settings.splitEvents = vorgabe.splitEvents;
            g_app->settings.splitMap = vorgabe.splitMap;
            g_app->settings.splitModel = vorgabe.splitModel;
            g_app->settings.splitButtons = vorgabe.splitButtons;
            g_app->settings.timelineFrac = vorgabe.timelineFrac;
            g_app->settings.splitFracX = vorgabe.splitFracX;
            g_app->settings.splitFracY = vorgabe.splitFracY;
            // Nicht sofort sichern: `destroyApp` ruft `saveSettings`
            // beim Beenden, und jede andere Aufteilungsaenderung wird
            // genauso behandelt. Hier eine zweite Speicherstelle
            // aufzumachen waere das Muster, das in diesem Programm schon
            // oft genug auseinandergelaufen ist.
            g_app->mapDirty = true;
            addStatus(tr(Str::DebugResetDone));
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::DebugResetHint));
        }
            ImGui::Separator();
            ImGui::BeginDisabled(!da);
            if (ImGui::MenuItem(tr(Str::DebugCapture))) {
                platform::renderDocAufnehmen();
                addStatus(tr(Str::DebugCaptureHint));
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", da ? tr(Str::DebugCaptureHint)
                                           : tr(Str::DebugNoRenderDoc));
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr(Str::MenuView))) {
        if (ImGui::MenuItem(tr(Str::ViewChangeHistory), nullptr, g_app->settings.changeHistory)) {
            g_app->settings.changeHistory = !g_app->settings.changeHistory;
            rebuildTree();
            for (auto& pn : g_app->splitPanes) { pn.dirty = true; }
        }
        if (ImGui::MenuItem(tr(Str::ViewHighlightSame), nullptr, g_app->settings.highlightSame)) {
            g_app->settings.highlightSame = !g_app->settings.highlightSame;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("%s", tr(Str::ViewHighlightSameHint));
        }
        ImGui::Separator();
        // --- Modell ------------------------------------------------------
        //
        // Was man einmal einstellt und dann nicht mehr anfasst, gehoert ins
        // Menue. Die Leiste unter der Ansicht bleibt dadurch flach, und dem
        // Bild geht weniger Hoehe verloren.
        ImGui::SeparatorText(tr(Str::ViewMenuModel));
        {
            const bool have = !g_app->model.empty();
            ImGui::BeginDisabled(!have);
            if (ImGui::MenuItem(tr(Str::ModelTextures), nullptr,
                                g_app->showModelTextures)) {
                g_app->showModelTextures = !g_app->showModelTextures;
                g_app->modelDirty = true;
            }
            if (ImGui::MenuItem(tr(Str::ModelCaps), nullptr, g_app->showCaps)) {
                g_app->showCaps = !g_app->showCaps;
                g_app->modelDirty = true;
                g_app->mapDirty = true;   // wirkt auch auf die Figuren der Karte
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr(Str::ModelCapsHint));
            }
            ImGui::EndDisabled();
            // --- Zahlen zum Vergleichen ------------------------------
            //
            // Ein GPU-Weg, der ein falsches Bild zeigt, sagt von sich aus
            // nichts. Diese drei Zahlen sagen, WO es klemmt:
            //
            //   Aufrufe 0        gar nichts gezeichnet
            //   Aufrufe << Liste viele Stapel uebersprungen (fehlende
            //                    Textur oder Shader)
            //   Aufrufe = Liste  gezeichnet - dann liegt es am Aussehen,
            //                    nicht am Weg dorthin
            {
                if (!g_app->gpuFehler.empty()) {
                    ImGui::TextDisabled("%s", g_app->gpuFehler.c_str());
                }
                // --- Die Zahlen stehen in der SEITENLEISTE -------------
                //
                // Sie standen hier, im View-Menue, und das ist beim Hinsehen
                // zu. Genau davor warnt Abschnitt 6 der Uebergabe: "die
                // Zaehlung stand im geschlossenen View-Menue".
                //
                // Es gab die Aufstellung ZWEIMAL - hier und in
                // `app_view3d.cpp` neben den Dreiecken. Die Zeitmarken aus
                // rc431 und der Zustandsschalter aus rc435 landeten in
                // dieser Fassung, also in der unsichtbaren. Auf shanks
                // Bildern stand deshalb weiter die alte einzeilige Zeile.
                //
                // Jetzt gibt es sie nur noch einmal, und zwar dort, wo man
                // hinschaut. Hier bleibt allein die FEHLERMELDUNG - die
                // gehoert an beide Stellen, weil sie erklaert, warum das
                // Menue gerade nichts Sinnvolles zeigt.
            }
            if (have) {
                std::size_t tris = 0;
                std::size_t drawn = 0;
                for (const GlmSurface& sf : g_app->model.surfaces) {
                    if (sf.isTag()) {
                        continue;
                    }
                    ++drawn;
                    tris += sf.indexes.size() / 3;
                }
                ImGui::Separator();
                ImGui::TextDisabled(tr(Str::ModelInfo), static_cast<int>(drawn),
                                    static_cast<int>(tris),
                                    g_app->model.numBones);
                if (g_app->modelTextures.found + g_app->modelTextures.missing != 0) {
                    ImGui::TextDisabled(tr(Str::ModelTexFound),
                                        g_app->modelTextures.found,
                                        g_app->modelTextures.found +
                                            g_app->modelTextures.missing);
                }
                ImGui::TextDisabled(tr(Str::ModelSkeleton),
                                    g_app->model.animFile.c_str());
                if (!g_app->anim.empty()) {
                    ImGui::TextDisabled(tr(Str::ModelAnimCount),
                                        static_cast<int>(g_app->animList.size()),
                                        g_app->anim.numFrames);
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::ViewLookRotationHint));
        }

        if (ImGui::BeginMenu(tr(Str::ViewTheme))) {
            for (const theme::Theme& t : theme::themes()) {
                const bool sel = (g_app->activeTheme != nullptr &&
                                  g_app->activeTheme->id == t.id);
                if (ImGui::MenuItem(t.name(), nullptr, sel)) {
                    g_app->activeTheme = &t;
                    g_app->themePending = true;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr(Str::ViewLanguage))) {
            for (const i18n::LanguageInfo& l : i18n::languages()) {
                const bool sel = (i18n::currentLanguage() == l.language);
                if (ImGui::MenuItem(l.nativeName, nullptr, sel)) {
                    i18n::setLanguage(l.language);
                }
            }
            ImGui::EndMenu();
        }
        // Feste Stufen statt eines Reglers.
        //
        // Ein Schieberegler IM MENUE ist ein schlechtes Bedienelement: das
        // Menue faengt Mausbewegungen ab, und beim Loslassen schliesst es.
        // Man zieht also blind und trifft selten. Windows, Firefox und
        // Visual Studio machen es alle mit festen Stufen - dieselben, die
        // Windows in seinen Anzeigeeinstellungen anbietet.
        //
        // Feiner einstellen geht weiter mit Strg + Mausrad; dort ist ein
        // stufenloser Wert am richtigen Platz, weil man dabei sieht, was
        // passiert.
        if (ImGui::BeginMenu(tr(Str::ViewDpiScale))) {
            for (const int step : {75, 100, 125, 150, 175, 200, 250, 300}) {
                const float want = static_cast<float>(step) / 100.0F;
                char label[16];
                std::snprintf(label, sizeof(label), "%d %%", step);
                // Die aktuelle Stufe anhaken - auch wenn sie ueber das
                // Mausrad dazwischen liegt, ist die naechstgelegene gemeint.
                const bool on = std::fabs(g_app->uiScale - want) < 0.005F;
                if (ImGui::MenuItem(label, nullptr, on)) {
                    g_app->uiScale = want;
                }
            }
            ImGui::Separator();
            ImGui::TextDisabled(tr(Str::ViewScaleNow),
                                static_cast<int>(g_app->uiScale * 100.0F + 0.5F));
            ImGui::EndMenu();
        }
        ImGui::TextDisabled("%s", tr(Str::ViewScaleHint));
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

// Die Auswahl im lebenden Dokument aufheben.
//
// Gebraucht an drei Stellen: nochmal auf dieselbe Zeile klicken, in die
// Leere unter dem Baum klicken, und der Fokuswechsel in ein Feld ohne
// Auswahl. Der Kamerabalken haengt daran mit, deshalb auch selectedKey.
void clearSelection() {
    g_app->selection.clear();
    g_app->selectedPath.clear();
    g_app->selected = -1;
}

}  // namespace

// Der Zugang von aussen - siehe app_internal.h.
//
// MUSS ausserhalb des namenlosen Namensraums stehen. Darin waere die
// Funktion nur in dieser Datei sichtbar, und app_view3d.cpp bekaeme sie
// beim Binden nicht - genau das hat lint_link.py gerade gemeldet, bevor
// der Binder es tat. Derselbe Fallstrick wie bei loadMapFromArchive.
//
// Setzt auch den Arbeitsbereich: wer von aussen einen Reiter waehlt, meint
// ihn als Ganzes und nicht nur das bearbeitete Skript.
void activateTab(int index) {
    switchTab(index);
    g_app->homeTab = index;
}

// Fuer den Selbsttest (gui/selbsttest.cpp): derselbe Weg wie ein Klick in
// die Ereignisliste bzw. wie "Open" nach dem Dateidialog.
void fuegeBefehlEin(const Command& c) { insertCommand(c); }
void ladeSkriptDatei(const std::string& pfad) { loadPath(pfad); }
// Ein neuer, leerer Reiter wie "New" - ohne die Rueckfrage und ohne das
// Wiederverwenden eines leeren Reiters, die doNew() davorschaltet.
void neuerReiter() { neuesSkript(); }


// Eine Zeile ueber ihren WEG auswaehlen und sichtbar machen.
//
// AUSSERHALB des anonymen Namensraums, weil app_view3d.cpp sie ruft.
// Beim ersten Anlauf stand sie darin - dann ist sie TU-lokal, und das
// Binden bricht ab:
//
//     LNK2019: Verweis auf nicht aufgeloestes externes Symbol
//              "void __cdecl bhed::gui::selectByPath(...)"
//
// Dasselbe Muster wie in rc174, nur andersherum: dort fehlte die
// Definition im anonymen Namensraum, hier steckt sie faelschlich darin.
//
// Dasselbe Muster stand dreimal ausgeschrieben da: beim Suchen, beim
// Anklicken eines Kameraschluessels, und ab rc178 beim Anklicken eines
// Blocks in der Zeitleiste. Drei Abschriften laufen auseinander - eine
// davon vergisst scrollToSelected, und dann springt die Ansicht nicht mit.
void selectByPath(const Path& p) {
    if (p.empty()) {
        return;
    }
    // Das Ziel SICHTBAR machen: alle Bloecke darueber aufklappen.
    //
    // Gefunden vom Selbsttest (rc568): die Suche fand sechs `wait`, aber
    // nach jedem "Find next" war die Auswahl leer. Fuenf davon stehen in
    // zugeklappten Bloecken, und rebuildTree verwirft eine Auswahl, deren
    // Zeile nicht im Baum steht ("Knoten ist weg"). Das Original klappt
    // den Baum bis zum Treffer auf; jetzt auch behaved - fuer jeden, der
    // hierher springt.
    for (std::size_t k = 1; k < p.size(); ++k) {
        g_app->expanded.setOpen(kennungFuer(Path(p.begin(), p.begin() + static_cast<std::ptrdiff_t>(k))), true);
    }
    // Ebenso ein gefaltetes Makro, zu dem die Zeile gehoert: seine Befehle
    // stehen in der Datei flach dahinter (tree.cpp, "die naechsten count
    // Befehle gehoeren zum Makro").
    {
        const std::vector<Node>& ns = g_app->doc.script().nodes;
        const std::size_t oben = p.front();
        for (std::size_t j = oben; j-- > 0;) {
            if (ns[j].kind != Node::Kind::Macro) { continue; }
            int befehle = 0;
            for (std::size_t q = j + 1; q <= oben && q < ns.size(); ++q) {
                if (ns[q].kind == Node::Kind::Command) { ++befehle; }
            }
            if (befehle <= ns[j].count) {
                g_app->expanded.setOpen(kennungFuer(Path{j}), true);
            }
            break;
        }
    }
    g_app->selectedPath = p;
    g_app->selection.clear();
    g_app->selection.push_back(p);
    rebuildTree();
    g_app->scrollToSelected = true;
    g_app->mapDirty = true;
}


// Ein Auswahlfeld, in das man auch TIPPEN kann.
//
// Gebaut auf ImGuis eigenem BeginCombo, nicht auf einem selbstgebauten
// Popup. Der erste Anlauf tat Letzteres und hatte drei Fehler, die alle
// dieselbe Wurzel hatten - er kaempfte gegen die Bibliothek:
//
//   * Der zweite Klick auf den Pfeil oeffnete nichts. ImGui schliesst ein
//     Popup beim Klick daneben - und der Pfeil IST daneben. Erst schloss es,
//     dann rief mein Code OpenPopup, und beides hob sich auf.
//   * Die Liste erschien HINTER dem Editorfenster. Popups innerhalb eines
//     modalen Fensters gehoeren auf dessen Stapel; ein von Hand geoeffnetes
//     landet daneben.
//   * Sie liess sich nicht rollen, weil OpenPopup in jedem Bild gerufen
//     wurde. Die Anleitung sagt es ausdruecklich: "don't call every frame!"
//
// BeginCombo macht all das selbst: Auf- und Zuklappen, Stapelordnung,
// Rollbalken, Tastaturbedienung. Das Tippen kommt als Textfeld INNERHALB
// der Liste dazu - so wie es Ravens BehavEd auch nicht konnte, aber wir
// brauchen es fuer Movie Duels' eigene Animationen.
//
// Bedienung:
//   aufklappen   ganze Liste, Filter leer
//   tippen       filtert
//   Eintrag      uebernimmt ihn
//   Eingabe      uebernimmt den GETIPPTEN Text, auch wenn er nicht in der
//                Liste steht
ComboEditResult comboEdit(const char* id, std::string& value,
                          const std::vector<TypeEntry>* entries, float width) {
    ComboEditResult out;
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width);

    // Der Filtertext lebt so lange, wie die Liste offen ist.
    static std::map<ImGuiID, std::string> filters;
    // Merker fuer "im naechsten Bild zum aktuellen Eintrag rollen" - siehe
    // die Begruendung weiter unten bei SetScrollY.
    static std::map<ImGuiID, int> rollNoch;
    const ImGuiID key = ImGui::GetID("##combo");

    // "Alpha-sort edit pulldowns" (Dialog 131): die Eintraege alphabetisch.
    // Der Schalter wurde bisher nur gespeichert und nirgends ausgewertet.
    //
    // Sortiert wird EINMAL je Liste (die Typmengen aendern sich nicht,
    // solange die .bhc geladen ist) - nicht in jedem Bild. Beide Durchlaeufe
    // unten, Hoehe und Zeilen, benutzen dieselbe Reihenfolge. Abschnitts-
    // ueberschriften entfallen dann: sortiert stuenden sie zwischen fremden
    // Eintraegen.
    const bool alpha = g_app->settings.alphaSortPulldowns;
    std::vector<std::size_t> reihenfolgeNatur;
    const std::vector<std::size_t>* reihenfolge = &reihenfolgeNatur;
    if (entries != nullptr) {
        if (alpha) {
            static std::map<const void*, std::vector<std::size_t>> sortiert;
            std::vector<std::size_t>& s = sortiert[entries];
            if (s.size() != entries->size()) {
                s.resize(entries->size());
                for (std::size_t q = 0; q < s.size(); ++q) { s[q] = q; }
                auto klein = [](const std::string& x) {
                    std::string y = x;
                    for (char& ch : y) {
                        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    }
                    return y;
                };
                std::stable_sort(s.begin(), s.end(), [&](std::size_t l, std::size_t r) {
                    return klein((*entries)[l].name) < klein((*entries)[r].name);
                });
            }
            reihenfolge = &s;
        } else {
            reihenfolgeNatur.resize(entries->size());
            for (std::size_t q = 0; q < reihenfolgeNatur.size(); ++q) {
                reihenfolgeNatur[q] = q;
            }
        }
    }

    // Die Liste soll so hoch sein wie ihr Inhalt, nicht wie die Vorgabe.
    //
    // ImGuiComboFlags_HeightLarge setzt eine feste Hoehe von zwanzig
    // Zeilen. Bleiben nach dem Filtern nur sechzehn uebrig, klafft darunter
    // leerer Raum - genau das war zu sehen.
    //
    // Nachgesehen in imgui_widgets.cpp, BeginComboPopup():
    //
    //     if (g.NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasSizeConstraint)
    //         ... (nur die Mindestbreite wird angehoben)
    //     else
    //         SetNextWindowSizeConstraints(constraint_min, constraint_max);
    //
    // Eine eigene Vorgabe wird also uebernommen. Gezaehlt wird mit dem
    // Filter des VORIGEN Bildes - der liegt schon vor, und ein Bild
    // Verzoegerung sieht niemand.
    // Die Hoehe, die die LISTE bekommt - ohne Suchfeld und Trenner.
    //
    // Sie muss ausdruecklich gerechnet werden. Das Klappfenster waechst mit
    // seinem Inhalt; ein Kindfenster mit Hoehe 0 nimmt "die restliche
    // Elterngroesse" - und beides zusammen ist eine Rueckkopplung. ImGui
    // fuehrt sie selbst als bekanntes Problem ("auto-fit feedback loop"),
    // und zu einem selbstwachsenden Fenster mit selbstwachsendem Kind
    // heisst der erste empfohlene Ausweg: die Inhaltsgroesse von Hand
    // rechnen.
    //
    // Genau die Zahlen liegen hier schon vor - `wanted`, `cap` und `extra`
    // bestimmen ohnehin die Fenstergrenze.
    float listenH = 0.0F;
    {
        const auto had = filters.find(key);
        const std::string prev = (had == filters.end()) ? std::string{} : had->second;
        // GENAU so zaehlen, wie die Liste ihre Zeilen baut: Eintraege PLUS
        // Ueberschriften. Die Ueberschriften vergessen zu haben war der
        // Grund, warum unten trotzdem noch Luft blieb.
        int visible = 0;
        if (entries != nullptr) {
            std::string needle = prev;
            for (char& c : needle) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            const std::string* lastSection = nullptr;
            for (const std::size_t ei : *reihenfolge) {
                const TypeEntry& e = (*entries)[ei];
                if (!needle.empty()) {
                    std::string low = e.name;
                    for (char& c : low) {
                        c = static_cast<char>(
                            std::tolower(static_cast<unsigned char>(c)));
                    }
                    if (low.find(needle) == std::string::npos) {
                        continue;
                    }
                }
                if (!alpha && !e.section.empty() &&
                    (lastSection == nullptr || *lastSection != e.section)) {
                    lastSection = &e.section;
                    ++visible;   // die Ueberschrift ist auch eine Zeile
                }
                ++visible;
            }
            if (!prev.empty()) {
                ++visible;   // die Zeile "... uebernehmen"
            }
        }
        const float line = ImGui::GetTextLineHeightWithSpacing();
        const float extra = ImGui::GetFrameHeightWithSpacing() +     // Filterfeld
                            ImGui::GetStyle().WindowPadding.y * 2.0F +
                            ImGui::GetStyle().ItemSpacing.y * 2.0F;  // Trenner
        const float wanted = static_cast<float>(std::max(visible, 1)) * line + extra;
        float cap = 20.0F * line + extra;   // wie HeightLarge

        // Und jetzt der Platz, der WIRKLICH da ist.
        //
        // Zwanzig Zeilen sind eine Obergrenze, kein Versprechen: steht das
        // Feld weit unten im Fenster, laeuft die Liste trotzdem hinaus und
        // die letzten Eintraege sind nicht erreichbar. Genau das war bei
        // SET zu sehen - die Liste reichte ueber den Fensterrand hinaus.
        //
        // ImGui klappt ein Kombifeld von selbst nach oben, wenn unten kein
        // Platz ist. Es kann das aber nur, wenn die verlangte Hoehe
        // ueberhaupt irgendwo hinpasst. Also nehmen wir die groessere der
        // beiden Seiten als Grenze - dann passt sie immer auf eine davon.
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const float rand = ImGui::GetStyle().WindowPadding.y * 2.0F;
            const ImVec2 hier = ImGui::GetCursorScreenPos();
            const float unten = (vp->Pos.y + vp->Size.y) -
                                (hier.y + ImGui::GetFrameHeight()) - rand;
            const float oben = hier.y - vp->Pos.y - rand;
            const float platz = std::max(unten, oben);
            if (platz > line * 3.0F) {
                cap = std::min(cap, platz);
            }
        }
        // --- Auch die BREITE festlegen -------------------------------
        //
        // shank: "beim Runterscrollen werden die ploetzlich breiter. Ich
        // denke, man muss es generell so breit machen wie das breiteste
        // Wort darin."
        //
        // Genau so ist es. ImGui misst das Klappfenster an den GERADE
        // SICHTBAREN Eintraegen aus - beim Scrollen kommen laengere dazu,
        // und es springt. Bei "SET_RAILCENTERTRACKUNLOCKED" faellt das auf.
        //
        // Der breiteste Eintrag steht von Anfang an fest; er wird einmal
        // gemessen und als Mindestbreite vorgegeben. Dann springt nichts
        // mehr, egal wohin man scrollt.
        //
        // Auch die Abschnittsueberschriften zaehlen mit ("Standard strings"
        // im Bild) - sie stehen in derselben Liste.
        float breitester = 0.0F;
        if (entries != nullptr) {
            std::string letzterAbschnitt;
            for (const TypeEntry& e : *entries) {
                breitester = std::max(breitester,
                                      ImGui::CalcTextSize(e.name.c_str()).x);
                if (e.section != letzterAbschnitt) {
                    letzterAbschnitt = e.section;
                    breitester =
                        std::max(breitester,
                                 ImGui::CalcTextSize(e.section.c_str()).x);
                }
            }
        }
        // Gedeckelt auf die Arbeitsflaeche.
        //
        // Seit rc511 setzt die Liste ihre Mindestbreite selbst. Bei sehr
        // langen Eintraegen kann die groesser werden als der Bildschirm -
        // ImGui schiebt das Fenster dann zurecht, aber der rechte Rand
        // liegt trotzdem draussen.
        //
        // Eine MINDESTbreite, die groesser ist als der Platz, ist keine
        // Mindestbreite mehr, sondern ein Widerspruch.
        const float platzB =
            ImGui::GetMainViewport()->WorkSize.x -
            ImGui::GetStyle().WindowPadding.x * 4.0F;
        const float minB =
            (breitester > 0.0F)
                ? breitester + ImGui::GetStyle().FramePadding.x * 2.0F +
                      ImGui::GetStyle().ScrollbarSize +
                      ImGui::GetStyle().WindowPadding.x * 2.0F
                : 0.0F;
        const float minBGedeckelt = std::min(minB, std::max(platzB, 0.0F));
        // Je Liste EINMAL ins Protokoll.
        //
        // Meine erste Fassung verglich mit EINER gemerkten Zahl. Im
        // Ereignisfenster wechseln sich aber mehrere Klapplisten ab (261
        // Eintraege und 3), und jede sah den Wert der anderen als
        // "Aenderung" - shanks Protokoll hat dadurch 4248 gleiche Zeilen.
        //
        // Ein Protokoll, das man nicht lesen kann, ist keines. Also je
        // (Anzahl, Breite) nur einmal.
        static std::set<std::pair<int, int>> schonGemeldet;
        const std::pair<int, int> schluessel{
            entries != nullptr ? static_cast<int>(entries->size()) : 0,
            static_cast<int>(minB)};
        if (schonGemeldet.insert(schluessel).second) {
            char zec[160];
            std::snprintf(zec, sizeof(zec),
                          "Klappfenster: %d Eintraege, breitester Text %.0f, "
                          "Mindestbreite %.0f (Platz %.0f%s)",
                          entries != nullptr
                              ? static_cast<int>(entries->size())
                              : 0,
                          static_cast<double>(breitester),
                          static_cast<double>(minBGedeckelt),
                          static_cast<double>(platzB),
                          minB > platzB ? ", GEDECKELT" : "");
            diag::detail(zec);
        }
        g_app->dbgComboMinB = minBGedeckelt;
        const float fensterH = std::min(wanted, cap);
        // Untergrenze EINE Zeile, nicht drei.
        //
        // Drei war meine erste Wahl ("sonst unbedienbar") und holte genau
        // den Fehler zurueck, den die Hoehenrechnung weiter oben schon
        // einmal beseitigt hat: filtert man auf einen Treffer herunter,
        // klafft darunter leerer Raum. Bei einem Treffer ergibt die
        // Rechnung eine Zeile - und `visible` wird ohnehin auf mindestens
        // 1 angehoben, also passt auch die Zeile "kein Treffer" hinein.
        listenH = std::max(line, fensterH - extra);
        g_app->dbgComboListenH = listenH;
        ImGui::SetNextWindowSizeConstraints(
            ImVec2{minBGedeckelt, 0.0F},
            ImVec2{FLT_MAX, fensterH});
    }

    const bool comboOffen = ImGui::BeginCombo("##combo", value.c_str());
    // BeginCombo meldet seine Beschriftung der Testumgebung nicht (nur beim
    // Aufklappen). Der Selbsttest findet Klapplisten sonst nicht.
    if (GImGui->TestEngineHookItems) {
        selbsttestMerkeText(GImGui, key, "##combo");
    }
    if (!comboOffen) {
        // Zugeklappt: den Filtertext vergessen. Sonst rechnet das naechste
        // Aufklappen die Hoehe einmal mit dem alten Text - und die Liste
        // springt im ersten Bild.
        filters.erase(key);
        rollNoch.erase(key);
        // Der Meldemerker steht in comboEdit weiter unten; hier ist er
        // nicht sichtbar. Das Aufraeumen geschieht dort ueber
        // IsWindowAppearing() - eine Stelle, ein Mechanismus.
    } else {
        std::string& filter = filters[key];

        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", filter.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) {
            filter.clear();
            buf[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        const bool entered = ImGui::InputTextWithHint(
            "##filter", tr(Str::FilterHint), buf, sizeof(buf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        filter = buf;
        // Escape schliesst die Liste - mit EINEM Druck, wie eine
        // Windows-Klappliste im Original. Das Suchfeld behielt die Taste
        // bisher fuer sich (verlassen), und ein zweites Escape bewirkte gar
        // nichts: ImGui schliesst Klappfenster mit Escape nur ueber die
        // Tastaturnavigation, und die ist aus. Gefunden vom Selbsttest.
        if ((ImGui::IsItemDeactivated() || !ImGui::IsAnyItemActive()) &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }

        // Eingabetaste: den getippten Text uebernehmen, auch wenn er in der
        // Liste nicht vorkommt. Genau dafuer ist das Feld da.
        if (entered && !filter.empty()) {
            value = filter;
            out.edited = true;
            out.picked = true;
            ImGui::CloseCurrentPopup();
        }

        std::string needle = filter;
        for (char& ch : needle) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }

        // --- Ab hier ein eigenes Kindfenster ------------------------------
        //
        // shank: "die suchleiste die nicht mit Scrollt ... das die suchliste
        // immer oben visible bleibt und beim runter scrollen nicht weg ist."
        //
        // Bisher standen Suchfeld und Liste im SELBEN Rollbereich, also
        // rollte das Feld mit hinaus. Ein eigenes Kindfenster fuer die Liste
        // trennt beides: das Klappfenster selbst rollt dann gar nicht mehr,
        // nur noch das Kind.
        //
        // Die Hoehe kommt von oben, ausdruecklich gerechnet - siehe dort.
        // Die Breite darf 0 sein ("restliche Elterngroesse"), weil die
        // Mindestbreite des Klappfensters bereits am breitesten Eintrag
        // haengt; sie kann also nicht zusammenfallen.
        //
        // `EndChild()` muss in JEDEM Fall gerufen werden, auch wenn
        // `BeginChild` false liefert - das steht so in der Anleitung.
        ImGui::Separator();
        // Einmal je Aufklappen: was gerechnet wurde und was daraus wurde.
        //
        // shank: "adde das ins debug log, das ich dir das schicken kann und
        // du kontrollieren kannst das alles geht." Genau dafuer. Ohne diese
        // Zeilen kann ich von hier aus nicht pruefen, ob das Kindfenster
        // seine Hoehe bekommt - gui/app.cpp laesst sich ohne ImGui nicht
        // einmal uebersetzen.
        // NICHT im ersten Bild messen.
        //
        // rc542 tat genau das und schrieb `Fensterhoehe 24`, waehrend die
        // Liste 600 hoch war - kleiner als das Suchfeld allein. Die Zahl
        // war falsch, nicht der Aufbau: ein selbstwachsendes Fenster kennt
        // seine Groesse im Bild des Auftauchens noch nicht. Dieselbe
        // Ursache wie beim geklemmten SetScrollY in rc541, nur eine
        // Messung statt einer Wirkung.
        //
        // Also im ZWEITEN Bild, einmal je Aufklappen.
        static std::set<ImGuiID> schonGemeldetAuf;
        if (ImGui::IsWindowAppearing()) {
            schonGemeldetAuf.erase(key);
        } else if (schonGemeldetAuf.insert(key).second) {
            char z[220];
            std::snprintf(z, sizeof(z),
                          "Klappliste auf: %d Eintraege, Listenhoehe %.0f, "
                          "Fensterhoehe %.0f, Suchfeld %.0f hoch",
                          entries != nullptr
                              ? static_cast<int>(entries->size())
                              : 0,
                          static_cast<double>(listenH),
                          static_cast<double>(ImGui::GetWindowSize().y),
                          static_cast<double>(
                              ImGui::GetFrameHeightWithSpacing()));
            diag::detail(z);
        }
        ImGui::BeginChild("##liste", ImVec2{0.0F, listenH});

        // --- Der eigene Wert, sichtbar ------------------------------------
        //
        // Uebernehmen liess sich getippter Text schon vorher - mit der
        // Eingabetaste. Nur stand am Feld "Tippen zum Filtern", und damit
        // kam niemand auf die Idee. Movie Duels bringt eigene Animationen
        // mit, die in keiner .bhc stehen; genau dafuer ist das da.
        //
        // Jetzt steht die Moeglichkeit als anklickbare Zeile ganz oben,
        // sobald der getippte Text nicht schon genau ein Eintrag ist. Damit
        // ist auch "kein Treffer" keine Sackgasse mehr, sondern der Ort, wo
        // man den eigenen Namen bestaetigt.
        //
        // Der Wert bleibt erhalten: der Pruefer meldet ihn als Anmerkung
        // V004 ("steht nicht in ..., bleibt unveraendert erhalten"), nicht
        // als Fehler. Geschrieben wird er unveraendert.
        bool exactMatch = false;
        if (entries != nullptr && !filter.empty()) {
            for (const TypeEntry& e : *entries) {
                if (e.name == filter) {
                    exactMatch = true;
                    break;
                }
            }
        }
        if (!filter.empty() && !exactMatch) {
            char label[320];
            // "###eigen" haengt die Kennung fest: sonst leitet ImGui sie aus
            // dem Text ab, und ein getipptes "##" wuerde die Beschriftung
            // abschneiden und die Kennung wandern lassen.
            std::snprintf(label, sizeof(label), tr(Str::UseTyped), filter.c_str());
            const std::string row = std::string(label) + "###eigen";
            if (ImGui::Selectable(row.c_str())) {
                value = filter;
                out.edited = true;
                out.picked = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if (entries != nullptr) {
            // Erst die Anzeigeliste bauen: Ueberschriften und Eintraege
            // gemischt, als Nummern. Danach kann der Clipper darueber
            // laufen.
            //
            // Der Clipper (ImGuiListClipper) reicht nur die SICHTBAREN
            // Zeilen durch. Vorher schnitt ich bei 500 Treffern ab und
            // schrieb "..." - eine Kruecke, weil 1543 Animationsnamen
            // untereinander sonst jedes Bild gekostet haetten. Mit dem
            // Clipper darf die Liste vollstaendig sein.
            struct DisplayRow {
                int entry;         // Nummer im Eintragsfeld, -1 = Ueberschrift
                const std::string* section;
            };
            static std::vector<DisplayRow> rows;
            rows.clear();
            rows.reserve(entries->size());
            const std::string* lastSection = nullptr;
            int currentRow = -1;
            for (const std::size_t i : *reihenfolge) {
                const TypeEntry& e = (*entries)[i];
                if (!needle.empty()) {
                    std::string low = e.name;
                    for (char& ch : low) {
                        ch = static_cast<char>(
                            std::tolower(static_cast<unsigned char>(ch)));
                    }
                    if (low.find(needle) == std::string::npos) {
                        continue;
                    }
                }
                if (!alpha && !e.section.empty() &&
                    (lastSection == nullptr || *lastSection != e.section)) {
                    lastSection = &e.section;
                    rows.push_back(DisplayRow{-1, lastSection});
                }
                if (e.name == value) {
                    currentRow = static_cast<int>(rows.size());
                }
                rows.push_back(DisplayRow{static_cast<int>(i), nullptr});
            }

            if (rows.empty()) {
                // Keine Sackgasse mehr: die Zeile "... uebernehmen" steht
                // schon oben. Hier nur noch der Hinweis, dass die Liste
                // nichts hergibt.
                ImGui::TextDisabled("%s", tr(Str::MsgNoMatch));
            } else {
                // Beim Aufklappen zum aktuellen Eintrag rollen. Mit einem
                // Clipper geht SetItemDefaultFocus nicht mehr - der Eintrag
                // wird ja gar nicht abgeschickt, wenn er ausserhalb liegt.
                // Deshalb ueber SetScrollY, das die Nummer kennt.
                //
                // Aber NICHT im ersten Bild. Das stand hier und wirkte
                // nicht: `SetScrollY` setzt nur ein Ziel, und ImGui klemmt
                // es beim naechsten `Begin()` gegen `ScrollMax`. Der Wert
                // stammt aus der Inhaltshoehe des VORIGEN Bildes - und die
                // ist beim frisch aufgeklappten Fenster null. Das Ziel
                // wurde also auf 0 geklemmt, und die Liste stand oben.
                //
                // shank zu rc540, mit zwei Bildern: die Klappliste geht bei
                // SET_ORIGIN auf und zeigt SET_PARM1 ganz oben, statt zum
                // aktuellen Eintrag zu rollen.
                //
                // Also im ZWEITEN Bild rollen. Dann steht die Inhaltshoehe
                // fest, weil der Clipper sie im ersten gesetzt hat. Ein
                // Bild Verzoegerung sieht niemand - dieselbe Ueberlegung
                // wie bei der Hoehenrechnung weiter oben.
                if (ImGui::IsWindowAppearing()) {
                    rollNoch[key] = 1;
                } else {
                    const auto rn = rollNoch.find(key);
                    if (rn != rollNoch.end()) {
                        const float ziel =
                            static_cast<float>(std::max(currentRow, 0)) *
                            ImGui::GetTextLineHeightWithSpacing();
                        if (currentRow >= 0) {
                            ImGui::SetScrollY(ziel);
                        }
                        // Was gewollt war, was moeglich ist, was schon
                        // steht. Nur diese eine Zeile je Aufklappen.
                        char z[200];
                        std::snprintf(
                            z, sizeof(z),
                            "Klappliste rollt: Zeile %d, Ziel %.0f, "
                            "ScrollMax %.0f, Kindhoehe %.0f%s",
                            currentRow, static_cast<double>(ziel),
                            static_cast<double>(ImGui::GetScrollMaxY()),
                            static_cast<double>(ImGui::GetWindowSize().y),
                            currentRow < 0 ? "  (Wert nicht in der Liste)"
                                           : "");
                        diag::detail(z);
                        rollNoch.erase(rn);
                    }
                }
                // Die Zeilenhoehe AUSDRUECKLICH mitgeben.
                //
                // Der Clipper setzt gleich hohe Zeilen voraus, und ohne
                // Angabe nimmt er die Hoehe der ERSTEN als Mass fuer alle
                // (ocornut/imgui#6042: "the ImGuiListClipper assumes that
                // all the rows are the same height").
                //
                // Unsere erste Zeile ist eine Ueberschrift, und die ist
                // hoeher als ein Eintrag. Also rechnete er die ganze Liste
                // mit Ueberschriftshoehe - bei siebzehn Zeilen ergab das
                // mehrere Zeilen Luft am unteren Rand, die niemand fuellt.
                // Genau der Abstand, der zu sehen war.
                //
                // Deshalb sind unten AUCH die Ueberschriften genau eine
                // Textzeile hoch. Beides zusammen - gleiche Hoehe und
                // angesagte Hoehe - macht die Rechnung exakt.
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(rows.size()),
                              ImGui::GetTextLineHeightWithSpacing());
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd;
                         ++row) {
                        const DisplayRow& d = rows[static_cast<std::size_t>(row)];
                        if (d.entry < 0) {
                            // Eine Ueberschrift, genau EINE Textzeile hoch.
                            //
                            // ImGui::SeparatorText ist hoeher - es setzt
                            // Abstand darueber und darunter. Das bringt den
                            // Clipper aus dem Tritt (siehe oben). Also der
                            // Text gedaempft, und der Strich daneben von
                            // Hand gezogen: sieht gleich aus, ist aber so
                            // hoch wie jeder andere Eintrag.
                            const ImVec2 wo = ImGui::GetCursorScreenPos();
                            ImGui::TextDisabled("%s", d.section->c_str());
                            const float th = ImGui::GetTextLineHeight();
                            const float breite =
                                ImGui::GetContentRegionAvail().x;
                            const float hinter =
                                wo.x + ImGui::CalcTextSize(d.section->c_str()).x +
                                ImGui::GetStyle().ItemSpacing.x;
                            const float rechts = wo.x + breite;
                            if (rechts > hinter) {
                                ImGui::GetWindowDrawList()->AddLine(
                                    ImVec2{hinter, wo.y + th * 0.5F},
                                    ImVec2{rechts, wo.y + th * 0.5F},
                                    ImGui::GetColorU32(ImGuiCol_Separator));
                            }
                            continue;
                        }
                        const TypeEntry& e =
                            (*entries)[static_cast<std::size_t>(d.entry)];
                        if (ImGui::Selectable(e.name.c_str(), e.name == value)) {
                            value = e.name;
                            out.picked = true;
                            // Selbst zuklappen.
                            //
                            // Normalerweise erledigt ImGui das: "By default,
                            // Selectable()/MenuItem() are calling
                            // CloseCurrentPopup()." Das haengt aber am
                            // AKTUELLEN Fenster - und seit rc542 steht die
                            // Liste in einem Kindfenster, und ein Kind ist
                            // kein Klappfenster. Also blieb die Liste offen.
                            //
                            // shank zu rc542: "die dropdown liste geht nicht
                            // mehr automatisch zu, wenn man was ausgewaehlt
                            // hat."
                            //
                            // Die Zeile "eigenen Wert uebernehmen" weiter
                            // oben tat es von Anfang an selbst - deshalb ist
                            // die nie aufgefallen.
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered() && !e.desc.empty()) {
                            ImGui::SetTooltip("%s", e.desc.c_str());
                        }
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return out;
}

// Einen Ordner als Spielordner aufnehmen, falls er noch fehlt.
void addGamePathDirectory(const std::string& dir) {
    if (dir.empty()) {
        return;
    }
    for (const std::string& g : g_app->settings.gamePaths) {
        if (g == dir) {
            return;
        }
    }
    g_app->settings.gamePaths.push_back(dir);
    rescanGamePaths();
    refreshPk3List();
}

// Vor jedem Wegwerfen fragen. Das Original zeigt denselben Dialog; ohne ihn
// verliert man die Arbeit beim versehentlichen Klick auf "New".
// Vor dem Wegwerfen fragen - und DANACH weitermachen.
//
// Die alte Fassung gab ein bool zurueck, weil eine Windows-MessageBox
// sofort antwortet. Ein Fenster im eigenen Stil tut das nicht: es steht ein
// paar Bilder lang da und wartet. Also wird mitgegeben, was danach geschehen
// soll.
//
// Ist nichts zu sichern, laeuft es unveraendert durch - dann ist der Aufruf
// so gut wie kostenlos.
void withUnsaved(std::function<void()> then, std::function<void()> onCancel) {
    // Ungesicherte Aenderungen: IMMER fragen, wie das Original.
    //
    // Vorher hiess "Yes/No query on Open/New/Exit" abgeschaltet: gar nicht
    // fragen - und die Aenderungen gingen still verloren. Am laufenden
    // BehavEd.exe beobachtet: "Current script has unsaved changes. Save
    // this file first?" kommt auch mit ausgeschaltetem Schalter. Der steuert
    // dort nur die ZUSAETZLICHEN Fragen "Open?", "New?", "Exit?" (doOpen,
    // doNew, confirmQuit).
    if (!g_app->doc.dirty()) {
        then();
        return;
    }
    (void)onCancel;
    g_app->askSaveName = !g_app->path.empty()  ? fileName(g_app->path)
                         : !g_app->shownName.empty() ? g_app->shownName
                                                     : std::string("unnamed.txt");
    g_app->askSaveThen = std::move(then);
    g_app->askSaveCancel = std::move(onCancel);
    g_app->askSaveOpen = true;
}

void frage(const std::string& text, std::function<void()> ja,
           std::function<void()> nein) {
    g_app->frageText = text;
    g_app->frageJa = std::move(ja);
    g_app->frageNein = std::move(nein);
    g_app->frageOffen = true;
    diag::detail("Frage: " + text);
}

// Die Ja/Nein-Frage - dieselbe Form wie die Speichernfrage darunter.
// Eingabe heisst Ja, Escape Nein, wie bei einer MessageBox.
void drawFrage() {
    if (!g_app->frageOffen) {
        return;
    }
    const std::string titel = std::string(tr(Str::AppTitle)) + "###frage";
    ImGui::OpenPopup(titel.c_str());
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2{vp->Pos.x + vp->Size.x * 0.5F, vp->Pos.y + vp->Size.y * 0.5F},
        ImGuiCond_Appearing, ImVec2{0.5F, 0.5F});
    if (!ImGui::BeginPopupModal(titel.c_str(), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    ImGui::TextUnformatted(g_app->frageText.c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const float knopfB = std::max(bhed::gui::dx(kEdOkW),
                                  ImGui::CalcTextSize(tr(Str::AnswerYes)).x +
                                      ImGui::GetStyle().FramePadding.x * 2.0F);
    const float reihe = knopfB * 2.0F + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(std::max(ImGui::GetStyle().WindowPadding.x,
                                  (ImGui::GetWindowWidth() - reihe) * 0.5F));
    int antwort = 0;   // 1 = Ja, 2 = Nein
    if (ImGui::Button(tr(Str::AnswerYes), ImVec2{knopfB, 0.0F}) ||
        ImGui::Shortcut(ImGuiKey_Enter) || ImGui::Shortcut(ImGuiKey_KeypadEnter)) {
        antwort = 1;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::AnswerNo), ImVec2{knopfB, 0.0F}) ||
        ImGui::Shortcut(ImGuiKey_Escape)) {
        antwort = 2;
    }
    if (antwort != 0) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    if (antwort != 0) {
        // Erst schliessen, dann ausfuehren: die Antwort darf selbst wieder
        // fragen (Einfuegen nach "Proceed?", Beenden nach "Exit?").
        std::function<void()> dann =
            std::move(antwort == 1 ? g_app->frageJa : g_app->frageNein);
        g_app->frageOffen = false;
        g_app->frageJa = nullptr;
        g_app->frageNein = nullptr;
        diag::detail(std::string("Frage beantwortet: ") + (antwort == 1 ? "Ja" : "Nein"));
        if (dann) { dann(); }
    }
}

// --- Zwischenablage wie im Original ---------------------------------------
//
// Das Original legt kopierte Zeilen als TEXT in die Windows-Zwischenablage:
// "//(BHVD)", Zeilenumbruch, dann die Skriptzeilen - beobachtet:
//
//     //(BHVD)
//     declare ( /*@DECLARE_TYPE*/ FLOAT, "rand" );
//
// Damit tauscht man mit Notepad, Foren, einer zweiten Instanz und dem
// Original selbst. behaved hatte bisher nur eine interne Ablage.
//
// Die interne Ablage bleibt: steht in Windows noch genau der Text, den wir
// selbst hineingelegt haben, wird sie benutzt. Sonst wird der Text gelesen
// wie eine Skriptdatei.
//
// ImGui::SetClipboardText schreibt Unicode-Text; Windows liefert daraus
// auch CF_TEXT, das Original liest ihn also.
void ablageNachWindows() {
    const std::vector<Node>& c = g_app->doc.clipboard();
    if (c.empty()) {
        return;
    }
    std::string text = "//(BHVD)\r\n";
    text += writeNodes(c, 0);
    ImGui::SetClipboardText(text.c_str());
    g_app->ablageText = text;
}

void kopieren(bool ausschneiden) {
    const Path sel = g_app->selectedPath;
    const std::vector<Path> m = selectionOrCurrent();
    if (m.empty()) {
        // Original: "No line selected, Copy entire script?" - beim
        // Ausschneiden ohne Auswahl tut es nichts.
        if (!ausschneiden && !g_app->doc.script().nodes.empty()) {
            frage(tr(Str::AskCopyAll), [] {
                g_app->doc.setClipboard(g_app->doc.script().nodes);
                ablageNachWindows();
                setStatus(tr(Str::MsgCopied),
                          static_cast<int>(g_app->doc.script().nodes.size()), 0);
            });
        }
        return;
    }
    if (ausschneiden) {
        const bool ok = m.size() > 1 ? g_app->doc.cutAll(m) : g_app->doc.cutAt(sel);
        if (ok) {
            ablageNachWindows();
            g_app->selection.clear();
            g_app->selectedPath.clear();
            rebuildTree();
        }
    } else {
        const bool ok = m.size() > 1 ? g_app->doc.copyAll(m) : g_app->doc.copyAt(sel);
        if (ok) { ablageNachWindows(); }
    }
}

void kopiereWeg(const Path& weg, bool ausschneiden) {
    if (ausschneiden) {
        if (g_app->doc.cutAt(weg)) {
            ablageNachWindows();
            rebuildTree();
        }
    } else if (g_app->doc.copyAt(weg)) {
        ablageNachWindows();
    }
}

void einfuegen(const Path& hinter) {
    const char* roh = ImGui::GetClipboardText();
    const std::string text = (roh != nullptr) ? std::string(roh) : std::string();
    const Path at = hinter;
    // In einen angeklickten Block ans Ende (siehe zielBlock) - nur, wenn das
    // Ziel die Auswahl ist; das Kontextmenue einer anderen Zeile meint
    // "hinter diese Zeile".
    const Path block = (hinter == g_app->selectedPath) ? zielBlock() : Path{};
    auto rein = [at, block] {
        if (!block.empty()) {
            if (g_app->doc.pasteInto(block)) {
                g_app->expanded.setOpen(kennungFuer(block), true);
                rebuildTree();
            }
            return;
        }
        if (g_app->doc.pasteAfter(at)) { rebuildTree(); }
    };
    // Nichts in Windows, oder noch unser eigener Text: die interne Ablage.
    if (text.find_first_not_of(" \t\r\n") == std::string::npos ||
        text == g_app->ablageText) {
        rein();
        return;
    }
    const bool vonBehaved = text.rfind("//(BHVD)", 0) == 0;
    auto uebernehmen = [text, vonBehaved, rein] {
        std::string rumpf = text;
        if (vonBehaved) {
            const std::size_t nl = rumpf.find('\n');
            rumpf = (nl == std::string::npos) ? std::string() : rumpf.substr(nl + 1);
        }
        Script sc;
        std::vector<Diag> d;
        (void)readScript(rumpf, sc, d);
        // Leerzeilen am Rand gehoeren nicht zum Kopierten.
        while (!sc.nodes.empty() && sc.nodes.front().kind == Node::Kind::Blank) {
            sc.nodes.erase(sc.nodes.begin());
        }
        while (!sc.nodes.empty() && sc.nodes.back().kind == Node::Kind::Blank) {
            sc.nodes.pop_back();
        }
        diag::detail("Einfuegen aus der Windows-Zwischenablage: " +
                     std::to_string(sc.nodes.size()) + " Zeilen" +
                     (vonBehaved ? " (von BehavEd)" : " (fremder Text)"));
        if (sc.nodes.empty()) {
            return;
        }
        g_app->doc.setClipboard(std::move(sc.nodes));
        rein();
    };
    if (vonBehaved) {
        uebernehmen();
    } else {
        // Original: "You seem to be pasting something that wasn't copied
        // from BehavEd" / "Proceed?"
        frage(tr(Str::AskPasteForeign), uebernehmen);
    }
}

// Die Frage selbst, im Stil des Programms.
void drawAskSave() {
    if (!g_app->askSaveOpen) {
        return;
    }
    ImGui::OpenPopup(tr(Str::AppTitle));
    // Mittig ueber dem Fenster - eine MessageBox steht auch dort.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2{vp->Pos.x + vp->Size.x * 0.5F, vp->Pos.y + vp->Size.y * 0.5F},
        ImGuiCond_Appearing, ImVec2{0.5F, 0.5F});
    if (!ImGui::BeginPopupModal(tr(Str::AppTitle), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    ImGui::TextUnformatted(tr(Str::MsgUnsavedChanges));
    ImGui::Spacing();
    ImGui::TextDisabled("%s", g_app->askSaveName.c_str());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Die Antwort erst NACH dem Schliessen ausfuehren: das Weitermachen
    // kann selbst wieder fragen (beim Beenden mit mehreren Reitern), und
    // dann muss dieses Fenster schon zu sein.
    int antwort = 0;   // 1 = Ja, 2 = Nein, 3 = Abbrechen

    // --- Knoepfe: uebliche Hoehe, mittig, mit Luft darunter ---------------
    //
    // Gemeldet: "die buttons sind etwas abgeschnitten".
    //
    // Sie hatten eine FESTE Hoehe aus dy(kEdFieldH) - gedacht fuer
    // Eingabefelder, wo sie zur Zeilenhoehe passen muss. Bei 144 dpi faellt
    // sie hoeher aus als das, was das Fenster fuer eine Knopfzeile
    // einplant, und der untere Rand schneidet ab.
    //
    // Mit Hoehe 0 nimmt ImGui die Hoehe, die zum Text passt - und die
    // stimmt dann auch mit der gemessenen Fenstergroesse ueberein. Dazu
    // eine Leerzeile darunter, damit die Knoepfe nicht auf der Kante
    // sitzen.
    //
    // Und mittig, wie im Ereignisfenster: drei Knoepfe links in der Ecke
    // sehen aus, als haette man den Rest vergessen.
    const float knopfB = bhed::gui::dx(kEdOkW);
    const float reihe = knopfB * 3.0F + ImGui::GetStyle().ItemSpacing.x * 2.0F;
    ImGui::SetCursorPosX(
        std::max((ImGui::GetWindowWidth() - reihe) * 0.5F,
                 ImGui::GetStyle().WindowPadding.x));

    if (ImGui::Button(tr(Str::AnswerYes), ImVec2{knopfB, 0.0F})) {
        antwort = 1;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::AnswerNo), ImVec2{knopfB, 0.0F})) {
        antwort = 2;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::EditorCancel), ImVec2{knopfB, 0.0F}) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        antwort = 3;
    }
    ImGui::Spacing();
    if (antwort != 0) {
        ImGui::CloseCurrentPopup();
        g_app->askSaveOpen = false;
        std::function<void()> then = std::move(g_app->askSaveThen);
        std::function<void()> beiAbbruch = std::move(g_app->askSaveCancel);
        g_app->askSaveThen = nullptr;
        g_app->askSaveCancel = nullptr;
        ImGui::EndPopup();
        if (antwort == 1) {
            if (doSave() && then) { then(); }
        } else if (antwort == 2) {
            if (then) { then(); }
        } else if (beiAbbruch) {
            beiAbbruch();
        }
        return;
    }
    ImGui::EndPopup();
}

// Ein Skript aus dem Speicher oeffnen.
//
// Herausgezogen, weil es jetzt zwei Aufrufer gibt: das .pk3-Fenster und die
// Skriptliste der Karte. Erkannt wird am INHALT, ob es eine kompilierte .ibi
// ist - die Endung sagt es nicht zuverlaessig, in Movie Duels heissen sie
// mal .IBI und mal .ibi.
void openScriptFromMemory(const std::string& data, const std::string& shownName) {
    // Auch von hier in einen eigenen Reiter - dieselbe Regel wie beim
    // Oeffnen aus einer Datei. Gerade beim Laden einer Mission mit zehn
    // Skripten ist das der Sinn der Sache.
    //
    // Die KARTE bleibt dabei stehen: der neue Reiter uebernimmt den Pfad
    // des alten. Beim Laden einer Mission gehoeren alle Skripte zu
    // derselben .bsp, und die zehnmal zu laden waere Unsinn. Nur ein
    // ausdruecklich neuer Reiter (das Pluszeichen) faengt ohne Karte an.
    const bool leerUndSauber =
        g_app->path.empty() && g_app->shownName.empty() &&
        !g_app->doc.dirty() && g_app->doc.script().nodes.empty();
    if (!leerUndSauber) {
        addTab(true);
        takeTab(g_app->activeTab);
    }
    Script sc;
    std::vector<Diag> d;
    const bool isIbi = data.size() > 4 && data.compare(0, 3, "IBI") == 0;
    if (isIbi) {
        std::vector<IbiBlock> bl;
        if (readIbi(data, bl, d)) {
            (void)decompile(bl, g_app->db, sc, d);
        }
    } else {
        (void)readScript(data, sc, d);
    }
    g_app->doc = Document{sc};
    // Aus einem Archiv gibt es keinen Pfad zum Zurueckspeichern.
    g_app->path.clear();
    g_app->shownName = fileName(shownName);
    // "md_ga/intro_jedi.txt" -> "md_ga/intro_jedi": so heisst es fuer ICARUS.
    {
        std::string sp = shownName;
        for (char& c : sp) { if (c == '\\') { c = '/'; } }
        const auto punkt = sp.find_last_of('.');
        const auto strich = sp.find_last_of('/');
        if (punkt != std::string::npos && (strich == std::string::npos || punkt > strich)) {
            sp.resize(punkt);
        }
        g_app->skriptPfad = sp;
    }
    g_app->selectedPath.clear();
    g_app->selection.clear();
    aenderungsStandNeu();
    g_app->lesezeichen.erase(tabKennung(g_app->activeTab));
    diag::info("Skript geladen: " + shownName);
    rebuildTree();
    setStatus(tr(Str::MsgLoaded), static_cast<int>(g_app->rows.size()), 0);
}


// --- Ueber Dateigrenzen hinweg sichtbar --------------------------------
//
// Diese Funktionen ruft app_view3d.cpp auf. Sie stehen deshalb AUSSERHALB
// der anonymen Gruppe - alles Uebrige darin bleibt auf diese Datei
// beschraenkt, wie es sich gehoert.

void rebuildTree() {
    // Kennungen richtigstellen, BEVOR die Zeilen gebaut werden.
    //
    // Der einzige Ort, an dem das noetig ist: hier laufen alle
    // Bearbeitungen zusammen. Keine der fuenfzehn Stellen, an denen ein
    // Node entsteht, muss etwas von Kennungen wissen.
    g_app->doc.vergibKennungen();
    // Die Gizmos zeigen die Kameras des Skripts - aendert sich das Skript,
    // muss die Ansicht neu.
    g_app->mapDirty = true;
    // Und die Kamerabahn ebenso: sie wird aus denselben Befehlen gebaut.
    g_app->camTrackValid = false;
    // Wege, die es nicht mehr gibt, aus der Auswahl werfen. Sonst zeigt sie
    // nach einer Loeschung auf Knoten, die weg sind - und die naechste
    // Bearbeitung traefe irgendetwas.
    for (std::size_t i = g_app->selection.size(); i > 0; --i) {
        if (nodeAt(g_app->doc.script(), g_app->selection[i - 1]) == nullptr) {
            g_app->selection.erase(g_app->selection.begin() +
                                   static_cast<std::ptrdiff_t>(i - 1));
        }
    }
    g_app->treeOpt.expanded = &g_app->expanded;
    buildTree(g_app->doc.script(), g_app->db, g_app->treeOpt, g_app->rows);
    g_app->issues.clear();
    validate(g_app->doc.script(), g_app->db, g_app->issues);

    // Die Auswahl haengt am WEG, nicht an der Zeilennummer. Sonst springt
    // sie bei jeder Einfuegung eine Zeile weiter, und der naechste Klick auf
    // "Delete" trifft den falschen Knoten.
    g_app->doc.vergibKennungen();
    g_app->zeilenMarke = zeilenMarken(g_app->activeTab, g_app->doc.script(), g_app->rows);
    g_app->selected = -1;
    if (!g_app->selectedPath.empty()) {
        for (std::size_t i = 0; i < g_app->rows.size(); ++i) {
            if (g_app->rows[i].path == g_app->selectedPath) {
                g_app->selected = static_cast<int>(i);
                break;
            }
        }
        if (g_app->selected < 0) {
            g_app->selectedPath.clear();   // Knoten ist weg (geloescht)
        }
    }
}

void openEditorForNode(const Path& p) {
    const Node* n = nodeAt(g_app->doc.script(), p);
    // Ein Makrokopf (standOnly, walkOnly ...) hat keinen Editor. "Edit"
    // klappt ihn im Original auf und wieder zu - am laufenden BehavEd
    // beobachtet (27.09.): dreimal Edit auf standOnly, zu/auf/zu, kein
    // Fenster, keine Meldung.
    if (n != nullptr && n->kind == Node::Kind::Macro) {
        g_app->doc.vergibKennungen();
        const Node* k = nodeAt(g_app->doc.script(), p);
        if (k != nullptr) {
            g_app->expanded.setOpen(k->kennung, !g_app->expanded.isOpen(k->kennung));
            rebuildTree();
        }
        return;
    }
    if (n == nullptr || n->kind != Node::Kind::Command) {
        return;
    }
    // Dieselbe Wahl wie der Pruefer (selectOverload): Feldzahl, Block, die
    // Markierung /*@SET_TYPES*/ und die Art jedes Arguments.
    //
    // Hier stand nur die Feldzahl. Der Abgleich mit dem Original (jeder
    // Befehl geoeffnet, 27.09.) fand zwei Folgen: `set ( "variablename",
    // "value" )` oeffnete mit der SET_TYPES-Klappliste statt zwei
    // Textfeldern, und `wait ( "kyle1 line 2" )` - auf einen Task warten -
    // als Millisekundenfeld <float>. Das Original zeigt beide Male <str>.
    const Command* best = selectOverload(*n, g_app->db);
    if (best == nullptr) {
        return;
    }
    g_app->editorOpen = true;
    g_app->editorInsert = false;
    g_app->editorCmd = best;
    g_app->editorPath = p;
    g_app->editorValues.clear();
    g_app->editorIsExpr.clear();
    g_app->editorWarExpr.clear();
    g_app->editorKinds.clear();
    g_app->editorDefs.clear();
    g_app->editorRefreshFrom = -1;
    // Auch die Helferzeilen zuruecksetzen.
    //
    // Sie wurden nie geleert, und `helferGross` waechst nur. Wer an einem
    // `if` den Ausdruckshelfer benutzt hatte, fand beim NAECHSTEN `if`
    // dieselben Werte vor - Typ, Name, Marke und Bereich.
    //
    // shank zu rc540, mit zwei Bildern: erst
    //     STRING / SET_LEADER / ORIGIN / 0.000 .. 2.000
    // eingestellt, dann ein anderes `if` geoeffnet - und dort stand alles
    // wieder so. "It should reset."
    //
    // Leeren genuegt: `helferGross` fuellt beim naechsten Zeichnen mit den
    // Vorgaben FLOAT / SET_PARM1 / ORIGIN / 0.0 .. 1.0 nach.
    g_app->helpGetType.clear();
    g_app->helpGetName.clear();
    g_app->helpTagName.clear();
    g_app->helpTagType.clear();
    g_app->helpRangeLow.clear();
    g_app->helpRangeHigh.clear();
    // Jedes Oeffnen beginnt in der Mitte des Programmfensters. Die Mitte
    // wird im ersten Bild aus der Arbeitsflaeche gesetzt (drawEditor);
    // negativ heisst "noch nicht gesetzt".
    //
    // Die gemessene Reihe, die frueher hier auf null ging (rc545: sie
    // sickerte vom vorigen Befehl in den naechsten), ist seit rc568 nur
    // noch ein Protokollwert - die Breite wird vor `Begin` ausgerechnet.
    g_app->editorMitte = ImVec2{-1.0F, -1.0F};
    g_app->editorRowW = 0.0F;
    for (const Arg& a : n->args) {
        g_app->editorValues.push_back(a.text);
        g_app->editorIsExpr.push_back(a.kind == Arg::Kind::Expr ? 1 : 0);
        g_app->editorWarExpr.push_back(
            a.kind == Arg::Kind::Expr ? 1 : 0);
    }
    // Die Helferzeilen aus dem WERT zurueckgewinnen.
    //
    // Geleert werden sie oben weiter - sonst schleppte der naechste Befehl
    // die Einstellungen des vorigen mit, und genau das hat shank in rc541
    // gemeldet. Aber danach wird gelesen, was im Feld steht.
    //
    // shank zu rc552: "die werte die ich bei den einzelnen parameters
    // eintrage werden nicht mehr gespeichert."
    //
    // Stimmt: der HELFER baut aus seinen vier Zeilen einen Ausdruck und
    // schreibt ihn ins Feld. Das Feld wird gespeichert, die vier Zeilen
    // nicht - sie standen beim naechsten Oeffnen wieder auf den Vorgaben,
    // obwohl im Feld `get( FLOAT, "SET_PARM1" )` stand.
    //
    // Die drei Formen sind fest und werden an genau einer Stelle gebaut,
    // also lassen sie sich auch an genau einer Stelle zurueckgewinnen:
    //
    //     get( <TYP>, "<NAME>" )
    //     tag( "<NAME>", <ART> )
    //     random( <VON>, <BIS> )
    //
    // Was nicht in diese Formen passt - ein von Hand getippter Ausdruck -,
    // laesst die Helferzeilen auf ihren Vorgaben. Raten waere hier
    // schlimmer als nichts zu tun.
    helferAusEinstellungen(best->name);
    helferAusWert();
    // Was der Editor GELADEN hat - Werte, Ausdrucksmodus, Helferzeilen.
    //
    // shank: "hast du ueberall debug hinzugefuegt?" Fuer den Editor bisher
    // nicht: das Protokoll zeigte die Fenstergroesse und die Spaltenkanten,
    // aber nie die WERTE. Und genau um die ging es.
    {
        std::string z = "Editor auf \"" + best->name + "\":";
        for (std::size_t i = 0; i < g_app->editorValues.size(); ++i) {
            z += " [" + std::to_string(i) + "] \"" + g_app->editorValues[i] +
                 "\"" +
                 ((i < g_app->editorIsExpr.size() && g_app->editorIsExpr[i])
                      ? " (Ausdruck)"
                      : "");
            if (i < g_app->helpGetType.size()) {
                z += " Helfer=" + g_app->helpGetType[i] + "/" +
                     g_app->helpGetName[i] + "/" + g_app->helpTagType[i];
            }
        }
        diag::detail(z);
    }
}

std::string fileName(const std::string& path) {
    const std::size_t cut = path.find_last_of("\\/");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

std::string directoryOf(const std::string& path) {
    const std::size_t cut = path.find_last_of("\\/");
    return cut == std::string::npos ? std::string{} : path.substr(0, cut);
}

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// --- Ein Bericht, den man anhaengt --------------------------------------
//
// Jede Runde des GPU-Umbaus begann damit, dass jemand eine Zahl vorlesen
// musste: welche Zustaende laufen, wie viele Aufrufe, welche Grafikkarte,
// was die Debugschicht sagt. Abgelesen wurde sie aus Bildschirmfotos, und
// zweimal stand die gesuchte Zahl in einem Menue, das beim Hinsehen zu war.
//
// Hier steht alles in einer Datei, neben dem Protokoll.
void schreibeBericht() {
    // --- Kein Knopf ------------------------------------------------------
    //
    // Die erste Fassung stand als "Bericht schreiben..." im View-Menue.
    // shanks Antwort darauf: "das bei view etwas drin ist um es ins log zu
    // schreiben ist bescheuert".
    //
    // Er hat recht, und die Begruendung steht seit rc425 im Quelltext ueber
    // der Debugschicht: "Ein Werkzeug, das man erst einschalten muss, ist
    // genau dann aus, wenn man es braucht." Dieselbe Ueberlegung, und ich
    // habe sie einen Bau lang nicht auf meinen eigenen Knopf angewandt.
    //
    // Der Bericht steht deshalb IM PROTOKOLL, und er wird ohne Zutun
    // geschrieben - beim Beenden und beim Absturz. Eine Datei weniger zum
    // Mitschicken.
    std::ostringstream f;
    f << "\n--- Bericht ---\n";
    // Die Fassung GANZ OBEN. Zu rc448 kam ein Protokoll zurueck, aus dem
    // sich nicht entscheiden liess, welches der beiden Pakete dieses
    // Namens gelaufen war - siehe include/bhed/fassung.h.
    f << "behaved 1.0.0-" << kFassung << ", gebaut " << __DATE__ << " "
      << __TIME__ << "\n";

    f << "\nKarte\n";
    f << "  Dreiecke     " << (g_app->mesh.indexes.size() / 3U) << "\n";
    f << "  Stapel       " << g_app->mesh.batches.size() << "\n";
    f << "  Texturen     " << g_app->textures.found << " gefunden, "
      << g_app->textures.missing << " fehlen\n";
    f << "  Feinheit     " << g_app->meshDetail << "\n";

    f << "\nAnsicht\n";
    f << "  Weg          Grafikkarte (Direct3D 11)"
      << (gpu::verfuegbar() ? "" : " - NICHT verfuegbar") << "\n";
    f << "  Helligkeit   " << g_app->mapBrightness << "\n";
    f << "  Grundlicht   " << g_app->mapMinLight << "\n";
    f << "  Effekte      " << (g_app->showEffects ? "an" : "aus") << "\n";
    f << "  Gluehen      " << (g_app->showGlow ? "an" : "aus") << "\n";

    f << "\nGrafikkarte\n";
    {
        // Der Text kommt aus gpumap - dort steht das Wissen ueber Geraet,
        // Zustaende, Shader und Zeiten, und es soll nicht zweimal existieren.
        std::istringstream is(gpu::berichtText());
        std::string zeile;
        while (std::getline(is, zeile)) {
            f << "  " << zeile << "\n";
        }
    }
    f << "  Aufrufe      " << g_app->gpuAufrufe << ", uebersprungen "
      << g_app->gpuUebersprungen << "\n";
    f << "               Karte " << g_app->gpuKarte
      << ", Effekte " << g_app->gpuEffekte
      << ", Mover " << g_app->gpuMover
      << ", Figuren " << g_app->gpuFiguren2
      << ", Gluehen " << g_app->gpuGluehen << "\n";
    if (!g_app->gpuFehler.empty()) {
        f << "  FEHLER       " << g_app->gpuFehler << "\n";
    }

    // --- Die Debugschicht ------------------------------------------------
    //
    // Diese Meldungen gab es seit rc425, gelesen hat sie niemand: die
    // Schicht schreibt an den Windows-Debugger. Was seither aufgelaufen
    // ist, steht hier.
    // --- Auffaellige Kartenstapel ----------------------------------------
    //
    // Die schwarzen Vierecke auf dem Hologrammtisch waren Kartenflaechen
    // (holo1, holo11, holo111). Drei Runden lang habe ich bei den Figuren
    // gesucht, weil es dort eine Liste gab und hier keine.
    f << "\nAuffaellige Kartenstapel\n";
    {
        const std::vector<gpu::StapelNotiz> n = gpu::auffaelligeStapel();
        if (n.empty()) {
            f << "  keine\n";
        } else {
            for (const gpu::StapelNotiz& z : n) {
                f << "  " << z.anzahl << "x  " << z.shader << "  ["
                  << z.was << "]\n";
            }
        }
    }

    f << "\nFlaechen der Figuren\n";
    if (g_app->figurArten.empty()) {
        f << "  keine\n";
    } else {
        for (const auto& [z, n] : g_app->figurArten) {
            f << "  " << n << "x  " << z << "\n";
        }
    }

    f << "\nMeldungen der Debugschicht\n";
    if (g_app->d3dMeldungen.empty()) {
        f << "  keine\n";
    } else {
        for (const std::string& z : g_app->d3dMeldungen) {
            f << "  " << z << "\n";
        }
    }

    // Zeilenweise ins Protokoll, damit die Einrueckung des Ablaufs stimmt.
    std::istringstream is(f.str());
    std::string zeile;
    while (std::getline(is, zeile)) {
        diag::info(zeile);
    }
}

void addStatus(const std::string& text) {
    char buf[400];
    std::snprintf(buf, sizeof(buf), "(%d) : %s",
                  static_cast<int>(g_app->statusLines.size()), text.c_str());
    g_app->statusLines.emplace_back(buf);
    // Und ins Protokoll, wortgleich.
    //
    // Bisher stand die Statusleiste NUR auf dem Schirm. In shanks
    // rc537-Protokoll steht zweimal `Ziel gleich Herkunft - nichts getan`,
    // also hat er zweimal die neue Meldung aus rc536 gesehen - vermutlich.
    // Nachpruefen liess es sich nicht, und er selbst konnte hinterher
    // nicht nachlesen, was ihm gesagt wurde.
    //
    // Wer blind entwickelt und Protokolle liest, muss darin sehen, was der
    // Benutzer gelesen hat. Sonst deutet man die Haelfte.
    diag::detail("Statuszeile: " + text);
}

void setStatus(const char* fmt, int a, int b) {
    char buf[240];
    std::snprintf(buf, sizeof(buf), fmt, a, b);
    addStatus(buf);
}

void rescanGamePaths() {
    diag::Step step("Spielordner durchsuchen");
    g_app->gamePaths.clear();

    // Alles verwerfen, was AUS den Spielordnern stammt.
    //
    // Die NPC-Liste wurde einmal gelesen und nie wieder - kommt der Ordner
    // mit den Modellen erst danach dazu, bleibt sie fuer immer leer, und
    // keine Figur bekommt ein Modell. Genau so sieht es aus, als suche das
    // Programm nur in der einen gewaehlten .pk3.
    g_app->npcMapRead = false;
    g_app->npcMap.clear();
    // Dasselbe gilt fuer die Shadertabelle und die geladenen Modelle: beide
    // stammen aus den Archiven der Spielordner. Wer einen Ordner hinzufuegt,
    // bekaeme sonst weiter die alte Antwort - und zwar genau die, wegen der
    // er den Ordner hinzugefuegt hat.
    g_app->shaderMapRead = false;
    g_app->shaderMap.clear();
    g_app->modelCache.clear();
    // Die Bildtabelle GEHOERT dazu: ihre Bilder stammen aus denselben
    // Archiven. Wer einen Spielordner hinzufuegt, will die Textur von dort
    // sehen - und bekaeme sonst weiter die alte.
    g_app->texCache.clear();
    g_app->skeletons.clear();
    g_app->effects.clear();
    g_app->effectRunners.clear();
    // Die Liste LEEREN, nicht nur die Merker zuruecksetzen.
    //
    // loadActorModels laeuft nur, wenn die Zahl der Eintraege nicht zur Zahl
    // der Figuren passt. Ein blosses tried=false aendert die Zahl nicht - es
    // liefe also nie wieder, und die Modelle blieben aus, auch nachdem der
    // richtige Spielordner dazugekommen ist.
    g_app->actorAssets.clear();
    g_app->actorWhy.clear();
    // ROFF-Bahnen ebenso - auch die gemerkten Fehlschlaege: der neue Ordner
    // bringt die Datei vielleicht mit.
    g_app->roffs.clear();
    for (const std::string& dir : g_app->settings.gamePaths) {
        GamePath gp;
        if (scanGamePath(dir, gp)) {
            diag::info(dir + ": " + std::to_string(gp.archives.size()) + " Archive, " +
                       std::to_string(gp.fileCount) + " Dateien");
            g_app->gamePaths.push_back(std::move(gp));
        } else {
            diag::warn("keine .pk3 in " + dir);
        }
    }
    // Ist schon eine Karte geladen, ihre Texturen neu suchen und die Szene
    // (Effekte, Modelle, Figuren) neu aufbauen - oben wurde alles geleert.
    // Vorher blieben nach dem Hinzufuegen eines Spielordners die fehlenden
    // Texturen bis zum Neuladen der Karte fehlend (Kartentest 27.09.).
    if (!g_app->geo.empty()) {
        loadMapTextures();
        g_app->camTrackValid = false;
        g_app->mapDirty = true;
    }
}

void refreshPk3List() {
    g_app->pk3Files.clear();
    std::vector<std::string> ext;
    switch (g_app->pk3Kind) {
        case 1: ext = {".bsp"}; break;
        case 2: ext = {".glm"}; break;
        default: ext = {".txt", ".ibi"}; break;
    }
    for (const GamePath& gp : g_app->gamePaths) {
        for (const FoundFile& f : findByExtension(gp, ext)) {
            // Nach Archiv filtern, wenn eines gewaehlt ist.
            if (!g_app->pk3Archive.empty() &&
                f.archive.find(g_app->pk3Archive) == std::string::npos) {
                continue;
            }
            g_app->pk3Files.push_back(f);
        }
    }
}

bool readFromArchives(const std::string& name, std::string& out,
                      std::string* fromArchive) {
    // RUECKWAERTS durch die Spielordner - der SPAETERE gewinnt.
    //
    // Das war ein handfester Fehler, und er hat still das halbe Programm
    // betroffen. Hier lief die Schleife vorwaerts, also gewann "base" gegen
    // "MD" - und damit das Originalspiel gegen den Mod.
    //
    // Die Engine haelt es umgekehrt, code/qcommon/files.cpp,
    // FS_AddGameDirectory():
    //
    //     search->next = fs_searchpaths;
    //     fs_searchpaths = search;
    //
    // Jedes Archiv wird VORN angehaengt. Da sie aufsteigend sortiert
    // hinzukommen (qsort mit paksort) und fs_game NACH fs_basegame an die
    // Reihe kommt, steht am Ende ganz vorn: das alphabetisch letzte Archiv
    // des ZULETZT hinzugefuegten Ordners. Also gewinnt der Mod.
    //
    // Was das ausgemacht hat, an einem Beispiel: Movie Duels ersetzt
    // models/players/_humanoid/_humanoid.gla durch eine Fassung mit 30384
    // Bildern. Geladen wurde die des Originalspiels mit 21376 - jede
    // Animation jenseits davon fehlte, und zwar ohne jede Meldung.
    for (auto gp = g_app->gamePaths.rbegin(); gp != g_app->gamePaths.rend();
         ++gp) {
        for (auto a = gp->archives.rbegin(); a != gp->archives.rend(); ++a) {
            if (const Pk3Entry* e = a->find(name)) {
                std::string err;
                if (readPk3File(*a, *e, out, &err)) {
                    if (fromArchive != nullptr) {
                        *fromArchive = a->path;
                    }
                    return true;
                }
                diag::warn("Auspacken fehlgeschlagen: " + name + " - " + err);
            }
        }
    }
    return false;
}

// Wie lange dauert ein Klang? -1, wenn er nicht zu finden ist.
//
// Gebraucht, damit die Zeitleiste weiss, WANN ein Klang noch laeuft. Ohne
// diese Zahl blieb endMs gleich startMs, und die Bedingung "laeuft gerade"
// war nie erfuellt - deshalb stieg der Klang beim Wiedereinschalten nicht
// ein, sondern man wartete bis zum naechsten.
//
// Das Ergebnis wird gemerkt: dieselbe Datei mehrfach zu entziffern, nur um
// ihre Laenge zu erfahren, waere Verschwendung - und beim Abspielen faellt
// so etwas sofort auf.
double soundLengthMs(const std::string& name) {
    if (name.empty()) {
        return -1.0;
    }
    const auto merk = g_app->soundLen.find(name);
    if (merk != g_app->soundLen.end()) {
        return merk->second;
    }
    double ms = -1.0;
    std::string data;
    if (findSoundData(name, data)) {
        const sound::Sound s = sound::decode(
            reinterpret_cast<const unsigned char*>(data.data()), data.size());
        if (s.ok && s.sampleRate > 0 && s.channels > 0) {
            ms = static_cast<double>(s.samples.size()) /
                 static_cast<double>(s.channels) /
                 static_cast<double>(s.sampleRate) * 1000.0;
        }
    }
    g_app->soundLen[name] = ms;
    return ms;
}

// Einen Klang entschluesselt und gepuffert holen.
//
// Fuer die Mundanimation: sie braucht die Abtastwerte in JEDEM Bild, und
// eine .wav je Bild neu zu entschluesseln waere nicht tragbar.
//
// Steht neben playSound und damit AUSSERHALB des namenlosen Namensraums -
// app_view3d.cpp braucht sie. Genau der Fallstrick aus Abschnitt 4.2 des
// rc241-Handouts; lint_link.py meldet ihn vor dem Binder.
const sound::Sound* soundFor(const std::string& name) {
    if (name.empty()) {
        return nullptr;
    }
    const auto have = g_app->soundCache.find(name);
    if (have != g_app->soundCache.end()) {
        return have->second.ok ? &have->second : nullptr;
    }
    sound::Sound s;
    std::string data;
    if (findSoundData(name, data)) {
        s = sound::decode(reinterpret_cast<const unsigned char*>(data.data()),
                          data.size());
    }
    // AUCH den Fehlschlag merken. Sonst wird in jedem Bild erneut in
    // vierzig Archiven gesucht.
    const auto put = g_app->soundCache.emplace(name, std::move(s));
    return put.first->second.ok ? &put.first->second : nullptr;
}

const std::vector<std::int16_t>* klangFuerWiedergabe(const std::string& name) {
    const auto have = g_app->klangPuffer.find(name);
    if (have != g_app->klangPuffer.end()) {
        return &have->second;
    }
    const sound::Sound* s = soundFor(name);   // entpackt, ebenfalls gemerkt
    if (s == nullptr) {
        return nullptr;
    }
    constexpr int kRate = 44100;
    constexpr int kChannels = 2;
    const auto put = g_app->klangPuffer.emplace(
        name, sound::convert(s->samples, s->sampleRate, s->channels, kRate, kChannels));
    return &put.first->second;
}

void playSound(const std::string& name, double abMs, bool background,
               float lautstaerke, float rechts, std::uint64_t schluessel) {
    if (name.empty()) {
        return;
    }
    if (rechts < 0.0F) {
        rechts = lautstaerke;   // ohne Raum: beide Ohren gleich
    }
    if (lautstaerke <= 0.001F && rechts <= 0.001F) {
        // Auch ein unhoerbarer Satz schneidet den vorigen ab.
        if (schluessel != 0 && g_app->audio.ready()) {
            (void)g_app->audio.play({}, 0.0F, 0.0F, schluessel);
        }
        return;   // zu weit weg, um gehoert zu werden
    }
    diag::Step step("Klang abspielen: " + name);
    // Aus dem Zwischenspeicher: einmal gesucht, entpackt und umgerechnet.
    const std::vector<std::int16_t>* fertig = klangFuerWiedergabe(name);
    if (fertig == nullptr) {
        step.fail("nicht gefunden");
        // Die Meldung wurde gebaut und dann weggeworfen - man hoerte nur
        // nichts und wusste nicht, warum (Kartentest 27.09.). Jetzt in die
        // Statusliste, je Datei einmal, damit das Abspielen sie nicht flutet.
        static std::set<std::string> gemeldet;
        if (gemeldet.insert(name).second) {
            char msg[300];
            std::snprintf(msg, sizeof(msg), tr(Str::SoundNotFound), name.c_str());
            addStatus(msg);
        }
        return;
    }
    // Festes Geraeteformat, alles wird umgerechnet. Begruendung steht in
    // bhed/sound.h: waveOut lehnt ungewoehnliche Raten je nach Treiber ab,
    // und ein Geraet, das je Klang neu geoeffnet wird, wuergt den vorigen ab.
    constexpr int kRate = 44100;
    constexpr int kChannels = 2;
    // Musik auf ein EIGENES Geraet - siehe musicAudio in app_internal.h.
    // Auf einem gemeinsamen Geraet reiht waveOut die Puffer hintereinander,
    // und eine minutenlange Musik haelt jede Stimme dahinter auf.
    Audio& geraet = background ? g_app->musicAudio : g_app->audio;
    if (!geraet.open(kRate, kChannels)) {
        addStatus(tr(Str::SoundNoDevice));
        return;
    }
    // Ein neues Stueck ERSETZT das laufende. Genau das tut die Engine auch:
    // SetConfigstring(CS_MUSIC) schaltet den Hintergrundstrom um, es
    // laufen nie zwei Stuecke uebereinander.
    if (background) {
        geraet.stopAll();
    }
    std::vector<std::int16_t> puffer = *fertig;
    // Die Daempfung gibt der MISCHER an, nicht wir hier.
    //
    // Vorher wurde der Puffer selbst leiser gerechnet. Das ist zwar
    // dasselbe Ergebnis, aber es verschenkt die Genauigkeit: der Mischer
    // summiert in int und begrenzt erst danach, waehrend ein vorher
    // halbierter int16-Puffer schon Stellen verloren hat.
    // MITTEN hinein einsteigen.
    //
    // Gebraucht, wenn der Ton waehrend des Abspielens wieder eingeschaltet
    // wird: der laufende Klang hat dann schon begonnen. Ihn von vorn zu
    // spielen waere gegenueber dem Bild verschoben, ihn wegzulassen hiess
    // bis zum naechsten Klang Stille - beim Anwender fuenf Sekunden.
    //
    // Der Puffer liegt ohnehin ganz im Speicher, also genuegt es, vorn
    // abzuschneiden. Auf eine RAHMENgrenze runden: bei zwei Kanaeln liegen
    // links und rechts abwechselnd im Puffer, und ein Versatz um eine
    // ungerade Zahl vertauscht sie.
    if (abMs > 0.0) {
        const std::size_t rahmen =
            static_cast<std::size_t>(abMs / 1000.0 * kRate);
        const std::size_t ab =
            std::min(rahmen * static_cast<std::size_t>(kChannels),
                     puffer.size());
        puffer.erase(puffer.begin(),
                     puffer.begin() + static_cast<std::ptrdiff_t>(ab));
        if (puffer.empty()) {
            return;   // laengst vorbei
        }
    }
    // Im Selbsttest stumm: alles bis hierher laeuft (Suchen, Entpacken,
    // Umrechnen, Versatz), nur hoeren soll man es nicht - shank arbeitet
    // nebenher ("mach bitte den play sound button aus wenn du testest").
    if (!g_app->stumm) {
        if (schluessel != 0 || rechts != lautstaerke) {
            geraet.play(puffer, lautstaerke, rechts, schluessel);
        } else {
            geraet.play(puffer, lautstaerke);
        }
    }
    char msg[300];
    std::snprintf(msg, sizeof(msg), tr(Str::SoundPlayed), name.c_str());
}

// Musik wie S_StartBackgroundTrack: "music/intro.mp3 music/loop.mp3" - das
// Intro einmal, dann die Schleife endlos; ein einzelner Name ist die
// Schleife (target_play_music, g_target.cpp:1242; worldspawn "music",
// g_spawn.cpp:1682). Vorher wurde der ganze Text als EIN Dateiname
// gesucht, und zwei Namen fanden nie etwas.
//
// abMs: mitten hinein (Wiedereinstieg). Gepuffert wird bis 4 Minuten nach
// dem Einstieg - laenger laeuft keine Zwischensequenz am Stueck.
void spieleMusik(const std::string& spec, double abMs) {
    std::vector<std::string> teile;
    {
        std::string wort;
        for (const char c : spec + " ") {
            if (c == ' ' || c == '\t' || c == ';') {
                if (!wort.empty()) {
                    teile.push_back(wort);
                    wort.clear();
                }
            } else {
                wort += c;
            }
        }
    }
    if (teile.empty()) {
        return;
    }
    const std::vector<std::int16_t>* intro = teile.size() >= 2 ? klangFuerWiedergabe(teile[0]) : nullptr;
    const std::vector<std::int16_t>* schleife = klangFuerWiedergabe(teile.back());
    if (schleife == nullptr || schleife->empty()) {
        playSound(teile.back(), abMs, true);   // meldet "nicht gefunden"
        return;
    }
    constexpr int kRate = 44100;
    constexpr int kChannels = 2;
    Audio& geraet = g_app->musicAudio;
    if (!geraet.open(kRate, kChannels)) {
        addStatus(tr(Str::SoundNoDevice));
        return;
    }
    geraet.stopAll();
    const std::size_t ab = static_cast<std::size_t>(std::max(0.0, abMs) / 1000.0 * kRate) * kChannels;
    const std::size_t bis = ab + static_cast<std::size_t>(240.0 * kRate) * kChannels;
    std::vector<std::int16_t> puffer;
    puffer.reserve(std::min<std::size_t>(bis, 64U << 20));
    if (intro != nullptr) {
        puffer.insert(puffer.end(), intro->begin(), intro->end());
    }
    while (puffer.size() < bis) {
        const std::size_t n = std::min(schleife->size(), bis - puffer.size());
        puffer.insert(puffer.end(), schleife->begin(), schleife->begin() + static_cast<std::ptrdiff_t>(n));
    }
    if (ab >= puffer.size()) {
        return;
    }
    puffer.erase(puffer.begin(), puffer.begin() + static_cast<std::ptrdiff_t>(ab));
    if (!g_app->stumm) {
        geraet.play(puffer, 1.0F);
    }
}

void playSoundSchleife(const std::string& name, double dauerMs, float lautstaerke, float rechts,
                       std::uint64_t schluessel) {
    if (rechts < 0.0F) {
        rechts = lautstaerke;
    }
    if (name.empty() || dauerMs <= 1.0 || (lautstaerke <= 0.001F && rechts <= 0.001F)) {
        return;
    }
    const std::vector<std::int16_t>* fertig = klangFuerWiedergabe(name);
    if (fertig == nullptr || fertig->empty()) {
        return;
    }
    constexpr int kRate = 44100;
    constexpr int kChannels = 2;
    Audio& geraet = g_app->audio;
    if (!geraet.open(kRate, kChannels)) {
        addStatus(tr(Str::SoundNoDevice));
        return;
    }
    // Auf ganze Rahmen, sonst tauschen links und rechts; hoechstens zwei
    // Minuten, damit ein Sprung weit nach vorn keinen riesigen Puffer baut.
    const double sek = std::min(dauerMs, 120000.0) / 1000.0;
    const std::size_t werte =
        static_cast<std::size_t>(sek * kRate) * static_cast<std::size_t>(kChannels);
    std::vector<std::int16_t> puffer;
    puffer.reserve(werte);
    while (puffer.size() < werte) {
        const std::size_t n = std::min(fertig->size(), werte - puffer.size());
        puffer.insert(puffer.end(), fertig->begin(),
                      fertig->begin() + static_cast<std::ptrdiff_t>(n));
    }
    if (!g_app->stumm) {
        if (schluessel != 0 || rechts != lautstaerke) {
            geraet.play(puffer, lautstaerke, rechts, schluessel);
        } else {
            geraet.play(puffer, lautstaerke);
        }
    }
}

// Die Textur zu EINEM Shadernamen laden.
//
// Herausgezogen aus loadMapTextures, damit die .md3-Modelle denselben Weg
// gehen: erst die Datei direkt versuchen, dann ueber das Shaderskript, und
// am Ende Bewegung und Mischart mitnehmen. Zwei getrennte Wege dafuer
// waeren zwei Gelegenheiten, unterschiedlich falsch zu liegen.
// "Nicht gefunden" ist keine Antwort - man will wissen, WO man suchen muss.
//
// Wenn eine Textur fehlt, hat das in der Praxis drei Gruende:
//
//   1. Das Archiv, in dem sie liegt, ist nicht installiert. Sehr haeufig:
//      eine Karte aus MD_Missions_Ep3 benutzt eine Textur aus
//      MD_Maps_Ep3, und wer nur die Missionen hat, hat die Textur nicht.
//   2. Sie heisst anders, als der Shader sagt - anderer Ordner, andere
//      Schreibweise.
//   3. Es gibt sie wirklich nirgends.
//
// Die drei lassen sich unterscheiden, indem man ueber ALLE Archive nach
// dem blossen DATEINAMEN sucht, ohne Ordner und ohne Endung. Findet sich
// "lava" irgendwo, ist es Fall 2 und man sieht sofort wo. Findet sich gar
// nichts, ist es Fall 1 oder 3 - und die Zahl der durchsuchten Archive
// beantwortet nebenbei die Frage, ob ueberhaupt ueberall gesucht wurde.
void reportMissingTexture(const std::string& shaderName) {
    if (!diag::detailOn()) {
        return;
    }
    std::string basis = shaderName;
    const std::size_t slash = basis.find_last_of('/');
    if (slash != std::string::npos) {
        basis = basis.substr(slash + 1);
    }
    for (char& c : basis) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    int archive = 0;
    int treffer = 0;
    for (const GamePath& gp : g_app->gamePaths) {
        for (const Pk3& a : gp.archives) {
            ++archive;
            for (const Pk3Entry& e : a.entries) {
                std::string low = e.name;
                for (char& c : low) {
                    c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }
                const std::size_t sl = low.find_last_of('/');
                const std::string nur =
                    (sl == std::string::npos) ? low : low.substr(sl + 1);
                const std::size_t dot = nur.find_last_of('.');
                const std::string ohne =
                    (dot == std::string::npos) ? nur : nur.substr(0, dot);
                if (ohne != basis) {
                    continue;
                }
                if (treffer < 6) {
                    diag::detail("  gleicher Dateiname in " + fileName(a.path) +
                                 ": " + e.name);
                }
                ++treffer;
            }
        }
    }
    if (treffer == 0) {
        diag::detail("  -> NICHT GEFUNDEN. \"" + basis +
                     "\" kommt in KEINEM der " + std::to_string(archive) +
                     " durchsuchten Archive vor - das Archiv mit dieser "
                     "Textur ist wahrscheinlich nicht installiert.");
    } else {
        diag::detail("  -> NICHT GEFUNDEN, aber der Dateiname kommt " +
                     std::to_string(treffer) +
                     " mal vor (siehe oben) - dann stimmt der ORDNER nicht.");
    }
}

bool loadTextureFor(const std::string& shaderName, TextureSet::Tex& out) {
    // Wie gross duerfen Texturen sein?
    //
    // 256 war zu klein. Die Lava von Mustafar ist 1024x1024; auf 256
    // geschrumpft und dann nah betrachtet sieht sie kloetzchenhaft aus -
    // genau das war zu sehen. 512 vervierfacht den Speicher je Textur,
    // aber bei rund hundert Texturen je Karte sind das etwa 100 MB statt
    // 25 MB, und das ist heute keine Groesse mehr.
    //
    // Warum nicht die volle Aufloesung: die Texturen werden bei JEDEM
    // Bildpunkt angesprochen, und was nicht in den Zwischenspeicher des
    // Rechners passt, kostet mehr als es bringt. 512 ist der Punkt, an dem
    // eine Wand aus normalem Abstand nicht mehr auffaellt.
    constexpr int kMaxSize = 512;
    ShaderMap& shaderMap = g_app->shaderMap;
        // --- Das Skript geht vor der Datei ----------------------------
        //
        // R_FindShader (tr_shader.cpp:3433) sucht ZUERST im Shadertext
        // (FindShaderInShaderText) und greift erst zur Bilddatei, wenn dort
        // nichts steht. Ein Shadername mit Skript faellt also NIE auf eine
        // gleichnamige Datei zurueck.
        //
        // behaved hat es umgekehrt gemacht - Datei zuerst. Solange beide
        // dasselbe Bild meinen, faellt das nicht auf. Wo sie es nicht tun,
        // entsteht ein Zwitter: das BILD kommt aus der Datei, aber
        // `blend`, `texGen`, `alphaConst` und `rgbWave` kommen aus der
        // ersten Stufe des SKRIPTS. Die beiden gehoeren nicht zusammen.
        //
        // So entstand die Verschlechterung an den Tempelsaeulen in rc348:
        // textures/jeditemple/pillarbase hat als erste Stufe
        // textures/imperial/env_shiny_floor mit `tcGen environment`. Das
        // Bild kam aus pillarbase.jpg, die Spiegelkoordinaten aus der
        // Skriptstufe - Koordinaten einer Spiegelung auf einer
        // Steintextur.
        //
        // Gemessen ueber die 43 Karten: 1575 BSP-Shader, 644 mit Skript,
        // 146 davon mit einer ersten Bildstufe, die anders heisst als der
        // Shader - und bei 61 von ihnen liegt zusaetzlich eine Datei
        // gleichen Namens herum. Genau diese 61 waren Zwitter.
        //
        // Die Datei bleibt als RUECKFALL, falls das Bild der ersten Stufe
        // fehlt.
        std::vector<std::string> tries;
        std::string lowered = shaderName;
        for (char& c : lowered) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const auto it = shaderMap.find(lowered);
        if (it != shaderMap.end() && !it->second.image.empty()) {
            tries = textureCandidates(it->second.image);
        }
        for (const std::string& c : textureCandidates(shaderName)) {
            tries.push_back(c);
        }

        std::string data;
        bool got = false;
        for (const std::string& name : tries) {
            // --- Schon entziffert? ------------------------------------
            //
            // Dieselbe Tabelle wie bei den Figuren (rc187), hier fuer die
            // Karte. Gemessen: 1887 ms fuer 103 Texturen, und mehrere
            // Shader zeigen regelmaessig auf dasselbe Bild.
            //
            // ZWEI Fallen stecken darin:
            //
            // 1. Die Karte verkleinert auf 512, die Figuren auf 256. Ein
            //    Eintrag allein nach dem Namen wuerde der Karte das kleine
            //    Bild unterschieben - die Lava von Mustafar saehe
            //    kloetzchenhaft aus. Deshalb steht die Groesse IM
            //    Schluessel.
            //
            // 2. An die fertige Textur haengen Shaderwerte: Scrollen,
            //    Blendmodus, Wellen. Die gehoeren zum SHADER, nicht zum
            //    Bild - zwei Shader koennen dasselbe Bild verschieden
            //    bewegen. Gemerkt werden darum nur die Bildpunkte; die
            //    Werte kommen danach jedes Mal frisch dazu.
            const std::string key =
                image::mappingName(name) + "@" + std::to_string(kMaxSize);
            g_app->kartenBilder.insert(key);
            TextureSet::Tex t;
            const auto hit = g_app->texCache.find(key);
            if (hit != g_app->texCache.end()) {
                t = hit->second;
                if (it != shaderMap.end()) {
                    // Dieselbe Meldung wie an der anderen Stelle - sonst
                    // fehlen im Protokoll genau die KARTEN-Shader, und
                    // `table_blue` (der Holotisch) tauchte nirgends auf.
                    if (it->second.imageStages > 1 ||
                        it->second.blend != BlendMode::Opaque) {
                        const char* wie =
                            (it->second.blend == BlendMode::Add) ? "additiv"
                            : (it->second.blend == BlendMode::AddAlpha)
                                ? "additiv-alpha"
                            : (it->second.blend == BlendMode::Alpha) ? "alpha"
                            : (it->second.blend == BlendMode::Filter)
                                ? "filter"
                                : "deckend";
                        diag::detail(
                            "Shader \"" + name + "\": " +
                            std::to_string(it->second.imageStages) +
                            " Bildstufe(n) gelesen, gezeichnet wird EINE (" +
                            wie + ")");
                    }
                    for (int k = 0; k < it->second.numTexMods; ++k) {
                        t.texMods[k] = it->second.texMods[k];
                    }
                    t.numTexMods = it->second.numTexMods;
                    t.blend = it->second.blend;
                    // Der Alphatest gehoert ZUM SHADER, genau wie blend -
                    // und er ist der Grund, warum die Geonosianer im
                    // schwarzen Kasten standen. Ihn hier zu vergessen
                    // heisst: der Kartenzeichner bekommt ihn nie zu sehen.
                    t.alphaTest = it->second.alphaTest;
                t.alphaConst = it->second.alphaConst;
                t.autosprite = it->second.autosprite;
                    t.cull = it->second.cull;
                    t.rgbWave = it->second.rgbWave;
                    t.deformWave = it->second.deformWave;
                    t.deformSpread = it->second.deformSpread;
                    t.polygonOffset = it->second.polygonOffset;
                    // glow der Stufe, aus der auch das Bild kommt.
                    t.glow = it->second.glow;
                    t.clamp = it->second.clamp;
                    t.depthWrite = it->second.depthWrite;
                    t.depthFunc = it->second.depthFunc;
                    t.alphaGen = it->second.alphaGen;
                    for (int k = 0; k < 3; ++k) {
                        t.rgbConst[k] = it->second.rgbConst[k];
                    }
                    t.ausSkript = true;
                    t.lightmapStufe = it->second.lightmapStufe;
                    t.rgbVertex = it->second.rgbVertex;
                    t.srcFactor = it->second.srcFactor;
                    t.dstFactor = it->second.dstFactor;
                    // --- tcGen environment gilt NUR fuer Zusatzstufen ---
                    //
                    // Gemessen an duel_jeditemple: 43 Shader dort haben die
                    // Umgebungsspiegelung als ERSTE Stufe:
                    //
                    //     textures/jeditemple/pillar
                    //     {
                    //         { map textures/imperial/env_shiny_floor
                    //           tcGen environment }
                    //         { map textures/jeditemple/pillar
                    //           blendFunc GL_SRC_ALPHA GL_SRC_ALPHA }
                    //         { map $lightmap
                    //           blendFunc GL_DST_COLOR GL_ZERO }
                    //     }
                    //
                    // behaved nimmt die erste Bildstufe als DIE Textur der
                    // Flaeche. Hier ist das die Spiegelung - und die
                    // eigentliche Saeulentextur aus der zweiten Stufe wird
                    // nie gezeichnet, weil `GL_SRC_ALPHA GL_SRC_ALPHA`
                    // weder Add noch AddAlpha ist.
                    //
                    // Das ist ein VORHANDENER Fehler, nicht einer von
                    // tcGen. Aber die Spiegelkoordinaten machen ihn
                    // sichtbar: mit den Koordinaten aus der .bsp lag die
                    // Spiegeltextur wie eine gewoehnliche Wand und sah
                    // zufaellig plausibel aus; als echte Spiegelung wird
                    // sie glatt, und die Saeule verliert ihre Zeichnung.
                    //
                    // Solange die Stufenwahl falsch ist, ruecken richtige
                    // Koordinaten das Bild also WEITER vom Spiel weg.
                    // Deshalb bleibt die Basisstufe hier unangetastet -
                    // gemessen, nicht vermutet, und als offener Punkt
                    // notiert statt stillschweigend gedreht.

                }
                // Die Bildpyramide bauen - siehe Tex::buildMips. Ohne sie
                // bliebe alles bei Stufe 0 und flimmerte in der Ferne
                // weiter.
                t.buildMips();
            out = std::move(t);
                got = true;
                break;
            }

            if (!readFromArchives(name, data)) {
                continue;
            }
            // Format am INHALT erkennen, nicht an der Endung: in JKA-Mods
            // liegen regelmaessig JPEG-Dateien mit der Endung .tga.
            const image::Image im = image::decode(
                reinterpret_cast<const unsigned char*>(data.data()), data.size());
            if (!im.ok || im.width <= 0 || im.height <= 0) {
                continue;
            }
            image::shrinkTo(im, kMaxSize, t.width, t.height, t.rgba);
            // In die Tabelle kommen die BILDPUNKTE - vor den Shaderwerten,
            // damit der naechste Shader mit demselben Bild seine eigenen
            // bekommt.
            g_app->texCache[key] = t;
            // Die Bewegung aus dem Shader mitnehmen - sonst steht die Lava
            // still, obwohl das Bild stimmt.
            if (it != shaderMap.end()) {
                // --- Was von einem mehrstufigen Shader ankommt ---------
                //
                // Gemeldet: "der Tisch und das Modell dahinter sehen so
                // komisch aus, ingame sieht das anders aus."
                //
                // Ein Hologramm ist typisch MEHRSTUFIG - der Todesstern in
                // epiii_boc.shader hat vier Stufen, alle mit
                // `blendFunc GL_ONE GL_ONE`, die uebereinander addiert
                // werden.
                //
                // behaved zeichnet nur EINE Stufe, und zwar die zuletzt
                // gefundene. Aus vier aufeinandergelegten Bildern wird also
                // eines - das sieht zwangslaeufig anders aus.
                //
                // Diese Zeile macht sichtbar, WELCHE Flaechen davon
                // betroffen sind und wie viel verlorengeht. Nur die
                // auffaelligen, sonst stehen hier neuntausend Zeilen.
                if (it->second.imageStages > 1 ||
                    it->second.blend != BlendMode::Opaque) {
                    const char* wie =
                        (it->second.blend == BlendMode::Add) ? "additiv"
                        : (it->second.blend == BlendMode::AddAlpha)
                            ? "additiv-alpha"
                        : (it->second.blend == BlendMode::Alpha) ? "alpha"
                        : (it->second.blend == BlendMode::Filter)
                            ? "filter"
                            : "deckend";
                    diag::detail(
                        "Shader \"" + name + "\": " +
                        std::to_string(it->second.imageStages) +
                        " Bildstufe(n) gelesen, gezeichnet wird EINE (" +
                        wie + ")");
                }
                for (int k = 0; k < it->second.numTexMods; ++k) {
                    t.texMods[k] = it->second.texMods[k];
                }
                t.numTexMods = it->second.numTexMods;
                t.blend = it->second.blend;
                t.alphaTest = it->second.alphaTest;
                t.alphaConst = it->second.alphaConst;
                t.autosprite = it->second.autosprite;
                t.cull = it->second.cull;
                t.rgbWave = it->second.rgbWave;
                t.deformWave = it->second.deformWave;
                t.deformSpread = it->second.deformSpread;
                t.polygonOffset = it->second.polygonOffset;
                t.glow = it->second.glow;
                t.clamp = it->second.clamp;
                t.depthWrite = it->second.depthWrite;
                t.depthFunc = it->second.depthFunc;
                t.alphaGen = it->second.alphaGen;
                for (int k = 0; k < 3; ++k) {
                    t.rgbConst[k] = it->second.rgbConst[k];
                }
                t.ausSkript = true;
                t.lightmapStufe = it->second.lightmapStufe;
                t.rgbVertex = it->second.rgbVertex;
                t.srcFactor = it->second.srcFactor;
                t.dstFactor = it->second.dstFactor;
            }
            t.buildMips();
            out = std::move(t);
            got = true;
            break;
        }
        return got;
}

void loadMapTextures() {
    diag::Step step("Texturen suchen");
    // Was die vorige Karte benutzt hat - aufgeraeumt wird am Ende, wenn
    // feststeht, was die neue davon wieder braucht.
    const std::unordered_set<std::string> alteKartenBilder = std::move(g_app->kartenBilder);
    g_app->kartenBilder.clear();
    g_app->textures = TextureSet{};
    // Die hochgeladenen Bilder hingen an den alten Nummern.
    gpu::vergissKarte();
    g_app->fehlendeTexturen.clear();
    // Die Zuordnungen zeigen in textures.byShader - wird die Liste neu
    // gebaut, sind die Nummern wertlos. Sie muessen MIT weg, sonst zeigt
    // ein Partikel die Textur einer Wand.
    g_app->effectShaders.clear();
    if (g_app->geo.shaders.empty() || g_app->gamePaths.empty()) {
        return;
    }

    // Alle .shader-Skripte EINMAL einlesen - und zwar dieselbe Tabelle, die
    // auch die Figurenmodelle benutzen. Bei shanks Installation sind es 40
    // Archive und 9014 Eintraege; sie zweimal aufzubauen kostet Sekunden und
    // bringt nichts.
    ShaderMap& shaderMap = g_app->shaderMap;
    if (!g_app->shaderMapRead) {
        g_app->shaderMapRead = true;
        // Auch hier gilt: der SPAETERE Spielordner gewinnt.
        //
        // parseShaderScript ueberschreibt gleichnamige Eintraege, also muss
        // der Mod ZULETZT gelesen werden - und dafuer die Ordner vorwaerts,
        // nicht rueckwaerts. Das ist genau umgekehrt zu readFromArchives,
        // und aus gutem Grund: dort gewinnt der ERSTE Treffer, hier der
        // LETZTE Eintrag. Beide Male soll der Mod gewinnen.
        //
        // Innerhalb eines Ordners ebenso: die Archive kommen aufsteigend,
        // und das alphabetisch letzte soll oben liegen - also auch hier
        // vorwaerts lesen.
        for (const GamePath& gp : g_app->gamePaths) {
            for (const FoundFile& f : findByExtension(gp, {".shader"})) {
                std::string text;
                if (readFromArchives(f.name, text)) {
                    parseShaderScript(text, shaderMap);
                    parseFogParms(text, g_app->nebelMap);
                }
            }
        }
        diag::info(std::to_string(shaderMap.size()) + " Shadereintraege gelesen");
    }

    g_app->textures.byShader.resize(g_app->geo.shaders.size());

    // --- Die Bilder NEBENLAEUFIG vorwaermen -----------------------------
    //
    // Gemessen: 1931 ms fuer 103 Texturen, und der Zwischenspeicher aus
    // rc188 hat daran NICHTS geaendert. Die Erklaerung liegt nahe - jeder
    // Shader zeigt auf ein eigenes Bild, es gibt also nichts zu sparen.
    // Die Zeit steckt im Entpacken und Entziffern einzigartiger Bilder,
    // und das ist reine Rechenarbeit.
    //
    // Reine Rechenarbeit ohne gemeinsamen Zustand laesst sich verteilen.
    // Erlaubt ist es hier, weil readPk3File seinen EIGENEN Dateizeiger
    // oeffnet (src/pk3.cpp) und weder die Archivliste noch die Shadertabelle
    // dabei veraendert werden - beide sind zu diesem Zeitpunkt fertig
    // aufgebaut und werden nur gelesen.
    //
    // Geschrieben wird erst HINTERHER, von einem Faden: jeder Arbeiter legt
    // seine Ergebnisse in ein eigenes Fach, und das Zusammenfuehren laeuft
    // der Reihe nach. Damit bleibt das Ergebnis unabhaengig von der Zahl
    // der Faeden - dieselbe Regel, die den Zeichner bytegleich haelt.
    {
        std::vector<std::string> namen;
        namen.reserve(g_app->geo.shaders.size());
        for (const BspShader& sh : g_app->geo.shaders) {
            if (sh.isDrawn()) {
                namen.push_back(sh.name);
            }
        }
        const unsigned kerne = std::max(1U, std::thread::hardware_concurrency());
        const std::size_t faeden =
            std::min<std::size_t>(kerne, std::max<std::size_t>(1, namen.size()));
        std::vector<std::vector<std::pair<std::string, TextureSet::Tex>>> faecher(
            faeden);
        std::atomic<std::size_t> naechste{0};
        const auto arbeit = [&](std::size_t fach) {
            for (;;) {
                const std::size_t k = naechste.fetch_add(1);
                if (k >= namen.size()) {
                    return;
                }
                // Dieselben Kandidaten wie in loadTextureFor, damit der
                // Treffer dort auch wirklich passt.
                std::vector<std::string> tries = textureCandidates(namen[k]);
                std::string lowered = image::mappingName(namen[k]);
                const auto it2 = shaderMap.find(lowered);
                if (it2 != shaderMap.end()) {
                    for (const std::string& c :
                         textureCandidates(it2->second.image)) {
                        tries.push_back(c);
                    }
                }
                for (const std::string& name : tries) {
                    // Liegt das Bild SCHON in der Tabelle? Dann gar nicht
                    // erst lesen.
                    //
                    // Genau hier steckten die 440 ms, die das Protokoll des
                    // Anwenders zeigte: beim Reiterwechsel stand
                    // "Bilder vorgewaermt: 0 neu" - und trotzdem dauerte
                    // "Texturen suchen" jedes Mal so lange wie beim ersten
                    // Mal. Der Grund: geprueft wurde erst beim EINSORTIEREN.
                    // Bis dahin waren alle 101 Texturen entpackt und
                    // entziffert - und wurden dann weggeworfen.
                    //
                    // Nur LESEN aus der Tabelle, und geschrieben wird erst
                    // hinterher von einem Faden. Damit bleibt es
                    // ungefaehrlich.
                    if (g_app->texCache.count(
                            image::mappingName(name) + "@512") != 0U) {
                        break;
                    }
                    std::string data;
                    if (!readFromArchives(name, data)) {
                        continue;
                    }
                    const image::Image im = image::decode(
                        reinterpret_cast<const unsigned char*>(data.data()),
                        data.size());
                    if (!im.ok || im.width <= 0 || im.height <= 0) {
                        continue;
                    }
                    TextureSet::Tex t;
                    image::shrinkTo(im, 512, t.width, t.height, t.rgba);
                    faecher[fach].emplace_back(
                        image::mappingName(name) + "@512", std::move(t));
                    break;
                }
            }
        };
        std::vector<std::thread> mannschaft;
        mannschaft.reserve(faeden);
        for (std::size_t f = 1; f < faeden; ++f) {
            mannschaft.emplace_back(arbeit, f);
        }
        arbeit(0);
        for (std::thread& th : mannschaft) {
            th.join();
        }
        std::size_t neu = 0;
        for (const auto& fach : faecher) {
            for (const auto& eintrag : fach) {
                // Auch vorgewaermte Bilder gehoeren zur Karte - sonst blieben
                // die, die loadTextureFor nie nachfragt, fuer immer liegen.
                g_app->kartenBilder.insert(eintrag.first);
                if (g_app->texCache.emplace(eintrag.first, eintrag.second)
                        .second) {
                    ++neu;
                }
            }
        }
        diag::info("Bilder vorgewaermt: " + std::to_string(neu) + " neu, " +
                   std::to_string(faeden) + " Faeden");
    }

    for (std::size_t i = 0; i < g_app->geo.shaders.size(); ++i) {
        const BspShader& sh = g_app->geo.shaders[i];
        if (!sh.isDrawn()) {
            continue;
        }
        TextureSet::Tex t;
        // --- Der Himmel ------------------------------------------------
        //
        // Ein Himmelsshader hat oft GAR KEINE eigene Textur - im Protokoll
        // stand deshalb "keine Textur fuer textures/skies/pmus", und die
        // Flaeche fiel ganz aus. Uebrig blieb der Verlaufshintergrund.
        //
        // Die sechs Seiten heissen <name>_rt _lf _bk _ft _up _dn, in genau
        // dieser Reihenfolge (ParseSkyParms). Sie ist nicht alphabetisch:
        // `bk` steht VOR `ft`.
        //
        // Die Ersatzregel stammt ebenfalls von dort: fehlt eine Seite, wird
        // die VORIGE genommen; fehlt gleich die erste, bleibt sie leer.
        // Das ist keine Schoenheit, sondern das Verhalten der Engine.
        {
            std::string lowSky = sh.name;
            for (char& c : lowSky) {
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }
            const auto sk = g_app->shaderMap.find(lowSky);
            if (sk != g_app->shaderMap.end() && !sk->second.skyBox.empty()) {
                static const char* kSuf[6] = {"rt", "lf", "bk",
                                              "ft", "up", "dn"};
                int letzte = -1;
                int geladen = 0;
                for (const char* suf : kSuf) {
                    TextureSet::Tex seite;
                    if (loadTextureFor(sk->second.skyBox + "_" + suf,
                                       seite)) {
                        letzte = static_cast<int>(
                            g_app->textures.byShader.size());
                        g_app->textures.byShader.push_back(std::move(seite));
                        ++geladen;
                    }
                    t.skyFaces.push_back(letzte);
                }
                diag::detail("Shader \"" + sh.name + "\": Himmelsbox " +
                             sk->second.skyBox + ", " +
                             std::to_string(geladen) + " von 6 Seiten");
                if (letzte >= 0) {
                    // Der Zeichner braucht eine nicht leere Textur, sonst
                    // gilt die Flaeche als texturlos. Die erste vorhandene
                    // Seite tut es - gezeichnet wird sie nie, die Farbe
                    // kommt aus skyFaces.
                    const TextureSet::Tex& ersteSeite =
                        g_app->textures.byShader[
                            static_cast<std::size_t>(t.skyFaces[0] >= 0
                                                         ? t.skyFaces[0]
                                                         : letzte)];
                    t.width = ersteSeite.width;
                    t.height = ersteSeite.height;
                    t.rgba = ersteSeite.rgba;
                    t.mips = ersteSeite.mips;
                    g_app->textures.byShader[i] = std::move(t);
                    ++g_app->textures.found;
                    continue;
                }
                t.skyFaces.clear();
            }
        }
        if (loadTextureFor(sh.name, t)) {
            // --- Die Stufen DARUEBER mitladen -------------------------
            //
            // Gemessen an md_am_sith (rc343): von 70 Shadern haben 31 die
            // Redewendung "Lightmap, dann Textur multiplizieren" - die
            // rechnet der Zeichner ohnehin selbst. Wirklich fehlen die
            // Stufen darueber: das additive Leuchten von `table_blue` und
            // die vier- bis fuenfstufigen Bildschirme.
            //
            // Deshalb wird ab der Stufe geladen, die auf die erste echte
            // Bildstufe folgt - alles davor ist der Standardfall und
            // bleibt unangetastet. So aendert sich an den 31 nichts.
            std::string low = sh.name;
            for (char& c : low) {
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }
            const auto si = g_app->shaderMap.find(low);
            if (si != g_app->shaderMap.end()) {
                bool ersteEchte = true;
                for (const ShaderInfo::Stage& st : si->second.stages) {
                    if (st.lightmap || st.image.empty()) {
                        continue;   // $lightmap belegt keine eigene Textur
                    }
                    if (ersteEchte) {
                        ersteEchte = false;
                        continue;   // die steckt schon in `t`
                    }
                    // --- NUR ADDITIVE Stufen -------------------------
                    //
                    // Gemeldet: "meine Lava ist jetzt schwarz."
                    //
                    // Und zwar zu Recht. Der Lavashader hat drei Stufen:
                    //
                    //     { map lava        rgbGen wave ... }
                    //     { map rock_3      blendFunc GL_DST_COLOR GL_ZERO }
                    //     { map rock_3_glow blendFunc GL_ONE GL_ONE  glow }
                    //
                    // Die mittlere MULTIPLIZIERT mit dunklem Fels. Ich habe
                    // sie zusaetzlich obendrauf gezeichnet - auf eine
                    // Flaeche, die behaveds Zeichner ohnehin schon mit
                    // Textur und Licht multipliziert hat. Zweimal
                    // multipliziert wird schwarz.
                    //
                    // Additive Stufen koennen das nicht: sie ADDIEREN nur
                    // Licht. Wird eine doppelt gezeichnet, ist es zu hell -
                    // aber nie schwarz, und es faellt sofort auf.
                    //
                    // Deshalb ab hier nur `GL_ONE GL_ONE`. Das ist genau
                    // das, was am Holotisch fehlte (das Leuchten von
                    // `table_blue`) und was die Bildschirme brauchen.
                    //
                    // Multiplikative Zusatzstufen bleiben damit offen. Sie
                    // richtig zu legen hiesse, den ganzen Stapel von unten
                    // aufzubauen statt auf das fertige Bild zu addieren -
                    // ein eigener Umbau, kein Zusatz.
                    //
                    // --- GL_SRC_ALPHA GL_ONE kommt dazu ---------------
                    //
                    // Additiv, aber mit dem Alphakanal gewichtet. Auch das
                    // kann nur HELLER machen, nie schwarz - die Begruendung
                    // oben gilt unveraendert.
                    //
                    // Aber nicht jede solche Stufe darf mit. Gemessen ueber
                    // die sechs pk3: von den 141 Weltstufen mit dieser
                    // Mischart brauchen
                    //
                    //     108   tcGen environment   (jetzt gebaut)
                    //      17   alphaGen lightingSpecular
                    //       5   alphaGen entity
                    //
                    // `lightingSpecular` ERSETZT den Alphakanal durch ein
                    // gerechnetes Glanzlicht aus Licht- und Blickrichtung.
                    // behaved kann das nicht. Die Stufe mit dem Alpha des
                    // BILDES zu zeichnen waere kein Naeherungswert, sondern
                    // etwas anderes: ein gleichmaessiger Schleier statt
                    // eines wandernden Glanzes. Dasselbe gilt fuer
                    // `entity`, dessen Wert von einer Spielfigur kommt, die
                    // es hier gar nicht gibt.
                    //
                    // Solche Stufen bleiben deshalb draussen - und das wird
                    // gemeldet, damit die Zahl im Protokoll steht statt in
                    // einer Vermutung.
                    //
                    // --- Jetzt kommen ALLE Bildstufen mit ---------------
                    //
                    // Bis rc349 gingen nur additive Stufen durch, mit der
                    // Begruendung, dass sie nur heller machen koennen und
                    // deshalb nie etwas ausloeschen. Das war eine
                    // Vorsichtsmassnahme gegen eine fehlende RECHNUNG, nicht
                    // gegen ein fehlendes Verstaendnis: es gab keinen
                    // allgemeinen Mischer.
                    //
                    // Seit rc349 gibt es ihn - gerechnet wird, was OpenGL
                    // rechnet, aus den beiden Faktoren von glBlendFunc. Und
                    // seit dieser Runde stimmt auch die REIHENFOLGE, weil
                    // nach dem Shader sortiert wird und nicht mehr je Stufe
                    // (siehe Prepared::sortBlend).
                    //
                    // Damit faellt der Grund weg. Gemessen ueber die
                    // Weltshader der neun pk3, was bisher unter den Tisch
                    // fiel:
                    //
                    //     597  alpha      alle weggelassen
                    //      39  filter     alle weggelassen
                    //      11  deckend    alle weggelassen
                    //
                    // 647 Bildstufen. Darunter die eigentliche
                    // Saeulentextur von textures/jeditemple/pillar, die im
                    // Spiel sichtbar ist und bei uns fehlte.
                    if (st.alphaGen == AlphaGen::Unsupported) {
                        diag::detail("Stufe \"" + st.image + "\" von " +
                                     sh.name +
                                     ": alphaGen nicht nachgebaut - bleibt"
                                     " weg statt falsch");
                        continue;
                    }
                    TextureSet::Tex zusatz;
                    if (!loadTextureFor(st.image, zusatz)) {
                        diag::detail("Stufe \"" + st.image +
                                     "\" von " + sh.name +
                                     " nicht ladbar - wird uebersprungen");
                        continue;
                    }
                    // Die Stufe bringt ihre EIGENE Mischart mit, nicht die
                    // des Shaders - darum geht es ja gerade.
                    zusatz.blend = st.blend;
                    zusatz.alphaTest = st.alphaTest;
                    // --- `depthFunc equal` auf einer alphagetesteten Grundstufe
                    //
                    // shank, 02.10.: die Geonosier im Publikum der Arena
                    // (textures/md_ga/geonosian1) standen in einem schwarzen
                    // Viereck. Der Shader: Bild mit `alphaFunc GE128`, dann
                    // DASSELBE Bild noch einmal multiplizierend mit
                    // `depthFunc equal`. Im Spiel scheitert diese Stufe genau
                    // dort, wo die erste den Bildpunkt verworfen und keine
                    // Tiefe geschrieben hat. Unser Zeichner rechnet "equal"
                    // als LESS_EQUAL (siehe gpumap_win32.cpp, holeTiefe) - die
                    // Stufe lag VOR der Wand dahinter, bestand den Test und
                    // multiplizierte die Wand mit dem schwarzen Rand des
                    // Bildes. Mit demselben Bild ist der Alphatest der
                    // Grundstufe genau die Maske, die das Spiel ueber die
                    // Tiefe bekommt.
                    if (st.depthFunc == DepthFunc::Equal && zusatz.alphaTest == AlphaTest::None &&
                        t.alphaTest != AlphaTest::None &&
                        image::mappingName(st.image) == image::mappingName(si->second.image)) {
                        zusatz.alphaTest = t.alphaTest;
                    }
                    zusatz.alphaConst = st.alphaConst;
                    for (int k = 0; k < st.numTexMods; ++k) {
                        zusatz.texMods[k] = st.texMods[k];
                    }
                    zusatz.numTexMods = st.numTexMods;
                    zusatz.rgbWave = st.rgbWave;
                    // Die Stufe bringt ihr EIGENES glow mit. Bei
                    // `table_blue` glueht genau diese dritte Stufe,
                    // die beiden darunter nicht.
                    zusatz.glow = st.glow;
                    zusatz.texGen = st.texGen;
                    zusatz.clamp = st.clamp;
                    zusatz.depthWrite = st.depthWrite;
                    zusatz.depthFunc = st.depthFunc;
                    zusatz.alphaGen = st.alphaGen;
                    for (int k = 0; k < 3; ++k) {
                        zusatz.rgbConst[k] = st.rgbConst[k];
                    }
                    // Eine Zusatzstufe ist immer geskriptet und bringt nie
                    // die Lightmap mit - die gehoert der Grundstufe.
                    zusatz.ausSkript = true;
                    zusatz.lightmapStufe = false;
                    zusatz.rgbVertex = st.rgbVertex;
                    // --- deformVertexes gilt fuer den GANZEN Shader -------
                    //
                    // Es steht vor den Stufen, nicht in ihnen
                    // (RB_DeformTessGeometry laeuft EINMAL je Flaeche, bevor
                    // die Stufen gezeichnet werden). Bis hierher bekam nur
                    // der Basiseintrag die Verformung.
                    //
                    // Die Folge war schwerer zu finden als der Fehler:
                    // die Basisstufe verschob die Ecken entlang ihrer
                    // Normalen, die Zusatzstufe nicht - also lagen ihre
                    // Bildpunkte auf einer ANDEREN Tiefe und fielen durch
                    // den Tiefenvergleich.
                    //
                    // Gemessen an textures/plasma_mustafar/lava
                    // (`deformVertexes wave 180 sin 11 13 0 .4`):
                    //
                    //     mit Tiefentest        193 Bildpunkte
                    //     ohne Tiefentest   103 059 Bildpunkte
                    //
                    // Gemeldet als "beim Magmafall sind es im Spiel zwei
                    // Texturanimationen und bei uns nur eine" - die zweite
                    // Stufe wurde schlicht nie sichtbar.
                    zusatz.deformWave = si->second.deformWave;
                    zusatz.deformSpread = si->second.deformSpread;
                    zusatz.srcFactor = st.srcFactor;
                    zusatz.dstFactor = st.dstFactor;
                    // --- animMap: die uebrigen Bilder dazuladen ---------
                    //
                    // Jedes Bild bekommt einen VOLLSTAENDIGEN Eintrag mit
                    // denselben Eigenschaften; der Zeichner tauscht dann
                    // nur den Zeiger. Das kostet ein paar Zahlen je Bild
                    // und spart dem Zeichner jede Sonderbehandlung.
                    //
                    // Das erste Bild ist `zusatz` selbst - deshalb steht
                    // sein eigener Index als erster in animFrames, und
                    // deshalb wird er VOR den uebrigen abgelegt.
                    const int eigenerIndex =
                        static_cast<int>(g_app->textures.byShader.size());
                    zusatz.animFreq = st.animFreq;
                    zusatz.animOneShot = st.animOneShot;
                    if (st.animImages.size() > 1) {
                        zusatz.animFrames.push_back(eigenerIndex);
                    }
                    t.extraStages.push_back(eigenerIndex);
                    g_app->textures.byShader.push_back(std::move(zusatz));
                    if (st.animImages.size() > 1) {
                        for (std::size_t bi = 1; bi < st.animImages.size();
                             ++bi) {
                            TextureSet::Tex bild =
                                g_app->textures.byShader[
                                    static_cast<std::size_t>(eigenerIndex)];
                            bild.rgba.clear();
                            bild.mips.clear();
                            if (!loadTextureFor(st.animImages[bi], bild)) {
                                continue;
                            }
                            // Die Eigenschaften der Stufe uebernehmen, das
                            // Bild ist das einzig Neue.
                            TextureSet::Tex& erst =
                                g_app->textures.byShader[
                                    static_cast<std::size_t>(eigenerIndex)];
                            bild.blend = erst.blend;
                            bild.alphaTest = erst.alphaTest;
                            bild.alphaConst = erst.alphaConst;
                            bild.numTexMods = erst.numTexMods;
                            for (int k = 0; k < erst.numTexMods; ++k) {
                                bild.texMods[k] = erst.texMods[k];
                            }
                            bild.rgbWave = erst.rgbWave;
                            bild.glow = erst.glow;
                            bild.texGen = erst.texGen;
                            bild.clamp = erst.clamp;
                            bild.alphaGen = erst.alphaGen;
                            for (int k = 0; k < 3; ++k) {
                                bild.rgbConst[k] = erst.rgbConst[k];
                            }
                            bild.ausSkript = erst.ausSkript;
                            bild.lightmapStufe = erst.lightmapStufe;
                            bild.rgbVertex = erst.rgbVertex;
                            bild.srcFactor = erst.srcFactor;
                            bild.dstFactor = erst.dstFactor;
                            bild.animFrames.clear();
                            bild.animFreq = 0.0F;
                            erst.animFrames.push_back(static_cast<int>(
                                g_app->textures.byShader.size()));
                            g_app->textures.byShader.push_back(
                                std::move(bild));
                        }
                    }
                }
                if (!t.extraStages.empty()) {
                    diag::detail("Shader \"" + sh.name + "\": " +
                                 std::to_string(t.extraStages.size()) +
                                 " zusaetzliche Stufe(n) geladen");
                }
                // --- animMap auf der BASISSTUFE ---------------------------
                //
                // Beide Faelle kommen vor: `textures/asjc_coruscant/gras`
                // hat die Bildfolge als erste Stufe,
                // `textures/asjc_coruscant/rail_1` als dritte. Die
                // Zusatzstufen sind oben behandelt; hier die Basis.
                //
                // Das erste Bild steckt schon in `t` - deshalb kommt der
                // Index von `t` selbst zuerst in die Liste. Den kennt man
                // hier: es ist `i`, der Platz in byShader, an den `t`
                // gleich wandert.
                if (si != g_app->shaderMap.end() &&
                    si->second.animImages.size() > 1) {
                    t.animFreq = si->second.animFreq;
                    t.animOneShot = si->second.animOneShot;
                    t.animFrames.push_back(static_cast<int>(i));
                    for (std::size_t bi = 1;
                         bi < si->second.animImages.size(); ++bi) {
                        TextureSet::Tex bild;
                        if (!loadTextureFor(si->second.animImages[bi],
                                            bild)) {
                            continue;
                        }
                        bild.blend = t.blend;
                        bild.alphaTest = t.alphaTest;
                        bild.alphaConst = t.alphaConst;
                        bild.autosprite = t.autosprite;
                        bild.cull = t.cull;
                        bild.rgbWave = t.rgbWave;
                        bild.deformWave = t.deformWave;
                        bild.deformSpread = t.deformSpread;
                        bild.polygonOffset = t.polygonOffset;
                        bild.glow = t.glow;
                        bild.clamp = t.clamp;
                        bild.alphaGen = t.alphaGen;
                        bild.numTexMods = t.numTexMods;
                        for (int k = 0; k < t.numTexMods; ++k) {
                            bild.texMods[k] = t.texMods[k];
                        }
                        for (int k = 0; k < 3; ++k) {
                            bild.rgbConst[k] = t.rgbConst[k];
                        }
                        bild.ausSkript = t.ausSkript;
                        bild.lightmapStufe = t.lightmapStufe;
                        bild.rgbVertex = t.rgbVertex;
                        bild.srcFactor = t.srcFactor;
                        bild.dstFactor = t.dstFactor;
                        // Die Zusatzstufen gehoeren an die BASIS, nicht an
                        // jedes Bild der Folge - sonst wuerden sie mehrfach
                        // gezeichnet.
                        t.animFrames.push_back(static_cast<int>(
                            g_app->textures.byShader.size()));
                        g_app->textures.byShader.push_back(std::move(bild));
                    }
                    diag::detail("Shader \"" + sh.name + "\": Bildfolge mit " +
                                 std::to_string(t.animFrames.size()) +
                                 " Bildern, " +
                                 std::to_string(
                                     static_cast<int>(si->second.animFreq)) +
                                 " je Sekunde");
                }
            }
            g_app->textures.byShader[i] = std::move(t);
            ++g_app->textures.found;
        } else {
            ++g_app->textures.missing;
            g_app->fehlendeTexturen.push_back(sh.name);
            diag::info("keine Textur fuer " + sh.name);
            reportMissingTexture(sh.name);
        }
    }
    diag::info(std::to_string(g_app->textures.found) + " gefunden, " +
               std::to_string(g_app->textures.missing) + " nicht gefunden");
    // Bilder der vorigen Karte, die diese nicht wieder benutzt, freigeben.
    // Alle Nutzer von loadTextureFor bekommen KOPIEN - es zeigt nichts
    // hinein.
    std::size_t weg = 0;
    for (const std::string& k : alteKartenBilder) {
        if (g_app->kartenBilder.count(k) == 0U) {
            weg += g_app->texCache.erase(k);
        }
    }
    if (weg != 0U) {
        diag::detail("Bilder der vorigen Karte freigegeben: " + std::to_string(weg));
    }
    g_app->mapDirty = true;
}

void loadMapFromBytes(const std::string& bytes, const std::string& shownPath) {
    std::string err;
    MapData m;
    if (!readBsp(bytes, m, &err)) {
        platform::showError(shownPath + "\n" + err, tr(Str::AppTitle));
        return;
    }
    m.path = shownPath;

    // Wer eine Karte ausdruecklich laedt, bekommt sie als EIGENE.
    //
    // Das ist der einzige Weg, auf dem ein Reiter zu einer kommt. Alle
    // anderen benutzen die des Arbeitsbereichs mit.
    if (g_app->activeTab >= 0 &&
        static_cast<std::size_t>(g_app->activeTab) < g_app->tabs.size()) {
        g_app->tabs[static_cast<std::size_t>(g_app->activeTab)].ownMap = true;
    }

    // --- Alles wegraeumen, was zur ALTEN Karte gehoerte -------------------
    //
    // Gemeldet: "dieses mesh objekt ist von der anderen map", und es blieb
    // bis zum Neustart stehen.
    //
    // `clearMap()` raeumt diese Listen - aber die wird beim WECHSEL gar
    // nicht gerufen: eine neue Karte laden ist kein Leeren. Also blieben
    // die aufgestellten Modelle der vorigen Karte im Bild, und das Schiff
    // von Mustafar stand auf Kamino.
    //
    // Betroffen ist alles, was aus den ENTITIES der Karte entsteht:
    // aufgestellte Modelle, laufende Effekte, die Zuordnung der
    // Effektshader. Die Dateicaches (md3Files, md3Meshes) bleiben - die
    // haengen am Dateinamen und nicht an der Karte, und sie noch einmal zu
    // lesen waere reine Verschwendung.
    // Die BRUSH-NETZE muessen mit weg, und zwar besonders dringend.
    //
    // Sie liegen nach UNTERMODELLNUMMER ab ("Netz 5"), nicht nach Namen.
    // Nach einem Kartenwechsel gibt es die Nummer 5 wieder - nur meint sie
    // ein voellig anderes Stueck Geometrie. Gemeldet als "es ist immer noch
    // was da von der anderen map, eventuell ein brush oder so": das waren
    // genau diese Netze, in der neuen Karte an fremder Stelle gezeichnet.
    //
    // Nach Namen abgelegte Caches (md3Files, md3Meshes, texCache) haben
    // dieses Problem nicht - derselbe Name meint dieselbe Datei. Nur
    // Nummern sind je Karte neu vergeben.
    const std::size_t vorherModelle = g_app->mapModels.size();
    const std::size_t vorherNetze = g_app->brushMeshes.size();
    // Alles, was aus DIESER Karte entsteht:
    g_app->mapModels.clear();       // aufgestellte .md3 (misc_model)
    g_app->nebenModelle.clear();    // _d1/_u1 und Bruchstuecke dazu
    g_app->effectShaders.clear();   // Zuordnung Effekt -> Shader
    g_app->effectRunners.clear();   // laufende fx_runner
    g_app->effects.clear();         // gelesene .efx
    g_app->brushMeshes.clear();     // Netze der Brush-Modelle
    // Und was auf NUMMERN in der Karte zeigt.
    //
    // pickedEntity ist ein Index in map.entities - nach dem Wechsel meint
    // dieselbe Zahl eine andere Entity, und die Anzeige zeigte Angaben, die
    // es in der neuen Karte gar nicht gibt.
    g_app->pickedEntity = -1;
    // Auch Kamera- und Schluesselwahl gehoeren zur alten Karte (selectedKey
    // wurde nirgends zurueckgesetzt und zeigte danach auf einen anderen Schluessel).
    g_app->cameraSelected = false;
    g_app->selectedKey = -1;
    g_app->hiddenEntities.clear();
    // Die Klickziele der vorigen Karte (wie clearMap, lint_mapstate).
    g_app->markers.clear();
    g_app->keyMarks.clear();
    g_app->actorMarks.clear();
    // Tueren und ihre Klaenge gehoeren zur alten Karte (wie clearMap).
    g_app->moverSim = MoverSim{};
    g_app->moverKlaenge.clear();
    // Und neu AUFBAUEN: Kartenmodelle und Effekte entstehen in
    // prepareScene(), das die Zeitleiste nur ruft, wenn camTrackValid fehlt.
    // Ohne diese Zeile blieben sie nach einem Kartenwechsel leer - nur die
    // erste Karte nach dem Start hatte sie (Kartenpruefung 27.09.: "Modelle
    // 0" bei jeder Karte). Die Kamerabahn loest ref_tags ueber die neuen
    // Entities auf, auch sie gilt nicht mehr.
    g_app->camTrackValid = false;
    if (vorherModelle != 0U || vorherNetze != 0U) {
        diag::detail("Karte gewechselt: " + std::to_string(vorherModelle) +
                     " aufgestellte Modelle und " + std::to_string(vorherNetze) +
                     " Brush-Netze der vorigen Karte entfernt");
    }

    g_app->map = std::move(m);
    // Die Effektbahnen (samt Spurprotokoll) gehoeren zur alten Geometrie.
    vergissEffektBahnen();

    {
        diag::Step geoStep("Kartengeometrie lesen");
        BspGeometry geo;
        std::string gerr;
        if (readBspGeometry(bytes, geo, &gerr)) {
            // Externe Lightmaps (q3map2 -external) VOR dem Netz: die Stapel
            // haengen an der Lightmapnummer. tr.worldDir ist "maps/<karte>".
            {
                std::string karte = shownPath;
                for (char& c : karte) { if (c == '\\') { c = '/'; } }
                const std::size_t schnitt = karte.find_last_of('/');
                if (schnitt != std::string::npos) { karte = karte.substr(schnitt + 1); }
                const std::size_t punkt = karte.find_last_of('.');
                if (punkt != std::string::npos) { karte = karte.substr(0, punkt); }
                const int extern2 = ladeExterneLightmaps(
                    geo, "maps/" + karte,
                    [](const std::string& n, std::string& out) { return readFromArchives(n, out); });
                if (extern2 > 0) {
                    diag::detail("Externe Lightmaps: " + std::to_string(extern2) + " aus maps/" + karte + "/");
                }
            }
            g_app->geo = std::move(geo);
            // Lightmaps und Texturen der vorigen Karte auf der Grafikkarte
            // vergessen - sie liegen dort nach Nummer.
            gpu::vergissKarte();
            g_app->mapBereit = false;
            // Die Welt ist Untermodell 0 - OHNE die beweglichen Teile, genau
            // wie beim Neubau nach einer Detailaenderung (app_view3d.cpp).
            // Hier stand buildMesh: bis zur ersten Detailaenderung stand jedes
            // Brush-Modell doppelt da, einmal in der Welt und einmal als
            // Mover. Deckend deckungsgleich und unsichtbar, durchscheinend
            // doppelt so dicht - die Hologramme in md_am_sith (alphaGen const
            // 0.4) kamen mit 64 statt 40 Prozent.
            g_app->mesh = g_app->geo.models.empty()
                              ? buildMesh(g_app->geo, g_app->meshDetail, !g_app->showSky)
                              : buildModelMesh(g_app->geo, 0, g_app->meshDetail,
                                               !g_app->showSky);
            g_app->mapBereit = !g_app->mesh.batches.empty();
            bool placed = false;
            for (const MapEntity& e : g_app->map.entities) {
                if (e.classname != "info_player_start" &&
                    e.classname != "info_player_deathmatch") {
                    continue;
                }
                if (e.origin.empty()) { continue; }
                std::istringstream is(e.origin);
                is >> g_app->cam.pos[0] >> g_app->cam.pos[1] >> g_app->cam.pos[2];
                g_app->cam.pos[2] += 64.0F;
                placed = true;
                break;
            }
            if (!placed) {
                for (int k = 0; k < 3; ++k) {
                    g_app->cam.pos[k] = (g_app->geo.mins[k] + g_app->geo.maxs[k]) * 0.5F;
                }
            }
            loadMapTextures();
        } else {
            geoStep.fail(gerr);
            g_app->geo = BspGeometry{};
            g_app->mesh = BspMesh{};
        }
    }
    g_app->mapDirty = true;
    setStatus(tr(Str::MapLoaded), static_cast<int>(g_app->map.entities.size()),
              static_cast<int>(g_app->map.targetNames.size()));
}

// Eine Karte ueber ihre KENNUNG aus den Archiven holen.
//
// Gebraucht beim Reiterwechsel: eine Mission kommt aus einem .pk3 und hat
// keinen Pfad auf der Platte, nur ihren Namen darin. Die Entities daneben
// (.ent) laedt der Missionsweg; hier geht es nur um die Geometrie, und
// mehr braucht der Wechsel auch nicht - die Entities haengen am Skript,
// nicht am Reiter.
bool loadMapFromArchive(const std::string& id) {
    std::string data;
    if (id.empty() || !readFromArchives(id, data)) {
        return false;
    }
    loadMapFromBytes(data, id);
    g_app->settings.mapPath.clear();
    return true;
}

void loadMap(const std::string& p) {
    diag::Step step("Karte laden: " + p);
    const std::string bytes = slurp(p);
    if (bytes.empty()) {
        step.fail("nicht lesbar");
        platform::showError(p, tr(Str::AppTitle));
        return;
    }
    loadMapFromBytes(bytes, p);
    g_app->settings.mapPath = p;
}

void doLoadMap() {
    const std::string start = g_app->settings.mapPath.empty()
                                  ? g_app->settings.lastDir
                                  : directoryOf(g_app->settings.mapPath);
    const std::string p = platform::openFileDialog(
        tr(Str::MapLoad), "JKA maps (*.bsp)|*.bsp|All files (*.*)|*.*", start);
    if (!p.empty()) {
        loadMap(p);
    }
}

void openEntFile() {
    const std::string p = platform::openFileDialog(
        tr(Str::OpenEnt), "Entity files (*.ent)|*.ent|All files (*.*)|*.*",
        g_app->settings.lastDir);
    if (p.empty()) {
        return;
    }
    diag::Step step("Entities laden: " + p);
    const std::string text = slurp(p);
    if (text.empty()) {
        step.fail("nicht lesbar");
        platform::showError(p, tr(Str::AppTitle));
        return;
    }
    std::vector<MapEntity> ents;
    parseEntities(text, ents);
    if (ents.empty()) {
        step.fail("keine Entities gefunden");
        return;
    }
    g_app->map.entities = std::move(ents);
    // Die Namenslisten neu aufbauen - der Editor bietet sie an.
    g_app->map.targetNames.clear();
    g_app->map.cameraGroups.clear();
    for (const MapEntity& e : g_app->map.entities) {
        for (auto* list : {&g_app->map.targetNames, &g_app->map.cameraGroups}) {
            const std::string& v = (list == &g_app->map.targetNames)
                                       ? e.targetname
                                       : e.cameraGroup;
            if (v.empty()) {
                continue;
            }
            if (std::find(list->begin(), list->end(), v) == list->end()) {
                list->push_back(v);
            }
        }
    }
    std::sort(g_app->map.targetNames.begin(), g_app->map.targetNames.end());
    std::sort(g_app->map.cameraGroups.begin(), g_app->map.cameraGroups.end());
    g_app->settings.lastDir = directoryOf(p);
    g_app->mapDirty = true;
    // Alles, was an den ALTEN Entities hing, gilt nicht mehr: die gewaehlte
    // und die ausgeblendeten Entities stehen als Index da, und die
    // Kamerabahn loest ref_tags ueber die Entities auf - neu aufbauen
    // (Kartentest 27.09.; vorher blieb alles auf dem alten Stand).
    g_app->pickedEntity = -1;
    g_app->hiddenEntities.clear();
    g_app->camTrackValid = false;
    char msg[200];
    std::snprintf(msg, sizeof(msg), tr(Str::EntLoaded),
                  static_cast<int>(g_app->map.entities.size()));
    addStatus(msg);
}

void selectRow(std::size_t rowIndex, bool ctrl, bool shift) {
    g_app->auswahlDurchEinfuegen.clear();   // jetzt waehlt der Benutzer
    // Wer zuletzt gewaehlt hat, bekommt die kraeftige Farbe - die andere
    // Liste behaelt ihre Markierung gedaempft. So macht es Windows mit
    // Listen ohne Fokus.
    g_app->selectionOwner = 2;
    // Eine Zeile, die man anklickt, steht schon da, wo man hinschaut - ein
    // liegengebliebenes "zur Auswahl rollen" darf sie nicht in die Mitte
    // ziehen. Die Tastatur setzt das Flag danach selbst wieder (moveSel).
    g_app->scrollToSelected = false;
    if (rowIndex >= g_app->rows.size()) {
        return;
    }
    const Path& p = g_app->rows[rowIndex].path;

    if (shift && g_app->selected >= 0 &&
        static_cast<std::size_t>(g_app->selected) < g_app->rows.size()) {
        // Von der letzten Auswahl bis hierher, ueber die ANGEZEIGTEN Zeilen -
        // nicht ueber die Wege. Der Nutzer sieht Zeilen, keine Baumpfade,
        // und erwartet genau das, was dazwischen steht.
        const auto a = static_cast<std::size_t>(g_app->selected);
        const std::size_t lo = std::min(a, rowIndex);
        const std::size_t hi = std::max(a, rowIndex);
        if (!ctrl) {
            g_app->selection.clear();
        }
        for (std::size_t i = lo; i <= hi; ++i) {
            const Path& q = g_app->rows[i].path;
            if (std::find(g_app->selection.begin(), g_app->selection.end(), q) ==
                g_app->selection.end()) {
                g_app->selection.push_back(q);
            }
        }
        g_app->selectedPath = p;
        return;
    }

    if (ctrl) {
        const auto it = std::find(g_app->selection.begin(), g_app->selection.end(), p);
        if (it != g_app->selection.end()) {
            g_app->selection.erase(it);
            // Der abgewaehlte darf nicht der Bezugspunkt bleiben.
            if (g_app->selectedPath == p && !g_app->selection.empty()) {
                g_app->selectedPath = g_app->selection.back();
            }
        } else {
            g_app->selection.push_back(p);
            g_app->selectedPath = p;
        }
        g_app->selected = static_cast<int>(rowIndex);
        return;
    }

    g_app->selection.assign(1, p);
    g_app->selectedPath = p;
    g_app->selected = static_cast<int>(rowIndex);
}

bool isSelected(const Path& p) {
    return std::find(g_app->selection.begin(), g_app->selection.end(), p) !=
           g_app->selection.end();
}

std::vector<Path> selectionOrCurrent() {
    if (!g_app->selection.empty()) {
        return g_app->selection;
    }
    if (g_app->selectedPath.empty()) {
        return {};
    }
    return {g_app->selectedPath};
}

// Einstellungen laden und anwenden. Der Ort kommt von der Plattformschicht
// (%APPDATA%\behaved), damit die Oberflaeche nichts ueber Windows weiss.
void loadSettings(App* a) {
    diag::Step step("Einstellungen laden");
    a->settingsFile = platform::settingsDirectory();
    if (a->settingsFile.empty()) {
        return;
    }
    a->settingsFile += "/behaved.cfg";
    if (!loadSettingsFile(a->settingsFile, a->settings)) {
        return;   // erste Benutzung
    }
    // In WELCHER Sprache laeuft die Oberflaeche - und woher kommt sie?
    //
    // Ohne diese Zeile liess es sich nicht nachsehen. Aufgefallen ist es
    // nur, weil in shanks rc539-Protokoll eine Statuszeile englisch war,
    // waehrend der Kopf `Gebiet de-DE` meldete. Die Protokolltexte selbst
    // sind fest deutsch und sagen darueber nichts.
    const char* woher = "aus dem Gebietsschema";
    if (const i18n::LanguageInfo* l = i18n::findLanguage(a->settings.language)) {
        i18n::setLanguage(l->language);
        woher = "aus den Einstellungen";
    }
    {
        std::string code = "?";
        for (const i18n::LanguageInfo& l : i18n::languages()) {
            if (l.language == i18n::currentLanguage()) { code = l.code; }
        }
        diag::detail("Sprache: " + code + " (" + woher + ", Einstellung \"" +
                     a->settings.language + "\")");
    }
    // Die Masse der Anordnung in den Programmzustand holen.
    //
    // Sie liegen in beiden - in den Einstellungen, weil sie ueberdauern
    // sollen, und im Zustand, weil das Zeichnen sie jedes Bild braucht.
    // Hier der eine Abgleich beim Start, unten der andere beim Sichern.
    a->timelineFrac = a->settings.timelineFrac;
    a->timelineFrames = a->settings.timelineFrames;
    a->mapSidebar = a->settings.mapSidebar;
    a->splitFracX = a->settings.splitFracX;
    a->splitFracY = a->settings.splitFracY;
    // --- Protokoll: was aus der Datei kam --------------------------------
    //
    // Gemeldet: "beim starten ist immer noch der balken links". Statt zu
    // raten, was davon nicht ankommt, schreibt der Start jetzt auf, WELCHE
    // Werte gelesen wurden. Stehen dort die Vorgaben, wurde nichts
    // gesichert; stehen die richtigen und das Bild sieht anders aus, liegt
    // es an der Anordnung, nicht am Sichern.
    {
        char zeile[300];
        std::snprintf(zeile, sizeof(zeile),
                      "Anordnung geladen: Spalten Ereignisse %.3f, Karte "
                      "%.3f, Status %.3f",
                      static_cast<double>(a->settings.splitEvents),
                      static_cast<double>(a->settings.splitMap),
                      static_cast<double>(a->settings.splitStatus));
        diag::info(zeile);
        std::snprintf(zeile, sizeof(zeile),
                      "   Zeitleiste %.3f, Bilder %s, Seitenleiste %s, "
                      "Raster %.3f/%.3f",
                      static_cast<double>(a->settings.timelineFrac),
                      a->settings.timelineFrames ? "ja" : "nein",
                      a->settings.mapSidebar ? "offen" : "zu",
                      static_cast<double>(a->settings.splitFracX),
                      static_cast<double>(a->settings.splitFracY));
        diag::info(zeile);
    }
    if (const theme::Theme* t = theme::findTheme(a->settings.theme)) {
        a->activeTheme = t;
        a->themePending = true;
    }
    a->uiScale = a->settings.uiScale;
    a->treeOpt.showTypes = a->settings.showTypes;
    a->treeOpt.gFloats = a->settings.gFloats;
    a->treeOpt.foldMacros = a->settings.foldMacros;
}

// Lage und Zustand des Programmfensters. Siehe `Settings::windowPlacement`.
//
// Zwei schmale Funktionen, damit `main_win32.cpp` nicht `app_internal.h`
// einbinden muss - dort ist `App` absichtlich unbekannt.
std::string gemerkteFensterlage() {
    return (g_app != nullptr) ? g_app->settings.windowPlacement : std::string{};
}

void merkeFensterlage(const std::string& wert) {
    if (g_app != nullptr) {
        g_app->settings.windowPlacement = wert;
    }
}

void saveSettings(App* a) {
    if (a == nullptr || a->settingsFile.empty()) {
        return;
    }
    for (const i18n::LanguageInfo& l : i18n::languages()) {
        if (l.language == i18n::currentLanguage()) {
            a->settings.language = l.code;
        }
    }
    if (a->activeTheme != nullptr) {
        a->settings.theme = a->activeTheme->id;
    }
    a->settings.uiScale = a->uiScale;
    a->settings.showTypes = a->treeOpt.showTypes;
    a->settings.gFloats = a->treeOpt.gFloats;
    a->settings.foldMacros = a->treeOpt.foldMacros;
    // Und die Masse der Anordnung zurueck - das Gegenstueck zum Laden.
    a->settings.timelineFrac = a->timelineFrac;
    a->settings.timelineFrames = a->timelineFrames;
    a->settings.mapSidebar = a->mapSidebar;
    a->settings.splitFracX = a->splitFracX;
    a->settings.splitFracY = a->splitFracY;
    // Das Gegenstueck zum Ladeprotokoll.
    //
    // Beim Start steht da, WAS gelesen wurde; ohne diese Zeile weiss man
    // nicht, ob es ueberhaupt geschrieben wurde. Genau diese Frage stand
    // schon einmal im Raum ("beim Starten ist immer noch der Balken
    // links") und liess sich nur zur Haelfte beantworten.
    const bool gesichert = saveSettingsFile(a->settingsFile, a->settings);
    {
        char z[300];
        std::snprintf(z, sizeof(z),
                      "Anordnung gesichert (%s): Ereignisse %.3f, Karte %.3f, "
                      "Status %.3f, Zeitleiste %.3f, Raster %.3f/%.3f",
                      gesichert ? "ok" : "FEHLGESCHLAGEN",
                      static_cast<double>(a->settings.splitEvents),
                      static_cast<double>(a->settings.splitMap),
                      static_cast<double>(a->settings.splitStatus),
                      static_cast<double>(a->settings.timelineFrac),
                      static_cast<double>(a->settings.splitFracX),
                      static_cast<double>(a->settings.splitFracY));
        diag::info(z);
    }
}

// Beim Start die zuletzt bearbeitete Datei wieder oeffnen - der Schalter
// "Re-open last file at startup" aus Dialog 131.
void saveSettings(App* a);

void reopenLastIfWanted(App* a, const std::string& kommandozeile) {
    // Absturz erkennen wie das Original (`legal_exit`): beim Start "nicht
    // sauber" SOFORT speichern, destroyApp setzt es beim Beenden zurueck.
    const bool zuletztSauber = a->settings.legalExit;
    a->settings.legalExit = false;
    saveSettings(a);
    if (!a->settings.gamePaths.empty()) {
        App* keepGp = g_app;
        g_app = a;
        rescanGamePaths();
        g_app = keepGp;
    }
    // Eine Datei von der Kommandozeile geht vor. Das Original: "Error reading
    // file "%s" specified in command line, ignoring..." - loadPath meldet
    // einen Lesefehler selbst.
    if (!kommandozeile.empty()) {
        diag::info("Kommandozeile: " + kommandozeile);
        App* keepCmd = g_app;
        g_app = a;
        loadPath(kommandozeile);
        g_app = keepCmd;
        return;
    }
    // Die Karte NUR laden, wenn "Letzte Datei beim Start oeffnen" gesetzt
    // ist - genau wie das Skript darunter.
    //
    // Vorher geschah es ungefragt, und das ist aus zwei Gruenden falsch:
    // das Programm startete mit einer Karte, die man vor Tagen zuletzt
    // benutzt hatte, ohne dass man es verlangt haette. Und eine .bsp ist
    // gross - yavin2 hat 13,8 MB - der Start dauerte spuerbar laenger,
    // ohne dass man wusste, wofuer.
    if (!a->settings.reopenLastFile || a->settings.recent.empty()) {
        return;
    }
    // Erst fragen, dann laden - wie das Original: "Auto-load most recent
    // file: "%s"?", nach einem Absturz "It appears you didn't exit BehavEd
    // legally last time (crash?) Should I still try and open your last
    // file "%s"?". Die Frage erscheint im ersten Bild.
    const std::string datei = a->settings.recent.front();
    const std::string karte = a->settings.mapPath;
    char text[900];
    std::snprintf(text, sizeof(text),
                  tr(zuletztSauber ? Str::AskAutoLoad : Str::AskCrashReopen),
                  datei.c_str());
    App* keep = g_app;
    g_app = a;
    frage(text, [datei, karte] {
        if (!karte.empty()) {
            loadMap(karte);
        }
        loadPath(datei);
    });
    g_app = keep;
}

// Wird von IM_ASSERT gerufen (siehe gui/imgui_config.h). Schreibt zuerst ins
// Protokoll, bricht dann ab - so wie die Vorgabe von ImGui es auch tut.
void logImGuiAssert(const char* expression, const char* file, int line) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "IMGUI-ZUSICHERUNG: %s   (%s:%d)",
                  expression, file, line);
    diag::error(buf);
    const std::string step = diag::currentStep();
    if (!step.empty()) {
        diag::error("  waehrend: " + step);
    }
    std::abort();
}

void resetIconTexture() { resetIcons(); }

App* createApp() {
    App* a = new App();
    a->activeTheme = theme::findTheme("windows");
    // Ein Reiter von Anfang an. Ohne ihn haette das lebende Dokument keinen
    // Platz zum Parken, und der erste Wechsel wuerde es verlieren.
    a->tabs.push_back(App::Parked{});
    a->activeTab = 0;
    return a;
}

void destroyApp(App* a) {
    a->settings.legalExit = true;   // sauber beendet
    saveSettings(a);
    delete a;
}

void setApp(App* a) {
    g_app = a;
    if (a != nullptr) {
        a->themePending = true;   // nicht hier anwenden, siehe App::themePending
    }
}

const theme::Theme* currentTheme(App* a) {
    return a != nullptr ? a->activeTheme : nullptr;
}

void setThemeById(App* a, const char* id) {
    if (a == nullptr || id == nullptr) {
        return;
    }
    if (const theme::Theme* t = theme::findTheme(id)) {
        a->activeTheme = t;
        a->themePending = true;
    }
}

float uiScale(App* a) { return a != nullptr ? a->uiScale : 1.0F; }
void setUiScale(App* a, float s) {
    if (a != nullptr) {
        a->uiScale = s;
    }
}

// Der Bildschirmmassstab kommt von aussen; die Oberflaeche fragt nicht bei
// Windows nach.
void setDpiScale(App* a, float s) {
    if (a != nullptr && s > 0.0F) {
        a->dpiScale = s;
    }
}

// Befehlsmodell laden. Der Ort wird vom Aufrufer bestimmt, damit die
// Oberflaeche nichts ueber Pfade wissen muss.
bool loadModel(App* a, const std::string& dataDir, std::string* error) {
    diag::Step step("Befehlsmodell laden: " + dataDir);
    a->dataDir = dataDir;
    // Eine in den Einstellungen angegebene .bhc hat Vorrang - Movie Duels
    // bringt moeglicherweise eine eigene mit.
    if (!a->settings.commandFile.empty()) {
        std::vector<LoadDiag> own;
        const std::string dir = a->settings.sourcePath.empty() ? dataDir
                                                               : a->settings.sourcePath;
        if (loadCommandDb(a->settings.commandFile, dir, a->db, own)) {
            (void)loadCommandDb(dataDir + "/supplement.bhc", dir, a->db, own);
            return true;
        }
        diag::warn("eigene .bhc nicht lesbar, nehme die mitgelieferte: " +
                   a->settings.commandFile);
    }
    std::vector<LoadDiag> ld;
    if (!loadCommandDb(dataDir + "/behaved.bhc", dataDir, a->db, ld)) {
        if (error != nullptr) {
            *error = "behaved.bhc not found in " + dataDir;
        }
        return false;
    }
    (void)loadCommandDb(dataDir + "/supplement.bhc", dataDir, a->db, ld);
    char msg[200];
    std::snprintf(msg, sizeof(msg), tr(Str::MsgModelLoaded),
                  static_cast<int>(a->db.commands.size()),
                  static_cast<int>(a->db.typesets.size()),
                  static_cast<int>(a->db.macros.size()));
    a->statusLines.push_back(msg);
    return true;
}

// Ein Bild zeichnen. Der Aufrufer hat ImGui::NewFrame() schon gerufen.
// Darf das Fenster zugehen?
//
// Fuer das KREUZ oben rechts. Der Beenden-Knopf und der Menueeintrag fragen
// selbst, aber das Kreuz geht an der Oberflaeche vorbei - es kommt als
// WM_CLOSE bei der Fensterschicht an, und die weiss nichts von ungesicherten
// Aenderungen.
//
// Gemeldet wurde genau das: "If I go to exit the program or load a new
// script, it should ask me if I want to save before exiting. Currently, if I
// load a script and make some changes and exit the program, it instantly
// closes and my changes are not saved."
//
// Beim LADEN wurde schon gefragt (confirmDiscard in loadPath und im
// pk3-Fenster) - beim Beenden nicht.
// Die Verschiebung und Drehung des Gizmos ins Skript schreiben.
//
// Das ist der Schritt, an dem es darauf ankommt. Drei Dinge sind wichtig:
//
//   1. Ueber replaceAt(), nicht am Skript vorbei. Damit landet die Aenderung
//      im Rueckgaengig-Speicher, das Dokument gilt als ungesichert, und die
//      Auswahl bleibt gueltig - alles, was eine Bearbeitung ausmacht.
//   2. Nur der EINE Wert wird angefasst. Der Knoten wird kopiert, ein
//      Argument ersetzt, der Rest bleibt Zeichen fuer Zeichen stehen. Ein
//      neu gebauter Knoten wuerde Anmerkungen und Schreibweisen verlieren,
//      und dann waere roundtrip nicht mehr bytegleich.
//   3. Geschrieben wird mit "%.3f" - so schreibt BehavEd seine Zahlen, und
//      so stehen sie in Ravens Dateien.
//
// Der Weg fuehrt ueber den Pfad des Schluessels: er zeigt auf genau den
// camera(MOVE)-Befehl, aus dem die Marke entstanden ist.
// Die Drehung ins Skript schreiben.
//
// Sie gehoert NICHT zum MOVE, sondern zum PAN, der darauf folgt - die
// Blickrichtung ist ein eigener Befehl. Gesucht wird deshalb ab dem
// Schluessel VORWAERTS der naechste camera(PAN); steht keiner davor, gibt es
// nichts zu drehen, und das sagen wir auch.
//
// Warum vorwaerts und nicht rueckwaerts: die Engine arbeitet die Befehle der
// Reihe nach ab, ein PAN nach dem MOVE gehoert zu diesem MOVE. Ein PAN
// davor gehoerte zum vorigen.

bool gizmoWriteAngles() {
    // Der Schluessel kommt aus dem SKRIPT, nicht aus App::keyMarks - die
    // Liste des letzten Bildes kannte nur die sichtbaren Schluessel und war
    // beim Abspielen leer (siehe kameraSchluessel).
    const std::vector<KameraSchluessel> ks = kameraSchluessel(g_app->doc.script());
    if (g_app->selectedKey < 0 || static_cast<std::size_t>(g_app->selectedKey) >= ks.size()) {
        return false;
    }
    const KameraSchluessel& schluessel = ks[static_cast<std::size_t>(g_app->selectedKey)];
    const Path& p = schluessel.path;
    if (p.empty()) {
        return false;
    }
    // Geschrieben wird, WAS DIE VORSCHAU ZEIGT - absolut: die Winkel, die
    // am Schluessel angezeigt werden, plus die Drehung. (Vorher "gefundenes
    // PAN + Drehung"; die Kamera sprang nach dem Schreiben um genau einen
    // Schwenk zurueck.)
    float ziel[3];
    for (int k = 0; k < 3; ++k) {
        ziel[k] = (schluessel.hasAng ? schluessel.ang[k] : 0.0F) + g_app->gizmoAngles[k];
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.3f %.3f %.3f", static_cast<double>(ziel[0]),
                  static_cast<double>(ziel[1]), static_cast<double>(ziel[2]));

    const Node* eltern = nullptr;
    const std::vector<Node>* geschwister = &g_app->doc.script().nodes;
    if (p.size() > 1) {
        Path oben = p;
        oben.pop_back();
        eltern = nodeAt(g_app->doc.script(), oben);
        if (eltern == nullptr) {
            return false;
        }
        geschwister = &eltern->children;
    }
    // Das PAN, das zu diesem Schluessel gehoert: das erste nach dem MOVE,
    // vor dem naechsten MOVE.
    for (std::size_t i = p.back() + 1U; i < geschwister->size(); ++i) {
        const Node& n = (*geschwister)[i];
        if (n.name != "camera" || n.args.size() < 2) {
            continue;
        }
        if (n.args[0].text != "PAN") {
            if (n.args[0].text == "MOVE") {
                break;
            }
            continue;
        }
        if (n.args[1].kind != Arg::Kind::Vector) {
            break;
        }
        Node neu = n;
        neu.args[1].text = buf;
        // Die Drehrichtung (drittes Feld) nur fuer die gedrehten Achsen
        // neu - wie im Original "die kurze Richtung".
        if (neu.args.size() >= 3 && neu.args[2].kind == Arg::Kind::Vector) {
            float dir[3] = {0.0F, 0.0F, 0.0F};
            if (std::sscanf(neu.args[2].text.c_str(), "%f %f %f", &dir[0],
                            &dir[1], &dir[2]) == 3) {
                for (int k = 0; k < 3; ++k) {
                    if (g_app->gizmoAngles[k] != 0.0F) {
                        dir[k] = panDirectionFor(g_app->gizmoAngles[k]);
                    }
                }
                char dbuf[64];
                std::snprintf(dbuf, sizeof(dbuf), "%.0f %.0f %.0f",
                              static_cast<double>(dir[0]),
                              static_cast<double>(dir[1]),
                              static_cast<double>(dir[2]));
                neu.args[2].text = dbuf;
            }
        }
        Path panPfad = p;
        panPfad.back() = i;
        if (!g_app->doc.replaceAt(panPfad, neu)) {
            return false;
        }
        for (int k = 0; k < 3; ++k) {
            g_app->gizmoAngles[k] = 0.0F;
        }
        rebuildTree();
        return true;
    }
    // --- Kein eigenes PAN: eins direkt hinter den MOVE setzen -----------
    //
    // Der Schluessel erbte seinen Blick von einem frueheren PAN. Dann
    // scheiterte "write to script" bisher still. Ein PAN mit Dauer 0 setzt
    // den Blick sofort - genau das, was die Vorschau zeigt.
    const Node* move = nodeAt(g_app->doc.script(), p);
    if (move == nullptr || move->args.empty()) {
        return false;
    }
    Node pan = *move;
    pan.children.clear();
    pan.args.clear();
    Arg art = move->args[0];
    art.text = "PAN";
    pan.args.push_back(art);
    Arg winkel;
    winkel.kind = Arg::Kind::Vector;
    winkel.text = buf;
    pan.args.push_back(winkel);
    Arg richtung;
    richtung.kind = Arg::Kind::Vector;
    char dbuf[64];
    std::snprintf(dbuf, sizeof(dbuf), "%.0f %.0f %.0f",
                  static_cast<double>(panDirectionFor(g_app->gizmoAngles[0])),
                  static_cast<double>(panDirectionFor(g_app->gizmoAngles[1])),
                  static_cast<double>(panDirectionFor(g_app->gizmoAngles[2])));
    richtung.text = dbuf;
    pan.args.push_back(richtung);
    Arg dauer;
    dauer.kind = Arg::Kind::Number;
    dauer.text = "0";
    pan.args.push_back(dauer);
    if (!g_app->doc.insertAfter(p, pan)) {
        return false;
    }
    for (int k = 0; k < 3; ++k) {
        g_app->gizmoAngles[k] = 0.0F;
    }
    rebuildTree();
    return true;
}

bool gizmoWriteBack() {
    // Aus dem Skript, nicht aus keyMarks - siehe gizmoWriteAngles.
    const std::vector<KameraSchluessel> ks = kameraSchluessel(g_app->doc.script());
    if (g_app->selectedKey < 0 || static_cast<std::size_t>(g_app->selectedKey) >= ks.size()) {
        return false;
    }
    const Path p = ks[static_cast<std::size_t>(g_app->selectedKey)].path;
    const Node* alt = nodeAt(g_app->doc.script(), p);
    if (alt == nullptr || alt->args.size() < 2) {
        return false;
    }
    // Argument 0 ist der Unterbefehl ("MOVE"), Argument 1 der Ort.
    if (alt->args[1].kind != Arg::Kind::Vector) {
        return false;
    }
    float xyz[3] = {0.0F, 0.0F, 0.0F};
    if (std::sscanf(alt->args[1].text.c_str(), "%f %f %f", &xyz[0], &xyz[1],
                    &xyz[2]) != 3) {
        return false;
    }
    Node neu = *alt;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.3f %.3f %.3f",
                  static_cast<double>(xyz[0] + g_app->gizmoOffset[0]),
                  static_cast<double>(xyz[1] + g_app->gizmoOffset[1]),
                  static_cast<double>(xyz[2] + g_app->gizmoOffset[2]));
    neu.args[1].text = buf;
    if (!g_app->doc.replaceAt(p, neu)) {
        return false;
    }
    for (int k = 0; k < 3; ++k) {
        g_app->gizmoOffset[k] = 0.0F;
    }
    rebuildTree();
    // Dasselbe wie beim Drehen: dorthin springen, wo der neue Ort erreicht
    // ist. Ein camera(MOVE, ...) faehrt ueber eine Dauer; am Anfang steht
    // die Kamera noch am alten Platz.
    return true;
}

bool confirmQuit() {
    // Schon beantwortet? Dann nicht nochmal fragen.
    //
    // Der Beenden-Knopf fragt selbst und laesst danach WM_CLOSE schicken;
    // ohne diese Abfrage kaeme derselbe Dialog ein zweites Mal.
    if (g_app->quitConfirmed) {
        return true;
    }
    // Ist gerade eine Frage offen, nicht noch eine anstossen.
    if (g_app->askSaveOpen) {
        return false;
    }
    // ALLE Reiter pruefen, nicht nur den aktiven.
    //
    // Mit mehreren offenen Skripten reicht der aktive nicht: die anderen
    // liegen geparkt daneben und haben ihren eigenen
    // Rueckgaengig-Speicher. Wer sie beim Beenden uebergeht, verliert genau
    // die Arbeit, um die es hier geht.
    //
    // Vorgegangen wird von vorn nach hinten: jeder ungesicherte Reiter wird
    // zuerst SICHTBAR gemacht und dann gefragt. Man soll sehen, worueber
    // man entscheidet.
    // Den ersten ungesicherten suchen.
    //
    // Keine Schleife mit Frage darin: das Fenster spannt sich ueber mehrere
    // Bilder, eine Schleife koennte gar nicht warten. Stattdessen wird
    // EINER gefragt, und was danach geschieht, ist "nochmal von vorn
    // suchen". So arbeitet sich die Kette ab, ein Reiter je Antwort.
    for (std::size_t i = g_app->quitAskFrom; i < g_app->tabs.size(); ++i) {
        const bool aktiv = (static_cast<int>(i) == g_app->activeTab);
        const bool schmutzig =
            aktiv ? g_app->doc.dirty() : g_app->tabs[i].doc.dirty();
        if (!schmutzig) {
            continue;
        }
        if (!aktiv) {
            switchTab(static_cast<int>(i));
        }
        // Diesen Reiter als beantwortet vormerken, BEVOR gefragt wird.
        //
        // "Nein" heisst wegwerfen - es macht das Dokument aber nicht
        // sauber. Ohne diese Marke faende die naechste Runde denselben
        // Reiter wieder und fragte erneut, endlos. Genau das war der
        // Fehler: "Clicking No when asked to save before closing never
        // closes the program."
        g_app->quitAskFrom = i + 1U;
        withUnsaved([] {
            // Beantwortet - jetzt der naechste. Ist keiner mehr da, ist
            // confirmQuit() beim naechsten Mal sofort fertig.
            if (confirmQuit()) {
                g_app->quitConfirmed = true;
                g_app->wantQuit = true;
            }
        },
        [] {
            // Abgebrochen: die Kette faengt beim naechsten Versuch von
            // vorn an. Sonst uebersaehe sie die schon gefragten Reiter.
            g_app->quitAskFrom = 0;
        });
        return false;
    }
    // "Exit?" - nur mit "Yes/No query on Open/New/Exit" (Original), und nur,
    // wenn eben keine Speichernfrage kam: wer die beantwortet hat, hat sich
    // schon entschieden.
    if (g_app->settings.queryOnDiscard && g_app->quitAskFrom == 0) {
        if (!g_app->frageOffen) {
            frage(tr(Str::AskExit), [] {
                g_app->quitConfirmed = true;
                g_app->wantQuit = true;
            });
        }
        return false;
    }
    return true;
}

// Was soll oben im Fenster stehen?
//
// Bisher stand dort immer "BehavEd". Wer zwei Fenster offen hat, sieht dann
// nicht, welches welches ist - genau so gemeldet: "Sometimes I have multiple
// scripts open so I need to see which script I am working on."
//
// Aufbau wie in jedem Editor: Dateiname, ein Stern bei ungesicherten
// Aenderungen, dann der Programmname. Der Dateiname zuerst, weil die
// Taskleiste von links abschneidet.
std::string windowTitle() {
    std::string t;
    if (!g_app->path.empty()) {
        t = fileName(g_app->path);
    } else if (!g_app->shownName.empty()) {
        t = g_app->shownName;
    } else {
        t = "unnamed.txt";
    }
    if (g_app->doc.dirty()) {
        t += " *";
    }
    t += " - BehavEd";
    return t;
}

bool wantsQuit() {
    // Einmal abfragen und zuruecksetzen: sonst schliesst das Fenster in
    // jedem folgenden Bild erneut, und ein abgebrochener Speichern-Dialog
    // wuerde nichts nuetzen.
    const bool q = g_app->wantQuit;
    g_app->wantQuit = false;
    return q;
}

void draw() {
    if (g_app == nullptr) { return; }
    // Einmal: Reste eines Updates wegraeumen, und im Hintergrund nachsehen.
    static bool updateGestartet = false;
    if (!updateGestartet) {
        updateGestartet = true;
        updater::beimStart();
    }

    // --- Den Klangstrom nachfuellen ------------------------------------
    //
    // Gemeldet: "der Sound ging ganz kurz, und danach reisst es ab."
    //
    // Das war der fehlende Teil von rc335. Der Mischer schreibt zwei
    // Bloecke im Voraus - rund 92 ms - und danach muss jemand nachlegen.
    // `update()` wurde aber NUR aus `play()` gerufen. Solange kein neuer
    // Klang begann, lief die Ausgabe leer, und der Rest fiel weg.
    //
    // Ein durchlaufender Strom braucht einen durchlaufenden Antrieb.
    // Hier ist er: einmal je Bild, unabhaengig davon, ob gerade etwas
    // gestartet wird.
    //
    // Beide Geraete, nicht nur eines - Musik laeuft ueber ein eigenes.
    g_app->audio.update();
    g_app->musicAudio.update();

    // Hier ist der erste Ort, an dem ImGui sicher einen Kontext hat.
    // createApp() und loadSettings() laufen VOR CreateContext(); ein
    // ImGui::GetStyle() dort loest "No current context" aus und das
    // Programm bricht beim Start ab, bevor ein Fenster erscheint.
    ensureIcons();

    // Zustand der Dialoge festhalten, BEVOR einer davon zeichnet und sich
    // dabei selbst schliesst. Begruendung steht bei der Feldbeschreibung
    // in app_internal.h.
    // Find getrennt: dort gehen Rueckgaengig/Wiederholen durch (Ersetzen).
    g_app->findAtFrameStart = g_app->findOpen;
    g_app->dialogAtFrameStart = g_app->editorOpen ||
                                g_app->askSaveOpen ||
                                g_app->prefsOpen || g_app->pk3Open ||
                                g_app->aboutOpen || g_app->missionPickOpen;

    // Strg+Mausrad vergroessert und verkleinert, wie in jedem Editor.
    if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0F &&
        !ImGui::GetIO().WantTextInput) {
        g_app->uiScale = std::clamp(
            g_app->uiScale + ImGui::GetIO().MouseWheel * 0.1F, 0.5F, 3.0F);
    }

    const float total = g_app->dpiScale * g_app->uiScale;
    if (g_app->themePending || std::fabs(total - g_app->appliedScale) > 0.001F) {
        applyScale(total);
        g_app->themePending = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    // Titel wie im Original: "unnamed.txt *"
    std::string title = g_app->path.empty() ? std::string("unnamed.txt")
                                            : fileName(g_app->path);
    if (g_app->doc.dirty()) {
        title += " *";
    }
    title += "###main";   // ImGui-Kennung stabil halten
    ImGui::Begin(title.c_str(), nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                 // Das Hauptfenster rollt nie. shank: "wenn ich vor und
                 // zurueck scrolle bewegen sich die Tabs" - der Inhalt war in
                 // der Kartenansicht 14 Punkte zu hoch (siehe computeLayout),
                 // und das Mausrad rollte die ganze Oberflaeche.
                 ImGuiWindowFlags_NoScrollWithMouse |
                 // --- Nie VOR die schwebenden Fenster ------------------
                 //
                 // Gemeldet: "If I click outside the window, then it
                 // closes/hides, and I am unable to open it again" - bei
                 // Notes, Keyboard shortcuts, Find und der Rueckfrage.
                 // Ein Klick auf das Hauptfenster holte es nach VORN; das
                 // schwebende Fenster lag dahinter, sein Offen-Merker
                 // stand weiter, und der Knopf setzte nur, was schon
                 // gesetzt war. Beim Einstellungsfenster sperrte der
                 // Merker obendrein alle Tastenkuerzel ("Reset all to
                 // defaults breaks all keyboard shortcuts").
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    drawMenu();
    // --- Die Reiterleiste, ganz oben --------------------------------------
    //
    // Ueber Events, Script Flow und Actions - so, wie du es beschrieben
    // hast: "tabs sollten überhalb von Event und scripts und so
    // untergebracht sein".
    //
    // ImGui bringt dafuer BeginTabBar mit. Verwendet werden drei Merkmale:
    //   Reorderable   die Reiter lassen sich mit der Maus umsortieren
    //   AutoSelectNew ein neu geoeffnetes Skript ist gleich vorn
    //   FittingPolicyScroll  bei vielen Reitern wird gerollt, nicht
    //                        gequetscht - Namen bleiben lesbar
    //
    // Der Stern hinter dem Namen ist derselbe wie im Fenstertitel.
    if (!g_app->tabs.empty()) {
        if (ImGui::BeginTabBar("##skripte",
                               ImGuiTabBarFlags_Reorderable |
                                   ImGuiTabBarFlags_AutoSelectNewTabs |
                                   ImGuiTabBarFlags_FittingPolicyScroll |
                                   ImGuiTabBarFlags_TabListPopupButton)) {
            int wechselZu = -1;
            int schliesse = -1;
            // --- ImGuis eigene Auswahl dem Programm nachfuehren -------------
            //
            // ImGui merkt sich selbst, welcher Reiter gewaehlt ist, und
            // meldet ihn jedes Bild. Wechselt das PROGRAMM den Reiter (drei
            // Skripte auf einmal geoeffnet, ein Reiter geschlossen, Fokus
            // aus einem Feld der Aufteilung), steht ImGui noch auf dem alten
            // - und die Meldung unten zog das Programm dorthin zurueck:
            // "ploetzlich in einem anderen Tab" (Selbsttest beim Nachstellen
            // von shanks Datenverlust, 02.10.). Deshalb: weicht ImGuis Wahl
            // vom Besitzer ab, bekommt dessen Reiter SetSelected, und in
            // diesem Bild ist ImGuis Meldung kein Klick.
            static int imguiWahl = -1;
            const bool nachfuehren = (imguiWahl != g_app->homeTab);
            int gemeldet = -1;
            for (std::size_t i = 0; i < g_app->tabs.size(); ++i) {
                // Fuer NAME und Zustand gilt weiter der aktive Reiter: nur
                // er hat das lebende Dokument. Fuer die AUSWAHL im Band
                // gilt der Besitzer - siehe unten.
                const bool aktiv = (static_cast<int>(i) == g_app->activeTab);
                const std::string& tp =
                    aktiv ? g_app->path : g_app->tabs[i].path;
                const std::string& tn =
                    aktiv ? g_app->shownName : g_app->tabs[i].shownName;
                const bool schmutzig =
                    aktiv ? g_app->doc.dirty() : g_app->tabs[i].doc.dirty();
                std::string name = !tp.empty()  ? fileName(tp)
                                   : !tn.empty() ? tn
                                                 : std::string("unnamed.txt");
                if (schmutzig) {
                    name += " *";
                }
                // "###" haengt die Kennung an der Nummer fest: sonst
                // wandert sie mit dem Namen, und ImGui verliert beim
                // Speichern die Zuordnung.
                name += "###tab" + std::to_string(i);
                bool offen = true;
                const ImGuiTabItemFlags fahnenReiter =
                    (nachfuehren && static_cast<int>(i) == g_app->homeTab) ? ImGuiTabItemFlags_SetSelected
                                                                           : ImGuiTabItemFlags_None;
                if (ImGui::BeginTabItem(name.c_str(), &offen, fahnenReiter)) {
                    gemeldet = static_cast<int>(i);
                    // Verglichen wird mit dem BESITZER, nicht mit dem
                    // aktiven Reiter.
                    //
                    // ImGui meldet JEDES BILD, welcher Reiter ausgewaehlt
                    // ist. Stand hier "aktiv", dann zog das Band den
                    // aktiven Reiter sofort zurueck, sobald ein Klick in
                    // ein Vergleichsfeld ihn verschoben hatte - und man
                    // landete "ploetzlich in einem anderen Tab".
                    //
                    // Der Besitzer aendert sich nur, wenn man wirklich
                    // einen Reiter anklickt. Damit bleibt das Band ruhig,
                    // waehrend man in den Feldern arbeitet.
                    if (static_cast<int>(i) != g_app->homeTab && !nachfuehren) {
                        wechselZu = static_cast<int>(i);
                    }
                    ImGui::EndTabItem();
                }
                if (!offen) {
                    schliesse = static_cast<int>(i);
                }
            }
            // Ein Pluszeichen zum Anlegen - wie im Browser.
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing |
                                              ImGuiTabItemFlags_NoTooltip)) {
                // Ohne Karte - ein neues, leeres Blatt.
                addTab(false);
                takeTab(g_app->activeTab);
            }
            ImGui::EndTabBar();
            imguiWahl = gemeldet;
            // ERST nach EndTabBar handeln - ein Umschalten mitten in der
            // Leiste zieht ImGui den Boden unter den Fuessen weg.
            if (schliesse >= 0) {
                closeTab(schliesse);
            } else if (wechselZu >= 0) {
                // ERST wechseln, DANN den Besitzer setzen.
                //
                // Andersherum war es falsch: switchTab parkt die aktuelle
                // Aufteilung in den Besitzer - und wenn der schon der NEUE
                // war, bekam der frische Reiter die vier Felder
                // angehaengt, die er nie hatte. Danach holte takeTab genau
                // die wieder hervor. Gemeldet als "wenn ich die tabs
                // wechsel, habe ich immer noch die 4er ansicht".
                //
                // In dieser Reihenfolge stimmt beides: geparkt wird in den
                // ALTEN Besitzer, geholt wird die Aufteilung des neuen.
                switchTab(wechselZu);
                g_app->homeTab = wechselZu;
            }
        }
    }

    const Layout l = computeLayout();

    // --- Was gilt gerade? -------------------------------------------------
    //
    // Reiter, Felder und Groessen ins Detailprotokoll, aber nur bei einer
    // AENDERUNG. Damit laesst sich nachvollziehen, was beim Oeffnen mehrerer
    // Skripte und beim Ziehen der Trennlinien wirklich geschieht - statt es
    // aus Bildschirmfotos zu erschliessen.
    {
        App::LayoutLog jetzt;
        jetzt.tabs = static_cast<int>(g_app->tabs.size());
        jetzt.activeTab = g_app->activeTab;
        jetzt.splitCount = g_app->splitCount;
        jetzt.focusPane = g_app->focusPane;
        for (int i = 0; i < App::kMaxSplit; ++i) {
            jetzt.paneTabs[i] = g_app->splitPanes[i].tab;
        }
        jetzt.frameW = static_cast<int>(l.total);
        jetzt.frameH = static_cast<int>(l.topH);
        jetzt.eventsW = static_cast<int>(l.eventsW);
        jetzt.flowW = static_cast<int>(l.flowW);
        jetzt.toolH = static_cast<int>(l.toolH);
        jetzt.tooSmall = l.tooSmall;
        jetzt.fracX = static_cast<int>(g_app->splitFracX * 1000.0F);
        jetzt.fracY = static_cast<int>(g_app->splitFracY * 1000.0F);
        jetzt.tlFrac = static_cast<int>(g_app->timelineFrac * 1000.0F);

        const App::LayoutLog& alt = g_app->lastLayoutLog;
        bool reiterAnders =
            jetzt.tabs != alt.tabs || jetzt.activeTab != alt.activeTab ||
            jetzt.splitCount != alt.splitCount ||
            jetzt.focusPane != alt.focusPane;
        for (int i = 0; i < App::kMaxSplit; ++i) {
            if (jetzt.paneTabs[i] != alt.paneTabs[i]) { reiterAnders = true; }
        }
        // Groessen erst ab EINEM Bildpunkt Unterschied - beim Ziehen aendern
        // sie sich sonst in jedem Bild um Bruchteile, und das Protokoll
        // liefe voll.
        const bool groessenAnders =
            jetzt.frameW != alt.frameW || jetzt.frameH != alt.frameH ||
            jetzt.eventsW != alt.eventsW || jetzt.flowW != alt.flowW ||
            jetzt.toolH != alt.toolH || jetzt.tooSmall != alt.tooSmall ||
            jetzt.fracX != alt.fracX ||
            jetzt.fracY != alt.fracY || jetzt.tlFrac != alt.tlFrac;

        if (reiterAnders) {
            char z[300];
            std::snprintf(z, sizeof(z),
                          "Reiter: %d offen, Arbeitsbereich %d, bearbeitet "
                          "wird %d | Felder: %d, Fokus %d, belegt mit "
                          "%d %d %d %d",
                          jetzt.tabs, g_app->homeTab, jetzt.activeTab,
                          jetzt.splitCount,
                          jetzt.focusPane, jetzt.paneTabs[0], jetzt.paneTabs[1],
                          jetzt.paneTabs[2], jetzt.paneTabs[3]);
            diag::detail(z);
            // Und was in den Feldern steht - der haeufigste Zweifel ist,
            // ob zwei Felder dasselbe Skript zeigen.
            for (int i = 0; i < jetzt.splitCount; ++i) {
                const int nr = g_app->splitPanes[i].tab;
                const char* name =
                    (nr >= 0 && static_cast<std::size_t>(nr) <
                                    g_app->tabs.size())
                        ? (g_app->tabs[static_cast<std::size_t>(nr)]
                                   .shownName.empty()
                               ? "(ohne Namen)"
                               : g_app->tabs[static_cast<std::size_t>(nr)]
                                     .shownName.c_str())
                        : "(kein Reiter)";
                std::snprintf(z, sizeof(z), "   Feld %d zeigt Reiter %d: %s", i,
                              nr, name);
                diag::detail(z);
            }
        }
        // Den Moduswechsel EINMAL je Bild feststellen - hier, weil diese
        // Stelle in jedem Modus laeuft.
        g_app->moduswechselBild = (g_app->letzterModus != g_app->leftMode);
        // Die Zeitleiste laeuft nur in der Kartenansicht weiter. Wer sie
        // beim Abspielen verlaesst, hatte eine stehende Uhr und weiter
        // laufenden Ton - also anhalten, wie "Stop" (Kartentest 27.09.).
        if (g_app->moduswechselBild && g_app->letzterModus == 1 && g_app->playing) {
            g_app->playing = false;
            g_app->audio.stopAll();
            g_app->laufendeSchleifen.clear();
            g_app->musicAudio.stopAll();
        }
        if (g_app->moduswechselBild) {
            char zm[140];
            std::snprintf(zm, sizeof(zm),
                          "Moduswechsel %d -> %d: dieses Bild wird "
                          "uebersprungen (Einschwingen)",
                          g_app->letzterModus, g_app->leftMode);
            diag::detail(zm);
            g_app->letzterModus = g_app->leftMode;
        }
        if (groessenAnders || g_app->dumpAngefordert) {
            g_app->dumpAngefordert = false;
            char z[300];
            std::snprintf(z, sizeof(z),
                          "Groessen: Fenster %dx%d | Ereignisse %d, Skripte "
                          "%d, Band %d | Raster %d/%d, Zeitleiste %d (Promille)",
                          jetzt.frameW, jetzt.frameH, jetzt.eventsW,
                          jetzt.flowW, jetzt.toolH, jetzt.fracX, jetzt.fracY,
                          jetzt.tlFrac);
            diag::detail(z);

            // --- ALLES, was wir je gemessen haben ------------------------
            //
            // shank: "wenn ich dir sage debuggen, dann will ich, dass er es
            // in eines der Logs schreibt. Also adde alles, wo ich dir mal
            // gesagt habe debugge es ins Log. Jede Position des UI,
            // Funktionen, alles."
            //
            // Bis hierher landete nur die Zeile darueber im Protokoll. Alles
            // andere, was in dieser Sitzung gemessen wurde, stand auf dem
            // Bildschirm - den ich nicht sehe. Jede dieser Zahlen hat uns
            // mindestens eine Runde gekostet:
            //
            //   splitEvents/Map/Model  drei Modi, drei Werte (rc494)
            //   frameH negativ         "alles Uebrige minus Fuss" (rc471)
            //   buttonsW/minButtonsW   Actions-Spalte (rc474, rc481)
            //   headerH je Modus       Knopfhoehe gegen Textzeile (rc478)
            //   ZelleW/restW/BildW     die Luecke neben der Karte (rc500)
            //   modeBarW               Modusleiste in der Tabelle (rc501)
            //
            // Auch nur bei Aenderung - `groessenAnders` deckt das ab.
            char z2[400];
            std::snprintf(z2, sizeof(z2),
                          "  Aufteilung: Modus %d | splitEvents %.4f, "
                          "splitMap %.4f, splitModel %.4f, splitButtons %.4f",
                          g_app->leftMode,
                          static_cast<double>(g_app->settings.splitEvents),
                          static_cast<double>(g_app->settings.splitMap),
                          static_cast<double>(g_app->settings.splitModel),
                          static_cast<double>(g_app->settings.splitButtons));
            diag::detail(z2);
            std::snprintf(z2, sizeof(z2),
                          "  Hoehen: headerH %.0f, fussH %.0f, frameH %.0f "
                          "(negativ = Rest minus Fuss), statusH %.0f, "
                          "toolH %.0f",
                          static_cast<double>(l.headerH),
                          static_cast<double>(l.fussH),
                          static_cast<double>(l.frameH),
                          static_cast<double>(l.statusH),
                          static_cast<double>(l.toolH));
            diag::detail(z2);
            std::snprintf(z2, sizeof(z2),
                          "  Breiten: buttonsW %.0f, minButtonsW %.0f, "
                          "modeBarW %.0f | Karte: ZelleW %.0f, restW %.0f, "
                          "BildW %.0f, SeitenspalteW %.0f",
                          static_cast<double>(l.buttonsW),
                          static_cast<double>(l.minButtonsW),
                          static_cast<double>(g_app->modeBarW),
                          static_cast<double>(g_app->dbgZelleW),
                          static_cast<double>(g_app->dbgRestW),
                          static_cast<double>(g_app->dbgBildW),
                          static_cast<double>(g_app->mapSidebarW));
            diag::detail(z2);
            std::snprintf(z2, sizeof(z2),
                          "  Stil: Zeichenhoehe %.0f, FramePadding.x %.0f, "
                          "CellPadding.x %.0f, ItemSpacing.x %.0f, "
                          "WindowPadding.x %.0f",
                          static_cast<double>(ImGui::GetFontSize()),
                          static_cast<double>(ImGui::GetStyle().FramePadding.x),
                          static_cast<double>(ImGui::GetStyle().CellPadding.x),
                          static_cast<double>(ImGui::GetStyle().ItemSpacing.x),
                          static_cast<double>(
                              ImGui::GetStyle().WindowPadding.x));
            diag::detail(z2);
        }
        // --- Und die REGELN nachprüfen ------------------------------------
        //
        // Ein Protokoll sagt, was gilt - nicht, ob es richtig ist. Diese
        // Bedingungen muessen immer gelten; bricht eine, liegt ein Fehler
        // vor, und er soll sich selbst melden statt beim Anwender
        // aufzufallen.
        //
        // Alle vier stammen aus echten Fehlern dieser Sitzung.
        if (reiterAnders) {
            char z[300];
            // rc179: neue Felder bekamen immer Reiter 0.
            for (int i = 0; i < jetzt.splitCount; ++i) {
                for (int j = i + 1; j < jetzt.splitCount; ++j) {
                    if (g_app->splitPanes[i].tab == g_app->splitPanes[j].tab) {
                        std::snprintf(z, sizeof(z),
                                      "   FEHLER: Feld %d und %d zeigen "
                                      "denselben Reiter %d",
                                      i, j, g_app->splitPanes[i].tab);
                        diag::detail(z);
                    }
                }
            }
            // rc184: nach dem Schliessen zeigte ein Feld ins Leere.
            for (int i = 0; i < jetzt.splitCount; ++i) {
                const int nr = g_app->splitPanes[i].tab;
                if (nr < 0 || static_cast<std::size_t>(nr) >=
                                  g_app->tabs.size()) {
                    std::snprintf(z, sizeof(z),
                                  "   FEHLER: Feld %d zeigt auf Reiter %d, es "
                                  "gibt aber nur %d",
                                  i, nr, jetzt.tabs);
                    diag::detail(z);
                }
            }
            if (jetzt.focusPane < 0 || jetzt.focusPane >= jetzt.splitCount) {
                std::snprintf(z, sizeof(z),
                              "   FEHLER: Fokus liegt auf Feld %d, es gibt "
                              "aber nur %d",
                              jetzt.focusPane, jetzt.splitCount);
                diag::detail(z);
            }
            // rc184: mehr Felder als Skripte.
            if (jetzt.splitCount > jetzt.tabs) {
                std::snprintf(z, sizeof(z),
                              "   FEHLER: %d Felder bei nur %d Reitern",
                              jetzt.splitCount, jetzt.tabs);
                diag::detail(z);
            }
        }
        if (groessenAnders) {
            char z[300];
            // rc202: die Skriptspalte klemmte, bevor die Symbolansicht
            // erreichbar war. Null waere schlimmer - dann kaeme man nicht
            // zurueck.
            if (jetzt.flowW <= 0 || jetzt.eventsW <= 0) {
                std::snprintf(z, sizeof(z),
                              "   FEHLER: eine Spalte ist auf null "
                              "(Ereignisse %d, Skripte %d)",
                              jetzt.eventsW, jetzt.flowW);
                diag::detail(z);
            }
            // rc195: das Band darf das Fenster nicht ueberragen.
            //
            // Die Bedingung stand frueher als `toolH > frameH + toolH` da -
            // algebraisch dasselbe wie `frameH < 0`, nur schwerer zu lesen.
            // Und sie kann seit rc258 nicht mehr anschlagen, weil frameH
            // bei null abgefangen wird. Gefragt wird deshalb den Vermerk,
            // den die Berechnung selbst hinterlaesst.
            if (jetzt.tooSmall) {
                diag::detail("   Fenster zu klein: nach Statuszeile, Band, "
                             "Reiterleiste und Kopfzeile bleibt nichts "
                             "uebrig. Die Hoehen sind bei null abgefangen.");
            }
        }
        if (reiterAnders || groessenAnders) {
            g_app->lastLayoutLog = jetzt;
        }

        // --- Wohin ging die letzte BEARBEITUNG? ---------------------------
        //
        // Beim Arbeiten an vier Skripten nebeneinander ist das DIE Frage:
        // landet die Aenderung in dem Skript, in das ich geklickt habe?
        //
        // Die Tiefe des Rueckgaengig-Stapels ist der verlaessliche Anzeiger:
        // sie waechst genau dann, wenn wirklich etwas geaendert wurde - und
        // nicht schon beim Anklicken oder Rollen.
        //
        // Beim REITERWECHSEL wechselt auch der Stapel; dann wird nur der
        // neue Stand gemerkt, ohne eine Bearbeitung zu melden.
        {
            const std::size_t tiefe = g_app->doc.undoDepth();
            if (g_app->activeTab != g_app->lastEditTab) {
                g_app->lastEditTab = g_app->activeTab;
                g_app->lastUndoDepth = tiefe;
            } else if (tiefe != g_app->lastUndoDepth) {
                const bool mehr = tiefe > g_app->lastUndoDepth;
                g_app->lastUndoDepth = tiefe;
                const std::string name =
                    !g_app->path.empty() ? fileName(g_app->path)
                    : !g_app->shownName.empty() ? g_app->shownName
                                                : std::string("unnamed.txt");
                char z[300];
                std::snprintf(z, sizeof(z),
                              "Bearbeitung: Feld %d, Reiter %d (%s), Stapel "
                              "%zu (%s)",
                              g_app->focusPane, g_app->activeTab, name.c_str(),
                              tiefe, mehr ? "Schritt dazu"
                                          : "rueckgaengig oder verworfen");
                diag::detail(z);

                // Die Regel, die das Ganze traegt: das FOKUSSIERTE Feld muss
                // den aktiven Reiter zeigen. Gilt sie nicht, ginge eine
                // Aenderung in ein anderes Skript als das angeklickte - und
                // das waere der schlimmste Fehler, den dieses Fenster haben
                // kann.
                const int fp =
                    std::clamp(g_app->focusPane, 0, App::kMaxSplit - 1);
                if (g_app->splitCount > 1 &&
                    g_app->splitPanes[fp].tab != g_app->activeTab) {
                    std::snprintf(z, sizeof(z),
                                  "   FEHLER: Feld %d zeigt Reiter %d, "
                                  "bearbeitet wurde aber Reiter %d",
                                  fp, g_app->splitPanes[fp].tab,
                                  g_app->activeTab);
                    diag::detail(z);
                }
            }
        }
    }
    // Bei der KARTE eine eigene Aufteilung.
    //
    // Die Knopfspalte "Actions" gehoert zum Skript, nicht zur Karte - dort
    // steht sie nur im Weg. Stattdessen:
    //
    //     Modusleiste | Ebenen | Karte | Skriptbaum
    //
    // Die Ebenen links, weil man sie liest wie eine Gliederung; der
    // Skriptbaum rechts, weil er beim Anklicken einer Entity die Antwort
    // gibt. Alles andere zur Karte steht im Band darunter.
    // --- Die Spalten als TABELLE ----------------------------------------
    //
    // Vorher: `drawEventsList`, Teiler, `drawFlowArea`, Teiler,
    // `drawButtonColumns` - aneinandergereiht mit `SameLine(0, 0)`, jede
    // Breite von Hand gerechnet, jeder Teiler selbst gezeichnet.
    //
    // Drei Runden sind an der Frage gescheitert, warum die Spalten nicht
    // gleich hoch enden (rc469, rc470, rc471). Der Grund war nie ein
    // einzelner Zahlendreher, sondern dass ueberhaupt gerechnet wurde:
    // sechs Werte aus einem Dutzend Summanden, und jede Aenderung muss an
    // jeder Stelle nachgezogen werden.
    //
    // ImGui hat dafuer Tabellen (seit 1.80). Sie rechnen die Breiten,
    // bringen die Trennlinien samt Ziehen mit - und eine Tabellenzeile ist
    // EINE Zeile: alle Zellen enden dort, wo sie endet. Nicht "sollten
    // gleich hoch sein", sondern koennen gar nicht anders.
    //
    // Was damit wegfaellt: `l.eventsW`, `l.flowW`, `l.buttonsW`, `l.rest`,
    // die beiden `splitter()`-Aufrufe und die gespeicherten
    // Aufteilungsbruechte - ImGui merkt sich die Spaltenbreiten selbst.
    //
    // Bei der KARTE hat die Knopfspalte nichts zu suchen: sie gehoert zum
    // Skript. Dann sind es zwei Spalten statt drei.
    {
        const bool mitKnoepfen = (g_app->leftMode != 1);
        const int spalten = mitKnoepfen ? 3 : 2;
        // --- KEINE gespeicherten Spaltenbreiten -------------------------
        //
        // ImGui legt Spaltenbreiten in der `imgui.ini` ab und stellt sie
        // beim naechsten Start wieder her. Das klingt hilfreich und ist
        // hier die Ursache eines Fehlers, den shank zweimal gesehen hat:
        //
        // Er hatte die Knopfspalte gezogen, als sie das noch zuliess. Die
        // Breite steht seither in seiner .ini. rc481 hat die Spalte auf
        // `NoResize` gesetzt - die gespeicherte Breite gilt trotzdem
        // weiter, und auf dem letzten Bild ist die Spalte auf sechzig
        // Punkte gequetscht: "Dele", "Dup", "Past".
        //
        // Die Recherche zu rc481 hatte genau das benannt: eine
        // veraenderbare Spalte "hands the column width to ImGui's table
        // state, which initialises once and then auto-fits or RESTORES FROM
        // SAVED SETTINGS".
        //
        // behaved speichert seine Aufteilung selbst, in den eigenen
        // Einstellungen. Eine zweite Fassung davon in der .ini ist genau
        // das Muster, das hier schon achtmal auseinandergelaufen ist.
        // Deshalb: nicht speichern.
        const ImGuiTableFlags fahnen =
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_NoPadOuterX |
            ImGuiTableFlags_NoSavedSettings;
        // Die Hoehe ist NEGATIV - "alles Uebrige minus Fuss", siehe
        // computeLayout. Die Tabelle erbt sie an alle Zellen.
        // --- Mindestbreiten, damit nichts verschwindet ------------------
        //
        // Mit `splitter()` sind auch dessen Klammern weggefallen. shank:
        // "ich kann Script Flow komplett ueber das Fenster links ziehen,
        // und Actions kann ich zu weit nach rechts und zu weit nach links
        // ziehen."
        //
        // ImGui klemmt Spalten auf `table->MinColumnWidth`, und das entsteht
        // in `BeginTable` aus `FramePadding.x` (imgui_tables.cpp:905). Die
        // Vorgabe sind rund vier Punkte - praktisch keine Grenze.
        //
        // Fuer FESTE Spalten gilt das Vierfache (Zeile 1047), fuer gedehnte
        // das Einfache (1093). Mit `GetFontSize() * 3` ergibt das am
        // kopflosen Pruefstand:
        //
        //     gedehnt   39   Events und Script Flow koennen nicht mehr
        //                    verschwinden
        //     fest     156   die Actions-Spalte bleibt breit genug fuer
        //                    "Treeview Options"
        //
        // Der Griff ueber `FramePadding` ist ein Umweg - ImGui bietet keine
        // Mindestbreite je Spalte an. Deshalb wird er sofort wieder
        // zurueckgenommen, damit er nicht auch fuer den Inhalt gilt.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2{ImGui::GetFontSize() * 3.0F,
                                   ImGui::GetStyle().FramePadding.y});
        // --- EINE TABELLEN-ID JE MODUS -------------------------------------
        //
        // shank: "Script Flow ist immer noch ueber alle 3 Instanzen
        // verbunden."
        //
        // Er hat recht, und rc494 hat nur die halbe Ursache behoben. Drei
        // eigene Einstellungswerte nuetzen nichts, solange ImGui die
        // Spaltenbreite woanders haelt:
        //
        //   `TableSetupColumn(label, flags, init_width_or_weight)`
        //
        // Der Parameter heisst INIT_width_or_weight. Er gilt beim ANLEGEN.
        // Danach steht die Breite im Tabellenzustand, und der haengt an der
        // Tabellen-ID:
        //
        //   imgui_tables.cpp:358   table = g.Tables.GetOrAddByKey(id);
        //
        // Eine ID, ein Zustand. Alle drei Modi benutzten "##spalten" und
        // teilten sich deshalb die gezogene Breite - egal, was in den
        // Einstellungen stand.
        //
        // Drei Anordnungen sind drei Tabellen. Genau das, was shank schon
        // in rc479 gesagt hat: "alle 3 Modi als separate UIs betrachten."
        const char* const modusId = (g_app->leftMode == 1)   ? "##spaltenMap"
                                    : (g_app->leftMode == 2) ? "##spaltenModel"
                                                             : "##spaltenEvents";

        // Die FENSTERBREITE gehoert in die Kennung der Tabelle.
        //
        // Warum: `TableSetupColumn` uebernimmt die vorgegebene Breite nur,
        // solange die Tabelle sich einrichtet - in ImGuis Quelltext steht
        // sie unter `if (table->IsInitializing)`. Danach gehoert die Breite
        // der Tabelle, und was wir ihr sagen, wird verworfen.
        //
        // Das erklaert shanks Protokoll bis auf den Punkt:
        //
        //   Zeichenhoehe 24 -> minEvents = 24 * 14        = 336
        //   erstes Bild, Fenster 1485: 1485 * 0.2094      = 311
        //                              auf die Untergrenze angehoben -> 336
        //   spaeter gemeldet: jetzt = 336 + 12 Polster    = 348
        //   und 348 / 2536                                = 0.1372
        //
        // Die Tabelle richtet sich also ein, WAEHREND das Fenster noch auf
        // seiner kleinen Startgroesse steht. Dort greift die Untergrenze,
        // und dieser Notwert bleibt fuer immer stehen. Beim naechsten Start
        // wird er als "so hat er gezogen" gespeichert - Runde um Runde
        // dasselbe.
        //
        // rc556 wartete zwei Bilder. Das half nicht und konnte nicht helfen:
        // es ist kein Nachhinken, sondern Eigentum.
        //
        // Aendert sich die Fensterbreite, aendert sich die Kennung, und die
        // Tabelle richtet sich neu ein - mit dem Anteil, den der Benutzer
        // eingestellt hat. Beim ZIEHEN aendert sich die Fensterbreite nicht,
        // die Kennung bleibt, und die Tabelle behaelt seinen Zug.
        //
        // `gesamtB` weiter unten ist dieselbe Zahl, steht hier aber noch
        // nicht - also an dieser Stelle selbst messen.
        char idPuffer[64];
        std::snprintf(idPuffer, sizeof(idPuffer), "%s#%d", modusId,
                      static_cast<int>(ImGui::GetContentRegionAvail().x));
        const char* const tabellenId = idPuffer;

        // KEINE Hoehe fuer die Tabelle.
        //
        // Gemessen: `outer_size.y` wirkt bei Tabellen nur mit `ScrollY`.
        // Ohne die Fahne wird eine negative Hoehe stillschweigend
        // ignoriert - die Reservierung fiel weg, und die Spalten liefen
        // sechs Punkte UNTER den Innenrand.
        //
        // Richtig ist: die Tabelle passt sich an, und die Kindfenster in
        // den Zellen halten den Fuss frei.
        const bool offen =
            ImGui::BeginTable(tabellenId, spalten, fahnen, ImVec2{0.0F, 0.0F});
        // Sofort zurueck: die groessere Polsterung galt nur fuer das Anlegen
        // der Tabelle, nicht fuer die Knoepfe darin.
        ImGui::PopStyleVar();
        if (offen) {
            // --- Die Gewichte haengen am MODUS --------------------------
            //
            // Meine erste Fassung nahm feste 0,22 und 0,58 - und die
            // Kartenansicht steckte damit in der schmalen Spalte, weil sie
            // in DERSELBEN Zelle liegt wie die Ereignisliste. Auf shanks
            // Bild ist die Karte nur noch ein Streifen.
            //
            // Die Aufteilung war schon immer eine andere je Modus
            // (`splitEvents` gegen `splitMap`, computeLayout). Die Zahlen
            // bleiben, sie sind jetzt nur Gewichte der Tabelle statt selbst
            // ausgerechneter Breiten.
            // JE MODUS ein eigener Wert.
            //
            // Vorher `leftMode != 0 ? splitMap : splitEvents` - Map und
            // Model teilten sich einen. Wer im einen zog, verstellte den
            // anderen.
            const float linksAnteil =
                (g_app->leftMode == 1)   ? g_app->settings.splitMap
                : (g_app->leftMode == 2) ? g_app->settings.splitModel
                                         : g_app->settings.splitEvents;
            const float gesamtB = ImGui::GetContentRegionAvail().x;
            // Die vorige Breite MERKEN, nicht gleich ueberschreiben - sonst
            // ist der Vergleich unten immer wahr und die Pruefung wertlos.
            const float vorigeB = g_app->letzteFensterB;
            // --- FEST vorn, GEDEHNT in der Mitte, FEST hinten ------------
            //
            // Die Anleitung zu den Groessenregeln (imgui_tables.cpp, Kopf):
            //
            //   "Fixed Columns will generally obtain their requested width
            //    (unless the table cannot fit them all). Stretch Columns
            //    will share the remaining width. [...] The typical use of
            //    mixing sizing policies is: any number of LEADING Fixed
            //    columns, followed by one or two TRAILING Stretch columns."
            //
            // Eine Mindestbreite fuer GEDEHNTE Spalten gibt es nicht - das
            // ist eine offene Anfrage im ImGui-Projekt (Issue #5685). Am
            // kopflosen Pruefstand nachgemessen: bei 900 Punkten
            // Fensterbreite wurden die gedehnten auf 159 und 565
            // zusammengequetscht, waehrend die FESTE Spalte ihre 142 hielt.
            //
            // Also ist Events fest, und die Grenze setze ich selbst - beim
            // Uebergeben, nicht beim Ziehen. Dann gilt sie auch, wenn das
            // Fenster schmaler wird.
            // --- Die Untergrenze haengt am MODUS -------------------------
            //
            // 14 Zeichenhoehen reichen fuer eine Ereignisliste. In den
            // Modi Map und Model steckt in derselben Spalte aber die ganze
            // Ansicht: Modusleiste, Ebenen, das Bild UND die
            // Einstellungsspalte rechts davon. Bei 14 passt davon nichts
            // hinein - auf shanks Bild fehlt die Karte einfach.
            //
            // Erschwerend: die Rueckkopplung aus rc484 hat den kleinen Wert
            // schon in seine Einstellungen geschrieben. rc490 hat das
            // Schrumpfen gestoppt, den gespeicherten Wert aber nicht
            // repariert. Eine Untergrenze beim BENUTZEN tut beides.
            const float minEvents = ImGui::GetFontSize() *
                                    ((g_app->leftMode == 0) ? 14.0F : 46.0F);
            // Der Script Flow behaelt mindestens so viel - sonst zieht man
            // ihn oder die Actions ueber die Nachbarspalte (shank: "das
            // Script-Flow-Fenster laesst sich komplett ueber das Fenster vom
            // Renderer ziehen").
            const float minFlow = ImGui::GetFontSize() * 10.0F;
            const float maxEvents =
                std::max(minEvents, gesamtB - l.buttonsW - minFlow);
            const float breiteEvents =
                std::clamp(gesamtB * linksAnteil, minEvents, maxEvents);
            // NoResize: gezogen wird ueber eigene Griffe (unten, nach
            // EndTable). ImGuis eigenes Ziehen verschiebt Breiten zwischen
            // festen und gedehnten Nachbarn und liess sich nicht zuverlaessig
            // begrenzen - im Test quetschte es Actions auf 97 statt 184 und
            // die Karte auf 0 (27.09.).
            ImGui::TableSetupColumn("Events",
                                    ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                                    breiteEvents);
            // Auch der Script Flow FEST, als Rest ausgerechnet (siehe unten):
            // bei gemischten festen und gedehnten Spalten verteilt ImGui den
            // Rest selbst und gab der linken Spalte 37 Punkte mehr als
            // gewuenscht - genau die fehlten dem Script Flow (27.09.).
            ImGui::TableSetupColumn("Flow",
                                    ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                                    std::max(minFlow, gesamtB - breiteEvents - (mitKnoepfen ? l.buttonsW : 0.0F) -
                                                          g_app->spaltenUeberhang));
            if (mitKnoepfen) {
                // Feste Breite, und zwar die GEWUENSCHTE, nicht die
                // kleinstmoegliche: `minButtonsW` ist die Untergrenze, bei
                // der die Beschriftungen gerade noch hineinpassen. Damit
                // waren "Treeview Options" und "Fold Macros" abgeschnitten.
                // --- Ziehbar, aber mit Klammern ------------------------
                //
                // rc481 hatte `NoResize` gesetzt. Das war zu grob: der
                // Griff zwischen Script Flow und Actions gehoert in ImGui
                // zur LINKEN Spalte, und die ist gedehnt. Wer ihn anfasst,
                // verschiebt deshalb den Teiler zwischen Events und Script
                // Flow - shank: "wenn ich den Actions-Slider waehle,
                // bewegt sich der linke Slider."
                //
                // Beides geht. Was die drei Fehler verursacht hat, war
                // nicht das Ziehen, sondern was daneben fehlte:
                //
                //   Untergrenze   `MinColumnWidth * 4` = 156, aus der
                //                 Polsterung (rc480) - die Spalte kann
                //                 nicht mehr verschwinden
                //   Nachbarn      ImGui haelt sie mindestens 39 breit
                //                 (imgui_tables.cpp:1571), solange
                //                 `NoKeepColumnsVisible` NICHT gesetzt ist
                //   Gedaechtnis   `NoSavedSettings` (rc482) - keine alte
                //                 Breite aus der .ini
                //
                // Eine echte Hoechstbreite bietet ImGui nicht an. Wer sie
                // braucht, muss den Griff selbst zeichnen und die Breite
                // selbst halten - das war frueher `splitter()`. Solange
                // die Nachbarn sichtbar bleiben, reicht die Klammer von
                // ImGui.
                ImGui::TableSetupColumn("Actions",
                                        ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                                        l.buttonsW);
            }
            // --- Die Breiten JEDES Bild aus den Anteilen, geklemmt ----------
            //
            // Actions: mindestens so breit, dass die Beschriftungen passen,
            // hoechstens 45 %. Links: Events 14, Karte/Model 46 Zeichen. Der
            // Script Flow behaelt in jedem Fall minFlow (plus die Zellraender,
            // sonst fehlen ihm genau die). Gezogen wird an eigenen Griffen,
            // die die Anteile aendern - siehe nach EndTable.
            {
                // Die Zellraender und Trennlinien, im letzten Bild GEMESSEN
                // (Gesamtbreite minus Summe der Spalten) - nicht geschaetzt.
                const float spiel = g_app->spaltenUeberhang;
                // --- Kein Teiler schiebt eine andere Spalte weg -------------
                //
                // shank, 28.09., mit Protokoll: "wenn die linke Spalte zu nah
                // an der rechten Actions ist, dann passiert das". Stand der
                // Script Flow schon auf seiner Mindestbreite, durfte Actions
                // trotzdem wachsen (ihre Grenze rechnete mit der MINDESTbreite
                // der linken Spalte) - und schob Events weg, ohne dass deren
                // Anteil sich aenderte. Wurde Actions wieder schmaler, sprang
                // Events von selbst zurueck. Der linke Teiler dagegen stiess an.
                //
                // Jetzt fuer beide gleich: die linke Spalte zuerst, geklemmt so,
                // dass Actions ihre Mindestbreite behaelt; dann Actions, geklemmt
                // an das, was neben der linken Spalte und dem Script Flow
                // wirklich frei ist. Beim Ziehen gilt fuer links dasselbe mit der
                // aktuellen Actions-Breite - wer den Script Flow auf seine
                // Mindestbreite drueckt, stoesst an, und die dritte Spalte bleibt
                // stehen.
                const float minB = mitKnoepfen ? l.minButtonsW : 0.0F;
                const float maxLAnordnung = std::max(minEvents, gesamtB - minB - minFlow - spiel);
                const float zielL = std::floor(std::clamp(gesamtB * linksAnteil, minEvents, maxLAnordnung));
                float zielB = 0.0F;
                if (mitKnoepfen) {
                    const float maxB = std::max(
                        l.minButtonsW, std::min(gesamtB * 0.45F, gesamtB - zielL - minFlow - spiel));
                    // Ganze Bildpunkte: ImGui rundet WidthGiven ab, und ein
                    // gebrochener Rest schwang ueber die gemessenen
                    // Zellraender von Bild zu Bild um einen Punkt hin und
                    // her (Ruhetest "Karte nach dem Laden", 28.09.).
                    zielB = std::floor(std::clamp(gesamtB * g_app->settings.splitButtons, l.minButtonsW, maxB));
                    g_app->spaltenMin[2] = l.minButtonsW;
                    g_app->spaltenMax[2] = maxB;
                }
                const float maxL = std::max(minEvents, gesamtB - zielB - minFlow - spiel);
                g_app->spaltenMin[0] = minEvents;
                g_app->spaltenMax[0] = maxL;
                g_app->spaltenMin[1] = minFlow;
                g_app->spaltenGesamt = gesamtB;
                // Erst ab dem zweiten Bild: vorher hat die Tabelle ihre
                // Mindestspaltenbreite noch nicht berechnet, und ImGui sichert
                // das zu (imgui_tables.cpp, TableSetColumnWidth).
                if (ImGuiTable* tabG = ImGui::GetCurrentTable(); tabG != nullptr && tabG->MinColumnWidth > 0.0F) {
                    // Der Script Flow ist der Rest - so fuellen die drei
                    // Spalten die Breite genau.
                    const float zielF = std::floor(std::max(minFlow, gesamtB - zielL - zielB - spiel));
                    // DIREKT setzen, nicht ueber TableSetColumnWidth: das
                    // klemmt jede Spalte auf WidthMax aus dem VORIGEN Bild.
                    // Wuchs Actions beim Ziehen des rechten Teilers, war ihr
                    // Hoechstwert noch der alte (der Script Flow war im
                    // letzten Bild breiter) - sie bekam weniger, der Script
                    // Flow schrumpfte voll, und rechts neben Actions blieb
                    // eine Luecke, die mit jeder Mausbewegung wuchs und
                    // schrumpfte (shank, 28.09.). Ausserdem kehrt es frueh
                    // zurueck, wenn die GEGEBENE Breite schon stimmt, und
                    // laesst dann einen falschen Wunsch stehen. Die drei
                    // Ziele sind oben geklemmt und fuellen die Breite genau;
                    // alle Spalten sind fest (WidthFixed), also nimmt
                    // TableUpdateLayout WidthRequest, wie es ist.
                    const float ziele[3] = {zielL, zielF, zielB};
                    for (int k = 0; k < spalten && k < 3; ++k) {
                        const float z = std::max(ziele[k], tabG->MinColumnWidth);
                        if (std::fabs(tabG->Columns[k].WidthRequest - z) > 0.5F) {
                            tabG->Columns[k].WidthRequest = z;
                            tabG->Columns[k].WidthAuto = z;
                        }
                    }
                    g_app->spaltenZiel[1] = zielF;
                    g_app->spaltenZiel[0] = zielL;
                    g_app->spaltenZiel[2] = zielB;
                    for (int k = 0; k < spalten && k < 3; ++k) {
                        g_app->spaltenReq[k] = tabG->Columns[k].WidthRequest;
                        g_app->spaltenGiven[k] = tabG->Columns[k].WidthGiven;
                    }
                }
            }
            ImGui::TableNextRow();
            // Was die Anordnung jetzt wirklich ist (fuer den Selbsttest), und
            // wie viel Zellraender und Trennlinien kosten.
            if (const ImGuiTable* tabI = ImGui::GetCurrentTable(); tabI != nullptr) {
                float summe = 0.0F;
                for (int k = 0; k < spalten; ++k) { summe += tabI->Columns[k].WidthGiven; }
                if (summe > 1.0F) {
                    // Erst bei einer Aenderung von mindestens ZWEI Punkten
                    // uebernehmen: sonst regelt die Messung ihre eigene
                    // Rundung nach, und die Actions-Knoepfe zappelten nach
                    // dem Kartenladen zwischen 281 und 282 Punkten Breite hin
                    // und her (Ruhetest "Karte nach dem Laden", 28.09. - der
                    // Fehler bestand schon vor dieser Nacht).
                    //
                    // NICHT "Gesamtbreite minus Summe": das misst eine Luecke
                    // rechts mit, und weil der Script Flow um genau diesen Wert
                    // schmaler gerechnet wird, bestaetigte sich eine einmal zu
                    // grosse Zahl jedes Bild selbst. Beim Ziehen des rechten
                    // Teilers wuchs sie so bis an die Klemme (6 Zeichen), alle
                    // Spalten rueckten nach links, und erst Minimieren setzte
                    // sie zurueck (shank, 28.09., mit Bildern). Gezaehlt wird
                    // jetzt nur, was die Spalten SELBST an Rand verbrauchen
                    // (Ausdehnung minus Inhalt), plus der linke Rahmen fuer
                    // beide Seiten.
                    const int letzte = spalten - 1;
                    float rand = 2.0F * std::max(0.0F, tabI->Columns[0].MinX - tabI->OuterRect.Min.x);
                    for (int k = 0; k < spalten; ++k) {
                        rand += (tabI->Columns[k].MaxX - tabI->Columns[k].MinX) - tabI->Columns[k].WidthGiven;
                    }
                    const float neu = std::clamp(std::round(rand), 0.0F, ImGui::GetFontSize() * 6.0F);
                    if (std::fabs(neu - g_app->spaltenUeberhang) >= 2.0F) {
                        g_app->spaltenUeberhang = neu;
                    }
                    // Fuer den Selbsttest: was rechts neben der letzten Spalte
                    // frei bleibt (soll nur der Rahmen sein).
                    g_app->spaltenLuecke = tabI->OuterRect.Max.x - tabI->Columns[letzte].MaxX;
                }
                const int n = mitKnoepfen ? 3 : 2;
                for (int k = 0; k < 3; ++k) {
                    g_app->spaltenB[k] = (k < n) ? tabI->Columns[k].MaxX - tabI->Columns[k].MinX : 0.0F;
                }
                g_app->teilerX[0] = tabI->Columns[0].MaxX;
                g_app->teilerX[1] = mitKnoepfen ? tabI->Columns[1].MaxX : 0.0F;
                g_app->teilerAnzahl = mitKnoepfen ? 2 : 1;
            }

            ImGui::TableNextColumn();
            // Wer gezogen hat, aendert die Spaltenbreite. Die ZELLE kennt
            // sie - ein oeffentliches `TableGetColumnWidth` gibt es nicht.
            //
            // Zurueckgelesen und als Anteil gesichert wird sie in jedem
            // Bild: "clamped on every read rather than only when dragged",
            // damit ein schmaleres Fenster die Spalte verkleinert statt die
            // Nachbarn leerzuraeumen.
            // --- Nur sichern, wenn sich die Breite WIRKLICH geaendert hat
            //
            // shanks Protokoll, zwei Starts hintereinander:
            //
            //   geladen:   Ereignisse 0.743, Karte 0.871
            //   gesichert: Ereignisse 0.404, Karte 0.900
            //
            // Beide verstellt, obwohl er nur angeschaut hat. 0.900 ist
            // exakt meine obere Klemmgrenze - der Wert ist dagegengelaufen.
            //
            // Zwei Fehler von mir:
            //
            //   rc490  `IsMouseDown` greift bei JEDEM Linksklick irgendwo im
            //          Fenster, nicht nur beim Ziehen des Teilers. Jeder
            //          Klick auf einen Knopf schrieb die Breite neu.
            //   rc490  `+ CellPadding.x * 2` korrigiert zu VIEL. Vorher
            //          driftete der Wert nach unten, jetzt nach oben. Ich
            //          habe die Richtung getauscht statt die Drift zu
            //          beenden.
            //
            // Die Drift endet erst, wenn gar nicht geschrieben wird, solange
            // sich nichts geaendert hat. Gezogen wurde genau dann, wenn die
            // Tabelle eine ANDERE Breite hat als die, die wir ihr vorgegeben
            // haben. Ohne Ziehen sind beide gleich, und dann bleibt der
            // Wert unangetastet - egal wie oft das Bild neu entsteht.
            {
                const float jetzt = ImGui::GetContentRegionAvail().x +
                                    ImGui::GetStyle().CellPadding.x * 2.0F;
                // Eine Zeichenhoehe Spielraum: Rundung und Polster sollen
                // nicht als Ziehen durchgehen.
                const float spielraum = ImGui::GetFontSize();
                // Und NICHT, waehrend sich das Fenster aendert.
                //
                // shanks Detailprotokoll:
                //
                //   Fenster 1485x929  | Ereignisse  511
                //   Fenster 2536x1326 | Ereignisse  928
                //   Fenster 2536x1056 | Ereignisse 2260
                //
                // Er hat das Fenster vergroessert, nicht den Teiler gezogen.
                // Eine feste Spalte behaelt dabei ihre Pixelbreite; der
                // Anteil aendert sich von allein, und mein Vergleich hielt
                // das fuer ein Ziehen.
                //
                // Antwort auf seine Frage "soll ich nicht ziehen?": doch.
                // Nur soll das FENSTER nichts verstellen.
                // ZWEI Bilder Ruhe, nicht eines.
                //
                // Gemessen in shanks rc555-Protokoll, unmittelbar nachdem
                // das Fenster von 1485 auf 2536 gewachsen ist:
                //
                //   Teiler geschrieben: jetzt 348, vorgegeben 572,
                //                       gesamt 2536, vorige 2536
                //
                //   vorgegeben  = 0.2256 * 2536 = 572
                //   jetzt       = 348, davon 12 Zellenpolster -> 336
                //   0.2256 * 1485 (die ALTE Breite)            -> 335
                //
                // Die Tabelle meldete also noch die Breite, die sie beim
                // schmalen Fenster hatte: eine ImGui-Tabelle uebernimmt
                // eine neue Spaltenbreite erst im NAECHSTEN Bild.
                //
                // Die Pruefung von rc490 sah nur ein Bild zurueck. Im Bild
                // nach der Aenderung galt das Fenster als stabil, die
                // Tabelle hinkte aber noch hinterher - und die Differenz
                // wurde als Ziehen gedeutet. Ergebnis:
                //
                //   Anordnung geladen:   Ereignisse 0.226
                //   Anordnung gesichert: Ereignisse 0.137
                //
                // Beim Ziehen aendert sich die Fensterbreite nicht, der
                // Zaehler laeuft also weiter hoch - echtes Ziehen bleibt
                // erkennbar.
                if (vorigeB > 0.0F && std::abs(gesamtB - vorigeB) < 1.0F) {
                    if (g_app->fensterStabilSeit < 1000) {
                        ++g_app->fensterStabilSeit;
                    }
                } else {
                    g_app->fensterStabilSeit = 0;
                }
                const bool fensterStabil = g_app->fensterStabilSeit >= 2;
                // UND: ImGui muss sagen, dass gezogen wird.
                //
                // rc499 hatte das schon einmal und es ist mir bei einem
                // spaeteren Umbau abhandengekommen. `lint_rueckkopplung`
                // haelt in seiner eigenen Begruendung fest, warum es die
                // einzige taugliche Schranke ist:
                //
                //   rc490  IsMouseDown          jeder Klick zaehlte
                //   rc497  Breitenvergleich     Fenstergroesse zaehlt mit
                //   rc498  Fenster stabil       der Rest ratscht weiter
                //
                // Alle drei haben VERSUCHT zu erkennen, ob gezogen wurde.
                // Keine hat gefragt. `ResizedColumn` ist die Antwort:
                // wird nicht gezogen, steht dort -1.
                const ImGuiTable* tab = ImGui::GetCurrentTable();
                const bool wirdGezogen =
                    (tab != nullptr) && (tab->ResizedColumn != -1);
                if (gesamtB > 0.0F && fensterStabil && wirdGezogen &&
                    std::abs(jetzt - breiteEvents) > spielraum) {
                    const float anteil =
                        std::clamp(jetzt / gesamtB, 0.05F, 0.9F);
                    if (g_app->leftMode == 1) {
                        g_app->settings.splitMap = anteil;
                    } else if (g_app->leftMode == 2) {
                        g_app->settings.splitModel = anteil;
                    } else {
                        g_app->settings.splitEvents = anteil;
                    }
                    // WARUM hier geschrieben wurde.
                    //
                    // shanks rc554-Protokoll: der Anteil sprang bei
                    // GLEICHER Fensterbreite von 0.2094 auf 0.1372, ohne
                    // dass er den Teiler angefasst hat - und genau dieser
                    // Wert wurde beim Beenden gesichert:
                    //
                    //   Anordnung geladen:   Ereignisse 0.209
                    //   Anordnung gesichert: Ereignisse 0.137
                    //
                    // Die drei Schutzmassnahmen von rc490 greifen also
                    // nicht: das Fenster war stabil, und die Tabelle meldete
                    // trotzdem eine andere Breite als vorgegeben. Woher der
                    // Unterschied kommt, sagt keine Zahl im Protokoll - also
                    // steht sie jetzt drin.
                    char zt[220];
                    std::snprintf(zt, sizeof(zt),
                                  "Teiler geschrieben: Modus %d, ruhig seit "
                                  "%d Bildern, jetzt %.0f, "
                                  "vorgegeben %.0f, gesamt %.0f, vorige "
                                  "Fensterbreite %.0f -> Anteil %.4f",
                                  g_app->leftMode,
                                  g_app->fensterStabilSeit,
                                  static_cast<double>(jetzt),
                                  static_cast<double>(breiteEvents),
                                  static_cast<double>(gesamtB),
                                  static_cast<double>(vorigeB),
                                  static_cast<double>(anteil));
                    diag::detail(zt);
                }
                g_app->letzteFensterB = gesamtB;
            }
            drawEventsList(l);

            ImGui::TableNextColumn();
            drawFlowArea(l);

            if (mitKnoepfen) {
                ImGui::TableNextColumn();
                // Und die Actions-Spalte GENAUSO zuruecklesen.
                //
                // shank zu rc557: "also die linke Seite events wird
                // gespeichert, nur die rechte Seite bei actions wird nicht
                // gespeichert." Sein Protokoll bestaetigt beides:
                //
                //   Anordnung geladen:   Ereignisse 0.210
                //   Anordnung gesichert: Ereignisse 0.210     <- haelt
                //
                // und in der gesicherten Zeile taucht `splitButtons` gar
                // nicht auf.
                //
                // Der Grund: `settings.splitButtons` wird geschrieben,
                // gelesen und BENUTZT - aber nirgends aus einem Zug
                // zurueckgeschrieben. Die Spalte ist ziehbar, der Zug
                // landete nur nie in den Einstellungen. Die linke Seite
                // hatte diesen Weg seit rc490, die rechte nie.
                //
                // Dieselben drei Schutzpruefungen wie links: nur bei
                // ruhigem Fenster, nur bei echtem Unterschied, und mit
                // Klammern.
                {
                    const float jetztK = ImGui::GetContentRegionAvail().x +
                                         ImGui::GetStyle().CellPadding.x * 2.0F;
                    const float spielraumK = ImGui::GetFontSize();
                    // `fensterStabil` steht in einem Block, der weiter
                    // oben schon geschlossen hat - hier ist es nicht mehr
                    // sichtbar. Der Zaehler selbst schon: er gehoert zum
                    // Programmzustand.
                    // Dieselbe Frage wie links: zieht jemand gerade?
                    const ImGuiTable* tabK = ImGui::GetCurrentTable();
                    const bool zugK =
                        (tabK != nullptr) && (tabK->ResizedColumn != -1);
                    if (gesamtB > 0.0F && g_app->fensterStabilSeit >= 2 &&
                        zugK &&
                        std::abs(jetztK - l.buttonsW) > spielraumK) {
                        g_app->settings.splitButtons =
                            std::clamp(jetztK / gesamtB, 0.03F, 0.60F);
                        char zk[200];
                        std::snprintf(zk, sizeof(zk),
                                      "Actions geschrieben: ruhig seit %d "
                                      "Bildern, jetzt %.0f, vorgegeben %.0f, "
                                      "gesamt %.0f -> Anteil %.4f",
                                      g_app->fensterStabilSeit,
                                      static_cast<double>(jetztK),
                                      static_cast<double>(l.buttonsW),
                                      static_cast<double>(gesamtB),
                                      static_cast<double>(
                                          g_app->settings.splitButtons));
                        diag::detail(zk);
                    }
                }
                drawButtonColumns(l);
            }
            ImGui::EndTable();
            // Die GANZE Tabelle: erst nach EndTable steht ihre Hoehe fest -
            // vorher (nach TableNextRow) war es nur die Kopfzeile.
            g_app->tabelleOben = ImGui::GetItemRectMin().y;
            g_app->tabelleUnten = ImGui::GetItemRectMax().y;
            g_app->teilerY = (g_app->tabelleOben + g_app->tabelleUnten) * 0.5F;
            // --- Die eigenen Griffe auf den Trennlinien --------------------
            //
            // Wie die Kante der Zeitleiste: ein unsichtbarer Knopf auf der
            // Linie, der beim Ziehen den ANTEIL verschiebt. Die Klemmung
            // oben sorgt dafuer, dass keine Spalte unter ihre Grenze geht -
            // man stoesst an, statt ueber die Nachbarspalte zu ziehen.
            if (g_app->spaltenGesamt > 1.0F && g_app->tabelleUnten > g_app->tabelleOben) {
                const ImVec2 zurueck = ImGui::GetCursorScreenPos();
                const float griffB = std::max(4.0F, ImGui::GetStyle().ItemSpacing.x);
                const float gB = g_app->spaltenGesamt;
                for (int k = 0; k < g_app->teilerAnzahl; ++k) {
                    ImGui::SetCursorScreenPos(ImVec2{g_app->teilerX[k] - griffB, g_app->tabelleOben});
                    ImGui::InvisibleButton(k == 0 ? "##spalteLinks" : "##spalteRechts",
                                           ImVec2{griffB * 2.0F, g_app->tabelleUnten - g_app->tabelleOben});
                    const bool aktiv = ImGui::IsItemActive();
                    if (ImGui::IsItemActivated()) {
                        diag::detail(std::string("Teiler ") + (k == 0 ? "links" : "rechts") + " gepackt bei " +
                                     std::to_string(g_app->teilerX[k]));
                        g_app->teilerStartMaus = ImGui::GetIO().MousePos.x;
                        g_app->teilerStartBreite = g_app->spaltenZiel[k == 0 ? 0 : 2];
                    }
                    if (ImGui::IsItemHovered() || aktiv) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                        ImGui::GetWindowDrawList()->AddLine(
                            ImVec2{g_app->teilerX[k], g_app->tabelleOben},
                            ImVec2{g_app->teilerX[k], g_app->tabelleUnten},
                            ImGui::GetColorU32(aktiv ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered), 2.0F);
                    }
                    if (aktiv) {
                        // ABSOLUT: Breite beim Anfassen plus der Weg der Maus
                        // seitdem. Vorher wurde jedes Bild die GEMESSENE
                        // Breite (samt Zellraendern) plus MouseDelta
                        // genommen - die Raender kamen so in jedem Bild
                        // dazu, die Spalte wuchs auch bei ruhiger Maus bis an
                        // die Grenze, und der Teiler lief der Maus davon
                        // (shank: "es wird entweder zu klein oder zu gross").
                        // Jetzt bleibt der Teiler unter der Maus, und an
                        // einer Grenze stoesst er an, bis die Maus zurueckkommt.
                        const float weg = ImGui::GetIO().MousePos.x - g_app->teilerStartMaus;
                        if (k == 0) {
                            float& anteil = (g_app->leftMode == 1)   ? g_app->settings.splitMap
                                            : (g_app->leftMode == 2) ? g_app->settings.splitModel
                                                                     : g_app->settings.splitEvents;
                            const float w = std::clamp(g_app->teilerStartBreite + weg, g_app->spaltenMin[0],
                                                       std::max(g_app->spaltenMin[0], g_app->spaltenMax[0]));
                            // +0.5: zielL rundet ab (std::floor), sonst fehlte
                            // bei manchen Breiten ein Punkt.
                            anteil = (w + 0.5F) / gB;
                        } else {
                            const float w = std::clamp(g_app->teilerStartBreite - weg, g_app->spaltenMin[2],
                                                       std::max(g_app->spaltenMin[2], g_app->spaltenMax[2]));
                            g_app->settings.splitButtons = (w + 0.5F) / gB;
                        }
                    }
                }
                ImGui::SetCursorScreenPos(zurueck);
            }
        }
    }

    // Die Bedienleiste UNTER allen drei Spalten, ueber die volle Breite.
    if (l.toolH > 0.0F) {
        // OHNE eigenen Hintergrund: ein Kasten mit eigener Farbe sieht wie
        // ein zusaetzlicher dunkler Balken aus. Die Bedienung soll auf der
        // Flaeche stehen, die ohnehin da ist.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0, 0, 0, 0});
        // KEINE Rollbalken: das Band hat eine feste Hoehe, und ein Balken
        // wuerde sie von innen auffressen. Was nicht hineinpasst, wird
        // abgeschnitten - das faellt auf und laesst sich beheben, ein
        // stiller Hoehenverlust nicht.
        // --- Die ZIEHKANTE ueber dem Band --------------------------------
        //
        // Seit rc173 stellte man die Hoehe der Zeitleiste ueber einen
        // Prozentregler ein. Das ist umstaendlich: in Blender zieht man die
        // Kante zwischen zwei Bereichen, und genau das erwartet man hier
        // auch.
        //
        // VOR dem Kindfenster gezeichnet, nicht danach: der Griff liegt
        // ueber dem Band, und ein Kindfenster faengt Klicks in seinem
        // Bereich ab. Dieselbe Reihenfolge-Frage wie bei den Trennlinien
        // des Rasters in rc179 - dort war es genau andersherum, weil der
        // Griff DARIN lag.
        if (g_app->leftMode == 1) {
            const float kanteH =
                std::max(4.0F, ImGui::GetStyle().ItemSpacing.y);
            ImGui::InvisibleButton("##tlkante", ImVec2{-FLT_MIN, kanteH});
            g_app->tlKanteX = (ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5F;
            g_app->tlKanteY = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5F;
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            }
            if (ImGui::IsItemActivated()) {
                g_app->tlKanteStartMaus = ImGui::GetIO().MousePos.y;
                g_app->tlKanteStartHoehe = g_app->tlHoeheIst;
            }
            if (ImGui::IsItemActive() && g_app->tlPlatz > 1.0F) {
                // ABSOLUT: Hoehe beim Anfassen plus Weg der Maus, geteilt
                // durch denselben Platz, aus dem die Hoehe entsteht. Vorher
                // MouseDelta durch die FENSTERhoehe - die Kante lief langsamer
                // als die Maus, und unter der Mindesthoehe sammelte sich ein
                // Rest an, den man erst zurueckziehen musste.
                // Nach OBEN ziehen macht die Leiste GROESSER.
                const float weg = ImGui::GetIO().MousePos.y - g_app->tlKanteStartMaus;
                const float hoehe = g_app->tlKanteStartHoehe - weg;
                g_app->timelineFrac = std::clamp(hoehe / g_app->tlPlatz, 0.08F, 0.60F);
            }
            // Eine sichtbare Linie: einen Griff, den man nicht sieht,
            // findet niemand (siehe rc193, das Lineal).
            const ImVec2 kA = ImGui::GetItemRectMin();
            const ImVec2 kB = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2{kA.x, (kA.y + kB.y) * 0.5F - 1.0F},
                ImVec2{kB.x, (kA.y + kB.y) * 0.5F + 1.0F},
                ImGui::GetColorU32(ImGui::IsItemHovered() || ImGui::IsItemActive()
                                       ? ImGuiCol_SliderGrabActive
                                       : ImGuiCol_Separator));
        }
        ImGui::BeginChild("toolband", ImVec2{-FLT_MIN, l.toolH},
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
        if (g_app->leftMode == 2) {
            drawModelPanel();
        } else if (g_app->leftMode == 1) {
            // Alles zur Karte im Band, ueber die volle Breite: Bedienung,
            // die angeklickte Entity und die Zeitleiste.
            //
            // Die Ebenenliste steht NICHT hier, sondern als eigene Spalte
            // links neben der Karte - sie ist eine Gliederung und gehoert
            // neben das Bild, nicht darunter.
            drawMapPanel();
            drawPickedEntity();
            drawTimeline();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // --- Die Aufteilung in Zahlen ---------------------------------------
    //
    // shank: "das mit dem Scaling nervt einfach so, kannst du Debug
    // einbauen fuer das Menue auch?"
    //
    // Zwoelf Runden lang habe ich Breiten geraten, er hat gebaut, und wir
    // haben Bilder verglichen. `tools/layoutcheck.cpp` misst das inzwischen
    // hier - aber nur den Aufbau, den es nachstellt, und nicht seine
    // Einstellungen auf seinem Bildschirm.
    //
    // Das hier zeigt DIESELBEN Zahlen im laufenden Programm. Wenn wieder
    // etwas klemmt, steht die Antwort da, statt dass wir sie erraten.
    if (g_app->layoutOpen) {
        if (ImGui::Begin(tr(Str::DebugLayoutTitle), &g_app->layoutOpen,
                         ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings)) {
            const ImGuiStyle& st = ImGui::GetStyle();
            // Nur Namen und Zahlen - Prosa gehoert uebersetzt, und ein
            // Debug-Auszug soll ablesbar sein, nicht erklaerend.
            ImGui::Text("leftMode         %d", g_app->leftMode);
            ImGui::Text("Fenster          %.0f x %.0f", ImGui::GetIO().DisplaySize.x,
                        ImGui::GetIO().DisplaySize.y);
            ImGui::Separator();
            ImGui::Text("splitEvents      %.4f", static_cast<double>(
                                                     g_app->settings.splitEvents));
            ImGui::Text("splitMap         %.4f",
                        static_cast<double>(g_app->settings.splitMap));
            ImGui::Text("splitModel       %.4f",
                        static_cast<double>(g_app->settings.splitModel));
            ImGui::Separator();
            ImGui::Text("ZelleW           %.0f",
                        static_cast<double>(g_app->dbgZelleW));
            ImGui::Text("restW            %.0f",
                        static_cast<double>(g_app->dbgRestW));
            ImGui::Text("BildW            %.0f",
                        static_cast<double>(g_app->dbgBildW));
            ImGui::Text("mapSidebarW      %.0f",
                        static_cast<double>(g_app->mapSidebarW));
            ImGui::Separator();
            // Ereignisfenster, Spalte 0. Die Differenz ist die Antwort:
            // 0 = beide Zeilen enden gleich.
            ImGui::Text("ed ZelleW        %.0f",
                        static_cast<double>(g_app->dbgZelle0));
            ImGui::Text("ed knopfB        %.0f",
                        static_cast<double>(g_app->dbgKnopfB));
            ImGui::Text("ed knopfW        %.0f",
                        static_cast<double>(g_app->dbgKnopfW));
            ImGui::Text("ed FeldEnde      %.0f",
                        static_cast<double>(g_app->dbgFeldEnde));
            ImGui::Text("ed HelferEnde    %.0f",
                        static_cast<double>(g_app->dbgHelferEnde));
            ImGui::Text("ed Differenz     %.0f",
                        static_cast<double>(g_app->dbgHelferEnde -
                                            g_app->dbgFeldEnde));
            ImGui::Text("combo MinBreite  %.0f",
                        static_cast<double>(g_app->dbgComboMinB));
            ImGui::Text("combo ListenH    %.0f",
                        static_cast<double>(g_app->dbgComboListenH));
            ImGui::Text("splitButtons     %.4f", static_cast<double>(
                                                     g_app->settings.splitButtons));
            ImGui::Separator();
            ImGui::Text("buttonsW         %.0f", static_cast<double>(l.buttonsW));
            ImGui::Text("minButtonsW      %.0f",
                        static_cast<double>(l.minButtonsW));
            ImGui::Text("headerH          %.0f", static_cast<double>(l.headerH));
            ImGui::Text("fussH            %.0f", static_cast<double>(l.fussH));
            ImGui::Text("frameH           %.0f", static_cast<double>(l.frameH));
            ImGui::Text("statusH          %.0f", static_cast<double>(l.statusH));
            ImGui::Separator();
            ImGui::Text("minEvents        %.0f",
                        static_cast<double>(
                            ImGui::GetFontSize() *
                            ((g_app->leftMode == 0) ? 14.0F : 46.0F)));
            ImGui::Text("FramePadding.x   %.0f",
                        static_cast<double>(st.FramePadding.x));
            ImGui::Text("CellPadding.x    %.0f",
                        static_cast<double>(st.CellPadding.x));
            ImGui::Text("Zeichenhoehe     %.0f",
                        static_cast<double>(ImGui::GetFontSize()));

        }
        ImGui::End();
    }

    drawStatus(l);
    drawUndoListe();
    updater::zeichneFenster();
    drawMessages();
    drawGamePaths();
    drawInterplay();
    drawKeyBindings();

    // Die Speichernfrage hat VORRANG - und zwar allein.
    //
    // Jedes dieser Fenster ruft `ImGui::OpenPopup` in JEDEM Bild. ImGui
    // haelt nur EIN Klappfenster je Ebene: wer dort etwas anderes
    // aufmacht, schliesst das vorherige. Standen zwei davon gleichzeitig
    // offen, machte jedes Bild abwechselnd das eine auf und das andere zu.
    // Keines lebte lang genug, um einen Klick anzunehmen.
    //
    // shank zu rc544: "wenn ich ein kleines Fenster offen habe, also zum
    // beispiel event und druecke rechts oben auf das programm x um es zu
    // schliessen, schliesst sich der event editor aber das grosse fenster
    // bleibt offen und ich kann nichts mehr anklicken und auch das
    // hauptfenster nicht mehr schliessen. ist nur noch mit taskmanager
    // schliessbar."
    //
    // Genau so: `confirmQuit()` gibt false zurueck, solange `askSaveOpen`
    // steht ("Ist gerade eine Frage offen, nicht noch eine anstossen"), und
    // die Frage laesst sich nicht beantworten, weil der Ereigniseditor sie
    // im naechsten Bild wieder zudrueckt. WM_CLOSE wird also fuer immer
    // abgelehnt.
    //
    // Deshalb: liegt die Speichernfrage an, wird sonst nichts gezeichnet.
    // Sie ist die einzige Frage, deren Antwort das Programm braucht.
    if (g_app->askSaveOpen) {
        drawAskSave();
    } else if (g_app->frageOffen) {
        // Dieselbe Regel: eine Frage zur Zeit, sonst schliessen sich die
        // Klappfenster gegenseitig.
        drawFrage();
    } else {
        drawEditor();
        drawFind();
        drawPrefs();
        drawPk3Browser();
        drawMissionPicker();
        drawAbout();
    }
    selbsttest();   // nur mit BHED_EDITORTEST, siehe selbsttest.cpp
    handleShortcuts();
    handleTreeKeys();
    ImGui::End();
}

// Startgroesse des Fensters in Pixeln, aus der Ressource.
void defaultWindowSize(int* w, int* h) {
    *w = static_cast<int>(kDialogW * kDluX);
    *h = static_cast<int>(kDialogH * kDluY);
}

FeldKnopf feldKnopf(const Command& c, std::size_t feld) {
    const bool fuehrend =
        !c.params.empty() && c.params[0].kind == Param::Kind::TypeSet;
    if (fuehrend) {
        return feld == 0 ? FeldKnopf::Keiner : FeldKnopf::Helfer;
    }
    return FeldKnopf::Expr;
}

bool auswahlMitFeldern(const CommandDb& db, const std::string& typeset) {
    const TypeSet* ts = db.typeset(typeset);
    if (ts == nullptr) { return false; }
    for (const TypeEntry& e : ts->entries) {
        if (!e.params.empty()) { return true; }
    }
    return false;
}

namespace {
bool sucheKennung(const std::vector<Node>& ns, Kennung k, Path& weg) {
    for (std::size_t i = 0; i < ns.size(); ++i) {
        weg.push_back(i);
        if (ns[i].kennung == k || sucheKennung(ns[i].children, k, weg)) {
            return true;
        }
        weg.pop_back();
    }
    return false;
}
}  // namespace

std::vector<Kennung> kennungenVon(const std::vector<Path>& wege) {
    g_app->doc.vergibKennungen();
    std::vector<Kennung> ks;
    for (const Path& p : wege) {
        const Node* n = nodeAt(g_app->doc.script(), p);
        if (n != nullptr && n->kennung != 0) { ks.push_back(n->kennung); }
    }
    return ks;
}

void markiereKennungen(const std::vector<Kennung>& ks) {
    g_app->doc.vergibKennungen();
    std::vector<Path> neu;
    for (const Kennung k : ks) {
        Path weg;
        if (sucheKennung(g_app->doc.script().nodes, k, weg)) {
            neu.push_back(weg);
            // Der Block, in dem es jetzt liegt, muss offen sein - sonst ist
            // die Markierung unsichtbar.
            Path eltern = weg;
            while (eltern.size() > 1) {
                eltern.pop_back();
                const Node* e = nodeAt(g_app->doc.script(), eltern);
                if (e != nullptr && e->kennung != 0) {
                    g_app->expanded.setOpen(e->kennung, true);
                }
            }
        }
    }
    g_app->selection = neu;
    g_app->selectedPath = neu.empty() ? Path{} : neu.front();
    // Bewusst KEIN scrollToSelected: abgelegt wird, wo man hinschaut, und
    // die Ansicht soll stehen bleiben (shank: "The view should not scroll").
}

void reiterSchliessen(int index) { closeTab(index); }
bool einzelnenSchrittZuruecknehmen(std::size_t i, std::string* bericht) { return schrittEinzelnZurueck(i, bericht); }

// --- Lesezeichen und Aenderungsrand --------------------------------------
//
// shank, 27.09.: "be able to mark a command/line in the script ... Like in
// notepad++" und "Having the highlight on the side could also be nice to
// have to show what was changed between saves and the current session."
//
// Vorbild ist Notepad++ (Handbuch, "Searching > Bookmarks" und "Editing >
// Change History"):
//   Lesezeichen   Klick in den Rand oder Strg+F2; F2 / Umschalt+F2 springen
//                 zum naechsten / vorigen, am Ende wieder von vorn.
//   Rand          orange = seit dem letzten Speichern geaendert,
//                 gruen  = in dieser Sitzung geaendert und gespeichert,
//                 blassblau = nach dem Speichern per Rueckgaengig wieder wie
//                 beim Oeffnen. Nach dem Laden ist nichts markiert.
namespace {

// Der eigene Text eines Knotens - ohne Kinder, mit allem, was man sieht.
std::string eigenerText(const Node& n) {
    std::string t = std::to_string(static_cast<int>(n.kind)) + "|" + n.name + "|" + n.raw + "|";
    for (const Arg& x : n.args) {
        t += std::to_string(static_cast<int>(x.kind)) + x.typeset + ":" + x.text +
             (x.locked ? "!" : "") + "\x1f";
    }
    if (n.hasBlock) { t += "{"; }
    return t;
}

void knotenListe(const std::vector<Node>& ns, std::vector<std::pair<Kennung, std::string>>& out) {
    for (const Node& n : ns) {
        if (n.kind != Node::Kind::Blank) {
            out.emplace_back(n.kennung, eigenerText(n));
        }
        knotenListe(n.children, out);
    }
}

std::vector<std::pair<Kennung, std::string>> knotenListe(const Script& s) {
    std::vector<std::pair<Kennung, std::string>> v;
    knotenListe(s.nodes, v);
    return v;
}

// Welche Knoten sind gegenueber `stand` anders? Neu, im Text geaendert oder
// VERSCHOBEN. Verschoben heisst: nicht in der laengsten Folge, die in beiden
// Staenden in derselben Reihenfolge steht - wie ein Zeilenvergleich, der
// eine verschobene Zeile als geaendert zeigt und die anderen nicht. Die
// Kennungen sind eindeutig, also genuegt die laengste steigende Teilfolge
// der alten Positionen (O(n log n)).
std::set<Kennung> geaendertGegen(const std::vector<std::pair<Kennung, std::string>>& stand,
                                 const std::vector<std::pair<Kennung, std::string>>& jetzt) {
    std::map<Kennung, std::size_t> wo;
    for (std::size_t i = 0; i < stand.size(); ++i) { wo[stand[i].first] = i; }
    std::set<Kennung> anders;
    std::vector<std::size_t> idx;    // Index in `jetzt`
    std::vector<std::size_t> pos;    // Position in `stand`
    for (std::size_t j = 0; j < jetzt.size(); ++j) {
        const Kennung k = jetzt[j].first;
        const auto it = wo.find(k);
        if (k == 0 || it == wo.end() || stand[it->second].second != jetzt[j].second) {
            anders.insert(k);
            continue;
        }
        idx.push_back(j);
        pos.push_back(it->second);
    }
    // Laengste steigende Teilfolge von `pos`, mit Rueckverfolgung.
    std::vector<std::size_t> ende;          // Index in pos des Endes einer Folge der Laenge l+1
    std::vector<std::ptrdiff_t> vor(pos.size(), -1);
    for (std::size_t i = 0; i < pos.size(); ++i) {
        std::size_t lo = 0;
        std::size_t hi = ende.size();
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (pos[ende[mid]] < pos[i]) { lo = mid + 1; } else { hi = mid; }
        }
        if (lo > 0) { vor[i] = static_cast<std::ptrdiff_t>(ende[lo - 1]); }
        if (lo == ende.size()) { ende.push_back(i); } else { ende[lo] = i; }
    }
    std::vector<bool> drin(pos.size(), false);
    if (!ende.empty()) {
        for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(ende.back()); i >= 0; i = vor[static_cast<std::size_t>(i)]) {
            drin[static_cast<std::size_t>(i)] = true;
        }
    }
    for (std::size_t i = 0; i < pos.size(); ++i) {
        if (!drin[i]) { anders.insert(jetzt[idx[i]].first); }
    }
    return anders;
}

}  // namespace

// Die Befehle beim Oeffnen, je Kennung, ohne Blockinhalt.
void originaleMerken(const std::vector<Node>& ns, std::map<Kennung, Node>& out) {
    for (const Node& n : ns) {
        if (n.kind != Node::Kind::Blank && n.kennung != 0) {
            Node k = n;
            k.children.clear();
            out[n.kennung] = std::move(k);
        }
        originaleMerken(n.children, out);
    }
}

void aenderungsStandNeu() {
    const int uid = tabKennung(g_app->activeTab);
    if (uid < 0) { return; }
    g_app->doc.vergibKennungen();
    auto& st = g_app->aenderungen[uid];
    st.geladen = knotenListe(g_app->doc.script());
    st.gespeichert = st.geladen;
    st.original.clear();
    originaleMerken(g_app->doc.script().nodes, st.original);
}

// "Revert to original": welche der gewaehlten Befehle sind INHALTLICH anders
// als beim Oeffnen, und womit werden sie ersetzt? Nicht dabei: neue Befehle
// (es gab kein Original), nur verschobene (ihr Inhalt ist gleich),
// Makrozeilen (ihre Zahl haengt an den Befehlen dahinter) und Zeilen, deren
// Art sich geaendert hat (REM - dafuer gibt es den REM-Knopf).
std::vector<std::pair<Path, Node>> zuruecksetzbar(const std::vector<Path>& wege) {
    std::vector<std::pair<Path, Node>> out;
    const int uid = tabKennung(g_app->activeTab);
    const auto it = g_app->aenderungen.find(uid);
    if (uid < 0 || it == g_app->aenderungen.end()) {
        return out;
    }
    g_app->doc.vergibKennungen();
    for (const Path& p : wege) {
        const Node* n = nodeAt(g_app->doc.script(), p);
        if (n == nullptr || n->kennung == 0 || n->kind == Node::Kind::Macro || n->kind == Node::Kind::Blank) {
            continue;
        }
        const auto o = it->second.original.find(n->kennung);
        if (o == it->second.original.end() || o->second.kind != n->kind || o->second.hasBlock != n->hasBlock) {
            continue;
        }
        if (eigenerText(o->second) != eigenerText(*n)) {
            out.emplace_back(p, o->second);
        }
    }
    return out;
}

std::string originalZeile(const Path& weg, bool* neu) {
    if (neu != nullptr) { *neu = false; }
    const int uid = tabKennung(g_app->activeTab);
    const auto it = g_app->aenderungen.find(uid);
    if (uid < 0 || it == g_app->aenderungen.end()) {
        return {};
    }
    g_app->doc.vergibKennungen();
    const Node* n = nodeAt(g_app->doc.script(), weg);
    if (n == nullptr || n->kennung == 0 || n->kind == Node::Kind::Macro || n->kind == Node::Kind::Blank) {
        return {};
    }
    const auto o = it->second.original.find(n->kennung);
    if (o == it->second.original.end()) {
        if (neu != nullptr) { *neu = true; }
        return {};
    }
    const std::vector<std::pair<Path, Node>> z = zuruecksetzbar({weg});
    return z.empty() ? std::string() : rowText(z.front().second, g_app->treeOpt);
}

std::string originalKurz(const std::vector<std::pair<Path, Node>>& zurueck) {
    if (zurueck.empty()) {
        return {};
    }
    std::string zeile = rowText(zurueck.front().second, g_app->treeOpt);
    // Sehr lange Zeilen (ein langer print-Text) kuerzen - das Menue soll
    // nicht ueber den Bildschirm wachsen.
    constexpr std::size_t kMax = 70;
    if (zeile.size() > kMax) {
        zeile = zeile.substr(0, kMax) + "...";
    }
    if (zurueck.size() > 1) {
        zeile += "  (+" + std::to_string(zurueck.size() - 1) + ")";
    }
    return zeile;
}

// shank: "I think it would look better if it were on one line" - grau
// "Original:", gleich daneben der Befehl.
void originalVorschau(const std::vector<std::pair<Path, Node>>& zurueck) {
    ++g_app->vorschauGezeichnet;
    ImGui::TextDisabled("%s", tr(Str::RevertPreviewLabel));
    ImGui::SameLine();
    ImGui::TextUnformatted(originalKurz(zurueck).c_str());
}

void aenderungenGespeichert() {
    const int uid = tabKennung(g_app->activeTab);
    if (uid < 0) { return; }
    g_app->doc.vergibKennungen();
    auto it = g_app->aenderungen.find(uid);
    if (it == g_app->aenderungen.end()) {
        aenderungsStandNeu();
        return;
    }
    it->second.gespeichert = knotenListe(g_app->doc.script());
    // Gleich neu rechnen - sonst bleibt orange stehen, bis zur naechsten
    // Aenderung (der Selbsttest hat es gefunden).
    g_app->zeilenMarke = zeilenMarken(g_app->activeTab, g_app->doc.script(), g_app->rows);
    for (auto& pn : g_app->splitPanes) { pn.dirty = true; }
}

std::vector<std::uint8_t> zeilenMarken(int tab, const Script& s, const std::vector<Row>& rows) {
    std::vector<std::uint8_t> m(rows.size(), kMarkeKeine);
    const int uid = tabKennung(tab);
    if (uid < 0 || !g_app->settings.changeHistory) { return m; }
    const std::vector<std::pair<Kennung, std::string>> jetzt = knotenListe(s);
    auto it = g_app->aenderungen.find(uid);
    if (it == g_app->aenderungen.end()) {
        // Zum ersten Mal gesehen: das ist der Stand beim Oeffnen.
        auto& st = g_app->aenderungen[uid];
        st.geladen = jetzt;
        st.gespeichert = jetzt;
        originaleMerken(s.nodes, st.original);
        return m;
    }
    const std::set<Kennung> gegenGeladen = geaendertGegen(it->second.geladen, jetzt);
    const std::set<Kennung> gegenGespeichert = geaendertGegen(it->second.gespeichert, jetzt);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Kennung k = rows[i].kennung;
        if (k == 0) { continue; }
        const bool l = gegenGeladen.count(k) != 0;
        const bool sp = gegenGespeichert.count(k) != 0;
        m[i] = (l && sp) ? kMarkeGeaendert
             : l         ? kMarkeGespeichert
             : sp        ? kMarkeZurueck
                         : kMarkeKeine;
    }
    return m;
}

bool hatLesezeichen(int tab, Kennung k) {
    const auto it = g_app->lesezeichen.find(tabKennung(tab));
    return it != g_app->lesezeichen.end() && it->second.count(k) != 0;
}

void lesezeichenUmschalten(const std::vector<Path>& wege) {
    const int uid = tabKennung(g_app->activeTab);
    if (uid < 0) { return; }
    g_app->doc.vergibKennungen();
    auto& lz = g_app->lesezeichen[uid];
    for (const Path& p : wege) {
        const Node* n = nodeAt(g_app->doc.script(), p);
        if (n == nullptr || n->kennung == 0) { continue; }
        if (lz.erase(n->kennung) == 0) { lz.insert(n->kennung); }
    }
    // Ein unveraendertes Dokument IST die Datei - dann gleich merken, sonst
    // beim naechsten Speichern (die Zeilennummern gelten fuer die Datei).
    if (!g_app->path.empty() && !g_app->doc.dirty()) {
        lesezeichenMerken();
    }
}

void lesezeichenSpringen(bool vorwaerts) {
    const int uid = tabKennung(g_app->activeTab);
    const auto it = g_app->lesezeichen.find(uid);
    g_app->doc.vergibKennungen();
    // Die gesetzten Lesezeichen in Dateireihenfolge, mit ihren Wegen.
    std::vector<Path> ziele;
    std::function<void(const std::vector<Node>&, Path&)> lauf =
        [&](const std::vector<Node>& ns, Path& weg) {
            for (std::size_t i = 0; i < ns.size(); ++i) {
                weg.push_back(i);
                if (it != g_app->lesezeichen.end() && ns[i].kennung != 0 &&
                    it->second.count(ns[i].kennung) != 0) {
                    ziele.push_back(weg);
                }
                lauf(ns[i].children, weg);
                weg.pop_back();
            }
        };
    Path w;
    lauf(g_app->doc.script().nodes, w);
    if (ziele.empty()) {
        addStatus(tr(Str::MsgNoBookmarks));
        return;
    }
    // Vom aktuellen Knoten aus; ohne Auswahl von vorn bzw. von hinten.
    const Path& hier = g_app->selectedPath;
    const Path* treffer = nullptr;
    if (vorwaerts) {
        for (const Path& z : ziele) { if (hier.empty() || hier < z) { treffer = &z; break; } }
        if (treffer == nullptr) { treffer = &ziele.front(); }   // Umlauf
    } else {
        for (auto r = ziele.rbegin(); r != ziele.rend(); ++r) {
            if (!hier.empty() && *r < hier) { treffer = &*r; break; }
        }
        if (treffer == nullptr) { treffer = &ziele.back(); }    // Umlauf
    }
    selectByPath(*treffer);
}

void lesezeichenAlleWeg() {
    g_app->lesezeichen.erase(tabKennung(g_app->activeTab));
    if (!g_app->path.empty()) {
        g_app->settings.lesezeichen.erase(g_app->path);
    }
}

void lesezeichenMerken() {
    if (g_app->path.empty()) { return; }
    const auto it = g_app->lesezeichen.find(tabKennung(g_app->activeTab));
    g_app->doc.vergibKennungen();
    const std::vector<std::pair<Kennung, std::string>> liste = knotenListe(g_app->doc.script());
    std::string nummern;
    if (it != g_app->lesezeichen.end()) {
        for (std::size_t i = 0; i < liste.size(); ++i) {
            if (it->second.count(liste[i].first) != 0) {
                if (!nummern.empty()) { nummern += ","; }
                nummern += std::to_string(i);
            }
        }
    }
    if (nummern.empty()) {
        g_app->settings.lesezeichen.erase(g_app->path);
    } else {
        g_app->settings.lesezeichen[g_app->path] = nummern;
    }
}

void lesezeichenLaden() {
    const int uid = tabKennung(g_app->activeTab);
    if (uid < 0) { return; }
    g_app->lesezeichen.erase(uid);
    const auto it = g_app->settings.lesezeichen.find(g_app->path);
    if (g_app->path.empty() || it == g_app->settings.lesezeichen.end()) { return; }
    g_app->doc.vergibKennungen();
    const std::vector<std::pair<Kennung, std::string>> liste = knotenListe(g_app->doc.script());
    auto& lz = g_app->lesezeichen[uid];
    std::size_t a = 0;
    const std::string& t = it->second;
    while (a < t.size()) {
        std::size_t b = t.find(',', a);
        if (b == std::string::npos) { b = t.size(); }
        const std::size_t n = static_cast<std::size_t>(std::atoi(t.substr(a, b - a).c_str()));
        if (n < liste.size() && liste[n].first != 0) { lz.insert(liste[n].first); }
        a = b + 1;
    }
}

int tabKennung(int tab) {
    if (tab < 0 || tab >= static_cast<int>(g_app->tabs.size())) { return -1; }
    int& u = g_app->tabs[static_cast<std::size_t>(tab)].uid;
    if (u == 0) { u = g_app->naechsteTabUid++; }
    return u;
}

void fensterZeigen(bool& offen, const char* id) {
    offen = true;
    ImGui::SetWindowFocus(id);
}

int ersetzeEinen() {
    const FindOptions& o = g_app->findLetzte;
    if (o.contains.empty()) {
        return 0;
    }
    // Steht die Auswahl auf einem Treffer? Dann ihn ersetzen - sonst erst
    // hinspringen (Notepad++: der erste Druck findet, der zweite ersetzt).
    const std::vector<Path> treffer = findAll(g_app->doc.script(), o);
    const Path sel = g_app->selectedPath;
    int zahl = 0;
    if (std::find(treffer.begin(), treffer.end(), sel) != treffer.end()) {
        if (const Node* alt = nodeAt(g_app->doc.script(), sel)) {
            Node neu = *alt;
            zahl = ersetzeInKnoten(neu, o, g_app->findErsatz);
            if (zahl > 0 && g_app->doc.replaceAt(sel, std::move(neu))) {
                rebuildTree();
            }
        }
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), tr(Str::FindReplacedN), zahl);
    addStatus(buf);
    findNext(1);
    return zahl;
}

int ersetzeAlle(bool alleDokumente) {
    const FindOptions& o = g_app->findLetzte;
    if (o.contains.empty()) {
        return 0;
    }
    int zahl = 0;
    int dokumente = 0;
    // Der aktive Reiter - ein Rueckgaengig-Schritt fuer alles.
    {
        Script kopie = g_app->doc.script();
        const std::vector<Path> nurIn =
            g_app->findInSelection ? selectionOrCurrent() : std::vector<Path>{};
        const int n = ersetzeImSkript(kopie, o, g_app->findErsatz, nurIn);
        if (n > 0) {
            g_app->doc.replaceAll(std::move(kopie), "replace");
            rebuildTree();
            zahl += n;
            ++dokumente;
        }
    }
    // Die anderen Reiter: ihr geparktes Dokument, ebenfalls je ein Schritt.
    // "In selection" gilt dort nicht - die Auswahl gehoert dem aktiven.
    if (alleDokumente) {
        for (int i = 0; i < static_cast<int>(g_app->tabs.size()); ++i) {
            if (i == g_app->activeTab) {
                continue;
            }
            Document& d = g_app->tabs[static_cast<std::size_t>(i)].doc;
            Script kopie = d.script();
            const int n = ersetzeImSkript(kopie, o, g_app->findErsatz);
            if (n > 0) {
                d.replaceAll(std::move(kopie), "replace");
                zahl += n;
                ++dokumente;
            }
        }
    }
    char buf[96];
    if (alleDokumente) {
        std::snprintf(buf, sizeof(buf), tr(Str::FindReplacedDocs), zahl, dokumente);
    } else {
        std::snprintf(buf, sizeof(buf), tr(Str::FindReplacedN), zahl);
    }
    addStatus(buf);
    g_app->findHits = findAll(g_app->doc.script(), o);
    return zahl;
}

ImGuiKeyChord kuerzelVon(keys::Action a) { return chordFor(a); }
const char* kuerzelName(keys::Action a) { return chordName(a); }

}  // namespace bhed::gui
