// wegpunkte.h - das Wegnetz der NPCs: lesen wie die Engine, erzeugen, pruefen
//
// Gewuenscht (11.10.2026): "kannst du noch einbauen das man npc waypoints
// erstellen kann bzw. er die automatisch erstellt kannst auch die originalen
// maps anschauen von JKA und JKO das du verstehst wie das system aufgebaut ist".
//
// So baut Movie Duels das Netz (code/game/g_nav.cpp, g_navigator.cpp,
// NAV::LoadFromEntitiesAndSaveToFile):
//
//   Knoten     waypoint, waypoint_small (Wegpunkte), waypoint_navgoal* (Ziele),
//              point_combat (Kampfpunkte)
//   Kanten     NUR vom Designer: target, target2, target3, target4 eines
//              Knotens nennen den targetname eines anderen; targetJump ist
//              eine Sprungkante (nur wer springen kann, nimmt sie)
//   automatisch NUR Kampf- und Zielpunkte: zu den 4 naechsten Knoten
//              (MIN_WAY_NEIGHBORS) bis 500 Einheiten (MAX_EDGE_AUTO_LEN),
//              hoechstens 60 hoeher oder tiefer (MAX_EDGE_FLOOR_DIST), im PVS;
//              zu einem anderen Kampf-/Zielpunkt nur bis 250 und hoechstens
//              einer (MAX_NONWP_NEIGHBORS); nicht mit NO_AUTO_CONNECT
//   Wegpunkt   Kasten -20 -20 -24 bis 20 20 45; spawnflags 1 SOLID_OK,
//              2 DROP_TO_FLOOR; ein Wegpunkt im Festen wird mit "ERROR:
//              Waypoint ... in solid!" verworfen
//
// In den 33 Original-Karten mit Wegnetz stehen 150 bis 490 Wegpunkte je
// Level, fast alle benannt und meist mit einem target (Ketten); der naechste
// Nachbar liegt im Median 226 Einheiten weg, verbundene im Median 306.
//
// Weil normale Wegpunkte NICHT automatisch verbunden werden, schreibt der
// Erzeuger die Kanten als target..target4 - sonst laeufen die NPCs im Spiel
// nicht.
#ifndef BHED_WEGPUNKTE_H
#define BHED_WEGPUNKTE_H

#include "bhed/bsp.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace bhed {

struct WegKnoten {
    enum class Art { Wegpunkt, Ziel, Kampf };
    Art art = Art::Wegpunkt;
    int entity = -1;            // Index in den Entities
    std::array<float, 3> ort{};
    std::string name;           // targetname
    bool keinAuto = false;      // NO_AUTO_CONNECT (Kampf-/Zielpunkte)
};

struct WegKante {
    int a = -1;                 // Knotenindizes
    int b = -1;
    bool designer = true;       // aus target..target4 / targetJump
    bool sprung = false;        // targetJump
};

struct WegNetz {
    std::vector<WegKnoten> knoten;
    std::vector<WegKante> kanten;
    // target-Namen, die keinen Knoten finden ("nav unable to locate target")
    std::vector<std::pair<int, std::string>> fehlendeZiele;   // (Knoten, Name)
};

// Ist eine Strecke frei? (Fuer das PVS der automatischen Kanten - die Engine
// fragt gi.inPVS; eine freie Sichtlinie ist die strengere, sichere Naeherung.)
// Leer: alles gilt als sichtbar.
using WegSicht = std::function<bool(const std::array<float, 3>& a, const std::array<float, 3>& b)>;

[[nodiscard]] bool istWegKlasse(const std::string& klasse);
[[nodiscard]] WegNetz baueWegNetz(const std::vector<MapEntity>& ents, const WegSicht& sicht = {});

// --- Erzeugen ----------------------------------------------------------------
//
// Ein Strahl in die Karte: true bei Treffer, dann Trefferpunkt und Normale.
using WegStrahl = std::function<bool(const std::array<float, 3>& a, const std::array<float, 3>& b,
                                     std::array<float, 3>& treffer, std::array<float, 3>& normale)>;

struct WegErzeugung {
    float abstand = 192.0F;     // Rasterweite (Originale: Median 226)
    int maxKnoten = 1500;
    float stufe = 60.0F;        // MAX_EDGE_FLOOR_DIST
    float steilsterBoden = 0.7F;   // Normale z mindestens (etwa 45 Grad)
};

struct NeuerWegpunkt {
    std::array<float, 3> boden{};      // Bodenpunkt
    std::array<float, 3> ort{};        // origin (Boden + 25: der Kasten beginnt 1 ueber dem Boden)
    std::vector<int> nachbarn;         // Indizes in der Ergebnisliste
};

// Flutet von den Startpunkten aus ueber den begehbaren Boden: je Schritt acht
// Richtungen im Abstand `abstand`, frei auf Knie- und Brusthoehe, Boden darunter
// nicht steiler als `steilsterBoden`, Hoehenunterschied hoechstens `stufe`,
// genug Kopffreiheit. Gleiche Rasterzelle und Etage = derselbe Knoten.
[[nodiscard]] std::vector<NeuerWegpunkt> erzeugeWegpunkte(const std::vector<std::array<float, 3>>& starts,
                                                          const WegStrahl& strahl, const WegErzeugung& p = {});

// Die Kanten auf target..target4 verteilen: jede Kante bekommt der Endpunkt
// mit weniger belegten Feldern (die Engine verbindet in beide Richtungen).
// Ergebnis je Knoten: die Indizes, die er als target..target4 nennt.
[[nodiscard]] std::vector<std::vector<int>> verteileZiele(const std::vector<NeuerWegpunkt>& wp, int felder = 4);

// --- Pruefen ----------------------------------------------------------------
struct WegBefund {
    enum class Art { ImFesten, Schwebend, Allein, ZielFehlt, ZuVieleZiele };
    Art art;
    int knoten = -1;
    std::string text;           // fuer die Liste (Englisch, der Aufrufer uebersetzt die Art)
};

// `strahl` wie oben; leer: nur die Pruefungen ohne Karte (Allein, ZielFehlt).
[[nodiscard]] std::vector<WegBefund> pruefeWegNetz(const WegNetz& netz, const std::vector<MapEntity>& ents,
                                                   const WegStrahl& strahl = {});

}  // namespace bhed
#endif
