// mission.h - Missionen finden
//
// Eine Mission ist kein Dateiformat, sondern ein Zusammenhang: eine Karte,
// die Entities dazu, und die Skripte, auf die diese Entities zeigen. Nichts
// davon steht an einer Stelle - man muss es zusammensuchen.
//
// Der Weg, an echten Daten nachgemessen:
//
//   maps/md_tfoaj_jedi.bsp        die Geometrie
//   maps/md_tfoaj_jedi.ent        die Entities, falls vorhanden
//                                 (sonst die im .bsp)
//        -> target_scriptrunner   usescript "md_tfoaj/intro_jedi"
//        -> NPC_spawner           deathscript, NPC_type
//              scripts/md_tfoaj/intro_jedi.txt
//              models/players/<...>/model.glm
//
// Ein Befund aus zzz_The_Fear_of_a_Jedi.pk3: dort liegen ZWEI .bsp mit
// identischem Inhalt - duel_jt_dojo.bsp und md_tfoaj_jedi.bsp, gleiche
// Pruefsumme. Nur die zweite hat eine .ent daneben, und nur die ist die
// Mission. Eine Karte ohne Skriptverweise ist eine Duellkarte, keine
// Mission.
#ifndef BHED_MISSION_H
#define BHED_MISSION_H

#include "bhed/bsp.h"

#include <functional>
#include <string>
#include <vector>

namespace bhed {

struct Mission {
    std::string mapFile;      // "maps/md_tfoaj_jedi.bsp"
    std::string entFile;      // "maps/md_tfoaj_jedi.ent", leer wenn keine
    std::string name;         // "md_tfoaj_jedi"
    // Die Skripte, auf die die Entities zeigen - schon aufgeloest auf
    // wirklich vorhandene Dateien.
    struct Script {
        std::string ref;      // "md_tfoaj/intro_jedi"
        std::string file;     // "scripts/md_tfoaj/intro_jedi.txt"
        std::string key;      // "usescript", "deathscript", ...
    };
    std::vector<Script> scripts;

    // Eine Karte ohne Skripte ist keine Mission, sondern eine Duellkarte.
    [[nodiscard]] bool playable() const noexcept { return !scripts.empty(); }
};

// Aus einer Liste vorhandener Dateinamen die Missionen zusammenstellen.
//
// entitiesFor liefert den Inhalt einer Datei; so bleibt die Funktion frei von
// Archiven und Dateisystem und laesst sich mit erfundenen Namen pruefen.
using FileReader = std::function<bool(const std::string& name, std::string& out)>;

[[nodiscard]] std::vector<Mission> findMissions(
    const std::vector<std::string>& fileNames, const FileReader& read);

}  // namespace bhed
#endif
