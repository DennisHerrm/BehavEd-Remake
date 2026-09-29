#include <algorithm>
#include "bhed/sound.h"

#include <cstddef>
#include <cmath>
#include <string>
#include <cstring>
#include <cctype>
#include <utility>

#define MINIMP3_ONLY_MP3       // MP1 und MP2 brauchen wir nicht
#define MINIMP3_NO_SIMD        // damit das Ergebnis auf jedem Rechner gleich ist
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

namespace bhed::sound {
namespace {

// Eine Obergrenze. Ein Effektklang ist ein paar Sekunden lang; eine Datei, die
// eine Stunde behauptet, ist entweder kaputt oder gehoert nicht hierher.
constexpr size_t kMaxSamples = size_t{48000} * 2 * 600;  // zehn Minuten in Stereo

uint32_t readU32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t readU16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

int16_t clampSample(float value) {
    if (value > 32767.0F) { return 32767;
}
    if (value < -32768.0F) { return -32768;
}
    return static_cast<int16_t>(std::lround(value));
}

}  // namespace

size_t Sound::frameCount() const {
    return channels > 0 ? samples.size() / static_cast<size_t>(channels) : 0;
}

float Sound::durationMs() const {
    if (sampleRate <= 0) { return 0.0F;
}
    return static_cast<float>(frameCount()) * 1000.0F /
           static_cast<float>(sampleRate);
}

Format sniff(const unsigned char* data, size_t size) {
    if ((data == nullptr) || size < 4) { return Format::Unknown;
}
    if (std::memcmp(data, "RIFF", 4) == 0) { return Format::Wav;
}

    // MP3 hat keine feste Kennung am Anfang. Zwei Faelle: ein ID3-Kopf, oder
    // gleich ein Rahmen, der mit elf gesetzten Bits beginnt.
    if (std::memcmp(data, "ID3", 3) == 0) { return Format::Mp3;
}
    if (data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) { return Format::Mp3;
}
    return Format::Unknown;
}

Sound decodeWav(const unsigned char* data, size_t size) {
    Sound out;
    auto fail = [&](const std::string& why) {
        out.error = why;
        return out;
    };
    if ((data == nullptr) || size < 44) { return fail("too small for a WAV file");
}
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
        return fail("not a RIFF/WAVE file");
    }

    // Die Abschnitte durchgehen. Reihenfolge und Anzahl sind nicht
    // festgelegt - wer fest annimmt, dass fmt bei Byte 12 und data bei 36
    // steht, scheitert an jeder Datei mit einem LIST-Abschnitt, und die sind
    // haeufig.
    size_t pos = 12;
    int formatTag = 0;
    int channels = 0;
    int bitsPerSample = 0;
    uint32_t sampleRate = 0;
    const unsigned char* audio = nullptr;
    size_t audioSize = 0;

    while (pos + 8 <= size) {
        const char* id = reinterpret_cast<const char*>(data + pos);
        const uint32_t chunkSize = readU32(data + pos + 4);
        const size_t body = pos + 8;
        if (body + chunkSize > size) {
            // Abgeschnitten: nehmen, was da ist, statt alles zu verwerfen.
            if (std::memcmp(id, "data", 4) == 0 && body < size) {
                audio = data + body;
                audioSize = size - body;
            }
            break;
        }
        if (std::memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16) {
            formatTag = readU16(data + body);
            channels = readU16(data + body + 2);
            sampleRate = readU32(data + body + 4);
            bitsPerSample = readU16(data + body + 14);
            // WAVE_FORMAT_EXTENSIBLE traegt die echte Art im Untertyp.
            if (formatTag == 0xFFFE && chunkSize >= 40) {
                formatTag = readU16(data + body + 24);
            }
        } else if (std::memcmp(id, "data", 4) == 0) {
            audio = data + body;
            audioSize = chunkSize;
        }
        // Abschnitte sind auf gerade Laenge aufgefuellt.
        pos = body + chunkSize + (chunkSize & 1U);
    }

    if ((audio == nullptr) || audioSize == 0) { return fail("no audio data in the WAV file");
}
    if (channels < 1 || channels > 8) { return fail("unsupported channel count");
}
    if (sampleRate < 1000 || sampleRate > 192000) { return fail("implausible sample rate");
}

    constexpr int kPcm = 1;
    constexpr int kFloat = 3;
    if (formatTag != kPcm && formatTag != kFloat) {
        return fail("compressed WAV files are not supported (format " +
                    std::to_string(formatTag) + ")");
    }

    const size_t bytesPerSample = static_cast<size_t>(bitsPerSample) / 8;
    if (bytesPerSample == 0) { return fail("bad bit depth");
}
    const size_t count = audioSize / bytesPerSample;
    if (count > kMaxSamples) { return fail("sound is implausibly long");
}

    out.samples.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const unsigned char* p = audio + i * bytesPerSample;
        switch (bitsPerSample) {
            case 8:
                // Acht Bit sind vorzeichenlos, mit 128 als Mitte - als
                // einzige Breite. Wer das uebersieht, bekommt ein lautes
                // Knacken und ein Signal, das nur die obere Haelfte nutzt.
                out.samples[i] = static_cast<int16_t>((static_cast<int>(p[0]) - 128)
                                                      * 256);
                break;
            case 16:
                out.samples[i] = static_cast<int16_t>(readU16(p));
                break;
            case 24: {
                const int32_t value = (static_cast<int32_t>(p[2]) << 24) |
                                      (static_cast<int32_t>(p[1]) << 16) |
                                      (static_cast<int32_t>(p[0]) << 8);
                out.samples[i] = static_cast<int16_t>(value >> 16);
                break;
            }
            case 32:
                if (formatTag == kFloat) {
                    float value = 0.0F;
                    std::memcpy(&value, p, 4);
                    out.samples[i] = clampSample(value * 32767.0F);
                } else {
                    const int32_t value = static_cast<int32_t>(readU32(p));
                    out.samples[i] = static_cast<int16_t>(value >> 16);
                }
                break;
            default:
                return fail("unsupported bit depth " + std::to_string(bitsPerSample));
        }
    }

    out.sampleRate = static_cast<int>(sampleRate);
    out.channels = channels;
    out.ok = true;
    return out;
}

Sound decodeMp3(const unsigned char* data, size_t size) {
    Sound out;
    if ((data == nullptr) || size < 4) {
        out.error = "too small for an MP3";
        return out;
    }

    mp3dec_t decoder;
    mp3dec_init(&decoder);

    size_t offset = 0;
    int16_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_frame_info_t info{};
    int guard = 0;

    while (offset < size) {
        const int samples = mp3dec_decode_frame(
            &decoder, data + offset, static_cast<int>(size - offset), frame, &info);

        if (info.frame_bytes == 0) { break;  // nichts mehr zu holen
}
        offset += static_cast<size_t>(info.frame_bytes);

        if (samples > 0) {
            if (out.channels == 0) {
                out.channels = info.channels;
                out.sampleRate = info.hz;
            }
            const size_t count = static_cast<size_t>(samples) *
                                 static_cast<size_t>(info.channels);
            if (out.samples.size() + count > kMaxSamples) {
                out.error = "sound is implausibly long";
                return out;
            }
            out.samples.insert(out.samples.end(), frame, frame + count);
        }
        // Eine Notbremse gegen Dateien, die den Decoder nicht vorankommen
        // lassen: ohne sie kann eine praeparierte Datei die Schleife ewig
        // laufen lassen.
        if (++guard > 200000) { break;
}
    }

    if (out.samples.empty()) {
        out.error = out.error.empty() ? "no MP3 frames found" : out.error;
        return out;
    }
    out.ok = true;
    return out;
}

Sound decode(const unsigned char* data, size_t size) {
    Sound out;
    switch (sniff(data, size)) {
        case Format::Wav: out = decodeWav(data, size); break;
        case Format::Mp3: out = decodeMp3(data, size); break;
        default:
            out.error = "unrecognised sound format";
            return out;
    }
    // Den Massstab fuer die Mundanimation gleich hier bilden - er haengt an
    // der ganzen Datei, und je Bild darueber zu laufen waere Verschwendung.
    //
    // snd_mem.cpp:246 ff.:
    //     if (iSample < 0) iSample = -iSample;
    //     if (sfx->fVolRange < (iSample >> 8)) sfx->fVolRange = iSample >> 8;
    int groesster = 0;
    for (const int16_t v : out.samples) {
        const int a = (v < 0) ? -static_cast<int>(v) : static_cast<int>(v);
        if ((a >> 8) > groesster) {
            groesster = a >> 8;
        }
    }
    out.volRange = groesster;
    return out;
}

int voiceLevel(const Sound& s, double posMs) {
    // Vor dem Anfang oder ohne Massstab laesst sich nichts sagen.
    if (!s.ok || s.sampleRate <= 0 || s.channels < 1 || s.volRange <= 0 ||
        posMs < 0.0) {
        return 0;
    }
    const auto rahmen = static_cast<long long>(s.frameCount());
    const auto stelle = static_cast<long long>(
        posMs / 1000.0 * static_cast<double>(s.sampleRate));
    if (stelle >= rahmen) {
        return 0;   // der Klang ist durch
    }

    // snd_dma.cpp:2356 ff. - zehn Abtastwerte im Abstand von 100.
    //
    // Der Abstand ist in der Engine FEST, unabhaengig von der Abtastrate;
    // der Kommentar dort sagt dazu "100 (at 11hz or 200 at 22hz) samples
    // apart", die Zahl im Code bleibt aber 100. Wir bleiben beim Code.
    //
    // Vielkanaliges wird ueber den ERSTEN Kanal gelesen. Die Engine
    // arbeitet auf einem einkanaligen Strom; die Sprachdateien von JKA sind
    // durchweg einkanalig, und fuer den Pegel genuegt eine Seite.
    long long summe = 0;
    int zahl = 0;
    for (int i = 0; i < 10; ++i) {
        const long long r = stelle + static_cast<long long>(i) * 100;
        if (r >= rahmen) {
            break;
        }
        const auto idx = static_cast<std::size_t>(r * s.channels);
        if (idx >= s.samples.size()) {
            break;
        }
        // ARITHMETISCH schieben, nicht teilen: bei negativen Werten ist
        // >> 8 nicht dasselbe wie / 256 (es rundet gegen minus unendlich).
        // Die Engine schiebt, also schieben wir auch.
        const int wert = static_cast<int>(s.samples[idx]) >> 8;
        summe += static_cast<long long>(wert) * wert;
        ++zahl;
    }
    if (zahl == 0) {
        return 0;
    }
    summe /= zahl;

    // Die Vorgaben von s_threshold1..4 (snd_dma.cpp:456 ff.).
    const double mass = static_cast<double>(s.volRange);
    const auto gesamt = static_cast<double>(summe);
    if (gesamt < mass * 0.3) { return -1; }   // spricht, aber gerade still
    if (gesamt < mass * 4.0) { return 1; }
    if (gesamt < mass * 6.0) { return 2; }
    if (gesamt < mass * 8.0) { return 3; }
    return 4;
}

std::vector<int16_t> convert(const std::vector<int16_t>& samples,
                             int sourceRate, int sourceChannels,
                             int targetRate, int targetChannels) {
    std::vector<int16_t> out;
    if (samples.empty() || sourceRate <= 0 || targetRate <= 0 ||
        sourceChannels < 1 || targetChannels < 1) {
        return out;
    }

    const size_t frames = samples.size() / static_cast<size_t>(sourceChannels);
    if (frames == 0) { return out;
}

    // Wie viele Bilder kommen heraus? Aufrunden, damit der letzte nicht
    // abgeschnitten wird - bei einem kurzen Aufschlag ist das hoerbar.
    const double ratio = static_cast<double>(targetRate) /
                         static_cast<double>(sourceRate);
    const auto outFrames = static_cast<size_t>(
        std::ceil(static_cast<double>(frames) * ratio));
    if (outFrames == 0) { return out;
}

    out.resize(outFrames * static_cast<size_t>(targetChannels));

    // Einen Kanal an einer gebrochenen Stelle lesen, linear dazwischen.
    const auto sampleAt = [&](double frame, int channel) {
        const auto index = static_cast<size_t>(frame);
        const double fraction = frame - static_cast<double>(index);
        const size_t last = frames - 1;
        const size_t a = index < last ? index : last;
        const size_t b = a < last ? a + 1 : last;
        const int ch = channel < sourceChannels ? channel : sourceChannels - 1;
        const double first = samples[a * static_cast<size_t>(sourceChannels) +
                                     static_cast<size_t>(ch)];
        const double second = samples[b * static_cast<size_t>(sourceChannels) +
                                      static_cast<size_t>(ch)];
        return first + (second - first) * fraction;
    };

    for (size_t i = 0; i < outFrames; ++i) {
        const double source = static_cast<double>(i) / ratio;
        for (int c = 0; c < targetChannels; ++c) {
            double value;
            if (sourceChannels == targetChannels || targetChannels > sourceChannels) {
                // Gleich viele Kanaele, oder es kommen welche dazu: dann
                // bekommt jeder zusaetzliche denselben Inhalt. Mono auf zwei
                // Lautsprecher heisst mittig, und das ist richtig so.
                value = sampleAt(source, c);
            } else {
                // Weniger Kanaele: mitteln. Sonst faellt bei Stereo eine
                // Haelfte des Klangs weg.
                double sum = 0.0;
                for (int sc = 0; sc < sourceChannels; ++sc) {
                    sum += sampleAt(source, sc);
                }
                value = sum / static_cast<double>(sourceChannels);
            }
            out[i * static_cast<size_t>(targetChannels) +
                static_cast<size_t>(c)] = clampSample(static_cast<float>(value));
        }
    }
    return out;
}

// --- Wie laut ist ein Klang aus dieser Entfernung? -----------------------
//
// Gemeldet: "die Effektsounds sind so penetrant und spielen dauerhaft.
// Sind das vielleicht 3D-Sounds, die nur abspielen, wenn man in der Naehe
// ist?"
//
// Ja. Die Engine spielt Effektklaenge AM ORT (FxScheduler.cpp:1884):
//
//     theFxHelper.PlaySound( org, ENTITYNUM_NONE, CHAN_AUTO, ... );
//
// und daempft sie nach Entfernung (snd_dma.cpp:1356 ff.):
//
//     dist = |origin - listener|
//     dist -= SOUND_FULLVOLUME;      // 256
//     if (dist < 0) dist = 0;
//     dist *= SOUND_ATTENUATE;       // 0.0008
//     scale = 1.0 - dist;
//
// Bis 256 Einheiten also volle Lautstaerke, danach linear abfallend, und
// bei 256 + 1/0,0008 = **1506 Einheiten stumm**.
//
// behaved kannte das nicht: jeder fx_runner spielte in voller Lautstaerke,
// egal wie weit weg er stand. Bei 28 Laeufern in einer Karte ist das genau
// der Dauerlaerm, den du gehoert hast.
float distanceVolume(const float quelle[3],
                                   const float hoerer[3]) {
    const float dx = quelle[0] - hoerer[0];
    const float dy = quelle[1] - hoerer[1];
    const float dz = quelle[2] - hoerer[2];
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    dist -= 256.0F;               // SOUND_FULLVOLUME
    if (dist < 0.0F) {
        dist = 0.0F;
    }
    dist *= 0.0008F;              // SOUND_ATTENUATE
    return std::clamp(1.0F - dist, 0.0F, 1.0F);
}


Kanal kanalAusName(const std::string& name) {
    std::string n;
    for (const char c : name) {
        n += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    static const std::pair<const char*, Kanal> kTabelle[] = {
        {"CHAN_AUTO", Kanal::Auto},           {"CHAN_LOCAL", Kanal::Local},
        {"CHAN_WEAPON", Kanal::Weapon},       {"CHAN_VOICE", Kanal::Voice},
        {"CHAN_VOICE_ATTEN", Kanal::VoiceAtten}, {"CHAN_VOICE_GLOBAL", Kanal::VoiceGlobal},
        {"CHAN_ITEM", Kanal::Item},           {"CHAN_BODY", Kanal::Body},
        {"CHAN_AMBIENT", Kanal::Ambient},     {"CHAN_LOCAL_SOUND", Kanal::LocalSound},
        {"CHAN_ANNOUNCER", Kanal::Announcer}, {"CHAN_LESS_ATTEN", Kanal::LessAtten},
        {"CHAN_MUSIC", Kanal::Music},
    };
    for (const auto& [k, v] : kTabelle) {
        if (n == k) {
            return v;
        }
    }
    return Kanal::Auto;
}

Raumlaut raumlaut(const float quelle[3], const float hoerer[3], const float hoererRechts[3], Kanal k) {
    Raumlaut r;
    if (k == Kanal::VoiceGlobal || k == Kanal::Announcer) {
        return r;   // S_Respatialize: immer voll, ohne Trennung
    }
    float v[3] = {quelle[0] - hoerer[0], quelle[1] - hoerer[1], quelle[2] - hoerer[2]};
    float dist = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (dist > 1e-4F) {
        for (float& x : v) { x /= dist; }
    }
    float mult = 0.0008F;   // SOUND_ATTENUATE
    switch (k) {
    case Kanal::Voice: dist -= 256.0F * 3.0F; break;
    case Kanal::LessAtten: dist -= 256.0F * 5.0F; break;
    case Kanal::VoiceAtten:
        dist -= 256.0F * 1.35F;
        mult = 0.004F;       // VOICE_ATTENUATE
        break;
    default: dist -= 256.0F; break;
    }
    dist = std::max(0.0F, dist) * mult;
    // listener_axis[1] zeigt nach LINKS; -dot(links, v) = dot(rechts, v).
    const float dot = hoererRechts[0] * v[0] + hoererRechts[1] * v[1] + hoererRechts[2] * v[2];
    constexpr float kTrennung = 0.5F;   // s_separation
    const float rs = std::max(0.0F, kTrennung + (1.0F - kTrennung) * dot);
    const float ls = std::max(0.0F, kTrennung - (1.0F - kTrennung) * dot);
    r.rechts = std::max(0.0F, (1.0F - dist) * rs);
    r.links = std::max(0.0F, (1.0F - dist) * ls);
    return r;
}

void Mixer::add(const std::vector<int16_t>& samples, float volume) {
    add(samples, volume, volume, 0);
}

void Mixer::add(const std::vector<int16_t>& samples, float links, float rechts, std::uint64_t schluessel) {
    const float volume = std::max(links, rechts);
    if (schluessel != 0) {
        // Channel-Stomp: der laufende Klang dieses Kanals endet sofort.
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                     [schluessel](const Voice& v) { return v.schluessel == schluessel; }),
                      voices_.end());
    }
    if (samples.empty() || volume <= 0.001F) {
        return;
    }
    if (voices_.size() >= kMaxVoices) {
        // Den LEISESTEN verdraengen, nicht den aeltesten.
        //
        // Die Engine sucht den Kanal, der am naechsten am Ende ist
        // (S_PickChannel). Bei uns ist "leise" das bessere Mass: ein weit
        // entfernter Klang faellt beim Verdraengen am wenigsten auf, und
        // genau darum ging es bei der Daempfung aus rc334.
        auto leisester = voices_.begin();
        for (auto it = voices_.begin(); it != voices_.end(); ++it) {
            if (it->volume < leisester->volume) {
                leisester = it;
            }
        }
        if (leisester->volume >= volume) {
            return;   // alle lauter als der neue - dann faellt der neue weg
        }
        voices_.erase(leisester);
    }
    Voice v;
    v.samples = samples;
    v.volume = volume;
    v.links = links;
    v.rechts = rechts;
    v.schluessel = schluessel;
    voices_.push_back(std::move(v));
}

std::size_t Mixer::mixInto(std::vector<int16_t>& out, std::size_t frames,
                           int channels) {
    const std::size_t werte =
        frames * static_cast<std::size_t>(std::max(channels, 1));
    out.assign(werte, 0);
    if (voices_.empty()) {
        return 0;
    }
    // In INT gerechnet, nicht in int16.
    //
    // Zwei Klaenge zu je 20000 ergeben 40000 - das passt nicht in ein
    // int16 und liefe ueber, aus laut wuerde schlagartig leise mit
    // umgekehrtem Vorzeichen (ein Knacken). Deshalb erst summieren, dann
    // begrenzen.
    std::vector<int> summe(werte, 0);
    for (Voice& v : voices_) {
        const std::size_t rest = v.samples.size() - v.pos;
        const std::size_t n = std::min(werte, rest);
        // Bei Stereo liegen die Werte verschraenkt: gerade links, ungerade
        // rechts. Der Anfang eines Blocks ist immer ein linker Wert, weil
        // pos stets um ganze Bilder weiterzaehlt.
        if (channels == 2) {
            for (std::size_t i = 0; i < n; ++i) {
                const float lv = (((v.pos + i) & 1U) == 0U) ? v.links : v.rechts;
                summe[i] += static_cast<int>(static_cast<float>(v.samples[v.pos + i]) * lv);
            }
        } else {
            for (std::size_t i = 0; i < n; ++i) {
                summe[i] += static_cast<int>(static_cast<float>(v.samples[v.pos + i]) * v.volume);
            }
        }
        v.pos += n;
    }
    for (std::size_t i = 0; i < werte; ++i) {
        out[i] = static_cast<int16_t>(
            std::clamp(summe[i], -32768, 32767));
    }
    // Fertige herauswerfen.
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) {
                                     return v.pos >= v.samples.size();
                                 }),
                  voices_.end());
    return voices_.size();
}

}  // namespace bhed::sound
