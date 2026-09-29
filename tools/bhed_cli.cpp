// bhed_cli.cpp - Befehlszeilenwerkzeug. Das erste, was man wirklich starten
// kann; die Oberflaeche kommt spaeter, der Kern ist schon fertig.
//
//   bhed check   <datei|ordner>   Skript gegen das Befehlsmodell pruefen
//   bhed tree    <datei>          Baumansicht als Text
//   bhed fmt     <datei>          neu formatieren (nach stdout)
//   bhed compile <datei> [-o x]   nach .ibi uebersetzen
//   bhed decompile <datei.ibi>    aus .ibi zurueck nach .icarus
//   bhed info                     geladenes Befehlsmodell zeigen
//
// Das Befehlsmodell wird neben der .exe unter data/base gesucht, oder ueber
// --data <ordner> angegeben.

#include "bhed/ibi.h"
#include "bhed/tree.h"
#include "bhed/validate.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
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

bool spew(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
    return static_cast<bool>(f);
}

// data/base neben der .exe, daneben, oder eine Ebene hoeher suchen.
fs::path findData(const fs::path& exe, const std::string& given) {
    if (!given.empty()) {
        return given;
    }
    std::error_code ec;
    fs::path dir = exe.parent_path();
    for (int i = 0; i < 4; ++i) {
        const fs::path c = dir / "data" / "base";
        if (fs::exists(c / "behaved.bhc", ec)) {
            return c;
        }
        dir = dir.parent_path();
        if (dir.empty()) {
            break;
        }
    }
    return {};
}

int usage() {
    std::fputs(
        "bhed - Werkzeug fuer ICARUS-Skripte (.icarus / .ibi)\n\n"
        "  bhed check     <datei|ordner> [--strict]\n"
        "  bhed tree      <datei> [--types] [--g] [--nofold]\n"
        "  bhed fmt       <datei> [-o <datei>]\n"
        "  bhed compile   <datei.icarus> [-o <datei.ibi>]\n"
        "  bhed decompile <datei.ibi> [-o <datei.icarus>]\n"
        "  bhed info\n\n"
        "  --data <ordner>   Ort von behaved.bhc und den Kopfdateien\n",
        stderr);
    return 2;
}

int walkFiles(const fs::path& p, std::vector<fs::path>& out) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        for (const auto& e : fs::recursive_directory_iterator(p, ec)) {
            if (!e.is_regular_file()) {
                continue;
            }
            std::string ext = e.path().extension().string();
            for (char& c : ext) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (ext == ".icarus") {
                out.push_back(e.path());
            }
        }
    } else {
        out.push_back(p);
    }
    return static_cast<int>(out.size());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return usage();
    }
    const std::string cmd = argv[1];

    std::string dataOpt;
    std::string outOpt;
    bool optTypes = false;
    bool optG = false;
    bool optNoFold = false;
    bool optStrict = false;
    std::vector<std::string> pos;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) { dataOpt = argv[++i]; }
        else if ((a == "-o" || a == "--out") && i + 1 < argc) { outOpt = argv[++i]; }
        else if (a == "--types") { optTypes = true; }
        else if (a == "--g") { optG = true; }
        else if (a == "--nofold") { optNoFold = true; }
        else if (a == "--strict") { optStrict = true; }
        else { pos.push_back(a); }
    }

    const fs::path data = findData(argv[0], dataOpt);
    if (data.empty()) {
        std::fputs("Befehlsmodell nicht gefunden. Erwartet data/base/behaved.bhc\n"
                   "neben der .exe, oder --data <ordner> angeben.\n", stderr);
        return 2;
    }
    bhed::CommandDb db;
    std::vector<bhed::LoadDiag> ld;
    if (!bhed::loadCommandDb((data / "behaved.bhc").string(), data.string(), db, ld)) {
        std::fprintf(stderr, "behaved.bhc nicht lesbar: %s\n", data.string().c_str());
        return 2;
    }
    const fs::path supp = data / "supplement.bhc";
    if (fs::exists(supp)) {
        (void)bhed::loadCommandDb(supp.string(), data.string(), db, ld);
    }
    for (const auto& d : ld) {
        std::fprintf(stderr, "MODELL %s:%d %s\n", d.file.c_str(), d.line, d.message.c_str());
    }

    if (cmd == "info") {
        std::printf("Befehlsmodell aus %s\n", data.string().c_str());
        std::printf("  %zu Signaturen, %zu Typmengen, %zu Makros\n",
                    db.commands.size(), db.typesets.size(), db.macros.size());
        for (const bhed::Command& c : db.commands) {
            std::printf("  %-12s %-34s %s\n", c.name.c_str(),
                        bhed::signatureText(c).c_str(), c.desc.c_str());
        }
        for (const auto& [name, ts] : db.typesets) {
            std::printf("  Typmenge %-18s %%%c  %4zu Eintraege%s\n", name.c_str(), ts.kind,
                        ts.entries.size(),
                        ts.hiddenAfterEol != 0 ? "  (weitere nach #eol verborgen)" : "");
        }
        return 0;
    }

    if (pos.empty()) {
        return usage();
    }
    const fs::path in = pos[0];

    if (cmd == "check") {
        std::vector<fs::path> files;
        walkFiles(in, files);
        int errs = 0;
        int warns = 0;
        for (const fs::path& f : files) {
            bhed::Script s;
            std::vector<bhed::Diag> rd;
            if (!bhed::readScript(slurp(f), s, rd)) {
                std::printf("%s: Strukturfehler\n", f.string().c_str());
                ++errs;
            }
            std::vector<bhed::Issue> is;
            bhed::validate(s, db, is);
            for (const bhed::Issue& i : is) {
                const bool err = i.level == bhed::Issue::Level::Error;
                if (err) { ++errs; } else { ++warns; }
                if (err || optStrict) {
                    std::printf("%s: %s %s [%s] %s\n", f.string().c_str(),
                                err ? "Fehler" : "Hinweis", i.code.c_str(),
                                i.where.c_str(), i.message.c_str());
                }
            }
        }
        std::printf("%zu Datei(en): %d Fehler, %d Anmerkungen%s\n", files.size(), errs, warns,
                    (warns != 0 && !optStrict) ? "  (--strict zeigt sie)" : "");
        return errs != 0 ? 1 : 0;
    }

    const std::string src = slurp(in);
    if (src.empty()) {
        std::fprintf(stderr, "leer oder nicht lesbar: %s\n", in.string().c_str());
        return 2;
    }

    if (cmd == "decompile") {
        std::vector<bhed::IbiBlock> bl;
        std::vector<bhed::Diag> d;
        if (!bhed::readIbi(src, bl, d)) {
            for (const auto& x : d) { std::fprintf(stderr, "%s\n", x.message.c_str()); }
            return 1;
        }
        bhed::Script s;
        (void)bhed::decompile(bl, db, s, d);
        const std::string out = bhed::writeScript(s);
        if (outOpt.empty()) { std::fwrite(out.data(), 1, out.size(), stdout); }
        else if (!spew(outOpt, out)) { return 1; }
        return 0;
    }

    bhed::Script s;
    std::vector<bhed::Diag> rd;
    if (!bhed::readScript(src, s, rd)) {
        std::fprintf(stderr, "%s: Strukturfehler\n", in.string().c_str());
    }
    for (const auto& d : rd) {
        std::fprintf(stderr, "%s:%d %s\n", in.string().c_str(), d.line, d.message.c_str());
    }

    if (cmd == "fmt") {
        const std::string out = bhed::writeScript(s);
        if (outOpt.empty()) { std::fwrite(out.data(), 1, out.size(), stdout); }
        else if (!spew(outOpt, out)) { return 1; }
        return 0;
    }
    if (cmd == "tree") {
        bhed::TreeOptions o;
        o.showTypes = optTypes;
        o.gFloats = optG;
        o.foldMacros = !optNoFold;
        std::vector<bhed::Row> rows;
        bhed::buildTree(s, db, o, rows);
        for (const bhed::Row& r : rows) {
            for (int i = 0; i < r.depth; ++i) { std::fputs("  ", stdout); }
            std::printf("%-14s %s%s\n", r.icon.c_str(), r.text.c_str(),
                        (r.childCount != 0 && r.what == bhed::Row::What::Macro) ? "  [+]" : "");
        }
        std::printf("\n%zu Zeilen\n", rows.size());
        return 0;
    }
    if (cmd == "compile") {
        std::vector<bhed::IbiBlock> bl;
        std::vector<bhed::Diag> d;
        const bool ok = bhed::compile(s, db, bl, d);
        for (const auto& x : d) { std::fprintf(stderr, "%s\n", x.message.c_str()); }
        const std::string bytes = bhed::writeIbi(bl);
        fs::path o = outOpt;
        if (o.empty()) { o = fs::path(in).replace_extension(".ibi"); }
        if (!spew(o, bytes)) {
            std::fprintf(stderr, "kann nicht schreiben: %s\n", o.string().c_str());
            return 1;
        }
        std::printf("%s: %zu Bloecke, %zu Byte\n", o.string().c_str(), bl.size(), bytes.size());
        return ok ? 0 : 1;
    }

    return usage();
}
