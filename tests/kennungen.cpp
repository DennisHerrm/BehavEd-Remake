// Der Aufklapp-Zustand haengt an KENNUNGEN, nicht an Wegen.
//
// Warum der Umbau
// ---------------
// Fuenf Runden lang (rc530 bis rc534) hat derselbe Fehler in immer neuer
// Verkleidung zugeschlagen: der Zustand merkte sich WEGE, und jede
// Strukturaenderung verschiebt Wege. Geflickt wurde jeweils die eine
// Bewegung, die shank gerade gemeldet hatte:
//
//   rc530  moveUp / moveDown          `Expanded::nachZug`
//   rc533  moveTo / moveToEnd         Anker statt Ziel uebergeben
//   rc534  moveInto, Rechtsklickmenue gar nicht nachgefuehrt
//
// Uebrig blieben elf Stellen, die es weiterhin nicht taten - jedes
// Einfuegen, jedes Loeschen, Ablage, Duplizieren, beide
// Mehrfachauswahl-Zuege - und das Rueckgaengigmachen, das sich damit
// ueberhaupt nicht heilen liess.
//
// Diese Datei prueft die drei Faelle, die vorher SCHEITERTEN, und dazu
// den, der mit einer Flagge im Knoten neu kaputtgegangen waere.
#include "bhed/edit.h"
#include "bhed/tree.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

static int g_fehl = 0;

static void erwarte(const char* was, bool ist, bool soll) {
    if (ist == soll) { std::printf("  ok    %s\n", was); return; }
    std::printf("  FEHL  %s (erwartet %s)\n", was, soll ? "ja" : "nein");
    ++g_fehl;
}

static bhed::Node bau(const char* name, bool block = false) {
    bhed::Node n;
    n.kind = bhed::Node::Kind::Command;
    n.name = name;
    n.hasBlock = block;
    return n;
}

// Die Kennung des Knotens an einem Weg - nach dem Vergeben.
static bhed::Kennung kennungAn(bhed::Document& d, const bhed::Path& p) {
    d.vergibKennungen();
    const std::vector<bhed::Node>* v = &d.script().nodes;
    const bhed::Node* n = nullptr;
    for (std::size_t k : p) {
        if (k >= v->size()) { return 0; }
        n = &(*v)[k];
        v = &n->children;
    }
    return n != nullptr ? n->kennung : 0;
}

static std::string namenAn(bhed::Document& d, const bhed::Path& p) {
    const std::vector<bhed::Node>* v = &d.script().nodes;
    const bhed::Node* n = nullptr;
    for (std::size_t k : p) {
        if (k >= v->size()) { return "(nichts)"; }
        n = &(*v)[k];
        v = &n->children;
    }
    return n != nullptr ? n->name : std::string{"(nichts)"};
}

int main() {
    // ---- 1. Ziehen ---------------------------------------------------
    //
    // Das ist der Fall, den rc533 einzeln geflickt hat. Jetzt ohne jede
    // Nachfuehrung.
    {
        bhed::Script sc;
        bhed::Node blk = bau("loop", true);
        blk.children.push_back(bau("wait"));
        sc.nodes.push_back(blk);
        bhed::Document d{sc};
        bhed::Expanded auf;
        const bhed::Kennung k = kennungAn(d, bhed::Path{0});
        auf.setOpen(k, true);

        bhed::Path gelandet;
        erwarte("Ziehen aus dem Block gelingt",
                d.moveTo(bhed::Path{0, 0}, bhed::Path{0}, &gelandet), true);
        d.vergibKennungen();
        erwarte("der loop ist immer noch aufgeklappt", auf.isOpen(k), true);
        erwarte("und er steht noch auf Weg 0",
                kennungAn(d, bhed::Path{0}) == k, true);
    }

    // ---- 2. EINFUEGEN - das ging vor dem Umbau kaputt -----------------
    //
    // Ein Befehl wird UEBER einem offenen Block eingefuegt. Vorher wanderte
    // der gemerkte Zustand auf den neuen Befehl, und das + des Blocks ging
    // zu. Nachgestellt in rc534:
    //
    //   Weg 1 ist jetzt "wait",   gilt als aufgeklappt=1
    //   Weg 2 ist jetzt "affect", gilt als aufgeklappt=0
    {
        bhed::Script sc;
        sc.nodes.push_back(bau("print"));
        sc.nodes.push_back(bau("affect", true));
        bhed::Document d{sc};
        bhed::Expanded auf;
        const bhed::Kennung k = kennungAn(d, bhed::Path{1});
        auf.setOpen(k, true);

        d.insertAfter(bhed::Path{0}, bau("wait"));
        d.vergibKennungen();
        erwarte("der affect steht jetzt auf Weg 2",
                namenAn(d, bhed::Path{2}) == "affect", true);
        erwarte("und ist immer noch aufgeklappt", auf.isOpen(k), true);
        erwarte("das eingefuegte wait ist NICHT aufgeklappt",
                auf.isOpen(kennungAn(d, bhed::Path{1})), false);
    }

    // ---- 3. LOESCHEN -------------------------------------------------
    {
        bhed::Script sc;
        sc.nodes.push_back(bau("print"));
        sc.nodes.push_back(bau("affect", true));
        bhed::Document d{sc};
        bhed::Expanded auf;
        const bhed::Kennung k = kennungAn(d, bhed::Path{1});
        auf.setOpen(k, true);

        erwarte("loeschen gelingt", d.removeAt(bhed::Path{0}), true);
        d.vergibKennungen();
        erwarte("der affect steht jetzt auf Weg 0",
                namenAn(d, bhed::Path{0}) == "affect", true);
        erwarte("und ist immer noch aufgeklappt", auf.isOpen(k), true);
    }

    // ---- 4. RUECKGAENGIG ----------------------------------------------
    //
    // Zwei Dinge auf einmal:
    //   a) Strg+Z darf den Zustand nicht KAPUTT machen (so war es).
    //   b) Strg+Z darf ihn auch nicht ZURUECKDREHEN. Aufklappen ist keine
    //      Bearbeitung. Genau das waere passiert, wenn die Flagge im
    //      Knoten laege und mit im Schnappschuss stuende.
    {
        bhed::Script sc;
        sc.nodes.push_back(bau("print"));
        sc.nodes.push_back(bau("affect", true));
        sc.nodes.push_back(bau("wait"));
        bhed::Document d{sc};
        bhed::Expanded auf;
        const bhed::Kennung k = kennungAn(d, bhed::Path{1});

        bhed::Path gelandet;
        (void)d.moveTo(bhed::Path{0}, bhed::Path{2}, &gelandet);
        d.vergibKennungen();
        // NACH der Bearbeitung aufklappen - so entsteht der Fall b).
        auf.setOpen(k, true);
        erwarte("nach dem Zug aufgeklappt", auf.isOpen(k), true);

        erwarte("Strg+Z gelingt", d.undo(), true);
        d.vergibKennungen();
        erwarte("der affect steht wieder auf Weg 1",
                namenAn(d, bhed::Path{1}) == "affect", true);
        erwarte("und traegt dieselbe Kennung",
                kennungAn(d, bhed::Path{1}) == k, true);
        erwarte("er ist immer noch aufgeklappt - Strg+Z hat ihn in Ruhe "
                "gelassen", auf.isOpen(k), true);
        erwarte("und der print daneben nicht",
                auf.isOpen(kennungAn(d, bhed::Path{0})), false);
    }

    // ---- 5. MEHRFACHAUSWAHL -------------------------------------------
    //
    // moveAllTo hat nie nachgefuehrt und liess sich mit `nachZug` auch
    // nicht heilen: N Entnahmen, ein Einfuegen.
    {
        bhed::Script sc;
        sc.nodes.push_back(bau("a"));
        sc.nodes.push_back(bau("b"));
        sc.nodes.push_back(bau("affect", true));
        sc.nodes.push_back(bau("c"));
        bhed::Document d{sc};
        bhed::Expanded auf;
        const bhed::Kennung k = kennungAn(d, bhed::Path{2});
        auf.setOpen(k, true);

        std::vector<bhed::Path> mehrere{bhed::Path{0}, bhed::Path{1}};
        erwarte("zwei auf einmal ziehen gelingt",
                d.moveAllTo(mehrere, bhed::Path{3}), true);
        d.vergibKennungen();
        erwarte("der affect steht jetzt auf Weg 0",
                namenAn(d, bhed::Path{0}) == "affect", true);
        erwarte("und ist immer noch aufgeklappt", auf.isOpen(k), true);
    }

    // ---- 6. Duplizieren: die Kopie erbt das + nicht -------------------
    //
    // Beide traegen erst dieselbe Kennung. Der Durchlauf gibt der ZWEITEN
    // in Baumreihenfolge eine neue - das Original behaelt seinen Zustand.
    {
        bhed::Script sc;
        sc.nodes.push_back(bau("affect", true));
        bhed::Document d{sc};
        bhed::Expanded auf;
        const bhed::Kennung k = kennungAn(d, bhed::Path{0});
        auf.setOpen(k, true);

        erwarte("duplizieren gelingt", d.cloneAt(bhed::Path{0}), true);
        d.vergibKennungen();
        erwarte("zwei Knoten", d.script().nodes.size() == 2, true);
        erwarte("das Original behaelt seine Kennung",
                kennungAn(d, bhed::Path{0}) == k, true);
        erwarte("die Kopie bekommt eine andere",
                kennungAn(d, bhed::Path{1}) != k, true);
        erwarte("Original aufgeklappt", auf.isOpen(k), true);
        erwarte("Kopie zu", auf.isOpen(kennungAn(d, bhed::Path{1})), false);
    }

    // ---- 6b. Bearbeiten laesst den Block aufgeklappt ------------------
    //
    // shank zu rc540: ein aufgeklapptes `if` klappte beim Druck auf "Ok"
    // zu. `replaceAt` uebernahm Kinder und Blockeigenschaft vom alten
    // Knoten, aber nicht die Kennung - also bekam der neue eine frische,
    // und der gemerkte Zustand zeigte ins Leere.
    {
        bhed::Script sc;
        bhed::Node blk = bau("if", true);
        blk.children.push_back(bau("declare"));
        sc.nodes.push_back(blk);
        bhed::Document d{sc};
        bhed::Expanded auf;
        const bhed::Kennung k = kennungAn(d, bhed::Path{0});
        auf.setOpen(k, true);

        bhed::Node neu = bau("if", true);      // wie der Editor ihn baut:
        neu.kennung = 0;                        // frisch, ohne Kennung
        erwarte("Ersetzen gelingt", d.replaceAt(bhed::Path{0}, neu), true);
        d.vergibKennungen();
        erwarte("der Knoten behaelt seine Kennung",
                kennungAn(d, bhed::Path{0}) == k, true);
        erwarte("er ist immer noch aufgeklappt", auf.isOpen(k), true);
        erwarte("und sein Kind ist noch da",
                d.script().nodes[0].children.size() == 1, true);
    }

    // ---- 7. Der Durchlauf ist stabil ---------------------------------
    //
    // Zweimal hintereinander aufgerufen darf er nichts mehr aendern -
    // sonst wandern Kennungen bei jedem Neubau des Baumes.
    {
        bhed::Script sc;
        bhed::Node blk = bau("affect", true);
        blk.children.push_back(bau("x"));
        blk.children.push_back(bau("y"));
        sc.nodes.push_back(blk);
        sc.nodes.push_back(bau("z"));
        bhed::Document d{sc};
        d.vergibKennungen();
        const bhed::Kennung a = kennungAn(d, bhed::Path{0});
        const bhed::Kennung b = kennungAn(d, bhed::Path{0, 1});
        const bhed::Kennung c = kennungAn(d, bhed::Path{1});
        d.vergibKennungen();
        d.vergibKennungen();
        erwarte("Kennungen bleiben beim zweiten Durchlauf gleich",
                kennungAn(d, bhed::Path{0}) == a &&
                    kennungAn(d, bhed::Path{0, 1}) == b &&
                    kennungAn(d, bhed::Path{1}) == c, true);
        erwarte("und sie sind verschieden", a != b && b != c && a != c, true);
        erwarte("keine ist 0", a != 0 && b != 0 && c != 0, true);
    }

    if (g_fehl != 0) {
        std::printf("Kennungs-Proben: %d Fehlschlag(e)\n", g_fehl);
        return 1;
    }
    std::printf("Kennungs-Proben bestanden (0 Fehlschlaege)\n");
    return 0;
}
