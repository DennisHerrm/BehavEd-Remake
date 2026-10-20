// Probe fuer bhed/grasflaechen.h (rc604): surfaceSprites wie
// tr_surfacesprites.cpp (OpenJK/Movie Duels rd-vanilla).
//
// Die Sollwerte sind von Hand aus den Formeln der Engine nachgerechnet -
// eine unabhaengige float32-Nachrechnung derselben Schleifen (randomchart,
// Q_rsqrt, (byte)-Abschneiden, randomindex/randominterval je Dreieck), nicht
// mit dieser Datei erzeugt.
#include "bhed/grasflaechen.h"
#include "bhed/shaderscript.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace bhed;

namespace {
int g_fehler = 0;
void pruefe(bool ok, const std::string& was) {
    std::printf("%s %s\n", ok ? "OK    " : "FEHLER", was.c_str());
    if (!ok) { ++g_fehler; }
}
bool nah(float a, float b, float t = 2e-3F) { return std::fabs(a - b) < t; }
bool byteNah(int a, int b) { return a - b <= 1 && b - a <= 1; }

bool quadGleich(const gras::Quad& q, const float soll[4][3], int licht, int alpha) {
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 3; ++k) {
            if (!nah(q.ecke[i][k], soll[i][k])) { return false; }
        }
    }
    return byteNah(q.licht, licht) && byteNah(q.alpha, alpha);
}

// Das Dreieck aller Proben: (0,0,0) (64,0,0) (0,64,0), Normale nach oben,
// Licht (blau) 200 / 100 / 50.
void dreieck(gras::Ecke e[3]) {
    const float p[3][3] = {{0, 0, 0}, {64, 0, 0}, {0, 64, 0}};
    const float l[3] = {200, 100, 50};
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < 3; ++k) { e[i].xyz[k] = p[i][k]; }
        e[i].normalZ = 1.0F;
        e[i].licht = l[i];
    }
}

gras::Rahmen sicht(float x, float y, float z, int zeit) {
    gras::Rahmen r;
    r.auge[0] = x;
    r.auge[1] = y;
    r.auge[2] = z;
    // Blick nach +y: axis[1] (links) = -x, axis[2] = +z.
    r.links[0] = -1.0F;
    r.links[1] = 0.0F;
    r.links[2] = 0.0F;
    r.zeitMs = zeit;
    gras::rahmenFertig(r);
    return r;
}
}  // namespace

int main() {
    // --- Grundbausteine -------------------------------------------------
    pruefe(nah(gras::kZufall[0], 0.6554F, 1e-6F) && nah(gras::kZufall[128], 0.9118F, 1e-6F) &&
               nah(gras::kZufall[255], 0.4677F, 1e-6F),
           "randomchart wie tr_surfacesprites.cpp:41");
    pruefe(nah(gras::qRsqrt(4096.0F), 0.0155985495F, 1e-8F), "Q_rsqrt(4096) mit dem Fehler der Engine (nicht 1/64)");
    pruefe(gras::alsByte(128.0F) == 128U && gras::alsByte(300.7F) == 44U && gras::alsByte(-3.7F) == 253U,
           "(byte) wie MSVC: abschneiden, untere acht Bit");

    // --- Lesen wie ParseSurfaceSprites / ParseSurfaceSpritesOptional -----
    {
        ShaderMap m;
        parseShaderScript("textures/probe/gras\n{\n  {\n    map textures/probe/boden\n  }\n"
                          "  {\n    map gfx/sprites/halm\n    surfaceSprites vertical 16 24 32 300\n"
                          "    ssFademax 200\n    SsFadescale 1.5\n    ssVariance 1 2\n    ssWind 0.5\n"
                          "    ssHangdown\n    ssFaceup\n    ssFXWeather\n    alphaFunc GE192\n"
                          "    blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA\n    depthWrite\n  }\n"
                          "  {\n    clampmap gfx/sprites/tropfen\n    ssFXGrow 6 6\n"
                          "    surfaceSprites effect 1.5 1.5 64 512\n    ssFXDuration 135\n"
                          "    ssFXAlphaRange 0.3 0\n    ssFXWeather\n    blendFunc GL_ONE GL_ONE\n  }\n"
                          "  {\n    map gfx/sprites/kaputt\n    surfaceSprites vertical 16 24 32 20\n  }\n}\n",
                          m);
        const auto it = m.find("textures/probe/gras");
        pruefe(it != m.end() && it->second.stages.size() == 4U, "Shader mit vier Stufen gelesen");
        if (it != m.end() && it->second.stages.size() == 4U) {
            const SurfaceSprite& a = it->second.stages[1].ss;
            pruefe(a.typ == 1 && nah(a.width, 16) && nah(a.height, 24) && nah(a.density, 32) && nah(a.fadeDist, 300),
                   "surfaceSprites vertical 16 24 32 300");
            pruefe(nah(a.fadeMax, 399.0F), "ssFademax 200 <= fadeDist wird verworfen, es bleibt 300*1.33");
            pruefe(nah(a.fadeScale, 1.5F), "SsFadescale (Gross-/Kleinschreibung egal)");
            pruefe(nah(a.variance[0], 1) && nah(a.variance[1], 2), "ssVariance 1 2");
            pruefe(nah(a.wind, 0.5F) && nah(a.windIdle, 0.5F), "ssWind setzt auch windIdle");
            pruefe(a.facing == 2, "ssHangdown gilt, ein zweites ssFaceup nicht");
            pruefe(a.typ == 1, "ssFXWeather nur bei effect");
            const SurfaceSprite& b = it->second.stages[2].ss;
            pruefe(b.typ == 4 && nah(b.fxDuration, 135) && nah(b.fxAlphaStart, 0.3F) && nah(b.fxAlphaEnd, 0.0F),
                   "effect + ssFXWeather = weatherfx, Dauer und Alphabereich");
            pruefe(nah(b.fxGrow[0], 0) && nah(b.fxGrow[1], 0), "ssFXGrow VOR surfaceSprites wird zurueckgesetzt");
            pruefe(!it->second.stages[3].ss.aktiv(), "fadedist < 32: keine surfaceSprites, eine normale Schicht");
            pruefe(!it->second.stages[0].ss.aktiv(), "die Grundstufe hat keine");
        }
    }

    // --- vertical: Stellen und Vierecke ---------------------------------
    SurfaceSprite ss;
    ss.typ = 1;
    ss.width = 16.0F;
    ss.height = 24.0F;
    ss.density = 32.0F;
    ss.fadeDist = 300.0F;
    ss.fadeMax = 400.0F;
    ss.variance[0] = 1.0F;
    ss.variance[1] = 2.0F;
    gras::Ecke e[3];
    dreieck(e);
    {
        std::vector<gras::Kandidat> k;
        gras::kandidaten(ss, e, k);
        // step = 32 * Q_rsqrt(4096) = 0.49915; randomindex 128, interval 3.
        pruefe(k.size() == 3U, "vertical, Dichte 32: drei Stellen auf dem Dreieck");
        if (k.size() == 3U) {
            pruefe(nah(k[0].p[0], 19.263334F) && nah(k[0].p[1], 15.608458F) && nah(k[0].p[2], 0.0F),
                   "erste Stelle (19.26, 15.61, 0)");
            pruefe(nah(k[0].breite, -19.7392F) && nah(k[0].hoehe, 35.2176F) && k[0].rechts == 1U,
                   "erste Stelle: Breite -19.74 (gespiegelt), Hoehe 35.22, rightvectorcount 1");
            pruefe(nah(k[0].zufallFade, 0.9875F, 1e-5F) && nah(k[0].licht, 133.31873F),
                   "erste Stelle: Ausblendzufall 0.9875, Licht 133.3");
            pruefe(nah(k[1].p[0], 51.691544F) && nah(k[1].p[1], 4.842716F) && k[1].rechts == 2U &&
                       nah(k[1].breite, 30.1904F) && nah(k[1].hoehe, 66.5712F),
                   "zweite Stelle");
            pruefe(nah(k[2].p[0], 9.424019F) && nah(k[2].p[1], 4.711739F) && k[2].rechts == 0U &&
                       nah(k[2].licht, 174.23183F),
                   "dritte Stelle");
        }
        const gras::Rahmen r = sicht(10, -100, 40, 0);
        std::vector<gras::Quad> q;
        gras::zeichneKandidaten(ss, k.data(), k.size(), r, false, q);
        pruefe(q.size() == 3U, "nah dran: alle drei gezeichnet");
        if (q.size() == 3U) {
            const float q0[4][3] = {{27.810408F, 20.543259F, 0.0F},
                                    {27.810408F, 20.543259F, 35.217602F},
                                    {10.716261F, 6.725817F, 35.217602F},
                                    {10.716261F, 10.673657F, 0.0F}};
            pruefe(quadGleich(q[0], q0, 133, 255), "erstes Viereck: Ecken, Licht 133, Alpha 255");
            const float q2[4][3] = {{-1.868021F, 6.706474F, 0.0F},
                                    {-1.868021F, 6.706474F, 44.784F},
                                    {20.716059F, 7.302603F, 44.784F},
                                    {20.716059F, 2.717003F, 0.0F}};
            pruefe(quadGleich(q[2], q2, 174, 255), "drittes Viereck");
        }
        std::vector<gras::Quad> qd;
        gras::dreieck(ss, e, r, false, qd);
        bool gleich = qd.size() == q.size();
        for (std::size_t n = 0; gleich && n < q.size(); ++n) {
            gleich = quadGleich(qd[n], q[n].ecke, q[n].licht, q[n].alpha);
        }
        pruefe(gleich && !qd.empty(), "dreieck() = kandidaten() + zeichneKandidaten()");
        const gras::Rahmen weit = sicht(10, -500, 40, 0);
        std::vector<gras::Quad> qw;
        gras::zeichneKandidaten(ss, k.data(), k.size(), weit, false, qw);
        pruefe(qw.empty(), "hinter fadeMax: nichts");
    }
    {
        // Weiter weg (ausblendend) und ssFadescale 1, Dichte 16.
        SurfaceSprite s2 = ss;
        s2.density = 16.0F;
        s2.fadeScale = 1.0F;
        const gras::Rahmen r = sicht(32, -330, 30, 0);
        std::vector<gras::Quad> q;
        gras::dreieck(s2, e, r, false, q);
        pruefe(q.size() == 7U, "Dichte 16, 330 weit: sieben Vierecke");
        if (q.size() == 7U) {
            const float q0[4][3] = {{24.240112F, 48.238667F, 0.0F},
                                    {24.240112F, 48.238667F, 35.217602F},
                                    {-4.976779F, 24.622242F, 35.217602F},
                                    {-4.976779F, 31.369793F, 0.0F}};
            pruefe(quadGleich(q[0], q0, 91, 74), "erstes: halb ausgeblendet (Alpha 74), breiter durch ssFadescale");
            const float q2[4][3] = {{27.577034F, 5.664540F, 0.0F},
                                    {27.577034F, 5.664540F, 38.16F},
                                    {55.779221F, 16.372780F, 38.16F},
                                    {55.779221F, 10.646449F, 0.0F}};
            pruefe(quadGleich(q[2], q2, 115, 157), "drittes: Alpha 157");
        }
    }
    {
        // Normale nach unten: vertical ohne ssHangdown zeichnet nichts.
        gras::Ecke u[3];
        dreieck(u);
        u[1].normalZ = 0.3F;
        std::vector<gras::Kandidat> k;
        gras::kandidaten(ss, u, k);
        pruefe(k.empty(), "eine Ecke mit normal.z < 0.5: das Dreieck bleibt leer");
    }

    // --- effect (additiv, wie die Regenspritzer von Kamino) -------------
    {
        SurfaceSprite fx;
        fx.typ = 3;
        fx.width = 2.0F;
        fx.height = 2.0F;
        fx.density = 8.0F;
        fx.fadeDist = 300.0F;
        fx.fadeMax = 400.0F;
        fx.variance[0] = 1.0F;
        fx.variance[1] = 0.75F;
        fx.fxDuration = 135.0F;
        fx.fxGrow[0] = 6.0F;
        fx.fxGrow[1] = 6.0F;
        fx.fxAlphaStart = 0.3F;
        fx.fxAlphaEnd = 0.0F;
        const gras::Rahmen r = sicht(10, -100, 40, 12345);
        std::vector<gras::Quad> q;
        gras::dreieck(fx, e, r, true, q);
        pruefe(q.size() == 32U, "effect, Zeit 12345: 32 Vierecke");
        if (q.size() >= 3U) {
            const float q0[4][3] = {{-6.193180F, 54.313225F, 0.0F},
                                    {-6.193180F, 54.313225F, 20.073296F},
                                    {15.963812F, 54.313225F, 20.073296F},
                                    {15.963812F, 54.313225F, 0.0F}};
            pruefe(quadGleich(q[0], q0, 1, 255), "erstes: fast am Ende seines Laufs (gross, Licht 1)");
            const float q1[4][3] = {{15.043848F, 45.750946F, 0.0F},
                                    {15.043848F, 45.750946F, 5.802155F},
                                    {8.418766F, 45.750946F, 5.802155F},
                                    {8.418766F, 45.750946F, 0.0F}};
            pruefe(quadGleich(q[1], q1, 11, 255), "zweites: am Anfang (klein)");
        }
        std::vector<gras::Quad> q2;
        gras::Rahmen r2 = r;
        r2.zeitMs = 12345 + 70;
        gras::dreieck(fx, e, r2, true, q2);
        pruefe(!q2.empty() && (q2.size() != q.size() || !nah(q2[0].ecke[0][0], q[0].ecke[0][0])),
               "effect wandert mit der Zeit");
        SurfaceSprite wx = fx;
        wx.typ = 4;
        pruefe(!gras::stufeZeichnet(wx, 1, 0.0F) && gras::stufeZeichnet(wx, 1, 1.0F),
               "weatherfx nur, wenn es regnet (curWeatherAmount)");
        pruefe(!gras::stufeZeichnet(fx, 0, 1.0F) && gras::stufeZeichnet(fx, 1, 0.0F),
               "Stufe 0 zeichnet die Engine nie als Bilder (tr_shade.cpp:2149)");
    }

    // --- Wind ruhig: ohne Wetter kein Wind ------------------------------
    {
        gras::Rahmen r = sicht(0, 0, 0, 0);
        const float x[3] = {1.0F, 0.0F, 0.0F};
        gras::windSetzen(0.0F, x, r);
        pruefe(nah(r.windTempo, 0.0F) && nah(r.grasWind[2], 0.0F) && nah(r.grasWind[0], 0.0F),
               "ohne Wind: curWindSpeed 0, Grasrichtung 0");
        gras::windSetzen(1000.0F, x, r);   // targetspeed 20 -> Neigung -70 Grad
        pruefe(nah(r.windTempo, 20.0F) && nah(r.grasWind[0], 0.34202F) && nah(r.grasWind[2], 0.93969F - 1.0F) &&
                   nah(r.blasWind[0], 20.0F),
               "Wind 1000 nach +x: Tempo 20, Gras um 20 Grad geneigt");
    }
    // --- Patches wie R_SubdividePatchToGrid / RB_SurfaceGrid ------------
    {
        // 3x3: ein Bogen in x (Mitte 64 hoch), in y flach; Licht 100 + 10 i + 20 j.
        gras::PatchPunkt pp[9];
        for (int j = 0; j < 3; ++j) {
            for (int i = 0; i < 3; ++i) {
                gras::PatchPunkt& p = pp[j * 3 + i];
                p.xyz[0] = 64.0F * static_cast<float>(i);
                p.xyz[1] = 64.0F * static_cast<float>(j);
                p.xyz[2] = (i == 1) ? 64.0F : 0.0F;
                p.normal[2] = 1.0F;
                p.farbe[2] = static_cast<std::uint8_t>(100 + 10 * i + 20 * j);
                p.farbe[3] = 255;
            }
        }
        const gras::PatchGitter g = gras::unterteilePatch(3, 3, pp);
        pruefe(g.breite == 9 && g.hoehe == 2, "Patch: 9 x 2 (zweimal unterteilt, die flache Richtung faellt weg)");
        if (g.breite == 9 && g.hoehe == 2) {
            pruefe(nah(g.fehlerBreite[1], 0.625F, 1e-5F) && nah(g.fehlerBreite[2], 0.139754F, 1e-5F) &&
                       nah(g.fehlerBreite[3], 0.515388F, 1e-5F) && nah(g.fehlerBreite[4], 0.0F),
                   "Patch: widthLodError 0.625 / 0.1398 / 0.5154 / 0");
            const gras::PatchPunkt& p1 = g.punkte[1];
            pruefe(nah(p1.xyz[0], 16.0F) && nah(p1.xyz[2], 14.0F) && nah(p1.normal[0], -0.59616F, 1e-4F) &&
                       nah(p1.normal[2], 0.80286F, 1e-4F) && p1.farbe[2] == 102U,
                   "Patch: Punkt 1 (16, 0, 14), Normale aus MakeMeshNormals, Licht 102");
            const gras::PatchPunkt& p12 = g.punkte[12];
            pruefe(nah(p12.xyz[0], 48.0F) && nah(p12.xyz[1], 128.0F) && nah(p12.xyz[2], 30.0F) &&
                       nah(p12.normal[2], 0.97097F, 1e-4F) && p12.farbe[2] == 146U,
                   "Patch: Punkt 12 (48, 128, 30), Licht 146");
            pruefe(nah(g.lodMitte[2], 16.0F) && nah(g.lodRadius, 91.913F, 1e-2F), "Patch: lodOrigin und lodRadius");
            std::vector<int> sp;
            std::vector<int> ze;
            const float vorn[3] = {0.0F, 1.0F, 0.0F};
            const float nahAuge[3] = {64.0F, -100.0F, 32.0F};
            gras::gitterLod(g, nahAuge, vorn, sp, ze);
            pruefe(sp.size() == 9U && ze.size() == 2U, "Patch nah (lodError 3.47): alle neun Spalten");
            const float fernAuge[3] = {64.0F, -3000.0F, 32.0F};
            gras::gitterLod(g, fernAuge, vorn, sp, ze);
            pruefe(sp.size() == 3U && sp[1] == 4 && ze.size() == 2U, "Patch fern (lodError 0.084): Spalten 0, 4, 8");
            std::vector<gras::Ecke> d;
            gras::gitterDreiecke(g, sp, ze, d);
            pruefe(d.size() == 12U && nah(d[1].xyz[1], 128.0F) && nah(d[2].xyz[0], 64.0F) && nah(d[2].xyz[2], 32.0F) &&
                       nah(d[3].xyz[0], 64.0F) && nah(d[5].xyz[1], 128.0F) && nah(d[5].xyz[0], 64.0F),
                   "Patch: Dreiecke v2 v3 v1 / v1 v3 v4 wie RB_SurfaceGrid");
        }
    }
    pruefe(nah(gras::reichweiteFaktor(80.0F, 80.0F), 1.0F) &&
               nah(gras::reichweiteFaktor(40.0F, 80.0F), std::tan(40.0F * 3.14159265F / 180.0F) /
                                                             std::tan(20.0F * 3.14159265F / 180.0F)),
           "rangescalefactor: Zoom zeigt die Bilder weiter weg");

    std::printf("%d Fehler\n", g_fehler);
    return g_fehler == 0 ? 0 : 1;
}
