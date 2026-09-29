// keys.h - Tastenkuerzel
//
// Abgeschrieben aus der Ressource ACCELERATOR 135 von BehavEd.exe, 23
// Eintraege. Die Befehlsnummern daneben sind die aus der Ressource; ueber
// sie ist jeder Eintrag einem Knopf aus Dialog 102 zuzuordnen.
//
// Zwei Kuerzel ueberraschen und sind deshalb ausdruecklich vermerkt:
//   Strg+A ist "Sa&ve As" (1007), NICHT "alles markieren".
//   Leertaste ist "Clone" (1010).
#ifndef BHED_KEYS_H
#define BHED_KEYS_H

#include <cstdint>
#include <vector>

namespace bhed::keys {

enum class Mod : std::uint8_t {
    None = 0,
    Ctrl = 1,
    Alt = 2,
    Shift = 4,
};

// Was das Kuerzel ausloest. Nur was wir auch umsetzen; die Eintraege des
// Originals, fuer die es bei uns (noch) nichts gibt, stehen mit Unhandled da,
// damit die Tabelle vollstaendig bleibt und der Vergleich mit der Ressource
// aufgeht.
enum class Action : std::uint8_t {
    Unhandled,
    Delete,
    Clone,
    Copy,
    Cut,
    Paste,
    CommentOut,
    Uncomment,
    EditItem,
    InsertItem,
    Open,
    Save,
    SaveAs,
    Backup,
    Restore,
    FindRepeat,
    FindPrevious,
    Find,
    // Numpad + / - (VK_ADD 0x6B, VK_SUBTRACT 0x6D in ACCELERATOR 135 -
    // frueher als 'K'/'M' abgeschrieben, weil 0x6B auch ein kleines k ist).
    ExpandNode,      // Num+        Eintrag samt allen Unterpunkten auf
    ExpandAll,       // Strg+Num+   alles auf
    CollapseNode,    // Num-        Eintrag zu
    CollapseAll,     // Strg+Num-   alles zu
    CutAlt,          // Strg+T      im Original ein zweites "Cut"

    // Ab hier: was BehavEd NICHT hatte.
    //
    // Die Ressource ACCELERATOR 135 kennt fuer "Move up" und "Move down"
    // kein Kuerzel - die Knoepfe gab es nur zum Anklicken. keytest
    // vergleicht weiterhin gegen die Ressource, deshalb stehen diese
    // Eintraege NICHT in table(), sondern nur in der Belegungsliste.
    MoveUp,
    MoveDown,
    Undo,
    Redo,
    // Lesezeichen wie in Notepad++: Strg+F2 setzen/entfernen, F2 naechstes,
    // Umschalt+F2 voriges.
    BookmarkToggle,
    BookmarkNext,
    BookmarkPrev,
    // Alle Reiter sichern. Stand fest verdrahtet auf Strg+Umschalt+S und
    // fehlte deshalb im Einstellungsfenster - gemeldet: "There is no Save
    // All keyboard shortcut".
    SaveAll,
};

// Alle Aktionen, die sich belegen lassen, in der Reihenfolge der Anzeige.
//
// Der Name ist die Kennung in den Einstellungen und darf sich NICHT mehr
// aendern - sonst verliert jeder seine eigenen Belegungen. Die Beschriftung
// fuer die Oberflaeche kommt aus der Uebersetzung, nicht von hier.
struct Bindable {
    Action action;
    const char* name;   // "MoveUp" - so steht es in der Einstellungsdatei
};

const std::vector<Bindable>& bindable();

// Name -> Aktion. Action::Unhandled, wenn der Name nicht bekannt ist; das
// passiert bei einer Einstellungsdatei aus einer neueren Fassung.
[[nodiscard]] Action fromName(const char* name);

struct Shortcut {
    Mod mod;
    // Virtuelle Taste nach Windows (VK_*), oder der Grossbuchstabe.
    int key;
    Action action;
    int resourceCommand;   // Befehlsnummer aus ACCELERATOR 135
    const char* label;     // fuer die Anzeige im Menue
};

// Die vollstaendige Tabelle, in der Reihenfolge der Ressource.
const std::vector<Shortcut>& table();

// Nachschlagen. Action::Unhandled, wenn nichts passt.
[[nodiscard]] Action lookup(Mod mod, int key);

}  // namespace bhed::keys
#endif
