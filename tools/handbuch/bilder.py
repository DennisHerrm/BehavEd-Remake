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

from PIL import Image, ImageDraw

HIER = os.path.dirname(os.path.abspath(__file__))
# Bildfolgen, die nur die Kartenansicht zeigen - ohne Text der Oberflaeche.
NUR_KARTE = {"a_kamera"}
WURZEL = os.path.dirname(os.path.dirname(HIER))


def lade(bmp, mit_marken=False):
    """Das Foto, zugeschnitten auf <name>.rect. Mit mit_marken auch die
    Markierungen aus <name>.marks ("x0 y0 x1 y1" je Zeile, Bildschirmpunkte),
    schon in die Koordinaten des Ausschnitts verschoben."""
    bild = Image.open(bmp).convert("RGB")
    ox, oy = 0, 0
    rect = os.path.splitext(bmp)[0] + ".rect"
    if os.path.exists(rect):
        teile = open(rect).read().split()
        if len(teile) == 4:
            x0, y0, x1, y1 = (int(t) for t in teile)
            x0, y0 = max(0, x0), max(0, y0)
            x1, y1 = min(bild.width, x1), min(bild.height, y1)
            if x1 - x0 > 8 and y1 - y0 > 8:
                bild = bild.crop((x0, y0, x1, y1))
                ox, oy = x0, y0
    if not mit_marken:
        return bild
    marken = []
    mp = os.path.splitext(bmp)[0] + ".marks"
    if os.path.exists(mp):
        for zeile in open(mp):
            t = zeile.split()
            if len(t) >= 4:
                a, b, c, d = (float(v) for v in t[:4])
                nr = int(t[4]) if len(t) >= 5 else 0
                a, b, c, d = a - ox, b - oy, c - ox, d - oy
                # Nur, was im Ausschnitt liegt (wenigstens zur Haelfte).
                if c > 0 and d > 0 and a < bild.width and b < bild.height and c - a > 2 and d - b > 2:
                    marken.append((max(0.0, a), max(0.0, b), min(float(bild.width), c), min(float(bild.height), d), nr))
    return bild, marken


# --- Markierungen wie in den Anleitungen von Unreal ----------------------------
#
# shank: "die Bilder sind zum Teil winzig, und du weisst gar nicht, wo du
# draufklicken sollst - bei der Unreal Engine sind manche Bilder markiert mit
# Pfeilen und Kaestchen". Ein gelber Rahmen um das Element, ein gelber Pfeil
# von der Seite mit dem meisten Platz; beides mit dunkler Kontur, damit es auf
# hellem wie dunklem Grund steht.
GELB = (255, 214, 10)
KONTUR = (20, 20, 20)


def _schrift(groesse):
    from PIL import ImageFont
    for name in ("segoeuib.ttf", "arialbd.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, groesse)
        except OSError:
            continue
    return ImageFont.load_default()


def zeichne_marken(bild, marken, f):
    if not marken:
        return bild
    bild = bild.convert("RGB")
    # Strichstaerke und Groesse wachsen mit dem Bild - grosse Bilder werden im
    # Handbuch verkleinert gezeigt, die Markierung soll dann gleich kraeftig sein.
    massstab = max(1.0, bild.width / 1100.0)
    st = max(3, round(4 * massstab + 1))
    nummern = [m for m in marken if m[4] > 0]
    # Eine Reihe (alle Rahmen nebeneinander, wie eine Leiste): die Nummern in
    # einem Band unter dem Bild, wie in den Unreal-Anleitungen. Sonst steht
    # jede Nummer als runde Plakette an ihrem Rahmen.
    reihe = bool(nummern) and max(m[1] for m in nummern) < min(m[3] for m in nummern)
    if reihe:
        zusatz = 70
        neu = Image.new("RGB", (bild.width, bild.height + zusatz), (17, 17, 22))
        neu.paste(bild, (0, 0))
        bild = neu
    d = ImageDraw.Draw(bild)
    w, h = bild.size
    schrift = _schrift(46)
    klein_schrift = _schrift(round(24 * massstab))
    plaketten = []   # schon gesetzte Mittelpunkte
    einfache = [m for m in marken if m[4] == 0]
    mit_pfeil = len(einfache) == 1   # mehrere Ziele: nur Rahmen, sonst kreuzen sich die Pfeile
    for (a, b, c, e, nr) in marken:
        a, b, c, e = a * f, b * f, c * f, e * f
        pad = st + 3 if nr == 0 else 3
        box = [a - pad, b - pad, c + pad, e + pad]
        rad = max(4, st * 2)
        d.rounded_rectangle([box[0] - 2, box[1] - 2, box[2] + 2, box[3] + 2], radius=rad + 2, outline=KONTUR, width=st + 4)
        d.rounded_rectangle(box, radius=rad, outline=GELB, width=st)
        if nr > 0:
            text = str(nr)
            if reihe:
                tb = d.textbbox((0, 0), text, font=schrift)
                tw, th = tb[2] - tb[0], tb[3] - tb[1]
                tx = (box[0] + box[2]) / 2 - tw / 2 - tb[0]
                ty = h - 70 + (70 - th) / 2 - tb[1]
                d.text((tx, ty), text, font=schrift, fill=GELB, stroke_width=3, stroke_fill=KONTUR)
            else:
                # Plakette links oben am Rahmen, ganz im Bild.
                rr = round(17 * massstab)

                def im_bild(px, py):
                    return min(max(px, rr + 2), w - rr - 2), min(max(py, rr + 2), h - rr - 2)

                # Links oben am Rahmen; liegt dort schon eine, in eine andere Ecke.
                ecken = [(box[0] - 4, box[1] - 4), (box[2] + 4, box[1] - 4), (box[0] - 4, box[3] + 4),
                         (box[2] + 4, box[3] + 4), (box[0] - 4, (box[1] + box[3]) / 2)]
                mx, my = im_bild(*ecken[0])
                for ex, ey in ecken:
                    px, py = im_bild(ex, ey)
                    if all((px - qx) ** 2 + (py - qy) ** 2 > (2.3 * rr) ** 2 for qx, qy in plaketten):
                        mx, my = px, py
                        break
                plaketten.append((mx, my))
                d.ellipse([mx - rr - 2, my - rr - 2, mx + rr + 2, my + rr + 2], fill=KONTUR)
                d.ellipse([mx - rr, my - rr, mx + rr, my + rr], fill=GELB)
                tb = d.textbbox((0, 0), text, font=klein_schrift)
                d.text((mx - (tb[2] - tb[0]) / 2 - tb[0], my - (tb[3] - tb[1]) / 2 - tb[1]), text, font=klein_schrift, fill=KONTUR)
            continue
        # Pfeil: von der Seite mit dem meisten Platz auf den Rahmen zu.
        if not mit_pfeil:
            continue
        frei = {"links": box[0], "rechts": w - box[2], "oben": box[1], "unten": h - box[3]}
        # Lieber waagerecht (rechts, dann links): von oben oder unten liefe der
        # Pfeil in Menues und Listen ueber die Nachbarzeilen.
        seite = max(frei, key=frei.get)
        for kandidat in ("rechts", "links"):
            if frei[kandidat] >= 80 * massstab:
                seite = kandidat
                break
        laenge = min(110.0 * massstab, frei[seite] - 8)
        if laenge < 36:
            continue
        kopf = min(38.0 * massstab, laenge * 0.45)
        breit = max(8.0, st * 2.2)
        cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
        gap = 6
        if seite == "rechts":
            sx, sy, dx, dy = box[2] + gap, cy, 1, 0
        elif seite == "links":
            sx, sy, dx, dy = box[0] - gap, cy, -1, 0
        elif seite == "unten":
            sx, sy, dx, dy = cx, box[3] + gap, 0, 1
        else:
            sx, sy, dx, dy = cx, box[1] - gap, 0, -1
        nx, ny = -dy, dx   # senkrecht dazu
        spitze = (sx, sy)
        hals = (sx + dx * kopf, sy + dy * kopf)
        ende = (sx + dx * laenge, sy + dy * laenge)
        pfeil = [spitze,
                 (hals[0] + nx * kopf * 0.75, hals[1] + ny * kopf * 0.75),
                 (hals[0] + nx * breit, hals[1] + ny * breit),
                 (ende[0] + nx * breit, ende[1] + ny * breit),
                 (ende[0] - nx * breit, ende[1] - ny * breit),
                 (hals[0] - nx * breit, hals[1] - ny * breit),
                 (hals[0] - nx * kopf * 0.75, hals[1] - ny * kopf * 0.75)]
        d.polygon(pfeil, fill=GELB, outline=KONTUR, width=3)
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
        bild, marken = lade(bmp, True)
        # Ganze Fenster breiter lassen - beim Vergroessern bleibt der Text lesbar.
        kl = klein(bild, 2200 if bild.width > 2000 else 1600)
        kl = zeichne_marken(kl, marken, kl.width / bild.width)
        kl.save(os.path.join(ziel, name + ".webp"), "WEBP", quality=84, method=6)
        einzeln += 1
    for name, liste in folgen.items():
        # Nur die Karte im Bild (keine Beschriftung): einmal auf Englisch -
        # die anderen Sprachen fallen darauf zurueck (gen_handbuch.bild_pfad).
        if sprache != "en" and name in NUR_KARTE:
            continue
        bilder = []
        for b in liste:
            roh, marken = lade(b, True)
            kl = klein(roh, 1200)
            bilder.append(zeichne_marken(kl, marken, kl.width / roh.width))
        # Viele Bilder: fluessig (shank: "die GIFs sollten mehr Bilder haben,
        # damit sie hochwertiger sind"), wenige: Zeit zum Hinsehen.
        dauer = 110 if len(bilder) >= 30 else 450
        # Am Ende kurz stehen bleiben, bevor es von vorn beginnt.
        dauern = [dauer] * (len(bilder) - 1) + [1500]
        bilder[0].save(os.path.join(ziel, name + ".webp"), "WEBP", save_all=True, append_images=bilder[1:],
                       duration=dauern, loop=0, quality=78, method=4)
        # Das Standbild zum Anhalten (gen_handbuch: Knopf am bewegten Bild).
        bilder[len(bilder) // 2].save(os.path.join(ziel, name + "_stand.webp"), "WEBP", quality=82, method=6)
    groesse = sum(os.path.getsize(f) for f in glob.glob(os.path.join(ziel, "*.webp")))
    print("%s: %d Bilder, %d bewegte, zusammen %.1f MB -> %s" % (sprache, einzeln, len(folgen), groesse / 1e6, ziel))


if __name__ == "__main__":
    main()
