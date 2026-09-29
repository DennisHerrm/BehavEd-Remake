// Befehle aus einem Block heraus- und wieder hineinbewegen.
//
// shank: "bei den affect-, task-, if-, else-Bloecken kann ich die Befehle
// nicht aus dem Block herausbewegen."
//
// Geprueft wird am fertigen Skripttext, nicht an inneren Zeigern: nach der
// Bewegung wird geschrieben und verglichen. Was zaehlt, ist die Datei.
#include "bhed/edit.h"
#include "bhed/script.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

int g_fehler = 0;

void erwarte(const std::string& was, const std::string& ist,
             const std::string& soll) {
    if (ist != soll) {
        std::printf("  FEHL  %s\n        ist:  %s\n        soll: %s\n",
                    was.c_str(), ist.c_str(), soll.c_str());
        ++g_fehler;
    }
}

// Das Skript als eine Zeile, damit der Vergleich lesbar bleibt.
std::string flach(const bhed::Script& s) {
    std::string out;
    const std::function<void(const std::vector<bhed::Node>&)> geh =
        [&](const std::vector<bhed::Node>& ns) {
            for (const bhed::Node& n : ns) {
                if (!out.empty()) { out += " "; }
                out += n.name;
                if (!n.children.empty()) {
                    out += "{";
                    geh(n.children);
                    out += " }";
                }
            }
        };
    geh(s.nodes);
    return out;
}

bhed::Document baue() {
    // affect { a b } c
    bhed::Script s;
    bhed::Node blk;
    blk.kind = bhed::Node::Kind::Command;
    blk.name = "affect";
    blk.hasBlock = true;
    bhed::Node a;
    a.kind = bhed::Node::Kind::Command;
    a.name = "a";
    bhed::Node b = a;
    b.name = "b";
    blk.children.push_back(a);
    blk.children.push_back(b);
    bhed::Node c = a;
    c.name = "c";
    s.nodes.push_back(blk);
    s.nodes.push_back(c);
    return bhed::Document{s};
}

}  // namespace

int main() {
    // --- Erstes Kind nach oben: aus dem Block heraus, davor -------------
    {
        bhed::Document d = baue();
        const bool ok = d.moveUp(bhed::Path{0, 0});
        erwarte("moveUp meldet Erfolg", ok ? "ja" : "nein", "ja");
        erwarte("erstes Kind wandert VOR den Block", flach(d.script()),
                "a affect{ b } c");
    }

    // --- Letztes Kind nach unten: aus dem Block heraus, dahinter --------
    {
        bhed::Document d = baue();
        const bool ok = d.moveDown(bhed::Path{0, 1});
        erwarte("moveDown meldet Erfolg", ok ? "ja" : "nein", "ja");
        erwarte("letztes Kind wandert HINTER den Block", flach(d.script()),
                "affect{ a } b c");
    }

    // --- Innerhalb des Blocks bleibt es beim Tauschen -------------------
    {
        bhed::Document d = baue();
        (void)d.moveDown(bhed::Path{0, 0});
        erwarte("innen wird nur getauscht", flach(d.script()),
                "affect{ b a } c");
    }

    // --- Oberste Ebene: nach oben am Anfang tut nichts ------------------
    {
        bhed::Document d = baue();
        const bool ok = d.moveUp(bhed::Path{0});
        erwarte("oberste Ebene, ganz oben: kein Erfolg", ok ? "ja" : "nein",
                "nein");
        erwarte("und nichts veraendert", flach(d.script()),
                "affect{ a b } c");
    }

    // --- Heraus und zurueck ergibt wieder den Anfang --------------------
    {
        bhed::Document d = baue();
        (void)d.moveUp(bhed::Path{0, 0});     // a affect{ b } c
        (void)d.moveDown(bhed::Path{0});      // affect{ b } ... ?
        std::printf("  heraus und zurueck: %s\n", flach(d.script()).c_str());
    }

    // --- Die zurueckgemeldete Stelle -----------------------------------
    //
    // Der Teil, an dem es in rc516 haperte: die Bewegung stimmte, aber der
    // Aufrufer hat die Auswahl selbst nachgerechnet und lag daneben.
    auto wegAlsText = [](const bhed::Path& w) {
        std::string t;
        for (std::size_t x : w) {
            if (!t.empty()) { t += "/"; }
            t += std::to_string(x);
        }
        return t.empty() ? std::string("(leer)") : t;
    };
    {
        bhed::Document d = baue();
        bhed::Path nachher;
        (void)d.moveUp(bhed::Path{0, 0}, &nachher);
        erwarte("nach dem Austritt steht er an der Stelle des Blocks",
                wegAlsText(nachher), "0");
    }
    {
        bhed::Document d = baue();
        bhed::Path nachher;
        (void)d.moveDown(bhed::Path{0, 1}, &nachher);
        erwarte("nach unten heraus: direkt hinter dem Block",
                wegAlsText(nachher), "1");
    }
    {
        bhed::Document d = baue();
        bhed::Path nachher;
        (void)d.moveDown(bhed::Path{0, 0}, &nachher);
        erwarte("innen nach unten: eine Stelle weiter, gleiche Ebene",
                wegAlsText(nachher), "0/1");
    }

    // --- Verschachtelte Bloecke ----------------------------------------
    //
    // shanks Frage: "muss ich mehrere Baeume gleichzeitig testen?"
    //
    // Mehrfachauswahl nicht - die tut am Blockrand absichtlich nichts. Aber
    // VERSCHACHTELUNG war eine echte Luecke: bei `affect { task { a } }`
    // soll `a` eine Ebene je Druck steigen und im affect landen, nicht
    // gleich ganz heraus durchfallen.
    {
        bhed::Node a2;
        a2.kind = bhed::Node::Kind::Command;
        a2.name = "a";
        bhed::Node b2 = a2;
        b2.name = "b";
        bhed::Node task = a2;
        task.name = "task";
        task.hasBlock = true;
        task.children = {a2, b2};
        bhed::Node c2 = a2;
        c2.name = "c";
        bhed::Node aff = a2;
        aff.name = "affect";
        aff.hasBlock = true;
        aff.children = {task, c2};
        bhed::Node d2 = a2;
        d2.name = "d";
        bhed::Script sn;
        sn.nodes = {aff, d2};

        bhed::Document doc{sn};
        bhed::Path nachher;
        (void)doc.moveUp(bhed::Path{0, 0, 0}, &nachher);
        erwarte("aus dem inneren Block in den aeusseren", flach(doc.script()),
                "affect{ a task{ b } c } d");
        (void)doc.moveUp(nachher, &nachher);
        erwarte("beim zweiten Mal ganz heraus", flach(doc.script()),
                "a affect{ task{ b } c } d");
    }

    // --- Ziehen aus einem Block heraus ---------------------------------
    //
    // Das laeuft ueber `moveTo`, nicht ueber moveUp/moveDown - ein anderer
    // Weg, also eine eigene Probe. Geprueft wird, dass ein Befehl aus dem
    // Block auf die oberste Ebene gezogen werden kann.
    {
        bhed::Document d = baue();          // affect { a b } c
        const bool ok = d.moveTo(bhed::Path{0, 0}, bhed::Path{1});
        erwarte("Ziehen meldet Erfolg", ok ? "ja" : "nein", "ja");
        erwarte("aus dem Block hinter c gezogen", flach(d.script()),
                "affect{ b } c a");
    }

    // --- Auf den EIGENEN Block fallen lassen ---------------------------
    //
    // shank: "Test 3 geht nicht, er setzt es nicht raus."
    //
    // Die natuerliche Geste beim Herausziehen ist, das Kind auf seinen
    // Block zu ziehen. Vorher landete es dadurch wieder DRIN - `moveInto`
    // auf den Elternknoten aendert nichts, also sah man keine Wirkung.
    //
    // Diese Probe deckt die Modellseite ab: `moveTo` auf den Elternweg muss
    // den Befehl HINTER den Block setzen, eine Ebene hoeher. Dass die
    // Oberflaeche jetzt diesen Weg waehlt statt `moveInto`, steht in
    // gui/app.cpp.
    {
        bhed::Document d = baue();          // affect { a b } c
        const bool ok = d.moveTo(bhed::Path{0, 0}, bhed::Path{0});
        erwarte("auf den eigenen Block: Erfolg", ok ? "ja" : "nein", "ja");
        erwarte("landet HINTER dem Block, nicht darin", flach(d.script()),
                "affect{ b } a c");
    }

    // --- Ans Ende der obersten Ebene (freie Flaeche) -------------------
    {
        bhed::Document d = baue();          // affect { a b } c
        const bool ok = d.moveToEnd(bhed::Path{0, 0});
        erwarte("moveToEnd meldet Erfolg", ok ? "ja" : "nein", "ja");
        erwarte("aus dem Block ans Ende", flach(d.script()),
                "affect{ b } c a");
        erwarte("genau EIN Rueckgaengig-Schritt",
                d.undo() && flach(d.script()) == "affect{ a b } c" ? "ja"
                                                                  : "nein",
                "ja");
    }
    {
        bhed::Document d = baue();
        erwarte("schon am Ende: kein Zug",
                d.moveToEnd(bhed::Path{1}) ? "ja" : "nein", "nein");
    }

    // --- Freie Flaeche: NEBEN den Block, nicht ans Ende ----------------
    //
    // shank: "er setzt es in dem Plus einfach nur ganz nach unten statt
    // raus neben der Funktion mit dem Plus."
    //
    // Bei `affect { a b } c` soll `a` direkt HINTER `affect` landen, also
    // vor `c` - nicht hinter `c`. Sonst muesste man es von Hand wieder
    // hochschieben.
    {
        bhed::Document d = baue();          // affect { a b } c
        bhed::Path eltern{0};
        const bool ok = d.moveTo(bhed::Path{0, 0}, eltern);
        erwarte("aus dem Block neben den Block", ok ? "ja" : "nein", "ja");
        erwarte("direkt hinter affect, VOR c", flach(d.script()),
                "affect{ b } a c");
    }
    // Zum Vergleich: ans Ende waere etwas anderes.
    {
        bhed::Document d = baue();
        (void)d.moveToEnd(bhed::Path{0, 0});
        erwarte("ans Ende landet dagegen HINTER c", flach(d.script()),
                "affect{ b } c a");
    }

    // --- Duplizieren, auch mehrere auf einmal --------------------------
    //
    // shank zu rc542: "The 'Duplicate' button doesn't work when multiple
    // commands are highlighted. It can only duplicate one command at a
    // time." Loeschen und Kopieren konnten es laengst.
    {
        bhed::Document d = baue();          // affect{ a b } c
        erwarte("einer allein geht wie bisher",
                d.cloneAt(bhed::Path{1}) ? flach(d.script()) : "FEHLER",
                "affect{ a b } c c");
    }
    {
        bhed::Document d = baue();
        std::vector<bhed::Path> zwei{bhed::Path{0, 0}, bhed::Path{0, 1}};
        erwarte("zwei Kinder auf einmal",
                d.cloneAll(zwei) ? flach(d.script()) : "FEHLER",
                "affect{ a a b b } c");
    }
    {
        // Von hinten nach vorn: sonst verschiebt die erste Kopie die Wege
        // der folgenden, und die zweite trifft den falschen Knoten.
        bhed::Document d = baue();
        std::vector<bhed::Path> gemischt{bhed::Path{1}, bhed::Path{0}};
        erwarte("Reihenfolge der Wege ist egal",
                d.cloneAll(gemischt) ? flach(d.script()) : "FEHLER",
                "affect{ a b } affect{ a b } c c");
    }
    {
        bhed::Document d = baue();
        erwarte("leere Liste tut nichts",
                d.cloneAll({}) ? "TAT ETWAS" : flach(d.script()),
                "affect{ a b } c");
    }
    {
        // Eine Geste, ein Rueckgaengig-Schritt - wie beim Makro in rc534.
        bhed::Document d = baue();
        std::vector<bhed::Path> zwei{bhed::Path{0, 0}, bhed::Path{0, 1}};
        (void)d.cloneAll(zwei);
        erwarte("EIN Strg+Z nimmt beide Kopien zurueck",
                d.undo() ? flach(d.script()) : "FEHLER",
                "affect{ a b } c");
    }

    // --- moveAllAt: die drei Zonen beim Ziehen im Baum (27.09.) ------------
    //
    // Ausgangslage immer: affect{ a b } c
    using St = bhed::Document::Stelle;
    const auto zug = [](std::vector<bhed::Path> von, const bhed::Path& anker, St wo) {
        bhed::Document d = baue();
        bhed::Path gelandet;
        if (!d.moveAllAt(std::move(von), anker, wo, &gelandet)) { return std::string("ABGEWIESEN"); }
        std::string w;
        for (std::size_t k : gelandet) { w += "/" + std::to_string(k); }
        return flach(d.script()) + " @" + w;
    };
    erwarte("c DAVOR den Block", zug({{1}}, {0}, St::Davor), "c affect{ a b } @/0");
    erwarte("c HINEIN (an den Anfang)", zug({{1}}, {0}, St::Hinein), "affect{ c a b } @/0/0");
    erwarte("c DAVOR b (mitten in den Block)", zug({{1}}, {0, 1}, St::Davor), "affect{ a c b } @/0/1");
    erwarte("a DAHINTER den Block (heraus)", zug({{0, 0}}, {0}, St::Dahinter), "affect{ b } a c @/1");
    erwarte("a DAHINTER c", zug({{0, 0}}, {1}, St::Dahinter), "affect{ b } c a @/2");
    erwarte("b DAVOR a (im Block nach oben)", zug({{0, 1}}, {0, 0}, St::Davor), "affect{ b a } c @/0/0");
    erwarte("a und b zusammen hinter c, Reihenfolge bleibt", zug({{0, 0}, {0, 1}}, {1}, St::Dahinter),
            "affect c a b @/2");
    erwarte("der Block mit seinem Kind gewaehlt: das Kind zieht mit", zug({{0}, {0, 0}}, {1}, St::Dahinter),
            "c affect{ a b } @/1");
    erwarte("nicht vor sich selbst", zug({{1}}, {1}, St::Davor), "ABGEWIESEN");
    erwarte("den Block nicht in sein eigenes Kind", zug({{0}}, {0, 1}, St::Davor), "ABGEWIESEN");
    erwarte("nicht HINEIN in etwas, das kein Block ist", zug({{0, 0}}, {1}, St::Hinein), "ABGEWIESEN");
    {
        bhed::Document d = baue();
        (void)d.moveAllAt({{0, 0}, {0, 1}}, {1}, St::Dahinter);
        erwarte("EIN Strg+Z nimmt den ganzen Zug zurueck", d.undo() ? flach(d.script()) : "FEHLER",
                "affect{ a b } c");
    }

    // --- Makros als Behaelter (am Original nachgesehen, 27.09.) ------------
    //
    // In der Datei flach: //$"standOnly"@3 und drei Befehle dahinter. Im
    // Original echte Kinder: hinein = an den Anfang, das Makro zaehlt mit,
    // die Makrozeile nimmt ihre Befehle mit.
    {
        const std::string kText =
            "wait ( 1.000 );\n//$\"standOnly\"@3\nset ( \"a\", \"1\" );\nset ( \"b\", \"1\" );\n"
            "set ( \"c\", \"1\" );\nwait ( 2.000 );\n";
        const auto neuDok = [&](const std::string& text) {
            bhed::Script s;
            std::vector<bhed::Diag> dg;
            (void)bhed::readScript("//Generated by BehavEd\n" + text, s, dg);
            return bhed::Document(s);
        };
        // wait1 $@3 a b c wait2 - Befehle mit erstem Argument, Makros mit Zahl
        const auto zeig = [](const bhed::Document& d) {
            std::string out;
            for (const bhed::Node& n : d.script().nodes) {
                if (n.kind == bhed::Node::Kind::Blank) { continue; }
                if (!out.empty()) { out += " "; }
                if (n.kind == bhed::Node::Kind::Macro) {
                    out += "$" + n.name + "@" + std::to_string(n.count);
                } else if (n.kind == bhed::Node::Kind::Command) {
                    std::string a0 = n.args.empty() ? std::string() : n.args[0].text;
                    a0.erase(std::remove(a0.begin(), a0.end(), '"'), a0.end());
                    out += (n.name == "wait") ? "w" + a0.substr(0, 1) : a0;
                } else {
                    out += "//";
                }
            }
            return out;
        };
        using St = bhed::Document::Stelle;
        bhed::Node x;
        x.kind = bhed::Node::Kind::Command;
        x.name = "set";
        {
            bhed::Arg ar;
            ar.kind = bhed::Arg::Kind::String;
            ar.text = "\"x\"";
            x.args.push_back(ar);
        }
        const auto fall = [&](const char* was, const std::function<void(bhed::Document&)>& tu,
                              const std::string& soll) {
            bhed::Document d = neuDok(kText);
            tu(d);
            erwarte(std::string("Makro: ") + was, zeig(d), soll);
        };
        // Wege: 0 wait1, 1 Makro, 2 a, 3 b, 4 c, 5 wait2
        fall("auf die Makrozeile gezogen = hinein, ganz oben",
             [](bhed::Document& d) { (void)d.moveAllAt({{5}}, {1}, St::Hinein); }, "w1 $standOnly@4 w2 a b c");
        fall("hinter die Makrozeile = hinter das ganze Makro",
             [](bhed::Document& d) { (void)d.moveAllAt({{0}}, {1}, St::Dahinter); }, "$standOnly@3 a b c w1 w2");
        fall("vor einen Befehl des Makros = hinein",
             [](bhed::Document& d) { (void)d.moveAllAt({{5}}, {3}, St::Davor); }, "w1 $standOnly@4 a w2 b c");
        fall("hinter den letzten Befehl des Makros = hinein",
             [](bhed::Document& d) { (void)d.moveAllAt({{0}}, {4}, St::Dahinter); }, "$standOnly@4 a b c w1 w2");
        fall("ein Befehl heraus", [](bhed::Document& d) { (void)d.moveAllAt({{3}}, {5}, St::Dahinter); },
             "w1 $standOnly@2 a c w2 b");
        fall("die Makrozeile zieht ihre Befehle mit",
             [](bhed::Document& d) { (void)d.moveAllAt({{1}}, {5}, St::Dahinter); }, "w1 w2 $standOnly@3 a b c");
        fall("Makro loeschen = samt Befehlen", [](bhed::Document& d) { (void)d.removeAll({{1}}); }, "w1 w2");
        fall("Makro duplizieren = samt Befehlen, dahinter", [](bhed::Document& d) { (void)d.cloneAll({{1}}); },
             "w1 $standOnly@3 a b c $standOnly@3 a b c w2");
        fall("einen Befehl im Makro duplizieren = bleibt drin", [](bhed::Document& d) { (void)d.cloneAll({{3}}); },
             "w1 $standOnly@4 a b b c w2");
        fall("hinter den letzten Befehl einfuegen = hinein",
             [&](bhed::Document& d) { (void)d.insertAfter({4}, x); }, "w1 $standOnly@4 a b c x w2");
        fall("hinter die Makrozeile einfuegen = hinter das Makro",
             [&](bhed::Document& d) { (void)d.insertAfter({1}, x); }, "w1 $standOnly@3 a b c x w2");
        fall("in das Makro einfuegen = an den Anfang",
             [&](bhed::Document& d) { (void)d.insertInto({1}, 0, x); }, "w1 $standOnly@4 x a b c w2");
        fall("Move up: der erste Befehl tritt nach oben aus",
             [](bhed::Document& d) { (void)d.moveUp({2}); }, "w1 a $standOnly@2 b c w2");
        fall("Move down: der letzte Befehl tritt nach unten aus",
             [](bhed::Document& d) { (void)d.moveDown({4}); }, "w1 $standOnly@2 a b c w2");
        fall("Move down ueber ein Makro springt ueber das ganze",
             [](bhed::Document& d) { (void)d.moveDown({0}); }, "$standOnly@3 a b c w1 w2");
        fall("Move up ueber ein Makro springt ueber das ganze",
             [](bhed::Document& d) { (void)d.moveUp({5}); }, "w1 w2 $standOnly@3 a b c");
        fall("Move up auf der Makrozeile bewegt das ganze Makro",
             [](bhed::Document& d) { (void)d.moveUp({1}); }, "$standOnly@3 a b c w1 w2");
        fall("Move down auf der Makrozeile bewegt das ganze Makro",
             [](bhed::Document& d) { (void)d.moveDown({1}); }, "w1 w2 $standOnly@3 a b c");
        fall("Einfuegen aus der Ablage hinter einen Befehl des Makros = hinein",
             [&](bhed::Document& d) { d.setClipboard({x}); (void)d.pasteAfter({2}); }, "w1 $standOnly@4 a x b c w2");
        fall("Einfuegen aus der Ablage in das Makro = an den Anfang",
             [&](bhed::Document& d) { d.setClipboard({x}); (void)d.pasteInto({1}); }, "w1 $standOnly@4 x a b c w2");
        {
            bhed::Document d = neuDok(kText);
            (void)d.moveAllAt({{5}}, {1}, St::Hinein);
            (void)d.undo();
            erwarte("Makro: Strg+Z stellt auch die Zahl wieder her", zeig(d), "w1 $standOnly@3 a b c w2");
        }
        {
            // Eine Zahl, die nicht zu den Befehlen passt, bleibt, solange das
            // Makro nicht betroffen ist - der Rundlauf muss bytegleich bleiben.
            bhed::Document d = neuDok("//$\"walkOnly\"@5\nset ( \"a\", \"1\" );\n// ende\nwait ( 1.000 );\n");
            (void)d.insertAfter({3}, x);
            erwarte("Makro: eine unpassende Zahl bleibt unberuehrt", zeig(d), "$walkOnly@5 a // w1 x");
        }
    }

    if (g_fehler != 0) {
        std::printf("FEHLGESCHLAGEN (%d)\n", g_fehler);
        return 1;
    }
    std::printf("alle Blockbewegungsproben bestanden (0 Fehlschlaege)\n");
    return 0;
}
