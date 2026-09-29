// Einen Knoten aus einem ANDEREN Dokument einfuegen.
//
// Das ist, was das Ziehen von rechts nach links tut. Geprueft wird, was
// dabei schiefgehen kann:
//
//   1. Kommt der Knoten vollstaendig an - samt Kindern?
//   2. Bleibt das QUELLDOKUMENT unberuehrt? Rechts wird nicht bearbeitet.
//   3. Laesst es sich rueckgaengig machen?
//   4. Ueberlebt der Knoten das Schreiben und Wiederlesen unveraendert?
#include "bhed/edit.h"
#include "bhed/script.h"
#include <cstddef>
#include <cstdio>
#include <string>
static int fehler = 0;
static void expect(const char* was, bool ok) {
    std::printf("  %s %s\n", ok ? "ok  " : "FEHL", was);
    if (!ok) { ++fehler; }
}
int main() {
    const std::string quelle =
        "affect ( \"kyle\", /*@AFFECT_TYPE*/ FLUSH )\n{\n"
        "\tset ( /*@SET_TYPES*/ \"SET_INVISIBLE\", \"true\" );\n"
        "\twait ( 500.000 );\n}\n";
    const std::string ziel = "wait ( 100.000 );\n";

    bhed::Script a;
    bhed::Script b;
    std::vector<bhed::Diag> d;
    (void)bhed::readScript(quelle, a, d);
    (void)bhed::readScript(ziel, b, d);
    expect("Quelle gelesen", !a.nodes.empty());
    expect("Ziel gelesen", !b.nodes.empty());

    bhed::Document dq{a};
    bhed::Document dz{b};

    const bhed::Path woher{0};
    const bhed::Node* nd = bhed::nodeAt(dq.script(), woher);
    expect("Knoten in der Quelle gefunden", nd != nullptr);
    if (nd == nullptr) { return 1; }
    const std::size_t kinderVorher = nd->children.size();

    const bhed::Path wohin{0};
    expect("eingefuegt", dz.insertAfter(wohin, *nd));
    expect("das Ziel hat jetzt zwei Knoten", dz.script().nodes.size() == 2);

    const bhed::Node* neu = bhed::nodeAt(dz.script(), bhed::Path{1});
    expect("der neue Knoten ist da", neu != nullptr);
    if (neu != nullptr) {
        expect("mit demselben Namen", neu->name == nd->name);
        std::printf("     Kinder: Quelle %zu, Kopie %zu\n",
                    kinderVorher, neu->children.size());
        expect("und mit allen Kindern", neu->children.size() == kinderVorher);
    }

    // Die Quelle darf sich NICHT geaendert haben.
    expect("die Quelle ist unberuehrt", dq.script().nodes.size() == 1);
    expect("und gilt weiter als gespeichert", !dq.dirty());

    // Rueckgaengig.
    expect("rueckgaengig moeglich", dz.undoDepth() > 0);
    (void)dz.undo();
    expect("danach wieder ein Knoten", dz.script().nodes.size() == 1);

    // Nochmal einfuegen und durch Schreiben/Lesen schicken.
    (void)dz.insertAfter(wohin, *nd);
    const std::string text = bhed::writeScript(dz.script());
    bhed::Script c;
    (void)bhed::readScript(text, c, d);
    expect("Schreiben und Wiederlesen ergibt dasselbe",
           c.nodes.size() == dz.script().nodes.size());
    if (c.nodes.size() == 2) {
        expect("auch die Kinder ueberleben",
               c.nodes[1].children.size() == kinderVorher);
    }

    std::printf("%s (%d Fehlschlaege)\n",
                fehler == 0 ? "alle Uebernahmeproben bestanden" : "FEHLER",
                fehler);
    return fehler == 0 ? 0 : 1;
}
