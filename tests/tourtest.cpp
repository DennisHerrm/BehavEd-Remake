// tourtest.cpp - die Tutorials im Programm ohne Oberflaeche (Paket tour, rc601)
//
// Geprueft wird, was sich ohne Fenster pruefen laesst:
//   a) i18n: Kennung <-> Text (strName / strVonName), die Grundlage fuer
//      {{Kennung}} in den Tour-Texten
//   b) JSON lesen und schreiben (bhed/json.h, frueher nur in update.cpp)
//   c) Zielangaben, Pruefungen, Aktionen, Text (bhed/tour.h)
//   d) der Ablauf mit einem nachgebauten Programmzustand: weiter, zurueck
//      (ohne sofort wieder weiterzuschalten), ueberspringen, manuell
//   e) Fortschritt ueber die Einstellungen (tour.<id>=...)
//   f) touren.json lesen; ist handbuch/<sprache>/touren.json erzeugt (Argument
//      1 = Handbuchordner), alle vier Sprachen pruefen
//   g) Suche im Handbuch (suche.js)
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "bhed/i18n.h"
#include "bhed/json.h"
#include "bhed/settings.h"
#include "bhed/tour.h"

namespace {

int fehler = 0;

void expect(const char* was, bool ok) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", was);
    if (!ok) { ++fehler; }
}

void i18nProben() {
    using bhed::i18n::Str;
    std::printf("i18n: Kennung <-> Text\n");
    Str s = Str::AppTitle;
    expect("strVonName(\"FileSave\") -> Str::FileSave", bhed::i18n::strVonName("FileSave", &s) && s == Str::FileSave);
    expect("strName(Str::FileSave) == \"FileSave\"", std::string(bhed::i18n::strName(Str::FileSave)) == "FileSave");
    s = Str::AppTitle;
    expect("unbekannter Name -> false, Ausgabe unberuehrt",
           !bhed::i18n::strVonName("GibtEsNicht", &s) && s == Str::AppTitle);
    expect("leerer Name -> false", !bhed::i18n::strVonName("", &s));
    expect("Gross/klein zaehlt (\"filesave\" ist kein Name)", !bhed::i18n::strVonName("filesave", &s));
    bool alle = true;
    for (std::size_t k = 0; k < static_cast<std::size_t>(Str::Count); ++k) {
        const Str x = static_cast<Str>(k);
        Str y = Str::AppTitle;
        if (!bhed::i18n::strVonName(bhed::i18n::strName(x), &y) || y != x) {
            std::printf("    Rundlauf bricht bei %zu (%s)\n", k, bhed::i18n::strName(x));
            alle = false;
            break;
        }
    }
    expect("jede Kennung findet sich ueber ihren Namen wieder", alle);
    expect("strName ausserhalb der Tabelle -> \"\"", std::string(bhed::i18n::strName(Str::Count)).empty());
}

void jsonProben() {
    using bhed::json::Wert;
    std::printf("JSON: lesen und schreiben\n");
    Wert w;
    std::string f;
    const std::string text =
        R"({"a": [1, -2.5, 3e2, true, false, null], "b": {"c": "x\"y\\z\n\u00c4\ud83e\udd16"}, "leer": {}, "l": []})";
    expect("lies: verschachteltes Objekt", bhed::json::lies(text, w, &f) && w.art == Wert::Art::Objekt);
    const Wert* a = w.feld("a");
    expect("lies: Liste mit Zahlen, Wahrheitswerten und null",
           a != nullptr && a->art == Wert::Art::Liste && a->liste.size() == 6 && a->liste[0].zahl == 1.0 &&
               a->liste[1].zahl == -2.5 && a->liste[2].zahl == 300.0 && a->liste[3].wahr() && !a->liste[4].wahr() &&
               a->liste[5].art == Wert::Art::Null);
    const Wert* b = w.feld("b");
    expect("lies: Escapes (Anfuehrung, Backslash, Zeilenende, Umlaut, Ersatzpaar)",
           b != nullptr && b->textVon("c") == "x\"y\\z\n\xC3\x84\xF0\x9F\xA4\x96");
    expect("lies: leeres Objekt und leere Liste",
           w.feld("leer") != nullptr && w.feld("leer")->objekt.empty() && w.feld("l") != nullptr &&
               w.feld("l")->art == Wert::Art::Liste && w.feld("l")->liste.empty());
    expect("zahlVon/wahrVon mit Vorgabe",
           w.zahlVon("fehlt", 7.0) == 7.0 && !w.wahrVon("fehlt") && w.wahrVon("fehlt", true));
    expect("lies: Reste nach dem Wert -> false mit Meldung", !bhed::json::lies("{} x", w, &f) && !f.empty());
    expect("lies: abgebrochen -> false", !bhed::json::lies(R"({"a": [1, 2)", w));
    expect("lies: leer -> false", !bhed::json::lies("", w));
    std::string tief;
    for (int k = 0; k < 100; ++k) { tief += "["; }
    for (int k = 0; k < 100; ++k) { tief += "]"; }
    expect("lies: mehr als 64 Ebenen -> false (kein Stapelueberlauf)", !bhed::json::lies(tief, w));

    // Schreiben: was geschrieben wird, liest sich gleich zurueck.
    Wert o = Wert::alsObjekt();
    const std::string heikel = "Zeile 1\nZeile 2 \"zitiert\" \\ \x01 \xE4\xB8\xAD";
    o.setze("text", Wert::alsText(heikel));
    o.setze("ganz", Wert::alsZahl(42));
    o.setze("halb", Wert::alsZahl(0.5));
    o.setze("neg", Wert::alsZahl(-3));
    o.setze("ja", Wert::alsBool(true));
    Wert l = Wert::alsListe();
    l.liste.push_back(Wert::alsText("a"));
    l.liste.push_back(Wert{});
    o.setze("liste", l);
    o.setze("ganz", Wert::alsZahl(43));   // ersetzt, haengt nicht an
    const std::string geschrieben = bhed::json::schreibe(o);
    std::printf("    %s\n", geschrieben.c_str());
    Wert zurueck;
    expect("schreibe: liest sich zurueck", bhed::json::lies(geschrieben, zurueck, &f));
    expect("schreibe: Text mit Steuerzeichen, Anfuehrung, Backslash und UTF-8 unveraendert",
           zurueck.textVon("text") == heikel);
    expect("schreibe: ganze Zahlen ohne Komma, halbe genau",
           geschrieben.find("\"ganz\":43") != std::string::npos &&
               geschrieben.find("\"halb\":0.5") != std::string::npos &&
               geschrieben.find("\"neg\":-3") != std::string::npos);
    expect("setze ersetzt ein vorhandenes Feld", o.objekt.size() == 6 && zurueck.zahlVon("ganz") == 43.0);
    expect("schreibe: Liste mit null, true",
           geschrieben.find("[\"a\",null]") != std::string::npos &&
               geschrieben.find("\"ja\":true") != std::string::npos);
    expect("zeichenkette: Steuerzeichen als \\u0001, Zeilenende als \\n",
           bhed::json::zeichenkette("a\x01\n") == "\"a\\u0001\\n\"");
}

// --- c) Zielangaben, Pruefungen, Aktionen, Text -------------------------------
void zielProben() {
    using bhed::tour::Ziel;
    std::printf("Zielangaben\n");
    Ziel z;
    std::string f;
    expect("knopf:FileSave", bhed::tour::parseZiel("knopf:FileSave", z, &f) && z.art == Ziel::Art::Knopf &&
                                 z.teile.size() == 1 && z.teile[0] == "FileSave" && z.fenster.empty());
    expect("knopf:EditorOk@###editor (Fenster)",
           bhed::tour::parseZiel("knopf:EditorOk@###editor", z, &f) && z.fenster == "###editor");
    expect("knopfN:##v@###editor#1 (woertlich, Fenster, Nummer)",
           bhed::tour::parseZiel("knopfN:##v@###editor#1", z, &f) && z.art == Ziel::Art::KnopfN &&
               z.teile[0] == "##v" && z.fenster == "###editor" && z.nte == 1);
    expect("menue:MenuFile>FileNew (Kette)", bhed::tour::parseZiel("menue:MenuFile>FileNew", z, &f) &&
                                                 z.art == Ziel::Art::Menue && z.teile.size() == 2 &&
                                                 z.teile[0] == "MenuFile" && z.teile[1] == "FileNew");
    expect("gruppe:ActMoveUp,ActMoveDown", bhed::tour::parseZiel("gruppe:ActMoveUp,ActMoveDown", z, &f) &&
                                               z.art == Ziel::Art::Gruppe && z.teile.size() == 2);
    expect("eintrag:affect", bhed::tour::parseZiel("eintrag:affect", z, &f) && z.art == Ziel::Art::Eintrag &&
                                 z.teile[0] == "affect");
    expect("zeile:affect#2", bhed::tour::parseZiel("zeile:affect#2", z, &f) && z.art == Ziel::Art::Zeile &&
                                 z.teile[0] == "affect" && z.nte == 2);
    expect("reiter:+", bhed::tour::parseZiel("reiter:+", z, &f) && z.art == Ziel::Art::Reiter);
    expect("fenster:###editor", bhed::tour::parseZiel("fenster:###editor", z, &f) && z.art == Ziel::Art::Fenster &&
                                    z.teile[0] == "###editor");
    expect("baum und meldungen ohne Argument", bhed::tour::parseZiel("baum", z, &f) && z.art == Ziel::Art::Baum &&
                                                   bhed::tour::parseZiel("meldungen", z, &f) &&
                                                   z.art == Ziel::Art::Meldungen);
    expect("unbekannte Art -> false mit Meldung", !bhed::tour::parseZiel("quatsch:x", z, &f) && !f.empty());
    expect("leeres Argument -> false", !bhed::tour::parseZiel("knopf:", z, &f) && !bhed::tour::parseZiel("zeile:", z, &f));
    expect("kaputte Nummer -> false", !bhed::tour::parseZiel("zeile:affect#x", z, &f));
    std::vector<Ziel> zs;
    expect("mehrere Ziele mit ;", bhed::tour::parseZiele("zeile:affect#0; eintrag:wait", zs, &f) && zs.size() == 2 &&
                                      zs[1].art == Ziel::Art::Eintrag);
    expect("leere Zielangabe = keine Ziele", bhed::tour::parseZiele("", zs, &f) && zs.empty());
    (void)bhed::tour::parseZiel("menue:MenuFile>FileNew", z, &f);
    const std::vector<std::string> k = bhed::tour::kennungenIn(z);
    expect("kennungenIn: Menuekette", k.size() == 2 && k[0] == "MenuFile");
    (void)bhed::tour::parseZiel("knopfN:##v", z, &f);
    expect("kennungenIn: woertlich hat keine", bhed::tour::kennungenIn(z).empty());
}

void pruefungProben() {
    using bhed::tour::Kontext;
    std::printf("Pruefungen\n");
    std::string f;
    expect("bekannt: reiter+|leer", bhed::tour::pruefungBekannt("reiter+|leer", &f));
    expect("bekannt: befehl:affect+, kind:affect>wait, wert:affect#0:0",
           bhed::tour::pruefungBekannt("befehl:affect+", &f) && bhed::tour::pruefungBekannt("kind:affect>wait", &f) &&
               bhed::tour::pruefungBekannt("wert:affect#0:0", &f));
    expect("bekannt: editor:affect, editorzu, fenster:messages, modus:0, manuell",
           bhed::tour::pruefungBekannt("editor:affect", &f) && bhed::tour::pruefungBekannt("editorzu", &f) &&
               bhed::tour::pruefungBekannt("fenster:messages", &f) && bhed::tour::pruefungBekannt("modus:0", &f) &&
               bhed::tour::pruefungBekannt("manuell", &f));
    expect("unbekannt: foo", !bhed::tour::pruefungBekannt("foo", &f) && !f.empty());
    expect("unbekannt: befehl ohne Namen, kind ohne >, wert ohne Feld",
           !bhed::tour::pruefungBekannt("befehl", &f) && !bhed::tour::pruefungBekannt("kind:affect", &f) &&
               !bhed::tour::pruefungBekannt("wert:affect#0", &f));
    expect("unbekannt: + an einem Zustand (leer+)", !bhed::tour::pruefungBekannt("leer+", &f));
    expect("leere Pruefung ist unbekannt", !bhed::tour::pruefungBekannt("", &f));

    Kontext a;
    Kontext b;
    a.reiter = 1;
    b.reiter = 2;
    expect("reiter+: 1 -> 2", bhed::tour::erfuellt("reiter+", b, a));
    expect("reiter+: 2 -> 2 nicht", !bhed::tour::erfuellt("reiter+", b, b));
    expect("befehl:affect ohne affect nicht", !bhed::tour::erfuellt("befehl:affect", b, a));
    b.befehle["affect"] = 1;
    expect("befehl:affect mit einem affect", bhed::tour::erfuellt("befehl:affect", b, a));
    a.befehle["affect"] = 1;
    expect("befehl:affect+ bei gleicher Zahl nicht", !bhed::tour::erfuellt("befehl:affect+", b, a));
    b.befehle["affect"] = 2;
    expect("befehl:affect+ bei mehr", bhed::tour::erfuellt("befehl:affect+", b, a));
    b.leer = false;
    expect("leer: nicht bei einem Skript mit Befehlen", !bhed::tour::erfuellt("leer", b, a));
    expect("Alternativen: reiter+|leer", bhed::tour::erfuellt("leer|reiter+", b, a));
    b.editorOffen = true;
    b.editorBefehl = "affect";
    expect("editor und editor:affect", bhed::tour::erfuellt("editor", b, a) && bhed::tour::erfuellt("editor:affect", b, a));
    expect("editor:wait nicht, editorzu nicht",
           !bhed::tour::erfuellt("editor:wait", b, a) && !bhed::tour::erfuellt("editorzu", b, a));
    b.fenster = {"messages"};
    expect("fenster:messages", bhed::tour::erfuellt("fenster:messages", b, a) && !bhed::tour::erfuellt("fenster:find", b, a));
    b.modus = 1;
    expect("modus:1, nicht modus:0", bhed::tour::erfuellt("modus:1", b, a) && !bhed::tour::erfuellt("modus:0", b, a));
    b.auswahl = "affect";
    b.auswahlZahl = 1;
    expect("auswahl und auswahl:affect", bhed::tour::erfuellt("auswahl", b, a) && bhed::tour::erfuellt("auswahl:affect", b, a) &&
                                             !bhed::tour::erfuellt("auswahl:wait", b, a));
    b.hatKind = [](const std::string& blk, const std::string& bef) { return blk == "affect" && bef == "wait"; };
    expect("kind:affect>wait ueber das Skript", bhed::tour::erfuellt("kind:affect>wait", b, a) &&
                                                    !bhed::tour::erfuellt("kind:affect>set", b, a));
    std::string wert = "DEFAULT";
    b.argument = [&wert](const std::string& bef, int n, int feld) {
        return (bef == "affect" && n == 0 && feld == 0) ? wert : std::string();
    };
    expect("wert: DEFAULT gilt als nicht gesetzt", !bhed::tour::erfuellt("wert:affect#0:0", b, a));
    wert = "";
    expect("wert: leer gilt als nicht gesetzt", !bhed::tour::erfuellt("wert:affect#0:0", b, a));
    wert = "kyle";
    expect("wert: kyle ist gesetzt", bhed::tour::erfuellt("wert:affect#0:0", b, a));
    expect("manuell ist nie von selbst erfuellt", !bhed::tour::erfuellt("manuell", b, a));
    b.schwebt = true;
    expect("schwebt", bhed::tour::erfuellt("schwebt|befehl:wait", b, a));
    a.uebersetzt = 10.0;
    b.uebersetzt = 12.5;
    a.gespeichert = 0;
    b.gespeichert = 1;
    a.knoten = 3;
    b.knoten = 4;
    a.rem = 0;
    b.rem = 2;
    a.aenderungen = 5;
    b.aenderungen = 6;
    expect("Zaehler: uebersetzt+, gespeichert+, knoten+, rem+, aenderung+",
           bhed::tour::erfuellt("uebersetzt+", b, a) && bhed::tour::erfuellt("gespeichert+", b, a) &&
               bhed::tour::erfuellt("knoten+", b, a) && bhed::tour::erfuellt("rem+", b, a) &&
               bhed::tour::erfuellt("aenderung+", b, a) && !bhed::tour::erfuellt("knoten+", a, a));
    b.aktivGesichert = true;
    expect("gespeichert (Zustand)", bhed::tour::erfuellt("gespeichert", b, a) && !bhed::tour::erfuellt("gespeichert", a, a));
    expect("istZustand: leer ja, reiter+ nein, gemischt nein",
           bhed::tour::istZustand("leer") && bhed::tour::istZustand("befehl:affect") &&
               !bhed::tour::istZustand("reiter+") && !bhed::tour::istZustand("reiter+|leer") &&
               !bhed::tour::istZustand("manuell"));
    const bhed::tour::Voraus v = bhed::tour::zerlegeVoraus("modus:0 < knopf:ViewEvents@modebar");
    expect("Vorbedingung zerlegen", v.pruefung == "modus:0" && v.oeffner == "knopf:ViewEvents@modebar");
    const bhed::tour::Voraus v2 = bhed::tour::zerlegeVoraus("editor");
    expect("Vorbedingung ohne Oeffner", v2.pruefung == "editor" && v2.oeffner.empty());
}

void aktionProben() {
    std::printf("Aktionen und Text\n");
    std::vector<bhed::tour::Aktion> a;
    std::string f;
    expect("Aktionen: waehle:affect#0; befehl:wait",
           bhed::tour::parseAktionen("waehle:affect#0; befehl:wait", a, &f) && a.size() == 2 && a[0].name == "waehle" &&
               a[0].arg == "affect#0" && a[1].name == "befehl" && a[1].arg == "wait");
    expect("Aktionen: wert mit = im Text", bhed::tour::parseAktionen("wert:affect#0:0=kyle", a, &f) && a.size() == 1 &&
                                               a[0].arg == "affect#0:0=kyle");
    expect("Aktionen: neuerReiter, editorzu ohne Argument",
           bhed::tour::parseAktionen("neuerReiter;editorzu", a, &f) && a.size() == 2);
    expect("Aktionen: unbekannt -> false", !bhed::tour::parseAktionen("formatiereFestplatte", a, &f) && !f.empty());
    expect("Aktionen: befehl ohne Namen -> false", !bhed::tour::parseAktionen("befehl:", a, &f));
    std::string bef;
    int n = -1;
    bhed::tour::befehlUndNummer("affect#2", bef, n);
    expect("befehlUndNummer: affect#2", bef == "affect" && n == 2);
    bhed::tour::befehlUndNummer("wait", bef, n);
    expect("befehlUndNummer: wait -> 0", bef == "wait" && n == 0);

    const std::vector<bhed::tour::Stueck> st = bhed::tour::zerlege(
        "**Open.** Click **{{FileSave}}** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) `my.txt` [[k-neues-skript|here]][[tipps]]{{icon:Neu}}.");
    using A = bhed::tour::Stueck::Art;
    bool ok = st.size() >= 9;
    if (ok) {
        ok = st[0].art == A::Text && st[0].inhalt == "Open." && st[0].fett && st[1].art == A::Text &&
             st[1].inhalt == " Click " && !st[1].fett && st[2].art == A::Kennung && st[2].inhalt == "FileSave" &&
             st[2].fett;
    }
    bool taste = false;
    bool code = false;
    bool link = false;
    std::string alles;
    for (const auto& x : st) {
        if (x.art == A::Taste && x.inhalt == "Ctrl") { taste = true; }
        if (x.art == A::Code && x.inhalt == "my.txt") { code = true; }
        if (x.art == A::Text && x.inhalt.find("here") != std::string::npos) { link = true; }
        alles += x.inhalt;
    }
    expect("zerlege: fett, Kennung in fett", ok);
    expect("zerlege: Taste, Code, Verweistext", taste && code && link);
    expect("zerlege: [[seite]] und {{icon:..}} fallen weg",
           alles.find("tipps") == std::string::npos && alles.find("icon") == std::string::npos &&
               alles.find("[[") == std::string::npos);
    const std::vector<std::string> ks = bhed::tour::kennungenImText("a {{MenuFile}} b **{{FileNew}}** {{icon:Neu}}");
    expect("kennungenImText", ks.size() == 2 && ks[0] == "MenuFile" && ks[1] == "FileNew");
}

// --- d) Ablauf -------------------------------------------------------------------
bhed::tour::Tour probeTour() {
    bhed::tour::Tour t;
    t.id = "probe";
    t.titel = "Probe";
    std::vector<bhed::tour::Schritt> s(4);
    s[0].text = "Neuer Reiter";
    s[0].fertig = "reiter+";
    s[0].einrichten = "neuerReiter";
    s[0].sicher = true;
    s[1].text = "affect einfuegen";
    s[1].fertig = "befehl:affect";
    s[1].einrichten = "befehl:affect";
    s[2].text = "Ansehen";
    s[2].fertig = "manuell";
    s[3].text = "Sichern";
    s[3].fertig = "gespeichert";
    s[3].sicher = false;
    s[3].einrichten = "editorzu";
    t.ansichten.emplace_back("classic", s);
    return t;
}

void ablaufProben() {
    using bhed::tour::Ablauf;
    using bhed::tour::Kontext;
    std::printf("Ablauf\n");
    const bhed::tour::Tour t = probeTour();
    expect("Ansicht graph faellt auf classic zurueck", t.schritte("graph") == t.schritte("classic") && t.schritte("classic") != nullptr);
    Ablauf a;
    expect("starte bei Schritt 0", a.starte(t, "classic") && a.aktiv() && a.schritt() == 0 && a.anzahl() == 4);
    Kontext k;
    k.reiter = 1;
    double zeit = 0.0;
    expect("erstes Bild nimmt nur den Stand auf", !a.bild(k, zeit) && !a.erfuellt());
    k.reiter = 2;
    zeit += 0.1;
    expect("reiter 1 -> 2: Schritt 1 erfuellt", a.bild(k, zeit) && a.erfuellt() && a.schritt() == 0);
    zeit += 0.3;
    (void)a.bild(k, zeit);
    expect("vor der Verzoegerung noch Schritt 1", a.schritt() == 0);
    zeit += 0.4;
    (void)a.bild(k, zeit);
    expect("nach 0,6 s von selbst Schritt 2", a.schritt() == 1 && a.stand(0) == Ablauf::Stand::Erledigt);
    // Zurueck: neuer Stand beim Beginn - kein sofortiges Weiterschalten.
    a.zurueck();
    expect("Zurueck: wieder Schritt 1, als offen", a.schritt() == 0 && a.stand(0) == Ablauf::Stand::Offen && a.zurueckGeholt());
    for (int i = 0; i < 5; ++i) {
        zeit += 0.2;
        (void)a.bild(k, zeit);
    }
    expect("Zurueck: schaltet bei gleichem Stand NICHT sofort wieder weiter", a.schritt() == 0 && !a.erfuellt());
    k.reiter = 3;
    zeit += 0.1;
    expect("Zurueck: ein weiterer neuer Reiter erfuellt ihn wieder", a.bild(k, zeit) && a.erfuellt());
    zeit += 0.7;
    (void)a.bild(k, zeit);
    expect("weiter zu Schritt 2", a.schritt() == 1);
    // Schritt 2 ist ein Zustand: schon erfuellt beim Betreten -> erledigt.
    k.befehle["affect"] = 1;
    zeit += 0.1;
    (void)a.bild(k, zeit);   // Stand aufnehmen
    zeit += 0.1;
    expect("Zustand schon erfuellt beim Betreten: gilt als erledigt", a.bild(k, zeit) || a.erfuellt());
    zeit += 0.7;
    (void)a.bild(k, zeit);
    expect("weiter zu Schritt 3 (manuell)", a.schritt() == 2);
    // Zurueck zu Schritt 2: der Zustand gilt noch - aber nach "Zurueck" erst
    // nach einer neuen Erfuellung (Flanke).
    a.zurueck();
    for (int i = 0; i < 4; ++i) {
        zeit += 0.2;
        (void)a.bild(k, zeit);
    }
    expect("Zurueck auf einen Zustand: kein sofortiges Weiterschalten", a.schritt() == 1 && !a.erfuellt());
    k.befehle["affect"] = 0;
    zeit += 0.1;
    (void)a.bild(k, zeit);
    k.befehle["affect"] = 1;
    zeit += 0.1;
    expect("Zurueck auf einen Zustand: nach falsch -> wahr erfuellt", a.bild(k, zeit));
    a.weiter();
    expect("Weiter von Hand nach Erfuellung", a.schritt() == 2);
    for (int i = 0; i < 10; ++i) {
        zeit += 0.2;
        (void)a.bild(k, zeit);
    }
    expect("manuell: bleibt stehen", a.schritt() == 2 && !a.erfuellt());
    a.weiter();
    expect("manuell: Weiter schaltet", a.schritt() == 3);
    expect("Mach es: nur bei sicheren Schritten", a.machEs().empty());
    const std::string e = a.ueberspringen();
    expect("Ueberspringen: gibt die Aktionen zurueck, markiert, Tour fertig",
           e == "editorzu" && a.stand(3) == Ablauf::Stand::Uebersprungen && a.fertig());
    Ablauf b;
    expect("starte ab Schritt 2", b.starte(t, "classic", 1) && b.schritt() == 1);
    expect("starte hinter dem Ende -> false", !b.starte(t, "classic", 9));
    Ablauf c;
    (void)c.starte(t, "classic");
    expect("Mach es bei einem sicheren Schritt: seine Aktionen", c.machEs() == "neuerReiter");
    expect("Ueberspringen in der Mitte: weiter zu Schritt 2", c.ueberspringen() == "neuerReiter" && c.schritt() == 1 &&
                                                                c.stand(0) == Ablauf::Stand::Uebersprungen && !c.fertig());
    c.schliesse();
    expect("schliesse: nicht mehr aktiv", !c.aktiv() && c.aktuell() == nullptr);
}

// --- e) Fortschritt -----------------------------------------------------------------
void standProben() {
    std::printf("Fortschritt\n");
    int n = 0;
    int z = 0;
    bool fertig = false;
    expect("standText 3/7", bhed::tour::standText(3, 7, false) == "3/7");
    expect("standText fertig", bhed::tour::standText(7, 7, true) == "fertig");
    expect("leseStand 3/7", bhed::tour::leseStand("3/7", n, z, fertig) && n == 3 && z == 7 && !fertig);
    expect("leseStand fertig", bhed::tour::leseStand("fertig", n, z, fertig) && fertig);
    expect("leseStand kaputt -> false", !bhed::tour::leseStand("x/7", n, z, fertig) &&
                                            !bhed::tour::leseStand("9/7", n, z, fertig) &&
                                            !bhed::tour::leseStand("", n, z, fertig));
    bhed::Settings s;
    s.tourStand["k-neues-skript"] = "3/7";
    s.tourStand["k-uebersetzen"] = "fertig";
    bhed::Settings r;
    expect("Einstellungen: Fortschritt ueberlebt Schreiben und Lesen",
           bhed::readSettings(bhed::writeSettings(s), r) && r.tourStand == s.tourStand);
    expect("Einstellungen: Zeile tour.k-neues-skript=3/7",
           bhed::writeSettings(s).find("tour.k-neues-skript=3/7\n") != std::string::npos);
}

// --- f) touren.json -------------------------------------------------------------------
const char* const kProbeJson = R"({"fassung": 1, "sprache": "en", "touren": [
 {"id": "k-probe", "gruppe": "tut_skript", "titel": "Probe", "kurz": "kurz", "seite": "k-probe.html",
  "ansichten": {"classic": [
    {"text": "Click **+**", "bild": "s_neu_plus", "unter": "u", "ziel": "reiter:+", "fertig": "reiter+|leer",
     "voraus": "", "einrichten": "neuerReiter", "sicher": true},
    {"text": "Find {{GroupEvents}}", "bild": "", "unter": "", "ziel": "eintrag:affect", "fertig": "schwebt|befehl:affect",
     "voraus": "modus:0 < knopf:ViewEvents@modebar", "einrichten": "modus:0", "sicher": true}
  ]}}
]})";

bool ersetzt(std::string s, const std::string& alt, const std::string& neu, bhed::tour::Touren& t, std::string& f) {
    const std::size_t p = s.find(alt);
    if (p == std::string::npos) { return true; }   // dann pruefte der Test nichts - als Fehler werten
    s.replace(p, alt.size(), neu);
    return bhed::tour::ladeTouren(s, t, &f);
}

void ladeProben() {
    std::printf("touren.json lesen\n");
    bhed::tour::Touren t;
    std::string f;
    expect("Probe-JSON liest sich", bhed::tour::ladeTouren(kProbeJson, t, &f) && t.liste.size() == 1 && t.fassung == 1);
    const bhed::tour::Tour* p = t.finde("k-probe");
    expect("finde und Schritte", p != nullptr && p->schritte("classic") != nullptr && p->schritte("classic")->size() == 2 &&
                                     (*p->schritte("classic"))[0].sicher && (*p->schritte("classic"))[0].ziel == "reiter:+");
    expect("unbekannte Zielart -> false", !ersetzt(kProbeJson, "reiter:+", "quatsch:+", t, f));
    std::printf("    (%s)\n", f.c_str());
    expect("unbekannte Pruefung -> false", !ersetzt(kProbeJson, "reiter+|leer", "reiter+|zaubern", t, f));
    expect("unbekannte Aktion -> false", !ersetzt(kProbeJson, "\"neuerReiter\"", "\"rm -rf\"", t, f));
    expect("unbekannte Vorbedingung -> false", !ersetzt(kProbeJson, "modus:0 <", "zaubern <", t, f));
    expect("kaputter Oeffner -> false", !ersetzt(kProbeJson, "< knopf:ViewEvents@modebar", "< knopf:", t, f));
    std::string doppelt = kProbeJson;
    const std::size_t a = doppelt.find("{\"id\"");
    const std::size_t e = doppelt.rfind("]}");
    doppelt.insert(e, "," + doppelt.substr(a, e - a));
    expect("doppelte Kennung -> false", !bhed::tour::ladeTouren(doppelt, t, &f));
    expect("kein JSON -> false", !bhed::tour::ladeTouren("touren", t, &f));
}

std::string datei(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) { return {}; }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Die Kennungen eines Textes und einer Zielangabe muessen alle existieren.
bool kennungenGibtEs(const bhed::tour::Schritt& s, std::string& fehlt) {
    std::vector<std::string> k = bhed::tour::kennungenImText(s.text);
    const std::vector<std::string> ku = bhed::tour::kennungenImText(s.unter);
    k.insert(k.end(), ku.begin(), ku.end());
    std::vector<bhed::tour::Ziel> zs;
    std::string f;
    (void)bhed::tour::parseZiele(s.ziel, zs, &f);
    const bhed::tour::Voraus v = bhed::tour::zerlegeVoraus(s.voraus);
    std::vector<bhed::tour::Ziel> oz;
    (void)bhed::tour::parseZiele(v.oeffner, oz, &f);
    zs.insert(zs.end(), oz.begin(), oz.end());
    for (const auto& z : zs) {
        const std::vector<std::string> kz = bhed::tour::kennungenIn(z);
        k.insert(k.end(), kz.begin(), kz.end());
    }
    for (const std::string& n : k) {
        bhed::i18n::Str x{};
        if (!bhed::i18n::strVonName(n, &x)) {
            fehlt = n;
            return false;
        }
    }
    return true;
}

// --- rc601 tour (Review): "Mach es" erledigt den Schritt, "Ueberspringen" tut ihn nicht ---
//
// Gemeldet (Code-Pruefung des Pakets): "Do it for me" auf k-neues-skript
// Schritt 2 (Einrichten "modus:0", fertig "schwebt|befehl:affect") konnte den
// Schritt nie erledigen; "Skip" fuehrte immer `einrichten` aus - bei
// k-befehl-einfuegen setzte es ein streunendes `wait` in das aktive Skript,
// bei k-neues-skript Schritt 4 schrieb es den Wert des ersten affect um.
//
// Geprueft an einem grob nachgebauten Programmzustand (echt macht es
// gui/tour.cpp, aktionenAusfuehren): jede Tour von vorn, je Schritt
//   - "Mach es" (wenn angeboten) muss die Pruefung des Schritts erfuellen;
//   - "Ueberspringen" darf einen reinen Zaehlerschritt (nur "...+") nicht
//     selbst erledigen und schreibt nie Werte (wert:) - es richtet nur ein,
//     was der naechste Schritt braucht.
// Danach geht die Probe weiter wie ein Anwender, der den Schritt mit "Mach
// es" erledigt oder ihn ueberspringt.
struct Modell {
    bhed::tour::Kontext k;
    std::map<std::string, std::string> werte;              // "affect#0:0" -> Text
    std::set<std::pair<std::string, std::string>> kinder;  // (Block, Befehl)
    std::string gewaehlt;
};

bhed::tour::Kontext alsKontext(const Modell& m) {
    auto kopie = std::make_shared<Modell>(m);
    bhed::tour::Kontext k = m.k;
    k.hatKind = [kopie](const std::string& block, const std::string& befehl) {
        return kopie->kinder.count({block, befehl}) > 0;
    };
    k.argument = [kopie](const std::string& befehl, int nte, int feld) {
        const auto it = kopie->werte.find(befehl + "#" + std::to_string(nte) + ":" + std::to_string(feld));
        return it != kopie->werte.end() ? it->second : std::string();
    };
    return k;
}

// Was die Aktionen am Programmzustand aendern (Nachbau von aktionenAusfuehren).
void wende(Modell& m, const std::string& aktionen) {
    std::vector<bhed::tour::Aktion> as;
    std::string f;
    if (!bhed::tour::parseAktionen(aktionen, as, &f)) { return; }
    for (const bhed::tour::Aktion& a : as) {
        std::string b;
        int n = 0;
        if (a.name == "neuerReiter") {
            const int reiter = m.k.reiter + 1;
            const int modus = m.k.modus;
            m = Modell{};
            m.k.reiter = reiter;
            m.k.modus = modus;
            m.k.leer = true;
        } else if (a.name == "befehl") {
            // fuegeBefehlEin: hinter die gewaehlte Zeile, bei einem offenen
            // Block an seinen Anfang.
            ++m.k.befehle[a.arg];
            ++m.k.knoten;
            ++m.k.aenderungen;
            m.k.leer = false;
            if (!m.gewaehlt.empty()) { m.kinder.insert({m.gewaehlt, a.arg}); }
        } else if (a.name == "waehle") {
            bhed::tour::befehlUndNummer(a.arg, b, n);
            if (b == "*") { b = m.k.befehle.empty() ? std::string() : m.k.befehle.begin()->first; }
            if (!b.empty() && m.k.befehle.count(b) > 0) {
                m.gewaehlt = b;
                m.k.auswahl = b;
                m.k.auswahlZahl = 1;
            }
        } else if (a.name == "wert") {
            const std::size_t gl = a.arg.find('=');
            const std::string links = a.arg.substr(0, gl);
            const std::size_t dp = links.rfind(':');
            bhed::tour::befehlUndNummer(links.substr(0, dp), b, n);
            m.werte[b + "#" + std::to_string(n) + ":" + links.substr(dp + 1)] =
                gl == std::string::npos ? std::string() : a.arg.substr(gl + 1);
            ++m.k.aenderungen;
        } else if (a.name == "modus") {
            m.k.modus = std::atoi(a.arg.c_str());
        } else if (a.name == "ansicht") {
            m.k.ansicht = std::atoi(a.arg.c_str());
        } else if (a.name == "editor") {
            bhed::tour::befehlUndNummer(a.arg, b, n);
            m.k.editorOffen = true;
            if (b.empty()) { b = !m.gewaehlt.empty() ? m.gewaehlt : (m.k.befehle.empty() ? "" : m.k.befehle.begin()->first); }
            m.k.editorBefehl = b;
        } else if (a.name == "editorzu") {
            m.k.editorOffen = false;
        } else if (a.name == "fenster") {
            m.k.fenster.push_back(a.arg);
        }
    }
}

// Nur Zaehler mit "+" ("knoten+", "aenderung+|uebersetzt+")?
bool nurZaehler(const std::string& pruefung) {
    std::size_t a = 0;
    bool eins = false;
    while (a <= pruefung.size()) {
        std::size_t e = pruefung.find('|', a);
        if (e == std::string::npos) { e = pruefung.size(); }
        std::string t = pruefung.substr(a, e - a);
        while (!t.empty() && t.front() == ' ') { t.erase(0, 1); }
        while (!t.empty() && t.back() == ' ') { t.pop_back(); }
        if (t.empty() || t.back() != '+') { return false; }
        eins = true;
        a = e + 1;
    }
    return eins;
}

void aktionenProben(const bhed::tour::Touren& t, const std::string& sp) {
    bool machtEs = true;
    bool skipTutNicht = true;
    bool skipOhneWert = true;
    for (const bhed::tour::Tour& tour : t.liste) {
        for (const auto& [ansicht, schritte] : tour.ansichten) {
            // Ein Skript mit einer Zeile, Ansicht Classic, Modus Events.
            Modell m;
            m.k.reiter = 1;
            m.k.knoten = 1;
            m.k.leer = false;
            m.k.befehle["wait"] = 1;
            for (std::size_t i = 0; i < schritte.size(); ++i) {
                const bhed::tour::Schritt& s = schritte[i];
                bhed::tour::Ablauf a;
                bhed::tour::Ablauf b;
                if (!a.starte(tour, ansicht, static_cast<int>(i)) || !b.starte(tour, ansicht, static_cast<int>(i))) { continue; }
                const std::string mach = a.machEs();
                const std::string skip = b.ueberspringen();
                const bhed::tour::Kontext vorher = alsKontext(m);
                const std::string wo = sp + " " + tour.id + "/" + ansicht + " Schritt " + std::to_string(i + 1);
                if (!mach.empty()) {
                    Modell nach = m;
                    wende(nach, mach);
                    if (!bhed::tour::erfuellt(s.fertig, alsKontext(nach), vorher)) {
                        std::printf("    %s: \"Mach es\" (%s) erfuellt \"%s\" nicht\n", wo.c_str(), mach.c_str(), s.fertig.c_str());
                        machtEs = false;
                    }
                }
                Modell nachSkip = m;
                wende(nachSkip, skip);
                if (nurZaehler(s.fertig) && bhed::tour::erfuellt(s.fertig, alsKontext(nachSkip), vorher)) {
                    std::printf("    %s: \"Ueberspringen\" (%s) erledigt den Schritt (%s) selbst\n", wo.c_str(), skip.c_str(),
                                s.fertig.c_str());
                    skipTutNicht = false;
                }
                std::vector<bhed::tour::Aktion> as;
                std::string f;
                if (bhed::tour::parseAktionen(skip, as, &f)) {
                    for (const auto& x : as) {
                        if (x.name == "wert") {
                            std::printf("    %s: \"Ueberspringen\" schreibt einen Wert (%s)\n", wo.c_str(), skip.c_str());
                            skipOhneWert = false;
                        }
                    }
                }
                wende(m, !mach.empty() ? mach : skip);
            }
        }
    }
    expect((sp + ": \"Mach es\" erledigt jeden Schritt, bei dem es angeboten wird").c_str(), machtEs);
    expect((sp + ": \"Ueberspringen\" erledigt keinen Zaehlerschritt selbst (kein streunender Befehl)").c_str(), skipTutNicht);
    expect((sp + ": \"Ueberspringen\" schreibt keine Werte ins Skript").c_str(), skipOhneWert);
}

void handbuchProben(const std::string& ordner) {
    std::printf("handbuch/<sprache>/touren.json (%s)\n", ordner.c_str());
    if (datei(ordner + "/en/touren.json").empty()) {
        std::printf("  (nicht erzeugt - python tools/handbuch/gen_handbuch.py; uebersprungen)\n");
        return;
    }
    const char* const sprachen[] = {"en", "de", "zh", "ja"};
    const char* const erste[] = {"k-neues-skript", "k-befehl-einfuegen", "k-werte-bearbeiten", "k-zeilen-ordnen",
                                 "k-uebersetzen"};
    std::vector<std::size_t> schritteEn;
    for (const char* sp : sprachen) {
        bhed::tour::Touren t;
        std::string f;
        const bool ok = bhed::tour::ladeTouren(datei(ordner + "/" + sp + "/touren.json"), t, &f);
        if (!ok) { std::printf("    %s: %s\n", sp, f.c_str()); }
        expect((std::string(sp) + ": liest sich (Ziele, Pruefungen, Aktionen bekannt, Kennungen eindeutig)").c_str(), ok);
        if (!ok) { continue; }
        expect((std::string(sp) + ": Sprache stimmt").c_str(), t.sprache == sp);
        bool alleDa = true;
        std::vector<std::size_t> anzahl;
        for (const char* id : erste) {
            const bhed::tour::Tour* p = t.finde(id);
            const auto* s = p != nullptr ? p->schritte("classic") : nullptr;
            if (s == nullptr || s->empty()) {
                std::printf("    %s: %s fehlt oder ohne Schritte\n", sp, id);
                alleDa = false;
                anzahl.push_back(0);
                continue;
            }
            anzahl.push_back(s->size());
        }
        expect((std::string(sp) + ": die fuenf Skript-Tutorials sind da, jedes mit Schritten").c_str(), alleDa);
        bool kennungen = true;
        std::string fehlt;
        for (const auto& tour : t.liste) {
            for (const auto& [ansicht, schritte] : tour.ansichten) {
                for (const auto& s : schritte) {
                    if (!kennungenGibtEs(s, fehlt)) {
                        std::printf("    %s %s/%s: {{%s}} gibt es nicht\n", sp, tour.id.c_str(), ansicht.c_str(), fehlt.c_str());
                        kennungen = false;
                    }
                }
            }
        }
        expect((std::string(sp) + ": jede {{Kennung}} in Texten und Zielen gibt es im Programm").c_str(), kennungen);
        aktionenProben(t, sp);   // rc601 tour (Review)
        if (std::string(sp) == "en") {
            schritteEn = anzahl;
        } else {
            expect((std::string(sp) + ": gleich viele Schritte wie Englisch").c_str(), anzahl == schritteEn);
        }
    }
}

// --- g) Suche ----------------------------------------------------------------------------
void sucheProben(const std::string& ordner) {
    std::printf("Suche im Handbuch\n");
    const std::string js =
        "window.HANDBUCH_INDEX=[{\"p\":\"k-neues-skript.html\",\"a\":\"schritte\",\"t\":\"Write a script from scratch\","
        "\"s\":\"Steps\",\"x\":\"Click the + at the end of the tab bar. Double-click affect in the Events list.\"},"
        "{\"p\":\"k-uebersetzen.html\",\"a\":\"\",\"t\":\"Compile and fix errors\",\"s\":\"Compile and fix errors\","
        "\"x\":\"Press Compile. The ibi is written next to the txt.\"},"
        "{\"p\":\"k-karte.html\",\"a\":\"\",\"t\":\"\\u5730\\u56fe\",\"s\":\"\\u5730\\u56fe\",\"x\":\"\\u52a0\\u8f7d\\u5730\\u56fe\"}];\n"
        "window.HANDBUCH_TEXTE={\"keine_treffer\":\"No results\"};\n";
    std::vector<bhed::tour::Abschnitt> idx;
    std::string f;
    expect("suche.js liest sich", bhed::tour::ladeSuchindex(js, idx, &f) && idx.size() == 3 && idx[0].anker == "schritte");
    const std::vector<bhed::tour::Treffer> t1 = bhed::tour::suche(idx, "compile");
    expect("compile -> k-uebersetzen zuerst", !t1.empty() && idx[t1[0].abschnitt].seite == "k-uebersetzen.html");
    const std::vector<bhed::tour::Treffer> t2 = bhed::tour::suche(idx, "AFFECT events");
    expect("AFFECT events (Gross/klein egal) -> k-neues-skript", !t2.empty() && idx[t2[0].abschnitt].seite == "k-neues-skript.html");
    const std::vector<bhed::tour::Treffer> t3 = bhed::tour::suche(idx, "\xE5\x9C\xB0\xE5\x9B\xBE");   // "Karte" auf Chinesisch
    expect("CJK: \xE5\x9C\xB0\xE5\x9B\xBE findet die Karte", !t3.empty() && idx[t3[0].abschnitt].seite == "k-karte.html");
    expect("leere Anfrage -> keine Treffer", bhed::tour::suche(idx, "   ").empty());
    expect("nichts passt -> keine Treffer", bhed::tour::suche(idx, "zyxw").empty());
    expect("kein Index -> false", !bhed::tour::ladeSuchindex("var x = 1;", idx, &f));
    const std::string echt = datei(ordner + "/en/suche.js");
    if (!echt.empty()) {
        std::vector<bhed::tour::Abschnitt> e;
        expect("echtes en/suche.js liest sich", bhed::tour::ladeSuchindex(echt, e, &f) && e.size() > 100);
        const std::vector<bhed::tour::Treffer> t = bhed::tour::suche(e, "compile ibi");
        std::printf("    \"compile ibi\": %s\n", t.empty() ? "-" : (e[t[0].abschnitt].seite + "#" + e[t[0].abschnitt].anker).c_str());
        expect("echtes en/suche.js: \"compile ibi\" hat Treffer", !t.empty());
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string handbuch = argc > 1 ? argv[1] : "handbuch";
    i18nProben();
    jsonProben();
    zielProben();
    pruefungProben();
    aktionProben();
    ablaufProben();
    standProben();
    ladeProben();
    handbuchProben(handbuch);
    sucheProben(handbuch);
    std::printf(fehler == 0 ? "\nalle Tourproben bestanden\n" : "\nFEHLGESCHLAGEN (%d)\n", fehler);
    return fehler == 0 ? 0 : 1;
}
