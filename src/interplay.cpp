// interplay.cpp - siehe interplay.h
#include "bhed/interplay.h"

#include <algorithm>
#include <cctype>

namespace bhed {
namespace {

std::string klein(const std::string& s) {
    std::string r = s;
    for (char& c : r) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return r;
}

// Der Wert eines Arguments.
//
// Arg::text steht laut script.h schon OHNE Anfuehrungszeichen - und zwar
// aus beiden Quellen, Textform wie uebersetzter Strom. Genau darauf kommt
// es hier an: sonst faende ein signal aus einer .txt sein waitsignal aus
// einer .ibi nie.
//
// Deshalb steht hier kein Abstreifen. Ich hatte zuerst eines eingebaut,
// bevor ich im Header nachgesehen habe - es haette nichts getan und den
// naechsten Leser glauben lassen, die Anfuehrungszeichen kaemen vor.
const std::string& wert(const Arg& a) { return a.text; }

void einmalig(std::vector<std::string>& liste, const std::string& s) {
    if (s.empty()) {
        return;
    }
    if (std::find(liste.begin(), liste.end(), s) == liste.end()) {
        liste.push_back(s);
    }
}

void gehe(const std::vector<Node>& nodes, ScriptFacts& out) {
    for (const Node& n : nodes) {
        const std::string name = klein(n.name);
        if (name == "signal" && !n.args.empty()) {
            einmalig(out.signalsSent, wert(n.args[0]));
        } else if (name == "waitsignal" && !n.args.empty()) {
            einmalig(out.signalsAwaited, wert(n.args[0]));
        } else if (name == "run" && !n.args.empty()) {
            einmalig(out.runs, wert(n.args[0]));
        } else if (name == "camera") {
            ++out.cameraCommands;
        }
        // Auch in Bloecke hinein: ein signal in einem if oder einer
        // Schleife ist genauso ein signal. Wer nur die oberste Ebene
        // durchginge, uebersaehe die meisten.
        gehe(n.children, out);
    }
}

}   // namespace

ScriptFacts scanScript(const Script& s) {
    ScriptFacts f;
    gehe(s.nodes, f);
    return f;
}

std::vector<SignalLink> linkSignals(const std::vector<NamedFacts>& scripts) {
    std::vector<SignalLink> out;

    auto hole = [&out](const std::string& name) -> SignalLink& {
        for (SignalLink& l : out) {
            if (l.name == name) {
                return l;
            }
        }
        out.push_back(SignalLink{name, {}, {}});
        return out.back();
    };

    for (const NamedFacts& nf : scripts) {
        for (const std::string& sig : nf.facts.signalsSent) {
            hole(sig).senders.push_back(nf.name);
        }
        for (const std::string& sig : nf.facts.signalsAwaited) {
            hole(sig).waiters.push_back(nf.name);
        }
    }

    std::sort(out.begin(), out.end(),
              [](const SignalLink& a, const SignalLink& b) {
                  return a.name < b.name;
              });
    return out;
}

std::vector<std::string> cameraScripts(const std::vector<NamedFacts>& scripts) {
    std::vector<std::string> out;
    for (const NamedFacts& nf : scripts) {
        if (nf.facts.cameraCommands > 0) {
            out.push_back(nf.name);
        }
    }
    return out;
}

}   // namespace bhed
