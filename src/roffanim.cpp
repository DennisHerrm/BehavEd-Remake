// roffanim.cpp - eigene ROFF-Bahnen im Skript (siehe bhed/roffanim.h)
#include "bhed/roffanim.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace bhed {
namespace {

constexpr const char* kKopf = "(BehavEd) roff ";

std::string zahl(double v) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.3f", v);
    // "120.500" -> "120.5", "0.000" -> "0": die Zeile bleibt kurz.
    std::string s = b;
    while (!s.empty() && s.back() == '0') { s.pop_back(); }
    if (!s.empty() && s.back() == '.') { s.pop_back(); }
    if (s == "-0") { s = "0"; }
    return s;
}

std::string zitat(const std::string& t) {
    std::string o = "\"";
    for (char c : t) {
        if (c == '"' || c == '\\') { o.push_back('\\'); }
        // Ein Zeilenende in einer Notiz zerbraeche den Kommentar.
        if (c == '\n' || c == '\r') {
            o += "\\n";
            continue;
        }
        o.push_back(c);
    }
    return o + "\"";
}

// Woerter der Zeile: "..." mit \-Escapes, '|' fuer sich, sonst bis zum
// Leerzeichen. zitiert[i] sagt, ob das Wort in Anfuehrungszeichen stand.
bool zerlege(const std::string& z, std::size_t ab, std::vector<std::string>& w, std::vector<char>& zitiert) {
    std::size_t i = ab;
    while (i < z.size()) {
        const char c = z[i];
        if (c == ' ' || c == '\t') {
            ++i;
            continue;
        }
        if (c == '|') {
            w.emplace_back("|");
            zitiert.push_back(0);
            ++i;
            continue;
        }
        if (c == '"') {
            std::string t;
            ++i;
            bool zu = false;
            while (i < z.size()) {
                if (z[i] == '\\' && i + 1 < z.size()) {
                    t.push_back(z[i + 1] == 'n' ? '\n' : z[i + 1]);
                    i += 2;
                    continue;
                }
                if (z[i] == '"') {
                    zu = true;
                    ++i;
                    break;
                }
                t.push_back(z[i]);
                ++i;
            }
            if (!zu) { return false; }
            w.push_back(t);
            zitiert.push_back(1);
            continue;
        }
        std::string t;
        while (i < z.size() && z[i] != ' ' && z[i] != '\t' && z[i] != '|') {
            t.push_back(z[i]);
            ++i;
        }
        w.push_back(t);
        zitiert.push_back(0);
    }
    return true;
}

bool istZahl(const std::string& t, double& v) {
    if (t.empty()) { return false; }
    char* ende = nullptr;
    v = std::strtod(t.c_str(), &ende);
    return ende != nullptr && *ende == '\0';
}

std::string normal(const std::string& d) {
    std::string s = d;
    for (char& c : s) {
        c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool istPlayRoff(const Node& n) {
    if (n.kind != Node::Kind::Command || n.name != "play" || n.args.size() < 2) { return false; }
    std::string t = n.args[0].text;
    for (char& c : t) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
    return t == "PLAY_ROFF";
}

Node kommentar(const std::string& text, std::size_t tiefe) {
    Node n;
    n.kind = Node::Kind::LineComment;
    n.raw = std::string(tiefe, '\t') + text;
    return n;
}

void sammle(const std::vector<Node>& ns, Path& weg, std::vector<RoffMarkeFund>& aus) {
    for (std::size_t i = 0; i < ns.size(); ++i) {
        weg.push_back(i);
        const Node& n = ns[i];
        if (n.kind == Node::Kind::LineComment) {
            RoffAnim a;
            if (leseRoffMarke(n.raw, a)) { aus.push_back(RoffMarkeFund{weg, std::move(a)}); }
        } else if (!n.children.empty()) {
            sammle(n.children, weg, aus);
        }
        weg.pop_back();
    }
}

// Die Geschwisterliste, in der `p` liegt (nullptr, wenn der Weg nicht passt).
std::vector<Node>* geschwister(Script& s, const Path& p) {
    if (p.empty()) { return nullptr; }
    std::vector<Node>* liste = &s.nodes;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (p[i] >= liste->size()) { return nullptr; }
        liste = &(*liste)[p[i]].children;
    }
    return p.back() < liste->size() ? liste : nullptr;
}

}  // namespace

bool gleicheRoff(const std::string& a, const std::string& b) { return normal(a) == normal(b); }

std::string roffMarke(const RoffAnim& a) {
    std::string z = std::string("//") + kKopf + zitat(a.datei) + (a.weich ? " weich " : " linear ") +
                    std::to_string(a.bildMs > 0 ? a.bildMs : 50);
    for (const RoffSchluessel& k : a.schluessel) {
        z += " | " + zahl(k.ms);
        for (float v : k.ort) { z += " " + zahl(static_cast<double>(v)); }
        for (float v : k.winkel) { z += " " + zahl(static_cast<double>(v)); }
    }
    for (const RoffNotiz& n : a.notizen) { z += " | note " + zahl(n.ms) + " " + zitat(n.text); }
    return z;
}

bool leseRoffMarke(const std::string& zeile, RoffAnim& out) {
    std::size_t i = 0;
    while (i < zeile.size() && (zeile[i] == ' ' || zeile[i] == '\t')) { ++i; }
    const std::string kopf = std::string("//") + kKopf;
    if (zeile.compare(i, kopf.size(), kopf) != 0) { return false; }
    std::vector<std::string> w;
    std::vector<char> zitiert;
    if (!zerlege(zeile, i + kopf.size(), w, zitiert) || w.size() < 3 || zitiert[0] == 0) { return false; }
    RoffAnim a;
    a.datei = w[0];
    if (w[1] == "weich") {
        a.weich = true;
    } else if (w[1] == "linear") {
        a.weich = false;
    } else {
        return false;
    }
    double v = 0.0;
    if (!istZahl(w[2], v) || v < 1.0) { return false; }
    a.bildMs = static_cast<int>(v);
    // Gruppen zwischen den '|'.
    std::size_t at = 3;
    while (at < w.size()) {
        if (w[at] != "|") { return false; }
        ++at;
        std::size_t ende = at;
        while (ende < w.size() && !(w[ende] == "|" && zitiert[ende] == 0)) { ++ende; }
        const std::size_t n = ende - at;
        if (n == 3 && w[at] == "note" && zitiert[at + 2] != 0) {
            RoffNotiz no;
            if (!istZahl(w[at + 1], no.ms)) { return false; }
            no.text = w[at + 2];
            a.notizen.push_back(std::move(no));
        } else if (n == 7) {
            RoffSchluessel k;
            double z[7];
            for (std::size_t j = 0; j < 7; ++j) {
                if (!istZahl(w[at + j], z[j])) { return false; }
            }
            k.ms = z[0];
            for (int c = 0; c < 3; ++c) {
                k.ort[c] = static_cast<float>(z[1 + c]);
                k.winkel[c] = static_cast<float>(z[4 + c]);
            }
            a.schluessel.push_back(k);
        } else {
            return false;
        }
        at = ende;
    }
    out = std::move(a);
    return true;
}

Roff roffAusAnim(const RoffAnim& a) { return baueRoff(a.schluessel, a.weich, a.bildMs, a.notizen); }

std::vector<Node> baueRoffBlock(const std::string& entity, double playMs, const RoffAnim& a) {
    std::string text = roffMarke(a) + "\n";
    text += "affect ( \"" + entity + "\", /*@AFFECT_TYPE*/ FLUSH )\n{\n";
    if (playMs > 0.5) {
        char z[64];
        std::snprintf(z, sizeof(z), "\twait ( %.3f );\n", playMs);
        text += z;
    }
    text += "\tplay ( /*@PLAY_TYPES*/ \"PLAY_ROFF\", \"" + a.datei + "\" );\n}\n";
    Script s;
    std::vector<Diag> diag;
    if (!readScript(text, s, diag)) { return {}; }
    // Die Marke wortgetreu (der Leser koennte an einem langen Kommentar
    // nichts aendern - sicher ist sicher).
    if (!s.nodes.empty() && s.nodes.front().kind == Node::Kind::LineComment) { s.nodes.front().raw = roffMarke(a); }
    return s.nodes;
}

std::optional<std::size_t> findeRoffBlock(const Script& s, const std::string& entity) {
    for (std::size_t i = 0; i + 1 < s.nodes.size(); ++i) {
        const Node& n = s.nodes[i];
        RoffAnim a;
        if (n.kind != Node::Kind::LineComment || !leseRoffMarke(n.raw, a)) { continue; }
        const Node& aff = s.nodes[i + 1];
        if (aff.name != "affect" || aff.args.empty() || aff.args[0].text != entity) { continue; }
        for (const Node& c : aff.children) {
            if (istPlayRoff(c) && gleicheRoff(c.args[1].text, a.datei)) { return i; }
        }
    }
    return std::nullopt;
}

bool leseRoffBlock(const Script& s, std::size_t index, RoffAnim& out, double& playMs) {
    if (index + 1 >= s.nodes.size() || !leseRoffMarke(s.nodes[index].raw, out)) { return false; }
    playMs = 0.0;
    for (const Node& c : s.nodes[index + 1].children) {
        if (c.kind != Node::Kind::Command) { continue; }
        if (c.name == "wait" && !c.args.empty()) {
            playMs += std::atof(c.args[0].text.c_str());
        } else if (istPlayRoff(c)) {
            return true;
        }
    }
    return false;
}

Script mitRoffBlock(const Script& s, const std::string& entity, double playMs, const RoffAnim* a) {
    Script neu = s;
    std::size_t at = 0;
    if (const auto alt = findeRoffBlock(neu, entity)) {
        at = *alt;
        neu.nodes.erase(neu.nodes.begin() + static_cast<std::ptrdiff_t>(at),
                        neu.nodes.begin() + static_cast<std::ptrdiff_t>(at + 2));
    }
    if (a != nullptr) {
        const std::vector<Node> block = baueRoffBlock(entity, playMs, *a);
        neu.nodes.insert(neu.nodes.begin() + static_cast<std::ptrdiff_t>(at), block.begin(), block.end());
    }
    return neu;
}

std::optional<std::size_t> findeRoffBlockNachDatei(const Script& s, const std::string& datei) {
    for (std::size_t i = 0; i + 1 < s.nodes.size(); ++i) {
        RoffAnim a;
        if (s.nodes[i].kind != Node::Kind::LineComment || !leseRoffMarke(s.nodes[i].raw, a) || !gleicheRoff(a.datei, datei)) {
            continue;
        }
        const Node& aff = s.nodes[i + 1];
        if (aff.name != "affect") { continue; }
        for (const Node& c : aff.children) {
            if (istPlayRoff(c) && gleicheRoff(c.args[1].text, a.datei)) { return i; }
        }
    }
    return std::nullopt;
}

Script mitRoffBlockBei(const Script& s, std::size_t index, const std::string& entity, double playMs, const RoffAnim* a) {
    Script neu = s;
    if (index + 1 >= neu.nodes.size()) { return mitRoffBlock(s, entity, playMs, a); }
    neu.nodes.erase(neu.nodes.begin() + static_cast<std::ptrdiff_t>(index),
                    neu.nodes.begin() + static_cast<std::ptrdiff_t>(index + 2));
    if (a != nullptr) {
        const std::vector<Node> block = baueRoffBlock(entity, playMs, *a);
        neu.nodes.insert(neu.nodes.begin() + static_cast<std::ptrdiff_t>(index), block.begin(), block.end());
    }
    return neu;
}

std::vector<RoffMarkeFund> alleRoffMarken(const Script& s) {
    std::vector<RoffMarkeFund> aus;
    Path weg;
    sammle(s.nodes, weg, aus);
    return aus;
}

std::optional<RoffMarkeFund> findeRoffMarke(const Script& s, const std::string& datei) {
    for (RoffMarkeFund& f : alleRoffMarken(s)) {
        if (gleicheRoff(f.anim.datei, datei)) { return std::move(f); }
    }
    return std::nullopt;
}

Script mitRoffMarkeVorPlay(const Script& s, const Path& playPfad, const RoffAnim& a) {
    Script neu = s;
    std::vector<Node>* liste = geschwister(neu, playPfad);
    if (liste == nullptr || !istPlayRoff((*liste)[playPfad.back()])) { return neu; }
    std::size_t i = playPfad.back();
    const std::size_t tiefe = playPfad.size() - 1;
    RoffAnim davor;
    if (i > 0 && (*liste)[i - 1].kind == Node::Kind::LineComment && leseRoffMarke((*liste)[i - 1].raw, davor)) {
        (*liste)[i - 1].raw = std::string(tiefe, '\t') + roffMarke(a);
    } else {
        liste->insert(liste->begin() + static_cast<std::ptrdiff_t>(i), kommentar(roffMarke(a), tiefe));
        ++i;
    }
    (*liste)[i].args[1].text = a.datei;
    return neu;
}

}  // namespace bhed
