// Makrozeilen im Baum: Zeiger und Sichtbarkeit.
//
// Ein Makro ist KEIN Block. Die Merkzeile `//$"standOnly"@3` sagt nur:
// die naechsten drei Befehle gehoeren zu mir. In der Datei stehen sie als
// Geschwister; das Zusammenfalten ist reine Anzeige.
//
// Daraus folgt zweierlei, und beides ging schief:
//
//  1. Wer etwas NEBEN die Makrozeile ablegt, landet mitten im Rumpf. Ist
//     das Makro zugeklappt, ist es unsichtbar. Das ist richtig so - aber
//     was im Rumpf steht, muss bei AUFGEKLAPPTEM Makro vollstaendig zu
//     sehen sein, Bloecke samt Inhalt.
//
//     shank zu rc538: "ich ziehe die Sachen auf loop und sie verschwinden -
//     gibt es keine Ebene 3?" Es gab keine.
//
//  2. Die Kinderzeilen zeigten auf Kopien in einem lokalen Vektor, der am
//     Ende des Schleifendurchlaufs starb. Unter ASan: heap-use-after-free.
//
// Diese Datei sollte MIT `-fsanitize=address` gebaut werden - sonst
// prueft sie Punkt 2 nicht.
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

static bhed::Node befehl(const char* n, bool blk = false) {
    bhed::Node x;
    x.kind = bhed::Node::Kind::Command;
    x.name = n;
    x.hasBlock = blk;
    return x;
}

static bhed::Node makro(const char* n, int anzahl) {
    bhed::Node x;
    x.kind = bhed::Node::Kind::Macro;
    x.name = n;
    x.count = anzahl;
    x.raw = std::string("//$\"") + n + "\"@" + std::to_string(anzahl);
    return x;
}

static std::vector<bhed::Row> zeilen(bhed::Document& d, bhed::Expanded& auf) {
    d.vergibKennungen();
    bhed::TreeOptions o;
    o.expanded = &auf;
    bhed::CommandDb db;
    std::vector<bhed::Row> r;
    bhed::buildTree(d.script(), db, o, r);
    return r;
}

static bool enthaelt(const std::vector<bhed::Row>& r, const char* name) {
    for (const bhed::Row& x : r) {
        if (x.name == name) { return true; }
    }
    return false;
}

static int tiefeVon(const std::vector<bhed::Row>& r, const char* name) {
    for (const bhed::Row& x : r) {
        if (x.name == name) { return x.depth; }
    }
    return -1;
}

int main() {
    // ---- 1. Die Zeiger der Makro-Kinder muessen gueltig bleiben -------
    //
    // Unter ASan ist genau DAS die Probe: nach buildTree noch einmal in
    // jeden Knoten hineinlesen. Vorher schlug hier heap-use-after-free zu.
    {
        bhed::Script sc;
        sc.nodes.push_back(makro("standOnly", 2));
        sc.nodes.push_back(befehl("set1"));
        sc.nodes.push_back(befehl("set2"));
        bhed::Document d{sc};
        bhed::Expanded auf;
        d.vergibKennungen();
        auf.setOpen(d.script().nodes[0].kennung, true);
        const std::vector<bhed::Row> r = zeilen(d, auf);
        erwarte("drei Zeilen: Makro plus zwei Kinder", r.size() == 3, true);
        bool alleGueltig = true;
        for (const bhed::Row& x : r) {
            if (x.node == nullptr) { alleGueltig = false; continue; }
            // Der Zugriff ist der Punkt - er darf nicht in totem Speicher
            // landen. Und der Name muss stimmen, nicht nur lesbar sein.
            if (x.node->name != x.name) { alleGueltig = false; }
        }
        erwarte("jede Zeile zeigt auf ihren ECHTEN Knoten", alleGueltig, true);
    }

    // ---- 2. Ein Block im Makrorumpf zeigt seinen Inhalt ---------------
    {
        bhed::Script sc;
        sc.nodes.push_back(makro("standOnly", 3));
        bhed::Node lp = befehl("loop", true);
        lp.children.push_back(befehl("drinnen"));
        sc.nodes.push_back(lp);              // Block IM Rumpf
        sc.nodes.push_back(befehl("set2"));
        sc.nodes.push_back(befehl("set3"));
        bhed::Document d{sc};
        bhed::Expanded auf;
        d.vergibKennungen();
        auf.setOpen(d.script().nodes[0].kennung, true);   // Makro auf
        auf.setOpen(d.script().nodes[1].kennung, true);   // loop auf
        const std::vector<bhed::Row> r = zeilen(d, auf);
        erwarte("der loop steht auf Ebene 1 unter dem Makro",
                tiefeVon(r, "loop") == 1, true);
        erwarte("und sein Inhalt auf Ebene 2 - es GIBT eine Ebene darunter",
                tiefeVon(r, "drinnen") == 2, true);
        erwarte("nichts aus dem Modell fehlt", r.size() == 5, true);
    }

    // ---- 3. Zugeklappter loop im Makrorumpf verbirgt nur seinen Inhalt -
    {
        bhed::Script sc;
        sc.nodes.push_back(makro("standOnly", 2));
        bhed::Node lp = befehl("loop", true);
        lp.children.push_back(befehl("drinnen"));
        sc.nodes.push_back(lp);
        sc.nodes.push_back(befehl("set2"));
        bhed::Document d{sc};
        bhed::Expanded auf;
        d.vergibKennungen();
        auf.setOpen(d.script().nodes[0].kennung, true);   // nur das Makro
        const std::vector<bhed::Row> r = zeilen(d, auf);
        erwarte("der loop ist zu sehen", enthaelt(r, "loop"), true);
        erwarte("sein Inhalt nicht", enthaelt(r, "drinnen"), false);
    }

    // ---- 4. Zugeklapptes Makro verbirgt den ganzen Rumpf --------------
    //
    // Das ist erwuenscht - aber es ist auch der Grund, warum das Ablegen
    // NEBEN einer Makrozeile so ueberrascht.
    {
        bhed::Script sc;
        sc.nodes.push_back(makro("standOnly", 2));
        sc.nodes.push_back(befehl("set1"));
        sc.nodes.push_back(befehl("set2"));
        sc.nodes.push_back(befehl("danach"));
        bhed::Document d{sc};
        bhed::Expanded auf;                   // nichts aufgeklappt
        const std::vector<bhed::Row> r = zeilen(d, auf);
        erwarte("Makrozeile sichtbar", enthaelt(r, "standOnly"), true);
        erwarte("Rumpf verborgen", enthaelt(r, "set1"), false);
        erwarte("was danach kommt, bleibt sichtbar",
                enthaelt(r, "danach"), true);
    }

    if (g_fehl != 0) {
        std::printf("Makrozeilen-Proben: %d Fehlschlag(e)\n", g_fehl);
        return 1;
    }
    std::printf("Makrozeilen-Proben bestanden (0 Fehlschlaege)\n");
    return 0;
}
