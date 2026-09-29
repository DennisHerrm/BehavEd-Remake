// interplay.h - was Skripte miteinander verbindet
//
// Mehrere Skripte einer Mission laufen gleichzeitig und muessen
// zusammenspielen. Im OpenJK-Quelltext nachgesehen, worueber:
//
//   * SIGNALE. CIcarus::Signal() schreibt in EINE Tabelle m_signals auf der
//     Instanz, nicht in eine je Skript. Ein Skript sendet, ein anderes
//     haengt in waitsignal und laeuft weiter. Das ist der uebliche Weg,
//     nebenlaeufige Skripte zu verzahnen.
//
//   * DIE KAMERA. In cg_camera.cpp steht ein einziges `camera_t
//     client_camera`. Es gibt keine Kamera je Skript: rufen zwei Skripte
//     gleichzeitig camera auf, ueberschreiben sie einander, und der letzte
//     Befehl gewinnt. Deshalb treibt in der Praxis genau EINES die Kamera.
//
//   * run(). Startet ein anderes Skript aus einem heraus.
//
// Diese Auswertung sammelt genau das - je Skript, damit die Oberflaeche
// mehrere gegenueberstellen kann.
#ifndef BHED_INTERPLAY_H
#define BHED_INTERPLAY_H

#include "bhed/script.h"

#include <string>
#include <vector>

namespace bhed {

// Was ein einzelnes Skript nach aussen tut und von aussen erwartet.
struct ScriptFacts {
    std::vector<std::string> signalsSent;      // signal( "x" )
    std::vector<std::string> signalsAwaited;   // waitsignal( "x" )
    std::vector<std::string> runs;             // run( "skript" )
    int cameraCommands = 0;                    // wie oft camera( ... )
};

// Ein Skript durchgehen, auch in Bloecken.
//
// Namen werden ohne Ruecksicht auf Gross- und Kleinschreibung erkannt: in
// den Quellen steht mal SIGNAL, mal signal, und das Spiel nimmt beides.
// Doppelte Eintraege stehen nur einmal darin - wer dreimal dasselbe Signal
// sendet, sendet dasselbe Signal.
[[nodiscard]] ScriptFacts scanScript(const Script& s);

// Ein Skript mit Namen, wie es die Uebersicht braucht.
struct NamedFacts {
    std::string name;
    ScriptFacts facts;
};

// Ein Signal ueber alle Skripte hinweg.
struct SignalLink {
    std::string name;
    std::vector<std::string> senders;   // wer es sendet
    std::vector<std::string> waiters;   // wer darauf wartet
};

// Alle Signale zusammenstellen, alphabetisch.
//
// Interessant sind vor allem die halben: ein Signal OHNE Empfaenger geht ins
// Leere, und eines OHNE Sender laesst ein Skript ewig warten. Beides ist
// hier daran zu erkennen, dass eine der beiden Listen leer ist.
//
// Achtung bei der Deutung: fehlt ein Sender, kann er auch in einem Skript
// stehen, das gerade nicht offen ist - oder die Engine sendet ihn selbst.
[[nodiscard]] std::vector<SignalLink> linkSignals(
    const std::vector<NamedFacts>& scripts);

// Welche Skripte die Kamera anfassen.
//
// Mehr als eines ist ein Warnzeichen, kein Fehler: es kann sein, dass sie
// nacheinander laufen. Gleichzeitig aber kaempfen sie um dieselbe globale
// Kamera, und das gibt im Spiel unerklaerliche Spruenge.
[[nodiscard]] std::vector<std::string> cameraScripts(
    const std::vector<NamedFacts>& scripts);

}   // namespace bhed

#endif
