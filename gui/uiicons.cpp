// uiicons.cpp - Bedien-Icons im Strichstil (Map-Tab und Inspektor)
//
// Gewuenscht (11.10.2026): "ich will das es moderner und intuitiv aussieht
// wenn du icons erstellen musst dafuer dann mach das bitte". Vorbild sind die
// Strich-Icons heutiger Werkzeuge (Unreal, Blender, Lucide): ein 16er-Raster,
// gleichmaessige Linienstaerke, runde Enden, keine Fuellflaechen ausser wo
// es der Form hilft. Gezeichnet mit ImDrawList - scharf bei jeder Groesse und
// in der Textfarbe des Farbschemas (hell wie dunkel).
//
// Die Befehlssymbole des Skriptbaums (vektoricons.cpp) bleiben die des
// Originals; diese hier sind fuer Bedienknoepfe.
#include "app_internal.h"

#include <cmath>
#include <initializer_list>
#include <vector>

namespace bhed::gui {
namespace {

constexpr float kPi = 3.14159265358979F;

struct Stift {
    ImDrawList* dl;
    ImVec2 o;
    float s;      // Bildpunkte je Rastereinheit (Groesse / 16)
    ImU32 c;
    float w;      // Linienstaerke in Bildpunkten
    [[nodiscard]] ImVec2 p(float x, float y) const { return ImVec2{o.x + x * s, o.y + y * s}; }
    void linie(float x0, float y0, float x1, float y1) const { dl->AddLine(p(x0, y0), p(x1, y1), c, w); }
    void zug(std::initializer_list<ImVec2> pts, bool zu = false) const {
        std::vector<ImVec2> v;
        for (const ImVec2& q : pts) { v.push_back(p(q.x, q.y)); }
        dl->AddPolyline(v.data(), static_cast<int>(v.size()), c, zu ? ImDrawFlags_Closed : ImDrawFlags_None, w);
    }
    void flaeche(std::initializer_list<ImVec2> pts) const {
        std::vector<ImVec2> v;
        for (const ImVec2& q : pts) { v.push_back(p(q.x, q.y)); }
        dl->AddConvexPolyFilled(v.data(), static_cast<int>(v.size()), c);
    }
    void rahmen(float x0, float y0, float x1, float y1, float rund = 1.5F) const {
        dl->AddRect(p(x0, y0), p(x1, y1), c, rund * s, 0, w);
    }
    void kreis(float x, float y, float r) const { dl->AddCircle(p(x, y), r * s, c, 0, w); }
    void punkt(float x, float y, float r) const { dl->AddCircleFilled(p(x, y), r * s, c); }
    void bogen(float x, float y, float r, float a0, float a1) const {
        dl->PathArcTo(p(x, y), r * s, a0, a1);
        dl->PathStroke(c, ImDrawFlags_None, w);
    }
    // Pfeilspitze an (x, y) in Richtung (dx, dy).
    void spitze(float x, float y, float dx, float dy, float l = 2.6F) const {
        const float n = std::sqrt(dx * dx + dy * dy);
        if (n <= 0.0F) { return; }
        dx /= n;
        dy /= n;
        zug({ImVec2{x - dx * l - dy * l * 0.8F, y - dy * l + dx * l * 0.8F}, ImVec2{x, y},
             ImVec2{x - dx * l + dy * l * 0.8F, y - dy * l - dx * l * 0.8F}});
    }
};

}  // namespace

// Jedes Icon in seiner Farbe - shank (11.10.): "du kannst sie auch gerne
// farbiger machen damit man die besser unterscheiden kann". Die Toene sind
// mittelhell und kraeftig; im hellen Farbschema dunkler, damit sie auf Weiss
// lesbar bleiben. Kamera in Magenta wie die camera-Befehle im Skriptbaum.
ImVec4 uiIconFarbe(UiIcon icon) {
    ImVec4 c{0.80F, 0.82F, 0.86F, 1.0F};   // neutral
    switch (icon) {
        case UiIcon::Neu: c = ImVec4{0.42F, 0.85F, 0.45F, 1.0F}; break;          // gruen
        case UiIcon::Oeffnen: c = ImVec4{0.98F, 0.75F, 0.30F, 1.0F}; break;      // bernstein
        case UiIcon::Speichern: c = ImVec4{0.40F, 0.68F, 1.00F, 1.0F}; break;    // blau
        case UiIcon::Mission: c = ImVec4{1.00F, 0.58F, 0.28F, 1.0F}; break;      // orange
        case UiIcon::Karte: c = ImVec4{0.35F, 0.85F, 0.78F, 1.0F}; break;        // tuerkis
        case UiIcon::Paket: c = ImVec4{0.85F, 0.66F, 0.42F, 1.0F}; break;
        case UiIcon::Liste: c = ImVec4{0.72F, 0.60F, 1.00F, 1.0F}; break;        // violett
        case UiIcon::Skript: c = ImVec4{0.75F, 0.80F, 0.90F, 1.0F}; break;
        case UiIcon::Entity: c = ImVec4{0.36F, 0.82F, 0.95F, 1.0F}; break;       // cyan
        case UiIcon::Namen: c = ImVec4{0.80F, 0.66F, 1.00F, 1.0F}; break;        // flieder
        case UiIcon::Figur: c = ImVec4{0.98F, 0.86F, 0.38F, 1.0F}; break;        // gelb
        case UiIcon::Effekt: c = ImVec4{1.00F, 0.55F, 0.35F, 1.0F}; break;       // orangerot
        case UiIcon::Leuchten: c = ImVec4{1.00F, 0.80F, 0.25F, 1.0F}; break;     // gold
        case UiIcon::Licht: c = ImVec4{1.00F, 0.92F, 0.55F, 1.0F}; break;        // warmgelb
        case UiIcon::Nebel: c = ImVec4{0.70F, 0.78F, 0.88F, 1.0F}; break;        // graublau
        case UiIcon::Himmel: c = ImVec4{0.45F, 0.75F, 1.00F, 1.0F}; break;       // himmelblau
        case UiIcon::Kamera: c = ImVec4{0.95F, 0.40F, 0.95F, 1.0F}; break;       // magenta
        case UiIcon::Einstellungen: c = ImVec4{0.78F, 0.80F, 0.84F, 1.0F}; break;
        case UiIcon::Statistik: c = ImVec4{0.50F, 0.88F, 0.55F, 1.0F}; break;
        case UiIcon::Plus: c = ImVec4{0.42F, 0.85F, 0.45F, 1.0F}; break;
        case UiIcon::Boden: c = ImVec4{0.55F, 0.85F, 0.45F, 1.0F}; break;
        case UiIcon::Hinfliegen: c = ImVec4{0.36F, 0.82F, 0.95F, 1.0F}; break;
        case UiIcon::Duplizieren: c = ImVec4{0.45F, 0.70F, 1.00F, 1.0F}; break;
        case UiIcon::Loeschen: c = ImVec4{1.00F, 0.42F, 0.40F, 1.0F}; break;     // rot
        case UiIcon::Explorer: c = ImVec4{0.98F, 0.75F, 0.30F, 1.0F}; break;
        case UiIcon::Warnung: c = ImVec4{1.00F, 0.75F, 0.30F, 1.0F}; break;
        case UiIcon::Haken: c = ImVec4{0.42F, 0.85F, 0.45F, 1.0F}; break;
        case UiIcon::Abspielen: c = ImVec4{0.42F, 0.85F, 0.45F, 1.0F}; break;
        case UiIcon::Pause: c = ImVec4{1.00F, 0.75F, 0.30F, 1.0F}; break;
        case UiIcon::Zielen: c = ImVec4{0.36F, 0.82F, 0.95F, 1.0F}; break;
        case UiIcon::Steuern: c = ImVec4{0.95F, 0.40F, 0.95F, 1.0F}; break;
        case UiIcon::Pfad: c = ImVec4{0.43F, 0.86F, 0.94F, 1.0F}; break;      // Wegnetz: tuerkis
        case UiIcon::Oben: c = ImVec4{0.55F, 0.80F, 1.00F, 1.0F}; break;
        case UiIcon::Erzeugen: c = ImVec4{1.00F, 0.80F, 0.30F, 1.0F}; break;
        case UiIcon::Raster: c = ImVec4{0.65F, 0.85F, 0.55F, 1.0F}; break;
        case UiIcon::Bildschirm: c = ImVec4{0.95F, 0.40F, 0.95F, 1.0F}; break;   // wie die Kamera
        case UiIcon::Animieren: c = ImVec4{1.00F, 0.78F, 0.30F, 1.0F}; break;   // Schluessel: bernstein
        case UiIcon::ZumAnfang: c = ImVec4{0.80F, 0.82F, 0.86F, 1.0F}; break;
        case UiIcon::Schleife: c = ImVec4{0.45F, 0.75F, 1.00F, 1.0F}; break;
        case UiIcon::Ton: c = ImVec4{0.42F, 0.85F, 0.78F, 1.0F}; break;
        case UiIcon::Folgen: c = ImVec4{0.95F, 0.40F, 0.95F, 1.0F}; break;
        case UiIcon::Zeilen: c = ImVec4{0.98F, 0.86F, 0.38F, 1.0F}; break;
        case UiIcon::Ruhig: c = ImVec4{0.62F, 0.66F, 0.75F, 1.0F}; break;
        case UiIcon::Einpassen: c = ImVec4{0.36F, 0.82F, 0.95F, 1.0F}; break;
        case UiIcon::Marke: c = ImVec4{0.95F, 0.40F, 0.95F, 1.0F}; break;     // Kameramarke: magenta wie die Kamera
        case UiIcon::Blitz: c = ImVec4{1.00F, 0.62F, 0.30F, 1.0F}; break;     // Ausloeser
        case UiIcon::Tuer: c = ImVec4{0.55F, 0.70F, 1.00F, 1.0F}; break;      // Mover
        case UiIcon::Wegpunkt: c = ImVec4{0.42F, 0.85F, 0.45F, 1.0F}; break;  // Laufziel
        case UiIcon::Bruch: c = ImVec4{0.85F, 0.66F, 0.42F, 1.0F}; break;
        default: break;
    }
    // Helles Farbschema: dieselbe Farbe, dunkler.
    const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    if (bg.x * 0.3F + bg.y * 0.59F + bg.z * 0.11F > 0.6F) {
        c = ImVec4{c.x * 0.62F, c.y * 0.62F, c.z * 0.62F, 1.0F};
    }
    return c;
}

void uiIcon(ImDrawList* dl, UiIcon icon, ImVec2 pos, float groesse, ImU32 farbe) {
    const Stift z{dl, pos, groesse / 16.0F, farbe, std::max(1.0F, groesse / 16.0F * 1.35F)};
    switch (icon) {
        case UiIcon::Neu:   // Blatt mit Eselsohr und Plus
            z.zug({ImVec2{9.5F, 1.5F}, ImVec2{3.5F, 1.5F}, ImVec2{3.5F, 14.5F}, ImVec2{8.0F, 14.5F}});
            z.zug({ImVec2{9.5F, 1.5F}, ImVec2{12.5F, 4.5F}, ImVec2{12.5F, 8.0F}});
            z.zug({ImVec2{9.5F, 1.5F}, ImVec2{9.5F, 4.5F}, ImVec2{12.5F, 4.5F}});
            z.linie(12.0F, 10.0F, 12.0F, 15.0F);
            z.linie(9.5F, 12.5F, 14.5F, 12.5F);
            break;
        case UiIcon::Oeffnen:   // Ordner
            z.zug({ImVec2{1.5F, 13.5F}, ImVec2{1.5F, 3.0F}, ImVec2{6.0F, 3.0F}, ImVec2{7.5F, 4.8F},
                   ImVec2{13.0F, 4.8F}, ImVec2{13.0F, 7.0F}});
            z.zug({ImVec2{1.5F, 13.5F}, ImVec2{4.0F, 7.0F}, ImVec2{15.0F, 7.0F}, ImVec2{12.5F, 13.5F}}, true);
            break;
        case UiIcon::Speichern:   // Diskette
            z.zug({ImVec2{2.0F, 2.0F}, ImVec2{11.5F, 2.0F}, ImVec2{14.0F, 4.5F}, ImVec2{14.0F, 14.0F},
                   ImVec2{2.0F, 14.0F}}, true);
            z.rahmen(4.5F, 2.0F, 10.5F, 6.0F, 0.0F);
            z.rahmen(4.5F, 9.0F, 11.5F, 14.0F, 0.5F);
            break;
        case UiIcon::Mehr:   // Pfeil nach unten (Klappmenue)
            z.zug({ImVec2{4.0F, 6.5F}, ImVec2{8.0F, 10.5F}, ImVec2{12.0F, 6.5F}});
            break;
        case UiIcon::Karte:   // gefaltete Karte
            z.zug({ImVec2{1.5F, 3.5F}, ImVec2{5.5F, 2.0F}, ImVec2{10.5F, 3.5F}, ImVec2{14.5F, 2.0F},
                   ImVec2{14.5F, 12.5F}, ImVec2{10.5F, 14.0F}, ImVec2{5.5F, 12.5F}, ImVec2{1.5F, 14.0F}}, true);
            z.linie(5.5F, 2.0F, 5.5F, 12.5F);
            z.linie(10.5F, 3.5F, 10.5F, 14.0F);
            break;
        case UiIcon::Paket:   // Kiste (.pk3)
            z.zug({ImVec2{8.0F, 1.5F}, ImVec2{14.0F, 4.5F}, ImVec2{14.0F, 11.5F}, ImVec2{8.0F, 14.5F},
                   ImVec2{2.0F, 11.5F}, ImVec2{2.0F, 4.5F}}, true);
            z.zug({ImVec2{2.0F, 4.5F}, ImVec2{8.0F, 7.5F}, ImVec2{14.0F, 4.5F}});
            z.linie(8.0F, 7.5F, 8.0F, 14.5F);
            break;
        case UiIcon::Liste:   // Liste (.ent)
            for (int k = 0; k < 3; ++k) {
                const float y = 4.0F + 4.0F * static_cast<float>(k);
                z.punkt(3.0F, y, 1.1F);
                z.linie(6.0F, y, 14.0F, y);
            }
            break;
        case UiIcon::Skript:   // Blatt mit Zeilen
            z.zug({ImVec2{3.0F, 1.5F}, ImVec2{13.0F, 1.5F}, ImVec2{13.0F, 14.5F}, ImVec2{3.0F, 14.5F}}, true);
            z.linie(5.5F, 5.0F, 10.5F, 5.0F);
            z.linie(5.5F, 8.0F, 10.5F, 8.0F);
            z.linie(5.5F, 11.0F, 8.5F, 11.0F);
            break;
        case UiIcon::Entity:   // Wuerfel
            z.zug({ImVec2{8.0F, 1.5F}, ImVec2{14.0F, 4.5F}, ImVec2{14.0F, 11.5F}, ImVec2{8.0F, 14.5F},
                   ImVec2{2.0F, 11.5F}, ImVec2{2.0F, 4.5F}}, true);
            z.zug({ImVec2{2.0F, 4.5F}, ImVec2{8.0F, 7.5F}, ImVec2{14.0F, 4.5F}});
            z.linie(8.0F, 7.5F, 8.0F, 14.5F);
            z.punkt(8.0F, 7.5F, 1.0F);
            break;
        case UiIcon::Namen:   // Schild mit Loch
            z.zug({ImVec2{1.5F, 7.0F}, ImVec2{7.0F, 1.5F}, ImVec2{14.5F, 1.5F}, ImVec2{14.5F, 9.0F},
                   ImVec2{9.0F, 14.5F}}, true);
            z.kreis(11.0F, 5.0F, 1.3F);
            break;
        case UiIcon::Figur:   // Person
            z.kreis(8.0F, 4.0F, 2.6F);
            z.zug({ImVec2{2.5F, 15.0F}, ImVec2{3.0F, 11.0F}, ImVec2{5.5F, 8.5F}, ImVec2{10.5F, 8.5F},
                   ImVec2{13.0F, 11.0F}, ImVec2{13.5F, 15.0F}});
            break;
        case UiIcon::Effekt:   // Funken
            z.zug({ImVec2{7.0F, 1.5F}, ImVec2{8.3F, 6.2F}, ImVec2{13.0F, 7.5F}, ImVec2{8.3F, 8.8F},
                   ImVec2{7.0F, 13.5F}, ImVec2{5.7F, 8.8F}, ImVec2{1.0F, 7.5F}, ImVec2{5.7F, 6.2F}}, true);
            z.linie(13.0F, 11.0F, 13.0F, 15.0F);
            z.linie(11.0F, 13.0F, 15.0F, 13.0F);
            break;
        case UiIcon::Leuchten:   // Sonne
            z.kreis(8.0F, 8.0F, 3.0F);
            for (int k = 0; k < 8; ++k) {
                const float a = static_cast<float>(k) * kPi / 4.0F;
                z.linie(8.0F + std::cos(a) * 5.0F, 8.0F + std::sin(a) * 5.0F, 8.0F + std::cos(a) * 7.0F,
                        8.0F + std::sin(a) * 7.0F);
            }
            break;
        case UiIcon::Licht:   // Gluehbirne
            z.bogen(8.0F, 6.5F, 4.5F, kPi * 0.8F, kPi * 2.2F);
            z.zug({ImVec2{4.5F, 9.5F}, ImVec2{6.0F, 11.5F}, ImVec2{10.0F, 11.5F}, ImVec2{11.5F, 9.5F}});
            z.linie(6.0F, 13.5F, 10.0F, 13.5F);
            z.linie(7.0F, 15.0F, 9.0F, 15.0F);
            break;
        case UiIcon::Nebel:   // Wolke mit Schleiern
            z.bogen(6.0F, 7.0F, 3.0F, kPi, kPi * 2.0F);
            z.bogen(10.5F, 7.5F, 2.5F, kPi * 1.2F, kPi * 2.0F);
            z.linie(2.0F, 10.5F, 14.0F, 10.5F);
            z.linie(4.0F, 13.5F, 12.0F, 13.5F);
            break;
        case UiIcon::Himmel:   // Berge mit Sonne
            z.zug({ImVec2{1.0F, 14.0F}, ImVec2{6.0F, 6.0F}, ImVec2{9.0F, 10.5F}, ImVec2{11.0F, 8.0F},
                   ImVec2{15.0F, 14.0F}}, true);
            z.kreis(12.0F, 3.5F, 1.8F);
            break;
        case UiIcon::Kamera:   // Filmkamera
            z.rahmen(1.5F, 5.0F, 11.0F, 12.5F, 1.0F);
            z.zug({ImVec2{11.0F, 7.5F}, ImVec2{14.5F, 5.5F}, ImVec2{14.5F, 12.0F}, ImVec2{11.0F, 10.0F}});
            break;
        case UiIcon::Einstellungen: {   // Zahnrad
            z.kreis(8.0F, 8.0F, 2.2F);
            std::vector<ImVec2> v;
            for (int k = 0; k < 16; ++k) {
                const float a = static_cast<float>(k) * kPi / 8.0F + kPi / 16.0F;
                const float r = (k % 2 == 0) ? 6.6F : 5.0F;
                v.push_back(z.p(8.0F + std::cos(a) * r, 8.0F + std::sin(a) * r));
            }
            dl->AddPolyline(v.data(), static_cast<int>(v.size()), farbe, ImDrawFlags_Closed, z.w);
            break;
        }
        case UiIcon::Statistik:   // Balken
            z.linie(2.0F, 14.5F, 14.0F, 14.5F);
            z.rahmen(3.0F, 8.0F, 5.5F, 14.5F, 0.3F);
            z.rahmen(7.0F, 3.0F, 9.5F, 14.5F, 0.3F);
            z.rahmen(11.0F, 6.0F, 13.5F, 14.5F, 0.3F);
            break;
        case UiIcon::Plus:
            z.linie(8.0F, 2.5F, 8.0F, 13.5F);
            z.linie(2.5F, 8.0F, 13.5F, 8.0F);
            break;
        case UiIcon::Boden:   // Pfeil auf eine Linie
            z.linie(8.0F, 1.5F, 8.0F, 10.5F);
            z.spitze(8.0F, 10.5F, 0.0F, 1.0F, 3.2F);
            z.linie(2.0F, 14.0F, 14.0F, 14.0F);
            break;
        case UiIcon::Hinfliegen:   // Zielkreuz
            z.kreis(8.0F, 8.0F, 4.5F);
            z.linie(8.0F, 0.8F, 8.0F, 4.0F);
            z.linie(8.0F, 12.0F, 8.0F, 15.2F);
            z.linie(0.8F, 8.0F, 4.0F, 8.0F);
            z.linie(12.0F, 8.0F, 15.2F, 8.0F);
            z.punkt(8.0F, 8.0F, 1.0F);
            break;
        case UiIcon::Duplizieren:   // zwei Blaetter
            z.rahmen(5.0F, 5.0F, 14.5F, 14.5F, 1.2F);
            z.zug({ImVec2{11.0F, 3.0F}, ImVec2{11.0F, 1.5F}, ImVec2{1.5F, 1.5F}, ImVec2{1.5F, 11.0F},
                   ImVec2{3.0F, 11.0F}});
            break;
        case UiIcon::Loeschen:   // Papierkorb
            z.linie(2.0F, 4.0F, 14.0F, 4.0F);
            z.zug({ImVec2{6.0F, 4.0F}, ImVec2{6.0F, 1.8F}, ImVec2{10.0F, 1.8F}, ImVec2{10.0F, 4.0F}});
            z.zug({ImVec2{3.5F, 4.0F}, ImVec2{4.5F, 14.5F}, ImVec2{11.5F, 14.5F}, ImVec2{12.5F, 4.0F}});
            z.linie(6.8F, 7.0F, 6.8F, 12.0F);
            z.linie(9.2F, 7.0F, 9.2F, 12.0F);
            break;
        case UiIcon::Rueckgaengig:
            z.bogen(8.5F, 9.0F, 5.0F, kPi * 1.15F, kPi * 2.25F);
            z.spitze(4.0F, 6.5F, -0.4F, 1.0F, 3.0F);
            break;
        case UiIcon::Wiederholen:
            z.bogen(7.5F, 9.0F, 5.0F, kPi * 0.75F, kPi * 1.85F);
            z.spitze(12.0F, 6.5F, 0.4F, 1.0F, 3.0F);
            break;
        case UiIcon::Explorer:   // offener Ordner mit Pfeil
            z.zug({ImVec2{1.5F, 13.5F}, ImVec2{1.5F, 3.0F}, ImVec2{6.0F, 3.0F}, ImVec2{7.5F, 4.8F},
                   ImVec2{11.0F, 4.8F}});
            z.zug({ImVec2{1.5F, 13.5F}, ImVec2{4.0F, 8.0F}, ImVec2{10.0F, 8.0F}});
            z.linie(9.5F, 13.5F, 15.0F, 8.0F);
            z.zug({ImVec2{11.5F, 8.0F}, ImVec2{15.0F, 8.0F}, ImVec2{15.0F, 11.5F}});
            break;
        case UiIcon::Suche:   // Lupe
            z.kreis(6.8F, 6.8F, 4.5F);
            z.linie(10.2F, 10.2F, 14.5F, 14.5F);
            break;
        case UiIcon::Verschieben:   // Kreuz aus Pfeilen
            z.linie(8.0F, 1.5F, 8.0F, 14.5F);
            z.linie(1.5F, 8.0F, 14.5F, 8.0F);
            z.spitze(8.0F, 1.5F, 0.0F, -1.0F);
            z.spitze(8.0F, 14.5F, 0.0F, 1.0F);
            z.spitze(1.5F, 8.0F, -1.0F, 0.0F);
            z.spitze(14.5F, 8.0F, 1.0F, 0.0F);
            break;
        case UiIcon::Drehen:
            z.bogen(8.0F, 8.0F, 5.5F, kPi * 0.2F, kPi * 1.8F);
            z.spitze(12.5F, 4.8F, 0.6F, 1.0F, 3.0F);
            break;
        case UiIcon::Mission:   // Fahne
            z.linie(3.0F, 1.5F, 3.0F, 15.0F);
            z.zug({ImVec2{3.0F, 2.0F}, ImVec2{13.5F, 2.0F}, ImVec2{11.0F, 5.5F}, ImVec2{13.5F, 9.0F},
                   ImVec2{3.0F, 9.0F}});
            break;
        case UiIcon::Schliessen:
            z.linie(3.5F, 3.5F, 12.5F, 12.5F);
            z.linie(12.5F, 3.5F, 3.5F, 12.5F);
            break;
        case UiIcon::Haken:
            z.zug({ImVec2{2.5F, 8.5F}, ImVec2{6.5F, 12.5F}, ImVec2{13.5F, 4.0F}});
            break;
        case UiIcon::Abspielen:
            z.flaeche({ImVec2{4.0F, 2.5F}, ImVec2{13.5F, 8.0F}, ImVec2{4.0F, 13.5F}});
            break;
        case UiIcon::Pause:
            z.flaeche({ImVec2{3.5F, 2.5F}, ImVec2{6.5F, 2.5F}, ImVec2{6.5F, 13.5F}, ImVec2{3.5F, 13.5F}});
            z.flaeche({ImVec2{9.5F, 2.5F}, ImVec2{12.5F, 2.5F}, ImVec2{12.5F, 13.5F}, ImVec2{9.5F, 13.5F}});
            break;
        case UiIcon::Zurueck:   // Winkel nach links
            z.zug({ImVec2{10.0F, 3.0F}, ImVec2{5.0F, 8.0F}, ImVec2{10.0F, 13.0F}});
            break;
        case UiIcon::Vor:   // Winkel nach rechts
            z.zug({ImVec2{6.0F, 3.0F}, ImVec2{11.0F, 8.0F}, ImVec2{6.0F, 13.0F}});
            break;
        case UiIcon::Zielen:   // Fadenkreuz: auf die Ansicht setzen
            z.kreis(8.0F, 8.0F, 5.0F);
            z.linie(8.0F, 0.8F, 8.0F, 4.5F);
            z.linie(8.0F, 11.5F, 8.0F, 15.2F);
            z.linie(0.8F, 8.0F, 4.5F, 8.0F);
            z.linie(11.5F, 8.0F, 15.2F, 8.0F);
            z.punkt(8.0F, 8.0F, 1.2F);
            break;
        case UiIcon::Steuern:   // Steuerknueppel: in der Kamera sitzen
            z.rahmen(2.5F, 11.0F, 13.5F, 14.5F, 1.2F);
            z.linie(8.0F, 11.0F, 8.0F, 5.5F);
            z.kreis(8.0F, 3.8F, 2.0F);
            z.punkt(11.2F, 12.8F, 0.9F);
            break;
        case UiIcon::ZumAnfang:   // Strich und Dreieck nach links
            z.linie(3.5F, 3.0F, 3.5F, 13.0F);
            z.flaeche({ImVec2{13.0F, 3.0F}, ImVec2{13.0F, 13.0F}, ImVec2{5.5F, 8.0F}});
            break;
        case UiIcon::Schleife:   // zwei Pfeile im Kreis
            z.bogen(8.0F, 8.0F, 5.5F, kPi * 1.15F, kPi * 1.95F);
            z.spitze(13.3F, 6.4F, 0.4F, 1.0F);
            z.bogen(8.0F, 8.0F, 5.5F, kPi * 0.15F, kPi * 0.95F);
            z.spitze(2.7F, 9.6F, -0.4F, -1.0F);
            break;
        case UiIcon::Ton:   // Lautsprecher mit Wellen
            z.zug({ImVec2{2.0F, 6.0F}, ImVec2{5.0F, 6.0F}, ImVec2{9.0F, 2.5F}, ImVec2{9.0F, 13.5F}, ImVec2{5.0F, 10.0F},
                   ImVec2{2.0F, 10.0F}}, true);
            z.bogen(9.5F, 8.0F, 3.0F, -kPi * 0.3F, kPi * 0.3F);
            z.bogen(9.5F, 8.0F, 5.5F, -kPi * 0.3F, kPi * 0.3F);
            break;
        case UiIcon::Folgen:   // Kamera mit Pfeil: die Ansicht faehrt mit
            z.rahmen(1.5F, 3.0F, 10.0F, 9.5F, 1.2F);
            z.zug({ImVec2{10.0F, 5.0F}, ImVec2{14.5F, 3.2F}, ImVec2{14.5F, 9.3F}, ImVec2{10.0F, 7.5F}});
            z.linie(2.5F, 13.0F, 13.0F, 13.0F);
            z.spitze(14.0F, 13.0F, 1.0F, 0.0F, 2.2F);
            break;
        case UiIcon::Zeilen:   // Personen-Zeilen
            z.punkt(3.0F, 4.0F, 1.6F);
            z.linie(6.0F, 4.0F, 14.5F, 4.0F);
            z.punkt(3.0F, 8.0F, 1.6F);
            z.linie(6.0F, 8.0F, 12.0F, 8.0F);
            z.punkt(3.0F, 12.0F, 1.6F);
            z.linie(6.0F, 12.0F, 13.5F, 12.0F);
            break;
        case UiIcon::Ruhig:   // Mond: ruhige Spuren
            z.bogen(8.0F, 8.0F, 6.0F, kPi * 0.35F, kPi * 1.65F);
            z.bogen(11.0F, 6.5F, 4.8F, kPi * 0.62F, kPi * 1.25F);
            break;
        case UiIcon::ZoomPlus:
        case UiIcon::ZoomMinus:   // Lupe mit Plus / Minus
            z.kreis(6.8F, 6.8F, 4.8F);
            z.linie(10.3F, 10.3F, 14.5F, 14.5F);
            z.linie(4.6F, 6.8F, 9.0F, 6.8F);
            if (icon == UiIcon::ZoomPlus) { z.linie(6.8F, 4.6F, 6.8F, 9.0F); }
            break;
        case UiIcon::Einpassen:   // vier Ecken nach aussen
            z.zug({ImVec2{1.5F, 5.5F}, ImVec2{1.5F, 1.5F}, ImVec2{5.5F, 1.5F}});
            z.zug({ImVec2{10.5F, 1.5F}, ImVec2{14.5F, 1.5F}, ImVec2{14.5F, 5.5F}});
            z.zug({ImVec2{14.5F, 10.5F}, ImVec2{14.5F, 14.5F}, ImVec2{10.5F, 14.5F}});
            z.zug({ImVec2{5.5F, 14.5F}, ImVec2{1.5F, 14.5F}, ImVec2{1.5F, 10.5F}});
            z.linie(5.5F, 8.0F, 10.5F, 8.0F);
            break;
        case UiIcon::Marke:   // Fahne auf Stab (ref_tag)
            z.linie(4.0F, 1.5F, 4.0F, 14.5F);
            z.flaeche({ImVec2{4.0F, 2.0F}, ImVec2{13.0F, 4.8F}, ImVec2{4.0F, 7.6F}});
            break;
        case UiIcon::Blitz:   // Ausloeser
            z.zug({ImVec2{9.5F, 1.0F}, ImVec2{3.5F, 9.0F}, ImVec2{8.0F, 9.0F}, ImVec2{6.5F, 15.0F}, ImVec2{12.5F, 7.0F},
                   ImVec2{8.0F, 7.0F}}, true);
            break;
        case UiIcon::Tuer:   // Tuer mit Klinke
            z.rahmen(3.5F, 1.5F, 12.5F, 14.5F, 0.8F);
            z.punkt(10.3F, 8.5F, 1.0F);
            break;
        case UiIcon::Wegpunkt:   // Stecknadel
            z.zug({ImVec2{8.0F, 15.0F}, ImVec2{3.6F, 8.6F}, ImVec2{3.2F, 5.5F}, ImVec2{5.0F, 2.4F}, ImVec2{8.0F, 1.3F},
                   ImVec2{11.0F, 2.4F}, ImVec2{12.8F, 5.5F}, ImVec2{12.4F, 8.6F}}, true);
            z.kreis(8.0F, 5.8F, 1.8F);
            break;
        case UiIcon::Pfad:   // drei Punkte, verbunden
            z.linie(3.0F, 12.5F, 8.0F, 4.0F);
            z.linie(8.0F, 4.0F, 13.0F, 11.0F);
            z.punkt(3.0F, 12.5F, 2.0F);
            z.punkt(8.0F, 4.0F, 2.0F);
            z.punkt(13.0F, 11.0F, 2.0F);
            break;
        case UiIcon::Oben:   // Raute von oben mit Pfeil nach unten
            z.zug({ImVec2{8.0F, 7.0F}, ImVec2{15.0F, 10.5F}, ImVec2{8.0F, 14.5F}, ImVec2{1.0F, 10.5F}}, true);
            z.linie(8.0F, 1.0F, 8.0F, 8.5F);
            z.spitze(8.0F, 8.5F, 0.0F, 1.0F, 2.4F);
            break;
        case UiIcon::Erzeugen:   // Zauberstab mit Funken
            z.linie(2.0F, 14.0F, 10.0F, 6.0F);
            z.linie(11.5F, 1.5F, 11.5F, 4.5F);
            z.linie(10.0F, 3.0F, 13.0F, 3.0F);
            z.linie(14.0F, 6.5F, 14.0F, 8.5F);
            z.linie(13.0F, 7.5F, 15.0F, 7.5F);
            break;
        case UiIcon::Animieren:   // zwei Schluesselrauten auf einer Bahn
            z.linie(1.5F, 12.5F, 14.5F, 3.5F);
            z.flaeche({ImVec2{4.0F, 8.0F}, ImVec2{6.8F, 10.8F}, ImVec2{4.0F, 13.6F}, ImVec2{1.2F, 10.8F}});
            z.flaeche({ImVec2{12.0F, 2.4F}, ImVec2{14.8F, 5.2F}, ImVec2{12.0F, 8.0F}, ImVec2{9.2F, 5.2F}});
            break;
        case UiIcon::Raster:   // Gitter
            z.rahmen(1.5F, 1.5F, 14.5F, 14.5F, 1.0F);
            z.linie(5.8F, 1.5F, 5.8F, 14.5F);
            z.linie(10.2F, 1.5F, 10.2F, 14.5F);
            z.linie(1.5F, 5.8F, 14.5F, 5.8F);
            z.linie(1.5F, 10.2F, 14.5F, 10.2F);
            break;
        case UiIcon::Sichtbar:   // offenes Auge
            z.zug({ImVec2{1.0F, 8.0F}, ImVec2{4.5F, 4.2F}, ImVec2{8.0F, 3.2F}, ImVec2{11.5F, 4.2F}, ImVec2{15.0F, 8.0F},
                   ImVec2{11.5F, 11.8F}, ImVec2{8.0F, 12.8F}, ImVec2{4.5F, 11.8F}}, true);
            z.punkt(8.0F, 8.0F, 2.2F);
            break;
        case UiIcon::Versteckt:   // geschlossenes Auge, durchgestrichen
            z.zug({ImVec2{1.0F, 8.0F}, ImVec2{4.5F, 10.8F}, ImVec2{8.0F, 11.8F}, ImVec2{11.5F, 10.8F}, ImVec2{15.0F, 8.0F}});
            z.linie(2.5F, 13.5F, 13.5F, 2.5F);
            break;
        case UiIcon::Bildschirm:   // Bild mit eingesetztem kleinem Bild unten rechts
            z.rahmen(1.5F, 2.5F, 14.5F, 13.5F, 1.0F);
            z.flaeche({ImVec2{8.5F, 8.0F}, ImVec2{13.0F, 8.0F}, ImVec2{13.0F, 12.0F}, ImVec2{8.5F, 12.0F}});
            break;
        case UiIcon::Bruch:   // Kiste mit Riss
            z.rahmen(2.0F, 3.0F, 14.0F, 14.0F, 0.8F);
            z.zug({ImVec2{7.5F, 3.0F}, ImVec2{6.0F, 7.0F}, ImVec2{9.0F, 9.0F}, ImVec2{7.0F, 14.0F}});
            break;
        case UiIcon::Warnung:
            z.zug({ImVec2{8.0F, 1.5F}, ImVec2{15.0F, 14.0F}, ImVec2{1.0F, 14.0F}}, true);
            z.linie(8.0F, 6.0F, 8.0F, 9.8F);
            z.punkt(8.0F, 12.0F, 0.9F);
            break;
    }
}

// Ein quadratischer Knopf mit Icon. `an`: als eingeschaltet zeichnen (Akzent).
// Die Kennung ist der Text - Tooltip und Selbsttest finden ihn darueber.
bool iconKnopf(const char* text, UiIcon icon, const char* hinweis, bool an, bool aktiv, bool umschalter) {
    const float h = ImGui::GetFrameHeight();
    ImGui::BeginDisabled(!aktiv);
    const bool geklickt = ImGui::InvisibleButton(text, ImVec2{h, h});
    const bool ueber = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool gedrueckt = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    const ImGuiStyle& st = ImGui::GetStyle();
    ImVec4 grund = st.Colors[ImGuiCol_Button];
    grund.w *= 0.0F;   // ruhend ohne Flaeche - moderner, ruhiger
    if (an) {
        grund = st.Colors[ImGuiCol_ButtonActive];
        grund.w = std::max(grund.w, 0.55F);
    }
    if (ueber && aktiv) { grund = st.Colors[gedrueckt ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered]; }
    if (grund.w > 0.0F) { dl->AddRectFilled(a, b, ImGui::GetColorU32(grund), st.FrameRounding + 2.0F); }
    // In seiner Farbe; ausgeschaltet gedaempft, gesperrt grau.
    ImVec4 farbe = aktiv ? uiIconFarbe(icon) : st.Colors[ImGuiCol_TextDisabled];
    if (aktiv && !an && !ueber && umschalter) { farbe.w *= 0.55F; }
    const float g = std::floor(h * 0.62F);
    // Auf ganze Bildpunkte - sonst sitzt das Icon einen halben Punkt daneben
    // und wirkt verwaschen und nicht mittig (shank, 11.10.).
    uiIcon(dl, icon, ImVec2{std::floor(a.x + (h - g) * 0.5F), std::floor(a.y + (h - g) * 0.5F)}, g,
           ImGui::GetColorU32(farbe));
    // Der Hinweis nennt zuerst, was der Knopf IST - ein Icon allein sagt es
    // nicht jedem.
    if (ueber) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(text, ImGui::FindRenderedTextEnd(text));
        if (hinweis != nullptr && hinweis[0] != '\0') {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0F);
            ImGui::TextDisabled("%s", hinweis);
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTooltip();
    }
    ImGui::EndDisabled();
    return geklickt && aktiv;
}

// Ein Knopf mit Icon und Text. Der Text ist zugleich die Kennung.
bool iconTextKnopf(const char* text, UiIcon icon, const char* hinweis, bool aktiv) {
    const float h = ImGui::GetFrameHeight();
    const ImGuiStyle& st = ImGui::GetStyle();
    const char* ende = ImGui::FindRenderedTextEnd(text);
    const float tw = ImGui::CalcTextSize(text, ende).x;
    const float g = std::floor(h * 0.62F);
    const float breite = st.FramePadding.x * 2.0F + g + st.ItemInnerSpacing.x + tw;
    ImGui::BeginDisabled(!aktiv);
    const bool geklickt = ImGui::InvisibleButton(text, ImVec2{breite, h});
    const bool ueber = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool gedrueckt = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    ImVec4 grund = st.Colors[ImGuiCol_Button];
    if (ueber && aktiv) { grund = st.Colors[gedrueckt ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered]; }
    dl->AddRectFilled(a, b, ImGui::GetColorU32(grund), st.FrameRounding);
    const ImU32 farbe = ImGui::GetColorU32(st.Colors[aktiv ? ImGuiCol_Text : ImGuiCol_TextDisabled]);
    const ImU32 iconFarbe = aktiv ? ImGui::GetColorU32(uiIconFarbe(icon)) : farbe;
    uiIcon(dl, icon, ImVec2{std::floor(a.x + st.FramePadding.x), std::floor(a.y + (h - g) * 0.5F)}, g, iconFarbe);
    dl->AddText(ImVec2{a.x + st.FramePadding.x + g + st.ItemInnerSpacing.x, a.y + st.FramePadding.y}, farbe, text, ende);
    if (ueber && hinweis != nullptr && hinweis[0] != '\0') {
        ImGui::SetTooltip("%s", hinweis);
    }
    ImGui::EndDisabled();
    return geklickt && aktiv;
}

}  // namespace bhed::gui
