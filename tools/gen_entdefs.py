# -*- coding: utf-8 -*-
"""
Entity-Beschreibungen fuer den Entity-Inspektor der Kartenansicht.

Zwei Quellen, getrennt gehalten:

  tools/entdefs.txt        FAKTEN aus dem Movie-Duels-Quelltext: Klassenname,
                           Farbe, Groesse, Spawnflags (Name und Bit) und die
                           Feldnamen. Keine Saetze aus dem Quelltext - nur
                           Schnittstellenangaben, ohne die man eine .ent gar
                           nicht schreiben kann.
  tools/entdefs_texte.txt  die ERKLAERUNGEN, selbst geschrieben, in allen vier
                           Sprachen (en, de, zh, ja).

  python tools/gen_entdefs.py --aus-quelle <Movie-Duels-Quelltext>
      liest die /*QUAKED ... */-Koepfe aus code/game/*.cpp und schreibt
      tools/entdefs.txt. Spawnflags Bit fuer Bit in der Reihenfolge der
      Kopfzeile ("x" = unbenutzt). Ein Feld gilt nur, wenn das Spiel es beim
      Spawnen wirklich einliest (Feldtabelle in g_spawn.cpp und
      G_SpawnString/Int/Float/... im Spielcode) - sonst stuenden Werte wie die
      Verhaltensnamen "idle", "roam" des NPC_spawner als Felder da.

  python tools/gen_entdefs.py
      erzeugt include/bhed/entdefs.h und src/entdefs.cpp aus beiden Dateien.

Anlass (11.10.2026): beim Bau einer eigenen Mission mussten die Spawnflags
(16 DROPTOFLOOR, 32 CINEMATIC ...) im Spielcode nachgelesen werden. Die Texte
sind eigene, damit die Lizenz von BehavEd-Remake frei bleibt (shank, 11.10.).
"""
import glob
import os
import re
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(HIER)
TXT = os.path.join(HIER, 'entdefs.txt')
TEXTE = os.path.join(HIER, 'entdefs_texte.txt')
SPRACHEN = 4

KOPF = re.compile(r'^/\*QUAKED\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*(\?|\(([^)]*)\)\s*\(([^)]*)\))?\s*(.*)$')
FELD = re.compile(r'^\s*"?([A-Za-z_][A-Za-z0-9_]*)"?(?:\s*(?:-+|:)|\t)')
FLAGNAME = re.compile(r'^[A-Za-z_][A-Za-z0-9_-]*$')


def zahlen(t, n):
    w = [float(x) for x in t.split()]
    if len(w) != n:
        raise ValueError(t)
    return w


def spielfelder(quelle):
    """Alle Felder, die das Spiel beim Spawnen einliest (klein)."""
    felder = set()
    for d in glob.glob(os.path.join(quelle, 'code', 'game', '*.cpp')):
        text = open(d, encoding='latin-1').read()
        for m in re.finditer(r'G_Spawn(?:String|Int|Float|Vector|Vector4|Flag|Boolean)\(\s*"([A-Za-z0-9_]+)"', text):
            felder.add(m.group(1).lower())
        if os.path.basename(d) == 'g_spawn.cpp':
            a = text.find('field_t fields[]')
            b = text.find('};', a)
            for m in re.finditer(r'\{\s*"([A-Za-z0-9_]+)"', text[a:b]):
                felder.add(m.group(1).lower())
    return felder


def aus_quelle(quelle):
    dateien = sorted(glob.glob(os.path.join(quelle, 'code', 'game', '*.cpp')))
    if not dateien:
        sys.exit('keine code/game/*.cpp unter %s' % quelle)
    gueltig = spielfelder(quelle)
    defs = {}
    for d in dateien:
        zeilen = open(d, encoding='latin-1').read().split('\n')
        i = 0
        while i < len(zeilen):
            m = KOPF.match(zeilen[i].rstrip())
            if not m:
                i += 1
                continue
            name, farbe, groesse, mins, maxs, rest = m.groups()
            koerper = []
            ende = '*/' in rest
            if ende:
                rest = rest[:rest.index('*/')]
            i += 1
            while not ende and i < len(zeilen):
                z = zeilen[i]
                if '*/' in z:
                    koerper.append(z[:z.index('*/')])
                    ende = True
                else:
                    koerper.append(z)
                i += 1
            if name in defs:
                continue   # die erste gewinnt (die Engine registriert je Name einmal)
            flags = [(1 << k, f) for k, f in enumerate(rest.split()) if f.lower() != 'x' and FLAGNAME.match(f)]
            flagnamen = {f.upper() for _, f in flags}
            felder = []
            for z in koerper:
                fm = FELD.match(z)
                if not fm:
                    continue
                k = fm.group(1)
                if k.upper() in flagnamen or k.lower() not in gueltig:
                    continue
                if all(k.lower() != x.lower() for x in felder):
                    felder.append(k)
            defs[name] = {
                'farbe': zahlen(farbe, 3),
                'brush': groesse == '?' or groesse is None,
                'mins': zahlen(mins, 3) if mins else [0, 0, 0],
                'maxs': zahlen(maxs, 3) if maxs else [0, 0, 0],
                'flags': flags,
                'felder': felder,
                'datei': os.path.basename(d),
            }
    with open(TXT, 'w', encoding='utf-8', newline='\n') as o:
        o.write('# Erzeugt von tools/gen_entdefs.py --aus-quelle aus den QUAKED-Koepfen des\n')
        o.write('# Movie-Duels-Quelltexts (code/game/*.cpp). NUR Fakten: Name, Farbe, Groesse,\n')
        o.write('# Spawnflags (Bit, Name), Felder, die das Spiel einliest. Die Erklaerungen\n')
        o.write('# stehen in tools/entdefs_texte.txt (eigene Texte).\n')
        o.write('# class / from / color / size (mins maxs) oder brush / flag <bit> <NAME> / key <name> / end\n\n')
        for name in sorted(defs, key=str.lower):
            e = defs[name]
            o.write('class %s\nfrom %s\ncolor %g %g %g\n' % ((name, e['datei']) + tuple(e['farbe'])))
            if e['brush']:
                o.write('brush\n')
            else:
                o.write('size %g %g %g %g %g %g\n' % tuple(e['mins'] + e['maxs']))
            for bit, f in e['flags']:
                o.write('flag %d %s\n' % (bit, f))
            for k in e['felder']:
                o.write('key %s\n' % k)
            o.write('end\n\n')
    print('%d Klassen nach %s' % (len(defs), TXT))


def cstr(t):
    # Der Quelltext bleibt ASCII (lint_portability): alles andere als UTF-8-Bytes
    # in Oktal - drei Ziffern, danach endet das Escape sicher.
    aus = []
    for b in t.encode('utf-8'):
        c = chr(b)
        if c == '\\':
            aus.append('\\\\')
        elif c == '"':
            aus.append('\\"')
        elif 32 <= b < 127:
            aus.append(c)
        else:
            aus.append('\\%03o' % b)
    return '"' + ''.join(aus) + '"'


def fakten_lesen():
    defs = []
    cur = None
    for z in open(TXT, encoding='utf-8'):
        z = z.rstrip('\n')
        if not z or z.startswith('#'):
            continue
        wort, _, rest = z.partition(' ')
        if wort == 'class':
            cur = {'name': rest, 'farbe': [1, 1, 1], 'brush': False, 'mins': [0, 0, 0], 'maxs': [0, 0, 0],
                   'flags': [], 'keys': []}
        elif wort == 'from':
            pass
        elif wort == 'color':
            cur['farbe'] = zahlen(rest, 3)
        elif wort == 'brush':
            cur['brush'] = True
        elif wort == 'size':
            w = zahlen(rest, 6)
            cur['mins'], cur['maxs'] = w[:3], w[3:]
        elif wort == 'flag':
            bit, _, n = rest.partition(' ')
            cur['flags'].append((int(bit), n))
        elif wort == 'key':
            if all(k.lower() != rest.lower() for k in cur['keys']):
                cur['keys'].append(rest)
        elif wort == 'end':
            defs.append(cur)
            cur = None
        else:
            sys.exit('entdefs.txt: unbekannte Zeile %r' % z)
    return {d['name']: d for d in defs}


def texte_lesen(defs):
    """art | klasse | name | en | de | zh | ja  (klasse * = fuer alle)."""
    texte = {}
    for nr, z in enumerate(open(TEXTE, encoding='utf-8'), 1):
        z = z.rstrip('\n')
        if not z.strip() or z.lstrip().startswith('#'):
            continue
        t = [x.strip() for x in z.split(' | ')]
        if len(t) != 3 + SPRACHEN or not all(t[3:]):
            sys.exit('entdefs_texte.txt:%d: art | klasse | name | en | de | zh | ja erwartet' % nr)
        art, klasse, name = t[:3]
        if art not in ('class', 'flag', 'key'):
            sys.exit('entdefs_texte.txt:%d: unbekannte Art %r' % (nr, art))
        if klasse != '*' and klasse not in defs:
            sys.exit('entdefs_texte.txt:%d: unbekannte Klasse %r' % (nr, klasse))
        if art == 'flag' and all(f.upper() != name.upper() for _, f in defs[klasse]['flags']):
            sys.exit('entdefs_texte.txt:%d: %s hat kein Flag %r' % (nr, klasse, name))
        schluessel = (art, klasse, name.lower())
        if schluessel in texte:
            sys.exit('entdefs_texte.txt:%d: doppelt %r' % (nr, schluessel))
        texte[schluessel] = t[3:]
        schreibweise[schluessel] = name
    return texte


schreibweise = {}   # (art, klasse, name klein) -> Name wie in entdefs_texte.txt


def erzeugen():
    defs = fakten_lesen()
    texte = texte_lesen(defs)
    # Felder mit eigenem Text gehoeren dazu, auch wenn der Kopf sie nicht nennt
    # (target_speaker "noise" steht dort ohne Strich).
    for (art, klasse, name) in texte:
        if art == 'key' and klasse != '*' and all(k.lower() != name for k in defs[klasse]['keys']):
            defs[klasse]['keys'].append(schreibweise[(art, klasse, name)])
    leer = [''] * SPRACHEN

    def text_fuer(art, klasse, name):
        return texte.get((art, klasse, name.lower())) or (texte.get((art, '*', name.lower())) if art == 'key' else None) or leer

    namen = sorted(defs, key=str.lower)
    h = '''// Erzeugt von tools/gen_entdefs.py aus tools/entdefs.txt und
// tools/entdefs_texte.txt - nicht von Hand aendern.
//
// Die Entity-Klassen des Spiels mit Groesse, Spawnflags und Feldern. Die
// Fakten (Namen, Bits, Groessen) stammen aus den QUAKED-Koepfen des
// Movie-Duels-Quelltexts, die Erklaerungen sind eigene, in vier Sprachen.
#pragma once

#include <cstddef>
#include <string_view>

namespace bhed::ent {

inline constexpr std::size_t kSprachen = 4;   // wie i18n::Language: en, de, zh, ja

struct Flag {
    int bit;                         // Wert in "spawnflags" (1, 2, 4, ...)
    const char* name;                // DROPTOFLOOR
    const char* text[kSprachen];     // leer, wenn es keine Erklaerung gibt
};

struct Feld {
    const char* name;                // wie im Quelltext; die Engine vergleicht ohne Gross/Klein
    const char* text[kSprachen];
};

struct Def {
    const char* klasse;
    float farbe[3];
    bool brush;                      // braucht ein Brush-Modell aus der Karte ("?" im Quelltext)
    float mins[3];
    float maxs[3];
    const char* text[kSprachen];
    const Flag* flags;
    std::size_t flagAnzahl;
    const Feld* felder;
    std::size_t feldAnzahl;
};

// Ohne Gross/Klein - "npc_spawner" findet NPC_spawner. nullptr, wenn unbekannt.
[[nodiscard]] const Def* def(std::string_view klasse) noexcept;
[[nodiscard]] std::size_t anzahl() noexcept;
[[nodiscard]] const Def& at(std::size_t i) noexcept;
// Der Text in der Sprache `sprache` (0..3), sonst englisch, sonst "".
[[nodiscard]] const char* uebersetzt(const char* const (&text)[kSprachen], int sprache) noexcept;

}  // namespace bhed::ent
'''
    c = ['// Erzeugt von tools/gen_entdefs.py aus tools/entdefs.txt und tools/entdefs_texte.txt -',
         '// nicht von Hand aendern.', '#include "bhed/entdefs.h"', '', '#include <algorithm>', '#include <cctype>',
         '#include <cstddef>', '', 'namespace bhed::ent {', 'namespace {', '']

    def arr(t):
        return '{' + ', '.join(cstr(x) for x in t) + '}'

    def fl(v):
        return repr(float(v)) + 'F'

    for k, n in enumerate(namen):
        d = defs[n]
        if d['flags']:
            c.append('const Flag kF%d[] = {' % k)
            for bit, f in d['flags']:
                c.append('    {%d, %s, %s},' % (bit, cstr(f), arr(text_fuer('flag', n, f))))
            c.append('};')
        if d['keys']:
            c.append('const Feld kK%d[] = {' % k)
            for f in d['keys']:
                c.append('    {%s, %s},' % (cstr(f), arr(text_fuer('key', n, f))))
            c.append('};')
    c.append('')
    c.append('const Def kDefs[] = {')
    for k, n in enumerate(namen):
        d = defs[n]
        c.append('    {%s, {%s, %s, %s}, %s, {%s, %s, %s}, {%s, %s, %s}, %s, %s, %d, %s, %d},' % (
            cstr(n), *map(fl, d['farbe']), 'true' if d['brush'] else 'false', *map(fl, d['mins']),
            *map(fl, d['maxs']), arr(texte.get(('class', n, '-'), leer)),
            ('kF%d' % k) if d['flags'] else 'nullptr', len(d['flags']),
            ('kK%d' % k) if d['keys'] else 'nullptr', len(d['keys'])))
    c += ['};', '',
          'bool gleich(std::string_view a, std::string_view b) noexcept {',
          '    return a.size() == b.size() &&',
          '           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {',
          '               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));',
          '           });',
          '}',
          '}  // namespace', '',
          'const Def* def(std::string_view klasse) noexcept {',
          '    for (const Def& d : kDefs) {',
          '        if (gleich(d.klasse, klasse)) { return &d; }',
          '    }',
          '    return nullptr;',
          '}', '',
          'std::size_t anzahl() noexcept { return sizeof(kDefs) / sizeof(kDefs[0]); }', '',
          'const Def& at(std::size_t i) noexcept { return kDefs[std::min(i, anzahl() - 1)]; }', '',
          'const char* uebersetzt(const char* const (&text)[kSprachen], int sprache) noexcept {',
          '    if (sprache >= 0 && static_cast<std::size_t>(sprache) < kSprachen && text[sprache][0] != \'\\0\') {',
          '        return text[sprache];',
          '    }',
          '    return text[0];',
          '}', '',
          '}  // namespace bhed::ent']
    open(os.path.join(WURZEL, 'include', 'bhed', 'entdefs.h'), 'w', encoding='utf-8', newline='\n').write(h)
    open(os.path.join(WURZEL, 'src', 'entdefs.cpp'), 'w', encoding='utf-8', newline='\n').write('\n'.join(c) + '\n')
    mit = sum(1 for v in texte)
    print('%d Klassen erzeugt, %d eigene Texte' % (len(defs), mit))


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--aus-quelle':
        aus_quelle(sys.argv[2])
    elif len(sys.argv) == 1:
        erzeugen()
    else:
        sys.exit(__doc__)
