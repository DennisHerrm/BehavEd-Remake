// Absturzabbild und Waechter (rc596).
//
// Der Absturzfilter in main_win32.cpp schreibt Code, Adresse und den letzten
// Schritt ins Protokoll. Was dort fehlt, ist der Zustand: welcher Aufruf
// wohin fuehrte, welche Werte die Variablen hatten. Den liefert ein
// Minidump (behaved-absturz.dmp neben dem Protokoll), mit Visual Studio oder
// WinDbg zu oeffnen.
//
// Geschrieben wird er von einem vorab angelegten Faden, nicht im
// abgestuerzten: dessen Stapel kann zerstoert sein, und DbgHelp ist nicht
// fadenfest (Microsoft, MiniDumpWriteDump).
//
// Der Waechter achtet auf den Hauptfaden: zaehlt die Schleife 30 Sekunden
// lang kein Bild weiter - und ist kein Dateidialog offen, der sie ganz
// legitim anhaelt -, steht ein Haenger im Protokoll, samt einem Abbild
// (behaved-haenger.dmp). Er beendet nichts; er schreibt nur auf, einmal je
// Sitzung.
#pragma once

#include <string>

struct _EXCEPTION_POINTERS;

namespace bhed::gui::wache {

// Beim Start, sobald der Protokollordner feststeht.
void starten(const std::string& ordner);
// Aus dem Absturzfilter: Abbild schreiben lassen und darauf warten.
void absturz(_EXCEPTION_POINTERS* info);
// Einmal je Durchlauf der Hauptschleife.
void lebenszeichen();
// Um alles, was die Hauptschleife absichtlich anhaelt (Dateidialoge,
// Meldungsfenster).
void modalBeginn();
void modalEnde();

// Fuer den Selbsttest: ein Abbild schreiben, als waere es ein Haenger, und
// sagen, ob die Datei entstanden ist.
[[nodiscard]] bool probeAbbild();

}  // namespace bhed::gui::wache
