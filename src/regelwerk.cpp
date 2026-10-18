// regelwerk.cpp - rc601 integ: der zentrale Schalter "OpenJK / Movie Duels".
// Siehe bhed/regelwerk.h.
#include "bhed/regelwerk.h"

#include "bhed/navlauf.h"
#include "bhed/saberkontakt.h"

#include <atomic>

namespace bhed {

namespace {
// Ohne regelwerkAnwenden: Movie Duels, g_spskill 2 - so rechneten die Pakete
// bisher, und ihre Unit-Tests setzen das voraus.
std::atomic<bool> gMd{true};
std::atomic<int> gStufe{2};
}  // namespace

const char* regelwerkSchluessel(int wahl) noexcept {
    switch (wahl) {
        case kRegelwerkOpenJK: return "openjk";
        case kRegelwerkMovieDuels: return "movieduels";
        default: return "auto";
    }
}

bool regelwerkAusSchluessel(const std::string& text, int& wahl) noexcept {
    if (text == "auto") {
        wahl = kRegelwerkAuto;
    } else if (text == "openjk") {
        wahl = kRegelwerkOpenJK;
    } else if (text == "movieduels") {
        wahl = kRegelwerkMovieDuels;
    } else {
        return false;
    }
    return true;
}

bool regelwerkAufloesen(int wahl, bool movieDuelsErkannt) noexcept {
    if (wahl == kRegelwerkOpenJK) { return false; }
    if (wahl == kRegelwerkMovieDuels) { return true; }
    // Automatisch: der Betreuer - "wenn der Ordner unklar ist, OpenJK".
    return movieDuelsErkannt;
}

bool regelwerkMovieDuels() noexcept { return gMd.load(std::memory_order_relaxed); }

void regelwerkAnwenden(bool movieDuels, int schwierigkeitEinstellung) noexcept {
    const int stufe = schwierigkeitWirksam(schwierigkeitEinstellung, movieDuels);
    gMd.store(movieDuels, std::memory_order_relaxed);
    gStufe.store(stufe, std::memory_order_relaxed);
    // Die Schalter der Pakete - sie bleiben die Stellen, die deren Code fragt.
    setzeSaberMovieDuelsRegeln(movieDuels);
    setzeSaberSpielstufe(stufe);
    navMovieDuelsRegeln(movieDuels);
}

int regelwerkSpielstufe() noexcept { return gStufe.load(std::memory_order_relaxed); }

int schwierigkeitWirksam(int einstellung, bool movieDuels) noexcept {
    if (einstellung >= 0 && einstellung <= 3) { return einstellung; }
    // g_spskill: MD gi.cvar("g_spskill", "2", ...) (g_main.cpp:752),
    // OpenJK gi.cvar("g_spskill", "0", ...) (g_main.cpp:631).
    return movieDuels ? 2 : 0;
}

bool fovAspectAdjustWirksam(int einstellung, bool movieDuels) noexcept {
    if (einstellung == 0 || einstellung == 1) { return einstellung == 1; }
    // cg_fovAspectAdjust: MD "1" (cg_main.cpp:555), OpenJK "0" (cg_main.cpp:355).
    return movieDuels;
}

float spielSeitenWirksam(float einstellung, bool movieDuels, float bildschirm) noexcept {
    if (einstellung > 0.0F) { return einstellung; }
    if (movieDuels) {
        // r_mode "-2" (sdl_window.cpp:779 MD): die Desktopaufloesung.
        return bildschirm > 0.0F ? bildschirm : 16.0F / 9.0F;
    }
    // r_mode "4" (sdl_window.cpp:746 OpenJK): 800x600.
    return 4.0F / 3.0F;
}

bool roffKlangFolgtEntity(bool movieDuels, bool mover) noexcept {
    // MD g_roff.cpp:262-266: immer cgi_S_StartSound(cent->s.pos.trBase, ...).
    // OpenJK g_roff.cpp:297-306: ET_MOVER fest am Ort, sonst G_SoundOnEnt.
    return !movieDuels && !mover;
}

}  // namespace bhed
