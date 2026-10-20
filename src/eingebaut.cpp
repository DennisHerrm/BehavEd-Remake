// eingebaut.cpp - data/base im Programm selbst (rc601 release)
//
// Siehe bhed/eingebaut.h: warum es das gibt, und wann die eingebaute Kopie
// gilt (nur wenn neben der .exe kein data\base liegt).
#include "bhed/eingebaut.h"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "bhed/i18n.h"

// Die Bytes erzeugt CMake beim Konfigurieren (tools/eingebaute_daten.cmake)
// als eingebaute_daten.inc im Bauordner und setzt dann fuer DIESE Datei
// BHED_EINGEBAUTE_DATEN. Ein Schalter statt __has_include: aendert er sich,
// uebersetzt MSBuild die Datei neu - eine Datei, die beim letzten Uebersetzen
// noch fehlte, kennt die Abhaengigkeitspruefung dagegen nicht. Ohne CMake
// (tools/verify.sh, tools/check_gui.sh uebersetzen src/*.cpp direkt mit g++)
// fehlt beides; dann ist nichts eingebaut, und behaved verhaelt sich wie bis
// rc600.

namespace bhed::eingebaut {
namespace {

namespace fs = std::filesystem;

#ifdef BHED_EINGEBAUTE_DATEN
// Legt kEingebauteDateien[] an: {Name, Bytes, Groesse} je Datei aus data/base.
#include "eingebaute_daten.inc"
#endif

std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// Liegt `p` schon mit genau diesen Bytes da? Dann wird nichts geschrieben -
// der Normalfall ab dem zweiten Start, und ein zweites behaved, das gerade
// liest, wird nicht gestoert.
bool gleicherInhalt(const fs::path& p, const unsigned char* daten, std::size_t groesse) {
    std::error_code ec;
    const std::uintmax_t vorhanden = fs::file_size(p, ec);
    if (ec || vorhanden != groesse) {
        return false;
    }
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        return false;
    }
    const std::string inhalt((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return inhalt.size() == groesse && (groesse == 0 || std::memcmp(inhalt.data(), daten, groesse) == 0);
}

// Erst "<name>.neu<zahl>" schreiben, dann ersetzen: ein abgebrochener Start
// hinterlaesst keine halbe Datei (dasselbe Vorgehen wie der Updater in
// gui/update_win32.cpp). Die Zahl trennt zwei eigenstaendige behaved, die im
// selben Augenblick zum ersten Mal starten - sonst schrieben beide in
// dieselbe Zwischendatei.
bool schreibe(const fs::path& p, const unsigned char* daten, std::size_t groesse, std::string* fehler) {
    fs::path neu = p;
    neu += ".neu" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    {
        errno = 0;
        std::ofstream f(neu, std::ios::binary | std::ios::trunc);
        if (groesse > 0) {
            f.write(reinterpret_cast<const char*>(daten), static_cast<std::streamsize>(groesse));
        }
        f.close();
        if (!f) {
            // rc601 release (Durchsicht): der Grund vom System (errno setzt
            // die Laufzeit beim Oeffnen und beim Schreiben/Schliessen, etwa
            // "permission denied", "no space on device") - er steht auch im
            // Meldungsfenster (auspackHinweis), vorher nur "nicht schreibbar".
            const int nummer = errno;
            if (fehler != nullptr) {
                *fehler = utf8(p.filename()) + ": " +
                          (nummer != 0 ? std::generic_category().message(nummer)
                                       : std::make_error_code(std::errc::io_error).message());
            }
            std::error_code weg;
            fs::remove(neu, weg);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(neu, p, ec);
    if (ec) {
        // Nicht jede Laufzeit ersetzt beim Umbenennen eine vorhandene Datei.
        std::error_code weg;
        fs::remove(p, weg);
        ec.clear();
        fs::rename(neu, p, ec);
    }
    if (ec) {
        if (fehler != nullptr) {
            *fehler = utf8(p.filename()) + ": " + ec.message();
        }
        std::error_code weg;
        fs::remove(neu, weg);
        return false;
    }
    return true;
}

}  // namespace

std::vector<Datei> dateien() {
#ifdef BHED_EINGEBAUTE_DATEN
    return std::vector<Datei>(std::begin(kEingebauteDateien), std::end(kEingebauteDateien));
#else
    return {};
#endif
}

bool auspacken(const std::filesystem::path& ziel, std::string_view fassung, std::string* fehler,
               int* geschrieben) {
    if (geschrieben != nullptr) {
        *geschrieben = 0;
    }
    const std::vector<Datei> alle = dateien();
    if (alle.empty()) {
        if (fehler != nullptr) {
            *fehler = "keine Daten im Programm eingebaut";
        }
        return false;
    }
    std::error_code ec;
    fs::create_directories(ziel, ec);
    std::error_code ecOrdner;
    if (!fs::is_directory(ziel, ecOrdner)) {
        // rc601 release (Durchsicht): der Grund, wie ihn das System meldet -
        // der Ordner selbst steht im Meldungsfenster schon davor.
        if (fehler != nullptr) {
            *fehler = ec ? ec.message() : std::make_error_code(std::errc::not_a_directory).message();
        }
        return false;
    }
    for (const Datei& d : alle) {
        // Die Namen sind die aus data/base, reines ASCII - die schmale
        // Umwandlung in einen Pfad ist dafuer sicher. rc604: "jk2/<name>"
        // (der Befehlssatz fuer Jedi Outcast, data/jk2) gehoert NEBEN
        // data/base, also nach <ziel>/../jk2 - wie im ausgepackten Zip.
        const std::string_view name = d.name;
        const bool jk2 = name.rfind("jk2/", 0) == 0;
        const fs::path p = jk2 ? ziel.parent_path() / "jk2" / std::string(name.substr(4)) : ziel / d.name;
        if (gleicherInhalt(p, d.daten, d.groesse)) {
            continue;
        }
        if (jk2) {
            std::error_code ecJk2;
            fs::create_directories(p.parent_path(), ecJk2);
        }
        if (!schreibe(p, d.daten, d.groesse, fehler)) {
            return false;
        }
        if (geschrieben != nullptr) {
            ++*geschrieben;
        }
    }
    // Der Stempel: von welchem Programm der Ordner stammt. Er entscheidet
    // nichts - verglichen wird oben der Inhalt jeder Datei, so heilt auch
    // ein von Hand veraenderter oder halb geloeschter Ordner von selbst.
    const std::string stempel = std::string(fassung) + "\n";
    const auto* stempelBytes = reinterpret_cast<const unsigned char*>(stempel.data());
    const fs::path stempelPfad = ziel / "fassung.txt";
    if (!gleicherInhalt(stempelPfad, stempelBytes, stempel.size()) &&
        !schreibe(stempelPfad, stempelBytes, stempel.size(), fehler)) {
        return false;
    }
    return true;
}

Wahl datenOrdnerWaehlen(const std::filesystem::path& exeOrdner,
                        const std::filesystem::path& einstellungsOrdner, std::string_view fassung) {
    Wahl w;
    w.ordner = exeOrdner / "data" / "base";
    std::error_code ec;
    if (fs::is_regular_file(w.ordner / "behaved.bhc", ec)) {
        return w;   // das ausgepackte Zip: wie bis rc600
    }
    if (einstellungsOrdner.empty()) {
        w.fehler = "kein Einstellungsordner";
        return w;
    }
    const fs::path ziel = einstellungsOrdner / "data" / "base";
    // Nur ein echter Versuch nennt das Ziel (auspackHinweis): ohne
    // eingebaute Daten gibt es nichts auszupacken.
    if (!dateien().empty()) {
        w.auspackZiel = ziel;
    }
    if (!auspacken(ziel, fassung, &w.fehler, &w.geschrieben)) {
        return w;
    }
    w.ordner = ziel;
    w.eingebaut = true;
    return w;
}

std::string auspackHinweis(const Wahl& wahl) {
    if (wahl.eingebaut || wahl.auspackZiel.empty() || wahl.fehler.empty()) {
        return {};
    }
    const std::string ziel = utf8(wahl.auspackZiel);
    const char* vorlage = i18n::tr(i18n::Str::DataUnpackFailed);
    const int n = std::snprintf(nullptr, 0, vorlage, ziel.c_str(), wahl.fehler.c_str());
    if (n <= 0) {
        return {};
    }
    std::string text(static_cast<std::size_t>(n) + 1, '\0');
    std::snprintf(text.data(), text.size(), vorlage, ziel.c_str(), wahl.fehler.c_str());
    text.resize(static_cast<std::size_t>(n));
    return text;
}

}  // namespace bhed::eingebaut
