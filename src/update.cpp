// update.cpp - siehe update.h
#include "bhed/update.h"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace bhed::update {
namespace {

// --- Ein kleiner JSON-Leser ------------------------------------------------
//
// Gebraucht werden nur ein paar Felder einer GitHub-Antwort. Statt einer
// Bibliothek ein Leser, der die ganze Grammatik versteht (sonst verliert er
// bei verschachtelten Objekten den Faden), aber nur Zeichenketten, Zahlen und
// die Struktur festhaelt.
struct Wert {
    enum class Art { Null, Bool, Zahl, Text, Liste, Objekt } art = Art::Null;
    std::string text;
    double zahl = 0.0;
    std::vector<Wert> liste;
    std::vector<std::pair<std::string, Wert>> objekt;

    [[nodiscard]] const Wert* feld(const std::string& k) const {
        for (const auto& [name, w] : objekt) {
            if (name == k) { return &w; }
        }
        return nullptr;
    }
    [[nodiscard]] std::string textVon(const std::string& k) const {
        const Wert* w = feld(k);
        return (w != nullptr && w->art == Art::Text) ? w->text : std::string();
    }
};

class Leser {
public:
    explicit Leser(const std::string& s) : s_(s) {}

    bool lies(Wert& w) {
        leer();
        if (i_ >= s_.size()) { return false; }
        const char c = s_[i_];
        if (c == '{') { return objekt(w); }
        if (c == '[') { return liste(w); }
        if (c == '"') { w.art = Wert::Art::Text; return text(w.text); }
        if (c == 't' || c == 'f') { return wort(w); }
        if (c == 'n') { w.art = Wert::Art::Null; return passt("null"); }
        return zahl(w);
    }
    bool amEnde() {
        leer();
        return i_ >= s_.size();
    }

private:
    const std::string& s_;
    std::size_t i_ = 0;
    int tiefe_ = 0;

    void leer() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_])) != 0) { ++i_; }
    }
    bool passt(const char* w) {
        for (std::size_t k = 0; w[k] != '\0'; ++k, ++i_) {
            if (i_ >= s_.size() || s_[i_] != w[k]) { return false; }
        }
        return true;
    }
    bool wort(Wert& w) {
        w.art = Wert::Art::Bool;
        if (s_[i_] == 't') { w.zahl = 1.0; return passt("true"); }
        return passt("false");
    }
    bool zahl(Wert& w) {
        const std::size_t a = i_;
        while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) != 0 || s_[i_] == '-' ||
                                  s_[i_] == '+' || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E')) {
            ++i_;
        }
        if (i_ == a) { return false; }
        w.art = Wert::Art::Zahl;
        w.zahl = std::strtod(s_.substr(a, i_ - a).c_str(), nullptr);
        return true;
    }
    static void utf8(unsigned cp, std::string& out) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& cp) {
        if (i_ + 4 > s_.size()) { return false; }
        cp = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[i_++];
            cp <<= 4;
            if (c >= '0' && c <= '9') { cp |= static_cast<unsigned>(c - '0'); }
            else if (c >= 'a' && c <= 'f') { cp |= static_cast<unsigned>(c - 'a' + 10); }
            else if (c >= 'A' && c <= 'F') { cp |= static_cast<unsigned>(c - 'A' + 10); }
            else { return false; }
        }
        return true;
    }
    bool text(std::string& out) {
        ++i_;   // "
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') { return true; }
            if (c != '\\') { out += c; continue; }
            if (i_ >= s_.size()) { return false; }
            const char e = s_[i_++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) { return false; }
                    // Ersatzpaar (Zeichen ausserhalb der Grundebene, etwa ein Emoji).
                    if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                        i_ += 2;
                        unsigned lo = 0;
                        if (!hex4(lo)) { return false; }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(cp, out);
                    break;
                }
                default: out += e; break;   // \" \\ \/
            }
        }
        return false;
    }
    bool liste(Wert& w) {
        if (++tiefe_ > 64) { return false; }
        w.art = Wert::Art::Liste;
        ++i_;
        leer();
        if (i_ < s_.size() && s_[i_] == ']') { ++i_; --tiefe_; return true; }
        while (true) {
            Wert kind;
            if (!lies(kind)) { return false; }
            w.liste.push_back(std::move(kind));
            leer();
            if (i_ >= s_.size()) { return false; }
            if (s_[i_] == ',') { ++i_; continue; }
            if (s_[i_] == ']') { ++i_; --tiefe_; return true; }
            return false;
        }
    }
    bool objekt(Wert& w) {
        if (++tiefe_ > 64) { return false; }
        w.art = Wert::Art::Objekt;
        ++i_;
        leer();
        if (i_ < s_.size() && s_[i_] == '}') { ++i_; --tiefe_; return true; }
        while (true) {
            leer();
            if (i_ >= s_.size() || s_[i_] != '"') { return false; }
            std::string k;
            if (!text(k)) { return false; }
            leer();
            if (i_ >= s_.size() || s_[i_] != ':') { return false; }
            ++i_;
            Wert kind;
            if (!lies(kind)) { return false; }
            w.objekt.emplace_back(std::move(k), std::move(kind));
            leer();
            if (i_ >= s_.size()) { return false; }
            if (s_[i_] == ',') { ++i_; continue; }
            if (s_[i_] == '}') { ++i_; --tiefe_; return true; }
            return false;
        }
    }
};

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

}  // namespace

bool parseRelease(const std::string& json, Release& out, std::string* fehler) {
    out = Release{};
    Wert w;
    Leser l(json);
    if (!l.lies(w) || !l.amEnde() || w.art != Wert::Art::Objekt) {
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

}  // namespace bhed::update
