// md3test.cpp - der Leser fuer .md3-Modelle.
//
// Geprueft wird gegen die Beschreibung in code/qcommon/qfiles.h und gegen
// echte Dateien: die 451 .md3 der Mod muessen sich alle lesen lassen.
#include "bhed/md3.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_fails = 0;

void expect(const char* what, bool ok) {
    if (!ok) {
        ++g_fails;
    }
    std::printf("  %-4s %s\n", ok ? "ok" : "FEHL", what);
}

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

void put32(std::string& b, std::size_t at, std::int32_t v) {
    std::memcpy(b.data() + at, &v, 4);
}

void put16(std::string& b, std::size_t at, std::int16_t v) {
    std::memcpy(b.data() + at, &v, 2);
}

void putf(std::string& b, std::size_t at, float v) {
    std::memcpy(b.data() + at, &v, 4);
}

// Eine kleine, aber echte .md3: ein Dreieck, zwei Bilder.
std::string bastle() {
    constexpr std::size_t kQ = 64;
    constexpr std::size_t kHdr = 4 + 4 + kQ + std::size_t{4} * 8;   // 108
    constexpr std::size_t kSurfHdr = 4 + kQ + std::size_t{4} * 10;  // 108
    const int verts = 3;
    const int frames = 2;
    const std::size_t ofsShaders = kSurfHdr;
    const std::size_t ofsTris = ofsShaders + kQ + 4;
    const std::size_t ofsSt = ofsTris + 12;
    const std::size_t ofsXyz = ofsSt + static_cast<std::size_t>(verts) * 8;
    const std::size_t surfEnd =
        ofsXyz + static_cast<std::size_t>(verts * frames) * 8;

    std::string b(kHdr + surfEnd, '\0');
    std::memcpy(b.data(), "IDP3", 4);
    put32(b, 4, 15);
    std::memcpy(b.data() + 8, "probe", 5);
    put32(b, 8 + kQ + 4, frames);       // numFrames
    put32(b, 8 + kQ + 8, 0);            // numTags
    put32(b, 8 + kQ + 12, 1);           // numSurfaces
    put32(b, 8 + kQ + 28, static_cast<std::int32_t>(kHdr));   // ofsSurfaces
    put32(b, 8 + kQ + 32, static_cast<std::int32_t>(b.size()));

    const std::size_t s = kHdr;
    std::memcpy(b.data() + s + 4, "flaeche", 7);
    const std::size_t n = s + 4 + kQ;
    put32(b, n + 4, frames);            // numFrames
    put32(b, n + 8, 1);                 // numShaders
    put32(b, n + 12, verts);            // numVerts
    put32(b, n + 16, 1);                // numTriangles
    put32(b, n + 20, static_cast<std::int32_t>(ofsTris));
    put32(b, n + 24, static_cast<std::int32_t>(ofsShaders));
    put32(b, n + 28, static_cast<std::int32_t>(ofsSt));
    put32(b, n + 32, static_cast<std::int32_t>(ofsXyz));
    put32(b, n + 36, static_cast<std::int32_t>(surfEnd));

    std::memcpy(b.data() + s + ofsShaders, "textures/x/y", 12);
    for (int k = 0; k < 3; ++k) {
        put32(b, s + ofsTris + static_cast<std::size_t>(k) * 4, k);
        putf(b, s + ofsSt + static_cast<std::size_t>(k) * 8,
             static_cast<float>(k) * 0.5F);
        putf(b, s + ofsSt + static_cast<std::size_t>(k) * 8 + 4, 0.25F);
    }
    // Bild 0: x = 64, 128, 192 -> nach der Skalierung 1, 2, 3.
    // Bild 1: doppelt so weit.
    for (int f = 0; f < frames; ++f) {
        for (int k = 0; k < verts; ++k) {
            const std::size_t at =
                s + ofsXyz +
                (static_cast<std::size_t>(f * verts + k)) * 8;
            put16(b, at, static_cast<std::int16_t>(64 * (k + 1) * (f + 1)));
            put16(b, at + 2, 0);
            put16(b, at + 4, 0);
            // Normale: lat = 0, lng = 64 -> lng = 90 Grad -> (1, 0, 0)
            put16(b, at + 6, static_cast<std::int16_t>(64));
        }
    }
    return b;
}

}  // namespace

int main(int argc, char** argv) {
    // --- Eine gebastelte Datei ------------------------------------------
    {
        bhed::Md3Model m;
        std::string err;
        expect("eine gueltige .md3 liest sich", bhed::readMd3(bastle(), m, &err));
        expect("zwei Bilder", m.numFrames == 2);
        expect("eine Flaeche", m.surfaces.size() == 1U);
        if (!m.surfaces.empty()) {
            const bhed::Md3Surface& s = m.surfaces[0];
            expect("der Shadername steht da", s.shader == "textures/x/y");
            expect("ein Dreieck", s.indexes.size() == 3U);
            expect("drei Ecken je Bild", s.numVerts == 3);

            // MD3_XYZ_SCALE ist 1/64: 64 -> 1, 128 -> 2, 192 -> 3.
            const bhed::Md3Vertex* f0 = s.frame(0);
            expect("die Ecken sind mit 1/64 skaliert",
                   f0 != nullptr && std::fabs(f0[0].xyz[0] - 1.0F) < 0.001F &&
                       std::fabs(f0[2].xyz[0] - 3.0F) < 0.001F);

            // Bild 1 steht doppelt so weit - so entsteht die Bewegung.
            const bhed::Md3Vertex* f1 = s.frame(1);
            expect("das zweite Bild hat andere Ecken",
                   f1 != nullptr && std::fabs(f1[0].xyz[0] - 2.0F) < 0.001F);

            // Die gepackte Normale: lat = 0, lng = 64 von 256, also 90 Grad.
            //   x = cos(0) * sin(90) = 1
            //   y = sin(0) * sin(90) = 0
            //   z = cos(90)          = 0
            expect("die gepackte Normale wird richtig ausgepackt",
                   f0 != nullptr && std::fabs(f0[0].normal[0] - 1.0F) < 0.01F &&
                       std::fabs(f0[0].normal[1]) < 0.01F &&
                       std::fabs(f0[0].normal[2]) < 0.01F);

            // Die Texturkoordinaten gelten fuer ALLE Bilder.
            expect("die Texturkoordinaten sind in jedem Bild gleich",
                   f0 != nullptr && f1 != nullptr &&
                       f0[1].st[0] == f1[1].st[0]);

            // Ein zu grosser Bildindex greift umlaufend, nicht ins Leere.
            expect("ein zu grosser Bildindex laeuft um",
                   s.frame(7) == s.frame(1));
        }
    }

    // --- Kaputte Dateien -------------------------------------------------
    {
        bhed::Md3Model m;
        std::string err;
        expect("eine leere Datei wird abgelehnt", !bhed::readMd3("", m, &err));
        expect("eine fremde Kennung wird abgelehnt",
               !bhed::readMd3("IDP2xxxxxxxxxxxxxxxx", m, &err));
        std::string falscheFassung = bastle();
        put32(falscheFassung, 4, 14);
        expect("eine falsche Fassung wird abgelehnt",
               !bhed::readMd3(falscheFassung, m, &err));
        // Abgeschnitten: darf nicht stuerzen.
        const std::string ganz = bastle();
        for (std::size_t k = 1; k < ganz.size(); k += 37) {
            bhed::Md3Model m2;
            (void)bhed::readMd3(ganz.substr(0, k), m2, nullptr);
        }
        expect("abgeschnittene Dateien ueberstanden", true);
    }

    // --- Echte Dateien, wenn welche mitgegeben wurden ---------------------
    int ok = 0;
    int bad = 0;
    int animiert = 0;
    bool normalenGut = true;
    for (int i = 1; i < argc; ++i) {
        bhed::Md3Model m;
        std::string err;
        if (!bhed::readMd3(slurp(argv[i]), m, &err)) {
            std::printf("     NICHT LESBAR %s: %s\n", argv[i], err.c_str());
            ++bad;
            continue;
        }
        ++ok;
        if (m.numFrames > 1) {
            ++animiert;
        }
        // Jede Normale muss Einheitslaenge haben - sonst ist das Auspacken
        // falsch, und die Beleuchtung waere es auch.
        for (const bhed::Md3Surface& s : m.surfaces) {
            const bhed::Md3Vertex* v = s.frame(0);
            if (v == nullptr) {
                continue;
            }
            for (int k = 0; k < std::min(s.numVerts, 32); ++k) {
                const float l = std::sqrt(v[k].normal[0] * v[k].normal[0] +
                                          v[k].normal[1] * v[k].normal[1] +
                                          v[k].normal[2] * v[k].normal[2]);
                if (l < 0.99F || l > 1.01F) {
                    normalenGut = false;
                }
            }
        }
    }
    if (argc > 1) {
        std::printf("     %d gelesen, %d nicht, %d davon animiert\n", ok, bad,
                    animiert);
        expect("alle echten Modelle lesen sich", bad == 0);
        expect("alle Normalen haben Einheitslaenge", normalenGut);
    }

    // --- Tags: wo eine Waffe ihre Muendung hat -----------------------------
    //
    // Gefragt: "wo bekommen wir das Projektil her?"
    //
    // Von einem Tag namens "tag_flash". Der Satz ist md3Tag_t
    // (qfiles.h:118) und misst 112 Byte: name[64], origin, axis[3]. Er
    // steht JE BILD - numTags * numFrames Saetze, bildweise hintereinander.
    //
    // Die Probe baut ein Modell mit ZWEI Bildern und ZWEI Tags, damit die
    // Bildweise auffaellt: waere sie tagweise gelesen, kaeme in Bild 1 der
    // falsche Wert heraus.
    {
        constexpr std::size_t kQ2 = 64;
        constexpr std::size_t kHdr2 = 108;
        constexpr std::size_t kTag = 112;
        const std::size_t ofsTags = kHdr2;
        std::string b(kHdr2 + 4 * kTag, '\0');
        std::memcpy(b.data(), "IDP3", 4);
        put32(b, 4, 15);
        std::memcpy(b.data() + 8, "waffe", 5);
        put32(b, 8 + kQ2 + 4, 2);      // numFrames
        put32(b, 8 + kQ2 + 8, 2);      // numTags
        put32(b, 8 + kQ2 + 12, 0);     // numSurfaces
        put32(b, 8 + kQ2 + 24, static_cast<std::int32_t>(ofsTags));
        put32(b, 8 + kQ2 + 28, static_cast<std::int32_t>(b.size()));
        put32(b, 8 + kQ2 + 32, static_cast<std::int32_t>(b.size()));

        // Bild 0: tag_flash bei x=10, tag_weapon bei x=20.
        // Bild 1: tag_flash bei x=30, tag_weapon bei x=40.
        const char* namen[4] = {"tag_flash", "tag_weapon", "tag_flash",
                                "tag_weapon"};
        const float xs[4] = {10.0F, 20.0F, 30.0F, 40.0F};
        for (int i = 0; i < 4; ++i) {
            const std::size_t o = ofsTags + static_cast<std::size_t>(i) * kTag;
            std::memcpy(b.data() + o, namen[i], std::strlen(namen[i]));
            std::memcpy(b.data() + o + kQ2, &xs[i], 4);
        }

        bhed::Md3Model m;
        std::string err;
        expect("ein Modell mit Tags liest sich", bhed::readMd3(b, m, &err));
        expect("beide Tags sind da", m.numTags == 2);

        const bhed::Md3Tag* f0 = m.tag("tag_flash", 0);
        expect("tag_flash in Bild 0", f0 != nullptr && f0->origin[0] == 10.0F);
        // DAS ist der Fall, der die Bildweise prueft.
        const bhed::Md3Tag* f1 = m.tag("tag_flash", 1);
        expect("und in Bild 1 der ANDERE Wert",
               f1 != nullptr && f1->origin[0] == 30.0F);
        expect("der zweite Tag ebenso",
               m.tag("tag_weapon", 1) != nullptr &&
                   m.tag("tag_weapon", 1)->origin[0] == 40.0F);

        // Ein Name, den es nicht gibt, liefert nullptr - nicht den ersten
        // besten. Sonst haenge der Muendungsblitz an einer beliebigen
        // Stelle des Modells.
        expect("ein unbekannter Tag liefert nichts",
               m.tag("tag_gibtsnicht") == nullptr);

        // Und ein Modell OHNE Tags liefert auch nichts, statt zu greifen.
        bhed::Md3Model leer;
        expect("ohne Tags ebenfalls nichts", leer.tag("tag_flash") == nullptr);
    }

    std::printf("\n%s (%d Fehlschlaege)\n",
                g_fails == 0 ? "alle Modellproben bestanden" : "FEHLGESCHLAGEN",
                g_fails);
    return g_fails == 0 ? 0 : 1;
}
