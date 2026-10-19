// kartenpruefung.cpp - siehe bhed/kartenpruefung.h (rc602).
#include "bhed/kartenpruefung.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace bhed::kartenpruefung {

namespace {

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

std::string ohneEndung(std::string s) {
    const std::size_t punkt = s.find_last_of('.');
    const std::size_t strich = s.find_last_of('/');
    if (punkt != std::string::npos && (strich == std::string::npos || punkt > strich)) { s.resize(punkt); }
    return s;
}

// Woerter einer Zeile; Klammern und Kommas (rgbGen const ( 1 1 1 )) trennen.
std::vector<std::string> woerter(const std::string& z) {
    std::vector<std::string> w;
    std::string cur;
    for (char c : z) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0 || c == '(' || c == ')' || c == ',') {
            if (!cur.empty()) { w.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) { w.push_back(cur); }
    return w;
}

// Anweisungen, deren Art mitzaehlt (tr_shader.cpp ParseStage/ParseShader).
bool mitArt(const std::string& k) {
    return k == "rgbgen" || k == "alphagen" || k == "tcgen" || k == "tcmod" || k == "deformvertexes" ||
           k == "sort" || k == "surfacesprites";
}

}  // namespace

void sammleShader(const std::string& text, std::map<std::string, ShaderAnweisungen>& out) {
    // Kommentare (// und /* */) weg; { und } auf eigene Zeilen - danach Zeile
    // fuer Zeile wie COM_ParseExt (ein Befehl je Zeile).
    std::string t;
    t.reserve(text.size() + text.size() / 8U);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') { ++i; }
            t += '\n';
            continue;
        }
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            const std::size_t ende = text.find("*/", i + 2);
            i = (ende == std::string::npos) ? text.size() : ende + 1;
            t += ' ';
            continue;
        }
        if (text[i] == '{' || text[i] == '}') {
            t += '\n';
            t += text[i];
            t += '\n';
            continue;
        }
        t += text[i];
    }
    std::string name;
    int tiefe = 0;
    ShaderAnweisungen cur;
    std::size_t at = 0;
    while (at < t.size()) {
        std::size_t eol = t.find('\n', at);
        if (eol == std::string::npos) { eol = t.size(); }
        const std::vector<std::string> w = woerter(t.substr(at, eol - at));
        at = eol + 1;
        if (w.empty()) { continue; }
        if (w[0] == "{") {
            ++tiefe;
            if (tiefe == 2) {
                cur.stufen.emplace_back();
                cur.stufenBild.emplace_back();
            }
            continue;
        }
        if (w[0] == "}") {
            --tiefe;
            if (tiefe <= 0) {
                if (!name.empty()) { out[name] = std::move(cur); }
                cur = ShaderAnweisungen{};
                name.clear();
                tiefe = 0;
            }
            continue;
        }
        if (tiefe == 0) {
            name = klein(ohneEndung(w[0]));
            continue;
        }
        std::string k = klein(w[0]);
        if (k == "texgen") { k = "tcgen"; }
        if (k == "deform") { k = "deformvertexes"; }
        std::string schluessel = k;
        if (mitArt(k) && w.size() > 1) {
            std::string art = klein(w[1]);
            // deformVertexes text0..text7 - eine Art.
            if (k == "deformvertexes" && art.rfind("text", 0) == 0) { art = "text"; }
            schluessel += " " + art;
        }
        if (tiefe == 1) {
            cur.oben.push_back(schluessel);
        } else if (!cur.stufen.empty()) {
            cur.stufen.back().push_back(schluessel);
            const bool anim = k == "animmap" || k == "clampanimmap" || k == "oneshotanimmap";
            if ((k == "map" || k == "clampmap" || k == "videomap" || anim) && cur.stufenBild.back().empty()) {
                // animMap <frequenz> <bild1> ...: das erste Bild.
                const std::size_t nr = anim ? 2U : 1U;
                if (w.size() > nr) { cur.stufenBild.back() = klein(ohneEndung(w[nr])); }
            }
        }
    }
}

namespace {

// Was das Bild nicht veraendert (oder nur q3map2 / den Editor betrifft) -
// zaehlt nicht als Luecke. tr_shader.cpp ParseShader: q3map*, qer*,
// surfaceParm, nomipmaps, nopicmip, noTC, entityMergable, tesssize, clampTime,
// light, sun (Schattenrichtung), material, hitLocation, hitMaterial,
// lightColor, surfacelight, noglfog. cull, polygonOffset, skyParms, fogParms,
// portal und sort bildet behaved nach (shaderscript.cpp) - auch keine Luecke.
bool ohneLuecke(const std::string& k) {
    static const std::set<std::string> kOhne = {
        "surfaceparm", "nomipmaps", "nopicmip", "notc", "entitymergable", "tesssize", "clamptime", "light",
        "sun", "material", "hitlocation", "hitmaterial", "lightcolor", "surfacelight", "noglfog", "cull",
        "polygonoffset", "skyparms", "fogparms", "portal",
    };
    if (k.rfind("q3map", 0) == 0 || k.rfind("qer", 0) == 0 || k.rfind("sort", 0) == 0) { return true; }
    return kOhne.count(k) != 0U;
}

// Der Weltzeichner (shaderscript.cpp, gpumap_win32.cpp): was er nachbildet.
bool weltKennt(const std::string& k) {
    static const std::set<std::string> kJa = {
        "map", "clampmap", "animmap", "clampanimmap", "oneshotanimmap", "alphafunc", "depthfunc", "blendfunc",
        "depthwrite", "glow", "detail",
        "rgbgen wave", "rgbgen const", "rgbgen identity", "rgbgen identitylighting", "rgbgen vertex",
        "rgbgen exactvertex", "rgbgen lightingdiffuse", "rgbgen lightingdiffuseentity", "rgbgen oneminusvertex",
        // Die Entity-Farbe der Welt und der Brush-Modelle ist weiss
        // (shaderRGBA 255) - "entity" wirkt dort wie identity.
        "rgbgen entity",
        "alphagen wave", "alphagen const", "alphagen identity", "alphagen entity", "alphagen vertex",
        "alphagen lightingspecular", "alphagen oneminusvertex", "alphagen portal",
        "tcgen environment", "tcgen texture", "tcgen base", "tcgen lightmap",
        "tcmod turb", "tcmod scale", "tcmod scroll", "tcmod stretch", "tcmod transform", "tcmod rotate",
        "deformvertexes autosprite", "deformvertexes autosprite2", "deformvertexes bulge",
        "deformvertexes wave", "deformvertexes normal", "deformvertexes move",
    };
    return kJa.count(k) != 0U;
}

// Kennt das Spiel die Anweisung? (Unbekanntes uebergeht es mit einer Warnung.)
bool spielKennt(const std::string& k) {
    static const std::set<std::string> kSpiel = {
        "map", "clampmap", "animmap", "clampanimmap", "oneshotanimmap", "videomap", "alphafunc", "depthfunc",
        "detail", "blendfunc", "depthwrite", "glow",
        "rgbgen wave", "rgbgen const", "rgbgen identity", "rgbgen identitylighting", "rgbgen entity",
        "rgbgen oneminusentity", "rgbgen vertex", "rgbgen exactvertex", "rgbgen lightingdiffuse",
        "rgbgen lightingdiffuseentity", "rgbgen oneminusvertex",
        "alphagen wave", "alphagen const", "alphagen identity", "alphagen entity", "alphagen oneminusentity",
        "alphagen vertex", "alphagen lightingspecular", "alphagen oneminusvertex", "alphagen dot",
        "alphagen oneminusdot", "alphagen portal",
        "tcgen environment", "tcgen lightmap", "tcgen texture", "tcgen base", "tcgen vector",
        "tcmod turb", "tcmod scale", "tcmod scroll", "tcmod stretch", "tcmod transform", "tcmod rotate",
        "tcmod entitytranslate",
        "deformvertexes projectionshadow", "deformvertexes text", "deformvertexes autosprite",
        "deformvertexes autosprite2",
        "deformvertexes bulge", "deformvertexes wave", "deformvertexes normal", "deformvertexes move",
    };
    if (k.rfind("surfacesprites", 0) == 0) { return true; }
    return kSpiel.count(k) != 0U;
}

void dazu(std::vector<std::string>& v, const std::string& s) {
    if (std::find(v.begin(), v.end(), s) == v.end()) { v.push_back(s); }
}

bool istGlowStufe(const std::vector<std::string>& st) {
    return std::find(st.begin(), st.end(), "glow") != st.end();
}

}  // namespace

std::vector<std::string> fehlendeMerkmale(const ShaderAnweisungen& s, Zusammenhang z, const std::string& hautName) {
    std::vector<std::string> fehlt;
    // ss* (ssFademax, ssVariance, ...) gehoeren zu surfaceSprites und zaehlen
    // ueber "surfacesprites <art>" mit.
    auto pruefeWelt = [&](const std::string& k) {
        if (k.rfind("ss", 0) == 0 && k.rfind("surfacesprites", 0) != 0) { return; }
        if (ohneLuecke(k) || !spielKennt(k) || weltKennt(k)) { return; }
        dazu(fehlt, k);
    };
    if (z == Zusammenhang::Welt) {
        for (const std::string& k : s.oben) { pruefeWelt(k); }
        for (const auto& st : s.stufen) {
            for (const std::string& k : st) { pruefeWelt(k); }
        }
        return fehlt;
    }
    if (z == Zusammenhang::Effekt) {
        // Effektteilchen (efxdraw.cpp): EIN Bild je Teilchen mit der Mischart
        // der Bildstufe; seit rc602 geht eine glow-Stufe ins Gluehen
        // (zeichneGluehen, Durchgang 1c). Weitere Stufen zeichnen sie nicht.
        int stufen = 0;
        for (const auto& st : s.stufen) {
            if (!st.empty()) { ++stufen; }
        }
        if (stufen > 1) { dazu(fehlt, "effekt: weitere stufe"); }
        return fehlt;
    }
    // Figur (zeichneFigurIn): seit rc602 ALLE Stufen des Skripts (Skript vor
    // Bild, rgbGen/alphaGen nach ComputeColors, tcGen environment, animMap,
    // glow) - bhed/figurstufen.h. Es fehlen tcMod, deformVertexes, videoMap
    // und die seltenen alphaGen dot/oneMinusDot/portal/entity.
    (void)hautName;
    for (const std::string& k : s.oben) {
        if (k.rfind("deformvertexes", 0) == 0) { dazu(fehlt, "figur: " + k); }
    }
    for (const auto& st : s.stufen) {
        for (const std::string& k : st) {
            if (k.rfind("tcmod", 0) == 0 || k == "videomap" || k == "alphagen dot" || k == "alphagen oneminusdot" ||
                k == "alphagen portal" || k == "alphagen entity" || k == "alphagen oneminusentity") {
                dazu(fehlt, "figur: " + k);
            }
        }
    }
    return fehlt;
}

// --- Spawntabellen (g_spawn.cpp spawns[], wortgleich) ----------------------
namespace {
// In beiden Regelwerken.
const char* const kBeide[] = {
    "NPC_Alora", "NPC_Bartender", "NPC_BespinCop", "NPC_BobaFett", "NPC_Chewbacca",
    "NPC_Colombian_EmplacedGunner", "NPC_Colombian_Rebel", "NPC_Colombian_Soldier", "NPC_Cultist",
    "NPC_Cultist_Commando", "NPC_Cultist_Destroyer", "NPC_Cultist_Saber",
    "NPC_Cultist_Saber_Powers", "NPC_Desann", "NPC_Droid_ATST", "NPC_Droid_Assassin",
    "NPC_Droid_Gonk", "NPC_Droid_Interrogator", "NPC_Droid_Mark1", "NPC_Droid_Mark2",
    "NPC_Droid_Mouse", "NPC_Droid_Probe", "NPC_Droid_Protocol", "NPC_Droid_R2D2", "NPC_Droid_R5D2",
    "NPC_Droid_Remote", "NPC_Droid_Saber", "NPC_Droid_Seeker", "NPC_Droid_Sentry", "NPC_Galak",
    "NPC_Gran", "NPC_HazardTrooper", "NPC_Human_Merc", "NPC_ImpWorker", "NPC_Imperial", "NPC_Jan",
    "NPC_Jawa", "NPC_Jedi", "NPC_Kothos", "NPC_Kyle", "NPC_Lando", "NPC_Lannik_Racto", "NPC_Luke",
    "NPC_Manuel_Vergara_RMG", "NPC_Merchant", "NPC_MineMonster", "NPC_MonMothma",
    "NPC_Monster_Claw", "NPC_Monster_Fish", "NPC_Monster_Flier2", "NPC_Monster_Glider",
    "NPC_Monster_Howler", "NPC_Monster_Lizard", "NPC_Monster_Murjj", "NPC_Monster_Mutant_Rancor",
    "NPC_Monster_Rancor", "NPC_Monster_Sand_Creature", "NPC_Monster_Swamp", "NPC_Monster_Wampa",
    "NPC_MorganKatarn", "NPC_Noghri", "NPC_Player", "NPC_Prisoner", "NPC_Ragnos", "NPC_Rax",
    "NPC_Rebel", "NPC_Reborn", "NPC_Reborn_New", "NPC_Reelo", "NPC_RocketTrooper", "NPC_Rodian",
    "NPC_Rosh_Penin", "NPC_Saboteur", "NPC_ShadowTrooper", "NPC_Snowtrooper", "NPC_Stormtrooper",
    "NPC_StormtrooperOfficer", "NPC_SwampTrooper", "NPC_Tavion", "NPC_Tavion_New", "NPC_Tie_Pilot",
    "NPC_Trandoshan", "NPC_Tusken", "NPC_Ugnaught", "NPC_Vehicle", "NPC_Weequay", "NPC_spawner",
    "emplaced_eweb", "emplaced_gun", "func_bobbing", "func_breakable", "func_button", "func_door",
    "func_glass", "func_group", "func_pendulum", "func_plat", "func_rotating", "func_static",
    "func_timer", "func_train", "func_usable", "func_wall", "fx_cloudlayer", "fx_explosion_trail",
    "fx_puff", "fx_rain", "fx_runner", "fx_snow", "fx_target_beam", "fx_wind", "fx_wind_zone",
    "info_notnull", "info_null", "info_player_deathmatch", "info_player_start", "light",
    "misc_ammo_floor_unit", "misc_atst_drivable", "misc_bsp", "misc_camera", "misc_camera_focus",
    "misc_camera_track", "misc_crystal_crate", "misc_cubemap", "misc_dlight",
    "misc_exploding_crate", "misc_gas_tank", "misc_ion_cannon", "misc_laser_arm", "misc_maglock",
    "misc_model", "misc_model_ammo_power_converter", "misc_model_ammo_rack", "misc_model_beacon",
    "misc_model_bomb_planted", "misc_model_breakable", "misc_model_cargo_small",
    "misc_model_ghoul", "misc_model_gun_rack", "misc_model_jabba_cam",
    "misc_model_shield_power_converter", "misc_model_static", "misc_model_welder",
    "misc_ns_turret", "misc_panel_turret", "misc_portal_camera", "misc_portal_surface",
    "misc_replicator_item", "misc_security_panel", "misc_sentry_turret", "misc_shield_floor_unit",
    "misc_skyportal", "misc_spotlight", "misc_teleporter", "misc_teleporter_dest",
    "misc_trip_mine", "misc_turret", "misc_weapon_shooter", "misc_weather_zone",
    "object_cargo_barrel1", "path_corner", "point_combat", "rail_lane", "rail_mover", "rail_track",
    "ref_tag", "shooter_grenade", "shooter_plasma", "shooter_rocket", "target_activate",
    "target_autosave", "target_change_parm", "target_counter", "target_deactivate", "target_delay",
    "target_friction_change", "target_give", "target_gravity_change", "target_interest",
    "target_kill", "target_laser", "target_level_change", "target_location", "target_play_music",
    "target_position", "target_print", "target_push", "target_random", "target_relay",
    "target_score", "target_scriptrunner", "target_secret", "target_speaker", "target_teleporter",
    "terrain", "trigger_always", "trigger_bidirectional", "trigger_entdist", "trigger_hurt",
    "trigger_location", "trigger_multiple", "trigger_once", "trigger_push", "trigger_shipboundary",
    "trigger_space", "trigger_teleport", "trigger_visible", "waypoint", "waypoint_navgoal",
    "waypoint_small"
};
// Nur OpenJK (code/game/g_spawn.cpp spawns[]).
const char* const kNurOpenJK[] = {
    "info_NPCnav"
};
// Nur Movie Duels (MovieDuels-SJE-SP-Rend2 code/game/g_spawn.cpp:738 ff.).
const char* const kNurMD[] = {
    "NPC_Projection_close", "NPC_Projection_far", "NPC_Projection_near",
    "NPC_md_darkside_gunners_random", "NPC_md_darkside_sith1_random",
    "NPC_md_darkside_sith2_random", "NPC_md_lightside_gunners_random",
    "NPC_md_lightside_jedi1_random", "NPC_md_lightside_jedi2_random", "fx_lava",
    "info_player_duel", "info_player_duel1", "info_player_duel2", "info_player_intermission",
    "info_player_intermission_blue", "info_player_intermission_red", "info_player_siegeteam1",
    "info_player_siegeteam2", "info_player_start_blue", "info_player_start_red", "misc_radar_icon",
    "misc_turretG2", "ref_tag_huge", "trigger_asteroid_field", "trigger_hyperspace"
};
template <std::size_t N>
bool drin(const char* const (&liste)[N], const std::string& k) {
    for (const char* e : liste) {
        if (k == e) { return true; }
    }
    return false;
}
bool istGegenstand(const std::string& k) {
    return k.rfind("item_", 0) == 0 || k.rfind("weapon_", 0) == 0 || k.rfind("ammo_", 0) == 0;
}
}  // namespace

bool imSpiel(const std::string& klasse, bool movieDuels, const Gegenstandsliste* gegenstaende) {
    // G_CallSpawn (g_spawn.cpp:1021 MD): zuerst die itemlist (ext_data/
    // items.dat, siehe Gegenstandsliste::spawnbar), dann spawns[] - beides
    // mit strcmp, also mit Gross-/Kleinschreibung.
    if (gegenstaende != nullptr ? gegenstaende->spawnbar(klasse) != nullptr : istGegenstand(klasse)) { return true; }
    if (drin(kBeide, klasse)) { return true; }
    return movieDuels ? drin(kNurMD, klasse) : drin(kNurOpenJK, klasse);
}

std::string sichtbarImSpiel(const std::string& klasse, const Gegenstandsliste* gegenstaende) {
    // Je Spawnfunktion nachgesehen (MD code/game): setzt sie ein Modell
    // (G_ModelIndex, G2API_InitGhoul2Model, s.modelindex), einen Effekt oder
    // eine Figur? misc_model gibt das Spiel frei (SP_misc_model,
    // g_misc.cpp:383 G_FreeEntity) - q3map2 hat es in die Karte gebacken.
    if (gegenstaende != nullptr ? gegenstaende->spawnbar(klasse) != nullptr : istGegenstand(klasse)) {
        return "Gegenstand";   // G_SpawnItem, g_items.cpp
    }
    if (klasse.rfind("NPC_", 0) == 0) { return "Figur"; }
    static const std::map<std::string, std::string> kArt = {
        // g_misc.cpp, g_misc_model.cpp
        {"misc_model_static", "Modell"}, {"misc_model_breakable", "Modell"}, {"misc_model_ghoul", "Modell"},
        {"misc_model_gun_rack", "Modell"}, {"misc_model_ammo_rack", "Modell"}, {"misc_model_cargo_small", "Modell"},
        {"misc_model_bomb_planted", "Modell"}, {"misc_model_beacon", "Modell"}, {"misc_model_welder", "Modell"},
        {"misc_model_jabba_cam", "Modell"}, {"misc_model_shield_power_converter", "Modell"},
        {"misc_model_ammo_power_converter", "Modell"}, {"misc_shield_floor_unit", "Modell"},
        {"misc_ammo_floor_unit", "Modell"}, {"misc_exploding_crate", "Modell"}, {"misc_gas_tank", "Modell"},
        {"misc_crystal_crate", "Modell"}, {"misc_security_panel", "Modell"}, {"misc_camera", "Modell"},
        {"misc_maglock", "Modell"}, {"misc_ion_cannon", "Modell"}, {"misc_trip_mine", "Modell"},
        {"misc_atst_drivable", "Figur"}, {"object_cargo_barrel1", "Modell"}, {"misc_bsp", "Modell"},
        {"terrain", "Modell"},
        // g_turret.cpp
        {"misc_turret", "Geschuetz"}, {"misc_turretG2", "Geschuetz"}, {"misc_ns_turret", "Geschuetz"},
        {"misc_laser_arm", "Geschuetz"}, {"misc_sentry_turret", "Geschuetz"}, {"misc_spotlight", "Geschuetz"},
        {"misc_panel_turret", "Geschuetz"},
        // g_emplaced.cpp
        {"emplaced_gun", "Geschuetz"}, {"emplaced_eweb", "Geschuetz"},
        // g_fx.cpp
        {"fx_runner", "Effekt"}, {"fx_rain", "Effekt"}, {"fx_snow", "Effekt"}, {"fx_cloudlayer", "Effekt"},
        {"fx_lava", "Effekt"}, {"fx_target_beam", "Effekt"}, {"fx_explosion_trail", "Effekt"},
        {"fx_puff", "Effekt"},
        // Portale und Himmel
        {"misc_portal_surface", "Portal"}, {"misc_skyportal", "Portal"},
    };
    const auto it = kArt.find(klasse);
    return it == kArt.end() ? std::string() : it->second;
}

}  // namespace bhed::kartenpruefung
