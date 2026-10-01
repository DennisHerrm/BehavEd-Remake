// update.h - der Auto-Updater in der Oberflaeche (Netz, Installation, Fenster)
//
// Der pruefbare Kern steht in include/bhed/update.h. Hier: GitHub fragen
// (WinHTTP, im Hintergrund), das .zip laden und neben die .exe entpacken,
// neu starten - und was man davon sieht: ein Hinweis in der Statuszeile und
// ein kleines Fenster mit den Notizen des Releases.
#ifndef BHED_GUI_UPDATE_H
#define BHED_GUI_UPDATE_H

#include <string>

#include "bhed/update.h"

namespace bhed::gui::updater {

enum class Stand { Ruhe, Pruefe, Aktuell, Verfuegbar, Laedt, Fertig, Fehler };

struct Zustand {
    Stand stand = Stand::Ruhe;
    std::string meldung;          // bei Fehler: was schiefging
    bhed::update::Release release;
    double fortschritt = 0.0;     // 0..1 beim Laden
    int dateien = 0;              // so viele wurden ersetzt
};

// Beim Programmstart: Reste eines frueheren Updates wegraeumen und - wenn
// eingeschaltet und kein Selbsttest laeuft - im Hintergrund nachsehen.
void beimStart();
// Nachsehen. `stumm`: nur der Hinweis in der Statuszeile, kein Fenster.
void pruefen(bool stumm);
// Das gefundene Release laden und installieren (im Hintergrund).
void installieren();
// Neu starten (fragt vorher nach ungespeicherten Aenderungen).
void neuStarten();
// Eine Kopie des Zustands (der Hintergrundfaden schreibt ihn).
[[nodiscard]] Zustand zustand();
// Die Fassung, gegen die verglichen wird: kFassung, im Selbsttest
// ueberschreibbar mit BHED_UPDATE_LOKAL.
[[nodiscard]] std::string lokaleFassung();

// Einmal je Bild: das Fenster (wenn offen) und der Hinweis unten.
void zeichneFenster();
void zeichneStatus();
void oeffneFenster();
[[nodiscard]] bool fensterOffen();

// Aus main: "--nach-update=<pid>" - auf das Ende der alten Instanz warten,
// damit sie ihre Einstellungen fertig schreibt, bevor diese sie liest.
// true, wenn das Argument dieses war (dann ist es keine Datei zum Oeffnen).
bool warteAufVorgaenger(const std::string& argument);

}  // namespace bhed::gui::updater

#endif  // BHED_GUI_UPDATE_H
