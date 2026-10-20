#include "bhed/camtrack.h"

#include "bhed/clock.h"
#include "bhed/regelwerk.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
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

CameraTrack buildCameraTrack(const Script& s, const TagLookup& tags, const RoffLookup& roffs) {
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
    // --- camera ( PATH ) (Paket kamera, rc601) -------------------------------
    //
    // Der Zustand von CGCam_StartRoff/CGCam_Roff: CAMERA_ROFFING, roff_frame,
    // next_roff_time und CAMERA_CUT. Die Bilder werden der Reihe nach
    // angewandt, sobald ihre Zeit erreicht ist - vor jedem Kamerabefehl die,
    // deren Zeit davor liegt, am Ende alle uebrigen. So wirken MOVE, PAN und
    // ENABLE mitten in einer Fahrt wie in der Engine auf client_camera.
    struct RoffLauf {
        std::shared_ptr<const Roff> datei;
        bool an = false;          // CAMERA_ROFFING
        std::size_t bild = 0;     // roff_frame
        double naechste = 0.0;    // next_roff_time
        Path path;                // die PATH-Zeile
        bool schnitt = false;     // CAMERA_CUT
    };
    RoffLauf roffLauf;
    // CGCam_Update - und damit CGCam_Roff - laeuft nur mit in_camera
    // (CG_DrawActiveFrame, cg_view.cpp:2103), also zwischen ENABLE und DISABLE.
    bool inKamera = false;
    double inKameraSeit = 0.0;
    bool folgt = false;   // CAMERA_FOLLOWING: dann laesst CGCam_Roff die Winkel in Ruhe
    // CAMERA_PANNING als eigener Merker (Paket kamera, rc601, Review).
    // `schwenk.an` heisst hier "angles2 laeuft"; CGCam_Roff setzt das in jedem
    // Bild, das Flag selbst aber NIE. Gesetzt von CGCam_StartRoff (:1806 MD,
    // :1821 OpenJK) und von PAN/ROLL mit Dauer (:330/:362 MD, :306/:338
    // OpenJK), geloescht von PAN ohne Dauer (:263 MD, :242 OpenJK), FOLLOW
    // (:522 MD, :494 OpenJK) und am Ende eines Schwenks (:1180 MD, :1224
    // OpenJK). Es entscheidet, ob nach CGCam_StopRoff der letzte
    // Winkelversatz noch laeuft.
    bool panningFlag = false;
    // CAMERA_ACCEL aus einer "fovaccel"-Notiz.
    struct Beschleunigt {
        bool an = false;
        double start = 0.0;
        double ende = 0.0;
        float von = 0.0F;
        float tempo = 0.0F;
        float beschl = 0.0F;
    };
    Beschleunigt fovBeschl;
    // CGCam_Update, CAMERA_ACCEL (cg_camera.cpp:1101-1164 OpenJK, 1053-1117 MD):
    //     t = (cg.time - FOV_time) * 0.001;  x = FOV2 + vel*t + 0.5*acc*t*t;  1..180
    // Nach dem Ende bleibt client_camera.FOV beim zuletzt gerechneten Wert.
    const auto beschlWert = [&](double jetzt) {
        const auto t = static_cast<float>((std::min(jetzt, fovBeschl.ende) - fovBeschl.start) * 0.001);
        const float x = fovBeschl.von + fovBeschl.tempo * t + static_cast<float>(0.5 * fovBeschl.beschl * t * t);
        return std::clamp(x, 1.0F, 180.0F);
    };
    const auto uebernehmen = [&](double jetzt) {
        // Waehrend CAMERA_ROFFING bringt jedes ROFF-Bild selbst Ort und
        // Winkel weiter (origin = origin2, angles += angles2); das naechste
        // Bild kommt genau dann, wenn das vorige endet, und CGCam_Update
        // prueft "move_time + move_duration < cg.time" - echt kleiner.
        if (fahrt.an && jetzt >= fahrt.ende && !roffLauf.an) {
            for (int k = 0; k < 3; ++k) { pos[k] = fahrt.ziel[k]; }
            fahrt.an = false;
        }
        if (schwenk.an && jetzt >= schwenk.ende && !roffLauf.an) {
            for (int k = 0; k < 3; ++k) { ang[k] = normalise360(ang[k] + schwenk.ziel[k]); }
            schwenk.an = false;
            panningFlag = false;   // "finished panning" (Review)
        }
        if (zoom.an && jetzt >= zoom.ende) {
            fov = zoom.ziel[0];
            zoom.an = false;
        }
        if (fovBeschl.an && jetzt >= fovBeschl.ende) {
            fov = beschlWert(jetzt);
            fovBeschl.an = false;
        }
    };
    // CGCam_SetFOV (cg_camera.cpp:353): nur FOV - ein laufender Zoom geht
    // vom neuen Wert aus weiter zu seinem Ziel (wie ZOOM mit Dauer 0 unten).
    const auto fovSetzen = [&](double jetzt, float wert, const Path& p) {
        uebernehmen(jetzt);
        CamSegment z;
        z.kind = CamSegment::Kind::Zoom;
        z.startMs = jetzt;
        z.endMs = jetzt;
        z.path = p;
        fov = wert;
        z.from[0] = wert;
        z.to[0] = wert;
        if (zoom.an && jetzt < zoom.ende) {
            z.to[0] = zoom.ziel[0];
            z.endMs = zoom.ende;
            z.lerpStartMs = zoom.start;
        }
        out.segments.push_back(z);
    };
    // Eine Notiz eines ROFF-Bilds, zerlegt wie CG_RoffNotetrackCallback
    // (cg_camera.cpp:1700-1797) und CGCam_NotetrackProcessFov/-FovZoom/
    // -FovAccel (:1456/:1495/:1594): Typ bis zum ersten Leerzeichen, Zahlen
    // bis zum naechsten Leerraum (atof), dazwischen werden nur Leerzeichen
    // ueberlesen.
    const auto feld = [](const std::string& text, std::size_t& a) {
        std::string t;
        while (a < text.size() && std::isspace(static_cast<unsigned char>(text[a])) == 0 && t.size() < 64U) {
            t += text[a++];
        }
        return t;
    };
    const auto leer = [](const std::string& text, std::size_t& a) {
        while (a < text.size() && text[a] == ' ') { ++a; }
    };
    const auto ziffer = [](const std::string& t) {
        return !t.empty() && std::isdigit(static_cast<unsigned char>(t[0])) != 0;
    };
    std::function<void(const std::string&, double)> roffNotiz = [&](const std::string& notiz, double jetzt) {
        std::size_t i = 0;
        std::string typ;
        while (i < notiz.size() && notiz[i] != ' ') { typ += notiz[i++]; }
        const bool weitere = i < notiz.size() && notiz[i] == ' ';
        const std::string rest = weitere ? notiz.substr(i + 1) : std::string();
        if (typ == "cut") {
            roffLauf.schnitt = true;
            // "this is just a really hacky way of getting a cut and a fov
            // command on the same frame"
            if (weitere) { roffNotiz(rest, jetzt); }
        } else if (typ == "fov") {
            // "fov <new fov>": nur mit einer Ziffer vorn; atof auf den Rest.
            if (weitere && ziffer(rest)) {
                fovSetzen(jetzt, static_cast<float>(std::atof(rest.substr(0, 64).c_str())), roffLauf.path);
            }
        } else if (typ == "fovzoom" && weitere && !rest.empty()) {
            // "fovzoom <begin fov> <end fov> <time>"; keine Zahl vorn heisst:
            // vom jetzigen client_camera.FOV.
            uebernehmen(jetzt);
            std::size_t a = 0;
            const std::string t1 = feld(rest, a);
            const float von = ziffer(t1) ? static_cast<float>(std::atof(t1.c_str()))
                                         : (fovBeschl.an ? beschlWert(jetzt) : fov);
            leer(rest, a);
            if (a >= rest.size()) { return; }   // "missing 'end fov' argument"
            const auto bis = static_cast<float>(std::atof(feld(rest, a).c_str()));
            leer(rest, a);
            if (a >= rest.size()) { return; }   // "missing 'time' argument"
            const double dauer = std::atof(feld(rest, a).c_str());
            // CGCam_Zoom2 (:379): ohne Dauer nur CGCam_SetFOV( FOV2 ).
            if (dauer == 0.0) {
                fovSetzen(jetzt, bis, roffLauf.path);
                return;
            }
            fovBeschl.an = false;
            fov = von;
            zoom.an = true;
            zoom.start = jetzt;
            zoom.ende = jetzt + dauer;
            zoom.ziel[0] = bis;
            CamSegment z;
            z.kind = CamSegment::Kind::Zoom;
            z.startMs = jetzt;
            z.endMs = zoom.ende;
            z.path = roffLauf.path;
            z.from[0] = von;
            z.to[0] = bis;
            out.segments.push_back(z);
        } else if (typ == "fovaccel" && weitere && !rest.empty()) {
            // "fovaccel <begin fov> <fov delta> <fov delta2> <time>":
            // Anfang, Tempo, Beschleunigung, Dauer.
            uebernehmen(jetzt);
            std::size_t a = 0;
            const std::string t1 = feld(rest, a);
            const float von = ziffer(t1) ? static_cast<float>(std::atof(t1.c_str()))
                                         : (fovBeschl.an ? beschlWert(jetzt) : fov);
            leer(rest, a);
            if (a >= rest.size()) { return; }
            const auto tempo = static_cast<float>(std::atof(feld(rest, a).c_str()));
            leer(rest, a);
            if (a >= rest.size()) { return; }
            const auto beschl = static_cast<float>(std::atof(feld(rest, a).c_str()));
            leer(rest, a);
            if (a >= rest.size()) { return; }
            const double dauer = std::atof(feld(rest, a).c_str());
            if (dauer == 0.0) { return; }   // CGCam_ZoomAccel (:395): "if ( !duration ) return;"
            fovBeschl = {true, jetzt, jetzt + dauer, von, tempo, beschl};
            CamSegment z;
            z.kind = CamSegment::Kind::Zoom;
            z.startMs = jetzt;
            z.endMs = fovBeschl.ende;
            z.path = roffLauf.path;
            z.beschleunigt = true;
            z.from[0] = von;
            z.to[0] = beschlWert(fovBeschl.ende);
            z.zoomTempo = tempo;
            z.zoomBeschl = beschl;
            out.segments.push_back(z);
        }
    };
    // Ein ROFF-Bild, genau wie CGCam_Roff (cg_camera.cpp:1864-1946 OpenJK,
    // 1849-1931 Movie Duels), zur Zeit `jetzt` (cg.time).
    const auto roffBild = [&](double jetzt) {
        const Roff& r = *roffLauf.datei;
        const double dauer = static_cast<double>(r.bildMs);   // roff->mFrameTime
        const RoffBild& b = r.bilder[roffLauf.bild];
        float o[3] = {b.ort[0], b.ort[1], b.ort[2]};
        float w[3] = {b.winkel[0], b.winkel[1], b.winkel[2]};
        // rc604 fix604 (JK2 Stufe 3): CGCam_Roff von Jedi Outcast kennt weder
        // Notizen noch CAMERA_CUT (codeJK2/cgame/cg_camera.cpp:1385-1464) - kein
        // "cut", kein "fov"/"fovaccel"/"fovzoom".
        const bool jk2 = regelwerkJediOutcast();
        if (r.fassung == kRoffFassung2 && !jk2) {
            // "since we just hit a new frame, clear our CUT flag"
            roffLauf.schnitt = false;
            // Nur die ERSTE Notiz des Bildes (mNoteTrackIndexes[mStartNote]);
            // zeigt mStartNote ins Leere, liest die Engine fremden Speicher -
            // hier faellt die Notiz weg.
            if (b.ersteNotiz != -1 || b.notizen != 0) {
                if (b.ersteNotiz >= 0 && static_cast<std::size_t>(b.ersteNotiz) < r.notizen.size()) {
                    roffNotiz(r.notizen[static_cast<std::size_t>(b.ersteNotiz)], jetzt);
                }
            }
        }
        // "Yeah, um, I guess this just has to be negated?" - ang[ROLL] = -ang[ROLL];
        w[2] = -w[2];
        // rc604 fix604: Jedi Outcast dreht auch PITCH um (codeJK2/cgame/cg_camera.cpp:1418).
        if (jk2) { w[0] = -w[0]; }
        if (roffLauf.bild > 0) {
            if (!folgt) {
                for (int k = 0; k < 3; ++k) { ang[k] += schwenk.ziel[k]; }   // VectorAdd( angles, angles2, angles )
            }
            for (int k = 0; k < 3; ++k) { pos[k] = fahrt.ziel[k]; }           // VectorCopy( origin2, origin )
        }
        if (!folgt) {
            for (int k = 0; k < 3; ++k) { schwenk.ziel[k] = w[k]; }
            schwenk.an = true;
            schwenk.start = jetzt;
            schwenk.ende = jetzt + dauer;
        }
        for (int k = 0; k < 3; ++k) { fahrt.ziel[k] = pos[k] + o[k]; }       // VectorAdd( origin, org, origin2 )
        fahrt.an = true;
        fahrt.start = jetzt;
        fahrt.ende = jetzt + dauer;
        const bool letztes = roffLauf.bild + 1 >= r.bilder.size();
        CamSegment seg;
        seg.kind = CamSegment::Kind::Pfad;
        seg.startMs = jetzt;
        seg.endMs = jetzt + dauer;
        seg.path = roffLauf.path;
        // Beim letzten Bild loescht CGCam_StopRoff CAMERA_MOVING - der Ort
        // steht ab jetzt auf origin (vieworg = origin), der letzte Versatz
        // wird nie sichtbar. Darum steht dieses Bild still - das zaehlt, wenn
        // mehrere Bilder in EINEM Durchlauf kommen (nach ENABLE).
        seg.pfadWinkel = !folgt;
        seg.schnitt = roffLauf.schnitt;
        // Paket kamera, rc601 (Review): nach CGCam_StopRoff zeigt CGCam_Update
        // den Winkelversatz nur noch im Zweig CAMERA_PANNING; ist das Flag weg
        // (PAN ohne Dauer mitten in der Bahn), gilt sofort refdefViewAngles =
        // angles (:1235-1243 MD, :1276-1283 OpenJK) - der letzte Versatz wird
        // nie angewandt.
        const bool letzterLaeuft = !letztes || panningFlag;
        for (int k = 0; k < 3; ++k) {
            seg.from[k] = pos[k];
            seg.to[k] = letztes ? pos[k] : fahrt.ziel[k];
            seg.vonWinkel[k] = ang[k];
            seg.zuWinkel[k] = letzterLaeuft ? ang[k] + schwenk.ziel[k] : ang[k];
        }
        out.segments.push_back(seg);
        if (letztes) {
            // CGCam_StopRoff: CAMERA_ROFFING und CAMERA_MOVING weg;
            // CAMERA_PANNING bleibt und bringt den letzten Winkelversatz zu
            // Ende - wenn es noch gesetzt ist (Review).
            roffLauf.an = false;
            fahrt.an = false;
            if (!folgt && !panningFlag) { schwenk.an = false; }
            return;
        }
        ++roffLauf.bild;
        roffLauf.naechste += dauer;   // next_roff_time += roff->mFrameTime
    };
    // Alle Bilder, deren Zeit vor `bis` liegt (am Ende: auch genau dann).
    // Ein Kamerabefehl derselben Zeit kommt zuerst - ICARUS laeuft im
    // Spielbild, CGCam_Update erst beim Zeichnen danach. Faellige Bilder, die
    // auf in_camera warten mussten, kommen alle zur Zeit von ENABLE.
    const auto roffBis = [&](double bis, bool mitGleich) {
        while (roffLauf.an && inKamera && roffLauf.datei &&
               (mitGleich ? roffLauf.naechste <= bis : roffLauf.naechste < bis)) {
            roffBild(std::max(roffLauf.naechste, inKameraSeit));
        }
    };
    // Laeuft gerade ein ROFF-Bild, zeigt CGCam_Update die Winkel als
    // angles + angles2 / pan_duration * (cg.time - pan_time) (:1186-1203) -
    // auch nachdem PAN oder ROLL ohne Dauer die Basis verschoben haben.
    const auto roffWinkelWeiter = [&](double jetzt, const Path& p) {
        if (!roffLauf.an || folgt || jetzt < schwenk.start || jetzt >= schwenk.ende) { return; }
        const auto anteil = static_cast<float>((jetzt - schwenk.start) / (schwenk.ende - schwenk.start));
        CamSegment w;
        w.kind = CamSegment::Kind::Pfad;
        w.startMs = jetzt;
        w.endMs = schwenk.ende;
        w.path = p;
        w.pfadOrt = false;
        for (int k = 0; k < 3; ++k) {
            w.vonWinkel[k] = ang[k] + schwenk.ziel[k] * anteil;
            w.zuWinkel[k] = ang[k] + schwenk.ziel[k];
        }
        out.segments.push_back(w);
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
        roffBis(now, false);   // Paket kamera, rc601: faellige ROFF-Bilder zuerst
        uebernehmen(now);

        const std::string& type = n.args[0].text;
        CamSegment seg;
        seg.startMs = now;
        seg.endMs = now;
        seg.path = Path{i};

        // --- camera ( PATH, "datei" ) (Paket kamera, rc601) -----------------
        //
        // CGCam_StartRoff (cg_camera.cpp:1813): FOLLOW und TRACK aus, die
        // Datei laden, roff_frame = 0, next_roff_time = cg.time. Scheitert
        // G_LoadRoff, bewegt sich nichts ("ROFF camera playback failed").
        if (type == "PATH" && n.args.size() >= 2) {
            if (folgt) {
                CamSegment f = seg;
                f.kind = CamSegment::Kind::Follow;   // CGCam_FollowDisable
                out.segments.push_back(f);
            }
            folgt = false;
            std::shared_ptr<const Roff> datei = roffs ? roffs(n.args[1].text) : nullptr;
            if (!datei || datei->bilder.empty()) {
                continue;
            }
            roffLauf.datei = std::move(datei);
            roffLauf.an = true;
            panningFlag = true;   // CGCam_StartRoff: CAMERA_PANNING (Review)
            roffLauf.bild = 0;
            roffLauf.naechste = now;
            roffLauf.path = Path{i};
            roffLauf.schnitt = false;
            continue;
        }

        if (type == "ENABLE") {
            // Paket kamera, rc601: in_camera an, next_roff_time = 0 (:94/:96) -
            // eine Bahn, die vorher begonnen hat, holt ihre Bilder auf einmal nach.
            inKamera = true;
            inKameraSeit = now;
            roffLauf.naechste = 0.0;
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
            inKamera = false;   // Paket kamera, rc601: ohne in_camera kein CGCam_Roff
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
            // Paket kamera, rc601: CGCam_Move beendet eine laufende
            // camera ( PATH )-Fahrt ("info_state &= ~CAMERA_ROFFING",
            // cg_camera.cpp:191).
            roffLauf.an = false;
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
            folgt = false;   // Paket kamera, rc601: CGCam_Pan ruft CGCam_FollowDisable
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
                panningFlag = false;   // Paket kamera, rc601 (Review)
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
                panningFlag = true;   // Paket kamera, rc601 (Review)
            }
            out.segments.push_back(seg);
            if (dur <= 0.0) {
                // Paket kamera, rc601: mitten in einem ROFF-Bild laeuft dessen
                // Winkelversatz von der neuen Basis aus weiter.
                roffWinkelWeiter(now, seg.path);
            }
            continue;
        }

        if (type == "ZOOM" && n.args.size() >= 2) {
            seg.kind = CamSegment::Kind::Zoom;
            // Paket kamera, rc601: ein ZOOM beendet hier ein "fovaccel" einer
            // camera ( PATH )-Notiz und rechnet von dessen jetzigem Wert aus.
            // (In der Engine hat CAMERA_ACCEL Vorrang und rechnet mit dem
            // ueberschriebenen FOV_time/FOV2 weiter - nicht nachgebaut.)
            if (fovBeschl.an) {
                fov = beschlWert(now);
                fovBeschl.an = false;
            }
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
                panningFlag = true;   // Paket kamera, rc601 (Review)
            }
            out.segments.push_back(seg);
            if (dur <= 0.0) {
                roffWinkelWeiter(now, seg.path);   // Paket kamera, rc601 (siehe PAN)
            }
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
                panningFlag = false;   // Paket kamera, rc601 (Review)
            }
            folgt = !g.empty();   // Paket kamera, rc601: CAMERA_FOLLOWING
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
    // Paket kamera, rc601: was von einer camera ( PATH )-Fahrt noch aussteht,
    // laeuft bis zu ihrem Ende - solange die Kamera an ist.
    roffBis(std::numeric_limits<double>::infinity(), true);

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
    bool geschaltet = false;   // Paket kamera, rc601 (Review): ENABLE/DISABLE schon gelaufen
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
                if (seg.beschleunigt) {
                    // Paket kamera, rc601: "fovaccel" (CAMERA_ACCEL,
                    // CGCam_Update): x = x0 + v t + 0.5 a t^2, t in Sekunden,
                    // auf 1..180 begrenzt; danach bleibt der letzte Wert.
                    const auto ts = static_cast<float>((std::min(ms, seg.endMs) - seg.startMs) * 0.001);
                    const float x = seg.from[0] + seg.zoomTempo * ts + static_cast<float>(0.5 * seg.zoomBeschl * ts * ts);
                    st.fovX = std::clamp(x, 1.0F, 180.0F);
                    fovVon = seg.from[0];
                    fovZu = st.fovX;
                    break;
                }
                st.fovX = seg.from[0] + (seg.to[0] - seg.from[0]) * f;
                fovVon = seg.from[0];
                fovZu = seg.to[0];
                break;
            case CamSegment::Kind::Pfad: {
                // Paket kamera, rc601: ein Bild einer camera ( PATH )-Fahrt.
                // CGCam_Update: Ort origin + (origin2 - origin) / move_duration *
                // (cg.time - move_time), Winkel angles + angles2 / pan_duration *
                // (cg.time - pan_time); mit CAMERA_CUT sofort das Ziel.
                const float g = seg.schnitt ? 1.0F : f;
                if (seg.pfadOrt) {
                    for (int k = 0; k < 3; ++k) {
                        st.pos[k] = seg.from[k] + (seg.to[k] - seg.from[k]) * g;
                    }
                }
                if (seg.pfadWinkel) {
                    for (int k = 0; k < 3; ++k) {
                        st.angles[k] = seg.vonWinkel[k] + (seg.zuWinkel[k] - seg.vonWinkel[k]) * g;
                    }
                    st.panning = laeuft && !seg.schnitt;
                }
                break;
            }
            case CamSegment::Kind::Enable:
                st.enabled = true;
                balkenAn = true;
                balkenSeit = seg.startMs;
                geschaltet = true;
                break;
            case CamSegment::Kind::Disable:
                st.enabled = false;
                balkenAn = false;
                balkenSeit = seg.startMs;
                geschaltet = true;
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
    // --- Wessen Blick zeigt das Spiel? (Paket kamera, rc601, Review) --------
    //
    // Gemeldet: "Scripts with no ENABLE at all lose every ZOOM" - sichtFovX
    // gab ohne st.enabled immer cg_fov 80, also auch fuer einen Skriptteil,
    // dessen Kamera ein ANDERES Skript einschaltet (dort kommt kein ENABLE
    // vor). Entscheidung: die 80 gelten nur, wenn die Bahn selbst schaltet -
    // nach DISABLE, und vor dem ersten ENABLE. Steht als erstes ein DISABLE,
    // lief die Kamera davor (ein anderes Skript hat sie eingeschaltet).
    if (geschaltet) {
        st.spielerBlick = !st.enabled;
    } else {
        for (const CamSegment& seg : segments) {
            if (seg.kind == CamSegment::Kind::Enable) {
                st.spielerBlick = true;
                break;
            }
            if (seg.kind == CamSegment::Kind::Disable) { break; }
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

bool kameraMarke(const std::string& text, std::string& name, bool& angles) {
    return istMarke(text, name, angles);
}

// --- Paket kamera, rc601 ------------------------------------------------------

float sichtFovX(const CameraState& st) noexcept {
    // in_camera: CGCam_RenderScene -> CGCam_Update -> CG_CalcFOVFromX( FOV )
    // mit dem Bildwinkel der Kamera. Sonst CG_CalcViewValues -> CG_CalcFov
    // -> fov_x = cg_fov.value (siehe kSpielerFovX). Ob die Kamera an ist,
    // sagt st.enabled (ENABLE setzt in_camera, CGCam_Enable cg_camera.cpp:94;
    // DISABLE loescht es, CGCam_Disable :138).
    //
    // Paket kamera, rc601 (Review): st.spielerBlick statt !st.enabled - ein
    // Skriptteil ohne ENABLE/DISABLE zeigt seinen ZOOM (CameraTrack::at).
    return st.spielerBlick ? kSpielerFovX : st.fovX;
}

CameraTrack buildCameraTrack(const Script& s, const TagLookup& tags) {
    // Ohne Auskunft ueber ROFF-Dateien: camera ( PATH ) bewegt nichts.
    return buildCameraTrack(s, tags, RoffLookup{});
}

double einstellungsZeit(const CameraTrack& t, const Path& movePfad) {
    for (const CamSegment& c : t.segments) {
        if (c.kind == CamSegment::Kind::Move && c.fremd.empty() && c.path == movePfad) {
            return c.endMs;
        }
    }
    return -1.0;
}

}  // namespace bhed
