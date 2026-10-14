"""Fotos des Selbsttests (BHED_EDITORTEST=handbuch) -> Bilder des Handbuchs.

Aufruf:  python tools/handbuch/bilder.py <fotoordner> <sprache> [<ziel-handbuch>]

  <fotoordner>/<name>.bmp   das ganze Fenster
  <fotoordner>/<name>.rect  "x0 y0 x1 y1" - der Ausschnitt (fehlt/leer: ganz)
  <name>_00 ... _NN         eine Bildfolge -> ein bewegtes Bild <name>.webp

Ziel: handbuch/bilder/<sprache>/<name>.webp (Breite hoechstens 1600 Pixel,
bewegte Bilder 1200). Braucht Pillow.
"""
import glob
import os
import re
import sys

from PIL import Image

HIER = os.path.dirname(os.path.abspath(__file__))
# Bildfolgen, die nur die Kartenansicht zeigen - ohne Text der Oberflaeche.
NUR_KARTE = {"a_kamera", "a_roff"}
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
    # Der Icon-Bogen (ikonen.bmp + ikonen.txt "Name x0 y0 x1 y1"): jedes
    # Icon einzeln als i_<Name>.png - ohne Text, darum nur auf Englisch.
    bogen = os.path.join(ordner, "ikonen.txt")
    if sprache == "en" and os.path.exists(bogen):
        gross = Image.open(os.path.join(ordner, "ikonen.bmp")).convert("RGB")
        for zeile in open(bogen):
            t = zeile.split()
            if len(t) == 5:
                x0, y0, x1, y1 = (int(v) for v in t[1:])
                gross.crop((x0, y0, x1, y1)).save(os.path.join(ziel, "i_" + t[0] + ".png"))
    for bmp in sorted(glob.glob(os.path.join(ordner, "*.bmp"))):
        name = os.path.splitext(os.path.basename(bmp))[0]
        if name == "ikonen":
            continue
        m = re.match(r"^(a_\w+?)_(\d\d)$", name)
        if m:
            folgen.setdefault(m.group(1), []).append(bmp)
            continue
        bild = lade(bmp)
        # Ganze Fenster breiter lassen - beim Vergroessern bleibt der Text lesbar.
        klein(bild, 2200 if bild.width > 2000 else 1600).save(os.path.join(ziel, name + ".webp"), "WEBP", quality=82, method=6)
        einzeln += 1
    for name, liste in folgen.items():
        # Nur die Karte im Bild (keine Beschriftung): einmal auf Englisch -
        # die anderen Sprachen fallen darauf zurueck (gen_handbuch.bild_pfad).
        if sprache != "en" and name in NUR_KARTE:
            continue
        bilder = [klein(lade(b), 1200) for b in liste]
        # Viele Bilder: fluessig (shank: "die GIFs sollten mehr Bilder haben,
        # damit sie hochwertiger sind"), wenige: Zeit zum Hinsehen.
        dauer = 110 if len(bilder) >= 30 else 450
        # Am Ende kurz stehen bleiben, bevor es von vorn beginnt.
        dauern = [dauer] * (len(bilder) - 1) + [1500]
        bilder[0].save(os.path.join(ziel, name + ".webp"), "WEBP", save_all=True, append_images=bilder[1:],
                       duration=dauern, loop=0, quality=78, method=4)
    groesse = sum(os.path.getsize(f) for f in glob.glob(os.path.join(ziel, "*.webp")))
    print("%s: %d Bilder, %d bewegte, zusammen %.1f MB -> %s" % (sprache, einzeln, len(folgen), groesse / 1e6, ziel))


if __name__ == "__main__":
    main()
