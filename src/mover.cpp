// mover.cpp - siehe bhed/mover.h
//
// Die Zeilenangaben beziehen sich auf OpenJK, code/game/.
#include "bhed/mover.h"
#include "bhed/gegenstaende.h"   // rc604: spawntBeiSchwierigkeit
#include "bhed/navlauf.h"        // rc604: kNavKartenSpawnMs

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace bhed {

namespace {

constexpr double kTakt = 50.0;   // FRAMETIME, eine Runde der Spiellogik
constexpr float kGrad = 3.14159265358979F / 180.0F;

bool gleichOhneFall(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

float zahl(const MapEntity& e, const char* key, float vorgabe, bool* gesetzt = nullptr) {
    const std::string* v = e.find(key);
    if (gesetzt != nullptr) { *gesetzt = (v != nullptr && !v->empty()); }
    if (v == nullptr || v->empty()) {
        return vorgabe;
    }
    return static_cast<float>(std::atof(v->c_str()));
}

int ganz(const MapEntity& e, const char* key) {
    const std::string* v = e.find(key);
    return (v == nullptr) ? 0 : std::atoi(v->c_str());
}

std::string text(const MapEntity& e, const char* key) {
    const std::string* v = e.find(key);
    return (v == nullptr) ? std::string() : *v;
}

void vektor(const std::string& s, float out[3]) {
    std::istringstream is(s);
    is >> out[0] >> out[1] >> out[2];
}

// "angles" (drei Werte) oder "angle" (nur Gier, F_ANGLEHACK) - der spaetere
// Schluessel gewinnt, wie in G_ParseField.
void winkelAus(const MapEntity& e, float out[3]) {
    out[0] = out[1] = out[2] = 0.0F;
    for (const auto& [k, v] : e.keys) {
        if (gleichOhneFall(k, "angles")) {
            vektor(v, out);
        } else if (gleichOhneFall(k, "angle")) {
            out[0] = 0.0F;
            out[1] = static_cast<float>(std::atof(v.c_str()));
            out[2] = 0.0F;
        }
    }
}

// G_SetMovedir (g_utils.cpp:700): -1 hoch, -2 runter, sonst AngleVectors.
void bewegungsRichtung(const float ang[3], float dir[3]) {
    if (ang[0] == 0.0F && ang[1] == -1.0F && ang[2] == 0.0F) {
        dir[0] = 0.0F; dir[1] = 0.0F; dir[2] = 1.0F;
        return;
    }
    if (ang[0] == 0.0F && ang[1] == -2.0F && ang[2] == 0.0F) {
        dir[0] = 0.0F; dir[1] = 0.0F; dir[2] = -1.0F;
        return;
    }
    const float cp = std::cos(ang[0] * kGrad);
    const float sp = std::sin(ang[0] * kGrad);
    const float cy = std::cos(ang[1] * kGrad);
    const float sy = std::sin(ang[1] * kGrad);
    dir[0] = cp * cy;
    dir[1] = cp * sy;
    dir[2] = -sp;
}

int modellVon(const MapEntity& e) {
    const std::string m = text(e, "model");
    if (m.size() < 2 || m[0] != '*') {
        return -1;
    }
    return std::atoi(m.c_str() + 1);
}

// Eine Tuer (bzw. ein Team davon).
struct Tuer {
    std::size_t mover = 0;          // Index in mover_
    float pos1[3]{};
    float pos2[3]{};
    double dauerMs = 1.0;
    bool linear = false;
};

enum class Lage { Pos1, AufDemWeg, Pos2, ZurueckDemWeg };

struct Team {
    std::vector<Tuer> tueren;       // [0] ist der Meister
    std::string targetname;
    int flags = 0;
    double waitMs = 2000.0;
    double delayMs = 0.0;
    bool hatFeld = false;
    float feldMin[3]{};
    float feldMax[3]{};
    std::string target, target2, opentarget, closetarget;
    // Laufzeit
    Lage lage = Lage::Pos1;
    double bahnAb = 0.0;
    double bahnDauer = 1.0;
    double zurueckUm = -1.0;       // ReturnToPos1-Termin
    double losUm = -1.0;           // verzoegertes Use_BinaryMover_Go
    bool fertig = false;           // wait < 0 und offen: nicht mehr benutzbar
};

constexpr int kToggle = 8;
constexpr int kLocked = 16;
constexpr int kPlayerUse = 64;
constexpr int kForceActivate = 2;

// --- Zerbrechen ------------------------------------------------------------

// material_t, g_shared.h:62 ff. - die Nummer ist der Kartenwert ("material").
enum Material : int {
    kMatMetal = 0, kMatGlass, kMatElectrical, kMatElecMetal, kMatDrkStone, kMatLtStone,
    kMatGlassMetal, kMatMetal2, kMatNone, kMatGreyStone, kMatMetal3, kMatCrate1,
    kMatGrate1, kMatRope, kMatCrate2, kMatWhiteMetal
};

constexpr int kNoExplosion = 2048;    // func_breakable/misc_model_breakable NO_EXPLOSION
constexpr int kUseNotBreak = 64;      // USE_NOT_BREAK
constexpr int kModellStartOff = 4096; // misc_model_breakable: Start off
constexpr float kSchwere = 800.0F;    // g_gravity, g_main.cpp:731 - EvaluateTrajectory TR_GRAVITY

// Der Zufall der Engine, aber wiederholbar: ein xorshift je Bruch. Q_flrand
// und rand() liefern im Spiel bei jedem Durchlauf anderes; eine Vorschau,
// die beim Zurueckspulen anders zerbricht, waere Unruhe statt Treue.
class Wuerfel {
public:
    explicit Wuerfel(std::uint32_t saat) : s_(saat == 0U ? 0x9E3779B9U : saat) {}
    std::uint32_t roh() {
        s_ ^= s_ << 13U;
        s_ ^= s_ >> 17U;
        s_ ^= s_ << 5U;
        return s_;
    }
    // Q_flrand(0, 1)
    float f01() { return static_cast<float>(roh() >> 8U) * (1.0F / 16777216.0F); }
    // Q_flrand(a, b)
    float bereich(float a, float b) { return a + (b - a) * f01(); }
    // Q_irand(lo, hi), beide eingeschlossen
    int ganz(int lo, int hi) {
        const int n = hi - lo + 1;
        return lo + std::min(n - 1, static_cast<int>(f01() * static_cast<float>(n)));
    }
    bool bit() { return (roh() & 1U) != 0U; }

private:
    std::uint32_t s_;
};

// COM_DefaultExtension: ".wav" nur, wenn der Name noch keine Endung hat.
std::string mitEndung(std::string s, const char* endung) {
    const std::size_t punkt = s.find_last_of('.');
    const std::size_t strich = s.find_last_of("/\\");
    if (punkt == std::string::npos || (strich != std::string::npos && punkt < strich)) {
        s += endung;
    }
    return s;
}

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

// G_NewString (g_spawn.cpp): "\n" im Kartentext wird ein Zeilenumbruch, ein
// Rueckstrich vor etwas anderem bleibt als Rueckstrich stehen und schluckt
// das Zeichen dahinter.
std::string kartenText(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            out += (s[i] == 'n') ? '\n' : '\\';
        } else {
            out += s[i];
        }
    }
    return out;
}

void normiere(float v[3]) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0.0F) {
        for (int k = 0; k < 3; ++k) { v[k] /= l; }
    }
}

// Das Modell eines Bruchstuecks je Material (CG_Chunks, cg_effects.cpp:410).
// Achtung, in cg_main.cpp:2276 VERTAUSCHT: CHUNK_METAL2 laedt metal1_N,
// CHUNK_METAL1 laedt metal2_N ("//_ /switched\ _").
std::string stueckModell(int material, Wuerfel& w) {
    const char* name = nullptr;
    switch (material) {
        case kMatMetal2: name = "metal/metal1"; break;       // CHUNK_METAL2
        case kMatGreyStone: name = "rock/rock1"; break;      // CHUNK_ROCK1
        case kMatLtStone: name = "rock/rock2"; break;        // CHUNK_ROCK2
        case kMatDrkStone: name = "rock/rock3"; break;       // CHUNK_ROCK3
        case kMatWhiteMetal: name = "metal/wmetal1"; break;  // CHUNK_WHITE_METAL
        case kMatCrate1: name = "crate/crate1"; break;
        case kMatCrate2: name = "crate/crate2"; break;
        case kMatElecMetal:
        case kMatGlassMetal:
        case kMatMetal: name = "metal/metal2"; break;        // CHUNK_METAL1
        case kMatMetal3: name = w.bit() ? "metal/metal2" : "metal/metal1"; break;
        default: return {};
    }
    char puffer[64];
    std::snprintf(puffer, sizeof(puffer), "models/chunks/%s_%d.md3", name, w.ganz(1, 4));
    return puffer;
}

// vectoangles (q_math.cpp)
void vektorWinkel(const float v[3], float out[3]) {
    float gier = 0.0F;
    float nick = 0.0F;
    if (v[1] == 0.0F && v[0] == 0.0F) {
        nick = (v[2] > 0.0F) ? 90.0F : 270.0F;
    } else {
        if (v[0] != 0.0F) {
            gier = std::atan2(v[1], v[0]) / kGrad;
        } else {
            gier = (v[1] > 0.0F) ? 90.0F : 270.0F;
        }
        if (gier < 0.0F) { gier += 360.0F; }
        const float vorn = std::sqrt(v[0] * v[0] + v[1] * v[1]);
        nick = std::atan2(v[2], vorn) / kGrad;
        if (nick < 0.0F) { nick += 360.0F; }
    }
    out[0] = -nick;
    out[1] = gier;
    out[2] = 0.0F;
}

// Wie viele Bruchstuecke bei "radius": anzahl * radius, aber hoechstens 512
// - mehr lokale Entities haelt das Spiel nicht (MAX_LOCAL_ENTITIES), und
// "radius" "1e10" machte die Umwandlung undefiniert (Code-Pruefung rc592).
int stueckZahl(int anzahl, float radius) {
    const float n = static_cast<float>(anzahl) * radius;
    if (!std::isfinite(n) || n < 0.0F) { return anzahl; }
    return static_cast<int>(std::min(n, 512.0F));
}

// AngleSubtract (q_math.cpp): auf -180..180
float winkelDifferenz(float a, float b) {
    float d = a - b;
    // Riesige oder unendliche Winkel aus der Datei: -360 aenderte nichts
    // mehr, die Schleife lief endlos (Code-Pruefung rc592). Fuer normale
    // Winkel greift das nicht.
    if (!std::isfinite(d)) { return 0.0F; }
    if (std::fabs(d) > 720.0F) { d = std::fmod(d, 360.0F); }
    while (d > 180.0F) { d -= 360.0F; }
    while (d < -180.0F) { d += 360.0F; }
    return d;
}

// Die Flugbahn eines Bruchstuecks vorausrechnen (CG_AddFragment,
// cg_localents.cpp:219): in jedem Bild des cgame ein Strahl vom alten zum
// neuen Ort. Trifft er, wird die Geschwindigkeit an der Flaeche gespiegelt
// und mit bounceFactor gedaempft (CG_ReflectVelocity); zeigt die Flaeche
// nach oben und bleibt weniger als 40 Einheiten je Sekunde nach oben
// uebrig, bleibt es liegen (TR_STATIONARY).
//
// Das cgame rechnet je gezeichnetem Bild - wie weit ein Stueck springt,
// haengt im Spiel also ein wenig an der Bildrate. Hier 20 ms (50 Bilder je
// Sekunde).
void fliege(Truemmer& tr, float abprall, const BspGeometry& geo) {
    constexpr double kBild = 20.0;
    float jetzt[3] = {tr.flug.front().ort[0], tr.flug.front().ort[1], tr.flug.front().ort[2]};
    double t = tr.abMs;
    int grenze = 400;
    while (t < tr.bisMs && grenze-- > 0) {
        const double t2 = t + kBild;
        const Truemmer::Flug s = tr.flug.back();
        const auto dt = static_cast<float>((t2 - s.abMs) * 0.001);
        float neu[3];
        for (int k = 0; k < 3; ++k) { neu[k] = s.ort[k] + s.vel[k] * dt; }
        neu[2] -= 0.5F * kSchwere * dt * dt;
        const TraceTreffer h = traceRay(geo, jetzt, neu);
        if (!h.hit) {
            for (int k = 0; k < 3; ++k) { jetzt[k] = neu[k]; }
            t = t2;
            continue;
        }
        // Geschwindigkeit im Augenblick des Aufpralls (EvaluateTrajectoryDelta)
        const double tt = t + kBild * static_cast<double>(h.fraction);
        const auto dtt = static_cast<float>((tt - s.abMs) * 0.001);
        float v[3] = {s.vel[0], s.vel[1], s.vel[2] - kSchwere * dtt};
        const float dot = v[0] * h.normal[0] + v[1] * h.normal[1] + v[2] * h.normal[2];
        // Das neue Stueck beginnt im AUFPRALL, nicht erst im naechsten Bild
        // (die Engine setzt trTime = cg.time und laesst es bis dahin am
        // alten Ort stehen). Sonst liefe die alte Parabel zwischen Aufprall
        // und Bild durch den Boden - wer zwischen zwei Bildern abfragt,
        // saehe das Stueck kurz darunter.
        Truemmer::Flug f;
        f.abMs = tt;
        for (int k = 0; k < 3; ++k) {
            f.vel[k] = (v[k] - 2.0F * dot * h.normal[k]) * abprall;
            // endpos der Engine liegt SURFACE_CLIP_EPSILON vor der Flaeche
            f.ort[k] = h.point[k] + h.normal[k] * 0.25F;
        }
        if (h.normal[2] > 0.0F && f.vel[2] < 40.0F) {
            f.liegt = true;
            tr.liegtAbMs = tt;
            tr.flug.push_back(f);
            return;
        }
        tr.flug.push_back(f);
        for (int k = 0; k < 3; ++k) { jetzt[k] = f.ort[k]; }
        t = tt;
    }
}

// Ein zerbrechliches Ding der Karte (func_breakable, misc_model_breakable).
struct Bruchstelle {
    std::size_t entity = 0;
    bool istModell = false;       // misc_model_breakable
    int mover = -1;               // func_breakable: Index in mover_
    int material = 0;
    float radius = 1.0F;          // "radius": Faktor auf die Stueckzahl
    int flags = 0;
    int delaySek = 0;             // "delay" ist F_INT (g_spawn.cpp:401): ganze Sekunden
    bool splash = false;          // splashDamage > 0 && splashRadius > 0
    bool hatGesundheit = false;   // misc: health -> max_health, takedamage
    bool geworfen = false;        // misc: gravity + throwtarget (target4)
    bool tie = false;             // misc: tie_fighter/tie_bomber.md3 (Sonderfall)
    std::string noise;            // func_breakable "noise"
    std::string target, target3, targetname;
    float absMin[3]{};
    float absMax[3]{};
    float ursprung[3]{};          // currentOrigin
    std::string modell, d1, u1, c1;
    // Laufzeit
    double bruchUm = -1.0;        // funcBBrushDieGo nach "delay"
    int bruchTiefe = -1;
    std::string bruchAusloeser;
    bool zerbrochen = false;
    bool weg = false;             // G_FreeEntity
    bool erschienen = false;      // misc: count (Start off aufgehoben)
    bool zeigtU1 = false;
    int brueche = 0;
};

// Ein func_train (g_mover.cpp:2163).
struct Zug {
    std::size_t mover = 0;
    std::size_t entity = 0;
    float speed = 100.0F;
    bool linear = false;          // "linear" -> alt_fire
    bool tie = false;             // spawnflags 2048
    int naechste = -1;            // nextTrain: Entitynummer
    bool faehrt = false;          // TR_*_STOP laeuft
    double trTime = 0.0;
    double dauer = 1.0;
    double losUm = -1.0;          // Think_BeginMoving
    float pos1[3]{};
    float pos2[3]{};
    float drehDelta[3]{};         // s.apos.trDelta, Grad je Sekunde - bleibt stehen
    bool unsichtbar = false;      // EF_NODRAW
};

// Ein target_speaker (g_target.cpp:226).
struct Sprecher {
    std::size_t entity = 0;
    int flags = 0;
    bool stumm = false;           // mit soundSet: keine use-Funktion
    bool tot = false;             // wait < 0 und schon benutzt: useF_NULL
    double waitMs = 0.0;
    double sperreBis = -1.0;      // painDebounceTime
    std::string noise;
    int sounds = 0;
    std::string gruppe;           // "soundGroup"
    float ort[3]{};
};

// Ein light mit targetname (SP_light, g_misc.cpp:165).
struct Licht {
    std::size_t entity = 0;
    int stil = 0;
    int stilAn = 0;               // switch_style
    int stilAus = 0;              // style_off
    bool an = true;
};

}  // namespace

TruemmerStand truemmerAt(const Truemmer& t, double ms) {
    TruemmerStand s;
    if (t.flug.empty() || ms < t.abMs || ms >= t.bisMs) {
        return s;
    }
    s.sichtbar = true;
    s.radius = t.radius;
    const double rest = t.bisMs - ms;
    s.deckkraft = (rest < 1000.0) ? static_cast<float>(rest / 1000.0) : 1.0F;   // FRAG_FADE_TIME
    const Truemmer::Flug* f = &t.flug.front();
    for (const Truemmer::Flug& g : t.flug) {
        if (g.abMs > ms) { break; }
        f = &g;
    }
    if (f->liegt) {
        for (int k = 0; k < 3; ++k) { s.ort[k] = f->ort[k]; }
    } else {
        const auto dt = static_cast<float>((ms - f->abMs) * 0.001);
        for (int k = 0; k < 3; ++k) { s.ort[k] = f->ort[k] + f->vel[k] * dt; }
        s.ort[2] -= 0.5F * kSchwere * dt * dt;
    }
    // LEF_TUMBLE: die Winkel laufen linear, bis das Stueck liegt.
    const double bis = (t.liegtAbMs >= 0.0 && ms > t.liegtAbMs) ? t.liegtAbMs : ms;
    const double sek = (bis - t.abMs) * 0.001;
    for (int k = 0; k < 3; ++k) {
        s.winkel[k] = static_cast<float>(std::fmod(t.winkel[k] + t.drehung[k] * sek, 360.0));
    }
    return s;
}

float bildschirmtextDeckkraft(const Bildschirmtext& b, double ms) {
    if (ms < b.ms || ms >= b.bisMs) {
        return 0.0F;
    }
    // CG_FadeColor(centerPrintTime, 1000 * 3): die letzten 200 ms blendet es aus.
    const double rest = 3000.0 - (ms - b.ms);
    return (rest < 200.0) ? static_cast<float>(rest / 200.0) : 1.0F;
}

float moverAnteil(double seitMs, double dauerMs, bool linear) {
    if (dauerMs <= 0.0 || seitMs >= dauerMs) {
        return 1.0F;
    }
    if (seitMs <= 0.0) {
        return 0.0F;
    }
    const double f = seitMs / dauerMs;
    if (linear) {
        return static_cast<float>(f);
    }
    // cos(90 - 90*f) = sin(90*f)
    return static_cast<float>(std::sin(f * 3.14159265358979 * 0.5));
}

void MoverSim::baue(const MapData& karte, const BspGeometry& geo,
                    std::vector<MoverUse> uses, const FigurImKasten& figur,
                    double endeMs) {
    // rc604: G_SpawnGEntityFromSpawnVars gibt jede Entity mit "notsingle"
    // oder dem Bit der Schwierigkeit frei, bevor ihre Spawnfunktion laeuft
    // (code/game/g_spawn.cpp:1064-1075, 1096-1101 OpenJK; :1260-1265 MD):
    // keine Tuer, kein Ausloesefeld (Think_SpawnNewDoorTrigger), kein
    // Lautsprecher, kein Ziel fuer G_Find. Der Platz in der Liste bleibt
    // (die Nummern der Entities gelten weiter), aber leer - worldspawn nicht,
    // die laeuft ueber SP_worldspawn (G_SpawnEntitiesFromString).
    MapData gefiltert;
    const MapData* quelle = &karte;
    if (spielstufe >= 0) {
        gefiltert = karte;
        for (MapEntity& e : gefiltert.entities) {
            if (e.classname != "worldspawn" && !spawntBeiSchwierigkeit(e, spielstufe)) {
                e = MapEntity{};
            }
        }
        quelle = &gefiltert;
    }
    const MapData& map = *quelle;
    mover_.clear();
    tuerWechsel_.clear();   // rc601 (bewegung, teil 3)
    fx_.clear();
    strahl_.clear();   // rc604
    entityUse_.clear();   // rc604: Fallen, Ionenkanonen, Wachgeschuetze
    auf_.clear();
    bruch_.clear();
    bruchFx_.clear();
    truemmer_.clear();
    bruchKlang_.clear();
    lautsprecher_.clear();
    texte_.clear();
    licht_.clear();
    modellWechsel_.clear();
    nachModell_.assign(geo.models.size(), -1);

    auto neuerMover = [&](std::size_t ei, int modell, const MapEntity& e) -> std::size_t {
        Mover m;
        m.modell = modell;
        m.entity = ei;
        m.klasse = e.classname;
        m.soundSet = text(e, "soundSet");
        if (!e.origin.empty()) {
            vektor(e.origin, m.origin0);
        }
        if (modell >= 0 && static_cast<std::size_t>(modell) < geo.models.size()) {
            const auto& g = geo.models[static_cast<std::size_t>(modell)];
            for (int k = 0; k < 3; ++k) { m.mitte[k] = 0.5F * (g.mins[k] + g.maxs[k]); }
        }
        mover_.push_back(m);
        const std::size_t idx = mover_.size() - 1;
        if (modell >= 0 && static_cast<std::size_t>(modell) < nachModell_.size()) {
            nachModell_[static_cast<std::size_t>(modell)] = static_cast<int>(idx);
        }
        return idx;
    };

    // --- Tueren und Teams --------------------------------------------------
    std::vector<Team> teams;
    std::map<std::string, std::size_t> teamNachName;   // "team"-Schluessel
    std::vector<int> teamVonEntity(map.entities.size(), -1);
    // --- fx_runner, die auf ein use warten --------------------------------
    struct Runner { std::size_t entity; bool oneShot; bool an = false; std::size_t offen = 0; };
    std::vector<Runner> runner;
    // rc604: fx_target_beam (code/game/g_fx.cpp:994-1167 OpenJK)
    struct Strahl {
        std::size_t entity;
        bool oneShot;
        bool an = false;
        std::size_t offen = 0;
        double ohneUseAb = -1.0;   // wait < 0: ab dem ersten fire_start kein use mehr
    };
    std::vector<Strahl> strahlen;
    // Sichtbarkeit, Drehung, Pendeln: Mover mit use-Umschalter
    std::vector<int> moverVonEntity(map.entities.size(), -1);
    // Zerbrechliches, Zuege, Lautsprecher, Lichter: Entity -> Index
    std::vector<Bruchstelle> bruchstellen;
    std::vector<Zug> zuege;
    std::vector<Sprecher> sprecher;
    std::vector<Licht> lichter;
    std::vector<int> bruchVonEntity(map.entities.size(), -1);
    std::vector<int> zugVonEntity(map.entities.size(), -1);
    std::vector<int> sprecherVonEntity(map.entities.size(), -1);
    std::vector<int> lichtVonEntity(map.entities.size(), -1);
    std::map<std::string, std::size_t> schleifeOffen;   // Traeger -> Index in lautsprecher_

    for (std::size_t ei = 0; ei < map.entities.size(); ++ei) {
        const MapEntity& e = map.entities[ei];
        const int modell = modellVon(e);
        if (e.classname == "fx_runner") {
            const int sf = ganz(e, "spawnflags");
            if ((sf & 3) != 0) {
                runner.push_back(Runner{ei, (sf & 2) != 0});
            }
            continue;
        }
        // rc604: fx_target_beam_link (g_fx.cpp:1124-1167 OpenJK) - ohne target
        // (G_Find findet nichts) gibt G_FreeEntity den Strahl frei. STARTOFF (1)
        // und ONE_SHOT (8) warten auf ein use, sonst laeuft think ab dem Laden.
        // Die use-Funktion setzt link immer; fx_target_beam_set_debounce
        // (:994-1008) nimmt sie bei wait < 0 im ersten fire_start wieder weg.
        if (e.classname == "fx_target_beam") {
            const std::string ziel = text(e, "target");
            bool zielDa = false;
            for (const MapEntity& z : map.entities) {
                if (!ziel.empty() && z.targetname == ziel) {
                    zielDa = true;
                    break;
                }
            }
            if (!zielDa) { continue; }
            const int sf = ganz(e, "spawnflags");
            Strahl st{ei, (sf & 8) != 0};
            const bool warteNie = zahl(e, "wait", 0.0F) < 0.0F;
            if ((sf & (1 | 8)) == 0) {
                strahl_.push_back(FxRunnerStart{ei, -1.0, false, -1.0});
                st.an = true;
                st.offen = strahl_.size() - 1;
                if (warteNie) { st.ohneUseAb = 0.0; }   // feuert schon beim Laden
            }
            strahlen.push_back(st);
            continue;
        }
        if (e.classname == "misc_model_breakable") {
            // SP_misc_model_breakable, g_breakable.cpp:1182
            Bruchstelle b;
            b.entity = ei;
            b.istModell = true;
            b.flags = ganz(e, "spawnflags");
            b.material = e.find("material") != nullptr ? ganz(e, "material") : kMatNone;   // Vorgabe 8
            b.radius = zahl(e, "radius", 1.0F);
            b.hatGesundheit = ganz(e, "health") != 0;
            b.splash = ganz(e, "splashDamage") > 0 && ganz(e, "splashRadius") > 0;
            b.target = text(e, "target");
            b.target3 = text(e, "target3");
            b.targetname = text(e, "targetname");
            b.geworfen = zahl(e, "gravity", 0.0F) != 0.0F && !text(e, "throwtarget").empty();
            b.modell = klein(text(e, "model"));
            b.tie = b.modell == "models/map_objects/ships/tie_fighter.md3" ||
                    b.modell == "models/map_objects/ships/tie_bomber.md3";
            if (b.modell.size() > 4) {
                const std::string stamm = b.modell.substr(0, b.modell.size() - 4);   // ".md3" ab
                // Das Schadensmodell nur mit health und ohne "no dmodel" (8),
                // das Stueckmodell nur mit health (takedamage).
                if (b.hatGesundheit && (b.flags & 8) == 0) { b.d1 = stamm + "_d1.md3"; }
                if (b.hatGesundheit) { b.c1 = stamm + "_c1.md3"; }
                if ((b.flags & 32) != 0) { b.u1 = stamm + "_u1.md3"; }
            }
            // Der Kasten: "mins"/"maxs", sonst -16..16, mit der Groesse
            // gestreckt; die Hoehe des Ursprungs rueckt dabei nach.
            float mn[3]{};
            float mx[3]{};
            if (const std::string* v = e.find("mins")) { vektor(*v, mn); }
            if (const std::string* v = e.find("maxs")) { vektor(*v, mx); }
            if (mn[0] == 0.0F && mn[1] == 0.0F && mn[2] == 0.0F) { mn[0] = mn[1] = mn[2] = -16.0F; }
            if (mx[0] == 0.0F && mx[1] == 0.0F && mx[2] == 0.0F) { mx[0] = mx[1] = mx[2] = 16.0F; }
            if (b.modell == "models/map_objects/ships/tie_bomber.md3") {
                mn[0] = mn[1] = mn[2] = -80.0F;
                mx[0] = mx[1] = mx[2] = 80.0F;
            }
            if (!e.origin.empty()) { vektor(e.origin, b.ursprung); }
            float skala[3] = {0.0F, 0.0F, 0.0F};
            bool mitSkala = false;
            if (const std::string* v = e.find("modelscale_vec")) {
                vektor(*v, skala);
                mitSkala = true;   // G_SpawnVector meldet "gesetzt", egal welcher Wert
            } else {
                const float s = zahl(e, "modelscale", 0.0F);
                if (s != 0.0F) {
                    skala[0] = skala[1] = skala[2] = s;
                    mitSkala = true;
                }
            }
            if (mitSkala) {
                for (int k = 0; k < 3; ++k) {
                    mx[k] *= skala[k];
                    const float alt = mn[k];
                    mn[k] *= skala[k];
                    if (k == 2) { b.ursprung[2] += alt - mn[2]; }
                }
            }
            // SV_LinkEntity: absmin/absmax um eine Einheit weiter
            for (int k = 0; k < 3; ++k) {
                b.absMin[k] = b.ursprung[k] + mn[k] - 1.0F;
                b.absMax[k] = b.ursprung[k] + mx[k] + 1.0F;
            }
            const bool startAus = (b.flags & kModellStartOff) != 0;
            modellWechsel_[ei].push_back(ModellWechsel{-1.0e30, startAus ? std::string{} : b.modell, false});
            bruchVonEntity[ei] = static_cast<int>(bruchstellen.size());
            bruchstellen.push_back(std::move(b));
            continue;
        }
        if (e.classname == "target_speaker") {
            Sprecher s;
            s.entity = ei;
            s.flags = ganz(e, "spawnflags");
            s.stumm = !text(e, "soundSet").empty();   // SP_target_speaker: VALIDSTRING(soundSet) -> return
            s.waitMs = static_cast<double>(zahl(e, "wait", 0.0F)) * 1000.0;
            s.noise = text(e, "noise");
            if (!s.noise.empty()) { s.noise = mitEndung(s.noise, ".wav"); }
            s.sounds = ganz(e, "sounds");
            s.gruppe = text(e, "soundGroup");
            if (!e.origin.empty()) { vektor(e.origin, s.ort); }
            // looped-on: die Schleife laeuft schon, wenn die Karte beginnt.
            if (!s.stumm && (s.flags & 1) != 0 && !s.noise.empty()) {
                KartenKlang k;
                k.entity = ei;
                k.datei = s.noise;
                k.ms = 0.0;
                k.schleife = true;
                for (int q = 0; q < 3; ++q) { k.ort[q] = s.ort[q]; }
                lautsprecher_.push_back(k);
                schleifeOffen["#" + std::to_string(ei)] = lautsprecher_.size() - 1;
            }
            sprecherVonEntity[ei] = static_cast<int>(sprecher.size());
            sprecher.push_back(std::move(s));
            continue;
        }
        if (e.classname == "light" && !text(e, "targetname").empty()) {
            // SP_light: ohne targetname gibt es zur Laufzeit nichts (G_FreeEntity).
            Licht l;
            l.entity = ei;
            l.stil = ganz(e, "style");
            l.stilAn = ganz(e, "switch_style");
            l.stilAus = ganz(e, "style_off");
            l.an = (ganz(e, "spawnflags") & 4) == 0;   // START_OFF
            licht_.push_back(LichtSchaltung{ei, l.stil, 0.0, l.an, l.an ? l.stilAn : l.stilAus});
            lichtVonEntity[ei] = static_cast<int>(lichter.size());
            lichter.push_back(l);
            continue;
        }
        if (modell <= 0 || static_cast<std::size_t>(modell) >= geo.models.size()) {
            continue;
        }
        const auto& sm = geo.models[static_cast<std::size_t>(modell)];
        if (e.classname == "func_breakable") {
            // SP_func_breakable, g_breakable.cpp:389; InitBBrush: steht auf
            // origin, ohne Winkel (TR_STATIONARY auf pos1).
            const std::size_t mi = neuerMover(ei, modell, e);
            Bruchstelle b;
            b.entity = ei;
            b.mover = static_cast<int>(mi);
            b.flags = ganz(e, "spawnflags");
            b.material = ganz(e, "material");   // Vorgabe 0 = MAT_METAL
            b.radius = zahl(e, "radius", 1.0F);
            b.delaySek = ganz(e, "delay");
            b.splash = ganz(e, "splashDamage") > 0 && ganz(e, "splashRadius") > 0;
            b.target = text(e, "target");
            b.targetname = text(e, "targetname");
            b.noise = text(e, "noise");
            if (!b.noise.empty()) { b.noise = mitEndung(b.noise, ".wav"); }
            for (int k = 0; k < 3; ++k) {
                b.ursprung[k] = mover_[mi].origin0[k];
                b.absMin[k] = b.ursprung[k] + sm.mins[k] - 1.0F;
                b.absMax[k] = b.ursprung[k] + sm.maxs[k] + 1.0F;
            }
            bruchVonEntity[ei] = static_cast<int>(bruchstellen.size());
            bruchstellen.push_back(std::move(b));
            continue;
        }
        if (e.classname == "func_train") {
            // SP_func_train, g_mover.cpp:2163: Winkel geloescht, speed 100,
            // ohne target gibt es ihn nicht.
            if (text(e, "target").empty()) {
                continue;
            }
            const std::size_t mi = neuerMover(ei, modell, e);
            Zug z;
            z.mover = mi;
            z.entity = ei;
            z.speed = zahl(e, "speed", 0.0F);
            if (z.speed == 0.0F) { z.speed = 100.0F; }
            z.linear = zahl(e, "linear", 0.0F) != 0.0F;
            z.tie = (ganz(e, "spawnflags") & 2048) != 0;
            zugVonEntity[ei] = static_cast<int>(zuege.size());
            zuege.push_back(z);
            continue;
        }
        if (e.classname == "func_door") {
            // SP_func_door, g_mover.cpp:1446
            const std::size_t mi = neuerMover(ei, modell, e);
            Mover& m = mover_[mi];
            float ang[3];
            winkelAus(e, ang);
            float dir[3];
            bewegungsRichtung(ang, dir);   // die Winkel selbst werden geloescht
            float speed = zahl(e, "speed", 0.0F);
            if (speed == 0.0F) { speed = 400.0F; }
            const float lip = zahl(e, "lip", 8.0F);
            const float groesse[3] = {sm.maxs[0] - sm.mins[0], sm.maxs[1] - sm.mins[1],
                                      sm.maxs[2] - sm.mins[2]};
            const float weite = std::fabs(dir[0]) * groesse[0] + std::fabs(dir[1]) * groesse[1] +
                                std::fabs(dir[2]) * groesse[2] - lip;
            Tuer t;
            t.mover = mi;
            for (int k = 0; k < 3; ++k) {
                t.pos1[k] = m.origin0[k];
                t.pos2[k] = m.origin0[k] + dir[k] * weite;
            }
            const int sf = ganz(e, "spawnflags");
            if ((sf & 1) != 0) {   // START_OPEN: vertauscht, steht offen
                for (int k = 0; k < 3; ++k) { std::swap(t.pos1[k], t.pos2[k]); }
            }
            const float dx = t.pos2[0] - t.pos1[0];
            const float dy = t.pos2[1] - t.pos1[1];
            const float dz = t.pos2[2] - t.pos1[2];
            t.dauerMs = std::max(1.0, std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz)) *
                                          1000.0 / speed);   // InitMoverTrData
            t.linear = zahl(e, "linear", 0.0F) != 0.0F;      // alt_fire
            for (int k = 0; k < 3; ++k) { m.origin0[k] = t.pos1[k]; }

            // Team (G_FindTeams): der erste ist Meister, er uebernimmt den
            // targetname.
            const std::string teamName = text(e, "team");
            std::size_t ti = teams.size();
            const auto it = teamName.empty() ? teamNachName.end() : teamNachName.find(teamName);
            if (it != teamNachName.end()) {
                ti = it->second;
            } else {
                Team neu;
                neu.flags = sf;
                float wait = zahl(e, "wait", 0.0F);
                if (wait == 0.0F) { wait = 2.0F; }
                neu.waitMs = wait * 1000.0;
                neu.delayMs = zahl(e, "delay", 0.0F) * 1000.0;
                neu.target = text(e, "target");
                neu.target2 = text(e, "target2");
                neu.opentarget = text(e, "opentarget");
                neu.closetarget = text(e, "closetarget");
                teams.push_back(neu);
                if (!teamName.empty()) { teamNachName[teamName] = ti; }
            }
            Team& team = teams[ti];
            team.tueren.push_back(t);
            if (team.targetname.empty()) { team.targetname = text(e, "targetname"); }
            if (zahl(e, "health", 0.0F) != 0.0F) { team.flags |= 0x10000; }   // gesundheit: schiessen
            teamVonEntity[ei] = static_cast<int>(ti);
            continue;
        }
        if (e.classname == "func_wall" || e.classname == "func_usable") {
            const std::size_t mi = neuerMover(ei, modell, e);
            mover_[mi].sichtbarAnfang = (ganz(e, "spawnflags") & 1) == 0;   // START_OFF
            moverVonEntity[ei] = static_cast<int>(mi);
            continue;
        }
        if (e.classname == "func_rotating") {
            // SP_func_rotating, g_mover.cpp:2314
            const std::size_t mi = neuerMover(ei, modell, e);
            Mover& m = mover_[mi];
            winkelAus(e, m.angles0);
            float speed = zahl(e, "speed", 0.0F);
            if (speed == 0.0F) { speed = 100.0F; }
            const int sf = ganz(e, "spawnflags");
            const int achse = ((sf & 4) != 0) ? 2 : ((sf & 8) != 0) ? 0 : 1;
            m.drehung[achse] = speed;
            m.drehAnfang = (sf & 1) != 0;
            moverVonEntity[ei] = static_cast<int>(mi);
            continue;
        }
        if (e.classname == "func_bobbing") {
            // SP_func_bobbing, g_mover.cpp:2406
            const std::size_t mi = neuerMover(ei, modell, e);
            Mover& m = mover_[mi];
            const float speed = zahl(e, "speed", 4.0F);
            const float hoehe = zahl(e, "height", 32.0F);
            const float phase = zahl(e, "phase", 0.0F);
            const int sf = ganz(e, "spawnflags");
            const int achse = ((sf & 1) != 0) ? 0 : ((sf & 2) != 0) ? 1 : 2;
            m.bobDelta[achse] = hoehe;
            m.bobPeriodeMs = static_cast<double>(speed) * 1000.0;
            m.bobPhase = phase;
            m.bobAnfang = (sf & 4) == 0;
            for (int k = 0; k < 3; ++k) { m.bobBasis[k] = m.origin0[k]; }
            if (!m.bobAnfang) {   // START_OFF: steht an der Stelle der Phase
                const float s = std::sin(phase * 3.14159265358979F * 2.0F);
                for (int k = 0; k < 3; ++k) { m.origin0[k] += s * m.bobDelta[k]; }
            }
            moverVonEntity[ei] = static_cast<int>(mi);
            continue;
        }
        if (e.classname == "func_static") {
            // SP_func_static, g_mover.cpp:2195: G_SetAngles - es steht
            // GEDREHT da, wenn die Karte Winkel angibt. Bewegt wird es nur
            // vom Skript (das rechnet die Szene).
            const std::size_t mi = neuerMover(ei, modell, e);
            winkelAus(e, mover_[mi].angles0);
            continue;
        }
        if (e.classname == "func_plat") {
            // SP_func_plat, g_mover.cpp:1640: in Ruhe UNTEN (pos1).
            const std::size_t mi = neuerMover(ei, modell, e);
            Mover& m = mover_[mi];
            bool gesetzt = false;
            float hoehe = zahl(e, "height", 0.0F, &gesetzt);
            if (!gesetzt) {
                hoehe = (sm.maxs[2] - sm.mins[2]) - zahl(e, "lip", 8.0F);
            }
            m.origin0[2] -= hoehe;
            continue;
        }
    }

    // --- Die Ausloesefelder (Think_SpawnNewDoorTrigger, g_mover.cpp:1237) ---
    for (Team& t : teams) {
        const bool gesperrt = (t.flags & kLocked) != 0;
        const bool ohneFeld = !gesperrt && (!t.targetname.empty() || (t.flags & 0x10000) != 0 ||
                                            (t.flags & kPlayerUse) != 0 ||
                                            (t.flags & kForceActivate) != 0);
        if (ohneFeld) {
            continue;
        }
        t.hatFeld = true;
        bool erst = true;
        for (const Tuer& d : t.tueren) {
            const Mover& m = mover_[d.mover];
            const auto& sm = geo.models[static_cast<std::size_t>(m.modell)];
            for (int k = 0; k < 3; ++k) {
                const float lo = m.origin0[k] + sm.mins[k];
                const float hi = m.origin0[k] + sm.maxs[k];
                t.feldMin[k] = erst ? lo : std::min(t.feldMin[k], lo);
                t.feldMax[k] = erst ? hi : std::max(t.feldMax[k], hi);
            }
            erst = false;
        }
        int duenn = 0;
        for (int k = 1; k < 3; ++k) {
            if (t.feldMax[k] - t.feldMin[k] < t.feldMax[duenn] - t.feldMin[duenn]) { duenn = k; }
        }
        t.feldMax[duenn] += 120.0F;
        t.feldMin[duenn] -= 120.0F;
    }

    // --- Die Wege der Zuege (Think_SetupTrainTargets, g_mover.cpp:2029) -----
    //
    // Der erste Halt ist die ERSTE Entity mit dem targetname (G_Find, ohne
    // Klassenpruefung); von dort aus zeigt jede Ecke auf die erste
    // path_corner unter ihren Zielen.
    std::vector<int> naechsteEcke(map.entities.size(), -1);
    const auto findeName = [&](const std::string& name, bool nurEcke) -> int {
        if (name.empty()) {
            return -1;
        }
        for (std::size_t i = 0; i < map.entities.size(); ++i) {
            const MapEntity& z = map.entities[i];
            if (gleichOhneFall(text(z, "targetname"), name) &&
                (!nurEcke || z.classname == "path_corner")) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    for (Zug& z : zuege) {
        z.naechste = findeName(text(map.entities[z.entity], "target"), false);
        int start = -1;
        int iter = 2000;
        for (int weg = z.naechste; weg >= 0 && weg != start && iter-- > 0;) {
            if (start < 0) { start = weg; }
            const int nx = findeName(text(map.entities[static_cast<std::size_t>(weg)], "target"), true);
            if (nx < 0) {
                break;
            }
            naechsteEcke[static_cast<std::size_t>(weg)] = nx;
            weg = nx;
        }
    }

    // --- Die Ereignisse -----------------------------------------------------
    struct Ereignis { double ms; std::string ziel; int tiefe; std::string ausloeser; };
    std::vector<Ereignis> warte;
    for (const MoverUse& u : uses) {
        // tiefe -1: schon aufgeloest (siehe MoverUse::aufgeloest)
        warte.push_back(Ereignis{u.ms, u.target, u.aufgeloest ? -1 : 0, u.ausloeser});
    }

    // rc601 (bewegung, teil 3): moverState und MOVER_LOCKED je Tuer merken -
    // is_valid fragt beides bei jedem A* (g_navigator.cpp:584-622 MD).
    // MatchTeam setzt den Zustand fuer das ganze Team (g_mover.cpp).
    const auto merke = [&](const Team& t, double ab) {
        const int z = t.lage == Lage::Pos1 ? 0 : t.lage == Lage::Pos2 ? 1 : t.lage == Lage::AufDemWeg ? 2 : 3;
        for (const Tuer& d : t.tueren) {
            tuerWechsel_[mover_[d.mover].modell].push_back(TuerWechsel{ab, z, (t.flags & kLocked) != 0});
        }
    };
    for (const Team& t : teams) { merke(t, -1.0e9); }

    auto bahnStueck = [&](Team& t, double ab, bool auf) {
        // Jede Tuer des Teams vom AKTUELLEN Ort zum Ziel. Kehrt sie mitten
        // im Weg um, bleibt der Ort stetig; die Dauer ist der Rest.
        const double voll = t.tueren.front().dauerMs;
        for (Tuer& d : t.tueren) {
            Mover& m = mover_[d.mover];
            const Stand s = at(m.modell, ab);
            const float* ziel = auf ? d.pos2 : d.pos1;
            const float* start = auf ? d.pos1 : d.pos2;
            double rest = 1.0;
            const float gx = ziel[0] - start[0], gy = ziel[1] - start[1], gz = ziel[2] - start[2];
            const float lx = ziel[0] - s.origin[0], ly = ziel[1] - s.origin[1], lz = ziel[2] - s.origin[2];
            const float ganzW = std::sqrt(gx * gx + gy * gy + gz * gz);
            if (ganzW > 0.001F) {
                rest = std::clamp(static_cast<double>(std::sqrt(lx * lx + ly * ly + lz * lz) / ganzW), 0.0, 1.0);
            }
            Stueck st;
            st.abMs = ab;
            st.dauerMs = std::max(1.0, d.dauerMs * rest);
            for (int k = 0; k < 3; ++k) {
                st.von[k] = s.origin[k];
                st.nach[k] = ziel[k];
            }
            st.linear = d.linear;
            m.bahn.push_back(st);
        }
        t.bahnAb = ab;
        t.bahnDauer = std::max(1.0, voll * 1.0);
        // Der Meister gibt den Takt: seine (Rest-)Dauer.
        t.bahnDauer = mover_[t.tueren.front().mover].bahn.back().dauerMs;
    };

    std::function<void(std::size_t, double, int)> tuerGo;
    std::function<void(std::size_t, double, int)> tuerUse;
    std::function<void(const std::string&, double, int, const std::string&)> benutze;

    // Use_BinaryMover_Go, g_mover.cpp:805
    tuerGo = [&](std::size_t ti, double t, int tiefe) {
        Team& team = teams[ti];
        switch (team.lage) {
            case Lage::Pos1:
                team.lage = Lage::AufDemWeg;
                merke(team, t);   // rc601 (bewegung, teil 3)
                bahnStueck(team, t + 50.0, true);   // "start moving 50 msec later"
                {
                    Oeffnung o;
                    o.modell = mover_[team.tueren.front().mover].modell;
                    o.abMs = t + 50.0;
                    o.offenMs = t + 50.0 + team.bahnDauer;
                    auf_.push_back(o);
                }
                if (!team.target.empty()) { benutze(team.target, t, tiefe + 1, std::string{}); }
                break;
            case Lage::Pos2:
                // Offen: nur die Rueckkehr neu stellen (TOGGLE sofort).
                team.zurueckUm = ((team.flags & kToggle) != 0) ? t + kTakt : t + team.waitMs;
                if (!team.target2.empty()) { benutze(team.target2, t, tiefe + 1, std::string{}); }
                break;
            case Lage::ZurueckDemWeg:
                team.lage = Lage::AufDemWeg;
                merke(team, t);   // rc601 (bewegung, teil 3)
                bahnStueck(team, t, true);
                break;
            case Lage::AufDemWeg:
                team.lage = Lage::ZurueckDemWeg;
                merke(team, t);   // rc601 (bewegung, teil 3)
                bahnStueck(team, t, false);
                break;
        }
    };
    // Use_BinaryMover, g_mover.cpp:957
    tuerUse = [&](std::size_t ti, double t, int tiefe) {
        Team& team = teams[ti];
        if (team.fertig) {
            return;
        }
        if ((team.flags & kLocked) != 0) {   // UnLockDoors: nur entsperren
            team.flags &= ~kLocked;
            merke(team, t);   // rc601 (bewegung, teil 3)
            return;
        }
        if (team.delayMs > 0.0) {
            if (team.losUm < 0.0) { team.losUm = t + team.delayMs; }
            return;
        }
        tuerGo(ti, t, tiefe);
    };

    // --- Zerbrechen ---------------------------------------------------------
    // rc601 ton: `temp` - der Klang ist eine Temp-Entity/G_Sound (siehe
    // KartenKlang::tempEntity), nicht CG_Chunks.
    const auto klang = [&](std::size_t ent, const std::string& datei, double t, const float ort[3],
                           bool temp = false) {
        KartenKlang k;
        k.entity = ent;
        k.datei = datei;
        k.ms = t;
        k.bisMs = t;
        k.tempEntity = temp;
        for (int q = 0; q < 3; ++q) { k.ort[q] = ort[q]; }
        bruchKlang_.push_back(k);
    };
    // CG_MiscModelExplosion, cg_effects.cpp:180
    const auto explosion = [&](std::size_t ent, const float mn[3], const float mx[3], int groesse,
                               int material, double t, Wuerfel& w) {
        int ct = 13;
        const char* eff = nullptr;
        const char* eff2 = nullptr;
        switch (material) {
            case kMatGlass: eff = "chunks/glassbreak"; ct = 5; break;
            case kMatGlassMetal: eff = "chunks/glassbreak"; eff2 = "chunks/metalexplode"; ct = 5; break;
            case kMatElectrical:
            case kMatElecMetal: eff = "chunks/sparkexplode"; ct = 5; break;
            case kMatMetal:
            case kMatMetal2:
            case kMatMetal3:
            case kMatCrate1:
            case kMatCrate2: eff = "chunks/metalexplode"; ct = 2; break;
            case kMatGrate1: eff = "chunks/grateexplode"; ct = 8; break;
            case kMatRope: eff = "chunks/ropebreak"; ct = 20; break;
            case kMatWhiteMetal:
            case kMatDrkStone:
            case kMatLtStone:
            case kMatGreyStone: eff = (groesse == 2) ? "chunks/rockbreaklg" : "chunks/rockbreakmed"; break;
            default: break;
        }
        if (eff == nullptr) {
            return;   // MAT_NONE und alles ohne Effekt
        }
        ct += 7 * groesse;
        float mitte[3];
        for (int k = 0; k < 3; ++k) { mitte[k] = 0.5F * (mn[k] + mx[k]); }
        for (int i = 0; i < ct; ++i) {
            BruchEffekt b;
            b.entity = ent;
            b.ms = t;
            for (int k = 0; k < 3; ++k) {
                const float r = w.f01() * 0.8F + 0.1F;
                b.ort[k] = r * mn[k] + (1.0F - r) * mx[k];
                b.richtung[k] = b.ort[k] - mitte[k];
            }
            normiere(b.richtung);   // "shoot effect away from center"
            b.effekt = (eff2 != nullptr && w.bit()) ? eff2 : eff;
            bruchFx_.push_back(std::move(b));
        }
    };
    // CG_Chunks, cg_effects.cpp:288
    const auto werfeStuecke = [&](std::size_t ent, const float mitte[3], const float mn[3],
                                  const float mx[3], int anzahl, int material,
                                  const std::string& eigenesModell, float basis,
                                  const std::string& eigenerKlang, double t, Wuerfel& w) {
        if (material == kMatNone) {
            return;   // "Well, we should do nothing" - auch kein eigener Klang
        }
        if (!eigenerKlang.empty()) { klang(ent, eigenerKlang, t, mitte); }
        std::string stdKlang;
        bool nurKlang = false;
        float tempo = 1.0F;
        switch (material) {
            case kMatGlass: stdKlang = "sound/weapons/explosions/glassbreak1.wav"; nurKlang = true; break;
            case kMatGrate1: stdKlang = "sound/effects/grate_destroy.wav"; nurKlang = true; break;
            case kMatElectrical:
                stdKlang = "sound/ambience/spark" + std::to_string(w.ganz(1, 6)) + ".wav";
                nurKlang = true;
                break;
            case kMatDrkStone:
            case kMatLtStone:
            case kMatGreyStone:
            case kMatWhiteMetal: stdKlang = "sound/effects/wall_smash.wav"; tempo = 0.5F; break;
            case kMatGlassMetal: stdKlang = "sound/weapons/explosions/glassbreak1.wav"; break;
            case kMatCrate1:
            case kMatCrate2:
                stdKlang = "sound/weapons/explosions/crateBust" + std::to_string(w.ganz(1, 2)) + ".wav";
                break;
            case kMatMetal:
            case kMatMetal2:
            case kMatMetal3:
            case kMatElecMetal: stdKlang = "sound/weapons/explosions/glasslcar.wav"; tempo = 0.8F; break;
            case kMatRope: return;   // keine Stuecke, kein Klang (FIXME der Engine)
            default: break;
        }
        if (eigenerKlang.empty() && !stdKlang.empty()) { klang(ent, stdKlang, t, mitte); }
        if (nurKlang) {
            return;   // Glas, Gitter, Funken: nur Effekt und Klang
        }
        if (basis <= 0.0F) { basis = 1.0F; }
        constexpr float kTempo = 300.0F;   // beide Aufrufer geben 300
        for (int i = 0; i < anzahl; ++i) {
            const std::string mod = !eigenesModell.empty() ? eigenesModell : stueckModell(material, w);
            if (mod.empty()) {
                continue;   // keine RGB-Achsen werfen
            }
            Truemmer tr;
            tr.entity = ent;
            tr.modell = mod;
            tr.abMs = t;
            tr.bisMs = t + 1300.0 + static_cast<double>(w.f01()) * 900.0;
            Truemmer::Flug f;
            f.abMs = t;
            float dir[3];
            for (int k = 0; k < 3; ++k) {
                const float r = w.f01() * 0.8F + 0.1F;
                f.ort[k] = r * mn[k] + (1.0F - r) * mx[k];
                dir[k] = f.ort[k] - mitte[k];
            }
            normiere(dir);
            const float v = w.bereich(kTempo * 0.5F, kTempo * 1.25F) * tempo;
            for (int k = 0; k < 3; ++k) { f.vel[k] = dir[k] * v; }
            for (float& a : tr.winkel) { a = w.f01() * 360.0F; }
            tr.drehung[0] = w.bereich(-1.0F, 1.0F);
            tr.drehung[1] = w.bereich(-1.0F, 1.0F);
            tr.drehung[2] = 0.0F;   // "don't do roll"
            const float dreh = w.f01() * 600.0F + 200.0F;
            for (float& d : tr.drehung) { d *= dreh; }
            const float abprall = 0.2F + w.f01() * 0.2F;
            tr.radius = w.bereich(basis * 0.75F, basis * 1.25F);
            tr.flug.push_back(f);
            fliege(tr, abprall, geo);
            truemmer_.push_back(std::move(tr));
        }
    };
    const auto saatVon = [](const Bruchstelle& b) {
        return static_cast<std::uint32_t>(b.entity) * 2654435761U +
               static_cast<std::uint32_t>(b.brueche) * 40503U + 12345U;
    };
    // funcBBrushDieGo, g_breakable.cpp:79
    const auto brichBrush = [&](Bruchstelle& b, double t, int tiefe, const std::string& ausloeser) {
        Wuerfel w(saatVon(b));
        ++b.brueche;
        b.zerbrochen = true;
        b.weg = true;
        // "if (self->target && attacker != nullptr) G_UseTargets" - der
        // Angreifer ist, wer benutzt hat. Kam das use aufgeloest, feuert
        // der Nachbau die Ziele (MoverUse::aufgeloest).
        if (tiefe >= 0 && !b.target.empty()) { benutze(b.target, t, tiefe + 1, ausloeser); }
        float gr[3];
        for (int k = 0; k < 3; ++k) { gr[k] = b.absMax[k] - b.absMin[k]; }
        int anzahl = static_cast<int>(w.f01() * 6.0F + 18.0F);
        float skala = std::sqrt(std::sqrt(gr[0] * gr[1] * gr[2])) * 1.75F;
        const int groesse = (skala > 48.0F) ? 2 : (skala > 24.0F) ? 1 : 0;
        skala /= static_cast<float>(anzahl);
        if (b.radius > 0.0F) { anzahl = stueckZahl(anzahl, b.radius); }
        float mitte[3];
        for (int k = 0; k < 3; ++k) { mitte[k] = 0.5F * (b.absMin[k] + b.absMax[k]); }
        if ((b.flags & kNoExplosion) == 0) {
            explosion(b.entity, b.absMin, b.absMax, groesse, b.material, t, w);
        }
        if (b.splash) {
            // G_TempEntity(org, EV_GENERAL_SOUND) mit cargoexplode
            klang(b.entity, "sound/weapons/explosions/cargoexplode.wav", t, mitte, true);
        }
        werfeStuecke(b.entity, mitte, b.absMin, b.absMax, anzahl, b.material, std::string{}, skala,
                     b.noise, t, w);
        // thinkF_G_FreeEntity, nextthink = level.time + 50: im Bild des
        // Bruchs steht der Brush noch, erst im naechsten ist er weg.
        if (b.mover >= 0) {
            mover_[static_cast<std::size_t>(b.mover)].sichtAb.emplace_back(t + kTakt, false);
        }
        bruch_.push_back(Bruch{b.entity, "func_breakable", t, b.material, groesse, anzahl});
    };
    // misc_model_breakable_die, g_breakable.cpp:489
    const auto brichModell = [&](Bruchstelle& b, double t, int tiefe, const std::string& ausloeser) {
        Wuerfel w(saatVon(b));
        ++b.brueche;
        float gr[3];
        for (int k = 0; k < 3; ++k) { gr[k] = b.absMax[k] - b.absMin[k]; }
        int anzahl = static_cast<int>(w.f01() * 6.0F + 20.0F);
        float skala = std::sqrt(std::sqrt(gr[0] * gr[1] * gr[2])) * 1.75F;
        const int groesse = (skala > 48.0F) ? 2 : (skala > 24.0F) ? 1 : 0;
        skala /= static_cast<float>(anzahl);
        if (b.radius > 0.0F) { anzahl = stueckZahl(anzahl, b.radius); }
        float mitte[3];
        for (int k = 0; k < 3; ++k) { mitte[k] = 0.5F * (b.absMin[k] + b.absMax[k]); }
        // modelindex3 (_c1) gibt es nur mit health, und gezeichnet wird es
        // nur, wenn die Datei geladen werden konnte (cgs.model_draw).
        const std::string eigenes = (!b.c1.empty() && dateiDa && dateiDa(b.c1)) ? b.c1 : std::string{};
        werfeStuecke(b.entity, mitte, b.absMin, b.absMax, anzahl, b.material, eigenes, skala,
                     std::string{}, t, w);
        // G_UseTargets(self, attacker) - hier ohne Pruefung auf den Angreifer
        if (tiefe >= 0 && !b.target.empty()) { benutze(b.target, t, tiefe + 1, ausloeser); }
        if ((b.flags & kNoExplosion) == 0) {
            if (b.splash && b.tie) {
                // "TEMP HACK for Tie Fighters- they're HUGE"
                BruchEffekt fx;
                fx.entity = b.entity;
                fx.effekt = "explosions/fighter_explosion2";
                fx.ms = t;
                for (int k = 0; k < 3; ++k) { fx.ort[k] = b.ursprung[k]; }
                bruchFx_.push_back(fx);
                klang(b.entity, "sound/weapons/tie_fighter/TIEexplode.wav", t, b.ursprung, true);
            } else {
                explosion(b.entity, b.absMin, b.absMax, groesse, b.material, t, w);
                // G_Sound(self, cargoexplode) - auch beim blossen Zerbrechen
                klang(b.entity, "sound/weapons/explosions/cargoexplode.wav", t, b.ursprung, true);
            }
        }
        // Schadensmodell oder weg. modelindex2 wird nie -1 (FIXME der
        // Engine): ohne health zeigt es auf Modell 0 und damit auf nichts,
        // bleibt aber als Entity stehen - und bricht beim naechsten use
        // wieder. Mit "no dmodel" (8) wird es freigegeben.
        if ((b.flags & 8) == 0) {
            modellWechsel_[b.entity].push_back(ModellWechsel{t, b.hatGesundheit ? b.d1 : std::string{}, true});
        } else {
            modellWechsel_[b.entity].push_back(ModellWechsel{t, std::string{}, true});
            b.weg = true;
        }
        b.zerbrochen = true;
        bruch_.push_back(Bruch{b.entity, "misc_model_breakable", t, b.material, groesse, anzahl});
    };
    // funcBBrushUse, g_breakable.cpp:201
    const auto brushUse = [&](Bruchstelle& b, double t, int tiefe, const std::string& ausloeser) {
        if (b.weg) {
            return;
        }
        if ((b.flags & kUseNotBreak) != 0) {
            // "Using it doesn't break it, makes it use it's targets"
            if (tiefe >= 0 && !b.target.empty()) { benutze(b.target, t, tiefe + 1, ausloeser); }
            return;
        }
        // funcBBrushDie: mit "delay" erst spaeter (ein zweites use in der
        // Zwischenzeit stellt nextthink neu).
        if (b.delaySek > 0) {
            b.bruchUm = t + static_cast<double>(b.delaySek) * 1000.0;
            b.bruchTiefe = tiefe;
            b.bruchAusloeser = ausloeser;
            return;
        }
        brichBrush(b, t, tiefe, ausloeser);
    };
    // misc_model_use, g_breakable.cpp:723
    const auto modellUse = [&](Bruchstelle& b, double t, int tiefe, const std::string& ausloeser) {
        if (b.weg || b.geworfen) {
            return;   // freigegeben, oder target4: wirft sich - nicht nachgebaut
        }
        if (b.zerbrochen && b.hatGesundheit) {
            // "used while broken fired target3"
            if (tiefe >= 0 && !b.target3.empty()) { benutze(b.target3, t, tiefe + 1, ausloeser); }
            return;
        }
        std::vector<ModellWechsel>& mw = modellWechsel_[b.entity];
        if (!b.erschienen) {
            // "Become solid again": count, EF_NODRAW weg (Start off)
            b.erschienen = true;
            if ((b.flags & kModellStartOff) != 0) {
                mw.push_back(ModellWechsel{t, b.modell, false});
            }
        }
        if ((b.flags & kUseNotBreak) != 0) {
            if ((b.flags & 32) != 0 && !b.u1.empty()) {
                // "Usemodels toggling": Hauptmodell <-> _u1
                b.zeigtU1 = !b.zeigtU1;
                mw.push_back(ModellWechsel{t, b.zeigtU1 ? b.u1 : b.modell, false});
            }
            return;
        }
        brichModell(b, t, tiefe, ausloeser);
    };

    // --- func_train -----------------------------------------------------
    const auto eckOrt = [&](int ecke, float out[3]) {
        out[0] = out[1] = out[2] = 0.0F;
        const MapEntity& z = map.entities[static_cast<std::size_t>(ecke)];
        if (!z.origin.empty()) { vektor(z.origin, out); }
    };
    // Reached_Train, g_mover.cpp:1885 (auch TrainUse)
    const auto zugErreicht = [&](Zug& z, double t) {
        const int next = z.naechste;
        if (next < 0 || naechsteEcke[static_cast<std::size_t>(next)] < 0) {
            z.faehrt = false;   // "just stop"
            return;
        }
        const MapEntity& ne = map.entities[static_cast<std::size_t>(next)];
        const std::string zugName = text(map.entities[z.entity], "targetname");
        // "fire all other targets" - der Zug ist der Ausloeser. Das weiss
        // nur MoverSim (der Nachbau kennt die Fahrzeiten nicht), also
        // feuert es hier, auch wenn das use aufgeloest kam.
        benutze(text(ne, "target"), t, 1, zugName);
        z.naechste = naechsteEcke[static_cast<std::size_t>(next)];
        eckOrt(next, z.pos1);
        eckOrt(z.naechste, z.pos2);
        float tempo = zahl(ne, "speed", 0.0F);
        if (tempo == 0.0F) { tempo = z.speed; }
        if (tempo < 1.0F) { tempo = 1.0F; }
        float weg[3];
        for (int k = 0; k < 3; ++k) { weg[k] = z.pos2[k] - z.pos1[k]; }
        const float laenge = std::sqrt(weg[0] * weg[0] + weg[1] * weg[1] + weg[2] * weg[2]);
        // trDuration ist ein int; SetMoverState macht aus 0 eine 1.
        {
            // In double und begrenzt: riesige path_corner-Orte machten die
            // Umwandlung nach int undefiniert (Code-Pruefung rc592).
            const double ms = static_cast<double>(laenge) * 1000.0 / static_cast<double>(tempo);
            z.dauer = (ms >= 1.0 && ms < 2.0e9) ? static_cast<double>(static_cast<int>(ms)) : (ms >= 2.0e9 ? 2.0e9 : 1.0);
        }
        if (z.dauer <= 0.0) { z.dauer = 1.0; }
        Mover& m = mover_[z.mover];
        const Stand jetzt = at(m.modell, t);
        const int nsf = ganz(ne, "spawnflags");
        // Winkel: TURN_TRAIN (1) dreht in 2 s auf die Fahrtrichtung,
        // YAW_TRAIN (4) nur die Gier, ROLL_TRAIN (8) legt sich dazu in die
        // Kurve. trDelta bleibt sonst stehen, wie es war.
        if ((nsf & 1) != 0 || (nsf & 4) != 0) {
            float ziel[3];
            vektorWinkel(weg, ziel);
            float angs[3];
            for (int k = 0; k < 3; ++k) { angs[k] = winkelDifferenz(ziel[k], jetzt.angles[k]); }
            if ((nsf & 1) != 0) {
                for (int k = 0; k < 3; ++k) { z.drehDelta[k] = angs[k] * 0.5F; }
            } else {
                z.drehDelta[1] = angs[1] * 0.5F;
                if ((nsf & 8) != 0) { z.drehDelta[2] = angs[1] * -0.1F; }
            }
            Stueck d;
            d.abMs = t;
            d.dauerMs = 2000.0;
            d.linear = z.linear;
            for (int k = 0; k < 3; ++k) {
                d.von[k] = jetzt.angles[k];
                d.nach[k] = jetzt.angles[k] + z.drehDelta[k] * 2.0F;
            }
            m.drehBahn.push_back(d);
        }
        // INVISIBLE (2): unsichtbar bis zur naechsten Ecke ohne das Flag
        const float wait = zahl(ne, "wait", 0.0F);
        if ((nsf & 2) != 0) {
            z.unsichtbar = true;
        } else if (wait == 0.0F) {
            z.unsichtbar = false;
        }
        m.sichtAb.emplace_back(t, !z.unsichtbar);
        if (wait != 0.0F) {
            // TR_STATIONARY auf pos1, Think_BeginMoving nach "wait". Ein
            // negativer Wert laeuft noch im selben Bild los (G_RunThink
            // nach G_MoverTeam).
            Stueck ruh;
            ruh.abMs = t;
            for (int k = 0; k < 3; ++k) { ruh.von[k] = ruh.nach[k] = z.pos1[k]; }
            m.bahn.push_back(ruh);
            z.faehrt = false;
            z.losUm = std::max(t, t + static_cast<double>(wait) * 1000.0);
            return;
        }
        Stueck st;
        st.abMs = t;
        st.dauerMs = z.dauer;
        st.linear = z.linear;
        for (int k = 0; k < 3; ++k) {
            st.von[k] = z.pos1[k];
            st.nach[k] = z.pos2[k];
        }
        m.bahn.push_back(st);
        z.faehrt = true;
        z.trTime = t;
        z.losUm = -1.0;
    };
    // Think_BeginMoving, g_mover.cpp:1861
    const auto zugLos = [&](Zug& z, double t) {
        Mover& m = mover_[z.mover];
        if (z.tie) {
            z.unsichtbar = false;
            m.sichtAb.emplace_back(t, true);
        }
        Stueck st;
        st.abMs = t;
        st.dauerMs = z.dauer;
        st.linear = z.linear;
        for (int k = 0; k < 3; ++k) {
            st.von[k] = z.pos1[k];
            st.nach[k] = z.pos2[k];
        }
        m.bahn.push_back(st);
        z.faehrt = true;
        z.trTime = t;
    };

    // --- target_speaker (Use_Target_Speaker, g_target.cpp:171) --------------
    const auto sprecherUse = [&](Sprecher& s, double t, const std::string& ausloeser) {
        if (s.stumm || s.tot || s.sperreBis > t) {
            return;
        }
        std::string datei = s.noise;
        if (s.sounds > 0) {
            // va(soundGroup, Q_irand(1, sounds)) - das %d von Hand, ohne
            // den Kartentext als Formatzeichenkette zu nehmen.
            Wuerfel w(static_cast<std::uint32_t>(s.entity) * 7919U + static_cast<std::uint32_t>(t));
            datei = s.gruppe;
            const std::size_t p = datei.find("%d");
            if (p != std::string::npos) { datei.replace(p, 2, std::to_string(w.ganz(1, s.sounds))); }
        }
        const bool amAusloeser = (s.flags & 8) != 0;
        if ((s.flags & 3) != 0) {
            // Schleife: an <-> aus, am Lautsprecher oder am Ausloeser
            const std::string traeger = amAusloeser ? "@" + klein(ausloeser) : "#" + std::to_string(s.entity);
            const auto offen = schleifeOffen.find(traeger);
            if (offen != schleifeOffen.end()) {
                lautsprecher_[offen->second].bisMs = t;
                schleifeOffen.erase(offen);
            } else if (!datei.empty()) {
                KartenKlang k;
                k.entity = s.entity;
                k.datei = datei;
                k.ms = t;
                k.schleife = true;
                k.amAusloeser = amAusloeser;
                k.ausloeser = ausloeser;
                for (int q = 0; q < 3; ++q) { k.ort[q] = s.ort[q]; }
                lautsprecher_.push_back(k);
                schleifeOffen[traeger] = lautsprecher_.size() - 1;
            }
        } else if (!datei.empty()) {
            KartenKlang k;
            k.entity = s.entity;
            k.datei = datei;
            k.ms = t;
            k.bisMs = t;
            k.amAusloeser = amAusloeser;
            k.global = !amAusloeser && (s.flags & 4) != 0;   // EV_GLOBAL_SOUND
            k.ausloeser = ausloeser;
            for (int q = 0; q < 3; ++q) { k.ort[q] = s.ort[q]; }
            lautsprecher_.push_back(k);
        }
        if (s.waitMs < 0.0) {
            s.tot = true;   // "BYE!"
        } else {
            s.sperreBis = t + s.waitMs;
        }
    };

    // G_UseTargets2: alles mit diesem targetname
    // rc605 (fix605): target_counter - count (SP_target_counter: ohne 2,
    // max_health = count) und bounceCount je Entity (g_target.cpp:579-641
    // OpenJK, :647-715 MD).
    struct Zaehler { int count = 2; int max = 2; int bounce = 0; };
    std::map<std::size_t, Zaehler> zaehler;
    benutze = [&](const std::string& ziel, double t, int tiefe, const std::string& ausloeser) {
        if (ziel.empty() || tiefe > 16) {
            return;
        }
        std::vector<bool> teamSchon(teams.size(), false);
        for (std::size_t ei = 0; ei < map.entities.size(); ++ei) {
            const MapEntity& e = map.entities[ei];
            const std::string tn = text(e, "targetname");
            bool trifft = gleichOhneFall(tn, ziel);
            // Ein Team hoert auf den Namen seines Meisters (G_FindTeams).
            if (!trifft && teamVonEntity[ei] >= 0 &&
                gleichOhneFall(teams[static_cast<std::size_t>(teamVonEntity[ei])].targetname, ziel)) {
                trifft = true;
            }
            if (!trifft) {
                continue;
            }
            if (teamVonEntity[ei] >= 0) {
                const auto ti = static_cast<std::size_t>(teamVonEntity[ei]);
                if (!teamSchon[ti]) {
                    teamSchon[ti] = true;
                    tuerUse(ti, t, tiefe);
                }
                continue;
            }
            if (bruchVonEntity[ei] >= 0) {
                Bruchstelle& b = bruchstellen[static_cast<std::size_t>(bruchVonEntity[ei])];
                if (b.istModell) {
                    modellUse(b, t, tiefe, ausloeser);
                } else {
                    brushUse(b, t, tiefe, ausloeser);
                }
                continue;
            }
            if (zugVonEntity[ei] >= 0) {
                // TrainUse -> Reached_Train, auch mitten in der Fahrt: der
                // Zug springt an die Ecke, auf die er zufuhr.
                zugErreicht(zuege[static_cast<std::size_t>(zugVonEntity[ei])], t);
                continue;
            }
            if (sprecherVonEntity[ei] >= 0) {
                sprecherUse(sprecher[static_cast<std::size_t>(sprecherVonEntity[ei])], t, ausloeser);
                continue;
            }
            if (lichtVonEntity[ei] >= 0) {
                // misc_dlight_use: umschalten, misc_lightstyle_set
                Licht& l = lichter[static_cast<std::size_t>(lichtVonEntity[ei])];
                l.an = !l.an;
                licht_.push_back(LichtSchaltung{ei, l.stil, t, l.an, l.an ? l.stilAn : l.stilAus});
                continue;
            }
            if (e.classname == "target_print") {
                // Use_Target_Print -> "cp" nur an den Spieler
                Bildschirmtext b;
                b.entity = ei;
                b.ms = t;
                b.bisMs = t + 3000.0;
                b.text = kartenText(text(e, "message"));
                b.gezeigt = gleichOhneFall(ausloeser, "player");
                texte_.push_back(std::move(b));
                continue;
            }
            if (moverVonEntity[ei] >= 0) {
                Mover& m = mover_[static_cast<std::size_t>(moverVonEntity[ei])];
                if (m.klasse == "func_wall" || m.klasse == "func_usable") {
                    m.umschalten.push_back(t);   // use_wall: an <-> aus
                } else if (m.klasse == "func_rotating") {
                    // func_rotating_use: an/aus, der Winkel bleibt stehen,
                    // wo er ist (TR_STATIONARY auf currentAngles).
                    m.drehUm.push_back(t);
                } else if (m.klasse == "func_bobbing") {
                    // func_bobbing_use: aus haelt am aktuellen Ort, an
                    // setzt die Welle an der gemerkten Phase fort.
                    m.bobUm.push_back(t);
                }
                continue;
            }
            if (e.classname == "fx_target_beam") {
                entityUse_.emplace_back(ei, t);   // rc604: fuer zielStrahlFeuer
                // fx_target_beam_use (g_fx.cpp:1091-1109 OpenJK): ONE_SHOT feuert
                // sofort (fx_target_beam_fire), think bleibt aus; sonst think an
                // (nextthink = level.time + 50 -> fire_start) oder aus.
                for (Strahl& st : strahlen) {
                    if (st.entity != ei) { continue; }
                    if (st.ohneUseAb >= 0.0 && t >= st.ohneUseAb) { continue; }   // e_UseFunc = useF_NULL
                    if (st.oneShot) {
                        strahl_.push_back(FxRunnerStart{ei, t, true, -1.0});
                    } else if (!st.an) {
                        strahl_.push_back(FxRunnerStart{ei, t + 50.0, false, -1.0});
                        st.an = true;
                        st.offen = strahl_.size() - 1;
                        if (zahl(e, "wait", 0.0F) < 0.0F) { st.ohneUseAb = t + 50.0; }
                    } else {
                        strahl_[st.offen].bisMs = t;
                        st.an = false;
                    }
                }
                continue;
            }
            if (e.classname == "misc_trip_mine" || e.classname == "misc_ion_cannon" ||
                e.classname == "misc_sentry_turret") {
                // rc604: misc_trip_mine_activate (g_misc.cpp:1799-1817 OpenJK),
                // ion_cannon_use (g_turret.cpp:2028-2041), pas_use (:1395-1411) -
                // was daraus folgt, rechnen laserfalleLauf, ionenkanoneLauf und
                // pasAufklappenMs.
                entityUse_.emplace_back(ei, t);
                continue;
            }
            if (e.classname == "fx_runner") {
                // rc604: e_UseFunc setzt erst fx_runner_link, 400 ms nach dem
                // Spawnen (SP_fx_runner, code/game/g_fx.cpp:262-263, 226-230
                // OpenJK) - ein use davor trifft keine use-Funktion.
                if (t < kNavKartenSpawnMs + 400.0) { continue; }
                for (Runner& r : runner) {
                    if (r.entity != ei) { continue; }
                    if (r.oneShot) {
                        fx_.push_back(FxRunnerStart{ei, t, true, -1.0});
                    } else if (!r.an) {
                        fx_.push_back(FxRunnerStart{ei, t, false, -1.0});
                        r.an = true;
                        r.offen = fx_.size() - 1;
                    } else {
                        fx_[r.offen].bisMs = t;
                        r.an = false;
                    }
                }
                continue;
            }
            if (tiefe < 0 && (e.classname == "target_relay" || e.classname == "target_delay" ||
                              e.classname == "target_counter")) {
                continue;   // die Ziele kamen schon einzeln
            }
            if (e.classname == "target_counter") {
                // rc605 (fix605): target_counter_use (g_target.cpp:579-626 OpenJK):
                // bei count 0 nichts; sonst count--; bleibt etwas, target2
                // (G_UseTargets2); sonst G_UseTargets - und mit bounceCount
                // (-1: immer) wieder von vorn. Unaufgeloest kommen nur die uses,
                // die der Nachbau nicht kennt (die Ziele einer misc_trip_mine,
                // WP_Explode) - der Nachbau rechnet seine Zaehler selbst
                // (ablauf.cpp), ohne den Stand von hier.
                auto it = zaehler.find(ei);
                if (it == zaehler.end()) {
                    Zaehler z;
                    z.count = ganz(e, "count");
                    if (z.count == 0) { z.count = 2; }
                    z.max = z.count;
                    z.bounce = ganz(e, "bounceCount");
                    it = zaehler.emplace(ei, z).first;
                }
                Zaehler& z = it->second;
                if (z.count == 0) { continue; }
                --z.count;
                if (z.count != 0) {
                    benutze(text(e, "target2"), t, tiefe + 1, ausloeser);
                    continue;
                }
                benutze(text(e, "target"), t, tiefe + 1, ausloeser);
                if (z.count == 0 && z.bounce != 0) {
                    z.count = z.max;
                    if (z.bounce > 0) { --z.bounce; }
                }
                continue;
            }
            if (e.classname == "target_relay") {
                // target_relay_use: nach "delay" die Ziele
                const double d = zahl(e, "delay", 0.0F) * 1000.0;
                warte.push_back(Ereignis{t + d, text(e, "target"), tiefe + 1, ausloeser});
                continue;
            }
            if (e.classname == "target_delay") {
                // SP_target_delay: "delay", sonst "wait", Vorgabe 1 s. Der
                // Zufallsanteil wird fuer die Vorschau gemittelt (0).
                bool gesetzt = false;
                float w = zahl(e, "delay", 0.0F, &gesetzt);
                if (!gesetzt) { w = zahl(e, "wait", 1.0F); }
                if (w == 0.0F) { w = 1.0F; }
                warte.push_back(Ereignis{t + static_cast<double>(w) * 1000.0,
                                         text(e, "target"), tiefe + 1, ausloeser});
                continue;
            }
        }
    };

    // --- Durchlaufen im Takt der Spiellogik ------------------------------
    std::stable_sort(warte.begin(), warte.end(),
                     [](const Ereignis& a, const Ereignis& b) { return a.ms < b.ms; });
    // Zuege ohne targetname (oder START_ON) fahren von selbst los
    // (Think_SetupTrainTargets -> Reached_Train, START_TIME_LINK_ENTS nach
    // dem Kartenstart). Wann die Karte relativ zur Zwischensequenz begann,
    // weiss hier niemand - sie fahren ab 0.
    for (Zug& z : zuege) {
        const MapEntity& e = map.entities[z.entity];
        if (text(e, "targetname").empty() || (ganz(e, "spawnflags") & 1) != 0) {
            zugErreicht(z, 0.0);
        }
    }
    for (double t = 0.0; t <= endeMs + kTakt; t += kTakt) {
        // Faellige use-Ereignisse (neue koennen dabei dazukommen).
        for (;;) {
            auto it = std::find_if(warte.begin(), warte.end(),
                                   [&](const Ereignis& e) { return e.ms <= t; });
            if (it == warte.end()) { break; }
            const Ereignis e = *it;
            warte.erase(it);
            benutze(e.ziel, e.ms, e.tiefe, e.ausloeser);
        }
        for (std::size_t ti = 0; ti < teams.size(); ++ti) {
            Team& team = teams[ti];
            if (team.losUm >= 0.0 && t >= team.losUm) {
                const double wann = team.losUm;
                team.losUm = -1.0;
                tuerGo(ti, wann, 0);
            }
            // Angekommen? (Reached_BinaryMover, g_mover.cpp:725)
            if ((team.lage == Lage::AufDemWeg || team.lage == Lage::ZurueckDemWeg) &&
                t >= team.bahnAb + team.bahnDauer) {
                const double da = team.bahnAb + team.bahnDauer;
                if (team.lage == Lage::AufDemWeg) {
                    team.lage = Lage::Pos2;
                    merke(team, t);   // rc601 (bewegung, teil 3)
                    if (team.waitMs < 0.0) {
                        team.fertig = true;
                    } else if ((team.flags & kToggle) == 0) {
                        team.zurueckUm = da + team.waitMs;
                    }
                    if (!team.opentarget.empty()) { benutze(team.opentarget, da, 1, std::string{}); }
                } else {
                    team.lage = Lage::Pos1;
                    merke(team, t);   // rc601 (bewegung, teil 3)
                    if (!team.closetarget.empty()) { benutze(team.closetarget, da, 1, std::string{}); }
                }
            }
            // ReturnToPos1
            if (team.lage == Lage::Pos2 && team.zurueckUm >= 0.0 && t >= team.zurueckUm) {
                const double wann = team.zurueckUm;
                team.zurueckUm = -1.0;
                team.lage = Lage::ZurueckDemWeg;
                merke(team, t);   // rc601 (bewegung, teil 3): G_RunThink in diesem Bild
                bahnStueck(team, wann, false);
            }
            // Ausloesefeld: eine Figur darin oeffnet und haelt offen
            // (Touch_DoorTrigger: nicht, solange sie schon aufgeht).
            if (team.hatFeld && figur && (team.flags & kLocked) == 0 &&
                team.lage != Lage::AufDemWeg && figur(t, team.feldMin, team.feldMax)) {
                tuerUse(ti, t, 0);
            }
        }
        // Verzoegerte Brueche (thinkF_funcBBrushDieGo nach "delay")
        for (Bruchstelle& b : bruchstellen) {
            if (b.bruchUm >= 0.0 && t >= b.bruchUm && !b.weg) {
                const double wann = b.bruchUm;
                b.bruchUm = -1.0;
                brichBrush(b, wann, b.bruchTiefe, b.bruchAusloeser);
            }
        }
        // Zuege: erst G_MoverTeam (angekommen?), dann G_RunThink
        // (Think_BeginMoving) - beide im Bild, nicht auf die Millisekunde.
        for (Zug& z : zuege) {
            if (z.faehrt && t >= z.trTime + z.dauer) {
                zugErreicht(z, t);
            }
            if (!z.faehrt && z.losUm >= 0.0 && t >= z.losUm) {
                z.losUm = -1.0;
                zugLos(z, t);
            }
        }
    }

    // --- Aufraeumen ---------------------------------------------------------
    const auto nachZeit = [](const KartenKlang& a, const KartenKlang& b) { return a.ms < b.ms; };
    std::stable_sort(bruchKlang_.begin(), bruchKlang_.end(), nachZeit);
    std::stable_sort(lautsprecher_.begin(), lautsprecher_.end(), nachZeit);
    std::stable_sort(bruchFx_.begin(), bruchFx_.end(),
                     [](const BruchEffekt& a, const BruchEffekt& b) { return a.ms < b.ms; });
    std::stable_sort(texte_.begin(), texte_.end(),
                     [](const Bildschirmtext& a, const Bildschirmtext& b) { return a.ms < b.ms; });
    // Ein neuer Text ersetzt den alten (cg.centerPrint hat nur einen Platz).
    // Nur gezeigte zaehlen: was nie ankam, verdraengt auch nichts.
    Bildschirmtext* vorher = nullptr;
    for (Bildschirmtext& b : texte_) {
        if (!b.gezeigt) {
            continue;
        }
        if (vorher != nullptr) { vorher->bisMs = std::min(vorher->bisMs, b.ms); }
        vorher = &b;
    }
}

MoverSim::Stand MoverSim::at(int modell, double ms) const {
    Stand s;
    if (modell < 0 || static_cast<std::size_t>(modell) >= nachModell_.size() ||
        nachModell_[static_cast<std::size_t>(modell)] < 0) {
        return s;
    }
    const Mover& m = mover_[static_cast<std::size_t>(nachModell_[static_cast<std::size_t>(modell)])];
    s.bekannt = true;
    for (int k = 0; k < 3; ++k) {
        s.origin[k] = m.origin0[k];
        s.angles[k] = m.angles0[k];
    }
    // Die Bahn: das letzte Stueck, das schon begonnen hat.
    for (const Stueck& st : m.bahn) {
        if (st.abMs > ms) {
            break;
        }
        const float f = moverAnteil(ms - st.abMs, st.dauerMs, st.linear);
        for (int k = 0; k < 3; ++k) {
            s.origin[k] = st.von[k] + (st.nach[k] - st.von[k]) * f;
        }
    }
    // func_rotating: TR_LINEAR auf den Winkeln, nur solange es an ist.
    //
    // Vorher sprang der Winkel beim Abschalten auf den Anfang zurueck; die
    // Engine laesst ihn stehen (func_rotating_use: TR_STATIONARY).
    {
        bool an = m.drehAnfang;
        double seit = 0.0;
        double anSek = 0.0;   // wie lange es bis ms gedreht hat
        for (const double u : m.drehUm) {
            if (u > ms) { break; }
            if (an) { anSek += (u - seit) * 0.001; }
            an = !an;
            seit = u;
        }
        if (an) { anSek += (ms - seit) * 0.001; }
        if (anSek > 0.0) {
            for (int k = 0; k < 3; ++k) {
                s.angles[k] = static_cast<float>(std::fmod(m.angles0[k] + m.drehung[k] * anSek, 360.0));
            }
        }
    }
    // func_bobbing: TR_SINE (bg_misc.cpp), ueber die use-Umschalter.
    //
    // Vorher galt der LETZTE Schaltzustand fuer die ganze Zeitleiste.
    if (m.bobPeriodeMs > 0.0) {
        constexpr double k2Pi = 3.14159265358979 * 2.0;
        bool an = m.bobAnfang;
        double trTime = m.bobPeriodeMs * m.bobPhase;   // SP_func_bobbing
        double radius = m.bobPhase;                    // START_OFF
        float steht[3] = {m.origin0[0], m.origin0[1], m.origin0[2]};
        for (const double u : m.bobUm) {
            if (u > ms) { break; }
            if (an) {
                // aus: am aktuellen Ort halten, Phase merken
                radius = (u - trTime) / m.bobPeriodeMs;
                const auto w = static_cast<float>(std::sin(radius * k2Pi));
                for (int k = 0; k < 3; ++k) { steht[k] = m.bobBasis[k] + w * m.bobDelta[k]; }
            } else {
                trTime = u - m.bobPeriodeMs * radius;   // an: dort weiter
            }
            an = !an;
        }
        if (an) {
            const auto w = static_cast<float>(std::sin((ms - trTime) / m.bobPeriodeMs * k2Pi));
            for (int k = 0; k < 3; ++k) { s.origin[k] = m.bobBasis[k] + w * m.bobDelta[k]; }
        } else {
            for (int k = 0; k < 3; ++k) { s.origin[k] = steht[k]; }
        }
    }
    // func_train: die Winkel in Stuecken (TR_*_STOP auf s.apos)
    for (const Stueck& st : m.drehBahn) {
        if (st.abMs > ms) {
            break;
        }
        const float f = moverAnteil(ms - st.abMs, st.dauerMs, st.linear);
        for (int k = 0; k < 3; ++k) {
            s.angles[k] = st.von[k] + (st.nach[k] - st.von[k]) * f;
        }
    }
    // Sichtbarkeit: jedes use schaltet um - oder feste Zustaende ab einer Zeit
    bool sicht = m.sichtbarAnfang;
    if (!m.sichtAb.empty()) {
        for (const auto& [ab, an] : m.sichtAb) {
            if (ab <= ms) { sicht = an; }
        }
    } else {
        for (const double u : m.umschalten) {
            if (u <= ms) { sicht = !sicht; }
        }
    }
    s.sichtbar = sicht;
    return s;
}

const Bildschirmtext* MoverSim::bildschirmtextAt(double ms) const {
    const Bildschirmtext* out = nullptr;
    for (const Bildschirmtext& b : texte_) {
        if (b.ms > ms) {
            break;
        }
        if (b.gezeigt && ms < b.bisMs) { out = &b; }
    }
    return out;
}

MoverSim::ModellStand MoverSim::modellAt(std::size_t entity, double ms) const {
    ModellStand s;
    const auto it = modellWechsel_.find(entity);
    if (it == modellWechsel_.end() || it->second.empty()) {
        return s;
    }
    s.bekannt = true;
    const ModellWechsel* w = &it->second.front();
    for (const ModellWechsel& x : it->second) {
        if (x.ms <= ms) { w = &x; }
    }
    s.modell = w->modell;
    s.sichtbar = !w->modell.empty();
    s.angehalten = w->angehalten;
    return s;
}

std::vector<std::string> MoverSim::nebenModelle() const {
    std::set<std::string> alle;
    for (const auto& [ent, liste] : modellWechsel_) {
        (void)ent;
        for (std::size_t i = 1; i < liste.size(); ++i) {   // [0] ist das Kartenmodell
            if (!liste[i].modell.empty()) { alle.insert(liste[i].modell); }
        }
    }
    for (const Truemmer& t : truemmer_) {
        alle.insert(t.modell);
    }
    return {alle.begin(), alle.end()};
}

void MoverSim::klangOrt(int modell, double ms, float out[3]) const {
    const Stand s = at(modell, ms);
    float mitte[3]{};
    if (modell >= 0 && static_cast<std::size_t>(modell) < nachModell_.size() &&
        nachModell_[static_cast<std::size_t>(modell)] >= 0) {
        const Mover& m = mover_[static_cast<std::size_t>(nachModell_[static_cast<std::size_t>(modell)])];
        for (int k = 0; k < 3; ++k) { mitte[k] = m.mitte[k]; }
    }
    for (int k = 0; k < 3; ++k) { out[k] = s.origin[k] + mitte[k]; }
}

std::vector<MoverKlang> MoverSim::klaenge(double endeMs) const {
    std::vector<MoverKlang> out;
    for (const Mover& m : mover_) {
        if (m.soundSet.empty()) {
            continue;
        }
        const auto neu = [&](int stufe, double ms, double bis) {
            MoverKlang k;
            k.entity = m.entity;
            k.modell = m.modell;
            k.soundSet = m.soundSet;
            k.stufe = stufe;
            k.ms = ms;
            k.bisMs = bis;
            out.push_back(k);
        };
        // Tueren: jede Fahrt beginnt mit START und der Schleife (auch beim
        // Umkehren mitten im Weg, Use_BinaryMover), und nur wer ANKOMMT,
        // spielt END und beendet die Schleife (Reached_BinaryMover).
        //
        // Ein func_train ruft nur G_PlayDoorLoopSound (Reached_Train) -
        // die Schleife, kein START und kein END.
        const bool zug = m.klasse == "func_train";
        for (std::size_t i = 0; i < m.bahn.size(); ++i) {
            const Stueck& st = m.bahn[i];
            if (st.dauerMs <= 0.0 || st.abMs > endeMs) {
                continue;
            }
            double ende = st.abMs + st.dauerMs;
            bool kommtAn = true;
            if (i + 1 < m.bahn.size() && m.bahn[i + 1].abMs < ende) {
                ende = m.bahn[i + 1].abMs;
                kommtAn = false;
            }
            if (!zug) { neu(0, st.abMs, st.abMs); }
            neu(1, st.abMs, ende);
            if (kommtAn && !zug) {
                neu(2, ende, ende);
            }
        }
        // func_rotating: nur auf use (func_rotating_use) - START_ON dreht
        // stumm, die Schleife setzt erst ein use.
        {
            bool an = m.drehAnfang;
            for (std::size_t i = 0; i < m.drehUm.size(); ++i) {
                const double u = m.drehUm[i];
                if (u > endeMs) { break; }
                an = !an;
                if (an) {
                    const double bis = (i + 1 < m.drehUm.size()) ? m.drehUm[i + 1] : endeMs;
                    neu(0, u, u);
                    neu(1, u, std::min(bis, endeMs));
                } else {
                    neu(2, u, u);
                }
            }
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const MoverKlang& a, const MoverKlang& b) { return a.ms < b.ms; });
    return out;
}

std::map<std::string, std::vector<std::string>> leseBmodelSets(const std::string& text) {
    std::map<std::string, std::vector<std::string>> out;
    std::istringstream zeilen(text);
    std::string zeile;
    std::string aktuell;
    const auto klein = [](std::string s) {
        for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        return s;
    };
    while (std::getline(zeilen, zeile)) {
        std::istringstream w(zeile);
        std::string wort;
        if (!(w >> wort) || wort[0] == ';' || wort.starts_with("//")) {
            continue;
        }
        const std::string k = klein(wort);
        if (k == "bmodelset") {
            std::string name;
            aktuell = (w >> name) ? klein(name) : std::string{};
            if (!aktuell.empty()) { out[aktuell]; }
            continue;
        }
        if (k == "generalset" || k == "localset") {
            aktuell.clear();   // ein anderer Satz, nicht fuer Mover
            continue;
        }
        if (k == "subwaves" && !aktuell.empty()) {
            std::string ordner;
            if (!(w >> ordner)) { continue; }
            std::vector<std::string>& liste = out[aktuell];
            liste.clear();
            std::string welle;
            while (w >> welle && liste.size() < 8) {   // MAX_WAVES_PER_GROUP
                liste.push_back(klein(welle) == "null"
                                    ? std::string{}
                                    : "sound/" + ordner + "/" + welle + ".wav");
            }
        }
    }
    return out;
}

}  // namespace bhed

namespace bhed {
namespace {
// Ein Buchstabe eines Lichtstilmusters zur Zeit: 'a' 0 bis 'z' 1, leer voll.
float musterZuZeit(const std::string& m, double ms) {
    if (m.empty()) { return 1.0F; }
    const double schritt = std::floor(std::max(0.0, ms) / 50.0);
    const auto i = static_cast<std::size_t>(std::fmod(schritt, static_cast<double>(m.size())));
    const int w = std::clamp(static_cast<int>(std::tolower(static_cast<unsigned char>(m[i]))) - 'a', 0, 25);
    return static_cast<float>(w) / 25.0F;
}
}  // namespace

std::array<std::array<float, 3>, 256> lichtStilGewichteZuZeit(const std::vector<MapEntity>& ents,
                                                              const std::vector<LichtSchaltung>& schaltungen,
                                                              double ms) {
    std::array<std::array<float, 3>, 256> g{};
    for (auto& c : g) { c = {1.0F, 1.0F, 1.0F}; }
    // Die Muster 1..31 (je Kanal) zur Zeit.
    std::array<std::array<std::string, 3>, 32> muster{};
    for (const MapEntity& e : ents) {
        if (e.classname != "worldspawn") { continue; }
        for (int i = 1; i < 32; ++i) {
            static const char* kKanal[3] = {"r", "g", "b"};
            for (int c = 0; c < 3; ++c) {
                if (const std::string* v = e.find("ls_" + std::to_string(i) + kKanal[c])) {
                    muster[static_cast<std::size_t>(i)][static_cast<std::size_t>(c)] = *v;
                }
            }
        }
        break;
    }
    const auto musterGewicht = [&](int stil) {
        std::array<float, 3> w{1.0F, 1.0F, 1.0F};
        if (stil > 0 && stil < 32) {
            for (std::size_t c = 0; c < 3U; ++c) { w[c] = musterZuZeit(muster[static_cast<std::size_t>(stil)][c], ms); }
        }
        return w;
    };
    for (int i = 1; i < 32; ++i) { g[static_cast<std::size_t>(i)] = musterGewicht(i); }
    const auto zustand = [&](int stil, bool an, int musterVon) {
        if (stil <= 0 || stil > 255) { return; }
        if (musterVon > 0 && musterVon < 256) {
            g[static_cast<std::size_t>(stil)] = musterGewicht(musterVon);
        } else {
            const float v = an ? 1.0F : 0.0F;
            g[static_cast<std::size_t>(stil)] = {v, v, v};
        }
    };
    // Der Zustand der Karte (SP_light) ...
    for (const MapEntity& e : ents) {
        if (e.classname != "light" || e.find("targetname") == nullptr) { continue; }
        const std::string* st = e.find("style");
        const int stil = st != nullptr ? std::atoi(st->c_str()) : 0;
        const std::string* sf = e.find("spawnflags");
        const bool an = ((sf != nullptr ? std::atoi(sf->c_str()) : 0) & 4) == 0;
        const std::string* sw = e.find(an ? "switch_style" : "style_off");
        zustand(stil, an, sw != nullptr ? std::atoi(sw->c_str()) : 0);
    }
    // ... und jede Schaltung bis jetzt, in ihrer Reihenfolge.
    std::vector<const LichtSchaltung*> bis;
    for (const LichtSchaltung& l : schaltungen) {
        if (l.ms <= ms) { bis.push_back(&l); }
    }
    std::stable_sort(bis.begin(), bis.end(), [](const LichtSchaltung* a, const LichtSchaltung* b) { return a->ms < b->ms; });
    for (const LichtSchaltung* l : bis) { zustand(l->stil, l->an, l->musterVon); }
    return g;
}

}  // namespace bhed

// --- Paket bewegung (rc601, teil 3): moverState fuer die Wegsuche -------------
namespace bhed {
int MoverSim::tuerZustand(int modell, double ms, bool* verriegelt) const {
    const auto it = tuerWechsel_.find(modell);
    if (it == tuerWechsel_.end() || it->second.empty()) {
        if (verriegelt != nullptr) { *verriegelt = false; }
        return -1;
    }
    const TuerWechsel* w = &it->second.front();
    for (const TuerWechsel& x : it->second) {
        if (x.ms <= ms + 1e-6) { w = &x; }
    }
    if (verriegelt != nullptr) { *verriegelt = w->verriegelt; }
    return w->zustand;
}
}  // namespace bhed

// --- Paket bewegung (rc601, teil 3): pos1/pos2 einer func_door ----------------
namespace bhed {
bool tuerStellungen(const MapEntity& e, const BspGeometry& geo, float pos1[3], float pos2[3]) {
    if (e.classname != "func_door") { return false; }
    const int modell = modellVon(e);
    if (modell <= 0 || static_cast<std::size_t>(modell) >= geo.models.size()) { return false; }
    const auto& sm = geo.models[static_cast<std::size_t>(modell)];
    // SP_func_door (g_mover.cpp:1553-1566 MD): pos1 = s.origin, pos2 = pos1 +
    // movedir * (DotProduct(abs_movedir, size) - lip); START_OPEN vertauscht.
    float o[3] = {0.0F, 0.0F, 0.0F};
    if (!e.origin.empty()) { vektor(e.origin, o); }
    float ang[3];
    winkelAus(e, ang);
    float dir[3];
    bewegungsRichtung(ang, dir);
    const float lip = zahl(e, "lip", 8.0F);
    const float weite = std::fabs(dir[0]) * (sm.maxs[0] - sm.mins[0]) + std::fabs(dir[1]) * (sm.maxs[1] - sm.mins[1]) +
                        std::fabs(dir[2]) * (sm.maxs[2] - sm.mins[2]) - lip;
    for (int k = 0; k < 3; ++k) {
        pos1[k] = o[k];
        pos2[k] = o[k] + dir[k] * weite;
    }
    if ((ganz(e, "spawnflags") & 1) != 0) {
        for (int k = 0; k < 3; ++k) { std::swap(pos1[k], pos2[k]); }
    }
    return true;
}
}  // namespace bhed

// =====================================================================
// rc604: misc_trip_mine - laserTrapThink, WP_Explode, misc_trip_mine_activate
// =====================================================================
namespace bhed {

LaserfalleErgebnis laserfalleLauf(const LaserfalleEingabe& e, const LaserfalleSpurFn& spur) {
    LaserfalleErgebnis r;
    constexpr double kFrametime = 100.0;   // FRAMETIME (g_local.h)
    constexpr double kUnendlich = 1.0e300;
    enum class Denken : std::uint8_t { Nichts, Falle, Frei };
    Denken denken = Denken::Nichts;
    double naechstes = kUnendlich;   // nextthink
    bool feuert = false;             // EF_FIRING
    if (e.sichtbarAbSpawn) { r.sichtbarAbMs = e.spawnMs; }
    // SP_misc_trip_mine (g_misc.cpp:1854-1857 OpenJK): an ohne targetname oder
    // mit START_ON -> misc_trip_mine_activate: think ein FRAMETIME spaeter.
    if (e.anAbSpawn) {
        denken = Denken::Falle;
        naechstes = e.spawnMs + kFrametime;
    }
    const auto schliesse = [&r](double t) {
        if (!r.strahlen.empty() && r.strahlen.back().bisMs < 0.0) { r.strahlen.back().bisMs = t; }
        if (!r.summen.empty() && r.summen.back().second < 0.0) { r.summen.back().second = t; }
    };
    std::size_t ui = 0;
    const float mn[3] = {-4.0F, -4.0F, -4.0F};
    const float mx[3] = {4.0F, 4.0F, 4.0F};
    for (int grenze = 0; grenze < 1000000; ++grenze) {
        while (ui < e.uses.size() && (!e.hatUse || e.uses[ui] < e.spawnMs)) { ++ui; }
        const double tUse = ui < e.uses.size() ? e.uses[ui] : kUnendlich;
        const double tDenk = denken == Denken::Nichts ? kUnendlich : naechstes;
        if (std::min(tUse, tDenk) > e.endeMs) { break; }
        if (tUse < tDenk) {
            ++ui;
            if (denken == Denken::Falle) {
                // misc_trip_mine_activate, Zweig "aus" (g_misc.cpp:1801-1807):
                // EF_FIRING und loopSound weg, kein think mehr.
                feuert = false;
                schliesse(tUse);
                denken = Denken::Nichts;
                naechstes = kUnendlich;
            } else {
                // Zweig "an" (g_misc.cpp:1808-1816): think = laserTrapThink in
                // FRAMETIME, EF_NODRAW weg. (Auch in den 50 ms nach WP_Explode:
                // das think G_FreeEntity wird dann ersetzt.)
                denken = Denken::Falle;
                naechstes = tUse + kFrametime;
                if (r.sichtbarAbMs >= 1.0e299) { r.sichtbarAbMs = tUse; }
            }
            continue;
        }
        const double t = tDenk;
        if (denken == Denken::Frei) {
            r.wegMs = t;   // G_FreeEntity (WP_Explode, g_weapon.cpp:228-229)
            break;
        }
        // laserTrapThink (wp_trip_mine.cpp:130-175 OpenJK)
        if (!feuert) {
            r.scharfMs.push_back(t);
            r.summen.emplace_back(t, -1.0);
            feuert = true;
        }
        naechstes = t + kFrametime;
        float ende[3];
        for (int q = 0; q < 3; ++q) { ende[q] = e.ort[q] + 2048.0F * e.richtung[q]; }
        const LaserfalleSpur s = spur(t, e.origin2, mn, mx, ende);
        if (s.figur || s.startsolid) {
            // WP_Explode: loopSound 0, Effekt, think G_FreeEntity in 50 ms;
            // EF_FIRING weg - der Strahl dieses Bildes wird nicht gezeichnet.
            r.explosionen.push_back(t);
            schliesse(t);
            feuert = false;
            denken = Denken::Frei;
            naechstes = t + 50.0;
            continue;
        }
        // pos4 = tr.endpos: das Ende des Strahls bis zum naechsten Denken.
        if (!r.strahlen.empty() && r.strahlen.back().bisMs < 0.0) {
            const LaserfalleStrahl& alt = r.strahlen.back();
            if (alt.ende[0] == s.endpos[0] && alt.ende[1] == s.endpos[1] && alt.ende[2] == s.endpos[2]) { continue; }
            r.strahlen.back().bisMs = t;
        }
        LaserfalleStrahl neu;
        neu.abMs = t;
        for (int q = 0; q < 3; ++q) { neu.ende[q] = s.endpos[q]; }
        r.strahlen.push_back(neu);
    }
    return r;
}

KastenSpur figurKastenSpur(const float start[3], const float ende[3], const float mins[3], const float maxs[3],
                      const float kastenMin[3], const float kastenMax[3]) {
    // CM_TraceThroughBrush (code/qcommon/cm_trace.cpp) mit den sechs
    // achsparallelen Seiten des Kastens (CM_TempBoxModel); dist -
    // DotProduct(offsets[signbits], normal) weitet jede Seite um den Kasten
    // der Spur.
    constexpr float kEpsilon = 0.125F;   // SURFACE_CLIP_EPSILON
    KastenSpur r;
    float enterFrac = -1.0F;
    float leaveFrac = 1.0F;
    bool getout = false;
    bool startout = false;
    for (int seite = 0; seite < 6; ++seite) {
        const int achse = seite >> 1;
        const bool positiv = (seite & 1) == 0;
        // +Achse: normal +1, dist = kastenMax, offsets[signbits] = mins;
        // -Achse: normal -1, dist = -kastenMin, offsets = maxs.
        const float dist = positiv ? kastenMax[achse] - mins[achse] : -kastenMin[achse] + maxs[achse];
        const float d1 = (positiv ? start[achse] : -start[achse]) - dist;
        const float d2 = (positiv ? ende[achse] : -ende[achse]) - dist;
        if (d2 > 0.0F) { getout = true; }
        if (d1 > 0.0F) { startout = true; }
        if (d1 > 0.0F && (d2 >= kEpsilon || d2 >= d1)) { return r; }
        if (d1 <= 0.0F && d2 <= 0.0F) { continue; }
        if (d1 > d2) {
            float f = (d1 - kEpsilon) / (d1 - d2);
            if (f < 0.0F) { f = 0.0F; }
            if (f > enterFrac) {
                enterFrac = f;
                for (float& n : r.normale) { n = 0.0F; }
                r.normale[achse] = positiv ? 1.0F : -1.0F;   // clipplane
            }
        } else {
            float f = (d1 + kEpsilon) / (d1 - d2);
            if (f > 1.0F) { f = 1.0F; }
            if (f < leaveFrac) { leaveFrac = f; }
        }
    }
    if (!startout) {
        for (float& n : r.normale) { n = 0.0F; }
        r.startsolid = true;
        if (!getout) {
            r.allsolid = true;
            r.fraction = 0.0F;
        }
        return r;
    }
    if (enterFrac < leaveFrac && enterFrac > -1.0F && enterFrac < r.fraction) {
        r.fraction = enterFrac < 0.0F ? 0.0F : enterFrac;
    } else {
        for (float& n : r.normale) { n = 0.0F; }
    }
    return r;
}

bool g2RadiusTreffer(const std::vector<float>& ecken, const std::vector<std::uint32_t>& idx, const float start[3],
                     const float ende[3], float radius) {
    // G2_RadiusTracePolys (code/rd-vanilla/G2_misc.cpp:1254-1460 OpenJK),
    // woertlich: zwei Achsen quer zum Strahl (basis1/basis2, theta 0), s/t/u
    // je Ecke, sechs Seitenbits; die ganze Flaeche faellt weg, wenn eine Seite
    // alle Ecken ausschliesst, ein Dreieck trifft, wenn keine Seite alle drei
    // seiner Ecken ausschliesst.
    const auto kreuz = [](const float a[3], const float b[3], float o[3]) {
        o[0] = a[1] * b[2] - a[2] * b[1];
        o[1] = a[2] * b[0] - a[0] * b[2];
        o[2] = a[0] * b[1] - a[1] * b[0];
    };
    const auto punkt = [](const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const auto normiere = [&punkt](float v[3]) {
        const float l = std::sqrt(punkt(v, v));
        if (l != 0.0F) {
            const float il = 1.0F / l;
            for (int q = 0; q < 3; ++q) { v[q] *= il; }
        }
    };
    if (radius == 0.0F || ecken.size() < 3U) { return false; }
    float basis1[3];
    float basis2[3] = {0.0F, 0.0F, 1.0F};
    float dir[3] = {ende[0] - start[0], ende[1] - start[1], ende[2] - start[2]};
    kreuz(dir, basis2, basis1);
    if (punkt(basis1, basis1) < 0.1F) {
        basis2[0] = 0.0F;
        basis2[1] = 1.0F;
        basis2[2] = 0.0F;
        kreuz(dir, basis2, basis1);
    }
    kreuz(dir, basis1, basis2);
    normiere(basis1);
    normiere(basis2);
    const float c = std::cos(0.0F);
    const float sn = std::sin(0.0F);
    float taxis[3];
    float saxis[3];
    for (int q = 0; q < 3; ++q) {
        taxis[q] = basis1[q] * (0.5F * c / radius) + basis2[q] * (0.5F * sn / radius);
        saxis[q] = basis1[q] * (-0.5F * sn / radius) + basis2[q] * (0.5F * c / radius);
    }
    const float f = punkt(dir, dir);
    if (f == 0.0F) { return false; }
    for (float& d : dir) { d /= f; }
    const std::size_t n = ecken.size() / 3U;
    std::vector<int> eckBits(n, 0);
    int flags = 63;
    for (std::size_t j = 0; j < n; ++j) {
        const float delta[3] = {ecken[j * 3] - start[0], ecken[j * 3 + 1] - start[1], ecken[j * 3 + 2] - start[2]};
        const float s = punkt(delta, saxis) + 0.5F;
        const float t = punkt(delta, taxis) + 0.5F;
        const float u = punkt(delta, dir);
        int v = 0;
        if (s > 0.0F) { v |= 1; }
        if (s < 1.0F) { v |= 2; }
        if (t > 0.0F) { v |= 4; }
        if (t < 1.0F) { v |= 8; }
        if (u > 0.0F) { v |= 16; }
        if (u < 1.0F) { v |= 32; }
        v = ~v;
        flags &= v;
        eckBits[j] = v;
    }
    if (flags != 0) { return false; }
    for (std::size_t j = 0; j + 2 < idx.size(); j += 3) {
        if (idx[j] >= n || idx[j + 1] >= n || idx[j + 2] >= n) { continue; }
        if ((63 & eckBits[idx[j]] & eckBits[idx[j + 1]] & eckBits[idx[j + 2]]) == 0) { return true; }
    }
    return false;
}

}  // namespace bhed

// =====================================================================
// rc604: Knochenanimation der Geschuetze, ion_cannon_think, pas_think
// =====================================================================
namespace bhed {

void g2Bild(const KnochenAnim& a, double ms, int& bild, int& naechstes, float& lerp) {
    // G2_TimingModel (code/rd-vanilla/tr_ghoul2.cpp:951-1150 OpenJK) fuer
    // animSpeed > 0 ohne Schleife: BONE_ANIM_OVERRIDE_FREEZE haelt am Ende
    // endFrame - 1.
    float zeit = static_cast<float>((ms - a.startMs) / 50.0);
    if (zeit < 0.0F) { zeit = 0.0F; }
    const float neu = static_cast<float>(a.startFrame) + zeit * a.speed;
    const float ende = static_cast<float>(a.endFrame);
    if (a.endFrame - a.startFrame != 0 && a.speed > 0.0F && neu > ende - 1.0F) {
        bild = a.endFrame - 1;
        naechstes = bild;
        lerp = 0.0F;
        return;
    }
    bild = static_cast<int>(neu);
    lerp = neu - static_cast<float>(bild);
    naechstes = bild + 1;
    if (naechstes >= a.endFrame) { naechstes = a.endFrame - 1; }
}

namespace {
// Q_flrand(-1, 1) / Q_irand(lo, hi) der Vorschau: stabil aus einer Saat (die
// Folge des Spiels haengt an seinem Zufallsgenerator und ist nicht
// nachzurechnen).
float flrandMinusEins(std::uint64_t saat, int n) {
    std::uint64_t h = saat ^ (0x9e3779b97f4a7c15ULL * static_cast<std::uint64_t>(n + 1));
    h ^= h >> 33U;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33U;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33U;
    return static_cast<float>(static_cast<double>(h >> 11U) / 9007199254740992.0) * 2.0F - 1.0F;
}
int irand(std::uint64_t saat, int n, int lo, int hi) {
    const float f = (flrandMinusEins(saat, n) + 1.0F) * 0.5F;
    int r = lo + static_cast<int>(f * static_cast<float>(hi - lo + 1));
    if (r > hi) { r = hi; }
    return r;
}
}  // namespace

IonenkanoneErgebnis ionenkanoneLauf(const IonenkanoneEingabe& e) {
    // SP_misc_ion_cannon (code/game/g_turret.cpp:2064-2130 OpenJK): wait 1500
    // (mindestens 500), random 400, delay 6000 (mindestens 1000); ohne
    // START_OFF denkt sie ab spawn + wait + Q_flrand(-1,1) * random, BURSTS:
    // count = Q_irand(0,5). ion_cannon_use (:2028-2041): ohne think -> think in
    // FRAMETIME ("fires right on being used"), sonst think weg.
    // ion_cannon_think (:1930-1976): BURSTS ohne count -> nextthink = delay +
    // Q_flrand * random, count neu, kein Schuss; sonst count--, Schuss
    // (G_PlayEffect "env/ion_cannon" am Bolzen *flash02, G_UseTargets2),
    // Animation 0..8 mit 0.6 (BONE_ANIM_OVERRIDE_FREEZE), nextthink = wait +
    // Q_flrand * random.
    IonenkanoneErgebnis r;
    constexpr double kUnendlich = 1.0e300;
    float wait = e.wait == 0.0F ? 1500.0F : (e.wait < 500.0F ? 500.0F : e.wait);
    const float random = e.random == 0.0F ? 400.0F : e.random;
    float delay = e.delay == 0.0F ? 6000.0F : (e.delay < 1000.0F ? 1000.0F : e.delay);
    int wurf = 0;
    bool denkt = !e.startOff;
    double naechstes = denkt ? e.spawnMs + wait + flrandMinusEins(e.saat, wurf++) * random : kUnendlich;
    int count = e.bursts ? irand(e.saat, wurf++, 0, 5) : 0;
    std::size_t ui = 0;
    for (int grenze = 0; grenze < 1000000; ++grenze) {
        while (ui < e.uses.size() && e.uses[ui] < e.spawnMs) { ++ui; }
        const double tUse = ui < e.uses.size() ? e.uses[ui] : kUnendlich;
        const double tDenk = denkt ? naechstes : kUnendlich;
        if (std::min(tUse, tDenk) > e.endeMs) { break; }
        if (tUse < tDenk) {
            ++ui;
            if (!denkt) {
                denkt = true;
                naechstes = tUse + 100.0;   // FRAMETIME
            } else {
                denkt = false;
            }
            continue;
        }
        const double t = tDenk;
        if (e.bursts) {
            if (count != 0) {
                --count;
            } else {
                naechstes = t + delay + flrandMinusEins(e.saat, wurf++) * random;
                count = irand(e.saat, wurf++, 0, 5);
                continue;
            }
        }
        r.schuesse.push_back(t);
        naechstes = t + wait + flrandMinusEins(e.saat, wurf++) * random;
    }
    (void)delay;
    (void)wait;
    return r;
}

double pasAufklappenMs(bool startOff, double spawnMs, const std::vector<double>& uses, double endeMs) {
    // SP_PAS (code/game/g_turret.cpp:1784-1890 OpenJK): ohne START_OFF denkt
    // pas_think ab spawn + 1000. pas_use (:1395-1411) kippt spawnflags & 1: an
    // -> think in 50 ms, aus -> kein think. Das erste pas_think (damage 0)
    // klappt auf: G2API_SetBoneAnimIndex(rootBone, 0, 11, FREEZE, 0.8).
    bool aus = startOff;
    double denken = aus ? 1.0e300 : spawnMs + 1000.0;
    for (const double u : uses) {
        if (u < spawnMs) { continue; }
        if (u > endeMs) { break; }
        if (denken <= u) { break; }   // schon gedacht -> aufgeklappt
        aus = !aus;
        denken = aus ? 1.0e300 : u + 50.0;
    }
    return denken <= endeMs ? denken : -1.0;
}

std::vector<PasSchwenk> pasSchwenkLauf(bool startOff, double spawnMs, const std::vector<double>& uses, double endeMs,
                                       float gier, int count) {
    // AngleNormalize360 (q_math.cpp): auf 65536 Stufen.
    const auto norm360 = [](float a) {
        return static_cast<float>((360.0 / 65536) * (static_cast<int>(a * (65536 / 360.0)) & 65535));
    };
    std::vector<PasSchwenk> r;
    bool aus = startOff;
    bool aufgeklappt = false;   // ent->damage
    double denken = aus ? 1.0e300 : spawnMs + 1000.0;
    float speed = gier;         // SP_PAS: base->speed = base->s.angles[YAW]
    std::size_t naechsterUse = 0;
    while (true) {
        while (naechsterUse < uses.size() && uses[naechsterUse] < spawnMs) { ++naechsterUse; }
        const bool useZuerst = naechsterUse < uses.size() && uses[naechsterUse] <= endeMs && uses[naechsterUse] < denken;
        if (useZuerst) {
            // pas_use (g_turret.cpp:1395-1411 OpenJK): spawnflags ^= 1; aus ->
            // nextthink 0, an -> level.time + 50.
            const double u = uses[naechsterUse++];
            aus = !aus;
            denken = aus ? 1.0e300 : u + 50.0;
            continue;
        }
        if (denken > endeMs) { break; }
        if (!aufgeklappt) {
            // "let us do our animation": damage = 1, nextthink + 1200.
            aufgeklappt = true;
            denken += 1200.0;
            continue;
        }
        // count > 0: ohne Feind schiesst es nie (count-- nur mit ent->enemy).
        speed = norm360(speed);
        const auto levelZeit = static_cast<int>(std::lround(denken - spawnMs + 1000.0));
        // "no enemy, so make us slowly sweep back and forth": sin() der C-Bibliothek (double).
        const auto diff = static_cast<float>(std::sin(static_cast<float>(levelZeit) * 0.0001F + static_cast<float>(count)) * 2.0F);
        if (std::fabs(diff) > 0.25F) {
            if (std::fabs(diff) > 10.0F) {
                speed += diff > 0.0F ? -10.0F : 10.0F;
            } else {
                speed -= diff;
            }
        }
        // back_angles = (0, 0, speed - s.angles[YAW]) fuer bone_hinge; front_angles
        // (-random, 0, 0) bleiben ohne Feind 0 (random wird nie gesetzt).
        r.push_back(PasSchwenk{denken, speed - gier});
        denken += 100.0;   // FRAMETIME
    }
    return r;
}

}  // namespace bhed

// =====================================================================
// rc604: fx_target_beam - link, use, think, Abklingzeit (g_fx.cpp OpenJK)
// =====================================================================
namespace bhed {

std::vector<double> zielStrahlFeuer(const ZielStrahlEingabe& e) {
    // SP_fx_target_beam (code/game/g_fx.cpp:1186-1225 OpenJK): wait/random in
    // Sekunden * 1000, think fx_target_beam_link nach START_TIME_LINK_ENTS.
    // fx_target_beam_link (:1124-1167): attackDebounceTime = level.time,
    // e_UseFunc erst jetzt (ein use davor trifft keine use-Funktion);
    // STARTOFF: kein think, sonst ohne ONE_SHOT think in FRAMETIME.
    // fx_target_beam_think (:1112-1121): solange attackDebounceTime > level.time
    // in FRAMETIME wieder, sonst fx_target_beam_fire_start (:1081-1088):
    // fx_target_beam_set_debounce (:994-1008: wait >= FRAMETIME -> level.time +
    // wait + Q_irand(-random, random), wait < 0 -> e_UseFunc = NULL, sonst
    // FRAMETIME + Q_irand), think in FRAMETIME, fx_target_beam_fire.
    // fx_target_beam_use (:1091-1109): ONE_SHOT feuert sofort und nimmt das
    // think weg; ohne think -> think in 50 ms; sonst think weg.
    std::vector<double> feuer;
    constexpr double kFrametime = 100.0;
    constexpr double kUnendlich = 1.0e300;
    const double linkMs = e.spawnMs + kFrametime;   // START_TIME_LINK_ENTS = FRAMETIME * 1
    if (linkMs > e.endeMs || !e.zielDa) { return feuer; }
    double abkling = linkMs;   // attackDebounceTime
    bool denkt = false;
    double naechstes = kUnendlich;
    bool useDa = true;
    if (!e.startOff && !e.oneShot) {
        denkt = true;
        naechstes = linkMs + kFrametime;
    }
    std::size_t ui = 0;
    int wurf = 0;
    const auto irand = [&e, &wurf](float r) {
        // Q_irand(-random, random): stabil aus der Saat (siehe ionenkanoneLauf).
        if (r <= 0.0F) { return 0.0; }
        std::uint64_t h = e.saat ^ (0x9e3779b97f4a7c15ULL * static_cast<std::uint64_t>(++wurf));
        h ^= h >> 31U;
        h *= 0xbf58476d1ce4e5b9ULL;
        h ^= h >> 27U;
        const int lo = -static_cast<int>(r);
        const int hi = static_cast<int>(r);
        return static_cast<double>(lo + static_cast<int>(h % static_cast<std::uint64_t>(hi - lo + 1)));
    };
    for (int grenze = 0; grenze < 1000000; ++grenze) {
        while (ui < e.uses.size() && e.uses[ui] < linkMs) { ++ui; }   // vor dem link: keine use-Funktion
        const double tUse = ui < e.uses.size() ? e.uses[ui] : kUnendlich;
        const double tDenk = denkt ? naechstes : kUnendlich;
        if (std::min(tUse, tDenk) > e.endeMs) { break; }
        if (tUse < tDenk) {
            ++ui;
            if (!useDa) { continue; }
            if (e.oneShot) {
                feuer.push_back(tUse);   // fx_target_beam_fire, danach thinkF_NULL
                denkt = false;
            } else if (!denkt) {
                denkt = true;
                naechstes = tUse + 50.0;
            } else {
                denkt = false;
            }
            continue;
        }
        const double t = tDenk;
        if (abkling > t) {
            naechstes = t + kFrametime;
            continue;
        }
        // fx_target_beam_fire_start
        if (e.waitMs >= kFrametime) {
            abkling = t + e.waitMs + irand(e.randomMs);
        } else if (e.waitMs < 0.0F) {
            useDa = false;
        } else {
            abkling = t + kFrametime + irand(e.randomMs);
        }
        naechstes = t + kFrametime;
        feuer.push_back(t);
    }
    return feuer;
}

}  // namespace bhed
