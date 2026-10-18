// Probe fuer bhed/saberspuren.h (Paket saber, rc601, Review): Brandspuren der
// Lichtschwerter wie CG_CreateSaberMarks, R_MarkFragments, cg_marks.cpp und
// R_SubdividePatchToGrid.
//
// Offen gewesen (NOTIZEN_saber.md, Abschnitt 9.4): "Brandspuren an Waenden ... werden
// noch nicht gezeichnet - es fehlen CM_MarkFragments und ein Markenspeicher".
#include "bhed/bspgeo.h"
#include "bhed/saberkontakt.h"
#include "bhed/saberspuren.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
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
bool nah(float a, float b, float eps = 0.01F) { return std::fabs(a - b) <= eps; }

// Eine Welt mit einem Knoten (beide Seiten Blatt 0) und einer Wand bei x = 20
// (Quadrat y,z in [-50, 50], Normale -x), zwei Dreiecke.
SpurWelt wandWelt(bool ausgenommen = false, float nx = -1.0F) {
    SpurWelt w;
    const float n[3] = {1.0F, 0.0F, 0.0F};
    w.ebenen.push_back(spurEbene(n, 1000.0F));
    SpurWelt::Knoten k;
    k.ebene = 0;
    k.kinder[0] = -1;
    k.kinder[1] = -1;
    w.knoten.push_back(k);
    w.blaetter.push_back(SpurWelt::Blatt{0, 1});
    w.blattFlaechen.push_back(0);
    SpurFlaeche f;
    f.art = SpurFlaeche::Art::Flaeche;
    f.ausgenommen = ausgenommen;
    f.punkte = {{20.0F, -50.0F, -50.0F}, {20.0F, 50.0F, -50.0F}, {20.0F, 50.0F, 50.0F}, {20.0F, -50.0F, 50.0F}};
    f.indizes = {0, 1, 2, 0, 2, 3};
    const float fn[3] = {nx, 0.0F, 0.0F};
    f.ebene = spurEbene(fn, 20.0F * nx);
    for (int i = 0; i < 3; ++i) {
        f.mins[i] = i == 0 ? 20.0F : -50.0F;
        f.maxs[i] = i == 0 ? 20.0F : 50.0F;
    }
    w.flaechen.push_back(f);
    w.gesehen.assign(1, 0);
    return w;
}
}  // namespace

int main() {
    // --- q_math -------------------------------------------------------------
    pruefe(nah(spurRsqrt(4.0F), 0.49915F, 0.0001F), "Q_rsqrt(4) mit einer Newton-Stufe (0,49915, nicht 0,5)");
    {
        const float n[3] = {0.0F, 0.0F, 1.0F};
        const SpurEbene p = spurEbene(n, 10.0F);
        const float a0[3] = {0, 0, 11};
        const float a1[3] = {1, 1, 12};
        const float b0[3] = {0, 0, 0};
        const float b1[3] = {1, 1, 9};
        const float c1[3] = {1, 1, 20};
        pruefe(p.typ == 2 && spurKastenSeite(a0, a1, p) == 1 && spurKastenSeite(b0, b1, p) == 2 &&
                   spurKastenSeite(b0, c1, p) == 3,
               "BoxOnPlaneSide: axial vorn/hinten/beides");
        const float m[3] = {0.0F, -0.6F, 0.8F};
        const SpurEbene q = spurEbene(m, 0.0F);
        pruefe(q.typ == 3 && q.vorzeichen == 2, "PlaneTypeForNormal/SetPlaneSignbits schraeg");
    }
    // --- CG_CreateSaberMarks + R_MarkFragments ----------------------------------
    {
        setzeSaberMovieDuelsRegeln(true);
        const SpurWelt w = wandWelt();
        const float s[3] = {20.0F, -5.0F, 10.0F};
        const float e[3] = {20.0F, 5.0F, 10.0F};
        const float n[3] = {-1.0F, 0.0F, 0.0F};
        std::vector<SpurPoly> neu;
        saberSpurenErzeugen(w, s, e, n, 4000, 77U, neu);
        pruefe(neu.size() == 2U, "MD: ein Bruchstueck -> Brandspur und Gluehen (" + std::to_string(neu.size()) + ")");
        if (neu.size() == 2U) {
            const SpurPoly& b = neu[0];
            const SpurPoly& g = neu[1];
            bool aufWand = b.zahl == 4;
            float ymin = 1e9F;
            float ymax = -1e9F;
            float zmin = 1e9F;
            float zmax = -1e9F;
            for (int j = 0; j < b.zahl; ++j) {
                aufWand = aufWand && nah(b.xyz[j][0], 20.0F, 0.001F);
                ymin = std::min(ymin, b.xyz[j][1]);
                ymax = std::max(ymax, b.xyz[j][1]);
                zmin = std::min(zmin, b.xyz[j][2]);
                zmax = std::max(zmax, b.xyz[j][2]);
            }
            pruefe(aufWand && nah(ymin, -5.65F) && nah(ymax, 5.65F) && nah(zmin, 9.35F) && nah(zmax, 10.65F),
                   "Viereck Radius 0,65 von oldPos bis endpos auf der Wand");
            pruefe(b.shader == 0 && b.alphaFade && b.zeit == 4000 && b.farbe[0] == 255 && b.farbe[3] == 255,
                   "Brandspur: rivetmark, alphaFade, time = cg.time, Farbe 255");
            pruefe(g.shader == 1 && !g.alphaFade && g.zeit == 4000 - 8500 && g.farbe[0] >= 215 && g.farbe[0] <= 255 &&
                       g.farbe[1] >= 96 && g.farbe[1] <= 128 && g.farbe[2] <= 15 && g.farbe[2] == g.farbe[3],
                   "Gluehen: saberDamageGlow, time - 8500, Farbe 215+r*40 / 96+r*32 / r*15");
            // MD: st = 0.5 + dot(delta, axis1) * 0.06 - die Ecke (20,-5.65,9.35)
            bool stOk = false;
            for (int j = 0; j < b.zahl; ++j) {
                if (nah(b.xyz[j][1], -5.65F) && nah(b.xyz[j][2], 9.35F)) {
                    stOk = nah(b.st[j][0], 0.5F - 5.65F * 0.06F, 0.001F) &&
                           (nah(b.st[j][1], 0.5F - 0.65F * 0.12F, 0.001F) || nah(b.st[j][1], 0.5F + 0.65F * 0.12F, 0.001F));
                }
            }
            pruefe(stOk, "MD: feste Texturkoordinaten 0,06 / 0,12");
        }
        // gleiche Saat -> gleiche Farben (Vorspulen)
        std::vector<SpurPoly> noch;
        saberSpurenErzeugen(w, s, e, n, 4000, 77U, noch);
        pruefe(noch.size() == neu.size() && noch[1].farbe[0] == neu[1].farbe[0] && noch[1].farbe[2] == neu[1].farbe[2],
               "gleiche Saat, gleiches Gluehen");
        // Ohne Bewegung: MD bricht ab ("Degenerate slash direction")
        std::vector<SpurPoly> still;
        saberSpurenErzeugen(w, s, s, n, 4000, 77U, still);
        pruefe(still.empty(), "MD: start == end -> keine Marke");
        // OpenJK: zufaellige Texturkoordinaten, dieselbe Form
        setzeSaberMovieDuelsRegeln(false);
        std::vector<SpurPoly> oj;
        saberSpurenErzeugen(w, s, e, n, 4000, 77U, oj);
        bool ojOk = oj.size() == 2U && oj[0].zahl == 4;
        for (int j = 0; ojOk && j < oj[0].zahl; ++j) {
            const float dy = oj[0].xyz[j][1];
            const float f0 = (oj[0].st[j][0] - 0.5F) / dy;   // 0.05 .. 0.08
            ojOk = f0 >= 0.0499F && f0 <= 0.0801F;
        }
        pruefe(ojOk, "OpenJK: st[0] = 0.5 + d * (0.05 + Q_flrand*0.03)");
        std::vector<SpurPoly> ojStill;
        saberSpurenErzeugen(w, s, s, n, 4000, 77U, ojStill);
        pruefe(ojStill.empty(), "OpenJK: start == end -> Viereck faellt zusammen, kein Bruchstueck");
        setzeSaberMovieDuelsRegeln(true);
        // Abgewandte Flaeche (DotProduct(normal, projectionDir) > -0.5) und
        // SURF_NOMARKS/NOIMPACT/CONTENTS_FOG
        std::vector<SpurPoly> weg;
        saberSpurenErzeugen(wandWelt(false, 1.0F), s, e, n, 4000, 1U, weg);
        saberSpurenErzeugen(wandWelt(true), s, e, n, 4000, 1U, weg);
        pruefe(weg.empty(), "abgewandte und ausgenommene Flaechen bekommen keine Marke");
        // Ueber die Kante: das Viereck wird an der Wand abgeschnitten
        const float s2[3] = {20.0F, 45.0F, 10.0F};
        const float e2[3] = {20.0F, 60.0F, 10.0F};
        std::vector<SpurPoly> kante;
        saberSpurenErzeugen(w, s2, e2, n, 4000, 1U, kante);
        float ymax = -1e9F;
        for (const SpurPoly& p : kante) {
            for (int j = 0; j < p.zahl; ++j) { ymax = std::max(ymax, p.xyz[j][1]); }
        }
        pruefe(!kante.empty() && nah(ymax, 50.0F), "an der Wandkante abgeschnitten (y bis 50)");
    }
    // --- Markenspeicher (cg_marks.cpp) -----------------------------------------
    {
        SpurSpeicher sp;
        for (int i = 0; i < 256; ++i) { sp.neu().zeit = 0; }
        pruefe(sp.zahl() == 256, "256 Plaetze (MAX_MARK_POLYS)");
        sp.neu().zeit = 8;
        pruefe(sp.zahl() == 1, "voll: die aelteste Gruppe gleicher Zeit faellt ganz (CG_AllocMark)");
        SpurSpeicher sp2;
        for (int i = 0; i < 300; ++i) { sp2.neu().zeit = i * 8; }
        const auto leb = sp2.lebende();
        pruefe(sp2.zahl() == 256 && leb.front()->zeit == 299 * 8 && leb.back()->zeit == 44 * 8,
               "verschiedene Zeiten: die 44 aeltesten fallen, neueste zuerst");
        SpurSpeicher sp3;
        sp3.neu().zeit = 1000;
        sp3.bild(11000);
        const int nachZehn = sp3.zahl();
        sp3.bild(11008);
        pruefe(nachZehn == 1 && sp3.zahl() == 0, "CG_AddMarks: fort, sobald cg.time > time + 10000");
        SpurPoly g;
        g.zeit = 5000 - 8500;
        g.farbe[0] = 230;
        g.farbe[1] = 100;
        g.farbe[2] = 10;
        std::uint8_t c[4];
        spurFarbe(g, 5400, c);
        const bool vorher = c[0] == 230 && c[1] == 100;
        spurFarbe(g, 6000, c);
        pruefe(vorher && c[0] == 115 && c[1] == 50 && c[2] == 5, "Gluehen: ab 0,5 s abblenden (Farbe * t/1000)");
        SpurPoly b;
        b.zeit = 0;
        b.alphaFade = true;
        b.farbe[0] = b.farbe[1] = b.farbe[2] = b.farbe[3] = 255;
        spurFarbe(b, 9500, c);
        pruefe(c[0] == 255 && c[3] == 127, "Brandspur: Alpha 255*t/1000 in der letzten Sekunde");
    }
    // --- R_SubdividePatchToGrid -------------------------------------------------
    {
        std::vector<std::array<float, 3>> flach;
        for (int j = 0; j < 3; ++j) {
            for (int i = 0; i < 3; ++i) { flach.push_back({32.0F * static_cast<float>(i), 32.0F * static_cast<float>(j), 0.0F}); }
        }
        std::vector<std::array<float, 3>> aus;
        int bw = 0;
        int bh = 0;
        spurGitter(3, 3, flach, 4.0F, aus, bw, bh);
        pruefe(bw == 2 && bh == 2 && nah(aus[3][0], 64.0F) && nah(aus[3][1], 64.0F),
               "flacher Patch: die Mittelspalten fallen (999), 2x2 (" + std::to_string(bw) + "x" + std::to_string(bh) + ")");
        // Ein Bogen in x/z, gerade in y
        std::vector<std::array<float, 3>> bogen;
        for (int j = 0; j < 3; ++j) {
            const float y = 32.0F * static_cast<float>(j);
            bogen.push_back({0.0F, y, 0.0F});
            bogen.push_back({32.0F, y, 64.0F});
            bogen.push_back({64.0F, y, 0.0F});
        }
        spurGitter(3, 3, bogen, 4.0F, aus, bw, bh);
        bool aufKurve = bw > 3 && bh == 2;
        for (int i = 0; aufKurve && i < bw; ++i) {
            const auto& p = aus[static_cast<std::size_t>(i)];
            float best = 1e9F;
            for (int k = 0; k <= 4000; ++k) {
                const float t = static_cast<float>(k) / 4000.0F;
                const float x = 2.0F * (1 - t) * t * 32.0F + t * t * 64.0F;
                const float z = 2.0F * (1 - t) * t * 64.0F;
                best = std::min(best, std::hypot(p[0] - x, p[2] - z));
            }
            aufKurve = best < 0.05F;
        }
        pruefe(aufKurve, "Bogen: unterteilt bis r_subdivisions 4, alle Punkte auf der Kurve (" + std::to_string(bw) +
                             "x" + std::to_string(bh) + ")");
    }
    // --- spurWeltAus -------------------------------------------------------------
    {
        BspGeometry g;
        g.planes.push_back(BspGeometry::Plane{{1.0F, 0.0F, 0.0F}, 1000.0F});
        BspGeometry::Node n;
        n.plane = 0;
        n.children[0] = -1;
        n.children[1] = -1;
        g.nodes.push_back(n);
        BspGeometry::Leaf l;
        l.firstSurface = 0;
        l.numSurfaces = 3;
        g.leafs.push_back(l);
        g.leafSurfaces = {0, 1, 2};
        BspShader sh0;
        sh0.name = "textures/wand";
        BspShader sh1;
        sh1.name = "textures/keine";
        sh1.surfaceFlags = 0x00100000U;   // SURF_NOMARKS
        BspShader sh2;
        sh2.name = "textures/nodraw";
        sh2.surfaceFlags = 0x00200000U;   // SURF_NODRAW
        g.shaders = {sh0, sh1, sh2};
        for (int i = 0; i < 4; ++i) {
            BspVertex v;
            v.xyz[0] = 20.0F;
            v.xyz[1] = (i == 1 || i == 2) ? 50.0F : -50.0F;
            v.xyz[2] = (i >= 2) ? 50.0F : -50.0F;
            g.verts.push_back(v);
        }
        g.indexes = {0, 1, 2, 0, 2, 3};
        BspSurface f;
        f.type = BspSurface::Type::Planar;
        f.shader = 0;
        f.firstVert = 0;
        f.numVerts = 4;
        f.firstIndex = 0;
        f.numIndexes = 6;
        f.ebenenNormale[0] = -1.0F;
        BspSurface f1 = f;
        f1.shader = 1;
        BspSurface p;
        p.type = BspSurface::Type::Patch;
        p.shader = 2;
        p.firstVert = 0;
        p.numVerts = 4;
        p.patchWidth = 2;
        p.patchHeight = 2;
        g.surfaces = {f, f1, p};
        const SpurWelt w = spurWeltAus(g);
        pruefe(w.flaechen.size() == 3U && w.flaechen[0].art == SpurFlaeche::Art::Flaeche &&
                   nah(w.flaechen[0].ebene.abstand, -20.0F) && !w.flaechen[0].ausgenommen &&
                   w.flaechen[1].ausgenommen && w.flaechen[2].art == SpurFlaeche::Art::Keine,
               "spurWeltAus: Ebene aus lightmapVecs[2], NOMARKS ausgenommen, NODRAW-Patch SF_SKIP");
        const float s[3] = {20.0F, -5.0F, 10.0F};
        const float e[3] = {20.0F, 5.0F, 10.0F};
        const float nn[3] = {-1.0F, 0.0F, 0.0F};
        std::vector<SpurPoly> neu;
        saberSpurenErzeugen(w, s, e, nn, 100, 5U, neu);
        pruefe(neu.size() == 2U, "spurWeltAus: Marke auf der gelesenen Wand, keine auf der NOMARKS-Kopie");
    }
    std::printf("%d Fehler\n", g_fehler);
    return g_fehler == 0 ? 0 : 1;
}
