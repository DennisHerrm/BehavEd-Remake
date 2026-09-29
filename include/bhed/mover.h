// mover.h - Tueren, Plattformen und alles, was die Karte selbst bewegt.
//
// Nachgebaut nach OpenJK SP, code/game/g_mover.cpp (func_door, func_plat,
// func_wall, func_rotating, func_bobbing), g_usable.cpp (func_usable),
// g_target.cpp (target_relay, target_delay), g_fx.cpp (fx_runner) und
// bg_misc.cpp (EvaluateTrajectory).
//
// Was das Skript mit `move`/`rotate` bewegt, rechnet weiterhin die Szene
// (scene.cpp). Hier steht, was die KARTE tut: auf ein `use` hin, oder weil
// eine Figur in das Ausloesefeld einer Tuer tritt.
//
// Gemeldet: "kannst du nun nachpruefen ... auch map animationen wie tueren
// und so gehen". Vorher bewegte sich keine Tuer: `use` spielte nur Klaenge,
// ein fx_runner mit START_OFF/ONESHOT startete nie, und ein func_wall mit
// START_OFF (das Palpatine-Hologramm in md_am_sith) blieb unsichtbar.
//
// Dazu, was ein `use` sonst an der Karte bewirkt (gemeldet: Explosionen und
// zerbrechende Dinge sollen in der Zwischensequenz aussehen wie im Spiel):
//   func_breakable, misc_model_breakable  g_breakable.cpp, cg_effects.cpp
//   func_train + path_corner              g_mover.cpp:1850 ff.
//   target_print                          g_target.cpp:139, cg_text.cpp:658
//   target_speaker                        g_target.cpp:171
//   light mit targetname                  g_misc.cpp:165 (Lichtstile)
#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "bhed/bsp.h"
#include "bhed/bspgeo.h"

namespace bhed {

// Ein `use` aus dem Skript: wann, und welcher targetname.
struct MoverUse {
    double ms = 0.0;
    std::string target;
    // Die Kette ueber target_relay/target_delay ist schon aufgeloest (der
    // ICARUS-Nachbau, bhed/ablauf.h, liefert auch die Ziele einzeln). Dann
    // leitet MoverSim sie nicht noch einmal weiter - sonst schaltete eine
    // Tuer hinter einem Relay zweimal.
    //
    // Das gilt auch fuer die EIGENEN Ziele eines func_breakable und eines
    // misc_model_breakable: die feuert der Nachbau (ablauf.cpp), denn dort
    // kann ein Ziel ein Skript starten. Kommt ein use NICHT aufgeloest (Proben
    // ohne Nachbau), feuert MoverSim sie selbst.
    bool aufgeloest = false;
    // Wer benutzt hat (der "activator" der Engine), als targetname - leer
    // heisst unbekannt. Ein `use` im Skript benutzt mit der Entity, auf der
    // das Skript laeuft (Q3_Use -> G_UseTargets2(ent, ent, target)).
    //
    // Gebraucht fuer target_print: Use_Target_Print schickt den Text nur,
    // wenn activator->client gesetzt ist, und SV_GameSendServerCommand
    // (sv_game.cpp:111) laesst nur Client 0 durch - also nur den Spieler.
    std::string ausloeser;
};

// --- Was beim Zerbrechen entsteht ------------------------------------------
//
// func_breakable (funcBBrushDieGo, g_breakable.cpp:79) und
// misc_model_breakable (misc_model_breakable_die, g_breakable.cpp:489)
// rufen dieselben beiden Stellen im cgame auf:
//
//   CG_MiscModelExplosion (cg_effects.cpp:180) - Effekte je Material,
//       13/5/2/8/20 Stueck plus 7 je Groessenstufe, verteilt im Kasten
//   CG_Chunks (cg_effects.cpp:288) - Bruchstuecke als .md3, die unter der
//       Schwerkraft fliegen, abprallen und nach 1,3 bis 2,2 s verblassen,
//       dazu der Klang des Materials
//
// Der Zufall der Engine (Q_flrand, rand) wird hier aus der Entitynummer
// gewuerfelt: dasselbe Bild zeigt beim Zurueckspulen dasselbe.

// Ein Effekt aus CG_MiscModelExplosion (theFxScheduler.PlayEffect).
struct BruchEffekt {
    std::size_t entity = 0;
    std::string effekt;          // "chunks/metalexplode" - ohne effects/ und .efx
    double ms = 0.0;
    float ort[3]{};
    float richtung[3]{0.0F, 0.0F, 1.0F};   // weg von der Mitte des Kastens
};

// Ein Bruchstueck (LE_FRAGMENT aus CG_Chunks).
struct Truemmer {
    std::size_t entity = 0;
    std::string modell;          // "models/chunks/metal/metal2_1.md3" oder das _c1-Modell
    double abMs = 0.0;
    double bisMs = 0.0;          // endTime: 1300 + Zufall * 900 ms
    float radius = 1.0F;         // le->radius: skaliert alle drei Achsen
    float winkel[3]{};           // angles.trBase
    float drehung[3]{};          // angles.trDelta, Grad je Sekunde (TR_LINEAR)
    // Die Flugbahn in Stuecken: jedes ist ein TR_GRAVITY-Wurf ab abMs, das
    // naechste beginnt am Aufprall (CG_ReflectVelocity, cg_localents.cpp:190).
    struct Flug {
        double abMs = 0.0;
        float ort[3]{};
        float vel[3]{};
        bool liegt = false;      // TR_STATIONARY: liegengeblieben
    };
    std::vector<Flug> flug;
    double liegtAbMs = -1.0;     // ab hier dreht es sich nicht mehr
};

// Wo ein Bruchstueck zum Zeitpunkt ms ist (CG_AddFragment).
struct TruemmerStand {
    bool sichtbar = false;
    float ort[3]{};
    float winkel[3]{};
    float radius = 1.0F;
    float deckkraft = 1.0F;      // die letzten FRAG_FADE_TIME = 1000 ms blasst es aus
};
[[nodiscard]] TruemmerStand truemmerAt(const Truemmer& t, double ms);

// Ein Klang der Karte, der KEIN soundSet ist: der Bruch (Material, "noise",
// cargoexplode) und target_speaker.
struct KartenKlang {
    std::size_t entity = 0;
    std::string datei;           // "sound/weapons/explosions/cargoexplode.wav"
    double ms = 0.0;
    double bisMs = -1.0;         // Schleife: laeuft bis hier, -1 = bis zum Ende
    bool schleife = false;       // s.loopSound: an der Entity, bis zum Abschalten
    bool global = false;         // EV_GLOBAL_SOUND: ueberall in voller Lautstaerke
    // target_speaker "activator" (spawnflags 8): der Klang sitzt am
    // Ausloeser, nicht am Lautsprecher. `ort` ist dann nur der Ersatz.
    bool amAusloeser = false;
    std::string ausloeser;
    float ort[3]{};
};

// Ein Text von target_print: Use_Target_Print -> "cp" -> CG_CenterPrint
// (cg_text.cpp:658). CG_DrawCenterString zeigt ihn 3000 ms, die letzten
// FADE_TIME = 200 ms blendet er aus (CG_FadeColor, cg_drawtools.cpp:372);
// ein neuer Text ersetzt den alten. Waehrend einer Skriptkamera zeichnet
// CG_Draw2D ihn NICHT (in_camera: frueher Ausstieg, cg_draw.cpp:9451) -
// die Zeit laeuft trotzdem weiter.
struct Bildschirmtext {
    std::size_t entity = 0;
    double ms = 0.0;
    double bisMs = 0.0;
    std::string text;            // "message", \n schon als Zeilenumbruch
    bool gezeigt = true;         // false: der Ausloeser war nicht der Spieler
};
// Deckkraft 0..1 zum Zeitpunkt ms (0 ausserhalb).
[[nodiscard]] float bildschirmtextDeckkraft(const Bildschirmtext& b, double ms);

// Ein Lichtschalter: misc_dlight_use -> misc_lightstyle_set (g_misc.cpp).
// Die Engine schaltet KEIN dynamisches Licht, sondern schreibt den
// Lichtstil "style" um: an = "z" oder das Muster von "switch_style", aus =
// "a" oder das Muster von "style_off". Sichtbar wird das nur ueber die
// Lightmaps dieses Stils.
struct LichtSchaltung {
    std::size_t entity = 0;
    int stil = 0;                // "style": dieser Stil wird umgeschrieben
    double ms = 0.0;
    bool an = true;
    int musterVon = 0;           // 0: fest "z"/"a", sonst das Muster dieses Stils
};

// Ein Bruch, fuer Proben und Protokoll.
struct Bruch {
    std::size_t entity = 0;
    std::string klasse;
    double ms = 0.0;
    int material = 0;
    int groesse = 0;             // 0/1/2 wie in funcBBrushDieGo
    int stuecke = 0;             // num_chunks (schon mit "radius")
};

// Ein Start eines fx_runner, der auf ein `use` wartet (START_OFF/ONESHOT).
struct FxRunnerStart {
    std::size_t entity = 0;   // Nummer in MapData::entities
    double ms = 0.0;
    bool oneShot = false;     // ONESHOT: genau ein Durchgang
    double bisMs = -1.0;      // START_OFF: laeuft bis hier (-1 = offen)
};

// Ein Klang eines Movers (G_PlayDoorSound, G_PlayDoorLoopSound).
//
// Die Karte nennt am Mover ein "soundSet"; sound/sound.txt ordnet jedem
// bmodelSet fuenf Klaenge zu, davon zaehlen die ersten drei: BMS_START,
// BMS_MID (die Schleife waehrend der Fahrt) und BMS_END (g_mover.cpp:33).
struct MoverKlang {
    std::size_t entity = 0;
    int modell = 0;
    std::string soundSet;
    int stufe = 0;          // 0 BMS_START, 1 BMS_MID (Schleife), 2 BMS_END
    double ms = 0.0;
    double bisMs = 0.0;     // nur die Schleife: laeuft bis hier
};

// Die bmodelSets aus sound/sound.txt (AS_GetBModelSet, snd_ambient.cpp):
//     bmodelSet impdoor1
//     subWaves movers/doors door1start door1move door1stop ...
// Name (klein) -> Dateien "sound/<ordner>/<name>.wav"; "null" bleibt leer,
// denn die Engine findet dazu keinen Klang und spielt nichts.
[[nodiscard]] std::map<std::string, std::vector<std::string>>
leseBmodelSets(const std::string& text);

class MoverSim {
public:
    // Steht eine Figur zum Zeitpunkt ms im Kasten mins..maxs? Fuer die
    // Ausloesefelder der Tueren (Touch_DoorTrigger).
    using FigurImKasten =
        std::function<bool(double ms, const float mins[3], const float maxs[3])>;

    // Alles einmal durchrechnen, von 0 bis endeMs in Schritten von 50 ms -
    // dem Takt der Spiellogik (FRAMETIME).
    void baue(const MapData& map, const BspGeometry& geo,
              std::vector<MoverUse> uses, const FigurImKasten& figur,
              double endeMs);

    struct Stand {
        bool bekannt = false;     // ein Mover dieser Simulation
        bool sichtbar = true;
        float origin[3]{};        // currentOrigin der Engine
        float angles[3]{};        // currentAngles, absolut
    };
    // Der Stand des Brush-Modells `modell` (die Zahl aus "*N").
    [[nodiscard]] Stand at(int modell, double ms) const;

    [[nodiscard]] const std::vector<FxRunnerStart>& fxStarts() const { return fx_; }

    // Fuer Proben und Protokoll: wann welche Tuer aufging.
    struct Oeffnung {
        int modell = 0;
        double abMs = 0.0;    // beginnt zu oeffnen
        double offenMs = 0.0; // ganz offen
    };
    [[nodiscard]] const std::vector<Oeffnung>& oeffnungen() const { return auf_; }

    // --- Zerbrechen, Zuege, Texte, Lautsprecher, Licht -------------------
    //
    // Gibt es diese Datei in den Archiven? Vor baue() setzen; ohne gilt
    // "nein". Gebraucht fuer das Bruchstueckmodell "<modell>_c1.md3": die
    // Engine nimmt es nur, wenn cgs.model_draw dafuer etwas geladen hat
    // (CG_Chunks), sonst die Stuecke des Materials.
    std::function<bool(const std::string&)> dateiDa;

    [[nodiscard]] const std::vector<Bruch>& brueche() const { return bruch_; }
    [[nodiscard]] const std::vector<BruchEffekt>& bruchEffekte() const { return bruchFx_; }
    [[nodiscard]] const std::vector<Truemmer>& truemmer() const { return truemmer_; }
    // Klaenge beim Zerbrechen, nach Zeit geordnet.
    [[nodiscard]] const std::vector<KartenKlang>& bruchKlaenge() const { return bruchKlang_; }
    // target_speaker: Einmalklaenge und Schleifen (auch die, die schon beim
    // Start laufen - looped-on), nach Zeit geordnet. Nur Daten: das
    // Abspielen am Ort macht spaeter die Ansicht.
    [[nodiscard]] const std::vector<KartenKlang>& lautsprecher() const { return lautsprecher_; }
    [[nodiscard]] const std::vector<Bildschirmtext>& bildschirmtexte() const { return texte_; }
    // Der Text, den CG_DrawCenterString zum Zeitpunkt ms zeigt (ohne die
    // Frage nach der Skriptkamera), oder nullptr.
    [[nodiscard]] const Bildschirmtext* bildschirmtextAt(double ms) const;
    [[nodiscard]] const std::vector<LichtSchaltung>& lichtSchaltungen() const { return licht_; }

    // Welches .md3 zeigt ein misc_model_breakable zum Zeitpunkt ms?
    struct ModellStand {
        bool bekannt = false;     // false: nicht von dieser Simulation - wie gehabt zeichnen
        bool sichtbar = true;
        std::string modell;       // Pfad klein, "models/.../kiste_d1.md3"
        bool angehalten = false;  // nach dem Bruch: s.frame = 0, keine Animation
    };
    [[nodiscard]] ModellStand modellAt(std::size_t entity, double ms) const;
    // Alle .md3, die ueber die Karte hinaus gebraucht werden: Schadens- und
    // Benutzmodelle (_d1/_u1) und die Bruchstuecke. Zum Vorladen.
    [[nodiscard]] std::vector<std::string> nebenModelle() const;

    // Alle Klaenge der Mover mit soundSet bis endeMs, nach Zeit geordnet.
    [[nodiscard]] std::vector<MoverKlang> klaenge(double endeMs) const;
    // Wo der Klang eines Modells zu hoeren ist: die Mitte des Modells am
    // aktuellen Ort (CalcTeamDoorCenter).
    void klangOrt(int modell, double ms, float out[3]) const;

    // Die Bahn eines Movers: Stuecke von Ort zu Ort (TR_*_STOP) oder ruhend.
    struct Stueck {
        double abMs = 0.0;
        double dauerMs = 0.0;     // 0: ruhend auf `von`
        float von[3]{};
        float nach[3]{};
        bool linear = false;      // TR_LINEAR_STOP statt TR_NONLINEAR_STOP
    };
    struct Mover {
        int modell = 0;
        std::size_t entity = 0;
        std::string klasse;
        std::string soundSet;                     // leer: stumm
        float mitte[3]{};                         // Mitte des Modells, bei Ursprung 0
        std::vector<Stueck> bahn;                 // leer: steht auf origin0
        float origin0[3]{};
        float angles0[3]{};
        // func_rotating: Winkelgeschwindigkeit (Grad je Sekunde) je Achse
        float drehung[3]{};
        bool drehAnfang = false;                  // START_ON
        std::vector<double> drehUm;               // jedes use schaltet um
        // func_bobbing: TR_SINE
        float bobDelta[3]{};
        float bobBasis[3]{};                      // s.origin, ohne Phasenversatz
        double bobPeriodeMs = 0.0;                // 0: kein func_bobbing
        double bobPhase = 0.0;                    // "phase", Anteil der Periode
        bool bobAnfang = false;
        std::vector<double> bobUm;                // jedes use schaltet um
        // Sichtbarkeit: Umschaltzeiten (func_wall/func_usable START_OFF)
        bool sichtbarAnfang = true;
        std::vector<double> umschalten;
        // ... oder feste Zustaende ab einer Zeit (func_breakable: weg ab
        // dem Bruch; func_train: EF_NODRAW an einem INVISIBLE-path_corner).
        // Gibt es welche, gelten sie statt `umschalten`.
        std::vector<std::pair<double, bool>> sichtAb;
        // func_train: die Winkel in Stuecken (TURN_TRAIN/YAW_TRAIN, 2000 ms,
        // Reached_Train). von/nach sind hier Winkel. Leer: angles0.
        std::vector<Stueck> drehBahn;
    };
    [[nodiscard]] const std::vector<Mover>& mover() const { return mover_; }

private:
    std::vector<Mover> mover_;
    std::vector<int> nachModell_;   // Modell -> Index in mover_, -1
    std::vector<FxRunnerStart> fx_;
    std::vector<Oeffnung> auf_;
    std::vector<Bruch> bruch_;
    std::vector<BruchEffekt> bruchFx_;
    std::vector<Truemmer> truemmer_;
    std::vector<KartenKlang> bruchKlang_;
    std::vector<KartenKlang> lautsprecher_;
    std::vector<Bildschirmtext> texte_;
    std::vector<LichtSchaltung> licht_;
    // misc_model_breakable: Entity -> Anfangszustand und Wechsel (ms, Modell;
    // leer = unsichtbar). Das erste Element gilt ab -unendlich.
    struct ModellWechsel {
        double ms = 0.0;
        std::string modell;
        bool angehalten = false;
    };
    std::map<std::size_t, std::vector<ModellWechsel>> modellWechsel_;
};

// EvaluateTrajectory fuer TR_LINEAR_STOP und TR_NONLINEAR_STOP
// (bg_misc.cpp:501): nichtlinear ist sin(90 Grad * Anteil) - schnell los,
// weich aus. Fuer Proben offen.
[[nodiscard]] float moverAnteil(double seitMs, double dauerMs, bool linear);

}  // namespace bhed
