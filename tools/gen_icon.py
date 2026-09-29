#!/usr/bin/env python3
"""
Erzeugt res/behaved.ico aus einer PNG-Vorlage.

Eine .ico ist kein Bildformat, sondern ein Behaelter mit mehreren Groessen.
Windows sucht sich die passende heraus: 16 fuer Titelleiste und Dateiliste,
32 fuer den Desktop, 48 fuer die Kachelansicht, 256 fuer die grosse Vorschau.
Fehlt eine Groesse, skaliert Windows selbst - und das sieht bei Pixelgrafik
schlecht aus.

Selbst geschrieben, nicht ueber Pillow: dessen ICO-Schreiber hat auf die
Anforderung von sieben Groessen mit EINER 16x16 geantwortet. Das Format ist
ein Verzeichnis plus DIBs, keine dreissig Zeilen, und so steht nachpruefbar
drin, was drin sein soll.

ZWEI VORLAGEN, nicht eine: unter 24 Pixeln ist "bED" nicht mehr lesbar - drei
Zeichen auf sechzehn Punkten werden zum Fleck. Fuer die kleinen Groessen
liegt deshalb eine eigene Zeichnung mit nur dem "b" daneben. Das ist kein
Kniff von uns: in BehavEd.exe stehen genau dieselben zwei Bilder, ICON 1 mit
"bED" in 32x32 und ICON 2 mit "b" in 16x16. Dasselbe Problem, dieselbe
Loesung.

Aufbau:
    ICONDIR     6 Byte      Kennung, Typ 1, Anzahl
    ICONDIRENTRY 16 Byte je Bild
    Daten                   DIB ohne Dateikopf, oder PNG ab 256
"""
import pathlib, struct, sys, io
from PIL import Image

root = pathlib.Path(__file__).resolve().parent.parent
src = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'res' / 'icon_source.png'
small_src = root / 'res' / 'icon_source_small.png'
out = root / 'res' / 'behaved.ico'

base = Image.open(src).convert('RGBA')
small = Image.open(small_src).convert('RGBA') if small_src.exists() else base

SIZES = [16, 24, 32, 48, 64, 128, 256]
# Ab hier wird die grosse Vorlage benutzt; darunter die kleine.
SMALL_UP_TO = 24


def scaled(size):
    source = small if size <= SMALL_UP_TO else base
    return source.resize((size, size), Image.LANCZOS)


def dib(im):
    """32-Bit-DIB mit Alphakanal, Zeilen von unten nach oben.

    Die Hoehe im Kopf ist DOPPELT so gross wie das Bild - das ist keine
    Schlamperei, sondern Vorschrift: dahinter steht die AND-Maske. Wir
    liefern sie mit lauter Nullen, weil der Alphakanal die Durchsichtigkeit
    schon traegt; Windows braucht sie trotzdem.
    """
    w, h = im.size
    px = im.load()
    body = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = px[x, y]
            body += bytes((b, g, r, a))
    mask_row = ((w + 31) // 32) * 4
    body += bytes(mask_row * h)
    header = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, len(body),
                         0, 0, 0, 0)
    return bytes(header) + bytes(body)


entries = []
blobs = []
for s in SIZES:
    im = scaled(s)
    if s >= 256:
        # Ab 256 als PNG. Ein 256er-DIB waere 256 KB; Windows Vista und
        # neuer versteht PNG im Behaelter.
        buf = io.BytesIO()
        im.save(buf, format='PNG')
        blob = buf.getvalue()
    else:
        blob = dib(im)
    blobs.append(blob)
    entries.append((s, len(blob)))

offset = 6 + 16 * len(entries)
data = struct.pack('<HHH', 0, 1, len(entries))
for (s, size), _ in zip(entries, blobs):
    b = 0 if s >= 256 else s          # 0 bedeutet 256
    data += struct.pack('<BBBBHHII', b, b, 0, 0, 1, 32, size, offset)
    offset += size
for blob in blobs:
    data += blob

out.write_bytes(data)
print("res/behaved.ico:", ", ".join(f"{s}x{s}" for s in SIZES),
      f"- {len(data)} Byte")
print(f"   bis {SMALL_UP_TO}px: {small_src.name}, darueber: {src.name}")
