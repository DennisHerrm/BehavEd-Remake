// Probe: uebersetzt jeder erzeugte Shader wirklich? (nur Windows/MSVC)
//
// tests/gpushader.cpp prueft, ob ein Merkmal im Quelltext steht - nicht, ob
// der Quelltext uebersetzt. Ein Tippfehler im HLSL fiel bisher erst zur
// Laufzeit auf, und dann nur fuer die Variante, die eine Karte gerade
// braucht (rc592: deformVertexes normal steht in keiner Karte mit
// tcGen environment, bulge und move in gar keiner).
//
// Hier wird jedes Merkmal einzeln, alle neuen Verformungen zusammen und
// jeder feste Shader mit D3DCompile uebersetzt - dieselben Profile wie
// gpumap_win32.cpp (vs_4_0, ps_4_0, gs_4_0).

#include "bhed/gpushader.h"
#include "bhed/gpustate.h"

#include <windows.h>
#include <d3dcompiler.h>

#include <cstdint>
#include <cstdio>
#include <string>

using namespace bhed::gpu;

namespace {

int fehler = 0;
int gut = 0;

void uebersetze(const std::string& was, const std::string& src, const char* profil) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    const HRESULT hr = D3DCompile(src.data(), src.size(), was.c_str(), nullptr, nullptr, "main",
                                  profil, 0, 0, &blob, &err);
    if (FAILED(hr)) {
        ++fehler;
        std::printf("  FEHL  %s (%s)\n", was.c_str(), profil);
        if (err != nullptr) {
            std::printf("%s\n", static_cast<const char*>(err->GetBufferPointer()));
        }
    } else {
        ++gut;
    }
    if (blob != nullptr) { blob->Release(); }
    if (err != nullptr) { err->Release(); }
}

}  // namespace

int main() {
    std::printf("hlslprobe\n");
    // Jedes Merkmal einzeln - bis zum hoechsten vergebenen Bit.
    for (int b = 0; b <= 21; ++b) {
        const std::uint32_t f = 1U << b;
        const std::string n = "bit" + std::to_string(b);
        uebersetze(n + " vs", vertexShaderHlsl(f, 1), "vs_4_0");
        uebersetze(n + " ps", pixelShaderHlsl(f), "ps_4_0");
        uebersetze(n + " figur", figurVertexShaderHlsl(f), "vs_4_0");
    }
    // Zusammen, wie sie auf einer Flaeche stehen koennen.
    const std::uint32_t alle = kDeform | kDeformNormal | kBulge | kDeformMove | kAutosprite2 |
                               kTexGenEnv | kTexMod | kLightmap;
    uebersetze("Verformungen zusammen vs", vertexShaderHlsl(alle, 2), "vs_4_0");
    uebersetze("oneMinusVertex ps", pixelShaderHlsl(kAlphaKonst | kAlphaEins), "ps_4_0");
    uebersetze("ohne Merkmal vs", vertexShaderHlsl(kNichts, 0), "vs_4_0");
    uebersetze("ohne Merkmal ps", pixelShaderHlsl(kNichts), "ps_4_0");
    // Die festen Shader.
    uebersetze("vollbild", vollbildVertexShaderHlsl(), "vs_4_0");
    uebersetze("klinge vs", klingeVertexShaderHlsl(), "vs_4_0");
    uebersetze("klinge ps", klingePixelShaderHlsl(), "ps_4_0");
    uebersetze("licht vs", lichtVertexShaderHlsl(), "vs_4_0");
    uebersetze("licht ps", lichtPixelShaderHlsl(), "ps_4_0");
    uebersetze("schatten ps", schattenPixelShaderHlsl(), "ps_4_0");
    uebersetze("schattenvolumen vs", schattenVolumenVsHlsl(), "vs_4_0");
    uebersetze("schattenvolumen gs", schattenVolumenGsHlsl(), "gs_4_0");
    uebersetze("schattenflach vs", schattenFlachVsHlsl(), "vs_4_0");
    uebersetze("schattenschwarz ps", schattenSchwarzPsHlsl(), "ps_4_0");
    uebersetze("schattenabdunkeln ps", schattenAbdunkelnPsHlsl(), "ps_4_0");
    uebersetze("nebel vs", nebelVertexShaderHlsl(), "vs_4_0");
    uebersetze("nebelfigur vs", nebelFigurVertexShaderHlsl(), "vs_4_0");
    uebersetze("nebel ps", nebelPixelShaderHlsl(), "ps_4_0");
    uebersetze("modellhintergrund ps", modellHintergrundPixelShaderHlsl(), "ps_4_0");
    uebersetze("glow shrink ps", glowShrinkPixelShaderHlsl(), "ps_4_0");
    uebersetze("glow blur ps", glowBlurPixelShaderHlsl(), "ps_4_0");
    uebersetze("glow composite ps", glowCompositePixelShaderHlsl(), "ps_4_0");
    if (fehler != 0) {
        std::printf("FEHLGESCHLAGEN (%d von %d)\n", fehler, fehler + gut);
        return 1;
    }
    std::printf("alle %d Shader uebersetzt (0 Fehlschlaege)\n", gut);
    return 0;
}
