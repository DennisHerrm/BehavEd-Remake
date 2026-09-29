// camtrack.h - wo die Kamera zu einem Zeitpunkt steht
//
// timeline.h sagt, WANN welcher Befehl laeuft. Diese Datei sagt, was daraus
// fuer die Kamera folgt: Ort, Blickrichtung und Blickwinkel zu jeder Zeit -
// auch ZWISCHEN zwei Einstellungen, waehrend eine Fahrt laeuft.
//
// Alles hier ist an cg_camera.cpp nachgemessen, nicht angenommen:
//
//   * Bewegung ist LINEAR. Zeile 1266:
//         vieworg[i] = origin[i]
//                    + ((origin2[i] - origin[i]) / move_duration)
//                      * (cg.time - move_time)
//     Keine Beschleunigung, keine Glaettung.
//
//   * Ein Schwenk nimmt den KUERZESTEN Weg. CGCam_Pan rechnet zwei Deltas
//     aus (delta1 und delta1 +/- 360) und nimmt das betragsmaessig kleinere,
//     sofern panDirection null ist - und das ist es in Ravens Skripten fast
//     immer. Von 350 Grad auf 10 Grad sind das +20, nicht -340. Wer das
//     falsch macht, laesst die Kamera einmal fast ganz herumfahren.
//
//   * Dauer 0 heisst SPRUNG, nicht "sofort fertig interpolieren":
//     CGCam_Move setzt bei !duration direkt die Position.
//
//   * Der Blickwinkel ist der WAAGERECHTE, Vorgabe 90
//     (CAMERA_DEFAULT_FOV in cg_camera.h).
#ifndef BHED_CAMTRACK_H
#define BHED_CAMTRACK_H

#include "bhed/script.h"

#include <functional>
#include <string>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bhed {

struct CameraState {
    float pos[3]{};
    float angles[3]{};       // pitch, yaw, roll - wie in der Engine
    float fovX = 90.0F;
    // Ist die Kamera zu diesem Zeitpunkt ueberhaupt aktiv?
    // camera ( ENABLE ) schaltet sie ein, camera ( DISABLE ) wieder aus.
    bool enabled = false;
    // Die Farbe der Vollbild-Blende (camera FADE), RGBA 0..1. Sie BLEIBT
    // auf dem Ziel stehen, wenn die Blende fertig ist - ein Schwarzbild
    // bleibt schwarz, auch nach DISABLE (CGCam_UpdateFade laeuft immer,
    // CGCam_DrawWideScreen fuellt, sobald alpha > 0).
    float fadeColor[4]{};
    // Die schwarzen Balken: 0 = keine, 1 = volle Hoehe (48 von 480). Nach
    // ENABLE blenden sie in BAR_DURATION = 1000 ms ein, nach DISABLE aus -
    // Deckkraft und Hoehe gemeinsam (CGCam_UpdateBarFade).
    float bars = 0.0F;
    // Laeuft gerade ein Schwenk (PAN oder ROLL mit Dauer)? Dann zielt FOLLOW
    // nicht - CGCam_Update prueft check_follow nur ohne CAMERA_PANNING.
    bool panning = false;
    // FOLLOW: seit wann, wie schnell, und ob die Kamera von ihrem Blick aus
    // hinueberblendet (initLerp) statt beim ersten Bild hinzuspringen.
    double followSinceMs = 0.0;
    float followSpeed = 100.0F;
    bool followInitLerp = false;
    // Das Zittern (SHAKE) in den Winkeln, schon in `angles` enthalten -
    // getrennt gemerkt, damit FOLLOW es nach dem Zielen wieder aufschlagen
    // kann (die Engine zittert NACH dem Zielen, CGCam_UpdateShake zuletzt).
    float shakeAngles[2]{};

    // Welcher cameraGroup wird gerade gefolgt? Leer heisst: keiner.
    //
    // Aufgeloest wird das NICHT hier, sondern dort, wo die Karte bekannt
    // ist: die Kamera zielt auf den Mittelpunkt aller Entities mit diesem
    // cameraGroup, und die stehen in der .bsp, nicht im Skript
    // (cg_camera.cpp, CGCam_FollowUpdate: alle Treffer sammeln, mitteln,
    // vectoangles auf die Richtung).
    //
    // In allen 284 Skripten der Mod steht FOLLOW genau zweimal, und beide
    // Male mit "NULL" - also zum ABSCHALTEN. Die Engine behandelt "none",
    // "NULL" und den leeren Namen gleich: Zielen aus.
    std::string followGroup;
};

// Ein Abschnitt der Kamerabahn: von wann bis wann sich was aendert.
// --- Welche Richtungsangabe braucht camera(PAN, ...)? --------------------
//
// PAN hat zwei Vektoren: das ZIEL und eine RICHTUNG.
//
//     camera ( PAN, <pitch yaw roll>, <richtung>, <dauer> )
//
// Die Richtung sagt nur, HERUM welche Seite gedreht wird - ihr Betrag
// zaehlt nicht, nur das Vorzeichen je Achse. Ist eine Komponente null,
// nimmt die Engine den KUERZEREN Weg (CGCam_Pan in code/cgame/
// cg_camera.cpp: "Didn't specify a direction, pick shortest").
//
// Daraus folgt der Fall, der uns interessiert: dreht der Anwender am
// Gizmo um mehr als 180 Grad, ist der gewollte Weg der laengere. Steht
// dann null in der Richtung, faehrt die Kamera im Spiel ANDERSHERUM als
// in der Vorschau - dieselbe Endstellung, aber die falsche Bewegung.
//
// Rueckgabe: -1, 0 oder +1 - genau das, was in die Richtung gehoert.
//
// Grenze: die Engine kann in EINEM PAN hoechstens knapp 360 Grad fahren
// (angles2 ist immer die Differenz zweier normalisierter Winkel). Wer
// weiter dreht, bekommt den Rest modulo 360; dafuer braucht es mehrere
// PAN-Befehle.
[[nodiscard]] float panDirectionFor(float deltaDeg);

// --- Von hier nach dort schauen ------------------------------------------
//
// Die Umkehrung von Camera::forward fuer eine Richtung: pitch und yaw, die
// von "from" auf "to" zeigen. Roll bleibt null - eine Blickrichtung sagt
// nichts ueber die Neigung.
//
// Gebraucht fuer camera(FOLLOW, ...): dort zielt die Kamera auf den
// Mittelpunkt einer cameraGroup, und in der Engine steht dafuer
// vectoangles(dir) in CGCam_FollowUpdate.
//
// Hier, weil dieselbe Rechnung an DREI Stellen gebraucht wird: beim
// Abspielen, beim Zeichnen der Kamera und beim Abtasten der Bahn. Drei
// Abschriften laufen auseinander, und dann zeigt die gezeichnete Kamera
// woanders hin als die abgespielte.
void aimAngles(const float from[3], const float to[3], float outAngles[3]);

struct CamSegment {
    enum class Kind : std::uint8_t {
        Move, Pan, Zoom, Roll, Enable, Disable, Fade,
        // camera ( SHAKE, staerke, dauer ) und
        // camera ( FOLLOW, "gruppe", tempo, anfangsblende )
        //
        // Zusammen sieben Verwendungen in den 284 Skripten der Mod - selten,
        // aber die letzten beiden Unterbefehle, die noch fehlten. Damit sind
        // 1902 von 1902 abgedeckt.
        Shake, Follow };

    Kind kind = Kind::Move;
    double startMs = 0.0;
    double endMs = 0.0;
    float from[3]{};
    float to[3]{};           // bei Zoom nur [0], bei Shake die Staerke
    std::string group;       // bei Follow: der cameraGroup-Name
    Path path;               // welche Zeile im Skript
    std::string fremd;       // aus einem anderen Skript (dann path leer)
    // Ab wann der Bruchteil zaehlt, wenn er nicht bei startMs anfaengt: ein
    // ZOOM mit Dauer 0 mitten in einem laufenden setzt nur den Ausgangswert
    // neu, der laufende Zoom geht weiter (CGCam_Zoom loescht CAMERA_ZOOMING
    // nicht). Negativ heisst: startMs.
    double lerpStartMs = -1.0;
    // ROLL mit Dauer ist ein Schwenk ueber ALLE drei Achsen (siehe
    // buildCameraTrack); ROLL ohne Dauer setzt nur den Rollwinkel.
    bool alleAchsen = false;
    // FADE: Ausgangs- und Zielfarbe (RGBA).
    float farbeVon[4]{};
    float farbeZu[4]{};
    // FOLLOW: Tempo und Anfangsblende.
    float speed = 100.0F;
    bool initLerp = false;
};

struct CameraTrack {
    std::vector<CamSegment> segments;
    double durationMs = 0.0;

    // Wo steht die Kamera zu diesem Zeitpunkt?
    [[nodiscard]] CameraState at(double ms) const;

    // Der Befehl, der zu diesem Zeitpunkt gerade laeuft - fuer das
    // Hervorheben im Baum.
    [[nodiscard]] const CamSegment* activeAt(double ms) const;
};

// Aus einem Skript die Kamerabahn bauen.
//
// Nur die OBERSTE Ebene: camera-Befehle in einem affect-Block gehoeren zu
// einer anderen Entity, nicht zur Kamera des Spielers. Gemessen an Ravens
// 1510 Skripten steht kein camera-Befehl in einem affect-Block.
// Wo eine benannte Marke in der Karte steht.
//
// Viele Missionen setzen die Kamera NICHT auf Zahlen, sondern auf eine
// Entity:
//
//     camera ( MOVE, tag( "cam1b", ORIGIN ), 0.000 );
//     camera ( PAN,  tag( "cam1b", ANGLES ), <0 0 0>, 0.000 );
//
// In der Karte steht dann ein ref_tag mit targetname "cam1b" und seinem
// origin. Ohne Aufloesung liest ein Vektorleser dort NULL Zahlen, und die
// Kamera steht auf 0 0 0 - im Boden. Genau so gemeldet fuer md_ga_jedi.
//
// Die Auskunft kommt von aussen, weil der Kern die Karte nicht kennt: die
// Oberflaeche hat die Entities, dieses Modul hat das Skript. Gibt es keine
// Auskunft oder findet sie nichts, bleibt es beim bisherigen Verhalten.
using TagLookup =
    std::function<bool(const std::string& name, float outOrigin[3],
                       float outAngles[3])>;

[[nodiscard]] CameraTrack buildCameraTrack(const Script& s,
                                           const TagLookup& tags = nullptr);

// Der kuerzeste Weg von a nach b in Grad, wie CGCam_Pan ihn waehlt.
[[nodiscard]] float shortestAngleDelta(float from, float to) noexcept;

// Der Verlauf, mit dem die Engine dreht und schiebt.
//
// Gemeldet: "wenn er sich laut Skript drehen soll, passiert das bei uns
// instant statt wie in der Engine mit Transition."
//
// Im Quelltext nachgesehen. Q3_Interface.cpp setzt bei rotate:
//
//     ent->s.apos.trType = TR_NONLINEAR_STOP;   (ausser bei alt_fire)
//
// und bg_misc.cpp rechnet dafuer:
//
//     deltaTime = trDuration*0.001f
//               * cos( DEG2RAD( 90 - 90*(atTime-trTime)/trDuration ) )
//
// Mit cos(90-x) = sin(x) und trDelta = Winkel/Dauer bleibt davon:
//
//     Anteil = sin( 90 Grad * Fortschritt )
//
// Also ein Verlauf, der SCHNELL anfaengt und langsam ausklingt - kein
// gleichmaessiger. Bei 0 ergibt er 0, bei 1 genau 1; dazwischen liegt er
// ueber der Geraden.
//
// Nicht zu verwechseln mit einem Ein- UND Ausklingen: der Beginn ist die
// schnellste Stelle.
[[nodiscard]] float nonlinearStop(float frac) noexcept;

}  // namespace bhed
#endif
