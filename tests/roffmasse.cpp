// roffmasse.cpp - JEDE .rof der Spielordner lesen und abspielen.
//
// rofftest prueft kleine Dateien, die es nur im Speicher gibt. Diese Probe
// laeuft ueber alles, was in den Archiven liegt (bei Movie Duels samt
// Grundspiel rund hundert Dateien), und prueft:
//
//   * liest sie sich wie in G_LoadRoff?
//   * laesst sie sich abspielen: jede Zeit ergibt endliche Zahlen, der
//     Endstand stimmt mit der Summe der Versaetze ueberein, die Notizen
//     zeigen in die Liste?
//   * welche Namen aus play ( "PLAY_ROFF", "..." ) in den Skripten finden
//     KEINE Datei? Das ist kein Fehler dieser Probe - die Engine laedt dann
//     ebenfalls nichts -, aber es gehoert in den Bericht. Die Namen werden
//     grob aus den .ibi/.txt gefischt (die Zeichenkette nach "PLAY_ROFF").
//
// Aufruf:  roffmasse <spielordner> [<spielordner> ...]
// Ohne Ordner: "uebersprungen", Rueckgabe 0 (wie meshmasse).
// Rueckgabe 1, sobald eine Datei nicht lesbar ist oder sich nicht sauber
// abspielen laesst.
#include "bhed/pk3.h"
#include "bhed/roff.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace bhed;

namespace {

std::string klein(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool endetAuf(const std::string& s, const char* e) {
    const std::string x = e;
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

// Der LETZTE Fund gewinnt, wie in der Engine.
bool lies(const std::vector<GamePath>& pfade, const std::string& name, std::string& out) {
    bool da = false;
    for (const GamePath& gp : pfade) {
        for (const Pk3& a : gp.archives) {
            const Pk3Entry* e = a.find(name);
            if (e != nullptr) {
                std::string d;
                if (readPk3File(a, *e, d)) {
                    out = std::move(d);
                    da = true;
                }
            }
        }
    }
    return da;
}

bool endlich(const RoffStand& s) {
    for (int a = 0; a < 3; ++a) {
        if (!std::isfinite(s.ort[a]) || !std::isfinite(s.winkel[a])) {
            return false;
        }
    }
    return true;
}

// Namen nach "PLAY_ROFF" in einem Skript. In einer .ibi folgt auf die
// Zeichenkette "PLAY_ROFF\0" ein Mitgliedskopf (Typ, Laenge) und dann der
// Name; in Klartext steht er in Anfuehrungszeichen. Beides erfasst: die
// naechste Folge druckbarer Zeichen ab drei Zeichen Laenge, die kein
// Kopf ist.
void fischeNamen(const std::string& d, std::vector<std::string>& out) {
    std::size_t p = 0;
    while ((p = d.find("PLAY_ROFF", p)) != std::string::npos) {
        p += 9;
        std::size_t q = p;
        const std::size_t grenze = std::min(d.size(), p + 64U);
        std::string name;
        while (q < grenze) {
            const auto c = static_cast<unsigned char>(d[q]);
            const bool druckbar = c >= 0x21 && c < 0x7F && c != '"' && c != ',' && c != '(' && c != ')';
            if (druckbar) {
                name += static_cast<char>(c);
            } else if (name.size() >= 3) {
                break;
            } else {
                name.clear();
            }
            ++q;
        }
        if (name.size() >= 3) {
            out.push_back(name);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> ordner;
    for (int i = 1; i < argc; ++i) {
        ordner.emplace_back(argv[i]);
    }
    if (ordner.empty()) {
        std::printf("roffmasse: kein Spielordner angegeben - uebersprungen\n");
        return 0;
    }
    std::vector<GamePath> pfade;
    for (const std::string& o : ordner) {
        GamePath gp;
        if (scanGamePath(o, gp)) {
            std::printf("%s: %zu Archive, %d Dateien\n", o.c_str(), gp.archives.size(), gp.fileCount);
            pfade.push_back(std::move(gp));
        } else {
            std::printf("%s: nicht lesbar - uebersprungen\n", o.c_str());
        }
    }
    if (pfade.empty()) {
        std::printf("roffmasse: kein lesbarer Spielordner - uebersprungen\n");
        return 0;
    }
    std::set<std::string> gesehen;
    std::vector<std::string> rofs;
    std::vector<std::string> skripte;
    for (const GamePath& gp : pfade) {
        for (const FoundFile& f : findByExtension(gp, {".rof", ".ibi", ".txt"})) {
            const std::string k = klein(f.name);
            if (!gesehen.insert(k).second) {
                continue;
            }
            if (endetAuf(k, ".rof")) {
                rofs.push_back(f.name);
            } else if (k.rfind("scripts/", 0) == 0) {
                skripte.push_back(f.name);
            }
        }
    }

    int fehlerhaft = 0;
    int mitHinweis = 0;
    int notizen = 0;
    std::map<int, int> fassungen;
    std::map<int, int> takte;
    std::set<std::string> vorhanden;
    std::string daten;
    for (const std::string& n : rofs) {
        vorhanden.insert(klein(n));
        if (!lies(pfade, n, daten)) {
            std::printf("  FEHLER %s: nicht auszupacken\n", n.c_str());
            ++fehlerhaft;
            continue;
        }
        Roff r;
        std::string grund;
        if (!leseRoff(daten, r, &grund)) {
            std::printf("  FEHLER %s: %s\n", n.c_str(), grund.c_str());
            ++fehlerhaft;
            continue;
        }
        ++fassungen[r.fassung];
        ++takte[r.bildMs];
        if (!r.hinweise.empty()) {
            ++mitHinweis;
            for (const std::string& h : r.hinweise) {
                std::printf("  Hinweis %s: %s\n", n.c_str(), h.c_str());
            }
        }
        // Abspielen, fuer Mover und Figur, ueber die ganze Laufzeit.
        const float null3[3] = {0.0F, 0.0F, 0.0F};
        const RoffBahn b = roffBahn(r, null3, null3);
        const double ende = roffLaufzeitMs(r);
        bool gut = b.stand.size() == r.bilder.size() + 1U;
        for (double t = 0.0; gut && t <= ende + 100.0; t += 25.0) {
            gut = endlich(roffStand(r, b, t, false)) && endlich(roffStand(r, b, t, true));
        }
        // Der Endstand ist die Summe der Versaetze (Figur) bzw. ohne den
        // letzten (Mover) - hier unabhaengig in double nachgerechnet.
        double sx = 0.0;
        double letzt = 0.0;
        for (const RoffBild& bi : r.bilder) {
            sx += bi.ort[0];
            letzt = bi.ort[0];
        }
        const RoffStand ef = roffStand(r, b, ende + 1000.0, true);
        const RoffStand em = roffStand(r, b, ende + 1000.0, false);
        const double tol = 1e-3 * (1.0 + std::fabs(sx)) + 1e-4 * static_cast<double>(r.bilder.size());
        if (gut && (std::fabs(ef.ort[0] - sx) > tol || std::fabs(em.ort[0] - (sx - letzt)) > tol)) {
            std::printf("  FEHLER %s: Endstand %.3f/%.3f statt %.3f/%.3f\n", n.c_str(), static_cast<double>(ef.ort[0]),
                        static_cast<double>(em.ort[0]), sx, sx - letzt);
            gut = false;
        }
        if (!gut) {
            std::printf("  FEHLER %s: laesst sich nicht sauber abspielen\n", n.c_str());
            ++fehlerhaft;
            continue;
        }
        for (const RoffAusloesung& a : roffAusloesungen(r)) {
            ++notizen;
            const RoffNotizBefehl nb = zerlegeRoffNotiz(r.notizen[a.notiz]);
            static const char* const kArt[] = {"nichts", "Effekt", "Klang", "unbekannt"};
            std::printf("  Notiz %s Bild %zu (%.0f ms): \"%s\" -> %s\n", n.c_str(), a.bild, a.ms,
                        r.notizen[a.notiz].c_str(), kArt[static_cast<int>(nb.art)]);
        }
    }

    // Die Namen aus den Skripten gegen die vorhandenen Dateien.
    std::map<std::string, int> genannt;
    for (const std::string& s : skripte) {
        if (!lies(pfade, s, daten)) {
            continue;
        }
        std::vector<std::string> namen;
        fischeNamen(daten, namen);
        for (const std::string& nm : namen) {
            ++genannt[nm];
        }
    }
    int aufrufe = 0;
    int fehlend = 0;
    for (const auto& [nm, anz] : genannt) {
        aufrufe += anz;
        if (vorhanden.count(klein(roffPfad(nm))) == 0) {
            ++fehlend;
            std::printf("  fehlt: %s (%dx in Skripten) - G_LoadRoff scheitert auch im Spiel\n",
                        roffPfad(nm).c_str(), anz);
        }
    }

    std::printf("\n%zu ROFF-Dateien:", rofs.size());
    for (const auto& [f, a] : fassungen) {
        std::printf(" Fassung %d x%d", f, a);
    }
    std::printf(";");
    for (const auto& [t, a] : takte) {
        std::printf(" %d ms x%d", t, a);
    }
    std::printf("\n%d mit Hinweis, %d ausgeloeste Notizen\n", mitHinweis, notizen);
    std::printf("PLAY_ROFF in Skripten: %d Aufrufe, %zu Namen, %d ohne Datei\n", aufrufe, genannt.size(), fehlend);
    std::printf("%s (%d fehlerhafte Dateien)\n", fehlerhaft != 0 ? "FEHLGESCHLAGEN" : "alle ROFFs gelesen und abgespielt",
                fehlerhaft);
    return fehlerhaft != 0 ? 1 : 0;
}
