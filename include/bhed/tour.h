// tour.h - Tutorials im Programm: Daten, Zielangaben, Pruefungen, Ablauf
//
// Paket tour (rc601). Der Betreuer wuenschte Tutorials, bei denen "das
// Programm selbst zeigt, welche Knoepfe man druecken muss, und Schritt fuer
// Schritt fuehrt" - erst ohne KI, KI spaeter. Vorbild sind die
// IntroTutorials von Unreal (Stufen mit Ziel-Widget), die Walkthroughs von
// VS Code (ein Schritt ist erledigt, wenn ein ZUSTAND eintritt, nicht ein
// bestimmter Klick) und Shepherd.js (abgedunkelter Hintergrund mit einer
// Oeffnung, durch die das Ziel bedienbar bleibt).
//
// Diese Datei ist der reine Teil ohne Oberflaeche - pruefbar in
// tests/tourtest.cpp. Die Oberflaeche (Fenster, Hervorhebung, Auswerten am
// Programmzustand) steht in gui/tour.cpp.
//
// Die Daten entstehen aus den Handbuchquellen: tools/handbuch/gen_handbuch.py
// schreibt handbuch/<sprache>/touren.json aus den Tutorial-Schritten
// (inhalt_schritte.py) und den Angaben in tools/handbuch/inhalt_touren.py.
//
// --- Zielangaben (worauf ein Schritt zeigt), mit ";" mehrere ------------------
//   knopf:<Kennung>[@<fenster>]       Knopf mit der Beschriftung tr(Kennung)
//   knopfN:<text>[@<fenster>][#n]     n-tes Element mit genau diesem Text ("##v")
//   menue:<K1>><K2>>...               Menuekette; gezeigt wird das tiefste
//                                     offene Glied (erst "File", dann "New")
//   gruppe:<K1>,<K2>[@<fenster>]      mehrere Knoepfe als ein Kasten
//   eintrag:<befehl>                  Eintrag der Ereignisliste links
//   zeile:<befehl>[#n]                n-te Zeile des Skriptbaums mit dem Befehl
//   reiter:+                          das "+" am Ende der Reiterleiste
//   fenster:<teil>                    ein ganzes Fenster ("###editor")
//   baum                              der Skriptbaum (Classic)
//   meldungen                         die Zaehler unten in der Statuszeile
//
// --- Pruefungen (wann ein Schritt erledigt ist), mit "|" Alternativen -------
//   Zaehler, mit "+" = "mehr als beim Beginn des Schritts":
//     reiter+  knoten+  aenderung+  rem+  uebersetzt+  gespeichert+
//     befehl:<name>[+]   (ohne "+": mindestens einer im aktiven Skript)
//   Zustaende:
//     leer                     das aktive Skript hat keine Befehle
//     kind:<block>><befehl>    ein <block> hat <befehl> direkt im Rumpf
//     wert:<befehl>#<n>:<feld> das Feld ist gesetzt (nicht leer, nicht DEFAULT)
//     auswahl[:<befehl>]       eine Zeile (dieses Befehls) ist gewaehlt
//     editor[:<befehl>]        der Ereigniseditor ist offen (fuer <befehl>)
//     editorzu                 der Ereigniseditor ist zu
//     fenster:<name>           messages, find
//     modus:<n>                Modusleiste: 0 Events, 1 Map, 2 Model
//     ansicht:<n>              Skriptansicht: 0 Classic, 1 Graph, 2 Tracks
//     gespeichert              aktives Skript hat einen Pfad und ist gesichert
//     schwebt                  die Maus steht eine Weile auf dem Ziel
//     geklickt                 das Ziel wurde gerade angeklickt
//     manuell                  nur der Knopf "Weiter" schaltet weiter
//
// --- Vorbedingung: "<pruefung> < <ziel>" --------------------------------------
//   Gilt die Pruefung nicht, zeigt die Tour auf den Oeffner (<ziel>) statt
//   auf das eigentliche Ziel - wie Unreals TabToFocusOrOpen.
//
// --- Einrichten (Ueberspringen / "Mach es"), mit ";" mehrere ------------------
//   neuerReiter  befehl:<name>  waehle:<befehl>#<n>|*  wert:<befehl>#<n>:<feld>=<text>
//   modus:<n>  ansicht:<n>  editor[:<befehl>#<n>]  editorzu  fenster:<name>
#ifndef BHED_TOUR_H
#define BHED_TOUR_H

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bhed::tour {

// --- Daten ----------------------------------------------------------------

struct Schritt {
    std::string text;        // roh: **fett**, `code`, <kbd>..</kbd>, {{Kennung}}
    std::string bild;        // Bildname im Handbuch
    std::string unter;       // Bildunterschrift (roh)
    std::string ziel;        // Zielangaben, ";" getrennt
    std::string fertig;      // Pruefung, "|" = oder
    std::string voraus;      // "<pruefung> < <ziel>"
    std::string einrichten;  // Aktionen, ";" getrennt ("Mach es")
    bool sicher = false;     // "Mach es" erlaubt (kein Dateidialog, nichts Unumkehrbares)
    // rc601 tour (Review): was "Ueberspringen" ausfuehrt - nur, was der
    // naechste Schritt braucht. Leer: `einrichten` ohne die Aktionen, die das
    // Skript aendern (befehl, wert) - siehe ueberspringAktionen().
    std::string ueberspringen;
};

struct Tour {
    std::string id;          // "k-neues-skript" = Handbuchseite ohne .html
    std::string gruppe;      // "tut_skript"
    std::string titel;
    std::string kurz;
    std::string seite;       // "k-neues-skript.html"
    std::vector<std::pair<std::string, std::vector<Schritt>>> ansichten;   // classic, graph, tracks

    // Die Schritte einer Ansicht; fehlt sie, gilt Classic (wie im Handbuch).
    [[nodiscard]] const std::vector<Schritt>* schritte(const std::string& ansicht) const;
};

struct Touren {
    int fassung = 0;
    std::string sprache;
    std::vector<Tour> liste;
    [[nodiscard]] const Tour* finde(const std::string& id) const;
};

// handbuch/<sprache>/touren.json lesen. false mit Meldung bei kaputten Daten,
// unbekannten Zielarten, Pruefungen oder Aktionen und doppelten Kennungen.
[[nodiscard]] bool ladeTouren(const std::string& json, Touren& out, std::string* fehler);

// Die Ansichtsnamen in der Reihenfolge der Einstellung flussAnsicht.
[[nodiscard]] const char* ansichtName(int flussAnsicht);

// --- Zielangaben --------------------------------------------------------------

struct Ziel {
    enum class Art { Keins, Knopf, KnopfN, Menue, Gruppe, Eintrag, Zeile, Reiter, Fenster, Baum, Meldungen };
    Art art = Art::Keins;
    std::vector<std::string> teile;   // Kennungen (Knopf, Menue, Gruppe) oder woertlich
    std::string fenster;              // "@..."
    int nte = 0;                      // "#n"
};

[[nodiscard]] bool parseZiel(const std::string& s, Ziel& out, std::string* fehler);
// Mehrere, ";" getrennt (die Rahmen werden nummeriert wie im Handbuch).
[[nodiscard]] bool parseZiele(const std::string& s, std::vector<Ziel>& out, std::string* fehler);
// Die i18n-Kennungen einer Zielangabe (Knopf, Menue, Gruppe).
[[nodiscard]] std::vector<std::string> kennungenIn(const Ziel& z);

// --- Pruefungen ---------------------------------------------------------------

// Was das Programm in einem Bild von sich zeigt. Die Oberflaeche fuellt das
// jedes Bild (gui/tour.cpp); die Pruefungen vergleichen es mit dem Stand beim
// Beginn des Schritts.
struct Kontext {
    int reiter = 0;
    int knoten = 0;           // Befehle in allen offenen Reitern
    int aenderungen = 0;      // Summe der Rueckgaengig-Tiefen aller Reiter
    double uebersetzt = 0.0;  // waechst bei jedem Uebersetzen
    int gespeichert = 0;      // waechst bei jedem Sichern
    bool aktivGesichert = false;   // aktives Skript hat Pfad und keine Aenderung
    bool leer = true;         // aktives Skript ohne Befehle
    int rem = 0;              // auskommentierte Zeilen im aktiven Skript
    std::map<std::string, int> befehle;   // Name -> Anzahl im aktiven Skript (alle Ebenen)
    std::string auswahl;      // Name des gewaehlten Befehls, "" = keiner
    int auswahlZahl = 0;
    bool editorOffen = false;
    std::string editorBefehl;
    std::vector<std::string> fenster;     // offene Fenster ("messages", "find")
    int modus = 0;
    int ansicht = 0;
    bool schwebt = false;
    bool geklickt = false;
    // Nachschlagen im lebenden Skript (nur fuer den aktuellen Stand).
    std::function<bool(const std::string& block, const std::string& befehl)> hatKind;
    std::function<std::string(const std::string& befehl, int nte, int feld)> argument;
};

// Die bekannten Pruefungen (Namen ohne Argument).
[[nodiscard]] const std::vector<std::string>& bekanntePruefungen();
// Kennt das Programm diese Pruefung (mit "|"-Alternativen)?
[[nodiscard]] bool pruefungBekannt(const std::string& pruefung, std::string* fehler);
// Erfuellt? `start` ist der Stand beim Beginn des Schritts.
[[nodiscard]] bool erfuellt(const std::string& pruefung, const Kontext& jetzt, const Kontext& start);
// Enthaelt die Pruefung nur Zustaende ohne "+" (dann darf sie beim Betreten
// schon erfuellt sein)?
[[nodiscard]] bool istZustand(const std::string& pruefung);

// Vorbedingung "<pruefung> < <ziel>" zerlegen.
struct Voraus {
    std::string pruefung;
    std::string oeffner;   // Zielangabe, leer = keiner
};
[[nodiscard]] Voraus zerlegeVoraus(const std::string& s);

// --- Aktionen (einrichten) ------------------------------------------------------

struct Aktion {
    std::string name;   // neuerReiter, befehl, waehle, wert, modus, ansicht, editor, editorzu, fenster
    std::string arg;    // alles hinter dem ersten ':'
};
[[nodiscard]] const std::vector<std::string>& bekannteAktionen();
[[nodiscard]] bool parseAktionen(const std::string& s, std::vector<Aktion>& out, std::string* fehler);
// "affect#2" -> ("affect", 2); "affect" -> ("affect", 0); "*" -> ("*", 0).
void befehlUndNummer(const std::string& s, std::string& befehl, int& nte);
// rc601 tour (Review): die Aktionen fuer "Ueberspringen". Gemeldet: "Skip
// always runs `einrichten`; for s_ein_doppel ... it inserts stray `wait` lines
// into the active script". Also: das eigene Feld `ueberspringen`, sonst
// `einrichten` ohne befehl/wert - Ueberspringen aendert das Skript nie.
[[nodiscard]] std::string ueberspringAktionen(const Schritt& s);

// --- Text ------------------------------------------------------------------------

// Ein Stueck des Schritttextes fuer die Darstellung.
struct Stueck {
    enum class Art { Text, Code, Taste, Kennung };
    Art art = Art::Text;
    std::string inhalt;   // bei Kennung der Name ("FileSave")
    bool fett = false;
};
// **fett**, `code`, <kbd>Taste</kbd>, {{Kennung}}; [[seite|Text]] wird zu
// Text, [[seite]] faellt weg, {{icon:Name}} faellt weg.
[[nodiscard]] std::vector<Stueck> zerlege(const std::string& text);
// Die Kennungen ({{Name}}) eines Textes.
[[nodiscard]] std::vector<std::string> kennungenImText(const std::string& text);

// --- Ablauf ----------------------------------------------------------------------

class Ablauf {
public:
    enum class Stand : unsigned char { Offen, Erledigt, Uebersprungen };

    // Eine Tour beginnen (bei Schritt `ab`, 0-basiert). false, wenn es die
    // Ansicht oder den Schritt nicht gibt.
    bool starte(const Tour& t, const std::string& ansicht, int ab = 0);
    void schliesse();

    [[nodiscard]] bool aktiv() const { return tour_ != nullptr; }
    [[nodiscard]] const Tour* tour() const { return tour_; }
    [[nodiscard]] const std::string& ansicht() const { return ansicht_; }
    [[nodiscard]] int schritt() const { return schritt_; }
    [[nodiscard]] int anzahl() const;
    [[nodiscard]] const Schritt* aktuell() const;
    [[nodiscard]] Stand stand(int i) const;
    // Der aktuelle Schritt ist erfuellt und wartet auf das Weiterschalten.
    [[nodiscard]] bool erfuellt() const { return erfuelltSeit_ >= 0.0; }
    // Ganz durch (der letzte Schritt erledigt oder uebersprungen).
    [[nodiscard]] bool fertig() const { return fertig_; }
    // Wurde der Schritt ueber "Zurueck" betreten? Dann zaehlt nur eine neue
    // Erfuellung, und "Weiter" schaltet von Hand.
    [[nodiscard]] bool zurueckGeholt() const { return zurueckGeholt_; }

    // Jedes Bild. `jetzt` in Sekunden. Gibt true zurueck, wenn der Schritt in
    // diesem Bild erfuellt wurde. Nach `verzoegerung` Sekunden schaltet er
    // von selbst weiter.
    bool bild(const Kontext& k, double jetzt, double verzoegerung = 0.6);
    // Von Hand weiter (manuell, oder ein schon erfuellter Schritt).
    void weiter();
    // Zurueck: der vorige Schritt, mit neuem Stand beim Beginn.
    void zurueck();
    // Ueberspringen: markiert den Schritt und gibt seine Aktionen zurueck
    // (ueberspringAktionen; die Oberflaeche fuehrt sie aus), dann weiter.
    std::string ueberspringen();
    // "Mach es": die Aktionen des Schritts, wenn er sicher ist (sonst "").
    // Der Schritt erledigt sich danach ueber seine Pruefung.
    [[nodiscard]] std::string machEs() const;

private:
    void betrete(int i, bool rueckwaerts);

    const Tour* tour_ = nullptr;
    const std::vector<Schritt>* schritte_ = nullptr;
    std::string ansicht_;
    int schritt_ = 0;
    std::vector<Stand> staende_;
    Kontext start_;
    bool startDa_ = false;       // Stand beim Beginn schon aufgenommen?
    bool warFalsch_ = false;     // nach "Zurueck": einmal nicht erfuellt gewesen?
    bool zurueckGeholt_ = false;
    double erfuelltSeit_ = -1.0;
    bool fertig_ = false;
};

// --- Fortschritt (in den Einstellungen: tour.<id>=<schritt>/<anzahl> oder fertig) ---

[[nodiscard]] std::string standText(int naechster, int anzahl, bool fertig);
[[nodiscard]] bool leseStand(const std::string& s, int& naechster, int& anzahl, bool& fertig);

// --- Suche im Handbuch (handbuch/<sprache>/suche.js) -------------------------------

struct Abschnitt {
    std::string seite;    // "k-neues-skript.html"
    std::string anker;    // "schritte" oder ""
    std::string titel;    // Seitentitel
    std::string ueber;    // Abschnittsueberschrift
    std::string text;     // Klartext
};
// window.HANDBUCH_INDEX=[...]; aus suche.js lesen.
[[nodiscard]] bool ladeSuchindex(const std::string& js, std::vector<Abschnitt>& out, std::string* fehler);
struct Treffer {
    std::size_t abschnitt = 0;
    double wert = 0.0;
};
// Rangfolge nach BM25 ueber Titel, Ueberschrift und Text (Titel zaehlt
// mehr). Woerter ohne Gross/klein; CJK-Zeichen zaehlen einzeln.
[[nodiscard]] std::vector<Treffer> suche(const std::vector<Abschnitt>& index, const std::string& anfrage,
                                         std::size_t hoechstens = 8);

}  // namespace bhed::tour

#endif
