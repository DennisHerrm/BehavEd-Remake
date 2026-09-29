// audit.cpp - jede Datei durch den zustaendigen Leser schicken.
//
// Gedacht fuer eine ganze Mod-Installation: Karten, Modelle, Skelette,
// Skripte. Meldet NUR, was nicht geht - und am Ende eine Bilanz.
//
// Nicht Teil der Auslieferung, sondern ein Werkzeug: die Proben in tests/
// pruefen Regeln, dieses hier prueft VORRAT. Beides braucht man.
//
// Aufruf:
//   audit <datei|verzeichnis> ...
#include "bhed/bsp.h"
#include "bhed/bspgeo.h"
#include "bhed/gla.h"
#include "bhed/efx/io.h"
#include "bhed/glm.h"
#include "bhed/ibi.h"
#include "bhed/script.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

struct Count {
    int ok = 0;
    int bad = 0;
};

Count g_bsp;
Count g_glm;
Count g_gla;
Count g_ibi;
Count g_efx;
int g_breitX = 0;
int g_breitY = 0;
std::map<std::string, int> g_prims;

// Welche Befehle stehen in den Skripten, und wie oft?
std::map<std::string, int> g_cmds;

void countCommands(const std::vector<bhed::Node>& nodes) {
    for (const bhed::Node& n : nodes) {
        if (n.kind == bhed::Node::Kind::Command) {
            ++g_cmds[n.name];
        }
        countCommands(n.children);
    }
}

void doFile(const std::filesystem::path& p) {
    const std::string ext = lower(p.extension().string());
    const std::string path = p.string();
    std::string err;

    if (ext == ".bsp") {
        const std::string b = slurp(path);
        bhed::MapData m;
        bhed::BspGeometry g;
        const bool a = bhed::readBsp(b, m, &err);
        const bool c = bhed::readBspGeometry(b, g, &err);
        if (!a || !c) {
            std::printf("  KARTE   %s: %s\n", path.c_str(), err.c_str());
            ++g_bsp.bad;
            return;
        }
        // Zusicherung, die fuer JEDE Karte gelten muss: die Untermodelle
        // teilen die Flaechen lueckenlos und ohne Dubletten auf. Sonst
        // stimmt die Trennung Welt/bewegliche Teile nicht.
        std::vector<int> mal(g.surfaces.size(), 0);
        for (const bhed::BspGeometry::SubModel& sm : g.models) {
            for (int k = 0; k < sm.numSurfaces; ++k) {
                const auto at = static_cast<std::size_t>(sm.firstSurface) +
                                static_cast<std::size_t>(k);
                if (at < mal.size()) {
                    ++mal[at];
                }
            }
        }
        int nie = 0;
        int mehr = 0;
        for (int n : mal) {
            if (n == 0) { ++nie; } else if (n > 1) { ++mehr; }
        }
        if (nie != 0 || mehr != 0) {
            std::printf("  KARTE   %s: Untermodelle decken die Flaechen nicht "
                        "sauber ab (%d ohne, %d mehrfach)\n",
                        path.c_str(), nie, mehr);
            ++g_bsp.bad;
            return;
        }
        ++g_bsp.ok;
        return;
    }

    if (ext == ".glm") {
        bhed::GlmModel m;
        if (!bhed::readGlm(slurp(path), m, &err)) {
            std::printf("  MODELL  %s: %s\n", path.c_str(), err.c_str());
            ++g_glm.bad;
            return;
        }
        // Nebenbei: wohin schaut das Modell?
        //
        // Ein Mensch ist schulterbreit und schmal von vorn. Die breitere
        // der beiden waagerechten Achsen ist also die Schulterachse, und
        // die steht QUER zur Blickrichtung. Das ist der zweite unabhaengige
        // Beleg fuer den festen Ausgleich von +90 Grad in mapview.cpp -
        // der erste kam aus der Grundstellung der .gla.
        float mn[3] = {1e9F, 1e9F, 1e9F};
        float mx[3] = {-1e9F, -1e9F, -1e9F};
        for (const auto& su : m.surfaces) {
            for (const auto& v : su.verts) {
                for (int c = 0; c < 3; ++c) {
                    mn[c] = std::min(mn[c], v.xyz[c]);
                    mx[c] = std::max(mx[c], v.xyz[c]);
                }
            }
        }
        if (mn[0] < 1e8F) {
            if ((mx[0] - mn[0]) > (mx[1] - mn[1])) {
                ++g_breitX;
            } else {
                ++g_breitY;
            }
        }
        ++g_glm.ok;
        return;
    }

    if (ext == ".gla") {
        bhed::GlaAnimation a;
        if (!bhed::readGla(slurp(path), a, &err)) {
            std::printf("  SKELETT %s: %s\n", path.c_str(), err.c_str());
            ++g_gla.bad;
            return;
        }
        ++g_gla.ok;
        return;
    }

    if (ext == ".efx") {
        const bhed::efx::ReadResult r = bhed::efx::read(slurp(path));
        if (r.hasErrors()) {
            std::string why = "unbekannt";
            for (const bhed::efx::Diagnostic& d : r.diagnostics) {
                if (d.severity == bhed::efx::Severity::Error) { why = d.message; }
            }
            std::printf("  EFFEKT  %s: %s\n", path.c_str(), why.c_str());
            ++g_efx.bad;
            return;
        }
        for (const auto& pr : r.effect.primitives) {
            ++g_prims[bhed::efx::typeName(pr.type)];
        }
        ++g_efx.ok;
        return;
    }

    if (ext == ".ibi") {
        std::vector<bhed::IbiBlock> bl;
        std::vector<bhed::Diag> d;
        if (!bhed::readIbi(slurp(path), bl, d)) {
            std::printf("  SKRIPT  %s: %s\n", path.c_str(),
                        d.empty() ? "nicht lesbar" : d.back().message.c_str());
            ++g_ibi.bad;
            return;
        }
        ++g_ibi.ok;
        return;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::filesystem::path> files;
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path root{argv[i]};
        if (std::filesystem::is_directory(root)) {
            for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
                if (e.is_regular_file()) {
                    files.push_back(e.path());
                }
            }
        } else {
            files.push_back(root);
        }
    }
    std::sort(files.begin(), files.end());
    std::printf("== Was nicht geht ==\n");
    for (const auto& f : files) {
        doFile(f);
    }
    std::printf("\n== Bilanz ==\n");
    std::printf("  Karten   %5d gelesen, %d nicht\n", g_bsp.ok, g_bsp.bad);
    std::printf("  Modelle  %5d gelesen, %d nicht\n", g_glm.ok, g_glm.bad);
    std::printf("  Skelette %5d gelesen, %d nicht\n", g_gla.ok, g_gla.bad);
    std::printf("  Skripte  %5d gelesen, %d nicht\n", g_ibi.ok, g_ibi.bad);
    std::printf("  Effekte  %5d gelesen, %d nicht\n", g_efx.ok, g_efx.bad);
    std::printf("\n  Modelle breiter in X: %d, breiter in Y: %d\n", g_breitX,
                g_breitY);
    std::printf("  (X breiter = Schulterachse ist X = Figur schaut nach +/-Y,\n"
                "   daher der feste Ausgleich von +90 Grad in mapview.cpp)\n");
    std::printf("\n  Primitive in den Effekten:\n");
    for (const auto& [k, v] : g_prims) {
        std::printf("    %6d  %s\n", v, k.c_str());
    }
    return (g_bsp.bad + g_glm.bad + g_gla.bad + g_ibi.bad + g_efx.bad) == 0 ? 0 : 1;
}
