// roundtrip.cpp - liest jedes .icarus, schreibt es zurueck, vergleicht Byte
// fuer Byte. Ravens 1510 Skripte sind der Pruefmassstab.
#include "bhed/script.h"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
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

// Erste abweichende Stelle als lesbaren Ausschnitt.
static std::string firstDiff(const std::string& a, const std::string& b) {
    std::size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    std::size_t line = 1;
    for (std::size_t k = 0; k < i; ++k) if (a[k] == '\n') ++line;
    auto snip = [&](const std::string& s) {
        std::size_t s0 = i > 30 ? i - 30 : 0;
        std::string t = s.substr(s0, 70);
        std::string q;
        for (char c : t) { if (c == '\r') q += "\\r"; else if (c == '\n') q += "\\n"; else if (c == '\t') q += "\\t"; else q += c; }
        return q;
    };
    std::ostringstream ss;
    ss << "Zeile " << line << "\n      Raven: " << snip(a) << "\n      unser: " << snip(b);
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "Aufruf: roundtrip <verzeichnis> [erlaubte-abweichung ...]\n"
            "  Jede weitere Angabe ist ein Dateiname, der abweichen DARF.\n"
            "  Gedacht fuer Ravens eigene Handarbeit, nicht zum Wegdruecken\n"
            "  eigener Fehler - jede benannte Ausnahme muss begruendet sein.\n");
        return 2;
    }
    std::vector<std::string> allowed;
    for (int i = 2; i < argc; ++i) { allowed.emplace_back(argv[i]); }
    int unexpected = 0;
    int total = 0, exact = 0, parseFail = 0, withDiag = 0, unstable = 0;
    std::vector<std::string> unstableEx;
    std::map<std::string, int> diagKinds;
    std::vector<std::string> examples;

    for (const auto& e : fs::recursive_directory_iterator(argv[1])) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".icarus") continue;
        ++total;

        const std::string src = slurp(e.path());
        bhed::Script s;
        std::vector<bhed::Diag> diag;
        if (!bhed::readScript(src, s, diag)) ++parseFail;
        if (!diag.empty()) {
            ++withDiag;
            for (const auto& d : diag) {
                std::string k = d.message.substr(0, d.message.find(':'));
                ++diagKinds[k];
            }
        }
        const std::string back = bhed::writeScript(s);

        // Festpunkt: zweimal durch den Kreis muss dasselbe ergeben, sonst
        // wandert eine Datei bei jedem Speichern.
        bhed::Script s2;
        std::vector<bhed::Diag> d2;
        if (!bhed::readScript(back, s2, d2)) ++parseFail;
        if (bhed::writeScript(s2) != back) {
            ++unstable;
            if (unstableEx.size() < 3) unstableEx.push_back(e.path().string());
        }

        if (back == src) { ++exact; continue; }
        bool ok = false;
        for (const std::string& a : allowed) {
            if (e.path().filename().string() == a) { ok = true; break; }
        }
        if (ok) { continue; }
        ++unexpected;
        if (examples.size() < 5)
            examples.push_back(e.path().string() + "\n      " + firstDiff(src, back));
    }

    std::printf("Skripte gesamt   : %d\n", total);
    std::printf("bytegleich       : %d  (%.2f %%)\n", exact,
                total ? 100.0 * exact / total : 0.0);
    std::printf("Strukturfehler   : %d\n", parseFail);
    std::printf("mit Anmerkungen  : %d\n", withDiag);
    std::printf("nicht festpunkt  : %d\n", unstable);
    for (const auto& x : unstableEx) std::printf("    %s\n", x.c_str());
    for (const auto& [k, v] : diagKinds) std::printf("    %6d  %s\n", v, k.c_str());
    if (!examples.empty()) {
        std::printf("\nErste Abweichungen:\n");
        for (const auto& x : examples) std::printf("  %s\n", x.c_str());
    }
    std::printf("unerwartete Abweichungen: %d  (%zu benannte Ausnahmen)\n",
                unexpected, allowed.size());
    return unexpected == 0 ? 0 : 1;
}
