// bsp.h - Entities aus einer JKA-Karte lesen
//
// Nur das Entity-Lump, nicht die Geometrie. Das ist Lump 0 in jedem
// id-Tech-3-BSP und reiner Text - bei duel_kamino_lp.bsp 15 KB fuer 111
// Entities. Ein Leser fuer Flaechen, Patches und Lightmaps waere ein
// Vielfaches an Arbeit und braeuchte einen Zeichner dazu; fuer einen
// Skripteditor genuegt, was hier steht:
//
//   targetname   - was affect, use, kill, remove ansprechen
//   cameraGroup  - was SET_CAMERA_GROUP erwartet
//   classname    - zum Sortieren und Anzeigen
//   origin       - fuer spaetere Kamerapfade
//
// Unterstuetzt werden RBSP (Jedi Academy, Jedi Outcast) und IBSP (Quake 3).
// Die Kennung wird geprueft, aber nicht die Fassungsnummer: das Entity-Lump
// liegt bei allen an derselben Stelle.
#ifndef BHED_BSP_H
#define BHED_BSP_H

#include <string>
#include <vector>

namespace bhed {

struct MapEntity {
    std::string classname;
    std::string targetname;
    std::string cameraGroup;
    std::string origin;
    std::vector<std::pair<std::string, std::string>> keys;

    [[nodiscard]] const std::string* find(const std::string& key) const;
};

struct MapData {
    std::string path;
    std::string magic;              // "RBSP" oder "IBSP"
    int version = 0;
    std::vector<MapEntity> entities;

    // Sortiert und ohne Dubletten - genau das, was eine Klappliste braucht.
    std::vector<std::string> targetNames;
    std::vector<std::string> cameraGroups;

    // Die Skripte, auf die diese Karte verweist.
    //
    // So haengt eine Mission zusammen: die Karte selbst weiss nichts von
    // Skripten, aber ihre Entities tun es -
    //
    //     target_scriptrunner  usescript    "md_tfoaj/intro_jedi"
    //     NPC_spawner          deathscript  "md_tfoaj/templeguard_defeated"
    //
    // Daraus wird scripts/md_tfoaj/intro_jedi.txt bzw. .ibi. Bei
    // md_tfoaj_jedi.ent sind das sieben Skripte, und alle sieben liegen im
    // selben Archiv.
    struct ScriptRef {
        std::string path;       // "md_tfoaj/intro_jedi"
        std::string key;        // "usescript", "deathscript", ...
        std::string owner;      // targetname oder classname der Entity
    };
    std::vector<ScriptRef> scripts;

    [[nodiscard]] bool empty() const noexcept { return entities.empty(); }
};

// Die Namenslisten und Skriptverweise aus den Entities aufbauen.
//
// Getrennt von readBsp, weil die Entities auch aus einer .ent-Datei kommen
// koennen - Movie Duels arbeitet so.
void collectNames(MapData& out);

// Liest den rohen Inhalt einer .bsp. false, wenn es keine ist.
[[nodiscard]] bool readBsp(const std::string& bytes, MapData& out,
                           std::string* error = nullptr);

// Nur das Entity-Lump als Text zerlegen. Getrennt gehalten, damit es ohne
// Datei pruefbar ist.
void parseEntities(const std::string& text, std::vector<MapEntity>& out);

}  // namespace bhed
#endif
