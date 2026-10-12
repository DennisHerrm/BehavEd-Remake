// Feste Arbeitsfaeden fuer kurze, gleichartige Aufgaben (rc596).
//
// Bis rc595 legte jede Stelle, die Strahltests oder Effektbahnen verteilt,
// je Aufruf neue std::thread an und wartete auf sie - mehrmals je Bild. Ein
// Faden kostet beim Anlegen und Beenden jedes Mal zig Mikrosekunden, und im
// Profil stand das Warten darauf (Thrd_join) mit bis zu einem Viertel der
// Bildzeit beim Fliegen ueber die Karte.
//
// Hier werden die Faeden einmal angelegt und warten danach. verteilt() gibt
// ihnen eine Aufgabe, der Aufrufer arbeitet mit und kehrt zurueck, wenn alle
// Plaetze erledigt sind. Jeder holt sich den naechsten Platz selbst - die
// Aufgaben sind verschieden teuer.
//
// Ruft eine Aufgabe ihrerseits verteilt() auf (oder laeuft gerade schon
// eine), wird die innere einfach im aufrufenden Faden erledigt: nie zwei
// Auftraege gleichzeitig, also auch kein gegenseitiges Warten.
#pragma once

#include <cstddef>
#include <functional>

namespace bhed::arbeiter {

// je(i) fuer i in [0, n). je darf nur Platz i beschreiben (oder sich selbst
// absichern). Kleine Mengen (unter `abWann`) laufen ohne Faeden.
void verteilt(std::size_t n, const std::function<void(std::size_t)>& je, std::size_t abWann = 16);

// Wie viele Faeden insgesamt mitarbeiten (Aufrufer eingeschlossen).
[[nodiscard]] unsigned anzahl();

}  // namespace bhed::arbeiter
