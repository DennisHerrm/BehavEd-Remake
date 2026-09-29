// commands.h - Befehlsmodell aus der .bhc und den Engine-Kopfdateien
//
// Die Befehlsliste wird NICHT gepflegt, sondern erzeugt: die .bhc nennt die
// Signaturen, die Auswahllisten kommen per  #include "datei.h" enum_e  aus
// den Kopfdateien der Engine. Damit bleibt das Werkzeug mit der Engine im
// Gleichstand.
#ifndef BHED_COMMANDS_H
#define BHED_COMMANDS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bhed {

struct Param {
    enum class Kind : std::uint8_t {
        String,   // %s
        Int,      // %d
        Float,    // %f
        Vector,   // %v
        Range,    // %r als eigener Typ (einmal im Header)
        TypeSet,  // %t="NAME" - Auswahlliste
        Expr,     // $ausdruck$
        Op,       // $ < > ! = $
    };
    Kind kind = Kind::String;   // was der Editor anbietet
    Kind wire = Kind::String;   // wie es in der .icarus geschrieben wird
    std::string def;      // Vorgabewert
    std::string typeset;  // bei Kind::TypeSet der Name der Menge
    std::string filter;   // Dateifilter aus !!"..." , sonst leer
    bool locked = false;  // %r-Vorsatz: Feld nicht aenderbar
};

struct TypeEntry {
    std::string name;
    std::string desc;
    std::vector<Param> params;  // eigene Parameter aus //## (281 Eintraege)

    // Ueberschrift, unter der dieser Eintrag steht.
    //
    // Die Kopfdateien enthalten dafuer eine eigene Anweisung, und zwar
    // ausdruecklich fuer BehavEd gedacht:
    //     //# #sep Parm strings
    //     SET_PARM1 = 0,//## %s="" # Set entity parm1
    //
    // 37 davon gibt es, 27 allein in anims.h - bei 1543 Animationsnamen ist
    // das genau die Liste, in der man sich sonst verlaeuft.
    std::string section;
};

struct TypeSet {
    // 's' -> Werte in Anfuehrungszeichen, 'i' -> blanker Bezeichner,
    // 'd' -> Zahl. Das entscheidet ueber die Schreibweise in der .icarus:
    //   <%s="SET_TYPES">   -> "SET_HEALTH"
    //   <%i="AFFECT_TYPE"> -> FLUSH
    char kind = 's';
    std::string name;
    std::vector<TypeEntry> entries;   // nur bis zum //# #eol des Headers
    int hiddenAfterEol = 0;
    [[nodiscard]] const TypeEntry* find(const std::string& v) const;
};

struct Command {
    std::string name;
    std::string icon;   // [I_SET] usw.
    std::string desc;
    bool block = false; // {} in der Signatur
    std::vector<Param> params;
};

// Ein Makro der .bhc: ein Name und ein Rumpf aus fertigen Befehlszeilen.
//
// Der Rumpf steht dort mit %r-Vorsatz an jedem Feld:
//     "patrolRun"//# patrols in a run, chases enemies
//     {
//         set( %r%s="SET_BEHAVIOR_STATE", %r%s="BS_DEFAULT" );
//         ...
//     }
// %r heisst gesperrt - beim Einfuegen werden daraus die /*!*/-Felder, die
// Ravens Skripte an ausgeklappten Makros tragen.
struct MacroLine {
    std::string command;
    std::vector<Param> params;
};

struct Macro {
    std::string name;
    std::string desc;
    std::vector<MacroLine> body;
};

struct CommandDb {
    std::vector<Command> commands;
    std::map<std::string, TypeSet> typesets;
    std::vector<std::string> macros;          // nur die Namen, wie bisher
    std::vector<Macro> macroBodies;
    [[nodiscard]] const Macro* macro(const std::string& name) const;

    [[nodiscard]] const TypeSet* typeset(const std::string& n) const;
    [[nodiscard]] std::vector<const Command*> overloads(const std::string& n) const;
};

struct LoadDiag {
    std::string file;
    int line = 0;
    std::string message;
};

// Liest die .bhc; Kopfdateien werden in includeDir gesucht (ohne Ruecksicht
// auf Gross-/Kleinschreibung, die .bhc schreibt "q3_interface.h").
[[nodiscard]] bool loadCommandDb(const std::string& bhcPath, const std::string& includeDir,
                   CommandDb& out, std::vector<LoadDiag>& diag);

// Parameterangabe hinter //## zerlegen. Oeffentlich, weil sie in der .bhc
// und in den Kopfdateien dieselbe ist.
void parseParamSpec(const std::string& spec, std::vector<Param>& out,
                    std::string& desc);

} // namespace bhed
#endif
