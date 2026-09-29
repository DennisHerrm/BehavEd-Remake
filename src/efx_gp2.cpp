#include "bhed/efx/gp2.h"

#include <algorithm>
#include <cctype>

namespace bhed::efx::gp2 {
namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) { return false;
}
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
}
    }
    return true;
}

bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

// Laeuft ueber den Text und zaehlt dabei mit, in welcher Zeile wir stehen.
class Cursor {
public:
    explicit Cursor(std::string_view text) : text_(text) {}

    bool eof() const { return pos_ >= text_.size(); }
    char peek(size_t off = 0) const {
        return pos_ + off < text_.size() ? text_[pos_ + off] : '\0';
    }
    int line() const { return line_; }

    void advance(size_t n = 1) {
        for (size_t i = 0; i < n && pos_ < text_.size(); ++i, ++pos_) {
            if (text_[pos_] == '\n') { ++line_;
}
        }
    }

    std::string_view slice(size_t from, size_t to) const {
        return text_.substr(from, to - from);
    }
    size_t pos() const { return pos_; }

private:
    std::string_view text_;
    size_t pos_ = 0;
    int line_ = 1;
};

void skipWhitespaceAndComments(Cursor& c, bool allowLineBreaks) {
    for (;;) {
        while (!c.eof() && isSpace(c.peek()) &&
               (allowLineBreaks || c.peek() != '\n')) {
            c.advance();
        }
        if (c.peek() == '/' && c.peek(1) == '/') {
            while (!c.eof() && c.peek() != '\n') { c.advance();
}
            continue;
        }
        if (c.peek() == '/' && c.peek(1) == '*') {
            c.advance(2);
            while (!c.eof() && (c.peek() != '*' || c.peek(1) != '/')) { c.advance();
}
            if (!c.eof()) { c.advance(2);
}
            continue;
        }
        return;
    }
}

std::string_view trimBack(std::string_view s) {
    while (!s.empty() && isSpace(s.back())) { s.remove_suffix(1);
}
    return s;
}

// Ein Token. readToEOL bestimmt, ob bis zum naechsten Weissraum gelesen wird
// oder bis Zeilenende - Letzteres macht Werte mit Leerzeichen moeglich,
// etwa "origin  0 -10 -10  10 10 10" als *einen* Wert.
std::string_view getToken(Cursor& c, bool allowLineBreaks, bool readToEOL = false) {
    skipWhitespaceAndComments(c, allowLineBreaks);
    if (c.eof()) { return {};
}

    if (c.peek() == '"') {
        c.advance();
        size_t start = c.pos();
        while (!c.eof() && c.peek() != '"') { c.advance();
}
        std::string_view tok = c.slice(start, c.pos());
        if (!c.eof()) { c.advance();  // schliessendes "
}
        return tok;
    }

    size_t start = c.pos();
    if (readToEOL) {
        while (!c.eof() && c.peek() != '\n' &&
               (c.peek() != '/' || (c.peek(1) != '/' && c.peek(1) != '*'))) {
            c.advance();
        }
        return trimBack(c.slice(start, c.pos()));
    }
    while (!c.eof() && !isSpace(c.peek())) { c.advance();
}
    return c.slice(start, c.pos());
}

bool parseGroup(Cursor& c, Group& group, bool topLevel, std::vector<Error>& errors) {
    for (;;) {
        int lineOfToken = c.line();
        std::string_view token = getToken(c, true);

        if (token.empty()) {
            if (topLevel) { return true;
}
            errors.push_back({lineOfToken, "Datei endet mitten in Gruppe \"" +
                                               group.name + "\" - es fehlt eine }"});
            return false;
        }
        if (token == "}") {
            if (topLevel) {
                errors.push_back({lineOfToken, "} ohne zugehoerige {"});
                return false;
            }
            return true;
        }

        std::string key(token);
        std::string_view next = getToken(c, true, /*readToEOL=*/true);

        if (next == "{") {
            group.subGroups.push_back(Group{});
            Group& sub = group.subGroups.back();
            sub.name = key;
            sub.line = lineOfToken;
            if (!parseGroup(c, sub, false, errors)) { return false;
}
        } else if (next == "[") {
            Property prop;
            prop.name = key;
            prop.wasList = true;
            prop.line = lineOfToken;
            for (;;) {
                int itemLine = c.line();
                std::string_view item = getToken(c, true, /*readToEOL=*/true);
                if (item.empty()) {
                    errors.push_back({itemLine, "Datei endet mitten in Liste \"" +
                                                    key + "\" - es fehlt eine ]"});
                    return false;
                }
                if (item == "]") { break;
}
                prop.values.emplace_back(item);
            }
            group.properties.push_back(std::move(prop));
        } else {
            Property prop;
            prop.name = key;
            prop.line = lineOfToken;
            prop.values.emplace_back(next);
            group.properties.push_back(std::move(prop));
        }
    }
}

}  // namespace

const Property* Group::findProperty(std::string_view key) const {
    for (const auto& p : properties) {
        if (iequals(p.name, key)) { return &p;
}
    }
    return nullptr;
}

const Group* Group::findSubGroup(std::string_view key) const {
    for (const auto& g : subGroups) {
        if (iequals(g.name, key)) { return &g;
}
    }
    return nullptr;
}

ParseResult parse(std::string_view text) {
    ParseResult result;
    Cursor c(text);
    parseGroup(c, result.topLevel, /*topLevel=*/true, result.errors);
    return result;
}

}  // namespace bhed::efx::gp2
