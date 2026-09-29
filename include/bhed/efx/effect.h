// Das Datenmodell einer .efx-Datei.
//
// Feldnamen, Wertebereiche und Flagbits sind aus dem Spielcode uebernommen
// (code/cgame/FxTemplate.cpp, FxPrimitives.h, FxScheduler.h in OpenJK).
// Wo das Spiel mehrere Schreibweisen akzeptiert - "parm"/"parms",
// "shader"/"shaders", "size"/"width" - merken wir uns, welche in der Datei
// stand, damit ein unveraendertes Feld beim Speichern unveraendert bleibt.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bhed::efx {

// ---------------------------------------------------------------------------
// Dialekt
//
// Singleplayer und Multiplayer sind zwei getrennte Zweige des FX-Systems mit
// getrennten Parsern (code/cgame/FxTemplate.cpp gegen
// codemp/client/FxTemplate.cpp). Sie sind sich sehr aehnlich, aber nicht
// gleich: jeder Zweig kennt Flags, die der andere nicht kennt und stumm
// ueberliest. Wer nicht weiss, fuer welchen Zweig er baut, merkt das erst im
// Spiel - dort passiert dann einfach nichts.
enum class Dialect {
    SP,    // Jedi Academy Singleplayer (Movie Duels)
    MP,    // Jedi Academy Multiplayer
    Both,  // nur, was beide Zweige verstehen
};

const char* dialectName(Dialect d);

// ---------------------------------------------------------------------------
// Primitivtypen

enum class PrimitiveType {
    Particle,
    Line,
    Tail,
    Cylinder,
    Emitter,
    Sound,
    Decal,
    OrientedParticle,
    Electricity,
    FxRunner,
    Light,
    CameraShake,
    ScreenFlash,
};

// Name, wie er in der Datei steht (Gross-/Kleinschreibung wie bei Raven).
const char* typeName(PrimitiveType t);
// Gegenrichtung; ignoriert Gross-/Kleinschreibung wie das Spiel.
std::optional<PrimitiveType> typeFromName(std::string_view name);
// Kurzbeschreibung fuer die Oberflaeche, Text aus dem Originaleditor.

// ---------------------------------------------------------------------------
// Wertebereiche
//
// Fast jedes Feld darf entweder ein fester Wert oder eine Spanne sein, aus der
// beim Abspielen gewuerfelt wird. "ranged" merkt sich, welche Form in der
// Datei stand: 20 und "20 20" sind identisch in der Wirkung, aber nicht im
// Text, und ein Editor soll Dateien nicht ohne Grund umschreiben.

struct Range {
    float min = 0.0f;
    float max = 0.0f;
    bool set = false;
    bool ranged = false;

    static Range single(float v) { return Range{v, v, true, false}; }
    static Range span(float a, float b) { return Range{a, b, true, true}; }
};

struct Vec3 {
    float v[3]{0.0f, 0.0f, 0.0f};
    float& operator[](int i) { return v[i]; }
    float operator[](int i) const { return v[i]; }
};

struct Vec3Range {
    Vec3 min;
    Vec3 max;
    bool set = false;
    bool ranged = false;
};

// ---------------------------------------------------------------------------
// Uebergangsarten (die Kurven, die transitions.exe zeigt)

enum : int {
    kCurveLinear = 0x1,
    kCurveRandom = 0x2,
    kCurveNonLinear = 0x4,
    kCurveWave = 0x8,
    kCurveClamp = 0xC,  // Achtung: NonLinear|Wave, kein eigenes Bit.
                        // Der Auswerter hat andere Bits - siehe
                        // curve::ausDatei.
};

// Ein animierbarer Kanal: Startwert, Endwert, ein Parameter und die Kurve.
// Wird fuer rgb, alpha, size, size2 und length verwendet.
struct Channel {
    Range start;
    Range end;
    Range parm;
    int curveFlags = 0;
    bool present = false;  // Block stand ueberhaupt in der Datei

    // Merkt sich die tatsaechlich benutzten Schluesselnamen.
    bool parmPlural = false;   // "parms" statt "parm"
    bool flagsPlural = true;   // "flags" statt "flag"

    // Die Woerter, wie sie in der Datei standen. Noetig, weil curveFlags
    // mehrdeutig ist: "nonlinear wave" ergibt exakt dieselben Bits wie
    // "clamp". Aus den Bits allein laesst sich nicht mehr sagen, was der
    // Autor geschrieben hat - und genau das will die Pruefung wissen.
    std::vector<std::string> curveWords;
};

// rgb hat drei Komponenten und braucht deshalb eine eigene Form.
struct ColorChannel {
    Vec3Range start;
    Vec3Range end;
    Range parm;
    int curveFlags = 0;
    bool present = false;
    bool parmPlural = false;
    bool flagsPlural = true;
    std::vector<std::string> curveWords;
};

// ---------------------------------------------------------------------------
// Flags

// "flags" - werden an die erzeugte Primitive weitergereicht.
enum : uint32_t {
    kFlagDepthHack = 0x00100000,
    kFlagRelative = 0x00200000,
    kFlagSetShaderTime = 0x00400000,
    kFlagExpensivePhysics = 0x00800000,
    kFlagGhoul2Trace = 0x00020000,   // teilt sich das Bit mit FX_SIZE2_RAND
    kFlagGhoul2Decals = 0x00040000,  // teilt sich das Bit mit FX_SIZE2_NONLINEAR
    kFlagAttachedModel = 0x01000000,
    kFlagApplyPhysics = 0x02000000,
    kFlagUseBBox = 0x04000000,

    // Dieselben drei Bits, beim Electricity mit anderer Bedeutung.
    //
    //   FX_TAPER  0x01000000  == FX_ATTACHED_MODEL
    //   FX_BRANCH 0x02000000  == FX_APPLY_PHYSICS
    //   FX_GROW   0x04000000  == FX_USE_BBOX
    //
    // Der Parser kennt fuer taper, branch und grow KEINE eigenen Schluessel.
    // Wer sie will, schreibt in die .efx-Datei "flags useModel usePhysics
    // useBBox" - und das Electricity liest daraus Verjuengung, Verzweigung
    // und Wachsen. Ravens Editor zeigt dafuer drei sinnvoll beschriftete
    // Haekchen; in der Datei steht etwas voellig anderes.
    //
    // Ohne diesen Zusammenhang sucht man vergeblich nach einem Schluessel
    // namens "taper".
    kFlagElectricityTaper = 0x01000000,
    kFlagElectricityBranch = 0x02000000,
    kFlagElectricityGrow = 0x04000000,
    kFlagUseAlpha = 0x08000000,
    kFlagEmitFx = 0x10000000,
    kFlagDeathRunsFx = 0x20000000,
    kFlagKillOnImpact = 0x40000000,
    kFlagImpactRunsFx = 0x80000000,

    // Nur Multiplayer. Alle drei belegen dasselbe Bit wie FX_SIZE2_LINEAR und
    // gelten jeweils nur fuer genau einen Primitivtyp - Raven schreibt im
    // Quelltext selbst dazu, dass ein Cylinder damit "evilness" ergibt.
    kFlagPaperPhysics = 0x00010000,    // nur Emitter
    kFlagLocalizedFlash = 0x00010000,  // nur Flash
    kFlagPlayerView = 0x00010000,      // nur Effekte in der Spieleransicht
};

// "spawnFlags" - steuern nur das Erzeugen, gehen nicht an die Primitive.
enum : uint32_t {
    kSpawnOrgOnSphere = 0x00001,
    kSpawnAxisFromSphere = 0x00002,
    kSpawnOrgOnCylinder = 0x00004,
    kSpawnOrg2FromTrace = 0x00010,
    kSpawnTraceImpactFx = 0x00020,
    kSpawnOrg2IsOffset = 0x00040,
    kSpawnCheapOrgCalc = 0x00100,
    kSpawnCheapOrg2Calc = 0x00200,
    kSpawnVelIsAbsolute = 0x00400,
    kSpawnAccelIsAbsolute = 0x00800,
    kSpawnRandRotAroundFwd = 0x01000,
    kSpawnEvenDistribution = 0x02000,
    kSpawnRgbComponentInterp = 0x04000,
    kSpawnAffectedByWind = 0x10000,
    kSpawnSoundLessAttenuation = 0x20000,
};

struct FlagName {
    const char* name;
    uint32_t bits;
    Dialect dialect;      // wo dieses Wort im Parser steht
    const char* note;     // Warnung zu geteilten Bits, sonst leer
};
const std::vector<FlagName>& flagNames();       // fuer "flags"
const std::vector<FlagName>& spawnFlagNames();  // fuer "spawnFlags"

// Sucht ein Flag ueber alle Dialekte hinweg.
const FlagName* findFlag(const std::vector<FlagName>& table, std::string_view name);

// ---------------------------------------------------------------------------
// Eine Primitive

// materialImpact, nur Multiplayer. Der Parser kennt genau einen Wert.
enum class MaterialImpact {
    None,
    ShellSound,
};

struct Primitive {
    PrimitiveType type = PrimitiveType::Particle;
    std::string name;

    MaterialImpact materialImpact = MaterialImpact::None;
    bool materialImpactSet = false;

    // Erzeugung
    Range count;
    Range life;
    Range delay;
    int cullRange = 0;
    bool cullRangeSet = false;

    uint32_t flags = 0;
    uint32_t spawnFlags = 0;

    // Lage
    Vec3Range origin;
    Vec3Range origin2;
    Vec3Range min;   // Begrenzungsbox
    Vec3Range max;
    Range radius;
    Range height;

    // Bewegung
    Vec3Range velocity;
    Vec3Range acceleration;
    Vec3Range angles;
    Vec3Range anglesDelta;
    Range gravity;
    Range density;
    Range variance;
    Range windModifier;
    Range rotation;
    Range rotationDelta;
    Range elasticity;  // in der Datei "bounce" oder "intensity"

    // Aussehen
    ColorChannel rgb;
    Channel alpha;
    Channel size;
    Channel size2;
    Channel length;

    // Verweise
    std::vector<std::string> shaders;
    std::vector<std::string> models;
    std::vector<std::string> sounds;
    std::vector<std::string> impactFx;
    std::vector<std::string> deathFx;
    std::vector<std::string> emitFx;
    std::vector<std::string> playFx;

    // Welche Schreibweise stand in der Datei? true = Plural.
    bool shadersPlural = true;
    bool modelsPlural = true;
    bool soundsPlural = true;
    bool elasticityAsIntensity = false;  // "intensity" statt "bounce"
    bool sizeAsWidth = false;            // "width" statt "size"
    bool size2AsWidth = false;
    bool lengthAsHeight = false;         // "height" statt "length" im Unterblock
    bool velocityShort = false;          // "vel" statt "velocity"
    bool accelShort = false;             // "accel" statt "acceleration"
    bool anglesSingular = false;         // "angle" statt "angles"
    bool flagsSingular = false;
    bool spawnFlagsSingular = false;
};

// ---------------------------------------------------------------------------
// Ein Effekt = eine .efx-Datei

// Ein neues Segment mit brauchbaren Anfangswerten.
//
// Warum nicht einfach `Primitive{}`: ein frisch angelegtes CameraShake hatte
// keine Staerke und keine Reichweite und tat deshalb nichts - man legt es an,
// drueckt Abspielen und nichts passiert. Das sieht aus wie ein kaputtes
// Programm, ist aber nur eine leere Zahl.
//
// Die Werte sind bewusst mittig: sichtbar, aber nicht uebertrieben.
Primitive freshPrimitive(PrimitiveType type);

struct Effect {
    int repeatDelay = 0;
    bool repeatDelaySet = false;
    std::vector<Primitive> primitives;
};

}  // namespace bhed::efx
