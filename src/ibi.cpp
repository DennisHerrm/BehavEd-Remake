// ibi.cpp
#include "bhed/ibi.h"
#include "bhed/gla.h"

#include <cstddef>
#include <cstdint>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <string>
#include <cstring>
#include <map>

namespace bhed {
namespace {

constexpr float kVersion = 1.57F;

void put32(std::string& o, std::int32_t v) {
    std::uint32_t u = 0;
    std::memcpy(&u, &v, 4);
    o += static_cast<char>(u & 0xFF);
    o += static_cast<char>((u >> 8) & 0xFF);
    o += static_cast<char>((u >> 16) & 0xFF);
    o += static_cast<char>((u >> 24) & 0xFF);
}

std::int32_t get32(const std::string& b, std::size_t p) {
    std::uint32_t u = static_cast<std::uint8_t>(b[p]) |
                      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[p + 1])) << 8) |
                      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[p + 2])) << 16) |
                      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[p + 3])) << 24);
    std::int32_t v = 0;
    std::memcpy(&v, &u, 4);
    return v;
}

std::string i32(std::int32_t v) {
    std::string s;
    put32(s, v);
    return s;
}

std::string f32(float v) {
    std::int32_t i = 0;
    std::memcpy(&i, &v, 4);
    return i32(i);
}

float asFloat(const std::string& d) {
    float v = 0;
    if (d.size() >= 4) {
        std::memcpy(&v, d.data(), 4);
    }
    return v;
}

std::int32_t asInt(const std::string& d) {
    std::int32_t v = 0;
    if (d.size() >= 4) {
        std::memcpy(&v, d.data(), 4);
    }
    return v;
}

// Blanke Bezeichner aus den %i-Typmengen tragen im .ibi ihre Zahl, nicht
// ihren Namen. Die Zahlen stehen in CIcarus (IcarusImplementation.h) hinter
// NUM_IDS und sind von DEvahebs Enums unabhaengig bestaetigt.
const std::map<std::string, std::int32_t>& identValues() {
    static const std::map<std::string, std::int32_t> m = {
        {"WAIT_COMPLETE", 51}, {"WAIT_TRIGGERED", 52},
        {"ANGLES", 53},        {"ORIGIN", 54},
        {"INSERT", 55},        {"FLUSH", 56},
        {"PAN", 57},           {"ZOOM", 58},  {"MOVE", 59},     {"FADE", 60},
        {"PATH", 61},          {"ENABLE", 62}, {"DISABLE", 63}, {"SHAKE", 64},
        {"ROLL", 65},          {"TRACK", 66},  {"DISTANCE", 67}, {"FOLLOW", 68},
        // DECLARE_TYPE benutzt die Tokenwerte selbst
        {"CHAR", TK_CHAR}, {"STRING", TK_STRING}, {"INT", TK_INT},
        {"FLOAT", TK_FLOAT}, {"IDENTIFIER", TK_IDENTIFIER}, {"VECTOR", TK_VECTOR},
    };
    return m;
}

const std::map<std::string, std::int32_t>& commandIds() {
    static const std::map<std::string, std::int32_t> m = {
        {"affect", ID_AFFECT}, {"sound", ID_SOUND},   {"move", ID_MOVE},
        {"rotate", ID_ROTATE}, {"wait", ID_WAIT},     {"set", ID_SET},
        {"loop", ID_LOOP},     {"print", ID_PRINT},   {"use", ID_USE},
        {"flush", ID_FLUSH},   {"run", ID_RUN},       {"kill", ID_KILL},
        {"remove", ID_REMOVE}, {"camera", ID_CAMERA}, {"if", ID_IF},
        {"else", ID_ELSE},     {"rem", ID_REM},       {"task", ID_TASK},
        {"do", ID_DO},         {"declare", ID_DECLARE}, {"free", ID_FREE},
        {"dowait", ID_DOWAIT}, {"signal", ID_SIGNAL},
        {"waitsignal", ID_WAITSIGNAL}, {"play", ID_PLAY},
    };
    return m;
}

std::string nameOfId(std::int32_t id) {
    for (const auto& [n, v] : commandIds()) {
        if (v == id) {
            return n;
        }
    }
    return {};
}


bool looksInt(const std::string& t) {
    if (t.empty()) {
        return false;
    }
    std::size_t i = (t[0] == '-' || t[0] == '+') ? 1 : 0;
    if (i >= t.size()) {
        return false;
    }
    for (; i < t.size(); ++i) {
        if (t[i] < '0' || t[i] > '9') {
            return false;
        }
    }
    return true;
}

// Eine Zahl so lesen wie Ravens IBIze - nicht wie atof.
//
// ICARUS' Tokenizer (JK2-GPL-Quelltext, code/icarus/Tokenizer.cpp,
// CTokenizer::HandleFloat) liest die Stellen vor dem Punkt als `long` und
// jede Nachkommastelle in float-Arithmetik:
//
//     float lower = 1.0;  float newValue = (float)value;
//     lower = lower / 10;  newValue = newValue + ((theByte - '0') * lower);
//
// Das trifft nicht immer die naechste float-Zahl: aus -675.587 wird
// -675.58704, atof gaebe -675.58698. Beim Byte-Vergleich aller 1510
// Raven-Skripte mit IBIze.exe waren das die letzten Abweichungen. Im Spiel
// ist es eine letzte Binaerstelle - aber eine .ibi soll so aussehen, wie
// IBIze sie schreibt. Alles, was nicht [+-]Ziffern[.Ziffern] ist
// (Exponent, ".5"), geht wie bisher an stof.
float toFloat(const std::string& t) {
    std::size_t i = 0;
    bool minus = false;
    if (i < t.size() && (t[i] == '-' || t[i] == '+')) {
        minus = t[i] == '-';
        ++i;
    }
    const std::size_t ziffernAb = i;
    long ganz = 0;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
        ganz = ganz * 10 + (t[i] - '0');
        ++i;
    }
    bool eigen = i > ziffernAb;
    float wert = static_cast<float>(ganz);
    if (eigen && i < t.size() && t[i] == '.') {
        ++i;
        float lower = 1.0F;
        while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
            lower = lower / 10;
            // Produkt und Summe genauer als float, erst das Ergebnis auf
            // float gerundet - so rechnete die x87-FPU, mit der IBIze 2003
            // gebaut wurde. Rein in float wich 1.993 im letzten Bit ab
            // (t2_rogue/findRacto), rein in double -675.587.
            wert = static_cast<float>(static_cast<double>(wert) +
                                      static_cast<double>(t[i] - '0') *
                                          static_cast<double>(lower));
            ++i;
        }
    }
    if (eigen && i == t.size()) {
        return minus ? -wert : wert;
    }
    try {
        return std::stof(t);
    } catch (...) {
        return 0.0F;
    }
}

// Ein Marker: Kennung mit vier Byte Inhalt. Gemessen an 1011 Raven-Dateien:
//   TK_VECTOR(14) -> 14.0     ID_GET(36) -> 36.0     ID_TAG(49) -> 49.0
//   ID_RANDOM(37) -> 16777216.0 (Q3_INFINITE)
//   Vergleiche (15..18) -> 0.0
IbiMember marker(std::int32_t id) {
    IbiMember m;
    m.id = id;
    float payload = static_cast<float>(id);
    if (id == ID_RANDOM) {
        payload = 16777216.0F;   // Q3_INFINITE
    } else if (id >= TK_GREATER_THAN && id <= TK_NOT) {
        payload = 0.0F;
    }
    m.data = f32(payload);
    return m;
}

IbiMember flt(float v) {
    IbiMember m;
    m.id = TK_FLOAT;
    m.data = f32(v);
    return m;
}

IbiMember str(const std::string& s) {
    IbiMember m;
    m.id = TK_STRING;
    m.data = s;
    m.data += '\0';
    return m;
}

// Zahlen aus einem Vektortext "1.0 2.0 3.0"
// Ein Vektor ist KEIN 12-Byte-Glied, sondern ein Marker mit vier Byte,
// gefolgt von drei einzelnen Floats. Gemessen: TK_VECTOR hat in allen 2793
// Vorkommen die Groesse 4.
void pushVector(std::vector<IbiMember>& out, const std::string& text) {
    float v[3] = {0, 0, 0};
    std::size_t p = 0;
    for (float& f : v) {
        while (p < text.size() && text[p] == ' ') { ++p; }
        const std::size_t a = p;
        while (p < text.size() && text[p] != ' ') { ++p; }
        f = toFloat(text.substr(a, p - a));
    }
    out.push_back(marker(TK_VECTOR));
    for (const float f : v) {
        out.push_back(flt(f));
    }
}

// --- Ausdruecke: get / random / tag ----------------------------------
// Sie werden zu FLACHEN Gliedern: Kennung, dann die Argumente.
// out hat absichtlich keine Vorbelegung: die Struktur wird ausschliesslich
// mit Klammerinitialisierung angelegt (ExprParser{text, 0, &ziel}). Ein
// Nullzeiger als Vorgabe waere eine Einladung, sie auch ohne Ziel zu bauen;
// so meldet es der Uebersetzer.
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct ExprParser {
    const std::string& t;
    std::size_t p = 0;
    std::vector<IbiMember>* out;
    bool ok = true;

    void skip() {
        while (p < t.size() && (t[p] == ' ' || t[p] == '\t')) {
            ++p;
        }
    }
    bool word(std::string& w) {
        skip();
        const std::size_t a = p;
        while (p < t.size() && ((std::isalnum(static_cast<unsigned char>(t[p])) != 0) || t[p] == '_')) {
            ++p;
        }
        w = t.substr(a, p - a);
        return !w.empty();
    }
    bool ch(char c) {
        skip();
        if (p < t.size() && t[p] == c) { ++p; return true; }
        return false;
    }
    bool quoted(std::string& s) {
        skip();
        if (p >= t.size() || t[p] != '"') {
            return false;
        }
        const std::size_t a = ++p;
        while (p < t.size() && t[p] != '"') {
            ++p;
        }
        s = t.substr(a, p - a);
        if (p < t.size()) { ++p; }
        return true;
    }

    // Ein Wert: Zahl, Text, Vektor, Bezeichner oder ein weiterer Aufruf.
    bool value() {
        skip();
        if (p < t.size() && t[p] == '<') {
            // $<0 0 0>$ - Vektor als Ausdruck, 125 Vorkommen bei Raven
            const std::size_t a = ++p;
            while (p < t.size() && t[p] != '>') {
                ++p;
            }
            pushVector(*out, t.substr(a, p - a));
            if (p < t.size()) { ++p; }
            return true;
        }
        if (p < t.size() && t[p] == '"') {
            std::string s;
            if (!quoted(s)) { return false; }
            out->push_back(str(s));
            return true;
        }
        // Zahlen zuerst: word() bricht am Punkt ab, "0.500000" waere sonst
        // als "0" gelesen worden und der Rest haette den Aufruf gesprengt.
        if (p < t.size() && (std::isdigit(static_cast<unsigned char>(t[p])) != 0 ||
                             t[p] == '-' || t[p] == '+' || t[p] == '.')) {
            const std::size_t a = p;
            if (t[p] == '-' || t[p] == '+') { ++p; }
            while (p < t.size() && (std::isdigit(static_cast<unsigned char>(t[p])) != 0 || t[p] == '.')) {
                ++p;
            }
            IbiMember m;
            m.id = TK_FLOAT;
            m.data = f32(toFloat(t.substr(a, p - a)));
            out->push_back(m);
            return true;
        }
        const std::size_t save = p;
        std::string w;
        if (!word(w)) {
            return false;
        }
        skip();
        if (p < t.size() && t[p] == '(') {
            p = save;
            return call();
        }
        if (looksInt(w) || (w.find('.') != std::string::npos) ||
            (!w.empty() && (w[0] == '-' || w[0] == '+'))) {
            out->push_back(flt(toFloat(w)));
            return true;
        }
        auto it = identValues().find(w);
        if (it != identValues().end()) {
            // Aufzaehlungswerte stehen als float auf der Platte. Beleg aus
            // dem Engine-Quelltext, Sequencer.cpp ParseAffect:
            //     type = (int)(*(float *) block->GetMemberData( 1 ));
            out->push_back(flt(static_cast<float>(it->second)));
        } else {
            IbiMember m;
            m.id = TK_IDENTIFIER;
            m.data = w;
            m.data += '\0';
            out->push_back(m);
        }
        return true;
    }

    bool call() {
        std::string name;
        if (!word(name)) {
            return false;
        }
        if (!ch('(')) {
            return false;
        }
        std::int32_t id = 0;
        if (name == "get") { id = ID_GET; }
        else if (name == "random") { id = ID_RANDOM; }
        else if (name == "tag") { id = ID_TAG; }
        else { return false; }
        out->push_back(marker(id));
        bool first = true;
        while (true) {
            skip();
            if (ch(')')) {
                break;
            }
            if (!first && !ch(',')) {
                return false;
            }
            first = false;
            if (!value()) {
                return false;
            }
        }
        return true;
    }
};

bool encodeArg(const Arg& a, const CommandDb& db, std::vector<IbiMember>& out,
               std::vector<Diag>& diag) {
    switch (a.kind) {
        case Arg::Kind::String:
            out.push_back(str(a.text));
            return true;
        case Arg::Kind::Vector:
            pushVector(out, a.text);
            return true;
        case Arg::Kind::Number:
            // IMMER float. TK_INT kommt in Ravens 1011 Dateien kein einziges
            // Mal vor - auch "wait ( 2000 )" steht dort als 2000.0f.
            out.push_back(flt(toFloat(a.text)));
            return true;
        case Arg::Kind::Ident: {
            // Zwei Wege, beide gemessen:
            //   AFFECT_TYPE, CAMERA_COMMANDS, TAG_TYPE, DECLARE_TYPE stehen
            //   als float mit ihrem Engine-Wert da (FLUSH -> 56.0).
            //   CHANNELS steht als TEXT da - alle 1111 TK_IDENTIFIER in
            //   Ravens Dateien gehoeren zu sound-Bloecken.
            auto it = identValues().find(a.text);
            if (it != identValues().end()) {
                out.push_back(flt(static_cast<float>(it->second)));
                return true;
            }
            IbiMember m;
            m.id = TK_IDENTIFIER;
            m.data = a.text;
            m.data += '\0';
            out.push_back(m);
            (void)db;
            (void)diag;
            return true;
        }
        case Arg::Kind::Expr: {
            std::string inner = a.text;
            {   // "$ = $" - BehavEd schreibt die Vergleiche mit Leerzeichen
                std::size_t x = 0;
                std::size_t y = inner.size();
                while (x < y && (inner[x] == ' ' || inner[x] == '\t')) { ++x; }
                while (y > x && (inner[y - 1] == ' ' || inner[y - 1] == '\t')) { --y; }
                inner = inner.substr(x, y - x);
            }
            // Vergleiche, die es in ICARUS NICHT gibt. IBIze.exe bricht dort
            // ab ("if : error parsing second expression"); wer aus anderen
            // Sprachen kommt, schreibt sie aber leicht. Eine Meldung, die
            // sagt, was stattdessen gilt, statt "nicht uebersetzbar".
            {
                std::string ohne;
                for (char c : inner) {
                    if (c != ' ' && c != '\t') { ohne.push_back(c); }
                }
                if (ohne == "!=" || ohne == "==" || ohne == "<=" || ohne == ">=" || ohne == "<>") {
                    diag.push_back({0, "ICARUS kennt keinen Vergleich \"" + inner +
                                           "\" - erlaubt sind = < > und ! (! heisst ungleich)",
                                    "C004", inner});
                    return false;
                }
            }
            if (inner == "=" || inner == "<" || inner == ">" || inner == "!") {
                // Auch Vergleiche sind Marker mit vier Byte Inhalt (0.0).
                // Ohne die Daten fehlen vier Byte, und ab dort ist die ganze
                // Datei verschoben.
                out.push_back(marker(inner == "=" ? TK_EQUALS
                                   : inner == "<" ? TK_LESS_THAN
                                   : inner == ">" ? TK_GREATER_THAN
                                                  : TK_NOT));
                return true;
            }
            // Ein Glied ab `ab`: erst als Aufruf, sonst als reiner Wert
            // (z. B. $2$). Liefert die Stelle dahinter, npos bei Fehler.
            auto glied = [&](std::size_t ab) -> std::size_t {
                const std::size_t before = out.size();
                ExprParser ep{inner, ab, &out, true};
                if (ep.call()) { return ep.p; }
                out.resize(before);
                ExprParser vp{inner, ab, &out, true};
                if (vp.value()) { return vp.p; }
                out.resize(before);
                return std::string::npos;
            };
            std::size_t p = glied(0);
            if (p == std::string::npos) {
                diag.push_back({0, "Ausdruck nicht uebersetzbar: " + inner, "C001", inner});
                return false;
            }
            // --- Die Einfeld-Form des if: $random( 0, 1 ) > 0.800000$ -----
            //
            // BehavEd schrieb frueher die ganze Bedingung in EIN Feld. IBIze
            // macht daraus drei Glieder - Wert, Vergleich, Wert - genau wie
            // aus der Dreifeld-Form. Hier wurde nach dem ersten Glied
            // aufgehoert: `> 0.800000` fiel weg, das if pruefte nur noch
            // "random(0,1)". Gefunden beim Byte-Vergleich mit IBIze.exe
            // (r5_wander, pressure_on und 39 weitere Raven-Skripte).
            while (p < inner.size() && (inner[p] == ' ' || inner[p] == '\t')) { ++p; }
            if (p < inner.size() &&
                (inner[p] == '=' || inner[p] == '<' || inner[p] == '>' || inner[p] == '!')) {
                const char op = inner[p];
                out.push_back(marker(op == '=' ? TK_EQUALS
                                   : op == '<' ? TK_LESS_THAN
                                   : op == '>' ? TK_GREATER_THAN
                                               : TK_NOT));
                p = glied(p + 1);
                if (p == std::string::npos) {
                    diag.push_back({0, "Ausdruck nicht uebersetzbar: " + inner, "C001", inner});
                    return false;
                }
                while (p < inner.size() && (inner[p] == ' ' || inner[p] == '\t')) { ++p; }
            }
            if (p < inner.size()) {
                diag.push_back({0, "Ausdruck nicht vollstaendig uebersetzt: " + inner, "C002", inner});
            }
            return true;
        }
    }
    return false;
}

void emit(const std::vector<Node>& ns, const CommandDb& db,
          std::vector<IbiBlock>& out, std::vector<Diag>& diag, const Path& vorne = {}) {
    for (std::size_t idx = 0; idx < ns.size(); ++idx) {
        const Node& n = ns[idx];
        // Meldungen, die bei diesem Knoten entstehen, bekommen seinen Weg -
        // die Oberflaeche waehlt ihn dann an (vorher stand nur "(0)").
        const std::size_t meldungenVorher = diag.size();
        Path hier = vorne;
        hier.push_back(idx);
        struct Wegmarke {
            std::vector<Diag>& d;
            std::size_t ab;
            const Path& weg;
            ~Wegmarke() {
                for (std::size_t k = ab; k < d.size(); ++k) {
                    if (d[k].path.empty()) { d[k].path = weg; }
                }
            }
        } wegmarke{diag, meldungenVorher, hier};
        if (n.kind != Node::Kind::Command) {
            continue;   // Leerzeilen, Kommentare und Makromarker sind Quelltext
        }
        // `rem` ist ein Kommentar und kommt NICHT in die .ibi.
        //
        // shank, 27.09.: mit behaved uebersetztes cin2_jedi.IBI -> im Spiel
        // "Invalid block ID". Der Sequencer der Engine kennt ID_REM nicht
        // (OpenJK code/icarus/Sequencer.cpp, Route(): keine case-Marke,
        // default -> "'%d' : invalid block ID", SEQ_FAILED), und die ganze
        // Sequenz bricht ab. Ravens IBIze laesst rem weg - dasselbe Skript
        // durch IBIze.exe aus dem SDK: dieselben Bloecke ohne die fuenf rem.
        // ID_REM bleibt in der Namensliste, damit das Einlesen fremder .ibi
        // mit rem-Bloecken weiter geht.
        if (n.name == "rem") {
            continue;
        }
        // `dowait ( "x" )` ist in der .bhc als Kurzform erklaert: "shorthand
        // form of: do("taskname"); wait("taskname")". IBIze schreibt genau
        // das - zwei Bloecke, ID_DO und ID_WAIT. Einen DOWAIT-Block kennt
        // der Sequencer nicht (Route(): keine case-Marke) - derselbe Abbruch
        // wie bei rem. Gefunden beim Byte-Vergleich aller 1510 Raven-Skripte
        // mit IBIze.exe: 508 wichen ab, alle mit dowait.
        if (n.name == "dowait") {
            for (const std::int32_t id : {ID_DO, ID_WAIT}) {
                IbiBlock b;
                b.id = id;
                for (const Arg& a : n.args) {
                    (void)encodeArg(a, db, b.members, diag);
                }
                out.push_back(std::move(b));
            }
            continue;
        }
        auto it = commandIds().find(n.name);
        if (it == commandIds().end()) {
            diag.push_back({0, "kein .ibi-Gegenstueck fuer " + n.name, "C003", n.name});
            continue;
        }
        IbiBlock b;
        b.id = it->second;
        for (const Arg& a : n.args) {
            (void)encodeArg(a, db, b.members, diag);
        }
        out.push_back(std::move(b));
        if (n.hasBlock) {
            emit(n.children, db, out, diag, hier);
            IbiBlock e;
            e.id = ID_BLOCK_END;
            out.push_back(e);
        }
    }
}

} // namespace

bool readIbi(const std::string& bytes, std::vector<IbiBlock>& out,
             std::vector<Diag>& diag) {
    out.clear();
    if (bytes.size() < 8 || bytes.compare(0, 3, "IBI") != 0) {
        diag.push_back({0, "keine IBI-Datei"});
        return false;
    }
    float ver = 0;
    std::memcpy(&ver, bytes.data() + 4, 4);
    if (ver != kVersion) {
        diag.push_back({0, "unerwartete Fassung"});
        return false;
    }
    // Die Kennung und die Fassung koennen heil aussehen und die Datei
    // trotzdem kaputt sein: in "IBI " und in 1.57f steht kein Nullbyte.
    // Erst weiter hinten stolpert der Leser dann ueber eine Gliedlaenge,
    // die nicht passt - und meldet etwas, das nach einem eigenen Fehler
    // klingt. Deshalb hier, VOR dem Zerlegen, einmal hinsehen.
    if (looksTextMangled(bytes)) {
        diag.push_back({0, "Nullbytes durch Leerzeichen ersetzt - die Datei "
                           "wurde im Text-Modus uebertragen und ist kaputt"});
        return false;
    }
    std::size_t p = 8;
    while (p + 9 <= bytes.size()) {
        IbiBlock b;
        b.id = get32(bytes, p);
        p += 4;
        const std::int32_t n = get32(bytes, p);
        p += 4;
        b.flags = static_cast<std::uint8_t>(bytes[p]);
        p += 1;
        if (n < 0) {
            diag.push_back({0, "negative Gliederzahl"});
            return false;
        }
        for (std::int32_t i = 0; i < n; ++i) {
            if (p + 8 > bytes.size()) {
                diag.push_back({0, "Datei endet mitten im Glied"});
                return false;
            }
            IbiMember m;
            m.id = get32(bytes, p);
            p += 4;
            const std::int32_t size = get32(bytes, p);
            p += 4;
            if (size < 0 || p + static_cast<std::size_t>(size) > bytes.size()) {
                diag.push_back({0, "Gliedlaenge liegt ausserhalb der Datei"});
                return false;
            }
            m.data = bytes.substr(p, static_cast<std::size_t>(size));
            p += static_cast<std::size_t>(size);
            b.members.push_back(std::move(m));
        }
        out.push_back(std::move(b));
    }
    return true;
}

std::string writeIbi(const std::vector<IbiBlock>& blocks) {
    std::string o;
    o += "IBI";
    o += '\0';
    o += f32(kVersion);
    for (const IbiBlock& b : blocks) {
        put32(o, b.id);
        put32(o, static_cast<std::int32_t>(b.members.size()));
        o += static_cast<char>(b.flags);
        for (const IbiMember& m : b.members) {
            put32(o, m.id);
            put32(o, static_cast<std::int32_t>(m.data.size()));
            o += m.data;
        }
    }
    return o;
}

bool compile(const Script& s, const CommandDb& db, std::vector<IbiBlock>& out,
             std::vector<Diag>& diag) {
    out.clear();
    const std::size_t before = diag.size();
    emit(s.nodes, db, out, diag);
    return diag.size() == before;
}

namespace {

// Name zu einem Aufzaehlungswert, oder leer.
std::string identName(float value) {
    const auto v = static_cast<std::int32_t>(value);
    for (const auto& [n, x] : identValues()) {
        if (x == v) {
            return n;
        }
    }
    return {};
}

// Kuerzeste Schreibweise, die den Wert BITGENAU zurueckliefert.
//
// %.3f reicht nicht: Ravens Dateien enthalten Werte, die sich mit drei
// Nachkommastellen nicht wiederherstellen lassen. Beim Rundlauf ueber 1011
// Dateien waren 122 davon betroffen - immer nur im letzten Bit, aber die
// Datei ist dann eben nicht mehr bytegleich.
//
// BehavEd selbst schreibt %.3f in die .icarus. Fuer die RUECKUEBERSETZUNG
// zaehlt aber, dass sich daraus wieder dieselbe .ibi bauen laesst; sonst
// waere sie als Vorlage wertlos.
std::string fmtFloat(float v) {
    char buf[40];
    // Erst der uebliche Weg. Nur wenn der den Wert nicht trifft, mehr
    // Stellen - so bleiben die allermeisten Zahlen lesbar.
    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(v));
    if (toFloat(buf) == v) {
        return buf;
    }
    for (int digits = 4; digits <= 9; ++digits) {
        std::snprintf(buf, sizeof(buf), "%.*f", digits, static_cast<double>(v));
        if (toFloat(buf) == v) {
            return buf;
        }
    }
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    return buf;
}

// Ein Argument aus der Gliederfolge lesen und dabei so viele Glieder
// verbrauchen, wie es braucht. Marker ziehen ihre Argumente nach:
//     get    -> Typ (float) + Name (Text)
//     tag    -> Name (Text) + Typ (float)
//     random -> zwei Floats
//     Vektor -> drei Floats
std::string readExprText(const std::vector<IbiMember>& ms, std::size_t& i);

// Ob an dieser Argumentstelle ein Aufzaehlungswert steht. Ohne diese Frage
// wird aus  camera ( ZOOM, 53.640, 0 )  ein  camera ( ZOOM, ANGLES, 0 ) -
// weil 53 zufaellig TYPE_ANGLES ist. Ein Float darf nur dort zum Namen
// werden, wo die Signatur eine %i-Typmenge vorsieht.
bool expectsEnum(const CommandDb& db, std::int32_t blockId, std::size_t argIndex) {
    const std::string name = nameOfId(blockId);
    if (name.empty()) {
        return false;
    }
    for (const Command* c : db.overloads(name)) {
        if (argIndex >= c->params.size()) {
            continue;
        }
        const Param& p = c->params[argIndex];
        if (p.kind != Param::Kind::TypeSet) {
            continue;
        }
        const TypeSet* ts = db.typeset(p.typeset);
        if (ts != nullptr && ts->kind == 'i') {
            return true;
        }
    }
    return false;
}

std::string valueText(const std::vector<IbiMember>& ms, std::size_t& i) {
    if (i >= ms.size()) {
        return {};
    }
    const IbiMember& m = ms[i];
    if (m.id == ID_GET || m.id == ID_TAG || m.id == ID_RANDOM || m.id == TK_VECTOR) {
        return readExprText(ms, i);
    }
    ++i;
    switch (m.id) {
        case TK_STRING:
            return "\"" + (m.data.empty() ? "" : m.data.substr(0, m.data.size() - 1)) + "\"";
        case TK_IDENTIFIER:
            return m.data.empty() ? "" : m.data.substr(0, m.data.size() - 1);
        case TK_FLOAT: {
            const float f = asFloat(m.data);
            const std::string name = identName(f);
            return name.empty() ? fmtFloat(f) : name;
        }
        default:
            return fmtFloat(asFloat(m.data));
    }
}

std::string readExprText(const std::vector<IbiMember>& ms, std::size_t& i) {
    const std::int32_t id = ms[i].id;
    ++i;
    if (id == TK_VECTOR) {
        std::string out = "<";
        for (int k = 0; k < 3; ++k) {
            out += " ";
            out += (i < ms.size()) ? fmtFloat(asFloat(ms[i].data)) : "0.000";
            ++i;
        }
        return out + " >";
    }
    if (id == ID_GET) {
        // Typ steht als float da, Name als Text.
        std::string type = "FLOAT";
        if (i < ms.size()) {
            const std::string n = identName(asFloat(ms[i].data));
            if (!n.empty()) { type = n; }
            ++i;
        }
        const std::string name = (i < ms.size()) ? valueText(ms, i) : "\"\"";
        return "get( " + type + ", " + name + " )";
    }
    if (id == ID_TAG) {
        const std::string name = (i < ms.size()) ? valueText(ms, i) : "\"\"";
        std::string type = "ORIGIN";
        if (i < ms.size()) {
            const std::string n = identName(asFloat(ms[i].data));
            if (!n.empty()) { type = n; }
            ++i;
        }
        return "tag( " + name + ", " + type + " )";
    }
    // random
    const std::string a = (i < ms.size()) ? fmtFloat(asFloat(ms[i].data)) : "0.000";
    ++i;
    const std::string b = (i < ms.size()) ? fmtFloat(asFloat(ms[i].data)) : "0.000";
    ++i;
    return "random( " + a + ", " + b + " )";
}

}  // namespace

bool decompile(const std::vector<IbiBlock>& blocks, const CommandDb& db,
               Script& out, std::vector<Diag>& diag) {
    out = Script{};
    std::vector<std::vector<Node>*> stack{&out.nodes};

    for (const IbiBlock& b : blocks) {
        if (b.id == ID_BLOCK_END) {
            if (stack.size() > 1) {
                stack.pop_back();
            } else {
                diag.push_back({0, "blockEnd ohne offenen Block"});
            }
            continue;
        }
        Node n;
        n.name = nameOfId(b.id);
        if (n.name.empty()) {
            diag.push_back({0, "unbekannte Blockkennung " + std::to_string(b.id)});
            continue;
        }

        std::size_t i = 0;
        while (i < b.members.size()) {
            const IbiMember& m = b.members[i];
            Arg a;
            if (m.id == ID_GET || m.id == ID_TAG || m.id == ID_RANDOM) {
                a.kind = Arg::Kind::Expr;
                a.text = readExprText(b.members, i);
            } else if (m.id == TK_VECTOR) {
                const std::string v = readExprText(b.members, i);
                a.kind = Arg::Kind::Vector;
                // "< a b c >" -> "a b c"
                a.text = (v.size() > 4) ? v.substr(2, v.size() - 4) : v;
            } else if (m.id >= TK_GREATER_THAN && m.id <= TK_NOT) {
                a.kind = Arg::Kind::Expr;
                a.text = (m.id == TK_EQUALS) ? "="
                       : (m.id == TK_LESS_THAN) ? "<"
                       : (m.id == TK_GREATER_THAN) ? ">" : "!";
                ++i;
            } else if (m.id == TK_STRING) {
                a.kind = Arg::Kind::String;
                a.text = m.data.empty() ? "" : m.data.substr(0, m.data.size() - 1);
                ++i;
            } else if (m.id == TK_IDENTIFIER) {
                a.kind = Arg::Kind::Ident;
                a.text = m.data.empty() ? "" : m.data.substr(0, m.data.size() - 1);
                ++i;
            } else if (m.id == TK_FLOAT || m.id == TK_INT) {
                const float f = (m.id == TK_FLOAT) ? asFloat(m.data)
                                                   : static_cast<float>(asInt(m.data));
                const std::string name =
                    expectsEnum(db, b.id, n.args.size()) ? identName(f) : std::string{};
                if (!name.empty()) {
                    a.kind = Arg::Kind::Ident;
                    a.text = name;
                } else {
                    a.kind = Arg::Kind::Number;
                    a.text = fmtFloat(f);
                }
                ++i;
            } else {
                a.kind = Arg::Kind::Ident;
                a.text = "<id" + std::to_string(m.id) + ">";
                ++i;
            }
            n.args.push_back(std::move(a));
        }

        const bool opensBlock = (b.id == ID_AFFECT || b.id == ID_TASK ||
                                 b.id == ID_LOOP || b.id == ID_IF || b.id == ID_ELSE);
        n.hasBlock = opensBlock;
        stack.back()->push_back(std::move(n));
        if (opensBlock) {
            stack.push_back(&stack.back()->back().children);
        }
    }
    return true;
}

} // namespace bhed
