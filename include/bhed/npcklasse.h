// npcklasse.h - rc602: welcher NPC_type aus welchem Klassennamen kommt.
//
// Gemeldet (md_am_sith, Movie Duels): "der NPC droid fehlt" - die Karte
// setzt einen NPC_Droid_R2D2 OHNE NPC_type-Schluessel. Im Spiel setzt die
// Spawnfunktion den Typ selbst:
//
//     void SP_NPC_Droid_R2D2( gentity_t *self) {
//         if ( self->spawnflags&1 ) self->NPC_type = "r2d2_imp";
//         else                      self->NPC_type = "r2d2";
//         SP_NPC_spawner( self );
//
// (code/game/NPC_spawn.cpp:3975 OpenJK, :5312 Movie Duels). behaved warnte
// stattdessen "Kein NPC_type - es erscheint niemand" und zeichnete nur
// einen Kasten.
//
// Hier steht die GANZE Tabelle beider Regelwerke: jede SP_NPC_*-Funktion aus
// spawns[] (g_spawn.cpp:747 ff. OpenJK, :877 ff. Movie Duels), mit ihren
// spawnflags-Zweigen und Zufallswahlen. Zwei Sorten:
//   - "fest": die Funktion UEBERSCHREIBT einen NPC_type-Schluessel der Karte
//     (SP_NPC_Kyle, SP_NPC_Droid_R2D2, SP_NPC_Stormtrooper ...);
//   - "if ( !self->NPC_type )": nur wenn die Karte keinen nennt
//     (SP_NPC_Reborn, SP_NPC_Jedi, SP_NPC_Imperial ...).
#ifndef BHED_NPCKLASSE_H
#define BHED_NPCKLASSE_H

#include "bhed/bsp.h"

#include <string>
#include <vector>

namespace bhed {

struct NpcKlassenTyp {
    // Der Klassenname steht in spawns[] - es gibt eine SP_-Funktion. Ein
    // anderer "NPC_..."-Name spawnt im Spiel GAR NICHTS (G_CallSpawn
    // vergleicht mit strcmp, g_spawn.cpp; "doesn't have a spawn function").
    bool bekannt = false;
    // Die Funktion ruft SP_NPC_spawner (bzw. G_VehicleSpawn). OpenJK
    // SP_NPC_Galak ist LEER (NPC_spawn.cpp:2350) - dort erscheint niemand.
    bool spawnt = false;
    // Der wirksame NPC_type ("" = keiner). Bei einer Zufallswahl die ERSTE
    // Moeglichkeit (siehe zufall) - die Vorschau muss sich festlegen.
    std::string typ;
    // Q_irand-Wahl der Engine: alle Moeglichkeiten in Quelltext-Reihenfolge.
    // Leer, wenn der Typ feststeht.
    std::vector<std::string> zufall;
    // Der Typ kommt aus dem Klassennamen, nicht aus dem NPC_type-Schluessel.
    bool ausKlasse = false;
};

// Der Kern: Klassenname, spawnflags, der NPC_type-Schluessel der Karte
// (leer = nicht gesetzt; ein LEERER Schluessel gilt hier als nicht gesetzt),
// ob die Entity eine "message" hat (SP_NPC_Human_Merc) und das Regelwerk.
[[nodiscard]] NpcKlassenTyp npcKlassenTyp(const std::string& classname, int spawnflags,
                                          const std::string& schluesselTyp, bool hatMessage,
                                          bool movieDuels);
// Dasselbe aus einer Entity der Karte.
[[nodiscard]] NpcKlassenTyp npcKlassenTyp(const MapEntity& e, bool movieDuels);
// ... mit dem aktiven Regelwerk (regelwerkMovieDuels()).
[[nodiscard]] NpcKlassenTyp npcKlassenTyp(const MapEntity& e);

// Kurzform: der wirksame NPC_type einer Entity ("" = keiner / keine Figur).
[[nodiscard]] std::string effektiverNpcTyp(const MapEntity& e);

// Erscheint diese Entity beim Laden der Karte als Figur? SP_NPC_spawner:
// ohne targetname denkt der Spawner bei START_TIME_REMOVE_ENTS + 50 und
// spawnt (NPC_spawn.cpp:2037 ff. OpenJK, :3087 ff. Movie Duels), mit
// targetname wartet er auf use. NPC_Vehicle genauso (:2126 / :3205).
[[nodiscard]] bool npcSpawntBeimKartenstart(const MapEntity& e);

}  // namespace bhed

#endif
