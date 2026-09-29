// PNG lesen.
//
// Aufbau: eine Kennung, dann eine Folge von Bloecken (Laenge, Art, Daten,
// Pruefsumme). Uns interessieren drei:
//
//   IHDR  Groesse, Bittiefe, Farbart
//   PLTE  Farbtabelle, wenn die Farbart eine verlangt
//   IDAT  die Bilddaten, zlib-verpackt - moeglicherweise auf mehrere
//         Bloecke verteilt, die zusammengehaengt werden muessen
//
// Nach dem Auspacken beginnt jede Zeile mit einem Filterbyte. Die fuenf
// Filter sagen, wie der Wert aus den Nachbarn vorherzusagen war - das ist der
// eigentliche Trick am Format, und die Stelle, an der man es falsch macht.
#include "bhed/image.h"

#include <cstring>

#include "bhed/inflate.h"

namespace bhed::image {
namespace {

uint32_t readU32Big(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// Der Paeth-Vorhersager. Nimmt den Nachbarn, der der Summe am naechsten liegt.
int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) { return a;
}
    return pb <= pc ? b : c;
}

}  // namespace

Image decodePng(const unsigned char* data, size_t size) {
    Image out;
    auto fail = [&](const char* why) {
        out.error = why;
        return out;
    };

    static const unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A,
                                                0x1A, 0x0A};
    if ((data == nullptr) || size < 8 || std::memcmp(data, kSignature, 8) != 0) {
        return fail("not a PNG file");
    }

    int width = 0;
    int height = 0;
    int bitDepth = 0;
    int colourType = 0;
    std::vector<unsigned char> palette;
    std::vector<unsigned char> alphaTable;
    std::vector<unsigned char> packed;
    size_t pos = 8;

    while (pos + 8 <= size) {
        const uint32_t length = readU32Big(data + pos);
        const char* type = reinterpret_cast<const char*>(data + pos + 4);
        const size_t body = pos + 8;
        if (body + length + 4 > size) { break;  // abgeschnitten
}

        if (std::memcmp(type, "IHDR", 4) == 0 && length >= 13) {
            width = static_cast<int>(readU32Big(data + body));
            height = static_cast<int>(readU32Big(data + body + 4));
            bitDepth = data[body + 8];
            colourType = data[body + 9];
            const int interlace = data[body + 12];
            if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
                return fail("implausible PNG size");
            }
            if (interlace != 0) {
                // Adam7 braucht einen ganz anderen Weg und kommt in
                // Spieldaten nicht vor.
                return fail("interlaced PNG is not supported");
            }
            if (bitDepth != 8 && bitDepth != 16) {
                return fail("only 8 and 16 bit PNGs are supported");
            }
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.assign(data + body, data + body + length);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            alphaTable.assign(data + body, data + body + length);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            // Die Bilddaten koennen auf mehrere Bloecke verteilt sein. Wer nur
            // den ersten nimmt, bekommt bei groesseren Bildern das obere
            // Drittel und darunter Muell.
            packed.insert(packed.end(), data + body, data + body + length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos = body + length + 4;  // + Pruefsumme
    }

    if (width == 0 || packed.empty()) { return fail("no image data in the PNG");
}

    int channels = 0;
    switch (colourType) {
        case 0: channels = 1; break;  // grau
        case 2: channels = 3; break;  // rgb
        case 3: channels = 1; break;  // Farbtabelle
        case 4: channels = 2; break;  // grau + alpha
        case 6: channels = 4; break;  // rgba
        default: return fail("unsupported PNG colour type");
    }
    if (colourType == 3 && palette.empty()) { return fail("paletted PNG without PLTE");
}

    const int bytesPerSample = bitDepth / 8;
    const size_t bytesPerPixel =
        static_cast<size_t>(channels) * static_cast<size_t>(bytesPerSample);
    const size_t stride = static_cast<size_t>(width) * bytesPerPixel;
    const size_t expected = (stride + 1) * static_cast<size_t>(height);

    // Die Daten liegen mit zlib-Kopf vor: zwei Byte davor, vier Byte Adler
    // dahinter. Unser Auspacker will rohes Deflate.
    if (packed.size() < 6) { return fail("PNG data too short");
}
    auto unpacked = inflate::raw(packed.data() + 2, packed.size() - 2, expected);
    if (!unpacked.ok) { return fail("PNG inflate failed");
}
    if (unpacked.data.size() < expected) { return fail("PNG data is incomplete");
}

    // Die Zeilenfilter rueckgaengig machen. Jede Zeile beginnt mit einem
    // Filterbyte; die Vorhersage nutzt den linken Nachbarn, die Zeile darueber
    // und deren linken Nachbarn.
    std::vector<unsigned char> pixels(stride * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        const size_t rowStart = static_cast<size_t>(y) * (stride + 1);
        const int filter = unpacked.data[rowStart];
        const unsigned char* source = unpacked.data.data() + rowStart + 1;
        unsigned char* target = pixels.data() + static_cast<size_t>(y) * stride;
        const unsigned char* above =
            y > 0 ? pixels.data() + static_cast<size_t>(y - 1) * stride : nullptr;

        for (size_t x = 0; x < stride; ++x) {
            const int left = x >= bytesPerPixel ? target[x - bytesPerPixel] : 0;
            const int up = (above != nullptr) ? above[x] : 0;
            const int upLeft =
                ((above != nullptr) && x >= bytesPerPixel) ? above[x - bytesPerPixel] : 0;
            int value = source[x];
            switch (filter) {
                case 0: break;                                   // ohne
                case 1: value += left; break;                    // links
                case 2: value += up; break;                      // oben
                case 3: value += (left + up) / 2; break;         // Mittel
                case 4: value += paeth(left, up, upLeft); break; // Paeth
                default: return fail("unknown PNG row filter");
            }
            target[x] = static_cast<unsigned char>(value & 0xFF);
        }
    }

    out.rgba.assign(static_cast<size_t>(width) * height * 4, 255);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char* p =
                pixels.data() + static_cast<size_t>(y) * stride + x * bytesPerPixel;
            // Bei 16 Bit nur das obere Byte nehmen - die Vorschau zeigt acht.
            auto sample = [&](int index) {
                return p[static_cast<size_t>(index) * bytesPerSample];
            };
            const size_t at = (static_cast<size_t>(y) * width + x) * 4;

            switch (colourType) {
                case 0:
                    out.rgba[at] = out.rgba[at + 1] = out.rgba[at + 2] = sample(0);
                    break;
                case 2:
                    out.rgba[at] = sample(0);
                    out.rgba[at + 1] = sample(1);
                    out.rgba[at + 2] = sample(2);
                    break;
                case 3: {
                    const size_t entry = static_cast<size_t>(sample(0)) * 3;
                    if (entry + 2 < palette.size()) {
                        out.rgba[at] = palette[entry];
                        out.rgba[at + 1] = palette[entry + 1];
                        out.rgba[at + 2] = palette[entry + 2];
                    }
                    const size_t index = sample(0);
                    if (index < alphaTable.size()) {
                        out.rgba[at + 3] = alphaTable[index];
                        out.hasAlpha = true;
                    }
                    break;
                }
                case 4:
                    out.rgba[at] = out.rgba[at + 1] = out.rgba[at + 2] = sample(0);
                    out.rgba[at + 3] = sample(1);
                    out.hasAlpha = true;
                    break;
                case 6:
                    out.rgba[at] = sample(0);
                    out.rgba[at + 1] = sample(1);
                    out.rgba[at + 2] = sample(2);
                    out.rgba[at + 3] = sample(3);
                    out.hasAlpha = true;
                    break;
                default: break;
            }
        }
    }

    out.width = width;
    out.height = height;
    out.ok = true;
    return out;
}

std::vector<unsigned char> encodeTga(const unsigned char* rgba, int width,
                                     int height) {
    std::vector<unsigned char> out;
    if ((rgba == nullptr) || width <= 0 || height <= 0) { return out;
}

    out.resize(18, 0);
    out[2] = 2;   // unkomprimiert, Echtfarbe
    out[12] = static_cast<unsigned char>(width & 0xFF);
    out[13] = static_cast<unsigned char>((width >> 8) & 0xFF);
    out[14] = static_cast<unsigned char>(height & 0xFF);
    out[15] = static_cast<unsigned char>((height >> 8) & 0xFF);
    out[16] = 32;    // 32 Bit
    out[17] = 0x28;  // von oben nach unten, acht Alphabits

    out.reserve(out.size() + static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        // Targa legt Farben als BGR ab.
        out.push_back(rgba[i * 4 + 2]);
        out.push_back(rgba[i * 4 + 1]);
        out.push_back(rgba[i * 4 + 0]);
        out.push_back(rgba[i * 4 + 3]);
    }
    return out;
}

}  // namespace bhed::image
