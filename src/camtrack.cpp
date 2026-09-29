#include "bhed/camtrack.h"

#include "bhed/clock.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace bhed {

void aimAngles(const float from[3], const float to[3], float outAngles[3]) {
    const float dx = to[0] - from[0];
    const float dy = to[1] - from[1];
    const float dz = to[2] - from[2];
    const float flach = std::sqrt(dx * dx + dy * dy);
    outAngles[2] = 0.0F;
    if (flach < 1.0e-6F) {
        // Genau darueber oder darunter: der Yaw ist nicht bestimmt, also
        // bleibt er null, und der Pitch nimmt das Vorzeichen. Dieselbe
        // Entscheidung wie in anglesFromBasis.
        outAngles[0] = (dz > 0.0F) ? -90.0F : 90.0F;
        outAngles[1] = 0.0F;
        return;
    }
    outAngles[1] = std::atan2(dy, dx) * 180.0F / 3.14159265F;
    // MINUS, weil forward() z = -sin(pitch) rechnet: nach oben schauen ist
    // ein negativer Pitch. Genau hier laege ein Vorzeichenfehler, und die
    // Kamera schaute konsequent in die falsche Hoehe.
    outAngles[0] = -std::atan2(dz, flach) * 180.0F / 3.14159265F;
}

float panDirectionFor(float deltaDeg) {
    // Auf einen Umlauf zurechtstutzen, VORZEICHEN behalten. std::fmod
    // macht genau das: fmod(400, 360) = 40, fmod(-400, 360) = -40.
    const float d = std::fmod(deltaDeg, 360.0F);
    if (d == 0.0F) {
        return 0.0F;
    }
    const float betrag = std::fabs(d);
    if (betrag < 180.0F) {
        // Der gewollte Weg IST der kuerzere - dann genuegt die Null, und
        // wir lassen die Datei so, wie ein Mensch sie schreiben wuerde.
        return 0.0F;
    }
    // Genau 180 zaehlt mit: bei Gleichstand nimmt die Engine den ANDEREN
    // Zweig ("if (fabs(delta1) < fabs(delta2))" ist dann falsch), also
    // kippt das Vorzeichen. Ohne Angabe waere es Zufall.
    return (d > 0.0F) ? 1.0F : -1.0F;
}
namespace {

float normalise360(float a) {
    a = std::fmod(a, 360.0F);
    if (a < 0.0F) {
        a += 360.0F;
    }
    return a;
}

// Drei Zahlen aus einem Vektorargument.
void readVec(const std::string& text, float out[3]) {
    std::istringstream is(text);
    is >> out[0] >> out[1] >> out[2];
}

// Steht in dem Argument eine MARKE statt Zahlen?
//
//     tag( "cam1b", ORIGIN )
//     tag( "cam1b", ANGLES )
//
// Dann liefert diese Funktion den Namen und ob Ort oder Blickrichtung
// gemeint ist. Sonst false.
//
// Geschrieben wird das in den Quellen unterschiedlich - mit und ohne
// Leerzeichen, mal TAG, mal tag. Deshalb wird zeichenweise gesucht und
// nicht auf eine feste Schreibweise geprueft.
bool istMarke(const std::string& text, std::string& name, bool& angles) {
    std::string k;
    for (char c : text) {
        k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (k.find("tag") == std::string::npos) {
        return false;
    }
    const std::size_t a = text.find('"');
    if (a == std::string::npos) {
        return false;
    }
    const std::size_t b = text.find('"', a + 1);
    if (b == std::string::npos) {
        return false;
    }
    name = text.substr(a + 1, b - a - 1);
    angles = k.find("angles", b) != std::string::npos;
    return !name.empty();
}

// Einen Vektor lesen - aus Zahlen ODER aus einer Marke in der Karte.
// false: eine Marke, die es nicht gibt - dann scheitert der ganze Befehl
// (TaskManager.cpp: GetVector schlaegt fehl, TASK_FAILED), und die Kamera
// tut nichts; ein laufender Uebergang geht weiter.
bool readVecOrTag(const std::string& text, const TagLookup& tags,
                  bool wantAngles, float out[3]) {
    std::string name;
    bool istWinkel = false;
    if (tags && istMarke(text, name, istWinkel)) {
        float o[3] = {0, 0, 0};
        float a[3] = {0, 0, 0};
        if (tags(name, o, a)) {
            // Das Wort in tag(...) entscheidet, nicht der Befehl: tag("x", ORIGIN)
            // in einem PAN liefert im Spiel den ORT (TaskManager GetVector).
            (void)wantAngles;
            const float* q = istWinkel ? a : o;
            for (int k = 0; k < 3; ++k) {
                out[k] = q[k];
            }
            return true;
        }
        // Nicht gefunden: die Zahlen unten wuerden 0 0 0 ergeben, und die
        // Kamera stuende im Boden ("sie ist immer im Boden").
        return false;
    }
    readVec(text, out);
    return true;
}

float readNum(const std::string& text, float fallback) {
    try {
        return std::stof(text);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

float nonlinearStop(float frac) noexcept {
    if (frac <= 0.0F) {
        return 0.0F;
    }
    if (frac >= 1.0F) {
        return 1.0F;
    }
    // sin(90 Grad * frac) - siehe die Herleitung im Kopf.
    return std::sin(frac * std::numbers::pi_v<float> * 0.5F);
}

namespace {
// AngleNormalize180 der Engine: erst auf 0..360, dann alles ueber 180
// abziehen - 180 selbst bleibt +180.
float normalise180(float a) {
    a = normalise360(a);
    return (a > 180.0F) ? a - 360.0F : a;
}

// CGCam_Pan, je Achse: ohne Richtung der kuerzere Weg, sonst der in die
// angegebene Richtung (negativ: abwaerts zaehlen, positiv: aufwaerts).
float panDelta(float from, float dest, float richtung) {
    const float d1 = normalise360(dest) - normalise360(from);
    const float d2 = (d1 < 0.0F) ? d1 + 360.0F : d1 - 360.0F;
    if (richtung == 0.0F) {
        return (std::fabs(d1) < std::fabs(d2)) ? d1 : d2;
    }
    if (richtung < 0.0F) {
        return (d1 < 0.0F) ? d1 : (d1 > 0.0F) ? d2 : 0.0F;
    }
    return (d1 > 0.0F) ? d1 : (d1 < 0.0F) ? d2 : 0.0F;
}

// Vier Zahlen einer Farbe: "<r g b>" und ein Alphawert.
void readFarbe(const std::string& rgb, const std::string& alpha, float out[4]) {
    std::istringstream is(rgb);
    is >> out[0] >> out[1] >> out[2];
    try {
        out[3] = std::stof(alpha);
    } catch (...) {
        out[3] = 0.0F;
    }
}
}  // namespace

float shortestAngleDelta(float from, float to) noexcept {
    // Genau CGCam_Pan: zwei Deltas, das betragsmaessig kleinere gewinnt.
    const float d1 = normalise360(to) - normalise360(from);
    const float d2 = (d1 < 0.0F) ? d1 + 360.0F : d1 - 360.0F;
    return (std::fabs(d1) < std::fabs(d2)) ? d1 : d2;
}

CameraTrack buildCameraTrack(const Script& s, const TagLookup& tags) {
    const SignalTimes signals = collectSignals(s);
    CameraTrack out;

    // --- Der Zustand wie in client_camera -----------------------------------
    //
    // Die Engine UEBERNIMMT Ort, Winkel und Bildwinkel erst, wenn ein
    // Uebergang FERTIG ist (cg_camera.cpp, CGCam_Update: "does not actually
    // move the camera until the movement time is done!", "does not store the
    // resultant angle in client_camera.angles until pan is done"). Ein neuer
    // MOVE, PAN oder ZOOM rechnet vom UEBERNOMMENEN Wert aus - unterbricht er
    // einen laufenden, springt das Bild zurueck auf dessen Anfang.
    //
    // Vorher rechnete behaved vom ZIEL des vorigen Befehls aus, als waere er
    // schon fertig. Bei einer Unterbrechung zeigte die Vorschau damit einen
    // Sprung nach vorn (oder keinen), wo das Spiel zurueckspringt.
    //
    // Die Grenze: kommt der neue Befehl GENAU zum Ende des alten (MOVE 2000;
    // wait 2000; MOVE), gilt der alte als uebernommen. Die Engine prueft
    // "cg.time > Ende" im Bildtakt; ob das vor dem naechsten Befehl schon
    // geschehen ist, haengt am Takt und laesst sich aus dem Quelltext nicht
    // entscheiden. Der Normalfall - fertig, dann weiter - bleibt so ohne Sprung.
    double now = 0.0;
    float pos[3] = {0, 0, 0};
    float ang[3] = {0, 0, 0};
    float fov = 90.0F;
    struct Laufend {
        bool an = false;
        double start = 0.0;
        double ende = 0.0;
        float ziel[3]{};
    };
    Laufend fahrt;      // CAMERA_MOVING: ziel = origin2
    Laufend schwenk;    // CAMERA_PANNING: ziel = angles2 (die DELTAS)
    Laufend zoom;       // CAMERA_ZOOMING: ziel[0] = FOV2
    const auto uebernehmen = [&](double jetzt) {
        if (fahrt.an && jetzt >= fahrt.ende) {
            for (int k = 0; k < 3; ++k) { pos[k] = fahrt.ziel[k]; }
            fahrt.an = false;
        }
        if (schwenk.an && jetzt >= schwenk.ende) {
            for (int k = 0; k < 3; ++k) { ang[k] = normalise360(ang[k] + schwenk.ziel[k]); }
            schwenk.an = false;
        }
        if (zoom.an && jetzt >= zoom.ende) {
            fov = zoom.ziel[0];
            zoom.an = false;
        }
    };

    for (std::size_t i = 0; i < s.nodes.size(); ++i) {
        const Node& n = s.nodes[i];
        if (n.kind != Node::Kind::Command) {
            continue;
        }

        // Die Uhr laeuft ueber bhed/clock.h - dieselbe wie in timeline.cpp
        // und scene.cpp.
        //
        // Vorher stand hier eine eigene Rechnung, die nur wait mit Zahl
        // kannte: $random$ ergab 0, waitsignal wurde uebergangen. Damit
        // lief die Kamera in zehn der 284 Skripte vor den Figuren her.
        if (n.name == "wait" || n.name == "waitsignal") {
            now = advance(n, now, now, signals);
            continue;
        }
        if (n.name != "camera" || n.args.empty()) {
            continue;
        }
        uebernehmen(now);

        const std::string& type = n.args[0].text;
        CamSegment seg;
        seg.startMs = now;
        seg.endMs = now;
        seg.path = Path{i};

        if (type == "ENABLE") {
            seg.kind = CamSegment::Kind::Enable;
            out.segments.push_back(seg);
            // CGCam_Enable setzt FOV und FOV2 auf CAMERA_DEFAULT_FOV. Ein
            // ZOOM von vorher gilt danach nicht mehr.
            fov = 90.0F;
            zoom.an = false;
            CamSegment z = seg;
            z.kind = CamSegment::Kind::Zoom;
            z.from[0] = 90.0F;
            z.to[0] = 90.0F;
            out.segments.push_back(z);
            continue;
        }
        if (type == "DISABLE") {
            seg.kind = CamSegment::Kind::Disable;
            out.segments.push_back(seg);
            continue;
        }

        if (type == "MOVE" && n.args.size() >= 2) {
            seg.kind = CamSegment::Kind::Move;
            // Fehlt eine Marke, scheitert der Befehl, und die Kamera bleibt,
            // wo sie war, statt an den Nullpunkt zu springen ("sie ist immer
            // im Boden").
            float dest[3] = {pos[0], pos[1], pos[2]};
            if (!readVecOrTag(n.args[1].text, tags, false, dest)) {
                continue;   // Marke fehlt: der Befehl scheitert im Spiel ganz
            }
            const double dur = (n.args.size() >= 3) ? readMs(n.args.back().text) : 0.0;
            if (dur <= 0.0) {
                // CGCam_Move ohne Dauer: CAMERA_MOVING weg, sofort dorthin.
                fahrt.an = false;
                for (int k = 0; k < 3; ++k) {
                    pos[k] = dest[k];
                    seg.from[k] = dest[k];
                    seg.to[k] = dest[k];
                }
            } else {
                // Vom UEBERNOMMENEN Ort aus (origin), nicht vom Ziel eines
                // noch laufenden MOVE.
                for (int k = 0; k < 3; ++k) {
                    seg.from[k] = pos[k];
                    seg.to[k] = dest[k];
                    fahrt.ziel[k] = dest[k];
                }
                fahrt.an = true;
                fahrt.start = now;
                fahrt.ende = now + dur;
                seg.endMs = fahrt.ende;
            }
            out.segments.push_back(seg);
            continue;
        }

        if (type == "PAN" && n.args.size() >= 2) {
            seg.kind = CamSegment::Kind::Pan;
            float dest[3] = {ang[0], ang[1], ang[2]};
            if (!readVecOrTag(n.args[1].text, tags, true, dest)) {
                continue;   // Marke fehlt: der Befehl scheitert im Spiel ganz
            }
            // camera ( PAN, <ziel>, <richtung>, <dauer> ): die Richtung je
            // Achse, 0 = der kuerzere Weg (CGCam_Pan). Vorher wurde sie nie
            // gelesen - ein Schwenk "andersherum" drehte in der Vorschau den
            // kurzen Weg.
            float richtung[3] = {0, 0, 0};
            if (n.args.size() >= 4) {
                readVec(n.args[2].text, richtung);
            }
            const double dur = (n.args.size() >= 4) ? readMs(n.args.back().text)
                               : (n.args.size() == 3) ? readMs(n.args[2].text) : 0.0;
            if (dur <= 0.0) {
                // CGCam_SetAngles: sofort, CAMERA_PANNING weg.
                schwenk.an = false;
                for (int k = 0; k < 3; ++k) {
                    ang[k] = dest[k];
                    seg.from[k] = dest[k];
                    seg.to[k] = dest[k];
                }
            } else {
                // Vom UEBERNOMMENEN Blick aus; ein laufender Schwenk ist
                // damit verworfen (seine Deltas werden ueberschrieben).
                for (int k = 0; k < 3; ++k) {
                    schwenk.ziel[k] = panDelta(ang[k], dest[k], richtung[k]);
                    seg.from[k] = ang[k];
                    seg.to[k] = ang[k] + schwenk.ziel[k];
                }
                schwenk.an = true;
                schwenk.start = now;
                schwenk.ende = now + dur;
                seg.endMs = schwenk.ende;
            }
            out.segments.push_back(seg);
            continue;
        }

        if (type == "ZOOM" && n.args.size() >= 2) {
            seg.kind = CamSegment::Kind::Zoom;
            const float ziel = readNum(n.args[1].text, fov);
            const double dur = (n.args.size() >= 3) ? readMs(n.args.back().text) : 0.0;
            if (dur <= 0.0) {
                // CGCam_SetFOV setzt nur FOV - CAMERA_ZOOMING bleibt. Laeuft
                // ein Zoom, geht er vom NEUEN Wert aus weiter zu seinem Ziel,
                // mit seiner alten Zeit.
                fov = ziel;
                if (zoom.an && now < zoom.ende) {
                    seg.from[0] = ziel;
                    seg.to[0] = zoom.ziel[0];
                    seg.endMs = zoom.ende;
                    seg.lerpStartMs = zoom.start;
                } else {
                    seg.from[0] = ziel;
                    seg.to[0] = ziel;
                }
            } else {
                // Vom UEBERNOMMENEN Bildwinkel aus (FOV bleibt der Anfang).
                seg.from[0] = fov;
                seg.to[0] = ziel;
                zoom.an = true;
                zoom.start = now;
                zoom.ende = now + dur;
                zoom.ziel[0] = ziel;
                seg.endMs = zoom.ende;
            }
            out.segments.push_back(seg);
            continue;
        }

        // --- ROLL ----------------------------------------------------
        //
        // CGCam_Roll mit Dauer (cg_camera.cpp):
        //
        //     client_camera.info_state |= CAMERA_PANNING;
        //     VectorCopy(client_camera.angles, client_camera.angles2);
        //     client_camera.angles2[2] = AngleDelta(dest, client_camera.angles[2]);
        //
        // angles2 sind DELTAS - kopiert werden aber die absoluten Winkel.
        // Nicken und Gieren laufen damit waehrend des Rollens auf das
        // DOPPELTE und bleiben dort (ein Fehler der Engine, schon bei Raven;
        // "FIXME/NOTE: this will override current panning!!!"). Die Vorschau
        // zeigt, was das Spiel zeigt - auch das. Ohne Dauer setzt ROLL nur den
        // Rollwinkel (CGCam_SetRoll).
        if (type == "ROLL" && n.args.size() >= 2) {
            seg.kind = CamSegment::Kind::Roll;
            const float dest = readNum(n.args[1].text, ang[2]);
            const double dur = (n.args.size() >= 3) ? readMs(n.args.back().text) : 0.0;
            if (dur <= 0.0) {
                ang[2] = dest;
                seg.from[0] = dest;
                seg.to[0] = dest;
            } else {
                schwenk.ziel[0] = ang[0];
                schwenk.ziel[1] = ang[1];
                schwenk.ziel[2] = normalise180(dest - ang[2]);
                for (int k = 0; k < 3; ++k) {
                    seg.from[k] = ang[k];
                    seg.to[k] = ang[k] + schwenk.ziel[k];
                }
                seg.alleAchsen = true;
                schwenk.an = true;
                schwenk.start = now;
                schwenk.ende = now + dur;
                seg.endMs = schwenk.ende;
            }
            out.segments.push_back(seg);
            continue;
        }

        // --- SHAKE ---------------------------------------------------
        //
        // camera ( SHAKE, staerke, dauer ). Die Staerke geht ungeskaliert
        // durch: Q3_Interface.cpp reicht sie unveraendert an CGCam_Shake
        // weiter, und dort wird nur bei MAX_SHAKE_INTENSITY = 16 gedeckelt
        // (cg_camera.h). Ein neues SHAKE ersetzt das laufende.
        if (type == "SHAKE" && n.args.size() >= 2) {
            seg.kind = CamSegment::Kind::Shake;
            seg.to[0] = std::min(readNum(n.args[1].text, 0.0F), 16.0F);
            const double dur = (n.args.size() >= 3) ? readMs(n.args.back().text) : 0.0;
            seg.endMs = now + dur;
            out.segments.push_back(seg);
            continue;
        }

        // --- FOLLOW --------------------------------------------------
        //
        // camera ( FOLLOW, "gruppe", tempo, anfangsblende ). "none", "NULL"
        // und der leere Name schalten das Zielen ab - so steht es in
        // CGCam_Follow, dreimal hintereinander abgefragt. Tempo 0 heisst 100.
        // FOLLOW beendet einen laufenden Schwenk, OHNE ihn zu uebernehmen.
        if (type == "FOLLOW" && n.args.size() >= 2) {
            seg.kind = CamSegment::Kind::Follow;
            std::string g = n.args[1].text;
            std::string low = g;
            for (char& c : low) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (low == "none" || low == "null") {
                g.clear();
            }
            seg.group = g;
            if (!g.empty()) {
                schwenk.an = false;
            }
            const float tempo = (n.args.size() >= 3) ? readNum(n.args[2].text, 0.0F) : 0.0F;
            seg.speed = (tempo != 0.0F) ? tempo : 100.0F;
            seg.initLerp = (n.args.size() >= 4) && readNum(n.args[3].text, 0.0F) != 0.0F;
            seg.endMs = now;   // wirkt ab jetzt, bis PAN oder ein neues FOLLOW kommt
            out.segments.push_back(seg);
            continue;
        }

        // --- FADE ----------------------------------------------------
        //
        // camera ( FADE, <r g b>, a, <r g b>, a, dauer ) - TaskManager.cpp
        // liest Vektor, Zahl, Vektor, Zahl, Zahl. Ohne Dauer gilt sofort die
        // Zielfarbe (CGCam_SetFade).
        if (type == "FADE") {
            seg.kind = CamSegment::Kind::Fade;
            if (n.args.size() >= 5) {
                readFarbe(n.args[1].text, n.args[2].text, seg.farbeVon);
                readFarbe(n.args[3].text, n.args[4].text, seg.farbeZu);
            }
            const double dur = (n.args.size() >= 6) ? readMs(n.args.back().text) : 0.0;
            seg.endMs = now + dur;
            out.segments.push_back(seg);
            continue;
        }
    }

    for (const CamSegment& seg : out.segments) {
        out.durationMs = std::max(out.durationMs, seg.endMs);
    }
    return out;
}

CameraState CameraTrack::at(double ms) const {
    CameraState st;
    // Der Bildwinkel, von dem und zu dem der letzte Zoom laeuft - das Zittern
    // rechnet mit ihrem Mittel ((FOV + FOV2) / 2, CGCam_UpdateShake).
    float fovVon = 90.0F;
    float fovZu = 90.0F;
    const CamSegment* zittern = nullptr;
    double balkenSeit = -1.0;
    bool balkenAn = false;
    for (const CamSegment& seg : segments) {
        if (seg.startMs > ms) {
            break;   // die Abschnitte stehen in Reihenfolge
        }
        // Wie weit ist dieser Abschnitt fortgeschritten?
        //
        // Dauer 0 heisst SPRUNG: CGCam_Move setzt bei !duration direkt die
        // Position. Ein t von 1 ist also richtig, nicht eine Division durch
        // null.
        const double von = (seg.lerpStartMs >= 0.0) ? seg.lerpStartMs : seg.startMs;
        const double span = seg.endMs - von;
        const double t = (span <= 0.0)
                             ? 1.0
                             : std::clamp((ms - von) / span, 0.0, 1.0);
        const auto f = static_cast<float>(t);
        const bool laeuft = t < 1.0;

        switch (seg.kind) {
            case CamSegment::Kind::Move:
                for (int k = 0; k < 3; ++k) {
                    st.pos[k] = seg.from[k] + (seg.to[k] - seg.from[k]) * f;
                }
                break;
            case CamSegment::Kind::Pan:
                for (int k = 0; k < 3; ++k) {
                    st.angles[k] = seg.from[k] + (seg.to[k] - seg.from[k]) * f;
                }
                st.panning = laeuft;
                // CGCam_Pan ruft CGCam_FollowDisable.
                st.followGroup.clear();
                break;
            case CamSegment::Kind::Roll:
                if (seg.alleAchsen) {
                    for (int k = 0; k < 3; ++k) {
                        st.angles[k] = seg.from[k] + (seg.to[k] - seg.from[k]) * f;
                    }
                    st.panning = laeuft;
                } else {
                    st.angles[2] = seg.to[0];
                }
                break;
            case CamSegment::Kind::Zoom:
                st.fovX = seg.from[0] + (seg.to[0] - seg.from[0]) * f;
                fovVon = seg.from[0];
                fovZu = seg.to[0];
                break;
            case CamSegment::Kind::Enable:
                st.enabled = true;
                balkenAn = true;
                balkenSeit = seg.startMs;
                break;
            case CamSegment::Kind::Disable:
                st.enabled = false;
                balkenAn = false;
                balkenSeit = seg.startMs;
                break;
            case CamSegment::Kind::Fade:
                for (int k = 0; k < 4; ++k) {
                    st.fadeColor[k] = seg.farbeVon[k] + (seg.farbeZu[k] - seg.farbeVon[k]) * f;
                }
                break;

            case CamSegment::Kind::Follow:
                // Gilt ab jetzt, bis PAN oder ein anderes FOLLOW kommt. Der
                // leere Name schaltet es ab. FOLLOW beendet einen Schwenk.
                st.followGroup = seg.group;
                st.followSinceMs = seg.startMs;
                st.followSpeed = seg.speed;
                st.followInitLerp = seg.initLerp;
                if (!seg.group.empty()) {
                    st.panning = false;
                }
                break;

            case CamSegment::Kind::Shake:
                // Nur das LETZTE zaehlt - CGCam_Shake ueberschreibt.
                zittern = laeuft ? &seg : nullptr;
                break;
        }
    }
    // --- Die Balken (CGCam_UpdateBarFade, BAR_DURATION 1000 ms) -----------
    if (balkenSeit >= 0.0) {
        const float b = static_cast<float>(std::clamp((ms - balkenSeit) / 1000.0, 0.0, 1.0));
        st.bars = balkenAn ? b : 1.0F - b;
    }
    // --- SHAKE ------------------------------------------------------------
    //
    // cg_camera.cpp, CGCam_UpdateShake():
    //
    //   intensity_scale = 1 - (t / duration) * ((FOV + FOV2) / 2 / 90)
    //   intensity       = shake_intensity * intensity_scale
    //   origin += flrand(-1,1) * intensity   (alle drei Achsen)
    //   angles += flrand(-1,1) * intensity   (nur i < 2, kein ROLL)
    //
    // Nicht gedeckelt: ueber 90 Grad Bildwinkel wird der Faktor zum Ende hin
    // negativ, und das Zittern waechst wieder - wie im Spiel.
    //
    // Statt echtem Zufall ein Wert, der nur an der ZEIT haengt. Eine Vorschau
    // muss beim Zurueckspulen dasselbe zeigen - sonst weiss man nie, ob eine
    // Aenderung von einem selbst kommt oder vom Wuerfel. Dieselbe Ueberlegung
    // wie bei $random$ in der Zeitleiste und bei den Partikeln.
    if (zittern != nullptr) {
        const double span = zittern->endMs - zittern->startMs;
        const float anteil = (span <= 0.0) ? 1.0F : static_cast<float>((ms - zittern->startMs) / span);
        const float scale = 1.0F - anteil * ((fovVon + fovZu) * 0.5F / 90.0F);
        const float amp = zittern->to[0] * scale;
        // Ein billiger, aber ausreichend wirrer Wert aus der Zeit.
        // Millisekunden mal Primzahl, dann die oberen Bits nehmen -
        // benachbarte Zeitpunkte ergeben dadurch weit auseinander
        // liegende Werte, und genau das soll ein Zittern sein.
        auto wobble = [ms](int axis) {
            auto h = static_cast<std::uint32_t>(
                static_cast<std::int64_t>(ms * 3.0) +
                static_cast<std::int64_t>(axis) * 7919);
            h *= 2654435761U;
            h ^= h >> 15U;
            h *= 2246822519U;
            h ^= h >> 13U;
            return (static_cast<float>(h >> 8U) / 8388608.0F) - 1.0F;
        };
        for (int k = 0; k < 3; ++k) {
            st.pos[k] += wobble(k) * amp;
        }
        for (int k = 0; k < 2; ++k) {   // kein ROLL, wie in der Engine
            st.shakeAngles[k] = wobble(k + 3) * amp;
            st.angles[k] += st.shakeAngles[k];
        }
    }
    for (float& a : st.angles) {
        a = normalise360(a);
        // Als Vorzeichenwinkel zurueckgeben: -30 liest sich besser als 330,
        // und der Zeichner rechnet ohnehin mit Sinus und Kosinus.
        if (a > 180.0F) {
            a -= 360.0F;
        }
    }
    return st;
}

const CamSegment* CameraTrack::activeAt(double ms) const {
    // Ein LAUFENDER Abschnitt schlaegt einen abgeschlossenen; unter mehreren
    // gleicher Art gewinnt der SPAETERE.
    //
    // Die alte Fassung schrieb:
    //
    //     if (ms <= seg.endMs)      { best = &seg; }
    //     else if (best == nullptr) { best = &seg; }
    //
    // Beide Zweige tun dasselbe - clang-tidy hat es als
    // bugprone-branch-clone gemeldet, und dahinter steckte ein echter
    // Fehler: sobald best einmal gesetzt war, konnte ein spaeterer
    // ABGESCHLOSSENER Abschnitt ihn nicht mehr ersetzen. Bei einem Skript
    // mit Kamerabefehlen bei 0, 1000 und 2000 lieferte activeAt(5000) also
    // den Befehl bei 0 - den ersten statt des letzten. Im Baum wurde damit
    // die falsche Zeile hervorgehoben.
    const CamSegment* laufend = nullptr;
    const CamSegment* letzterFertig = nullptr;
    for (const CamSegment& seg : segments) {
        if (seg.startMs > ms) {
            break;   // die Abschnitte stehen in Reihenfolge
        }
        if (ms <= seg.endMs) {
            laufend = &seg;
        } else {
            letzterFertig = &seg;
        }
    }
    return (laufend != nullptr) ? laufend : letzterFertig;
}

}  // namespace bhed
