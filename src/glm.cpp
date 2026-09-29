#include "bhed/glm.h"
#include "bhed/diag.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

namespace bhed {
namespace {
// OldToNewRemapTable, tr_ghoul2.cpp:3151 - Knochen des JK2-Skeletts (72) auf
// die des JKA-Skeletts (53). Mehrere alte Knochen fallen auf einen neuen
// (ltalus/ltarsal, die Fingerglieder), "face_always_" wird der Kopf.
constexpr std::int32_t kJk2NachJka[72] = {
    0, 1, 2, 3, 4, 5, 6, 6, 7, 8, 9, 10,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 29, 34, 35, 35,
    30, 31, 31, 32, 33, 33, 32, 33, 33, 34, 35, 35,
    36, 37, 38, 39, 40, 41, 42, 42, 43, 44, 44, 43,
    44, 44, 45, 46, 46, 45, 46, 46, 47, 48, 48, 52,
};
}  // namespace
namespace {

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

std::string fixed(const std::string& b, std::size_t at, std::size_t len) {
    if (at + len > b.size()) {
        return {};
    }
    const std::size_t n = ::strnlen(b.data() + at, len);
    return b.substr(at, n);
}

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t e = s.size();
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (a < e && sp(s[a])) { ++a; }
    while (e > a && sp(s[e - 1])) { --e; }
    return s.substr(a, e - a);
}

}  // namespace

bool GlmSurface::isOffByName() const noexcept {
    // Die Flagge zuerst - sie ist die Angabe des Formats. Der Namensvergleich
    // bleibt als Rueckfall: der Kommentar in mdx_format.h sagt, die Flagge
    // "saves strcmp()ing for _off in surface names", also gab es beides.
    if ((flags & kFlagOff) != 0) {
        return true;
    }
    return name.size() > 4 && name.compare(name.size() - 4, 4, "_off") == 0;
}

bool readGlm(const std::string& b, GlmModel& out, std::string* error) {
    out = GlmModel{};
    // Kopf: Kennung(4) Fassung(4) Name(64) .gla-Pfad(64) + 7 int
    constexpr std::size_t kHeaderSize = 8 + 64 + 64 + 7 * 4;
    if (b.size() < kHeaderSize) {
        if (error != nullptr) { *error = "zu kurz fuer ein .glm"; }
        return false;
    }
    if (b.compare(0, 4, "2LGM") != 0) {
        if (error != nullptr) { *error = "keine .glm (Kennung " + b.substr(0, 4) + ")"; }
        return false;
    }
    const std::int32_t version = i32(b, 4);
    if (version != 6) {
        // Nicht abweisen: eine andere Fassung koennte trotzdem lesbar sein.
        // Nur vermerken, damit man es im Protokoll sieht.
        if (error != nullptr) {
            *error = "ungewohnte Fassung " + std::to_string(version);
        }
    }
    out.name = fixed(b, 8, 64);
    out.animFile = fixed(b, 72, 64);

    const std::size_t at = 8 + 128;
    out.numBones = i32(b, at + 4);
    // --- JK2-Modelle: 72 Knochen auf das Skelett mit 53 -------------------
    //
    // R_LoadMDXM, tr_ghoul2.cpp:3658: ein Modell mit 72 Knochen, das
    // "_humanoid" als Skelett nennt, ist ein JK2-Modell. Die Engine laedt es
    // trotzdem ("converting jk2 model") und schreibt seine Knochenverweise
    // ueber OldToNewRemapTable auf die 53 Knochen von JKA um.
    //
    // Gefunden mit tests/meshmasse.cpp (27.09.): jarjar und yarael in Movie
    // Duels sind solche Modelle. behaved legte ihre Ecken bisher auf die
    // JK2-Nummern - Knochen 18 ist dort eine Augenbraue, in JKA ein Finger.
    //
    // Die Engine prueft zusaetzlich, dass die .gla NICHT ebenfalls 72
    // Knochen hat; in JKA und Movie Duels hat _humanoid.gla immer 53.
    const bool jk2Modell =
        out.numBones == 72 && out.animFile.find("_humanoid") != std::string::npos;
    if (jk2Modell) {
        out.numBones = 53;
        out.jk2Umgewandelt = true;
    }
    out.numLods = i32(b, at + 8);
    const std::int32_t ofsLods = i32(b, at + 12);
    const std::int32_t numSurfaces = i32(b, at + 16);
    const std::int32_t ofsHierarchy = i32(b, at + 20);
    const std::int32_t ofsEnd = i32(b, at + 24);

    if (numSurfaces <= 0 || numSurfaces > 4096 || ofsLods <= 0 ||
        ofsHierarchy <= 0 ||
        static_cast<std::size_t>(ofsEnd) > b.size() + 4) {
        if (error != nullptr) { *error = "unsinnige Angaben im Kopf"; }
        return false;
    }

    // --- Hierarchie ---
    out.surfaces.resize(static_cast<std::size_t>(numSurfaces));
    std::size_t p = static_cast<std::size_t>(ofsHierarchy);
    for (std::int32_t i = 0; i < numSurfaces; ++i) {
        if (p + 144 > b.size()) {
            if (error != nullptr) { *error = "Hierarchie abgeschnitten"; }
            return false;
        }
        GlmSurface& s = out.surfaces[static_cast<std::size_t>(i)];
        s.name = fixed(b, p, 64);
        s.flags = static_cast<std::uint32_t>(i32(b, p + 64));
        s.shader = fixed(b, p + 68, 64);
        s.parent = i32(b, p + 136);
        const std::int32_t numChildren = i32(b, p + 140);
        if (numChildren < 0 || numChildren > numSurfaces) {
            if (error != nullptr) { *error = "unsinnige Kinderzahl"; }
            return false;
        }
        p += 144 + static_cast<std::size_t>(numChildren) * 4;
    }

    // --- LOD 0: die feinste Stufe ---
    const std::size_t lod = static_cast<std::size_t>(ofsLods);
    if (lod + 4 + static_cast<std::size_t>(numSurfaces) * 4 > b.size()) {
        if (error != nullptr) { *error = "LOD-Verzeichnis abgeschnitten"; }
        return false;
    }

    bool first = true;
    for (std::int32_t i = 0; i < numSurfaces; ++i) {
        const std::int32_t rel = i32(b, lod + 4 + static_cast<std::size_t>(i) * 4);
        const std::size_t base = lod + 4 + static_cast<std::size_t>(rel);
        if (rel < 0 || base + 40 > b.size()) {
            continue;
        }
        GlmSurface& s = out.surfaces[static_cast<std::size_t>(i)];
        const std::int32_t numVerts = i32(b, base + 12);
        const std::int32_t ofsVerts = i32(b, base + 16);
        const std::int32_t numTris = i32(b, base + 20);
        const std::int32_t ofsTris = i32(b, base + 24);
        if (numVerts < 0 || numTris < 0 || numVerts > 65536 || numTris > 65536) {
            continue;
        }

        // Dreiecke: je drei int32.
        const std::size_t triAt = base + static_cast<std::size_t>(ofsTris);
        if (triAt + static_cast<std::size_t>(numTris) * 12 <= b.size()) {
            s.indexes.reserve(static_cast<std::size_t>(numTris) * 3);
            for (std::int32_t t = 0; t < numTris; ++t) {
                for (int k = 0; k < 3; ++k) {
                    const std::int32_t idx =
                        i32(b, triAt + static_cast<std::size_t>(t) * 12 +
                                   static_cast<std::size_t>(k) * 4);
                    s.indexes.push_back(
                        (idx >= 0 && idx < numVerts) ? static_cast<std::uint32_t>(idx) : 0U);
                }
            }
        }

        // Vertices. Der Aufbau ist ungewoehnlich und muss nachgemessen
        // werden: erst alle Normalen+Positionen+Gewichtsangaben als Block,
        // DANACH die Texturkoordinaten als eigener Block. Genau so schreibt
        // g2c sie auch.
        // Die Knochenverweise der Flaeche. Die 5-Bit-Indizes im Vertex
        // zeigen HIERHIN, nicht ins Skelett: fuenf Bit reichen nur fuer 32
        // Werte, ein Skelett hat 53 Knochen.
        const std::int32_t numBoneRefs = i32(b, base + 28);
        const std::int32_t ofsBoneRefs = i32(b, base + 32);
        std::vector<std::int32_t> boneRefs;
        if (numBoneRefs > 0 && numBoneRefs <= 64) {
            const std::size_t rAt = base + static_cast<std::size_t>(ofsBoneRefs);
            if (rAt + static_cast<std::size_t>(numBoneRefs) * 4 <= b.size()) {
                boneRefs.reserve(static_cast<std::size_t>(numBoneRefs));
                for (std::int32_t r = 0; r < numBoneRefs; ++r) {
                    std::int32_t ref = i32(b, rAt + static_cast<std::size_t>(r) * 4);
                    // JK2-Modell auf das JKA-Skelett umlegen (siehe
                    // kJk2NachJka) - wie R_LoadMDXM die BoneRefs umschreibt.
                    if (jk2Modell) {
                        ref = (ref >= 0 && ref < 72) ? kJk2NachJka[ref] : 0;
                    }
                    boneRefs.push_back(ref);
                }
            }
        }

        const std::size_t vAt = base + static_cast<std::size_t>(ofsVerts);
        // 32 Byte je Vertex: Normale(12) xyz(12) packed(4) und VIER
        // Gewichtsbytes. Danach ein eigener Block mit 8 Byte
        // Texturkoordinaten je Vertex, danach die BoneRefs.
        //
        // Nachgemessen an model.glm, Flaeche "hips": ofsVerts 2260,
        // 136 Vertices, ofsBoneRefs 7700. Zwischen Vertexblock und BoneRefs
        // liegen 1088 Byte = genau 8 je Vertex. Die Rechnung geht auf.
        constexpr std::size_t kVertSize = 32;
        const std::size_t stAt = vAt + static_cast<std::size_t>(numVerts) * kVertSize;
        if (stAt + static_cast<std::size_t>(numVerts) * 8 > b.size()) {
            continue;
        }
        s.verts.resize(static_cast<std::size_t>(numVerts));
        for (std::int32_t v = 0; v < numVerts; ++v) {
            const std::size_t o = vAt + static_cast<std::size_t>(v) * kVertSize;
            GlmVertex& gv = s.verts[static_cast<std::size_t>(v)];
            for (int k = 0; k < 3; ++k) {
                gv.normal[k] = f32(b, o + static_cast<std::size_t>(k) * 4);
                gv.xyz[k] = f32(b, o + 12 + static_cast<std::size_t>(k) * 4);
            }
            const std::size_t so = stAt + static_cast<std::size_t>(v) * 8;
            gv.st[0] = f32(b, so);
            gv.st[1] = f32(b, so + 4);

            // Knochengewichte. Sie stecken gepackt in vier Byte
            // "uiNmWeightsAndBoneIndexes" plus einem Byte je Gewicht:
            //   Bit 30-31  Anzahl der Gewichte minus eins
            //   Bit 0-19   fuenf Bit je Knochenverweis (bis vier)
            //   Bit 20-29  die oberen zwei Bit jedes Gewichts
            // Das letzte Gewicht ergibt sich als Rest zu 1 - so schreibt es
            // auch g2c.
            const std::uint32_t packed =
                static_cast<std::uint32_t>(i32(b, o + 24));
            const int numWeights = static_cast<int>((packed >> 30) & 0x3U) + 1;
            float total = 0.0F;
            for (int k = 0; k < numWeights; ++k) {
                // Der 5-Bit-Wert ist ein Index in die BoneRefs DIESER
                // Flaeche, nicht in das Skelett. Hier wird er aufgeloest -
                // ohne das bekommt man eine zerrissene Figur, und zwar ohne
                // jede Fehlermeldung.
                const auto local = static_cast<std::size_t>((packed >> (5 * k)) & 0x1FU);
                const std::int32_t global =
                    (local < boneRefs.size()) ? boneRefs[local] : 0;
                gv.bones[k] = static_cast<std::uint8_t>(
                    (global >= 0 && global < 256) ? global : 0);
                if (k + 1 == numWeights) {
                    gv.weights[k] = 1.0F - total;
                } else {
                    // Acht untere Bit aus dem Byte, zwei obere aus packed.
                    //
                    // Nachgerechnet gegen G2_GetVertBoneWeight
                    // (mdx_format.h:341). Dort steht
                    //
                    //     iTemp |= (packed >> (TOPBITS_SHIFT + k*2)) & 0x300
                    //
                    // mit TOPBITS_SHIFT = 5*4 - 8 = 12. Das sieht anders aus
                    // als die Fassung hier (Versatz 20, Maske 0x3, dann acht
                    // nach links) - es ist aber DASSELBE: beide holen die
                    // Bits 20+2k und 21+2k und legen sie auf Stelle 8 und 9.
                    //
                    // Ich habe es umgeschrieben, weil ich einen Fehler
                    // vermutete, und dann gemessen: null Unterschied im
                    // Bild. Die Notiz bleibt, damit der naechste nicht
                    // denselben Umweg geht.
                    const auto lower = static_cast<std::uint32_t>(
                        static_cast<unsigned char>(b[o + 28 + static_cast<std::size_t>(k)]));
                    const std::uint32_t upper =
                        (packed >> (20 + 2 * static_cast<std::uint32_t>(k))) & 0x3U;
                    gv.weights[k] =
                        static_cast<float>(lower | (upper << 8)) / 1023.0F;
                    total += gv.weights[k];
                }
            }

            for (int k = 0; k < 3; ++k) {
                if (first) {
                    out.mins[k] = out.maxs[k] = gv.xyz[k];
                } else {
                    out.mins[k] = std::min(out.mins[k], gv.xyz[k]);
                    out.maxs[k] = std::max(out.maxs[k], gv.xyz[k]);
                }
            }
            first = false;
        }
    }
    // Was das Modell wirklich mitbringt.
    std::size_t dreiecke = 0;
    std::size_t ecken = 0;
    for (const GlmSurface& s : out.surfaces) {
        dreiecke += s.indexes.size() / 3;
        ecken += s.verts.size();
    }
    diag::detail("GLM gelesen: \"" + out.name + "\", " +
                 std::to_string(b.size()) + " Bytes, " +
                 std::to_string(out.surfaces.size()) + " Flaechen, " +
                 std::to_string(dreiecke) + " Dreiecke, " +
                 std::to_string(ecken) + " Ecken, " +
                 std::to_string(out.numBones) + " Knochen, " +
                 std::to_string(out.numLods) + " Stufen, Skelett \"" +
                 out.animFile + "\"");
    return true;
}

void applySkin(const std::string& text, GlmModel& model) {
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t eol = text.find('\n', at);
        if (eol == std::string::npos) {
            eol = text.size();
        }
        const std::string line = trim(text.substr(at, eol - at));
        at = eol + 1;
        if (line.empty() || line[0] == '/') {
            continue;
        }
        const std::size_t comma = line.find(',');
        if (comma == std::string::npos) {
            continue;
        }
        std::string surface = trim(line.substr(0, comma));
        const std::string tex = trim(line.substr(comma + 1));

        // --- Die Endung "_off" im FLAECHENnamen ---------------------------
        //
        // Eine zweite Schreibweise neben "*off", die ich beim Nachlesen von
        // tr_skin.cpp gefunden habe (Zeile 310 ff.):
        //
        //     if ( !strcmp( &surfName[strlen(surfName)-4], "_off") )
        //     {
        //         if ( !strcmp( token ,"*off" ) )
        //             continue;             // don't need these double offs
        //         surfName[strlen(surfName)-4] = 0;   // remove the "_off"
        //     }
        //
        // Zwei Dinge stecken darin, und beide sind leicht zu uebersehen:
        //
        // 1. Heisst die Zeile "x_off,*off", wird sie GANZ uebersprungen -
        //    nicht etwa die Flaeche "x_off" abgeschaltet. Die Engine nennt
        //    das "double offs".
        // 2. Sonst wird das Suffix GESTRICHEN: "torso_off,irgendwas" faerbt
        //    die Flaeche "torso", nicht "torso_off".
        //
        // Ohne den zweiten Punkt sucht behaved eine Flaeche, die es nicht
        // gibt, und die Zeile bleibt wirkungslos.
        if (surface.size() > 4 &&
            surface.compare(surface.size() - 4, 4, "_off") == 0) {
            if (tex == "*off") {
                continue;   // doppeltes Aus - die Engine ueberspringt es
            }
            surface.resize(surface.size() - 4);
        }

        for (GlmSurface& s : model.surfaces) {
            if (s.name != surface) {
                continue;
            }
            // "*off" schaltet die Flaeche ab, statt ihr eine Textur zu geben.
            //
            // Belegt in tr_ghoul2.cpp: dort wird bei shader->name == "*off"
            // G2_SetSurfaceOnOff mit G2SURFACEFLAG_OFF gerufen. Ohne das
            // werden alle Varianten eines Modells gleichzeitig gezeichnet -
            // zwei Koepfe ineinander.
            if (!tex.empty() && tex[0] == '*') {
                s.skinnedOff = (tex == "*off");
                if (!s.skinnedOff) {
                    // "*default" und Verwandte: die Flaeche bleibt an, nur
                    // ohne eigene Textur.
                    s.texture.clear();
                }
                continue;
            }
            s.skinnedOff = false;
            s.texture = tex;
        }
    }
}


int surfaceIndex(const GlmModel& model, const std::string& name) {
    for (std::size_t i = 0; i < model.surfaces.size(); ++i) {
        if (model.surfaces[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

namespace {
// Aus drei bereits verformten Ecken eine Ausrichtung bauen.
//
// Zeile fuer Zeile nach G2_ProcessSurfaceBolt2 (tr_ghoul2.cpp:2080 ff.,
// der Zweig fuer eine gewoehnliche Tagflaeche).
bool boltFromTriangle(const float p[3][3], BoneMatrix& out) {
    // Die Seitennummern sind NICHT frei gewaehlt: mdx_format.h:78 ff. legt
    // fest, dass Seite 0 die laengste und Seite 2 die kuerzeste ist, und
    // MDX_TAG_ORIGIN (tr_ghoul2.cpp:1935) sagt, dass die dritte Ecke der
    // Ursprung ist. Das Werkzeug, das die Modelle baut, ordnet die Ecken
    // entsprechend an - wer hier selbst misst, welche Seite laenger ist,
    // bekommt bei fast gleich langen Seiten ein Flackern.
    constexpr int kLongest = 0;
    constexpr int kShortest = 2;
    constexpr int kOrigin = 2;

    float sides[3][3] = {};
    for (int j = 0; j < 3; ++j) {
        for (int c = 0; c < 3; ++c) {
            sides[j][c] = p[(j + 1) % 3][c] - p[j][c];
        }
    }
    auto norm = [](const float in[3], float o[3]) {
        const float l = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
        if (l < 1e-6F) {
            o[0] = o[1] = o[2] = 0.0F;
            return false;
        }
        for (int c = 0; c < 3; ++c) {
            o[c] = in[c] / l;
        }
        return true;
    };
    float ax0[3] = {};
    float ax1[3] = {};
    float ax2[3] = {};
    if (!norm(sides[kLongest], ax0) || !norm(sides[kShortest], ax1)) {
        return false;   // entartetes Dreieck
    }
    // Die laengere Seite genau rechtwinklig zur kuerzeren stellen.
    const float d = ax0[0] * ax1[0] + ax0[1] * ax1[1] + ax0[2] * ax1[2];
    const float tmp[3] = {ax0[0] - d * ax1[0], ax0[1] - d * ax1[1],
                          ax0[2] - d * ax1[2]};
    if (!norm(tmp, ax0)) {
        return false;
    }
    const float kreuz[3] = {
        sides[kLongest][1] * sides[kShortest][2] -
            sides[kLongest][2] * sides[kShortest][1],
        sides[kLongest][2] * sides[kShortest][0] -
            sides[kLongest][0] * sides[kShortest][2],
        sides[kLongest][0] * sides[kShortest][1] -
            sides[kLongest][1] * sides[kShortest][0]};
    if (!norm(kreuz, ax2)) {
        return false;
    }
    // Die Spaltenbelegung ist die aus der Engine, samt Vorzeichen: dort
    // steht dazu "do some magic to orient minus Y to positive X and so on
    // so bolt on stuff is oriented correctly". Wer sie umsortiert, haelt
    // das Schwert verkehrt herum.
    for (int r = 0; r < 3; ++r) {
        out.m[r][0] = ax1[r];
        out.m[r][1] = ax0[r];
        out.m[r][2] = -ax2[r];
        out.m[r][3] = p[kOrigin][r];
    }
    return true;
}
}  // namespace

bool boltMatrixRigid(const GlmModel& model, int surface, BoneMatrix& out) {
    if (surface < 0 ||
        static_cast<std::size_t>(surface) >= model.surfaces.size()) {
        return false;
    }
    const GlmSurface& s = model.surfaces[static_cast<std::size_t>(surface)];
    if (s.verts.size() < 3) {
        return false;
    }
    float p[3][3] = {};
    for (int j = 0; j < 3; ++j) {
        for (int c = 0; c < 3; ++c) {
            p[j][c] = s.verts[static_cast<std::size_t>(j)].xyz[c];
        }
    }
    return boltFromTriangle(p, out);
}

bool boltMatrix(const GlmModel& model, int surface,
                const std::vector<BoneMatrix>& world,
                const GlaAnimation& anim, BoneMatrix& out) {
    if (surface < 0 ||
        static_cast<std::size_t>(surface) >= model.surfaces.size()) {
        return false;
    }
    const GlmSurface& s = model.surfaces[static_cast<std::size_t>(surface)];
    // Eine Tagflaeche ist GENAU ein Dreieck. Ist sie es nicht, ist es keine.
    if (s.verts.size() < 3) {
        return false;
    }

    // Die drei Ecken wie jeden anderen Vertex verformen.
    float p[3][3] = {};
    for (int j = 0; j < 3; ++j) {
        const GlmVertex& v = s.verts[static_cast<std::size_t>(j)];
        for (int k = 0; k < 4; ++k) {
            if (v.weights[k] <= 0.0001F) {
                continue;
            }
            const auto b = static_cast<std::size_t>(v.bones[k]);
            if (b >= world.size() || b >= anim.bones.size()) {
                continue;
            }
            float lokal[3] = {};
            anim.bones[b].basePoseInv.transform(v.xyz, lokal);
            float welt[3] = {};
            world[b].transform(lokal, welt);
            for (int c = 0; c < 3; ++c) {
                p[j][c] += welt[c] * v.weights[k];
            }
        }
    }

    return boltFromTriangle(p, out);
}

}  // namespace bhed
