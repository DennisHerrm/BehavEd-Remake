// Messpunkte fuer den Profiler Tracy (rc596).
//
// Nur mit -DBEHAVED_TRACY=ON (holt Tracy beim Konfigurieren); dann laesst
// sich behaved mit dem Tracy-Programm verbinden und zeigt je Bild, welcher
// Abschnitt wie lange brauchte. Ohne die Option sind die Makros leer - die
// ausgelieferte .exe enthaelt nichts davon.
//
// Bis rc595 wurde fuer jede Leistungsfrage ein Stichproben-Profiler
// eingebaut und wieder entfernt. Mit festen Messpunkten genuegt es, die
// Option einzuschalten.
#pragma once

#ifdef BHED_TRACY
#include <tracy/Tracy.hpp>
#define BHED_MESS(name) ZoneScopedN(name)
#define BHED_BILD() FrameMark
#else
#define BHED_MESS(name) static_cast<void>(0)
#define BHED_BILD() static_cast<void>(0)
#endif
