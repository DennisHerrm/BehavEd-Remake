// timeline.h - wann laeuft welcher Befehl?
//
// Ein ICARUS-Skript ist keine Liste, die von oben nach unten abgearbeitet
// wird. Es sind mehrere Ablaeufe, die nebeneinander laufen:
//
//   * Das Skript selbst laeuft auf der Entity, die es gestartet hat.
//   * Jedes `affect ( "name", ... )` uebergibt seinen Block dem EIGENEN
//     Ablauf von "name". Der laeuft ab diesem Augenblick PARALLEL weiter.
//     Belegt in CSequencer::Affect (Sequencer.cpp): der Block wandert in die
//     Warteschlange einer anderen Sequenz.
//   * `wait` haelt nur den Ablauf an, in dem es steht.
//
// Genau deshalb steht in Ravens eigenem Lehrtext der Satz, dass bei zwei
// wait-Anweisungen von 2000 und 5000 nicht sieben Sekunden vergehen, sondern
// fuenf - die eine laeuft im affect-Block, die andere daneben.
//
// Diese Datei rechnet daraus eine Zeitleiste: je Ablauf eine Spur, darin die
// Befehle mit ihrer Zeit in Millisekunden ab Skriptbeginn.
//
// Was NICHT bestimmbar ist, wird als Anmerkung vermerkt statt geraten:
// wie lange eine Animation dauert, wann ein Signal kommt, wie oft eine
// Schleife laeuft. Eine Zeitleiste, die so etwas erfindet, ist schlimmer als
// keine.
#ifndef BHED_TIMELINE_H
#define BHED_TIMELINE_H

#include "bhed/commands.h"
#include "bhed/script.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bhed {

struct TimelineEvent {
    double startMs = 0.0;
    // Ende bei allem, was Zeit braucht: camera MOVE ueber 3000 ms, ein
    // wait, ein move. Sonst gleich dem Anfang.
    double endMs = 0.0;
    Path path;                  // welcher Knoten im Baum
    // Stammt der Befehl aus einem ANDEREN Skript (run, use, spawnscript -
    // siehe bhed/ablauf.h), steht hier dessen Pfad, und `path` ist leer.
    std::string fremd;
    std::string label;          // wie er in der Leiste steht
    std::string sound;          // bei sound: der Dateiname, sonst leer
    std::string kanal;          // bei sound: der Kanal (CHAN_VOICE ...)
    // Bei use: der Name der angesprochenen Entity, sonst leer.
    //
    // NICHT in `sound` gelegt: dort steht ein Dateiname, hier ein
    // Entityname. Wer beides in ein Feld packt, hat spaeter eine Stelle,
    // die "mus1" abzuspielen versucht.
    std::string target;

    enum class Kind : std::uint8_t {
        Camera, Sound, Wait, Set, Move, Other,
        // use ( "name" ) - spricht eine Entity in der Karte an.
        //
        // Gemeldet: "mir fehlen noch Sounds, Lichtschwerter und Musik."
        //
        // Die Musik steht NICHT im Skript und auch nicht im worldspawn,
        // sondern in target_play_music-Entities der Karte, die per use
        // angesprochen werden (g_target.cpp:1225 f.):
        //
        //     void target_play_music_use(...)
        //     {
        //         gi.SetConfigstring(CS_MUSIC, self->message);
        //     }
        //
        // In md_ga_jedi liegen sieben davon - mus1 bis mus5, mus_boss und
        // mus_bossintro -, dazu zwei target_speaker (hammer_sound,
        // march1). Ohne `use` blieb all das stumm.
        Use,
        // SET_SABERACTIVE - die Klinge geht an oder aus.
        //
        // Ein eigenes Ereignis, weil der KLANG nicht im Skript steht:
        // Q3_SetSaberActive (Q3_Interface.cpp:6393) ruft nur
        // ps.SaberActivate(). Gespielt wird saber[0].soundOn im Zeichner
        // (cg_players.cpp:8219), und der Dateiname steht am Ende einer
        // Kette: NPC_type -> .npc `saber` -> .sab `soundOn`.
        //
        // Die Kette kennt nur die Oberflaeche, also traegt das Ereignis
        // hier nur, WER und OB - aufgeloest wird beim Abspielen.
        Saber,
    };
    // Bei Saber: an oder aus.
    bool on = false;
    Kind kind = Kind::Other;

    [[nodiscard]] double durationMs() const { return endMs - startMs; }
};

struct TimelineTrack {
    // Leer heisst: das Skript selbst. Sonst der Name aus dem affect.
    std::string entity;
    double startMs = 0.0;       // wann dieser Ablauf beginnt
    std::vector<TimelineEvent> events;
};

struct Timeline {
    std::vector<TimelineTrack> tracks;
    double durationMs = 0.0;
    // Wo die Sequenz PRAKTISCH endet.
    //
    // Skripte parken sich am Ende gern mit einem sehr langen wait, damit
    // nichts weiterlaeuft - in cin2_jedi.txt steht hinter dem
    // camera ( DISABLE ) ein wait ( 999999 ). Die Zwischensequenz ist da
    // laengst vorbei; ohne diese Unterscheidung quetscht sich alles
    // Sehenswerte in ein halbes Prozent des Balkens.
    double practicalEndMs = 0.0;
    // Was sich nicht ausrechnen liess, im Klartext und mit Ort.
    std::vector<Diag> notes;
};

struct TimelineOptions {
    // Ein wait mit random(a,b) hat keinen festen Wert. Die Engine wuerfelt
    // einmal und behaelt ihn; wir nehmen die Mitte und sagen es dazu.
    bool randomAsMiddle = true;
    // Wie lange ein waitsignal ohne passendes signal dauert. Nur damit die
    // Leiste weiterlaeuft; es steht als Anmerkung dabei.
    double unknownWaitMs = 1000.0;
    // Schleifen mit fester Zahl bis hierhin ausrollen, danach abbrechen.
    int maxLoopExpand = 8;
    // Ab dieser Dauer gilt ein wait als "Skript geparkt", nicht als Pause.
    // Eine Minute ist grosszuegig: die laengste echte Pause in Ravens 1011
    // Skripten liegt weit darunter, die Parkzeiten bei 999999.
    double parkingWaitMs = 60000.0;
};

[[nodiscard]] Timeline buildTimeline(const Script& s, const CommandDb& db,
                                     const TimelineOptions& opt = {});

}  // namespace bhed
#endif
