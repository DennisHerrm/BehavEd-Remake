// Lesen und Schreiben von .efx-Dateien.
#pragma once

#include <functional>
#include <string>

#include <string_view>
#include <vector>

#include "bhed/efx/effect.h"
#include "bhed/efx/gp2.h"

namespace bhed::efx {

enum class Severity { Error, Warning, Info };

struct Diagnostic {
    Severity severity = Severity::Error;
    int line = 0;
    std::string message;

    // efxed traegt hier noch eine Uebersetzungskennung mit. Die bleibt
    // drueben: behaved hat seine eigene Uebersetzung, und zwei davon im
    // selben Programm waeren eine Quelle fuer Abweichungen. Der Text
    // genuegt, weil diese Meldungen ins Protokoll gehen, nicht in die
    // Oberflaeche.
};

struct ReadResult {
    Effect effect;
    std::vector<Diagnostic> diagnostics;
    bool hasErrors() const;
};

// Liest den Inhalt einer .efx-Datei. Unbekannte Schluessel sind Warnungen,
// keine Fehler - genau wie im Spiel, das solche Zeilen ueberliest.
ReadResult read(std::string_view text);

// Zahlformat beim Schreiben.
enum class NumberFormat {
    // "%1.4g" - bitgleich mit dem alten Editor. Kuerzt 0.988235 auf 0.9882.
    Raven,
    // Kuerzeste Schreibweise, die den float exakt wiederherstellt. Wer eine
    // Datei nur oeffnet und speichert, verliert damit nichts.
    Exact,
};

struct WriteOptions {
    NumberFormat numbers = NumberFormat::Exact;
    bool crlf = true;   // Raven-Dateien haben Windows-Zeilenenden
    bool tabs = true;   // Einrueckung mit Tabs, wie im Original
};

std::string write(const Effect& effect, const WriteOptions& options = {});

// efxed hat hier noch validate(), validateAgainstShaders() und
// validateShaderNames(). Die bleiben drueben: behaved LIEST Effekte und
// zeichnet sie, es bearbeitet sie nicht. Eine Pruefliste, die niemand zu
// sehen bekommt, waere Ballast - und sie zoege den ganzen Shaderbestand
// von efxed mit herueber.

}  // namespace bhed::efx
