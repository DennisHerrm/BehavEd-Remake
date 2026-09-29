#!/usr/bin/env python3
"""
Portierungspruefer. GCC bindet vieles durchgereicht ein, MSVC nicht -
solche Fehler merkt man sonst erst auf dem anderen Rechner.

Prueft:
  1. jede benutzte std::-Funktion hat ihre Kopfdatei eingebunden
  2. keine Nicht-ASCII-Zeichen im Quelltext (Codepage-Fallen unter MSVC)
"""
import re, sys, pathlib

NEED = {
    'snprintf':'cstdio','printf':'cstdio','fprintf':'cstdio','fputs':'cstdio',
    'fwrite':'cstdio','memcpy':'cstring','memcmp':'cstring','strlen':'cstring',
    'isdigit':'cctype','isalnum':'cctype','isupper':'cctype','islower':'cctype',
    'tolower':'cctype','toupper':'cctype','isspace':'cctype',
    'from_chars':'charconv','to_chars':'charconv','stof':'string','stoi':'string',
    'to_string':'string','sort':'algorithm','max':'algorithm','min':'algorithm',
    'memmove':'cstring','abs':'cstdlib','uint8_t':'cstdint','int32_t':'cstdint',
    'size_t':'cstddef',
}

root = pathlib.Path(__file__).resolve().parent.parent
bad = 0
for f in sorted(list((root/'src').glob('*.cpp')) + list((root/'include/bhed').glob('*.h'))
                + list((root/'tests').glob('*.cpp')) + list((root/'tools').glob('*.cpp'))):
    t = f.read_text(encoding='utf-8', errors='replace')
    # Kommentare fuer die Include-Pruefung ausblenden.
    #
    # Ohne das schlaegt der Pruefer auch dann an, wenn ein Name nur im
    # Kommentar steht - und zwar genau dort, wo man ihn ERKLAERT: "hier
    # steht bewusst kein std::from_chars, weil ...". Ein Pruefer, der eine
    # Begruendung als Verstoss meldet, erzieht dazu, keine zu schreiben.
    #
    # Nur Zeilenkommentare; Blockkommentare kommen im Quelltext nicht vor
    # (lint_sources achtet darauf).
    code = re.sub(r'//[^\n]*', '', t)
    inc = set(re.findall(r'#include <([a-z_]+)>', t))
    # eigene Kopfdateien mitzaehlen: sie duerfen liefern, was sie einbinden
    for fn, hdr in NEED.items():
        if re.search(r'\bstd::' + fn + r'\b', code) and hdr not in inc:
            print(f"{f.relative_to(root)}: std::{fn} ohne <{hdr}>")
            bad += 1
    # Absolute Unix-Pfade. Anlass: tests/diagtest.cpp schrieb nach
    # "/tmp/...", was hier laeuft und unter Windows mit sieben
    # Fehlschlaegen scheitert. Solche Pfade gehoeren nicht in den
    # Quelltext - temp_directory_path oder ein Aufrufparameter.
    for i, ln in enumerate(t.split('\n'), 1):
        if ln.lstrip().startswith('//') or ln.lstrip().startswith('#'):
            continue
        m = re.search(r'"(/(?:tmp|home|usr|var|etc|mnt|opt)/[^"]*)"', ln)
        if m:
            print(f"{f.relative_to(root)}:{i}: absoluter Unix-Pfad {m.group(1)}")
            bad += 1

    nonascii = [i+1 for i, ln in enumerate(t.split('\n')) if any(ord(c) > 126 for c in ln)]
    if nonascii:
        print(f"{f.relative_to(root)}: Nicht-ASCII in Zeile(n) {nonascii[:6]}"
              f"{' ...' if len(nonascii) > 6 else ''}")
        bad += len(nonascii)

print("Portierungspruefung:", "ok" if bad == 0 else f"{bad} Beanstandung(en)")
sys.exit(1 if bad else 0)
