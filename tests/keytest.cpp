// keytest.cpp - Tastentabelle gegen die Ressource ACCELERATOR 135
//
// Die Ressource ist hier wortgetreu abgeschrieben (Modifikator, Taste,
// Befehlsnummer). Weicht unsere Tabelle ab, faellt es hier auf und nicht
// erst, wenn jemand Strg+A drueckt und statt "Speichern unter" etwas
// anderes passiert.
#include "bhed/keys.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Row { const char* mod; int key; int cmd; };

// ACCELERATOR 135, 23 Eintraege, in der Reihenfolge der Ressource.
//
// 0x6B und 0x6D sind VK_ADD und VK_SUBTRACT (Merker FVIRTKEY). Hier stand
// frueher 'K' und 'M': 107 und 109 sind auch die Zeichen 'k' und 'm'.
const Row kResource[] = {
    {"Ctrl", 'A', 1007},   {"Alt", 'B', 1009},    {"Ctrl", 'C', 1045},
    {"Ctrl", 'D', 32779},  {"Ctrl", 'O', 1005},   {"Alt", 'R', 1012},
    {"Ctrl", 'S', 1006},   {"Ctrl", 'T', 32778},  {"Ctrl", 'V', 1046},
    {"", 0x6B, 32774},     {"Ctrl", 0x6B, 32772}, {"", 0x08, 32771},
    {"Ctrl", 0x08, 32783}, {"", 0x2E, 1002},      {"", 0x72, 32781},
    {"Ctrl", 0x72, 32780}, {"Shift", 0x72, 32782}, {"", 0x2D, 1001},
    {"", 0x0D, 1003},      {"", 0x20, 1010},      {"", 0x6D, 32776},
    {"Ctrl", 0x6D, 32773}, {"Ctrl", 'X', 1047},
};

int fails = 0;

void fail(const std::string& what) {
    std::printf("  FEHL  %s\n", what.c_str());
    ++fails;
}

const char* modName(bhed::keys::Mod m) {
    switch (m) {
        case bhed::keys::Mod::Ctrl: return "Ctrl";
        case bhed::keys::Mod::Alt: return "Alt";
        case bhed::keys::Mod::Shift: return "Shift";
        default: return "";
    }
}

}  // namespace

int main() {
    const auto& t = bhed::keys::table();
    const std::size_t n = sizeof(kResource) / sizeof(kResource[0]);

    if (t.size() != n) {
        fail("Anzahl: Ressource " + std::to_string(n) + ", unser " +
             std::to_string(t.size()));
    }
    for (std::size_t i = 0; i < n && i < t.size(); ++i) {
        const std::string pos = "Eintrag " + std::to_string(i + 1);
        if (std::string(modName(t[i].mod)) != kResource[i].mod) {
            fail(pos + ": Modifikator " + modName(t[i].mod) + " statt " + kResource[i].mod);
        }
        if (t[i].key != kResource[i].key) {
            fail(pos + ": Taste");
        }
        if (t[i].resourceCommand != kResource[i].cmd) {
            fail(pos + ": Befehlsnummer " + std::to_string(t[i].resourceCommand) +
                 " statt " + std::to_string(kResource[i].cmd));
        }
    }

    // Die zwei ueberraschenden, ausdruecklich:
    if (bhed::keys::lookup(bhed::keys::Mod::Ctrl, 'A') != bhed::keys::Action::SaveAs) {
        fail("Strg+A muss \"Speichern unter\" sein, nicht \"alles markieren\"");
    }
    if (bhed::keys::lookup(bhed::keys::Mod::None, 0x20) != bhed::keys::Action::Clone) {
        fail("Leertaste muss \"Clone\" sein");
    }
    if (bhed::keys::lookup(bhed::keys::Mod::Ctrl, 0x08) != bhed::keys::Action::Uncomment) {
        fail("Strg+Ruecktaste muss entkommentieren (nicht Alt+Ruecktaste)");
    }

    std::printf("  %zu Tastenkuerzel gegen ACCELERATOR 135: %s\n", n,
                fails != 0 ? "ABWEICHUNGEN" : "alle gleich");
    return fails != 0 ? 1 : 0;
}
