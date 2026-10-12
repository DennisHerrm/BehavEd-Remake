// Probe fuer die festen Arbeitsfaeden (bhed/arbeiter.h, rc596).
//
// Jeder Platz genau einmal, auch bei vielen Auftraegen hintereinander; ein
// verschachtelter Aufruf laeuft durch, statt zu haengen; zwei Faeden, die
// gleichzeitig verteilen, bekommen beide alles erledigt.
#include "bhed/arbeiter.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
int g_fehler = 0;
void pruefe(bool ok, const char* was) {
    std::printf("%s %s\n", ok ? "OK    " : "FEHLER", was);
    if (!ok) { ++g_fehler; }
}

bool jederGenauEinmal(std::size_t n) {
    std::vector<std::atomic<int>> zaehler(n);
    for (auto& z : zaehler) { z.store(0); }
    bhed::arbeiter::verteilt(n, [&](std::size_t i) { zaehler[i].fetch_add(1); });
    for (const auto& z : zaehler) {
        if (z.load() != 1) { return false; }
    }
    return true;
}
}  // namespace

int main() {
    std::printf("Arbeitsfaeden: %u\n", bhed::arbeiter::anzahl());
    pruefe(jederGenauEinmal(0), "nichts zu tun");
    pruefe(jederGenauEinmal(5), "wenige Plaetze (ohne Faeden)");
    pruefe(jederGenauEinmal(10000), "zehntausend Plaetze");

    bool alle = true;
    for (int r = 0; r < 2000 && alle; ++r) {
        alle = jederGenauEinmal(static_cast<std::size_t>(17 + (r % 300)));
    }
    pruefe(alle, "zweitausend Auftraege hintereinander");

    // Verschachtelt: innen laeuft es im aufrufenden Faden.
    std::atomic<int> innen{0};
    bhed::arbeiter::verteilt(64, [&](std::size_t) {
        bhed::arbeiter::verteilt(32, [&](std::size_t) { innen.fetch_add(1); });
    });
    pruefe(innen.load() == 64 * 32, "verschachtelter Aufruf");

    // Zwei Faeden zugleich: einer bekommt die Mannschaft, der andere rechnet selbst.
    std::atomic<int> a{0};
    std::atomic<int> b{0};
    std::thread zweiter([&] {
        for (int r = 0; r < 200; ++r) {
            bhed::arbeiter::verteilt(500, [&](std::size_t) { b.fetch_add(1); });
        }
    });
    for (int r = 0; r < 200; ++r) {
        bhed::arbeiter::verteilt(500, [&](std::size_t) { a.fetch_add(1); });
    }
    zweiter.join();
    pruefe(a.load() == 200 * 500 && b.load() == 200 * 500, "zwei Faeden verteilen gleichzeitig");

    std::printf("%s (%d Fehler)\n", g_fehler == 0 ? "ALLES OK" : "FEHLER", g_fehler);
    return g_fehler == 0 ? 0 : 1;
}
