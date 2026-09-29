// interplaytest.cpp - was Skripte miteinander verbindet
//
// Braucht keinen Korpus: die Skripte stehen hier als Text und werden
// gelesen. So laeuft die Probe auch dort, wo das Spiel nicht installiert
// ist.
#include "bhed/interplay.h"
#include "bhed/script.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int fails = 0;

void expect(const char* was, bool ok) {
    std::printf("  %s   %s\n", ok ? "ok  " : "FEHL", was);
    if (!ok) {
        ++fails;
    }
}

bhed::Script lies(const std::string& text) {
    bhed::Script s;
    std::vector<bhed::Diag> d;
    (void)bhed::readScript(text, s, d);
    return s;
}

bool enthaelt(const std::vector<std::string>& v, const std::string& s) {
    for (const std::string& x : v) {
        if (x == s) {
            return true;
        }
    }
    return false;
}

}   // namespace

int main() {
    // --- Ein einzelnes Skript durchgehen --------------------------------
    {
        const bhed::Script s = lies(
            "signal ( \"ani1_walk\" );\n"
            "waitsignal ( \"gunray_look\" );\n"
            "run ( \"scripts/common/switch_on\" );\n"
            "camera ( ENABLE );\n"
            "camera ( MOVE, <0.0 0.0 0.0>, 1000.000 );\n");
        const bhed::ScriptFacts f = bhed::scanScript(s);

        expect("ein gesendetes Signal gefunden",
               enthaelt(f.signalsSent, "ani1_walk"));
        expect("ein erwartetes Signal gefunden",
               enthaelt(f.signalsAwaited, "gunray_look"));
        expect("ein run gefunden",
               enthaelt(f.runs, "scripts/common/switch_on"));
        std::printf("     Kamerabefehle: %d\n", f.cameraCommands);
        expect("beide Kamerabefehle gezaehlt", f.cameraCommands == 2);
        expect("gesendet und erwartet werden nicht verwechselt",
               !enthaelt(f.signalsSent, "gunray_look"));
    }

    // --- Auch INNERHALB von Bloecken ------------------------------------
    //
    // Der haeufigste Fehler waere, nur die oberste Ebene durchzugehen: die
    // meisten Signale stehen in einem affect- oder if-Block.
    {
        const bhed::Script s = lies(
            "affect ( \"bd4\", FLUSH )\n"
            "{\n"
            "\tsignal ( \"tief\" );\n"
            "\tcamera ( PAN, <0.0 0.0 0.0>, <0.0 0.0 0.0>, 0.000 );\n"
            "}\n");
        const bhed::ScriptFacts f = bhed::scanScript(s);
        expect("ein Signal in einem Block wird gefunden",
               enthaelt(f.signalsSent, "tief"));
        expect("ein Kamerabefehl in einem Block auch",
               f.cameraCommands == 1);
    }

    // --- Gross- und Kleinschreibung -------------------------------------
    //
    // In den Quellen steht mal SIGNAL, mal signal; das Spiel nimmt beides.
    {
        const bhed::Script s = lies("SIGNAL ( \"laut\" );\n");
        expect("SIGNAL wird wie signal gelesen",
               enthaelt(bhed::scanScript(s).signalsSent, "laut"));
    }

    // --- Mehrere Skripte gegenueberstellen ------------------------------
    {
        std::vector<bhed::NamedFacts> alle;
        alle.push_back({"intro.ibi",
                        bhed::scanScript(lies("signal ( \"los\" );\n"
                                              "waitsignal ( \"fertig\" );\n"
                                              "camera ( ENABLE );\n"))});
        alle.push_back({"wache.ibi",
                        bhed::scanScript(lies("waitsignal ( \"los\" );\n"
                                              "signal ( \"fertig\" );\n"))});
        alle.push_back({"stumm.ibi",
                        bhed::scanScript(lies("waitsignal ( \"niemand\" );\n"))});

        const std::vector<bhed::SignalLink> l = bhed::linkSignals(alle);
        std::printf("     %zu Signale ueber 3 Skripte\n", l.size());
        expect("drei verschiedene Signale", l.size() == 3);
        expect("alphabetisch geordnet",
               l.size() == 3 && l[0].name < l[1].name && l[1].name < l[2].name);

        for (const bhed::SignalLink& x : l) {
            if (x.name == "los") {
                expect("\"los\" hat Sender und Empfaenger",
                       enthaelt(x.senders, "intro.ibi") &&
                           enthaelt(x.waiters, "wache.ibi"));
            }
            if (x.name == "niemand") {
                // Der interessante Fall: das Skript wartet ewig.
                expect("\"niemand\" hat keinen Sender", x.senders.empty());
                expect("und genau einen Wartenden", x.waiters.size() == 1);
            }
        }

        // --- Die Kamera: eine einzige, global -----------------------------
        //
        // In cg_camera.cpp steht ein einziges camera_t client_camera. Zwei
        // Skripte, die sie gleichzeitig anfassen, ueberschreiben einander.
        const std::vector<std::string> kam = bhed::cameraScripts(alle);
        expect("nur das intro fasst die Kamera an",
               kam.size() == 1 && kam[0] == "intro.ibi");

        alle.push_back({"zweite_kamera.ibi",
                        bhed::scanScript(lies("camera ( DISABLE );\n"))});
        expect("ein zweites Kameraskript faellt auf",
               bhed::cameraScripts(alle).size() == 2);
    }

    // --- Teilen sich Missionen eine Karte? ------------------------------
    //
    // Gefragt: "sind das alle Missionen, oder teilen sich davon welche
    // was?" Bei Movie Duels ist das Teilen der Regelfall - md_afif2_jedi
    // und md_afif2_sith sind dieselbe .bsp, einmal aus Sicht des Jedi und
    // einmal des Sith.
    //
    // Verglichen wird die KARTENDATEI, nicht der Name der Mission. Am
    // Namen zu raten ("beide fangen mit md_afif2 an") ginge bei
    // md_po_jedi / md_po_prologue_jedi schief: gleicher Anfang, andere
    // Karte.
    {
        struct Miss { std::string name; std::string karte; };
        const std::vector<Miss> alle2{
            {"md_afif2_jedi", "maps/md_afif2.bsp"},
            {"md_afif2_sith", "maps/md_afif2.bsp"},
            {"md_am_sith", "maps/md_am_sith.bsp"},
            {"md_po_jedi", "maps/md_po.bsp"},
            {"md_po_prologue_jedi", "maps/md_po_prologue.bsp"},
        };
        auto teilen = [&alle2](const std::string& karte) {
            int n = 0;
            for (const Miss& m : alle2) {
                if (m.karte == karte) { ++n; }
            }
            return n;
        };

        expect("zwei Missionen auf derselben Karte werden gezaehlt",
               teilen("maps/md_afif2.bsp") == 2);
        expect("eine allein bleibt allein",
               teilen("maps/md_am_sith.bsp") == 1);
        expect("gleicher Namensanfang heisst nicht gleiche Karte",
               teilen("maps/md_po.bsp") == 1 &&
                   teilen("maps/md_po_prologue.bsp") == 1);
    }

    std::printf("\n%s (%d Fehlschlaege)\n",
                fails != 0 ? "FEHLGESCHLAGEN" : "alle Zusammenspielproben bestanden",
                fails);
    return fails != 0 ? 1 : 0;
}
