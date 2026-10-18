// eingebautedatentest.cpp - data/base im Programm (rc601 release, bhed/eingebaut.h)
//
// Aufruf: eingebautedatentest <data/base>
//
// shank wollte fuer jedes Release "eine .exe ..., dass die Leute sie
// runterladen und nutzen koennen". Bis rc600 lief eine behaved.exe ohne den
// Ordner data\base daneben nicht ("behaved.bhc not found in ...", Ende mit 2).
// Diese Probe verlangt, dass die Dateien von data/base Byte fuer Byte im
// Programm stecken, sich auspacken lassen und daraus dasselbe Befehlsmodell
// entsteht wie aus data/base. Ohne eingebaute Daten (rc600) schlaegt sie
// gleich in der ersten Zeile fehl.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "bhed/commands.h"
#include "bhed/eingebaut.h"
#include "bhed/i18n.h"

namespace fs = std::filesystem;

namespace {

int fehler = 0;

void expect(const std::string& was, bool ok) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", was.c_str());
    if (!ok) {
        ++fehler;
    }
}

std::string lies(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct Zahlen {
    bool ok = false;
    std::size_t befehle = 0;
    std::size_t typmengen = 0;
    std::size_t makros = 0;
    std::size_t meldungen = 0;
};

// So laedt das Programm sein Modell (gui/app.cpp, loadModel).
Zahlen lade(const fs::path& ordner) {
    bhed::CommandDb db;
    std::vector<bhed::LoadDiag> diag;
    Zahlen z;
    z.ok = bhed::loadCommandDb((ordner / "behaved.bhc").string(), ordner.string(), db, diag);
    (void)bhed::loadCommandDb((ordner / "supplement.bhc").string(), ordner.string(), db, diag);
    z.befehle = db.commands.size();
    z.typmengen = db.typesets.size();
    z.makros = db.macros.size();
    z.meldungen = diag.size();
    return z;
}

std::string text(const Zahlen& z) {
    return std::to_string(z.befehle) + " Befehle, " + std::to_string(z.typmengen) + " Typmengen, " +
           std::to_string(z.makros) + " Makros, " + std::to_string(z.meldungen) + " Meldungen";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Aufruf: eingebautedatentest <data/base>\n");
        return 2;
    }
    const fs::path quelle = argv[1];
    std::vector<std::string> namen;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(quelle, ec)) {
        if (e.is_regular_file()) {
            namen.push_back(e.path().filename().string());
        }
    }
    std::sort(namen.begin(), namen.end());

    // --- Was im Programm steckt ------------------------------------------
    const std::vector<bhed::eingebaut::Datei> alle = bhed::eingebaut::dateien();
    expect("eingebaut: " + std::to_string(alle.size()) + " Dateien, data/base hat " +
               std::to_string(namen.size()),
           !namen.empty() && alle.size() == namen.size());
    for (const std::string& n : namen) {
        const auto it = std::find_if(alle.begin(), alle.end(),
                                     [&](const bhed::eingebaut::Datei& d) { return n == d.name; });
        const std::string soll = lies(quelle / n);
        expect("eingebaut, Byte fuer Byte gleich: " + n,
               it != alle.end() && it->groesse == soll.size() &&
                   (soll.empty() || std::memcmp(it->daten, soll.data(), soll.size()) == 0));
    }

    // --- Auspacken und laden -----------------------------------------------
    const fs::path tmp = fs::temp_directory_path() /
                         ("bhed_eingebaut_" + std::to_string(std::chrono::steady_clock::now()
                                                                 .time_since_epoch()
                                                                 .count()));
    const fs::path ziel = tmp / "einst" / "data" / "base";
    std::string f;
    int geschrieben = -1;
    const bool ok = bhed::eingebaut::auspacken(ziel, "rc999", &f, &geschrieben);
    expect("auspacken in einen leeren Ordner: " + std::to_string(geschrieben) + " Dateien geschrieben" +
               (f.empty() ? "" : " (" + f + ")"),
           ok && geschrieben == static_cast<int>(namen.size()));
    bool gleich = !namen.empty();
    for (const std::string& n : namen) {
        if (lies(ziel / n) != lies(quelle / n)) {
            gleich = false;
            std::printf("        anders: %s\n", n.c_str());
        }
    }
    expect("ausgepackt: jede Datei gleich der aus data/base", gleich);
    expect("ausgepackt: fassung.txt nennt die Fassung", lies(ziel / "fassung.txt") == "rc999\n");
    const Zahlen a = lade(quelle);
    const Zahlen b = lade(ziel);
    std::printf("        data/base:   %s\n        ausgepackt:  %s\n", text(a).c_str(), text(b).c_str());
    expect("loadCommandDb: aus der ausgepackten Kopie dasselbe Modell wie aus data/base",
           a.ok && b.ok && a.befehle > 0 && a.befehle == b.befehle && a.typmengen == b.typmengen &&
               a.makros == b.makros && a.meldungen == b.meldungen);

    // Zweiter Start: alles schon da, nichts wird geschrieben.
    (void)bhed::eingebaut::auspacken(ziel, "rc999", &f, &geschrieben);
    expect("zweites Auspacken schreibt nichts (" + std::to_string(geschrieben) + ")", geschrieben == 0);

    // Ein veraenderter und ein geloeschter Ordnerinhalt heilt beim naechsten Start.
    if (!namen.empty()) {
        {
            std::ofstream kaputt(ziel / namen.front(), std::ios::binary | std::ios::app);
            kaputt << "x";
        }
        fs::remove(ziel / namen.back(), ec);
        const bool ok3 = bhed::eingebaut::auspacken(ziel, "rc999", &f, &geschrieben);
        expect("veraendert + geloescht: genau diese 2 neu geschrieben (" + std::to_string(geschrieben) + ")",
               ok3 && geschrieben == (namen.size() > 1 ? 2 : 1) &&
                   lies(ziel / namen.front()) == lies(quelle / namen.front()) &&
                   lies(ziel / namen.back()) == lies(quelle / namen.back()));
    }

    // --- Welcher Ordner gilt -------------------------------------------------
    // Liegt data\base neben der .exe (das ausgepackte Zip), gilt er - wie bisher.
    const fs::path mit = tmp / "mit";
    fs::create_directories(mit / "data" / "base", ec);
    fs::copy_file(quelle / "behaved.bhc", mit / "data" / "base" / "behaved.bhc", ec);
    const bhed::eingebaut::Wahl w1 = bhed::eingebaut::datenOrdnerWaehlen(mit, tmp / "einst2", "rc999");
    expect("Wahl: data\\base neben der .exe gilt, nichts ausgepackt",
           w1.ordner == mit / "data" / "base" && !w1.eingebaut && !fs::exists(tmp / "einst2", ec));
    // Die .exe allein: die eingebaute Kopie, ausgepackt in den Einstellungsordner.
    const fs::path ohne = tmp / "ohne";
    fs::create_directories(ohne, ec);
    const bhed::eingebaut::Wahl w2 = bhed::eingebaut::datenOrdnerWaehlen(ohne, tmp / "einst2", "rc999");
    expect("Wahl: die .exe allein -> <Einstellungen>/data/base, ausgepackt" +
               (w2.fehler.empty() ? std::string() : " (" + w2.fehler + ")"),
           w2.ordner == tmp / "einst2" / "data" / "base" && w2.eingebaut &&
               fs::exists(w2.ordner / "behaved.bhc", ec) && lade(w2.ordner).befehle == a.befehle);
    expect("Wahl: nichts neben die .exe geschrieben", !fs::exists(ohne / "data", ec));
    // Ohne Einstellungsordner bleibt es beim Ordner neben der .exe; der Aufrufer
    // meldet dann, dass die Daten fehlen.
    const bhed::eingebaut::Wahl w3 = bhed::eingebaut::datenOrdnerWaehlen(ohne, fs::path(), "rc999");
    expect("Wahl: ohne Einstellungsordner -> neben der .exe, mit Grund",
           w3.ordner == ohne / "data" / "base" && !w3.eingebaut && !w3.fehler.empty());

    // --- rc601 release (Durchsicht): Auspacken scheitert ----------------------
    // Vorher zeigte das Meldungsfenster dann nur "das ganze Zip entpacken" mit
    // dem Ordner neben der .exe; der Grund ging nur ins Protokoll. Jetzt nennt
    // die Wahl Ziel und Grund, und auspackHinweis macht daraus die zweite
    // Zeile in der eingestellten Sprache. Nachgestellt mit einer DATEI "data"
    // im Einstellungsordner: data\base laesst sich dort nicht anlegen.
    const fs::path gesperrt = tmp / "einst3";
    fs::create_directories(gesperrt, ec);
    {
        std::ofstream sperre(gesperrt / "data", std::ios::binary);
        sperre << "eine Datei, kein Ordner\n";
    }
    const bhed::eingebaut::Wahl w4 = bhed::eingebaut::datenOrdnerWaehlen(ohne, gesperrt, "rc999");
    expect("Wahl: Auspacken scheitert -> neben der .exe, mit Ziel und Grund (" + w4.fehler + ")",
           w4.ordner == ohne / "data" / "base" && !w4.eingebaut &&
               w4.auspackZiel == gesperrt / "data" / "base" && !w4.fehler.empty());
    const auto hinweisIn = [&](bhed::i18n::Language sprache) {
        const std::u8string z = w4.auspackZiel.u8string();
        char soll[4096];
        std::snprintf(soll, sizeof(soll), bhed::i18n::trIn(sprache, bhed::i18n::Str::DataUnpackFailed),
                      std::string(z.begin(), z.end()).c_str(), w4.fehler.c_str());
        return std::string(soll);
    };
    bhed::i18n::setLanguage(bhed::i18n::Language::English);
    expect("auspackHinweis: englisch, mit Ziel und Grund",
           bhed::eingebaut::auspackHinweis(w4) == hinweisIn(bhed::i18n::Language::English));
    bhed::i18n::setLanguage(bhed::i18n::Language::German);
    expect("auspackHinweis: in der eingestellten Sprache (deutsch)",
           bhed::eingebaut::auspackHinweis(w4) == hinweisIn(bhed::i18n::Language::German));
    bhed::i18n::setLanguage(bhed::i18n::Language::English);
    expect("auspackHinweis: leer, wenn ausgepackt oder gar nicht versucht",
           bhed::eingebaut::auspackHinweis(w1).empty() && bhed::eingebaut::auspackHinweis(w2).empty() &&
               bhed::eingebaut::auspackHinweis(w3).empty() && w1.auspackZiel.empty() && w3.auspackZiel.empty());
    // Eine Datei, die sich nicht ersetzen laesst (an ihrer Stelle ein Ordner
    // mit Inhalt): der Grund nennt ihren Namen.
    const fs::path klemmt = tmp / "einst4" / "data" / "base";
    fs::create_directories(klemmt / "behaved.bhc" / "drin", ec);
    std::string f4;
    const bool ok4 = bhed::eingebaut::auspacken(klemmt, "rc999", &f4);
    expect("auspacken: nicht ersetzbar -> \"behaved.bhc: <Grund>\" (" + f4 + ")",
           !ok4 && f4.rfind("behaved.bhc: ", 0) == 0 && f4.size() > 13);

    fs::remove_all(tmp, ec);
    std::printf(fehler == 0 ? "\nalle Proben der eingebauten Daten bestanden\n" : "\nFEHLGESCHLAGEN (%d)\n", fehler);
    return fehler == 0 ? 0 : 1;
}
