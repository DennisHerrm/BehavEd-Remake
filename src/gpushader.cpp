// gpushader.cpp - HLSL aus den Merkmalsbits
//
// Siehe bhed/gpushader.h. Diese Datei braucht kein Direct3D: sie baut
// Zeichenketten. Geprueft wird sie in tests/gpushader.cpp.

#include "bhed/gpushader.h"

#include <string>

#include "bhed/gpustate.h"

namespace bhed::gpu {

namespace {

// Die Konstanten, die jeder Shader bekommt.
//
// Reihenfolge und Groesse muessen zum Puffer im Backend passen. Deshalb
// steht der Block hier EINMAL und nicht je Variante - sonst laufen die
// Varianten auseinander, und das faellt erst als verschobenes Bild auf.
const char* kKonstanten = R"(
cbuffer JeBild : register(b0) {
    // row_major ist KEIN Schoenheitsfehler.
    //
    // Direct3D packt Matrizen in Konstantenpuffern standardmaessig
    // SPALTENweise. Die Matrix kommt aus baueViewProj() aber zeilenweise
    // (bhed/gpucam.h). Ohne dieses Wort liest der Shader sie transponiert -
    // kein Absturz, sondern ein Bild, das aussieht, als stuende die Kamera
    // woanders.
    row_major float4x4 gViewProj;
    float3   gKamera;      // Weltkoordinaten, fuer tcGen environment
    float    gZeit;        // Sekunden, fuer die Wellen
    // --- Helligkeit, wie im Rasterer -----------------------------------
    //
    // Der Rasterer rechnet (mapview.cpp:2769):
    //
    //     r = lightmap * brightness
    //     r = max(r, minLight * 255)      // Grundhelligkeit als UNTERgrenze
    //
    // Ohne diese beiden kommt die Karte viel zu dunkel heraus - shanks
    // erster Versuch mit richtigen Lightmaps sah aus wie eine Nachtszene,
    // weil sein Regler auf 2,0x steht und der Shader mit 1,0 rechnete.
    //
    // Die Grundhelligkeit ist ausdruecklich ein max() und keine Addition:
    // sie hebt die dunkelsten Stellen an, ohne die hellen zu veraendern.
    float    gHelligkeit;
    float    gGrundlicht;
    // --- Fuer den Himmel ------------------------------------------------
    //
    // Der Rasterer nimmt die Farbe NICHT aus den Koordinaten der Flaeche,
    // sondern aus der Blickrichtung des Bildpunktes (mapview.cpp:2860). Das
    // ist gleichwertig dazu, die Box in unendlicher Entfernung zu zeichnen -
    // und die Verdeckung kommt geschenkt mit, weil die Himmelsflaechen
    // echte Geometrie bleiben.
    //
    // Dafuer braucht der Shader dieselben Groessen wie die Handrechnung:
    // die drei Kameraachsen, die Brennweite und die halbe Bildgroesse.
    float2   gHalb;        // halfW, halfH
    float    gFokus;
    float    gFuellung;
    float3   gVorn;
    float    gFuellung2;
    float3   gRechts;
    float    gFuellung3;
    float3   gHoch;
    float    gFuellung4;
};
cbuffer JeStapel : register(b1) {
    float4 gTexMatrix;     // die ganze tcMod-Kette als 2x2
    float4 gTexOffTurb;    // Verschiebung x,y + Amplitude,Phase von turb
    float4 gRgb;           // rgbGen const / wave, .a = alphaConst
    float  gAlphaSchwelle;
    // EIN float4, kein `float gDeform[4]`. Das Feld belegte vier
    // Viererbloecke, das letzte Element davon aber nur .x - und
    // gDeformSpread rutschte in .y dahinter (Lage 29). Der Packer schrieb
    // nach 32, der Shader las fuer den Ortsanteil der Welle null: der
    // Lavafall in md_am_sith pulsierte im Gleichtakt, statt zu wogen.
    // Mit float4 gibt es keine Packregel zu erraten: 16..19, dann 20.
    float4 gDeform;        // base, amplitude, phase, frequency
    float  gDeformSpread;
};
)";

// Eine Zeile nur anhaengen, wenn das Bit gesetzt ist. Die Pruefung liest
// sich dadurch wie die Merkmalsliste, und ein vergessenes Bit faellt beim
// Lesen auf.
void wenn(std::string& s, std::uint32_t features, std::uint32_t bit,
          const char* zeile) {
    if ((features & bit) != 0U) {
        s += zeile;
    }
}

// Der Knochenblock. Eigener Puffer (b2), weil er nur bei Figuren gebraucht
// wird und mit 6 KiB der groesste von allen ist.
//
// DREI float4 je Knochen: BoneMatrix ist 3x4 (gla.h:33), die vierte Zeile
// waere immer 0,0,0,1. Sie mitzuschicken kostete ein Drittel mehr Platz fuer
// nichts.
const char* kKnochen = R"(
cbuffer JeFigur : register(b2) {
    float4 gKnochen[384];      // 128 Knochen x 3 Zeilen
    row_major float4x4 gFigurWelt;
    // Das Licht an der Stelle der Figur, schon mal Helligkeit und /255
    // (sampleLightGrid + addDynamicLights). gLichtRichtung.w: 1 = aus dem
    // Gitter, 0 = keins da - dann die alte Schattierung aus der Normalen.
    float4 gLichtUmgebung;
    float4 gLichtGerichtet;
    float4 gLichtRichtung;
};
)";

// Ein Dreieck, das den ganzen Bildschirm bedeckt - fuer die Durchgaenge, die
// nur Bildpunkte verarbeiten (Verkleinern, Weichzeichnen, Auflegen).
//
// Ein Dreieck und kein Viereck: das spart einen Zeichenaufruf und hat keine
// Naht in der Mitte, an der zwei Dreiecke sich beruehren. Die Ecken kommen
// aus SV_VertexID, es braucht also gar keinen Puffer.
const char* kVollbildVs = R"(
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
VSOut main(uint id : SV_VertexID) {
    VSOut aus;
    aus.uv = float2((id << 1) & 2, id & 2);
    aus.pos = float4(aus.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return aus;
}
)";

}  // namespace

std::string vollbildVertexShaderHlsl() { return kVollbildVs; }

// --- Die Klinge eines Lichtschwerts --------------------------------------
//
// Zwei Baender je Klinge, schon in Weltkoordinaten und zur Kamera gedreht
// (gpu::baueKlingenband). Die Farbe kommt aus der Textur (blue_glow2,
// blue_line), und nur wenn sie fehlt, aus der Ersatzfarbe - genau wie im
// Rasterer. gFarbe.a sagt, welches von beiden: 1 Textur, 0 Farbe.
std::string klingeVertexShaderHlsl() {
    std::string s;
    s += kKonstanten;
    s += R"(
struct VSIn {
    float3 pos   : POSITION;
    float2 uv    : TEXCOORD0;
    float4 farbe : COLOR0;
};
struct VSOut {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 farbe : COLOR0;
};
VSOut main(VSIn ein) {
    VSOut aus;
    aus.pos = mul(gViewProj, float4(ein.pos, 1.0));
    aus.uv = ein.uv;
    aus.farbe = ein.farbe;
    return aus;
}
)";
    return s;
}

std::string klingePixelShaderHlsl() {
    return R"(
Texture2D    gBild    : register(t0);
SamplerState gSampler : register(s0);
struct VSOut {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 farbe : COLOR0;
};
float4 main(VSOut ein) : SV_TARGET {
    float3 t = gBild.Sample(gSampler, ein.uv).rgb;
    return float4(lerp(ein.farbe.rgb, t, ein.farbe.a), 1);
}
)";
}

// --- Dynamisches Licht und Blobschatten ------------------------------------
//
// Beides legt die Engine NACH den Stufen einer Flaeche obendrauf, und beides
// nur dort, wo die Flaeche schon im Tiefenpuffer steht (GLS_DEPTHFUNC_EQUAL
// in ProjectDlightTexture2, polygonOffset beim markShadow). Deshalb ein
// eigener Durchgang ueber die deckenden Kartenstapel statt einer weiteren
// Merkmalsvariante: die Kartenshader bleiben, wie sie sind.
//
// Bis zu 32 Lichter (MAX_DLIGHTS, tr_local.h) und 32 Schatten je Aufruf -
// der Packer gibt jedem Stapel nur die, deren Kugel bzw. Kasten seinen
// Huellkasten beruehrt.
namespace {
const char* kLichtKonstanten = R"(
cbuffer JeLicht : register(b4) {
    float4 gLichtInfo;            // x Lichter, y Schatten, z Alphaschwelle, w 1 = mit Bild
    float4 gLichtOrt[32];         // xyz Ort, w Radius
    float4 gLichtFarbe[32];       // rgb
    float4 gSchattenOrt[32];      // xyz Auftreffpunkt, w Radius
    float4 gSchattenNormale[32];  // xyz Normale der getroffenen Ebene, w Alpha
};
struct VSLicht {
    float4 pos  : SV_POSITION;
    float3 welt : TEXCOORD0;
    float3 nrm  : TEXCOORD1;
    float2 uv   : TEXCOORD2;
};
)";
}  // namespace

std::string lichtVertexShaderHlsl() {
    std::string s = kKonstanten;
    s += kLichtKonstanten;
    s += R"(
struct VSIn {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
    float2 uvLm   : TEXCOORD1;
    float4 farbe  : COLOR0;
};
VSLicht main(VSIn ein) {
    VSLicht aus;
    // Dieselbe Rechnung wie im Kartenshader, damit die Tiefe auf die
    // gezeichnete Flaeche faellt (LESS_EQUAL).
    aus.pos = mul(gViewProj, float4(ein.pos, 1.0));
    aus.welt = ein.pos;
    aus.nrm = ein.normal;
    aus.uv = ein.uv;
    return aus;
}
)";
    return s;
}

// ProjectDlightTexture2 (rd-vanilla/tr_shade.cpp:660, r_dlightStyle 1), je
// Dreieck:
//
//     fac      = Abstand des Lichts von der Ebene des Dreiecks
//                (Rueckseite oder fac >= radius: kein Licht)
//     modulate = 1 - fac^2 / radius^2
//     Textur   = tr.dlightImage, in der Ebene zentriert unter dem Licht,
//                Massstab 0.5 / sqrt(radius^2 - fac^2)
//     Farbe    = dl->color * modulate
//
// und das MAL dem ersten deckenden Nicht-Lightmap-Bild der Flaeche,
// GL_ONE GL_ONE. Hat die Flaeche kein solches Bild (tcMod, tcGen
// environment), wird statt dessen GL_DST_COLOR GL_ONE gemischt - dann
// kommt die Farbe ohne Bild heraus (gLichtInfo.w = 0).
//
// gfx/2d/dlight ist kreisrund; der Abgriff laeuft deshalb ueber den
// Abstand zur Mitte, nicht ueber die Kantenrichtung des Dreiecks, die die
// Engine als Achse nimmt.
std::string lichtPixelShaderHlsl() {
    std::string s = kLichtKonstanten;
    s += R"(
Texture2D    gBild    : register(t0);
Texture2D    gDlicht  : register(t1);
SamplerState gSampler : register(s0);
SamplerState gRand    : register(s1);
float4 main(VSLicht ein) : SV_TARGET {
    float4 t = gBild.Sample(gSampler, ein.uv);
    if (gLichtInfo.z > 0.0) {
        clip(t.a - gLichtInfo.z);
    }
    float nl = length(ein.nrm);
    if (nl < 1e-6) {
        discard;
    }
    float3 n = ein.nrm / nl;
    float3 summe = float3(0, 0, 0);
    int anzahl = (int)gLichtInfo.x;
    for (int i = 0; i < anzahl; ++i) {
        float r = gLichtOrt[i].w;
        float3 zumLicht = gLichtOrt[i].xyz - ein.welt;
        float fac = dot(n, zumLicht);
        if (fac <= 0.0 || fac >= r) {
            continue;
        }
        float modulate = 1.0 - (fac * fac) / (r * r);
        float rr = sqrt(r * r - fac * fac);
        float3 inEbene = zumLicht - fac * n;
        float u = length(inEbene) * 0.5 / rr;
        if (u >= 0.5) {
            continue;
        }
        float b = gDlicht.Sample(gRand, float2(0.5 + u, 0.5)).r;
        summe += gLichtFarbe[i].rgb * (modulate * b);
    }
    if (gLichtInfo.w > 0.5) {
        return float4(t.rgb * summe, 1);
    }
    return float4(summe, 1);
}
)";
    return s;
}

// Der Blobschatten (CG_PlayerShadow -> CG_ImpactMark, cg_players.cpp und
// cg_marks.cpp). R_MarkFragments nimmt Flaechen, die
//
//   * zur getroffenen Ebene zeigen (dot >= 0.5),
//   * hoechstens 32 Einheiten darueber und 20 darunter liegen
//     (die beiden Schnittebenen, tr_marks.cpp:306 ff.),
//   * im Quadrat aus +-radius liegen,
//
// und legt darauf `markShadow`: clampmap gfx/damage/shadow, blendFunc
// GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA, rgbGen identity (das Bild ist
// schwarz), alphaGen vertex = 1 - trace.fraction. Am Ziel bleibt also
// ziel * (1 - a); mehrere Schatten multiplizieren sich.
std::string schattenPixelShaderHlsl() {
    std::string s = kLichtKonstanten;
    s += R"(
Texture2D    gBild     : register(t0);
Texture2D    gSchatten : register(t1);
SamplerState gSampler  : register(s0);
SamplerState gRand     : register(s1);
float4 main(VSLicht ein) : SV_TARGET {
    if (gLichtInfo.z > 0.0) {
        clip(gBild.Sample(gSampler, ein.uv).a - gLichtInfo.z);
    }
    float nl = length(ein.nrm);
    if (nl < 1e-6) {
        discard;
    }
    float3 n = ein.nrm / nl;
    float bleibt = 1.0;
    int anzahl = (int)gLichtInfo.y;
    for (int i = 0; i < anzahl; ++i) {
        float3 hn = gSchattenNormale[i].xyz;
        if (dot(n, hn) < 0.5) {
            continue;
        }
        float3 d = ein.welt - gSchattenOrt[i].xyz;
        float h = dot(d, hn);
        if (h > 32.0 || h < -20.0) {
            continue;
        }
        float r = gSchattenOrt[i].w;
        float u = length(d - h * hn) * 0.5 / r;
        if (u >= 0.5) {
            continue;
        }
        float a = gSchatten.Sample(gRand, float2(0.5 + u, 0.5)).a *
                  gSchattenNormale[i].w;
        bleibt *= (1.0 - a);
    }
    return float4(bleibt, bleibt, bleibt, 1);
}
)";
    return s;
}

// --- Die Schattenarten 2 und 3 (cg_shadows) ----------------------------------
//
// Beide arbeiten an der FIGUR selbst, nicht an der Karte: dieselben Ecken und
// Knochen wie figurVertexShaderHlsl, die Haut wird hier noch einmal gerechnet.
// Die Engine nimmt dafuer jede Flaeche der Sortierstufe SS_OPAQUE
// (tr_ghoul2.cpp, RenderSurfaces).
//
// JeSchatten (b5):
//     gSchattenLicht.xyz  Richtung (Art 2: schon (x*0.3, y*0.3, 1), Art 3:
//                         die Lichtrichtung der Figur)
//     gSchattenLicht.w    shadowPlane - Hoehe des Bodens unter der Figur + 1
namespace {
const char* kSchattenHaut = R"(
cbuffer JeSchatten : register(b5) {
    float4 gSchattenLicht;
};
struct VSIn {
    float3 pos     : POSITION;
    float3 normal  : NORMAL;
    float2 uv      : TEXCOORD0;
    float4 bones   : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};
float3 hautInWelt(VSIn ein) {
    float3 welt = float3(0, 0, 0);
    [unroll] for (int i = 0; i < 4; ++i) {
        float w = ein.weights[i];
        if (w <= 0.0) { continue; }
        int b = (int)(ein.bones[i] + 0.5) * 3;
        float4 r0 = gKnochen[b + 0];
        float4 r1 = gKnochen[b + 1];
        float4 r2 = gKnochen[b + 2];
        welt += float3(dot(r0.xyz, ein.pos) + r0.w,
                       dot(r1.xyz, ein.pos) + r1.w,
                       dot(r2.xyz, ein.pos) + r2.w) * w;
    }
    return mul(gFigurWelt, float4(welt, 1.0)).xyz;
}
)";
}  // namespace

// Art 2: die Ecke nur in die Welt bringen - das Volumen baut der
// Geometrie-Shader.
std::string schattenVolumenVsHlsl() {
    std::string s = kKonstanten;
    s += kKnochen;
    s += kSchattenHaut;
    s += R"(
struct VSOut {
    float4 pos  : SV_POSITION;
    float3 welt : TEXCOORD0;
};
VSOut main(VSIn ein) {
    VSOut aus;
    aus.welt = hautInWelt(ein);
    aus.pos = mul(gViewProj, float4(aus.welt, 1.0));
    return aus;
}
)";
    return s;
}

// RB_DoShadowTessEnd und R_RenderShadowEdges (tr_shadows.cpp der Mod),
// Dreieck fuer Dreieck:
//
//     normal = (v2 - v1) x (v3 - v1);  facing = dot(normal, lightDir) > 0
//     shadowXyz = xyz - lightDir * (z - shadowPlane + 100)
//
// Fuer jedes zugewandte Dreieck ALLE drei Kanten als Band zwischen Ecke und
// verschobener Ecke ("we are going to render all edges even though it is a
// tiny bit slower") und beide Deckel - die Engine zeichnet mit
// _STENCIL_REVERSE (z-fail), und das braucht ein geschlossenes Volumen.
// Innere Kanten kommen zweimal mit umgekehrtem Umlauf und heben sich auf.
std::string schattenVolumenGsHlsl() {
    std::string s = kKonstanten;
    s += R"(
cbuffer JeSchatten : register(b5) {
    float4 gSchattenLicht;
};
struct GSIn {
    float4 pos  : SV_POSITION;
    float3 welt : TEXCOORD0;
};
struct GSOut {
    float4 pos : SV_POSITION;
};
float3 weg(float3 p) {
    return p - gSchattenLicht.xyz * (p.z - gSchattenLicht.w + 100.0);
}
void ecke(float3 p, inout TriangleStream<GSOut> aus) {
    GSOut o;
    o.pos = mul(gViewProj, float4(p, 1.0));
    aus.Append(o);
}
[maxvertexcount(18)]
void main(triangle GSIn ein[3], inout TriangleStream<GSOut> aus) {
    float3 v0 = ein[0].welt;
    float3 v1 = ein[1].welt;
    float3 v2 = ein[2].welt;
    float3 n = cross(v1 - v0, v2 - v0);
    if (dot(n, gSchattenLicht.xyz) <= 0.0) {
        return;
    }
    float3 v[3] = {v0, v1, v2};
    float3 w[3] = {weg(v0), weg(v1), weg(v2)};
    [unroll] for (int i = 0; i < 3; ++i) {
        int j = (i + 1) % 3;
        ecke(v[i], aus);
        ecke(w[i], aus);
        ecke(v[j], aus);
        ecke(w[j], aus);
        aus.RestartStrip();
    }
    ecke(v0, aus); ecke(v1, aus); ecke(v2, aus);
    aus.RestartStrip();
    ecke(w[2], aus); ecke(w[1], aus); ecke(w[0], aus);
    aus.RestartStrip();
}
)";
    return s;
}

// Art 3: RB_ProjectionShadowDeform (tr_shadows.cpp) - jede Ecke entlang der
// Lichtrichtung auf die Ebene des Bodens gedrueckt. Die Richtung wird so
// gekippt, dass sie mindestens 0.5 nach oben zeigt ("don't let the shadows
// get too long or go negative"). Gezeichnet mit dem Shader
// `projectionShadow`: polygonOffset, schwarz, deckend.
std::string schattenFlachVsHlsl() {
    std::string s = kKonstanten;
    s += kKnochen;
    s += kSchattenHaut;
    s += R"(
struct VSOut {
    float4 pos : SV_POSITION;
};
VSOut main(VSIn ein) {
    VSOut aus;
    float3 p = hautInWelt(ein);
    float3 l = gSchattenLicht.xyz;
    float d = l.z;
    if (d < 0.5) {
        l.z += 0.5 - d;
        d = l.z;
    }
    l /= d;
    p -= l * (p.z - gSchattenLicht.w);
    aus.pos = mul(gViewProj, float4(p, 1.0));
    return aus;
}
)";
    return s;
}

// Schwarz und deckend (`rgbGen wave square 0 0 0 0`, blendFunc GL_ONE
// GL_ZERO) fuer Art 3; fuer Art 2 dasselbe mit Alpha 0.5 auf allem, was im
// Volumen liegt (RB_ShadowFinish: qglColor4f(0, 0, 0, 0.5), GL_SRC_ALPHA
// GL_ONE_MINUS_SRC_ALPHA, Stencil ungleich null).
std::string schattenSchwarzPsHlsl() {
    return R"(
float4 main(float4 pos : SV_POSITION) : SV_TARGET {
    return float4(0, 0, 0, 1);
}
)";
}

std::string schattenAbdunkelnPsHlsl() {
    return R"(
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
float4 main(VSOut ein) : SV_TARGET {
    return float4(0, 0, 0, 0.5);
}
)";
}

// --- Nebel (r_drawfog 1) -----------------------------------------------------
//
// RB_FogPass (tr_shade.cpp): die Flaeche ein zweites Mal, in der Farbe des
// Nebels, mit tr.fogImage als Alpha, GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA,
// auf der schon gezeichneten Tiefe. Die beiden Koordinaten rechnet
// RB_CalcFogTexCoords JE ECKE (tr_shade_calc.cpp) - hier im Vertex-Shader,
// damit sie genauso ueber das Dreieck laufen wie in der Engine:
//
//     s = tcScale * (Abstand entlang der Blickrichtung) + 1/512
//     t = Tiefe unter der Nebeloberflaeche, je nachdem, ob das Auge im
//         Nebel steht, auf 1/32 .. 31/32 abgebildet
//
// und das Bild selbst ist R_FogFactor (tr_image.cpp) mit der Tabelle aus
// R_InitFogTable (Wurzel) - hier ausgerechnet statt nachgeschlagen.
//
// JeNebel (b6):
//     gNebelFarbe   rgb, w = tcScale
//     gNebelEbene   fog->surface als Tiefenvektor (xyz, w)
//     gNebelInfo    x 1 = mit Oberflaeche, y eye_t, z Alphaschwelle
namespace {
const char* kNebelKonstanten = R"(
cbuffer JeNebel : register(b6) {
    float4 gNebelFarbe;
    float4 gNebelEbene;
    float4 gNebelInfo;
};
struct VSNebel {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float2 st  : TEXCOORD1;
};
float2 nebelST(float3 v) {
    float s = gNebelFarbe.w * (dot(v, gVorn) - dot(gKamera, gVorn)) + 1.0 / 512.0;
    float t = 1.0;
    float augeT = 1.0;
    if (gNebelInfo.x > 0.5) {
        t = dot(v, gNebelEbene.xyz) + gNebelEbene.w;
        augeT = gNebelInfo.y;
    }
    if (augeT < 0.0) {
        t = (t < 1.0) ? 1.0 / 32.0 : 1.0 / 32.0 + 30.0 / 32.0 * t / (t - augeT);
    } else {
        t = (t < 0.0) ? 1.0 / 32.0 : 31.0 / 32.0;
    }
    return float2(s, t);
}
)";
}  // namespace

std::string nebelVertexShaderHlsl() {
    std::string s = kKonstanten;
    s += kNebelKonstanten;
    s += R"(
struct VSIn {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
    float2 uvLm   : TEXCOORD1;
    float4 farbe  : COLOR0;
};
VSNebel main(VSIn ein) {
    VSNebel aus;
    aus.pos = mul(gViewProj, float4(ein.pos, 1.0));
    aus.uv = ein.uv;
    aus.st = nebelST(ein.pos);
    return aus;
}
)";
    return s;
}

std::string nebelFigurVertexShaderHlsl() {
    std::string s = kKonstanten;
    s += kKnochen;
    s += kSchattenHaut;
    s += kNebelKonstanten;
    s += R"(
VSNebel main(VSIn ein) {
    VSNebel aus;
    float3 p = hautInWelt(ein);
    aus.pos = mul(gViewProj, float4(p, 1.0));
    aus.uv = ein.uv;
    aus.st = nebelST(p);
    return aus;
}
)";
    return s;
}

std::string nebelPixelShaderHlsl() {
    std::string s = kKonstanten;
    s += kNebelKonstanten;
    s += R"(
Texture2D    gBild    : register(t0);
SamplerState gSampler : register(s0);
float4 main(VSNebel ein) : SV_TARGET {
    if (gNebelInfo.z > 0.0) {
        clip(gBild.Sample(gSampler, ein.uv).a - gNebelInfo.z);
    }
    float sv = ein.st.x - 1.0 / 512.0;
    float t = ein.st.y;
    if (sv < 0.0 || t < 1.0 / 32.0) {
        discard;
    }
    if (t < 31.0 / 32.0) {
        sv *= (t - 1.0 / 32.0) / (30.0 / 32.0);
    }
    sv = min(sv * 8.0, 1.0);
    return float4(gNebelFarbe.rgb, sqrt(sv));
}
)";
    return s;
}

// Der Hintergrund des Modellfensters: ein senkrechter Verlauf, wie ihn
// renderModel zeichnete - oben 56, unten 34 (von 255), gleich in allen
// drei Kanaelen.
std::string modellHintergrundPixelShaderHlsl() {
    return R"(
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
float4 main(VSOut ein) : SV_TARGET {
    float v = (34.0 + 22.0 * (1.0 - saturate(ein.uv.y))) / 255.0;
    return float4(v, v, v, 1);
}
)";
}

std::string glowShrinkPixelShaderHlsl() {
    // Woertlich wie shrink (src/glow.cpp:53): Kastenmittel ueber alle
    // Quellpunkte, die auf einen Zielpunkt fallen.
    //
    // Vorher verkleinerte der Weichzeichner mit Abstand 0 - vier Abgriffe
    // an derselben Stelle, bilinear also hoechstens 2x2 Quellpunkte. Von
    // 2500 auf 320 Punkte fallen so sieben von acht Zeilen unter den Tisch:
    // das Gluehen wurde kantig ("rastermaessig") und flimmerte, und je
    // weiter weg eine leuchtende Flaeche, desto mehr davon fiel durch.
    return R"(
cbuffer JeDurchgang : register(b3) {
    float2 gSchritt;
    float  gAbstand;
    float  gGewicht;
    float  gWeich;
    float2 gQuelle;       // Breite, Hoehe des vollen Puffers
    float  gFrei;
};
Texture2D    gVoll    : register(t0);
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
float4 main(VSOut ein) : SV_TARGET {
    const int sw = (int)gQuelle.x;
    const int sh = (int)gQuelle.y;
    const int dw = (int)round(1.0 / gSchritt.x);
    const int dh = (int)round(1.0 / gSchritt.y);
    const int x = (int)ein.pos.x;
    const int y = (int)ein.pos.y;
    const int x0 = x * sw / dw;
    const int x1 = min(max(x0 + 1, (x + 1) * sw / dw), x0 + 64);
    const int y0 = y * sh / dh;
    const int y1 = min(max(y0 + 1, (y + 1) * sh / dh), y0 + 64);
    float3 sum = float3(0, 0, 0);
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            sum += gVoll.Load(int3(xx, yy, 0)).rgb;
        }
    }
    return float4(sum / (float)max(1, (x1 - x0) * (y1 - y0)), 1);
}
)";
}

std::string glowBlurPixelShaderHlsl() {
    // Woertlich wie glowBlur (src/glow.cpp:109): ein Vier-Punkt-Kern in den
    // Ecken, Gewicht intensity*0.25 je Punkt.
    //
    // Der Abstand waechst je Durchgang um `delta` und wird GERUNDET
    // (std::lround). Das Runden gehoert dazu: der Rasterer arbeitet auf
    // ganzen Bildpunkten, und ein Shader, der mit Zwischenwerten abtastet,
    // ergibt ein weicheres Bild als der Rasterer - sichtbar anders, obwohl
    // beide "richtig" aussehen.
    return R"(
cbuffer JeDurchgang : register(b3) {
    float2 gSchritt;      // 1/Breite, 1/Hoehe des kleinen Puffers
    float  gAbstand;      // in BILDPUNKTEN, schon gerundet
    float  gGewicht;      // intensity * 0.25
};
Texture2D    gQuelle  : register(t0);
SamplerState gSampler : register(s0);
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
float4 main(VSOut ein) : SV_TARGET {
    float3 acc = float3(0, 0, 0);
    [unroll] for (int sy = -1; sy <= 1; sy += 2) {
        [unroll] for (int sx = -1; sx <= 1; sx += 2) {
            float2 uv = ein.uv + float2(sx, sy) * gAbstand * gSchritt;
            acc += gQuelle.Sample(gSampler, saturate(uv)).rgb * gGewicht;
        }
    }
    return float4(acc, 1);
}
)";
}

std::string glowCompositePixelShaderHlsl() {
    // Woertlich wie glowComposite (src/glow.cpp:255):
    //
    //     soft:  out = g + s * (1 - g/255)
    //     hart:  out = g + s
    //
    // `soft` ist die uebliche Einstellung. Die Form mit den vertauschten
    // Rollen steht so im Quelltext des Rasterers - und deshalb auch hier.
    return R"(
cbuffer JeDurchgang : register(b3) {
    float2 gSchritt;
    float  gAbstand;
    float  gGewicht;
    float  gWeich;        // 1 = soft, 0 = hart
    float3 gFuellung;
};
Texture2D    gGlow    : register(t0);
Texture2D    gSzene   : register(t1);
SamplerState gSampler : register(s0);
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
float4 main(VSOut ein) : SV_TARGET {
    float3 g = gGlow.Sample(gSampler, ein.uv).rgb;
    float3 s = gSzene.Sample(gSampler, ein.uv).rgb;
    float3 aus = (gWeich > 0.5) ? (g + s * (1.0 - g)) : (g + s);
    return float4(saturate(aus), 1);
}
)";
}

std::string figurVertexShaderHlsl(std::uint32_t features) {
    std::string s;
    s += kKonstanten;
    s += kKnochen;
    s += R"(
struct VSIn {
    float3 pos     : POSITION;
    float3 normal  : NORMAL;
    float2 uv      : TEXCOORD0;
    float4 bones   : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};
struct VSOut {
    float4 pos    : SV_POSITION;
    float2 uv     : TEXCOORD0;
    float2 uvLm   : TEXCOORD1;
    float4 farbe  : COLOR0;
};
VSOut main(VSIn ein) {
    VSOut aus;
    // --- Die Verformung, wie BoneMatrix::transform (gla.cpp:87) ----------
    //
    //     out[i] = m[i][0]*in[0] + m[i][1]*in[1] + m[i][2]*in[2] + m[i][3]
    //
    // Also drei Zeilen mit je vier Werten, die vierte Spalte ist die
    // Verschiebung. Genau so liegen sie im Puffer.
    //
    // Die Gewichte summieren sich zu eins - dafuer sorgt packeEcken()
    // (gpuskin.cpp). Hier wird NICHT noch einmal normiert: eine zweite
    // Normierung wuerde einen Fehler in der ersten verstecken.
    float3 welt = float3(0, 0, 0);
    float3 nrm = float3(0, 0, 0);
    [unroll] for (int i = 0; i < 4; ++i) {
        float w = ein.weights[i];
        if (w <= 0.0) { continue; }
        int b = (int)(ein.bones[i] + 0.5) * 3;
        float4 r0 = gKnochen[b + 0];
        float4 r1 = gKnochen[b + 1];
        float4 r2 = gKnochen[b + 2];
        float3 p = float3(dot(r0.xyz, ein.pos) + r0.w,
                          dot(r1.xyz, ein.pos) + r1.w,
                          dot(r2.xyz, ein.pos) + r2.w);
        // Die Normale wird OHNE die Verschiebung gedreht - sie ist eine
        // Richtung, kein Ort. Wer die vierte Spalte mitnimmt, bekommt
        // Beleuchtung, die sich mit der Figur durch den Raum bewegt.
        float3 n = float3(dot(r0.xyz, ein.normal),
                          dot(r1.xyz, ein.normal),
                          dot(r2.xyz, ein.normal));
        welt += p * w;
        nrm += n * w;
    }
    // Danach die Stellung der Figur in der Welt.
    float4 w4 = mul(gFigurWelt, float4(welt, 1.0));
    aus.pos = mul(gViewProj, w4);
    aus.uv = ein.uv;
    aus.uvLm = float2(0.0, 0.0);
    // --- Die Beleuchtung aus der Normalen --------------------------------
    //
    // Hier stand `aus.farbe = float4(1, 1, 1, 1)`. Die Normale wurde oben
    // ueber die Knochen gedreht - und dann WEGGEWORFEN. Figuren waren damit
    // voellig flach: Textur mal Helligkeit, ohne jede Schattierung.
    //
    // Sichtbar wurde es an den Battledroids in md_am_sith. Sie sahen mit
    // ausgeschaltetem Gluehen wie schwarze Silhouetten aus und mit
    // eingeschaltetem weiss - eine flache, dunkle Textur mal zwei, die der
    // Gluehdurchgang dann ausblies. Der Bericht zu rc453 hat alle anderen
    // Erklaerungen ausgeschlossen: sechzig Flaechen, alle deckend, alle mit
    // gefundener Textur, keine additiv.
    //
    // Der Rasterer rechnet (mapview.cpp:3362):
    //
    //     shade = 0.22 + 0.78 * |dot(normale, licht)|
    //     licht = normalise(0.4, 0.55, 0.72)
    //
    // Der Betrag ist Absicht: eine Figur, die sich dreht, soll nicht zur
    // Haelfte schwarz werden. Die 0.22 sind das Grundlicht, damit die
    // abgewandte Seite nicht verschwindet.
    //
    // DIESELBE Formel, sonst waeren es zwei Fassungen - und die sind hier
    // schon fuenfmal auseinandergelaufen.
    //
    // --- Das Licht aus dem Gitter der Karte --------------------------------
    //
    // Wie der Rasterer (mapview.cpp, renderActors) und RB_CalcDiffuseColor
    // (tr_shade_calc.cpp): Umgebung + max(0, n.l) * gerichtet, geklemmt.
    // Vorher fehlte es auf dem GPU-Weg ganz - jede Figur war gleich hell,
    // ob im Schatten oder unter einer Lampe.
    //
    // Die Normale erst in die WELT drehen (gFigurWelt ohne Verschiebung):
    // das Gitterlicht kommt aus einer Weltrichtung.
    {
        // Eine Normale der Laenge null (in 684 Modellen der Mod, vor allem
        // Kartenobjekte - tests/meshmasse.cpp) bleibt null: normalize()
        // gaebe NaN und damit schwarze Bildpunkte. Die Engine rechnet mit
        // ihr einfach dot = 0, also nur Umgebungslicht - so auch hier.
        float3 nw = mul((float3x3)gFigurWelt, nrm);
        float nl = length(nw);
        float3 n = (nl > 1e-6) ? nw / nl : float3(0, 0, 0);
        if (gLichtRichtung.w > 0.5) {
            float ein = max(0.0, dot(n, gLichtRichtung.xyz));
            float3 lit = min(float3(1, 1, 1),
                             gLichtUmgebung.rgb + ein * gLichtGerichtet.rgb);
            aus.farbe = float4(lit, 1);
        } else {
            float3 licht = normalize(float3(0.4, 0.55, 0.72));
            float schatten = 0.22 + 0.78 * abs(dot(n, licht));
            aus.farbe = float4(schatten, schatten, schatten, 1);
        }
    }
    return aus;
}
)";
    (void)features;
    return s;
}

std::string vertexShaderHlsl(std::uint32_t features, int numTexMods) {
    std::string s;
    s += kKonstanten;
    s += R"(
struct VSIn {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
    float2 uvLm   : TEXCOORD1;
    float4 farbe  : COLOR0;
};
struct VSOut {
    float4 pos    : SV_POSITION;
    float2 uv     : TEXCOORD0;
    float2 uvLm   : TEXCOORD1;
    float4 farbe  : COLOR0;
};
VSOut main(VSIn ein) {
    VSOut aus;
    float3 welt = ein.pos;
)";

    // deformVertexes wave - RB_CalcDeformVertexes: die Ecke wandert entlang
    // ihrer Normalen. Der Versatz haengt vom ORT ab, deshalb hier und nicht
    // im Pixel-Shader.
    // --- Die Phase ZUERST kuerzen, dann den Sinus ----------------------
    //
    // `waveValue` im Rasterer rechnet (wave.cpp:59):
    //
    //     t = fraction(phase + zeit * frequenz);      // immer in [0,1)
    //     wert = base + shape(func, t) * amplitude;
    //
    // Hier stand der volle Wert im Sinus. Das ist mathematisch dasselbe -
    // aber nicht numerisch. Der Ortsanteil `off` wird gross: an der
    // Lavaflaeche von md_am_sith spannen die Koordinaten ueber 7168 x 9456
    // x 2078 Einheiten, und mit `deformSpread = 1/180` laeuft `off` bis
    // ueber hundert. Das Argument des Sinus erreicht damit rund 630.
    //
    // Der Unterschied zwischen zwei benachbarten Ecken betraegt dort im
    // Mittel 0,053 Wellen - gemessen ueber alle 19617 Kanten der Flaeche,
    // also knapp 19 Ecken je Wellenlaenge. Das ist eine gut darstellbare
    // Welle. Sie ueberlebt aber nicht, wenn 0,33 Bogenmass Unterschied aus
    // einem Argument von 630 herausgerechnet werden muessen: die
    // Bereichsreduktion von `sin` arbeitet auf der Grafikkarte einfach
    // genau, und was bleibt, ist eine Bewegung im Gleichtakt - ein
    // Pulsieren statt einer Welle.
    //
    // `frac` kuerzt genau wie `fraction(x) = x - floor(x)`, auch bei
    // negativen Werten.
    wenn(s, features, kDeform, R"(    {
        float off = (welt.x + welt.y + welt.z) * gDeformSpread;
        float t = frac(gDeform[2] + off + gZeit * gDeform[3]);
        float w = gDeform[0] + gDeform[1] * sin(t * 6.2831853);
        welt += ein.normal * w;
    }
)");

    // autosprite: die Ecke zur Kamera drehen. Nur 34 Vorkommen in allen 43
    // Karten, aber ohne sie stehen Flammen und Rauchfahnen quer.
    //
    // Wie AutospriteDeform (tr_shade_calc.cpp): das Plaettchen steht entlang
    // der LINKS- und HOCH-Achse der Kamera, nicht zur Ecke gedreht. Beim
    // Hochladen (baueAutosprites, gpudraw.cpp) ist die Position schon die
    // Mitte des Vierecks, und in der Normalen stehen Richtung (x: +-1 links,
    // y: +-1 hoch) und Radius (z).
    //
    // Vorher stand hier `welt += rechts * uv.x + hoch * uv.y` - ein Versatz
    // um die Texturkoordinate, also um null bis eine Einheit. Die Holo-
    // Flaechen in md_am_sith blieben so stehen, wie sie gebaut sind: das
    // Palpatine-Hologramm als grosses Rechteck, das die Figuren dahinter
    // ueberdeckte.
    wenn(s, features, kAutosprite, R"(    {
        float3 links = -gRechts;
        welt += (links * ein.normal.x + gHoch * ein.normal.y) * ein.normal.z;
    }
)");

    // --- Die Stellung des Netzes steckt in gViewProj ---------------------
    //
    // Fruehere Fassung hatte eine eigene `gModellWelt` im STAPEL-Puffer.
    // Das brachte den Puffer von 60 auf 76 float und ein Bild, in dem alle
    // Ecken in einen Punkt liefen - ein Faecher aus roten Strahlen, auch
    // wenn Himmel, Effekte, Gluehen und Figuren abgeschaltet waren.
    //
    // Ein Mover braucht keine zweite Matrix: seine Stellung laesst sich VOR
    // dem Zeichnen in die Kameramatrix multiplizieren. Ein Wert weniger im
    // Puffer ist ein Wert weniger, der falsch liegen kann - und der
    // Stapelpuffer bleibt bei den 60 float, die seit rc392 stimmen.
    s += "    aus.pos = mul(gViewProj, float4(welt, 1.0));\n";

    // tcGen environment - die Texturkoordinate kommt aus der Spiegelung,
    // nicht aus der Datei.
    if ((features & kTexGenEnv) != 0U) {
        s += R"(    {
        float3 zur = normalize(gKamera - welt);
        float3 sp = reflect(-zur, normalize(ein.normal));
        aus.uv = sp.yz * 0.5 + 0.5;
    }
)";
    } else {
        s += "    aus.uv = ein.uv;\n";
    }

    // --- Die tcMod-Kette: EINE Abbildung, kein Ablauf --------------------
    //
    // Bis rc442 lief hier eine Schleife ueber bis zu acht Schritte, jeder
    // mit seiner Art in .w. Das kostete 32 der 60 float im Stapelpuffer und
    // hatte einen Schritt, der nicht hineinpasste: `tcMod transform` braucht
    // sechs Werte und stand deshalb auf null.
    //
    // rd-rend2 rechnet die ganze Kette auf der CPU zu einer affinen
    // Abbildung zusammen (`ComputeTexMods`) und laesst im Shader nur noch
    // das hier stehen (`ModTexCoords`, glsl/generic.glsl):
    //
    //     st2.x = st.x*M.x + (st.y*M.z + off.x)
    //     st2.y = st.x*M.y + (st.y*M.w + off.y)
    //
    // `turb` passt als einziger nicht in eine Matrix - er verschiebt je nach
    // ORT. Er kommt danach obendrauf, mit Amplitude und Phase aus
    // gTexOffTurb.zw.
    if ((features & kTexMod) != 0U) {
        s += R"(    {
        float2 st2;
        st2.x = aus.uv.x * gTexMatrix.x + (aus.uv.y * gTexMatrix.z
                                           + gTexOffTurb.x);
        st2.y = aus.uv.x * gTexMatrix.y + (aus.uv.y * gTexMatrix.w
                                           + gTexOffTurb.y);
        float2 ort = float2(welt.x + welt.z, welt.y);
        float2 tx = frac(ort * 0.0009765625 + gTexOffTurb.w.xx);
        aus.uv = st2 + sin(tx * 6.2831853) * gTexOffTurb.z;
    }
)";
    }
    // Die Zahl der Schritte steht nicht mehr im erzeugten Quelltext - die
    // Kette ist ja schon zusammengerechnet. Sie bleibt im Shadernamen, damit
    // dieser Kopf sich nicht aendert; zwei Namen fuer denselben Quelltext
    // kosten hoechstens eine Uebersetzung zu viel.
    (void)numTexMods;

    wenn(s, features, kLightmap, "    aus.uvLm = ein.uvLm;\n");
    if ((features & kLightmap) == 0U) {
        s += "    aus.uvLm = float2(0.0, 0.0);\n";
    }

    s += "    aus.farbe = ein.farbe;\n";
    // rgbGen const UND rgbGen wave ersetzen die Eckenfarbe - keins von
    // beiden multipliziert sie. Hier stand `*=` fuer die Welle; das machte
    // die Lava um den Faktor der Eckenhelligkeit zu dunkel (gemessen: 0,40).
    //
    // Der Wert steht fuer beide im selben Feld, und der Packer laesst die
    // Welle vorgehen - wie der Rasterer. Deshalb reicht EINE Zeile, und sie
    // darf nicht zweimal dastehen.
    if ((features & (kRgbConst | kRgbWave)) != 0U) {
        s += "    aus.farbe.rgb = gRgb.rgb;\n";
    }
    s += "    return aus;\n}\n";
    return s;
}

// --- Welcher Alphawert gilt? -----------------------------------------
//
// Der Rasterer (mapview.cpp:2950):
//
//     alpha = (textur.a / 255) * vonAlphaGen
//     vonAlphaGen = alphaKonst >= 0 ? alphaKonst
//                 : alphaGen == Identity ? 1.0
//                 : eckAlpha
//
// Der Alphakanal der Textur multipliziert IMMER; was davor steht,
// entscheidet alphaGen. Der Shader nahm bis rc443 durchweg den Eckenwert -
// `alphaGen const 0` (unter anderem in der Lava) hatte gar keine Wirkung.
std::string alphaZeile(std::uint32_t features) {
    if ((features & kAlphaKonst) != 0U) {
        return "    c.a *= gRgb.a;\n";
    }
    if ((features & kAlphaEins) != 0U) {
        // alphaGen identity: der Eckenwert gilt NICHT. Die Zeile faellt
        // ganz weg - der Alphakanal der Textur bleibt allein stehen.
        return "";
    }
    return "    c.a *= ein.farbe.a;\n";
}

std::string pixelShaderHlsl(std::uint32_t features) {
    std::string s;
    s += kKonstanten;
    s += R"(
Texture2D    gBild     : register(t0);
Texture2D    gLightmap : register(t1);
// Die sechs Himmelsseiten als Feld - die Seitennummern sind die aus
// skyFace(), damit die Zuordnung nicht zweimal erfunden wird.
Texture2DArray gHimmel : register(t2);
SamplerState gSampler  : register(s0);
struct VSOut {
    float4 pos    : SV_POSITION;
    float2 uv     : TEXCOORD0;
    float2 uvLm   : TEXCOORD1;
    float4 farbe  : COLOR0;
};
float4 main(VSOut ein) : SV_TARGET {
    float4 c = gBild.Sample(gSampler, ein.uv);
)";

    // --- Der Himmel, woertlich wie skyFace() ------------------------------
    //
    // Die Richtung rueckwaerts aus der Projektion:
    //
    //     dot(q,R)/z = (sx - halfW) / (focal * halfH)
    //     dot(q,U)/z = (halfH - sy) / (focal * halfH)
    //     dot(q,F)/z = 1
    //
    // Die Zuordnung Richtung -> Seite und die Koordinaten darauf sind Zeile
    // fuer Zeile aus mapview.cpp:55 uebernommen. Wer hier eine Achse
    // vertauscht, bekommt einen Himmel, der beim Drehen springt - und das
    // faellt erst auf, wenn man sich umsieht.
    if ((features & kSky) != 0U) {
        s += R"(    {
        float2 sp = ein.pos.xy;
        float a = (sp.x - gHalb.x) / (gFokus * gHalb.y);
        float b = (gHalb.y - sp.y) / (gFokus * gHalb.y);
        float3 d = gVorn + gRechts * a + gHoch * b;
        float ax = abs(d.x); float ay = abs(d.y); float az = abs(d.z);
        int face; float sN; float tN;
        if (ax >= ay && ax >= az) {
            if (d.x > 0) { face = 0; sN = -d.y/ax; tN =  d.z/ax; }
            else         { face = 1; sN =  d.y/ax; tN =  d.z/ax; }
        } else if (ay >= az) {
            if (d.y > 0) { face = 2; sN =  d.x/ay; tN =  d.z/ay; }
            else         { face = 3; sN = -d.x/ay; tN =  d.z/ay; }
        } else {
            if (d.z > 0) { face = 4; sN = -d.y/az; tN = -d.x/az; }
            else         { face = 5; sN = -d.y/az; tN =  d.x/az; }
        }
        float2 huv = float2(saturate((sN + 1.0) * 0.5),
                            saturate(1.0 - (tN + 1.0) * 0.5));
        c = gHimmel.Sample(gSampler, float3(huv, face));
        return c;
    }
)";
    }
    s += "";

    // alphaFunc. NUR wenn der Shader es verlangt - das war rc385: der Test
    // lief ohne Bedingung, und ueber die Haelfte jeder Figur fiel weg, weil
    // JKA-Texturen fast alle einen Alphakanal tragen.
    wenn(s, features, kAlphaTest,
         "    clip(c.a - gAlphaSchwelle);\n");

    if ((features & kLightmap) != 0U) {
        // --- Mit Lightmap gilt die Eckenfarbe NICHT --------------------
        //
        // Hier stand `c *= ein.farbe;` VOR diesem Block, also fuer beide
        // Zweige. Die lightmapbeleuchteten Flaechen bekamen damit
        //
        //     Textur x Eckenfarbe x Lightmap
        //
        // Der Rasterer nimmt entweder das eine ODER das andere
        // (mapview.cpp:2669, `if (byVertex) ... else if (lm != nullptr)`) -
        // niemals beides.
        //
        // Wie viel das ausmacht, an md_am_sith.bsp nachgemessen: auf
        // lightmapbeleuchteten Flaechen liegt die mittlere Eckenhelligkeit
        // bei 46 von 255, also 0,18. Der GPU-Weg zeigte diese Flaechen
        // damit bei rund einem Fuenftel der richtigen Helligkeit. Genau
        // das war "das Lightning sieht immer noch nicht richtig aus, es
        // ist sehr dunkel".
        //
        // Der ALPHAWERT der Ecke bleibt: er traegt durchscheinende
        // Flaechen, und den nimmt der Rasterer unabhaengig vom Licht
        // (`eckAlpha`, mapview.cpp).
        //
        // Damit faellt auch rgbGen const/wave auf lightmapbeleuchteten
        // Flaechen weg - genau wie im Rasterer, wo der Lightmap-Zweig die
        // Eckenfarbe gar nicht erst ansieht.
        s += alphaZeile(features);
        s += R"(    {
        float3 lm = gLightmap.Sample(gSampler, ein.uvLm).rgb * gHelligkeit;
        lm = max(lm, gGrundlicht.xxx);
        c.rgb *= lm;
    }
)";
        // rgbGen const/wave der Bildstufe gilt AUCH mit Lightmap: in der
        // Engine sind es zwei Stufen, Lightmap x (Textur x rgbGen).
        if ((features & (kRgbConst | kRgbWave)) != 0U) {
            s += "    c.rgb *= ein.farbe.rgb;\n";
        }
    } else {
        // Farbe UND Alpha aus der Ecke - aber der Alphaanteil haengt an
        // alphaGen, deshalb getrennt.
        s += "    c.rgb *= ein.farbe.rgb;\n";
        s += alphaZeile(features);
        // --- Ohne Lightmap traegt die ECKENFARBE das Licht -----------------
        //
        // Der Rasterer multipliziert sie mit `opt.brightness`
        // (mapview.cpp:2687, `kVertexBoost`). Der Shader tat das nicht - und
        // Flaechen ohne Lightmap kamen dadurch um den Faktor 2 zu dunkel
        // heraus.
        //
        // Gemeldet: "die Lava ist schwarz". Die Lava hat keine Lightmap; ihre
        // Eckenfarben sind dunkel und sollten erst durch den Faktor zum
        // Leuchten kommen.
        //
        // Die Grundhelligkeit gilt hier genauso: sie hebt die dunkelsten
        // Stellen an, ohne die hellen zu veraendern.
        if ((features & kRohEcke) != 0U) {
            // Eine Effektfarbe ist schon fertig - sie kommt aus dem
            // rgb-Block der .efx und darf nicht aufgehellt werden. Der
            // Rasterer setzt hier ausdruecklich den Faktor eins
            // (`kVertexBoost = p.rohEcke ? 1.0F : opt.brightness`,
            // mapview.cpp:2687). Ohne diesen Zweig waren die Effekte auf
            // dem GPU-Weg um den ganzen Helligkeitsfaktor zu hell - bei
            // shanks Einstellung von 2,0x also doppelt.
        } else {
            s += R"(    {
        float3 vf = c.rgb * gHelligkeit;
        c.rgb = max(vf, gGrundlicht.xxx * c.rgb);
    }
)";
        }
    }

    // alphaGen lightingSpecular - eigener Weg, kein Sonderfall der Farbe.
    //
    // Hier stand `gRgb.a`, und darin steckt der alphaConst. Zwei
    // verschiedene alphaGen-Formen teilten sich also einen Wert; eine
    // Flaeche mit lightingSpecular und OHNE alphaGen const bekam die Eins
    // aus dem Vorgabezweig, eine MIT beidem den falschen Wert.
    //
    // Der Rasterer rechnet das Glanzlicht gar nicht (AlphaGen::Unsupported,
    // shaderscript.h). Bis der GPU-Weg es rechnet, bleibt der Alphawert
    // unangetastet - das ist derselbe Stand wie im Rasterer und nicht
    // stillschweigend etwas anderes.
    (void)kSpecular;

    // --- Ueberlauf proportional herunterrechnen, nicht abschneiden --------
    //
    // Der Rasterer (mapview.cpp, "Ueberlauf NICHT einzeln abschneiden")
    // teilt alle drei Kanaele durch den groessten, sobald einer ueber 1
    // steigt - wie R_ColorShiftLightingBytes: "normalize by color instead
    // of saturating to white". Das Renderziel schnitt dagegen JEDEN Kanal
    // einzeln bei 1 ab.
    //
    // Bei 2,0x Helligkeit ist das der Unterschied zwischen Orange und Gelb:
    // die Lava in md_am_sith (rgbGen wave 0,8 x 2,0 = 1,6 x Textur) hat Rot
    // weit ueber 1 und Gruen knapp darunter. Abgeschnitten bleibt Rot bei 1
    // und Gruen holt auf - gelb. Heruntergerechnet bleibt das Verhaeltnis -
    // orange, wie im Rasterer.
    s += R"(    {
        float spitze = max(c.r, max(c.g, c.b));
        if (spitze > 1.0) {
            c.rgb /= spitze;
        }
    }
)";

    s += "    return c;\n}\n";
    return s;
}

std::string shaderName(std::uint32_t features, int numTexMods) {
    std::string n = "fx";
    struct Eintrag {
        std::uint32_t bit;
        const char* kuerzel;
    };
    // Feste Reihenfolge: gleiche Bits, gleicher Name - sonst erkennt man
    // wiederverwendbare Shader nicht wieder.
    static const Eintrag kListe[] = {
        {kLightmap, "_lm"},      {kTexMod, "_tc"},
        {kRgbWave, "_rw"},       {kCullNone, "_c2"},
        {kCullBack, "_cb"},      {kTexGenEnv, "_env"},
        {kAlphaTest, "_at"},     {kPolygonOffset, "_po"},
        {kAutosprite, "_as"},    {kDeform, "_df"},
        {kRgbConst, "_rc"},      {kSpecular, "_sp"},
        // alphaGen: DREI verschiedene Quelltexte, also drei Kuerzel.
        {kAlphaKonst, "_ak"},    {kAlphaEins, "_a1"},
        {kSky, "_sky"},
        // kRohEcke MUSS ein Kuerzel bekommen: es aendert den erzeugten
        // Quelltext (kein Helligkeitsfaktor auf der Eckenfarbe). Ohne
        // Kuerzel bekaemen zwei verschiedene Shader denselben Namen und
        // teilten sich einen Zwischenspeicher - der erste, der uebersetzt
        // wird, gaelte dann fuer beide.
        {kRohEcke, "_re"},
        // kGlow bekommt KEIN Kuerzel: es aendert nicht, WIE gezeichnet wird,
        // sondern ob der Stapel in den Gluehdurchgang gehoert. Zwei Shader
        // dafuer waeren zwei Uebersetzungen desselben Quelltexts.
    };
    for (const Eintrag& e : kListe) {
        if ((features & e.bit) != 0U) {
            n += e.kuerzel;
        }
    }
    if (numTexMods > 0) {
        n += std::to_string(numTexMods);
    }
    return n;
}

}  // namespace bhed::gpu
