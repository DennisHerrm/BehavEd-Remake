// checkall.cpp - liest alle .icarus und haelt sie gegen das Befehlsmodell.
#include "bhed/script.h"
#include "bhed/validate.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "Aufruf: checkall <skriptverzeichnis> <behaved.bhc> <headerverzeichnis>\n");
        return 2;
    }
    bhed::CommandDb db;
    std::vector<bhed::LoadDiag> ld;
    if (!bhed::loadCommandDb(argv[2], argv[3], db, ld)) {
        std::fprintf(stderr, "Befehlsmodell nicht ladbar\n");
        return 2;
    }
    // Nachtrag anhaengen (loadCommandDb ergaenzt die Datenbank)
    if (argc > 4 && !bhed::loadCommandDb(argv[4], argv[3], db, ld))
        std::fprintf(stderr, "Nachtrag nicht ladbar: %s\n", argv[4]);

    for (const auto& d : ld)
        std::printf("MODELL %s:%d %s\n", d.file.c_str(), d.line, d.message.c_str());
    std::printf("Modell: %zu Signaturen, %zu Typmengen, %zu Makros\n\n",
                db.commands.size(), db.typesets.size(), db.macros.size());

    int files = 0, clean = 0, errs = 0, warns = 0, broken = 0;
    std::map<std::string, int> byCode;
    std::map<std::string, std::pair<int, std::string>> samples;

    for (const auto& e : fs::recursive_directory_iterator(argv[1])) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".icarus") continue;
        ++files;

        bhed::Script s;
        std::vector<bhed::Diag> d;
        if (!bhed::readScript(slurp(e.path()), s, d)) ++broken;

        std::vector<bhed::Issue> issues;
        bhed::validate(s, db, issues);
        if (issues.empty()) { ++clean; continue; }
        for (const auto& i : issues) {
            if (i.level == bhed::Issue::Level::Error) ++errs; else ++warns;
            ++byCode[i.code];
            auto& sm = samples[i.code];
            ++sm.first;
            if (sm.second.empty())
                sm.second = e.path().filename().string() + " [" + i.where + "] " + i.message;
        }
    }

    std::printf("Dateien          : %d\n", files);
    std::printf("Strukturfehler   : %d\n", broken);
    std::printf("ohne Beanstandung: %d  (%.2f %%)\n", clean, files ? 100.0 * clean / files : 0.0);
    std::printf("Fehler           : %d\n", errs);
    std::printf("Anmerkungen      : %d\n\n", warns);
    for (const auto& [code, n] : byCode)
        std::printf("  %s  %6d   %s\n", code.c_str(), n, samples[code].second.c_str());
    return errs ? 1 : 0;
}
