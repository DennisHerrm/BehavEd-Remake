// shaderscript.h - aus einem Shadernamen eine Bilddatei machen
//
// In der .bsp steht bei jeder Flaeche nur ein Name, etwa
// "textures/yavin/stone". Der kann zweierlei bedeuten:
//
//   1. Es gibt eine Datei textures/yavin/stone.jpg (oder .tga, .png).
//      Das ist der haeufigere Fall und braucht gar nichts weiter.
//   2. Es gibt einen Eintrag in einer .shader-Datei unter scripts/, der
//      sagt, welche Bilder in welchen Stufen benutzt werden. Dann steht das
//      eigentliche Bild in einer "map"-Zeile.
//
// Wir suchen erst nach 1 und greifen nur bei Bedarf auf 2 zurueck. Von den
// Shaderstufen interessiert nur die ERSTE mit einem echten Bild - alles
// weitere (Mischmodi, Bewegung, Leuchten) gehoert in einen Spielrenderer,
// nicht in eine Editoransicht.
#ifndef BHED_SHADERSCRIPT_H
#define BHED_SHADERSCRIPT_H

#include "bhed/wave.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bhed {

// Was wir aus einem Shader mitnehmen.
//
// Bisher war das nur der Pfad des ersten brauchbaren Bildes. Fuer eine
// Vorschau reicht das nicht ganz: eine Lava, die stillsteht, sieht falsch
// aus, obwohl das Bild stimmt.
//
// Mitgenommen wird jetzt zusaetzlich "tcMod scroll" - die haeufigste
// Bewegung in den Shadern der Mod und die, die man sofort sieht. Die
// Rechnung steht in tr_shade_calc.cpp, RB_CalcScrollTexCoords():
//
//     adjustedScrollS = scrollSpeed[0] * floatTime;
//     adjustedScrollS = adjustedScrollS - floor( adjustedScrollS );
//     st[0] += adjustedScrollS;
//
// NICHT mitgenommen (und das ist eine bewusste Grenze, keine Vergesslichkeit):
//   deformVertexes   die Flaeche selbst wogt - braucht Geometrieaenderung
//   rgbGen wave      die Helligkeit pulst
//   blendFunc        additives und durchscheinendes Mischen
//   animMap          eine Bildfolge statt eines Bildes
//   mehrere Stufen   uebereinandergelegte Lagen
// Alles davon gehoert in einen Spielrenderer. Der Softwarezeichner hier
// kann nur deckend, eine Lage, feste Geometrie.
// Wie wird die Stufe mit dem Untergrund verrechnet?
//
// Aus blendFunc der Stufe, aus der auch das Bild kommt. Die Engine
// unterscheidet feiner (tr_shader.cpp kennt ein Dutzend Faktoren); fuer
// eine Vorschau genuegen die drei, die den Bildeindruck bestimmen:
//
//   Opaque    ohne blendFunc, oder GL_ONE GL_ZERO. Deckend.
//   Add       GL_ONE GL_ONE - das Bild wird DAZUGEZAEHLT. So entstehen
//             Hologramme, Feuer, Leuchten: Schwarz ist unsichtbar, Helles
//             leuchtet ueber allem.
//   Alpha     GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA - durchscheinend nach
//             dem Alphakanal des Bildes. Glas, Rauch, Gitter.
//   Filter    GL_DST_COLOR GL_ZERO - der Untergrund wird MULTIPLIZIERT.
//
// Filter braucht eine Erklaerung, weil er auf 373 der 678 Shader aus
// MD_Maps_Ep3 zutrifft und trotzdem nichts Besonderes bedeutet. Das
// haeufigste Muster ist naemlich dies:
//
//     { map $lightmap }
//     { map textures/x/y   blendFunc GL_DST_COLOR GL_ZERO }
//
// Das ist Lightmap MAL Textur - also eine ganz gewoehnliche beleuchtete
// Flaeche, kein durchscheinender Effekt. Genau diese Multiplikation macht
// unser Zeichner ohnehin selbst, mit der Lightmap aus der .bsp. Filter
// wird deshalb wie DECKEND behandelt.
//
// Ein echter Filter ueber Weltgeometrie (ein einfaerbendes Abziehbild)
// waere damit falsch - aber der ist selten, und ihn faelschlich deckend zu
// zeichnen faellt weit weniger auf, als 373 normale Waende durchscheinend
// zu machen.
//
// Die Reihenfolge ist nicht beliebig. tr_shader.cpp, Zeile 3172:
//     if (( blendSrcBits == GLS_SRCBLEND_ONE ) && ( blendDstBits == GLS_DSTBLEND_ONE ))
//         shader.sort = SS_BLEND1;   // "GL_ONE GL_ONE needs to come a bit later"
//     else
//         shader.sort = SS_BLEND0;
// Additives kommt also NACH dem uebrigen Durchscheinenden.
// AddAlpha braucht eine eigene Begruendung, weil es weder Add noch Alpha ist.
//
//   GL_SRC_ALPHA GL_ONE   ->  ziel = ziel + quelle * a
//
// Additiv, aber mit dem Alphakanal GEWICHTET. Das ist die uebliche Art, ein
// Glanzlicht oder ein Leuchten UEBER eine vorhandene Textur zu legen: wo der
// Alphakanal null ist, passiert nichts; wo er voll ist, leuchtet es wie
// GL_ONE GL_ONE.
//
// behaved stufte es bisher als `Alpha` ein - also als DAEMPFEND. Eine
// leuchtende Stufe wurde damit zu einem Schleier, der das Darunterliegende
// wegnimmt statt etwas hinzuzufuegen.
//
// Gemessen ueber die sechs pk3: 585 Bildstufen tragen diese Mischart. Wie
// viele davon behaved wirklich richtig zeichnen KANN, ist eine andere und
// viel kleinere Zahl - siehe die Notiz bei den Zusatzstufen in gui/app.cpp.
enum class BlendMode : std::uint8_t { Opaque, Alpha, Add, AddAlpha, Filter };

// --- Die beiden Faktoren von glBlendFunc, einzeln -----------------------
//
// BlendMode fasst zusammen; das reicht fuer die Sortierung, aber nicht zum
// Rechnen. Die Engine kennt 9 Quell- und 8 Zielfaktoren
// (NameToSrcBlendMode :314, NameToDstBlendMode :361), und in den sechs pk3
// stehen davon 14 verschiedene KOMBINATIONEN auf Zusatzstufen - ein langer
// Schwanz, den man nicht mit fuenf Schubladen erwischt:
//
//     289  GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
//     109  GL_ONE GL_SRC_ALPHA
//      51  GL_DST_COLOR GL_SRC_COLOR
//      46  GL_SRC_ALPHA GL_SRC_ALPHA
//      42  GL_DST_COLOR GL_ONE
//      ... und neun weitere
//
// Jede einzeln nachzubauen waere Schubladenzaehlen ohne Ende. Stattdessen
// wird gerechnet, was OpenGL rechnet:
//
//     ergebnis = quelle * srcFaktor + ziel * dstFaktor
//
// Damit sind Opaque/Alpha/Add/AddAlpha/Filter nur noch Namen fuer fuenf
// haeufige Paare, keine Sonderfaelle mehr.
enum class BlendFactor : std::uint8_t {
    One,
    Zero,
    SrcColor,
    OneMinusSrcColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstColor,
    OneMinusDstColor,
    DstAlpha,
    OneMinusDstAlpha,
    // GL_SRC_ALPHA_SATURATE = min(As, 1-Ad). Unser Bildspeicher hat keinen
    // brauchbaren Zielalphakanal; nach dem Buchstaben waere Ad = 1 und der
    // Faktor damit NULL, die Stufe also unsichtbar. Weil das mit Sicherheit
    // nicht gemeint ist und die Mischart in den sechs pk3 KEIN EINZIGES Mal
    // vorkommt, wird sie wie SrcAlpha behandelt - festgehalten, damit es
    // niemand fuer gemessen haelt.
    SrcAlphaSaturate,
};

// Der ALPHATEST einer Stufe.
//
// Gemeldet: "diese Sachen werden immer mit schwarzem Hintergrund gerendert,
// obwohl sie eigentlich transparent sein sollten."
//
// `blendFunc` allein reicht nicht. Die haeufigste Art, ein Gitter, ein
// Blatt oder ein Abziehbild durchsichtig zu machen, ist NICHT Mischen,
// sondern WEGWERFEN: `alphaFunc` verwirft jeden Bildpunkt, dessen
// Alphawert die Schwelle nicht erreicht.
//
// Und das Tueckische daran: eine solche Stufe ist formal DECKEND. Aus den
// Basis-Shadern von JKA:
//
//     alphaFunc GE192
//     blendFunc GL_ONE GL_ZERO      <- das ist "deckend"
//     depthWrite
//
// Wer nur auf blendFunc sieht, haelt sie fuer eine gewoehnliche Wand und
// zeichnet den schwarzen Grund mit. In den Shadern aus assets1 steht
// alphaFunc 172 mal.
//
// Die Schwellen stehen in tr_shader.cpp:287 ff.
enum class AlphaTest : std::uint8_t {
    None,
    Gt0,     // GT0    - alles ueber null
    Lt128,   // LT128  - alles UNTER 128 (selten, aber es gibt es)
    Ge128,   // GE128
    Ge192,   // GE192
};

// Welche Seiten einer Flaeche werden gezeichnet?
//
// Aus dem Schluesselwort "cull" im Shader. tr_shader.cpp, Zeile 2378:
//
//     if ( !Q_stricmp( token, "none" ) || !Q_stricmp( token, "twosided" )
//          || !Q_stricmp( token, "disable" ) )
//         shader.cullType = CT_TWO_SIDED;
//     else if ( !Q_stricmp( token, "back" ) || "backside" || "backsided" )
//         shader.cullType = CT_BACK_SIDED;
//
// Ohne Angabe gilt CT_FRONT_SIDED - das Uebliche.
//
// Das ist kein Randfall: allein in den Shadern von MD_Maps_Ep3 stehen 117
// mal "cull twosided". Gitter, Pflanzen, Fahnen, Vorhaenge - alles, was aus
// einer einzelnen Flaeche besteht und von beiden Seiten zu sehen sein soll.
// Wer sie einseitig zeichnet, bei dem verschwindet die halbe Einrichtung,
// je nachdem, wo die Kamera steht.
enum class CullMode : std::uint8_t { Front, Two, Back };

// Woher kommen die Texturkoordinaten dieser Stufe?
//
// tcGen (auch texgen geschrieben) kennt vier Quellen:
// environment, lightmap, texture/base, vector. Nur die erste aendert
// wirklich etwas gegenueber dem, was in der .bsp steht, und nur sie kommt
// haeufig vor - 364 Shader in den sechs pk3.
//
//     RB_CalcEnvironmentTexCoords, tr_shade_calc.cpp:902
//
//         viewer = normalize( viewOrigin - vertex )
//         d      = dot( normal, viewer )
//         st[0]  = normal[0]*d - 0.5*viewer[0]
//         st[1]  = normal[1]*d - 0.5*viewer[1]
//
// Nur x und y der beiden Vektoren, z faellt weg. Das ist die eigenwillige
// Q3-Formel und sie wird hier absichtlich WOERTLICH uebernommen, statt sie
// zu einer "richtigen" Spiegelung zu verbessern: was zaehlt, ist die
// Uebereinstimmung mit dem Spiel, nicht die Optik fuer sich.
//
// Das Ergebnis liegt etwa in -1.5 bis 1.5 und wird NICHT auf 0..1 gebracht.
// Die Engine verlaesst sich auf GL_REPEAT, behaved wiederholt ohnehin.
enum class TexGen : std::uint8_t { Base, Environment };

// Der Tiefenvergleich dieser Stufe.
//
// ParseStage (tr_shader.cpp:1206) kennt drei Werte:
//
//     lequal    die Vorgabe - naeher oder gleich weit
//     equal     NUR gleich weit. Der uebliche Weg fuer eine zweite
//               Beleuchtungsschicht auf derselben Flaeche.
//     disable   kein Vergleich, die Stufe zeichnet immer
//
// Ohne `equal` z-flimmern Mehrfachschichten gegeneinander; ohne `disable`
// verschwindet, was absichtlich durch Waende scheinen soll.
enum class DepthFunc : std::uint8_t { LEqual, Equal, Disable };

// --- tcMod: eine KETTE, keine Einzelwerte -------------------------------
//
// Eine Stufe darf bis zu vier tcMod tragen (TR_MAX_TEXMODS, tr_local.h:279),
// und sie werden in der Reihenfolge der Datei nacheinander angewandt
// (ComputeTexCoords, tr_shade.cpp:1658).
//
// Die Reihenfolge ist kein Detail. Gemessen ueber die neun pk3:
//
//     750 Stufen mit einem tcMod
//     304 mit zwei
//      62 mit drei
//       7 mit vier
//
// und darunter 80-mal `scroll + scale` gegen 57-mal `scale + scroll`.
// Beides ergibt etwas anderes: einmal wird der schon verschobene Wert
// gestaucht, einmal der gestauchte verschoben. Mit Einzelfeldern statt
// einer Kette laesst sich das nicht auseinanderhalten - deshalb loest diese
// Kette das bisherige `scroll[2]` ab.
enum class TexModKind : std::uint8_t {
    Scroll,
    Scale,
    Rotate,
    Stretch,
    Turb,
    Transform,
};

struct TexMod {
    TexModKind kind = TexModKind::Scroll;
    // Bedeutung je nach `kind`:
    //   Scroll     a[0], a[1]  Geschwindigkeit
    //   Scale      a[0], a[1]  Faktor
    //   Rotate     a[0]        Grad je Sekunde
    //   Transform  a[0..3]     Matrix, a[4], a[5] Verschiebung
    //   Stretch    -           siehe `wave`
    //   Turb       -           siehe `wave`
    float a[6]{0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    Wave wave;
};

// TR_MAX_TEXMODS, tr_local.h:279. Mehr laesst die Engine nicht zu, und in
// den pk3 steht nie mehr.
inline constexpr int kMaxTexMods = 4;

// Woher kommt der Alphawert dieser Stufe?
//
// ComputeColors (tr_shade.cpp:1369) kennt zwoelf Formen. behaved rechnet
// zwei davon:
//
//   FromImage    keine alphaGen-Zeile - der Alphakanal des Bildes gilt
//   Const        alphaGen const <x> - ein fester Wert, siehe alphaConst
//
// Alles Uebrige ist NICHT nachgebaut, und das ist der Grund, warum es hier
// eine eigene Stufe bekommt statt stillschweigend als FromImage zu gelten.
// `alphaGen lightingSpecular` etwa ERSETZT den Alphakanal durch ein aus
// Licht- und Blickrichtung gerechnetes Glanzlicht; die Stufe stattdessen
// mit dem Bildalpha zu zeichnen ist kein Naeherungswert, sondern etwas
// anderes - ein gleichmaessiger Schleier statt eines wandernden Glanzes.
//
// Wer eine solche Stufe zeichnet, macht das Bild nicht ungenauer, sondern
// falsch. Deshalb wird sie lieber weggelassen und gemeldet.
enum class AlphaGen : std::uint8_t {
    // Kein alphaGen in der Datei: der Alphakanal des Bildes gilt.
    //
    // --- Und hier weicht behaved bewusst ab -----------------------------
    //
    // Die Engine nimmt ohne alphaGen AGEN_IDENTITY, also Alpha eins - die
    // Eckenfarbe spielt keine Rolle. behaved multipliziert zusaetzlich mit
    // dem Alphawert der Ecke, verhaelt sich also wie `alphaGen vertex`.
    //
    // Gemessen ueber die 43 Karten: KEINE einzige Flaeche hat einen
    // Eckenalphawert ungleich 255 ohne `alphaGen vertex` dabeizustehen.
    // Der Unterschied betrifft dort also null Flaechen.
    //
    // Warum es trotzdem so bleibt: der Effektzeichner legt die Blende in
    // den Alphakanal der ECKE (`useAlpha`, 278 Vorkommen). Waere die
    // Vorgabe `Identity`, ginge sie verloren - und das laesst sich hier
    // nicht nachmessen, weil es keinen Prueflauf fuer Effektbilder gibt.
    //
    // Damit die Umstellung spaeter eine Zeile bleibt, tragen die
    // Effekttexturen ihr `Vertex` seit rc361 ausdruecklich.
    FromImage,
    Const,
    // alphaGen lightingSpecular - ein Glanzlicht aus Licht- und
    // Blickrichtung.
    //
    // Sah lange nach "nicht nachbaubar" aus, weil ein Glanzlicht eine
    // Lichtquelle braucht und behaved keine hat. Der Quelltext sagt etwas
    // anderes: fuer WELTFLAECHEN nimmt die Engine eine feste Position,
    //
    //     tr_shade_calc.cpp:1044
    //     vec3_t lightOrigin = { -960, 1980, 96 };  // FIXME: track dynamically
    //
    // eine Konstante mit einem FIXME daneben, seit Quake 3. Damit ist die
    // Rechnung vollstaendig bestimmt und braucht nichts, was wir nicht
    // haben. Man kommt nur darauf, wenn man nachsieht - raten wuerde hier
    // niemand.
    //
    // (Fuer MODELLE nimmt die Engine `currentEntity->lightDir` aus dem
    // Lichtgitter. Das betrifft uns nicht: der Kartenzeichner zeichnet
    // keine Modelle.)
    //
    // Gemessen: 482 Shader der neun pk3, in den 43 Karten auf 2090
    // Flaechen - der groesste Posten, der noch offen war.
    LightingSpecular,
    // alphaGen vertex - der Alphawert der ECKE aus der .bsp.
    //
    // ComputeColors (tr_shade.cpp:204): svars.colors[i][3] =
    // vertexColors[i][3]. Der Alphakanal der Textur multipliziert danach
    // wie immer.
    //
    // Das ist der Terrainverlauf von q3map2: zwei Bodentexturen
    // uebereinander, gemischt ueber die Eckenfarbe. In den 43 Karten steht
    // es auf 204 Flaechen.
    Vertex,
    // alphaGen identity - Alpha eins, die Eckenfarbe wird IGNORIERT.
    //
    // Das ist die Vorgabe der Engine, wenn gar kein alphaGen dasteht. Bei
    // behaved ist die Vorgabe eine andere - siehe die Notiz bei
    // FromImage.
    Identity,
    Unsupported,
};

struct ShaderInfo {
    std::string image;            // erstes brauchbares Bild
    // Die tcMod-Kette der Stufe, aus der auch das Bild kommt - siehe TexMod.
    // Loest das fruehere `scroll[2]` ab: mit Einzelfeldern liess sich die
    // Reihenfolge mehrerer tcMod nicht auseinanderhalten.
    TexMod texMods[kMaxTexMods];
    int numTexMods = 0;
    BlendMode blend = BlendMode::Opaque;
    // Wie viele Bildstufen behaved LIEST - nicht, wie viele der Shader hat.
    //
    // Gemeldet: "der Tisch und das Modell dahinter sehen so komisch aus,
    // ingame sieht das anders aus."
    //
    // Ein Hologramm ist typisch mehrstufig - der Todesstern in
    // epiii_boc.shader hat VIER Stufen, alle `blendFunc GL_ONE GL_ONE`.
    // behaved zeichnet aber nur EINE davon, und zwar die letzte gefundene.
    //
    // Beim Bauen dieser Zahl kam heraus, dass es noch schlimmer ist:
    // fuer einen Shader mit ZWEI Stufen meldet der Leser **eine**. Er
    // verarbeitet also gar nicht alle Stufen, sondern steigt nach der
    // ersten aus.
    //
    // Das ist der eigentliche Befund und noch nicht behoben - die Zahl
    // steht hier, damit im Protokoll sichtbar ist, welche Flaechen davon
    // betroffen sind.
    int imageStages = 0;
    // --- alphaGen const --------------------------------------------------
    //
    // Gemeldet: "der Tisch und das Modell dahinter sehen komisch aus."
    //
    // Aus `plasma_Mustafar.shader`, dem Shader dieses Tisches:
    //
    //     textures/plasma_mustafar/holo1
    //     {
    //         map textures/plasma_mustafar/holo1
    //         blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
    //         alphaGen const 0.4
    //         ...
    //     }
    //
    // **`alphaGen const 0.4`** - die Flaeche ist zu 40 Prozent
    // durchsichtig, unabhaengig davon, was im Bild steht.
    //
    // behaved kannte den Befehl nicht und nahm den Alphawert allein aus
    // dem Bild. Ist das Bild deckend, wird die Flaeche deckend - genau der
    // harte blaue Block statt eines Hologramms.
    //
    // Negativ heisst: nicht gesetzt, es gilt der Wert aus dem Bild.
    float alphaConst = -1.0F;

    // deformVertexes autosprite: jedes Viereck wird durch ein
    // kamerazugewandtes Plaettchen um seinen Mittelpunkt ersetzt
    // (AutospriteDeform, tr_shade_calc.cpp:377).
    //
    // Gemeldet: eine grosse blaue Scheibe quer durchs Bild. Die
    // Holo-Flaechen sind als grosse Rechtecke gebaut und sollen zur
    // Laufzeit zu Sprites zusammenklappen.
    bool autosprite = false;

    // --- ALLE Bildstufen, in der Reihenfolge der Datei -------------------
    //
    // Gemessen (rc341): `table_blue` in plasma_Mustafar.shader hat drei:
    //
    //     { map $lightmap }
    //     { map table_blue   blendFunc GL_DST_COLOR GL_ZERO }
    //     { map table_blue   blendFunc GL_ONE GL_ONE   glow }
    //
    // behaved benutzte bisher nur die LETZTE - rein additiv, ohne die
    // daempfende zweite. Das ist das helle Viereck am Holotisch.
    //
    // Die Felder darunter (`image`, `blend`, `alphaTest`, ...) bleiben
    // unveraendert: sie sind weiterhin die letzte Stufe, und alles, was
    // heute damit arbeitet, arbeitet unveraendert weiter. Diese Liste
    // kommt DANEBEN dazu, damit der Umbau in Schritten geht statt in einem
    // Sprung.
    //
    // Eine `$lightmap`-Stufe steht mit leerem `image` drin - sie bringt
    // kein eigenes Bild, aber sie belegt einen Platz in der Reihenfolge,
    // und die Reihenfolge entscheidet beim Uebereinanderlegen.
    struct Stage {
        std::string image;        // leer bei $lightmap
        bool lightmap = false;    // map $lightmap
        BlendMode blend = BlendMode::Opaque;
        AlphaTest alphaTest = AlphaTest::None;
        float alphaConst = -1.0F;
        // Woher der Alphawert kommt - siehe AlphaGen.
        AlphaGen alphaGen = AlphaGen::FromImage;
        // --- depthWrite: Tiefe schreiben, obwohl gemischt wird ----------
        //
        // Die Engine LOESCHT die Tiefenmaske fuer jede Stufe mit blendFunc
        // (tr_shader.cpp:1444) - ausser `depthwrite` stand ausdruecklich da.
        //
        // Und dann hat es eine zweite Wirkung, die man leicht uebersieht:
        // ein durchscheinender Shader, dessen erste Stufe die Tiefe
        // schreibt, wird als SS_SEE_THROUGH einsortiert
        // (tr_shader.cpp:3165) - also gleich hinter das Deckende statt zu
        // den Mischstufen. Der Kommentar im Quelltext nennt den Fall beim
        // Namen: "see through item, like a grill or grate".
        //
        // Ein Gitter soll eben nicht wie Rauch behandelt werden.
        bool depthWrite = false;
        DepthFunc depthFunc = DepthFunc::LEqual;
        // rgbGen const ( r g b ) - eine feste Einfaerbung, 0..1 je Kanal.
        // Negativ heisst: nicht gesetzt.
        //
        // ComputeColors (tr_shade.cpp:1369) schreibt bei CGEN_CONST den
        // Wert DIREKT in die Eckenfarbe, genau wie bei `rgbGen wave` - er
        // multipliziert die Vertexfarbe nicht, er ERSETZT sie. Die Textur
        // wird danach wie immer daraufgerechnet.
        //
        // Gemessen: 209 Shader der neun pk3, und in den 43 Karten liegen
        // sie auf 7721 Flaechen - der groesste einzelne Posten, der bisher
        // stumm verworfen wurde.
        float rgbConst[3]{-1.0F, -1.0F, -1.0F};
        // rgbGen vertex / exactVertex / oneMinusVertex / lightingDiffuse:
        // die Farbe kommt aus den Ecken. Siehe ShaderInfo::rgbVertex.
        bool rgbVertex = false;
        // `clampmap` statt `map`: mit GL_CLAMP geladen, nicht GL_REPEAT.
        // Die Textur laeuft am Rand in ihre Randfarbe aus, statt
        // zurueckzuspringen. 143 Shader der neun pk3, in den 43 Karten
        // auf 3314 Flaechen.
        bool clamp = false;
        // Die beiden Faktoren aus glBlendFunc - siehe BlendFactor.
        // Vorgabe GL_ONE, GL_ZERO: deckend, also gar kein Mischen.
        BlendFactor srcFactor = BlendFactor::One;
        BlendFactor dstFactor = BlendFactor::Zero;
        // Die tcMod-Kette dieser Stufe, in der Reihenfolge der Datei.
        TexMod texMods[kMaxTexMods];
        int numTexMods = 0;
        // --- animMap: ALLE Bilder, nicht nur das erste -------------------
        //
        //     animMap <bilder je Sekunde> <bild1> <bild2> ...
        //
        // `image` traegt weiterhin das erste - das ist das, was ohne Zeit
        // gilt. Hier stehen alle, damit sich die Folge auch bewegen kann.
        //
        // R_BindAnimatedImage (tr_shade.cpp:224) waehlt so aus:
        //
        //     index = Q_ftol( floatTime * speed * FUNCTABLE_SIZE )
        //     index >>= FUNCTABLE_SIZE2          // also / 1024
        //     if (index < 0) index = 0
        //     index %= numImageAnimations
        //
        // Der Umweg ueber 1024 und zurueck ist keine Zierde: er schneidet
        // ab, statt zu runden, und das verschiebt den Bildwechsel.
        //
        // Gemessen: 186 Shader der neun pk3, in den 43 Karten auf 423
        // Flaechen.
        std::vector<std::string> animImages;
        float animFreq = 0.0F;
        // `oneshotanimMap`: die Folge laeuft EINMAL und bleibt auf dem
        // letzten Bild stehen (bundle->oneShotAnimMap, R_BindAnimatedImage
        // klemmt statt Modulo). `clampanimMap` setzt dagegen nur `clamp`.
        bool animOneShot = false;
        Wave rgbWave;
        // tcGen dieser Stufe. Siehe TexGen.
        TexGen texGen = TexGen::Base;
        // --- glow: diese Stufe geht in den Gluehdurchgang ----------------
        //
        // JKA-eigen, in keinem Q3-Handbuch. Die Engine zeichnet die Szene
        // ein ZWEITES Mal, laesst dabei jede Stufe OHNE `glow` weg
        // (tr_shade.cpp:1877) und jeden Shader ohne `hasGlow`
        // (tr_backend.cpp:690), zeichnet das Ergebnis weich und legt es
        // ueber das Bild.
        //
        // Uebersprungen wird die STUFE, nicht die Flaeche. Bei `table_blue`
        // landet also nur die dritte Stufe im Gluehpuffer, die Lightmap und
        // die daempfende zweite bleiben draussen. Deshalb steht das Merkmal
        // hier und nicht nur am Shader.
        //
        // Gemessen ueber die sechs pk3: 1039 der 4457 Shader tragen `glow`,
        // bei MD_Maps_Ep3 sind es 342 von 736.
        bool glow = false;
    };
    std::vector<Stage> stages;

    // tcGen der Stufe, aus der auch das Bild kommt.
    TexGen texGen = TexGen::Base;
    // `glow` der Stufe, aus der auch das Bild kommt - dieselbe Stufe, aus
    // der `blend`, `alphaTest` und `rgbWave` stammen.
    bool glow = false;
    // Traegt IRGENDEINE Stufe `glow`? Entspricht shader.hasGlow der Engine.
    // Wird gebraucht, weil der Gluehdurchgang ganze Flaechen ueberspringen
    // darf, deren Shader gar nichts beitraegt - das spart bei md_am_sith
    // den groessten Teil der Arbeit.
    bool hasGlow = false;

    // rgbGen const der Stufe, aus der auch das Bild kommt.
    float rgbConst[3]{-1.0F, -1.0F, -1.0F};

    // --- Woher das Licht einer geskripteten Flaeche kommt ----------------
    //
    // In der Engine (tr_shader.cpp) bringt NUR eine `$lightmap`-Stufe die
    // Lightmap. Auf einer vertexbeleuchteten Flaeche (-3) wird sie durch
    // `rgbGen exactVertex` ersetzt (FinishShader). Die Eckenfarbe gilt
    // sonst nur mit `rgbGen vertex/exactVertex/...`. Ohne rgbGen setzt
    // ParseStage `identity` bzw. `identityLighting`: kein Licht.
    //
    // Gemeldet: das blaue Schild am Rohr in md_am_sith
    // (`textures/plasma_mustafar/blue_gradient`, rgbGen const hellblau,
    // q3map_nolightmap) kam rot - behaved nahm die roten Eckenfarben vom
    // Lavalicht statt der festen Farbe.
    bool rgbVertex = false;       // Bildstufe mit rgbGen vertex & Co.
    bool lightmapStufe = false;   // irgendeine Stufe ist `map $lightmap`

    // --- skyParms: die Himmelsbox ---------------------------------------
    //
    //     skyParms <aussenbox> <wolkenhoehe> <innenbox>
    //
    // ParseSkyParms (tr_shader.cpp) haengt sechs Endungen an den Namen, in
    // GENAU dieser Reihenfolge:
    //
    //     rt  lf  bk  ft  up  dn
    //
    // Sie ist nicht alphabetisch und nicht die uebliche - `bk` steht VOR
    // `ft`. Wer sie raet, bekommt eine Box, die an zwei Seiten vertauscht
    // ist, und merkt es nur, wenn er sich im Spiel umdreht.
    //
    // Ein `-` heisst: keine Aussenbox. Die Innenbox ist in JKA nicht
    // unterstuetzt (die Engine warnt selbst), die Wolkenhoehe brauchen wir
    // ohne Wolkenschichten nicht.
    //
    // Gemessen: 130 skyParms-Zeilen in den neun pk3, davon 23 mit `-`;
    // 56 verschiedene Boxen.
    std::string skyBox;

    // depthWrite und depthFunc der Stufe, aus der auch das Bild kommt.
    bool depthWrite = false;
    DepthFunc depthFunc = DepthFunc::LEqual;

    // clampmap der Stufe, aus der auch das Bild kommt.
    bool clamp = false;

    // animMap der Stufe, aus der auch das Bild kommt - siehe Stage.
    std::vector<std::string> animImages;
    float animFreq = 0.0F;
    bool animOneShot = false;

    // alphaGen der Stufe, aus der auch das Bild kommt.
    AlphaGen alphaGen = AlphaGen::FromImage;

    // Die Mischfaktoren der Stufe, aus der auch das Bild kommt.
    BlendFactor srcFactor = BlendFactor::One;
    BlendFactor dstFactor = BlendFactor::Zero;

    // Der Alphatest der Stufe, aus der auch das Bild kommt.
    AlphaTest alphaTest = AlphaTest::None;
    CullMode cull = CullMode::Front;

    // rgbGen wave - die Helligkeit pulst.
    //
    // tr_shade_calc.cpp, RB_CalcWaveColor(): glow = EvalWaveForm(wf),
    // auf 0..1 begrenzt, dann als Farbe aller drei Kanaele.
    Wave rgbWave;

    // deformVertexes wave - die Flaeche selbst wogt.
    //
    // RB_CalcDeformVertexes(): jede Ecke wandert ENTLANG IHRER NORMALEN,
    // um einen Betrag, der von ihrem Ort abhaengt:
    //
    //     off   = ( x + y + z ) * deformationSpread;
    //     scale = WAVEVALUE( table, base, amplitude, phase + off, freq );
    //     xyz  += normal * scale;
    //
    // Dadurch wogt die Flaeche als Welle statt gleichmaessig zu atmen.
    // Bei frequency == 0 faellt der Ortsanteil weg und alles bewegt sich
    // im Gleichtakt - auch das steht so im Quelltext.
    //
    // deformSpread ist der KEHRWERT der Zahl in der Datei:
    //     ds->deformationSpread = 1.0f / atof( token );   (tr_shader.cpp)
    Wave deformWave;
    float deformSpread = 0.0F;

    // polygonOffset - die Flaeche klebt auf einer anderen.
    //
    // Abziehbilder, Schmutzflecken, Leuchten, Projektionen: Geometrie, die
    // DECKUNGSGLEICH auf einer Wand liegt. Ohne Vorrang entscheidet die
    // Rechengenauigkeit, welche von beiden oben liegt - und weil sie bei
    // jeder Kamerabewegung anders ausfaellt, FLIMMERT es.
    //
    // Die Engine loest das zweifach, tr_shader.cpp:
    //   Zeile 2319:  polygonOffset -> shader.polygonOffset = true
    //   Zeile 2994:  if ( shader.polygonOffset && !shader.sort )
    //                    shader.sort = SS_DECAL;
    // und im Zeichner, tr_shade.cpp Zeile 2078:
    //   qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
    // mit r_offsetfactor = -1 als Vorgabe (tr_init.cpp, Zeile 1630) - also
    // ein Stueck ZUR KAMERA hin.
    //
    // In den Shadern von MD_Maps_Ep3 stehen 21 solche Flaechen.
    bool polygonOffset = false;
};

// Shadername -> was wir davon wissen.
using ShaderMap = std::map<std::string, ShaderInfo>;

// Eine .shader-Datei zerlegen und die Zuordnungen anhaengen.
void parseShaderScript(const std::string& text, ShaderMap& out);

// Kandidaten fuer eine Bilddatei zu einem Namen, in der Reihenfolge, in der
// gesucht werden sollte. JKA meldet in tr_image_load.cpp jpg, png und tga an.
[[nodiscard]] std::vector<std::string> textureCandidates(const std::string& name);

}  // namespace bhed
#endif
