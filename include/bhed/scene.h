// scene.h - wo jede Figur steht und was sie tut
//
// timeline.h sagt, WANN welcher Befehl laeuft. camtrack.h macht daraus die
// Kamerabahn. Diese Datei macht dasselbe fuer die FIGUREN: Ort, Blickrichtung
// und Animation zu jeder Zeit.
//
// Woher die Angaben kommen:
//
//   Startort      NPC_spawner in der Karte oder der .ent-Datei
//   Ortswechsel   set ( SET_ORIGIN, < x y z > )      - ein Sprung
//   Gehen         set ( SET_NAVGOAL, "name" )        - laeuft dorthin
//   Tempo         set ( SET_WALKING, true/false )
//   Animation     set ( SET_ANIM_BOTH, "BOTH_..." )  - wenn angegeben
//
// Die Geschwindigkeiten stehen in NPC_stats.cpp der Engine:
//     walkSpeed = 90, runSpeed = 300   (Einheiten je Sekunde)
//
// WAS DAS HIER NICHT KANN, und das ist wichtig:
//
//   * Keine Wegfindung. Die Engine schickt einen NPC ueber ein Netz aus
//     Wegpunkten um Hindernisse herum; hier geht es geradeaus zum Ziel.
//     Bei Zwischensequenzen stimmt das meistens, weil die Wegpunkte dicht
//     genug stehen - aber es ist eine Naeherung, keine Nachbildung.
//   * Keine Kollision. Eine Figur laeuft durch eine Wand, wenn der Wegpunkt
//     dahinterliegt.
//   * Die Animation beim Gehen waehlt im Spiel die Engine nach dem
//     Bewegungszustand. Hier wird sie aus SET_WALKING abgeleitet:
//     BOTH_WALK1 oder BOTH_RUN1, im Stehen BOTH_STAND1.
//
// Fuer das Abstimmen von Kamerafahrten gegen Sprache reicht das - man sieht,
// wo die Figuren stehen und wann sie sich bewegen. Fuer eine Vorschau, die
// dem Spiel bis auf den Zentimeter gleicht, reicht es nicht.
#ifndef BHED_SCENE_H
#define BHED_SCENE_H

#include "bhed/bsp.h"
#include "bhed/roff.h"
#include "bhed/script.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bhed {

// Wie weit ein Kopf sich drehen kann, in Grad - und zwar ASYMMETRISCH.
//
// Gemeldet: "wenn ich Kopf und Oberkoerper aus mache, sieht es richtig aus.
// Nicht immer dreht sich der Oberkoerper mit dem Kopf mit."
//
// Beide Beobachtungen treffen zu, und beide haben denselben Grund: rc254
// hatte den FALSCHEN Weg der Engine nachgebaut.
//
// Es gibt zwei. `CG_PlayerAngles` (cg_players.cpp:3030) ist der alte Weg
// fuer MD3-Modelle - dort gibt es die Oberkoerperdrehung mit
// LOOK_SWING_SCALE, und dort habe ich sie abgeschrieben. Fuer GHOUL2-
// Modelle laeuft aber `CG_G2PlayerAngles` (ebenda:2479), und das ist ein
// eigener Zweig, der mit `return` endet, BEVOR jener Block je erreicht
// wird (ebenda:2881).
//
// Jedes Modell in Movie Duels ist ein .glm. Es gilt also der Ghoul2-Weg,
// und der dreht den Oberkoerper GAR NICHT dem Blick nach - nur die
// Brustwirbel bekommen ein Zehntel ab.
//
// Die Anschlaege stehen dort als nackte Zahlen (ebenda:2878):
//
//     vec3_t headClampMinAngles = {-25,-55,-10},
//            headClampMaxAngles = { 50, 50, 10};
//
// Also -55 bis +50 Grad im Gierwinkel, nicht symmetrisch. Davor klemmt
// CG_UpdateLookAngles noch auf +-70 (ebenda:2866), was hier nie bindet.
inline constexpr float kHeadYawClampMin = -55.0F;
inline constexpr float kHeadYawClampMax = 50.0F;

// Wie schnell sich eine Figur zu ihrem Watchtarget dreht, in Grad je
// Sekunde.
//
// Hergeleitet aus NPC_UpdateAngles (code/game/NPC_utils.cpp:288 ff.). Die
// Engine dreht nicht auf der Stelle und auch nicht exponentiell, sondern
// mit FESTEM Winkelschritt je Serverbild:
//
//     decay  = 60.0 + yawSpeed * 3;
//     decay *= 50.0f / 1000.0f;      // je 50-ms-Bild
//     error -= decay;                // gegen null, ohne zu ueberschiessen
//     viewangles[YAW] = targetYaw + error;
//
// Ein Serverbild sind 50 ms, also zwanzig je Sekunde. Der Faktor 50/1000
// und die zwanzig heben einander auf, und uebrig bleibt schlicht
//
//     Grad je Sekunde = 60 + yawSpeed * 3
//
// yawSpeed steht in den Werten des NPC; die Vorgabe ist 90
// (NPC_stats.cpp:1995), also 60 + 270 = 330. Eine Kehrtwende um 180 Grad
// dauert damit knapp 0,55 s - sichtbar, aber zuegig.
//
// SET_YAWSPEED setzt einen anderen yawSpeed; turnRateFor() rechnet ihn mit
// derselben Formel um. Ohne SET_YAWSPEED gilt diese Vorgabe.
inline constexpr float kNpcTurnDegPerSec = 60.0F + 90.0F * 3.0F;

// Wie lange braucht eine Figur fuer `dist` Einheiten zu einer Marke, mit
// Tempo `v` (Einheiten je Sekunde), Richtung (ux, uy) und Zielradius?
//
// Die Engine laesst NPCs nicht einfach mit v geradeaus gehen. STEER::GoTo
// (g_navigator.cpp:4399) verfolgt das Ziel mit Seek und einer
// Abbremszone von 4 x Radius: darin ist die Wunschgeschwindigkeit
// v * abstand / zone (ebenda:4666). Angekommen ist die Figur, sobald sie im
// Radius ist ODER der Zielpunkt in ihrem Kasten liegt (STEER::Reached,
// :5361) - bei +-15 in x und y also schon deutlich vor dem Punkt.
//
// Das Modell allein ergibt kuerzere Zeiten, als das Spiel braucht:
// Beschleunigung und Reibung in pmove und die Denktakte kommen dazu.
// Anlauf 300 ms und Faktor 1.5 sind GEMESSEN (28.09., md_dd_jedi, eigene
// Probe mit Tempo 55, 100, 200 ueber 40/80/150 Einheiten plus obi1 aus der
// Intro, g_ICARUSDebug-Zeiten). Eine Naeherung - aber naeher am Spiel als
// d / v, das im Mittel um den Faktor 1.8 zu frueh ankam.
[[nodiscard]] double gehDauerMs(double dist, double v, double ux, double uy, double zielRadius = 12.0);

// Dieselbe Rechnung fuer einen abweichenden yawSpeed, wie SET_YAWSPEED ihn
// setzt (Q3_Interface.cpp:3438 schreibt den Wert ungeprueft in die Werte
// des NPC).
[[nodiscard]] constexpr float turnRateFor(float yawSpeed) noexcept {
    const float rate = 60.0F + yawSpeed * 3.0F;
    // Ein negativer oder null-Wert wuerde die Figur nie ankommen lassen.
    // Die Engine kennt diesen Fall nicht - dort steht dann einfach nie
    // etwas an -, und genau das tun wir auch: der Rest bleibt stehen.
    return (rate > 0.0F) ? rate : 0.0F;
}

// --- Waffe und Lichtschwerter einer Figur ------------------------------
//
// Gemeldet: jede Zwischensequenz soll im Editor aussehen wie im Spiel -
// und dazu gehoert, was die Figuren in der Hand halten.
//
// Die Engine fuehrt das an ZWEI Stellen, und beide werden hier getrennt
// nachgebildet, weil sie auseinanderlaufen koennen:
//
//   ps.weapon / ps.saber[2]   der ZUSTAND (welche Waffe, welche Klingen an)
//   ent->weaponModel[2]       was wirklich in der Hand HAENGT (Ghoul2)
//
// Ein Beispiel, wo beides auseinanderfaellt, steht im Quelltext selbst:
// SET_WEAPON WP_NONE entfernt die Modelle (G_RemoveWeaponModels,
// g_client.cpp:2317), laesst aber ps.weapons[WP_SABER] stehen. Ein
// spaeteres SET_SABERACTIVE schaltet dann auf WP_SABER zurueck
// (Q3_SetSaberActive, Q3_Interface.cpp:6862 ff.), OHNE den Griff wieder
// anzuhaengen - und CG_AddSaberBladeGo zeichnet ohne Griffmodell keine
// Klinge (cg_players.cpp:13765: "modelIndex == -1 ... return").

// MAX_BLADES, q_shared.h - so viele Klingen kann ein Griff tragen.
inline constexpr int kMaxKlingen = 8;

// Eine Klinge. Die Laenge wird nicht gespeichert, sondern aus dem letzten
// Wechsel berechnet (klingenLaenge): die Engine laesst sie mit fester Rate
// wachsen und schrumpfen, also genuegt der Stand beim letzten Umschalten.
struct KlingenZustand {
    bool an = false;           // blade[i].active
    double seitMs = -1.0e9;    // letzter Wechsel oder Neustart der Laenge
    float laengeDann = 0.0F;   // Laenge zu seitMs
    float max = 32.0F;         // lengthMax (wp_saberLoad.cpp:630: 32)
    float radius = 3.0F;       // SABER_RADIUS_STANDARD (wp_saber.h:57)
    // Aufgeloeste Farbe, klein geschrieben: "red", "blue", "xff3f00" ...
    // NIE "random" - das wird beim Aufbau festgelegt (saberFarbeAufloesen).
    std::string farbe;
};

// Ein Schwert (ps.saber[n]).
struct SaberZustand {
    // Index in Actor::saberArten. -1 heisst: die NULLSTRUKTUR, die ein NPC
    // ohne `saber`-Schluessel hat - gi.Malloc(..., qtrue) loescht den
    // Client (NPC_spawn.cpp:2422), also numBlades 0 und kein Modell.
    int art = -1;
    int numBlades = 0;
    std::array<KlingenZustand, kMaxKlingen> klinge{};
};

// Was an einem Handbolzen haengt (ent->weaponModel[n]).
struct HandBelegung {
    int modell = -1;          // Index in Actor::modelle, -1 heisst: nichts
    bool istSaber = false;    // der Griff von saber[n] - dann haengen Klingen daran
};

// Ein Modell aus SET_ADDLHANDBOLT_MODEL / SET_ADDRHANDBOLT_MODEL.
struct KinoModell {
    int modell = -1;          // Index in Actor::modelle, -1 heisst: frei
    bool links = false;       // handLBolt statt handRBolt
};
// So viele gleichzeitig angehaengte Kinomodelle werden gefuehrt. Die Engine
// kennt keine Grenze (siehe Kind::HandModell in Actor::at), die Skripte der
// Mod haengen nie mehr als eines auf einmal an.
inline constexpr int kMaxKinoModelle = 4;

struct ActorState {
    float pos[3]{};
    float angles[3]{};
    std::string animation;   // leer heisst: keine bekannt

    // --- Der OBERKOERPER hat seine eigene Animation ----------------------
    //
    // Gemeldet: "ich denke manche Charaktere benutzen die falschen
    // Animationen."
    //
    // Bis rc251 landeten SET_ANIM_BOTH, SET_ANIM_UPPER und SET_ANIM_LOWER
    // alle im selben Feld - der letzte gewann fuer den ganzen Koerper. In
    // der Engine sind es ZWEI Knochen (bg_panimate.cpp:4848, 4864;
    // g_client.cpp:1353, 1544):
    //
    //     SETANIM_LEGS  -> rootBone        = "model_root"    (das Skelett)
    //     SETANIM_TORSO -> lowerLumbarBone = "lower_lumbar"  (ab der Taille)
    //
    // Ein UPPER laesst die Beine also in Ruhe. In den 40 Skripten der
    // Mission stehen 50 Bloecke mit SET_ANIM_UPPER, darunter reine
    // Oberkoerperanimationen wie TORSO_WEAPONIDLE2 - die bekamen bisher
    // auch die Beine.
    //
    // Leer heisst: der Oberkoerper folgt `animation`. Ein SET_ANIM_BOTH
    // setzt beide und LEERT dieses Feld wieder - danach gibt es keine
    // getrennte Oberkoerperspur mehr, genau wie in der Engine, wo BOTH
    // beide Knochen neu belegt.
    std::string torsoAnimation;
    double torsoStartMs = 0.0;
    bool torsoHold = false;
    bool visible = true;
    // Endgueltig fort (remove / kill), nicht nur unsichtbar. Ein
    // spaeteres SET_INVISIBLE "false" holt sie nicht zurueck.
    bool removed = false;
    // Die Klinge brennt.
    //
    // Aus SET_SABERACTIVE. Der KLANG dazu haengt an der Zeitleiste (siehe
    // TimelineEvent::Kind::Saber) - hier steht der Zustand, den die
    // Ansicht braucht: eine Klinge zeichnet man nur, solange sie an ist.
    bool saberActive = false;
    // Wann die Klinge zuletzt an- oder ausgeschaltet wurde.
    //
    // Gemeldet: "die Animationsgeschwindigkeit, wie schnell das Saber
    // gezogen und gerendert wird - es wird naemlich momentan instant
    // gerendert."
    //
    // Stimmt: behaved sprang von 0 auf volle Laenge. Die Engine laesst sie
    // WACHSEN (cg_players.cpp:7386):
    //
    //     length += lengthMax/10 * cg.frametime/100;
    //
    // Das sind lengthMax/1000 je Millisekunde - also genau EINE SEKUNDE
    // von null bis voll, unabhaengig von der Laenge der Klinge. Beim
    // Ausschalten dasselbe mit `-=` (ebenda:7336).
    double saberSinceMs = -1.0e9;

    // --- Waffe und Schwerter, wie die Engine sie fuehrt ------------------
    //
    // saberActive oben bleibt die Kurzfassung: "irgendeine Klinge ist an"
    // (ps.SaberActive()). Die Einzelheiten stehen hier.
    //
    // ps.weapon, z.B. "WP_BLASTER". LEER heisst: unbekannt - keine .npc
    // zur Figur. Dann gilt das alte Verhalten: Schwertbefehle wirken, als
    // halte die Figur ein Schwert, und gezeichnet wird keine Waffe.
    std::string waffe;
    // ps.weapons[WP_SABER]. SET_WEAPON WP_NONE laesst es stehen
    // (G_SetWeapon, Q3_Interface.cpp:3427 ff. raeumt die Liste nicht).
    bool hatSaber = false;
    // ps.dualSabers - saber[1] zaehlt nur, wenn das gesetzt ist.
    bool dualSabers = false;
    std::array<SaberZustand, 2> saber{};
    // [0] rechte Hand (handRBolt), [1] linke (handLBolt).
    std::array<HandBelegung, 2> hand{};
    std::array<KinoModell, kMaxKinoModelle> kino{};
    int kinoLetztes = -1;     // ent->cinematicModel: das zuletzt angehaengte
    // Der Summton, der gerade laeuft - leer heisst keiner. Fuer die
    // Tonseite: sie soll ihn als Schleife an der Figur spielen.
    //
    // CG_StopWeaponSounds (cg_players.cpp:7101 ff.): EIN Ton je Figur,
    // immer der soundLoop von saber[0], solange ps.weapon WP_SABER ist und
    // irgendeine Klinge an ist - unabhaengig davon, wie lang sie gerade ist.
    std::string saberLoop;

    // --- Schiesst gerade ---------------------------------------------------
    //
    // Gemeldet: "es fehlt noch die Schussanimation der Super Battle
    // Droids."
    //
    // Sie steht nicht als Animation im Skript. Dort steht nur
    // SET_FIRE_WEAPON, und der Rest passiert in der Engine:
    // Q3_SetFireWeapon (Q3_Interface.cpp:4523) setzt SCF_FIRE_WEAPON,
    // NPC_BSDefault (AI_Default.cpp:742) ruft daraufhin je Bild
    // WeaponThink(), das ueber ShootThink() BUTTON_ATTACK setzt - und ERST
    // der Bewegungsschritt waehlt die Animation (bg_pmove.cpp:13810 ff.):
    //
    //     PM_SetAnim( pm, SETANIM_TORSO, BOTH_ATTACK3,
    //                 SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD
    //                 |SETANIM_FLAG_RESTART );
    //
    // Vier Glieder zwischen Skriptzeile und Bild. Deshalb war nichts zu
    // sehen: behaved kannte nur das erste.
    bool firing = false;
    // Wann das Feuern begann. DER Bezugspunkt fuer den Takt.
    //
    // Beim Nachrechnen eines Protokolls aufgefallen: der Takt hing vorher
    // an animStartMs, dem Beginn der ANIMATION. In md_ga_jedi steht der
    // bei 0, das Feuern beginnt aber bei 47400 - die Schuesse lagen damit
    // auf Vielfachen von 750 seit dem Skriptanfang statt seit dem ersten
    // Schuss. Sichtbar wird das als bis zu einen Takt versetzter erster
    // Schuss.
    //
    // In der Engine zaehlt NPCInfo->shotTime, und das faengt an, wenn das
    // Feuern anfaengt (NPC_combat.cpp:1220).
    double firingSinceMs = 0.0;
    // SET_SHOT_SPACING, in Millisekunden. Der Abstand zwischen zwei
    // Schuessen - und damit, wie oft die Animation neu anfaengt
    // (SETANIM_FLAG_RESTART). Vorgabe: einmal je Sekunde.
    float shotSpacingMs = 1000.0F;
    bool moving = false;
    // Mit welchem Faktor die Animation laeuft. 1 heisst: wie in der .cfg.
    // Siehe kAnimSpeedWalk oben - ohne das rutschen die Fuesse.
    float animSpeed = 1.0F;
    bool crouched = false;

    // Die Animation haelt, statt zu wiederholen.
    //
    // set ( SET_ANIM_HOLDTIME_BOTH, -1 ) heisst: einfrieren. In Ravens 1510
    // Skripten steht das 510 von 721 Mal, also bei 71 % - ohne diese
    // Unterscheidung laufen Gesten und Posen in Endlosschleife, wo sie
    // stehenbleiben sollten.
    bool holdAnim = false;
    // Ab wann die Animation laeuft. Ohne diesen Bezug beginnt jede
    // Animation bei Null der Zeitleiste statt bei ihrem Einsatz.
    double animStartMs = 0.0;

    // --- Der Uebergang zur vorigen Animation -----------------------------
    //
    // Gemeldet: "es gibt manchmal keine Transition sondern es springt
    // einfach von Animation zu Animation."
    //
    // Im Quelltext nachgesehen, und es steht anders da als erwartet.
    // Ghoul2 mischt NICHT zwei laufende Animationen. Beim Umschalten wird
    // die alte Stellung EINGEFROREN - ghoul2_shared.h:97 sagt es
    // ausdruecklich: geblendet wird "zur und von der zuletzt gespielten
    // Frame desselben Knochens", Frame in der Einzahl. Der eingefrorene
    // Stand bewegt sich waehrend des Uebergangs nicht mehr.
    //
    // Deshalb genuegen vier Angaben statt eines zweiten Abspielers:
    //
    //   prevAnimation    welche Animation vorher lief
    //   prevAnimSinceMs  wie weit sie beim Umschalten war - EINGEFROREN
    //   blendStartMs     wann umgeschaltet wurde
    //   blendMs          wie lange der Uebergang dauert
    //
    // prevAnimation leer heisst: kein Uebergang.
    std::string prevAnimation;
    double prevAnimSinceMs = 0.0;
    bool prevHoldAnim = false;
    double blendStartMs = 0.0;
    double blendMs = 0.0;

    // --- Der Uebergang des OBERKOERPERS, fuer sich -----------------------
    //
    // In der Engine sind Beine und Oberkoerper zwei Knochenanimationen
    // ("model_root" und "lower_lumbar"), und JEDE blendet fuer sich von
    // ihrem letzten Bild (PM_SetAnimFinal setzt BONE_ANIM_BLEND je Knochen).
    // Bis hierher gab es nur EINEN Uebergang fuer die ganze Figur; ein
    // SET_ANIM_UPPER sprang deshalb hart, und beim Uebergang der Beine
    // blieb der Oberkoerper auf seinem neuen Bild stehen.
    //
    // Dieselben vier Angaben wie oben. Leer heisst: kein eigener Uebergang.
    std::string torsoPrevAnimation;
    double torsoPrevSinceMs = 0.0;
    double torsoBlendStartMs = 0.0;
    double torsoBlendMs = 0.0;

    // Wohin die Figur schaut, wenn ein SET_LOOK_TARGET gesetzt ist.
    bool hasLookTarget = false;
    float lookAt[3]{};

    // Wohin die Figur sich DREHT, wenn ein SET_WATCHTARGET gesetzt ist.
    //
    // watchFromYaw und watchStartMs halten fest, wo sie stand, als das Ziel
    // gesetzt wurde: die Engine dreht nicht auf der Stelle, sondern mit
    // fester Winkelgeschwindigkeit (siehe kNpcTurnDegPerSec).
    bool hasWatchTarget = false;
    float watchAt[3]{};
    float watchFromYaw = 0.0F;
    double watchStartMs = 0.0;
    // Die geltende Drehrate in Grad je Sekunde. Vorgabe, bis ein
    // SET_YAWSPEED etwas anderes sagt.
    float turnDegPerSec = kNpcTurnDegPerSec;
};

// Welchen Koerperteil ein Animations- oder Haltebefehl trifft.
//
// Die Engine kennt die drei als SETANIM_TORSO, SETANIM_LEGS und ihre
// Vereinigung SETANIM_BOTH (bg_public.h:166 ff.). Sie landen auf
// verschiedenen Knochen, siehe torsoAnimation in ActorState.
enum class AnimPart : std::uint8_t { Both, Upper, Lower };

// Ein Schritt in der Bahn einer Figur.
struct ActorStep {
    enum class Kind : std::uint8_t {
        Teleport, WalkTo, Face, Anim, Show, Hide, Hold, Look, Crouch,
        // SET_WATCHTARGET - die Figur DREHT SICH zu einer Entity.
        //
        // Gemeldet: "die Rotationen fehlen von den Charakteren, wenn
        // Charaktere sich drehen und sich anschauen."
        //
        // Nicht dasselbe wie Look. SET_LOOK_TARGET dreht in der Engine nur
        // den KOPF und ein Stueck des Oberkoerpers (cg_players.cpp:3319 ff.,
        // CG_UpdateAngleClamp mit Anschlaegen). SET_WATCHTARGET dreht den
        // ganzen Koerper - NPC_behavior.cpp:256 ff.:
        //
        //     vectoangles( viewvec, viewangles );
        //     NPCInfo->lockedDesiredYaw = NPCInfo->desiredYaw
        //                               = viewangles[YAW];
        //
        // mit dem Vermerk daneben: "NOTE: this will override any angles set
        // by NPC_MoveToGoal". Deshalb gewinnt Watch hier gegen die
        // Laufrichtung.
        //
        // In den 24 Skripten von md_ga_jedi kommt SET_WATCHTARGET 39 mal
        // vor - haeufiger als SET_LOOK_TARGET (31 mal).
        Watch,
        // SET_YAWSPEED - wie schnell sich die Figur dreht.
        //
        // Eigene Schrittart statt eines Feldes im Watch-Schritt: yawSpeed
        // gehoert in der Engine zu den Werten des NPC, nicht zum Blickziel.
        // Es kann vor, zwischen oder nach mehreren SET_WATCHTARGET stehen
        // und gilt fuer alle folgenden.
        //
        // value traegt die fertige Drehrate in Grad je Sekunde, nicht den
        // rohen yawSpeed - umgerechnet mit turnRateFor(), damit die Formel
        // an EINER Stelle steht.
        TurnRate,
        // remove ( "name" ) und kill ( "name" ) - die Entity ist FORT.
        //
        // Nicht dasselbe wie Hide. SET_INVISIBLE laesst sich mit einem
        // SET_INVISIBLE "false" zuruecknehmen; remove nicht - die Engine
        // gibt die Entity frei (CQuake3GameInterface::Remove,
        // Q3_Interface.cpp:9712, ruft Q3_RemoveEnt fuer JEDE Entity dieses
        // targetname). Danach gibt es nichts mehr, was ein spaeterer Befehl
        // wieder sichtbar machen koennte.
        //
        // Deshalb eine eigene Art und kein Hide: ein Hide, auf das im Skript
        // ein SET_INVISIBLE "false" folgt, waere sonst wieder da.
        Gone,
        // SET_SABERACTIVE - die Klinge an oder aus. value 1 heisst an.
        Saber,
        // SET_FIRE_WEAPON - value 1 heisst an.
        Fire,
        // SET_SHOT_SPACING - value ist der Abstand in Millisekunden.
        ShotSpacing,
        // move ( <ziel>, <winkel>, <dauer> ) und rotate ( <winkel>, <dauer> )
        //
        // Die beiden fehlten, und sie sind nicht selten: in den 64
        // Skripten aus MD_Maps_Ep1/2/4/6 kommt move 85 mal vor und rotate
        // 86 mal - zusammen oefter als alle set-Befehle. Damit standen
        // Tueren, Plattformen und die fliegenden Schiffe still.
        Glide,   // move: Ort UND Winkel ueber eine Dauer
        Turn,    // rotate: nur der Winkel
        // play ( "PLAY_ROFF", "<datei>" ) - die Entity folgt einer
        // aufgezeichneten Bahn (bhed/roff.h). Ort UND Winkel, ab dem Stand
        // beim Befehl (Q3_Interface.cpp:10704: pos1/pos2 = currentOrigin/
        // currentAngles). text traegt den Namen aus dem Skript.
        Roff,
        // SET_WEAPON - text ist der Waffenname (WP_...) oder "drop",
        // index das Weltmodell in Actor::modelle (-1: keines).
        // Q3_SetWeapon / G_SetWeapon, Q3_Interface.cpp:3410 ff.
        Waffe,
        // SET_SABER1 / SET_SABER2 - slot 0/1, text der .sab-Name oder
        // "none"/"remove", index die Art in Actor::saberArten.
        // WP_SetSaber, wp_saberLoad.cpp:3075 ff.
        SaberWahl,
        // SET_SABER1_COLOR1/2, SET_SABER2_COLOR1/2 - slot, index die Klinge
        // (0 oder 1), text die aufgeloeste Farbe. WP_SaberSetColor,
        // wp_saberLoad.cpp:3141.
        SaberFarbe,
        // SET_SABER1BLADEON/OFF, SET_SABER2BLADEON/OFF - slot, index die
        // Klinge, value 1 an / 0 aus. Q3_SetSaberBladeActive,
        // Q3_Interface.cpp:6912 ff.
        KlingeSchalter,
        // SET_ADDRHANDBOLT_MODEL / SET_ADDLHANDBOLT_MODEL (value 1, slot 0
        // rechts / 1 links, index das Modell) und SET_REMOVE*HANDBOLT_MODEL
        // (value 0). Q3_AddRHandModel & Co., Q3_Interface.cpp:6321 ff.
        HandModell,
    };

    Kind kind = Kind::Teleport;
    double startMs = 0.0;
    double endMs = 0.0;
    float from[3]{};
    float to[3]{};
    std::string text;        // Animationsname oder Wegpunktname
    float value = 0.0F;      // Haltezeit in ms, oder 1/0 fuer an/aus
    // Bei Glide und Turn: Winkel von und nach. Getrennt von from/to, weil
    // move BEIDES auf einmal aendert.
    float fromAng[3]{};
    float toAng[3]{};
    // Glide: nur wenn `move` Winkel angibt, aendert es die Winkel. Die
    // Engine laesst apos sonst in Ruhe (Q3_Lerp2Pos, Q3_Interface.cpp:8098
    // "if ( angles != NULL )").
    bool mitWinkel = true;
    // Glide: TR_LINEAR_STOP statt TR_NONLINEAR_STOP - der Kartenschluessel
    // "linear" (alt_fire, g_mover.cpp:648).
    bool linear = false;
    // Bei Anim und Hold: welcher Koerperteil gemeint ist.
    //
    // Ein eigenes Feld und kein Zahlenwert in `value`: dort steht bei Hold
    // schon die Haltezeit, und zwei Bedeutungen in einem Feld sind genau
    // die Art Doppelbelegung, die spaeter niemand mehr auseinanderhaelt.
    AnimPart part = AnimPart::Both;
    // Bei den Waffen- und Schwertschritten: welches Schwert (0/1) bzw.
    // welche Hand, und ein Index (Art, Klinge oder Modell - siehe Kind).
    int slot = 0;
    int index = -1;
    // Bei Roff: die gelesene Datei und die daraus aufaddierte Bahn (ab
    // from/fromAng). Geteilt, weil Schritte oft kopiert werden und eine
    // Bahn wie skiff_roff1 3800 Bilder hat.
    std::shared_ptr<const bhed::Roff> roff;
    std::shared_ptr<const RoffBahn> roffBahn;
    // Bei Roff: laeuft sie auf einer Figur (NPC, client) oder einem Mover?
    // Die Engine hat dafuer zwei Wege, siehe roffStand().
    bool roffFigur = false;
    Path path;               // welche Zeile im Skript
    std::string fremd;       // aus einem anderen Skript (dann path leer)
};

// Die Vorgabe fuer die Blendzeit, in Millisekunden.
//
// code/game/bg_public.h:176:  #define SETANIM_BLEND_DEFAULT 100
//
// Der Kommentar an PM_SetAnimFinal (bg_panimate.cpp:4625) sagt
// "// default blendTime=350" - der ist FALSCH und war es schon in JK2:
// codeJK2/game/bg_public.h:177 hatte 2002 ebenfalls 100, mit demselben
// veralteten Kommentar daneben. Movie Duels hat beides unveraendert
// uebernommen (code/game/bg_panimate.cpp:6023).
//
// Der Weg vom Skript bis zum Knochen, nachverfolgt:
//
//   SET_ANIM_BOTH -> Q3_SetAnimUpper/Lower -> SetUpperAnim/SetLowerAnim
//   (Q3_Interface.cpp:1955, 1985), und dort steht
//
//       NPC_SetAnim(ent, SETANIM_TORSO, animID,
//                   SETANIM_FLAG_RESTART|SETANIM_FLAG_HOLD|SETANIM_FLAG_OVERRIDE);
//
//   OHNE fuenftes Argument. Also greift die Vorgabe: 100 ms.
//
// 100 ms sind bei 20 Bildern je Sekunde ZWEI Bilder. Der Uebergang ist
// also da - er ist nur so kurz, dass er wie ein Sprung aussieht. Genau
// das war die Meldung.
inline constexpr double kSetAnimBlendDefault = 100.0;

// Wie weit ist der Uebergang? 0 heisst ganz die alte Stellung, 1 ganz die
// neue. Ohne Uebergang: 1.
//
// Die Grenzen kommen aus tr_ghoul2.cpp:1357 - geblendet wird NUR, solange
// 0 <= (jetzt - blendStart) < blendTime gilt. Davor und danach zaehlt
// allein die neue Animation.
[[nodiscard]] float blendFraction(const ActorState& st, double ms) noexcept;
// Dasselbe fuer den Uebergang des Oberkoerpers (torsoPrevAnimation).
[[nodiscard]] float torsoBlendFraction(const ActorState& st, double ms) noexcept;

// Ein aufgeloester .sab-Eintrag, so wie WP_SaberParseParms ihn in
// ps.saber[n] schreibt (wp_saberLoad.cpp:2894 ff.) - fuer EINE Figur und
// EIN Schwertfach, denn eine Farbe "random" wird je Figur und Fach
// festgelegt (saberFarbeAufloesen).
struct SaberArt {
    // Wie im Skript oder in der .npc genannt. Leer heisst: die Vorgaben
    // aus WP_SaberSetDefaults (wp_saberLoad.cpp:620 ff.) - so steht ein
    // Schwert nach SET_SABER1 "none" da, und so auch eines, dessen Name in
    // keiner .sab-Datei steht (WP_SaberParseParms kehrt dann nach den
    // Vorgaben zurueck).
    std::string name;
    int fach = 0;                // fuer saber[0] oder saber[1] aufgeloest
    bool gefunden = false;       // stand in den .sab-Dateien
    int modell = -1;             // der Griff, Index in Actor::modelle
    std::string typ;             // saberType, gross: SABER_STAFF ...
    std::string soundOn;
    std::string soundLoop;
    std::string soundOff;
    int numBlades = 1;
    int bladeStyle2Start = 0;
    bool zweihaendig = false;    // twoHanded   -> SFL_TWO_HANDED
    bool handgelenk = false;     // boltToWrist -> SFL_BOLT_TO_WRIST
    bool ohneKlinge = false;     // noBlade     -> SFL2_NO_BLADE
    bool ohneKlinge2 = false;    // noBlade2    -> SFL2_NO_BLADE2
    // Kampfstile als Bitmasken (1 << SS_*), siehe SaberDef.
    int stilGelernt = 0;
    int stilVerboten = 0;
    int stilEineKlinge = 0;
    // Laenge, Radius und Farbe je Klinge; `an` bleibt hier immer aus.
    std::array<KlingenZustand, kMaxKlingen> klinge{};
};

// Der Griff, den die Engine nimmt, wenn keiner genannt ist
// (DEFAULT_SABER_MODEL, bg_public.h:35).
inline constexpr const char* kStandardGriff = "models/weapons2/saber_1/saber_1.glm";

struct Actor {    std::string name;        // der Name aus dem affect
    std::string npcType;     // NPC_type aus dem Spawner, falls gefunden
    std::string modelPath;   // vom Benutzer zugeordnet
    float start[3]{};
    float startAngles[3]{};
    bool haveStart = false;
    // Zeigt die Entity auf ein Brush-Modell? "model" "*12" -> 12.
    //
    // Das ist der Weg, auf dem Tueren, Plattformen und Raumschiffe zu ihrer
    // Geometrie kommen - sie haben kein .glm, sondern ein Stueck der Karte.
    // -1 heisst: keins.
    int brushModel = -1;
    // Kartenschluessel "linear" "1": gleichfoermig statt weich auslaufend.
    bool linearBewegung = false;
    std::vector<ActorStep> steps;

    // --- Was die Szene von der animation.cfg der Figur wissen muss --------
    //
    // Laenge einer Animation, wie die Engine sie fuer SETANIM_FLAG_HOLD
    // rechnet (bg_panimate.cpp:4721): (numFrames - 1) * |frameLerp|, mit
    // frameLerp = ceil(1000 / fps) in ganzen Millisekunden. Und frameLerp
    // selbst, fuer die Tempogrenze beim Gehen und Laufen.
    //
    // Leer, solange das Skelett nicht geladen ist - dann gibt es keine
    // Timer, und eine Animation laeuft wie bisher weiter.
    std::map<std::string, double> animDauerMs;
    std::map<std::string, double> bildDauerMs;

    // --- Nur fuer den Aufbau: was ueber affect-Bloecke erhalten bleibt ----
    //
    // SCF_WALKING und SCF_RUNNING sind zwei eigene Schalter am NPC
    // (Q3_Interface.cpp:4887 ff.), RUNNING geht vor (g_navigator.cpp:2909).
    // Vorher war es EIN bool, das jeder affect-Block zuruecksetzte - und
    // SET_RUNNING "false" liess die Figur gehen.
    bool gehSchalter = false;
    bool laufSchalter = false;
    float gehTempo = 90.0F;
    float laufTempo = 300.0F;

    // --- Waffe und Schwerter -------------------------------------------
    //
    // Modellpfade (Waffen, Griffe, Kinomodelle), auf die ActorState und die
    // Schritte per Index zeigen - so traegt der Zustand, der je Bild neu
    // entsteht, nur Zahlen statt Pfade.
    std::vector<std::string> modelle;
    std::vector<SaberArt> saberArten;
    // Die Ausstattung beim Spawnen (NPC_ParseParms und NPC_Begin). Ohne
    // .npc bleibt startWaffe leer - siehe ActorState::waffe.
    std::string startWaffe;
    bool startHatSaber = false;
    bool startDual = false;
    std::array<SaberZustand, 2> startSaber{};
    std::array<HandBelegung, 2> startHand{};
    // ps.saberAnimLevel nach dem Spawnen (SS_FAST 1 ... SS_STAFF 7; 0 ist
    // SS_NONE) - aus `saberStyle` der .npc oder den Vorgaben nach Klasse
    // (WP_SaberInitBladeData). Bestimmt die Kampfhaltung, siehe
    // saberGrundhaltung.
    int startStil = 0;
    // Zeitraeume, in denen die Figur nicht gezeichnet wird, unabhaengig von
    // SET_INVISIBLE: der Spieler, solange die Skriptkamera an ist
    // (CG_Player, cg_players.cpp:15602: "if (in_camera && clientNum == 0)
    // return;"). [von, bis), bis < 0 heisst: bis zum Ende.
    std::vector<std::pair<double, double>> kameraVerborgen;

    [[nodiscard]] ActorState at(double ms) const;
};

// Eine Notiz einer ROFF-Bahn, die im Spiel etwas ausloest.
//
// G_RoffNotetrackCallback (g_roff.cpp:37) kennt zwei: "effect" spielt
// einen Effekt am Ort der Entity (plus Versatz), "sound" einen Klang auf
// CHAN_BODY. Hier nur als Angabe fuer spaeter - weder Effekt noch Klang
// werden daraus schon gestartet. In den ROFFs, die PLAY_ROFF in Movie
// Duels abspielt, steht keine einzige Notiz; die Notizen, die es gibt
// ("fov", "cut", "fovzoom"), gehoeren zu Kamera-ROFFs (camera PATH).
struct RoffEreignis {
    double ms = 0.0;             // wann G_Roff das Bild anwendet
    std::string figur;           // affect-Name der Entity
    std::string datei;           // Name aus dem play-Befehl
    std::string text;            // die Notiz, roh
    RoffNotizBefehl befehl;      // zerlegt
    // Wo und wie: bei "effect" mit Versatz und ggf. eigenen Winkeln
    // (roffEffektOrt), bei "sound" der Ort der Entity (s.pos.trBase).
    float ort[3]{};
    float winkel[3]{};
};

struct Scene {
    std::vector<Actor> actors;
    double durationMs = 0.0;
    // Alle ausgeloesten ROFF-Notizen, nach Zeit.
    std::vector<RoffEreignis> roffEreignisse;

    [[nodiscard]] const Actor* find(const std::string& name) const;
};

// Aus Skript und Karte die Bahnen aller Figuren bauen.
//
// Die Karte wird fuer zweierlei gebraucht: die Startorte der NPC_spawner und
// die Orte der waypoint_navgoal, auf die SET_NAVGOAL zeigt.
// Was der Aufbau zusaetzlich wissen kann, wenn die Oberflaeche es hat.
//
// animDauer/bildDauer: Laenge einer Animation bzw. eines Bildes fuer die
// Figur mit diesem affect-Namen, in ms; negativ heisst unbekannt. Damit
// wartet wait( "task" ) so lange, wie die Animation laeuft (TID_ANIM_*
// endet, wenn ihr Timer abgelaufen ist - bg_panimate.cpp:4399).
//
// roff: die gelesene ROFF-Datei zu einem Namen aus play ( "PLAY_ROFF", ... )
// - der Name so, wie er im Skript steht (roffPfad() macht den Archivpfad
// daraus). nullptr, wenn es sie nicht gibt; dann steht die Entity still.
struct SzenenHilfe {
    const std::map<std::string, struct NpcDef>* npcs = nullptr;
    // Fuer Waffen und Schwerter: die .sab-Eintraege und weapons.dat. Fehlen
    // sie, bleibt es bei Vorgaben (Standardschwert, keine Waffenmodelle).
    const std::map<std::string, struct SaberDef>* sabers = nullptr;
    const std::map<std::string, struct WeaponDef>* waffen = nullptr;
    std::function<double(const std::string& figur, const std::string& anim)> animDauer;
    std::function<double(const std::string& figur, const std::string& anim)> bildDauer;
    std::function<std::shared_ptr<const Roff>(const std::string& name)> roff;
};

[[nodiscard]] Scene buildScene(const Script& s, const MapData& map,
                               const SzenenHilfe* hilfe = nullptr);

// NPC-Beschreibungen: welcher NPC_type benutzt welches Modell?
//
// In JKA stehen sie unter ext_data/npcs/*.npc:
//
//     md_ani_tcwa
//     {
//         playerModel   anakin_tcw
//         ...
//     }
//
// Daraus wird models/players/anakin_tcw/model.glm. Ohne diese Zuordnung
// wuesste man zu "md_ani_tcwa" nicht, welche Figur gemeint ist.
// Was aus einer .npc-Datei gebraucht wird.
//
// Bis rc255 war das nur der Modellname, und NpcMap war schlicht
// map<string,string>. Fuer das Lichtschwert kommt der `saber`-Schluessel
// dazu - er nennt den Eintrag in einer .sab-Datei, und der wiederum traegt
// Modell, Klaenge, Farbe und Laenge der Klinge.
struct NpcDef {
    std::string playerModel;
    // legsmodel - eine Figur aus alten MD3-Teilen (NPC_stats.cpp:2790).
    // Ohne playerModel setzt NPC_ParseParms kein Ghoul2-Modell
    // (md3_model bleibt qtrue, ebenda:4549), und das Spiel zeichnet
    // models/players/<legsmodel>/lower.md3 (CG_RegisterClientModelname,
    // cg_main.cpp:1255). So der Schwebedroide remote_sp.
    std::string legsModel;
    std::string saber;      // leer heisst: keines genannt
    // Der `weapon`-Schluessel, z.B. "WP_BLASTER". Ueber die 120
    // .npc-Dateien in assets1 gezaehlt: 63 mal WP_SABER, 31 mal WP_BLASTER,
    // 29 mal WP_NONE, 15 mal WP_MELEE, 7 mal WP_REPEATER.
    std::string weapon;
    // Irgendeine weapon-Zeile nannte WP_SABER (ps.weapons[WP_SABER]) - dann
    // schaltet SET_SABERACTIVE auf das Schwert, auch wenn eine andere Waffe
    // in der Hand ist.
    bool saberImInventar = false;
    // Der `customSkin`-Schluessel. Daraus wird
    // models/players/<playerModel>/model_<customSkin>.skin
    // (NPC_stats.cpp:1908). Die Vorgabe ist "default" - ebenda:1597.
    //
    // Gemeldet: "was noch fehlt, sind richtige Skinvarianten von den
    // Modellen." Genau der hier: behaved las immer model_default.skin.
    //
    // Ueber die 120 .npc-Dateien in assets1 gezaehlt: 73 NPC-Typen nennen
    // einen eigenen Skin, in 41 verschiedenen Paaren aus Modell und Skin.
    // Darunter DASSELBE Modell mit verschiedenen Skins - cultist in braun
    // und rot, reborn_new in blau und rot. Wer den Modellpuffer nur nach
    // dem Modellnamen schluesselt, zeigt dem zweiten den Skin des ersten.
    std::string customSkin = "default";
    // walkSpeed/runSpeed aus der .npc (NPC_ParseParms, dl_NPC_stats.cpp:
    // 3293 ff.). 0 heisst: nicht angegeben, es gelten 90/300.
    float walkSpeed = 0.0F;
    float runSpeed = 0.0F;
    // --- Fuer Waffe und Schwerter (NPC_ParseParms, NPC_stats.cpp) --------
    //
    // saber2 - das zweite Schwert (ebenda:4094). Leer heisst keines.
    std::string saber2;
    // saberStyle (ebenda:4498), auf SS_FAST..SS_STAFF (1..7) gekappt.
    // 0 heisst: nicht genannt - dann waehlt WP_SaberInitBladeData nach
    // Klasse und Rang.
    int saberStil = 0;
    // `class` (z.B. "CLASS_VADER") und `rank` (z.B. "lt"), klein wie gelesen.
    std::string klasse;
    std::string rang;
    // `scale` / `scaleX` / `scaleY` / `scaleZ` in Prozent, als Faktor
    // (NPC_ParseParms, NPC_stats.cpp: s.modelScale = n / 100, nur wenn
    // n != 100 und nicht negativ). Die Juenglinge der Mod haben 50.
    float skala[3] = {1.0F, 1.0F, 1.0F};
    // playerTeam, z.B. "TEAM_ENEMY" (ebenda:3378). Braucht es fuer die
    // Vorgabewaffe, wenn `weapon` fehlt oder WP_NONE ist (npcTeamWaffe).
    std::string team;
    // kotorWeapons: dann haengt altweaponmodel statt weaponmodel in der
    // Hand (ebenda:4059; NPC_spawn.cpp:679 ff.).
    bool kotorWeapons = false;
    // dualPistols: WP_DUAL_PISTOL / WP_DUAL_CLONEPISTOL auch links
    // (Char_Dual_Pistols, AI_Character.cpp:173; NPC_spawn.cpp:740 ff.).
    bool dualPistols = false;
    // saberColor/saberColor2..8, saber2Color..., saberLength..., saberRadius...
    // (ebenda:4128 ff.) - JE KLINGE; leer bzw. 0 heisst: keine Angabe.
    // Ein `saber`-Schluessel NACH einer Farbangabe loescht sie wieder,
    // denn WP_SaberParseParms setzt das ganze Schwert neu auf - der Leser
    // tut dasselbe.
    std::array<std::array<std::string, kMaxKlingen>, 2> saberFarbe{};
    std::array<std::array<float, kMaxKlingen>, 2> saberLaenge{};
    std::array<std::array<float, kMaxKlingen>, 2> saberRadius{};
};

using NpcMap = std::map<std::string, NpcDef>;   // Typ -> Beschreibung

void parseNpcFile(const std::string& text, NpcMap& out);

// --- Lichtschwerter ----------------------------------------------------
//
// Gemeldet: "weder die Lichtschwerter werden geladen und ordentlich
// angezeigt als auch die Sounds."
//
// Die Kette, im Quelltext nachgesehen:
//
//   NPC_type            "md_ani_ga"     aus dem NPC_spawner der Karte
//   -> saber            "anakin_ep2"    aus ext_data/npcs/*.npc
//   -> .sab-Eintrag                     aus ext_data/sabers/*.sab
//   -> soundOn / saberModel / ...
//
// Der Aufbau einer .sab-Datei ist derselbe wie bei .npc und den
// Shaderskripten: Name, geschweifte Klammer, Zeilen mit Schluessel und
// Wert. Die Schluesselnamen stehen in wp_saberLoad.cpp:1827 ff.
//
// Fehlt ein Klangschluessel, gelten die Vorgaben aus
// wp_saberLoad.cpp:368 ff.:
//
//     soundOn   sound/weapons/saber/enemy_saber_on.wav
//     soundLoop sound/weapons/saber/saberhum3.wav
//     soundOff  sound/weapons/saber/enemy_saber_off.wav
//
// Gemessen an den 28 .sab-Dateien aus MD_Hilts und assets1: 261
// Eintraege, davon 252 mit eigenem soundOn - die Vorgabe greift also
// selten, aber sie greift.
struct SaberDef {
    std::string name;        // Klartextname, nur zur Anzeige
    std::string model;       // saberModel, ein .glm
    std::string soundOn;
    std::string soundLoop;
    std::string soundOff;
    std::string color;
    float length = 0.0F;
    // saberRadius. Die Vorgabe der Engine sind 3
    // (SABER_RADIUS_STANDARD, wp_saber.h:57).
    float radius = 3.0F;
    // --- Mehr als eine Klinge ----------------------------------------
    //
    // Doppelklingen, Zweihaender und Griffe ohne Klinge (Elektrostab,
    // Gaffi-Stock). Ueber die .sab-Dateien der Mod gezaehlt: 94 mal
    // numBlades 2, 85 mal twoHanded 1, 71 mal noBlade 1.
    std::string type;          // saberType, gross geschrieben
    int numBlades = 1;         // numBlades, 1..8 (Saber_ParseNumBlades)
    int bladeStyle2Start = 0;  // ab dieser Klinge gelten die *2-Schalter
    bool twoHanded = false;
    bool boltToWrist = false;
    bool noBlade = false;
    bool noBlade2 = false;
    // Kampfstile, Bitmasken aus 1 << SS_* (wp_saberLoad.cpp:1320 ff.):
    // saberStyle lernt EINEN Stil und verbietet alle anderen,
    // saberStyleLearned/-Forbidden fuegen einen hinzu. singleBladeStyle
    // gilt, solange von zwei Klingen nur die erste brennt.
    int stylesLearned = 0;
    int stylesForbidden = 0;
    int singleBladeStyle = 0;
    // Je Klinge, in der Reihenfolge der Datei ausgewertet: saberColor
    // setzt ALLE acht, saberColor2 nur die zweite (wp_saberLoad.cpp:878
    // ff.). Leer bzw. 0 heisst: keine Angabe - dann gilt die Vorgabe
    // (zufaellige Farbe, Laenge 32, Radius 3; ebenda:626 ff.).
    std::array<std::string, kMaxKlingen> bladeColor{};
    std::array<float, kMaxKlingen> bladeLength{};
    std::array<float, kMaxKlingen> bladeRadius{};
};

using SaberMap = std::map<std::string, SaberDef>;

void parseSaberFile(const std::string& text, SaberMap& out);

// --- Waffen ------------------------------------------------------------
//
// Gefragt: "wo bekommen wir das Projektil her?"
//
// Aus ext_data/weapons.dat. Die Datei nennt je Waffe das Modell und die
// Effekte, und die Effekte sind gewoehnliche .efx-Dateien - Line-Primitive,
// die behaved laengst zeichnet. Ein Beispiel aus assets1:
//
//     weapontype       WP_BLASTER
//     weaponmodel      models/weapons2/blaster_r/blaster.md3
//     muzzleEffect     blaster/muzzle_flash
//     missileFuncName  blaster_func
//
// Der Weg zum Bild ist damit: NPC_type -> .npc `weapon` -> weapons.dat
// -> `muzzleEffect` und das Modell -> dessen Tag "tag_flash" -> dort
// haengen Muendungsblitz und Startpunkt des Geschosses.
struct WeaponDef {
    std::string model;         // weaponmodel
    std::string altModel;      // altweaponmodel (kotorWeapons)
    std::string muzzleEffect;  // muzzleEffect, OHNE "effects/" und ".efx"
    std::string altMuzzleEffect;
    int fireTimeMs = 0;        // firetime
};

// Schluessel ist der weapontype, z.B. "WP_BLASTER".
using WeaponMap = std::map<std::string, WeaponDef>;

void parseWeaponsDat(const std::string& text, WeaponMap& out);

// --- Was von einer Waffe in der Hand haengt ----------------------------
//
// weapons.dat nennt ein .md3 (das Modell der Ich-Ansicht). In die Hand
// einer Figur kommt aber ein Ghoul2-Modell, und der Name wird dafuer
// umgeschrieben (G_CreateG2AttachedWeaponModel, wp_saber.cpp:729 ff.):
//
//     ".md3" abschneiden; steht nirgends "_w" und nicht "noweap" darin,
//     "_w" anhaengen; dann ".glm" anhaengen.
//
// Aus models/weapons2/blaster_r/blaster.md3 wird also blaster_w.glm. Ein
// Pfad ohne ".md3" bleibt, wie er ist (Schwertgriffe sind schon .glm).
// Ein LEERER Name wird zum Standardgriff - so steht es dort auch.
[[nodiscard]] std::string waffenWeltmodell(const std::string& weaponMdl);

// Eine Schwertfarbe festlegen, wie TranslateSaberColor sie liest
// (NPC_stats.cpp:402 ff.), klein geschrieben.
//
// "random" waehlt die Engine mit Q_irand(SABER_ORANGE, SABER_PURPLE) -
// orange, gelb, gruen, blau oder lila, NIE rot. "prequel_random" gruen oder
// blau. Hier nicht zufaellig, sondern aus `saat` (Figurenname, Fach,
// Klinge) berechnet: eine Vorschau, die man anhaelt und zurueckspult, darf
// nicht bei jedem Aufbau eine andere Farbe zeigen - aber jede Figur soll
// ihre EIGENE bekommen statt aller dieselbe. Leer gilt wie "random": so
// belegt WP_SaberSetDefaults jede Klinge (wp_saberLoad.cpp:626).
[[nodiscard]] std::string saberFarbeAufloesen(const std::string& name,
                                              const std::string& saat);

// Eine Farbe, die TranslateSaberColor NICHT beim Namen kennt, als RGB 0..1
// (Q_parseSaberColor, q_shared.cpp:666 ff.): ein Buchstabe u..z und sechs
// Hexziffern ("xff3f00" - so steht es 36 mal in den .sab-Dateien der Mod),
// oder ein Buchstabe a..t als Farbton. Unlesbare Hexziffern ergeben weiss,
// wie dort. false fuer die benannten Farben (red, orange, yellow, green,
// blue, purple, unstable_red, black, white, rgb, custom) - die zeichnet die
// Engine mit eigenen Bildern statt mit einer Farbe.
[[nodiscard]] bool saberFarbeRgb(const std::string& farbe, float rgb[3]);

// Die Waffe, die ein NPC bekommt, wenn seine .npc keine nennt oder WP_NONE
// sagt: NPC_Begin ruft dann NPC_SetWeapons (NPC_spawn.cpp:1848), und das
// sucht nach Team und NPC_type aus (NPC_WeaponsForTeam, ebenda:1342 ff.).
// Rueckgabe z.B. "WP_BLASTER"; "WP_NONE", wenn es keine gibt.
[[nodiscard]] std::string npcTeamWaffe(const std::string& team,
                                       const std::string& npcType,
                                       int spawnflags);

// Haelt die Figur ein Schwert, so dass seine Klingen wachsen und gezeichnet
// werden? ps.weapon == WP_SABER - oder unbekannt (siehe ActorState::waffe).
[[nodiscard]] bool haeltSaber(const ActorState& st) noexcept;

// Wie lang ist Klinge `klinge` von Schwert `saberNr` zur Zeit ms?
//
// Die Engine laesst jede Klinge fuer sich wachsen und schrumpfen, mit
// lengthMax/10 je 100 ms (cg_players.cpp:16233 ff., cg_ignitionSpeed
// Vorgabe 1) - also eine Sekunde von null bis voll. Das geschieht NUR,
// solange die Figur das Schwert haelt (der Block steht unter "weapon ==
// WP_SABER", ebenda:16200); sonst bleibt die Laenge stehen.
//
// Nicht nachgebildet: SS_FAST waechst doppelt so schnell (lengthMax/5).
// Den Kampfstil einer Figur kennt behaved nicht.
[[nodiscard]] float klingenLaenge(const ActorState& st, int saberNr, int klinge,
                                  double ms) noexcept;

// --- Nur fuer den Aufbau (src/ausruestung.cpp) --------------------------
//
// buildScene legt damit die Ausstattung beim Spawnen fest und loest Namen in
// Indizes auf; Actor::at wendet die Schritte an. Sie stehen hier und nicht
// versteckt, damit die Proben (tests/scenetest.cpp) sie einzeln pruefen
// koennen.

// Pfad in Actor::modelle eintragen (einmal je Pfad) und den Index liefern.
// Leer liefert -1.
int modellIndex(Actor& a, const std::string& pfad);
// Die SaberArt fuer diesen .sab-Namen und dieses Fach holen oder anlegen.
// Leerer Name: die Vorgaben von WP_SaberSetDefaults.
int saberArtIndex(Actor& a, const std::string& name, int fach,
                  const SzenenHilfe* hilfe);
// Das Weltmodell einer Waffe als Index in Actor::modelle; -1, wenn weapons.dat
// fehlt oder die Waffe dort nicht steht.
int waffenModellIndex(Actor& a, const std::string& waffe, bool kotor,
                      const SzenenHilfe* hilfe);
// Die Ausstattung beim Spawnen. def ist die .npc der Figur oder nullptr.
void ausstattungBeimSpawnen(Actor& a, const NpcDef* def, const SzenenHilfe* hilfe,
                            int spawnflags);
// Einen Waffen- oder Schwertschritt anwenden (Kind::Waffe, SaberWahl,
// SaberFarbe, KlingeSchalter, HandModell und Saber).
void ausruestungSchritt(const Actor& a, ActorState& st, const ActorStep& step);
// Was sich erst aus dem ganzen Zustand ergibt (der Summton).
void ausruestungAbschluss(const Actor& a, ActorState& st);

// --- Die Kampfhaltung mit brennender Klinge ----------------------------
//
// Eine Figur, die mit Lichtschwert in der Hand steht, steht im Spiel
// nicht in BOTH_STAND1: PM_Footsteps waehlt, solange irgendeine Klinge
// Laenge hat, PM_ReadyPoseForSaberAnimLevel (bg_pmove.cpp:11860 ff.) -
// nach ps.saberAnimLevel. Mit den Einstellungen der Mod
// (g_SerenityJediEngineMode 2, g_RealisticBlockingMode 0,
// g_ActivateAnimationStyle 0) ergibt das fuer Spieler und NPC dieselbe
// Tabelle (PM_ReadyPoseForSaberAnimLevelAMD, ebenda:14660; der
// "Cosmetic mode" in PM_ReadyPoseForSaberAnimLevelNPC):
//
//     SS_FAST   BOTH_SABERFAST_STANCE     SS_TAVION  BOTH_SABERTAVION_STANCE
//     SS_MEDIUM BOTH_STAND2               SS_DUAL    BOTH_SABERDUAL_STANCE
//     SS_STRONG BOTH_SABERSLOW_STANCE     SS_STAFF   BOTH_SABERSTAFF_STANCE
//     SS_DESANN BOTH_SABERDESANN_STANCE   sonst      BOTH_STAND2
[[nodiscard]] const char* saberHaltungFuerStil(int stil) noexcept;
// ps.saberAnimLevel im Zustand st: der Startstil, berichtigt wie in
// PM_WeaponLightsaber (bg_pmove.cpp:22686 ff.) - singleBladeStyle,
// verbotene Stile (WP_SaberStyleValidForSaber, WP_UseFirstValidSaberStyle),
// zwei Schwerter.
[[nodiscard]] int saberStilJetzt(const Actor& a, const ActorState& st) noexcept;
// Setzt die Kampfhaltung, wenn st gerade in der Grundhaltung steht
// (Actor::at ruft es nur dann). Mit Blende beim Zuenden und nach dem
// Einfahren der Klinge zurueck in den Stand.
void saberGrundhaltung(const Actor& a, ActorState& st, double ms);

// --- Wo ein Schuss anfaengt --------------------------------------------
//
// Berichtigt: "SBD schiesst es aus der Hand, deshalb gibt es kein eigenes
// Waffenmodell."
//
// Damit faellt der Weg ueber `tag_flash` weg - der gilt nur, wenn ein
// Waffenmodell gezeichnet wird. Ohne eines rechnet die Engine die Muendung
// aus dem ENTITYURSPRUNG, ganz ohne Bolzen (CalcMuzzlePoint,
// g_weapon.cpp:449 ff.):
//
//     VectorCopy( ent->currentOrigin, muzzlePoint );
//     switch( ent->s.weapon ) {
//     case WP_BLASTER:
//         muzzlePoint[2] += ent->client->ps.viewheight;
//         muzzlePoint[2] -= 1;
//         VectorMA( muzzlePoint, 2, forwardVec, muzzlePoint );  // NPC
//         VectorMA( muzzlePoint, 1, vrightVec, muzzlePoint );
//
// Die Blickhoehe ist standheight + STANDARD_VIEWHEIGHT_OFFSET, also
// 40 + (-4) = 36 (bg_public.h:52 und 63, NPC_stats.cpp:2031). Fuer einen
// Blaster liegt die Muendung damit 35 Einheiten ueber dem Ursprung, zwei
// nach vorn und eine nach rechts.
//
// Der Kommentar dort sagt auch, warum die zwei so klein sind: "NPC, don't
// set too far forward otherwise the projectile can go through doors".
//
// `yawDeg` ist der Gierwinkel der Figur. Rueckgabe in Weltkoordinaten.
void muzzlePoint(const float origin[3], float yawDeg,
                 const std::string& weapon, float out[3]);

// Die Geschwindigkeiten der Engine, in Einheiten je Sekunde.
// Aus NPC_stats.cpp: walkSpeed = 90, runSpeed = 300.
// --- Wie schnell die ANIMATION dabei laeuft -----------------------------
//
// Gemeldet: "statt sauber ueber den Boden zu laufen slidet er dauerhaft
// ein bisschen, und das wird auch das Stottern ausloesen."
//
// Genau das gleicht die Engine aus, und der Schalter dafuer ist
// standardmaessig AN (`g_noFootSlide`, Vorgabe "1", g_main.cpp:640).
//
// bg_panimate.cpp:4824:
//
//     animSpeed *= (gent->resultspeed / moveSpeedOfAnim);
//     if (animSpeed < 0.01f) animSpeed = 0.01f;
//     if (animSpeed > 1.5f * timeScaleMod) animSpeed = 1.5f * timeScaleMod;
//
// `moveSpeedOfAnim` ist die Geschwindigkeit, FUER DIE die Animation
// gebaut wurde. Laeuft die Figur schneller, laeuft auch die Animation
// schneller - und die Fuesse bleiben am Boden.
//
// Die Werte stehen im selben Block (ebenda:4778 ff.):
inline constexpr float kAnimSpeedWalk = 50.0F;    // gehen
inline constexpr float kAnimSpeedRun = 150.0F;    // laufen
inline constexpr float kAnimSpeedCrouch = 75.0F;  // geduckt gehen
// Die Obergrenze aus derselben Stelle. timeScaleMod ist 1, solange die
// Zeit nicht gedehnt wird.
inline constexpr float kAnimSpeedMax = 1.5F;
inline constexpr float kAnimSpeedMin = 0.01F;

inline constexpr float kWalkSpeed = 90.0F;
inline constexpr float kRunSpeed = 300.0F;

}  // namespace bhed
#endif
