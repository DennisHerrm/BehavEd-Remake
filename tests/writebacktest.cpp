// Schreibt das Gizmo genau EINEN Wert zurueck - und sonst nichts?
//
// Das ist die Frage, an der das Rueckschreiben haengt. Ein Editor, der beim
// Verschieben einer Kamera nebenbei die Schreibweise anderer Zeilen
// aendert, ist unbrauchbar: die Datei liegt in einer Versionsverwaltung,
// und jede unnoetige Aenderung verrauscht den Vergleich.
//
// Geprueft wird deshalb Zeichen fuer Zeichen:
//
//   1. Der geaenderte Befehl traegt den neuen Wert.
//   2. JEDE andere Zeile steht unveraendert da.
//   3. Rueckgaengig stellt den Ausgangszustand bytegleich wieder her.
//   4. Die Schreibweise stimmt: "%.3f", wie BehavEd es schreibt.
#include "bhed/edit.h"
#include "bhed/script.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int fehler = 0;

void expect(const char* was, bool ok) {
    std::printf("  %s   %s\n", ok ? "ok  " : "FEHL", was);
    if (!ok) { ++fehler; }
}

const char* kQuelle =
    "rem ( \"---- Anfang ----\" );\n"
    "camera ( ENABLE );\n"
    "camera ( MOVE, <-76.000 6618.000 58.000>, 0.000 );\n"
    "camera ( PAN, <-19.000 152.000 0.000>, <0.000 0.000 0.000>, 0.000 );\n"
    "camera ( ZOOM, 60.000, 0.000 );\n"
    "wait ( 5000.000 );\n"
    "camera ( MOVE, <411.000 7196.000 81.000>, 0.000 );\n";

std::string schreibe(const bhed::Script& s) {
    return bhed::writeScript(s);
}

}  // namespace

int main() {
    bhed::Script sc;
    std::vector<bhed::Diag> d;
    if (!bhed::readScript(kQuelle, sc, d)) {
        std::printf("Quelle liess sich nicht lesen\n");
        return 1;
    }
    const std::string vorher = schreibe(sc);
    bhed::Document doc{sc};

    // Der ERSTE MOVE steht an Position 2.
    bhed::Path p;
    p.push_back(2);
    const bhed::Node* alt = bhed::nodeAt(doc.script(), p);
    expect("der Befehl ist ein camera ( MOVE )",
           alt != nullptr && alt->name == "camera" && alt->args.size() >= 2 &&
               alt->args[0].text == "MOVE");
    if (alt == nullptr) { return 1; }

    // --- Wie das Gizmo es tut: kopieren, EIN Argument ersetzen ----------
    bhed::Node neu = *alt;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.3f %.3f %.3f",
                  -76.0 + 128.0, 6618.0, 58.0);
    neu.args[1].text = buf;
    expect("replaceAt nimmt es an", doc.replaceAt(p, neu));

    const std::string nachher = schreibe(doc.script());

    // --- 1. Der neue Wert steht drin ------------------------------------
    expect("der neue Wert steht im Skript",
           nachher.find("52.000 6618.000 58.000") != std::string::npos);

    // --- 2. Jede andere Zeile unveraendert ------------------------------
    {
        std::vector<std::string> a;
        std::vector<std::string> b;
        for (int durch = 0; durch < 2; ++durch) {
            const std::string& q = (durch == 0) ? vorher : nachher;
            std::vector<std::string>& z = (durch == 0) ? a : b;
            std::string akt;
            for (char c : q) {
                if (c == '\n') { z.push_back(akt); akt.clear(); }
                else { akt += c; }
            }
            if (!akt.empty()) { z.push_back(akt); }
        }
        expect("gleich viele Zeilen wie vorher", a.size() == b.size());
        int anders = 0;
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
            if (a[i] != b[i]) {
                ++anders;
                std::printf("       Zeile %zu: \"%s\"\n              statt \"%s\"\n",
                            i, b[i].c_str(), a[i].c_str());
            }
        }
        expect("GENAU EINE Zeile hat sich geaendert", anders == 1);
    }

    // --- 3. Rueckgaengig stellt bytegleich wieder her --------------------
    expect("rueckgaengig gelingt", doc.undo());
    expect("und das Ergebnis ist BYTEGLEICH zum Anfang",
           schreibe(doc.script()) == vorher);

    // --- 4. Die Schreibweise ---------------------------------------------
    //
    // BehavEd schreibt %.3f. Eine Zahl ohne Nachkommastellen oder mit
    // sechsen faellt in einem Vergleich sofort auf.
    (void)doc.redo();
    const std::string wieder = schreibe(doc.script());
    expect("drei Nachkommastellen, wie im Original",
           wieder.find("52.000") != std::string::npos &&
               wieder.find("52.0 ") == std::string::npos &&
               wieder.find("52.000000") == std::string::npos);

    std::printf("%s (%d Fehlschlaege)\n",
                fehler == 0 ? "alle Rueckschreibproben bestanden" : "FEHLER",
                fehler);
    return fehler == 0 ? 0 : 1;
}
