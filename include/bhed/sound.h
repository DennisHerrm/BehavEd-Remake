// Klaenge lesen.
//
// JKA spielt `.wav` und `.mp3`. Der Fehler, den du am Original gefunden hast,
// liegt genau hier: EffectsEd importiert nur `PlaySoundA`, und das kann kein
// MP3 - deshalb bleibt jeder Effekt mit einem MP3 im Editor stumm, obwohl er
// im Spiel klingt.
//
// Ergebnis ist immer 16-Bit-PCM, egal was in der Datei stand. Die Ausgabe
// bekommt damit ein Format statt fuenf Sonderfaelle.
//
// **WAV lesen wir selbst** - das Format ist ein paar Dutzend Zeilen und laesst
// sich vollstaendig pruefen.
//
// **Fuer MP3 liegt `third_party/minimp3.h` bei.** Das ist die erste fremde
// Datei im Baum, und die Entscheidung fiel nicht leicht: bis hierhin hatte das
// Projekt keine einzige Abhaengigkeit, und Deflate und JPEG habe ich selbst
// geschrieben.
//
// Ein MP3-Decoder ist aber eine andere Groessenordnung - Huffman, IMDCT,
// Polyphasen-Synthesebank, Bit-Reservoir. Rund zweitausend Zeilen, bei denen
// ein Fehler nicht als falsche Farbe auffaellt, sondern als Rauschen, das
// vielleicht nur bei bestimmten Bitraten auftritt. minimp3 ist gemeinfrei
// (CC0, keine Auflagen), eine einzige Datei ohne eigene Abhaengigkeiten, und
// ISO-konform geprueft.
//
// Uebernommen aus efxed, unveraendert bis auf den Namensraum. Die beiden
// Erkenntnisse in den Kommentaren unten sind dort im Betrieb erarbeitet
// worden und gelten hier genauso.
#ifndef BHED_SOUND_H
#define BHED_SOUND_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bhed::sound {

struct Sound {
    int sampleRate = 0;
    int channels = 0;
    std::vector<int16_t> samples;  // verschraenkt: L R L R ...
    bool ok = false;
    std::string error;

    // Der groesste Abtastwert der ganzen Datei, um 8 nach rechts geschoben.
    //
    // Die Engine bildet ihn beim Einlesen (snd_mem.cpp:246 ff.):
    //
    //     if (iSample < 0) iSample = -iSample;
    //     if (sfx->fVolRange < (iSample >> 8)) sfx->fVolRange = iSample >> 8;
    //
    // Er ist der MASSSTAB fuer die Mundanimation - die Schwellen sind
    // Vielfache davon, nicht feste Zahlen. Ein leise aufgenommener Satz
    // bewegt den Mund also genauso weit wie ein lauter.
    int volRange = 0;

    // Dauer in Millisekunden. Fuer die Vorschau: ein Klang, der laenger ist als
    // der Effekt, soll nicht mitten im naechsten Durchlauf weiterlaufen.
    float durationMs() const;
    size_t frameCount() const;
};

enum class Format { Unknown, Wav, Mp3 };
Format sniff(const unsigned char* data, size_t size);

// Erkennt das Format am Inhalt, nicht an der Endung - wie bei den Bildern.
Sound decode(const unsigned char* data, size_t size);

// Der Pegel fuer die Mundanimation an einer Stelle im Klang.
//
// Gemeldet: "face animationen fehlen noch."
//
// Der Mund bewegt sich in JKA NICHT durch die Koerperanimation. Er ist ein
// eigener Animationslauf auf dem Knochen "face" (g_client.cpp:1653,
// cg_players.cpp:5132), und WELCHE Animation gespielt wird, entscheidet die
// LAUTSTAERKE des gerade laufenden Klangs (cg_players.cpp:5191):
//
//     anim = FACE_TALK1 + gi.VoiceVolume[...] - 1;
//
// VoiceVolume entsteht in snd_dma.cpp:2333 ff.: von der Abspielstelle aus
// zehn Abtastwerte im Abstand von 100 nehmen, jeden um 8 nach rechts
// schieben, quadrieren, mitteln - und gegen volRange * {0.3, 4, 6, 8}
// halten (die Vorgaben von s_threshold1..4, snd_dma.cpp:456 ff.).
//
// Rueckgabe:
//    0  spricht nicht (ueber das Ende hinaus oder davor)
//   -1  spricht, ist aber gerade still  -> FACE_TALK0
//    1..4  die Stufen                   -> FACE_TALK1..4
[[nodiscard]] int voiceLevel(const Sound& s, double posMs);

// PCM in 8, 16, 24 und 32 Bit sowie 32-Bit-Gleitkomma. Alles andere
// (komprimierte WAVs wie ADPCM) wird abgewiesen und beim Namen genannt.
Sound decodeWav(const unsigned char* data, size_t size);

Sound decodeMp3(const unsigned char* data, size_t size);

// Einen Klang auf das Format des Tongeraets bringen: Abtastrate und Kanaele.
//
// Warum das noetig ist - der Fall aus dem Betrieb: eine MP3 mit 44100 Hz wurde
// abgespielt, eine WAV mit 11025 Hz nicht. Beide wurden fehlerfrei gelesen,
// beide hatten Werte, die Ausloesung stimmte bei beiden.
//
// Der Unterschied lag im Tongeraet. Wir haben es mit der *genauen* Rate der
// Datei geoeffnet, also mal mit 44100, mal mit 11025. Windows mischt heute
// intern mit einer festen Rate; `waveOut` ist nur noch nachgebildet, und
// ungewoehnliche Raten lehnt es je nach Treiber schlicht ab. Bei einer
// 44100er-Datei faellt das nie auf, weil es die Rate des Geraets ist.
//
// Ein Tongeraet, das jedes Mal neu geoeffnet wird, ist ohnehin die falsche
// Bauform: zwei Klaenge mit verschiedenen Raten haetten sich gegenseitig
// abgewuergt. Ein festes Format, in das alles umgerechnet wird, loest beides.
//
// Lineare Zwischenwerte. Fuer einen Effekteditor genuegt das: die Klaenge sind
// kurze Aufschlaege, keine Musik, und der Unterschied zu einem aufwendigen
// Filter ist an einem Explosionsgeraeusch nicht hoerbar.
std::vector<int16_t> convert(const std::vector<int16_t>& samples,
                             int sourceRate, int sourceChannels,
                             int targetRate, int targetChannels);

// --- Wie laut ist ein Klang aus dieser Entfernung? ---------------------
//
// Gemeldet: "die Effektsounds sind so penetrant und spielen dauerhaft.
// Sind das vielleicht 3D-Sounds, die nur abspielen, wenn man in der Naehe
// ist?"
//
// Ja. Die Engine spielt Effektklaenge AM ORT (FxScheduler.cpp:1884) und
// daempft sie nach Entfernung (snd_dma.cpp:1356 ff.):
//
//     dist -= SOUND_FULLVOLUME;   // 256
//     if (dist < 0) dist = 0;
//     dist *= SOUND_ATTENUATE;    // 0.0008
//     scale = 1.0 - dist;
//
// Also: voll bis 256 Einheiten, halb bei 881, **stumm ab 1506**.
[[nodiscard]] float distanceVolume(const float quelle[3],
                                   const float hoerer[3]);

// --- Der Kanal eines Klangs (channels.h der Engine) ----------------------
//
// Ein Skript sagt sound ( CHAN_VOICE, "..." ). Der Kanal entscheidet zwei
// Dinge: wie der Klang mit der Entfernung leiser wird (S_SpatializeOrigin,
// snd_dma.cpp:1401) und ob er einen laufenden Klang derselben Entity
// abschneidet (S_CheckChannelStomp, snd_dma.cpp:1110 - alle drei
// Stimmkanaele gelten dabei als einer).
enum class Kanal : std::uint8_t {
    Auto, Local, Weapon, Voice, VoiceAtten, VoiceGlobal, Item, Body, Ambient,
    LocalSound, Announcer, LessAtten, Music,
};
[[nodiscard]] Kanal kanalAusName(const std::string& name);

// Links/rechts-Lautstaerke eines Klangs am Ort `quelle`, gehoert bei
// `hoerer` mit der Blickachse `rechts` (Einheitsvektor nach rechts).
//
// Nachgerechnet aus S_SpatializeOrigin mit s_separation 0.5 (Vorgabe,
// snd_dma.cpp:457):
//
//     CHAN_VOICE       voll bis 768 Einheiten, dann 0.0008 je Einheit
//     CHAN_VOICE_ATTEN voll bis 346, dann 0.004 je Einheit (schnell leise)
//     CHAN_LESS_ATTEN  voll bis 1280
//     sonst            voll bis 256
//     dot   = -DotProduct( listener_axis[1] (links), richtung )
//     rechts = 0.5 + 0.5 * dot,  links = 0.5 - 0.5 * dot
//     laut  = (1 - dist) * rechts/links
//
// Ein Klang GENAU VOR dem Hoerer hat also je Ohr die HALBE Lautstaerke;
// nur ein Klang ganz auf einer Seite erreicht dort die volle.
struct Raumlaut {
    float links = 1.0F;
    float rechts = 1.0F;
};
[[nodiscard]] Raumlaut raumlaut(const float quelle[3], const float hoerer[3],
                                const float hoererRechts[3], Kanal k);

// --- rc601 ton: was die Engine um raumlaut herum tut ----------------------
//
// Gemeldet (Tester, Movie Duels): "You can hear everything on the map,
// including all the invisible NPCs sabers". raumlaut selbst stimmte; falsch
// waren der Hoerer, das Einfrieren beim Start, die Mischpegel und das
// Zusammenlegen gleicher Schleifen. Die Bausteine dafuer stehen hier, damit
// sie sich ohne Tongeraet pruefen lassen (tests/soundtest.cpp).

// Der Rechtsvektor der Kamera MIT Roll, wie AngleVectors (shared/qcommon/
// q_math.c:1291 in Movie Duels, :1314 in OpenJK):
//     right = (-sr*sp*cy + cr*sy, -sr*sp*sy - cr*cy, -sr*cp)
// Die Engine hoert mit listener_axis[1] = -right (AnglesToAxis, q_math.c:997 MD,
// aus cg.refdef.viewaxis, cg_view.cpp:3307 MD); raumlaut erwartet den
// Rechtsvektor selbst. Vorher kam er nur aus der Gier - eine gerollte
// Skriptkamera vertauschte dann die Ohren nicht.
void rechtsAusWinkeln(const float winkel[3], float rechts[3]);

// Ab welcher Entfernung ein Kanal stumm ist (raumlaut erreicht 0), aus
// S_SpatializeOrigin (Movie Duels code/client/snd_dma.cpp:1402 ff.):
//     sonst            256 + 1/0.0008 = 1506
//     CHAN_VOICE       768 + 1250     = 2018
//     CHAN_LESS_ATTEN  1280 + 1250    = 2530
//     CHAN_VOICE_ATTEN 345.6 + 250    = 595.6
//     CHAN_VOICE_GLOBAL, CHAN_ANNOUNCER: nie (unendlich)
[[nodiscard]] float hoerweite(Kanal k);

// Die Mischpegel der Engine. Vorgaben der Cvars (snd_dma.cpp:455 ff. MD,
// gleich in OpenJK snd_dma.cpp:462 ff.): s_volume 0.5, s_volumeVoice 1.0,
// s_musicvolume 0.25. S_PaintChannels nimmt fuer die drei Stimmkanaele
// voice_vol, fuer alles andere normal_vol (snd_mix.cpp:263 ff. MD); die Musik
// geht mit s_musicvolume in S_RawSamples (snd_dma.cpp:2175 ff. MD).
struct Mischpegel {
    float effekte = 0.5F;   // s_volume
    float stimmen = 1.0F;   // s_volumeVoice
    float musik = 0.25F;    // s_musicvolume
};
// SOUND_FMAXVOL (snd_dma.cpp:152 MD): ein Klang AUF der Hoerer-Entity wird
// nicht gedaempft, also mit master_vol 255 * 0.75 leiser gestellt
// (S_StartSound, snd_dma.cpp:1705 ff. MD) - nur fuer Kanaele unter
// CHAN_AMBIENT.
inline constexpr float kSoundFMaxVol = 0.75F;

// CHAN_VOICE, CHAN_VOICE_ATTEN, CHAN_VOICE_GLOBAL: s_volumeVoice, und sie
// laufen bei Pegel 0 weiter (S_Respatialize, snd_dma.cpp:2721 ff. MD).
[[nodiscard]] bool istStimmkanal(Kanal k);

// Der Faktor, mit dem ein Kanal in den Mischer geht: s_volumeVoice oder
// s_volume, auf der Hoerer-Entity (k < CHAN_AMBIENT) zusaetzlich
// SOUND_FMAXVOL.
[[nodiscard]] float kanalPegel(Kanal k, bool amHoerer, const Mischpegel& p = {});

// S_AddLoopSounds (snd_dma.cpp:2082 ff. MD): alle Schleifen DERSELBEN Datei
// werden ein Kanal - je Ohr die Beitraege summiert, jedes Ohr bei
// SOUND_MAXVOL (hier 1) gekappt. "Multiple identical looping sounds will just
// increase volume without any speed cost" (target_speaker, g_target.cpp:190 MD).
[[nodiscard]] Raumlaut schleifenSumme(const std::vector<Raumlaut>& beitraege);

// Ist ein Kanal fuer die Engine stumm? S_SpatializeOrigin rechnet die
// Lautstaerke als GANZE Zahl, leftvol = (int)(master_vol * scale), mit
// master_vol 255 bzw. auf der Hoerer-Entity 255 * 0.75 (snd_dma.cpp:1466 ff.,
// 1695 ff. MD); S_Respatialize loescht einen Nicht-Stimmkanal, sobald beide
// Ohren 0 sind (snd_dma.cpp:2721 ff. MD).
[[nodiscard]] bool engineStumm(const Raumlaut& r, bool amHoerer = false);

// --- Mehrere Klaenge GLEICHZEITIG ----------------------------------------
//
// Gemeldet: "als wuerden Sounds aufeinander warten beim Abspielen."
//
// Genau das taten sie. behaved gab jeden Klang mit einem eigenen
// `waveOutWrite` an dasselbe Geraet - und waveOut MISCHT NICHT, es spielt
// die geschriebenen Puffer der Reihe nach ab. Klang zwei begann also erst,
// wenn Klang eins zu Ende war.
//
// Die Engine macht es anders (snd_mix.cpp:265, S_PaintChannels): sie mischt
// bis zu 32 Kanaele in EINEN Puffer, Sample fuer Sample aufaddiert.
//
// Dieser Mischer tut dasselbe. Er steht hier und nicht im Windows-Teil,
// damit er PRUEFBAR ist: die Tonausgabe kann ich auf dem Rechner, auf dem
// dieser Quelltext entsteht, nicht hoeren - das Mischen sehr wohl
// nachrechnen.
class Mixer {
public:
    // Einen Klang dazunehmen. `volume` ist 0..1 (die Entfernungsdaempfung
    // aus distanceVolume). Laeuft schon die Hoechstzahl, wird der LEISESTE
    // verdraengt - so wie die Engine es tut (S_PickChannel waehlt den
    // Kanal, der am naechsten am Ende ist).
    void add(const std::vector<int16_t>& samples, float volume);
    // Mit getrennter Lautstaerke je Ohr (Raumlaut) und einem Schluessel
    // fuer das Abschneiden: ein neuer Klang mit demselben Schluessel (nicht
    // 0) beendet den laufenden - so schneidet der naechste Satz einer
    // Figur den vorigen ab (S_PickChannel mit S_CheckChannelStomp).
    void add(const std::vector<int16_t>& samples, float links, float rechts,
             std::uint64_t schluessel);

    // Den naechsten Block mischen. Fertige Klaenge fallen dabei heraus.
    // Der Rueckgabewert ist die Zahl der noch laufenden Klaenge.
    std::size_t mixInto(std::vector<int16_t>& out, std::size_t frames,
                        int channels);

    [[nodiscard]] std::size_t active() const { return voices_.size(); }
    void clear() { voices_.clear(); }

    // 32 wie MAX_CHANNELS in snd_local.h:189.
    static constexpr std::size_t kMaxVoices = 32;

    // --- rc601 ton: Stimmen mit Nummer, je Bild nachregelbar ---------------
    //
    // S_Respatialize (Movie Duels snd_dma.cpp:2571 ff.) rechnet JEDEN
    // laufenden Kanal in jedem Bild neu - der Hoerer bewegt sich, die Quelle
    // auch. behaved rechnete die Lautstaerke einmal beim Start und nie
    // wieder; ein Summen, das nah begann, blieb zwei Minuten so laut. Dafuer
    // braucht der Mischer: eine Nummer je Stimme, den Pegel nachtraeglich,
    // einzeln anhalten, und Schleifen ohne vorab ausgerollten Puffer (vorher
    // 120 s, rund 21 MB je Schleife, beim Start noch einmal kopiert).
    using Daten = std::shared_ptr<const std::vector<int16_t>>;
    struct Start {
        float links = 1.0F;
        float rechts = 1.0F;
        // Channel-Stomp wie add(): ein laufender Klang mit demselben
        // Schluessel (nicht 0) endet sofort.
        std::uint64_t schluessel = 0;
        // Einstieg im Klang, in Werten (verschraenkt, auf Bildgrenze).
        std::size_t abWert = 0;
        // Am Ende wieder vorn - und zwar im GEMEINSAMEN Takt aller
        // Schleifen: S_PaintChannels liest ltime % iSoundLengthInSamples
        // (snd_mix.cpp:332 MD). Eine Schleife, die neu einen Kanal bekommt,
        // setzt also dort ein, wo sie ohnehin gerade waere.
        bool schleife = false;
        // Auch bei Pegel 0 starten (Stimmkanaele laufen still weiter).
        bool stummErlaubt = false;
        // Ein Klang auf der Hoerer-Entity: S_PickChannel verdraengt ihn nur
        // fuer einen anderen Klang am Hoerer (snd_dma.cpp:1186 MD).
        bool amHoerer = false;
    };
    // Liefert die Nummer der Stimme, 0 wenn nicht gestartet (leer, stumm
    // ohne Erlaubnis, oder alle 32 belegt mit nichts Verdraengbarem). Ist
    // der Mischer voll, weicht wie in S_PickChannel der AELTESTE Kanal -
    // keine Schleife, und nichts am Hoerer.
    std::uint64_t starte(Daten daten, const Start& s);
    // false, wenn die Stimme schon fertig ist.
    bool setzePegel(std::uint64_t id, float links, float rechts);
    void stoppe(std::uint64_t id);
    [[nodiscard]] bool laeuft(std::uint64_t id) const;

private:
    struct Voice {
        // rc601 ton: geteilt statt kopiert - eine Schleife je Datei liest
        // denselben Puffer, ohne ihn bei jedem Start zu kopieren.
        Daten daten;
        std::size_t pos = 0;
        float volume = 1.0F;       // die lautere Seite, fuer das Verdraengen
        float links = 1.0F;
        float rechts = 1.0F;
        std::uint64_t schluessel = 0;
        std::uint64_t id = 0;      // rc601 ton: Nummer fuer setzePegel/stoppe
        std::uint64_t nummer = 0;  // Startreihenfolge (startSample der Engine)
        bool schleife = false;
        bool amHoerer = false;
    };
    std::vector<Voice> voices_;
    std::uint64_t naechsteId_ = 1;
    std::uint64_t gemischt_ = 0;   // gemischte Werte seit dem Anfang
    int kanaele_ = 2;              // aus dem letzten mixInto
};

}  // namespace bhed::sound
#endif
