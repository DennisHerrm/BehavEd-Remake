// missionbautest.cpp - eine Mission anlegen und packen (missionbau.h, entdefs.h,
// writeEntities/setKey in bsp.h, writePk3 in pk3.h)
//
// Aufruf: missionbautest <bhc> <kopfdateien> <nachtrag> <md_twj_jedi.ent>
#include "bhed/bsp.h"
#include "bhed/commands.h"
#include "bhed/entdefs.h"
#include "bhed/missionbau.h"
#include "bhed/pk3.h"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
int fails = 0;
void expect(const char* what, bool ok) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", what);
    if (!ok) { ++fails; }
}

std::string slurp(const char* p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

bool hatBefund(const std::vector<bhed::MissionBefund>& b, const char* code, bool fehler) {
    for (const auto& x : b) {
        if (x.code == code && x.fehler == fehler) { return true; }
    }
    return false;
}

const bhed::Pk3Datei* datei(const std::vector<bhed::Pk3Datei>& v, const std::string& n) {
    for (const auto& d : v) {
        if (d.name == n) { return &d; }
    }
    return nullptr;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::printf("Aufruf: missionbautest <bhc> <kopfdateien> <nachtrag> <ent>\n");
        return 2;
    }
    // --- Entity-Beschreibungen aus dem Quelltext --------------------------
    std::printf("Entity-Beschreibungen\n");
    {
        expect("ueber 200 Klassen", bhed::ent::anzahl() > 200);
        const bhed::ent::Def* d = bhed::ent::def("npc_spawner");
        expect("NPC_spawner ohne Gross/Klein gefunden", d != nullptr);
        bool drop = false;
        bool kino = false;
        bool npcType = false;
        if (d != nullptr) {
            for (std::size_t i = 0; i < d->flagAnzahl; ++i) {
                drop = drop || (std::string(d->flags[i].name) == "DROPTOFLOOR" && d->flags[i].bit == 16);
                kino = kino || (std::string(d->flags[i].name) == "CINEMATIC" && d->flags[i].bit == 32);
            }
            for (std::size_t i = 0; i < d->feldAnzahl; ++i) {
                npcType = npcType || std::string(d->felder[i].name) == "NPC_type";
            }
            expect("NPC_spawner: Groesse -24 .. 40", d->mins[2] == -24.0F && d->maxs[2] == 40.0F && !d->brush);
        }
        expect("NPC_spawner: DROPTOFLOOR = 16", drop);
        expect("NPC_spawner: CINEMATIC = 32", kino);
        expect("NPC_spawner: Feld NPC_type beschrieben", npcType);
        const bhed::ent::Def* r = bhed::ent::def("target_scriptrunner");
        bool inaktiv = false;
        if (r != nullptr) {
            for (std::size_t i = 0; i < r->flagAnzahl; ++i) {
                inaktiv = inaktiv || (std::string(r->flags[i].name) == "INACTIVE" && r->flags[i].bit == 128);
            }
        }
        expect("target_scriptrunner: INACTIVE = 128 (Bit 7, sechs x davor)", inaktiv);
        const bhed::ent::Def* b = bhed::ent::def("func_door");
        expect("func_door braucht einen Brush", b != nullptr && b->brush);
        expect("unbekannte Klasse: nullptr", bhed::ent::def("gibt_es_nicht") == nullptr);

        // Eigene Texte in vier Sprachen, nur Fakten aus dem Quelltext (11.10.).
        bool dropDe = false;
        bool idle = false;
        if (d != nullptr) {
            for (std::size_t i = 0; i < d->flagAnzahl; ++i) {
                if (std::string(d->flags[i].name) == "DROPTOFLOOR") {
                    dropDe = std::string(bhed::ent::uebersetzt(d->flags[i].text, 1)).find("Boden") != std::string::npos;
                }
            }
            for (std::size_t i = 0; i < d->feldAnzahl; ++i) {
                idle = idle || std::string(d->felder[i].name) == "idle";
            }
        }
        expect("DROPTOFLOOR hat einen deutschen Text", dropDe);
        expect("Verhaltensnamen (idle) sind keine Felder des NPC_spawner", !idle);
        const bhed::ent::Def* sp = bhed::ent::def("target_speaker");
        bool loop = false;
        bool noise = false;
        if (sp != nullptr) {
            for (std::size_t i = 0; i < sp->flagAnzahl; ++i) {
                loop = loop || (std::string(sp->flags[i].name) == "looped-on" && sp->flags[i].bit == 1);
            }
            for (std::size_t i = 0; i < sp->feldAnzahl; ++i) {
                noise = noise || std::string(sp->felder[i].name) == "noise";
            }
        }
        expect("target_speaker: looped-on = 1 (Name mit Bindestrich), Feld noise", loop && noise);
        // Kein Satz aus den QUAKED-Kommentaren - die Texte sind eigene.
        const char* fremd[] = {"NPC can be in air", "Will spawn with no default AI", "wav file to play",
                               "Script to run when used", "only a player can trigger this"};
        bool sauber = true;
        const auto pruefeText = [&](const char* const (&t)[bhed::ent::kSprachen]) {
            for (std::size_t s = 0; s < bhed::ent::kSprachen; ++s) {
                for (const char* f : fremd) {
                    sauber = sauber && std::string(t[s]).find(f) == std::string::npos;
                }
            }
        };
        for (std::size_t k = 0; k < bhed::ent::anzahl(); ++k) {
            const bhed::ent::Def& x = bhed::ent::at(k);
            pruefeText(x.text);
            for (std::size_t i = 0; i < x.flagAnzahl; ++i) { pruefeText(x.flags[i].text); }
            for (std::size_t i = 0; i < x.feldAnzahl; ++i) { pruefeText(x.felder[i].text); }
        }
        expect("kein Satz aus dem Spielquelltext in den Texten", sauber);
        const char* const leer[bhed::ent::kSprachen] = {"en", "", "", ""};
        expect("fehlt eine Sprache: englisch", std::string(bhed::ent::uebersetzt(leer, 2)) == "en");
    }

    // --- setKey / writeEntities --------------------------------------------
    std::printf("Entities schreiben\n");
    {
        const std::string text = slurp(argv[4]);
        std::vector<bhed::MapEntity> a;
        bhed::parseEntities(text, a);
        expect("Vorlage gelesen", a.size() > 10);
        std::vector<bhed::MapEntity> b;
        bhed::parseEntities(bhed::writeEntities(a), b);
        bool gleich = a.size() == b.size();
        for (std::size_t i = 0; gleich && i < a.size(); ++i) {
            gleich = a[i].keys == b[i].keys && a[i].classname == b[i].classname &&
                     a[i].targetname == b[i].targetname && a[i].origin == b[i].origin;
        }
        expect("schreiben + lesen gibt dieselben Entities", gleich);

        bhed::MapEntity e;
        bhed::setKey(e, "classname", "NPC_spawner");
        bhed::setKey(e, "NPC_targetname", "maul");
        bhed::setKey(e, "npc_targetname", "ahsoka");
        expect("Schluessel ohne Gross/Klein ersetzt, nicht verdoppelt",
               e.keys.size() == 2 && e.keys[1].first == "NPC_targetname" && e.keys[1].second == "ahsoka");
        bhed::setKey(e, "targetname", "spawn\"er\n1");
        expect("Anfuehrungszeichen/Umbruch weggelassen, targetname in Takt", e.targetname == "spawner1");
        bhed::setKey(e, "origin", "1 2 3");
        expect("origin in Takt", e.origin == "1 2 3");
        bhed::setKey(e, "targetname", "");
        expect("leerer Wert entfernt den Schluessel", e.find("targetname") == nullptr && e.targetname.empty());
    }

    // --- writePk3 ----------------------------------------------------------
    std::printf("Archiv schreiben\n");
    {
        const std::filesystem::path p = std::filesystem::temp_directory_path() / "bhed_missionbau_test.pk3";
        std::string bin(70000, '\0');
        for (std::size_t i = 0; i < bin.size(); ++i) { bin[i] = static_cast<char>(i * 31U); }
        const std::vector<bhed::Pk3Datei> f = {
            {"maps/test.ent", "{\n\"classname\" \"worldspawn\"\n}\n"},
            {"scripts/t/intro.IBI", bin},
            {"leer.txt", ""}};
        std::string err;
        expect("geschrieben", bhed::writePk3(p.string(), f, &err));
        // Ein zweites Mal: ein vorhandenes Archiv wird ersetzt.
        expect("ersetzt", bhed::writePk3(p.string(), f, &err));
        bhed::Pk3 arc;
        const bool gelesen = bhed::readPk3Directory(p.string(), arc, &err);
        expect("mit dem eigenen Leser gelesen", gelesen && arc.entries.size() == 3);
        bool alle = gelesen;
        for (const auto& d : f) {
            const bhed::Pk3Entry* e = arc.find(d.name);
            std::string inhalt;
            alle = alle && e != nullptr && bhed::readPk3File(arc, *e, inhalt) && inhalt == d.inhalt;
        }
        expect("jeder Inhalt bytegleich zurueck", alle);
        std::error_code ec;
        expect("keine .tmp liegen geblieben",
               !std::filesystem::exists(std::filesystem::path(p.string() + ".tmp"), ec));
        std::filesystem::remove(p, ec);
    }

    // --- neueMission -------------------------------------------------------
    std::printf("Neue Mission\n");
    {
        std::vector<bhed::MapEntity> karte;
        bhed::parseEntities(
            "{\n\"classname\" \"worldspawn\"\n}\n"
            "{\n\"classname\" \"info_player_start\"\n\"origin\" \"100 200 24\"\n\"angle\" \"90\"\n}\n"
            "{\n\"classname\" \"func_door\"\n\"targetname\" \"intro\"\n\"model\" \"*1\"\n}\n",
            karte);
        const bhed::NeueMission n = bhed::neueMission(karte, "md_test", "md_test_jedi");
        expect("alle Entities der Karte bleiben (worldspawn, Tuer)", n.entities.size() == karte.size() + 1);
        const bhed::MapEntity* start = nullptr;
        const bhed::MapEntity* runner = nullptr;
        for (const auto& e : n.entities) {
            if (e.classname == "info_player_start") { start = &e; }
            if (e.classname == "target_scriptrunner") { runner = &e; }
        }
        expect("Scriptrunner mit usescript md_test/intro",
               runner != nullptr && runner->find("usescript") != nullptr && *runner->find("usescript") == "md_test/intro");
        expect("Spielerstart loest ihn aus - und nicht die Tuer \"intro\"",
               start != nullptr && runner != nullptr && start->find("target") != nullptr &&
                   *start->find("target") == runner->targetname && runner->targetname != "intro");
        expect("Intro-Vorlage nennt die Mission", n.introText.find("md_test_jedi") != std::string::npos);

        std::vector<bhed::MapEntity> mitZiel;
        bhed::parseEntities("{\n\"classname\" \"info_player_start\"\n\"target\" \"t1\"\n\"origin\" \"0 0 0\"\n}\n", mitZiel);
        const bhed::NeueMission n2 = bhed::neueMission(mitZiel, "x", "x_jedi");
        expect("vorhandenes target bleibt, der Scriptrunner heisst genauso",
               n2.entities.size() == 2 && *n2.entities[0].find("target") == "t1" && n2.entities[1].targetname == "t1");

        const bhed::NeueMission n3 = bhed::neueMission({}, "x", "x_jedi");
        expect("ohne Spielerstart: einer angelegt", n3.startAngelegt && n3.entities.size() == 2);
    }

    // --- missionDateien ----------------------------------------------------
    std::printf("Mission zusammenstellen\n");
    {
        bhed::CommandDb db;
        std::vector<bhed::LoadDiag> ld;
        bool geladen = bhed::loadCommandDb(argv[1], argv[2], db, ld);
        geladen = geladen && bhed::loadCommandDb(argv[3], argv[2], db, ld);
        expect("Befehlsliste geladen", geladen);

        const bhed::NeueMission n = bhed::neueMission({}, "md_test", "md_test_jedi");
        bhed::MissionInhalt m;
        m.name = "md_test_jedi";
        m.bsp = "RBSP-Platzhalter";
        m.entities = n.entities;
        bhed::MapEntity spawner;
        bhed::setKey(spawner, "classname", "NPC_spawner");
        bhed::setKey(spawner, "deathscript", "md_test/fehlt");
        bhed::setKey(spawner, "spawnscript", "idle");
        bhed::setKey(spawner, "target", "niemand");
        m.entities.push_back(spawner);
        m.skripte.push_back({n.introRef, n.introText});
        m.weitere.push_back({"strings/English/md_test_jedi.str", "VERSION \"1\"\nREFERENCE A\nLANG_ENGLISH \"a\"\n"});
        std::vector<bhed::MissionBefund> bef;
        const std::vector<bhed::Pk3Datei> f = bhed::missionDateien(m, db, bef);
        expect("Karte unter dem Missionsnamen", datei(f, "maps/md_test_jedi.bsp") != nullptr);
        expect(".ent dabei", datei(f, "maps/md_test_jedi.ent") != nullptr);
        expect("Intro als .txt", datei(f, "scripts/md_test/intro.txt") != nullptr);
        const bhed::Pk3Datei* ibi = datei(f, "scripts/md_test/intro.IBI");
        expect("Intro uebersetzt (.IBI)", ibi != nullptr && ibi->inhalt.compare(0, 3, "IBI") == 0);
        expect("fehlendes deathscript gemeldet (S001, kein Fehler)", hatBefund(bef, "S001", false));
        bool idleGemeldet = false;
        for (const auto& b : bef) { idleGemeldet = idleGemeldet || b.arg == "idle"; }
        expect("\"idle\" ist ein Verhalten, kein Skript", !idleGemeldet);
        expect("target ohne Ziel gemeldet (S005)", hatBefund(bef, "S005", false));
        const bhed::Pk3Datei* str = datei(f, "strings/English/md_test_jedi.str");
        expect(".str bekommt ENDMARKER (S004)",
               str != nullptr && str->inhalt.find("ENDMARKER") != std::string::npos && hatBefund(bef, "S004", false));
        expect("keine Fehler", !hatBefund(bef, "S002", true) && !hatBefund(bef, "S006", true));

        m.skripte.push_back({"md_test/kaputt", "affect ( \"x\", FLUSH )\n{\nwait ( 1.000 );\n"});
        bef.clear();
        const std::vector<bhed::Pk3Datei> f2 = bhed::missionDateien(m, db, bef);
        expect("nicht uebersetzbares Skript: Fehler S002, keine .IBI",
               hatBefund(bef, "S002", true) && datei(f2, "scripts/md_test/kaputt.IBI") == nullptr);

        m.name = "md test";
        bef.clear();
        (void)bhed::missionDateien(m, db, bef);
        expect("Name mit Leerzeichen: Fehler S006", hatBefund(bef, "S006", true));
        expect("gueltige Namen", bhed::gueltigerMissionsName("md_tpa_jedi") && !bhed::gueltigerMissionsName("") &&
                                     !bhed::gueltigerMissionsName("a/b") && !bhed::gueltigerMissionsName(std::string(49, 'a')));
    }

    std::printf("%s (%d Fehler)\n", fails == 0 ? "missionbau: alles gut" : "missionbau: FEHLER", fails);
    return fails == 0 ? 0 : 1;
}
