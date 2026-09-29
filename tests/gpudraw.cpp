// Probe fuer die Zeichenliste.
//
// Datenfrei: das Netz wird hier von Hand gebaut, damit die Probe ohne .bsp
// und ohne pk3 laeuft. Was sie prueft, ist die REIHENFOLGE und das
// Zusammenfassen - genau die zwei Dinge, die im Bild als "Rauch hinter dem
// Felsen" oder "Lava durch die Wand" auffallen.

#include "bhed/gpudraw.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace bhed;
using namespace bhed::gpu;

namespace {

int fehler = 0;

void erwarte(const char* was, bool ok) {
    std::printf("  %-4s  %s\n", ok ? "ok" : "FEHL", was);
    if (!ok) {
        ++fehler;
    }
}

// Ein Texturensatz mit ein paar Eintraegen: deckend, additiv, alpha.
TextureSet baueTexturen() {
    TextureSet ts;
    ts.byShader.resize(4);
    for (auto& t : ts.byShader) {
        // Ein Eintrag gilt nur als brauchbar, wenn er Bildpunkte hat.
        t.width = 2;
        t.height = 2;
        t.rgba.assign(16, 255);
        t.buildMips();
    }
    // 0: deckend
    ts.byShader[0].srcFactor = BlendFactor::One;
    ts.byShader[0].dstFactor = BlendFactor::Zero;
    // 1: additiv
    ts.byShader[1].blend = BlendMode::Add;
    ts.byShader[1].srcFactor = BlendFactor::One;
    ts.byShader[1].dstFactor = BlendFactor::One;
    // 2: alpha
    ts.byShader[2].blend = BlendMode::Alpha;
    ts.byShader[2].srcFactor = BlendFactor::SrcAlpha;
    ts.byShader[2].dstFactor = BlendFactor::OneMinusSrcAlpha;
    // 3: deckend, aber mit tcMod - also ein ANDERER Zustand
    ts.byShader[3].srcFactor = BlendFactor::One;
    ts.byShader[3].dstFactor = BlendFactor::Zero;
    ts.byShader[3].numTexMods = 1;
    ts.byShader[3].texMods[0].kind = TexModKind::Scroll;
    return ts;
}

// Derselbe Satz, aber Shader 0 traegt eine ZUSATZSTUFE: das additive
// Leuchten (Platz 1). So sehen die Bildschirme in md_am_sith aus.
TextureSet baueTexturenMitStufe() {
    TextureSet ts = baueTexturen();
    ts.byShader[0].extraStages.push_back(1);
    return ts;
}

BspMesh baueNetz(const std::vector<int>& shaderJeStapel) {
    BspMesh m;
    std::uint32_t at = 0;
    for (const int sh : shaderJeStapel) {
        BspMesh::Batch b;
        b.shader = sh;
        b.lightmap = -1;
        b.firstIndex = at;
        b.numIndexes = 3;
        at += 3;
        m.batches.push_back(b);
    }
    m.indexes.assign(at, 0);
    return m;
}

}  // namespace

int main() {
    std::printf("gpudraw\n");
    const TextureSet ts = baueTexturen();

    // --- Deckend zuerst, gemischt danach ---------------------------------
    //
    // Absichtlich in der falschen Reihenfolge eingespeist.
    {
        const BspMesh m = baueNetz({1, 0, 2, 0});
        const auto calls = buildDrawCalls(m, &ts, 0.0F);
        erwarte("alle vier Stapel kommen an", calls.size() == 4);
        bool gemischtGesehen = false;
        bool deckendNachGemischt = false;
        for (const auto& c : calls) {
            if (c.state.blend != Blend::Opaque) {
                gemischtGesehen = true;
            } else if (gemischtGesehen) {
                deckendNachGemischt = true;
            }
        }
        erwarte("kein deckender Stapel nach einem gemischten",
                !deckendNachGemischt);
    }

    // --- Unter den GEMISCHTEN bleibt die Reihenfolge der Karte ------------
    //
    // Additiv ist vertauschbar, Alpha nicht. Wer hier umsortiert, aendert
    // das Bild - und zwar an genau den Stellen, an denen es am meisten
    // auffaellt.
    {
        const BspMesh m = baueNetz({2, 1, 2});
        const auto calls = buildDrawCalls(m, &ts, 0.0F);
        erwarte("drei gemischte Stapel", calls.size() == 3);
        erwarte("Reihenfolge der Karte bleibt",
                calls[0].batch == 0 && calls[1].batch == 1 &&
                    calls[2].batch == 2);
    }

    // --- Deckende werden nach Zustand gruppiert ---------------------------
    {
        const BspMesh m = baueNetz({0, 3, 0, 3});
        const auto calls = buildDrawCalls(m, &ts, 0.0F);
        erwarte("vier deckende Stapel", calls.size() == 4);
        // 0,0,3,3 statt 0,3,0,3 - sonst vier Zustandswechsel statt zwei.
        erwarte("gleiche Zustaende liegen beieinander",
                calls[0].state.gleichWie(calls[1].state) &&
                    calls[2].state.gleichWie(calls[3].state) &&
                    !calls[1].state.gleichWie(calls[2].state));
        erwarte("also zwei Zustandswechsel statt vier",
                countStateChanges(calls) == 2);
    }

    // --- Ein Stapel ohne Textur wird UEBERSPRUNGEN ------------------------
    //
    // Nicht durch einen Ersatz gezeichnet. In rc367 hat ein Rueckfall auf
    // "Platz 0" riesige Lavavierecke ins Bild gemalt, weil Platz 0 zufaellig
    // die Lavatextur war.
    {
        TextureSet leer = ts;
        leer.byShader[1].rgba.clear();
        leer.byShader[1].width = 0;
        leer.byShader[1].height = 0;
        const BspMesh m = baueNetz({0, 1, 0});
        const auto calls = buildDrawCalls(m, &leer, 0.0F);
        erwarte("der Stapel ohne Bild faellt weg", calls.size() == 2);
        for (const auto& c : calls) {
            erwarte("und keiner zeigt auf den fehlenden Platz", c.bild != 1);
        }
    }

    // --- Leere Stapel kommen gar nicht erst in die Liste -------------------
    {
        BspMesh m = baueNetz({0, 0});
        m.batches[0].numIndexes = 0;
        const auto calls = buildDrawCalls(m, &ts, 0.0F);
        erwarte("ein Stapel ohne Indizes faellt weg", calls.size() == 1);
    }

    // --- Ohne Texturensatz gibt es nichts zu zeichnen ----------------------
    {
        const BspMesh m = baueNetz({0, 1});
        erwarte("kein Texturensatz -> leere Liste",
                buildDrawCalls(m, nullptr, 0.0F).empty());
    }

    // --- Der Gluehdurchgang ------------------------------------------------
    //
    // Er nimmt dieselben Stapel, aber nur die leuchtenden - und in
    // DERSELBEN Reihenfolge. Wuerde er umsortieren, leuchtete etwas an einer
    // Stelle, an der es im Hauptbild verdeckt ist.
    {
        TextureSet mitGlow = ts;
        mitGlow.byShader[0].glow = true;   // deckend UND leuchtend
        mitGlow.byShader[2].glow = true;   // alpha UND leuchtend
        const BspMesh m = baueNetz({0, 1, 2, 0});
        const auto alle = buildDrawCalls(m, &mitGlow, 0.0F);
        const auto gluehend = nurGluehende(alle);
        erwarte("drei von vier gluehen", gluehend.size() == 3);
        for (const auto& c : gluehend) {
            erwarte("  und jeder traegt das Bit",
                    (c.state.features & kGlow) != 0U);
        }
        // Die Reihenfolge muss die der vollen Liste sein.
        std::size_t j = 0;
        bool inOrdnung = true;
        for (const auto& c : alle) {
            if ((c.state.features & kGlow) != 0U) {
                if (j >= gluehend.size() || gluehend[j].batch != c.batch) {
                    inOrdnung = false;
                }
                ++j;
            }
        }
        erwarte("die Reihenfolge bleibt die der vollen Liste", inOrdnung);
    }
    {
        // Ohne leuchtende Stapel ist der Durchgang leer - und darf nicht
        // etwa alle zurueckgeben.
        const BspMesh m = baueNetz({0, 1, 2});
        erwarte("nichts glueht -> leere Liste",
                nurGluehende(buildDrawCalls(m, &ts, 0.0F)).empty());
    }

    // --- Die Lage: deckend gegen gemischt ---------------------------------
    //
    // Der Fehler, den das beheben soll: der GPU-Weg zeichnet Karte, dann
    // Mover. Beide Listen sind IN SICH richtig sortiert - und trotzdem
    // stehen die gemischten Kartenflaechen vor den deckenden Tueren.
    {
        // 0 deckend, 2 alpha, 3 deckend-mit-tcMod, 1 additiv
        const BspMesh m = baueNetz({0, 2, 3, 1});
        const std::vector<DrawCall> alle = buildDrawCalls(m, &ts, 0.0F);
        const std::vector<DrawCall> d = nurLage(alle, Lage::Deckend);
        const std::vector<DrawCall> g = nurLage(alle, Lage::Gemischt);

        erwarte("zwei deckende", d.size() == 2);
        erwarte("zwei gemischte", g.size() == 2);
        erwarte("zusammen wieder alle", d.size() + g.size() == alle.size());

        bool nurDeckend = true;
        for (const DrawCall& c : d) {
            if (c.state.blend != Blend::Opaque) { nurDeckend = false; }
        }
        erwarte("in der deckenden Lage ist nichts Gemischtes", nurDeckend);
        bool nurGemischt = true;
        for (const DrawCall& c : g) {
            if (c.state.blend == Blend::Opaque) { nurGemischt = false; }
        }
        erwarte("in der gemischten Lage ist nichts Deckendes", nurGemischt);

        // Die Reihenfolge INNERHALB einer Lage muss die der vollen Liste
        // sein. Bei den gemischten haengt das Bild daran.
        bool folge = true;
        std::size_t j = 0;
        for (const DrawCall& c : alle) {
            if (c.state.blend != Blend::Opaque) {
                if (j >= g.size() || g[j].batch != c.batch) { folge = false; }
                ++j;
            }
        }
        erwarte("gemischte behalten ihre Reihenfolge", folge && j == g.size());
    }
    {
        // Zwei Netze nacheinander - Karte und Mover. Die Lage muss die
        // Reihenfolge ueber BEIDE richtigstellen.
        const BspMesh karte = baueNetz({0, 2});     // deckend, alpha
        const BspMesh mover = baueNetz({0});        // deckend (die Tuer)
        const std::vector<DrawCall> ck = buildDrawCalls(karte, &ts, 0.0F);
        const std::vector<DrawCall> cm = buildDrawCalls(mover, &ts, 0.0F);

        // So lief es bisher: erst die ganze Karte, dann der Mover.
        std::vector<DrawCall> alt;
        alt.insert(alt.end(), ck.begin(), ck.end());
        alt.insert(alt.end(), cm.begin(), cm.end());
        bool deckendNachGemischt = false;
        bool schonGemischt = false;
        for (const DrawCall& c : alt) {
            if (c.state.blend != Blend::Opaque) { schonGemischt = true; }
            else if (schonGemischt) { deckendNachGemischt = true; }
        }
        erwarte("der alte Ablauf zeichnet deckend NACH gemischt (der Fehler)",
                deckendNachGemischt);

        // Und so soll es laufen: zwei Durchgaenge ueber beide Netze.
        std::vector<DrawCall> neu;
        for (const auto* l : {&ck, &cm}) {
            const std::vector<DrawCall> t = nurLage(*l, Lage::Deckend);
            neu.insert(neu.end(), t.begin(), t.end());
        }
        for (const auto* l : {&ck, &cm}) {
            const std::vector<DrawCall> t = nurLage(*l, Lage::Gemischt);
            neu.insert(neu.end(), t.begin(), t.end());
        }
        erwarte("gleich viele Aufrufe wie vorher", neu.size() == alt.size());
        bool sauber = true;
        schonGemischt = false;
        for (const DrawCall& c : neu) {
            if (c.state.blend != Blend::Opaque) { schonGemischt = true; }
            else if (schonGemischt) { sauber = false; }
        }
        erwarte("in zwei Lagen steht kein deckender mehr hinter einem "
                "gemischten", sauber);
    }
    {
        // Lage::Alles muss die Liste unveraendert lassen - der
        // Gluehdurchgang und der Vergleich haengen daran.
        const BspMesh m = baueNetz({0, 2, 1});
        const std::vector<DrawCall> alle = buildDrawCalls(m, &ts, 0.0F);
        const std::vector<DrawCall> a2 = nurLage(alle, Lage::Alles);
        bool gleich = (a2.size() == alle.size());
        for (std::size_t i = 0; gleich && i < alle.size(); ++i) {
            if (a2[i].batch != alle[i].batch) { gleich = false; }
        }
        erwarte("Lage::Alles aendert nichts", gleich);
        erwarte("leere Liste bleibt leer",
                nurLage({}, Lage::Deckend).empty());
    }

    // --- Die Stufen UEBER der ersten --------------------------------------
    //
    // Der GPU-Weg zeichnete bis rc435 nur die Grundstufe. Das additive
    // Leuchten der Bildschirme sitzt darueber - deshalb fand der
    // Gluehdurchgang nur zwei Aufrufe, waehrend der Rasterer 61529
    // Bildpunkte gluehen liess.
    {
        const TextureSet ts2 = baueTexturenMitStufe();
        const BspMesh m = baueNetz({0, 0});
        const std::vector<DrawCall> calls = buildDrawCalls(m, &ts2, 0.0F);
        erwarte("zwei Stapel mit je einer Zusatzstufe ergeben vier Aufrufe",
                calls.size() == 4);

        int zusatz = 0;
        for (const DrawCall& c : calls) {
            if (c.zusatzstufe) { ++zusatz; }
        }
        erwarte("zwei davon sind Zusatzstufen", zusatz == 2);

        // Die Zusatzstufe zeigt auf ihren EIGENEN Platz, nicht auf den des
        // Stapels - sonst zeichnete sie dasselbe Bild noch einmal.
        bool eigenesBild = true;
        for (const DrawCall& c : calls) {
            if (c.zusatzstufe && c.bild != 1) { eigenesBild = false; }
            if (!c.zusatzstufe && c.bild != 0) { eigenesBild = false; }
        }
        erwarte("die Zusatzstufe hat ihr eigenes Bild", eigenesBild);

        // Und sie liegt auf DERSELBEN Geometrie.
        bool gleicheEcken = true;
        for (const DrawCall& c : calls) {
            if (!c.zusatzstufe) { continue; }
            bool gefunden = false;
            for (const DrawCall& g : calls) {
                if (!g.zusatzstufe && g.batch == c.batch &&
                    g.firstIndex == c.firstIndex &&
                    g.numIndexes == c.numIndexes) {
                    gefunden = true;
                }
            }
            if (!gefunden) { gleicheEcken = false; }
        }
        erwarte("die Zusatzstufe liegt auf derselben Geometrie", gleicheEcken);

        // Das Wichtigste: die Grundstufe kommt VOR ihrer Zusatzstufe.
        bool reihenfolge = true;
        for (std::size_t i = 0; i < calls.size(); ++i) {
            if (!calls[i].zusatzstufe) { continue; }
            bool grundstufeDavor = false;
            for (std::size_t j = 0; j < i; ++j) {
                if (!calls[j].zusatzstufe && calls[j].batch == calls[i].batch) {
                    grundstufeDavor = true;
                }
            }
            if (!grundstufeDavor) { reihenfolge = false; }
        }
        erwarte("die Grundstufe kommt VOR ihrer Zusatzstufe", reihenfolge);
    }
    {
        // Die Zusatzstufe glueht (Platz 1 ist additiv, aber nicht als
        // gluehend gekennzeichnet) - hier zaehlt nur, dass der
        // Gluehdurchgang die Stufen ueberhaupt sehen KANN.
        TextureSet ts2 = baueTexturenMitStufe();
        ts2.byShader[1].glow = true;
        const BspMesh m = baueNetz({0});
        const std::vector<DrawCall> alle = buildDrawCalls(m, &ts2, 0.0F);
        const std::vector<DrawCall> g = nurGluehende(alle);
        erwarte("der Gluehdurchgang findet die gluehende ZUSATZSTUFE",
                g.size() == 1 && g[0].zusatzstufe);
    }
    {
        // Ohne Zusatzstufen aendert sich nichts - der alte Weg bleibt.
        const BspMesh m = baueNetz({0, 2, 1});
        erwarte("ohne Zusatzstufen bleibt die Liste wie bisher",
                buildDrawCalls(m, &ts, 0.0F).size() == 3);
    }

    // --- Die Zaehlung selbst ----------------------------------------------
    {
        erwarte("leere Liste hat null Wechsel",
                countStateChanges({}) == 0U);
        const BspMesh m = baueNetz({0});
        erwarte("ein Aufruf ist ein Zustand",
                countStateChanges(buildDrawCalls(m, &ts, 0.0F)) == 1U);
    }

    // --- Autosprite: Mitte, Radius, Richtung, Wicklung --------------------
    //
    // Ein Viereck 40 breit, 100 hoch, stehend in der x-z-Ebene um (10,20,30)
    // - wie das Palpatine-Hologramm. Die Engine (AutospriteDeform): Mitte,
    // Radius = |Ecke - Mitte| * 0,707, Wicklung 0,1,3 / 3,1,2.
    {
        BspMesh m;
        const float ecken[4][3] = {{-10, 20, -20}, {30, 20, -20}, {30, 20, 80}, {-10, 20, 80}};
        for (const auto& e : ecken) {
            BspVertex v;
            for (int k = 0; k < 3; ++k) { v.xyz[k] = e[k]; }
            m.verts.push_back(v);
        }
        m.indexes = {0, 1, 2, 0, 2, 3};
        BspMesh::Batch b;
        b.firstIndex = 0;
        b.numIndexes = 6;
        m.batches.push_back(b);
        std::vector<std::uint32_t> idx = m.indexes;
        const std::vector<SpriteEcke> sp = baueAutosprites(m, {1}, idx);
        erwarte("Autosprite: vier Ecken umgebaut", sp.size() == 4U);
        const float r = std::sqrt(20.0F * 20.0F + 50.0F * 50.0F) * 0.707F;
        bool gut = sp.size() == 4U;
        for (const SpriteEcke& e : sp) {
            gut = gut && e.mitte[0] == 10.0F && e.mitte[1] == 20.0F && e.mitte[2] == 30.0F &&
                  std::fabs(e.radius - r) < 1e-3F;
        }
        erwarte("Mitte des Vierecks, Radius = halbe Diagonale x 0,707", gut);
        erwarte("Richtung und st wie RB_AddQuadStamp",
                sp.size() == 4U && sp[0].links == 1.0F && sp[0].hoch == 1.0F && sp[1].links == -1.0F &&
                    sp[2].hoch == -1.0F && sp[2].st[0] == 1.0F && sp[2].st[1] == 1.0F &&
                    sp[3].st[0] == 0.0F && sp[3].st[1] == 1.0F);
        // Ecken in der Reihenfolge des ersten Auftretens: 0,1,2,3.
        erwarte("Wicklung 0,1,3 / 3,1,2",
                idx.size() == 6U && idx[0] == 0U && idx[1] == 1U && idx[2] == 3U && idx[3] == 3U &&
                    idx[4] == 1U && idx[5] == 2U);

        std::vector<std::uint32_t> idx2 = m.indexes;
        erwarte("ohne Autosprite bleibt alles, wie es ist",
                baueAutosprites(m, {0}, idx2).empty() && idx2 == m.indexes);
        BspMesh ungerade = m;
        ungerade.indexes = {0, 1, 2};
        ungerade.batches[0].numIndexes = 3;
        std::vector<std::uint32_t> idx3 = ungerade.indexes;
        erwarte("ein Abschnitt, der nicht in Vierecke passt, bleibt stehen",
                baueAutosprites(ungerade, {1}, idx3).empty() && idx3 == ungerade.indexes);
    }

    if (fehler != 0) {
        std::printf("FEHLGESCHLAGEN (%d)\n", fehler);
        return 1;
    }
    std::printf("alle Gegenproben bestanden (0 Fehlschlaege)\n");
    return 0;
}
