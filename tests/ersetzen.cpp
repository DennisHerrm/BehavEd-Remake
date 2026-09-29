// Probe fuer das Ersetzen im Find-Fenster (ersetzeInKnoten, ersetzeImSkript).
//
// shank: "in the find window, let us replace the item containing text.
// Would be useful for cameras, as some are reused throughout the cutscene".
// Datenfrei: die Knoten werden hier von Hand gebaut.

#include "bhed/edit.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace bhed;

namespace {

int fehler = 0;

void erwarte(const char* was, bool ok) {
    std::printf("  %-4s  %s\n", ok ? "ok" : "FEHL", was);
    if (!ok) {
        ++fehler;
    }
}

Arg arg(Arg::Kind k, const std::string& text) {
    Arg a;
    a.kind = k;
    a.text = text;
    return a;
}

Node kamera(const std::string& art, const std::string& vec) {
    Node n;
    n.kind = Node::Kind::Command;
    n.name = "camera";
    n.args.push_back(arg(Arg::Kind::Ident, art));
    n.args.push_back(arg(Arg::Kind::Vector, vec));
    n.args.push_back(arg(Arg::Kind::Number, "0.000"));
    return n;
}

Script skript() {
    Script s;
    s.nodes.push_back(kamera("MOVE", "1299.000 -2714.000 1082.000"));
    s.nodes.push_back(kamera("PAN", "-1.000 -173.000 0.000"));
    s.nodes.push_back(kamera("MOVE", "1308.000 -2710.000 1080.000"));
    s.nodes.push_back(kamera("MOVE", "1299.000 -2714.000 1082.000"));
    Node p;
    p.kind = Node::Kind::Command;
    p.name = "print";
    p.args.push_back(arg(Arg::Kind::String, "1299.000 -2714.000 1082.000"));
    s.nodes.push_back(p);
    return s;
}

}  // namespace

int main() {
    std::printf("ersetzen\n");
    const std::string alt = "1299.000 -2714.000 1082.000";

    // --- Ganzes Feld, nur Kameras, Vektor wie BehavEd geschrieben -----------
    {
        Script s = skript();
        FindOptions o;
        o.named = "camera";
        o.contains = alt;
        o.wholeString = true;
        const int n = ersetzeImSkript(s, o, "1300 -2700 1090");
        erwarte("zwei wiederverwendete Kamerastellungen ersetzt", n == 2);
        erwarte("mit drei Nachkommastellen wie BehavEd",
                s.nodes[0].args[1].text == "1300.000 -2700.000 1090.000" &&
                    s.nodes[3].args[1].text == "1300.000 -2700.000 1090.000");
        erwarte("die andere MOVE-Stellung bleibt",
                s.nodes[2].args[1].text == "1308.000 -2710.000 1080.000");
        erwarte("print (anderer Name) bleibt", s.nodes[4].args[0].text == alt);
        erwarte("der Befehlsname wird nie ersetzt", s.nodes[0].name == "camera");
    }
    // --- Ohne Namensfilter: auch das Textfeld, dort ohne Zahlenumbau -------
    {
        Script s = skript();
        FindOptions o;
        o.contains = alt;
        o.wholeString = true;
        erwarte("ohne 'called' alle drei Felder", ersetzeImSkript(s, o, "1300 -2700 1090") == 3);
        erwarte("ein Textfeld bleibt, wie getippt",
                s.nodes[4].args[0].text == "1300 -2700 1090");
    }
    // --- Teiltext, ohne Gross/klein, jedes Vorkommen ------------------------
    {
        Node n = kamera("MOVE", "1299.000 -2714.000 1299.000");
        FindOptions o;
        o.contains = "1299";
        o.wholeString = false;
        erwarte("Teiltext: beide Vorkommen", ersetzeInKnoten(n, o, "1300") == 2);
        erwarte("Teiltext: der Rest bleibt stehen",
                n.args[1].text == "1300.000 -2714.000 1300.000");
        Node m = kamera("move", "0 0 0");
        FindOptions o2;
        o2.contains = "MOVE";
        o2.wholeString = true;
        erwarte("ohne Gross/klein wie die Suche", ersetzeInKnoten(m, o2, "PAN") == 1 &&
                                                      m.args[0].text == "PAN");
    }
    // --- Gesperrte Makrofelder bleiben stehen -------------------------------
    {
        Node n = kamera("MOVE", alt);
        n.args[1].locked = true;
        FindOptions o;
        o.contains = alt;
        erwarte("gesperrtes Feld (/*!*/) bleibt", ersetzeInKnoten(n, o, "1 2 3") == 0 &&
                                                      n.args[1].text == alt);
    }
    // --- In selection -------------------------------------------------------
    {
        Script s = skript();
        FindOptions o;
        o.named = "camera";
        o.contains = alt;
        erwarte("In selection: nur der gewaehlte Treffer",
                ersetzeImSkript(s, o, "5 5 5", {Path{3}}) == 1 &&
                    s.nodes[0].args[1].text == alt &&
                    s.nodes[3].args[1].text == "5.000 5.000 5.000");
    }
    // --- Ein Rueckgaengig-Schritt -------------------------------------------
    {
        Document d(skript());
        Script kopie = d.script();
        FindOptions o;
        o.named = "camera";
        o.contains = alt;
        (void)ersetzeImSkript(kopie, o, "7 7 7");
        d.replaceAll(std::move(kopie), "replace");
        erwarte("nach Replace All ungesichert", d.dirty());
        d.undo();
        erwarte("EIN Strg+Z holt alles zurueck",
                d.script().nodes[0].args[1].text == alt &&
                    d.script().nodes[3].args[1].text == alt && !d.dirty());
    }

    // --- Gleiche Befehle (View: Highlight identical commands) -------------
    {
        const auto befehl = [](const char* name, std::vector<std::pair<Arg::Kind, std::string>> args) {
            Node n;
            n.name = name;
            for (const auto& [k, txt] : args) {
                Arg a;
                a.kind = k;
                a.text = txt;
                n.args.push_back(a);
            }
            return n;
        };
        const Node a = befehl("camera", {{Arg::Kind::Ident, "MOVE"},
                                         {Arg::Kind::Vector, "1162.000 -2675.000 1095.000"},
                                         {Arg::Kind::Number, "0.000"}});
        const Node gleich = befehl("camera", {{Arg::Kind::Ident, "MOVE"},
                                              {Arg::Kind::Vector, "1162 -2675 1095"},
                                              {Arg::Kind::Number, "0"}});
        const Node andereStelle = befehl("camera", {{Arg::Kind::Ident, "MOVE"},
                                                    {Arg::Kind::Vector, "1162.000 -2675.000 1096.000"},
                                                    {Arg::Kind::Number, "0.000"}});
        const Node pan = befehl("camera", {{Arg::Kind::Ident, "PAN"},
                                           {Arg::Kind::Vector, "1162.000 -2675.000 1095.000"},
                                           {Arg::Kind::Number, "0.000"}});
        const Node kleinGeschrieben = befehl("CAMERA", {{Arg::Kind::Ident, "move"},
                                                        {Arg::Kind::Vector, "1162.000 -2675.000 1095.000"},
                                                        {Arg::Kind::Number, "0.000"}});
        erwarte("gleich: 1162 und 1162.000 sind derselbe Wert", gleicherBefehl(a, gleich));
        erwarte("nicht gleich: eine Koordinate anders", !gleicherBefehl(a, andereStelle));
        erwarte("nicht gleich: PAN statt MOVE", !gleicherBefehl(a, pan));
        erwarte("gleich: Gross/klein zaehlt nicht (wie die Suche)", gleicherBefehl(a, kleinGeschrieben));
        Node leer1;
        leer1.kind = Node::Kind::Blank;
        erwarte("Leerzeilen gleichen sich nie", !gleicherBefehl(leer1, leer1));
        Node rem1;
        rem1.kind = Node::Kind::LineComment;
        rem1.raw = "// Anakin";
        Node rem2 = rem1;
        erwarte("Kommentare mit gleichem Text gleichen sich", gleicherBefehl(rem1, rem2));
        rem2.raw = "// Barriss";
        erwarte("... mit anderem Text nicht", !gleicherBefehl(rem1, rem2));
    }

    if (fehler != 0) {
        std::printf("FEHLGESCHLAGEN (%d)\n", fehler);
        return 1;
    }
    std::printf("alle Gegenproben bestanden (0 Fehlschlaege)\n");
    return 0;
}
