// mapview.cpp - Kamera, Texturensatz und Stapelzustaende fuer die Kartenansicht.
//
// Hier stand bis rc568 auch der Software-Rasterer (renderMap, renderActors,
// renderModel). Gezeichnet wird seitdem nur noch ueber die Grafikkarte
// (gui/gpumap_win32.cpp); geblieben ist, was beide Wege teilten.
#include "bhed/mapview.h"

#include "bhed/wave.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace bhed {

namespace {

// std::numbers::pi_v ist genauer als jede handgeschriebene
// Konstante - der Unterschied betraegt 8,7e-08. Gemeldet von
// modernize-use-std-numbers.
constexpr float kPi = std::numbers::pi_v<float>;
float rad(float deg) { return deg * kPi / 180.0F; }

struct Vec3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 normalise(const Vec3& a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0.0F ? Vec3{a.x / l, a.y / l, a.z / l} : a;
}
}  // namespace

void Camera::setFovX(float fovXDegrees, float aspect) {
    // Genau die Rechnung aus CG_CalcFOVFromX (cg_view.cpp) - MIT der
    // Seitenverhaeltnis-Korrektur cg_fovAspectAdjust. Movie Duels schaltet
    // sie ein (cg_main.cpp: "cg_fovAspectAdjust", "1"), und CGCam_Update
    // ruft CG_CalcFOVFromX in jedem Bild: der Bildwinkel einer Kamera gilt
    // waagerecht fuer 4:3, der SENKRECHTE bleibt bei jedem Seitenverhaeltnis
    // gleich. Hier stand, Zwischensequenzen benutzten sie nicht - falsch;
    // bei 16:9 war die Vorschau dadurch deutlich zu eng (FOV 90: 58,7 statt
    // 73,7 Grad senkrecht).
    float fx = std::clamp(fovXDegrees, 1.0F, 179.0F);
    {
        constexpr float kGrundSeiten = 0.75F;   // 3/4
        fx = std::atan(std::tan(rad(fx) * 0.5F) * kGrundSeiten * std::max(aspect, 0.01F)) * 360.0F / kPi;
        fx = std::clamp(fx, 1.0F, 179.0F);
    }
    const float x = 1.0F / std::tan(rad(fx) * 0.5F);
    const float fy = std::atan2(1.0F / std::max(aspect, 0.01F), x);
    fovY = std::clamp(fy * 360.0F / kPi, 1.0F, 179.0F);
}

void Camera::forward(float out[3]) const {
    // Wie in der Engine: pitch um die Y-Achse, yaw um Z, X ist vorn.
    const float p = rad(angles[0]);
    const float y = rad(angles[1]);
    out[0] = std::cos(p) * std::cos(y);
    out[1] = std::cos(p) * std::sin(y);
    out[2] = -std::sin(p);
}

void Camera::right(float out[3]) const {
    float f[3];
    forward(f);
    const Vec3 r = normalise(cross(Vec3{f[0], f[1], f[2]}, Vec3{0, 0, 1}));
    out[0] = r.x;
    out[1] = r.y;
    out[2] = r.z;
}

void Camera::up(float out[3]) const {
    float f[3];
    float r[3];
    forward(f);
    right(r);
    const Vec3 u = cross(Vec3{r[0], r[1], r[2]}, Vec3{f[0], f[1], f[2]});
    out[0] = u.x;
    out[1] = u.y;
    out[2] = u.z;
}

void rotateAboutAxis(const float v[3], const float axis[3], float degrees,
                     float out[3]) {
    // Rodrigues:  v' = v cos t + (k x v) sin t + k (k.v)(1 - cos t)
    const float t = rad(degrees);
    const float c = std::cos(t);
    const float si = std::sin(t);
    const Vec3 k{axis[0], axis[1], axis[2]};
    const Vec3 vv{v[0], v[1], v[2]};
    const Vec3 kv = cross(k, vv);
    const float kd = k.x * vv.x + k.y * vv.y + k.z * vv.z;
    out[0] = vv.x * c + kv.x * si + k.x * kd * (1.0F - c);
    out[1] = vv.y * c + kv.y * si + k.y * kd * (1.0F - c);
    out[2] = vv.z * c + kv.z * si + k.z * kd * (1.0F - c);
}

void anglesFromBasis(const float forwardV[3], const float upV[3],
                     float outAngles[3]) {
    // Yaw und Pitch stecken allein in der Blickrichtung - das ist die
    // Umkehrung von Camera::forward():
    //     f.x = cos(p) cos(y)   f.y = cos(p) sin(y)   f.z = -sin(p)
    const float fx = forwardV[0];
    const float fy = forwardV[1];
    const float fz = std::clamp(forwardV[2], -1.0F, 1.0F);
    const float flach = std::sqrt(fx * fx + fy * fy);
    float yaw = 0.0F;
    float pitch = 0.0F;
    if (flach > 1.0e-6F) {
        yaw = std::atan2(fy, fx) * 180.0F / 3.14159265F;
        pitch = std::atan2(-fz, flach) * 180.0F / 3.14159265F;
    } else {
        // Senkrecht nach oben oder unten: der Yaw ist nicht mehr bestimmt
        // (Gimbal Lock). Dann bleibt er, wie er war - hier null - und der
        // Pitch nimmt das Vorzeichen.
        pitch = (fz < 0.0F) ? 90.0F : -90.0F;
    }

    // Der Roll ist der Rest: um wie viel die tatsaechliche Obenrichtung
    // gegen die ROLLFREIE verdreht ist. Die rollfreie Basis bauen wir aus
    // pitch und yaw - genau so, wie Camera::right()/up() es tun (die den
    // Roll ja ignorieren).
    Camera ohneRoll;
    ohneRoll.angles[0] = pitch;
    ohneRoll.angles[1] = yaw;
    ohneRoll.angles[2] = 0.0F;
    float r0[3];
    float u0[3];
    ohneRoll.right(r0);
    ohneRoll.up(u0);
    const float aufU = upV[0] * u0[0] + upV[1] * u0[1] + upV[2] * u0[2];
    const float aufR = upV[0] * r0[0] + upV[1] * r0[1] + upV[2] * r0[2];
    float roll = std::atan2(aufR, aufU) * 180.0F / 3.14159265F;
    if (flach <= 1.0e-6F) {
        roll = 0.0F;
    }
    outAngles[0] = pitch;
    outAngles[1] = yaw;
    outAngles[2] = roll;
}

void Camera::move(float forwardAmount, float rightAmount, float upAmount) {
    float f[3];
    float r[3];
    forward(f);
    right(r);
    for (int k = 0; k < 3; ++k) {
        pos[k] += f[k] * forwardAmount + r[k] * rightAmount;
    }
    pos[2] += upAmount;   // hoch und runter immer senkrecht, wie beim Fliegen
}

void Orbit::pivot(const Camera& cam, float out[3]) const {
    float f[3];
    cam.forward(f);
    for (int k = 0; k < 3; ++k) {
        out[k] = cam.pos[k] + f[k] * distance;
    }
}

void Orbit::turn(Camera& cam, float deltaYawDeg, float deltaPitchDeg) {
    float p[3];
    pivot(cam, p);
    cam.angles[1] += deltaYawDeg;
    cam.angles[0] = std::clamp(cam.angles[0] + deltaPitchDeg, -89.0F, 89.0F);
    // Die Kamera so zuruecksetzen, dass der Drehpunkt wieder vor ihr liegt.
    float f[3];
    cam.forward(f);
    for (int k = 0; k < 3; ++k) {
        cam.pos[k] = p[k] - f[k] * distance;
    }
}

void Orbit::pan(Camera& cam, float dx, float dy) {
    float r[3];
    float u[3];
    cam.right(r);
    cam.up(u);
    // Der Betrag richtet sich nach dem Abstand: aus der Ferne schiebt man
    // weiter, aus der Naehe feiner.
    const float k = distance * 0.0015F;
    for (int i = 0; i < 3; ++i) {
        cam.pos[i] += r[i] * (-dx * k) + u[i] * (dy * k);
    }
}

void Orbit::zoom(Camera& cam, float steps) {
    float p[3];
    pivot(cam, p);
    // Vielfach statt fest: ein Schritt aendert den Abstand um ein Zehntel,
    // damit es aus jeder Entfernung gleich schnell wirkt.
    distance = std::clamp(distance * std::pow(0.9F, steps), 8.0F, 60000.0F);
    float f[3];
    cam.forward(f);
    for (int k = 0; k < 3; ++k) {
        cam.pos[k] = p[k] - f[k] * distance;
    }
}

void drawLetterbox(MapImage& out, float anteil) {
    const int W = out.width;
    const int H = out.height;
    anteil = std::clamp(anteil, 0.0F, 1.0F);
    if (W <= 0 || H <= 0 || anteil <= 0.0F ||
        out.rgba.size() < static_cast<std::size_t>(W) *
                              static_cast<std::size_t>(H) * 4U) {
        return;
    }
    // Ein Zehntel der Hoehe, oben und unten - wie bar_height_dest = 480/10.
    const int bar = std::max(1, static_cast<int>(static_cast<float>(H / 10) * anteil + 0.5F));
    const auto deck = static_cast<std::uint8_t>(std::lround(255.0F * anteil));
    for (int y = 0; y < H; ++y) {
        if (y >= bar && y < H - bar) {
            continue;
        }
        for (int x = 0; x < W; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(W) +
                                    static_cast<std::size_t>(x)) * 4U;
            out.rgba[at + 0] = 0;
            out.rgba[at + 1] = 0;
            out.rgba[at + 2] = 0;
            out.rgba[at + 3] = deck;
        }
    }
}

void drawVollblende(MapImage& out, const float farbe[4]) {
    const float a = std::clamp(farbe[3], 0.0F, 1.0F);
    if (a <= 0.0F || out.width <= 0 || out.height <= 0) {
        return;
    }
    const std::size_t n = static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height);
    if (out.rgba.size() < n * 4U) {
        return;
    }
    const auto kanal = [](float v) {
        return static_cast<std::uint8_t>(std::lround(255.0F * std::clamp(v, 0.0F, 1.0F)));
    };
    const std::uint8_t r = kanal(farbe[0]);
    const std::uint8_t g = kanal(farbe[1]);
    const std::uint8_t b = kanal(farbe[2]);
    // Ueber das, was schon im Ueberlagerungsbild steht (die Balken): wie
    // CG_FillRect nach den Balken - "Quelle ueber Ziel".
    for (std::size_t i = 0; i < n; ++i) {
        std::uint8_t* p = &out.rgba[i * 4U];
        const float da = static_cast<float>(p[3]) / 255.0F;
        const float oa = a + da * (1.0F - a);
        if (oa <= 0.0F) {
            continue;
        }
        const auto misch = [&](std::uint8_t quelle, std::uint8_t ziel) {
            const float v = (static_cast<float>(quelle) * a + static_cast<float>(ziel) * da * (1.0F - a)) / oa;
            return static_cast<std::uint8_t>(std::lround(v));
        };
        p[0] = misch(r, p[0]);
        p[1] = misch(g, p[1]);
        p[2] = misch(b, p[2]);
        p[3] = static_cast<std::uint8_t>(std::lround(oa * 255.0F));
    }
}

void TextureSet::Tex::buildMips() {
    if (!mips.empty() || rgba.empty() || width <= 0 || height <= 0) {
        return;
    }
    // Je Stufe die halbe Kante, bis 1x1 - wie R_MipMap (tr_image.cpp:466).
    // Vier Nachbarn zu einem; ungerade Kanten runden ab, wie dort auch.
    const std::uint8_t* src = rgba.data();
    int sw = width;
    int sh = height;
    while (sw > 1 || sh > 1) {
        const int dw = std::max(1, sw / 2);
        const int dh = std::max(1, sh / 2);
        Mip m;
        m.width = dw;
        m.height = dh;
        m.rgba.assign(static_cast<std::size_t>(dw) *
                          static_cast<std::size_t>(dh) * 4U, 0);
        for (int y = 0; y < dh; ++y) {
            for (int x = 0; x < dw; ++x) {
                unsigned sum[4] = {0, 0, 0, 0};
                unsigned n = 0;
                for (int oy = 0; oy < 2; ++oy) {
                    const int py = std::min(y * 2 + oy, sh - 1);
                    for (int ox = 0; ox < 2; ++ox) {
                        const int px = std::min(x * 2 + ox, sw - 1);
                        const std::size_t at =
                            (static_cast<std::size_t>(py) *
                                 static_cast<std::size_t>(sw) +
                             static_cast<std::size_t>(px)) * 4U;
                        for (int k = 0; k < 4; ++k) {
                            sum[k] += src[at + static_cast<std::size_t>(k)];
                        }
                        ++n;
                    }
                }
                const std::size_t dst =
                    (static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(dw) +
                     static_cast<std::size_t>(x)) * 4U;
                for (int k = 0; k < 4; ++k) {
                    m.rgba[dst + static_cast<std::size_t>(k)] =
                        static_cast<std::uint8_t>(sum[k] / n);
                }
            }
        }
        mips.push_back(std::move(m));
        src = mips.back().rgba.data();
        sw = dw;
        sh = dh;
    }
}

const std::uint8_t* TextureSet::Tex::levelData(int level, int& w,
                                               int& h) const {
    if (level <= 0 || mips.empty()) {
        w = width;
        h = height;
        return rgba.data();
    }
    const int idx = std::min(level - 1, static_cast<int>(mips.size()) - 1);
    const Mip& m = mips[static_cast<std::size_t>(idx)];
    w = m.width;
    h = m.height;
    return m.rgba.data();
}

namespace {
// Der gemeinsame Rumpf: aus einem `TextureSet::Tex` wird ein `BatchState`.
//
// Er stand nur in `batchStateFor(textures, texId, ...)`, war also fuer
// Kartenflaechen da. Figurenflaechen liegen in `ModelTextures::bySurface`
// und sind derselbe Typ - sie kamen nur nie hier vorbei.
void batchStateAus(const TextureSet::Tex& t, float zeitSekunden,
                   BatchState& bs, const TextureSet* textures) {
    bs.haveSpecular = (t.alphaGen == AlphaGen::LightingSpecular);
    bs.ausSkript = t.ausSkript;
    bs.lightmapStufe = t.lightmapStufe;
    bs.rgbVertex = t.rgbVertex;
    if (t.rgbConst[0] >= 0.0F) {
        for (int k = 0; k < 3; ++k) {
            bs.rgbConst[k] = std::max(t.rgbConst[k], 0.0F);
        }
        bs.haveRgbConst = true;
    }
    if (t.rgbWave.active()) {
        bs.rgbGlow = std::clamp(waveValue(t.rgbWave, zeitSekunden), 0.0F, 1.0F);
        bs.haveRgbWave = true;
    }
    // Der Himmel braucht den ganzen Satz, weil die sechs Seiten ueber ihn
    // nachgeschlagen werden. Eine Figurenflaeche hat keinen - dann bleibt
    // der Himmel aus, und das ist richtig so.
    if (t.skyFaces.size() == 6 && textures != nullptr) {
        bs.skySet = textures;
        bs.skyFaces = &t.skyFaces;
    }
    bs.blend = t.blend;
    bs.srcFactor = t.srcFactor;
    bs.dstFactor = t.dstFactor;
    bs.cull = t.cull;
    bs.alphaTest = t.alphaTest;
    bs.alphaGen = t.alphaGen;
    bs.depthFunc = t.depthFunc;
    bs.texGen = t.texGen;
    bs.alphaConst = t.alphaConst;
    bs.depthWrite = t.depthWrite;
    bs.autosprite = t.autosprite;
    bs.polygonOffset = t.polygonOffset;
    bs.glow = t.glow;
    for (int k = 0; k < t.numTexMods && k < kMaxTexMods; ++k) {
        bs.texMods[k] = t.texMods[k];
    }
    bs.numTexMods = std::min(t.numTexMods, kMaxTexMods);
    bs.deformWave = t.deformWave;
    bs.deformSpread = t.deformSpread;
    if (!t.empty()) {
        bs.tex = &t;
    }
}
}  // namespace

int animBildFuer(const TextureSet* textures, int texId, float zeitSekunden) {
    if (textures == nullptr || texId < 0 ||
        static_cast<std::size_t>(texId) >= textures->byShader.size()) {
        return texId;
    }
    const TextureSet::Tex& t0 = textures->byShader[static_cast<std::size_t>(texId)];
    const std::size_t n = t0.animFrames.size();
    if (n <= 1 || t0.animFreq <= 0.0F) {
        return texId;
    }
    // R_BindAnimatedImage (tr_shade.cpp): der Umweg ueber 1024 schneidet ab.
    const auto roh = static_cast<long long>(zeitSekunden * t0.animFreq * 1024.0F);
    long long idx = roh >> 10;
    if (idx < 0) {
        idx = 0;
    }
    if (t0.animOneShot) {
        // oneShotAnimMap: auf dem letzten Bild stehen bleiben.
        idx = std::min(idx, static_cast<long long>(n) - 1);
    } else {
        idx %= static_cast<long long>(n);
    }
    return t0.animFrames[static_cast<std::size_t>(idx)];
}

BatchState batchStateFor(const TextureSet::Tex& t, float zeitSekunden) {
    // --- EINE Fassung fuer Karte und Figuren -----------------------------
    //
    // Eine Kartenflaeche wird ueber `byShader[texId]` nachgeschlagen, eine
    // Figurenflaeche ueber `ModelTextures::bySurface[si]` - aber beides ist
    // ein `TextureSet::Tex`, und was daraus folgt, ist dasselbe.
    //
    // Bis rc451 gab es diese Ableitung nur fuer die Karte. Der GPU-Weg
    // zeichnete Figuren deshalb DURCHWEG deckend (`holeBlend(Blend::Opaque)`
    // einmal vor der Schleife), und ein Hologramm mit `blendFunc GL_ONE
    // GL_ONE` wurde zu einem schwarzen Viereck mit einem blassen Umriss
    // darin - genau das, was auf dem Tisch in md_am_sith stand.
    BatchState bs;
    batchStateAus(t, zeitSekunden, bs, nullptr);
    return bs;
}

BatchState batchStateFor(const TextureSet* textures, int texId,
                         float zeitSekunden) {
    BatchState bs;
    if (textures == nullptr || texId < 0 ||
        static_cast<std::size_t>(texId) >= textures->byShader.size()) {
        return bs;
    }
    const TextureSet::Tex& t =
        textures->byShader[static_cast<std::size_t>(texId)];
    batchStateAus(t, zeitSekunden, bs, textures);
    return bs;
}

// Die Knochenstellung einer Figur fuer EIN Bild.
//
// Warum diese Funktion ueberhaupt da ist
// --------------------------------------
// Bis rc429 stand dieser Ablauf nur im Rasterer. Der GPU-Weg rief schlicht
//
//     a.anim->worldMatrices(a.frame, welt)
//
// und liess damit VIER Dinge aus, nicht eines: den Vorrang von Ober- und
// Unterkoerper, den Mund, die Mischung zwischen zwei Bildern und den
// Uebergang zwischen zwei Animationen. Dazu den Kopfwinkel.
//
// Sichtbar war das als falsche Haltung UND als Springen, wo der Rasterer
// weich blendet.
//
// Zwei Fassungen desselben Ablaufs waeren wieder auseinandergelaufen -
// genau das Muster, das hier schon dreimal Runden gekostet hat
// (brauchtEcken gegen vbZuKlein). Deshalb EINE Fassung, die beide rufen.
// Wenn sie falsch ist, ist sie ueberall gleich falsch, und ein Vergleich
// der beiden Bilder hat wieder Aussagekraft.
//
// `hilf` ist ein Arbeitspuffer fuer den Uebergang. Er wird vom Aufrufer
// gehalten, damit er nicht je Figur und Bild neu entsteht.
//
// Rueckgabe: ob ueberhaupt eine Knochenstellung entstanden ist. Ist sie
// false, steht in `world` nichts Brauchbares und das Modell wird
// unverformt gezeichnet.
bool figurKnochen(const ActorDraw& act, std::vector<BoneMatrix>& world,
                  std::vector<BoneMatrix>& hilf) {
    const bool skinned = (act.anim != nullptr) && !act.anim->bones.empty() &&
                         act.anim->numFrames > 0;
    if (skinned) {
        // --- Ober- und Unterkoerper koennen verschiedene Bilder haben
        //
        // SET_ANIM_LOWER setzt die Animation auf "model_root" (das
        // ganze Skelett), SET_ANIM_UPPER auf "lower_lumbar" - und
        // dessen Teilbaum ist alles oberhalb der Taille
        // (bg_panimate.cpp:4848, 4864; g_client.cpp:1353, 1544).
        //
        // Bis rc251 bekam der ganze Koerper ein Bild, und eine reine
        // Oberkoerperanimation wie TORSO_WEAPONIDLE2 landete auch in
        // den Beinen. Gemeldet als "manche Charaktere benutzen die
        // falschen Animationen".
        std::vector<BoneFrameOverride> vorrang;
        if (act.torsoFrame >= 0) {
            const int taille = boneIndex(*act.anim, "lower_lumbar");
            if (taille >= 0) {
                BoneFrameOverride o{
                    taille,
                    std::clamp(act.torsoFrame, 0,
                               act.anim->numFrames - 1)};
                if (act.torsoNext >= 0 && act.torsoFraction > 0.001F) {
                    o.next = std::clamp(act.torsoNext, 0, act.anim->numFrames - 1);
                    o.fraction = act.torsoFraction;
                }
                vorrang.push_back(o);
            }
        }
        // Der Mund ist ein DRITTER Lauf, auf dem Knochen "face".
        //
        // Dieselbe Mechanik wie beim Oberkoerper - deshalb war rc252
        // die Voraussetzung dafuer. Der Teilbaum ab "face" traegt den
        // Kiefer; er liegt unter dem cranium, also unter dem
        // Oberkoerper, und der spaetere Eintrag gewinnt.
        if (act.faceFrame >= 0) {
            const int gesicht = boneIndex(*act.anim, "face");
            if (gesicht >= 0) {
                vorrang.push_back(BoneFrameOverride{
                    gesicht,
                    std::clamp(act.faceFrame, 0,
                               act.anim->numFrames - 1)});
            }
        }
        // --- Zwischen zwei Bildern ------------------------------------
        //
        // Gemeldet: "er stottert immer noch beim Laufen."
        //
        // behaved sprang von Bild zu Bild - bei 20 Bildern je Sekunde
        // zwanzig Stufen, waehrend der Bildschirm sechzig oder mehr
        // zeigt. Das Rutschen aus rc290 war ein zweites Problem; DIES
        // ist das Stottern.
        //
        // Gemischt wird auf den LOKALEN Matrizen, vor dem
        // Zusammensetzen der Kette - so macht es die Engine
        // (tr_ghoul2.cpp:1531 ff., UnCompressBone fuer beide Bilder,
        // dann zwoelf Werte linear). Wer erst zusammensetzt und dann
        // mischt, bekommt bei gebeugten Gliedern zu kurze Knochen.
        const bool koerperMischt = act.frameNext >= 0 && act.frameFraction > 0.001F;
        const bool torsoMischt = !vorrang.empty() && vorrang.front().next >= 0;
        if (koerperMischt || torsoMischt) {
            act.anim->worldMatricesLerp(
                std::clamp(act.frame, 0, act.anim->numFrames - 1),
                std::clamp(koerperMischt ? act.frameNext : act.frame, 0,
                           act.anim->numFrames - 1),
                koerperMischt ? act.frameFraction : 0.0F, vorrang, world);
        } else {
            act.anim->worldMatrices(
                std::clamp(act.frame, 0, act.anim->numFrames - 1), vorrang,
                world);
        }
        // --- Der Uebergang --------------------------------------------
        //
        // Gemischt wird auf KNOCHENEBENE, vor dem Skinning - genau wie
        // in tr_ghoul2.cpp:1543:
        //
        //     tbone[2] = blendLerp * tbone[2] + (1-blendLerp) * tbone[5]
        //
        // tbone[2] ist die Stellung aus der laufenden Animation,
        // tbone[5] die eingefrorene aus der vorigen. Bei halber
        // Blendzeit liegt die Stellung genau in der Mitte.
        //
        // Es ist eine LINEARE Mischung auf den 3x4-Matrizen,
        // komponentenweise - kein Slerp. Bei stark verschiedenen
        // Stellungen und kurzer Blendzeit "schrumpft" ein Glied kurz.
        // Das ist keine Ungenauigkeit hier, sondern genau das, was die
        // Engine zeigt.
        //
        // Der Preis: 53 Matrizen mehr je Figur und Bild. Gemessen sind
        // das 0,03 % der Skinningarbeit - die Verformung laeuft je
        // VERTEX, der Uebergang je KNOCHEN.
        // --- Zwei Uebergaenge: Beine und Oberkoerper ------------------
        //
        // In der Engine blendet jede Knochenanimation fuer sich. Die alte
        // Seite bekommt deshalb fuer den Teilbaum ab "lower_lumbar" das
        // alte Bild des OBERKOERPERS (torsoPrevFrame), und jeder Knochen
        // mischt mit dem Anteil SEINER Spur.
        const bool koerperBlend = act.prevFrame >= 0 && act.blendLerp < 1.0F;
        const bool torsoBlend = act.torsoPrevFrame >= 0 && act.torsoBlendLerp < 1.0F;
        if (koerperBlend || torsoBlend) {
            const int taille = boneIndex(*act.anim, "lower_lumbar");
            std::vector<BoneFrameOverride> alt;
            for (const BoneFrameOverride& o : vorrang) {
                if (torsoBlend && o.bone == taille) {
                    continue;   // wird unten durch das alte Bild ersetzt
                }
                alt.push_back(BoneFrameOverride{o.bone, o.frame});
            }
            if (torsoBlend && taille >= 0) {
                alt.push_back(BoneFrameOverride{
                    taille, std::clamp(act.torsoPrevFrame, 0, act.anim->numFrames - 1)});
            }
            act.anim->worldMatrices(
                std::clamp(koerperBlend ? act.prevFrame : act.frame, 0,
                           act.anim->numFrames - 1),
                alt, hilf);
            if (hilf.size() == world.size()) {
                // Welche Knochen gehoeren zum Oberkoerper?
                std::vector<char> oben(world.size(), 0);
                if (torsoBlend && taille >= 0) {
                    for (std::size_t b = 0; b < world.size(); ++b) {
                        for (int p = static_cast<int>(b), schritte = 0;
                             p >= 0 && schritte < 256; ++schritte) {
                            if (p == taille) { oben[b] = 1; break; }
                            p = act.anim->bones[static_cast<std::size_t>(p)].parent;
                        }
                    }
                }
                const float lk = koerperBlend ? std::clamp(act.blendLerp, 0.0F, 1.0F) : 1.0F;
                const float lt = std::clamp(act.torsoBlendLerp, 0.0F, 1.0F);
                for (std::size_t b = 0; b < world.size(); ++b) {
                    const float l = (oben[b] != 0) ? lt : lk;
                    if (l >= 1.0F) {
                        continue;
                    }
                    const float g = 1.0F - l;
                    for (int r = 0; r < 3; ++r) {
                        for (int c = 0; c < 4; ++c) {
                            world[b].m[r][c] =
                                l * world[b].m[r][c] + g * hilf[b].m[r][c];
                        }
                    }
                }
            }
        }
    }

    // --- Der Kopf, NACH dem Uebergang -------------------------------
    //
    // Die Reihenfolge ist keine Geschmacksfrage: die Engine setzt die
    // Kopfwinkel auf die FERTIGEN Knochen, und ein Uebergang zwischen
    // zwei Animationen soll die Blickrichtung nicht mit herumziehen.
    // Wer den Kopf VOR dem Mischen drehte, bekaeme bei jedem
    // Animationswechsel ein Zucken im Blick.
    //
    // "cranium" ist der Kopfknochen in _humanoid - nachgesehen, nicht
    // geraten: Knochen 15, Elternteil 14 (cervical), darueber 13
    // (thoracic). Kennt ein Skelett den Namen nicht, gibt boneIndex
    // -1 und rotateBoneSubtreeYaw laesst alles stehen.
    if (skinned && act.headYaw != 0.0F) {
        // --- Der Blick verteilt sich auf drei Knochen ---------------
        //
        // Gemeldet: "nicht immer dreht sich der Oberkoerper mit dem
        // Kopf mit."
        //
        // Er dreht sich GAR NICHT mit - jedenfalls nicht bei einem
        // Ghoul2-Modell, und das sind in Movie Duels alle. rc254 hatte
        // die Oberkoerperdrehung aus `CG_PlayerAngles`
        // (cg_players.cpp:3030) abgeschrieben, dem alten Weg fuer
        // MD3-Modelle. Ghoul2 laeuft ueber `CG_G2PlayerAngles`
        // (ebenda:2479), und der endet mit `return`, bevor jener Block
        // je erreicht wird.
        //
        // Was dort steht, ist allein `CG_G2ClientNeckAngles`
        // (ebenda:2301 ff.):
        //
        //     thoracicAngles[YAW] = lA[YAW] * 0.1f;
        //     neckAngles[YAW]     = lA[YAW] * 0.3f;
        //     headAngles[YAW]     = lA[YAW] * 0.6f;
        //
        // Drei Knochen, Summe eins. Die Brustwirbel bekommen ein
        // Zehntel - das ist alles, was vom Oberkoerper mitgeht.
        //
        // Die Reihenfolge ist von aussen nach innen. Jede Drehung wirkt
        // um den Ursprung ihres Knochens, WIE ER GERADE STEHT - also
        // muss die aeussere schon drin sein, wenn die innere an die
        // Reihe kommt. Andersherum saesse der Kopf neben dem Hals.
        const struct {
            const char* knochen;
            float anteil;
        } kette[] = {
            {"thoracic", 0.10F},
            {"cervical", 0.30F},
            {"cranium", 0.60F},
        };
        for (const auto& g : kette) {
            rotateBoneSubtreeYaw(*act.anim, world,
                                 boneIndex(*act.anim, g.knochen),
                                 act.headYaw * g.anteil);
        }
    }

    // Die Figur steht am Ort und ist um yaw gedreht.
    //
    // Dazu kommt ein fester Ausgleich von +90 Grad. Grund, gemessen an
    // models/players/_humanoid_md/_humanoid.gla in der Grundstellung:
    // die linke und die rechte Koerperhaelfte liegen entlang X
    // auseinander (lfemurYZ zu rfemurYZ 7,2 in X und 0,0 in Y; lhand zu
    // rhand 38,0 in X). Mit links = z x vorn folgt vorn = links x z =
    // (0,-1,0), die Figur schaut also nach -Y.
    //
    // Die Engine setzt +X als vorn (Create_Matrix -> AnglesToAxis,
    // forward = (cp*cy, cp*sy, -sp)). Eine Drehung um +90 Grad um Z
    // bildet -Y auf +X ab.
    //
    // Inzwischen dreifach belegt, aus drei unabhaengigen Richtungen:
    //
    //   1. Die Grundstellung der .gla - links und rechts liegen entlang
    //      X auseinander (lhand zu rhand 38,0 in X und 0,0 in Y).
    //   2. Die AUSDEHNUNG der Modelle selbst. Ueber 80 Spielermodelle
    //      aus MD_Models_Legends und MD_Models_KOTOR gemessen: rund 43
    //      Einheiten in X, aber nur 11 bis 21 in Y. Ein Mensch ist
    //      schulterbreit und schmal von vorn - die breite Achse ist die
    //      Schulterachse, und die steht quer zur Blickrichtung.
    //      75 von 80 Modellen stimmen ueberein.
    //   3. Der Bildschirm: ohne den Ausgleich standen alle Figuren
    //      90 Grad nach rechts verdreht.
    //
    // Wo die Engine das ausgleicht, habe ich weiterhin NICHT gefunden -
    // RootMatrix() liefert im Normalfall die Einheitsmatrix. Aber der
    // Zusammenhang selbst steht fest, und darauf kommt es hier an.
    return skinned;
}

// --- Die Klingenlage aus dem saberType -----------------------------------
//
// Nur gebraucht, wenn der Griff keine "*bladeN"-Bolzen hat (tag_hack in
// CG_AddSaberBladeGo, cg_players.cpp:13818 ff.). Alle Stab-Arten tragen
// "STAFF" im Namen, dazu SABER_ELECTROSTAFF.
KlingenLage klingenLageFuer(const std::string& typ) {
    if (typ.find("STAFF") != std::string::npos) { return KlingenLage::Stab; }
    if (typ == "SABER_BROAD") { return KlingenLage::Breit; }
    if (typ == "SABER_PRONG") { return KlingenLage::Zinke; }
    if (typ == "SABER_SAI") { return KlingenLage::Sai; }
    if (typ == "SABER_CLAW") { return KlingenLage::Kralle; }
    return KlingenLage::Einzeln;
}

// Wurzel und Spitze einer Klinge im Raum des GRIFFS.
//
// CG_AddSaberBladeGo (cg_players.cpp:13767 ff.): erst "*blade<N+1>", sonst
// "*flash" mit tag_hack. Aus der Bolzenmatrix:
//
//     org     = ORIGIN
//     axis[0] = NEGATIVE_X   (die Klingenrichtung)
//     axis[1] = NEGATIVE_Y   ("right")
//     axis[2] = POSITIVE_Z   ("up")
//
// und die Spitze liegt bei org + axis[0] * Laenge.
bool klingenStrecke(const GlmModel& griff, KlingenLage lage, int nummer,
                    float laenge, float wurzel[3], float spitze[3]) {
    bool hack = false;
    int bs = surfaceIndex(griff, "*blade" + std::to_string(nummer + 1));
    if (bs < 0) {
        hack = true;
        bs = surfaceIndex(griff, "*flash");
    }
    BoneMatrix m{};
    if (!boltMatrixRigid(griff, bs, m)) {
        return false;
    }
    float org[3];
    float ax0[3];
    float ax1[3];
    float ax2[3];
    for (int k = 0; k < 3; ++k) {
        org[k] = m.m[k][3];
        ax0[k] = -m.m[k][0];
        ax1[k] = -m.m[k][1];
        ax2[k] = m.m[k][2];
    }
    const auto vor = [&org](const float* achse, float um) {
        for (int k = 0; k < 3; ++k) { org[k] += achse[k] * um; }
    };
    if (hack) {
        switch (lage) {
            case KlingenLage::Stab:
                if (nummer == 1) {
                    for (float& v : ax0) { v = -v; }
                    vor(ax0, 16.0F);
                }
                break;
            case KlingenLage::Breit:
                if (nummer == 0) { vor(ax1, -1.0F); }
                if (nummer == 1) { vor(ax1, 1.0F); }
                break;
            case KlingenLage::Zinke:
                if (nummer == 0) { vor(ax1, -3.0F); }
                if (nummer == 1) { vor(ax1, 3.0F); }
                break;
            case KlingenLage::Sai:
                if (nummer == 1) { vor(ax1, -3.0F); }
                if (nummer == 2) { vor(ax1, 3.0F); }
                break;
            case KlingenLage::Kralle:
                if (nummer >= 0 && nummer <= 2) {
                    vor(ax0, 2.0F);
                    vor(ax2, 2.0F);
                    if (nummer == 1) { vor(ax1, 2.0F); }
                    if (nummer == 2) { vor(ax1, -2.0F); }
                }
                break;
            case KlingenLage::Einzeln:
                break;
        }
    }
    for (int k = 0; k < 3; ++k) {
        wurzel[k] = org[k];
        spitze[k] = org[k] + ax0[k] * laenge;
    }
    return true;
}

}  // namespace bhed
