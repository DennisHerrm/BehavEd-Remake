#include "bhed/scene.h"
#include "bhed/camtrack.h"

#include "bhed/num.h"

#include "bhed/clock.h"
#include <map>
#include <memory>
#include <functional>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <cstddef>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace bhed {
namespace {
// Der kuerzere Weg zwischen zwei Winkeln, in Grad.
//
// Nachgebaut nach AngleDelta() der Engine (shared/qcommon/q_math.c): das
// Ergebnis liegt immer in (-180, 180].
float angleDelta(float to, float from) {
    float d = std::fmod(to - from, 360.0F);
    if (d > 180.0F) {
        d -= 360.0F;
    } else if (d < -180.0F) {
        d += 360.0F;
    }
    return d;
}


void readVec(const std::string& text, float out[3]) {
    std::istringstream is(text);
    is >> out[0] >> out[1] >> out[2];
}

// readMs kommt aus bhed/clock.h - dieselbe Rechnung wie in camtrack.cpp
// und timeline.cpp. Hier stand eine eigene Kopie, und die war die einzige
// der drei, die $random$ kannte.

float distance(const float a[3], const float b[3]) {
    float sum = 0.0F;
    for (int k = 0; k < 3; ++k) {
        const float d = a[k] - b[k];
        sum += d * d;
    }
    return std::sqrt(sum);
}

// Den Ort eines Wegpunkts in der Karte nachschlagen.
bool findEntity(const MapData& map, const std::string& targetname, float out[3]) {
    for (const MapEntity& e : map.entities) {
        if (e.origin.empty()) {
            continue;
        }
        if (e.targetname == targetname) {
            readVec(e.origin, out);
            return true;
        }
        // --- Eine FIGUR heisst im Spiel wie ihr Spawner -------------------
        //
        // Gemeldet: "die Kopfbewegungen gehen immer noch nicht."
        //
        // Hier stand nur der Vergleich mit targetname. Ein NPC_spawner
        // traegt seinen Namen aber unter NPC_targetname - und damit fand
        // ein SET_LOOK_TARGET oder SET_WATCHTARGET NIE eine andere Figur.
        // Der Schritt wurde als "Ziel loeschen" abgelegt, und die Figur
        // schaute geradeaus.
        //
        // Genau das ist bei fet1 in intro_jedi passiert:
        //
        //     set ( "SET_LOOK_TARGET", "win2" );
        //
        // win2 ist ein NPC_spawner mit NPC_targetname "win2". Der Schritt
        // kam mit value 0 und Ziel 0/0/0 an.
        //
        // Im Spiel geht das anders. NPC_spawn.cpp:1688 f., beim Erzeugen
        // der Figur aus dem Spawner:
        //
        //     newent->script_targetname = G_NewString(ent->NPC_targetname);
        //     newent->targetname        = G_NewString(ent->NPC_targetname);
        //
        // Die erzeugte Figur bekommt den NPC_targetname ihres Spawners als
        // ihren EIGENEN targetname. Ein G_Find ueber targetname findet sie
        // also - und deshalb muessen wir hier beides ansehen.
        //
        // Der eigene targetname geht VOR: er ist der direktere Treffer, und
        // in einer Karte koennen beide Namen nebeneinander vorkommen.
        if (const std::string* npcName = e.find("NPC_targetname")) {
            if (*npcName == targetname) {
                readVec(e.origin, out);
                return true;
            }
        }
    }
    return false;
}

// Den Spawner einer Figur suchen. Er heisst nicht targetname, sondern
// NPC_targetname - das ist der Name, den affect anspricht.
//
// Vorher stand hier nur `classname == "NPC_spawner"`. Die Engine kennt
// aber DEUTLICH mehr: code/game/g_spawn.cpp fuehrt in seiner Tabelle 88
// Klassennamen, die eine Figur erzeugen - NPC_spawner, NPC_Kyle, NPC_Jedi,
// NPC_Reborn, NPC_Stormtrooper, NPC_Droid_R2D2 und so weiter. Alle
// beginnen mit "NPC_", und genau daran erkennen wir sie.
//
// Das war der Grund fuer "0 von 64 Figuren mit Modell": eine Karte, die
// ihre Figuren mit NPC_Kyle statt NPC_spawner setzt, hatte hier keinen
// einzigen Treffer - und ohne Spawner kein NPC_type, ohne NPC_type kein
// Modell.
bool isNpcSpawner(const MapEntity& e) {
    // Die Engine vergleicht Klassennamen mit strcmp, also GENAU
    // (g_spawn.cpp, G_CallSpawn). Deshalb hier kein tolower.
    return e.classname.starts_with("NPC_");
}

// Kein NPC_type am Spawner? Dann sagt der Klassenname ihn.
//
// Die eigenen SP_-Funktionen setzen ihn selbst, wenn er fehlt - etwa
// SP_NPC_Stormtrooper mit self->NPC_type = "rockettrooper" je nach
// spawnflags (NPC_spawn.cpp). Das im Einzelnen nachzubauen lohnt hier
// nicht; fuer die Modellsuche genuegt der Name hinter "NPC_" in
// Kleinbuchstaben - "NPC_Kyle" -> "kyle", "NPC_Jedi" -> "jedi". Genau so
// heissen die Eintraege in den .npc-Dateien.
//
// Eine Naeherung, und sie ist als solche gekennzeichnet: findet sich der
// Name in keiner .npc, bleibt die Figur eben ohne Modell - wie vorher, nur
// dass es jetzt ueberhaupt einen Versuch gibt.
// Die SP_NPC_*-Funktionen setzen NPC_type selbst (NPC_spawn.cpp:3115 ff.),
// oft abhaengig von spawnflags. Die Vorgaben (ohne Flags) und die Regeln
// der haeufigen Klassen, aus dem Quelltext abgeschrieben. Vorher wurde aus
// "NPC_Droid_Remote" einfach "droid_remote" - die .npc heisst aber
// "remote_sp", und die Schwebedroiden von md_afh blieben ohne Modell.
std::string typeFromClassname(const std::string& classname);

std::string typeFromClassname(const std::string& classname, int spawnflags) {
    struct Regel {
        const char* klasse;
        const char* vorgabe;
        int flag1;
        const char* typ1;
        int flag2;
        const char* typ2;
        int flag3;
        const char* typ3;
        int flag4;
        const char* typ4;
    };
    static const Regel kRegeln[] = {
        {"NPC_Stormtrooper", "StormTrooper", 8, "rockettrooper", 4, "stofficeralt", 2, "stcommander", 1, "stofficer"},
        {"NPC_Imperial", "Imperial", 1, "ImpOfficer", 2, "ImpCommander", 0, nullptr, 0, nullptr},
        {"NPC_Jedi", "Jedi", 2, "jedimaster", 1, "jeditrainer", 0, nullptr, 0, nullptr},
        {"NPC_Kyle", "Kyle", 1, "Kyle_boss", 0, nullptr, 0, nullptr, 0, nullptr},
        {"NPC_Reborn", "reborn", 1, "rebornforceuser", 2, "rebornfencer", 4, "rebornacrobat", 8, "rebornboss"},
        {"NPC_Human_Merc", "human_merc", 1, "human_merc_key", 2, "human_merc_bow", 4, "human_merc_rep", 8, "human_merc_flc"},
        {"NPC_Tusken", "tusken", 1, "tuskensniper", 0, nullptr, 0, nullptr, 0, nullptr},
        {"NPC_Jawa", "jawa", 1, "jawa_armed", 0, nullptr, 0, nullptr, 0, nullptr},
        {"NPC_Rodian", "rodian", 1, "rodian2", 0, nullptr, 0, nullptr, 0, nullptr},
        {"NPC_Gran", "gran", 1, "granshooter", 2, "granboxer", 0, nullptr, 0, nullptr},
        {"NPC_Alora", "alora", 1, "alora_dual", 0, nullptr, 0, nullptr, 0, nullptr},
        {"NPC_Galak", "Galak", 1, "Galak_Mech", 0, nullptr, 0, nullptr, 0, nullptr},
        {"NPC_Monster_Rancor", "rancor", 1, "mutant_rancor", 0, nullptr, 0, nullptr, 0, nullptr},
        {"NPC_Monster_Sand_Creature", "sand_creature", 1, "sand_creature_fast", 0, nullptr, 0, nullptr, 0, nullptr},
        {"NPC_Droid_R2D2", "r2d2", 1, "r2d2_imp", 0, nullptr, 0, nullptr, 0, nullptr},
        {"NPC_Droid_R5D2", "r5d2", 1, "r5d2_imp", 0, nullptr, 0, nullptr, 0, nullptr},
        {"NPC_Droid_Protocol", "protocol", 1, "protocol_imp", 0, nullptr, 0, nullptr, 0, nullptr},
        {"NPC_Droid_Saber", "saber_droid", 1, "saber_droid_training", 0, nullptr, 0, nullptr, 0, nullptr},
        {"NPC_Saboteur", "saboteur", 1, "saboteursniper", 2, "saboteurpistol", 4, "saboteurcommando", 0, nullptr},
        {"NPC_Cultist", "cultist", 1, "cultist_grip", 2, "cultist_lightning", 4, "cultist_drain", 0, nullptr},
        {"NPC_HazardTrooper", "hazardtrooper", 1, "hazardtrooperofficer", 2, "hazardtrooperconcussion", 0, nullptr, 0, nullptr},
    };
    static const std::pair<const char*, const char*> kFest[] = {
        {"NPC_Droid_Remote", "remote_sp"}, {"NPC_Droid_Seeker", "seeker"}, {"NPC_Droid_Sentry", "sentry"},
        {"NPC_Droid_Interrogator", "interrogator"}, {"NPC_Droid_Probe", "probe"}, {"NPC_Droid_Mark1", "mark1"},
        {"NPC_Droid_Mark2", "mark2"}, {"NPC_Droid_ATST", "atst"}, {"NPC_Droid_Gonk", "gonk"},
        {"NPC_Droid_Mouse", "mouse"}, {"NPC_Droid_Assassin", "assassin_droid"}, {"NPC_BobaFett", "Boba_Fett"},
        {"NPC_Chewbacca", "Chewie"}, {"NPC_Tie_Pilot", "stormpilot"}, {"NPC_RocketTrooper", "rockettrooper2"},
        {"NPC_Snowtrooper", "snowtrooper"}, {"NPC_Monster_Wampa", "wampa"}, {"NPC_Monster_Howler", "howler"},
        {"NPC_Monster_Mutant_Rancor", "mutant_rancor"}, {"NPC_MineMonster", "minemonster"},
        {"NPC_Reborn_New", "reborn_new"}, {"NPC_Rosh_Penin", "rosh_penin"}, {"NPC_Tavion_New", "tavion_new"},
        {"NPC_Cultist_Saber", "cultist_saber"}, {"NPC_Cultist_Saber_Powers", "cultist_saber2"},
        {"NPC_Cultist_Commando", "cultistcommando"}, {"NPC_Cultist_Destroyer", "cultist_destroyer"},
        {"NPC_ShadowTrooper", "ShadowTrooper"}, {"NPC_SwampTrooper", "SwampTrooper"}, {"NPC_Weequay", "Weequay"},
        {"NPC_Ugnaught", "Ugnaught"}, {"NPC_BespinCop", "BespinCop"}, {"NPC_Prisoner", "Prisoner"},
        {"NPC_Kothos", "VKothos"}, {"NPC_Lannik_Racto", "lannik_racto"}, {"NPC_Vehicle", "swoop"},
        {"NPC_Player", "player"}, {"NPC_Noghri", "noghri"}, {"NPC_Trandoshan", "Trandoshan"},
        {"NPC_ImpWorker", "ImpWorker"}, {"NPC_Merchant", "merchant"}, {"NPC_Rebel", "Rebel"},
    };
    for (const Regel& r : kRegeln) {
        if (classname == r.klasse) {
            const std::pair<int, const char*> fl[] = {{r.flag1, r.typ1}, {r.flag2, r.typ2}, {r.flag3, r.typ3},
                                                      {r.flag4, r.typ4}};
            for (const auto& [f, typ] : fl) {
                if (f != 0 && typ != nullptr && (spawnflags & f) != 0) {
                    return typ;
                }
            }
            return r.vorgabe;
        }
    }
    for (const auto& [k, typ] : kFest) {
        if (classname == k) {
            return typ;
        }
    }
    return typeFromClassname(classname);
}

std::string typeFromClassname(const std::string& classname) {
    if (classname.size() <= 4) {
        return {};
    }
    std::string t = classname.substr(4);
    for (char& c : t) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (t == "spawner" || t == "vehicle") {
        return {};   // sagen nichts ueber die Figur aus
    }
    // NPC_Player: SP_NPC_Player setzt NPC_type "Player" (NPC_spawn.cpp:3117)
    // - eine Figur, die aussieht wie der Spieler. Die App loest "player"
    // auf das Spielermodell auf (g_char_model).
    return t;
}

// Namen vergleichen, wie die Engine es tut: OHNE Ruecksicht auf Gross- und
// Kleinschreibung.
//
// code/game/Q3_Interface.cpp, CQuake3GameInterface::GetByName():
//
//     ei = m_EntityList.find( Q_strupr( (char *) temp ) );
//
// Die Liste ist nach Grossbuchstaben aufgebaut. Ein Skript, das
// affect ( "Kanan1" ) schreibt, waehrend die Karte "kanan1" nennt, findet
// die Figur im Spiel - hier fand es sie vorher nicht.
bool sameName(const std::string& a, const std::string& b) {
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

const MapEntity* findSpawner(const MapData& map, const std::string& name) {
    for (const MapEntity& e : map.entities) {
        if (!isNpcSpawner(e)) {
            continue;
        }
        const std::string* t = e.find("NPC_targetname");
        if (t != nullptr && sameName(*t, name)) {
            return &e;
        }
        if (sameName(e.targetname, name)) {
            return &e;
        }
    }
    return nullptr;
}

// Und die allgemeine Suche: JEDE Entity mit diesem Namen.
//
// affect spricht nicht nur Figuren an. Nachgezaehlt an den 23 Karten aus
// MD_Maps_Ep1/2/4/6: von den 25 aufloesbaren affect-Zielen in den
// mitgelieferten Skripten ist KEIN EINZIGES ein NPC. Es sind func_usable,
// func_static, trigger_multiple, func_breakable, func_door - Tueren,
// Plattformen, und die fliegenden Schiffe der Zwischensequenzen.
//
// Die Engine unterscheidet dabei gar nicht: GetByName() sucht in einer
// Liste ueber ALLE benannten Entities. Vorher fand diese Stelle nur
// NPC-Spawner, und alles andere stand ohne Ort da - also unsichtbar, ohne
// Bahn auf der Karte.
const MapEntity* findNamed(const MapData& map, const std::string& name) {
    for (const MapEntity& e : map.entities) {
        const std::string* t = e.find("NPC_targetname");
        if (t != nullptr && sameName(*t, name)) {
            return &e;
        }
        // script_targetname: der Name, unter dem ICARUS eine Entity kennt
        // (ValidEntity, Q3_Interface.cpp:8096). md_dd_jedi "falcon2" ist ein
        // func_static NUR mit script_targetname - ohne diese Zeile blieb das
        // Raumschiff der Zwischensequenz stehen.
        const std::string* st = e.find("script_targetname");
        if (st != nullptr && sameName(*st, name)) {
            return &e;
        }
        if (sameName(e.targetname, name)) {
            return &e;
        }
    }
    return nullptr;
}

// Der Wert eines set-Befehls: set ( "SET_X", wert )
const std::string* setValue(const Node& n, const char* type) {
    if (n.name != "set" || n.args.size() < 2) {
        return nullptr;
    }
    if (n.args[0].text != type) {
        return nullptr;
    }
    return &n.args[1].text;
}

}  // namespace

const Actor* Scene::find(const std::string& name) const {
    for (const Actor& a : actors) {
        if (a.name == name) {
            return &a;
        }
    }
    return nullptr;
}

ActorState Actor::at(double ms) const {
    ActorState st;
    for (int k = 0; k < 3; ++k) {
        st.pos[k] = start[k];
        st.angles[k] = startAngles[k];
    }
    // Ohne Angaben steht die Figur.
    st.animation = "BOTH_STAND1";
    // Die Ausstattung beim Spawnen - siehe ausstattungBeimSpawnen.
    st.waffe = startWaffe;
    st.hatSaber = startHatSaber;
    st.dualSabers = startDual;
    st.saber = startSaber;
    st.hand = startHand;

    // --- Die Anim-Timer der Engine -----------------------------------------
    //
    // ICARUS setzt Animationen mit SETANIM_FLAG_RESTART|HOLD|OVERRIDE
    // (Q3_Interface.cpp:1957). HOLD stellt den Timer auf die Laenge der
    // Animation (bg_panimate.cpp:4721); SET_ANIM_HOLDTIME stellt ihn neu,
    // -1 heisst nie (Q3_Interface.cpp:2060). Laeuft der Timer der Beine ab,
    // setzt PM_Footsteps die Grundhaltung (bg_pmove.cpp:8165, 100 ms
    // Blende); der Oberkoerper folgt dann den Beinen.
    //
    // Vorher gab es diese Timer nicht: eine Geste ohne HOLDTIME fror in
    // ihrer Endstellung ein, statt zurueck in den Stand zu blenden.
    constexpr double kNie = 1e300;
    double beineBis = -1.0;   // -1: keine Skriptanimation auf den Beinen
    double torsoBis = -1.0;
    double gehEnde = 0.0;
    // Ist der Timer der Beine BEKANNT (Laenge aus der .cfg oder HOLDTIME)?
    // Ohne Skelett gibt es keine Laenge; dann bleibt es beim alten
    // Verhalten: ein Weg loest eine vorher gesetzte Animation ab.
    bool beineFest = false;
    double beineAbMs = -1.0;             // Start der letzten Beinanimation
    const ActorStep* weg = nullptr;      // der Weg, auf dem die Figur gerade ist
    // Reihenfolge im Skript: bei gleicher Zeit gewinnt, was spaeter steht.
    std::ptrdiff_t beineNr = -1;
    std::ptrdiff_t wegNr = -1;
    const auto dauer = [this](const std::string& n) {
        const auto it = animDauerMs.find(n);
        return (it == animDauerMs.end()) ? kNie : it->second;
    };
    // Laeuft gerade eine ROFF-Bahn auf dieser Figur? Dann dreht sie sich
    // nicht zum Watchtarget: NPC_UpdateAngles steht nur im Zweig OHNE
    // Bahn (NPC.cpp:2380 ff.) - die Winkel kommen allein aus G_Roff.
    bool roffFigurLaeuft = false;

    for (const ActorStep& step : steps) {
        if (step.startMs > ms) {
            break;   // die Schritte stehen in Reihenfolge
        }
        switch (step.kind) {
            case ActorStep::Kind::Teleport:
                for (int k = 0; k < 3; ++k) {
                    st.pos[k] = step.to[k];
                }
                break;
            case ActorStep::Kind::WalkTo: {
                const double span = step.endMs - step.startMs;
                const double t = (span <= 0.0)
                                     ? 1.0
                                     : std::min(1.0, (ms - step.startMs) / span);
                const auto f = static_cast<float>(t);
                for (int k = 0; k < 3; ++k) {
                    st.pos[k] = step.from[k] + (step.to[k] - step.from[k]) * f;
                }
                // Beim Gehen schaut die Figur in Laufrichtung - so macht es
                // die Engine auch, solange kein SET_LOOK_TARGET gesetzt ist.
                gehEnde = step.endMs;
                wegNr = &step - steps.data();
                const float dx = step.to[0] - step.from[0];
                const float dy = step.to[1] - step.from[1];
                if (dx * dx + dy * dy > 1.0F) {
                    // NICHT sofort: desiredYaw wird die Laufrichtung, und
                    // die Figur dreht sich mit ihrer Drehrate dorthin
                    // (g_navigator.cpp:4281, NPC_UpdateAngles). Vorher sprang
                    // der Winkel beim Loslaufen.
                    const float ziel = std::atan2(dy, dx) * 180.0F / std::numbers::pi_v<float>;
                    const float vorher = st.angles[1];
                    float rest = ziel - vorher;
                    while (rest > 180.0F) { rest -= 360.0F; }
                    while (rest < -180.0F) { rest += 360.0F; }
                    const auto geschafft = static_cast<float>(
                        st.turnDegPerSec * std::max(0.0, ms - step.startMs) / 1000.0);
                    st.angles[1] = (geschafft >= std::fabs(rest))
                                       ? ziel
                                       : vorher + ((rest < 0.0F) ? -geschafft : geschafft);
                }
                st.moving = (t < 1.0);
                // Die Laufanimation waehlt erst der Abschnitt nach der
                // Schleife - sie haengt am Anim-Timer (siehe dort).
                weg = st.moving ? &step : nullptr;
                break;
            }
            case ActorStep::Kind::Face:
                for (int k = 0; k < 3; ++k) {
                    st.angles[k] = step.to[k];
                }
                break;
            case ActorStep::Kind::Glide:
            case ActorStep::Kind::Turn: {
                const double span = step.endMs - step.startMs;
                const double t = (span <= 0.0)
                                     ? 1.0
                                     : std::min(1.0, (ms - step.startMs) / span);
                // Der Verlauf der Engine, nicht der gleichmaessige.
                //
                // Gemeldet: "wenn er sich laut Skript drehen soll, passiert
                // das bei uns instant statt wie in der Engine mit
                // Transition."
                //
                // Q3_Interface.cpp setzt bei rotate TR_NONLINEAR_STOP, und
                // bg_misc.cpp rechnet dafuer sin(90 Grad * Fortschritt):
                // schnell los, langsam aus. Wir sind gleichmaessig
                // gefahren - bei kurzen Dauern sieht das aus, als spraenge
                // es.
                // ZWEI Verlaeufe, und das ist kein Versehen.
                //
                // Im Quelltext gefunden: TR_NONLINEAR_STOP wird durchweg auf
                // `apos` gesetzt - die WINKEL. Fuer `pos`, den Ort, habe ich
                // keinen solchen Beleg gefunden.
                //
                // Mein erster Anlauf nahm die Kurve fuer beides. Die Probe
                // "move bewegt: nach 500 ms halbe Strecke" hat es sofort
                // gemeldet - zu Recht: eine Vermutung auf die Strecke
                // auszudehnen, fuer die der Beleg fehlt, waere geraten und
                // nicht nachgesehen.
                //
                // Nachtrag: der Beleg fuer `pos` steht in g_mover.cpp:648
                // (SetMoverState, von Q3_Lerp2Pos ueber MatchTeam gerufen):
                //     if ( ent->alt_fire ) trType = TR_LINEAR_STOP;
                //     else                 trType = TR_NONLINEAR_STOP;
                // Der Ort laeuft also GENAUSO weich aus - ausser die Karte
                // setzt "linear" "1". Bei halber Zeit sind 70,7 % der
                // Strecke geschafft, nicht 50.
                const auto fLinear = static_cast<float>(t);
                const float f = step.linear ? fLinear : nonlinearStop(fLinear);
                if (step.kind == ActorStep::Kind::Glide) {
                    for (int k = 0; k < 3; ++k) {
                        st.pos[k] = step.from[k] + (step.to[k] - step.from[k]) * f;
                    }
                }
                // Turn aendert nur die Winkel, Glide sie nur mit Winkeln -
                // sonst ueberschreibt ein gleichzeitiger move ein rotate
                // (und umgekehrt), und die Drehung springt ans Ende.
                if (step.kind == ActorStep::Kind::Glide && !step.mitWinkel) {
                    st.moving = (t < 1.0);
                    break;
                }
                // Winkel ueber den KUERZEREN Weg.
                //
                // Genau so macht es die Engine, Q3_Interface.cpp:
                //     ang[i] = AngleDelta( angles[i], ent->currentAngles[i] );
                //     ent->s.apos.trDelta[i] = ang[i] / (duration * 0.001f);
                //
                // Ohne AngleDelta dreht ein Schiff von 350 nach 10 Grad
                // einmal fast ganz herum, statt die zwanzig Grad zu gehen.
                for (int k = 0; k < 3; ++k) {
                    float w = step.fromAng[k] +
                              angleDelta(step.toAng[k], step.fromAng[k]) * f;
                    // Auf 0..360 bringen: vom aktuellen Stand aus gerechnet
                    // stuende sonst 370 statt 10 da (gleichwertig, aber
                    // nicht eindeutig lesbar).
                    w = std::fmod(w, 360.0F);
                    if (w < 0.0F) { w += 360.0F; }
                    st.angles[k] = w;
                }
                st.moving = (t < 1.0) && step.kind == ActorStep::Kind::Glide;
                break;
            }
            case ActorStep::Kind::Roff: {
                // Die Bahn aus der Datei, ab dem Stand beim play-Befehl -
                // wie G_Roff sie Bild fuer Bild anwendet (bhed/roff.h).
                // Ohne gelesene Datei steht die Entity, wie in der Engine
                // (G_LoadRoff scheitert, Play tut nichts).
                if (!step.roff || !step.roffBahn) {
                    break;
                }
                const RoffStand r = roffStand(*step.roff, *step.roffBahn, ms - step.startMs, step.roffFigur);
                for (int k = 0; k < 3; ++k) {
                    st.pos[k] = r.ort[k];
                    // Auf 0..360 wie bei rotate; die Engine selbst laesst
                    // die Winkel frei auflaufen, gezeichnet wird dasselbe.
                    float w = std::fmod(r.winkel[k], 360.0F);
                    if (w < 0.0F) { w += 360.0F; }
                    st.angles[k] = w;
                }
                // Eine Figur geht dabei nicht: NPC_Think ruft statt
                // ClientThink nur NPC_ApplyRoff (NPC.cpp:2157), es gibt
                // keinen Bewegungsschritt und damit keine Laufanimation.
                // Ein Mover faehrt - wie bei move.
                st.moving = !step.roffFigur && ms < step.endMs;
                if (step.roffFigur && ms < step.endMs) {
                    roffFigurLaeuft = true;
                }
                break;
            }
            case ActorStep::Kind::Anim:
                // --- UPPER laesst die Beine in Ruhe ---------------------
                //
                // SETANIM_TORSO setzt die Animation auf "lower_lumbar",
                // SETANIM_LEGS auf "model_root" (bg_panimate.cpp:4848,
                // 4864). Ein UPPER fasst die Beinspur also nicht an.
                if (step.part == AnimPart::Upper) {
                    // Der Oberkoerper blendet von dem, was er gerade
                    // zeigte: seiner eigenen Animation, oder - folgte er den
                    // Beinen - der Animation der Beine.
                    if (!st.torsoAnimation.empty()) {
                        st.torsoPrevAnimation = st.torsoAnimation;
                        st.torsoPrevSinceMs = step.startMs - st.torsoStartMs;
                    } else {
                        st.torsoPrevAnimation = st.animation;
                        st.torsoPrevSinceMs = step.startMs - st.animStartMs;
                    }
                    st.torsoBlendStartMs = step.startMs;
                    st.torsoBlendMs = kSetAnimBlendDefault;
                    st.torsoAnimation = step.text;
                    st.torsoStartMs = step.startMs;
                    st.torsoHold = false;
                    torsoBis = step.startMs + dauer(step.text);
                    break;
                }
                beineBis = step.startMs + dauer(step.text);
                beineFest = animDauerMs.count(step.text) != 0;
                beineAbMs = step.startMs;
                beineNr = &step - steps.data();
                if (step.part == AnimPart::Both) {
                    torsoBis = -1.0;   // der Oberkoerper folgt den Beinen
                }
                // BOTH belegt BEIDE Knochen neu - danach gibt es keine
                // getrennte Oberkoerperspur mehr. LOWER laesst eine
                // bestehende stehen.
                if (step.part == AnimPart::Both) {
                    // Hatte der Oberkoerper eine eigene Animation, blendet
                    // er von DER - nicht von der alten Beinanimation, die
                    // der Uebergang der ganzen Figur zeigt.
                    if (!st.torsoAnimation.empty()) {
                        st.torsoPrevAnimation = st.torsoAnimation;
                        st.torsoPrevSinceMs = step.startMs - st.torsoStartMs;
                        st.torsoBlendStartMs = step.startMs;
                        st.torsoBlendMs = kSetAnimBlendDefault;
                    } else {
                        st.torsoPrevAnimation.clear();
                    }
                    st.torsoAnimation.clear();
                }
                // Den Uebergang aufzeichnen, BEVOR die alte Angabe
                // ueberschrieben wird.
                //
                // Geblendet wird auch, wenn DIESELBE Animation neu gesetzt
                // wird. Das ist kein Versehen: Skripte setzen
                // SETANIM_FLAG_RESTART, und in PM_SetAnimFinal steht
                //
                //     (torsOnAnimNow && !animRestart) ? (animFlags & ~BONE_ANIM_BLEND)
                //                                     : animFlags
                //
                // Mit gesetztem RESTART ist !animRestart falsch, also
                // bleibt BONE_ANIM_BLEND stehen.
                if (!st.animation.empty()) {
                    st.prevAnimation = st.animation;
                    // EINGEFROREN: wie weit die alte Animation im Moment
                    // des Umschaltens war. Dieser Wert bleibt stehen,
                    // waehrend der Uebergang laeuft - die alte Seite ist
                    // ein Standbild, kein zweiter Abspieler.
                    st.prevAnimSinceMs = step.startMs - st.animStartMs;
                    st.prevHoldAnim = st.holdAnim;
                    st.blendStartMs = step.startMs;
                    st.blendMs = kSetAnimBlendDefault;
                }
                st.animation = step.text;
                st.animStartMs = step.startMs;
                st.holdAnim = false;
                break;
            case ActorStep::Kind::Hold: {
                // SET_ANIM_HOLDTIME_UPPER haelt den OBERkoerper, nicht die
                // Beine - Q3_SetAnimHoldTime bekommt dafuer den Parameter
                // `lower` (Q3_Interface.cpp:2060) und ruft entsprechend
                // PM_SetTorsoAnimTimer oder PM_SetLegsAnimTimer.
                bool halt = false;
                if (step.value < 0.0F) {
                    halt = true;              // unbegrenzt einfrieren
                } else if (step.value > 0.0F) {
                    halt = (ms <= step.endMs);
                }
                if (step.part == AnimPart::Upper) {
                    st.torsoHold = halt;
                } else {
                    st.holdAnim = halt;
                    if (step.part == AnimPart::Both) {
                        st.torsoHold = halt;
                    }
                }
                // Der Timer: -1 nie, sonst ab JETZT so viele ms
                // (PM_SetLegsAnimTimer/PM_SetTorsoAnimTimer).
                {
                    const double bis = (step.value < 0.0F)
                                           ? kNie
                                           : step.startMs + static_cast<double>(step.value);
                    if (step.part != AnimPart::Upper && beineBis >= 0.0) {
                        beineBis = bis;
                        beineFest = true;
                    }
                    if (step.part != AnimPart::Lower && torsoBis >= 0.0) { torsoBis = bis; }
                }
                break;
            }
            case ActorStep::Kind::Look:
                st.hasLookTarget = (step.value > 0.5F);
                for (int k = 0; k < 3; ++k) {
                    st.lookAt[k] = step.to[k];
                }
                break;
            case ActorStep::Kind::TurnRate:
                st.turnDegPerSec = step.value;
                break;
            case ActorStep::Kind::Watch:
                if (step.value > 0.5F) {
                    // Nur beim EINSCHALTEN merken, wo sie stand.
                    //
                    // Steht dasselbe Ziel zweimal hintereinander im Skript,
                    // faengt die Drehung sonst jedes Mal von vorn an - und
                    // die Figur bliebe stehen, solange nachgesetzt wird.
                    if (!st.hasWatchTarget) {
                        st.watchFromYaw = st.angles[1];
                        st.watchStartMs = step.startMs;
                    }
                    st.hasWatchTarget = true;
                    for (int k = 0; k < 3; ++k) {
                        st.watchAt[k] = step.to[k];
                    }
                } else {
                    st.hasWatchTarget = false;
                }
                break;
            case ActorStep::Kind::Crouch:
                st.crouched = (step.value > 0.5F);
                break;
            case ActorStep::Kind::Show:
                // Ist die Entity fort, macht sie kein Befehl wieder
                // sichtbar - es gibt sie nicht mehr.
                if (!st.removed) {
                    st.visible = true;
                }
                break;
            case ActorStep::Kind::Saber:
            case ActorStep::Kind::Waffe:
            case ActorStep::Kind::SaberWahl:
            case ActorStep::Kind::SaberFarbe:
            case ActorStep::Kind::KlingeSchalter:
            case ActorStep::Kind::HandModell:
                // Waffe und Schwerter: src/ausruestung.cpp. Dort setzt auch
                // SET_SABERACTIVE saberActive und saberSinceMs - nur bei
                // einer AENDERUNG, damit zweimal "an" die Klinge nicht
                // wieder einfahren laesst.
                ausruestungSchritt(*this, st, step);
                break;
            case ActorStep::Kind::Fire: {
                const bool an = (step.value > 0.5F);
                // Nur beim EINschalten neu setzen. Ein zweites
                // SET_FIRE_WEAPON "true" waehrend des Feuerns soll den Takt
                // nicht zuruecksetzen - die Engine tut das auch nicht, sie
                // setzt bloss dasselbe Flag noch einmal.
                if (an && !st.firing) {
                    st.firingSinceMs = step.startMs;
                }
                st.firing = an;
                break;
            }
            case ActorStep::Kind::ShotSpacing:
                // Null waere eine Animation, die in jedem Bild neu
                // anfaengt - also ein Standbild auf dem ersten Bild.
                if (step.value > 1.0F) {
                    st.shotSpacingMs = step.value;
                }
                break;
            case ActorStep::Kind::Gone:
                st.visible = false;
                st.removed = true;
                break;
            case ActorStep::Kind::Hide:
                st.visible = false;
                break;
        }
    }
    // Ein Blickziel dreht NICHT den Koerper.
    //
    // Hier stand bis rc248 eine Drehung des ganzen Koerpers, und die war
    // falsch. cg_players.cpp setzt aus dem lookTarget die renderInfo.head-
    // Angles, und die wirken ueber CG_UpdateAngleClamp (ebenda:3319 f.)
    // nur auf den KOPF, mit Anschlaegen bei 64 Grad.
    //
    // Die Drehung selbst passiert deshalb im Zeichner, an einem Knochen -
    // hier bleibt nur das ZIEL stehen (st.hasLookTarget, st.lookAt). Wer
    // den Koerper drehen will, nimmt SET_WATCHTARGET; das steht direkt
    // darunter und gewinnt gegen alles.
    //
    // Was dabei absichtlich verlorengeht: solange behaved den Oberkoerper
    // nicht mitdreht (in der Engine folgt er mit halbem Ausschlag,
    // LOOK_SWING_SCALE 0.5), wirkt ein Blick nach hinten steifer als im
    // Spiel. Steif und richtig ist besser als beweglich und falsch.
    // --- Das Watchtarget gewinnt gegen ALLES davor -----------------------
    //
    // NPC_behavior.cpp:257 sagt es ausdruecklich: "NOTE: this will override
    // any angles set by NPC_MoveToGoal". Der Block steht deshalb NACH dem
    // Blickziel und ohne die !st.moving-Bedingung - eine Figur, die zu
    // einem Wegpunkt geht und dabei jemanden ansieht, dreht den Koerper
    // zum Angesehenen, nicht in die Laufrichtung.
    //
    // Nur der Gierwinkel. Die Engine setzt daneben desiredPitch, aber der
    // wirkt auf die Blickrichtung und nicht auf die Beine; gezeichnet wird
    // hier angles[1]. Einen Nickwinkel zu setzen, den niemand zeichnet,
    // waere eine Zeile, die nichts tut und beim naechsten Lesen Fragen
    // aufwirft.
    if (st.hasWatchTarget && !roffFigurLaeuft) {
        const float dx = st.watchAt[0] - st.pos[0];
        const float dy = st.watchAt[1] - st.pos[1];
        if (dx * dx + dy * dy > 1.0F) {
            const float ziel =
                std::atan2(dy, dx) * 180.0F / std::numbers::pi_v<float>;
            // Der kuerzere Weg, wie AngleDelta ihn liefert.
            float rest = ziel - st.watchFromYaw;
            while (rest > 180.0F) { rest -= 360.0F; }
            while (rest < -180.0F) { rest += 360.0F; }
            const auto verstrichen =
                static_cast<float>(std::max(0.0, ms - st.watchStartMs));
            const float geschafft = st.turnDegPerSec * verstrichen / 1000.0F;
            if (geschafft >= std::fabs(rest)) {
                st.angles[1] = ziel;      // angekommen
            } else {
                st.angles[1] = st.watchFromYaw +
                               ((rest < 0.0F) ? -geschafft : geschafft);
            }
        }
    }
    // --- Gehen und Laufen: nur, wenn der Timer der Beine es zulaesst ------
    //
    // PM_Footsteps setzt BOTH_WALK1/BOTH_RUN1 ohne SETANIM_FLAG_OVERRIDE,
    // und PM_SetAnimFinal spielt eine Beinanimation nur, wenn der Timer
    // abgelaufen ist (bg_panimate.cpp: bodyPlay = animOverride ||
    // !bodyTimerOn). Eine Skriptanimation mit laufendem Timer bleibt also
    // stehen, und die Figur gleitet mit ihr zum Ziel. Erst wenn der Timer
    // ablaeuft, beginnt die Laufanimation.
    //
    // Vorher loeste jeder Weg die Skriptanimation sofort ab.
    const auto timerHaelt = [&]() {
        return beineBis >= 0.0 && ms < beineBis &&
               (beineFest || beineNr > wegNr);
    };
    if (st.moving && weg != nullptr && !weg->text.empty()) {
        if (!timerHaelt()) {
            double ab = weg->startMs;
            if (beineBis >= 0.0 && beineBis < kNie && beineBis > ab) {
                ab = beineBis;   // die Laufanimation beginnt mit dem Ablauf
            }
            // Kein Uebergang fuer die LAUFanimation - die waehlt im Spiel
            // die Engine aus dem Bewegungszustand; hier wird sie aus
            // SET_WALKING abgeleitet. Ein laufender Uebergang zeigte auf
            // ein anderes Paar und faellt weg.
            st.animation = weg->text;
            st.prevAnimation.clear();
            // Die Animation laeuft MIT dem Tempo - siehe kAnimSpeedWalk in
            // scene.h. Ohne das rutschen die Fuesse ueber den Boden.
            st.animSpeed = (weg->value > 0.0F) ? weg->value : 1.0F;
            // Die Grenze der Engine gilt fuer das ABSOLUTE Tempo gegenueber
            // 20 Bildern je Sekunde (bg_panimate.cpp:4834: animSpeed =
            // 50/frameLerp * Verhaeltnis, hoechstens 1,5). BOTH_RUN1 hat
            // 30 fps - vorher lief es mit 45 fps, also 50 % zu schnell.
            const auto lerp = bildDauerMs.find(weg->text);
            if (lerp != bildDauerMs.end() && lerp->second > 0.0) {
                st.animSpeed = std::min(
                    st.animSpeed, static_cast<float>(1.5 * lerp->second / 50.0));
            }
            st.animStartMs = ab;
            beineBis = -1.0;
        }
    } else if (!st.moving && wegNr >= 0 && beineAbMs < gehEnde &&
               !timerHaelt() && !(beineBis > gehEnde && beineBis < kNie)) {
        // Nach dem Weg: die Grundhaltung ab dem Wegende. Das gilt auch,
        // wenn vor dem Weg eine Skriptanimation gesetzt war - der Weg hat
        // sie abgeloest, sie kommt danach nicht wieder.
        if (st.animation != "BOTH_STAND1") {
            st.animation = "BOTH_STAND1";
            st.animStartMs = gehEnde;
            st.prevAnimation.clear();
        }
        st.animSpeed = 1.0F;
        beineBis = -1.0;
    }
    // --- Abgelaufene Timer: zurueck in den Stand -------------------------
    if (beineBis >= 0.0 && beineBis < kNie && !st.moving && ms > beineBis &&
        st.animation != "BOTH_STAND1") {
        st.prevAnimation = st.animation;
        st.prevAnimSinceMs = beineBis - st.animStartMs;
        st.prevHoldAnim = true;
        st.blendStartMs = beineBis;
        st.blendMs = kSetAnimBlendDefault;
        st.animation = "BOTH_STAND1";
        st.animStartMs = beineBis;
        st.animSpeed = 1.0F;
    }
    if (torsoBis >= 0.0 && torsoBis < kNie && ms > torsoBis && !st.torsoAnimation.empty()) {
        // Der Oberkoerper folgt wieder den Beinen - mit Uebergang von
        // seinem letzten Bild, wie jeder Knochenwechsel.
        st.torsoPrevAnimation = st.torsoAnimation;
        st.torsoPrevSinceMs = torsoBis - st.torsoStartMs;
        st.torsoBlendStartMs = torsoBis;
        st.torsoBlendMs = kSetAnimBlendDefault;
        st.torsoAnimation.clear();
    }
    // Im Hocken ein anderer Grundzustand.
    //
    // Diese drei Nachbesserungen sind UNSERE Naeherung, kein Ereignis der
    // Engine. Schreiben sie die Animation um, zeigt ein laufender Uebergang
    // auf ein Paar, das es so nie gab - also faellt er weg.
    if (!st.moving && st.crouched &&
        (st.animation == "BOTH_STAND1" || st.animation.empty())) {
        st.animation = "BOTH_CROUCH1IDLE";
        st.prevAnimation.clear();
    }
    // Hier stand eine Nachbesserung: jede BOTH_WALK*/BOTH_RUN*-Animation
    // einer stehenden Figur wurde zu BOTH_STAND1. Sie fing die Laufanimation
    // ab, die nach einem Weg stehen blieb - das tut jetzt der Abschnitt
    // "Gehen und Laufen" oben, am Wegende und mit Timer. Die Nachbesserung
    // traf aber auch eine GESKRIPTETE Geh-Animation (SET_ANIM_BOTH
    // BOTH_WALK1 auf der Stelle), und die spielt die Engine sehr wohl.
    // --- Mit brennender Klinge: die Kampfhaltung -------------------------
    //
    // Nur in der Grundhaltung - eine Skriptanimation mit laufendem Timer
    // (auch ein geskriptetes BOTH_STAND1) bleibt, wie sie ist.
    if (!st.moving && !st.crouched && st.animation == "BOTH_STAND1" &&
        (beineBis < 0.0 || (beineBis < kNie && ms > beineBis))) {
        saberGrundhaltung(*this, st, ms);
    }
    ausruestungAbschluss(*this, st);
    for (const auto& [von, bis] : kameraVerborgen) {
        if (ms >= von && (bis < 0.0 || ms < bis)) {
            st.visible = false;
        }
    }
    return st;
}

float blendFraction(const ActorState& st, double ms) noexcept {
    if (st.prevAnimation.empty() || st.blendMs <= 0.0) {
        return 1.0F;
    }
    const double seit = ms - st.blendStartMs;
    // Die Grenzen der Engine, tr_ghoul2.cpp:1357:
    //
    //     if (currentTime > bone.blendStart &&
    //         currentTime < bone.blendStart + bone.blendTime)
    //
    // Davor und danach zaehlt allein die neue Animation. Ohne diese
    // Klammer liefe der Uebergang bei negativer Zeit rueckwaerts und nach
    // dem Ende ueber die neue Stellung hinaus.
    if (seit < 0.0 || seit >= st.blendMs) {
        return 1.0F;
    }
    return static_cast<float>(seit / st.blendMs);
}

float torsoBlendFraction(const ActorState& st, double ms) noexcept {
    if (st.torsoPrevAnimation.empty() || st.torsoBlendMs <= 0.0) {
        return 1.0F;
    }
    const double seit = ms - st.torsoBlendStartMs;
    if (seit < 0.0 || seit >= st.torsoBlendMs) {
        return 1.0F;   // dieselben Grenzen wie blendFraction
    }
    return static_cast<float>(seit / st.torsoBlendMs);
}

void parseNpcFile(const std::string& text, NpcMap& out) {
    // Aufbau wie bei den Shaderskripten: Name, geschweifte Klammer, Zeilen
    // mit Schluessel und Wert.
    std::string name;
    int depth = 0;
    // Was der gerade gelesene Block sagt. customSkin bleibt LEER, bis ein
    // Schluessel kommt - die Vorgabe "default" wird erst beim Ablegen
    // eingesetzt, damit "der erste gewinnt" weiter gilt.
    NpcDef cur;
    cur.customSkin.clear();
    std::size_t at = 0;

    auto trim = [](std::string v) {
        std::size_t a = 0;
        std::size_t e = v.size();
        auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
        while (a < e && sp(v[a])) { ++a; }
        while (e > a && sp(v[e - 1])) { --e; }
        return v.substr(a, e - a);
    };
    const auto neu = [&cur]() {
        cur = NpcDef{};
        cur.customSkin.clear();
    };

    while (at < text.size()) {
        std::size_t eol = text.find('\n', at);
        if (eol == std::string::npos) {
            eol = text.size();
        }
        std::string line = trim(text.substr(at, eol - at));
        at = eol + 1;
        const std::size_t slash = line.find("//");
        if (slash != std::string::npos) {
            line = trim(line.substr(0, slash));
        }
        if (line.empty()) {
            continue;
        }
        if (line == "{") {
            ++depth;
            continue;
        }
        if (line == "}") {
            --depth;
            // Fahrzeuge (CLASS_VEHICLE) nennen kein Modell - es kommt aus der
            // .veh (NPC_stats.cpp:4562); sie bleiben trotzdem stehen.
            std::string kl = cur.klasse;
            for (char& c : kl) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
            if (depth == 0 && !name.empty() &&
                (!cur.playerModel.empty() || !cur.legsModel.empty() || kl == "CLASS_VEHICLE")) {
                std::string key = name;
                for (char& c : key) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (cur.customSkin.empty()) {
                    cur.customSkin = "default";
                }
                out[key] = cur;
            }
            name.clear();
            neu();
            continue;
        }
        if (depth == 0) {
            name = line;
            neu();
            continue;
        }
        // playerModel <name>, saber <name>
        const std::size_t sp = line.find_first_of(" \t");
        if (sp == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, sp);
        for (char& c : key) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const std::string wert = trim(line.substr(sp + 1));
        // Wie in NPC_ParseParms (Q_strncpyz bei jeder Zeile): die letzte gilt.
        if (key == "playermodel") {
            cur.playerModel = wert;
        }
        if (key == "legsmodel") {
            cur.legsModel = wert;
        }
        // --- saber und saber2 ------------------------------------------
        //
        // Wie die Engine: JEDER saber-Schluessel ruft WP_SaberParseParms
        // und setzt das Schwert neu auf (NPC_stats.cpp:4073 ff.) - der
        // LETZTE gewinnt, und Farben, Laengen und Radien, die VORHER
        // standen, sind danach fort. `saber1` kennt NPC_ParseParms nicht.
        //
        // Bis hierher gewann der erste und `saber1` galt als Synonym. Ueber
        // die .npc-Dateien der Mod nachgezaehlt: kein Block nennt zwei
        // saber-Schluessel, keiner `saber1`, keiner eine Farbe vor dem
        // Schwert. Am Ergebnis aendert sich dort also nichts.
        const auto schwertNeu = [&cur](int fach) {
            const auto f = static_cast<std::size_t>(fach);
            cur.saberFarbe[f] = {};
            cur.saberLaenge[f] = {};
            cur.saberRadius[f] = {};
        };
        if (key == "saber") {
            cur.saber = wert;
            schwertNeu(0);
        }
        if (key == "saber2") {
            cur.saber2 = wert;
            schwertNeu(1);
        }
        // saberColor, saberColor2..8, saber2Color, saber2Color2..8 und die
        // RGB-Schreibweisen (NPC_stats.cpp:4128 ff.). Ohne Ziffer: alle
        // acht Klingen. Mit Ziffer N: Klinge N-1, und nur 2..8 gelten -
        // "atoi - 1" muss zwischen 1 und 7 liegen, saberColor1 wird mit
        // einer Warnung verworfen.
        //
        // Nicht nachgebildet: forced_rgb_saber_colours. Ein saberColorRGB
        // schuetzt dort seine Klingen gegen ein SPAETERES saberColor; hier
        // gilt einfach das spaetere. In den Dateien der Mod steht keine
        // RGB-Schreibweise.
        const auto jeKlinge = [&key](const char* vorsatz, int& klinge) {
            const std::string v = vorsatz;
            if (key.compare(0, v.size(), v) != 0) {
                return false;
            }
            std::string rest = key.substr(v.size());
            if (rest.compare(0, 3, "rgb") == 0) {
                rest = rest.substr(3);
            }
            if (rest.empty()) {
                klinge = -1;
                return true;
            }
            if (rest.size() != 1 || rest[0] < '2' || rest[0] > '8') {
                klinge = -2;   // falsche Ziffer: die Engine verwirft es
                return true;
            }
            klinge = rest[0] - '1';
            return true;
        };
        const auto setze = [](auto& feld, int klinge, const auto& v) {
            if (klinge == -1) {
                for (auto& x : feld) { x = v; }
            } else if (klinge >= 0 && klinge < kMaxKlingen) {
                feld[static_cast<std::size_t>(klinge)] = v;
            }
        };
        int klinge = -2;
        // Die laengeren Vorsaetze zuerst: "saber2color" beginnt nicht mit
        // "sabercolor", aber "sabercolorrgb" mit "sabercolor".
        if (jeKlinge("saber2color", klinge)) {
            setze(cur.saberFarbe[1], klinge, wert);
        } else if (jeKlinge("sabercolor", klinge)) {
            setze(cur.saberFarbe[0], klinge, wert);
        } else if (jeKlinge("saber2length", klinge)) {
            // "cap": unter 4 wird 4 (ebenda:4337).
            setze(cur.saberLaenge[1], klinge,
                  std::max(4.0F, static_cast<float>(std::atof(wert.c_str()))));
        } else if (jeKlinge("saberlength", klinge)) {
            setze(cur.saberLaenge[0], klinge,
                  std::max(4.0F, static_cast<float>(std::atof(wert.c_str()))));
        } else if (jeKlinge("saber2radius", klinge)) {
            // "cap": unter 0.25 wird 0.25 (ebenda:4424).
            setze(cur.saberRadius[1], klinge,
                  std::max(0.25F, static_cast<float>(std::atof(wert.c_str()))));
        } else if (jeKlinge("saberradius", klinge)) {
            setze(cur.saberRadius[0], klinge,
                  std::max(0.25F, static_cast<float>(std::atof(wert.c_str()))));
        }
        // Jede weapon-Zeile setzt ps.weapon neu und traegt die Waffe in
        // ps.weapons ein (NPC_stats.cpp:3860 ff.) - die LETZTE ist die in
        // der Hand. md_ben_hooded nennt "WP_MELEE", dann "WP_SABER"; vorher
        // galt die erste, und Ben stand ohne Schwert da.
        if (key == "weapon") {
            cur.weapon = wert;
            std::string w = wert;
            for (char& c : w) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
            if (w == "WP_SABER") {
                cur.saberImInventar = true;
            }
        }
        if (key == "playerteam") {
            cur.team = wert;
        }
        // saberStyle, gekappt wie NPC_ParseParms (NPC_stats.cpp:4506).
        if (key == "saberstyle") {
            const long n = std::strtol(wert.c_str(), nullptr, 10);
            cur.saberStil = static_cast<int>(std::clamp(n, 1L, 7L));
        }
        if (key == "class") {
            cur.klasse = wert;
        }
        if (key == "rank") {
            cur.rang = wert;
        }
        // kotorWeapons und dualPistols: JEDE Zahl schaltet an - auch 0
        // (NPC_stats.cpp:4059 ff. setzt 1, sobald eine Zahl dasteht).
        if (key == "kotorweapons") {
            cur.kotorWeapons = true;
        }
        if (key == "dualpistols") {
            cur.dualPistols = true;
        }
        if (key == "customskin") {
            cur.customSkin = wert;
        }
        // walkSpeed / runSpeed (NPC_ParseParms, dl_NPC_stats.cpp:3293 ff.).
        // Ohne sie gingen alle Figuren mit 90 und liefen mit 300 - die
        // Wege dauerten dadurch anders lange als im Spiel.
        if (key == "walkspeed") {
            cur.walkSpeed = static_cast<float>(std::atof(wert.c_str()));
        }
        if (key == "runspeed") {
            cur.runSpeed = static_cast<float>(std::atof(wert.c_str()));
        }
    }
}

void parseWeaponsDat(const std::string& text, WeaponMap& out) {
    // Aufbau: Kommentare mit //, dann Bloecke in geschweiften Klammern.
    // Anders als bei .npc und .sab steht der NAME nicht vor der Klammer,
    // sondern als Schluessel `weapontype` DARIN.
    WeaponDef def;
    std::string typ;
    int depth = 0;
    std::size_t at = 0;

    auto trim = [](std::string v) {
        std::size_t a = 0;
        std::size_t e = v.size();
        auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
        while (a < e && sp(v[a])) { ++a; }
        while (e > a && sp(v[e - 1])) { --e; }
        return v.substr(a, e - a);
    };

    while (at < text.size()) {
        std::size_t eol = text.find('\n', at);
        if (eol == std::string::npos) {
            eol = text.size();
        }
        std::string line = trim(text.substr(at, eol - at));
        at = eol + 1;
        const std::size_t slash = line.find("//");
        if (slash != std::string::npos) {
            line = trim(line.substr(0, slash));
        }
        if (line.empty()) {
            continue;
        }
        if (line == "{") {
            ++depth;
            typ.clear();
            def = WeaponDef{};
            continue;
        }
        if (line == "}") {
            --depth;
            if (depth == 0 && !typ.empty()) {
                out[typ] = def;
            }
            typ.clear();
            def = WeaponDef{};
            continue;
        }
        if (depth == 0) {
            continue;
        }
        const std::size_t sp2 = line.find_first_of(" \t");
        if (sp2 == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, sp2);
        for (char& c : key) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const std::string val = trim(line.substr(sp2 + 1));
        if (key == "weapontype") { typ = val; }
        else if (key == "weaponmodel") { def.model = val; }
        else if (key == "altweaponmodel") { def.altModel = val; }
        else if (key == "muzzleeffect") { def.muzzleEffect = val; }
        else if (key == "altmuzzleeffect") { def.altMuzzleEffect = val; }
        else if (key == "firetime") {
            def.fireTimeMs = std::atoi(val.c_str());
        }
    }
}

void parseSaberFile(const std::string& text, SaberMap& out) {
    // Derselbe Aufbau wie .npc: Name, geschweifte Klammer, Zeilen mit
    // Schluessel und Wert. Die Schluesselnamen stehen in
    // wp_saberLoad.cpp:1827 ff.
    std::string name;
    int depth = 0;
    SaberDef def;
    std::size_t at = 0;

    auto trim = [](std::string v) {
        std::size_t a = 0;
        std::size_t e = v.size();
        auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
        while (a < e && sp(v[a])) { ++a; }
        while (e > a && sp(v[e - 1])) { --e; }
        // Werte stehen oft in Anfuehrungszeichen, oft aber auch nicht -
        // saberColor und saberLength kommen nackt. Beides mitnehmen.
        std::string r = v.substr(a, e - a);
        if (r.size() >= 2 && r.front() == '"' && r.back() == '"') {
            r = r.substr(1, r.size() - 2);
        }
        return r;
    };

    while (at < text.size()) {
        std::size_t eol = text.find('\n', at);
        if (eol == std::string::npos) {
            eol = text.size();
        }
        std::string line = trim(text.substr(at, eol - at));
        at = eol + 1;
        const std::size_t slash = line.find("//");
        if (slash != std::string::npos) {
            line = trim(line.substr(0, slash));
        }
        if (line.empty()) {
            continue;
        }
        if (line == "{") {
            ++depth;
            continue;
        }
        if (line == "}") {
            --depth;
            if (depth == 0 && !name.empty()) {
                std::string key = name;
                for (char& c : key) {
                    c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }
                out[key] = def;
            }
            name.clear();
            def = SaberDef{};
            continue;
        }
        if (depth == 0) {
            name = line;
            def = SaberDef{};
            continue;
        }
        const std::size_t sp2 = line.find_first_of(" \t");
        if (sp2 == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, sp2);
        for (char& c : key) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const std::string val = trim(line.substr(sp2 + 1));
        if (key == "name") { def.name = val; }
        else if (key == "sabermodel") { def.model = val; }
        else if (key == "soundon") { def.soundOn = val; }
        else if (key == "soundloop") { def.soundLoop = val; }
        else if (key == "soundoff") { def.soundOff = val; }
        // Die Kampfstile (Saber_ParseSaberStyle und Verwandte,
        // wp_saberLoad.cpp:1320 ff.; die Namen aus TranslateSaberStyle).
        else if (key == "saberstyle" || key == "saberstylelearned" ||
                 key == "saberstyleforbidden" || key == "singlebladestyle") {
            std::string n = val;
            for (char& c : n) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            static const char* const kStile[] = {"", "fast", "medium", "strong", "desann",
                                                 "tavion", "dual", "staff"};
            int stil = 0;
            for (int s = 1; s < 8; ++s) {
                if (n == kStile[s]) {
                    stil = s;
                }
            }
            if (key == "saberstyle") {
                def.stylesLearned = 1 << stil;
                def.stylesForbidden = 0;
                for (int s = 1; s < 8; ++s) {
                    if (s != stil) {
                        def.stylesForbidden |= 1 << s;
                    }
                }
            } else if (key == "saberstylelearned") {
                def.stylesLearned |= 1 << stil;
            } else if (key == "saberstyleforbidden") {
                def.stylesForbidden |= 1 << stil;
            } else {
                def.singleBladeStyle = stil;
            }
        }
        else if (key == "sabercolor") { def.color = val; }
        else if (key == "saberlength") {
            try { def.length = std::stof(val); } catch (...) {}
        }
        else if (key == "saberradius") {
            try { def.radius = std::stof(val); } catch (...) {}
        }
        // --- Je Klinge (wp_saberLoad.cpp:860 ff.) ------------------------
        //
        // saberColor/saberLength/saberRadius ohne Ziffer setzen ALLE acht
        // Klingen, mit Ziffer N (2..7 - mehr kennt die Schluesseltabelle
        // ebenda:2718 ff. nicht) nur Klinge N-1. Laenge unter 4 wird 4,
        // Radius unter 0.25 wird 0.25 - wie Saber_ParseSaberLength und
        // Saber_ParseSaberRadius. Die Felder oben (color, length, radius)
        // bleiben daneben, wie sie waren: roh und nur aus dem Schluessel
        // ohne Ziffer.
        const auto zahl = [&val]() {
            return std::strtof(val.c_str(), nullptr);
        };
        const auto jeKlinge = [&key](const char* vorsatz, int& klinge) {
            const std::string v = vorsatz;
            if (key.compare(0, v.size(), v) != 0) {
                return false;
            }
            std::string rest = key.substr(v.size());
            if (v == "sabercolor" && rest.compare(0, 3, "rgb") == 0) {
                rest = rest.substr(3);
            }
            if (rest.empty()) {
                klinge = -1;
                return true;
            }
            if (rest.size() != 1 || rest[0] < '2' || rest[0] > '7') {
                return false;   // kein Schluessel der Tabelle
            }
            klinge = rest[0] - '1';
            return true;
        };
        const auto setze = [](auto& feld, int klinge, const auto& v) {
            if (klinge < 0) {
                for (auto& x : feld) { x = v; }
            } else {
                feld[static_cast<std::size_t>(klinge)] = v;
            }
        };
        int klinge = -1;
        if (jeKlinge("sabercolor", klinge)) {
            setze(def.bladeColor, klinge, val);
        } else if (jeKlinge("saberlength", klinge)) {
            setze(def.bladeLength, klinge, std::max(4.0F, zahl()));
        } else if (jeKlinge("saberradius", klinge)) {
            setze(def.bladeRadius, klinge, std::max(0.25F, zahl()));
        }
        if (key == "sabertype") {
            def.type = val;
            for (char& c : def.type) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        } else if (key == "numblades") {
            // Ausserhalb 1..8 bricht die Engine mit Com_Error ab
            // (Saber_ParseNumBlades); hier bleibt die Angabe unbeachtet.
            const long n = std::strtol(val.c_str(), nullptr, 10);
            if (n >= 1 && n <= kMaxKlingen) {
                def.numBlades = static_cast<int>(n);
            }
        } else if (key == "bladestyle2start") {
            def.bladeStyle2Start = static_cast<int>(std::strtol(val.c_str(), nullptr, 10));
        } else if (key == "twohanded") {
            def.twoHanded = std::strtol(val.c_str(), nullptr, 10) != 0;
        } else if (key == "bolttowrist") {
            def.boltToWrist = std::strtol(val.c_str(), nullptr, 10) != 0;
        } else if (key == "noblade") {
            def.noBlade = std::strtol(val.c_str(), nullptr, 10) != 0;
        } else if (key == "noblade2") {
            def.noBlade2 = std::strtol(val.c_str(), nullptr, 10) != 0;
        }
    }
}

// Der eigentliche Aufbau.
//
// signals kommt fertig von aussen (clock.cpp, collectSignals) - frueher gab
// es dafuer einen zweiten Durchgang durch DIESE Funktion, den aber nur die
// Szene kannte. Jetzt teilen sich alle drei Auswerter dieselbe Sammlung.
Scene buildOnce(const Script& s, const MapData& map,
                const std::map<std::string, double>& signals,
                const SzenenHilfe* hilfe) {
    Scene out;

    // Jeder affect-Block gehoert zu einer Figur. Die Uhr laeuft dabei auf
    // der OBERSTEN Ebene weiter - genau wie in timeline.h: ein affect haelt
    // den Ablauf nicht an, sein Inhalt laeuft parallel.
    double now = 0.0;

    // --- Entfernte Figuren, erst am Schluss zugeordnet -------------------
    //
    // `remove ( "win1" )` nennt ein ZIEL, und das ist fast nie die Figur des
    // umgebenden affect-Blocks: in den 40 Skripten dieser Mission stehen 38
    // der 56 Entfernungen INNERHALB eines affect, und sie treffen jemand
    // anderen.
    //
    // Die Schritte darum nicht sofort setzen, sondern hier merken. Zwei
    // Gruende, und beide sind handfest:
    //
    //   1. `actor` ist ein Zeiger in out.actors. Waehrend der Schleife eine
    //      weitere Figur anzulegen, weil das Ziel noch keine hat, laesst
    //      diesen Zeiger ins Leere zeigen - der Vektor waechst um.
    //   2. Ein Skript darf jemanden entfernen, dessen eigener affect-Block
    //      erst SPAETER kommt. Zur Entfernungszeit gibt es die Figur noch
    //      nicht.
    //
    // Name und Zeitpunkt, in Reihenfolge.
    std::vector<std::pair<std::string, double>> entfernt;
    // kill ist NICHT remove: die Figur stirbt und bleibt liegen (siehe
    // unten bei der Zuordnung).
    std::vector<std::pair<std::string, double>> getoetet;

    for (std::size_t i = 0; i < s.nodes.size(); ++i) {
        const Node& n = s.nodes[i];
        if (n.kind != Node::Kind::Command) {
            continue;
        }
        if (n.name == "wait" && !n.args.empty() &&
            n.args[0].kind != Arg::Kind::String) {
            now += readMs(n.args[0].text);
            continue;
        }
        // remove / kill auf der OBERSTEN Ebene.
        //
        // CQuake3GameInterface::Remove (Q3_Interface.cpp:9712) sucht das
        // Ziel ueber targetname und entfernt ALLE, die so heissen - eine
        // while-Schleife ueber G_Find. Danach ist die Entity fort und kommt
        // nicht wieder.
        //
        // "self" und "enemy" sind Sonderfaelle. Auf oberster Ebene gibt es
        // kein self - der Skripttraeger ist keine Figur -, also uebergehen
        // wir sie hier.
        if ((n.name == "remove" || n.name == "kill") && !n.args.empty()) {
            const std::string& ziel = n.args[0].text;
            if (ziel != "self" && ziel != "enemy" && !ziel.empty()) {
                (n.name == "kill" ? getoetet : entfernt).emplace_back(ziel, now);
            }
            continue;
        }
        if (n.name != "affect" || n.args.empty()) {
            continue;
        }

        const std::string name = n.args[0].text;
        Actor* actor = nullptr;
        for (Actor& a : out.actors) {
            if (a.name == name) {
                actor = &a;
            }
        }
        if (actor == nullptr) {
            Actor fresh;
            fresh.name = name;
            // Startort aus dem Spawner.
            // Erst der Spawner (der traegt den NPC_type), sonst irgendeine
            // Entity dieses Namens - Tuer, Plattform, Raumschiff.
            const MapEntity* sp = findSpawner(map, name);
            if (sp == nullptr) {
                sp = findNamed(map, name);
            }
            // Der Spieler: kein Spawner, er steht beim Laden am
            // info_player_start (SP_info_player_start / SelectSpawnPoint).
            const bool istSpieler = sp == nullptr && sameName(name, "player");
            if (istSpieler) {
                for (const MapEntity& e : map.entities) {
                    if (e.classname == "info_player_start") {
                        sp = &e;
                        break;
                    }
                }
                // Ohne info_player_start (Duell- und MP-Karten): wie
                // SelectSpawnPoint ein anderer Startpunkt.
                for (const MapEntity& e : map.entities) {
                    if (sp == nullptr && e.classname.rfind("info_player_", 0) == 0 && !e.origin.empty()) {
                        sp = &e;
                    }
                }
            }
            if (sp != nullptr) {
                if (!sp->origin.empty()) {
                    readVec(sp->origin, fresh.start);
                    fresh.haveStart = true;
                } else if (const std::string* mdl0 = sp->find("model")) {
                    // Ein Brush-Modell OHNE origin-Brush: currentOrigin ist
                    // dann 0 0 0, und Q3_Lerp2Pos bewegt es ganz normal von
                    // dort. Vorher blieb so eine Entity stehen - die meisten
                    // skriptgesteuerten func_door und func_static sind so
                    // gebaut.
                    if (!mdl0->empty() && (*mdl0)[0] == '*') {
                        fresh.haveStart = true;
                    }
                }
                if (const std::string* lin = sp->find("linear")) {
                    fresh.linearBewegung = (std::atoi(lin->c_str()) != 0);
                }
                // Blickrichtung: die Engine kennt ZWEI Schluessel dafuer.
                //
                // code/game/g_spawn.cpp, Feldtabelle:
                //     {"angles", FOFS(s.angles), F_VECTOR},
                //     {"angle",  FOFS(s.angles), F_ANGLEHACK},
                //
                // Beide schreiben in dasselbe Feld. "angles" nimmt alle drei
                // Werte, "angle" nur den Gierwinkel - und setzt die anderen
                // beiden ausdruecklich auf null (G_ParseField, F_ANGLEHACK:
                // [0] = 0; [1] = v;). Wer beide angibt, bei dem gewinnt der
                // SPAETERE, weil die Engine die Schluessel der Reihe nach
                // abarbeitet.
                //
                // Vorher stand hier nur "angle". Eine Karte mit
                // "angles" "0 270 0" - und das ist die haeufigere
                // Schreibweise - liess jede Figur nach Norden schauen.
                for (const auto& [k, v] : sp->keys) {
                    std::string lk = k;
                    for (char& c : lk) {
                        c = static_cast<char>(std::tolower(
                            static_cast<unsigned char>(c)));
                    }
                    if (lk == "angles") {
                        readVec(v, fresh.startAngles);
                    } else if (lk == "angle") {
                        float gier = 0.0F;
                        if (parseFloat(v, gier)) {
                            fresh.startAngles[0] = 0.0F;
                            fresh.startAngles[1] = gier;
                            fresh.startAngles[2] = 0.0F;
                        }
                    }
                }
                // func_door, func_plat und func_train LOESCHEN ihre Winkel:
                // "angle" ist dort die Bewegungsrichtung, keine Stellung
                // (G_SetMovedir, g_utils.cpp:713; g_mover.cpp:1646, 2084).
                if (sp->classname == "func_door" || sp->classname == "func_plat" ||
                    sp->classname == "func_train") {
                    for (float& w : fresh.startAngles) { w = 0.0F; }
                }
                // "model" "*12" -> Brush-Modell 12.
                if (const std::string* mdl = sp->find("model")) {
                    if (!mdl->empty() && (*mdl)[0] == '*') {
                        int nummer = -1;
                        fresh.brushModel =
                            parseInt(mdl->substr(1), nummer) ? nummer : -1;
                    }
                }
                if (const std::string* t = sp->find("NPC_type")) {
                    fresh.npcType = *t;
                } else if (isNpcSpawner(*sp)) {
                    // Kein NPC_type am Spawner: dann sagt der Klassenname
                    // ihn. NPC_spawner selbst sagt nichts - dort MUSS einer
                    // stehen.
                    //
                    // Die Abfrage auf NPC_* ist nicht ueberfluessig: seit
                    // affect auch Tueren und Plattformen trifft, kommt hier
                    // auch "func_static" an - und typeFromClassname haette
                    // daraus stur "_static" gemacht, also einen Figurentyp,
                    // den es nicht gibt. Eine Probe hat es gefangen.
                    const std::string* sfs = sp->find("spawnflags");
                    fresh.npcType = typeFromClassname(sp->classname, sfs == nullptr ? 0 : std::atoi(sfs->c_str()));
                }
                if (istSpieler) {
                    fresh.npcType = "player";
                }
            }
            // walkSpeed/runSpeed aus der .npc (Vorgabe 90/300).
            const NpcDef* npcDef = nullptr;
            if (hilfe != nullptr && hilfe->npcs != nullptr && !fresh.npcType.empty()) {
                for (const auto& [typ, def] : *hilfe->npcs) {
                    if (typ.size() != fresh.npcType.size()) { continue; }
                    bool gleich = true;
                    for (std::size_t z = 0; z < typ.size(); ++z) {
                        if (std::tolower(static_cast<unsigned char>(typ[z])) !=
                            std::tolower(static_cast<unsigned char>(fresh.npcType[z]))) {
                            gleich = false;
                            break;
                        }
                    }
                    if (!gleich) { continue; }
                    if (def.walkSpeed > 0.0F) { fresh.gehTempo = def.walkSpeed; }
                    if (def.runSpeed > 0.0F) { fresh.laufTempo = def.runSpeed; }
                    npcDef = &def;
                    break;
                }
            }
            // Waffe und Schwerter beim Spawnen. spawnflags braucht es nur
            // fuer die Vorgabewaffe (SFB_RIFLEMAN, SFB_PHASER).
            int spawnflags = 0;
            if (sp != nullptr) {
                if (const std::string* sf = sp->find("spawnflags")) {
                    spawnflags = std::atoi(sf->c_str());
                }
            }
            ausstattungBeimSpawnen(fresh, npcDef, hilfe, spawnflags);
            out.actors.push_back(std::move(fresh));
            actor = &out.actors.back();
        }

        // Den Block durchgehen. Die Uhr der Figur beginnt beim Stand des
        // affect und laeuft in ihrem eigenen Takt weiter.
        double t = now;
        // Gehen, wenn WALKING gesetzt und RUNNING nicht (RUNNING geht vor).
        const auto gehend = [actor]() { return actor->gehSchalter && !actor->laufSchalter; };
        // --- Die Geschwindigkeiten der Figur ----------------------------
        //
        // SET_WALKSPEED kommt in md_ga_jedi 34 mal vor, SET_RUNSPEED 26 mal
        // - und behaved hat beide bisher uebergangen und stur mit 90 bzw.
        // 300 gerechnet.
        //
        // Sie werden HIER mitgefuehrt und nicht zur Auswertungszeit, weil
        // die Dauer eines SET_NAVGOAL beim Einlesen festgelegt wird. Das
        // ist eine Vereinfachung: setzt ein Skript die Geschwindigkeit erst
        // WAEHREND des Gehens um, rechnet die Engine ab da anders, wir
        // nicht. In den 40 Skripten dieser Mission kommt das nicht vor -
        // die Geschwindigkeit steht immer vor dem Wegziel.
        // Pro FIGUR, nicht pro Block - ein SET_WALKSPEED gilt weiter.
        float& walkSpeed = actor->gehTempo;
        float& runSpeed = actor->laufTempo;
        float here[3] = {actor->start[0], actor->start[1], actor->start[2]};
        // Die Blickrichtung laeuft mit: rotate dreht ab dem AKTUELLEN
        // Winkel, nicht ab null.
        float facing[3] = {actor->startAngles[0], actor->startAngles[1],
                           actor->startAngles[2]};
        // Wo steht die Figur am Anfang dieses Blocks? Der letzte bekannte
        // Ort aus den bisherigen Schritten.
        if (!actor->steps.empty()) {
            const ActorState prev = actor->at(now);
            for (int k = 0; k < 3; ++k) {
                here[k] = prev.pos[k];
                // Auch die Winkel: ein rotate in einem frueheren affect
                // gilt weiter (currentAngles der Engine).
                facing[k] = prev.angles[k];
            }
        }

        // --- In die Bloecke HINEINSTEIGEN --------------------------------
        //
        // Vorher lief diese Stelle nur ueber die unmittelbaren Kinder des
        // affect. Alles, was in einem Block steckt, war damit unsichtbar -
        // und das ist der Normalfall: in den 64 Skripten aus
        // MD_Maps_Ep1/2/4/6 stehen do 56 mal, task 42 mal, if 24 mal und
        // loop 6 mal. Der Flyby der Droidenjaeger hat GENAU EINEN Befehl
        // ausserhalb seines loop - alles andere fiel unter den Tisch.
        //
        // Wie es ausgewertet wird:
        //   loop / if / else   der Rumpf laeuft AN DIESER STELLE, also
        //                      einfach weitergehen. Ein loop wird EINMAL
        //                      gezeigt - bei loop(-1) gaebe es sonst keine
        //                      Vorschau, sondern eine Endlosschleife.
        //   task ( "name" )    der Rumpf laeuft NICHT hier, sondern erst
        //                      beim passenden do. Also nur merken.
        //   do ( "name" )      jetzt den gemerkten Rumpf durchgehen.
        //
        // Die Tiefe ist begrenzt: ein task, das sich selbst per do aufruft,
        // wuerde sonst nicht zurueckkehren.
        std::map<std::string, const Node*> tasks;
        std::function<void(const std::vector<Node>&, const Path&, int)> run;
        // base als const& - ein Path ist ein Vektor, und er wurde bei
        // jedem Aufruf kopiert, obwohl er nur gelesen wird.
        run = [&](const std::vector<Node>& kids, const Path& base, int depth) {
        if (depth > 8) {
            return;
        }
        for (std::size_t c = 0; c < kids.size(); ++c) {
            const Node& child = kids[c];
            if (child.kind != Node::Kind::Command) {
                continue;
            }
            Path p = base;
            p.push_back(c);

            if (child.name == "task" && !child.args.empty()) {
                tasks[child.args[0].text] = &child;
                continue;
            }
            if (child.name == "do" && !child.args.empty()) {
                const auto it = tasks.find(child.args[0].text);
                if (it != tasks.end()) {
                    run(it->second->children, Path{}, depth + 1);
                }
                continue;
            }
            // dowait = do, dann wait auf denselben Task (ibi.cpp schreibt
            // es genau so ins .ibi). Vorher uebergangen: alles im Task fiel
            // weg, und was danach kam, lief zu frueh. Raven benutzt dowait
            // in 508 von 1510 Skripten.
            if (child.name == "dowait" && !child.args.empty()) {
                const auto it = tasks.find(child.args[0].text);
                if (it != tasks.end()) {
                    run(it->second->children, Path{}, depth + 1);
                }
                double eigenes = t;
                for (const ActorStep& done : actor->steps) {
                    eigenes = std::max(eigenes, done.endMs);
                }
                t = std::max(t, eigenes);
                continue;
            }
            if (child.hasBlock &&
                (child.name == "loop" || child.name == "if" ||
                 child.name == "else")) {
                run(child.children, p, depth + 1);
                continue;
            }

            // --- signal und waitsignal --------------------------------
            //
            // 39 mal signal und 39 mal waitsignal in den 284 Skripten der
            // Mod. Sie steuern die ZEIT: waitsignal haelt den Ablauf an,
            // bis irgendwo signal mit demselben Namen faellt.
            //
            // Ohne sie lief alles nach einem waitsignal zu frueh - und das
            // ist bei einer Zwischensequenz genau die Stelle, an der die
            // Figuren auf den Sprecher warten.
            //
            // Der erste Durchgang merkt sich, WANN ein Signal faellt; der
            // zweite spult beim Warten dorthin vor. Deshalb zwei Durchgaenge
            // und keine Vorwaertsschau: ein Signal kann in einem spaeteren
            // affect-Block fallen als das Warten darauf.
            // signal wird nur noch eingesammelt (clock.cpp), hier also
            // uebergangen.
            if (child.name == "signal") {
                continue;
            }
            // Warten - alle Spielarten ueber die gemeinsame Uhr.
            if (child.name == "waitsignal" ||
                (child.name == "wait" && !child.args.empty() &&
                 child.args[0].kind != Arg::Kind::String)) {
                t = advance(child, t, t, signals);
                continue;
            }
            // wait ( "name" ): bis zum Ende dessen, was dieser Ablauf
            // zuletzt angestossen hat. Die Regel steht in clock.cpp; hier
            // wird nur der eigene Endzeitpunkt dazugereicht.
            if (child.name == "wait" && !child.args.empty() &&
                child.args[0].kind == Arg::Kind::String) {
                double eigenes = t;
                for (const ActorStep& done : actor->steps) {
                    eigenes = std::max(eigenes, done.endMs);
                }
                t = advance(child, t, eigenes, signals);
                continue;
            }

            // --- move ( <ziel>, <winkel>, <dauer> ) --------------------
            //
            // 85 Verwendungen in den 64 Skripten aus MD_Maps_Ep1/2/4/6 -
            // haeufiger als jeder einzelne set-Befehl. Genau damit fliegen
            // die Schiffe und fahren die Plattformen.
            if (child.name == "move" && child.args.size() >= 2 &&
                child.args[0].kind == Arg::Kind::Vector) {
                ActorStep step;
                step.kind = ActorStep::Kind::Glide;
                step.startMs = t;
                step.linear = actor->linearBewegung;
                readVec(child.args[0].text, step.to);
                // Von der AKTUELLEN Stelle, auch mitten in einer Bewegung
                // (Q3_Lerp2Pos: VectorCopy( ent->currentOrigin, ent->pos1 )).
                // Vorher begann ein zweiter move am Ziel des ersten - ein
                // sichtbarer Sprung.
                const ActorState jetzt = actor->at(t);
                for (int k = 0; k < 3; ++k) {
                    step.from[k] = actor->steps.empty() ? here[k] : jetzt.pos[k];
                    step.fromAng[k] = actor->steps.empty() ? facing[k] : jetzt.angles[k];
                    step.toAng[k] = step.fromAng[k];
                }
                step.mitWinkel = false;
                std::size_t durAt = 1;
                if (child.args.size() >= 3 &&
                    child.args[1].kind == Arg::Kind::Vector) {
                    readVec(child.args[1].text, step.toAng);
                    step.mitWinkel = true;
                    durAt = 2;
                }
                const double dur = readMs(child.args[durAt].text);
                step.endMs = t + dur;
                step.path = p;
                for (int k = 0; k < 3; ++k) {
                    here[k] = step.to[k];
                    facing[k] = step.toAng[k];
                }
                actor->steps.push_back(std::move(step));
                // move WARTET NICHT - der Ablauf laeuft weiter. Wer warten
                // will, schreibt wait ( "name" ) dahinter. Genau so steht
                // es in den Skripten der Missionen.
                continue;
            }

            // --- rotate ( <winkel>, <dauer> ) --------------------------
            if (child.name == "rotate" && !child.args.empty() &&
                child.args[0].kind == Arg::Kind::Vector) {
                ActorStep step;
                step.kind = ActorStep::Kind::Turn;
                step.startMs = t;
                // Vom AKTUELLEN Winkel (Q3_Lerp2Angles: currentAngles).
                const ActorState jetzt = actor->at(t);
                for (int k = 0; k < 3; ++k) {
                    step.from[k] = here[k];
                    step.to[k] = here[k];
                    step.fromAng[k] = actor->steps.empty() ? facing[k] : jetzt.angles[k];
                }
                readVec(child.args[0].text, step.toAng);
                // Auch rotate faehrt gleichfoermig, wenn alt_fire gesetzt
                // ist - durch "linear" "1" ODER durch eine fruehere
                // ROFF-Bahn (Q3_Lerp2Angles, Q3_Interface.cpp:8689).
                step.linear = actor->linearBewegung;
                const double dur =
                    child.args.size() >= 2 ? readMs(child.args[1].text) : 0.0;
                step.endMs = t + dur;
                step.path = p;
                for (int k = 0; k < 3; ++k) {
                    facing[k] = step.toAng[k];
                }
                actor->steps.push_back(std::move(step));
                continue;
            }

            // --- play ( "PLAY_ROFF", "<datei>" ) -------------------------
            //
            // CQuake3GameInterface::Play (Q3_Interface.cpp:10683) kennt nur
            // diesen einen Typ. Die Bahn beginnt am AKTUELLEN Stand, auch
            // mitten in einem move (pos1/pos2 = currentOrigin/
            // currentAngles), und endet mit dem letzten Bild - dann ist
            // auch die Aufgabe fertig (TID_MOVE_NAV, g_roff.cpp:626).
            if (child.name == "play" && child.args.size() >= 2 &&
                sameName(child.args[0].text, "PLAY_ROFF")) {
                std::shared_ptr<const Roff> datei =
                    (hilfe != nullptr && hilfe->roff) ? hilfe->roff(child.args[1].text) : nullptr;
                if (!datei) {
                    continue;   // G_LoadRoff scheitert: nichts bewegt sich
                }
                ActorStep step;
                step.kind = ActorStep::Kind::Roff;
                step.startMs = t;
                step.endMs = t + roffLaufzeitMs(*datei);
                step.text = child.args[1].text;
                // Zwei Wege in G_Roff: mit client (NPC) oder ohne. Eine
                // Figur hat hier einen NPC_type, alles andere nicht.
                step.roffFigur = !actor->npcType.empty();
                const ActorState jetzt = actor->at(t);
                for (int k = 0; k < 3; ++k) {
                    step.from[k] = actor->steps.empty() ? here[k] : jetzt.pos[k];
                    step.fromAng[k] = actor->steps.empty() ? facing[k] : jetzt.angles[k];
                }
                auto bahn = std::make_shared<const RoffBahn>(roffBahn(*datei, step.from, step.fromAng));
                const RoffStand ende = roffEndstand(*datei, *bahn, step.roffFigur);
                for (int k = 0; k < 3; ++k) {
                    step.to[k] = ende.ort[k];
                    step.toAng[k] = ende.winkel[k];
                    here[k] = ende.ort[k];
                    facing[k] = ende.winkel[k];
                }
                // Die Notizen: was sie ausloesen, wann und wo.
                for (const RoffAusloesung& a : roffAusloesungen(*datei)) {
                    RoffEreignis e;
                    e.ms = t + a.ms;
                    e.figur = name;
                    e.datei = step.text;
                    e.text = datei->notizen[a.notiz];
                    e.befehl = zerlegeRoffNotiz(e.text);
                    const RoffStand basis = roffStandVorBild(*datei, *bahn, a.bild, step.roffFigur);
                    if (e.befehl.art == RoffNotizBefehl::Art::Effekt) {
                        roffEffektOrt(e.befehl, basis.ort, basis.winkel, e.ort, e.winkel);
                    } else {
                        for (int k = 0; k < 3; ++k) {
                            e.ort[k] = basis.ort[k];
                            e.winkel[k] = basis.winkel[k];
                        }
                    }
                    out.roffEreignisse.push_back(std::move(e));
                }
                // "make it true linear... FIXME: sticks around after ROFF
                // is done" (g_roff.cpp:597): alt_fire bleibt an, jedes
                // spaetere move und rotate dieser Entity faehrt
                // gleichfoermig. Nur ohne client - Figuren gehen den
                // anderen Weg.
                if (!step.roffFigur) {
                    actor->linearBewegung = true;
                }
                step.roff = std::move(datei);
                step.roffBahn = std::move(bahn);
                step.path = p;
                actor->steps.push_back(std::move(step));
                // play WARTET NICHT - erst ein wait/dowait auf den Task.
                continue;
            }

            if (const std::string* v = setValue(child, "SET_ORIGIN")) {
                ActorStep step;
                step.kind = ActorStep::Kind::Teleport;
                step.startMs = t;
                step.endMs = t;
                readVec(*v, step.to);
                for (int k = 0; k < 3; ++k) {
                    step.from[k] = here[k];
                    here[k] = step.to[k];
                }
                step.path = p;
                actor->steps.push_back(std::move(step));
                if (!actor->haveStart) {
                    for (int k = 0; k < 3; ++k) {
                        actor->start[k] = here[k];
                    }
                    actor->haveStart = true;
                }
                continue;
            }

            if (const std::string* v = setValue(child, "SET_ANGLES")) {
                ActorStep step;
                step.kind = ActorStep::Kind::Face;
                step.startMs = t;
                step.endMs = t;
                readVec(*v, step.to);
                step.path = p;
                // Das naechste rotate beginnt HIER (Q3_SetAngles setzt
                // currentAngles).
                for (int k = 0; k < 3; ++k) {
                    facing[k] = step.to[k];
                }
                actor->steps.push_back(std::move(step));
                continue;
            }

            if (const std::string* v = setValue(child, "SET_WALKING")) {
                actor->gehSchalter = (*v == "true");
                continue;
            }

            // SET_RUNNING ist das Gegenstueck - 304 Vorkommen in Ravens
            // Bestand, und ohne es liefe eine Figur, die ausdruecklich auf
            // Laufen gestellt wird, weiter im Gehtempo.
            if (const std::string* v = setValue(child, "SET_RUNNING")) {
                actor->laufSchalter = (*v == "true");
                continue;
            }

            if (const std::string* v = setValue(child, "SET_CROUCHED")) {
                ActorStep step;
                step.kind = ActorStep::Kind::Crouch;
                step.startMs = t;
                step.endMs = t;
                step.value = (*v == "true") ? 1.0F : 0.0F;
                step.path = p;
                actor->steps.push_back(std::move(step));
                continue;
            }

            // Wohin die Figur schaut. Das Ziel ist ein Entityname; ohne Ort
            // in der Karte laesst sich nichts damit anfangen.
            if (const std::string* v = setValue(child, "SET_LOOK_TARGET")) {
                ActorStep step;
                step.kind = ActorStep::Kind::Look;
                step.startMs = t;
                step.endMs = t;
                step.path = p;
                if (*v == "NULL" || v->empty() ||
                    !findEntity(map, *v, step.to)) {
                    step.value = 0.0F;   // Ziel loeschen
                } else {
                    step.value = 1.0F;
                    step.text = *v;
                }
                actor->steps.push_back(std::move(step));
                continue;
            }

            // Wen die Figur ansieht und sich dabei zudreht.
            //
            // Q3_SetWatchTarget (Q3_Interface.cpp:3063) loescht das Ziel bei
            // "NULL", bei "NONE" und wenn der eigene targetname genannt
            // wird - alle drei Faelle mitgenommen.
            if (const std::string* v = setValue(child, "SET_WATCHTARGET")) {
                ActorStep step;
                step.kind = ActorStep::Kind::Watch;
                step.startMs = t;
                step.endMs = t;
                step.path = p;
                const bool leer =
                    v->empty() || *v == "NULL" || *v == "NONE" ||
                    *v == "null" || *v == "none" || *v == name;
                if (leer || !findEntity(map, *v, step.to)) {
                    step.value = 0.0F;
                } else {
                    step.value = 1.0F;
                    step.text = *v;
                }
                actor->steps.push_back(std::move(step));
                continue;
            }

            // Die Haltezeit gehoert zur zuletzt gesetzten Animation.
            //
            // -1 heisst unbegrenzt: einfrieren statt wiederholen. 71 % aller
            // Vorkommen sind -1. Eine positive Zahl haelt so viele
            // Millisekunden, 0 hebt das Halten auf.
            for (const auto& [key, teil] :
                 {std::pair{"SET_ANIM_HOLDTIME_BOTH", AnimPart::Both},
                  std::pair{"SET_ANIM_HOLDTIME_UPPER", AnimPart::Upper},
                  std::pair{"SET_ANIM_HOLDTIME_LOWER", AnimPart::Lower}}) {
                if (const std::string* v = setValue(child, key)) {
                    ActorStep step;
                    step.kind = ActorStep::Kind::Hold;
                    step.startMs = t;
                    try {
                        step.value = std::stof(*v);
                    } catch (...) {
                        step.value = 0.0F;
                    }
                    step.endMs = (step.value > 0.0F) ? t + step.value : t;
                    step.part = teil;
                    step.path = p;
                    actor->steps.push_back(std::move(step));
                    break;
                }
            }

            // --- Die Geschwindigkeiten ----------------------------------
            //
            // Q3_SetWalkSpeed / Q3_SetRunSpeed (Q3_Interface.cpp:3370,
            // 3404) haben eine Eigenheit, die man nachlesen muss:
            //
            //     if(int_data == 0)
            //     {
            //         self->NPC->stats.walkSpeed = self->client->ps.speed = 1;
            //     }
            //
            //     self->NPC->stats.walkSpeed = self->client->ps.speed = int_data;
            //
            // Es fehlt das else. Der Block laeuft, setzt 1 - und die Zeile
            // darunter ueberschreibt es sofort wieder mit der Null. Die
            // Null bleibt also eine Null.
            //
            // Und eine Null steht wirklich still: PM_CmdScale
            // (bg_pmove.cpp:784) rechnet
            //
            //     scale = ps->speed * max / (127 * total)
            //
            // Mit speed 0 ist scale 0 und die Figur bewegt sich nicht.
            //
            // Das ist kein Randfall. In dieser Mission sind 25 der 34
            // SET_WALKSPEED und 25 der 26 SET_RUNSPEED genau 0.000 - der
            // uebliche Griff, um eine Figur fuer die Zwischensequenz
            // festzunageln. behaved hat sie bisher trotzdem losmarschieren
            // lassen.
            if (const std::string* v = setValue(child, "SET_WALKSPEED")) {
                try {
                    walkSpeed = std::stof(*v);
                } catch (...) {
                }
                continue;
            }
            if (const std::string* v = setValue(child, "SET_RUNSPEED")) {
                try {
                    runSpeed = std::stof(*v);
                } catch (...) {
                }
                continue;
            }
            // Q3_SetYawSpeed (Q3_Interface.cpp:3438) schreibt den Wert
            // ungeprueft in die Werte des NPC. Der Schritt wirkt erst zur
            // Auswertungszeit, denn gedreht wird dort - anders als beim
            // Gehen, dessen Dauer hier festgelegt wird.
            if (const std::string* v = setValue(child, "SET_YAWSPEED")) {
                ActorStep step;
                step.kind = ActorStep::Kind::TurnRate;
                step.startMs = t;
                step.endMs = t;
                step.path = p;
                try {
                    step.value = turnRateFor(std::stof(*v));
                } catch (...) {
                    step.value = kNpcTurnDegPerSec;
                }
                actor->steps.push_back(std::move(step));
                continue;
            }

            if (const std::string* v = setValue(child, "SET_NAVGOAL")) {
                // "NULL" heisst: Ziel loeschen, nicht hingehen.
                if (*v == "NULL" || v->empty()) {
                    continue;
                }
                float goal[3] = {0, 0, 0};
                if (!findEntity(map, *v, goal)) {
                    // Der Wegpunkt steht nicht in der Karte. Ohne Ort
                    // laesst sich nichts rechnen - vermerken und weiter.
                    continue;
                }
                ActorStep step;
                step.kind = ActorStep::Kind::WalkTo;
                step.startMs = t;
                for (int k = 0; k < 3; ++k) {
                    step.from[k] = here[k];
                    step.to[k] = goal[k];
                }
                const float dist = distance(here, goal);
                const float speed = gehend() ? walkSpeed : runSpeed;
                // Geschwindigkeit null heisst STEHENBLEIBEN, nicht sofort
                // ankommen. Die Figur bekommt gar keinen Gehschritt: sie
                // bleibt, wo sie ist, und der Ort fuer die folgenden
                // Schritte bleibt es auch.
                //
                // Ohne diesen Zweig waere es eine Teilung durch null - und
                // je nach Rundung entweder ein Sprung ans Ziel oder ein
                // NaN, das sich still durch alle folgenden Orte zieht.
                if (!(speed > 0.0F)) {
                    continue;
                }
                // Dieselbe Rechnung wie der Nachbau (bhed/ablauf.h): so kommt
                // die Figur an, wenn dowait fertig ist.
                {
                    const double ux = (dist > 0.0F) ? (goal[0] - here[0]) / dist : 1.0;
                    const double uy = (dist > 0.0F) ? (goal[1] - here[1]) / dist : 0.0;
                    step.endMs = t + gehDauerMs(dist, speed, ux, uy);
                }
                step.text = gehend() ? "BOTH_WALK1" : "BOTH_RUN1";
                // --- Die Animation laeuft MIT dem Tempo ----------------
                //
                // Gemeldet: "statt sauber ueber den Boden zu laufen slidet
                // er dauerhaft ein bisschen."
                //
                // bg_panimate.cpp:4824:
                //
                //     animSpeed *= (resultspeed / moveSpeedOfAnim);
                //
                // Die Animation ist fuer EINE bestimmte Geschwindigkeit
                // gebaut - 50 beim Gehen, 150 beim Laufen. Wer schneller
                // geht, muss sie schneller abspielen, sonst machen die
                // Fuesse zu wenig Schritte fuer den Weg.
                //
                // Mit den Vorgaben von behaved: 90/50 = 1,8 beim Gehen -
                // die Animation lief also fast zwei Mal zu LANGSAM. Genau
                // das sieht man als Rutschen.
                //
                // Der Schalter dafuer ist in der Engine standardmaessig an
                // (g_noFootSlide, Vorgabe "1", g_main.cpp:640) - es ist
                // also der Normalfall, kein Sonderweg.
                step.value = speed / (gehend() ? kAnimSpeedWalk
                                               : kAnimSpeedRun);
                step.value = std::clamp(step.value, kAnimSpeedMin,
                                        kAnimSpeedMax);
                step.path = p;
                for (int k = 0; k < 3; ++k) {
                    here[k] = goal[k];
                }
                // Der Ablauf der Figur laeuft WEITER, waehrend sie geht:
                // SET_NAVGOAL wartet nicht. Wer warten will, schreibt ein
                // wait dahinter - so steht es in den Skripten.
                actor->steps.push_back(std::move(step));
                continue;
            }

            // Animationen, falls das Skript sie ausdruecklich setzt.
            // Die drei Befehle treffen VERSCHIEDENE Knochen - siehe
            // torsoAnimation in scene.h. Der Koerperteil wandert deshalb
            // mit in den Schritt.
            for (const auto& [key, teil] :
                 {std::pair{"SET_ANIM_BOTH", AnimPart::Both},
                  std::pair{"SET_ANIM_UPPER", AnimPart::Upper},
                  std::pair{"SET_ANIM_LOWER", AnimPart::Lower}}) {
                if (const std::string* v = setValue(child, key)) {
                    ActorStep step;
                    step.kind = ActorStep::Kind::Anim;
                    step.startMs = t;
                    step.endMs = t;
                    // Der Anim-Timer: ein wait( "task" ) endet erst, wenn
                    // er abgelaufen ist (TID_ANIM_*, bg_panimate.cpp:4399).
                    if (hilfe != nullptr && hilfe->animDauer) {
                        const double d = hilfe->animDauer(actor->name, *v);
                        if (d > 0.0) { step.endMs = t + d; }
                    }
                    step.text = *v;
                    step.part = teil;
                    step.path = p;
                    actor->steps.push_back(std::move(step));
                    break;
                }
            }

            // remove / kill INNERHALB eines affect-Blocks.
            //
            // Das Ziel ist meist eine ANDERE Figur - 38 der 56 Entfernungen
            // in dieser Mission stehen so. "self" trifft die Figur des
            // Blocks; "enemy" koennen wir nicht aufloesen, weil behaved
            // keine Gegner kennt.
            //
            // Gesammelt statt sofort gesetzt: siehe die Begruendung bei
            // `entfernt` weiter oben.
            if ((child.name == "remove" || child.name == "kill") &&
                !child.args.empty()) {
                const std::string& ziel = child.args[0].text;
                auto& liste = (child.name == "kill") ? getoetet : entfernt;
                if (ziel == "self") {
                    liste.emplace_back(name, t);
                } else if (ziel != "enemy" && !ziel.empty()) {
                    liste.emplace_back(ziel, t);
                }
                continue;
            }

            if (const std::string* v = setValue(child, "SET_FIRE_WEAPON")) {
                ActorStep step;
                step.kind = ActorStep::Kind::Fire;
                step.startMs = t;
                step.endMs = t;
                step.value = (*v == "true") ? 1.0F : 0.0F;
                step.path = p;
                actor->steps.push_back(std::move(step));
                continue;
            }

            if (const std::string* v = setValue(child, "SET_SHOT_SPACING")) {
                ActorStep step;
                step.kind = ActorStep::Kind::ShotSpacing;
                step.startMs = t;
                step.endMs = t;
                step.value = std::strtof(v->c_str(), nullptr);
                step.path = p;
                actor->steps.push_back(std::move(step));
                continue;
            }

            if (const std::string* v = setValue(child, "SET_SABERACTIVE")) {
                ActorStep step;
                step.kind = ActorStep::Kind::Saber;
                step.startMs = t;
                step.endMs = t;
                step.value = (*v == "true") ? 1.0F : 0.0F;
                step.path = p;
                actor->steps.push_back(std::move(step));
                continue;
            }

            // --- Waffe und Schwerter ------------------------------------
            //
            // Gezaehlt ueber die Skripte der Mod (.IBI in allen .pk3):
            // SET_WEAPON 1278 mal, SET_SABERACTIVE 1185, SET_SABER2 141,
            // SET_SABER1 95, SET_SABER1BLADEON/OFF 42, SET_SABER2BLADEON/OFF
            // 21, SET_ADDLHANDBOLT_MODEL 23, SET_SABER1_COLOR1 11.
            //
            // Die Namen werden HIER aufgeloest (Modellpfad, .sab-Eintrag,
            // "random"), damit Actor::at nur noch Zahlen umlegt.
            if (const std::string* v = setValue(child, "SET_WEAPON")) {
                ActorStep step;
                step.kind = ActorStep::Kind::Waffe;
                step.startMs = t;
                step.endMs = t;
                // GetIDForString vergleicht ohne Gross/Klein; "drop" ist
                // ein eigener Fall in Q3_SetWeapon (Q3_Interface.cpp:3528).
                std::string w = *v;
                for (char& z : w) {
                    z = static_cast<char>(std::toupper(static_cast<unsigned char>(z)));
                }
                if (w == "DROP") {
                    w = "drop";
                }
                // Was nicht mit WP_ beginnt, kennt WPTable nicht: G_SetWeapon
                // bekaeme -1 und liefe ins Leere. Uebergehen.
                if (w != "drop" && w.compare(0, 3, "WP_") != 0) {
                    continue;
                }
                step.text = w;
                if (w != "drop" && w != "WP_NONE" && w != "WP_SABER") {
                    bool kotor = false;
                    if (hilfe != nullptr && hilfe->npcs != nullptr) {
                        std::string typ = actor->npcType;
                        for (char& z : typ) {
                            z = static_cast<char>(std::tolower(static_cast<unsigned char>(z)));
                        }
                        const auto it = hilfe->npcs->find(typ);
                        kotor = (it != hilfe->npcs->end()) && it->second.kotorWeapons;
                    }
                    step.index = waffenModellIndex(*actor, w, kotor, hilfe);
                }
                step.path = p;
                actor->steps.push_back(std::move(step));
                continue;
            }
            {
                // SET_SABER1 / SET_SABER2 (WP_SetSaber).
                int fach = -1;
                const std::string* v = setValue(child, "SET_SABER1");
                if (v != nullptr) {
                    fach = 0;
                } else if ((v = setValue(child, "SET_SABER2")) != nullptr) {
                    fach = 1;
                }
                if (v != nullptr) {
                    ActorStep step;
                    step.kind = ActorStep::Kind::SaberWahl;
                    step.startMs = t;
                    step.endMs = t;
                    step.slot = fach;
                    step.text = *v;
                    std::string k = *v;
                    for (char& z : k) {
                        z = static_cast<char>(std::tolower(static_cast<unsigned char>(z)));
                    }
                    if (k == "none" || k == "remove") {
                        // WP_RemoveSaber setzt die VORGABEN ein.
                        step.index = saberArtIndex(*actor, "", fach, hilfe);
                    } else {
                        step.index = saberArtIndex(*actor, *v, fach, hilfe);
                        // Ein Zweihaender im zweiten Fach: WP_SetSaber
                        // entfernt ihn gleich wieder (wp_saberLoad.cpp:3099).
                        if (fach == 1 &&
                            actor->saberArten[static_cast<std::size_t>(step.index)].zweihaendig) {
                            step.text = "none";
                            step.index = saberArtIndex(*actor, "", fach, hilfe);
                        }
                    }
                    step.path = p;
                    actor->steps.push_back(std::move(step));
                    continue;
                }
            }
            {
                // SET_SABER1_COLOR1 .. SET_SABER2_COLOR2 (WP_SaberSetColor):
                // "toSet - SET_SABER1_COLOR1" ist die KLINGE, nicht das
                // Schwert (Q3_Interface.cpp:10070 ff.).
                static const char* const kFarbSchluessel[4] = {
                    "SET_SABER1_COLOR1", "SET_SABER1_COLOR2", "SET_SABER2_COLOR1",
                    "SET_SABER2_COLOR2"};
                bool gefunden = false;
                for (int f = 0; f < 4 && !gefunden; ++f) {
                    const std::string* v = setValue(child, kFarbSchluessel[f]);
                    if (v == nullptr) {
                        continue;
                    }
                    gefunden = true;
                    ActorStep step;
                    step.kind = ActorStep::Kind::SaberFarbe;
                    step.startMs = t;
                    step.endMs = t;
                    step.slot = f / 2;
                    step.index = f % 2;
                    // "random" wuerfelt TranslateSaberColor bei JEDEM Befehl
                    // neu; die Saat traegt deshalb die Zeit mit.
                    step.text = saberFarbeAufloesen(
                        *v, actor->name + "|" + kFarbSchluessel[f] + "|" +
                                std::to_string(static_cast<long long>(t)));
                    step.path = p;
                    actor->steps.push_back(std::move(step));
                }
                if (gefunden) {
                    continue;
                }
            }
            {
                // SET_SABER1BLADEON .. SET_SABER2BLADEOFF: atoi(data) ist
                // die Klinge, von 0 an gezaehlt (Q3_Interface.cpp:9704 ff.).
                static const char* const kKlingenSchluessel[4] = {
                    "SET_SABER1BLADEON", "SET_SABER1BLADEOFF", "SET_SABER2BLADEON",
                    "SET_SABER2BLADEOFF"};
                bool gefunden = false;
                for (int f = 0; f < 4 && !gefunden; ++f) {
                    const std::string* v = setValue(child, kKlingenSchluessel[f]);
                    if (v == nullptr) {
                        continue;
                    }
                    gefunden = true;
                    ActorStep step;
                    step.kind = ActorStep::Kind::KlingeSchalter;
                    step.startMs = t;
                    step.endMs = t;
                    step.slot = f / 2;
                    // atoi: "1.000" gibt 1, wie in der Engine.
                    step.index = std::atoi(v->c_str());
                    step.value = (f % 2 == 0) ? 1.0F : 0.0F;
                    step.path = p;
                    actor->steps.push_back(std::move(step));
                }
                if (gefunden) {
                    continue;
                }
            }
            {
                // SET_ADDRHANDBOLT_MODEL & Co. (Q3_Interface.cpp:9576 ff.).
                static const char* const kHandSchluessel[4] = {
                    "SET_ADDRHANDBOLT_MODEL", "SET_ADDLHANDBOLT_MODEL",
                    "SET_REMOVERHANDBOLT_MODEL", "SET_REMOVELHANDBOLT_MODEL"};
                bool gefunden = false;
                for (int f = 0; f < 4 && !gefunden; ++f) {
                    const std::string* v = setValue(child, kHandSchluessel[f]);
                    if (v == nullptr) {
                        continue;
                    }
                    gefunden = true;
                    ActorStep step;
                    step.kind = ActorStep::Kind::HandModell;
                    step.startMs = t;
                    step.endMs = t;
                    step.slot = f % 2;          // 0 rechts, 1 links
                    step.value = (f < 2) ? 1.0F : 0.0F;
                    step.text = *v;
                    if (f < 2) {
                        step.index = modellIndex(*actor, *v);
                    }
                    step.path = p;
                    actor->steps.push_back(std::move(step));
                }
                if (gefunden) {
                    continue;
                }
            }

            if (const std::string* v = setValue(child, "SET_INVISIBLE")) {
                ActorStep step;
                step.kind = (*v == "true") ? ActorStep::Kind::Hide
                                           : ActorStep::Kind::Show;
                step.startMs = t;
                step.endMs = t;
                step.path = p;
                actor->steps.push_back(std::move(step));
                continue;
            }
        }
        };
        run(n.children, Path{i}, 0);
    }

    // --- Die gesammelten Entfernungen zuordnen ---------------------------
    //
    // Jetzt stehen alle Figuren fest, also darf out.actors wachsen: die
    // Zeiger aus der Schleife oben werden nicht mehr gebraucht.
    //
    // Eine Figur, die nur ENTFERNT wird und sonst nirgends vorkommt, wird
    // hier NICHT angelegt. Ohne affect-Block hat sie keine Bahn, und eine
    // Figur zu erfinden, um sie im selben Atemzug verschwinden zu lassen,
    // brachte nichts als einen Eintrag in der Liste.
    for (const auto& [ziel, wann] : entfernt) {
        for (Actor& a : out.actors) {
            if (a.name != ziel) {
                continue;
            }
            ActorStep step;
            step.kind = ActorStep::Kind::Gone;
            step.startMs = wann;
            step.endMs = wann;
            a.steps.push_back(std::move(step));
        }
    }

    // --- kill: die Figur STIRBT --------------------------------------------
    //
    // CQuake3GameInterface::Kill (Q3_Interface.cpp:10591) setzt health auf 0
    // und ruft die Sterbefunktion - bei einer Figur player_die mit
    // G_PickDeathAnim (g_combat.cpp:3896). Die Leiche bleibt liegen; nur
    // remove gibt die Entity frei.
    //
    // Welche Todesanimation, haengt im Spiel von der Trefferstelle ab
    // (G_GetHitLocation) und ist teils zufaellig. kill hat keinen Treffer -
    // wir nehmen wiederholbar BOTH_DEATH1 und frieren auf dem letzten Bild
    // ein (die Engine wechselt danach auf BOTH_DEAD1, die liegende
    // Endstellung derselben Bewegung). Eine Naeherung, als solche vermerkt.
    //
    // Ein Brush-Modell (func_breakable) zerbricht statt zu sterben - das
    // Zerbrechen zeichnet MoverSim; hier verschwindet es nur.
    for (Actor& a : out.actors) {
        std::stable_sort(a.steps.begin(), a.steps.end(),
                         [](const ActorStep& x, const ActorStep& y) { return x.startMs < y.startMs; });
    }
    for (const auto& [ziel, wann] : getoetet) {
        for (Actor& a : out.actors) {
            if (a.name != ziel) {
                continue;
            }
            if (a.brushModel > 0) {
                ActorStep weg;
                weg.kind = ActorStep::Kind::Gone;
                weg.startMs = wann;
                weg.endMs = wann;
                a.steps.push_back(std::move(weg));
                continue;
            }
            // Wo sie stirbt, bleibt sie: ein laufender Weg endet hier.
            const ActorState st = a.at(wann);
            ActorStep halt;
            halt.kind = ActorStep::Kind::Teleport;
            halt.startMs = wann;
            halt.endMs = wann;
            for (int k = 0; k < 3; ++k) {
                halt.to[k] = st.pos[k];
            }
            a.steps.push_back(halt);
            ActorStep tod;
            tod.kind = ActorStep::Kind::Anim;
            tod.startMs = wann;
            tod.endMs = wann;
            tod.text = "BOTH_DEATH1";
            tod.part = AnimPart::Both;
            if (hilfe != nullptr && hilfe->animDauer) {
                const double d = hilfe->animDauer(a.name, tod.text);
                if (d > 0.0) { tod.endMs = wann + d; }
            }
            a.steps.push_back(tod);
            ActorStep liegen;
            liegen.kind = ActorStep::Kind::Hold;
            liegen.startMs = wann;
            liegen.endMs = wann;
            liegen.value = -1.0F;   // einfrieren
            liegen.part = AnimPart::Both;
            a.steps.push_back(liegen);
        }
    }

    // Die Schritte je Figur in Reihenfolge bringen: at() bricht sonst zu
    // frueh ab. Sie entstehen zwar schon geordnet, aber ein affect kann
    // dieselbe Figur mehrfach ansprechen.
    for (Actor& a : out.actors) {
        std::stable_sort(a.steps.begin(), a.steps.end(),
                         [](const ActorStep& x, const ActorStep& y) {
                             return x.startMs < y.startMs;
                         });
        for (const ActorStep& step : a.steps) {
            out.durationMs = std::max(out.durationMs, step.endMs);
        }
    }
    std::stable_sort(out.roffEreignisse.begin(), out.roffEreignisse.end(),
                     [](const RoffEreignis& x, const RoffEreignis& y) { return x.ms < y.ms; });
    return out;
}

double gehDauerMs(double dist, double v, double ux, double uy, double zielRadius) {
    if (!(v > 0.0) || !(dist > 0.0)) {
        return 0.0;
    }
    const double zone = 4.0 * zielRadius;
    const double achse = std::max(std::fabs(ux), std::fabs(uy));
    const double kasten = (achse > 1e-3) ? 15.0 / achse : 15.0;
    const double ende = std::max(zielRadius, kasten);
    if (dist <= ende) {
        return 0.0;
    }
    double s = 0.0;
    if (dist > zone) {
        s += (dist - zone) / v;
    }
    const double inZone = std::min(dist, zone);
    if (inZone > ende && zone > 0.0) {
        s += zone / v * std::log(inZone / ende);
    }
    // Angepasst an neun Messungen im Spiel (md_dd_jedi, Tempo 55/100/200,
    // 40-150 Einheiten, dazu obi1 aus der Intro): Anlauf 300 ms, dann das
    // 1,5-fache des Steuermodells. Streuung um +-100 ms bis auf zwei Wege,
    // die im Spiel offenbar um etwas herumliefen.
    return 300.0 + 1.5 * s * 1000.0;
}

Scene buildScene(const Script& s, const MapData& map, const SzenenHilfe* hilfe) {
    // Die Signale kommen jetzt aus bhed/clock.h und werden EINMAL
    // eingesammelt - dieselbe Antwort, die auch die Kamerabahn und die
    // Zeitleiste bekommen. Vorher lief dafuer hier ein eigener zweiter
    // Aufbau, den die anderen beiden nicht kannten.
    std::map<std::string, double> signals = collectSignals(s);
    Scene sc = buildOnce(s, map, signals, hilfe);
    // Die Laengen der Animationen, die jede Figur benutzt - fuer die Timer
    // in Actor::at (siehe animDauerMs).
    if (hilfe != nullptr && hilfe->animDauer) {
        for (Actor& a : sc.actors) {
            // Dazu die Kampfhaltungen (saberGrundhaltung prueft, ob es sie gibt).
            std::vector<std::string> namen = {"BOTH_STAND1", "BOTH_WALK1", "BOTH_RUN1",
                                              "BOTH_CROUCH1IDLE"};
            for (int stil = 1; stil <= 7; ++stil) {
                namen.emplace_back(saberHaltungFuerStil(stil));
            }
            for (const ActorStep& st : a.steps) {
                // Waffen- und Schwertschritte tragen Waffen-, Schwert- und
                // Modellnamen, keine Animationen.
                const bool ausruestung =
                    st.kind == ActorStep::Kind::Waffe || st.kind == ActorStep::Kind::SaberWahl ||
                    st.kind == ActorStep::Kind::SaberFarbe ||
                    st.kind == ActorStep::Kind::HandModell;
                if (!st.text.empty() && !ausruestung) { namen.push_back(st.text); }
            }
            for (const std::string& n : namen) {
                const double d = hilfe->animDauer(a.name, n);
                if (d >= 0.0) { a.animDauerMs[n] = d; }
                if (hilfe->bildDauer) {
                    const double b = hilfe->bildDauer(a.name, n);
                    if (b > 0.0) { a.bildDauerMs[n] = b; }
                }
            }
        }
    }
    return sc;
}



void muzzlePoint(const float origin[3], float yawDeg,
                 const std::string& weapon, float out[3]) {
    // Blickhoehe eines stehenden NPC: standheight (DEFAULT_MAXS_2 = 40,
    // NPC_stats.cpp:2031) plus STANDARD_VIEWHEIGHT_OFFSET (-4).
    constexpr float kViewHeight = 36.0F;

    const float r = yawDeg * 3.14159265358979F / 180.0F;
    const float vorn[3] = {std::cos(r), std::sin(r), 0.0F};
    // In der Engine ist rechts = (sin, -cos, 0): AngleVectors legt right
    // auf die NEGATIVE Seite der Gierdrehung.
    const float rechts[3] = {std::sin(r), -std::cos(r), 0.0F};

    for (int k = 0; k < 3; ++k) {
        out[k] = origin[k];
    }

    float hoch = 0.0F;
    float nachVorn = 0.0F;
    float nachRechts = 0.0F;
    if (weapon == "WP_BRYAR_PISTOL" || weapon == "WP_BLASTER_PISTOL") {
        hoch = kViewHeight - 16.0F;
        nachVorn = 28.0F;
        nachRechts = 6.0F;
    } else if (weapon == "WP_ROCKET_LAUNCHER" || weapon == "WP_CONCUSSION" ||
               weapon == "WP_THERMAL") {
        hoch = kViewHeight - 2.0F;
    } else if (weapon == "WP_BLASTER") {
        hoch = kViewHeight - 1.0F;
        nachVorn = 2.0F;      // der NPC-Wert, nicht die 12 des Spielers
        nachRechts = 1.0F;
    } else {
        // Die uebrigen Zweige von CalcMuzzlePoint habe ich nicht
        // nachgelesen. Die Blickhoehe allein ist naeher an der Wahrheit als
        // der Ursprung am Boden - und es ist ehrlicher, das hier zu
        // schreiben, als eine Zahl zu erfinden.
        hoch = kViewHeight;
    }

    out[2] += hoch;
    for (int k = 0; k < 3; ++k) {
        out[k] += vorn[k] * nachVorn + rechts[k] * nachRechts;
    }
}

}  // namespace bhed
