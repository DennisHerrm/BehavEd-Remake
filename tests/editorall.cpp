// Jeder Befehl der .bhc durch den Ereigniseditor und zurueck.
//
// Warum diese Probe:
//
// shank: "es geht mir darum das du die ganzen funktionen abgleichst mit if
// use sound, all die kleinen fenster die du oeffnen kannst und was
// eintragen kannst, dass die funktionieren. wenn man ein script schreibt".
//
// Der Abgleich der OBERFLAECHE (ABGLEICH.md) beantwortet das nicht: dass ein
// Fenster aufgeht und Felder hat, heisst nicht, dass hinten das richtige
// herauskommt. Drei Fehler dieser Art hat shank selbst gefunden - Save
// schrieb in die falsche Datei, Compile speicherte nicht mit, Expr! zeigte
// nur einen Helfer.
//
// Was hier laeuft, ist der Weg, den ein Klick auf "Ok" nimmt:
//
//     defaultValueFor(p)        was im Feld steht, wenn das Fenster aufgeht
//         -> argForParam(p, ...)   was daraus als Argument wird
//         -> writeScript()         wie es im Skript steht
//         -> parseScript()         was beim naechsten Oeffnen gelesen wird
//
// Geprueft wird, dass am Ende DERSELBE Befehl mit DENSELBEN Argumenten
// steht. Laeuft das fuer alle Befehle der .bhc durch, kann kein Fenster
// etwas schreiben, das sich nicht wieder lesen laesst.
#include "bhed/commands.h"
#include "bhed/edit.h"
#include "bhed/script.h"

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_fehler = 0;

void erwarte(const std::string& was, bool ok) {
    if (!ok) {
        std::printf("  FEHL  %s\n", was.c_str());
        ++g_fehler;
    }
}

std::string lies(const std::string& pfad) {
    std::ifstream f(pfad, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("  (keine .bhc angegeben - Test uebersprungen)\n");
        return 0;
    }
    bhed::CommandDb db;
    std::vector<bhed::LoadDiag> diag;
    const std::string bhc = argv[1];
    const std::string ordner = (argc > 2) ? argv[2] : std::string();
    if (!bhed::loadCommandDb(bhc, ordner, db, diag)) {
        std::printf("  FEHL  .bhc nicht lesbar\n");
        for (const bhed::LoadDiag& d : diag) {
            std::printf("        %s\n", d.message.c_str());
        }
        return 1;
    }

    const std::vector<bhed::Command>& befehle = db.commands;
    std::printf("  %zu Befehle aus der .bhc\n", befehle.size());

    int mitFeldern = 0;
    int felderGesamt = 0;
    for (const bhed::Command& c : befehle) {
        // Wie der Editor: fuer jedes Feld der Vorgabewert, dann das Argument.
        bhed::Node n;
        n.kind = bhed::Node::Kind::Command;
        n.name = c.name;
        n.hasBlock = c.block;
        for (const bhed::Param& p : c.params) {
            const std::string wert = defaultValueFor(p, db);
            n.args.push_back(argForParam(p, wert, db, nullptr));
        }
        if (!c.params.empty()) {
            ++mitFeldern;
            felderGesamt += static_cast<int>(c.params.size());
        }

        bhed::Script skript;
        skript.nodes.push_back(n);
        const std::string text = bhed::writeScript(skript);

        bhed::Script zurueck;
        std::vector<bhed::Diag> pdiag;
        if (!bhed::readScript(text, zurueck, pdiag)) {
            std::string wo = pdiag.empty() ? "" : pdiag[0].message;
            erwarte(c.name + ": nicht wieder lesbar (" + wo + ")", false);
            continue;
        }
        if (zurueck.nodes.size() != 1U) {
            erwarte(c.name + ": ergibt " +
                        std::to_string(zurueck.nodes.size()) +
                        " Knoten statt einem",
                    false);
            std::printf("        geschrieben: %s", text.c_str());
            continue;
        }
        const bhed::Node& z = zurueck.nodes[0];
        erwarte(c.name + ": Name bleibt", z.name == n.name);
        erwarte(c.name + ": Zahl der Argumente bleibt (" +
                    std::to_string(n.args.size()) + " -> " +
                    std::to_string(z.args.size()) + ")",
                z.args.size() == n.args.size());
        if (z.args.size() != n.args.size()) {
            std::printf("        geschrieben: %s", text.c_str());
            continue;
        }
        for (std::size_t i = 0; i < n.args.size(); ++i) {
            if (z.args[i].text != n.args[i].text) {
                erwarte(c.name + ": Argument " + std::to_string(i) +
                            " \"" + n.args[i].text + "\" -> \"" +
                            z.args[i].text + "\"",
                        false);
            }
            if (z.args[i].kind != n.args[i].kind) {
                erwarte(c.name + ": Argument " + std::to_string(i) +
                            " wechselt die Art",
                        false);
            }
        }
    }

    std::printf("  %d Befehle mit Feldern, %d Felder insgesamt\n", mitFeldern,
                felderGesamt);

    // --- Die FENSTERSEITE: stimmen Felder, Arten und Klapplisten? --------
    //
    // Bis hierher wurde geprueft, was hinten herauskommt. shank will beides:
    // "ob die Fenster die Felder alle stimmen und die funktion auch."
    //
    // Ein Fenster stimmt, wenn
    //   * es genau so viele Felder hat, wie die Signatur Parameter hat,
    //   * jede Feldart eine ist, die der Editor kennt,
    //   * jede Klappliste wirklich existiert und nicht leer ist,
    //   * ihr Vorgabewert auch DRINSTEHT - sonst zeigt das Fenster beim
    //     Oeffnen einen Eintrag, den die Liste nicht anbietet.
    int listen = 0;
    int eintraege = 0;
    for (const bhed::Command& c : befehle) {
        for (std::size_t i = 0; i < c.params.size(); ++i) {
            const bhed::Param& p = c.params[i];
            const std::string wo = c.name + " Feld " + std::to_string(i);
            switch (p.kind) {
                case bhed::Param::Kind::String:
                case bhed::Param::Kind::Int:
                case bhed::Param::Kind::Float:
                case bhed::Param::Kind::Vector:
                case bhed::Param::Kind::Range:
                case bhed::Param::Kind::Expr:
                case bhed::Param::Kind::Op:
                    break;
                case bhed::Param::Kind::TypeSet: {
                    const bhed::TypeSet* ts = db.typeset(p.typeset);
                    erwarte(wo + ": Klappliste \"" + p.typeset +
                                "\" gibt es nicht",
                            ts != nullptr);
                    if (ts == nullptr) { break; }
                    ++listen;
                    eintraege += static_cast<int>(ts->entries.size());
                    erwarte(wo + ": Klappliste \"" + p.typeset + "\" ist leer",
                            !ts->entries.empty());
                    const std::string vor = defaultValueFor(p, db);
                    erwarte(wo + ": Vorgabe \"" + vor +
                                "\" steht nicht in der Klappliste \"" +
                                p.typeset + "\"",
                            ts->entries.empty() || ts->find(vor) != nullptr);
                    break;
                }
                default:
                    erwarte(wo + ": Feldart kennt der Editor nicht", false);
                    break;
            }
        }
    }
    std::printf("  %d Klapplisten mit %d Eintraegen geprueft\n", listen,
                eintraege);

    // --- JEDER Eintrag JEDER Klappliste, nicht nur die Vorgabe -----------
    //
    // Eine Liste kann 60 Eintraege haben, und geprueft war bisher einer.
    // Ein einzelner, der falsch geschrieben wird - etwa ohne
    // Anfuehrungszeichen bei %s -, faellt sonst erst im Spiel auf.
    int listenproben = 0;
    for (const bhed::Command& c : befehle) {
        for (std::size_t f = 0; f < c.params.size(); ++f) {
            if (c.params[f].kind != bhed::Param::Kind::TypeSet) { continue; }
            const bhed::TypeSet* ts = db.typeset(c.params[f].typeset);
            if (ts == nullptr) { continue; }
            for (const bhed::TypeEntry& e : ts->entries) {
                bhed::Node n;
                n.kind = bhed::Node::Kind::Command;
                n.name = c.name;
                n.hasBlock = c.block;
                for (std::size_t i = 0; i < c.params.size(); ++i) {
                    const std::string wert =
                        (i == f) ? e.name : defaultValueFor(c.params[i], db);
                    n.args.push_back(argForParam(c.params[i], wert, db,
                                                 nullptr));
                }
                bhed::Script skript;
                skript.nodes.push_back(n);
                const std::string text = bhed::writeScript(skript);
                bhed::Script zurueck;
                std::vector<bhed::Diag> pdiag;
                ++listenproben;
                const std::string wo = c.name + " Feld " +
                                       std::to_string(f) + " = " + e.name;
                if (!bhed::readScript(text, zurueck, pdiag) ||
                    zurueck.nodes.size() != 1U ||
                    zurueck.nodes[0].args.size() != n.args.size()) {
                    erwarte(wo + ": Aufbau geht verloren", false);
                    std::printf("        geschrieben: %s", text.c_str());
                    continue;
                }
                if (bhed::writeScript(zurueck) != text) {
                    erwarte(wo + ": Text aendert sich beim zweiten Schreiben",
                            false);
                }
            }
        }
    }
    std::printf("  %d Proben ueber alle Klapplisteneintraege\n",
                listenproben);

    // --- Die ABHAENGIGEN Felder: was das Fenster wirklich zeigt ----------
    //
    // Bei `set` haengt das ZWEITE Feld vom ersten ab: `SET_ORIGIN` will
    // einen Vektor, `SET_PARM1` eine Zeichenkette. `editorFields` baut die
    // Liste danach auf - das ist die Funktion, die die Fenster macht.
    //
    // Geprueft wird fuer JEDEN Eintrag JEDER Klappliste: die Felder, die
    // das Fenster dann zeigt, mit ihren Vorgaben ausgefuellt, geschrieben,
    // gelesen, wieder geschrieben.
    int abhaengig = 0;
    int mitEigenen = 0;
    for (const bhed::Command& c : befehle) {
        for (std::size_t f = 0; f < c.params.size(); ++f) {
            if (c.params[f].kind != bhed::Param::Kind::TypeSet) { continue; }
            const bhed::TypeSet* ts = db.typeset(c.params[f].typeset);
            if (ts == nullptr) { continue; }
            for (const bhed::TypeEntry& e : ts->entries) {
                // Werte so setzen, wie sie im Fenster staenden.
                std::vector<std::string> werte(c.params.size());
                for (std::size_t i = 0; i < c.params.size(); ++i) {
                    werte[i] = defaultValueFor(c.params[i], db);
                }
                werte[f] = e.name;
                const std::vector<bhed::Param> felder =
                    bhed::editorFields(c, db, werte);
                const std::string wo = c.name + " mit " + e.name;
                if (!e.params.empty()) { ++mitEigenen; }
                erwarte(wo + ": das Fenster zeigt kein Feld",
                        !felder.empty());
                // Jedes gezeigte Feld muss eine Art haben, die der Editor
                // kennt, und jede Klappliste darin muss es geben.
                for (std::size_t i = 0; i < felder.size(); ++i) {
                    if (felder[i].kind != bhed::Param::Kind::TypeSet) {
                        continue;
                    }
                    // Zeigt ein Feld auf eine Klappliste, muss es sie
                    // geben. Sonst waere das Feld leer und der Befehl
                    // nicht schreibbar.
                    //
                    // Fehlt sie in den Daten - wie "TACTICAL", das Ravens
                    // Q3_Interface.h erklaert und keine Kopfdatei
                    // definiert -, macht `editorFields` ein Textfeld
                    // daraus. Dann steht hier gar keine Klappliste mehr,
                    // und diese Probe greift nicht.
                    erwarte(wo + ": Feld " + std::to_string(i) +
                                " zeigt auf die Klappliste \"" +
                                felder[i].typeset + "\", die es nicht gibt",
                            db.typeset(felder[i].typeset) != nullptr);
                }
                // Und der Befehl daraus muss den Rundlauf ueberstehen.
                bhed::Node n;
                n.kind = bhed::Node::Kind::Command;
                n.name = c.name;
                n.hasBlock = c.block;
                for (std::size_t i = 0; i < felder.size(); ++i) {
                    const std::string wert =
                        (i == f) ? e.name : defaultValueFor(felder[i], db);
                    n.args.push_back(argForParam(felder[i], wert, db,
                                                 nullptr));
                }
                bhed::Script skript;
                skript.nodes.push_back(n);
                const std::string text = bhed::writeScript(skript);
                bhed::Script zurueck;
                std::vector<bhed::Diag> pdiag;
                ++abhaengig;
                if (!bhed::readScript(text, zurueck, pdiag) ||
                    zurueck.nodes.size() != 1U ||
                    zurueck.nodes[0].args.size() != n.args.size()) {
                    erwarte(wo + ": Aufbau geht verloren", false);
                    std::printf("        geschrieben: %s", text.c_str());
                    continue;
                }
                if (bhed::writeScript(zurueck) != text) {
                    erwarte(wo + ": Text aendert sich beim zweiten Schreiben",
                            false);
                    std::printf("        erst: %s", text.c_str());
                    std::printf("        dann: %s",
                                bhed::writeScript(zurueck).c_str());
                }
            }
        }
    }
    std::printf("  %d Fensteraufbauten geprueft, %d mit eigenen Feldern\n",
                abhaengig, mitEigenen);

    // --- Zweiter Durchgang: UNANGENEHME Werte ----------------------------
    //
    // Der erste nimmt die Vorgabewerte. Die sind brav - kein Leerzeichen,
    // kein Anfuehrungszeichen, kein Minus. Was ein Benutzer eintippt, ist
    // es nicht.
    //
    // Hier steht jedes Feld nacheinander auf jedem dieser Werte, waehrend
    // die uebrigen ihre Vorgabe behalten. Geprueft wird dasselbe: kommt
    // nach Schreiben und Lesen derselbe Befehl mit denselben Argumenten
    // heraus?
    //
    // Das ist die Stelle, an der ein fehlendes Anfuehrungszeichen auffaellt
    // (Pruefcode V011) - im Bild sieht man es nicht, im Skript schon.
    static const char* const kBoese[] = {
        "",                        // leer
        "hat leerzeichen",         // Leerzeichen
        "mit \"anfuehrung\" drin",  // Anfuehrungszeichen
        "-1",                      // negativ
        "0 0 0",                   // wie ein Vektor
        "a;b",                     // Semikolon - Befehlsende
        "//kommentar",             // sieht aus wie ein Kommentar
        "$evaluate$",              // Sonderzeichen
    };
    int proben = 0;
    for (const bhed::Command& c : befehle) {
        for (std::size_t f = 0; f < c.params.size(); ++f) {
            for (const char* boese : kBoese) {
                bhed::Node n;
                n.kind = bhed::Node::Kind::Command;
                n.name = c.name;
                n.hasBlock = c.block;
                for (std::size_t i = 0; i < c.params.size(); ++i) {
                    const std::string wert =
                        (i == f) ? std::string(boese)
                                 : defaultValueFor(c.params[i], db);
                    n.args.push_back(argForParam(c.params[i], wert, db,
                                                 nullptr));
                }
                bhed::Script skript;
                skript.nodes.push_back(n);
                const std::string text = bhed::writeScript(skript);
                bhed::Script zurueck;
                std::vector<bhed::Diag> pdiag;
                ++proben;
                const std::string wo = c.name + " Feld " +
                                       std::to_string(f) + " = \"" +
                                       boese + "\"";
                if (!bhed::readScript(text, zurueck, pdiag)) {
                    erwarte(wo + ": nicht wieder lesbar", false);
                    continue;
                }
                if (zurueck.nodes.size() != 1U ||
                    zurueck.nodes[0].args.size() != n.args.size()) {
                    erwarte(wo + ": Aufbau geht verloren", false);
                    std::printf("        geschrieben: %s", text.c_str());
                    continue;
                }
                // Verglichen wird der TEXT nach einem zweiten Schreiben,
                // nicht die innere Darstellung.
                //
                // Grund: `$evaluate$` kommt als Arg::Kind::Expr mit dem Text
                // "evaluate" zurueck - die Dollarzeichen sind die
                // Schreibweise, nicht der Inhalt. Beim naechsten Schreiben
                // stehen sie wieder da. Wer die innere Darstellung
                // vergleicht, meldet das als Fehler, obwohl die Datei
                // unveraendert bleibt.
                //
                // Die Datei ist, worauf es ankommt: sie geht ins Spiel.
                const std::string nochmal = bhed::writeScript(zurueck);
                if (nochmal != text) {
                    erwarte(wo + ": Text aendert sich beim zweiten "
                                 "Schreiben", false);
                    std::printf("        erst:    %s", text.c_str());
                    std::printf("        dann:    %s", nochmal.c_str());
                }
            }
        }
    }
    std::printf("  %d Proben mit unangenehmen Werten\n", proben);
    if (g_fehler != 0) {
        std::printf("FEHLGESCHLAGEN (%d)\n", g_fehler);
        return 1;
    }
    std::printf("alle Editorproben bestanden (0 Fehlschlaege)\n");
    return 0;
}
