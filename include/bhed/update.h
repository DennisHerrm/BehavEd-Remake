// update.h - Auto-Updater: was ohne Netz und Fenster pruefbar ist
//
// Gewuenscht (shank, 01.10.2026): "eine Auto-Updater-Funktion, dass man in
// Zukunft Aenderungen einfach direkt ueber GitHub runterladen kann - wir
// haben ja das Repo angelegt."
//
// Der Ablauf (gui/update_win32.cpp):
//
//   1. GET api.github.com/repos/<repo>/releases/latest   -> Release (JSON)
//   2. Ist das Release neuer als diese Fassung (kFassung, "rc568")?
//   3. Das .zip des Releases laden, entpacken, die Dateien neben die .exe
//      legen (die laufende .exe wird dabei nur umbenannt), neu starten.
//
// Hier steht der Teil, der sich ohne Netz pruefen laesst
// (tests/updatetest.cpp): JSON lesen, Fassungen vergleichen, Zielpfade
// aus den Archiveintraegen.
#ifndef BHED_UPDATE_H
#define BHED_UPDATE_H

#include <string>
#include <vector>

namespace bhed::update {

// Das Repository, aus dem die Updates kommen: ein OEFFENTLICHES nur fuer die
// fertigen Programme. Der Quelltext liegt privat in DennisHerrm/BehavEd-Remake;
// so bekommt jeder die Updates ohne GitHub-Konto.
inline constexpr const char* kRepo = "DennisHerrm/BehavEd-Remake-Releases";

struct Asset {
    std::string name;          // "BehavEd-Remake-rc568.zip"
    std::string apiUrl;        // api.github.com/.../assets/<id> (mit Anmeldung)
    std::string downloadUrl;   // github.com/.../download/... (oeffentlich)
    long long size = 0;
};

struct Release {
    std::string tag;           // "v1.0.0-rc568"
    std::string name;          // "BehavEd-Remake 1.0.0-rc568"
    std::string body;          // die Notizen (Markdown)
    std::string htmlUrl;       // Seite des Releases
    std::vector<Asset> assets;
};

// Liest die Antwort von /releases/latest. false bei kaputtem JSON oder
// ohne tag_name.
[[nodiscard]] bool parseRelease(const std::string& json, Release& out, std::string* fehler = nullptr);

// Die Nummer hinter "rc": "v1.0.0-rc568" -> 568, "rc568" -> 568, sonst -1.
[[nodiscard]] int rcNummer(const std::string& s);

// Ist `tag` (vom Server) neuer als `lokal` (kFassung)? Ohne erkennbare
// Nummer auf einer Seite: nein - lieber kein Update als ein falsches.
[[nodiscard]] bool istNeuer(const std::string& tag, const std::string& lokal);

// Das erste .zip unter den Dateien des Releases, sonst nullptr.
[[nodiscard]] const Asset* zipAsset(const Release& r);

// Wohin ein Archiveintrag neben der .exe gehoert. Ein gemeinsamer oberster
// Ordner ("BehavEd-Remake/") wird abgestreift - `oberOrdner` ist er, oder
// leer. Leer zurueck heisst: ueberspringen (Ordner selbst, absolute Pfade,
// ".." - ein Archiv soll nichts ausserhalb des Programmordners anfassen).
[[nodiscard]] std::string zielImOrdner(const std::string& eintrag, const std::string& oberOrdner);

// Der gemeinsame oberste Ordner aller Eintraege ("BehavEd-Remake/"), oder
// leer, wenn es keinen gibt.
[[nodiscard]] std::string gemeinsamerOrdner(const std::vector<std::string>& eintraege);

}  // namespace bhed::update

#endif  // BHED_UPDATE_H
