// gpustate.cpp - Uebersetzung Shaderzustand -> Zustand der Grafikkarte
//
// Siehe bhed/gpustate.h. Diese Datei haengt bewusst von nichts ab, was
// Windows oder Direct3D braucht: sie laesst sich ueberall uebersetzen und
// pruefen. Der Teil, der das nicht kann, soll so klein wie moeglich bleiben.

#include "bhed/gpustate.h"

#include <cmath>

namespace bhed::gpu {

namespace {

// Die Mischformel aus den beiden Faktoren.
//
// Warum nicht ueber BatchState::blend?
// ------------------------------------
// `BlendMode` ist eine Zusammenfassung (Opaque, Add, Alpha, Filter,
// AddAlpha), aber die WAHRHEIT stehen die beiden Faktoren. Ein Shader mit
// `blendFunc GL_ONE GL_ONE` und einer, der ueber `blendFunc add` dasselbe
// sagt, muessen hier gleich herauskommen - und umgekehrt darf eine
// Kombination, die zufaellig unter Alpha einsortiert wurde, aber andere
// Faktoren hat, NICHT als Alpha durchgehen.
//
// Deshalb wird hier auf die Faktoren geschaut und die Zusammenfassung nur
// als Rueckfall benutzt.
Blend ausFaktoren(BlendFactor src, BlendFactor dst, BlendMode grob) {
    if (src == BlendFactor::One && dst == BlendFactor::Zero) {
        return Blend::Opaque;
    }
    if (src == BlendFactor::One && dst == BlendFactor::One) {
        return Blend::Add;
    }
    if (src == BlendFactor::SrcAlpha &&
        dst == BlendFactor::OneMinusSrcAlpha) {
        return Blend::AlphaBlend;
    }
    if (src == BlendFactor::DstColor && dst == BlendFactor::Zero) {
        return Blend::Filter;
    }
    if (src == BlendFactor::SrcAlpha && dst == BlendFactor::One) {
        return Blend::AddAlpha;
    }
    // Die zwei Nachzuegler aus der Messung - siehe gpustate.h.
    if (src == BlendFactor::Zero && dst == BlendFactor::OneMinusSrcColor) {
        return Blend::InvSrcColor;
    }
    if (src == BlendFactor::One && dst == BlendFactor::SrcAlpha) {
        return Blend::OneSrcAlpha;
    }
    // Kein bekanntes Paar - und hier stand beinahe ein Rueckfall auf die
    // grobe Einteilung.
    //
    // Das waere falsch gewesen. `BlendMode` kennt nur fuenf Schubladen und
    // hat keinen Wert fuer "weiss nicht"; jedes fremde Paar waere also
    // stillschweigend in einer davon gelandet. In shaderscript.h steht es
    // ausdruecklich: allein auf Zusatzstufen kommen 14 verschiedene
    // KOMBINATIONEN vor, "ein langer Schwanz, den man nicht mit fuenf
    // Schubladen erwischt".
    //
    // Ein Stapel, der falsch gemischt wird, faellt im Bild auf. Einer, der
    // als deckend durchrutscht, oft erst viel spaeter. Deshalb UNBEKANNT -
    // der Aufrufer soll entscheiden, nicht diese Funktion raten.
    (void)grob;
    return Blend::Unbekannt;
}

// alphaFunc in eine Schwelle von 0 bis 1.
//
// Die Engine testet GEGEN den gefilterten Alphawert (tr_shader.cpp:290ff).
// GE128 heisst "behalte, was >= 128 ist", also Schwelle 128/255.
float schwelleFuer(AlphaTest a) {
    switch (a) {
        case AlphaTest::Gt0:   return 1.0F / 255.0F;
        case AlphaTest::Ge128: return 128.0F / 255.0F;
        case AlphaTest::Ge192: return 192.0F / 255.0F;
        // LT128 verwirft, was DARUEBER liegt - eine andere Richtung, keine
        // andere Schwelle. Kommt in den 43 Karten nicht vor; wer sie
        // einbaut, braucht ein eigenes Bit, nicht diesen Wert.
        case AlphaTest::Lt128: return 0.0F;
        case AlphaTest::None:  return 0.0F;
    }
    return 0.0F;
}

}  // namespace

Licht lichtFuer(const BatchState& bs, int lightmap, bool vertexColour,
                bool zusatzstufe) {
    Licht l;
    if (vertexColour) {
        l.roh = true;
        return l;
    }
    if (!bs.ausSkript) {
        l.lightmap = (lightmap >= 0);
        return l;
    }
    if (!zusatzstufe && bs.lightmapStufe) {
        if (lightmap >= 0) {
            l.lightmap = true;
            return l;
        }
        if (lightmap == -3) {
            return l;   // exactVertex: die Eckenfarbe
        }
        // -1/-2: $lightmap wird tr.whiteImage - bleibt die feste Farbe.
    }
    if (bs.rgbVertex) {
        return l;
    }
    l.fest = true;
    return l;
}

PipelineState pipelineFor(const BatchState& bs, bool hatLightmap,
                          bool rohEcke, bool zusatzstufe, bool festeFarbe) {
    PipelineState p;

    p.blend = ausFaktoren(bs.srcFactor, bs.dstFactor, bs.blend);
    p.srcFactor = bs.srcFactor;
    p.dstFactor = bs.dstFactor;
    // --- Die GRUNDSTUFE ist deckend, solange sie nicht ausdruecklich
    //     mischt ---------------------------------------------------------
    //
    // Nachgelesen in textures/plasma_mustafar/screen_5 (der Karte selbst):
    //
    //     { map $lightmap }
    //     { map .../screen_5      blendFunc GL_DST_COLOR GL_ZERO }
    //     { map .../screen_5_glow blendFunc GL_ONE GL_ONE  glow }
    //
    // Die ersten beiden Stufen sind die Redewendung "Lightmap, dann Textur
    // multiplizieren". behaved fasst sie zu einem Aufruf zusammen und
    // rechnet die Lightmap im Shader - das Multiplizieren ist damit getan.
    // Blieb der Aufruf `Filter`, multiplizierte er ein zweites Mal, diesmal
    // gegen den leeren Bildspeicher: schwarz.
    //
    // Genau das waren die dunklen Bildschirme und Lampen. 37 Shader dieser
    // Mission sind so gebaut.
    //
    // Der Rasterer trifft dieselbe Unterscheidung (mapview.cpp:1472).
    if (!zusatzstufe && p.blend != Blend::Add && p.blend != Blend::AlphaBlend &&
        p.blend != Blend::AddAlpha) {
        p.blend = Blend::Opaque;
    }
    if (p.blend == Blend::Opaque) {
        // Nach dem Zurueckfallen muessen die Faktoren dazu passen, sonst
        // stellte `holeBlend` fuer eine deckende Flaeche noch das
        // Faktorenpaar der Vorlage ein.
        p.srcFactor = BlendFactor::One;
        p.dstFactor = BlendFactor::Zero;
    }

    p.depthWrite = bs.depthWrite;
    p.depthTestEqual = (bs.depthFunc == DepthFunc::Equal);
    p.depthTestOff = (bs.depthFunc == DepthFunc::Disable);

    std::uint32_t f = kNichts;
    if (hatLightmap) {
        f |= kLightmap;
    }
    if (rohEcke) {
        f |= kRohEcke;
    }
    // `alphaConst >= 0` heisst: es stand ein `alphaGen const` da. Der
    // Rasterer prueft genauso (`p.alphaKonst >= 0.0F`), und er prueft es
    // ZUERST - const geht vor identity.
    if (bs.alphaConst >= 0.0F) {
        f |= kAlphaKonst;
    } else if (bs.alphaGen == AlphaGen::Identity) {
        f |= kAlphaEins;
    }
    if (bs.numTexMods > 0) {
        f |= kTexMod;
    }
    if (bs.haveRgbWave) {
        f |= kRgbWave;
    }
    if (bs.haveRgbConst) {
        f |= kRgbConst;
    }
    // Feste Farbe: aus gRgb (1,1,1 ohne rgbGen, siehe packeKonstanten),
    // ohne Helligkeitsfaktor - dafuer steht kRohEcke im Pixelshader.
    if (festeFarbe) {
        f |= kRohEcke;
        if (!bs.haveRgbWave) {
            f |= kRgbConst;
        }
    }
    if (bs.haveSpecular) {
        f |= kSpecular;
    }
    if (bs.texGen != TexGen::Base) {
        f |= kTexGenEnv;
    }
    if (bs.alphaTest != AlphaTest::None) {
        f |= kAlphaTest;
    }
    if (bs.polygonOffset) {
        f |= kPolygonOffset;
    }
    if (bs.glow) {
        f |= kGlow;
    }
    // Ein Himmelswuerfel liegt nur vor, wenn genau sechs Seiten da sind -
    // das prueft batchStateFor. Hier reicht der Zeiger als Frage.
    if (bs.skyFaces != nullptr) {
        f |= kSky;
    }
    if (bs.autosprite) {
        f |= kAutosprite;
    }
    // deformVertexes gilt fuer den GANZEN Shader, nicht je Stufe - das war
    // der Fehler aus rc383. Hier steht es deshalb einmal, nicht je Stufe.
    if (bs.deformWave.active()) {
        f |= kDeform;
    }
    switch (bs.cull) {
        // "Two" heisst zweiseitig - die Engine nennt es cull disable oder
        // cull twosided, gemeint ist dasselbe: nichts weglassen.
        case CullMode::Two: f |= kCullNone; break;
        case CullMode::Back: f |= kCullBack; break;
        case CullMode::Front: break;
    }
    p.features = f;

    p.numTexMods = bs.numTexMods;
    p.alphaSchwelle = schwelleFuer(bs.alphaTest);
    return p;
}

namespace {
// Absichtlich ein roher Zeiger auf eine Zeichenkettenkonstante: er wird aus
// einem Absturzbehandler gelesen, und dort darf nichts mehr belegt werden.
const char* g_schritt = "";
}  // namespace

void setzeSchritt(const char* was) { g_schritt = (was != nullptr) ? was : ""; }
const char* letzterSchritt() { return g_schritt; }

const char* blendName(Blend b) {
    switch (b) {
        case Blend::Opaque:     return "deckend";
        case Blend::Add:        return "additiv";
        case Blend::AlphaBlend: return "alpha";
        case Blend::Filter:     return "filter";
        case Blend::AddAlpha:   return "additiv-alpha";
        case Blend::InvSrcColor: return "invers-quellfarbe";
        case Blend::OneSrcAlpha: return "eins-quellalpha";
        case Blend::Unbekannt:  return "UNBEKANNT";
    }
    return "?";
}

}  // namespace bhed::gpu
