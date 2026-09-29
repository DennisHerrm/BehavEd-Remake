// clock.h - EINE Uhr fuer die ganze Vorschau.
//
// Ein ICARUS-Skript wird an drei Stellen ausgewertet:
//
//   camtrack.cpp   wo steht die Kamera zu Zeitpunkt t
//   timeline.cpp   welcher Befehl liegt wann auf der Leiste
//   scene.cpp      wo steht jede Figur zu Zeitpunkt t
//
// Jede dieser Stellen hatte ihre EIGENE Rechnung dafuer, wie lange ein
// Befehl die Uhr anhaelt - und die drei waren sich nicht einig:
//
//   Befehl                camtrack        timeline          scene
//   wait ( 3000 )         3000            3000              3000
//   wait ( $random$ )     0               Mitte             Mitte
//   wait ( "name" )       0               1000 (geraten)    bis zum Ende
//                                                           des eigenen Zugs
//   waitsignal ( "x" )    0               0                 bis zum Signal
//
// Zehn der 284 Skripte der Mod enthalten sowohl waitsignal als auch
// camera-Befehle. Dort lief die Kamera vor den Figuren her, weil die eine
// Uhr das Warten kannte und die andere nicht.
//
// Epic hat denselben Weg genommen und beschreibt ihn im Rueckblick auf
// Sequencer 4.26: vorher hatte jede Spur ihre eigene Auswertung, danach
// laeuft alles durch EINE. Der Grund ist derselbe - nicht Geschwindigkeit,
// sondern dass mehrere Auswertungen desselben Ablaufs auseinanderlaufen.
#ifndef BHED_CLOCK_H
#define BHED_CLOCK_H

#include "bhed/script.h"

#include <map>
#include <string>

namespace bhed {

// Wann faellt welches Signal? Name -> Millisekunden ab Skriptbeginn.
using SignalTimes = std::map<std::string, double>;

// Eine Zahl aus einem Argument lesen.
//
// Ist es ein Ausdruck $random( a, b )$, kommt die MITTE heraus. Im Spiel
// wuerfelt die Engine einmal und behaelt den Wert; fuer eine Vorschau ist
// die Mitte die einzige brauchbare Wahl, weil sie wiederholbar ist. Eine
// Zeitleiste, die bei jedem Blick anders aussieht, taugt nicht zum
// Abstimmen.
[[nodiscard]] double readMs(const std::string& text);

// Alle Signale eines Skripts einsammeln, mit dem fruehesten Zeitpunkt.
//
// Ein Durchgang, der nur die Zeit fortschreibt - ohne Figuren, ohne Kamera.
// Danach koennen alle drei Auswerter dieselbe Antwort auf waitsignal geben.
//
// Warum ein eigener Durchgang und keine Vorwaertsschau: ein Signal kann in
// einem SPAETEREN affect-Block fallen als das Warten darauf, und die Bloecke
// haben unabhaengige Uhren.
[[nodiscard]] SignalTimes collectSignals(const Script& s);

// Wie weit haelt dieser Befehl die Uhr an?
//
// now ist die aktuelle Zeit des Ablaufs, ownEndMs das Ende dessen, was
// dieser Ablauf zuletzt angestossen hat (fuer wait mit Namen). Gibt die
// NEUE Zeit zurueck, nicht die Dauer - bei waitsignal ist das ein Sprung
// nach vorn und keine Addition.
//
// Nicht behandelte Befehle geben now unveraendert zurueck.
[[nodiscard]] double advance(const Node& n, double now, double ownEndMs,
                             const SignalTimes& signals);

}  // namespace bhed
#endif
