// Klangausgabe ueber waveOut.
//
// Bewusst die alte, einfache Windows-Schnittstelle statt WASAPI oder einer
// Bibliothek: sie ist seit Windows 3.1 unveraendert, braucht kein COM, keine
// Initialisierung und keine zusaetzliche Datei. Fuer ein paar Effektklaenge
// gleichzeitig reicht sie vollkommen.
//
// **Achtung: dieser Teil ist ungeprueft.** Alles andere in diesem Projekt
// wurde gegen Referenzdaten gemessen, bevor es ausgeliefert wurde. Hier geht
// das nicht - es gibt keine Tonausgabe auf dem Rechner, auf dem der Quelltext
// entsteht. Der Klangleser (`bhed::sound`) ist vollstaendig geprueft; nur das
// Abspielen ist es nicht.
#pragma once

#include "bhed/sound.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace bhed::gui {

class Audio {
public:
    // Alle fuenf Sonderfunktionen ausdruecklich - die "Rule of Five".
    //
    // `Buffer` ist hier nur angekuendigt, nicht ausdefiniert (er braucht
    // `windows.h`, das nicht in diesen Kopf gehoert). Ein `unique_ptr` auf
    // einen unvollstaendigen Typ ist erlaubt, aber sein Aufraeumen muss dort
    // stehen, wo der Typ vollstaendig ist - sonst schlaegt der Uebersetzer bei
    // jedem zu, der diesen Kopf einbindet.
    //
    // Kopieren ist verboten: ein Tongeraet gibt es nur einmal, und zwei
    // Halter, die dieselben Puffer freigeben, sind ein doppeltes Freigeben.
    // Verschieben ist erlaubt und wird in der .cpp erzeugt.
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    Audio(Audio&&) noexcept;
    Audio& operator=(Audio&&) noexcept;

    // Oeffnet ein Ausgabegeraet fuer dieses Format. Ein zweiter Aufruf mit
    // anderem Format schliesst das alte und oeffnet neu - waveOut kann keine
    // gemischten Formate auf einem Geraet.
    bool open(int sampleRate, int channels);
    void close();

    // Spielt einen Puffer ab. Kopiert ihn, weil waveOut asynchron liest und
    // der Aufrufer seinen Puffer sonst zu frueh freigeben koennte.
    // `volume` ist 0..1 - die Entfernungsdaempfung aus rc334.
    //
    // Der Klang wird MITGEMISCHT, nicht angehaengt. Vorher bekam jeder
    // einen eigenen waveOutWrite auf dasselbe Geraet, und waveOut spielt
    // solche Puffer der Reihe nach ab statt sie zu mischen - Klang zwei
    // begann erst, wenn Klang eins zu Ende war. Genau das war das
    // gemeldete "als wuerden Sounds aufeinander warten".
    bool play(const std::vector<int16_t>& samples, float volume = 1.0F);
    // Getrennt je Ohr und mit Kanalschluessel (siehe sound::Mixer::add).
    bool play(const std::vector<int16_t>& samples, float links, float rechts,
              std::uint64_t schluessel);

    // Fertige Puffer freigeben. Muss regelmaessig gerufen werden, sonst
    // sammeln sie sich an.
    void update();

    void stopAll();

    bool ready() const { return handle_ != nullptr; }
    const char* lastError() const { return error_; }
    int activeBuffers() const;

private:
    // --- Der Mischer -----------------------------------------------------
    //
    // Er steht in `bhed::sound`, nicht hier: dort ist er PRUEFBAR. Die
    // Tonausgabe kann ich auf dem Rechner, auf dem dieser Quelltext
    // entsteht, nicht hoeren - dass 1000 und 2000 zusammen 3000 ergeben,
    // sehr wohl nachrechnen. Die Proben stehen in soundtest.cpp.
    sound::Mixer mixer_;
    // Wie viele Bloecke im Voraus geschrieben werden.
    //
    // Bei 2048 Bildern und 44,1 kHz sind das rund 46 ms je Block.
    //
    // VIER, nicht zwei. Nachgefuellt wird einmal je Bild - und aus den
    // Messungen des Nutzers dauert ein Bild in dichten Szenen 50 bis 66 ms
    // (15 bis 20 Bilder je Sekunde). Zwei Bloecke sind 92 ms; bei einem
    // Bild von 66 ms bleiben nur 26 ms Luft, und ein einzelnes langsameres
    // Bild laesst die Ausgabe leerlaufen.
    //
    // Vier Bloecke sind 185 ms. Das haelt auch drei langsame Bilder
    // hintereinander aus und kostet dafuer knapp eine Zehntelsekunde
    // Verzoegerung, bis ein neuer Klang einsetzt - in einem
    // Vorschauwerkzeug nicht zu bemerken.
    static constexpr std::size_t kBlockFrames = 2048;
    static constexpr std::size_t kVorlauf = 4;

    void* handle_ = nullptr;   // HWAVEOUT
    int sampleRate_ = 0;
    int channels_ = 0;
    const char* error_ = "";

    struct Buffer;
    // Besitzende Zeiger gehoeren in einen Halter, nicht in einen rohen Zeiger.
    //
    // Vorher stand hier `std::vector<Buffer*>` mit einem `new` und vier
    // `delete` - davon zwei auf Fehlerpfaden. Vergisst man eines, ist es ein
    // Leck; schreibt man eines zu viel, ein doppeltes Freigeben.
    //
    // Mit `unique_ptr` verschwindet die Frage: der Halter gibt frei, wenn er
    // aus dem Bereich faellt, auch wenn dazwischen etwas fehlschlaegt.
    std::vector<std::unique_ptr<Buffer>> buffers_;
};

}  // namespace bhed::gui
