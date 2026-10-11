// Erzeugt von tools/gen_entdefs.py aus tools/entdefs.txt und
// tools/entdefs_texte.txt - nicht von Hand aendern.
//
// Die Entity-Klassen des Spiels mit Groesse, Spawnflags und Feldern. Die
// Fakten (Namen, Bits, Groessen) stammen aus den QUAKED-Koepfen des
// Movie-Duels-Quelltexts, die Erklaerungen sind eigene, in vier Sprachen.
#pragma once

#include <cstddef>
#include <string_view>

namespace bhed::ent {

inline constexpr std::size_t kSprachen = 4;   // wie i18n::Language: en, de, zh, ja

struct Flag {
    int bit;                         // Wert in "spawnflags" (1, 2, 4, ...)
    const char* name;                // DROPTOFLOOR
    const char* text[kSprachen];     // leer, wenn es keine Erklaerung gibt
};

struct Feld {
    const char* name;                // wie im Quelltext; die Engine vergleicht ohne Gross/Klein
    const char* text[kSprachen];
};

struct Def {
    const char* klasse;
    float farbe[3];
    bool brush;                      // braucht ein Brush-Modell aus der Karte ("?" im Quelltext)
    float mins[3];
    float maxs[3];
    const char* text[kSprachen];
    const Flag* flags;
    std::size_t flagAnzahl;
    const Feld* felder;
    std::size_t feldAnzahl;
};

// Ohne Gross/Klein - "npc_spawner" findet NPC_spawner. nullptr, wenn unbekannt.
[[nodiscard]] const Def* def(std::string_view klasse) noexcept;
[[nodiscard]] std::size_t anzahl() noexcept;
[[nodiscard]] const Def& at(std::size_t i) noexcept;
// Der Text in der Sprache `sprache` (0..3), sonst englisch, sonst "".
[[nodiscard]] const char* uebersetzt(const char* const (&text)[kSprachen], int sprache) noexcept;

}  // namespace bhed::ent
