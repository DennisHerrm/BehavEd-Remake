// ausruestung.cpp - Waffe und Lichtschwerter einer Figur ueber die Zeit
//
// Was hier steht, bildet vier Stellen der Engine nach:
//
//   NPC_ParseParms / NPC_Begin      die Ausstattung beim Spawnen
//                                   (NPC_stats.cpp, NPC_spawn.cpp)
//   Q3_Set SET_WEAPON, SET_SABER*   die Skriptbefehle (Q3_Interface.cpp,
//                                   wp_saberLoad.cpp)
//   G_CreateG2AttachedWeaponModel   was davon in der Hand haengt
//                                   (wp_saber.cpp:729 ff.)
//   CG_Player, Klingenblock         wie lang jede Klinge gerade ist
//                                   (cg_players.cpp:16200 ff.)
//
// Die Zeichnung selbst macht die Oberflaeche (gui/app_view3d.cpp); hier
// entsteht nur der Zustand, den sie braucht - und den die Proben pruefen
// (tests/waffentest.cpp).
//
// NICHT nachgebildet, mit Absicht oder weil es fuer die Skripte der Mod
// nichts aendert:
//
//   * Holster. Haelt eine Figur eine andere Waffe, haengt die Engine ihr
//     Schwert an die Huefte (WP_SaberAddHolsteredG2SaberModels,
//     wp_saber.cpp:1073 ff.) - aber nur mit ps.weapons[WP_SABER], und
//     G_SetWeapon leert diese Liste bei NPCs. Nach SET_SABER1/2 ohne
//     Schwert in der Hand wandert der Griff dorthin; hier verschwindet er.
//   * customSkin eines Griffs (.sab, 3 Eintraege in der Mod).
//   * Das Aussehen der Klinge: die Mod zeichnet je Farbe und Stil eigene
//     Shader (Ep1-Glanz, RGB eingefaerbt, schwarz, weiss; cg_players.cpp:
//     7640 ff.), die Klinge wird beim Zuenden schmaler (radiusmult,
//     ebenda:7825) und wirft Licht. behaved nimmt die sechs Farbbilder,
//     eine RGB-Farbe bekommt das naechste davon.
//   * SS_FAST zuendet doppelt so schnell (siehe klingenLaenge).
//   * "drop": die fortgeworfene Waffe liegt nicht am Boden.
//   * Linke Waffe nach NPC_class (REBORN ab RANK_LT_COMM mit
//     WP_BLASTER_PISTOL, Calo Nord mit WP_REBELBLASTER; NPC_spawn.cpp:716
//     ff.) - die Klasse steht nicht in NpcDef.
//   * tag_hack fuer SABER_ARC, SABER_STAR, SABER_TRIDENT.
//   * Klaenge: nur SET_SABERACTIVE spielt An/Aus (Zeitleiste). Dass die
//     Engine soundOn auch beim Zuenden durch SET_SABER1/2 und BLADEON
//     spielt (cg_players.cpp:16263), fehlt; den Summton gibt es als
//     ActorState::saberLoop, gespielt wird er noch nicht.
//   * Die Muendung eines Schusses kommt weiter aus dem Ursprung der Figur
//     (muzzlePoint), nicht vom Bolzen "*flash" des Waffenmodells.
#include "bhed/scene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace bhed {
namespace {

std::string klein(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string gross(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string ohneRand(const std::string& s) {
    std::size_t a = 0;
    std::size_t e = s.size();
    while (a < e && (s[a] == ' ' || s[a] == '\t')) { ++a; }
    while (e > a && (s[e - 1] == ' ' || s[e - 1] == '\t')) { --e; }
    return s.substr(a, e - a);
}

// FNV-1a, 32 Bit. Nur fuer die Wahl einer "random"-Farbe - es muss nicht
// gut streuen, nur bei gleicher Saat gleich ausfallen, auf jedem Rechner.
std::uint32_t streuwert(const std::string& s) {
    std::uint32_t h = 2166136261U;
    for (const char c : s) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619U;
    }
    return h;
}

bool beginntMit(const std::string& s, const char* vorsatz) {
    const std::string v = vorsatz;
    return s.compare(0, v.size(), v) == 0;
}

// Die Laenge einer Klinge zur Zeit ms (siehe klingenLaenge in scene.h).
// `haelt` false: die Laenge steht, denn der Klingenblock der Engine laeuft
// nur fuer ps.weapon == WP_SABER.
float laengeZu(const KlingenZustand& k, double ms, bool haelt) {
    if (!haelt) {
        return k.laengeDann;
    }
    const double dt = std::max(0.0, ms - k.seitMs);
    // lengthMax/10 * frametime/100 = lengthMax/1000 je Millisekunde
    // (cg_players.cpp:16306 ff.; zurueck ebenso, ebenda:16233 ff.).
    const double schritt = static_cast<double>(k.max) / 1000.0 * dt;
    // "!active || length > lengthMax" schrumpft (ebenda:16218).
    if (k.an && k.laengeDann <= k.max) {
        return static_cast<float>(
            std::min(static_cast<double>(k.max),
                     static_cast<double>(k.laengeDann) + schritt));
    }
    return static_cast<float>(
        std::max(0.0, static_cast<double>(k.laengeDann) - schritt));
}

// Eine Klinge an- oder ausschalten, ohne dass die Laenge springt.
void schalte(KlingenZustand& k, bool an, double ms, bool haelt) {
    k.laengeDann = laengeZu(k, ms, haelt);
    k.seitMs = ms;
    k.an = an;
}

template <typename F>
void jedeKlinge(ActorState& st, F&& f) {
    for (SaberZustand& s : st.saber) {
        for (KlingenZustand& k : s.klinge) {
            f(k);
        }
    }
}

// Die Laengen einfrieren: die Figur legt das Schwert weg.
void frieren(ActorState& st, double ms) {
    jedeKlinge(st, [ms](KlingenZustand& k) {
        k.laengeDann = laengeZu(k, ms, true);
        k.seitMs = ms;
    });
}

// Weiterwachsen ab jetzt: die Figur nimmt das Schwert wieder.
void fortsetzen(ActorState& st, double ms) {
    jedeKlinge(st, [ms](KlingenZustand& k) { k.seitMs = ms; });
}

// WP_SaberInitBladeData (wp_saber.cpp:2189 ff.): ALLE Klingen BEIDER
// Schwerter auf Laenge null - die Schalter bleiben, wie sie sind.
void nullen(ActorState& st, double ms) {
    jedeKlinge(st, [ms](KlingenZustand& k) {
        k.laengeDann = 0.0F;
        k.seitMs = ms;
    });
}

// ps.SaberActive() (q_shared.h:1898).
bool irgendeineAn(const ActorState& st) {
    const auto an = [](const SaberZustand& s) {
        for (int b = 0; b < s.numBlades && b < kMaxKlingen; ++b) {
            if (s.klinge[static_cast<std::size_t>(b)].an) {
                return true;
            }
        }
        return false;
    };
    return an(st.saber[0]) || (st.dualSabers && an(st.saber[1]));
}

// Ein Schwertfach mit einer Art belegen (WP_SaberParseParms). Die Schalter
// der Klingen bleiben: WP_SaberSetDefaults fasst blade[i].active nicht an.
void belege(const Actor& a, SaberZustand& s, int art) {
    s.art = art;
    if (art < 0 || static_cast<std::size_t>(art) >= a.saberArten.size()) {
        s.art = -1;
        s.numBlades = 0;
        return;
    }
    const SaberArt& d = a.saberArten[static_cast<std::size_t>(art)];
    s.numBlades = d.numBlades;
    for (std::size_t b = 0; b < s.klinge.size(); ++b) {
        s.klinge[b].max = d.klinge[b].max;
        s.klinge[b].radius = d.klinge[b].radius;
        s.klinge[b].farbe = d.klinge[b].farbe;
    }
}

// Welcher Griff haengt fuer saber[n] in der Hand? Die Nullstruktur hat kein
// Modell, und WP_SaberAddG2SaberModels nimmt dann DEFAULT_SABER_MODEL
// (wp_saber.cpp:877 ff.) - der steht immer an Stelle 0 von Actor::modelle.
int griffModell(const Actor& a, const SaberZustand& s) {
    if (s.art >= 0 && static_cast<std::size_t>(s.art) < a.saberArten.size()) {
        const int m = a.saberArten[static_cast<std::size_t>(s.art)].modell;
        if (m >= 0) {
            return m;
        }
    }
    return a.modelle.empty() ? -1 : 0;
}

// WP_SaberAddG2SaberModels (wp_saber.cpp:805 ff.): saber[0] rechts, saber[1]
// links, aber nur mit ps.dualSabers.
void griffeAnhaengen(const Actor& a, ActorState& st, int nurFach) {
    for (int n = 0; n < 2; ++n) {
        if (nurFach >= 0 && n != nurFach) {
            continue;
        }
        if (n == 1 && !st.dualSabers) {
            break;
        }
        const auto f = static_cast<std::size_t>(n);
        st.hand[f].modell = griffModell(a, st.saber[f]);
        st.hand[f].istSaber = true;
    }
}

// Q3_SetSaberActive und Q3_SetSaberBladeActive beginnen gleich
// (Q3_Interface.cpp:6862 ff., 6929 ff.): haelt die Figur kein Schwert, hat
// es aber (ps.weapons[WP_SABER]), wird umgeschaltet - NUR ps.weapon, das
// Modell bleibt, wie es ist. Hat sie keins, bricht die Engine mit einer
// Fehlermeldung ab.
bool zumSchwert(ActorState& st, double ms) {
    if (haeltSaber(st)) {
        return true;
    }
    if (!st.hatSaber) {
        return false;
    }
    st.waffe = "WP_SABER";
    fortsetzen(st, ms);
    return true;
}

bool istKeinSchwert(const std::string& name) {
    const std::string k = klein(ohneRand(name));
    return k == "none" || k == "remove";
}

}  // namespace

// --- Oeffentlich (scene.h) ----------------------------------------------

std::string waffenWeltmodell(const std::string& weaponMdl) {
    if (weaponMdl.empty()) {
        return kStandardGriff;
    }
    std::string w = weaponMdl;
    // Q_strncpyz(weapon_model, ps_weapon_model, 64): laenger schneidet die
    // Engine ab. Die laengsten Pfade in weapons.dat der Mod haben 58.
    if (w.size() > 63U) {
        w.resize(63U);
    }
    // strstr unterscheidet Gross und Klein - ".MD3" bliebe stehen.
    const std::size_t md3 = w.find(".md3");
    if (md3 == std::string::npos) {
        return w;
    }
    w.resize(md3);
    // strstr(weapon_model, "_w") durchsucht den GANZEN Pfad, nicht nur den
    // Dateinamen - so steht es dort.
    if (w.find("_w") == std::string::npos && w.find("noweap") == std::string::npos) {
        w += "_w";
    }
    w += ".glm";
    return w;
}

std::string saberFarbeAufloesen(const std::string& name, const std::string& saat) {
    const std::string k = klein(ohneRand(name));
    // SABER_ORANGE .. SABER_PURPLE (q_shared.h:335 ff.).
    static const char* const kZufall[] = {"orange", "yellow", "green", "blue", "purple"};
    static const char* const kPrequel[] = {"green", "blue"};
    if (k.empty() || k == "random") {
        return kZufall[streuwert(saat) % 5U];
    }
    if (k == "prequel_random") {
        return kPrequel[streuwert(saat) % 2U];
    }
    return k;
}

bool saberFarbeRgb(const std::string& farbe, float rgb[3]) {
    const std::string k = klein(ohneRand(farbe));
    static const char* const kBenannt[] = {"red",   "orange",       "yellow", "green",
                                           "blue",  "purple",       "unstable_red",
                                           "black", "white",        "rgb",    "custom"};
    for (const char* n : kBenannt) {
        if (k == n) {
            return false;
        }
    }
    if (k.empty()) {
        return false;
    }
    const char c = k[0];
    if (c >= 'a' && c < 'u') {
        // Ein Farbton in 24 Stufen (ebenda:669 ff.), Wort fuer Wort.
        const int deg = (c - 'a') * 360 / 24;
        const double winkel = static_cast<double>(deg % 120) * 3.14159265358979323846 / 180.0;
        const double v = (std::cos(winkel) / std::cos(3.14159265358979323846 / 3.0 - winkel) + 1.0) / 3.0;
        const auto fv = static_cast<float>(v);
        if (deg <= 120) {
            rgb[0] = fv; rgb[1] = 1.0F - fv; rgb[2] = 0.0F;
        } else if (deg <= 240) {
            rgb[0] = 0.0F; rgb[1] = fv; rgb[2] = 1.0F - fv;
        } else {
            rgb[0] = 1.0F - fv; rgb[1] = 0.0F; rgb[2] = fv;
        }
        return true;
    }
    // u..z: sechs Hexziffern dahinter.
    int wert = 0;
    for (std::size_t i = 0; i < 6; ++i) {
        const char h = (i + 1 < k.size()) ? k[i + 1] : '\0';
        int ziffer = -1;
        if (h >= '0' && h <= '9') {
            ziffer = h - '0';
        } else if (h >= 'a' && h <= 'f') {
            ziffer = 10 + (h - 'a');
        }
        if (ziffer < 0) {
            rgb[0] = rgb[1] = rgb[2] = 1.0F;
            return true;
        }
        if ((i & 1U) != 0U) {
            wert |= ziffer;
            rgb[i >> 1U] = static_cast<float>(wert) / 255.0F;
        } else {
            wert = ziffer << 4;
        }
    }
    return true;
}

std::string npcTeamWaffe(const std::string& team, const std::string& npcType,
                         int spawnflags) {
    // TeamTable (NPC_stats.cpp:128 ff.) kennt "enemy" und "TEAM_ENEMY".
    std::string t = klein(ohneRand(team));
    if (beginntMit(t, "team_")) {
        t = t.substr(5);
    }
    const std::string n = klein(npcType);
    // Die Liste aus NPC_WeaponsForTeam (NPC_spawn.cpp:1342 ff.), in ihrer
    // Reihenfolge. Wo sie ZWEI Waffen gibt, waehlt NPC_SetWeapons die
    // hoehere und MELEE nur, wenn sonst nichts da ist (ebenda:1560 ff.) -
    // das Ergebnis steht gleich hier.
    if (t == "enemy") {
        if (n == "tavion" || beginntMit(n, "reborn") || n == "desann" ||
            beginntMit(n, "shadowtrooper")) {
            return "WP_SABER";
        }
        if (beginntMit(n, "stofficer")) { return "WP_FLECHETTE"; }
        if (n == "stcommander" || n == "stcommander_jk2") { return "WP_REPEATER"; }
        if (n == "swamptrooper") { return "WP_FLECHETTE"; }
        if (n == "swamptrooper2") { return "WP_REPEATER"; }
        if (n == "rockettrooper") { return "WP_ROCKET_LAUNCHER"; }
        if (n == "imperial" || beginntMit(n, "impworker") || n == "stormpilot") {
            return "WP_BLASTER_PISTOL";
        }
        if (n == "galak") { return "WP_BLASTER"; }
        if (n == "galak_mech") { return "WP_REPEATER"; }
        if (beginntMit(n, "ugnaught")) { return "WP_NONE"; }
        if (n == "granshooter") { return "WP_BLASTER"; }
        if (n == "granboxer") { return "WP_MELEE"; }
        if (beginntMit(n, "gran")) { return "WP_THERMAL"; }   // THERMAL|MELEE
        if (n == "rodian") { return "WP_DISRUPTOR"; }
        if (n == "rodian2") { return "WP_BLASTER"; }
        if (n == "interrogator" || n == "sentry" || beginntMit(n, "protocol")) {
            return "WP_NONE";
        }
        if (beginntMit(n, "weequay")) { return "WP_BOWCASTER"; }
        if (n == "impofficer" || n == "impcommander") { return "WP_BLASTER"; }
        if (n == "probe" || n == "seeker" || beginntMit(n, "remote")) {
            return "WP_BOT_LASER";
        }
        if (n == "trandoshan") { return "WP_REPEATER"; }
        if (n == "atst") { return "WP_ATST_SIDE"; }   // MAIN|SIDE, SIDE ist hoeher
        if (n == "mark1" || n == "mark2") { return "WP_BOT_LASER"; }
        if (n == "minemonster" || n == "howler") { return "WP_MELEE"; }
        return "WP_BLASTER";   // "Stormtroopers, etc."
    }
    if (t == "player") {
        constexpr int kSfbRifleman = 2;   // SFB_RIFLEMAN, b_local.h:156
        constexpr int kSfbPhaser = 4;     // SFB_PHASER,   b_local.h:157
        if ((spawnflags & kSfbRifleman) != 0) { return "WP_REPEATER"; }
        if ((spawnflags & kSfbPhaser) != 0) { return "WP_BLASTER_PISTOL"; }
        if (beginntMit(n, "jedi") || n == "luke") { return "WP_SABER"; }
        if (beginntMit(n, "prisoner") || beginntMit(n, "elder")) { return "WP_NONE"; }
        if (beginntMit(n, "bespincop")) { return "WP_BLASTER_PISTOL"; }
        if (n == "monmothma" || n == "md_grogu") { return "WP_NONE"; }
        return "WP_BLASTER";   // "rebel"
    }
    // TEAM_NEUTRAL nennt nur Ausnahmen, die alle WP_NONE ergeben; alles
    // andere (auch TEAM_FREE, die Vorgabe) faellt durch zu WP_NONE.
    return "WP_NONE";
}

bool haeltSaber(const ActorState& st) noexcept {
    return st.waffe.empty() || st.waffe == "WP_SABER";
}

float klingenLaenge(const ActorState& st, int saberNr, int klinge, double ms) noexcept {
    if (saberNr < 0 || saberNr > 1 || klinge < 0 || klinge >= kMaxKlingen) {
        return 0.0F;
    }
    const SaberZustand& s = st.saber[static_cast<std::size_t>(saberNr)];
    if (klinge >= s.numBlades) {
        return 0.0F;
    }
    return laengeZu(s.klinge[static_cast<std::size_t>(klinge)], ms, haeltSaber(st));
}

int modellIndex(Actor& a, const std::string& pfad) {
    if (pfad.empty()) {
        return -1;
    }
    for (std::size_t i = 0; i < a.modelle.size(); ++i) {
        if (a.modelle[i] == pfad) {
            return static_cast<int>(i);
        }
    }
    a.modelle.push_back(pfad);
    return static_cast<int>(a.modelle.size() - 1);
}

int saberArtIndex(Actor& a, const std::string& name, int fach,
                  const SzenenHilfe* hilfe) {
    const std::string schluessel = klein(ohneRand(name));
    for (std::size_t i = 0; i < a.saberArten.size(); ++i) {
        if (a.saberArten[i].fach == fach && klein(a.saberArten[i].name) == schluessel) {
            return static_cast<int>(i);
        }
    }
    // Erst die Vorgaben (WP_SaberSetDefaults, wp_saberLoad.cpp:620 ff.),
    // dann darueber, was die .sab-Datei sagt - genau die Reihenfolge von
    // WP_SaberParseParms.
    SaberArt art;
    art.name = ohneRand(name);
    art.fach = fach;
    art.typ = "SABER_SINGLE";
    art.soundOn = "sound/weapons/saber/enemy_saber_on.wav";
    art.soundLoop = "sound/weapons/saber/saberhum3.wav";
    art.soundOff = "sound/weapons/saber/enemy_saber_off.wav";
    std::string griff = kStandardGriff;
    const std::string saat = a.name + "|" + schluessel + "|" + std::to_string(fach);
    for (std::size_t b = 0; b < art.klinge.size(); ++b) {
        // Jede Klinge fuer sich zufaellig - so wuerfelt die Vorgabe
        // (Q_irand je Klinge, ebenda:626).
        art.klinge[b].farbe = saberFarbeAufloesen("", saat + "|" + std::to_string(b));
    }
    if (!schluessel.empty() && hilfe != nullptr && hilfe->sabers != nullptr) {
        const auto it = hilfe->sabers->find(schluessel);
        if (it != hilfe->sabers->end()) {
            const SaberDef& d = it->second;
            art.gefunden = true;
            if (!d.model.empty()) { griff = d.model; }
            if (!d.type.empty()) { art.typ = d.type; }
            if (!d.soundOn.empty()) { art.soundOn = d.soundOn; }
            if (!d.soundLoop.empty()) { art.soundLoop = d.soundLoop; }
            if (!d.soundOff.empty()) { art.soundOff = d.soundOff; }
            art.numBlades = std::clamp(d.numBlades, 1, kMaxKlingen);
            art.bladeStyle2Start = d.bladeStyle2Start;
            art.zweihaendig = d.twoHanded;
            art.handgelenk = d.boltToWrist;
            art.ohneKlinge = d.noBlade;
            art.ohneKlinge2 = d.noBlade2;
            art.stilGelernt = d.stylesLearned;
            art.stilVerboten = d.stylesForbidden;
            art.stilEineKlinge = d.singleBladeStyle;
            for (std::size_t b = 0; b < art.klinge.size(); ++b) {
                if (!d.bladeColor[b].empty()) {
                    // Eine GENANNTE Farbe - auch "random" - wuerfelt die
                    // Engine EINMAL und gibt sie allen Klingen, die der
                    // Schluessel trifft (Saber_ParseSaberColor,
                    // ebenda:878 ff.). Deshalb hier ohne Klingennummer.
                    art.klinge[b].farbe = saberFarbeAufloesen(d.bladeColor[b], saat + "|farbe");
                }
                if (d.bladeLength[b] > 0.0F) { art.klinge[b].max = d.bladeLength[b]; }
                if (d.bladeRadius[b] > 0.0F) { art.klinge[b].radius = d.bladeRadius[b]; }
            }
        }
    }
    art.modell = modellIndex(a, griff);
    a.saberArten.push_back(std::move(art));
    return static_cast<int>(a.saberArten.size() - 1);
}

int waffenModellIndex(Actor& a, const std::string& waffe, bool kotor,
                      const SzenenHilfe* hilfe) {
    if (hilfe == nullptr || hilfe->waffen == nullptr) {
        return -1;
    }
    const std::string gesucht = gross(ohneRand(waffe));
    for (const auto& [typ, def] : *hilfe->waffen) {
        if (gross(typ) != gesucht) {
            continue;
        }
        // kotorWeapons nimmt altweaponmodel (NPC_spawn.cpp:679 ff.,
        // G_SetWeapon Q3_Interface.cpp:3486 ff.). com_kotor, der Schalter
        // des ganzen Spiels, steht auf 0 (common.cpp:1199).
        return modellIndex(a, waffenWeltmodell(kotor ? def.altModel : def.model));
    }
    return -1;
}

void ausstattungBeimSpawnen(Actor& a, const NpcDef* def, const SzenenHilfe* hilfe,
                            int spawnflags) {
    // Stelle 0 ist IMMER der Standardgriff - siehe griffModell.
    (void)modellIndex(a, kStandardGriff);
    a.startSaber = {};
    a.startHand = {};
    a.startDual = false;
    if (def == nullptr) {
        // Keine .npc: alles wie bisher. Ein Standardschwert, damit
        // SET_SABERACTIVE weiter eine Klinge zeigt, und keine Waffe.
        a.startWaffe.clear();
        a.startHatSaber = true;   // "haelt ein Schwert" - also hat sie eins
        const int art = saberArtIndex(a, "", 0, hilfe);
        belege(a, a.startSaber[0], art);
        return;
    }

    // --- Die Waffe (NPC_ParseParms "weapon", NPC_stats.cpp:3860) ---------
    //
    // Fehlt sie oder heisst sie WP_NONE, sucht NPC_Begin eine aus
    // (NPC_spawn.cpp:1848: "not set by the NPCs.cfg").
    std::string waffe = gross(ohneRand(def->weapon));
    if (waffe.empty() || waffe == "WP_NONE") {
        waffe = npcTeamWaffe(def->team, a.npcType, spawnflags);
    }
    a.startWaffe = waffe;
    a.startHatSaber = (waffe == "WP_SABER") || def->saberImInventar;

    // --- Die Schwerter (NPC_stats.cpp:4073 ff.) --------------------------
    const auto uebersteuern = [&a, def](SaberZustand& s, int fach) {
        const auto f = static_cast<std::size_t>(fach);
        const std::string saat = a.name + "|npc|" + std::to_string(fach);
        for (std::size_t b = 0; b < s.klinge.size(); ++b) {
            if (!def->saberFarbe[f][b].empty()) {
                s.klinge[b].farbe = saberFarbeAufloesen(def->saberFarbe[f][b], saat);
            }
            if (def->saberLaenge[f][b] > 0.0F) { s.klinge[b].max = def->saberLaenge[f][b]; }
            if (def->saberRadius[f][b] > 0.0F) { s.klinge[b].radius = def->saberRadius[f][b]; }
        }
    };
    if (!def->saber.empty()) {
        belege(a, a.startSaber[0], saberArtIndex(a, def->saber, 0, hilfe));
    }
    uebersteuern(a.startSaber[0], 0);
    // saber2 nur, wenn das erste kein Zweihaender ist; ist das ZWEITE einer,
    // entfernt die Engine es wieder (WP_RemoveSaber setzt dabei die Vorgaben
    // ein, wp_saberLoad.cpp:3008).
    const bool ersterZweihaendig =
        a.startSaber[0].art >= 0 &&
        a.saberArten[static_cast<std::size_t>(a.startSaber[0].art)].zweihaendig;
    if (!def->saber2.empty() && !ersterZweihaendig) {
        const int art = saberArtIndex(a, def->saber2, 1, hilfe);
        if (a.saberArten[static_cast<std::size_t>(art)].zweihaendig) {
            belege(a, a.startSaber[1], saberArtIndex(a, "", 1, hilfe));
        } else {
            belege(a, a.startSaber[1], art);
            a.startDual = true;
        }
    }
    uebersteuern(a.startSaber[1], 1);

    // --- Der Kampfstil (NPC_ParseParms "saberStyle", sonst die Vorgaben
    // aus WP_SaberInitBladeData, wp_saber.cpp:2233 ff.) --------------------
    //
    // Wo die Engine wuerfelt (Q_irand(SS_FAST, SS_STRONG): Kultisten,
    // Schattentruppen, alle uebrigen), nehmen wir die Mitte, SS_MEDIUM -
    // im Spiel ist es jedes Mal ein anderer.
    a.startStil = def->saberStil;
    if (a.startStil == 0) {
        const std::string k = gross(ohneRand(def->klasse));
        const std::string r = klein(ohneRand(def->rang));
        const bool feind = gross(ohneRand(def->team)) == "TEAM_ENEMY";
        // TranslateRankName: Unbekanntes und Fehlendes ist RANK_CIVILIAN.
        const bool zivil = r.empty() || r == "civilian" ||
                           (r != "crewman" && r != "ensign" && r != "ltjg" && r != "lt" &&
                            r != "ltcomm" && r != "commander" && r != "captain");
        if (k == "CLASS_DESANN" || k == "CLASS_VADER") {
            a.startStil = 4;
        } else if (k == "CLASS_TAVION" || k == "CLASS_YODA") {
            a.startStil = 5;
        } else if (k == "CLASS_ALORA") {
            a.startStil = 6;
        } else if (k == "CLASS_GALEN") {
            a.startStil = 7;
        } else if (klein(a.npcType).rfind("cultist", 0) == 0) {
            a.startStil = 2;
        } else if (feind && (zivil || r == "ltjg")) {
            a.startStil = 1;
        } else if (feind && (r == "crewman" || r == "ensign")) {
            a.startStil = 2;
        } else if (feind && k == "CLASS_SHADOWTROOPER") {
            a.startStil = 2;
        } else if (feind && r == "lt") {
            a.startStil = 3;
        } else {
            a.startStil = 2;   // Spielerfigur (Vorgabe SS_MEDIUM) und Wuerfel
        }
    }

    // --- Was in der Hand haengt (NPC_spawn.cpp:572 ff., 640 ff.) ---------
    if (waffe == "WP_SABER") {
        // Nur mit einem echten Schwert: "if ( saber[0].type != SABER_NONE )"
        // (ebenda:572). Die Nullstruktur bekommt beim Spawnen keinen Griff.
        if (a.startSaber[0].art >= 0) {
            a.startHand[0].modell = griffModell(a, a.startSaber[0]);
            a.startHand[0].istSaber = true;
            if (a.startDual) {
                a.startHand[1].modell = griffModell(a, a.startSaber[1]);
                a.startHand[1].istSaber = true;
            }
        }
    } else if (waffe != "WP_NONE") {
        const int m = waffenModellIndex(a, waffe, def->kotorWeapons, hilfe);
        a.startHand[0].modell = m;
        // Die zweite Pistole links (ebenda:740 ff.). WP_DROIDEKA haengt dort
        // nur fuer CLASS_DROIDEKA links an; ihr Modell ist ohnehin noweap.
        if (((waffe == "WP_DUAL_PISTOL" || waffe == "WP_DUAL_CLONEPISTOL") &&
             def->dualPistols) ||
            waffe == "WP_DROIDEKA") {
            a.startHand[1].modell = m;
        }
    }
}

void ausruestungSchritt(const Actor& a, ActorState& st, const ActorStep& step) {
    const double ms = step.startMs;
    const bool vorher = irgendeineAn(st);
    const auto fach = static_cast<std::size_t>(std::clamp(step.slot, 0, 1));
    switch (step.kind) {
        case ActorStep::Kind::Waffe: {
            // G_SetWeapon (Q3_Interface.cpp:3410 ff.) und Q3_SetWeapon
            // ("drop", ebenda:3528).
            const std::string& w = step.text;
            if (haeltSaber(st)) {
                frieren(st, ms);
            }
            st.hand = {};   // G_RemoveWeaponModels
            if (w == "drop" || w == "WP_NONE") {
                // WP_NONE raeumt ps.weapons NICHT - hatSaber bleibt. Bei
                // "drop" wirft die Figur ihre Waffe fort (TossClientItems);
                // das liegende Stueck zeichnet behaved nicht.
                if (w == "drop") {
                    st.hatSaber = false;
                }
                st.waffe = "WP_NONE";
                break;
            }
            // "Should NPCs have only 1 weapon at a time?" - ja: die Liste
            // wird geleert und nur die neue eingetragen (ebenda:3447 ff.).
            const bool hatteSchwert = st.hatSaber;
            st.hatSaber = (w == "WP_SABER");
            st.waffe = w;
            if (w == "WP_SABER") {
                // "if (!hadWeapon) WP_SaberInitBladeData" (ebenda:3476).
                if (hatteSchwert) {
                    fortsetzen(st, ms);
                } else {
                    nullen(st, ms);
                }
                griffeAnhaengen(a, st, -1);
            } else {
                st.hand[0].modell = step.index;
                st.hand[0].istSaber = false;
            }
            break;
        }
        case ActorStep::Kind::SaberWahl: {
            // WP_SetSaber (wp_saberLoad.cpp:3075 ff.). Ein Zweihaender im
            // zweiten Fach kommt schon als "none" an (siehe buildScene).
            if (istKeinSchwert(step.text)) {
                // WP_RemoveSaber (ebenda:3001 ff.): Vorgaben, dualSabers
                // aus, Klingen aus und auf null, Modell im Fach fort - auch
                // eine Waffe, die dort hing.
                belege(a, st.saber[fach], step.index);
                st.dualSabers = false;
                for (KlingenZustand& k : st.saber[fach].klinge) {
                    k.an = false;
                    k.laengeDann = 0.0F;
                    k.seitMs = ms;
                }
                st.hand[fach] = HandBelegung{};
                break;
            }
            // "if (ent->weaponModel[saberNum] > 0) remove" - auch hier
            // trifft es eine Waffe, die gerade in der rechten Hand haengt.
            st.hand[fach] = HandBelegung{};
            belege(a, st.saber[fach], step.index);
            nullen(st, ms);
            if (fach == 1) {
                st.dualSabers = true;
            }
            if (haeltSaber(st)) {
                // Haelt sie das Schwert, haengt der neue Griff sofort in der
                // Hand und ZUENDET: SetLength(0) und Activate() (ebenda:3113
                // ff.). Sonst wandert er ans Holster - das zeichnet behaved
                // nicht (siehe den Kopf dieser Datei).
                griffeAnhaengen(a, st, static_cast<int>(fach));
                for (KlingenZustand& k : st.saber[fach].klinge) {
                    k.an = true;
                    k.laengeDann = 0.0F;
                    k.seitMs = ms;
                }
            }
            break;
        }
        case ActorStep::Kind::SaberFarbe: {
            // WP_SaberSetColor: nur die Farbe, an welcher Waffe auch immer.
            if (step.index >= 0 && step.index < kMaxKlingen) {
                st.saber[fach].klinge[static_cast<std::size_t>(step.index)].farbe = step.text;
            }
            break;
        }
        case ActorStep::Kind::KlingeSchalter: {
            if (!zumSchwert(st, ms)) {
                break;
            }
            // SaberBladeActivate (q_shared.h:1939): saber[1] nur mit
            // dualSabers, und die Klinge muss es geben.
            if (fach == 1 && !st.dualSabers) {
                break;
            }
            SaberZustand& s = st.saber[fach];
            if (step.index < 0 || step.index >= s.numBlades) {
                break;
            }
            schalte(s.klinge[static_cast<std::size_t>(step.index)], step.value > 0.5F, ms,
                    true);
            break;
        }
        case ActorStep::Kind::Saber: {
            if (!zumSchwert(st, ms)) {
                break;
            }
            const bool an = step.value > 0.5F;
            // SaberActivate: saber[0], saber[1] nur mit dualSabers.
            // SaberDeactivate: BEIDE (q_shared.h:1949 ff.).
            for (std::size_t n = 0; n < st.saber.size(); ++n) {
                if (an && n == 1 && !st.dualSabers) {
                    break;
                }
                SaberZustand& s = st.saber[n];
                for (int b = 0; b < s.numBlades && b < kMaxKlingen; ++b) {
                    schalte(s.klinge[static_cast<std::size_t>(b)], an, ms, true);
                }
            }
            break;
        }
        case ActorStep::Kind::HandModell: {
            // Q3_AddRHandModel & Co. (Q3_Interface.cpp:6321 ff.). Beide
            // Seiten teilen EIN Fach, ent->cinematicModel. Ein zweites Add
            // vergisst das erste, ENTFERNT es aber nicht - es haengt weiter
            // an der Hand. Und Remove nimmt das zuletzt angehaengte, gleich
            // welche Seite der Befehl nennt.
            if (step.value > 0.5F) {
                if (step.index < 0) {
                    // G2API_InitGhoul2Model schlaegt fehl: cinematicModel
                    // wird -1, ein spaeteres Remove tut nichts.
                    st.kinoLetztes = -1;
                    break;
                }
                int frei = -1;
                for (int i = 0; i < kMaxKinoModelle; ++i) {
                    if (st.kino[static_cast<std::size_t>(i)].modell < 0) {
                        frei = i;
                        break;
                    }
                }
                if (frei < 0) {
                    frei = (st.kinoLetztes + 1 + kMaxKinoModelle) % kMaxKinoModelle;
                }
                st.kino[static_cast<std::size_t>(frei)] = KinoModell{step.index, step.slot == 1};
                st.kinoLetztes = frei;
            } else if (st.kinoLetztes >= 0) {
                // Nicht nachgebildet: vor dem ersten Add steht
                // cinematicModel auf 0 (der Speicher ist geloescht, nichts
                // setzt es), und Remove wuerde Ghoul2-Modell 0 entfernen -
                // die Figur selbst. Hier tut ein solches Remove nichts.
                st.kino[static_cast<std::size_t>(st.kinoLetztes)].modell = -1;
            }
            break;
        }
        default:
            break;
    }
    const bool nachher = irgendeineAn(st);
    if (nachher != vorher) {
        st.saberSinceMs = ms;
    }
    st.saberActive = nachher;
}

namespace {

const SaberArt* artVon(const Actor& a, const ActorState& st, int n) {
    const int art = st.saber[static_cast<std::size_t>(n)].art;
    if (art < 0 || static_cast<std::size_t>(art) >= a.saberArten.size()) {
        return nullptr;
    }
    return &a.saberArten[static_cast<std::size_t>(art)];
}

bool schwertAn(const ActorState& st, int n) {
    const SaberZustand& s = st.saber[static_cast<std::size_t>(n)];
    for (int b = 0; b < s.numBlades && b < kMaxKlingen; ++b) {
        if (s.klinge[static_cast<std::size_t>(b)].an) {
            return true;
        }
    }
    return false;
}

constexpr int kSsFast = 1;
constexpr int kSsTavion = 5;
constexpr int kSsDual = 6;
constexpr int kSsZahl = 8;   // SS_NUM_SABER_STYLES

// ps.SaberLength() > 0 - irgendeine Klinge hat Laenge.
bool klingeHatLaenge(const ActorState& st, double ms) {
    for (int n = 0; n < 2; ++n) {
        if (n == 1 && !st.dualSabers) {
            break;
        }
        for (int b = 0; b < kMaxKlingen; ++b) {
            if (klingenLaenge(st, n, b, ms) > 0.0F) {
                return true;
            }
        }
    }
    return false;
}

// Wann die letzte eingefahrene Klinge die Laenge 0 erreicht hat.
double eingefahrenAb(const ActorState& st) {
    double bis = -1.0e9;
    for (int n = 0; n < 2; ++n) {
        if (n == 1 && !st.dualSabers) {
            break;
        }
        const SaberZustand& s = st.saber[static_cast<std::size_t>(n)];
        for (int b = 0; b < s.numBlades && b < kMaxKlingen; ++b) {
            const KlingenZustand& k = s.klinge[static_cast<std::size_t>(b)];
            if (!k.an && k.laengeDann > 0.0F && k.max > 0.0F) {
                bis = std::max(bis, k.seitMs + static_cast<double>(k.laengeDann) * 1000.0 /
                                                   static_cast<double>(k.max));
            }
        }
    }
    return bis;
}

}  // namespace

const char* saberHaltungFuerStil(int stil) noexcept {
    switch (stil) {
        case 1: return "BOTH_SABERFAST_STANCE";
        case 3: return "BOTH_SABERSLOW_STANCE";
        case 4: return "BOTH_SABERDESANN_STANCE";
        case 5: return "BOTH_SABERTAVION_STANCE";
        case 6: return "BOTH_SABERDUAL_STANCE";
        case 7: return "BOTH_SABERSTAFF_STANCE";
        default: return "BOTH_STAND2";
    }
}

int saberStilJetzt(const Actor& a, const ActorState& st) noexcept {
    const SaberArt* s0 = artVon(a, st, 0);
    const SaberArt* s1 = artVon(a, st, 1);
    const bool an0 = schwertAn(st, 0);
    const bool an1 = st.dualSabers && schwertAn(st, 1);
    const int stil = a.startStil;
    // singleBladeStyle: ein Schwert, nur die erste Klinge brennt.
    if (s0 != nullptr && s0->stilEineKlinge != 0 && !st.dualSabers && st.saber[0].klinge[0].an &&
        !st.saber[0].klinge[1].an) {
        return s0->stilEineKlinge;
    }
    // WP_SaberStyleValidForSaber (wp_saberLoad.cpp:528).
    const auto gueltig = [&](int s) {
        if (an0 && s0 != nullptr && (s0->stilVerboten & (1 << s)) != 0) {
            return false;
        }
        if (st.dualSabers) {
            if (an1) {
                if (s1 != nullptr && (s1->stilVerboten & (1 << s)) != 0) {
                    return false;
                }
                if (s != kSsDual) {
                    if (s != kSsTavion) {
                        return false;
                    }
                    const bool tavion0 = an0 && s0 != nullptr && (s0->stilGelernt & (1 << kSsTavion)) != 0;
                    const bool tavion1 = s1 != nullptr && (s1->stilGelernt & (1 << kSsTavion)) != 0;
                    return tavion0 || tavion1;
                }
            } else if (s == kSsDual) {
                return false;
            }
        } else if (s == kSsDual) {
            return false;
        }
        return true;
    };
    if (gueltig(stil)) {
        return stil;
    }
    // WP_UseFirstValidSaberStyle (ebenda:460), samt seiner Eigenheit: ein
    // zweites Schwert OHNE Verbote schliesst SS_DUAL aus.
    {
        bool falsch = false;
        int erlaubt = 0;
        for (int s = 1; s < kSsZahl; ++s) {
            erlaubt |= 1 << s;
        }
        if (an0 && s0 != nullptr && s0->stilVerboten != 0) {
            if ((s0->stilVerboten & (1 << stil)) != 0) {
                falsch = true;
                erlaubt &= ~s0->stilVerboten;
            }
        }
        if (st.dualSabers) {
            if (an1 && s1 != nullptr && s1->stilVerboten != 0) {
                if ((s1->stilVerboten & (1 << stil)) != 0) {
                    falsch = true;
                    erlaubt &= ~s1->stilVerboten;
                }
            } else {
                erlaubt &= ~(1 << kSsDual);
            }
        } else {
            erlaubt &= ~(1 << kSsDual);
            if (stil == kSsDual) {
                falsch = true;
            }
        }
        if (falsch && erlaubt != 0) {
            for (int s = 1; s < kSsZahl; ++s) {
                if ((erlaubt & (1 << s)) != 0) {
                    return s;
                }
            }
        }
    }
    if (st.dualSabers) {
        if (an1) {
            return kSsDual;
        }
        if (an0) {
            return kSsFast;
        }
    }
    return stil;
}

void saberGrundhaltung(const Actor& a, ActorState& st, double ms) {
    if (!haeltSaber(st)) {
        return;
    }
    // Hat die Figur die Animation nicht, laesst PM_SetAnimFinal die alte
    // stehen (PM_HasAnimation). Ohne Skelett wissen wir es nicht - dann
    // wird sie gesetzt.
    const auto vorhanden = [&a](const std::string& n) {
        return a.animDauerMs.empty() || a.animDauerMs.count(n) != 0;
    };
    const std::string haltung = saberHaltungFuerStil(saberStilJetzt(a, st));
    if (haltung == st.animation || !vorhanden(haltung)) {
        return;
    }
    if (klingeHatLaenge(st, ms)) {
        // Beim Zuenden MITTEN im Stand: Wechsel mit der Blende von
        // PM_SetAnim (100 ms), sonst nur ein anderer Name fuer denselben
        // Anfang (die Blende dorthin gilt weiter).
        const double ab = (st.saberActive && st.saberSinceMs > st.animStartMs) ? st.saberSinceMs
                                                                              : st.animStartMs;
        if (ab > st.animStartMs) {
            st.prevAnimation = st.animation;
            st.prevAnimSinceMs = ab - st.animStartMs;
            st.prevHoldAnim = false;
            st.blendStartMs = ab;
            st.blendMs = kSetAnimBlendDefault;
            st.animStartMs = ab;
        }
        st.animation = haltung;
        return;
    }
    // Die Klinge ist ganz eingefahren, waehrend die Figur stand: von der
    // Kampfhaltung zurueck in BOTH_STAND1.
    const double aus = eingefahrenAb(st);
    if (aus > st.animStartMs && aus <= ms) {
        st.prevAnimation = haltung;
        st.prevAnimSinceMs = aus - st.animStartMs;
        st.prevHoldAnim = false;
        st.blendStartMs = aus;
        st.blendMs = kSetAnimBlendDefault;
        st.animStartMs = aus;
    }
}

void ausruestungAbschluss(const Actor& a, ActorState& st) {
    st.saberLoop.clear();
    if (!haeltSaber(st) || !st.saberActive) {
        return;
    }
    const int art = st.saber[0].art;
    // Die Nullstruktur hat soundLoop 0 - kein Ton.
    if (art >= 0 && static_cast<std::size_t>(art) < a.saberArten.size()) {
        st.saberLoop = a.saberArten[static_cast<std::size_t>(art)].soundLoop;
    }
}

}  // namespace bhed
