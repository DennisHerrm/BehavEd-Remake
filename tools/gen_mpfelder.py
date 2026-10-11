# -*- coding: utf-8 -*-
"""
Erzeugt include/bhed/mpfelder.h und src/mpfelder.cpp aus tools/mp_felder.tsv
und tools/mp_get.tsv.

Die Tabellen sind gegen den MP-Quelltext geprueft (codemp/game/g_ICARUScb.c,
MovieDuels/OpenJK und das Raven-MP-SDK von 2003), Beleg je Zeile in der
Spalte "note". Ausloeser war der MP-Bericht von @Seph (08.10.2026); 7 seiner
40 Aussagen waren falsch, 14 nur teilweise richtig - die Tabelle hier ist die
nachgepruefte Fassung, nicht die aus dem Bericht.
"""
import os
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(HIER)

STATUS = {'works': 'Geht', 'unknown': 'Unbekannt', 'stubbed': 'Stub', 'noop': 'OhneWirkung', 'differs': 'Anders'}
WER = {'-': 'Alle', 'any': 'Alle', 'mover': 'Mover', 'player_or_npc': 'SpielerOderNpc', 'npc': 'Npc'}


def zeilen(datei):
    for z in open(os.path.join(HIER, datei), encoding='utf-8'):
        z = z.rstrip('\n').rstrip('\r')
        if not z or z.startswith('#'):
            continue
        yield z.split('\t')


def main():
    felder = []
    for t in zeilen('mp_felder.tsv'):
        if t[0] == 'field':
            continue
        if len(t) != 6:
            sys.exit('mp_felder.tsv: 6 Spalten erwartet: %r' % t)
        name, in_sp, in_mp, status, wer, note = t
        if status not in STATUS or wer not in WER:
            sys.exit('mp_felder.tsv: unbekannter Wert in %r' % t)
        felder.append((name.upper(), in_sp != 'no', in_mp == 'yes', STATUS[status], WER[wer],
                       '[raven:stubbed]' in note))
    gets = []
    for t in zeilen('mp_get.tsv'):
        if t[0] == 'field':
            continue
        if len(t) != 3:
            sys.exit('mp_get.tsv: 3 Spalten erwartet: %r' % t)
        name, typ, urteil = t
        if urteil.startswith('always_fails') or urteil.startswith('unreachable'):
            e = 'Scheitert'
        elif 'CRASH' in urteil:
            e = 'AbsturzOhneParms'
        else:
            e = 'Geht'
        gets.append((name.upper(), typ.upper(), e))
    felder.sort()
    gets.sort()

    h = '''// Erzeugt von tools/gen_mpfelder.py aus tools/mp_felder.tsv und mp_get.tsv -
// nicht von Hand aendern.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace bhed::mp {

// Was ein set-Feld im Multiplayer tut (codemp/game/g_ICARUScb.c).
enum class Status : std::uint8_t {
    Geht,          // tut, was es soll
    Unbekannt,     // MP kennt den Namen nicht: "variable or field not found"
    Stub,          // vorhanden, tut nichts (Warnung nur mit developer 2)
    OhneWirkung,   // angenommen, tut nichts
    Anders,        // laeuft, aber anders als in SP (eigene Meldung)
};

// Auf welcher Entity es wirkt (aus den Pruefungen im MP-Code).
enum class Wer : std::uint8_t { Alle, Mover, SpielerOderNpc, Npc };

struct Feld {
    const char* name;
    bool inSp;        // in der SP-Feldliste (SDK oder Movie Duels)
    bool inMp;        // in der MP-Feldtabelle
    Status status;
    Wer wer;
    bool ravenStub;   // im originalen Raven-MP-Spielmodul nur ein Stub (wirkt nur auf OpenJK-Servern)
};

// $get( TYP, "FELD" ) im Multiplayer.
enum class Get : std::uint8_t {
    Geht,
    Scheitert,         // liefert nie einen Wert: if nimmt immer else, andere Befehle brechen ab
    AbsturzOhneParms,  // SET_PARMn als VECTOR auf einer Entity ohne Parms: Absturz
};

// nullptr, wenn der Name in keiner Liste steht. Gross/klein egal.
[[nodiscard]] const Feld* feld(std::string_view name);
// Unbekannte Kombination: Get::Geht (kein Befund).
[[nodiscard]] Get get(std::string_view feld, std::string_view typ);
[[nodiscard]] std::size_t anzahlFelder();

}  // namespace bhed::mp
'''
    c = ['// Erzeugt von tools/gen_mpfelder.py aus tools/mp_felder.tsv und mp_get.tsv -',
         '// nicht von Hand aendern.',
         '#include "bhed/mpfelder.h"', '',
         '#include <algorithm>', '#include <cctype>', '#include <cstddef>', '#include <string>', '',
         'namespace bhed::mp {', '', 'namespace {', '',
         'const Feld kFelder[] = {']
    for n, sp, mp, st, wer, rv in felder:
        c.append('    {"%s", %s, %s, Status::%s, Wer::%s, %s},' % (
            n, 'true' if sp else 'false', 'true' if mp else 'false', st, wer, 'true' if rv else 'false'))
    c += ['};', '', 'struct GetEintrag {', '    const char* feld;', '    const char* typ;', '    Get ergebnis;', '};', '',
          'const GetEintrag kGets[] = {']
    for n, typ, e in gets:
        c.append('    {"%s", "%s", Get::%s},' % (n, typ, e))
    c += ['};', '',
          'std::string gross(std::string_view s) {',
          '    std::string g(s);',
          '    for (char& ch : g) { ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); }',
          '    return g;',
          '}', '',
          '}  // namespace', '',
          'const Feld* feld(std::string_view name) {',
          '    const std::string g = gross(name);',
          '    const auto* ende = std::end(kFelder);',
          '    const auto* it = std::lower_bound(std::begin(kFelder), ende, g,',
          '                                      [](const Feld& f, const std::string& n) { return f.name < n; });',
          '    return (it != ende && it->name == g) ? it : nullptr;',
          '}', '',
          'Get get(std::string_view feld, std::string_view typ) {',
          '    const std::string f = gross(feld);',
          '    const std::string t = gross(typ);',
          '    for (const GetEintrag& e : kGets) {',
          '        if (f == e.feld && t == e.typ) { return e.ergebnis; }',
          '    }',
          '    return Get::Geht;',
          '}', '',
          'std::size_t anzahlFelder() { return sizeof(kFelder) / sizeof(kFelder[0]); }', '',
          '}  // namespace bhed::mp', '']
    open(os.path.join(WURZEL, 'include', 'bhed', 'mpfelder.h'), 'w', encoding='utf-8', newline='\n').write(h)
    open(os.path.join(WURZEL, 'src', 'mpfelder.cpp'), 'w', encoding='utf-8', newline='\n').write('\n'.join(c))
    print('MP-Felder erzeugt: %d Felder, %d $get-Eintraege' % (len(felder), len(gets)))


main()
