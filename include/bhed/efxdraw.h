// Einen Effekt zu Dreiecken machen, damit die Kartenansicht ihn zeichnen kann.
//
// Der Weg dahin ist bewusst der schon vorhandene: heraus kommt ein ganz
// gewoehnliches BspMesh, und das reist als MoverDraw durch denselben
// Aufsammelschritt von renderMap wie die Welt und die beweglichen Teile.
// Damit teilen sich Partikel, Tueren und Karte einen Tiefenpuffer, ein
// Beschneiden und einen Rasterisierer. Ein eigener Partikelzeichner waere
// ein zweiter Rasterisierer gewesen - und der laeuft bei der naechsten
// Aenderung auseinander.
//
// Was die Simulation angeht, wird NICHTS neu erfunden: Zeitplanung, Bahn und
// Kurven kommen aus efxed (bhed/efx/sim.h, curve.h), das seinerseits gegen
// FxScheduler.cpp und FxPrimitives.cpp gebaut und mit ueber viertausend
// Proben abgesichert ist. Hier steht nur, wie aus einem Ort und einer Groesse
// vier Ecken werden.
//
// WAS GEZEICHNET WIRD, und wie genau (Stand rc569):
//
//   * Farbe, Mischart und Bild kommen aus dem Shader der .efx - additiv,
//     durchscheinend, mit animMap und setShaderTime. Der alte Satz "der
//     Rasterisierer kann nur DECKEND" gilt seit dem GPU-Weg nicht mehr.
//   * Particle als gedrehtes Sprite (rotation/rotationDelta), Oriented-
//     Particle flach senkrecht zu seiner Richtung (RB_SurfaceOrientedQuad),
//     Decal flach auf der Flaeche (CG_ImpactMark, mit Drehung), Tail und
//     Line als Band (RB_SurfaceLine), Cylinder als Ring von Baendern entlang
//     der Richtung des Effekts.
//   * Erzeugt wird wie in CreateEffect: origin/velocity/acceleration in der
//     Achse des Effekts, orgOnSphere, orgOnCylinder, axisFromSphere,
//     randRotAroundFwd, org2fromTrace (FxScheduler.cpp:1523 ff.).
//   * Electricity mit Zacken, Verjuengung (FX_TAPER), Verzweigung
//     (FX_BRANCH) und Wachsen (FX_GROW) - Schritt fuer Schritt nach
//     RB_SurfaceElectricity/DoBoltSeg/ApplyShape (tr_surface.cpp:709 ff.).
//     Der Zufall haengt am Samen des Teilchens, nicht an der Uhr: dasselbe
//     Bild zeigt beim Zurueckspulen denselben Blitz. Siehe blitz() in
//     efxdraw.cpp fuer die eine bewusste Abweichung (CreateShape).
//   * Flash (ScreenFlash) als bildschirmfuellendes Sprite vor dem Auge,
//     genau wie CFlash::Draw (FxPrimitives.cpp:2283) - mit Sichtpruefung aus
//     CFlash::Init. Siehe effectScreenFlashesAt/appendScreenFlashes.
//   * Emitter: das Modell (useModel) mit Drehung und Groessenkurve, die
//     Fahne (emitFx), Aufprall- und Todeseffekte. Siehe emitterModelsAt.
//   * Folgeeffekte (impactFx, deathFx, emitFx, playfx) mehrstufig bis
//     kMaxFolgeTiefe - siehe effectFollowUpsAt.
//   * Light und CameraShake wirken ueber effectLightsAt/effectShakeAt,
//     Sound ueber die Klangausgabe der Anwendung.
//
// GRENZEN, die man kennen muss:
//
//   * Decals werden NICHT gegen die Geometrie geschnitten
//     (CM_MarkFragments) - ueber einer Kante steht das Viereck durch die
//     Wand.
//   * useBBox spurt mit einem verlaengerten Strahl statt mit einem Kasten
//     (behaved hat keinen Kastenspurtest gegen die Dreiecke). Das Teilchen
//     bleibt um die Kastenausdehnung vor der Flaeche stehen - an Kanten
//     kann ein echter Kasten frueher haengen bleiben.
//   * FX_RELATIVE entsteht in der Engine nur zur Laufzeit (ein Effekt an
//     einem Knochen oder einer Figur, G_PlayEffect mit isRelative). Das
//     Wort "relative" in einer .efx ueberliest der SP-Parser. Gebaut ist
//     es ueber EffectInstance::anker/relativ; heute setzt die Ansicht noch
//     keinen Anker, weil sie keine angehefteten Effekte abspielt.
//   * Ein Flash nimmt die Kamera des GEZEIGTEN Bildes fuer seine
//     Sichtpruefung; die Engine friert sie beim Entstehen ein.
//   * `cullrange` wird nicht ausgewertet: PlayEffect laesst ein Primitiv
//     weg, wenn der Effekt weiter als cullrange vom AUGE entfernt startet
//     (FxScheduler.cpp:1173) - der Zeichner zeigt es trotzdem.
//   * Ein Folgeeffekt bekommt nur die Richtung ax[0] mit, nicht die ganze
//     Achse; ax[1]/ax[2] baut er neu (MakeNormalVectors). Anders als im
//     Spiel nur, wenn der Ausloeser randRotAroundFwd oder orgOnCylinder +
//     axisFromSphere traegt.
//   * Die Feinzacken eines Blitzes stehen still (im Spiel flackern sie je
//     Bild) - siehe blitz() in efxdraw.cpp.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <map>
#include <vector>

#include "bhed/bspgeo.h"
#include "bhed/efx/effect.h"
#include "bhed/mapview.h"

namespace bhed {

// Ein laufender Effekt in der Welt.
struct EffectInstance {
    const efx::Effect* effect = nullptr;
    float origin[3]{};
    double startMs = 0.0;   // wann er ausgeloest wurde, auf der Zeitleiste
    bool loops = false;     // fx_runner mit Wiederholung
    float intervalMs = 0.0F;
    unsigned seed = 1U;     // damit dasselbe Bild dasselbe zeigt
    // --- Wohin der Effekt "schaut" ---------------------------------------
    //
    // Gebraucht fuer `decal` (41 Vorkommen): ein Decal ist ein Viereck, das
    // FLACH AUF der getroffenen Flaeche liegt - ohne ihre Normale weiss man
    // nicht, wie es liegen soll.
    //
    // Die Engine uebergibt sie als `ax[0]` an CG_ImpactMark
    // (FxScheduler.cpp:1796). Bei einem Aufprall ist es die Flaechennormale,
    // bei einem fx_runner die Richtung des Spawners.
    //
    // Vorgabe: nach oben. Das ist die haeufigste Lage (Boden) und niemals
    // sinnlos.
    float normal[3]{0.0F, 0.0F, 1.0F};
    // Ein fx_runner mit START_OFF laeuft ab dem ersten `use` und bis zum
    // naechsten (fx_runner_use, g_fx.cpp). -1: kein Ende.
    double endMs = -1.0;
    // --- Woran der Effekt haengt, und ob er mitgeht (FX_RELATIVE) -------
    //
    // Ohne `anker` steht der Effekt fest an `origin`/`normal` - der
    // Normalfall fuer fx_runner und Folgeeffekte.
    //
    // Mit `anker` fragt der Zeichner Ort und Richtung zu einem Zeitpunkt
    // ab. Was das heisst, haengt an `relativ`:
    //
    //   relativ == false   Jedes Teilchen entsteht dort, wo der Anker bei
    //                      SEINER Geburt stand, und fliegt dann frei - wie
    //                      CreateEffect( fx, cg_entities[entNum].lerpOrigin
    //                      ...) im Zeitplaner (FxScheduler.cpp:1403).
    //   relativ == true    FX_RELATIVE: das Teilchen merkt sich nur seinen
    //                      Versatz und haengt in jedem Bild am AKTUELLEN
    //                      Stand des Ankers (CParticle::Update,
    //                      FxPrimitives.cpp:166 ff.; CLine::Update :817;
    //                      CElectricity::Update :938; CCylinder::Update
    //                      :1319). Die Engine setzt das Flag, sobald ein
    //                      Effekt an einem Bolt gespielt wird
    //                      (FxScheduler.cpp:1542), nicht aus der Datei.
    //
    // Gibt der Anker false zurueck, gelten `origin` und `normal`.
    std::function<bool(double ms, float org[3], float fwd[3])> anker;
    bool relativ = false;
};

// --- Ein dynamisches Licht aus einem Effekt ------------------------------
//
// `light` ist ein Primitivtyp wie `particle`. Ueber die 376 .efx-Dateien
// aus assets1 gezaehlt: 72 Vorkommen. Sie bringen kein Bild mit, sondern
// beleuchten, was in der Naehe steht - eine Flamme macht die Wand hell,
// ein Blasterschuss die Figur, an der er vorbeifliegt.
//
// FxScheduler.cpp:1898:
//
//     FX_AddLight( org, mSizeStart, mSizeEnd, mSizeParm,
//                  sRGB, eRGB, mRGBParm, mLife, mFlags );
//
// Groesse ist also der RADIUS, und beide - Radius und Farbe - laufen ueber
// die Lebensdauer von start nach end.
struct EffectLight {
    float origin[3]{};
    float radius = 0.0F;
    float color[3] = {1, 1, 1};   // 0..1
};

// --- Kamerawackeln aus Effekten ----------------------------------------
//
// `CameraShake` ist ein Primitivtyp wie `particle` - 73 Vorkommen in den
// 376 .efx aus assets1, der dritthaeufigste ueberhaupt.
//
// FxScheduler.cpp:1909:
//
//     theFxHelper.CameraShake( org, mElasticity, mRadius, mLife );
//
// Der Kommentar darueber erklaert die ungewoehnliche Belegung: "elasticity
// is actually the intensity ... radius is the distance in which the shake
// will have some effect ... life is how long the effect lasts."
//
// Die Abstandsdaempfung steht in CG_ExplosionEffects (cg_effects.cpp:155):
//
//     if ( dist > radius ) return;
//     intensityScale = 1 - ( dist / radius );
//     realIntensity  = intensity * intensityScale;
//
// Gibt die Staerke zurueck, die an der Kamera ankommt - 0, wenn gerade
// keine wirkt. `camPos` ist der Ort der Kamera, nicht der des Effekts:
// gedaempft wird nach dem Abstand ZUM BETRACHTER.
[[nodiscard]] float effectShakeAt(const std::vector<EffectInstance>& live,
                                  double nowMs, const float camPos[3]);

// Alle Lichter, die zu diesem Zeitpunkt brennen.
[[nodiscard]] std::vector<EffectLight> effectLightsAt(
    const std::vector<EffectInstance>& live, double nowMs);

// --- Wo ein Effekt einen ZWEITEN ausloest -------------------------------
//
// Gemessen (rc319): `impactFx` steht 18 mal in den 376 Effektdateien,
// `deathFx` 2 mal.
//
// Die Engine startet beim Aufprall einen weiteren Effekt
// (FxPrimitives.cpp:318):
//
//     theFxScheduler.PlayEffect( mImpactFxID, trace.endpos,
//                                trace.plane.normal );
//
// Gemeldet wird hier nur WAS, WO und WANN - den Effekt selbst kennt der
// Zeichner nicht, der Name steht in der .efx und die Datei liegt in der
// Verwaltung der Anwendung. Dasselbe Vorgehen wie bei den Lichtern und
// beim Wackeln: rechnen hier, nachschlagen dort.
struct EffectImpact {
    std::string effectName;   // aus der .efx, z.B. "effects/sparks/hit"
    float origin[3]{};
    float normal[3]{};
    double startMs = 0.0;     // auf der Zeitleiste, nicht relativ
    // Woher stammt er? Nur zur Unterscheidung im Protokoll - gezeichnet
    // wird alles gleich.
    enum class Herkunft { Aufprall, Tod, Ausstoss };
    Herkunft herkunft = Herkunft::Aufprall;
    [[nodiscard]] bool vomTod() const { return herkunft == Herkunft::Tod; }
    // Ausgangswert fuer den Folgeeffekt. Haengt am ausloesenden Teilchen
    // (Effekt, Durchgang, Stelle im Zeitplan, Art, Nummer) - nicht an der
    // Uhrzeit: so zeigt derselbe Aufprall bei jedem Bild und nach jedem
    // Zurueckspulen dieselben Funken.
    unsigned seed = 1U;
};

// --- Ein Modell, das an einem Emitter haengt ----------------------------
//
// FxPrimitives.cpp:1376 ff.: ein Emitter mit `useModel` zeichnet an seiner
// Stelle ein .md3 - bei probehead.efx den Droidenkopf selbst, nicht nur
// seine Rauchfahne.
//
// Gedreht wird ueber `angleDelta` (CEmitter::UpdateAngles, ebenda:1534):
//
//     VectorMA( mAngles, mFrameTime * 0.01f, mAngleDelta, mAngles );
//
// Also Grad je Millisekunde mal hundertstel - ueber die ganze Lebenszeit
// aufsummiert ergibt das `angleDelta * 0.01 * alterInMs`. Die Startwinkel
// sind `angle` plus die Richtung des Effekts (vectoangles( ax[0] ),
// FxScheduler.cpp:1831); liegt das Modell still, bremst die Engine die
// Drehung ab (CEmitter::Update, FxPrimitives.cpp:1524).
//
// `scale` ist der Wert der size-Kurve: CEmitter::Draw skaliert die Achsen
// mit mRefEnt.radius (FxPrimitives.cpp:1394).
struct EmitterModelDraw {
    std::string modelPath;
    float origin[3]{};
    float angles[3]{};   // Nick, Gier, Roll in Grad
    float scale = 1.0F;
};

// Alle Emittermodelle, die zu diesem Zeitpunkt fliegen - EINES JE TEILCHEN
// (count, delay, Wiederholung), mit dem Modell, das das Teilchen aus der
// Liste gewuerfelt hat.
//
// Ohne Geometrie fliegen sie geradeaus - genau wie die Partikel.
[[nodiscard]] std::vector<EmitterModelDraw> emitterModelsAt(
    const std::vector<EffectInstance>& live, double nowMs,
    const BspGeometry* geo);

// Alle Folgeeffekte EINER Stufe, die bis `nowMs` ausgeloest wurden - je
// TEILCHEN, nicht je Primitive: vom Aufprall (`impactFx`, bei jedem
// Aufprall bis es liegt), vom Lebensende (`deathFx`), aus der Fahne eines
// Emitters (`emitFx`), vom FxRunner (`playfx`, mit count und delay) und am
// Ende einer gespurten Line/Electricity (org2fromTrace + traceImpactFx).
// Mehrere Stufen: effectFollowUpsAt.
//
// Das Lebensende, CParticle::Die (FxPrimitives.cpp:102 ff.):
//
//     if ( mFlags & FX_DEATH_RUNS_FX && !(mFlags & FX_KILL_ON_IMPACT) )
//     {
//         VectorSet( norm, Q_flrand(-1,1), Q_flrand(-1,1), Q_flrand(-1,1) );
//         VectorNormalize( norm );
//         theFxScheduler.PlayEffect( mDeathFxID, mOrigin1, norm );
//     }
//
// Zwei Dinge daran, die man sonst uebersieht: wer WIRKLICH an der Wand
// stirbt, bekommt keinen Todeseffekt - aber FX_Add loescht
// FX_KILL_ON_IMPACT, wenn die Lebenszeit regulaer ablaeuft (FxUtil.cpp:241),
// ein killOnImpact-Teilchen ohne Aufprall bekommt ihn also doch. Und die
// Normale ist ZUFAELLIG, nicht etwa nach oben.
//
// Ohne Geometrie gibt es keine Wand und damit keinen Aufprall.
[[nodiscard]] std::vector<EffectImpact> effectImpactsAt(
    const std::vector<EffectInstance>& live, double nowMs,
    const BspGeometry* geo);

// Ein dynamisches Licht auf ein bereits ermitteltes Licht draufrechnen.
//
// Genau wie R_SetupEntityLighting es tut (tr_light.cpp:435 ff.):
//
//     power = DLIGHT_AT_RADIUS * ( dl->radius * dl->radius );
//     if ( d < DLIGHT_MINIMUM_RADIUS ) d = DLIGHT_MINIMUM_RADIUS;
//     d = power / ( d * d );
//     VectorMA( ent->directedLight, d, dl->color, ent->directedLight );
//     VectorMA( lightDir, d, dir, lightDir );
//
// Beide Konstanten stehen dort auf 16 (ebenda:31 und 34). Das Licht wird
// also auf das GERICHTETE addiert, nicht auf das Umgebungslicht, und die
// Richtung wandert mit - deshalb hellt ein naher Blitz die zugewandte
// Seite auf und nicht die Figur als Ganzes.
void addDynamicLights(GridLight& licht, const float origin[3],
                      const std::vector<EffectLight>& lichter);

// Wie viele Vierecke hoechstens? Ein Effekt mit count 500 und Wiederholung
// erzeugt sonst mehr Dreiecke als die halbe Karte.
inline constexpr int kMaxQuadsPerEffect = 2000;

// Shadername -> Nummer in TextureSet::byShader.
//
// Ohne das werden Partikel als farbige Vierecke OHNE Bild gezeichnet - und
// genau so sahen sie bisher aus. Jede .efx nennt fuer jedes Primitiv einen
// Shader ("gfx/misc/steam", "gfx/effects/ftail"), und der bringt nicht nur
// das Bild mit, sondern auch die Mischart: fast alle Effektshader sind
// additiv (blendFunc GL_ONE GL_ONE). Schwarz ist darin unsichtbar - deshalb
// sind Rauch- und Feuerbilder schwarz hinterlegt und ergeben trotzdem
// weiche Wolken statt Kaesten.
using EffectShaderSlots = std::map<std::string, int>;

// --- Folgeeffekte, mehrstufig ----------------------------------------
//
// Ein Folgeeffekt kann selbst wieder einen ausloesen: eine Explosion wirft
// Truemmer (Emitter), die Truemmer ziehen Rauch (emitFx), der Rauch
// entsteht als eigener Effekt. Bisher reichte die Ansicht nur EINE Stufe
// weiter - die Truemmer flogen, rauchten aber nicht.
//
// Die Engine kennt KEINE Tiefengrenze: PlayEffect (FxScheduler.cpp:1125)
// plant jeden Folgeeffekt wie einen neuen. Gebremst wird sie allein durch
// den Vorrat MAX_EFFECTS = 1200 (FxPrimitives.h:31) - ist er voll, wird der
// aelteste Platz geraeumt (FX_GetValidEffect, FxUtil.cpp:164).
//
// Hier gibt es beides als Schranke, weil eine Kette, die sich selbst
// ausloest (A startet A), in der Engine vom Vorrat gebremst wird und bei
// uns sonst gar nicht:
//
//   kMaxFolgeTiefe    so viele Stufen. Gezaehlt ueber die 649 .efx der
//                     Mod (base und MD): die laengste Kette hat vier Stufen
//                     (exegol/exegol_lightning_strikes_rift), Kreise gibt
//                     es keine. Acht laesst Luft und faengt Kreise ab.
//   kMaxFolgeEffekte  so viele Folgeeffekte insgesamt - dieselbe Zahl wie
//                     der Vorrat der Engine, nur je Effekt statt je
//                     Teilchen gezaehlt.
inline constexpr int kMaxFolgeTiefe = 8;
inline constexpr std::size_t kMaxFolgeEffekte = 1200;

// Schlaegt einen Effekt nach seinem Namen nach ("sparks/hit" - ohne
// "effects/" und ohne ".efx", so wie er in der Datei steht). nullptr, wenn
// es ihn nicht gibt. Die Anwendung kennt die Dateien, der Zeichner nicht.
using EffectLookup = std::function<const efx::Effect*(const std::string&)>;

// Alle Folgeeffekte aller Stufen, die zu `nowMs` noch etwas zeigen (oder
// deren eigene Folgen es noch koennten) - fertig als EffectInstance zum
// Anhaengen an die Liste fuer buildEffectMesh.
[[nodiscard]] std::vector<EffectInstance> effectFollowUpsAt(
    const std::vector<EffectInstance>& live, double nowMs,
    const BspGeometry* geo, const EffectLookup& lookup,
    int maxTiefe = kMaxFolgeTiefe, std::size_t maxEffekte = kMaxFolgeEffekte);

// --- Der Vollbildblitz (Primitivtyp "Flash") -------------------------
//
// 19 Vorkommen in den 649 .efx der Mod (Explosionen, Erschuetterung,
// Machtstoss). CFlash (FxPrimitives.cpp:2249 ff.):
//
//   Init   Richtung und Abstand vom Auge zum Blitz:
//              dis > 600, oder hinter der Blickrichtung (mod < 0.5) und
//              weiter als 100   -> mod = 0, kein Blitz
//              hinter der Blickrichtung, aber naeher als 100 -> mod += 1.1
//              mod *= 1 - dis*dis / (600*600)
//          Start- und Endfarbe werden mit mod multipliziert.
//   Draw   ein Sprite mit dem Shader der .efx, 8 Einheiten vor dem Auge,
//          Halbmesser 8 * tan(fov_x / 2) - bedeckt also die ganze Breite.
//          shaderRGBA = Farbe (geklemmt auf 0..1), Alpha IMMER 255.
//
// Alpha- und size-Bloecke stehen zwar in den Dateien, werden von
// FX_AddFlash aber nicht gelesen (FxUtil.cpp:1381, auskommentiert) - hier
// deshalb auch nicht.
struct ScreenFlash {
    std::string shader;      // klein geschrieben, wie EffectShaderSlots
    float rgb[3]{};          // fertig: mod, Kurve, geklemmt
    float origin[3]{};       // wo der Blitz ausgeloest wurde
};

// Alle Blitze zu diesem Zeitpunkt. `eye` ist der Standort der Kamera,
// `fwd` ihre Blickrichtung (Einheitsvektor).
[[nodiscard]] std::vector<ScreenFlash> effectScreenFlashesAt(
    const std::vector<EffectInstance>& live, double nowMs, const float eye[3],
    const float fwd[3]);

// Haengt die Blitze als Sprites an ein Effektnetz an - ein Viereck je
// Blitz, in einem eigenen Stapel je Shader. Durch das Netz statt ueber die
// 2D-Ueberlagerung, weil nur so der Shader (meist additiv, mit Bild) und
// die Reihenfolge stimmen: im Spiel liegt der Blitz IN der Szene, unter den
// Kinobalken (CG_DrawActiveFrame zeichnet die Balken danach).
//
// right/up: die Achsen der Kamera wie bei buildEffectMesh; fovXGrad: der
// waagerechte Bildwinkel.
void appendScreenFlashes(BspMesh& mesh, const std::vector<ScreenFlash>& blitze,
                         const float eye[3], const float fwd[3],
                         const float right[3], const float up[3],
                         float fovXGrad,
                         const EffectShaderSlots* slots = nullptr);

// Die Summe aller Blitze als eine Farbe, zum Aufaddieren auf das Bild -
// fuer Aufrufer ohne Shader (Proben, Protokoll). Das ist die Wirkung eines
// ADDITIVEN Blitzshaders mit weissem Bild; die Ansicht selbst nimmt
// appendScreenFlashes.
struct ScreenFlashOverlay {
    float rgb[3]{};
    bool aktiv = false;
};
[[nodiscard]] ScreenFlashOverlay screenFlashOverlay(
    const std::vector<ScreenFlash>& blitze);


// Baut das Netz fuer einen Zeitpunkt.
//
// right und up sind die Achsen der Kamera - die Vierecke stehen senkrecht
// zur Blickrichtung, so wie die Engine ihre Partikel ausrichtet
// (FxPrimitives.cpp, CParticle::Draw benutzt dafuer die Achsen der Ansicht).
//
// nowMs ist die Zeit auf der Zeitleiste, nicht seit dem Ausloesen.
// --- Wo steht die Kamera, und was kann sie sehen? ----------------------
//
// Ohne diese Angabe wird JEDER Effekt gebaut - auch hinter der Kamera und
// in anderen Raeumen. Das ist nicht bloss Verschwendung an Dreiecken: ein
// Teilchen mit `usePhysics` macht einen Spurtest gegen die ganze
// Kartengeometrie, und das ist der teuerste Einzelposten im Programm.
//
// Gemessen an md_am_sith, Kamera bei (-30, 7084, 171), von wo aus KEIN
// EINZIGES Effektdreieck im Bild landet:
//
//     Effektnetz mit Spurtests   351.8 ms
//     Effektnetz ohne Spurtests    0.6 ms
//
// Faktor 600, fuer nichts. Und weil die Zeit an der Physik haengt und nicht
// an der Menge, kostet ein einzelner Runner dasselbe wie alle 26.
//
// `viewer` ist der Standort der Kamera. Ist er gesetzt UND `geo` hat eine
// Sichtbarkeitstabelle, wird ein Runner uebersprungen, dessen Cluster von
// dort aus nicht sichtbar ist. Ohne Tabelle oder ohne Standort bleibt alles
// wie bisher - die Vorgabe aendert nichts.
struct EffectCull {
    float viewer[3]{};
    bool haveViewer = false;
    // Runner naeher als das werden IMMER gebaut, auch wenn die Tabelle sie
    // wegwirft. Die Tabelle arbeitet auf Raeumen, und ein Effekt an einer
    // Tuerschwelle kann in beiden liegen; grosszuegig zu sein kostet hier
    // fast nichts und verhindert Aussetzer.
    float alwaysRadius = 512.0F;
    // Die Sichtbarkeitstabelle ueberhaupt fragen? Die Ansicht schaltet das
    // ab (siehe app_view3d.cpp, "Die Keulung ist NOCH NICHT
    // eingeschaltet") und reicht trotzdem den Standort herein: ohne Auge
    // gibt es weder die Bandbreite nach RB_SurfaceLine noch die Richtung
    // eines Blitzes (RB_SurfaceElectricity) noch CParticle::Cull.
    bool sichtTabelle = true;
};

// --- Der Merkzettel der Bahnen ---------------------------------------
//
// buildEffectMesh, emitterModelsAt und effectFollowUpsAt merken sich jede
// Bahn mit Aufprall samt Spurbuch (efxdraw.cpp, flugMitMerk), damit ein
// spaeteres UND ein frueheres Bild sie nicht neu spuren muss. Voll wird er
// nicht (bei 65536 Bahnen wird geleert). Leeren aendert kein Bild - es
// kostet nur, dass die naechsten Bilder wieder spuren. Gebraucht fuer die
// Proben (Vorlage gegen Hilfe) und fuer wen Speicher freigeben will.
void vergissEffektBahnen();

[[nodiscard]] BspMesh buildEffectMesh(const std::vector<EffectInstance>& live,
                                      double nowMs, const float right[3],
                                      const float up[3],
                                      const EffectShaderSlots* slots = nullptr,
                                      const BspGeometry* geo = nullptr,
                                      const EffectCull* cull = nullptr);

}  // namespace bhed
