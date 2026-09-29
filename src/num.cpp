// num.cpp - siehe bhed/num.h
#include "bhed/num.h"

#include <cctype>
#include <charconv>
#include <cstddef>
#include <system_error>

namespace bhed {
namespace {

// Fuehrende Leerzeichen ueberspringen.
//
// std::from_chars tut das ausdruecklich NICHT - anders als std::stof, das
// es tat. Ohne diesen Schritt scheiterte jede Zeile mit Einrueckung, und
// genau so sind die .shader- und .cfg-Dateien der Engine geschrieben.
std::string_view skipSpace(std::string_view t) noexcept {
    std::size_t at = 0;
    while (at < t.size() &&
           std::isspace(static_cast<unsigned char>(t[at])) != 0) {
        ++at;
    }
    return t.substr(at);
}

template <typename T>
bool parseAny(std::string_view text, T& out) noexcept {
    const std::string_view t = skipSpace(text);
    if (t.empty()) {
        return false;
    }
    T value{};
    const auto* first = t.data();
    const auto* last = t.data() + t.size();
    const std::from_chars_result r = std::from_chars(first, last, value);
    if (r.ec != std::errc{} || r.ptr == first) {
        return false;
    }
    out = value;
    return true;
}

}  // namespace

bool parseFloat(std::string_view text, float& out) noexcept {
    return parseAny(text, out);
}

bool parseInt(std::string_view text, int& out) noexcept {
    return parseAny(text, out);
}

bool parseDouble(std::string_view text, double& out) noexcept {
    return parseAny(text, out);
}

}  // namespace bhed
