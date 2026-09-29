#include "bhed/gla.h"
#include "bhed/diag.h"

#include <functional>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace bhed {
namespace {

// Aus g2c/src/compress.cpp - die Kompression der .gla liegt fest.
constexpr float kQuatScale = 16383.0F;
constexpr float kQuatBias = 2.0F;
constexpr float kXlatScale = 64.0F;
constexpr float kXlatBias = 512.0F;

constexpr std::size_t kMaxQPath = 64;
constexpr std::size_t kCompBoneSize = 14;

std::int32_t i32(const std::string& b, std::size_t at) {
    if (at + 4 > b.size()) {
        return 0;
    }
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) {
        v = (v << 8) | static_cast<unsigned char>(b[at + static_cast<std::size_t>(i)]);
    }
    std::int32_t out = 0;
    std::memcpy(&out, &v, 4);
    return out;
}

float f32(const std::string& b, std::size_t at) {
    const std::int32_t v = i32(b, at);
    float out = 0.0F;
    std::memcpy(&out, &v, 4);
    return out;
}

std::uint16_t u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}

std::string fixed(const std::string& b, std::size_t at, std::size_t len) {
    if (at + len > b.size()) {
        return {};
    }
    return std::string(b.data() + at, ::strnlen(b.data() + at, len));
}

float unsquashQuat(std::uint16_t raw) {
    return static_cast<float>(raw) / kQuatScale - kQuatBias;
}

float unsquashXlat(std::uint16_t raw) {
    return static_cast<float>(raw) / kXlatScale - kXlatBias;
}

}  // namespace

BoneMatrix BoneMatrix::identity() noexcept {
    BoneMatrix out;
    out.m[0][0] = 1.0F;
    out.m[1][1] = 1.0F;
    out.m[2][2] = 1.0F;
    return out;
}

BoneMatrix BoneMatrix::operator*(const BoneMatrix& rhs) const noexcept {
    BoneMatrix out;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out.m[i][j] = m[i][0] * rhs.m[0][j] + m[i][1] * rhs.m[1][j] +
                          m[i][2] * rhs.m[2][j];
        }
        out.m[i][3] = m[i][0] * rhs.m[0][3] + m[i][1] * rhs.m[1][3] +
                      m[i][2] * rhs.m[2][3] + m[i][3];
    }
    return out;
}

void BoneMatrix::transform(const float in[3], float out[3]) const noexcept {
    for (int i = 0; i < 3; ++i) {
        out[i] = m[i][0] * in[0] + m[i][1] * in[1] + m[i][2] * in[2] + m[i][3];
    }
}

BoneMatrix GlaAnimation::localMatrix(int frame, int bone) const {
    if (frame >= numFrames && anhang != nullptr) {
        return anhang->localMatrix(frame - numFrames, bone);
    }
    BoneMatrix out = BoneMatrix::identity();
    const std::size_t nb = bones.size();
    if (frame < 0 || bone < 0 || nb == 0) {
        return out;
    }
    const std::size_t at = static_cast<std::size_t>(frame) * nb +
                           static_cast<std::size_t>(bone);
    if (at >= indexes.size()) {
        return out;
    }
    const std::size_t off = static_cast<std::size_t>(indexes[at]) * kCompBoneSize;
    if (off + kCompBoneSize > bonePool.size()) {
        return out;
    }
    const std::uint8_t* c = bonePool.data() + off;

    // Quaternion aus vier uint16, dann in eine Matrix.
    const float w = unsquashQuat(u16(c + 0));
    const float x = unsquashQuat(u16(c + 2));
    const float y = unsquashQuat(u16(c + 4));
    const float z = unsquashQuat(u16(c + 6));

    const float x2 = x + x;
    const float y2 = y + y;
    const float z2 = z + z;
    const float xx = x * x2;
    const float xy = x * y2;
    const float xz = x * z2;
    const float yy = y * y2;
    const float yz = y * z2;
    const float zz = z * z2;
    const float wx = w * x2;
    const float wy = w * y2;
    const float wz = w * z2;

    out.m[0][0] = 1.0F - (yy + zz);
    out.m[0][1] = xy - wz;
    out.m[0][2] = xz + wy;
    out.m[1][0] = xy + wz;
    out.m[1][1] = 1.0F - (xx + zz);
    out.m[1][2] = yz - wx;
    out.m[2][0] = xz - wy;
    out.m[2][1] = yz + wx;
    out.m[2][2] = 1.0F - (xx + yy);

    out.m[0][3] = unsquashXlat(u16(c + 8));
    out.m[1][3] = unsquashXlat(u16(c + 10));
    out.m[2][3] = unsquashXlat(u16(c + 12));
    return out;
}

std::vector<int> GlaAnimation::topologicalOrder() const {
    const auto n = static_cast<int>(bones.size());
    std::vector<int> out;
    std::vector<char> done(static_cast<std::size_t>(n), 0);
    out.reserve(static_cast<std::size_t>(n));

    // So lange Durchlaeufe, bis nichts mehr dazukommt. Bei 53 Bones ist das
    // billig, und es kommt ohne Rekursion aus.
    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 0; i < n; ++i) {
            if (done[static_cast<std::size_t>(i)] != 0) {
                continue;
            }
            const int p = bones[static_cast<std::size_t>(i)].parent;
            if (p >= 0 && p < n && done[static_cast<std::size_t>(p)] == 0) {
                continue;   // Elternteil noch nicht dran
            }
            out.push_back(i);
            done[static_cast<std::size_t>(i)] = 1;
            progress = true;
        }
    }
    // Ein Kreis im Elternbezug waere kaputt - die uebrigen hinten anhaengen,
    // statt sie zu verlieren.
    for (int i = 0; i < n; ++i) {
        if (done[static_cast<std::size_t>(i)] == 0) {
            out.push_back(i);
        }
    }
    return out;
}

void GlaAnimation::worldMatrices(int frame, std::vector<BoneMatrix>& out) const {
    worldMatrices(frame, {}, out);
}

BoneMatrix GlaAnimation::localMatrixLerp(int frameA, int frameB, float t,
                                         int bone) const {
    if (t <= 0.0F) {
        return localMatrix(frameA, bone);
    }
    if (t >= 1.0F) {
        return localMatrix(frameB, bone);
    }
    const BoneMatrix a = localMatrix(frameA, bone);
    const BoneMatrix b = localMatrix(frameB, bone);
    // Zwoelf Werte, linear - genau wie tr_ghoul2.cpp:1531 ff. Die Engine
    // nimmt hier KEIN Quaternion; ein Nachbau mit slerp saehe an schnellen
    // Drehungen anders aus als das Spiel.
    BoneMatrix out;
    const float f = 1.0F - t;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            out.m[r][c] = f * a.m[r][c] + t * b.m[r][c];
        }
    }
    return out;
}

void GlaAnimation::worldMatricesLerp(int frameA, int frameB, float t,
                                     std::vector<BoneMatrix>& out) const {
    worldMatricesLerp(frameA, frameB, t, {}, out);
}

void GlaAnimation::worldMatricesLerp(int frameA, int frameB, float t,
                                     const std::vector<BoneFrameOverride>& over,
                                     std::vector<BoneMatrix>& out) const {
    const std::size_t n = bones.size();
    out.assign(n, BoneMatrix::identity());
    std::vector<int> ordnung;
    ordnung.reserve(n);
    // Eltern zuerst - dieselbe Reihenfolge wie in worldMatrices.
    std::vector<bool> fertig(n, false);
    std::function<void(std::size_t)> zuerst = [&](std::size_t i) {
        if (i >= n || fertig[i]) {
            return;
        }
        fertig[i] = true;
        const int p = bones[i].parent;
        if (p >= 0 && static_cast<std::size_t>(p) < n) {
            zuerst(static_cast<std::size_t>(p));
        }
        ordnung.push_back(static_cast<int>(i));
    };
    for (std::size_t i = 0; i < n; ++i) {
        zuerst(i);
    }

    // Je Knochen ein eigenes Bildpaar - Ueberschreibungen vererben sich
    // an den Teilbaum, genau wie in worldMatrices.
    std::vector<int> bildA(n, frameA);
    std::vector<int> bildB(n, frameB);
    std::vector<float> anteil(n, t);   // jeder Teilbaum mit eigenem Takt
    for (const BoneFrameOverride& o : over) {
        if (o.bone < 0 || static_cast<std::size_t>(o.bone) >= n) {
            continue;
        }
        const auto ub = static_cast<std::size_t>(o.bone);
        bildA[ub] = o.frame;
        bildB[ub] = (o.next >= 0) ? o.next : o.frame;
        anteil[ub] = (o.next >= 0) ? o.fraction : 0.0F;
    }

    for (const int b : ordnung) {
        const auto ub = static_cast<std::size_t>(b);
        const GlaBone& bone = bones[ub];
        // Erben, wenn dieser Knochen keinen eigenen Eintrag hat.
        if (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) < n) {
            const auto up = static_cast<std::size_t>(bone.parent);
            bool eigen = false;
            for (const BoneFrameOverride& o : over) {
                if (o.bone == b) { eigen = true; break; }
            }
            if (!eigen) {
                bildA[ub] = bildA[up];
                bildB[ub] = bildB[up];
                anteil[ub] = anteil[up];
            }
        }
        // Gemischt wird die LOKALE Matrix, vor dem Zusammensetzen.
        const BoneMatrix a =
            localMatrixLerp(bildA[ub], bildB[ub], anteil[ub], b);
        if (bone.parent < 0 || static_cast<std::size_t>(bone.parent) >= n) {
            out[ub] = a * bone.basePose;
        } else {
            const auto up = static_cast<std::size_t>(bone.parent);
            out[ub] = out[up] * bones[up].basePoseInv * a * bone.basePose;
        }
    }
}

void GlaAnimation::worldMatrices(int frame,
                                 const std::vector<BoneFrameOverride>& over,
                                 std::vector<BoneMatrix>& out) const {
    const std::size_t n = bones.size();
    out.assign(n, BoneMatrix::identity());

    // --- Welches Bild gilt fuer welchen Knochen? -------------------------
    //
    // Die Regel steht in tr_ghoul2.cpp, CBoneCache::EvalLow (Zeile 138 ff.):
    //
    //     EvalLow(parent);                          // Elternteil zuerst
    //     mBones[index].newFrame = par.newFrame;    // ERBEN
    //     ...
    //     G2_TransformBone(index, *this);           // dann ueberschreiben,
    //                                               // wenn dieser Knochen
    //                                               // einen eigenen Eintrag
    //                                               // in der Knochenliste hat
    //
    // Ein Knochen erbt also erst das Bild seines Elternteils und
    // ueberschreibt es nur, wenn fuer IHN eine Animation gesetzt wurde.
    // Damit gilt eine Knochenanimation fuer den ganzen Teilbaum - genau so
    // teilt die Engine Ober- und Unterkoerper.
    //
    // Berechnet in topologischer Reihenfolge, nicht nach Index: in Ravens
    // _humanoid.gla haben acht Knochen ihren Elternteil HINTER sich.
    std::vector<int> bild(n, frame);
    const std::vector<int> ordnung = topologicalOrder();
    for (const int b : ordnung) {
        const auto ub = static_cast<std::size_t>(b);
        const int p = bones[ub].parent;
        if (p >= 0 && static_cast<std::size_t>(p) < n) {
            bild[ub] = bild[static_cast<std::size_t>(p)];
        }
        for (const BoneFrameOverride& o : over) {
            if (o.bone == b && o.frame >= 0) {
                bild[ub] = o.frame;
                break;
            }
        }
    }

    for (const int b : ordnung) {
        const auto ub = static_cast<std::size_t>(b);
        const GlaBone& bone = bones[ub];
        const BoneMatrix a = localMatrix(bild[ub], b);
        if (bone.parent < 0 || static_cast<std::size_t>(bone.parent) >= n) {
            out[ub] = a * bone.basePose;
        } else {
            const auto up = static_cast<std::size_t>(bone.parent);
            out[ub] = out[up] * bones[up].basePoseInv * a * bone.basePose;
        }
    }
}

bool readGla(const std::string& b, GlaAnimation& out, std::string* error) {
    out = GlaAnimation{};
    if (b.size() < 8 + kMaxQPath + 4 + std::size_t{5} * 4) {
        if (error != nullptr) { *error = "zu kurz fuer eine .gla"; }
        return false;
    }
    if (b.compare(0, 4, "2LGA") != 0) {
        if (error != nullptr) { *error = "keine .gla (Kennung " + b.substr(0, 4) + ")"; }
        return false;
    }
    out.skeletonName = fixed(b, 8, kMaxQPath);
    std::size_t at = 8 + kMaxQPath;
    out.scale = f32(b, at);
    at += 4;
    out.numFrames = i32(b, at);
    const std::int32_t ofsFrames = i32(b, at + 4);
    const std::int32_t numBones = i32(b, at + 8);
    const std::int32_t ofsCompBonePool = i32(b, at + 12);
    const std::int32_t ofsSkel = i32(b, at + 16);

    (void)ofsSkel;   // zeigt auf den ersten Bone; wir gehen ueber das
                     // Verzeichnis direkt hinter dem Kopf
    if (numBones <= 0 || numBones > 4096 || out.numFrames < 0 ||
        static_cast<std::size_t>(numBones) * 4 + 8 + kMaxQPath + 28 > b.size()) {
        if (error != nullptr) {
            *error = looksTextMangled(b)
                         ? "Nullbytes durch Leerzeichen ersetzt - die Datei "
                           "wurde im Text-Modus uebertragen und ist kaputt"
                         : "unsinnige Angaben im Kopf";
        }
        return false;
    }

    // --- Skelett ---
    //
    // Das Verzeichnis liegt DIREKT HINTER DEM KOPF, nicht bei ofsSkel: der
    // Wert zeigt auf den ersten Bone. Und die Versaetze darin sind relativ
    // zum VERZEICHNIS, nicht zu ofsSkel.
    //
    // Nachgemessen an _humanoid.gla: Kopf endet bei 100, dort steht 212,
    // und 100+212 = 312 = ofsSkel = Anfang von "model_root". Wer ofsSkel als
    // Bezug nimmt, liest den Bonenamen als Versatzliste und bekommt
    // Zahlen wie 1701080941 ("mode" als int gelesen).
    constexpr std::size_t kHeaderEnd = 8 + kMaxQPath + 4 + std::size_t{6} * 4;
    out.bones.resize(static_cast<std::size_t>(numBones));
    for (std::int32_t i = 0; i < numBones; ++i) {
        const std::int32_t rel =
            i32(b, kHeaderEnd + static_cast<std::size_t>(i) * 4);
        const std::size_t base = kHeaderEnd + static_cast<std::size_t>(rel);
        // Name(64) Flaggen(4) Elternteil(4) Grundstellung(48)
        // Umkehrung(48) numChildren(4) + Kinderliste
        if (rel < 0 || base + kMaxQPath + 8 + 96 + 4 > b.size()) {
            if (error != nullptr) { *error = "Skelett abgeschnitten"; }
            return false;
        }
        GlaBone& bone = out.bones[static_cast<std::size_t>(i)];
        bone.name = fixed(b, base, kMaxQPath);
        bone.parent = i32(b, base + kMaxQPath + 4);
        std::size_t m = base + kMaxQPath + 8;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                bone.basePose.m[r][c] = f32(b, m);
                m += 4;
            }
        }
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                bone.basePoseInv.m[r][c] = f32(b, m);
                m += 4;
            }
        }
    }

    // --- Bonevorrat ---
    if (ofsCompBonePool > 0 &&
        static_cast<std::size_t>(ofsCompBonePool) < b.size()) {
        const std::size_t poolAt = static_cast<std::size_t>(ofsCompBonePool);
        // Der Vorrat reicht bis zum Dateiende: das Skelett steht DAVOR,
        // gleich hinter dem Kopf.
        const std::size_t poolEnd = b.size();
        if (poolEnd > poolAt) {
            out.bonePool.assign(b.begin() + static_cast<std::ptrdiff_t>(poolAt),
                                b.begin() + static_cast<std::ptrdiff_t>(poolEnd));
        }
    }

    // --- Verweise: drei Byte je Bild und Bone ---
    const std::size_t need = static_cast<std::size_t>(out.numFrames) *
                             static_cast<std::size_t>(numBones);
    if (ofsFrames > 0 && need != 0 &&
        static_cast<std::size_t>(ofsFrames) + need * 3 <= b.size()) {
        out.indexes.resize(need);
        const auto* p = reinterpret_cast<const std::uint8_t*>(b.data()) +
                        static_cast<std::size_t>(ofsFrames);
        for (std::size_t i = 0; i < need; ++i) {
            out.indexes[i] = static_cast<std::uint32_t>(p[i * 3]) |
                             (static_cast<std::uint32_t>(p[i * 3 + 1]) << 8) |
                             (static_cast<std::uint32_t>(p[i * 3 + 2]) << 16);
        }
    } else if (need != 0) {
        if (error != nullptr) { *error = "Bildverweise liegen ausserhalb der Datei"; }
        // Das Skelett ist trotzdem brauchbar - die Ruhelage laesst sich
        // zeigen, nur die Bewegung nicht.
        out.numFrames = 0;
    }
    // `name` bleibt beim Lesen leer - der Name steht im Kopf als
    // `skeletonName`. Hier stand `out.name`, und shanks rc549-Protokoll
    // meldete deshalb `GLA gelesen: ""` - eine Zeile ohne die eine Angabe,
    // die sie tragen sollte.
    diag::detail("GLA gelesen: \"" + out.skeletonName + "\", " +
                 std::to_string(b.size()) + " Bytes, " +
                 std::to_string(out.bones.size()) + " Knochen, " +
                 std::to_string(out.numFrames) + " Bilder, Vorrat " +
                 std::to_string(out.bonePool.size()) + " Bytes, " +
                 std::to_string(out.indexes.size()) + " Verweise, Massstab " +
                 std::to_string(out.scale));
    return true;
}

bool looksTextMangled(const std::string& bytes) {
    // Eine Binaerdatei ohne ein einziges Nullbyte auf den ersten Kilobytes
    // ist keine. Zusammen mit auffallend vielen Leerzeichen ist die Sache
    // klar.
    const std::size_t n = std::min<std::size_t>(bytes.size(), 4096);
    if (n < 16) {
        return false;
    }
    std::size_t nullen = 0;
    std::size_t leer = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (bytes[i] == '\0') { ++nullen; }
        if (bytes[i] == ' ') { ++leer; }
    }
    return nullen == 0 && leer * 8 > n;
}

std::string skeletonNameFor(const std::string& animFile) {
    const std::size_t last = animFile.find_last_of('/');
    if (last == std::string::npos) {
        return {};
    }
    const std::string dir = animFile.substr(0, last);
    const std::size_t prev = dir.find_last_of('/');
    return prev == std::string::npos ? dir : dir.substr(prev + 1);
}

std::vector<std::string> animCfgCandidates(const std::string& animFile) {
    const std::size_t last = animFile.find_last_of('/');
    if (last == std::string::npos) {
        return {};
    }
    const std::string dir = animFile.substr(0, last);
    const std::string skel = skeletonNameFor(animFile);
    if (skel.empty()) {
        return {dir + "/animation.cfg"};
    }
    return {dir + "/" + skel + ".cfg", dir + "/animation.cfg"};
}

std::vector<AnimEntry> parseAnimationCfg(const std::string& text) {
    std::vector<AnimEntry> out;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t eol = text.find('\n', at);
        if (eol == std::string::npos) {
            eol = text.size();
        }
        std::string line = text.substr(at, eol - at);
        at = eol + 1;
        // Kommentare und Leerzeilen.
        const std::size_t slash = line.find("//");
        if (slash != std::string::npos) {
            line = line.substr(0, slash);
        }
        // In Felder zerlegen: Leerzeichen und Tabulatoren gemischt.
        std::vector<std::string> parts;
        std::size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t' ||
                                       line[i] == '\r')) {
                ++i;
            }
            const std::size_t start = i;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' &&
                   line[i] != '\r') {
                ++i;
            }
            if (i > start) {
                parts.push_back(line.substr(start, i - start));
            }
        }
        if (parts.size() < 3) {
            continue;
        }
        AnimEntry e;
        e.name = parts[0];
        try {
            e.firstFrame = std::stoi(parts[1]);
            e.numFrames = std::stoi(parts[2]);
            if (parts.size() > 3) { e.loopFrame = std::stoi(parts[3]); }
            if (parts.size() > 4) { e.fps = std::stoi(parts[4]); }
        } catch (...) {
            continue;   // keine Zahlenzeile
        }
        if (e.numFrames > 0) {
            out.push_back(std::move(e));
        }
    }
    return out;
}

// -1 heisst: DIESES Skelett kennt den Namen nicht.
//
// Vorher kam hier eine 0 zurueck, und die ist falsch. Die Engine behandelt
// einen unbekannten Namen als NICHT GESCHEHEN - sie laesst die Figur in der
// Animation, die sie schon hatte. Zwei Sperren, beide in Movie Duels
// nachgesehen:
//
//   Q3_SetAnimUpper (Q3_Interface.cpp:2237)
//       if (animID == -1) { DebugPrint("unknown animation sequence '%s'");
//                           return qfalse; }
//
//   PM_HasAnimation (bg_panimate.cpp:5531)
//       //No frames, no anim
//       if (animations[animation].numFrames == 0) return qfalse;
//
// Der erste faengt einen Namen ab, den es ueberhaupt nicht gibt, der zweite
// einen, den DIESES Skelett nicht hat. In beiden Faellen wird nichts
// gesetzt.
//
// Eine 0 dagegen ist ein GUELTIGES Bild - in _humanoid_jango liegt dort
// FACE_ALERT, eine Gesichtsanimation ohne Koerperhaltung. Die Figur stand
// damit in der Grundstellung. Genau so gemeldet: "die Jango-Figur hat keine
// Animation, sie steht in der Root-Pose fest."
int frameForAnimationIn(const std::vector<AnimEntry>& sections,
                        const std::string& name, double sinceMs, bool unused_hold, float speed,
                        float* fraction, int* nextFrame) {
    if (fraction != nullptr) {
        *fraction = 0.0F;
    }
    if (nextFrame != nullptr) {
        *nextFrame = -1;
    }
    if (name.empty() || sections.empty()) {
        return -1;
    }
    (void)unused_hold;   // siehe gla.h - wirkungslos, absichtlich
    for (const AnimEntry& e : sections) {
        if (e.name != name) {
            continue;
        }
        if (e.numFrames <= 0) {
            return e.firstFrame;
        }
        // Wo im Abschnitt sind wir? Gerechnet ab dem EINSATZ der Animation,
        // nicht ab Null der Zeitleiste - sonst beginnt jede Geste mittendrin.
        // `speed` streckt oder staucht den Abspieltakt - siehe
        // kAnimSpeedWalk in scene.h. 1 heisst: wie in der .cfg.
        //
        // Der Bildtakt wie in der Engine: frameLerp = ceil(1000 / fps) in
        // GANZEN Millisekunden (dl_NPC_stats.cpp:1004), bei negativen fps
        // floor(1000 / fps) - und dann laeuft die Animation RUECKWAERTS
        // (bg_panimate.cpp:4735: Anfang und Ende getauscht). Vorher wurde
        // eine negative Zahl zu 1 fps: 143 Eintraege der animation.cfg -
        // Aufstehen, Knien, Hinsetzen - krochen vorwaerts, zwanzigmal zu
        // langsam. Und 30/45/60 fps liefen ohne das Runden 2-3,5 %
        // zu schnell.
        const bool rueckwaerts = (e.fps < 0);
        const double frameLerp =
            (e.fps > 0) ? std::ceil(1000.0 / e.fps)
                        : (e.fps < 0) ? std::fabs(std::floor(1000.0 / e.fps)) : 1000.0;
        const double perFrame =
            frameLerp / static_cast<double>(std::max(speed, 0.01F));
        const double roh = std::max(sinceMs, 0.0) / perFrame;
        auto step = static_cast<long long>(roh);
        // Der Bruchteil zwischen diesem und dem naechsten Bild. Ohne ihn
        // springt die Figur in Stufen - bei 20 Bildern je Sekunde zwanzig
        // Mal, waehrend der Bildschirm sechzig zeigt. Genau das ist das
        // gemeldete Stottern.
        const auto anteil = static_cast<float>(roh - static_cast<double>(step));

        // --- Wiederholen oder stehenbleiben? ------------------------------
        //
        // Gemeldet: "die Animationen hoeren zum Teil auf zu loopen."
        //
        // Das lag an DIESER Stelle. Sie hatte zwei verschiedene Dinge
        // vermengt:
        //
        //   `hold`       aus SET_ANIM_HOLDTIME_BOTH
        //   `loopFrame`  die dritte Zahl der animation.cfg
        //
        // Nachgelesen sind das zwei Paar Schuhe:
        //
        // **loopFrames entscheidet, OB sie laeuft** (bg_panimate.cpp:4723):
        //
        //     animFlags = (curAnim.loopFrames != -1)
        //                     ? BONE_ANIM_OVERRIDE_LOOP
        //                     : BONE_ANIM_OVERRIDE_FREEZE;
        //
        // Der Wert selbst ist dabei nur ein SCHALTER, kein Schleifenpunkt -
        // gewiederholt wird der ganze Bereich von firstFrame an. Das
        // `% numFrames` unten war also von Anfang an richtig.
        //
        // **Die Haltezeit entscheidet, WIE LANGE sie nicht ersetzt werden
        // darf** (Q3_SetAnimHoldTime -> PM_SetLegsAnimTimer,
        // bg_panimate.cpp:4390). Die -1 heisst dort "unbegrenzt halten",
        // und der Kommentar sagt es ausdruecklich: *"let it be -1 if that
        // was intentional"*. Von Einfrieren steht da nichts.
        //
        // Eine laufende Animation mit Haltezeit -1 laeuft in der Engine
        // also WEITER. behaved liess sie auf dem letzten Bild stehen - und
        // weil die -1 einundsiebzig Prozent aller Haltezeiten ausmacht,
        // traf es fast alles.
        if (e.loopFrame < 0) {
            step = std::min<long long>(step, e.numFrames - 1);
        } else {
            step %= e.numFrames;
        }
        if (fraction != nullptr) {
            *fraction = anteil;
        }
        if (nextFrame != nullptr) {
            // Das naechste Bild - beim Wiederholen wieder von vorn, beim
            // Stehenbleiben dasselbe.
            long long weiter = step + 1;
            if (e.loopFrame < 0) {
                weiter = std::min<long long>(weiter, e.numFrames - 1);
            } else {
                weiter %= e.numFrames;
            }
            *nextFrame = e.firstFrame +
                         static_cast<int>(rueckwaerts ? (e.numFrames - 1 - weiter) : weiter);
        }
        return e.firstFrame +
               static_cast<int>(rueckwaerts ? (e.numFrames - 1 - step) : step);
    }
    return -1;   // der Name steht nicht in der .cfg dieses Skeletts
}


int boneIndex(const GlaAnimation& anim, const std::string& name) {
    for (std::size_t i = 0; i < anim.bones.size(); ++i) {
        if (anim.bones[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void rotateBoneSubtreeYaw(const GlaAnimation& anim,
                          std::vector<BoneMatrix>& world, int bone,
                          float yawDeg) {
    if (bone < 0 || static_cast<std::size_t>(bone) >= world.size() ||
        world.size() != anim.bones.size() || yawDeg == 0.0F) {
        return;
    }
    // Die Nachkommen mitnehmen.
    //
    // Der Kopf traegt in _humanoid die Gesichtsknochen; drehte man nur den
    // cranium, bliebe das Gesicht stehen und die Figur bekaeme ein
    // verdrehtes Gesicht statt eines gedrehten Kopfes.
    //
    // Die Reihenfolge der Knochen in der Datei ist nicht die des Baums -
    // in Ravens _humanoid.gla haben acht Knochen ihren Elternteil HINTER
    // sich (siehe topologicalOrder). Deshalb wird die Zugehoerigkeit ueber
    // die Elternkette bestimmt und nicht ueber den Index.
    std::vector<char> dazu(world.size(), 0);
    dazu[static_cast<std::size_t>(bone)] = 1;
    for (const int b : anim.topologicalOrder()) {
        const auto ub = static_cast<std::size_t>(b);
        const int p = anim.bones[ub].parent;
        if (p >= 0 && static_cast<std::size_t>(p) < dazu.size() &&
            dazu[static_cast<std::size_t>(p)] != 0) {
            dazu[ub] = 1;
        }
    }

    // Gedreht wird um die SENKRECHTE durch den Ursprung des Knochens.
    //
    // Also erst dorthin verschieben, drehen, zurueckschieben. Der Ursprung
    // des Kopfes bleibt damit stehen, wo er ist - der Kopf dreht sich, er
    // wandert nicht. Das ist die Zusicherung, an der eine Probe den
    // Unterschied zu einer Drehung um den Weltnullpunkt festmacht.
    const BoneMatrix& kopf = world[static_cast<std::size_t>(bone)];
    const float px = kopf.m[0][3];
    const float py = kopf.m[1][3];
    const float rad = yawDeg * 3.14159265F / 180.0F;
    const float cs = std::cos(rad);
    const float sn = std::sin(rad);

    for (std::size_t b = 0; b < world.size(); ++b) {
        if (dazu[b] == 0) {
            continue;
        }
        BoneMatrix& m = world[b];
        // Die drei Spalten der Ausrichtung drehen ...
        for (int c = 0; c < 3; ++c) {
            const float x = m.m[0][c];
            const float y = m.m[1][c];
            m.m[0][c] = cs * x - sn * y;
            m.m[1][c] = sn * x + cs * y;
        }
        // ... und den Ort um den Kopfursprung herum.
        const float dx = m.m[0][3] - px;
        const float dy = m.m[1][3] - py;
        m.m[0][3] = px + cs * dx - sn * dy;
        m.m[1][3] = py + sn * dx + cs * dy;
    }
}

}  // namespace bhed
