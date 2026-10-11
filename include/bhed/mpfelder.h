// Erzeugt von tools/gen_mpfelder.py aus tools/mp_felder.tsv und mp_get.tsv -
// nicht von Hand aendern.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace bhed::mp {

// Was ein set-Feld im Multiplayer tut (codemp/game/g_ICARUScb.c).
enum class Status : std::uint8_t {
    Geht,          // tut, was es soll
    Unbekannt,     // MP kennt den Namen nicht: "variable or field not found"
    Stub,          // vorhanden, tut nichts (Warnung nur mit developer 2)
    OhneWirkung,   // angenommen, tut nichts
    Anders,        // laeuft, aber anders als in SP (eigene Meldung)
};

// Auf welcher Entity es wirkt (aus den Pruefungen im MP-Code).
enum class Wer : std::uint8_t { Alle, Mover, SpielerOderNpc, Npc };

struct Feld {
    const char* name;
    bool inSp;        // in der SP-Feldliste (SDK oder Movie Duels)
    bool inMp;        // in der MP-Feldtabelle
    Status status;
    Wer wer;
    bool ravenStub;   // im originalen Raven-MP-Spielmodul nur ein Stub (wirkt nur auf OpenJK-Servern)
};

// $get( TYP, "FELD" ) im Multiplayer.
enum class Get : std::uint8_t {
    Geht,
    Scheitert,         // liefert nie einen Wert: if nimmt immer else, andere Befehle brechen ab
    AbsturzOhneParms,  // SET_PARMn als VECTOR auf einer Entity ohne Parms: Absturz
};

// nullptr, wenn der Name in keiner Liste steht. Gross/klein egal.
[[nodiscard]] const Feld* feld(std::string_view name);
// Unbekannte Kombination: Get::Geht (kein Befund).
[[nodiscard]] Get get(std::string_view feld, std::string_view typ);
[[nodiscard]] std::size_t anzahlFelder();

}  // namespace bhed::mp
