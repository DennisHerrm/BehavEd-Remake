#include "bhed/efx/sim.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bhed::efx::sim {

float Random::next() {
    // Eigener Generator statt std::rand: der ist zwischen Laufzeitbibliotheken
    // verschieden, und eine Vorschau soll auf jedem Rechner gleich aussehen.
    state_ = state_ * 1664525U + 1013904223U;
    return static_cast<float>((state_ >> 8) & 0xFFFFFF) /
           static_cast<float>(0x1000000);
}

float Random::range(float low, float high) { return low + (high - low) * next(); }

float Random::pick(const Range& r) {
    if (!r.set) { return 0.0F;
}
    if (!r.ranged || r.min == r.max) { return r.min;
}
    return range(std::min(r.min, r.max), std::max(r.min, r.max));
}

// Zum `lateTime`-Ausgleich der Engine, den wir BEWUSST nicht nachbauen.
//
// FxScheduler.cpp, in CreateEffect:
//
//     if ( lateTime > 0 ) {
//         float ftime = lateTime * 0.001f;
//         float time2 = ftime * ftime * 0.5f;
//         VectorMA( vel, ftime, accel, vel );
//         for ( int i = 0 ; i < 3 ; i++ )
//             org[i] = org[i] + ftime * vel[i] + time2 * vel[i];
//     }
//
// `lateTime` ist `theFxHelper.mTime - effect->mStartTime`: der Rueckstand
// zum Bildraster. Ein geplanter Effekt startet im Spiel erst im ersten Bild
// NACH seinem Zeitpunkt, also bis zu einer Bilddauer zu spaet, und die Engine
// rueckt ihn entlang seiner Bahn vor.
//
// Bei uns ist der Rueckstand immer null: wir rechnen die Lage zum exakten
// Zeitpunkt, nicht zum naechsten Bild. Den Ausgleich nachzubauen hiesse, erst
// ein Bildraster einzufuehren - und damit waere die Vorschau wieder von der
// Bildrate abhaengig, genau das, was wir an mehreren Stellen vermieden haben.
//
// Nebenbei: die Zeile ist fehlerhaft. `time2 * vel[i]` muesste `time2 *
// accel[i]` heissen - sonst geht die Geschwindigkeit zweimal ein und die
// Beschleunigung gar nicht. Wirksam wird das ueber hoechstens eine Bilddauer,
// also wenige Millimeter. Ein Grund mehr, es zu lassen.
std::vector<Spawn> schedule(const Effect& effect, Random& random,
                            const std::vector<bool>& enabledMask) {
    std::vector<Spawn> spawns;

    // Hoechstens 24 Segmente - mehr nimmt die Engine nicht an.
    //
    //     #define FX_MAX_EFFECT_COMPONENTS 24
    //
    //     void CFxScheduler::AddPrimitiveToEffect( ... ) {
    //         if ( ct >= FX_MAX_EFFECT_COMPONENTS ) {
    //             theFxHelper.Print( "FxScheduler:  Error--too many primitives" );
    //         } else { ... }
    //     }
    //
    // Die ueberzaehligen werden beim Laden STILL verworfen - der Effekt
    // startet trotzdem, nur ohne sie. Wer eine Datei mit dreissig Segmenten
    // schreibt, sieht im Spiel die ersten vierundzwanzig.
    //
    // Wir haben alle dreissig simuliert. Die Vorschau zeigte damit mehr als
    // das Spiel, und zwar ohne Hinweis - genau der Fall, in dem man den
    // Fehler zuletzt beim Editor sucht. (Gemeldet wird er von den
    // Pruefregeln, aber gemeldet heisst nicht gezeigt.)
    constexpr size_t kMaxComponents = 24;
    const size_t componentCount =
        std::min(effect.primitives.size(), kMaxComponents);

    for (size_t i = 0; i < componentCount; ++i) {
        const Primitive& p = effect.primitives[i];
        if (i < enabledMask.size() && !enabledMask[i]) { continue;
}

        // Anzahl. Nicht gesetzt heisst einmal - so verhaelt sich die Engine,
        // wo mSpawnCount mit 1 vorbelegt ist.
        int count = 1;
        if (p.count.set) {
            // clang-tidy schlaegt lround vor, und im Allgemeinen zu
            // Recht: (int)(x + 0.5) rundet bei NEGATIVEN Werten falsch.
            //
            // Hier steht es trotzdem so, weil die Engine es so tut -
            // FxScheduler.h, CFxRange::GetRoundedVal():
            //
            //     return (int)(flrand(mMin, mMax) + 0.5f);
            //
            // Eine Anzahl ist nie negativ, der Unterschied kann also gar
            // nicht auftreten. Und dieser Leser soll dieselben Zahlen
            // liefern wie das Spiel, nicht die mathematisch schoeneren.
            //
            // NOLINTNEXTLINE(bugprone-incorrect-roundings)
            count = static_cast<int>(random.pick(p.count) + 0.5F);
        }
        // `count 0` erzeugt NICHTS - GetRoundedVal() ohne Untergrenze
        // (FxScheduler.cpp:1175). Hier stand eine Eins als Mindestwert.
        if (count < 0) {
            count = 0;
        }
        // FX_MAX_EFFECT_COMPONENTS begrenzt die Segmente, nicht die Anzahl je
        // Segment - aber eine Datei mit count 100000 wuerde die Vorschau
        // aufhaengen, und das hilft niemandem beim Bearbeiten.
        constexpr int kMaxPerSegment = 4096;
        if (count > kMaxPerSegment) { count = kMaxPerSegment;
}

        // Gleichmaessige Verteilung: die Spanne wird in count Schritte geteilt.
        //   factor = |max - min| / count;  delay = t * factor
        // Bemerkenswert: die Engine benutzt dabei NICHT den Minimalwert als
        // Versatz - sie beginnt bei null. Bei delay 200..400 und count 4
        // ergibt das 0, 50, 100, 150, nicht 200, 250, 300, 350.
        const bool even = (p.spawnFlags & kSpawnEvenDistribution) != 0;
        const float span = std::fabs(p.delay.max - p.delay.min);
        const float factor = count > 0 ? span / static_cast<float>(count) : 0.0F;

        for (int t = 0; t < count; ++t) {
            Spawn spawn;
            spawn.primitiveIndex = static_cast<int>(i);
            spawn.timeMs = even ? static_cast<float>(t) * factor
                                : random.pick(p.delay);
            if (spawn.timeMs < 0.0F) { spawn.timeMs = 0.0F;
}
            spawns.push_back(spawn);
        }
    }

    std::stable_sort(spawns.begin(), spawns.end(),
                     [](const Spawn& a, const Spawn& b) {
                         return a.timeMs < b.timeMs;
                     });
    return spawns;
}

namespace {

// Feste Schrittweite statt der Bildzeit.
//
// Die Engine rechnet mit `mFloatFrameTime` - dann haengt die Bahn davon ab,
// wie schnell der Rechner gerade ist. Fuer ein Vorschauwerkzeug waere das
// falsch: dasselbe Alter muss dasselbe Bild geben, sonst zittert der Funke,
// sobald die Bildrate schwankt.
//
// Zwei Millisekunden - fein genug, dass ein Partikel bei
// Blastergeschwindigkeit (2300 Einheiten je Sekunde) je Schritt gut vier
// Einheiten zuruecklegt.
constexpr float kSchrittS = 0.002F;
// Und eine Obergrenze fuer die Schritte. Bis rc569 lag sie bei tausend
// (zwei Sekunden), danach blieb das Teilchen einfach STEHEN - ein Nebel-
// sprite aus exegol/fogsprites.efx (`gravity -2 -1`, `life 1.6e+004`)
// erreicht den Boden aber erst nach knapp drei Sekunden und hing darueber
// in der Luft. Dreissigtausend Schritte sind sechzig Sekunden, laenger lebt
// kein Teilchen der Mod (das laengste: `life 5.2e+004` in
// sarlacc/smoke_explosion.efx). Bezahlbar ist das, weil ein Aufrufer mit
// FlugZustand nicht jedes Bild von vorn anfaengt.
constexpr int kMaxSchritte = 30000;

// Wie viele VOLLE Schritte passen in `sekunden`? Ganzzahlig, damit jeder
// Aufruf - ob von vorn oder fortgesetzt - dieselben Schritte macht. Die
// Toleranz faengt 0.5 / 0.002 = 249.99998 in float ab.
int volleSchritte(float sekunden) {
    const float n = std::floor(sekunden / kSchrittS + 1e-3F);
    return (n <= 0.0F) ? 0 : static_cast<int>(std::min(n, 2.0e9F));
}

// Der Stand nach vollen Schritten - wie FlugZustand, aber ohne Spurbuch,
// damit das Kopieren je Aufruf billig bleibt.
struct Stand {
    camera::Vec3 p;
    camera::Vec3 v;
    camera::Vec3 a;
    int schritte = 0;
    FlugErgebnis aus;
};

// Der gemeinsame Kern von `flugbahn`, `flugbahnWeiter` und
// `flugbahnAbtasten`.
//
// EIN Schrittgeber fuer alle, damit die Fahne eines Emitters genau dort
// liegt, wo `flugbahn` den Emitter selbst hinstellt. Zwei Kopien derselben
// Schleife laufen beim naechsten Eingriff auseinander.
//
// `jePunkt` darf leer sein, `merk` auch; ohne beides ist es genau
// `flugbahn`. Mit `merk` kommen die Spurtests der vollen Schritte aus dem
// Spurbuch, soweit es reicht (siehe FlugZustand in sim.h), und was neu
// gespurt wird, kommt hinein.
FlugErgebnis schritte(
    const camera::Vec3& origin, const camera::Vec3& velocity,
    const camera::Vec3& acceleration, float gravity, float secondsSinceSpawn,
    float elasticity, bool killOnImpact, const TraceFn& trace,
    float abstandSekunde,
    const std::function<bool(float, const camera::Vec3&)>* jePunkt,
    FlugZustand* merk) {
    const bool abtasten = jePunkt != nullptr && abstandSekunde > 0.0F;
    float naechsterPunkt = 0.0F;
    bool weiter = true;
    // Meldet alle Abtastpunkte bis einschliesslich `bis` mit der Lage `p`.
    // Faellt `weiter` auf false, will der Aufrufer nichts mehr.
    auto melde = [&](float bis, const camera::Vec3& p) {
        while (abtasten && weiter && naechsterPunkt <= bis + 1e-6F) {
            weiter = (*jePunkt)(naechsterPunkt, p);
            naechsterPunkt += abstandSekunde;
        }
    };
    if (secondsSinceSpawn <= 0.0F) {
        FlugErgebnis aus;
        aus.position = origin;
        melde(0.0F, origin);
        return aus;
    }
    if (!trace) {
        // Ohne Welt gibt es keine Wand - dann ist es die freie Bahn.
        FlugErgebnis aus;
        while (abtasten && weiter && naechsterPunkt <= secondsSinceSpawn + 1e-6F) {
            weiter = (*jePunkt)(naechsterPunkt,
                                positionAt(origin, velocity, acceleration,
                                           gravity, naechsterPunkt));
            naechsterPunkt += abstandSekunde;
        }
        aus.position = positionAt(origin, velocity, acceleration, gravity,
                                  secondsSinceSpawn);
        return aus;
    }
    if (merk != nullptr && !merk->gueltig) {
        *merk = FlugZustand{};   // ein leeres Buch
    }

    // Der Spurtest des vollen Schritts `s` (ab 1): aus dem Buch, solange es
    // reicht - dort steht, was trace fuer GENAU diese Strecke ergab, denn
    // ein Wiederholungslauf kommt mit denselben Bits hier an. Danach echt,
    // und ins Buch. Neu gespurt wird immer der Schritt bekannt+1: jeder Lauf
    // beginnt an einem Stand, der hoechstens `bekannt` weit ist.
    std::size_t buchPos = 0;
    auto spurVoll = [&](int s, const camera::Vec3& von, const camera::Vec3& bis,
                        camera::Vec3& punkt, camera::Vec3& normale) {
        if (merk != nullptr && s <= merk->bekannt) {
            const auto& b = merk->buch;
            while (buchPos < b.size() && b[buchPos].schritt < s) {
                ++buchPos;
            }
            if (buchPos < b.size() && b[buchPos].schritt == s) {
                punkt = b[buchPos].punkt;
                normale = b[buchPos].normale;
                return true;
            }
            return false;
        }
        const bool traf = trace(von, bis, punkt, normale);
        if (merk != nullptr) {
            if (traf) {
                merk->buch.push_back(FlugZustand::Treffer{s, punkt, normale});
            }
            merk->bekannt = s;
        }
        return traf;
    };

    // Ein Schritt: erst die Geschwindigkeit, dann der Ort - wie
    // UpdateVelocity() und UpdateOrigin() in FxPrimitives.cpp:254 ff.
    // `bisher` ist die Zeit NACH dem Schritt, `s` die Nummer des vollen
    // Schritts (0: der angeschnittene letzte - der geht nicht ins Buch).
    auto schritt = [&](Stand& z, float dt, float bisher, int s) {
        z.v.x += z.a.x * dt;
        z.v.y += z.a.y * dt;
        z.v.z += z.a.z * dt;
        const camera::Vec3 ziel{z.p.x + z.v.x * dt, z.p.y + z.v.y * dt,
                                z.p.z + z.v.z * dt};
        camera::Vec3 punkt{};
        camera::Vec3 normale{};
        FlugErgebnis& aus = z.aus;
        const bool getroffen = (s > 0) ? spurVoll(s, z.p, ziel, punkt, normale)
                                       : trace(z.p, ziel, punkt, normale);
        if (!getroffen) {
            z.p = ziel;
            return;
        }
        // Die Zeit aus der ZAHL der Schritte, nicht als Alter minus Rest:
        // so ist sie fuer jedes Alter, das diesen Schritt voll enthaelt,
        // bitgleich - und das Ergebnis laesst sich fortsetzen und merken.
        if (!aus.traf) {
            aus.traf = true;
            aus.trefferPunkt = punkt;
            aus.trefferNormale = normale;
            aus.trefferSekunde = bisher;
        }
        if (aus.aufpralle < FlugErgebnis::kMaxAufpralle) {
            const auto k = static_cast<std::size_t>(aus.aufpralle);
            aus.aufprallPunkt[k] = punkt;
            aus.aufprallNormale[k] = normale;
            aus.aufprallSekunde[k] = bisher;
            ++aus.aufpralle;
        }
        if (killOnImpact) {
            z.p = punkt;
            aus.gestorben = true;
            if (dt == kSchrittS) {
                aus.endgueltigAb = bisher;
            }
            return;
        }
        ++aus.abpraller;
        // Spiegeln und daempfen - FxPrimitives.cpp:329 ff.
        const float dot =
            z.v.x * normale.x + z.v.y * normale.y + z.v.z * normale.z;
        z.v.x -= 2.0F * dot * normale.x;
        z.v.y -= 2.0F * dot * normale.y;
        z.v.z -= 2.0F * dot * normale.z;
        z.v.x *= elasticity;
        z.v.y *= elasticity;
        z.v.z *= elasticity;
        // Zu langsam auf einem Boden: liegenbleiben. Die Engine schaltet
        // dort die Physik ganz ab (ebenda:337).
        if (normale.z > 0.0F && z.v.z < 4.0F) {
            z.v = camera::Vec3{0.0F, 0.0F, 0.0F};
            z.a = camera::Vec3{0.0F, 0.0F, 0.0F};
            aus.liegt = true;
            aus.liegtSekunde = bisher;
            if (dt == kSchrittS) {
                aus.endgueltigAb = bisher;
            }
        }
        z.p = punkt;
    };

    const int voll = volleSchritte(secondsSinceSpawn);
    // --- Von vorn, fortgesetzt, oder ab einer Marke ---------------------
    //
    // Ein gemerkter Stand gilt, wenn er zu DERSELBEN Bahn gehoert (das
    // prueft der Aufrufer ueber seinen Schluessel) und nicht weiter ist als
    // das verlangte Alter. Dann sind die Schritte bis dorthin schon
    // gemacht, und zwar exakt dieselben. Ist er weiter, gilt die letzte
    // Marke davor. Die Abtastung meldet jeden Punkt und faengt deshalb
    // immer vorn an - nur die Spurtests kommen dann aus dem Buch.
    Stand z;
    auto vonVorn = [&]() {
        z.p = origin;
        z.v = velocity;
        // Die Schwerkraft steckt in der Beschleunigung, genau wie
        // CFxScheduler::CreateEffect es tut: `accel[2] += mGravity`.
        z.a = acceleration;
        z.a.z += gravity;
        z.aus = FlugErgebnis{};
        z.aus.position = origin;
        z.schritte = 0;
    };
    bool angesetzt = false;
    if (merk != nullptr && merk->gueltig && !abtasten) {
        if (merk->schritte <= voll) {
            z.p = merk->p;
            z.v = merk->v;
            z.a = merk->a;
            z.schritte = merk->schritte;
            z.aus = merk->aus;
            angesetzt = true;
        } else {
            // Die letzte Marke bis `voll`. Das Ergebnis bis dorthin setzt
            // sich aus dem Buch zusammen: Marken gibt es nur, solange das
            // Teilchen fliegt, also war jeder Treffer davor ein Abpraller -
            // genau die Buchfuehrung von `schritt` oben.
            const auto& mk = merk->marken;
            auto it = std::upper_bound(
                mk.begin(), mk.end(), voll,
                [](int w, const FlugZustand::Marke& m) { return w < m.schritt; });
            if (it != mk.begin()) {
                const FlugZustand::Marke& m = *(it - 1);
                vonVorn();
                z.p = m.p;
                z.v = m.v;
                z.schritte = m.schritt;
                for (const FlugZustand::Treffer& t : merk->buch) {
                    if (t.schritt > m.schritt) {
                        break;
                    }
                    const float bisher = static_cast<float>(t.schritt) * kSchrittS;
                    FlugErgebnis& aus = z.aus;
                    if (!aus.traf) {
                        aus.traf = true;
                        aus.trefferPunkt = t.punkt;
                        aus.trefferNormale = t.normale;
                        aus.trefferSekunde = bisher;
                    }
                    if (aus.aufpralle < FlugErgebnis::kMaxAufpralle) {
                        const auto k = static_cast<std::size_t>(aus.aufpralle);
                        aus.aufprallPunkt[k] = t.punkt;
                        aus.aufprallNormale[k] = t.normale;
                        aus.aufprallSekunde[k] = bisher;
                        ++aus.aufpralle;
                    }
                    ++aus.abpraller;
                }
                angesetzt = true;
            }
        }
    }
    if (!angesetzt) {
        vonVorn();
        melde(0.0F, z.p);
    }
    const int bis = std::min(voll, kMaxSchritte);
    while (z.schritte < bis && weiter && !z.aus.liegt && !z.aus.gestorben) {
        ++z.schritte;
        const float bisher = static_cast<float>(z.schritte) * kSchrittS;
        schritt(z, kSchrittS, bisher, z.schritte);
        melde(bisher, z.p);
        if (merk != nullptr && z.schritte % kMarkenAbstand == 0 &&
            !z.aus.liegt && !z.aus.gestorben &&
            (merk->marken.empty() || merk->marken.back().schritt < z.schritte)) {
            merk->marken.push_back(FlugZustand::Marke{z.schritte, z.p, z.v});
        }
    }
    if (z.aus.gestorben || z.aus.liegt) {
        // Fertig: fuer jedes spaetere Alter dasselbe.
        z.aus.position = z.p;
    }
    // Den WEITESTEN Stand behalten - nur VOLLE Schritte, der Rest unten
    // auf einer Kopie.
    if (merk != nullptr && (!merk->gueltig || z.schritte > merk->schritte)) {
        merk->p = z.p;
        merk->v = z.v;
        merk->a = z.a;
        merk->schritte = z.schritte;
        merk->aus = z.aus;
        merk->gueltig = true;
    }
    if (z.aus.gestorben || z.aus.liegt) {
        melde(secondsSinceSpawn, z.p);
        return z.aus;
    }
    Stand ende = z;
    if (voll <= kMaxSchritte) {
        // Der angeschnittene letzte Schritt.
        const float rest =
            secondsSinceSpawn - static_cast<float>(voll) * kSchrittS;
        if (rest > 0.0F && weiter) {
            schritt(ende, rest, secondsSinceSpawn, 0);
        }
        melde(secondsSinceSpawn, ende.p);
    } else if (weiter) {
        // --- Ueber die Schrittgrenze hinaus: frei weiter, nicht STEHEN ---
        //
        // Ohne weitere Spurtests ist die freie Bahn ab dem letzten Stand
        // die ehrlichste Fortsetzung. Wer liegt, kommt hier nicht an.
        const camera::Vec3 fest = ende.p;
        const camera::Vec3 v0 = ende.v;
        const float t0 = static_cast<float>(kMaxSchritte) * kSchrittS;
        auto frei = [&](float t) {
            return positionAt(fest, v0, ende.a, 0.0F, std::max(0.0F, t - t0));
        };
        while (abtasten && weiter && naechsterPunkt <= secondsSinceSpawn + 1e-6F) {
            weiter = (*jePunkt)(naechsterPunkt, frei(naechsterPunkt));
            naechsterPunkt += abstandSekunde;
        }
        ende.p = frei(secondsSinceSpawn);
    }
    ende.aus.position = ende.p;
    return ende.aus;
}

}  // namespace

FlugErgebnis flugbahn(const camera::Vec3& origin, const camera::Vec3& velocity,
                      const camera::Vec3& acceleration, float gravity,
                      float secondsSinceSpawn, float elasticity,
                      bool killOnImpact, const TraceFn& trace) {
    return schritte(origin, velocity, acceleration, gravity, secondsSinceSpawn,
                    elasticity, killOnImpact, trace, 0.0F, nullptr, nullptr);
}

bool flugOhneSpur(const FlugZustand& zustand, float secondsSinceSpawn) {
    if (!zustand.gueltig) {
        return false;
    }
    if (zustand.aus.liegt || zustand.aus.gestorben) {
        return true;
    }
    return std::min(volleSchritte(secondsSinceSpawn), kMaxSchritte) <= zustand.bekannt;
}

FlugErgebnis flugbahnWeiter(const camera::Vec3& origin,
                            const camera::Vec3& velocity,
                            const camera::Vec3& acceleration, float gravity,
                            float secondsSinceSpawn, float elasticity,
                            bool killOnImpact, const TraceFn& trace,
                            FlugZustand& zustand) {
    return schritte(origin, velocity, acceleration, gravity, secondsSinceSpawn,
                    elasticity, killOnImpact, trace, 0.0F, nullptr, &zustand);
}

FlugErgebnis flugbahnAbtasten(
    const camera::Vec3& origin, const camera::Vec3& velocity,
    const camera::Vec3& acceleration, float gravity, float bisSekunde,
    float abstandSekunde, float elasticity, bool killOnImpact,
    const TraceFn& trace,
    const std::function<bool(float sekunde, const camera::Vec3& lage)>& jePunkt,
    FlugZustand* zustand) {
    return schritte(origin, velocity, acceleration, gravity, bisSekunde,
                    elasticity, killOnImpact, trace, abstandSekunde,
                    jePunkt ? &jePunkt : nullptr, zustand);
}

camera::Vec3 positionAt(const camera::Vec3& origin, const camera::Vec3& velocity,
                        const camera::Vec3& acceleration, float gravity,
                        float secondsSinceSpawn) {
    // Der Name stand in der Kopfdatei anders als hier (t). Das uebersetzt
    // sich klaglos und liest sich schlecht: wer die Kopfdatei liest, sucht
    // beim Nachschlagen ein t, das es nicht gibt.
    const float t = secondsSinceSpawn;
    // Die Engine hat ZWEI Bewegungsmodelle, und lange stand hier das falsche.
    //
    // 1. Angeheftet (`FX_RELATIVE`, an einem Bolt oder Spieler). CParticle::
    //    Update rechnet dort geschlossen:
    //
    //        realVel = ax*mVel;  realVel[2] += 0.5*mGravity*time;
    //        realVel += time*realAccel;
    //        org += time*realVel;
    //
    //    Ausmultipliziert: org + v*t + 0.5*g*t^2 + a*t^2. Die Beschleunigung
    //    wirkt doppelt so stark, wie die Physik verlangt - Raven hat den
    //    Zweifel selbst notiert.
    //
    // 2. In der Welt gespielt (der gewoehnliche Fall, und der, den ein
    //    Editor zeigt). `CFxScheduler::CreateEffect(fx, origin, axis, ...)`
    //    faltet die Schwerkraft in die Beschleunigung:
    //
    //        accel[2] += fx->mGravity.GetVal();
    //
    //    und `CParticle::UpdateOrigin` rechnet dann SCHRITTWEISE:
    //
    //        UpdateVelocity();            // mVel += mAccel * mFloatFrameTime
    //        new_origin = mOrigin1 + mFloatFrameTime * mVel;
    //
    //    Das ergibt im Grenzwert 0.5*a*t^2, nicht a*t^2. Nachgerechnet:
    //
    //        Beschleunigung 100, eine Sekunde
    //          bei  30 fps  51.67      bei 125 fps  50.40
    //          bei  60 fps  50.83      Grenzwert    50.00
    //
    // Wir zeigen unbeheftete Effekte, also gilt Modell 2. Fuer die
    // Schwerkraft aendert sich dadurch nichts (beide Modelle geben 0.5*g*t^2);
    // eine Datei mit `accel` flog bei uns dagegen doppelt so weit wie im
    // Spiel.
    //
    // Der Grenzwert statt der Schrittrechnung: die Engine haengt damit an der
    // Bildrate - bei 30 fps fliegt dasselbe Teilchen weiter als bei 125. Eine
    // Vorschau, die auf jedem Rechner anders aussieht, waere schlimmer als
    // ein Prozent Abweichung.
    camera::Vec3 total = acceleration;
    total.z += gravity;
    return origin + velocity * t + total * (0.5F * t * t);
}

std::vector<Plane> roomPlanes(float halfWidth, float halfDepth, float height) {
    // Alle Normalen zeigen nach innen - dorthin, wo sich die Partikel
    // aufhalten. Ein Vorzeichen falsch, und sie prallen von aussen ab.
    return {
        {{0.0F, 0.0F, 1.0F}, 0.0F},            // Boden
        {{0.0F, 0.0F, -1.0F}, -height},        // Decke
        {{1.0F, 0.0F, 0.0F}, -halfWidth},      // links
        {{-1.0F, 0.0F, 0.0F}, -halfWidth},     // rechts
        {{0.0F, 1.0F, 0.0F}, -halfDepth},      // hinten
        {{0.0F, -1.0F, 0.0F}, -halfDepth},     // vorn
    };
}

Hit trace(const camera::Vec3& from, const camera::Vec3& to,
          const std::vector<Plane>& planes) {
    Hit best;
    const camera::Vec3 delta = to - from;

    for (const auto& plane : planes) {
        const float startDistance = camera::dot(plane.normal, from) - plane.distance;
        const float endDistance = camera::dot(plane.normal, to) - plane.distance;

        // Nur von der freien Seite kommend treffen. Wer schon dahinter
        // steckt, soll nicht nach aussen abprallen.
        if (startDistance < 0.0F) { continue;
}
        if (endDistance >= 0.0F) { continue;
}

        const float denominator = startDistance - endDistance;
        if (denominator <= 1e-6F) { continue;
}
        const float fraction = startDistance / denominator;

        // Die fruehste Beruehrung gewinnt. Wer nur die erste gefundene nimmt,
        // bekommt in einer Ecke den falschen Abpraller.
        if (fraction < best.fraction) {
            best.hit = true;
            best.fraction = fraction < 0.0F ? 0.0F : fraction;
            best.normal = plane.normal;
            best.point = from + delta * best.fraction;
        }
    }
    return best;
}

Path buildPath(const camera::Vec3& origin, const camera::Vec3& velocity,
               const camera::Vec3& acceleration, float gravity, float lifeMs,
               const std::vector<Plane>& planes, float elasticity,
               bool stopOnFirst) {
    Path path;
    path.segments.push_back({0.0F, origin, velocity, acceleration, gravity});
    if (planes.empty() || lifeMs <= 0.0F) { return path;
}

    // Feste Schrittweite, nicht die Bildrate: sonst haengt das Ergebnis
    // davon ab, wie schnell der Rechner ist, und die Vorschau sieht auf jedem
    // Rechner anders aus.
    constexpr float kStepMs = 8.0F;
    constexpr int kMaxBounces = 32;

    camera::Vec3 position = origin;
    camera::Vec3 currentVelocity = velocity;
    camera::Vec3 currentAcceleration = acceleration;
    float currentGravity = gravity;
    float segmentStart = 0.0F;

    // Der Schrittzaehler ist eine GANZZAHL, die Zeit wird daraus gerechnet.
    //
    // Vorher lief hier "for (float t = 0; t < lifeMs; t += kStepMs)". Bei
    // einer Lebensdauer von zehn Sekunden sind das tausend Additionen, und
    // deren Rundungsfehler summiert sich - das letzte t liegt dann nicht
    // mehr dort, wo es soll. clang-tidy meldet das als FloatLoopCounter,
    // und der Hinweis ist berechtigt: eine Fliesskommazahl als Zaehler ist
    // immer eine Ungenauigkeit auf Raten.
    //
    // i * kStepMs hat den Fehler nicht: jeder Zeitpunkt wird EINMAL
    // gerechnet, nicht aufsummiert.
    for (int i = 0;; ++i) {
        const float t = static_cast<float>(i) * kStepMs;
        if (t >= lifeMs) {
            break;
        }
        const float step = std::min(kStepMs, lifeMs - t);
        const float from = t - segmentStart;
        const float to = from + step;

        const camera::Vec3 a = positionAt(position, currentVelocity,
                                          currentAcceleration, currentGravity,
                                          from * 0.001F);
        const camera::Vec3 b = positionAt(position, currentVelocity,
                                          currentAcceleration, currentGravity,
                                          to * 0.001F);

        const Hit hit = trace(a, b, planes);
        if (!hit.hit) { continue;
}

        const float hitMs = t + step * hit.fraction;
        path.impactMs.push_back(hitMs);
        path.impactPoint.push_back(hit.point);
        path.impactNormal.push_back(hit.normal);

        if (stopOnFirst) {
            path.killed = true;
            path.killedMs = hitMs;
            return path;
        }

        // Die Geschwindigkeit im Augenblick des Aufpralls. Sie ist nicht die
        // Startgeschwindigkeit - Schwerkraft und Beschleunigung haben bis
        // dahin gewirkt.
        const float seconds = (hitMs - segmentStart) * 0.001F;
        camera::Vec3 impactVelocity = currentVelocity;
        impactVelocity.z += currentGravity * seconds;
        impactVelocity = impactVelocity + currentAcceleration * seconds;

        // Spiegeln:  v' = v - 2*(v*n)*n, dann mit der Elastizitaet daempfen.
        const float dot = camera::dot(impactVelocity, hit.normal);
        impactVelocity = impactVelocity - hit.normal * (2.0F * dot);
        impactVelocity = impactVelocity * elasticity;

        // Zur Ruhe kommen. Genau wie in der Engine: auf einer nach oben
        // zeigenden Flaeche und mit kaum noch Aufwaertsbewegung bleibt es
        // liegen. Ohne das zittert ein Funke ewig auf dem Boden.
        if (hit.normal.z > 0.0F && impactVelocity.z < 4.0F) {
            impactVelocity = {};
            currentAcceleration = {};
            currentGravity = 0.0F;
            // Die Engine loescht hier BEIDE Flags:
            //     mFlags &= ~(FX_APPLY_PHYSICS|FX_IMPACT_RUNS_FX);
            // Ein liegender Funke soll nicht weiter Aufpralleffekte
            // ausloesen. Bei uns hoert die Zerlegung ohnehin auf, weil sich
            // nichts mehr bewegt - aber der letzte Aufprall zaehlt noch, und
            // genau so macht es die Engine auch: die Loeschung steht NACH dem
            // Ausloesen.
            path.settledMs = hitMs;
        }

        // Ein Stueck von der Flaeche wegsetzen, sonst faengt der naechste
        // Schritt schon wieder in ihr an.
        position = hit.point + hit.normal * 0.03F;
        currentVelocity = impactVelocity;
        segmentStart = hitMs;
        path.segments.push_back({hitMs, position, currentVelocity,
                                 currentAcceleration, currentGravity});

        if (static_cast<int>(path.segments.size()) > kMaxBounces) { break;
}
    }
    return path;
}

camera::Vec3 positionOnPath(const Path& path, float msSinceSpawn) {
    if (path.segments.empty()) { return {};
}
    if (path.killed && msSinceSpawn > path.killedMs) { msSinceSpawn = path.killedMs;
}

    // Rueckwaerts suchen: der letzte Abschnitt, der schon begonnen hat.
    size_t index = path.segments.size() - 1;
    while (index > 0 && path.segments[index].startMs > msSinceSpawn) { --index;
}

    const PathSegment& segment = path.segments[index];
    const float seconds = (msSinceSpawn - segment.startMs) * 0.001F;
    return positionAt(segment.origin, segment.velocity, segment.acceleration,
                      segment.gravity, seconds < 0.0F ? 0.0F : seconds);
}

}  // namespace bhed::efx::sim
