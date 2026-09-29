// Abgleich: was liest behaved aus den Original-Kopfdateien, und stimmt es
// mit dem ueberein, was in den Kopfdateien steht?
//
// Aufruf:  tabellen <behaved.bhc> <ordner-mit-kopfdateien>
#include "bhed/commands.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

using namespace bhed;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("Aufruf: tabellen <behaved.bhc> <kopfdatei-ordner>\n");
        return 2;
    }
    CommandDb db;
    std::vector<LoadDiag> diag;
    if (!loadCommandDb(argv[1], argv[2], db, diag)) {
        std::printf("laden fehlgeschlagen\n");
        for (const LoadDiag& d : diag) {
            std::printf("  %s:%d %s\n", d.file.c_str(), d.line,
                        d.message.c_str());
        }
        return 1;
    }
    if (!diag.empty()) {
        std::printf("Meldungen beim Laden: %zu\n", diag.size());
        for (const LoadDiag& d : diag) {
            std::printf("  %s:%d %s\n", d.file.c_str(), d.line,
                        d.message.c_str());
        }
        std::printf("\n");
    }
    std::printf("Befehle: %zu   Tabellen: %zu   Makros: %zu\n\n",
                db.commands.size(), db.typesets.size(), db.macroBodies.size());
    std::printf("%-22s %6s %8s   %s\n",
                "Tabelle", "sicht", "nach eol", "erster / letzter sichtbarer Eintrag");
    std::printf("---------------------- ------ --------   "
                "-----------------------------------\n");
    std::size_t sichtbar = 0;
    std::size_t versteckt = 0;
    std::vector<std::string> namen;
    namen.reserve(db.typesets.size());
    for (const auto& kv : db.typesets) { namen.push_back(kv.first); }
    std::sort(namen.begin(), namen.end());
    for (const std::string& n : namen) {
        const TypeSet& t = db.typesets.at(n);
        sichtbar += t.entries.size();
        versteckt += static_cast<std::size_t>(t.hiddenAfterEol);
        std::printf("%-22s %6zu %8d   %s .. %s\n", n.c_str(), t.entries.size(),
                    t.hiddenAfterEol,
                    t.entries.empty() ? "-" : t.entries.front().name.c_str(),
                    t.entries.empty() ? "-" : t.entries.back().name.c_str());
    }
    std::printf("\nzusammen sichtbar %zu, nach #eol verworfen %zu\n",
                sichtbar, versteckt);

    // Gegenprobe: Eintraege, die laut Kopfdatei NICHT in der Liste stehen
    // duerfen, weil sie hinter `//# #eol` stehen.
    const char* darfNicht[] = {
        "BS_WAIT", "BS_STAND_GUARD", "BS_PATROL", "BS_INVESTIGATE",
        "BS_STAND_AND_SHOOT", "BS_HUNT_AND_KILL", "NUM_BSTATES",
        "INV_GOODIE_KEY", "INV_SECURITY_KEY", "INV_MAX",
        "TEAM_NUM_TEAMS", "WP_NUM_WEAPONS", "HL_MAX",
        "MAX_OBJECTIVES", "MAX_MISSIONFAILED", "MAX_STATUSTEXT",
    };
    std::printf("\nGegenprobe - diese stehen hinter #eol und duerfen NICHT "
                "angeboten werden:\n");
    int schlecht = 0;
    for (const char* verboten : darfNicht) {
        bool gefunden = false;
        std::string wo;
        for (const auto& kv : db.typesets) {
            for (const TypeEntry& e : kv.second.entries) {
                if (e.name == verboten) { gefunden = true; wo = kv.first; }
            }
        }
        if (gefunden) {
            std::printf("  FEHLER  %-22s steht in Tabelle %s\n", verboten,
                        wo.c_str());
            ++schlecht;
        }
    }
    if (schlecht == 0) {
        std::printf("  ok - keiner der %zu Eintraege wird angeboten\n",
                    sizeof(darfNicht) / sizeof(darfNicht[0]));
    }

    // Und die Gegenrichtung: Eintraege, die es GEBEN muss.
    const char* mussGeben[] = {
        "BS_DEFAULT", "BS_FLEE", "BS_CINEMATIC",
        "WP_SABER", "WP_CONCUSSION", "WP_NOGHRI_STICK",
        "CHAN_VOICE", "CHAN_VOICE_ATTEN", "CHAN_VOICE_GLOBAL", "CHAN_MUSIC",
        "TEAM_FREE", "TEAM_NEUTRAL",
        "INV_ELECTROBINOCULARS", "INV_SENTRY",
        "DM_AUTO", "DM_DEATH",
        "HL_HEAD", "HL_GENERIC6",
        "SET_NAVGOAL", "SET_BEHAVIOR_STATE", "SET_PARM8",
        // Achtung: das Handbuch schreibt SET_BEHAVIORSTATE (ohne
        // Unterstrich). Das ist die JK2-Form; JKA hat sie umbenannt. Beim
        // Schreiben dieser Probe bin ich selbst darauf hereingefallen und
        // hielt behaved fuer fehlerhaft. Es war das Handbuch.
    };
    std::printf("\nGegenprobe - diese MUESSEN angeboten werden:\n");
    int fehlt = 0;
    for (const char* noetig : mussGeben) {
        bool gefunden = false;
        for (const auto& kv : db.typesets) {
            for (const TypeEntry& e : kv.second.entries) {
                if (e.name == noetig) { gefunden = true; }
            }
        }
        if (!gefunden) { std::printf("  FEHLT   %s\n", noetig); ++fehlt; }
    }
    if (fehlt == 0) {
        std::printf("  ok - alle %zu vorhanden\n",
                    sizeof(mussGeben) / sizeof(mussGeben[0]));
    }
    return (schlecht + fehlt) == 0 ? 0 : 1;
}
