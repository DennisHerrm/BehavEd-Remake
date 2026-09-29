// Probe fuer die Uebersetzung Shaderzustand -> Zustand der Grafikkarte.
//
// Warum das hier steht
// --------------------
// Der eigentliche Direct3D-Teil laesst sich auf einem Rechner ohne Windows
// weder uebersetzen noch pruefen. Die UEBERSETZUNG des Zustands dagegen ist
// reine Datenverarbeitung - und genau dort sitzen die Fehler, die man spaeter
// im Bild sucht und nicht findet.
//
// Deshalb ist sie aus dem Backend herausgehalten (src/gpustate.cpp) und wird
// hier geprueft. Alles, was hier abgesichert ist, muss nicht am fremden
// Rechner debuggt werden.

#include "bhed/gpustate.h"

#include <cstdio>

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

BatchState leer() {
    BatchState bs;
    return bs;
}

}  // namespace

int main() {
    std::printf("gpustate\n");

    // --- Die Mischformel kommt aus den FAKTOREN, nicht aus der Einteilung -
    //
    // `BlendMode` ist eine Zusammenfassung; die Wahrheit stehen srcFactor
    // und dstFactor. Ein Stapel, der zufaellig unter Alpha einsortiert ist,
    // aber additive Faktoren hat, muss additiv herauskommen.
    {
        BatchState bs = leer();
        bs.blend = BlendMode::Alpha;         // die grobe Einteilung luegt
        bs.srcFactor = BlendFactor::One;     // die Faktoren sagen additiv
        bs.dstFactor = BlendFactor::One;
        erwarte("Faktoren schlagen die Einteilung",
                pipelineFor(bs, false).blend == Blend::Add);
    }
    {
        BatchState bs = leer();
        bs.srcFactor = BlendFactor::One;
        bs.dstFactor = BlendFactor::Zero;
        erwarte("ONE/ZERO ist deckend",
                pipelineFor(bs, false).blend == Blend::Opaque);
    }
    {
        BatchState bs = leer();
        bs.srcFactor = BlendFactor::DstColor;
        bs.dstFactor = BlendFactor::Zero;
        // Die ABBILDUNG der Faktoren wird an der ZUSATZSTUFE geprueft.
        //
        // Bei der Grundstufe ist Filter seit rc437 ausdruecklich deckend -
        // dort ist das Multiplizieren schon im Shader erledigt, und ein
        // zweites Mal multiplizierte gegen den leeren Bildspeicher. Das
        // wird zwei Bloecke weiter unten eigens geprueft; hier geht es um
        // die Faktoren selbst.
        erwarte("DST_COLOR/ZERO ist filter",
                pipelineFor(bs, false, false, true).blend == Blend::Filter);
    }
    {
        BatchState bs = leer();
        bs.srcFactor = BlendFactor::SrcAlpha;
        bs.dstFactor = BlendFactor::One;
        erwarte("SRC_ALPHA/ONE ist additiv-alpha",
                pipelineFor(bs, false).blend == Blend::AddAlpha);
    }

    // Ein Paar, das nirgends abgebildet ist, darf NICHT stillschweigend als
    // deckend durchgehen. Ein falsch gemischter Stapel faellt im Bild auf;
    // einer, der als deckend durchrutscht, oft erst viel spaeter.
    {
        BatchState bs = leer();
        bs.srcFactor = BlendFactor::DstAlpha;
        bs.dstFactor = BlendFactor::OneMinusDstAlpha;
        // DstAlpha/OneMinusDstAlpha kommt in den 43 Karten nicht vor -
        // gemessen. Wenn es eines Tages auftaucht, soll es auffallen.
        // Auch hier an der Zusatzstufe: die Grundstufe faellt auf deckend
        // zurueck, wie im Rasterer. Die Frage, um die es geht - erkennt
        // `ausFaktoren` das Paar? - stellt sich unabhaengig davon.
        erwarte("unbekanntes Paar bleibt UNBEKANNT",
                pipelineFor(bs, false, false, true).blend == Blend::Unbekannt);
    }

    // --- Die Merkmalsbits ------------------------------------------------
    {
        BatchState bs = leer();
        erwarte("nichts gesetzt heisst keine Bits",
                pipelineFor(bs, false).features == kNichts);
        erwarte("Lightmap kommt vom Stapel, nicht vom Zustand",
                (pipelineFor(bs, true).features & kLightmap) != 0U);
    }
    {
        BatchState bs = leer();
        bs.numTexMods = 2;
        const PipelineState p = pipelineFor(bs, false);
        erwarte("tcMod setzt sein Bit", (p.features & kTexMod) != 0U);
        erwarte("und reicht die Anzahl durch", p.numTexMods == 2);
    }
    {
        BatchState bs = leer();
        bs.cull = CullMode::Two;
        erwarte("cull twosided",
                (pipelineFor(bs, false).features & kCullNone) != 0U);
        bs.cull = CullMode::Back;
        erwarte("cull back ist ein ANDERES Bit",
                (pipelineFor(bs, false).features & kCullBack) != 0U);
        bs.cull = CullMode::Front;
        erwarte("cull front setzt keins",
                (pipelineFor(bs, false).features &
                 (kCullNone | kCullBack)) == 0U);
    }
    {
        // deformVertexes gilt fuer den ganzen Shader - der Fehler aus rc383
        // war, es je Stufe zu behandeln. Hier gehoert es genau einmal hin.
        BatchState bs = leer();
        // active() haengt an der FUNKTION, nicht an der Amplitude
        // (wave.h:49) - eine Welle ohne func ist keine Welle.
        bs.deformWave.func = WaveFunc::Sin;
        bs.deformWave.amplitude = 5.0F;
        bs.deformWave.frequency = 1.0F;
        erwarte("deformVertexes setzt sein Bit",
                (pipelineFor(bs, false).features & kDeform) != 0U);
    }

    // --- alphaFunc: Bit UND Schwelle -------------------------------------
    //
    // Ohne alphaFunc darf KEINE Schwelle wirken. Das war der Fehler aus
    // rc385: der Test lief ohne Bedingung, und ueber die Haelfte jeder Figur
    // fiel weg.
    {
        BatchState bs = leer();
        const PipelineState p = pipelineFor(bs, false);
        erwarte("ohne alphaFunc kein Bit", (p.features & kAlphaTest) == 0U);
        erwarte("und keine Schwelle", p.alphaSchwelle == 0.0F);
    }
    {
        BatchState bs = leer();
        bs.alphaTest = AlphaTest::Ge128;
        const PipelineState p = pipelineFor(bs, false);
        erwarte("GE128 setzt das Bit", (p.features & kAlphaTest) != 0U);
        erwarte("GE128 ist 128/255",
                p.alphaSchwelle > 0.501F && p.alphaSchwelle < 0.503F);
    }
    {
        BatchState bs = leer();
        bs.alphaTest = AlphaTest::Ge192;
        erwarte("GE192 ist hoeher als GE128",
                pipelineFor(bs, false).alphaSchwelle > 0.75F);
    }

    // --- Tiefenverhalten -------------------------------------------------
    {
        BatchState bs = leer();
        bs.depthFunc = DepthFunc::Equal;
        const PipelineState p = pipelineFor(bs, false);
        erwarte("depthFunc equal", p.depthTestEqual && !p.depthTestOff);
        bs.depthFunc = DepthFunc::Disable;
        const PipelineState q = pipelineFor(bs, false);
        erwarte("depthFunc disable", q.depthTestOff && !q.depthTestEqual);
    }

    // --- Gleichheit: gleicher Zustand, ein Zeichenaufruf ------------------
    //
    // Das ist die Zahl, um die es beim Umbau geht. Gemessen kommen je Bild
    // nie mehr als 24 verschiedene Zustaende vor - aber nur, wenn gleiche
    // Zustaende auch als gleich erkannt werden.
    {
        BatchState a = leer();
        BatchState b = leer();
        b.numTexMods = 1;
        erwarte("gleicher Zustand ist gleich",
                pipelineFor(a, false).gleichWie(pipelineFor(a, false)));
        erwarte("verschiedener Zustand ist verschieden",
                !pipelineFor(a, false).gleichWie(pipelineFor(b, false)));
        erwarte("die Lightmap allein macht schon einen Unterschied",
                !pipelineFor(a, false).gleichWie(pipelineFor(a, true)));
    }

    // --- Die Grundstufe ist deckend, die Zusatzstufe nicht ---------------
    //
    // Aus textures/plasma_mustafar/screen_5 der echten Karte:
    //
    //     { map $lightmap }
    //     { map .../screen_5      blendFunc GL_DST_COLOR GL_ZERO }
    //     { map .../screen_5_glow blendFunc GL_ONE GL_ONE  glow }
    //
    // Die ersten beiden fasst behaved zu einem Aufruf zusammen. Bliebe der
    // `Filter`, multiplizierte er gegen den leeren Bildspeicher - die
    // Bildschirme und Lampen waren deshalb schwarz. 37 Shader der Mission
    // sind so gebaut.
    {
        BatchState f{};
        f.srcFactor = BlendFactor::DstColor;
        f.dstFactor = BlendFactor::Zero;
        erwarte("Filter wird bei der Grundstufe deckend",
                pipelineFor(f, true, false, false).blend == Blend::Opaque);
        erwarte("Filter bleibt bei der Zusatzstufe Filter",
                pipelineFor(f, true, false, true).blend == Blend::Filter);

        // Was ausdruecklich mischt, bleibt in BEIDEN Faellen.
        BatchState a{};
        a.srcFactor = BlendFactor::One;
        a.dstFactor = BlendFactor::One;
        erwarte("additiv bleibt additiv - Grundstufe",
                pipelineFor(a, true, false, false).blend == Blend::Add);
        erwarte("additiv bleibt additiv - Zusatzstufe",
                pipelineFor(a, true, false, true).blend == Blend::Add);

        BatchState al{};
        al.srcFactor = BlendFactor::SrcAlpha;
        al.dstFactor = BlendFactor::OneMinusSrcAlpha;
        erwarte("Alpha bleibt Alpha bei der Grundstufe",
                pipelineFor(al, true, false, false).blend == Blend::AlphaBlend);

        BatchState o{};
        o.srcFactor = BlendFactor::One;
        o.dstFactor = BlendFactor::Zero;
        erwarte("deckend bleibt deckend",
                pipelineFor(o, true, false, false).blend == Blend::Opaque);
    }

    // --- Der lange Schwanz: die Faktoren bleiben erhalten ----------------
    //
    // Aus textures/plasma_mustafar/lava, zweite Stufe:
    //     blendFunc GL_DST_COLOR GL_SRC_ALPHA
    // `Blend` hat dafuer keine Schublade - der GPU-Weg zeichnete es deckend
    // und ueberdeckte damit die leuchtende erste Stufe. Direct3D kann das
    // Paar unmittelbar; dafuer muessen die Faktoren durchgereicht werden.
    {
        BatchState lava{};
        lava.srcFactor = BlendFactor::DstColor;
        lava.dstFactor = BlendFactor::SrcAlpha;
        const PipelineState p = pipelineFor(lava, false, false, true);
        erwarte("das Paar bleibt UNBEKANNT - fuer die Diagnose",
                p.blend == Blend::Unbekannt);
        erwarte("aber die Faktoren sind da: Quelle",
                p.srcFactor == BlendFactor::DstColor);
        erwarte("und das Ziel",
                p.dstFactor == BlendFactor::SrcAlpha);

        // Zwei verschiedene unbekannte Paare sind zwei VERSCHIEDENE
        // Zustaende - sonst teilten sie sich einen Mischzustand.
        BatchState anders{};
        anders.srcFactor = BlendFactor::DstAlpha;
        anders.dstFactor = BlendFactor::OneMinusDstAlpha;
        erwarte("zwei unbekannte Paare sind nicht derselbe Zustand",
                !p.gleichWie(pipelineFor(anders, false, false, true)));
    }
    {
        // Faellt die Grundstufe auf deckend zurueck, muessen die Faktoren
        // mitkommen - sonst stellte der Mischzustand noch das Paar der
        // Vorlage ein.
        BatchState lava{};
        lava.srcFactor = BlendFactor::DstColor;
        lava.dstFactor = BlendFactor::SrcAlpha;
        const PipelineState g = pipelineFor(lava, false, false, false);
        erwarte("die Grundstufe wird deckend", g.blend == Blend::Opaque);
        erwarte("und ihre Faktoren auch - Quelle",
                g.srcFactor == BlendFactor::One);
        erwarte("und ihre Faktoren auch - Ziel",
                g.dstFactor == BlendFactor::Zero);
    }

    if (fehler != 0) {
        std::printf("FEHLGESCHLAGEN (%d)\n", fehler);
        return 1;
    }
    std::printf("alle Gegenproben bestanden (0 Fehlschlaege)\n");
    return 0;
}
