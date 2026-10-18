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
#include "bhed/roff.h"

#include <functional>
#include <memory>
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

    // Paket kamera, rc601 (Review): zeigt das Spiel hier den Blick des
    // Spielers (cg_fov 80) statt der Skriptkamera? Nur, wenn die Bahn die
    // Kamera selbst schaltet: nach DISABLE, und vor dem ersten ENABLE, wenn
    // das erste Schalten ein ENABLE ist. Ein Skriptteil ohne ENABLE und
    // DISABLE - seine Kamera schaltet ein anderes Skript ein - und die Zeit
    // vor einem ersten DISABLE zeigen die Kamera. Gesetzt von
    // CameraTrack::at; sichtFovX liest es.
    bool spielerBlick = false;
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
        Shake, Follow,
        // Paket kamera, rc601: ein Bild einer camera ( PATH )-Fahrt
        // (CGCam_Roff) - Ort von from nach to, Winkel von vonWinkel nach
        // zuWinkel. Eine eigene Art, damit Zeitleiste, Einstellungsliste und
        // Auto-Key die vielen ROFF-Bilder nicht fuer MOVE-Schluessel halten.
        Pfad };

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
    // --- Paket kamera, rc601: camera ( PATH ) ---------------------------
    // Pfad: die Winkel dieses ROFF-Bilds; ob es Ort und Winkel bewegt (das
    // letzte Bild bewegt den Ort nicht mehr, waehrend FOLLOW bleiben die
    // Winkel); "cut" springt sofort ans Ziel (CAMERA_CUT).
    float vonWinkel[3]{};
    float zuWinkel[3]{};
    bool pfadOrt = true;
    bool pfadWinkel = true;
    bool schnitt = false;
    // Zoom aus einer "fovaccel"-Notiz (CAMERA_ACCEL): from[0] ist der
    // Anfangswert, dazu Tempo (Grad/s) und Beschleunigung (Grad/s^2).
    bool beschleunigt = false;
    float zoomTempo = 0.0F;
    float zoomBeschl = 0.0F;
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

// Steht in einem Argument eine Marke - tag( "cam1b", ORIGIN ) - statt Zahlen?
// Dann Name und ob die Blickrichtung (ANGLES) gemeint ist.
[[nodiscard]] bool kameraMarke(const std::string& text, std::string& name, bool& angles);

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

// --- Paket kamera, rc601 ------------------------------------------------------
//
// Vor camera ( ENABLE ) und nach camera ( DISABLE ) zeigt das Spiel nicht die
// Skriptkamera, sondern den Blick des Spielers: CG_DrawActiveFrame ruft dann
// statt CGCam_RenderScene CG_CalcViewValues (cg_view.cpp:2103-2113 OpenJK,
// :3181-3191 Movie Duels), und das endet in CG_CalcFov mit
// fov_x = cg_fov.value (cg_view.cpp:1399 OpenJK, :1899 MD). Vorgabe von
// cg_fov ist in beiden "80" (cg_main.cpp:354 OpenJK, :554 MD).
inline constexpr float kSpielerFovX = 80.0F;

// Der waagerechte Bildwinkel, den das Spiel zu diesem Kamerastand zeigt:
// kSpielerFovX, wo st.spielerBlick gilt, sonst st.fovX.
[[nodiscard]] float sichtFovX(const CameraState& st) noexcept;

// Wann die Einstellung mit dem MOVE in Zeile `movePfad` steht: das Ende
// seiner Fahrt (startMs + Dauer, bei Dauer 0 der Start) - dieselbe Zeit, auf
// die kameraSchluesselWaehlen die Zeitleiste stellt. -1, wenn die Bahn
// diesen MOVE nicht kennt (Marke fehlt, Bahn noch nicht gebaut).
[[nodiscard]] double einstellungsZeit(const CameraTrack& t, const Path& movePfad);

// --- camera ( PATH, "datei" ): eine Kamerafahrt aus einer ROFF-Datei ----------
//
// CGCam_StartRoff / CGCam_Roff (cg_camera.cpp:1813/1864 OpenJK, 1798/1849
// Movie Duels): je ROFF-Bild ein Versatz im Ort und in den Winkeln, dazu die
// Notizen "cut", "fov", "fovzoom" und "fovaccel" (CG_RoffNotetrackCallback).
// Die Datei kommt von aussen, weil der Kern die Archive nicht kennt:
// scripts/<datei>.rof wie bei PLAY_ROFF (roffPfad). Ohne Auskunft oder ohne
// Datei bewegt PATH die Kamera nicht.
using RoffLookup = std::function<std::shared_ptr<const Roff>(const std::string& name)>;
[[nodiscard]] CameraTrack buildCameraTrack(const Script& s, const TagLookup& tags, const RoffLookup& roffs);

}  // namespace bhed
#endif
