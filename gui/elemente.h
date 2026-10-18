// elemente.h - was ImGui in diesem Bild gezeichnet hat (das Element-Register)
//
// Paket tour (rc601). Bisher lag das Register im namenlosen Namensraum von
// gui/selbsttest.cpp und lief nur im Selbsttest. Die Tutorials im Programm
// (gui/tour.cpp) zeigen auf Knoepfe und brauchen es deshalb auch im normalen
// Betrieb - aber nur fuer die paar Beschriftungen, auf die die Tour gerade
// zeigt. Drei Stufen:
//
//   Aus        kein Haken laeuft; ImGui prueft je Element nur das Flag
//              TestEngineHookItems (imgui_internal.h:4396-4397).
//   Gefiltert  jedes ItemAdd landet als kleiner Eintrag ohne Zeichenketten in
//              einem wiederverwendeten Puffer; ein volles Element entsteht nur
//              fuer Beschriftungen, die mit einem Wunsch beginnen (ohne
//              Speicheranforderung verglichen).
//   Voll       alles, wie bisher im Selbsttest.
//
// Wer das Register braucht, meldet es in JEDEM Bild neu an (wunschVoll /
// wunschLabels); bildEnde() leert es und setzt die Stufe fuer das naechste
// Bild. Meldet niemand etwas an, ist es im naechsten Bild aus.
//
// Die Fehlerbehebung dieses Pakets: ImGuis TabItemEx meldet die Beschriftung
// (IMGUI_TEST_ENGINE_ITEM_INFO, imgui_widgets.cpp:10711) VOR seinem ItemAdd
// (imgui_widgets.cpp:10827). Solche Eintraege warten jetzt auf ihr Rechteck,
// statt das des vorigen Elements zu bekommen (Handbuchbild s_neu_plus: der
// Rahmen stand auf dem letzten Reiter statt auf "+").
#ifndef BHED_GUI_ELEMENTE_H
#define BHED_GUI_ELEMENTE_H

#include "imgui.h"
#include "imgui_internal.h"

#include <string>
#include <vector>

namespace bhed::gui::elemente {

struct Element {
    ImGuiID id = 0;
    std::string label;      // leer, wenn nur ItemAdd kam
    ImRect rect;
    ImRect clip;            // sichtbarer Bereich beim Zeichnen (Fenster/Zelle)
    std::string fenster;    // Name des obersten Fensters
    std::string innen;      // Name des Fensters, in dem es steht (Kindfenster)
    // rc601: die Zustandsfahnen aus ITEM_INFO (Opened bei BeginMenu,
    // Checked bei Haken) - die Tour erkennt daran ein offenes Menue.
    ImGuiItemStatusFlags flags = 0;
    // rc601: Beschriftung kam vor dem ItemAdd (Reiter); das Rechteck folgt.
    bool wartet = false;
};

enum class Modus { Aus, Gefiltert, Voll };

// Die Eintraege dieses Bildes (Reihenfolge wie gezeichnet).
std::vector<Element>& alle();
// Die Stufe des laufenden Bildes.
Modus modus();

// Fuer das NAECHSTE Bild anmelden - in jedem Bild neu.
void wunschVoll();
// Beschriftungen, deren Anfang passt ("Save" behaelt auch "Save as"; "##v"
// die Eingabefelder). Mit "*" vorn passt das ENDE ("*errors" behaelt die
// Zaehler "3 errors" der Statuszeile). Leere Eintraege werden ignoriert.
void wunschLabels(const std::vector<std::string>& praefixe);

// Ende des Bildes (gui/app.cpp, nach selbsttest()): leeren und die Stufe fuer
// das naechste Bild setzen (GImGui->TestEngineHookItems).
void bildEnde();

// Die Haken (ImGuiTestEngineHook_ItemAdd/ItemInfo, gui/elemente.cpp).
void merkeElement(ImGuiContext* ctx, ImGuiID id, const ImRect& bb);
void merkeText(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags);

// --- Suchen ---------------------------------------------------------------
// Die Beschriftung ohne den "##..."-Teil - so, wie sie auf dem Knopf steht.
std::string sichtbarerText(const char* label);
// Das `nte` Element mit dieser Beschriftung. Verglichen wird mit dem ganzen
// Text ("##v" findet Eingabefelder) und mit dem sichtbaren ("Ok").
// `fenster`: Teil des Fensternamens, leer = egal. `nachbar`: nur Treffer auf
// derselben Zeile wie dieses Element.
const Element* finde(const std::string& label, const std::string& fenster, int nte, bool praefix,
                     const std::string& nachbar = "");
bool aufZeileMit(const Element& e, const std::string& nachbar, const std::string& fenster);
const Element* findeKennung(ImGuiID id);
// Liegt das Element ganz im sichtbaren Bereich (mit dem halben Zeilenabstand
// als Spiel, siehe selbsttest.cpp)?
bool ganzImBild(const Element& e);

}  // namespace bhed::gui::elemente

#endif
