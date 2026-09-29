// screenshot.cpp - das Modell gegen ein Bildschirmfoto des Originals halten.
//
// Die Events-Liste von BehavEd 2.0 zeigt jede Signatur mit ihren
// Parametertypen. Sie ist hier wortgetreu abgeschrieben und dient als
// Pruefmassstab: Reihenfolge, Anzahl, Ueberladungen und Typschreibweise
// muessen uebereinstimmen. Quelle: Bildschirmfotos vom 11.08.2026,
// BehavEd 2.0 mit behaved.bhc und dem JKA-SDK-Kopfdateisatz.
#include "bhed/ibi.h"
#include "bhed/tree.h"

#include <fstream>
#include <sstream>

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Row { const char* name; const char* sig; };

// So steht es im Bild, Zeile fuer Zeile.
const Row kEvents[] = {
    {"flush",      "(  )"},
    {"if",         "( <expr>, <expr>, <expr> )"},
    {"else",       "(  )"},
    {"loop",       "( <int> )"},
    {"affect",     "( <str>, < E\"affect_type\" > )"},
    {"run",        "( <str> )"},
    {"wait",       "( <float> )"},
    {"waitsignal", "( <str> )"},
    {"signal",     "( <str> )"},
    {"sound",      "( < E\"channels\" >, <str> )"},
    {"move",       "( <vec>, <vec>, <float> )"},
    {"move",       "( <expr>, <expr> )"},
    {"rotate",     "( <vec>, <float> )"},
    {"use",        "( <str> )"},
    {"use",        "( <expr> )"},
    {"kill",       "( <str> )"},
    {"remove",     "( <str> )"},
    {"print",      "( <str> )"},
    {"rem",        "( <str> )"},
    {"declare",    "( < E\"declare_type\" >, <str> )"},
    {"free",       "( <str> )"},
    {"set",        "( < E\"set_types\" >, <str> )"},
    {"set",        "( <str>, <str> )"},
    {"camera",     "( < E\"camera_commands\" > )"},
    {"task",       "( <str> )"},
    {"do",         "( <str> )"},
    {"wait",       "( <str> )"},
    {"dowait",     "( <str> )"},
    {"play",       "( < E\"play_types\" >, <str> )"},
};

// Die Makros, in der Reihenfolge des Bildes.
const char* kMacros[] = {
    "standOnly", "walkOnly", "runOnly",
    "standNoAlerts", "walkNoAlerts", "runNoAlerts",
    "standNoEnemies", "walkNoEnemies", "runNoEnemies",
    "standGuardNoChase", "patrolRun", "patrolNoChase",
    "patrolWalkNoChase", "patrolRunNoChase", "default",
};

int fails = 0;

void check(const std::string& what, const std::string& got, const std::string& want) {
    if (got == want) {
        return;
    }
    std::printf("  FEHL  %s\n        Bild : %s\n        unser: %s\n",
                what.c_str(), want.c_str(), got.c_str());
    ++fails;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Aufruf: screenshot <bhc> <headerdir> [nachtrag]\n");
        return 2;
    }
    bhed::CommandDb db;
    std::vector<bhed::LoadDiag> ld;
    if (!bhed::loadCommandDb(argv[1], argv[2], db, ld)) {
        return 2;
    }
    // Der Nachtrag ist eine eigene Zutat und steht NICHT im Bild - fuer
    // diesen Vergleich bleibt er draussen.
    (void)argv;

    const std::size_t n = sizeof(kEvents) / sizeof(kEvents[0]);
    if (db.commands.size() != n) {
        std::printf("  FEHL  Anzahl: Bild %zu, unser %zu\n", n, db.commands.size());
        ++fails;
    }
    for (std::size_t i = 0; i < n && i < db.commands.size(); ++i) {
        check("Zeile " + std::to_string(i + 1) + " Name",
              db.commands[i].name, kEvents[i].name);
        check("Zeile " + std::to_string(i + 1) + " (" + kEvents[i].name + ")",
              bhed::signatureText(db.commands[i]), kEvents[i].sig);
    }

    const std::size_t m = sizeof(kMacros) / sizeof(kMacros[0]);
    if (db.macros.size() != m) {
        std::printf("  FEHL  Makrozahl: Bild %zu, unser %zu\n", m, db.macros.size());
        ++fails;
    }
    for (std::size_t i = 0; i < m && i < db.macros.size(); ++i) {
        check("Makro " + std::to_string(i + 1), db.macros[i], kMacros[i]);
    }

    // --- Baumansicht gegen die Bildschirmfotos von intro_jedi.txt --------
    //
    // Der Schalter "Show Types" schaltet die ganze Schreibweise um, nicht
    // nur die Typmenge. Zeilen aus dem Bild, wortgetreu:
    // Die Baumvorlage am NAMEN suchen, nicht an der Stelle im Aufruf.
    // Vorher stand sie fest auf argv[3] - als weitere Pruefdateien dazukamen,
    // rutschte sie weg und der Vergleich lief gegen die falsche Datei.
    const char* treeFixture = nullptr;
    for (int a = 3; a < argc; ++a) {
        if (std::string(argv[a]).find("intro_jedi") != std::string::npos) {
            treeFixture = argv[a];
        }
    }
    if (treeFixture != nullptr) {
        std::ifstream f(treeFixture, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        bhed::Script sc;
        std::vector<bhed::Diag> d;
        if (f && bhed::readScript(ss.str(), sc, d)) {
            bhed::TreeOptions o;
            o.gFloats = true;          // "%g floats" ist im Bild gedrueckt
            o.showTypes = false;
            std::vector<bhed::Row> rows;
            bhed::buildTree(sc, db, o, rows);

            struct Want { const char* text; };
            const Want kFromScreenshot[] = {
                {"affect ( anakin2, FLUSH )"},
                {"set ( SET_MORELIGHT, true )"},
                {"set ( SET_WEAPON, WP_SABER )"},
                {"wait ( 3000 )"},
                {"rem ( ------------ Intro ------------ )"},
                {"camera ( ENABLE )"},
                {"camera ( MOVE, -6048 7162 1214, 0 )"},
                {"camera ( PAN, 4 -51 0, 0 0 0, 0 )"},
                {"camera ( ZOOM, 80, 0 )"},
                {"sound ( CHAN_VOICE_GLOBAL, sound/md_twj/anakin1.mp3 )"},
            };
            for (const Want& w : kFromScreenshot) {
                bool found = false;
                for (const bhed::Row& r : rows) {
                    if (r.text == w.text) { found = true; break; }
                }
                if (!found) {
                    std::printf("  FEHL  Baumzeile fehlt: %s\n", w.text);
                    ++fails;
                }
            }

            // Bloecke sind nach dem Laden ZU - im Bild steht [+] vor jedem
            // affect, und die Kinder sind nicht zu sehen.
            bhed::Expanded ex;
            o.expanded = &ex;
            std::vector<bhed::Row> closed;
            bhed::buildTree(sc, db, o, closed);
            if (closed.size() >= rows.size()) {
                std::printf("  FEHL  zugeklappt muessten weniger Zeilen sein "
                            "(%zu gegen %zu)\n", closed.size(), rows.size());
                ++fails;
            }
            ex.openAll();
            std::vector<bhed::Row> opened;
            bhed::buildTree(sc, db, o, opened);
            if (opened.size() != rows.size()) {
                std::printf("  FEHL  aufgeklappt muss so viele Zeilen sein wie ohne "
                            "Faltung\n");
                ++fails;
            }
            std::printf("  Baumansicht gegen intro_jedi.txt: %zu Zeilen offen, "
                        "%zu zugeklappt\n", rows.size(), closed.size());
        }
    }

    // --- Echte Mod-Skripte ------------------------------------------------
    //
    // Sie stehen hier, weil sie Formen enthalten, die in Ravens Bestand
    // selten sind: gonkability.txt hat sieben else-Zweige und geht sechs
    // Ebenen tief, mit $random()$ und $get()$ in jeder Bedingung.
    for (int a = 3; a < argc; ++a) {
        std::ifstream f(argv[a], std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        const std::string src = ss.str();
        if (src.empty()) {
            continue;
        }
        const std::string name = argv[a];
        bhed::Script sc;
        std::vector<bhed::Diag> d;
        const bool ok = bhed::readScript(src, sc, d);
        if (!ok || bhed::writeScript(sc) != src) {
            std::printf("  FEHL  %s: Rundlauf nicht bytegleich\n", name.c_str());
            ++fails;
            continue;
        }
        // Uebersetzen, zurueck, wieder uebersetzen: dieselben Bytes.
        std::vector<bhed::IbiBlock> b1;
        std::vector<bhed::Diag> cd;
        (void)bhed::compile(sc, db, b1, cd);
        bhed::Script back;
        std::vector<bhed::Diag> dd;
        (void)bhed::decompile(b1, db, back, dd);
        bhed::Script again;
        (void)bhed::readScript(bhed::writeScript(back), again, dd);
        std::vector<bhed::IbiBlock> b2;
        (void)bhed::compile(again, db, b2, cd);
        if (bhed::writeIbi(b1) != bhed::writeIbi(b2)) {
            std::printf("  FEHL  %s: Uebersetzung ist kein Festpunkt\n", name.c_str());
            ++fails;
            continue;
        }
        std::printf("  ok    %s: bytegleich, %zu Bloecke\n", name.c_str(), b1.size());
    }

    std::printf("  %zu Signaturen und %zu Makros gegen das Bildschirmfoto: %s\n",
                n, m, fails ? "ABWEICHUNGEN" : "alle gleich");
    return fails ? 1 : 0;
}
