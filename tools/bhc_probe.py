#!/usr/bin/env python3
"""
Sondierung: BehavEd-Befehlsmodell aus .bhc + Engine-Headern rekonstruieren
und gegen 1510 echte .icarus-Skripte gegenpruefen.

Kein Produktionscode - nur Beleg, dass das Modell traegt.
"""
import re, sys, os, glob, collections

HDR = "/mnt/user-data/uploads"

# --- Teil 1: C-Enum aus Header lesen -------------------------------------
# Ein Header-Enum ist markiert mit:  typedef enum //# setType_e
# Eintraege:  NAME,//## %s="" # Beschreibung      (mit Parameterangabe)
#             NAME,//# Beschreibung               (nur Doku)
#             NAME,                               (blank)
# Ende der fuer den Editor sichtbaren Liste:  //# #eol

ENUM_TAG = re.compile(r'typedef\s+enum\s*//#\s*([A-Za-z_][A-Za-z0-9_]*)')
ENTRY = re.compile(
    r'^\s*([A-Z][A-Za-z0-9_]*)\s*(?:=\s*[^,]+?)?\s*,?\s*'
    r'(?://(#{0,2})\s*(.*))?$'
)
# Parameterspezifikation innerhalb eines //## Kommentars
PARAM = re.compile(r'(%r)?(%[a-z%])(?:=("([^"]*)"|<([^>]*)>|[-\w.]+))?')
FILEFILTER = re.compile(r'!!"([^"]*)"(?:!!)?')


def read(path):
    with open(path, 'rb') as f:
        return f.read().decode('latin-1')


def parse_params(comment):
    """'%s="" # Set entity parm1'  ->  ([param...], 'Set entity parm1')"""
    filt = None
    m = FILEFILTER.search(comment)
    if m:
        filt = m.group(1)
        comment = FILEFILTER.sub('', comment)
    desc = ''
    if '#' in comment:
        head, desc = comment.split('#', 1)
        desc = desc.strip()
    else:
        head = comment
    params = []
    for em in re.finditer(r'\$([^$]*)\$', head):
        inner = em.group(1).strip()
        if set(inner.split()) <= {'<', '>', '!', '='} and inner:
            params.append(dict(kind='%op', default=inner, locked=False, filter=None))
        else:
            params.append(dict(kind='%e', default=inner, locked=False, filter=None))
    head = re.sub(r'\$[^$]*\$', '', head)
    for pm in PARAM.finditer(head):
        locked, kind, raw, sq, ang = pm.group(1), pm.group(2), pm.group(3), pm.group(4), pm.group(5)
        if kind == '%%':
            continue
        params.append(dict(kind=kind, default=sq if sq is not None
                           else (ang if ang is not None else raw),
                           locked=bool(locked), filter=filt))
    return params, desc


def parse_enum(path, enum_name):
    """Liefert Liste von Eintraegen bis zum //# #eol Marker."""
    text = read(path)
    lines = text.split('\n')
    start = None
    for i, ln in enumerate(lines):
        m = ENUM_TAG.search(ln)
        if m and m.group(1) == enum_name:
            start = i
            break
    if start is None:
        return None
    out, depth, eol = [], 0, False
    for ln in lines[start:]:
        raw = ln.rstrip('\r')
        if '{' in raw:
            depth += 1
            continue
        if '}' in raw and depth:
            break
        if not depth:
            continue
        if re.search(r'//#\s*#eol', raw):
            eol = True
            continue
        m = ENTRY.match(raw)
        if not m or not m.group(1):
            continue
        name, hashes, comment = m.group(1), m.group(2), m.group(3) or ''
        params, desc = ([], comment.strip())
        if hashes == '##':
            params, desc = parse_params(comment)
        out.append(dict(name=name, params=params, desc=desc, after_eol=eol))
    return out


# --- Teil 2: .bhc lesen ---------------------------------------------------
TYPESET = re.compile(r'^<(%[a-z])="([^"]+)">')
INCLUDE = re.compile(r'^#include\s+"([^"]+)"\s+(\S+)')
CMD = re.compile(r'^(?:\[(\w+)\]\s*)?([a-z]\w*)\s*\((.*?)\)\s*(\{\})?\s*;?\s*(?://#?\s*(.*))?$')


def strip_block_comments(text):
    out, i, depth = [], 0, 0
    while i < len(text):
        if text.startswith('/*', i):
            depth += 1; i += 2; continue
        if text.startswith('*/', i) and depth:
            depth -= 1; i += 2; continue
        if not depth:
            out.append(text[i])
        i += 1
    return ''.join(out)


def parse_bhc(path):
    text = strip_block_comments(read(path)).replace('\r', '')
    lines = text.split('\n')
    typesets, commands, macros = {}, [], []
    i = 0
    while i < len(lines):
        ln = lines[i].strip()
        m = TYPESET.match(ln)
        if m:
            kind, name = m.group(1), m.group(2)
            entries, j = [], i + 1
            while j < len(lines) and '{' not in lines[j]:
                j += 1
            j += 1
            while j < len(lines) and '}' not in lines[j]:
                e = lines[j].strip()
                if e:
                    inc = INCLUDE.match(e)
                    if inc:
                        entries.append(('include', inc.group(1), inc.group(2)))
                    else:
                        em = re.match(r'^"?([A-Za-z_][\w]*)"?\s*(?://(#{1,2})\s*(.*))?$', e)
                        num = re.match(r'^(-?\d+)\s*$', e)
                        if em:
                            p, d = ([], em.group(3) or '')
                            if em.group(2) == '##':
                                p, d = parse_params(em.group(3) or '')
                            entries.append(('entry', em.group(1), p, d))
                        elif num:
                            entries.append(('entry', num.group(1), [], ''))
                j += 1
            typesets[name] = dict(kind=kind, entries=entries)
            i = j + 1
            continue
        mm = re.match(r'^"(\w+)"\s*(?://#\s*(.*))?$', ln)
        if mm and i + 1 < len(lines) and lines[i + 1].strip().startswith('{'):
            macros.append(mm.group(1))
            while i < len(lines) and '}' not in lines[i]:
                i += 1
            i += 1
            continue
        c = CMD.match(ln)
        if c and c.group(2):
            params, desc = parse_params(c.group(3) + (' # ' + (c.group(5) or '') if c.group(5) else ''))
            commands.append(dict(icon=c.group(1), name=c.group(2), params=params,
                                 block=bool(c.group(4)), desc=desc, raw=ln))
        i += 1
    return typesets, commands, macros


# --- Bericht --------------------------------------------------------------
def main():
    ts, cmds, macros = parse_bhc(os.path.join(HDR, 'behaved.bhc'))
    print(f"behaved.bhc:  {len(ts)} Typmengen, {len(cmds)} Befehlszeilen, {len(macros)} Makros")
    print(f"  Makros: {', '.join(macros)}")
    print()

    # Typmengen aufloesen
    unresolved = []
    resolved = {}
    for name, d in ts.items():
        entries = []
        for e in d['entries']:
            if e[0] == 'include':
                fn = e[1].lower()
                path = None
                for cand in os.listdir(HDR):
                    if cand.lower() == fn:
                        path = os.path.join(HDR, cand)
                if path is None:
                    unresolved.append((name, e[1], 'Datei fehlt'))
                    continue
                got = parse_enum(path, e[2])
                if got is None:
                    unresolved.append((name, f"{e[1]}:{e[2]}", 'Enum nicht gefunden'))
                    continue
                vis = [g for g in got if not g['after_eol']]
                hid = [g for g in got if g['after_eol']]
                print(f"  {name:18s} <- {e[1]:16s} {e[2]:16s} "
                      f"{len(vis):4d} sichtbar" +
                      (f", {len(hid)} nach #eol verborgen" if hid else ""))
                entries += vis
            else:
                entries.append(dict(name=e[1], params=e[2], desc=e[3], after_eol=False))
        resolved[name] = entries

    print()
    if unresolved:
        print("NICHT AUFLOESBAR:")
        for u in unresolved:
            print("  ", u)
        print()

    # Parametertypen zaehlen
    kinds = collections.Counter()
    for c in cmds:
        for p in c['params']:
            kinds[p['kind']] += 1
    for e in resolved.values():
        for x in e:
            for p in x.get('params', []):
                kinds[p['kind']] += 1
    print("Parametertypen gesamt:", dict(kinds))
    print()

    # SET_TYPES gegen echte Skripte
    sets = resolved.get('SET_TYPES', [])
    known = {s['name'] for s in sets}
    withp = sum(1 for s in sets if s['params'])
    print(f"SET_TYPES: {len(sets)} Eintraege, davon {withp} mit Parameterangabe (//##)")

    used = collections.Counter()
    files = glob.glob('/home/claude/work/jascripts/**/*.icarus', recursive=True)
    for f in files:
        t = read(f)
        for m in re.finditer(r'set\s*\(\s*(?:/\*@\w+\*/\s*)?"([^"]+)"', t):
            used[m.group(1)] += 1
    unknown = {k: v for k, v in used.items() if k not in known}
    print(f"In {len(files)} Skripten: {len(used)} verschiedene set-Ziele, "
          f"{len(unknown)} davon NICHT in SET_TYPES")
    for k, v in sorted(unknown.items(), key=lambda x: -x[1])[:25]:
        print(f"    {v:5d}x  {k}")

    # Typmengen-Referenzen der Befehle pruefen
    print()
    missing = set()
    for c in cmds:
        for p in c['params']:
            if p['kind'] == '%t' and p['default'] not in resolved:
                missing.add((c['name'], p['default']))
    print("Befehle mit unbekannter Typmenge:", missing or "keine")


main()
