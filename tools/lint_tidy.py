#!/usr/bin/env python3
"""lint_tidy.py - der Quelltext gegen die C++ Core Guidelines.

Die Guidelines von Stroustrup und Sutter sind der Maßstab, nach dem in der
Branche gelesen wird, und clang-tidy ist das Werkzeug, das sie durchsetzt.
Dieser Prüfer ruft es mit einer AUSGEWÄHLTEN Regelmenge auf.

Warum ausgewählt und nicht alles?

Ein voller Lauf über den Kern meldete 892 Verstöße. Davon entfielen 778 auf
drei Regeln:

    366  cppcoreguidelines-pro-bounds-pointer-arithmetic
    307  cppcoreguidelines-avoid-c-arrays
    105  cppcoreguidelines-pro-bounds-array-to-pointer-decay

Alle drei rühren von derselben Sache her: `float xyz[3]`. Genau so heißt es
in der Engine (`vec3_t` in q_shared.h), und dieses Programm bildet die
Engine nach — bis in die Feldnamen. Die Regeln zielen auf `gsl::span`, eine
Bibliothek, die dieses Programm nicht benutzt und nicht benutzen soll: ein
Werkzeug ohne Abhängigkeiten war eine Entscheidung, keine Nachlässigkeit.

Eine Warnung, die man 778 mal übergeht, ist keine Warnung mehr. Deshalb sind
diese drei abgeschaltet — und die Begründung steht hier, statt in einem
Kopfnicken.

Was BLEIBT, ist alles, was auf einen echten Fehler zeigen kann. Diese Regeln
haben beim ersten Lauf sieben Stellen gefunden, davon eine, die genau das
Muster hatte, das im Rasterisierer schon einmal zugeschlagen hat:

    static_cast<std::size_t>(a + b)   // erst addieren, dann erweitern

Aufruf:  python3 tools/lint_tidy.py [datei ...]
Ohne Angabe wird src/ geprüft.
"""

import pathlib
import shutil
import glob
import os
import subprocess
import sys

WURZEL = pathlib.Path(__file__).resolve().parent.parent

# Was geprüft wird. Alles, was auf einen Fehler zeigen kann.
AN = [
    "bugprone-*",
    "cppcoreguidelines-*",
    "performance-*",
]

# Was nicht, mit Grund. Jede Abschaltung braucht einen - sonst wächst die
# Liste, bis der Prüfer nichts mehr prüft.
AUS = {
    "cppcoreguidelines-avoid-c-arrays":
        "float xyz[3] ist die Form der Engine (vec3_t). 307 Treffer.",
    "cppcoreguidelines-pro-bounds-pointer-arithmetic":
        "dieselbe Ursache, zielt auf gsl::span. 366 Treffer.",
    "cppcoreguidelines-pro-bounds-array-to-pointer-decay":
        "dieselbe Ursache. 105 Treffer.",
    "cppcoreguidelines-pro-bounds-constant-array-index":
        "verlangt gsl::at() fuer jeden Zugriff mit Laufvariable.",
    "cppcoreguidelines-avoid-magic-numbers":
        "Formeln aus der Engine stehen mit ihren Zahlen da, mit Quellenangabe.",
    "cppcoreguidelines-avoid-do-while":
        "Geschmack, kein Fehler.",
    "bugprone-easily-swappable-parameters":
        "trifft jede Funktion mit zwei float - etwa (breite, hoehe).",
    "cppcoreguidelines-init-variables":
        "meldet auch Variablen, die in der naechsten Zeile beschrieben werden.",
    "performance-enum-size":
        "ein enum als int statt uint8_t spart nichts Messbares.",
    "cppcoreguidelines-non-private-member-variables-in-classes":
        "Datenstrukturen ohne Verhalten brauchen keine Kapselung.",
    "cppcoreguidelines-avoid-non-const-global-variables":
        "g_app ist der Programmzustand einer Oberflaeche, bewusst so.",
    "cppcoreguidelines-avoid-const-or-ref-data-members":
        "kurzlebige Wachen halten absichtlich eine Referenz.",
    "cppcoreguidelines-pro-type-vararg":
        "printf-artige Ausgabe im Protokoll und in den Proben.",
    "cppcoreguidelines-pro-type-reinterpret-cast":
        "beim Lesen von Binaerdateien unvermeidlich.",
    "bugprone-branch-clone":
        "zwei Zweige mit gleichem Rumpf koennen die klarere Schreibweise sein.",
    "bugprone-empty-catch":
        "ein bewusst uebergangener Fehler ist mit Kommentar daneben in Ordnung.",
}


def eingebauteKoepfe():
    """Wo liegen stddef.h und Verwandte?

    clang-tidy findet seine eigenen nicht immer - etwa wenn clang ohne die
    zugehoerige Laufzeitumgebung ausgepackt wurde. Dann bricht es ab, und
    alles danach ist wertlos. GCCs Satz tut es genauso.
    """
    # CLANGS eigener Satz zuerst.
    #
    # GCCs Satz loest zwar stddef.h, aber sein `xmmintrin.h` ruft
    # GCC-Befehle auf, die clang nicht kennt - mapview.cpp scheiterte daran
    # mit "use of undeclared identifier '__builtin_ia32_addss'". Der Satz
    # muss zum Uebersetzer passen, nicht nur die Datei enthalten.
    #
    # Auf Ubuntu steckt clangs Satz in `libclang-common-<v>-dev`, nicht in
    # `clang-tidy` selbst.
    # Ausdruecklich gesetzt geht vor - fuer clang aus einem eigenen
    # Auspackort statt aus der Systeminstallation.
    eigen = os.environ.get("BHED_TIDY_INCLUDE", "").strip()
    kandidaten = [eigen] if eigen else []
    for muster in ("/usr/lib/llvm-*/lib/clang/*/include",
                   "/usr/lib/gcc/*/*/include"):
        kandidaten += sorted(glob.glob(muster))
    for k in kandidaten:
        if pathlib.Path(k, "stddef.h").exists():
            return ["-isystem", k]
    return []


def main() -> int:
    if shutil.which("clang-tidy") is None:
        # UEBERSPRUNGEN heisst NICHT bestanden.
        #
        # Bis rc429 stand hier nur "uebersprungen", und der Pruefzettel in
        # UEBERGABE.md meldete "13 Linter, 0 Beanstandungen". Als
        # clang-tidy zum ersten Mal wirklich lief, waren es 36. Der
        # Zettel war neun Runden lang falsch, ohne dass jemand log.
        #
        # Der Rueckgabewert bleibt 0, damit ein Rechner ohne clang-tidy
        # weiterarbeiten kann. Aber die Meldung sagt jetzt, was fehlt.
        print("clang-tidy NICHT VORHANDEN - diese Pruefung ist AUSGEFALLEN,")
        print("  nicht bestanden. Der Stand ist damit UNGEPRUEFT.")
        print("  (Installation: apt-get install clang-tidy)")
        return 0

    dateien = [pathlib.Path(a) for a in sys.argv[1:]]
    if not dateien:
        dateien = sorted((WURZEL / "src").glob("*.cpp"))

    checks = ",".join(["-*"] + AN + [f"-{k}" for k in AUS])
    treffer = []
    kaputt = []
    for f in dateien:
        r = subprocess.run(
            ["clang-tidy", "-quiet", f"--checks={checks}", str(f), "--",
             "-std=c++20", f"-I{WURZEL / 'include'}",
             f"-I{WURZEL / 'third_party'}"] + eingebauteKoepfe(),
            capture_output=True, text=True, check=False)
        # --- Ein Uebersetzungsfehler macht ALLE Warnungen wertlos ---------
        # clang-tidy bricht dann mitten im Baum ab und meldet danach
        # Warnungen aus einem halb geparsten Quelltext. Bis rc445 warf
        # dieser Pruefer die Fehlerzeile weg und zaehlte nur die Warnungen -
        # gemeldet wurden "36 Beanstandungen", und ALLE waren Schrott.
        #
        # Nachgemessen an vier Dateien: mit `stddef.h file not found` zwoelf
        # Warnungen, mit gefundenen Koepfen null. Keine einzige war echt.
        #
        # Ein Pruefer, der einen Ausfall als Befund meldet, ist schlimmer
        # als keiner: man repariert Code, an dem nichts war.
        if "error:" in r.stdout or "error:" in r.stderr:
            zeile = next((z for z in (r.stdout + r.stderr).splitlines()
                          if "error:" in z), "unbekannter Fehler")
            kaputt.append("%s: %s" % (f.name, zeile.strip()))
            continue
        for zeile in r.stdout.splitlines():
            if "warning:" in zeile:
                treffer.append(zeile)

    if kaputt:
        print("clang-tidy konnte %d Datei(en) nicht uebersetzen - diese "
              "Pruefung ist AUSGEFALLEN:" % len(kaputt))
        for k in kaputt[:5]:
            print("  " + k)
        print("  Meist fehlen die eingebauten Kopfdateien des Uebersetzers.")
        return 1

    if not treffer:
        print(f"clang-tidy: {len(dateien)} Dateien, keine Beanstandung")
        print(f"  ({len(AUS)} Regeln abgeschaltet, jede mit Begruendung im Kopf)")
        return 0

    print(f"clang-tidy: {len(treffer)} Beanstandung(en)")
    for z in treffer[:40]:
        print("  " + z)
    if len(treffer) > 40:
        print(f"  ... und {len(treffer) - 40} weitere")
    return 1


if __name__ == "__main__":
    sys.exit(main())
