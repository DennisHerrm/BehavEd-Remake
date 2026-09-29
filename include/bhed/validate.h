// validate.h - ein gelesenes Skript gegen das Befehlsmodell halten
#ifndef BHED_VALIDATE_H
#define BHED_VALIDATE_H

#include "bhed/commands.h"
#include "bhed/script.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bhed {

struct Issue {
    enum class Level : std::uint8_t { Error, Warning, Info };
    Level level = Level::Error;
    std::string code;     // V001 ...
    std::string where;    // Befehlspfad, z. B. affect/task/set
    // Der Text - in DEUTSCH, weil der Kern keine Uebersetzung kennt.
    //
    // Er bleibt als Rueckfall stehen: die Werkzeuge auf der Befehlszeile
    // (checks, validate) geben ihn direkt aus, und dort ist er richtig.
    std::string message;
    // Die Bausteine fuer eine uebersetzte Fassung.
    //
    // Die Oberflaeche kennt zu jedem code einen eigenen Satz und setzt die
    // beiden Teile ein. So bleibt der Kern frei von Uebersetzungen, und im
    // Programm steht trotzdem alles in einer Sprache - gemeldet wurde
    // naemlich genau das: "the text is in German".
    std::string arg1{};
    std::string arg2{};
};

// Die Felder, die ein Befehl mit den gegebenen Argumenten tatsaechlich hat.
// Ein Auswahllisteneintrag bringt eigene Parameter mit; die ERSETZEN die
// restlichen Parameter des Befehls (set, sound) oder ergaenzen sie (camera).
[[nodiscard]] std::vector<Param> expandFields(const Command& c, const CommandDb& db,
                                const std::vector<Arg>& args);

// Welche Signatur ist gemeint? Bei set, move, use und wait gibt es mehrere
// mit gleicher Feldzahl; entscheidend ist die Annotation /*@TYPMENGE*/ und
// die Schreibweise der Werte. Der Editor MUSS dieselbe Wahl treffen wie der
// Pruefer, sonst haengt er beim Oeffnen eine Annotation an, die vorher nicht
// da war - und die Datei aendert sich, ohne dass jemand etwas bearbeitet hat.
[[nodiscard]] const Command* selectOverload(const Node& n, const CommandDb& db);

void validate(const Script& s, const CommandDb& db, std::vector<Issue>& out);

} // namespace bhed
#endif
