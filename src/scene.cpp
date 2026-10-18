#include "bhed/scene.h"
#include "bhed/npcklasse.h"
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
#include <initializer_list>   // rc601 bewegung
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

// --- Paket bewegung (rc601): Namen so suchen, wie die Engine sie findet ----
//
// Diese Klassen gibt das Spiel gleich beim Laden wieder frei, und G_Find
// ueberspringt freie Entities (g_utils.cpp:516, PInUse). Ein SET_NAVGOAL,
// SET_WATCHTARGET oder SET_VIEWTARGET auf sie findet im Spiel NICHTS:
//
//   waypoint, waypoint_small   G_FreeEntity in SP_waypoint/_small (g_nav.cpp:197, 238)
//   waypoint_navgoal           G_FreeEntity nach TAG_Add (g_nav.cpp:283) - als
//                              TAG fuer SET_NAVGOAL aber da
//   ref_tag                    G_FreeEntity nach TAG_Add (g_ref.cpp:400) - ebenso
//   info_null                  nach START_TIME_REMOVE_ENTS = 300 ms (g_misc.cpp:50-62)
bool imSpielFreigegeben(const MapEntity& e) {
    const std::string& k = e.classname;
    return k == "waypoint" || k == "waypoint_small" || k.rfind("waypoint_navgoal", 0) == 0 || k == "ref_tag" ||
           k == "info_null";
}

// G_Find( NULL, FOFS(targetname), name ): die erste Entity im Spiel mit diesem
// targetname, ohne Gross/klein (Q_stricmp), ein leerer Name findet nichts.
// Eine Figur traegt den NPC_targetname ihres Spawners als targetname
// (NPC_spawn.cpp, newent->targetname), der Spieler heisst "player"
// (g_client.cpp). istFigur: das Ziel ist ein NPC oder der Spieler.
bool findeImSpiel(const MapData& map, const std::string& name, float out[3], bool* istFigur) {
    if (istFigur != nullptr) { *istFigur = false; }
    if (name.empty()) {
        return false;
    }
    for (const MapEntity& e : map.entities) {
        if (e.origin.empty() || imSpielFreigegeben(e) || e.targetname.empty() || !sameName(e.targetname, name)) {
            continue;
        }
        readVec(e.origin, out);
        return true;
    }
    for (const MapEntity& e : map.entities) {
        if (e.origin.empty() || !isNpcSpawner(e)) {
            continue;
        }
        const std::string* n = e.find("NPC_targetname");
        if (n != nullptr && sameName(*n, name)) {
            readVec(e.origin, out);
            if (istFigur != nullptr) { *istFigur = true; }
            return true;
        }
    }
    if (sameName(name, "player")) {
        for (const MapEntity& e : map.entities) {
            if (e.classname == "info_player_start" && !e.origin.empty()) {
                readVec(e.origin, out);
                if (istFigur != nullptr) { *istFigur = true; }
                return true;
            }
        }
    }
    return false;
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

// --- rc601 (bewegung, Wege): Ort auf einem WalkTo-Stueck --------------------
void wegOrtBei(const ActorStep& st, double t, float out[3]) {
    if (st.lauf) {
        const double tt = std::clamp(t, st.startMs, std::max(st.startMs, st.endMs));
        const NavVec p = ortBei(*st.lauf, tt);
        for (int k = 0; k < 3; ++k) { out[k] = p[static_cast<std::size_t>(k)]; }
        return;
    }
    const double span = st.endMs - st.startMs;
    const double f = (span <= 0.0) ? 1.0 : std::min(1.0, (t - st.startMs) / span);
    const auto ff = static_cast<float>(f);
    for (int k = 0; k < 3; ++k) {
        out[k] = st.from[k] + (st.to[k] - st.from[k]) * ff;
    }
}

bool wegBewegtBei(const ActorStep& st, double t) {
    if (!(t < st.endMs)) { return false; }
    if (!st.lauf) { return t >= st.startMs; }
    // Das Denkbild, dessen Pmove die Figur nach t bringt.
    for (const LaufTakt& k : st.lauf->takte) {
        if (k.t >= t) { return k.bewegt; }
    }
    return false;
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
    // rc601 (bewegung, Durchsicht 2): seit wann BS_NOCLIP gilt (-1: nicht).
    double noclipAb = -1.0;
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
    // --- rc601 (bewegung): Drehen wie NPC_UpdateAngles --------------------
    //
    // Fuer einen NPC (nicht den Spieler selbst, keine Tuer) wird der
    // Gierwinkel nicht mehr je Schritt geraten, sondern nach der Schleife
    // aus den Ereignissen gerechnet - siehe dort. Die Schleife sammelt sie
    // nur, dazu die Schritte, die den Ort aendern.
    const bool npcFigur = !npcType.empty() && !echterSpieler && brushModel <= 0;
    enum class GierArt : std::uint8_t { Sprung, Wunsch, Rate, Weg, WatchAn, WatchAus, Verhalten, Ort, RoffEnde, Waffe };
    struct GierEreignis {
        double t = 0.0;
        GierArt art = GierArt::Sprung;
        float wert = 0.0F;
        const ActorStep* step = nullptr;
    };
    std::vector<GierEreignis> gierEreignisse;
    std::vector<const ActorStep*> ortsSchritte;

    for (const ActorStep& step : steps) {
        if (step.startMs > ms) {
            break;   // die Schritte stehen in Reihenfolge
        }
        switch (step.kind) {
            case ActorStep::Kind::Teleport:
                for (int k = 0; k < 3; ++k) {
                    st.pos[k] = step.to[k];
                }
                // rc601 (bewegung): ein Sprung beendet das Gehen auf dem alten
                // Weg. Vorher blieben moving und weg stehen, und die Figur
                // lief am neuen Ort auf der Stelle weiter ("Laufband"), bis
                // der alte Weg zu Ende gewesen waere. Laeuft sie vom neuen Ort
                // weiter, folgt ein eigener WalkTo zur selben Zeit.
                st.moving = false;
                weg = nullptr;
                gehEnde = std::min(gehEnde, step.startMs);
                if (npcFigur) {
                    ortsSchritte.push_back(&step);
                    gierEreignisse.push_back({step.startMs, GierArt::Ort, 0.0F, &step});
                }
                break;
            case ActorStep::Kind::WalkTo: {
                // rc601 (bewegung, Wege): aus der Bahn je Denkbild, sonst linear.
                wegOrtBei(step, ms, st.pos);
                // Beim Gehen schaut die Figur in Laufrichtung - so macht es
                // die Engine auch, solange kein SET_LOOK_TARGET gesetzt ist.
                gehEnde = step.endMs;
                wegNr = &step - steps.data();
                // Gedreht wird nach der Schleife (rc601): desiredYaw wird die
                // Laufrichtung (STEER::DeActivate, g_navigator.cpp:4152), und
                // NPC_UpdateAngles dreht mit der Drehrate dorthin.
                if (npcFigur) {
                    ortsSchritte.push_back(&step);
                    gierEreignisse.push_back({step.startMs, GierArt::Weg, 0.0F, &step});
                }
                // rc601 (bewegung, Wege): die Beine gehen, solange STEER
                // eine Bewegung befiehlt (ucmd.forwardmove/rightmove) - nicht
                // beim Ausrollen nach der Ankunft und nicht in einem
                // Stop-Denkbild.
                st.moving = wegBewegtBei(step, ms);
                // Die Laufanimation waehlt erst der Abschnitt nach der
                // Schleife - sie haengt am Anim-Timer (siehe dort).
                weg = st.moving ? &step : nullptr;
                break;
            }
            case ActorStep::Kind::Face:
                for (int k = 0; k < 3; ++k) {
                    st.angles[k] = step.to[k];
                }
                // rc601 (bewegung): bei einem NPC ein Sprung des Gierwinkels
                // (SetClientViewAngle) und zugleich desiredYaw (Q3_SetDYaw,
                // Q3_Interface.cpp:1286-1291) - weiter nach der Schleife.
                if (npcFigur) {
                    gierEreignisse.push_back({step.startMs, GierArt::Sprung, step.to[1], &step});
                }
                break;
            case ActorStep::Kind::Glide:
            case ActorStep::Kind::Turn: {
                // rc601 (bewegung): Lerp2Pos/Lerp2Angles lehnen jeden client
                // ab ("NOT a mover") - auf einer Figur tut move/rotate nichts.
                if (npcFigur || echterSpieler) {
                    break;
                }
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
                // rc601 (bewegung): nach der Bahn behaelt die Figur den
                // letzten Winkel - G_Roff setzt desiredYaw = trBase[YAW]
                // (g_roff.cpp, Zweig mit client).
                if (npcFigur) {
                    ortsSchritte.push_back(&step);
                    if (step.roffFigur && step.endMs <= ms) {
                        gierEreignisse.push_back({step.endMs, GierArt::RoffEnde, step.toAng[1], &step});
                    }
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
                if (npcFigur) {
                    gierEreignisse.push_back({step.startMs, GierArt::Rate, step.value, &step});
                }
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
                    st.watchStep = step.spur.empty() ? nullptr : &step;
                    if (npcFigur) {
                        gierEreignisse.push_back({step.startMs, GierArt::WatchAn, 0.0F, &step});
                    }
                } else {
                    st.hasWatchTarget = false;
                    st.watchStep = nullptr;
                    if (npcFigur) {
                        gierEreignisse.push_back({step.startMs, GierArt::WatchAus, 0.0F, &step});
                    }
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
                // rc601 (bewegung, Durchsicht 2): die Waffe aendert die
                // Drehrate (npcDrehrate) - ab dem naechsten Denkbild.
                if (npcFigur && step.kind == ActorStep::Kind::Waffe) {
                    gierEreignisse.push_back({step.startMs, GierArt::Waffe, 0.0F, &step});
                }
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
            case ActorStep::Kind::Verhalten:
                // rc601 (bewegung, gemeinsam mit saber): der letzte gilt.
                // rc601 (bewegung, Durchsicht 2): Beginn von BS_NOCLIP merken.
                if (step.text == "BS_NOCLIP") {
                    if (st.verhalten != "BS_NOCLIP") { noclipAb = step.startMs; }
                } else {
                    noclipAb = -1.0;
                }
                st.verhalten = step.text;
                if (npcFigur) {
                    gierEreignisse.push_back({step.startMs, GierArt::Verhalten, 0.0F, &step});
                }
                break;
            case ActorStep::Kind::Wunschgier:
                // rc601 (bewegung): SET_DYAW / SET_VIEWTARGET - desiredYaw.
                if (npcFigur) {
                    gierEreignisse.push_back({step.startMs, GierArt::Wunsch, step.value, &step});
                }
                break;
            case ActorStep::Kind::SaberKollision:
                // rc601 (saber)
                st.saberKollisionen = step.value > 0.5F;
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
    if (st.hasWatchTarget && st.watchStep != nullptr && !st.watchStep->spur.empty()) {
        // Ein bewegtes Ziel: wo es JETZT ist.
        const ActorStep& w = *st.watchStep;
        const double f = std::max(0.0, ms - w.startMs) / w.spurSchrittMs;
        const auto i = std::min(static_cast<std::size_t>(f), w.spur.size() - 1U);
        const std::size_t j = std::min(i + 1U, w.spur.size() - 1U);
        const auto r = static_cast<float>(std::min(1.0, f - static_cast<double>(i)));
        for (int k = 0; k < 3; ++k) {
            st.watchAt[k] = w.spur[i][static_cast<std::size_t>(k)] +
                            (w.spur[j][static_cast<std::size_t>(k)] - w.spur[i][static_cast<std::size_t>(k)]) * r;
        }
    }
    // =====================================================================
    // rc601 (bewegung): der Gierwinkel eines NPC - EIN Regler wie
    // NPC_UpdateAngles
    // =====================================================================
    //
    // Gemeldet: "NPCs don't rotate with their YAW_SPEED. They just instantly
    // turn."
    //
    // Bis rc600 rechneten zwei getrennte Formeln (Laufrichtung, Watchtarget)
    // mit je eigenem Anfangswert und ohne Gedaechtnis zwischen den
    // Schritten. Darum sprang die Figur beim Wechsel des Watchtargets (Start
    // und Zeit des ersten blieben stehen), bei NULL zurueck auf ihren alten
    // Winkel und bei SET_ANGLES im Gehen; ein spaeteres SET_YAWSPEED wirkte
    // rueckwirkend, SET_DYAW und SET_VIEWTARGET kannte behaved nicht.
    //
    // Die Engine hat EINEN Regler (GierLauf, scene.h). desiredYaw schreiben
    // STEER (Laufrichtung), NPC_BSCinematic (Watchtarget, jedes Denkbild neu
    // - und NUR in BS_CINEMATIC, NPC_behavior.cpp:304; NPC_BSDefault kennt
    // kein Watchtarget), Q3_SetDYaw (SET_DYAW, SET_VIEWTARGET, SET_ANGLES)
    // und G_Roff. NPC_UpdateAngles dreht in jedem Denkbild um
    // (60 + 3*yawSpeed) * 0.05 Grad darauf zu.
    //
    // Hier laufen die gesammelten Ereignisse in ihrer Zeitfolge durch diesen
    // Regler. Zwischen zwei Ereignissen steht desiredYaw fest (geschlossen
    // gerechnet, genau an jedem Denkbild) - ausser bei einem Watchtarget,
    // wenn Figur oder Ziel sich bewegen: dann wie NPC_BSCinematic Denkbild
    // fuer Denkbild, ueber hoechstens die letzten kVerfolgFensterMs. Davor
    // gilt die Figur als eingeschwungen - genau, solange das Ziel langsamer
    // um sie wandert, als sie sich dreht.
    if (npcFigur && !roffFigurLaeuft) {
        constexpr double kVerfolgFensterMs = 3000.0;
        // rc601 (bewegung, R5): eine feste Drehrate (Rocket Trooper,
        // WP_EMPLACED_GUN) ueberstimmt .npc und SET_YAWSPEED.
        const bool festeRate = festeDrehrate > 0.0F;
        constexpr float kGrad = 180.0F / std::numbers::pi_v<float>;
        std::stable_sort(gierEreignisse.begin(), gierEreignisse.end(),
                         [](const GierEreignis& a, const GierEreignis& b) { return a.t < b.t; });
        // Wo die Figur zur Zeit tt steht - dieselben Ortsschritte wie in der
        // Schleife oben (Teleport, WalkTo, Roff).
        const auto ortBei = [&](double tt, float o[3]) {
            for (int k = 0; k < 3; ++k) { o[k] = start[k]; }
            for (const ActorStep* ps : ortsSchritte) {
                if (ps->startMs > tt) { break; }
                if (ps->kind == ActorStep::Kind::Teleport) {
                    for (int k = 0; k < 3; ++k) { o[k] = ps->to[k]; }
                } else if (ps->kind == ActorStep::Kind::WalkTo) {
                    wegOrtBei(*ps, tt, o);   // rc601 (bewegung, Wege)
                } else if (ps->kind == ActorStep::Kind::Roff && ps->roff && ps->roffBahn) {
                    const RoffStand r = roffStand(*ps->roff, *ps->roffBahn, tt - ps->startMs, ps->roffFigur);
                    for (int k = 0; k < 3; ++k) { o[k] = r.ort[k]; }
                }
            }
        };
        // Der Ort des Watchtargets zur Zeit tt (mit Spur: das Ziel geht).
        const auto zielBei = [](const ActorStep& w, double tt, float o[3]) {
            if (w.spur.empty()) {
                for (int k = 0; k < 3; ++k) { o[k] = w.to[k]; }
                return;
            }
            const double f = std::max(0.0, tt - w.startMs) / w.spurSchrittMs;
            const auto i = std::min(static_cast<std::size_t>(f), w.spur.size() - 1U);
            const std::size_t j = std::min(i + 1U, w.spur.size() - 1U);
            const auto r = static_cast<float>(std::min(1.0, f - static_cast<double>(i)));
            for (int k = 0; k < 3; ++k) {
                const auto kk = static_cast<std::size_t>(k);
                o[k] = w.spur[i][kk] + (w.spur[j][kk] - w.spur[i][kk]) * r;
            }
        };
        // desiredYaw aus NPC_BSCinematic: vectoangles( Ziel - Auge ). Auf sich
        // selbst ein Nullvektor - vectoangles gibt dann Gier 0 (q_math.c).
        const auto watchGier = [&](const ActorStep& w, double tt) {
            if (w.index == 1) { return 0.0F; }
            float ich[3];
            float da[3];
            ortBei(tt, ich);
            zielBei(w, tt, da);
            const float dx = da[0] - ich[0];
            const float dy = da[1] - ich[1];
            return (dx == 0.0F && dy == 0.0F) ? 0.0F : std::atan2(dy, dx) * kGrad;
        };
        const auto laufGier = [&](const ActorStep& w) {
            return std::atan2(w.to[1] - w.from[1], w.to[0] - w.from[0]) * kGrad;
        };
        // Bewegt sich die Figur zwischen a und b?
        const auto bewegtSich = [&](double a, double b) {
            for (const ActorStep* ps : ortsSchritte) {
                if (ps->startMs >= b) { break; }
                if (ps->endMs <= a) { continue; }
                if (ps->kind == ActorStep::Kind::Roff) { return true; }
                if (ps->kind == ActorStep::Kind::WalkTo &&
                    std::fabs(ps->to[0] - ps->from[0]) + std::fabs(ps->to[1] - ps->from[1]) > 0.01F) {
                    return true;
                }
            }
            return false;
        };
        GierLauf g;
        g.setzen(0.0, startAngles[1]);
        // rc601 (bewegung, Durchsicht 2): je Waffe in der Hand (npcDrehrate);
        // yawRate ist stats.yawSpeed als Grad/s (.npc, SET_YAWSPEED).
        float yawRate = startDrehrate;
        std::string waffeJetzt;
        (void)festeRate;
        g.rate = npcDrehrate(*this, waffeJetzt, yawRate);
        bool kino = false;
        const ActorStep* watch = nullptr;
        const ActorStep* lauf = nullptr;
        double tc = 0.0;
        const auto laeuft = [&](double tt) { return lauf != nullptr && tt >= lauf->startMs && tt < lauf->endMs; };
        // rc601 (bewegung, Durchsicht 2): auf einem Stueck mit Plan (planeLauf)
        // IST die Gier die des Plans je Denkbild - NPC_UpdateAngles nach
        // STEER::DeActivate mit desiredYaw aus der Geschwindigkeit, nicht die
        // Richtung des Stuecks. Gemeldet in §8.7: "an Ecken weicht das leicht
        // ab". Am Ende des Stuecks uebernimmt der Regler den Zustand des Plans
        // (viewangles und desiredYaw) und dreht von dort weiter.
        const ActorStep* planWeg = nullptr;
        const auto planAbgeben = [&](double tt) {
            if (planWeg == nullptr || tt < planWeg->endMs) { return; }
            const double te = planWeg->endMs;
            g.setzen(te, gierBei(*planWeg->lauf, te));
            g.zielAb(te, zustandBei(*planWeg->lauf, te).wunschGier);
            if (tc < te) { tc = te; }
            planWeg = nullptr;
        };
        // Den Regler bis tt fuehren. Nur ein Watchtarget mit Bewegung braucht
        // die Denkbilder einzeln - sonst steht desiredYaw fest und g ist genau.
        const auto weiter = [&](double tt) {
            if (tt <= tc) { return; }
            if (kino && watch != nullptr && watch->index != 1 && (!watch->spur.empty() || bewegtSich(tc, tt))) {
                double t = tc;
                float y = g.bei(tc);
                if (tt - tc > kVerfolgFensterMs) {
                    t = tt - kVerfolgFensterMs;
                    y = watchGier(*watch, t);
                }
                const float schritt = g.rate * static_cast<float>(kNpcDenktaktMs / 1000.0);
                while (t + kNpcDenktaktMs <= tt) {
                    t += kNpcDenktaktMs;
                    const float wunsch = watchGier(*watch, t);
                    float fehler = winkel180(y - wunsch);
                    if (std::fabs(fehler) > kMinAngleError) {
                        fehler = (fehler < 0.0F) ? std::min(0.0F, fehler + schritt) : std::max(0.0F, fehler - schritt);
                    }
                    y = wunsch + fehler;
                }
                g.yaw0 = y;
                g.t0 = t;
                g.ziel = watchGier(*watch, tt);
            }
            tc = tt;
        };
        for (const GierEreignis& e : gierEreignisse) {
            if (e.t > ms) { break; }
            planAbgeben(e.t);   // rc601 (bewegung, Durchsicht 2)
            weiter(e.t);
            const bool imWatch = kino && watch != nullptr;
            switch (e.art) {
                case GierArt::Sprung:
                    g.setzen(e.t, e.wert);
                    if (imWatch) {
                        g.zielAb(e.t, watchGier(*watch, e.t));
                    } else if (laeuft(e.t)) {
                        g.zielAb(e.t, laufGier(*lauf));
                    }
                    break;
                case GierArt::Wunsch:
                    // Beim Gehen und mit Watchtarget ueberschreiben STEER bzw.
                    // NPC_BSCinematic desiredYaw im naechsten Denkbild.
                    if (!imWatch && !laeuft(e.t)) {
                        g.zielAb(e.t, e.wert);
                    }
                    break;
                case GierArt::Rate:
                    yawRate = e.wert;
                    g.rateAb(e.t, npcDrehrate(*this, waffeJetzt, yawRate));
                    break;
                case GierArt::Waffe:
                    waffeJetzt = e.step->text;
                    g.rateAb(e.t, npcDrehrate(*this, waffeJetzt, yawRate));
                    break;
                case GierArt::Weg:
                    lauf = e.step;
                    if (e.step->lauf) { planWeg = e.step; }   // rc601 (bewegung, Durchsicht 2)
                    if (!imWatch && std::fabs(lauf->to[0] - lauf->from[0]) + std::fabs(lauf->to[1] - lauf->from[1]) > 1.0F) {
                        g.zielAb(e.t, laufGier(*lauf));
                    }
                    break;
                case GierArt::WatchAn:
                    watch = e.step;
                    if (kino) {
                        g.zielAb(e.t, watchGier(*watch, e.t));
                    }
                    break;
                case GierArt::WatchAus:
                    // Q3_SetWatchTarget loescht nur den Zeiger: desiredYaw
                    // bleibt (g.ziel); beim Gehen nimmt STEER wieder die
                    // Laufrichtung.
                    watch = nullptr;
                    if (imWatch && laeuft(e.t)) {
                        g.zielAb(e.t, laufGier(*lauf));
                    }
                    break;
                case GierArt::Verhalten: {
                    const bool neu = e.step->text == "BS_CINEMATIC";
                    if (neu != kino) {
                        kino = neu;
                        if (kino && watch != nullptr) {
                            g.zielAb(e.t, watchGier(*watch, e.t));
                        } else if (!kino && watch != nullptr && laeuft(e.t)) {
                            g.zielAb(e.t, laufGier(*lauf));
                        }
                    }
                    break;
                }
                case GierArt::Ort:
                    if (imWatch) {
                        g.zielAb(e.t, watchGier(*watch, e.t));
                    }
                    break;
                case GierArt::RoffEnde:
                    g.setzen(e.t, e.wert);
                    break;
            }
        }
        planAbgeben(ms);   // rc601 (bewegung, Durchsicht 2)
        weiter(ms);
        st.angles[1] = (planWeg != nullptr && ms >= planWeg->startMs) ? gierBei(*planWeg->lauf, ms) : g.bei(ms);
        st.turnDegPerSec = g.rate;
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
            // --- Rueckwaerts (rc597) ------------------------------------
            //
            // Schaut die Figur gegen ihre Laufrichtung (ein SET_WATCHTARGET
            // hinter ihr), laeuft sie rueckwaerts: STEER::DeActivate macht
            // aus der Bewegung eine Eingabe relativ zum Blick, eine negative
            // Vorwaertseingabe setzt PMF_BACKWARDS_RUN (bg_pmove.cpp), und
            // PM_Footsteps waehlt BOTH_WALKBACK1 (mit Klinge WALKBACK2) bzw.
            // BOTH_RUNBACK1/2. Genau so machen es die Skripte - Vader in
            // md_dd/cin1_jedi ("vader_walkback").
            std::string lauf = weg->text;
            // rc601 (bewegung, R7): mit der Bahn je Denkbild waehlt PM_Footsteps
            // nach BUTTON_WALKING DIESES Denkbilds (STEER::DeActivate: mSpeed <
            // walkSpeed + 5, dann NPC_ApplyScriptFlags), und die Animation
            // laeuft mit dem wirklichen Tempo (resultspeed, bg_pmove.cpp:34277;
            // bg_panimate.cpp:6860-6925: animSpeed *= resultspeed / 50 bzw. 150).
            float animTempo = -1.0F;
            if (weg->lauf) {
                const std::vector<LaufTakt>& tk = weg->lauf->takte;
                for (std::size_t i = 0; i < tk.size(); ++i) {
                    if (tk[i].t < ms) { continue; }
                    lauf = tk[i].gehAnim ? "BOTH_WALK1" : "BOTH_RUN1";
                    const NavVec& vor = i == 0 ? weg->lauf->anfang.ort : tk[i - 1].ort;
                    const float dx = tk[i].ort[0] - vor[0];
                    const float dy = tk[i].ort[1] - vor[1];
                    const float dz = tk[i].ort[2] - vor[2];
                    const float result = std::sqrt(dx * dx + dy * dy + dz * dz) / static_cast<float>(kNavDenkMs / 1000.0);
                    animTempo = std::clamp(result / (tk[i].gehAnim ? kAnimSpeedWalk : kAnimSpeedRun), kAnimSpeedMin,
                                           kAnimSpeedMax);
                    break;
                }
            }
            {
                const float mx = weg->to[0] - weg->from[0];
                const float my = weg->to[1] - weg->from[1];
                const float g = st.angles[1] * std::numbers::pi_v<float> / 180.0F;
                if (mx * mx + my * my > 1.0F && std::cos(g) * mx + std::sin(g) * my < 0.0F) {
                    const bool rennt = lauf == "BOTH_RUN1";
                    lauf = rennt ? (st.saberActive ? "BOTH_RUNBACK2" : "BOTH_RUNBACK1")
                                 : (st.saberActive ? "BOTH_WALKBACK2" : "BOTH_WALKBACK1");
                }
            }
            st.animation = lauf;
            st.prevAnimation.clear();
            // Die Animation laeuft MIT dem Tempo - siehe kAnimSpeedWalk in
            // scene.h. Ohne das rutschen die Fuesse ueber den Boden.
            st.animSpeed = (weg->value > 0.0F) ? weg->value : 1.0F;
            if (animTempo > 0.0F) { st.animSpeed = animTempo; }   // rc601 (bewegung, R7)
            // Die Grenze der Engine gilt fuer das ABSOLUTE Tempo gegenueber
            // 20 Bildern je Sekunde (bg_panimate.cpp:4834: animSpeed =
            // 50/frameLerp * Verhaeltnis, hoechstens 1,5). BOTH_RUN1 hat
            // 30 fps - vorher lief es mit 45 fps, also 50 % zu schnell.
            const auto lerp = bildDauerMs.find(lauf);
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
    // --- rc601 (bewegung, Durchsicht 2): BS_NOCLIP behaelt die Beine -------
    //
    // PmoveSingle nimmt in BS_NOCLIP (client->noclip) PM_NoclipMove und kehrt
    // danach zurueck - ohne PM_Footsteps (bg_pmove.cpp:4860-4939 MD). Die
    // Beine behalten also die Animation, die PM_Footsteps im letzten
    // Denkbild davor gesetzt hat: kam BS_NOCLIP mitten im Rennen, laeuft
    // BOTH_RUN1 weiter, auch ueber die Ankunft hinaus. Eine Skriptanimation
    // danach (PM_SetAnim aus ICARUS) gilt trotzdem. Bis zur Durchsicht 2 stand
    // hier die Standanimation (§8.7).
    if (noclipAb >= 0.0 && ms >= noclipAb && beineAbMs < noclipAb && npcFigur) {
        // Das Denkbild, in dem BS_NOCLIP gesetzt wurde, lief noch mit
        // PM_Footsteps (das Skript laeuft am Ende von NPC_Think).
        const ActorState vor = at(noclipAb - 0.01);
        const bool laufAnim = vor.animation == "BOTH_WALK1" || vor.animation == "BOTH_RUN1" ||
                              vor.animation == "BOTH_WALKBACK1" || vor.animation == "BOTH_WALKBACK2" ||
                              vor.animation == "BOTH_RUNBACK1" || vor.animation == "BOTH_RUNBACK2";
        if (vor.moving && laufAnim) {
            st.animation = vor.animation;
            st.animSpeed = vor.animSpeed;
            st.animStartMs = vor.animStartMs;
            st.prevAnimation.clear();
            beineBis = -1.0;
        }
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
    // --- rc601 (bewegung): die Hoehe aus der Schwerkraft ------------------
    //
    // schwerkraftEinrechnen (src/schwerkraft.cpp) hat Z je Pmove-Takt
    // abgelegt. Dazwischen linear, wie cgame zwischen zwei Bildern eines NPC
    // mittelt - nur nicht ueber ein SET_ORIGIN hinweg: dort springt der Ort,
    // und die Vorschau zeigt bis zum Sprung die alte Hoehe, ab dem Sprung die
    // neue.
    if (!hoehe.empty()) {
        const double f = std::max(0.0, ms) / kPmoveTaktMs;
        const std::size_t letzte = hoehe.size() - 1U;
        const std::size_t i = std::min(static_cast<std::size_t>(f), letzte);
        const std::size_t j = std::min(i + 1U, letzte);
        const double ti = static_cast<double>(i) * kPmoveTaktMs;
        const double tj = static_cast<double>(j) * kPmoveTaktMs;
        bool sprung = false;
        bool schonGesprungen = false;
        for (const ActorStep& s : steps) {
            if (s.kind == ActorStep::Kind::Teleport && s.echterSprung && s.startMs > ti + 1e-6 &&
                s.startMs <= tj + 1e-6) {
                sprung = true;
                schonGesprungen = schonGesprungen || s.startMs <= ms;
            }
        }
        if (sprung) {
            st.pos[2] = schonGesprungen ? hoehe[j] : hoehe[i];
        } else {
            const auto r = static_cast<float>(std::min(1.0, f - static_cast<double>(i)));
            st.pos[2] = hoehe[i] + (hoehe[j] - hoehe[i]) * r;
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
        if (key == "scale" || key == "scalex" || key == "scaley" || key == "scalez") {
            const long n = std::strtol(wert.c_str(), nullptr, 10);
            // 0 setzt das Spiel zwar (NPC_stats.cpp), der Renderer nimmt eine
            // 0 aber je Achse als 1 (G2_misc.cpp "check for scales of 0").
            if (n >= 0 && n != 100) {
                const float f = n == 0 ? 1.0F : static_cast<float>(n) / 100.0F;
                if (key == "scale") {
                    cur.skala[0] = cur.skala[1] = cur.skala[2] = f;
                } else {
                    cur.skala[key[5] - 'x'] = f;
                }
            }
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
        // rc601 (bewegung): wie NPC_ParseParms (NPC_stats.cpp:3720-3757, MD):
        // COM_ParseInt, also atoi ("55.9" ist 55); n < 0 wird mit "bad
        // walkSpeed" verworfen, 0 gilt.
        if (key == "walkspeed" || key == "runspeed") {
            const int n = std::atoi(wert.c_str());
            if (n >= 0) {
                (key == "walkspeed" ? cur.walkSpeed : cur.runSpeed) = static_cast<float>(n);
            }
        }
        // rc601 (bewegung): yawSpeed (NPC_stats.cpp:3700-3716, MD) -
        // COM_ParseInt, n <= 0 wird mit "bad yawSpeed" verworfen.
        if (key == "yawspeed") {
            const int n = std::atoi(wert.c_str());
            if (n > 0) {
                cur.yawSpeed = static_cast<float>(n);
            }
        }
        // rc601 (bewegung): fuer die Schwerkraft. "width" setzt mins/maxs
        // [0] und [1] auf -n/n (COM_ParseInt, NPC_stats.cpp:3633-3643);
        // "movetype flyswim" macht einen Flieger (MoveTypeNameToEnum,
        // NPC_stats.cpp:468-487, 3688-3696; Q_stricmp) - PM_FlyMove, keine
        // Schwerkraft.
        if (key == "width") {
            const int n = std::atoi(wert.c_str());
            cur.breite = static_cast<float>(n);
        }
        // rc601 (bewegung, Wege): "height" n -> maxs[2] = n + DEFAULT_MINS_2
        // (NPC_stats.cpp:3645-3672, ausser fliegenden Fahrzeugen) - fuer den
        // Kasten, mit dem STEER::Reached die Ankunft sieht.
        if (key == "height") {
            cur.hoehe = static_cast<float>(std::atoi(wert.c_str()));
        }
        if (key == "movetype") {
            cur.fliegt = sameName(wert, "flyswim");
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
        // rc601 (saber): die Schluessel zum Beruehren (wp_saberLoad.cpp:2794 ff. MD).
        else if (saberKontaktSchluessel(def.kontakt, key, val)) {}
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
    // --- rc601 (bewegung, Review): g_gravity der Karte -------------------
    //
    // SP_worldspawn: G_SpawnString( "gravity", "800", &s ) und
    // cvar_set( "g_gravity", s ) (g_spawn.cpp:1698-1704 MD) - der Wert ist
    // atof des Textes. Die worldspawn ist die erste Entity der Karte.
    for (const MapEntity& e : map.entities) {
        if (e.classname != "worldspawn") { continue; }
        if (const std::string* g = e.find("gravity")) {
            out.schwerkraft = static_cast<float>(std::atof(g->c_str()));
        }
        break;
    }

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
    // rc601 (bewegung, Wege): Wegnetz und Spuren der Karte, beim ersten
    // Laufziel geholt (SzenenHilfe::nav, sonst aus den Entities).
    std::shared_ptr<const NavWelt> navWelt;

    // rc602: eine Figur anlegen - fuer den Namen eines affect (der Spawner
    // wird gesucht) oder fuer eine beim Kartenstart gespawnte Figur, die ihren
    // Spawner mitbringt (festerSpawner, siehe SzenenHilfe::kartenFiguren).
    // Der Inhalt ist der bisherige Aufbau aus der affect-Schleife.
    const auto figurAnlegen = [&](const std::string& name, const MapEntity* festerSpawner) {
            Actor fresh;
            fresh.name = name;
            // Startort aus dem Spawner.
            // Erst der Spawner (der traegt den NPC_type), sonst irgendeine
            // Entity dieses Namens - Tuer, Plattform, Raumschiff.
            // rc602: eine Kartenfigur bringt ihren Spawner mit (festerSpawner).
            const MapEntity* sp = festerSpawner != nullptr ? festerSpawner : findSpawner(map, name);
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
                // rc602: bei einem NPC_* entscheidet die Spawnfunktion der Klasse
                // (bhed/npcklasse.h) - SP_NPC_Droid_R2D2 setzt "r2d2", auch ohne
                // und sogar GEGEN einen NPC_type-Schluessel; SP_NPC_Reborn nur,
                // wenn keiner steht. Vorher galt immer der Schluessel und nur
                // ohne ihn eine Naeherung aus dem Namen (typeFromClassname).
                if (isNpcSpawner(*sp)) {
                    fresh.npcType = effektiverNpcTyp(*sp);
                    fresh.kartenEntity = static_cast<int>(sp - map.entities.data());
                } else if (const std::string* t = sp->find("NPC_type")) {
                    // Andere Klassen mit dem Schluessel (misc_model_breakable ...).
                    fresh.npcType = *t;
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
                    // rc601 (bewegung): 0 gilt (siehe NpcDef::walkSpeed).
                    if (def.walkSpeed >= 0.0F) { fresh.gehTempo = def.walkSpeed; }
                    if (def.runSpeed >= 0.0F) { fresh.laufTempo = def.runSpeed; }
                    npcDef = &def;
                    break;
                }
            }
            // rc601 (bewegung): das Tempo beim Spawnen festhalten - der Ablauf
            // braucht es (ablaufTempo), und gehTempo/laufTempo aendert das
            // Skript gleich.
            // rc601 (bewegung): die Drehrate aus yawSpeed der .npc, und wer
            // der Spieler selbst ist (kein NPC).
            // rc601 (bewegung, Durchsicht 2): dazu die Schwierigkeit
            // (npcWerteBeimSpawnen) - auch auf die Vorgabe yawSpeed 90
            // (NPC_stats.cpp:2324 MD). Nur NPCs: der Spieler laeuft nicht
            // durch NPC_Begin.
            if (npcDef != nullptr && !istSpieler) {
                float yaw = npcDef->yawSpeed > 0.0F ? npcDef->yawSpeed : 90.0F;
                npcWerteBeimSpawnen(*npcDef, fresh.npcType,
                                    hilfe != nullptr ? hilfe->schwierigkeit : kSchwierigkeitVorgabe, yaw, fresh.gehTempo,
                                    fresh.laufTempo);
                fresh.startDrehrate = turnRateFor(yaw);
            }
            fresh.spawnGehTempo = fresh.gehTempo;
            fresh.spawnLaufTempo = fresh.laufTempo;
            fresh.echterSpieler = istSpieler;
            // Waffe und Schwerter beim Spawnen. spawnflags braucht es nur
            // fuer die Vorgabewaffe (SFB_RIFLEMAN, SFB_PHASER).
            int spawnflags = 0;
            if (sp != nullptr) {
                if (const std::string* sf = sp->find("spawnflags")) {
                    spawnflags = std::atoi(sf->c_str());
                }
            }
            ausstattungBeimSpawnen(fresh, npcDef, hilfe, spawnflags);
            // rc601 (bewegung, R5): NPC_UpdateAngles (NPC_utils.cpp:274-290
            // MD) - WP_EMPLACED_GUN dreht mit yawSpeed 20, ein Rocket Trooper
            // ohne Feind mit 1 ("just slowly lookin' around"). SET_ENEMY bildet
            // behaved nicht nach: der Rocket Trooper gilt als ohne Feind.
            if (fresh.startWaffe == "WP_EMPLACED_GUN") {
                fresh.festeDrehrate = turnRateFor(20.0F);
            } else if (npcDef != nullptr && sameName(npcDef->klasse, "CLASS_ROCKETTROOPER")) {
                fresh.festeDrehrate = turnRateFor(1.0F);
            }
            // rc601 (bewegung, Durchsicht 2): die Klasse fuer npcDrehrate (die
            // Waffe zaehlt je Denkbild, auch nach SET_WEAPON).
            fresh.raketenTruppler = npcDef != nullptr && sameName(npcDef->klasse, "CLASS_ROCKETTROOPER");
            fresh.rang = npcDef != nullptr ? npcDef->rang : std::string();   // rc601 integ
            // rc601 (bewegung, gemeinsam mit saber): ein NPC_spawner mit
            // spawnflags 32 (SFB_CINEMATIC) beginnt in BS_CINEMATIC
            // (NPC_SetMiscDefaultData, NPC_spawn.cpp:315-319).
            if (sp != nullptr && isNpcSpawner(*sp) && (spawnflags & 32) != 0) {
                ActorStep kino;
                kino.kind = ActorStep::Kind::Verhalten;
                kino.text = "BS_CINEMATIC";
                fresh.steps.push_back(std::move(kino));
            }
            // --- rc601 (bewegung): was die Schwerkraft wissen muss ----------
            //
            // Die Breite des Kastens ("width", sonst 16), die spawnflags
            // (SFB_STARTINSOLID 128 laesst G_CheckInSolid aus), und wer gar
            // nicht faellt (NPC_SetMiscDefaultData, NPC_spawn.cpp:646-670,
            // 940-1006 in Movie Duels):
            //   * Flieger: TEAM_PLAYER CLASS_SEEKER; TEAM_ENEMY CLASS_PROBE,
            //     _REMOTE, _GLIDER, _INTERROGATOR, _SENTRY (ps.gravity = 0,
            //     MT_FLYSWIM), dazu "movetype flyswim" aus der .npc.
            //     OpenJK kennt CLASS_GLIDER hier nicht.
            //   * Fahrzeuge (CLASS_VEHICLE): Jaeger und Gleiter fliegen bzw.
            //     schweben (FLY_VEHICLE/FLY_HOVER, bg_pmove.cpp:33205-33221);
            //     Laeufer fallen zwar, behaved kennt die Fahrzeugart aber
            //     nicht - alle bleiben ohne Schwerkraft (Naeherung).
            //   * JSF_AMBUSH (spawnflags 16, "CEILING") haengt (client->noclip)
            //     bei TEAM_PLAYER CLASS_JEDI, _KYLE, _AHSOKA, _JEDIMASTER,
            //     _LUKE und TEAM_ENEMY CLASS_TAVION, _ALORA, _GALEN, _REBORN
            //     mit WP_SABER, _DESANN, _SITHLORD, _BOC, _VADER,
            //     _SHADOWTROOPER. OpenJK: JEDI/KYLE/LUKE und TAVION/ALORA/
            //     REBORN mit Klinge/DESANN/SHADOWTROOPER.
            fresh.spawnflags = spawnflags;
            if (npcDef != nullptr && npcDef->breite >= 0.0F) {
                fresh.halbBreite = npcDef->breite;
            }
            if (npcDef != nullptr && npcDef->hoehe >= 0.0F) {
                fresh.hoeheOben = npcDef->hoehe - 24.0F;   // rc601 (bewegung, Wege): n + DEFAULT_MINS_2
            }
            // rc601 (bewegung, R6): die Stufe je Klasse (PM_StepSlideMove,
            // bg_slidemove.cpp:384-416 MD): CLASS_ATST 70, CLASS_RANCOR 48
            // bzw. mit spawnflags 1 (Mutant) 64, sonst maxs[2] <= 0 nur 4.
            if (npcDef != nullptr && sameName(npcDef->klasse, "CLASS_ATST")) {
                fresh.stufe = 70.0F;
            } else if (npcDef != nullptr && sameName(npcDef->klasse, "CLASS_RANCOR")) {
                fresh.stufe = (spawnflags & 1) != 0 ? 64.0F : 48.0F;
            } else if (fresh.hoeheOben <= 0.0F) {
                fresh.stufe = 4.0F;
            }
            if (npcDef != nullptr) {
                const auto klasse = [npcDef](std::initializer_list<const char*> namen) {
                    for (const char* n : namen) {
                        if (sameName(npcDef->klasse, n)) { return true; }
                    }
                    return false;
                };
                const bool spielerTeam = sameName(npcDef->team, "TEAM_PLAYER");
                const bool feindTeam = sameName(npcDef->team, "TEAM_ENEMY");
                fresh.ohneSchwerkraft =
                    npcDef->fliegt || klasse({"CLASS_VEHICLE"}) || (spielerTeam && klasse({"CLASS_SEEKER"})) ||
                    (feindTeam && klasse({"CLASS_PROBE", "CLASS_REMOTE", "CLASS_GLIDER", "CLASS_INTERROGATOR",
                                          "CLASS_SENTRY"}));
                const bool lauerer =
                    (spielerTeam && klasse({"CLASS_JEDI", "CLASS_KYLE", "CLASS_AHSOKA", "CLASS_JEDIMASTER",
                                            "CLASS_LUKE"})) ||
                    (feindTeam && (klasse({"CLASS_TAVION", "CLASS_ALORA", "CLASS_GALEN", "CLASS_DESANN",
                                           "CLASS_SITHLORD", "CLASS_BOC", "CLASS_VADER", "CLASS_SHADOWTROOPER"}) ||
                                   (klasse({"CLASS_REBORN"}) && sameName(npcDef->weapon, "WP_SABER"))));
                fresh.haengtAnfangs = lauerer && (spawnflags & 16) != 0 && sp != nullptr && isNpcSpawner(*sp);
            }
            out.actors.push_back(std::move(fresh));
    };

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
            figurAnlegen(name, nullptr);
            actor = &out.actors.back();
        }

        // Den Block durchgehen. Die Uhr der Figur beginnt beim Stand des
        // affect und laeuft in ihrem eigenen Takt weiter.
        double t = now;
        // Gehen, wenn WALKING gesetzt und RUNNING nicht (RUNNING geht vor).
        //
        // rc601 (bewegung): ohne Schalter entscheidet das Verhalten. Nur
        // NPC_BSCinematic ruft NPC_MoveToGoal ohne BUTTON_WALKING (dann gilt
        // runSpeed); NPC_BSDefault setzt es vorher (AI_Default.cpp:1066-1081,
        // ebenso jedi_patrol, AI_Jedi.cpp:10176), und STEER::Activate nimmt
        // dann walkSpeed (g_navigator.cpp:3990-3997). SCF_RUNNING geht immer
        // vor.
        const auto verhaltenBei = [actor](double tt) {
            const std::string* v = nullptr;
            double ab = -1.0e300;
            for (const ActorStep& st : actor->steps) {
                if (st.kind == ActorStep::Kind::Verhalten && st.startMs <= tt + 0.5 && st.startMs >= ab) {
                    v = &st.text;
                    ab = st.startMs;
                }
            }
            return v == nullptr ? std::string{} : *v;
        };
        double gehZeit = 0.0;   // wann gefragt wird (setzt wegPlanen)
        const auto gehend = [actor, &verhaltenBei, &gehZeit]() {
            if (actor->laufSchalter) {
                return false;
            }
            return verhaltenBei(gehZeit) == "BS_CINEMATIC" ? actor->gehSchalter : true;
        };
        // Ein NPC (nicht der Spieler selbst, keine Tuer): nur ihn treffen
        // SET_NAVGOAL, SET_WATCHTARGET, SET_DYAW, SET_BEHAVIOR_STATE.
        const bool npc = !actor->npcType.empty() && !actor->echterSpieler && actor->brushModel <= 0;
        // --- Die Geschwindigkeiten der Figur ----------------------------
        //
        // SET_WALKSPEED kommt in md_ga_jedi 34 mal vor, SET_RUNSPEED 26 mal
        // - und behaved hat beide bisher uebergangen und stur mit 90 bzw.
        // 300 gerechnet.
        //
        // Pro FIGUR, nicht pro Block - ein SET_WALKSPEED gilt weiter. Seit
        // rc601 (bewegung) gilt es auch fuer einen Weg, der schon laeuft:
        // siehe den Regler unten. Bis rc600 stand hier die Annahme, die
        // Geschwindigkeit stehe immer VOR dem Wegziel - die Skripte der Mod
        // setzen sie aber oft danach (intro_jedi.txt:147-149).
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

        // =================================================================
        // Paket bewegung (rc601): das Laufziel als Regler
        // =================================================================
        //
        // Gemeldet: "NPCs don't walk to waypoints correctly. Looks like they
        // ignore WALK_SPEED and RUN_SPEED."
        //
        // Bis rc600 wurde ein Weg EINMAL beim SET_NAVGOAL geplant - mit
        // Gangart und Tempo dieses Augenblicks und vom VORIGEN Ziel aus. Die
        // Engine fuehrt einen Weg anders: in jedem Denkbild ruft
        // NPC_MoveToGoal STEER::Activate, und das liest SCF_RUNNING,
        // SCF_WALKING, stats.runSpeed und stats.walkSpeed NEU
        // (g_navigator.cpp:3983-3997), steuert vom AKTUELLEN Ort
        // (currentOrigin) zum goalEntity und haelt an, sobald goalEntity
        // fehlt (SET_NAVGOAL "NULL": Q3_Interface.cpp:2137-2142).
        //
        // Darum hier ein Ziel je Figur (Actor::zielGesetzt/zielOrt), und
        // jede Aenderung - ein neues Ziel, NULL, Gangart, Tempo, SET_ORIGIN -
        // schneidet den laufenden Weg am aktuellen Ort ab und plant den Rest
        // neu. Steht die Aenderung im selben Bild wie das Ziel, wird nur neu
        // geplant: die Engine bewegt die Figur erst im naechsten Denkbild.
        //
        // rc601 (bewegung, Wege wie im Spiel): Das Modell eines Weges ist
        // nicht mehr gerade und gleichfoermig (gehDauerMs), sondern die
        // Engine selbst, Denkbild fuer Denkbild: STEER::GoTo, sonst NAV::GoTo
        // ueber das Wegnetz, sonst STEER::Stop, dann ein Pmove (planeLauf,
        // bhed/navlauf.h). Ein Plan laeuft vom Ziel bis zur Ankunft und dem
        // Stillstand danach; jede Aenderung schneidet ihn zur Zeit tt ab und
        // plant ab dem ZUSTAND dort weiter (Ort, ps.velocity, Timer, Pfad) -
        // genau wie die Engine, die in jedem Denkbild neu liest. Biegt der Weg
        // um eine Ecke des Wegnetzes, wird er in Stuecke geteilt (gleicher
        // path, nahtlos, teilweg ausser dem letzten), damit Zeitleiste und
        // Missions-Tab ihn wie einen Lauf behandeln.
        enum class Halt : std::uint8_t { Keiner, Frueher, GleichesBild, Geschnitten, Angekommen };
        constexpr double kGleichzeitig = 0.5;   // ms: dasselbe Bild
        // Kam der Zustand gerade aus einem abgeschnittenen Plan? Dann gilt
        // seine Gier (NPC_UpdateAngles in der Rechnung) - sonst die der Szene.
        bool gierAusPlan = false;
        if (!actor->laufZustandDa) {
            actor->laufZustand = LaufZustand{};
            actor->laufZustandDa = true;
            // rc601 (bewegung, teil 3): der erste Plan beginnt bei NPC_Begin
            // (G_CheckInSolid, die ersten Pmoves) - die Figur faellt im Spiel
            // sofort, nicht erst mit dem ersten Laufziel. Nicht fuer den
            // Spieler, Brush-Modelle, Flieger und haengende JSF_AMBUSH-Jedi.
            if (actor->brushModel <= 0 && !actor->echterSpieler && !actor->ohneSchwerkraft && !actor->haengtAnfangs &&
                !actor->npcType.empty()) {
                double spawn = kNavKartenSpawnMs;
                if (hilfe != nullptr && hilfe->erscheint != nullptr) {
                    const auto it = hilfe->erscheint->find(actor->name);
                    if (it != hilfe->erscheint->end()) { spawn = it->second; }
                }
                actor->laufZustand.gesetztMs = spawn;
                actor->laufZustand.gesetztSpawn = true;
                // rc601 integ: das erste NPC_Think (wie der Vorlauf, 150 ms
                // nach NPC_Begin) - der Bezug fuer den Takt des Verhaltens.
                actor->laufZustand.denkNullMs = spawn + 150.0;
                actor->laufZustand.gesetztImFesten = (actor->spawnflags & 128) == 0;   // SFB_STARTINSOLID
            }
        }
        // Die Stuecke des laufenden Plans loeschen (von hinten, die Indizes
        // davor bleiben gueltig).
        const auto stueckeLoeschen = [&](double ab) {
            std::vector<std::size_t>& st = actor->laufStuecke;
            for (std::size_t k = st.size(); k-- > 0;) {
                const std::size_t i = st[k];
                if (i < actor->steps.size() && actor->steps[i].startMs >= ab - kGleichzeitig) {
                    actor->steps.erase(actor->steps.begin() + static_cast<std::ptrdiff_t>(i));
                    st.erase(st.begin() + static_cast<std::ptrdiff_t>(k));
                }
            }
        };
        // Den laufenden Plan zur Zeit tt anhalten. Danach steht in `o` der
        // Ort, und actor->laufZustand ist der Zustand der Figur zu tt.
        // `fahrt` und `neu` bleiben fuer die Aufrufer (immer false/-1).
        const auto wegAnhalten = [&](double ttBefehl, float o[3], bool& fahrt, int& neu) -> Halt {
            // rc601 (bewegung, Durchsicht 2): im Denkbild des Befehls
            // (navDenkbildAb) - der Ort und Zustand NACH diesem Denkbild.
            const double tt = navDenkbildAb(ttBefehl);
            neu = -1;
            fahrt = false;
            for (int k = 0; k < 3; ++k) { o[k] = here[k]; }
            if (!actor->laufPlan) {
                return Halt::Keiner;
            }
            const std::shared_ptr<const LaufPlan> plan = actor->laufPlan;
            if (tt < plan->abMs - kGleichzeitig) {
                // Ein frueheres affect, das erst jetzt gelesen wird (rohe
                // Skripte mit mehreren Bloecken): nicht rueckwaerts in einen
                // Weg eingreifen, der spaeter beginnt.
                return Halt::Frueher;
            }
            actor->laufPlan.reset();
            if (tt <= plan->abMs + kGleichzeitig) {
                // Im selben Bild: noch kein Denkbild, der Plan entfaellt.
                stueckeLoeschen(plan->abMs);
                actor->laufStuecke.clear();
                actor->laufZustand = plan->anfang;
                for (int k = 0; k < 3; ++k) { o[k] = plan->anfang.ort[static_cast<std::size_t>(k)]; }
                return Halt::GleichesBild;
            }
            const NavVec jetzt = ortBei(*plan, tt);
            actor->laufZustand = zustandBei(*plan, tt);
            gierAusPlan = true;
            actor->laufZustand.ort = jetzt;
            stueckeLoeschen(tt + 2.0 * kGleichzeitig);
            for (const std::size_t i : actor->laufStuecke) {
                ActorStep& w = actor->steps[i];
                if (w.startMs < tt && w.endMs > tt + kGleichzeitig) {
                    for (int k = 0; k < 3; ++k) { w.to[k] = jetzt[static_cast<std::size_t>(k)]; }
                    w.endMs = tt;
                    w.teilweg = true;
                    if (w.ankunftMs > tt) { w.ankunftMs = -1.0; }
                }
            }
            actor->laufStuecke.clear();
            for (int k = 0; k < 3; ++k) { o[k] = jetzt[static_cast<std::size_t>(k)]; }
            if (plan->ankunftMs >= 0.0 && tt >= plan->ankunftMs - kGleichzeitig) {
                // Angekommen: NPC_ReachedGoal loescht das Ziel
                // (NPC_goal.cpp:141-164, NPC_ClearGoal).
                actor->zielGesetzt = false;
                return Halt::Angekommen;
            }
            return Halt::Geschnitten;
        };
        // Die Drehrate zur Zeit tt (SET_YAWSPEED, sonst die der .npc).
        const auto drehRateBei = [&](double tt) {
            // R5, rc601 (bewegung, Durchsicht 2): je Waffe in der Hand
            // (npcDrehrate), sonst SET_YAWSPEED bzw. die .npc.
            float r = actor->startDrehrate;
            std::string waffe;
            for (const ActorStep& st : actor->steps) {
                if (st.startMs > tt + kGleichzeitig) { continue; }
                if (st.kind == ActorStep::Kind::TurnRate) { r = st.value; }
                if (st.kind == ActorStep::Kind::Waffe) { waffe = st.text; }
            }
            return npcDrehrate(*actor, waffe, r);
        };
        // desiredYaw von NPC_BSCinematic mit Watchtarget (NPC_behavior.cpp:
        // 304-315) - nur in BS_CINEMATIC, das Ziel wie in Actor::at.
        const auto watchGierFn = [&](double tt) -> std::function<bool(double, const NavVec&, float&)> {
            if (verhaltenBei(tt) != "BS_CINEMATIC") { return {}; }
            const ActorStep* w = nullptr;
            for (const ActorStep& st : actor->steps) {
                if (st.kind == ActorStep::Kind::Watch && st.startMs <= tt + kGleichzeitig) { w = &st; }
            }
            if (w == nullptr || w->value < 0.5F) { return {}; }
            const ActorStep kopie = *w;
            return [kopie](double t, const NavVec& ich, float& gier) {
                if (kopie.index == 1) {
                    gier = 0.0F;
                    return true;
                }
                float da[3] = {kopie.to[0], kopie.to[1], kopie.to[2]};
                if (!kopie.spur.empty()) {
                    const double f = std::max(0.0, t - kopie.startMs) / kopie.spurSchrittMs;
                    const auto i = std::min(static_cast<std::size_t>(f), kopie.spur.size() - 1U);
                    const std::size_t j = std::min(i + 1U, kopie.spur.size() - 1U);
                    const auto r = static_cast<float>(std::min(1.0, f - static_cast<double>(i)));
                    for (int k = 0; k < 3; ++k) {
                        const auto kk = static_cast<std::size_t>(k);
                        da[k] = kopie.spur[i][kk] + (kopie.spur[j][kk] - kopie.spur[i][kk]) * r;
                    }
                }
                const float dx = da[0] - ich[0];
                const float dy = da[1] - ich[1];
                gier = (dx == 0.0F && dy == 0.0F) ? 0.0F : std::atan2(dy, dx) * 180.0F / std::numbers::pi_v<float>;
                return true;
            };
        };
        // Einen Plan ab `von` zur Zeit tt. `neu` und `fahrt` sind ohne
        // Bedeutung (der Zustand traegt alles). `merken`: er wird der
        // laufende Plan (sonst ein eigener Weg aus einem frueheren affect).
        const auto wegPlanen = [&](double ttBefehl, const float von[3], bool fahrt, int neu, bool merken) {
            // rc601 (bewegung, Durchsicht 2): der Plan beginnt im Denkbild des
            // Befehls (navDenkbildAb), bewegt wird ab dem naechsten.
            const double tt = navDenkbildAb(ttBefehl);
            (void)fahrt;
            (void)neu;
            for (int k = 0; k < 3; ++k) { here[k] = von[k]; }
            LaufZustand z = merken ? actor->laufZustand : LaufZustand{};
            const NavVec vonV{von[0], von[1], von[2]};
            // rc601 (bewegung, teil 3): eine andere Hoehe allein ist kein Sprung
            // (sie kommt aus dem Fallen im Plan; ein SET_ORIGIN traegt gesetztMs).
            if (z.ort[0] != vonV[0] || z.ort[1] != vonV[1] || (z.ort[2] != vonV[2] && z.hatVorlauf())) {
                // Ein Sprung (SET_ORIGIN, Spawn) seit dem letzten Plan:
                // G_SetOrigin - Pfad weg, Geschwindigkeit bleibt.
                z.springen(vonV);
            }
            if (!gierAusPlan || !merken) {
                z.gier = actor->steps.empty() ? actor->startAngles[1] : actor->at(tt).angles[1];
                z.wunschGier = z.gier;
            }
            gierAusPlan = false;
            const bool bewegtSich = z.tempoVek[0] != 0.0F || z.tempoVek[1] != 0.0F || z.tempoVek[2] != 0.0F;
            if (!actor->zielGesetzt && !bewegtSich) {
                if (merken) { actor->laufZustand = z; }
                return;
            }
            // STEER::Activate (g_navigator.cpp:3983-3997): mMaxSpeed =
            // runSpeed, ausser SCF_WALKING ohne SCF_RUNNING - dann walkSpeed.
            gehZeit = tt;
            const float v = gehend() ? walkSpeed : runSpeed;
            LaufWerte werte;
            werte.zielGesetzt = actor->zielGesetzt;
            for (int k = 0; k < 3; ++k) { werte.ziel[static_cast<std::size_t>(k)] = actor->zielOrt[k]; }
            werte.zielRadius = static_cast<float>(actor->zielRadius);
            werte.maxTempo = v;
            werte.gehTempo = walkSpeed;
            werte.drehRate = drehRateBei(tt);
            werte.halbBreite = actor->halbBreite;
            werte.hoeheOben = actor->hoeheOben;
            werte.stufe = actor->stufe;
            werte.gangSchalter = actor->laufSchalter ? 2 : (actor->gehSchalter ? 1 : 0);
            werte.fremdGier = watchGierFn(tt);
            // rc601 integ: Takt des Verhaltens (OpenJK 100 ms, Movie Duels jedes
            // Denkbild) mit der Waffe in der Hand zu Beginn des Plans.
            {
                std::string wa = actor->at(tt).waffe;
                if (wa.empty() || wa == "drop") { wa = wa.empty() ? actor->startWaffe : "WP_NONE"; }
                werte.verhaltenTaktMs = npcVerhaltenTaktMs(navMovieDuelsRegeln(), wa,
                                                           hilfe != nullptr ? hilfe->schwierigkeit : kSchwierigkeitVorgabe,
                                                           actor->rang);
            }
            if (!navWelt) { navWelt = (hilfe != nullptr && hilfe->nav) ? hilfe->nav : navWeltAusEntities(map.entities); }
            // BS_NOCLIP: NPC_BSNoClip + PM_NoclipMove mit dem ps.speed, das
            // die Figur gerade hat (bhed/navlauf.h, LaufWerte::ohneKoerper).
            werte.ohneKoerper = verhaltenBei(tt) == "BS_NOCLIP";
            // rc601 (bewegung, teil 3): ps.gravity (g_gravity der Karte, ab
            // SET_GRAVITY der eigene Wert) und ob sie faellt.
            werte.fallen = !actor->ohneSchwerkraft;
            werte.schwerkraft = out.schwerkraft;
            for (const auto& [ms, g] : actor->eigeneSchwerkraft) {
                if (ms <= tt + kGleichzeitig) { werte.schwerkraft = g; }
            }
            // rc601 (bewegung, teil 3): Vorlauf ab Spawn/SET_ORIGIN.
            double planAb = tt;
            const bool vorlauf = merken && z.hatVorlauf() && z.gesetztMs < tt - kGleichzeitig && !werte.ohneKoerper;
            if (vorlauf) {
                planAb = z.gesetztMs;
                werte.zielAbMs = tt;
                werte.spawnVorlauf = z.gesetztSpawn;
                werte.spawnImFesten = z.gesetztImFesten;
            }
            std::shared_ptr<const LaufPlan> voll;
            // rc601 (bewegung, teil 3): der Plan aus dem gemeinsamen Takt aller
            // Figuren (navKoerperLauf ueber die Ablauf-Rechnung), wenn es fuer
            // genau diese Eingaben einen gibt - sonst ohne die anderen.
            if (hilfe != nullptr && hilfe->koerperPlaene) {
                const auto it = hilfe->koerperPlaene->find(
                    navKoerperSchluessel(actor->name, navPlanSchluessel(*navWelt, z, werte, planAb, tt + 120000.0)));
                if (it != hilfe->koerperPlaene->end()) { voll = it->second; }
            }
            if (!voll) { voll = std::make_shared<const LaufPlan>(planeLauf(*navWelt, z, werte, planAb, tt + 120000.0)); }
            std::shared_ptr<const LaufPlan> planGeteilt = voll;
            if (vorlauf) {
                // Die Stuecke beginnen beim Befehl: der Plan ab tt; der
                // Vorlauf (fuer die Hoehe) in Actor::vorlaeufe.
                LaufPlan sicht;
                sicht.abMs = tt;
                sicht.anfang = zustandBei(*voll, tt);
                sicht.anfang.ort = ortBei(*voll, tt);
                for (std::size_t i = 0; i < voll->takte.size(); ++i) {
                    if (voll->takte[i].t <= tt + kGleichzeitig) { continue; }
                    sicht.takte.push_back(voll->takte[i]);
                    sicht.zustaende.push_back(voll->zustaende[i]);
                }
                sicht.ankunftMs = voll->ankunftMs;
                sicht.ruheMs = voll->ruheMs;
                sicht.nie = voll->nie;
                sicht.zielAnfangsErreicht = voll->zielAnfangsErreicht;
                sicht.hoeheGilt = voll->hoeheGilt;
                planGeteilt = std::make_shared<const LaufPlan>(std::move(sicht));
                actor->vorlaeufe.emplace_back(voll, tt);
                for (int k = 0; k < 3; ++k) { here[k] = planGeteilt->anfang.ort[static_cast<std::size_t>(k)]; }
            }
            const std::shared_ptr<const LaufPlan> plan = planGeteilt;
            if (merken) {
                actor->laufZustand = plan->takte.empty() ? z : plan->zustaende.back();
            }
            const bool schonGemeldet = !actor->stehtOhneWeg.empty() && actor->stehtOhneWeg.back().pfad == actor->zielPfad &&
                                       actor->stehtOhneWeg.back().ziel[0] == actor->zielOrt[0] &&
                                       actor->stehtOhneWeg.back().ziel[1] == actor->zielOrt[1];
            if (plan->nie && werte.zielGesetzt && v > 0.0F && merken && !schonGemeldet && !werte.ohneKoerper) {
                // Kein Weg: die Figur steht (STEER::Stop in jedem Denkbild),
                // TID_MOVE_NAV wird nie fertig. Fuer den Hinweis - einmal je
                // SET_NAVGOAL, nicht je neuer Planung.
                Actor::StehtOhneWeg h;
                h.pfad = actor->zielPfad;
                h.ms = tt;
                h.abstand = distance(von, actor->zielOrt);
                for (int k = 0; k < 3; ++k) {
                    h.von[static_cast<std::size_t>(k)] = von[k];
                    h.ziel[static_cast<std::size_t>(k)] = actor->zielOrt[k];
                }
                actor->stehtOhneWeg.push_back(h);
            }
            if (plan->zielAnfangsErreicht && werte.zielGesetzt && merken) {
                // Schon im Radius oder im Kasten: STEER::Reached im ersten
                // Denkbild, NPC_ReachedGoal loescht das Ziel.
                actor->zielGesetzt = false;
            }
            // Die Stuecke: ein Wechsel des Steuerziels (Pfadpunkt des
            // Wegnetzes oder direkt), der mindestens zwei Denkbilder haelt.
            const std::vector<LaufTakt>& tk = plan->takte;
            std::size_t letzteBewegung = tk.size();
            for (std::size_t i = tk.size(); i-- > 0;) {
                const NavVec& a = tk[i].ort;
                const NavVec& b = i == 0 ? plan->anfang.ort : tk[i - 1].ort;
                if (a != b) {
                    letzteBewegung = i;
                    break;
                }
            }
            if (letzteBewegung == tk.size()) {
                // Sie bewegt sich nie (kein Weg, Tempo 0, schon da).
                if (merken) {
                    actor->laufPlan = plan;
                    actor->laufStuecke.clear();
                }
                if (werte.ohneKoerper && werte.zielGesetzt && merken && !tk.empty() && !plan->zielAnfangsErreicht) {
                    // NPC_BSNoClip setzt desiredYaw in jedem Denkbild aufs Ziel,
                    // auch wenn ps.speed 0 ist - NPC_UpdateAngles dreht dorthin.
                    ActorStep dreh;
                    dreh.kind = ActorStep::Kind::Wunschgier;
                    dreh.startMs = tt;   // wie SET_DYAW: zur Zeit des Befehls
                    dreh.endMs = dreh.startMs;
                    dreh.value = tk.front().zustandWunschGier;
                    dreh.path = actor->zielPfad;
                    actor->steps.push_back(std::move(dreh));
                    actor->laufStuecke.push_back(actor->steps.size() - 1);
                }
                return;
            }
            std::vector<std::size_t> grenzen;   // Index des Takts, an dem ein Stueck endet
            // Ein Stop-Denkbild (ps.speed 0) hat kein Steuerziel - es zaehlt
            // zum laufenden Stueck.
            int aktuell = tk.front().stueck;
            for (std::size_t i = 0; i <= letzteBewegung; ++i) {
                if (tk[i].tempo > 0) {
                    aktuell = tk[i].stueck;
                    break;
                }
            }
            // Ein Stueck dauert mindestens drei Denkbilder - ein Pfadpunkt, der
            // im ersten Denkbild schon im Radius liegt (UpdatePath nimmt ihn
            // dann sofort weg), gibt keine eigene Ecke.
            std::size_t stueckAb = 0;
            for (std::size_t i = 1; i <= letzteBewegung; ++i) {
                if (tk[i].stueck == aktuell || tk[i].tempo == 0) { continue; }
                const bool haelt = i + 1 <= letzteBewegung && (tk[i + 1].stueck == tk[i].stueck || tk[i + 1].tempo == 0);
                if (!haelt) { continue; }
                aktuell = tk[i].stueck;
                if (i - stueckAb < 3) { continue; }
                grenzen.push_back(i - 1);
                stueckAb = i;
            }
            grenzen.push_back(letzteBewegung);
            // Gehen oder Laufen waehlt BUTTON_WALKING (PM_Footsteps,
            // bg_pmove.cpp:15358). STEER::DeActivate setzt es unter
            // walkSpeed + 5 (g_navigator.cpp:4170-4177), danach
            // NPC_ApplyScriptFlags (NPC.cpp:800-813): SCF_RUNNING loescht es,
            // sonst setzt SCF_WALKING es. Ein "Lauf" mit runSpeed 60 ist also
            // ein Gang.
            const bool gehAnim = !actor->laufSchalter && (actor->gehSchalter || v < walkSpeed + 5.0F);
            double ab = tt;
            NavVec abOrt = plan->anfang.ort;
            if (merken) {
                actor->laufPlan = plan;
                actor->laufStuecke.clear();
            }
            for (std::size_t g = 0; g < grenzen.size(); ++g) {
                const LaufTakt& ende = tk[grenzen[g]];
                ActorStep step;
                step.kind = ActorStep::Kind::WalkTo;
                step.startMs = ab;
                step.endMs = ende.t;
                for (int k = 0; k < 3; ++k) {
                    step.from[k] = abOrt[static_cast<std::size_t>(k)];
                    step.to[k] = ende.ort[static_cast<std::size_t>(k)];
                    step.laufZiel[k] = actor->zielOrt[k];   // rc601 (bewegung), bleibt beim Schnitt
                }
                step.lauf = plan;
                const bool letztes = g + 1 == grenzen.size();
                step.teilweg = !(letztes && plan->ankunftMs >= 0.0);
                if (plan->ankunftMs >= 0.0 && plan->ankunftMs > ab + kGleichzeitig &&
                    plan->ankunftMs <= ende.t + kGleichzeitig) {
                    step.ankunftMs = plan->ankunftMs;
                } else if (letztes && plan->ankunftMs >= 0.0 && step.ankunftMs < 0.0 && plan->ankunftMs >= ab) {
                    step.ankunftMs = plan->ankunftMs;
                }
                step.text = gehAnim ? "BOTH_WALK1" : "BOTH_RUN1";
                // Die Animation laeuft MIT dem Tempo (siehe unten bei
                // SET_NAVGOAL und kAnimSpeedWalk in scene.h).
                step.value = std::clamp(v / (gehAnim ? kAnimSpeedWalk : kAnimSpeedRun), kAnimSpeedMin, kAnimSpeedMax);
                step.path = actor->zielPfad;
                actor->steps.push_back(std::move(step));
                if (merken) { actor->laufStuecke.push_back(actor->steps.size() - 1); }
                ab = ende.t;
                abOrt = ende.ort;
            }
            for (int k = 0; k < 3; ++k) { here[k] = abOrt[static_cast<std::size_t>(k)]; }
        };
        // Gangart oder Tempo haben sich geaendert: den Rest neu planen.
        const auto neuPlanen = [&](double tt) {
            if (!actor->laufPlan) {
                return;
            }
            float o[3];
            bool fahrt = false;
            int neu = -1;
            if (wegAnhalten(tt, o, fahrt, neu) == Halt::Frueher) {
                return;
            }
            wegPlanen(tt, o, fahrt, neu, true);
        };
        // Q3_SetWalkSpeed / Q3_SetRunSpeed (Q3_Interface.cpp:3621-3657 MD)
        // setzen auch ps.speed: den Plan zur Zeit tt schneiden, ps.speed
        // setzen, neu planen.
        const auto psTempoSetzen = [&](double tt, int wert) {
            if (!actor->laufPlan) {
                actor->laufZustand.tempo = wert;
                return;
            }
            float o[3];
            bool fahrt = false;
            int neu = -1;
            if (wegAnhalten(tt, o, fahrt, neu) == Halt::Frueher) {
                return;
            }
            actor->laufZustand.tempo = wert;
            wegPlanen(tt, o, fahrt, neu, true);
        };

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
                // rc601 (bewegung): Lerp2Pos lehnt jeden client ab ("NOT a
                // mover", Q3_Interface.cpp:8571-8575) - eine Figur bewegt
                // move nicht, und die Aufgabe wird nie fertig.
                if (!actor->npcType.empty()) {
                    continue;
                }
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
                // rc601 (bewegung): ebenso Lerp2Angles (Q3_Interface.cpp:
                // 8686-8690) - eine Figur dreht sich mit SET_DYAW.
                if (!actor->npcType.empty()) {
                    continue;
                }
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
                // rc601 (bewegung): set ( SET_ORIGIN ) ruft G_SetOrigin
                // (Q3_Interface.cpp:8772 ff., auch OpenJK:8249), NICHT
                // Q3_SetOrigin: der Ort gilt genau, die Geschwindigkeit
                // bleibt, und goalEntity bleibt stehen (g_utils.cpp:1585-1613
                // loescht nur den Pfad). Die Figur laeuft also vom neuen Ort
                // weiter zu ihrem Ziel. Der laufende Weg endet hier.
                float alt[3];
                bool fahrt = false;
                int neu = -1;
                const Halt halt = wegAnhalten(t, alt, fahrt, neu);
                if (neu >= 0) {
                    // Im selben Bild geplant: der Weg kommt NACH den Sprung.
                    actor->steps.erase(actor->steps.begin() + neu);
                }
                ActorStep step;
                step.kind = ActorStep::Kind::Teleport;
                step.startMs = t;
                step.endMs = t;
                readVec(*v, step.to);
                for (int k = 0; k < 3; ++k) {
                    step.from[k] = alt[k];
                    here[k] = step.to[k];
                }
                step.path = p;
                // Ein echtes SET_ORIGIN: die Schwerkraft nimmt seine Hoehe
                // (schwerkraftEinrechnen), eingefuegte Spruenge nicht.
                step.echterSprung = true;
                const float neuOrt[3] = {step.to[0], step.to[1], step.to[2]};
                actor->steps.push_back(std::move(step));
                // rc601 (bewegung, teil 3): G_SetOrigin am Ende dieses
                // Denkbilds - der naechste Plan beginnt hier (die Figur faellt
                // ab dem naechsten Pmove).
                if (!actor->laufZustandDa) {
                    actor->laufZustand = LaufZustand{};
                    actor->laufZustandDa = true;
                }
                if (actor->brushModel <= 0 && !actor->echterSpieler && !actor->ohneSchwerkraft) {
                    actor->laufZustand.gesetztMs = navDenkbildAb(t);
                    actor->laufZustand.gesetztSpawn = false;
                }
                if (!actor->haveStart) {
                    for (int k = 0; k < 3; ++k) {
                        actor->start[k] = here[k];
                    }
                    actor->haveStart = true;
                }
                if (halt != Halt::Frueher && (actor->zielGesetzt || halt != Halt::Keiner)) {
                    // rc601 (bewegung, Wege): auch ohne Ziel - eine Figur, die
                    // noch ausrollt, rollt vom neuen Ort weiter (ps.velocity
                    // bleibt bei G_SetOrigin).
                    wegPlanen(t, neuOrt, fahrt, -1, true);
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
                // rc601 (bewegung, Wege): SetClientViewAngle + Q3_SetDYaw -
                // ab dem naechsten Denkbild mit der neuen Gier weiter.
                if (actor->laufPlan) {
                    float o[3];
                    bool fahrt = false;
                    int neu = -1;
                    if (wegAnhalten(t, o, fahrt, neu) != Halt::Frueher) {
                        gierAusPlan = false;
                        wegPlanen(t, o, fahrt, neu, true);
                    }
                }
                continue;
            }

            // rc601 (bewegung): "true" ohne Gross/klein (Q_stricmp,
            // Q3_Interface.cpp:9345 ff.), und ein laufender Weg geht sofort
            // in der neuen Gangart weiter (STEER::Activate liest die
            // Schalter in jedem Denkbild).
            if (const std::string* v = setValue(child, "SET_WALKING")) {
                actor->gehSchalter = sameName(*v, "true");
                neuPlanen(t);
                continue;
            }

            // SET_RUNNING ist das Gegenstueck - 304 Vorkommen in Ravens
            // Bestand, und ohne es liefe eine Figur, die ausdruecklich auf
            // Laufen gestellt wird, weiter im Gehtempo.
            if (const std::string* v = setValue(child, "SET_RUNNING")) {
                actor->laufSchalter = sameName(*v, "true");
                neuPlanen(t);
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
                // rc601 (bewegung): wie Q3_SetWatchTarget (Q3_Interface.cpp:
                // 3264-3296, MD = OpenJK):
                //
                //   nur ein NPC ("is not an NPC" sonst);
                //   "NULL"/"NONE" (Q_stricmp): Zeiger loeschen - desiredYaw
                //       bleibt, die Figur schaut weiter dorthin;
                //   dann G_Find( targetname ): findet es nichts, bleibt das
                //       ALTE Ziel (return vor der Zuweisung). Freigegebene
                //       Entities (waypoint_navgoal, ref_tag, info_null ...)
                //       findet es nie; "player" ist der Spieler;
                //   der eigene Name loescht erst und findet dann sich selbst:
                //       die Figur schaut auf sich - vectoangles eines
                //       Nullvektors ist Gier 0.
                //
                // Das Watchtarget dreht den Koerper nur in BS_CINEMATIC
                // (NPC_BSCinematic, NPC_behavior.cpp:304) - siehe Actor::at.
                if (!npc) {
                    continue;
                }
                ActorStep step;
                step.kind = ActorStep::Kind::Watch;
                step.startMs = t;
                step.endMs = t;
                step.path = p;
                if (sameName(*v, "NULL") || sameName(*v, "NONE")) {
                    step.value = 0.0F;
                } else if (sameName(*v, name)) {
                    step.value = 1.0F;
                    step.text = *v;
                    step.index = 1;   // auf sich selbst
                } else if (findeImSpiel(map, *v, step.to, nullptr)) {
                    step.value = 1.0F;
                    step.text = *v;
                } else {
                    continue;   // "can't find WatchTarget": das alte bleibt
                }
                actor->steps.push_back(std::move(step));
                // rc601 (bewegung, Wege): NPC_BSCinematic ueberschreibt
                // desiredYaw ab dem naechsten Denkbild (Rueckwaerts-Bremse).
                neuPlanen(t);
                continue;
            }

            // --- rc601 (bewegung): SET_BEHAVIOR_STATE ----------------------
            //
            // Gemeinsam mit dem Paket saber: ein Schritt Kind::Verhalten mit
            // dem BS_-Namen in Grossbuchstaben (GetIDForString vergleicht ohne
            // Gross/klein). Q3_SetBState (Q3_Interface.cpp:2608 ff.) wirkt nur
            // auf NPCs. Die Gangart haengt daran (siehe gehend) - ein
            // laufender Weg geht im naechsten Denkbild anders weiter.
            if (const std::string* v = setValue(child, "SET_BEHAVIOR_STATE")) {
                if (!npc) {
                    continue;
                }
                ActorStep step;
                step.kind = ActorStep::Kind::Verhalten;
                step.startMs = t;
                step.endMs = t;
                step.text = *v;
                for (char& z : step.text) {
                    z = static_cast<char>(std::toupper(static_cast<unsigned char>(z)));
                }
                step.path = p;
                // Schwerkraft: jedes SET_BEHAVIOR_STATE ausser BS_NOCLIP setzt
                // client->noclip = false (Q3_Interface.cpp:2670-2677, auch
                // bei einem unbekannten Namen) - ein CEILING-Jedi faellt.
                if (step.text != "BS_NOCLIP" && (actor->haengtBisMs < 0.0 || t < actor->haengtBisMs)) {
                    actor->haengtBisMs = t;
                }
                actor->steps.push_back(std::move(step));
                neuPlanen(t);
                continue;
            }

            // --- rc601 (bewegung): SET_DYAW und SET_VIEWTARGET ----------------
            //
            // Gemeldet: "NPCs don't rotate with their YAW_SPEED." Diese beiden
            // SIND die Befehle, mit denen ein Skript eine Figur mit ihrer
            // Drehrate dreht - behaved kannte sie nicht. Beide setzen nur
            // desiredYaw (Q3_SetDYaw, Q3_Interface.cpp:2832 ff.: "if
            // ( !ent->enemy )"), gedreht wird in NPC_UpdateAngles. Und beide
            // halten TID_ANGLE_FACE, bis der Winkel steht (Set,
            // Q3_Interface.cpp:9030-9043; NPC_utils.cpp:388) - dowait darauf
            // wartet die Drehung ab. Gemessen wird gegen das, was desiredYaw
            // im naechsten Denkbild wirklich ist: beim Gehen ueberschreibt es
            // STEER, mit Watchtarget in BS_CINEMATIC NPC_BSCinematic.
            {
                const std::string* dyaw = setValue(child, "SET_DYAW");
                const std::string* sicht = dyaw == nullptr ? setValue(child, "SET_VIEWTARGET") : nullptr;
                if (dyaw != nullptr || sicht != nullptr) {
                    if (!npc) {
                        continue;   // Q3_SetDYaw: "is not an NPC"
                    }
                    const ActorState j = actor->at(t);
                    ActorStep step;
                    step.kind = ActorStep::Kind::Wunschgier;
                    step.startMs = t;
                    step.path = p;
                    float zielNick = 0.0F;
                    bool gefunden = true;
                    if (dyaw != nullptr) {
                        step.value = static_cast<float>(std::atof(dyaw->c_str()));   // atof (Q3_Interface.cpp:9031)
                    } else {
                        // Q3_SetViewTarget (Q3_Interface.cpp:3204 ff.): vom
                        // Auge (origin + viewheight 36) zum Ziel - bei einer
                        // Figur deren Auge, sonst ihr Ursprung.
                        float ort[3];
                        bool zielFigur = false;
                        gefunden = findeImSpiel(map, *sicht, ort, &zielFigur);
                        if (gefunden) {
                            constexpr float kAuge = 36.0F;   // standheight 40 + STANDARD_VIEWHEIGHT_OFFSET -4
                            const float dx = ort[0] - j.pos[0];
                            const float dy = ort[1] - j.pos[1];
                            const float dz = (ort[2] + (zielFigur ? kAuge : 0.0F)) - (j.pos[2] + kAuge);
                            step.value = std::atan2(dy, dx) * 180.0F / std::numbers::pi_v<float>;
                            zielNick = -std::atan2(dz, std::hypot(dx, dy)) * 180.0F / std::numbers::pi_v<float>;
                            step.text = *sicht;
                        } else {
                            // "can't find ViewTarget": desiredYaw bleibt, die
                            // Aufgabe haelt trotzdem ihr Bild.
                            step.value = j.angles[1];
                            step.index = 1;
                        }
                    }
                    // rc601 (bewegung, Durchsicht 2): laeuft oder rollt die Figur
                    // noch (ein Plan), gilt das neue desiredYaw IM Plan - ab dem
                    // naechsten Denkbild ueberschreibt es STEER, solange das Ziel
                    // steht, danach dreht NPC_UpdateAngles dorthin, waehrend sie
                    // ausrollt. Vorher verwarf die Szene ein SET_DYAW, solange das
                    // letzte Wegstueck noch rollte - nach "dowait" auf den Lauf
                    // drehte sich die Figur dann gar nicht. TID_ANGLE_FACE endet
                    // im ersten Denkbild mit "exact" (planGenauAb), sonst vom
                    // letzten Zustand des Plans aus.
                    if (actor->laufPlan && (dyaw != nullptr || gefunden)) {
                        float o[3];
                        bool fahrt = false;
                        int neu = -1;
                        if (wegAnhalten(t, o, fahrt, neu) != Halt::Frueher) {
                            actor->laufZustand.wunschGier = step.value;
                            wegPlanen(t, o, fahrt, neu, true);
                            if (actor->laufPlan) {
                                const LaufPlan& np = *actor->laufPlan;
                                double ende = planGenauAb(np, t);
                                if (ende < 0.0 && !np.takte.empty()) {
                                    const LaufZustand& zl = np.zustaende.back();
                                    const double h = drehHalteMs(zl.gier, zl.wunschGier, drehRateBei(t));
                                    ende = h < 0.0 ? -1.0 : np.takte.back().t + h;
                                }
                                if (ende >= 0.0) {
                                    step.endMs = ende;
                                    actor->steps.push_back(std::move(step));
                                    continue;
                                }
                            }
                        }
                    }
                    // Das wirkliche desiredYaw im naechsten Denkbild.
                    float wirkt = step.value;
                    const bool kino = j.verhalten == "BS_CINEMATIC";
                    if (kino && j.hasWatchTarget) {
                        wirkt = (std::hypot(j.watchAt[0] - j.pos[0], j.watchAt[1] - j.pos[1]) > 0.5F)
                                    ? std::atan2(j.watchAt[1] - j.pos[1], j.watchAt[0] - j.pos[0]) * 180.0F /
                                          std::numbers::pi_v<float>
                                    : 0.0F;
                    } else if (j.moving && actor->wegIndex >= 0) {
                        const ActorStep& w = actor->steps[static_cast<std::size_t>(actor->wegIndex)];
                        wirkt = std::atan2(w.to[1] - w.from[1], w.to[0] - w.from[0]) * 180.0F / std::numbers::pi_v<float>;
                    }
                    // rc601 (bewegung, teil 3): die Gier Denkbild fuer Denkbild mit
                    // ANGLE2SHORT, der Nick wie bisher.
                    const float nickFehler = (sicht != nullptr && gefunden) ? std::fabs(winkel180(zielNick)) : 0.0F;
                    const double haltGier = drehHalteMs(j.angles[1], wirkt, j.turnDegPerSec);
                    const double haltNick = drehHalteMs(nickFehler, j.turnDegPerSec);
                    const double halt = (haltGier < 0.0 || haltNick < 0.0) ? -1.0 : std::max(haltGier, haltNick);
                    // Drehrate 0: TID_ANGLE_FACE wird nie fertig - die Szene
                    // kennt kein "nie" und haelt nicht; der Ablauf schon.
                    step.endMs = t + std::max(0.0, halt);
                    actor->steps.push_back(std::move(step));
                    continue;
                }
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
            // rc601 (bewegung): int_data = atoi( data ) (Q3_Interface.cpp:
            // 9070-9078) - "55.700" ist 55, Text ist 0. Und ein laufender Weg
            // geht ab sofort mit dem neuen Tempo weiter.
            // Ein Ausdruck ($get(...)$) ist hier nicht auszurechnen - dann
            // bleibt das Tempo, wie bisher.
            // rc601 (bewegung, BS_NOCLIP): "stats.walkSpeed = client->ps.speed
            // = int_data" - ps.speed gilt bis zum naechsten STEER::Activate;
            // in BS_NOCLIP (PM_CmdScale) ist es DAS Tempo.
            if (const std::string* v = setValue(child, "SET_WALKSPEED")) {
                if (child.args[1].kind != Arg::Kind::Expr) {
                    walkSpeed = static_cast<float>(std::atoi(v->c_str()));
                    psTempoSetzen(t, std::atoi(v->c_str()));
                }
                continue;
            }
            if (const std::string* v = setValue(child, "SET_RUNSPEED")) {
                if (child.args[1].kind != Arg::Kind::Expr) {
                    runSpeed = static_cast<float>(std::atoi(v->c_str()));
                    psTempoSetzen(t, std::atoi(v->c_str()));
                }
                continue;
            }
            // Q3_SetYawSpeed (Q3_Interface.cpp:3438) schreibt den Wert
            // ungeprueft in die Werte des NPC. Der Schritt wirkt erst zur
            // Auswertungszeit, denn gedreht wird dort - anders als beim
            // Gehen, dessen Dauer hier festgelegt wird.
            if (const std::string* v = setValue(child, "SET_YAWSPEED")) {
                if (!npc) {
                    continue;   // rc601 (bewegung): Q3_SetYawSpeed "is not an NPC"
                }
                ActorStep step;
                step.kind = ActorStep::Kind::TurnRate;
                step.startMs = t;
                step.endMs = t;
                step.path = p;
                // rc601 (bewegung, Review): float_data = atof( data )
                // (Q3_Interface.cpp:9085-9087 MD) - wie der Ablauf. std::stof
                // warf bei "fast" oder "", und dann galten 330 statt 60 Grad/s.
                step.value = turnRateFor(static_cast<float>(std::atof(v->c_str())));
                actor->steps.push_back(std::move(step));
                neuPlanen(t);   // rc601 (bewegung, Wege): NPC_UpdateAngles dreht ab jetzt so
                continue;
            }
            // --- rc601 (bewegung, Review): set ( SET_GRAVITY ) ----------------
            //
            // Q3_SetGravity (Q3_Interface.cpp:3788-3807, 9105-9107 MD): nur ein
            // client (NPC oder Spieler), float_data = atof( data ), ab dann
            // SVF_CUSTOM_GRAVITY. Gerechnet wird in schwerkraft.cpp.
            if (const std::string* v = setValue(child, "SET_GRAVITY")) {
                if ((npc || actor->echterSpieler) && child.args[1].kind != Arg::Kind::Expr) {
                    actor->eigeneSchwerkraft.emplace_back(t, static_cast<float>(std::atof(v->c_str())));
                }
                continue;
            }

            if (const std::string* v = setValue(child, "SET_NAVGOAL")) {
                // rc601 (bewegung): Q3_SetNavGoal (Q3_Interface.cpp:2106 ff.)
                //
                //   "null"/"NULL" (Q_stricmp): goalEntity = NULL, die Figur
                //       haelt an, TID_MOVE_NAV ist erledigt. Bis rc600
                //       uebergangen - sie lief weiter bis ans alte Ziel.
                //   unbekannt: "can't find NAVGOAL", return qfalse - das
                //       ALTE Ziel bleibt.
                //   sonst: neues Ziel, ab dem AKTUELLEN Ort (bis rc600 ab
                //       dem vorigen Ziel - ein sichtbarer Sprung, wenn das
                //       neue Ziel vor der Ankunft kam).
                //
                // Die Animation laeuft MIT dem Tempo (gemeldet: "statt sauber
                // ueber den Boden zu laufen slidet er dauerhaft ein
                // bisschen"): bg_panimate.cpp:4824 rechnet animSpeed *=
                // resultspeed / moveSpeedOfAnim, mit 50 beim Gehen und 150
                // beim Laufen (kAnimSpeedWalk, scene.h) - g_noFootSlide ist
                // standardmaessig an (g_main.cpp:640). Das macht wegPlanen.
                //
                // Der Ablauf der Figur laeuft WEITER, waehrend sie geht:
                // SET_NAVGOAL wartet nicht. Wer warten will, schreibt ein
                // wait dahinter - so steht es in den Skripten.
                if (!npc) {
                    continue;   // "tried to set a navgoal on a non-NPC" (Q3_Interface.cpp:2118)
                }
                const bool loeschen = sameName(*v, "NULL");
                NavZiel ziel;
                if (!loeschen) {
                    ziel = navZielAufloesen(map, *v);
                    if (!ziel.gefunden) {
                        continue;   // das alte Ziel bleibt
                    }
                }
                float jetzt[3];
                bool fahrt = false;
                int neu = -1;
                const Halt halt = wegAnhalten(t, jetzt, fahrt, neu);
                if (halt == Halt::Frueher) {
                    // Ein frueheres affect in einem rohen Skript: wie bis
                    // rc600 ein eigener Weg ab hier, der den spaeteren nicht
                    // anruehrt.
                    if (!loeschen) {
                        const bool merkZiel = actor->zielGesetzt;
                        float merkOrt[3] = {actor->zielOrt[0], actor->zielOrt[1], actor->zielOrt[2]};
                        const double merkRadius = actor->zielRadius;
                        const Path merkPfad = actor->zielPfad;
                        actor->zielGesetzt = true;
                        for (int k = 0; k < 3; ++k) { actor->zielOrt[k] = ziel.ort[k]; }
                        actor->zielRadius = ziel.radius;
                        actor->zielPfad = p;
                        wegPlanen(t, jetzt, false, -1, false);
                        actor->zielGesetzt = merkZiel;
                        for (int k = 0; k < 3; ++k) { actor->zielOrt[k] = merkOrt[k]; }
                        actor->zielRadius = merkRadius;
                        actor->zielPfad = merkPfad;
                    }
                    continue;
                }
                if (loeschen) {
                    actor->zielGesetzt = false;
                    wegPlanen(t, jetzt, fahrt, neu, true);   // verwirft einen Weg aus demselben Bild
                    continue;
                }
                actor->zielGesetzt = true;
                for (int k = 0; k < 3; ++k) { actor->zielOrt[k] = ziel.ort[k]; }
                actor->zielRadius = ziel.radius;
                actor->zielPfad = p;
                // rc601 (bewegung, Wege): NPC_SetMoveGoal - tempGoal->waypoint
                // = WAYPOINT_NONE, noWaypointTime = 0, lastWaypoint = NONE
                // (g_nav.cpp:43-91, Q3_Interface.cpp:2163).
                actor->laufZustand.neuesZiel();
                wegPlanen(t, jetzt, fahrt, neu, true);
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

            // --- rc601 (saber): SET_SABERCOLLISIONS ---------------------------
            //
            // SET_BEHAVIOR_STATE las das Paket saber hier ein zweites Mal; rc601
            // integ: es gilt allein die Fassung des Pakets bewegung weiter oben
            // (mit noclip/Gierereignissen) - beide erzeugten denselben Schritt.
            // SET_SABERCOLLISIONS (nur Movie Duels): "true" ohne Gross/klein
            // schaltet ein, alles andere aus; jede Entity mit client
            // (Q3_Interface.cpp:10307-10325 MD). Ob es wirkt, entscheidet
            // erst das Regelwerk (saberMovieDuelsRegeln).
            if (const std::string* v = setValue(child, "SET_SABERCOLLISIONS")) {
                if (actor->brushModel > 0) {
                    continue;
                }
                ActorStep step;
                step.kind = ActorStep::Kind::SaberKollision;
                step.startMs = t;
                step.endMs = t;
                step.value = sameName(*v, "true") ? 1.0F : 0.0F;
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
                const float rateVorher = drehRateBei(t);
                actor->steps.push_back(std::move(step));
                // rc601 (bewegung, Durchsicht 2): die Drehrate haengt an der
                // Waffe (npcDrehrate) - ein laufender Weg dreht ab jetzt so.
                // Nur bei einer Aenderung: sonst bliebe der Plan derselbe.
                if (npc && drehRateBei(t) != rateVorher) { neuPlanen(t); }
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

    // --- rc602: die Figuren, die beim Laden der Karte spawnen -------------
    //
    // SP_NPC_spawner: ohne targetname denkt der Spawner bei
    // START_TIME_REMOVE_ENTS + 50 und spawnt (NPC_spawn.cpp:2037 ff. OpenJK,
    // :3087 ff. MD) - die Figur steht da, ob ein Skript sie nennt oder nicht.
    // Gemeldet: "der NPC droid fehlt" (md_am_sith, NPC_Droid_R2D2 ohne Namen
    // neben ani1). Eine Figur ohne NPC_targetname hat im Spiel keinen Namen
    // (NPC_Spawn_Do gibt nur NPC_targetname weiter); hier heisst sie
    // "<classname>#<index>", damit die Vorschau sie fuehren kann. Vor den
    // Entfernungen, damit remove/kill auch eine benannte Kartenfigur trifft.
    if (hilfe != nullptr && hilfe->kartenFiguren) {
        for (std::size_t ei = 0; ei < map.entities.size(); ++ei) {
            const MapEntity& e = map.entities[ei];
            if (!npcSpawntBeimKartenstart(e)) {
                continue;
            }
            const std::string* nt = e.find("NPC_targetname");
            const std::string name = nt != nullptr ? *nt : std::string();
            bool schon = false;
            for (const Actor& a : out.actors) {
                schon = schon || a.kartenEntity == static_cast<int>(ei) || (!name.empty() && sameName(a.name, name));
            }
            if (schon) {
                continue;
            }
            figurAnlegen(name.empty() ? e.classname + "#" + std::to_string(ei) : name, &e);
            out.actors.back().kartenEntity = static_cast<int>(ei);
            out.actors.back().kartenFigur = true;
        }
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
            //
            // rc601 (bewegung): und zwar wirklich - das WalkTo wird beim Tod
            // abgeschnitten (vorher nur ein Sprung obendrauf, und die
            // Laufanimation lief bis zum alten Wegende weiter).
            // rc601 (bewegung, Wege): ein Weg kann aus mehreren Stuecken
            // bestehen - spaetere fallen weg, das laufende endet am Ort der
            // Bahn zu dieser Zeit.
            a.steps.erase(std::remove_if(a.steps.begin(), a.steps.end(),
                                         [wann](const ActorStep& w) {
                                             return w.kind == ActorStep::Kind::WalkTo && w.startMs >= wann;
                                         }),
                          a.steps.end());
            for (ActorStep& w : a.steps) {
                if (w.kind != ActorStep::Kind::WalkTo || !(w.startMs < wann && w.endMs > wann)) {
                    continue;
                }
                wegOrtBei(w, wann, w.to);
                w.endMs = wann;
                w.teilweg = true;
                if (w.ankunftMs > wann) { w.ankunftMs = -1.0; }
            }
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
                                              "BOTH_CROUCH1IDLE", "BOTH_WALKBACK1", "BOTH_WALKBACK2",
                                              "BOTH_RUNBACK1", "BOTH_RUNBACK2"};
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
    // --- Blickziel ist eine andere Figur (rc597) -------------------------
    //
    // Vader in md_dd/cin1_jedi schaut auf obi2, waehrend der um ihn
    // herumgeht, und laeuft dabei rueckwaerts ("vader_walkback"). Die Engine
    // nimmt den AKTUELLEN Ort des Ziels (NPC_BSCinematic); hier stand bisher
    // sein Spawner. Die Bahn des Ziels wird einmal abgetastet - sein Ort
    // haengt nicht von seinem eigenen Blick ab, die Reihenfolge ist also egal.
    {
        double ende = sc.durationMs;
        for (const Actor& a : sc.actors) {
            for (const ActorStep& st : a.steps) { ende = std::max(ende, st.endMs); }
        }
        ende = std::min(ende + 1000.0, 600000.0);
        for (Actor& a : sc.actors) {
            for (ActorStep& st : a.steps) {
                if (st.kind != ActorStep::Kind::Watch || st.value < 0.5F || st.text.empty()) { continue; }
                const Actor* ziel = nullptr;
                for (const Actor& b : sc.actors) {
                    if (&b != &a && b.haveStart && b.name == st.text) { ziel = &b; }
                }
                if (ziel == nullptr) { continue; }
                bool bewegt = false;
                for (const ActorStep& zs : ziel->steps) {
                    bewegt = bewegt || zs.kind == ActorStep::Kind::WalkTo || zs.kind == ActorStep::Kind::Glide ||
                             zs.kind == ActorStep::Kind::Teleport;
                }
                if (!bewegt) { continue; }
                for (double t = st.startMs; t <= ende; t += st.spurSchrittMs) {
                    const ActorState zs = ziel->at(t);
                    st.spur.push_back({zs.pos[0], zs.pos[1], zs.pos[2]});
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

std::vector<RoffEffektStart> roffEffektStarts(const Scene& s) {
    std::vector<RoffEffektStart> aus;
    for (const RoffEreignis& e : s.roffEreignisse) {
        if (e.befehl.art != RoffNotizBefehl::Art::Effekt || e.befehl.datei.empty()) {
            continue;
        }
        RoffEffektStart r;
        r.name = e.befehl.datei;
        std::replace(r.name.begin(), r.name.end(), '\\', '/');
        if (r.name.size() > 4) {
            std::string ende = r.name.substr(r.name.size() - 4);
            for (char& c : ende) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            if (ende == ".efx") { r.name.resize(r.name.size() - 4); }
        }
        r.pfad = "effects/" + r.name + ".efx";
        r.ms = e.ms;
        // AngleVectors: vorn aus Nicken und Gieren.
        const double nick = e.winkel[0] * std::numbers::pi / 180.0;
        const double gier = e.winkel[1] * std::numbers::pi / 180.0;
        r.richtung[0] = static_cast<float>(std::cos(nick) * std::cos(gier));
        r.richtung[1] = static_cast<float>(std::cos(nick) * std::sin(gier));
        r.richtung[2] = static_cast<float>(-std::sin(nick));
        for (int k = 0; k < 3; ++k) { r.ort[k] = e.ort[k]; }
        aus.push_back(std::move(r));
    }
    return aus;
}

// =====================================================================
// Paket bewegung (rc601): Laufziel, Fahrt, Tempo fuer den Ablauf
// =====================================================================
NavZiel navZielAufloesen(const MapData& map, const std::string& name) {
    NavZiel z;
    if (name.empty()) {
        return z;   // weder TAG noch G_Find (leerer Name) - das alte Ziel bleibt
    }
    // 1. TAG_GetOrigin2( NULL, name ): ref_tag und waypoint_navgoal, ohne
    //    Gross/klein (TAG_Find, g_ref.cpp:161 Q_strlwr), der ERSTE gewinnt
    //    (TAG_Add verwirft einen doppelten Namen, g_ref.cpp:205). Ein ref_tag
    //    mit "ownername" gehoert einem anderen Besitzer - TAG_Find( NULL )
    //    sucht nur beim allgemeinen.
    for (const MapEntity& e : map.entities) {
        const bool navgoal = e.classname.rfind("waypoint_navgoal", 0) == 0;
        const bool reftag = e.classname == "ref_tag";
        if ((!navgoal && !reftag) || e.origin.empty() || e.targetname.empty() || !sameName(e.targetname, name)) {
            continue;
        }
        if (reftag) {
            const std::string* besitzer = e.find("ownername");
            if (besitzer != nullptr && !besitzer->empty()) {
                continue;
            }
        }
        readVec(e.origin, z.ort);
        if (navgoal) {
            // SP_waypoint_navgoal (g_nav.cpp:260-277): origin[2] += 0.125,
            // und "int radius = ent->radius ? ent->radius : 12" - der
            // Schluessel ist ein F_FLOAT (g_spawn.cpp:368), abgeschnitten.
            z.ort[2] += 0.125F;
            float r = 0.0F;
            if (const std::string* rs = e.find("radius")) {
                r = std::strtof(rs->c_str(), nullptr);
            }
            z.radius = (r != 0.0F) ? static_cast<double>(static_cast<int>(r)) : 12.0;
        } else {
            z.radius = 16.0;   // SP_reference_tag: TAG_Add( ..., 16, 0 ) (g_ref.cpp:396)
        }
        z.gefunden = true;
        z.marke = true;
        return z;
    }
    // 2. G_Find( NULL, FOFS(targetname), name ): goalEntity ist dann diese
    //    Entity, die Aufgabe ist SOFORT fertig (Q3_SetNavGoal gibt qfalse),
    //    und der Radius ist sqrt(ent->maxs[0]*2) + sqrt(targ->maxs[0]*2)
    //    (Q3_Interface.cpp:2155). maxs[0] eines NPC ist 16 (playerMaxs,
    //    g_client.cpp:94), eine Punkt-Entity hat 0.
    bool figurZiel = false;
    if (findeImSpiel(map, name, z.ort, &figurZiel)) {
        z.gefunden = true;
        z.marke = false;
        z.radius = std::sqrt(16.0 + 16.0) + (figurZiel ? std::sqrt(16.0 + 16.0) : 0.0);
    }
    return z;
}

bool engineFindet(const MapData& map, const std::string& name, float out[3], bool* istFigur) {
    return findeImSpiel(map, name, out, istFigur);
}

double gehDauerInFahrtMs(double dist, double v, double ux, double uy, double zielRadius) {
    // gehDauerMs ohne seinen Anlauf von 300 ms (siehe dort). Dieselbe
    // Rechnung in scene.cpp und ablauf.cpp, damit Weg und dowait zusammen
    // enden.
    const double mit = gehDauerMs(dist, v, ux, uy, zielRadius);
    return (mit > 300.0) ? mit - 300.0 : 0.0;
}

float winkel180(float a) noexcept {
    // AngleNormalize180 (q_math.c): (-180, 180].
    a = std::fmod(a, 360.0F);
    if (a <= -180.0F) {
        a += 360.0F;
    } else if (a > 180.0F) {
        a -= 360.0F;
    }
    return a;
}

// rc601 (bewegung, teil 3): ein Denkbild NPC_UpdateAngles mit der
// Quantisierung der Engine (siehe bhed/scene.h).
float npcGierSchritt(float yaw, float ziel, float rate) noexcept {
    float fehler = winkel180(yaw - ziel);   // AngleDelta(viewangles[YAW], targetYaw)
    if (std::fabs(fehler) > kMinAngleError && fehler != 0.0F) {
        const float decay = rate * (static_cast<float>(kNpcDenktaktMs) / 1000.0F);
        if (fehler < 0.0F) {
            fehler += decay;
            if (fehler > 0.0F) { fehler = 0.0F; }
        } else {
            fehler -= decay;
            if (fehler < 0.0F) { fehler = 0.0F; }
        }
    }
    return navGierQuant(ziel + fehler);
}

float GierLauf::bei(double t) const noexcept {
    // NPC_UpdateAngles (NPC_utils.cpp:340-367, MD): error =
    // AngleDelta( viewangles[YAW], targetYaw ); ist |error| groesser als
    // MIN_ANGLE_ERROR, geht er um decay = rate * 50/1000 je Denkbild gegen 0,
    // ohne Ueberschiessen; viewangles[YAW] = targetYaw + error.
    //
    // rc601 (bewegung, teil 3): Denkbild fuer Denkbild mit ANGLE2SHORT/
    // SHORT2ANGLE (npcGierSchritt) - bis teil 3 geschlossen (rate * dt) und
    // ohne die Quantisierung; zwischen zwei Denkbildern linear (wie cgame).
    if (!(t > t0) || !(rate > 0.0F)) {
        return yaw0;
    }
    float y = yaw0;
    double tf = t0;
    for (int n = 0; n < 4000 && tf + kNpcDenktaktMs <= t + 1e-6; ++n) {
        const float yn = npcGierSchritt(y, ziel, rate);
        tf += kNpcDenktaktMs;
        if (yn == y) {
            return y;   // steht still: so bleibt es
        }
        y = yn;
    }
    if (t > tf + 1e-6) {
        const float yn = npcGierSchritt(y, ziel, rate);
        const auto f = static_cast<float>((t - tf) / kNpcDenktaktMs);
        return y + winkel180(yn - y) * f;
    }
    return y;
}

double drehHalteMs(float yaw, float ziel, float rate) noexcept {
    // rc601 (bewegung, teil 3): wie unten, aber Denkbild fuer Denkbild mit der
    // Quantisierung - "exact" im ersten Denkbild, in dem |AngleDelta| vor dem
    // Drehen hoechstens MIN_ANGLE_ERROR ist.
    if (!(rate > 0.0F)) {
        return std::fabs(winkel180(yaw - ziel)) <= kMinAngleError ? kNpcDenktaktMs : -1.0;
    }
    float y = yaw;
    for (int n = 1; n < 4000; ++n) {
        if (std::fabs(winkel180(y - ziel)) <= kMinAngleError) {
            return kNpcDenktaktMs * n;
        }
        y = npcGierSchritt(y, ziel, rate);
    }
    return -1.0;
}

double drehHalteMs(float fehler, float rate) noexcept {
    // TID_ANGLE_FACE: NPC_UpdateAngles meldet "exact" erst im Denkbild, in
    // dem der Fehler schon vorher null war (NPC_utils.cpp:388-391) - also
    // ein Bild nach dem letzten Schritt: 50 * (ceil(fehler/decay) + 1).
    const float f = std::fabs(fehler);
    if (f <= kMinAngleError) {
        return kNpcDenktaktMs;
    }
    if (!(rate > 0.0F)) {
        return -1.0;   // decay 0: nie fertig
    }
    const double decay = static_cast<double>(rate) * kNpcDenktaktMs / 1000.0;
    const double schritte = std::ceil(static_cast<double>(f) / decay - 1e-4);
    return kNpcDenktaktMs * (schritte + 1.0);
}

// --- Paket bewegung (rc601, Durchsicht 2): die Schwierigkeit ---------------
// Warum und woher: bhed/scene.h, npcWerteBeimSpawnen.
void npcWerteBeimSpawnen(const NpcDef& def, const std::string& npcTyp, int schwierigkeit, float& yawSpeed,
                         float& gehTempo, float& laufTempo) {
    const auto klasse = [&def](std::initializer_list<const char*> namen) {
        for (const char* n : namen) {
            if (sameName(def.klasse, n)) { return true; }
        }
        return false;
    };
    // NPC_Begin (NPC_spawn.cpp:1810-1877 MD): eine else-if-Kette.
    // rc601 (bewegung, teil 3): OpenJK (NPC_spawn.cpp:1087-1130) kennt nur
    // Stormtrooper/Swamptrooper/Impworker/rodian2 und Reborn/Shadowtrooper -
    // Guard, Clonetrooper, SBD, Battledroid, BoC und Galen sind Movie Duels.
    const bool md = navMovieDuelsRegeln();
    if (sameName(npcTyp, "rodian")) {
        // nur stats.aim
    } else if ((md ? klasse({"CLASS_STORMTROOPER", "CLASS_GUARD", "CLASS_CLONETROOPER", "CLASS_SBD", "CLASS_BATTLEDROID",
                             "CLASS_SWAMPTROOPER", "CLASS_IMPWORKER"})
                   : klasse({"CLASS_STORMTROOPER", "CLASS_SWAMPTROOPER", "CLASS_IMPWORKER"})) ||
               sameName(npcTyp, "rodian2")) {
        switch (schwierigkeit) {
        case 0: yawSpeed *= 0.75F; break;
        case 2: yawSpeed *= 1.5F; break;
        default: break;
        }
    } else if (md ? klasse({"CLASS_REBORN", "CLASS_BOC", "CLASS_SHADOWTROOPER", "CLASS_GALEN"})
                  : klasse({"CLASS_REBORN", "CLASS_SHADOWTROOPER"})) {
        switch (schwierigkeit) {
        case 1: yawSpeed *= 1.25F; break;
        case 2: yawSpeed *= 1.5F; break;
        default: break;
        }
    }
    // NPC_SetMiscDefaultData (NPC_spawn.cpp:2018 MD, nach der Kette oben).
    if (klasse({"CLASS_SAND_CREATURE"}) && schwierigkeit > 1) {
        laufTempo = static_cast<float>(static_cast<int>(static_cast<float>(static_cast<int>(laufTempo)) * 1.5F));
    }
    if (klasse({"CLASS_TUSKEN"}) && schwierigkeit > 1) {
        // "on hard, tusken raiders are faster than you"
        laufTempo = 280.0F;
        gehTempo = 65.0F;
    }
}

// rc601 (bewegung, Durchsicht 2): siehe bhed/scene.h.
float npcDrehrate(const Actor& a, const std::string& waffe, float yawRate) {
    // NPC_UpdateAngles (NPC_utils.cpp:274-290 MD): "if (NPC->s.weapon ==
    // WP_EMPLACED_GUN) yawSpeed = 20; else if (CLASS_ROCKETTROOPER &&
    // !NPC->enemy) yawSpeed = 1; else yawSpeed = NPCInfo->stats.yawSpeed;"
    std::string w = waffe.empty() ? a.startWaffe : waffe;
    if (w == "drop") { w = "WP_NONE"; }
    if (sameName(w, "WP_EMPLACED_GUN")) { return turnRateFor(20.0F); }
    if (a.raketenTruppler) { return turnRateFor(1.0F); }
    return yawRate;
}

std::function<float(const std::string&, const std::string&, float)> ablaufDrehrate(const Scene& s) {
    auto figuren = std::make_shared<std::map<std::string, Actor>>();
    for (const Actor& a : s.actors) {
        Actor kopf;
        kopf.startWaffe = a.startWaffe;
        kopf.raketenTruppler = a.raketenTruppler;
        kopf.festeDrehrate = a.festeDrehrate;
        figuren->emplace(a.name, std::move(kopf));
    }
    return [figuren](const std::string& figur, const std::string& waffe, float yawRate) -> float {
        const auto it = figuren->find(figur);
        return it == figuren->end() ? yawRate : npcDrehrate(it->second, waffe, yawRate);
    };
}

std::function<float(const std::string&)> ablaufDrehrateYaw(const Scene& s) {
    auto raten = std::make_shared<std::map<std::string, float>>();
    for (const Actor& a : s.actors) { raten->emplace(a.name, a.startDrehrate); }
    return [raten](const std::string& figur) -> float {
        const auto it = raten->find(figur);
        return it == raten->end() ? -1.0F : it->second;
    };
}

// rc601 integ: siehe bhed/scene.h.
std::function<int(const std::string&, const std::string&)> ablaufVerhaltenTakt(const Scene& s, int spielstufe) {
    auto figuren = std::make_shared<std::map<std::string, std::pair<std::string, std::string>>>();
    for (const Actor& a : s.actors) { figuren->emplace(a.name, std::make_pair(a.startWaffe, a.rang)); }
    return [figuren, spielstufe](const std::string& figur, const std::string& waffe) -> int {
        const auto it = figuren->find(figur);
        std::string w = waffe;
        std::string rang;
        if (it != figuren->end()) {
            if (w.empty()) { w = it->second.first; }
            rang = it->second.second;
        }
        if (w == "drop") { w = "WP_NONE"; }
        return npcVerhaltenTaktMs(navMovieDuelsRegeln(), w, spielstufe, rang);
    };
}

// rc601 (bewegung, teil 3): siehe bhed/scene.h.
std::function<int(const std::string&)> ablaufVorlauf(const Scene& s) {
    auto arten = std::make_shared<std::map<std::string, int>>();
    for (const Actor& a : s.actors) {
        int art = 1;
        if (a.brushModel > 0 || a.ohneSchwerkraft) {
            art = -2;
        } else if (a.echterSpieler || a.haengtAnfangs || a.npcType.empty()) {
            art = -1;
        } else if ((a.spawnflags & 128) != 0) {
            art = 0;   // SFB_STARTINSOLID: kein G_CheckInSolid
        }
        (*arten)[a.name] = art;
    }
    return [arten](const std::string& f) {
        const auto it = arten->find(f);
        return it == arten->end() ? -1 : it->second;
    };
}

std::function<float(const std::string&, bool)> ablaufTempo(const Scene& s) {
    // Bis rc600 stand diese Rechnung in gui/app_view3d.cpp und gab dem
    // Ablauf gehTempo/laufTempo der fertigen Szene - also das LETZTE
    // SET_WALKSPEED/SET_RUNSPEED im Skript, oft 0 (md_ga_jedi: 25 von 34).
    // Der Ablauf fiel dann auf 90/300 zurueck, die Szene ging mit den
    // .npc-Werten (meist 55/200): Weg und dowait liefen auseinander.
    std::map<std::string, std::pair<float, float>> tempi;
    for (const Actor& a : s.actors) {
        tempi.emplace(a.name, std::make_pair(a.spawnGehTempo, a.spawnLaufTempo));
    }
    return [tempi](const std::string& figur, bool gehen) -> float {
        const auto it = tempi.find(figur);
        if (it == tempi.end()) {
            return -1.0F;
        }
        return gehen ? it->second.first : it->second.second;
    };
}

}  // namespace bhed
