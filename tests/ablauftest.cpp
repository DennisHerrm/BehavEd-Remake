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
    // 10b. rc604: CSequencer::Run (Sequencer.cpp:383-411 OpenJK) legt den
    //      neuen Ablauf nur an (Route -> Prime -> SetCommand -> PushTask,
    //      TaskManager.cpp:857-878) - kein Go(). Er laeuft erst beim naechsten
    //      Update der Entity (G_RunThink, g_main.cpp:1077-1086). Der Runner
    //      (Entity 1) ist in diesem Bild schon dran gewesen: ein Bild spaeter.
    {
        MapData m;
        MapEntity r;
        r.classname = "target_scriptrunner";
        r.targetname = "runner";
        r.keys = {{"classname", "target_scriptrunner"}, {"targetname", "runner"}, {"usescript", "x/jetzt"}};
        m.entities = {r};
        auto jetzt = std::make_shared<Script>(lies("print ( \"jetzt\" );\n"));
        AblaufWelt w3;
        w3.karte = &m;
        w3.skript = [jetzt](const std::string& p) -> const Script* { return p == "x/jetzt" ? jetzt.get() : nullptr; };
        const Script s = lies("use ( \"runner\" );\nwait ( 500.000 );\n");
        const Ablauf a = simuliereAblauf(s, "", w3);
        const double t = zeitVon(a, "runner", "print", "jetzt");
        std::printf("   run: print bei %.1f ms\n", t);
        pruefe(std::fabs(t - 50.0) < 0.5, "Run ohne Go: das Skript beginnt beim naechsten Update der Entity");
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

// rc603: der Kartenstart (simuliereKartenstart) und die Reihenfolge der
// Skripte einer Mission (skripteNachStart). Jede Zeit aus dem Quelltext der
// Engine: spawnscript beim Spawnen 0 (g_spawn.cpp:1114-1124), worldspawn 100
// (:1660-1676), trigger_always 300 (g_trigger.cpp:620-624), NPC-Spawner ohne
// targetname 350 -> NPC_Begin 500 -> erstes Bild 550 (NPC_spawn.cpp:2049),
// ClientSpawn am Ende dieses Bildes (g_client.cpp:2432-2434; die Intros
// sprechen dort schon Figuren an, die NPC_Begin erst bei 550 anmeldet).
void kartenstart() {
    MapData m;
    const auto ent = [](const std::string& k, std::vector<std::pair<std::string, std::string>> keys) {
        MapEntity e;
        e.classname = k;
        for (const auto& [a, b] : keys) {
            if (a == "targetname") { e.targetname = b; }
            if (a == "origin") { e.origin = b; }
        }
        keys.emplace_back("classname", k);
        e.keys = std::move(keys);
        return e;
    };
    m.entities = {
        ent("worldspawn", {{"spawnscript", "k/welt"}}),
        ent("trigger_multiple", {{"usescript", "k/nie"}, {"model", "*1"}}),   // nur der Spieler beim Betreten
        ent("target_scriptrunner", {{"targetname", "zweit"}, {"usescript", "k/zweit"}}),
        ent("NPC_spawner", {{"targetname", "spawnwache2"}, {"NPC_targetname", "w2"}, {"NPC_type", "stormtrooper"},
                            {"deathscript", "k/tod"}, {"origin", "0 0 0"}}),
        ent("NPC_spawner", {{"NPC_targetname", "wache"}, {"NPC_type", "stormtrooper"}, {"spawnscript", "k/wache"},
                            {"origin", "64 0 0"}}),
        ent("info_player_start", {{"target", "start"}, {"origin", "0 0 0"}}),
        ent("target_scriptrunner", {{"targetname", "start"}, {"usescript", "k/intro"}}),
        ent("target_scriptrunner", {{"targetname", "start"}, {"usescript", "k/spieler"}}),
        ent("trigger_always", {{"target", "immer"}}),
        ent("target_scriptrunner", {{"targetname", "immer"}, {"usescript", "k/immer"}}),
        ent("func_static", {{"spawnscript", "k/statisch"}, {"model", "*2"}}),
        ent("func_static", {{"spawnscript", "k/schwer"}, {"spawnflags", "256"}, {"model", "*3"}}),
        // Ohne NPC_targetname: fuer ICARUS namenlos, das spawnscript laeuft doch.
        ent("NPC_spawner", {{"NPC_type", "stormtrooper"}, {"spawnscript", "k/namenlos"}, {"origin", "0 64 0"}}),
        ent("NPC_spawner", {{"targetname", "spn"}, {"NPC_type", "stormtrooper"}, {"spawnscript", "k/spaeter"},
                            {"origin", "0 128 0"}}),
    };
    std::map<std::string, std::shared_ptr<Script>> skripte;
    skripte["k/intro"] = std::make_shared<Script>(
        lies("wait ( 2000.000 );\nuse ( \"zweit\" );\nuse ( \"spawnwache2\" );\n"));
    skripte["k/zweit"] = std::make_shared<Script>(lies("use ( \"spn\" );\nwait ( 500.000 );\nkill ( \"w2\" );\n"));
    for (const char* n : {"k/welt", "k/nie", "k/tod", "k/wache", "k/spieler", "k/immer", "k/statisch", "k/schwer",
                          "k/namenlos", "k/spaeter"}) {
        skripte[n] = std::make_shared<Script>(lies("wait ( 1.000 );\n"));
    }
    AblaufWelt w;
    w.karte = &m;
    w.skript = [skripte](const std::string& p) -> const Script* {
        const auto it = skripte.find(p);
        return it == skripte.end() ? nullptr : it->second.get();
    };
    w.spielstufe = 0;   // leicht: spawnflags 256 (NOT_EASY) spawnt nicht
    const Ablauf a = simuliereKartenstart(w);
    const auto beginn = [&a](const std::string& p) {
        for (std::size_t i = 0; i < a.skripte.size(); ++i) {
            if (a.skripte[i] == p) { return a.skriptBeginn[i]; }
        }
        return -2.0;
    };
    for (std::size_t i = 0; i < a.skripte.size(); ++i) {
        std::printf("  Kartenstart: %s ab %.0f ms (Folge %d)\n", a.skripte[i].c_str(), a.skriptBeginn[i], a.skriptFolge[i]);
    }
    pruefe(beginn("k/statisch") == 0.0, "Kartenstart: spawnscript einer Entity beim Spawnen (0 ms)");
    pruefe(beginn("k/welt") == 100.0, "Kartenstart: spawnscript von worldspawn ueber den Skriptstarter (100 ms)");
    pruefe(beginn("k/immer") == 300.0, "Kartenstart: trigger_always (300 ms)");
    pruefe(beginn("k/intro") == 550.0 && beginn("k/spieler") == 550.0,
           "Kartenstart: die Ziele der Startposition beim ClientSpawn (Ende des Bildes 550)");
    pruefe(beginn("k/wache") == 550.0, "Kartenstart: NPC-Spawner ohne targetname - spawnscript im ersten Bild nach NPC_Begin (550 ms)");
    // rc604: Run ohne Go - das Intro (Run bei 550, ClientSpawn) laeuft erst
    // im Bild 600 los, sein wait endet bei 2650; k/zweit (Entity 3, vor dem
    // Runner 7) laeuft ab 2700, sein wait 500 endet bei 3250.
    pruefe(beginn("k/zweit") == 2650.0, "Kartenstart: use aus dem Intro nach wait ( 2000 ) (2650 ms)");
    pruefe(beginn("k/tod") == 3250.0, "Kartenstart: kill nach wait ( 500 ) startet das deathscript der gespawnten Figur (3250 ms)");
    pruefe(beginn("k/namenlos") == 550.0, "Kartenstart: Spawner ohne NPC_targetname - spawnscript trotzdem (550 ms)");
    pruefe(beginn("k/spaeter") == 2800.0,
           "Kartenstart: Spawner ohne NPC_targetname per use - spawnscript nach NPC_Begin (use 2700 + 100)");
    pruefe(beginn("k/nie") == -2.0 && beginn("k/schwer") == -2.0,
           "Kartenstart: Trigger beim Betreten und eine Entity, die bei dieser Schwierigkeit nicht spawnt, starten nichts");
    const std::vector<std::string> pfade = {"k/nie", "K/Zweit", "scripts/k/tod.IBI", "k/wache", "k/spieler",
                                            "k/intro", "k/immer", "k/welt", "k/statisch", "k/schwer"};
    const std::vector<std::size_t> folge = skripteNachStart(pfade, a);
    std::string text;
    for (const std::size_t i : folge) { text += " " + pfade[i]; }
    std::printf("  Reihenfolge:%s\n", text.c_str());
    pruefe(folge == std::vector<std::size_t>{8, 7, 6, 3, 5, 4, 1, 2, 0, 9},
           "Reihenfolge: nach dem ersten Start, bei gleicher Zeit nach der Folge (wache, dann intro vor spieler), nie gestartete hinten");
    // Mittlere Schwierigkeit: dann spawnt die Entity mit NOT_EASY.
    w.spielstufe = 1;
    const Ablauf b = simuliereKartenstart(w);
    bool schwer = false;
    for (std::size_t i = 0; i < b.skripte.size(); ++i) { schwer = schwer || (b.skripte[i] == "k/schwer" && b.skriptBeginn[i] == 0.0); }
    pruefe(schwer, "Kartenstart: bei g_spskill 1 spawnt die Entity mit spawnflags 256 und startet ihr spawnscript");
    // Ein gewoehnlicher Ablauf: das Startskript beginnt bei 0, was es per
    // use startet, zur Zeit des use.
    const Ablauf c = simuliereAblauf(*skripte["k/intro"], "k/intro", w);
    pruefe(!c.skriptBeginn.empty() && c.skriptBeginn[0] == 0.0, "Ablauf: das Startskript beginnt bei 0");
}

// rc603 (Durchsicht): Spawner ohne targetname im Kartenstart, wie die Engine
// sie behandelt.
//   * Jeder Spawner erzeugt SEINE Figur mit SEINEM spawnscript, auch wenn
//     mehrere denselben NPC_targetname tragen (t1_fatal: drei NPC_ImpWorker
//     "npc_corpse1" mit spawncorpse, spawncorpse2, spawncorpse3) - NPC_Begin
//     ruft G_ActivateBehavior( ent, BSET_SPAWN ) fuer jede Figur.
//   * NPC_Spawn_Do benutzt die Ziele eines Spawners ohne use-Funktion
//     (e_UseFunc == useF_NULL, also ohne targetname) gleich beim Spawnen:
//     G_UseTargets( ent, ent ) (NPC_spawn.cpp:1762-1766 OpenJK, :2805-2811
//     Movie Duels) - im Bild 400, in dem NPC_Spawn_Go laeuft.
void kartenstartSpawner() {
    MapData m;
    const auto ent = [](const std::string& k, std::vector<std::pair<std::string, std::string>> keys) {
        MapEntity e;
        e.classname = k;
        for (const auto& [a, b] : keys) {
            if (a == "targetname") { e.targetname = b; }
            if (a == "origin") { e.origin = b; }
        }
        keys.emplace_back("classname", k);
        e.keys = std::move(keys);
        return e;
    };
    m.entities = {
        ent("worldspawn", {}),
        ent("info_player_start", {{"origin", "0 0 0"}}),
        ent("NPC_ImpWorker", {{"NPC_targetname", "leiche"}, {"spawnscript", "k/leiche1"}, {"origin", "0 256 0"}}),
        ent("NPC_ImpWorker", {{"NPC_targetname", "leiche"}, {"spawnscript", "k/leiche2"}, {"origin", "64 256 0"}}),
        ent("NPC_ImpWorker", {{"NPC_targetname", "leiche"}, {"spawnscript", "k/leiche3"}, {"origin", "128 256 0"}}),
        ent("NPC_spawner", {{"NPC_type", "stormtrooper"}, {"target", "autoziel"}, {"origin", "0 384 0"}}),
        ent("target_scriptrunner", {{"targetname", "autoziel"}, {"usescript", "k/auto"}}),
        ent("NPC_spawner", {{"NPC_type", "stormtrooper"}, {"NPC_targetname", "mitziel"}, {"spawnscript", "k/mitziel"},
                            {"target", "autoziel2"}, {"origin", "0 512 0"}}),
        ent("target_scriptrunner", {{"targetname", "autoziel2"}, {"usescript", "k/auto2"}}),
    };
    std::map<std::string, std::shared_ptr<Script>> skripte;
    for (const char* n : {"k/leiche1", "k/leiche2", "k/leiche3", "k/auto", "k/mitziel", "k/auto2"}) {
        skripte[n] = std::make_shared<Script>(lies("wait ( 1.000 );\n"));
    }
    AblaufWelt w;
    w.karte = &m;
    w.skript = [skripte](const std::string& p) -> const Script* {
        const auto it = skripte.find(p);
        return it == skripte.end() ? nullptr : it->second.get();
    };
    w.spielstufe = 1;
    const Ablauf a = simuliereKartenstart(w);
    const auto beginn = [&a](const std::string& p) {
        for (std::size_t i = 0; i < a.skripte.size(); ++i) {
            if (a.skripte[i] == p) { return a.skriptBeginn[i]; }
        }
        return -2.0;
    };
    for (std::size_t i = 0; i < a.skripte.size(); ++i) {
        std::printf("  Kartenstart (Spawner): %s ab %.0f ms\n", a.skripte[i].c_str(), a.skriptBeginn[i]);
    }
    pruefe(beginn("k/leiche1") == 550.0 && beginn("k/leiche2") == 550.0 && beginn("k/leiche3") == 550.0,
           "Kartenstart: drei Spawner mit demselben NPC_targetname - jedes spawnscript im Bild 550 (t1_fatal npc_corpse1)");
    pruefe(beginn("k/auto") == 400.0,
           "Kartenstart: Spawner ohne targetname benutzt sein target beim Spawnen (NPC_Spawn_Do, Bild 400)");
    pruefe(beginn("k/auto2") == 400.0 && beginn("k/mitziel") == 550.0,
           "Kartenstart: Spawner mit NPC_targetname und target - Ziel bei 400, spawnscript bei 550");
}

// rc603 (Durchsicht): Entities, deren Spawnfunktion sie gleich wieder
// freigibt, starten kein spawnscript. G_SpawnGEntityFromSpawnVars fragt
// ValidEntity NACH G_CallSpawn (g_spawn.cpp:1108-1114 OpenJK); G_FreeEntity
// hat die Entity da schon geleert (memset, g_utils.cpp:988), behaviorSet ist
// leer. SP_waypoint / SP_waypoint_small / SP_waypoint_navgoal geben immer
// frei (g_nav.cpp:172-300 OpenJK, :162-283 Movie Duels), SP_info_null (auch
// func_group) mit spawnflags 1, sonst erst bei START_TIME_REMOVE_ENTS
// (g_misc.cpp:54-65), SP_light ohne targetname (g_misc.cpp:164-169).
// t1_fatal: der waypoint_navgoal "riposte1" mit spawnscript
// t1_fatal/riposte1 stand als erster Reiter vorn (@0); vjun1 ebenso
// common/nav_goto (waypoint_navgoal "haztroopernav").
void kartenstartFreigegeben() {
    MapData m;
    const auto ent = [](const std::string& k, std::vector<std::pair<std::string, std::string>> keys) {
        MapEntity e;
        e.classname = k;
        for (const auto& [a, b] : keys) {
            if (a == "targetname") { e.targetname = b; }
            if (a == "origin") { e.origin = b; }
        }
        keys.emplace_back("classname", k);
        e.keys = std::move(keys);
        return e;
    };
    m.entities = {
        ent("worldspawn", {}),
        ent("info_player_start", {{"origin", "0 0 0"}}),
        ent("waypoint_navgoal", {{"targetname", "riposte1"}, {"spawnscript", "k/navziel"}, {"origin", "0 64 0"}}),
        ent("waypoint", {{"spawnscript", "k/wegpunkt"}, {"origin", "0 128 0"}}),
        ent("waypoint_small", {{"spawnscript", "k/wegpunktklein"}, {"origin", "0 192 0"}}),
        ent("info_null", {{"spawnflags", "1"}, {"spawnscript", "k/nullweg"}, {"origin", "0 256 0"}}),
        ent("func_group", {{"spawnflags", "1"}, {"spawnscript", "k/gruppeweg"}}),
        ent("info_null", {{"targetname", "bleibt"}, {"spawnscript", "k/nullbleibt"}, {"origin", "0 320 0"}}),
        ent("light", {{"spawnscript", "k/lichtweg"}, {"origin", "0 384 0"}}),
        ent("light", {{"targetname", "lampe"}, {"spawnscript", "k/licht"}, {"origin", "0 448 0"}}),
        ent("func_static", {{"spawnscript", "k/statisch"}, {"model", "*1"}}),
    };
    std::map<std::string, std::shared_ptr<Script>> skripte;
    for (const char* n : {"k/navziel", "k/wegpunkt", "k/wegpunktklein", "k/nullweg", "k/gruppeweg", "k/nullbleibt",
                          "k/lichtweg", "k/licht", "k/statisch"}) {
        skripte[n] = std::make_shared<Script>(lies("wait ( 1.000 );\n"));
    }
    AblaufWelt w;
    w.karte = &m;
    w.skript = [skripte](const std::string& p) -> const Script* {
        const auto it = skripte.find(p);
        return it == skripte.end() ? nullptr : it->second.get();
    };
    w.spielstufe = 1;
    const Ablauf a = simuliereKartenstart(w);
    const auto beginn = [&a](const std::string& p) {
        for (std::size_t i = 0; i < a.skripte.size(); ++i) {
            if (a.skripte[i] == p) { return a.skriptBeginn[i]; }
        }
        return -2.0;
    };
    for (std::size_t i = 0; i < a.skripte.size(); ++i) {
        std::printf("  Kartenstart (freigegeben): %s ab %.0f ms\n", a.skripte[i].c_str(), a.skriptBeginn[i]);
    }
    pruefe(beginn("k/navziel") < 0.0 && beginn("k/wegpunkt") < 0.0 && beginn("k/wegpunktklein") < 0.0,
           "Kartenstart: waypoint, waypoint_small, waypoint_navgoal geben sich beim Spawnen frei - kein spawnscript");
    pruefe(beginn("k/nullweg") < 0.0 && beginn("k/gruppeweg") < 0.0 && beginn("k/lichtweg") < 0.0,
           "Kartenstart: info_null/func_group mit spawnflags 1 und light ohne targetname - kein spawnscript");
    pruefe(beginn("k/nullbleibt") == 0.0 && beginn("k/licht") == 0.0 && beginn("k/statisch") == 0.0,
           "Kartenstart: info_null ohne spawnflags 1, light mit targetname, func_static - spawnscript bei 0");
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

// rc604: ein Skriptname, dessen Spawner G_SpawnGEntityFromSpawnVars wegen der
// Schwierigkeit freigibt (code/game/g_spawn.cpp:1064-1075 OpenJK): affect findet
// niemanden, der Block laeuft nicht.
void entfernterSpawner() {
    MapData m;
    MapEntity sp;
    sp.classname = "NPC_spawner";
    sp.origin = "0 0 0";
    sp.keys = {{"classname", "NPC_spawner"}, {"NPC_targetname", "wache"}, {"NPC_type", "stormtrooper"},
               {"spawnflags", "1024"}, {"origin", "0 0 0"}};
    m.entities = {sp};
    MapEntity welt;
    welt.classname = "worldspawn";
    welt.keys = {{"classname", "worldspawn"}};
    m.entities.insert(m.entities.begin(), welt);
    const Script s = lies("affect ( \"wache\", FLUSH )\n{\n\tprint ( \"da\" );\n}\n");
    const auto drucke = [&](int stufe) {
        AblaufWelt w;
        w.karte = &m;
        w.spielstufe = stufe;
        const Ablauf a = simuliereAblauf(s, "", w);
        return zeitVon(a, "wache", "print", "da") >= 0.0 ? 1 : 0;
    };
    const int schwer = drucke(2);
    const int leicht = drucke(0);
    std::printf("  entfernter Spawner: print schwer %d, leicht %d\n", schwer, leicht);
    pruefe(schwer == 0 && leicht == 1, "affect auf eine Figur, deren Spawner die Schwierigkeit entfernt: laeuft nicht");
}

// rc605: eine explodierende misc_trip_mine benutzt ihr target (WP_Explode ->
// G_UseTargets) - der Nachbau rechnet den target_counter dahinter (count 2:
// erst die zweite Explosion feuert usescript und target) und gibt die
// Benutzung der Wand an MoverSim weiter.
void fallenZiele() {
    MapData m;
    const auto ent = [](const std::string& k, std::vector<std::pair<std::string, std::string>> keys) {
        MapEntity e;
        e.classname = k;
        for (const auto& [a, b] : keys) {
            if (a == "targetname") { e.targetname = b; }
            if (a == "origin") { e.origin = b; }
        }
        keys.emplace_back("classname", k);
        e.keys = std::move(keys);
        return e;
    };
    m.entities = {
        ent("worldspawn", {}),
        ent("misc_trip_mine", {{"target", "zaehler"}, {"origin", "0 0 0"}}),
        ent("misc_trip_mine", {{"target", "zaehler"}, {"origin", "64 0 0"}}),
        ent("target_counter", {{"targetname", "zaehler"}, {"count", "2"}, {"usescript", "f/zaehler"}, {"target", "wand"}}),
        ent("func_usable", {{"targetname", "wand"}, {"model", "*1"}}),
    };
    std::map<std::string, std::shared_ptr<Script>> skripte;
    skripte["f/zaehler"] = std::make_shared<Script>(lies("print ( \"zaehler\" );\n"));
    AblaufWelt w;
    w.karte = &m;
    w.skript = [skripte](const std::string& p) -> const Script* {
        const auto it = skripte.find(p);
        return it == skripte.end() ? nullptr : it->second.get();
    };
    w.fallenUses = {{600.0, "zaehler", 1}, {1200.0, "zaehler", 2}};
    const Script s = lies("wait ( 2000.000 );\n");
    const Ablauf a = simuliereAblauf(s, "", w);
    const double druck = zeitVon(a, "zaehler", "print", "zaehler");
    int wand = 0;
    double wandMs = -1.0;
    for (const Ablauf::Benutzung& b : a.benutzt) {
        if (b.name == "wand") {
            ++wand;
            wandMs = b.ms;
        }
    }
    std::printf("  Fallenziele: usescript bei %.0f, Wand %d mal (bei %.0f)\n", druck, wand, wandMs);
    pruefe(druck >= 1200.0 && druck < 1300.0, "zweite Explosion: target_counter feuert usescript (count 2)");
    pruefe(wand == 1 && std::fabs(wandMs - 1200.0) < 0.5, "zweite Explosion: target des Zaehlers einmal benutzt, bei 1200 ms");
}

int main(int argc, char** argv) {
    if (argc >= 3) {
        return werkzeug(argc, argv);
    }
    feste();
    kartenstart();   // rc603
    kartenstartSpawner();   // rc603 (Durchsicht)
    kartenstartFreigegeben();   // rc603 (Durchsicht)
    entfernterSpawner();   // rc604
    fallenZiele();         // rc605
    std::printf("%s\n", fehler == 0 ? "alle Proben bestanden" : "FEHLGESCHLAGEN");
    return fehler == 0 ? 0 : 1;
}
