// saberkontakt.cpp - Paket saber (rc601). Siehe bhed/saberkontakt.h.
//
// Die Rechnungen sind aus der Engine uebertragen, Zeile fuer Zeile, mit
// ihren Eigenheiten (umgedrehte Pruefung in tri_tri_intersect, die Naeherung
// in G_FindClosestPointOnLineSegment, VectorCompare2). "OJ" heisst OpenJK
// (code/), "MD" Movie Duels (MovieDuels-SJE-SP-Rend2), beide master.
#include "bhed/saberkontakt.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>

namespace bhed {

namespace {

bool g_saberMd = true;

float punkt(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void minus(const float a[3], const float b[3], float o[3]) {
    for (int i = 0; i < 3; ++i) { o[i] = a[i] - b[i]; }
}
void kopie(const float a[3], float o[3]) {
    for (int i = 0; i < 3; ++i) { o[i] = a[i]; }
}
void ma(const float a[3], float s, const float b[3], float o[3]) {
    for (int i = 0; i < 3; ++i) { o[i] = a[i] + s * b[i]; }
}
float laenge(const float a[3]) { return std::sqrt(punkt(a, a)); }
float abstand(const float a[3], const float b[3]) {
    float d[3];
    minus(a, b, d);
    return laenge(d);
}
// VectorNormalize (q_math.c): gibt die Laenge; ein Nullvektor bleibt null.
float normieren(float v[3]) {
    const float l = laenge(v);
    if (l != 0.0F) {
        const float il = 1.0F / l;
        for (int i = 0; i < 3; ++i) { v[i] *= il; }
    }
    return l;
}
// VectorNormalize2 (q_math.c)
void normieren2(const float v[3], float o[3]) {
    const float l = laenge(v);
    if (l != 0.0F) {
        const float il = 1.0F / l;
        for (int i = 0; i < 3; ++i) { o[i] = v[i] * il; }
    } else {
        o[0] = o[1] = o[2] = 0.0F;
    }
}
// DotProductNormalize (q_math.c:1384 OJ)
float punktNormiert(const float a[3], const float b[3]) {
    float x[3];
    float y[3];
    normieren2(a, x);
    normieren2(b, y);
    return punkt(x, y);
}
bool gleich(const float a[3], const float b[3]) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }
bool null(const float a[3]) { return a[0] == 0.0F && a[1] == 0.0F && a[2] == 0.0F; }
// VectorCompare2 (q_math.c:1228 OJ) - woertlich, samt dem "+" fuer y und z,
// das die Pruefung fuer gleiche Vektoren fehlschlagen laesst. Damit nimmt
// WP_SaberDamageTrace praktisch immer den Weg mit den Schritten.
bool vergleich2(const float v1[3], const float v2[3]) {
    if (v1[0] > (v2[0] + 0.0001F) || v1[0] < (v2[0] - 0.0001F) || v1[1] > (v2[1] + 0.0001F) ||
        v1[1] < (v2[1] + 0.0001F) || v1[2] > (v2[2] + 0.0001F) || v1[2] < (v2[2] + 0.0001F)) {
        return false;
    }
    return true;
}

constexpr float kPi = 3.14159265358979323846F;

// vectoangles (q_math.c)
void nachWinkeln(const float v[3], float ang[3]) {
    float gier = 0.0F;
    float nick = 0.0F;
    if (v[1] == 0.0F && v[0] == 0.0F) {
        gier = 0.0F;
        nick = (v[2] > 0.0F) ? 90.0F : 270.0F;
    } else {
        if (v[0] != 0.0F) {
            gier = std::atan2(v[1], v[0]) * 180.0F / kPi;
        } else if (v[1] > 0.0F) {
            gier = 90.0F;
        } else {
            gier = 270.0F;
        }
        if (gier < 0.0F) { gier += 360.0F; }
        const float vorn = std::sqrt(v[0] * v[0] + v[1] * v[1]);
        nick = std::atan2(v[2], vorn) * 180.0F / kPi;
        if (nick < 0.0F) { nick += 360.0F; }
    }
    ang[0] = -nick;
    ang[1] = gier;
    ang[2] = 0.0F;
}
// LerpAngle (q_math.c)
float winkelMisch(float von, float nach, float t) {
    if (nach - von > 180.0F) { nach -= 360.0F; }
    if (nach - von < -180.0F) { nach += 360.0F; }
    return von + t * (nach - von);
}
// AngleVectors (q_math.c), nur forward
void vorwaerts(const float ang[3], float f[3]) {
    const float g = ang[1] * (kPi * 2.0F / 360.0F);
    const float sy = std::sin(g);
    const float cy = std::cos(g);
    const float n = ang[0] * (kPi * 2.0F / 360.0F);
    const float sp = std::sin(n);
    const float cp = std::cos(n);
    f[0] = cp * cy;
    f[1] = cp * sy;
    f[2] = -sp;
}

bool inListe(const std::string& name, const char* const* liste) {
    for (const char* const* p = liste; *p != nullptr; ++p) {
        if (name == *p) { return true; }
    }
    return false;
}

// --- Animationslisten (bg_panimate.cpp / bg_pmove.cpp) ---------------------
// PM_SaberInSpecialAttack, bg_panimate.cpp:1213 OJ
const char* const kSonderOj[] = {
    "BOTH_A2_STABBACK1", "BOTH_ATTACK_BACK", "BOTH_CROUCHATTACKBACK1", "BOTH_ROLL_STAB", "BOTH_BUTTERFLY_LEFT",
    "BOTH_BUTTERFLY_RIGHT", "BOTH_BUTTERFLY_FL1", "BOTH_BUTTERFLY_FR1", "BOTH_FJSS_TR_BL", "BOTH_FJSS_TL_BR",
    "BOTH_LUNGE2_B__T_", "BOTH_FORCELEAP2_T__B_", "BOTH_JUMPFLIPSLASHDOWN1", "BOTH_JUMPFLIPSTABDOWN",
    "BOTH_JUMPATTACK6", "BOTH_JUMPATTACK7", "BOTH_SPINATTACK6", "BOTH_SPINATTACK7", "BOTH_FORCELONGLEAP_ATTACK",
    "BOTH_VS_ATR_S", "BOTH_VS_ATL_S", "BOTH_VT_ATR_S", "BOTH_VT_ATL_S", "BOTH_A7_KICK_F", "BOTH_A7_KICK_B",
    "BOTH_A7_KICK_R", "BOTH_A7_KICK_L", "BOTH_A7_KICK_S", "BOTH_A7_KICK_BF", "BOTH_A7_KICK_RL",
    "BOTH_A7_KICK_F_AIR", "BOTH_A7_KICK_B_AIR", "BOTH_A7_KICK_R_AIR", "BOTH_A7_KICK_L_AIR", "BOTH_STABDOWN",
    "BOTH_STABDOWN_STAFF", "BOTH_STABDOWN_DUAL", "BOTH_A6_SABERPROTECT", "BOTH_A7_SOULCAL", "BOTH_A1_SPECIAL",
    "BOTH_A2_SPECIAL", "BOTH_A3_SPECIAL", "BOTH_FLIP_ATTACK7", "BOTH_PULL_IMPALE_STAB", "BOTH_PULL_IMPALE_SWING",
    "BOTH_ALORA_SPIN_SLASH", "BOTH_A6_FB", "BOTH_A6_LR", "BOTH_A7_HILT", "BOTH_LK_S_DL_S_SB_1_W",
    "BOTH_LK_S_DL_T_SB_1_W", "BOTH_LK_S_ST_S_SB_1_W", "BOTH_LK_S_ST_T_SB_1_W", "BOTH_LK_S_S_S_SB_1_W",
    "BOTH_LK_S_S_T_SB_1_W", "BOTH_LK_DL_DL_S_SB_1_W", "BOTH_LK_DL_DL_T_SB_1_W", "BOTH_LK_DL_ST_S_SB_1_W",
    "BOTH_LK_DL_ST_T_SB_1_W", "BOTH_LK_DL_S_S_SB_1_W", "BOTH_LK_DL_S_T_SB_1_W", "BOTH_LK_ST_DL_S_SB_1_W",
    "BOTH_LK_ST_DL_T_SB_1_W", "BOTH_LK_ST_ST_S_SB_1_W", "BOTH_LK_ST_ST_T_SB_1_W", "BOTH_LK_ST_S_S_SB_1_W",
    "BOTH_LK_ST_S_T_SB_1_W", "BOTH_HANG_ATTACK", nullptr};
// Movie Duels ergaenzt (bg_panimate.cpp:1822 MD)
const char* const kSonderMdDazu[] = {
    "BOTH_FORCELEAP2_T__B__PAL", "BOTH_JUMPATTACK6_GRIEV", "BOTH_SPINATTACK6_GRIEV", "BOTH_FORCEJUMPDASH_ATTACK",
    "BOTH_KICK_F_MD", "BOTH_KICK_F_MD_GALEN", "BOTH_SWEEP_KICK", "BOTH_SWEEP_KICK_GALEN", "BOTH_FLYING_KICK",
    "BOTH_FLYING_KICK_GALEN", "BOTH_A1_SPECIAL_YODA", "BOTH_A2_SPECIAL_ANI", "BOTH_A6_SABERPROTECT_GRIEV",
    "BOTH_STABDOWN_WINDU", "BOTH_SMASHDOWN_DUAL", "BOTH_SMASHDOWN_SINGLE", "BOTH_SMASHDOWN_STAFF", "BOTH_SLAP_L",
    "BOTH_SLAP_R", "BOTH_A7_SLAP_R", "BOTH_A7_SLAP_R_GALEN", "BOTH_A7_SLAP_L", "BOTH_A7_SLAP_L_GALEN",
    "BOTH_GRAPPLE_FIRE", "BOTH_GRAPPLE_FIRE_GALEN", nullptr};
// PM_SpinningSaberAnim (bg_pmove.cpp OJ/MD); MD ergaenzt die _YODA-Fassungen.
const char* const kDrehen[] = {
    "BOTH_T1_BR_BL", "BOTH_T1__R__L", "BOTH_T1__R_BL", "BOTH_T1_TR_BL", "BOTH_T1_BR_TL", "BOTH_T1_BR__L",
    "BOTH_T1_TL_BR", "BOTH_T1__L_BR", "BOTH_T1__L__R", "BOTH_T1_BL_BR", "BOTH_T1_BL__R", "BOTH_T1_BL_TR",
    "BOTH_T2_BR__L", "BOTH_T2_BR_BL", "BOTH_T2__R_BL", "BOTH_T2__L_BR", "BOTH_T2_BL_BR", "BOTH_T2_BL__R",
    "BOTH_T3_BR__L", "BOTH_T3_BR_BL", "BOTH_T3__R_BL", "BOTH_T3__L_BR", "BOTH_T3_BL_BR", "BOTH_T3_BL__R",
    "BOTH_T4_BR__L", "BOTH_T4_BR_BL", "BOTH_T4__R_BL", "BOTH_T4__L_BR", "BOTH_T4_BL_BR", "BOTH_T4_BL__R",
    "BOTH_T5_BR_BL", "BOTH_T5__R__L", "BOTH_T5__R_BL", "BOTH_T5_TR_BL", "BOTH_T5_BR_TL", "BOTH_T5_BR__L",
    "BOTH_T5_TL_BR", "BOTH_T5__L_BR", "BOTH_T5__L__R", "BOTH_T5_BL_BR", "BOTH_T5_BL__R", "BOTH_T5_BL_TR",
    "BOTH_T6_BR_TL", "BOTH_T6__R_TL", "BOTH_T6__R__L", "BOTH_T6__R_BL", "BOTH_T6_TR_TL", "BOTH_T6_TR__L",
    "BOTH_T6_TR_BL", "BOTH_T6_T__TL", "BOTH_T6_T__BL", "BOTH_T6_TL_BR", "BOTH_T6__L_BR", "BOTH_T6__L__R",
    "BOTH_T6_TL__R", "BOTH_T6_TL_TR", "BOTH_T6__L_TR", "BOTH_T6__L_T_", "BOTH_T6_BL_T_", "BOTH_T6_BR__L",
    "BOTH_T6_BR_BL", "BOTH_T6_BL_BR", "BOTH_T6_BL__R", "BOTH_T6_BL_TR", "BOTH_T7_BR_TL", "BOTH_T7_BR__L",
    "BOTH_T7_BR_BL", "BOTH_T7__R__L", "BOTH_T7__R_BL", "BOTH_T7_TR__L", "BOTH_T7_T___R", "BOTH_T7_TL_BR",
    "BOTH_T7__L_BR", "BOTH_T7__L__R", "BOTH_T7_BL_BR", "BOTH_T7_BL__R", "BOTH_T7_BL_TR", "BOTH_T7_TL_TR",
    "BOTH_T7_T__BR", "BOTH_T7__L_TR", "BOTH_V7_BL_S7", "BOTH_A2_STABBACK1", "BOTH_ATTACK_BACK",
    "BOTH_CROUCHATTACKBACK1", "BOTH_BUTTERFLY_LEFT", "BOTH_BUTTERFLY_RIGHT", "BOTH_FJSS_TR_BL", "BOTH_FJSS_TL_BR",
    "BOTH_JUMPFLIPSLASHDOWN1", "BOTH_JUMPFLIPSTABDOWN", nullptr};
const char* const kDrehenMdDazu[] = {"BOTH_T5__R__L_YODA", "BOTH_T5_BR_TL_YODA", "BOTH_T5_TL_BR_YODA",
                                     "BOTH_T5__L__R_YODA", "BOTH_T5_BL__R_YODA", "BOTH_T5_BL_TR_YODA", nullptr};
// PM_InSpecialJump (bg_pmove.cpp:892 OJ, :1213 MD) samt PM_InReboundJump,
// PM_InReboundHold, PM_InReboundRelease und PM_InBackFlip.
const char* const kSprung[] = {
    "BOTH_WALL_RUN_RIGHT", "BOTH_WALL_RUN_RIGHT_STOP", "BOTH_WALL_RUN_RIGHT_FLIP", "BOTH_WALL_RUN_LEFT",
    "BOTH_WALL_RUN_LEFT_STOP", "BOTH_WALL_RUN_LEFT_FLIP", "BOTH_WALL_FLIP_RIGHT", "BOTH_WALL_FLIP_LEFT",
    "BOTH_WALL_FLIP_BACK1", "BOTH_BUTTERFLY_LEFT", "BOTH_BUTTERFLY_RIGHT", "BOTH_BUTTERFLY_FL1",
    "BOTH_BUTTERFLY_FR1", "BOTH_FJSS_TR_BL", "BOTH_FJSS_TL_BR", "BOTH_FORCELEAP2_T__B_", "BOTH_JUMPFLIPSLASHDOWN1",
    "BOTH_JUMPFLIPSTABDOWN", "BOTH_JUMPATTACK6", "BOTH_JUMPATTACK7", "BOTH_ARIAL_LEFT", "BOTH_ARIAL_RIGHT",
    "BOTH_ARIAL_F1", "BOTH_CARTWHEEL_LEFT", "BOTH_CARTWHEEL_RIGHT", "BOTH_FORCELONGLEAP_START",
    "BOTH_FORCELONGLEAP_ATTACK", "BOTH_FORCEWALLRUNFLIP_START", "BOTH_FORCEWALLRUNFLIP_END",
    "BOTH_FORCEWALLRUNFLIP_ALT", "BOTH_FLIP_ATTACK7", "BOTH_FLIP_HOLD7", "BOTH_FLIP_LAND", "BOTH_A7_SOULCAL",
    "BOTH_FORCEWALLREBOUND_FORWARD", "BOTH_FORCEWALLREBOUND_LEFT", "BOTH_FORCEWALLREBOUND_BACK",
    "BOTH_FORCEWALLREBOUND_RIGHT", "BOTH_FORCEWALLHOLD_FORWARD", "BOTH_FORCEWALLHOLD_LEFT",
    "BOTH_FORCEWALLHOLD_BACK", "BOTH_FORCEWALLHOLD_RIGHT", "BOTH_FORCEWALLRELEASE_FORWARD",
    "BOTH_FORCEWALLRELEASE_LEFT", "BOTH_FORCEWALLRELEASE_BACK", "BOTH_FORCEWALLRELEASE_RIGHT", "BOTH_FLIP_BACK1",
    "BOTH_FLIP_BACK2", "BOTH_FLIP_BACK3", "BOTH_ALORA_FLIP_B", nullptr};
const char* const kSprungMdDazu[] = {"BOTH_FORCELEAP2_T__B__PAL", "BOTH_JUMPATTACK6_GRIEV",
                                     "BOTH_FORCEJUMPDASH_START", "BOTH_FORCEJUMPDASH_ATTACK", nullptr};

// rc601 saber review: PM_SuperBreakLoseAnim / PM_SuperBreakWinAnim
// (bg_panimate.cpp:7028/7056 OJ, :14946/14974 MD - gleich).
const char* const kSuperBruchVerlust[] = {
    "BOTH_LK_S_DL_S_SB_1_L",  "BOTH_LK_S_DL_T_SB_1_L",  "BOTH_LK_S_ST_S_SB_1_L",  "BOTH_LK_S_ST_T_SB_1_L",
    "BOTH_LK_S_S_S_SB_1_L",   "BOTH_LK_S_S_T_SB_1_L",   "BOTH_LK_DL_DL_S_SB_1_L", "BOTH_LK_DL_DL_T_SB_1_L",
    "BOTH_LK_DL_ST_S_SB_1_L", "BOTH_LK_DL_ST_T_SB_1_L", "BOTH_LK_DL_S_S_SB_1_L",  "BOTH_LK_DL_S_T_SB_1_L",
    "BOTH_LK_ST_DL_S_SB_1_L", "BOTH_LK_ST_DL_T_SB_1_L", "BOTH_LK_ST_ST_S_SB_1_L", "BOTH_LK_ST_ST_T_SB_1_L",
    "BOTH_LK_ST_S_S_SB_1_L",  "BOTH_LK_ST_S_T_SB_1_L",  nullptr};
const char* const kSuperBruchSieg[] = {
    "BOTH_LK_S_DL_S_SB_1_W",  "BOTH_LK_S_DL_T_SB_1_W",  "BOTH_LK_S_ST_S_SB_1_W",  "BOTH_LK_S_ST_T_SB_1_W",
    "BOTH_LK_S_S_S_SB_1_W",   "BOTH_LK_S_S_T_SB_1_W",   "BOTH_LK_DL_DL_S_SB_1_W", "BOTH_LK_DL_DL_T_SB_1_W",
    "BOTH_LK_DL_ST_S_SB_1_W", "BOTH_LK_DL_ST_T_SB_1_W", "BOTH_LK_DL_S_S_SB_1_W",  "BOTH_LK_DL_S_T_SB_1_W",
    "BOTH_LK_ST_DL_S_SB_1_W", "BOTH_LK_ST_DL_T_SB_1_W", "BOTH_LK_ST_ST_S_SB_1_W", "BOTH_LK_ST_ST_T_SB_1_W",
    "BOTH_LK_ST_S_S_SB_1_W",  "BOTH_LK_ST_S_T_SB_1_W",  nullptr};

bool drehendeAnim(const std::string& a) {
    return inListe(a, kDrehen) || (g_saberMd && inListe(a, kDrehenMdDazu));
}
bool sonderSprung(const std::string& a) {
    return inListe(a, kSprung) || (g_saberMd && inListe(a, kSprungMdDazu));
}

}  // namespace

bool saberMovieDuelsRegeln() noexcept { return g_saberMd; }
float saberZuendTeiler(int stil) noexcept { return (g_saberMd && stil == 1) ? 5.0F : 10.0F; }
void setzeSaberMovieDuelsRegeln(bool movieDuels) noexcept { g_saberMd = movieDuels; }
// rc601 integ: g_spskill ist EINE Cvar - die Schwierigkeit aus den
// Einstellungen gilt auch hier (regelwerkAnwenden setzt sie). -1: die Vorgabe
// des Regelwerks (MD "2", g_main.cpp:752; OJ "0", g_main.cpp:631).
namespace {
int g_saberStufe = -1;
}  // namespace
int saberSpielstufe() noexcept { return g_saberStufe >= 0 ? g_saberStufe : (g_saberMd ? 2 : 0); }
void setzeSaberSpielstufe(int stufe) noexcept { g_saberStufe = (stufe >= 0 && stufe <= 3) ? stufe : -1; }
int saberRealistisch() noexcept { return g_saberMd ? 1 : 0; }

bool saberSuperBruchVerlust(const std::string& anim) { return inListe(anim, kSuperBruchVerlust); }
bool saberSuperBruchSieg(const std::string& anim) { return inListe(anim, kSuperBruchSieg); }

bool saberSonderAngriff(const std::string& torsoAnim) {
    return inListe(torsoAnim, kSonderOj) || (g_saberMd && inListe(torsoAnim, kSonderMdDazu));
}

// --- G_FindClosestPointOnLineSegment (q_math.c:692 OJ) ----------------------
bool naechsterPunktAufStrecke(const float start[3], const float end[3], const float from[3], float result[3]) {
    float vecStart2From[3];
    float vecStart2End[3];
    float vecEnd2Start[3];
    float vecEnd2From[3];
    minus(from, start, vecStart2From);
    minus(end, start, vecStart2End);
    float dot = punktNormiert(vecStart2From, vecStart2End);
    if (dot <= 0.0F) {
        kopie(start, result);
        return false;
    }
    if (dot == 1.0F) {
        if (punkt(vecStart2From, vecStart2From) < punkt(vecStart2End, vecStart2End)) {
            kopie(from, result);
        } else {
            kopie(end, result);
        }
        return false;
    }
    minus(from, end, vecEnd2From);
    minus(start, end, vecEnd2Start);
    dot = punktNormiert(vecEnd2From, vecEnd2Start);
    if (dot <= 0.0F) {
        kopie(end, result);
        return false;
    }
    if (dot == 1.0F) {
        if (punkt(vecEnd2From, vecEnd2From) < punkt(vecEnd2Start, vecEnd2Start)) {
            kopie(from, result);
        } else {
            kopie(end, result);
        }
        return false;
    }
    // theta = 90 * (1 - dot) - so steht es da (keine echte Winkelrechnung).
    const float theta = 90.0F * (1.0F - dot);
    const float distEnd2From = laenge(vecEnd2From);
    const float cosTheta = std::cos(theta * kPi / 180.0F);
    const float distEnd2Result = cosTheta * distEnd2From;
    normieren(vecEnd2Start);
    ma(end, distEnd2Result, vecEnd2Start, result);
    return true;
}

// --- ShortestLineSegBewteen2LineSegs (tri_coll_test.cpp:343 OJ) ------------
float kuerzesterAbstand(const float start1[3], const float end1[3], const float start2[3], const float end2[3],
                        float close1[3], float close2[3]) {
    float currentDist = 0.0F;
    float newPnt[3];
    float startDif[3];
    float v1[3];
    float v2[3];
    minus(start2, start1, startDif);
    minus(end1, start1, v1);
    minus(end2, start2, v2);
    const float v1v1 = punkt(v1, v1);
    const float v2v2 = punkt(v2, v2);
    const float v1v2 = punkt(v1, v2);
    const float denom = (v1v2 * v1v2) - (v1v1 * v2v2);
    if (std::fabs(denom) > 0.001F) {
        float s = -((v2v2 * punkt(v1, startDif)) - (v1v2 * punkt(v2, startDif))) / denom;
        float t = ((v1v1 * punkt(v2, startDif)) - (v1v2 * punkt(v1, startDif))) / denom;
        bool done = true;
        if (s < 0) { done = false; s = 0; }
        if (s > 1) { done = false; s = 1; }
        if (t < 0) { done = false; t = 0; }
        if (t > 1) { done = false; t = 1; }
        ma(start1, s, v1, close1);
        ma(start2, t, v2, close2);
        currentDist = abstand(close1, close2);
        if (done) {
            return currentDist;
        }
    } else {
        currentDist = kSaberUnendlich;
    }
    const auto paar = [&](const float* a, const float* b) {
        const float d = abstand(a, b);
        if (d < currentDist) {
            kopie(a, close1);
            kopie(b, close2);
            currentDist = d;
        }
    };
    paar(start1, start2);
    paar(start1, end2);
    paar(end1, start2);
    paar(end1, end2);
    naechsterPunktAufStrecke(start2, end2, start1, newPnt);
    paar(start1, newPnt);
    naechsterPunktAufStrecke(start2, end2, end1, newPnt);
    paar(end1, newPnt);
    naechsterPunktAufStrecke(start1, end1, start2, newPnt);
    {
        const float d = abstand(start2, newPnt);
        if (d < currentDist) {
            kopie(newPnt, close1);
            kopie(start2, close2);
            currentDist = d;
        }
    }
    naechsterPunktAufStrecke(start1, end1, end2, newPnt);
    {
        const float d = abstand(end2, newPnt);
        if (d < currentDist) {
            kopie(newPnt, close1);
            kopie(end2, close2);
            currentDist = d;
        }
    }
    return currentDist;
}

// --- tri_tri_intersect (tri_coll_test.cpp, Moeller 1997, Raven-Fassung) -----
namespace {
constexpr float kEps = 0.000001F;   // EPSILON, USE_EPSILON_TEST 1

// EDGE_EDGE_TEST / EDGE_AGAINST_TRI_EDGES / POINT_IN_TRI
bool kanteKante(const float v0[3], const float u0[3], const float u1[3], int i0, int i1, float ax, float ay) {
    const float bx = u0[i0] - u1[i0];
    const float by = u0[i1] - u1[i1];
    const float cx = v0[i0] - u0[i0];
    const float cy = v0[i1] - u0[i1];
    const float f = ay * bx - ax * by;
    const float d = by * cx - bx * cy;
    if ((f > 0 && d >= 0 && d <= f) || (f < 0 && d <= 0 && d >= f)) {
        const float e = ax * cy - ay * cx;
        if (f > 0) {
            if (e >= 0 && e <= f) { return true; }
        } else {
            if (e <= 0 && e >= f) { return true; }
        }
    }
    return false;
}
bool kanteGegenDreieck(const float v0[3], const float v1[3], const float u0[3], const float u1[3],
                       const float u2[3], int i0, int i1) {
    const float ax = v1[i0] - v0[i0];
    const float ay = v1[i1] - v0[i1];
    return kanteKante(v0, u0, u1, i0, i1, ax, ay) || kanteKante(v0, u1, u2, i0, i1, ax, ay) ||
           kanteKante(v0, u2, u0, i0, i1, ax, ay);
}
bool punktImDreieck(const float v0[3], const float u0[3], const float u1[3], const float u2[3], int i0, int i1) {
    float a = u1[i1] - u0[i1];
    float b = -(u1[i0] - u0[i0]);
    float c = -a * u0[i0] - b * u0[i1];
    const float d0 = a * v0[i0] + b * v0[i1] + c;
    a = u2[i1] - u1[i1];
    b = -(u2[i0] - u1[i0]);
    c = -a * u1[i0] - b * u1[i1];
    const float d1 = a * v0[i0] + b * v0[i1] + c;
    a = u0[i1] - u2[i1];
    b = -(u0[i0] - u2[i0]);
    c = -a * u2[i0] - b * u2[i1];
    const float d2 = a * v0[i0] + b * v0[i1] + c;
    if (d0 * d1 > 0.0F) {
        if (d0 * d2 > 0.0F) { return true; }
    }
    return false;
}
// coplanar_tri_tri
bool koplanar(const float n[3], const float v0[3], const float v1[3], const float v2[3], const float u0[3],
              const float u1[3], const float u2[3]) {
    const float a0 = std::fabs(n[0]);
    const float a1 = std::fabs(n[1]);
    const float a2 = std::fabs(n[2]);
    int i0 = 0;
    int i1 = 0;
    if (a0 > a1) {
        if (a0 > a2) { i0 = 1; i1 = 2; } else { i0 = 0; i1 = 1; }
    } else {
        if (a2 > a1) { i0 = 0; i1 = 1; } else { i0 = 0; i1 = 2; }
    }
    if (kanteGegenDreieck(v0, v1, u0, u1, u2, i0, i1)) { return true; }
    if (kanteGegenDreieck(v1, v2, u0, u1, u2, i0, i1)) { return true; }
    if (kanteGegenDreieck(v2, v0, u0, u1, u2, i0, i1)) { return true; }
    if (punktImDreieck(v0, u0, u1, u2, i0, i1)) { return true; }
    if (punktImDreieck(u0, v0, v1, v2, i0, i1)) { return true; }
    return false;
}
// COMPUTE_INTERVALS samt ISECT. false heisst: koplanar (das Makro kehrt dann
// mit coplanar_tri_tri zurueck).
bool intervall(float vv0, float vv1, float vv2, float d0, float d1, float d2, float d0d1, float d0d2,
               float& is0, float& is1) {
    const auto isect = [&](float a0, float a1, float a2, float e0, float e1, float e2) {
        is0 = a0 + (a1 - a0) * e0 / (e0 - e1);
        is1 = a0 + (a2 - a0) * e0 / (e0 - e2);
    };
    if (d0d1 > 0.0F) {
        isect(vv2, vv0, vv1, d2, d0, d1);
    } else if (d0d2 > 0.0F) {
        isect(vv1, vv0, vv2, d1, d0, d2);
    } else if (d1 * d2 > 0.0F || d0 != 0.0F) {
        isect(vv0, vv1, vv2, d0, d1, d2);
    } else if (d1 != 0.0F) {
        isect(vv1, vv0, vv2, d1, d0, d2);
    } else if (d2 != 0.0F) {
        isect(vv2, vv0, vv1, d2, d0, d1);
    } else {
        return false;
    }
    return true;
}
}  // namespace

bool dreieckeSchneiden(const float v0[3], const float v1[3], const float v2[3], const float u0[3],
                       const float u1[3], const float u2[3]) {
    float e1[3];
    float e2[3];
    float n1[3];
    float n2[3];
    const auto kreuz = [](const float a[3], const float b[3], float o[3]) {
        o[0] = a[1] * b[2] - a[2] * b[1];
        o[1] = a[2] * b[0] - a[0] * b[2];
        o[2] = a[0] * b[1] - a[1] * b[0];
    };
    minus(v1, v0, e1);
    minus(v2, v0, e2);
    kreuz(e1, e2, n1);
    const float d1 = -punkt(n1, v0);
    float du0 = punkt(n1, u0) + d1;
    float du1 = punkt(n1, u1) + d1;
    float du2 = punkt(n1, u2) + d1;
    if (std::fabs(du0) < kEps) { du0 = 0.0F; }
    if (std::fabs(du1) < kEps) { du1 = 0.0F; }
    if (std::fabs(du2) < kEps) { du2 = 0.0F; }
    const float du0du1 = du0 * du1;
    const float du0du2 = du0 * du2;
    if (du0du1 > 0.0F && du0du2 > 0.0F) { return false; }
    minus(u1, u0, e1);
    minus(u2, u0, e2);
    kreuz(e1, e2, n2);
    const float d2 = -punkt(n2, u0);
    float dv0 = punkt(n2, v0) + d2;
    float dv1 = punkt(n2, v1) + d2;
    float dv2 = punkt(n2, v2) + d2;
    if (std::fabs(dv0) < kEps) { dv0 = 0.0F; }
    if (std::fabs(dv1) < kEps) { dv1 = 0.0F; }
    if (std::fabs(dv2) < kEps) { dv2 = 0.0F; }
    const float dv0dv1 = dv0 * dv1;
    const float dv0dv2 = dv0 * dv2;
    if (dv0dv1 > 0.0F && dv0dv2 > 0.0F) { return false; }
    float d[3];
    kreuz(n1, n2, d);
    float maxi = std::fabs(d[0]);
    int index = 0;
    const float b = std::fabs(d[1]);
    const float c = std::fabs(d[2]);
    if (b > maxi) { maxi = b; index = 1; }
    if (c > maxi) { maxi = c; index = 2; }
    const float vp0 = v0[index];
    const float vp1 = v1[index];
    const float vp2 = v2[index];
    const float up0 = u0[index];
    const float up1 = u1[index];
    const float up2 = u2[index];
    float is1[2];
    float is2[2];
    if (!intervall(vp0, vp1, vp2, dv0, dv1, dv2, dv0dv1, dv0dv2, is1[0], is1[1])) {
        return koplanar(n1, v0, v1, v2, u0, u1, u2);
    }
    if (!intervall(up0, up1, up2, du0, du1, du2, du0du1, du0du2, is2[0], is2[1])) {
        return koplanar(n1, v0, v1, v2, u0, u1, u2);
    }
    if (is1[0] > is1[1]) { std::swap(is1[0], is1[1]); }
    if (is2[0] > is2[1]) { std::swap(is2[0], is2[1]); }
    // So steht es in OpenJK (tri_coll_test.cpp:306) und Movie Duels (:302):
    // true, wenn die Intervalle sich NICHT ueberlappen.
    if (is1[1] < is2[0] || is2[1] < is1[0]) { return true; }
    return false;
}

// --- Wuerfel -------------------------------------------------------------
std::uint64_t saberSaat(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d, std::uint64_t e) noexcept {
    std::uint64_t h = 0x9e3779b97f4a7c15ULL;
    for (const std::uint64_t x : {a, b, c, d, e}) {
        h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= h >> 31;
        h *= 0xbf58476d1ce4e5b9ULL;
        h ^= h >> 27;
    }
    return h;
}
int saberWurf(std::uint64_t saat, int lo, int hi) noexcept {
    if (hi <= lo) { return lo; }
    std::uint64_t z = saat;
    z ^= z >> 33;
    z *= 0xff51afd7ed558ccdULL;
    z ^= z >> 33;
    const auto spanne = static_cast<std::uint64_t>(hi - lo + 1);
    return lo + static_cast<int>(z % spanne);
}

// --- .sab ---------------------------------------------------------------
namespace {
std::string effektName(std::string v) {
    // G_EffectIndex -> CFxScheduler::RegisterEffect: "effects/" vorn und die
    // Endung weg (FxScheduler.cpp).
    std::string k = v;
    for (char& c : k) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '\\') { c = '/'; }
    }
    if (k.rfind("effects/", 0) == 0) { v = v.substr(8); k = k.substr(8); }
    if (k.size() > 4 && k.compare(k.size() - 4, 4, ".efx") == 0) { v.resize(v.size() - 4); }
    return v;
}
}  // namespace

bool saberKontaktSchluessel(SaberKontaktWerte& w, const std::string& s, const std::string& wert) {
    const auto an = [&wert]() { return std::atoi(wert.c_str()) != 0; };
    const auto klang = [&](std::array<std::string, 3>& z, const char* basis) {
        const std::string b = basis;
        for (int i = 0; i < 3; ++i) {
            if (s == b + std::to_string(i + 1)) {
                z[static_cast<std::size_t>(i)] = wert;
                return true;
            }
        }
        return false;
    };
    if (klang(w.blockKlang, "blocksound") || klang(w.blockKlang2, "block2sound") ||
        klang(w.prallKlang, "bouncesound") || klang(w.prallKlang2, "bounce2sound")) {
        return true;
    }
    if (s == "blockeffect") { w.blockEffekt = effektName(wert); return true; }
    if (s == "blockeffect2") { w.blockEffekt2 = effektName(wert); return true; }
    if (s == "hitothereffect") { w.andereEffekt = effektName(wert); return true; }
    if (s == "hitothereffect2") { w.andereEffekt2 = effektName(wert); return true; }
    // Saber_ParseNoWallMarks & Co.: nur ein Wert != 0 setzt das Flag; 0 laesst
    // es stehen (wp_saberLoad.cpp, "if (n) saber->saberFlags2 |= ...").
    if (s == "nowallmarks") { w.keineWandspuren = w.keineWandspuren || an(); return true; }
    if (s == "nowallmarks2") { w.keineWandspuren2 = w.keineWandspuren2 || an(); return true; }
    if (s == "noidleeffect") { w.keinLeerlauf = w.keinLeerlauf || an(); return true; }
    if (s == "noidleeffect2") { w.keinLeerlauf2 = w.keinLeerlauf2 || an(); return true; }
    if (s == "noclashflare") { w.keinBlitz = w.keinBlitz || an(); return true; }
    if (s == "noclashflare2") { w.keinBlitz2 = w.keinBlitz2 || an(); return true; }
    if (s == "oninwater") { w.imWasserAn = w.imWasserAn || an(); return true; }
    if (s == "bounceonwalls") { w.prallAnWand = w.prallAnWand || an(); return true; }
    return false;
}

// --- Figur ------------------------------------------------------------------
float SaberSchwert::gesamtLaenge() const noexcept {
    float l = 0.0F;
    for (int b = 0; b < numBlades && b < 8; ++b) { l += klinge[static_cast<std::size_t>(b)].laenge; }
    return l;
}
bool SaberFigur::schwertAn() const noexcept {
    // ps.SaberActive(): saber[0].Active() || (dualSabers && saber[1].Active())
    for (int n = 0; n < (dual ? 2 : 1); ++n) {
        const SaberSchwert& s = schwert[static_cast<std::size_t>(n)];
        for (int b = 0; b < s.numBlades && b < 8; ++b) {
            if (s.klinge[static_cast<std::size_t>(b)].aktiv) { return true; }
        }
    }
    return false;
}
float SaberFigur::schwertLaenge() const noexcept {
    // ps.SaberLength(): saber[0].Length() + (dualSabers ? saber[1].Length() : 0)
    return schwert[0].gesamtLaenge() + (dual ? schwert[1].gesamtLaenge() : 0.0F);
}

bool kinoSchwertAnim(const SaberFigur& f) {
    if (!f.npc || f.verhalten != "BS_CINEMATIC") {
        return false;
    }
    if (f.torsoAnim == "BOTH_CIN_16" || f.torsoAnim == "BOTH_CIN_17") {
        return true;
    }
    // Nur Movie Duels: "&& com_outcast->integer == 0 && self->client->saberCollisions"
    // (wp_saber.cpp:5332-5337 MD); com_outcast ist "0" (cg_main.cpp:756 MD).
    return g_saberMd && f.saberKollisionen;
}

// --- Spur gegen einen Kasten ------------------------------------------------
SaberSpur kastenSpur(const float start[3], const float ende[3], const float spurMins[3], const float spurMaxs[3],
                     const float kastenMins[3], const float kastenMaxs[3]) {
    constexpr float kEpsilon = 0.125F;   // SURFACE_CLIP_EPSILON (cm_local.h)
    SaberSpur t;
    // Die sechs Seiten der Kiste in der Reihenfolge von CM_InitBoxHull:
    // +x (maxs0), -x (-mins0), +y, -y, +z, -z. Der Abstand verschiebt sich
    // um den Kasten der Spur (offsets[signbits], CM_Trace).
    float normale[6][3]{};
    float dist[6]{};
    for (int i = 0; i < 6; ++i) {
        const int achse = i >> 1;
        const bool negativ = (i & 1) != 0;
        normale[i][achse] = negativ ? -1.0F : 1.0F;
        dist[i] = negativ ? (-kastenMins[achse] + spurMaxs[achse]) : (kastenMaxs[achse] - spurMins[achse]);
    }
    if (gleich(start, ende)) {
        // CM_Trace: "if start and end are the same, do a position test"
        // (CM_TestBoxInBrush).
        for (int i = 0; i < 6; ++i) {
            if (punkt(start, normale[i]) - dist[i] > 0.0F) {
                kopie(ende, t.endpos);
                return t;
            }
        }
        t.startsolid = t.allsolid = true;
        t.fraction = 0.0F;
        t.inhalt = kInhaltSchwert;
        t.welt = false;
        kopie(start, t.endpos);
        return t;
    }
    float enterFrac = -1.0F;
    float leaveFrac = 1.0F;
    int clip = -1;
    bool getout = false;
    bool startout = false;
    for (int i = 0; i < 6; ++i) {
        const float d1 = punkt(start, normale[i]) - dist[i];
        const float d2 = punkt(ende, normale[i]) - dist[i];
        if (d2 > 0) { getout = true; }
        if (d1 > 0) { startout = true; }
        if (d1 > 0 && (d2 >= kEpsilon || d2 >= d1)) {
            kopie(ende, t.endpos);
            return t;
        }
        if (d1 <= 0 && d2 <= 0) { continue; }
        if (d1 > d2) {
            float f = (d1 - kEpsilon) / (d1 - d2);
            if (f < 0) { f = 0; }
            if (f > enterFrac) {
                enterFrac = f;
                clip = i;
            }
        } else {
            float f = (d1 + kEpsilon) / (d1 - d2);
            if (f > 1) { f = 1; }
            if (f < leaveFrac) { leaveFrac = f; }
        }
    }
    t.welt = false;
    if (!startout) {
        t.startsolid = true;
        t.inhalt = kInhaltSchwert;
        if (!getout) {
            t.allsolid = true;
            t.fraction = 0.0F;
        }
    } else if (enterFrac < leaveFrac && enterFrac > -1 && enterFrac < t.fraction) {
        if (enterFrac < 0) { enterFrac = 0; }
        t.fraction = enterFrac;
        if (clip >= 0) { kopie(normale[clip], t.normale); }
        t.inhalt = kInhaltSchwert;
    }
    // CM_Trace: endpos aus dem Anteil
    if (t.fraction == 1.0F) {
        kopie(ende, t.endpos);
    } else {
        for (int i = 0; i < 3; ++i) { t.endpos[i] = start[i] + t.fraction * (ende[i] - start[i]); }
    }
    return t;
}

// --- WP_SaberUpdate: der Kasten des Schwerts --------------------------------
void saberKastenSetzen(SaberFigur& f) {
    SaberKasten& k = f.kasten;
    k = SaberKasten{};
    kopie(f.ort, k.ort);
    // "if ( !self->client->ps.SaberActive() || !self->client->ps.saberBlocking ...)"
    // -> kein Blocken: nur G_SetOrigin, die Masse geloescht. Eine Figur mit
    // brennender Klinge blockt: G_InCinematicSaberAnim faelscht BLK_TIGHT
    // (wp_saber.cpp:7900 OJ, :23610 MD), sonst steht ein Skript-NPC in LS_READY
    // mit BLK_WIDE (saberMoveData, bg_panimate.cpp:127 OJ, :204 MD).
    if (!f.schwertAn()) {
        return;
    }
    // rc601 saber review: "|| PM_SuperBreakLoseAnim( torsoAnim )" - kein
    // Blocken, kein Kasten (wp_saber.cpp:7936 OJ, :23638 MD).
    if (saberSuperBruchVerlust(f.torsoAnim)) {
        return;
    }
    // Movie Duels: "if (self->s.eFlags & EF_NODRAW) saberent->contents = 0"
    // (wp_saber.cpp:23865 MD). OpenJK laesst den Kasten stehen.
    if (g_saberMd && f.unsichtbar) {
        return;
    }
    const bool kino = kinoSchwertAnim(f);
    // Erster Zweig: NPC, nicht beschaeftigt (Jedi_SaberBusy: ein Skript-NPC
    // schwingt nicht), nicht "realistisch", weaponTime <= 0, nicht im
    // Kinoschwert - ein Kasten um den KOERPER, 12 vor ihm (wp_saber.cpp:7987-8005
    // OJ, :23718-23745 MD).
    // rc601 saber review: der Spieler (s.number 0) nimmt diesen Zweig ebenfalls:
    // "|| (self->s.number == 0 && saberBlocking == BLK_WIDE && (g_saberAutoBlocking
    // ->integer || ...))" - g_saberAutoBlocking ist "1" (g_main.cpp:662 OJ, :785 MD),
    // LS_READY blockt BLK_WIDE, und ohne NPC gilt G_InCinematicSaberAnim nie.
    if (((f.npc && saberRealistisch() == 0) || !f.npc) && !kino) {
        float vorn[3];
        const float ang[3] = {0.0F, f.gier, 0.0F};
        vorwaerts(ang, vorn);
        ma(f.ort, 12.0F, vorn, k.ort);
        for (int i = 0; i < 3; ++i) {
            k.mins[i] = f.mins[i] - 8.0F;
            k.maxs[i] = f.maxs[i] + 8.0F;
        }
        k.fest = true;
        return;
    }
    // Sonst: der Kasten um die Klingen. Je Klinge relativ zu IHRER Mitte, der
    // Ursprung bleibt die Mitte der zuletzt bearbeiteten - wie im Spiel.
    float org[3];
    kopie(f.ort, org);
    const int zahl = f.dual ? 2 : 1;
    for (int n = 0; n < zahl; ++n) {
        const SaberSchwert& s = f.schwert[static_cast<std::size_t>(n)];
        for (int b = 0; b < s.numBlades && b < 8; ++b) {
            const SaberKlinge& kl = s.klinge[static_cast<std::size_t>(b)];
            if (kl.laenge <= 0.0F) { continue; }
            float spitze[3];
            ma(kl.mp, kl.laenge, kl.md, spitze);
            ma(kl.mp, kl.laenge * 0.5F, kl.md, org);
            for (int i = 0; i < 3; ++i) {
                float t = spitze[i] - org[i];
                t += t >= 0 ? 8.0F : -8.0F;
                float w = kl.mp[i] - org[i];
                w += w >= 0 ? 8.0F : -8.0F;
                k.maxs[i] = std::max({k.maxs[i], t, w});
                k.mins[i] = std::min({k.mins[i], t, w});
            }
        }
    }
    // "if ( weaponTime > 0 || self->s.number || g_saberAutoBlocking->integer || ... )"
    // - mindestens 16 (32 bei saberBlocked, das hier BLOCKED_NONE ist). rc601
    // saber review: g_saberAutoBlocking "1" gilt auch fuer den Spieler, nicht nur
    // fuer NPCs (vorher stand hier "if (f.npc)").
    for (int i = 0; i < 3; ++i) {
        k.maxs[i] = std::max(k.maxs[i], 16.0F);
        k.mins[i] = std::min(k.mins[i], -16.0F);
    }
    kopie(org, k.ort);
    k.fest = true;
}

// --- Server: WP_SaberDamageTrace, der Teil "Schwert gegen Schwert" ----------
namespace {

struct Trefferstand {
    float hitFraction = 1.0F;      // saberHitFraction
    float hitOrt[3]{};             // saberHitLocation
    float hitNormale[3]{};         // saberHitNormal
    float gekreuzt = -1.0F;        // sabersCrossed
    int hitFigur = -1;             // saberHitEntity -> owner
};

// WP_SabersIntersect (wp_saber.cpp:1796 OJ, :4448 MD), checkDir qfalse.
bool schwerterSchneiden(const SaberFigur& f1, int s1, int b1, const SaberFigur& f2) {
    if (f1.schwertLaenge() <= 0.0F || f2.schwertLaenge() <= 0.0F) { return false; }
    const SaberKlinge& k1 = f1.schwert[static_cast<std::size_t>(s1)].klinge[static_cast<std::size_t>(b1)];
    for (int n = 0; n < 2; ++n) {
        const SaberSchwert& s2 = f2.schwert[static_cast<std::size_t>(n)];
        for (int b = 0; b < s2.numBlades && b < 8; ++b) {
            const SaberKlinge& k2 = s2.klinge[static_cast<std::size_t>(b)];
            if (s2.typ.empty() || s2.typ == "SABER_NONE" || k2.laenge <= 0.0F) { continue; }
            float dir[3];
            float base1[3];
            float baseNext1[3];
            float tip1[3];
            float tipNext1[3];
            kopie(k1.mpAlt, base1);
            kopie(k1.mp, baseNext1);
            minus(k1.mp, k1.mpAlt, dir);
            normieren(dir);
            ma(baseNext1, kSaberExtrapolieren, dir, baseNext1);
            ma(base1, k1.laenge, k1.mdAlt, tip1);
            ma(baseNext1, k1.laenge, k1.md, tipNext1);
            minus(tipNext1, tip1, dir);
            normieren(dir);
            ma(tipNext1, kSaberExtrapolieren, dir, tipNext1);
            float base2[3];
            float baseNext2[3];
            float tip2[3];
            float tipNext2[3];
            kopie(k2.mpAlt, base2);
            kopie(k2.mp, baseNext2);
            minus(k2.mp, k2.mpAlt, dir);
            normieren(dir);
            ma(baseNext2, kSaberExtrapolieren, dir, baseNext2);
            ma(base2, k2.laenge, k2.mdAlt, tip2);
            ma(baseNext2, k2.laenge, k2.md, tipNext2);
            minus(tipNext2, tip2, dir);
            normieren(dir);
            ma(tipNext2, kSaberExtrapolieren, dir, tipNext2);
            if (dreieckeSchneiden(base1, tip1, baseNext1, base2, tip2, baseNext2)) { return true; }
            if (dreieckeSchneiden(base1, tip1, baseNext1, base2, tip2, tipNext2)) { return true; }
            if (dreieckeSchneiden(base1, tip1, tipNext1, base2, tip2, baseNext2)) { return true; }
            if (dreieckeSchneiden(base1, tip1, tipNext1, base2, tip2, tipNext2)) { return true; }
        }
    }
    return false;
}

// WP_SabersDistance (wp_saber.cpp:1925 OJ, :4567 MD): nur saber[0].blade[0].
float schwerterAbstand(const SaberFigur& f1, const SaberFigur& f2) {
    if (f1.schwertLaenge() <= 0.0F || f2.schwertLaenge() <= 0.0F) { return 0.0F; }
    const SaberKlinge& a = f1.schwert[0].klinge[0];
    const SaberKlinge& b = f2.schwert[0].klinge[0];
    float tip1[3];
    float tip2[3];
    float p1[3];
    float p2[3];
    ma(a.mp, a.laenge, a.md, tip1);
    ma(b.mp, b.laenge, b.md, tip2);
    return kuerzesterAbstand(a.mp, tip1, b.mp, tip2, p1, p2);
}

// gi.trace mit MASK_SHOT|CONTENTS_LIGHTSABER (SV_Trace, SV_ClipMoveToEntities
// sv_world.cpp): erst die Welt, dann die Schwertkaesten der anderen. Koerper
// (CONTENTS_BODY mit G2-Kollision) prueft behaved nicht.
struct Spurergebnis {
    SaberSpur t;
    int figur = -2;   // -2 keine (ENTITYNUM_NONE), -1 Welt, sonst Besitzer des Schwertkastens
};
Spurergebnis schussSpur(const std::vector<SaberFigur>& figuren, std::size_t selbst, const float a[3],
                        const float b[3], const float mins[3], const float maxs[3], bool mitSchwertern,
                        const SaberSpurFn& welt) {
    Spurergebnis r;
    r.t = welt ? welt(a, b, kMaskeSchussWelt) : SaberSpur{};
    if (!welt) { kopie(b, r.t.endpos); }
    r.figur = (r.t.fraction < 1.0F) ? -1 : -2;
    if (r.t.allsolid) { return r; }
    if (!mitSchwertern) { return r; }
    for (std::size_t j = 0; j < figuren.size(); ++j) {
        if (r.t.allsolid) { break; }
        if (j == selbst) { continue; }   // "don't clip against own missiles"
        const SaberKasten& k = figuren[j].kasten;
        if (!k.fest) { continue; }
        float kmin[3];
        float kmax[3];
        for (int i = 0; i < 3; ++i) {
            kmin[i] = k.ort[i] + k.mins[i];
            kmax[i] = k.ort[i] + k.maxs[i];
        }
        SaberSpur t = kastenSpur(a, b, mins, maxs, kmin, kmax);
        if (t.allsolid) {
            if (!r.t.allsolid) { r.figur = static_cast<int>(j); }
            r.t.allsolid = true;
        } else if (t.startsolid) {
            if (!r.t.startsolid) { r.figur = static_cast<int>(j); }
            r.t.startsolid = true;
        }
        if (t.fraction < r.t.fraction) {
            const bool altStart = r.t.startsolid;
            const bool altAll = r.t.allsolid;
            r.t = t;
            r.figur = static_cast<int>(j);
            if (altStart) { r.t.startsolid = true; }
            if (altAll) { r.t.allsolid = true; }
        }
    }
    return r;
}

// WP_SaberDamageForTrace (wp_saber.cpp:2447 OJ, :5409 MD), so weit es
// Welt und Schwerter betrifft. true: Wand getroffen.
bool schadensSpur(std::vector<SaberFigur>& figuren, std::size_t ai, const float start[3], const float end[3],
                  bool extrapolieren, int s, int b, Trefferstand& st, const SaberSpurFn& welt) {
    SaberFigur& at = figuren[ai];
    // Nur Movie Duels: "a script-invisible NPC's saber hits nothing"
    // (WP_IsScriptInvisible, wp_saber.cpp:5426 MD).
    if (g_saberMd && at.unsichtbar) { return false; }
    float end2[3];
    kopie(end, end2);
    if (extrapolieren) {
        float diff[3];
        minus(end, start, diff);
        if (g_saberMd) {
            if (normieren(diff) > 0.0F) { ma(end2, kSaberExtrapolieren, diff, end2); }
        } else {
            normieren(diff);
            ma(end2, kSaberExtrapolieren, diff, end2);
        }
    }
    // Groessere Spur (Kasten +-2) fuer Spieler, Verbuendete, Shadowtrooper,
    // Alora, Bosse; ein Griff ohne Klinge nimmt den Radius der Klinge.
    const SaberSchwert& sw = at.schwert[static_cast<std::size_t>(s)];
    float r = 0.0F;
    if (!sw.zweiteArt(b) ? sw.ohneKlinge : sw.ohneKlinge2) {
        r = sw.klinge[static_cast<std::size_t>(b)].radius;
    }
    if (r == 0.0F && (!at.npc || at.team == "TEAM_PLAYER" || at.klasse == "CLASS_SHADOWTROOPER" ||
                      at.klasse == "CLASS_ALORA" || at.boss)) {
        r = kSaberRadiusSchaden;
    }
    const float mins[3] = {-r, -r, -r};
    const float maxs[3] = {r, r, r};
    Spurergebnis tr = schussSpur(figuren, ai, start, end2, mins, maxs, true, welt);
    if (tr.figur == -2) { return false; }
    if (tr.figur == -1) {
        // SFL_BOUNCE_ON_WALLS (MD auch g_saberbounceonwalls "0"): Ort und
        // Normale merken.
        if (sw.werte.prallAnWand) {
            kopie(tr.t.endpos, st.hitOrt);
            kopie(tr.t.normale, st.hitNormale);
        }
        return true;
    }
    const std::size_t oi = static_cast<std::size_t>(tr.figur);
    SaberFigur& owner = figuren[oi];
    if (g_saberMd && owner.unsichtbar) { return false; }
    bool schneiden = schwerterSchneiden(at, s, b, owner);
    const float dist = schwerterAbstand(at, owner);
    if (st.gekreuzt == -1.0F || st.gekreuzt > dist) { st.gekreuzt = dist; }
    const float stufe = static_cast<float>(saberSpielstufe());
    const float kollision = (saberRealistisch() != 0)
                                ? kSaberKollisionsAbstand
                                : kSaberKollisionsAbstand + 6.0F + static_cast<float>(saberSpielstufe()) * 4.0F;
    if (kinoSchwertAnim(owner) && kinoSchwertAnim(at)) { schneiden = true; }
    if (dist > kollision && !schneiden) {
        // "saber hit owner through saber": neu ohne Schwerter; die Welt zaehlt
        // als Wand, ein Koerper waere Schaden (prueft behaved nicht).
        const Spurergebnis tr2 = schussSpur(figuren, ai, start, end2, mins, maxs, false, welt);
        return tr2.figur == -1;
    }
    if ((tr.t.fraction < st.hitFraction || tr.t.startsolid) && dist < (8.0F + stufe) * 4.0F &&
        (schneiden || dist < (4.0F + stufe) * 2.0F)) {
        if (tr.t.allsolid || tr.t.startsolid) {
            const float d = abstand(start, end2);
            if (d > 0.0F) {
                float f = schwerterAbstand(at, owner) / d;
                if (f > 1.0F) { f = 1.0F; }
                if (f < st.hitFraction) { st.hitFraction = f; }
            } else {
                st.hitFraction = 0.0F;
            }
        } else {
            st.hitFraction = tr.t.fraction;
        }
        kopie(tr.t.endpos, st.hitOrt);
        st.hitFigur = static_cast<int>(oi);
    }
    return false;
}

bool keineWirkungTorso(const std::string& t) {
    // "no effects, no damage" (wp_saber.cpp:4636 OJ; MD :8282-8301 dazu
    // BOTH_CIN_70, BOTH_CIN_50, BOTH_MD_CIN_1).
    if (t == "BOTH_A7_HILT") { return true; }
    return g_saberMd && (t == "BOTH_CIN_70" || t == "BOTH_CIN_50" || t == "BOTH_MD_CIN_1");
}

void lerpRichtung(const float von[3], const float nach[3], float t, float o[3]) {
    float a1[3];
    float a2[3];
    float m[3];
    nachWinkeln(von, a1);
    nachWinkeln(nach, a2);
    for (int x = 0; x < 3; ++x) { m[x] = winkelMisch(a1[x], a2[x], t); }
    vorwaerts(m, o);
}

// WP_SaberDamageTrace fuer eine Klinge.
void klingenSpur(std::vector<SaberFigur>& figuren, std::size_t ai, int s, int b, bool keineEffekte,
                 bool keinBlitz, double ms, const SaberSpurFn& welt, std::vector<SaberKlirren>& aus) {
    SaberFigur& ent = figuren[ai];
    SaberKlinge& kl = ent.schwert[static_cast<std::size_t>(s)].klinge[static_cast<std::size_t>(b)];
    Trefferstand st;
    if (kl.laenge <= 1.0F) { return; }   // "cen get down to 1 when in a wall"
    if (!ent.hatAlt) { return; }          // "just started up the saber?"
    // Unter Wasser geht die Klinge aus (blade.active = qfalse) - behaved
    // schaltet die Szene nicht um, es gibt dann nur kein Klirren.
    // rc601 saber review: gi.pointcontents( renderInfo.muzzlePoint ) - das ist
    // die Lage von saber[0].blade[0] (nur sie schreibt CG_AddSaberBladeGo nach
    // renderInfo, cg_players.cpp:14207 MD, 6348 OJ), nicht die dieser Klinge.
    if (!ent.schwert[static_cast<std::size_t>(s)].werte.imWasserAn && welt) {
        const float* mp00 = ent.schwert[0].klinge[0].mp;
        const SaberSpur w = welt(mp00, mp00, kInhaltWasser | kInhaltSchleim | kInhaltLava);
        if (w.startsolid || w.allsolid) { return; }
    }
    if (keineWirkungTorso(ent.torsoAnim)) { return; }
    // Ohne G_InCinematicSaberAnim (und ohne WEAPON_FIRING, das ein Skript-NPC
    // nie hat: PM_WeaponLightsaber, bg_pmove.cpp) faellt jedes Klirren durch
    // das Tor unten - die Spuren aendern dann nichts Sichtbares.
    if (!kinoSchwertAnim(ent)) { return; }
    float mp1[3];
    float md1[3];
    float mp2[3];
    float md2[3];
    kopie(kl.mpAlt, mp1);
    kopie(kl.mdAlt, md1);
    kopie(kl.mp, mp2);
    kopie(kl.md, md2);
    float baseOld[3];
    float endOld[3];
    float baseNew[3];
    float endNew[3];
    kopie(mp1, baseOld);
    ma(baseOld, kl.laenge, md1, endOld);
    kopie(mp2, baseNew);
    ma(baseNew, kl.laenge, md2, endNew);
    if (vergleich2(baseOld, baseNew) && vergleich2(endOld, endNew)) {
        (void)schadensSpur(figuren, ai, mp2, endNew, false, s, b, st, welt);
    } else {
        bool wand = schadensSpur(figuren, ai, baseOld, baseNew, true, s, b, st, welt);
        if (st.hitFraction < 1.0F) {
            lerpRichtung(md1, md2, st.hitFraction, md2);
            float diff[3];
            minus(mp2, mp1, diff);
            ma(mp1, st.hitFraction, diff, baseNew);
            ma(baseNew, kl.laenge, md2, endNew);
        }
        const std::string& torso = ent.torsoAnim;
        float curDirFrac = 1.0F;
        if (saberSonderAngriff(torso) || drehendeAnim(torso) || sonderSprung(torso)) {
            curDirFrac = punkt(md1, md2);
        }
        float dirInc = 0.0F;
        constexpr float kMaxSchwung = 0.33F;   // MAX_SABER_SWING_INC
        if (std::fabs(curDirFrac) < 1.0F - kMaxSchwung) {
            curDirFrac = dirInc = 1.0F / ((1.0F - curDirFrac) / kMaxSchwung);
        } else {
            curDirFrac = 1.0F;
            dirInc = 0.0F;
        }
        bool hitSaber = false;
        float curMD2[3];
        float curBase2[3];
        kopie(md1, curMD2);
        kopie(baseOld, curBase2);
        for (;;) {
            float curMD1[3];
            float curBase1[3];
            kopie(curMD2, curMD1);
            kopie(curBase2, curBase1);
            if (curDirFrac >= 1.0F) {
                kopie(md2, curMD2);
                kopie(baseNew, curBase2);
            } else {
                lerpRichtung(md1, md2, curDirFrac, curMD2);
                float diff[3];
                minus(baseNew, baseOld, diff);
                ma(baseOld, curDirFrac, diff, curBase2);
            }
            for (float step = 8.0F; step < kl.laenge && step < kl.laengeAlt; step += 12.0F) {
                float alt[3];
                float neu[3];
                ma(curBase1, step, curMD1, alt);
                ma(curBase2, step, curMD2, neu);
                if (schadensSpur(figuren, ai, alt, neu, true, s, b, st, welt)) { wand = true; }
                if (st.hitFraction < 1.0F) {
                    float diff[3];
                    minus(mp2, mp1, diff);
                    ma(mp1, st.hitFraction, diff, baseNew);
                    ma(baseNew, kl.laenge, curMD2, endNew);
                    lerpRichtung(curMD1, curMD2, st.hitFraction, curMD2);
                    hitSaber = true;
                }
                if (wand) { break; }
            }
            if (wand || hitSaber) { break; }
            if (curDirFrac >= 1.0F) { break; }
            curDirFrac += dirInc;
            if (curDirFrac >= 1.0F) { curDirFrac = 1.0F; }
        }
        (void)schadensSpur(figuren, ai, endOld, endNew, false, s, b, st, welt);
    }
    // Das Tor: (saberHitFraction < 1 || sabersCrossed in [0, 32]) &&
    // (WEAPON_FIRING || saberInFlight || G_InCinematicSaberAnim(ent)) -
    // wirksam wird es nur mit saberHitFraction < 1 (wp_saber.cpp:5074, 5580 OJ;
    // :8768, :9327 MD). Saberlocks und Abgleiten (WP_SabersCheckLock,
    // WP_GetSaberDeflectionAngle) aendern Animationen und sind nicht
    // nachgebaut: es bleibt beim Block (WP_SaberBlockSound/-Effect).
    if (st.hitFraction >= 1.0F || st.hitFigur < 0) {
        return;
    }
    const SaberSchwert& sw = ent.schwert[static_cast<std::size_t>(s)];
    const bool zweite = sw.zweiteArt(b);
    SaberKlirren k;
    k.ms = ms;
    k.figur = ent.figur;
    k.gegner = figuren[static_cast<std::size_t>(st.hitFigur)].figur;
    k.schwert = s;
    k.klinge = b;
    kopie(st.hitOrt, k.ort);
    kopie(st.hitNormale, k.normale);
    kopie(ent.ort, k.klangOrt);
    const std::uint64_t saat = saberSaat(static_cast<std::uint64_t>(ms), static_cast<std::uint64_t>(ent.figur),
                                         static_cast<std::uint64_t>(s * 8 + b), 0x6b6c6972ULL);
    // WP_SaberBlockSound (wp_saber.cpp:747 OJ, :2178 MD): erst die Wuerfe,
    // dann blockSound[Q_irand(0,2)] - ein leerer Eintrag ist Index 0, still.
    const int index = g_saberMd ? saberWurf(saat, 1, 90) : saberWurf(saat, 1, 9);
    const int klassisch = saberWurf(saat ^ 0x55ULL, 1, 30);
    const std::array<std::string, 3>& eigen = zweite ? sw.werte.blockKlang2 : sw.werte.blockKlang;
    if (!eigen[0].empty()) {
        k.klang = eigen[static_cast<std::size_t>(saberWurf(saat ^ 0x77ULL, 0, 2))];
    } else if (g_saberMd) {
        k.klang = (sw.typ == "SABER_SINGLE_CLASSIC")
                      ? "sound/weapons/saber/classicblock" + std::to_string(klassisch) + ".mp3"
                      : "sound/weapons/saber/saberblock" + std::to_string(index) + ".mp3";
    } else {
        k.klang = "sound/weapons/saber/saberblock" + std::to_string(index) + ".wav";
    }
    // WP_SaberBlockEffect (wp_saber.cpp:2348 OJ, :5087 MD), cutNotBlock
    // qfalse; g_SerenityJediEngineMode "0" -> saber/saber_block.
    if (!keineEffekte) {
        const std::string& eigenFx = zweite ? sw.werte.blockEffekt2 : sw.werte.blockEffekt;
        k.effekt = !eigenFx.empty() ? eigenFx : std::string("saber/saber_block");
    }
    k.blitz = !keinBlitz;
    aus.push_back(std::move(k));
}

}  // namespace

void saberServerTakt(std::vector<SaberFigur>& figuren, double ms, const SaberSpurFn& welt,
                     std::vector<SaberKlirren>& aus) {
    for (std::size_t i = 0; i < figuren.size(); ++i) {
        SaberFigur& f = figuren[i];
        // ClientThink_real: WP_SaberUpdate (g_active.cpp:10892 MD) ...
        saberKastenSetzen(f);
        // rc601 saber review: WP_SabersDamageTrace kehrt bei PM_SuperBreakLoseAnim
        // sofort zurueck (wp_saber.cpp:12698 MD, 5683 OJ).
        const bool keineSpur = saberSuperBruchVerlust(f.torsoAnim);
        // ... dann ClientEvents -> WP_SabersDamageTrace (g_active.cpp:2474 MD,
        // :1832 OJ) und WP_SaberUpdateOldBladeData.
        const int zahl = keineSpur ? 0 : (f.dual ? 2 : 1);
        for (int n = 0; n < zahl; ++n) {
            const SaberSchwert& sw = f.schwert[static_cast<std::size_t>(n)];
            for (int b = 0; b < sw.numBlades && b < 8; ++b) {
                const SaberKlinge& kl = sw.klinge[static_cast<std::size_t>(b)];
                if (!kl.aktiv && kl.laenge == 0.0F) { continue; }
                bool keineEffekte = false;
                if (b != 0 && (sw.typ == "SABER_BROAD" || sw.typ == "SABER_SAI" || sw.typ == "SABER_CLAW")) {
                    keineEffekte = true;
                }
                const bool keinBlitz = sw.zweiteArt(b) ? sw.werte.keinBlitz2 : sw.werte.keinBlitz;
                klingenSpur(figuren, i, n, b, keineEffekte, keinBlitz, ms, welt, aus);
            }
        }
        // WP_SaberUpdateOldBladeData
        for (SaberSchwert& sw : f.schwert) {
            for (int b = 0; b < sw.numBlades && b < 8; ++b) {
                SaberKlinge& kl = sw.klinge[static_cast<std::size_t>(b)];
                kopie(kl.mp, kl.mpAlt);
                kopie(kl.md, kl.mdAlt);
                kl.laengeAlt = kl.laenge;
            }
        }
        f.hatAlt = true;
    }
}

// --- Client: CG_AddSaberBladeGo ---------------------------------------------
float saberZeichenLaenge(float l, float lMax) noexcept {
    if (l < lMax) {
        if (l < lMax - 8.0F) {
            return l + 8.0F;
        }
        return lMax;
    }
    return l;
}

SaberWandBild saberWandTakt(SaberWandKlinge& k, SaberWandFigur& f, const SaberWandEingabe& in,
                            const SaberSpurFn& welt, std::vector<SaberWandEreignis>& aus) {
    SaberWandBild bild;
    // CG_Player: wachsen und schrumpfen (cg_players.cpp:7326-7389 OJ):
    // lengthMax/10 * cg.frametime/100 je Bild.
    const float schritt = in.laengeMax / 10.0F * static_cast<float>(kSaberClientTaktMs) / 100.0F;
    if (!in.aktiv || k.laenge > in.laengeMax) {
        if (k.laenge > 0.0F) { k.laenge -= schritt; }
    } else if (k.laenge < in.laengeMax) {
        if (in.superBruchSieg) {
            // "just keep it full length!" (cg_players.cpp:7371 OJ, :16567 MD)
            k.laenge = in.laengeMax;
        } else {
            // rc601 saber review 2b: Movie Duels zuendet SS_FAST doppelt so
            // schnell - "lengthMax / 5 * cg.frametime / 100 * cg_ignitionSpeed.value"
            // (cg_players.cpp:16578 MD; cg_ignitionSpeed "1", cg_main.cpp:711 MD).
            // Alle anderen Stile /10 (SS_STAFF mit cg_ignitionSpeedstaff "1"),
            // OpenJK immer /10. rc601 integ: der Teiler aus saberZuendTeiler
            // (dieselbe Rechnung wie vorher, Bit fuer Bit; nutzt auch der Ton).
            k.laenge += in.laengeMax / saberZuendTeiler(in.stil) * static_cast<float>(kSaberClientTaktMs) / 100.0F;
        }
        if (k.laenge > in.laengeMax) { k.laenge = in.laengeMax; }
    }
    // rc601 saber review: kein Kappen auf 0 - die Engine zieht ab, solange
    // length > 0, und laesst den Rest unter null stehen (cg_players.cpp:16233 MD,
    // 7333 OJ); das naechste Zuenden waechst von dort.
    if (k.laenge <= 0.0F) {
        return bild;
    }
    bild.gezeichnet = true;
    const float lokal = saberZeichenLaenge(k.laenge, in.laengeMax);
    bild.zeichenLaenge = lokal;
    if (!welt) {
        return bild;
    }
    float ende[3];
    ma(in.org, lokal, in.achse, ende);
    for (int i = 0; i < 3; ++i) { ende[i] += in.achse[i]; }   // VectorAdd( end, axis[0], end )
    // "Saber is on the other side of a wall": vom Ursprung der Figur zum
    // Griff, nur CONTENTS_SOLID.
    const SaberSpur wurzel = welt(in.wurzelOrt, in.org, kInhaltFest);
    if (wurzel.fraction < 1.0F) {
        k.laenge = 0.1F;
        return bild;
    }
    // noIdleEffect: "idle, do no marks" - ohne saberMove zaehlt nur der
    // Sonderangriff im Oberkoerper.
    const bool keineSpuren = in.keinLeerlauf && !in.angriff;
    const SaberSpur t = welt(in.org, ende, kMaskeFest | kInhaltWasser | kInhaltSchleim);
    const auto wurf = [&in](std::uint64_t salz, int lo, int hi) { return saberWurf(in.saat ^ salz, lo, hi); };
    if (t.fraction < 1.0F) {
        if ((t.inhalt & (kInhaltWasser | kInhaltSchleim)) != 0U) {
            if (!keineSpuren && wurf(0x11ULL, 0, 10) == 0) {
                float ort[3];
                kopie(t.endpos, ort);
                ort[2] += 4.0F;
                if (wurf(0x12ULL, 1, std::max(in.numBlades, 1)) == 1) {
                    SaberWandEreignis e;
                    e.art = SaberWandEreignis::Art::Kochen;
                    e.ms = in.ms;
                    kopie(ort, e.ort);
                    e.normale[2] = 1.0F;
                    e.name = "saber/boil";
                    aus.push_back(e);
                    SaberWandEreignis kl = e;
                    kl.art = SaberWandEreignis::Art::Klang;
                    kl.name = "sound/weapons/saber/hitwater.wav";
                    aus.push_back(kl);
                }
            }
        } else if (!keineSpuren) {
            const bool spurenErlaubt = !in.keineWandspuren;
            float normale[3];
            kopie(t.normale, normale);
            if (g_saberMd && normieren(normale) == 0.0F) {
                normale[0] = normale[1] = 0.0F;
                normale[2] = 1.0F;
            }
            const bool funke = g_saberMd ? (wurf(0x21ULL, 0, 3) == 0)
                                         : (wurf(0x21ULL, 1, std::max(in.numBlades, 1)) == 1);
            if (spurenErlaubt && (t.flaeche & kFlaecheKeinEinschlag) == 0U && t.welt && funke) {
                SaberWandEreignis e;
                e.art = SaberWandEreignis::Art::Funken;
                e.ms = in.ms;
                kopie(t.endpos, e.ort);
                kopie(normale, e.normale);
                e.name = "sparks/spark_nosnd";
                aus.push_back(e);
            }
            if (k.hatAltePos) {
                // "only put marks on architecture": ENTITYNUM_WORLD (EF_PERMANENT
                // und ET_TERRAIN gibt es in behaved nicht) - keine Brush-Entities.
                if (t.welt && !t.brushModell && spurenErlaubt) {
                    SaberWandEreignis sp;
                    sp.art = SaberWandEreignis::Art::Spur;
                    sp.ms = in.ms;
                    kopie(k.altePos, sp.von);
                    kopie(t.endpos, sp.ort);
                    kopie(normale, sp.normale);
                    aus.push_back(sp);
                    const bool klang = g_saberMd ? (wurf(0x31ULL, 0, 3) == 0)
                                                 : (wurf(0x31ULL, 1, std::max(in.numBlades, 1)) == 1);
                    if (klang && in.ms - f.klangSperreMs >= 100.0) {
                        f.klangSperreMs = in.ms;
                        SaberWandEreignis kl;
                        kl.art = SaberWandEreignis::Art::Klang;
                        kl.ms = in.ms;
                        kopie(in.wurzelOrt, kl.ort);   // cent->lerpOrigin, CHAN_ITEM
                        if (g_saberMd && in.angriff) {
                            kl.name = "sound/weapons/saber/saberstrikewall" + std::to_string(wurf(0x32ULL, 1, 17)) +
                                      ".mp3";
                        } else {
                            kl.name = "sound/weapons/saber/saberhitwall" + std::to_string(wurf(0x32ULL, 1, 3)) +
                                      ".wav";
                        }
                        aus.push_back(kl);
                    }
                }
            } else {
                k.hatAltePos = true;
            }
        }
        // rc601 saber review: im Wasserzweig steht "i = 1" (cg_players.cpp:14364
        // MD, 6458 OJ) - das VectorCopy trifft dann trail.oldPos[1], nicht den
        // Platz [0], den die Brandspuren lesen; gekuerzt wird nur bei "!i".
        const bool wasser = (t.inhalt & (kInhaltWasser | kInhaltSchleim)) != 0U;
        if (!wasser) {
            kopie(t.endpos, k.altePos);
            kopie(t.normale, k.alteNormale);
        }
        if (!wasser && (t.inhalt & (kInhaltFest | kInhaltGelaende | kInhaltSchussSperre)) != 0U) {
            k.laenge = k.laenge * t.fraction;
            if (k.laenge <= 0.1F) { k.laenge = 0.1F; }
        }
    } else {
        k.hatAltePos = false;
    }
    return bild;
}

}  // namespace bhed
