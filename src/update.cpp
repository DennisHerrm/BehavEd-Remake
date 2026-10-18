// update.cpp - siehe update.h
#include "bhed/update.h"
#include "bhed/json.h"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace bhed::update {
namespace {

// --- Der JSON-Leser ---------------------------------------------------------
//
// Gebraucht werden nur ein paar Felder einer GitHub-Antwort. Der Leser stand
// bis rc600 hier; seit rc601 (Paket tour) liegt er in bhed/json.h, weil auch
// die Tutorials im Programm JSON lesen.
using Wert = ::bhed::json::Wert;

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

}  // namespace

bool parseRelease(const std::string& json, Release& out, std::string* fehler) {
    out = Release{};
    Wert w;
    if (!::bhed::json::lies(json, w) || w.art != Wert::Art::Objekt) {
        if (fehler != nullptr) { *fehler = "Antwort ist kein gueltiges JSON-Objekt"; }
        return false;
    }
    out.tag = w.textVon("tag_name");
    out.name = w.textVon("name");
    out.body = w.textVon("body");
    out.htmlUrl = w.textVon("html_url");
    if (const Wert* a = w.feld("assets"); a != nullptr && a->art == Wert::Art::Liste) {
        for (const Wert& e : a->liste) {
            if (e.art != Wert::Art::Objekt) { continue; }
            Asset as;
            as.name = e.textVon("name");
            as.apiUrl = e.textVon("url");
            as.downloadUrl = e.textVon("browser_download_url");
            if (const Wert* g = e.feld("size"); g != nullptr && g->art == Wert::Art::Zahl) {
                as.size = static_cast<long long>(g->zahl);
            }
            out.assets.push_back(std::move(as));
        }
    }
    if (out.tag.empty()) {
        if (fehler != nullptr) {
            // GitHub schickt bei Fehlern {"message": "..."} - das ist die beste Auskunft.
            const std::string m = w.textVon("message");
            *fehler = m.empty() ? std::string("Antwort ohne tag_name") : m;
        }
        return false;
    }
    return true;
}

int rcNummer(const std::string& s) {
    const std::string k = klein(s);
    const std::size_t p = k.rfind("rc");
    if (p == std::string::npos) { return -1; }
    std::size_t i = p + 2;
    if (i >= k.size() || std::isdigit(static_cast<unsigned char>(k[i])) == 0) { return -1; }
    int n = 0;
    while (i < k.size() && std::isdigit(static_cast<unsigned char>(k[i])) != 0 && n < 100000000) {
        n = n * 10 + (k[i] - '0');
        ++i;
    }
    return n;
}

bool istNeuer(const std::string& tag, const std::string& lokal) {
    const int a = rcNummer(tag);
    const int b = rcNummer(lokal);
    return a >= 0 && b >= 0 && a > b;
}

const Asset* zipAsset(const Release& r) {
    for (const Asset& a : r.assets) {
        const std::string n = klein(a.name);
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".zip") == 0) { return &a; }
    }
    return nullptr;
}

std::string gemeinsamerOrdner(const std::vector<std::string>& eintraege) {
    std::string ober;
    for (const std::string& e : eintraege) {
        const std::size_t s = e.find('/');
        if (s == std::string::npos) { return {}; }   // eine Datei ganz oben: kein gemeinsamer Ordner
        const std::string o = e.substr(0, s + 1);
        if (ober.empty()) {
            ober = o;
        } else if (o != ober) {
            return {};
        }
    }
    return ober;
}

std::string zielImOrdner(const std::string& eintrag, const std::string& oberOrdner) {
    std::string p = eintrag;
    for (char& c : p) {
        if (c == '\\') { c = '/'; }
    }
    if (!oberOrdner.empty() && p.compare(0, oberOrdner.size(), oberOrdner) == 0) {
        p = p.substr(oberOrdner.size());
    }
    if (p.empty() || p.back() == '/') { return {}; }                    // ein Ordner
    if (p.front() == '/' || (p.size() > 1 && p[1] == ':')) { return {}; }   // absolut
    // Kein ".." als Teil: ein Archiv darf nichts ausserhalb anfassen.
    std::size_t a = 0;
    while (a <= p.size()) {
        const std::size_t b = p.find('/', a);
        const std::string teil = p.substr(a, (b == std::string::npos ? p.size() : b) - a);
        if (teil == ".." || teil.empty()) { return {}; }
        if (b == std::string::npos) { break; }
        a = b + 1;
    }
    return p;
}

// --- rc601 release: was das Update mit einem Eintrag tut ------------------
//
// Bis rc600 stand in gui/update_win32.cpp: die laufende .exe ist der Eintrag,
// dessen DATEINAME ihrem gleicht - in welchem Ordner des Archivs auch immer.
// Das ging schief, sobald die .exe anders heisst als die im Zip: die
// eigenstaendige BehavEd-Remake-rcNNN-windows-x64.exe blieb alt, daneben lag
// eine neue behaved.exe, der Neustart (neustartWennVorgemerkt) startete die
// alte Datei, und das Update bot sich beim naechsten Start wieder an. Und
// eine behaved.exe in einem Unterordner des Zips haette die laufende ersetzt.
//
// Jetzt: das Programm des Zips ist "behaved.exe" ganz oben (oder ein Eintrag
// mit dem Namen der laufenden .exe, wie bisher) - es kommt an die Stelle der
// LAUFENDEN .exe, wie immer sie heisst. Laeuft sie eigenstaendig (kein
// data\base daneben), kommt nur sie: ihre Daten stecken im Programm
// (bhed/eingebaut.h), und data/ und handbuch/ - fast 600 Dateien - sollen
// nicht ungefragt neben einer .exe in "Downloads" landen.
Einspielen einspielenAls(const std::string& rel, const std::string& laufenderName, bool nurProgramm) {
    if (rel.empty()) { return Einspielen::Ueberspringen; }
    const bool obenAuf = rel.find('/') == std::string::npos;
    const std::string k = klein(rel);
    if (obenAuf && (k == "behaved.exe" || k == klein(laufenderName))) { return Einspielen::Programm; }
    return nurProgramm ? Einspielen::Ueberspringen : Einspielen::Datei;
}

}  // namespace bhed::update
