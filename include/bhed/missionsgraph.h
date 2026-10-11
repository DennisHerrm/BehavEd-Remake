// missionsgraph.h - der Missionsfluss als Knoten und Verbindungen
//
// Gewuenscht (11.10.2026): neben der klassischen Liste eine Ansicht "wie
// Blueprint", in der man sieht, was was ausloest. Eine Mission haengt so
// zusammen:
//
//   info_player_start --target--> target_scriptrunner --usescript--> Skript
//   Skript --use--> NPC_spawner / Skriptstarter / Kartenwechsel
//   Skript --affect--> Figur (steuert sie)
//   NPC_spawner --deathscript--> Skript
//
// Hier steht der Teil ohne Oberflaeche: Knoten und Kanten aus den Entities
// und den Skripten ableiten und in Spalten anordnen (tests/missionbautest).
#ifndef BHED_MISSIONSGRAPH_H
#define BHED_MISSIONSGRAPH_H

#include "bhed/bsp.h"
#include "bhed/script.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace bhed {

struct GraphKnoten {
    enum class Art { Spielerstart, Skriptstarter, Skript, Figur, Kartenwechsel, Ausloeser, Entity };
    Art art = Art::Entity;
    std::string titel;        // Name (targetname / Skriptname)
    std::string unter;        // Klasse, NPC_type, Kurzfassung des Skripts
    int entity = -1;          // Index in den Entities, sonst -1
    std::string skript;       // ICARUS-Pfad ("md_x/intro"), sonst leer
    std::string schluessel;   // fest je Knoten: "e:<index>" oder "s:<pfad>"
    int spalte = 0;
    int zeile = 0;
    float x = 0.0F;           // Anordnung (Weltkoordinaten der Ansicht)
    float y = 0.0F;
};

struct GraphKante {
    int von = -1;
    int nach = -1;
    enum class Art { Ausloesen, Steuern, Skript } art = Art::Ausloesen;
    std::string label;        // "target", "usescript", "use", "affect" ...
};

struct MissionsGraph {
    std::vector<GraphKnoten> knoten;
    std::vector<GraphKante> kanten;
    bool gekuerzt = false;    // mehr als maxKnoten - der Rest fehlt
};

// Liefert das Skript zu einem ICARUS-Pfad, oder nullptr.
using GraphSkriptQuelle = std::function<const Script*(const std::string& pfad)>;

// Knoten sind Spielerstart, Skriptstarter, Figuren, Kartenwechsel und
// Ausloeser (immer), andere Entities, wenn sie verbunden sind, und alle Skripte,
// auf die Entities oder andere Skripte zeigen. `startSkript`: ein Skript, das
// auch ohne Verweis dazugehoert (das offene).
[[nodiscard]] MissionsGraph baueMissionsGraph(const std::vector<MapEntity>& ents, const GraphSkriptQuelle& skript,
                                              const std::string& startSkript = {}, std::size_t maxKnoten = 400);

// In Spalten von links nach rechts (nach dem laengsten Weg von den Quellen),
// in jeder Spalte nach dem Mittel der Vorgaenger sortiert. Nicht verbundene
// Teile stehen untereinander.
void ordneGraph(MissionsGraph& g, float breite, float hoehe, float abstandX, float abstandY);

}  // namespace bhed
#endif
