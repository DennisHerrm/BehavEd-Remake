#include "bhed/keys.h"

#include <cstring>

namespace bhed::keys {
namespace {

constexpr int kBack = 0x08;
constexpr int kReturn = 0x0D;
constexpr int kSpace = 0x20;
constexpr int kInsert = 0x2D;
constexpr int kDelete = 0x2E;
constexpr int kF3 = 0x72;
// VK_ADD / VK_SUBTRACT - der Ziffernblock. In der Ressource steht 107 und
// 109; das sind zugleich die Zeichen 'k' und 'm', und so wurden sie frueher
// faelschlich abgeschrieben (als 'K' und 'M').
constexpr int kNumPlus = 0x6B;
constexpr int kNumMinus = 0x6D;

}  // namespace

const std::vector<Shortcut>& table() {
    static const std::vector<Shortcut> kTable = {
        {Mod::Ctrl,  'A',     Action::SaveAs,       1007,  "Ctrl+A"},
        {Mod::Alt,   'B',     Action::Backup,       1009,  "Alt+B"},
        {Mod::Ctrl,  'C',     Action::Copy,         1045,  "Ctrl+C"},
        {Mod::Ctrl,  'D',     Action::Unhandled,    32779, "Ctrl+D"},
        {Mod::Ctrl,  'O',     Action::Open,         1005,  "Ctrl+O"},
        {Mod::Alt,   'R',     Action::Restore,      1012,  "Alt+R"},
        {Mod::Ctrl,  'S',     Action::Save,         1006,  "Ctrl+S"},
        {Mod::Ctrl,  'T',     Action::CutAlt,       32778, "Ctrl+T"},
        {Mod::Ctrl,  'V',     Action::Paste,        1046,  "Ctrl+V"},
        {Mod::None,  kNumPlus, Action::ExpandNode,  32774, "Num+"},
        {Mod::Ctrl,  kNumPlus, Action::ExpandAll,   32772, "Ctrl+Num+"},
        {Mod::None,  kBack,   Action::CommentOut,   32771, "Backspace"},
        {Mod::Ctrl,  kBack,   Action::Uncomment,    32783, "Ctrl+Backspace"},
        {Mod::None,  kDelete, Action::Delete,       1002,  "Delete"},
        {Mod::None,  kF3,     Action::FindRepeat,   32781, "F3"},
        // Ctrl+F3 steht so in ACCELERATOR 135. Wir belegen die Suche
        // trotzdem mit Ctrl+F - dazu unten mehr. Der Eintrag bleibt hier
        // stehen, damit keytest die Ressource weiterhin vollstaendig
        // vergleichen kann; die WIRKENDE Belegung setzt chordFor().
        {Mod::Ctrl,  kF3,     Action::Find,         32780, "Ctrl+F3"},
        {Mod::Shift, kF3,     Action::FindPrevious, 32782, "Shift+F3"},
        {Mod::None,  kInsert, Action::InsertItem,   1001,  "Insert"},
        {Mod::None,  kReturn, Action::EditItem,     1003,  "Enter"},
        {Mod::None,  kSpace,  Action::Clone,        1010,  "Space"},
        {Mod::None,  kNumMinus, Action::CollapseNode, 32776, "Num-"},
        {Mod::Ctrl,  kNumMinus, Action::CollapseAll,  32773, "Ctrl+Num-"},
        {Mod::Ctrl,  'X',     Action::Cut,          1047,  "Ctrl+X"},
    };
    return kTable;
}

const std::vector<Bindable>& bindable() {
    // Reihenfolge wie im Knopfstreifen "Actions", damit man im
    // Einstellungsfenster findet, was man sucht.
    static const std::vector<Bindable> v = {
        {Action::Delete,       "Delete"},
        {Action::Clone,        "Clone"},
        {Action::Copy,         "Copy"},
        {Action::Cut,          "Cut"},
        {Action::CutAlt,       "CutAlt"},
        {Action::Paste,        "Paste"},
        {Action::CommentOut,   "CommentOut"},
        {Action::Uncomment,    "Uncomment"},
        {Action::Find,         "Find"},
        {Action::FindRepeat,   "FindRepeat"},
        {Action::FindPrevious, "FindPrevious"},
        {Action::ExpandNode,   "ExpandNode"},
        {Action::CollapseNode, "CollapseNode"},
        {Action::ExpandAll,    "ExpandAll"},
        {Action::CollapseAll,  "CollapseAll"},
        {Action::MoveUp,       "MoveUp"},
        {Action::MoveDown,     "MoveDown"},
        {Action::Undo,         "Undo"},
        {Action::Redo,         "Redo"},
        {Action::BookmarkToggle, "BookmarkToggle"},
        {Action::BookmarkNext, "BookmarkNext"},
        {Action::BookmarkPrev, "BookmarkPrev"},
        {Action::EditItem,     "EditItem"},
        {Action::InsertItem,   "InsertItem"},
        {Action::Open,         "Open"},
        {Action::Save,         "Save"},
        {Action::SaveAs,       "SaveAs"},
        {Action::SaveAll,      "SaveAll"},
        {Action::Backup,       "Backup"},
        {Action::Restore,      "Restore"},
    };
    return v;
}

Action fromName(const char* name) {
    if (name == nullptr) {
        return Action::Unhandled;
    }
    for (const Bindable& b : bindable()) {
        if (std::strcmp(b.name, name) == 0) {
            return b.action;
        }
    }
    return Action::Unhandled;
}

Action lookup(Mod mod, int key) {
    for (const Shortcut& s : table()) {
        if (s.mod == mod && s.key == key) {
            return s.action;
        }
    }
    return Action::Unhandled;
}

}  // namespace bhed::keys
