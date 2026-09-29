// Erzeugt von tools/gen_icons.py - nicht von Hand aendern.
//
// Symbolstreifen BITMAP 132 aus BehavEd.exe: 40 Symbole zu 16x16,
// als RGBA. Weiss ist durchsichtig (Maskenfarbe des Originals).
#pragma once

#include <cstddef>
#include <cstdint>

namespace bhed::icons {

constexpr int kSize = 16;
constexpr int kCount = 40;

// Reihenfolge wie in ICON_OVERRIDES der behaved.bhc. Gerader Index:
// Normalfassung, ungerader: fuer die ausgewaehlte Zeile.
constexpr const char* kNames[] = {
    "I_BRACE",
    "I_BRACE_ALT",
    "I_EVENT",
    "I_EVENT_ALT",
    "I_MACRO",
    "I_MACRO_ALT",
    "I_SPACE",
    "I_SPACE_ALT",
    "I_SOUND",
    "I_SOUND_ALT",
    "I_CAMERA",
    "I_CAMERA_ALT",
    "I_ROTATE",
    "I_ROTATE_ALT",
    "I_REMOVE",
    "I_REMOVE_ALT",
    "I_SET",
    "I_SET_ALT",
    "I_MOVE",
    "I_MOVE_ALT",
    "I_IF",
    "I_IF_ALT",
    "I_LOOP",
    "I_LOOP_ALT",
    "I_DO",
    "I_DO_ALT",
    "I_WAIT",
    "I_WAIT_ALT",
    "I_DOWAIT",
    "I_DOWAIT_ALT",
    "I_SIGNAL",
    "I_SIGNAL_ALT",
    "I_WAITSIGNAL",
    "I_WAITSIGNAL_ALT",
    "I_FLUSH",
    "I_FLUSH_ALT",
    "I_WAITCLOCK",
    "I_WAITCLOCK_ALT",
    "I_UNNAMED19",
    "I_UNNAMED19_ALT",
};

constexpr std::size_t kPixelBytes = 40960;
extern const std::uint8_t kPixels[kPixelBytes];

// Index zu einem Namen, oder -1.
int indexOf(const char* name);


// Das Logo aus dem Ueber-Dialog des Originals (BITMAP 133).
constexpr int kLogoWidth = 133;
constexpr int kLogoHeight = 54;
constexpr std::size_t kLogoBytes = 28728;
extern const std::uint8_t kLogoPixels[kLogoBytes];

}  // namespace bhed::icons
