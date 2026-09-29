#include "bhed/bspgeo.h"
#include "bhed/image.h"
#include "bhed/diag.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace bhed {
namespace {

// Lumpnummern. Bei RBSP wie bei IBSP an denselben Stellen.
constexpr int kLumpShaders = 1;
constexpr int kLumpDrawVerts = 10;
constexpr int kLumpDrawIndexes = 11;
constexpr int kLumpSurfaces = 13;
constexpr int kLumpLightmaps = 14;
constexpr int kLumpLightGrid = 15;
constexpr int kLumpLightArray = 17;
constexpr std::size_t kGridPointSize = 30;   // mgrid_t, tr_local.h:742

// Nachgemessen an yavin2.bsp und duel_kamino_lp.bsp: beide Lumpgroessen
// gehen restlos auf.
constexpr std::size_t kShaderSize = 72;    // name[64] + zwei Flaggenfelder
constexpr std::size_t kVertexSize = 80;    // xyz, st, 4x lightmap, normal, 4x rgba
constexpr std::size_t kSurfaceSize = 148;
constexpr std::size_t kLightmapBytes = std::size_t{128} * 128 * 3;

std::int32_t i32(const std::string& b, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) {
        v = (v << 8) | static_cast<unsigned char>(b[at + static_cast<std::size_t>(i)]);
    }
    std::int32_t out = 0;
    std::memcpy(&out, &v, 4);
    return out;
}

float f32(const std::string& b, std::size_t at) {
    const std::int32_t v = i32(b, at);
    float out = 0.0F;
    std::memcpy(&out, &v, 4);
    return out;
}

struct Lump {
    std::size_t offset = 0;
    std::size_t length = 0;
    [[nodiscard]] bool fits(std::size_t total) const {
        return offset + length <= total;
    }
};

Lump lumpAt(const std::string& b, int index) {
    Lump l;
    const std::size_t at = 8 + static_cast<std::size_t>(index) * 8;
    if (at + 8 > b.size()) {
        return l;
    }
    const std::int32_t off = i32(b, at);
    const std::int32_t len = i32(b, at + 4);
    if (off < 0 || len < 0) {
        return l;
    }
    l.offset = static_cast<std::size_t>(off);
    l.length = static_cast<std::size_t>(len);
    return l;
}

}  // namespace

namespace {

// Aus code/game/surfaceflags.h von OpenJK - ABGESCHRIEBEN, nicht geraten.
//
// Der erste Anlauf hatte hier 0x80 fuer NODRAW und dazu ein SURF_SKIP und
// SURF_HINT, die es in JKA gar nicht gibt. Dass die Ansicht trotzdem richtig
// aussah, lag allein an der Namensliste weiter unten - die Flaggenpruefung
// hat nie etwas gefunden. Ein Fehler, der sich hinter einer zweiten,
// funktionierenden Pruefung versteckt hat.
constexpr std::uint32_t kSurfSky = 0x00002000U;
constexpr std::uint32_t kSurfNoDraw = 0x00200000U;
constexpr std::uint32_t kContentsPlayerClip = 0x00000010U;
constexpr std::uint32_t kContentsTrigger = 0x00000400U;

}  // namespace

bool BspShader::isSky() const noexcept {
    return (surfaceFlags & kSurfSky) != 0 ||
           name.find("skies/") != std::string::npos;
}

bool BspShader::isDrawn() const noexcept {
    if ((surfaceFlags & kSurfNoDraw) != 0) {
        return false;
    }
    // Hilfskoerper: Spielerabgrenzung und Ausloeser. Sie haben oft kein
    // NODRAW, weil sie im Spiel ueber den Shader unsichtbar werden.
    if ((contentFlags & (kContentsPlayerClip | kContentsTrigger)) != 0) {
        return false;
    }
    // Zusaetzlich am Namen: Radiant legt Hilfsflaechen unter system/ ab, und
    // nicht alle tragen die Flaggen. Gemessen an duel_kamino_lp.bsp: dort
    // heisst die erste Flaeche "textures/system/clip" ohne NODRAW.
    // Diese Liste hat lange die kaputte Flaggenpruefung verdeckt.
    static const char* kHidden[] = {"system/clip", "system/trigger",
                                    "system/hint", "system/skip",
                                    "system/caulk", "system/nodraw",
                                    "system/physics_clip", "system/cushion",
                                    "common/clip", "common/trigger"};
    for (const char* h : kHidden) {
        if (name.find(h) != std::string::npos) {
            return false;
        }
    }
    return true;
}

bool readBspGeometry(const std::string& b, BspGeometry& out, std::string* error) {
    out = BspGeometry{};
    if (b.size() < 8 + 18 * 8) {
        if (error != nullptr) { *error = "zu kurz fuer eine .bsp"; }
        return false;
    }
    const std::string magic = b.substr(0, 4);
    if (magic != "RBSP" && magic != "IBSP" && magic != "FBSP") {
        if (error != nullptr) { *error = "unbekannte Kennung: " + magic; }
        return false;
    }

    // --- Shader ---
    const Lump sh = lumpAt(b, kLumpShaders);
    if (!sh.fits(b.size())) {
        if (error != nullptr) { *error = "Shader-Lump liegt ausserhalb der Datei"; }
        return false;
    }
    for (std::size_t i = 0; i + kShaderSize <= sh.length; i += kShaderSize) {
        BspShader s;
        const std::size_t at = sh.offset + i;
        const char* raw = b.data() + at;
        const std::size_t len = ::strnlen(raw, 64);
        s.name.assign(raw, len);
        s.surfaceFlags = static_cast<std::uint32_t>(i32(b, at + 64));
        s.contentFlags = static_cast<std::uint32_t>(i32(b, at + 68));
        out.shaders.push_back(std::move(s));
    }

    // --- Vertices ---
    const Lump dv = lumpAt(b, kLumpDrawVerts);
    if (!dv.fits(b.size())) {
        if (error != nullptr) { *error = "Vertex-Lump liegt ausserhalb der Datei"; }
        return false;
    }
    out.verts.reserve(dv.length / kVertexSize);
    bool first = true;
    for (std::size_t i = 0; i + kVertexSize <= dv.length; i += kVertexSize) {
        const std::size_t at = dv.offset + i;
        BspVertex v;
        for (int k = 0; k < 3; ++k) { v.xyz[k] = f32(b, at + static_cast<std::size_t>(k) * 4); }
        v.st[0] = f32(b, at + 12);
        v.st[1] = f32(b, at + 16);
        // Vier Lightmap-Koordinatenpaare; wir nehmen das erste.
        v.lightmap[0] = f32(b, at + 20);
        v.lightmap[1] = f32(b, at + 24);
        for (int k = 0; k < 3; ++k) {
            v.normal[k] = f32(b, at + 52 + static_cast<std::size_t>(k) * 4);
        }
        for (int k = 0; k < 4; ++k) {
            v.colour[k] = static_cast<std::uint8_t>(b[at + 64 + static_cast<std::size_t>(k)]);
        }
        for (int k = 0; k < 3; ++k) {
            if (first) {
                out.mins[k] = out.maxs[k] = v.xyz[k];
            } else {
                out.mins[k] = std::min(out.mins[k], v.xyz[k]);
                out.maxs[k] = std::max(out.maxs[k], v.xyz[k]);
            }
        }
        first = false;
        out.verts.push_back(v);
    }

    // --- Indizes ---
    const Lump di = lumpAt(b, kLumpDrawIndexes);
    if (!di.fits(b.size())) {
        if (error != nullptr) { *error = "Index-Lump liegt ausserhalb der Datei"; }
        return false;
    }
    out.indexes.reserve(di.length / 4);
    for (std::size_t i = 0; i + 4 <= di.length; i += 4) {
        const std::int32_t v = i32(b, di.offset + i);
        // Ein Index ausserhalb der Vertexliste waere ein Absturz beim
        // Zeichnen. Lieber hier abweisen als dort.
        if (v < 0 || static_cast<std::size_t>(v) >= out.verts.size()) {
            out.indexes.push_back(0);
            continue;
        }
        out.indexes.push_back(static_cast<std::uint32_t>(v));
    }

    // --- Untermodelle (Lump 7) ---
    //
    // Wie R_LoadSubmodels() in code/rd-vanilla/tr_bsp.cpp. dmodel_t ist
    // 40 Byte: mins[3], maxs[3], firstSurface, numSurfaces, firstBrush,
    // numBrushes. Modell 0 ist die Welt, alles darueber sind die
    // beweglichen Teile, auf die eine Entity mit "model" "*12" zeigt.
    constexpr int kLumpModels = 7;
    constexpr std::size_t kModelSize = 40;
    const Lump mo = lumpAt(b, kLumpModels);
    if (mo.fits(b.size())) {
        for (std::size_t i = 0; i + kModelSize <= mo.length; i += kModelSize) {
            const std::size_t at = mo.offset + i;
            BspGeometry::SubModel m;
            for (int k = 0; k < 3; ++k) {
                m.mins[k] = f32(b, at + static_cast<std::size_t>(k) * 4);
                m.maxs[k] = f32(b, at + 12 + static_cast<std::size_t>(k) * 4);
            }
            m.firstSurface = i32(b, at + 24);
            m.numSurfaces = i32(b, at + 28);
            out.models.push_back(m);
        }
    }

    // --- Sichtbarkeit: Ebenen, Knoten, Blaetter, Bittabelle ---
    //
    // Damit laesst sich zeichnen wie die Engine: nur, was vom Standpunkt
    // aus ueberhaupt zu sehen ist. Der Kompilierer hat das ausgerechnet;
    // wir schlagen nur nach.
    {
        constexpr int kLumpPlanes = 2;
        constexpr int kLumpNodes = 3;
        constexpr int kLumpLeafs = 4;
        constexpr int kLumpLeafSurfaces = 5;
        constexpr int kLumpVis = 16;

        const Lump pl = lumpAt(b, kLumpPlanes);
        if (pl.fits(b.size())) {
            for (std::size_t i = 0; i + 16 <= pl.length; i += 16) {
                const std::size_t at = pl.offset + i;
                BspGeometry::Plane p;
                for (int k = 0; k < 3; ++k) {
                    p.normal[k] = f32(b, at + static_cast<std::size_t>(k) * 4);
                }
                p.dist = f32(b, at + 12);
                out.planes.push_back(p);
            }
        }
        // dnode_t ist 36 Byte: planeNum, children[2], mins[3], maxs[3].
        const Lump nd = lumpAt(b, kLumpNodes);
        if (nd.fits(b.size())) {
            for (std::size_t i = 0; i + 36 <= nd.length; i += 36) {
                const std::size_t at = nd.offset + i;
                BspGeometry::Node n;
                n.plane = i32(b, at);
                n.children[0] = i32(b, at + 4);
                n.children[1] = i32(b, at + 8);
                out.nodes.push_back(n);
            }
        }
        // dleaf_t ist 48 Byte: cluster, area, mins[3], maxs[3],
        // firstLeafSurface, numLeafSurfaces, firstLeafBrush, numLeafBrushes.
        const Lump lf = lumpAt(b, kLumpLeafs);
        if (lf.fits(b.size())) {
            for (std::size_t i = 0; i + 48 <= lf.length; i += 48) {
                const std::size_t at = lf.offset + i;
                BspGeometry::Leaf l;
                l.cluster = i32(b, at);
                for (int k = 0; k < 3; ++k) {
                    l.mins[k] = static_cast<float>(
                        i32(b, at + 8 + static_cast<std::size_t>(k) * 4));
                    l.maxs[k] = static_cast<float>(
                        i32(b, at + 20 + static_cast<std::size_t>(k) * 4));
                }
                l.firstSurface = i32(b, at + 32);
                l.numSurfaces = i32(b, at + 36);
                out.leafs.push_back(l);
            }
        }
        const Lump ls = lumpAt(b, kLumpLeafSurfaces);
        if (ls.fits(b.size())) {
            for (std::size_t i = 0; i + 4 <= ls.length; i += 4) {
                out.leafSurfaces.push_back(i32(b, ls.offset + i));
            }
        }
        const Lump vs = lumpAt(b, kLumpVis);
        if (vs.fits(b.size()) && vs.length >= 8) {
            out.numClusters = i32(b, vs.offset);
            out.clusterBytes = i32(b, vs.offset + 4);
            const std::size_t need = static_cast<std::size_t>(out.numClusters) *
                                     static_cast<std::size_t>(out.clusterBytes);
            if (out.numClusters > 0 && out.clusterBytes > 0 &&
                need + 8 <= vs.length) {
                out.vis.assign(b.begin() + static_cast<std::ptrdiff_t>(vs.offset + 8),
                               b.begin() + static_cast<std::ptrdiff_t>(vs.offset + 8 + need));
            } else {
                out.numClusters = 0;
                out.clusterBytes = 0;
            }
        }
    }

    // --- Flaechen ---
    const Lump su = lumpAt(b, kLumpSurfaces);
    if (!su.fits(b.size())) {
        if (error != nullptr) { *error = "Flaechen-Lump liegt ausserhalb der Datei"; }
        return false;
    }
    for (std::size_t i = 0; i + kSurfaceSize <= su.length; i += kSurfaceSize) {
        const std::size_t at = su.offset + i;
        BspSurface s;
        s.shader = i32(b, at);
        const std::int32_t type = i32(b, at + 8);
        s.type = (type >= 1 && type <= 4) ? static_cast<BspSurface::Type>(type)
                                          : BspSurface::Type::Bad;
        s.firstVert = i32(b, at + 12);
        s.numVerts = i32(b, at + 16);
        s.firstIndex = i32(b, at + 20);
        s.numIndexes = i32(b, at + 24);
        s.lightmap = i32(b, at + 36);          // erste von vier
        s.patchWidth = i32(b, at + 140);
        s.patchHeight = i32(b, at + 144);

        // Alles pruefen, was spaeter als Zeiger benutzt wird.
        const auto nv = static_cast<std::int32_t>(out.verts.size());
        const auto ni = static_cast<std::int32_t>(out.indexes.size());
        const auto ns = static_cast<std::int32_t>(out.shaders.size());
        if (s.firstVert < 0 || s.numVerts < 0 || s.firstVert + s.numVerts > nv ||
            s.firstIndex < 0 || s.numIndexes < 0 || s.firstIndex + s.numIndexes > ni ||
            s.shader < 0 || s.shader >= ns) {
            continue;   // kaputte Flaeche uebergehen, nicht die ganze Karte
        }
        // Den Kasten um die Flaeche EINMAL rechnen - siehe
        // BspSurface::mins/maxs. Der Spurtest spart sich damit die
        // Dreiecke jeder Flaeche, die er ohnehin verfehlt.
        if (s.numVerts > 0) {
            for (int k = 0; k < 3; ++k) {
                s.mins[k] = 1.0e30F;
                s.maxs[k] = -1.0e30F;
            }
            for (int v = 0; v < s.numVerts; ++v) {
                // ERST erweitern, DANN addieren.
                //
                // `static_cast<std::size_t>(s.firstVert + v)` rechnet die
                // Summe in `int` und erweitert das Ergebnis - bei einer
                // Karte mit mehr als zwei Milliarden Ecken laeuft sie
                // vorher ueber. So weit ist keine, aber die Reihenfolge
                // kostet nichts und die Frage stellt sich dann nie wieder.
                const BspVertex& bv =
                    out.verts[static_cast<std::size_t>(s.firstVert) +
                              static_cast<std::size_t>(v)];
                for (int k = 0; k < 3; ++k) {
                    s.mins[k] = std::min(s.mins[k], bv.xyz[k]);
                    s.maxs[k] = std::max(s.maxs[k], bv.xyz[k]);
                }
            }
        }
        out.surfaces.push_back(s);
    }

    // --- Flaeche -> Cluster ---
    //
    // Aus den Blaettern zusammengetragen: jedes Blatt gehoert zu einem
    // Cluster und nennt seine Flaechen. Umgedreht ergibt das je Flaeche
    // die Liste ihrer Cluster.
    //
    // Doppelte werden entfernt - ein Boden kann in neunundneunzig
    // Blaettern liegen, aber viele davon teilen sich einen Cluster, und
    // jede Wiederholung waere ein Vergleich mehr bei jedem Bild.
    if (!out.leafs.empty() && !out.surfaces.empty()) {
        std::vector<std::vector<int>> proFlaeche(out.surfaces.size());
        for (const BspGeometry::Leaf& lf : out.leafs) {
            if (lf.cluster < 0) {
                continue;
            }
            for (int i = 0; i < lf.numSurfaces; ++i) {
                const auto at = static_cast<std::size_t>(lf.firstSurface) +
                                static_cast<std::size_t>(i);
                if (at >= out.leafSurfaces.size()) {
                    continue;
                }
                const int si = out.leafSurfaces[at];
                if (si < 0 || static_cast<std::size_t>(si) >= proFlaeche.size()) {
                    continue;
                }
                proFlaeche[static_cast<std::size_t>(si)].push_back(lf.cluster);
            }
        }
        out.surfaceFirst.resize(out.surfaces.size(), 0U);
        out.surfaceCount.resize(out.surfaces.size(), 0U);
        for (std::size_t i = 0; i < proFlaeche.size(); ++i) {
            std::vector<int>& v = proFlaeche[i];
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
            out.surfaceFirst[i] =
                static_cast<std::uint32_t>(out.surfaceClusters.size());
            out.surfaceCount[i] = static_cast<std::uint32_t>(v.size());
            out.surfaceClusters.insert(out.surfaceClusters.end(), v.begin(),
                                       v.end());
        }
    }

    // --- Lightmaps ---
    const Lump lm = lumpAt(b, kLumpLightmaps);
    if (lm.fits(b.size())) {
        for (std::size_t i = 0; i + kLightmapBytes <= lm.length; i += kLightmapBytes) {
            out.lightmaps.emplace_back(
                b.begin() + static_cast<std::ptrdiff_t>(lm.offset + i),
                b.begin() + static_cast<std::ptrdiff_t>(lm.offset + i + kLightmapBytes));
        }
    }

    // --- Das Lichtgitter (Lump 15 und 17) ---------------------------------
    //
    // Damit werden FIGUREN beleuchtet. Die Flaechen der Karte haben ihre
    // Lightmaps; alles, was sich bewegt, holt sein Licht aus diesem Gitter.
    //
    // Der Satz je Punkt ist mgrid_t (tr_local.h:742), 30 Byte. Geht die
    // Lumplaenge nicht restlos auf, lesen wir lieber gar nichts: ein um
    // einen Byte verschobenes Gitter faerbt die halbe Karte falsch, und das
    // waere schlechter als kein Gitter.
    const Lump lg = lumpAt(b, kLumpLightGrid);
    if (lg.fits(b.size()) && lg.length >= kGridPointSize &&
        lg.length % kGridPointSize == 0) {
        const std::size_t n = lg.length / kGridPointSize;
        out.lightGrid.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t at = lg.offset + i * kGridPointSize;
            BspGeometry::LightGridPoint& p = out.lightGrid[i];
            std::size_t k = at;
            for (int s2 = 0; s2 < 4; ++s2) {
                for (int c = 0; c < 3; ++c) {
                    p.ambient[s2][c] = static_cast<std::uint8_t>(b[k++]);
                }
            }
            for (int s2 = 0; s2 < 4; ++s2) {
                for (int c = 0; c < 3; ++c) {
                    p.direct[s2][c] = static_cast<std::uint8_t>(b[k++]);
                }
            }
            for (int s2 = 0; s2 < 4; ++s2) {
                p.styles[s2] = static_cast<std::uint8_t>(b[k++]);
            }
            p.latLong[0] = static_cast<std::uint8_t>(b[k++]);
            p.latLong[1] = static_cast<std::uint8_t>(b[k]);
        }
    }
    const Lump la = lumpAt(b, kLumpLightArray);
    if (la.fits(b.size()) && la.length % 2 == 0) {
        out.lightArray.resize(la.length / 2);
        for (std::size_t i = 0; i < out.lightArray.size(); ++i) {
            const std::size_t at = la.offset + i * 2;
            out.lightArray[i] = static_cast<std::uint16_t>(
                static_cast<unsigned char>(b[at]) |
                (static_cast<unsigned char>(b[at + 1]) << 8));
        }
    }

    // Ursprung und Ausdehnung des Gitters, wie R_LoadLightGrid sie rechnet
    // (tr_bsp.cpp:1163 ff.) - aus den Grenzen des WELTmodells, auf ein
    // Vielfaches der Gitterweite gerundet:
    //
    //     lightGridOrigin[i] = size[i] * ceil ( mins[i] / size[i] );
    //     maxs[i]            = size[i] * floor( maxs[i] / size[i] );
    //     lightGridBounds[i] = (maxs[i] - origin[i]) / size[i] + 1;
    //
    // Die Gitterweite selbst steht im worldspawn unter "gridsize" und wird
    // vom Aufrufer gesetzt; die Vorgabe 64/64/128 steht im Kopf.
    // Die Gitterweite steht im worldspawn unter "gridsize" - dieselbe
    // Stelle, aus der auch R_LoadLightGrid sie holt. Fehlt sie, bleibt die
    // Vorgabe 64/64/128 stehen.
    //
    // Dafuer wird das Entity-Lump hier ein zweites Mal ueberflogen. Es
    // vollstaendig zu zerlegen ist Aufgabe von bsp.cpp; hier genuegt der
    // eine Schluessel, und ihn durchzureichen hiesse, die Reihenfolge
    // zweier Leser aneinanderzubinden.
    {
        const Lump ent = lumpAt(b, 0);
        if (ent.fits(b.size())) {
            const std::string txt = b.substr(ent.offset, ent.length);
            const std::size_t at = txt.find("\"gridsize\"");
            if (at != std::string::npos) {
                const std::size_t a2 = txt.find('"', at + 10);
                const std::size_t e2 = (a2 == std::string::npos)
                                           ? std::string::npos
                                           : txt.find('"', a2 + 1);
                if (e2 != std::string::npos) {
                    float g[3] = {0, 0, 0};
                    if (std::sscanf(txt.substr(a2 + 1, e2 - a2 - 1).c_str(),
                                    "%f %f %f", &g[0], &g[1], &g[2]) == 3 &&
                        g[0] > 0.0F && g[1] > 0.0F && g[2] > 0.0F) {
                        for (int i = 0; i < 3; ++i) {
                            out.lightGridSize[i] = g[i];
                        }
                    }
                }
            }
        }
    }

    if (!out.models.empty()) {
        const BspGeometry::SubModel& welt = out.models[0];
        for (int i = 0; i < 3; ++i) {
            const float gs = out.lightGridSize[i];
            if (gs <= 0.0F) {
                continue;
            }
            out.lightGridOrigin[i] = gs * std::ceil(welt.mins[i] / gs);
            const float mx = gs * std::floor(welt.maxs[i] / gs);
            out.lightGridBounds[i] =
                static_cast<int>((mx - out.lightGridOrigin[i]) / gs) + 1;
        }
    }
    // Die Kartengeometrie in Zahlen. Ohne diese Zeile stand im Protokoll
    // nur, dass das Lesen 40 ms gedauert hat - nicht, was dabei herauskam.
    diag::detail("Kartengeometrie: " + std::to_string(b.size()) + " Bytes, " +
                 std::to_string(out.verts.size()) + " Ecken, " +
                 std::to_string(out.indexes.size() / 3) + " Dreiecke, " +
                 std::to_string(out.surfaces.size()) + " Flaechen, " +
                 std::to_string(out.shaders.size()) + " Shader, " +
                 std::to_string(out.lightmaps.size()) + " Lichtkarten, " +
                 std::to_string(out.models.size()) + " Untermodelle, " +
                 std::to_string(out.planes.size()) + " Ebenen, " +
                 std::to_string(out.nodes.size()) + " Knoten, " +
                 std::to_string(out.lightGrid.size()) + " Lichtgitterpunkte");
    return true;
}

namespace {

// Ein Bezier-Feld aus 3x3 Kontrollpunkten an der Stelle (u,v) auswerten.
BspVertex bezier(const BspVertex* c[9], float u, float v) {
    const float bu[3] = {(1 - u) * (1 - u), 2 * u * (1 - u), u * u};
    const float bv[3] = {(1 - v) * (1 - v), 2 * v * (1 - v), v * v};
    BspVertex out;
    float col[4] = {0, 0, 0, 0};
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            const float w = bu[i] * bv[j];
            const BspVertex& p = *c[j * 3 + i];
            for (int k = 0; k < 3; ++k) {
                out.xyz[k] += p.xyz[k] * w;
                out.normal[k] += p.normal[k] * w;
            }
            out.st[0] += p.st[0] * w;
            out.st[1] += p.st[1] * w;
            out.lightmap[0] += p.lightmap[0] * w;
            out.lightmap[1] += p.lightmap[1] * w;
            for (int k = 0; k < 4; ++k) {
                col[k] += static_cast<float>(p.colour[k]) * w;
            }
        }
    }
    for (int k = 0; k < 4; ++k) {
        out.colour[k] = static_cast<std::uint8_t>(std::clamp(col[k], 0.0F, 255.0F));
    }
    return out;
}

}  // namespace

// Gemeinsamer Kern von buildMesh und buildModelMesh.
//
// first/count schneiden aus geo.surfaces heraus, was gezeichnet werden
// soll. Ein Untermodell ist genau so ein Ausschnitt - deshalb braucht es
// keine zweite Fassung derselben Rechnung.
BspMesh buildRange(const BspGeometry& geo, int level, bool skipSky,
                   std::size_t first, std::size_t count);

bool BspGeometry::surfaceVisible(std::size_t surface, int fromCluster) const {
    // Ohne Tabelle ist alles sichtbar - so haelt es auch R_ClusterPVS,
    // das dann novis mit lauter Einsen zurueckgibt.
    if (vis.empty() || surfaceFirst.size() <= surface) {
        return true;
    }
    const std::uint32_t first = surfaceFirst[surface];
    const std::uint32_t n = surfaceCount[surface];
    if (n == 0U) {
        // In keinem Blatt - lieber zeichnen als verschwinden lassen.
        return true;
    }
    for (std::uint32_t k = 0; k < n; ++k) {
        const std::size_t at = static_cast<std::size_t>(first) + k;
        if (at < surfaceClusters.size() &&
            clusterVisible(fromCluster, surfaceClusters[at])) {
            return true;
        }
    }
    return false;
}

int BspGeometry::leafAt(const float p[3]) const {
    if (nodes.empty() || leafs.empty() || planes.empty()) {
        return -1;
    }
    // Wie R_PointInLeaf: am Baum entlang, bei jedem Knoten die Seite der
    // Ebene waehlen. Negative Kindnummern sind Blaetter: -(blatt + 1).
    int at = 0;
    for (int guard = 0; guard < 1024; ++guard) {
        if (at < 0) {
            const int leaf = -(at + 1);
            return (leaf >= 0 && static_cast<std::size_t>(leaf) < leafs.size())
                       ? leaf
                       : -1;
        }
        if (static_cast<std::size_t>(at) >= nodes.size()) {
            return -1;
        }
        const Node& n = nodes[static_cast<std::size_t>(at)];
        if (n.plane < 0 || static_cast<std::size_t>(n.plane) >= planes.size()) {
            return -1;
        }
        const Plane& pl = planes[static_cast<std::size_t>(n.plane)];
        const float d = p[0] * pl.normal[0] + p[1] * pl.normal[1] +
                        p[2] * pl.normal[2] - pl.dist;
        at = n.children[d >= 0.0F ? 0 : 1];
    }
    return -1;
}

bool BspGeometry::clusterVisible(int from, int to) const {
    // Ohne Tabelle ist alles sichtbar - so macht es auch R_ClusterPVS, das
    // dann novis mit lauter Einsen zurueckgibt.
    if (vis.empty() || clusterBytes <= 0) {
        return true;
    }
    if (from < 0 || from >= numClusters || to < 0 || to >= numClusters) {
        return true;   // ausserhalb: lieber zeichnen als weglassen
    }
    const std::size_t at = static_cast<std::size_t>(from) *
                               static_cast<std::size_t>(clusterBytes) +
                           static_cast<std::size_t>(to) / 8U;
    if (at >= vis.size()) {
        return true;
    }
    return (vis[at] & (1U << (static_cast<unsigned>(to) & 7U))) != 0U;
}

BspMesh buildModelMesh(const BspGeometry& geo, int index, int level,
                       bool skipSky) {
    if (index < 0 || static_cast<std::size_t>(index) >= geo.models.size()) {
        return {};
    }
    const BspGeometry::SubModel& m = geo.models[static_cast<std::size_t>(index)];
    if (m.firstSurface < 0 || m.numSurfaces <= 0) {
        return {};
    }
    return buildRange(geo, level, skipSky,
                      static_cast<std::size_t>(m.firstSurface),
                      static_cast<std::size_t>(m.numSurfaces));
}

BspMesh buildMesh(const BspGeometry& geo, int level, bool skipSky) {
    return buildRange(geo, level, skipSky, 0, geo.surfaces.size());
}

int ladeExterneLightmaps(BspGeometry& geo, const std::string& kartenOrdner,
                         const std::function<bool(const std::string&, std::string&)>& lies) {
    int hoechste = -1;
    for (const BspSurface& s : geo.surfaces) {
        hoechste = std::max(hoechste, s.lightmap);
    }
    int geladen = 0;
    // Die aus der .bsp sind 128x128 - ihre Masse eintragen, damit die
    // Listen gleich lang sind.
    geo.lightmapBreite.resize(geo.lightmaps.size(), BspGeometry::kLightmapSize);
    geo.lightmapHoehe.resize(geo.lightmaps.size(), BspGeometry::kLightmapSize);
    for (int i = 0; i <= hoechste && lies; ++i) {
        const auto ui = static_cast<std::size_t>(i);
        if (ui < geo.lightmaps.size() && !geo.lightmaps[ui].empty()) {
            continue;
        }
        char name[64];
        std::snprintf(name, sizeof(name), "/lm_%04d", i);
        for (const char* endung : {".tga", ".jpg", ".png"}) {
            std::string daten;
            if (!lies(kartenOrdner + name + endung, daten)) {
                continue;
            }
            const image::Image im = image::decode(
                reinterpret_cast<const unsigned char*>(daten.data()), daten.size());
            if (!im.ok || im.width <= 0 || im.height <= 0) {
                continue;
            }
            if (geo.lightmaps.size() <= ui) {
                geo.lightmaps.resize(ui + 1U);
                geo.lightmapBreite.resize(ui + 1U, 0);
                geo.lightmapHoehe.resize(ui + 1U, 0);
            }
            std::vector<std::uint8_t> rgb(static_cast<std::size_t>(im.width) *
                                          static_cast<std::size_t>(im.height) * 3U);
            for (std::size_t p = 0; p < static_cast<std::size_t>(im.width) *
                                            static_cast<std::size_t>(im.height); ++p) {
                rgb[p * 3U + 0] = im.rgba[p * 4U + 0];
                rgb[p * 3U + 1] = im.rgba[p * 4U + 1];
                rgb[p * 3U + 2] = im.rgba[p * 4U + 2];
            }
            geo.lightmaps[ui] = std::move(rgb);
            geo.lightmapBreite[ui] = im.width;
            geo.lightmapHoehe[ui] = im.height;
            ++geladen;
            break;
        }
    }
    // Was es weder in der .bsp noch daneben gibt, wird nach Ecken
    // beleuchtet - wie R_FindLightmap (return lightmapsVertex).
    for (BspSurface& s : geo.surfaces) {
        if (s.lightmap >= 0 &&
            (static_cast<std::size_t>(s.lightmap) >= geo.lightmaps.size() ||
             geo.lightmaps[static_cast<std::size_t>(s.lightmap)].empty())) {
            s.lightmap = -3;
        }
    }
    return geladen;
}

BspMesh buildRange(const BspGeometry& geo, int level, bool skipSky,
                   std::size_t first, std::size_t count) {
    BspMesh mesh;
    level = std::clamp(level, 1, 16);

    // Nach Lightmap sortiert zusammenfassen: so entsteht je Lightmap ein
    // Zeichenaufruf statt einer je Flaeche. Bei yavin2 sind das 40 statt
    // 12900.
    std::map<std::pair<int, int>, std::vector<std::uint32_t>> byBatch;
    // Je Zeichenaufruf: welche Flaeche hat wie viele Indizes beigesteuert.
    // Daraus entstehen die Abschnitte, die sich einzeln keulen lassen.
    struct RunNote {
        std::uint32_t surface;
        std::uint32_t count;
    };
    std::map<std::pair<int, int>, std::vector<RunNote>> runsByBatch;

    const std::size_t last = std::min(first + count, geo.surfaces.size());
    for (std::size_t si = first; si < last; ++si) {
        const BspSurface& s = geo.surfaces[si];
        if (s.shader >= 0 && static_cast<std::size_t>(s.shader) < geo.shaders.size()) {
            const BspShader& sh = geo.shaders[static_cast<std::size_t>(s.shader)];
            if (!sh.isDrawn()) {
                continue;
            }
            // Himmelsflaechen weglassen. Sie umschliessen die Karte und
            // verdecken beim Blick von aussen alles darin - in yavin2 sind
            // das die grossen grauen Waende, die man in der Uebersicht
            // sieht.
            if (skipSky && sh.isSky()) {
                continue;
            }
        }
        // Nach Lightmap UND Shader gruppieren. Vorher stand hier eine 0 -
        // solange es keine Texturen gab, war der Shader egal. Jetzt
        // bestimmt er, welches Bild aufgetragen wird.
        const auto key = std::make_pair(s.lightmap, s.shader);
        std::vector<std::uint32_t>& out = byBatch[key];
        // Merken, wie viele Indizes DIESE Flaeche beisteuert - daraus wird
        // unten ihr Abschnitt.
        // Als Lambda, weil die Schleife ZWEI Ausgaenge hat: ebene Flaechen
        // und Dreiecksnetze verlassen sie mit continue, nur Patches laufen
        // bis zum Ende durch. Meine erste Fassung stand nur am Ende - und
        // zeichnete deshalb 418 Abschnitte auf statt zwoelftausend. Die
        // Probe "decken die Abschnitte den Aufruf lueckenlos ab" hat es
        // sofort gezeigt: 146016 von 303744 Indizes erfasst.
        const std::size_t vorher = out.size();
        auto merkeAbschnitt = [&] {
            if (out.size() > vorher) {
                runsByBatch[key].push_back(
                    {static_cast<std::uint32_t>(si),
                     static_cast<std::uint32_t>(out.size() - vorher)});
            }
        };

        if (s.type == BspSurface::Type::Planar ||
            s.type == BspSurface::Type::TriangleSoup) {
            const auto base = static_cast<std::uint32_t>(mesh.verts.size());
            // ERST erweitern, DANN addieren.
            //
            // static_cast<std::size_t>(s.firstVert + i) rechnet die Summe in
            // int aus und erweitert erst danach - ein Ueberlauf waere schon
            // passiert, bevor der grosse Typ ins Spiel kommt. Bei heutigen
            // Karten reicht int aus, aber genau dieses Muster hat im
            // Rasterisierer schon einmal zugeschlagen (rc115), und es kostet
            // nichts, es richtig zu schreiben.
            const auto vertBase = static_cast<std::size_t>(s.firstVert);
            for (int i = 0; i < s.numVerts; ++i) {
                mesh.verts.push_back(
                    geo.verts[vertBase + static_cast<std::size_t>(i)]);
            }
            const auto idxBase = static_cast<std::size_t>(s.firstIndex);
            for (int i = 0; i < s.numIndexes; ++i) {
                out.push_back(base +
                              geo.indexes[idxBase + static_cast<std::size_t>(i)]);
            }
            merkeAbschnitt();
            continue;
        }

        if (s.type != BspSurface::Type::Patch || s.patchWidth < 3 || s.patchHeight < 3) {
            merkeAbschnitt();
            continue;   // Flares und Unfug ueberspringen
        }

        // Ein Patch ist ein Gitter aus 3x3-Feldern, die sich Randpunkte
        // teilen: Breite und Hoehe sind deshalb immer ungerade.
        const int pw = s.patchWidth;
        const int ph = s.patchHeight;
        for (int py = 0; py + 2 < ph; py += 2) {
            for (int px = 0; px + 2 < pw; px += 2) {
                const BspVertex* c[9];
                for (int j = 0; j < 3; ++j) {
                    for (int i = 0; i < 3; ++i) {
                        const int idx = s.firstVert + (py + j) * pw + (px + i);
                        c[j * 3 + i] = &geo.verts[static_cast<std::size_t>(idx)];
                    }
                }
                const auto base = static_cast<std::uint32_t>(mesh.verts.size());
                for (int j = 0; j <= level; ++j) {
                    for (int i = 0; i <= level; ++i) {
                        mesh.verts.push_back(
                            bezier(c, static_cast<float>(i) / static_cast<float>(level),
                                   static_cast<float>(j) / static_cast<float>(level)));
                    }
                }
                const auto row = static_cast<std::uint32_t>(level + 1);
                for (int j = 0; j < level; ++j) {
                    for (int i = 0; i < level; ++i) {
                        const std::uint32_t a =
                            base + static_cast<std::uint32_t>(j) * row +
                            static_cast<std::uint32_t>(i);
                        out.push_back(a);
                        out.push_back(a + row);
                        out.push_back(a + 1);
                        out.push_back(a + 1);
                        out.push_back(a + row);
                        out.push_back(a + row + 1);
                    }
                }
            }
        }
        merkeAbschnitt();
    }

    for (const auto& [key, idx] : byBatch) {
        if (idx.empty()) {
            continue;
        }
        BspMesh::Batch batch;
        batch.lightmap = key.first;
        batch.shader = key.second;
        batch.firstIndex = static_cast<std::uint32_t>(mesh.indexes.size());
        batch.numIndexes = static_cast<std::uint32_t>(idx.size());
        batch.firstRun = static_cast<std::uint32_t>(mesh.runs.size());
        std::uint32_t offset = batch.firstIndex;
        const auto note = runsByBatch.find(key);
        if (note != runsByBatch.end()) {
            for (const RunNote& rn : note->second) {
                BspMesh::Run run;
                run.firstIndex = offset;
                run.numIndexes = rn.count;
                run.surface = rn.surface;
                mesh.runs.push_back(run);
                offset += rn.count;
            }
        }
        batch.numRuns = static_cast<std::uint32_t>(mesh.runs.size()) -
                        batch.firstRun;
        mesh.indexes.insert(mesh.indexes.end(), idx.begin(), idx.end());
        mesh.batches.push_back(batch);
    }
    // --- Unbrauchbare Zahlen ausraeumen ----------------------------------
    //
    // Gefunden mit tests/meshmasse.cpp (27.09.): in 109 von 274 Karten -
    // Ravens eigenen wie t1_danger und yavin2 eingeschlossen - stehen in den
    // Lightmap-Koordinaten der Flaechen mit Lightmap -3 (LIGHTMAP_BY_VERTEX)
    // NaN-Werte. Der Kompilierer laesst sie offen, weil sie dort niemand
    // liest; die Engine benutzt sie nicht. Sie kamen aber bis in den
    // Eckenpuffer der Grafikkarte, und ein NaN, das ein Shader einmal
    // mitrechnet, faerbt den Bildpunkt schwarz. Deshalb hier auf null - und
    // dasselbe fuer jede andere nicht endliche Zahl, die ohnehin kein
    // sinnvolles Bild ergaebe.
    for (BspVertex& v : mesh.verts) {
        for (float& f : v.lightmap) { if (!std::isfinite(f)) { f = 0.0F; } }
        for (float& f : v.st) { if (!std::isfinite(f)) { f = 0.0F; } }
        for (float& f : v.normal) { if (!std::isfinite(f)) { f = 0.0F; } }
    }
    return mesh;
}


GridLight sampleLightGrid(const BspGeometry& geo, const float origin[3]) {
    GridLight out;
    if (geo.lightGrid.empty() || geo.lightArray.empty()) {
        return out;
    }
    for (int i = 0; i < 3; ++i) {
        if (geo.lightGridBounds[i] < 2 || geo.lightGridSize[i] <= 0.0F) {
            return out;
        }
    }

    int pos[3] = {};
    float frac[3] = {};
    for (int i = 0; i < 3; ++i) {
        const float v =
            (origin[i] - geo.lightGridOrigin[i]) / geo.lightGridSize[i];
        pos[i] = static_cast<int>(std::floor(v));
        frac[i] = v - static_cast<float>(pos[i]);
        // Die Engine KLEMMT hier, sie verwirft nicht: eine Figur ausserhalb
        // des Gitters bekommt das Licht des Randes. Das ist besser als
        // Schwarz, und es ist, was das Spiel zeigt.
        pos[i] = std::clamp(pos[i], 0, geo.lightGridBounds[i] - 1);
    }

    const int schritt[3] = {1, geo.lightGridBounds[0],
                            geo.lightGridBounds[0] * geo.lightGridBounds[1]};
    const long long start = static_cast<long long>(pos[0]) * schritt[0] +
                            static_cast<long long>(pos[1]) * schritt[1] +
                            static_cast<long long>(pos[2]) * schritt[2];

    float summe = 0.0F;
    float richtung[3] = {0, 0, 0};
    for (int i = 0; i < 8; ++i) {
        float anteil = 1.0F;
        long long at = start;
        for (int j = 0; j < 3; ++j) {
            if ((i & (1 << j)) != 0) {
                anteil *= frac[j];
                at += schritt[j];
            } else {
                anteil *= (1.0F - frac[j]);
            }
        }
        if (at < 0 ||
            static_cast<std::size_t>(at) >= geo.lightArray.size()) {
            continue;
        }
        const std::size_t idx = geo.lightArray[static_cast<std::size_t>(at)];
        if (idx >= geo.lightGrid.size()) {
            continue;
        }
        const BspGeometry::LightGridPoint& p = geo.lightGrid[idx];
        // 0xff ist LS_NONE (qfiles.h:344) - ein Punkt, der in einer Wand
        // steckt. Ihn mitzumitteln zoege jede Figur an einer Wand ins
        // Dunkle.
        if (p.styles[0] == 0xFF) {
            continue;
        }
        summe += anteil;
        for (int st = 0; st < 4; ++st) {
            if (p.styles[st] == 0xFF) {
                break;
            }
            for (int c = 0; c < 3; ++c) {
                out.ambient[c] += anteil * static_cast<float>(p.ambient[st][c]);
                out.directed[c] += anteil * static_cast<float>(p.direct[st][c]);
            }
        }
        // latLong: zwei Bytes, die den ganzen Kreis in 256 Schritte teilen.
        constexpr float kZuRad = 2.0F * 3.14159265358979F / 256.0F;
        const float lat = static_cast<float>(p.latLong[1]) * kZuRad;
        const float lng = static_cast<float>(p.latLong[0]) * kZuRad;
        const float n[3] = {std::cos(lat) * std::sin(lng),
                            std::sin(lat) * std::sin(lng), std::cos(lng)};
        for (int c = 0; c < 3; ++c) {
            richtung[c] += anteil * n[c];
        }
    }

    if (summe <= 0.0F) {
        return out;   // alle acht Punkte steckten in Waenden
    }
    const float inv = 1.0F / summe;
    for (int c = 0; c < 3; ++c) {
        out.ambient[c] *= inv;
        out.directed[c] *= inv;
    }
    const float len = std::sqrt(richtung[0] * richtung[0] +
                                richtung[1] * richtung[1] +
                                richtung[2] * richtung[2]);
    if (len > 1e-6F) {
        for (int c = 0; c < 3; ++c) {
            out.dir[c] = richtung[c] / len;
        }
    }
    // "give everything a minimum light add" - tr_light.cpp:424. Ohne diesen
    // Zuschlag steht eine Figur im Schatten voellig schwarz da.
    for (int c = 0; c < 3; ++c) {
        out.ambient[c] += 32.0F;
    }
    out.ok = true;
    return out;
}


namespace {
// Ein Strahl gegen EIN Dreieck: Moeller-Trumbore.
//
// Gibt den Anteil entlang der Strecke zurueck, oder -1 bei keinem Treffer.
// Rueckseiten werden MITGENOMMEN: eine Wand, die man von hinten trifft,
// soll den Schuss trotzdem stoppen - sonst fliegt ein Geschoss durch eine
// falsch herum gebaute Flaeche ins Freie.
float rayTriangle(const float o[3], const float d[3], const float a[3],
                  const float b[3], const float c[3]) {
    constexpr float kEps = 1e-6F;
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float p[3] = {d[1] * e2[2] - d[2] * e2[1],
                        d[2] * e2[0] - d[0] * e2[2],
                        d[0] * e2[1] - d[1] * e2[0]};
    const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (det > -kEps && det < kEps) {
        return -1.0F;   // parallel
    }
    const float inv = 1.0F / det;
    const float t[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
    const float u = (t[0] * p[0] + t[1] * p[1] + t[2] * p[2]) * inv;
    if (u < 0.0F || u > 1.0F) {
        return -1.0F;
    }
    const float q[3] = {t[1] * e1[2] - t[2] * e1[1],
                        t[2] * e1[0] - t[0] * e1[2],
                        t[0] * e1[1] - t[1] * e1[0]};
    const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (v < 0.0F || u + v > 1.0F) {
        return -1.0F;
    }
    const float w = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return (w > kEps) ? w : -1.0F;
}
}  // namespace

namespace {
// Eine Flaeche, genau wie die Vorlage sie prueft: Kasten, Typ, Shader,
// dann jedes Dreieck in seiner Reihenfolge. Herausgezogen, damit
// traceRayEinfach und traceRay (fuer kleine Flaechen) DENSELBEN Text
// ausfuehren - zwei Kopien liefen beim naechsten Eingriff auseinander.
void flaecheEinfach(const BspGeometry& geo, int sf, const float start[3],
                    const float end[3], const float d[3],
                    float& besteFraktion, TraceTreffer& out) {
    const BspSurface& s = geo.surfaces[static_cast<std::size_t>(sf)];
    // --- Trifft der Strahl den Kasten der Flaeche ueberhaupt? ---
    //
    // Ohne diese Frage lief der Test durch JEDES Dreieck JEDER
    // Flaeche im Blatt. Eine Lavaflaeche hat tausende, und ein
    // Strahl von zwanzig Einheiten Laenge trifft davon fast nie
    // eines.
    //
    // Der Test aendert das ERGEBNIS nicht: was der Kasten nicht
    // enthaelt, kann kein Dreieck der Flaeche treffen. Deshalb ist
    // er keine Naeherung, sondern nur eine Abkuerzung - und der
    // Bildvergleich muss BYTEGLEICH bleiben.
    {
        bool daneben = false;
        for (int k = 0; k < 3 && !daneben; ++k) {
            const float lo = std::min(start[k], end[k]);
            const float hi = std::max(start[k], end[k]);
            if (hi < s.mins[k] || lo > s.maxs[k]) {
                daneben = true;
            }
        }
        if (daneben) {
            return;
        }
    }
    // Flares sind Lichtpunkte ohne Koerper.
    if (s.type != BspSurface::Type::Planar &&
        s.type != BspSurface::Type::TriangleSoup &&
        s.type != BspSurface::Type::Patch) {
        return;
    }
    if (s.shader >= 0 &&
        s.shader < static_cast<int>(geo.shaders.size()) &&
        !geo.shaders[static_cast<std::size_t>(s.shader)].isDrawn()) {
        return;   // Clip, Trigger, Caulk: kein Einschlag
    }
    // --- Patches -------------------------------------------
    //
    // In md_ga_jedi sind **1246 der 2269 Flaechen Patches** -
    // gekruemmte Flaechen, darunter der ganze Arenaboden. Wer sie
    // ueberspringt, trifft nach unten nichts. Genau das ist mir
    // beim ersten Messen passiert.
    //
    // Unterteilt werden sie erst beim Netzbau (buildMesh). Hier
    // wird stattdessen das KONTROLLGITTER genommen: je Feld zwei
    // Dreiecke durch die Stuetzpunkte.
    //
    // Das ist eine NAEHERUNG - die echte Flaeche woelbt sich
    // zwischen den Stuetzpunkten. Fuer einen Einschlagsort reicht
    // das: es geht um ein paar Einheiten auf einer Wand, nicht um
    // Spielmechanik. Das steht hier, damit es niemand spaeter fuer
    // exakt haelt.
    if (s.type == BspSurface::Type::Patch) {
        const int pw = s.patchWidth;
        const int ph = s.patchHeight;
        if (pw < 2 || ph < 2) {
            return;
        }
        for (int py = 0; py + 1 < ph; ++py) {
            for (int px = 0; px + 1 < pw; ++px) {
                const auto at = [&](int x, int y) {
                    return static_cast<std::size_t>(s.firstVert) +
                           static_cast<std::size_t>(y) *
                               static_cast<std::size_t>(pw) +
                           static_cast<std::size_t>(x);
                };
                const std::size_t q[4] = {at(px, py), at(px + 1, py),
                                          at(px, py + 1),
                                          at(px + 1, py + 1)};
                if (q[3] >= geo.verts.size()) {
                    continue;
                }
                const std::size_t tri[2][3] = {{q[0], q[1], q[2]},
                                               {q[1], q[3], q[2]}};
                for (const auto& t3 : tri) {
                    const float f = rayTriangle(
                        start, d, geo.verts[t3[0]].xyz,
                        geo.verts[t3[1]].xyz, geo.verts[t3[2]].xyz);
                    if (f >= 0.0F && f < besteFraktion) {
                        besteFraktion = f;
                        out.hit = true;
                        out.fraction = f;
                        out.surface = sf;
                        for (int w2 = 0; w2 < 3; ++w2) {
                            out.point[w2] = start[w2] + d[w2] * f;
                            out.normal[w2] = geo.verts[t3[0]].normal[w2];
                        }
                    }
                }
            }
        }
        return;
    }

    for (int k = 0; k + 2 < s.numIndexes; k += 3) {
        const std::size_t i0 =
            static_cast<std::size_t>(s.firstVert) +
            static_cast<std::size_t>(
                geo.indexes[static_cast<std::size_t>(s.firstIndex) +
                            static_cast<std::size_t>(k)]);
        const std::size_t i1 =
            static_cast<std::size_t>(s.firstVert) +
            static_cast<std::size_t>(
                geo.indexes[static_cast<std::size_t>(s.firstIndex) +
                            static_cast<std::size_t>(k) + 1U]);
        const std::size_t i2 =
            static_cast<std::size_t>(s.firstVert) +
            static_cast<std::size_t>(
                geo.indexes[static_cast<std::size_t>(s.firstIndex) +
                            static_cast<std::size_t>(k) + 2U]);
        if (i0 >= geo.verts.size() || i1 >= geo.verts.size() ||
            i2 >= geo.verts.size()) {
            continue;
        }
        const float f = rayTriangle(start, d, geo.verts[i0].xyz,
                                    geo.verts[i1].xyz,
                                    geo.verts[i2].xyz);
        if (f >= 0.0F && f < besteFraktion) {
            besteFraktion = f;
            out.hit = true;
            out.fraction = f;
            out.surface = sf;
            for (int q = 0; q < 3; ++q) {
                out.point[q] = start[q] + d[q] * f;
            }
            // Die Normale aus dem Dreieck, nicht aus dem Vertex:
            // Vertexnormalen sind geglaettet und zeigen an einer
            // Kante in eine Richtung, die es als Flaeche nicht gibt.
            const float e1[3] = {geo.verts[i1].xyz[0] - geo.verts[i0].xyz[0],
                                 geo.verts[i1].xyz[1] - geo.verts[i0].xyz[1],
                                 geo.verts[i1].xyz[2] - geo.verts[i0].xyz[2]};
            const float e2[3] = {geo.verts[i2].xyz[0] - geo.verts[i0].xyz[0],
                                 geo.verts[i2].xyz[1] - geo.verts[i0].xyz[1],
                                 geo.verts[i2].xyz[2] - geo.verts[i0].xyz[2]};
            float n[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                          e1[2] * e2[0] - e1[0] * e2[2],
                          e1[0] * e2[1] - e1[1] * e2[0]};
            const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] +
                                       n[2] * n[2]);
            if (nl > 1e-6F) {
                // --- DEM STRAHL ENTGEGEN drehen ---------------
                //
                // Die Normale aus dem Umlauf zeigt mal so, mal so.
                // Die Engine liefert immer die Ebene der
                // EINTRITTSSEITE (cm_trace.cpp:136), und
                // Brush-Ebenen zeigen nach aussen - ihre Normale
                // weist also stets dem Strahl entgegen.
                //
                // Das ist kein Schoenheitsfehler: rc320 spiegelt
                // damit die Geschwindigkeit beim Aufprall
                // (`vel -= 2 * dot * normale`). Zeigt sie falsch
                // herum, wird der Partikel IN die Wand gespiegelt
                // statt von ihr weg.
                //
                // Gefunden ueber duel_mus_both aus Episode 3: dort
                // fand ein Raster von 25 Straehlen nach unten
                // KEINEN einzigen Boden, obwohl es ueberall
                // auftraf. Bei md_ga_jedi hatte es zufaellig
                // gestimmt.
                const float dl = n[0] * (end[0] - start[0]) +
                                 n[1] * (end[1] - start[1]) +
                                 n[2] * (end[2] - start[2]);
                const float vz = (dl > 0.0F) ? -1.0F : 1.0F;
                for (int q = 0; q < 3; ++q) {
                    out.normal[q] = vz * n[q] / nl;
                }
            }
        }
    }
}

}  // namespace

TraceTreffer traceRayEinfach(const BspGeometry& geo, const float start[3],
                             const float end[3]) {
    TraceTreffer out;
    const float d[3] = {end[0] - start[0], end[1] - start[1],
                        end[2] - start[2]};
    const float laenge = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (laenge < 1e-4F) {
        return out;
    }

    // Welche Blaetter beruehrt die Strecke? Ueber den Baum, mit dem
    // Huellkasten des Blattes als letzter Pruefung.
    //
    // Rekursion ohne Rekursion: ein eigener Stapel. Bei einer tiefen Karte
    // kaeme man sonst der Aufrufkette bedenklich nahe, und ein Absturz im
    // Vorschauwerkzeug ist schlimmer als ein fehlender Einschlag.
    std::vector<int> stapel;
    stapel.push_back(0);
    float besteFraktion = 1.0F;

    auto pruefeBlatt = [&](const BspGeometry::Leaf& blatt) {
        for (int i = 0; i < blatt.numSurfaces; ++i) {
            const int si = blatt.firstSurface + i;
            if (si < 0 || si >= static_cast<int>(geo.leafSurfaces.size())) {
                continue;
            }
            const int sf = geo.leafSurfaces[static_cast<std::size_t>(si)];
            if (sf < 0 || sf >= static_cast<int>(geo.surfaces.size())) {
                continue;
            }
            flaecheEinfach(geo, sf, start, end, d, besteFraktion, out);
        }
    };

    while (!stapel.empty()) {
        const int knoten = stapel.back();
        stapel.pop_back();
        if (knoten < 0) {
            const auto bi = static_cast<std::size_t>(-(knoten + 1));
            if (bi < geo.leafs.size()) {
                pruefeBlatt(geo.leafs[bi]);
            }
            continue;
        }
        if (static_cast<std::size_t>(knoten) >= geo.nodes.size()) {
            continue;
        }
        const BspGeometry::Node& n = geo.nodes[static_cast<std::size_t>(knoten)];
        if (n.plane < 0 || static_cast<std::size_t>(n.plane) >= geo.planes.size()) {
            continue;
        }
        const BspGeometry::Plane& pl = geo.planes[static_cast<std::size_t>(n.plane)];
        const float d0 = start[0] * pl.normal[0] + start[1] * pl.normal[1] +
                         start[2] * pl.normal[2] - pl.dist;
        const float d1 = end[0] * pl.normal[0] + end[1] * pl.normal[1] +
                         end[2] * pl.normal[2] - pl.dist;
        // Liegen BEIDE Enden auf derselben Seite, ist die andere Haelfte
        // unerreichbar. Sonst muessen beide durchsucht werden.
        if (d0 >= 0.0F && d1 >= 0.0F) {
            stapel.push_back(n.children[0]);
        } else if (d0 < 0.0F && d1 < 0.0F) {
            stapel.push_back(n.children[1]);
        } else {
            stapel.push_back(n.children[0]);
            stapel.push_back(n.children[1]);
        }
    }
    return out;
}


// ======================================================================
// SpurHilfe - traceRay schneller, Ergebnis BITGLEICH
// ======================================================================
//
// Gemessen an md_ta_sith, intro_sith (bluesparks an der Luftschleuse, vier
// Runner, je Durchgang 70 Teilchen mit usePhysics): 400000 Spurtests je
// Bild, 2,2 Mikrosekunden je Test, zusammen 850 ms im Block "Effekte".
// Je Test: 10 Knoten, 1 Blatt, 50 Flaechenkaesten - und 300 Dreiecke. Die
// kommen fast alle aus EINER Flaeche: models/map_objects/tantive/airlock,
// eine Dreieckssuppe aus 720 Dreiecken, deren Kasten den ganzen Gang
// umschliesst. Jeder Funkenschritt von einer Einheit Laenge schnitt damit
// hunderte Dreiecke, die zwanzig Einheiten entfernt liegen.
//
// Nachher an derselben Stelle: 0,3 Mikrosekunden je Test (33 Baumknoten,
// 6 Dreiecke geprueft, 0,03 bis rayTriangle durchgelassen) - ein Siebtel.
// Die Hilfe fuer die ganze Karte (30871 Flaechen) ist in 40 ms gebaut,
// beim ersten Spurtest.
//
// Was sich aendert und warum das Ergebnis dasselbe bleibt:
//
//   1. Der Stapel liegt auf dem Stapel (kein std::vector je Aufruf).
//   2. Liegt eine Flaeche in mehreren Blaettern, wird sie nur beim ERSTEN
//      Blatt geprueft. Beim zweiten Mal ergaebe jedes Dreieck denselben
//      Anteil f wie beim ersten, und der wird nur uebernommen, wenn er
//      ECHT kleiner ist als der beste bisher - der ist aber schon hoechstens
//      so gross. Es kann sich also nichts aendern.
//   3. Ob eine Flaeche ueberhaupt zaehlt (Typ, Shader), steht vorab fest -
//      isDrawn() vergleicht zehn Zeichenketten, je Test. Je Blatt liegen
//      die zaehlenden Flaechen mit ihrem Kasten dicht hintereinander, in
//      der Reihenfolge von leafSurfaces.
//   4. Grosse Flaechen (ab kMinBaum Dreiecken) bekommen je Normalenfach
//      einen Baum ueber ihre Dreiecke. Ein Dreieck wird nur weggelassen,
//      wenn der Strahltest der Vorlage (rayTriangle, IN FLOAT gerechnet) es
//      BEWEISBAR verwirft; die uebrigen laufen durch dasselbe rayTriangle,
//      in derselben Reihenfolge, mit derselben Uebernahme - gleich weite
//      Treffer entscheiden sich also wie vorher.
//
// Der Beweis zu 4 - zwei Wege, ein Dreieck wegzulassen. Bezeichnungen wie
// in rayTriangle: e1 = b-a, e2 = c-a (in float, so wie rayTriangle sie
// bildet - Normale, Winkel und Kasten unten gehoeren zu GENAU diesem
// Dreieck), T = o-a, N = e1 x e2, s = |d^.n^|, eps = 2^-24.
//
//   Ebene:  rayTriangle rechnet w = (e2.Q)/det mit e2.Q = N.T = |N| h0
//           und det = |N| (h0 - h1), h0/h1 die Abstaende von Anfang und
//           Ende zur Ebene. Die Rundungsfehler von Zaehler und Nenner sind
//           hoechstens C eps |T||e1||e2| und C eps |d||e1||e2| (C = 32
//           ist grosszuegig; nachgezaehlt sind es unter 20). Liegen beide
//           Enden um mehr als m = C eps (|T|+|d|) / sin(phi) auf DERSELBEN
//           Seite, faellt w in float sicher aus (0, 1) - nachgerechnet fuer
//           beide Vorzeichen von det. Geprueft wird mit 2m plus dem Fehler
//           der eigenen Rechnung (in double). Braucht keine Annahme ueber
//           die Richtung.
//   Abstand: Mit eta = C eps / (s sin(phi)) <= 1e-2 sind die relativen
//           Fehler von det, u, v, w durch eta beschraenkt. Ein Treffer in
//           float hiesse dann: Strecke und Dreieck liegen naeher als
//           eta (3|T| + 4 Kante + 2|d|) beieinander. Liegen ihre Kaesten
//           weiter auseinander (hier mit Faktor 2, |T| nach oben
//           abgeschaetzt und dem Rundungsfehler der Kastenrechnung), gibt
//           es in float keinen Treffer. Fuer einen ganzen Knoten gilt das
//           mit dem kleinsten s ueber seinen Normalenkasten und dem
//           kleinsten sin(phi) darin - deshalb die Normalenfaecher: ein
//           Knoten mit Normalen in alle Richtungen haette immer s = 0.
//
// Beides zusammen deckt den Fall ab, in dem die Vorlage Zahlenmuell liefern
// koennte (Strahl fast in der Ebene eines fernen Dreiecks): dort greift der
// Abstand nicht (s zu klein), und die Ebene nur, wenn die Strecke weit
// genug von ihr weg ist - sonst wird das Dreieck GETESTET, wie vorher.
//
// Die Probe efxtest ("traceRay = traceRayEinfach") haelt beide Wege an
// 150000 Strecken gegeneinander, darunter gleich weite Treffer auf
// gemeinsamen Kanten mit verschiedenen Normalen (dreht man die Reihenfolge
// der Kandidaten um, meldet sie ueber 10000 Unterschiede), und baut ein
// ganzes Effektnetz einmal ueber die Hilfe, einmal ueber die Vorlage.

struct BspGeometry::SpurHilfe {
    struct Dreieck {
        float a[3], b[3], c[3];   // die Ecken, wie rayTriangle sie bekommt
        float lo[3], hi[3];       // Kasten, um eine float-Stufe nach aussen
        float n[3];               // Einheitsnormale, Vorzeichen fest
        float sinPhi;             // Winkel an a: |N| / (|e1||e2|)
        float kante;              // max(|e1|, |e2|), aufgerundet
        std::uint32_t nummer;     // Stelle in der Reihenfolge der Vorlage
        std::uint32_t normVert;   // Patch: dieser Vertex liefert die Normale
    };
    struct Knoten {
        float lo[3], hi[3];
        float nlo[3], nhi[3];     // Kasten der Normalen aller Dreiecke darin
        float minSin;
        float maxKante;
        std::uint32_t erstes;     // Blatt: erstes Dreieck; sonst rechtes Kind
        std::uint32_t anzahl;     // Blatt: > 0; sonst 0 (linkes Kind = +1)
    };
    struct Flaeche {
        bool baum = false;        // false: flaecheEinfach wie die Vorlage
        bool patch = false;
        std::uint32_t wurzelErste = 0;    // in SpurHilfe::wurzeln
        std::uint32_t wurzelAnzahl = 0;
        std::uint32_t immerErstes = 0;   // zu spitze Dreiecke: ohne Baum
        std::uint32_t immerAnzahl = 0;
    };
    std::vector<Flaeche> flaechen;
    std::vector<unsigned char> spurbar;   // Typ und Shader wie in flaecheEinfach
    std::vector<Dreieck> dreiecke;
    std::vector<Knoten> knoten;
    std::vector<std::uint32_t> wurzeln;   // je Flaeche ein Baum je Normalenfach
    // Je Blatt die Flaechen, die zaehlen (gueltig, Typ und Shader passen),
    // in der Reihenfolge von leafSurfaces - mit ihrem Kasten, dicht
    // hintereinander. Die Vorlage springt fuer jede der im Mittel 50
    // Flaechen eines Blatts in BspSurface (ueber 80 Byte je Eintrag).
    struct BlattEintrag {
        float lo[3];
        float hi[3];
        std::uint32_t sf;
    };
    std::vector<std::uint32_t> blattAnfang;   // leafs.size() + 1 Eintraege
    std::vector<BlattEintrag> blattEintraege;
    float maxKoord = 0.0F;
    // Woran die Hilfe haengt - siehe BspGeometry::spurHilfe.
    const void* verts = nullptr;
    std::size_t nVerts = 0;
    const void* indexes = nullptr;
    std::size_t nIndexes = 0;
    const void* surfaces = nullptr;
    std::size_t nSurfaces = 0;
    const void* shaders = nullptr;
    std::size_t nShaders = 0;
    const void* leafs = nullptr;
    std::size_t nLeafs = 0;
    const void* leafSurfaces = nullptr;
    std::size_t nLeafSurfaces = 0;

    [[nodiscard]] bool passt(const BspGeometry& g) const noexcept {
        return verts == g.verts.data() && nVerts == g.verts.size() &&
               indexes == g.indexes.data() && nIndexes == g.indexes.size() &&
               surfaces == g.surfaces.data() && nSurfaces == g.surfaces.size() &&
               shaders == g.shaders.data() && nShaders == g.shaders.size() &&
               leafs == g.leafs.data() && nLeafs == g.leafs.size() &&
               leafSurfaces == g.leafSurfaces.data() &&
               nLeafSurfaces == g.leafSurfaces.size();
    }
};

namespace {

// Ab so vielen Dreiecken lohnt der Baum. Darunter kostet die Pruefung, ob
// man ein Dreieck weglassen darf, so viel wie der Test selbst.
constexpr std::size_t kMinBaum = 8;
constexpr std::uint32_t kBlattGroesse = 4;
constexpr double kEpsF = 5.9604644775390625e-8;   // 2^-24
constexpr double kFehlerC = 32.0;                  // siehe oben, "C"
// Spitzer als das kommt ein Dreieck nicht in den Baum (nur Ebenentest).
constexpr float kMinSin = 0.02F;
// Der Abstandsweg verlangt eta = C eps / (s sin(phi)) <= 1e-2 - nur dann
// sind die Fehlerschranken (linear gerechnet, mit Faktor 2 Luft) gueltig.
constexpr double kMinProdukt = kFehlerC * kEpsF / 1e-2;
constexpr auto kMinProduktF = static_cast<float>(kMinProdukt);
constexpr auto kEtaZaehlerF = static_cast<float>(kFehlerC * kEpsF);
// s = |d^.n^| wird in float gerechnet (d^ und n^ je auf eine Stufe genau,
// drei Produkte, zwei Summen): hoechstens so viel zu gross.
constexpr float kSFehler = 2e-6F;
// Normalenfaecher: groesster Anteil (3 Seiten, Vorzeichen fest) mal 8x8
// fuer die beiden anderen. Innerhalb eines Fachs liegen die Normalen eng
// beieinander - der Kasten ihrer Normalen bleibt klein, und die Untergrenze
// fuer |d^.n^| taugt etwas. Ein gemischter Knoten haette immer s = 0.
constexpr int kFachTeilung = 8;

std::mutex g_spurHilfeSperre;
std::atomic<bool> g_spurVorlage{false};

float nachUnten(double x) {
    const auto f = static_cast<float>(x);
    return std::nextafter(f, -std::numeric_limits<float>::infinity());
}
float nachOben(double x) {
    const auto f = static_cast<float>(x);
    return std::nextafter(f, std::numeric_limits<float>::infinity());
}

// Ein Dreieck der Flaeche fuer die Hilfe vorbereiten. `ta/tb/tc` sind genau
// die Zeiger, die flaecheEinfach an rayTriangle gaebe.
BspGeometry::SpurHilfe::Dreieck macheDreieck(const float* ta, const float* tb,
                                             const float* tc,
                                             std::uint32_t nummer,
                                             std::uint32_t normVert) {
    BspGeometry::SpurHilfe::Dreieck t{};
    for (int k = 0; k < 3; ++k) {
        t.a[k] = ta[k];
        t.b[k] = tb[k];
        t.c[k] = tc[k];
    }
    t.nummer = nummer;
    t.normVert = normVert;
    // e1/e2 in FLOAT, wie rayTriangle; alles Weitere in double.
    float e1f[3];
    float e2f[3];
    for (int k = 0; k < 3; ++k) {
        e1f[k] = tb[k] - ta[k];
        e2f[k] = tc[k] - ta[k];
    }
    const double e1[3] = {e1f[0], e1f[1], e1f[2]};
    const double e2[3] = {e2f[0], e2f[1], e2f[2]};
    const double nn[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                          e1[2] * e2[0] - e1[0] * e2[2],
                          e1[0] * e2[1] - e1[1] * e2[0]};
    const double ln = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
    const double l1 = std::sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
    const double l2 = std::sqrt(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);
    t.kante = nachOben(std::max(l1, l2));
    t.sinPhi = 0.0F;
    t.n[0] = 0.0F;
    t.n[1] = 0.0F;
    t.n[2] = 1.0F;
    if (ln > 0.0 && l1 > 0.0 && l2 > 0.0) {
        // Abrunden: ein kleinerer Winkel macht die Schranken nur weiter.
        t.sinPhi = nachUnten(std::min(1.0, ln / (l1 * l2)));
        double n[3] = {nn[0] / ln, nn[1] / ln, nn[2] / ln};
        // Festes Vorzeichen (groesster Anteil positiv), damit der
        // Normalenkasten eines Knotens eng bleibt; fuer |d.n| und fuer
        // "beide Enden auf derselben Seite" ist es gleich.
        int gross = 0;
        for (int k = 1; k < 3; ++k) {
            if (std::fabs(n[k]) > std::fabs(n[gross])) {
                gross = k;
            }
        }
        if (n[gross] < 0.0) {
            for (double& x : n) {
                x = -x;
            }
        }
        for (int k = 0; k < 3; ++k) {
            t.n[k] = static_cast<float>(n[k]);
        }
    }
    // Der Kasten des float-Dreiecks (a, a+e1, a+e2) UND der Ecken selbst.
    for (int k = 0; k < 3; ++k) {
        const double w[5] = {ta[k], tb[k], tc[k],
                             static_cast<double>(ta[k]) + e1[k],
                             static_cast<double>(ta[k]) + e2[k]};
        double lo = w[0];
        double hi = w[0];
        for (const double x : w) {
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
        t.lo[k] = nachUnten(lo);
        t.hi[k] = nachOben(hi);
    }
    return t;
}

// Baut den Baum ueber dreiecke[von, bis) und haengt die Knoten an.
// Gibt die Nummer des Wurzelknotens zurueck.
std::uint32_t baueBaum(BspGeometry::SpurHilfe& h, std::uint32_t von,
                       std::uint32_t bis) {
    const auto ich = static_cast<std::uint32_t>(h.knoten.size());
    h.knoten.emplace_back();
    {
        BspGeometry::SpurHilfe::Knoten kn{};
        for (int k = 0; k < 3; ++k) {
            kn.lo[k] = std::numeric_limits<float>::infinity();
            kn.hi[k] = -std::numeric_limits<float>::infinity();
            kn.nlo[k] = std::numeric_limits<float>::infinity();
            kn.nhi[k] = -std::numeric_limits<float>::infinity();
        }
        kn.minSin = 1.0F;
        kn.maxKante = 0.0F;
        for (std::uint32_t i = von; i < bis; ++i) {
            const auto& t = h.dreiecke[i];
            for (int k = 0; k < 3; ++k) {
                kn.lo[k] = std::min(kn.lo[k], t.lo[k]);
                kn.hi[k] = std::max(kn.hi[k], t.hi[k]);
                kn.nlo[k] = std::min(kn.nlo[k], t.n[k]);
                kn.nhi[k] = std::max(kn.nhi[k], t.n[k]);
            }
            kn.minSin = std::min(kn.minSin, t.sinPhi);
            kn.maxKante = std::max(kn.maxKante, t.kante);
        }
        h.knoten[ich] = kn;
    }
    if (bis - von <= kBlattGroesse) {
        h.knoten[ich].erstes = von;
        h.knoten[ich].anzahl = bis - von;
        return ich;
    }
    // Teilen an der Mitte der laengsten Achse der Schwerpunkte.
    float clo[3] = {std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::infinity()};
    float chi[3] = {-clo[0], -clo[1], -clo[2]};
    auto mitte = [](const BspGeometry::SpurHilfe::Dreieck& t, int k) {
        return (t.lo[k] + t.hi[k]) * 0.5F;
    };
    for (std::uint32_t i = von; i < bis; ++i) {
        for (int k = 0; k < 3; ++k) {
            clo[k] = std::min(clo[k], mitte(h.dreiecke[i], k));
            chi[k] = std::max(chi[k], mitte(h.dreiecke[i], k));
        }
    }
    int achse = 0;
    for (int k = 1; k < 3; ++k) {
        if (chi[k] - clo[k] > chi[achse] - clo[achse]) {
            achse = k;
        }
    }
    const std::uint32_t halb = von + (bis - von) / 2;
    std::nth_element(h.dreiecke.begin() + von, h.dreiecke.begin() + halb,
                     h.dreiecke.begin() + bis,
                     [&](const auto& x, const auto& y) {
                         const float mx = mitte(x, achse);
                         const float my = mitte(y, achse);
                         return mx < my || (mx == my && x.nummer < y.nummer);
                     });
    (void)baueBaum(h, von, halb);
    const std::uint32_t rechts = baueBaum(h, halb, bis);
    h.knoten[ich].erstes = rechts;
    h.knoten[ich].anzahl = 0;
    return ich;
}

void baueSpurHilfe(BspGeometry::SpurHilfe& h, const BspGeometry& geo) {
    h.verts = geo.verts.data();
    h.nVerts = geo.verts.size();
    h.indexes = geo.indexes.data();
    h.nIndexes = geo.indexes.size();
    h.surfaces = geo.surfaces.data();
    h.nSurfaces = geo.surfaces.size();
    h.shaders = geo.shaders.data();
    h.nShaders = geo.shaders.size();
    h.leafs = geo.leafs.data();
    h.nLeafs = geo.leafs.size();
    h.leafSurfaces = geo.leafSurfaces.data();
    h.nLeafSurfaces = geo.leafSurfaces.size();
    for (const BspVertex& v : geo.verts) {
        for (const float x : v.xyz) {
            h.maxKoord = std::max(h.maxKoord, std::fabs(x));
        }
    }
    h.flaechen.assign(geo.surfaces.size(), {});
    h.spurbar.assign(geo.surfaces.size(), 0);
    std::vector<BspGeometry::SpurHilfe::Dreieck> liste;
    for (std::size_t sf = 0; sf < geo.surfaces.size(); ++sf) {
        const BspSurface& s = geo.surfaces[sf];
        // Dieselben Bedingungen wie flaecheEinfach, nur einmal statt je Test.
        const bool typOk = s.type == BspSurface::Type::Planar ||
                           s.type == BspSurface::Type::TriangleSoup ||
                           s.type == BspSurface::Type::Patch;
        const bool gezeichnet =
            !(s.shader >= 0 && s.shader < static_cast<int>(geo.shaders.size()) &&
              !geo.shaders[static_cast<std::size_t>(s.shader)].isDrawn());
        h.spurbar[sf] = (typOk && gezeichnet) ? 1 : 0;
        if (h.spurbar[sf] == 0) {
            continue;
        }
        // Die Dreiecke in der Reihenfolge der Vorlage - mit denselben
        // Auslassungen (ungueltige Ecken).
        liste.clear();
        bool einfach = false;
        if (s.type == BspSurface::Type::Patch) {
            const int pw = s.patchWidth;
            const int ph = s.patchHeight;
            if (pw < 2 || ph < 2) {
                continue;   // die Vorlage prueft hier nichts
            }
            std::uint32_t nummer = 0;
            for (int py = 0; py + 1 < ph; ++py) {
                for (int px = 0; px + 1 < pw; ++px) {
                    const auto at = [&](int x, int y) {
                        return static_cast<std::size_t>(s.firstVert) +
                               static_cast<std::size_t>(y) *
                                   static_cast<std::size_t>(pw) +
                               static_cast<std::size_t>(x);
                    };
                    const std::size_t q[4] = {at(px, py), at(px + 1, py),
                                              at(px, py + 1), at(px + 1, py + 1)};
                    if (q[3] >= geo.verts.size()) {
                        nummer += 2;
                        continue;
                    }
                    const std::size_t tri[2][3] = {{q[0], q[1], q[2]},
                                                   {q[1], q[3], q[2]}};
                    for (const auto& t3 : tri) {
                        liste.push_back(macheDreieck(
                            geo.verts[t3[0]].xyz, geo.verts[t3[1]].xyz,
                            geo.verts[t3[2]].xyz, nummer,
                            static_cast<std::uint32_t>(t3[0])));
                        ++nummer;
                    }
                }
            }
        } else {
            if (s.firstIndex < 0 || s.numIndexes < 0 ||
                static_cast<std::size_t>(s.firstIndex) +
                        static_cast<std::size_t>(s.numIndexes) >
                    geo.indexes.size()) {
                einfach = true;   // was die Vorlage hier tut, tut sie weiter
            } else {
                std::uint32_t nummer = 0;
                for (int k = 0; k + 2 < s.numIndexes; k += 3) {
                    std::size_t ii[3];
                    for (int j = 0; j < 3; ++j) {
                        ii[j] = static_cast<std::size_t>(s.firstVert) +
                                static_cast<std::size_t>(
                                    geo.indexes[static_cast<std::size_t>(s.firstIndex) +
                                                static_cast<std::size_t>(k) +
                                                static_cast<std::size_t>(j)]);
                    }
                    if (ii[0] < geo.verts.size() && ii[1] < geo.verts.size() &&
                        ii[2] < geo.verts.size()) {
                        liste.push_back(macheDreieck(
                            geo.verts[ii[0]].xyz, geo.verts[ii[1]].xyz,
                            geo.verts[ii[2]].xyz, nummer, 0U));
                    }
                    ++nummer;
                }
            }
        }
        if (einfach || liste.size() < kMinBaum) {
            continue;
        }
        BspGeometry::SpurHilfe::Flaeche& fl = h.flaechen[sf];
        fl.baum = true;
        fl.patch = s.type == BspSurface::Type::Patch;
        // Zu spitze Dreiecke kommen nicht in den Baum - fuer sie gilt nur
        // der Ebenentest.
        const auto erstes = static_cast<std::uint32_t>(h.dreiecke.size());
        for (const auto& t : liste) {
            if (t.sinPhi >= kMinSin) {
                h.dreiecke.push_back(t);
            }
        }
        const auto gutEnde = static_cast<std::uint32_t>(h.dreiecke.size());
        for (const auto& t : liste) {
            if (t.sinPhi < kMinSin) {
                h.dreiecke.push_back(t);
            }
        }
        fl.immerErstes = gutEnde;
        fl.immerAnzahl = static_cast<std::uint32_t>(h.dreiecke.size()) - gutEnde;
        // Nach Normalenfach sortieren, dann je Fach ein Baum.
        auto fach = [](const BspGeometry::SpurHilfe::Dreieck& t) {
            int gross = 0;
            for (int k = 1; k < 3; ++k) {
                if (std::fabs(t.n[k]) > std::fabs(t.n[gross])) {
                    gross = k;
                }
            }
            const int k1 = (gross + 1) % 3;
            const int k2 = (gross + 2) % 3;
            const auto zelle = [](float x) {
                const int z = static_cast<int>((x + 1.0F) * 0.5F * kFachTeilung);
                return std::clamp(z, 0, kFachTeilung - 1);
            };
            return (gross * kFachTeilung + zelle(t.n[k1])) * kFachTeilung + zelle(t.n[k2]);
        };
        std::sort(h.dreiecke.begin() + erstes, h.dreiecke.begin() + gutEnde,
                  [&](const auto& x, const auto& y) {
                      const int fx = fach(x);
                      const int fy = fach(y);
                      return fx < fy || (fx == fy && x.nummer < y.nummer);
                  });
        fl.wurzelErste = static_cast<std::uint32_t>(h.wurzeln.size());
        std::uint32_t von = erstes;
        while (von < gutEnde) {
            std::uint32_t bis = von + 1;
            while (bis < gutEnde && fach(h.dreiecke[bis]) == fach(h.dreiecke[von])) {
                ++bis;
            }
            h.wurzeln.push_back(baueBaum(h, von, bis));
            von = bis;
        }
        fl.wurzelAnzahl = static_cast<std::uint32_t>(h.wurzeln.size()) - fl.wurzelErste;
    }
}

// Die Blattlisten - nach den Flaechen, denn sie brauchen `spurbar`.
void baueBlattListen(BspGeometry::SpurHilfe& h, const BspGeometry& geo) {
    h.blattAnfang.assign(geo.leafs.size() + 1U, 0U);
    for (std::size_t bi = 0; bi < geo.leafs.size(); ++bi) {
        h.blattAnfang[bi] = static_cast<std::uint32_t>(h.blattEintraege.size());
        const BspGeometry::Leaf& blatt = geo.leafs[bi];
        for (int i = 0; i < blatt.numSurfaces; ++i) {
            const int si = blatt.firstSurface + i;
            if (si < 0 || si >= static_cast<int>(geo.leafSurfaces.size())) {
                continue;
            }
            const int sf = geo.leafSurfaces[static_cast<std::size_t>(si)];
            if (sf < 0 || sf >= static_cast<int>(geo.surfaces.size()) ||
                h.spurbar[static_cast<std::size_t>(sf)] == 0) {
                continue;
            }
            const BspSurface& s = geo.surfaces[static_cast<std::size_t>(sf)];
            BspGeometry::SpurHilfe::BlattEintrag e{};
            for (int k = 0; k < 3; ++k) {
                e.lo[k] = s.mins[k];
                e.hi[k] = s.maxs[k];
            }
            e.sf = static_cast<std::uint32_t>(sf);
            h.blattEintraege.push_back(e);
        }
    }
    h.blattAnfang[geo.leafs.size()] = static_cast<std::uint32_t>(h.blattEintraege.size());
}

const BspGeometry::SpurHilfe* spurHilfeFuer(const BspGeometry& geo) {
    const BspGeometry::SpurHilfe* h =
        geo.spurHilfe.aktuell.load(std::memory_order_acquire);
    if (h != nullptr && h->passt(geo)) {
        return h;
    }
    const std::lock_guard<std::mutex> sperre(g_spurHilfeSperre);
    h = geo.spurHilfe.aktuell.load(std::memory_order_acquire);
    if (h != nullptr && h->passt(geo)) {
        return h;
    }
    auto neu = std::make_shared<BspGeometry::SpurHilfe>();
    baueSpurHilfe(*neu, geo);
    baueBlattListen(*neu, geo);
    // Eine veraltete behalten: ein anderer Faden koennte sie gerade noch
    // lesen. Aelter als eine ist ausgeschlossen - wer die Geometrie
    // aendert, spurt nicht gleichzeitig darauf.
    auto& gebaut = geo.spurHilfe.gebaut;
    if (gebaut.size() > 1) {
        gebaut.erase(gebaut.begin(), gebaut.end() - 1);
    }
    gebaut.push_back(neu);
    geo.spurHilfe.aktuell.store(neu.get(), std::memory_order_release);
    return neu.get();
}

// Alles, was ein Spurtest fuer die Weglassregeln braucht, einmal gerechnet.
struct Strecke {
    float s[3];
    float e[3];
    float d[3];        // wie in der Vorlage: end - start, in float
    float lo[3];
    float hi[3];
    float dh[3];       // d / |d|
    double laenge;     // |d|
    double spiel;      // Rundungsfehler der Kastenrechnung in float
    float laengeF;     // dasselbe in float, aufgerundet
    float spielF;
};

// Darf ein Kasten (Knoten oder Dreieck) samt allem darin wegfallen? Der
// Abstandsweg aus dem Kommentar oben. `nlo/nhi`: der Kasten der Normalen
// (beim einzelnen Dreieck zweimal dieselbe).
bool kastenFern(const Strecke& st, const float lo[3], const float hi[3],
                const float nlo[3], const float nhi[3], float minSin,
                float maxKante) {
    float g = -std::numeric_limits<float>::infinity();
    for (int k = 0; k < 3; ++k) {
        g = std::max(g, std::max(lo[k] - st.hi[k], st.lo[k] - hi[k]));
    }
    if (!(g > 0.0F)) {
        return false;
    }
    // Untergrenze von s = |d^.n^| ueber den Normalenkasten.
    float slo = 0.0F;
    float shi = 0.0F;
    for (int k = 0; k < 3; ++k) {
        const float p = st.dh[k] * nlo[k];
        const float q = st.dh[k] * nhi[k];
        slo += std::min(p, q);
        shi += std::max(p, q);
    }
    const float sLow = ((slo > 0.0F) ? slo : ((shi < 0.0F) ? -shi : 0.0F)) - kSFehler;
    const float produkt = sLow * minSin;
    if (!(produkt >= kMinProduktF)) {
        return false;
    }
    // |T| nach oben: die Summe der Betraege statt der Wurzel (sie ist nie
    // kleiner). In float gerechnet - die paar Stufen Rundung deckt der
    // Aufschlag von einem Promille, die Subtraktionen deckt `spiel`.
    float t1 = 0.0F;
    for (int k = 0; k < 3; ++k) {
        t1 += std::max(std::fabs(st.s[k] - lo[k]), std::fabs(st.s[k] - hi[k]));
    }
    const float dmax = (kEtaZaehlerF / produkt) *
                       (5.0F * t1 + 8.0F * maxKante + 3.0F * st.laengeF);
    return g > 2.002F * dmax + st.spielF;
}

// Der Ebenenweg: beide Enden sicher auf derselben Seite?
bool ebeneFern(const Strecke& st, const BspGeometry::SpurHilfe::Dreieck& t) {
    if (!(t.sinPhi > 1e-6F)) {
        return false;
    }
    double t0[3];
    double t1[3];
    double tl2 = 0.0;
    for (int k = 0; k < 3; ++k) {
        t0[k] = static_cast<double>(st.s[k]) - t.a[k];
        t1[k] = static_cast<double>(st.e[k]) - t.a[k];
        tl2 += t0[k] * t0[k];
    }
    const double h0 = t.n[0] * t0[0] + t.n[1] * t0[1] + t.n[2] * t0[2];
    const double h1 = t.n[0] * t1[0] + t.n[1] * t1[1] + t.n[2] * t1[2];
    const double tl = std::sqrt(tl2);
    const double m = 2.0 * kFehlerC * kEpsF * (tl + st.laenge) /
                         static_cast<double>(t.sinPhi) +
                     4.0 * kEpsF * (tl + st.laenge) + 1e-12;
    return (h0 > m && h1 > m) || (h0 < -m && h1 < -m);
}

struct Kandidat {
    std::uint32_t nummer;
    std::uint32_t dreieck;
};

// Eine Flaeche mit Baum: die Dreiecke, die nicht beweisbar verfehlt
// werden, in der Reihenfolge der Vorlage durch rayTriangle.
void flaecheMitBaum(const BspGeometry& geo, const BspGeometry::SpurHilfe& h,
                    const BspGeometry::SpurHilfe::Flaeche& fl, int sf,
                    const Strecke& st, float& besteFraktion, TraceTreffer& out,
                    std::vector<Kandidat>& kand) {
    kand.clear();
    for (std::uint32_t w = fl.wurzelErste; w < fl.wurzelErste + fl.wurzelAnzahl; ++w) {
        std::uint32_t stapel[64];
        int oben = 0;
        stapel[oben++] = h.wurzeln[w];
        while (oben > 0) {
            const std::uint32_t ki = stapel[--oben];
            const auto& kn = h.knoten[ki];
            if (kastenFern(st, kn.lo, kn.hi, kn.nlo, kn.nhi, kn.minSin, kn.maxKante)) {
                continue;
            }
            if (kn.anzahl > 0) {
                for (std::uint32_t i = kn.erstes; i < kn.erstes + kn.anzahl; ++i) {
                    const auto& t = h.dreiecke[i];
                    if (kastenFern(st, t.lo, t.hi, t.n, t.n, t.sinPhi, t.kante) ||
                        ebeneFern(st, t)) {
                        continue;
                    }
                    kand.push_back({t.nummer, i});
                }
                continue;
            }
            if (oben + 2 > 64) {
                // Tiefer als 64 wird ein halbierender Baum ueber weniger als
                // 2^32 Dreiecke nicht - trotzdem lieber die Vorlage als
                // etwas verlieren. besteFraktion ist hier noch unberuehrt.
                flaecheEinfach(geo, sf, st.s, st.e, st.d, besteFraktion, out);
                return;
            }
            stapel[oben++] = kn.erstes;   // rechts
            stapel[oben++] = ki + 1;      // links
        }
    }
    for (std::uint32_t i = fl.immerErstes; i < fl.immerErstes + fl.immerAnzahl; ++i) {
        if (!ebeneFern(st, h.dreiecke[i])) {
            kand.push_back({h.dreiecke[i].nummer, i});
        }
    }
    std::sort(kand.begin(), kand.end(),
              [](const Kandidat& x, const Kandidat& y) { return x.nummer < y.nummer; });
    for (const Kandidat& kd : kand) {
        const auto& t = h.dreiecke[kd.dreieck];
        const float f = rayTriangle(st.s, st.d, t.a, t.b, t.c);
        if (!(f >= 0.0F && f < besteFraktion)) {
            continue;
        }
        // Ab hier WORTGLEICH mit flaecheEinfach.
        besteFraktion = f;
        out.hit = true;
        out.fraction = f;
        out.surface = sf;
        if (fl.patch) {
            for (int w2 = 0; w2 < 3; ++w2) {
                out.point[w2] = st.s[w2] + st.d[w2] * f;
                out.normal[w2] = geo.verts[t.normVert].normal[w2];
            }
            continue;
        }
        for (int q = 0; q < 3; ++q) {
            out.point[q] = st.s[q] + st.d[q] * f;
        }
        const float e1[3] = {t.b[0] - t.a[0], t.b[1] - t.a[1], t.b[2] - t.a[2]};
        const float e2[3] = {t.c[0] - t.a[0], t.c[1] - t.a[1], t.c[2] - t.a[2]};
        float n[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                      e1[2] * e2[0] - e1[0] * e2[2],
                      e1[0] * e2[1] - e1[1] * e2[0]};
        const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (nl > 1e-6F) {
            const float dl = n[0] * (st.e[0] - st.s[0]) +
                             n[1] * (st.e[1] - st.s[1]) +
                             n[2] * (st.e[2] - st.s[2]);
            const float vz = (dl > 0.0F) ? -1.0F : 1.0F;
            for (int q = 0; q < 3; ++q) {
                out.normal[q] = vz * n[q] / nl;
            }
        }
    }
}

// Je Faden: welche Flaeche wurde in DIESEM Spurtest schon geprueft
// (Punkt 2 oben). Ein Zaehler statt Loeschen - geloescht wird nur, wenn er
// ueberlaeuft.
struct Merkstempel {
    std::vector<std::uint32_t> stempel;
    std::uint32_t runde = 0;
    std::vector<Kandidat> kand;
};

}  // namespace

void setzeSpurVorlage(bool an) noexcept {
    g_spurVorlage.store(an, std::memory_order_relaxed);
}

BspGeometry::SpurHilfeHalter::~SpurHilfeHalter() = default;

void BspGeometry::SpurHilfeHalter::vergiss() noexcept {
    const std::lock_guard<std::mutex> sperre(g_spurHilfeSperre);
    aktuell.store(nullptr, std::memory_order_release);
    gebaut.clear();
}

TraceTreffer traceRay(const BspGeometry& geo, const float start[3],
                      const float end[3]) {
    TraceTreffer out;
    const float d[3] = {end[0] - start[0], end[1] - start[1],
                        end[2] - start[2]};
    const float laenge = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (laenge < 1e-4F) {
        return out;
    }
    if (g_spurVorlage.load(std::memory_order_relaxed)) {
        return traceRayEinfach(geo, start, end);
    }
    const BspGeometry::SpurHilfe* h = spurHilfeFuer(geo);
    Strecke st{};
    double groesst = 0.0;
    for (int k = 0; k < 3; ++k) {
        st.s[k] = start[k];
        st.e[k] = end[k];
        st.d[k] = d[k];
        st.lo[k] = std::min(start[k], end[k]);
        st.hi[k] = std::max(start[k], end[k]);
        st.dh[k] = d[k] / laenge;
        groesst = std::max(groesst, static_cast<double>(std::fabs(start[k])));
        groesst = std::max(groesst, static_cast<double>(std::fabs(end[k])));
    }
    st.laenge = laenge;
    // g = lo - hi in float: je Subtraktion hoechstens eine halbe Stufe der
    // groessten beteiligten Koordinate; vierfach genommen.
    st.spiel = 4.0 * kEpsF * (static_cast<double>(h->maxKoord) + groesst + st.laenge) + 1e-9;
    st.laengeF = nachOben(st.laenge);
    st.spielF = nachOben(st.spiel);

    thread_local Merkstempel merk;
    if (merk.stempel.size() < geo.surfaces.size()) {
        merk.stempel.resize(geo.surfaces.size(), 0U);
    }
    if (++merk.runde == 0U) {
        std::fill(merk.stempel.begin(), merk.stempel.end(), 0U);
        merk.runde = 1U;
    }
    const std::uint32_t runde = merk.runde;

    // Derselbe Weg durch den Baum wie in traceRayEinfach, in derselben
    // Reihenfolge (erst Kind 1, dann Kind 0).
    int stapel[256];
    int oben = 0;
    stapel[oben++] = 0;
    float besteFraktion = 1.0F;
    while (oben > 0) {
        const int knoten = stapel[--oben];
        if (knoten < 0) {
            const auto bi = static_cast<std::size_t>(-(knoten + 1));
            if (bi >= geo.leafs.size()) {
                continue;
            }
            // Dieselben Flaechen in derselben Reihenfolge wie in der
            // Vorlage, ohne die, die dort ohnehin nichts pruefen.
            for (std::uint32_t e = h->blattAnfang[bi]; e < h->blattAnfang[bi + 1U]; ++e) {
                const BspGeometry::SpurHilfe::BlattEintrag& be = h->blattEintraege[e];
                if (st.hi[0] < be.lo[0] || st.lo[0] > be.hi[0] ||
                    st.hi[1] < be.lo[1] || st.lo[1] > be.hi[1] ||
                    st.hi[2] < be.lo[2] || st.lo[2] > be.hi[2]) {
                    continue;   // der Kasten der Flaeche, wie in der Vorlage
                }
                const std::size_t sfu = be.sf;
                if (merk.stempel[sfu] == runde) {
                    continue;   // schon geprueft, siehe Punkt 2
                }
                merk.stempel[sfu] = runde;
                const int sf = static_cast<int>(be.sf);
                const auto& fl = h->flaechen[sfu];
                if (fl.baum) {
                    flaecheMitBaum(geo, *h, fl, sf, st, besteFraktion, out, merk.kand);
                } else {
                    flaecheEinfach(geo, sf, start, end, d, besteFraktion, out);
                }
            }
            continue;
        }
        if (static_cast<std::size_t>(knoten) >= geo.nodes.size()) {
            continue;
        }
        const BspGeometry::Node& n = geo.nodes[static_cast<std::size_t>(knoten)];
        if (n.plane < 0 || static_cast<std::size_t>(n.plane) >= geo.planes.size()) {
            continue;
        }
        const BspGeometry::Plane& pl = geo.planes[static_cast<std::size_t>(n.plane)];
        const float d0 = start[0] * pl.normal[0] + start[1] * pl.normal[1] +
                         start[2] * pl.normal[2] - pl.dist;
        const float d1 = end[0] * pl.normal[0] + end[1] * pl.normal[1] +
                         end[2] * pl.normal[2] - pl.dist;
        if (oben + 2 > 256) {
            // Ein Baum, tiefer als je eine Karte - dann die Vorlage.
            return traceRayEinfach(geo, start, end);
        }
        if (d0 >= 0.0F && d1 >= 0.0F) {
            stapel[oben++] = n.children[0];
        } else if (d0 < 0.0F && d1 < 0.0F) {
            stapel[oben++] = n.children[1];
        } else {
            stapel[oben++] = n.children[0];
            stapel[oben++] = n.children[1];
        }
    }
    return out;
}

}  // namespace bhed
