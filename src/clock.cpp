// clock.cpp - siehe bhed/clock.h
#include "bhed/clock.h"

#include "bhed/num.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace bhed {
namespace {

// Den Rumpf eines Blocks durchgehen. Fuer das Einsammeln der Signale reicht
// dieselbe Aufteilung wie in scene.cpp: loop, if und else laufen an ihrer
// Stelle, ein task erst beim passenden do.
void walkForSignals(const std::vector<Node>& kids, double start,
                    SignalTimes& out, int depth,
                    std::map<std::string, const Node*>& tasks);

void collectFrom(const std::vector<Node>& nodes, double start, SignalTimes& out,
                 int depth) {
    std::map<std::string, const Node*> tasks;
    walkForSignals(nodes, start, out, depth, tasks);
}

void walkForSignals(const std::vector<Node>& kids, double start,
                    SignalTimes& out, int depth,
                    std::map<std::string, const Node*>& tasks) {
    if (depth > 8) {
        return;   // ein task, das sich selbst aufruft
    }
    double t = start;
    for (const Node& n : kids) {
        if (n.kind != Node::Kind::Command) {
            continue;
        }
        // Ein affect uebergibt seinen Block einem EIGENEN Ablauf, der ab
        // hier parallel laeuft - die eigene Uhr geht dabei nicht weiter.
        // Belegt in CSequencer::Affect (Sequencer.cpp).
        if (n.name == "affect" && n.hasBlock) {
            collectFrom(n.children, t, out, depth + 1);
            continue;
        }
        if (n.name == "task" && !n.args.empty()) {
            tasks[n.args[0].text] = &n;
            continue;
        }
        if (n.name == "do" && !n.args.empty()) {
            const auto it = tasks.find(n.args[0].text);
            if (it != tasks.end()) {
                walkForSignals(it->second->children, t, out, depth + 1, tasks);
            }
            continue;
        }
        if (n.hasBlock &&
            (n.name == "loop" || n.name == "if" || n.name == "else")) {
            walkForSignals(n.children, t, out, depth + 1, tasks);
            continue;
        }
        if (n.name == "signal" && !n.args.empty()) {
            const std::string& nm = n.args[0].text;
            const auto had = out.find(nm);
            if (had == out.end() || t < had->second) {
                out[nm] = t;
            }
            continue;
        }
        if (n.name == "wait" && !n.args.empty() &&
            n.args[0].kind != Arg::Kind::String) {
            t += readMs(n.args[0].text);
            continue;
        }
        // waitsignal wird im Sammeldurchgang uebergangen: sonst haenge die
        // Sammlung von sich selbst ab.
    }
}

}  // namespace

double readMs(const std::string& text) {
    // Ohne Ausnahmen - siehe bhed/num.h. Frueher stand hier ein
    // try/catch um std::stod; das ist fuer eine ERWARTETE Abweichung
    // (ein Ausdruck statt einer Zahl) der falsche Weg.
    double zahl = 0.0;
    if (parseDouble(text, zahl)) {
        return zahl;
    }
    {
        // $random( a, b )$ - die Mitte. Warum, steht in clock.h.
        const std::size_t at = text.find("random");
        if (at == std::string::npos) {
            return 0.0;
        }
        const std::size_t open = text.find('(', at);
        if (open == std::string::npos) {
            return 0.0;
        }
        const std::size_t comma = text.find(',', open);
        if (comma == std::string::npos) {
            return 0.0;
        }
        const std::size_t close = text.find(')', comma);
        if (close == std::string::npos) {
            return 0.0;
        }
        double lo = 0.0;
        double hi = 0.0;
        if (!parseDouble(text.substr(open + 1, comma - open - 1), lo) ||
            !parseDouble(text.substr(comma + 1, close - comma - 1), hi)) {
            return 0.0;
        }
        return (lo + hi) * 0.5;
    }
}

SignalTimes collectSignals(const Script& s) {
    SignalTimes out;
    collectFrom(s.nodes, 0.0, out, 0);
    return out;
}

double advance(const Node& n, double now, double ownEndMs,
               const SignalTimes& signals) {
    if (n.kind != Node::Kind::Command || n.args.empty()) {
        return now;
    }
    if (n.name == "wait") {
        if (n.args[0].kind != Arg::Kind::String) {
            return now + readMs(n.args[0].text);
        }
        // wait ( "name" ) wartet, bis das genannte Objekt seine Bewegung
        // beendet hat. Naeherung: bis zum Ende dessen, was dieser Ablauf
        // zuletzt angestossen hat. Das trifft das haeufige Muster
        // move(...); wait("ich"). Steht dort ein anderer Name, ist es
        // hoechstens zu frueh, nie zu spaet.
        return std::max(now, ownEndMs);
    }
    if (n.name == "waitsignal") {
        const auto it = signals.find(n.args[0].text);
        if (it != signals.end()) {
            return std::max(now, it->second);
        }
        // Ein Signal, das nie faellt, darf nicht haengen lassen.
        return now;
    }
    return now;
}

}  // namespace bhed
