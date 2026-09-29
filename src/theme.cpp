#include "bhed/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bhed::theme {
namespace {

constexpr Color rgb(int r, int g, int b, float a = 1.0F) {
    return Color{static_cast<float>(r) / 255.0F, static_cast<float>(g) / 255.0F,
                 static_cast<float>(b) / 255.0F, a};
}

// Von sRGB in den linearen Raum, wie WCAG es vorschreibt. Der naive Weg -
// die Kanaele einfach mitteln - liefert bei dunklen Farben deutlich zu
// guenstige Werte.
float linearize(float channel) {
    return channel <= 0.03928F ? channel / 12.92F
                               : std::pow((channel + 0.055F) / 1.055F, 2.4F);
}

}  // namespace

std::string Color::toHex() const {
    auto to255 = [](float v) {
        return static_cast<int>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F));
    };
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", to255(r), to255(g), to255(b));
    return buffer;
}

float relativeLuminance(const Color& c) {
    return 0.2126F * linearize(c.r) + 0.7152F * linearize(c.g) + 0.0722F * linearize(c.b);
}

float contrastRatio(const Color& a, const Color& b) {
    const float la = relativeLuminance(a);
    const float lb = relativeLuminance(b);
    const float hi = std::max(la, lb);
    const float lo = std::min(la, lb);
    return (hi + 0.05F) / (lo + 0.05F);
}

float worstContrast(const Theme& t) {
    return std::min({contrastRatio(t.text, t.window),
                     contrastRatio(t.text, t.child),
                     contrastRatio(t.text, t.button),
                     contrastRatio(t.text, t.frame)});
}

const std::vector<Theme>& themes() {
    static const std::vector<Theme> kThemes = [] {
        std::vector<Theme> v;

        {   // Windows 95 / 2000 - so sah BehavEd 2003 aus.
            // COLOR_BTNFACE ist D4D0C8, die Kanten FFFFFF / 808080 / 404040.
            Theme t;
            t.id = "behaved-classic";
            t.nameId = i18n::Str::ThemeBehavEdClassic;
            t.window = rgb(0xD4, 0xD0, 0xC8);
            t.child = rgb(0xFF, 0xFF, 0xFF);
            t.text = rgb(0x00, 0x00, 0x00);
            t.textDisabled = rgb(0x80, 0x80, 0x80);
            t.button = rgb(0xD4, 0xD0, 0xC8);
            t.buttonHovered = rgb(0xDF, 0xDF, 0xDF);
            t.buttonActive = rgb(0xA0, 0x9C, 0x96);
            t.frame = rgb(0xFF, 0xFF, 0xFF);
            t.frameHovered = rgb(0xFF, 0xFF, 0xFF);
            t.border = rgb(0x80, 0x80, 0x80);
            t.header = rgb(0x00, 0x00, 0x80);
            t.accent = rgb(0x00, 0x00, 0x80);
            t.raisedBorders = true;
            v.push_back(t);
        }
        {   // So sieht BehavEd heute unter Windows 11 aus - abgelesen von den
            // Bildschirmfotos: Flaeche F0F0F0, Listen weiss, Rahmen duenn.
            Theme t;
            t.id = "windows";
            t.nameId = i18n::Str::ThemeWindows;
            t.window = rgb(0xF0, 0xF0, 0xF0);
            t.child = rgb(0xFF, 0xFF, 0xFF);
            t.text = rgb(0x1A, 0x1A, 0x1A);
            t.textDisabled = rgb(0x8A, 0x8A, 0x8A);
            t.button = rgb(0xE1, 0xE1, 0xE1);
            t.buttonHovered = rgb(0xE5, 0xF1, 0xFB);
            t.buttonActive = rgb(0xCC, 0xE4, 0xF7);
            t.frame = rgb(0xFF, 0xFF, 0xFF);
            t.frameHovered = rgb(0xFF, 0xFF, 0xFF);
            t.border = rgb(0xAD, 0xAD, 0xAD);
            t.header = rgb(0x00, 0x78, 0xD7);
            t.accent = rgb(0x00, 0x67, 0xC0);
            v.push_back(t);
        }
        {
            Theme t;
            t.id = "dark";
            t.nameId = i18n::Str::ThemeDark;
            t.window = rgb(0x2B, 0x2B, 0x2E);
            t.child = rgb(0x1E, 0x1E, 0x21);
            t.text = rgb(0xE6, 0xE6, 0xE6);
            t.textDisabled = rgb(0x8A, 0x8A, 0x90);
            t.button = rgb(0x3A, 0x3A, 0x3F);
            t.buttonHovered = rgb(0x4A, 0x4A, 0x52);
            t.buttonActive = rgb(0x5A, 0x5A, 0x64);
            t.frame = rgb(0x25, 0x25, 0x29);
            t.frameHovered = rgb(0x30, 0x30, 0x36);
            t.border = rgb(0x4A, 0x4A, 0x50);
            t.header = rgb(0x2D, 0x5A, 0x8C);
            t.accent = rgb(0x4C, 0x8E, 0xDA);
            v.push_back(t);
        }
        {
            Theme t;
            t.id = "midnight";
            t.nameId = i18n::Str::ThemeMidnight;
            t.window = rgb(0x14, 0x18, 0x24);
            t.child = rgb(0x0D, 0x10, 0x1A);
            t.text = rgb(0xD8, 0xDE, 0xEC);
            t.textDisabled = rgb(0x76, 0x7E, 0x94);
            t.button = rgb(0x20, 0x27, 0x3A);
            t.buttonHovered = rgb(0x2C, 0x35, 0x4E);
            t.buttonActive = rgb(0x38, 0x44, 0x62);
            t.frame = rgb(0x18, 0x1D, 0x2C);
            t.frameHovered = rgb(0x22, 0x2A, 0x3E);
            t.border = rgb(0x33, 0x3D, 0x56);
            t.header = rgb(0x24, 0x46, 0x7A);
            t.accent = rgb(0x5A, 0x9B, 0xE8);
            v.push_back(t);
        }
        {
            Theme t;
            t.id = "light";
            t.nameId = i18n::Str::ThemeLight;
            t.window = rgb(0xF7, 0xF7, 0xF5);
            t.child = rgb(0xFF, 0xFF, 0xFF);
            t.text = rgb(0x1C, 0x1C, 0x1C);
            t.textDisabled = rgb(0x88, 0x88, 0x88);
            t.button = rgb(0xE8, 0xE8, 0xE4);
            t.buttonHovered = rgb(0xDC, 0xDC, 0xD8);
            t.buttonActive = rgb(0xC8, 0xC8, 0xC2);
            t.frame = rgb(0xFF, 0xFF, 0xFF);
            t.frameHovered = rgb(0xF2, 0xF2, 0xEE);
            t.border = rgb(0xBB, 0xBB, 0xB4);
            t.header = rgb(0xBD, 0xD6, 0xF2);
            t.accent = rgb(0x1F, 0x6F, 0xB8);
            v.push_back(t);
        }
        {
            Theme t;
            t.id = "high-contrast";
            t.nameId = i18n::Str::ThemeHighContrast;
            t.window = rgb(0x00, 0x00, 0x00);
            t.child = rgb(0x00, 0x00, 0x00);
            t.text = rgb(0xFF, 0xFF, 0xFF);
            t.textDisabled = rgb(0xB0, 0xB0, 0xB0);
            t.button = rgb(0x1A, 0x1A, 0x1A);
            t.buttonHovered = rgb(0x33, 0x33, 0x33);
            t.buttonActive = rgb(0x4D, 0x4D, 0x4D);
            t.frame = rgb(0x0A, 0x0A, 0x0A);
            t.frameHovered = rgb(0x1A, 0x1A, 0x1A);
            t.border = rgb(0xFF, 0xFF, 0x00);
            t.header = rgb(0x00, 0x00, 0xAA);
            t.accent = rgb(0xFF, 0xFF, 0x00);
            v.push_back(t);
        }
        return v;
    }();
    return kThemes;
}

const Theme* findTheme(const std::string& id) {
    for (const Theme& t : themes()) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

}  // namespace bhed::theme
