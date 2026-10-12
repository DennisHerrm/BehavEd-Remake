// Fuzz-Ziel fuer libFuzzer (rc596): alle Dateileser, die fremde Dateien
// aus .pk3-Archiven und von der Platte lesen.
//
// Gebaut nur mit -DBEHAVED_FUZZ=ON (MSVC: /fsanitize=fuzzer,address), in
// einem eigenen Build-Ordner. Das erste Byte waehlt den Leser, der Rest ist
// die Datei. Forderung wie in tests/fuzz.cpp: kein Absturz, kein Treffer
// des AddressSanitizers, kein Haenger - egal wie kaputt die Datei ist.
//
// Aufruf etwa:
//   fuzzziel.exe korpus -max_len=1048576 -max_total_time=900 -timeout=10
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bhed/bsp.h"
#include "bhed/bspgeo.h"
#include "bhed/efx/io.h"
#include "bhed/gla.h"
#include "bhed/glm.h"
#include "bhed/ibi.h"
#include "bhed/image.h"
#include "bhed/inflate.h"
#include "bhed/md3.h"
#include "bhed/roff.h"
#include "bhed/script.h"
#include "bhed/shaderscript.h"
#include "bhed/sound.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 1) {
        return 0;
    }
    const unsigned leser = data[0];
    const auto* rest = reinterpret_cast<const unsigned char*>(data + 1);
    const std::size_t n = size - 1;
    const std::string text(reinterpret_cast<const char*>(rest), n);
    switch (leser % 16U) {
        case 0: {
            bhed::MapData m;
            (void)bhed::readBsp(text, m);
            break;
        }
        case 1: {
            bhed::BspGeometry g;
            (void)bhed::readBspGeometry(text, g);
            break;
        }
        case 2: {
            bhed::Md3Model m;
            (void)bhed::readMd3(text, m);
            break;
        }
        case 3: {
            bhed::GlmModel m;
            (void)bhed::readGlm(text, m);
            break;
        }
        case 4: {
            bhed::GlaAnimation a;
            (void)bhed::readGla(text, a);
            break;
        }
        case 5: {
            std::vector<bhed::IbiBlock> b;
            std::vector<bhed::Diag> d;
            (void)bhed::readIbi(text, b, d);
            break;
        }
        case 6: {
            bhed::Roff r;
            (void)bhed::leseRoff(text, r);
            break;
        }
        case 7:
            (void)bhed::image::decode(rest, n);
            break;
        case 8:
            (void)bhed::sound::decode(rest, n);
            break;
        case 9:
            // Die erwartete Groesse aus den ersten vier Bytes, gedeckelt.
            if (n >= 4) {
                const std::size_t erwartet = (static_cast<std::size_t>(rest[0]) | (static_cast<std::size_t>(rest[1]) << 8) |
                                              (static_cast<std::size_t>(rest[2]) << 16)) % (4U << 20);
                (void)bhed::inflate::raw(rest + 4, n - 4, erwartet);
            }
            break;
        case 10: {
            bhed::ShaderMap m;
            bhed::parseShaderScript(text, m);
            break;
        }
        case 11: {
            bhed::NebelMap m;
            bhed::parseFogParms(text, m);
            break;
        }
        case 12:
            (void)bhed::efx::read(std::string_view(text));
            break;
        case 13: {
            bhed::Script s;
            std::vector<bhed::Diag> d;
            (void)bhed::readScript(text, s, d);
            break;
        }
        case 14:
            (void)bhed::parseAnimationCfg(text);
            break;
        default:
            (void)bhed::image::decodeTga(rest, n);
            break;
    }
    return 0;
}
