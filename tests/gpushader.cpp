// Probe fuer die Shadererzeugung.
//
// Was sie kann und was nicht
// --------------------------
// Sie kann NICHT pruefen, ob das HLSL uebersetzt - dafuer braucht es
// Direct3D, und das gibt es auf diesem Rechner nicht. Ein Tippfehler im
// Shader faellt erst auf shanks Rechner auf.
//
// Sie kann pruefen, ob jedes gesetzte Merkmal auch im Quelltext landet, und
// ob es OHNE das Merkmal wirklich fehlt. Das ist genau die Fehlerklasse, die
// in diesem Projekt am haeufigsten war:
//
//   rc383  deformVertexes wurde nur in den Basiseintrag kopiert
//   rc385  der Alphatest lief ohne die Bedingung
//   rc386  splitMap wurde geschrieben, aber nie gelesen
//
// Alle drei: ein Merkmal, das an einer von zwei Stellen fehlte.

#include "bhed/gpushader.h"
#include "bhed/gpustate.h"

#include <cstddef>
#include <cstdio>
#include <string>

using namespace bhed::gpu;

namespace {

int fehler = 0;

void erwarte(const char* was, bool ok) {
    std::printf("  %-4s  %s\n", ok ? "ok" : "FEHL", was);
    if (!ok) {
        ++fehler;
    }
}

bool enthaelt(const std::string& s, const char* teil) {
    return s.find(teil) != std::string::npos;
}

// Ein Merkmal ist richtig verdrahtet, wenn sein Kennzeichen MIT dem Bit
// auftaucht und OHNE das Bit fehlt. Nur die eine Haelfte zu pruefen reicht
// nicht - ein Shader, der alles immer enthaelt, bestuende sie.
void beideRichtungen(const char* name, std::uint32_t bit, const char* kennung,
                     bool imVertex) {
    const std::string mit = imVertex ? vertexShaderHlsl(bit, 1)
                                     : pixelShaderHlsl(bit);
    const std::string ohne = imVertex ? vertexShaderHlsl(kNichts, 0)
                                      : pixelShaderHlsl(kNichts);
    char z[160];
    std::snprintf(z, sizeof(z), "%s steht drin, wenn das Bit gesetzt ist", name);
    erwarte(z, enthaelt(mit, kennung));
    std::snprintf(z, sizeof(z), "%s fehlt, wenn es nicht gesetzt ist", name);
    erwarte(z, !enthaelt(ohne, kennung));
}

}  // namespace

int main() {
    std::printf("gpushader\n");

    // --- Jedes Merkmal, in beide Richtungen ------------------------------
    beideRichtungen("deformVertexes", kDeform, "ein.normal * w", true);
    // Wie AutospriteDeform: entlang der Kameraachsen, Richtung und Radius
    // aus der Normalen (baueAutosprites beim Hochladen).
    beideRichtungen("autosprite", kAutosprite, "links * ein.normal.x + gHoch * ein.normal.y", true);
    beideRichtungen("tcGen environment", kTexGenEnv, "reflect", true);
    // NICHT auf "gTexMatrix" pruefen: der Name steht ohnehin in der
    // Erklaerung des Konstantenpuffers, also auch ohne das Bit. `st2` gibt
    // es nur im erzeugten Block.
    beideRichtungen("tcMod", kTexMod, "float2 st2;", true);
    beideRichtungen("rgbGen const", kRgbConst, "aus.farbe.rgb = gRgb.rgb", true);
    // Seit rc440 ERSETZT auch die Welle die Eckenfarbe - dieselbe Zeile wie
    // rgbGen const, denn beide meinen dasselbe (mapview.cpp:1818).
    beideRichtungen("rgbGen wave", kRgbWave, "aus.farbe.rgb = gRgb.rgb", true);
    beideRichtungen("Lightmap (Vertex)", kLightmap, "aus.uvLm = ein.uvLm", true);
    beideRichtungen("alphaFunc", kAlphaTest, "clip(", false);
    beideRichtungen("Lightmap (Pixel)", kLightmap, "gLightmap.Sample", false);
    // Ohne Lightmap traegt die Eckenfarbe das Licht - und braucht denselben
    // Helligkeitsfaktor. Genau der fehlte, und die Lava kam schwarz heraus.
    erwarte("ohne Lightmap wird die Eckenfarbe aufgehellt",
            enthaelt(pixelShaderHlsl(kNichts), "c.rgb * gHelligkeit"));
    erwarte("mit Lightmap NICHT doppelt",
            !enthaelt(pixelShaderHlsl(kLightmap), "c.rgb * gHelligkeit"));
    // `kSpecular` erzeugt seit rc443 KEINEN Quelltext mehr.
    //
    // Die Zeile lautete `c.a = saturate(c.a * gRgb.a);` - und `gRgb.a` ist
    // eins, solange kein `alphaGen const` dasteht. Bei `lightingSpecular`
    // steht nie eines da (alphaGen hat einen Wert, nicht zwei), also war die
    // Zeile immer wirkungslos. Sie sah nur so aus, als waere das Glanzlicht
    // gerechnet.
    //
    // Der Rasterer rechnet es ebenfalls nicht (AlphaGen::Unsupported). Das
    // Bit bleibt - es haelt fest, dass diese Flaeche ein Glanzlicht WILL -
    // aber es taeuscht keine Rechnung mehr vor.
    erwarte("lightingSpecular erzeugt keinen Quelltext",
            pixelShaderHlsl(kSpecular) == pixelShaderHlsl(kNichts));
    beideRichtungen("Himmel", kSky, "gHimmel.Sample", false);

    // --- Der Figuren-Shader ----------------------------------------------
    //
    // Er ist ein eigener Shader, kein Merkmal des Karten-Shaders: andere
    // Eingabe, und die Verformung passiert VOR der Projektion.
    {
        const std::string f = figurVertexShaderHlsl(kNichts);
        erwarte("Figuren-Shader kennt die Knochen",
                enthaelt(f, "gKnochen["));
        erwarte("und die Gewichte", enthaelt(f, "BLENDWEIGHT"));
        erwarte("und die Knochennummern", enthaelt(f, "BLENDINDICES"));
        erwarte("er dreht die Normale OHNE Verschiebung",
                enthaelt(f, "dot(r0.xyz, ein.normal)"));
        erwarte("und nimmt fuer den Ort die vierte Spalte MIT",
                enthaelt(f, "dot(r0.xyz, ein.pos) + r0.w"));
        erwarte("die Stellung der Figur kommt danach",
                enthaelt(f, "mul(gFigurWelt"));
        erwarte("und zuletzt die Kamera", enthaelt(f, "mul(gViewProj"));
        // 128 Knochen x 3 Zeilen - passt zu kMaxKnochen in gpuskin.h.
        erwarte("der Knochenpuffer fasst 384 Zeilen",
                enthaelt(f, "gKnochen[384]"));
        erwarte("auch die Figurmatrix wird ZEILENweise gelesen",
                enthaelt(f, "row_major float4x4 gFigurWelt"));

        // Der Karten-Shader darf davon NICHTS enthalten - sonst schleppte
        // jeder Kartenstapel einen ungenutzten Knochenzweig mit.
        const std::string k = vertexShaderHlsl(kNichts, 0);
        erwarte("der Karten-Shader kennt keine Knochen",
                !enthaelt(k, "gKnochen"));
    }

    // --- Die Gluehdurchgaenge ---------------------------------------------
    {
        const std::string v = vollbildVertexShaderHlsl();
        erwarte("das Vollbild-Dreieck braucht keinen Puffer",
                enthaelt(v, "SV_VertexID"));
        // Ein Dreieck, kein Viereck: keine Naht in der Mitte, ein
        // Zeichenaufruf weniger.
        // ": POSITION" mit Doppelpunkt - SV_POSITION enthaelt das Wort
        // ebenfalls, und danach zu suchen prueft nichts.
        erwarte("und kommt ohne Eingabelayout aus",
                !enthaelt(v, ": POSITION") && !enthaelt(v, "NORMAL"));

        // Ueberlauf wie im Rasterer: alle Kanaele durch den groessten, nicht
        // einzeln abschneiden (sonst wird Orange bei 2,0x zu Gelb).
        erwarte("der Pixelshader rechnet Ueberlauf proportional herunter",
                enthaelt(pixelShaderHlsl(kNichts), "c.rgb /= spitze"));

        const std::string sh = glowShrinkPixelShaderHlsl();
        // Kastenmittel wie shrink() in glow.cpp - nicht ein Abgriff je
        // Zielpunkt, sonst wird das Gluehen kantig und flimmert.
        erwarte("das Verkleinern mittelt ueber den ganzen Quellbereich",
                enthaelt(sh, "gVoll.Load") && enthaelt(sh, "for (int xx") &&
                    enthaelt(sh, "gQuelle"));

        const std::string b = glowBlurPixelShaderHlsl();
        erwarte("der Weichzeichner hat den Vier-Punkt-Kern",
                enthaelt(b, "sy += 2") && enthaelt(b, "sx += 2"));
        erwarte("und rechnet mit dem Gewicht", enthaelt(b, "gGewicht"));
        erwarte("und mit dem wachsenden Abstand", enthaelt(b, "gAbstand"));

        const std::string k = glowCompositePixelShaderHlsl();
        // soft: g + s*(1-g), hart: g + s - woertlich aus glow.cpp:255.
        erwarte("das Auflegen kennt beide Formeln",
                enthaelt(k, "(g + s * (1.0 - g))") && enthaelt(k, "(g + s)"));
        erwarte("und den Schalter dafuer", enthaelt(k, "gWeich"));
        erwarte("es liest die Szene UND das Gluehen",
                enthaelt(k, "gGlow.Sample") && enthaelt(k, "gSzene.Sample"));
    }

    // --- Der Konstantenblock steht in BEIDEN Shadern ---------------------
    //
    // Reihenfolge und Groesse muessen zum Puffer im Backend passen. Zwei
    // Fassungen davon waeren ein verschobenes Bild, das man lange sucht.
    {
        const std::string v = vertexShaderHlsl(kNichts, 0);
        const std::string p = pixelShaderHlsl(kNichts);
        erwarte("Vertex kennt gViewProj", enthaelt(v, "gViewProj"));
        // Ohne row_major liest Direct3D die Matrix spaltenweise, also
        // transponiert - siehe die Notiz im Shader.
        erwarte("und liest sie ZEILENweise",
                enthaelt(v, "row_major float4x4 gViewProj"));
        erwarte("beide kennen gZeit",
                enthaelt(v, "gZeit") && enthaelt(p, "gZeit"));
        // Die Kameraachsen brauchen beide Shader nicht - aber der
        // Konstantenblock ist EINER, also stehen sie in beiden.
        erwarte("beide kennen gVorn",
                enthaelt(v, "gVorn") && enthaelt(p, "gVorn"));
        erwarte("beide kennen gFokus",
                enthaelt(v, "gFokus") && enthaelt(p, "gFokus"));
        erwarte("beide kennen gHelligkeit",
                enthaelt(v, "gHelligkeit") && enthaelt(p, "gHelligkeit"));
        erwarte("und gGrundlicht",
                enthaelt(v, "gGrundlicht") && enthaelt(p, "gGrundlicht"));
        erwarte("beide kennen gAlphaSchwelle",
                enthaelt(v, "gAlphaSchwelle") && enthaelt(p, "gAlphaSchwelle"));
    }

    // --- Ohne Lightmap wird uvLm trotzdem gesetzt ------------------------
    //
    // Sonst steht dort Muell, und je nach Grafikkarte sieht man es oder
    // nicht - der schlimmste Fall: es faellt auf einem Rechner auf und auf
    // dem anderen nicht.
    {
        const std::string v = vertexShaderHlsl(kNichts, 0);
        erwarte("uvLm wird auch ohne Lightmap gesetzt",
                enthaelt(v, "aus.uvLm = float2(0.0, 0.0)"));
    }

    // --- Es gibt keine tcMod-Schleife mehr --------------------------------
    //
    // Bis rc442 lief der Shader die Kette Schritt fuer Schritt ab, und diese
    // Proben pruefte die Grenze der Schleife. Seit rc443 rechnet die CPU die
    // Kette zusammen (nach ComputeTexMods aus rd-rend2); im Shader steht nur
    // noch EINE affine Abbildung.
    //
    // Die Proben pruefen deshalb jetzt das Gegenteil: dass keine Schleife
    // mehr entsteht, egal wie viele Schritte gemeldet werden.
    {
        for (int n : {0, 1, 3, 8}) {
            const std::string v = vertexShaderHlsl(kTexMod, n);
            erwarte("keine tcMod-Schleife mehr",
                    !enthaelt(v, "gTexMod[i]") && !enthaelt(v, "for (int i"));
        }
        erwarte("dafuer die zusammengerechnete Abbildung",
                enthaelt(vertexShaderHlsl(kTexMod, 1), "gTexMatrix.x"));
        erwarte("die Zahl der Stufen aendert den Quelltext NICHT mehr",
                vertexShaderHlsl(kTexMod, 1) == vertexShaderHlsl(kTexMod, 4));
    }

    // --- Gleiche Bits, gleicher Quelltext --------------------------------
    //
    // Sonst kann das Backend Shader nicht wiederverwenden, und aus 24
    // Zustaenden wuerden 421 Uebersetzungen.
    {
        const std::uint32_t f = kLightmap | kTexMod | kAlphaTest;
        erwarte("gleiche Bits erzeugen denselben Quelltext",
                vertexShaderHlsl(f, 2) == vertexShaderHlsl(f, 2));
        erwarte("andere Bits erzeugen anderen Quelltext",
                vertexShaderHlsl(f, 2) != vertexShaderHlsl(f | kDeform, 2));
    }

    // --- Eckenfarbe und Lightmap schliessen einander AUS ------------------
    //
    // Der Rasterer nimmt entweder die Eckenfarbe oder die Lightmap
    // (mapview.cpp, `if (byVertex) ... else if (lm != nullptr)`), niemals
    // beides. Der Shader tat beides und war dadurch auf
    // lightmapbeleuchteten Flaechen um den Faktor 5 zu dunkel - an
    // md_am_sith.bsp nachgemessen: mittlere Eckenhelligkeit 46 von 255.
    {
        const std::string mitLm = pixelShaderHlsl(kLightmap);
        const std::string ohneLm = pixelShaderHlsl(kNichts);

        erwarte("mit Lightmap wird die Eckenfarbe NICHT auf rgb gerechnet",
                mitLm.find("ein.farbe.rgb") == std::string::npos);
        erwarte("mit Lightmap bleibt der Eckenalpha erhalten",
                mitLm.find("c.a *= ein.farbe.a;") != std::string::npos);
        erwarte("mit Lightmap wird die Lightmap aufgerechnet",
                mitLm.find("c.rgb *= lm;") != std::string::npos);

        // Seit rc443 in zwei Zeilen: die Farbe immer, der Alphawert nur
        // wenn alphaGen ihn zulaesst.
        erwarte("ohne Lightmap traegt die Eckenfarbe das Licht",
                ohneLm.find("c.rgb *= ein.farbe.rgb;") != std::string::npos);
        erwarte("und ihr Alphawert gilt ohne alphaGen ebenfalls",
                ohneLm.find("c.a *= ein.farbe.a;") != std::string::npos);
        erwarte("ohne Lightmap gibt es keine Lightmap-Abtastung",
                ohneLm.find("gLightmap.Sample") == std::string::npos);
    }

    // --- Rohe Eckenfarbe: kein Helligkeitsfaktor -------------------------
    //
    // Eine Effektfarbe kommt fertig aus dem rgb-Block der .efx. Der
    // Rasterer setzt dort den Faktor eins (`kVertexBoost = p.rohEcke
    // ? 1.0F : opt.brightness`); der GPU-Weg kannte den Unterschied nicht
    // und war bei shanks 2,0x doppelt so hell.
    {
        const std::string roh = pixelShaderHlsl(kRohEcke);
        const std::string normal = pixelShaderHlsl(kNichts);
        // Auf die VERWENDUNG pruefen, nicht auf das Wort: gHelligkeit
        // steht ohnehin in der Erklaerung des Konstantenpuffers. Die erste
        // Fassung dieser Probe suchte nach dem Wort und schlug fehl - zu
        // Recht.
        erwarte("rohe Eckenfarbe bekommt keinen Helligkeitsfaktor",
                roh.find("c.rgb * gHelligkeit") == std::string::npos);
        erwarte("gebackenes Licht in den Ecken schon",
                normal.find("c.rgb * gHelligkeit") != std::string::npos);
        erwarte("die Eckenfarbe wird in beiden Faellen benutzt",
                roh.find("c.rgb *= ein.farbe.rgb;") != std::string::npos &&
                normal.find("c.rgb *= ein.farbe.rgb;") != std::string::npos);
        erwarte("rohe Ecke hat einen EIGENEN Shadernamen",
                shaderName(kRohEcke, 0) != shaderName(kNichts, 0));
    }

    // --- rgbGen ERSETZT die Eckenfarbe, es multipliziert sie nicht -------
    //
    // Der Rasterer (mapview.cpp:1818): "rgbGen wave ERSETZT die
    // Vertexfarbe, sie multipliziert sie nicht." Der Shader hatte fuer die
    // Welle `*=` und fuer const `=`. An der Lava von md_am_sith gemessen:
    // Eckenhelligkeit 0,40, Welle 0,8 - der GPU-Weg rechnete 0,32 statt 0,8.
    {
        // Die rgbGen-Zeilen stehen im VERTEX-Shader.
        const std::string welle = vertexShaderHlsl(kRgbWave, 0);
        const std::string konst = vertexShaderHlsl(kRgbConst, 0);
        const std::string beides = vertexShaderHlsl(kRgbWave | kRgbConst, 0);
        const std::string ohne = vertexShaderHlsl(kNichts, 0);

        erwarte("rgbGen wave ersetzt",
                welle.find("aus.farbe.rgb = gRgb.rgb;") != std::string::npos);
        erwarte("rgbGen wave multipliziert NICHT",
                welle.find("aus.farbe.rgb *= gRgb.rgb;") == std::string::npos);
        erwarte("rgbGen const ersetzt",
                konst.find("aus.farbe.rgb = gRgb.rgb;") != std::string::npos);
        erwarte("ohne beides bleibt die Eckenfarbe unberuehrt",
                ohne.find("gRgb.rgb;") == std::string::npos);

        // Stehen beide, darf die Zeile nur EINMAL dastehen - sonst
        // ueberschriebe sie sich selbst, und bei einer kuenftigen Aenderung
        // liefe eine der beiden Fassungen davon.
        std::size_t n = 0;
        for (std::size_t i = beides.find("aus.farbe.rgb = gRgb.rgb;");
             i != std::string::npos;
             i = beides.find("aus.farbe.rgb = gRgb.rgb;", i + 1)) {
            ++n;
        }
        erwarte("bei beiden Bits steht die Zeile genau einmal", n == 1);
    }

    // --- Die Phase wird VOR dem Sinus gekuerzt ---------------------------
    //
    // `waveValue` im Rasterer: t = fraction(phase + zeit*freq), dann
    // shape(func, t). Der Shader warf den vollen Wert in den Sinus.
    //
    // An der Lavaflaeche von md_am_sith erreicht das Argument rund 630
    // (Ausdehnung 7168 x 9456 x 2078, deformSpread 1/180). Der Abstand
    // zweier benachbarter Ecken betraegt dort 0,053 Wellen - eine gut
    // darstellbare Welle, die aber nicht ueberlebt, wenn sie aus einem
    // Argument von 630 herausgerechnet werden muss.
    {
        const std::string d = vertexShaderHlsl(kDeform, 0);
        erwarte("deformVertexes kuerzt die Phase vor dem Sinus",
                d.find("frac(gDeform[2] + off") != std::string::npos);
        erwarte("und wirft nicht mehr den vollen Wert hinein",
                d.find("sin((gDeform[2] + off") == std::string::npos);

        // turb steckt jetzt in derselben Abbildung, mit `ort` statt der
        // ausgeschriebenen Koordinaten - gekuerzt wird weiter.
        const std::string t = vertexShaderHlsl(kTexMod, 1);
        erwarte("tcMod turb kuerzt ebenfalls",
                t.find("frac(ort * 0.0009765625") != std::string::npos);
    }

    // --- alphaGen: was VOR dem Alphakanal der Textur steht ---------------
    {
        const std::string konst = pixelShaderHlsl(kAlphaKonst);
        const std::string eins = pixelShaderHlsl(kAlphaEins);
        const std::string vorgabe = pixelShaderHlsl(kNichts);

        erwarte("alphaGen const nimmt den festen Wert",
                konst.find("c.a *= gRgb.a;") != std::string::npos);
        erwarte("alphaGen const nimmt NICHT den Eckenwert",
                konst.find("c.a *= ein.farbe.a;") == std::string::npos);
        erwarte("alphaGen identity laesst den Eckenwert weg",
                eins.find("c.a *= ein.farbe.a;") == std::string::npos &&
                eins.find("c.a *= gRgb.a;") == std::string::npos);
        erwarte("ohne alphaGen gilt der Eckenwert",
                vorgabe.find("c.a *= ein.farbe.a;") != std::string::npos);
        // Die drei Faelle muessen sich unterscheiden - sonst waere einer
        // wirkungslos, so wie kSpecular es war.
        erwarte("die drei Faelle erzeugen drei verschiedene Shader",
                konst != eins && eins != vorgabe && konst != vorgabe);
        erwarte("und drei verschiedene Namen",
                shaderName(kAlphaKonst, 0) != shaderName(kAlphaEins, 0) &&
                shaderName(kAlphaKonst, 0) != shaderName(kNichts, 0));
    }

    // --- Die Figur wird beleuchtet ---------------------------------------
    //
    // Der Figurenshader drehte die Normale ueber die Knochen und warf sie
    // dann weg: `aus.farbe = float4(1, 1, 1, 1)`. Figuren waren damit
    // voellig flach - Textur mal Helligkeit, ohne Schattierung. Die
    // Battledroids in md_am_sith sahen deshalb wie schwarze Silhouetten aus
    // und mit Gluehen wie weisse.
    //
    // Die Formel muss die des Rasterers sein (mapview.cpp:3362):
    //     shade = 0.22 + 0.78 * |dot(normale, licht)|
    {
        const std::string f = figurVertexShaderHlsl(kNichts);
        // Auf die ZUWEISUNG pruefen, nicht auf die Zeichenkette: der
        // alte Ausdruck steht noch im Kommentar darueber, und ein Pruefer,
        // der Prosa mitzaehlt, prueft nichts. Genau daran ist
        // lint_merkmalsbits.py zweimal gescheitert.
        erwarte("die Figur wird nicht mehr pauschal weiss",
                f.find("    aus.farbe = float4(1, 1, 1, 1);") ==
                    std::string::npos);
        erwarte("die gedrehte Normale wird benutzt - in die Welt gedreht",
                f.find("float3 nw = mul((float3x3)gFigurWelt, nrm);") != std::string::npos);
        erwarte("eine Normale der Laenge null gibt kein NaN",
                f.find("float3 n = (nl > 1e-6) ? nw / nl : float3(0, 0, 0);") != std::string::npos);
        erwarte("mit Lichtgitter: Umgebung + max(0, n.l) * gerichtet",
                f.find("gLichtUmgebung.rgb + ein * gLichtGerichtet.rgb") != std::string::npos &&
                    f.find("float ein = max(0.0, dot(n, gLichtRichtung.xyz));") != std::string::npos);
        erwarte("ohne Lichtgitter die alte Formel",
                f.find("0.22 + 0.78 * abs(dot(n, licht))") !=
                    std::string::npos);
        // Der Betrag ist Absicht: eine Figur, die sich dreht, soll nicht
        // zur Haelfte schwarz werden.
        erwarte("mit BETRAG, nicht mit saturate",
                f.find("abs(dot(n, licht))") != std::string::npos);
    }

    // --- Der Pixelshader der Figuren hellt NICHT auf ---------------------
    //
    // Der Rasterer rechnet fuer eine Figur `Textur x shade`
    // (mapview.cpp:3382), ohne `opt.brightness`. Der Pixelshader mit
    // `kNichts` multipliziert mit `gHelligkeit` - bei 2,0 wird damit alles
    // ueber shade 0,5 auf Weiss geklemmt, und die Beleuchtung aus rc454
    // war unsichtbar.
    //
    // `kRohEcke` ist genau die Aussage "diese Eckenfarbe ist fertig".
    {
        const std::string figur = pixelShaderHlsl(kRohEcke);
        erwarte("mit kRohEcke wird die Eckenfarbe nicht aufgehellt",
                figur.find("c.rgb * gHelligkeit") == std::string::npos);
        erwarte("die Eckenfarbe wird aber benutzt",
                figur.find("c.rgb *= ein.farbe.rgb;") != std::string::npos);
    }

    // --- Der Name ist eindeutig und stabil -------------------------------
    {
        const std::uint32_t f = kLightmap | kAlphaTest;
        erwarte("Name ist stabil", shaderName(f, 1) == shaderName(f, 1));
        erwarte("Name enthaelt die Kuerzel",
                shaderName(f, 0) == std::string("fx_lm_at"));
        erwarte("Name unterscheidet die tcMod-Zahl",
                shaderName(kTexMod, 1) != shaderName(kTexMod, 2));
        erwarte("verschiedene Bits, verschiedene Namen",
                shaderName(kDeform, 0) != shaderName(kAutosprite, 0));
    }

    if (fehler != 0) {
        std::printf("FEHLGESCHLAGEN (%d)\n", fehler);
        return 1;
    }
    std::printf("alle Gegenproben bestanden (0 Fehlschlaege)\n");
    return 0;
}
