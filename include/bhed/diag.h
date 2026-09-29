// diag.h - Ablaufprotokoll
//
// Wozu das da ist: wenn das Programm auf einem fremden Rechner beim Start
// abstuerzt, gibt es keinen Debugger, keinen Stapelabzug und keine Person,
// die sagen kann, was sie getan hat. Es gibt nur eine Datei. Die letzte
// Zeile darin muss den Schritt benennen, der gerade lief.
//
// Uebernommen aus efxed, samt der Regeln, die dort aus echten Abstuerzen
// entstanden sind:
//
//  1. Nach jeder Zeile wird geleert. Ein gepuffertes Protokoll verliert
//     genau die Zeile, auf die es ankommt.
//  2. Marken sind fein. Lieber eine zu viel als eine, die drei Schritte
//     zusammenfasst.
//  3. Ein Schritt vermerkt seinen BEGINN, bevor er anfaengt - nicht sein
//     Ende, wenn er fertig ist. Ein Schritt ohne Abschlusszeile ist der
//     Schuldige.
//  4. Das vorige Protokoll wird beim Start beiseitegelegt, nicht
//     ueberschrieben. Sonst loescht der Neustartversuch nach dem Absturz
//     gerade den Beweis.
//
// Anlass hier: der erste Start scheiterte an einer ImGui-Zusicherung
// ("No current context"), und es gab nur den Meldungskasten der
// Laufzeitbibliothek - keine Datei, keine Spur.
#ifndef BHED_DIAG_H
#define BHED_DIAG_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bhed::diag {

enum class Level : std::uint8_t {
    Step,     // Ablaufmarke
    Info,
    Warning,
    Error,
};

// Oeffnet die Protokolldatei und legt ein vorhandenes Protokoll als
// "<name>.vorher" beiseite. Muss als Erstes im Programm laufen - vor allem,
// was abstuerzen koennte.
//
// false, wenn die Datei nicht geschrieben werden kann. Das ist kein Grund
// abzubrechen: das Protokoll laeuft dann nur im Speicher weiter.
bool open(const std::string& path);

// Schliesst die Datei und vermerkt einen ordentlichen Abschluss. Fehlt diese
// Zeile in einem Protokoll, ist das Programm nicht sauber beendet worden.
void close();

void write(Level level, const std::string& text);

// --- Das DETAILPROTOKOLL ------------------------------------------------
//
// Eine zweite Datei neben dem Ablaufprotokoll, "<name>-detail.log".
//
// Warum getrennt: das Ablaufprotokoll soll man LESEN koennen. Es hat rund
// fuenfzig Zeilen und beantwortet die Frage "wo ist es steckengeblieben".
// Das Detailprotokoll beantwortet die andere Frage - "warum wurde diese
// eine Datei nicht gefunden" - und dafuer braucht es JEDEN Versuch:
//
//     Textur textures/plasma_mustafar/lava
//       versucht textures/plasma_mustafar/lava.jpg     nicht da
//       versucht textures/plasma_mustafar/lava.tga     nicht da
//       versucht textures/plasma_mustafar/lava.png     nicht da
//       Shader kennt: (kein map-Befehl)
//       -> nicht gefunden
//
// Das sind bei einer Karte mit hundert Texturen mehrere hundert Zeilen.
// Zwischen die fuenfzig wichtigen gemischt waeren sie unbrauchbar, in einer
// eigenen Datei sind sie genau das Richtige.
//
// Kostet nichts, wenn niemand hinsieht: detail() prueft zuerst, ob die
// Datei offen ist, und baut die Zeichenkette gar nicht erst.
bool openDetail(const std::string& path);

// Das Verzeichnis, in dem die Protokolle liegen - leer, wenn keines
// geschrieben wird. Der Bericht soll daneben landen, ohne dass jeder
// Aufrufer den Pfad noch einmal zusammensetzt.
[[nodiscard]] std::string logVerzeichnis();
void closeDetail();

// Laeuft das Detailprotokoll gerade? Fuer teure Aufrufe:
//
//     if (diag::detailOn()) { diag::detail(teuerZusammengebaut()); }
[[nodiscard]] bool detailOn() noexcept;

void detail(const std::string& text);

inline void info(const std::string& text) { write(Level::Info, text); }
inline void warn(const std::string& text) { write(Level::Warning, text); }
inline void error(const std::string& text) { write(Level::Error, text); }

// Ein Schritt. Der Beginn wird sofort vermerkt, das Ende beim Verlassen des
// Gueltigkeitsbereichs - samt Dauer.
//
//     {
//         bhed::diag::Step step("Schriften laden");
//         buildFonts();        // stuerzt das hier ab, fehlt die
//     }                        // Abschlusszeile, und man weiss es
//
// Bewusst als Objekt und nicht als Funktionspaar: ein vorzeitiges return
// darf die Abschlusszeile nicht verschlucken, sonst saehe ein
// uebersprungener Schritt aus wie ein abgestuerzter.
class Step {
public:
    explicit Step(std::string name);
    ~Step();

    Step(const Step&) = delete;
    Step& operator=(const Step&) = delete;
    Step(Step&&) = delete;
    Step& operator=(Step&&) = delete;

    // Vermerkt einen Misserfolg, ohne den Schritt abzubrechen.
    void fail(const std::string& reason);

private:
    std::string name_;
    std::int64_t startMicros_ = 0;
    std::string failure_;
};

// Der zuletzt begonnene, noch nicht abgeschlossene Schritt. Fuer einen
// Ausnahmebehandler: der kann damit sagen, wo es passiert ist.
[[nodiscard]] std::string currentStep();

// Alle bisherigen Zeilen - fuer den Menuepunkt "Protokoll anzeigen" und fuer
// den Fall, dass die Datei nicht geschrieben werden konnte.
[[nodiscard]] const std::vector<std::string>& lines();

// Kopfzeilen mit Angaben zur Umgebung. Der Aufrufer liefert sie, weil sie
// betriebssystemabhaengig sind.
void writeHeader(const std::vector<std::pair<std::string, std::string>>& entries);

// Nur fuer Tests.
void resetForTesting();

}  // namespace bhed::diag
#endif
