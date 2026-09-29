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
#include "bhed/script.h"

#include <functional>
#include <map>
#include <string>
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
    // (<= 0: die Vorgaben der Engine, 90 und 300).
    std::function<float(const std::string& figur, bool gehen)> tempo;
    // Wie lange eine ROFF-Bahn laeuft, bis G_Roff ihre Aufgabe erledigt
    // (roffLaufzeitMs aus bhed/roff.h), fuer den Namen aus
    // play ( "PLAY_ROFF", "<name>" ). Negativ: unbekannt - dann gilt play
    // als sofort erledigt. kRoffFehlt: die Datei gibt es nicht - dann
    // bleibt die Aufgabe offen, wie im Spiel.
    std::function<double(const std::string& name)> roffDauer;
    // Takt der Spiellogik. 0 schaltet ihn ab (nur fuer Proben).
    double bildMs = 50.0;
    // Spaetestens hier ist Schluss - loop ( -1 ) laeuft sonst ewig.
    double grenzeMs = 600000.0;
    // Nach dem Ende des Startskripts noch so lange weiterlaufen lassen
    // (Figuren, die noch laufen; Klaenge, die noch klingen).
    double nachlaufMs = 3000.0;
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
    // Wer das Startskript ausfuehrt (targetname), leer = ein
    // target_scriptrunner ohne Namen oder unbekannt.
    std::string traeger;
    bool traegerIstFigur = false;

    Script flach;
    std::map<Path, AblaufHerkunft> herkunft;

    // Figuren, die erst waehrend des Ablaufs entstehen (NPC_spawner per
    // use): Name -> ab wann es sie gibt. Vorher sind sie nicht zu sehen.
    std::map<std::string, double> erscheint;

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
};

// Das Startskript `start` (dessen Pfad `startPfad` ist, z.B.
// "md_ga/grievous_jedi"; leer, wenn unbekannt) ausfuehren.
[[nodiscard]] Ablauf simuliereAblauf(const Script& start, const std::string& startPfad,
                                     const AblaufWelt& welt);

// Wer fuehrt dieses Skript in der Karte aus? targetname der Entity, die
// es als usescript/spawnscript/... traegt (bei NPC-Spawnern der
// NPC_targetname). Leer, wenn keine es nennt.
[[nodiscard]] std::string ablaufTraeger(const MapData& karte, const std::string& startPfad,
                                        bool* istFigur = nullptr);

}  // namespace bhed

#endif
