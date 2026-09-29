// editfuzz.cpp - Zufaellige Bearbeitungen ueber den echten Bestand.
//
// Drei Forderungen, die ein Editor nie verletzen darf:
//   1. Nach jeder Bearbeitung laesst sich das Skript schreiben und wieder
//      lesen, und das Ergebnis ist ein Festpunkt.
//   2. Alles rueckgaengig ergibt EXAKT den Ausgangstext zurueck, Byte fuer Byte.
//   3. Kein Absturz und kein Sanitizer-Treffer, auch bei ungueltigen Wegen.
#include "bhed/edit.h"
#include "bhed/script.h"
#include "bhed/validate.h"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void allPaths(const std::vector<bhed::Node>& ns, const bhed::Path& pre,
              std::vector<bhed::Path>& out) {
    for (std::size_t i = 0; i < ns.size(); ++i) {
        bhed::Path p = pre;
        p.push_back(i);
        out.push_back(p);
        allPaths(ns[i].children, p, out);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "Aufruf: editfuzz <skriptdir> <bhc> <headerdir> [runden]\n");
        return 2;
    }
    const int rounds = argc > 4 ? std::stoi(argv[4]) : 3000;

    bhed::CommandDb db;
    std::vector<bhed::LoadDiag> ld;
    if (!bhed::loadCommandDb(argv[2], argv[3], db, ld)) {
        return 2;
    }

    std::vector<std::string> seeds;
    for (const auto& e : fs::recursive_directory_iterator(argv[1])) {
        if (!e.is_regular_file() || e.path().extension() != ".icarus") {
            continue;
        }
        std::ifstream f(e.path(), std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        seeds.push_back(ss.str());
        if (seeds.size() >= 400) {
            break;
        }
    }

    std::mt19937 rng(987654);
    int notFixedPoint = 0;
    int undoMismatch = 0;
    int noSnapshot = 0;
    int redoMismatch = 0;
    long ops = 0;
    long applied = 0;

    for (int r = 0; r < rounds; ++r) {
        const std::string& src = seeds[rng() % seeds.size()];
        bhed::Script s;
        std::vector<bhed::Diag> d;
        (void)bhed::readScript(src, s, d);
        const std::string base = bhed::writeScript(s);

        bhed::Document doc{s};
        const int n = 1 + static_cast<int>(rng() % 12);
        for (int k = 0; k < n; ++k) {
            std::vector<bhed::Path> paths;
            allPaths(doc.script().nodes, {}, paths);
            bhed::Path p;
            if (!paths.empty() && (rng() % 10) != 0) {
                p = paths[rng() % paths.size()];
            } else {
                // absichtlich ungueltiger Weg
                p = {rng() % 50, rng() % 50};
            }
            ++ops;
            const std::size_t depthBefore = doc.undoDepth();
            const unsigned op = rng() % 11;
            bool ok = false;
            switch (op) {
                case 0: ok = doc.removeAt(p); break;
                case 1: ok = doc.cloneAt(p); break;
                case 2: ok = doc.moveUp(p); break;
                case 3: ok = doc.moveDown(p); break;
                case 4: ok = doc.copyAt(p); break;
                case 5: ok = doc.cutAt(p); break;
                case 6: ok = doc.pasteAfter(p); break;
                case 7: ok = doc.commentOut(p); break;
                case 8: ok = doc.uncomment(p); break;
                case 9: ok = doc.insertRem(p); break;
                case 10: {
                    const bhed::Command& c = db.commands[rng() % db.commands.size()];
                    ok = doc.insertAfter(p, bhed::makeNode(c, db));
                    break;
                }
                default: break;
            }
            applied += ok ? 1 : 0;

            // Eine ausgefuehrte Bearbeitung MUSS eine Momentaufnahme angelegt
            // haben, sonst ist sie nicht rueckgaengig zu machen. copyAt aendert
            // nichts und legt deshalb auch keine an.
            const bool mutating = (op != 4);
            if (ok && mutating && doc.undoDepth() == depthBefore) {
                ++noSnapshot;
            }

            // Forderung 1: schreiben, lesen, schreiben ergibt dasselbe
            const std::string w1 = bhed::writeScript(doc.script());
            bhed::Script s2;
            std::vector<bhed::Diag> d2;
            (void)bhed::readScript(w1, s2, d2);
            if (bhed::writeScript(s2) != w1) {
                ++notFixedPoint;
            }
            // darf melden was es will, nur nicht knallen
            std::vector<bhed::Issue> is;
            bhed::validate(doc.script(), db, is);
        }

        // Forderung 2b: rueckgaengig und wieder vor ergibt denselben Stand
        const std::string atEnd = bhed::writeScript(doc.script());
        int back = 0;
        while (back < 3 && doc.undo()) {
            ++back;
        }
        for (int k = 0; k < back; ++k) {
            if (!doc.redo()) {
                ++redoMismatch;
            }
        }
        if (bhed::writeScript(doc.script()) != atEnd) {
            ++redoMismatch;
        }

        // Forderung 2: alles rueckgaengig -> Ausgangstext
        while (doc.undo()) {
        }
        if (bhed::writeScript(doc.script()) != base) {
            ++undoMismatch;
        }
    }

    std::printf("Runden              : %d\n", rounds);
    std::printf("Bearbeitungen       : %ld (%ld ausgefuehrt, %ld abgewiesen)\n",
                ops, applied, ops - applied);
    std::printf("ohne Momentaufnahme : %d\n", noSnapshot);
    std::printf("Wiederholen falsch  : %d\n", redoMismatch);
    std::printf("kein Festpunkt      : %d\n", notFixedPoint);
    std::printf("Rueckgaengig ungleich : %d\n", undoMismatch);
    return (notFixedPoint + undoMismatch + noSnapshot + redoMismatch) != 0 ? 1 : 0;
}
