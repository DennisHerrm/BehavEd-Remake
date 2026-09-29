// tree.h - aus einem Skript die Zeilen der Baumansicht bauen
//
// Die Datei ist flach; das Falten ist Sache der Anzeige. Zwei Faltungen:
//   * Blockbefehle (affect, task, if, loop) - Kinder sind der Blockinhalt
//   * Makros - //$"walkOnly"@5 fasst die naechsten 5 Befehle zusammen
#ifndef BHED_TREE_H
#define BHED_TREE_H

#include "bhed/commands.h"
#include "bhed/script.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bhed {

// Welche Bloecke aufgeklappt sind. Beim Laden sind alle zu - so zeigt das
// Original ein Skript nach dem Oeffnen, mit [+] vor jedem affect/task.
//
// Der Schluessel ist die KENNUNG des Knotens, nicht sein Weg.
//
// Frueher stand hier der Weg. Ein Weg ist aber keine Eigenschaft des
// Knotens, sondern seiner Stelle: jedes Einfuegen, Loeschen und Ziehen
// verschiebt alle Wege dahinter. Fuenf Runden lang (rc530-rc534) wurde das
// mit `nachZug` einzeln nachgefuehrt, und elf Bearbeitungswege taten es
// trotzdem nicht - Einfuegen, Loeschen, Ablage, Duplizieren, beide
// Mehrfachauswahl-Zuege. Das Rueckgaengigmachen liess sich damit ueberhaupt
// nicht heilen.
//
// Mit der Kennung faellt `nachZug` ersatzlos weg: es gibt nichts
// nachzufuehren. Die Kennungen fahren im Schnappschuss mit, also ueberlebt
// der Zustand auch Strg+Z - und zwar ohne sich dabei zurueckzudrehen.
// Aufklappen ist keine Bearbeitung und gehoert nicht in die
// Rueckgaengig-Kette; genau deshalb liegt der Zustand HIER und nicht als
// Flagge im Knoten.
class Expanded {
public:
    [[nodiscard]] bool isOpen(Kennung k) const;
    void setOpen(Kennung k, bool open);
    void toggle(Kennung k) { setOpen(k, !isOpen(k)); }
    void openAll() { all_ = true; open_.clear(); }
    void closeAll() { all_ = false; open_.clear(); }

private:
    std::vector<Kennung> open_;   // Ausnahmen zum Grundzustand
    bool all_ = false;
};

struct TreeOptions {
    bool showTypes = false;   // Schalter "Show Types" im Original
    bool gFloats = false;     // Schalter "%g floats": 1000.000 -> 1000
    bool foldMacros = true;   // Makro als ein Knoten statt N Zeilen
    bool showBlanks = false;  // Leerzeilen mit anzeigen
    const Expanded* expanded = nullptr;  // nullptr = alles aufgeklappt
};

struct Row {
    enum class What : std::uint8_t { Command, Macro, Comment, Blank };
    What what = What::Command;
    int depth = 0;
    std::string icon;        // I_SET, I_CAMERA ... aus der .bhc
    std::string text;        // angezeigte Beschriftung (Name + Argumente)
    // Name und Argumente auch einzeln, weil die Anzeige sie in ZWEI
    // SPALTEN setzt - so wie das Original. Am Bildschirmfoto ausgemessen:
    // die Klammer beginnt bei jeder Zeile 59 Bildpunkte hinter dem Namen,
    // unabhaengig davon, wie lang der Name ist.
    std::string name;        // "affect"
    std::string args;        // "( anakin2, FLUSH )"
    std::string help;        // Beschreibung, gehoert in die Statuszeile
    const Node* node = nullptr;
    Path path;               // Weg zum Knoten, fuer die Bearbeitung
    // Die Kennung des Knotens - Schluessel fuer den Aufklapp-Zustand.
    // Steht hier, damit die Anzeige nicht ueber `node` dereferenzieren
    // muss, um sie zu bekommen.
    Kennung kennung = 0;
    // Steht diese Zeile UNTER einem Makro?
    //
    // shank hat dreimal versucht, einen Befehl aus `standOnly` heraus zu
    // ziehen, und jedes Mal passierte nichts. Der Grund: ein Makro ist
    // KEIN Block. Es ist eine Merkzeile plus die Befehle, die es erzeugt
    // hat - und die stehen alle auf derselben Ebene, nicht darin. Der Baum
    // rueckt sie nur ein, damit man sieht, wozu sie gehoeren.
    //
    // Im Baum sahen Makro und Block bis hierher gleich aus: beide mit "+"
    // und eingerueckten Zeilen. Das war der eigentliche Mangel - nicht das
    // Bewegen, das immer richtig gearbeitet hat.
    bool ausMakro = false;
    int childCount = 0;      // > 0: aufklappbar
    bool open = false;       // bei Bloecken: aufgeklappt?
    bool locked = false;     // enthaelt /*!*/-Felder
    // Letztes Kind seines Elters? Nur fuer die gepunkteten Linien: bei der
    // letzten Zeile eines Blocks hoert die senkrechte Linie auf halber
    // Hoehe auf, statt durchzulaufen. Am Original ausgemessen.
    bool lastChild = false;
};

// Baut die sichtbaren Zeilen. Zugeklappte Knoten liefern ihre Kinder nicht.
void buildTree(const Script& s, const CommandDb& db, const TreeOptions& opt,
               std::vector<Row>& out);

// Eine einzelne Zeile so beschriften, wie BehavEd sie im Baum zeigt.
[[nodiscard]] std::string rowText(const Node& n, const TreeOptions& opt);

// Nur der Argumentteil, also "( anakin2, FLUSH )". rowText ist genau
// Name + Leerzeichen + dies.
[[nodiscard]] std::string rowArgs(const Node& n, const TreeOptions& opt);

// --- Die Spaltenmasse der Baumansicht ----------------------------------
//
// An einem Bildschirmfoto von BehavEd 2.0 ausgemessen: Zeilenhoehe 16,
// MS Sans Serif 8pt, also rund 13 Bildpunkte Schrifthoehe. Gemessen wurden
// die Mittelpunkte der gepunkteten Senkrechten (x = 25 und x = 44) und der
// Beginn der runden Klammer (x = 117 bei Tiefe 0, x = 136 bei Tiefe 1).
//
// Daraus:
//   Einrueckung je Ebene   44 - 25 = 19
//   Namensspalte          117 - 58 = 59   und  136 - 77 = 59   (gleich!)
//
// Dass die Namensspalte auf BEIDEN Ebenen genau 59 breit ist, ist der
// eigentliche Befund: die Klammer beginnt immer gleich weit hinter dem
// Namen, ob der Name nun "rem" oder "waitsignal" heisst.
//
// Hier stehen die Masse als Verhaeltnis zur Schrifthoehe, damit sie bei
// 144 dpi und beim Zoomen mitwachsen. Eine Probe haelt sie bei 13 auf den
// gemessenen Werten fest.
struct TreeMetrics {
    float indent = 0.0F;   // Einrueckung je Ebene            (19 bei 13)
    float box = 0.0F;      // Kantenlaenge des Kaestchens     ( 9 bei 13)
    float iconAt = 0.0F;   // Symbol, ab Spaltenmitte         (11 bei 13)
    float nameAt = 0.0F;   // Name, ab Spaltenmitte           (33 bei 13)
    float nameW = 0.0F;    // Breite der Namensspalte         (59 bei 13)
};

[[nodiscard]] TreeMetrics treeMetricsFor(float fontHeight);

// Zeile der Events-Liste, wie BehavEd sie zeigt:
//   affect     ( <str>, < E"affect_type" > )
//   move       ( <vec>, <vec>, <float> )
[[nodiscard]] std::string signatureText(const Command& c);

// Symbol eines Befehls; kommt aus dem [I_xxx]-Vorsatz der .bhc.
[[nodiscard]] std::string iconFor(const Node& n, const CommandDb& db);

} // namespace bhed
#endif
