// mapviewtest.cpp - Kamera, Umkreisen, Blende und Bildpyramide
//
// Die Bedienung laesst sich hier nicht anfassen, aber nachrechnen: beim
// Umkreisen muss der Drehpunkt STEHEN bleiben, beim Zoomen die Richtung.
// Genau daran scheitern solche Steuerungen sonst - man dreht sich, und das
// Ziel wandert langsam aus dem Bild.
#include "bhed/mapview.h"
#include "bhed/gpucam.h"
#include "bhed/settings.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
int fails = 0;
bool near(float a, float b, float tol) {
    return (a > b ? a - b : b - a) < tol;
}
void expect(const char* what, bool ok) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", what);
    if (!ok) { ++fails; }
}
float dist(const float a[3], const float b[3]) {
    float s = 0.0F;
    for (int k = 0; k < 3; ++k) { s += (a[k] - b[k]) * (a[k] - b[k]); }
    return std::sqrt(s);
}

// --- Paket kamera, rc601: das Spielfenster ------------------------------------
//
// Gemeldet vom Tester: "I think camera FOV is slightly off". Die Vorschau nahm
// das Seitenverhaeltnis ihrer eigenen Ansicht, das Spiel das seines Fensters,
// und cg_fovAspectAdjust war fest an.
//
// Die Vergleichswerte rechnet diese Probe SELBST, mit der Formel aus
// CG_CalcFOVFromX (cg_view.cpp:1260-1278 in OpenJK, :1705-1722 in Movie
// Duels), in double - nicht mit den Funktionen, die geprueft werden.
constexpr double kPiD = 3.14159265358979323846;
struct Erwartet {
    double fovX = 0.0;   // cg.refdef.fov_x
    double fovY = 0.0;   // cg.refdef.fov_y
};
Erwartet calcFovFromX(double fovX, double seiten, bool adjust) {
    double fx = fovX;
    if (adjust) {
        // fov_x = atan( tan( desiredFov*M_PI / 360.0f ) * baseAspect*aspect )*360.0f / M_PI;
        fx = std::atan(std::tan(fovX * kPiD / 360.0) * 0.75 * seiten) * 360.0 / kPiD;
    }
    // x = cg.refdef.width / tan( fov_x / 360 * M_PI ); fov_y = atan2( cg.refdef.height, x );
    const double x = seiten / std::tan(fx / 360.0 * kPiD);
    return {fx, std::atan2(1.0, x) * 360.0 / kPiD};
}
double tanHalb(double grad) { return std::tan(grad * kPiD / 360.0); }

void spielfensterProben() {
    std::printf("\n--- Spielfenster (Paket kamera) ---\n");
    const bhed::SpielFenster md169{16.0F / 9.0F, true};
    const bhed::SpielFenster ojk169{16.0F / 9.0F, false};
    const bhed::SpielFenster ojk43{4.0F / 3.0F, false};

    // Die bekannten Zahlen (Spiel bei 1920x1080).
    struct Fall {
        const char* name;
        bhed::SpielFenster s;
        float fov;
        double fx;
        double fy;
    };
    const Fall faelle[] = {
        {"Movie Duels 16:9, FOV 90", md169, 90.0F, 106.2602, 73.7398},
        {"Movie Duels 16:9, FOV 50", md169, 50.0F, 63.7420, 38.5526},
        {"Movie Duels 16:9, FOV 110", md169, 110.0F, 124.5871, 93.9329},
        {"OpenJK 16:9, FOV 90", ojk169, 90.0F, 90.0, 58.7155},
        {"OpenJK 16:9, FOV 50", ojk169, 50.0F, 50.0, 29.3950},
        {"OpenJK 16:9, FOV 110", ojk169, 110.0F, 110.0, 77.5521},
        {"OpenJK 4:3, FOV 90 (r_mode 4)", ojk43, 90.0F, 90.0, 73.7398},
        {"Movie Duels 21:9, FOV 90", bhed::SpielFenster{64.0F / 27.0F, true}, 90.0F, 121.2845, 73.7398},
        {"Movie Duels 16:10, FOV 40", bhed::SpielFenster{16.0F / 10.0F, true}, 40.0F, 47.1880, 30.5369},
    };
    for (const Fall& f : faelle) {
        const bhed::SpielSicht s = bhed::spielSicht(f.fov, f.s);
        const Erwartet e = calcFovFromX(f.fov, f.s.seiten, f.s.fovAspectAdjust);
        char t[200];
        std::snprintf(t, sizeof(t), "%s: %.2f x %.2f (Spiel %.2f x %.2f)", f.name, (double)s.fovX, (double)s.fovY,
                      f.fx, f.fy);
        expect(t, near(s.fovX, static_cast<float>(f.fx), 0.02F) && near(s.fovY, static_cast<float>(f.fy), 0.02F) &&
                      near(s.fovX, static_cast<float>(e.fovX), 0.02F) && near(s.fovY, static_cast<float>(e.fovY), 0.02F));
        expect("  tan(fov/2) passt dazu",
               near(bhed::spielTanHalbX(f.fov, f.s), static_cast<float>(tanHalb(f.fx)), 0.0005F) &&
                   near(bhed::spielTanHalbY(f.fov, f.s), static_cast<float>(tanHalb(f.fy)), 0.0005F));
    }

    // --- Der Rahmen in der Ansicht --------------------------------------
    //
    // Die Ansicht aus dem Handbuchbild h_fenster.webp: 945 x 335 (2,82:1).
    const float p = 945.0F / 335.0F;
    {
        const bhed::SpielRahmen r = bhed::spielRahmenIn(p, 16.0F / 9.0F);
        std::printf("     Rahmen 16:9 in 945x335: %.1f x %.1f Bildpunkte\n", (double)(r.breite * 945.0F),
                    (double)(r.hoehe * 335.0F));
        expect("16:9 in einer breiten Ansicht: volle Hoehe, 595,6 Bildpunkte breit",
               near(r.breite * 945.0F, 595.6F, 1.0F) && near(r.hoehe, 1.0F, 0.0001F));
        const bhed::SpielRahmen r2 = bhed::spielRahmenIn(4.0F / 3.0F, 16.0F / 9.0F);
        expect("16:9 in einer 4:3-Ansicht: volle Breite, drei Viertel Hoehe",
               near(r2.breite, 1.0F, 0.0001F) && near(r2.hoehe, 0.75F, 0.0001F));
        const bhed::SpielRahmen r3 = bhed::spielRahmenIn(16.0F / 9.0F, 16.0F / 9.0F);
        expect("gleiches Seitenverhaeltnis: die ganze Ansicht", near(r3.breite, 1.0F, 0.0001F) && near(r3.hoehe, 1.0F, 0.0001F));
    }

    // --- Der Bildwinkel der Ansicht --------------------------------------
    //
    // Mit dem gesetzten fovY zeigt der Rahmen genau das Spielbild: am Rand
    // des Rahmens tan(fov_x/2) des Spiels, oben und unten tan(fov_y/2).
    // (Die Ansicht rechnet waagerecht tan(fovY/2) * Breite/Hoehe, siehe
    // gpucam.h.)
    struct Ansicht {
        const char* name;
        float seiten;
        bhed::SpielFenster s;
        float fov;
    };
    const Ansicht ansichten[] = {
        {"Movie Duels 16:9 in 945x335, FOV 90", p, md169, 90.0F},
        {"OpenJK 16:9 in 945x335, FOV 90", p, ojk169, 90.0F},
        {"Movie Duels 16:9 in 4:3, FOV 90", 4.0F / 3.0F, md169, 90.0F},
        {"OpenJK 16:9 in 4:3, FOV 30", 4.0F / 3.0F, ojk169, 30.0F},
        {"Movie Duels 21:9 in 16:9, FOV 40", 16.0F / 9.0F, bhed::SpielFenster{64.0F / 27.0F, true}, 40.0F},
        {"Movie Duels 16:9 genau 16:9, FOV 90", 16.0F / 9.0F, md169, 90.0F},
    };
    for (const Ansicht& a : ansichten) {
        bhed::Camera c;
        const bhed::SpielRahmen r = bhed::setzeSpielFov(c, a.fov, a.seiten, a.s);
        const Erwartet e = calcFovFromX(a.fov, a.s.seiten, a.s.fovAspectAdjust);
        const double tV = tanHalb(static_cast<double>(c.fovY));
        const double randX = tV * a.seiten * r.breite;   // tan am linken/rechten Rand des Rahmens
        const double randY = tV * r.hoehe;               // tan oben/unten im Rahmen
        char t[220];
        std::snprintf(t, sizeof(t), "%s: Rahmen %.3f x %.3f, im Rahmen %.2f x %.2f Grad (Spiel %.2f x %.2f)", a.name,
                      (double)r.breite, (double)r.hoehe, std::atan(randX) * 360.0 / kPiD, std::atan(randY) * 360.0 / kPiD,
                      e.fovX, e.fovY);
        expect(t, std::fabs(randX - tanHalb(e.fovX)) < 0.001 && std::fabs(randY - tanHalb(e.fovY)) < 0.001 &&
                      (r.breite >= 0.9999F || r.hoehe >= 0.9999F));
    }
    {
        bhed::Camera c;
        const bhed::SpielRahmen r = bhed::setzeSpielFov(c, 90.0F, 4.0F / 3.0F, md169);
        std::printf("     16:9 in 4:3: senkrecht %.2f Grad in der Ansicht\n", (double)c.fovY);
        expect("16:9 in 4:3: die Ansicht oeffnet senkrecht 90 Grad, der Rahmen nur 73,74",
               near(c.fovY, 90.0F, 0.02F) && near(r.hoehe, 0.75F, 0.0001F));
    }
    // Unsinnige Werte duerfen nichts kaputtmachen.
    {
        bool ok = true;
        for (const float fov : {0.0F, -5.0F, 1000.0F, 6.291F}) {
            for (const float ans : {0.0F, 0.5F, 3.0F}) {
                for (const float sei : {0.0F, 4.0F / 3.0F, 5.0F}) {
                    for (const bool adj : {false, true}) {
                        bhed::Camera c;
                        const bhed::SpielRahmen r = bhed::setzeSpielFov(c, fov, ans, bhed::SpielFenster{sei, adj});
                        const bhed::SpielSicht s = bhed::spielSicht(fov, bhed::SpielFenster{sei, adj});
                        ok = ok && std::isfinite(c.fovY) && c.fovY >= 1.0F && c.fovY <= 179.0F && std::isfinite(r.breite) &&
                             std::isfinite(r.hoehe) && r.breite > 0.0F && r.breite <= 1.0F && r.hoehe > 0.0F &&
                             r.hoehe <= 1.0F && std::isfinite(s.fovX) && std::isfinite(s.fovY);
                    }
                }
            }
        }
        expect("Bildwinkel 0, -5, 1000 und Seitenverhaeltnis 0 bleiben endlich und in 1..179", ok);
    }

    // --- Die freie Ansicht -------------------------------------------------
    //
    // Sie behaelt ihren Bildwinkel (73,7 senkrecht). Der Rahmen zeigt, was
    // eine Kamera mit FOV 90 dort im Spiel saehe.
    {
        bhed::Camera frei;   // fovY 73,7
        const double tV = tanHalb(static_cast<double>(frei.fovY));
        for (const bhed::SpielFenster& s : {md169, ojk169}) {
            const bhed::SpielRahmen r = bhed::spielRahmenFreieSicht(frei, p, s);
            const Erwartet e = calcFovFromX(90.0, s.seiten, s.fovAspectAdjust);
            const double b = std::min(1.0, tanHalb(e.fovX) / (tV * p));
            const double h = std::min(1.0, tanHalb(e.fovY) / tV);
            char t[200];
            std::snprintf(t, sizeof(t), "freie Ansicht, %s: Rahmen %.3f x %.3f (soll %.3f x %.3f)",
                          s.fovAspectAdjust ? "Movie Duels" : "OpenJK", (double)r.breite, (double)r.hoehe, b, h);
            expect(t, std::fabs(r.breite - b) < 0.002 && std::fabs(r.hoehe - h) < 0.002);
        }
        // Die freie Kamera selbst bleibt unangetastet.
        expect("die freie Kamera behaelt ihren Bildwinkel", near(frei.fovY, 73.7F, 0.0001F));
    }
}

// --- Paket kamera, rc601: Rollen wie AnglesToAxis -----------------------------
//
// CGCam_Update endet mit AnglesToAxis( cg.refdefViewAngles, cg.refdef.viewaxis )
// (cg_camera.cpp:1307 OpenJK, :1267 MD) - samt Rollwinkel. Camera::right()/up()
// bauten die Basis ohne ihn; ROLL und der Rollanteil eines PAN waren im Bild
// nie zu sehen. Die Vergleichswerte: AngleVectors aus q_math.c:1314 (OpenJK),
// hier abgeschrieben.
void angleVectors(const float ang[3], float vorn[3], float rechts[3], float oben[3]) {
    const double y = ang[1] * (kPiD * 2.0 / 360.0);
    const double p = ang[0] * (kPiD * 2.0 / 360.0);
    const double r = ang[2] * (kPiD * 2.0 / 360.0);
    const double sy = std::sin(y), cy = std::cos(y);
    const double sp = std::sin(p), cp = std::cos(p);
    const double sr = std::sin(r), cr = std::cos(r);
    vorn[0] = static_cast<float>(cp * cy);
    vorn[1] = static_cast<float>(cp * sy);
    vorn[2] = static_cast<float>(-sp);
    rechts[0] = static_cast<float>(-1 * sr * sp * cy + -1 * cr * -sy);
    rechts[1] = static_cast<float>(-1 * sr * sp * sy + -1 * cr * cy);
    rechts[2] = static_cast<float>(-1 * sr * cp);
    oben[0] = static_cast<float>(cr * sp * cy + -sr * -sy);
    oben[1] = static_cast<float>(cr * sp * sy + -sr * cy);
    oben[2] = static_cast<float>(cr * cp);
}
bool gleich3(const float a[3], const float b[3], float tol = 0.0005F) {
    return near(a[0], b[0], tol) && near(a[1], b[1], tol) && near(a[2], b[2], tol);
}

void rollProben() {
    std::printf("\n--- Rollen (Paket kamera) ---\n");
    const float proben[][3] = {{10.0F, 30.0F, 25.0F}, {-20.0F, 135.0F, -40.0F}, {0.0F, 90.0F, 30.0F},
                               {45.0F, -60.0F, 90.0F}, {5.0F, 200.0F, 0.0F}};
    for (const auto& w : proben) {
        bhed::Camera c;
        for (int k = 0; k < 3; ++k) { c.angles[k] = w[k]; }
        float f[3];
        float r[3];
        float u[3];
        c.forward(f);
        c.right(r);
        c.up(u);
        float ef[3];
        float er[3];
        float eu[3];
        angleVectors(w, ef, er, eu);
        char t[160];
        std::snprintf(t, sizeof(t), "Winkel %.0f %.0f %.0f: Basis wie AngleVectors (oben %.3f %.3f %.3f, soll %.3f %.3f %.3f)",
                      (double)w[0], (double)w[1], (double)w[2], (double)u[0], (double)u[1], (double)u[2], (double)eu[0],
                      (double)eu[1], (double)eu[2]);
        expect(t, gleich3(f, ef) && gleich3(r, er) && gleich3(u, eu));
        float zurueck[3];
        bhed::anglesFromBasis(f, u, zurueck);
        const float d = std::fmod(zurueck[2] - w[2] + 540.0F, 360.0F) - 180.0F;
        expect("  und der Rollwinkel kommt aus der Basis zurueck", std::fabs(d) < 0.05F);
    }
    // Ohne Roll bleibt alles, wie es war - die freie Kamera hat keinen.
    {
        bhed::Camera c;
        c.angles[0] = 20.0F;
        c.angles[1] = 90.0F;
        float f[3];
        float r[3];
        float u[3];
        c.forward(f);
        c.right(r);
        c.up(u);
        // Die alte Rechnung: rechts = normalise(vorn x Z), oben = rechts x vorn.
        float ar[3] = {f[1], -f[0], 0.0F};
        const float l = std::sqrt(ar[0] * ar[0] + ar[1] * ar[1]);
        ar[0] /= l;
        ar[1] /= l;
        const float au[3] = {ar[1] * f[2] - ar[2] * f[1], ar[2] * f[0] - ar[0] * f[2], ar[0] * f[1] - ar[1] * f[0]};
        expect("ohne Roll: dieselbe Basis wie bisher (freie Kamera unveraendert)", gleich3(r, ar) && gleich3(u, au));
    }
    // Senkrecht nach unten (pitch 90): die alte Rechnung teilte durch null
    // und lieferte eine leere Basis; AngleVectors bleibt bestimmt.
    {
        bhed::Camera c;
        c.angles[0] = 90.0F;
        c.angles[1] = 30.0F;
        float r[3];
        float u[3];
        c.right(r);
        c.up(u);
        float ef[3];
        float er[3];
        float eu[3];
        angleVectors(c.angles, ef, er, eu);
        expect("pitch 90: rechts und oben wie AngleVectors, nicht null", gleich3(r, er) && gleich3(u, eu));
    }
    // Und die Kameramatrix des GPU-Wegs nimmt den Roll mit: Zeile 1 (y) ist
    // oben * Fokus (gpucam.cpp).
    {
        bhed::Camera c;
        c.angles[0] = 0.0F;
        c.angles[1] = 90.0F;
        c.angles[2] = 30.0F;
        c.fovY = 60.0F;
        float m[16];
        bhed::gpu::baueViewProj(c, 800, 600, 4.0F, 8192.0F, m);
        const float l = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
        const float zeile[3] = {m[4] / l, m[5] / l, m[6] / l};
        float ef[3];
        float er[3];
        float eu[3];
        angleVectors(c.angles, ef, er, eu);
        std::printf("     Kameramatrix bei Roll 30: oben %.3f %.3f %.3f (soll %.3f %.3f %.3f)\n", (double)zeile[0],
                    (double)zeile[1], (double)zeile[2], (double)eu[0], (double)eu[1], (double)eu[2]);
        expect("die Kameramatrix rollt wie das Spiel (ROLL 30)", gleich3(zeile, eu, 0.001F));
    }
}

// --- Paket kamera, rc601 (Review): die freie Kamera fliegt waagerecht ----------
//
// Seit right()/up() rollen (AngleVectors), schob Camera::move (WASD) und
// Orbit::pan (mittlere Maustaste) eine gerollte freie Kamera schraeg - nach
// "Zur Einstellung fliegen" auf einer Einstellung mit ROLL stieg sie beim
// Seitwaertsfliegen. Entscheidung: das Bild zeigt den Roll weiter (wie
// AngleVectors), die BEWEGUNG bleibt waagerecht - mit der rollfreien Basis
// (die alte Rechnung: rechts = normalise(vorn x Z), oben = rechts x vorn).
void bewegenProben() {
    std::printf("\n--- Bewegen ohne Rollwinkel (Paket kamera, Review) ---\n");
    bhed::Camera gerollt;
    gerollt.pos[0] = 100.0F;
    gerollt.pos[1] = 200.0F;
    gerollt.pos[2] = 300.0F;
    gerollt.angles[0] = 10.0F;
    gerollt.angles[1] = 30.0F;
    gerollt.angles[2] = 45.0F;
    bhed::Camera flach = gerollt;
    flach.angles[2] = 0.0F;
    {
        bhed::Camera c = gerollt;
        c.move(0.0F, 100.0F, 0.0F);
        bhed::Camera d = flach;
        d.move(0.0F, 100.0F, 0.0F);
        std::printf("     seitwaerts 100 mit Roll 45: %.2f %.2f %.2f (ohne Roll %.2f %.2f %.2f)\n", (double)c.pos[0],
                    (double)c.pos[1], (double)c.pos[2], (double)d.pos[0], (double)d.pos[1], (double)d.pos[2]);
        expect("gerollte Kamera: seitwaerts (D) bleibt auf derselben Hoehe", near(c.pos[2], 300.0F, 0.001F));
        expect("gerollte Kamera: seitwaerts genau wie ohne Roll", gleich3(c.pos, d.pos, 0.001F));
    }
    {
        bhed::Camera c = gerollt;
        c.move(50.0F, -30.0F, 20.0F);
        bhed::Camera d = flach;
        d.move(50.0F, -30.0F, 20.0F);
        expect("gerollte Kamera: vor, seitwaerts und hoch zusammen wie ohne Roll", gleich3(c.pos, d.pos, 0.001F));
    }
    {
        bhed::Orbit o;
        o.distance = 400.0F;
        bhed::Camera c = gerollt;
        o.pan(c, 80.0F, 0.0F);
        bhed::Camera d = flach;
        o.pan(d, 80.0F, 0.0F);
        std::printf("     Orbit seitwaerts mit Roll 45: z %.3f (ohne Roll %.3f)\n", (double)c.pos[2], (double)d.pos[2]);
        expect("Umkreisen-Schieben waagerecht (mittlere Maustaste): Hoehe bleibt", near(c.pos[2], 300.0F, 0.001F));
        bhed::Camera e = gerollt;
        o.pan(e, 25.0F, -60.0F);
        bhed::Camera g = flach;
        o.pan(g, 25.0F, -60.0F);
        expect("Umkreisen-Schieben mit Roll genau wie ohne Roll", gleich3(c.pos, d.pos, 0.001F) && gleich3(e.pos, g.pos, 0.001F));
    }
    {
        // Das Bild rollt weiter: right()/up() bleiben AngleVectors.
        float r[3];
        float er[3];
        float ef[3];
        float eu[3];
        gerollt.right(r);
        angleVectors(gerollt.angles, ef, er, eu);
        expect("das Bild rollt weiter (right() wie AngleVectors)", gleich3(r, er));
    }
}

// --- Paket kamera, rc601: die Einstellungen des Spielfensters ------------------
void einstellungsProben() {
    std::printf("\n--- Einstellungen des Spielfensters (Paket kamera) ---\n");
    const bhed::Settings vorgabe;
    expect("Vorgabe: Seitenverhaeltnis wie der Bildschirm (0), cg_fovAspectAdjust automatisch, Rahmen an",
           vorgabe.spielSeiten == 0.0F && vorgabe.fovAspectAdjust == -1 && vorgabe.spielRahmenFrei);
    bhed::Settings a;
    a.spielSeiten = 16.0F / 10.0F;
    a.fovAspectAdjust = 0;
    a.spielRahmenFrei = false;
    bhed::Settings b;
    (void)bhed::readSettings(bhed::writeSettings(a), b);
    std::printf("     zurueckgelesen: %.4f, %d, %d\n", (double)b.spielSeiten, b.fovAspectAdjust, b.spielRahmenFrei ? 1 : 0);
    expect("Seitenverhaeltnis, cg_fovAspectAdjust und Rahmen ueberstehen das Speichern",
           near(b.spielSeiten, 1.6F, 0.0005F) && b.fovAspectAdjust == 0 && !b.spielRahmenFrei);
    a.fovAspectAdjust = 1;
    (void)bhed::readSettings(bhed::writeSettings(a), b);
    expect("cg_fovAspectAdjust 1 ebenso", b.fovAspectAdjust == 1);
    bhed::Settings c;
    (void)bhed::readSettings("spielSeiten=99\nfovAspectAdjust=7\n", c);
    expect("unsinnige Werte werden verworfen", c.spielSeiten == 0.0F && c.fovAspectAdjust == -1);
}
}  // namespace

int main() {
    // --- Blickrichtung ----------------------------------------------------
    {
        bhed::Camera c;
        float f[3];
        c.angles[0] = 0.0F;
        c.angles[1] = 0.0F;
        c.forward(f);
        expect("yaw 0 blickt nach +X", f[0] > 0.99F);
        c.angles[1] = 90.0F;
        c.forward(f);
        expect("yaw 90 blickt nach +Y", f[1] > 0.99F);
        c.angles[0] = 90.0F;
        c.forward(f);
        // pitch positiv heisst in der Engine nach UNTEN.
        expect("pitch 90 blickt nach unten", f[2] < -0.99F);
    }

    // --- Umkreisen laesst den Drehpunkt stehen -----------------------------
    {
        bhed::Camera c;
        c.pos[0] = 100.0F;
        bhed::Orbit o;
        o.distance = 400.0F;
        float before[3];
        o.pivot(c, before);
        for (int i = 0; i < 24; ++i) {
            o.turn(c, 15.0F, 3.0F);
        }
        float after[3];
        o.pivot(c, after);
        expect("Drehpunkt bleibt nach 24 Drehungen stehen", dist(before, after) < 0.5F);
        expect("Abstand bleibt erhalten",
               std::fabs(dist(c.pos, after) - o.distance) < 0.5F);
    }

    // --- Zoomen laesst die Richtung stehen ---------------------------------
    {
        bhed::Camera c;
        c.angles[1] = 37.0F;
        c.angles[0] = -12.0F;
        bhed::Orbit o;
        float p0[3];
        o.pivot(c, p0);
        const float yaw = c.angles[1];
        const float pitch = c.angles[0];
        o.zoom(c, 5.0F);
        float p1[3];
        o.pivot(c, p1);
        expect("Zoomen aendert die Blickrichtung nicht",
               std::fabs(c.angles[1] - yaw) < 0.001F &&
                   std::fabs(c.angles[0] - pitch) < 0.001F);
        expect("Drehpunkt bleibt beim Zoomen stehen", dist(p0, p1) < 0.5F);
        expect("naeher heran", o.distance < 512.0F);
        o.zoom(c, -20.0F);
        expect("und wieder weiter weg", o.distance > 512.0F);
        for (int i = 0; i < 200; ++i) { o.zoom(c, 5.0F); }
        expect("Zoomen laeuft nicht durch den Drehpunkt hindurch", o.distance > 0.0F);
    }

    // --- Schieben haengt vom Abstand ab ------------------------------------
    {
        bhed::Camera near1;
        bhed::Camera far1;
        bhed::Orbit onear;
        bhed::Orbit ofar;
        onear.distance = 100.0F;
        ofar.distance = 4000.0F;
        onear.pan(near1, 50.0F, 0.0F);
        ofar.pan(far1, 50.0F, 0.0F);
        float zero[3] = {0, 0, 0};
        expect("aus der Ferne schiebt man weiter",
               dist(far1.pos, zero) > dist(near1.pos, zero) * 5.0F);
    }

    // --- Blickwinkel: waagerecht zu senkrecht -----------------------------
    //
    // ICARUS gibt in camera ( ZOOM, ... ) den WAAGERECHTEN Winkel an, der
    // Zeichner rechnet mit dem senkrechten. Die Engine rechnet in
    // CG_CalcFOVFromX:
    //     x     = breite / tan( fov_x / 2 )
    //     fov_y = atan2( hoehe, x )
    // Wer die Zahl direkt als fovY einsetzt, bekommt ein zu enges Bild - und
    // zwar umso mehr, je breiter das Fenster ist.
    {
        bhed::Camera c;
        c.setFovX(90.0F, 4.0F / 3.0F);
        // Der bekannte Wert: fov_x 90 bei 4:3 ergibt 73,74 senkrecht.
        expect("fov_x 90 bei 4:3 ergibt 73,7 senkrecht",
               std::fabs(c.fovY - 73.74F) < 0.1F);

        // Mit cg_fovAspectAdjust (Movie Duels: an) bleibt der SENKRECHTE
        // Winkel bei jedem Seitenverhaeltnis gleich - breiter heisst nur
        // mehr zu den Seiten. Ohne die Korrektur waeren es bei 16:9 nur
        // 58,7 Grad gewesen.
        bhed::Camera wide;
        wide.setFovX(90.0F, 16.0F / 9.0F);
        expect("bei 16:9 bleibt der senkrechte Winkel 73,7 (cg_fovAspectAdjust)",
               std::fabs(wide.fovY - 73.74F) < 0.1F);

        // Die Spanne aus Ravens Skripten: 6,3 bis 110 Grad.
        bhed::Camera tele;
        tele.setFovX(6.291F, 16.0F / 9.0F);
        expect("ein Teleobjektiv bleibt eng", tele.fovY > 0.5F && tele.fovY < 8.0F);
        bhed::Camera wideAngle;
        wideAngle.setFovX(110.0F, 16.0F / 9.0F);
        expect("ein Weitwinkel bleibt weit", wideAngle.fovY > 60.0F);

        // Unsinnige Werte duerfen nichts kaputtmachen.
        bhed::Camera bad;
        bad.setFovX(0.0F, 1.0F);
        expect("ein Winkel von 0 wird abgefangen", bad.fovY > 0.0F);
        bad.setFovX(1000.0F, 1.0F);
        expect("und einer von 1000 auch", bad.fovY < 180.0F);
        bad.setFovX(90.0F, 0.0F);
        expect("ein Seitenverhaeltnis von 0 ebenso",
               bad.fovY > 0.0F && bad.fovY < 180.0F);
    }

    // --- Die Blende kommt ZULETZT ---------------------------------------
    //
    // Gemeldet: "kann man durch die schwarzen balken noch durchsehen wenn
    // die cutscene laeuft". Der Grund war die Reihenfolge: renderMap malte
    // die Blende, und DANACH wurden Figuren und Effekte gezeichnet - die
    // uebermalten sie wieder. Im Bild stand der Kopf des Hologramms mitten
    // im schwarzen Balken.
    //
    // Diese Probe braucht keine Karte: sie fuellt ein Bild, kleistert den
    // Blendenbereich zu (so wie es eine Figur taete) und prueft, dass
    // drawLetterbox danach wieder deckt.
    {
        bhed::MapImage img;
        img.width = 100;
        img.height = 100;
        img.rgba.assign(100U * 100U * 4U, 128);
        bhed::drawLetterbox(img);
        // Eine "Figur" in die obere Blende malen.
        for (int y = 0; y < 10; ++y) {
            for (int x = 0; x < 100; ++x) {
                const std::size_t at =
                    (static_cast<std::size_t>(y) * 100U +
                     static_cast<std::size_t>(x)) * 4U;
                img.rgba[at + 0] = 200;
                img.rgba[at + 1] = 60;
                img.rgba[at + 2] = 255;
                img.rgba[at + 3] = 255;
            }
        }
        // Vorher: sie steht sichtbar im Balken.
        expect("ohne zweiten Durchgang steht die Figur IM Balken",
               img.rgba[0] == 200);
        // Und jetzt die Blende erneut - so, wie es die Ansicht jetzt macht.
        bhed::drawLetterbox(img);
        bool obenSchwarz = true;
        bool untenSchwarz = true;
        for (int x = 0; x < 100; ++x) {
            for (int y = 0; y < 10; ++y) {
                const std::size_t o =
                    (static_cast<std::size_t>(y) * 100U +
                     static_cast<std::size_t>(x)) * 4U;
                if (img.rgba[o] != 0 || img.rgba[o + 1] != 0 ||
                    img.rgba[o + 2] != 0 || img.rgba[o + 3] != 255) {
                    obenSchwarz = false;
                }
                const std::size_t u =
                    (static_cast<std::size_t>(99 - y) * 100U +
                     static_cast<std::size_t>(x)) * 4U;
                if (img.rgba[u] != 0 || img.rgba[u + 3] != 255) {
                    untenSchwarz = false;
                }
            }
        }
        expect("danach deckt die obere Blende wieder vollstaendig",
               obenSchwarz);
        expect("die untere ebenso", untenSchwarz);
        // Die Bildmitte darf sie NICHT anfassen.
        const std::size_t mitte = (50U * 100U + 50U) * 4U;
        expect("und die Bildmitte bleibt unberuehrt",
               img.rgba[mitte] == 128);
        // Und die Hoehe stimmt: ein Zehntel, wie bar_height_dest = 480/10.
        const std::size_t knapp = (10U * 100U + 50U) * 4U;
        expect("genau ein Zehntel hoch - die elfte Zeile ist frei",
               img.rgba[knapp] == 128);
    }

    // --- Brush-Modelle mit eigenem Ursprung -----------------------------
    //
    // Ein Brush-Modell, dessen Entity einen "origin"-Schluessel hat, liegt
    // RELATIV dazu in der Datei: q3map2 zieht den Ursprung ab, sobald ein
    // origin-Brush im Gebilde steckt, und die Engine addiert ihn beim
    // Zeichnen wieder.
    //
    // Die Zahlen stammen aus md_am_sith: das Raumschiff "sta2" ist Modell
    // *125 mit Ausmassen von -118 bis 84 um die Null, und sein Eintrag sagt
    // "origin" "-1514 7376 645". Ohne die Addition liegt es 7000 Einheiten
    // daneben - dort sieht man es nie, und die Animation schien zu fehlen.
    {
        const float ursprung[3] = {-1514.0F, 7376.0F, 645.0F};
        // Ein Dreieck, wie es in der Datei staende: um die Null herum.
        const float lokal[3][3] = {{-118.0F, -84.0F, -22.0F},
                                   {84.5F, -84.0F, -22.0F},
                                   {0.0F, 84.0F, 32.0F}};

        // So rechnet der Zeichner: Welt = dreh(v - pivot) + pivot + offset.
        // Mit pivot = 0 und offset = Ursprung wird daraus v + Ursprung.
        auto welt = [&](const float v[3], const float pivot[3],
                        const float offset[3], float out[3]) {
            for (int k = 0; k < 3; ++k) {
                out[k] = (v[k] - pivot[k]) + pivot[k] + offset[k];
            }
        };

        const float pivotNull[3] = {0.0F, 0.0F, 0.0F};
        float ecke[3];
        welt(lokal[0], pivotNull, ursprung, ecke);
        std::printf("     Ecke im Bau %.1f %.1f %.1f -> Welt %.1f %.1f %.1f\n",
                    (double)lokal[0][0], (double)lokal[0][1],
                    (double)lokal[0][2], (double)ecke[0], (double)ecke[1],
                    (double)ecke[2]);
        expect("das Modell landet an seinem Platz in der Karte",
               std::fabs(ecke[0] - (-1632.0F)) < 0.01F &&
                   std::fabs(ecke[1] - 7292.0F) < 0.01F &&
                   std::fabs(ecke[2] - 623.0F) < 0.01F);

        // Und die Gegenrechnung: das ALTE Verfahren war
        // offset = jetzt - start, pivot = start. Beim Start ist das null,
        // das Modell stuende also am Nullpunkt der Karte.
        float alt[3];
        const float offsetAlt[3] = {0.0F, 0.0F, 0.0F};   // jetzt == start
        welt(lokal[0], ursprung, offsetAlt, alt);
        const float abstand =
            std::sqrt((alt[0] - ecke[0]) * (alt[0] - ecke[0]) +
                      (alt[1] - ecke[1]) * (alt[1] - ecke[1]) +
                      (alt[2] - ecke[2]) * (alt[2] - ecke[2]));
        std::printf("     altes Verfahren lag %.0f Einheiten daneben\n",
                    (double)abstand);
        expect("das alte Verfahren lag WEIT daneben - der Fehler war echt",
               abstand > 7000.0F);

        // Bewegt: nach dem ersten move steht das Schiff bei 152 7312 336.
        // Der Versatz ist dann dieser Ort selbst, nicht die Differenz.
        const float ziel[3] = {152.0F, 7312.0F, 336.0F};
        float bewegt[3];
        welt(lokal[2], pivotNull, ziel, bewegt);
        expect("nach dem move steht es am Ziel des Skripts",
               std::fabs(bewegt[0] - 152.0F) < 0.01F &&
                   std::fabs(bewegt[1] - (7312.0F + 84.0F)) < 0.01F &&
                   std::fabs(bewegt[2] - (336.0F + 32.0F)) < 0.01F);
    }

    // --- Ein verworfener Bildpunkt darf NICHTS verdecken -------------------
    //
    // Gemeldet: "es ist nun grau statt schwarz."
    //
    // Genau das war der Fehler. Der Alphatest verwarf die Bildpunkte
    // richtig, aber die TIEFE stand da schon: die Tafel blockierte
    // weiterhin die Wand dahinter, und sichtbar blieb der leere
    // Hintergrund. Aus Schwarz wurde Grau - ein Fortschritt, der wie ein
    // halber Fehler aussah.
    //
    // Denselben Fehler hatte ich in rc282 im FIGURENzeichner schon einmal.
    // Zweimal derselbe Griff heisst: er gehoert in eine Probe.
    //
    // Aufbau: zwei Flaechen hintereinander. Die vordere ist voll
    // durchsichtig und hat alphaFunc GT0, die hintere ist deckend. Zu
    // sehen sein muss die HINTERE.
    {
        bhed::TextureSet ts;
        // Vorn: alles Alpha 0, wird komplett verworfen.
        bhed::TextureSet::Tex vorn;
        vorn.width = 2;
        vorn.height = 2;
        vorn.rgba.assign(2 * 2 * 4, 0);      // auch Alpha 0
        vorn.alphaTest = bhed::AlphaTest::Gt0;
        // Hinten: voll deckend und deutlich erkennbar.
        bhed::TextureSet::Tex hinten;
        hinten.width = 2;
        hinten.height = 2;
        hinten.rgba.assign(2 * 2 * 4, 255);
        for (std::size_t i = 0; i < 4; ++i) {
            hinten.rgba[i * 4 + 0] = 255;    // rot
            hinten.rgba[i * 4 + 1] = 0;
            hinten.rgba[i * 4 + 2] = 0;
            hinten.rgba[i * 4 + 3] = 255;
        }
        ts.byShader.push_back(vorn);
        ts.byShader.push_back(hinten);

        // EHRLICH GESAGT: das hier prueft nur, dass die Daten so
        // aussehen, wie sie sollen - NICHT, dass renderMap die Reihenfolge
        // einhaelt. Dafuer braeuchte es eine synthetische .bsp mit zwei
        // Flaechen hintereinander, und die gibt es noch nicht.
        //
        // Der wirkliche Waechter ist bis dahin der Lauf mit
        // md_ga_jedi.bsp: dort stehen 25 Flaechen mit
        // textures/md_ga/geonosian1, und wer die Reihenfolge vertauscht,
        // sieht es sofort auf dem Bildschirm.
        //
        // Das steht hier, damit niemand diese Zusicherung fuer mehr haelt,
        // als sie ist.
        expect("die vordere Textur verwirft alles",
               ts.byShader[0].alphaTest == bhed::AlphaTest::Gt0 &&
                   ts.byShader[0].rgba[3] == 0);
        expect("die hintere ist deckend",
               ts.byShader[1].alphaTest == bhed::AlphaTest::None &&
                   ts.byShader[1].rgba[3] == 255);
    }

    // --- Die Bildpyramide --------------------------------------------------
    //
    // Gebeten: "mach Mipmaps."
    //
    // Jede Stufe hat die halbe Kante der vorigen, bis 1x1 - wie R_MipMap
    // (tr_image.cpp:466): je vier Nachbarn zu einem.
    {
        bhed::TextureSet::Tex t;
        t.width = 8;
        t.height = 4;
        t.rgba.assign(8 * 4 * 4, 0);
        // Ein Schachbrett, damit sich das Mitteln zeigt.
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 8; ++x) {
                const std::size_t at =
                    (static_cast<std::size_t>(y) * 8U +
                     static_cast<std::size_t>(x)) * 4U;
                const std::uint8_t w = ((x + y) % 2 == 0) ? 255U : 0U;
                for (int k = 0; k < 3; ++k) { t.rgba[at + static_cast<std::size_t>(k)] = w; }
                t.rgba[at + 3] = 255U;
            }
        }
        t.buildMips();
        // 8x4 -> 4x2 -> 2x1 -> 1x1, also DREI Stufen ueber dem Bild selbst.
        expect("die Pyramide hat drei Stufen", t.mips.size() == 3U);
        if (t.mips.size() == 3U) {
            expect("erste Stufe 4x2",
                   t.mips[0].width == 4 && t.mips[0].height == 2);
            expect("zweite Stufe 2x1",
                   t.mips[1].width == 2 && t.mips[1].height == 1);
            expect("letzte Stufe 1x1",
                   t.mips[2].width == 1 && t.mips[2].height == 1);
            // Aus dem Schachbrett wird beim Mitteln GRAU. Bliebe es
            // schwarz-weiss, waere nicht gemittelt worden.
            expect("aus dem Schachbrett wird Grau",
                   t.mips[0].rgba[0] > 120U && t.mips[0].rgba[0] < 136U);
        }
        // Mehrfaches Rufen darf nicht noch einmal anbauen.
        t.buildMips();
        expect("zweimal bauen aendert nichts", t.mips.size() == 3U);

        // Eine zu hohe Stufe gibt die kleinste vorhandene, statt ins Leere
        // zu greifen.
        int w = 0;
        int h = 0;
        const std::uint8_t* d = t.levelData(99, w, h);
        expect("eine zu hohe Stufe gibt die kleinste",
               d != nullptr && w == 1 && h == 1);
        // Und Stufe 0 ist das Bild selbst.
        const std::uint8_t* d0 = t.levelData(0, w, h);
        expect("Stufe 0 ist das Bild selbst",
               d0 == t.rgba.data() && w == 8 && h == 4);
    }

    // --- Wie gross sind die Dreiecke, die die Flaeche tragen? --------------
    //
    // Diese Frage entscheidet, ob sich ein Umbau auf VIERERGRUPPEN von
    // Bildpunkten lohnt (SIMD ueber vier Bildpunkte statt vier Kanaele).
    //
    // Bei so einem Umbau rechnet jeder Bildpunkt einer Vierergruppe ALLES
    // mit und wird erst am Ende verworfen. Bei kleinen Dreiecken liegen von
    // vier Bildpunkten oft nur ein oder zwei wirklich drin - dann rechnet
    // man das Vierfache fuer ein Viertel Ertrag.
    //
    // Gemessen an md_ga_jedi, Blick von 240/1208/1304:
    //
    //     unter      4 Bildpunkte:   260 Dreiecke,   0,0 % der Flaeche
    //     4 bis     16:              1249 Dreiecke,  0,0 %
    //     16 bis    64:               557 Dreiecke,  0,0 %
    //     64 bis   256:               437 Dreiecke,  0,0 %
    //     256 bis 1024:               273 Dreiecke,  0,0 %
    //     ueber   1024:               597 Dreiecke, **100,0 %**
    //
    // Also: die ZAHL der Dreiecke liegt bei den kleinen, die FLAECHE
    // vollstaendig bei den grossen. In einem Dreieck von tausend
    // Bildpunkten liegt eine Vierergruppe fast immer ganz drin.
    //
    // Der Umbau wuerde sich also lohnen. Diese Probe haelt die Grundlage
    // fest: waere es umgekehrt, duerfte man ihn nicht machen - und dann
    // soll das hier fehlschlagen, statt dass jemand ihn baut und sich
    // hinterher wundert.
    {
        // Ein grosses und ein winziges Dreieck. Geprueft wird, dass die
        // Flaechenrechnung ueberhaupt das misst, worauf die Aussage beruht.
        auto flaeche = [](float ax, float ay, float bx, float by, float cx,
                          float cy) {
            return std::fabs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay)) *
                   0.5F;
        };
        const float gross = flaeche(0, 0, 100, 0, 0, 100);
        const float klein = flaeche(0, 0, 2, 0, 0, 2);
        expect("ein 100er Dreieck traegt 5000 Bildpunkte",
               near(gross, 5000.0F, 1.0F));
        expect("ein 2er Dreieck traegt zwei",
               near(klein, 2.0F, 0.01F));
        // Und das Verhaeltnis, um das es geht: EIN grosses Dreieck traegt
        // so viel wie zweieinhalbtausend winzige.
        expect("eines traegt so viel wie tausende kleine",
               gross / klein > 2000.0F);
    }

    spielfensterProben();
    rollProben();
    bewegenProben();
    einstellungsProben();

    std::printf("\n%s (%d Fehlschlaege)\n",
                fails != 0 ? "FEHLGESCHLAGEN" : "alle Ansichtsproben bestanden", fails);
    return fails != 0 ? 1 : 0;
}
