// vektoricons.cpp - die Befehlssymbole als Vektorzeichnung.
//
// shank, 27.09.: "hier sind die ganzen icons bereinigt ohne den weissen
// hintergrund kannst du die adden und als vektor grafik neu zeichnen das die
// sauber sind die icons". Die bereinigten Vorlagen liegen in
// gui/icons_vorlage/ (16x16, aus Symbolstreifen BITMAP 132 von BehavEd.exe).
//
// Bis rc568 wurde der 16x16-Streifen auf Schriftgroesse hochgezogen - bei
// 144 dpi fast das Doppelte, also unscharf, und mit dem hellgruenen Saum der
// alten Maskenfarbe. Jetzt zeichnet jedes Symbol sich selbst auf einem
// 16er-Raster, das auf jede Groesse skaliert: dieselben Formen und Farben wie
// die Vorlagen, aber glatt.
//
// Zwei Farbsaetze wie im Original ("Use Alternative Coloured Icons", Dialog
// 131 id 1052): der zweite in den dunkleren Toenen, bei { } und e mit dem
// Punkt an anderer Stelle - so steht es im Streifen.
//
// Eine Abweichung, bewusst: die schwarzen Klammern von affect/task nehmen
// die Textfarbe. Schwarz auf dem dunklen Baum war kaum zu sehen; im hellen
// Farbschema bleiben sie schwarz.
#include "app_internal.h"

#include <cmath>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace bhed::gui {

namespace {

constexpr float kPi = 3.14159265358979F;

struct Pinsel {
    ImDrawList* dl;
    ImVec2 o;
    float s;   // Bildpunkte je Rastereinheit (Groesse / 16)

    [[nodiscard]] ImVec2 p(float x, float y) const { return ImVec2{o.x + x * s, o.y + y * s}; }
    void rechteck(float x0, float y0, float x1, float y1, ImU32 c, float rund = 0.0F) const {
        dl->AddRectFilled(p(x0, y0), p(x1, y1), c, rund * s);
    }
    void rahmen(float x0, float y0, float x1, float y1, ImU32 c, float w) const {
        dl->AddRect(p(x0, y0), p(x1, y1), c, 0.0F, 0, w * s);
    }
    void linie(float x0, float y0, float x1, float y1, ImU32 c, float w) const {
        dl->AddLine(p(x0, y0), p(x1, y1), c, w * s);
    }
    void zug(std::initializer_list<ImVec2> pts, ImU32 c, float w, bool zu = false) const {
        std::vector<ImVec2> v;
        for (const ImVec2& q : pts) { v.push_back(p(q.x, q.y)); }
        dl->AddPolyline(v.data(), static_cast<int>(v.size()), c, zu ? ImDrawFlags_Closed : 0, w * s);
    }
    void flaeche(std::initializer_list<ImVec2> pts, ImU32 c) const {
        std::vector<ImVec2> v;
        for (const ImVec2& q : pts) { v.push_back(p(q.x, q.y)); }
        dl->AddConcavePolyFilled(v.data(), static_cast<int>(v.size()), c);
    }
    void bogen(float cx, float cy, float r, float a0, float a1, ImU32 c, float w) const {
        dl->PathArcTo(p(cx, cy), r * s, a0, a1, 32);
        dl->PathStroke(c, 0, w * s);
    }
    void ellipsenBogen(float cx, float cy, float rx, float ry, float a0, float a1, ImU32 c, float w) const {
        dl->PathEllipticalArcTo(p(cx, cy), ImVec2{rx * s, ry * s}, 0.0F, a0, a1, 40);
        dl->PathStroke(c, 0, w * s);
    }
    void kreis(float cx, float cy, float r, ImU32 c, float w) const {
        dl->AddCircle(p(cx, cy), r * s, c, 40, w * s);
    }
    void scheibe(float cx, float cy, float r, ImU32 c) const {
        dl->AddCircleFilled(p(cx, cy), r * s, c, 32);
    }
};

struct Farben {
    ImU32 blau, blauPunkt, rot, rotDunkel, magenta, gruen, gruenDunkel, weiss, schwarz, klammer;
};

Farben farben(bool alt) {
    Farben f{};
    f.schwarz = IM_COL32(0, 0, 0, 255);
    f.klammer = ImGui::GetColorU32(ImGuiCol_Text);
    if (!alt) {
        f.blau = IM_COL32(0, 0, 255, 255);
        f.blauPunkt = IM_COL32(0, 0, 128, 255);
        f.rot = IM_COL32(255, 0, 0, 255);
        f.rotDunkel = IM_COL32(128, 0, 0, 255);
        f.magenta = IM_COL32(255, 0, 255, 255);
        f.gruen = IM_COL32(0, 255, 0, 255);
        f.gruenDunkel = IM_COL32(0, 128, 0, 255);
        f.weiss = IM_COL32(255, 255, 255, 255);
    } else {
        // Der zweite Farbsatz des Streifens: alles eine Stufe dunkler.
        f.blau = IM_COL32(0, 0, 128, 255);
        f.blauPunkt = IM_COL32(0, 0, 128, 255);
        f.rot = IM_COL32(128, 0, 0, 255);
        f.rotDunkel = IM_COL32(128, 0, 0, 255);
        f.magenta = IM_COL32(128, 0, 128, 255);
        f.gruen = IM_COL32(0, 128, 0, 255);
        f.gruenDunkel = IM_COL32(0, 128, 0, 255);
        f.weiss = IM_COL32(192, 192, 192, 255);
    }
    return f;
}

bool ist(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

// { }  - affect, task. Die rechte Klammer ist die gespiegelte linke.
void klammern(const Pinsel& z, ImU32 c, bool alt) {
    auto seite = [&](bool rechts) {
        auto x = [&](float v) { return rechts ? 16.0F - v : v; };
        z.zug({{x(6.3F), 1.5F}, {x(5.1F), 1.7F}, {x(4.4F), 2.5F}, {x(4.3F), 6.2F},
               {x(3.8F), 7.4F}, {x(2.4F), 8.0F}, {x(3.8F), 8.6F}, {x(4.3F), 9.8F},
               {x(4.4F), 13.5F}, {x(5.1F), 14.3F}, {x(6.3F), 14.5F}},
              c, 1.5F);
    };
    seite(false);
    seite(true);
    if (alt) {   // im zweiten Satz ein Punkt in der Mitte
        z.flaeche({{8.0F, 6.6F}, {9.4F, 8.0F}, {8.0F, 9.4F}, {6.6F, 8.0F}}, c);
    }
}

// e.  - alle Befehle ohne eigenes Symbol (use, kill, print, rem, run, ...)
void ereignis(const Pinsel& z, const Farben& f, bool alt) {
    const float dx = alt ? 2.6F : 0.0F;   // im zweiten Satz steht der Punkt links
    const float cx = 6.4F + dx;
    z.ellipsenBogen(cx, 9.0F, 3.8F, 4.3F, 0.0F, -(2.0F * kPi - 0.75F), f.blau, 1.9F);
    z.linie(cx - 3.8F, 9.0F, cx + 3.8F, 9.0F, f.blau, 1.7F);
    if (!alt) {
        z.rechteck(11.7F, 10.1F, 13.3F, 11.7F, f.blauPunkt);
    } else {
        z.rechteck(0.9F, 10.1F, 2.5F, 11.7F, f.blauPunkt);
    }
}

}  // namespace

bool vektorIcon(ImDrawList* dl, const char* name, ImVec2 pos, float groesse, bool alt) {
    if (dl == nullptr || name == nullptr) { return false; }
    const Pinsel z{dl, pos, groesse / 16.0F};
    const Farben f = farben(alt);

    if (ist(name, "I_SPACE")) { return true; }   // absichtlich leer
    if (ist(name, "I_BRACE")) { klammern(z, f.klammer, alt); return true; }
    if (ist(name, "I_EVENT")) { ereignis(z, f, alt); return true; }
    if (ist(name, "I_MACRO")) {   // [ ]
        z.zug({{6.3F, 2.0F}, {3.8F, 2.0F}, {3.8F, 14.0F}, {6.3F, 14.0F}}, f.rot, 1.6F);
        z.zug({{9.7F, 2.0F}, {12.2F, 2.0F}, {12.2F, 14.0F}, {9.7F, 14.0F}}, f.rot, 1.6F);
        return true;
    }
    if (ist(name, "I_SOUND")) {   // Lautsprecher: Feld mit Punkt und zwei Wellen
        z.rechteck(1.0F, 2.0F, 15.0F, 14.0F, f.magenta, 0.8F);
        z.scheibe(4.2F, 8.0F, 1.1F, f.weiss);
        z.bogen(4.6F, 8.0F, 3.5F, -0.95F, 0.95F, f.weiss, 1.35F);
        z.bogen(4.6F, 8.0F, 6.2F, -0.78F, 0.78F, f.weiss, 1.35F);
        return true;
    }
    if (ist(name, "I_CAMERA")) {  // Filmkamera auf Stativ
        z.rechteck(2.0F, 1.5F, 11.2F, 8.6F, f.magenta, 0.5F);
        z.flaeche({{10.6F, 4.0F}, {14.6F, 1.6F}, {14.6F, 8.5F}, {10.6F, 6.1F}}, f.magenta);
        z.linie(5.6F, 8.4F, 3.0F, 14.3F, f.magenta, 1.15F);
        z.linie(7.6F, 8.4F, 10.2F, 14.3F, f.magenta, 1.15F);
        return true;
    }
    if (ist(name, "I_ROTATE")) {  // Kreispfeil, Spitze links
        z.bogen(9.4F, 8.6F, 4.4F, -1.95F, 2.95F, f.blau, 1.9F);
        z.flaeche({{1.2F, 9.3F}, {5.5F, 6.1F}, {5.5F, 12.3F}}, f.blau);
        return true;
    }
    if (ist(name, "I_REMOVE")) {  // Verbotszeichen
        z.kreis(8.0F, 8.3F, 5.3F, f.blau, 1.7F);
        z.linie(4.9F, 11.4F, 11.1F, 5.2F, f.blau, 2.1F);
        return true;
    }
    if (ist(name, "I_SET")) {     // die "5"
        z.rechteck(1.0F, 1.5F, 14.5F, 3.3F, f.blau);
        z.rechteck(1.0F, 1.5F, 3.0F, 8.0F, f.blau);
        z.rechteck(1.0F, 6.3F, 14.5F, 8.0F, f.blau);
        z.rechteck(12.5F, 6.3F, 14.5F, 14.3F, f.blau);
        z.rechteck(1.0F, 12.5F, 14.5F, 14.3F, f.blau);
        return true;
    }
    if (ist(name, "I_MOVE")) {    // Pfeil nach rechts
        z.rechteck(1.0F, 5.2F, 8.6F, 10.8F, f.blau);
        z.flaeche({{8.0F, 1.3F}, {15.0F, 8.0F}, {8.0F, 14.7F}}, f.blau);
        return true;
    }
    if (ist(name, "I_IF")) {      // ?  - if und else
        z.bogen(7.9F, 5.2F, 3.0F, -kPi, 0.95F, f.rot, 2.3F);
        z.zug({{9.65F, 7.64F}, {7.9F, 9.0F}, {7.9F, 10.7F}}, f.rot, 2.3F);
        z.rechteck(6.6F, 12.3F, 9.2F, 14.7F, f.rot, 0.4F);
        return true;
    }
    if (ist(name, "I_LOOP")) {    // Schleife mit zwei Ansaetzen
        z.rahmen(3.6F, 2.6F, 12.4F, 13.3F, f.rot, 1.7F);
        z.linie(0.8F, 7.8F, 3.6F, 7.8F, f.rot, 1.4F);
        z.linie(12.4F, 7.8F, 15.2F, 7.8F, f.rot, 1.4F);
        return true;
    }
    // Die drei Kaestchen: schwarz umrandet, dunkler Rahmen, helle Flaeche.
    auto kaestchen = [&](float y0, float y1, ImU32 dunkel, ImU32 hell) {
        z.rechteck(3.0F, y0, 13.0F, y1, dunkel);
        if (!alt) { z.rechteck(4.0F, y0 + 1.0F, 12.0F, y1 - 1.0F, hell); }
    };
    if (ist(name, "I_DO")) {
        z.rechteck(2.0F, 1.0F, 14.0F, 15.0F, f.schwarz);
        kaestchen(2.0F, 14.0F, f.gruenDunkel, f.gruen);
        return true;
    }
    if (ist(name, "I_WAIT")) {    // auf einen Task warten
        z.rechteck(2.0F, 1.0F, 14.0F, 15.0F, f.schwarz);
        kaestchen(2.0F, 14.0F, f.rotDunkel, f.rot);
        return true;
    }
    if (ist(name, "I_DOWAIT")) {
        z.rechteck(2.0F, 1.0F, 14.0F, 15.0F, f.schwarz);
        kaestchen(2.0F, 7.5F, f.rotDunkel, f.rot);
        kaestchen(8.5F, 14.0F, f.gruenDunkel, f.gruen);
        return true;
    }
    if (ist(name, "I_SIGNAL")) {  // Kasten mit Fahne oben
        z.rechteck(5.0F, 1.0F, 6.7F, 7.0F, f.rot);
        z.rechteck(5.0F, 1.0F, 8.6F, 2.7F, f.rot);
        z.rahmen(2.3F, 4.7F, 13.8F, 10.6F, f.rot, 1.2F);
        z.rechteck(5.0F, 10.6F, 7.4F, 15.0F, f.rot);
        return true;
    }
    if (ist(name, "I_WAITSIGNAL")) {  // Kasten mit liegender Fahne
        z.rahmen(2.3F, 2.3F, 13.8F, 9.6F, f.rot, 1.2F);
        z.rechteck(5.0F, 4.3F, 10.6F, 5.5F, f.rot);
        z.rechteck(8.3F, 4.3F, 10.6F, 7.3F, f.rot);
        z.rechteck(4.4F, 9.6F, 6.8F, 15.0F, f.rot);
        return true;
    }
    if (ist(name, "I_FLUSH")) {   // die Spuelung
        z.rechteck(3.5F, 2.0F, 6.3F, 8.6F, f.rot);
        z.linie(6.1F, 6.4F, 9.9F, 2.2F, f.rot, 1.8F);
        z.flaeche({{2.8F, 8.0F}, {12.8F, 8.0F}, {12.2F, 9.7F}, {10.3F, 11.3F}, {10.7F, 14.5F},
                   {4.2F, 14.5F}, {4.6F, 11.3F}, {3.0F, 9.9F}},
                  f.rot);
        return true;
    }
    if (ist(name, "I_WAITCLOCK")) {   // Uhr
        z.kreis(8.0F, 8.6F, 5.9F, f.rot, 1.15F);
        z.linie(8.0F, 8.6F, 8.0F, 4.6F, f.rot, 1.25F);
        z.linie(8.0F, 8.6F, 10.9F, 8.6F, f.rot, 1.25F);
        return true;
    }
    return false;   // unbekannt: der Aufrufer nimmt das Bild aus dem Streifen
}

}  // namespace bhed::gui
