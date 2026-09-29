// gpudraw.cpp - siehe bhed/gpudraw.h
//
// Braucht kein Direct3D. Geprueft in tests/gpudraw.cpp.

#include "bhed/gpudraw.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bhed::gpu {

namespace {

// Deckend zuerst, gemischt danach.
//
// Warum nicht einfach nach Zustand sortieren?
// -------------------------------------------
// Weil die Reihenfolge unter den gemischten NICHT beliebig ist. Additives
// Mischen ist zwar vertauschbar (a+b = b+a), aber Alpha ist es nicht: was
// zuerst gezeichnet wird, liegt hinten. Und der Tiefenpuffer hilft nur bei
// deckenden Flaechen - gemischte schreiben ihn meist gar nicht.
//
// Deshalb: nur die deckenden werden nach Zustand gruppiert. Bei den
// gemischten bleibt die Reihenfolge, in der sie in der Karte stehen - genau
// wie der vorhandene Rasterer es macht. Ein Umsortieren dort waere eine
// Aenderung des Bildes, keine Beschleunigung.
int gruppe(const DrawCall& c) {
    // Eine ZUSATZSTUFE zaehlt immer zur zweiten Gruppe, auch wenn sie
    // deckend ist.
    //
    // Sie liegt auf derselben Flaeche wie ihre Grundstufe und muss NACH ihr
    // gezeichnet werden. Die erste Gruppe wird aber nach Zustand
    // umgruppiert, damit gleiche beieinanderliegen - dort koennte eine
    // Zusatzstufe vor ihrer eigenen Grundstufe landen. In der zweiten Gruppe
    // bleibt die Reihenfolge der Karte, und die ist Grundstufe zuerst.
    return (c.state.blend == Blend::Opaque && !c.zusatzstufe) ? 0 : 1;
}

}  // namespace

std::vector<DrawCall> buildDrawCalls(const BspMesh& mesh,
                                     const TextureSet* textures,
                                     float zeitSekunden) {
    std::vector<DrawCall> calls;
    calls.reserve(mesh.batches.size());

    for (std::size_t i = 0; i < mesh.batches.size(); ++i) {
        const BspMesh::Batch& b = mesh.batches[i];
        if (b.numIndexes == 0U) {
            continue;
        }
        const BatchState bs = batchStateFor(textures, b.shader, zeitSekunden);
        if (bs.tex == nullptr) {
            // Kein brauchbares Bild. UEBERSPRINGEN, nicht ersetzen.
            //
            // In rc367 hat ein Rueckfall auf "Platz 0" riesige Lavavierecke
            // ins Bild gemalt, weil Platz 0 zufaellig die Lavatextur war.
            // Eine fehlende Textur soll als Loch auffallen, nicht als etwas
            // anderes durchgehen.
            continue;
        }
        DrawCall c;
        const Licht l = lichtFuer(bs, b.lightmap, b.vertexColour, false);
        c.state = pipelineFor(bs, l.lightmap, l.roh, false, l.fest);
        c.firstIndex = b.firstIndex;
        c.numIndexes = b.numIndexes;
        c.bild = b.shader;
        c.lightmap = b.lightmap;
        c.batch = i;
        calls.push_back(c);

        // --- Und die Stufen darueber -----------------------------------
        //
        // Dieselbe Geometrie, ein anderer Platz im Texturensatz. Der
        // Rasterer legt dafuer eigene Auftraege an (mapview.cpp:1222,
        // "Die Stufen darueber"); hier sind es eigene Zeichenaufrufe.
        //
        // Eine Stufe ohne brauchbares Bild wird UEBERSPRUNGEN, wie die
        // Grundstufe auch - eine fehlende Textur soll als Loch auffallen.
        if (textures == nullptr ||
            static_cast<std::size_t>(b.shader) >= textures->byShader.size()) {
            continue;
        }
        for (const int extra :
             textures->byShader[static_cast<std::size_t>(b.shader)]
                 .extraStages) {
            if (extra < 0 ||
                static_cast<std::size_t>(extra) >= textures->byShader.size()) {
                continue;
            }
            const BatchState es = batchStateFor(textures, extra, zeitSekunden);
            if (es.tex == nullptr) {
                continue;
            }
            DrawCall z;
            const Licht lz = lichtFuer(es, b.lightmap, b.vertexColour, true);
            z.state = pipelineFor(es, lz.lightmap, lz.roh, true, lz.fest);
            z.firstIndex = b.firstIndex;
            z.numIndexes = b.numIndexes;
            z.bild = extra;
            z.lightmap = b.lightmap;
            z.batch = i;
            z.zusatzstufe = true;
            calls.push_back(z);
        }
    }

    // Stabil sortieren: gleiche Gruppe behaelt ihre Reihenfolge.
    //
    // std::stable_sort und nicht std::sort - bei den gemischten haengt das
    // Bild an der Reihenfolge, und eine instabile Sortierung wuerde sie je
    // nach Standardbibliothek anders vertauschen. Ein Bild, das sich mit dem
    // Compiler aendert, ist nicht abnehmbar.
    std::stable_sort(calls.begin(), calls.end(),
                     [](const DrawCall& a, const DrawCall& b2) {
                         const int ga = gruppe(a);
                         const int gb = gruppe(b2);
                         if (ga != gb) {
                             return ga < gb;
                         }
                         if (ga != 0) {
                             // Gemischt: Reihenfolge der Karte behalten.
                             return false;
                         }
                         // Deckend: nach Zustand gruppieren, damit gleiche
                         // beieinanderliegen und kein Wechsel noetig ist.
                         if (a.state.features != b2.state.features) {
                             return a.state.features < b2.state.features;
                         }
                         if (a.state.numTexMods != b2.state.numTexMods) {
                             return a.state.numTexMods < b2.state.numTexMods;
                         }
                         return false;
                     });
    return calls;
}

std::vector<DrawCall> nurLage(const std::vector<DrawCall>& calls, Lage lage) {
    if (lage == Lage::Alles) {
        return calls;
    }
    std::vector<DrawCall> raus;
    raus.reserve(calls.size());
    for (const DrawCall& c : calls) {
        const bool deckend = (c.state.blend == Blend::Opaque);
        if ((lage == Lage::Deckend) == deckend) {
            raus.push_back(c);
        }
    }
    return raus;
}

std::vector<DrawCall> nurGluehende(const std::vector<DrawCall>& calls) {
    std::vector<DrawCall> aus;
    for (const DrawCall& c : calls) {
        if ((c.state.features & kGlow) != 0U) {
            aus.push_back(c);
        }
    }
    return aus;
}

std::vector<SpriteEcke> baueAutosprites(const BspMesh& mesh,
                                        const std::vector<char>& istSprite,
                                        std::vector<std::uint32_t>& indizes) {
    std::vector<SpriteEcke> aus;
    if (indizes.size() != mesh.indexes.size()) {
        return aus;
    }
    // Ein Abschnitt: die verschiedenen Ecken in der Reihenfolge ihres
    // ersten Auftretens, je vier ein Viereck - dieselbe Sammlung wie im
    // Rasterer.
    auto abschnitt = [&](std::uint32_t first, std::uint32_t count) {
        if (count < 6U || (count % 6U) != 0U ||
            static_cast<std::size_t>(first) + count > mesh.indexes.size()) {
            return;
        }
        std::vector<std::uint32_t> ids;
        for (std::uint32_t q = 0; q < count; ++q) {
            const std::uint32_t id = mesh.indexes[first + q];
            bool schon = false;
            for (const std::uint32_t w : ids) {
                if (w == id) { schon = true; break; }
            }
            if (!schon) {
                if (id >= mesh.verts.size()) { return; }
                ids.push_back(id);
            }
        }
        if (ids.empty() || (ids.size() % 4U) != 0U ||
            count != (ids.size() / 4U) * 6U) {
            return;
        }
        // Richtung und Texturkoordinate je Ecke, Reihenfolge wie
        // RB_AddQuadStamp: +links+hoch, -links+hoch, -links-hoch, +links-hoch.
        static const float richtung[4][2] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
        static const float st[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        std::uint32_t schreib = first;
        for (std::size_t q = 0; q + 3 < ids.size(); q += 4) {
            float mitte[3]{};
            for (int w = 0; w < 4; ++w) {
                for (int k = 0; k < 3; ++k) {
                    mitte[k] += 0.25F * mesh.verts[ids[q + static_cast<std::size_t>(w)]].xyz[k];
                }
            }
            const BspVertex& v0 = mesh.verts[ids[q]];
            const float dx = v0.xyz[0] - mitte[0];
            const float dy = v0.xyz[1] - mitte[1];
            const float dz = v0.xyz[2] - mitte[2];
            // 0.707 steht so in der Engine.
            const float radius = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.707F;
            for (int w = 0; w < 4; ++w) {
                SpriteEcke e;
                e.ecke = ids[q + static_cast<std::size_t>(w)];
                for (int k = 0; k < 3; ++k) { e.mitte[k] = mitte[k]; }
                e.links = richtung[w][0];
                e.hoch = richtung[w][1];
                e.radius = radius;
                e.st[0] = st[w][0];
                e.st[1] = st[w][1];
                aus.push_back(e);
            }
            const std::uint32_t* e = &ids[q];
            const std::uint32_t neu[6] = {e[0], e[1], e[3], e[3], e[1], e[2]};
            for (const std::uint32_t n : neu) { indizes[schreib++] = n; }
        }
    };
    for (std::size_t b = 0; b < mesh.batches.size() && b < istSprite.size(); ++b) {
        if (istSprite[b] == 0) {
            continue;
        }
        const BspMesh::Batch& bt = mesh.batches[b];
        if (bt.numRuns > 0U && !mesh.runs.empty()) {
            for (std::uint32_t k = 0; k < bt.numRuns; ++k) {
                const std::size_t at = static_cast<std::size_t>(bt.firstRun) + k;
                if (at >= mesh.runs.size()) { break; }
                abschnitt(mesh.runs[at].firstIndex, mesh.runs[at].numIndexes);
            }
        } else {
            abschnitt(bt.firstIndex, bt.numIndexes);
        }
    }
    return aus;
}

std::size_t countStateChanges(const std::vector<DrawCall>& calls) {
    if (calls.empty()) {
        return 0U;
    }
    std::size_t n = 1U;
    for (std::size_t i = 1; i < calls.size(); ++i) {
        if (!calls[i].state.gleichWie(calls[i - 1].state)) {
            ++n;
        }
    }
    return n;
}

}  // namespace bhed::gpu
