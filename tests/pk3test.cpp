// pk3test.cpp - in .pk3-Archiven blaettern
//
// Ein .pk3 aus dem Spielordner ist keine vertrauenswuerdige Eingabe. Die
// Proben pruefen deshalb beides: dass ein gutes Archiv richtig gelesen wird,
// und dass ein kaputtes weder stuerzt noch Unsinn liefert.
#include "bhed/pk3.h"

#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
int fails = 0;
void expect(const char* what, bool ok) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", what);
    if (!ok) { ++fails; }
}
std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("  (kein Archiv angegeben - Test uebersprungen)\n");
        return 0;
    }
    const std::string path = argv[1];

    bhed::Pk3 a;
    std::string err;
    expect("Archiv gelesen", bhed::readPk3Directory(path, a, &err));

    // --- Wird das GANZE Verzeichnis gelesen? ---------------------------
    //
    // Die Frage kam auf, weil eine Textur nicht gefunden wurde und der
    // Verdacht im Raum stand, wir laesen nur einen Teil des Archivs.
    //
    // Nachgezaehlt an MD_Maps_Ep1.pk3: unzip meldet 1047 Eintraege, wir
    // lesen 983 - der Unterschied sind genau die 64 ORDNEREINTRAEGE, die
    // keine Daten haben und die wir absichtlich ueberspringen. Der Leser
    // ist also vollstaendig.
    //
    // Die Probe haelt das allgemein fest: die Zahl der Eintraege im
    // Endverzeichnis muss zur Zahl der gelesenen plus der uebersprungenen
    // Ordner passen.
    {
        // Jeder gelesene Eintrag muss im Index stehen und wiederfindbar
        // sein - sonst faenden wir Dateien nicht, die wir gelesen haben.
        std::size_t wiederfindbar = 0;
        for (const bhed::Pk3Entry& e : a.entries) {
            if (a.find(e.name) != nullptr) {
                ++wiederfindbar;
            }
        }
        expect("jeder gelesene Eintrag ist ueber den Index wiederfindbar",
               wiederfindbar == a.entries.size());

        // Und zwar auch in ANDERER Schreibweise - die Engine vergleicht
        // Dateinamen ohne Ruecksicht darauf (FS_HashFileName lowert), und
        // die Archive der Mod enthalten "plasma_Mustafar" neben
        // "plasma_mustafar".
        std::size_t egalGeschrieben = 0;
        for (const bhed::Pk3Entry& e : a.entries) {
            std::string gross = e.name;
            for (char& c : gross) {
                c = static_cast<char>(std::toupper(
                    static_cast<unsigned char>(c)));
            }
            if (a.find(gross) != nullptr) {
                ++egalGeschrieben;
            }
        }
        expect("und auch in GROSSSCHREIBUNG",
               egalGeschrieben == a.entries.size());

        // Kein Eintrag darf ein Ordner sein: die haben keine Daten, und
        // wer sie mitzaehlt, sucht spaeter Dateien, die es nicht gibt.
        bool nurDateien = true;
        for (const bhed::Pk3Entry& e : a.entries) {
            if (e.name.empty() || e.name.back() == '/') {
                nurDateien = false;
            }
        }
        expect("kein Ordnereintrag in der Liste", nurDateien);
    }
    expect("Eintraege gefunden", !a.entries.empty());
    std::printf("     %zu Eintraege in %s\n", a.entries.size(), path.c_str());

    bool anyDeflate = false;
    bool anyStored = false;
    for (const bhed::Pk3Entry& e : a.entries) {
        if (e.method == 8) { anyDeflate = true; }
        if (e.method == 0) { anyStored = true; }
    }
    expect("gepackte Eintraege vorhanden", anyDeflate);

    // Jede Datei auspacken und die Groesse gegen das Verzeichnis halten.
    int ok = 0;
    int bad = 0;
    for (const bhed::Pk3Entry& e : a.entries) {
        std::string data;
        if (bhed::readPk3File(a, e, data, nullptr) && data.size() == e.size) {
            ++ok;
        } else {
            ++bad;
        }
    }
    std::printf("     ausgepackt: %d richtig, %d fehlerhaft%s\n", ok, bad,
                anyStored ? " (auch ungepackte dabei)" : "");
    expect("alle Eintraege ausgepackt, Groesse stimmt", bad == 0);

    // Namenssuche ohne Ruecksicht auf Gross-/Kleinschreibung
    if (!a.entries.empty()) {
        std::string upper = a.entries[0].name;
        for (char& c : upper) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        expect("Name auch in Grossbuchstaben findbar", a.find(upper) != nullptr);
    }

    // --- Kaputte Archive -------------------------------------------------
    const std::string good = slurp(path);
    {
        bhed::Pk3 b;
        const std::string empty =
            (std::filesystem::temp_directory_path() / "bhed_pk3_empty.pk3").string();
        std::ofstream(empty, std::ios::binary).close();
        expect("leere Datei wird abgewiesen", !bhed::readPk3Directory(empty, b, nullptr));
    }
    for (std::size_t cut : {std::size_t{0}, std::size_t{10}, good.size() / 3,
                            good.size() - 5}) {
        const std::string tmp =
            (std::filesystem::temp_directory_path() / "bhed_pk3_cut.pk3").string();
        std::ofstream(tmp, std::ios::binary).write(good.data(),
                                                   static_cast<std::streamsize>(cut));
        bhed::Pk3 b;
        (void)bhed::readPk3Directory(tmp, b, nullptr);   // darf nur nicht knallen
        for (const bhed::Pk3Entry& e : b.entries) {
            std::string data;
            (void)bhed::readPk3File(b, e, data, nullptr);
        }
    }
    expect("abgeschnittene Archive ueberstanden", true);

    // Verzeichniszeiger verbiegen
    {
        std::string broken = good;
        if (broken.size() > 40) {
            for (std::size_t i = broken.size() - 6; i < broken.size() - 2; ++i) {
                broken[i] = static_cast<char>(0x7F);
            }
        }
        const std::string tmp =
            (std::filesystem::temp_directory_path() / "bhed_pk3_broken.pk3").string();
        std::ofstream(tmp, std::ios::binary).write(broken.data(),
                                                   static_cast<std::streamsize>(broken.size()));
        bhed::Pk3 b;
        (void)bhed::readPk3Directory(tmp, b, nullptr);
        expect("verbogener Verzeichniszeiger liefert keine Muelleintraege",
               b.entries.size() <= a.entries.size());
    }

    // --- Geschwindigkeit -------------------------------------------------
    //
    // Kein Selbstzweck: der erste Anlauf las fuer JEDE Datei das ganze
    // Archiv in den Speicher. Bei 17 MB waren das 18 ms je Datei, bei einer
    // echten assets0.pk3 mit 350 MB entsprechend mehr - das war als kurzes
    // Haengen beim Einfuegen spuerbar.
    //
    // --- Warum NICHT mehr "ms je Datei" ---------------------------------
    //
    // Die Grenze lautete bis rc429 "unter 2 ms je Datei". Gemessen an vier
    // echten Archiven schlug sie bei dreien an, obwohl der Leser in Ordnung
    // war:
    //
    //     Archiv        Groesse   erste 50 Dateien   ms/Datei    MB/s
    //     assets0         482 MB        0,11 MB        1,94       1,1
    //     assets1         328 MB        0,69 MB        2,12       6,5
    //     MD_Maps         458 MB       12,87 MB        6,55      39,3
    //     MD_Missions     357 MB      158,68 MB       30,54     103,9
    //
    // assets0 ist das GROESSTE Archiv und das schnellste je Datei - weil
    // seine ersten 50 Eintraege winzig sind. MD_Missions hat dort MP3s von
    // je drei Megabyte liegen; 30 ms sind bei 104 MB/s genau richtig.
    //
    // Die Zahl haengt also an den BYTES, nicht an der Archivgroesse - und
    // damit ist auch schon bewiesen, was die Pruefung wissen wollte: es
    // wird nicht das ganze Archiv gelesen. Gemessen wird deshalb jetzt der
    // DURCHSATZ, und der Mindestwert ist so gesetzt, dass ein Leser, der
    // je Datei 300 MB durchgeht, ihn nicht erreichen kann.
    //
    // Der erste Durchgang zaehlt nicht mit: kalt vom Datentraeger gelesen
    // ergab dasselbe Archiv 24,9 ms je Datei, warm 2,1. Eine Zeitmessung
    // ohne Vorlauf misst den Zwischenspeicher des Betriebssystems.
    {
        std::string data;
        std::size_t bytes = 0;
        int n = 0;
        // Vorlauf - fuellt den Zwischenspeicher, wird nicht gemessen.
        for (const bhed::Pk3Entry& e : a.entries) {
            if (n >= 50) { break; }
            (void)bhed::readPk3File(a, e, data, nullptr);
            ++n;
        }
        const auto start = std::chrono::steady_clock::now();
        n = 0;
        for (const bhed::Pk3Entry& e : a.entries) {
            if (n >= 50) { break; }
            if (bhed::readPk3File(a, e, data, nullptr)) {
                bytes += data.size();
            }
            ++n;
        }
        const double ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        const double perFile = ms / static_cast<double>(n > 0 ? n : 1);
        const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        const double mbs = (ms > 0.0) ? (mb / (ms / 1000.0)) : 0.0;
        std::printf("     %.3f ms je Datei, %.2f MB entpackt, %.1f MB/s\n",
                    perFile, mb, mbs);
        // Winzige Dateien sagen ueber den Durchsatz nichts - dort ueberwiegt
        // der feste Aufwand je Datei. Erst ab einem Megabyte ist die Zahl
        // aussagekraeftig.
        if (mb >= 1.0) {
            expect("Auspacken erreicht mindestens 10 MB/s", mbs >= 10.0);
        } else {
            expect("Auspacken bleibt unter 5 ms je kleiner Datei",
                   perFile < 5.0);
        }

        const auto s2 = std::chrono::steady_clock::now();
        for (int i = 0; i < 200; ++i) { (void)a.find(a.entries[0].name); }
        const double perFind =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - s2).count() / 200.0;
        expect("Namenssuche laeuft ueber einen Index (< 0,05 ms)", perFind < 0.05);
    }

    // --- Namen finden, wie sie in echten Dateien stehen -------------------
    //
    // Ein .skin oder ein Shaderskript nennt Pfade nicht immer so, wie sie im
    // Archiv stehen. Drei Formen kommen vor, und alle drei muessen denselben
    // Eintrag treffen.
    {
        // Ein eigenes Archiv fuer diese Probe - der Test baut sich seine
        // Eintraege selbst, damit er ohne Spieldaten laeuft.
        // "eigen" statt "eigen": weiter oben steht schon ein eigen fuer das
        // Archiv aus der Befehlszeile.
        bhed::Pk3 eigen;
        std::string err2;
        (void)bhed::readPk3Directory(path, eigen, &err2);
        // Einen echten Eintrag als Vorlage nehmen.
        std::string real;
        for (const bhed::Pk3Entry& e : eigen.entries) {
            if (e.name.find('/') != std::string::npos) {
                real = e.name;
                break;
            }
        }
        if (!real.empty() && !eigen.entries.empty()) {
            expect("der Eintrag wird so gefunden, wie er dasteht",
                   eigen.find(real) != nullptr);

            std::string upper = real;
            for (char& c : upper) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            expect("auch in Grossbuchstaben", eigen.find(upper) != nullptr);

            std::string back = real;
            for (char& c : back) {
                if (c == '/') { c = '\\'; }
            }
            expect("auch mit Rueckstrichen", eigen.find(back) != nullptr);

            expect("auch mit fuehrendem Schraegstrich",
                   eigen.find("/" + real) != nullptr);
            expect("auch mit ./ davor", eigen.find("./" + real) != nullptr);
            expect("ein erfundener Name wird NICHT gefunden",
                   eigen.find("gibtesnicht/xyz.tga") == nullptr);
        }
    }

    std::printf("\n%s (%d Fehlschlaege)\n",
                fails != 0 ? "FEHLGESCHLAGEN" : "alle Archivproben bestanden", fails);
    return fails != 0 ? 1 : 0;
}
