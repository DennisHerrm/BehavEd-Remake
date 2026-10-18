// saberkontakt.h - Paket saber (rc601): Lichtschwerter beruehren Waende und
// einander, wie im Spiel.
//
// Gemeldet (Tester, Movie Duels): "There are no saber collisions".
// Entschieden: "Funken und Klang" - treffen Klingen einander oder eine Wand,
// kommen die Effekte und Klaenge des Spiels; die Animationen laufen weiter wie
// im Skript. Dazu der Betreuer: "es soll genauso laufen wie im Spiel".
//
// Die Engine teilt die Arbeit auf zwei Stellen auf, und dieser Baustein auch:
//
//   1. Der CLIENT zeichnet jede Klinge und spurt sie dabei gegen die Welt
//      (CG_AddSaberBladeGo, cg_players.cpp). Trifft sie, wird die Klinge
//      gekuerzt (ps.saber[].blade[].length *= trace.fraction, sie waechst mit
//      lengthMax je Sekunde nach), es spruehen Funken (sparks/spark_nosnd), es
//      klingt saberhitwall/saberstrikewall, im Wasser kocht es (saber/boil).
//      Das laeuft je Clientbild: com_maxfps 125 -> 8 ms (sys_main.cpp:162 OJ,
//      :175 MD). -> saberWandTakt.
//   2. Der SERVER prueft je Spielbild (sv_fps 20 -> 50 ms, sv_init.cpp:397)
//      die Klingen gegeneinander (WP_SaberDamageTrace, wp_saber.cpp): Spuren
//      von der alten zur neuen Lage gegen die Kaesten der anderen Schwerter
//      (CONTENTS_LIGHTSABER, WP_SaberUpdate), dann WP_SabersIntersect /
//      WP_SabersDistance. Ein Treffer spielt saber_block und saberblock%d.
//      In Zwischensequenzen nur, wenn G_InCinematicSaberAnim gilt - in Movie
//      Duels schaltet das Skript das mit SET_SABERCOLLISIONS ein.
//      -> saberServerTakt.
//
// Alles hier ist rein (keine App, kein Ton, kein Fenster): die Welt kommt
// als Rueckruf (SaberSpurFn), der Zufall als Saat. Gleiche Eingabe gibt
// gleiches Ergebnis - so bleibt das Vorspulen und Zurueckspringen auf der
// Zeitleiste stabil. Die Wuerfe der Engine (Q_irand) werden aus Takt, Figur,
// Schwert und Klinge gewuerfelt (saberWurf).
//
// Regelwerk: Movie Duels (Vorgabe) und OpenJK. Wo sie sich unterscheiden,
// entscheidet saberMovieDuelsRegeln() - die Liste der Unterschiede steht in
// NOTIZEN_saber.md.
#ifndef BHED_SABERKONTAKT_H
#define BHED_SABERKONTAKT_H

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bhed {

// --- Regelwerk ------------------------------------------------------------
//
// true: Movie Duels (MovieDuels-SJE-SP-Rend2), false: OpenJK (reines JKA).
// rc601 integ: gesetzt vom zentralen Schalter (bhed/regelwerk.h,
// regelwerkAnwenden); ohne ihn Movie Duels.
[[nodiscard]] bool saberMovieDuelsRegeln() noexcept;
void setzeSaberMovieDuelsRegeln(bool movieDuels) noexcept;

// --- Takte und Masse aus der Engine -----------------------------------------
inline constexpr int kSaberServerTaktMs = 50;   // sv_fps "20" (sv_init.cpp:397) -> frameMsec 50
inline constexpr int kSaberClientTaktMs = 8;    // com_maxfps "125" -> minMsec 1000/125 (common.cpp:1410 OJ)
inline constexpr float kSaberExtrapolieren = 16.0F;   // SABER_EXTRAPOLATE_DIST (wp_saber.h)
inline constexpr float kSaberKollisionsAbstand = 6.0F;   // SABER_COLLISION_DIST (wp_saber.cpp:2445 OJ, :5398 MD)
inline constexpr float kSaberRadiusSchaden = 2.0F;   // useRadiusForDamage = 2 (wp_saber.cpp:2499 OJ; SABER_RADIUS_DAMAGE_DIST MD)
inline constexpr float kSaberUnendlich = 16777216.0F;   // Q3_INFINITE (q_shared.h)

// surfaceflags.h (OJ und MD gleich)
inline constexpr std::uint32_t kInhaltFest = 0x00000001U;      // CONTENTS_SOLID
inline constexpr std::uint32_t kInhaltLava = 0x00000002U;      // CONTENTS_LAVA
inline constexpr std::uint32_t kInhaltWasser = 0x00000004U;    // CONTENTS_WATER
inline constexpr std::uint32_t kInhaltSchussSperre = 0x00000080U;   // CONTENTS_SHOTCLIP
inline constexpr std::uint32_t kInhaltGelaende = 0x00001000U;  // CONTENTS_TERRAIN
inline constexpr std::uint32_t kInhaltSchleim = 0x00020000U;   // CONTENTS_SLIME
inline constexpr std::uint32_t kInhaltSchwert = 0x00040000U;   // CONTENTS_LIGHTSABER
inline constexpr std::uint32_t kMaskeFest = kInhaltFest | kInhaltGelaende;   // MASK_SOLID (bg_public.h:715 OJ)
// MASK_SHOT ohne CONTENTS_BODY/CORPSE: Koerper prueft behaved nicht (siehe
// NOTIZEN_saber.md, Grenzen).
inline constexpr std::uint32_t kMaskeSchussWelt = kInhaltFest | kInhaltSchussSperre | kInhaltGelaende;
inline constexpr std::uint32_t kFlaecheKeinEinschlag = 0x00080000U;   // SURF_NOIMPACT

// Die Cvar-Vorgaben, die hier zaehlen.
// g_spskill: MD "2" (g_main.cpp:752), OJ "0" (g_main.cpp:631).
[[nodiscard]] int saberSpielstufe() noexcept;
// rc601 integ: die wirksame Schwierigkeit setzen (regelwerkAnwenden); -1 =
// wieder die Vorgabe des Regelwerks.
void setzeSaberSpielstufe(int stufe) noexcept;
// g_saberMoreRealistic: MD "1" (g_main.cpp:788), OJ "0" (g_main.cpp:663).
[[nodiscard]] int saberRealistisch() noexcept;

// --- Die Rechnungen aus der Engine, woertlich ------------------------------

// ShortestLineSegBewteen2LineSegs (tri_coll_test.cpp:343 OJ). Gibt den
// Abstand und die beiden naechsten Punkte.
float kuerzesterAbstand(const float start1[3], const float end1[3], const float start2[3],
                        const float end2[3], float naechster1[3], float naechster2[3]);
// G_FindClosestPointOnLineSegment (q_math.c:692 OJ) - samt seiner
// Winkelnaeherung theta = 90 * (1 - dot); nachgebaut, wie es steht.
bool naechsterPunktAufStrecke(const float start[3], const float end[3], const float from[3], float result[3]);
// tri_tri_intersect (tri_coll_test.cpp:225 OJ, :221 MD). ACHTUNG, wie im
// Spiel: die letzte Pruefung ist gegenueber Moellers Vorlage umgedreht
// ("if(isect1[1]<isect2[0] || isect2[1]<isect1[0]) return qtrue") - so
// steht es in OpenJK und Movie Duels, so rechnet das Spiel.
bool dreieckeSchneiden(const float v0[3], const float v1[3], const float v2[3], const float u0[3],
                       const float u1[3], const float u2[3]);

// Q_irand(lo, hi) aus einer Saat - gleich verteilt, stabil.
[[nodiscard]] int saberWurf(std::uint64_t saat, int lo, int hi) noexcept;
[[nodiscard]] std::uint64_t saberSaat(std::uint64_t a, std::uint64_t b, std::uint64_t c = 0, std::uint64_t d = 0,
                                      std::uint64_t e = 0) noexcept;

// PM_SaberInSpecialAttack (bg_panimate.cpp:1822 MD, :1213 OJ) nach Namen.
[[nodiscard]] bool saberSonderAngriff(const std::string& torsoAnim);
// rc601 saber review: PM_SuperBreakLoseAnim / PM_SuperBreakWinAnim
// (bg_panimate.cpp:7028/7056 OJ, :14946/14974 MD).
[[nodiscard]] bool saberSuperBruchVerlust(const std::string& anim);
[[nodiscard]] bool saberSuperBruchSieg(const std::string& anim);
// rc601 integ: der Teiler fuer das Ausfahren einer Klinge - CG_Player waechst
// je Bild um lengthMax / Teiler * cg.frametime / 100 (cg_players.cpp:16578 MD,
// 7326-7389 OJ): Movie Duels 5 fuer SS_FAST (stil 1), sonst 10; OpenJK immer
// 10. Dieselbe Rate fuer die Zeichnung (saberWandTakt) und den Zuendklang des
// Pakets ton (tonKlingen), damit beide gleich lange Klingen sehen.
[[nodiscard]] float saberZuendTeiler(int stil) noexcept;

// --- Was die .sab-Datei zum Beruehren sagt ---------------------------------
//
// WP_SaberParseParms (wp_saberLoad.cpp:2794 ff. MD, 1882 ff. OJ). Leer heisst:
// nicht gesetzt. Effektnamen ohne "effects/" und ".efx" (G_EffectIndex).
struct SaberKontaktWerte {
    std::array<std::string, 3> blockKlang{};    // blockSound1..3
    std::array<std::string, 3> blockKlang2{};   // block2Sound1..3
    std::array<std::string, 3> prallKlang{};    // bounceSound1..3
    std::array<std::string, 3> prallKlang2{};   // bounce2Sound1..3
    std::string blockEffekt;     // blockEffect
    std::string blockEffekt2;    // blockEffect2
    std::string andereEffekt;    // hitOtherEffect
    std::string andereEffekt2;   // hitOtherEffect2
    bool keineWandspuren = false;    // noWallMarks   -> SFL2_NO_WALL_MARKS
    bool keineWandspuren2 = false;   // noWallMarks2
    bool keinLeerlauf = false;       // noIdleEffect  -> SFL2_NO_IDLE_EFFECT
    bool keinLeerlauf2 = false;      // noIdleEffect2
    bool keinBlitz = false;          // noClashFlare  -> SFL2_NO_CLASH_FLARE
    bool keinBlitz2 = false;         // noClashFlare2
    bool imWasserAn = false;         // onInWater     -> SFL_ON_IN_WATER
    bool prallAnWand = false;        // bounceOnWalls -> SFL_BOUNCE_ON_WALLS
};
// Einen .sab-Schluessel (klein geschrieben) eintragen; false: keiner von uns.
bool saberKontaktSchluessel(SaberKontaktWerte& w, const std::string& schluesselKlein, const std::string& wert);

// --- ps.saber[] einer Figur, so weit das Beruehren es braucht ---------------
struct SaberKlinge {
    bool aktiv = false;          // blade[].active
    float laenge = 0.0F;         // blade[].length (gekuerzt an Waenden)
    float laengeMax = 0.0F;      // blade[].lengthMax
    float laengeAlt = 0.0F;      // blade[].lengthOld
    float radius = 3.0F;
    float mp[3]{};               // muzzlePoint
    float md[3]{};               // muzzleDir (Einheitsvektor)
    float mpAlt[3]{};            // muzzlePointOld
    float mdAlt[3]{};            // muzzleDirOld
};
struct SaberSchwert {
    std::string typ;             // saberType, gross (SABER_SINGLE ...)
    int numBlades = 0;
    int bladeStyle2Start = 0;
    bool ohneKlinge = false;     // SFL2_NO_BLADE
    bool ohneKlinge2 = false;    // SFL2_NO_BLADE2
    bool zweihaendig = false;    // SFL_TWO_HANDED
    SaberKontaktWerte werte;
    std::array<SaberKlinge, 8> klinge{};
    // WP_SaberBladeUseSecondBladeStyle (wp_saberLoad.cpp)
    [[nodiscard]] bool zweiteArt(int b) const noexcept { return bladeStyle2Start > 0 && b >= bladeStyle2Start; }
    [[nodiscard]] float gesamtLaenge() const noexcept;
};
// Der Kasten des Schwerts (saberent, WP_SaberUpdate) - zuletzt gesetzt in
// der Runde der Figur.
struct SaberKasten {
    bool fest = false;           // contents & CONTENTS_LIGHTSABER
    float ort[3]{};
    float mins[3]{};
    float maxs[3]{};
};
struct SaberFigur {
    int figur = -1;              // Index der Figur in der Szene (= Reihenfolge der Entities)
    bool npc = true;             // ent->NPC (nicht der Spieler)
    bool unsichtbar = false;     // EF_NODRAW (SET_INVISIBLE)
    std::string verhalten;       // NPC->behaviorState, leer = BS_DEFAULT
    bool saberKollisionen = false;   // client->saberCollisions (nur Movie Duels)
    std::string torsoAnim;       // ps.torsoAnim
    std::string klasse;          // NPC_class, z.B. CLASS_SHADOWTROOPER (gross)
    std::string team;            // playerTeam, z.B. TEAM_PLAYER (gross)
    bool boss = false;           // NPCAI_BOSS_CHARACTER
    float ort[3]{};              // currentOrigin
    float gier = 0.0F;           // viewangles[YAW]
    float mins[3]{-16.0F, -16.0F, -24.0F};
    float maxs[3]{16.0F, 16.0F, 40.0F};
    bool dual = false;           // ps.dualSabers
    std::array<SaberSchwert, 2> schwert{};
    bool hatAlt = false;         // renderInfo.muzzlePointOld gesetzt
    SaberKasten kasten;
    [[nodiscard]] bool schwertAn() const noexcept;   // ps.SaberActive()
    [[nodiscard]] float schwertLaenge() const noexcept;   // ps.SaberLength()
};

// G_InCinematicSaberAnim (wp_saber.cpp:2434 OJ, :5325 MD).
[[nodiscard]] bool kinoSchwertAnim(const SaberFigur& f);

// --- Die Welt, wie gi.trace sie zeigt --------------------------------------
struct SaberSpur {
    float fraction = 1.0F;
    float endpos[3]{};
    float normale[3]{};
    std::uint32_t inhalt = 0;    // trace.contents
    std::uint32_t flaeche = 0;   // trace.surfaceFlags
    bool startsolid = false;
    bool allsolid = false;
    bool welt = true;            // ENTITYNUM_WORLD oder ein Brush-Modell (SOLID_BMODEL)
    bool brushModell = false;    // getroffen hat ein Brush-Modell (Tuer, func_wall), nicht die Welt
};
// Strecke a -> b mit Inhaltsmaske; der Rueckruf kennt Welt und Brush-Modelle.
using SaberSpurFn = std::function<SaberSpur(const float a[3], const float b[3], std::uint32_t maske)>;

// Eine Spur gegen einen achsparallelen Kasten (CM_TempBoxModel +
// CM_TraceThroughBrush, cm_trace.cpp; SURFACE_CLIP_EPSILON 0.125), mit dem
// Kasten der Spur (mins/maxs, beide 0 = Punkt).
[[nodiscard]] SaberSpur kastenSpur(const float start[3], const float ende[3], const float spurMins[3],
                                   const float spurMaxs[3], const float kastenMins[3], const float kastenMaxs[3]);

// WP_SaberUpdate (wp_saber.cpp:7858 OJ, WP_SaberUpdateJKA :23569 MD): der
// Kasten des Schwerts, aus der aktuellen Lage der Klingen.
void saberKastenSetzen(SaberFigur& f);

// --- Server: Klingen gegeneinander -----------------------------------------
struct SaberKlirren {
    double ms = 0.0;
    int figur = -1;              // der "Angreifer" (ent), in dessen Runde es geschah
    int gegner = -1;             // Besitzer des getroffenen Schwerts
    int schwert = 0;
    int klinge = 0;
    float ort[3]{};              // saberHitLocation
    float normale[3]{};          // saberHitNormal - meist geloescht (0,0,0)
    float klangOrt[3]{};         // ent->currentOrigin (G_Sound)
    std::string effekt;          // ohne "effects/" und ".efx"; leer: keiner (g_saberNoEffects)
    std::string klang;           // leer: G_Sound mit Index 0 - still
    bool blitz = false;          // g_saberFlashTime / g_saberFlashPos
};

// Ein Spielbild: fuer jede Figur in Reihenfolge WP_SaberUpdate (Kasten),
// WP_SabersDamageTrace (nur gegen Schwerter und Welt) und
// WP_SaberUpdateOldBladeData. `figuren` muss die Lage dieses Bildes tragen
// (mp/md/laenge) und die alte des vorigen (mpAlt/mdAlt/laengeAlt, hatAlt);
// danach steht in alt, was jetzt ist.
void saberServerTakt(std::vector<SaberFigur>& figuren, double ms, const SaberSpurFn& welt,
                     std::vector<SaberKlirren>& aus);

// --- Client: eine Klinge gegen die Wand ------------------------------------
//
// Der Stand, den die Engine von Bild zu Bild mitfuehrt.
struct SaberWandKlinge {
    float laenge = 0.0F;         // ps.saber[].blade[].length (nach dem Kuerzen)
    bool hatAltePos = false;     // trail.haveOldPos[0]
    float altePos[3]{};          // trail.oldPos[0]
    float alteNormale[3]{};
};
struct SaberWandFigur {
    double klangSperreMs = -1.0e12;   // ps.saberHitWallSoundDebounceTime
};
struct SaberWandEingabe {
    double ms = 0.0;
    float wurzelOrt[3]{};        // root_origin (cent->lerpOrigin)
    float org[3]{};              // muzzlePoint dieses Bildes
    float achse[3]{};            // axis[0]
    float laengeMax = 0.0F;
    bool aktiv = false;          // blade[].active
    int numBlades = 1;
    bool keineWandspuren = false;   // fuer die Art dieser Klinge (SFL2_NO_WALL_MARKS bzw. 2)
    bool keinLeerlauf = false;      // SFL2_NO_IDLE_EFFECT bzw. 2
    bool angriff = false;        // PM_SaberInSpecialAttack(torsoAnim); saberMove gibt es hier nicht
    std::uint64_t saat = 0;
    // rc601 saber review 2b: ps.saberAnimLevel (SS_FAST 1 ... SS_STAFF 7) und
    // PM_SuperBreakWinAnim(torsoAnim) - fuer die Zuendgeschwindigkeit.
    int stil = 0;
    bool superBruchSieg = false;
};
struct SaberWandEreignis {
    enum class Art : std::uint8_t { Funken, Kochen, Klang, Spur };
    Art art = Art::Funken;
    double ms = 0.0;
    float ort[3]{};
    float normale[3]{};
    float von[3]{};              // Spur: von trail.oldPos nach ort
    std::string name;            // Effekt- oder Klangname
    int figur = -1;              // rc601 saber review: wessen Klinge (CHAN_ITEM-Stomp der Entity); setzt der Aufrufer
};
// Ergebnis eines Bildes: die Laenge, mit der dieses Bild zeichnet (die
// "length" vor dem Kuerzen, CG_DoSaber), und ob die Klinge ueberhaupt da ist.
struct SaberWandBild {
    float zeichenLaenge = 0.0F;
    bool gezeichnet = false;     // ps length > 0
};
// Ein Clientbild fuer eine Klinge: wachsen/schrumpfen (CG_Player,
// cg_players.cpp:7326-7389 OJ), dann CG_AddSaberBladeGo (Spuren, Funken,
// Klang, Kuerzen).
SaberWandBild saberWandTakt(SaberWandKlinge& k, SaberWandFigur& f, const SaberWandEingabe& in,
                            const SaberSpurFn& welt, std::vector<SaberWandEreignis>& aus);

// Die Laenge, mit der CG_AddSaberBladeGo zeichnet und spurt, aus der
// gefuehrten Laenge (cg_players.cpp:6343-6355 OJ, :14217-14234 MD).
[[nodiscard]] float saberZeichenLaenge(float laenge, float laengeMax) noexcept;

}  // namespace bhed

#endif
