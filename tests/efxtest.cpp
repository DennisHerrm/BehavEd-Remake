// efxtest.cpp - Effekte lesen und zu Vierecken machen.
//
// Der Leser und die Simulation stammen aus efxed und sind dort gegen
// FxScheduler.cpp und FxPrimitives.cpp gebaut. Hier wird deshalb nicht die
// Physik geprueft, sondern das, was in behaved dazukam: dass aus einem Ort
// und einer Groesse vier Ecken werden, dass dieselbe Zeit dasselbe Bild
// ergibt, und dass die Menge nicht ausufert.
#include "bhed/efx/curve.h"
#include "bhed/efx/effect.h"
#include "bhed/efx/sim.h"
#include "bhed/efx/vec3.h"
#include "bhed/efxdraw.h"
#include "bhed/efx/io.h"
#include "bhed/bspgeo.h"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace {

int g_fails = 0;

bool near(float a, float b, float tol) {
    return (a > b ? a - b : b - a) < tol;
}
void expect(const char* what, bool ok) {
    if (!ok) {
        ++g_fails;
    }
    std::printf("  %-4s %s\n", ok ? "ok" : "FEHL", what);
}

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

// Ein Effekt mit einer einzigen Partikelgruppe, damit die Zahlen bekannt
// sind. Geschrieben wie eine echte .efx.
const char* kEiner =
    "Particle\n"
    "{\n"
    "  count 4\n"
    "  life 1000\n"
    // Gewuerfelte Geschwindigkeit - ohne die haette der Effekt gar keinen
    // Zufall, und die Probe auf den Ausgangswert unten waere sinnlos: sie
    // schlug zuerst an, weil ALLE Werte fest waren und zwei Ausgangswerte
    // deshalb dasselbe ergeben MUSSTEN. Nicht der Zeichner war falsch,
    // sondern der Beispieleffekt.
    // Die Schreibweise ist die der echten Dateien: drei Zahlen fuer einen
    // festen Wert, SECHS fuer einen Bereich - ohne Klammern. Mein erster
    // Versuch schrieb "( -50 -50 -50 ) ( 50 50 50 )", und der Leser
    // ueberging das Feld stillschweigend. Die Probe schlug daraufhin an,
    // und zwar zu Recht: ohne Zufall MUESSEN zwei Ausgangswerte dasselbe
    // ergeben. Falsch war die Probe, nicht der Zeichner.
    "  origin 0 0 0\n"
    "  velocity -50 -50 -50 50 50 50\n"
    "  size\n"
    "  {\n"
    "    start 10\n"
    "  }\n"
    "  rgb\n"
    "  {\n"
    "    start 1 1 1\n"
    "  }\n"
    "  alpha\n"
    "  {\n"
    "    start 1\n"
    "  }\n"
    "  shader\n"
    "  [\n"
    "    gfx/misc/steam\n"
    "  ]\n"
    "}\n";

void probenRc569() {
    const float right[3] = {0.0F, 1.0F, 0.0F};
    const float up[3] = {0.0F, 0.0F, 1.0F};

    // =====================================================================
    // Ab hier: rc569 - Blitz, Vollbildblitz, Emitter, Folgeeffekte,
    // FX_RELATIVE und useBBox. Alle gegen HANDWERTE oder gegen die Regeln
    // der Engine, nicht gegen "es sieht irgendwie aus".
    // =====================================================================

    // Ausdehnung eines Netzes: der groesste Abstand einer Ecke von der
    // Geraden a->b und der groesste Abstand von a entlang der Geraden.
    struct Abweichung {
        float quer = 0.0F;
        float laengs = 0.0F;
    };
    auto abweichung = [](const bhed::BspMesh& m, const float a[3],
                         const float b[3]) {
        Abweichung w;
        float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        for (float& c : d) {
            c /= l;
        }
        for (const auto& v : m.verts) {
            const float p[3] = {v.xyz[0] - a[0], v.xyz[1] - a[1], v.xyz[2] - a[2]};
            const float t = p[0] * d[0] + p[1] * d[1] + p[2] * d[2];
            const float q[3] = {p[0] - d[0] * t, p[1] - d[1] * t, p[2] - d[2] * t};
            w.quer = std::max(w.quer, std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]));
            w.laengs = std::max(w.laengs, t);
        }
        return w;
    };
    auto gleicheNetze = [](const bhed::BspMesh& x, const bhed::BspMesh& y) {
        if (x.verts.size() != y.verts.size() || x.indexes != y.indexes) {
            return false;
        }
        for (std::size_t i = 0; i < x.verts.size(); ++i) {
            for (int k = 0; k < 3; ++k) {
                if (x.verts[i].xyz[k] != y.verts[i].xyz[k]) {
                    return false;
                }
            }
        }
        return true;
    };

    // --- Electricity: gezackt, wiederholbar, verzweigt ---------------------
    //
    // RB_SurfaceElectricity/DoBoltSeg (tr_surface.cpp:768 ff.): je 16
    // Einheiten ein Schritt, je Schritt ApplyShape mit Tiefe 2 - also
    // 3 * 3 = 9 Baender. Ein Blitz von 200 Einheiten hat die Schritte
    // 16, 32, ... 192 (i <= dis), das sind 12, also 108 Vierecke.
    //
    // origin2 "200 0 0" ist OERTLICH: der Effekt zeigt mit seiner Vorgabe
    // nach oben (normal 0 0 1), also liegt das Ende 200 Einheiten ueber dem
    // Anfang.
    auto blitzText = [](const char* flags, const char* chaos) {
        std::string t =
            "Electricity\n{\n"
            "\tlife\t\t1000\n"
            "\torigin2\t\t200 0 0\n"
            "\tsize\n\t{\n\t\tstart 2\n\t}\n"
            "\tshaders\n\t[\n\t\tgfx/misc/blueLine\n\t]\n";
        t += std::string("\tbounce\t\t") + chaos + "\n";
        if (flags[0] != '\0') {
            t += std::string("\tflags\t\t") + flags + "\n";
        }
        t += "}\n";
        return t;
    };
    {
        const auto r = bhed::efx::read(blitzText("", "1"));
        expect("die Blitzdatei ist lesbar", !r.effect.primitives.empty());
        if (!r.effect.primitives.empty()) {
            bhed::EffectInstance in;
            in.effect = &r.effect;
            in.seed = 21U;
            const bhed::BspMesh m = bhed::buildEffectMesh({in}, 300.0, right, up);
            std::printf("     Blitz 200 Einheiten: %zu Vierecke\n",
                        m.indexes.size() / 6U);
            expect("ein Blitz von 200 Einheiten hat 12 * 9 = 108 Baender",
                   m.indexes.size() == 108U * 6U);
            const float a[3] = {0.0F, 0.0F, 0.0F};
            const float b[3] = {0.0F, 0.0F, 200.0F};
            const Abweichung w = abweichung(m, a, b);
            std::printf("     Blitz: quer %.1f, laengs %.1f\n",
                        static_cast<double>(w.quer), static_cast<double>(w.laengs));
            // Gerade waere quer = size = 2. Die Zacken (off startet bei
            // 10 10 10, dazu Chaos 1 je Schritt bis 7 Einheiten) liegen
            // deutlich darueber.
            expect("er ist gezackt, nicht gerade", w.quer > 5.0F);
            expect("und reicht bis ans Ende", near(w.laengs, 200.0F, 8.0F));
            // Zurueckspulen: dasselbe Bild, egal was dazwischen lag.
            const bhed::BspMesh spaeter = bhed::buildEffectMesh({in}, 700.0, right, up);
            const bhed::BspMesh nochmal = bhed::buildEffectMesh({in}, 300.0, right, up);
            expect("dieselbe Zeit ergibt denselben Blitz", gleicheNetze(m, nochmal));
            // Die Grobform steht ueber das Leben (e->frame wird einmal
            // gewuerfelt, FxPrimitives.cpp:890).
            expect("und die Form steht ueber sein Leben", gleicheNetze(m, spaeter));
            bhed::EffectInstance anders = in;
            anders.seed = 22U;
            const bhed::BspMesh m2 = bhed::buildEffectMesh({anders}, 300.0, right, up);
            expect("ein anderer Samen ergibt einen anderen Blitz",
                   !gleicheNetze(m, m2));
            // Ohne Chaos: Zacken nur aus ApplyShape und dem Anfangsversatz,
            // und die sind kleiner.
            const auto r0 = bhed::efx::read(blitzText("", "0"));
            bhed::EffectInstance ruhig = in;
            ruhig.effect = &r0.effect;
            const Abweichung w0 = abweichung(
                bhed::buildEffectMesh({ruhig}, 300.0, right, up), a, b);
            expect("Chaos 0 zackt weniger als Chaos 1", w0.quer < w.quer);
        }
    }
    {
        // FX_BRANCH heisst in der Datei "usePhysics". Verzweigt wird nur im
        // ersten Fuenftel (1 - perc > 0.8) mit 7 % je Schritt, hoechstens
        // dreimal. Ein langer Blitz (1000 Einheiten, 62 Schritte, davon 12
        // im ersten Fuenftel) verzweigt also meistens, aber nicht immer.
        auto lang = [](const char* flags) {
            std::string t =
                "Electricity\n{\n\tlife\t\t1000\n\torigin2\t\t1000 0 0\n"
                "\tbounce\t\t1\n\tsize\n\t{\n\t\tstart 2\n\t}\n";
            if (flags[0] != '\0') {
                t += std::string("\tflags\t\t") + flags + "\n";
            }
            return t + "}\n";
        };
        const auto rOhne = bhed::efx::read(lang(""));
        const auto rMit = bhed::efx::read(lang("usePhysics"));
        int verzweigt = 0;
        bool ohneNie = true;
        bool hoechstensDrei = true;
        std::size_t grund = 0;
        for (unsigned sd = 1U; sd <= 32U; ++sd) {
            bhed::EffectInstance a;
            a.effect = &rOhne.effect;
            a.seed = sd;
            bhed::EffectInstance b = a;
            b.effect = &rMit.effect;
            const std::size_t qa =
                bhed::buildEffectMesh({a}, 100.0, right, up).indexes.size() / 6U;
            const std::size_t qb =
                bhed::buildEffectMesh({b}, 100.0, right, up).indexes.size() / 6U;
            grund = qa;
            ohneNie = ohneNie && qa == 62U * 9U;
            if (qb > qa) {
                ++verzweigt;
            }
            // Ein Ast ist hoechstens halb so lang wie der Rest plus 80
            // Streuung - grosszuegig: drei Aeste zu je hoechstens 45
            // Schritten.
            hoechstensDrei = hoechstensDrei && qb <= qa + 3U * 45U * 9U;
        }
        std::printf("     langer Blitz: %zu Vierecke, %d von 32 verzweigt\n",
                    grund, verzweigt);
        expect("ohne FX_BRANCH genau 62 * 9 Baender", ohneNie);
        expect("mit FX_BRANCH verzweigt ein Teil der Blitze",
               verzweigt > 0 && verzweigt < 32);
        expect("und nie mehr als drei Aeste", hoechstensDrei);
    }
    {
        // FX_TAPER ("useModel"): radius * (1 - perc^2), also am Ende null.
        // FX_GROW ("useBBox"): bei halbem Leben halb so lang.
        const auto rT = bhed::efx::read(blitzText("useModel", "0"));
        const auto rG = bhed::efx::read(blitzText("useBBox", "0"));
        bhed::EffectInstance t;
        t.effect = &rT.effect;
        t.seed = 5U;
        const bhed::BspMesh mt = bhed::buildEffectMesh({t}, 300.0, right, up);
        // Die Breite eines Bandes: Abstand der beiden Ecken am selben Ende.
        float vorneBreit = 0.0F;
        float hintenBreit = 1e9F;
        for (std::size_t i = 0; i + 3 < mt.verts.size(); i += 4) {
            for (std::size_t e = 0; e < 4; e += 2) {
                const auto& p = mt.verts[i + (e == 0 ? 0U : 2U)];
                const auto& q = mt.verts[i + (e == 0 ? 1U : 3U)];
                const float dx = p.xyz[0] - q.xyz[0];
                const float dy = p.xyz[1] - q.xyz[1];
                const float dz = p.xyz[2] - q.xyz[2];
                const float br = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float z = (p.xyz[2] + q.xyz[2]) * 0.5F;
                if (z < 40.0F) {
                    vorneBreit = std::max(vorneBreit, br);
                }
                if (z > 196.0F) {
                    hintenBreit = std::min(hintenBreit, br);
                }
            }
        }
        std::printf("     verjuengt: vorn %.2f, an der Spitze %.2f\n",
                    static_cast<double>(vorneBreit), static_cast<double>(hintenBreit));
        expect("FX_TAPER: vorn voll breit (2 * size)", vorneBreit > 3.0F);
        expect("FX_TAPER: an der Spitze fast null", hintenBreit < 0.5F);

        bhed::EffectInstance g;
        g.effect = &rG.effect;
        g.seed = 5U;
        const float a[3] = {0.0F, 0.0F, 0.0F};
        const float b[3] = {0.0F, 0.0F, 200.0F};
        const Abweichung halb =
            abweichung(bhed::buildEffectMesh({g}, 500.0, right, up), a, b);
        const Abweichung voll =
            abweichung(bhed::buildEffectMesh({g}, 999.0, right, up), a, b);
        std::printf("     wachsend: bei halbem Leben %.1f, am Ende %.1f\n",
                    static_cast<double>(halb.laengs), static_cast<double>(voll.laengs));
        expect("FX_GROW: bei halbem Leben halb so lang",
               halb.laengs > 90.0F && halb.laengs < 115.0F);
        expect("FX_GROW: am Ende ganz", voll.laengs > 190.0F);
    }

    // --- Flash: CFlash::Init und CFlash::Draw ------------------------------
    //
    // Handrechnung, Blitz im Ursprung, Auge bei (-100, 0, 0) mit Blick
    // nach +x:
    //     dis = 100, mod = dot(dif, fwd) = 1   (>= 0.5, bleibt)
    //     mod *= 1 - 100^2 / 600^2 = 35/36 = 0.97222
    //     rgb start 1 0.5 0.25, end 0 0 0, linear:
    //       t = 0    -> 0.97222 0.48611 0.24306
    //       t = 500  -> die Haelfte
    {
        const char* text =
            "Flash\n{\n"
            "\tlife\t\t1000\n"
            "\trgb\n\t{\n\t\tstart 1 0.5 0.25\n\t\tend 0 0 0\n\t\tflags linear\n\t}\n"
            // Alpha und size stehen in den echten Dateien - FX_AddFlash
            // liest sie nicht. Sie duerfen nichts aendern.
            "\talpha\n\t{\n\t\tstart 0.1\n\t}\n"
            "\tshaders\n\t[\n\t\tgfx/effects/whiteFlash\n\t]\n"
            "}\n";
        const auto r = bhed::efx::read(text);
        expect("die Flash-Datei ist lesbar", !r.effect.primitives.empty());
        bhed::EffectInstance in;
        in.effect = &r.effect;
        in.seed = 9U;
        const float auge[3] = {-100.0F, 0.0F, 0.0F};
        const float blick[3] = {1.0F, 0.0F, 0.0F};
        const auto f0 = bhed::effectScreenFlashesAt({in}, 0.0, auge, blick);
        expect("im Blick: genau ein Blitz", f0.size() == 1U);
        if (f0.size() == 1U) {
            std::printf("     Blitz bei 0 ms: %.4f %.4f %.4f\n",
                        static_cast<double>(f0[0].rgb[0]),
                        static_cast<double>(f0[0].rgb[1]),
                        static_cast<double>(f0[0].rgb[2]));
            expect("Farbe mal mod (35/36)",
                   near(f0[0].rgb[0], 0.97222F, 0.002F) &&
                       near(f0[0].rgb[1], 0.48611F, 0.002F) &&
                       near(f0[0].rgb[2], 0.24306F, 0.002F));
            expect("mit dem Shader aus der Datei, klein",
                   f0[0].shader == "gfx/effects/whiteflash");
        }
        const auto f5 = bhed::effectScreenFlashesAt({in}, 500.0, auge, blick);
        expect("bei 500 ms die Haelfte (linear)",
               f5.size() == 1U && near(f5[0].rgb[0], 0.48611F, 0.003F));
        expect("nach dem Leben nichts",
               bhed::effectScreenFlashesAt({in}, 1001.0, auge, blick).empty());
        // Hinter dem Auge und weiter als 100: mod = 0.
        // Auge bei x = 200, Blick nach +x: der Blitz im Ursprung liegt
        // hinter ihm (mod = -1).
        const float hinten[3] = {200.0F, 0.0F, 0.0F};
        expect("hinter dem Blick und weiter als 100: kein Blitz",
               bhed::effectScreenFlashesAt({in}, 0.0, hinten, blick).empty());
        // Hinter dem Blick, aber naeher als 100: mod = -1 + 1.1 = 0.1,
        // mal 1 - 50^2/600^2 = 0.99306 -> 0.0993.
        const float dicht[3] = {50.0F, 0.0F, 0.0F};
        const float blickWeg[3] = {1.0F, 0.0F, 0.0F};
        const auto fn = bhed::effectScreenFlashesAt({in}, 0.0, dicht, blickWeg);
        expect("hinter dem Blick, aber naeher als 100: schwach (0.0993)",
               fn.size() == 1U && near(fn[0].rgb[0], 0.0993F, 0.002F));
        const float fern[3] = {-700.0F, 0.0F, 0.0F};
        expect("weiter als 600: kein Blitz",
               bhed::effectScreenFlashesAt({in}, 0.0, fern, blick).empty());
        // Das Sprite: 8 Einheiten vor dem Auge, Halbmesser 8*tan(45) = 8
        // bei fov_x 90, Alpha 255.
        bhed::BspMesh netz;
        const float rechts[3] = {0.0F, -1.0F, 0.0F};
        const float oben[3] = {0.0F, 0.0F, 1.0F};
        bhed::appendScreenFlashes(netz, f0, auge, blick, rechts, oben, 90.0F);
        bool lage = netz.verts.size() == 4U && netz.batches.size() == 1U;
        for (const auto& v : netz.verts) {
            lage = lage && near(v.xyz[0], -92.0F, 0.01F) &&
                   near(std::fabs(v.xyz[1]), 8.0F, 0.01F) &&
                   near(std::fabs(v.xyz[2]), 8.0F, 0.01F) && v.colour[3] == 255;
        }
        expect("das Sprite liegt 8 vor dem Auge und fuellt fov_x", lage);
        const bhed::ScreenFlashOverlay ov = bhed::screenFlashOverlay(f0);
        expect("die Summe als Ueberlagerung", ov.aktiv && near(ov.rgb[0], 0.97222F, 0.002F));
    }

    // --- Emitter: das Modell, je Teilchen --------------------------------
    //
    // count 3, zwei Modelle, size 0 -> 2 linear: bei halbem Leben ist der
    // Massstab 1. Bis rc569 gab es EIN Modell mit der Startgroesse (hier 0,
    // die als 1 genommen wurde).
    {
        const char* text =
            "Emitter\n{\n"
            "\tflags\t\tuseModel\n"
            "\tcount\t\t3\n"
            "\tlife\t\t1000\n"
            "\tvelocity\t\t100 0 0\n"
            "\tangleDelta\t\t0 100 0\n"
            "\tsize\n\t{\n\t\tstart 0\n\t\tend 2\n\t\tflags linear\n\t}\n"
            "\tmodels\n\t[\n\t\tmodels/A.md3\n\t\tmodels/b.md3\n\t]\n"
            "}\n";
        const auto r = bhed::efx::read(text);
        bhed::EffectInstance in;
        in.effect = &r.effect;
        in.seed = 17U;
        const auto m = bhed::emitterModelsAt({in}, 500.0, nullptr);
        expect("drei Emitter, drei Modelle", m.size() == 3U);
        bool gut = m.size() == 3U;
        for (const auto& d : m) {
            gut = gut && near(d.scale, 1.0F, 0.01F) &&
                  (d.modelPath == "models/a.md3" || d.modelPath == "models/b.md3") &&
                  // velocity oertlich: x zeigt nach oben -> 50 Einheiten hoch
                  near(d.origin[2], 50.0F, 0.5F) &&
                  // yaw = vectoangles(0 0 1) (0) + 100 * 0.01 * 500 = 500
                  near(d.angles[1], 500.0F, 0.01F) &&
                  // pitch = -90 aus vectoangles der Richtung nach oben
                  near(d.angles[0], -90.0F, 0.01F);
        }
        expect("Massstab aus der Kurve, Ort, Drehung und Modell stimmen", gut);
        expect("bei Groesse null nichts (Anfang)",
               bhed::emitterModelsAt({in}, 0.0, nullptr).empty());
    }

    // --- Folgeeffekte: mehrstufig, begrenzt, gleich beim Zurueckspulen -----
    {
        const auto rA = bhed::efx::read("FxRunner\n{\n\tplayfx\n\t[\n\t\tt/b\n\t]\n}\n");
        const auto rB = bhed::efx::read(
            "FxRunner\n{\n\tdelay\t\t50\n\tplayfx\n\t[\n\t\tt/c\n\t]\n}\n");
        const auto rC = bhed::efx::read(
            "Particle\n{\n\tlife\t\t1000\n\tsize\n\t{\n\t\tstart 4\n\t}\n}\n");
        const auto rD = bhed::efx::read("FxRunner\n{\n\tplayfx\n\t[\n\t\tt/d\n\t]\n}\n");
        // Emitter mit Fahne: 100 Einheiten je Sekunde nach oben, alle 10
        // Einheiten ein Effekt.
        const auto rE = bhed::efx::read(
            "Emitter\n{\n\tflags\t\temitFx\n\tlife\t\t2000\n"
            "\tvelocity\t\t100 0 0\n\tdensity\t\t10\n"
            "\temitfx\n\t[\n\t\tt/c\n\t]\n}\n");
        // killOnImpact OHNE Wand: am Lebensende trotzdem ein Todeseffekt
        // (FX_Add loescht das Flag bei regulaerem Ablauf, FxUtil.cpp:238).
        const auto rK = bhed::efx::read(
            "Particle\n{\n\tflags\t\tdeathFx impactKills\n\tlife\t\t300\n"
            "\tdeathfx\n\t[\n\t\tt/c\n\t]\n}\n");
        const std::map<std::string, const bhed::efx::Effect*> alle = {
            {"t/b", &rB.effect}, {"t/c", &rC.effect}, {"t/d", &rD.effect}};
        const bhed::EffectLookup nach = [&alle](const std::string& n) {
            const auto it = alle.find(n);
            return it == alle.end() ? nullptr : it->second;
        };
        bhed::EffectInstance a;
        a.effect = &rA.effect;
        a.seed = 3U;
        const auto f = bhed::effectFollowUpsAt({a}, 100.0, nullptr, nach);
        expect("zwei Stufen: A startet B, B startet C", f.size() == 2U);
        if (f.size() == 2U) {
            expect("B sofort, C nach dessen delay 50",
                   f[0].effect == &rB.effect && f[1].effect == &rC.effect &&
                       near(static_cast<float>(f[1].startMs), 50.0F, 0.01F));
        }
        const auto f1 = bhed::effectFollowUpsAt({a}, 100.0, nullptr, nach, 1);
        expect("mit Tiefe 1 nur B", f1.size() == 1U);
        // Ein Effekt, der sich selbst startet: die Engine bremst nur ueber
        // ihren Vorrat, wir ueber die Tiefe.
        // (Bei 30 ms - ein FxRunner lebt 50 ms, danach zaehlt er nicht mehr.)
        bhed::EffectInstance d;
        d.effect = &rD.effect;
        const auto fd = bhed::effectFollowUpsAt({d}, 30.0, nullptr, nach);
        expect("eine Selbstausloesung endet an kMaxFolgeTiefe",
               fd.size() == static_cast<std::size_t>(bhed::kMaxFolgeTiefe));
        const auto fd2 = bhed::effectFollowUpsAt({d}, 30.0, nullptr, nach, 20, 5U);
        expect("und an der Gesamtzahl", fd2.size() == 5U);

        bhed::EffectInstance e;
        e.effect = &rE.effect;
        e.seed = 8U;
        const auto fe = bhed::effectImpactsAt({e}, 1000.0, nullptr);
        std::printf("     Fahne nach 100 Einheiten: %zu Effekte\n", fe.size());
        expect("die Fahne: etwa alle 10 Einheiten einer",
               fe.size() >= 8U && fe.size() <= 11U);
        bool geordnet = true;
        for (std::size_t i = 1; i < fe.size(); ++i) {
            geordnet = geordnet && fe[i].origin[2] > fe[i - 1].origin[2] &&
                       fe[i].startMs > fe[i - 1].startMs;
        }
        expect("entlang der Bahn und der Zeit", geordnet);
        const auto fe2 = bhed::effectImpactsAt({e}, 1000.0, nullptr);
        bool gleich = fe.size() == fe2.size();
        for (std::size_t i = 0; gleich && i < fe.size(); ++i) {
            gleich = fe[i].seed == fe2[i].seed && fe[i].origin[2] == fe2[i].origin[2];
        }
        expect("und beim zweiten Mal dieselbe", gleich);

        bhed::EffectInstance k;
        k.effect = &rK.effect;
        k.seed = 4U;
        const auto fk = bhed::effectImpactsAt({k}, 500.0, nullptr);
        expect("killOnImpact ohne Aufprall: Todeseffekt am Lebensende",
               fk.size() == 1U && fk[0].vomTod() &&
                   near(static_cast<float>(fk[0].startMs), 300.0F, 0.01F));
    }

    // --- FX_RELATIVE: am AKTUELLEN Anker ---------------------------------
    //
    // Ein Teilchen ohne Bewegung, 10 Einheiten vor dem Anker. Der Anker
    // faehrt mit 1 Einheit je Millisekunde nach +y.
    //   relativ:     bei 500 ms bei y = 500 (haengt mit)
    //   nicht rel.:  bei y = 0 (dort, wo der Anker bei der Geburt war)
    {
        const auto r = bhed::efx::read(
            "Particle\n{\n\tlife\t\t1000\n\torigin\t\t10 0 0\n"
            "\tsize\n\t{\n\t\tstart 4\n\t}\n}\n");
        bhed::EffectInstance in;
        in.effect = &r.effect;
        in.anker = [](double ms, float org[3], float fwd[3]) {
            org[0] = 0.0F;
            org[1] = static_cast<float>(ms);
            org[2] = 0.0F;
            fwd[0] = 1.0F;
            fwd[1] = 0.0F;
            fwd[2] = 0.0F;
            return true;
        };
        auto mitte = [](const bhed::BspMesh& m) {
            float c[3] = {0.0F, 0.0F, 0.0F};
            for (const auto& v : m.verts) {
                for (int q = 0; q < 3; ++q) {
                    c[q] += v.xyz[q] / static_cast<float>(m.verts.size());
                }
            }
            return std::array<float, 3>{c[0], c[1], c[2]};
        };
        in.relativ = true;
        const bhed::BspMesh mr = bhed::buildEffectMesh({in}, 500.0, right, up);
        in.relativ = false;
        const bhed::BspMesh mf = bhed::buildEffectMesh({in}, 500.0, right, up);
        const auto cr = mitte(mr);
        const auto cf = mitte(mf);
        std::printf("     relativ y=%.1f, fest y=%.1f\n",
                    static_cast<double>(cr[1]), static_cast<double>(cf[1]));
        expect("FX_RELATIVE haengt am aktuellen Anker",
               !mr.verts.empty() && near(cr[1], 500.0F, 0.1F) && near(cr[0], 10.0F, 0.1F));
        expect("ohne FX_RELATIVE bleibt es am Geburtsort",
               !mf.verts.empty() && near(cf[1], 0.0F, 0.1F) && near(cf[0], 10.0F, 0.1F));
    }

    // --- flugbahnWeiter: fortgesetzt = von vorn, bitgleich -----------------
    //
    // Der Merkzettel in efxdraw.cpp setzt Bahnen Bild fuer Bild fort, statt
    // sie neu zu schreiten. Das darf NICHTS am Ergebnis aendern - auch
    // nicht beim Zurueckspulen (frueheres Alter: von vorn) und nicht bei
    // Altern, die zwischen zwei Schritten liegen.
    {
        auto boden = [](const bhed::efx::camera::Vec3& von,
                        const bhed::efx::camera::Vec3& bis,
                        bhed::efx::camera::Vec3& punkt,
                        bhed::efx::camera::Vec3& normale) {
            if (von.z >= 0.0F && bis.z < 0.0F) {
                const float f = von.z / (von.z - bis.z);
                punkt = bhed::efx::camera::Vec3{von.x + (bis.x - von.x) * f,
                                                von.y + (bis.y - von.y) * f, 0.0F};
                normale = bhed::efx::camera::Vec3{0.0F, 0.0F, 1.0F};
                return true;
            }
            return false;
        };
        const bhed::efx::camera::Vec3 start{0.0F, 0.0F, 300.0F};
        const bhed::efx::camera::Vec3 v{40.0F, 0.0F, 120.0F};
        const bhed::efx::camera::Vec3 a{0.0F, 0.0F, 0.0F};
        bhed::efx::sim::FlugZustand z;
        bool gleich = true;
        int liegtBei = 0;
        const float alter[] = {0.3F, 0.7F, 0.7013F, 2.0F, 1.0F, 3.5F, 3.5F, 9.0F};
        for (const float t : alter) {
            const auto w = bhed::efx::sim::flugbahnWeiter(start, v, a, -300.0F, t, 0.6F,
                                                           false, boden, z);
            const auto f = bhed::efx::sim::flugbahn(start, v, a, -300.0F, t, 0.6F,
                                                     false, boden);
            gleich = gleich && w.position.x == f.position.x &&
                     w.position.z == f.position.z && w.aufpralle == f.aufpralle &&
                     w.liegt == f.liegt && w.trefferSekunde == f.trefferSekunde;
            if (w.liegt) {
                ++liegtBei;
            }
        }
        expect("flugbahnWeiter = flugbahn, vorwaerts, rueckwaerts, zwischen Schritten",
               gleich);
        expect("und das Teilchen kommt dabei zur Ruhe", liegtBei > 0);
    }

    // --- Physik in WELTkoordinaten, jeder Aufprall, useBBox ---------------
    //
    // Eine erfundene Karte: ein Boden bei z = 0, NUR um x = 5000 herum.
    // Bis rc569 lief der Spurtest mit dem Versatz zum Effekt statt mit der
    // Weltlage - bei einem Effekt ueber (5000, 0, 100) suchte er den Boden
    // um den Nullpunkt der Karte herum, fand keinen, und das Teilchen fiel
    // hindurch.
    {
        bhed::BspGeometry geo;
        geo.planes.push_back(bhed::BspGeometry::Plane{{0.0F, 0.0F, 1.0F}, -1.0e6F});
        bhed::BspGeometry::Node knoten;
        knoten.plane = 0;
        knoten.children[0] = -1;
        knoten.children[1] = -1;
        geo.nodes.push_back(knoten);
        bhed::BspGeometry::Leaf blatt;
        blatt.numSurfaces = 1;
        geo.leafs.push_back(blatt);
        geo.leafSurfaces.push_back(0);
        const float ecken[4][2] = {{4000.0F, -1000.0F}, {6000.0F, -1000.0F},
                                   {6000.0F, 1000.0F}, {4000.0F, 1000.0F}};
        for (const auto& e : ecken) {
            bhed::BspVertex v;
            v.xyz[0] = e[0];
            v.xyz[1] = e[1];
            v.normal[2] = 1.0F;
            geo.verts.push_back(v);
        }
        geo.indexes = {0, 1, 2, 0, 2, 3};
        bhed::BspSurface sf;
        sf.type = bhed::BspSurface::Type::Planar;
        sf.shader = -1;
        sf.numVerts = 4;
        sf.numIndexes = 6;
        sf.mins[0] = 4000.0F;
        sf.mins[1] = -1000.0F;
        sf.mins[2] = -1.0F;
        sf.maxs[0] = 6000.0F;
        sf.maxs[1] = 1000.0F;
        sf.maxs[2] = 1.0F;
        geo.surfaces.push_back(sf);

        auto fallText = [](const char* extra) {
            return std::string("Particle\n{\n\tflags\t\tusePhysics impactFx\n"
                               "\tlife\t\t3000\n\tgravity\t\t-800\n\tbounce\t\t0.5\n"
                               "\tsize\n\t{\n\t\tstart 4\n\t}\n"
                               "\timpactfx\n\t[\n\t\tt/staub\n\t]\n") +
                   extra + "}\n";
        };
        const auto rP = bhed::efx::read(fallText(""));
        const auto rB = bhed::efx::read(fallText("\tmin\t\t-5 -5 -5\n\tmax\t\t5 5 5\n"));
        bhed::EffectInstance in;
        in.effect = &rP.effect;
        in.origin[0] = 5000.0F;
        in.origin[2] = 100.0F;
        in.seed = 2U;
        auto hoehe = [&](const bhed::EffectInstance& x, double t) {
            const bhed::BspMesh m = bhed::buildEffectMesh({x}, t, right, up, nullptr, &geo);
            float z = 0.0F;
            for (const auto& v : m.verts) {
                z += v.xyz[2] / static_cast<float>(std::max<std::size_t>(m.verts.size(), 1U));
            }
            return m.verts.empty() ? -9999.0F : z;
        };
        const float zPunkt = hoehe(in, 2500.0);
        bhed::EffectInstance box = in;
        box.effect = &rB.effect;
        const float zKasten = hoehe(box, 2500.0);
        std::printf("     liegt bei z = %.2f (Strahl), %.2f (Kasten -5..5)\n",
                    static_cast<double>(zPunkt), static_cast<double>(zKasten));
        expect("der Boden unter dem Effekt haelt es auf (Weltlage)",
               near(zPunkt, 0.0F, 0.5F));
        expect("useBBox: es liegt um die Kastenhoehe hoeher", near(zKasten, 5.0F, 0.5F));
        // Der Merkzettel fuer fertige Bahnen (efxdraw.cpp, flugMitMerk):
        // bei 2500 ms liegt das Teilchen, das Ergebnis ist gemerkt. Bei
        // 2900 ms kommt es aus dem Merkzettel - und muss BITGLEICH sein mit
        // einer frischen Rechnung. Frisch heisst: eine Kopie der Karte an
        // einer anderen Adresse, also ein anderer Schluessel.
        {
            const bhed::BspGeometry kopie = geo;
            const bhed::BspMesh gemerkt =
                bhed::buildEffectMesh({in}, 2900.0, right, up, nullptr, &geo);
            const bhed::BspMesh frisch =
                bhed::buildEffectMesh({in}, 2900.0, right, up, nullptr, &kopie);
            expect("gemerkte Bahn = frisch gerechnete Bahn (bitgleich)",
                   !gemerkt.verts.empty() && gleicheNetze(gemerkt, frisch));
        }
        // Und eine Bahn, die NOCH FLIEGT (gravity -2: nach fuenf Sekunden
        // erst 25 Einheiten gesunken): 3000 ms rechnen, dann bei 5000 ms
        // fortsetzen - gegen frisch bei 5000 ms.
        {
            const auto rL = bhed::efx::read(
                "Particle\n{\n\tflags\t\tusePhysics\n\tlife\t\t20000\n"
                "\tgravity\t\t-2\n\tsize\n\t{\n\t\tstart 4\n\t}\n}\n");
            bhed::EffectInstance langsam = in;
            langsam.effect = &rL.effect;
            (void)bhed::buildEffectMesh({langsam}, 3000.0, right, up, nullptr, &geo);
            const bhed::BspMesh weiter =
                bhed::buildEffectMesh({langsam}, 5000.0, right, up, nullptr, &geo);
            const bhed::BspGeometry kopie2 = geo;
            const bhed::BspMesh frisch2 =
                bhed::buildEffectMesh({langsam}, 5000.0, right, up, nullptr, &kopie2);
            expect("fortgesetzte Bahn = frisch gerechnete Bahn (bitgleich)",
                   !weiter.verts.empty() && gleicheNetze(weiter, frisch2));
        }
        // Mit Elastizitaet 0.5 springt es mehrmals - und JEDER Aufprall
        // startet den Aufpralleffekt (FxPrimitives.cpp:318), bis es liegt.
        const auto staub = bhed::effectImpactsAt({in}, 2500.0, &geo);
        std::printf("     Aufpralleffekte: %zu\n", staub.size());
        bool amBoden = staub.size() >= 2U;
        for (const auto& s0 : staub) {
            amBoden = amBoden && near(s0.origin[2], 0.0F, 0.01F) &&
                      near(s0.origin[0], 5000.0F, 0.5F) && near(s0.normal[2], 1.0F, 0.01F);
        }
        expect("jeder Aufprall einen, alle auf dem Boden", amBoden);
    }
}

// =====================================================================
// Leistung ohne Bildaenderung: SpurHilfe, Spurbuch, Vorrechnen.
//
// Anlass: md_ta_sith (intro_sith) brauchte im Block "Effekte" bis 1,1 s
// je Bild, beim Springen auf der Zeitleiste ueber 6 s. Die drei Umbauten
// dagegen (bspgeo.cpp SpurHilfe, efx_sim.cpp Spurbuch, efxdraw.cpp
// vorrechnen) duerfen das Bild um KEIN Bit aendern. Diese Proben halten
// jeweils den neuen Weg gegen den alten - an erfundener Geometrie mit
// allen Faellen, die weh tun koennten: gleich weite Treffer auf
// gemeinsamen Kanten, spitze und entartete Dreiecke, Strecken fast in der
// Ebene, Flaechen in mehreren Blaettern, Patches, eine Clip-Flaeche.
// =====================================================================

// Ein fester Zufall fuer die Proben (nicht std::rand: ueberall gleich).
struct ProbenZufall {
    unsigned s = 12345U;
    float next() {
        s = s * 1664525U + 1013904223U;
        return static_cast<float>((s >> 8) & 0xFFFFFFU) / static_cast<float>(0x1000000);
    }
    float range(float a, float b) { return a + (b - a) * next(); }
};

bhed::BspGeometry probenKarte() {
    bhed::BspGeometry g;
    ProbenZufall z;
    // Zwei Blaetter, geteilt an x = 0.
    g.planes.push_back(bhed::BspGeometry::Plane{{1.0F, 0.0F, 0.0F}, 0.0F});
    bhed::BspGeometry::Node knoten;
    knoten.plane = 0;
    knoten.children[0] = -1;   // x >= 0: Blatt 0
    knoten.children[1] = -2;   // x <  0: Blatt 1
    g.nodes.push_back(knoten);
    bhed::BspShader clip;
    clip.name = "textures/system/clip";
    g.shaders.push_back(clip);
    bhed::BspShader wand;
    wand.name = "textures/probe/wand";
    g.shaders.push_back(wand);

    auto neueFlaeche = [&](bhed::BspSurface::Type typ, int shader) {
        bhed::BspSurface s;
        s.type = typ;
        s.shader = shader;
        s.firstVert = static_cast<int>(g.verts.size());
        s.firstIndex = static_cast<int>(g.indexes.size());
        return s;
    };
    auto ecke = [&](float x, float y, float zz) {
        bhed::BspVertex v;
        v.xyz[0] = x;
        v.xyz[1] = y;
        v.xyz[2] = zz;
        v.normal[2] = 1.0F;
        g.verts.push_back(v);
    };
    auto abschliessen = [&](bhed::BspSurface& s) {
        s.numVerts = static_cast<int>(g.verts.size()) - s.firstVert;
        s.numIndexes = static_cast<int>(g.indexes.size()) - s.firstIndex;
        for (int k = 0; k < 3; ++k) {
            s.mins[k] = 1e30F;
            s.maxs[k] = -1e30F;
        }
        for (int i = 0; i < s.numVerts; ++i) {
            for (int k = 0; k < 3; ++k) {
                const float x = g.verts[static_cast<std::size_t>(s.firstVert + i)].xyz[k];
                s.mins[k] = std::min(s.mins[k], x);
                s.maxs[k] = std::max(s.maxs[k], x);
            }
        }
        g.surfaces.push_back(s);
    };
    // 0: Boden, 8x8 Felder auf ganzen Hunderten - 128 Dreiecke, jede
    // Gitterlinie eine gemeinsame Kante.
    {
        bhed::BspSurface s = neueFlaeche(bhed::BspSurface::Type::Planar, 1);
        for (int y = 0; y <= 8; ++y) {
            for (int x = 0; x <= 8; ++x) {
                ecke(-400.0F + 100.0F * static_cast<float>(x),
                     -400.0F + 100.0F * static_cast<float>(y), 0.0F);
            }
        }
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const auto a = static_cast<std::uint32_t>(y * 9 + x);
                g.indexes.insert(g.indexes.end(), {a, a + 1U, a + 10U, a, a + 10U, a + 9U});
            }
        }
        abschliessen(s);
    }
    // 1: Decke, zwei Dreiecke (bleibt beim Weg der Vorlage).
    {
        bhed::BspSurface s = neueFlaeche(bhed::BspSurface::Type::Planar, -1);
        ecke(-400.0F, -400.0F, 300.0F);
        ecke(400.0F, -400.0F, 300.0F);
        ecke(400.0F, 400.0F, 300.0F);
        ecke(-400.0F, 400.0F, 300.0F);
        g.indexes.insert(g.indexes.end(), {0U, 1U, 2U, 0U, 2U, 3U});
        abschliessen(s);
    }
    // 2: die Suppe - 150 zufaellige Dreiecke, jedes zehnte spitz, jedes
    // dreissigste entartet, dazu ein Faecher auf z = 150 mit gemeinsamen
    // Kanten.
    {
        bhed::BspSurface s = neueFlaeche(bhed::BspSurface::Type::TriangleSoup, 1);
        std::uint32_t n = 0;
        for (int i = 0; i < 150; ++i) {
            const float cx = z.range(-300.0F, 300.0F);
            const float cy = z.range(-300.0F, 300.0F);
            const float cz = z.range(10.0F, 290.0F);
            const float r = z.range(5.0F, 80.0F);
            float p[3][3];
            for (auto& q : p) {
                q[0] = cx + z.range(-r, r);
                q[1] = cy + z.range(-r, r);
                q[2] = cz + z.range(-r, r);
            }
            if (i % 10 == 3) {   // spitz: die dritte Ecke fast auf der Kante
                const float t = z.next();
                for (int k = 0; k < 3; ++k) {
                    p[2][k] = p[0][k] + (p[1][k] - p[0][k]) * t + z.range(-0.01F, 0.01F);
                }
            }
            if (i % 30 == 7) {   // entartet: zwei Ecken gleich
                for (int k = 0; k < 3; ++k) {
                    p[2][k] = p[1][k];
                }
            }
            for (const auto& q : p) {
                ecke(q[0], q[1], q[2]);
            }
            g.indexes.insert(g.indexes.end(), {n, n + 1U, n + 2U});
            n += 3U;
        }
        // Acht Daecher auf ganzen Zahlen: zwei geneigte Dreiecke mit
        // gemeinsamem First. Ein senkrechter Strahl auf den First trifft
        // beide gleich weit - und ihre Normalen zeigen verschieden. Hier
        // entscheidet allein die Reihenfolge der Vorlage.
        for (int r = 0; r < 8; ++r) {
            const float x0 = -350.0F + 100.0F * static_cast<float>(r);
            ecke(x0 - 40.0F, -350.0F, 170.0F);
            ecke(x0, -350.0F, 200.0F);
            ecke(x0, -250.0F, 200.0F);
            ecke(x0 + 40.0F, -350.0F, 170.0F);
            g.indexes.insert(g.indexes.end(), {n, n + 1U, n + 2U, n + 1U, n + 3U, n + 2U});
            n += 4U;
        }
        const std::uint32_t mitte = n;
        ecke(0.0F, 0.0F, 150.0F);
        ++n;
        for (int i = 0; i <= 12; ++i) {
            const float w = static_cast<float>(i) * 0.5235988F;
            ecke(std::round(120.0F * std::cos(w)), std::round(120.0F * std::sin(w)), 150.0F);
            ++n;
        }
        for (std::uint32_t i = 0; i < 12U; ++i) {
            g.indexes.insert(g.indexes.end(), {mitte, mitte + 1U + i, mitte + 2U + i});
        }
        abschliessen(s);
    }
    // 3: ein Patch 5x5, gewellt (32 Dreiecke im Kontrollgitter).
    {
        bhed::BspSurface s = neueFlaeche(bhed::BspSurface::Type::Patch, -1);
        s.patchWidth = 5;
        s.patchHeight = 5;
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 5; ++x) {
                ecke(100.0F + 40.0F * static_cast<float>(x),
                     -300.0F + 40.0F * static_cast<float>(y),
                     60.0F + 25.0F * std::sin(static_cast<float>(x + 2 * y)));
                // Je Stuetzpunkt eine eigene Normale: ein Patch meldet die
                // Normale seiner ersten Ecke, gleich weite Treffer zweier
                // Felddreiecke unterscheiden sich also darin.
                g.verts.back().normal[0] = std::sin(static_cast<float>(3 * x + y));
                g.verts.back().normal[1] = std::cos(static_cast<float>(x - 2 * y));
            }
        }
        abschliessen(s);
    }
    // 4: eine Clip-Flaeche mitten im Raum - trifft nie.
    {
        bhed::BspSurface s = neueFlaeche(bhed::BspSurface::Type::Planar, 0);
        ecke(-50.0F, -400.0F, 0.0F);
        ecke(-50.0F, 400.0F, 0.0F);
        ecke(-50.0F, 400.0F, 300.0F);
        ecke(-50.0F, -400.0F, 300.0F);
        g.indexes.insert(g.indexes.end(), {0U, 1U, 2U, 0U, 2U, 3U});
        abschliessen(s);
    }
    // Blatt 0 (x >= 0): Boden, Suppe, Decke, Patch, Suppe noch einmal.
    // Blatt 1 (x <  0): Suppe, Clip, Boden, Decke.
    g.leafSurfaces = {0, 2, 1, 3, 2, 2, 4, 0, 1};
    bhed::BspGeometry::Leaf b0;
    b0.firstSurface = 0;
    b0.numSurfaces = 5;
    bhed::BspGeometry::Leaf b1;
    b1.firstSurface = 5;
    b1.numSurfaces = 4;
    g.leafs = {b0, b1};
    for (int k = 0; k < 3; ++k) {
        g.mins[k] = -400.0F;
        g.maxs[k] = 400.0F;
    }
    g.mins[2] = 0.0F;
    g.maxs[2] = 300.0F;
    return g;
}

bool gleicherTreffer(const bhed::TraceTreffer& a, const bhed::TraceTreffer& b) {
    return a.hit == b.hit && a.surface == b.surface &&
           std::memcmp(a.point, b.point, sizeof(a.point)) == 0 &&
           std::memcmp(a.normal, b.normal, sizeof(a.normal)) == 0 &&
           std::memcmp(&a.fraction, &b.fraction, sizeof(a.fraction)) == 0;
}

bool gleichesErgebnis(const bhed::efx::sim::FlugErgebnis& a,
                      const bhed::efx::sim::FlugErgebnis& b) {
    auto v = [](const bhed::efx::camera::Vec3& x, const bhed::efx::camera::Vec3& y) {
        return std::memcmp(&x, &y, sizeof(x)) == 0;
    };
    bool ok = v(a.position, b.position) && a.gestorben == b.gestorben &&
              a.liegt == b.liegt && a.abpraller == b.abpraller && a.traf == b.traf &&
              v(a.trefferPunkt, b.trefferPunkt) && v(a.trefferNormale, b.trefferNormale) &&
              std::memcmp(&a.trefferSekunde, &b.trefferSekunde, sizeof(float)) == 0 &&
              a.aufpralle == b.aufpralle &&
              std::memcmp(&a.liegtSekunde, &b.liegtSekunde, sizeof(float)) == 0 &&
              std::memcmp(&a.endgueltigAb, &b.endgueltigAb, sizeof(float)) == 0;
    for (int k = 0; k < bhed::efx::sim::FlugErgebnis::kMaxAufpralle; ++k) {
        const auto kk = static_cast<std::size_t>(k);
        ok = ok && v(a.aufprallPunkt[kk], b.aufprallPunkt[kk]) &&
             v(a.aufprallNormale[kk], b.aufprallNormale[kk]) &&
             std::memcmp(&a.aufprallSekunde[kk], &b.aufprallSekunde[kk], sizeof(float)) == 0;
    }
    return ok;
}

bool gleichesNetzBytes(const bhed::BspMesh& x, const bhed::BspMesh& y) {
    if (x.verts.size() != y.verts.size() || x.indexes != y.indexes ||
        x.batches.size() != y.batches.size()) {
        return false;
    }
    for (std::size_t i = 0; i < x.verts.size(); ++i) {
        const bhed::BspVertex& a = x.verts[i];
        const bhed::BspVertex& b = y.verts[i];
        if (std::memcmp(a.xyz, b.xyz, sizeof(a.xyz)) != 0 ||
            std::memcmp(a.st, b.st, sizeof(a.st)) != 0 ||
            std::memcmp(a.normal, b.normal, sizeof(a.normal)) != 0 ||
            std::memcmp(a.colour, b.colour, sizeof(a.colour)) != 0) {
            return false;
        }
    }
    for (std::size_t i = 0; i < x.batches.size(); ++i) {
        const auto& a = x.batches[i];
        const auto& b = y.batches[i];
        if (a.shader != b.shader || a.firstIndex != b.firstIndex ||
            a.numIndexes != b.numIndexes ||
            std::memcmp(&a.shaderTime, &b.shaderTime, sizeof(float)) != 0 ||
            std::memcmp(&a.depthScale, &b.depthScale, sizeof(float)) != 0) {
            return false;
        }
    }
    return true;
}

void probenLeistung() {
    namespace sim = bhed::efx::sim;
    using V = bhed::efx::camera::Vec3;
    const bhed::BspGeometry geo = probenKarte();

    // --- 1. traceRay = traceRayEinfach, bitgleich ------------------------
    {
        struct Dr { float a[3], b[3], c[3]; };
        std::vector<Dr> dreiecke;
        for (const bhed::BspSurface& s : geo.surfaces) {
            if (s.type == bhed::BspSurface::Type::Patch) {
                continue;
            }
            for (int k = 0; k + 2 < s.numIndexes; k += 3) {
                Dr d{};
                const float* q[3];
                for (int j = 0; j < 3; ++j) {
                    q[j] = geo.verts[static_cast<std::size_t>(s.firstVert) +
                                     geo.indexes[static_cast<std::size_t>(s.firstIndex + k + j)]]
                               .xyz;
                }
                std::memcpy(d.a, q[0], sizeof(d.a));
                std::memcpy(d.b, q[1], sizeof(d.b));
                std::memcpy(d.c, q[2], sizeof(d.c));
                dreiecke.push_back(d);
            }
        }
        ProbenZufall z;
        z.s = 777U;
        int unterschiede = 0;
        int treffer = 0;
        int gleichWeit = 0;
        constexpr int kStrecken = 150000;
        for (int i = 0; i < kStrecken; ++i) {
            const Dr& d = dreiecke[static_cast<std::size_t>(z.next() * static_cast<float>(dreiecke.size())) %
                                   dreiecke.size()];
            const float wa = z.next();
            const float wb = z.next() * (1.0F - wa);
            float p[3];
            float e1[3];
            float e2[3];
            for (int k = 0; k < 3; ++k) {
                e1[k] = d.b[k] - d.a[k];
                e2[k] = d.c[k] - d.a[k];
                p[k] = d.a[k] + wa * e1[k] + wb * e2[k];
            }
            float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                          e1[0] * e2[1] - e1[1] * e2[0]};
            const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (nl > 0.0F) {
                for (float& c : n) {
                    c /= nl;
                }
            }
            float dir[3] = {z.range(-1.0F, 1.0F), z.range(-1.0F, 1.0F), z.range(-1.0F, 1.0F)};
            const int art = i % 6;
            float ab = z.range(-20.0F, 20.0F);
            if (art == 1 || art == 2) {
                // fast in der Ebene, knapp daneben
                const float dn = dir[0] * n[0] + dir[1] * n[1] + dir[2] * n[2];
                const float rest = std::pow(10.0F, -2.0F - 6.0F * z.next()) *
                                   (z.next() < 0.5F ? -1.0F : 1.0F);
                for (int k = 0; k < 3; ++k) {
                    dir[k] = dir[k] - dn * n[k] + rest * n[k];
                }
                ab = std::pow(10.0F, -1.0F - 6.0F * z.next()) * (z.next() < 0.5F ? -1.0F : 1.0F);
            }
            if (art == 3) {
                // senkrecht auf ganzen Zahlen: trifft Gitterlinien und Ecken
                // des Bodens, die Firste der Daecher und die Diagonalen des
                // Patches genau - gleich weite Treffer zweier Dreiecke
                const float wahl = z.next();
                if (wahl < 0.34F) {
                    p[0] = std::round(z.range(-4.0F, 4.0F)) * 100.0F +
                           (z.next() < 0.5F ? 0.0F : std::round(z.range(-50.0F, 50.0F)));
                    p[1] = std::round(z.range(-4.0F, 4.0F)) * 100.0F;
                    p[2] = std::round(z.range(1.0F, 40.0F));
                } else if (wahl < 0.67F) {
                    p[0] = -350.0F + 100.0F * std::round(z.range(0.0F, 7.0F));
                    p[1] = std::round(z.range(-345.0F, -255.0F));
                    p[2] = 250.0F;
                } else {
                    const float k = std::round(z.range(0.0F, 40.0F));
                    p[0] = 100.0F + 40.0F * std::round(z.range(0.0F, 3.0F)) + 40.0F - k;
                    p[1] = -300.0F + 40.0F * std::round(z.range(0.0F, 3.0F)) + k;
                    p[2] = 150.0F;
                }
                dir[0] = 0.0F;
                dir[1] = 0.0F;
                dir[2] = -1.0F;
                ab = 0.0F;
                for (float& c : n) {
                    c = 0.0F;
                }
            }
            const float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            const float len = (art == 3) ? 200.0F : std::pow(10.0F, z.range(-1.0F, 2.0F));
            float s[3];
            float e[3];
            for (int k = 0; k < 3; ++k) {
                s[k] = p[k] + n[k] * ab - dir[k] / dl * len * ((art == 3) ? 0.0F : z.next());
                e[k] = s[k] + dir[k] / dl * len;
            }
            if (art == 5) {
                for (int k = 0; k < 3; ++k) {
                    s[k] = z.range(geo.mins[k], geo.maxs[k]);
                    e[k] = s[k] + dir[k] / dl * len;
                }
            }
            const bhed::TraceTreffer alt = bhed::traceRayEinfach(geo, s, e);
            const bhed::TraceTreffer neu = bhed::traceRay(geo, s, e);
            treffer += alt.hit ? 1 : 0;
            gleichWeit += (art == 3 && alt.hit) ? 1 : 0;
            if (!gleicherTreffer(alt, neu)) {
                ++unterschiede;
            }
        }
        std::printf("     %d Strecken, %d Treffer (%d senkrecht aufs Gitter), %d Unterschiede\n",
                    kStrecken, treffer, gleichWeit, unterschiede);
        expect("traceRay = traceRayEinfach, bitgleich (Treffer, Punkt, Normale, Anteil, Flaeche)",
               unterschiede == 0 && treffer > kStrecken / 10);
        // Eine KOPIE der Karte baut ihre eigene Hilfe und trifft dasselbe.
        const bhed::BspGeometry kopie = geo;
        const float s[3] = {150.0F, -250.0F, 200.0F};
        const float e[3] = {150.0F, -250.0F, -10.0F};
        expect("die Kopie einer Karte spurt wie das Original",
               gleicherTreffer(bhed::traceRay(kopie, s, e), bhed::traceRayEinfach(geo, s, e)));
    }

    // --- 2. Spurbuch: flugbahnWeiter in beliebiger Reihenfolge -----------
    {
        const std::vector<sim::Plane> raum = sim::roomPlanes(200.0F, 150.0F, 300.0F);
        const sim::TraceFn wand = [&raum](const V& von, const V& bis, V& punkt, V& normale) {
            const sim::Hit h = sim::trace(von, bis, raum);
            if (!h.hit) {
                return false;
            }
            punkt = h.point;
            normale = h.normal;
            return true;
        };
        struct Fall {
            V v;
            float g;
            float el;
            bool toetet;
        };
        const Fall faelle[] = {{{350.0F, 120.0F, 400.0F}, -500.0F, 0.8F, false},
                               {{600.0F, -300.0F, 50.0F}, 0.0F, 0.95F, false},   // liegt nie
                               {{200.0F, 80.0F, 300.0F}, -700.0F, 0.3F, false},  // liegt bald
                               {{-400.0F, 10.0F, 100.0F}, -300.0F, 0.5F, true}};
        ProbenZufall z;
        z.s = 4242U;
        bool gleich = true;
        int aufrufe = 0;
        for (const Fall& f : faelle) {
            sim::FlugZustand zustand;
            std::vector<float> alter;
            for (int i = 0; i < 70; ++i) {
                alter.push_back(z.range(0.0F, 9.0F));
            }
            // genau auf Schritten und Marken, und rueckwaerts in Folge
            alter.insert(alter.end(), {0.256F, 0.512F, 0.002F, 0.0F, 8.5F, 0.3F, 0.254F, 0.258F});
            for (const float t : alter) {
                const auto w = sim::flugbahnWeiter({0.0F, 0.0F, 100.0F}, f.v, {0.0F, 0.0F, 0.0F},
                                                   f.g, t, f.el, f.toetet, wand, zustand);
                const auto r = sim::flugbahn({0.0F, 0.0F, 100.0F}, f.v, {0.0F, 0.0F, 0.0F}, f.g, t,
                                             f.el, f.toetet, wand);
                gleich = gleich && gleichesErgebnis(w, r);
                ++aufrufe;
            }
        }
        std::printf("     %d Alter in Zufallsfolge gegen frische Rechnung\n", aufrufe);
        expect("Spurbuch: flugbahnWeiter = flugbahn, vorwaerts und rueckwaerts, bitgleich", gleich);

        // Die Fahne: abgetastet mit Buch = ohne Buch, Punkt fuer Punkt.
        bool fahneGleich = true;
        sim::FlugZustand zustand;
        for (const float bis : {1.3F, 0.4F, 2.9F, 2.9F, 0.0F, 5.0F}) {
            std::vector<std::pair<float, V>> ohne;
            std::vector<std::pair<float, V>> mit;
            const auto r1 = sim::flugbahnAbtasten(
                {0.0F, 0.0F, 100.0F}, faelle[0].v, {0.0F, 0.0F, 0.0F}, faelle[0].g, bis, 0.008F,
                faelle[0].el, false, wand, [&](float s0, const V& p) {
                    ohne.emplace_back(s0, p);
                    return true;
                });
            const auto r2 = sim::flugbahnAbtasten(
                {0.0F, 0.0F, 100.0F}, faelle[0].v, {0.0F, 0.0F, 0.0F}, faelle[0].g, bis, 0.008F,
                faelle[0].el, false, wand,
                [&](float s0, const V& p) {
                    mit.emplace_back(s0, p);
                    return true;
                },
                &zustand);
            fahneGleich = fahneGleich && gleichesErgebnis(r1, r2) && ohne.size() == mit.size() &&
                          (ohne.empty() ||
                           std::memcmp(ohne.data(), mit.data(), ohne.size() * sizeof(ohne[0])) == 0);
            // dazwischen dieselbe Bahn weiterrechnen - derselbe Zustand
            (void)sim::flugbahnWeiter({0.0F, 0.0F, 100.0F}, faelle[0].v, {0.0F, 0.0F, 0.0F},
                                      faelle[0].g, bis * 1.7F, faelle[0].el, false, wand, zustand);
        }
        expect("Spurbuch: die Fahne liegt Punkt fuer Punkt wie ohne Buch", fahneGleich);
    }

    // --- 3. Das ganze Effektnetz: alter Weg gegen neuen ------------------
    //
    // Alter Weg: traceRayEinfach und fuer JEDES Bild ein leerer Merkzettel
    // - jede Bahn von vorn, ein Faden. Neuer Weg: SpurHilfe, Spurbuch,
    // Vorrechnen auf allen Kernen; erst der Reihe nach, dann dieselben
    // Zeitpunkte in anderer Reihenfolge (Zurueckspulen, Springen).
    {
        const auto funken = bhed::efx::read(
            "repeatDelay 300\n"
            "Particle\n{\n\tflags usePhysics impactFx\n\tcount 60\n\tlife 1000 2000\n"
            "\tvelocity 100 -250 -250 400 250 250\n\tgravity -350 -550\n\tbounce 0.2 0.7\n"
            "\tsize\n\t{\n\t\tstart 2 4\n\t}\n\timpactfx\n\t[\n\t\tp/staub\n\t]\n}\n"
            "Tail\n{\n\tflags usePhysics\n\tcount 20\n\tlife 1500\n"
            "\tvelocity 150 -200 -200 350 200 200\n\tgravity -400\n\tbounce 0.3\n"
            "\tsize\n\t{\n\t\tstart 1\n\t}\n\tlength\n\t{\n\t\tstart -6\n\t}\n}\n"
            "Particle\n{\n\tflags usePhysics impactKills deathFx\n\tcount 12\n\tlife 900\n"
            "\tvelocity -300 -300 50 300 300 300\n\tgravity -600\n"
            "\tsize\n\t{\n\t\tstart 3\n\t}\n\tdeathfx\n\t[\n\t\tp/staub\n\t]\n}\n"
            "Emitter\n{\n\tflags usePhysics useModel emitFx\n\tcount 3\n\tlife 2500\n"
            "\tvelocity 50 -150 -150 250 150 150\n\tgravity -300\n\tbounce 0.5\n"
            "\tdensity 6\n\tvariance 2\n\tsize\n\t{\n\t\tstart 1\n\t}\n"
            "\tmodels\n\t[\n\t\tmodels/probe/brocken.md3\n\t]\n\temitfx\n\t[\n\t\tp/rauch\n\t]\n}\n");
        const auto staub = bhed::efx::read(
            "Particle\n{\n\tflags usePhysics\n\tcount 3\n\tlife 600\n"
            "\tvelocity -40 -40 20 40 40 90\n\tgravity -300\n\tbounce 0.4\n"
            "\tsize\n\t{\n\t\tstart 2\n\t}\n}\n");
        const auto rauch = bhed::efx::read(
            "Particle\n{\n\tlife 700\n\tvelocity 0 0 10 0 0 30\n\tsize\n\t{\n\t\tstart 5\n\t}\n}\n");
        expect("die Probeneffekte sind lesbar",
               !funken.hasErrors() && !staub.hasErrors() && !rauch.hasErrors() &&
                   funken.effect.primitives.size() == 4U);
        const bhed::EffectLookup nach = [&](const std::string& n) -> const bhed::efx::Effect* {
            if (n == "p/staub") {
                return &staub.effect;
            }
            if (n == "p/rauch") {
                return &rauch.effect;
            }
            return nullptr;
        };
        std::vector<bhed::EffectInstance> live;
        for (int i = 0; i < 2; ++i) {
            bhed::EffectInstance in;
            in.effect = &funken.effect;
            in.origin[0] = -150.0F + 150.0F * static_cast<float>(i);
            in.origin[1] = 40.0F * static_cast<float>(i);
            in.origin[2] = 120.0F;
            in.loops = true;
            in.intervalMs = 300.0F;
            in.startMs = 100.0 * i;
            in.seed = 17U + static_cast<unsigned>(i);
            live.push_back(in);
        }
        const float rechts[3] = {0.0F, 1.0F, 0.0F};
        const float oben[3] = {0.0F, 0.0F, 1.0F};
        bhed::EffectCull auge;
        auge.viewer[0] = 900.0F;   // Blick nach -x (vorn = oben x rechts)
        auge.viewer[2] = 150.0F;
        auge.haveViewer = true;
        auge.sichtTabelle = false;
        struct Bild {
            bhed::BspMesh netz;
            std::vector<bhed::EmitterModelDraw> modelle;
            std::vector<bhed::EffectInstance> folgen;
        };
        auto bild = [&](double t) {
            Bild b;
            b.folgen = bhed::effectFollowUpsAt(live, t, &geo, nach);
            std::vector<bhed::EffectInstance> alle = live;
            alle.insert(alle.end(), b.folgen.begin(), b.folgen.end());
            b.netz = bhed::buildEffectMesh(alle, t, rechts, oben, nullptr, &geo, &auge);
            b.modelle = bhed::emitterModelsAt(alle, t, &geo);
            return b;
        };
        auto gleichesBild = [](const Bild& x, const Bild& y) {
            bool ok = gleichesNetzBytes(x.netz, y.netz) && x.modelle.size() == y.modelle.size() &&
                      x.folgen.size() == y.folgen.size();
            for (std::size_t i = 0; ok && i < x.modelle.size(); ++i) {
                ok = std::memcmp(x.modelle[i].origin, y.modelle[i].origin,
                                 sizeof(x.modelle[i].origin)) == 0 &&
                     std::memcmp(x.modelle[i].angles, y.modelle[i].angles,
                                 sizeof(x.modelle[i].angles)) == 0;
            }
            for (std::size_t i = 0; ok && i < x.folgen.size(); ++i) {
                ok = std::memcmp(x.folgen[i].origin, y.folgen[i].origin,
                                 sizeof(x.folgen[i].origin)) == 0 &&
                     x.folgen[i].startMs == y.folgen[i].startMs &&
                     x.folgen[i].seed == y.folgen[i].seed;
            }
            return ok;
        };
        std::vector<double> zeiten;
        for (double t = 0.0; t <= 3500.0; t += 250.0) {
            zeiten.push_back(t);
        }
        zeiten.push_back(2345.0);
        std::vector<Bild> vorlage;
        bhed::setzeSpurVorlage(true);
        std::size_t vierecke = 0;
        std::size_t folgen = 0;
        std::size_t modelle = 0;
        for (const double t : zeiten) {
            bhed::vergissEffektBahnen();
            vorlage.push_back(bild(t));
            vierecke = std::max(vierecke, vorlage.back().netz.indexes.size() / 6U);
            folgen = std::max(folgen, vorlage.back().folgen.size());
            modelle = std::max(modelle, vorlage.back().modelle.size());
        }
        bhed::setzeSpurVorlage(false);
        bhed::vergissEffektBahnen();
        bool reihe = true;
        for (std::size_t i = 0; i < zeiten.size(); ++i) {
            reihe = reihe && gleichesBild(bild(zeiten[i]), vorlage[i]);
        }
        // Andere Reihenfolge: springend, vor und zurueck (Schritt 7 von 16)
        // - einmal mit vollem, einmal mit leerem Merkzettel.
        bool sprung = true;
        for (int runde = 0; runde < 2; ++runde) {
            if (runde == 1) {
                bhed::vergissEffektBahnen();
            }
            for (std::size_t k = 0; k < zeiten.size(); ++k) {
                const std::size_t i = (k * 7U + 3U) % zeiten.size();
                sprung = sprung && gleichesBild(bild(zeiten[i]), vorlage[i]);
            }
        }
        std::printf("     %zu Zeitpunkte, bis %zu Vierecke, %zu Folgeeffekte, %zu Modelle je Bild\n",
                    zeiten.size(), vierecke, folgen, modelle);
        expect("Effektnetz: neuer Weg = alter Weg, der Reihe nach (bitgleich)",
               reihe && vierecke > 100U && folgen > 10U && modelle > 0U);
        expect("Effektnetz: neuer Weg = alter Weg, gesprungen und rueckwaerts (bitgleich)", sprung);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const float right[3] = {0.0F, 1.0F, 0.0F};
    const float up[3] = {0.0F, 0.0F, 1.0F};

    // --- Lesen ---------------------------------------------------------
    const bhed::efx::ReadResult r = bhed::efx::read(kEiner);
    expect("ein einfacher Effekt liest sich", !r.hasErrors());
    expect("mit genau einer Primitive", r.effect.primitives.size() == 1U);

    bhed::EffectInstance inst;
    inst.effect = &r.effect;

    // --- Ein Tail ist KEINE Linie ---------------------------------------
    //
    // Beide sind Baender, aber ihr zweites Ende kommt von ganz
    // verschiedenen Stellen.
    //
    // Eine Line hat zwei Punkte: FxScheduler.cpp reicht org und org2
    // getrennt an FX_AddLine weiter.
    //
    // Ein Tail hat nur EINEN Punkt und eine LAENGE. Sein zweites Ende
    // rechnet die Engine jedes Bild neu (CTail::CalcNewEndpoint):
    //
    //     VectorSubtract( mOldOrigin, mOrigin1, temp );
    //     VectorNormalize( temp );
    //     VectorMA( mOrigin1, mLength, temp, oldorigin );
    //
    // `temp` zeigt nach HINTEN, entgegen der Bewegung - und deshalb sind
    // die Laengen in den Dateien negativ. `volcano.efx` hat
    // `length start -4 -2 end -280 -200`.
    //
    // behaved griff bei einem Tail nach `origin2`. Das gibt es dort nicht,
    // also war es null - ein Band ohne Laenge, praktisch unsichtbar.
    // Gemeldet als "die Spritzer sind winzig".
    //
    // Geprueft wird die AUSDEHNUNG des Netzes: ein Tail mit Laenge muss
    // sich weiter erstrecken als einer ohne.
    {
        auto ausdehnung = [&right, &up](const char* text) {
            const bhed::efx::ReadResult r2 = bhed::efx::read(text);
            bhed::EffectInstance in4;
            in4.effect = &r2.effect;
            const bhed::BspMesh m = bhed::buildEffectMesh({in4}, 500.0,
                                                          right, up);
            float hoch = -1.0e9F;
            float tief = 1.0e9F;
            for (const bhed::BspVertex& v : m.verts) {
                hoch = std::max(hoch, v.xyz[2]);
                tief = std::min(tief, v.xyz[2]);
            }
            return (m.verts.empty()) ? 0.0F : (hoch - tief);
        };
        // Die Klammern muessen auf EIGENE Zeilen. Der Leser fuehrt die
        // Klammertiefe zeilenweise; `size { start 2 }` auf einer Zeile wird
        // nicht als Block erkannt und landet als "Unbekannter Schluessel".
        //
        // Genau darauf bin ich hier hereingefallen: die erste Fassung
        // dieser Probe war rot, und ich habe den Leser verdaechtigt statt
        // meinen Probentext. In echten .efx-Dateien steht es nie so - die
        // Falle gilt nur fuer handgeschriebene Proben. Dieselbe Notiz steht
        // seit rc351 bei den Shaderproben.
        const char* kMitLaenge =
            "Tail\n{\n"
            "  spawnFlags absoluteVel\n"
            "  count 1\n"
            "  life 2000\n"
            "  velocity 0 0 200 0 0 200\n"
            "  size\n  {\n    start 2\n  }\n"
            "  length\n  {\n    start -200\n  }\n"
            "  shaders\n  [\n    gfx/x\n  ]\n"
            "}\n";
        const char* kOhneLaenge =
            "Tail\n{\n"
            "  spawnFlags absoluteVel\n"
            "  count 1\n"
            "  life 2000\n"
            "  velocity 0 0 200 0 0 200\n"
            "  size\n  {\n    start 2\n  }\n"
            "  shaders\n  [\n    gfx/x\n  ]\n"
            "}\n";
        const float mit = ausdehnung(kMitLaenge);
        const float ohne = ausdehnung(kOhneLaenge);
        std::printf("  Tail: mit Laenge %.0f, ohne %.0f Einheiten\n",
                    static_cast<double>(mit), static_cast<double>(ohne));
        expect("ein Tail mit Laenge spannt ein langes Band",
               mit > 150.0F);
        expect("ohne Laenge bleibt er kurz", ohne < 50.0F);
    }

    // --- Alle lebenden Durchgaenge, nicht nur der neueste ----------------
    //
    // Gemeldet: "die Spritzer sind nicht so hoch".
    //
    // `efxdraw.cpp` nahm bei einem wiederholenden Runner nur den ZULETZT
    // faelligen Durchgang. Das ist falsch, sobald die Teilchen laenger
    // leben als der Abstand zwischen zwei Durchgaengen - und das ist der
    // Normalfall: `mustafar/volcano.efx` hat `repeatDelay 300` und
    // `life 5800 4200`, also gut dreissig ueberlappende Durchgaenge.
    //
    // Gemessen an md_am_sith: die Saeule war 578 Einheiten hoch, und zwar
    // bei JEDEM Zeitpunkt gleich - waehrend die Bahn allein einen Scheitel
    // bei rund 950 ergibt. Dass sich die Zahl nicht aenderte, war der
    // Hinweis: kein Teilchen wurde alt genug.
    //
    // Die Probe stellt das nach: derselbe Effekt, einmal mit kurzem und
    // einmal mit langem Wiederholabstand. Beim kurzen muessen MEHR Teilchen
    // gleichzeitig da sein - und sie muessen HOEHER gekommen sein, weil
    // aeltere dabei sind.
    {
        const char* kBrunnen =
            "Particle\n{\n"
            "  spawnFlags absoluteVel\n"
            "  count 4\n"
            "  life 4000\n"
            "  velocity 0 0 400 0 0 400\n"
            "  size { start 4 }\n"
            "  shaders [ gfx/x ]\n"
            "}\n";
        const bhed::efx::ReadResult br = bhed::efx::read(kBrunnen);
        expect("der Brunnen liest sich", !br.hasErrors());
        auto messen = [&right, &up, &br](float abstandMs) {
            bhed::EffectInstance in3;
            in3.effect = &br.effect;
            in3.loops = true;
            in3.intervalMs = abstandMs;
            in3.startMs = 0.0;
            const bhed::BspMesh m = bhed::buildEffectMesh({in3}, 3500.0,
                                                          right, up);
            // Nicht die Hoehe des HOECHSTEN Teilchens - die ist in beiden
            // Faellen dieselbe, weil der aelteste Durchgang gleich alt ist.
            // Was sich unterscheidet, ist die SPANNE: leben mehrere
            // Durchgaenge, gibt es Teilchen in jedem Alter, also von ganz
            // unten bis ganz oben. Lebt nur einer, stehen alle auf
            // derselben Hoehe.
            float hoch = -1.0e9F;
            float tief = 1.0e9F;
            for (const bhed::BspVertex& v : m.verts) {
                hoch = std::max(hoch, v.xyz[2]);
                tief = std::min(tief, v.xyz[2]);
            }
            return std::pair<std::size_t, float>{m.verts.size(),
                                                 hoch - tief};
        };
        const auto kurz = messen(200.0F);    // viele Durchgaenge leben
        const auto lang = messen(5000.0F);   // nur einer
        std::printf("  Durchgaenge: kurz %zu Ecken, Spanne %.0f | "
                    "lang %zu Ecken, Spanne %.0f\n",
                    kurz.first, static_cast<double>(kurz.second),
                    lang.first, static_cast<double>(lang.second));
        expect("bei kurzem Abstand sind mehr Teilchen da",
               kurz.first > lang.first);
        // Der Faktor ist grosszuegig: es geht um "viele Alter" gegen
        // "ein Alter", nicht um eine genaue Zahl.
        expect("und sie stehen in allen Hoehen statt in einer",
               kurz.second > lang.second * 3.0F);
    }

    // --- Oertliche gegen absolute Geschwindigkeit ------------------------
    //
    // FxScheduler.cpp:1566 - ohne `absoluteVel` ist die Geschwindigkeit
    // OERTLICH und wird in die Achse des Runners gedreht:
    //
    //     VectorScale( ax[0], mVelX, vel );
    //     VectorMA( vel, mVelY, ax[1], vel );
    //     VectorMA( vel, mVelZ, ax[2], vel );
    //
    // behaved hat das Flag bis rc362 gelesen (efx_effect.cpp:123) und nie
    // benutzt - jede Geschwindigkeit galt als absolut.
    //
    // Gemeldet als "die Spritzer sind nicht so hoch": `volcano.efx` hat kein
    // `absoluteVel` und `velocity 1200 -180 -180 ...`. Ein fx_runner ohne
    // `angle` zeigt nach OBEN (g_fx.cpp:245, "the default of up"), also
    // sind das 975 bis 1200 Einheiten Steighoehe. Als Weltrichtung gelesen
    // fliegen die Tropfen stattdessen nach +x.
    //
    // Die Probe stellt genau das nach: derselbe Effekt, einmal mit und
    // einmal ohne das Flag, bei einem Runner der nach oben zeigt. Ohne das
    // Flag muessen die Teilchen NACH OBEN wandern, mit dem Flag nach +x.
    {
        const char* kLokal =
            "Particle\n{\n"
            "  count 12\n"
            "  life 2000\n"
            "  velocity 400 0 0 400 0 0\n"
            "  size { start 4 }\n"
            "  shaders [ gfx/x ]\n"
            "}\n";
        const char* kAbsolut =
            "Particle\n{\n"
            "  spawnFlags absoluteVel\n"
            "  count 12\n"
            "  life 2000\n"
            "  velocity 400 0 0 400 0 0\n"
            "  size { start 4 }\n"
            "  shaders [ gfx/x ]\n"
            "}\n";
        const bhed::efx::ReadResult lo = bhed::efx::read(kLokal);
        const bhed::efx::ReadResult ab = bhed::efx::read(kAbsolut);
        expect("beide Fassungen lesen sich",
               !lo.hasErrors() && !ab.hasErrors());
        auto weiteste = [&right, &up](const bhed::efx::Effect& e, int achse) {
            bhed::EffectInstance in2;
            in2.effect = &e;
            // Ein Runner ohne `angle` zeigt nach oben - genau der Fall aus
            // md_am_sith, wo KEIN EINZIGER der 41 Runner ein `angles` hat.
            in2.normal[0] = 0.0F;
            in2.normal[1] = 0.0F;
            in2.normal[2] = 1.0F;
            const bhed::BspMesh m = bhed::buildEffectMesh({in2}, 1000.0,
                                                          right, up);
            float weit = 0.0F;
            for (const bhed::BspVertex& v : m.verts) {
                weit = std::max(weit, v.xyz[achse]);
            }
            return weit;
        };
        const float lokalHoch = weiteste(lo.effect, 2);
        const float lokalVorn = weiteste(lo.effect, 0);
        const float absHoch = weiteste(ab.effect, 2);
        const float absVorn = weiteste(ab.effect, 0);
        std::printf("  oertlich: hoch %.0f vorn %.0f | absolut: hoch %.0f "
                    "vorn %.0f\n",
                    static_cast<double>(lokalHoch),
                    static_cast<double>(lokalVorn),
                    static_cast<double>(absHoch),
                    static_cast<double>(absVorn));
        expect("ohne absoluteVel steigen die Teilchen", lokalHoch > 100.0F);
        expect("und fliegen NICHT nach vorn", lokalVorn < lokalHoch);
        expect("mit absoluteVel fliegen sie nach vorn", absVorn > 100.0F);
        expect("und steigen NICHT", absHoch < absVorn);
    }

    // --- Vierecke ------------------------------------------------------
    //
    // count 4, life 1000: waehrend der ersten Sekunde muessen Vierecke da
    // sein, danach keine mehr.
    const bhed::BspMesh bei0 = bhed::buildEffectMesh({inst}, 0.0, right, up);
    const bhed::BspMesh spaet = bhed::buildEffectMesh({inst}, 9000.0, right, up);
    expect("waehrend der Lebensdauer entstehen Vierecke",
           !bei0.indexes.empty());
    expect("je Viereck vier Ecken und sechs Indizes",
           bei0.verts.size() % 4U == 0U &&
               bei0.indexes.size() == bei0.verts.size() / 4U * 6U);
    expect("danach keine mehr", spaet.indexes.empty());
    expect("ein Zeichenaufruf, wenn etwas da ist", bei0.batches.size() == 1U);
    expect("und keiner, wenn nichts da ist", spaet.batches.empty());

    // --- Wiederholbarkeit ------------------------------------------------
    //
    // Das ist die wichtigste Probe hier. Eine Vorschau, die beim
    // Zurueckspulen anders aussieht, taugt nicht zum Abstimmen einer
    // Zwischensequenz - man wuesste nie, ob die Aenderung von einem selbst
    // kommt oder vom Zufall.
    const bhed::BspMesh nochmal = bhed::buildEffectMesh({inst}, 0.0, right, up);
    bool gleich = nochmal.verts.size() == bei0.verts.size() &&
                  nochmal.indexes.size() == bei0.indexes.size();
    for (std::size_t i = 0; gleich && i < bei0.verts.size(); ++i) {
        for (int c = 0; c < 3; ++c) {
            if (bei0.verts[i].xyz[c] != nochmal.verts[i].xyz[c]) {
                gleich = false;
            }
        }
    }
    expect("dieselbe Zeit ergibt EXAKT dasselbe Bild", gleich);

    // Ein anderer Ausgangswert soll etwas anderes ergeben - sonst saehen
    // zwei Runner nebeneinander Partikel fuer Partikel gleich aus.
    //
    // Verglichen wird bei 500 ms, NICHT bei 0. Bei 0 ist noch nichts
    // geflogen: positionAt(org, vel, ..., 0) ergibt den Ursprung, egal wie
    // schnell das Partikel ist. Zwei Ausgangswerte muessen dort dasselbe
    // ergeben - auch das war zuerst eine falsche Probe und kein Fehler.
    bhed::EffectInstance anders = inst;
    anders.seed = inst.seed + 12345U;
    const bhed::BspMesh spaeterA = bhed::buildEffectMesh({inst}, 500.0, right, up);
    const bhed::BspMesh b2 = bhed::buildEffectMesh({anders}, 500.0, right, up);
    bool verschieden = b2.verts.size() != spaeterA.verts.size();
    for (std::size_t i = 0; !verschieden && i < spaeterA.verts.size(); ++i) {
        for (int c = 0; c < 3; ++c) {
            if (spaeterA.verts[i].xyz[c] != b2.verts[i].xyz[c]) {
                verschieden = true;
            }
        }
    }
    expect("ein anderer Ausgangswert ergibt ein anderes Bild", verschieden);

    // --- Der Ort wandert mit --------------------------------------------
    bhed::EffectInstance weg = inst;
    weg.origin[0] = 1000.0F;
    const bhed::BspMesh v = bhed::buildEffectMesh({weg}, 0.0, right, up);
    bool verschoben = !v.verts.empty();
    for (const bhed::BspVertex& vert : v.verts) {
        if (vert.xyz[0] < 900.0F) {
            verschoben = false;
        }
    }
    expect("die Vierecke stehen am Ort des Runners", verschoben);

    // --- Die Vierecke stehen quer zur Blickrichtung ----------------------
    //
    // right und up spannen sie auf, also darf sich entlang der dritten
    // Achse nichts aendern. Mit right = Y und up = Z heisst das: alle vier
    // Ecken eines Vierecks haben dasselbe X.
    bool flach = !bei0.verts.empty();
    for (std::size_t i = 0; i + 3 < bei0.verts.size(); i += 4) {
        for (int k = 1; k < 4; ++k) {
            if (bei0.verts[i].xyz[0] != bei0.verts[i + static_cast<std::size_t>(k)].xyz[0]) {
                flach = false;
            }
        }
    }
    expect("jedes Viereck liegt in der Ebene von right und up", flach);

    // --- Der Umlauf --------------------------------------------------
    //
    // JKA wickelt seine Dreiecke ENTGEGEN der Normalen, und der Zeichner
    // keult danach (rc103, tr_backend.cpp: qglCullFace( GL_FRONT )). Wer
    // hier andersherum wickelt, bekommt unsichtbare Partikel.
    if (bei0.verts.size() >= 4 && bei0.indexes.size() >= 3) {
        const bhed::BspVertex& a = bei0.verts[bei0.indexes[0]];
        const bhed::BspVertex& b = bei0.verts[bei0.indexes[1]];
        const bhed::BspVertex& c = bei0.verts[bei0.indexes[2]];
        const float u1[3] = {b.xyz[0] - a.xyz[0], b.xyz[1] - a.xyz[1],
                             b.xyz[2] - a.xyz[2]};
        const float v1[3] = {c.xyz[0] - a.xyz[0], c.xyz[1] - a.xyz[1],
                             c.xyz[2] - a.xyz[2]};
        const float n[3] = {u1[1] * v1[2] - u1[2] * v1[1],
                            u1[2] * v1[0] - u1[0] * v1[2],
                            u1[0] * v1[1] - u1[1] * v1[0]};
        const float dp = n[0] * a.normal[0] + n[1] * a.normal[1] +
                         n[2] * a.normal[2];
        expect("der Umlauf laeuft ENTGEGEN der Normalen, wie in JKA", dp < 0.0F);
    }

    // --- Echte Dateien, wenn welche mitgegeben wurden --------------------
    int gelesen = 0;
    int gezeichnet = 0;
    for (int i = 1; i < argc; ++i) {
        const bhed::efx::ReadResult e = bhed::efx::read(slurp(argv[i]));
        if (e.hasErrors()) {
            continue;
        }
        ++gelesen;
        bhed::EffectInstance ei;
        ei.effect = &e.effect;
        std::size_t meiste = 0;
        for (double t = 0.0; t <= 3000.0; t += 100.0) {
            const bhed::BspMesh m = bhed::buildEffectMesh({ei}, t, right, up);
            meiste = std::max(meiste, m.indexes.size() / 6U);
            if (static_cast<int>(m.indexes.size() / 6U) >
                bhed::kMaxQuadsPerEffect) {
                std::printf("     %s ueberschreitet die Obergrenze\n", argv[i]);
                ++g_fails;
            }
        }
        if (meiste != 0U) {
            ++gezeichnet;
        }
    }
    if (argc > 1) {
        std::printf("     %d Effektdateien gelesen, %d davon zeigen etwas\n",
                    gelesen, gezeichnet);
        expect("die Obergrenze wird nie ueberschritten", true);
    }

    // --- Folgeeffekte ueber ALLE mitgegebenen Dateien (rc569) --------------
    //
    // Die Namen in impactfx/deathfx/emitfx/playfx ("sparks/hit") werden
    // gegen die mitgegebenen Dateien aufgeloest (alles ab "effects/", ohne
    // ".efx"). Geprueft wird, dass keine Kette die Schranken sprengt, dass
    // dasselbe Bild zweimal dieselbe Liste gibt - und gemeldet, wie tief die
    // tiefste Kette wirklich reicht (die Zahl hinter kMaxFolgeTiefe).
    if (argc > 1) {
        std::vector<bhed::efx::ReadResult> gelesenAlle;
        std::vector<std::string> namen;
        gelesenAlle.reserve(static_cast<std::size_t>(argc));
        for (int i = 1; i < argc; ++i) {
            std::string n = argv[i];
            for (char& c : n) {
                c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            const std::size_t at = n.rfind("effects/");
            if (at == std::string::npos || n.size() < 4U) {
                continue;
            }
            n = n.substr(at + 8U);
            n = n.substr(0, n.size() - 4U);   // ".efx"
            gelesenAlle.push_back(bhed::efx::read(slurp(argv[i])));
            namen.push_back(n);
        }
        std::map<std::string, const bhed::efx::Effect*> verzeichnis;
        for (std::size_t i = 0; i < namen.size(); ++i) {
            if (!gelesenAlle[i].hasErrors()) {
                verzeichnis[namen[i]] = &gelesenAlle[i].effect;
            }
        }
        const bhed::EffectLookup nach = [&verzeichnis](const std::string& n) {
            std::string k = n;
            for (char& c : k) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            const auto it = verzeichnis.find(k);
            return it == verzeichnis.end() ? nullptr : it->second;
        };
        std::size_t meisteFolgen = 0;
        int tiefste = 0;
        bool stabil = true;
        bool begrenzt = true;
        for (const auto& [name, ef] : verzeichnis) {
            bhed::EffectInstance ei;
            ei.effect = ef;
            ei.seed = static_cast<unsigned>(name.size()) * 2654435761U;
            for (double t = 0.0; t <= 3000.0; t += 250.0) {
                const auto f = bhed::effectFollowUpsAt({ei}, t, nullptr, nach);
                const auto f2 = bhed::effectFollowUpsAt({ei}, t, nullptr, nach);
                stabil = stabil && f.size() == f2.size();
                begrenzt = begrenzt && f.size() <= bhed::kMaxFolgeEffekte;
                meisteFolgen = std::max(meisteFolgen, f.size());
                for (int tf = bhed::kMaxFolgeTiefe; tf > tiefste; --tf) {
                    if (bhed::effectFollowUpsAt({ei}, t, nullptr, nach, tf).size() >
                        bhed::effectFollowUpsAt({ei}, t, nullptr, nach, tf - 1).size()) {
                        tiefste = tf;
                        break;
                    }
                }
                (void)bhed::emitterModelsAt({ei}, t, nullptr);
                const float auge[3] = {-100.0F, 0.0F, 0.0F};
                const float blick[3] = {1.0F, 0.0F, 0.0F};
                (void)bhed::effectScreenFlashesAt({ei}, t, auge, blick);
            }
        }
        std::printf("     Folgeeffekte: hoechstens %zu je Effekt, tiefste Kette %d Stufen\n",
                    meisteFolgen, tiefste);
        expect("Folgeeffekte: dasselbe Bild, dieselbe Liste", stabil);
        expect("Folgeeffekte: nie ueber kMaxFolgeEffekte", begrenzt);
    }

    // --- Dynamische Lichter aus Effekten -----------------------------------
    //
    // `light` ist ein Primitivtyp wie `particle` - 72 Vorkommen in den 376
    // .efx-Dateien aus assets1. Sie bringen kein Bild mit, sondern
    // beleuchten, was in der Naehe steht.
    //
    // Die Formel steht in R_SetupEntityLighting (tr_light.cpp:435 ff.):
    //
    //     power = DLIGHT_AT_RADIUS * ( radius * radius );
    //     if ( d < DLIGHT_MINIMUM_RADIUS ) d = DLIGHT_MINIMUM_RADIUS;
    //     d = power / ( d * d );
    //
    // Beide Konstanten stehen auf 16 (ebenda:31 und 34). Diese Probe
    // rechnet gegen HANDWERTE - sonst waere jede Zahl darin beliebig.
    {
        bhed::EffectLight l;
        l.origin[0] = 100.0F;
        l.radius = 32.0F;
        l.color[0] = l.color[1] = l.color[2] = 1.0F;
        const std::vector<bhed::EffectLight> lichter{l};

        // Abstand 100, Radius 32:
        //   power = 16 * 32 * 32 = 16384
        //   d     = 16384 / (100*100) = 1.6384
        //   Zuwachs = 1.6384 * 1.0 * 255 = 417.8
        bhed::GridLight g;
        g.ok = true;
        const float hier[3] = {0.0F, 0.0F, 0.0F};
        bhed::addDynamicLights(g, hier, lichter);
        expect("die Staerke folgt der Formel der Engine",
               near(g.directed[0], 417.8F, 1.0F));

        // Und die Richtung zeigt ZUM Licht, also nach +x.
        expect("die Richtung zeigt zum Licht",
               near(g.dir[0], 1.0F, 0.01F));

        // Der Mindestabstand: naeher als 16 Einheiten wird nicht gerechnet.
        // Ohne ihn ginge die Staerke direkt am Licht gegen unendlich, und
        // eine Figur, die durch eine Flamme laeuft, wuerde rein weiss.
        //
        //   d     = 16384 / (16*16) = 64
        //   Zuwachs = 64 * 255 = 16320
        bhed::GridLight nah;
        nah.ok = true;
        const float dicht[3] = {99.0F, 0.0F, 0.0F};   // ein Einheit entfernt
        bhed::addDynamicLights(nah, dicht, lichter);
        expect("naeher als der Mindestabstand wird geklemmt",
               near(nah.directed[0], 16320.0F, 5.0F));

        // Ohne Lichter bleibt alles, wie es war - auch das `ok`. Sonst
        // meldete eine Karte ohne Gitter ploetzlich Licht, nur weil die
        // Funktion gerufen wurde.
        bhed::GridLight leer;
        bhed::addDynamicLights(leer, hier, {});
        expect("ohne Lichter bleibt es unberuehrt",
               !leer.ok && near(leer.directed[0], 0.0F, 0.001F));
    }

    // --- Die Bahn MIT Aufprall ---------------------------------------------
    //
    // Gemeldet: "die EFX werden wohl noch nicht korrekt gerendert."
    // Gemessen (rc319): `usePhysics` steht 166 mal in den 376
    // Effektdateien - der groesste einzelne Posten.
    //
    // Bis hierher flog alles geradeaus durch die Wand. Die Engine prallt
    // ab (CParticle::UpdateOrigin, FxPrimitives.cpp:249 ff.).
    //
    // Geprueft wird gegen HANDWERTE, mit einem erfundenen Boden bei z=0.
    {
        // Ein Boden bei z=0: alles, was darunter will, wird geblockt.
        auto boden = [](const bhed::efx::camera::Vec3& von,
                        const bhed::efx::camera::Vec3& bis,
                        bhed::efx::camera::Vec3& punkt,
                        bhed::efx::camera::Vec3& normale) {
            if (von.z >= 0.0F && bis.z < 0.0F) {
                const float t = von.z / (von.z - bis.z);
                punkt = bhed::efx::camera::Vec3{von.x + (bis.x - von.x) * t,
                                                von.y + (bis.y - von.y) * t,
                                                0.0F};
                normale = bhed::efx::camera::Vec3{0.0F, 0.0F, 1.0F};
                return true;
            }
            return false;
        };

        const bhed::efx::camera::Vec3 start{0.0F, 0.0F, 100.0F};
        const bhed::efx::camera::Vec3 ruhe{0.0F, 0.0F, 0.0F};
        const bhed::efx::camera::Vec3 keine{0.0F, 0.0F, 0.0F};

        // OHNE Wand faellt es einfach durch: freier Fall aus 100 mit
        // g = -800 braucht 0,5 s bis z=0, nach 1 s waere es bei
        // 100 - 0,5*800*1 = -300.
        {
            const auto frei = bhed::efx::sim::flugbahn(
                start, ruhe, keine, -800.0F, 1.0F, 0.5F, false, nullptr);
            expect("ohne Wand faellt es hindurch", frei.position.z < -100.0F);
        }

        // MIT Boden darf es nie darunter geraten.
        {
            const auto mit = bhed::efx::sim::flugbahn(
                start, ruhe, keine, -800.0F, 1.0F, 0.5F, false, boden);
            expect("mit Boden bleibt es darueber", mit.position.z >= -0.01F);
            expect("und es hat mindestens einmal geprallt",
                   mit.abpraller >= 1);
        }

        // Elastizitaet NULL heisst: liegenbleiben, kein Abprallen.
        {
            const auto tot = bhed::efx::sim::flugbahn(
                start, ruhe, keine, -800.0F, 2.0F, 0.0F, false, boden);
            expect("ohne Elastizitaet bleibt es liegen", tot.liegt);
            expect("und zwar auf dem Boden",
                   near(tot.position.z, 0.0F, 0.01F));
        }

        // killOnImpact endet beim ERSTEN Treffer, genau dort.
        {
            const auto weg = bhed::efx::sim::flugbahn(
                start, ruhe, keine, -800.0F, 2.0F, 0.9F, true, boden);
            expect("killOnImpact endet am Aufschlag", weg.gestorben);
            expect("genau auf der Ebene",
                   near(weg.position.z, 0.0F, 0.01F));
        }

        // Und der Fall, der leicht uebersehen wird: OHNE Aufprall muss die
        // Bahn dieselbe sein wie die geschlossene Rechnung. Sonst haette
        // ein Partikel je nach Flag eine andere Flugbahn.
        {
            const bhed::efx::camera::Vec3 hoch{0.0F, 0.0F, 1000.0F};
            const bhed::efx::camera::Vec3 v{100.0F, 0.0F, 0.0F};
            const auto schritt = bhed::efx::sim::flugbahn(
                hoch, v, keine, -800.0F, 0.5F, 0.5F, false, boden);
            const auto zu = bhed::efx::sim::positionAt(hoch, v, keine,
                                                       -800.0F, 0.5F);
            // Schrittweise gegen geschlossen: nicht bitgleich, aber nah -
            // der Unterschied ist der Integrationsfehler von Euler.
            expect("ohne Wand stimmt sie mit der freien Bahn ueberein",
                   near(schritt.position.x, zu.x, 1.0F) &&
                       near(schritt.position.z, zu.z, 5.0F));
        }
    }

    // --- WANN und WO der Aufprall war --------------------------------------
    //
    // Gebraucht fuer `impactFx` (18 Vorkommen): dort startet die Engine
    // einen zweiten Effekt am Trefferpunkt (FxPrimitives.cpp:318).
    //
    // Der Zeitpunkt ist die Zahl, die man leicht falsch bekommt - und ein
    // falscher Zeitpunkt heisst, dass der Funkenschlag vor oder nach dem
    // Aufschlag erscheint.
    //
    // Handrechnung: freier Fall aus 100 Einheiten mit g = -800.
    //     100 = 0,5 * 800 * t^2   ->   t = 0,5 s
    {
        auto boden = [](const bhed::efx::camera::Vec3& von,
                        const bhed::efx::camera::Vec3& bis,
                        bhed::efx::camera::Vec3& punkt,
                        bhed::efx::camera::Vec3& normale) {
            if (von.z >= 0.0F && bis.z < 0.0F) {
                const float t = von.z / (von.z - bis.z);
                punkt = bhed::efx::camera::Vec3{von.x + (bis.x - von.x) * t,
                                                von.y + (bis.y - von.y) * t,
                                                0.0F};
                normale = bhed::efx::camera::Vec3{0.0F, 0.0F, 1.0F};
                return true;
            }
            return false;
        };
        const bhed::efx::camera::Vec3 start{0.0F, 0.0F, 100.0F};
        const bhed::efx::camera::Vec3 ruhe{0.0F, 0.0F, 0.0F};

        const auto b = bhed::efx::sim::flugbahn(start, ruhe, ruhe, -800.0F,
                                                2.0F, 0.0F, false, boden);
        expect("der Aufprall wird gemeldet", b.traf);
        // Ein halbe Sekunde, mit etwas Luft fuer den Schrittfehler.
        expect("und zwar nach einer halben Sekunde",
               near(b.trefferSekunde, 0.5F, 0.02F));
        expect("genau auf der Ebene", near(b.trefferPunkt.z, 0.0F, 0.01F));
        expect("mit der Normalen nach oben",
               near(b.trefferNormale.z, 1.0F, 0.001F));

        // Und der Fall, der leicht durchrutscht: NUR der erste Aufprall
        // zaehlt. Mit Elastizitaet prallt es mehrfach - der gemeldete
        // Zeitpunkt muss trotzdem der des ERSTEN sein.
        const auto mehr = bhed::efx::sim::flugbahn(start, ruhe, ruhe, -800.0F,
                                                   2.0F, 0.8F, false, boden);
        expect("es prallt mehrfach", mehr.abpraller >= 2);
        expect("gemeldet wird trotzdem der erste",
               near(mehr.trefferSekunde, 0.5F, 0.02F));

        // Ohne Wand gar nichts.
        const auto frei = bhed::efx::sim::flugbahn(start, ruhe, ruhe, -800.0F,
                                                   2.0F, 0.5F, false, nullptr);
        expect("ohne Wand wird nichts gemeldet", !frei.traf);
    }

    // --- deathFx: der Effekt am Lebensende ---------------------------------
    //
    // CParticle::Die (FxPrimitives.cpp:102 ff.):
    //
    //     if ( mFlags & FX_DEATH_RUNS_FX && !(mFlags & FX_KILL_ON_IMPACT) )
    //         theFxScheduler.PlayEffect( mDeathFxID, mOrigin1, norm );
    //
    // Zwei Dinge daran, die man uebersieht: wer an der Wand stirbt bekommt
    // KEINEN Todeseffekt (er hatte schon den Aufpralleffekt), und die
    // Normale ist ZUFAELLIG, nicht nach oben.
    //
    // Und eine dritte, die mich hier zweimal erwischt hat:
    //
    //   * ohne Kartengeometrie darf trotzdem gemeldet werden - ein
    //     Partikel stirbt am Ende seines Lebens, ob eine Wand da ist oder
    //     nicht. Mein erster Versuch stieg ohne Karte sofort aus.
    //   * `life` steht in MILLISEKUNDEN. Mein erster Versuch hat es als
    //     Sekunden behandelt, und der Todeseffekt kam bei 4478000 ms statt
    //     bei 4478.
    {
        // Der Aufbau ist der echten effects/chunks/probehead.efx
        // nachgebildet - die einzige Datei in assets1, die einen
        // Todeseffekt benennt. Erfundenes Format haette ich mir hier
        // zweimal zurechtgelegt; abgeschrieben stimmt es.
        const char* text =
            "Particle\n"
            "{\n"
            "\tflags\t\t\tdeathFx\n"
            "\tlife\t\t\t1000 1000\n"
            "\tdeathfx\n"
            "\t[\n"
            "\t\ttest/tot\n"
            "\t]\n"
            "}\n";
        const auto r = bhed::efx::read(text);
        expect("die Probendatei ist lesbar", !r.effect.primitives.empty());
        if (!r.effect.primitives.empty()) {
            bhed::EffectInstance in;
            in.effect = &r.effect;
            in.startMs = 0.0;
            in.seed = 7U;

            // Vor dem Lebensende: noch nichts.
            expect("vor dem Ende wird nichts gemeldet",
                   bhed::effectImpactsAt({in}, 500.0, nullptr).empty());

            // Danach genau einer - OHNE Kartengeometrie.
            const auto e = bhed::effectImpactsAt({in}, 1500.0, nullptr);
            expect("nach dem Ende genau einer, auch ohne Karte",
                   e.size() == 1U);
            // Und ein Emitter OHNE emitFx stoesst nichts aus - sonst
            // haette diese Datei hier plotzlich eine Rauchfahne.
            if (e.size() == 1U) {
                expect("er ist als Todeseffekt gekennzeichnet", e[0].vomTod());
                expect("mit dem Namen aus der Datei",
                       e[0].effectName == "test/tot");
                // life 1000 1000 heisst: genau bei 1000 ms.
                expect("und genau am Lebensende",
                       near(static_cast<float>(e[0].startMs), 1000.0F, 1.0F));
                const float n = e[0].normal[0] * e[0].normal[0] +
                                e[0].normal[1] * e[0].normal[1] +
                                e[0].normal[2] * e[0].normal[2];
                expect("die Normale hat die Laenge eins", near(n, 1.0F, 0.01F));
            }
        }
    }

    // --- Ein Range hat VIER Felder, nicht zwei -----------------------------
    //
    // Der Fehler, der mich in rc321 bis rc323 dreimal begleitet hat, ohne
    // dass ich ihn bemerkte:
    //
    //     efx::Range{min, max}          // set bleibt FALSE
    //     Random::pick(ungesetzt) == 0  // efx_sim.cpp:19
    //
    // Folge: jede Geschwindigkeit, jede Drehung, jede Beschleunigung, die
    // ich so gewuerfelt habe, war NULL. Ein Emittermodell fiel senkrecht
    // herunter, obwohl seine Datei `velocity 300 -80 -80 400 80 80` sagt.
    //
    // Aufgefallen beim Vergleich der Zahlen im Protokoll mit den Zahlen in
    // der Datei. Wer nur prueft "es bewegt sich", sieht es nicht - es
    // bewegte sich ja, die Schwerkraft war ja da.
    //
    // Diese Probe haelt genau das fest.
    {
        bhed::efx::sim::Random rnd(1U);
        // So, wie ich es falsch gemacht hatte:
        const bhed::efx::Range ohne{300.0F, 400.0F};
        expect("ein Range ohne `set` gibt null", near(rnd.pick(ohne), 0.0F, 0.001F));

        // Und so, wie es sein muss:
        bhed::efx::Range mit;
        mit.min = 300.0F;
        mit.max = 400.0F;
        mit.set = true;
        mit.ranged = true;
        const float v = rnd.pick(mit);
        expect("mit `set` liegt der Wert im Bereich",
               v >= 300.0F && v <= 400.0F);

        // Der Sonderfall: gesetzt, aber nicht `ranged` - dann gilt `min`.
        bhed::efx::Range fest;
        fest.min = 42.0F;
        fest.max = 99.0F;
        fest.set = true;
        fest.ranged = false;
        expect("ohne `ranged` gilt der Mindestwert",
               near(rnd.pick(fest), 42.0F, 0.001F));
    }

    // --- useAlpha: Farbe voll, Blende im Alphakanal ------------------------
    //
    // FxPrimitives.cpp:599 ff.:
    //
    //     if ( mFlags & FX_USE_ALPHA ) {
    //         ClampVec( mRefEnt.angles, shaderRGBA );   // Farbe VOLL
    //         shaderRGBA[3] = (byte)(perc1 * 0xff);     // Blende
    //     } else {
    //         VectorScale( mRefEnt.angles, perc1, ... ); // Farbe skalieren
    //     }
    //
    // 215 Vorkommen - der haeufigste Punkt auf der Liste aus rc319.
    //
    // Geprueft wird an zwei Partikeln, die sich NUR im Flag
    // unterscheiden, halb verblasst (alpha 1 -> 0 ueber die Lebenszeit):
    //
    //   ohne useAlpha:  Farbe halbiert, Alphakanal voll
    //   mit  useAlpha:  Farbe voll,     Alphakanal halbiert
    {
        auto bau = [](bool mitFlag) {
            std::string t =
                "Particle\n"
                "{\n"
                "\tlife\t\t1000 1000\n"
                // Bloecke mit GESCHWEIFTEN Klammern - eckige sind fuer
                // Namenslisten (models, deathfx). Aus einer echten Datei
                // abgeschrieben (effects/sparks/spark_exp_nosnd.efx),
                // nachdem ich es zum dritten Mal falsch geraten hatte.
                "\trgb\n\t{\n\t\tstart 1 1 1\n\t\tend 1 1 1\n\t}\n"
                "\talpha\n\t{\n\t\tstart 1\n\t\tend 0\n\t\tflags linear\n\t}\n"
                "\tsize\n\t{\n\t\tstart 20\n\t\tend 20\n\t}\n";
            if (mitFlag) {
                t += "\tflags\t\tuseAlpha\n";
            }
            t += "}\n";
            return t;
        };
        for (int i = 0; i < 2; ++i) {
            const bool mitFlag = (i == 1);
            const auto r = bhed::efx::read(bau(mitFlag));
            if (r.effect.primitives.empty()) {
                expect("die Probendatei ist lesbar", false);
                continue;
            }
            bhed::EffectInstance in;
            in.effect = &r.effect;
            in.startMs = 0.0;
            in.seed = 3U;
            const float rechts[3] = {0.0F, 1.0F, 0.0F};
            const float hoch[3] = {0.0F, 0.0F, 1.0F};
            // Bei der HAELFTE der Lebenszeit ist die Blende 0,5.
            const bhed::BspMesh m =
                bhed::buildEffectMesh({in}, 500.0, rechts, hoch);
            if (m.verts.empty()) {
                expect("es entsteht ueberhaupt ein Viereck", false);
                continue;
            }
            const auto& v = m.verts[0];
            std::printf("     %s: Farbe %d, Alpha %d\n",
                        mitFlag ? "mit useAlpha " : "ohne useAlpha",
                        (int)v.colour[0], (int)v.colour[3]);
            if (mitFlag) {
                expect("mit useAlpha bleibt die Farbe voll",
                       v.colour[0] > 240);
                expect("und die Blende steht im Alphakanal",
                       v.colour[3] > 100 && v.colour[3] < 160);
            } else {
                expect("ohne useAlpha wird die Farbe halbiert",
                       v.colour[0] > 100 && v.colour[0] < 160);
                expect("und der Alphakanal bleibt voll", v.colour[3] > 240);
            }
        }
    }

    // --- depthHack: der Effekt gewinnt gegen die Wand ----------------------
    //
    // 5 Vorkommen - selten, aber auffaellig, wenn es fehlt: ein
    // Muendungsblitz verschwindet dann hinter der Wand, obwohl er davor
    // liegen soll.
    //
    // Die Engine schaltet dafuer NICHT den Tiefentest ab, sondern staucht
    // den Bereich (tr_backend.cpp:818):
    //
    //     qglDepthRange (0, .3);
    //
    // Geprueft wird die Wirkung: mit dem Flag traegt der Stapel den Faktor
    // 0,3, ohne den Faktor 1. Das ist die Zahl, an der alles haengt.
    {
        auto bau = [](bool mitFlag) {
            std::string t =
                "Particle\n"
                "{\n"
                "\tlife\t\t1000 1000\n"
                "\trgb\n\t{\n\t\tstart 1 1 1\n\t\tend 1 1 1\n\t}\n"
                "\tsize\n\t{\n\t\tstart 20\n\t\tend 20\n\t}\n";
            if (mitFlag) {
                t += "\tflags\t\tdepthHack\n";
            }
            t += "}\n";
            return t;
        };
        for (int i = 0; i < 2; ++i) {
            const bool mitFlag = (i == 1);
            const auto r = bhed::efx::read(bau(mitFlag));
            if (r.effect.primitives.empty()) {
                expect("die Probendatei ist lesbar", false);
                continue;
            }
            bhed::EffectInstance in;
            in.effect = &r.effect;
            in.startMs = 0.0;
            in.seed = 5U;
            const float rechts[3] = {0.0F, 1.0F, 0.0F};
            const float hoch[3] = {0.0F, 0.0F, 1.0F};
            const bhed::BspMesh m =
                bhed::buildEffectMesh({in}, 300.0, rechts, hoch);
            if (m.batches.empty()) {
                expect("es entsteht ein Stapel", false);
                continue;
            }
            std::printf("     %s: depthScale %.2f\n",
                        mitFlag ? "mit depthHack " : "ohne depthHack",
                        static_cast<double>(m.batches[0].depthScale));
            if (mitFlag) {
                expect("mit depthHack wird die Tiefe gestaucht",
                       near(m.batches[0].depthScale, 0.3F, 0.001F));
            } else {
                expect("ohne depthHack bleibt sie unveraendert",
                       near(m.batches[0].depthScale, 1.0F, 0.001F));
            }
        }
    }

    // --- Decal: flach AUF der Flaeche --------------------------------------
    //
    // 41 Vorkommen - der haeufigste noch ungezeichnete Typ.
    //
    // Die Engine legt ein Quadrat der Kante 2*size um den Auftreffpunkt,
    // senkrecht zur Flaechennormalen (CG_ImpactMark, cg_marks.cpp:160):
    //
    //     axis[0] = normale
    //     ecke = origin +/- radius*axis[1] +/- radius*axis[2]
    //
    // Geprueft wird die Lage, denn genau die kann man falsch bekommen:
    // steht das Viereck senkrecht zur Wand statt darauf, sieht man es von
    // vorn als Strich.
    {
        const char* text =
            "Decal\n"
            "{\n"
            "\tlife\t\t1000 1000\n"
            "\trgb\n\t{\n\t\tstart 1 1 1\n\t\tend 1 1 1\n\t}\n"
            "\tsize\n\t{\n\t\tstart 10\n\t\tend 10\n\t}\n"
            "}\n";
        const auto r = bhed::efx::read(text);
        expect("die Decal-Datei ist lesbar", !r.effect.primitives.empty());
        if (!r.effect.primitives.empty()) {
            bhed::EffectInstance in;
            in.effect = &r.effect;
            in.startMs = 0.0;
            in.seed = 11U;
            // Eine WAND: Normale zeigt nach +x.
            in.normal[0] = 1.0F;
            in.normal[1] = 0.0F;
            in.normal[2] = 0.0F;
            const float rechts[3] = {0.0F, 1.0F, 0.0F};
            const float hoch[3] = {0.0F, 0.0F, 1.0F};
            const bhed::BspMesh m =
                bhed::buildEffectMesh({in}, 300.0, rechts, hoch);
            expect("es entsteht ein Viereck", m.verts.size() == 4U);
            if (m.verts.size() == 4U) {
                // ALLE vier Ecken muessen in derselben Ebene liegen, und
                // zwar zwei Einheiten VOR der Wand (der Versatz gegen das
                // Flimmern, wie beim Aufpralleffekt in rc287).
                bool inEbene = true;
                for (const auto& v : m.verts) {
                    if (!near(v.xyz[0], 2.0F, 0.01F)) {
                        inEbene = false;
                    }
                }
                expect("alle vier Ecken liegen in der Wandebene", inEbene);
                // Und sie spannen die volle Kante auf: 2*size = 20.
                float minY = 1e9F;
                float maxY = -1e9F;
                for (const auto& v : m.verts) {
                    minY = std::min(minY, v.xyz[1]);
                    maxY = std::max(maxY, v.xyz[1]);
                }
                expect("die Kante ist doppelt so lang wie size",
                       near(maxY - minY, 20.0F, 0.1F));
            }
        }
    }

    // --- FxRunner: spielt einen anderen Effekt -----------------------------
    //
    // 19 Vorkommen, und der einfachste Typ von allen. Die ganze Behandlung
    // in der Engine (FxScheduler.cpp:1891):
    //
    //     case FxRunner:
    //         PlayEffect( fx->mPlayFxHandles.GetHandle(), org, ax );
    //         break;
    //
    // Kein Bild, keine Bahn, kein Alter.
    //
    // Aufbau aus effects/atst/wall_impact.efx abgeschrieben - `playfx` ist
    // eine NAMENSLISTE und hat deshalb eckige Klammern, anders als die
    // Bloecke `rgb` und `alpha`. Genau diese Verwechslung hat mir in rc330
    // eine Probe verdorben.
    {
        const char* text =
            "FxRunner\n"
            "{\n"
            "\tcount\t\t\t1 2\n"
            "\tdelay\t\t\t0 100\n"
            "\tplayfx\n"
            "\t[\n"
            "\t\ttest/rauch\n"
            "\t]\n"
            "}\n";
        const auto r = bhed::efx::read(text);
        expect("die FxRunner-Datei ist lesbar", !r.effect.primitives.empty());
        if (!r.effect.primitives.empty()) {
            bhed::EffectInstance in;
            in.effect = &r.effect;
            in.startMs = 250.0;
            in.seed = 13U;
            in.origin[0] = 100.0F;
            in.origin[1] = 200.0F;
            in.origin[2] = 300.0F;
            // --- count und delay GELTEN (rc569) ----------------------
            //
            // Bis rc568 stand hier "genau einer, und sofort" - behaved
            // spielte einen FxRunner einmal ab. Die Engine plant ihn wie
            // jedes andere Primitiv: `count`-mal, jedes mit eigener
            // Verzoegerung (PlayEffect, FxScheduler.cpp:1188 ff.). Bei
            // `count 1 2` und `delay 0 100` sind es also ein oder zwei
            // Effekte, jeder zwischen 250 und 350 ms.
            const auto e = bhed::effectImpactsAt({in}, 1000.0, nullptr);
            expect("der Runner meldet ein oder zwei Effekte",
                   e.size() == 1U || e.size() == 2U);
            bool alleRichtig = !e.empty();
            for (const auto& f : e) {
                // AM ORT DES RUNNERS, nicht im Ursprung - das ist die Zahl,
                // die man leicht vergisst.
                alleRichtig = alleRichtig && f.effectName == "test/rauch" &&
                              near(f.origin[0], 100.0F, 0.01F) &&
                              near(f.origin[2], 300.0F, 0.01F) &&
                              f.startMs >= 249.99 && f.startMs <= 350.01;
            }
            expect("mit Namen, am Ort des Runners, innerhalb von delay",
                   alleRichtig);
            // Ueber viele Samen muessen beide Anzahlen vorkommen - sonst
            // wuerde `count` doch nicht gewuerfelt.
            bool einer = false;
            bool zwei = false;
            bool verzoegert = false;
            // Die Samen GESPREIZT: Random ist ein LCG, und dessen erster
            // Wert haengt kaum vom Samen ab (1, 2, 3 ... ergeben fast
            // dieselbe Zahl). Die Ansicht spreizt ihre Samen ebenso.
            for (unsigned sd = 1U; sd < 64U; ++sd) {
                in.seed = sd * 2654435761U;
                const auto ee = bhed::effectImpactsAt({in}, 1000.0, nullptr);
                einer = einer || ee.size() == 1U;
                zwei = zwei || ee.size() == 2U;
                for (const auto& f : ee) {
                    verzoegert = verzoegert || f.startMs > 260.0;
                }
            }
            expect("count 1 2 ergibt mal einen, mal zwei", einer && zwei);
            expect("und delay verschiebt den Start", verzoegert);
        }
    }

    // --- Kurvenflags: Dateibits -> Auswerterbits (rc568) ------------------
    //
    // Die Datei traegt die Engine-Bits (linear 1, random 2, nonlinear 4,
    // wave 8, clamp 0xC). Vorher gingen sie ungewandelt in den Auswerter:
    // `clamp` lief als Welle mit Zufall, `nonlinear` als Welle.
    {
        namespace cv = bhed::efx::curve;
        expect("Kurvenflags: linear bleibt linear",
               cv::ausDatei(bhed::efx::kCurveLinear) == cv::kLinear);
        expect("nonlinear wird nonlinear, nicht wave",
               cv::ausDatei(bhed::efx::kCurveNonLinear) == cv::kNonLinear);
        expect("clamp wird clamp",
               cv::ausDatei(bhed::efx::kCurveClamp) == cv::kClamp);
        expect("random wird random",
               cv::ausDatei(bhed::efx::kCurveRandom) == cv::kRandom);
        // `length start -40 end -1285 parm 30 flags linear clamp` wie in
        // mustafar/volcano1: waechst schnell, dann langsam - und schwingt
        // NICHT zurueck.
        cv::Curve c;
        c.start = -40.0F;
        c.end = -1285.0F;
        c.parm = 30.0F;
        c.flags = cv::ausDatei(bhed::efx::kCurveLinear | bhed::efx::kCurveClamp);
        const float life = 2000.0F;
        const float pp = cv::resolveParm(c, 0.0F, life);
        float vorher = 0.0F;
        bool monoton = true;
        for (int i = 0; i <= 20; ++i) {
            const float t = life * static_cast<float>(i) / 20.0F;
            const float l = -cv::evaluate(c, t, 0.0F, life, pp);
            if (l + 0.01F < vorher) { monoton = false; }
            vorher = l;
        }
        const float bei30 = -cv::evaluate(c, 0.3F * life, 0.0F, life, pp);
        expect("linear clamp waechst ohne Zurueckschwingen", monoton);
        expect("und ist beim Klemmpunkt schon ueber die Haelfte",
               bei30 > 0.5F * 1285.0F && bei30 < 1285.0F);
    }

    // Blitz, Vollbildblitz, Emitter, Folgeeffekte, FX_RELATIVE, useBBox
    probenRc569();

    // SpurHilfe, Spurbuch, Vorrechnen: schneller, aber bitgleich
    probenLeistung();

    std::printf("\n%s (%d Fehlschlaege)\n",
                g_fails == 0 ? "alle Effektproben bestanden" : "FEHLGESCHLAGEN",
                g_fails);
    return g_fails == 0 ? 0 : 1;
}
