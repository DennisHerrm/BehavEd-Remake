// eingebaut.h - data/base im Programm selbst (rc601 release)
//
// Gewuenscht (shank, 17.10.2026): "bei jedem Release sollte es auch eine
// .exe geben, die gebaut wird, dass die Leute sie runterladen und nutzen
// koennen". Eine behaved.exe ALLEIN lief bis rc600 aber nicht: ohne den
// Ordner data\base daneben kam "behaved.bhc not found in ..." und das
// Programm endete mit 2 (gui/main_win32.cpp).
//
// Jetzt stecken die Dateien von data/base (behaved.bhc, supplement.bhc und
// die elf Kopfdateien, zusammen etwa 156 KB) im Programm. CMake erzeugt sie
// beim Konfigurieren als eingebaute_daten.inc im Bauordner
// (tools/eingebaute_daten.cmake).
//
// Liegt data\base neben der .exe (das ausgepackte Zip), gilt weiter dieser
// Ordner - die eingebaute Kopie ist nur der Rueckfall. Sonst packt behaved
// sie nach <Einstellungsordner>\data\base aus (%APPDATA%\behaved, mit
// BHED_EINSTELLUNGEN ein eigener) und laedt von dort. Die Leser bleiben,
// wie sie sind: loadCommandDb liest weiter von der Platte.
#ifndef BHED_EINGEBAUT_H
#define BHED_EINGEBAUT_H

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace bhed::eingebaut {

struct Datei {
    const char* name;              // "behaved.bhc"
    const unsigned char* daten;
    std::size_t groesse;
};

// Alle eingebauten Dateien, nach Namen sortiert. Leer, wenn ohne CMake
// gebaut (tools/verify.sh uebersetzt src/*.cpp direkt) - dann gibt es den
// Rueckfall nicht, und alles verhaelt sich wie bis rc600.
[[nodiscard]] std::vector<Datei> dateien();

// Packt die eingebauten Dateien nach `ziel` aus; der Ordner wird angelegt.
// Eine Datei, die dort schon mit demselben Inhalt liegt, bleibt unberuehrt -
// eine fehlende oder veraenderte wird (ueber "<name>.neu") neu geschrieben.
// Dazu kommt fassung.txt mit `fassung`, damit man im Ordner sieht, von
// welchem Programm er stammt. false, wenn nichts eingebaut ist oder ein
// Schreiben scheitert; `fehler` sagt dann, woran - in den Worten des Systems
// ("Zugriff verweigert", "no space on device"), bei einer Datei mit ihrem
// Namen davor ("behaved.bhc: ..."), denn der Text steht auch im
// Meldungsfenster (auspackHinweis).
[[nodiscard]] bool auspacken(const std::filesystem::path& ziel, std::string_view fassung,
                             std::string* fehler = nullptr, int* geschrieben = nullptr);

// Woher das Befehlsmodell kommt.
struct Wahl {
    std::filesystem::path ordner;   // dort liegen behaved.bhc und die Kopfdateien
    bool eingebaut = false;         // aus dem Programm ausgepackt
    int geschrieben = 0;            // so viele Dateien wurden dabei neu geschrieben
    std::string fehler;             // warum der Rueckfall nicht ging (sonst leer)
    // rc601 release (Durchsicht): wohin ausgepackt werden sollte - leer, wenn
    // es gar nicht versucht wurde (data\base neben der .exe, nichts
    // eingebaut, kein Einstellungsordner).
    std::filesystem::path auspackZiel;
};

// <exeOrdner>/data/base, wenn dort behaved.bhc liegt - wie bisher. Sonst die
// eingebauten Dateien, ausgepackt nach <einstellungsOrdner>/data/base. Geht
// auch das nicht (nichts eingebaut, kein Einstellungsordner, nicht
// schreibbar), kommt <exeOrdner>/data/base zurueck: der Aufrufer meldet dann,
// dass die Daten fehlen, und nennt den Ordner, in dem sie fehlen.
[[nodiscard]] Wahl datenOrdnerWaehlen(const std::filesystem::path& exeOrdner,
                                      const std::filesystem::path& einstellungsOrdner,
                                      std::string_view fassung);

// rc601 release (Durchsicht): die zweite Zeile des Meldungsfensters, wenn
// das Auspacken versucht wurde und scheiterte (Platte voll,
// Einstellungsordner schreibgeschuetzt oder umgeleitet) - in der
// eingestellten Sprache (Str::DataUnpackFailed), mit dem Ordner und dem
// Grund. Vorher stand dann nur "das ganze Zip entpacken" mit dem Ordner neben
// der .exe da, und der Grund ging nur ins Protokoll. Leer, wenn es nichts zu
// sagen gibt (ausgepackt, oder gar nicht versucht).
[[nodiscard]] std::string auspackHinweis(const Wahl& wahl);

}  // namespace bhed::eingebaut

#endif  // BHED_EINGEBAUT_H
