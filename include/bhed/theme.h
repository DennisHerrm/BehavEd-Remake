// theme.h - Farbgebungen
//
// Uebernommen aus efxed, ohne den Himmel-Anteil (wir zeichnen keine Szene).
// Neu sind zwei Farbgebungen, die es dort nicht gab:
//   BehavEd Classic - das Grau von Windows 95, wie das Werkzeug 2003 aussah
//   Windows         - das helle Aussehen, das BehavEd unter Windows 11 hat
//                     (so steht es auf den Bildschirmfotos vom 11.08.2026)
#ifndef BHED_THEME_H
#define BHED_THEME_H

#include "bhed/i18n.h"

#include <string>
#include <vector>

namespace bhed::theme {

struct Color {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
    [[nodiscard]] std::string toHex() const;
};

struct Theme {
    std::string id;        // fuer die Einstellungsdatei
    i18n::Str nameId{};    // uebersetzter Anzeigename
    [[nodiscard]] const char* name() const { return i18n::tr(nameId); }

    Color window;          // Fensterflaeche
    Color child;           // Listen- und Baumflaeche
    Color text;
    Color textDisabled;
    Color button;
    Color buttonHovered;
    Color buttonActive;
    Color frame;           // Eingabefelder
    Color frameHovered;
    Color border;
    Color header;          // ausgewaehlte Zeile
    Color accent;          // Hervorhebung, Fortschritt
    bool raisedBorders = false;  // 3D-Kanten wie unter Windows 95
};

const std::vector<Theme>& themes();
const Theme* findTheme(const std::string& id);

// Relative Helligkeit nach WCAG.
[[nodiscard]] float relativeLuminance(const Color& c);
// Kontrastverhaeltnis nach WCAG, 1:1 bis 21:1.
[[nodiscard]] float contrastRatio(const Color& a, const Color& b);

// Prueft eine Farbgebung: Text auf Flaeche, Text auf Liste, Knopfbeschriftung.
// Rueckgabe ist das schlechteste gefundene Verhaeltnis.
[[nodiscard]] float worstContrast(const Theme& t);

}  // namespace bhed::theme
#endif
