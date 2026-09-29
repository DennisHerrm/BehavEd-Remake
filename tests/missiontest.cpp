// missiontest.cpp - Missionen finden
//
// Eine Mission ist kein Dateiformat, sondern ein Zusammenhang: Karte,
// Entities daneben, Skripte aus den Entities. Die Proben arbeiten mit
// erfundenen Dateinamen und einem eigenen Leser - so laufen sie ohne
// Spieldaten und pruefen genau die Verknuepfung.
#include "bhed/mission.h"

#include <cstddef>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {
int fails = 0;
void expect(const char* what, bool ok) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", what);
    if (!ok) { ++fails; }
}

// Ein erfundenes Archiv.
struct FakeArchive {
    std::map<std::string, std::string> files;

    [[nodiscard]] std::vector<std::string> names() const {
        std::vector<std::string> out;
        for (const auto& [k, v] : files) {
            out.push_back(k);
        }
        return out;
    }
    [[nodiscard]] bhed::FileReader reader() const {
        return [this](const std::string& n, std::string& out) {
            const auto it = files.find(n);
            if (it == files.end()) {
                return false;
            }
            out = it->second;
            return true;
        };
    }
};

const char* kEnt =
    "{\n\"classname\" \"target_scriptrunner\"\n"
    "\"targetname\" \"intro\"\n\"usescript\" \"md/intro\"\n}\n"
    "{\n\"classname\" \"NPC_spawner\"\n"
    "\"deathscript\" \"md/wache_tot\"\n}\n";
}  // namespace

int main() {
    // --- Der Normalfall ---------------------------------------------------
    {
        FakeArchive a;
        a.files["maps/md_test.bsp"] = "RBSP";
        a.files["maps/md_test.ent"] = kEnt;
        a.files["scripts/md/intro.txt"] = "";
        a.files["scripts/md/wache_tot.txt"] = "";

        const auto ms = bhed::findMissions(a.names(), a.reader());
        expect("eine Karte gefunden", ms.size() == 1);
        if (!ms.empty()) {
            expect("sie heisst md_test", ms[0].name == "md_test");
            expect("die .ent wurde erkannt", ms[0].entFile == "maps/md_test.ent");
            expect("zwei Skripte", ms[0].scripts.size() == 2);
            expect("sie ist spielbar", ms[0].playable());
            bool resolved = true;
            for (const bhed::Mission::Script& s : ms[0].scripts) {
                if (s.file.empty()) { resolved = false; }
            }
            expect("beide Skripte auf eine Datei aufgeloest", resolved);
        }
    }

    // --- Eine Karte ohne Skripte ist keine Mission ------------------------
    //
    // Der Fall aus zzz_The_Fear_of_a_Jedi.pk3: dort liegen zwei .bsp mit
    // IDENTISCHEM Inhalt - duel_jt_dojo und md_tfoaj_jedi. Nur die zweite
    // hat eine .ent daneben, und nur die ist die Mission.
    {
        FakeArchive a;
        a.files["maps/duell.bsp"] = "RBSP";
        a.files["maps/mission.bsp"] = "RBSP";
        a.files["maps/mission.ent"] = kEnt;
        a.files["scripts/md/intro.txt"] = "";
        a.files["scripts/md/wache_tot.txt"] = "";

        const auto ms = bhed::findMissions(a.names(), a.reader());
        expect("beide Karten aufgefuehrt", ms.size() == 2);
        // Spielbare zuerst.
        expect("die Mission steht vorn",
               !ms.empty() && ms[0].name == "mission" && ms[0].playable());
        expect("die Duellkarte ist nicht spielbar",
               ms.size() == 2 && !ms[1].playable());
    }

    // --- Die Endung des Skripts ist nicht verlaesslich --------------------
    //
    // In Movie Duels liegen .txt und .IBI nebeneinander, und die
    // Schreibweise wechselt.
    {
        FakeArchive a;
        a.files["maps/m.bsp"] = "RBSP";
        a.files["maps/m.ent"] = kEnt;
        a.files["scripts/md/intro.IBI"] = "";      // gross
        a.files["scripts/md/wache_tot.icarus"] = "";

        const auto ms = bhed::findMissions(a.names(), a.reader());
        expect("auch .IBI und .icarus werden gefunden",
               !ms.empty() && ms[0].scripts.size() == 2 &&
                   !ms[0].scripts[0].file.empty() &&
                   !ms[0].scripts[1].file.empty());
    }

    // --- Ein Verweis ohne Datei ------------------------------------------
    {
        FakeArchive a;
        a.files["maps/m.bsp"] = "RBSP";
        a.files["maps/m.ent"] = kEnt;
        // keine Skriptdateien

        const auto ms = bhed::findMissions(a.names(), a.reader());
        expect("der Verweis bleibt stehen",
               !ms.empty() && ms[0].scripts.size() == 2);
        expect("aber ohne Datei",
               !ms.empty() && ms[0].scripts[0].file.empty());
    }

    // --- Mehrere Missionen in EINEM Archiv --------------------------------
    //
    // Das ist der Normalfall, nicht die Ausnahme: ein Mod bringt mehrere
    // Missionen mit, je eine .ent je Karte. Der erste Anlauf lud stumm die
    // einzige gefundene - bei mehreren muss gefragt werden.
    {
        FakeArchive a;
        a.files["maps/eins.bsp"] = "RBSP";
        a.files["maps/eins.ent"] =
            "{\n\"classname\" \"target_scriptrunner\"\n"
            "\"usescript\" \"md/eins\"\n}\n";
        a.files["maps/zwei.bsp"] = "RBSP";
        a.files["maps/zwei.ent"] =
            "{\n\"classname\" \"target_scriptrunner\"\n"
            "\"usescript\" \"md/zwei\"\n}\n";
        a.files["maps/duell.bsp"] = "RBSP";
        a.files["scripts/md/eins.txt"] = "";
        a.files["scripts/md/zwei.txt"] = "";

        const auto ms = bhed::findMissions(a.names(), a.reader());
        expect("drei Karten aufgefuehrt", ms.size() == 3);
        int playable = 0;
        for (const bhed::Mission& m : ms) {
            if (m.playable()) { ++playable; }
        }
        expect("zwei davon spielbar", playable == 2);
        expect("die spielbaren stehen vorn",
               ms.size() == 3 && ms[0].playable() && ms[1].playable() &&
                   !ms[2].playable());
        // Jede Mission muss IHR eigenes Skript haben, nicht das der anderen.
        expect("jede Mission hat ihr eigenes Skript",
               ms.size() == 3 && ms[0].scripts.size() == 1 &&
                   ms[1].scripts.size() == 1 &&
                   ms[0].scripts[0].ref != ms[1].scripts[0].ref);
    }

    // --- Nichts drin ------------------------------------------------------
    {
        FakeArchive a;
        a.files["readme.txt"] = "";
        expect("ohne Karte keine Mission",
               bhed::findMissions(a.names(), a.reader()).empty());

        // Ohne Leser darf nichts knallen.
        expect("ohne Leser kein Ergebnis",
               bhed::findMissions({"maps/x.bsp"}, nullptr).empty());
    }

    std::printf("\n%s (%d Fehlschlaege)\n",
                fails != 0 ? "FEHLGESCHLAGEN" : "alle Missionsproben bestanden",
                fails);
    return fails != 0 ? 1 : 0;
}
