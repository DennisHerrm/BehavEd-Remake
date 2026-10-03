// Gegenproben zur Code-Pruefung vom 03.10.2026.
//
// shank: "schau den kompletten code nochmal an ob es noch irgendwas gibt das
// fehler verursachen koennte. Teste es und wenn es wirklich einer ist fixen."
//
// Jeder Fund steht hier als Probe, die mit dem alten Stand FEHLSCHLAEGT (oder
// abstuerzt/haengt) und mit der Korrektur besteht. Haengende Faelle laufen in
// einem eigenen Faden mit Zeitwaechter; abstuerzende in try/catch, wo es eine
// Ausnahme ist. Wer eine Probe aendert, laesst sie einmal gegen den alten Stand
// laufen - sonst beweist sie nichts.
#include "bhed/ablauf.h"
#include "bhed/bspgeo.h"
#include "bhed/edit.h"
#include "bhed/gla.h"
#include "bhed/mapview.h"
#include "bhed/md3.h"
#include "bhed/pk3.h"
#include "bhed/scene.h"
#include "bhed/script.h"
#include "bhed/settings.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <new>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace {

int g_fehler = 0;

void pruefe(bool ok, const char* was) {
    std::printf("%s %s\n", ok ? "  ok  " : "  FEHL", was);
    std::fflush(stdout);
    if (!ok) { ++g_fehler; }
}

// Laeuft `f` nicht binnen `sekunden` durch, ist das ein Haenger: melden und
// den Lauf beenden (der Faden laesst sich nicht abbrechen).
void mitZeitwaechter(const char* was, double sekunden, const std::function<void()>& f) {
    std::atomic<bool> fertig{false};
    std::thread t([&] { f(); fertig = true; });
    const auto start = std::chrono::steady_clock::now();
    while (!fertig) {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() > sekunden) {
            std::printf("  FEHL  %s - haengt (nach %.0f s abgebrochen)\n", was, sekunden);
            std::printf("FEHLGESCHLAGEN\n");
            std::fflush(stdout);
            std::_Exit(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    t.join();
}

bhed::Script lies(const std::string& text) {
    bhed::Script s;
    std::vector<bhed::Diag> d;
    (void)bhed::readScript(text, s, d);
    return s;
}

// ---- Eine winzige .bsp im Speicher ------------------------------------------
void setzeI32(std::string& b, std::size_t at, std::int32_t v) {
    for (int i = 0; i < 4; ++i) {
        b[at + static_cast<std::size_t>(i)] = static_cast<char>((static_cast<std::uint32_t>(v) >> (8 * i)) & 0xFFU);
    }
}

struct BspFlaeche {
    std::int32_t typ = 1;   // 1 eben, 2 Patch
    std::int32_t firstVert = 0;
    std::int32_t numVerts = 3;
    std::int32_t firstIndex = 0;
    std::int32_t numIndexes = 3;
    std::int32_t lightmap = -3;
    std::int32_t fog = -1;
    std::int32_t pw = 0;
    std::int32_t ph = 0;
};

std::string baueBsp(const std::vector<BspFlaeche>& flaechen, int ecken, bool mitNebel = false,
                    std::int32_t nebelBrush = 0) {
    constexpr int kLumps = 18;
    std::string b(8 + kLumps * 8, '\0');
    b[0] = 'R'; b[1] = 'B'; b[2] = 'S'; b[3] = 'P';
    setzeI32(b, 4, 1);
    auto lump = [&](int nr, const std::string& daten) {
        setzeI32(b, 8 + static_cast<std::size_t>(nr) * 8, static_cast<std::int32_t>(b.size()));
        setzeI32(b, 12 + static_cast<std::size_t>(nr) * 8, static_cast<std::int32_t>(daten.size()));
        b += daten;
    };
    std::string shader(72, '\0');
    std::memcpy(shader.data(), "textures/test", 13);
    setzeI32(shader, 68, 1);   // CONTENTS_SOLID
    lump(1, shader);
    std::string verts(static_cast<std::size_t>(ecken) * 80, '\0');
    lump(10, verts);
    std::string idx(3 * 4, '\0');
    setzeI32(idx, 0, 0); setzeI32(idx, 4, 1); setzeI32(idx, 8, 2);
    lump(11, idx);
    if (mitNebel) {
        std::string nebel(72, '\0');
        std::memcpy(nebel.data(), "textures/fogs/test", 18);
        setzeI32(nebel, 64, nebelBrush);
        setzeI32(nebel, 68, -1);
        lump(12, nebel);
    }
    std::string fl;
    for (const BspFlaeche& f : flaechen) {
        std::string s(148, '\0');
        setzeI32(s, 0, 0);
        setzeI32(s, 4, f.fog);
        setzeI32(s, 8, f.typ);
        setzeI32(s, 12, f.firstVert);
        setzeI32(s, 16, f.numVerts);
        setzeI32(s, 20, f.firstIndex);
        setzeI32(s, 24, f.numIndexes);
        setzeI32(s, 36, f.lightmap);
        setzeI32(s, 140, f.pw);
        setzeI32(s, 144, f.ph);
        fl += s;
    }
    lump(13, fl);
    return b;
}

#ifdef _WIN32
std::size_t spitzenSpeicher() {
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return pmc.PeakPagefileUsage;
}
#endif

}  // namespace

// PRUEFUNG_OHNE=<name>[,<name>]: Faelle auslassen, die im alten Stand
// abstuerzen oder haengen - so lassen sich die uebrigen einzeln sehen.
bool ohne(const char* name) {
    const char* e = std::getenv("PRUEFUNG_OHNE");
    return e != nullptr && std::strstr(e, name) != nullptr;
}

int main() {
    // ---- Skriptkern ------------------------------------------------------
    // 1. Die Zeilenzahl eines Makros wird beim Speichern mitgeschrieben.
    {
        const std::string text =
            "//$\"standOnly\"@3\r\n"
            "set ( \"SET_BEHAVIOR_STATE\", \"BS_DEFAULT\" );\r\n"
            "set ( \"SET_WALKING\", \"false\" );\r\n"
            "set ( \"SET_RUNNING\", \"false\" );\r\n"
            "wait ( 1000.000 );\r\n";
        bhed::Document doc{lies(text)};
        doc.vergibKennungen();
        (void)doc.removeAt(bhed::Path{1});
        const std::string raus = bhed::writeScript(doc.script());
        pruefe(raus.find("@2") != std::string::npos, "Makro: nach dem Loeschen eines Befehls steht @2 in der Datei");
        const bhed::Script wieder = lies(raus);
        bool waitFrei = false;
        int makroZahl = -1;
        for (const bhed::Node& n : wieder.nodes) {
            if (n.kind == bhed::Node::Kind::Macro) { makroZahl = n.count; }
        }
        // Nach dem Wiedereinlesen: Makro mit 2, wait steht danach als 4. Zeile.
        waitFrei = wieder.nodes.size() == 4 && wieder.nodes[3].name == "wait" && makroZahl == 2;
        pruefe(waitFrei, "Makro: nach dem Wiedereinlesen verschluckt das Makro den wait NICHT");
    }
    // 2. Ein Zug ohne Wirkung loescht das Wiederholen nicht.
    {
        bhed::Document doc{lies("wait ( 1.000 );\r\nwait ( 2.000 );\r\nwait ( 3.000 );\r\n")};
        doc.vergibKennungen();
        for (int k = 0; k < 3; ++k) {
            bhed::Node n = doc.script().nodes[0];
            n.args[0].text = std::to_string(10 + k) + ".000";
            (void)doc.replaceAt(bhed::Path{0}, n);
        }
        (void)doc.undo(); (void)doc.undo(); (void)doc.undo();
        const std::size_t vorher = doc.redoDepth();
        (void)doc.moveTo(bhed::Path{1}, bhed::Path{0});   // auf den Vorgaenger = bleibt stehen
        pruefe(vorher == 3 && doc.redoDepth() == 3, "Wiederholen: ein Zug auf die eigene Stelle loescht es nicht");
    }
    // 3. Einfuegen einer Kopie VOR dem Original: das Original behaelt seine Kennung.
    {
        bhed::Document doc{lies("wait ( 1.000 );\r\nwait ( 2.000 );\r\nwait ( 3.000 );\r\n"
                                "wait ( 4.000 );\r\nwait ( 5.000 );\r\nwait ( 6.000 );\r\n")};
        doc.vergibKennungen();
        const auto orig = doc.script().nodes[5].kennung;
        (void)doc.copyAll({bhed::Path{5}});
        (void)doc.pasteAfter(bhed::Path{1});
        doc.vergibKennungen();
        const auto& ns = doc.script().nodes;
        pruefe(ns.size() == 7 && ns[6].kennung == orig && ns[2].kennung != orig,
               "Einfuegen: das Original behaelt seine Kennung, die Kopie bekommt eine neue");
    }
    // 4. UTF-8-BOM vor dem ersten Befehl.
    {
        const bhed::Script s = lies("\xEF\xBB\xBFwait ( 1.000 );\r\n");
        pruefe(!s.nodes.empty() && s.nodes[0].name == "wait", "BOM: der erste Befehl heisst wait, nicht <BOM>wait");
    }
    // 5. ICARUS: dowait auf einen Task ohne Wartbares in loop(-1) haengt nicht.
    // PRUEFUNG_OHNE_ICARUS: diesen Fall auslassen (um die uebrigen Proben gegen
    // einen Stand zu sehen, in dem er noch haengt).
    if (std::getenv("PRUEFUNG_OHNE_ICARUS") == nullptr && !ohne("icarus"))
    mitZeitwaechter("ICARUS: loop(-1){dowait(t)} mit t ohne wait", 20.0, [] {
        const bhed::Script s = lies(
            "task ( \"t\" )\r\n{\r\n\taffect ( \"door\", FLUSH )\r\n\t{\r\n\t\twait ( 100.000 );\r\n\t}\r\n}\r\n"
            "loop ( -1 )\r\n{\r\n\tdowait ( \"t\" );\r\n}\r\n");
        bhed::AblaufWelt w;
        const bhed::Ablauf a = bhed::simuliereAblauf(s, "", w);
        (void)a;
    });
    pruefe(true, "ICARUS: loop(-1){dowait(t)} kehrt zurueck");

    // 5b. Handbearbeitete Zeilen gehen beim Speichern nicht verloren.
    {
        const std::string raus = bhed::writeScript(lies("wait ( 1000.000 ); // warten auf die Tuer\r\n"));
        pruefe(raus.find("// warten auf die Tuer") != std::string::npos,
               "Leser: ein Kommentar hinter dem Befehl ueberlebt Laden und Speichern");
    }
    {
        const bhed::Script s = lies("affect ( \"tuer\", FLUSH )\r\n{\r\n\twait ( 1.000 );\r\n} // ende affect\r\n"
                                    "wait ( 2.000 );\r\n");
        std::size_t befehle = 0;
        for (const bhed::Node& n : s.nodes) {
            if (n.kind == bhed::Node::Kind::Command) { ++befehle; }
        }
        pruefe(befehle == 2 && !s.nodes.empty() && s.nodes[0].children.size() == 1,
               "Leser: '} // ende' schliesst den Block - wait(2) rutscht nicht hinein");
        pruefe(bhed::writeScript(s).find("// ende affect") != std::string::npos,
               "Leser: der Kommentar hinter '}' bleibt erhalten");
    }
    {
        const std::string raus = bhed::writeScript(lies("wait ( 1.000 );\r\nhier stand etwas ohne Klammer\r\n"));
        pruefe(raus.find("hier stand etwas ohne Klammer") != std::string::npos,
               "Leser: eine Zeile ohne Klammer verschwindet beim Speichern nicht");
    }

    // 6. Zahlenfeld mit Komma (deutsche Schreibweise): "1,5" im wait-Feld.
    {
        bhed::CommandDb db;
        bhed::Param p;
        p.kind = bhed::Param::Kind::Float;
        bhed::Node n;
        n.kind = bhed::Node::Kind::Command;
        n.name = "wait";
        n.args.push_back(bhed::argForParam(p, "1,5", db, nullptr));
        bhed::Script s;
        s.nodes.push_back(n);
        const bhed::Script z = lies(bhed::writeScript(s));
        pruefe(z.nodes.size() == 1 && z.nodes[0].args.size() == 1 && z.nodes[0].args[0].text == "1.5",
               "Zahlenfeld: \"1,5\" wird 1.5 - nicht zwei Argumente");
    }
    // 7. Text mit Anfuehrungszeichen UND Komma ueberlebt Schreiben und Lesen.
    {
        bhed::Node n;
        n.kind = bhed::Node::Kind::Command;
        n.name = "print";
        bhed::Arg a;
        a.kind = bhed::Arg::Kind::String;
        a.text = "sag \"hallo, du\" laut";
        n.args.push_back(a);
        bhed::Script s;
        s.nodes.push_back(n);
        const bhed::Script z = lies(bhed::writeScript(s));
        pruefe(z.nodes.size() == 1 && z.nodes[0].args.size() == 1 && z.nodes[0].args[0].text == a.text,
               "Text: Anfuehrungszeichen mit Komma darin bleibt EIN Argument");
    }

    // ---- NPC-Datei -------------------------------------------------------
    {
        bhed::NpcMap m;
        bhed::parseNpcFile("winzig\n{\n\tplayerModel jedi\n\tscale 0\n}\n", m);
        const auto it = m.find("winzig");
        pruefe(it != m.end() && it->second.skala[0] == 1.0F && it->second.skala[2] == 1.0F,
               "NPC: scale 0 gilt wie in der Engine als 'nicht skaliert' (CG_Player prueft != 0)");
    }

    // ---- Einstellungen ---------------------------------------------------
    {
        bhed::Settings s;
        for (int i = 0; i < 10; ++i) { s.gamePaths.push_back("C:/Spiel/Ordner" + std::to_string(i)); }
        s.lesezeichen["C:/a=b/skript.txt"] = "3,7";
        bhed::Settings z;
        (void)bhed::readSettings(bhed::writeSettings(s), z);
        pruefe(z.gamePaths.size() == 10, "Einstellungen: alle 10 Spielordner ueberleben Speichern und Laden");
        const auto lz = z.lesezeichen.find("C:/a=b/skript.txt");
        pruefe(lz != z.lesezeichen.end() && lz->second == "3,7",
               "Einstellungen: Lesezeichen eines Pfads mit '=' bleibt erhalten");
    }

    // ---- Kartenleser: kaputte Dateien ------------------------------------
    if (!ohne("ueberlauf")) {
        BspFlaeche f;
        f.firstVert = 0x7FFFFFF0;
        f.numVerts = 0x20;
        const std::string b = baueBsp({f}, 3);
        bhed::BspGeometry g;
        const bool ok = bhed::readBspGeometry(b, g);
        bool absturzFrei = true;
        if (ok) {
            const bhed::BspMesh m = bhed::buildMesh(g, 4, true);
            (void)m;
        }
        pruefe(absturzFrei && (!ok || g.surfaces.empty()),
               "BSP: firstVert+numVerts mit Ueberlauf wird abgelehnt (kein Lesen ausserhalb)");
    }
    if (!ohne("patch")) {
        BspFlaeche f;
        f.typ = 2;
        f.numVerts = 9;
        f.pw = 201;
        f.ph = 201;
        const std::string b = baueBsp({f}, 9);
        bhed::BspGeometry g;
        if (bhed::readBspGeometry(b, g)) {
            const bhed::BspMesh m = bhed::buildMesh(g, 4, true);
            pruefe(m.verts.size() < 1000, "BSP: Patch groesser als seine Ecken wird uebersprungen");
        } else {
            pruefe(true, "BSP: Patch groesser als seine Ecken wird abgelehnt");
        }
    }
    if (!ohne("lightmap")) {
        BspFlaeche f;
        f.lightmap = 0x7FFFFFFF;
        const std::string b = baueBsp({f}, 3);
        bhed::BspGeometry g;
        (void)bhed::readBspGeometry(b, g);
        long anfragen = 0;
        mitZeitwaechter("BSP: Lightmapnummer 0x7FFFFFFF", 20.0, [&] {
            (void)bhed::ladeExterneLightmaps(g, "maps/test", [&](const std::string&, std::string&) {
                ++anfragen;
                return false;
            });
        });
        pruefe(anfragen < 100000, "BSP: eine unsinnige Lightmapnummer fragt nicht Milliarden Dateien ab");
    }
    {
        const std::string b = baueBsp({BspFlaeche{}}, 3, true, 5);   // Brush 5 gibt es nicht
        bhed::BspGeometry g;
        (void)bhed::readBspGeometry(b, g);
        pruefe(g.nebel.size() == 1 && !g.nebel[0].gueltig,
               "BSP: Nebel mit ungueltigem Brush gilt nicht (Engine: ERR_DROP), statt wie globaler Nebel");
    }

    // ---- Skelett mit kaputtem Elternverweis ------------------------------
    //
    // gla.cpp uebernimmt `parent` ungeprueft; alle Stellen in gla.cpp
    // pruefen ihn, nur figurKnochen (mapview.cpp, Oberkoerper-Uebergang)
    // nicht. PRUEFUNG_OHNE=gla: auslassen (stuerzt im alten Stand ab).
    if (!ohne("gla")) {
        bhed::GlaAnimation g;
        g.numFrames = 1;
        g.bones.resize(2);
        g.bones[0].name = "lower_lumbar";
        g.bones[0].parent = -1;
        g.bones[1].name = "kaputt";
        g.bones[1].parent = 50000000;   // gibt es nicht
        g.bonePool.assign(14, 0);
        g.indexes.assign(2, 0);
        bhed::ActorDraw act;
        act.anim = &g;
        act.frame = 0;
        act.torsoPrevFrame = 0;
        act.torsoBlendLerp = 0.5F;
        std::vector<bhed::BoneMatrix> welt;
        std::vector<bhed::BoneMatrix> hilf;
        (void)bhed::figurKnochen(act, welt, hilf);
        pruefe(true, "Skelett: ein Elternverweis ausserhalb der Knochenliste stuerzt beim Oberkoerper-Uebergang nicht ab");
    }

    // ---- Modell- und Archivleser: riesige Groessenangaben ----------------
    {
        std::string b(108 + 108 + 64, '\0');
        b[0] = 'I'; b[1] = 'D'; b[2] = 'P'; b[3] = '3';
        setzeI32(b, 4, 15);
        setzeI32(b, 8 + 64 + 4, 1);     // numFrames
        setzeI32(b, 8 + 64 + 12, 1);    // numSurfaces
        setzeI32(b, 8 + 64 + 28, 108);  // ofsSurfaces
        setzeI32(b, 8 + 64 + 32, static_cast<std::int32_t>(b.size()));
        const std::size_t n = 108 + 4 + 64;
        setzeI32(b, 108, 0x33504449);
        setzeI32(b, n + 4, 0x7FFFFFFF);  // numFrames der Flaeche
        setzeI32(b, n + 12, 65536);      // numVerts
        setzeI32(b, n + 16, 0);
        setzeI32(b, n + 32, 108);
        setzeI32(b, n + 36, 108);
        bool ausnahme = false;
        try {
            bhed::Md3Model m;
            (void)bhed::readMd3(b, m);
        } catch (const std::bad_alloc&) {
            ausnahme = true;
        } catch (const std::length_error&) {
            ausnahme = true;
        }
        pruefe(!ausnahme, "MD3: eine Flaeche mit 2^31 Bildern fuehrt nicht zu bad_alloc (Absturz)");
    }
#ifdef _WIN32
    {
        const std::filesystem::path p = std::filesystem::temp_directory_path() / "behaved_pruefung.pk3";
        {
            std::ofstream f(p, std::ios::binary);
            std::string kopf(30, '\0');
            kopf[0] = 'P'; kopf[1] = 'K'; kopf[2] = 3; kopf[3] = 4;
            f << kopf << "x";
        }
        bhed::Pk3 arc;
        arc.path = p.string();
        bhed::Pk3Entry e;
        e.name = "x";
        e.compressedSize = 0xFFFFFFF0U;
        e.size = 1;
        e.method = 0;
        const std::size_t vorher = spitzenSpeicher();
        std::string aus;
        bool ausnahme = false;
        try {
            (void)bhed::readPk3File(arc, e, aus);
        } catch (const std::bad_alloc&) {
            ausnahme = true;
        }
        const std::size_t zusatz = spitzenSpeicher() - vorher;
        std::error_code ec;
        std::filesystem::remove(p, ec);
        pruefe(!ausnahme && zusatz < (std::size_t{512} << 20),
               "PK3: ein Eintrag mit 4 GB Laenge in einer kleinen Datei belegt keine 4 GB");
    }
#endif

    std::printf("%s\n", g_fehler == 0 ? "alle Proben bestanden" : "FEHLGESCHLAGEN");
    return g_fehler == 0 ? 0 : 1;
}
