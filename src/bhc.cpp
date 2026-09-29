// bhc.cpp - .bhc und Engine-Kopfdateien lesen
//
// Belegte Eigenheiten, die hier abgebildet sind:
//  * Befehlszeilen koennen mit "//" statt "//#" kommentiert sein
//    (move ist so geschrieben - 602 Vorkommen in Ravens Skripten).
//  * "//# #eol" in einem Header schneidet die Liste ab. bstate.h hat
//    danach sieben interne bStates, die im Editor nicht erscheinen duerfen.
//  * Ein Auswahllisteneintrag kann eigene Parameter tragen (//##):
//    SET_HEALTH bringt %d mit, CHAN_VOICE einen Dateifilter.

#include "bhed/commands.h"

#include <cstddef>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace bhed {
namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (a < b && sp(s[a])) { ++a;
}
    while (b > a && sp(s[b - 1])) { --b;
}
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}
    return s;
}

std::vector<std::string> readLines(const fs::path& p, bool* ok) {
    // Am Stueck lesen. Zeichenweises f.get() kostete beim Laden des Modells
    // 112 ms - mehr als das Zerlegen aller 1510 Skripte zusammen.
    std::ifstream f(p, std::ios::binary);
    *ok = static_cast<bool>(f);
    std::vector<std::string> out;
    if (!*ok) { return out;
}
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string all = ss.str();
    std::string cur;
    cur.reserve(128);
    for (char c : all) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') { cur += c;
}
    }
    if (!cur.empty()) { out.push_back(cur);
}
    return out;
}

// /* ... */ entfernen, Verschachtelung mitzaehlen (die .bhc kommentiert
// einen ganzen Altbestand am Ende aus).
void stripBlockComments(std::vector<std::string>& lines) {
    int depth = 0;
    for (std::string& ln : lines) {
        std::string out;
        for (std::size_t i = 0; i < ln.size();) {
            if (ln.compare(i, 2, "/*") == 0) { ++depth; i += 2; continue; }
            if (ln.compare(i, 2, "*/") == 0 && (depth != 0)) { --depth; i += 2; continue; }
            if (depth == 0) { out += ln[i];
}
            ++i;
        }
        ln = out;
    }
}

Param::Kind kindOf(char c) {
    switch (c) {
        case 'd': return Param::Kind::Int;
        case 'f': return Param::Kind::Float;
        case 'v': return Param::Kind::Vector;
        case 't': return Param::Kind::TypeSet;
        case 'r': return Param::Kind::Range;
        default:  return Param::Kind::String;
    }
}

} // namespace

void parseParamSpec(const std::string& spec, std::vector<Param>& out, std::string& desc) {
    desc.clear();
    std::string head = spec;

    // Dateifilter  !!"..."  herausloesen, gehoert zum zuletzt gelesenen Feld
    std::string filter;
    for (std::size_t i = 0; i + 2 < head.size(); ++i) {
        if (head.compare(i, 3, "!!\"") == 0) {
            std::size_t e = head.find('"', i + 3);
            if (e == std::string::npos) { break;
}
            filter = head.substr(i + 3, e - i - 3);
            std::size_t stop = e + 1;
            if (head.compare(stop, 2, "!!") == 0) { stop += 2;
}
            head.erase(i, stop - i);
            break;
        }
    }

    // Beschreibung hinter dem ersten ' # '
    std::size_t h = head.find('#');
    if (h != std::string::npos) {
        desc = trim(head.substr(h + 1));
        head = head.substr(0, h);
    }

    bool locked = false;
    for (std::size_t i = 0; i < head.size(); ++i) {
        char c = head[i];
        if (c == '$') {
            std::size_t e = head.find('$', i + 1);
            if (e == std::string::npos) { break;
}
            std::string inner = trim(head.substr(i + 1, e - i - 1));
            Param p;
            bool onlyOps = !inner.empty();
            for (char x : inner) {
                if (x != '<' && x != '>' && x != '!' && x != '=' && x != ' ') { onlyOps = false;
}
}
            p.kind = onlyOps ? Param::Kind::Op : Param::Kind::Expr;
            p.wire = p.kind;
            p.def = inner;
            out.push_back(p);
            i = e;
            continue;
        }
        if (c != '%' || i + 1 >= head.size()) { continue;
}
        char t = head[i + 1];
        if (t == '%') { ++i; continue; }            // %% = keine Parameter
        if (t == 'r' && i + 2 < head.size() && head[i + 2] == '%') {
            locked = true;                          // %r%s -> gesperrtes Feld
            ++i;
            continue;
        }
        Param p;
        p.kind = kindOf(t);
        p.locked = locked;
        locked = false;
        i += 2;
        if (i < head.size() && head[i] == '=') {
            ++i;
            if (i < head.size() && head[i] == '"') {
                std::size_t e = head.find('"', i + 1);
                if (e == std::string::npos) { e = head.size();
}
                p.def = head.substr(i + 1, e - i - 1);
                i = e;
            } else if (i < head.size() && head[i] == '<') {
                std::size_t e = head.find('>', i + 1);
                if (e == std::string::npos) { e = head.size();
}
                p.def = trim(head.substr(i + 1, e - i - 1));
                i = e;
            } else {
                std::size_t e = i;
                while (e < head.size() && head[e] != ',' && head[e] != ')' &&
                       head[e] != ' ' && head[e] != '%') {
                    ++e;
}
                p.def = trim(head.substr(i, e - i));
                i = e - 1;
            }
        }
        if (p.kind == Param::Kind::TypeSet) { p.typeset = p.def;
}
        p.wire = p.kind;
        out.push_back(p);
    }
    if (!filter.empty() && !out.empty()) { out.back().filter = filter;
}
}

const TypeEntry* TypeSet::find(const std::string& v) const {
    for (const TypeEntry& e : entries) {
        if (e.name == v) { return &e;
}
}
    return nullptr;
}

const TypeSet* CommandDb::typeset(const std::string& n) const {
    auto it = typesets.find(n);
    return it == typesets.end() ? nullptr : &it->second;
}

const Macro* CommandDb::macro(const std::string& name) const {
    for (const Macro& m : macroBodies) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}

std::vector<const Command*> CommandDb::overloads(const std::string& n) const {
    std::vector<const Command*> out;
    for (const Command& c : commands) {
        if (c.name == n) { out.push_back(&c);
}
}
    return out;
}

namespace {

// Ein C-Enum aus einer Kopfdatei lesen, markiert durch
//   typedef enum //# setType_e
bool parseEnum(const fs::path& path, const std::string& enumName,
               std::vector<TypeEntry>& out, int& hidden) {
    bool ok = false;
    std::vector<std::string> lines = readLines(path, &ok);
    if (!ok) { return false;
}

    std::size_t start = std::string::npos;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        std::size_t te = l.find("typedef enum");
        if (te == std::string::npos) { continue;
}
        std::size_t h = l.find("//#", te);
        if (h == std::string::npos) { continue;
}
        if (trim(l.substr(h + 3)) == enumName) { start = i; break; }
    }
    if (start == std::string::npos) { return false;
}

    int depth = 0;
    bool afterEol = false;
    std::string section;
    hidden = 0;
    for (std::size_t i = start; i < lines.size(); ++i) {
        const std::string l = lines[i];
        if (l.find('{') != std::string::npos) { ++depth; continue; }
        if (l.find('}') != std::string::npos && (depth != 0)) { break;
}
        if (depth == 0) { continue;
}

        std::string s = trim(l);
        if (s.starts_with("//#") && trim(s.substr(3)) == "#eol") { afterEol = true; continue; }
        // "//# #sep <Text>" setzt eine Ueberschrift fuer die folgenden
        // Eintraege. Dieselbe Familie wie #eol, und wie dieses ausdruecklich
        // fuer BehavEd in die Kopfdateien geschrieben.
        if (s.starts_with("//#")) {
            const std::string rest = trim(s.substr(3));
            if (rest.starts_with("#sep")) {
                section = trim(rest.substr(4));
                // Manche Ueberschriften tragen selbst noch einen Kommentar:
                //   #sep BOTH_ DEAD POSES # Should be last frame of ...
                const std::size_t hash = section.find(" # ");
                if (hash != std::string::npos) {
                    section = trim(section.substr(0, hash));
                }
                continue;
            }
        }

        // Bezeichner am Zeilenanfang
        std::size_t k = 0;
        if (k >= s.size() || ((std::isupper(static_cast<unsigned char>(s[k])) == 0) && s[k] != '_')) { continue;
}
        while (k < s.size() && ((std::isalnum(static_cast<unsigned char>(s[k])) != 0) || s[k] == '_')) { ++k;
}
        TypeEntry e;
        e.name = s.substr(0, k);
        e.section = section;

        std::size_t c = s.find("//", k);
        if (c != std::string::npos) {
            std::string com = s.substr(c + 2);
            if (com.starts_with("##")) { parseParamSpec(com.substr(2), e.params, e.desc);
            } else if (com.starts_with('#')) { e.desc = trim(com.substr(1));
            } else { e.desc = trim(com);
}
        }
        if (afterEol) { ++hidden;
        } else { out.push_back(std::move(e));
}
    }
    return true;
}

// Verzeichnis einmal auflisten. Die .bhc bindet 14 Kopfdateien ein; je
// Einbindung neu zu durchsuchen hiess 14 Verzeichnisdurchlaeufe.
class HeaderIndex {
public:
    explicit HeaderIndex(const fs::path& dir) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            map_[lower(e.path().filename().string())] = e.path();
}
    }
    fs::path find(const std::string& want) const {
        auto it = map_.find(lower(want));
        return it == map_.end() ? fs::path{} : it->second;
    }
private:
    std::map<std::string, fs::path> map_;
};

} // namespace

bool loadCommandDb(const std::string& bhcPath, const std::string& includeDir,
                   CommandDb& out, std::vector<LoadDiag>& diag) {
    bool ok = false;
    std::vector<std::string> lines = readLines(bhcPath, &ok);
    if (!ok) {
        diag.push_back({bhcPath, 0, "Datei nicht lesbar"});
        return false;
    }
    stripBlockComments(lines);
    const HeaderIndex headers{includeDir};

    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string s = trim(lines[i]);
        if (s.empty()) { continue;
}

        // Typmenge:  <%s="NAME">
        if (s.size() > 4 && s[0] == '<' && s[1] == '%') {
            char kind = s[2];
            std::size_t q1 = s.find('"');
            std::size_t q2 = s.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) { continue;
}
            TypeSet ts;
            ts.kind = kind;
            ts.name = s.substr(q1 + 1, q2 - q1 - 1);

            std::size_t j = i + 1;
            while (j < lines.size() && lines[j].find('{') == std::string::npos) { ++j;
}
            ++j;
            for (; j < lines.size() && lines[j].find('}') == std::string::npos; ++j) {
                std::string e = trim(lines[j]);
                if (e.empty() || e.starts_with("//")) { continue;
}
                if (e.starts_with("#include")) {
                    std::size_t a = e.find('"');
                    std::size_t b = e.find('"', a + 1);
                    if (a == std::string::npos || b == std::string::npos) { continue;
}
                    std::string file = e.substr(a + 1, b - a - 1);
                    std::string en = trim(e.substr(b + 1));
                    fs::path p = headers.find(file);
                    if (p.empty()) {
                        diag.push_back({file, static_cast<int>(j) + 1,
                                        "Kopfdatei nicht gefunden (Typmenge " + ts.name + ")"});
                        continue;
                    }
                    int hidden = 0;
                    if (!parseEnum(p, en, ts.entries, hidden)) {
                        diag.push_back({file, static_cast<int>(j) + 1,
                                        "Enum " + en + " nicht gefunden"});
}
                    ts.hiddenAfterEol += hidden;
                    continue;
                }
                TypeEntry te;
                std::size_t c = e.find("//");
                std::string head = c == std::string::npos ? e : trim(e.substr(0, c));
                if (head.size() >= 2 && head.front() == '"' && head.back() == '"') {
                    head = head.substr(1, head.size() - 2);
}
                te.name = head;
                if (c != std::string::npos) {
                    std::string com = e.substr(c + 2);
                    if (com.starts_with("##")) { parseParamSpec(com.substr(2), te.params, te.desc);
                    } else if (com.starts_with('#')) { te.desc = trim(com.substr(1));
}
                }
                if (!te.name.empty()) { ts.entries.push_back(std::move(te));
}
            }
            out.typesets[ts.name] = std::move(ts);
            i = j;
            continue;
        }

        // Makro:  "name"//# beschreibung   gefolgt von {
        if (s[0] == '"') {
            std::size_t q2 = s.find('"', 1);
            if (q2 != std::string::npos && i + 1 < lines.size() &&
                trim(lines[i + 1]).starts_with('{')) {
                Macro m;
                m.name = s.substr(1, q2 - 1);
                const std::size_t hash = s.find("//#", q2);
                if (hash != std::string::npos) {
                    m.desc = trim(s.substr(hash + 3));
                }
                out.macros.push_back(m.name);

                // Den Rumpf mitnehmen. Ohne ihn kann man das Makro zwar
                // anzeigen, aber nicht einfuegen - und genau das war es
                // vorher: in der Ereignisliste stand es, ein Doppelklick tat
                // nichts.
                std::size_t j = i + 1;
                while (j < lines.size() && lines[j].find('{') == std::string::npos) { ++j; }
                ++j;
                for (; j < lines.size() && lines[j].find('}') == std::string::npos; ++j) {
                    const std::string e = trim(lines[j]);
                    if (e.empty() || e.starts_with("//")) { continue; }
                    const std::size_t op = e.find('(');
                    const std::size_t cl = e.rfind(')');
                    if (op == std::string::npos || cl == std::string::npos || cl < op) {
                        continue;
                    }
                    MacroLine ml;
                    ml.command = trim(e.substr(0, op));
                    std::string ignored;
                    parseParamSpec(e.substr(op + 1, cl - op - 1), ml.params, ignored);
                    if (!ml.command.empty()) {
                        m.body.push_back(std::move(ml));
                    }
                }
                out.macroBodies.push_back(std::move(m));
                i = j;
                continue;
            }
        }

        // Befehl:  [I_SET] set( %t="SET_TYPES", %s="DEFAULT" );//# text
        std::string icon;
        std::string rest = s;
        if (rest[0] == '[') {
            std::size_t e = rest.find(']');
            if (e == std::string::npos) { continue;
}
            icon = rest.substr(1, e - 1);
            rest = trim(rest.substr(e + 1));
        }
        // Zeilenkommentar zuerst abtrennen. Sonst findet rfind(')') die
        // Klammer im Text: "loop ( %d=-1 ) {} //# ... (-1 = forever)".
        std::string comment;
        {
            bool q = false;
            for (std::size_t k = 0; k + 1 < rest.size(); ++k) {
                if (rest[k] == '"') { q = !q;
}
                if (!q && rest[k] == '/' && rest[k + 1] == '/') {
                    comment = rest.substr(k + 2);
                    rest = trim(rest.substr(0, k));
                    break;
                }
            }
            if (comment.starts_with('#')) { comment = comment.substr(1);
}
        }

        std::size_t op = rest.find('(');
        if (op == std::string::npos) { continue;
}
        std::string name = trim(rest.substr(0, op));
        if (name.empty()) { continue;
}
        bool nameOk = true;
        for (char c : name) {
            if ((std::isalnum(static_cast<unsigned char>(c)) == 0) && c != '_') { nameOk = false;
}
}
        if (!nameOk || (std::islower(static_cast<unsigned char>(name[0])) == 0)) { continue;
}

        std::size_t cl = rest.rfind(')');
        if (cl == std::string::npos || cl < op) { continue;
}

        Command cmd;
        cmd.icon = icon;
        cmd.name = name;
        std::string tail = trim(rest.substr(cl + 1));
        cmd.block = tail.starts_with("{}");

        std::string desc;
        parseParamSpec(rest.substr(op + 1, cl - op - 1), cmd.params, desc);
        cmd.desc = trim(comment.empty() ? desc : comment);
        out.commands.push_back(std::move(cmd));
    }
    return true;
}

} // namespace bhed
