// Probe fuer die Mover-Simulation (bhed/mover.h).
//
// Die Sollwerte stammen aus OpenJK SP, code/game/g_mover.cpp:
//   SP_func_door: speed 400, wait 2 s, lip 8, pos2 = pos1 + dir*(|dir|.size - lip)
//   Use_BinaryMover_Go: "start moving 50 msec later"
//   Reached_BinaryMover: nach wait zurueck, TOGGLE wartet auf das naechste use
//   UnLockDoors: der erste use einer LOCKED-Tuer entsperrt nur
//   Think_SpawnNewDoorTrigger: Feld = Team-Kasten, duennste Achse +-120
// Und aus g_breakable.cpp / cg_effects.cpp (funcBBrushDieGo, misc_model_
// breakable_die, CG_MiscModelExplosion, CG_Chunks), Reached_Train,
// Use_Target_Print, Use_Target_Speaker und misc_dlight_use.
// Datenfrei: Karte und Brush-Modelle werden hier von Hand gebaut.

#include "bhed/mover.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace bhed;

namespace {

int fehler = 0;

void erwarte(const char* was, bool ok) {
    std::printf("  %-4s  %s\n", ok ? "ok" : "FEHL", was);
    if (!ok) {
        ++fehler;
    }
}

bool nah(float a, float b, float eps = 0.5F) { return std::fabs(a - b) <= eps; }

MapEntity ent(const std::string& klasse,
              std::vector<std::pair<std::string, std::string>> keys) {
    MapEntity e;
    e.classname = klasse;
    for (const auto& [k, v] : keys) {
        if (k == "targetname") { e.targetname = v; }
        if (k == "origin") { e.origin = v; }
    }
    keys.emplace_back("classname", klasse);
    e.keys = std::move(keys);
    return e;
}

// Modell n: ein Tuerblatt 8 breit (x), 64 lang (y), 128 hoch (z).
BspGeometry geometrie(int anzahl) {
    BspGeometry g;
    g.models.resize(static_cast<std::size_t>(anzahl) + 1);
    for (int i = 1; i <= anzahl; ++i) {
        auto& m = g.models[static_cast<std::size_t>(i)];
        m.mins[0] = 0; m.mins[1] = 0; m.mins[2] = 0;
        m.maxs[0] = 8; m.maxs[1] = 64; m.maxs[2] = 128;
    }
    return g;
}

}  // namespace

int main() {
    std::printf("mover\n");

    // --- Der Verlauf ------------------------------------------------------
    erwarte("nichtlinear: sin(90 Grad * Anteil)",
            nah(moverAnteil(50, 100, false), 0.7071F, 0.001F) &&
                nah(moverAnteil(50, 100, true), 0.5F, 0.001F) &&
                moverAnteil(0, 100, false) == 0.0F && moverAnteil(200, 100, false) == 1.0F);

    // --- Eine Tuer auf use ------------------------------------------------
    {
        MapData map;
        map.entities.push_back(ent("func_door", {{"model", "*1"}, {"angle", "90"},
                                                 {"targetname", "tuer"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g, {{1000.0, "tuer"}}, nullptr, 8000.0);
        // Weg 64 - 8 = 56 in +y, Dauer 56*1000/400 = 140 ms, ab 1050.
        erwarte("vor dem use: zu", nah(s.at(1, 900).origin[1], 0.0F));
        erwarte("50 ms nach dem use faengt sie an", nah(s.at(1, 1050).origin[1], 0.0F));
        erwarte("halb durch: sin(45 Grad) der Strecke",
                nah(s.at(1, 1120).origin[1], 56.0F * 0.7071F, 0.8F));
        erwarte("offen nach 140 ms: 56 in +y", nah(s.at(1, 1200).origin[1], 56.0F) &&
                                                  nah(s.at(1, 1200).origin[0], 0.0F));
        erwarte("bleibt wait = 2 s offen", nah(s.at(1, 3150).origin[1], 56.0F));
        erwarte("dann wieder zu", nah(s.at(1, 3500).origin[1], 0.0F));
        erwarte("Oeffnung gemeldet", s.oeffnungen().size() == 1U &&
                                          nah(static_cast<float>(s.oeffnungen()[0].abMs), 1050.0F));
    }
    // --- TOGGLE: bleibt offen bis zum naechsten use ------------------------
    {
        MapData map;
        map.entities.push_back(ent("func_door", {{"model", "*1"}, {"angle", "-1"},
                                                 {"targetname", "t"}, {"spawnflags", "8"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g, {{1000.0, "t"}, {6000.0, "t"}}, nullptr, 9000.0);
        // angle -1: hoch, Weg 128 - 8 = 120
        erwarte("angle -1 faehrt hoch", nah(s.at(1, 2000).origin[2], 120.0F));
        erwarte("TOGGLE: nach 4 s immer noch offen", nah(s.at(1, 5000).origin[2], 120.0F));
        erwarte("zweites use schliesst", nah(s.at(1, 7000).origin[2], 0.0F));
    }
    // --- LOCKED: der erste use entsperrt nur ------------------------------
    {
        MapData map;
        map.entities.push_back(ent("func_door", {{"model", "*1"}, {"angle", "90"},
                                                 {"targetname", "t"}, {"spawnflags", "16"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g, {{1000.0, "t"}, {3000.0, "t"}}, nullptr, 6000.0);
        erwarte("LOCKED: erster use oeffnet nicht", nah(s.at(1, 2000).origin[1], 0.0F));
        erwarte("zweiter use oeffnet", nah(s.at(1, 3500).origin[1], 56.0F));
    }
    // --- Team: beide Blaetter, targetname am zweiten ----------------------
    {
        MapData map;
        map.entities.push_back(ent("func_door", {{"model", "*1"}, {"angle", "90"}, {"team", "d"}}));
        map.entities.push_back(ent("func_door", {{"model", "*2"}, {"angle", "270"}, {"team", "d"},
                                                 {"targetname", "paar"}}));
        const BspGeometry g = geometrie(2);
        MoverSim s;
        s.baue(map, g, {{1000.0, "paar"}}, nullptr, 5000.0);
        erwarte("Team: das erste Blatt geht nach +y", nah(s.at(1, 1500).origin[1], 56.0F));
        erwarte("Team: das zweite nach -y", nah(s.at(2, 1500).origin[1], -56.0F));
    }
    // --- Ausloesefeld: eine Figur oeffnet ---------------------------------
    {
        MapData map;
        map.entities.push_back(ent("func_door", {{"model", "*1"}, {"angle", "90"}}));
        const BspGeometry g = geometrie(1);
        // Feld: x 0..8 um 120 erweitert (-120..128), y 0..64, z 0..128.
        const auto figur = [](double ms, const float mn[3], const float mx[3]) {
            const float p[3] = {-60.0F, 32.0F, 40.0F};   // steht vor der Tuer
            const bool drin = p[0] >= mn[0] && p[0] <= mx[0] && p[1] >= mn[1] && p[1] <= mx[1];
            return drin && ms >= 2000.0 && ms < 2500.0;
        };
        MoverSim s;
        s.baue(map, g, {}, figur, 8000.0);
        erwarte("ohne Figur zu", nah(s.at(1, 1900).origin[1], 0.0F));
        erwarte("Figur im Feld: sie geht auf", nah(s.at(1, 2300).origin[1], 56.0F));
        erwarte("haelt offen, solange sie drin steht, dann wait",
                nah(s.at(1, 4300).origin[1], 56.0F) && nah(s.at(1, 5000).origin[1], 0.0F));
    }
    // --- START_OPEN steht offen ------------------------------------------
    {
        MapData map;
        map.entities.push_back(ent("func_door", {{"model", "*1"}, {"angle", "90"},
                                                 {"targetname", "t"}, {"spawnflags", "1"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g, {{1000.0, "t"}}, nullptr, 2000.0);
        erwarte("START_OPEN: steht offen", nah(s.at(1, 500).origin[1], 56.0F));
        erwarte("use schliesst sie", nah(s.at(1, 1500).origin[1], 0.0F));
    }
    // --- func_wall START_OFF, fx_runner, target_delay ----------------------
    {
        MapData map;
        map.entities.push_back(ent("func_wall", {{"model", "*1"}, {"targetname", "holo"},
                                                 {"spawnflags", "1"}}));
        map.entities.push_back(ent("fx_runner", {{"targetname", "spritzer"}, {"spawnflags", "2"},
                                                 {"fxFile", "md2/lava_shot"}}));
        map.entities.push_back(ent("fx_runner", {{"targetname", "dampf"}, {"spawnflags", "1"},
                                                 {"fxFile", "volumetric/steam"}}));
        map.entities.push_back(ent("target_delay", {{"targetname", "spaeter"}, {"target", "holo"},
                                                    {"wait", "2"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g,
               {{1000.0, "holo"}, {2000.0, "spritzer"}, {3000.0, "dampf"},
                {5000.0, "dampf"}, {6000.0, "spaeter"}},
               nullptr, 9000.0);
        erwarte("func_wall START_OFF: erst unsichtbar", !s.at(1, 500).sichtbar);
        erwarte("use macht es sichtbar", s.at(1, 1500).sichtbar);
        erwarte("target_delay schaltet 2 s spaeter wieder aus",
                s.at(1, 7900).sichtbar && !s.at(1, 8100).sichtbar);
        const auto& fx = s.fxStarts();
        erwarte("ONESHOT-Runner einmal bei 2000", fx.size() == 2U && fx[0].oneShot &&
                                                      nah(static_cast<float>(fx[0].ms), 2000.0F));
        erwarte("START_OFF-Runner von 3000 bis 5000",
                fx.size() == 2U && !fx[1].oneShot && nah(static_cast<float>(fx[1].ms), 3000.0F) &&
                    nah(static_cast<float>(fx[1].bisMs), 5000.0F));
    }
    // --- func_rotating und func_plat -------------------------------------
    {
        MapData map;
        map.entities.push_back(ent("func_rotating", {{"model", "*1"}, {"spawnflags", "1"},
                                                     {"speed", "90"}}));
        map.entities.push_back(ent("func_plat", {{"model", "*2"}}));
        const BspGeometry g = geometrie(2);
        MoverSim s;
        s.baue(map, g, {}, nullptr, 3000.0);
        erwarte("func_rotating START_ON: 90 Grad je Sekunde um die Hochachse",
                nah(s.at(1, 1000).angles[1], 90.0F));
        erwarte("func_plat ruht unten (Hoehe - lip = 120)", nah(s.at(2, 0).origin[2], -120.0F));
    }

    // --- func_rotating auf use: der Winkel bleibt beim Abschalten stehen ---
    {
        MapData map;
        map.entities.push_back(ent("func_rotating", {{"model", "*1"}, {"speed", "90"},
                                                     {"targetname", "rad"}, {"soundSet", "impdoor1"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g, {{1000.0, "rad"}, {2000.0, "rad"}}, nullptr, 5000.0);
        erwarte("func_rotating ohne START_ON steht", nah(s.at(1, 900).angles[1], 0.0F));
        erwarte("nach use dreht es", nah(s.at(1, 1500).angles[1], 45.0F));
        erwarte("abgeschaltet bleibt der Winkel stehen (nicht zurueck auf 0)",
                nah(s.at(1, 4000).angles[1], 90.0F));
        const std::vector<MoverKlang> k = s.klaenge(5000.0);
        erwarte("func_rotating: START und Schleife beim Einschalten, END beim Abschalten",
                k.size() == 3U && k[0].stufe == 0 && nah(static_cast<float>(k[0].ms), 1000.0F) &&
                    k[1].stufe == 1 && nah(static_cast<float>(k[1].bisMs), 2000.0F) &&
                    k[2].stufe == 2 && nah(static_cast<float>(k[2].ms), 2000.0F));
    }
    // --- func_bobbing auf use: jeder Zeitpunkt mit SEINEM Schaltzustand ---
    {
        MapData map;
        map.entities.push_back(ent("func_bobbing", {{"model", "*1"}, {"spawnflags", "4"},
                                                    {"height", "32"}, {"speed", "4"},
                                                    {"targetname", "wipp"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g, {{2000.0, "wipp"}}, nullptr, 6000.0);
        erwarte("func_bobbing START_OFF steht bis zum use", nah(s.at(1, 1000).origin[2], 0.0F));
        // Eingeschaltet bei 2000 mit Phase 0: nach einer Viertelperiode
        // (1 s) ganz oben.
        erwarte("... und wippt danach (Viertelperiode: ganz oben)",
                nah(s.at(1, 3000).origin[2], 32.0F));
    }
    // --- Tuerklaenge: START + Schleife bei der Abfahrt, END bei Ankunft ----
    {
        MapData map;
        map.entities.push_back(ent("func_door", {{"model", "*1"}, {"angle", "90"},
                                                 {"targetname", "tuer"}, {"soundSet", "impdoor1"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g, {{1000.0, "tuer"}}, nullptr, 8000.0);
        const std::vector<MoverKlang> k = s.klaenge(8000.0);
        // Auf 1050..1190, zurueck ab 3190..3330.
        erwarte("Tuer: sechs Klaenge (auf und zu je START, Schleife, END)", k.size() == 6U);
        if (k.size() == 6U) {
            erwarte("START bei 1050", k[0].stufe == 0 && nah(static_cast<float>(k[0].ms), 1050.0F));
            erwarte("Schleife 1050..1190", k[1].stufe == 1 && nah(static_cast<float>(k[1].bisMs), 1190.0F));
            erwarte("END bei 1190", k[2].stufe == 2 && nah(static_cast<float>(k[2].ms), 1190.0F));
            erwarte("beim Schliessen wieder START bei 3190",
                    k[3].stufe == 0 && nah(static_cast<float>(k[3].ms), 3190.0F));
        }
        float ort[3];
        s.klangOrt(1, 2000.0, ort);
        erwarte("Klang aus der Mitte der offenen Tuer", nah(ort[0], 4.0F) && nah(ort[1], 32.0F + 56.0F) &&
                                                           nah(ort[2], 64.0F));
    }
    {
        MapData map;
        map.entities.push_back(ent("func_door", {{"model", "*1"}, {"angle", "90"},
                                                 {"targetname", "tuer"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g, {{1000.0, "tuer"}}, nullptr, 8000.0);
        erwarte("ohne soundSet: stumm", s.klaenge(8000.0).empty());
    }
    // --- sound/sound.txt ----------------------------------------------------
    {
        const auto sets = leseBmodelSets(
            "; Kommentar\r\n"
            "generalSet wind\r\nsubWaves ambience wind1\r\n\r\n"
            "bmodelSet ImpDoor1\r\n"
            "subWaves movers/doors door1start door1move door1stop door1start door1stop\r\n\r\n"
            "bmodelSet cart_roll\r\nsubWaves movers/objects cart_roll null null cart_roll null\r\n");
        const auto it = sets.find("impdoor1");
        erwarte("bmodelSet gelesen, Name ohne Gross/Klein",
                it != sets.end() && it->second.size() == 5U &&
                    it->second[0] == "sound/movers/doors/door1start.wav" &&
                    it->second[1] == "sound/movers/doors/door1move.wav" &&
                    it->second[2] == "sound/movers/doors/door1stop.wav");
        const auto c = sets.find("cart_roll");
        erwarte("\"null\" heisst: kein Klang", c != sets.end() && c->second.size() == 5U &&
                                                    c->second[1].empty() && !c->second[0].empty());
        erwarte("generalSet ist kein Moverklang", sets.find("wind") == sets.end());
    }

    // =====================================================================
    // Was ein `use` sonst an der Karte bewirkt
    //
    // Sollwerte aus Movie Duels SP, code/game/g_breakable.cpp,
    // cgame/cg_effects.cpp, game/g_mover.cpp (func_train), g_target.cpp
    // (target_print, target_speaker), g_misc.cpp (light).
    // =====================================================================

    // --- func_breakable: Metall, Ziel dahinter --------------------------
    {
        MapData map;
        map.entities.push_back(ent("func_breakable", {{"model", "*1"}, {"targetname", "kiste"},
                                                      {"target", "danach"}}));
        map.entities.push_back(ent("func_wall", {{"model", "*2"}, {"targetname", "danach"},
                                                 {"spawnflags", "1"}}));
        const BspGeometry g = geometrie(2);
        MoverSim s;
        s.baue(map, g, {{1000.0, "kiste"}}, nullptr, 6000.0);
        erwarte("func_breakable steht vor dem use", s.at(1, 900).sichtbar && s.at(1, 900).bekannt);
        // thinkF_G_FreeEntity 50 ms nach dem Bruch
        erwarte("... und im Bild des Bruchs noch (G_FreeEntity ein Bild spaeter)", s.at(1, 1000).sichtbar);
        erwarte("ab 1050 ist der Brush weg", !s.at(1, 1050).sichtbar && !s.at(1, 5000).sichtbar);
        erwarte("sein target feuert beim Bruch (func_wall wird sichtbar)",
                !s.at(2, 900).sichtbar && s.at(2, 1000).sichtbar);
        const auto& br = s.brueche();
        // Kasten 10 x 66 x 130 (mit der Einheit aus SV_LinkEntity):
        // sqrt(sqrt(85800)) * 1.75 = 29.95 -> Groesse 1
        erwarte("ein Bruch bei 1000, Groesse 1", br.size() == 1U && nah(static_cast<float>(br[0].ms), 1000.0F) &&
                                                     br[0].groesse == 1);
        erwarte("18 bis 23 Stuecke (Q_flrand * 6 + 18)", br.size() == 1U && br[0].stuecke >= 18 && br[0].stuecke <= 23);
        // MAT_METAL: chunks/metalexplode, 2 + 7 * Groesse = 9 Stueck
        bool fxOk = s.bruchEffekte().size() == 9U;
        for (const BruchEffekt& f : s.bruchEffekte()) {
            fxOk = fxOk && f.effekt == "chunks/metalexplode" && nah(static_cast<float>(f.ms), 1000.0F) &&
                   f.ort[0] >= -1.0F && f.ort[0] <= 9.0F && f.ort[2] >= -1.0F && f.ort[2] <= 129.0F;
        }
        erwarte("CG_MiscModelExplosion: 9 x chunks/metalexplode im Kasten", fxOk);
        bool stueckOk = br.size() == 1U && static_cast<int>(s.truemmer().size()) == br[0].stuecke;
        bool faellt = true;
        for (const Truemmer& t : s.truemmer()) {
            stueckOk = stueckOk && t.modell.rfind("models/chunks/metal/metal2_", 0) == 0 &&
                       t.bisMs >= t.abMs + 1300.0 && t.bisMs <= t.abMs + 2200.0;
            // ohne Boden: nach 1,2 s liegt jedes Stueck tiefer als am Anfang
            // (hoechstens 375 * 0.8 nach oben, 800 Schwere)
            faellt = faellt && truemmerAt(t, t.abMs + 1200.0).ort[2] < t.flug.front().ort[2];
        }
        erwarte("CG_Chunks: MAT_METAL wirft metal2_N (CHUNK_METAL1, vertauscht), 1,3..2,2 s", stueckOk);
        erwarte("die Stuecke fallen mit g_gravity 800", faellt);
        if (!s.truemmer().empty()) {
            const Truemmer& t = s.truemmer().front();
            erwarte("Stueck verblasst die letzte Sekunde, danach weg",
                    nah(truemmerAt(t, t.bisMs - 500.0).deckkraft, 0.5F, 0.01F) &&
                        !truemmerAt(t, t.bisMs + 1.0).sichtbar && !truemmerAt(t, t.abMs - 1.0).sichtbar);
        }
        const auto& k = s.bruchKlaenge();
        erwarte("Klang des Metalls: glasslcar bei 1000",
                k.size() == 1U && k[0].datei == "sound/weapons/explosions/glasslcar.wav" &&
                    nah(static_cast<float>(k[0].ms), 1000.0F));
        const std::vector<std::string> neben = s.nebenModelle();
        erwarte("die Stueckmodelle stehen zum Vorladen bereit", !neben.empty() &&
                                                                  neben[0].rfind("models/chunks/", 0) == 0);
    }
    // --- ... kommt das use aufgeloest, feuert der Nachbau die Ziele -------
    {
        MapData map;
        map.entities.push_back(ent("func_breakable", {{"model", "*1"}, {"targetname", "kiste"},
                                                      {"target", "danach"}}));
        map.entities.push_back(ent("func_wall", {{"model", "*2"}, {"targetname", "danach"},
                                                 {"spawnflags", "1"}}));
        const BspGeometry g = geometrie(2);
        MoverSim s;
        s.baue(map, g, {{1000.0, "kiste", true}}, nullptr, 3000.0);
        erwarte("aufgeloest: bricht, feuert sein target aber NICHT noch einmal",
                !s.at(1, 1100).sichtbar && !s.at(2, 2000).sichtbar);
    }
    // --- Glas mit delay und splashDamage; USE_NOT_BREAK ------------------
    {
        MapData map;
        map.entities.push_back(ent("func_breakable", {{"model", "*1"}, {"targetname", "glas"},
                                                      {"material", "1"}, {"delay", "2.7"},
                                                      {"splashDamage", "10"}, {"splashRadius", "100"}}));
        map.entities.push_back(ent("func_breakable", {{"model", "*2"}, {"targetname", "schalter"},
                                                      {"spawnflags", "64"}, {"target", "licht"}}));
        map.entities.push_back(ent("func_wall", {{"model", "*3"}, {"targetname", "licht"},
                                                 {"spawnflags", "1"}}));
        const BspGeometry g = geometrie(3);
        MoverSim s;
        s.baue(map, g, {{1000.0, "glas"}, {1500.0, "schalter"}}, nullptr, 6000.0);
        // "delay" ist F_INT: 2.7 -> 2 Sekunden
        erwarte("delay 2.7 wird 2 s (F_INT): Bruch bei 3000",
                s.brueche().size() == 1U && nah(static_cast<float>(s.brueche()[0].ms), 3000.0F));
        erwarte("bis 3049 steht das Glas, ab 3050 nicht", s.at(1, 3049).sichtbar && !s.at(1, 3050).sichtbar);
        int glasFx = 0;
        for (const BruchEffekt& f : s.bruchEffekte()) {
            if (f.effekt == "chunks/glassbreak") { ++glasFx; }
        }
        erwarte("MAT_GLASS: 5 + 7 x chunks/glassbreak", glasFx == 12);
        erwarte("MAT_GLASS wirft keine Stuecke", s.truemmer().empty());
        bool cargo = false;
        bool glas = false;
        for (const KartenKlang& k : s.bruchKlaenge()) {
            cargo = cargo || k.datei == "sound/weapons/explosions/cargoexplode.wav";
            glas = glas || k.datei == "sound/weapons/explosions/glassbreak1.wav";
        }
        erwarte("splashDamage: cargoexplode, dazu glassbreak1", cargo && glas && s.bruchKlaenge().size() == 2U);
        erwarte("USE_NOT_BREAK: bleibt stehen und feuert nur sein target",
                s.at(2, 5000).sichtbar && s.at(3, 1500).sichtbar && !s.at(3, 1400).sichtbar);
    }
    // --- misc_model_breakable: Schadensmodell, _c1, target3 ---------------
    {
        MapData map;
        map.entities.push_back(ent("misc_model_breakable",
                                   {{"model", "models/map_objects/Kiste.md3"}, {"origin", "100 0 0"},
                                    {"targetname", "kiste"}, {"health", "10"}, {"material", "11"},
                                    {"target", "wand"}, {"target3", "wand3"}}));
        map.entities.push_back(ent("func_wall", {{"model", "*1"}, {"targetname", "wand"}, {"spawnflags", "1"}}));
        map.entities.push_back(ent("func_wall", {{"model", "*2"}, {"targetname", "wand3"}, {"spawnflags", "1"}}));
        map.entities.push_back(ent("misc_model_breakable", {{"model", "models/a.md3"}, {"origin", "0 0 0"},
                                                            {"targetname", "nackt"}}));
        map.entities.push_back(ent("misc_model_breakable", {{"model", "models/b.md3"}, {"origin", "0 0 0"},
                                                            {"targetname", "spaeter"},
                                                            {"spawnflags", "4192"}}));
        const BspGeometry g = geometrie(2);
        MoverSim s;
        s.dateiDa = [](const std::string& p) { return p == "models/map_objects/kiste_c1.md3"; };
        s.baue(map, g, {{500.0, "kiste"}, {1500.0, "kiste"}, {500.0, "nackt"}, {800.0, "nackt"},
                        {700.0, "spaeter"}, {900.0, "spaeter"}},
               nullptr, 6000.0);
        const MoverSim::ModellStand vor = s.modellAt(0, 400.0);
        const MoverSim::ModellStand nach = s.modellAt(0, 600.0);
        erwarte("misc_model_breakable: vorher das Kartenmodell",
                vor.bekannt && vor.sichtbar && vor.modell == "models/map_objects/kiste.md3");
        erwarte("... nach dem Bruch das _d1-Modell, angehalten",
                nach.sichtbar && nach.modell == "models/map_objects/kiste_d1.md3" && nach.angehalten);
        erwarte("target beim Bruch, target3 beim use danach (\"used while broken\")",
                s.at(1, 500).sichtbar && !s.at(2, 1400).sichtbar && s.at(2, 1500).sichtbar);
        int kisteBrueche = 0;
        int nacktBrueche = 0;
        for (const Bruch& b : s.brueche()) {
            if (b.entity == 0U) { ++kisteBrueche; }
            if (b.entity == 3U) { ++nacktBrueche; }
        }
        erwarte("mit health bricht es nur einmal", kisteBrueche == 1);
        bool c1 = false;
        bool nurC1 = true;
        for (const Truemmer& t : s.truemmer()) {
            if (t.entity != 0U) { continue; }
            c1 = true;
            nurC1 = nurC1 && t.modell == "models/map_objects/kiste_c1.md3";
        }
        erwarte("CG_Chunks nimmt das _c1-Modell, wenn es die Datei gibt", c1 && nurC1);
        bool kiste = false;
        bool cargo = false;
        for (const KartenKlang& k : s.bruchKlaenge()) {
            if (k.entity != 0U) { continue; }
            kiste = kiste || k.datei.rfind("sound/weapons/explosions/crateBust", 0) == 0;
            cargo = cargo || (k.datei == "sound/weapons/explosions/cargoexplode.wav" && nah(k.ort[0], 100.0F));
        }
        erwarte("Klang: crateBust und cargoexplode am Ursprung", kiste && cargo);
        erwarte("ohne health: unsichtbar (modelindex2 = 0) und bricht beim naechsten use wieder",
                !s.modellAt(3, 600.0).sichtbar && s.modellAt(3, 400.0).sichtbar && nacktBrueche == 2);
        erwarte("MAT_NONE (Vorgabe 8): keine Stuecke", [&] {
            for (const Truemmer& t : s.truemmer()) {
                if (t.entity == 3U) { return false; }
            }
            return true;
        }());
        erwarte("Start off + USE_NOT_BREAK + Benutzmodell: erst unsichtbar, dann _u1, dann wieder das Modell",
                !s.modellAt(4, 600.0).sichtbar && s.modellAt(4, 800.0).modell == "models/b_u1.md3" &&
                    s.modellAt(4, 1000.0).modell == "models/b.md3");
        bool d1 = false;
        for (const std::string& n : s.nebenModelle()) { d1 = d1 || n == "models/map_objects/kiste_d1.md3"; }
        erwarte("das _d1-Modell steht zum Vorladen bereit", d1);
    }
    // --- Bruchstuecke prallen am Boden ab und bleiben liegen ------------
    {
        MapData map;
        map.entities.push_back(ent("func_breakable", {{"model", "*1"}, {"targetname", "fels"},
                                                      {"material", "5"}}));
        BspGeometry g = geometrie(1);
        // Ein Boden bei z = -40 fuer traceRay: ein Knoten, ein Blatt, eine Flaeche.
        g.planes.push_back(BspGeometry::Plane{{0.0F, 0.0F, 1.0F}, -100000.0F});
        BspGeometry::Node knoten;
        knoten.plane = 0;
        knoten.children[0] = -1;
        knoten.children[1] = -1;
        g.nodes.push_back(knoten);
        BspGeometry::Leaf blatt;
        blatt.numSurfaces = 1;
        g.leafs.push_back(blatt);
        g.leafSurfaces.push_back(0);
        const float ecken[4][2] = {{-5000.0F, -5000.0F}, {5000.0F, -5000.0F}, {5000.0F, 5000.0F}, {-5000.0F, 5000.0F}};
        for (const auto& e : ecken) {
            BspVertex v;
            v.xyz[0] = e[0];
            v.xyz[1] = e[1];
            v.xyz[2] = -40.0F;
            g.verts.push_back(v);
        }
        for (const std::uint32_t i : {0U, 1U, 2U, 0U, 2U, 3U}) { g.indexes.push_back(i); }
        BspSurface boden;
        boden.type = BspSurface::Type::Planar;
        boden.shader = -1;
        boden.numVerts = 4;
        boden.numIndexes = 6;
        boden.mins[0] = boden.mins[1] = -5000.0F;
        boden.maxs[0] = boden.maxs[1] = 5000.0F;
        boden.mins[2] = boden.maxs[2] = -40.0F;
        g.surfaces.push_back(boden);
        MoverSim s;
        s.baue(map, g, {{1000.0, "fels"}}, nullptr, 5000.0);
        bool nieDrunter = !s.truemmer().empty();
        int liegen = 0;
        bool felsModell = true;
        for (const Truemmer& t : s.truemmer()) {
            for (double ms = t.abMs; ms < t.bisMs; ms += 25.0) {
                nieDrunter = nieDrunter && truemmerAt(t, ms).ort[2] >= -41.0F;
            }
            if (t.liegtAbMs >= 0.0) { ++liegen; }
            felsModell = felsModell && t.modell.rfind("models/chunks/rock/rock2_", 0) == 0;
        }
        erwarte("kein Stueck faellt durch den Boden", nieDrunter);
        erwarte("Stuecke prallen ab und bleiben liegen (CG_ReflectVelocity)", liegen > 0);
        erwarte("MAT_LT_STONE wirft rock2_N", felsModell);
        bool wand = false;
        for (const KartenKlang& k : s.bruchKlaenge()) { wand = wand || k.datei == "sound/effects/wall_smash.wav"; }
        erwarte("Stein: wall_smash", wand);
    }
    // --- func_train entlang der path_corner -------------------------------
    {
        MapData map;
        map.entities.push_back(ent("func_train", {{"model", "*1"}, {"origin", "-500 0 0"},
                                                  {"targetname", "zug"}, {"target", "p1"},
                                                  {"linear", "1"}}));
        map.entities.push_back(ent("path_corner", {{"origin", "0 0 0"}, {"targetname", "p1"}, {"target", "p2"}}));
        map.entities.push_back(ent("path_corner", {{"origin", "200 0 0"}, {"targetname", "p2"}, {"target", "p3"},
                                                   {"wait", "1"}, {"spawnflags", "1"}}));
        map.entities.push_back(ent("path_corner", {{"origin", "200 100 0"}, {"targetname", "p3"}}));
        map.entities.push_back(ent("func_train", {{"model", "*2"}, {"origin", "0 0 0"}, {"target", "q1"}}));
        map.entities.push_back(ent("path_corner", {{"origin", "0 0 0"}, {"targetname", "q1"}, {"target", "q2"}}));
        map.entities.push_back(ent("path_corner", {{"origin", "0 0 100"}, {"targetname", "q2"}, {"target", "q1"}}));
        const BspGeometry g = geometrie(2);
        MoverSim s;
        s.baue(map, g, {{1000.0, "zug"}}, nullptr, 8000.0);
        erwarte("func_train steht vor dem use an seinem origin", nah(s.at(1, 900).origin[0], -500.0F));
        erwarte("use: springt an p1 und faehrt los (speed 100, linear)",
                nah(s.at(1, 1000).origin[0], 0.0F) && nah(s.at(1, 2000).origin[0], 100.0F));
        erwarte("an p2 nach 2 s, wartet dort 1 s", nah(s.at(1, 3000).origin[0], 200.0F) &&
                                                     nah(s.at(1, 3500).origin[0], 200.0F) &&
                                                     nah(s.at(1, 3999).origin[1], 0.0F));
        erwarte("dann nach p3 (100 in 1 s)", nah(s.at(1, 4500).origin[1], 50.0F) &&
                                                  nah(s.at(1, 5000).origin[1], 100.0F));
        erwarte("an der letzten Ecke bleibt er stehen", nah(s.at(1, 7000).origin[0], 200.0F) &&
                                                           nah(s.at(1, 7000).origin[1], 100.0F));
        erwarte("TURN_TRAIN an p2: in 2 s auf Gier 90",
                nah(s.at(1, 3000).angles[1], 0.0F) && nah(s.at(1, 4000).angles[1], 45.0F) &&
                    nah(s.at(1, 5000).angles[1], 90.0F));
        erwarte("Zug ohne targetname faehrt von selbst (nichtlinear) im Kreis",
                nah(s.at(2, 500).origin[2], 100.0F * 0.7071F, 1.0F) && nah(s.at(2, 1000).origin[2], 100.0F) &&
                    nah(s.at(2, 2000).origin[2], 0.0F));
    }
    // --- target_print ---------------------------------------------------
    {
        MapData map;
        map.entities.push_back(ent("target_print", {{"targetname", "info"}, {"message", "Hallo\\nWelt"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g,
               {{1000.0, "info", false, "player"}, {2000.0, "info", false, "runner"},
                {2500.0, "info", false, "Player"}},
               nullptr, 8000.0);
        const Bildschirmtext* a = s.bildschirmtextAt(1500.0);
        erwarte("target_print vom Spieler: Text mit Zeilenumbruch (G_NewString)",
                a != nullptr && a->text == "Hallo\nWelt");
        erwarte("nicht vom Spieler ausgeloest: kommt nicht an (activator->client, Client 0)",
                s.bildschirmtexte().size() == 3U && !s.bildschirmtexte()[1].gezeigt &&
                    s.bildschirmtextAt(2200.0) != nullptr && nah(static_cast<float>(s.bildschirmtextAt(2200.0)->ms), 1000.0F));
        erwarte("ein neuer Text ersetzt den alten", s.bildschirmtextAt(2600.0) != nullptr &&
                                                       nah(static_cast<float>(s.bildschirmtextAt(2600.0)->ms), 2500.0F));
        erwarte("3 s zu sehen, die letzten 200 ms blendet er aus",
                s.bildschirmtextAt(5600.0) == nullptr &&
                    nah(bildschirmtextDeckkraft(s.bildschirmtexte()[2], 5400.0), 0.5F, 0.01F) &&
                    nah(bildschirmtextDeckkraft(s.bildschirmtexte()[2], 3000.0), 1.0F, 0.01F));
    }
    // --- target_speaker -------------------------------------------------
    {
        MapData map;
        map.entities.push_back(ent("target_speaker", {{"targetname", "summen"}, {"spawnflags", "1"},
                                                      {"noise", "sound/ambience/hum"}, {"origin", "1 2 3"}}));
        map.entities.push_back(ent("target_speaker", {{"targetname", "knall"}, {"spawnflags", "4"},
                                                      {"noise", "sound/explo.wav"}, {"wait", "2"}}));
        map.entities.push_back(ent("target_speaker", {{"targetname", "satz"}, {"soundSet", "x"},
                                                      {"noise", "sound/y.wav"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g,
               {{1000.0, "summen"}, {2000.0, "summen"}, {1000.0, "knall"}, {1500.0, "knall"},
                {3500.0, "knall"}, {1000.0, "satz"}},
               nullptr, 8000.0);
        const auto& l = s.lautsprecher();
        erwarte("vier Eintraege (Schleife aus, Knall, Schleife an, Knall)", l.size() == 4U);
        if (l.size() == 4U) {
            erwarte("looped-on laeuft ab 0 bis zum ersten use, .wav ergaenzt",
                    l[0].schleife && nah(static_cast<float>(l[0].ms), 0.0F) &&
                        nah(static_cast<float>(l[0].bisMs), 1000.0F) && l[0].datei == "sound/ambience/hum.wav" &&
                        nah(l[0].ort[2], 3.0F));
            erwarte("global: EV_GLOBAL_SOUND", !l[1].schleife && l[1].global && nah(static_cast<float>(l[1].ms), 1000.0F));
            erwarte("zweites use schaltet die Schleife wieder an (offen)",
                    l[2].schleife && nah(static_cast<float>(l[2].ms), 2000.0F) && l[2].bisMs < 0.0);
            erwarte("wait 2: das use bei 1500 geht verloren, das bei 3500 nicht",
                    nah(static_cast<float>(l[3].ms), 3500.0F));
        }
    }
    // --- rc604: notsingle/Schwierigkeit entfernt Mover und Lautsprecher ----
    // G_SpawnGEntityFromSpawnVars (code/game/g_spawn.cpp:1064-1075, 1096-1101
    // OpenJK; :1260-1265 MD) gibt eine Entity mit "notsingle" oder dem Bit
    // spawnflags & (1 << (8 + g_spskill)) frei, bevor ihre Spawnfunktion
    // laeuft - keine Tuer, kein Ausloesefeld, kein Lautsprecher.
    {
        MapData map;
        map.entities.push_back(ent("worldspawn", {}));
        map.entities.push_back(ent("func_door", {{"model", "*1"}, {"angle", "90"}, {"targetname", "tuer"},
                                                 {"spawnflags", "256"}}));
        map.entities.push_back(ent("func_door", {{"model", "*2"}, {"angle", "90"}, {"targetname", "tuer2"},
                                                 {"notsingle", "1"}}));
        map.entities.push_back(ent("func_door", {{"model", "*3"}, {"angle", "90"}, {"targetname", "tuer3"},
                                                 {"spawnflags", "512"}}));
        map.entities.push_back(ent("target_speaker", {{"targetname", "summen"}, {"spawnflags", "257"},
                                                      {"noise", "sound/ambience/hum"}}));
        const BspGeometry g = geometrie(3);
        MoverSim s;
        s.spielstufe = 0;   // g_spskill 0 (leicht)
        s.baue(map, g, {{1000.0, "tuer"}, {1000.0, "tuer2"}, {1000.0, "tuer3"}, {1000.0, "summen"}}, nullptr,
               4000.0);
        erwarte("leicht: Tuer mit Bit 256 gibt es nicht", !s.at(1, 1200.0).bekannt);
        erwarte("notsingle: Tuer gibt es nicht", !s.at(2, 1200.0).bekannt);
        erwarte("leicht: Tuer mit Bit 512 oeffnet", s.at(3, 1200.0).bekannt && nah(s.at(3, 1200.0).origin[1], 56.0F));
        erwarte("nur eine Oeffnung", s.oeffnungen().size() == 1U && s.oeffnungen()[0].modell == 3);
        erwarte("leicht: Lautsprecher mit Bit 256 schweigt", s.lautsprecher().empty());
        MoverSim m;
        m.spielstufe = 1;   // mittel
        m.baue(map, g, {{1000.0, "tuer"}, {1000.0, "tuer3"}}, nullptr, 4000.0);
        erwarte("mittel: Tuer mit Bit 256 oeffnet, die mit 512 fehlt",
                m.at(1, 1200.0).bekannt && nah(m.at(1, 1200.0).origin[1], 56.0F) && !m.at(3, 1200.0).bekannt);
    }
    // --- rc604: fx_target_beam auf use ------------------------------------
    // fx_target_beam_use (code/game/g_fx.cpp:1091-1109 OpenJK): ONE_SHOT (8)
    // feuert sofort einmal; sonst schaltet ein use think an (nextthink + 50)
    // oder aus. fx_target_beam_link (:1124-1167) laesst ohne STARTOFF (1) und
    // ONE_SHOT von selbst laufen; wait < 0 nimmt nach dem ersten Feuern die
    // use-Funktion weg (fx_target_beam_set_debounce, :994-1008).
    {
        MapData map;
        map.entities.push_back(ent("info_null", {{"targetname", "ziel"}, {"origin", "0 0 0"}}));
        map.entities.push_back(ent("fx_target_beam", {{"targetname", "aus"}, {"target", "ziel"}, {"spawnflags", "1"},
                                                      {"origin", "0 0 64"}}));
        map.entities.push_back(ent("fx_target_beam", {{"targetname", "schuss"}, {"target", "ziel"}, {"spawnflags", "8"},
                                                      {"origin", "0 0 64"}}));
        map.entities.push_back(ent("fx_target_beam", {{"targetname", "dauer"}, {"target", "ziel"}, {"origin", "0 0 64"}}));
        map.entities.push_back(ent("fx_target_beam", {{"targetname", "einmal"}, {"target", "ziel"}, {"spawnflags", "1"},
                                                      {"wait", "-1"}, {"origin", "0 0 64"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g,
               {{1000.0, "aus"}, {3000.0, "aus"}, {2000.0, "schuss"}, {1500.0, "dauer"}, {1000.0, "einmal"},
                {2000.0, "einmal"}},
               nullptr, 8000.0);
        const auto von = [&](std::size_t ei) {
            std::vector<FxRunnerStart> v;
            for (const FxRunnerStart& f : s.strahlStarts()) {
                if (f.entity == ei) { v.push_back(f); }
            }
            return v;
        };
        const auto a = von(1);
        erwarte("STARTOFF: use an (+50 ms), naechstes use aus",
                a.size() == 1U && nah(static_cast<float>(a[0].ms), 1050.0F) && nah(static_cast<float>(a[0].bisMs), 3000.0F) &&
                    !a[0].oneShot);
        const auto b = von(2);
        erwarte("ONE_SHOT: ein Schuss beim use", b.size() == 1U && b[0].oneShot && nah(static_cast<float>(b[0].ms), 2000.0F));
        const auto c = von(3);
        erwarte("ohne STARTOFF: laeuft ab dem Laden, use schaltet aus",
                c.size() == 1U && c[0].ms < 0.0 && nah(static_cast<float>(c[0].bisMs), 1500.0F));
        const auto d = von(4);
        erwarte("wait -1: nach dem ersten Feuern keine use-Funktion mehr",
                d.size() == 1U && nah(static_cast<float>(d[0].ms), 1050.0F) && d[0].bisMs < 0.0);
    }
    // --- light mit targetname: Lichtstil umschalten ---------------------
    {
        MapData map;
        map.entities.push_back(ent("light", {{"targetname", "lampe"}, {"style", "33"},
                                             {"switch_style", "5"}, {"spawnflags", "4"}}));
        map.entities.push_back(ent("light", {{"style", "34"}}));   // ohne Namen: gibt es nicht
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g, {{1000.0, "lampe"}}, nullptr, 3000.0);
        const auto& li = s.lichtSchaltungen();
        erwarte("light START_OFF: Stil 33 aus, auf use an mit dem Muster von switch_style",
                li.size() == 2U && li[0].stil == 33 && !li[0].an && li[0].musterVon == 0 && li[1].an &&
                    li[1].musterVon == 5 && nah(static_cast<float>(li[1].ms), 1000.0F));
    }

    // --- rc604: misc_trip_mine (laserTrapThink, WP_Explode, misc_trip_mine_activate) ---
    {
        LaserfalleEingabe e;
        e.ort[0] = 0.0F;
        e.richtung[0] = 1.0F;
        e.origin2[0] = 4.0F;
        e.anAbSpawn = true;
        e.spawnMs = -200.0;
        e.endeMs = 3000.0;
        int spuren = 0;
        float zuletztStart = -1.0F;
        float zuletztEnde = -1.0F;
        const auto still = [&](double, const float s[3], const float mn[3], const float mx[3], const float en[3]) {
            ++spuren;
            zuletztStart = s[0];
            zuletztEnde = en[0];
            LaserfalleSpur r;
            r.endpos[0] = 300.0F;
            (void)mn;
            (void)mx;
            return r;
        };
        const LaserfalleErgebnis a = laserfalleLauf(e, still);
        erwarte("Falle an ab Spawn: scharf und summen ein FRAMETIME spaeter, ein Strahl offen",
                a.scharfMs.size() == 1U && nah(static_cast<float>(a.scharfMs[0]), -100.0F) && a.summen.size() == 1U &&
                    a.summen[0].second < 0.0 && a.strahlen.size() == 1U && a.strahlen[0].bisMs < 0.0 &&
                    nah(a.strahlen[0].ende[0], 300.0F) && a.explosionen.empty() && a.wegMs < 0.0 &&
                    nah(static_cast<float>(a.sichtbarAbMs), -200.0F));
        erwarte("laserTrapThink alle 100 ms von origin2 bis trBase + 2048 * movedir",
                spuren == 32 && nah(zuletztStart, 4.0F) && nah(zuletztEnde, 2048.0F));
        // Eine Figur im Strahl ab 1000 ms: WP_Explode im Denkbild 1000.
        const auto figur = [](double t, const float*, const float*, const float*, const float*) {
            LaserfalleSpur r;
            r.endpos[0] = t >= 950.0 ? 120.0F : 300.0F;
            r.figur = t >= 950.0;
            return r;
        };
        const LaserfalleErgebnis b = laserfalleLauf(e, figur);
        erwarte("Figur im Strahl: Explosion im naechsten Denkbild, Strahl und Summen enden, frei 50 ms spaeter",
                b.explosionen.size() == 1U && nah(static_cast<float>(b.explosionen[0]), 1000.0F) &&
                    b.strahlen.size() == 1U && nah(static_cast<float>(b.strahlen[0].bisMs), 1000.0F) &&
                    nah(static_cast<float>(b.summen[0].second), 1000.0F) && nah(static_cast<float>(b.wegMs), 1050.0F));
        // Ein Mover im Strahl ab 500 ms: pos4 kuerzer, kein Knall.
        const auto mover = [](double t, const float*, const float*, const float*, const float*) {
            LaserfalleSpur r;
            r.endpos[0] = (t >= 500.0 && t < 800.0) ? 150.0F : 300.0F;
            return r;
        };
        const LaserfalleErgebnis c = laserfalleLauf(e, mover);
        erwarte("Mover im Strahl: drei Stuecke 300 / 150 / 300, kein Knall",
                c.strahlen.size() == 3U && nah(static_cast<float>(c.strahlen[0].bisMs), 500.0F) &&
                    nah(c.strahlen[1].ende[0], 150.0F) && nah(static_cast<float>(c.strahlen[1].bisMs), 800.0F) &&
                    nah(c.strahlen[2].ende[0], 300.0F) && c.strahlen[2].bisMs < 0.0 && c.explosionen.empty());
        // START_OFF mit targetname: unsichtbar bis zum use, dann an; use schaltet aus und wieder an.
        LaserfalleEingabe f = e;
        f.anAbSpawn = false;
        f.sichtbarAbSpawn = false;
        f.hatUse = true;
        f.uses = {1000.0, 2000.0, 2500.0};
        const LaserfalleErgebnis d = laserfalleLauf(f, still);
        erwarte("START_OFF: sichtbar ab dem use, scharf 100 ms spaeter, use aus, use wieder scharf",
                nah(static_cast<float>(d.sichtbarAbMs), 1000.0F) && d.scharfMs.size() == 2U &&
                    nah(static_cast<float>(d.scharfMs[0]), 1100.0F) && nah(static_cast<float>(d.scharfMs[1]), 2600.0F) &&
                    d.summen.size() == 2U && nah(static_cast<float>(d.summen[0].second), 2000.0F) && d.strahlen.size() == 2U &&
                    nah(static_cast<float>(d.strahlen[0].abMs), 1100.0F) && nah(static_cast<float>(d.strahlen[0].bisMs), 2000.0F) &&
                    nah(static_cast<float>(d.strahlen[1].abMs), 2600.0F));
        LaserfalleEingabe g = f;
        g.hatUse = false;
        const LaserfalleErgebnis h = laserfalleLauf(g, still);
        erwarte("ohne targetname keine use-Funktion", h.scharfMs.empty() && h.sichtbarAbMs >= 1.0e299);
    }
    // rc605 (fix605): target_counter (g_target.cpp:579-641 OpenJK) in der Mover-
    // Simulation - die Ziele einer explodierenden misc_trip_mine (WP_Explode:
    // G_UseTargets, g_weapon.cpp:221-224 OpenJK) kommen unaufgeloest an (t3_byss:
    // je 2 bzw. 4 Fallen auf einen Zaehler, der ein func_usable schaltet).
    {
        MapData map;
        map.entities.push_back(ent("func_wall", {{"model", "*1"}, {"targetname", "holo"}, {"spawnflags", "1"}}));
        map.entities.push_back(ent("target_counter", {{"targetname", "zaehler"}, {"target", "holo"}, {"count", "2"}}));
        map.entities.push_back(ent("target_counter", {{"targetname", "zwei"}, {"target", "holo"}, {"target2", "holo"},
                                                      {"bounceCount", "1"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g, {{1000.0, "zaehler"}, {2000.0, "zaehler"}, {3000.0, "zaehler"}}, nullptr, 5000.0);
        erwarte("target_counter count 2: der erste use schaltet nichts",
                !s.at(1, 1500).sichtbar);
        erwarte("... der zweite feuert target (func_wall an), ein dritter nichts mehr (count 0)",
                s.at(1, 2500).sichtbar && s.at(1, 3500).sichtbar);
        MoverSim z;
        // ohne count: 2; target2 bei jedem use vor dem letzten; bounceCount 1: einmal von vorn.
        z.baue(map, g, {{1000.0, "zwei"}, {2000.0, "zwei"}, {3000.0, "zwei"}, {4000.0, "zwei"}}, nullptr, 6000.0);
        erwarte("target_counter target2 und bounceCount: an 1000, aus 2000, an 3000, aus 4000",
                z.at(1, 1500).sichtbar && !z.at(1, 2500).sichtbar && z.at(1, 3500).sichtbar && !z.at(1, 4500).sichtbar);
    }
    {
        MapData map;
        map.entities.push_back(ent("misc_trip_mine", {{"targetname", "falle"}, {"spawnflags", "4"}, {"origin", "0 0 0"}}));
        map.entities.push_back(ent("target_relay", {{"targetname", "relais"}, {"target", "falle"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g, {{1000.0, "falle"}, {2000.0, "relais"}}, nullptr, 3000.0);
        const auto& u = s.entityUses();
        erwarte("misc_trip_mine: die uses (auch ueber target_relay) fuer misc_trip_mine_activate",
                u.size() == 2U && u[0].first == 0U && nah(static_cast<float>(u[0].second), 1000.0F) &&
                    nah(static_cast<float>(u[1].second), 2000.0F));
    }
    {
        const float s[3] = {-100.0F, 0.0F, 0.0F};
        const float en[3] = {100.0F, 0.0F, 0.0F};
        const float mn[3] = {-4.0F, -4.0F, -4.0F};
        const float mx[3] = {4.0F, 4.0F, 4.0F};
        const float kmin[3] = {-16.0F, -16.0F, -24.0F};
        const float kmax[3] = {16.0F, 16.0F, 40.0F};
        const KastenSpur k = figurKastenSpur(s, en, mn, mx, kmin, kmax);
        const float s2[3] = {-100.0F, 30.0F, 0.0F};
        const float e2[3] = {100.0F, 30.0F, 0.0F};
        const KastenSpur k2 = figurKastenSpur(s2, e2, mn, mx, kmin, kmax);
        const float s3[3] = {0.0F, 0.0F, 0.0F};
        const KastenSpur k3 = figurKastenSpur(s3, en, mn, mx, kmin, kmax);
        erwarte("CM_TraceThroughBrush gegen den Figurenkasten: (80 - 0.125) / 200, daneben frei, innen startsolid",
                nah(k.fraction, 0.399375F, 0.0001F) && !k.startsolid && k2.fraction == 1.0F && k3.startsolid &&
                    !k3.allsolid);
    }
    {
        const std::vector<float> ecken = {0.0F, -10.0F, -10.0F, 0.0F, 10.0F, -10.0F, 0.0F, 0.0F, 10.0F};
        const std::vector<std::uint32_t> idx = {0, 1, 2};
        const float a[3] = {-50.0F, 0.0F, 0.0F};
        const float b[3] = {50.0F, 0.0F, 0.0F};
        const float a2[3] = {-50.0F, 30.0F, 0.0F};
        const float b2[3] = {50.0F, 30.0F, 0.0F};
        const float b3[3] = {-20.0F, 0.0F, 0.0F};
        const float a4[3] = {-50.0F, 13.0F, 0.0F};
        const float b4[3] = {50.0F, 13.0F, 0.0F};
        erwarte("G2_RadiusTracePolys: Treffer, daneben, davor endend, 3 neben der Ecke mit Radius 4",
                g2RadiusTreffer(ecken, idx, a, b, 4.0F) && !g2RadiusTreffer(ecken, idx, a2, b2, 4.0F) &&
                    !g2RadiusTreffer(ecken, idx, a, b3, 4.0F) && g2RadiusTreffer(ecken, idx, a4, b4, 4.0F));
    }

    // --- rc604: G2_TimingModel, ion_cannon_think, pas_think -------------------
    {
        KnochenAnim a;
        a.startMs = 1000.0;
        a.startFrame = 0;
        a.endFrame = 11;
        a.speed = 0.8F;
        int b0 = 0, n0 = 0, b1 = 0, n1 = 0, b2 = 0, n2 = 0, b3 = 0, n3 = 0;
        float l0 = 0, l1 = 0, l2 = 0, l3 = 0;
        g2Bild(a, 900.0, b0, n0, l0);
        g2Bild(a, 1125.0, b1, n1, l1);    // 2.5 * 0.8 = 2.0
        g2Bild(a, 1640.0, b2, n2, l2);    // 12.8 * 0.8 = 10.24 > 10 -> Ende
        g2Bild(a, 1590.0, b3, n3, l3);    // 11.8 * 0.8 = 9.44
        erwarte("G2_TimingModel FREEZE: vor dem Start Bild 0, dann startFrame + Zeit/50 * speed, am Ende endFrame - 1",
                b0 == 0 && n0 == 1 && l0 == 0.0F && b1 == 2 && n1 == 3 && nah(l1, 0.0F, 0.001F) && b2 == 10 && n2 == 10 &&
                    l2 == 0.0F && b3 == 9 && n3 == 10 && nah(l3, 0.44F, 0.001F));
    }
    {
        IonenkanoneEingabe e;
        e.spawnMs = -200.0;
        e.endeMs = 10000.0;
        e.random = 0.001F;   // praktisch ohne Zufall
        const IonenkanoneErgebnis a = ionenkanoneLauf(e);
        erwarte("ion_cannon_think ohne START_OFF: alle wait = 1500 ms ab spawn + wait",
                a.schuesse.size() == 6U && nah(static_cast<float>(a.schuesse[0]), 1300.0F) &&
                    nah(static_cast<float>(a.schuesse[5]), 8800.0F));
        IonenkanoneEingabe f = e;
        f.startOff = true;
        f.wait = 300.0F;   // -> 500
        f.uses = {1000.0, 2600.0};
        const IonenkanoneErgebnis b = ionenkanoneLauf(f);
        erwarte("START_OFF: use -> Schuss in FRAMETIME, dann alle 500 ms (wait mindestens 500), use -> aus",
                b.schuesse.size() == 4U && nah(static_cast<float>(b.schuesse[0]), 1100.0F) &&
                    nah(static_cast<float>(b.schuesse[3]), 2600.0F));
        IonenkanoneEingabe g = e;
        g.bursts = true;
        g.delay = 500.0F;   // -> 1000
        const IonenkanoneErgebnis c = ionenkanoneLauf(g);
        bool pausen = c.schuesse.size() >= 2U;
        int lang = 0;
        for (std::size_t i = 1; i < c.schuesse.size(); ++i) {
            const double d = c.schuesse[i] - c.schuesse[i - 1];
            if (std::fabs(d - 1500.0) > 1.0) {
                ++lang;
                pausen = pausen && std::fabs(std::fmod(d - 1500.0, 1000.0)) < 1.0;
            }
        }
        erwarte("BURSTS: Pausen von delay (mindestens 1000) zwischen den Salven", pausen && lang >= 1);
        erwarte("pas_think klappt ab spawn + 1000 auf; START_OFF erst nach dem use + 50; use vorher schaltet aus",
                nah(static_cast<float>(pasAufklappenMs(false, -200.0, {}, 5000.0)), 800.0F) &&
                    nah(static_cast<float>(pasAufklappenMs(true, -200.0, {2000.0}, 5000.0)), 2050.0F) &&
                    pasAufklappenMs(false, -200.0, {500.0}, 5000.0) < 0.0 && pasAufklappenMs(true, -200.0, {}, 5000.0) < 0.0);
        // rc605 FU4: der Schwenk ohne Feind. Aufklappen bei 800, das zweite
        // pas_think 1200 spaeter (2000 = level.time 3200): speed =
        // AngleNormalize360(90) = 90, diff_yaw = sin(3200 * 0.0001f + 150) * 2,
        // speed -= diff_yaw; danach alle 100 ms mit AngleNormalize360 davor.
        {
            const auto norm360 = [](float a) {
                return static_cast<float>((360.0 / 65536) * (static_cast<int>(a * (65536 / 360.0)) & 65535));
            };
            float speed = 90.0F;
            std::vector<float> soll;
            for (int lt = 3200; lt <= 3500; lt += 100) {
                speed = norm360(speed);
                const float d = static_cast<float>(std::sin(static_cast<float>(lt) * 0.0001F + 150.0F) * 2.0F);
                if (std::fabs(d) > 0.25F) { speed -= std::fabs(d) > 10.0F ? (d > 0.0F ? 10.0F : -10.0F) : d; }
                soll.push_back(speed - 90.0F);
            }
            const std::vector<PasSchwenk> s = pasSchwenkLauf(false, -200.0, {}, 2300.0, 90.0F, 150);
            bool ok = s.size() == 4;
            for (std::size_t k = 0; ok && k < 4; ++k) {
                ok = nah(static_cast<float>(s[k].ms), 2000.0F + 100.0F * static_cast<float>(k)) && std::fabs(s[k].gier - soll[k]) < 1.0e-4F;
            }
            std::string txt;
            for (const PasSchwenk& p : s) { txt += " " + std::to_string(static_cast<int>(p.ms)) + ":" + std::to_string(p.gier); }
            erwarte(("pas_think ohne Feind: Schwenk ab 2000 alle 100 ms, sin(level.time * 0.0001 + count) * 2 (" + txt + ")").c_str(), ok);
            // START_OFF, use 1000 (an: aufklappen 1050, Schwenk ab 2250), use
            // 2600 (aus), use 3000 (an: think 3050, schon aufgeklappt).
            const std::vector<PasSchwenk> u = pasSchwenkLauf(true, -200.0, {1000.0, 2600.0, 3000.0}, 3200.0, 0.0F, 150);
            std::string tu;
            for (const PasSchwenk& p : u) { tu += " " + std::to_string(static_cast<int>(p.ms)); }
            erwarte(("pas_use schaltet den Schwenk aus und an (" + tu + ")").c_str(),
                    u.size() == 6 && nah(static_cast<float>(u[0].ms), 2250.0F) && nah(static_cast<float>(u[3].ms), 2550.0F) &&
                        nah(static_cast<float>(u[4].ms), 3050.0F) && nah(static_cast<float>(u[5].ms), 3150.0F));
        }
    }

    // --- rc604: fx_target_beam - Abklingzeit, use, link ------------------------
    {
        ZielStrahlEingabe e;
        e.spawnMs = -200.0;
        e.endeMs = 1000.0;
        const std::vector<double> a = zielStrahlFeuer(e);
        erwarte("fx_target_beam ohne wait: link + FRAMETIME, dann alle 100 ms",
                a.size() == 11U && nah(static_cast<float>(a[0]), 0.0F) && nah(static_cast<float>(a[10]), 1000.0F));
        ZielStrahlEingabe f = e;
        f.waitMs = 150.0F;
        const std::vector<double> b = zielStrahlFeuer(f);
        erwarte("wait 150: das think prueft alle 100 ms - Schuesse alle 200 ms",
                b.size() == 6U && nah(static_cast<float>(b[1]), 200.0F) && nah(static_cast<float>(b[5]), 1000.0F));
        ZielStrahlEingabe g = e;
        g.waitMs = 1000.0F;
        g.endeMs = 3000.0;
        g.uses = {-150.0, 300.0, 500.0};
        const std::vector<double> c = zielStrahlFeuer(g);
        erwarte("use vor dem link trifft nichts; aus/an wartet die Abklingzeit ab (wieder ab 1000 + 0 statt 550)",
                c.size() == 3U && nah(static_cast<float>(c[0]), 0.0F) && nah(static_cast<float>(c[1]), 1050.0F) &&
                    nah(static_cast<float>(c[2]), 2050.0F));
        ZielStrahlEingabe h = e;
        h.oneShot = true;
        h.uses = {-150.0, 400.0};
        const std::vector<double> d = zielStrahlFeuer(h);
        erwarte("ONE_SHOT: feuert beim use sofort, vor dem link nicht", d.size() == 1U && nah(static_cast<float>(d[0]), 400.0F));
    }

    // --- rc604: fx_runner - die use-Funktion kommt erst mit fx_runner_link -----
    {
        MapData map;
        map.entities.push_back(ent("fx_runner", {{"targetname", "fx"}, {"spawnflags", "1"}, {"fxFile", "env/x"},
                                                 {"origin", "0 0 0"}}));
        const BspGeometry g = geometrie(1);
        MoverSim s;
        s.baue(map, g, {{100.0, "fx"}, {1000.0, "fx"}}, nullptr, 3000.0);
        const auto& f = s.fxStarts();
        erwarte("fx_runner: ein use vor fx_runner_link (Spawn + 400 ms) trifft nichts, das spaetere startet",
                f.size() == 1U && nah(static_cast<float>(f[0].ms), 1000.0F));
    }

    if (fehler != 0) {
        std::printf("FEHLGESCHLAGEN (%d)\n", fehler);
        return 1;
    }
    std::printf("alle Gegenproben bestanden (0 Fehlschlaege)\n");
    return 0;
}
