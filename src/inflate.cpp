#include "bhed/inflate.h"

#include <cstring>

namespace bhed::inflate {
namespace {

// Bitweise lesen, immer mit Grenzpruefung. Laeuft der Puffer aus, liefert
// jeder weitere Aufruf 0 und setzt das Fehlerzeichen - damit kann die
// Schleife oben aufhoeren, statt in den Speicher daneben zu greifen.
class BitReader {
public:
    BitReader(const unsigned char* data, size_t size) : data_(data), size_(size) {}

    bool overrun() const { return overrun_; }

    unsigned bit() {
        if (bytePos_ >= size_) {
            overrun_ = true;
            return 0;
        }
        const unsigned value = (data_[bytePos_] >> bitPos_) & 1U;
        if (++bitPos_ == 8) {
            bitPos_ = 0;
            ++bytePos_;
        }
        return value;
    }

    unsigned bits(int count) {
        unsigned value = 0;
        for (int i = 0; i < count; ++i) { value |= bit() << i;
}
        return value;
    }

    void alignToByte() {
        if (bitPos_ != 0) {
            bitPos_ = 0;
            ++bytePos_;
        }
    }

    bool copyRaw(size_t count, std::vector<unsigned char>& out, size_t limit) {
        if (bytePos_ + count > size_) { return false;
}
        if (out.size() + count > limit) { return false;
}
        out.insert(out.end(), data_ + bytePos_, data_ + bytePos_ + count);
        bytePos_ += count;
        return true;
    }

    size_t bytePosition() const { return bytePos_; }

private:
    const unsigned char* data_;
    size_t size_;
    size_t bytePos_ = 0;
    int bitPos_ = 0;
    bool overrun_ = false;
};

// Ein Huffman-Baum als kanonische Tabelle. Deflate legt die Codes nicht
// explizit ab, sondern nur ihre Laengen - daraus ergeben sich die Codes
// eindeutig, wenn man sie in der Reihenfolge der Laengen vergibt.
struct Huffman {
    // counts[l] = wie viele Codes die Laenge l haben
    // symbols   = die Symbole, nach Codelaenge sortiert
    int counts[16] = {};
    std::vector<int> symbols;

    bool build(const unsigned char* lengths, int count) {
        std::memset(counts, 0, sizeof(counts));
        for (int i = 0; i < count; ++i) {
            if (lengths[i] > 15) { return false;
}
            ++counts[lengths[i]];
        }
        // Laenge 0 heisst "kommt nicht vor".
        counts[0] = 0;

        // Ueberbelegte Tabelle abweisen: mehr Codes einer Laenge, als es bei
        // dieser Laenge geben kann. Ohne die Pruefung baut ein verbogenes
        // Archiv einen Baum, in dem man sich verlaufen kann.
        int left = 1;
        for (int length = 1; length <= 15; ++length) {
            left <<= 1;
            left -= counts[length];
            if (left < 0) { return false;
}
        }

        int offsets[16] = {};
        for (int length = 1; length < 15; ++length) {
            offsets[length + 1] = offsets[length] + counts[length];
        }
        symbols.assign(count, 0);
        for (int i = 0; i < count; ++i) {
            if (lengths[i] != 0) { symbols[offsets[lengths[i]]++] = i;
}
        }
        return true;
    }

    // Liest ein Symbol. -1 heisst: ungueltiger Code.
    int decode(BitReader& reader) const {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length <= 15; ++length) {
            code |= static_cast<int>(reader.bit());
            if (reader.overrun()) { return -1;
}
            const int count = counts[length];
            if (code - first < count) {
                // Erst erweitern, dann addieren - sonst rechnet die Summe
                // in int und der Ueberlauf waere schon geschehen.
                return symbols[static_cast<size_t>(index) +
                               static_cast<size_t>(code - first)];
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -1;
    }
};

// Die festen Tabellen aus RFC 1951, Abschnitt 3.2.6.
void buildFixed(Huffman& literals, Huffman& distances) {
    unsigned char lengths[288];
    for (int i = 0; i < 144; ++i) { lengths[i] = 8;
}
    for (int i = 144; i < 256; ++i) { lengths[i] = 9;
}
    for (int i = 256; i < 280; ++i) { lengths[i] = 7;
}
    for (int i = 280; i < 288; ++i) { lengths[i] = 8;
}
    literals.build(lengths, 288);

    unsigned char distanceLengths[30];
    for (int i = 0; i < 30; ++i) { distanceLengths[i] = 5;
}
    distances.build(distanceLengths, 30);
}

const int kLengthBase[] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                           15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                           67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLengthExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                            2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistanceBase[] = {1,    2,    3,    4,    5,    7,     9,    13,
                             17,   25,   33,   49,   65,   97,    129,  193,
                             257,  385,  513,  769,  1025, 1537,  2049, 3073,
                             4097, 6145, 8193, 12289, 16385, 24577};
const int kDistanceExtra[] = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5,  6,
                              6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

}  // namespace

Result raw(const unsigned char* data, size_t size, size_t expectedSize) {
    Result result;
    if ((data == nullptr) || size == 0) {
        result.error = "empty input";
        return result;
    }
    // Obergrenze mit etwas Luft: ein Archiv, das eine unsinnige Groesse
    // behauptet, soll keinen unsinnigen Speicher bekommen.
    const size_t limit = expectedSize > 0 ? expectedSize : (64U << 20);
    if (limit > (256U << 20)) {
        result.error = "declared size is implausible";
        return result;
    }
    result.data.reserve(limit);

    BitReader reader(data, size);
    Huffman fixedLiterals;
    Huffman fixedDistances;
    buildFixed(fixedLiterals, fixedDistances);

    bool lastBlock = false;
    while (!lastBlock) {
        lastBlock = reader.bit() != 0;
        const unsigned type = reader.bits(2);
        if (reader.overrun()) {
            result.error = "input ended inside a block header";
            return result;
        }

        if (type == 0) {
            // Unkomprimiert: Laenge und ihr Einerkomplement, dann die Bytes.
            reader.alignToByte();
            const unsigned length = reader.bits(16);
            const unsigned check = reader.bits(16);
            if (reader.overrun()) {
                result.error = "input ended in a stored block";
                return result;
            }
            if ((length ^ 0xFFFFU) != check) {
                result.error = "stored block length check failed";
                return result;
            }
            if (!reader.copyRaw(length, result.data, limit)) {
                result.error = "stored block does not fit";
                return result;
            }
            continue;
        }
        if (type == 3) {
            result.error = "reserved block type";
            return result;
        }

        Huffman dynamicLiterals;
        Huffman dynamicDistances;
        const Huffman* literals = &fixedLiterals;
        const Huffman* distances = &fixedDistances;

        if (type == 2) {
            // Die Tabellen stehen selbst komprimiert im Strom.
            const int literalCount = static_cast<int>(reader.bits(5)) + 257;
            const int distanceCount = static_cast<int>(reader.bits(5)) + 1;
            const int codeCount = static_cast<int>(reader.bits(4)) + 4;
            if (reader.overrun() || literalCount > 288 || distanceCount > 30) {
                result.error = "bad dynamic table sizes";
                return result;
            }

            static const int kOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                           11, 4,  12, 3, 13, 2, 14, 1, 15};
            unsigned char codeLengths[19] = {};
            for (int i = 0; i < codeCount; ++i) {
                codeLengths[kOrder[i]] = static_cast<unsigned char>(reader.bits(3));
            }
            Huffman codeTable;
            if (!codeTable.build(codeLengths, 19)) {
                result.error = "bad code-length table";
                return result;
            }

            unsigned char lengths[288 + 30] = {};
            int filled = 0;
            while (filled < literalCount + distanceCount) {
                const int symbol = codeTable.decode(reader);
                if (symbol < 0) {
                    result.error = "bad code-length symbol";
                    return result;
                }
                if (symbol < 16) {
                    lengths[filled++] = static_cast<unsigned char>(symbol);
                    continue;
                }
                int repeat = 0;
                unsigned char value = 0;
                if (symbol == 16) {
                    if (filled == 0) {
                        result.error = "repeat before any length";
                        return result;
                    }
                    value = lengths[filled - 1];
                    repeat = 3 + static_cast<int>(reader.bits(2));
                } else if (symbol == 17) {
                    repeat = 3 + static_cast<int>(reader.bits(3));
                } else {
                    repeat = 11 + static_cast<int>(reader.bits(7));
                }
                if (filled + repeat > literalCount + distanceCount) {
                    result.error = "length repeat runs past the table";
                    return result;
                }
                while (repeat-- > 0) { lengths[filled++] = value;
}
            }
            if (reader.overrun()) {
                result.error = "input ended inside the tables";
                return result;
            }
            if (!dynamicLiterals.build(lengths, literalCount) ||
                !dynamicDistances.build(lengths + literalCount, distanceCount)) {
                result.error = "bad huffman table";
                return result;
            }
            literals = &dynamicLiterals;
            distances = &dynamicDistances;
        }

        while (true) {
            const int symbol = literals->decode(reader);
            if (symbol < 0) {
                result.error = "bad literal symbol";
                return result;
            }
            if (symbol == 256) { break;  // Blockende
}

            if (symbol < 256) {
                if (result.data.size() >= limit) {
                    result.error = "output larger than declared";
                    return result;
                }
                result.data.push_back(static_cast<unsigned char>(symbol));
                continue;
            }

            const int lengthIndex = symbol - 257;
            if (lengthIndex >= 29) {
                result.error = "bad length code";
                return result;
            }
            const size_t length =
                static_cast<size_t>(kLengthBase[lengthIndex]) +
                reader.bits(kLengthExtra[lengthIndex]);

            const int distanceSymbol = distances->decode(reader);
            if (distanceSymbol < 0 || distanceSymbol >= 30) {
                result.error = "bad distance code";
                return result;
            }
            const size_t distance =
                static_cast<size_t>(kDistanceBase[distanceSymbol]) +
                reader.bits(kDistanceExtra[distanceSymbol]);

            // Die wichtigste Pruefung ueberhaupt: zeigt der Rueckverweis vor
            // den Anfang, wuerde ein naiver Auspacker in fremden Speicher
            // lesen. Genau damit bricht man einen Zip-Leser auf.
            if (distance > result.data.size()) {
                result.error = "back reference points before the start";
                return result;
            }
            if (result.data.size() + length > limit) {
                result.error = "output larger than declared";
                return result;
            }
            if (reader.overrun()) {
                result.error = "input ended inside a match";
                return result;
            }

            // Byteweise kopieren, nicht blockweise: bei distance < length
            // ueberlappen Quelle und Ziel, und genau das ist beabsichtigt -
            // so werden Wiederholungen ausgedrueckt.
            size_t from = result.data.size() - distance;
            for (size_t i = 0; i < length; ++i) {
                result.data.push_back(result.data[from + i]);
            }
        }
    }

    result.ok = true;
    return result;
}

}  // namespace bhed::inflate
