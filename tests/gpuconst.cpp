// Probe fuer das Packen der Konstanten.
//
// Was hier schiefgehen kann
// -------------------------
// HLSL sortiert alles in Bloecke zu vier float. Ein `float gDeform[4]` war
// die Falle: vier Viererbloecke, aber das letzte Element nur mit .x belegt -
// das naechste Skalar lag in .y dahinter (29), nicht bei 32. Seit rc568 ist
// es ein float4. Wer die Lagen verschiebt, bekommt keinen Absturz, sondern
// ein Bild, in dem etwas falsch wandert oder nur pulsiert.
//
// Auf dem Rechner mit Grafikkarte waere das sehr schwer einzukreisen. Hier
// ist es eine Zahl, die man vergleichen kann.

#include "bhed/gpuconst.h"

#include "bhed/gpushader.h"

#include <cstddef>
#include <cstdio>
#include <string>
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

}  // namespace

int main() {
    std::printf("gpuconst\n");

    // --- Die Lage der Felder ---------------------------------------------
    //
    // Diese Zahlen stehen doppelt: hier und im cbuffer in gpushader.cpp.
    // Genau deshalb werden sie geprueft - eine Verschiebung faellt sonst
    // erst als schiefes Bild auf.
    erwarte("gTexMatrix steht bei 0", kTexMatrixAt == 0U);
    erwarte("gTexOffTurb steht bei 4", kTexOffTurbAt == 4U);
    erwarte("gRgb steht bei 8", kRgbAt == 8U);
    erwarte("gAlphaSchwelle steht bei 12", kAlphaAt == 12U);
    // Ein float4 beginnt auf einer Viererblockgrenze: nach
    // gAlphaSchwelle (12) waere 13 frei, HLSL schiebt auf 16.
    erwarte("gDeform steht bei 16, nicht bei 13", kDeformAt == 16U);
    // float4 gDeform: Spread gleich dahinter. Mit `float gDeform[4]` lag er
    // bei 29 (D3DReflect), gerechnet war 32 - der Lavafall pulsierte nur.
    erwarte("gDeformSpread steht bei 20", kDeformSpreadAt == 20U);
    erwarte("der Block ist 24 float gross", kKonstFloats == 24U);
    erwarte("der Shader hat gDeform als float4",
            vertexShaderHlsl(kDeform, 0).find("float4 gDeform;") != std::string::npos);
    erwarte("und der Shader kennt keine eigene Modellmatrix",
            vertexShaderHlsl(0U, 0).find("gModellWelt") == std::string::npos);

    std::vector<float> buf(kKonstFloats + 8U, -99.0F);

    // --- Alles wird geschrieben, nichts bleibt stehen ---------------------
    //
    // Ein nicht beschriebenes Feld enthielte den Wert des vorigen Stapels -
    // ein Fehler, der nur manchmal auftritt.
    {
        BatchState bs;
        packeKonstanten(bs, buf.data(), 0.0F);
        bool sauber = true;
        for (std::size_t i = 0; i < kKonstFloats; ++i) {
            if (buf[i] == -99.0F) {
                sauber = false;
            }
        }
        erwarte("kein Feld bleibt unbeschrieben", sauber);
        erwarte("und dahinter wird nichts angefasst",
                buf[kKonstFloats] == -99.0F);
    }

    // --- Die tcMod-Kette wird zusammengerechnet --------------------------
    //
    // Nach ComputeTexMods aus rd-rend2: alle Schritte ergeben EINE affine
    // Abbildung, turb steht daneben.
    {
        BatchState bs;
        bs.numTexMods = 2;
        bs.texMods[0].kind = TexModKind::Scroll;
        bs.texMods[0].a[0] = 0.25F;
        bs.texMods[0].a[1] = 0.5F;
        bs.texMods[1].kind = TexModKind::Turb;
        bs.texMods[1].wave.func = WaveFunc::Sin;
        bs.texMods[1].wave.amplitude = 0.3F;
        bs.texMods[1].wave.phase = 1.0F;
        bs.texMods[1].wave.frequency = 0.1F;
        packeKonstanten(bs, buf.data(), 2.0F);

        // scroll ist eine reine Verschiebung, gekuerzt auf [0,1):
        // 0.25*2 = 0.5, 0.5*2 = 1.0 -> 0.0
        erwarte("die Matrix bleibt die Einheitsmatrix",
                buf[kTexMatrixAt + 0] == 1.0F && buf[kTexMatrixAt + 1] == 0.0F &&
                buf[kTexMatrixAt + 2] == 0.0F && buf[kTexMatrixAt + 3] == 1.0F);
        erwarte("scroll steht in der Verschiebung, gekuerzt",
                buf[kTexOffTurbAt + 0] == 0.5F &&
                buf[kTexOffTurbAt + 1] == 0.0F);
        erwarte("turb steht daneben: Amplitude",
                buf[kTexOffTurbAt + 2] == 0.3F);
        // now = phase + zeit * frequenz = 1.0 + 2.0*0.1 = 1.2
        erwarte("turb steht daneben: Phase mit der Zeit",
                buf[kTexOffTurbAt + 3] > 1.19F && buf[kTexOffTurbAt + 3] < 1.21F);
    }
    {
        // DER PUNKT DER GANZEN UMSTELLUNG: tcMod transform.
        //
        // Sechs Werte, die in keinen Viererblock passten - sie standen
        // seit dem ersten Anlauf auf null (13 Vorkommen in 43 Karten,
        // offener Punkt 5 der Uebergabe). Als affine Abbildung passen sie
        // hinein wie jeder andere Schritt.
        BatchState bs;
        bs.numTexMods = 1;
        bs.texMods[0].kind = TexModKind::Transform;
        bs.texMods[0].a[0] = 2.0F;   // m00
        bs.texMods[0].a[1] = 3.0F;   // m01
        bs.texMods[0].a[2] = 4.0F;   // m10
        bs.texMods[0].a[3] = 5.0F;   // m11
        bs.texMods[0].a[4] = 6.0F;   // tx
        bs.texMods[0].a[5] = 7.0F;   // ty
        packeKonstanten(bs, buf.data(), 0.0F);
        erwarte("transform steht jetzt IN der Matrix",
                buf[kTexMatrixAt + 0] == 2.0F && buf[kTexMatrixAt + 1] == 3.0F &&
                buf[kTexMatrixAt + 2] == 4.0F && buf[kTexMatrixAt + 3] == 5.0F);
        erwarte("und seine Verschiebung auch",
                buf[kTexOffTurbAt + 0] == 6.0F &&
                buf[kTexOffTurbAt + 1] == 7.0F);
    }
    {
        // Zwei Schritte hintereinander: erst doppelt so gross, dann
        // verschoben. Die Reihenfolge muss die des Shaderskripts sein.
        BatchState bs;
        bs.numTexMods = 2;
        bs.texMods[0].kind = TexModKind::Scale;
        bs.texMods[0].a[0] = 2.0F;
        bs.texMods[0].a[1] = 2.0F;
        bs.texMods[1].kind = TexModKind::Scroll;
        bs.texMods[1].a[0] = 0.25F;
        bs.texMods[1].a[1] = 0.0F;
        packeKonstanten(bs, buf.data(), 1.0F);
        erwarte("die Skalierung steht in der Matrix",
                buf[kTexMatrixAt + 0] == 2.0F && buf[kTexMatrixAt + 3] == 2.0F);
        erwarte("und das spaetere scroll in der Verschiebung",
                buf[kTexOffTurbAt + 0] == 0.25F);
    }
    {
        // Ohne tcMod bleibt die Einheitsabbildung stehen - sonst
        // verschwaenden die Koordinaten bei jedem Stapel ohne Kette.
        BatchState bs;
        packeKonstanten(bs, buf.data(), 5.0F);
        erwarte("ohne tcMod: Einheitsmatrix",
                buf[kTexMatrixAt + 0] == 1.0F && buf[kTexMatrixAt + 3] == 1.0F);
        erwarte("ohne tcMod: keine Verschiebung",
                buf[kTexOffTurbAt + 0] == 0.0F &&
                buf[kTexOffTurbAt + 1] == 0.0F);
        erwarte("ohne tcMod: kein turb",
                buf[kTexOffTurbAt + 2] == 0.0F);
    }

    // --- rgbGen ------------------------------------------------------------
    {
        BatchState bs;
        packeKonstanten(bs, buf.data(), 0.0F);
        erwarte("ohne rgbGen ist die Farbe weiss",
                buf[kRgbAt] == 1.0F && buf[kRgbAt + 1] == 1.0F);
        erwarte("und alphaConst voll", buf[kRgbAt + 3] == 1.0F);

        bs.haveRgbConst = true;
        bs.rgbConst[0] = 0.25F;
        bs.rgbConst[1] = 0.5F;
        bs.rgbConst[2] = 0.75F;
        packeKonstanten(bs, buf.data(), 0.0F);
        erwarte("rgbGen const kommt an",
                buf[kRgbAt] == 0.25F && buf[kRgbAt + 2] == 0.75F);
    }
    {
        BatchState bs;
        bs.haveRgbWave = true;
        bs.rgbGlow = 0.4F;
        packeKonstanten(bs, buf.data(), 0.0F);
        erwarte("rgbGen wave setzt alle drei Kanaele",
                buf[kRgbAt] == 0.4F && buf[kRgbAt + 1] == 0.4F &&
                    buf[kRgbAt + 2] == 0.4F);
    }

    // --- alphaFunc: dieselbe Umrechnung wie in der Pipeline ----------------
    //
    // Zwei Umrechnungen derselben Schwelle waeren zwei Wahrheiten.
    {
        BatchState bs;
        bs.alphaTest = AlphaTest::Ge128;
        packeKonstanten(bs, buf.data(), 0.0F);
        erwarte("die Schwelle kommt aus derselben Quelle",
                buf[kAlphaAt] == pipelineFor(bs, false).alphaSchwelle);
        erwarte("und ist 128/255",
                buf[kAlphaAt] > 0.501F && buf[kAlphaAt] < 0.503F);
    }

    // --- deformVertexes: ein float4, dicht gepackt --------------------------
    {
        BatchState bs;
        bs.deformWave.func = WaveFunc::Sin;
        bs.deformWave.base = 1.0F;
        bs.deformWave.amplitude = 2.0F;
        bs.deformWave.phase = 3.0F;
        bs.deformWave.frequency = 4.0F;
        bs.deformSpread = 0.4F;
        packeKonstanten(bs, buf.data(), 0.0F);
        erwarte("base bei +0", buf[kDeformAt + 0] == 1.0F);
        erwarte("amplitude bei +1", buf[kDeformAt + 1] == 2.0F);
        erwarte("phase bei +2", buf[kDeformAt + 2] == 3.0F);
        erwarte("frequency bei +3", buf[kDeformAt + 3] == 4.0F);
        erwarte("spread dahinter bei 20", buf[kDeformSpreadAt] == 0.4F);
    }
    {
        // Ohne Welle darf nichts stehenbleiben.
        BatchState bs;
        bs.deformSpread = 0.4F;
        packeKonstanten(bs, buf.data(), 0.0F);
        erwarte("ohne deformVertexes bleibt alles null",
                buf[kDeformAt] == 0.0F && buf[kDeformSpreadAt] == 0.0F);
    }

    // --- Ein Nullzeiger stuerzt nicht ab -----------------------------------
    {
        BatchState bs;
        packeKonstanten(bs, nullptr, 0.0F);
        erwarte("nullptr wird abgefangen", true);
    }

    // --- Der Shader und dieser Packer muessen denselben Block meinen ------
    //
    // Sie stehen in zwei Dateien. Diese Probe haelt sie zusammen: was hier
    // gepackt wird, muss im erzeugten HLSL auch vorkommen - sonst schreibt
    // der eine in ein Feld, das der andere nicht liest.
    {
        const std::string hlsl = vertexShaderHlsl(kTexMod | kDeform, 1);
        erwarte("der Shader kennt gTexMatrix",
                hlsl.find("gTexMatrix") != std::string::npos);
        erwarte("der Shader kennt gTexOffTurb",
                hlsl.find("gTexOffTurb") != std::string::npos);
        erwarte("der Shader kennt gDeform", hlsl.find("gDeform[") != std::string::npos);
        erwarte("der Shader kennt gDeformSpread",
                hlsl.find("gDeformSpread") != std::string::npos);
        const std::string ps = pixelShaderHlsl(kAlphaTest);
        erwarte("der Pixel-Shader kennt gAlphaSchwelle",
                ps.find("gAlphaSchwelle") != std::string::npos);
        erwarte("und gRgb", ps.find("gRgb") != std::string::npos);
    }

    // --- Bei rgbGen wave UND const geht die Welle vor --------------------
    //
    // mapview.cpp:1833: "`wave` geht vor: steht beides in einer Stufe ..."
    // Der Packer liess bis rc439 den const-Wert vorgehen.
    {
        BatchState bs{};
        bs.haveRgbWave = true;
        bs.rgbGlow = 0.8F;
        bs.haveRgbConst = true;
        bs.rgbConst[0] = 0.1F;
        bs.rgbConst[1] = 0.2F;
        bs.rgbConst[2] = 0.3F;
        std::vector<float> f(kKonstFloats, -1.0F);
        packeKonstanten(bs, f.data(), 0.0F);
        erwarte("die Welle gewinnt gegen const - rot",
                f[kRgbAt + 0] > 0.79F && f[kRgbAt + 0] < 0.81F);
        erwarte("die Welle gewinnt gegen const - gruen",
                f[kRgbAt + 1] > 0.79F && f[kRgbAt + 1] < 0.81F);
    }
    {
        // Ohne Welle gilt const weiter.
        BatchState bs{};
        bs.haveRgbConst = true;
        bs.rgbConst[0] = 0.25F;
        std::vector<float> f(kKonstFloats, -1.0F);
        packeKonstanten(bs, f.data(), 0.0F);
        erwarte("ohne Welle steht der const-Wert da",
                f[kRgbAt + 0] > 0.24F && f[kRgbAt + 0] < 0.26F);
    }

    if (fehler != 0) {
        std::printf("FEHLGESCHLAGEN (%d)\n", fehler);
        return 1;
    }
    std::printf("alle Gegenproben bestanden (0 Fehlschlaege)\n");
    return 0;
}
