// tour.cpp - siehe tour.h (Paket tour, rc601)
#include "bhed/tour.h"

#include "bhed/json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace bhed::tour {
namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])) != 0) { ++a; }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])) != 0) { --b; }
    return s.substr(a, b - a);
}

std::vector<std::string> teile(const std::string& s, char trenner) {
    std::vector<std::string> out;
    std::size_t a = 0;
    while (true) {
        const std::size_t e = s.find(trenner, a);
        out.push_back(trim(s.substr(a, e == std::string::npos ? std::string::npos : e - a)));
        if (e == std::string::npos) { break; }
        a = e + 1;
    }
    return out;
}

// Kennungen der i18n-Tabelle und Befehlsnamen: Buchstaben, Ziffern, _.
bool istName(const std::string& s) {
    if (s.empty()) { return false; }
    for (const char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') { return false; }
    }
    return true;
}

bool istZahl(const std::string& s) {
    if (s.empty() || s.size() > 6) { return false; }
    for (const char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) { return false; }
    }
    return true;
}

void setzeFehler(std::string* f, const std::string& text) {
    if (f != nullptr) { *f = text; }
}

// "##v@###editor#1" -> Text "##v", Fenster "###editor", n 1. Die Nummer ist
// nur ein "#" mit Ziffern AM ENDE - "##v" selbst bleibt unangetastet.
void zerlegeArg(const std::string& arg, std::string& text, std::string& fenster, int& nte, bool& hatNummer) {
    std::string rest = arg;
    nte = 0;
    hatNummer = false;
    const std::size_t raute = rest.rfind('#');
    if (raute != std::string::npos && raute + 1 < rest.size() && istZahl(rest.substr(raute + 1))) {
        nte = std::stoi(rest.substr(raute + 1));
        hatNummer = true;
        rest = rest.substr(0, raute);
    }
    const std::size_t at = rest.find('@');
    if (at != std::string::npos && at > 0) {
        text = rest.substr(0, at);
        fenster = rest.substr(at + 1);
    } else {
        text = rest;
        fenster.clear();
    }
}

// --- Pruefungen ------------------------------------------------------------------

struct Teil {
    std::string name;
    std::string arg;
    bool plus = false;
};

Teil zerlegePruefung(const std::string& s0) {
    Teil t;
    std::string s = trim(s0);
    if (!s.empty() && s.back() == '+') {
        t.plus = true;
        s.pop_back();
        s = trim(s);
    }
    const std::size_t d = s.find(':');
    if (d == std::string::npos) {
        t.name = s;
    } else {
        t.name = trim(s.substr(0, d));
        t.arg = trim(s.substr(d + 1));
    }
    return t;
}

const std::vector<std::string> kZaehler = {"reiter", "knoten", "aenderung", "rem", "uebersetzt"};
const std::vector<std::string> kZustandOhneArg = {"leer", "editorzu", "schwebt", "geklickt", "manuell"};
const std::vector<std::string> kFenster = {"messages", "find"};

bool enthaelt(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// "affect#0:0" -> befehl, n, feld.
bool zerlegeWert(const std::string& arg, std::string& befehl, int& nte, int& feld) {
    const std::size_t raute = arg.find('#');
    const std::size_t dp = arg.find(':', raute == std::string::npos ? 0 : raute);
    if (raute == std::string::npos || dp == std::string::npos) { return false; }
    befehl = arg.substr(0, raute);
    const std::string n = arg.substr(raute + 1, dp - raute - 1);
    const std::string f = arg.substr(dp + 1);
    if (!istName(befehl) || !istZahl(n) || !istZahl(f)) { return false; }
    nte = std::stoi(n);
    feld = std::stoi(f);
    return true;
}

bool teilBekannt(const Teil& t, std::string* fehler) {
    const auto falsch = [&](const std::string& warum) {
        setzeFehler(fehler, "Pruefung \"" + t.name + (t.arg.empty() ? "" : ":" + t.arg) + (t.plus ? "+" : "") +
                                "\": " + warum);
        return false;
    };
    if (t.name.empty()) { return falsch("leer"); }
    if (enthaelt(kZaehler, t.name)) {
        if (!t.plus) { return falsch("ein Zaehler braucht \"+\""); }
        if (!t.arg.empty()) { return falsch("ohne Argument"); }
        return true;
    }
    if (t.name == "gespeichert") {
        return t.arg.empty() ? true : falsch("ohne Argument");
    }
    if (t.name == "befehl") {
        return istName(t.arg) ? true : falsch("braucht einen Befehlsnamen");
    }
    if (t.plus) { return falsch("\"+\" gibt es nur an Zaehlern und befehl:"); }
    if (enthaelt(kZustandOhneArg, t.name)) {
        return t.arg.empty() ? true : falsch("ohne Argument");
    }
    if (t.name == "kind") {
        const std::size_t g = t.arg.find('>');
        if (g == std::string::npos || !istName(t.arg.substr(0, g)) || !istName(t.arg.substr(g + 1))) {
            return falsch("Form kind:<block>><befehl>");
        }
        return true;
    }
    if (t.name == "wert") {
        std::string b;
        int n = 0;
        int f = 0;
        return zerlegeWert(t.arg, b, n, f) ? true : falsch("Form wert:<befehl>#<n>:<feld>");
    }
    if (t.name == "auswahl" || t.name == "editor") {
        return (t.arg.empty() || istName(t.arg)) ? true : falsch("Argument ist ein Befehlsname");
    }
    if (t.name == "fenster") {
        return enthaelt(kFenster, t.arg) ? true : falsch("bekannte Fenster: messages, find");
    }
    if (t.name == "modus" || t.name == "ansicht") {
        return (t.arg == "0" || t.arg == "1" || t.arg == "2") ? true : falsch("0, 1 oder 2");
    }
    return falsch("unbekannt");
}

int anzahlVon(const Kontext& k, const std::string& befehl) {
    const auto it = k.befehle.find(befehl);
    return it == k.befehle.end() ? 0 : it->second;
}

bool teilErfuellt(const Teil& t, const Kontext& jetzt, const Kontext& start) {
    if (t.name == "reiter") { return jetzt.reiter > start.reiter; }
    if (t.name == "knoten") { return jetzt.knoten > start.knoten; }
    if (t.name == "aenderung") { return jetzt.aenderungen > start.aenderungen; }
    if (t.name == "rem") { return jetzt.rem > start.rem; }
    if (t.name == "uebersetzt") { return jetzt.uebersetzt > start.uebersetzt; }
    if (t.name == "gespeichert") { return t.plus ? jetzt.gespeichert > start.gespeichert : jetzt.aktivGesichert; }
    if (t.name == "befehl") {
        return t.plus ? anzahlVon(jetzt, t.arg) > anzahlVon(start, t.arg) : anzahlVon(jetzt, t.arg) > 0;
    }
    if (t.name == "leer") { return jetzt.leer; }
    if (t.name == "kind") {
        const std::size_t g = t.arg.find('>');
        return g != std::string::npos && jetzt.hatKind && jetzt.hatKind(t.arg.substr(0, g), t.arg.substr(g + 1));
    }
    if (t.name == "wert") {
        std::string b;
        int n = 0;
        int f = 0;
        if (!jetzt.argument || !zerlegeWert(t.arg, b, n, f)) { return false; }
        // "DEFAULT" ist Ravens Platzhalter fuer "hier gehoert etwas hin"
        // (src/edit.cpp, defaultValueFor) - also noch nicht gesetzt.
        const std::string v = trim(jetzt.argument(b, n, f));
        return !v.empty() && v != "DEFAULT";
    }
    if (t.name == "auswahl") { return t.arg.empty() ? jetzt.auswahlZahl > 0 : jetzt.auswahl == t.arg; }
    if (t.name == "editor") { return jetzt.editorOffen && (t.arg.empty() || jetzt.editorBefehl == t.arg); }
    if (t.name == "editorzu") { return !jetzt.editorOffen; }
    if (t.name == "fenster") { return enthaelt(jetzt.fenster, t.arg); }
    if (t.name == "modus") { return std::to_string(jetzt.modus) == t.arg; }
    if (t.name == "ansicht") { return std::to_string(jetzt.ansicht) == t.arg; }
    if (t.name == "schwebt") { return jetzt.schwebt; }
    if (t.name == "geklickt") { return jetzt.geklickt; }
    return false;   // manuell und Unbekanntes
}

}  // namespace

// --- Daten ----------------------------------------------------------------------------

const std::vector<Schritt>* Tour::schritte(const std::string& ansicht) const {
    for (const auto& [name, s] : ansichten) {
        if (name == ansicht) { return &s; }
    }
    for (const auto& [name, s] : ansichten) {
        if (name == "classic") { return &s; }
    }
    return nullptr;
}

const Tour* Touren::finde(const std::string& id) const {
    for (const Tour& t : liste) {
        if (t.id == id) { return &t; }
    }
    return nullptr;
}

const char* ansichtName(int flussAnsicht) {
    switch (flussAnsicht) {
        case 1: return "graph";
        case 2: return "tracks";
        default: return "classic";
    }
}

bool ladeTouren(const std::string& text, Touren& out, std::string* fehler) {
    out = Touren{};
    json::Wert w;
    std::string f;
    if (!json::lies(text, w, &f) || w.art != json::Wert::Art::Objekt) {
        setzeFehler(fehler, "touren.json: " + (f.empty() ? std::string("kein Objekt") : f));
        return false;
    }
    out.fassung = static_cast<int>(w.zahlVon("fassung", 0.0));
    out.sprache = w.textVon("sprache");
    const json::Wert* liste = w.feld("touren");
    if (liste == nullptr || liste->art != json::Wert::Art::Liste) {
        setzeFehler(fehler, "touren.json: keine Liste \"touren\"");
        return false;
    }
    std::set<std::string> ids;
    for (const json::Wert& tw : liste->liste) {
        Tour t;
        t.id = tw.textVon("id");
        if (t.id.empty() || !ids.insert(t.id).second) {
            setzeFehler(fehler, "touren.json: Tour ohne oder mit doppelter Kennung \"" + t.id + "\"");
            return false;
        }
        t.gruppe = tw.textVon("gruppe");
        t.titel = tw.textVon("titel");
        t.kurz = tw.textVon("kurz");
        t.seite = tw.textVon("seite");
        const json::Wert* an = tw.feld("ansichten");
        if (an == nullptr || an->art != json::Wert::Art::Objekt || an->objekt.empty()) {
            setzeFehler(fehler, t.id + ": keine Ansichten");
            return false;
        }
        for (const auto& [ansicht, sl] : an->objekt) {
            if (sl.art != json::Wert::Art::Liste || sl.liste.empty()) {
                setzeFehler(fehler, t.id + "/" + ansicht + ": keine Schritte");
                return false;
            }
            std::vector<Schritt> schritte;
            int nr = 0;
            for (const json::Wert& sw : sl.liste) {
                ++nr;
                const std::string wo = t.id + "/" + ansicht + " Schritt " + std::to_string(nr) + ": ";
                Schritt s;
                s.text = sw.textVon("text");
                s.bild = sw.textVon("bild");
                s.unter = sw.textVon("unter");
                s.ziel = sw.textVon("ziel");
                s.fertig = trim(sw.textVon("fertig"));
                s.voraus = trim(sw.textVon("voraus"));
                s.einrichten = sw.textVon("einrichten");
                s.sicher = sw.wahrVon("sicher", false);
                s.ueberspringen = sw.textVon("ueberspringen");   // rc601 tour (Review); fehlt in aelteren Dateien
                if (s.fertig.empty()) { s.fertig = "manuell"; }
                std::vector<Ziel> zs;
                std::vector<Aktion> as;
                if (!parseZiele(s.ziel, zs, &f) || !pruefungBekannt(s.fertig, &f) || !parseAktionen(s.einrichten, as, &f) ||
                    !parseAktionen(s.ueberspringen, as, &f)) {
                    setzeFehler(fehler, wo + f);
                    return false;
                }
                if (!s.voraus.empty()) {
                    const Voraus v = zerlegeVoraus(s.voraus);
                    if (!pruefungBekannt(v.pruefung, &f) || !parseZiele(v.oeffner, zs, &f)) {
                        setzeFehler(fehler, wo + "Vorbedingung: " + f);
                        return false;
                    }
                }
                schritte.push_back(std::move(s));
            }
            t.ansichten.emplace_back(ansicht, std::move(schritte));
        }
        out.liste.push_back(std::move(t));
    }
    return true;
}

// --- Zielangaben --------------------------------------------------------------------------

bool parseZiel(const std::string& s0, Ziel& out, std::string* fehler) {
    out = Ziel{};
    const std::string s = trim(s0);
    const std::size_t d = s.find(':');
    const std::string art = d == std::string::npos ? s : s.substr(0, d);
    const std::string arg = d == std::string::npos ? std::string() : s.substr(d + 1);
    const auto falsch = [&](const std::string& warum) {
        setzeFehler(fehler, "Ziel \"" + s + "\": " + warum);
        return false;
    };
    if (art == "baum" || art == "meldungen") {
        if (!arg.empty()) { return falsch("ohne Argument"); }
        out.art = art == "baum" ? Ziel::Art::Baum : Ziel::Art::Meldungen;
        return true;
    }
    if (d == std::string::npos || trim(arg).empty()) { return falsch("Argument fehlt"); }
    std::string text;
    bool hatNummer = false;
    zerlegeArg(arg, text, out.fenster, out.nte, hatNummer);
    if (art == "knopf") {
        if (!istName(text) || hatNummer) { return falsch("knopf:<Kennung>[@<fenster>]"); }
        out.art = Ziel::Art::Knopf;
        out.teile = {text};
    } else if (art == "knopfN") {
        if (text.empty()) { return falsch("knopfN:<text>[@<fenster>][#n]"); }
        out.art = Ziel::Art::KnopfN;
        out.teile = {text};
    } else if (art == "menue") {
        out.art = Ziel::Art::Menue;
        out.teile = teile(text, '>');
        for (const std::string& k : out.teile) {
            if (!istName(k)) { return falsch("menue:<Kennung>><Kennung>..."); }
        }
    } else if (art == "gruppe") {
        out.art = Ziel::Art::Gruppe;
        out.teile = teile(text, ',');
        for (const std::string& k : out.teile) {
            if (!istName(k)) { return falsch("gruppe:<Kennung>,<Kennung>..."); }
        }
    } else if (art == "eintrag" || art == "zeile") {
        if (!istName(text) || !out.fenster.empty()) { return falsch(art + ":<befehl>" + (art == "zeile" ? "[#n]" : "")); }
        if (art == "eintrag" && hatNummer) { return falsch("eintrag:<befehl> ohne Nummer"); }
        out.art = art == "eintrag" ? Ziel::Art::Eintrag : Ziel::Art::Zeile;
        out.teile = {text};
    } else if (art == "reiter") {
        if (arg != "+") { return falsch("nur reiter:+"); }
        out.art = Ziel::Art::Reiter;
        out.teile = {"+"};
        out.fenster.clear();
        out.nte = 0;
    } else if (art == "fenster") {
        out.art = Ziel::Art::Fenster;
        out.teile = {trim(arg)};
        out.fenster.clear();
        out.nte = 0;
    } else {
        return falsch("unbekannte Art \"" + art + "\"");
    }
    return true;
}

bool parseZiele(const std::string& s, std::vector<Ziel>& out, std::string* fehler) {
    out.clear();
    for (const std::string& t : teile(s, ';')) {
        if (t.empty()) { continue; }
        Ziel z;
        if (!parseZiel(t, z, fehler)) { return false; }
        out.push_back(std::move(z));
    }
    return true;
}

std::vector<std::string> kennungenIn(const Ziel& z) {
    if (z.art == Ziel::Art::Knopf || z.art == Ziel::Art::Menue || z.art == Ziel::Art::Gruppe) { return z.teile; }
    return {};
}

// --- Pruefungen ------------------------------------------------------------------------------

const std::vector<std::string>& bekanntePruefungen() {
    static const std::vector<std::string> k = {"reiter",  "knoten",  "aenderung", "rem",     "uebersetzt", "gespeichert",
                                               "befehl",  "leer",    "kind",      "wert",    "auswahl",    "editor",
                                               "editorzu", "fenster", "modus",    "ansicht", "schwebt",    "geklickt",
                                               "manuell"};
    return k;
}

bool pruefungBekannt(const std::string& pruefung, std::string* fehler) {
    if (trim(pruefung).empty()) {
        setzeFehler(fehler, "leere Pruefung");
        return false;
    }
    for (const std::string& alt : teile(pruefung, '|')) {
        if (!teilBekannt(zerlegePruefung(alt), fehler)) { return false; }
    }
    return true;
}

bool erfuellt(const std::string& pruefung, const Kontext& jetzt, const Kontext& start) {
    for (const std::string& alt : teile(pruefung, '|')) {
        const Teil t = zerlegePruefung(alt);
        if (teilBekannt(t, nullptr) && teilErfuellt(t, jetzt, start)) { return true; }
    }
    return false;
}

bool istZustand(const std::string& pruefung) {
    for (const std::string& alt : teile(pruefung, '|')) {
        const Teil t = zerlegePruefung(alt);
        if (t.plus || t.name == "manuell" || enthaelt(kZaehler, t.name)) { return false; }
    }
    return true;
}

Voraus zerlegeVoraus(const std::string& s) {
    Voraus v;
    const std::size_t lt = s.find('<');
    v.pruefung = trim(lt == std::string::npos ? s : s.substr(0, lt));
    if (lt != std::string::npos) { v.oeffner = trim(s.substr(lt + 1)); }
    return v;
}

// --- Aktionen ----------------------------------------------------------------------------------

const std::vector<std::string>& bekannteAktionen() {
    static const std::vector<std::string> k = {"neuerReiter", "befehl", "waehle",   "wert",   "modus",
                                               "ansicht",     "editor", "editorzu", "fenster"};
    return k;
}

void befehlUndNummer(const std::string& s, std::string& befehl, int& nte) {
    const std::size_t raute = s.rfind('#');
    if (raute != std::string::npos && istZahl(s.substr(raute + 1))) {
        befehl = s.substr(0, raute);
        nte = std::stoi(s.substr(raute + 1));
    } else {
        befehl = s;
        nte = 0;
    }
}

// --- rc601 tour (Review): Aktionen fuer "Ueberspringen" ---------------------------------------
//
// Gemeldet: "Skip always runs `einrichten`; for s_ein_doppel, s_ein_einfg,
// s_ein_ziehen, s_ein_kontext it inserts stray `wait` lines into the active
// script; Skip/Do-it on k-neues-skript step 4 rewrites the first affect."
// Ueberspringen richtet nur ein, was der naechste Schritt braucht (Modus,
// Auswahl, Editor, Fenster, ein neuer Reiter) - es aendert das Skript nie.
std::string ueberspringAktionen(const Schritt& s) {
    if (!s.ueberspringen.empty()) { return s.ueberspringen; }
    std::vector<Aktion> as;
    std::string f;
    if (!parseAktionen(s.einrichten, as, &f)) { return {}; }
    std::string out;
    for (const Aktion& a : as) {
        if (a.name == "befehl" || a.name == "wert") { continue; }
        out += (out.empty() ? "" : ";") + a.name + (a.arg.empty() ? "" : ":" + a.arg);
    }
    return out;
}

bool parseAktionen(const std::string& s, std::vector<Aktion>& out, std::string* fehler) {
    out.clear();
    for (const std::string& t : teile(s, ';')) {
        if (t.empty()) { continue; }
        Aktion a;
        const std::size_t d = t.find(':');
        a.name = trim(d == std::string::npos ? t : t.substr(0, d));
        a.arg = d == std::string::npos ? std::string() : trim(t.substr(d + 1));
        const auto falsch = [&](const std::string& warum) {
            setzeFehler(fehler, "Aktion \"" + t + "\": " + warum);
            return false;
        };
        std::string b;
        int n = 0;
        if (a.name == "neuerReiter" || a.name == "editorzu") {
            if (!a.arg.empty()) { return falsch("ohne Argument"); }
        } else if (a.name == "befehl") {
            if (!istName(a.arg)) { return falsch("braucht einen Befehlsnamen"); }
        } else if (a.name == "waehle" || a.name == "editor") {
            befehlUndNummer(a.arg, b, n);
            if (!(a.arg.empty() && a.name == "editor") && b != "*" && !istName(b)) { return falsch("<befehl>[#n] oder *"); }
        } else if (a.name == "wert") {
            const std::size_t gl = a.arg.find('=');
            int f = 0;
            if (gl == std::string::npos || !zerlegeWert(a.arg.substr(0, gl), b, n, f)) {
                return falsch("wert:<befehl>#<n>:<feld>=<text>");
            }
        } else if (a.name == "modus" || a.name == "ansicht") {
            if (a.arg != "0" && a.arg != "1" && a.arg != "2") { return falsch("0, 1 oder 2"); }
        } else if (a.name == "fenster") {
            if (!enthaelt(kFenster, a.arg)) { return falsch("bekannte Fenster: messages, find"); }
        } else {
            return falsch("unbekannt");
        }
        out.push_back(std::move(a));
    }
    return true;
}

// --- Text ------------------------------------------------------------------------------------------

std::vector<Stueck> zerlege(const std::string& text) {
    std::vector<Stueck> out;
    bool fett = false;
    std::string puffer;
    const auto textRaus = [&]() {
        if (puffer.empty()) { return; }
        if (!out.empty() && out.back().art == Stueck::Art::Text && out.back().fett == fett) {
            out.back().inhalt += puffer;
        } else {
            out.push_back(Stueck{Stueck::Art::Text, puffer, fett});
        }
        puffer.clear();
    };
    std::size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 2, "**") == 0) {
            textRaus();
            fett = !fett;
            i += 2;
            continue;
        }
        if (text[i] == '`') {
            const std::size_t e = text.find('`', i + 1);
            if (e != std::string::npos) {
                textRaus();
                out.push_back(Stueck{Stueck::Art::Code, text.substr(i + 1, e - i - 1), fett});
                i = e + 1;
                continue;
            }
        }
        if (text.compare(i, 5, "<kbd>") == 0) {
            const std::size_t e = text.find("</kbd>", i + 5);
            if (e != std::string::npos) {
                textRaus();
                out.push_back(Stueck{Stueck::Art::Taste, text.substr(i + 5, e - i - 5), fett});
                i = e + 6;
                continue;
            }
        }
        if (text.compare(i, 2, "{{") == 0) {
            const std::size_t e = text.find("}}", i + 2);
            if (e != std::string::npos) {
                const std::string name = text.substr(i + 2, e - i - 2);
                textRaus();
                if (name.rfind("icon:", 0) != 0) { out.push_back(Stueck{Stueck::Art::Kennung, name, fett}); }
                i = e + 2;
                continue;
            }
        }
        if (text.compare(i, 2, "[[") == 0) {
            const std::size_t e = text.find("]]", i + 2);
            if (e != std::string::npos) {
                const std::string innen = text.substr(i + 2, e - i - 2);
                const std::size_t strich = innen.find('|');
                if (strich != std::string::npos) { puffer += innen.substr(strich + 1); }
                i = e + 2;
                continue;
            }
        }
        puffer += text[i];
        ++i;
    }
    textRaus();
    return out;
}

std::vector<std::string> kennungenImText(const std::string& text) {
    std::vector<std::string> out;
    for (const Stueck& s : zerlege(text)) {
        if (s.art == Stueck::Art::Kennung) { out.push_back(s.inhalt); }
    }
    return out;
}

// --- Ablauf -------------------------------------------------------------------------------------------

bool Ablauf::starte(const Tour& t, const std::string& ansicht, int ab) {
    const std::vector<Schritt>* s = t.schritte(ansicht);
    if (s == nullptr || s->empty() || ab < 0 || ab >= static_cast<int>(s->size())) { return false; }
    tour_ = &t;
    schritte_ = s;
    ansicht_ = ansicht;
    staende_.assign(s->size(), Stand::Offen);
    fertig_ = false;
    betrete(ab, false);
    return true;
}

void Ablauf::schliesse() {
    tour_ = nullptr;
    schritte_ = nullptr;
    staende_.clear();
    fertig_ = false;
    erfuelltSeit_ = -1.0;
}

int Ablauf::anzahl() const { return schritte_ != nullptr ? static_cast<int>(schritte_->size()) : 0; }

const Schritt* Ablauf::aktuell() const {
    if (schritte_ == nullptr || schritt_ < 0 || schritt_ >= anzahl()) { return nullptr; }
    return &(*schritte_)[static_cast<std::size_t>(schritt_)];
}

Ablauf::Stand Ablauf::stand(int i) const {
    return (i >= 0 && i < static_cast<int>(staende_.size())) ? staende_[static_cast<std::size_t>(i)] : Stand::Offen;
}

void Ablauf::betrete(int i, bool rueckwaerts) {
    schritt_ = i;
    startDa_ = false;
    warFalsch_ = false;
    zurueckGeholt_ = rueckwaerts;
    erfuelltSeit_ = -1.0;
}

bool Ablauf::bild(const Kontext& k, double jetzt, double verzoegerung) {
    const Schritt* s = aktuell();
    if (s == nullptr || fertig_) { return false; }
    if (erfuelltSeit_ >= 0.0) {
        if (jetzt - erfuelltSeit_ >= verzoegerung) { weiter(); }
        return false;
    }
    if (!startDa_) {
        // Der Stand beim Beginn - erst im ersten Bild NACH dem Wechsel, damit
        // die Aktionen eines Ueberspringens schon darin stecken.
        start_ = k;
        startDa_ = true;
        return false;
    }
    const bool ok = tour::erfuellt(s->fertig, k, start_);
    if (!ok) {
        warFalsch_ = true;
        return false;
    }
    // Nach "Zurueck" zaehlt nur eine NEUE Erfuellung: ein Zustand, der noch
    // gilt, schaltet nicht sofort wieder weiter (sonst kaeme man nie zurueck).
    if (zurueckGeholt_ && !warFalsch_) { return false; }
    staende_[static_cast<std::size_t>(schritt_)] = Stand::Erledigt;
    erfuelltSeit_ = jetzt;
    return true;
}

void Ablauf::weiter() {
    if (aktuell() == nullptr) { return; }
    auto& st = staende_[static_cast<std::size_t>(schritt_)];
    if (st == Stand::Offen) { st = Stand::Erledigt; }
    if (schritt_ + 1 < anzahl()) {
        betrete(schritt_ + 1, false);
    } else {
        fertig_ = true;
        erfuelltSeit_ = -1.0;
    }
}

void Ablauf::zurueck() {
    if (aktuell() == nullptr || schritt_ <= 0) { return; }
    staende_[static_cast<std::size_t>(schritt_)] = Stand::Offen;
    staende_[static_cast<std::size_t>(schritt_ - 1)] = Stand::Offen;
    fertig_ = false;
    betrete(schritt_ - 1, true);
}

std::string Ablauf::ueberspringen() {
    const Schritt* s = aktuell();
    if (s == nullptr || fertig_) { return {}; }
    const std::string aktionen = ueberspringAktionen(*s);   // rc601 tour (Review): nicht mehr `einrichten`
    staende_[static_cast<std::size_t>(schritt_)] = Stand::Uebersprungen;
    if (schritt_ + 1 < anzahl()) {
        betrete(schritt_ + 1, false);
    } else {
        fertig_ = true;
        erfuelltSeit_ = -1.0;
    }
    return aktionen;
}

std::string Ablauf::machEs() const {
    const Schritt* s = aktuell();
    return (s != nullptr && s->sicher && !fertig_) ? s->einrichten : std::string();
}

// --- Fortschritt --------------------------------------------------------------------------------------

std::string standText(int naechster, int anzahl, bool fertig) {
    if (fertig) { return "fertig"; }
    return std::to_string(naechster) + "/" + std::to_string(anzahl);
}

bool leseStand(const std::string& s0, int& naechster, int& anzahl, bool& fertig) {
    const std::string s = trim(s0);
    if (s == "fertig") {
        fertig = true;
        naechster = 0;
        anzahl = 0;
        return true;
    }
    const std::size_t strich = s.find('/');
    if (strich == std::string::npos || !istZahl(s.substr(0, strich)) || !istZahl(s.substr(strich + 1))) { return false; }
    const int n = std::stoi(s.substr(0, strich));
    const int z = std::stoi(s.substr(strich + 1));
    if (z < 1 || n > z) { return false; }
    naechster = n;
    anzahl = z;
    fertig = false;
    return true;
}

// --- Suche im Handbuch ---------------------------------------------------------------------------------

namespace {

bool istCjk(unsigned cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xAC00 && cp <= 0xD7AF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF);
}

// Woerter (ASCII klein) und einzelne CJK-Zeichen.
std::vector<std::string> woerter(const std::string& s) {
    std::vector<std::string> out;
    std::string w;
    const auto wortRaus = [&]() {
        if (!w.empty()) {
            out.push_back(w);
            w.clear();
        }
    };
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            if (std::isalnum(c) != 0 || c == '_') {
                w += static_cast<char>(std::tolower(c));
            } else {
                wortRaus();
            }
            ++i;
            continue;
        }
        std::size_t n = 1;
        unsigned cp = 0;
        if ((c & 0xE0) == 0xC0) {
            n = 2;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            n = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            n = 4;
            cp = c & 0x07;
        }
        if (i + n > s.size()) { break; }
        for (std::size_t k = 1; k < n; ++k) { cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F); }
        const std::string zeichen = s.substr(i, n);
        if (istCjk(cp)) {
            wortRaus();
            if (cp < 0x3000 || cp > 0x303F) { out.push_back(zeichen); }   // ohne CJK-Satzzeichen
        } else if (cp == 0x2192 || cp == 0x00B7 || cp == 0x2014 || cp == 0x2013 || cp == 0x00A0) {
            wortRaus();   // Pfeil, Mittelpunkt, Gedankenstriche, festes Leerzeichen
        } else {
            w += zeichen;   // Umlaute und Akzente gehoeren zum Wort
        }
        i += n;
    }
    wortRaus();
    return out;
}

bool istCjkWort(const std::string& w) { return !w.empty() && static_cast<unsigned char>(w[0]) >= 0xE0; }

}  // namespace

bool ladeSuchindex(const std::string& js, std::vector<Abschnitt>& out, std::string* fehler) {
    out.clear();
    const std::string kopf = "window.HANDBUCH_INDEX=";
    const std::size_t a = js.find(kopf);
    if (a == std::string::npos) {
        setzeFehler(fehler, "suche.js: kein window.HANDBUCH_INDEX");
        return false;
    }
    const std::size_t start = a + kopf.size();
    std::size_t ende = js.find(";\nwindow.HANDBUCH_TEXTE", start);
    if (ende == std::string::npos) { ende = js.find(";\n", start); }
    if (ende == std::string::npos) { ende = js.size(); }
    json::Wert w;
    std::string f;
    if (!json::lies(js.substr(start, ende - start), w, &f) || w.art != json::Wert::Art::Liste) {
        setzeFehler(fehler, "suche.js: " + (f.empty() ? std::string("keine Liste") : f));
        return false;
    }
    for (const json::Wert& e : w.liste) {
        if (e.art != json::Wert::Art::Objekt) { continue; }
        Abschnitt x;
        x.seite = e.textVon("p");
        x.anker = e.textVon("a");
        x.titel = e.textVon("t");
        x.ueber = e.textVon("s");
        x.text = e.textVon("x");
        out.push_back(std::move(x));
    }
    return true;
}

std::vector<Treffer> suche(const std::vector<Abschnitt>& index, const std::string& anfrage, std::size_t hoechstens) {
    std::vector<std::string> fragen;
    for (std::string& w : woerter(anfrage)) {
        // Ein einzelner lateinischer Buchstabe passt auf fast alles.
        if (w.size() < 2 && !istCjkWort(w)) { continue; }
        if (std::find(fragen.begin(), fragen.end(), w) == fragen.end()) { fragen.push_back(std::move(w)); }
    }
    if (fragen.empty() || index.empty()) { return {}; }
    // BM25 ueber drei Felder (BM25F-artig): Titel x3, Ueberschrift x2, Text x1.
    // Ein Suchwort passt auf jedes Wort, das mit ihm BEGINNT - "comp" findet
    // "compile", waehrend man noch tippt.
    constexpr double kTitel = 3.0;
    constexpr double kUeber = 2.0;
    constexpr double k1 = 1.2;
    constexpr double b = 0.75;
    const std::size_t n = index.size();
    std::vector<std::vector<double>> tf(n, std::vector<double>(fragen.size(), 0.0));
    std::vector<double> laenge(n, 0.0);
    std::vector<double> df(fragen.size(), 0.0);
    for (std::size_t d = 0; d < n; ++d) {
        const auto feld = [&](const std::string& text, double gewicht) {
            const std::vector<std::string> ws = woerter(text);
            laenge[d] += gewicht * static_cast<double>(ws.size());
            for (const std::string& w : ws) {
                for (std::size_t q = 0; q < fragen.size(); ++q) {
                    if (w.compare(0, fragen[q].size(), fragen[q]) == 0) { tf[d][q] += gewicht; }
                }
            }
        };
        feld(index[d].titel, kTitel);
        feld(index[d].ueber, kUeber);
        feld(index[d].text, 1.0);
        for (std::size_t q = 0; q < fragen.size(); ++q) {
            if (tf[d][q] > 0.0) { df[q] += 1.0; }
        }
    }
    double mittel = 0.0;
    for (const double l : laenge) { mittel += l; }
    mittel = std::max(1.0, mittel / static_cast<double>(n));
    std::vector<Treffer> out;
    for (std::size_t d = 0; d < n; ++d) {
        double wert = 0.0;
        for (std::size_t q = 0; q < fragen.size(); ++q) {
            if (tf[d][q] <= 0.0) { continue; }
            const double idf = std::log(1.0 + (static_cast<double>(n) - df[q] + 0.5) / (df[q] + 0.5));
            wert += idf * tf[d][q] * (k1 + 1.0) / (tf[d][q] + k1 * (1.0 - b + b * laenge[d] / mittel));
        }
        if (wert > 0.0) { out.push_back(Treffer{d, wert}); }
    }
    std::stable_sort(out.begin(), out.end(), [](const Treffer& x, const Treffer& y) { return x.wert > y.wert; });
    if (out.size() > hoechstens) { out.resize(hoechstens); }
    return out;
}

}  // namespace bhed::tour
