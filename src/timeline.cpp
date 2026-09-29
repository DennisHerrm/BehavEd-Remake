#include <cctype>
#include "bhed/timeline.h"

#include "bhed/clock.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace bhed {
namespace {

double toMs(const std::string& text, const TimelineOptions& opt, bool* uncertain) {
    // Ein Ausdruck: random( a, b ) hat keinen festen Wert.
    const std::size_t r = text.find("random");
    if (r != std::string::npos) {
        if (uncertain != nullptr) {
            *uncertain = true;
        }
        const std::size_t open = text.find('(', r);
        const std::size_t comma = text.find(',', open);
        const std::size_t close = text.find(')', comma);
        if (open == std::string::npos || comma == std::string::npos ||
            close == std::string::npos) {
            return 0.0;
        }
        try {
            const double a = std::stod(text.substr(open + 1, comma - open - 1));
            const double b = std::stod(text.substr(comma + 1, close - comma - 1));
            return opt.randomAsMiddle ? (a + b) / 2.0 : a;
        } catch (...) {
            return 0.0;
        }
    }
    try {
        return std::stod(text);
    } catch (...) {
        if (uncertain != nullptr) {
            *uncertain = true;
        }
        return 0.0;
    }
}

std::string joinArgs(const Node& n) {
    std::string o = n.name;
    o += " (";
    for (std::size_t i = 0; i < n.args.size(); ++i) {
        o += (i != 0) ? ", " : " ";
        o += n.args[i].text;
    }
    o += " )";
    return o;
}

// Der Zustand eines laufenden Ablaufs.
struct Runner {
    std::size_t track = 0;
    double now = 0.0;
    // Wann endet, was dieser Ablauf zuletzt angestossen hat? Fuer
    // wait ( "name" ) - siehe bhed/clock.h.
    double lastEndMs = 0.0;
};

class Builder {
public:
    // signals_ kommt von aussen: eingesammelt wird EINMAL je Skript, in
    // buildTimeline. Sonst liefe der Sammeldurchgang je affect-Block neu.
    SignalTimes signals_;

    Builder(const CommandDb& db, const TimelineOptions& opt, Timeline& out)
        : db_(db), opt_(opt), out_(out) {}

    void run(const std::vector<Node>& nodes, Runner& r, Path& prefix, int depth) {
        if (depth > 32) {
            note(prefix, "zu tief verschachtelt, hier abgebrochen");
            return;
        }
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            prefix.push_back(i);
            step(nodes[i], r, prefix, depth);
            prefix.pop_back();
        }
    }

private:
    void note(const Path& p, const std::string& what) {
        out_.notes.push_back({static_cast<int>(p.empty() ? 0 : p.back()), what});
    }

    void add(Runner& r, TimelineEvent e) {
        out_.tracks[r.track].events.push_back(std::move(e));
    }

    std::size_t newTrack(const std::string& entity, double startMs) {
        // Laeuft schon eine Spur fuer diese Entity, wird sie weiterbenutzt -
        // im Spiel hat jede Entity genau EINEN Ablauf.
        for (std::size_t i = 0; i < out_.tracks.size(); ++i) {
            if (out_.tracks[i].entity == entity) {
                return i;
            }
        }
        TimelineTrack t;
        t.entity = entity;
        t.startMs = startMs;
        out_.tracks.push_back(std::move(t));
        return out_.tracks.size() - 1;
    }

    void step(const Node& n, Runner& r, Path& prefix, int depth) {
        if (n.kind != Node::Kind::Command) {
            return;   // Kommentare, Leerzeilen, Makromarker
        }

        TimelineEvent e;
        e.path = prefix;
        e.label = joinArgs(n);
        e.startMs = r.now;
        e.endMs = r.now;

        const std::string& c = n.name;

        if (c == "affect") {
            // Der Block wandert in den Ablauf der Ziel-Entity und laeuft
            // ab JETZT parallel. Der eigene Ablauf laeuft ohne Pause weiter.
            const std::string target = n.args.empty() ? std::string("?")
                                                      : n.args[0].text;
            Runner sub;
            sub.track = newTrack(target, r.now);
            sub.now = r.now;
            e.kind = TimelineEvent::Kind::Other;
            add(r, e);
            run(n.children, sub, prefix, depth + 1);
            return;
        }

        if (c == "waitsignal") {
            // Frueher gar nicht behandelt - der Befehl stand als Punkt auf
            // der Leiste, hielt sie aber nicht an. Die Figuren warteten,
            // die Leiste nicht.
            const double bis = advance(n, r.now, r.lastEndMs, signals_);
            if (bis > r.now) {
                e.endMs = bis;
            } else {
                note(prefix, "waitsignal ohne passendes signal - uebergangen");
            }
            add(r, e);
            r.now = e.endMs;
            return;
        }

        if (c == "wait") {
            // wait ( 3000 ) haelt an; wait ( "signalname" ) wartet auf ein
            // Signal, dessen Zeitpunkt hier nicht feststeht.
            const bool bySignal = !n.args.empty() &&
                                  n.args[0].kind == Arg::Kind::String;
            if (bySignal) {
                // Die gemeinsame Uhr weiss es besser als eine Schaetzung:
                // sie spult bis zum Ende dessen vor, was dieser Ablauf
                // zuletzt angestossen hat.
                note(prefix, "wait auf ein Signal - bis zum Ende des eigenen "
                             "Zugs angenommen");
                e.endMs = advance(n, r.now, r.lastEndMs, signals_);
            } else {
                bool uncertain = false;
                const double ms = n.args.empty()
                                      ? 0.0
                                      : toMs(n.args[0].text, opt_, &uncertain);
                if (uncertain) {
                    note(prefix, "wait mit random - Mitte genommen");
                }
                e.endMs = r.now + ms;
                if (ms >= opt_.parkingWaitMs) {
                    // Kein Warten mehr, sondern ein Parken. Der Zeitpunkt
                    // davor ist das praktische Ende.
                    note(prefix, "sehr langes wait (" +
                                 std::to_string(static_cast<long>(ms)) +
                                 " ms) - das Skript parkt hier, die Sequenz "
                                 "ist vorher zu Ende");
                    parked_ = parked_ > 0.0 ? std::min(parked_, r.now) : r.now;
                }
            }
            e.kind = TimelineEvent::Kind::Wait;
            add(r, e);
            r.now = e.endMs;
            return;
        }

        if (c == "camera") {
            // Die Dauer ist bei allen Kameraarten das LETZTE Feld.
            // Belegt an CTaskManager::Camera: PAN, MOVE, ZOOM, ROLL, FADE
            // und SHAKE lesen ihre Zeit zuletzt.
            e.kind = TimelineEvent::Kind::Camera;
            if (n.args.size() >= 2) {
                bool uncertain = false;
                e.endMs = r.now + toMs(n.args.back().text, opt_, &uncertain);
            }
            add(r, e);
            // Eine Kamerafahrt haelt den Ablauf NICHT an. Wer warten will,
            // schreibt ein wait dahinter - so steht es in jedem Skript.
            return;
        }

        if (c == "move" || c == "rotate") {
            e.kind = TimelineEvent::Kind::Move;
            if (!n.args.empty()) {
                bool uncertain = false;
                e.endMs = r.now + toMs(n.args.back().text, opt_, &uncertain);
            }
            add(r, e);
            return;
        }

        if (c == "sound") {
            e.kind = TimelineEvent::Kind::Sound;
            if (n.args.size() >= 2) {
                e.sound = n.args[1].text;
                e.kanal = n.args[0].text;   // CHAN_VOICE, CHAN_AUTO ...
            }
            // Die Laenge steht in der Datei, nicht im Skript. Sie wird
            // spaeter nachgetragen, wenn der Klang geladen ist.
            add(r, e);
            return;
        }

        if (c == "use") {
            e.kind = TimelineEvent::Kind::Use;
            if (!n.args.empty()) {
                e.target = n.args[0].text;
            }
            add(r, e);
            return;
        }

        if (c == "set") {
            e.kind = TimelineEvent::Kind::Set;
            // SET_SABERACTIVE bekommt eine eigene Art - siehe Kind::Saber.
            if (n.args.size() >= 2) {
                std::string k = n.args[0].text;
                for (char& ch : k) {
                    ch = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(ch)));
                }
                if (k == "set_saberactive") {
                    e.kind = TimelineEvent::Kind::Saber;
                    e.on = (n.args[1].text == "true");
                }
            }
            add(r, e);
            return;
        }

        if (c == "loop") {
            bool uncertain = false;
            const double count = n.args.empty() ? 0.0
                                                : toMs(n.args[0].text, opt_, &uncertain);
            const int times = static_cast<int>(count);
            add(r, e);
            if (times < 0) {
                note(prefix, "Endlosschleife - einmal gezeigt");
                run(n.children, r, prefix, depth + 1);
                return;
            }
            if (times > opt_.maxLoopExpand) {
                note(prefix, "Schleife ueber " + std::to_string(times) +
                             " Durchlaeufe - nur " +
                             std::to_string(opt_.maxLoopExpand) + " gezeigt");
            }
            for (int k = 0; k < std::min(times, opt_.maxLoopExpand); ++k) {
                run(n.children, r, prefix, depth + 1);
            }
            return;
        }

        if (c == "if" || c == "else") {
            // Beide Zweige laufen hier durch: welcher im Spiel greift, haengt
            // von Werten ab, die es hier nicht gibt. Das steht als Anmerkung
            // dabei, damit niemand die Zeit fuer bare Muenze nimmt.
            note(prefix, "Verzweigung - beide Zweige in der Leiste");
            add(r, e);
            run(n.children, r, prefix, depth + 1);
            return;
        }

        if (c == "task") {
            // Ein task wird nur DEFINIERT, nicht ausgefuehrt. Erst do
            // startet ihn.
            add(r, e);
            return;
        }

        add(r, e);
    }

    const CommandDb& db_;
    const TimelineOptions& opt_;
    Timeline& out_;

public:
    double parked_ = 0.0;
};

}  // namespace

Timeline buildTimeline(const Script& s, const CommandDb& db,
                       const TimelineOptions& opt) {
    Timeline out;
    out.tracks.push_back(TimelineTrack{});   // Spur 0: das Skript selbst

    Builder b(db, opt, out);
    // Einmal je Skript einsammeln, dann steht die Antwort auf jedes
    // waitsignal fest - dieselbe wie in camtrack.cpp und scene.cpp.
    b.signals_ = collectSignals(s);
    Runner r;
    Path prefix;
    b.run(s.nodes, r, prefix, 0);

    for (const TimelineTrack& t : out.tracks) {
        for (const TimelineEvent& e : t.events) {
            out.durationMs = std::max(out.durationMs, e.endMs);
        }
    }

    // Das praktische Ende: bis zum ersten Parken, aber nicht vor dem letzten
    // Ereignis, das VOR dem Parken beginnt - sonst schnitte man eine
    // laufende Kamerafahrt ab.
    out.practicalEndMs = out.durationMs;
    if (b.parked_ > 0.0) {
        double end = b.parked_;
        for (const TimelineTrack& t : out.tracks) {
            for (const TimelineEvent& e : t.events) {
                if (e.startMs <= b.parked_ + 0.5 && e.endMs < opt.parkingWaitMs * 10.0) {
                    end = std::max(end, e.endMs);
                }
            }
        }
        out.practicalEndMs = end;
    }
    return out;
}

}  // namespace bhed
