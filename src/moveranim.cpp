// moveranim.cpp - Schluesselstellen <-> affect-Block (siehe bhed/moveranim.h)
#include "bhed/moveranim.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace bhed {
namespace {

std::string vek(const std::array<float, 3>& v) {
    char b[96];
    std::snprintf(b, sizeof(b), "< %.3f %.3f %.3f >", static_cast<double>(v[0]), static_cast<double>(v[1]),
                  static_cast<double>(v[2]));
    return b;
}

bool leseVek(const std::string& t, std::array<float, 3>& v) {
    std::string s;
    for (char c : t) { s.push_back((c == '<' || c == '>') ? ' ' : c); }
    std::istringstream is(s);
    return static_cast<bool>(is >> v[0] >> v[1] >> v[2]);
}

double zahl(const std::string& t) { return std::atof(t.c_str()); }

}  // namespace

std::string animMarke(const std::string& name) { return "//(BehavEd) animation \"" + name + "\""; }

std::vector<Node> baueAnimBlock(const std::string& name, std::vector<AnimSchluessel> schluessel) {
    if (schluessel.empty()) { return {}; }
    std::stable_sort(schluessel.begin(), schluessel.end(),
                     [](const AnimSchluessel& a, const AnimSchluessel& b) { return a.ms < b.ms; });
    std::string text = animMarke(name) + "\n";
    text += "affect ( \"" + name + "\", /*@AFFECT_TYPE*/ FLUSH )\n{\n";
    char z[256];
    const AnimSchluessel& erst = schluessel.front();
    if (erst.ms > 0.5) {
        std::snprintf(z, sizeof(z), "\twait ( %.3f );\n", erst.ms);
        text += z;
    }
    text += "\tmove ( " + vek(erst.ort) + ", " + vek(erst.winkel) + ", 0.000 );\n";
    for (std::size_t i = 1; i < schluessel.size(); ++i) {
        const double d = std::max(0.0, schluessel[i].ms - schluessel[i - 1].ms);
        std::snprintf(z, sizeof(z), "%.3f", d);
        text += "\tmove ( " + vek(schluessel[i].ort) + ", " + vek(schluessel[i].winkel) + ", " + z + " );\n";
        if (d > 0.5) { text += std::string("\twait ( ") + z + " );\n"; }
    }
    text += "}\n";
    Script s;
    std::vector<Diag> diag;
    if (!readScript(text, s, diag)) { return {}; }
    return s.nodes;
}

std::optional<std::size_t> findeAnimBlock(const Script& s, const std::string& name) {
    const std::string marke = animMarke(name);
    for (std::size_t i = 0; i + 1 < s.nodes.size(); ++i) {
        const Node& n = s.nodes[i];
        if (n.kind == Node::Kind::LineComment && n.raw.find(marke.substr(2)) != std::string::npos &&
            s.nodes[i + 1].name == "affect") {
            return i;
        }
    }
    return std::nullopt;
}

std::vector<AnimSchluessel> leseAnimBlock(const Script& s, std::size_t index) {
    std::vector<AnimSchluessel> aus;
    if (index + 1 >= s.nodes.size()) { return aus; }
    const Node& aff = s.nodes[index + 1];
    double t = 0.0;
    for (const Node& c : aff.children) {
        if (c.kind != Node::Kind::Command) { continue; }
        if (c.name == "wait" && !c.args.empty()) {
            t += zahl(c.args[0].text);
        } else if (c.name == "move" && c.args.size() >= 3) {
            AnimSchluessel k;
            if (!leseVek(c.args[0].text, k.ort)) { continue; }
            (void)leseVek(c.args[1].text, k.winkel);
            const double d = zahl(c.args[2].text);
            // Der Schluessel ist dort, wo die Fahrt ANKOMMT: Start + Dauer.
            // (Die Zeit selbst laeuft mit dem folgenden wait weiter.)
            k.ms = t + d;
            aus.push_back(k);
        }
    }
    return aus;
}

Script mitAnimBlock(const Script& s, const std::string& name, const std::vector<AnimSchluessel>& schluessel) {
    Script neu = s;
    const std::optional<std::size_t> alt = findeAnimBlock(neu, name);
    std::size_t at = 0;
    if (alt) {
        at = *alt;
        neu.nodes.erase(neu.nodes.begin() + static_cast<std::ptrdiff_t>(at),
                        neu.nodes.begin() + static_cast<std::ptrdiff_t>(at + 2));
    }
    const std::vector<Node> block = baueAnimBlock(name, schluessel);
    neu.nodes.insert(neu.nodes.begin() + static_cast<std::ptrdiff_t>(at), block.begin(), block.end());
    return neu;
}

}  // namespace bhed
