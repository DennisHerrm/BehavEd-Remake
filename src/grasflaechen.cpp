// grasflaechen.cpp - surfaceSprites wie im Spiel (rc604). Siehe den Kopf
// von bhed/grasflaechen.h.
//
// Zeilenangaben ohne Datei: tr_surfacesprites.cpp, OpenJK code/rd-vanilla
// (Movie Duels code/rd-vanilla: dieselbe Rechnung, Variablen umbenannt -
// randomchart, randomindex, ss_view_right ...). Die Typen folgen der Engine:
// float, wo sie float rechnet, und double, wo ein Literal wie 0.02 oder
// 1.0 die Rechnung nach double hebt - sonst kippt an der Grenze ein Halm.
#include "bhed/grasflaechen.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace bhed::gras {

// clang-format off
const float kZufall[256] = {
    0.6554F, 0.6909F, 0.4806F, 0.6218F, 0.5717F, 0.3896F, 0.0677F, 0.7356F,
    0.8333F, 0.1105F, 0.4445F, 0.8161F, 0.4689F, 0.0433F, 0.7152F, 0.0336F,
    0.0186F, 0.9140F, 0.1626F, 0.6553F, 0.8340F, 0.7094F, 0.2020F, 0.8087F,
    0.9119F, 0.8009F, 0.1339F, 0.8492F, 0.9173F, 0.5003F, 0.6012F, 0.6117F,
    0.5525F, 0.5787F, 0.1586F, 0.3293F, 0.9273F, 0.7791F, 0.8589F, 0.4985F,
    0.0883F, 0.8545F, 0.2634F, 0.4727F, 0.3624F, 0.1631F, 0.7825F, 0.0662F,
    0.6704F, 0.3510F, 0.7525F, 0.9486F, 0.4685F, 0.1535F, 0.1545F, 0.1121F,
    0.4724F, 0.8483F, 0.3833F, 0.1917F, 0.8207F, 0.3885F, 0.9702F, 0.9200F,
    0.8348F, 0.7501F, 0.6675F, 0.4994F, 0.0301F, 0.5225F, 0.8011F, 0.1696F,
    0.5351F, 0.2752F, 0.2962F, 0.7550F, 0.5762F, 0.7303F, 0.2835F, 0.4717F,
    0.1818F, 0.2739F, 0.6914F, 0.7748F, 0.7640F, 0.8355F, 0.7314F, 0.5288F,
    0.7340F, 0.6692F, 0.6813F, 0.2810F, 0.8057F, 0.0648F, 0.8749F, 0.9199F,
    0.1462F, 0.5237F, 0.3014F, 0.4994F, 0.0278F, 0.4268F, 0.7238F, 0.5107F,
    0.1378F, 0.7303F, 0.7200F, 0.3819F, 0.2034F, 0.7157F, 0.5552F, 0.4887F,
    0.0871F, 0.3293F, 0.2892F, 0.4545F, 0.0088F, 0.1404F, 0.0275F, 0.0238F,
    0.0515F, 0.4494F, 0.7206F, 0.2893F, 0.6060F, 0.5785F, 0.4182F, 0.5528F,
    0.9118F, 0.8742F, 0.3859F, 0.6030F, 0.3495F, 0.4550F, 0.9875F, 0.6900F,
    0.6416F, 0.2337F, 0.7431F, 0.9788F, 0.6181F, 0.2464F, 0.4661F, 0.7621F,
    0.7020F, 0.8203F, 0.8869F, 0.2145F, 0.7724F, 0.6093F, 0.6692F, 0.9686F,
    0.5609F, 0.0310F, 0.2248F, 0.2950F, 0.2365F, 0.1347F, 0.2342F, 0.1668F,
    0.3378F, 0.4330F, 0.2775F, 0.9901F, 0.7053F, 0.7266F, 0.4840F, 0.2820F,
    0.5733F, 0.4555F, 0.6049F, 0.0770F, 0.4760F, 0.6060F, 0.4159F, 0.3427F,
    0.1234F, 0.7062F, 0.8569F, 0.1878F, 0.9057F, 0.9399F, 0.8139F, 0.1407F,
    0.1794F, 0.9123F, 0.9493F, 0.2827F, 0.9934F, 0.0952F, 0.4879F, 0.5160F,
    0.4118F, 0.4873F, 0.3642F, 0.7470F, 0.0866F, 0.5172F, 0.6365F, 0.2676F,
    0.2407F, 0.7223F, 0.5761F, 0.1143F, 0.7137F, 0.2342F, 0.3353F, 0.6880F,
    0.2296F, 0.6023F, 0.6027F, 0.4138F, 0.5408F, 0.9859F, 0.1503F, 0.7238F,
    0.6054F, 0.2477F, 0.6804F, 0.1432F, 0.4540F, 0.9776F, 0.8762F, 0.7607F,
    0.9025F, 0.9807F, 0.0652F, 0.8661F, 0.7663F, 0.2586F, 0.3994F, 0.0335F,
    0.7328F, 0.0166F, 0.9589F, 0.4348F, 0.5493F, 0.7269F, 0.6867F, 0.6614F,
    0.6800F, 0.7804F, 0.5591F, 0.8381F, 0.0910F, 0.7573F, 0.8985F, 0.3083F,
    0.3188F, 0.8481F, 0.2356F, 0.6736F, 0.4770F, 0.4560F, 0.6266F, 0.4677F
};
// clang-format on

float qRsqrt(float zahl) noexcept {
    // q_math.c Q_rsqrt (OpenJK :360, MD :375): 0x5f3759df und EIN
    // Newton-Schritt - genau dieser Fehler bestimmt die Schrittweite.
    const float x2 = zahl * 0.5F;
    std::int32_t i = 0;
    std::memcpy(&i, &zahl, sizeof(i));
    i = 0x5f3759df - (i >> 1);
    float y = 0.0F;
    std::memcpy(&y, &i, sizeof(y));
    y = y * (1.5F - (x2 * y * y));
    return y;
}

std::uint8_t alsByte(float v) noexcept {
    // MSVC: cvttss2si nach int32, dann die unteren acht Bit. Ausserhalb von
    // int32 liefert der Befehl 0x80000000 - also 0.
    if (!(v > -2147483648.0F && v < 2147483648.0F)) { return 0U; }
    return static_cast<std::uint8_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(v)) & 0xFFU);
}

namespace {

constexpr double kFadeRange = 250.0;   // FADE_RANGE, :328

struct Faden {
    // :574-586 (und gleich in :950, :1225)
    float cutdist2 = 0.0F;
    float fadedist2 = 0.0F;
    float invFadediff = 0.0F;
    float faderange = 1.0F;
};

Faden faden(const SurfaceSprite& ss, const Rahmen& r) {
    Faden f;
    const float cutdist = ss.fadeMax * r.reichweite;
    const float fadedist = ss.fadeDist * r.reichweite;
    f.cutdist2 = cutdist * cutdist;
    f.fadedist2 = fadedist * fadedist;
    f.invFadediff = static_cast<float>(1.0 / static_cast<double>(f.cutdist2 - f.fadedist2));
    f.faderange = static_cast<float>(kFadeRange / static_cast<double>(cutdist - fadedist));
    if (f.faderange > 1.0F) { f.faderange = 1.0F; }   // :583
    return f;
}

// SSVertAlpha (:592)
float eckAlpha(const Faden& f, const Rahmen& r, const float* xyz) {
    const float d[3] = {r.auge[0] - xyz[0], r.auge[1] - xyz[1], r.auge[2] - xyz[2]};
    const float l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    return 1.0F - (l2 - f.fadedist2) * f.invFadediff;
}

// Die Kopfrechnung jeder Dreiecksschleife (:710-726): Schrittweite und die
// zwei Zufallszahlen aus den Ecken. false = das Dreieck ist zu klein.
bool kopf(const Ecke e[3], float dichte, float& step, std::uint8_t& index, std::uint8_t& abstand) {
    const float* v1 = e[0].xyz;
    const float* v2 = e[1].xyz;
    const float* v3 = e[2].xyz;
    const float vec1to2[2] = {v2[0] - v1[0], v2[1] - v1[1]};
    const float vec1to3[2] = {v3[0] - v1[0], v3[1] - v1[1]};
    float triarea = vec1to3[0] * vec1to2[1] - vec1to3[1] * vec1to2[0];
    triarea = std::fabs(triarea);
    if (triarea <= 1.0F) { return false; }   // :719 "Insanely small abhorrent triangle"
    step = dichte * qRsqrt(triarea);          // :723
    index = alsByte(v1[0] + v1[1] + v2[0] + v2[1] + v3[0] + v3[1]);           // :725
    abstand = static_cast<std::uint8_t>(alsByte(v1[0] + v2[1] + v3[2]) | 0x03U);   // :726
    return true;
}

void punkt(const Ecke e[3], float fa, float fb, float fc, float out[3]) {
    // VectorScale(v1, fa) ; VectorMA(fb, v2) ; VectorMA(fc, v3)  (:776)
    for (int k = 0; k < 3; ++k) {
        float p = e[0].xyz[k] * fa;
        p = p + fb * e[1].xyz[k];
        p = p + fc * e[2].xyz[k];
        out[k] = p;
    }
}

void viereck(const float pts[16], float licht, float alpha, std::vector<Quad>& aus) {
    // SQuickSprite.Add (tr_quicksprite.cpp): vier Punkte im Abstand 4.
    Quad q;
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 3; ++k) { q.ecke[i][k] = pts[i * 4 + k]; }
    }
    q.licht = alsByte(licht);                                            // (byte)light
    q.alpha = alsByte(static_cast<float>(static_cast<double>(alpha) * 255.0));   // (byte)(alpha*255.0)
    aus.push_back(q);
}

// RB_VerticalSurfaceSprite (:342). Ohne Windpunkt (r_windPointForce 0).
void senkrecht(const float loc[3], float width, float height, float licht, float alpha, float wind, float windidle,
               int hangdown, const float skew[2], bool flattened, int rvc, const Rahmen& r, std::vector<Quad>& aus) {
    float loc2[3];
    const float angle = static_cast<float>((loc[0] + loc[1]) * 0.02 + (r.zeitMs * 0.0015));   // :351
    if (windidle > 0.0) {
        const float windsway = static_cast<float>(height * windidle * 0.075);              // :355
        loc2[0] = loc[0] + skew[0] + std::cos(angle) * windsway;
        loc2[1] = loc[1] + skew[1] + std::sin(angle) * windsway;
    } else {
        loc2[0] = loc[0] + skew[0];
        loc2[1] = loc[1] + skew[1];
    }
    loc2[2] = (hangdown != 0) ? loc[2] - height : loc[2] + height;                        // :359/372
    if (wind > 0.0 && r.windTempo > 0.001) {                                              // :382
        float windsway = static_cast<float>(height * wind * 0.075);
        for (int k = 0; k < 3; ++k) { loc2[k] = loc2[k] + (height * wind) * r.grasWind[k]; }   // VectorMA
        if (r.windTempo < 40.0) {
            windsway = static_cast<float>(windsway * (r.windTempo * (1.0 / 100.0)));
        } else {
            windsway *= 0.4F;
        }
        loc2[2] = static_cast<float>(loc2[2] + std::sin(angle * 2.5) * windsway);          // :397
    }
    float right[3];
    if (flattened) {
        // :400 - DEG2RAD(loc[0]): die x-Koordinate als Winkel
        right[0] = static_cast<float>(std::sin(((loc[0]) * 3.14159265358979323846) / 180.0F) * width);
        right[1] = static_cast<float>(std::cos(((loc[0]) * 3.14159265358979323846) / 180.0F) * height);
        right[2] = 0.0F;
    } else {
        const float s = static_cast<float>(width * 0.5);
        for (int k = 0; k < 3; ++k) { right[k] = r.rechts[rvc][k] * s; }   // :408
    }
    float p[16];
    p[0] = loc[0] + right[0];   // unten rechts
    p[1] = loc[1] + right[1];
    p[2] = loc[2] + right[2];
    p[3] = 0.0F;
    p[4] = loc2[0] + right[0];  // oben rechts
    p[5] = loc2[1] + right[1];
    p[6] = loc2[2] + right[2];
    p[7] = 0.0F;
    p[8] = static_cast<float>(loc2[0] - right[0] + r.vorn[0] * width * 0.2);   // oben links (:432)
    p[9] = static_cast<float>(loc2[1] - right[1] + r.vorn[1] * width * 0.2);
    p[10] = loc2[2] - right[2];
    p[11] = 0.0F;
    p[12] = loc[0] - right[0];  // unten links
    p[13] = loc[1] - right[1];
    p[14] = loc[2] - right[2];
    p[15] = 0.0F;
    viereck(p, licht, alpha, aus);
}

// RB_OrientedSurfaceSprite (:845) und RB_EffectSurfaceSprite (:1121) -
// dieselben Ecken.
void ausgerichtet(const float loc[3], float width, float height, float licht, float alpha, int faceup, const Rahmen& r,
                  std::vector<Quad>& aus) {
    float p[16];
    if (faceup != 0) {
        width = static_cast<float>(width * 0.5);
        height = static_cast<float>(height * 0.5);
        (void)height;
        p[0] = loc[0] + width;  p[1] = loc[1] - width;  p[2] = static_cast<float>(loc[2] + 1.0);
        p[4] = loc[0] + width;  p[5] = loc[1] + width;  p[6] = static_cast<float>(loc[2] + 1.0);
        p[8] = loc[0] - width;  p[9] = loc[1] + width;  p[10] = static_cast<float>(loc[2] + 1.0);
        p[12] = loc[0] - width; p[13] = loc[1] - width; p[14] = static_cast<float>(loc[2] + 1.0);
    } else {
        float loc2[3];
        float right[3];
        const float s = static_cast<float>(width * 0.5);
        for (int k = 0; k < 3; ++k) {
            loc2[k] = loc[k] + height * r.oben[k];   // VectorMA(loc, height, ssViewUp)
            right[k] = r.links[k] * s;              // VectorScale(ssViewRight, width*0.5)
        }
        for (int k = 0; k < 3; ++k) {
            p[0 + k] = loc[k] + right[k];
            p[4 + k] = loc2[k] + right[k];
            p[8 + k] = loc2[k] - right[k];
            p[12 + k] = loc[k] - right[k];
        }
    }
    p[3] = p[7] = p[11] = p[15] = 0.0F;
    viereck(p, licht, alpha, aus);
}

bool eckenPassen(const SurfaceSprite& ss, const Ecke e[3]) {
    for (int i = 0; i < 3; ++i) {
        const float nz = e[i].normalZ;
        if (ss.typ == 1 || ss.typ == 5) {
            // :635 - facing (auch ssFaceup/ssAnyangle) heisst hier: haengen
            if (ss.facing != 0) {
                if (nz > -0.5) { return false; }
            } else if (nz < 0.5) {
                return false;
            }
        } else {
            const float minnormal = (ss.facing != 0) ? 0.99F : 0.5F;   // :964
            if (nz < minnormal) { return false; }
        }
    }
    return true;
}

}  // namespace

void rahmenFertig(Rahmen& r) noexcept {
    // :165 CrossProduct(ssViewRight, up, ssfwdvector)
    const float up[3] = {0.0F, 0.0F, 1.0F};
    r.vorn[0] = r.links[1] * up[2] - r.links[2] * up[1];
    r.vorn[1] = r.links[2] * up[0] - r.links[0] * up[2];
    r.vorn[2] = r.links[0] * up[1] - r.links[1] * up[0];
    // :168-183 - 10 Grad vor, 30 zurueck, 30 vor, 10 zurueck
    const float s[4] = {0.985F, 0.866F, 0.866F, 0.985F};
    const float v[4] = {0.174F, -0.5F, 0.5F, -0.174F};
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 3; ++k) { r.rechts[i][k] = r.links[k] * s[i] + v[i] * r.vorn[k]; }
    }
}

float reichweiteFaktor(float fovX, float standardFovX) noexcept {
    // :147-160 (der Standard ist schon gesetzt)
    constexpr double kGrad = 3.14159265358979323846 / 180.0F;
    if (standardFovX == fovX) { return 1.0F; }
    const float standardscalex = static_cast<float>(std::tan(standardFovX * 0.5 * kGrad));
    if (fovX > 135.0F) { return static_cast<float>(standardscalex / std::tan(135.0F * 0.5F * kGrad)); }
    return static_cast<float>(standardscalex / std::tan(fovX * 0.5 * kGrad));
}

void windSetzen(float tempo, const float richtung[3], Rahmen& r) noexcept {
    // :197-200 - R_GetWindSpeed liefert immer true; targetspeed *= 0.02
    const float targetspeed = static_cast<float>(tempo * 0.02F);
    // :237-241 - vectoangles der waagerechten Windrichtung
    float yaw = 0.0F;
    if (richtung[0] != 0.0F || richtung[1] != 0.0F) {
        yaw = static_cast<float>(std::atan2(richtung[1], richtung[0]) * 180.0 / 3.14159265358979323846);
        if (yaw < 0.0F) { yaw += 360.0F; }
    }
    float pitch = static_cast<float>(-90.0 + targetspeed);   // :248
    if (pitch > -45.0) { pitch = -45.0F; }
    // AngleVectors(ang, forward): (cp*cy, cp*sy, -sp)
    constexpr double kGrad = 3.14159265358979323846 / 180.0;
    const float sp = static_cast<float>(std::sin(pitch * kGrad));
    const float cp = static_cast<float>(std::cos(pitch * kGrad));
    const float sy = static_cast<float>(std::sin(yaw * kGrad));
    const float cy = static_cast<float>(std::cos(yaw * kGrad));
    // :262-263 targetWindGrassDir, z - 1
    r.grasWind[0] = cp * cy;
    r.grasWind[1] = cp * sy;
    r.grasWind[2] = -sp - 1.0F;
    // :267-268, :286 targetWindBlowVect * curWindSpeed (eingeschwungen:
    // curWindSpeed = targetspeed, :279-282)
    r.windTempo = targetspeed;
    r.blasWind[0] = cy * targetspeed;
    r.blasWind[1] = sy * targetspeed;
    r.blasWind[2] = 0.0F;
}

void kandidaten(const SurfaceSprite& ss, const Ecke e[3], std::vector<Kandidat>& aus) {
    if (ss.typ != 1 && ss.typ != 2 && ss.typ != 5) { return; }
    if (!eckenPassen(ss, e)) { return; }
    float step = 0.0F;
    std::uint8_t randomindex = 0;
    std::uint8_t randominterval = 0;
    if (!kopf(e, ss.density, step, randomindex, randominterval)) { return; }
    const bool senk = (ss.typ != 2);
    std::uint8_t rightvectorcount = 0;   // :727
    for (float posi = 0.0F; posi < 1.0; posi += step) {
        for (float posj = 0.0F; posj < (1.0 - posi); posj += step) {
            float fa = 0.0F;
            float fb = 0.0F;
            if (senk) {
                // :733-745 - beide Zufallszahlen vor den Abbruechen
                fa = posi + kZufall[randomindex] * step;
                randomindex = static_cast<std::uint8_t>(randomindex + randominterval);
                fb = posj + kZufall[randomindex] * step;
                randomindex = static_cast<std::uint8_t>(randomindex + randominterval);
                rightvectorcount = static_cast<std::uint8_t>((rightvectorcount + 1) & 3);
                if (fa > 1.0) { continue; }
                if (fb > (1.0 - fa)) { continue; }
            } else {
                // :1043-1051 - oriented bricht nach jeder Zahl einzeln ab
                fa = posi + kZufall[randomindex] * step;
                randomindex = static_cast<std::uint8_t>(randomindex + randominterval);
                if (fa > 1.0) { continue; }
                fb = posj + kZufall[randomindex] * step;
                randomindex = static_cast<std::uint8_t>(randomindex + randominterval);
                if (fb > (1.0 - fa)) { continue; }
            }
            const float fc = static_cast<float>(1.0 - fa - fb);
            Kandidat k;
            k.zufallFade = kZufall[randomindex];   // :753 thisspritesfadestart
            randomindex = static_cast<std::uint8_t>(randomindex + randominterval);
            if (!senk) {
                randomindex = static_cast<std::uint8_t>(randomindex + randominterval);   // :1065
            }
            punkt(e, fa, fb, fc, k.p);
            float s = 0.0F;
            const float g[3] = {fa, fb, fc};
            for (int i = 0; i < 3; ++i) {
                const float* v = e[i].xyz;
                s += g[i] * (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            }
            k.s = s;
            k.licht = e[0].licht * fa + e[1].licht * fb + e[2].licht * fc;   // :780
            // :788-804 - Groesse, Spiegelung, Schraege
            std::uint8_t randomindex2 = randomindex;
            float width = static_cast<float>(ss.width * (1.0 + (ss.variance[0] * kZufall[randomindex2])));
            const float height = static_cast<float>(ss.height * (1.0 + (ss.variance[1] * kZufall[randomindex2++])));
            if (kZufall[randomindex2++] > 0.5) { width = -width; }
            if (senk && ss.vertSkew != 0.0F) {
                k.schief[0] = height * ((ss.vertSkew * 2.0F * kZufall[randomindex2++]) - ss.vertSkew);
                k.schief[1] = height * ((ss.vertSkew * 2.0F * kZufall[randomindex2++]) - ss.vertSkew);
            }
            k.breite = width;
            k.hoehe = height;
            k.rechts = rightvectorcount;
            aus.push_back(k);
        }
    }
}

void zeichneKandidaten(const SurfaceSprite& ss, const Kandidat* k, std::size_t n, const Rahmen& r, bool additiv,
                       std::vector<Quad>& aus) {
    if (n == 0U) { return; }
    const Faden f = faden(ss, r);
    // alphapos = fa*a1 + fb*a2 + fc*a3 mit a_i = 1 - (|V-v_i|^2 - fade2)*inv
    //          = 1 - (|V|^2 - 2 V.P + S - fade2)*inv,   weil fa+fb+fc = 1.
    // In double gerechnet, damit grosse Koordinaten nichts ausloeschen.
    const double V[3] = {r.auge[0], r.auge[1], r.auge[2]};
    const double vv = V[0] * V[0] + V[1] * V[1] + V[2] * V[2];
    const bool senk = (ss.typ != 2);
    const bool flach = (ss.typ == 5);
    for (std::size_t i = 0; i < n; ++i) {
        const Kandidat& c = k[i];
        const double d2 = vv - 2.0 * (V[0] * c.p[0] + V[1] * c.p[1] + V[2] * c.p[2]) + c.s;
        const float alphapos = static_cast<float>(1.0 - (d2 - f.fadedist2) * f.invFadediff);
        // :753-757
        const float fadestart = static_cast<float>(f.faderange + (1.0 - f.faderange) * c.zufallFade);
        float alpha = static_cast<float>(1.0 - ((fadestart - alphapos) / f.faderange));
        if (!(alpha > 0.0)) { continue; }
        if (alpha > 1.0) { alpha = 1.0F; }
        float light = c.licht;
        if (additiv) {
            light = static_cast<float>((128 + (light * 0.5)) * alpha);   // :784
            alpha = 1.0F;
        }
        float width = c.breite;
        if (ss.fadeScale != 0.0F && alphapos < 1.0) {
            width = static_cast<float>(width * (1.0 + (ss.fadeScale * (1.0 - alphapos))));   // :797
        }
        if (senk) {
            senkrecht(c.p, width, c.hoehe, light, alpha, ss.wind, ss.windIdle, ss.facing, c.schief, flach, c.rechts, r,
                      aus);
        } else {
            ausgerichtet(c.p, width, c.hoehe, light, alpha, ss.facing, r, aus);
        }
    }
}

void effektDreieck(const SurfaceSprite& ss, const Ecke e[3], const Rahmen& r, bool additiv, std::vector<Quad>& aus) {
    if (ss.typ != 3 && ss.typ != 4) { return; }
    // :1256 weatherfx
    float density = ss.density;
    if (ss.typ == 4) {
        if (r.wetter < 0.01) { return; }
        density = ss.density / r.wetter;
    }
    if (!eckenPassen(ss, e)) { return; }
    const Faden f = faden(ss, r);
    const float fxalpha = ss.fxAlphaEnd - ss.fxAlphaStart;   // :1228
    // :1251 einblenden, wenn es ausblendet und nicht als Punkt beginnt
    const bool fadeinout = (ss.fxAlphaEnd < 0.05 && ss.height >= 0.1 && ss.width >= 0.1);
    const float a1 = eckAlpha(f, r, e[0].xyz);
    const float a2 = eckAlpha(f, r, e[1].xyz);
    const float a3 = eckAlpha(f, r, e[2].xyz);
    if (a1 <= 0.0F && a2 <= 0.0F && a3 <= 0.0F) { return; }   // :1316
    float step = 0.0F;
    std::uint8_t randomindex = 0;
    std::uint8_t randominterval = 0;
    if (!kopf(e, density, step, randomindex, randominterval)) { return; }
    for (float posi = 0.0F; posi < 1.0F; posi += step) {
        for (float posj = 0.0F; posj < (1.0 - posi); posj += step) {
            // :1343-1347
            const float effecttime = static_cast<float>((r.zeitMs + 10000.0 * kZufall[randomindex]) / ss.fxDuration);
            const float effectpos = effecttime - static_cast<float>(static_cast<int>(effecttime));
            std::uint8_t randomindex2 = alsByte(static_cast<float>(randomindex) + effecttime);
            randomindex = static_cast<std::uint8_t>(randomindex + randominterval);
            const float fa = posi + kZufall[randomindex2++] * step;
            if (fa > 1.0F) { continue; }
            const float fb = posj + kZufall[randomindex2++] * step;
            if (fb > (1.0 - fa)) { continue; }
            const float fc = static_cast<float>(1.0 - fa - fb);
            const float alphapos = a1 * fa + a2 * fb + a3 * fc;
            const float fadestart = static_cast<float>(f.faderange + (1.0 - f.faderange) * kZufall[randomindex2]);
            randomindex2 = static_cast<std::uint8_t>(randomindex2 + randominterval);
            float alpha = 1.0F - ((fadestart - alphapos) / f.faderange);
            if (!(alpha > 0.0F)) { continue; }
            if (alpha > 1.0F) { alpha = 1.0F; }
            float curpoint[3];
            punkt(e, fa, fb, fc, curpoint);
            float light = e[0].licht * fa + e[1].licht * fb + e[2].licht * fc;
            randomindex2 = randomindex;   // :1377
            float width = ss.width * (1.0F + (ss.variance[0] * kZufall[randomindex2]));
            float height = ss.height * (1.0F + (ss.variance[1] * kZufall[randomindex2++]));
            width = width + (effectpos * ss.fxGrow[0] * width);    // :1381
            height = height + (effectpos * ss.fxGrow[1] * height);
            if (fadeinout) {
                if (effectpos > 0.5) {
                    alpha = static_cast<float>(alpha * (ss.fxAlphaStart + (fxalpha * (effectpos - 0.5) * 2.0)));
                } else {
                    alpha = static_cast<float>(alpha * (ss.fxAlphaStart + (fxalpha * (0.5 - effectpos) * 2.0)));
                }
            } else {
                alpha = alpha * (ss.fxAlphaStart + (fxalpha * effectpos));   // :1398
            }
            if (additiv) {
                light = static_cast<float>((128 + (light * 0.5)) * alpha);   // :1404
                alpha = 1.0F;
            }
            if (kZufall[randomindex2] > 0.5F) { width = -width; }   // :1408
            if (ss.fadeScale != 0.0F && alphapos < 1.0F) {
                width = static_cast<float>(width * (1.0F + (ss.fadeScale * (1.0 - alphapos))));
            }
            if (ss.wind > 0.0F && r.windTempo > 0.001) {
                float drawpoint[3];   // :1421 VectorMA(curpoint, effectpos*wind, curWindBlowVect)
                const float s = effectpos * ss.wind;
                for (int k = 0; k < 3; ++k) { drawpoint[k] = curpoint[k] + s * r.blasWind[k]; }
                ausgerichtet(drawpoint, width, height, light, alpha, ss.facing, r, aus);
            } else {
                ausgerichtet(curpoint, width, height, light, alpha, ss.facing, r, aus);
            }
        }
    }
}

void dreieck(const SurfaceSprite& ss, const Ecke e[3], const Rahmen& r, bool additiv, std::vector<Quad>& aus) {
    switch (ss.typ) {   // :1492
        case 5:
        case 1:
        case 2: {
            std::vector<Kandidat> k;
            kandidaten(ss, e, k);
            zeichneKandidaten(ss, k.data(), k.size(), r, additiv, aus);
            break;
        }
        case 3:
        case 4:
            effektDreieck(ss, e, r, additiv, aus);
            break;
        default:
            break;
    }
}

bool stufeZeichnet(const SurfaceSprite& ss, std::size_t stufenNr, float wetter) noexcept {
    if (!ss.aktiv() || stufenNr < 1U) { return false; }   // tr_shade.cpp:2149 MD: stage = 1
    if (ss.typ == 4 && wetter < 0.01F) { return false; }   // :1258
    return true;
}

// --- Patches wie die Engine: tr_curve.cpp (Movie Duels rd-vanilla) ----------

namespace {

constexpr int kMaxGrid = 65;   // MAX_GRID_SIZE, tr_local.h:577

using Gitter = std::vector<PatchPunkt>;   // ctrl[MAX_GRID_SIZE][MAX_GRID_SIZE]
inline PatchPunkt& c_(Gitter& g, int zeile, int spalte) {
    return g[static_cast<std::size_t>(zeile) * kMaxGrid + static_cast<std::size_t>(spalte)];
}

// VectorLength / VectorNormalize / VectorNormalize2 (q_math.c): sqrt in double.
float laenge(const float v[3]) {
    return static_cast<float>(std::sqrt(static_cast<double>(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])));
}
float normiere2(const float v[3], float out[3]) {
    const float l = laenge(v);
    if (l != 0.0F) {
        const float il = 1.0F / l;
        out[0] = v[0] * il;
        out[1] = v[1] * il;
        out[2] = v[2] * il;
    } else {
        out[0] = out[1] = out[2] = 0.0F;
    }
    return l;
}

// LerpDrawVert (tr_curve.cpp:49)
void mitte(const PatchPunkt& a, const PatchPunkt& b, PatchPunkt& out) {
    for (int k = 0; k < 3; ++k) {
        out.xyz[k] = static_cast<float>(0.5 * (a.xyz[k] + b.xyz[k]));
        out.normal[k] = static_cast<float>(0.5 * (a.normal[k] + b.normal[k]));
    }
    for (int k = 0; k < 4; ++k) {
        out.farbe[k] = static_cast<std::uint8_t>((a.farbe[k] + b.farbe[k]) >> 1);
    }
}

// Transpose (:78)
void tausche(int width, int height, Gitter& ctrl) {
    if (width > height) {
        for (int i = 0; i < height; i++) {
            for (int j = i + 1; j < width; j++) {
                if (j < height) {
                    std::swap(c_(ctrl, j, i), c_(ctrl, i, j));
                } else {
                    c_(ctrl, j, i) = c_(ctrl, i, j);
                }
            }
        }
    } else {
        for (int i = 0; i < width; i++) {
            for (int j = i + 1; j < height; j++) {
                if (j < width) {
                    std::swap(c_(ctrl, i, j), c_(ctrl, j, i));
                } else {
                    c_(ctrl, i, j) = c_(ctrl, j, i);
                }
            }
        }
    }
}

// MakeMeshNormals (:123)
void normalen(int width, int height, Gitter& ctrl) {
    static const int neighbors[8][2] = {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};
    bool wrapWidth = false;
    int i = 0;
    for (i = 0; i < height; i++) {
        float delta[3];
        for (int k = 0; k < 3; ++k) { delta[k] = c_(ctrl, i, 0).xyz[k] - c_(ctrl, i, width - 1).xyz[k]; }
        if (laenge(delta) > 1.0) { break; }
    }
    if (i == height) { wrapWidth = true; }
    bool wrapHeight = false;
    for (i = 0; i < width; i++) {
        float delta[3];
        for (int k = 0; k < 3; ++k) { delta[k] = c_(ctrl, 0, i).xyz[k] - c_(ctrl, height - 1, i).xyz[k]; }
        if (laenge(delta) > 1.0) { break; }
    }
    if (i == width) { wrapHeight = true; }
    for (i = 0; i < width; i++) {
        for (int j = 0; j < height; j++) {
            float around[8][3]{};
            bool good[8]{};
            PatchPunkt& dv = c_(ctrl, j, i);
            float base[3] = {dv.xyz[0], dv.xyz[1], dv.xyz[2]};
            for (int k = 0; k < 8; k++) {
                around[k][0] = around[k][1] = around[k][2] = 0.0F;
                good[k] = false;
                for (int dist = 1; dist <= 3; dist++) {
                    int x = i + neighbors[k][0] * dist;
                    int y = j + neighbors[k][1] * dist;
                    if (wrapWidth) {
                        if (x < 0) { x = width - 1 + x; }
                        else if (x >= width) { x = 1 + x - width; }
                    }
                    if (wrapHeight) {
                        if (y < 0) { y = height - 1 + y; }
                        else if (y >= height) { y = 1 + y - height; }
                    }
                    if (x < 0 || x >= width || y < 0 || y >= height) { break; }   // edge of patch
                    float temp[3];
                    for (int q = 0; q < 3; ++q) { temp[q] = c_(ctrl, y, x).xyz[q] - base[q]; }
                    if (normiere2(temp, temp) == 0.0F) { continue; }   // degenerate edge, get more dist
                    good[k] = true;
                    for (int q = 0; q < 3; ++q) { around[k][q] = temp[q]; }
                    break;   // good edge
                }
            }
            float sum[3] = {0.0F, 0.0F, 0.0F};
            int count = 0;
            for (int k = 0; k < 8; k++) {
                if (!good[k] || !good[(k + 1) & 7]) { continue; }
                const float* a = around[(k + 1) & 7];
                const float* b = around[k];
                float normal[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
                if (normiere2(normal, normal) == 0.0F) { continue; }
                for (int q = 0; q < 3; ++q) { sum[q] = normal[q] + sum[q]; }
                count++;
            }
            (void)count;
            normiere2(sum, dv.normal);
        }
    }
}

// InvertCtrl (:229)
void spiegleSpalten(int width, int height, Gitter& ctrl) {
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width / 2; j++) {
            std::swap(c_(ctrl, i, j), c_(ctrl, i, width - 1 - j));
        }
    }
}

// InvertErrorTable (:244)
void spiegleFehler(float errorTable[2][kMaxGrid], int width, int height) {
    float copy[2][kMaxGrid];
    std::memcpy(copy, errorTable, sizeof(copy));
    for (int i = 0; i < width; i++) { errorTable[1][i] = copy[0][i]; }
    for (int i = 0; i < height; i++) { errorTable[0][i] = copy[1][height - 1 - i]; }
}

// PutPointsOnCurve (:264)
void aufDieKurve(Gitter& ctrl, int width, int height) {
    PatchPunkt prev;
    PatchPunkt next;
    for (int i = 0; i < width; i++) {
        for (int j = 1; j < height; j += 2) {
            mitte(c_(ctrl, j, i), c_(ctrl, j + 1, i), prev);
            mitte(c_(ctrl, j, i), c_(ctrl, j - 1, i), next);
            mitte(prev, next, c_(ctrl, j, i));
        }
    }
    for (int j = 0; j < height; j++) {
        for (int i = 1; i < width; i += 2) {
            mitte(c_(ctrl, j, i), c_(ctrl, j, i + 1), prev);
            mitte(c_(ctrl, j, i), c_(ctrl, j, i - 1), next);
            mitte(prev, next, c_(ctrl, j, i));
        }
    }
}

}  // namespace

PatchGitter unterteilePatch(int width, int height, const PatchPunkt* points, float subdivisions) {
    PatchGitter g;
    if (points == nullptr || width < 3 || height < 3 || width > 32 || height > 32) { return g; }   // MAX_PATCH_SIZE
    Gitter ctrl(static_cast<std::size_t>(kMaxGrid) * kMaxGrid);
    float errorTable[2][kMaxGrid]{};
    for (int i = 0; i < width; i++) {
        for (int j = 0; j < height; j++) { c_(ctrl, j, i) = points[j * width + i]; }
    }
    PatchPunkt prev;
    PatchPunkt next;
    PatchPunkt mid;
    for (int dir = 0; dir < 2; dir++) {
        for (int j = 0; j < kMaxGrid; j++) { errorTable[dir][j] = 0.0F; }
        // horizontal subdivisions (:316)
        for (int j = 0; j + 2 < width; j += 2) {
            float max_len = 0.0F;
            for (int i = 0; i < height; i++) {
                float midxyz[3];
                for (int l = 0; l < 3; l++) {
                    midxyz[l] = static_cast<float>(
                        (c_(ctrl, i, j).xyz[l] + c_(ctrl, i, j + 1).xyz[l] * 2 + c_(ctrl, i, j + 2).xyz[l]) * 0.25);
                }
                float vec_out[3];
                for (int l = 0; l < 3; l++) {
                    midxyz[l] = midxyz[l] - c_(ctrl, i, j).xyz[l];
                    vec_out[l] = c_(ctrl, i, j + 2).xyz[l] - c_(ctrl, i, j).xyz[l];
                }
                normiere2(vec_out, vec_out);   // VectorNormalize
                const float d = midxyz[0] * vec_out[0] + midxyz[1] * vec_out[1] + midxyz[2] * vec_out[2];
                for (int l = 0; l < 3; l++) { midxyz[l] = midxyz[l] - vec_out[l] * d; }
                const float len = laenge(midxyz);
                if (len > max_len) { max_len = len; }
            }
            if (max_len < 0.1) {   // :346 alle Punkte auf der Linie
                errorTable[dir][j + 1] = 999;
                continue;
            }
            if (width + 2 > kMaxGrid) {
                errorTable[dir][j + 1] = static_cast<float>(1.0 / max_len);
                continue;   // can't subdivide any more
            }
            if (max_len <= subdivisions) {
                errorTable[dir][j + 1] = static_cast<float>(1.0 / max_len);
                continue;   // didn't need subdivision
            }
            errorTable[dir][j + 2] = static_cast<float>(1.0 / max_len);
            // insert two columns and replace the peak (:364)
            width += 2;
            for (int i = 0; i < height; i++) {
                mitte(c_(ctrl, i, j), c_(ctrl, i, j + 1), prev);
                mitte(c_(ctrl, i, j + 1), c_(ctrl, i, j + 2), next);
                mitte(prev, next, mid);
                for (int k = width - 1; k > j + 3; k--) { c_(ctrl, i, k) = c_(ctrl, i, k - 2); }
                c_(ctrl, i, j + 1) = prev;
                c_(ctrl, i, j + 2) = mid;
                c_(ctrl, i, j + 3) = next;
            }
            j -= 2;   // back up and recheck this set again
        }
        tausche(width, height, ctrl);
        std::swap(width, height);
    }
    aufDieKurve(ctrl, width, height);
    // cull out any rows or columns that are colinear (:393)
    for (int i = 1; i < width - 1; i++) {
        if (errorTable[0][i] != 999) { continue; }
        for (int j = i + 1; j < width; j++) {
            for (int k = 0; k < height; k++) { c_(ctrl, k, j - 1) = c_(ctrl, k, j); }
            errorTable[0][j - 1] = errorTable[0][j];
        }
        width--;
    }
    for (int i = 1; i < height - 1; i++) {
        if (errorTable[1][i] != 999) { continue; }
        for (int j = i + 1; j < height; j++) {
            for (int k = 0; k < width; k++) { c_(ctrl, j - 1, k) = c_(ctrl, j, k); }
            errorTable[1][j - 1] = errorTable[1][j];
        }
        height--;
    }
    // flip for longest tristrips (:419)
    if (height > width) {
        tausche(width, height, ctrl);
        spiegleFehler(errorTable, width, height);
        std::swap(width, height);
        spiegleSpalten(width, height, ctrl);
    }
    normalen(width, height, ctrl);
    g.breite = width;
    g.hoehe = height;
    g.fehlerBreite.assign(errorTable[0], errorTable[0] + width);
    g.fehlerHoehe.assign(errorTable[1], errorTable[1] + height);
    g.punkte.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int k = 0; k < 3; ++k) {
        g.mins[k] = 99999.0F;    // ClearBounds
        g.maxs[k] = -99999.0F;
    }
    for (int i = 0; i < width; i++) {
        for (int j = 0; j < height; j++) {
            PatchPunkt& v = g.punkte[static_cast<std::size_t>(j) * static_cast<std::size_t>(width) + static_cast<std::size_t>(i)];
            v = c_(ctrl, j, i);
            for (int k = 0; k < 3; ++k) {
                g.mins[k] = std::min(g.mins[k], v.xyz[k]);
                g.maxs[k] = std::max(g.maxs[k], v.xyz[k]);
            }
        }
    }
    // :455 localOrigin/meshRadius - ParseMesh ersetzt sie durch lightmapVecs
    float o[3];
    for (int k = 0; k < 3; ++k) {
        o[k] = (g.mins[k] + g.maxs[k]) * 0.5F;
        g.lodMitte[k] = o[k];
    }
    const float d[3] = {g.mins[0] - o[0], g.mins[1] - o[1], g.mins[2] - o[2]};
    g.lodRadius = laenge(d);
    return g;
}

void gitterLod(const PatchGitter& g, const float auge[3], const float vorn[3], std::vector<int>& spalten,
               std::vector<int>& zeilen, float lodCurveError) {
    spalten.clear();
    zeilen.clear();
    if (g.breite < 2 || g.hoehe < 2) { return; }
    // LodErrorForVolume (tr_surface.cpp:1487 MD) - Welt: ori ist die Einheit
    float lodError = 0.0F;
    if (lodCurveError >= 0.0F) {
        const float w[3] = {g.lodMitte[0] - auge[0], g.lodMitte[1] - auge[1], g.lodMitte[2] - auge[2]};
        float d = w[0] * vorn[0] + w[1] * vorn[1] + w[2] * vorn[2];
        if (d < 0) { d = -d; }
        d -= g.lodRadius;
        if (d < 1) { d = 1; }
        lodError = lodCurveError / d;
    }
    // :1538-1558
    spalten.push_back(0);
    for (int i = 1; i < g.breite - 1; i++) {
        if (g.fehlerBreite[static_cast<std::size_t>(i)] <= lodError) { spalten.push_back(i); }
    }
    spalten.push_back(g.breite - 1);
    zeilen.push_back(0);
    for (int i = 1; i < g.hoehe - 1; i++) {
        if (g.fehlerHoehe[static_cast<std::size_t>(i)] <= lodError) { zeilen.push_back(i); }
    }
    zeilen.push_back(g.hoehe - 1);
}

void gitterDreiecke(const PatchGitter& g, const std::vector<int>& spalten, const std::vector<int>& zeilen,
                    std::vector<Ecke>& aus) {
    const int lodW = static_cast<int>(spalten.size());
    const int lodH = static_cast<int>(zeilen.size());
    auto ecke = [&](int zeile, int spalte) {
        const PatchPunkt& p = g.punkte[static_cast<std::size_t>(zeilen[static_cast<std::size_t>(zeile)]) *
                                           static_cast<std::size_t>(g.breite) +
                                       static_cast<std::size_t>(spalten[static_cast<std::size_t>(spalte)])];
        Ecke e;
        for (int k = 0; k < 3; ++k) { e.xyz[k] = p.xyz[k]; }
        e.normalZ = p.normal[2];
        e.licht = static_cast<float>(p.farbe[2]);
        return e;
    };
    // :1626-1645 - "vertex order to be reckognized as tristrips"
    for (int i = 0; i + 1 < lodH; i++) {
        for (int j = 0; j + 1 < lodW; j++) {
            // v1 = (i, j+1), v2 = (i, j), v3 = (i+1, j), v4 = (i+1, j+1)
            aus.push_back(ecke(i, j));
            aus.push_back(ecke(i + 1, j));
            aus.push_back(ecke(i, j + 1));
            aus.push_back(ecke(i, j + 1));
            aus.push_back(ecke(i + 1, j));
            aus.push_back(ecke(i + 1, j + 1));
        }
    }
}

}  // namespace bhed::gras
