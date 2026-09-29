#!/usr/bin/env python3
"""
Zeichnet Dialog 102 aus BehavEd.exe massstabsgetreu als HTML nach.

Nichts hier ist geschaetzt: Groessen und Positionen kommen aus
data/behaved_layout.json (PE-Ressource), die Symbole aus dem
Symbolstreifen BITMAP 132, die Zuordnung Befehl->Symbol aus den
[I_...]-Praefixen in behaved.bhc.

Umrechnung Dialogeinheiten -> Pixel bei MS Sans Serif 8pt:
    baseunit = (6, 13)   ->   x * 6/4 = 1.5   |   y * 13/8 = 1.625
"""
import json, os, re, sys, html

DX, DY = 1.5, 1.625
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Befehl -> Symbol, abgelesen aus den [I_xxx]-Praefixen der behaved.bhc
CMD_ICON = {
    'flush': 'I_FLUSH', 'if': 'I_IF', 'else': 'I_IF', 'loop': 'I_LOOP',
    'wait': 'I_WAITCLOCK', 'waitsignal': 'I_WAITSIGNAL', 'signal': 'I_SIGNAL',
    'sound': 'I_SOUND', 'move': 'I_MOVE', 'rotate': 'I_ROTATE',
    'remove': 'I_REMOVE', 'set': 'I_SET', 'camera': 'I_CAMERA',
    'do': 'I_DO', 'dowait': 'I_DOWAIT',
    # ohne [I_..]-Praefix in der .bhc -> Blockoeffner bzw. leer
    'affect': 'I_BRACE', 'task': 'I_BRACE', 'run': 'I_SPACE', 'use': 'I_SPACE',
    'kill': 'I_SPACE', 'print': 'I_SPACE', 'rem': 'I_MACRO',
    'declare': 'I_SPACE', 'free': 'I_SPACE', 'play': 'I_SPACE',
}


def px(v, axis):
    return round(v * (DX if axis == 'x' else DY))


def load():
    lay = json.load(open(os.path.join(HERE, 'data/behaved_layout.json')))
    ico = json.load(open(os.path.join(HERE, 'data/icons_b64.json')))
    return lay['dialogs']['102'], ico


def parse_icarus(path):
    """Sehr einfacher Zeilenleser fuer die Baumvorschau."""
    rows, depth = [], 0
    for raw in open(path, 'rb').read().decode('latin-1').split('\n'):
        ln = raw.strip().rstrip('\r')
        if not ln or ln.startswith('//'):
            continue
        if ln == '}':
            depth = max(0, depth - 1)
            rows.append((depth, 'I_BRACE', '}'))
            continue
        m = re.match(r'^([a-z]\w*)\s*\((.*)$', ln)
        name = m.group(1) if m else ''
        icon = CMD_ICON.get(name, 'I_SPACE')
        text = re.sub(r'/\*@(\w+)\*/\s*', '', ln)
        text = text.rstrip('{').strip()
        rows.append((depth, icon, text))
        if ln.endswith('{'):
            depth += 1
    return rows


def main():
    dlg, ico = load()
    script = sys.argv[1] if len(sys.argv) > 1 else None
    rows = parse_icarus(script) if script else []

    W, H = px(dlg['cx'], 'x'), px(dlg['cy'], 'y')
    parts = []

    def img(name):
        return ico.get(name, ico['I_SPACE'])

    # Gruppenrahmen zuerst, damit Bedienelemente darueberliegen
    for c in dlg['ctrls']:
        if c['cls'] == 'BUTTON' and (c['style'] & 0xF) == 7:
            parts.append(
                f'<fieldset class="grp" style="left:{px(c["x"],"x")}px;top:{px(c["y"],"y")}px;'
                f'width:{px(c["w"],"x")}px;height:{px(c["h"],"y")}px">'
                f'<legend>{html.escape(c["text"])}</legend></fieldset>')

    for c in dlg['ctrls']:
        x, y = px(c['x'], 'x'), px(c['y'], 'y')
        w, h = px(c['w'], 'x'), px(c['h'], 'y')
        pos = f'left:{x}px;top:{y}px;width:{w}px;height:{h}px'
        cls, st, txt = c['cls'], c['style'], html.escape(c['text'])
        if cls == 'BUTTON':
            bs = st & 0xF
            if bs == 7:
                continue
            dis = ' dis' if st & 0x08000000 else ''
            if bs in (2, 3):
                parts.append(f'<div class="chk{dis}" style="{pos}">'
                             f'<span class="box"></span>{txt}</div>')
            else:
                d = ' def' if bs == 1 else ''
                parts.append(f'<button class="btn{d}{dis}" style="{pos}">{txt}</button>')
        elif cls == 'STATIC':
            parts.append(f'<div class="lbl" style="{pos}">{txt}</div>')
        elif cls == 'EDIT':
            parts.append(f'<div class="edit" style="{pos}"></div>')
        elif cls == 'SysTreeView32':
            body = []
            for d, icon, t in rows:
                body.append(f'<div class="row"><span style="width:{d*17}px"></span>'
                            f'<img src="data:image/png;base64,{img(icon)}">'
                            f'<span>{html.escape(t)}</span></div>')
            parts.append(f'<div class="tree" style="{pos}">{"".join(body)}</div>')
        elif cls == 'SysListView32':
            if c['id'] == 1011:      # Events-Liste links
                items = ['affect', 'camera', 'declare', 'do', 'dowait', 'else',
                         'flush', 'free', 'if', 'kill', 'loop', 'move', 'play',
                         'print', 'rem', 'remove', 'rotate', 'run', 'set',
                         'signal', 'sound', 'task', 'use', 'wait', 'waitsignal',
                         '--- Makros ---', 'standOnly', 'walkOnly', 'runOnly',
                         'standNoAlerts', 'walkNoAlerts', 'runNoAlerts',
                         'standNoEnemies', 'walkNoEnemies', 'runNoEnemies',
                         'standGuardNoChase', 'patrolRun', 'patrolNoChase',
                         'patrolWalkNoChase', 'patrolRunNoChase', 'default']
                body = ''.join(
                    f'<div class="row"><img src="data:image/png;base64,'
                    f'{img(CMD_ICON.get(i, "I_MACRO"))}"><span>{i}</span></div>'
                    for i in items)
            else:
                body = ('<div class="row"><span>Parsed 41 commands, 26 typesets, '
                        '15 macros from behaved.bhc</span></div>'
                        '<div class="row"><span>SET_TYPES: 261 entries '
                        '(q3_interface.h / setType_e)</span></div>'
                        '<div class="row"><span>ANIM_NAMES: 1543 entries '
                        '(anims.h / animNumber_e)</span></div>')
            parts.append(f'<div class="list" style="{pos}">{body}</div>')
        elif cls == 'msctls_progress32':
            parts.append(f'<div class="prog" style="{pos}"></div>')

    css = """
body{margin:0;background:#5a7a9a;font:11px 'MS Sans Serif',Tahoma,Verdana,sans-serif}
#win{position:relative;margin:18px auto;background:#d4d0c8;
     box-shadow:inset -1px -1px 0 #404040,inset 1px 1px 0 #fff,
                inset -2px -2px 0 #808080,inset 2px 2px 0 #dfdfdf;
     padding:0}
#cap{height:18px;background:linear-gradient(90deg,#0a246a,#a6caf0);color:#fff;
     font-weight:bold;padding:2px 4px;display:flex;justify-content:space-between}
#cap b{font-weight:bold}
#client{position:relative}
.grp{position:absolute;margin:0;padding:0;border:1px solid #fff;
     border-top-color:#808080;border-left-color:#808080;box-sizing:border-box}
.grp:after{content:'';position:absolute;inset:-1px;border:1px solid #808080;
     border-top-color:#fff;border-left-color:#fff;pointer-events:none}
.grp>legend{padding:0 3px;color:#000}
.btn{position:absolute;background:#d4d0c8;border:0;padding:0;color:#000;
     font:11px 'MS Sans Serif',Tahoma,sans-serif;cursor:default;
     box-shadow:inset -1px -1px 0 #404040,inset 1px 1px 0 #fff,
                inset -2px -2px 0 #808080,inset 2px 2px 0 #dfdfdf}
.btn:active{box-shadow:inset 1px 1px 0 #404040,inset 2px 2px 0 #808080}
.btn.def{outline:1px solid #000;outline-offset:0}
.dis{color:#808080;text-shadow:1px 1px 0 #fff}
.lbl{position:absolute;color:#000;overflow:hidden}
.edit{position:absolute;background:#fff;
     box-shadow:inset 1px 1px 0 #404040,inset -1px -1px 0 #fff,
                inset 2px 2px 0 #808080,inset -2px -2px 0 #d4d0c8}
.chk{position:absolute;display:flex;align-items:center;gap:4px;color:#000}
.chk .box{width:13px;height:13px;background:#fff;flex:none;
     box-shadow:inset 1px 1px 0 #404040,inset -1px -1px 0 #fff,
                inset 2px 2px 0 #808080,inset -2px -2px 0 #d4d0c8}
.tree,.list{position:absolute;background:#fff;overflow:auto;
     box-shadow:inset 1px 1px 0 #404040,inset -1px -1px 0 #fff,
                inset 2px 2px 0 #808080,inset -2px -2px 0 #d4d0c8}
.tree{padding:2px}
.row{display:flex;align-items:center;gap:3px;height:17px;white-space:nowrap;
     padding-left:2px}
.row img{width:16px;height:16px;image-rendering:pixelated;flex:none}
.row:hover{background:#000080;color:#fff}
.prog{position:absolute;background:#d4d0c8;
     box-shadow:inset 1px 1px 0 #404040,inset -1px -1px 0 #fff}
"""
    doc = (f'<!DOCTYPE html><meta charset="utf-8"><title>BehavEd 2.0 — Nachbau</title>'
           f'<style>{css}</style>'
           f'<div id="win" style="width:{W + 6}px">'
           f'<div id="cap"><b>BehavEd</b><span>_ □ ×</span></div>'
           f'<div id="client" style="width:{W}px;height:{H}px">'
           f'{"".join(parts)}</div></div>')
    open(os.path.join(HERE, 'behaved_original.html'), 'w').write(doc)
    print(f"Fenster {dlg['cx']}x{dlg['cy']} dlu = {W}x{H} px, "
          f"{len(dlg['ctrls'])} Bedienelemente, {len(rows)} Baumzeilen")


main()
