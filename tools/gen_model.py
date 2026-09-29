#!/usr/bin/env python3
"""
Befehlsmodell aus behaved.bhc + Engine-Headern nach data/model.json.

Erzeugt genau das, was der Event-Editor braucht:
  commands[]  Name, Symbol, Parameterliste, Beschreibung, Block ja/nein
  typesets{}  aufgeloeste Auswahllisten; Eintraege koennen eine EIGENE
              Parameterliste tragen (//## im Header) - das ist der Kern:
              set(SET_HEALTH, ...) braucht ein Zahlenfeld,
              set(SET_ANIM_BOTH, ...) eine Liste mit 1543 Eintraegen.
"""
import json, os, sys, re

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(HERE, 'tools'))

src = open(os.path.join(HERE, 'tools/bhc_probe.py')).read().split('# --- Bericht')[0]
ns = {}
exec(src, ns)
parse_bhc, parse_enum, read = ns['parse_bhc'], ns['parse_enum'], ns['read']
HDR = ns['HDR']

CMD_ICON = {
    'flush': 'I_FLUSH', 'if': 'I_IF', 'else': 'I_IF', 'loop': 'I_LOOP',
    'wait': 'I_WAITCLOCK', 'waitsignal': 'I_WAITSIGNAL', 'signal': 'I_SIGNAL',
    'sound': 'I_SOUND', 'move': 'I_MOVE', 'rotate': 'I_ROTATE',
    'remove': 'I_REMOVE', 'set': 'I_SET', 'camera': 'I_CAMERA',
    'do': 'I_DO', 'dowait': 'I_DOWAIT', 'affect': 'I_BRACE', 'task': 'I_BRACE',
    'rem': 'I_MACRO',
}


def main():
    ts, cmds, macros = parse_bhc(os.path.join(HDR, 'behaved.bhc'))
    resolved = {}
    for name, d in ts.items():
        entries = []
        for e in d['entries']:
            if e[0] == 'include':
                path = None
                for cand in os.listdir(HDR):
                    if cand.lower() == e[1].lower():
                        path = os.path.join(HDR, cand)
                if not path:
                    continue
                got = parse_enum(path, e[2]) or []
                entries += [dict(name=g['name'], params=g['params'], desc=g['desc'])
                            for g in got if not g['after_eol']]
            else:
                entries.append(dict(name=e[1], params=e[2], desc=e[3]))
        resolved[name] = dict(kind=d['kind'], entries=entries)

    out = []
    seen = {}
    for c in cmds:
        c = dict(c)
        c['icon'] = CMD_ICON.get(c['name'], 'I_SPACE')
        seen.setdefault(c['name'], []).append(c)
        out.append(c)

    model = dict(commands=out, typesets=resolved, macros=macros,
                 overloads={k: len(v) for k, v in seen.items() if len(v) > 1})
    json.dump(model, open(os.path.join(HERE, 'data/model.json'), 'w'))
    n_ent = sum(len(v['entries']) for v in resolved.values())
    print(f"{len(out)} Signaturen ({len(seen)} Befehlsnamen), "
          f"{len(resolved)} Typmengen mit {n_ent} Eintraegen, {len(macros)} Makros")
    print("Ueberladungen:", model['overloads'])
    withsub = sum(1 for v in resolved.values() for e in v['entries'] if e['params'])
    print(f"Eintraege mit eigener Parameterangabe: {withsub}")


main()
