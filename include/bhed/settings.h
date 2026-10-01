// settings.h - Einstellungen, wie Dialog 131 des Originals sie kennt
//
// Aus der Ressource von BehavEd.exe (DIALOG 131 "Preferences"):
//   Skriptpfad, Ort von IBIZE.EXE, Command Description File, Quelldateipfad,
//   "Re-open last file at startup", "Alpha-sort edit pulldowns"
// Dazu kommen bei uns Sprache, Farbgebung und die Liste zuletzt geoeffneter
// Dateien (der MRU-Knopf).
//
// Bewusst ohne Windows: eine schlichte Textdatei mit Schluessel=Wert. Damit
// ist das Lesen und Schreiben ohne Oberflaeche pruefbar, und die Datei
// laesst sich von Hand reparieren, wenn etwas schiefgeht.
#ifndef BHED_SETTINGS_H
#define BHED_SETTINGS_H

#include <map>
#include <cstddef>
#include <string>
#include <vector>

namespace bhed {

struct Settings {
    // Dialog 131
    std::string scriptPath;        // wo die .icarus liegen
    std::string ibizePath;         // IBIZE.EXE, falls extern uebersetzt wird
    std::string commandFile;       // die .bhc
    std::string sourcePath;        // Quelldateien (Kopfdateien der Engine)
    bool reopenLastFile = false;
    // Wurde das Programm zuletzt sauber beendet? Beim Start auf false
    // gesetzt und sofort gespeichert, beim Beenden wieder true - wie
    // `legal_exit` in der Registry des Originals. Steht es beim naechsten
    // Start noch auf false, ist das Programm abgestuerzt.
    bool legalExit = true;
    bool alphaSortPulldowns = false;
    // "Use Alternative Coloured Icons" (id 1052 in Dialog 131). Der
    // Symbolstreifen enthaelt jedes Symbol zweimal, hell und dunkel - das
    // ist ein zweiter Farbsatz zum Umschalten, keine Auswahlfassung.
    bool alternativeIcons = false;
    // "Yes/No query on Open/New/Exit" (id 1058)
    bool queryOnDiscard = true;
    // "File-Open dialog defaults to last dir" (id 1065)
    bool dialogUsesLastDir = true;
    std::string lastDir;
    std::string mapPath;   // zuletzt geladene .bsp
    std::vector<std::string> gamePaths;   // Ordner mit .pk3, z. B. GameData/base

    // unsere Zutaten
    // LEER heisst "der Benutzer hat noch nichts gewaehlt - nimm das
    // Gebietsschema des Rechners".
    //
    // Hier stand `"en"`. Damit war "nichts gewaehlt" nicht von "Englisch
    // gewaehlt" zu unterscheiden, und der Start ueberschrieb die
    // Gebietserkennung sofort wieder:
    //
    //     setLanguage(fromSystemLocale("de-DE"))   -> Deutsch
    //     loadSettingsFile(...)                    -> language == "en"
    //     findLanguage("en") -> Englisch           -> Deutsch weg
    //
    // Fehlt der Schluessel in der Einstellungsdatei - etwa weil sie aus
    // einer Fassung stammt, die ihn noch nicht geschrieben hat -, blieb
    // die Vorgabe stehen und gewann. `fromSystemLocale` war damit ohne
    // Wirkung.
    //
    // shank arbeitet auf `Gebiet de-DE`, und in seinem rc539-Protokoll
    // steht die Statuszeile trotzdem auf Englisch:
    //
    //     Statuszeile: Dropped where it already was - nothing to do
    //
    // `findLanguage("")` liefert nullptr, also bleibt das Gebietsschema
    // stehen. Waehlt jemand ausdruecklich eine Sprache, schreibt
    // saveSettings() ihren Code hierher, und der gewinnt wieder.
    std::string language;
    std::string theme = "windows";
    float uiScale = 1.0F;
    // Anteil der Ereignisliste an der Breite von Ereignissen + Skriptablauf.
    // Vorgabe aus Dialog 102: 191 / (191 + 372) = 0,339.
    // Die Helferzeilen des Ereigniseditors, je Befehl UND Feld.
    //
    // Schluessel: "if/0", "set/1" ... Wert: Typ|Name|Marke|Art|von|bis
    //
    // Warum in den Einstellungen und nicht im Skript: die vier Zeilen sind
    // ein BAUHELFER. Erst wer Get, Tag oder Rnd drueckt, schreibt einen
    // Ausdruck ins Feld - und nur der gehoert in die Datei. Wer nur die
    // Klappliste umstellt, hat das Skript nicht geaendert und will trotzdem,
    // dass seine Wahl beim naechsten Oeffnen noch dasteht.
    //
    // shank zu rc554: "dann werden die Werte bei IF nicht gespeichert, also
    // die Helferzeilen ... es muss fuer alle die Werte speichern, jedes
    // command." Auf seinem Bild stehen SET_PARM5, SET_PARM9 und SET_PARM16
    // in den drei Spalten eines `if`, waehrend die Felder noch `exp1`,
    // `< > ! =` und `exp2` sagen - also gar keinen Ausdruck enthalten. Ins
    // Skript kann das nicht, in die Einstellungen schon.
    // Lage und Zustand des PROGRAMMFENSTERS, als "showCmd l t r b".
    //
    // Das ist keine ImGui-Sache: ImGui zeichnet IN das Fenster, das Fenster
    // selbst gehoert Win32. Die richtige Stelle ist `WINDOWPLACEMENT`, und
    // laut Microsoft liefert sie beides auf einmal:
    //
    //   showCmd            SW_SHOWMAXIMIZED / SW_SHOWMINIMIZED / SW_SHOWNORMAL
    //   rcNormalPosition   die Groesse im WIEDERHERGESTELLTEN Zustand
    //
    // Also auch dann die richtige Groesse, wenn beim Beenden maximiert war.
    // Und `SetWindowPlacement` rueckt ein Fenster selbst zurecht, das sonst
    // ganz ausserhalb des Bildschirms laege - nach einem Monitorwechsel
    // oder einer anderen Aufloesung.
    //
    // Leer heisst "noch nie gespeichert": dann bleibt es beim Vorgabewert
    // des Systems, wie bisher.
    std::string windowPlacement;
    std::map<std::string, std::string> helfer;
    // Lesezeichen je Datei: Pfad -> Nummern der Zeilen in Dateireihenfolge
    // ("3,17,42"). Wie in Notepad++ bleiben sie ueber einen Neustart.
    std::map<std::string, std::string> lesezeichen;
    // Den Aenderungsrand (orange/gruen/blassblau) zeigen.
    bool changeHistory = true;
    // Beim Start im Hintergrund auf GitHub nach einer neueren Fassung sehen
    // (gui/update_win32.cpp). Gefunden wird nur angezeigt - installiert wird
    // erst auf Klick.
    bool updateCheck = true;
    // Befehle, die GENAU so aussehen wie der ausgewaehlte, schwach
    // hervorheben (View-Menue). Vorgabe aus - shank: "I probably won't want
    // it on all the time".
    bool highlightSame = false;
    float splitEvents = 0.339F;
    // Derselbe Anteil, aber fuer die Kartenansicht. Eine 3D-Ansicht braucht
    // mehr Platz als eine Befehlsliste.
    float splitMap = 0.55F;
    // Eigener Wert fuer die MODELLANSICHT.
    //
    // Vorher teilte sie sich `splitMap` mit der Karte: `leftMode != 0 ?
    // splitMap : splitEvents`. Wer im Modus Map zog und dann auf Model
    // wechselte, fand dort dieselbe Stellung - und beim Zurueckwechseln war
    // die eigene ueberschrieben.
    //
    // shank: "die Position von Script Flow teilt sich mit allen 3 Modi eine
    // Position, und das ist das Problem. Jedes muss seine eigene haben."
    float splitModel = 0.55F;
    // --- Was der Anwender eingestellt hat, bleibt eingestellt -------------
    //
    // Gemeldet: "kann er nicht speichern wie weit ich was gezogen habe beim
    // naechsten oeffnen". Zu Recht - die Spaltenanteile lagen schon hier,
    // die neueren Masse aber nur im Programmzustand und waren nach jedem
    // Start wieder auf Vorgabe.
    //
    // Hoehe der Zeitleiste, als Anteil der Kartenspalte.
    float timelineFrac = 0.22F;
    // Zeitleiste in Bildern statt Sekunden?
    bool timelineFrames = false;
    // Die Einstellungen rechts neben der Ansicht: auf oder zu?
    bool mapSidebar = true;
    // Die Trennlinien im Raster der geteilten Ansicht.
    float splitFracX = 0.5F;
    float splitFracY = 0.5F;
    // Anteil der Fensterhoehe fuer die Statusliste. Vorher waren es feste
    // sechs Zeilen - wer Pruefmeldungen liest, braucht mehr, wer an der
    // Karte arbeitet, weniger.
    float splitStatus = 0.18F;
    // Anteil der Aktionen-Spalte an der Fensterbreite.
    // Vorgabe aus Dialog 102: 71 / 663 = 0,107. Der gemessene Textbedarf ist
    // die Untergrenze; darunter wird sie nicht schmaler.
    float splitButtons = 0.107F;
    bool showTypes = false;
    bool gFloats = false;
    bool foldMacros = true;
    std::vector<std::string> recent;   // zuletzt geoeffnet, neueste zuerst

    // Eigene Tastenbelegungen.
    //
    // Gespeichert wird die ZAHL eines ImGuiKeyChord, nicht der Name.
    // imgui.h sagt zu GetKeyName ausdruecklich: "These names are provided
    // for debugging purposes and are not meant to be saved persistently nor
    // compared." Ein Name aus einer anderen Fassung oder einem anderen
    // Tastaturlayout waere also nicht verlaesslich zurueckzulesen.
    //
    // Der Aktionsname dagegen ist unsere eigene, feste Kennung
    // (keys::bindable()). Steht eine Aktion NICHT in dieser Liste, gilt die
    // Vorgabe - so bleibt die Datei klein und eine neue Aktion bekommt
    // automatisch ihre Vorgabe.
    //
    // Ein Eintrag mit chord 0 heisst ausdruecklich "gar keine Taste".
    struct KeyBinding {
        std::string action;   // "MoveUp"
        int chord = 0;        // ImGuiKeyChord
    };
    std::vector<KeyBinding> keyBindings;

    // Wie das Original: MRU0 bis MRU19 in der Registry, also 20 Eintraege.
    static constexpr std::size_t kMaxRecent = 20;

    // Eine Datei in die Liste aufnehmen; Dubletten wandern nach vorn.
    void noteRecent(const std::string& path);
};

// Text der Einstellungsdatei erzeugen und lesen. Unbekannte Schluessel
// bleiben beim Lesen unbeachtet, statt die Datei abzuweisen - eine aeltere
// Fassung des Programms soll eine neuere Datei ueberstehen.
[[nodiscard]] std::string writeSettings(const Settings& s);
[[nodiscard]] bool readSettings(const std::string& text, Settings& out);

[[nodiscard]] bool loadSettingsFile(const std::string& path, Settings& out);
[[nodiscard]] bool saveSettingsFile(const std::string& path, const Settings& s);

}  // namespace bhed
#endif
