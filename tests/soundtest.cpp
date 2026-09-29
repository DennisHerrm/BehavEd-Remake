// soundtest.cpp - Klangleser
//
// Das Abspielen laesst sich hier nicht pruefen (kein Tongeraet), das LESEN
// dagegen vollstaendig. Die Proben bauen die Dateien selbst, damit der Test
// ohne Beigaben laeuft.
#include "bhed/sound.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {
int fails = 0;
bool near(float a, float b, float tol) {
    return (a > b ? a - b : b - a) < tol;
}
void expect(const char* what, bool ok) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", what);
    if (!ok) { ++fails; }
}

void put32(std::vector<unsigned char>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) { b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF)); }
}
void put16(std::vector<unsigned char>& b, std::uint16_t v) {
    for (int i = 0; i < 2; ++i) { b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF)); }
}
void putStr(std::vector<unsigned char>& b, const char* s) {
    for (const char* p = s; *p != '\0'; ++p) { b.push_back(static_cast<unsigned char>(*p)); }
}

// Ein WAV mit 16 Bit PCM bauen.
std::vector<unsigned char> makeWav(int rate, int channels, int frames) {
    std::vector<unsigned char> b;
    const std::uint32_t dataBytes =
        static_cast<std::uint32_t>(frames * channels * 2);
    putStr(b, "RIFF");
    put32(b, 36 + dataBytes);
    putStr(b, "WAVE");
    putStr(b, "fmt ");
    put32(b, 16);
    put16(b, 1);                                    // PCM
    put16(b, static_cast<std::uint16_t>(channels));
    put32(b, static_cast<std::uint32_t>(rate));
    put32(b, static_cast<std::uint32_t>(rate * channels * 2));
    put16(b, static_cast<std::uint16_t>(channels * 2));
    put16(b, 16);
    putStr(b, "data");
    put32(b, dataBytes);
    for (int i = 0; i < frames * channels; ++i) {
        const auto v = static_cast<std::int16_t>((i % 100) * 300 - 15000);
        put16(b, static_cast<std::uint16_t>(v));
    }
    return b;
}
}  // namespace

int main() {
    // --- WAV -------------------------------------------------------------
    {
        const auto w = makeWav(22050, 1, 1000);
        expect("WAV am Inhalt erkannt",
               bhed::sound::sniff(w.data(), w.size()) == bhed::sound::Format::Wav);
        const bhed::sound::Sound s = bhed::sound::decode(w.data(), w.size());
        expect("WAV gelesen", s.ok);
        expect("Abtastrate stimmt", s.sampleRate == 22050);
        expect("ein Kanal", s.channels == 1);
        expect("1000 Bilder", s.frameCount() == 1000);
        expect("Dauer rund 45 ms", s.durationMs() > 44.0F && s.durationMs() < 46.0F);
    }
    {
        const auto w = makeWav(44100, 2, 500);
        const bhed::sound::Sound s = bhed::sound::decode(w.data(), w.size());
        expect("Stereo gelesen", s.ok && s.channels == 2 && s.frameCount() == 500);
    }

    // --- Kaputte Eingaben ------------------------------------------------
    {
        const auto good = makeWav(11025, 1, 200);
        for (std::size_t cut : {std::size_t{0}, std::size_t{4}, std::size_t{20},
                                std::size_t{40}, good.size() / 2}) {
            const bhed::sound::Sound s =
                bhed::sound::decode(good.data(), cut);   // darf nur nicht knallen
            (void)s;
        }
        expect("abgeschnittene WAV ueberstanden", true);

        // Ein WAV, das eine unsinnige Datenmenge behauptet.
        auto lying = good;
        std::uint32_t huge = 0x7FFFFFFFU;
        std::memcpy(lying.data() + lying.size() - 4 - 400, &huge, 4);
        const bhed::sound::Sound s = bhed::sound::decode(lying.data(), lying.size());
        expect("erlogene Groesse liefert keinen Riesenpuffer",
               !s.ok || s.samples.size() < 1000000);
    }
    {
        const unsigned char junk[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
        const bhed::sound::Sound s = bhed::sound::decode(junk, sizeof(junk));
        expect("Muell wird abgewiesen und benannt", !s.ok && !s.error.empty());
    }

    // --- Umrechnen -------------------------------------------------------
    {
        // Der Fall aus efxed: eine 11025er-Datei muss auf die feste Rate des
        // Geraets gebracht werden, sonst lehnt waveOut sie je nach Treiber ab.
        std::vector<std::int16_t> mono(1000, 1234);
        const auto out = bhed::sound::convert(mono, 11025, 1, 44100, 2);
        expect("11025 mono -> 44100 stereo, Laenge passt",
               out.size() == mono.size() * 4 * 2);
        const auto same = bhed::sound::convert(mono, 44100, 1, 44100, 1);
        expect("gleiches Format bleibt unveraendert", same.size() == mono.size());
    }

    // --- Der Pegel fuer die Mundanimation ----------------------------------
    //
    // Zeile fuer Zeile nach snd_dma.cpp:2333 ff. nachgebaut. Die Probe baut
    // sich einen Klang mit BEKANNTEN Werten, damit die Stufen belegbar sind
    // und nicht bloss "irgendwas kommt heraus".
    //
    // Aufbau: 22050 Hz, einkanalig, 2000 Bilder. Die erste Haelfte ist
    // still (Wert 0), die zweite laut (Wert 30000).
    {
        std::vector<unsigned char> b;
        const int rate = 22050;
        const int frames = 2000;
        const auto dataBytes = static_cast<std::uint32_t>(frames * 2);
        putStr(b, "RIFF"); put32(b, 36 + dataBytes); putStr(b, "WAVE");
        putStr(b, "fmt "); put32(b, 16); put16(b, 1); put16(b, 1);
        put32(b, static_cast<std::uint32_t>(rate));
        put32(b, static_cast<std::uint32_t>(rate * 2));
        put16(b, 2); put16(b, 16);
        putStr(b, "data"); put32(b, dataBytes);
        for (int i = 0; i < frames; ++i) {
            put16(b, static_cast<std::uint16_t>(
                         static_cast<std::int16_t>(i < 1000 ? 0 : 30000)));
        }
        const bhed::sound::Sound s = bhed::sound::decode(b.data(), b.size());
        expect("der Probeklang liest sich", s.ok && s.frameCount() == 2000);

        // volRange = groesster Betrag >> 8 = 30000 >> 8 = 117.
        expect("der Massstab ist der groesste Wert, um 8 geschoben",
               s.volRange == 117);

        // Im stillen Teil: alle zehn Abtastwerte 0, Mittel 0.
        // 0 < 117*0.3 -> Stufe -1 (spricht, aber gerade still).
        expect("im stillen Teil: spricht, aber still",
               bhed::sound::voiceLevel(s, 0.0) == -1);

        // Im lauten Teil ab Bild 1000: jeder Wert 30000>>8 = 117,
        // quadriert 13689, Mittel 13689. Weit ueber 117*8 = 936 -> Stufe 4.
        const double lautMs = 1000.0 / rate * 1000.0;
        expect("im lauten Teil: hoechste Stufe",
               bhed::sound::voiceLevel(s, lautMs + 1.0) == 4);

        // Hinter dem Ende spricht niemand mehr. DAS ist die Zusicherung,
        // die den Mund wieder schliesst - ohne sie bliebe er nach dem
        // letzten Satz offen stehen.
        expect("hinter dem Ende: spricht nicht",
               bhed::sound::voiceLevel(s, 5000.0) == 0);
        expect("vor dem Anfang ebenfalls nicht",
               bhed::sound::voiceLevel(s, -10.0) == 0);

        // Ohne Massstab laesst sich nichts sagen - sonst kaeme bei einem
        // durchweg stillen Klang die hoechste Stufe heraus, weil alle
        // Schwellen null waeren.
        bhed::sound::Sound leer = s;
        leer.volRange = 0;
        expect("ohne Massstab kein Pegel",
               bhed::sound::voiceLevel(leer, 0.0) == 0);
    }

    // --- Die Entfernungsdaempfung ------------------------------------------
    //
    // Gemeldet: "die Effektsounds sind so penetrant und spielen dauerhaft.
    // Sind das vielleicht 3D-Sounds, die nur abspielen, wenn man in der
    // Naehe ist?"
    //
    // Ja - und behaved kannte das nicht. Jeder fx_runner spielte in voller
    // Lautstaerke, egal wie weit weg er stand. Bei 28 Laeufern in einer
    // Karte ist das der Dauerlaerm.
    //
    // snd_dma.cpp:1356 ff.:
    //
    //     dist -= SOUND_FULLVOLUME;   // 256
    //     if (dist < 0) dist = 0;
    //     dist *= SOUND_ATTENUATE;    // 0.0008
    //     scale = 1.0 - dist;
    //
    // Nachgerechnet gegen diese Formel:
    {
        const float ohr[3] = {0.0F, 0.0F, 0.0F};
        auto beiAbstand = [&](float d) {
            const float q[3] = {d, 0.0F, 0.0F};
            return bhed::sound::distanceVolume(q, ohr);
        };
        expect("am Ort selbst volle Lautstaerke",
               near(beiAbstand(0.0F), 1.0F, 0.001F));
        // Bis SOUND_FULLVOLUME faellt gar nichts ab.
        expect("bis 256 Einheiten unveraendert",
               near(beiAbstand(256.0F), 1.0F, 0.001F));
        // 500: (500-256)*0.0008 = 0.1952  ->  80,5 %
        expect("bei 500 Einheiten rund 80 Prozent",
               near(beiAbstand(500.0F), 0.805F, 0.005F));
        // Die Haelfte bei 256 + 0.5/0.0008 = 881.
        expect("bei 881 Einheiten die Haelfte",
               near(beiAbstand(881.0F), 0.5F, 0.005F));
        // Stumm ab 256 + 1/0.0008 = 1506.
        expect("ab 1506 Einheiten stumm",
               near(beiAbstand(1506.0F), 0.0F, 0.001F));
        // Und NICHT negativ dahinter - sonst kaeme der Klang jenseits
        // davon mit umgedrehtem Vorzeichen zurueck.
        expect("und dahinter nicht negativ",
               beiAbstand(5000.0F) >= 0.0F);
    }

    // --- Der Mischer: Klaenge laufen GLEICHZEITIG --------------------------
    //
    // Gemeldet: "als wuerden Sounds aufeinander warten beim Abspielen."
    //
    // Genau das taten sie: behaved gab jeden Klang mit einem eigenen
    // `waveOutWrite` an dasselbe Geraet - und waveOut MISCHT NICHT, es
    // spielt die Puffer der Reihe nach. Klang zwei begann erst, wenn Klang
    // eins zu Ende war.
    //
    // Die Engine mischt bis zu 32 Kanaele in einen Puffer
    // (snd_mix.cpp:265, S_PaintChannels).
    //
    // DIESE Probe ist der Prueftstein ohne Ohren: die Tonausgabe kann ich
    // auf diesem Rechner nicht hoeren, aber nachrechnen, dass im Puffer die
    // SUMME steht.
    {
        bhed::sound::Mixer m;
        const std::vector<int16_t> a(100, 1000);
        const std::vector<int16_t> b(100, 2000);
        m.add(a, 1.0F);
        m.add(b, 1.0F);
        expect("beide Klaenge sind aktiv", m.active() == 2U);

        std::vector<int16_t> aus;
        m.mixInto(aus, 50, 1);
        expect("der Block hat die verlangte Laenge", aus.size() == 50U);
        // DAS ist der Punkt: 1000 + 2000, nicht nacheinander.
        expect("beide klingen GLEICHZEITIG, also die Summe",
               aus[0] == 3000 && aus[49] == 3000);
        expect("und beide laufen noch", m.active() == 2U);

        // Nach dem Rest sind sie fertig und fallen heraus.
        m.mixInto(aus, 50, 1);
        expect("danach ist keiner mehr aktiv", m.active() == 0U);
        // Und der leere Mischer liefert STILLE, kein altes Echo.
        m.mixInto(aus, 10, 1);
        bool still = true;
        for (const int16_t w : aus) {
            if (w != 0) { still = false; }
        }
        expect("ein leerer Mischer liefert Stille", still);
    }

    // --- Ueberlauf: laut plus laut wird nicht leise ------------------------
    //
    // Zwei Klaenge zu je 20000 ergeben 40000 - das passt NICHT in ein
    // int16. Ohne Begrenzung liefe es ueber, und aus laut wuerde
    // schlagartig leise mit umgekehrtem Vorzeichen: ein Knacken.
    //
    // Deshalb wird in int summiert und erst danach begrenzt.
    {
        bhed::sound::Mixer m;
        const std::vector<int16_t> laut(10, 20000);
        m.add(laut, 1.0F);
        m.add(laut, 1.0F);
        std::vector<int16_t> aus;
        m.mixInto(aus, 10, 1);
        expect("laut plus laut bleibt laut, statt umzuschlagen",
               aus[0] == 32767);
        // Und dasselbe nach unten.
        bhed::sound::Mixer m2;
        const std::vector<int16_t> tief(10, -20000);
        m2.add(tief, 1.0F);
        m2.add(tief, 1.0F);
        m2.mixInto(aus, 10, 1);
        expect("und nach unten ebenso", aus[0] == -32768);
    }

    // --- Die Lautstaerke aus rc334 wirkt im Mischer ------------------------
    {
        bhed::sound::Mixer m;
        const std::vector<int16_t> s1(10, 1000);
        m.add(s1, 0.5F);
        std::vector<int16_t> aus;
        m.mixInto(aus, 10, 1);
        expect("halbe Lautstaerke halbiert die Werte",
               aus[0] > 480 && aus[0] < 520);
        // Und ein stummer Klang belegt gar keinen Kanal.
        bhed::sound::Mixer m2;
        m2.add(s1, 0.0F);
        expect("ein stummer Klang belegt keinen Kanal", m2.active() == 0U);
    }

    // --- Mehr als 32: der LEISESTE weicht ----------------------------------
    //
    // 32 wie MAX_CHANNELS (snd_local.h:189). Wird es voll, soll der
    // leiseste weichen - ein weit entfernter Klang faellt am wenigsten auf.
    // Genau darum ging es bei der Daempfung aus rc334.
    {
        bhed::sound::Mixer m;
        const std::vector<int16_t> s1(1000, 1000);
        for (std::size_t i = 0; i < bhed::sound::Mixer::kMaxVoices; ++i) {
            m.add(s1, 1.0F);
        }
        expect("zweiunddreissig Kanaele sind voll",
               m.active() == bhed::sound::Mixer::kMaxVoices);
        // Ein LEISERER kommt nicht mehr hinein - alle laufenden sind lauter.
        m.add(s1, 0.2F);
        expect("ein leiserer verdraengt niemanden",
               m.active() == bhed::sound::Mixer::kMaxVoices);
        // Aber die Zahl bleibt gedeckelt, egal wie oft man nachlegt.
        for (int i = 0; i < 10; ++i) {
            m.add(s1, 1.0F);
        }
        expect("und die Zahl bleibt gedeckelt",
               m.active() == bhed::sound::Mixer::kMaxVoices);
    }

    // --- Raumlaut nach S_SpatializeOrigin (snd_dma.cpp:1401) ------------
    {
        using bhed::sound::Kanal;
        const float ohr[3] = {0.0F, 0.0F, 0.0F};
        const float rechts[3] = {0.0F, -1.0F, 0.0F};   // Blick +x, rechts = -y
        const float vorn[3] = {100.0F, 0.0F, 0.0F};
        const auto a = bhed::sound::raumlaut(vorn, ohr, rechts, Kanal::Auto);
        expect("Raumlaut: vorn nah je Ohr die Haelfte",
               std::fabs(a.links - 0.5F) < 1e-3F && std::fabs(a.rechts - 0.5F) < 1e-3F);
        const float seite[3] = {0.0F, -100.0F, 0.0F};
        const auto b = bhed::sound::raumlaut(seite, ohr, rechts, Kanal::Auto);
        expect("Raumlaut: ganz rechts nur rechts, dort voll",
               std::fabs(b.rechts - 1.0F) < 1e-3F && b.links < 1e-3F);
        const float weit[3] = {1000.0F, 0.0F, 0.0F};
        const auto c = bhed::sound::raumlaut(weit, ohr, rechts, Kanal::Voice);
        // (1000 - 768) * 0.0008 = 0.1856 -> 0.5 * 0.8144
        expect("Raumlaut: CHAN_VOICE voll bis 768, dann 0.0008 je Einheit",
               std::fabs(c.links - 0.4072F) < 1e-3F);
        const auto d = bhed::sound::raumlaut(weit, ohr, rechts, Kanal::VoiceAtten);
        // (1000 - 345.6) * 0.004 > 1 -> stumm
        expect("Raumlaut: CHAN_VOICE_ATTEN ist auf 1000 Einheiten stumm", d.links < 1e-3F && d.rechts < 1e-3F);
        const auto g = bhed::sound::raumlaut(weit, ohr, rechts, Kanal::VoiceGlobal);
        expect("Raumlaut: CHAN_VOICE_GLOBAL immer voll", g.links == 1.0F && g.rechts == 1.0F);
        expect("Kanalname CHAN_VOICE_ATTEN erkannt",
               bhed::sound::kanalAusName("chan_voice_atten") == Kanal::VoiceAtten);
    }
    // --- Channel-Stomp: der naechste Satz schneidet den vorigen ab --------
    {
        bhed::sound::Mixer m;
        const std::vector<int16_t> s(1000, 1000);
        m.add(s, 1.0F, 1.0F, 42);
        m.add(s, 1.0F, 1.0F, 42);
        m.add(s, 1.0F, 1.0F, 0);
        m.add(s, 1.0F, 1.0F, 0);
        expect("Stomp: gleicher Schluessel ersetzt, Schluessel 0 nie", m.active() == 3);
        std::vector<int16_t> out;
        bhed::sound::Mixer m2;
        m2.add(s, 0.0F, 1.0F, 0);
        m2.mixInto(out, 10, 2);
        expect("Stereo: links 0, rechts voll", out[0] == 0 && out[1] == 1000);
    }

    std::printf("\n%s (%d Fehlschlaege)\n",
                fails != 0 ? "FEHLGESCHLAGEN" : "alle Klangproben bestanden", fails);
    return fails != 0 ? 1 : 0;
}
