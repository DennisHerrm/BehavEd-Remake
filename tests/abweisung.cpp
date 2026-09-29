// Sagt das Modell, WARUM eine Bewegung nichts getan hat?
//
// shanks rc535-Protokoll:
//     Baum: "else" steht schon am Ende - nichts getan
// Der Grund stand nur im Protokoll. Jetzt liefert `Document` ihn als Grund,
// und die Oberflaeche macht daraus eine Zeile in der Statusleiste.
//
// Geprueft wird HIER das Modell - der Text ist Sache der Oberflaeche und
// steht in der Uebersetzungstabelle.
#include "bhed/edit.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

static int g_fehl = 0;
using A = bhed::Document::Abweisung;

static const char* name(A a) {
    switch (a) {
        case A::Keine:              return "Keine";
        case A::SchonAmEnde:        return "SchonAmEnde";
        case A::SchonGanzOben:      return "SchonGanzOben";
        case A::ZielGleichHerkunft: return "ZielGleichHerkunft";
        case A::ZielImGezogenen:    return "ZielImGezogenen";
        case A::ZielIstKeinBlock:   return "ZielIstKeinBlock";
        case A::WegUngueltig:       return "WegUngueltig";
    }
    return "?";
}

static void erwarte(const char* was, A ist, A soll) {
    if (ist == soll) { std::printf("  ok    %-44s -> %s\n", was, name(ist)); return; }
    std::printf("  FEHL  %-44s -> %s (erwartet %s)\n", was, name(ist), name(soll));
    ++g_fehl;
}

static bhed::Node bau(const char* n, bool blk = false) {
    bhed::Node x;
    x.kind = bhed::Node::Kind::Command;
    x.name = n;
    x.hasBlock = blk;
    return x;
}

// affect { a } b
static bhed::Document baue() {
    bhed::Script s;
    bhed::Node blk = bau("affect", true);
    blk.children.push_back(bau("a"));
    s.nodes.push_back(blk);
    s.nodes.push_back(bau("b"));
    return bhed::Document{s};
}

int main() {
    // shanks Fall: der einzige Knoten oben, auf die freie Flaeche gezogen.
    {
        bhed::Script s;
        s.nodes.push_back(bau("else", true));
        bhed::Document d{s};
        (void)d.moveToEnd(bhed::Path{0});
        erwarte("einziger Knoten oben -> freie Flaeche",
                d.letzteAbweisung(), A::SchonAmEnde);
    }
    // Dort abgelegt, wo er schon stand.
    {
        bhed::Document d = baue();
        (void)d.moveTo(bhed::Path{1}, bhed::Path{1});
        erwarte("auf sich selbst abgelegt",
                d.letzteAbweisung(), A::ZielGleichHerkunft);
    }
    // Ein Block in sich selbst.
    {
        bhed::Document d = baue();
        (void)d.moveTo(bhed::Path{0}, bhed::Path{0, 0});
        erwarte("Block in sein eigenes Kind",
                d.letzteAbweisung(), A::ZielImGezogenen);
    }
    // HINEIN in etwas ohne Rumpf.
    {
        bhed::Document d = baue();
        (void)d.moveInto(bhed::Path{0}, bhed::Path{1});
        erwarte("HINEIN in einen Befehl ohne { }",
                d.letzteAbweisung(), A::ZielIstKeinBlock);
    }
    // Ganz oben, Move up.
    {
        bhed::Document d = baue();
        (void)d.moveUp(bhed::Path{0});
        erwarte("Move up auf dem ersten Knoten oben",
                d.letzteAbweisung(), A::SchonGanzOben);
    }
    // Ganz unten, Move down.
    {
        bhed::Document d = baue();
        (void)d.moveDown(bhed::Path{1});
        erwarte("Move down auf dem letzten Knoten oben",
                d.letzteAbweisung(), A::SchonAmEnde);
    }
    // Und der wichtigste: ein Zug, der KLAPPT, hinterlaesst keinen Grund.
    // Sonst zeigte die Statusleiste eine alte Meldung zu einem gelungenen
    // Zug - schlimmer als keine.
    {
        bhed::Document d = baue();
        (void)d.moveUp(bhed::Path{1});                 // scheitert nicht
        erwarte("ein gelungener Zug setzt keinen Grund",
                d.letzteAbweisung(), A::Keine);
    }
    // Auch nach einem gescheiterten Zug muss der naechste gelungene
    // wieder aufraeumen.
    {
        bhed::Document d = baue();
        (void)d.moveDown(bhed::Path{1});               // scheitert
        (void)d.moveUp(bhed::Path{1});                 // gelingt
        erwarte("der naechste gelungene Zug raeumt auf",
                d.letzteAbweisung(), A::Keine);
    }

    if (g_fehl != 0) {
        std::printf("Abweisungs-Proben: %d Fehlschlag(e)\n", g_fehl);
        return 1;
    }
    std::printf("Abweisungs-Proben bestanden (0 Fehlschlaege)\n");
    return 0;
}
