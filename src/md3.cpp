// md3.cpp - siehe bhed/md3.h
#include "bhed/md3.h"
#include "bhed/diag.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace bhed {
namespace {

// std::numbers::pi_v ist genauer als jede handgeschriebene
// Konstante - der Unterschied betraegt 8,7e-08. Gemeldet von
// modernize-use-std-numbers.
constexpr float kPi = std::numbers::pi_v<float>;
// MD3_XYZ_SCALE aus qfiles.h.
constexpr float kXyzScale = 1.0F / 64.0F;
constexpr std::size_t kMaxQPath = 64;

std::int32_t i32(const std::string& b, std::size_t at) {
    if (at + 4 > b.size()) {
        return 0;
    }
    std::int32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

std::int16_t i16(const std::string& b, std::size_t at) {
    if (at + 2 > b.size()) {
        return 0;
    }
    std::int16_t v = 0;
    std::memcpy(&v, b.data() + at, 2);
    return v;
}

float f32(const std::string& b, std::size_t at) {
    if (at + 4 > b.size()) {
        return 0.0F;
    }
    float v = 0.0F;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

std::string cstr(const std::string& b, std::size_t at, std::size_t max) {
    if (at >= b.size()) {
        return {};
    }
    const std::size_t end = std::min(at + max, b.size());
    std::string out;
    for (std::size_t i = at; i < end && b[i] != '\0'; ++i) {
        out.push_back(b[i]);
    }
    return out;
}

// Die gepackte Normale auspacken - genau wie tr_surface.cpp, Zeile 1212.
//
// Die Engine schlaegt dafuer in einer Sinustabelle mit 1024 Eintraegen
// nach; wir rechnen direkt. Das Ergebnis ist dasselbe bis auf die
// Tabellenaufloesung, und eine Vorschau braucht die nicht.
void unpackNormal(std::int16_t packed, float out[3]) {
    const auto lat = static_cast<float>((packed >> 8) & 0xFF) *
                     (2.0F * kPi / 256.0F);
    const auto lng = static_cast<float>(packed & 0xFF) * (2.0F * kPi / 256.0F);
    out[0] = std::cos(lat) * std::sin(lng);
    out[1] = std::sin(lat) * std::sin(lng);
    out[2] = std::cos(lng);
}

}  // namespace

const Md3Vertex* Md3Surface::frame(int index) const {
    if (verts.empty() || numVerts <= 0 || numFrames <= 0) {
        return nullptr;
    }
    int f = index % numFrames;
    if (f < 0) {
        f += numFrames;
    }
    const auto at = static_cast<std::size_t>(f) *
                    static_cast<std::size_t>(numVerts);
    if (at + static_cast<std::size_t>(numVerts) > verts.size()) {
        return nullptr;
    }
    return verts.data() + at;
}

bool readMd3(const std::string& b, Md3Model& out, std::string* error) {
    out = Md3Model{};
    // md3Header_t ist 108 Byte: 2 int, name[64], 8 int.
    constexpr std::size_t kHeader = 4 + 4 + kMaxQPath + std::size_t{4} * 8;
    if (b.size() < kHeader) {
        if (error != nullptr) { *error = "zu kurz fuer eine .md3"; }
        return false;
    }
    if (b.compare(0, 4, "IDP3") != 0) {
        if (error != nullptr) { *error = "keine .md3 (Kennung fehlt)"; }
        return false;
    }
    const std::int32_t version = i32(b, 4);
    if (version != 15) {
        if (error != nullptr) {
            *error = "unerwartete Fassung " + std::to_string(version) +
                     " (erwartet 15)";
        }
        return false;
    }

    out.name = cstr(b, 8, kMaxQPath);
    const std::int32_t numFrames = i32(b, 8 + kMaxQPath + 4);
    const std::int32_t numSurfaces = i32(b, 8 + kMaxQPath + 12);
    const std::int32_t ofsSurfaces = i32(b, 8 + kMaxQPath + 28);

    // Die Obergrenze fuer numSurfaces war ERFUNDEN.
    //
    // Ich hatte 256 angesetzt, weil qfiles.h MD3_MAX_SURFACES auf 32+32
    // definiert - also schien 256 grosszuegig. R_LoadMD3() in
    // tr_model.cpp prueft die Zahl aber ueberhaupt NICHT; begrenzt wird nur
    // je Flaeche gegen SHADER_MAX_VERTEXES. Und das ist kein theoretischer
    // Unterschied: models/map_objects/TFed/deka_dispenser.MD3 aus
    // MD_Maps_Ep1 hat 300 Flaechen und laedt im Spiel einwandfrei.
    //
    // Was hier bleibt, ist nur eine Wache gegen Unsinn in einer kaputten
    // Datei, keine Formatgrenze.
    if (numFrames <= 0 || numFrames > 4096 || numSurfaces < 0 ||
        numSurfaces > 8192 || ofsSurfaces < 0) {
        if (error != nullptr) { *error = "unsinnige Angaben im Kopf"; }
        return false;
    }
    out.numFrames = numFrames;

    // --- Die Tags (Lump zwischen Bildern und Flaechen) --------------------
    //
    // Ein Tag ist eine benannte Stelle im Modell samt Ausrichtung. Die
    // Muendung einer Waffe heisst "tag_flash", und an ihr haengen der
    // Muendungsblitz und der Startpunkt des Geschosses.
    //
    // Der Satz ist md3Tag_t (qfiles.h:118) und misst 112 Byte:
    // name[64], origin (3 float), axis[3] (9 float).
    //
    // Er steht JE BILD - numTags * numFrames Saetze, bildweise
    // hintereinander. Bei Waffen ist numFrames 1, aber darauf verlassen
    // darf man sich nicht.
    {
        const std::int32_t numTags = i32(b, 8 + kMaxQPath + 8);
        const std::int32_t ofsTags = i32(b, 8 + kMaxQPath + 24);
        constexpr std::size_t kTagSize = 112;
        if (numTags > 0 && numTags <= 1024 && ofsTags >= 0) {
            const auto anzahl =
                static_cast<std::size_t>(numTags) *
                static_cast<std::size_t>(numFrames);
            const auto beginn = static_cast<std::size_t>(ofsTags);
            // Passt der Bereich nicht in die Datei, lesen wir GAR KEINE
            // Tags. Halb gelesene waeren schlimmer als keine: eine Waffe
            // mit einer Muendung an einer erfundenen Stelle sieht aus wie
            // ein Fehler in der Zielrechnung.
            if (beginn + anzahl * kTagSize <= b.size()) {
                out.numTags = numTags;
                out.tags.resize(anzahl);
                for (std::size_t i = 0; i < anzahl; ++i) {
                    const std::size_t o = beginn + i * kTagSize;
                    Md3Tag& t = out.tags[i];
                    t.name = cstr(b, o, kMaxQPath);
                    for (int k = 0; k < 3; ++k) {
                        t.origin[k] = f32(b, o + kMaxQPath +
                                                 static_cast<std::size_t>(k) * 4);
                    }
                    for (int r = 0; r < 3; ++r) {
                        for (int k = 0; k < 3; ++k) {
                            t.axis[r][k] = f32(
                                b, o + kMaxQPath + 12 +
                                       static_cast<std::size_t>(r * 3 + k) * 4);
                        }
                    }
                }
            }
        }
    }

    auto at = static_cast<std::size_t>(ofsSurfaces);
    for (std::int32_t s = 0; s < numSurfaces; ++s) {
        // md3Surface_t: ident, name[64], dann 10 int.
        constexpr std::size_t kSurfHeader = 4 + kMaxQPath + std::size_t{4} * 10;
        if (at + kSurfHeader > b.size()) {
            break;   // abgeschnittene Datei: was da ist, behalten
        }
        Md3Surface sf;
        sf.name = cstr(b, at + 4, kMaxQPath);
        const std::size_t n = at + 4 + kMaxQPath;
        const std::int32_t sFrames = i32(b, n + 4);
        const std::int32_t sShaders = i32(b, n + 8);
        const std::int32_t sVerts = i32(b, n + 12);
        const std::int32_t sTris = i32(b, n + 16);
        const std::int32_t ofsTris = i32(b, n + 20);
        const std::int32_t ofsShaders = i32(b, n + 24);
        const std::int32_t ofsSt = i32(b, n + 28);
        const std::int32_t ofsXyz = i32(b, n + 32);
        const std::int32_t ofsEnd = i32(b, n + 36);

        // Eine LEERE Flaeche beendet die Datei nicht.
        //
        // models/map_objects/bespin/engi_wall3.md3 aus MD_Maps_Ep5 hat zwei
        // Flaechen mit null Ecken. R_LoadMD3() prueft nur auf ZU VIELE
        // (SHADER_MAX_VERTEXES), nicht auf null - die Datei laedt im Spiel.
        // Ein break hier hat sie als "keine lesbare Flaeche" abgelehnt.
        //
        // Unbrauchbare Angaben beenden dagegen sehr wohl: ohne ofsEnd
        // wuesste man nicht, wo die naechste Flaeche anfaengt.
        if (ofsEnd <= 0 || sFrames <= 0 || sVerts > 65536 || sTris > 65536 ||
            sVerts < 0 || sTris < 0) {
            break;
        }
        if (sVerts == 0) {
            at += static_cast<std::size_t>(ofsEnd);
            continue;   // nichts zu zeichnen, aber die Datei geht weiter
        }
        sf.numVerts = sVerts;
        sf.numFrames = sFrames;

        // Der Shadername der ersten Stufe.
        if (sShaders > 0 && ofsShaders > 0) {
            sf.shader = cstr(b, at + static_cast<std::size_t>(ofsShaders),
                             kMaxQPath);
        }

        // Dreiecke.
        for (std::int32_t t = 0; t < sTris; ++t) {
            const std::size_t ta = at + static_cast<std::size_t>(ofsTris) +
                                   static_cast<std::size_t>(t) * 12U;
            if (ta + 12 > b.size()) {
                break;
            }
            bool ok = true;
            std::uint32_t idx[3];
            for (int k = 0; k < 3; ++k) {
                const std::int32_t v = i32(b, ta + static_cast<std::size_t>(k) * 4);
                if (v < 0 || v >= sVerts) {
                    ok = false;
                    break;
                }
                idx[k] = static_cast<std::uint32_t>(v);
            }
            if (ok) {
                for (const std::uint32_t v : idx) {
                    sf.indexes.push_back(v);
                }
            }
        }

        // Ecken: numVerts * numFrames, dazu die Texturkoordinaten, die fuer
        // ALLE Bilder gelten und deshalb nur einmal dastehen.
        const std::size_t total = static_cast<std::size_t>(sVerts) *
                                  static_cast<std::size_t>(sFrames);
        // Erst pruefen, ob so viele Ecken ueberhaupt in der Datei stehen:
        // numFrames 2^31 bei 65536 Ecken wollte sonst 2^47 Ecken anlegen -
        // bad_alloc und Absturz an einem einzigen kaputten Modell
        // (Code-Pruefung 03.10.).
        if (ofsXyz < 0 || at + static_cast<std::size_t>(ofsXyz) > b.size() ||
            total > (b.size() - at - static_cast<std::size_t>(ofsXyz)) / 8U) {
            break;
        }
        sf.verts.resize(total);
        for (std::size_t i = 0; i < total; ++i) {
            const std::size_t va = at + static_cast<std::size_t>(ofsXyz) + i * 8U;
            if (va + 8 > b.size()) {
                break;
            }
            Md3Vertex& v = sf.verts[i];
            for (int k = 0; k < 3; ++k) {
                v.xyz[k] = static_cast<float>(
                               i16(b, va + static_cast<std::size_t>(k) * 2)) *
                           kXyzScale;
            }
            unpackNormal(i16(b, va + 6), v.normal);

            const std::size_t sa = at + static_cast<std::size_t>(ofsSt) +
                                   (i % static_cast<std::size_t>(sVerts)) * 8U;
            v.st[0] = f32(b, sa);
            v.st[1] = f32(b, sa + 4);
        }

        out.surfaces.push_back(std::move(sf));
        at += static_cast<std::size_t>(ofsEnd);
    }

    // Eine .md3 GANZ OHNE Flaechen ist gueltig.
    //
    // Sie enthaelt dann nur Tags - Anheftpunkte. So sind die
    // *_hand.md3 der Waffen gebaut: sie zeigen nichts, sie sagen der
    // Engine, wo die Waffe in der Hand sitzt. Ich hatte das zuerst als
    // Fehler behandelt und damit sechs gueltige Dateien abgelehnt.
    //
    // Wer zeichnen will, fragt empty() - nicht den Rueckgabewert.
    std::size_t dreiecke = 0;
    for (const Md3Surface& s : out.surfaces) {
        dreiecke += s.indexes.size() / 3;
    }
    diag::detail("MD3 gelesen: \"" + out.name + "\", " +
                 std::to_string(b.size()) + " Bytes, " +
                 std::to_string(out.surfaces.size()) + " Flaechen, " +
                 std::to_string(dreiecke) + " Dreiecke, " +
                 std::to_string(out.numFrames) + " Bilder, " +
                 std::to_string(out.numTags) + " Anheftpunkte");
    return true;
}

}  // namespace bhed

namespace bhed {

BspMesh md3ToMesh(const Md3Model& m, int frame, int shaderBase) {
    BspMesh out;
    int slot = shaderBase;
    for (const Md3Surface& s : m.surfaces) {
        const Md3Vertex* src = s.frame(frame);
        if (src == nullptr || s.indexes.empty()) {
            ++slot;
            continue;
        }
        const auto base = static_cast<std::uint32_t>(out.verts.size());
        for (int k = 0; k < s.numVerts; ++k) {
            BspVertex v;
            for (int c = 0; c < 3; ++c) {
                v.xyz[c] = src[k].xyz[c];
                v.normal[c] = src[k].normal[c];
            }
            v.st[0] = src[k].st[0];
            v.st[1] = src[k].st[1];
            for (std::uint8_t& c : v.colour) {
                c = 255;
            }
            out.verts.push_back(v);
        }
        BspMesh::Batch b;
        b.firstIndex = static_cast<std::uint32_t>(out.indexes.size());
        // Der Umlauf: .md3 wickelt wie JKA, also entgegen der Normalen -
        // dieselbe Regel wie bei den Flaechen der Karte (tr_backend.cpp,
        // GL_Cull ruft bei CT_FRONT_SIDED qglCullFace( GL_FRONT ) auf).
        // Deshalb hier NICHT umdrehen.
        for (const std::uint32_t i : s.indexes) {
            out.indexes.push_back(base + i);
        }
        b.numIndexes = static_cast<std::uint32_t>(out.indexes.size()) -
                       b.firstIndex;
        b.lightmap = -3;   // Vertexfarben: ein .md3 hat keine Lightmap
        b.shader = slot;
        out.batches.push_back(b);
        ++slot;
    }
    return out;
}


const Md3Tag* Md3Model::tag(const std::string& name, int frame) const {
    if (numTags <= 0 || tags.empty()) {
        return nullptr;
    }
    // Das Bild laeuft um, wie bei Md3Surface::frame() - ein zu grosser Wert
    // soll nicht ins Leere greifen.
    const int bilder = std::max(numFrames, 1);
    const int f = ((frame % bilder) + bilder) % bilder;
    const auto beginn = static_cast<std::size_t>(f) *
                        static_cast<std::size_t>(numTags);
    for (int i = 0; i < numTags; ++i) {
        const std::size_t at = beginn + static_cast<std::size_t>(i);
        if (at < tags.size() && tags[at].name == name) {
            return &tags[at];
        }
    }
    return nullptr;
}

}  // namespace bhed
