// missiontest.cpp - Missionen finden
//
// Eine Mission ist kein Dateiformat, sondern ein Zusammenhang: Karte,
// Entities daneben, Skripte aus den Entities. Die Proben arbeiten mit
// erfundenen Dateinamen und einem eigenen Leser - so laufen sie ohne
// Spieldaten und pruefen genau die Verknuepfung.
#include "bhed/mission.h"

#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
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

    // === rc603: die Missionen eines GANZEN Spiels ==========================
    //
    // Der Fall aus dem reinen JKA: die Karten liegen in assets0, das
    // gewaehlte Archiv (assets1) hat keine - trotzdem alle Missionen.
    {
        std::vector<bhed::SpielArchiv> spiel;
        spiel.push_back({"C:/jka/base/assets0.pk3",
                         {"maps/t1_sour.bsp", "maps/kor1.bsp", "maps/duell.bsp", "scripts/t1_sour/intro.IBI",
                          "scripts/kor1/a.ibi", "models/x.glm"}});
        spiel.push_back({"C:/jka/base/assets1.pk3", {"textures/a.jpg", "sound/b.mp3"}});
        // assets2 ueberschreibt eine Karte und bringt ein Skript, das in
        // assets0 fehlt (wie die Patches von Raven).
        spiel.push_back({"C:/jka/base/assets2.pk3", {"maps/KOR1.bsp", "scripts/t1_sour/spaet.ibi"}});
        const auto karten = bhed::missionsKarten(spiel, "c:\\JKA\\base\\assets1.pk3");
        expect("rc603: drei Karten des Spiels, auch aus assets1 gewaehlt", karten.size() == 3);
        const bhed::MissionsKarte* kor1 = nullptr;
        const bhed::MissionsKarte* sour = nullptr;
        for (const bhed::MissionsKarte& k : karten) {
            if (k.name == "KOR1" || k.name == "kor1") { kor1 = &k; }
            if (k.name == "t1_sour") { sour = &k; }
        }
        expect("rc603: kor1 einmal, aus dem spaeteren Archiv (assets2 gewinnt)",
               kor1 != nullptr && kor1->bspArchiv == 2 && kor1->bsp == "maps/KOR1.bsp");
        expect("rc603: keine Karte gilt als im gewaehlten assets1", sour != nullptr && !sour->imGewaehlten &&
                                                                         kor1 != nullptr && !kor1->imGewaehlten);
        const auto skripte = bhed::skriptNamen(spiel);
        const bhed::DateiGibt gibt = [&](const std::string& n) {
            std::string k = n;
            for (char& c : k) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            return skripte.count(k) != 0U;
        };
        std::vector<bhed::MapEntity> ents;
        bhed::parseEntities("{\n\"classname\" \"target_scriptrunner\"\n\"usescript\" \"t1_sour/intro\"\n}\n"
                            "{\n\"classname\" \"target_scriptrunner\"\n\"usescript\" \"t1_sour/spaet\"\n}\n",
                            ents);
        const bhed::Mission m = bhed::missionAus(*sour, bhed::kartenSkriptVerweise(ents), gibt);
        expect("rc603: Skripte aus jedem Archiv des Spiels aufgeloest (auch das aus assets2)",
               m.scripts.size() == 2 && m.scripts[0].file == "scripts/t1_sour/intro.ibi" &&
                   m.scripts[1].file == "scripts/t1_sour/spaet.ibi" && m.playable());
    }
    // Die .ent liegt in einem ANDEREN Archiv als die .bsp (Movie Duels:
    // MD_Maps_* / MD_Missions_*), und das gewaehlte Archiv bringt nur sie.
    {
        std::vector<bhed::SpielArchiv> spiel;
        spiel.push_back({"D:/md/MD/MD_Maps.pk3", {"maps/duel_x.bsp", "maps/md_a.bsp"}});
        spiel.push_back({"D:/md/MD/MD_Missions.pk3", {"maps/md_a.ent", "scripts/md_a/intro.txt"}});
        const auto karten = bhed::missionsKarten(spiel, "D:/md/MD/MD_Missions.pk3");
        const bhed::MissionsKarte* a = nullptr;
        for (const bhed::MissionsKarte& k : karten) {
            if (k.name == "md_a") { a = &k; }
        }
        expect("rc603: die .ent aus dem anderen Archiv gehoert zur Karte, und die Mission steht im gewaehlten",
               a != nullptr && a->ent == "maps/md_a.ent" && a->entArchiv == 1 && a->bspArchiv == 0 && a->imGewaehlten);
        std::vector<bhed::Mission> ms;
        bhed::Mission duell;
        duell.name = "aaa_duell";
        ms.push_back(duell);
        bhed::Mission fremd;
        fremd.name = "b_fremd";
        fremd.scripts.push_back({});
        ms.push_back(fremd);
        bhed::Mission eigen;
        eigen.name = "z_eigen";
        eigen.scripts.push_back({});
        eigen.imGewaehlten = true;
        ms.push_back(eigen);
        bhed::sortiereMissionen(ms);
        expect("rc603: Reihenfolge - erst die des gewaehlten Archivs, dann spielbare, dann Duellkarten",
               ms[0].name == "z_eigen" && ms[1].name == "b_fremd" && ms[2].name == "aaa_duell");
    }
    // Welche Spielordner gehoeren zu einem Archiv?
    {
        const std::vector<std::string> eingerichtet = {"C:\\Spiele\\JKA MD\\base", "C:\\Spiele\\JKA MD\\MD"};
        const auto hatPk3 = [](const std::string& o) {
            return bhed::gleicherOrdner(o, "C:\\Spiele\\JKA MD\\base") || bhed::gleicherOrdner(o, "C:/Spiele/JK2/GameData/base") ||
                   bhed::gleicherOrdner(o, "C:\\Spiele\\JKA\\GameData\\base");
        };
        using Art = bhed::SpielordnerWahl::Art;
        const auto w1 = bhed::spielordnerFuerArchiv("c:/spiele/jka md/md/MD_Missions_Ep3.pk3", eingerichtet, {}, hatPk3);
        expect("rc603: Archiv eines eingerichteten Ordners (andere Schreibweise) - die eingerichteten Ordner",
               w1.art == Art::Eingerichtet && w1.ordner == eingerichtet);
        const auto w2 = bhed::spielordnerFuerArchiv("C:/Spiele/JK2/GameData/base/assets1.pk3", eingerichtet, {}, hatPk3);
        expect("rc603: base eines anderen Spiels (JK2) - nur dieser Ordner, nichts gemischt",
               w2.art == Art::Installation && w2.ordner.size() == 1 && w2.ordner[0] == "C:/Spiele/JK2/GameData/base");
        const auto w3 = bhed::spielordnerFuerArchiv("C:\\Spiele\\JKA\\GameData\\meinemod\\zz_x.pk3", eingerichtet, {}, hatPk3);
        expect("rc603: Mod einer anderen Installation - deren base, dann die Mod (FS_Startup)",
               w3.art == Art::Installation && w3.ordner.size() == 2 && w3.ordner[0] == "C:\\Spiele\\JKA\\GameData\\base" &&
                   w3.ordner[1] == "C:\\Spiele\\JKA\\GameData\\meinemod");
        const auto w4 = bhed::spielordnerFuerArchiv("C:\\Downloads\\zzz_mission.pk3", eingerichtet, {}, hatPk3);
        expect("rc603: lose Mod - die eingerichteten Ordner und der Ordner dahinter (gewinnt)",
               w4.art == Art::Lose && w4.ordner.size() == 3 && w4.ordner[0] == eingerichtet[0] && w4.ordner[2] == "C:\\Downloads");
        const auto w5 = bhed::spielordnerFuerArchiv("C:/Spiele/JK2/GameData/base/assets0.pk3", eingerichtet, w2.ordner, hatPk3);
        expect("rc603: ein weiteres Archiv des Spiels dieser Sitzung - dieselben Ordner",
               w5.art == Art::Sitzung && w5.ordner == w2.ordner);
        const auto w6 = bhed::spielordnerFuerArchiv("C:\\Spiele\\JKA MD\\base\\assets1.pk3", eingerichtet, w2.ordner, hatPk3);
        expect("rc603: wieder ein Archiv der eingerichteten Ordner - zurueck zu ihnen",
               w6.art == Art::Eingerichtet && w6.ordner == eingerichtet);
        expect("rc603: gleicherOrdner ohne Gross/Klein, / gleich \\, ohne Schraegstrich am Ende",
               bhed::gleicherOrdner("C:\\A\\b\\", "c:/a/B") && !bhed::gleicherOrdner("C:/a/b", "C:/a/bc"));
        expect("rc603: ordnerVonDatei", bhed::ordnerVonDatei("C:\\x\\base\\assets1.pk3") == "C:\\x\\base" &&
                                            bhed::ordnerVonDatei("a/b/c.pk3") == "a/b");
    }
    // Lesen im Hintergrund: eine echte .pk3 mit .bsp (Entities in Lump 0, am
    // Ende wie bei q3map) und einer mit .ent, mehrere Faeden.
    {
        const std::string ordner = std::filesystem::temp_directory_path().string() + "/bhed_missiontest_rc603";
        std::filesystem::create_directories(ordner);
        const std::string entsBsp = "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"target_scriptrunner\"\n"
                                    "\"usescript\" \"m/aus_bsp\"\n}\n";
        // RBSP 1, 18 Lumps; die Geometrie (hier Fuellbytes) VOR dem Entity-Lump.
        std::string bsp = "RBSP";
        const auto int32 = [&bsp](std::int32_t v) {
            for (int b = 0; b < 4; ++b) { bsp.push_back(static_cast<char>((static_cast<std::uint32_t>(v) >> (8 * b)) & 0xFFU)); }
        };
        int32(1);
        const std::int32_t kopf = 8 + 18 * 8;
        const std::int32_t fuell = 5000;
        int32(kopf + fuell);
        int32(static_cast<std::int32_t>(entsBsp.size()));
        for (int l = 1; l < 18; ++l) {
            int32(kopf);
            int32(0);
        }
        bsp += std::string(static_cast<std::size_t>(fuell), 'x');
        bsp += entsBsp;
        std::string err;
        const bool geschrieben =
            bhed::writePk3(ordner + "/karten.pk3", {{"maps/m1.bsp", bsp}, {"maps/m2.bsp", bsp}}, &err) &&
            bhed::writePk3(ordner + "/mission.pk3",
                           {{"maps/m2.ent", "{\n\"classname\" \"target_scriptrunner\"\n\"usescript\" \"m/aus_ent\"\n}\n"}}, &err);
        expect("rc603: Probe-Archive geschrieben", geschrieben);
        bhed::MapData alt;
        std::string lump;
        std::vector<bhed::MapEntity> neu;
        const bool beide = bhed::readBsp(bsp, alt) && bhed::readBspEntityLump(bsp, lump);
        bhed::parseEntities(lump, neu);
        bool gleich = beide && neu.size() == alt.entities.size();
        for (std::size_t i = 0; gleich && i < neu.size(); ++i) { gleich = neu[i].keys == alt.entities[i].keys; }
        expect("rc603: readBspEntityLump liefert dieselben Entities wie readBsp", gleich);
        bhed::Pk3 k;
        bhed::Pk3 mi;
        if (geschrieben && bhed::readPk3Directory(ordner + "/karten.pk3", k) &&
            bhed::readPk3Directory(ordner + "/mission.pk3", mi)) {
            std::vector<bhed::MissionsAuftrag> auftraege;
            for (int r = 0; r < 40; ++r) {
                bhed::MissionsAuftrag a1;
                a1.bspArchiv = k.path;
                a1.bsp = *k.find("maps/m1.bsp");
                auftraege.push_back(a1);
                bhed::MissionsAuftrag a2 = a1;
                a2.bsp = *k.find("maps/m2.bsp");
                a2.entArchiv = mi.path;
                a2.ent = *mi.find("maps/m2.ent");
                auftraege.push_back(a2);
            }
            bhed::MissionsAuftrag kaputt;
            kaputt.bspArchiv = ordner + "/gibt_es_nicht.pk3";
            kaputt.bsp = *k.find("maps/m1.bsp");
            auftraege.push_back(kaputt);
            bhed::MissionsSuche suche(auftraege, 4);
            for (int warte = 0; warte < 2000 && !suche.fertig(); ++warte) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            bool alle = suche.fertig() && suche.gesamt() == 81;
            for (std::size_t i = 0; alle && i + 1 < suche.gesamt(); ++i) {
                const auto& v = suche.verweise()[i];
                alle = suche.gelesen()[i] != 0 && v.size() == 1 && v[0].path == (i % 2 == 0 ? "m/aus_bsp" : "m/aus_ent");
            }
            expect("rc603: Hintergrundsuche mit 4 Faeden - .bsp-Lump und .ent (Vorrang) je Karte richtig", alle);
            expect("rc603: ein fehlendes Archiv gilt als nicht lesbar", suche.fertig() && suche.gelesen()[80] == 0);
            bhed::MissionsSuche abbruch(auftraege, 2);
            abbruch.abbrechen();
            bool teilRichtig = abbruch.erledigt() <= abbruch.gesamt();
            std::size_t bearbeitet = 0;
            for (std::size_t i = 0; teilRichtig && i + 1 < abbruch.gesamt(); ++i) {
                if (abbruch.bearbeitet()[i] == 0) { continue; }
                ++bearbeitet;
                const auto& v = abbruch.verweise()[i];
                teilRichtig = abbruch.gelesen()[i] != 0 && v.size() == 1 &&
                              v[0].path == (i % 2 == 0 ? "m/aus_bsp" : "m/aus_ent");
            }
            std::printf("  (abgebrochen nach %zu bearbeiteten Karten)\n", bearbeitet);
            expect("rc603: Abbrechen wartet auf die Faeden; was bearbeitet ist, ist richtig gelesen", teilRichtig &&
                                                                                                    bearbeitet == abbruch.erledigt());
        } else {
            expect("rc603: Probe-Archive lesbar", false);
        }
        std::error_code ec;
        std::filesystem::remove_all(ordner, ec);
    }

    std::printf("\n%s (%d Fehlschlaege)\n",
                fails != 0 ? "FEHLGESCHLAGEN" : "alle Missionsproben bestanden",
                fails);
    return fails != 0 ? 1 : 0;
}
