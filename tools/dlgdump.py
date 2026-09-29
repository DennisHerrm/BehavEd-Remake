#!/usr/bin/env python3
"""DIALOG / DIALOGEX Ressourcen aus einer PE-Datei lesen."""
import pefile, struct, sys

WSTYLE = {
    0x80000000: 'WS_POPUP', 0x40000000: 'WS_CHILD', 0x10000000: 'WS_VISIBLE',
    0x08000000: 'WS_DISABLED', 0x00C00000: 'WS_CAPTION', 0x00080000: 'WS_SYSMENU',
    0x00040000: 'WS_THICKFRAME', 0x00020000: 'WS_MINIMIZEBOX', 0x00010000: 'WS_MAXIMIZEBOX',
    0x00800000: 'WS_BORDER', 0x00400000: 'WS_DLGFRAME', 0x00200000: 'WS_VSCROLL',
    0x00100000: 'WS_HSCROLL', 0x00010000: 'WS_GROUP', 0x00020000: 'WS_TABSTOP',
}
ATOM = {0x80: 'BUTTON', 0x81: 'EDIT', 0x82: 'STATIC', 0x83: 'LISTBOX',
        0x84: 'SCROLLBAR', 0x85: 'COMBOBOX'}

BS = {0: 'PUSHBUTTON', 1: 'DEFPUSHBUTTON', 2: 'CHECKBOX', 3: 'AUTOCHECKBOX',
      4: 'RADIOBUTTON', 5: '3STATE', 6: 'AUTO3STATE', 7: 'GROUPBOX',
      8: 'USERBUTTON', 9: 'AUTORADIOBUTTON', 0xB: 'OWNERDRAW'}


class R:
    def __init__(self, b): self.b, self.p = b, 0
    def u16(self):
        v = struct.unpack_from('<H', self.b, self.p)[0]; self.p += 2; return v
    def i16(self):
        v = struct.unpack_from('<h', self.b, self.p)[0]; self.p += 2; return v
    def u32(self):
        v = struct.unpack_from('<I', self.b, self.p)[0]; self.p += 4; return v
    def align(self):
        self.p = (self.p + 3) & ~3
    def sz(self):
        """Name oder Ordinal"""
        first = self.u16()
        if first == 0:
            return ''
        if first == 0xFFFF:
            return ATOM.get(self.u16(), 'ORD')
        out = []
        c = first
        while c:
            out.append(chr(c)); c = self.u16()
        return ''.join(out)


def styles(s, cls):
    out = [n for m, n in WSTYLE.items() if s & m == m and n not in ('WS_GROUP', 'WS_TABSTOP')]
    if cls == 'BUTTON':
        out.append(BS.get(s & 0xF, f'BS_{s & 0xF:X}'))
    if cls == 'STATIC':
        out.append({0: 'SS_LEFT', 1: 'SS_CENTER', 2: 'SS_RIGHT',
                    3: 'SS_ICON', 0xE: 'SS_BITMAP'}.get(s & 0xF, f'SS_{s & 0xF:X}'))
    if s & 0x00020000 and cls != 'BUTTON':
        out.append('WS_TABSTOP')
    return out


def parse(data):
    r = R(data)
    sig = struct.unpack_from('<HH', data, 0)
    ex = (sig[1] == 0xFFFF and sig[0] == 1)
    if ex:
        r.u16(); r.u16(); r.u32()          # version, signature, helpID
        exstyle = r.u32(); style = r.u32()
    else:
        style = r.u32(); exstyle = r.u32()
    n = r.u16()
    x, y, cx, cy = r.i16(), r.i16(), r.i16(), r.i16()
    menu = r.sz(); cls = r.sz(); title = r.sz()
    font = None
    if style & 0x40:  # DS_SETFONT
        pt = r.u16()
        if ex:
            weight = r.u16(); italic = r.u16() & 0xFF
        else:
            weight, italic = None, None
        face = r.sz()
        font = (face, pt, weight, italic)
    ctrls = []
    for _ in range(n):
        r.align()
        if ex:
            r.u32(); cexstyle = r.u32(); cstyle = r.u32()
        else:
            cstyle = r.u32(); cexstyle = r.u32()
        cx_, cy_, cw, ch = r.i16(), r.i16(), r.i16(), r.i16()
        cid = r.u32() if ex else r.u16()
        ccls = r.sz(); ctext = r.sz()
        extra = r.u16()
        r.p += extra
        ctrls.append(dict(cls=ccls, text=ctext, id=cid, x=cx_, y=cy_, w=cw, h=ch,
                          style=cstyle, exstyle=cexstyle))
    return dict(ex=ex, title=title, x=x, y=y, cx=cx, cy=cy, font=font,
                style=style, ctrls=ctrls)


pe = pefile.PE(sys.argv[1] if len(sys.argv) > 1 else '/mnt/user-data/uploads/BehavEd.exe')
for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
    if t.struct.Id != 5:
        continue
    for e in t.directory.entries:
        for l in e.directory.entries:
            d = l.data.struct
            raw = pe.get_data(d.OffsetToData, d.Size)
            dl = parse(raw)
            print('=' * 78)
            print(f"DIALOG{'EX' if dl['ex'] else ''} {e.struct.Id}  \"{dl['title']}\"  "
                  f"{dl['cx']}x{dl['cy']} dlu   Font: {dl['font']}")
            for c in dl['ctrls']:
                st = ', '.join(styles(c['style'], c['cls']))
                print(f"   {c['cls']:<10s} id={c['id']:<6d} "
                      f"({c['x']:4d},{c['y']:4d}) {c['w']:4d}x{c['h']:<4d} "
                      f"\"{c['text']}\"  [{st}]")
