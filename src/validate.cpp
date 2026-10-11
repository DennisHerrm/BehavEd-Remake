// validate.cpp
//
// Grundsatz: ein unbekanntes set-Ziel ist eine ANMERKUNG, kein Fehler.
// Beleg: SET_COLLIDABLE_ROFFS steht in zwei Raven-Skripten, aber weder im
// JKA-SDK-Header noch in OpenJK - ein Ueberbleibsel aus JK2. Wer solche
// Werte abweist, zerschiesst beim Oeffnen-und-Speichern fremde Skripte.

#include "bhed/validate.h"
#include "bhed/mpfelder.h"

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

const TypeEntry* naechsterEintrag(const std::string& wert, const TypeSet& ts) {
    const std::string ziel = vereinfache(wert);
    // Stufe 1: gleich, wenn man Unterstriche wegdenkt.
    for (const TypeEntry& e : ts.entries) {
        if (vereinfache(e.name) == ziel) {
            return &e;
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
    return treffer;
}

std::string naechster(const std::string& wert, const TypeSet& ts) {
    const TypeEntry* e = naechsterEintrag(wert, ts);
    return e != nullptr ? " (meintest du " + e->name + "?)" : std::string();
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

namespace {

std::string grossText(const std::string& s) {
    std::string g = s;
    for (char& c : g) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
    return g;
}

// Alle $get( TYP, "FELD" ) in einem Ausdruck.
std::vector<std::pair<std::string, std::string>> getsIn(const std::string& t) {
    std::vector<std::pair<std::string, std::string>> out;
    const std::string g = grossText(t);
    std::size_t p = 0;
    while ((p = g.find("GET(", p)) != std::string::npos) {
        std::size_t q = p + 4;
        while (q < g.size() && g[q] == ' ') { ++q; }
        std::size_t e = q;
        while (e < g.size() && (std::isalpha(static_cast<unsigned char>(g[e])) != 0)) { ++e; }
        const std::string typ = g.substr(q, e - q);
        const std::size_t a = g.find('"', e);
        const std::size_t b = a == std::string::npos ? a : g.find('"', a + 1);
        if (b != std::string::npos && !typ.empty()) {
            out.emplace_back(typ, g.substr(a + 1, b - a - 1));
        }
        p += 4;
    }
    return out;
}

void sammleNamen(const std::vector<Node>& ns, std::vector<std::string>& variablen,
                 std::vector<std::pair<std::string, const Node*>>& tasks) {
    for (const Node& n : ns) {
        if (n.kind != Node::Kind::Command) { continue; }
        if (n.name == "declare" && n.args.size() >= 2) { variablen.push_back(grossText(n.args[1].text)); }
        if (n.name == "task" && !n.args.empty()) { tasks.emplace_back(grossText(n.args[0].text), &n); }
        sammleNamen(n.children, variablen, tasks);
    }
}

// Haelt ein Task etwas, das im MP nie fertig wird? (Kein MP-Code meldet
// TID_ANIM_*, TID_ANIM_HOLD, TID_RESIZE oder TID_CHAN_VOICE als erledigt.)
const Node* nieFertig(const std::vector<Node>& ns) {
    for (const Node& n : ns) {
        if (n.kind != Node::Kind::Command) { continue; }
        if (n.name == "set" && !n.args.empty()) {
            const std::string f = grossText(n.args[0].text);
            if (f.rfind("SET_ANIM_", 0) == 0 || f == "SET_ENDFRAME" || f == "SET_ANIMFRAME" || f == "SET_WIDTH") {
                return &n;
            }
        }
        if (n.name == "sound" && !n.args.empty() && grossText(n.args[0].text).rfind("CHAN_VOICE", 0) == 0) {
            return &n;
        }
        if (const Node* k = nieFertig(n.children)) { return k; }
    }
    return nullptr;
}

void mpWalk(const std::vector<Node>& ns, const CommandDb& db, const std::string& path,
            const std::vector<std::string>& variablen,
            const std::vector<std::pair<std::string, const Node*>>& tasks, std::vector<Issue>& out) {
    using L = Issue::Level;
    for (const Node& n : ns) {
        if (n.kind != Node::Kind::Command) { continue; }
        const std::string where = path.empty() ? n.name : path + "/" + n.name;
        if (n.name == "camera") {
            out.push_back({L::Warning, "M001", where, "camera tut im MP nichts (alle Unterbefehle sind Stubs)",
                           n.args.empty() ? std::string() : n.args[0].text, ""});
        }
        if (n.name == "print" && !n.args.empty() && n.args[0].kind == Arg::Kind::String) {
            const std::string& t = n.args[0].text;
            if (t.rfind('!', 0) != 0 && t.rfind("@@@", 0) != 0) {
                out.push_back({L::Warning, "M007", where,
                               "print ohne ! am Anfang zeigt im MP nichts an", t, ""});
            }
        }
        if (n.name == "set" && !n.args.empty() && n.args[0].kind != Arg::Kind::Expr) {
            const std::string name = grossText(n.args[0].text);
            const mp::Feld* f = mp::feld(name);
            const bool variable = std::find(variablen.begin(), variablen.end(), name) != variablen.end();
            if ((f == nullptr || !f->inMp) && !variable) {
                out.push_back({L::Warning, "M002", where, name + " kennt der MP nicht (variable or field not found)",
                               name, ""});
            } else if (f != nullptr && f->inMp) {
                if (f->status == mp::Status::Stub) {
                    out.push_back({L::Warning, "M003", where, name + " ist im MP ein Stub und tut nichts", name, ""});
                } else if (f->status == mp::Status::OhneWirkung) {
                    out.push_back({L::Warning, "M004", where, name + " wird im MP angenommen, tut aber nichts", name, ""});
                } else {
                    if (f->wer == mp::Wer::Npc) {
                        out.push_back({L::Info, "M005", where, name + " wirkt im MP nur auf NPCs", name, ""});
                    } else if (f->wer == mp::Wer::SpielerOderNpc) {
                        out.push_back({L::Info, "M006", where, name + " wirkt im MP nur auf Spieler und NPCs", name, ""});
                    }
                    if (f->ravenStub) {
                        out.push_back({L::Info, "M016", where,
                                       name + " wirkt nur auf OpenJK-Servern (im originalen MP ein Stub)", name, ""});
                    }
                }
                if (name == "SET_ORIGIN" || name == "SET_TELEPORT_DEST") {
                    out.push_back({L::Info, "M008", where, name + " wirkt im MP nicht auf Spieler und NPCs", name, ""});
                }
                if (name == "SET_WEAPON" || name == "SET_COPY_ORIGIN" || name == "SET_SABERACTIVE") {
                    out.push_back({L::Warning, "M009", where,
                                   name + " stuerzt den MP-Server ab, wenn das Skript nicht auf einem Spieler oder NPC laeuft",
                                   name, ""});
                }
                if (name == "SET_SABERACTIVE") {
                    out.push_back({L::Warning, "M010", where, "SET_SABERACTIVE ist im MP umgekehrt", name, ""});
                }
                if (name == "SET_SABER_OFFENSE" || name == "SET_SABER_DEFENSE" || name == "SET_SABER_THROW") {
                    const char* stattdessen = name == "SET_SABER_OFFENSE"   ? "Absorb"
                                              : name == "SET_SABER_DEFENSE" ? "Protect"
                                                                            : "Rage";
                    out.push_back({L::Warning, "M011", where, name + " setzt im MP die Macht " + stattdessen,
                                   name, stattdessen});
                }
                if (name == "SET_TIMESCALE") {
                    out.push_back({L::Info, "M012", where, "SET_TIMESCALE gilt fuer den ganzen Server", name, ""});
                }
                if (name == "SET_GRAVITY") {
                    out.push_back({L::Info, "M013", where, "SET_GRAVITY haelt im MP nur bei NPCs", name, ""});
                }
            }
        }
        // $get in den Argumenten
        for (const Arg& a : n.args) {
            if (a.kind != Arg::Kind::Expr) { continue; }
            for (const auto& [typ, feldname] : getsIn(a.text)) {
                const mp::Get g = mp::get(feldname, typ);
                if (g == mp::Get::Scheitert) {
                    out.push_back({L::Warning, "M015", where,
                                   "$get( " + typ + ", " + feldname + " ) liefert im MP nie einen Wert",
                                   typ, feldname});
                } else if (g == mp::Get::AbsturzOhneParms) {
                    out.push_back({L::Warning, "M009", where,
                                   "$get( " + typ + ", " + feldname + " ) stuerzt ab, wenn die Entity keine Parms hat",
                                   feldname, ""});
                }
            }
        }
        if (n.name == "dowait" && !n.args.empty()) {
            const std::string t = grossText(n.args[0].text);
            for (const auto& [name, task] : tasks) {
                if (name != t) { continue; }
                if (const Node* k = nieFertig(task->children)) {
                    const std::string was = k->name == "set" && !k->args.empty() ? k->args[0].text : k->name;
                    out.push_back({L::Warning, "M017", where,
                                   "dowait kehrt im MP nie zurueck: der Task enthaelt " + was, n.args[0].text, was});
                }
                break;
            }
        }
        mpWalk(n.children, db, where, variablen, tasks, out);
    }
}

// set ( "SET_..." ) ohne /*@SET_TYPES*/: ein Feldname, den es nicht gibt, ist
// ein Tippfehler (Variablen heissen nicht SET_...). Mit Annotation meldet ihn
// schon V004.
void feldnamenWalk(const std::vector<Node>& ns, const CommandDb& db, const std::string& path,
                   std::vector<Issue>& out) {
    const TypeSet* felder = db.typeset("SET_TYPES");
    for (const Node& n : ns) {
        if (n.kind != Node::Kind::Command) { continue; }
        const std::string where = path.empty() ? n.name : path + "/" + n.name;
        if (felder != nullptr && n.name == "set" && !n.args.empty() && n.args[0].typeset.empty() &&
            n.args[0].kind != Arg::Kind::Expr) {
            const std::string name = n.args[0].text;
            // Auch die Felder von Movie Duels (in der Tabelle als SP-Feld) gelten - sie
            // stehen nicht in der mitgelieferten SDK-Liste.
            const mp::Feld* bekannt = mp::feld(name);
            if (grossText(name).rfind("SET_", 0) == 0 && felder->find(name) == nullptr &&
                (bekannt == nullptr || !bekannt->inSp)) {
                const TypeEntry* vorschlag = naechsterEintrag(name, *felder);
                out.push_back({Issue::Level::Warning, "V012", where,
                               name + " ist kein Feld" + (vorschlag != nullptr ? " (meintest du " + vorschlag->name + "?)" : ""),
                               name, vorschlag != nullptr ? vorschlag->name : std::string()});
            }
        }
        feldnamenWalk(n.children, db, where, out);
    }
}

}  // namespace

void validate(const Script& s, const CommandDb& db, std::vector<Issue>& out, bool mp) {
    walk(s.nodes, db, "", out);
    if (!mp) {
        // Im MP meldet M002 dasselbe (und mehr) - nicht doppelt.
        feldnamenWalk(s.nodes, db, "", out);
        return;
    }
    std::vector<std::string> variablen;
    std::vector<std::pair<std::string, const Node*>> tasks;
    sammleNamen(s.nodes, variablen, tasks);
    mpWalk(s.nodes, db, "", variablen, tasks, out);
    // Hoechstens 32 Variablen auf einmal (Q3_Registers, MAX_VARIABLES).
    if (variablen.size() > 32) {
        out.push_back({Issue::Level::Warning, "M014", "declare",
                       std::to_string(variablen.size()) + " Variablen - der MP haelt hoechstens 32",
                       std::to_string(variablen.size()), ""});
    }
}

} // namespace bhed
