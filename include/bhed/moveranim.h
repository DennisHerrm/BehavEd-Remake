// moveranim.h - eigene Animationen fuer Mover: Schluesselstellen <-> Skriptblock
//
// Gewuenscht (11.10.2026): "auch sowas wie das fliegende schiff das man das
// alles erstellen kann in unserem mission creator kit ... das man solche
// sachen selbst animieren kann".
//
// So bewegen die Missionen ein Schiff (md_am_sith: func_static "sta2" mit
// Ursprungsbrush): ein affect-Block auf seinen Skriptnamen mit move-Befehlen.
// move ( <ort>, <winkel>, dauer ) wartet NICHT - die Zeit vergeht erst mit
// dem wait dahinter (scene.cpp, Q3_Lerp2Pos). Daraus und zurueck:
//
//     //(BehavEd) animation "sta2"
//     affect ( "sta2", FLUSH )
//     {
//         wait ( 1500.000 );                                   Schluessel 1 bei 1,5 s
//         move ( < x y z >, < p y r >, 0.000 );                 hinsetzen
//         move ( < x y z >, < p y r >, 2000.000 );             Schluessel 2 bei 3,5 s
//         wait ( 2000.000 );
//         ...
//     }
//
// Der Block steht am ANFANG des Skripts, damit die Zeiten von dessen Start an
// zaehlen. Die Kommentarzeile markiert ihn - behaved erkennt seine eigenen
// Bloecke daran wieder und schreibt sie neu, wenn sich ein Schluessel aendert.
#ifndef BHED_MOVERANIM_H
#define BHED_MOVERANIM_H

#include "bhed/script.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace bhed {

struct AnimSchluessel {
    double ms = 0.0;                 // ab Start des Skripts
    std::array<float, 3> ort{};
    std::array<float, 3> winkel{};
};

// Die Markierung vor dem Block.
[[nodiscard]] std::string animMarke(const std::string& name);

// Die Zeilen des Blocks: Kommentar + affect mit move/wait. Leere Liste bei
// keinen Schluesseln.
[[nodiscard]] std::vector<Node> baueAnimBlock(const std::string& name, std::vector<AnimSchluessel> schluessel);

// Wo steht der Block fuer `name` (Index der Kommentarzeile auf oberster Ebene)?
[[nodiscard]] std::optional<std::size_t> findeAnimBlock(const Script& s, const std::string& name);

// Die Schluessel aus dem Block bei `index` (Kommentarzeile) zurueckgewinnen.
[[nodiscard]] std::vector<AnimSchluessel> leseAnimBlock(const Script& s, std::size_t index);

// Ein neues Skript mit dem Block fuer `name` (ersetzt den alten oder steht
// am Anfang). Keine Schluessel: der Block faellt weg.
[[nodiscard]] Script mitAnimBlock(const Script& s, const std::string& name, const std::vector<AnimSchluessel>& schluessel);

}  // namespace bhed
#endif
