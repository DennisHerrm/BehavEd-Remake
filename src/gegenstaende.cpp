// gegenstaende.cpp - siehe bhed/gegenstaende.h (rc602).
#include "bhed/gegenstaende.h"

#include "bhed/bsp.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace bhed {

namespace {

// Die Aufzaehlung ITM_* in Reihenfolge (code/game/g_items.h).
// Movie Duels (MovieDuels-SJE-SP-Rend2 code/game/g_items.h).
const char* const kItmMD[] = {
    "ITM_NONE", "ITM_SABER_PICKUP", "ITM_BLASTER_PISTOL_PICKUP", "ITM_BLASTER_PICKUP",
    "ITM_DISRUPTOR_PICKUP", "ITM_BOWCASTER_PICKUP", "ITM_REPEATER_PICKUP", "ITM_DEMP2_PICKUP",
    "ITM_FLECHETTE_PICKUP", "ITM_CONCUSSION_RIFLE_PICKUP", "ITM_ROCKET_LAUNCHER_PICKUP",
    "ITM_THERMAL_DET_PICKUP", "ITM_TRIP_MINE_PICKUP", "ITM_DET_PACK_PICKUP", "ITM_STUN_BATON_PICKUP",
    "ITM_MELEE", "ITM_BRYAR_PISTOL_PICKUP", "ITM_EMPLACED_GUN_PICKUP", "ITM_DROIDEKA_PICKUP",
    "ITM_BOT_LASER_PICKUP", "ITM_TURRET_PICKUP", "ITM_ATST_MAIN_PICKUP", "ITM_ATST_SIDE_PICKUP",
    "ITM_TIE_FIGHTER_PICKUP", "ITM_RAPID_FIRE_CONC_PICKUP", "ITM_JAWA_PICKUP",
    "ITM_TUSKEN_RIFLE_PICKUP", "ITM_TUSKEN_STAFF_PICKUP", "ITM_SCEPTER_PICKUP",
    "ITM_NOGHRI_STICK_PICKUP", "ITM_BATTLEDROID_PICKUP", "ITM_THEFIRSTORDER_PICKUP",
    "ITM_CLONECARBINE_PICKUP", "ITM_REBELBLASTER_PICKUP", "ITM_CLONERIFLE_PICKUP",
    "ITM_CLONECOMMANDO_PICKUP", "ITM_Z6_ROTARY_CANNON_PICKUP", "ITM_REBELRIFLE_PICKUP",
    "ITM_REY_PICKUP", "ITM_JANGO_PICKUP", "ITM_BOBA_PICKUP", "ITM_CLONEPISTOL_PICKUP",
    "ITM_SBD_BLASTER_PICKUP", "ITM_DUALPISTOL_PICKUP", "ITM_DUALCLONEPISTOLPISTOL_PICKUP",
    "ITM_WRIST_BLASTER_PICKUP", "ITM_AMMO_FORCE_PICKUP", "ITM_AMMO_BLASTER_PICKUP",
    "ITM_AMMO_POWERCELL_PICKUP", "ITM_AMMO_METAL_BOLTS_PICKUP", "ITM_AMMO_ROCKETS_PICKUP",
    "ITM_AMMO_EMPLACED_PICKUP", "ITM_AMMO_THERMAL_PICKUP", "ITM_AMMO_TRIPMINE_PICKUP",
    "ITM_AMMO_DETPACK_PICKUP", "ITM_FORCE_HEAL_PICKUP", "ITM_FORCE_LEVITATION_PICKUP",
    "ITM_FORCE_SPEED_PICKUP", "ITM_FORCE_PUSH_PICKUP", "ITM_FORCE_PULL_PICKUP",
    "ITM_FORCE_TELEPATHY_PICKUP", "ITM_FORCE_GRIP_PICKUP", "ITM_FORCE_LIGHTNING_PICKUP",
    "ITM_FORCE_SABERTHROW_PICKUP", "ITM_BATTERY_PICKUP", "ITM_SEEKER_PICKUP", "ITM_SHIELD_PICKUP",
    "ITM_BACTA_PICKUP", "ITM_DATAPAD_PICKUP", "ITM_BINOCULARS_PICKUP", "ITM_SENTRY_GUN_PICKUP",
    "ITM_LA_GOGGLES_PICKUP", "ITM_CLOAK_PICKUP", "ITM_BARRIER_PICKUP", "ITM_GRAPPLEHOOK_PICKUP",
    "ITM_MEDPAK_PICKUP", "ITM_SHIELD_SM_PICKUP", "ITM_SHIELD_LRG_PICKUP", "ITM_GOODIE_KEY_PICKUP",
    "ITM_SECURITY_KEY_PICKUP"
};
// rc604 jk2: Jedi Outcast (codeJK2/game/g_items.h, itemNum_e) - andere
// Reihenfolge, ohne die JKA-Waffen.
const char* const kItmJk2[] = {
    "ITM_NONE", "ITM_SABER_PICKUP", "ITM_BRYAR_PISTOL_PICKUP", "ITM_BLASTER_PICKUP", "ITM_DISRUPTOR_PICKUP",
    "ITM_BOWCASTER_PICKUP", "ITM_REPEATER_PICKUP", "ITM_DEMP2_PICKUP", "ITM_FLECHETTE_PICKUP",
    "ITM_ROCKET_LAUNCHER_PICKUP", "ITM_THERMAL_DET_PICKUP", "ITM_TRIP_MINE_PICKUP", "ITM_DET_PACK_PICKUP",
    "ITM_STUN_BATON_PICKUP", "ITM_BOT_LASER_PICKUP", "ITM_EMPLACED_GUN_PICKUP", "ITM_TURRET_PICKUP",
    "ITM_MELEE", "ITM_ATST_MAIN_PICKUP", "ITM_ATST_SIDE_PICKUP", "ITM_TIE_FIGHTER_PICKUP",
    "ITM_RAPID_FIRE_CONC_PICKUP", "ITM_BLASTER_PISTOL_PICKUP", "ITM_AMMO_FORCE_PICKUP",
    "ITM_AMMO_BLASTER_PICKUP", "ITM_AMMO_POWERCELL_PICKUP", "ITM_AMMO_METAL_BOLTS_PICKUP",
    "ITM_AMMO_ROCKETS_PICKUP", "ITM_AMMO_EMPLACED_PICKUP", "ITM_AMMO_THERMAL_PICKUP",
    "ITM_AMMO_TRIPMINE_PICKUP", "ITM_AMMO_DETPACK_PICKUP", "ITM_FORCE_HEAL_PICKUP",
    "ITM_FORCE_LEVITATION_PICKUP", "ITM_FORCE_SPEED_PICKUP", "ITM_FORCE_PUSH_PICKUP", "ITM_FORCE_PULL_PICKUP",
    "ITM_FORCE_TELEPATHY_PICKUP", "ITM_FORCE_GRIP_PICKUP", "ITM_FORCE_LIGHTNING_PICKUP",
    "ITM_FORCE_SABERTHROW_PICKUP", "ITM_BATTERY_PICKUP", "ITM_SEEKER_PICKUP", "ITM_SHIELD_PICKUP",
    "ITM_BACTA_PICKUP", "ITM_DATAPAD_PICKUP", "ITM_BINOCULARS_PICKUP", "ITM_SENTRY_GUN_PICKUP",
    "ITM_LA_GOGGLES_PICKUP", "ITM_MEDPAK_PICKUP", "ITM_SHIELD_SM_PICKUP", "ITM_SHIELD_LRG_PICKUP",
    "ITM_GOODIE_KEY_PICKUP", "ITM_SECURITY_KEY_PICKUP",
};
// OpenJK (code/game/g_items.h).
const char* const kItmOpenJK[] = {
    "ITM_NONE", "ITM_SABER_PICKUP", "ITM_BLASTER_PISTOL_PICKUP", "ITM_BLASTER_PICKUP",
    "ITM_DISRUPTOR_PICKUP", "ITM_BOWCASTER_PICKUP", "ITM_REPEATER_PICKUP", "ITM_DEMP2_PICKUP",
    "ITM_FLECHETTE_PICKUP", "ITM_CONCUSSION_RIFLE_PICKUP", "ITM_ROCKET_LAUNCHER_PICKUP",
    "ITM_THERMAL_DET_PICKUP", "ITM_TRIP_MINE_PICKUP", "ITM_DET_PACK_PICKUP", "ITM_STUN_BATON_PICKUP",
    "ITM_MELEE", "ITM_BRYAR_PISTOL_PICKUP", "ITM_EMPLACED_GUN_PICKUP", "ITM_BOT_LASER_PICKUP",
    "ITM_TURRET_PICKUP", "ITM_ATST_MAIN_PICKUP", "ITM_ATST_SIDE_PICKUP", "ITM_TIE_FIGHTER_PICKUP",
    "ITM_RAPID_FIRE_CONC_PICKUP", "ITM_JAWA_PICKUP", "ITM_TUSKEN_RIFLE_PICKUP",
    "ITM_TUSKEN_STAFF_PICKUP", "ITM_SCEPTER_PICKUP", "ITM_NOGHRI_STICK_PICKUP",
    "ITM_AMMO_FORCE_PICKUP", "ITM_AMMO_BLASTER_PICKUP", "ITM_AMMO_POWERCELL_PICKUP",
    "ITM_AMMO_METAL_BOLTS_PICKUP", "ITM_AMMO_ROCKETS_PICKUP", "ITM_AMMO_EMPLACED_PICKUP",
    "ITM_AMMO_THERMAL_PICKUP", "ITM_AMMO_TRIPMINE_PICKUP", "ITM_AMMO_DETPACK_PICKUP",
    "ITM_FORCE_HEAL_PICKUP", "ITM_FORCE_LEVITATION_PICKUP", "ITM_FORCE_SPEED_PICKUP",
    "ITM_FORCE_PUSH_PICKUP", "ITM_FORCE_PULL_PICKUP", "ITM_FORCE_TELEPATHY_PICKUP",
    "ITM_FORCE_GRIP_PICKUP", "ITM_FORCE_LIGHTNING_PICKUP", "ITM_FORCE_SABERTHROW_PICKUP",
    "ITM_BATTERY_PICKUP", "ITM_SEEKER_PICKUP", "ITM_SHIELD_PICKUP", "ITM_BACTA_PICKUP",
    "ITM_DATAPAD_PICKUP", "ITM_BINOCULARS_PICKUP", "ITM_SENTRY_GUN_PICKUP", "ITM_LA_GOGGLES_PICKUP",
    "ITM_MEDPAK_PICKUP", "ITM_SHIELD_SM_PICKUP", "ITM_SHIELD_LRG_PICKUP", "ITM_GOODIE_KEY_PICKUP",
    "ITM_SECURITY_KEY_PICKUP"
};

bool gleichOhneFall(const std::string& a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return i == a.size() && b[i] == '\0';
}

// COM_ParseExt: Woerter, "..." am Stueck, // und /* */ sind Kommentare.
struct Leser {
    const std::string& t;
    std::size_t at = 0;
    bool zeilenEnde = false;   // vor dem gelieferten Wort lag ein Zeilenende
    std::string wort() {
        zeilenEnde = false;
        for (;;) {
            while (at < t.size() && std::isspace(static_cast<unsigned char>(t[at])) != 0) {
                if (t[at] == '\n') { zeilenEnde = true; }
                ++at;
            }
            if (at + 1 < t.size() && t[at] == '/' && t[at + 1] == '/') {
                while (at < t.size() && t[at] != '\n') { ++at; }
                continue;
            }
            if (at + 1 < t.size() && t[at] == '/' && t[at + 1] == '*') {
                const std::size_t e = t.find("*/", at + 2);
                at = (e == std::string::npos) ? t.size() : e + 2;
                continue;
            }
            break;
        }
        if (at >= t.size()) { return std::string(); }
        std::string w;
        if (t[at] == '"') {
            ++at;
            while (at < t.size() && t[at] != '"') { w += t[at++]; }
            if (at < t.size()) { ++at; }
            return w;
        }
        while (at < t.size() && std::isspace(static_cast<unsigned char>(t[at])) == 0) { w += t[at++]; }
        return w;
    }
    // SkipRestOfLine
    void zeileUeberspringen() {
        while (at < t.size() && t[at] != '\n') { ++at; }
    }
    // COM_ParseString/COM_ParseInt lesen das naechste Wort derselben Zeile
    // (COM_ParseExt(data, qfalse)): am Zeilenende gibt es keins.
    bool wortDerZeile(std::string& w) {
        const std::size_t merk = at;
        w = wort();
        if (zeilenEnde || w.empty()) {
            at = merk;
            return false;
        }
        return true;
    }
};

}  // namespace

const Gegenstand* Gegenstandsliste::spawnbar(const std::string& klasse) const {
    // G_CallSpawn (g_spawn.cpp:1021 MD): ab Platz 1, bis zum ersten Platz ohne
    // classname, strcmp.
    for (std::size_t i = 1; i < plaetze.size(); ++i) {
        if (plaetze[i].classname.empty()) { return nullptr; }
        if (plaetze[i].classname == klasse) { return &plaetze[i]; }
    }
    return nullptr;
}

Gegenstandsliste leseGegenstaende(const std::string& itemsDat, bool movieDuels, bool jediOutcast) {
    Gegenstandsliste l;
    // rc604 jk2: IT_Name sucht in der itemNum_e von JK2 (g_itemLoad.cpp,
    // dieselben Schluessel itemname ... worldmodel, codeJK2 :60-69).
    const char* const* namen = jediOutcast ? kItmJk2 : movieDuels ? kItmMD : kItmOpenJK;
    const std::size_t zahl = jediOutcast  ? std::size(kItmJk2)
                             : movieDuels ? std::size(kItmMD)
                                          : std::size(kItmOpenJK);
    l.plaetze.resize(zahl);
    for (std::size_t i = 0; i < zahl; ++i) { l.plaetze[i].itemname = namen[i]; }
    Leser r{itemsDat};
    std::size_t nr = 0;   // itemParms.itemNum
    // IT_ParseParms: jedes "{" beginnt IT_ParseWeaponParms bis "}".
    for (std::string w = r.wort(); !w.empty(); w = r.wort()) {
        if (w != "{") { continue; }
        for (std::string k = r.wort(); !k.empty() && k != "}"; k = r.wort()) {
            std::string v;
            if (gleichOhneFall(k, "itemname")) {
                // IT_Name: unbekannte Namen landen auf Platz 0 (ITM_NONE).
                nr = 0;
                if (r.wortDerZeile(v)) {
                    for (std::size_t i = 0; i < zahl; ++i) {
                        if (gleichOhneFall(v, namen[i])) { nr = i; }
                    }
                }
                // IT_SetDefaults
                l.plaetze[nr].mins = {-16.0F, -16.0F, -2.0F};
                l.plaetze[nr].maxs = {16.0F, 16.0F, 16.0F};
            } else if (gleichOhneFall(k, "classname")) {
                if (r.wortDerZeile(v)) { l.plaetze[nr].classname = v; }
            } else if (gleichOhneFall(k, "worldmodel")) {
                if (r.wortDerZeile(v)) { l.plaetze[nr].worldmodel = v; }
            } else if (gleichOhneFall(k, "type")) {
                if (r.wortDerZeile(v)) {
                    for (char& c : v) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
                    l.plaetze[nr].typ = v;
                }
            } else if (gleichOhneFall(k, "tag")) {
                if (r.wortDerZeile(v)) {
                    for (char& c : v) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
                    l.plaetze[nr].tag = v;
                }
            } else if (gleichOhneFall(k, "min") || gleichOhneFall(k, "max")) {
                // IT_Min/IT_Max: drei COM_ParseInt; fehlt eins, Rest der Zeile weg.
                std::array<float, 3>& ziel = gleichOhneFall(k, "min") ? l.plaetze[nr].mins : l.plaetze[nr].maxs;
                for (int i = 0; i < 3; ++i) {
                    if (!r.wortDerZeile(v)) {
                        r.zeileUeberspringen();
                        break;
                    }
                    ziel[static_cast<std::size_t>(i)] = static_cast<float>(std::atoi(v.c_str()));
                }
            } else if (gleichOhneFall(k, "count") || gleichOhneFall(k, "icon") || gleichOhneFall(k, "pickupsound")) {
                (void)r.wortDerZeile(v);
            } else {
                // "bad parameter in external item data" - Rest der Zeile weg.
                r.zeileUeberspringen();
            }
        }
    }
    return l;
}

}  // namespace bhed

// --- Aufstellen ---------------------------------------------------------------

namespace bhed {

bool spawntBeiSchwierigkeit(const MapEntity& e, int spielstufe) {
    // G_SpawnInt("notsingle", "0", &i): atoi.
    if (const std::string* ns = e.find("notsingle"); ns != nullptr && std::atoi(ns->c_str()) != 0) { return false; }
    // "easy -256 medium -512 hard -1024": spawnflags & (1 << (8 + g_spskill)).
    const std::string* sf = e.find("spawnflags");
    const int flags = sf != nullptr ? std::atoi(sf->c_str()) : 0;
    const int stufe = spielstufe < 0 ? 0 : spielstufe;
    return (flags & (1 << (8 + stufe))) == 0;
}

GegenstandAufstellung gegenstandAufstellen(const MapEntity& e, const Gegenstand& g, int spielstufe, bool movieDuels,
                                           const GegenstandSpur& spur) {
    GegenstandAufstellung a;
    if (!spawntBeiSchwierigkeit(e, spielstufe)) {
        a.grund = "Schwierigkeit/notsingle";
        return a;
    }
    // G_SpawnItem: "targetname indicates they want to spawn it later".
    if (!e.targetname.empty() || e.find("targetname") != nullptr) {
        a.grund = "wartet auf use";
        return a;
    }
    const std::string* sfText = e.find("spawnflags");
    const int sf = sfText != nullptr ? std::atoi(sfText->c_str()) : 0;
    // FinishSpawningItem, Schritt 5: weapon_saber mit NPC_type nimmt das
    // Modell aus der .sab (WP_SaberParseParms) - nicht nachgebildet.
    if (g.typ == "IT_WEAPON" && g.tag == "WP_SABER" && e.find("NPC_type") != nullptr &&
        !e.find("NPC_type")->empty()) {
        a.grund = "Schwert mit NPC_type (Modell aus der .sab) - nicht nachgebildet";
        return a;
    }
    // ITMSF_INVISIBLE: EF_NODRAW (CG_Item zeichnet nichts).
    if ((sf & kItmsfInvisible) != 0) {
        a.grund = "ITMSF_INVISIBLE";
        return a;
    }
    a.modell = g.worldmodel;
    if (a.modell.empty()) {
        a.grund = "kein worldmodel";
        return a;
    }
    std::string klein = a.modell;
    for (char& c : klein) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    a.glm = klein.size() > 4 && klein.compare(klein.size() - 4, 4, ".glm") == 0;
    // Winkel: G_ParseField, "angles" F_VECTOR, "angle" F_ANGLEHACK (0 v 0);
    // der spaetere Schluessel gewinnt. Movie Duels: "angles" mit EINER Zahl
    // ist der Gierwinkel (g_spawn.cpp:1125 MD); OpenJK liest sie als Nicken.
    for (const auto& [k, v] : e.keys) {
        std::string lk = k;
        for (char& c : lk) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        if (lk == "angles") {
            float w[3] = {0.0F, 0.0F, 0.0F};
            const int n = std::sscanf(v.c_str(), "%f %f %f", &w[0], &w[1], &w[2]);
            if (n == 1 && movieDuels) {
                w[1] = w[0];
                w[0] = 0.0F;
            }
            a.winkel = {w[0], w[1], w[2]};
        } else if (lk == "angle") {
            a.winkel = {0.0F, static_cast<float>(std::atof(v.c_str())), 0.0F};
        }
    }
    float o[3] = {0.0F, 0.0F, 0.0F};
    (void)std::sscanf(e.origin.c_str(), "%f %f %f", &o[0], &o[1], &o[2]);
    a.ort = {o[0], o[1], o[2]};
    // 2. Bounding box: beide Null -> ITEM_RADIUS (bg_public.h:66 MD, 15).
    std::array<float, 3> mins = g.mins;
    std::array<float, 3> maxs = g.maxs;
    if (mins == std::array<float, 3>{0.0F, 0.0F, 0.0F} && maxs == std::array<float, 3>{0.0F, 0.0F, 0.0F}) {
        mins = {-15.0F, -15.0F, -2.0F};
        maxs = {15.0F, 15.0F, 15.0F};
    }
    // 7. "ent->s.origin[2] += 1"; ohne ITMSF_SUSPEND bis MIN_WORLD_COORD
    // (-64*1024, q_shared.h:925) fallen; im Festen -> G_FreeEntity.
    a.ort[2] += 1.0F;
    if ((sf & kItmsfSuspend) == 0 && spur) {
        const std::array<float, 3> ende{a.ort[0], a.ort[1], -64.0F * 1024.0F};
        std::array<float, 3> endpos = a.ort;
        bool startsolid = false;
        spur(a.ort, mins, maxs, ende, endpos, startsolid);
        if (startsolid) {
            a.grund = "beginnt im Festen (FinishSpawningItem entfernt ihn)";
            return a;
        }
        a.ort = endpos;
    }
    // CG_Item: spinAngles = s.angles; ITMSF_VERTICAL kippt die Achsen.
    a.md3Winkel = a.winkel;
    if ((sf & kItmsfVertical) != 0) {
        if (g.typ == "IT_WEAPON" && g.tag == "WP_SABER") {
            const std::string* r = e.find("random");   // G_SpawnFloat("random", "0")
            const float zufall = r != nullptr ? static_cast<float>(std::atof(r->c_str())) : 0.0F;
            a.md3Winkel[0] += (zufall != 0.0F) ? zufall : -20.0F;
        } else {
            a.md3Winkel[0] -= 75.0F;
        }
    }
    a.sichtbar = true;
    return a;
}

}  // namespace bhed
