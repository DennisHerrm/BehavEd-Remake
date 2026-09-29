// Baseline-JPEG.
//
// Aufbau der Datei: eine Folge von Abschnitten, jeder mit einer Kennung
// (0xFF, dann ein Byte). Uns interessieren fuenf davon:
//
//   DQT  die Quantisierungstabellen - womit die DCT-Werte geteilt wurden
//   SOF0 Groesse, Anzahl der Kanaele, deren Unterabtastung
//   DHT  die Huffman-Tabellen
//   DRI  alle wieviel Bloecke ein Neustart kommt
//   SOS  ab hier die eigentlichen Bilddaten
//
// Der Weg eines Bildpunkts ist umgekehrt zur Kodierung:
//
//   Huffman auspacken -> 64 Koeffizienten
//   mit der Quantisierungstabelle multiplizieren
//   inverse DCT -> 8x8 Helligkeitswerte
//   Kanaele hochrechnen (Farbe liegt oft in halber Aufloesung)
//   YCbCr -> RGB
#include "bhed/image.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdio>
#include <cstring>

namespace bhed::image {
namespace {

// Die Reihenfolge, in der die 64 Koeffizienten in der Datei stehen: von der
// linken oberen Ecke im Zickzack nach rechts unten. Grobe Strukturen zuerst,
// feine zuletzt - deshalb kann man hinten abschneiden, und genau das macht
// die Kompression.
const int kZigZag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

struct HuffmanTable {
    unsigned char counts[17] = {};   // wie viele Codes je Laenge
    unsigned char symbols[256] = {};
    // Vorgerechnet, damit das Dekodieren nicht Bit fuer Bit suchen muss.
    int minCode[17] = {};
    int maxCode[17] = {};
    int valuePointer[17] = {};
    bool present = false;

    void build() {
        int code = 0;
        int k = 0;
        for (int length = 1; length <= 16; ++length) {
            valuePointer[length] = k;
            minCode[length] = code;
            code += counts[length];
            k += counts[length];
            maxCode[length] = (counts[length] != 0U) ? code - 1 : -1;
            code <<= 1;
        }
        present = true;
    }
};

// Fuer PROGRESSIVE JPEGs.
//
// Ein progressives JPEG schreibt dasselbe Bild in mehreren DURCHGAENGEN:
// erst grob, dann immer feiner. Jeder Durchgang ist ein eigener Scan und
// traegt nur einen Teil bei:
//
//   Spektralauswahl (Ss..Se)  welche der 64 Koeffizienten dieser Scan
//                             behandelt. Ss == 0 ist der Gleichanteil,
//                             der immer allein steht.
//   Naeherung (Ah, Al)        wie viele Bits. Ah == 0 ist der erste
//                             Durchgang fuer diese Koeffizienten, Ah > 0
//                             haengt ein weiteres Bit an ("refinement").
//
// Daraus folgt der Unterschied im Aufbau: bei baseline kann jeder Block
// sofort ruecktransformiert werden, bei progressive erst, wenn ALLE Scans
// gelesen sind. Also muessen die Koeffizienten des ganzen Bildes stehen
// bleiben - das ist "coeffs" unten, 64 Werte je Block.
//
// Die Mod braucht das: 6 der ueber 2400 JPEG-Texturen sind progressiv,
// darunter ausgerechnet die Lava von Mustafar. Die Engine benutzt libjpeg
// und kann es laengst (tr_image_jpg.cpp ruft jpeg_read_header auf); wer es
// nicht kann, dem fehlen genau diese Bilder - und die Meldung "keine
// Textur" fuehrt dabei in die Irre, denn gefunden wurde sie sehr wohl.
struct Component {
    int id = 0;
    int hSampling = 1, vSampling = 1;
    int quantTable = 0;
    int dcTable = 0, acTable = 0;
    int previousDc = 0;
    std::vector<unsigned char> pixels;  // volle Bildgroesse, hochgerechnet
    int blocksPerLine = 0, blocksPerColumn = 0;
    std::vector<unsigned char> raw;     // in eigener Aufloesung
    int rawWidth = 0, rawHeight = 0;

    // Nur progressiv: alle Koeffizienten, 64 je Block, ZICKZACK-geordnet.
    std::vector<short> coeffs;
    int blocksWide = 0, blocksHigh = 0;
};

// Bitweise lesen, mit der JPEG-Eigenheit: ein 0xFF im Datenstrom wird als
// 0xFF 0x00 geschrieben, damit es nicht mit einer Kennung verwechselt wird.
// Beim Lesen muss die Null wieder verschwinden.
class BitReader {
public:
    BitReader(const unsigned char* data, size_t size, size_t position)
        : data_(data), size_(size), pos_(position) {}

    int bit() {
        if (bitCount_ == 0) {
            if (pos_ >= size_) { overrun_ = true; return 0; }
            current_ = data_[pos_++];
            if (current_ == 0xFF) {
                if (pos_ < size_ && data_[pos_] == 0x00) {
                    ++pos_;  // eingeschobene Null
                } else {
                    // Eine echte Kennung - der Abschnitt ist zu Ende.
                    overrun_ = true;
                    return 0;
                }
            }
            bitCount_ = 8;
        }
        --bitCount_;
        return (current_ >> bitCount_) & 1;
    }

    int bits(int count) {
        int value = 0;
        for (int i = 0; i < count; ++i) { value = (value << 1) | bit();
}
        return value;
    }

    void alignAndSkipRestart() {
        // Nach einem Neustart beginnt ein neues Byte, und die Kennung
        // RST0..RST7 wird uebersprungen.
        bitCount_ = 0;
        while (pos_ + 1 < size_) {
            if (data_[pos_] == 0xFF && data_[pos_ + 1] >= 0xD0 &&
                data_[pos_ + 1] <= 0xD7) {
                pos_ += 2;
                return;
            }
            ++pos_;
        }
    }

    bool overrun() const { return overrun_; }
    size_t position() const { return pos_; }

private:
    const unsigned char* data_;
    size_t size_;
    size_t pos_;
    unsigned char current_ = 0;
    int bitCount_ = 0;
    bool overrun_ = false;
};

int decodeHuffman(BitReader& reader, const HuffmanTable& table) {
    int code = 0;
    for (int length = 1; length <= 16; ++length) {
        code = (code << 1) | reader.bit();
        if (reader.overrun()) { return -1;
}
        if (table.maxCode[length] >= 0 && code <= table.maxCode[length]) {
            const int index = table.valuePointer[length] + code - table.minCode[length];
            if (index < 0 || index >= 256) { return -1;
}
            return table.symbols[index];
        }
    }
    return -1;
}

// Die Zahlen stehen in einer eigenen Darstellung: `length` Bits, und der
// obere Halbbereich zaehlt negativ. Ohne diese Umrechnung sind alle Werte um
// die Haelfte daneben.
int extend(int value, int length) {
    if (length == 0) { return 0;
}
    return value < (1 << (length - 1)) ? value - (1 << length) + 1 : value;
}

// Inverse DCT, getrennt in Zeilen und Spalten.
//
// Die einfache Fassung mit Kosinustabelle statt einer schnellen Variante:
// sie ist nachvollziehbar, und der Unterschied faellt beim Laden einer Textur
// nicht auf. Eine schnelle DCT einzubauen, ohne sie pruefen zu koennen, waere
// die schlechtere Wahl.
void inverseDct(const int* input, unsigned char* output, int stride) {
    static float cosTable[8][8];
    static bool ready = false;
    if (!ready) {
        for (int x = 0; x < 8; ++x) {
            for (int u = 0; u < 8; ++u) {
                cosTable[x][u] =
                    std::cos((2.0F * static_cast<float>(x) + 1.0F) *
                             static_cast<float>(u) * std::numbers::pi_v<float> / 16.0F);
            }
        }
        ready = true;
    }
    auto scale = [](int u) { return u == 0 ? 0.70710678F : 1.0F; };

    float temp[64];
    // Erst die Zeilen, dann die Spalten - 8+8 statt 64 Durchlaeufen je Punkt.
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            float sum = 0.0F;
            for (int u = 0; u < 8; ++u) {
                sum += scale(u) * static_cast<float>(input[y * 8 + u]) * cosTable[x][u];
            }
            temp[y * 8 + x] = sum * 0.5F;
        }
    }
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            float sum = 0.0F;
            for (int v = 0; v < 8; ++v) {
                sum += scale(v) * temp[v * 8 + x] * cosTable[y][v];
            }
            // +128, weil beim Kodieren 128 abgezogen wurde.
            const int value = static_cast<int>(std::lround(sum * 0.5F)) + 128;
            output[y * stride + x] =
                static_cast<unsigned char>(value < 0 ? 0 : (value > 255 ? 255 : value));
        }
    }
}

unsigned char clampByte(int v) {
    return static_cast<unsigned char>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

}  // namespace

Image decodeJpeg(const unsigned char* data, size_t size) {
    Image out;
    auto fail = [&](const char* why) {
        out.error = why;
        return out;
    };
    if ((data == nullptr) || size < 4) { return fail("too small for a JPEG");
}
    if (data[0] != 0xFF || data[1] != 0xD8) { return fail("no JPEG start marker");
}

    unsigned short quant[4][64] = {};
    HuffmanTable dcTables[4];
    HuffmanTable acTables[4];
    std::vector<Component> components;
    int width = 0;
    int height = 0;
    int restartInterval = 0;
    // SOF2 statt SOF0/SOF1: das Bild kommt in mehreren Durchgaengen.
    bool progressive = false;
    // Bei progressive wird erst nach dem LETZTEN Scan zusammengesetzt.
    bool anyScan = false;
    size_t pos = 2;

    while (pos + 1 < size) {
        if (data[pos] != 0xFF) { ++pos; continue; }
        const unsigned char marker = data[pos + 1];
        pos += 2;
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;  // ohne Nutzlast
        }
        if (marker == 0xD9) { break;  // Bildende
}
        if (pos + 2 > size) { return fail("truncated segment header");
}
        const size_t length =
            (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
        if (length < 2 || pos + length > size) { return fail("bad segment length");
}
        const unsigned char* segment = data + pos + 2;
        const size_t segmentSize = length - 2;

        switch (marker) {
            case 0xC0:    // SOF0, Baseline
            case 0xC2:    // SOF2, progressiv - gleicher Kopf, anderer Ablauf
            case 0xC1: {  // SOF1, erweitert sequenziell - gleicher Aufbau
                progressive = (marker == 0xC2);
                if (segmentSize < 6) { return fail("short SOF segment");
}
                height = (segment[1] << 8) | segment[2];
                width = (segment[3] << 8) | segment[4];
                const int count = segment[5];
                if (width <= 0 || height <= 0) { return fail("bad image size");
}
                if (width > 16384 || height > 16384) { return fail("implausible size");
}
                if (count < 1 || count > 4) { return fail("unsupported component count");
}
                if (segmentSize < 6 + static_cast<size_t>(count) * 3) {
                    return fail("short component list");
                }
                components.resize(static_cast<size_t>(count));
                for (int i = 0; i < count; ++i) {
                    const unsigned char* entry =
                        segment + 6 + static_cast<std::ptrdiff_t>(i) * 3;
                    components[static_cast<size_t>(i)].id = entry[0];
                    components[static_cast<size_t>(i)].hSampling = entry[1] >> 4;
                    components[static_cast<size_t>(i)].vSampling = entry[1] & 0x0F;
                    components[static_cast<size_t>(i)].quantTable = entry[2] & 3;
                    if (components[static_cast<size_t>(i)].hSampling < 1 ||
                        components[static_cast<size_t>(i)].hSampling > 4 ||
                        components[static_cast<size_t>(i)].vSampling < 1 ||
                        components[static_cast<size_t>(i)].vSampling > 4) {
                        return fail("bad sampling factors");
                    }
                }
                break;
            }
            case 0xC3: case 0xC5: case 0xC6: case 0xC7:
            case 0xC9: case 0xCA: case 0xCB:
            case 0xCD: case 0xCE: case 0xCF:
                return fail("unsupported JPEG encoding");
            case 0xC4: {  // DHT
                size_t at = 0;
                while (at + 17 <= segmentSize) {
                    const int id = segment[at] & 3;
                    const bool isAc = (segment[at] >> 4) != 0;
                    HuffmanTable& table = isAc ? acTables[id] : dcTables[id];
                    int total = 0;
                    for (int i = 1; i <= 16; ++i) {
                        table.counts[i] = segment[at + i];
                        total += table.counts[i];
                    }
                    if (total > 256 || at + 17 + static_cast<size_t>(total) >
                                           segmentSize) {
                        return fail("bad huffman table");
                    }
                    std::memcpy(table.symbols, segment + at + 17,
                                static_cast<size_t>(total));
                    table.build();
                    at += 17 + static_cast<size_t>(total);
                }
                break;
            }
            case 0xDB: {  // DQT
                size_t at = 0;
                while (at < segmentSize) {
                    const int id = segment[at] & 3;
                    const bool sixteenBit = (segment[at] >> 4) != 0;
                    ++at;
                    const size_t need = sixteenBit ? 128U : 64U;
                    if (at + need > segmentSize) { return fail("short quant table");
}
                    for (size_t i = 0; i < 64; ++i) {
                        quant[id][kZigZag[i]] =
                            sixteenBit
                                ? static_cast<unsigned short>((segment[at + i * 2] << 8) |
                                                              segment[at + i * 2 + 1])
                                : segment[at + i];
                    }
                    at += need;
                }
                break;
            }
            case 0xDD:  // DRI
                if (segmentSize >= 2) {
                    restartInterval = (segment[0] << 8) | segment[1];
                }
                break;
            case 0xDA: {  // SOS - ab hier die Bilddaten
                if (components.empty()) { return fail("scan before frame header");
}
                if (segmentSize < 1) { return fail("short scan header");
}
                const int scanCount = segment[0];
                if (segmentSize < 1 + static_cast<size_t>(scanCount) * 2) {
                    return fail("short scan component list");
                }
                for (int i = 0; i < scanCount; ++i) {
                    const int id = segment[1 + i * 2];
                    const int tables = segment[2 + i * 2];
                    for (auto& c : components) {
                        if (c.id == id) {
                            c.dcTable = tables >> 4;
                            c.acTable = tables & 0x0F;
                        }
                    }
                }

                // Groesse in Bloecken. Ein MCU fasst so viele Bloecke, wie die
                // groesste Unterabtastung verlangt.
                int hMax = 1;
                int vMax = 1;
                for (const auto& c : components) {
                    hMax = std::max(hMax, c.hSampling);
                    vMax = std::max(vMax, c.vSampling);
                }
                const int mcuWidth = 8 * hMax;
                const int mcuHeight = 8 * vMax;
                const int mcusPerLine = (width + mcuWidth - 1) / mcuWidth;
                const int mcusPerColumn = (height + mcuHeight - 1) / mcuHeight;

                for (auto& c : components) {
                    c.rawWidth = mcusPerLine * c.hSampling * 8;
                    c.rawHeight = mcusPerColumn * c.vSampling * 8;
                    c.raw.assign(static_cast<size_t>(c.rawWidth) * c.rawHeight, 128);
                    c.previousDc = 0;
                }

                // --- Der progressive Ablauf ---------------------------
                //
                // Er unterscheidet sich grundlegend: statt jeden Block
                // sofort zurueckzurechnen, werden die Koeffizienten
                // eingesammelt und erst nach dem LETZTEN Scan verarbeitet.
                // Vier Scan-Arten, die die Norm (ITU T.81, G.1.2)
                // unterscheidet:
                //
                //   Ss == 0, Ah == 0   Gleichanteil, erster Durchgang
                //   Ss == 0, Ah  > 0   Gleichanteil, ein Bit mehr
                //   Ss  > 0, Ah == 0   Wechselanteile, erster Durchgang
                //   Ss  > 0, Ah  > 0   Wechselanteile verfeinern
                //
                // Die letzte ist die verwickeltste: sie haengt an schon
                // vorhandenen Werten ein Bit an UND traegt neue Werte ein,
                // beides im selben Durchlauf, gesteuert von einem
                // EOB-Zaehler ueber mehrere Bloecke hinweg.
                if (progressive) {
                    if (segmentSize < 4 + static_cast<size_t>(scanCount) * 2) {
                        return fail("short progressive scan header");
                    }
                    const unsigned char* tail =
                        segment + 1 + static_cast<std::ptrdiff_t>(scanCount) * 2;
                    const int ss = tail[0];
                    const int se = tail[1];
                    const int ah = tail[2] >> 4;
                    const int al = tail[2] & 0x0F;
                    if (ss > 63 || se > 63 || ss > se) {
                        return fail("bad spectral selection");
                    }

                    // Beim ERSTEN Scan die Puffer anlegen.
                    if (!anyScan) {
                        anyScan = true;
                        for (auto& c : components) {
                            c.blocksWide = mcusPerLine * c.hSampling;
                            c.blocksHigh = mcusPerColumn * c.vSampling;
                            c.coeffs.assign(
                                static_cast<size_t>(c.blocksWide) *
                                    static_cast<size_t>(c.blocksHigh) * 64U,
                                0);
                            c.rawWidth = c.blocksWide * 8;
                            c.rawHeight = c.blocksHigh * 8;
                            c.raw.assign(
                                static_cast<size_t>(c.rawWidth) * c.rawHeight,
                                128);
                        }
                    }

                    // Die Komponenten DIESES Scans - ein progressiver Scan
                    // enthaelt oft nur eine.
                    std::vector<Component*> scan;
                    for (int i = 0; i < scanCount; ++i) {
                        const int id = segment[1 + i * 2];
                        for (auto& c : components) {
                            if (c.id == id) { scan.push_back(&c); }
                        }
                    }
                    if (scan.empty()) { return fail("scan names no component"); }

                    BitReader br(data, size, pos + length);
                    for (auto* c : scan) { c->previousDc = 0; }
                    int eobRun = 0;

                    // Ein einzelner Block, in allen vier Spielarten.
                    auto doBlock = [&](Component& c, short* co) -> bool {
                        const HuffmanTable& dc = dcTables[c.dcTable & 3];
                        const HuffmanTable& ac = acTables[c.acTable & 3];
                        if (ss == 0) {
                            if (ah == 0) {
                                if (!dc.present) { return false; }
                                const int t = decodeHuffman(br, dc);
                                if (t < 0 || t > 15) { return false; }
                                const int diff =
                                    (t != 0) ? extend(br.bits(t), t) : 0;
                                c.previousDc += diff;
                                // Al ist die Verschiebung: die groben Bits
                                // zuerst, die feinen spaeter.
                                co[0] = static_cast<short>(c.previousDc << al);
                            } else {
                                // Verfeinerung: genau ein Bit anhaengen.
                                if (br.bits(1) != 0) {
                                    co[0] = static_cast<short>(co[0] |
                                                               (1 << al));
                                }
                            }
                            return true;
                        }
                        // --- Wechselanteile ---
                        if (!ac.present) { return false; }
                        if (ah == 0) {
                            // Erster Durchgang. Ein EOB-Lauf ueberspringt
                            // ganze Bloecke.
                            if (eobRun > 0) {
                                --eobRun;
                                return true;
                            }
                            for (int k = ss; k <= se;) {
                                const int rs = decodeHuffman(br, ac);
                                if (rs < 0) { return false; }
                                const int run = rs >> 4;
                                const int sizeBits = rs & 0x0F;
                                if (sizeBits == 0) {
                                    if (run < 15) {
                                        // EOBn: 2^run - 1 weitere Bloecke
                                        // ueberspringen, plus die Bits.
                                        eobRun = (1 << run) - 1;
                                        if (run > 0) {
                                            eobRun += br.bits(run);
                                        }
                                        break;
                                    }
                                    k += 16;   // ZRL
                                    continue;
                                }
                                k += run;
                                if (k > se) { break; }
                                co[k] = static_cast<short>(
                                    extend(br.bits(sizeBits), sizeBits) << al);
                                ++k;
                            }
                            return true;
                        }
                        // Verfeinerung der Wechselanteile.
                        //
                        // Die verwickeltste der vier Spielarten. Waehrend
                        // man ueber die Nullen laeuft, muss an JEDEN schon
                        // vorhandenen Wert unterwegs ein Bit angehaengt
                        // werden; nur die echten Nullen zaehlen fuer den
                        // Lauf. Der Ablauf folgt Schritt fuer Schritt
                        // decode_mcu_AC_refine() aus libjpegs jdphuff.c -
                        // dem Decoder, den die Engine selbst benutzt.
                        //
                        // Ein Unterschied zum ersten Durchgang, der mich
                        // zuerst gekostet hat: dort ist der EOB-Zaehler
                        // (1 << r) - 1, hier ist er (1 << r). Der
                        // Unterschied ist, dass er hier erst am ENDE des
                        // Blocks heruntergezaehlt wird, dort schon beim
                        // Setzen. Um eins daneben, und der ganze Bitstrom
                        // laeuft aus dem Tritt.
                        const int plus = 1 << al;
                        // NICHT "-1 << al": eine negative Zahl zu
                        // verschieben ist in C++ nicht festgelegt.
                        const int minus = -(1 << al);
                        int k = ss;
                        if (eobRun == 0) {
                            for (; k <= se; ++k) {
                                const int rs = decodeHuffman(br, ac);
                                if (rs < 0) { return false; }
                                int run = rs >> 4;
                                int value = rs & 0x0F;
                                if (value != 0) {
                                    // Es kann nur eine 1 sein - mehr Bits
                                    // gibt es in einer Verfeinerung nicht.
                                    value = (br.bits(1) != 0) ? plus : minus;
                                } else if (run != 15) {
                                    eobRun = 1 << run;
                                    if (run > 0) { eobRun += br.bits(run); }
                                    break;
                                }
                                // Ueber "run" Nullen laufen und dabei jeden
                                // vorhandenen Wert verfeinern.
                                while (k <= se) {
                                    short& co_k = co[k];
                                    if (co_k != 0) {
                                        if (br.bits(1) != 0 &&
                                            (co_k & plus) == 0) {
                                            co_k = static_cast<short>(
                                                co_k + (co_k >= 0 ? plus
                                                                  : minus));
                                        }
                                    } else {
                                        if (--run < 0) { break; }
                                    }
                                    ++k;
                                }
                                if (value != 0 && k <= se) {
                                    co[k] = static_cast<short>(value);
                                }
                            }
                        }
                        if (eobRun > 0) {
                            // Im EOB-Lauf: keine neuen Werte, aber die
                            // vorhandenen bekommen trotzdem ihr Bit.
                            for (; k <= se; ++k) {
                                short& co_k = co[k];
                                if (co_k != 0 && br.bits(1) != 0 &&
                                    (co_k & plus) == 0) {
                                    co_k = static_cast<short>(
                                        co_k + (co_k >= 0 ? plus : minus));
                                }
                            }
                            --eobRun;
                        }
                        return true;
                    };

                    // Ein Scan mit EINER Komponente laeuft ueber deren
                    // Bloecke, einer mit mehreren ueber die MCUs. Das steht
                    // so in der Norm und ist keine Wahlmoeglichkeit.
                    bool ok = true;
                    int done = 0;
                    if (scan.size() == 1) {
                        Component& c = *scan[0];
                        // Die Blockzahl eines NICHT verschachtelten Scans.
                        //
                        // Sie richtet sich nach der ECHTEN Groesse dieser
                        // Komponente, nicht nach der MCU-Aufteilung:
                        //
                        //     compW    = aufgerundet(width  * h / hMax)
                        //     blocksW  = aufgerundet(compW / 8)
                        //
                        // Meine erste Fassung hat das in einem Rutsch
                        // gerechnet und dabei zweimal gerundet - heraus kam
                        // eine Zeile zu wenig, und der Bitstrom lief aus
                        // dem Tritt. Die Norm (ITU T.81, A.2.3) trennt die
                        // beiden Schritte, und das aus gutem Grund.
                        const int compW = (width * c.hSampling + hMax - 1) / hMax;
                        const int compH = (height * c.vSampling + vMax - 1) / vMax;
                        const int bw = (compW + 7) / 8;
                        const int bh = (compH + 7) / 8;
                        const int useW = std::max(1, std::min(bw, c.blocksWide));
                        const int useH = std::max(1, std::min(bh, c.blocksHigh));
                        for (int by = 0; by < useH && ok; ++by) {
                            for (int bx = 0; bx < useW; ++bx) {
                                if (restartInterval > 0 && done > 0 &&
                                    done % restartInterval == 0) {
                                    br.alignAndSkipRestart();
                                    c.previousDc = 0;
                                    eobRun = 0;
                                }
                                ++done;
                                short* co =
                                    c.coeffs.data() +
                                    (static_cast<size_t>(by) * c.blocksWide +
                                     bx) * 64U;
                                if (!doBlock(c, co)) {
                                    ok = br.overrun();
                                    break;
                                }
                            }
                        }
                    } else {
                        for (int mcuY = 0; mcuY < mcusPerColumn && ok; ++mcuY) {
                            for (int mcuX = 0; mcuX < mcusPerLine && ok; ++mcuX) {
                                if (restartInterval > 0 && done > 0 &&
                                    done % restartInterval == 0) {
                                    br.alignAndSkipRestart();
                                    for (auto* c : scan) { c->previousDc = 0; }
                                    eobRun = 0;
                                }
                                ++done;
                                for (auto* c : scan) {
                                    for (int by = 0; by < c->vSampling; ++by) {
                                        for (int bx = 0; bx < c->hSampling; ++bx) {
                                            const int gx =
                                                mcuX * c->hSampling + bx;
                                            const int gy =
                                                mcuY * c->vSampling + by;
                                            short* co =
                                                c->coeffs.data() +
                                                (static_cast<size_t>(gy) *
                                                     c->blocksWide + gx) * 64U;
                                            if (!doBlock(*c, co)) {
                                                ok = br.overrun();
                                                by = c->vSampling;
                                                bx = c->hSampling;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (!ok) { return fail("bad progressive scan"); }

                    // Bis zur naechsten Kennung vorspulen.
                    pos = br.position();
                    while (pos + 1 < size &&
                           !(data[pos] == 0xFF && data[pos + 1] != 0x00 &&
                             !(data[pos + 1] >= 0xD0 && data[pos + 1] <= 0xD7))) {
                        ++pos;
                    }
                    // Kommt noch ein Scan? Dann weiter sammeln. Erst am
                    // Dateiende stehen alle Koeffizienten fest, und vorher
                    // waere jede Ruecktransformation zu frueh.
                    const bool nochScans =
                        pos + 1 < size && data[pos + 1] != 0xD9;
                    if (nochScans) {
                        continue;
                    }
                }

                // Der BASELINE-Weg. Bei progressive ist oben schon alles
                // eingesammelt, und es geht direkt ans Zusammensetzen.
                if (!progressive) {
                BitReader reader(data, size, pos + length);
                int block[64];
                unsigned char samples[64];
                int mcuCount = 0;

                for (int mcuY = 0; mcuY < mcusPerColumn; ++mcuY) {
                    for (int mcuX = 0; mcuX < mcusPerLine; ++mcuX) {
                        if (restartInterval > 0 && mcuCount > 0 &&
                            mcuCount % restartInterval == 0) {
                            reader.alignAndSkipRestart();
                            for (auto& c : components) { c.previousDc = 0;
}
                        }
                        ++mcuCount;

                        for (auto& c : components) {
                            for (int by = 0; by < c.vSampling; ++by) {
                                for (int bx = 0; bx < c.hSampling; ++bx) {
                                    std::memset(block, 0, sizeof(block));

                                    const HuffmanTable& dc = dcTables[c.dcTable & 3];
                                    const HuffmanTable& ac = acTables[c.acTable & 3];
                                    if (!dc.present || !ac.present) {
                                        return fail("scan uses a missing huffman table");
                                    }

                                    // Der Gleichanteil steht als Differenz zum
                                    // vorigen Block - deshalb previousDc.
                                    const int t = decodeHuffman(reader, dc);
                                    if (t < 0 || t > 15) {
                                        if (reader.overrun()) { goto scanDone;
}
                                        return fail("bad DC code");
                                    }
                                    const int diff = (t != 0) ? extend(reader.bits(t), t) : 0;
                                    c.previousDc += diff;
                                    block[0] = c.previousDc *
                                               quant[c.quantTable & 3][0];

                                    // Die Wechselanteile: je Eintrag erst die
                                    // Zahl der uebersprungenen Nullen, dann der
                                    // Wert.
                                    for (int k = 1; k < 64;) {
                                        const int rs = decodeHuffman(reader, ac);
                                        if (rs < 0) {
                                            if (reader.overrun()) { goto scanDone;
}
                                            return fail("bad AC code");
                                        }
                                        const int run = rs >> 4;
                                        const int sizeBits = rs & 0x0F;
                                        if (sizeBits == 0) {
                                            if (run != 15) { break;  // Blockende
}
                                            k += 16;
                                            continue;
                                        }
                                        k += run;
                                        if (k > 63) { break;
}
                                        const int value =
                                            extend(reader.bits(sizeBits), sizeBits);
                                        block[kZigZag[k]] =
                                            value * quant[c.quantTable & 3][kZigZag[k]];
                                        ++k;
                                    }

                                    inverseDct(block, samples, 8);

                                    const int originX =
                                        (mcuX * c.hSampling + bx) * 8;
                                    const int originY =
                                        (mcuY * c.vSampling + by) * 8;
                                    for (int y = 0; y < 8; ++y) {
                                        const int targetY = originY + y;
                                        if (targetY >= c.rawHeight) { break;
}
                                        for (int x = 0; x < 8; ++x) {
                                            const int targetX = originX + x;
                                            if (targetX >= c.rawWidth) { break;
}
                                            c.raw[static_cast<size_t>(targetY) *
                                                      c.rawWidth + targetX] =
                                                samples[y * 8 + x];
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            scanDone:
                ;
                }   // Ende des Baseline-Weges

                // --- Progressiv: JETZT ruecktransformieren ------------
                //
                // Erst hier stehen alle Koeffizienten fest. Der Ablauf ist
                // derselbe wie bei baseline, nur eben nachgelagert:
                // entquantisieren, Zickzack aufloesen, IDCT, ablegen.
                if (progressive) {
                    int blk[64];
                    unsigned char smp[64];
                    for (auto& c : components) {
                        for (int by = 0; by < c.blocksHigh; ++by) {
                            for (int bx = 0; bx < c.blocksWide; ++bx) {
                                const short* co =
                                    c.coeffs.data() +
                                    (static_cast<size_t>(by) * c.blocksWide +
                                     bx) * 64U;
                                std::memset(blk, 0, sizeof(blk));
                                for (int k = 0; k < 64; ++k) {
                                    blk[kZigZag[k]] =
                                        co[k] * quant[c.quantTable & 3]
                                                     [kZigZag[k]];
                                }
                                inverseDct(blk, smp, 8);
                                for (int y = 0; y < 8; ++y) {
                                    const int ty = by * 8 + y;
                                    if (ty >= c.rawHeight) { break; }
                                    for (int x = 0; x < 8; ++x) {
                                        const int tx = bx * 8 + x;
                                        if (tx >= c.rawWidth) { break; }
                                        c.raw[static_cast<size_t>(ty) *
                                                  c.rawWidth + tx] =
                                            smp[y * 8 + x];
                                    }
                                }
                            }
                        }
                    }
                }

                // Zusammensetzen. Farbkanaele liegen oft in halber Aufloesung
                // und werden hochgerechnet - schlicht durch Wiederholen, wie
                // es die meisten Decoder tun.
                out.rgba.assign(static_cast<size_t>(width) * height * 4, 255);
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        auto sampleOf = [&](const Component& c) {
                            const int sx = x * c.hSampling / hMax;
                            const int sy = y * c.vSampling / vMax;
                            const int cx = sx < c.rawWidth ? sx : c.rawWidth - 1;
                            const int cy = sy < c.rawHeight ? sy : c.rawHeight - 1;
                            return static_cast<int>(
                                c.raw[static_cast<size_t>(cy) * c.rawWidth + cx]);
                        };
                        const size_t at = (static_cast<size_t>(y) * width + x) * 4;

                        if (components.size() >= 3) {
                            // Als float, nicht als int: die Umrechnung
                            // multipliziert gleich darauf mit
                            // Kommazahlen, und eine stillschweigende
                            // Umwandlung mitten in der Formel ist genau
                            // das, wovor die Regel warnt.
                            const auto Y = static_cast<float>(sampleOf(components[0]));
                            const auto cb =
                                static_cast<float>(sampleOf(components[1]) - 128);
                            const auto cr =
                                static_cast<float>(sampleOf(components[2]) - 128);
                            // JFIF-Umrechnung. Die Beiwerte stehen in der Norm.
                            out.rgba[at + 0] =
                                clampByte(static_cast<int>(std::lround(
                                    Y + 1.402F * cr)));
                            out.rgba[at + 1] = clampByte(static_cast<int>(std::lround(
                                Y - 0.344136F * cb - 0.714136F * cr)));
                            out.rgba[at + 2] =
                                clampByte(static_cast<int>(std::lround(
                                    Y + 1.772F * cb)));
                        } else {
                            const int grey = sampleOf(components[0]);
                            out.rgba[at + 0] = clampByte(grey);
                            out.rgba[at + 1] = clampByte(grey);
                            out.rgba[at + 2] = clampByte(grey);
                        }
                        out.rgba[at + 3] = 255;
                    }
                }
                out.width = width;
                out.height = height;
                out.hasAlpha = false;  // JPEG kennt keinen Alphakanal
                out.ok = true;
                return out;
            }
            default:
                break;  // APPn, COM und anderes ueberspringen
        }
        pos += length;
    }
    return fail("no image data found");
}

}  // namespace bhed::image
