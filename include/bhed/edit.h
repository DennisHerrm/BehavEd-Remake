// edit.h - Bearbeiten, Zwischenablage, Rueckgaengig, Suchen
//
// Rueckgaengig arbeitet mit Momentaufnahmen des ganzen Skripts, nicht mit
// einem Aenderungsprotokoll. Begruendung, gemessen: das groesste Raven-Skript
// ist 30 KiB (963 Zeilen), das Mittel 1,1 KiB. Eine Kopie kostet hier
// nichts, ist aber beweisbar richtig - ein Protokoll muesste fuer jede neue
// Bearbeitungsart eine eigene Umkehrung mitbringen, und genau dort sitzen
// in Editoren die Fehler, die man erst nach dem Speichern bemerkt.
#ifndef BHED_EDIT_H
#define BHED_EDIT_H

#include "bhed/commands.h"
#include "bhed/script.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bhed {

// Path steht in script.h.

// Zeiger auf den Knoten am Weg, oder nullptr.
[[nodiscard]] const Node* nodeAt(const Script& s, const Path& p);

// Aus einem Feld des Event-Editors wieder ein Argument machen.
//
// Die Schreibform folgt der Deklaration des Feldes; gemessen an 9200
// set-Aufrufen bei Raven:
//     %t und %s -> in Anfuehrungszeichen   (7145 Belege)
//     %d und %f -> blank                   (1562)
//     %v        -> < x y z >               (110)
// Ausnahme: eine %i-Typmenge steht blank (FLUSH, CHAN_VOICE, PAN).
// previous ist das Argument, das dort bisher stand, oder nullptr fuer ein
// neues. Bei Ausdrucksfeldern entscheidet die BISHERIGE Schreibweise:
//     move ( $tag( "cp1", ORIGIN)$, 0.000 )   Zahl blank
//     if   ( ..., ..., $2$ )                  dieselbe Art Feld, mit Dollar
// Aus dem Wert allein ist das nicht zu entscheiden, und ein Editor darf die
// Schreibweise, die jemand gewaehlt hat, nicht umdeuten.
// Welche Felder das Ereignisfenster zeigt.
//
// Nicht einfach `c.params`: bei `set` haengt das ZWEITE Feld vom ersten ab -
// `SET_ORIGIN` will einen Vektor, `SET_PARM1` eine Zeichenkette. Die
// Klapplisteneintraege bringen dafuer eigene Parameter mit, und die
// ersetzen die nachfolgenden aus der Signatur.
//
// `werte` sind die Werte, die gerade in den Feldern stehen - daraus folgt,
// welcher Eintrag gewaehlt ist und damit, wie es weitergeht.
//
// Lag bis rc467 in der Oberflaeche und las `g_app`. Damit war ausgerechnet
// die Funktion, die die Fenster aufbaut, die einzige, die keine Probe
// erreichen konnte.
[[nodiscard]] std::vector<Param> editorFields(
    const Command& c, const CommandDb& db,
    const std::vector<std::string>& werte);

[[nodiscard]] Arg argForParam(const Param& p, const std::string& value,
                              const CommandDb& db, const Arg* previous = nullptr);

// Der Vorgabewert eines Feldes, so wie er im Skript stehen soll.
//
// In der .bhc steht bei vielen Feldern der TYPNAME als Platzhalter, nicht
// ein Wert:
//     affect ( DEFAULT, AFFECT_TYPE )      "AFFECT_TYPE" ist kein Wert
//     set ( SET_TYPES, DEFAULT )           "SET_TYPES" auch nicht
//     wait ( 1000.0 )                      das hier schon
//
// Diese Funktion loest das auf: bei einer Typmenge den ersten Eintrag
// (FLUSH, SET_PARM1, ENABLE), bei Zahlen eine Null statt "DEFAULT".
//
// Vorher steckte die Logik nur in makeNode, und das Neu-Auswerten im Editor
// benutzte den rohen Text - so landete "AFFECT_TYPE" im Skript.
[[nodiscard]] std::string defaultValueFor(const Param& p, const CommandDb& db);

// Ein Makro ausklappen: der Marker plus die Befehle seines Rumpfes.
//
// Ergebnis ist genau das, was BehavEd in die Datei schreibt:
//     //$"walkOnly"@5
//     set ( /*!*/ "SET_WALKING", /*!*/ "true" );
//     ...
// Der Marker nennt Namen und Zeilenzahl, damit die Ausklappung im Baum
// wieder zu einem Knoten gefaltet werden kann. Die %r-Felder der .bhc
// werden zu /*!*/ - gesperrt, weil sie zum Makro gehoeren.
[[nodiscard]] std::vector<Node> makeMacroNodes(const Macro& m, const CommandDb& db);

// Neuen Knoten aus einer Signatur bauen, mit den Vorgabewerten der .bhc.
[[nodiscard]] Node makeNode(const Command& c, const CommandDb& db);

// Suchen, entspricht Dialog 140 des Originals.
// Suchen (Dialog 140). "called" vergleicht nur den Befehlsnamen,
// "containing" jedes Argument einzeln - siehe findAll in edit.cpp.
struct FindOptions {
    std::string named;        // "... a script item called:"
    std::string contains;     // "... any item containing:"
    bool wholeString = true;  // "Whole-string match only" - im Original vorgewaehlt
};
[[nodiscard]] std::vector<Path> findAll(const Script& s, const FindOptions& o);

// Sind zwei Befehle GLEICH - derselbe Name und dieselben Werte?
//
// Fuer die Hervorhebung "gleiche Befehle" (shank: "when editing commands
// like cameras that are repeated, having it show the other cameras that are
// exactly the same would be helpful"). Zahlen und Vektoren werden als Zahlen
// verglichen: "1162" und "1162.000" sind derselbe Wert. Text ohne
// Gross/klein, wie die Suche. Kommentare gleichen sich, wenn ihr Text
// gleich ist; Leerzeilen nie. Die Kinder eines Blocks zaehlen nicht - es
// geht um die Zeile, die man sieht.
[[nodiscard]] bool gleicherBefehl(const Node& a, const Node& b);

// --- Ersetzen, wie "Replace" in Notepad++ ----------------------------------
//
// Ersetzt wird, was die Suche mit "containing" trifft - nach DENSELBEN
// Regeln: jedes Argument fuer sich, mit wholeString das ganze Argument,
// sonst jedes Vorkommen als Teiltext, ohne Gross/klein. Der Befehlsname
// wird nie ersetzt ("called" waehlt nur aus), gesperrte Makrofelder
// (/*!*/) bleiben stehen.
//
// Zahlen und Vektoren schreibt BehavEd mit drei Nachkommastellen
// ("1299.000 -2714.000 1082.000"). Ersetzt man ein GANZES Zahlen- oder
// Vektorargument durch Zahlen, wird der Ersatz ebenso geschrieben - sonst
// stuende "1300 -2700 1082" neben lauter ".000".
//
// Gedacht fuer wiederverwendete Kameras: dieselbe MOVE-Stellung an mehreren
// Stellen der Zwischensequenz auf einmal aendern (shank).
//
// Rueckgabe: die Zahl der ersetzten Argumente.
int ersetzeInKnoten(Node& n, const FindOptions& o, const std::string& mit);
// Alle Treffer eines Skripts. `nurIn` (nicht leer): nur Treffer, die einer
// dieser Wege sind oder darunter liegen - "In selection".
int ersetzeImSkript(Script& s, const FindOptions& o, const std::string& mit,
                    const std::vector<Path>& nurIn = {});

class Document {
public:
    explicit Document(Script s);

    [[nodiscard]] const Script& script() const { return s_; }
    // Ungesichert heisst: der Stand weicht vom zuletzt gespeicherten ab.
    //
    // Nicht einfach ein Schalter, der bei jeder Aenderung angeht: wer etwas
    // aendert und es wieder zurueckninmt, hat nichts zu speichern. Der
    // Vergleich laeuft ueber die Tiefe des Rueckgaengig-Stapels.
    [[nodiscard]] bool dirty() const { return undo_.size() != savedDepth_; }
    void markSaved() { savedDepth_ = undo_.size(); }

    // Jedem Knoten eine eindeutige Kennung geben - in EINEM Durchlauf.
    //
    // Statt sie an den fuenfzehn Stellen zu vergeben, an denen ein Node
    // entsteht. Wer eine davon vergaesse, bekaeme stillschweigend eine
    // doppelte Kennung, und dann klappte der falsche Knoten auf. Der
    // Durchlauf kann nichts vergessen.
    //
    // Regel: wer 0 traegt oder eine in DIESEM Durchlauf schon gesehene,
    // bekommt eine neue. Damit stimmt es in allen Faellen:
    //
    //   Rueckgaengig  alte Kennungen, alle eindeutig -> nichts geschieht,
    //                 der Aufklapp-Zustand passt weiter
    //   Duplizieren   beide gleich -> der ZWEITE in Baumreihenfolge
    //                 bekommt eine neue, das Original behaelt seinen
    //                 Zustand
    //   Ablage,       Kennung aus einem anderen Dokument kann kollidieren
    //   Reiterwechsel -> wird neu vergeben
    //   Datei laden   alles 0 -> alles frisch
    //
    // Zweimal hintereinander aufgerufen aendert er nichts mehr; darauf
    // verlaesst sich der Baumneubau.
    void vergibKennungen();

    // WARUM eine Bewegung nichts getan hat.
    //
    // shanks rc535-Protokoll:
    //
    //     Baum: "else" steht schon am Ende - nichts getan
    //
    // Der Grund stand nur im Protokoll, und das sieht er beim Arbeiten
    // nicht. Handout Abschnitt 4: ein Werkzeug, das bei einer sinnlosen
    // Geste einfach nichts tut, laesst den Benutzer glauben, es sei kaputt.
    // Es soll sagen, warum.
    //
    // Der GRUND steht hier, der TEXT in der Oberflaeche. Sonst haette das
    // Modell Anzeigetexte, und die muessten uebersetzt werden.
    enum class Abweisung : std::uint8_t {
        Keine = 0,
        SchonAmEnde,          // steht bereits an der letzten Stelle
        SchonGanzOben,        // steht bereits an der ersten Stelle
        ZielGleichHerkunft,   // der Zug wuerde nichts veraendern
        ZielImGezogenen,      // ein Block kann nicht in sich selbst
        ZielIstKeinBlock,     // HINEIN geht nur bei { }
        WegUngueltig,         // leerer oder nicht aufloesbarer Weg
    };
    // Wird zu Beginn JEDER Bewegung auf Keine gesetzt.
    [[nodiscard]] Abweisung letzteAbweisung() const { return abweisung_; }

    // --- Bearbeiten. Alle liefern false, wenn der Weg nicht aufloesbar ist,
    //     und lassen das Skript dann unveraendert. ---------------------------
    // Ein LEERER Weg bedeutet "ans Ende der obersten Ebene".
    //
    // Ohne diese Bedeutung ging gar nichts: bei einem leeren Skript gibt es
    // keinen Knoten, hinter den man einfuegen koennte - also kam nie ein
    // erster hinein, und damit nie ein zweiter. Der Doppelklick in der
    // Ereignisliste tat schlicht nichts, und keine Probe hat es bemerkt,
    // weil alle mit einem bereits gefuellten Skript arbeiteten.
    bool insertAfter(const Path& p, Node n);
    // Wo ein Knoten landet, den insertAfter(p) einsetzt: hinter p - bei
    // einer Makrozeile hinter dem ganzen Makro.
    [[nodiscard]] Path wegHinter(const Path& p);
    // Ist `p` eine Makrozeile bzw. gehoert zu einem Makro?
    [[nodiscard]] bool istMakro(const Path& p) const;
    // Das Gegenstueck: DAVOR statt dahinter.
    //
    // Gebraucht, seit man aus der Ereignisliste zwischen zwei Zeilen ziehen
    // kann. Ueber insertAfter des Vorgaengers ginge es fast - nur bei der
    // ERSTEN Zeile eines Blocks gibt es keinen Vorgaenger, und genau dort
    // will man am haeufigsten etwas davorsetzen.
    //
    // Ein LEERER Weg bedeutet spiegelbildlich "an den ANFANG der obersten
    // Ebene".
    bool insertBefore(const Path& p, Node n);
    bool insertInto(const Path& blockPath, std::size_t at, Node n);
    bool removeAt(const Path& p);
    bool cloneAt(const Path& p);      // Knopf "Clone" des Originals
    // `nachher` bekommt, WO der Knoten gelandet ist.
    //
    // Ohne das ging es schief: der Aufrufer hat die Auswahl selbst
    // nachgefuehrt (`--selectedPath.back()`), und das stimmt nur, solange
    // der Knoten seine Ebene behaelt. Seit rc516 kann er den Block
    // verlassen - dann ist der Weg ein anderer, und zwar kuerzer.
    //
    // Wer die Bewegung macht, weiss als Einziger, wohin. Also sagt er es.
    // Einen Knoten ans ENDE der obersten Ebene bewegen.
    //
    // Gebraucht fuer die freie Flaeche unter dem Baum: dort gibt es keinen
    // Nachbarn, auf den man zielen koennte, also braucht es eine eigene
    // Bedeutung. Sie ist die einzige Geste, die "heraus aus ALLEM" heisst.
    // `nachher` wie bei moveTo: der Weg, auf dem er gelandet ist.
    bool moveToEnd(const Path& from, Path* nachher = nullptr);
    bool moveUp(const Path& p, Path* nachher = nullptr);
    bool moveDown(const Path& p, Path* nachher = nullptr);
    // Knopf "REM" (Ruecktaste) und sein Gegenstueck (Alt+Ruecktaste).
    // Belegt durch ACCELERATOR 135: 32771 auf Ruecktaste, 32783 auf
    // Strg+Ruecktaste (FCONTROL, nicht FALT),
    // und durch das Format in Ravens Dateien:
    //     <Tabs>//(BHVDREM)  <Zeile ohne fuehrende Tabs>
    bool commentOut(const Path& p);
    // produced meldet, wie viele Knoten aus dem Block entstanden sind.
    // `anfang` bekommt den Weg der ersten zurueckgeholten Anweisung - sie
    // kann VOR `p` liegen, wenn `p` mitten in einem auskommentierten Block
    // stand.
    bool uncomment(const Path& p, std::size_t* produced = nullptr,
                   Path* anfang = nullptr);

    // Einen leeren rem()-Befehl einfuegen (der Befehl aus der .bhc).
    bool insertRem(const Path& p);

    // Einen Knoten durch einen anderen ersetzen. Das ist genau, was der
    // Event-Editor beim Klick auf Ok tut: derselbe Befehl, neue Felder.
    bool replaceAt(const Path& p, Node n);
    // Mehrere Knoten auf einmal ersetzen (Felder, nicht Blockinhalt) - EIN
    // Rueckgaengig-Schritt. Fuer "Revert to original".
    bool replaceMany(const std::vector<std::pair<Path, Node>>& ersatz);
    // Das ganze Skript durch ein anderes ersetzen - EIN Rueckgaengig-Schritt
    // mit der Bezeichnung `was`. Fuer "nur diesen Schritt zuruecknehmen"
    // aus der Undo-Liste, das an mehreren Stellen zugleich aendern kann.
    bool ersetzeSkript(Script neu, const char* was);

    // --- Zwischenablage ---------------------------------------------------
    bool copyAt(const Path& p);
    bool cutAt(const Path& p);

    // --- Mehrere Knoten auf einmal ---------------------------------------
    //
    // Wichtig dabei: die Wege werden ABSTEIGEND abgearbeitet. Loescht man
    // von vorn, verschieben sich alle folgenden Wege um eins, und der
    // zweite Aufruf trifft den falschen Knoten. Genau daran scheitert eine
    // Mehrfachauswahl sonst, und zwar still - es wird etwas geloescht, nur
    // nicht das Gewaehlte.
    //
    // Alles zusammen ergibt EINE Momentaufnahme: Rueckgaengig nimmt die
    // ganze Auswahl zurueck, nicht Knoten fuer Knoten.
    // Einen Knoten an eine andere Stelle ziehen.
    //
    // Zwei Faelle muessen abgefangen werden, sonst geht das Skript kaputt:
    //   * Ein Block in sich selbst - dann haenge der Baum an sich selbst.
    //   * Der Weg des Ziels verschiebt sich, sobald die Quelle davor
    //     entnommen wird. Deshalb wird erst entnommen, dann der Zielweg
    //     angepasst, dann eingefuegt.
    // `nachher` liefert den Weg, auf dem der Knoten TATSAECHLICH gelandet
    // ist. Ohne das muss der Aufrufer ihn ausrechnen - und rechnet falsch.
    //
    // shank zu rc532: "dazu geht auch immer das + zu wenn ich was raus
    // ziehe." Die Aufklapp-Zustaende haengen an Wegen und muessen dem Zug
    // nachgefuehrt werden. Der Aufrufer uebergab dafuer bisher den
    // ANKER (den Weg des Blocks), nicht das Ziel: `moveTo([0,0], [0])`
    // laesst den Knoten auf [1] landen, nachgefuehrt wurde aber gegen [0]
    // - und dabei wanderte der offene Zustand von Knoten 0 auf Knoten 1.
    // Der Block galt danach als zu.
    //
    // Dieselbe Lehre wie bei den Spaltenbreiten und wie bei
    // moveUp/moveDown in rc530: erfragen statt herleiten.
    bool moveTo(const Path& from, const Path& to, Path* nachher = nullptr);

    // Mehrere Knoten auf einmal duplizieren - mit EINEM
    // Rueckgaengig-Schritt.
    //
    // shank zu rc542: "The 'Duplicate' button doesn't work when multiple
    // commands are highlighted. It can only duplicate one command at a
    // time." Loeschen und Kopieren konnten die Mehrfachauswahl laengst,
    // Duplizieren nicht.
    //
    // Wie bei removeAll von HINTEN nach vorn: sonst verschiebt die erste
    // Kopie die Wege aller folgenden.
    bool cloneAll(std::vector<Path> paths);
    bool removeAll(std::vector<Path> paths);
    bool copyAll(std::vector<Path> paths);
    bool cutAll(std::vector<Path> paths);

    // Mehrere Knoten gemeinsam verschieben.
    //
    // Die Richtung bestimmt die Reihenfolge, und das ist kein Detail:
    //   hoch  -> von VORN, sonst rueckt der zweite in die Luecke des ersten
    //   runter-> von HINTEN, aus demselben Grund spiegelverkehrt
    // Wer das vertauscht, bekommt eine Auswahl, die sich beim Schieben
    // selbst zerlegt - sichtbar erst, wenn mehr als zwei gewaehlt sind.
    //
    // Die Wege werden mitgefuehrt: nach dem Aufruf steht in paths, wo die
    // Knoten jetzt sind, damit die Auswahl mitwandern kann.
    bool moveAll(std::vector<Path>& paths, bool up);

    // Mehrere Knoten an eine Stelle ziehen. Die Reihenfolge untereinander
    // bleibt erhalten.
    bool moveAllTo(std::vector<Path> paths, const Path& to);

    // Einen Knoten IN einen Block ziehen, an dessen Anfang.
    //
    // Getrennt von moveTo: dort landet der Knoten NEBEN dem Ziel. Wer einen
    // Befehl auf ein affect zieht, meint aber fast immer hinein - genau
    // dafuer ist der Block da.
    // `nachher` wie bei moveTo: der Weg, auf dem der Knoten gelandet ist.
    // Das ist NICHT `block` + "/0" - liegt die Quelle vor dem Block und auf
    // derselben Ebene, rueckt der Block beim Entnehmen um eins vor.
    bool moveInto(const Path& from, const Path& block, Path* nachher = nullptr);

    // Einen oder mehrere Knoten an eine AUSDRUECKLICHE Stelle ziehen: vor
    // `anker`, dahinter, oder an den Anfang des Blocks `anker`. Die drei
    // Zonen beim Ziehen im Baum (oberes Drittel / Mitte / unteres Drittel),
    // wie beim Ziehen aus der Ereignisliste. Ein Rueckgaengig-Schritt; die
    // Reihenfolge untereinander bleibt. Liegt ein gewaehlter Knoten in einem
    // anderen gewaehlten, zieht er mit diesem mit. `ersterNachher`: wo der
    // erste Knoten gelandet ist.
    enum class Stelle : std::uint8_t { Davor, Hinein, Dahinter };
    bool moveAllAt(std::vector<Path> paths, const Path& anker, Stelle wo,
                   Path* ersterNachher = nullptr);
    // Mehrere Knoten hinter `p` einsetzen - mit EINEM Rueckgaengig-Schritt.
    //
    // shanks rc533-Protokoll: der Rueckgaengig-Stapel sprang beim Einfuegen
    // des Makros `standOnly` von 7 auf 11. Das Makro sind vier Knoten
    // (Merkzeile plus drei set), und jedes insertAfter legte einen eigenen
    // Schritt an. Wer danach Strg+Z drueckt, holt ein Viertel Makro zurueck
    // und muss noch dreimal druecken.
    //
    // Eine Geste, ein Schritt - dieselbe Regel wie bei den Nullzuegen in
    // rc530.
    //
    // `nachher` liefert den Weg des ZULETZT eingesetzten Knotens.
    bool insertAfterAll(const Path& p, const std::vector<Node>& nodes,
                        Path* nachher = nullptr);
    bool pasteAfter(const Path& p);   // leerer Weg: ans Ende, wie insertAfter
    // Die Ablage ans ENDE eines Blocks setzen (affect, if, else, loop, task
    // ...). Ein Rueckgaengig-Schritt. false, wenn `block` kein Block ist.
    bool pasteInto(const Path& block);
    // Das ganze Skript ersetzen, als EIN Rueckgaengig-Schritt (Restore aus
    // der Sicherung - Strg+Z holt den Stand davor zurueck).
    bool replaceAll(Script s, const char* was = "restore");
    [[nodiscard]] bool hasClipboard() const { return !clip_.empty(); }

    // Knoten von AUSSEN in die Zwischenablage legen.
    //
    // Fuer die geteilte Ansicht: rechts steht ein anderes Dokument, und aus
    // ihm soll sich etwas herueberholen lassen. Die Ablage gehoert dem
    // Dokument (jedes hat seine eigene), also braucht das linke einen Weg,
    // Fremdes aufzunehmen.
    //
    // Kein Rueckgaengig-Eintrag: Kopieren aendert das Dokument nicht. Erst
    // das Einfuegen tut das, und DAS steht schon im Stapel.
    void setClipboard(std::vector<Node> nodes) { clip_ = std::move(nodes); }

    // Und wieder heraus.
    //
    // Gebraucht, damit die Ablage dem ANWENDER folgt statt dem Dokument:
    // wer in einem Reiter kopiert, will es im naechsten einfuegen koennen.
    // Die Oberflaeche traegt sie beim Reiterwechsel hinueber.
    [[nodiscard]] const std::vector<Node>& clipboard() const { return clip_; }

    // --- Rueckgaengig -------------------------------------------------------
    bool undo();
    bool redo();
    [[nodiscard]] std::size_t undoDepth() const { return undo_.size(); }
    [[nodiscard]] std::size_t redoDepth() const { return redo_.size(); }

    // Was der naechste Schritt zuruecknehmen bzw. wiederholen wuerde.
    // Leer, wenn nichts ansteht. Damit kann das Menue "Rueckgaengig:
    // Loeschen" anzeigen statt nur "Rueckgaengig" - man soll vorher wissen,
    // was passiert.
    [[nodiscard]] const char* undoLabel() const;
    [[nodiscard]] const char* redoLabel() const;
    // Zaehlt jede Aenderung des Inhalts, auch Undo und Redo - fuer Anzeigen,
    // die sich nur bei einer Aenderung neu rechnen sollen.
    [[nodiscard]] std::uint64_t stand() const { return stand_; }

    // Fuer die Undo-Liste (wie in 3ds Max). undoStand(i) ist der Stand VOR
    // Schritt i (0 = der aelteste), undoWas(i) seine Bezeichnung. Beim Redo
    // ist redoStand(0) der Stand NACH dem naechsten Wiederholen.
    [[nodiscard]] const Script& undoStand(std::size_t i) const { return undo_[i].script; }
    [[nodiscard]] const char* undoWas(std::size_t i) const { return undo_[i].what; }
    [[nodiscard]] const Script& redoStand(std::size_t i) const { return redo_[redo_.size() - 1 - i].script; }
    [[nodiscard]] const char* redoWas(std::size_t i) const { return redo_[redo_.size() - 1 - i].what; }

    // Obergrenze, damit ein langer Arbeitstag den Speicher nicht auffrisst.
    static constexpr std::size_t kMaxUndo = 200;

private:
    // Der naechste freie Wert. Beginnt bei 1, weil 0 "noch keine" heisst.
    Abweisung abweisung_ = Abweisung::Keine;
    Kennung naechsteKennung_ = 1;

    void snapshot(const char* what);
    [[nodiscard]] std::vector<Node>* siblingsOf(const Path& p, std::size_t& idx);

    // --- Makros als Behaelter -------------------------------------------
    //
    // In der Datei ist ein Makro flach: die Merkzeile //$"standOnly"@3 und
    // danach die drei Befehle auf DERSELBEN Ebene. Im Original sind sie im
    // Baum aber echte KINDER des Makroknotens (am laufenden BehavEd
    // nachgesehen, 27.09.): Hineinziehen und Hinzufuegen setzt dort hinein,
    // das Makro zaehlt beim Speichern mit (@3 -> @5), und die Makrozeile
    // nimmt ihre Befehle mit, wenn man sie bewegt oder loescht.
    //
    // Der Waechter haelt zu Beginn jeder Bearbeitung fest, welche Befehle zu
    // welchem Makro gehoeren, und fuehrt am Ende die Zahlen nach (siehe
    // edit.cpp, MakroWache).
    friend struct MakroWache;
    std::uint64_t aenderungen_ = 0;   // zaehlt jede Momentaufnahme
    int wachTiefe_ = 0;               // nur der aeusserste Waechter zaehlt
    // Befehle, die ausdruecklich in ein Makro kommen: (Befehl, Makro).
    std::vector<std::pair<Kennung, Kennung>> beitritt_;
    // Befehle, die ausdruecklich aus ihrem Makro austreten.
    std::vector<Kennung> austritt_;
    // Der Weg des Makros, zu dem der Befehl auf `p` gehoert - leer sonst.
    [[nodiscard]] Path makroZu(const Path& p);
    // Der Weg des letzten Befehls eines Makros (ohne die in `ohne`), bei
    // einem leeren das Makro selbst.
    [[nodiscard]] Path gruppenEnde(const Path& makro, const std::vector<Path>& ohne = {});
    // Die Wege um die Befehle der gewaehlten Makros ergaenzen.
    [[nodiscard]] std::vector<Path> mitGruppen(std::vector<Path> paths);
    // Die gerade eingefuegten Knoten ab `weg` (anzahl Stueck) dem Makro mit
    // der Kennung `makro` zuschlagen. Direkt nach dem Einfuegen rufen.
    void merkeBeitritt(const Path& weg, std::size_t anzahl, Kennung makro);
    [[nodiscard]] Kennung kennungBei(const Path& p);

    Script s_;
    struct Step {
        Script script;
        const char* what;   // zeigt auf eine Zeichenkette mit Programmlaufzeit
    };
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    std::size_t savedDepth_ = 0;
    std::uint64_t stand_ = 0;
    std::vector<Node> clip_;
};

// --- Welches Argument eines Befehls ist seine DAUER? ---------------------
//
// Gebraucht, um in der Zeitleiste an einer Kante ziehen zu koennen. Zwei
// Faelle, beide so in ICARUS:
//
//     wait   ( 5000.000 )                      -> Argument 0
//     camera ( MOVE, <ort>, 5000.000 )         -> das LETZTE Argument
//     camera ( PAN, <winkel>, <richtung>, 5000.000 )
//
// Nicht jeder Befehl hat eine: camera(ENABLE) und camera(DISABLE) haben
// keine, wait("signal") wartet auf ein Signal statt auf Zeit. Rueckgabe -1
// heisst "nicht ziehbar", und die Oberflaeche zeigt dort keinen Griff -
// ein Griff, der nichts tut, ist schlimmer als keiner.
//
// Hier im Kern und nicht in der Oberflaeche, damit Proben es fassen.
[[nodiscard]] int durationArgIndex(const Node& n);

} // namespace bhed
#endif
