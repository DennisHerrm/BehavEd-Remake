// Nimmt die Adresse jeder in gpumap.h erklaerten Funktion.
//
// Warum das reicht
// ----------------
// Eine Erklaerung im Kopf und eine Definition mit anderer Parameterliste
// sind zwei verschiedene Funktionen - der Uebersetzer beanstandet nichts,
// erst der Binder findet es. Und der lief bisher nur auf shanks Rechner.
//
// Zweimal hat ihn das einen Bau gekostet:
//
//   LNK2019: Verweis auf nicht aufgeloestes externes Symbol
//            bhed::gpu::zeichneKarte(...)
//
// Die Adresse einer Funktion zu nehmen verlangt, dass es sie GIBT. Zusammen
// mit gpumap_win32.cpp gebunden, faellt jede Abweichung hier auf - auf
// diesem Rechner, in einer Sekunde.
#include "gpumap.h"

namespace {
const void* const kAdressen[] = {
    reinterpret_cast<const void*>(&bhed::gpu::verfuegbar),
    reinterpret_cast<const void*>(&bhed::gpu::shutdown),
    reinterpret_cast<const void*>(&bhed::gpu::bereiteZiel),
    reinterpret_cast<const void*>(&bhed::gpu::zielTextur),
    reinterpret_cast<const void*>(&bhed::gpu::zeichneKarte),
    reinterpret_cast<const void*>(&bhed::gpu::zeichneMover),
    reinterpret_cast<const void*>(&bhed::gpu::zeichneFigur),
    reinterpret_cast<const void*>(&bhed::gpu::zeichneGluehen),
};
}  // namespace

int main() {
    for (const void* p : kAdressen) {
        if (p == nullptr) {
            return 1;
        }
    }
    return 0;
}
