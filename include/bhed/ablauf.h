// ablauf.h - ein ICARUS-Nachbau: was laeuft wann, auf wem, ueber ALLE Skripte
//
// Bis hierher wurde ein Skript nicht ausgefuehrt, sondern abgeschaetzt:
// timeline.cpp, camtrack.cpp und scene.cpp liefen je fuer sich durch den
// Baum des OFFENEN Skripts und zaehlten die waits zusammen. Damit fehlte
// alles, was eine Zwischensequenz im Spiel wirklich zusammenhaelt:
//
//   * `do ( "task" )` auf oberster Ebene - die Kamera darin lief nie
//   * `run ( "andere/datei" )`, `affect` in `affect`, in `task`, in `loop`
//   * `dowait`, das wartet, bis Stimme, Weg und Animation fertig sind
//   * `use` auf target_scriptrunner, NPC_spawner, target_relay, -delay,
//     -counter, trigger_* - also alles, was weitere Skripte und Figuren
//     ins Spiel bringt
//   * der Takt: die Spiellogik laeuft in Bildern zu 50 ms (sv_fps 20,
//     sv_main.cpp:538), und ein wait ist erst fertig, wenn
//
//         task->GetTimeStamp() + dwtime < GetTime()      (TaskManager.cpp:1100)
//
//     also im Bild NACH dem Ablauf. wait ( 1000 ) dauert im Spiel 1050 ms.
//     Eine Kamera mit zehn waits laeuft damit eine halbe Sekunde hinter
//     einer Figur mit einem einzigen wait gleicher Laenge her - genau die
//     Art Versatz, die im Editor anders aussah als im Spiel.
//
// Diese Datei fuehrt die Skripte so aus, wie CSequencer und CTaskManager
// es tun (code/icarus/), Bild fuer Bild, auf jeder beteiligten Entity
// gleichzeitig. Heraus kommt ein FLACHES Skript: oben die Befehle des
// Traegers, darunter je Entity ein affect-Block mit genau den Befehlen, die
// sie ausgefuehrt hat, in zeitlicher Folge und mit waits dazwischen. Das
// koennen die bisherigen Auswerter ohne Aenderung lesen - und jeder Befehl
// darin weiss ueber `herkunft`, aus welcher Zeile welchen Skripts er stammt.
#ifndef BHED_ABLAUF_H
#define BHED_ABLAUF_H

#include "bhed/bsp.h"
#include "bhed/navlauf.h"
#include "bhed/script.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace bhed {

// Rueckgabe von AblaufWelt::roffDauer: diese ROFF-Datei gibt es nicht.
//
// Dann ruft Play nie Q3_TaskIDSet (Q3_Interface.cpp:10690), und
// CTaskManager::Play meldet selbst kein Completed (TaskManager.cpp:1603) -
// die Aufgabe bleibt fuer immer offen, ein dowait darauf haengt. Das ist
// etwas anderes als "unbekannt" (keine Archive geladen), wo der Nachbau wie
// ueberall den Befehl als sofort erledigt nimmt.
inline constexpr double kRoffFehlt = -2.0;
// rc604 (A2 Rest): die Bahn endet nie (OpenJK "loop rof", roffMitSchleife) -
// G_Roff ruft Q3_TaskIDComplete nie, ein dowait darauf haengt.
inline constexpr double kRoffEndlos = -3.0;

// Was der Nachbau von aussen braucht. Alles darf fehlen - dann gilt der
// Befehl als sofort erledigt, wie in der Engine bei einer unbekannten
// Animation.
struct AblaufWelt {
    const MapData* karte = nullptr;
    // "md_ga/intro_jedi" -> das gelesene Skript, oder nullptr.
    std::function<const Script*(const std::string& pfad)> skript;
    // Laenge einer Klangdatei in ms (<= 0: unbekannt).
    std::function<double(const std::string& datei)> klangDauer;
    // Laenge einer Animation fuer diese Figur in ms (< 0: unbekannt).
    std::function<double(const std::string& figur, const std::string& anim)> animDauer;
    // Geh-/Lauftempo einer Figur aus ihrer .npc, Einheiten je Sekunde
    // (< 0: die Vorgaben der Engine, 90 und 300). rc601 (bewegung): 0 ist
    // gueltig - die Figur steht (NPC_ParseParms verwirft nur n < 0). Die
    // Oberflaeche nimmt ablaufTempo() aus bhed/scene.h.
    std::function<float(const std::string& figur, bool gehen)> tempo;
    // Wie lange eine ROFF-Bahn laeuft, bis G_Roff ihre Aufgabe erledigt
    // (roffLaufzeitMs aus bhed/roff.h), fuer den Namen aus
    // play ( "PLAY_ROFF", "<name>" ). Negativ: unbekannt - dann gilt play
    // als sofort erledigt. kRoffFehlt: die Datei gibt es nicht - dann
    // bleibt die Aufgabe offen, wie im Spiel.
    std::function<double(const std::string& name)> roffDauer;
    // rc604 (A2 Rest, nur OpenJK): die Skripte, die Notizen "USE <skript>"
    // der Bahn starten (RunScript(ent, argument), code/game/g_roff.cpp:
    // 308-319), mit ihrer Zeit nach dem play-Befehl.
    std::function<std::vector<std::pair<double, std::string>>(const std::string& name)> roffSkripte;
    // Takt der Spiellogik. 0 schaltet ihn ab (nur fuer Proben).
    double bildMs = 50.0;
    // rc601 (bewegung, Wege wie im Spiel): Wegnetz und Spuren der Karte fuer
    // NAV/STEER (bhed/navlauf.h) - dieselbe Welt wie SzenenHilfe::nav, sonst
    // endet dowait anders, als die Szene ankommt. Leer: das Netz aus den
    // Entities der Karte ohne Spuren (navWeltAusEntities). Dazu der Kasten
    // je Figur (halbe Breite, maxs[2]) wie in der Szene; leer: 16 und 40.
    std::shared_ptr<const NavWelt> nav;
    std::function<float(const std::string& figur)> halbBreite;
    std::function<float(const std::string& figur)> hoeheOben;
    // rc601 (bewegung, R5/R6): die Drehrate beim Spawnen (Grad/s, < 0
    // unbekannt), ob sie fest ist (Rocket Trooper, WP_EMPLACED_GUN -
    // SET_YAWSPEED wirkt dann nicht), und die Stufe je Klasse.
    std::function<float(const std::string& figur)> drehrate;
    std::function<bool(const std::string& figur)> drehrateFest;
    std::function<float(const std::string& figur)> stufe;
    // rc605: SCF_NAV_CAN_JUMP je Figur wie in der Szene (Actor::kannSpringen) -
    // NPC-Spruenge im Weg (LaufWerte::kannSpringen).
    std::function<bool(const std::string& figur)> kannSpringen;
    // rc605: die Ziele explodierender misc_trip_mine aus der Spur der Szene
    // (laserfalleLauf): WP_Explode -> G_UseTargets( self, attacker ), attacker
    // ist die Falle selbst (eine Karten-Falle hat weder owner noch activator,
    // laserTrapThink: activator = owner = NULL; g_weapon.cpp:185-224 OpenJK).
    // Der Nachbau benutzt `ziel` zur Zeit `ms` (entity: Index der Falle in
    // karte->entities) - target_counter, target_relay und usescript laufen
    // dann hier, und MoverSim bekommt die aufgeloesten Benutzungen.
    struct FallenUse {
        double ms = 0.0;
        std::string ziel;
        int entity = -1;
        bool operator==(const FallenUse&) const = default;
    };
    std::vector<FallenUse> fallenUses;
    // rc601 (bewegung, Durchsicht 2): die Drehrate je Denkbild nach der Waffe
    // in der Hand (SET_WEAPON, NPC_utils.cpp:274-290 MD) - ablaufDrehrate und
    // ablaufDrehrateYaw aus bhed/scene.h. Gesetzt, ersetzen sie drehrate/
    // drehrateFest.
    std::function<float(const std::string& figur, const std::string& waffe, float yawRate)> drehrateNpc;
    std::function<float(const std::string& figur)> drehrateYaw;
    // rc601 integ: der Takt des Verhaltens je Figur und Waffe in der Hand
    // (ablaufVerhaltenTakt, bhed/scene.h; LaufWerte::verhaltenTaktMs). Ohne:
    // Movie Duels 50, OpenJK 100 (ohne die Jedi-Ausnahme).
    std::function<int(const std::string& figur, const std::string& waffe)> verhaltenTakt;
    // rc601 (bewegung, teil 3): Fallen und Vorlauf je Figur wie in der Szene
    // (ablaufVorlauf in bhed/scene.h): -2 faellt nicht (Flieger, Fahrzeug),
    // -1 faellt, aber kein Vorlauf ab dem Spawn (Spieler, haengt), 0 Vorlauf
    // ohne G_CheckInSolid (SFB_STARTINSOLID), 1 mit. Leer: NPCs 1, sonst -1.
    std::function<int(const std::string& figur)> vorlauf;
    // rc605 (fix605): bleibt die Figur nach dem Tod ein Koerper? CorpsePhysics
    // setzt CONTENTS_CORPSE nur, wenn NPC_class weder CLASS_MARK1 noch
    // CLASS_INTERROGATOR ist (NPC.cpp:209-216 MD / 122-127 OpenJK)
    // (ablaufBleibtFest, bhed/scene.h). Ohne: jede wird zur Leiche.
    std::function<bool(const std::string& figur)> bleibtFest;
    // rc601 (bewegung, teil 3): Plaene aus dem gemeinsamen Takt aller Figuren
    // (navKoerperLauf), nach navKoerperSchluessel. Fehlt einer: planeLauf.
    std::shared_ptr<const std::map<std::uint64_t, std::shared_ptr<const LaufPlan>>> koerperPlaene;
    // Spaetestens hier ist Schluss - loop ( -1 ) laeuft sonst ewig.
    double grenzeMs = 600000.0;
    // Nach dem Ende des Startskripts noch so lange weiterlaufen lassen
    // (Figuren, die noch laufen; Klaenge, die noch klingen).
    double nachlaufMs = 3000.0;
    // rc603: g_spskill fuer den Kartenstart (simuliereKartenstart) - wer bei
    // dieser Schwierigkeit nicht spawnt (SpawnForCurrentDifficultySetting),
    // startet auch kein Skript. < 0: alle Entities.
    int spielstufe = -1;
};

// Woher ein Befehl im flachen Skript stammt.
struct AblaufHerkunft {
    int skript = -1;   // Index in Ablauf::skripte; -1 = vom Nachbau eingefuegt
    Path path;         // Pfad im Baum dieses Skripts
};

struct Ablauf {
    // [0] ist das Startskript, danach alles, was per run, use oder
    // spawnscript dazukam - als Pfad wie "md_ga/kill_fett2".
    std::vector<std::string> skripte;
    // rc603: parallel zu skripte - wann das Skript zum ersten Mal loslief
    // (ms; -1 = nur geladen, nie gestartet) und an welcher Stelle unter allen
    // Starts (bei gleicher Zeit entscheidet sie).
    std::vector<double> skriptBeginn;
    std::vector<int> skriptFolge;
    // Wer das Startskript ausfuehrt (targetname), leer = ein
    // target_scriptrunner ohne Namen oder unbekannt.
    std::string traeger;
    bool traegerIstFigur = false;

    Script flach;
    std::map<Path, AblaufHerkunft> herkunft;

    // Figuren, die erst waehrend des Ablaufs entstehen (NPC_spawner per
    // use): Name -> ab wann es sie gibt. Vorher sind sie nicht zu sehen.
    std::map<std::string, double> erscheint;

    // rc604 fix604: gemeldet "Nicht jede Figur muss fuer jedes Script
    // gleichzeitig laufen ... ich brauche keine 5 Obi-Wans alle zeigen lassen,
    // wenn sie in dem Moment nicht gebraucht werden". Die Darsteller anderer
    // Zwischensequenzen (kinoDarsteller: NPC_targetname + SFB_CINEMATIC, beim
    // Laden gespawnt), die dieser Ablauf nie anspricht - klein geschrieben.
    // Die Szene legt sie nicht an (SzenenHilfe::fremdeDarsteller), der
    // gemeinsame Takt fuehrt sie nicht als Koerper. Leer im Kartenstart.
    std::set<std::string> fremdeDarsteller;

    // Jede Entity, die per use angesprochen wurde - auch ueber
    // target_relay, target_delay, target_counter und trigger_* hinweg,
    // mit der Zeit, zu der es im Spiel ankommt. Die Kette ist hier schon
    // aufgeloest; wer das liest, reicht NICHT noch einmal weiter.
    struct Benutzung {
        double ms = 0.0;
        std::string name;   // targetname
        // Wer benutzt hat (der activator der Engine): der Name der Entity,
        // auf der das Skript lief, auch ueber Relais hinweg. Leer =
        // unbekannt. target_print zeigt nur, was der SPIELER ausloest.
        std::string ausloeser;
    };
    std::vector<Benutzung> benutzt;

    std::vector<std::string> hinweise;
    double endeMs = 0.0;    // wann das Startskript fertig war
    double laufMs = 0.0;    // wie weit simuliert wurde
    int befehle = 0;        // ausgefuehrte Befehle, fuer das Protokoll

    [[nodiscard]] const AblaufHerkunft* woher(const Path& p) const;

    // rc601 (bewegung, teil 3): jeder Plan dieser Rechnung (planeLauf ohne die
    // anderen Figuren) mit Schluessel (navKoerperSchluessel) und die Figuren
    // als Kaesten - fuer den gemeinsamen Takt (navKoerperLauf).
    std::vector<NavAbschnitt> laufAbschnitte;
    std::vector<std::uint64_t> laufSchluessel;
    std::vector<NavFigurKoerper> laufFiguren;
    std::vector<std::string> laufFigurNamen;
};

// Das Startskript `start` (dessen Pfad `startPfad` ist, z.B.
// "md_ga/grievous_jedi"; leer, wenn unbekannt) ausfuehren.
[[nodiscard]] Ablauf simuliereAblauf(const Script& start, const std::string& startPfad,
                                     const AblaufWelt& welt);
// rc601 (bewegung, teil 3): dasselbe mit allen Figuren im selben Takt -
// navKoerperLauf ueber die Plaene der Rechnung, dann noch einmal mit dessen
// Plaenen (welt.koerperPlaene), bis sie gleich bleiben (hoechstens 7 Rechnungen;
// ein Plan zur Zeit t haengt nur an dem, was vor t geschah - jeder Durchgang
// macht mindestens das naechste Ereignis richtig). welt.koerperPlaene ist danach
// das, was auch die Szene nehmen muss (SzenenHilfe::koerperPlaene).
[[nodiscard]] Ablauf simuliereAblaufMitKoerpern(const Script& start, const std::string& startPfad, AblaufWelt& welt,
                                                int* durchgaenge = nullptr);
// rc601 integ: die Abkuerzung in simuliereAblaufMitKoerpern (bekommt eine
// Rechnung des gemeinsamen Takts Bit fuer Bit dieselben Eingaben wie die
// vorige, endet die Schleife ohne sie) - an (Vorgabe) oder aus, nur fuer die
// Probe P4b, die beide Wege Bit fuer Bit vergleicht.
void ablaufKoerperAbkuerzungAn(bool an);

// rc603: die Karte vom Laden an, ohne Startskript - was die Engine beim Laden
// selbst anstoesst (spawnscripts der Entities, worldspawn, trigger_always,
// NPC-Spawner ohne targetname, die Ziele der Startposition beim ClientSpawn),
// und alles, was daraus folgt. skriptBeginn sagt, wann jedes Skript im Spiel
// zum ersten Mal startet (relativ zum Start der Karte). Was nur der Spieler
// ausloest (Trigger beim Betreten, ein Kampf), startet hier nicht.
[[nodiscard]] Ablauf simuliereKartenstart(const AblaufWelt& welt);

// rc603: die Skripte `pfade` (ICARUS-Pfade wie in den Entities) in der
// Reihenfolge ihres ersten Starts in `a` - bei gleicher Zeit nach der Folge
// der Starts; die nie gestarteten dahinter in ihrer Reihenfolge. Ergebnis:
// Indizes in `pfade`.
[[nodiscard]] std::vector<std::size_t> skripteNachStart(const std::vector<std::string>& pfade, const Ablauf& a);

// Wer fuehrt dieses Skript in der Karte aus? targetname der Entity, die
// es als usescript/spawnscript/... traegt (bei NPC-Spawnern der
// NPC_targetname). Leer, wenn keine es nennt.
// rc604 fix604: `schluessel` bekommt den Schluessel, unter dem sie es traegt
// ("usescript", "spawnscript", "deathscript" ...).
[[nodiscard]] std::string ablaufTraeger(const MapData& karte, const std::string& startPfad,
                                        bool* istFigur = nullptr, std::string* schluessel = nullptr);

}  // namespace bhed

#endif
