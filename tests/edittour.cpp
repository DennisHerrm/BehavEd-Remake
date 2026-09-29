// edittour.cpp - was ein Editor NIEMALS tun darf
//
// Zwei Forderungen ueber den gesamten Raven-Bestand:
//
//  1. Jeder Weg aus der Baumansicht loest wieder auf denselben Knoten auf.
//     Sonst trifft "Delete" den falschen.
//  2. Einen Knoten im Event-Editor oeffnen und mit Ok wieder schliessen,
//     ohne etwas zu aendern, darf die Datei nicht veraendern. Das ist der
//     Handgriff, den ein Nutzer hundertmal am Tag macht, und genau dort
//     zerschiessen Editoren fremde Dateien.
#include "bhed/edit.h"
#include "bhed/tree.h"
#include "bhed/validate.h"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Die Felder, die der Editor fuer diesen Knoten zeigen wuerde.
std::vector<bhed::Param> fieldsFor(const bhed::Command& c, const bhed::CommandDb& db,
                                   const std::vector<bhed::Arg>& args) {
    std::vector<bhed::Param> out;
    std::size_t skip = 0;
    for (std::size_t i = 0; i < c.params.size(); ++i) {
        if (skip != 0) { --skip; continue; }
        out.push_back(c.params[i]);
        if (c.params[i].kind != bhed::Param::Kind::TypeSet) { continue; }
        const bhed::TypeSet* ts = db.typeset(c.params[i].typeset);
        if (ts == nullptr) { continue; }
        const std::size_t at = out.size() - 1;
        if (at >= args.size()) { continue; }
        const bhed::TypeEntry* e = ts->find(args[at].text);
        if (e == nullptr || e->params.empty()) { continue; }
        for (const bhed::Param& sp : e->params) { out.push_back(sp); }
        skip = e->params.size();
    }
    return out;
}

const bhed::Command* pickSignature(const bhed::Node& n, const bhed::CommandDb& db) {
    return bhed::selectOverload(n, db);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "Aufruf: edittour <skriptdir> <bhc> <headerdir> [nachtrag]\n");
        return 2;
    }
    bhed::CommandDb db;
    std::vector<bhed::LoadDiag> ld;
    if (!bhed::loadCommandDb(argv[2], argv[3], db, ld)) { return 2; }
    if (argc > 4 && !bhed::loadCommandDb(argv[4], argv[3], db, ld)) { return 2; }

    int files = 0;
    int rows = 0;
    int pathBad = 0;
    int touched = 0;
    int changed = 0;
    int noSignature = 0;
    std::map<std::string, int> changedBy;
    std::vector<std::string> examples;

    for (const auto& e : fs::recursive_directory_iterator(argv[1])) {
        if (!e.is_regular_file() || e.path().extension() != ".icarus") { continue; }
        ++files;
        const std::string src = slurp(e.path());

        bhed::Script s;
        std::vector<bhed::Diag> d;
        if (!bhed::readScript(src, s, d)) { continue; }
        const std::string base = bhed::writeScript(s);

        // --- 1) Wege der Baumansicht ---
        bhed::TreeOptions o;
        o.foldMacros = false;
        std::vector<bhed::Row> tr;
        bhed::buildTree(s, db, o, tr);
        for (const bhed::Row& r : tr) {
            ++rows;
            if (bhed::nodeAt(s, r.path) != r.node) { ++pathBad; }
        }

        // --- 2) Oeffnen und mit Ok schliessen, ohne Aenderung ---
        for (const bhed::Row& r : tr) {
            if (r.what != bhed::Row::What::Command || r.node == nullptr) { continue; }
            const bhed::Command* c = pickSignature(*r.node, db);
            if (c == nullptr) { ++noSignature; continue; }
            ++touched;

            const std::vector<bhed::Param> f = fieldsFor(*c, db, r.node->args);
            if (f.size() != r.node->args.size()) { ++noSignature; continue; }

            bhed::Node n;
            n.kind = bhed::Node::Kind::Command;
            n.name = r.node->name;
            for (std::size_t i = 0; i < f.size(); ++i) {
                n.args.push_back(bhed::argForParam(f[i], r.node->args[i].text, db,
                                                  &r.node->args[i]));
            }
            bhed::Document doc{s};
            if (!doc.replaceAt(r.path, n)) { continue; }
            if (bhed::writeScript(doc.script()) != base) {
                ++changed;
                ++changedBy[c->name];
                if (examples.size() < 5) {
                    examples.push_back(e.path().filename().string() + "  " +
                                       bhed::rowText(*r.node, o));
                }
            }
        }
    }

    std::printf("Dateien                 : %d\n", files);
    std::printf("Baumzeilen              : %d\n", rows);
    std::printf("Weg loest falsch auf    : %d\n", pathBad);
    std::printf("Knoten geoeffnet+Ok     : %d\n", touched);
    std::printf("davon veraendert        : %d\n", changed);
    std::printf("ohne passende Signatur  : %d\n", noSignature);
    for (const auto& [k, v] : changedBy) { std::printf("    %6d  %s\n", v, k.c_str()); }
    for (const auto& x : examples) { std::printf("    %s\n", x.c_str()); }
    return (pathBad != 0 || changed != 0) ? 1 : 0;
}
