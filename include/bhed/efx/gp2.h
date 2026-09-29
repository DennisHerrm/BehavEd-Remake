// GenericParser2 - das Textformat, auf dem .efx aufsetzt.
//
// Nachbau von code/game/genericparser2.cpp aus OpenJK, Zeichen fuer Zeichen:
// gleiche Kommentarregeln, gleiche Tokengrenzen, gleiche Listen- und
// Gruppensyntax. Weicht der Parser hier vom Spiel ab, sieht der Editor etwas
// anderes als die Engine - genau das wollen wir nicht.
//
// Unterschied zum Original: wir merken uns Zeilennummern. Das Spiel braucht
// sie nicht, ein Editor schon, sonst kann er einen Fehler nicht anzeigen.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bhed::efx::gp2 {

// Ein Schluessel mit einem oder mehreren Werten.
//   key   value          -> ein Wert
//   key   [ a b c ]      -> mehrere Werte (Liste)
struct Property {
    std::string name;
    std::vector<std::string> values;
    bool wasList = false;  // stand in [ ] - fuer verlustfreies Zurueckschreiben
    int line = 0;
};

// Ein benannter Block in { }. Der aeusserste Block der Datei hat keinen Namen
// und keine Klammern.
struct Group {
    std::string name;
    std::vector<Property> properties;
    std::vector<Group> subGroups;
    int line = 0;

    // Beide Suchen ignorieren Gross-/Kleinschreibung - so macht es das Spiel.
    const Property* findProperty(std::string_view key) const;
    const Group* findSubGroup(std::string_view key) const;
};

struct Error {
    int line = 0;
    std::string message;
};

struct ParseResult {
    Group topLevel;
    std::vector<Error> errors;
    bool ok() const { return errors.empty(); }
};

// Zerlegt einen kompletten Dateiinhalt. Wirft nicht; Fehler stehen im Ergebnis.
ParseResult parse(std::string_view text);

}  // namespace bhed::efx::gp2
