// Welche Fassung ist das?
//
// Bis rc448 stand im Protokollkopf nur `behaved 1.0.0` und die Bauzeit.
// Beides beantwortet die Frage nicht, die man stellt, wenn ein Bericht
// fehlt: WELCHES PAKET hat shank gebaut?
//
// Genau das ist passiert. Es gab zwei Pakete namens rc448 - eines mit einem
// Menuepunkt, eines mit dem Bericht ohne Knopf. Beide melden `1.0.0`, beide
// melden eine Bauzeit, und aus dem Protokoll liess sich nicht entscheiden,
// welches lief. Eine Auskunft, die die einzige Frage nicht beantwortet, fuer
// die es sie gibt.
//
// `tools/lint_fassung.py` haelt diesen Wert mit der obersten Ueberschrift in
// AENDERUNGEN.md gleich - vergessen kann man ihn also nicht.
#ifndef BHED_FASSUNG_H
#define BHED_FASSUNG_H

namespace bhed {

inline constexpr const char* kFassung = "rc577";

}  // namespace bhed

#endif  // BHED_FASSUNG_H
