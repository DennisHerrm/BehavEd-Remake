// app_gras.cpp - surfaceSprites in der Vorschau wie im Spiel (rc604)
//
// Gras, Buesche, Blumen und Regenspritzer: eine Shaderstufe mit
// `surfaceSprites` zeichnet die Engine NICHT als Schicht (tr_shade.cpp:1888
// MD, "continue"), sondern nach den anderen Stufen als viele kleine Vierecke
// auf jedem Dreieck der Flaeche (RB_DrawSurfaceSprites,
// tr_surfacesprites.cpp:1440). Die Rechnung steht in bhed/grasflaechen.h;
// hier kommen Karte, Bilder und Grafikkarte dazu:
//
//   - beim Laden (app.cpp, loadMapTextures) wird jede solche Stufe ein
//     eigener Textureintrag mit Eckenfarbe statt einer Schicht
//     (grasStufeLaden) - SQuickSprite (tr_quicksprite.cpp) bindet das Bild
//     der Stufe (R_BindAnimatedImage), setzt ihre stateBits (Mischart,
//     Alphatest, depthWrite), schaltet das Wegschneiden der Rueckseite ab
//     und nimmt als Farbe (licht, licht, licht, alpha) - rgbGen, alphaGen
//     und tcMod der Stufe gelten dabei NICHT;
//   - in jedem Bild baut baueGrasNetz die Vierecke aller Flaechen in
//     Reichweite, wie die Engine je Bild auf der CPU. Was nur von den Ecken
//     abhaengt (Stelle, Groesse, Spiegelung, Licht je Halm), rechnet es je
//     Flaeche EINMAL (gras::kandidaten) - das Ergebnis ist dasselbe, die
//     Zufallstabelle wird nur nicht in jedem Bild neu durchlaufen. Flaechen,
//     deren naechster Punkt weiter als fadeMax weg ist, und solche ganz
//     hinter der Kamera fallen vorher weg (die Engine laesst dort jedes
//     Dreieck an "a1 <= 0 && a2 <= 0 && a3 <= 0" fallen, :705 - dasselbe Bild).
#include "app_internal.h"
#include "bhed/grasflaechen.h"
#include "bhed/messpunkt.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bhed::gui {

namespace {

struct GrasFlaeche {
    int shader = 0;                 // Platz in geo.shaders / textures.byShader
    std::uint32_t surface = 0;      // Nummer in geo.surfaces (fuer den Nebel)
    int nebel = -1;                 // dsurface_t.fogNum - tess.fogNum der Halme
    std::uint32_t firstIndex = 0;   // Abschnitt in g_app->mesh (BspMesh::Run)
    std::uint32_t numIndexes = 0;
    float mins[3]{};
    float maxs[3]{};
    // je Stufe (App::grasStufen[shader]) die festen Halme; erst gebaut,
    // wenn die Flaeche zum ersten Mal in Reichweite kommt.
    std::vector<std::vector<gras::Kandidat>> kandidaten;
    std::vector<char> gebaut;
    // Ein Patch: die Dreiecke der ENGINE (R_SubdividePatchToGrid, je Bild
    // RB_SurfaceGrid mit LOD), nicht die, mit denen behaved zeichnet.
    bool patch = false;
    gras::PatchGitter gitter;
    std::vector<int> lodSpalten;
    std::vector<int> lodZeilen;
    std::vector<gras::Ecke> dreiecke;   // je drei Ecken
};

struct GrasStand {
    const std::uint32_t* idx = nullptr;
    std::size_t nIdx = 0;
    int stand = -1;
    std::vector<GrasFlaeche> flaechen;
};
GrasStand g_gras;
std::string g_grasZielShader;   // Selbsttest: Shader der gefundenen Flaeche
int g_grasPatchZahl = 0;        // Patches mit Halmen beim letzten Bau

// Die Ecke i des Netzes als Ecke von tess: xyz, normal[2] und die BLAUE
// Eckenfarbe (input->vertexColors[..][2], :649).
gras::Ecke ecke(const BspVertex& v) {
    gras::Ecke e;
    for (int k = 0; k < 3; ++k) { e.xyz[k] = v.xyz[k]; }
    e.normalZ = v.normal[2];
    e.licht = static_cast<float>(v.colour[2]);
    return e;
}

void flaechenBauen() {
    const BspMesh& m = g_app->mesh;
    if (g_gras.idx == m.indexes.data() && g_gras.nIdx == m.indexes.size() && g_gras.stand == g_app->grasStand) {
        return;
    }
    g_gras = GrasStand{};
    g_gras.idx = m.indexes.data();
    g_gras.nIdx = m.indexes.size();
    g_gras.stand = g_app->grasStand;
    for (const BspMesh::Batch& b : m.batches) {
        if (b.shader < 0 || static_cast<std::size_t>(b.shader) >= g_app->grasStufen.size() ||
            g_app->grasStufen[static_cast<std::size_t>(b.shader)].empty()) {
            continue;
        }
        const std::size_t stufen = g_app->grasStufen[static_cast<std::size_t>(b.shader)].size();
        const std::uint32_t ende = std::min<std::uint32_t>(b.firstRun + b.numRuns, static_cast<std::uint32_t>(m.runs.size()));
        for (std::uint32_t r = b.firstRun; r < ende; ++r) {
            const BspMesh::Run& run = m.runs[r];
            if (run.numIndexes < 3U || static_cast<std::size_t>(run.firstIndex) + run.numIndexes > m.indexes.size()) {
                continue;
            }
            GrasFlaeche f;
            f.shader = b.shader;
            f.surface = run.surface;
            if (run.surface < g_app->geo.surfaces.size()) { f.nebel = g_app->geo.surfaces[run.surface].fog; }
            f.firstIndex = run.firstIndex;
            f.numIndexes = run.numIndexes;
            for (int k = 0; k < 3; ++k) {
                f.mins[k] = 1.0e30F;
                f.maxs[k] = -1.0e30F;
            }
            for (std::uint32_t i = 0; i < run.numIndexes; ++i) {
                const std::uint32_t vi = m.indexes[run.firstIndex + i];
                if (vi >= m.verts.size()) { continue; }
                for (int k = 0; k < 3; ++k) {
                    f.mins[k] = std::min(f.mins[k], m.verts[vi].xyz[k]);
                    f.maxs[k] = std::max(f.maxs[k], m.verts[vi].xyz[k]);
                }
            }
            f.kandidaten.resize(stufen);
            f.gebaut.assign(stufen, 0);
            if (run.surface < g_app->geo.surfaces.size()) {
                const BspSurface& bs = g_app->geo.surfaces[run.surface];
                if (bs.type == BspSurface::Type::Patch && bs.firstVert >= 0 &&
                    static_cast<std::size_t>(bs.firstVert) + static_cast<std::size_t>(bs.numVerts) <=
                        g_app->geo.verts.size() &&
                    bs.patchWidth * bs.patchHeight == bs.numVerts) {
                    // ParseMesh (tr_bsp.cpp MD): die Kontrollpunkte, dann
                    // R_SubdividePatchToGrid; lodOrigin/lodRadius aus
                    // lightmapVecs[0..1].
                    std::vector<gras::PatchPunkt> pp(static_cast<std::size_t>(bs.numVerts));
                    for (int k = 0; k < bs.numVerts; ++k) {
                        const BspVertex& v = g_app->geo.verts[static_cast<std::size_t>(bs.firstVert + k)];
                        gras::PatchPunkt& q = pp[static_cast<std::size_t>(k)];
                        for (int c = 0; c < 3; ++c) {
                            q.xyz[c] = v.xyz[c];
                            q.normal[c] = v.normal[c];
                        }
                        for (int c = 0; c < 4; ++c) { q.farbe[c] = v.colour[c]; }
                    }
                    f.gitter = gras::unterteilePatch(bs.patchWidth, bs.patchHeight, pp.data());
                    if (f.gitter.breite >= 2 && f.gitter.hoehe >= 2) {
                        f.patch = true;
                        float b1[3];
                        for (int k = 0; k < 3; ++k) {
                            b1[k] = bs.patchLod[0][k] + bs.patchLod[1][k];
                            f.gitter.lodMitte[k] = b1[k] * 0.5F;
                            f.mins[k] = std::min(f.mins[k], f.gitter.mins[k]);
                            f.maxs[k] = std::max(f.maxs[k], f.gitter.maxs[k]);
                        }
                        const float d[3] = {bs.patchLod[0][0] - f.gitter.lodMitte[0],
                                            bs.patchLod[0][1] - f.gitter.lodMitte[1],
                                            bs.patchLod[0][2] - f.gitter.lodMitte[2]};
                        f.gitter.lodRadius = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    }
                }
            }
            g_gras.flaechen.push_back(std::move(f));
        }
    }
}

// Abstand vom Auge zum naechsten Punkt des Kastens.
float kastenAbstand(const float* auge, const float* mins, const float* maxs) {
    float d2 = 0.0F;
    for (int k = 0; k < 3; ++k) {
        float d = 0.0F;
        if (auge[k] < mins[k]) { d = mins[k] - auge[k]; }
        else if (auge[k] > maxs[k]) { d = auge[k] - maxs[k]; }
        d2 += d * d;
    }
    return std::sqrt(d2);
}

// Wie weit ein Halm hoechstens aus dem Kasten seiner Flaeche ragt.
float halmUeberstand(const SurfaceSprite& ss) {
    const float h = ss.height * (1.0F + ss.variance[1]) * (1.0F + ss.fxGrow[1]) * (1.0F + std::fabs(ss.vertSkew) * 2.0F);
    const float w = ss.width * (1.0F + ss.variance[0]) * (1.0F + ss.fxGrow[0]) * (1.0F + std::fabs(ss.fadeScale));
    return h * (1.0F + ss.wind + ss.windIdle) + w + 2.0F;
}

}  // namespace

void grasStufeLaden(const ShaderInfo& info, const ShaderInfo::Stage& st, std::size_t stufenNr, const std::string& name,
                    std::size_t shaderNr) {
    // tr_shade.cpp:2149 MD - nur Stufe 1 .. numUnfoggedPasses-1 wird als
    // Bilder gezeichnet; an Platz 0 gar nicht (auch nicht als Schicht).
    if (stufenNr < 1U) { return; }
    TextureSet::Tex huelle;
    const std::size_t vorher = g_app->textures.byShader.size();
    zusatzstufeLaden(info, st, name, huelle);
    if (huelle.extraStages.empty()) { return; }
    // SQuickSprite: Eckenfarbe (licht, licht, licht, alpha), kein rgbGen,
    // alphaGen oder tcMod der Stufe, beide Seiten (qglDisable(GL_CULL_FACE),
    // tr_quicksprite.cpp StartGroup). Fuer alle Bilder einer animMap.
    for (std::size_t k = vorher; k < g_app->textures.byShader.size(); ++k) {
        TextureSet::Tex& z = g_app->textures.byShader[k];
        z.rgbVertex = true;
        for (float& c : z.rgbConst) { c = -1.0F; }
        z.rgbWave = Wave{};
        z.alphaGen = AlphaGen::Vertex;
        z.alphaWave = Wave{};
        z.alphaConst = -1.0F;
        z.numTexMods = 0;
        z.texGen = TexGen::Base;
        z.cull = CullMode::Two;
        z.glow = false;
        z.deformWave = Wave{};
        z.deformSpread = 0.0F;
        z.deform2 = DeformZusatz{};
        z.autosprite = false;
        z.lightmapStufe = false;
        z.portalWeite = 0.0F;
    }
    if (shaderNr >= g_app->grasStufen.size()) { g_app->grasStufen.resize(shaderNr + 1U); }
    App::GrasStufe gs;
    gs.platz = huelle.extraStages[0];
    gs.ss = st.ss;
    gs.stufenNr = stufenNr;
    // :1461 - GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE: additiv, Licht statt Alpha
    gs.additiv = (st.srcFactor == BlendFactor::One && st.dstFactor == BlendFactor::One);
    g_app->grasStufen[shaderNr].push_back(gs);
    ++g_app->grasStand;
}

namespace {

// Die Halme fuer EINE Sicht - RB_DrawSurfaceSprites rechnet mit den
// viewParms des Bildes, das gerade entsteht (auch Kameravorschau und
// Himmelsportal sind eigene Sichten).
void baueGras(const Camera& cam, float reichweite, BspMesh& netz, int& vierecke, int& flaechenZahl) {
    BHED_MESS("Gras");
    netz.verts.clear();
    netz.indexes.clear();
    netz.batches.clear();
    netz.runs.clear();
    vierecke = 0;
    flaechenZahl = 0;
    g_grasPatchZahl = 0;
    if (!g_app->zeigeGras || g_app->grasStufen.empty() || g_app->mesh.indexes.empty()) { return; }
    flaechenBauen();
    if (g_gras.flaechen.empty()) { return; }

    // R_SurfaceSpriteFrameUpdate (:106) und RB_DrawSurfaceSprites (:1471)
    gras::Rahmen r;
    float rechts[3];
    float vorn[3];
    cam.right(rechts);
    cam.up(r.oben);
    cam.forward(vorn);
    for (int k = 0; k < 3; ++k) {
        r.auge[k] = cam.pos[k];
        r.links[k] = -rechts[k];   // ori.axis[1] zeigt nach links
    }
    r.reichweite = reichweite;
    r.zeitMs = static_cast<int>(std::max(0.0, g_app->playMs));
    float wind[3];
    const bool regnet = wetterFuerGras(wind);   // R_IsRaining, Wetterzonen
    r.wetter = regnet ? 1.0F : 0.0F;            // :188-195 (r_surfaceWeather 0)
    {
        const float tempo = std::sqrt(wind[0] * wind[0] + wind[1] * wind[1] + wind[2] * wind[2]);
        const float x[3] = {1.0F, 0.0F, 0.0F};   // mGlobalWindDirection ohne Wind
        if (tempo > 0.0F) {
            const float d[3] = {wind[0] / tempo, wind[1] / tempo, wind[2] / tempo};
            gras::windSetzen(tempo, d, r);
        } else {
            gras::windSetzen(0.0F, x, r);
        }
    }
    gras::rahmenFertig(r);

    // Je Bild (Textureintrag) ein Zeichenaufruf, wie SQuickSprite je Stufe.
    // Schluessel: Platz und Nebel - je Nebel ein Abschnitt (Run) mit einer
    // Flaeche dieses Nebels, damit zeichneNebel die Halme einfaerbt.
    struct Gruppe {
        std::vector<gras::Quad> quads;
        std::uint32_t flaeche = 0;
    };
    std::unordered_map<long long, Gruppe> jePlatz;
    std::unordered_set<int> additivPlatz;
    std::vector<gras::Quad> quads;
    std::vector<int> lodSp;
    std::vector<int> lodZe;
    const BspMesh& m = g_app->mesh;
    for (GrasFlaeche& f : g_gras.flaechen) {
        const std::vector<App::GrasStufe>& stufen = g_app->grasStufen[static_cast<std::size_t>(f.shader)];
        bool etwas = false;
        for (std::size_t si = 0; si < stufen.size() && si < f.kandidaten.size(); ++si) {
            const App::GrasStufe& gs = stufen[si];
            if (!gras::stufeZeichnet(gs.ss, gs.stufenNr, r.wetter)) { continue; }
            // Ausser Reichweite: jede Ecke hat alpha <= 0 (:705, :1316).
            if (kastenAbstand(r.auge, f.mins, f.maxs) >= gras::reichweite(gs.ss, r)) { continue; }
            // Ganz hinter der Kamera (Halme eingerechnet): unsichtbar.
            {
                const float rand = halmUeberstand(gs.ss);
                float naechst = -1.0e30F;
                for (int c = 0; c < 8; ++c) {
                    const float p[3] = {((c & 1) != 0 ? f.maxs[0] : f.mins[0]) - r.auge[0],
                                        ((c & 2) != 0 ? f.maxs[1] : f.mins[1]) - r.auge[1],
                                        ((c & 4) != 0 ? f.maxs[2] : f.mins[2]) - r.auge[2]};
                    naechst = std::max(naechst, p[0] * vorn[0] + p[1] * vorn[1] + p[2] * vorn[2]);
                }
                if (naechst < -rand) { continue; }
            }
            quads.clear();
            if (f.patch) {
                // RB_SurfaceGrid: welche Zeilen und Spalten bei dieser Sicht.
                // Aendert sich das, liegen die Halme anders - wie im Spiel.
                gras::gitterLod(f.gitter, r.auge, vorn, lodSp, lodZe);
                if (lodSp != f.lodSpalten || lodZe != f.lodZeilen || f.dreiecke.empty()) {
                    f.lodSpalten = lodSp;
                    f.lodZeilen = lodZe;
                    f.dreiecke.clear();
                    gras::gitterDreiecke(f.gitter, lodSp, lodZe, f.dreiecke);
                    for (std::size_t k = 0; k < f.gebaut.size(); ++k) {
                        f.gebaut[k] = 0;
                        f.kandidaten[k].clear();
                    }
                }
            }
            // Die Dreiecke der Flaeche: beim Patch die der Engine, sonst die
            // der Karte (dieselben Ecken in derselben Reihenfolge wie tess).
            const std::size_t dreieckZahl = f.patch ? f.dreiecke.size() / 3U : f.numIndexes / 3U;
            const auto dreieckAt = [&](std::size_t n, gras::Ecke e[3]) {
                for (int k = 0; k < 3; ++k) {
                    e[k] = f.patch ? f.dreiecke[n * 3U + static_cast<std::size_t>(k)]
                                   : ecke(m.verts[m.indexes[f.firstIndex + n * 3U + static_cast<std::size_t>(k)]]);
                }
            };
            const bool effekt = (gs.ss.typ == 3 || gs.ss.typ == 4);
            if (!effekt && f.gebaut[si] == 0) {
                for (std::size_t n = 0; n < dreieckZahl; ++n) {
                    gras::Ecke e[3];
                    dreieckAt(n, e);
                    gras::kandidaten(gs.ss, e, f.kandidaten[si]);
                }
                f.gebaut[si] = 1;
            }
            if (effekt) {
                for (std::size_t n = 0; n < dreieckZahl; ++n) {
                    gras::Ecke e[3];
                    dreieckAt(n, e);
                    gras::effektDreieck(gs.ss, e, r, gs.additiv, quads);
                }
            } else {
                gras::zeichneKandidaten(gs.ss, f.kandidaten[si].data(), f.kandidaten[si].size(), r, gs.additiv, quads);
            }
            if (!quads.empty()) {
                if (gs.additiv) { additivPlatz.insert(gs.platz); }
                const long long schluessel = (static_cast<long long>(gs.platz) << 20) + (f.nebel + 1);
                Gruppe& ziel = jePlatz[schluessel];
                ziel.flaeche = f.surface;
                ziel.quads.insert(ziel.quads.end(), quads.begin(), quads.end());
                etwas = true;
            }
        }
        if (etwas) {
            ++flaechenZahl;
            if (f.patch) { ++g_grasPatchZahl; }
        }
    }
    // tr_quicksprite.cpp:38 - (1,1) (1,0) (0,0) (0,1); GL_QUADS als zwei Dreiecke
    static const float kSt[4][2] = {{1.0F, 1.0F}, {1.0F, 0.0F}, {0.0F, 0.0F}, {0.0F, 1.0F}};
    std::size_t gesamt = 0;
    for (const auto& [schluessel, g] : jePlatz) { gesamt += g.quads.size(); }
    netz.verts.reserve(gesamt * 4U);
    netz.indexes.reserve(gesamt * 6U);
    for (const auto& [schluessel, g] : jePlatz) {
        const int platz = static_cast<int>(schluessel >> 20);
        const std::vector<gras::Quad>& liste = g.quads;
        BspMesh::Batch b;
        b.lightmap = -1;
        b.shader = platz;
        // Die Eckenfarbe ist das LICHT der Karte (vertexColors, :649), keine
        // fertige Effektfarbe - sie bekommt deshalb dieselbe Helligkeit wie
        // die Flaechen mit rgbGen vertex (kein kRohEcke, gpushader.cpp).
        // Additive Bilder (Regenspritzer) tragen dagegen einen fertigen
        // Helligkeitswert ((128 + licht/2) * alpha, :784) - wie Effekte roh.
        b.vertexColour = additivPlatz.count(platz) != 0U;
        b.firstIndex = static_cast<std::uint32_t>(netz.indexes.size());
        for (const gras::Quad& q : liste) {
            const std::uint32_t basis = static_cast<std::uint32_t>(netz.verts.size());
            for (int i = 0; i < 4; ++i) {
                BspVertex v;
                for (int k = 0; k < 3; ++k) { v.xyz[k] = q.ecke[i][k]; }
                v.st[0] = kSt[i][0];
                v.st[1] = kSt[i][1];
                v.normal[2] = 1.0F;
                v.colour[0] = v.colour[1] = v.colour[2] = q.licht;
                v.colour[3] = q.alpha;
                netz.verts.push_back(v);
            }
            const std::uint32_t idx[6] = {basis, basis + 1U, basis + 2U, basis, basis + 2U, basis + 3U};
            netz.indexes.insert(netz.indexes.end(), idx, idx + 6);
        }
        b.numIndexes = static_cast<std::uint32_t>(netz.indexes.size()) - b.firstIndex;
        BspMesh::Run run;
        run.firstIndex = b.firstIndex;
        run.numIndexes = b.numIndexes;
        run.surface = g.flaeche;
        b.firstRun = static_cast<std::uint32_t>(netz.runs.size());
        b.numRuns = 1;
        netz.runs.push_back(run);
        netz.batches.push_back(b);
    }
    vierecke = static_cast<int>(gesamt);
}

}  // namespace

void baueGrasNetz(const Camera& cam, float reichweite) {
    g_app->grasReichweite = reichweite;
    baueGras(cam, reichweite, g_app->grasNetz, g_app->grasVierecke, g_app->grasFlaechen);
    g_app->grasPatches = g_grasPatchZahl;
}

void baueGrasNetzNeben(const Camera& cam, float reichweite) {
    int flaechen = 0;
    baueGras(cam, reichweite, g_app->grasNetzNeben, g_app->grasViereckeNeben, flaechen);
}

bool grasTestZiel(float mitte[3], bool alleArten, bool nurPatch) {
    // Die groesste Flaeche der Karte, deren Shader eine senkrechte
    // surfaceSprites-Stufe (vertical/flattened) an Platz >= 1 hat - nach dem
    // Shaderskript, nicht nach dem Geladenen (so prueft es auch einen Stand,
    // der die Stufe noch als Schicht zeichnet).
    const BspMesh& m = g_app->mesh;
    double beste = 0.0;
    bool da = false;
    for (const BspMesh::Batch& b : m.batches) {
        if (b.shader < 0 || static_cast<std::size_t>(b.shader) >= g_app->geo.shaders.size()) { continue; }
        std::string low = g_app->geo.shaders[static_cast<std::size_t>(b.shader)].name;
        for (char& c : low) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        const auto si = g_app->shaderMap.find(low);
        if (si == g_app->shaderMap.end()) { continue; }
        bool gras = false;
        for (std::size_t k = 1; k < si->second.stages.size(); ++k) {
            const int t = si->second.stages[k].ss.typ;
            gras = gras || t == 1 || t == 5 || (alleArten && t != 0);
        }
        if (!gras) { continue; }
        const std::uint32_t ende = std::min<std::uint32_t>(b.firstRun + b.numRuns, static_cast<std::uint32_t>(m.runs.size()));
        for (std::uint32_t r = b.firstRun; r < ende; ++r) {
            const BspMesh::Run& run = m.runs[r];
            if (nurPatch && (run.surface >= g_app->geo.surfaces.size() ||
                             g_app->geo.surfaces[run.surface].type != BspSurface::Type::Patch)) {
                continue;
            }
            // Das groesste Dreieck, auf dem die Engine zeichnet (alle drei
            // Normalen nach oben, :644) - seine Mitte liegt sicher auf dem Boden.
            for (std::uint32_t i = 0; i + 2 < run.numIndexes; i += 3) {
                const BspVertex& va = m.verts[m.indexes[run.firstIndex + i]];
                const BspVertex& vb = m.verts[m.indexes[run.firstIndex + i + 1]];
                const BspVertex& vc = m.verts[m.indexes[run.firstIndex + i + 2]];
                if (va.normal[2] < 0.5F || vb.normal[2] < 0.5F || vc.normal[2] < 0.5F) { continue; }
                const double u[3] = {vb.xyz[0] - va.xyz[0], vb.xyz[1] - va.xyz[1], vb.xyz[2] - va.xyz[2]};
                const double v[3] = {vc.xyz[0] - va.xyz[0], vc.xyz[1] - va.xyz[1], vc.xyz[2] - va.xyz[2]};
                const double kx = u[1] * v[2] - u[2] * v[1];
                const double ky = u[2] * v[0] - u[0] * v[2];
                const double kz = u[0] * v[1] - u[1] * v[0];
                const double A = 0.5 * std::sqrt(kx * kx + ky * ky + kz * kz);
                if (A > beste) {
                    beste = A;
                    g_grasZielShader = g_app->geo.shaders[static_cast<std::size_t>(b.shader)].name;
                    for (int k = 0; k < 3; ++k) {
                        mitte[k] = static_cast<float>((va.xyz[k] + vb.xyz[k] + vc.xyz[k]) / 3.0);
                    }
                    da = true;
                }
            }
        }
    }
    if (da) { diag::detail("Gras: Testziel auf " + g_grasZielShader); }
    return da;
}

}  // namespace bhed::gui
