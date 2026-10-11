# -*- coding: utf-8 -*-
"""
Erzeugt include/bhed/changelog.h und src/changelog.cpp aus tools/changelog.txt.

Wie gen_i18n.py: Nicht-ASCII als ausdrueckliche UTF-8-Bytes, damit MSVC ohne
/utf-8 die chinesischen und japanischen Texte nicht in die ANSI-Codepage
umwandelt. Bricht ab, wenn einer Fassung eine Sprache fehlt - ein leerer
Eintrag im Fenster faellt sonst erst beim Anwender auf.
"""
import os
import re
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(HIER)
SPRACHEN = ['en', 'de', 'zh', 'ja']   # Reihenfolge wie i18n::Language


def esc(s):
    out = []
    prev_hex = False
    for b in s.encode('utf-8'):
        if b == 0x5C:
            out.append('\\\\'); prev_hex = False
        elif b == 0x22:
            out.append('\\"'); prev_hex = False
        elif b == 0x0A:
            out.append('\\n'); prev_hex = False
        elif b < 0x80:
            c = chr(b)
            # Ein Hexzeichen direkt nach \xNN gehoerte sonst zur Zahl.
            if prev_hex and c in '0123456789abcdefABCDEF':
                out.append('""')
            out.append(c); prev_hex = False
        else:
            out.append('\\x%02x' % b); prev_hex = True
    return ''.join(out)


def lies():
    eintraege = []
    akt = None
    sprache = None
    for nr, zeile in enumerate(open(os.path.join(HIER, 'changelog.txt'), encoding='utf-8'), 1):
        z = zeile.rstrip('\n').rstrip('\r')
        if z.startswith('#') and not z.startswith('##'):
            continue
        m = re.match(r'^## (\S+) (\d{4}-\d{2}-\d{2})\s*$', z)
        if m:
            akt = {'fassung': m.group(1), 'datum': m.group(2), 'text': {}}
            eintraege.append(akt)
            sprache = None
            continue
        m = re.match(r'^\[(en|de|zh|ja)\]\s*$', z)
        if m:
            if akt is None:
                sys.exit('Zeile %d: Sprache ohne Fassung' % nr)
            sprache = m.group(1)
            akt['text'][sprache] = []
            continue
        if not z.strip():
            continue
        if akt is None or sprache is None:
            sys.exit('Zeile %d: Text ausserhalb einer Fassung/Sprache: %s' % (nr, z))
        if not z.startswith('- '):
            sys.exit('Zeile %d: Punkte beginnen mit "- ": %s' % (nr, z))
        akt['text'][sprache].append(z[2:].strip())
    for e in eintraege:
        for s in SPRACHEN:
            if not e['text'].get(s):
                sys.exit('Fassung %s: Sprache %s fehlt' % (e['fassung'], s))
    return eintraege


def main():
    eintraege = lies()
    h = '''// Erzeugt von tools/gen_changelog.py aus tools/changelog.txt - nicht von Hand aendern.
#pragma once

#include <cstddef>

namespace bhed::changelog {

struct Eintrag {
    const char* fassung;   // "rc583"
    const char* datum;     // "2026-10-11"
    // Je Sprache (Reihenfolge wie i18n::Language) die Punkte, durch '\\n'
    // getrennt, UTF-8.
    const char* text[4];
};

// Neueste Fassung zuerst.
[[nodiscard]] const Eintrag* eintraege();
[[nodiscard]] std::size_t anzahl();

}  // namespace bhed::changelog
'''
    c = ['// Erzeugt von tools/gen_changelog.py aus tools/changelog.txt - nicht von Hand aendern.',
         '#include "bhed/changelog.h"', '', '#include <cstddef>', '', 'namespace bhed::changelog {', '', 'namespace {',
         'const Eintrag kEintraege[] = {']
    for e in eintraege:
        teile = ['"%s"' % esc('\n'.join(e['text'][s])) for s in SPRACHEN]
        c.append('    {"%s", "%s", {%s}},' % (e['fassung'], e['datum'], ',\n      '.join(teile)))
    c += ['};', '}  // namespace', '',
          'const Eintrag* eintraege() { return kEintraege; }',
          'std::size_t anzahl() { return sizeof(kEintraege) / sizeof(kEintraege[0]); }', '',
          '}  // namespace bhed::changelog', '']
    open(os.path.join(WURZEL, 'include', 'bhed', 'changelog.h'), 'w', encoding='utf-8', newline='\n').write(h)
    open(os.path.join(WURZEL, 'src', 'changelog.cpp'), 'w', encoding='utf-8', newline='\n').write('\n'.join(c))
    print('Changelog erzeugt: %d Fassungen x %d Sprachen' % (len(eintraege), len(SPRACHEN)))


main()
