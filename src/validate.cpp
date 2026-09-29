// validate.cpp
//
// Grundsatz: ein unbekanntes set-Ziel ist eine ANMERKUNG, kein Fehler.
// Beleg: SET_COLLIDABLE_ROFFS steht in zwei Raven-Skripten, aber weder im
// JKA-SDK-Header noch in OpenJK - ein Ueberbleibsel aus JK2. Wer solche
// Werte abweist, zerschiesst beim Oeffnen-und-Speichern fremde Skripte.

#include "bhed/validate.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace bhed {
namespace {

bool isNumber(const std::string& s) {
    if (s.empty()) { return false;
}
    std::size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    bool digit = false;
    bool dot = false;
    for (; i < s.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(s[i])) != 0) { digit = true; continue; }
        if (s[i] == '.' && !dot) { dot = true; continue; }
        return false;
    }
    return digit;
}

const char* kindName(Param::Kind k) {
    switch (k) {
        case Param::Kind::String:  return "Text";
        case Param::Kind::Int:     return "Ganzzahl";
        case Param::Kind::Float:   return "Kommazahl";
        case Param::Kind::Vector:  return "Vektor";
        case Param::Kind::Range:   return "Bereich";
        case Param::Kind::TypeSet: return "Auswahl";
        case Param::Kind::Expr:    return "Ausdruck";
        case Param::Kind::Op:      return "Vergleich";
    }
    return "?";
}

// Ein Ausdruck $...$ darf ueberall stehen - get(), random(), tag() liefern
// zur Laufzeit den passenden Typ. 1329 Vorkommen in Ravens Skripten.
bool acceptsExpr(const Arg& a) { return a.kind == Arg::Kind::Expr; }

// "Meintest du ...?" fuer einen Wert, der nicht in der Tabelle steht.
//
// Warum es das gibt
// -----------------
// Das offizielle ICARUS_Manual.doc stammt aus der Zeit vor Jedi Academy und
// nennt sechs Set-Felder unter Namen, die es in JKA nicht mehr gibt.
// Gemessen am Original-Q3_Interface.h (261 Felder) gegen die 195 im
// Handbuch genannten:
//
//   Handbuch                 Q3_Interface.h (JKA)
//   SET_BEHAVIORSTATE   ->   SET_BEHAVIOR_STATE
//   SET_COPYORIGIN      ->   SET_COPY_ORIGIN
//   SET_DEFAULTBSTATE   ->   SET_DEFAULT_BSTATE
//   SET_ENEMYTEAM       ->   SET_ENEMY_TEAM
//   SET_LOCKEDENEMY     ->   SET_LOCKED_ENEMY
//   SET_PLAYERTEAM      ->   SET_PLAYER_TEAM
//
// Der Unterschied ist jedes Mal nur ein Unterstrich. Wer nach dem Handbuch
// arbeitet - und alle tun das, es ist die einzige Anleitung - schreibt die
// linke Spalte und bekam bisher nur "steht nicht in SET_TYPES". Richtig,
// aber es sagt nicht, was zu tun ist.
//
// Acht weitere Namen aus dem Handbuch gibt es in JKA gar nicht mehr
// (SET_CAPTUREGOAL, SET_STUCKSCRIPT, SET_TEMPBEHAVIOR, SET_AFFIRMNEGTARG,
// SET_PRECACHE, SET_MISSION_STATUS_SCREEN sowie die Platzhalter SET_TABLE
// und SET_PARM). Fuer die gibt es zu Recht keinen Vorschlag.
//
// Zwei Stufen, beide billig:
//   1. Unterstriche und Gross-/Kleinschreibung wegdenken - trifft alle
//      sechs oben genau.
//   2. Sonst die uebliche Bearbeitungsentfernung, hoechstens 2 (gemessen,
//      siehe unten). Bei SET_GIBBERISH kommt so nichts heraus, und das ist
//      richtig: ein Vorschlag, der danebenliegt, ist schlimmer als keiner.
std::string vereinfache(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c != '_') { o += static_cast<char>(std::tolower(
                                static_cast<unsigned char>(c))); }
    }
    return o;
}

std::size_t abstand(const std::string& a, const std::string& b) {
    std::vector<std::size_t> zeile(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) { zeile[j] = j; }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t schraeg = zeile[0];
        zeile[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t alt = zeile[j];
            const std::size_t kosten = (a[i - 1] == b[j - 1]) ? 0U : 1U;
            zeile[j] = std::min({zeile[j] + 1, zeile[j - 1] + 1,
                                 schraeg + kosten});
            schraeg = alt;
        }
    }
    return zeile[b.size()];
}

std::string naechster(const std::string& wert, const TypeSet& ts) {
    const std::string ziel = vereinfache(wert);
    // Stufe 1: gleich, wenn man Unterstriche wegdenkt.
    for (const TypeEntry& e : ts.entries) {
        if (vereinfache(e.name) == ziel) {
            return " (meintest du " + e.name + "?)";
        }
    }
    // Stufe 2: naechster Nachbar, aber nur wenn er wirklich nah ist.
    //
    // Die Schwelle ist gemessen, nicht geschaetzt. Gegen die acht Namen,
    // die es in JKA wirklich nicht mehr gibt (plus zwei erfundene):
    //
    //   Abstand <= 1   0 Fehlvorschlaege von 8
    //   Abstand <= 2   0 Fehlvorschlaege von 8
    //   Abstand <= 3   1 - SET_STUCKSCRIPT waere zu SET_ATTACKSCRIPT
    //                      geworden, und das ist ein ganz anderes Feld
    //
    // Deshalb feste 2, nicht ein Anteil der Laenge. Ein Vorschlag, der
    // danebenliegt, ist schlimmer als keiner: er schickt jemanden in die
    // falsche Richtung, und dort sucht er dann.
    const std::size_t grenze = 2;
    std::size_t beste = grenze + 1;
    const TypeEntry* treffer = nullptr;
    for (const TypeEntry& e : ts.entries) {
        const std::size_t d = abstand(ziel, vereinfache(e.name));
        if (d < beste) { beste = d; treffer = &e; }
    }
    if (treffer != nullptr) {
        return " (meintest du " + treffer->name + "?)";
    }
    return {};
}

void checkArg(const Param& p, const Arg& a, const CommandDb& db,
              const std::string& where, std::vector<Issue>& out) {
    if (acceptsExpr(a)) { return;
}

    // Gemessen an 9200 set-Aufrufen: die Schreibform folgt der Deklaration
    // des Auswahleintrags selbst.
    //   %t / %s -> zitiert   ("false", "taspir2/darksideend_skip")
    //   %d / %f -> blank     (0, 2000.000)
    //   %v      -> < x y z >
    // 54 Ausreisser sind Handarbeit und sollen als Anmerkung auffallen.

    if (p.kind == Param::Kind::TypeSet) {
        const TypeSet* ts = db.typeset(p.typeset);
        if (ts == nullptr) {
            out.push_back({Issue::Level::Error, "V005", where,
                           "Typmenge " + p.typeset + " unbekannt"});
            return;
        }
        // Schreibweise: nur %i-Mengen stehen blank. %s und %d werden
        // zitiert - FORCE_LEVELS ist %d und steht in 79 Belegen als "3".
        const bool wantQuoted = (ts->kind != 'i');
        const bool isQuoted = (a.kind == Arg::Kind::String);
        if (wantQuoted != isQuoted && a.kind != Arg::Kind::Number) {
            out.push_back({Issue::Level::Warning, "V006", where,
                           std::string("Wert ") + a.text + " ist " +
                           (isQuoted ? "in Anfuehrungszeichen" : "blank") +
                           ", Typmenge " + ts->name + " ist %" + ts->kind});
}
        if (!a.typeset.empty() && a.typeset != ts->name) {
            out.push_back({Issue::Level::Warning, "V007", where,
                           "Annotation /*@" + a.typeset + "*/ passt nicht zu " + ts->name,
                           a.typeset, ts->name});
}
        if (ts->find(a.text) == nullptr) {
            out.push_back({Issue::Level::Warning, "V004", where,
                           a.text + " steht nicht in " + ts->name +
                           naechster(a.text, *ts) +
                           " (bleibt unveraendert erhalten)",
                           a.text, ts->name});
}
        return;
    }

    switch (p.kind) {
        case Param::Kind::String:
            if (a.kind != Arg::Kind::String) {
                out.push_back({Issue::Level::Warning, "V011", where,
                               "Textfeld ohne Anfuehrungszeichen: " + a.text,
                               a.text, ""});
}
            break;
        case Param::Kind::Int:
        case Param::Kind::Float:
            if (a.kind != Arg::Kind::Number || !isNumber(a.text)) {
                out.push_back({Issue::Level::Warning, "V008", where,
                               std::string("Feld erwartet ") + kindName(p.kind) +
                               ", gefunden: " + a.text});
}
            break;
        case Param::Kind::Vector:
            if (a.kind != Arg::Kind::Vector) {
                out.push_back({Issue::Level::Warning, "V008", where,
                               "Feld erwartet Vektor, gefunden: " + a.text});
}
            break;
        case Param::Kind::Op:
            if (a.text != "=" && a.text != "<" && a.text != ">" && a.text != "!") {
                out.push_back({Issue::Level::Warning, "V009", where,
                               "Vergleich erwartet = < > !, gefunden: " + a.text,
                               a.text, ""});
}
            break;
        default:
            break;
    }
}

int scoreOverload(const Command& c, const CommandDb& db, const Node& n) {
    const std::vector<Param> f = expandFields(c, db, n.args);
    int score = 0;
    if (f.size() != n.args.size()) { score += 100 * static_cast<int>(
        f.size() > n.args.size() ? f.size() - n.args.size() : n.args.size() - f.size());
}
    if (c.block != n.hasBlock) { score += 50;
}

    for (std::size_t i = 0; i < f.size() && i < n.args.size(); ++i) {
        const Param& p = f[i];
        const Arg& a = n.args[i];
        if (a.kind == Arg::Kind::Expr) { continue;
}
        // Die Annotation im Skript zeigt die gemeinte Ueberladung an - genau
        // dafuer schreibt BehavEd sie hin. set("fXY","1") ohne Annotation ist
        // eine Variable, set(/*@SET_TYPES*/ "SET_HEALTH", 5) ist die Auswahl.
        if (p.kind == Param::Kind::TypeSet) {
            if (a.typeset == p.typeset) { score -= 20;
            } else if (!a.typeset.empty()) { score += 20;
            } else {
                // Ohne Annotation eine Spur schlechter als eine Signatur
                // ohne Klappliste - so entscheidet das Original bei
                // Gleichstand: die Makrozeilen set ( /*!*/ "SET_WALKING",
                // /*!*/ "false" ) oeffnet es als set( %s, %s ) mit zwei
                // Textfeldern, nicht als SET_TYPES-Auswahl (Abgleich 27.09.).
                score += 1;
            }
            const TypeSet* ts = db.typeset(p.typeset);
            if ((ts != nullptr) && (ts->find(a.text) == nullptr)) { score += 5;
}
        } else if (!a.typeset.empty()) {
            score += 10;
        }
        if ((p.kind == Param::Kind::Int || p.kind == Param::Kind::Float) &&
            a.kind != Arg::Kind::Number) {
            score += 3;
}
        if (p.kind == Param::Kind::Vector && a.kind != Arg::Kind::Vector) { score += 3;
}
    }
    return score;
}

}  // namespace

const Command* selectOverload(const Node& n, const CommandDb& db) {
    const std::vector<const Command*> ov = db.overloads(n.name);
    if (ov.empty()) {
        return nullptr;
    }
    const Command* best = ov.front();
    int bestScore = scoreOverload(*best, db, n);
    for (std::size_t i = 1; i < ov.size(); ++i) {
        const int s = scoreOverload(*ov[i], db, n);
        if (s < bestScore) { bestScore = s; best = ov[i]; }
    }
    return best;
}

namespace {

void walk(const std::vector<Node>& ns, const CommandDb& db,
          const std::string& path, std::vector<Issue>& out) {
    for (const Node& n : ns) {
        if (n.kind != Node::Kind::Command) { continue;
}
        const std::string where = path.empty() ? n.name : path + "/" + n.name;

        const Command* best = selectOverload(n, db);
        if (best == nullptr) {
            out.push_back({Issue::Level::Error, "V001", where,
                           "Befehl steht nicht in der .bhc", n.name, ""});
            walk(n.children, db, where, out);
            continue;
        }

        // --- Fuer die PRUEFUNG zaehlt, was der Eintrag verlangt ----------
        //
        // selectOverload waehlt fuer ein `set` ohne /*@SET_TYPES*/ die
        // Fassung "set for variables" (<str>, <str>) - so oeffnet es das
        // Original. Die Felder dieser Fassung sind aber nicht, was das
        // Skript braucht: `set ( SET_ANIM_HOLDTIME_BOTH, -1.000 )` ist ein
        // %f-Eintrag und steht zu Recht blank. Gemeldet: jede solche Zeile
        // bekam V011 "Textfeld ohne Anfuehrungszeichen".
        //
        // Steht der erste Wert in der Typmenge einer anderen Fassung, wird
        // gegen DEREN Felder geprueft (samt eigenen Parametern des
        // Eintrags, expandFields).
        const Command* pruef = best;
        if (!n.args.empty() && !best->params.empty() &&
            best->params.front().kind != Param::Kind::TypeSet) {
            for (const Command* c : db.overloads(n.name)) {
                if (c->params.empty() || c->params.front().kind != Param::Kind::TypeSet) {
                    continue;
                }
                const TypeSet* ts = db.typeset(c->params.front().typeset);
                if (ts != nullptr && ts->find(n.args.front().text) != nullptr) {
                    pruef = c;
                    break;
                }
            }
        }
        const std::vector<Param> f = expandFields(*pruef, db, n.args);
        if (f.size() != n.args.size()) {
            out.push_back({Issue::Level::Error, "V002", where,
                           "erwartet " + std::to_string(f.size()) + " Felder, gefunden " +
                           std::to_string(n.args.size())});
}
        if (pruef->block != n.hasBlock) {
            out.push_back({Issue::Level::Error, "V010", where,
                           pruef->block ? "Block { } fehlt" : "unerwarteter Block { }"});
}

        for (std::size_t i = 0; i < f.size() && i < n.args.size(); ++i) {
            checkArg(f[i], n.args[i], db, where, out);
}

        walk(n.children, db, where, out);
    }
}

} // namespace

std::vector<Param> expandFields(const Command& c, const CommandDb& db,
                                const std::vector<Arg>& args) {
    std::vector<Param> out;
    std::size_t skip = 0;
    for (const auto & p : c.params) {
        if (skip != 0U) { --skip; continue; }
        out.push_back(p);
        if (p.kind != Param::Kind::TypeSet) { continue;
}
        const TypeSet* ts = db.typeset(p.typeset);
        if (ts == nullptr) { continue;
}
        const std::size_t at = out.size() - 1;
        if (at >= args.size()) { continue;
}
        const TypeEntry* e = ts->find(args[at].text);
        if ((e == nullptr) || e->params.empty()) { continue;
}
        for (const Param& sp : e->params) { out.push_back(sp);
}
        skip = e->params.size();
    }
    return out;
}

void validate(const Script& s, const CommandDb& db, std::vector<Issue>& out) {
    walk(s.nodes, db, "", out);
}

} // namespace bhed
