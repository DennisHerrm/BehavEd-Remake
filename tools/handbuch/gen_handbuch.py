"""Erzeugt das Handbuch (handbuch/<sprache>/*.html) aus tools/handbuch/inhalt*.py.

Aufruf:  python tools/handbuch/gen_handbuch.py [ziel]
         ziel: Ordner, in den "handbuch/" geschrieben wird (Vorgabe: Projektwurzel)

Aufbau
------
Eine Quelle fuer alle Sprachen: jeder Text ist ein T(en, de, zh, ja). Die
Seiten bestehen aus Bausteinen (Absatz, Ueberschrift, Bild, Schritte,
Hinweis, Tastentabelle, Karten ...). Daraus entstehen je Sprache:

  handbuch/<sprache>/<seite>.html   die Seiten
  handbuch/<sprache>/suche.js       der Suchindex (als Skript, damit die Suche
                                    auch aus dem Ordner ohne Server geht)
  handbuch/<sprache>/touren.json    die Tutorials im Programm (rc601: Schritte
                                    plus Angaben aus inhalt_touren.py)
  handbuch/assets/                  Stil, Suchskript, Logo
  handbuch/index.html               leitet in die Sprache des Browsers

Bilder liegen in handbuch/bilder/<sprache>/<name>.webp; fehlt eines in einer
Sprache, gilt das englische. Sie entstehen mit dem Selbsttest "handbuch"
(nur das eigene Fenster) und tools/handbuch/bilder.py.

Drei Ansichten (rc601): shank wollte "wie bei Language bei dem Reiter auch
fuer Classic, Graph und Timeline umschalten koennen". Ein Baustein
("modus", {"classic": [...], "graph": [...], "tracks": [...]}) haelt je
Ansicht eigene Bausteine; ein Schritt darf ein dict mit denselben Schluesseln
sein. Fehlt eine Ansicht, gilt Classic. Oben auf solchen Seiten steht der
Umschalter; die Wahl merkt sich der Browser (localStorage "bhed-modus").

Inline-Auszeichnung in Texten:
  **fett**   `code`   [[seite]]  [[seite#anker]]  [[seite#anker|Text]]
  <kbd>Strg+S</kbd> darf roh stehen.
"""
import html
import json
import os
import re
import shutil
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
WURZEL = os.path.dirname(os.path.dirname(HIER))
sys.path.insert(0, HIER)

from inhalt_basis import SPRACHEN, T, ui  # noqa: E402
import inhalt  # noqa: E402
import inhalt_touren  # noqa: E402  (rc601 tour: Tutorials im Programm)

# rc601: --pruefen baut in einen Zwischenordner und endet mit Fehler, wenn
# einer Seite eine Sprache fehlt (Text oder Bild) - ctest "handbuch_sprachen".
PRUEFEN = "--pruefen" in sys.argv
_ARGS = [a for a in sys.argv[1:] if a != "--pruefen"]
if PRUEFEN:
    import tempfile
    ZIEL = os.path.join(tempfile.mkdtemp(prefix="bhed_handbuch_"), "handbuch")
else:
    ZIEL = os.path.join(_ARGS[0] if _ARGS else WURZEL, "handbuch")
# Wo die Bilder liegen (handbuch/bilder steht nicht in git; beim Pruefen die
# des Arbeitsordners - fehlen sie ganz, prueft --pruefen nur die Texte).
BILDER = os.path.join(WURZEL, "handbuch", "bilder") if PRUEFEN else os.path.join(ZIEL, "bilder")
# Bilder ohne Text der Oberflaeche - in jeder Sprache das englische
# (Icons i_*, Bildfolgen nur aus der Kartenansicht wie bilder.py NUR_KARTE).
NEUTRALE_BILDER = {"a_kamera", "a_kamera_stand"}
nur_englisch = set()


def _lade_ui_texte():
    """Die Oberflaechentexte des Programms (tools/gen_i18n.py, Liste S) -
    {{Kennung}} im Handbuch wird in jeder Sprache genau so beschriftet wie
    im Programm."""
    import ast
    quelle = open(os.path.join(WURZEL, "tools", "gen_i18n.py"), encoding="utf-8").read()
    baum = ast.parse(quelle)
    for knoten in baum.body:
        if isinstance(knoten, ast.Assign) and any(getattr(z, "id", "") == "S" for z in knoten.targets):
            return {e[0]: e[1:] for e in ast.literal_eval(knoten.value)}
    raise SystemExit("gen_i18n.py: Liste S nicht gefunden")


UI_TEXTE = _lade_ui_texte()
_SPALTE = {"en": 0, "de": 1, "zh": 2, "ja": 3}


def ui_text(kennung, sp):
    if kennung not in UI_TEXTE:
        raise SystemExit("unbekannter Oberflaechentext {{%s}}" % kennung)
    t = UI_TEXTE[kennung][_SPALTE[sp]]
    t = re.sub(r"\s*\((Enter|Esc)\)$", "", t)
    t = re.sub(r"\s*\(%[-+0-9.]*[dsf]\)", "", t)
    t = re.sub(r"%[-+0-9.]*[dsf]", "", t).strip()
    return t.replace("&&", "").replace("&", "").replace("", "&").rstrip(".").strip()


def text(t, sp):
    if isinstance(t, T):
        t = t.get(sp)
    # {{icon:Name}}: das Icon des Programms (UiIcon::Name), vom Selbsttest
    # ausgeschnitten (bilder.py -> bilder/en/i_Name.png).
    t = re.sub(r"\{\{icon:(\w+)\}\}", lambda m: "⟬" + m.group(1) + "⟭", t)
    return re.sub(r"\{\{(\w+)\}\}", lambda m: "⟦" + ui_text(m.group(1), sp) + "⟧", t)


def esc(s):
    return html.escape(s, quote=True)


SEITEN = inhalt.SEITEN
NACH_ID = {s["id"]: s for s in SEITEN}


def ueberschriften(seite):
    return [b for b in seite["bloecke"] if b[0] == "h2"]


def anker_titel(seite_id, anker, sp):
    s = NACH_ID[seite_id]
    if not anker:
        return text(s["titel"], sp)
    for b in ueberschriften(s):
        if b[1] == anker:
            return text(b[2], sp)
    raise SystemExit("Anker fehlt: %s#%s" % (seite_id, anker))


def inline(t, sp):
    """Text -> HTML mit **fett**, `code`, [[links]]."""
    teile = []
    pos = 0
    muster = re.compile(r"\*\*(.+?)\*\*|`([^`]+)`|\[\[([^\]|#]+)(?:#([^\]|]+))?(?:\|([^\]]+))?\]\]|(<kbd>.*?</kbd>)|⟦([^⟧]*)⟧|⟬(\w+)⟭")
    for m in muster.finditer(t):
        teile.append(esc(t[pos:m.start()]))
        if m.group(1) is not None:
            teile.append("<strong>" + inline(m.group(1), sp) + "</strong>")
        elif m.group(2) is not None:
            teile.append("<code>" + esc(m.group(2)) + "</code>")
        elif m.group(6) is not None:
            teile.append(m.group(6))
        elif m.group(7) is not None:
            teile.append('<span class="ui">%s</span>' % esc(m.group(7)))
        elif m.group(8) is not None:
            p = os.path.join(BILDER, "en", "i_" + m.group(8) + ".png")
            if os.path.exists(p):
                teile.append('<img class="ikon" src="../bilder/en/i_%s.png" alt="">' % m.group(8))
            else:
                fehlende_bilder.add("i_" + m.group(8))
        else:
            sid, anker, beschr = m.group(3), m.group(4), m.group(5)
            if sid not in NACH_ID:
                raise SystemExit("Link auf unbekannte Seite: " + sid)
            ziel = sid + ".html" + ("#" + anker if anker else "")
            titel = anker_titel(sid, anker, sp)   # prueft auch den Anker
            beschr = beschr if beschr else titel
            teile.append('<a href="%s">%s</a>' % (ziel, esc(beschr)))
        pos = m.end()
    teile.append(esc(t[pos:]))
    return "".join(teile)


def klartext(t):
    """Fuer den Suchindex: ohne Auszeichnung."""
    t = re.sub(r"\[\[([^\]|#]+)(?:#[^\]|]+)?\|([^\]]+)\]\]", r"\2", t)
    t = re.sub(r"\[\[([^\]]+)\]\]", "", t)
    t = re.sub(r"<[^>]+>", "", t)
    t = re.sub(r"⟬\w+⟭", "", t)
    return t.replace("**", "").replace("`", "").replace("⟦", "").replace("⟧", "")


# Tastennamen wie auf der Tastatur des Landes (nur Deutsch weicht ab).
_TASTEN_DE = {"Ctrl": "Strg", "Shift": "Umschalt", "Space": "Leertaste", "Home": "Pos1", "End": "Ende",
              "Delete": "Entf", "Insert": "Einfg", "Backspace": "Rücktaste", "Up": "Oben", "Down": "Unten"}


def taste(t, sp):
    return _TASTEN_DE.get(t, t) if sp == "de" else t


def tasten_text(t, sp):
    return t


def tasten_teile(k):
    """'Ctrl+S' -> Ctrl, S; 'Num+' bleibt eine Taste (das Plus gehoert dazu)."""
    teile = []
    for t in k.split("+"):
        if t == "" and teile:
            teile[-1] += "+"
        else:
            teile.append(t)
    return teile


def bild_pfad(name, sp):
    for s in (sp, "en"):
        for endung in (".webp", ".png", ".jpg"):
            p = os.path.join(BILDER, s, name + endung)
            if os.path.exists(p):
                if s != sp and name not in NEUTRALE_BILDER:
                    nur_englisch.add("%s/%s" % (sp, name))
                return "../bilder/%s/%s%s" % (s, name, endung)
    return None


fehlende_bilder = set()


def bild_html(name, unter, sp, breit=False, anim=False, schmal=False):
    p = bild_pfad(name, sp)
    klasse = "bild" + (" breit" if breit else "") + (" anim" if anim else "") + (" schmal" if schmal else "")
    if p is None:
        fehlende_bilder.add(name)
        inner = '<div class="platzhalter">%s</div>' % esc(name)
    else:
        inner = '<a href="%s" class="zoom"><img src="%s" alt="%s" loading="lazy"></a>' % (p, p, esc(klartext(text(unter, sp))))
        if anim:
            inner += '<span class="abzeichen">%s</span>' % esc(ui("animation", sp))
            stand = bild_pfad(name + "_stand", sp)
            if stand:
                inner += '<button class="anhalten" type="button" data-stand="%s" data-lauf="%s" aria-label="%s">&#10074;&#10074;</button>' % (stand, p, esc(ui("pause", sp)))
    return '<figure class="%s">%s<figcaption>%s</figcaption></figure>' % (klasse, inner, inline(text(unter, sp), sp))


_LEGENDE_KOPF = {"en": ("No.", "Name", "What it does"), "de": ("Nr.", "Name", "Was es macht"),
                 "zh": ("编号", "名称", "作用"), "ja": ("番号", "名前", "機能")}


def legende_html(zeilen, sp):
    """zeilen: [(T Name, T Beschreibung), ...] - nummeriert ab 1."""
    k = _LEGENDE_KOPF[sp]
    rumpf = "".join('<tr><td class="lnr">%d</td><td>%s</td><td>%s</td></tr>' % (
        i, inline(text(n, sp), sp), inline(text(d, sp), sp)) for i, (n, d) in enumerate(zeilen, 1))
    return ('<table class="legende"><thead><tr><th>%s</th><th>%s</th><th>%s</th></tr></thead><tbody>%s</tbody></table>'
            % (k[0], k[1], k[2], rumpf))


def fluss_html(titel, schritte, unten, sp, fuss=None):
    """Eine Ablaufgrafik wie in den Unreal-Anleitungen ("The Development
    Pipeline"): Kaesten auf einem Pfeil von links nach rechts, darunter je
    Kasten optional weitere Kaesten mit einem Pfeil nach unten. Als SVG, damit
    sie scharf bleibt und dem Hell/Dunkel-Thema folgt (Farben aus CSS)."""
    bw, bh, gap, x0 = 176, 64, 30, 22
    n = len(schritte)
    breite = x0 * 2 + n * bw + (n - 1) * gap
    tiefe = max((len(u) for u in unten), default=0) if unten else 0
    hoehe = 56 + bh + (40 + tiefe * (bh + 12) if tiefe else 0) + (34 if fuss else 0)

    def textzeilen(t):
        woerter = esc(text(t, sp)).split(" ")
        zeilen, z = [], ""
        for w in woerter:
            if len(z) + len(w) + 1 > 16 and z:
                zeilen.append(z)
                z = w
            else:
                z = (z + " " + w).strip()
        zeilen.append(z)
        return zeilen[:3]

    def kasten(x, y, t, haupt=True):
        zeilen = textzeilen(t)
        ty = y + bh / 2 - (len(zeilen) - 1) * 11 + 7
        tsp = "".join('<tspan x="%d" dy="%d">%s</tspan>' % (x + bw / 2, 0 if i == 0 else 22, z) for i, z in enumerate(zeilen))
        return ('<rect x="%d" y="%d" width="%d" height="%d" rx="10" class="%s"/>'
                '<text x="%d" y="%d" text-anchor="middle" class="fluss-text">%s</text>') % (
            x, y, bw, bh, "fluss-kasten" if haupt else "fluss-unter", x + bw / 2, ty, tsp)

    y = 44
    teile = ['<text x="%d" y="24" text-anchor="middle" class="fluss-titel">%s</text>' % (breite / 2, esc(text(titel, sp)))]
    # Der durchgehende Pfeil hinter den Kaesten.
    my = y + bh / 2
    teile.append('<path d="M%d %d H%d" class="fluss-linie"/>' % (6, my, breite - 16))
    teile.append('<path d="M%d %d l-14 -9 v18 z" class="fluss-spitze"/>' % (breite - 4, my))
    for i, t in enumerate(schritte):
        x = x0 + i * (bw + gap)
        teile.append(kasten(x, y, t))
        if unten and i < len(unten):
            for k, u in enumerate(unten[i]):
                uy = y + bh + 40 + k * (bh + 12)
                if k == 0:
                    teile.append('<path d="M%d %d V%d" class="fluss-linie"/>' % (x + bw / 2, y + bh, uy - 10))
                    teile.append('<path d="M%d %d l-8 -12 h16 z" class="fluss-spitze"/>' % (x + bw / 2, uy - 2))
                teile.append(kasten(x, uy, u, False))
    if fuss:
        teile.append('<text x="%d" y="%d" text-anchor="middle" class="fluss-fuss">%s</text>' % (breite / 2, hoehe - 10, esc(text(fuss, sp))))
    return ('<figure class="fluss"><svg viewBox="0 0 %d %d" role="img" aria-label="%s">%s</svg></figure>'
            % (breite, hoehe, esc(text(titel, sp)), "".join(teile)))


MODI = ("classic", "graph", "tracks")
MODUS_UI = {"classic": "FlowClassic", "graph": "FlowGraph", "tracks": "FlowTracks"}


def modus_klassen(modus, varianten):
    """Fuer welche Ansichten gilt diese Fassung? Classic deckt die fehlenden."""
    gilt = [modus] + ([m for m in MODI if m not in varianten] if modus == "classic" else [])
    return "modusinhalt " + " ".join("m-" + m for m in gilt)


def hat_modus(seite):
    for b in seite["bloecke"]:
        if b[0] == "modus":
            return True
        if b[0] == "steps" and any(isinstance(x, dict) for x in b[1]):
            return True
    return False


def moduswahl_html(sp):
    knoepfe = "".join('<button type="button" data-modus="%s">%s</button>' % (m, esc(ui_text(MODUS_UI[m], sp))) for m in MODI)
    return '<div class="moduswahl"><span>%s</span>%s</div>' % (esc(ui("ansicht", sp)), knoepfe)


def schritt_inhalt(x, sp, abschnitt):
    if isinstance(x, tuple):
        # (Text, Bildname, Bildunterschrift[, Legende])
        t, name, unter = x[:3]
        leg = legende_html(x[3], sp) if len(x) > 3 else ""
        abschnitt["x"].append(klartext(text(t, sp)))
        if len(x) > 3:
            abschnitt["x"].extend(klartext(text(n, sp)) + " " + klartext(text(d, sp)) for n, d in x[3])
        return "<p>%s</p>%s%s" % (inline(text(t, sp), sp), bild_html(name, unter, sp), leg)
    abschnitt["x"].append(klartext(text(x, sp)))
    return "<p>%s</p>" % inline(text(x, sp), sp)


def bloecke_html(seite, sp, index_eintraege):
    aus = []
    abschnitt = {"a": "", "s": text(seite["titel"], sp), "x": []}

    def abschnitt_zu():
        index_eintraege.append({
            "p": seite["id"] + ".html",
            "a": abschnitt["a"],
            "t": text(seite["titel"], sp),
            "s": abschnitt["s"],
            "x": " ".join(abschnitt["x"]),
        })

    def baustein(b):
        nonlocal abschnitt
        art = b[0]
        if art == "modus":
            for m in MODI:
                if m not in b[1]:
                    continue
                innen = []
                aus_alt = list(aus)
                aus.clear()
                for u in b[1][m]:
                    assert u[0] != "h2", "keine h2 in einem modus-Baustein"
                    baustein(u)
                innen = list(aus)
                aus[:] = aus_alt
                aus.append('<div class="%s">%s</div>' % (modus_klassen(m, b[1]), "\n".join(innen)))
            return
        if art == "h2":
            abschnitt_zu()
            abschnitt = {"a": b[1], "s": text(b[2], sp), "x": []}
            aus.append('<h2 id="%s">%s<a class="anker" href="#%s">#</a></h2>' % (b[1], esc(text(b[2], sp)), b[1]))
        elif art == "h3":
            aus.append("<h3>%s</h3>" % esc(text(b[1], sp)))
            abschnitt["x"].append(text(b[1], sp))
        elif art == "p":
            aus.append("<p>%s</p>" % inline(text(b[1], sp), sp))
            abschnitt["x"].append(klartext(text(b[1], sp)))
        elif art == "note":
            aus.append('<div class="hinweis">%s</div>' % inline(text(b[1], sp), sp))
            abschnitt["x"].append(klartext(text(b[1], sp)))
        elif art == "warn":
            aus.append('<div class="hinweis warnung">%s</div>' % inline(text(b[1], sp), sp))
            abschnitt["x"].append(klartext(text(b[1], sp)))
        elif art == "list":
            aus.append("<ul>" + "".join("<li>%s</li>" % inline(text(x, sp), sp) for x in b[1]) + "</ul>")
            abschnitt["x"].extend(klartext(text(x, sp)) for x in b[1])
        elif art == "steps":
            punkte = []
            for i, x in enumerate(b[1], 1):
                if isinstance(x, dict):
                    # Ein Schritt je Ansicht.
                    innen = "".join('<div class="%s">%s</div>' % (modus_klassen(m, x), schritt_inhalt(x[m], sp, abschnitt))
                                    for m in MODI if m in x)
                else:
                    innen = schritt_inhalt(x, sp, abschnitt)
                punkte.append('<li><div class="schritt"><span class="nr">%d</span><div>%s</div></div></li>' % (i, innen))
            aus.append('<ol class="schritte">' + "".join(punkte) + "</ol>")
        elif art == "fluss":
            # ("fluss", Titel, [Schritte], [[darunter] je Schritt] oder None[, Fusszeile])
            aus.append(fluss_html(b[1], b[2], b[3] if len(b) > 3 else None, sp, b[4] if len(b) > 4 else None))
            abschnitt["x"].append(" ".join(klartext(text(t, sp)) for t in b[2]))
        elif art == "legende":
            aus.append(legende_html(b[1], sp))
            abschnitt["x"].extend(klartext(text(n, sp)) + " " + klartext(text(d, sp)) for n, d in b[1])
        elif art == "img":
            aus.append(bild_html(b[1], b[2], sp, breit=len(b) > 3 and b[3] == "breit", schmal=len(b) > 3 and b[3] == "schmal"))
            abschnitt["x"].append(klartext(text(b[2], sp)))
        elif art == "anim":
            aus.append(bild_html(b[1], b[2], sp, breit=True, anim=True))
            abschnitt["x"].append(klartext(text(b[2], sp)))
        elif art == "imgs":
            aus.append('<div class="bildreihe">' + "".join(bild_html(n, u, sp) for n, u in b[1]) + "</div>")
            abschnitt["x"].extend(klartext(text(u, sp)) for _, u in b[1])
        elif art == "keys":
            zeilen = "".join('<tr><td>%s</td><td>%s</td></tr>' % (
                " + ".join("<kbd>%s</kbd>" % esc(taste(t, sp)) for t in tasten_teile(k)) if isinstance(k, str) else
                "<kbd>%s</kbd>" % esc(text(k, sp)), inline(tasten_text(text(d, sp), sp), sp)) for k, d in b[1])
            aus.append('<table class="tasten"><tbody>%s</tbody></table>' % zeilen)
            abschnitt["x"].extend(klartext(text(d, sp)) + " " + (k if isinstance(k, str) else text(k, sp)) for k, d in b[1])
        elif art == "table":
            kopf = "".join("<th>%s</th>" % inline(text(h, sp), sp) for h in b[1])
            zeilen = "".join("<tr>" + "".join("<td>%s</td>" % inline(text(c, sp), sp) for c in r) + "</tr>" for r in b[2])
            aus.append('<table class="liste"><thead><tr>%s</tr></thead><tbody>%s</tbody></table>' % (kopf, zeilen))
            for r in b[2]:
                abschnitt["x"].append(" ".join(klartext(text(c, sp)) for c in r))
        elif art == "index":
            # ("index", [(Titel, [Seiten-ids]), ...]): alle Tutorials auf einen
            # Blick, je Bereich eine Spalte mit Links (shank: "du hast viele
            # Tutorials gemacht und irgendwie wird nur eines angezeigt" - die
            # Karten standen erst weit unten).
            spalten = []
            for titel, ids in b[1]:
                links = "".join('<li><a href="%s.html">%s</a></li>' % (sid, esc(text(NACH_ID[sid]["titel"], sp))) for sid in ids)
                spalten.append('<section><h4>%s <span>%d</span></h4><ul>%s</ul></section>' % (esc(text(titel, sp)), len(ids), links))
                abschnitt["x"].extend(text(NACH_ID[sid]["titel"], sp) for sid in ids)
            aus.append('<div class="tutindex">%s</div>' % "".join(spalten))
        elif art == "cards":
            karten = []
            for sid in b[1]:
                s = NACH_ID[sid]
                p = bild_pfad(s.get("titelbild", ""), sp) if s.get("titelbild") else None
                if p is None and s.get("titelbild"):
                    fehlende_bilder.add(s["titelbild"])
                bild = '<img src="%s" alt="" loading="lazy">' % p if p else '<div class="platzhalter"></div>'
                meta = '<div class="meta">%s</div>' % esc(text(s["meta"], sp)) if s.get("meta") else ""
                karten.append('<a class="karte" href="%s.html">%s<div class="kartentext"><h3>%s</h3><p>%s</p>%s</div></a>' % (
                    sid, bild, esc(text(s["titel"], sp)), esc(text(s["kurz"], sp)), meta))
            aus.append('<div class="karten%s">' % (" breit" if len(b) > 2 and b[2] == "breit" else "") + "".join(karten) + "</div>")
        else:
            raise SystemExit("unbekannter Baustein: " + art)

    if hat_modus(seite):
        aus.append(moduswahl_html(sp))
    for b in seite["bloecke"]:
        baustein(b)
    abschnitt_zu()
    return "\n".join(aus)


def seitenleiste(aktiv, sp):
    """Die Seitenleiste mit Ausklapp-Pfeilen wie in der Unreal-Dokumentation
    (shank: "Pfeile, die man ausklappen kann, das ist ganz praktisch"). Die
    Tutorial-Bereiche (Gruppen tut_*) stehen als Untergruppen unter
    "Tutorials", die acht Teile des grossen Projekts unter dessen Seite."""

    def eintrag(s, kinder=None):
        ist = s["id"] == aktiv["id"]
        unter = ""
        if ist:
            unter = '<ul class="unter">' + "".join(
                '<li><a href="#%s">%s</a></li>' % (b[1], esc(text(b[2], sp))) for b in ueberschriften(s)) + "</ul>"
        link = '<a href="%s.html">%s</a>' % (s["id"], esc(text(s["titel"], sp)))
        if kinder:
            offen = ist or any(k["id"] == aktiv["id"] for k in kinder)
            innen = "".join(eintrag(k) for k in kinder)
            return ('<li class="%s"><details class="untergruppe" %s><summary><span class="pfeil"></span>%s</summary>'
                    '%s<ul>%s</ul></details></li>') % ("aktiv" if ist else "", "open" if offen else "", link, unter, innen)
        return '<li class="%s">%s%s</li>' % ("aktiv" if ist else "", link, unter)

    def untergruppe(titel, seiten):
        offen = any(s["id"] == aktiv["id"] for s in seiten)
        return ('<li><details class="untergruppe" %s><summary><span class="pfeil"></span><span class="ugtitel">%s</span>'
                '</summary><ul>%s</ul></details></li>') % ("open" if offen else "", esc(text(titel, sp)),
                                                          "".join(eintrag(s) for s in seiten))

    unter_gruppen = [(g, t) for g, t in inhalt.GRUPPEN if g.startswith("tut_")]
    teile = []
    for gruppe_id, gruppe_titel in inhalt.GRUPPEN:
        if gruppe_id.startswith("tut_"):
            continue
        seiten = [s for s in SEITEN if s["gruppe"] == gruppe_id]
        alle = list(seiten)
        punkte = []
        if gruppe_id == "tut":
            projekt_teile = [s for s in seiten if s["id"].startswith("tut-p") and s["id"][5:].isdigit()]
            for s in seiten:
                if s in projekt_teile:
                    continue
                punkte.append(eintrag(s, projekt_teile if s["id"] == "tut-projekt" else None))
            for g, t in unter_gruppen:
                us = [s for s in SEITEN if s["gruppe"] == g]
                alle += us
                punkte.append(untergruppe(t, us))
        else:
            punkte = [eintrag(s) for s in seiten]
        offen = any(s["id"] == aktiv["id"] for s in alle)
        teile.append('<details class="gruppe" %s><summary><span class="pfeil"></span>%s</summary><ul>%s</ul></details>' % (
            "open" if offen else "", esc(text(gruppe_titel, sp)), "".join(punkte)))
    return "\n".join(teile)


def sprachwahl(seite, sp):
    namen = {"en": "English", "de": "Deutsch", "zh": "中文", "ja": "日本語"}
    return "".join('<a href="../%s/%s.html" class="%s" lang="%s">%s</a>' % (
        s, seite["id"], "aktiv" if s == sp else "", s, namen[s]) for s in SPRACHEN)


def nachbarn(seite, sp):
    i = SEITEN.index(seite)
    vor = SEITEN[i - 1] if i > 0 else None
    nach = SEITEN[i + 1] if i + 1 < len(SEITEN) else None
    teile = []
    if vor:
        teile.append('<a class="vor" href="%s.html"><span>%s</span>%s</a>' % (vor["id"], esc(ui("zurueck", sp)), esc(text(vor["titel"], sp))))
    else:
        teile.append("<span></span>")
    if nach:
        teile.append('<a class="nach" href="%s.html"><span>%s</span>%s</a>' % (nach["id"], esc(ui("weiter", sp)), esc(text(nach["titel"], sp))))
    return '<nav class="blaettern">%s</nav>' % "".join(teile)


# --- Tutorials im Programm (rc601, Paket tour) -------------------------------
#
# handbuch/<sprache>/touren.json: die Schritte der Tutorials aus
# inhalt_touren.TOUREN, mit den Angaben aus inhalt_touren.py (worauf der Schritt
# zeigt, wann er erledigt ist). Gelesen von include/bhed/tour.h (ladeTouren).


def tour_text(t, sp):
    """Der Text der Sprache mit {{Kennung}} ROH - das Programm beschriftet sie
    zur Laufzeit selbst, in der eingestellten Sprache. Jede Kennung wird wie im
    Handbuch geprueft; [[seite|Text]] wird zu Text, [[seite]] zum Seitentitel."""
    if isinstance(t, T):
        t = t.get(sp)
    for k in re.findall(r"\{\{(\w+)\}\}", t):
        ui_text(k, sp)   # bricht bei einer unbekannten Kennung ab
    t = re.sub(r"\{\{icon:\w+\}\}", "", t)

    def verweis(m):
        sid, anker, beschr = m.group(1), m.group(2), m.group(3)
        if sid not in NACH_ID:
            raise SystemExit("Link auf unbekannte Seite: " + sid)
        titel = anker_titel(sid, anker, sp)
        return beschr if beschr else titel
    return re.sub(r"\[\[([^\]|#]+)(?:#([^\]|]+))?(?:\|([^\]]+))?\]\]", verweis, t)


def tour_schritte(seite, ansicht):
    """[(Text, Bildname, Bildunterschrift)] der Schritte einer Ansicht."""
    roh = None
    for b in seite["bloecke"]:
        if b[0] == "modus":
            for u in b[1].get(ansicht) or b[1].get("classic") or []:
                if u[0] == "steps":
                    roh = u[1]
                    break
        elif b[0] == "steps":
            roh = b[1]
        if roh is not None:
            break
    aus = []
    for x in roh or []:
        if isinstance(x, dict):
            x = x.get(ansicht) or x.get("classic")
        if isinstance(x, tuple):
            aus.append((x[0], x[1], x[2]))
        else:
            aus.append((x, "", None))
    return aus


def touren_json(sp):
    liste = []
    for sid in inhalt_touren.TOUREN:
        s = NACH_ID.get(sid)
        if s is None:
            raise SystemExit("Tour ohne Handbuchseite: " + sid)
        ansichten = {}
        for ansicht in inhalt_touren.ANSICHTEN:
            schritte = []
            for nr, (t, bild, unter) in enumerate(tour_schritte(s, ansicht), 1):
                a = inhalt_touren.angaben(sid, ansicht, nr, bild)
                if a is None:
                    raise SystemExit("%s/%s Schritt %d (%s): keine Angaben in inhalt_touren.py"
                                     % (sid, ansicht, nr, bild or "ohne Bild"))
                inhalt_touren.pruefe(sid, ansicht, nr, a, lambda k: k in UI_TEXTE)
                eintrag = {"text": tour_text(t, sp), "bild": bild or "", "unter": tour_text(unter, sp) if unter else ""}
                eintrag.update(a)
                schritte.append(eintrag)
            if not schritte:
                raise SystemExit("%s/%s: keine Schritte" % (sid, ansicht))
            ansichten[ansicht] = schritte
        liste.append({"id": sid, "gruppe": s["gruppe"], "titel": tour_text(s["titel"], sp),
                      "kurz": tour_text(s["kurz"], sp) if s.get("kurz") else "", "seite": sid + ".html",
                      "ansichten": ansichten})
    return json.dumps({"fassung": 1, "sprache": sp, "touren": liste}, ensure_ascii=False, indent=1) + "\n"


VORLAGE = """<!doctype html>
<html lang="{lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{titel} - BehavEd-Remake</title>
<link rel="stylesheet" href="../assets/stil.css">
<script>try{{var t=localStorage.getItem('bhed-thema');if(t)document.documentElement.dataset.theme=t;var m=new URLSearchParams(location.search).get('modus')||localStorage.getItem('bhed-modus');if(m)document.documentElement.dataset.modus=m;}}catch(e){{}}</script>
</head>
<body>
<aside class="leiste">
  <a class="marke" href="index.html"><img src="../assets/logo.svg" alt=""><span><b>BehavEd-Remake</b><small>{guide}</small></span></a>
  <div class="suche">
    <input id="suchfeld" type="search" placeholder="{suchen}" autocomplete="off" aria-label="{suchen}">
    <div id="treffer" class="treffer" hidden></div>
  </div>
  <nav class="kapitel">{leiste}</nav>
</aside>
<main>
  <header class="kopf">
    <div class="sprachen">{sprachen}</div>
    <button id="thema" class="thema" title="{thema}" aria-label="{thema}">&#9681;</button>
  </header>
  <article>
    <div class="ueber">{gruppe}</div>
    <h1>{titel}</h1>
    {kurz}
    {inhalt}
    {blaettern}
  </article>
  <footer>{fuss}</footer>
</main>
<div id="gross" class="gross" hidden><img alt=""></div>
<script src="suche.js"></script>
<script src="../assets/handbuch.js"></script>
</body>
</html>
"""


def schreibe(pfad, inhalt_text):
    os.makedirs(os.path.dirname(pfad), exist_ok=True)
    with open(pfad, "w", encoding="utf-8", newline="\n") as f:
        f.write(inhalt_text)


def main():
    os.makedirs(ZIEL, exist_ok=True)
    assets = os.path.join(ZIEL, "assets")
    os.makedirs(assets, exist_ok=True)
    for name in ("stil.css", "handbuch.js", "logo.svg"):
        shutil.copyfile(os.path.join(HIER, "assets", name), os.path.join(assets, name))
    gruppen = dict(inhalt.GRUPPEN)
    for sp in SPRACHEN:
        index = []
        for s in SEITEN:
            inhalt_html = bloecke_html(s, sp, index)
            kurz = '<p class="einleitung">%s</p>' % inline(text(s["kurz"], sp), sp) if s.get("kurz") else ""
            seite = VORLAGE.format(
                lang=sp, titel=esc(text(s["titel"], sp)), guide=esc(ui("guide", sp)), suchen=esc(ui("suchen", sp)),
                thema=esc(ui("thema", sp)), leiste=seitenleiste(s, sp), sprachen=sprachwahl(s, sp),
                gruppe=esc(text(gruppen[s["gruppe"]], sp)), kurz=kurz, inhalt=inhalt_html,
                blaettern=nachbarn(s, sp), fuss=esc(ui("fuss", sp)))
            schreibe(os.path.join(ZIEL, sp, s["id"] + ".html"), seite)
        texte = {k: ui(k, sp) for k in ("keine_treffer", "treffer_auf")}
        schreibe(os.path.join(ZIEL, sp, "suche.js"),
                 "window.HANDBUCH_INDEX=" + json.dumps(index, ensure_ascii=False, separators=(",", ":")) +
                 ";\nwindow.HANDBUCH_TEXTE=" + json.dumps(texte, ensure_ascii=False) + ";\n")
        # rc601 tour: die Tutorials im Programm (gelesen von bhed/tour.h).
        schreibe(os.path.join(ZIEL, sp, "touren.json"), touren_json(sp))
    # Einstieg: in die Sprache des Browsers.
    schreibe(os.path.join(ZIEL, "index.html"), """<!doctype html><html><head><meta charset="utf-8"><title>BehavEd-Remake</title>
<script>var l=(navigator.language||'en').slice(0,2);if(['en','de','zh','ja'].indexOf(l)<0)l='en';location.replace(l+'/index.html');</script>
</head><body><a href="en/index.html">English</a> <a href="de/index.html">Deutsch</a> <a href="zh/index.html">中文</a> <a href="ja/index.html">日本語</a></body></html>
""")
    print("Handbuch: %d Seiten x %d Sprachen -> %s" % (len(SEITEN), len(SPRACHEN), ZIEL))
    ohne = []
    for s in SEITEN:
        if not s["gruppe"].startswith("tut"):
            continue
        bloecke = list(s["bloecke"])
        for b in s["bloecke"]:
            if b[0] == "modus":
                for m, bs in b[1].items():
                    bloecke += [("steps", u[1], m) for u in bs if u[0] == "steps"]
        for b in bloecke:
            if b[0] == "steps":
                for i, x in enumerate(b[1], 1):
                    for m, y in (x.items() if isinstance(x, dict) else [("", x)]):
                        if not isinstance(y, tuple):
                            ohne.append("%s#%d%s" % (s["id"], i, ("/" + (m or b[2])) if (m or len(b) > 2) else ""))
    if ohne:
        print("Tutorial-Schritte ohne Bild (%d): %s" % (len(ohne), " ".join(ohne[:40]) + (" ..." if len(ohne) > 40 else "")))
    if T.fehlend:
        print("noch nicht uebersetzt (gilt englisch): " + ", ".join("%s %d" % kv for kv in sorted(T.fehlend.items())))
    if fehlende_bilder:
        print("fehlende Bilder (%d): %s" % (len(fehlende_bilder), " ".join(sorted(fehlende_bilder))))
    if nur_englisch:
        print("Bilder nur englisch (%d): %s" % (len(nur_englisch), " ".join(sorted(nur_englisch)[:40])))
    import inhalt_basis
    unbenutzt = sum(1 for sp, t in inhalt_basis.tabelle().items() for en in t if (sp, en) not in inhalt_basis.benutzt)
    if unbenutzt:
        print("Hinweis: %d Eintraege in uebersetzung/ werden nicht mehr gebraucht" % unbenutzt)
    # rc601: jede Seite in allen vier Sprachen - fehlt ein Text oder ein Bild
    # einer Sprache, endet der Bau mit Fehler.
    if T.fehlend:
        for en in list(T.fehlende_texte)[:15]:
            print("  ohne %s: %s" % ("/".join(sorted(T.fehlende_texte[en])), en[:110].encode("ascii", "replace").decode()))
    bilder_da = os.path.isdir(os.path.join(BILDER, "en"))
    fehler = bool(T.fehlend) or (bilder_da and bool(fehlende_bilder or nur_englisch))
    if PRUEFEN:
        shutil.rmtree(os.path.dirname(ZIEL), ignore_errors=True)
        print("handbuch_sprachen: " + ("FEHLER" if fehler else "ok") + ("" if bilder_da else " (ohne Bilder geprueft)"))
    if fehler:
        sys.exit(1)


if __name__ == "__main__":
    main()
