// json.cpp - siehe json.h (Paket tour, rc601; der Leser stammt aus update.cpp)
#include "bhed/json.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace bhed::json {

const Wert* Wert::feld(const std::string& k) const {
    for (const auto& [name, w] : objekt) {
        if (name == k) { return &w; }
    }
    return nullptr;
}

std::string Wert::textVon(const std::string& k) const {
    const Wert* w = feld(k);
    return (w != nullptr && w->art == Art::Text) ? w->text : std::string();
}

double Wert::zahlVon(const std::string& k, double vorgabe) const {
    const Wert* w = feld(k);
    return (w != nullptr && w->art == Art::Zahl) ? w->zahl : vorgabe;
}

bool Wert::wahrVon(const std::string& k, bool vorgabe) const {
    const Wert* w = feld(k);
    return (w != nullptr && w->art == Art::Bool) ? w->zahl != 0.0 : vorgabe;
}

Wert Wert::alsText(std::string t) {
    Wert w;
    w.art = Art::Text;
    w.text = std::move(t);
    return w;
}

Wert Wert::alsZahl(double z) {
    Wert w;
    w.art = Art::Zahl;
    w.zahl = z;
    return w;
}

Wert Wert::alsBool(bool b) {
    Wert w;
    w.art = Art::Bool;
    w.zahl = b ? 1.0 : 0.0;
    return w;
}

Wert Wert::alsListe() {
    Wert w;
    w.art = Art::Liste;
    return w;
}

Wert Wert::alsObjekt() {
    Wert w;
    w.art = Art::Objekt;
    return w;
}

Wert& Wert::setze(const std::string& k, Wert w) {
    art = Art::Objekt;
    for (auto& [name, alt] : objekt) {
        if (name == k) {
            alt = std::move(w);
            return alt;
        }
    }
    objekt.emplace_back(k, std::move(w));
    return objekt.back().second;
}

namespace {

// --- Der Leser (bis rc600 in src/update.cpp) ---------------------------------
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
    [[nodiscard]] std::size_t stelle() const { return i_; }

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

void schreibeZahl(double z, std::string& out) {
    char b[40];
    // Ganze Zahlen ohne Komma ("43", nicht "43.000000"); sonst so genau, dass
    // sie sich gleich zurueckliest.
    if (std::isfinite(z) && std::fabs(z) < 1.0e15 && z == std::floor(z)) {
        std::snprintf(b, sizeof(b), "%.0f", z);
    } else if (std::isfinite(z)) {
        std::snprintf(b, sizeof(b), "%.17g", z);
        // %.17g schreibt 0.5 als 0.5, 0.1 aber als 0.10000000000000001 -
        // die kuerzeste Form waehlen, die sich gleich zurueckliest.
        for (int stellen = 1; stellen <= 17; ++stellen) {
            char k[40];
            std::snprintf(k, sizeof(k), "%.*g", stellen, z);
            if (std::strtod(k, nullptr) == z) {
                std::snprintf(b, sizeof(b), "%s", k);
                break;
            }
        }
    } else {
        std::snprintf(b, sizeof(b), "null");   // JSON kennt kein NaN/Unendlich
    }
    out += b;
}

void schreibeIn(const Wert& w, std::string& out) {
    switch (w.art) {
        case Wert::Art::Null: out += "null"; break;
        case Wert::Art::Bool: out += (w.zahl != 0.0) ? "true" : "false"; break;
        case Wert::Art::Zahl: schreibeZahl(w.zahl, out); break;
        case Wert::Art::Text: out += zeichenkette(w.text); break;
        case Wert::Art::Liste: {
            out += '[';
            for (std::size_t k = 0; k < w.liste.size(); ++k) {
                if (k > 0) { out += ','; }
                schreibeIn(w.liste[k], out);
            }
            out += ']';
            break;
        }
        case Wert::Art::Objekt: {
            out += '{';
            for (std::size_t k = 0; k < w.objekt.size(); ++k) {
                if (k > 0) { out += ','; }
                out += zeichenkette(w.objekt[k].first);
                out += ':';
                schreibeIn(w.objekt[k].second, out);
            }
            out += '}';
            break;
        }
    }
}

}  // namespace

bool lies(const std::string& text, Wert& out, std::string* fehler) {
    out = Wert{};
    Leser l(text);
    if (!l.lies(out) || !l.amEnde()) {
        if (fehler != nullptr) { *fehler = "kein gueltiges JSON (Stelle " + std::to_string(l.stelle()) + ")"; }
        return false;
    }
    return true;
}

std::string schreibe(const Wert& w) {
    std::string out;
    schreibeIn(w, out);
    return out;
}

std::string zeichenkette(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[16];
                    std::snprintf(b, sizeof(b), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += b;
                } else {
                    out += c;   // UTF-8 bleibt, wie es ist
                }
        }
    }
    out += '"';
    return out;
}

}  // namespace bhed::json
