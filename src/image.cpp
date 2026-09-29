#include "bhed/image.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cctype>
#include <cstring>

namespace bhed::image {

void shrinkTo(const Image& im, int maxSize, int& outW, int& outH,
              std::vector<unsigned char>& outRgba) {
    const int scale = std::max(1, std::max(im.width, im.height) / maxSize);
    outW = std::max(1, im.width / scale);
    outH = std::max(1, im.height / scale);
    outRgba.resize(static_cast<std::size_t>(outW) *
                   static_cast<std::size_t>(outH) * 4U);
    // --- Ueber den ganzen Block MITTELN, nicht einen Punkt herausgreifen --
    //
    // Gebeten: "mach Mipmaps und shrinkTo gleich auch richtig."
    //
    // Vorher wurde je Zielpunkt EIN Quellpunkt genommen: bei scale 2 also
    // einer von vieren, bei scale 4 einer von sechzehn. Der Rest fiel
    // ersatzlos weg. Eine 1024er Textur, die auf 512 gebracht wird,
    // verliert so drei Viertel ihrer Bildpunkte - und mit ihnen jedes
    // feine Muster, das dann als Flimmern zurueckkommt.
    //
    // Jetzt der Mittelwert ueber den ganzen Block. Das ist genau das, was
    // die Engine beim Bauen der Mipmaps tut (R_MipMap in tr_image.cpp:
    // je vier Nachbarn zu einem).
    //
    // Der Alphakanal wird MITgemittelt. Das ist bei alphagetesteten
    // Texturen (Gitter, Fluegel) wichtig: nimmt man dort einen einzelnen
    // Punkt, faellt je nach Zufall die halbe Struktur weg.
    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            unsigned sum[4] = {0, 0, 0, 0};
            unsigned n = 0;
            for (int sy = 0; sy < scale; ++sy) {
                const int py = y * scale + sy;
                if (py >= im.height) {
                    break;
                }
                for (int sx = 0; sx < scale; ++sx) {
                    const int px = x * scale + sx;
                    if (px >= im.width) {
                        break;
                    }
                    const std::size_t src =
                        (static_cast<std::size_t>(py) *
                             static_cast<std::size_t>(im.width) +
                         static_cast<std::size_t>(px)) * 4U;
                    for (int k = 0; k < 4; ++k) {
                        sum[k] += im.rgba[src + static_cast<std::size_t>(k)];
                    }
                    ++n;
                }
            }
            const std::size_t dst =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(outW) +
                 static_cast<std::size_t>(x)) * 4U;
            if (n == 0) {
                continue;   // kann nur am Rand bei entarteten Groessen sein
            }
            for (int k = 0; k < 4; ++k) {
                outRgba[dst + static_cast<std::size_t>(k)] =
                    static_cast<unsigned char>(sum[k] / n);
            }
        }
    }
}

std::string mappingName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        if (c == '.') {
            break;
        }
        out.push_back((c == '\\')
                          ? '/'
                          : static_cast<char>(std::tolower(
                                static_cast<unsigned char>(c))));
    }
    return out;
}
namespace {

// Eine Obergrenze fuer die Bildgroesse. Eine Textur mit 30000x30000 waere
// 3.6 GB - kein JKA-Material sieht so aus, und ein Kopf, der es behauptet,
// ist entweder kaputt oder boesartig.
constexpr int kMaxDimension = 16384;

bool tooBig(int width, int height) {
    if (width <= 0 || height <= 0) { return true;
}
    if (width > kMaxDimension || height > kMaxDimension) { return true;
}
    // 4 Byte je Bildpunkt, mit Reserve gegen Ueberlauf gerechnet.
    return static_cast<int64_t>(width) * height > 64LL * 1024 * 1024;
}

}  // namespace

Format sniff(const unsigned char* data, size_t size) {
    if ((data == nullptr) || size < 4) { return Format::Unknown;
}

    // PNG: 89 50 4E 47 0D 0A 1A 0A
    if (size >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
        data[3] == 'G') {
        return Format::Png;
    }
    // JPEG: FF D8 FF
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) { return Format::Jpeg;
}

    // Targa hat keine Kennung am Anfang - das ist die Schwaeche des Formats.
    // Neuere Dateien tragen eine Fusszeile mit "TRUEVISION-XFILE"; sonst
    // bleibt nur, den Kopf auf Plausibilitaet zu pruefen.
    if (size >= 26) {
        const char* footer = reinterpret_cast<const char*>(data + size - 18);
        if (std::memcmp(footer, "TRUEVISION-XFILE", 16) == 0) { return Format::Tga;
}
    }
    if (size >= 18) {
        const unsigned char imageType = data[2];
        const unsigned char depth = data[16];
        const bool knownType = imageType == 1 || imageType == 2 || imageType == 3 ||
                               imageType == 9 || imageType == 10 || imageType == 11;
        const bool knownDepth = depth == 8 || depth == 15 || depth == 16 ||
                                depth == 24 || depth == 32;
        if (knownType && knownDepth) { return Format::Tga;
}
    }
    return Format::Unknown;
}

const char* formatName(Format format) {
    switch (format) {
        case Format::Tga: return "TGA";
        case Format::Png: return "PNG";
        case Format::Jpeg: return "JPEG";
        default: return "unknown";
    }
}

Image decodeTga(const unsigned char* data, size_t size) {
    Image out;
    auto fail = [&](const char* why) {
        out.error = why;
        return out;
    };
    if ((data == nullptr) || size < 18) { return fail("too small for a TGA header");
}

    const unsigned idLength = data[0];
    const unsigned colourMapType = data[1];
    const unsigned imageType = data[2];
    const unsigned colourMapLength =
        static_cast<unsigned>(data[5]) | (static_cast<unsigned>(data[6]) << 8);
    const unsigned colourMapDepth = data[7];
    const int width = static_cast<int>(data[12]) | (static_cast<int>(data[13]) << 8);
    const int height = static_cast<int>(data[14]) | (static_cast<int>(data[15]) << 8);
    const unsigned depth = data[16];
    const unsigned descriptor = data[17];

    if (tooBig(width, height)) { return fail("implausible image size");
}

    const bool rleCompressed = imageType == 9 || imageType == 10 || imageType == 11;
    const bool paletted = imageType == 1 || imageType == 9;
    const bool greyscale = imageType == 3 || imageType == 11;

    if (imageType != 1 && imageType != 2 && imageType != 3 && imageType != 9 &&
        imageType != 10 && imageType != 11) {
        return fail("unsupported TGA image type");
    }
    if (depth != 8 && depth != 15 && depth != 16 && depth != 24 && depth != 32) {
        return fail("unsupported TGA bit depth");
    }

    size_t offset = 18 + idLength;
    if (offset > size) { return fail("truncated after the header");
}

    // Farbtabelle, wenn vorhanden.
    std::vector<unsigned char> palette;
    if (colourMapType == 1) {
        const unsigned entryBytes = (colourMapDepth + 7) / 8;
        const size_t tableBytes = static_cast<size_t>(colourMapLength) * entryBytes;
        if (offset + tableBytes > size) { return fail("truncated colour map");
}
        palette.assign(data + offset, data + offset + tableBytes);
        offset += tableBytes;
    } else if (paletted) {
        return fail("paletted image without a colour map");
    }

    const unsigned bytesPerPixel = (depth + 7) / 8;
    const size_t pixelCount = static_cast<size_t>(width) * height;
    out.rgba.assign(pixelCount * 4, 0);

    // Ein Bildpunkt in RGBA umsetzen. Targa legt Farben als BGR ab - das ist
    // der Klassiker, an dem umgesetzte Bilder blau statt rot werden.
    auto writePixel = [&](size_t index, const unsigned char* pixel) {
        unsigned char r = 0;
        unsigned char g = 0;
        unsigned char b = 0;
        unsigned char a = 255;
        if (paletted) {
            const unsigned entryBytes = (colourMapDepth + 7) / 8;
            const size_t entry = static_cast<size_t>(pixel[0]) * entryBytes;
            if (entry + entryBytes <= palette.size()) {
                b = palette[entry];
                g = entryBytes > 1 ? palette[entry + 1] : b;
                r = entryBytes > 2 ? palette[entry + 2] : b;
                if (entryBytes > 3) { a = palette[entry + 3];
}
            }
        } else if (greyscale) {
            r = g = b = pixel[0];
        } else if (depth == 15 || depth == 16) {
            const unsigned value =
                static_cast<unsigned>(pixel[0]) | (static_cast<unsigned>(pixel[1]) << 8);
            // Fuenf Bit je Kanal auf acht strecken: nicht mal 8, sondern die
            // oberen Bits nach unten wiederholen. Sonst erreicht Weiss nur 248.
            const unsigned r5 = (value >> 10) & 0x1F;
            const unsigned g5 = (value >> 5) & 0x1F;
            const unsigned b5 = value & 0x1F;
            r = static_cast<unsigned char>((r5 << 3) | (r5 >> 2));
            g = static_cast<unsigned char>((g5 << 3) | (g5 >> 2));
            b = static_cast<unsigned char>((b5 << 3) | (b5 >> 2));
            if (depth == 16) { a = ((value & 0x8000) != 0U) ? 255 : 0;
}
        } else {
            b = pixel[0];
            g = pixel[1];
            r = pixel[2];
            if (depth == 32) { a = pixel[3];
}
        }
        out.rgba[index * 4 + 0] = r;
        out.rgba[index * 4 + 1] = g;
        out.rgba[index * 4 + 2] = b;
        out.rgba[index * 4 + 3] = a;
    };

    // Zeilenrichtung. Bit 5 des Deskriptors: gesetzt heisst oben beginnend,
    // sonst unten - und unten ist die Voreinstellung. Wer das uebersieht,
    // bekommt jedes zweite Bild auf dem Kopf.
    const bool topDown = (descriptor & 0x20) != 0;

    std::vector<unsigned char> pixel(bytesPerPixel);
    size_t written = 0;
    while (written < pixelCount) {
        if (rleCompressed) {
            if (offset >= size) { return fail("truncated RLE data");
}
            const unsigned packet = data[offset++];
            const size_t count = (packet & 0x7F) + 1U;
            if (written + count > pixelCount) { return fail("RLE packet overruns");
}

            if ((packet & 0x80) != 0U) {
                // Ein Bildpunkt, count-mal wiederholt.
                if (offset + bytesPerPixel > size) { return fail("truncated RLE run");
}
                std::memcpy(pixel.data(), data + offset, bytesPerPixel);
                offset += bytesPerPixel;
                for (size_t i = 0; i < count; ++i) {
                    writePixel(written + i, pixel.data());
                }
            } else {
                if (offset + count * bytesPerPixel > size) {
                    return fail("truncated RLE literals");
                }
                for (size_t i = 0; i < count; ++i) {
                    writePixel(written + i, data + offset + i * bytesPerPixel);
                }
                offset += count * bytesPerPixel;
            }
            written += count;
        } else {
            if (offset + bytesPerPixel > size) { return fail("truncated pixel data");
}
            writePixel(written, data + offset);
            offset += bytesPerPixel;
            ++written;
        }
    }

    // Falls die Datei von unten nach oben liegt, Zeilen tauschen.
    if (!topDown && height > 1) {
        const size_t rowBytes = static_cast<size_t>(width) * 4;
        std::vector<unsigned char> row(rowBytes);
        for (int y = 0; y < height / 2; ++y) {
            unsigned char* upper = out.rgba.data() + static_cast<size_t>(y) * rowBytes;
            unsigned char* lower =
                out.rgba.data() + static_cast<size_t>(height - 1 - y) * rowBytes;
            std::memcpy(row.data(), upper, rowBytes);
            std::memcpy(upper, lower, rowBytes);
            std::memcpy(lower, row.data(), rowBytes);
        }
    }

    out.width = width;
    out.height = height;
    out.hasAlpha = depth == 32 || (depth == 16 && !greyscale);
    out.ok = true;
    return out;
}

Image decode(const unsigned char* data, size_t size) {
    Image out;
    switch (sniff(data, size)) {
        case Format::Tga:
            return decodeTga(data, size);
        case Format::Png:
            return decodePng(data, size);
        case Format::Jpeg:
            return decodeJpeg(data, size);
        default:
            out.error = "unrecognised image format";
            return out;
    }
}

}  // namespace bhed::image

