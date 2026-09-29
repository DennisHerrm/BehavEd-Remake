#!/usr/bin/env python3
"""
Erzeugt gui/icons_gen.h aus dem Symbolstreifen von BehavEd.exe.

Quelle ist data/bmp_132.bmp - BITMAP 132 aus der Ressource, 640x16, also
40 Symbole zu 16x16. Es sind 20 Paare: gerader Index ist die Normalfassung,
ungerader die fuer die ausgewaehlte Zeile. Die Namen stehen in ICON_OVERRIDES
der behaved.bhc; sie muessen in dieser Reihenfolge bleiben, weil die .bhc
selbst sagt:

    "these must be left in this order, since they mirror the order of icons
     in res/bitmap1.bmp"

Weiss ist die Maskenfarbe und wird durchsichtig.
"""
import pathlib, sys, struct

NAMES = ("I_BRACE I_EVENT I_MACRO I_SPACE I_SOUND I_CAMERA I_ROTATE I_REMOVE "
         "I_SET I_MOVE I_IF I_LOOP I_DO I_WAIT I_DOWAIT I_SIGNAL I_WAITSIGNAL "
         "I_FLUSH I_WAITCLOCK").split()

root = pathlib.Path(__file__).resolve().parent.parent
bmp = (root / 'data' / 'bmp_132.bmp').read_bytes()

# Windows-BMP: Dateikopf 14 Byte, dann BITMAPINFOHEADER
offbits = struct.unpack_from('<I', bmp, 10)[0]
hdr = struct.unpack_from('<IiihhIIiiII', bmp, 14)
width, height, bpp = hdr[1], hdr[2], hdr[4]
clrused = hdr[9] or (1 << bpp)
assert bpp == 8, f"erwartet 8 bit, gefunden {bpp}"

palette = []
for i in range(clrused):
    b, g, r, _ = bmp[14 + hdr[0] + i*4: 14 + hdr[0] + i*4 + 4]
    palette.append((r, g, b))

rowsize = ((width * bpp + 31) // 32) * 4
def pixel(x, y):
    # BMP-Zeilen stehen von unten nach oben
    row = height - 1 - y
    return palette[bmp[offbits + row*rowsize + x]]

count = width // 16
rgba = bytearray()
for i in range(count):
    for y in range(16):
        for x in range(16):
            r, g, b = pixel(i*16 + x, y)
            a = 0 if (r, g, b) == (255, 255, 255) else 255
            rgba += bytes((r, g, b, a))

out = ['// Erzeugt von tools/gen_icons.py - nicht von Hand aendern.',
       '//',
       '// Symbolstreifen BITMAP 132 aus BehavEd.exe: %d Symbole zu 16x16,' % count,
       '// als RGBA. Weiss ist durchsichtig (Maskenfarbe des Originals).',
       '#pragma once',
       '',
       '#include <cstddef>',
       '#include <cstdint>',
       '',
       'namespace bhed::icons {',
       '',
       'constexpr int kSize = 16;',
       'constexpr int kCount = %d;' % count,
       '',
       '// Reihenfolge wie in ICON_OVERRIDES der behaved.bhc. Gerader Index:',
       '// Normalfassung, ungerader: fuer die ausgewaehlte Zeile.',
       'constexpr const char* kNames[] = {']
for i in range(count):
    pair, half = divmod(i, 2)
    name = NAMES[pair] if pair < len(NAMES) else 'I_UNNAMED%d' % pair
    out.append('    "%s%s",' % (name, '' if half == 0 else '_ALT'))
out += ['};', '',
        'constexpr std::size_t kPixelBytes = %d;' % len(rgba),
        'extern const std::uint8_t kPixels[kPixelBytes];',
        '',
        '// Index zu einem Namen, oder -1.',
        'int indexOf(const char* name);',
        '',
        '}  // namespace bhed::icons']
(root / 'gui' / 'icons_gen.h').write_text('\n'.join(out) + '\n', encoding='utf-8')

src = ['// Erzeugt von tools/gen_icons.py - nicht von Hand aendern.',
       '#include "icons_gen.h"', '', '#include <cstring>', '',
       'namespace bhed::icons {', '',
       'const std::uint8_t kPixels[kPixelBytes] = {']
line = '    '
for n, b in enumerate(rgba):
    line += '%d,' % b
    if len(line) > 92:
        src.append(line)
        line = '    '
if line.strip():
    src.append(line)
src += ['};', '',
        'int indexOf(const char* name) {',
        '    for (int i = 0; i < kCount; ++i) {',
        '        if (std::strcmp(kNames[i], name) == 0) {',
        '            return i;',
        '        }',
        '    }',
        '    return -1;',
        '}', '',
        '}  // namespace bhed::icons']
(root / 'gui' / 'icons_gen.cpp').write_text('\n'.join(src) + '\n', encoding='utf-8')
# --- Das Logo aus BITMAP 133, 133x54, 24 Bit -------------------------------
logo = (root / 'data' / 'bmp_133.bmp').read_bytes()
loff = struct.unpack_from('<I', logo, 10)[0]
lhdr = struct.unpack_from('<IiihhIIiiII', logo, 14)
lw, lh, lbpp = lhdr[1], lhdr[2], lhdr[4]
assert lbpp == 24, "erwartet 24 bit, gefunden %d" % lbpp
lrow = ((lw * 24 + 31) // 32) * 4
lrgba = bytearray()
for y in range(lh):
    row = lh - 1 - y
    for x in range(lw):
        at = loff + row * lrow + x * 3
        b, g, r = logo[at], logo[at+1], logo[at+2]
        lrgba += bytes((r, g, b, 255))

extra = ['', '// Das Logo aus dem Ueber-Dialog des Originals (BITMAP 133).',
         'constexpr int kLogoWidth = %d;' % lw,
         'constexpr int kLogoHeight = %d;' % lh,
         'constexpr std::size_t kLogoBytes = %d;' % len(lrgba),
         'extern const std::uint8_t kLogoPixels[kLogoBytes];']
h = (root / 'gui' / 'icons_gen.h').read_text(encoding='utf-8')
h = h.replace('}  // namespace bhed::icons', '\n'.join(extra) + '\n\n}  // namespace bhed::icons')
(root / 'gui' / 'icons_gen.h').write_text(h, encoding='utf-8')

c2 = ['', 'const std::uint8_t kLogoPixels[kLogoBytes] = {']
line = '    '
for n, b in enumerate(lrgba):
    line += '%d,' % b
    if len(line) > 92:
        c2.append(line)
        line = '    '
if line.strip():
    c2.append(line)
c2.append('};')
cpp = (root / 'gui' / 'icons_gen.cpp').read_text(encoding='utf-8')
cpp = cpp.replace('}  // namespace bhed::icons', '\n'.join(c2) + '\n\n}  // namespace bhed::icons')
(root / 'gui' / 'icons_gen.cpp').write_text(cpp, encoding='utf-8')

print("%d Symbole, %d Byte RGBA -> gui/icons_gen.h + .cpp" % (count, len(rgba)))
print("Logo %dx%d, %d Byte RGBA" % (lw, lh, len(lrgba)))
