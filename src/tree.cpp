// tree.cpp
#include "bhed/tree.h"

#include "bhed/diag.h"
#include "bhed/validate.h"

#include <cmath>

#include <cstddef>
#include <string>
#include <cctype>
#include <cstdio>

namespace bhed {
namespace {

// 1000.000 -> 1000 ,  0.500 -> 0.5   (Schalter "%g floats")
std::string trimZeros(const std::string& t) {
    if (t.find('.') == std::string::npos) {
        return t;
    }
    std::size_t e = t.size();
    while (e > 0 && t[e - 1] == '0') {
        --e;
    }
    if (e > 0 && t[e - 1] == '.') {
        --e;
    }
    return e == 0 ? "0" : t.substr(0, e);
}

void appendArg(std::string& o, const Arg& a, const TreeOptions& opt) {
    // "Show Types" schaltet die ganze Schreibweise um, nicht nur die
    // Typmenge. Abgelesen an Bildschirmfotos des Originals:
    //   aus:  affect ( anakin2, FLUSH )
    //         set ( SET_MORELIGHT, true )
    //         camera ( MOVE, -6048 7162 1214, 0 )
    //         rem ( ---------- Intro ---------- )
    //   ein:  set ( <SET_TYPES> "SET_PARM1", "DEFAULT" )
    //         move ( < 0 0 0 >, < 0 0 0 >, 1000 )
    // Ohne Typen also weder Anfuehrungszeichen noch spitze Klammern noch
    // Dollarzeichen - die Baumansicht zeigt nur den Wert.
    // "%g floats" KUERZT die Zahlen - so, wie der C-Formatbuchstabe %g es
    // tut. Die Beschreibungen sagen das Gegenteil:
    //
    //   JKHub-Tutorial: "Shows verbose Floating Point Numbers. Sets the
    //                    editor whether or not to show all decimal-places"
    //   OpenJK-Wiki:    "%g floats - Shows all floats with several decimal
    //                    places (default shows only decimal places used)"
    //
    // Das Bildschirmfoto vom 11.08.2026 entscheidet dagegen: dort ist der
    // Knopf "%g floats" sichtbar GEDRUECKT (versenkt, mit Fokusrahmen),
    // "Show Types" nicht - und die Zahlen stehen gekuerzt da:
    //     wait ( 3000 )
    //     camera ( MOVE, -6048 7162 1214, 0 )
    // Gemessen schlaegt beschrieben.
    //
    // Nebenbei kuerzt der Schalter auch die Bestandteile eines Vektors:
    //     < -6048.000 7162.000 1214.000 >   ->   -6048 7162 1214
    auto shorten = [&](const std::string& t) {
        if (!opt.gFloats) {
            return t;
        }
        std::string out;
        std::size_t i = 0;
        while (i < t.size()) {
            const std::size_t sp = t.find(' ', i);
            const std::size_t end = (sp == std::string::npos) ? t.size() : sp;
            if (!out.empty()) {
                out += ' ';
            }
            out += trimZeros(t.substr(i, end - i));
            i = (sp == std::string::npos) ? t.size() : sp + 1;
        }
        return out;
    };

    // Gesperrte Felder aus einer Makroausklappung tragen "(R)" und behalten
    // ihre Anfuehrungszeichen - auch bei ausgeschaltetem "Show Types":
    //     set ( (R)"SET_BEHAVIOR_STATE", (R)"BS_DEFAULT" )
    // So steht es im Bildschirmfoto des Originals. Der Buchstabe steht
    // vermutlich fuer "read-only"; in der Datei ist es /*!*/, in der .bhc
    // der %r-Vorsatz.
    if (a.locked) {
        o += "(R)";
        if (a.kind == Arg::Kind::String) {
            o += '"';
            o += a.text;
            o += '"';
        } else {
            o += a.text;
        }
        return;
    }

    if (!opt.showTypes) {
        o += (a.kind == Arg::Kind::Number || a.kind == Arg::Kind::Vector)
                 ? shorten(a.text)
                 : a.text;
        return;
    }
    if (!a.typeset.empty()) {
        o += '<';
        o += a.typeset;
        o += "> ";
    }
    switch (a.kind) {
        case Arg::Kind::String: o += '"'; o += a.text; o += '"'; break;
        case Arg::Kind::Expr:   o += '$'; o += a.text; o += '$'; break;
        case Arg::Kind::Vector: o += "< "; o += shorten(a.text); o += " >"; break;
        case Arg::Kind::Number: o += shorten(a.text); break;
        case Arg::Kind::Ident:  o += a.text; break;
    }
}

bool anyLocked(const Node& n) {
    for (const Arg& a : n.args) {
        if (a.locked) {
            return true;
        }
    }
    return false;
}

const Command* pick(const Node& n, const CommandDb& db) {
    // Dieselbe Wahl wie Editor und Pruefer - siehe selectOverload.
    return selectOverload(n, db);
}

void walk(const std::vector<Node>& ns, const CommandDb& db,
          const TreeOptions& opt, int depth, Path& prefix, std::vector<Row>& out) {
    // Auskommentierte Bloecke MIT Struktur zeigen, wie das Original.
    //
    // In der Datei ist ein auskommentierter Block eine Folge von Zeilen
    // "//(BHVDREM)  ..." (das bleibt so - Ravens Skripte muessen bytegleich
    // bleiben). Bisher stand jede davon als flache Textzeile im Baum, samt
    // "{" und "}". Das Original zeigt den Block als Block: Kopf, darunter
    // eingerueckt der Inhalt, mit "/////////////" davor, auf- und
    // zuklappbar.
    //
    // `remTiefe` ist die Klammertiefe innerhalb der laufenden REM-Folge,
    // `verborgenAb` die Tiefe, ab der ein zugeklappter REM-Block nichts
    // zeigt (-1: nichts verborgen).
    int remTiefe = 0;
    int verborgenAb = -1;
    auto remText = [](const Node& k, std::string* s) {
        return k.kind == Node::Kind::LineComment && remInhalt(k.raw, s);
    };
    for (std::size_t i = 0; i < ns.size(); ++i) {
        const Node& n = ns[i];
        {
            std::string inhalt;
            if (remText(n, &inhalt)) {
                if (inhalt == "{" || inhalt == "}") {
                    // Klammerzeilen sind Struktur, keine Zeile im Baum.
                    if (inhalt == "{") {
                        ++remTiefe;
                    } else if (remTiefe > 0) {
                        --remTiefe;
                        if (verborgenAb >= 0 && remTiefe <= verborgenAb) {
                            verborgenAb = -1;
                        }
                    }
                    continue;
                }
                if (verborgenAb >= 0 && remTiefe > verborgenAb) {
                    continue;   // im zugeklappten Block
                }
                // Folgt ein "{"? Dann ist diese Zeile der Kopf eines Blocks;
                // gezaehlt werden die sichtbaren Zeilen darin.
                std::size_t q = i + 1;
                while (q < ns.size() && ns[q].kind == Node::Kind::Blank) { ++q; }
                std::string naechste;
                int kinder = 0;
                if (q < ns.size() && remText(ns[q], &naechste) && naechste == "{") {
                    int tf = 0;
                    for (std::size_t z = q; z < ns.size(); ++z) {
                        std::string s;
                        if (ns[z].kind == Node::Kind::Blank) { continue; }
                        if (!remText(ns[z], &s)) { break; }
                        if (s == "{") { ++tf; continue; }
                        if (s == "}") { if (--tf == 0) { break; } continue; }
                        if (tf == 1) { ++kinder; }
                    }
                }
                Row r;
                r.what = Row::What::Comment;
                r.kennung = n.kennung;
                prefix.push_back(i);
                r.path = prefix;
                prefix.pop_back();
                r.depth = depth + remTiefe;
                r.icon = "I_MACRO";
                r.text = "/////////////  " + inhalt;
                r.node = &n;
                r.childCount = kinder;
                const bool offen = (kinder == 0) || (opt.expanded == nullptr) ||
                                   opt.expanded->isOpen(n.kennung);
                r.open = offen;
                out.push_back(std::move(r));
                if (kinder > 0 && !offen) {
                    verborgenAb = remTiefe;
                }
                continue;
            }
            if (n.kind != Node::Kind::Blank) {
                remTiefe = 0;       // die REM-Folge ist zu Ende
                verborgenAb = -1;
            }
        }
        prefix.push_back(i);
        // Fuenferregel: wer einen Destruktor hat, braucht eine Aussage zu
        // Kopieren und Verschieben.
        //
        // Eine Wache wie diese darf NICHT kopiert werden - eine Kopie wuerde
        // beim Verlassen ein zweites Mal pop_back() rufen und den Pfad
        // zerstoeren. Sie stillschweigend kopierbar zu lassen heisst, sich
        // auf die Aufmerksamkeit des naechsten Lesers zu verlassen.
        // (C++ Core Guidelines, C.21)
        struct PopGuard {
            Path& p;
            explicit PopGuard(Path& path) : p(path) {}
            ~PopGuard() { p.pop_back(); }
            PopGuard(const PopGuard&) = delete;
            PopGuard& operator=(const PopGuard&) = delete;
            PopGuard(PopGuard&&) = delete;
            PopGuard& operator=(PopGuard&&) = delete;
        } guard{prefix};

        if (n.kind == Node::Kind::Blank) {
            if (opt.showBlanks) {
                Row r;
                r.what = Row::What::Blank;
            r.kennung = n.kennung;
            r.path = prefix;
                r.depth = depth;
                r.icon = "I_SPACE";
                r.node = &n;
                out.push_back(std::move(r));
            }
            continue;
        }
        if (n.kind == Node::Kind::LineComment) {
            Row r;
            r.what = Row::What::Comment;
            r.kennung = n.kennung;
            r.path = prefix;
            r.depth = depth;
            r.icon = "I_MACRO";
            r.text = n.raw;
            r.node = &n;
            out.push_back(std::move(r));
            continue;
        }
        if (n.kind == Node::Kind::Macro) {
            Row r;
            r.what = Row::What::Macro;
            r.kennung = n.kennung;
            r.path = prefix;
            r.depth = depth;
            r.icon = "I_MACRO";
            r.text = n.name;
            // `name` blieb hier leer - als einzige der vier Zeilenarten.
            //
            // Folge in shanks rc530-Protokoll: die Zeile, die eigens fuer
            // die Makrofrage eingebaut wurde, druckte genau fuer
            // Makrozeilen einen leeren Namen:
            //
            //     Aufnehmen: "" von Weg 0/1, Tiefe 2
            //     Baum: "standOnly" gezogen, 0/1 -> 1 (Tiefe 2 -> 1)
            //
            // Derselbe Name, einmal aus der Zeile, einmal aus dem Knoten.
            // Ein Protokoll mit Loch ist schlimmer als keines.
            r.name = n.name;
            r.help = "Makro, erzeugt " + std::to_string(n.count) + " Befehle";
            r.node = &n;
            r.childCount = n.count;
            out.push_back(std::move(r));
            if (!opt.foldMacros) {
                continue;
            }
            // Die naechsten count Befehle gehoeren zum Makro. Im Original
            // stehen sie als EINGERUECKTE KINDER unter dem Makroknoten, mit
            // eigenem Aufklappkaestchen - nicht als Geschwister. Die Datei
            // bleibt trotzdem flach; das Falten ist nur Anzeige.
            const bool open = (opt.expanded == nullptr) || opt.expanded->isOpen(n.kennung);
            out.back().open = open;
            int want = n.count;
            // ZEIGER, keine Kopien.
            //
            // Hier stand `std::vector<Node> kids` und darunter
            // `kr.node = &kid` - ein Zeiger in einen Vektor, der am Ende
            // dieses Schleifendurchlaufs stirbt. Jede Makro-Kind-Zeile
            // trug danach einen Zeiger auf freigegebenen Speicher, und die
            // Oberflaeche liest `r.node` staendig.
            //
            // AddressSanitizer, an einem Makro mit zwei Befehlen:
            //     ERROR: heap-use-after-free ... READ of size 1
            //     freed by thread T0 here: std::vector<bhed::Node>::~vector
            //
            // Aufgefallen ist es nie, weil keine Probe Makrozeilen unter
            // ASan gebaut hat. `tests/makrozeilen.cpp` tut es jetzt.
            std::vector<const Node*> kids;
            while (want > 0 && i + 1 < ns.size()) {
                const Node& x = ns[i + 1];
                if (x.kind == Node::Kind::Command) {
                    --want;
                } else if (x.kind != Node::Kind::Blank) {
                    break;
                }
                ++i;
                if (open) {
                    kids.push_back(&x);
                }
            }
            // Die Wege der Kinder zeigen weiter auf ihre echte Stelle in
            // der Datei - sonst traefe "Loeschen" den falschen Knoten.
            //
            // Hier stand einmal: "Ein Makrorumpf enthaelt nur einfache
            // Befehle, keine Bloecke." Das stimmt nicht. Ein Makro ist kein
            // Block; sein Rumpf sind schlicht die naechsten count
            // Geschwister. Wer einen `loop` NEBEN die Makrozeile zieht,
            // hat einen Block im Rumpf - und der ist erlaubt, die Datei
            // bleibt flach.
            //
            // shank zu rc538: "ich ziehe die Sachen auf loop und sie
            // verschwinden - gibt es keine Ebene 3?" Es gab keine: der
            // Rumpf wurde flach gezeichnet, ohne Klammern und ohne Kinder.
            // Alles, was er in den loop zog, lag im Modell richtig und war
            // trotzdem unsichtbar.
            //
            // Nichts, was im Modell steht, darf in der Anzeige fehlen.
            const std::size_t first = prefix.back() + 1;
            for (std::size_t k = 0; k < kids.size(); ++k) {
                const Node& kid = *kids[k];
                Row kr;
                kr.what = Row::What::Command;
                kr.ausMakro = true;   // steht daneben, nicht darin
                kr.depth = depth + 1;
                kr.icon = iconFor(kid, db);
                kr.text = rowText(kid, opt);
                kr.name = kid.name;
                kr.args = rowArgs(kid, opt);
                const Command* kc = pick(kid, db);
                kr.help = (kc != nullptr) ? kc->desc : std::string{};
                kr.node = &kid;
                kr.locked = anyLocked(kid);
                kr.kennung = kid.kennung;
                kr.childCount = static_cast<int>(kid.children.size());
                kr.path = prefix;
                kr.path.back() = first + k;
                const bool kidAuf = (opt.expanded == nullptr) ||
                                    opt.expanded->isOpen(kid.kennung);
                kr.open = kidAuf;
                out.push_back(std::move(kr));
                if (kid.hasBlock && kidAuf) {
                    Path kp = prefix;
                    kp.back() = first + k;
                    walk(kid.children, db, opt, depth + 2, kp, out);
                }
            }
            continue;
        }

        const Command* c = pick(n, db);
        Row r;
        r.what = Row::What::Command;
            r.path = prefix;
        r.depth = depth;
        r.icon = iconFor(n, db);
        r.text = rowText(n, opt);
        r.name = n.name;
        r.args = rowArgs(n, opt);
        r.help = (c != nullptr) ? c->desc : "Befehl steht nicht in der .bhc";
        r.node = &n;
        r.kennung = n.kennung;
        r.childCount = static_cast<int>(n.children.size());
        r.locked = anyLocked(n);
        const bool open = (opt.expanded == nullptr) || opt.expanded->isOpen(n.kennung);
        r.open = open;
        out.push_back(std::move(r));
        if (n.hasBlock && open) {
            walk(n.children, db, opt, depth + 1, prefix, out);
        }
    }
}

} // namespace

bool Expanded::isOpen(Kennung k) const {
    // Kennung 0 heisst "noch keine vergeben" - ein solcher Knoten kann
    // keinen eigenen Zustand haben und folgt dem Grundzustand.
    if (k == 0) {
        return all_;
    }
    for (const Kennung q : open_) {
        if (q == k) {
            return !all_;   // Eintrag ist die Ausnahme zum Grundzustand
        }
    }
    return all_;
}

void Expanded::setOpen(Kennung k, bool open) {
    // shank hat mehrfach gemeldet, dass ein + von selbst zugeht. Die
    // Ursache war jedes Mal dieselbe: der Zustand hing an WEGEN, und jede
    // Strukturaenderung verschiebt Wege. Seit dem Umbau auf Kennungen kann
    // das nicht mehr passieren - die Zeile bleibt trotzdem, weil sie
    // mehrfach gezeigt hat, was wirklich geschieht, statt es raten zu
    // lassen.
    if (k == 0) {
        return;
    }
    diag::detail(std::string("Aufklappen: Kennung ") + std::to_string(k) +
                 " -> " + (open ? "AUF" : "ZU") + " (Grundzustand " +
                 (all_ ? "auf" : "zu") + ", " +
                 std::to_string(open_.size()) + " Ausnahmen)");
    const bool isException = (open != all_);
    for (std::size_t i = 0; i < open_.size(); ++i) {
        if (open_[i] == k) {
            if (!isException) {
                open_.erase(open_.begin() + static_cast<std::ptrdiff_t>(i));
            }
            return;
        }
    }
    if (isException) {
        open_.push_back(k);
    }
}

TreeMetrics treeMetricsFor(float fontHeight) {
    TreeMetrics m;
    m.indent = std::round(fontHeight * 1.46F);
    m.box = std::round(fontHeight * 0.70F);
    if (std::fmod(m.box, 2.0F) == 0.0F) {
        m.box += 1.0F;   // ungerade, damit der Strich im Kaestchen mittig sitzt
    }
    m.iconAt = std::round(fontHeight * 0.85F);
    m.nameAt = std::round(fontHeight * 2.54F);
    m.nameW = std::round(fontHeight * 4.54F);
    return m;
}

std::string rowArgs(const Node& n, const TreeOptions& opt) {
    std::string o = "( ";
    for (std::size_t i = 0; i < n.args.size(); ++i) {
        if (i != 0) {
            o += ", ";
        }
        appendArg(o, n.args[i], opt);
    }
    o += " )";
    return o;
}

// Eine Quelle, zwei Verwendungen: rowText ist genau Name + " " + rowArgs.
// Sonst laufen die beiden bei der naechsten Aenderung auseinander.
std::string rowText(const Node& n, const TreeOptions& opt) {
    return n.name + " " + rowArgs(n, opt);
}

std::string signatureText(const Command& c) {
    std::string o = "( ";
    for (std::size_t i = 0; i < c.params.size(); ++i) {
        if (i != 0) {
            o += ", ";
        }
        const Param& p = c.params[i];
        switch (p.kind) {
            case Param::Kind::TypeSet: {
                std::string low = p.typeset;
                for (char& ch : low) {
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
                o += "< E\"" + low + "\" >";
                break;
            }
            case Param::Kind::String: o += "<str>"; break;
            case Param::Kind::Int:    o += "<int>"; break;
            case Param::Kind::Float:  o += "<float>"; break;
            case Param::Kind::Vector: o += "<vec>"; break;
            case Param::Kind::Range:  o += "<range>"; break;
            // BehavEd zeigt auch den Vergleich als <expr>
            case Param::Kind::Expr:
            case Param::Kind::Op:     o += "<expr>"; break;
        }
    }
    o += " )";
    return o;
}

std::string iconFor(const Node& n, const CommandDb& db) {
    // Das Symbol steht in der .bhc, nicht im Quelltext:
    //   [I_SET] set( %t="SET_TYPES", %s="DEFAULT" );
    for (const Command* c : db.overloads(n.name)) {
        if (!c->icon.empty()) {
            return c->icon;
        }
    }
    // Ohne [I_xxx] in der .bhc: Blockbefehle bekommen die Klammern, alle
    // anderen das blaue e. Am Bildschirmfoto abgelesen - run, use, kill,
    // print, rem, declare, free und play tragen alle I_EVENT.
    return n.hasBlock ? "I_BRACE" : "I_EVENT";
}

void buildTree(const Script& s, const CommandDb& db, const TreeOptions& opt,
               std::vector<Row>& out) {
    out.clear();
    Path prefix;
    walk(s.nodes, db, opt, 0, prefix, out);

    // Nachlauf: welche Zeile ist die letzte ihres Blocks?
    //
    // Nur fuer die gepunkteten Linien der Anzeige. Eine Zeile ist die
    // letzte ihrer Ebene, wenn danach keine weitere auf DERSELBEN Ebene
    // kommt, bevor eine flachere auftaucht. Dort hoert die senkrechte
    // Linie auf halber Hoehe auf - am Original ausgemessen.
    //
    // Die flache Liste reicht dafuer; der Baum muss nicht noch einmal
    // durchlaufen werden.
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i].lastChild = true;
        for (std::size_t j = i + 1; j < out.size(); ++j) {
            if (out[j].depth < out[i].depth) {
                break;              // Block zu Ende: es war die letzte
            }
            if (out[j].depth == out[i].depth) {
                out[i].lastChild = false;
                break;
            }
        }
    }
}

} // namespace bhed
