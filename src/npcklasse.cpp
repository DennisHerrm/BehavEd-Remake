// npcklasse.cpp - rc602: die SP_NPC_*-Funktionen beider Regelwerke als
// Tabelle "Klassenname -> NPC_type". Siehe den Kopf von bhed/npcklasse.h.
//
// Abgeschrieben aus code/game/NPC_spawn.cpp (OpenJK, JACoders/OpenJK) und
// code/game/NPC_spawn.cpp (MovieDuels-SJE-SP-Rend2). Zeilen: OpenJK zuerst,
// Movie Duels mit "MD". Die Reihenfolge der Zweige ist die der Engine - sie
// entscheidet, wenn mehrere Flags gesetzt sind (SP_NPC_Stormtrooper prueft 8
// vor 4 vor 2 vor 1).
#include "bhed/npcklasse.h"

#include "bhed/regelwerk.h"

#include <cstdlib>
#include <initializer_list>

namespace bhed {

namespace {

struct Bau {
    const std::string& schluessel;
    int sf;
    bool message;
    NpcKlassenTyp& r;

    // self->NPC_type = "..."; - ohne Ruecksicht auf den Schluessel.
    void setze(const char* t) {
        r.typ = t;
        r.ausKlasse = true;
        r.zufall.clear();
    }
    // Q_irand ueber die Liste: die Vorschau zeigt die erste Moeglichkeit.
    void zufall(std::initializer_list<const char*> l) {
        r.zufall.assign(l.begin(), l.end());
        r.typ = r.zufall.front();
        r.ausKlasse = true;
    }
    // if ( !self->NPC_type ) - der Schluessel der Karte geht vor.
    [[nodiscard]] bool frei() const { return schluessel.empty(); }
    [[nodiscard]] bool f(int bit) const { return (sf & bit) != 0; }
};

// Movie Duels: die Listen der Zufallsspawner (NPC_spawn.cpp:63 ff. MD),
// gezogen mit Q_irand2(0, n) = rand() % (n + 1) (q_math.c:274 MD).
const std::initializer_list<const char*> kLightsideJedi1 = {
    "md_mundi", "md_depa", "md_aayla", "md_luminara", "md_shaak_ti", "md_adi_gallia", "md_yarael", "md_yaddle",
    "md_coleman", "md_yoda", "md_windu", "md_quigon", "md_fisto", "md_drallig", "md_plo_koon", "md_saesee",
    "md_jocasta", "md_barriss", "md_ongree", "md_sora", "md_tarados", "md_obi_ep3", "md_ani_ep3", "md_foul_moudama",
    "md_youngling", "md_jed1", "md_jed6", "md_jed10", "md_jbrute", "md_ezra", "md_kanan", "md_caleb",
    "md_gungan_warrior", "md_bultar", "md_agen", "cal_kestis", "md_sabine", "md_joopi", "md_tsuichoi",
    "md_stass_allie", "md_sarissa_jeng", "md_redath", "md_micah", "md_bolla_ropal", "md_koffi", "md_corran",
    "md_jolee", "md_atris", "md_meetra"};
const std::initializer_list<const char*> kLightsideJedi2 = {
    "md_ahsoka_rebels", "md_plo_tcw", "md_fisto_tcw", "md_adi_tcw", "md_eeth_koth", "md_even_piell", "md_quinlan",
    "md_drallig_tcw", "md_tiplee", "md_tiplar", "md_serra", "jedimaster", "md_obi_tcw", "md_ani_tcw", "md_ahsoka_s7",
    "md_windu_tcw", "md_templeguard", "md_depa_tcw_robed", "jaro_tapal", "md_obi_jabiim", "md_ben_robed",
    "md_luke_rotj", "md_luke_crait", "md_rey_skywalker", "md_rahn", "md_kyle", "md_mara", "rosh_penin",
    "md_shaakti_tfu", "militiatrooper", "md_ahsoka_tm", "md_luke_hooded_tm", "md_kento_marek", "md_kota", "md_galen",
    "md_bastila", "md_jed_nikto", "md_halsey", "md_ima_gundi", "md_knox", "md_tera_sinube", "md_nahdar",
    "md_oppo_rancisis", "md_thongla_jur", "md_falon_grey", "jedi_vrook", "md_revan_jedi"};
const std::initializer_list<const char*> kLightsideGunners = {
    "md_pad_ga", "md_clo_cody", "md_clo_rex", "md_clo_ep2", "md_duchess_satine", "md_nien_nunb",
    "md_bail_organa_tfu", "md_ackbar", "bespincop", "md_dindjarin", "md_grogu", "md_greef", "md_cara_dune",
    "md_kuiil", "md_new_rep_sec_droid", "rebel2", "militiasaboteur", "md_poe", "jan", "md_hera", "md_rex_old",
    "md_leia_anh", "md_han_esb", "md_chewie", "md_lando_bespin", "md_mon_mothma", "md_cassian", "md_bodhi",
    "md_chirrut", "md_jarjar", "md_baze_malbus", "md_jyn_erso", "md_finn", "md_rose_tico", "md_k2so",
    "md_rebel_pilot", "md_mando_dw", "md_luthen"};
const std::initializer_list<const char*> kDarksideSith1 = {
    "md_dooku", "md_magnaguard", "md_clone_assassin", "md_kylo_shirtless", "md_sidious_ep3_robed", "md_ani_sith",
    "md_maul", "md_grievous", "md_gua_am", "md_kyle_emp", "md_sidious_tcw", "md_savage", "md_dooku_tcw",
    "md_maul_cyber_tcw", "md_maul_tcw_staff", "md_ventress", "darthdesolous", "darthphobos", "shadowtrooper",
    "reborn", "md_vader", "md_emperor2", "md_royalguard", "md_shadowguard", "md_royalcombat", "purge_trooper",
    "md_mando_vizsla", "md_malak", "md_nihilus", "md_traya", "md_thexan", "md_visas", "md_sion"};
const std::initializer_list<const char*> kDarksideSith2 = {
    "stormie_tfa_riot", "md_emperor_ros_blind", "md_kylo_ros_helmet", "md_pguard", "md_maw", "md_yun", "md_boc",
    "md_pic", "md_gorc", "md_jerec", "md_sariss", "tavion", "alora_dual", "md_desann", "gamorrean", "vizam_tgpoc",
    "md_mau_luke", "md_plagueis", "md_starkiller", "md_revan", "md_7thsister", "md_5thbrother",
    "md_templeguard_inquisitor", "md_8thbrother", "md_maul_rebels6", "md_2ndsister", "md_shin", "md_baylan",
    "md_dagan", "md_arcann", "md_bandon", "md_bane_armor", "md_malgus_hooded"};
const std::initializer_list<const char*> kDarksideGunners = {
    "md_sbd_am", "md_tus2_tc", "battledroid", "md_sithtrooper", "md_hondo", "swamptrooper", "md_embo", "md_phasma",
    "md_tarkin", "saboteur", "md_bossk", "md_hux", "md_fennec", "md_greedo", "md_krennic", "md_thrawn", "weequay",
    "stormtrooper", "md_deathtrooper", "md_shoretrooper", "md_darktrooper", "md_gideon", "stormie_tfa", "impworker",
    "md_mother_talzin", "md_cadbane", "md_durge", "md_jango", "boba_fett_esb", "md_mando_maul3",
    "md_mandalore_ultimate", "md_neocrusader_red", "md_hk47"};

// SP_NPC_Cultist_Saber (:3304) und _Powers (:3375). Beide pruefen
// "spawnflags&2" ZWEIMAL - der Zweig ALL (4) ist so nie erreichbar, und
// mit nur Flag 4 landet die Figur im letzten else. Wie im Spiel.
void cultistSaber(Bau& b, bool powers) {
    if (!b.frei()) { return; }
    if (b.f(1)) {
        b.setze(b.f(8) ? (powers ? "cultist_saber_med_throw2" : "cultist_saber_med_throw")
                       : (powers ? "cultist_saber_med2" : "cultist_saber_med"));
    } else if (b.f(2)) {
        b.setze(b.f(8) ? (powers ? "cultist_saber_strong_throw2" : "cultist_saber_strong_throw")
                       : (powers ? "cultist_saber_strong2" : "cultist_saber_strong"));
    } else {
        // Auch _Powers nimmt hier "cultist_saber_throw" OHNE 2 (:3375 ff.).
        b.setze(b.f(8) ? "cultist_saber_throw" : (powers ? "cultist_saber2" : "cultist_saber"));
    }
}

// Die Funktionen, die in beiden Regelwerken gleich sind. false = kein
// Eintrag dieser Klasse.
bool gemeinsam(const std::string& k, Bau& b) {
    // --- feste Namen: ueberschreiben den Schluessel --------------------
    struct Fest {
        const char* klasse;
        const char* typ;
    };
    static const Fest kFest[] = {
        {"NPC_Player", "Player"},            // :2149
        {"NPC_Lando", "Lando"},              // :2184
        {"NPC_Jan", "Jan"},                  // :2198
        {"NPC_Luke", "Luke"},                // :2212
        {"NPC_MonMothma", "MonMothma"},      // :2226
        {"NPC_Tavion", "Tavion"},            // :2267
        {"NPC_Reelo", "Reelo"},              // :2334
        {"NPC_Desann", "Desann"},            // :2361
        {"NPC_Manuel_Vergara_RMG", "Desann"},   // spawns[] -> SP_NPC_Desann
        {"NPC_Rax", "Rax"},                  // :2376
        {"NPC_BobaFett", "Boba_Fett"},       // :2390
        {"NPC_Ragnos", "Ragnos"},            // :2403
        {"NPC_Lannik_Racto", "lannik_racto"},   // :2417
        {"NPC_Chewbacca", "Chewie"},         // :2456
        {"NPC_Bartender", "Bartender"},      // :2470
        {"NPC_MorganKatarn", "MorganKatarn"},   // :2484
        {"NPC_Merchant", "merchant"},        // :2639
        {"NPC_Snowtrooper", "snowtrooper"},  // :2780
        {"NPC_Tie_Pilot", "stormpilot"},     // :2795 (MD mit com_outcast 1: stormpilot_jk2)
        {"NPC_ImpWorker", "ImpWorker"},      // :3139
        {"NPC_Cultist_Destroyer", "cultist_destroyer"},   // :3523
        {"NPC_Monster_Murjj", "Murjj"},      // :3602
        {"NPC_Monster_Swamp", "Swamp"},      // :3616
        {"NPC_Monster_Howler", "howler"},    // :3630
        {"NPC_Monster_Mutant_Rancor", "mutant_rancor"},   // :3671
        {"NPC_Monster_Wampa", "wampa"},      // :3684
        {"NPC_MineMonster", "minemonster"},  // :3697
        {"NPC_Monster_Claw", "Claw"},        // :3711
        {"NPC_Monster_Glider", "Glider"},    // :3725
        {"NPC_Monster_Flier2", "Flier2"},    // :3739
        {"NPC_Monster_Lizard", "Lizard"},    // :3753
        {"NPC_Monster_Fish", "Fish"},        // :3767
        {"NPC_Droid_Interrogator", "interrogator"},   // :3808
        {"NPC_Droid_Probe", "probe"},        // :3824
        {"NPC_Droid_Mark1", "mark1"},        // :3841
        {"NPC_Droid_Mark2", "mark2"},        // :3858
        {"NPC_Droid_ATST", "atst"},          // :3872
        {"NPC_Droid_Remote", "remote_sp"},   // :3888
        {"NPC_Droid_Seeker", "seeker"},      // :3904
        {"NPC_Droid_Sentry", "sentry"},      // :3920
        {"NPC_Droid_Gonk", "gonk"},          // :3939
        {"NPC_Droid_Mouse", "mouse"},        // :3957
    };
    for (const Fest& x : kFest) {
        if (k == x.klasse) {
            b.setze(x.typ);
            return true;
        }
    }
    // --- feste Namen mit spawnflags ------------------------------------
    struct FestFlag {
        const char* klasse;
        int bit;
        const char* mit;
        const char* ohne;
    };
    static const FestFlag kFestFlag[] = {
        {"NPC_Kyle", 1, "Kyle_boss", "Kyle"},                     // :2163
        {"NPC_Alora", 1, "alora_dual", "alora"},                  // :2313
        {"NPC_Kothos", 1, "VKothos", "DKothos"},                  // :2435
        {"NPC_Monster_Rancor", 1, "mutant_rancor", "rancor"},     // :3647
        {"NPC_Monster_Sand_Creature", 1, "sand_creature_fast", "sand_creature"},   // :3783
        {"NPC_Droid_R2D2", 1, "r2d2_imp", "r2d2"},                // :3975, MD :5312
        {"NPC_Droid_R5D2", 1, "r5d2_imp", "r5d2"},                // :4001
        {"NPC_Droid_Protocol", 1, "protocol_imp", "protocol"},    // :4024
    };
    for (const FestFlag& x : kFestFlag) {
        if (k == x.klasse) {
            b.setze(b.f(x.bit) ? x.mit : x.ohne);
            return true;
        }
    }
    if (k == "NPC_Rosh_Penin") {   // :2242
        b.setze(b.f(1) ? "rosh_dark" : b.f(2) ? "rosh_penin_noforce" : "rosh_penin");
        return true;
    }
    if (k == "NPC_Tavion_New") {   // :2285
        b.setze(b.f(1) ? "tavion_scepter" : b.f(2) ? "tavion_sith_sword" : "tavion_new");
        return true;
    }
    if (k == "NPC_Stormtrooper" || k == "NPC_StormtrooperOfficer") {
        // :2734 - OHNE "if (!self->NPC_type)": ueberschreibt immer.
        // NPC_StormtrooperOfficer: spawnflags |= 1, dann dasselbe (:2766).
        // Movie Duels mit com_outcast 1 nimmt die *_jk2-Fassungen; Vorgabe
        // "0" (g_main.cpp:889 MD) - also dieselben Namen.
        const int sf = k == "NPC_StormtrooperOfficer" ? (b.sf | 1) : b.sf;
        if ((sf & 8) != 0) {
            b.setze("rockettrooper");
        } else if ((sf & 4) != 0) {
            b.setze("stofficeralt");
        } else if ((sf & 2) != 0) {
            b.setze("stcommander");
        } else if ((sf & 1) != 0) {
            b.setze("stofficer");
        } else {
            b.zufall({"StormTrooper", "StormTrooper2"});   // Q_irand(0, 1)
        }
        return true;
    }
    // --- nur ohne Schluessel ("if ( !self->NPC_type )") -----------------
    if (k == "NPC_Vehicle") {   // :2101
        if (b.frei()) { b.setze("swoop"); }
        return true;
    }
    if (k == "NPC_Jedi") {   // :2508
        if (!b.frei()) { return true; }
        if (b.f(4)) {
            // Q_irand(0, 11); trifft die Wahl g_char_model, wuerfelt die
            // Engine neu (strstr) - die Vorschau nimmt die erste.
            b.zufall({"jedi_hf1", "jedi_hf2", "jedi_hm1", "jedi_hm2", "jedi_kdm1", "jedi_kdm2", "jedi_rm1", "jedi_rm2",
                      "jedi_tf1", "jedi_tf2", "jedi_zf1", "jedi_zf2"});
        } else if (b.f(2)) {
            b.setze("jedimaster");
        } else if (b.f(1)) {
            b.setze("jeditrainer");
        } else {
            b.zufall({"Jedi", "Jedi2"});
        }
        return true;
    }
    if (k == "NPC_Prisoner") {   // :2601
        if (b.frei()) {
            if (b.f(1)) {
                b.zufall({"elder", "elder2"});
            } else {
                b.zufall({"Prisoner", "Prisoner2"});
            }
        }
        return true;
    }
    if (k == "NPC_Rebel") {   // :2652
        if (b.frei()) { b.setze("Rebel"); }
        return true;
    }
    if (k == "NPC_Human_Merc") {   // :2684 - "message" (ein Schluessel) zuerst
        if (b.frei()) {
            b.setze(b.message ? "human_merc_key"
                    : b.f(1)  ? "human_merc_bow"
                    : b.f(2)  ? "human_merc_rep"
                    : b.f(4)  ? "human_merc_flc"
                    : b.f(8)  ? "human_merc_cnc"
                              : "human_merc");
        }
        return true;
    }
    if (k == "NPC_RocketTrooper") {   // :2814
        if (b.frei()) { b.setze(b.f(1) ? "rockettrooper2Officer" : "rockettrooper2"); }
        return true;
    }
    if (k == "NPC_HazardTrooper") {   // :2842
        if (b.frei()) {
            b.setze(b.f(1) ? "hazardtrooperofficer" : b.f(2) ? "hazardtrooperconcussion" : "hazardtrooper");
        }
        return true;
    }
    if (k == "NPC_Ugnaught") {   // :2874
        if (b.frei()) { b.zufall({"Ugnaught", "Ugnaught2"}); }
        return true;
    }
    if (k == "NPC_Jawa") {   // :2900
        if (b.frei()) { b.setze(b.f(1) ? "jawa_armed" : "jawa"); }
        return true;
    }
    if (k == "NPC_Gran") {   // :2928
        if (b.frei()) {
            if (b.f(1)) {
                b.setze("granshooter");
            } else if (b.f(2)) {
                b.setze("granboxer");
            } else {
                b.zufall({"gran", "gran2"});
            }
        }
        return true;
    }
    if (k == "NPC_Rodian") {   // :2965
        if (b.frei()) { b.setze(b.f(1) ? "rodian2" : "rodian"); }
        return true;
    }
    if (k == "NPC_Weequay") {   // :2989
        if (b.frei()) { b.zufall({"Weequay", "Weequay2", "Weequay3", "Weequay4"}); }
        return true;
    }
    if (k == "NPC_Trandoshan") {   // :3020
        if (b.frei()) { b.setze("Trandoshan"); }
        return true;
    }
    if (k == "NPC_Tusken") {   // :3037
        if (b.frei()) { b.setze(b.f(1) ? "tuskensniper" : "tusken"); }
        return true;
    }
    if (k == "NPC_Noghri") {   // :3061
        if (b.frei()) { b.setze("noghri"); }
        return true;
    }
    if (k == "NPC_SwampTrooper") {   // :3079
        if (b.frei()) { b.setze(b.f(1) ? "SwampTrooper2" : "SwampTrooper"); }
        return true;
    }
    if (k == "NPC_Imperial") {   // :3111 (MD com_outcast 1: *_jk2)
        if (b.frei()) { b.setze(b.f(1) ? "ImpOfficer" : b.f(2) ? "ImpCommander" : "Imperial"); }
        return true;
    }
    if (k == "NPC_BespinCop") {   // :3153 - "!Q_irand(0, 1)" -> BespinCop bei 0
        if (b.frei()) { b.zufall({"BespinCop", "BespinCop2"}); }
        return true;
    }
    if (k == "NPC_Reborn" || k == "NPC_Colombian_Soldier" || k == "NPC_Colombian_Rebel") {   // :3187
        if (b.frei()) {
            b.setze(b.f(1)   ? "rebornforceuser"
                    : b.f(2) ? "rebornfencer"
                    : b.f(4) ? "rebornacrobat"
                    : b.f(8) ? "rebornboss"
                             : "reborn");
        }
        return true;
    }
    if (k == "NPC_Reborn_New") {   // :3232
        if (b.frei()) {
            if (b.f(8)) {
                b.setze(b.f(1) ? "RebornMasterDual" : b.f(2) ? "RebornMasterStaff" : "RebornMaster");
            } else if (b.f(4)) {
                b.setze(b.f(1) ? "reborn_dual2" : b.f(2) ? "reborn_staff2" : "reborn_new2");
            } else {
                b.setze(b.f(1) ? "reborn_dual" : b.f(2) ? "reborn_staff" : "reborn_new");
            }
        }
        return true;
    }
    if (k == "NPC_Cultist_Saber") {
        cultistSaber(b, false);
        return true;
    }
    if (k == "NPC_Cultist_Saber_Powers") {
        cultistSaber(b, true);
        return true;
    }
    if (k == "NPC_Cultist") {   // :3445
        if (b.frei()) {
            if (b.f(1)) {
                // SABER: spawnflags = 0, dann zufaellig 1/2/4 und vielleicht
                // 8, weiter mit SP_NPC_Cultist_Saber. Flag 4 faellt dort ins
                // letzte else (siehe cultistSaber).
                b.zufall({"cultist_saber_med", "cultist_saber_med_throw", "cultist_saber_strong",
                          "cultist_saber_strong_throw", "cultist_saber", "cultist_saber_throw"});
            } else {
                b.setze(b.f(2) ? "cultist_grip" : b.f(4) ? "cultist_lightning" : b.f(8) ? "cultist_drain" : "cultist");
            }
        }
        return true;
    }
    if (k == "NPC_Cultist_Commando") {   // :3505
        if (b.frei()) { b.setze("cultistcommando"); }
        return true;
    }
    if (k == "NPC_ShadowTrooper" || k == "NPC_Colombian_EmplacedGunner") {   // :3538
        if (b.frei()) { b.zufall({"ShadowTrooper", "ShadowTrooper2"}); }
        return true;
    }
    if (k == "NPC_Saboteur") {   // :3567
        if (b.frei()) {
            b.setze(b.f(1) ? "saboteursniper" : b.f(2) ? "saboteurpistol" : b.f(4) ? "saboteurcommando" : "saboteur");
        }
        return true;
    }
    if (k == "NPC_Droid_Assassin") {   // :4045
        if (b.frei()) { b.setze("assassin_droid"); }
        return true;
    }
    if (k == "NPC_Droid_Saber") {   // :4062
        if (b.frei()) { b.setze(b.f(1) ? "saber_droid_training" : "saber_droid"); }
        return true;
    }
    return false;
}

// Nur Movie Duels (g_spawn.cpp:881 ff., 994 ff. MD).
bool nurMovieDuels(const std::string& k, Bau& b) {
    if (k == "NPC_Projection_far") { b.setze("projection_far"); return true; }      // :3235 MD
    if (k == "NPC_Projection_near") { b.setze("projection_near"); return true; }    // :3242 MD
    if (k == "NPC_Projection_close") { b.setze("projection_close"); return true; }  // :3249 MD
    if (k == "NPC_Galak") {   // :3450 MD - in OpenJK leer
        b.setze(b.f(1) ? "Galak_Mech" : "Galak");
        return true;
    }
    // :3714 ff. MD - ueberschreiben den Schluessel.
    if (k == "NPC_md_lightside_jedi1_random") { b.zufall(kLightsideJedi1); return true; }
    if (k == "NPC_md_lightside_jedi2_random") { b.zufall(kLightsideJedi2); return true; }
    if (k == "NPC_md_lightside_gunners_random") { b.zufall(kLightsideGunners); return true; }
    if (k == "NPC_md_darkside_sith1_random") { b.zufall(kDarksideSith1); return true; }
    if (k == "NPC_md_darkside_sith2_random") { b.zufall(kDarksideSith2); return true; }
    if (k == "NPC_md_darkside_gunners_random") { b.zufall(kDarksideGunners); return true; }
    return false;
}

}  // namespace

NpcKlassenTyp npcKlassenTyp(const std::string& classname, int spawnflags, const std::string& schluesselTyp,
                            bool hatMessage, bool movieDuels) {
    NpcKlassenTyp r;
    r.typ = schluesselTyp;
    Bau b{schluesselTyp, spawnflags, hatMessage, r};
    if (classname == "NPC_spawner") {   // SP_NPC_spawner: der Schluessel oder nichts
        r.bekannt = true;
        r.spawnt = true;
        return r;
    }
    if (classname == "NPC_Galak" && !movieDuels) {
        // OpenJK: "void SP_NPC_Galak( gentity_t *self) { }" (:2350) - kein
        // SP_NPC_spawner, es erscheint niemand.
        r.bekannt = true;
        r.spawnt = false;
        r.typ.clear();
        return r;
    }
    if (gemeinsam(classname, b) || (movieDuels && nurMovieDuels(classname, b))) {
        r.bekannt = true;
        r.spawnt = true;
        return r;
    }
    // Kein Eintrag in spawns[]: G_CallSpawn findet keine Funktion.
    r.typ.clear();
    return r;
}

NpcKlassenTyp npcKlassenTyp(const MapEntity& e, bool movieDuels) {
    const std::string* t = e.find("NPC_type");
    const std::string* sf = e.find("spawnflags");
    const std::string* msg = e.find("message");
    return npcKlassenTyp(e.classname, sf != nullptr ? std::atoi(sf->c_str()) : 0, t != nullptr ? *t : std::string(),
                         msg != nullptr && !msg->empty(), movieDuels);
}

NpcKlassenTyp npcKlassenTyp(const MapEntity& e) { return npcKlassenTyp(e, regelwerkMovieDuels()); }

std::string effektiverNpcTyp(const MapEntity& e) {
    NpcKlassenTyp k = npcKlassenTyp(e);
    return k.spawnt ? k.typ : std::string();
}

bool npcSpawntBeimKartenstart(const MapEntity& e) {
    if (!e.targetname.empty()) { return false; }
    const NpcKlassenTyp k = npcKlassenTyp(e);
    return k.spawnt && !k.typ.empty();
}

}  // namespace bhed
