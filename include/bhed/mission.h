// mission.h - Missionen finden
//
// Eine Mission ist kein Dateiformat, sondern ein Zusammenhang: eine Karte,
// die Entities dazu, und die Skripte, auf die diese Entities zeigen. Nichts
// davon steht an einer Stelle - man muss es zusammensuchen.
//
// Der Weg, an echten Daten nachgemessen:
//
//   maps/md_tfoaj_jedi.bsp        die Geometrie
//   maps/md_tfoaj_jedi.ent        die Entities, falls vorhanden
//                                 (sonst die im .bsp)
//        -> target_scriptrunner   usescript "md_tfoaj/intro_jedi"
//        -> NPC_spawner           deathscript, NPC_type
//              scripts/md_tfoaj/intro_jedi.txt
//              models/players/<...>/model.glm
//
// Ein Befund aus zzz_The_Fear_of_a_Jedi.pk3: dort liegen ZWEI .bsp mit
// identischem Inhalt - duel_jt_dojo.bsp und md_tfoaj_jedi.bsp, gleiche
// Pruefsumme. Nur die zweite hat eine .ent daneben, und nur die ist die
// Mission. Eine Karte ohne Skriptverweise ist eine Duellkarte, keine
// Mission.
#ifndef BHED_MISSION_H
#define BHED_MISSION_H

#include "bhed/bsp.h"
#include "bhed/pk3.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace bhed {

struct Mission {
    std::string mapFile;      // "maps/md_tfoaj_jedi.bsp"
    std::string entFile;      // "maps/md_tfoaj_jedi.ent", leer wenn keine
    std::string name;         // "md_tfoaj_jedi"
    // Die Skripte, auf die die Entities zeigen - schon aufgeloest auf
    // wirklich vorhandene Dateien.
    struct Script {
        std::string ref;      // "md_tfoaj/intro_jedi"
        std::string file;     // "scripts/md_tfoaj/intro_jedi.txt"
        std::string key;      // "usescript", "deathscript", ...
    };
    std::vector<Script> scripts;
    // rc603: das Archiv, aus dem die Engine die Karte laedt (das gewinnende),
    // und ob das in "Load mission" GEWAEHLTE Archiv die .bsp oder die .ent
    // mitbringt - diese Missionen stehen in der Auswahl vorn.
    std::string archiv;
    bool imGewaehlten = false;

    // Eine Karte ohne Skripte ist keine Mission, sondern eine Duellkarte.
    [[nodiscard]] bool playable() const noexcept { return !scripts.empty(); }
};

// Aus einer Liste vorhandener Dateinamen die Missionen zusammenstellen.
//
// entitiesFor liefert den Inhalt einer Datei; so bleibt die Funktion frei von
// Archiven und Dateisystem und laesst sich mit erfundenen Namen pruefen.
using FileReader = std::function<bool(const std::string& name, std::string& out)>;

[[nodiscard]] std::vector<Mission> findMissions(
    const std::vector<std::string>& fileNames, const FileReader& read);

// --- rc603: die Missionen eines GANZEN Spiels --------------------------------
//
// Gemeldet: mit den Daten des reinen JKA zeigte "Load mission" keine Mission.
// Die Auswahl nahm EIN .pk3 und suchte die Missionen ueber die Karten genau
// dieses Archivs. In JKA (und JK2) liegen Karten, Entities und Skripte aber
// ueber assets0..assets3 (JK2: ..assets5) verteilt - nur assets0 ergab
// Missionen, die anderen eine leere Liste. Und die Skripte einer Karte wurden
// nur im selben Archiv gesucht.
//
// Jetzt gilt das Spiel des Archivs: alle Archive seiner Spielordner in der
// Suchreihenfolge der Engine. Je Name zaehlt nur die Datei, die die Engine
// laedt - das spaetere Archiv gewinnt, der spaetere Spielordner gewinnt
// (FS_AddGameDirectory, code/qcommon/files.cpp:2758-2759 OpenJK: jeder
// Eintrag wird VORN in fs_searchpaths eingehaengt; FS_Startup :2897-2945:
// erst "base", dann fs_game).

// Ein Archiv im Suchpfad, in Suchreihenfolge (das spaetere gewinnt), mit
// allen Dateinamen darin.
struct SpielArchiv {
    std::string pfad;
    std::vector<std::string> namen;
};

// Eine Karte des Spiels, so wie die Engine sie findet: jeder Name einmal, aus
// dem Archiv, das gewinnt. Ebenso die .ent daneben - Movie Duels laedt sie
// ueber das Dateisystem, aus welchem Archiv auch immer.
struct MissionsKarte {
    std::string name;              // "t1_sour"
    std::string bsp;               // "maps/t1_sour.bsp" (Schreibweise im Archiv)
    std::size_t bspArchiv = 0;     // Index in den Archiven
    std::string ent;               // leer = keine .ent
    std::size_t entArchiv = 0;
    bool imGewaehlten = false;     // das gewaehlte Archiv bringt .bsp oder .ent mit
};

// Alle Karten (maps/**.bsp) der Archive, nach Namen sortiert. `gewaehlt`: der
// Pfad des gewaehlten Archivs (ohne Gross/Klein, / und \ gleich).
[[nodiscard]] std::vector<MissionsKarte> missionsKarten(const std::vector<SpielArchiv>& archive,
                                                        const std::string& gewaehlt);

// Alle Dateinamen unter scripts/ in Kleinschreibung - das Aufloesen der
// Skriptverweise sucht darin (in JKA liegen die Skripte nicht unbedingt im
// Archiv der Karte, sondern wo immer das Spiel sie findet).
[[nodiscard]] std::unordered_set<std::string> skriptNamen(const std::vector<SpielArchiv>& archive);

// Die Skriptverweise einer Karte aus ihren Entities (collectNames).
[[nodiscard]] std::vector<MapData::ScriptRef> kartenSkriptVerweise(std::vector<MapEntity> ents);

// Eine Mission aus einer Karte und ihren Verweisen; `gibt` sagt, ob es eine
// Datei gibt (beliebige Schreibweise).
using DateiGibt = std::function<bool(const std::string& name)>;
[[nodiscard]] Mission missionAus(const MissionsKarte& k, const std::vector<MapData::ScriptRef>& verweise,
                                 const DateiGibt& gibt);

// Die Reihenfolge der Auswahl: erst die Missionen des gewaehlten Archivs,
// darin und danach die spielbaren zuerst, dann nach Namen.
void sortiereMissionen(std::vector<Mission>& missionen);

// Was eine Karte zum Lesen ihrer Entities braucht: Archivpfad und
// Verzeichniseintrag von .bsp und (falls vorhanden) .ent.
struct MissionsAuftrag {
    std::string bspArchiv;
    Pk3Entry bsp;
    std::string entArchiv;          // leer = keine .ent
    Pk3Entry ent;
};

// Die Skriptverweise einer Karte lesen: die .ent, wenn es sie gibt (sie
// ersetzt die Entities der .bsp), sonst Lump 0 der .bsp. Ohne Protokoll und
// ohne gemeinsamen Zustand - laeuft in Arbeitsfaeden. false: nicht lesbar.
[[nodiscard]] bool leseSkriptVerweise(const MissionsAuftrag& a, std::vector<MapData::ScriptRef>& out);

// Liest viele Karten im Hintergrund (eigene Faeden). Die Entities stehen in
// fast jeder .bsp am ENDE (289 von 291 Karten in base und Movie Duels: Lump 0
// liegt hinter der Geometrie) - jede Karte muss also ganz entpackt werden,
// bei Movie Duels rund 3 GB. Das geschieht nicht im Bild.
class MissionsSuche {
public:
    MissionsSuche(std::vector<MissionsAuftrag> auftraege, unsigned faeden);
    ~MissionsSuche();
    MissionsSuche(const MissionsSuche&) = delete;
    MissionsSuche& operator=(const MissionsSuche&) = delete;
    MissionsSuche(MissionsSuche&&) = delete;
    MissionsSuche& operator=(MissionsSuche&&) = delete;

    [[nodiscard]] std::size_t gesamt() const { return auftraege_.size(); }
    [[nodiscard]] std::size_t erledigt() const { return erledigt_.load(); }
    [[nodiscard]] bool fertig() const { return erledigt_.load() >= auftraege_.size(); }
    // Erst nach fertig() oder abbrechen() lesen: je Auftrag die Verweise;
    // bearbeitet()[i] == 0, wenn die Karte nicht mehr drankam (abgebrochen),
    // gelesen()[i] == 0, wenn sie nicht lesbar war.
    [[nodiscard]] const std::vector<std::vector<MapData::ScriptRef>>& verweise() const { return verweise_; }
    [[nodiscard]] const std::vector<char>& gelesen() const { return gelesen_; }
    [[nodiscard]] const std::vector<char>& bearbeitet() const { return bearbeitet_; }
    // Bricht ab und wartet auf die Faeden (hoechstens eine Karte je Faden).
    // Was bis dahin gelesen ist, bleibt (bearbeitet).
    void abbrechen();

private:
    void arbeite();
    std::vector<MissionsAuftrag> auftraege_;
    std::vector<std::vector<MapData::ScriptRef>> verweise_;
    std::vector<char> gelesen_;
    std::vector<char> bearbeitet_;
    std::atomic<std::size_t> naechster_{0};
    std::atomic<std::size_t> erledigt_{0};
    std::atomic<bool> halt_{false};
    std::vector<std::thread> faeden_;
};

// --- rc603: welche Spielordner gehoeren zu einem gewaehlten Archiv? -----------
//
// Vorher nahm "Load mission" den Ordner des Archivs DAUERHAFT in die
// Spielordner auf. Eine JK2-pk3 (oder ein zweites JKA) mischte sich so fuer
// immer unter die eingerichteten - als spaeterer Ordner gewann sie sogar.
//
// Jetzt wird nichts mehr dauerhaft aufgenommen. Gilt ein anderes Spiel, dann
// nur fuer diese Sitzung (die Einstellungen bleiben unberuehrt):
//   * Der Ordner ist eingerichtet: die eingerichteten Spielordner.
//   * Er gehoert zu den Spielordnern dieser Sitzung: diese.
//   * Er heisst "base": eine eigene Installation - nur er (FS_Startup mit
//     BASEGAME allein, ohne fs_game).
//   * Daneben liegt ein "base" mit .pk3: eine Mod dieser Installation -
//     base, dann der Ordner (FS_Startup: base, dann fs_game).
//   * Sonst (eine lose Mod, etwa im Download-Ordner): die eingerichteten
//     Spielordner und der Ordner dahinter, als gewinnender.
struct SpielordnerWahl {
    enum class Art { Eingerichtet, Sitzung, Installation, Lose };
    Art art = Art::Eingerichtet;
    std::vector<std::string> ordner;   // in Suchreihenfolge, der spaetere gewinnt
};
[[nodiscard]] SpielordnerWahl spielordnerFuerArchiv(const std::string& archivPfad,
                                                    const std::vector<std::string>& eingerichtet,
                                                    const std::vector<std::string>& sitzung,
                                                    const std::function<bool(const std::string& ordner)>& hatPk3);
// Derselbe Ordner? Ohne Gross/Klein, / und \ gleich, ohne Schraegstrich am Ende.
[[nodiscard]] bool gleicherOrdner(const std::string& a, const std::string& b);
// Der Ordner einer Datei ("C:\x\base\assets1.pk3" -> "C:\x\base").
[[nodiscard]] std::string ordnerVonDatei(const std::string& pfad);

}  // namespace bhed
#endif
