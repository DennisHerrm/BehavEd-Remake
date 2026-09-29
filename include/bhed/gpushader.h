// gpushader.h - Shaderquelltext aus den Merkmalsbits erzeugen
//
// Warum erzeugen statt schreiben
// ------------------------------
// Ein fester Shader je Kombination waere bei zwoelf Merkmalsbits eine
// unuebersehbare Zahl von Dateien. Ein einziger Shader mit allen Zweigen
// waere langsam und wuerde die Bits nur verstecken.
//
// Der eigentliche Grund ist aber ein anderer: Shaderquelltext laesst sich
// auf diesem Rechner weder uebersetzen noch ausfuehren - es gibt keine
// Grafikkarte und kein Direct3D. Erzeugt man ihn dagegen aus den Bits, dann
// ist die ERZEUGUNG gewoehnlicher C++-Code, und den kann man pruefen:
//
//   - hat jedes gesetzte Bit auch eine Zeile im Quelltext?
//   - steht in einem Shader OHNE das Bit die Zeile wirklich nicht drin?
//   - erzeugen gleiche Bits denselben Quelltext (sonst kein Wiederverwenden)?
//
// Das faengt nicht jeden Fehler - ein Tippfehler im HLSL faellt erst beim
// Uebersetzen auf. Es faengt aber die Klasse von Fehlern, die in diesem
// Projekt am haeufigsten war: ein Merkmal, das irgendwo vergessen wurde.
// Genau so entstanden rc383 (deformVertexes nur im Basiseintrag) und rc385
// (Alphatest ohne Bedingung).
//
// Sprache
// -------
// Erzeugt wird HLSL fuer Direct3D 11. Der OpenGL-Weg behaelt vorerst den
// vorhandenen Rasterer; zwei Sprachen gleichzeitig einzufuehren waere
// doppelte Angriffsflaeche, und der D3D-Weg ist der, den shanks Rechner
// benutzt (siehe Protokoll: "Grafikschnittstelle starten: Direct3D 11").
#ifndef BHED_GPUSHADER_H
#define BHED_GPUSHADER_H

#include <cstdint>
#include <string>

namespace bhed::gpu {

// Der Vertex-Shader fuer diese Merkmalskombination.
//
// `features` ist das Bitfeld aus PipelineState (siehe gpustate.h).
// `numTexMods` begrenzt die tcMod-Schleife; 0 heisst: keine.
[[nodiscard]] std::string vertexShaderHlsl(std::uint32_t features,
                                           int numTexMods);

// Der Vertex-Shader fuer FIGUREN.
//
// Eigener Shader und keine Merkmalsvariante des Karten-Shaders: die Eingabe
// ist eine andere (Knochen statt Lightmap), und die Verformung passiert VOR
// der Projektion. Beides in einen Shader zu zwingen hiesse, in jedem
// Kartenstapel einen ungenutzten Knochenzweig mitzuschleppen.
[[nodiscard]] std::string figurVertexShaderHlsl(std::uint32_t features);

// Der Pixel-Shader fuer dieselbe Kombination.
[[nodiscard]] std::string pixelShaderHlsl(std::uint32_t features);

// --- Die Durchgaenge, die nur Bildpunkte verarbeiten -------------------
//
// Ein Vollbild-Dreieck als Vertex-Shader, dazu zwei Pixel-Shader fuer das
// Weichzeichnen und das Auflegen des Gluehens. Beide sind woertlich aus
// src/glow.cpp uebernommen - zwei Fassungen derselben Formel waeren zwei
// verschieden aussehende Gluehbilder.
[[nodiscard]] std::string vollbildVertexShaderHlsl();
// Die Klingen der Lichtschwerter: fertige Weltecken, Textur oder Farbe.
[[nodiscard]] std::string klingeVertexShaderHlsl();
[[nodiscard]] std::string klingePixelShaderHlsl();
// Der Verlauf hinter dem Modellfenster (zu vollbildVertexShaderHlsl).
[[nodiscard]] std::string modellHintergrundPixelShaderHlsl();
[[nodiscard]] std::string glowShrinkPixelShaderHlsl();
[[nodiscard]] std::string glowBlurPixelShaderHlsl();
[[nodiscard]] std::string glowCompositePixelShaderHlsl();

// Ein kurzer Name fuer diese Kombination - fuer Protokolle und um erzeugte
// Shader wiederzuerkennen.
[[nodiscard]] std::string shaderName(std::uint32_t features, int numTexMods);

}  // namespace bhed::gpu

#endif  // BHED_GPUSHADER_H
