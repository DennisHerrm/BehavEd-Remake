// ablauftest.cpp - Proben fuer den ICARUS-Nachbau (bhed/ablauf.h)
//
// Ohne Argumente: feste Faelle, jeder aus einer Stelle im Engine-Quelltext.
// Mit Argumenten ein Werkzeug:
//
//   ablauftest <skriptordner> <startpfad> [spielordner...]
//
// fuehrt scripts/<startpfad>.txt aus dem Ordner aus (dekompilierte Skripte)
// und schreibt das flache Skript samt Zeiten. Die Karte wird ueber die
// Skriptverweise in den .bsp der Spielordner gesucht.
#include "bhed/ablauf.h"
#include "bhed/bsp.h"
#include "bhed/pk3.h"
#include "bhed/script.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace bhed;

namespace {

int fehler = 0;

void pruefe(bool ok, const char* was) {
    if (!ok) {
        std::printf("FEHLER: %s\n", was);
        ++fehler;
    } else {
        std::printf("ok: %s\n", was);
    }
}

Script lies(const std::string& text) {
    Script s;
    std::vector<Diag> d;
    (void)readScript(text, s, d);
    return s;
}

// Die Zeit, zu der der n-te Befehl namens `name` (mit erstem Argument
// `arg`, falls nicht leer) in der Spur `spur` liegt ("" = oben).
double zeitVon(const Ablauf& a, const std::string& spur, const std::string& name, const std::string& arg) {
    const auto such = [&](const std::vector<Node>& ns) -> double {
        double t = 0.0;
        for (const Node& n : ns) {
            if (n.name == "wait") {
                t += std::atof(n.args[0].text.c_str());
                continue;
            }
            if (n.name == name && (arg.empty() || (!n.args.empty() && n.args.back().text == arg) ||
                                   (!n.args.empty() && n.args[0].text == arg))) {
                return t;
            }
        }
        return -1.0;
    };
    if (spur.empty()) {
        return such(a.flach.nodes);
    }
    for (const Node& n : a.flach.nodes) {
        if (n.name == "affect" && !n.args.empty() && n.args[0].text == spur) {
            return such(n.children);
        }
    }
    return -1.0;
}

void feste() {
    AblaufWelt w;
    // 1. wait ( 1000 ) dauert 1050 ms: stempel + d < jetzt (TaskManager.cpp:1100)
    {
        const Script s = lies("wait ( 1000.000 );\ncamera ( ENABLE );\nwait ( 100.000 );\ncamera ( DISABLE );\n");
        const Ablauf a = simuliereAblauf(s, "", w);
        pruefe(std::fabs(zeitVon(a, "", "camera", "ENABLE") - 1050.0) < 0.5, "wait 1000 -> 1050 ms (Takt 50)");
        pruefe(std::fabs(zeitVon(a, "", "camera", "DISABLE") - 1200.0) < 0.5, "wait 100 -> 150 ms");
    }
    // 2. do laeuft eingereiht: die Kamera im Task kommt, der Aufrufer wartet
    //    auf die waits im Task (CheckDo setzt m_curSequence).
    {
        const Script s = lies(
            "task ( \"t\" )\n{\n\twait ( 1000.000 );\n\tcamera ( ENABLE );\n}\n"
            "do ( \"t\" );\ncamera ( DISABLE );\n");
        const Ablauf a = simuliereAblauf(s, "", w);
        pruefe(std::fabs(zeitVon(a, "", "camera", "ENABLE") - 1050.0) < 0.5, "Kamera im Task laeuft");
        pruefe(std::fabs(zeitVon(a, "", "camera", "DISABLE") - 1050.0) < 0.5, "do haelt den Aufrufer bis zum Task-Ende");
    }
    // 3. affect laeuft nebenher: das wait im Block haelt oben nichts an.
    {
        const Script s = lies(
            "affect ( \"a\", FLUSH )\n{\n\twait ( 2000.000 );\n\tset ( \"SET_ANIM_BOTH\", \"BOTH_STAND1\" );\n}\n"
            "wait ( 500.000 );\ncamera ( ENABLE );\n");
        const Ablauf a = simuliereAblauf(s, "", w);
        pruefe(std::fabs(zeitVon(a, "", "camera", "ENABLE") - 550.0) < 0.5, "affect blockiert den Aufrufer nicht");
        pruefe(std::fabs(zeitVon(a, "a", "set", "BOTH_STAND1") - 2050.0) < 0.5, "affect-Block laeuft eigenstaendig");
    }
    // 4. dowait wartet auf die Stimme (TID_CHAN_VOICE)
    {
        AblaufWelt w2;
        w2.klangDauer = [](const std::string&) { return 2345.0; };
        const Script s = lies(
            "affect ( \"a\", FLUSH )\n{\n\ttask ( \"sag\" )\n\t{\n\t\tsound ( CHAN_VOICE, \"sound/x.mp3\" );\n\t}\n"
            "\tdowait ( \"sag\" );\n\tset ( \"SET_ANIM_BOTH\", \"BOTH_STAND1\" );\n}\n");
        const Ablauf a = simuliereAblauf(s, "", w2);
        const double t = zeitVon(a, "a", "set", "BOTH_STAND1");
        pruefe(t >= 2345.0 && t <= 2400.0, "dowait wartet auf CHAN_VOICE");
    }
    // 5. signal / waitsignal ueber Entities
    {
        const Script s = lies(
            "affect ( \"a\", FLUSH )\n{\n\twaitsignal ( \"go\" );\n\tset ( \"SET_ANIM_BOTH\", \"BOTH_STAND1\" );\n}\n"
            "wait ( 3000.000 );\nsignal ( \"go\" );\n");
        const Ablauf a = simuliereAblauf(s, "", w);
        const double t = zeitVon(a, "a", "set", "BOTH_STAND1");
        pruefe(t >= 3050.0 && t <= 3100.0, "waitsignal wartet auf das Signal");
    }
    // 6. loop ( 3 ) dreimal, loop ( -1 ) bis zur Grenze
    {
        const Script s = lies("loop ( 3.000 )\n{\n\twait ( 100.000 );\n\tprint ( \"x\" );\n}\n");
        const Ablauf a = simuliereAblauf(s, "", w);
        int n = 0;
        for (const Node& k : a.flach.nodes) { n += (k.name == "print") ? 1 : 0; }
        pruefe(n == 3, "loop ( 3 ) laeuft dreimal");
    }
    // 7. INSERT holt ein laufendes wait zurueck, es beginnt von vorn
    {
        const Script s = lies(
            "affect ( \"a\", FLUSH )\n{\n\twait ( 1000.000 );\n\tprint ( \"nach\" );\n}\n"
            "wait ( 500.000 );\naffect ( \"a\", INSERT )\n{\n\tprint ( \"dazwischen\" );\n}\n");
        const Ablauf a = simuliereAblauf(s, "", w);
        const double t = zeitVon(a, "a", "print", "nach");
        pruefe(t >= 1550.0 && t <= 1650.0, "INSERT: wait beginnt von vorn");
    }
    // 8. Herkunft zeigt auf die Zeile im Startskript
    {
        const Script s = lies("wait ( 100.000 );\ncamera ( ENABLE );\n");
        const Ablauf a = simuliereAblauf(s, "", w);
        bool gefunden = false;
        for (std::size_t i = 0; i < a.flach.nodes.size(); ++i) {
            if (a.flach.nodes[i].name == "camera") {
                const AblaufHerkunft* h = a.woher(Path{i});
                gefunden = h != nullptr && h->skript == 0 && h->path == Path{1};
            }
        }
        pruefe(gefunden, "Herkunft der Kamerazeile");
    }
    // 9. ohne Takt (bildMs 0) genau die Summe
    {
        AblaufWelt w0;
        w0.bildMs = 0.0;
        const Script s = lies("wait ( 1000.000 );\ncamera ( ENABLE );\nwait ( 250.000 );\ncamera ( DISABLE );\n");
        const Ablauf a = simuliereAblauf(s, "", w0);
        pruefe(std::fabs(zeitVon(a, "", "camera", "DISABLE") - 1250.0) < 0.5, "ohne Takt: Summe der waits");
    }
    // 10. use auf target_scriptrunner startet dessen Skript, Spawner mit delay
    {
        MapData m;
        MapEntity r;
        r.classname = "target_scriptrunner";
        r.targetname = "runner";
        r.keys = {{"classname", "target_scriptrunner"}, {"targetname", "runner"}, {"usescript", "x/zwei"}};
        MapEntity sp;
        sp.classname = "NPC_spawner";
        sp.targetname = "sp1";
        sp.origin = "10 0 0";
        sp.keys = {{"classname", "NPC_spawner"}, {"targetname", "sp1"}, {"NPC_targetname", "bob"},
                   {"delay", "1"}, {"spawnscript", "x/bob"}};
        m.entities = {r, sp};
        auto zwei = std::make_shared<Script>(lies("wait ( 200.000 );\nprint ( \"zwei\" );\n"));
        auto bob = std::make_shared<Script>(lies("set ( \"SET_ANIM_BOTH\", \"BOTH_SIT1\" );\n"));
        AblaufWelt w3;
        w3.karte = &m;
        w3.skript = [zwei, bob](const std::string& p) -> const Script* {
            if (p == "x/zwei") { return zwei.get(); }
            if (p == "x/bob") { return bob.get(); }
            return nullptr;
        };
        const Script s = lies("use ( \"runner\" );\nuse ( \"sp1\" );\nwait ( 3000.000 );\n");
        const Ablauf a = simuliereAblauf(s, "", w3);
        pruefe(zeitVon(a, "runner", "print", "zwei") >= 250.0, "target_scriptrunner startet usescript");
        const auto it = a.erscheint.find("bob");
        pruefe(it != a.erscheint.end() && std::fabs(it->second - 1100.0) < 51.0, "NPC_spawner mit delay 1 s (+100 ms NPC_Begin)");
        pruefe(zeitVon(a, "bob", "set", "BOTH_SIT1") >= 1000.0, "spawnscript laeuft auf der Figur");
    }
    // 11. play ( "PLAY_ROFF" ) haelt TID_MOVE_NAV bis zum letzten Bild
    //     (Q3_Interface.cpp:10698, g_roff.cpp:626). Fehlt die Datei, ruft
    //     niemand Completed (TaskManager.cpp:1603): dowait haengt.
    {
        const Script s = lies(
            "affect ( \"a\", FLUSH )\n{\n\ttask ( \"flug\" )\n\t{\n\t\tplay ( \"PLAY_ROFF\", \"x/bahn\" );\n\t}\n"
            "\tdowait ( \"flug\" );\n\tset ( \"SET_ANIM_BOTH\", \"BOTH_STAND1\" );\n}\n");
        AblaufWelt w4;
        w4.roffDauer = [](const std::string& n) { return n == "x/bahn" ? 1750.0 : kRoffFehlt; };
        const Ablauf a = simuliereAblauf(s, "", w4);
        const double t = zeitVon(a, "a", "set", "BOTH_STAND1");
        pruefe(t >= 1750.0 && t <= 1800.0, "dowait wartet auf die ROFF-Bahn");
        const Ablauf b = simuliereAblauf(s, "", w);
        pruefe(std::fabs(zeitVon(b, "a", "set", "BOTH_STAND1")) < 0.5, "ROFF ohne Angabe: sofort fertig");
        AblaufWelt w5;
        w5.roffDauer = [](const std::string&) { return kRoffFehlt; };
        const Ablauf c = simuliereAblauf(s, "", w5);
        pruefe(zeitVon(c, "a", "set", "BOTH_STAND1") < 0.0, "ROFF fehlt: dowait haengt wie im Spiel");
    }
    // 12. Zerbrechliches feuert seine Ziele im Nachbau (MoverSim bekommt
    //     sie einzeln): func_breakable nach "delay" (F_INT, ganze Sekunden),
    //     misc_model_breakable beim Bruch und target3 beim use danach.
    //     Und jede Benutzung weiss, wer ausgeloest hat.
    {
        MapData m;
        const auto ent = [](const std::string& k, std::vector<std::pair<std::string, std::string>> keys) {
            MapEntity e;
            e.classname = k;
            for (const auto& [a, b] : keys) {
                if (a == "targetname") { e.targetname = b; }
            }
            keys.emplace_back("classname", k);
            e.keys = std::move(keys);
            return e;
        };
        m.entities = {ent("func_breakable", {{"targetname", "fels"}, {"target", "runner"}, {"delay", "1.9"},
                                             {"model", "*1"}}),
                      ent("target_scriptrunner", {{"targetname", "runner"}, {"usescript", "x/drei"}}),
                      ent("misc_model_breakable", {{"targetname", "kiste"}, {"health", "5"}, {"target", "t1"},
                                                   {"target3", "t3"}, {"model", "models/k.md3"}}),
                      ent("func_breakable", {{"targetname", "knopf"}, {"spawnflags", "64"}, {"target", "t2"},
                                             {"model", "*2"}})};
        auto drei = std::make_shared<Script>(lies("print ( \"drei\" );\n"));
        AblaufWelt w4;
        w4.karte = &m;
        w4.skript = [drei](const std::string& p) -> const Script* { return p == "x/drei" ? drei.get() : nullptr; };
        const Script s = lies(
            "affect ( \"player\", FLUSH )\n{\n\tuse ( \"fels\" );\n}\n"
            "use ( \"kiste\" );\nwait ( 500.000 );\nuse ( \"kiste\" );\nuse ( \"knopf\" );\nuse ( \"knopf\" );\n"
            "use ( \"fels\" );\nwait ( 3000.000 );\n");
        const Ablauf a = simuliereAblauf(s, "", w4);
        double fels = -1.0;
        double runner = -1.0;
        std::string felsWer;
        int t1 = 0;
        int t2 = 0;
        int t3 = 0;
        int runnerZahl = 0;
        for (const Ablauf::Benutzung& b : a.benutzt) {
            if (b.name == "fels" && fels < 0.0) { fels = b.ms; felsWer = b.ausloeser; }
            if (b.name == "runner") { runner = b.ms; ++runnerZahl; }
            t1 += (b.name == "t1") ? 1 : 0;
            t2 += (b.name == "t2") ? 1 : 0;
            t3 += (b.name == "t3") ? 1 : 0;
        }
        pruefe(felsWer == "player", "Benutzung kennt den Ausloeser (affect player)");
        pruefe(fels >= 0.0 && std::fabs(runner - fels - 1000.0) < 0.5 && runnerZahl == 1,
               "func_breakable feuert sein target nach delay 1.9 -> 1 s, und nur einmal");
        pruefe(zeitVon(a, "runner", "print", "drei") >= fels + 1000.0, "... und startet damit ein Skript");
        pruefe(t1 == 1 && t3 == 1, "misc_model_breakable: target beim Bruch, target3 beim use danach");
        pruefe(t2 == 2, "USE_NOT_BREAK: jedes use feuert das target");
    }
}

std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

void schreibe(const std::vector<Node>& ns, int tiefe, double t0, const Ablauf& a, const Path& basis,
              const std::string& wer) {
    double t = t0;
    for (std::size_t i = 0; i < ns.size(); ++i) {
        const Node& n = ns[i];
        Path p = basis;
        p.push_back(i);
        if (n.name == "wait") {
            t += std::atof(n.args[0].text.c_str());
            continue;
        }
        std::string args;
        for (const Arg& x : n.args) {
            args += (args.empty() ? "" : ", ") + x.text;
        }
        const AblaufHerkunft* h = a.woher(p);
        const std::string von = (h != nullptr && h->skript >= 0) ? a.skripte[static_cast<std::size_t>(h->skript)] : "";
        if (n.name == "affect") {
            schreibe(n.children, tiefe + 1, t, a, p, n.args.empty() ? std::string{} : n.args[0].text);
            continue;
        }
        // "@wer zeit befehl ( ... ) [skript]" - eindeutig fuer den Vergleich
        // mit dem Spielprotokoll (behaved_ingame/vergleich.py).
        std::printf("@%s %.0f %s ( %s )   [%s]\n", wer.empty() ? a.traeger.c_str() : wer.c_str(), t,
                    n.name.c_str(), args.c_str(), von.c_str());
    }
}

int werkzeug(int argc, char** argv) {
    const std::filesystem::path ordner = argv[1];
    const std::string start = argv[2];
    std::map<std::string, std::unique_ptr<Script>> cache;
    const auto laden = [&](const std::string& pfad) -> const Script* {
        auto it = cache.find(pfad);
        if (it != cache.end()) {
            return it->second.get();
        }
        const std::filesystem::path f = ordner / "scripts" / (pfad + ".txt");
        if (!std::filesystem::exists(f)) {
            cache[pfad] = nullptr;
            return nullptr;
        }
        auto s = std::make_unique<Script>(lies(slurp(f)));
        const Script* r = s.get();
        cache[pfad] = std::move(s);
        return r;
    };
    MapData karte;
    // --npc <datei>: je Zeile "<npc_type> <walkSpeed> <runSpeed>" - die
    // Tempi aus den .npc-Dateien, damit Gehzeiten wie in der App rechnen.
    std::map<std::string, std::pair<float, float>> tempi;
    for (int i = 3; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--npc") {
            std::ifstream f(argv[i + 1]);
            std::string typ;
            float geh = 0.0F;
            float lauf = 0.0F;
            while (f >> typ >> geh >> lauf) {
                for (char& c : typ) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                tempi[typ] = {geh, lauf};
            }
        }
    }
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.size() > 4 && a.substr(a.size() - 4) == ".bsp") {
            // Karte direkt aus einer Datei
            (void)readBsp(slurp(a), karte);
            continue;
        }
    }
    const Script* s = laden(start);
    if (s == nullptr) {
        std::printf("Skript %s fehlt\n", start.c_str());
        return 2;
    }
    AblaufWelt w;
    w.karte = karte.empty() ? nullptr : &karte;
    w.skript = laden;
    if (!tempi.empty()) {
        w.tempo = [&karte, &tempi](const std::string& figur, bool gehen) -> float {
            for (const MapEntity& e : karte.entities) {
                const std::string* n = e.find("NPC_targetname");
                if (n == nullptr || *n != figur) {
                    continue;
                }
                std::string typ;
                if (const std::string* ty = e.find("NPC_type")) {
                    typ = *ty;
                } else if (e.classname.size() > 4) {
                    typ = e.classname.substr(4);
                }
                for (char& c : typ) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                const auto it = tempi.find(typ);
                if (it != tempi.end()) {
                    return gehen ? it->second.first : it->second.second;
                }
            }
            return -1.0F;
        };
    }
    const Ablauf a = simuliereAblauf(*s, start, w);
    std::printf("Traeger: %s%s  Ende %.0f ms, simuliert bis %.0f, %d Befehle, %zu Skripte\n",
                a.traeger.c_str(), a.traegerIstFigur ? " (Figur)" : "", a.endeMs, a.laufMs, a.befehle,
                a.skripte.size());
    for (const auto& [n, t] : a.erscheint) {
        std::printf("  erscheint %s bei %.0f\n", n.c_str(), t);
    }
    for (const std::string& h : a.hinweise) {
        std::printf("  Hinweis: %s\n", h.c_str());
    }
    schreibe(a.flach.nodes, 0, 0.0, a, Path{}, std::string{});
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3) {
        return werkzeug(argc, argv);
    }
    feste();
    std::printf("%s\n", fehler == 0 ? "alle Proben bestanden" : "FEHLGESCHLAGEN");
    return fehler == 0 ? 0 : 1;
}
