#include "audio_win32.h"

#include <windows.h>
#include <mmsystem.h>

#include <cstring>
#include <memory>
#include <utility>

namespace bhed::gui {

struct Audio::Buffer {
    WAVEHDR header{};
    std::vector<int16_t> data;
};

// Hier ist `Buffer` vollstaendig - deshalb stehen die Sonderfunktionen in
// dieser Datei und nicht im Kopf.
Audio::Audio() = default;
Audio::~Audio() { close(); }
Audio::Audio(Audio&&) noexcept = default;
Audio& Audio::operator=(Audio&&) noexcept = default;

bool Audio::open(int sampleRate, int channels) {
    if (handle_ && sampleRate == sampleRate_ && channels == channels_) return true;
    close();
    if (sampleRate <= 0 || channels < 1 || channels > 2) {
        error_ = "unsupported audio format";
        return false;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(channels);
    format.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(channels * 2);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HWAVEOUT device = nullptr;
    const MMRESULT result =
        waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        error_ = "waveOutOpen failed";
        return false;
    }
    handle_ = device;
    sampleRate_ = sampleRate;
    channels_ = channels;
    error_ = "";
    return true;
}

void Audio::close() {
    if (!handle_) return;
    auto device = static_cast<HWAVEOUT>(handle_);
    waveOutReset(device);
    for (const auto& buffer : buffers_) {
        waveOutUnprepareHeader(device, &buffer->header, sizeof(WAVEHDR));
    }
    buffers_.clear();
    waveOutClose(device);
    handle_ = nullptr;
    sampleRate_ = 0;
    channels_ = 0;
}

bool Audio::play(const std::vector<int16_t>& samples, float volume) {
    if (!handle_ || samples.empty()) {
        return false;
    }
    // --- DAZUMISCHEN, nicht anhaengen ------------------------------------
    //
    // Gemeldet: "als wuerden Sounds aufeinander warten beim Abspielen."
    //
    // Hier stand vorher ein eigener waveOutWrite je Klang. waveOut MISCHT
    // ABER NICHT - es spielt die geschriebenen Puffer der Reihe nach ab.
    // Klang zwei begann also erst, wenn Klang eins zu Ende war.
    //
    // Die Engine mischt bis zu 32 Kanaele in einen Puffer
    // (snd_mix.cpp:265, S_PaintChannels). Genau das tut `sound::Mixer`.
    mixer_.add(samples, volume);
    // Gleich nachlegen, damit der neue Klang nicht erst beim naechsten
    // Bild hoerbar wird.
    update();
    return true;
}

bool Audio::play(const std::vector<int16_t>& samples, float links, float rechts,
                 std::uint64_t schluessel) {
    if (!handle_) {
        return false;
    }
    mixer_.add(samples, links, rechts, schluessel);
    update();
    return true;
}

void Audio::update() {
    if (!handle_) return;
    auto device = static_cast<HWAVEOUT>(handle_);
    for (size_t i = 0; i < buffers_.size();) {
        if (buffers_[i]->header.dwFlags & WHDR_DONE) {
            waveOutUnprepareHeader(device, &buffers_[i]->header, sizeof(WAVEHDR));
            buffers_.erase(buffers_.begin() + static_cast<ptrdiff_t>(i));
        } else {
            ++i;
        }
    }

    // --- Und den Strom am Laufen halten -------------------------------
    //
    // Solange etwas klingt, muessen Bloecke nachgeschoben werden - sonst
    // laeuft die Ausgabe leer und der Rest des Klangs faellt weg.
    //
    // Zwei Bloecke im Voraus: genug, dass zwischen zwei Bildern nichts
    // abreisst, und wenig genug, dass ein neuer Klang schnell hoerbar
    // wird. Bei 2048 Bildern und 44,1 kHz sind das rund 46 ms je Block.
    while (mixer_.active() != 0U && buffers_.size() < kVorlauf) {
        auto buffer = std::make_unique<Buffer>();
        mixer_.mixInto(buffer->data, kBlockFrames, channels_);
        if (buffer->data.empty()) {
            break;
        }
        buffer->header = WAVEHDR{};
        buffer->header.lpData = reinterpret_cast<LPSTR>(buffer->data.data());
        buffer->header.dwBufferLength =
            static_cast<DWORD>(buffer->data.size() * sizeof(int16_t));
        if (waveOutPrepareHeader(device, &buffer->header, sizeof(WAVEHDR)) !=
            MMSYSERR_NOERROR) {
            break;
        }
        if (waveOutWrite(device, &buffer->header, sizeof(WAVEHDR)) !=
            MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(device, &buffer->header, sizeof(WAVEHDR));
            break;
        }
        buffers_.push_back(std::move(buffer));
    }
}

void Audio::stopAll() {
    if (!handle_) return;
    // ZUERST den Mischer leeren.
    //
    // Sonst schiebt `update()` unten sofort wieder einen Block aus
    // denselben Klaengen nach - "alles anhalten" haette dann keine
    // Wirkung, und beim Musikwechsel liefe das alte Stueck weiter.
    mixer_.clear();
    waveOutReset(static_cast<HWAVEOUT>(handle_));
    update();
}

int Audio::activeBuffers() const { return static_cast<int>(buffers_.size()); }

}  // namespace bhed::gui
