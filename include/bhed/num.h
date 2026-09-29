// num.h - Zahlen aus Text lesen, ohne Ausnahmen
//
// An sieben Stellen stand bisher dieses Muster:
//
//     try {
//         const float v = std::stof(value);
//         ...
//     } catch (...) {
//         // Wert bleibt, wie er war.
//     }
//
// Das ist nicht falsch, aber es hat drei Nachteile, und clang-tidy meldet
// es zu Recht als bugprone-empty-catch:
//
//   * Ein leerer catch-Block sieht aus wie ein vergessener. Wer ihn liest,
//     muss den Kommentar glauben; wer ihn spaeter aendert, uebersieht ihn.
//   * std::stof wirft bei JEDER Abweichung - auch bei "12abc", das eine 12
//     liefert und den Rest verschluckt. Ob das gewollt war, sieht man dem
//     Aufruf nicht an.
//   * Eine Ausnahme fuer einen Tippfehler in einer Einstellungsdatei ist
//     teuer. In einer Schleife ueber tausend Zeilen merkt man das.
//
// std::from_chars (C++17) macht es ohne Ausnahmen: es gibt zurueck, wie
// weit es gekommen ist und was schiefging. Das ist der Weg, den die
// C++ Core Guidelines fuer genau diesen Fall vorsehen - eine erwartete
// Abweichung ist kein Ausnahmefall.
#ifndef BHED_NUM_H
#define BHED_NUM_H

#include <string_view>

namespace bhed {

// Liest eine Fliesskommazahl. Gibt false zurueck, wenn nichts Brauchbares
// dastand; out bleibt dann unveraendert.
//
// Fuehrende Leerzeichen werden uebergangen, nachfolgender Text stoert
// nicht - "12 34" ergibt 12. Das entspricht dem, was std::stof tat, und
// die Dateien der Engine sind so geschrieben.
[[nodiscard]] bool parseFloat(std::string_view text, float& out) noexcept;

// Dasselbe fuer ganze Zahlen.
[[nodiscard]] bool parseInt(std::string_view text, int& out) noexcept;

// Und fuer doppelte Genauigkeit - die Zeitleiste rechnet damit.
[[nodiscard]] bool parseDouble(std::string_view text, double& out) noexcept;

}  // namespace bhed
#endif
