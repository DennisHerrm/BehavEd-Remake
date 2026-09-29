// mapview.h - was die Kartenansicht braucht: Kamera, Texturen, Stapelzustaende
//
// Gezeichnet wird ueber die Grafikkarte (gui/gpumap_win32.cpp, Direct3D 11).
// Bis rc568 stand hier auch ein Software-Rasterer; er war zuletzt nur noch
// Vergleichsfassung und ist entfernt. Geblieben ist, was beide Wege teilten.
#ifndef BHED_MAPVIEW_H
#define BHED_MAPVIEW_H

#include "bhed/bspgeo.h"
#include "bhed/shaderscript.h"
#include "bhed/gla.h"
#include "bhed/glm.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bhed {

struct Camera {
    float pos[3]{0, 0, 0};
    // Grad, wie in JKA: pitch (hoch/runter), yaw (links/rechts), roll.
    // Genau die Reihenfolge, die auch camera ( PAN, < p y r >, ... ) erwartet.
    float angles[3]{0, 0, 0};
    // SENKRECHTER Blickwinkel. Die Engine rechnet aber mit dem WAAGERECHTEN
    // (fov_x): in CG_CalcFOVFromX steht
    //     x     = breite / tan( fov_x / 2 )
    //     fov_y = atan2( hoehe, x )
    // Wer die Zahl aus einem camera ( ZOOM, ... ) einfach als fovY einsetzt,
    // bekommt ein zu enges Bild - und zwar umso mehr, je breiter das Fenster
    // ist.
    float fovY = 73.7F;   // entspricht fov_x 90 bei 4:3

    // Aus dem waagerechten Blickwinkel setzen, wie ihn ICARUS angibt.
    void setFovX(float fovXDegrees, float aspect);

    void forward(float out[3]) const;
    void right(float out[3]) const;
    void up(float out[3]) const;
    void move(float forwardAmount, float rightAmount, float upAmount);
};

// --- Drehen um eine beliebige Achse (Rodrigues) -----------------------
//
// Gebraucht fuer das Drehgizmo im WELT-System: dort dreht man um die
// Weltachsen X, Y, Z, waehrend die Kamera ihre Winkel als pitch/yaw/roll
// speichert. Der Weg ist: Basis aus den Winkeln bauen, um die Weltachse
// drehen, Winkel wieder herauslesen.
//
// Die Achse muss auf Laenge eins sein. Gedreht wird im RECHTSSYSTEM, also
// gegen den Uhrzeigersinn, wenn man der Achse entgegenblickt - dieselbe
// Konvention wie in der Engine.
void rotateAboutAxis(const float v[3], const float axis[3], float degrees,
                     float out[3]);

// --- Winkel aus einer Orientierung zurueckrechnen ---------------------
//
// Die Umkehrung von Camera::forward/right/up: aus Blickrichtung und
// Obenrichtung wieder pitch, yaw, roll. Genau hier lauert die
// Haendigkeitsfalle, die in diesem Projekt schon mehrfach zugeschlagen
// hat - deshalb prueft anglestest den Rundlauf Winkel -> Basis -> Winkel.
void anglesFromBasis(const float forwardV[3], const float upV[3],
                     float outAngles[3]);

// Bedienung wie im Ansichtsfenster von 3ds Max.
//
// Warum nicht nur Fliegen: zum SETZEN einer Kamera will man ein Ziel
// umkreisen und dabei sehen, wie es aus verschiedenen Richtungen aussieht.
// Fliegen ist gut, um irgendwo hinzukommen, aber umstaendlich, um sich um
// etwas herumzubewegen.
//
// Der Drehpunkt liegt eine feste Strecke vor der Kamera. Beim Umkreisen
// bleibt er stehen und die Kamera wandert um ihn herum; beim Zoomen kommt
// die Kamera naeher, ohne dass sich die Blickrichtung aendert.
struct Orbit {
    float distance = 512.0F;   // Abstand zum Drehpunkt

    // Drehpunkt aus Kamerastellung und Abstand.
    void pivot(const Camera& cam, float out[3]) const;

    // Umkreisen: die Kamera wandert, der Drehpunkt bleibt.
    void turn(Camera& cam, float deltaYawDeg, float deltaPitchDeg);

    // Schieben: Kamera UND Drehpunkt wandern seitwaerts. Der Betrag haengt
    // vom Abstand ab - sonst schiebt man aus der Ferne unmerklich langsam
    // und aus der Naehe viel zu schnell.
    void pan(Camera& cam, float dx, float dy);

    // Zoomen: naeher heran, aber nie durch den Drehpunkt hindurch.
    void zoom(Camera& cam, float steps);
};

struct MapImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;   // width*height*4

    // Der Abstand zur Kamera je Bildpunkt - fuer die Ueberlagerung (Gizmos,
    // Entitykreuze), aus dem Tiefenpuffer der Grafikkarte (gpu::leseTiefe).
    std::vector<float> depth;
};

// Texturen je Shader. Der Index ist die Shadernummer aus der .bsp.
//
// Leere Eintraege sind der Normalfall und kein Fehler: nicht jeder Shader
// hat ein Bild, das sich finden laesst - manche sind reine Hilfsflaechen,
// manche stecken in einer .pk3, die nicht eingebunden ist.
struct TextureSet {
    struct Tex {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> rgba;

        // --- Die Bildpyramide -------------------------------------------
        //
        // Gebeten: "mach Mipmaps."
        //
        // Jede Stufe hat die halbe Kantenlaenge der vorigen, bis 1x1. Die
        // Engine baut sie mit `R_MipMap` (tr_image.cpp:466): je vier
        // Nachbarn zu einem, genau wie unser shrinkTo mit scale 2.
        //
        // Wozu: ein Bildpunkt auf einer weit entfernten Wand deckt viele
        // Texel ab. Wer dort einen einzelnen abtastet, bekommt bei jeder
        // kleinsten Kamerabewegung einen anderen - das ist das Flimmern.
        // Eine kleinere Stufe hat das Mitteln schon hinter sich.
        //
        // Stufe 0 ist `rgba` selbst; hier stehen nur 1 und aufwaerts.
        struct Mip {
            int width = 0;
            int height = 0;
            std::vector<std::uint8_t> rgba;
        };
        std::vector<Mip> mips;
        // Die Pyramide erzeugen. Mehrfaches Rufen ist harmlos.
        void buildMips();
        // Die Stufe `level` - 0 ist das Bild selbst. Zu grosse Werte geben
        // die kleinste vorhandene Stufe, statt ins Leere zu greifen.
        [[nodiscard]] const std::uint8_t* levelData(int level, int& w,
                                                    int& h) const;
        // tcMod scroll aus dem Shader, Einheiten je Sekunde. Damit steht
        // Lava nicht mehr still.
        // Die tcMod-Kette dieser Stufe, in der Reihenfolge der Datei -
        // siehe TexMod in bhed/shaderscript.h.
        TexMod texMods[kMaxTexMods];
        int numTexMods = 0;
        // blendFunc aus dem Shader. Damit ein Hologramm durchscheint,
        // statt als blauer Block dazustehen.
        BlendMode blend = BlendMode::Opaque;
        // Der Alphatest aus dem Shader. Er gilt AUCH bei einer sonst
        // deckenden Stufe - siehe AlphaTest in shaderscript.h.
        AlphaTest alphaTest = AlphaTest::None;
        // alphaGen const aus dem Shader - siehe ShaderInfo::alphaConst.
        // Negativ heisst: nicht gesetzt, es gilt der Wert aus dem Bild.
        float alphaConst = -1.0F;
        // deformVertexes autosprite - siehe autospriteQuad().
        bool autosprite = false;
        // --- Weitere Bildstufen dieses Shaders --------------------------
        //
        // Nummern in `byShader`, in der Reihenfolge der Datei, OHNE die
        // Stufe, die schon dieser Eintrag selbst ist.
        //
        // Gemessen an md_am_sith (rc343): von 70 Shadern haben 31 die
        // Redewendung "Lightmap, dann Textur multiplizieren" - die rechnet
        // der Zeichner ohnehin selbst, dort ist nichts zu tun. Wirklich
        // fehlen die Stufen DARUEBER: das additive Leuchten von
        // `table_blue` und die vier- bis fuenfstufigen Bildschirme.
        //
        // Deshalb stehen hier nur die zusaetzlichen, nicht alle.
        std::vector<int> extraStages;
        // cull aus dem Shader. Eine beidseitige Flaeche wird von BEIDEN
        // Seiten gezeichnet - Gitter, Pflanzen, Vorhaenge.
        CullMode cull = CullMode::Front;
        // rgbGen wave: die Helligkeit pulst.
        Wave rgbWave;
        // rgbGen const ( r g b ) - feste Einfaerbung, 0..1. Negativ heisst
        // nicht gesetzt. Siehe ShaderInfo::Stage::rgbConst.
        float rgbConst[3]{-1.0F, -1.0F, -1.0F};
        // --- Woher das Licht kommt (siehe ShaderInfo::lightmapStufe) ----
        //
        // ausSkript: der Shader steht in einer .shader-Datei. Ohne Skript
        // baut die Engine selbst einen (R_FindShader): Lightmap x Textur
        // bzw. Eckenfarbe bei -3 - genau das, was behaved bisher immer tat.
        // Mit Skript gilt nur, was dort steht.
        bool ausSkript = false;
        bool lightmapStufe = false;
        bool rgbVertex = false;
        // clampmap statt map: die Koordinaten werden geklemmt, nicht
        // gekachelt. Siehe tapLevel in src/mapview.cpp.
        bool clamp = false;
        // depthWrite und depthFunc der Stufe - siehe ShaderInfo::Stage.
        bool depthWrite = false;
        DepthFunc depthFunc = DepthFunc::LEqual;
        // --- animMap: die uebrigen Bilder der Folge --------------------
        //
        // Indizes in byShader, wie extraStages - das erste ist DIESE Textur
        // selbst. Leer oder einelementig heisst: kein Wechsel.
        //
        // Die Bilder tragen dieselben Eigenschaften wie die Stufe; gewaehlt
        // wird nur das Bild. Deshalb sind es vollstaendige Tex-Eintraege und
        // nicht bloss Bilddaten - der Zeichner tauscht dann einfach den
        // Zeiger und muss nichts weiter wissen.
        std::vector<int> animFrames;
        float animFreq = 0.0F;
        // oneshotanimMap: am letzten Bild stehen bleiben statt von vorn.
        bool animOneShot = false;
        // --- skyParms: die sechs Seiten der Himmelsbox ------------------
        //
        // Indizes in byShader, in der Reihenfolge der Engine:
        //
        //     0 rt   1 lf   2 bk   3 ft   4 up   5 dn
        //
        // Leer heisst: kein Himmel. Ist die Liste gefuellt, holt der
        // Zeichner die Farbe NICHT aus den Texturkoordinaten der Flaeche,
        // sondern aus der BLICKRICHTUNG des Bildpunktes - siehe
        // skySample in src/mapview.cpp.
        std::vector<int> skyFaces;
        // Woher der Alphawert kommt - siehe AlphaGen. Nur LightingSpecular
        // aendert hier etwas; Const steckt in alphaConst, FromImage ist die
        // Vorgabe, und Unsupported kommt gar nicht erst bis hierher.
        AlphaGen alphaGen = AlphaGen::FromImage;
        // deformVertexes wave: die Flaeche wogt entlang ihrer Normalen.
        Wave deformWave;
        float deformSpread = 0.0F;
        // polygonOffset: die Flaeche klebt auf einer anderen und bekommt
        // Vorrang, damit es nicht flimmert.
        bool polygonOffset = false;
        // glow: diese Stufe geht in den Gluehdurchgang.
        //
        // JKA-eigen, in keinem Q3-Handbuch. Wird nicht hier gezeichnet,
        // sondern in einem zweiten Durchgang - siehe gpu::zeichneGluehen.
        //
        // Es steht an der STUFE, nicht am Shader: bei `table_blue` glueht
        // nur die dritte von drei Stufen, die Lightmap und die daempfende
        // zweite bleiben draussen. Genau so macht es tr_shade.cpp:1877.
        bool glow = false;
        // Woher die Texturkoordinaten kommen - siehe TexGen. Bei
        // Environment werden sie je Ecke aus Normale und Blickrichtung
        // gerechnet statt aus der .bsp genommen.
        TexGen texGen = TexGen::Base;
        // Die beiden Faktoren aus glBlendFunc - siehe BlendFactor. `blend`
        // daneben ist nur noch fuer die SORTIERUNG da; gerechnet wird mit
        // diesen beiden.
        BlendFactor srcFactor = BlendFactor::One;
        BlendFactor dstFactor = BlendFactor::Zero;

        // Mischart UND Faktoren zusammen setzen.
        //
        // Seit die Faktoren gerechnet werden, ist `blend` allein nicht mehr
        // ausreichend: wer nur ihn setzt, bekommt die Vorgabefaktoren
        // GL_ONE GL_ZERO und damit eine DECKENDE Flaeche - egal, was in
        // `blend` steht.
        //
        // Das ist genau einmal passiert und hat vier Proben umgeworfen.
        // Deshalb gibt es diese Funktion: wer aus einem BlendMode heraus
        // arbeitet (der Effektzeichner, die Proben), ruft sie und kann die
        // Faktoren nicht mehr vergessen.
        //
        // Aus dem Shaderskript kommen beide ohnehin einzeln; dort wird sie
        // nicht gebraucht.
        void setBlend(BlendMode m) noexcept {
            blend = m;
            switch (m) {
                case BlendMode::Add:
                    srcFactor = BlendFactor::One;
                    dstFactor = BlendFactor::One;
                    break;
                case BlendMode::AddAlpha:
                    srcFactor = BlendFactor::SrcAlpha;
                    dstFactor = BlendFactor::One;
                    break;
                case BlendMode::Alpha:
                    srcFactor = BlendFactor::SrcAlpha;
                    dstFactor = BlendFactor::OneMinusSrcAlpha;
                    break;
                case BlendMode::Filter:
                    srcFactor = BlendFactor::DstColor;
                    dstFactor = BlendFactor::Zero;
                    break;
                case BlendMode::Opaque:
                    srcFactor = BlendFactor::One;
                    dstFactor = BlendFactor::Zero;
                    break;
            }
        }

        [[nodiscard]] bool empty() const noexcept { return rgba.empty(); }
    };
    std::vector<Tex> byShader;
    int found = 0;
    int missing = 0;
};

// Ein bewegtes Brush-Modell: eine Tuer, eine Plattform, ein Raumschiff.
//
// Die Geometrie steht in der .bsp AN IHREM GEBAUTEN PLATZ - deshalb keine
// Position, sondern eine VERSCHIEBUNG dagegen, und eine Drehung um den
// eigenen Ursprung. Bei t=0 sind beide null, und das Teil steht dort, wo
// der Kartenbauer es hingelegt hat.
struct MoverDraw {
    const BspMesh* mesh = nullptr;
    float offset[3]{};   // aktueller Ort minus Startort
    float yaw = 0.0F;    // Drehung gegenueber dem gebauten Winkel
    float pivot[3]{};    // worum gedreht wird: der Ursprung der Entity
    // --- Nicken und Rollen dazu ----------------------------------------
    //
    // Tueren und Aufzuege drehen sich nur um Z - dafuer genuegte `yaw`.
    // Ein Modell an einem Emitter TAUMELT: `angleDelta -20 -40 -20 20 40
    // 20` in probehead.efx dreht um alle drei Achsen.
    //
    // Reihenfolge wie in der Engine (AnglesToAxis, q_math.c): erst Gier
    // um Z, dann Nicken um Y, dann Rollen um X. Wer sie vertauscht,
    // bekommt bei zwei gleichzeitigen Drehungen etwas anderes heraus -
    // Drehungen sind nicht vertauschbar.
    //
    // Null und null heisst: genau wie vorher. Alles, was heute `yaw`
    // benutzt, bleibt unberuehrt.
    float pitch = 0.0F;
    float roll = 0.0F;
    // Die Skalierung je Achse - Kartenmodelle mit "modelscale" oder
    // "modelscale_vec". Tueren und Plattformen haben 1 1 1.
    float scale[3]{1.0F, 1.0F, 1.0F};
};

// Die schwarzen Blenden oben und unten, wie im Spiel.
//
// Ein Zehntel der Hoehe je Blende - cg_camera.cpp rechnet mit
// bar_height_dest = 480/10.
//
// Ein EIGENER Schritt, weil die Reihenfolge zaehlt: Figuren und Effekte
// werden nach der Karte gezeichnet, und wer die Blende vorher malt,
// bekommt sie von ihnen wieder uebermalt. Genau so war es zu sehen - der
// Kopf des Hologramms und die Figuren am unteren Rand standen mitten im
// schwarzen Balken.
// `anteil` 0..1: Hoehe UND Deckkraft der Balken, wie CGCam_UpdateBarFade
// beide gemeinsam ein- und ausblendet.
void drawLetterbox(MapImage& out, float anteil = 1.0F);
// Die Vollbild-Blende von camera ( FADE ): das ganze Bild in `farbe` (RGBA
// 0..1) ueberdecken - CG_FillRect(0, 0, 640, 480, fade_color).
void drawVollblende(MapImage& out, const float farbe[4]);

// Texturen je Flaeche des Modells. Der Index ist die Flaechennummer.
struct ModelTextures {
    std::vector<TextureSet::Tex> bySurface;
    int found = 0;
    int missing = 0;
};

// --- Das Lichtschwert ----------------------------------------------------
//
// Gemeldet: "ich sehe die Lichtschwerter auch nicht."
//
// Der Griff ist ein EIGENES Modell, das an einem Bolzen der Figur haengt
// (g_client.cpp:1629: G2API_AddBolt auf "*r_hand", fuer das zweite Schwert
// "*l_hand"). Er wird starr mitgefuehrt - er hat kein eigenes Skelett, das
// sich bewegt.
//
// Die Klinge ist kein Modell, sondern gezeichnet: Klinge N beginnt am
// Bolzen "*bladeN" des GRIFFS und laeuft in dessen negative x-Richtung
// (CG_AddSaberBladeGo, cg_players.cpp:13784 ff. - NEGATIVE_X, mit dem
// Vermerk "was NEGATIVE_Y, but the md3->glm exporter screws up this tag").
// Fehlt der Bolzen, nimmt die Engine "*flash" und legt die Klingen nach
// dem saberType zurecht (tag_hack, ebenda:13818 ff.) - siehe KlingenLage.

// So viele Klingen kann ein Griff tragen - MAX_BLADES, dieselbe Zahl wie
// kMaxKlingen in scene.h (dort geprueft: app_view3d.cpp, static_assert).
inline constexpr int kAnbauKlingen = 8;
// Anbauten je Figur: zwei Hand-Faecher (ent->weaponModel[2]) und die
// Kinomodelle (kMaxKinoModelle in scene.h).
inline constexpr int kAnbauMax = 6;

// Wie die Klingen liegen, wenn der Griff keine "*bladeN"-Bolzen hat
// (tag_hack in CG_AddSaberBladeGo, cg_players.cpp:13818 ff.). Nur die
// Faelle, die Versaetze brauchen; alles andere zeichnet wie ein
// Einzelschwert. Nicht nachgebildet: SABER_ARC, SABER_STAR und
// SABER_TRIDENT - in den .sab-Dateien der Mod kommt keiner vor.
enum class KlingenLage : std::uint8_t {
    Einzeln,   // SABER_SINGLE und alle Abwandlungen: kein Versatz
    Stab,      // SABER_STAFF & Co.: Klinge 2 umgedreht, 16 weiter
    Breit,     // SABER_BROAD: -1 / +1 quer
    Zinke,     // SABER_PRONG: -3 / +3 quer
    Sai,       // SABER_SAI: Klinge 2 -3, Klinge 3 +3 quer
    Kralle,    // SABER_CLAW: 2 vor, 2 hoch, Klinge 2/3 +-2 quer
};

// Die Lage aus dem saberType der .sab (gross geschrieben).
[[nodiscard]] KlingenLage klingenLageFuer(const std::string& typ);

// Wurzel und Spitze von Klinge `nummer` im Raum des GRIFFS, nach
// CG_AddSaberBladeGo: erst der Bolzen "*blade<nummer+1>", sonst "*flash"
// mit den Versaetzen von `lage`. false, wenn der Griff keinen der beiden
// Bolzen hat.
[[nodiscard]] bool klingenStrecke(const GlmModel& griff, KlingenLage lage, int nummer,
                                  float laenge, float wurzel[3], float spitze[3]);

// Eine Klinge, wie sie in diesem Bild gezeichnet wird.
struct KlingeDraw {
    int nummer = 0;            // welche Klinge des Griffs, von 0 an
    float laenge = 0.0F;       // Einheiten; unter 0.5 wird sie nicht eingetragen
    // Halber Durchmesser. saberRadius aus der .sab-Datei, Vorgabe 3
    // (SABER_RADIUS_STANDARD, wp_saber.h:57).
    float radius = 3.0F;
    // Farbe der Klinge. Sie gilt nur, wenn die Texturen unten fehlen.
    std::uint8_t farbe[3] = {120, 180, 255};
    // --- Die beiden Bilder, aus denen eine Klinge besteht ------------
    //
    // Gemeldet: "besser, aber noch nicht perfekt."
    //
    // Sie war ein Rechteck in EINER Farbe, mit harten Kanten. In der
    // Engine sind es zwei TEXTUREN (cg_players.cpp:5763 ff.):
    //
    //     glow  = cgs.media.blueSaberGlowShader;   gfx/.../blue_glow2
    //     blade = cgs.media.blueSaberCoreShader;   gfx/.../blue_line
    //
    // Nachgemessen an den echten Dateien aus assets1:
    //
    //   blue_glow2  128x128, quer durch die Mitte 0 .. 140 .. 0 -
    //               ein weicher Verlauf, Spitzenwert nur 140 von 255
    //   blue_line   64x256,  quer 0 .. 255 mit breitem Plateau,
    //               LAENGS durchgehend 252 - ein Band mit weichen Raendern
    //
    // Diese Verlaeufe nachzurechnen hiesse raten. Die Bilder liegen vor,
    // also werden sie benutzt - so wie die Engine es tut.
    const TextureSet::Tex* glowTex = nullptr;
    const TextureSet::Tex* coreTex = nullptr;
};

// Ein Modell an einem Bolzen der Figur: Waffe, Schwertgriff oder
// Kinomodell.
struct AnbauDraw {
    const GlmModel* model = nullptr;
    const ModelTextures* textures = nullptr;
    // Der Bolzen der FIGUR: "*r_hand", "*l_hand" - bei boltToWrist
    // "*r_hand_cap_r_arm" / "*l_hand_cap_l_arm" (wp_saber.cpp:858 ff.).
    const char* bolzen = "*r_hand";
    KlingenLage lage = KlingenLage::Einzeln;
    int klingen = 0;           // wie viele von klinge[] gelten
    std::array<KlingeDraw, kAnbauKlingen> klinge{};
};

// Eine Figur, wie sie in diesem Bild gezeichnet wird.
struct ActorDraw {
    // Ein fester Zuschlag auf den Gierwinkel, in Grad.
    //
    // DIAGNOSE, keine Einstellung. Gemessen an
    // models/players/_humanoid_md/_humanoid.gla liegen in der Grundstellung
    // die linke und die rechte Koerperhaelfte entlang X auseinander
    // (lfemurYZ zu rfemurYZ: 7,2 in X, 0,0 in Y; lhand zu rhand: 38,0 in X).
    // Quer zur Blickrichtung liegt also X - die Figur schaut nach +/-Y.
    //
    // Die Engine dagegen setzt +X als vorn: Create_Matrix() ruft schlicht
    // AnglesToAxis() auf (G2_misc.cpp), und dort ist forward = (cp*cy,
    // cp*sy, -sp). Ein Ausgleich ist im Zeichenweg nicht zu finden -
    // RootMatrix() liefert im Normalfall die Einheitsmatrix
    // (tr_ghoul2.cpp, Zeile 2736).
    //
    // ENTSCHIEDEN: der feste Ausgleich betraegt +90 Grad, und er steckt im
    // Zeichner (gpu::baueFigurWelt). Rechnerisch passt das zur Messung:
    //
    //   links = z x vorn   =>   vorn = links x z
    //   links = (1,0,0), z = (0,0,1)  =>  vorn = (0,-1,0) = -Y
    //
    // Die Figur schaut also nach -Y. Eine Drehung um +90 Grad um Z bildet
    // (0,-1,0) auf (1,0,0) = +X ab - genau die Achse, die die Engine als
    // vorn ansieht. Am Bildschirm bestaetigt: ohne Ausgleich standen alle
    // Figuren 90 Grad nach rechts verdreht.
    //
    // Dieses Feld bleibt als DIAGNOSE bestehen und kommt ZUSAETZLICH dazu.
    // Es steht auf 0, solange nichts Neues zu klaeren ist.
    float yawOffset = 0.0F;

    const GlmModel* model = nullptr;
    const GlaAnimation* anim = nullptr;
    const ModelTextures* textures = nullptr;
    int frame = 0;
    // Das NAECHSTE Bild und der Anteil dazwischen.
    //
    // Gemeldet: "er stottert immer noch beim Laufen."
    //
    // behaved sprang von Bild zu Bild - bei 20 Bildern je Sekunde zwanzig
    // Stufen, waehrend der Bildschirm sechzig zeigt. Die Engine mischt
    // dazwischen (tr_ghoul2.cpp:1531 ff.).
    //
    // frameNext < 0 heisst: nicht mischen, wie bisher.
    int frameNext = -1;
    float frameFraction = 0.0F;
    // Die EINGEFRORENE Frame der vorigen Animation, -1 heisst: keine.
    //
    // Ghoul2 mischt nicht zwei laufende Animationen, sondern die laufende
    // gegen ein Standbild (ghoul2_shared.h:97). Deshalb reicht EINE Zahl.
    int prevFrame = -1;
    // 0 heisst ganz das Standbild, 1 ganz die laufende Animation.
    float blendLerp = 1.0F;
    float pos[3]{};
    float yaw = 0.0F;
    // Wie weit der KOPF gegen den Koerper gedreht ist, in Grad.
    //
    // Aus SET_LOOK_TARGET. Null heisst geradeaus. Der Wert ist bereits auf
    // kHeadYawClampDeg beschnitten, wenn er hier ankommt - der Zeichner
    // beschneidet nicht noch einmal, sonst gaebe es zwei Stellen fuer
    // dieselbe Regel.
    // Wie weit KOPF und OBERKOERPER gegen ihren jeweiligen Elternteil
    // gedreht sind, in Grad. Beide zusammen ergeben den ganzen Ausschlag
    // zum Blickziel - siehe torsoSwingShare() in scene.h.
    //
    // Beide sind Winkel AM KNOCHEN, nicht in der Welt. Die Engine rechnet
    // sie ebenso aus der Kette heraus (cg_players.cpp:3358 f.).
    // Der GANZE Blickausschlag, nicht der Winkel eines einzelnen Knochens.
    // Wie er sich auf Brust, Hals und Kopf verteilt, steht in mapview.cpp.
    float headYaw = 0.0F;
    // Das Bild der OBERKOERPERanimation, -1 heisst: keine eigene.
    //
    // Gilt fuer den Teilbaum ab "lower_lumbar" - so teilt die Engine Ober-
    // und Unterkoerper (bg_panimate.cpp:4864).
    int torsoFrame = -1;
    // Das naechste Bild der Oberkoerperanimation und der Anteil daran -
    // wie frameNext/frameFraction fuer den Koerper.
    int torsoNext = -1;
    float torsoFraction = 0.0F;
    // Der eigene Uebergang des Oberkoerpers: das eingefrorene Bild der
    // vorigen Oberkoerperanimation und der Anteil der neuen (1 = fertig).
    int torsoPrevFrame = -1;
    float torsoBlendLerp = 1.0F;
    // Das Bild der MUNDanimation, -1 heisst: keine.
    //
    // Gilt fuer den Teilbaum ab "face" - dort setzt die Engine sie
    // (g_client.cpp:1653, cg_players.cpp:5132), und der Kiefer haengt
    // darunter.
    int faceFrame = -1;

    // --- Das Licht an dieser Stelle --------------------------------------
    //
    // Gemeldet als Unterschied zwischen Spiel und Programm: die Figuren
    // standen flach und gleich hell da.
    //
    // Flaechen der Karte haben Lightmaps, Figuren nicht - die Engine holt
    // ihr Licht aus dem Lichtgitter der .bsp (sampleLightGrid in
    // bspgeo.h). Ohne `ok` bleibt es bei der alten Schattierung aus den
    // Normalen; eine Karte ohne Gitter soll nicht schwarz werden.
    GridLight light;

    // --- Was die Figur in den Haenden haelt ------------------------------
    //
    // Waffen, Schwertgriffe und Kinomodelle (SET_ADD*HANDBOLT_MODEL) - alle
    // auf demselben Weg: ein eigenes Ghoul2-Modell, starr an einen Bolzen
    // der Figur gehaengt (G2API_AttachG2Model, wp_saber.cpp:782 und
    // Q3_Interface.cpp:6343). Welche es sind, steht in anbau[0..anbauten).
    std::array<AnbauDraw, kAnbauMax> anbau{};
    int anbauten = 0;
};

// --- Der Helligkeitsfaktor gilt auch fuer Figuren ----------------------
//
// Das Licht aus dem Gitter (ActorDraw::light) wird mit derselben Helligkeit
// multipliziert wie die Lightmaps der Karte (gpu::FigurLicht, gesetzt in
// app_view3d.cpp) - ohne sie sind die Figuren nur halb so hell wie die
// Karte um sie herum.
//
// Gemeldet: "bei Watt Tambor fehlt der Brustpanzer ... es ist nur in der Map
// so". Im Modellfenster war die Figur braun und deutlich, in der Karte fast
// schwarz vor einem hellen Raum - und ein dunkelbrauner Panzer auf dunklen
// Riemen ist nicht zu erkennen. Es fehlte keine Flaeche; es fehlte das
// Licht.
//
// Die Engine macht dasselbe: R_ColorShiftLightingBytes wird auf die Werte
// aus dem Lichtgitter genauso angewandt wie auf die Lightmaps.
// --- Der Zustand eines Stapels, an einer Stelle abgeleitet --------------
//
// Warum es das gibt
// -----------------
// Ein Stapel (BspMesh::Batch) ist schon die Zeichenliste, die auch eine
// Grafikkarte bekaeme: Ecken, Indizes, nach Shader gruppiert. Was fehlt, ist
// die Beschreibung, WIE er gezeichnet wird - und die stand frueher nur als
// zwei Dutzend lokale Variablen mitten im Rasterer.
//
// Gemessen (`ZUSTAND=1` im Messwerkzeug), ueber die Episode-3-Karten:
//
//     duel_invisible_hand   660 Stapel ->  24 verschiedene Zustaende
//     md_am_sith            421 Stapel ->  14
//     duel_jt_outside       521 Stapel ->   3
//
// Nie mehr als 24. In md_am_sith fallen 339 der 421 Stapel auf ZWEI
// Zustaende. Die Beschreibung darf also klein sein.
//
// Was hier NICHT steht: alles, was schon in TextureSet::Tex liegt (Mischart,
// tcMod-Kette, cull, ...). Dafuer zeigt `tex` dorthin. Hier stehen nur die
// Werte, die man erst AUSRECHNEN muss - vor allem solche, die von der Zeit
// abhaengen.
struct BatchState {
    // Zeigt auf den Texturen-Eintrag, oder nullptr wenn der Stapel keine
    // brauchbare Textur hat. Alles Unveraenderliche steht dort.
    const TextureSet::Tex* tex = nullptr;

    // rgbGen wave, zum gefragten Zeitpunkt ausgerechnet (0 bis 1).
    float rgbGlow = 1.0F;
    bool haveRgbWave = false;

    // rgbGen const - negative Werte im Tex heissen "nicht gesetzt".
    float rgbConst[3]{1.0F, 1.0F, 1.0F};
    bool haveRgbConst = false;

    // Woher das Licht kommt - siehe TextureSet::Tex::ausSkript.
    bool ausSkript = false;
    bool lightmapStufe = false;
    bool rgbVertex = false;

    // alphaGen lightingSpecular braucht einen eigenen Weg.
    bool haveSpecular = false;

    // Ein Himmelswuerfel liegt nur vor, wenn genau sechs Seiten da sind.
    const TextureSet* skySet = nullptr;
    const std::vector<int>* skyFaces = nullptr;

    // --- Ab hier: uebernommen, damit ein Verbraucher OHNE Tex auskommt ---
    //
    // Diese Felder stehen unveraendert auch im TextureSet::Tex. Sie werden
    // trotzdem kopiert, weil die Schnittstelle sonst nur halb ist: wer den
    // Zustand haben will, muesste sich zusaetzlich den Tex besorgen und
    // wissen, welche Felder daraus gelten. Genau solche halben
    // Schnittstellen erzeugen zwei Wahrheiten.
    //
    // Es sind ein paar Dutzend Bytes je Stapel, und ein Bild hat nach
    // Messung nie mehr als 24 verschiedene Zustaende.
    BlendMode blend = BlendMode::Opaque;
    BlendFactor srcFactor = BlendFactor::One;
    BlendFactor dstFactor = BlendFactor::Zero;
    CullMode cull = CullMode::Front;
    AlphaTest alphaTest = AlphaTest::None;
    AlphaGen alphaGen = AlphaGen::FromImage;
    DepthFunc depthFunc = DepthFunc::LEqual;
    TexGen texGen = TexGen::Base;
    float alphaConst = -1.0F;
    bool depthWrite = false;
    bool autosprite = false;
    bool polygonOffset = false;
    // Glueht diese Stufe? 1227 der 4447 Shader tun es - der groesste
    // Einzelposten in der Merkmalszaehlung.
    //
    // Der GPU-Weg braucht die Auskunft im Zustand, sonst kann er nicht
    // wissen, was in den Gluehpuffer gehoert.
    bool glow = false;

    // Die tcMod-Kette dieser Stufe, in ihrer Reihenfolge.
    TexMod texMods[kMaxTexMods];
    int numTexMods = 0;

    // deformVertexes gilt fuer den GANZEN Shader, nicht je Stufe - siehe
    // rc383. Steht deshalb hier und nicht bei den Stufen.
    Wave deformWave;
    float deformSpread = 0.0F;
};

// Leitet den Zustand eines Stapels ab. `zeitSekunden` ist die Zeit, mit der
// zeitabhaengige Wellen ausgewertet werden.
//
// Diese Funktion ist der EINZIGE Ort, an dem aus einem Texturen-Eintrag ein
// Zeichenzustand wird - damit ein zweiter Verbraucher (etwa ein Weg ueber
// die Grafikkarte) dieselbe Ableitung benutzt und nicht seine eigene baut.
// Dieselbe Ableitung fuer eine FIGURENflaeche: die liegt in
// `ModelTextures::bySurface` und nicht in `TextureSet::byShader`, ist aber
// derselbe Typ. Bis rc451 gab es nur die Fassung fuer die Karte, und der
// GPU-Weg zeichnete Figuren deshalb durchweg deckend.
[[nodiscard]] BatchState batchStateFor(const TextureSet::Tex& t,
                                       float zeitSekunden);

[[nodiscard]] BatchState batchStateFor(const TextureSet* textures, int texId,
                                       float zeitSekunden);

// Das Bild einer animMap-Folge zur Zeit `zeitSekunden` (Zeit des Stapels):
// der Platz in `byShader`, der wirklich gezeichnet wird. Ohne Folge kommt
// `texId` selbst zurueck. Der GPU-Weg hatte bis rc568 keine Bildwahl und
// zeigte immer das erste Bild.
[[nodiscard]] int animBildFuer(const TextureSet* textures, int texId,
                               float zeitSekunden);

// Die Knochenstellung einer Figur fuer EIN Bild.
//
// Bis rc429 stand der Ablauf nur im (inzwischen entfernten) Rasterer, und
// der GPU-Weg rief `worldMatrices(frame, out)` direkt - ohne Vorrang von Ober- und Unterkoerper, ohne Mund, ohne
// Mischung zwischen zwei Bildern, ohne Uebergang zwischen zwei
// Animationen und ohne Kopfwinkel.
//
// `hilf` ist ein Arbeitspuffer, den der Aufrufer haelt. Rueckgabe: ob eine
// Knochenstellung entstanden ist.
[[nodiscard]] bool figurKnochen(const ActorDraw& act,
                                std::vector<BoneMatrix>& world,
                                std::vector<BoneMatrix>& hilf);

}  // namespace bhed
#endif
