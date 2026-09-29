// fuzz.cpp - kaputte Eingaben. Ein Editor bekommt halbfertige Dateien,
// abgeschnittene Downloads und Handarbeit zu sehen; er darf nie abstuerzen.
//
// Zwei Forderungen:
//   1. kein Absturz, kein Sanitizer-Treffer
//   2. Festpunkt: lesen -> schreiben -> lesen -> schreiben ergibt dasselbe
#include "bhed/script.h"
#include "bhed/validate.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "Aufruf: fuzz <skriptdir> <bhc> <headerdir> [runden]\n"); return 2; }
    const int rounds = argc > 4 ? std::atoi(argv[4]) : 20000;

    bhed::CommandDb db;
    std::vector<bhed::LoadDiag> ld;
    if (!bhed::loadCommandDb(argv[2], argv[3], db, ld)) return 2;

    std::vector<std::string> seeds;
    for (const auto& e : fs::recursive_directory_iterator(argv[1])) {
        if (!e.is_regular_file() || e.path().extension() != ".icarus") continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::ostringstream ss; ss << f.rdbuf();
        seeds.push_back(ss.str());
        if (seeds.size() >= 300) break;
    }
    if (seeds.empty()) { std::fprintf(stderr, "keine Saatdateien\n"); return 2; }

    std::mt19937 rng(12345);
    int unstable = 0;
    std::size_t bytes = 0;

    for (int r = 0; r < rounds; ++r) {
        std::string s = seeds[rng() % seeds.size()];
        const int ops = 1 + static_cast<int>(rng() % 8);
        for (int k = 0; k < ops && !s.empty(); ++k) {
            switch (rng() % 5) {
                case 0: s[rng() % s.size()] = static_cast<char>(rng() % 256); break;      // Byte kippen
                case 1: s.erase(rng() % s.size(), 1 + rng() % 40); break;                 // Stueck loeschen
                case 2: s.insert(rng() % s.size(), std::string(1 + rng() % 8, "(){}\"$<>,;%@*/\t"[rng() % 15])); break;
                case 3: s.resize(rng() % s.size());  break;                               // abschneiden
                case 4: { std::size_t a = rng() % s.size(); std::size_t n = 1 + rng() % 30;
                          s.insert(a, s.substr(a, std::min(n, s.size() - a))); } break;   // Stueck verdoppeln
            }
        }
        bytes += s.size();

        bhed::Script sc;
        std::vector<bhed::Diag> d;
        (void)bhed::readScript(s, sc, d);
        const std::string w1 = bhed::writeScript(sc);

        bhed::Script sc2;
        std::vector<bhed::Diag> d2;
        (void)bhed::readScript(w1, sc2, d2);
        if (bhed::writeScript(sc2) != w1) ++unstable;

        std::vector<bhed::Issue> is;
        bhed::validate(sc, db, is);      // darf melden, was es will - nur nicht knallen
    }

    std::printf("Runden        : %d\n", rounds);
    std::printf("Bytes gesamt  : %.1f MiB\n", static_cast<double>(bytes) / 1048576.0);
    std::printf("kein Festpunkt: %d\n", unstable);
    return unstable ? 1 : 0;
}
