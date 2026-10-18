// soundtest.cpp - Klangleser
//
// Das Abspielen laesst sich hier nicht pruefen (kein Tongeraet), das LESEN
// dagegen vollstaendig. Die Proben bauen die Dateien selbst, damit der Test
// ohne Beigaben laeuft.
#include "bhed/sound.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <memory>
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

    // =====================================================================
    // rc601 ton: Raumklang wie die Engine (S_Respatialize, S_AddLoopSounds)
    // =====================================================================
    //
    // Gemeldet: "You can hear everything on the map, including all the
    // invisible NPCs sabers". Die Formel von S_SpatializeOrigin stimmte schon
    // (oben); falsch waren der Hoerer, das Einfrieren der Pegel beim Start,
    // die Mischpegel und das Zusammenlegen gleicher Schleifen.

    // --- Der Rechtsvektor mit Roll (AngleVectors, q_math.c:1314) ----------
    {
        using bhed::sound::Kanal;
        float r[3];
        const float gier90[3] = {0.0F, 90.0F, 0.0F};
        bhed::sound::rechtsAusWinkeln(gier90, r);
        expect("rechtsAusWinkeln: Gier 90 -> rechts ist +x",
               near(r[0], 1.0F, 1e-5F) && near(r[1], 0.0F, 1e-5F) && near(r[2], 0.0F, 1e-5F));
        const float gier0[3] = {0.0F, 0.0F, 0.0F};
        bhed::sound::rechtsAusWinkeln(gier0, r);
        expect("rechtsAusWinkeln: Gier 0 -> rechts ist -y",
               near(r[0], 0.0F, 1e-5F) && near(r[1], -1.0F, 1e-5F) && near(r[2], 0.0F, 1e-5F));
        // Nick 90 (Blick senkrecht nach unten) und Roll 90:
        // (-sr*sp*cy + cr*sy, -sr*sp*sy - cr*cy, -sr*cp) = (-1, 0, 0)
        const float schraeg[3] = {90.0F, 0.0F, 90.0F};
        bhed::sound::rechtsAusWinkeln(schraeg, r);
        expect("rechtsAusWinkeln: Nick 90 und Roll 90 nach AngleVectors",
               near(r[0], -1.0F, 1e-5F) && near(r[1], 0.0F, 1e-5F) && near(r[2], 0.0F, 1e-5F));
        // Roll 180 (Kamera auf dem Kopf) vertauscht die Ohren - mit dem
        // alten Rechtsvektor nur aus der Gier blieb es gleich.
        const float ohr[3] = {0.0F, 0.0F, 0.0F};
        const float rechtsDa[3] = {0.0F, -100.0F, 0.0F};
        float r0[3];
        bhed::sound::rechtsAusWinkeln(gier0, r0);
        const auto a = bhed::sound::raumlaut(rechtsDa, ohr, r0, Kanal::Auto);
        const float kopf[3] = {0.0F, 0.0F, 180.0F};
        float r180[3];
        bhed::sound::rechtsAusWinkeln(kopf, r180);
        const auto b = bhed::sound::raumlaut(rechtsDa, ohr, r180, Kanal::Auto);
        expect("Roll 180 vertauscht links und rechts",
               near(a.rechts, 1.0F, 1e-3F) && a.links < 1e-3F && near(b.links, 1.0F, 1e-3F) && b.rechts < 1e-3F);
    }

    // --- Wie weit ein Kanal traegt (raumlaut erreicht 0) ---------------------
    {
        using bhed::sound::Kanal;
        expect("hoerweite CHAN_AUTO 1506 (256 + 1/0.0008)", near(bhed::sound::hoerweite(Kanal::Auto), 1506.0F, 0.01F));
        expect("hoerweite CHAN_BODY wie CHAN_AUTO", near(bhed::sound::hoerweite(Kanal::Body), 1506.0F, 0.01F));
        expect("hoerweite CHAN_VOICE 2018", near(bhed::sound::hoerweite(Kanal::Voice), 2018.0F, 0.01F));
        expect("hoerweite CHAN_LESS_ATTEN 2530", near(bhed::sound::hoerweite(Kanal::LessAtten), 2530.0F, 0.01F));
        expect("hoerweite CHAN_VOICE_ATTEN 595.6", near(bhed::sound::hoerweite(Kanal::VoiceAtten), 595.6F, 0.01F));
        expect("hoerweite CHAN_VOICE_GLOBAL und CHAN_ANNOUNCER unbegrenzt",
               std::isinf(bhed::sound::hoerweite(Kanal::VoiceGlobal)) &&
                   std::isinf(bhed::sound::hoerweite(Kanal::Announcer)));
        const float ohr[3] = {0.0F, 0.0F, 0.0F};
        const float rr[3] = {0.0F, -1.0F, 0.0F};
        bool passt = true;
        for (const Kanal k : {Kanal::Auto, Kanal::Voice, Kanal::LessAtten, Kanal::VoiceAtten}) {
            const float w = bhed::sound::hoerweite(k);
            const float innen[3] = {w - 1.0F, 0.0F, 0.0F};
            const float aussen[3] = {w + 1.0F, 0.0F, 0.0F};
            const auto i = bhed::sound::raumlaut(innen, ohr, rr, k);
            const auto o = bhed::sound::raumlaut(aussen, ohr, rr, k);
            if (!(std::max(i.links, i.rechts) > 0.0F && std::max(o.links, o.rechts) == 0.0F)) { passt = false; }
        }
        expect("hoerweite: knapp davor noch hoerbar, knapp dahinter stumm (alle Kanaele)", passt);
    }

    // --- Mischpegel: s_volume, s_volumeVoice, SOUND_FMAXVOL -------------------
    {
        using bhed::sound::Kanal;
        expect("kanalPegel: CHAN_AUTO mal s_volume 0.5", near(bhed::sound::kanalPegel(Kanal::Auto, false), 0.5F, 1e-6F));
        expect("kanalPegel: CHAN_VOICE mal s_volumeVoice 1.0",
               near(bhed::sound::kanalPegel(Kanal::Voice, false), 1.0F, 1e-6F));
        expect("kanalPegel: CHAN_VOICE_ATTEN und CHAN_VOICE_GLOBAL sind Stimmen",
               near(bhed::sound::kanalPegel(Kanal::VoiceAtten, false), 1.0F, 1e-6F) &&
                   near(bhed::sound::kanalPegel(Kanal::VoiceGlobal, false), 1.0F, 1e-6F));
        expect("kanalPegel: CHAN_BODY und CHAN_LESS_ATTEN wie CHAN_AUTO",
               near(bhed::sound::kanalPegel(Kanal::Body, false), 0.5F, 1e-6F) &&
                   near(bhed::sound::kanalPegel(Kanal::LessAtten, false), 0.5F, 1e-6F));
        expect("kanalPegel: CHAN_AUTO am Hoerer 0.75 * 0.5", near(bhed::sound::kanalPegel(Kanal::Auto, true), 0.375F, 1e-6F));
        expect("kanalPegel: CHAN_VOICE am Hoerer 0.75", near(bhed::sound::kanalPegel(Kanal::Voice, true), 0.75F, 1e-6F));
        expect("kanalPegel: ab CHAN_AMBIENT ohne SOUND_FMAXVOL",
               near(bhed::sound::kanalPegel(Kanal::Ambient, true), 0.5F, 1e-6F));
        bhed::sound::Mischpegel p;
        expect("Mischpegel: Vorgaben 0.5 / 1.0 / 0.25 (s_volume, s_volumeVoice, s_musicvolume)",
               near(p.effekte, 0.5F, 1e-6F) && near(p.stimmen, 1.0F, 1e-6F) && near(p.musik, 0.25F, 1e-6F));
        p.effekte = 0.8F;
        p.stimmen = 0.6F;
        expect("kanalPegel: eigene Regler wirken",
               near(bhed::sound::kanalPegel(Kanal::Auto, false, p), 0.8F, 1e-6F) &&
                   near(bhed::sound::kanalPegel(Kanal::Voice, false, p), 0.6F, 1e-6F));
    }

    // --- Gleiche Schleifen: je Ohr summiert, bei SOUND_MAXVOL gekappt ---------
    {
        using bhed::sound::Raumlaut;
        const auto drei = bhed::sound::schleifenSumme({Raumlaut{0.5F, 0.5F}, Raumlaut{0.5F, 0.5F}, Raumlaut{0.5F, 0.5F}});
        expect("schleifenSumme: dreimal 0.5 wird 1 (gekappt)", near(drei.links, 1.0F, 1e-6F) && near(drei.rechts, 1.0F, 1e-6F));
        const auto zwei = bhed::sound::schleifenSumme({Raumlaut{0.2F, 0.1F}, Raumlaut{0.3F, 0.0F}});
        expect("schleifenSumme: je Ohr fuer sich", near(zwei.links, 0.5F, 1e-6F) && near(zwei.rechts, 0.1F, 1e-6F));
        const auto keine = bhed::sound::schleifenSumme({});
        expect("schleifenSumme: ohne Quelle Stille", keine.links == 0.0F && keine.rechts == 0.0F);
        // (int)(255 * 0.003) = 0 auf beiden Ohren -> stumm; 255 * 0.004 = 1 nicht.
        expect("engineStumm: unter einer Stufe von 255 auf beiden Ohren",
               bhed::sound::engineStumm(Raumlaut{0.003F, 0.003F}) && !bhed::sound::engineStumm(Raumlaut{0.004F, 0.0F}));
        // Am Hoerer ist master_vol nur 255 * 0.75: 0.005 * 191.25 = 0.96 -> 0.
        expect("engineStumm: am Hoerer mit master_vol 255 * 0.75",
               bhed::sound::engineStumm(Raumlaut{0.005F, 0.0F}, true) &&
                   !bhed::sound::engineStumm(Raumlaut{0.005F, 0.0F}, false));
    }

    // --- Der Mischer: Stimmen mit Nummer, je Bild nachregelbar ----------------
    {
        using Mixer = bhed::sound::Mixer;
        auto d = std::make_shared<const std::vector<int16_t>>(std::vector<int16_t>(1000, 1000));
        Mixer m;
        Mixer::Start st;
        const std::uint64_t id = m.starte(d, st);
        expect("starte liefert eine Nummer", id != 0U && m.laeuft(id));
        expect("setzePegel trifft die laufende Stimme", m.setzePegel(id, 0.25F, 0.75F));
        std::vector<int16_t> out;
        m.mixInto(out, 10, 2);
        expect("setzePegel wirkt sofort (Stereo 250/750)", out[0] == 250 && out[1] == 750);
        (void)m.setzePegel(id, 0.0F, 0.0F);
        m.mixInto(out, 10, 2);
        expect("eine auf 0 geregelte Stimme bleibt laufen (still)", out[0] == 0 && m.laeuft(id) && m.active() == 1U);
        m.stoppe(id);
        expect("stoppe beendet genau diese Stimme", !m.laeuft(id) && m.active() == 0U);
        expect("setzePegel auf eine fertige Nummer meldet false", !m.setzePegel(id, 1.0F, 1.0F));
    }
    {
        using Mixer = bhed::sound::Mixer;
        auto d = std::make_shared<const std::vector<int16_t>>(std::vector<int16_t>(1000, 1000));
        Mixer m;
        Mixer::Start st;
        st.links = 0.0F;
        st.rechts = 0.0F;
        expect("eine stumme Stimme ohne Erlaubnis startet nicht (wie add)", m.starte(d, st) == 0U && m.active() == 0U);
        st.stummErlaubt = true;   // Stimmkanaele: S_Respatialize laesst sie laufen
        const std::uint64_t id = m.starte(d, st);
        expect("mit Erlaubnis laeuft sie still", id != 0U && m.active() == 1U);
        (void)m.setzePegel(id, 1.0F, 1.0F);
        std::vector<int16_t> out;
        m.mixInto(out, 10, 2);
        expect("... und wird hoerbar, sobald der Pegel steigt", out[0] == 1000 && out[1] == 1000);
    }
    {
        // Schleifen laufen ueber das Ende hinaus - ohne den vorab auf
        // 120 s ausgerollten Puffer (21 MB je Schleife).
        using Mixer = bhed::sound::Mixer;
        auto zehn = std::make_shared<const std::vector<int16_t>>(std::vector<int16_t>(20, 1000));   // 10 Bilder
        Mixer m;
        Mixer::Start st;
        st.schleife = true;
        const std::uint64_t id = m.starte(zehn, st);
        std::vector<int16_t> out;
        m.mixInto(out, 25, 2);
        expect("eine Schleife von 10 Bildern liefert nach 25 Bildern noch Klang",
               out.size() == 50U && out[48] == 1000 && out[49] == 1000 && m.laeuft(id));
        Mixer e;
        (void)e.starte(zehn, Mixer::Start{});
        e.mixInto(out, 25, 2);
        expect("ein Einmalklang derselben Laenge ist danach fertig", out[48] == 0 && e.active() == 0U);
    }
    {
        // Alle Schleifen laufen im selben Takt: S_PaintChannels nimmt
        // ltime % iSoundLengthInSamples (snd_mix.cpp:332 MD).
        using Mixer = bhed::sound::Mixer;
        std::vector<int16_t> rampe(20);
        for (std::size_t i = 0; i < rampe.size(); ++i) { rampe[i] = static_cast<int16_t>((i / 2U) * 100U); }
        auto dRampe = std::make_shared<const std::vector<int16_t>>(rampe);
        auto still = std::make_shared<const std::vector<int16_t>>(std::vector<int16_t>(100, 0));
        Mixer m;
        (void)m.starte(still, Mixer::Start{});
        std::vector<int16_t> out;
        m.mixInto(out, 3, 2);
        Mixer::Start st;
        st.schleife = true;
        (void)m.starte(dRampe, st);
        m.mixInto(out, 1, 2);
        expect("eine neue Schleife beginnt im gemeinsamen Takt (Bild 3 nach 3 gemischten)", out[0] == 300 && out[1] == 300);
    }
    {
        // Channel-Stomp auch ueber starte: gleicher Schluessel ersetzt.
        using Mixer = bhed::sound::Mixer;
        auto d = std::make_shared<const std::vector<int16_t>>(std::vector<int16_t>(1000, 1000));
        Mixer m;
        Mixer::Start st;
        st.schluessel = 77;
        const std::uint64_t a = m.starte(d, st);
        const std::uint64_t b = m.starte(d, st);
        expect("starte: gleicher Schluessel schneidet ab", !m.laeuft(a) && m.laeuft(b) && m.active() == 1U);
    }
    {
        // Verdraengen wie S_PickChannel (snd_dma.cpp:1138 ff. MD): der
        // AELTESTE weicht - aber keine Schleife und nichts am Hoerer.
        using Mixer = bhed::sound::Mixer;
        auto d = std::make_shared<const std::vector<int16_t>>(std::vector<int16_t>(1000, 1000));
        Mixer m;
        Mixer::Start hoerer;
        hoerer.amHoerer = true;
        Mixer::Start schleife;
        schleife.schleife = true;
        const std::uint64_t h = m.starte(d, hoerer);
        const std::uint64_t s = m.starte(d, schleife);
        std::vector<std::uint64_t> rest;
        while (m.active() < Mixer::kMaxVoices) { rest.push_back(m.starte(d, Mixer::Start{})); }
        const std::uint64_t neu = m.starte(d, Mixer::Start{});
        expect("voller Mischer: der aelteste gewoehnliche Klang weicht",
               neu != 0U && !rest.empty() && !m.laeuft(rest.front()) && m.laeuft(h) && m.laeuft(s) &&
                   m.active() == Mixer::kMaxVoices);
    }

    std::printf("\n%s (%d Fehlschlaege)\n",
                fails != 0 ? "FEHLGESCHLAGEN" : "alle Klangproben bestanden", fails);
    return fails != 0 ? 1 : 0;
}
