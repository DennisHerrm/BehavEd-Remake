"""Fotos des Selbsttests (BHED_EDITORTEST=handbuch) -> Bilder des Handbuchs.

Aufruf:  python tools/handbuch/bilder.py <fotoordner> <sprache> [<ziel-handbuch>]

  <fotoordner>/<name>.bmp   das ganze Fenster
  <fotoordner>/<name>.rect  "x0 y0 x1 y1" - der Ausschnitt (fehlt/leer: ganz)
  <name>_00 ... _NN         eine Bildfolge -> ein bewegtes Bild <name>.webp

Ziel: handbuch/bilder/<sprache>/<name>.webp (Breite hoechstens 1600 Pixel,
bewegte Bilder 1100). Braucht Pillow.
"""
import glob
import os
import re
import sys

from PIL import Image

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(os.path.dirname(HIER))


def lade(bmp):
    bild = Image.open(bmp).convert("RGB")
    rect = os.path.splitext(bmp)[0] + ".rect"
    if os.path.exists(rect):
        teile = open(rect).read().split()
        if len(teile) == 4:
            x0, y0, x1, y1 = (int(t) for t in teile)
            x0, y0 = max(0, x0), max(0, y0)
            x1, y1 = min(bild.width, x1), min(bild.height, y1)
            if x1 - x0 > 8 and y1 - y0 > 8:
                bild = bild.crop((x0, y0, x1, y1))
    return bild


def klein(bild, breite):
    if bild.width <= breite:
        return bild
    h = round(bild.height * breite / bild.width)
    return bild.resize((breite, h), Image.LANCZOS)


def main():
    ordner, sprache = sys.argv[1], sys.argv[2]
    ziel = os.path.join(sys.argv[3] if len(sys.argv) > 3 else os.path.join(WURZEL, "handbuch"), "bilder", sprache)
    os.makedirs(ziel, exist_ok=True)
    folgen = {}
    einzeln = 0
    for bmp in sorted(glob.glob(os.path.join(ordner, "*.bmp"))):
        name = os.path.splitext(os.path.basename(bmp))[0]
        m = re.match(r"^(a_\w+?)_(\d\d)$", name)
        if m:
            folgen.setdefault(m.group(1), []).append(bmp)
            continue
        bild = lade(bmp)
        # Ganze Fenster breiter lassen - beim Vergroessern bleibt der Text lesbar.
        klein(bild, 2200 if bild.width > 2000 else 1600).save(os.path.join(ziel, name + ".webp"), "WEBP", quality=82, method=6)
        einzeln += 1
    for name, liste in folgen.items():
        bilder = [klein(lade(b), 1100) for b in liste]
        bilder[0].save(os.path.join(ziel, name + ".webp"), "WEBP", save_all=True, append_images=bilder[1:],
                       duration=450, loop=0, quality=75, method=4)
    groesse = sum(os.path.getsize(f) for f in glob.glob(os.path.join(ziel, "*.webp")))
    print("%s: %d Bilder, %d bewegte, zusammen %.1f MB -> %s" % (sprache, einzeln, len(folgen), groesse / 1e6, ziel))


if __name__ == "__main__":
    main()
