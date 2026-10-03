// edit.cpp
#include "bhed/edit.h"
#include <cmath>

#include "bhed/diag.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <cstdlib>
#include <sstream>

namespace bhed {

int durationArgIndex(const Node& n) {
    if (n.kind != Node::Kind::Command || n.args.empty()) {
        return -1;
    }
    // wait ( 5000.000 ) - die Zahl selbst.
    //
    // Die zweite Form, wait ( "signalname" ), hat KEINE Dauer; sie wartet
    // auf ein Signal. Deshalb wird auf die ART des Arguments gesehen und
    // nicht nur auf den Namen des Befehls.
    if (n.name == "wait") {
        return n.args[0].kind == Arg::Kind::Number ? 0 : -1;
    }
    // camera ( UNTERBEFEHL, ..., dauer ) - das letzte Argument, wenn es
    // eine Zahl ist. ENABLE und DISABLE haben nur den Unterbefehl.
    if (n.name == "camera" && n.args.size() >= 2) {
        const std::size_t letzte = n.args.size() - 1U;
        if (n.args[letzte].kind == Arg::Kind::Number) {
            return static_cast<int>(letzte);
        }
        return -1;
    }
    return -1;
}

namespace {

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::vector<Node>* resolveSiblings(Script& s, const Path& p, std::size_t& idx) {
    if (p.empty()) {
        return nullptr;
    }
    std::vector<Node>* cur = &s.nodes;
    for (std::size_t d = 0; d + 1 < p.size(); ++d) {
        if (p[d] >= cur->size()) {
            return nullptr;
        }
        cur = &(*cur)[p[d]].children;
    }
    idx = p.back();
    return idx <= cur->size() ? cur : nullptr;
}

void collect(const std::vector<Node>& ns, const Path& prefix,
             const FindOptions& o, std::vector<Path>& out) {
    const std::string named = lower(o.named);
    const std::string cont = lower(o.contains);

    for (std::size_t i = 0; i < ns.size(); ++i) {
        const Node& n = ns[i];
        Path p = prefix;
        p.push_back(i);

        // Wie das Original (am laufenden BehavEd.exe beobachtet, siehe
        // ABGLEICH-VERHALTEN.md, Suche):
        //
        //   "called"      nur der BEFEHLSNAME, exakt, ohne Gross/klein.
        //                 `us` findet nichts, ein Argument wie `gonk_allies`
        //                 auch nicht.
        //   "containing"  jedes ARGUMENT einzeln. Mit "Whole-string" exakt,
        //                 sonst als Teiltext. Nicht der Befehlsname, und
        //                 nicht ueber Argumentgrenzen hinweg.
        //
        // Vorher hing "containing" Name und alle Argumente zu EINEM Text
        // zusammen. Mit "Whole-string" - im Original vorgewaehlt - traf das
        // praktisch nie.
        //
        // Sind beide Felder gefuellt, muessen beide passen.
        auto passt = [&](const std::string& text) {
            const std::string t = lower(text);
            return o.wholeString ? (t == cont) : (t.find(cont) != std::string::npos);
        };
        bool nameOk = true;
        if (!named.empty()) {
            // Ein gefaltetes Makro traegt seinen Namen ebenfalls als Namen.
            nameOk = (n.kind == Node::Kind::Command || n.kind == Node::Kind::Macro) &&
                     lower(n.name) == named;
        }
        bool inhaltOk = true;
        if (!cont.empty()) {
            inhaltOk = false;
            for (const Arg& a : n.args) {
                if (passt(a.text)) {
                    inhaltOk = true;
                    break;
                }
            }
            // Auskommentierte Zeilen (REM) haben nur ihren Rohtext. Darin
            // wird als Teiltext gesucht - ein ganzer Text waere dort nie
            // ein einzelnes Argument.
            if (!inhaltOk && !o.wholeString && !n.raw.empty() &&
                lower(n.raw).find(cont) != std::string::npos) {
                inhaltOk = true;
            }
        }
        const bool hit = (!named.empty() || !cont.empty()) && nameOk && inhaltOk;
        if (hit) {
            out.push_back(p);
        }
        collect(n.children, p, o, out);
    }
}

} // namespace

const Node* nodeAt(const Script& s, const Path& p) {
    if (p.empty()) {
        return nullptr;
    }
    const std::vector<Node>* cur = &s.nodes;
    const Node* n = nullptr;
    for (const std::size_t i : p) {
        if (i >= cur->size()) {
            return nullptr;
        }
        n = &(*cur)[i];
        cur = &n->children;
    }
    return n;
}

std::string defaultValueFor(const Param& p, const CommandDb& db) {
    // Eine Typmenge: den ersten Eintrag nehmen. Das ist der, den auch
    // Ravens BehavEd vorschlaegt - FLUSH bei affect, ENABLE bei camera.
    if (!p.typeset.empty()) {
        if (const TypeSet* ts = db.typeset(p.typeset)) {
            if (!ts->entries.empty()) {
                return ts->entries.front().name;
            }
        }
    }

    // "DEFAULT" ist Ravens Platzhalter fuer "hier gehoert etwas hin". Bei
    // Text ist das brauchbar, bei einer Zahl nicht: wer ein Feld fuer eine
    // Gleitkommazahl oeffnet, erwartet 0, nicht das Wort DEFAULT.
    const bool placeholder =
        p.def.empty() || p.def == "DEFAULT" || p.def == p.typeset;
    if (placeholder) {
        switch (p.kind) {
            case Param::Kind::Float:
                return "0.000";
            case Param::Kind::Int:
                return "0";
            case Param::Kind::Vector:
                return "0.000 0.000 0.000";
            default:
                break;
        }
    }
    return p.def;
}

// Die Felder des Editors: eigene Parameter des Befehls, plus die des
// gewaehlten Auswahllisteneintrags. Dieselbe Regel wie in expandFields, hier
// aber mit den ANGEZEIGTEN Vorgabewerten aus der Signatur ("DEFAULT",
// "FILENAME") - so steht es in den Bildschirmfotos des Originals.
std::vector<Param> editorFields(const Command& c, const CommandDb& db,
                                const std::vector<std::string>& werte) {
    std::vector<Param> out;
    std::size_t skip = 0;
    for (std::size_t i = 0; i < c.params.size(); ++i) {
        if (skip != 0) { --skip; continue; }
        out.push_back(c.params[i]);
        if (c.params[i].kind != Param::Kind::TypeSet) { continue; }
        const TypeSet* ts = db.typeset(c.params[i].typeset);
        if (ts == nullptr) {
            // --- Klappliste gibt es nicht -> Textfeld -------------------
            //
            // Gefunden von `tests/editorall.cpp`: `set` mit
            // SET_TACTICAL_SHOW und SET_TACTICAL_HIDE zeigt auf die Liste
            // "TACTICAL". Ravens eigenes Q3_Interface.h erklaert sie
            // (Zeile 288/289), und KEINE Kopfdatei definiert sie.
            //
            // Ein Fehler in den Daten von 1999, nicht in behaved - das
            // Original zeigt dort dieselbe leere Klappliste. Aber eine
            // leere Klappliste ist ein Feld, in das man nichts eintragen
            // kann: der Befehl laesst sich gar nicht schreiben.
            //
            // Als Textfeld kann man wenigstens tippen, was man braucht.
            out.back().kind = Param::Kind::String;
            continue;
        }
        const std::size_t at = out.size() - 1;
        if (at >= werte.size()) { continue; }
        const TypeEntry* e = ts->find(werte[at]);
        if (e == nullptr || e->params.empty()) { continue; }
        for (std::size_t k = 0; k < e->params.size(); ++k) {
            Param sp = e->params[k];
            // Auch hier: eine Klappliste, die es nicht gibt, wird ein
            // Textfeld. Die abhaengigen Felder kommen aus dem
            // Klapplisteneintrag, nicht aus der Signatur - der Rueckfall
            // oben erreicht sie nicht, und genau dort sitzt "TACTICAL".
            if (sp.kind == Param::Kind::TypeSet &&
                db.typeset(sp.typeset) == nullptr) {
                sp.kind = Param::Kind::String;
            }
            const std::size_t repl = i + 1 + k;
            if (repl < c.params.size() && !c.params[repl].def.empty()) {
                sp.def = c.params[repl].def;
            }
            out.push_back(sp);
        }
        skip = e->params.size();
    }
    return out;
}

Arg argForParam(const Param& p, const std::string& value, const CommandDb& db,
                const Arg* previous) {
    // Hat sich der Wert nicht geaendert, aendert sich das Argument gar nicht.
    //
    // Das ist der Grundsatz, nicht eine Ausnahme: ein Editor darf beim
    // blossen Oeffnen und Schliessen nichts umschreiben. Ravens Bestand hat
    // 47 Stellen wie  set ( "SET_AIM", "1" )  - der Eintrag deklariert %d,
    // die Zahl steht aber in Anfuehrungszeichen. Der Pruefer meldet das als
    // Anmerkung V008; stillschweigend geradeziehen darf der Editor es nicht.
    if (previous != nullptr && previous->text == value) {
        return *previous;
    }

    // --- Ein LEERES Zahlenfeld schreibt sonst gar nichts -----------------
    //
    // Gefunden von `tests/editorall.cpp`: jedes Feld jedes Befehls
    // nacheinander auf acht unangenehme Werte gesetzt, 312 Proben. Vier
    // schlugen an, alle mit demselben Muster:
    //
    //     wait   (  );
    //     loop   (  ) { }
    //     move   ( < 0.0 0.0 0.0 >, < 0.0 0.0 0.0 >,  );
    //     rotate ( < 0.0 0.0 0.0 >,  );
    //
    // Zwischen den Klammern steht nichts. Beim naechsten Oeffnen fehlt das
    // Argument - der Befehl ist kaputt, und im Editor sieht man es nicht.
    //
    // Wer ein Zahlenfeld leert und "Ok" drueckt, meint nicht "kein
    // Argument", sondern hat nur nichts eingetippt. Also die Vorgabe des
    // Feldes, wie sie beim Oeffnen dastand.
    //
    // Nur fuer ZAHLEN. Ein leeres Textfeld ist etwas anderes: "" ist eine
    // gueltige Zeichenkette und wird auch als solche geschrieben.
    std::string wert = value;
    if (wert.find_first_not_of(" \t") == std::string::npos &&
        (p.kind == Param::Kind::Float || p.kind == Param::Kind::Int ||
         p.kind == Param::Kind::Vector)) {
        wert = defaultValueFor(p, db);
    }

    Arg a;
    a.text = wert;
    // JEDES Feld kann einen Ausdruck aufnehmen - dafuer ist der Knopf
    // "Expr!" da, und das Original zeigt es woertlich an: "<expr> ..... was
    // <str>". Stand dort also schon ein $get(...)$, bleibt es einer, auch
    // wenn das Feld als %v oder %f deklariert ist.
    //     camera ( MOVE, $tag( "at1", ORIGIN)$, 0 )   %v haelt einen Ausdruck
    //     set ( "SET_ORIGIN", $tag( "press1", ORIGIN)$ )
    if (previous != nullptr && previous->kind == Arg::Kind::Expr) {
        a.kind = Arg::Kind::Expr;
        a.typeset = previous->typeset;
        a.locked = previous->locked;
        return a;
    }
    // Umgekehrt: in einem Ausdrucksfeld bleibt eine blanke Zahl blank.
    //     move ( $tag( "cp1", ORIGIN)$, 0.000 )   gegen   if ( ..., $2$ )
    if ((p.kind == Param::Kind::Expr || p.kind == Param::Kind::Op) &&
        previous != nullptr) {
        a.kind = previous->kind;
        a.typeset = previous->typeset;
        a.locked = previous->locked;
        return a;
    }
    if (previous != nullptr) {
        a.locked = previous->locked;   // /*!*/ aus einer Makroausklappung
    }
    switch (p.kind) {
        case Param::Kind::TypeSet: {
            const TypeSet* ts = db.typeset(p.typeset);
            a.kind = (ts != nullptr && ts->kind == 'i') ? Arg::Kind::Ident
                                                        : Arg::Kind::String;
            // Unveraenderte Werte behalten ihre Schreibweise, auch wenn sie
            // von der Normalform abweicht. Ravens Makroausklappungen tragen
            // keine /*@SET_TYPES*/-Annotation:
            //     set ( /*!*/ "SET_WALKING", /*!*/ "true" )
            // Wer sie beim blossen Oeffnen anhaengt, aendert 409 Zeilen im
            // Bestand, ohne dass jemand etwas bearbeitet haette.
            a.typeset = (previous != nullptr && previous->text == wert)
                            ? previous->typeset
                            : p.typeset;
            break;
        }
        case Param::Kind::Int:
        case Param::Kind::Float:
            a.kind = Arg::Kind::Number;
            // Komma als Dezimalzeichen (deutsche Tastatur): "1,5" stuende
            // sonst als  wait ( 1,5 )  in der Datei - zwei Argumente, der
            // Befehl ist kaputt (Code-Pruefung 03.10.).
            std::replace(a.text.begin(), a.text.end(), ',', '.');
            break;
        case Param::Kind::Vector:
            a.kind = Arg::Kind::Vector;
            break;
        case Param::Kind::Expr:
        case Param::Kind::Op:
            a.kind = Arg::Kind::Expr;
            break;
        default:
            a.kind = (previous != nullptr) ? previous->kind : Arg::Kind::String;
            if (previous != nullptr) { a.typeset = previous->typeset; }
            break;
    }
    return a;
}

std::vector<Node> makeMacroNodes(const Macro& m, const CommandDb& db) {
    std::vector<Node> out;

    Node marker;
    marker.kind = Node::Kind::Macro;
    marker.name = m.name;
    marker.count = static_cast<int>(m.body.size());
    marker.raw = "//$\"" + m.name + "\"@" + std::to_string(m.body.size());
    out.push_back(std::move(marker));

    for (const MacroLine& line : m.body) {
        Node n;
        n.kind = Node::Kind::Command;
        n.name = line.command;
        for (const Param& p : line.params) {
            Arg a = argForParam(p, p.def, db, nullptr);
            // Die Felder eines ausgeklappten Makros sind gesperrt. In der
            // .bhc steht dafuer %r, in der Datei /*!*/.
            a.locked = p.locked;
            // Und sie tragen KEINE /*@TYPMENGE*/-Annotation: Ravens
            // Ausklappungen haben keine, gemessen an 634 Vorkommen.
            a.typeset.clear();
            n.args.push_back(std::move(a));
        }
        out.push_back(std::move(n));
    }
    return out;
}

Node makeNode(const Command& c, const CommandDb& db) {
    Node n;
    n.kind = Node::Kind::Command;
    n.name = c.name;
    n.hasBlock = c.block;
    for (const Param& p : c.params) {
        Arg a;
        switch (p.kind) {
            case Param::Kind::TypeSet: {
                a.kind = Arg::Kind::String;
                a.typeset = p.typeset;
                const TypeSet* ts = db.typeset(p.typeset);
                if (ts != nullptr) {
                    if (ts->kind == 'i') {
                        a.kind = Arg::Kind::Ident;
                    }
                    if (!ts->entries.empty()) {
                        a.text = ts->entries.front().name;
                    }
                }
                break;
            }
            case Param::Kind::Vector:
                a.kind = Arg::Kind::Vector;
                a.text = defaultValueFor(p, db);
                break;
            case Param::Kind::Int:
            case Param::Kind::Float:
                a.kind = Arg::Kind::Number;
                // Ueber defaultValueFor, damit auch "DEFAULT" zu einer Null
                // wird - vorher wurde nur auf LEER geprueft, und in der .bhc
                // steht bei vielen Zahlenfeldern eben "DEFAULT".
                a.text = defaultValueFor(p, db);
                break;
            case Param::Kind::Expr:
            case Param::Kind::Op:
                a.kind = Arg::Kind::Expr;
                a.text = defaultValueFor(p, db);
                break;
            default:
                a.kind = Arg::Kind::String;
                a.text = defaultValueFor(p, db);
                break;
        }
        n.args.push_back(std::move(a));
    }
    return n;
}

namespace {
// Alle Zahlen eines Arguments, wenn es NUR aus Zahlen besteht.
bool zahlenAus(const std::string& text, std::vector<double>& aus) {
    std::istringstream is(text);
    std::string teil;
    aus.clear();
    while (is >> teil) {
        char* ende = nullptr;
        const double v = std::strtod(teil.c_str(), &ende);
        if (ende == teil.c_str() || *ende != '\0') {
            return false;
        }
        aus.push_back(v);
    }
    return !aus.empty();
}

bool gleichesArgument(const Arg& a, const Arg& b) {
    std::vector<double> za;
    std::vector<double> zb;
    if (zahlenAus(a.text, za) && zahlenAus(b.text, zb)) {
        if (za.size() != zb.size()) {
            return false;
        }
        for (std::size_t i = 0; i < za.size(); ++i) {
            if (std::fabs(za[i] - zb[i]) > 0.0005) {   // drei Nachkommastellen
                return false;
            }
        }
        return true;
    }
    if (a.text.size() != b.text.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.text.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a.text[i])) !=
            std::tolower(static_cast<unsigned char>(b.text[i]))) {
            return false;
        }
    }
    return true;
}
}  // namespace

bool gleicherBefehl(const Node& a, const Node& b) {
    if (a.kind != b.kind) {
        return false;
    }
    switch (a.kind) {
        case Node::Kind::Blank:
            return false;
        case Node::Kind::LineComment:
            return a.raw == b.raw;
        case Node::Kind::Macro:
        case Node::Kind::Command:
            break;
    }
    if (a.args.size() != b.args.size() || a.name.size() != b.name.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.name.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a.name[i])) !=
            std::tolower(static_cast<unsigned char>(b.name[i]))) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.args.size(); ++i) {
        if (!gleichesArgument(a.args[i], b.args[i])) {
            return false;
        }
    }
    return true;
}

std::vector<Path> findAll(const Script& s, const FindOptions& o) {
    std::vector<Path> out;
    if (o.named.empty() && o.contains.empty()) {
        return out;
    }
    collect(s.nodes, {}, o, out);
    return out;
}

namespace {

// Wie BehavEd Zahlen schreibt: drei Nachkommastellen. Gibt false zurueck,
// wenn `text` nicht genau `anzahl` Zahlen sind.
bool alsBehavEdZahlen(const std::string& text, int anzahl, std::string& aus) {
    std::istringstream is(text);
    std::string teil;
    std::string ergebnis;
    int gelesen = 0;
    while (is >> teil) {
        char* ende = nullptr;
        const double v = std::strtod(teil.c_str(), &ende);
        if (ende == teil.c_str() || *ende != '\0') {
            return false;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.3f", v);
        if (!ergebnis.empty()) { ergebnis += ' '; }
        ergebnis += buf;
        ++gelesen;
    }
    if (gelesen != anzahl) {
        return false;
    }
    aus = ergebnis;
    return true;
}

}  // namespace

int ersetzeInKnoten(Node& n, const FindOptions& o, const std::string& mit) {
    if (o.contains.empty()) {
        return 0;
    }
    const std::string cont = lower(o.contains);
    int zahl = 0;
    for (Arg& a : n.args) {
        if (a.locked) {
            continue;
        }
        const std::string t = lower(a.text);
        if (o.wholeString) {
            if (t != cont) {
                continue;
            }
            std::string neu = mit;
            std::string zahlen;
            if (a.kind == Arg::Kind::Number && alsBehavEdZahlen(mit, 1, zahlen)) {
                neu = zahlen;
            } else if (a.kind == Arg::Kind::Vector && alsBehavEdZahlen(mit, 3, zahlen)) {
                neu = zahlen;
            }
            a.text = neu;
            ++zahl;
            continue;
        }
        // Teiltext: jedes Vorkommen, ohne Gross/klein, von links nach rechts.
        std::size_t ab = 0;
        std::string ergebnis;
        bool getroffen = false;
        for (;;) {
            const std::size_t at = t.find(cont, ab);
            if (at == std::string::npos) {
                break;
            }
            ergebnis += a.text.substr(ab, at - ab);
            ergebnis += mit;
            ab = at + cont.size();
            getroffen = true;
            ++zahl;
        }
        if (getroffen) {
            ergebnis += a.text.substr(ab);
            a.text = ergebnis;
        }
    }
    return zahl;
}

int ersetzeImSkript(Script& s, const FindOptions& o, const std::string& mit,
                    const std::vector<Path>& nurIn) {
    int zahl = 0;
    for (const Path& p : findAll(s, o)) {
        if (!nurIn.empty()) {
            bool drin = false;
            for (const Path& q : nurIn) {
                if (p.size() >= q.size() && std::equal(q.begin(), q.end(), p.begin())) {
                    drin = true;
                    break;
                }
            }
            if (!drin) {
                continue;
            }
        }
        std::size_t idx = 0;
        std::vector<Node>* sib = resolveSiblings(s, p, idx);
        if (sib == nullptr || idx >= sib->size()) {
            continue;
        }
        zahl += ersetzeInKnoten((*sib)[idx], o, mit);
    }
    return zahl;
}

Document::Document(Script s) : s_(std::move(s)) {}

namespace {

// Baum ablaufen und Kennungen richtigstellen. Rekursiv, weil Bloecke
// Kinder haben.
void kennungenDurchlauf(std::vector<Node>& knoten,
                        std::unordered_set<Kennung>& gesehen,
                        Kennung& naechste) {
    for (Node& n : knoten) {
        if (n.kennung == 0 || !gesehen.insert(n.kennung).second) {
            // 0 oder Doppel. Eine freie suchen - der Zaehler laeuft vorwaerts,
            // aber ein Wert aus einem anderen Dokument kann darueber liegen.
            while (!gesehen.insert(naechste).second) {
                ++naechste;
            }
            n.kennung = naechste;
            ++naechste;
        }
        kennungenDurchlauf(n.children, gesehen, naechste);
    }
}

}  // namespace

void Document::vergibKennungen() {
    std::unordered_set<Kennung> gesehen;
    kennungenDurchlauf(s_.nodes, gesehen, naechsteKennung_);
}

// --- Makros als Behaelter --------------------------------------------------
namespace {

constexpr std::size_t kKeins = static_cast<std::size_t>(-1);

// Die Stellen der Befehle eines Makros in seiner Geschwisterliste: die
// naechsten `count` Befehle, Leerzeilen dazwischen zaehlen nicht, alles
// andere beendet es - genau wie der Baum sie faltet (tree.cpp).
std::vector<std::size_t> makroStellen(const std::vector<Node>& ns, std::size_t m) {
    std::vector<std::size_t> st;
    int will = ns[m].count;
    for (std::size_t q = m + 1; will > 0 && q < ns.size(); ++q) {
        if (ns[q].kind == Node::Kind::Command) {
            st.push_back(q);
            --will;
        } else if (ns[q].kind != Node::Kind::Blank) {
            break;
        }
    }
    return st;
}

// Die letzte Stelle, die zum Makro gehoert (bei einem leeren: es selbst).
std::size_t gruppenLetzte(const std::vector<Node>& ns, std::size_t m) {
    const std::vector<std::size_t> st = makroStellen(ns, m);
    return st.empty() ? m : st.back();
}

// Zu welchem Makro gehoert der Befehl auf Stelle i? kKeins, wenn keinem.
std::size_t makroVon(const std::vector<Node>& ns, std::size_t i) {
    if (i >= ns.size() || ns[i].kind != Node::Kind::Command) {
        return kKeins;
    }
    for (std::size_t j = i; j-- > 0;) {
        if (ns[j].kind == Node::Kind::Macro) {
            const std::vector<std::size_t> st = makroStellen(ns, j);
            return std::find(st.begin(), st.end(), i) != st.end() ? j : kKeins;
        }
        if (ns[j].kind == Node::Kind::LineComment) {
            return kKeins;
        }
    }
    return kKeins;
}

using MakroKarte = std::unordered_map<Kennung, std::vector<Kennung>>;

void sammleMakros(const std::vector<Node>& ns, MakroKarte& out) {
    for (std::size_t i = 0; i < ns.size(); ++i) {
        if (ns[i].kind == Node::Kind::Macro) {
            std::vector<Kennung>& m = out[ns[i].kennung];
            for (const std::size_t q : makroStellen(ns, i)) {
                m.push_back(ns[q].kennung);
            }
        }
        sammleMakros(ns[i].children, out);
    }
}

// Die Zahlen der Makros nach einer Bearbeitung nachfuehren. Zu einem Makro
// gehoert, was direkt dahinter steht und
//   * schon vorher dazugehoerte (und nicht ausdruecklich austritt),
//   * ausdruecklich hineinkommt (`rein`), oder
//   * ZWISCHEN seinen Befehlen gelandet ist (dahinter folgt wieder einer).
// Geaendert wird die Zahl nur, wenn sich die Zugehoerigkeit geaendert hat -
// eine Datei, deren Zahl nicht zu den Befehlen passt, bleibt sonst so, wie
// sie ist (der Rundlauf muss bytegleich bleiben).
void fuehreMakrosNach(std::vector<Node>& ns, const MakroKarte& vorher,
                      const std::unordered_map<Kennung, Kennung>& rein,
                      const std::unordered_set<Kennung>& raus) {
    for (std::size_t i = 0; i < ns.size(); ++i) {
        fuehreMakrosNach(ns[i].children, vorher, rein, raus);
        if (ns[i].kind != Node::Kind::Macro) {
            continue;
        }
        const auto it = vorher.find(ns[i].kennung);
        if (it == vorher.end()) {
            continue;   // neu entstanden (eingefuegt, kopiert) - Zahl stimmt
        }
        const Kennung mk = ns[i].kennung;
        const std::unordered_set<Kennung> alt(it->second.begin(), it->second.end());
        const auto hinein = [&](Kennung k) {
            const auto r = rein.find(k);
            return r != rein.end() && r->second == mk;
        };
        // Ausgetreten zaehlt, ausser es faellt ausdruecklich wieder hinein.
        const auto draussen = [&](Kennung k) { return raus.count(k) != 0U && !hinein(k); };
        const auto gehoert = [&](Kennung k) {
            if (hinein(k)) { return true; }
            if (raus.count(k) != 0U) { return false; }
            return alt.count(k) != 0U;
        };
        std::vector<Kennung> jetzt;
        std::size_t q = i + 1;
        while (q < ns.size()) {
            const Node& x = ns[q];
            if (x.kind == Node::Kind::Blank) { ++q; continue; }
            if (x.kind != Node::Kind::Command || draussen(x.kennung)) { break; }
            if (gehoert(x.kennung)) {
                jetzt.push_back(x.kennung);
                ++q;
                continue;
            }
            // Dazwischen gelandet? Ueber weitere Neue hinweg nachsehen, ob
            // wieder ein Befehl des Makros folgt.
            bool dazwischen = false;
            for (std::size_t r = q + 1; r < ns.size(); ++r) {
                if (ns[r].kind == Node::Kind::Blank) { continue; }
                if (ns[r].kind != Node::Kind::Command || draussen(ns[r].kennung)) { break; }
                if (alt.count(ns[r].kennung) != 0U) { dazwischen = true; }
                if (dazwischen || rein.count(ns[r].kennung) != 0U) { break; }
            }
            if (!dazwischen) { break; }
            jetzt.push_back(x.kennung);
            ++q;
        }
        if (jetzt != it->second) {
            ns[i].count = static_cast<int>(jetzt.size());
        }
    }
}

}  // namespace

// Haelt zu Beginn einer Bearbeitung fest, welche Befehle zu welchem Makro
// gehoeren, und fuehrt am Ende die Zahlen nach. Nur der aeusserste zaehlt:
// cutAll ruft copyAll und removeAll, und die Zahlen duerfen nur EINMAL
// nachgefuehrt werden, gegen den Stand vor dem ganzen Zug.
struct MakroWache {
    Document& d;
    bool aussen = false;
    std::uint64_t vorherZahl = 0;
    MakroKarte vorher;
    explicit MakroWache(Document& doc) : d(doc) {
        aussen = (d.wachTiefe_++ == 0);
        if (!aussen) {
            return;
        }
        d.vergibKennungen();
        vorherZahl = d.aenderungen_;
        d.beitritt_.clear();
        d.austritt_.clear();
        sammleMakros(d.s_.nodes, vorher);
    }
    MakroWache(const MakroWache&) = delete;
    MakroWache& operator=(const MakroWache&) = delete;
    ~MakroWache() {
        --d.wachTiefe_;
        if (!aussen) {
            return;
        }
        if (d.aenderungen_ != vorherZahl) {
            d.vergibKennungen();
            std::unordered_map<Kennung, Kennung> rein;
            for (const auto& [k, m] : d.beitritt_) { rein[k] = m; }
            const std::unordered_set<Kennung> raus(d.austritt_.begin(), d.austritt_.end());
            fuehreMakrosNach(d.s_.nodes, vorher, rein, raus);
        }
        d.beitritt_.clear();
        d.austritt_.clear();
    }
};

Kennung Document::kennungBei(const Path& p) {
    const Node* n = nodeAt(s_, p);
    return n != nullptr ? n->kennung : 0;
}

Path Document::makroZu(const Path& p) {
    std::size_t i = 0;
    const std::vector<Node>* sib = siblingsOf(p, i);
    if (sib == nullptr || i >= sib->size()) {
        return {};
    }
    const std::size_t m = makroVon(*sib, i);
    if (m == kKeins) {
        return {};
    }
    Path w = p;
    w.back() = m;
    return w;
}

Path Document::gruppenEnde(const Path& makro, const std::vector<Path>& ohne) {
    std::size_t i = 0;
    const std::vector<Node>* sib = siblingsOf(makro, i);
    if (sib == nullptr || i >= sib->size() || (*sib)[i].kind != Node::Kind::Macro) {
        return makro;
    }
    const std::vector<std::size_t> st = makroStellen(*sib, i);
    for (auto it = st.rbegin(); it != st.rend(); ++it) {
        Path w = makro;
        w.back() = *it;
        if (std::find(ohne.begin(), ohne.end(), w) == ohne.end()) {
            return w;
        }
    }
    return makro;
}

std::vector<Path> Document::mitGruppen(std::vector<Path> paths) {
    std::vector<Path> out = paths;
    for (const Path& p : paths) {
        std::size_t i = 0;
        const std::vector<Node>* sib = siblingsOf(p, i);
        if (sib == nullptr || i >= sib->size() || (*sib)[i].kind != Node::Kind::Macro) {
            continue;
        }
        // Auch die Leerzeilen zwischen seinen Befehlen - sie gehoeren dazu.
        const std::size_t e = gruppenLetzte(*sib, i);
        for (std::size_t q = i + 1; q <= e; ++q) {
            Path w = p;
            w.back() = q;
            out.push_back(w);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void Document::merkeBeitritt(const Path& weg, std::size_t anzahl, Kennung makro) {
    if (makro == 0 || weg.empty()) {
        return;
    }
    vergibKennungen();
    for (std::size_t k = 0; k < anzahl; ++k) {
        Path w = weg;
        w.back() += k;
        const Node* n = nodeAt(s_, w);
        if (n != nullptr && n->kind == Node::Kind::Command) {
            beitritt_.emplace_back(n->kennung, makro);
        }
    }
}

void Document::snapshot(const char* what) {
    ++aenderungen_;
    ++stand_;
    // --- Liegt der gespeicherte Stand im Wiederholen-Speicher? -------------
    //
    // Dann ist er gleich weg (redo_.clear unten), und KEINE Tiefe trifft ihn
    // mehr. dirty() verglich nur die Zahl der Schritte: speichern bei 5,
    // einmal zurueck, etwas ANDERES aendern - wieder 5, also "gesichert",
    // obwohl der Inhalt ein anderer war. Kein Stern, keine Nachfrage beim
    // Schliessen oder Beenden, und die Aenderung war verloren (Code-Pruefung
    // zu shanks Datenverlust, 03.10.).
    // Fuer verwirf(): den Verlauf so merken, wie er VOR diesem Schritt war.
    savedVorher_ = savedDepth_;
    abgeschnitten_ = false;
    if (savedDepth_ != kNieGesichert && savedDepth_ > undo_.size()) {
        savedDepth_ = kNieGesichert;
    }
    undo_.push_back(Step{s_, what});
    if (undo_.size() > kMaxUndo) {
        undo_.erase(undo_.begin());
        abgeschnitten_ = true;
        // Der aelteste Stand ist weg; der gespeicherte Punkt wandert mit,
        // sonst zeigt das Dokument dauerhaft "ungesichert". War der
        // gespeicherte Stand GENAU der weggefallene, ist er nicht mehr
        // erreichbar - dann gilt das Dokument als ungesichert, bis es wieder
        // gespeichert wird (vorher blieb savedDepth_ auf 0, und ganz unten
        // im Verlauf stand faelschlich "gesichert").
        if (savedDepth_ == 0) {
            savedDepth_ = kNieGesichert;
        } else if (savedDepth_ != kNieGesichert) {
            --savedDepth_;
        }
    }
    redoVorher_ = std::move(redo_);
    redo_.clear();
}

// --- Einen Schritt zuruecknehmen, der nichts bewirkt hat -----------------
//
// Viele Befehle machen erst die Momentaufnahme und merken dann, dass der Zug
// nicht geht (Ziel gleich Quelle, nichts verschiebbar). Bisher stand dort
// undo() + redo_.clear() - damit war der ganze Wiederholen-Speicher weg,
// obwohl am Skript nichts passiert war: Strg+Z, ein Zug auf dieselbe Stelle,
// und Strg+Y ging nicht mehr (Code-Pruefung 03.10.). Jetzt kommt der Verlauf
// genau so zurueck, wie er vor snapshot() war.
void Document::verwirf() {
    if (undo_.empty()) {
        return;
    }
    ++stand_;
    s_ = std::move(undo_.back().script);
    undo_.pop_back();
    redo_ = std::move(redoVorher_);
    redoVorher_.clear();
    if (!abgeschnitten_) {
        savedDepth_ = savedVorher_;
    } else if (savedVorher_ == 0 || savedVorher_ == kNieGesichert) {
        // Der aelteste Stand fiel beim Abschneiden weg - er bleibt weg.
        savedDepth_ = kNieGesichert;
    } else {
        savedDepth_ = savedVorher_ - 1;
    }
    abgeschnitten_ = false;
}

std::vector<Node>* Document::siblingsOf(const Path& p, std::size_t& idx) {
    return resolveSiblings(s_, p, idx);
}

Path Document::wegHinter(const Path& p) {
    if (p.empty()) {
        return Path{s_.nodes.size()};
    }
    Path w = istMakro(p) ? gruppenEnde(p) : p;
    ++w.back();
    return w;
}

bool Document::istMakro(const Path& p) const {
    const Node* n = nodeAt(s_, p);
    return n != nullptr && n->kind == Node::Kind::Macro;
}

bool Document::insertAfter(const Path& p, Node n) {
    MakroWache wache(*this);
    if (p.empty()) {
        // Ans Ende der obersten Ebene. Das ist der Weg fuer den ersten
        // Knoten ueberhaupt.
        snapshot("insert");
        s_.nodes.push_back(std::move(n));
        return true;
    }
    // Hinter eine MAKROZEILE heisst hinter das ganze Makro; hinter einen
    // seiner Befehle heisst hinein (im Original sind es Kinder). Ein Makro
    // kommt nie in ein Makro - die Datei kann das nicht ausdruecken.
    Path anker = p;
    Kennung makro = 0;
    if (const Node* a = nodeAt(s_, p); a != nullptr && a->kind == Node::Kind::Macro) {
        anker = gruppenEnde(p);
    } else if (const Path m = makroZu(p); !m.empty()) {
        if (n.kind == Node::Kind::Macro) {
            anker = gruppenEnde(m);
        } else {
            makro = kennungBei(m);
        }
    }
    std::size_t i = 0;
    std::vector<Node>* sib = siblingsOf(anker, i);
    if (sib == nullptr || i >= sib->size()) {
        return false;
    }
    snapshot("insert");
    sib->insert(sib->begin() + static_cast<std::ptrdiff_t>(i) + 1, std::move(n));
    Path neu = anker;
    neu.back() = i + 1;
    merkeBeitritt(neu, 1, makro);
    return true;
}

bool Document::insertBefore(const Path& p, Node n) {
    MakroWache wache(*this);
    // Ein Makro vor einen Befehl EINES Makros: vor dessen Makrozeile.
    if (n.kind == Node::Kind::Macro && !p.empty()) {
        if (const Path m = makroZu(p); !m.empty()) {
            return insertBefore(m, std::move(n));
        }
    }
    if (p.empty()) {
        // An den ANFANG der obersten Ebene - spiegelbildlich zu insertAfter,
        // das bei leerem Weg ans Ende haengt.
        snapshot("insert");
        s_.nodes.insert(s_.nodes.begin(), std::move(n));
        return true;
    }
    std::size_t i = 0;
    const std::vector<Node>* sib = siblingsOf(p, i);
    if (sib == nullptr || i >= sib->size()) {
        return false;
    }
    snapshot("insert");
    // Nach dem snapshot() neu aufloesen - dieselbe Vorsicht wie in
    // insertInto: der Weg ist die verlaesslichere Ansprache als ein Zeiger.
    std::size_t j = 0;
    std::vector<Node>* sib2 = siblingsOf(p, j);
    if (sib2 == nullptr || j > sib2->size()) {
        return false;
    }
    sib2->insert(sib2->begin() + static_cast<std::ptrdiff_t>(j), std::move(n));
    return true;
}

bool Document::insertInto(const Path& blockPath, std::size_t at, Node n) {
    MakroWache wache(*this);
    std::size_t i = 0;
    std::vector<Node>* sib = siblingsOf(blockPath, i);
    // In ein MAKRO: an die at-te Stelle seiner Befehle (0 = an den Anfang).
    if (sib != nullptr && i < sib->size() && (*sib)[i].kind == Node::Kind::Macro) {
        if (n.kind == Node::Kind::Macro) {
            return false;   // Makros nicht verschachteln
        }
        const std::vector<std::size_t> st = makroStellen(*sib, i);
        const std::size_t bei = (at == 0 || st.empty()) ? i + 1 : st[std::min(at, st.size()) - 1] + 1;
        const Kennung mk = (*sib)[i].kennung;
        snapshot("insert");
        std::size_t j = 0;
        std::vector<Node>* s2 = siblingsOf(blockPath, j);
        s2->insert(s2->begin() + static_cast<std::ptrdiff_t>(bei), std::move(n));
        Path neu = blockPath;
        neu.back() = bei;
        merkeBeitritt(neu, 1, mk);
        return true;
    }
    if (sib == nullptr || i >= sib->size() || !(*sib)[i].hasBlock) {
        return false;
    }
    std::vector<Node>& kids = (*sib)[i].children;
    if (at > kids.size()) {
        return false;
    }
    snapshot("insert");
    // Nach dem snapshot() neu aufloesen: die Kopie hat die Zeiger nicht
    // ungueltig gemacht, aber der Weg ist die verlaesslichere Ansprache.
    std::size_t j = 0;
    std::vector<Node>* sib2 = siblingsOf(blockPath, j);
    (*sib2)[j].children.insert(
        (*sib2)[j].children.begin() + static_cast<std::ptrdiff_t>(at), std::move(n));
    return true;
}

bool Document::replaceAll(Script s, const char* was) {
    snapshot(was);
    s_ = std::move(s);
    return true;
}

bool Document::removeAt(const Path& p) {
    MakroWache wache(*this);
    std::size_t i = 0;
    std::vector<Node>* sib = siblingsOf(p, i);
    if (sib == nullptr || i >= sib->size()) {
        return false;
    }
    // Eine Makrozeile nimmt ihre Befehle mit (im Original: Knoten samt
    // Kindern).
    if ((*sib)[i].kind == Node::Kind::Macro) {
        return removeAll({p});
    }
    snapshot("delete");
    std::size_t j = 0;
    std::vector<Node>* s2 = siblingsOf(p, j);
    s2->erase(s2->begin() + static_cast<std::ptrdiff_t>(j));
    return true;
}

bool Document::cloneAt(const Path& p) {
    // Dieselbe Regel wie fuer mehrere: ein Makro als Ganzes, ein Befehl in
    // einem Makro bleibt mit seiner Kopie darin.
    return cloneAll({p});
}

namespace {

// Einen Weg als "0/2/1" schreiben - so sieht man die Ebene sofort.
std::string wegText(const Path& w) {
    std::string t;
    for (std::size_t x : w) {
        if (!t.empty()) {
            t += "/";
        }
        t += std::to_string(x);
    }
    return t.empty() ? std::string("(oben)") : t;
}

}  // namespace

bool Document::moveUp(const Path& p, Path* nachher) {
    MakroWache wache(*this);
    abweisung_ = Abweisung::Keine;
    std::size_t i = 0;
    std::vector<Node>* sib = siblingsOf(p, i);
    if (sib == nullptr || i >= sib->size()) {
        abweisung_ = Abweisung::WegUngueltig;
        return false;
    }
    // --- Makros: als Ganzes, und am Rand heraus -------------------------
    //
    // Ein Makro wandert samt seinen Befehlen; ueber ein Makro darueber
    // springt man als Ganzes (wie ueber einen Block); der ERSTE Befehl eines
    // Makros tritt nach oben aus, vor die Makrozeile.
    if ((*sib)[i].kind == Node::Kind::Macro && i > 0) {
        const std::size_t e = gruppenLetzte(*sib, i);
        std::size_t vorn = i - 1;
        if (const std::size_t mv = makroVon(*sib, vorn); mv != kKeins) {
            vorn = mv;
        }
        snapshot("moveup");
        std::size_t j = 0;
        std::vector<Node>* s2 = siblingsOf(p, j);
        std::rotate(s2->begin() + static_cast<std::ptrdiff_t>(vorn),
                    s2->begin() + static_cast<std::ptrdiff_t>(i),
                    s2->begin() + static_cast<std::ptrdiff_t>(e) + 1);
        diag::detail("Baum: Makro \"" + (*s2)[vorn].name + "\" samt Befehlen nach OBEN");
        if (nachher != nullptr) { *nachher = p; nachher->back() = vorn; }
        return true;
    }
    if ((*sib)[i].kind == Node::Kind::Macro && i == 0 && p.size() >= 2) {
        const std::size_t e = gruppenLetzte(*sib, i);
        snapshot("moveup-heraus");
        std::size_t j = 0;
        std::vector<Node>* s2 = siblingsOf(p, j);
        const std::vector<Node> gruppe(s2->begin(), s2->begin() + static_cast<std::ptrdiff_t>(e) + 1);
        s2->erase(s2->begin(), s2->begin() + static_cast<std::ptrdiff_t>(e) + 1);
        Path eltern(p.begin(), p.end() - 1);
        std::size_t ei = 0;
        std::vector<Node>* esib = siblingsOf(eltern, ei);
        if (esib == nullptr) { return false; }
        esib->insert(esib->begin() + static_cast<std::ptrdiff_t>(ei), gruppe.begin(), gruppe.end());
        if (nachher != nullptr) { *nachher = eltern; }
        return true;
    }
    if (const std::size_t m = makroVon(*sib, i); m != kKeins) {
        const std::vector<std::size_t> st = makroStellen(*sib, m);
        if (!st.empty() && st.front() == i) {
            snapshot("moveup");
            std::size_t j = 0;
            std::vector<Node>* s2 = siblingsOf(p, j);
            std::rotate(s2->begin() + static_cast<std::ptrdiff_t>(m),
                        s2->begin() + static_cast<std::ptrdiff_t>(i),
                        s2->begin() + static_cast<std::ptrdiff_t>(i) + 1);
            diag::detail("Baum: \"" + (*s2)[m].name + "\" aus dem Makro heraus nach OBEN");
            if (nachher != nullptr) { *nachher = p; nachher->back() = m; }
            return true;
        }
    } else if (i > 0) {
        std::size_t start = makroVon(*sib, i - 1);
        if (start == kKeins && (*sib)[i - 1].kind == Node::Kind::Macro) {
            start = i - 1;
        }
        if (start != kKeins) {
            snapshot("moveup");
            std::size_t j = 0;
            std::vector<Node>* s2 = siblingsOf(p, j);
            std::rotate(s2->begin() + static_cast<std::ptrdiff_t>(start),
                        s2->begin() + static_cast<std::ptrdiff_t>(i),
                        s2->begin() + static_cast<std::ptrdiff_t>(i) + 1);
            diag::detail("Baum: \"" + (*s2)[start].name + "\" ueber ein Makro nach OBEN");
            if (nachher != nullptr) { *nachher = p; nachher->back() = start; }
            return true;
        }
    }
    // --- Aus dem Block HERAUS, statt am Anfang stehenzubleiben ----------
    //
    // shank: "bei den affect-, task-, if-, else-Bloecken kann ich die
    // Befehle nicht aus dem Block herausbewegen. Dragging oder Move
    // up/down nach oben/unten sollte es herausbewegen."
    //
    // Bisher gab `i == 0` einfach `false` zurueck - der erste Befehl in
    // einem Block sass fest. Man musste ihn ausschneiden und woanders
    // wieder einfuegen.
    //
    // Richtig ist: der Befehl wird zum VORGAENGER seines Blocks. Das ist
    // dieselbe Bewegung, die er sonst macht - nur eine Ebene hoeher, und
    // genau das erwartet man von "nach oben".
    if (i == 0) {
        if (p.size() < 2) {
            abweisung_ = Abweisung::SchonGanzOben;
            return false;   // schon ganz oben auf oberster Ebene
        }
        snapshot("moveup-heraus");
        Node mit = (*sib)[0];
        // Ins Protokoll: shank will jede Bewegung nachlesen koennen, nicht
        // nur die Anzeige. Ein Block-Austritt ist die Art Aenderung, bei
        // der man hinterher wissen will, was genau passiert ist.
        {
            char z[200];
            std::snprintf(z, sizeof(z),
                          "Baum: \"%s\" aus Block heraus nach OBEN "
                          "(Tiefe %zu -> %zu)",
                          mit.name.c_str(), p.size(), p.size() - 1);
            diag::detail(z);
        }
        sib->erase(sib->begin());
        // Der Elternpfad ist der Pfad ohne sein letztes Glied.
        Path eltern(p.begin(), p.end() - 1);
        std::size_t ei = 0;
        std::vector<Node>* esib = siblingsOf(eltern, ei);
        if (esib == nullptr) {
            return false;
        }
        esib->insert(esib->begin() + static_cast<std::ptrdiff_t>(ei),
                     std::move(mit));
        if (nachher != nullptr) {
            *nachher = eltern;   // er steht jetzt AN der Stelle des Blocks
        }
        return true;
    }
    snapshot("moveup");
    std::size_t j = 0;
    std::vector<Node>* s2 = siblingsOf(p, j);
    std::swap((*s2)[j - 1], (*s2)[j]);
    // Auch den gewoehnlichen Tausch protokollieren.
    //
    // Vorher stand nur der Block-AUSTRITT im Protokoll. shanks Datei hatte
    // 24 Bearbeitungsschritte und keine einzige Zeile - das hiess nur, dass
    // er nie am Blockrand war, und beantwortete die Frage "geht es?" gar
    // nicht.
    //
    // Ein Protokoll, das nur den Sonderfall kennt, kann den Normalfall
    // nicht bestaetigen.
    Path zielW = p;
    zielW.back() = j - 1;
    diag::detail("Baum: \"" + (*s2)[j - 1].name + "\" nach OBEN, " +
                 wegText(p) + " -> " + wegText(zielW));
    if (nachher != nullptr) {
        *nachher = p;
        nachher->back() = j - 1;
    }
    return true;
}

bool Document::moveDown(const Path& p, Path* nachher) {
    MakroWache wache(*this);
    abweisung_ = Abweisung::Keine;
    std::size_t i = 0;
    std::vector<Node>* sib = siblingsOf(p, i);
    if (sib == nullptr || i >= sib->size()) {
        abweisung_ = Abweisung::WegUngueltig;
        return false;
    }
    // Spiegelbild der Makro-Regeln in moveUp; der LETZTE Befehl eines Makros
    // tritt nach unten aus - er steht schon hinter den anderen, er gehoert
    // nur nicht mehr dazu.
    if ((*sib)[i].kind == Node::Kind::Macro) {
        const std::size_t e = gruppenLetzte(*sib, i);
        if (e + 1 < sib->size()) {
            const std::size_t n0 = e + 1;
            const std::size_t n1 = ((*sib)[n0].kind == Node::Kind::Macro) ? gruppenLetzte(*sib, n0) : n0;
            snapshot("movedown");
            std::size_t j = 0;
            std::vector<Node>* s2 = siblingsOf(p, j);
            std::rotate(s2->begin() + static_cast<std::ptrdiff_t>(i),
                        s2->begin() + static_cast<std::ptrdiff_t>(n0),
                        s2->begin() + static_cast<std::ptrdiff_t>(n1) + 1);
            const std::size_t neu = i + (n1 - n0 + 1);
            diag::detail("Baum: Makro \"" + (*s2)[neu].name + "\" samt Befehlen nach UNTEN");
            if (nachher != nullptr) { *nachher = p; nachher->back() = neu; }
            return true;
        }
        if (p.size() < 2) {
            abweisung_ = Abweisung::SchonAmEnde;
            return false;
        }
        snapshot("movedown-heraus");
        std::size_t j = 0;
        std::vector<Node>* s2 = siblingsOf(p, j);
        const std::vector<Node> gruppe(s2->begin() + static_cast<std::ptrdiff_t>(i),
                                       s2->begin() + static_cast<std::ptrdiff_t>(e) + 1);
        s2->erase(s2->begin() + static_cast<std::ptrdiff_t>(i), s2->begin() + static_cast<std::ptrdiff_t>(e) + 1);
        Path eltern(p.begin(), p.end() - 1);
        std::size_t ei = 0;
        std::vector<Node>* esib = siblingsOf(eltern, ei);
        if (esib == nullptr) { return false; }
        esib->insert(esib->begin() + static_cast<std::ptrdiff_t>(ei) + 1, gruppe.begin(), gruppe.end());
        if (nachher != nullptr) { *nachher = eltern; nachher->back() = ei + 1; }
        return true;
    }
    if (const std::size_t m = makroVon(*sib, i); m != kKeins) {
        const std::vector<std::size_t> st = makroStellen(*sib, m);
        if (!st.empty() && st.back() == i) {
            snapshot("movedown");
            austritt_.push_back((*sib)[i].kennung);
            diag::detail("Baum: \"" + (*sib)[i].name + "\" aus dem Makro heraus nach UNTEN");
            if (nachher != nullptr) { *nachher = p; }
            return true;
        }
    } else if (i + 1 < sib->size() && (*sib)[i + 1].kind == Node::Kind::Macro) {
        const std::size_t e = gruppenLetzte(*sib, i + 1);
        snapshot("movedown");
        std::size_t j = 0;
        std::vector<Node>* s2 = siblingsOf(p, j);
        std::rotate(s2->begin() + static_cast<std::ptrdiff_t>(i),
                    s2->begin() + static_cast<std::ptrdiff_t>(i) + 1,
                    s2->begin() + static_cast<std::ptrdiff_t>(e) + 1);
        diag::detail("Baum: \"" + (*s2)[e].name + "\" ueber ein Makro nach UNTEN");
        if (nachher != nullptr) { *nachher = p; nachher->back() = e; }
        return true;
    }
    // Spiegelbild zu `moveUp`: der letzte Befehl im Block wird zum
    // NACHFOLGER seines Blocks.
    if (i + 1 >= sib->size()) {
        if (p.size() < 2) {
            abweisung_ = Abweisung::SchonAmEnde;
            return false;   // schon ganz unten auf oberster Ebene
        }
        snapshot("movedown-heraus");
        Node mit = (*sib)[i];
        {
            char z[200];
            std::snprintf(z, sizeof(z),
                          "Baum: \"%s\" aus Block heraus nach UNTEN "
                          "(Tiefe %zu -> %zu)",
                          mit.name.c_str(), p.size(), p.size() - 1);
            diag::detail(z);
        }
        sib->erase(sib->begin() + static_cast<std::ptrdiff_t>(i));
        Path eltern(p.begin(), p.end() - 1);
        std::size_t ei = 0;
        std::vector<Node>* esib = siblingsOf(eltern, ei);
        if (esib == nullptr) {
            return false;
        }
        esib->insert(esib->begin() + static_cast<std::ptrdiff_t>(ei) + 1,
                     std::move(mit));
        if (nachher != nullptr) {
            *nachher = eltern;   // direkt HINTER dem Block
            nachher->back() = ei + 1;
        }
        return true;
    }
    snapshot("movedown");
    std::size_t j = 0;
    std::vector<Node>* s2 = siblingsOf(p, j);
    std::swap((*s2)[j], (*s2)[j + 1]);
    Path zielW = p;
    zielW.back() = j + 1;
    diag::detail("Baum: \"" + (*s2)[j + 1].name + "\" nach UNTEN, " +
                 wegText(p) + " -> " + wegText(zielW));
    if (nachher != nullptr) {
        *nachher = p;
        nachher->back() = j + 1;
    }
    return true;
}

// <Tabs><Rest>  ->  <Tabs>//(BHVDREM)  <Rest>
static std::string remLine(const std::string& line) {
    std::size_t t = 0;
    while (t < line.size() && line[t] == '\t') {
        ++t;
    }
    return line.substr(0, t) + "//(BHVDREM)  " + line.substr(t);
}

static bool stripRem(const std::string& raw, std::string* rest) {
    std::size_t t = 0;
    while (t < raw.size() && raw[t] == '\t') {
        ++t;
    }
    if (raw.compare(t, 11, "//(BHVDREM)") != 0) {
        return false;
    }
    std::size_t r = t + 11;
    while (r < raw.size() && raw[r] == ' ') {
        ++r;
    }
    *rest = raw.substr(0, t) + raw.substr(r);
    return true;
}

bool Document::commentOut(const Path& p) {
    std::size_t i = 0;
    std::vector<Node>* sib = siblingsOf(p, i);
    // Auch Makromarker duerfen auskommentiert werden - Ravens
    // Hoth2/atst_attack.icarus enthaelt "//(BHVDREM)  //$\"default\"@4".
    if (sib == nullptr || i >= sib->size() ||
        ((*sib)[i].kind != Node::Kind::Command &&
         (*sib)[i].kind != Node::Kind::Macro)) {
        return false;
    }
    const int depth = static_cast<int>(p.size()) - 1;
    const std::string text = writeNodes({(*sib)[i]}, depth);

    std::vector<Node> lines;
    std::string cur;
    for (const char c : text) {
        if (c == '\n') {
            // Leerzeilen innerhalb des Blocks bleiben leer - BehavEd setzt
            // kein //(BHVDREM) davor. Nachgesehen in areis/intro1.icarus.
            Node ln;
            if (cur.empty()) {
                ln.kind = Node::Kind::Blank;
            } else {
                ln.kind = Node::Kind::LineComment;
                ln.raw = remLine(cur);
            }
            lines.push_back(std::move(ln));
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    if (lines.empty()) {
        return false;
    }
    snapshot("comment");
    std::size_t j = 0;
    std::vector<Node>* s2 = siblingsOf(p, j);
    s2->erase(s2->begin() + static_cast<std::ptrdiff_t>(j));
    s2->insert(s2->begin() + static_cast<std::ptrdiff_t>(j), lines.begin(), lines.end());
    return true;
}

// Un-REM: die gewaehlte Anweisung samt IHREM Block - nicht mehr.
//
// Vorher lief es von der gewaehlten Zeile rueckwaerts und vorwaerts ueber
// ALLE angrenzenden REM-Zeilen. Zwei getrennt auskommentierte Befehle
// hintereinander kamen damit gemeinsam zurueck. Im Original ist REM ein
// Merkmal JE KNOTEN (Tooltip: "Toggle REMark status of current line"), ein
// Nachbar bleibt, wie er ist.
//
// Die Grenzen stehen im Text: eine Anweisung ist eine Zeile; folgt ihr ein
// "{", reicht sie bis zur passenden "}". Leerzeilen innerhalb eines Blocks
// tragen kein //(BHVDREM) (BehavEd setzt keins davor) und gehoeren dazu.
// Steht die gewaehlte Zeile IN einem auskommentierten Block, kommt der
// ganze Block zurueck, zu dem sie gehoert.
bool Document::uncomment(const Path& p, std::size_t* produced, Path* anfang) {
    std::size_t i = 0;
    std::vector<Node>* sib = siblingsOf(p, i);
    if (sib == nullptr || i >= sib->size() ||
        (*sib)[i].kind != Node::Kind::LineComment) {
        return false;
    }
    std::string rest;
    auto isRemAt = [&](std::size_t k) {
        return (*sib)[k].kind == Node::Kind::LineComment && stripRem((*sib)[k].raw, &rest);
    };
    auto isBlankAt = [&](std::size_t k) { return (*sib)[k].kind == Node::Kind::Blank; };
    auto inhalt = [&](std::size_t k) {
        std::string t;
        (void)remInhalt((*sib)[k].raw, &t);
        return t;
    };
    if (!isRemAt(i)) {
        return false;
    }
    // Anfang der zusammenhaengenden REM-Folge.
    std::size_t lauf = i;
    while (lauf > 0) {
        std::size_t k = lauf - 1;
        while (k > 0 && isBlankAt(k)) {
            --k;
        }
        if (!isRemAt(k)) {
            break;
        }
        lauf = k;
    }
    // Die Folge in Anweisungen zerlegen und die finden, die `i` enthaelt.
    std::size_t von = 0;
    std::size_t bis = 0;
    bool gefunden = false;
    const std::size_t n = sib->size();
    for (std::size_t k = lauf; k < n && !gefunden;) {
        if (isBlankAt(k)) { ++k; continue; }
        if (!isRemAt(k)) { break; }
        const std::size_t start = k;
        std::size_t ende = k;
        int tiefe = 0;
        std::size_t q = k + 1;
        if (inhalt(k) == "{") {
            tiefe = 1;                       // Block ohne Kopf - bis zur "}"
        } else {
            while (q < n && isBlankAt(q)) { ++q; }
            if (q < n && isRemAt(q) && inhalt(q) == "{") {
                tiefe = 1;
                ende = q;
                ++q;
            }
        }
        for (; tiefe > 0 && q < n; ++q) {
            if (isBlankAt(q)) { continue; }
            if (!isRemAt(q)) { break; }
            const std::string t = inhalt(q);
            if (t == "{") { ++tiefe; }
            if (t == "}") { --tiefe; }
            ende = q;
        }
        if (i >= start && i <= ende) {
            von = start;
            bis = ende;
            gefunden = true;
        }
        k = ende + 1;
    }
    if (!gefunden) {
        return false;
    }
    std::string body;
    for (std::size_t k = von; k <= bis; ++k) {
        if (isRemAt(k)) {
            body += rest;
        }
        body += "\r\n";
    }
    const std::size_t end = bis + 1;
    Script tmp;
    std::vector<Diag> d;
    if (!readScript("//Generated by BehavEd\r\n" + body, tmp, d) || tmp.nodes.empty()) {
        return false;
    }
    snapshot("uncomment");
    std::size_t j = von;
    Path start = p;
    start.back() = von;
    std::vector<Node>* s2 = siblingsOf(start, j);
    s2->erase(s2->begin() + static_cast<std::ptrdiff_t>(j),
              s2->begin() + static_cast<std::ptrdiff_t>(end));
    s2->insert(s2->begin() + static_cast<std::ptrdiff_t>(j),
               tmp.nodes.begin(), tmp.nodes.end());
    if (produced != nullptr) {
        *produced = tmp.nodes.size();
    }
    if (anfang != nullptr) {
        *anfang = start;
    }
    return true;
}

bool Document::insertRem(const Path& p) {
    Node rem;
    rem.kind = Node::Kind::Command;
    rem.name = "rem";
    Arg a;
    a.kind = Arg::Kind::String;
    a.text = "comment";
    rem.args.push_back(std::move(a));
    return insertAfter(p, std::move(rem));
}

bool Document::ersetzeSkript(Script neu, const char* was) {
    snapshot(was);
    s_ = std::move(neu);
    return true;
}

bool Document::replaceMany(const std::vector<std::pair<Path, Node>>& ersatz) {
    MakroWache wache(*this);
    bool gueltig = false;
    for (const auto& [p, n] : ersatz) {
        std::size_t idx = 0;
        const std::vector<Node>* sib = siblingsOf(p, idx);
        gueltig = gueltig || (sib != nullptr && idx < sib->size());
    }
    if (!gueltig) {
        return false;
    }
    snapshot("revert");
    for (const auto& [p, n] : ersatz) {
        std::size_t idx = 0;
        std::vector<Node>* sib = siblingsOf(p, idx);
        if (sib == nullptr || idx >= sib->size()) {
            continue;
        }
        Node neu = n;
        // Wie replaceAt: Blockinhalt und Kennung bleiben.
        neu.children = (*sib)[idx].children;
        neu.hasBlock = (*sib)[idx].hasBlock;
        neu.kennung = (*sib)[idx].kennung;
        (*sib)[idx] = std::move(neu);
    }
    return true;
}

bool Document::replaceAt(const Path& p, Node n) {
    std::size_t idx = 0;
    std::vector<Node>* sib = siblingsOf(p, idx);
    if (sib == nullptr || idx >= sib->size()) {
        return false;
    }
    // Kinder des alten Knotens uebernehmen: der Editor aendert nur die
    // Felder, nicht den Blockinhalt.
    n.children = (*sib)[idx].children;
    n.hasBlock = (*sib)[idx].hasBlock;
    // Und die KENNUNG. Es ist derselbe Knoten, nur mit anderen Feldern.
    //
    // Ohne das kam er mit Kennung 0 herein, `vergibKennungen()` gab ihm
    // eine neue, und der gemerkte Aufklapp-Zustand zeigte auf eine
    // Kennung, die es nicht mehr gab: der Block klappte beim Druck auf
    // "Ok" zu.
    //
    // shank zu rc540, mit zwei Bildern: ein `if` mit einem `declare`
    // darin, aufgeklappt - nach "Ok" zugeklappt.
    //
    // Vor rc535 fiel das nicht auf, weil der Zustand am WEG hing und
    // replaceAt den Weg nicht aendert. Der Umbau auf Kennungen hat diese
    // Stelle uebersehen - sie ist die einzige, die einen Knoten ersetzt
    // statt ihn zu bewegen.
    n.kennung = (*sib)[idx].kennung;
    snapshot("edit");
    (*sib)[idx] = std::move(n);
    return true;
}

bool Document::copyAt(const Path& p) {
    const Node* n = nodeAt(s_, p);
    if (n == nullptr) {
        return false;
    }
    if (n->kind == Node::Kind::Macro) {
        return copyAll({p});   // samt seinen Befehlen
    }
    clip_.assign(1, *n);   // mitsamt Block, falls vorhanden
    return true;
}

bool Document::cutAt(const Path& p) {
    MakroWache wache(*this);
    return copyAt(p) && removeAt(p);
}

bool Document::insertAfterAll(const Path& p, const std::vector<Node>& nodes,
                              Path* nachher) {
    MakroWache wache(*this);
    if (nodes.empty()) {
        return false;
    }
    if (p.empty()) {
        snapshot("insert");        // EINMAL, nicht je Knoten
        s_.nodes.insert(s_.nodes.end(), nodes.begin(), nodes.end());
        if (nachher != nullptr) {
            *nachher = Path{s_.nodes.size() - 1};
        }
        return true;
    }
    // Wie insertAfter: hinter eine Makrozeile = hinter das Makro, hinter
    // einen seiner Befehle = hinein - ausser es kommt selbst ein Makro mit.
    const bool mitMakro = std::any_of(nodes.begin(), nodes.end(),
                                      [](const Node& n) { return n.kind == Node::Kind::Macro; });
    Path anker = p;
    Kennung makro = 0;
    if (const Node* a = nodeAt(s_, p); a != nullptr && a->kind == Node::Kind::Macro) {
        anker = gruppenEnde(p);
    } else if (const Path m = makroZu(p); !m.empty()) {
        if (mitMakro) {
            anker = gruppenEnde(m);
        } else {
            makro = kennungBei(m);
        }
    }
    std::size_t i = 0;
    std::vector<Node>* sib = siblingsOf(anker, i);
    if (sib == nullptr || i >= sib->size()) {
        return false;
    }
    snapshot("insert");            // EINMAL, nicht je Knoten
    sib->insert(sib->begin() + static_cast<std::ptrdiff_t>(i) + 1, nodes.begin(), nodes.end());
    Path neu = anker;
    neu.back() = i + 1;
    merkeBeitritt(neu, nodes.size(), makro);
    if (nachher != nullptr) {
        *nachher = anker;
        nachher->back() = i + nodes.size();
    }
    return true;
}

namespace {

// Kennungen eines Teilbaums loeschen; vergibKennungen gibt dann frische.
void ohneKennung(std::vector<Node>& ns) {
    for (Node& n : ns) {
        n.kennung = 0;
        ohneKennung(n.children);
    }
}

}  // namespace

// Was eingefuegt wird, ist eine KOPIE und bekommt eigene Kennungen. Mit den
// alten bekam beim Einfuegen VOR dem Original die Kopie dessen Kennung (wer
// zuerst kommt, behaelt sie) und das Original eine neue - Auswahl, Markierung
// am Rand und gezieltes Rueckgaengig hingen dann an der Kopie (Code-Pruefung
// 03.10.).
std::vector<Node> Document::clipKopie() const {
    std::vector<Node> k = clip_;
    ohneKennung(k);
    return k;
}

bool Document::pasteAfter(const Path& p) {
    MakroWache wache(*this);
    if (clip_.empty()) {
        return false;
    }
    if (p.empty()) {
        snapshot("paste");
        const std::vector<Node> k = clipKopie();
        s_.nodes.insert(s_.nodes.end(), k.begin(), k.end());
        return true;
    }
    // Wie insertAfter (Makrozeile: dahinter; Befehl darin: hinein).
    const bool mitMakro = std::any_of(clip_.begin(), clip_.end(),
                                      [](const Node& n) { return n.kind == Node::Kind::Macro; });
    Path anker = p;
    Kennung makro = 0;
    if (const Node* a = nodeAt(s_, p); a != nullptr && a->kind == Node::Kind::Macro) {
        anker = gruppenEnde(p);
    } else if (const Path m = makroZu(p); !m.empty()) {
        if (mitMakro) {
            anker = gruppenEnde(m);
        } else {
            makro = kennungBei(m);
        }
    }
    std::size_t i = 0;
    std::vector<Node>* sib = siblingsOf(anker, i);
    if (sib == nullptr || i >= sib->size()) {
        return false;
    }
    snapshot("paste");
    std::size_t j = 0;
    std::vector<Node>* s2 = siblingsOf(anker, j);
    const std::vector<Node> k = clipKopie();
    s2->insert(s2->begin() + static_cast<std::ptrdiff_t>(j) + 1, k.begin(), k.end());
    Path neu = anker;
    neu.back() = j + 1;
    merkeBeitritt(neu, clip_.size(), makro);
    return true;
}

bool Document::pasteInto(const Path& block) {
    MakroWache wache(*this);
    if (clip_.empty() || block.empty()) {
        return false;
    }
    std::size_t i = 0;
    std::vector<Node>* sib = siblingsOf(block, i);
    if (sib == nullptr || i >= sib->size()) {
        return false;
    }
    // An den ANFANG - wie das Original beim Hinzufuegen in einen offenen
    // Block oder ein offenes Makro (am laufenden BehavEd nachgesehen).
    if ((*sib)[i].kind == Node::Kind::Macro) {
        if (std::any_of(clip_.begin(), clip_.end(),
                        [](const Node& n) { return n.kind == Node::Kind::Macro; })) {
            return pasteAfter(block);   // ein Makro nicht ins Makro: dahinter
        }
        const Kennung mk = (*sib)[i].kennung;
        snapshot("paste");
        std::size_t j = 0;
        std::vector<Node>* s2 = siblingsOf(block, j);
        const std::vector<Node> k = clipKopie();
        s2->insert(s2->begin() + static_cast<std::ptrdiff_t>(j) + 1, k.begin(), k.end());
        Path neu = block;
        neu.back() = j + 1;
        merkeBeitritt(neu, clip_.size(), mk);
        return true;
    }
    if (!(*sib)[i].hasBlock) {
        return false;
    }
    snapshot("paste");
    std::size_t j = 0;
    std::vector<Node>* s2 = siblingsOf(block, j);
    std::vector<Node>& kids = (*s2)[j].children;
    const std::vector<Node> k = clipKopie();
    kids.insert(kids.begin(), k.begin(), k.end());
    return true;
}

bool Document::undo() {
    if (undo_.empty()) {
        return false;
    }
    redo_.push_back(Step{s_, undo_.back().what});
    ++stand_;
    s_ = undo_.back().script;
    undo_.pop_back();
    return true;
}

bool Document::redo() {
    if (redo_.empty()) {
        return false;
    }
    undo_.push_back(Step{s_, redo_.back().what});
    ++stand_;
    s_ = redo_.back().script;
    redo_.pop_back();
    return true;
}

namespace {

// Wege absteigend sortieren: erst nach Tiefe, dann nach Stelle. So trifft
// jede Loeschung noch den Knoten, den der Weg meinte.
void sortDescending(std::vector<Path>& paths) {
    std::sort(paths.begin(), paths.end(), [](const Path& a, const Path& b) {
        const std::size_t n = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < n; ++i) {
            if (a[i] != b[i]) {
                return a[i] > b[i];
            }
        }
        return a.size() > b.size();
    });
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
}

}  // namespace

// Mehrfachauswahl verlaesst KEINEN Block - Absicht, kein Versehen.
//
// Einzeln wandert ein Befehl seit rc516 aus dem Block heraus. Bei mehreren
// waere die Bewegung mehrdeutig: gehen alle heraus, oder nur der oberste,
// und was passiert mit denen dazwischen? Diese Funktion verlangt darum
// gleiche Ebene und gleichen Elternknoten und tut am Blockrand nichts.
//
// Lieber nichts tun als etwas raten, das man hinterher von Hand richten
// muss. Wenn shank es anders braucht, ist das eine eigene Runde mit einer
// eigenen Frage: was soll dann genau passieren?
bool Document::moveAll(std::vector<Path>& paths, bool up) {
    MakroWache wache(*this);
    if (paths.empty()) {
        return false;
    }
    // Eine gewaehlte Makrozeile nimmt ihre Befehle mit.
    paths = mitGruppen(std::move(paths));
    // Nur Geschwister derselben Ebene lassen sich sinnvoll gemeinsam
    // schieben. Eine gemischte Auswahl waere nicht eindeutig: wohin soll ein
    // Knoten aus einem Block relativ zu einem daneben?
    const std::size_t depth = paths.front().size();
    for (const Path& p : paths) {
        if (p.size() != depth) {
            return false;
        }
        if (depth > 1 && !std::equal(p.begin(), p.end() - 1,
                                     paths.front().begin())) {
            return false;
        }
    }

    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    // Am Rand angekommen? Dann nichts tun, statt die Auswahl zu zerlegen.
    std::size_t idx = 0;
    std::vector<Node>* sib = siblingsOf(paths.front(), idx);
    if (sib == nullptr) {
        return false;
    }
    if (up && paths.front().back() == 0) {
        return false;
    }
    if (!up && paths.back().back() + 1 >= sib->size()) {
        return false;
    }

    snapshot(up ? "moveup" : "movedown");
    bool any = false;
    if (up) {
        // Von VORN: der erste macht Platz, in den der zweite nachruecken
        // kann. Andersherum ueberholen sie sich.
        for (Path& p : paths) {
            std::size_t i = 0;
            std::vector<Node>* s2 = siblingsOf(p, i);
            if (s2 == nullptr || i == 0 || i >= s2->size()) {
                continue;
            }
            std::swap((*s2)[i], (*s2)[i - 1]);
            --p.back();
            any = true;
        }
    } else {
        for (std::size_t k = paths.size(); k > 0; --k) {
            Path& p = paths[k - 1];
            std::size_t i = 0;
            std::vector<Node>* s2 = siblingsOf(p, i);
            if (s2 == nullptr || i + 1 >= s2->size()) {
                continue;
            }
            std::swap((*s2)[i], (*s2)[i + 1]);
            ++p.back();
            any = true;
        }
    }
    if (!any) {
        verwirf();
    }
    return any;
}

bool Document::moveInto(const Path& from, const Path& block, Path* nachher) {
    MakroWache wache(*this);
    abweisung_ = Abweisung::Keine;
    if (const Node* ziel = nodeAt(s_, block); ziel != nullptr && ziel->kind == Node::Kind::Macro) {
        return moveAllAt({from}, block, Stelle::Hinein, nachher);
    }
    if (from.empty() || block.empty() || from == block) {
        abweisung_ = (from == block) ? Abweisung::ZielGleichHerkunft
                                     : Abweisung::WegUngueltig;
        return false;
    }
    // Ein Block darf nicht in sich selbst - weder direkt noch in ein Kind.
    if (block.size() >= from.size() &&
        std::equal(from.begin(), from.end(), block.begin())) {
        abweisung_ = Abweisung::ZielImGezogenen;
        return false;
    }
    const Node* target = nodeAt(s_, block);
    if (target == nullptr || !target->hasBlock) {
        abweisung_ = Abweisung::ZielIstKeinBlock;
        return false;
    }
    const Node* src = nodeAt(s_, from);
    if (src == nullptr) {
        abweisung_ = Abweisung::WegUngueltig;
        return false;
    }
    const Node copy = *src;

    snapshot("moveto");
    {
        std::size_t i = 0;
        std::vector<Node>* sib = siblingsOf(from, i);
        if (sib == nullptr || i >= sib->size()) {
            verwirf();
            return false;
        }
        sib->erase(sib->begin() + static_cast<std::ptrdiff_t>(i));
    }
    // Der Weg des Blocks verschiebt sich, wenn die Quelle davor UND auf
    // derselben Ebene lag.
    Path dest = block;
    if (from.size() <= dest.size() &&
        std::equal(from.begin(), from.end() - 1, dest.begin()) &&
        from.back() < dest[from.size() - 1]) {
        --dest[from.size() - 1];
    }
    // Ueber die Geschwister an den Knoten kommen: nodeAt liefert einen
    // konstanten Zeiger, hier muss aber eingefuegt werden.
    std::size_t bi = 0;
    std::vector<Node>* holder = siblingsOf(dest, bi);
    Node* blockNode = (holder != nullptr && bi < holder->size())
                          ? &(*holder)[bi]
                          : nullptr;
    if (blockNode == nullptr) {
        verwirf();
        return false;
    }
    blockNode->children.insert(blockNode->children.begin(), copy);
    // `moveInto` hat bisher NICHTS protokolliert.
    //
    // shanks Protokoll zeigte 17 Zuege, alle "Tiefe 1 -> 1". Das sah aus,
    // als bewege sich nie etwas in einen Block hinein - dabei hinterliess
    // ein solcher Zug schlicht keine Spur. Man konnte "ist reingegangen"
    // nicht von "nichts passiert" unterscheiden.
    //
    // Ein Protokoll mit Loch ist schlimmer als keines: es sieht vollstaendig
    // aus und ist es nicht.
    // Fuer das Protokoll UND fuer den Aufrufer: `dest`, nicht `block`.
    // Lag die Quelle vor dem Block auf derselben Ebene, ist der Block beim
    // Entnehmen um eins vorgerueckt - `block` zeigt dann daneben.
    Path gelandet = dest;
    gelandet.push_back(0);
    if (nachher != nullptr) {
        *nachher = gelandet;
    }
    diag::detail("Baum: \"" + copy.name + "\" HINEIN in \"" +
                 blockNode->name + "\", " + wegText(from) + " -> " +
                 wegText(gelandet) + " (Tiefe " +
                 std::to_string(from.size()) + " -> " +
                 std::to_string(gelandet.size()) + ")");
    return true;
}

bool Document::moveAllAt(std::vector<Path> paths, const Path& ankerWunsch, Stelle woWunsch,
                         Path* ersterNachher) {
    MakroWache wache(*this);
    abweisung_ = Abweisung::Keine;
    if (paths.empty() || ankerWunsch.empty()) {
        abweisung_ = Abweisung::WegUngueltig;
        return false;
    }
    // --- Makros ----------------------------------------------------------
    //
    // Eine Makrozeile zieht ihre Befehle mit. Auf die Makrozeile HINEIN =
    // an den Anfang des Makros (im Original: "rein gesetzt, und zwar ganz
    // oben" - shank, am Original getestet); DAHINTER = hinter das ganze
    // Makro. Vor oder hinter einen Befehl des Makros = ins Makro. Ein Makro
    // kommt nie in ein Makro; dann hinter das Ziel-Makro.
    paths = mitGruppen(std::move(paths));
    const bool zieheMakro = std::any_of(paths.begin(), paths.end(), [&](const Path& p) {
        const Node* n = nodeAt(s_, p);
        return n != nullptr && n->kind == Node::Kind::Macro;
    });
    Path anker = ankerWunsch;
    Stelle wo = woWunsch;
    Kennung beitrittMakro = 0;
    bool insMakro = false;   // HINEIN in eine Makrozeile (flach: direkt dahinter)
    if (const Node* an = nodeAt(s_, anker); an != nullptr && an->kind == Node::Kind::Macro) {
        if (wo == Stelle::Hinein && !zieheMakro) {
            insMakro = true;
            beitrittMakro = an->kennung;
        } else if (wo != Stelle::Davor) {
            anker = gruppenEnde(ankerWunsch, paths);
            wo = Stelle::Dahinter;
        }
    } else if (const Path m = makroZu(anker); !m.empty()) {
        if (zieheMakro) {
            anker = gruppenEnde(m, paths);
            wo = Stelle::Dahinter;
        } else {
            beitrittMakro = kennungBei(m);
        }
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    // Was IN einem anderen gewaehlten Knoten liegt, zieht mit ihm mit.
    const auto liegtIn = [](const Path& innen, const Path& aussen) {
        return innen.size() > aussen.size() && std::equal(aussen.begin(), aussen.end(), innen.begin());
    };
    {
        std::vector<Path> oben;
        for (const Path& p : paths) {
            bool drin = false;
            for (const Path& q : paths) {
                if (liegtIn(p, q)) { drin = true; break; }
            }
            if (!drin) { oben.push_back(p); }
        }
        paths = std::move(oben);
    }
    for (const Path& p : paths) {
        // Vor/hinter/in sich selbst: keine Bewegung.
        if (p == anker) {
            diag::detail("Baum: Ziehen abgewiesen (" + wegText(p) + ") - Ziel ist der Gezogene");
            abweisung_ = Abweisung::ZielGleichHerkunft;
            return false;
        }
        // In sich selbst hinein - dann haenge der Baum an sich selbst.
        if (liegtIn(anker, p)) {
            diag::detail("Baum: Ziehen abgewiesen (von " + wegText(p) + " nach " + wegText(anker) +
                         ") - Ziel liegt IM Gezogenen");
            abweisung_ = Abweisung::ZielImGezogenen;
            return false;
        }
    }
    const Node* ziel = nodeAt(s_, anker);
    if (ziel == nullptr || (wo == Stelle::Hinein && !ziel->hasBlock && !insMakro)) {
        abweisung_ = Abweisung::WegUngueltig;
        return false;
    }
    std::vector<Node> genommen;
    for (const Path& p : paths) {
        if (const Node* n = nodeAt(s_, p)) {
            genommen.push_back(*n);
        }
    }
    if (genommen.empty()) {
        return false;
    }
    // Was gezogen wird, verlaesst sein Makro - es sei denn, es faellt wieder
    // hinein (merkeBeitritt unten). Sonst hielte "dazwischen gelandet" einen
    // herausgezogenen Befehl fuer ein Mitglied und zoege Fremde mit hinein.
    // Befehle, die MIT ihrer Makrozeile wandern, bleiben natuerlich drin.
    for (const Path& p : paths) {
        const Path m = makroZu(p);
        if (!m.empty() && std::find(paths.begin(), paths.end(), m) != paths.end()) {
            continue;
        }
        austritt_.push_back(kennungBei(p));
    }

    snapshot("moveto");
    // Absteigend entnehmen - jedes Entnehmen verschiebt die folgenden Wege,
    // auch den des Ankers, wenn der Knoten vor ihm auf seiner Ebene lag.
    Path a = anker;
    for (std::size_t k = paths.size(); k > 0; --k) {
        const Path& p = paths[k - 1];
        std::size_t i = 0;
        std::vector<Node>* sib = siblingsOf(p, i);
        if (sib == nullptr || i >= sib->size()) {
            continue;
        }
        sib->erase(sib->begin() + static_cast<std::ptrdiff_t>(i));
        if (p.size() <= a.size() && std::equal(p.begin(), p.end() - 1, a.begin()) &&
            p.back() < a[p.size() - 1]) {
            --a[p.size() - 1];
        }
    }
    Path erster;
    if (insMakro) {
        std::size_t j = 0;
        std::vector<Node>* sib = siblingsOf(a, j);
        if (sib == nullptr || j >= sib->size()) {
            verwirf();
            return false;
        }
        sib->insert(sib->begin() + static_cast<std::ptrdiff_t>(j) + 1, genommen.begin(), genommen.end());
        erster = a;
        erster.back() = j + 1;
    } else if (wo == Stelle::Hinein) {
        std::size_t j = 0;
        std::vector<Node>* sib = siblingsOf(a, j);
        if (sib == nullptr || j >= sib->size() || !(*sib)[j].hasBlock) {
            verwirf();
            return false;
        }
        std::vector<Node>& kids = (*sib)[j].children;
        kids.insert(kids.begin(), genommen.begin(), genommen.end());
        erster = a;
        erster.push_back(0);
    } else {
        std::size_t j = 0;
        std::vector<Node>* sib = siblingsOf(a, j);
        if (sib == nullptr || j >= sib->size()) {
            verwirf();
            return false;
        }
        const std::size_t bei = (wo == Stelle::Davor) ? j : j + 1;
        sib->insert(sib->begin() + static_cast<std::ptrdiff_t>(bei), genommen.begin(), genommen.end());
        erster = a;
        erster.back() = bei;
    }
    merkeBeitritt(erster, genommen.size(), beitrittMakro);
    diag::detail("Baum: " + std::to_string(genommen.size()) + " Knoten " +
                 (insMakro ? "ins Makro" : wo == Stelle::Davor ? "vor" : wo == Stelle::Hinein ? "in" : "hinter") +
                 " " + wegText(anker) + " gezogen, erster jetzt auf " + wegText(erster));
    if (ersterNachher != nullptr) {
        *ersterNachher = erster;
    }
    return true;
}

bool Document::moveAllTo(std::vector<Path> paths, const Path& to) {
    MakroWache wache(*this);
    if (paths.empty()) {
        return false;
    }
    // Mit Makros: dieselben Regeln wie beim Ziehen (moveAllAt, dahinter).
    {
        bool makro = !makroZu(to).empty();
        for (const Path& p : paths) {
            const Node* n = nodeAt(s_, p);
            makro = makro || (n != nullptr && n->kind == Node::Kind::Macro) || !makroZu(p).empty();
        }
        if (const Node* z = nodeAt(s_, to); z != nullptr && z->kind == Node::Kind::Macro) {
            makro = true;
        }
        if (makro) {
            return moveAllAt(std::move(paths), to, Stelle::Dahinter);
        }
    }
    if (paths.size() == 1) {
        return moveTo(paths.front(), to);
    }
    // Kein Knoten darf sein eigenes Ziel enthalten.
    for (const Path& p : paths) {
        if (p == to) {
            return false;
        }
        if (to.size() > p.size() && std::equal(p.begin(), p.end(), to.begin())) {
            return false;
        }
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    // Erst alle Knoten holen, dann absteigend entnehmen, dann am Ziel
    // einfuegen. In dieser Reihenfolge, weil jedes Entnehmen die folgenden
    // Wege verschiebt.
    std::vector<Node> taken;
    for (const Path& p : paths) {
        if (const Node* n = nodeAt(s_, p)) {
            taken.push_back(*n);
        }
    }
    if (taken.empty()) {
        return false;
    }

    snapshot("moveto");
    Path target = to;
    for (std::size_t k = paths.size(); k > 0; --k) {
        const Path& p = paths[k - 1];
        std::size_t i = 0;
        std::vector<Node>* sib = siblingsOf(p, i);
        if (sib == nullptr || i >= sib->size()) {
            continue;
        }
        sib->erase(sib->begin() + static_cast<std::ptrdiff_t>(i));
        // Lag der Knoten vor dem Ziel und auf dessen Ebene, rueckt das Ziel
        // um eins vor.
        if (p.size() <= target.size() &&
            std::equal(p.begin(), p.end() - 1, target.begin()) &&
            p.back() < target[p.size() - 1]) {
            --target[p.size() - 1];
        }
    }

    std::size_t j = 0;
    std::vector<Node>* dst = siblingsOf(target, j);
    if (dst == nullptr) {
        verwirf();
        return false;
    }
    const std::size_t at = std::min(j + 1, dst->size());
    dst->insert(dst->begin() + static_cast<std::ptrdiff_t>(at),
                taken.begin(), taken.end());
    diag::detail("Baum: " + std::to_string(taken.size()) +
                 " Befehle gezogen nach " + wegText(to) + " (Tiefe -> " +
                 std::to_string(to.size()) + ")");
    return true;
}

bool Document::moveToEnd(const Path& from, Path* nachher) {
    MakroWache wache(*this);
    abweisung_ = Abweisung::Keine;
    if (from.empty()) {
        abweisung_ = Abweisung::WegUngueltig;
        return false;
    }
    // Eine Makrozeile samt Befehlen ans Ende; hinter ein Makro am Ende, nicht
    // hinein.
    if (const Node* q = nodeAt(s_, from); q != nullptr && q->kind == Node::Kind::Macro && !s_.nodes.empty()) {
        Path letzte{s_.nodes.size() - 1};
        if (const Path m = makroZu(letzte); !m.empty()) {
            letzte = m;
        }
        return moveAllAt({from}, letzte, Stelle::Dahinter, nachher);
    }
    // Steht er schon als letzter oben, ist nichts zu tun - kein Zug, kein
    // Rueckgaengig-Schritt (dieselbe Regel wie in `moveTo`).
    if (from.size() == 1 && from[0] + 1 == s_.nodes.size()) {
        diag::detail("Baum: \"" + s_.nodes[from[0]].name +
                     "\" steht schon am Ende - nichts getan");
        abweisung_ = Abweisung::SchonAmEnde;
        return false;
    }
    const Node* src = nodeAt(s_, from);
    if (src == nullptr) {
        return false;
    }
    const Node copy = *src;
    // KEIN eigener `snapshot` hier: `removeAt` legt selbst einen an.
    //
    // Mit beidem gaebe es ZWEI Rueckgaengig-Schritte fuer einen Zug - das
    // erste Strg+Z holte den Befehl an seine alte Stelle, das zweite
    // machte gar nichts, und dazwischen waere er kurz weg. Genau die Sorte
    // Ueberraschung, die man beim Rueckgaengigmachen am wenigsten will.
    if (!removeAt(from)) {
        verwirf();
        return false;
    }
    s_.nodes.push_back(copy);
    if (nachher != nullptr) {
        *nachher = Path{s_.nodes.size() - 1};
    }
    diag::detail("Baum: \"" + copy.name + "\" ans Ende der obersten Ebene, " +
                 wegText(from) + " -> " + std::to_string(s_.nodes.size() - 1) +
                 " (Tiefe " + std::to_string(from.size()) + " -> 1)");
    return true;
}

bool Document::moveTo(const Path& from, const Path& to, Path* nachher) {
    MakroWache wache(*this);
    abweisung_ = Abweisung::Keine;
    // Mit Makros: dieselben Regeln wie beim Ziehen (moveAllAt, dahinter).
    if (!from.empty() && !to.empty() && from != to) {
        const Node* q = nodeAt(s_, from);
        const Node* z = nodeAt(s_, to);
        if ((q != nullptr && q->kind == Node::Kind::Macro) || (z != nullptr && z->kind == Node::Kind::Macro) ||
            !makroZu(to).empty()) {
            return moveAllAt({from}, to, Stelle::Dahinter, nachher);
        }
    }
    if (from.empty() || to.empty() || from == to) {
        // Auch die ABWEISUNG protokollieren.
        //
        // Bisher schwieg `moveTo`, wenn es `false` lieferte. Genau das ist
        // aber die interessante Auskunft, wenn jemand meldet "es geht
        // nicht": WARUM ging es nicht.
        diag::detail("Baum: Ziehen abgewiesen (von " + wegText(from) +
                     " nach " + wegText(to) + ") - leerer oder gleicher Weg");
        abweisung_ = (from == to) ? Abweisung::ZielGleichHerkunft
                                  : Abweisung::WegUngueltig;
        return false;
    }
    // Ein Block darf nicht in sich selbst - dann haenge der Baum an sich
    // selbst und jeder Durchlauf liefe endlos.
    if (to.size() > from.size() &&
        std::equal(from.begin(), from.end(), to.begin())) {
        diag::detail("Baum: Ziehen abgewiesen (von " + wegText(from) +
                     " nach " + wegText(to) + ") - Ziel liegt IM Gezogenen");
        abweisung_ = Abweisung::ZielImGezogenen;
        return false;
    }
    const Node* src = nodeAt(s_, from);
    if (src == nullptr) {
        return false;
    }
    const Node copy = *src;

    snapshot("moveto");

    // Entnehmen.
    {
        std::size_t i = 0;
        std::vector<Node>* sib = siblingsOf(from, i);
        if (sib == nullptr || i >= sib->size()) {
            verwirf();
            return false;
        }
        sib->erase(sib->begin() + static_cast<std::ptrdiff_t>(i));
    }

    // Zielweg anpassen: lag die Quelle davor UND auf derselben Ebene,
    // ruecken die folgenden Wege um eins vor.
    Path target = to;
    if (from.size() <= target.size() &&
        std::equal(from.begin(), from.end() - 1, target.begin()) &&
        from.back() < target[from.size() - 1]) {
        --target[from.size() - 1];
    }

    std::size_t j = 0;
    std::vector<Node>* dst = siblingsOf(target, j);
    if (dst == nullptr) {
        verwirf();
        return false;
    }
    const std::size_t at = std::min(j + 1, dst->size());
    dst->insert(dst->begin() + static_cast<std::ptrdiff_t>(at), copy);
    // Auch das Ziehen ins Protokoll - es nimmt einen anderen Weg als
    // `Move up/down`, und ohne Zeile weiss man hinterher nicht, WELCHER der
    // beiden Wege gelaufen ist.
    //
    // Die Tiefen stehen dabei: wird sie kleiner, hat der Befehl einen Block
    // verlassen. Genau das ist die Frage, die shank stellt.
    Path zielW = target;
    zielW.back() = at;
    // Ein Zug, der nichts bewegt, ist kein Zug.
    //
    // shanks Protokoll: `"set" gezogen, 4 -> 4`. Der Befehl landete, wo er
    // war, und hinterliess trotzdem einen Rueckgaengig-Schritt. Wer danach
    // Strg+Z drueckt, erwartet die vorige ECHTE Aenderung zurueck und
    // bekommt nichts.
    if (zielW == from) {
        verwirf();
        diag::detail("Baum: \"" + copy.name + "\" gezogen, aber Ziel gleich "
                     "Herkunft (" + wegText(from) + ") - nichts getan");
        abweisung_ = Abweisung::ZielGleichHerkunft;
        return false;
    }
    diag::detail("Baum: \"" + copy.name + "\" gezogen, " + wegText(from) +
                 " -> " + wegText(zielW) + " (Tiefe " +
                 std::to_string(from.size()) + " -> " +
                 std::to_string(zielW.size()) + ")");
    if (nachher != nullptr) {
        *nachher = zielW;
    }
    return true;
}

bool Document::copyAll(std::vector<Path> paths) {
    if (paths.empty()) {
        return false;
    }
    paths = mitGruppen(std::move(paths));   // ein Makro samt seinen Befehlen
    // Zum Kopieren AUFSTEIGEND, damit die Reihenfolge in der Ablage der im
    // Skript entspricht.
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    std::vector<Node> taken;
    for (const Path& p : paths) {
        if (const Node* n = nodeAt(s_, p)) {
            taken.push_back(*n);
        }
    }
    if (taken.empty()) {
        return false;
    }
    clip_ = std::move(taken);
    return true;
}

bool Document::cloneAll(std::vector<Path> paths) {
    MakroWache wache(*this);
    if (paths.empty()) {
        return false;
    }
    // Einheiten: ein gewaehltes Makro als Ganzes (seine Befehle nicht noch
    // einmal einzeln); die Kopie steht hinter dem ganzen Makro. Ein Befehl in
    // einem Makro bleibt mit seiner Kopie darin.
    std::vector<Path> makros;
    for (const Path& p : paths) {
        const Node* n = nodeAt(s_, p);
        if (n != nullptr && n->kind == Node::Kind::Macro) { makros.push_back(p); }
    }
    std::vector<Path> einheiten;
    for (const Path& p : paths) {
        const Node* n = nodeAt(s_, p);
        if (n == nullptr) { continue; }
        if (n->kind != Node::Kind::Macro) {
            const Path m = makroZu(p);
            if (!m.empty() && std::find(makros.begin(), makros.end(), m) != makros.end()) {
                continue;
            }
        }
        einheiten.push_back(p);
    }
    sortDescending(einheiten);
    snapshot("clone");
    bool any = false;
    for (const Path& p : einheiten) {
        std::size_t i = 0;
        std::vector<Node>* sib = siblingsOf(p, i);
        if (sib == nullptr || i >= sib->size()) {
            continue;
        }
        if ((*sib)[i].kind == Node::Kind::Macro) {
            const std::size_t e = gruppenLetzte(*sib, i);
            const std::vector<Node> gruppe(sib->begin() + static_cast<std::ptrdiff_t>(i),
                                           sib->begin() + static_cast<std::ptrdiff_t>(e) + 1);
            sib->insert(sib->begin() + static_cast<std::ptrdiff_t>(e) + 1, gruppe.begin(), gruppe.end());
            any = true;
            continue;
        }
        const Path m = makroZu(p);
        const Kennung mk = m.empty() ? 0 : kennungBei(m);
        Node copy = (*sib)[i];
        sib->insert(sib->begin() + static_cast<std::ptrdiff_t>(i) + 1,
                    std::move(copy));
        Path neu = p;
        neu.back() = i + 1;
        merkeBeitritt(neu, 1, mk);
        any = true;
    }
    if (!any) {
        // Nichts getan: die Momentaufnahme wieder zuruecknehmen, sonst
        // haette man einen leeren Schritt im Rueckgaengig-Stapel.
        verwirf();
    }
    return any;
}

bool Document::removeAll(std::vector<Path> paths) {
    MakroWache wache(*this);
    if (paths.empty()) {
        return false;
    }
    paths = mitGruppen(std::move(paths));   // eine Makrozeile samt Befehlen
    sortDescending(paths);
    snapshot("delete");
    bool any = false;
    for (const Path& p : paths) {
        std::size_t i = 0;
        std::vector<Node>* sib = siblingsOf(p, i);
        if (sib == nullptr || i >= sib->size()) {
            continue;
        }
        sib->erase(sib->begin() + static_cast<std::ptrdiff_t>(i));
        any = true;
    }
    if (!any) {
        // Nichts getan: die Momentaufnahme wieder zuruecknehmen, sonst
        // haette man einen leeren Schritt im Rueckgaengig-Stapel.
        verwirf();
    }
    return any;
}

bool Document::cutAll(std::vector<Path> paths) {
    MakroWache wache(*this);
    if (!copyAll(paths)) {
        return false;
    }
    return removeAll(std::move(paths));
}

const char* Document::undoLabel() const {
    return undo_.empty() ? "" : undo_.back().what;
}

const char* Document::redoLabel() const {
    return redo_.empty() ? "" : redo_.back().what;
}

} // namespace bhed
