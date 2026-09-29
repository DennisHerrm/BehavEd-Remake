// Zeitgeber und Bewegung: wann erscheint was, und wohin fliegt es.
//
// Zwei Teile, beide ohne Grafikschnittstelle und damit pruefbar:
//
//   Zeitplanung   welches Segment loest wann aus, und wie oft
//   Bahn          wo ist eine Primitive zum Zeitpunkt t
//
// Beides ist aus `FxScheduler.cpp` und `FxPrimitives.cpp` hergeleitet. An zwei
// Stellen weicht die Engine von dem ab, was man erwarten wuerde - siehe
// `positionAt`.
#pragma once

#include <functional>
#include <cstdint>
#include <vector>

#include "bhed/efx/vec3.h"
#include "bhed/efx/effect.h"

namespace bhed::efx::sim {

// Wie fragt man die Welt nach einer Wand? Eine Funktion statt der
// BSP-Geometrie direkt: so bleibt efx frei von der Kartenschicht, und die
// Proben koennen eine erfundene Wand einsetzen.
//
// Gibt `true` und fuellt Punkt und Normale, wenn zwischen `von` und `bis`
// etwas Festes liegt.
using TraceFn = std::function<bool(const camera::Vec3& von,
                                   const camera::Vec3& bis,
                                   camera::Vec3& punkt,
                                   camera::Vec3& normale)>;

// Ein Zufallsgeber, der hereingereicht wird statt intern zu ziehen. Sonst
// laesst sich nichts davon pruefen: dieselbe Datei muesste zweimal dasselbe
// ergeben, und das tut sie nur mit bekanntem Ausgangswert.
class Random {
public:
    explicit Random(unsigned seed = 1u) : state_(seed ? seed : 1u) {}
    float next();                       // 0 bis 1
    float range(float low, float high);
    float pick(const Range& r);         // wuerfelt zwischen min und max, wenn ranged

private:
    unsigned state_;
};

// Eine geplante Ausloesung.
struct Spawn {
    int primitiveIndex = 0;
    float timeMs = 0.0f;  // relativ zum Ausloesen des Effekts
};

// Plant alle Ausloesungen eines Effekts.
//
// Je Primitive: `count`-mal, jedes mit eigener `delay`. Ist
// `evenDistribution` gesetzt, werden die Verzoegerungen gleichmaessig ueber die
// Spanne verteilt statt gewuerfelt - genau das ist der Sinn des Flags.
// enabledMask: welche Segmente mitspielen. Leer heisst alle.
//
// Das Haekchen in der Segmentliste ist eine reine Vorschau-Einstellung des
// Editors - es steht nicht in der Datei und gehoert deshalb nicht in die
// Primitive, sondern hierher als Parameter.
std::vector<Spawn> schedule(const Effect& effect, Random& random,
                            const std::vector<bool>& enabledMask = {});

// Die Bahn einer Primitive.
//
// **Achtung, hier weicht die Engine von der Physik ab.** Sie rechnet die Lage
// geschlossen statt schrittweise:
//
//     realVel[2] += 0.5 * gravity * t
//     realVel    += t * accel
//     org         = start + t * realVel
//
// Ausmultipliziert wirkt die Schwerkraft mit `0.5*g*t^2` - richtig - die
// Beschleunigung aber mit `1.0*a*t^2`, also **doppelt so weit**, wie eine
// saubere Integration ergaebe.
//
// Raven hat das selbst bemerkt; im Quelltext steht daneben:
// *"NOTE: not sure if this is even 100% correct math-wise"*.
//
// Wir bauen es genau so nach. Das Ziel ist nicht richtige Physik, sondern
// dieselbe Bahn wie im Spiel - eine Vorschau, die anders fliegt als das Spiel,
// ist schlimmer als keine.
// Eine Ebene, an der abgeprallt wird. `normal` zeigt in den freien Raum.
struct Plane {
    camera::Vec3 normal{0.0f, 0.0f, 1.0f};
    float distance = 0.0f;  // normal*x = distance liegt auf der Ebene
};

// Was ein Wegstueck getroffen hat.
struct Hit {
    bool hit = false;
    float fraction = 1.0f;   // 0..1 entlang des Wegstuecks
    camera::Vec3 point;
    camera::Vec3 normal;
};

// Trifft das Wegstueck von `from` nach `to` eine der Ebenen?
//
// Die frueheste Beruehrung gewinnt. Wer nur die erste gefundene nimmt, bekommt
// in einer Ecke den falschen Abpraller.
Hit trace(const camera::Vec3& from, const camera::Vec3& to,
          const std::vector<Plane>& planes);

// Der Testraum als sechs Ebenen, alle nach innen zeigend.
std::vector<Plane> roomPlanes(float halfWidth, float halfDepth, float height);

// Ein Abschnitt der Bahn: ab `startMs` gilt diese Geschwindigkeit ab diesem
// Punkt.
//
// Mit Abprallern laesst sich die Bahn nicht mehr in einer Formel ausdruecken -
// nach jedem Aufprall gilt eine neue Geschwindigkeit, und **wann** er kommt,
// haengt vom Weg ab. Also wird sie beim Ausloesen einmal in Abschnitte zerlegt;
// jeder einzelne ist wieder geschlossen ausrechenbar.
//
// Das haelt die Vorschau bildratenunabhaengig: sie laeuft nicht Schritt fuer
// Schritt mit, sondern schlaegt nach.
struct PathSegment {
    float startMs = 0.0f;
    camera::Vec3 origin;
    camera::Vec3 velocity;
    camera::Vec3 acceleration;
    float gravity = 0.0f;
};

// Was beim Abprallen herauskommt.
struct Path {
    std::vector<PathSegment> segments;
    // Zeitpunkte und Orte der Aufpralle - fuer impactfx und killOnImpact.
    std::vector<float> impactMs;
    std::vector<camera::Vec3> impactPoint;
    std::vector<camera::Vec3> impactNormal;
    bool killed = false;      // killOnImpact hat zugeschlagen
    float killedMs = 0.0f;

    // Ab wann es liegen bleibt. Danach kommt nichts mehr - die Engine
    // loescht an dieser Stelle `FX_APPLY_PHYSICS` und `FX_IMPACT_RUNS_FX`.
    // 0 heisst: kam nie zur Ruhe.
    float settledMs = 0.0f;
};

// Zerlegt die Bahn in Abschnitte und sammelt die Aufpralle.
//
// elasticity ist der Anteil der Geschwindigkeit, der einen Aufprall uebersteht.
// stopOnFirst entspricht `killOnImpact`.
Path buildPath(const camera::Vec3& origin, const camera::Vec3& velocity,
               const camera::Vec3& acceleration, float gravity, float lifeMs,
               const std::vector<Plane>& planes, float elasticity,
               bool stopOnFirst);

// Die Lage zu einem Zeitpunkt, ueber alle Abschnitte hinweg.
camera::Vec3 positionOnPath(const Path& path, float msSinceSpawn);

// --- Die Bahn MIT Aufprall ---------------------------------------------
//
// Gemeldet: "die EFX werden wohl noch nicht korrekt gerendert."
// Gemessen (rc319): `usePhysics` steht 166 mal in den 376 Effektdateien.
//
// `positionAt` unten rechnet die freie Bahn - Beschleunigung und
// Schwerkraft, geschlossen. Wer die Wand ignoriert, fliegt hindurch:
// Funken fallen durch den Boden, Truemmer verschwinden in der Mauer.
//
// Die Engine (CParticle::UpdateOrigin, FxPrimitives.cpp:249 ff.) rechnet
// SCHRITTWEISE und prallt ab:
//
//     UpdateVelocity();                        // vel += accel * dt
//     new_origin = origin + dt * vel;
//     wenn dazwischen etwas Festes liegt:
//         dot = vel . normale
//         vel -= 2 * dot * normale              // spiegeln
//         vel *= elasticity                     // und daempfen
//         wenn normale.z > 0 und vel.z < 4:
//             vel = 0; accel = 0                // liegenbleiben
//         origin = trace.endpos
//
// WICHTIG, und ich hatte es zuerst falsch: die Beschleunigung wirkt
// IMMER, auch ohne `usePhysics`. Das Flag steuert allein den Aufprall -
// `UpdateVelocity()` steht VOR der Flagpruefung. Deshalb war die
// Schwerkraft in behaved auch nie falsch; es fehlte nur die Wand.
//
// `flugbahn` gibt den Ort nach `secondsSinceSpawn` zurueck, mit Aufprall.
// Ohne Geometrie oder ohne Aufprall ist es dasselbe wie `positionAt`.
struct FlugErgebnis {
    camera::Vec3 position;
    bool gestorben = false;   // killOnImpact und getroffen
    bool liegt = false;       // zur Ruhe gekommen
    int abpraller = 0;
    // --- Wo und wann der ERSTE Aufprall war -----------------------------
    //
    // Gebraucht fuer `impactFx`: die Engine startet dort einen zweiten
    // Effekt (FxPrimitives.cpp:318):
    //
    //     theFxScheduler.PlayEffect( mImpactFxID, trace.endpos,
    //                                trace.plane.normal );
    //
    // Nur der ERSTE zaehlt. Die Engine ruft es zwar bei jedem Aufprall,
    // aber ein Partikel mit impactFx traegt fast immer auch killOnImpact -
    // und selbst ohne waere ein Funkenschlag je Abpraller mehr, als die
    // Vorlage meint.
    bool traf = false;
    camera::Vec3 trefferPunkt;
    camera::Vec3 trefferNormale;
    float trefferSekunde = 0.0F;   // seit dem Entstehen

    // --- JEDER Aufprall, bis es liegt -----------------------------------
    //
    // Der Absatz oben ("nur der ERSTE zaehlt") war eine Vereinfachung, und
    // sie weicht von der Engine ab: CParticle::UpdateOrigin
    // (FxPrimitives.cpp:318) startet den Aufpralleffekt bei JEDEM Treffer,
    // bis das Teilchen zur Ruhe kommt - erst dort loescht sie
    // FX_IMPACT_RUNS_FX (ebenda:342). Ein springender Truemmerbrocken mit
    // impactFx staubt also bei jedem Aufsetzen. `traf` und `treffer*`
    // bleiben fuer den ersten stehen; hier stehen alle.
    //
    // Fest bemessen statt std::vector: flugbahn laeuft je Teilchen und je
    // Bild, und eine Speicheranforderung darin kostet mehr als die Rechnung.
    // Acht reichen - mit Elastizitaet unter eins wird jeder Sprung kuerzer,
    // und nach wenigen liegt es.
    static constexpr int kMaxAufpralle = 8;
    int aufpralle = 0;
    camera::Vec3 aufprallPunkt[kMaxAufpralle];
    camera::Vec3 aufprallNormale[kMaxAufpralle];
    float aufprallSekunde[kMaxAufpralle]{};

    // Seit wann es liegt (nur gueltig mit `liegt`). Gebraucht fuer die
    // Drehung eines Emittermodells: CEmitter::Update (FxPrimitives.cpp:1524)
    // bremst sie, sobald sich der Ort nicht mehr aendert.
    float liegtSekunde = 0.0F;

    // Ab welchem Alter sich am Ergebnis NICHTS mehr aendert (liegt oder an
    // der Wand gestorben, und zwar in einem vollen Schritt). -1: noch nicht.
    // Damit darf ein Aufrufer das Ergebnis fuer jedes spaetere Alter
    // wiederverwenden, statt die ganze Bahn samt Spurtests neu zu rechnen -
    // siehe den Merkzettel in efxdraw.cpp.
    float endgueltigAb = -1.0F;
};

// `elasticity` 0 heisst: bleibt liegen. `killOnImpact`: endet beim ersten
// Treffer. `schrittMs` ist die Schrittweite - die Engine benutzt die
// Bildzeit, wir eine feste, damit dasselbe Alter immer dasselbe Bild gibt.
[[nodiscard]] FlugErgebnis flugbahn(const camera::Vec3& origin,
                                    const camera::Vec3& velocity,
                                    const camera::Vec3& acceleration,
                                    float gravity, float secondsSinceSpawn,
                                    float elasticity, bool killOnImpact,
                                    const TraceFn& trace);

// --- Die Bahn FORTSETZEN statt jedes Bild von vorn --------------------
//
// flugbahn rechnet vom Ausloesen an, Schritt fuer Schritt mit Spurtest.
// Fuer ein Teilchen, das zwoelf Sekunden lebt, sind das im letzten Bild
// sechstausend Spurtests - in JEDEM Bild. `FlugZustand` merkt sich den
// Stand nach dem letzten vollen Schritt; der naechste Aufruf mit einem
// spaeteren Alter macht dort weiter. Das Ergebnis ist bitgleich mit
// flugbahn fuer dasselbe Alter (dieselben Schritte in derselben
// Reihenfolge); ein frueheres Alter setzt an einer Marke an (Spurbuch).
//
// Der Zustand gehoert zu GENAU EINER Bahn: wer ihn mit anderen Eingaben
// wiederverwendet, bekommt Unsinn. efxdraw.cpp haengt ihn deshalb an einen
// Schluessel aus allen Eingaben.
//
// --- Das Spurbuch: auch ZURUECK ohne Spurtests ------------------------
//
// Bis hierher galt: "ein frueheres Alter faengt einfach von vorn an" - mit
// allen Spurtests. Gemessen an md_ta_sith (intro_sith, 17 Bilder im Abstand
// von 500 ms, in zufaelliger Reihenfolge): 26,9 s statt 14,0 s der Reihe
// nach, einzelne Bilder ueber 3 s. Die Zeitleiste wird aber hin und her
// gezogen, nicht nur abgespielt.
//
// Ein Schritt besteht aus zwei Teilen: v und p fortschreiben (ein paar
// Rechenschritte) und der Spurtest (das Teure). Der Spurtest haengt nur an
// Anfang und Ende des Schritts - und die sind beim zweiten Mal dieselben
// Bits wie beim ersten. Also merkt sich der Zustand je Schritt, OB er traf,
// und wenn ja, wo (`buch`, nur die Treffer - die allermeisten Schritte
// treffen nichts). Ein Wiederholungslauf rechnet v und p genauso und nimmt
// das Ergebnis des Spurtests aus dem Buch: bitgleich, ohne einen einzigen
// Spurtest. Damit er nicht vom Ausloesen an laufen muss, liegt alle
// kMarkenAbstand Schritte eine Marke mit p und v.
//
// `p`, `v`, `a`, `schritte`, `aus` bleiben der WEITESTE Stand; ein
// frueheres Alter setzt an der Marke davor an und laesst ihn stehen.
inline constexpr int kMarkenAbstand = 128;   // 256 ms
struct FlugZustand {
    camera::Vec3 p;
    camera::Vec3 v;
    camera::Vec3 a;
    int schritte = 0;        // so viele volle Schritte sind gemacht
    FlugErgebnis aus;        // Treffer, Aufpralle, liegt - bis hierher
    bool gueltig = false;
    struct Treffer {
        int schritt = 0;     // der wievielte volle Schritt (ab 1)
        camera::Vec3 punkt;
        camera::Vec3 normale;
    };
    std::vector<Treffer> buch;   // nach Schritt geordnet
    int bekannt = 0;             // die Schritte 1..bekannt stehen im Buch
    struct Marke {
        int schritt = 0;
        camera::Vec3 p;
        camera::Vec3 v;
    };
    std::vector<Marke> marken;   // nur, solange das Teilchen fliegt
};
// Kaeme flugbahnWeiter mit diesem Zustand fuer dieses Alter OHNE einen
// neuen Spurtest aus (alles im Buch, oder die Bahn ist zu Ende)? Fuer das
// Vorrechnen in efxdraw.cpp: nur Bahnen, die Arbeit machen, gehen an die
// Hilfsfaeden. Der angeschnittene letzte Schritt zaehlt nicht mit.
[[nodiscard]] bool flugOhneSpur(const FlugZustand& zustand,
                                float secondsSinceSpawn);
[[nodiscard]] FlugErgebnis flugbahnWeiter(const camera::Vec3& origin,
                                          const camera::Vec3& velocity,
                                          const camera::Vec3& acceleration,
                                          float gravity, float secondsSinceSpawn,
                                          float elasticity, bool killOnImpact,
                                          const TraceFn& trace,
                                          FlugZustand& zustand);

// --- Die Bahn in festen Abstaenden abtasten, in EINEM Durchlauf ---------
//
// Gebraucht fuer die Fahne eines Emitters (emitFx): CEmitter::Draw
// (FxPrimitives.cpp:1406 ff.) prueft alle TRAIL_RATE = 8 ms, ob die Bahn
// weit genug gekommen ist, um den naechsten Effekt fallen zu lassen.
//
// Vorher rief der Zeichner dafuer `flugbahn` fuer JEDEN Abtastpunkt von
// vorn auf - mit eingeschalteter Physik ist das quadratisch: fuenf Sekunden
// Emitterleben, alle zwanzig Millisekunden neu ab null, sind rund
// dreihunderttausend Spurtests je Emitter und Bild. Hier laeuft die Bahn
// einmal, und `jePunkt` bekommt die Lage zu jedem Abtastzeitpunkt.
//
// Dieselben Schritte wie `flugbahn`: an jedem Abtastpunkt kommt dieselbe
// Lage heraus, die `flugbahn` fuer dieses Alter liefert (bis auf den
// letzten, angeschnittenen Schritt). Gibt `jePunkt` false zurueck, ist
// Schluss. Das Ergebnis ist das von `flugbahn(bisSekunde)`.
//
// Mit `zustand` (derselbe wie fuer flugbahnWeiter, fuer DIESELBE Bahn)
// laeuft die Abtastung zwar weiterhin vom Ausloesen an - jeder Punkt wird
// gemeldet -, die Spurtests aber kommen aus dem Buch, soweit es reicht.
// Vorher lief die Fahne eines Emitters in JEDEM Bild mit allen Spurtests
// von vorn.
FlugErgebnis flugbahnAbtasten(
    const camera::Vec3& origin, const camera::Vec3& velocity,
    const camera::Vec3& acceleration, float gravity, float bisSekunde,
    float abstandSekunde, float elasticity, bool killOnImpact,
    const TraceFn& trace,
    const std::function<bool(float sekunde, const camera::Vec3& lage)>& jePunkt,
    FlugZustand* zustand = nullptr);

camera::Vec3 positionAt(const camera::Vec3& origin, const camera::Vec3& velocity,
                        const camera::Vec3& acceleration, float gravity,
                        float secondsSinceSpawn);

}  // namespace bhed::efx::sim
