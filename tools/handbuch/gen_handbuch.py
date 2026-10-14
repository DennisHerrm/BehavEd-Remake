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
  handbuch/assets/                  Stil, Suchskript, Logo
  handbuch/index.html               leitet in die Sprache des Browsers

Bilder liegen in handbuch/bilder/<sprache>/<name>.webp; fehlt eines in einer
Sprache, gilt das englische. Sie entstehen mit dem Selbsttest "handbuch"
(nur das eigene Fenster) und tools/handbuch/bilder.py.

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

ZIEL = os.path.join(sys.argv[1] if len(sys.argv) > 1 else WURZEL, "handbuch")


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
            p = os.path.join(ZIEL, "bilder", "en", "i_" + m.group(8) + ".png")
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
            p = os.path.join(ZIEL, "bilder", s, name + endung)
            if os.path.exists(p):
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
    return '<figure class="%s">%s<figcaption>%s</figcaption></figure>' % (klasse, inner, inline(text(unter, sp), sp))


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

    for b in seite["bloecke"]:
        art = b[0]
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
                if isinstance(x, tuple):
                    # (Text, Bildname, Bildunterschrift)
                    t, name, unter = x
                    punkte.append('<li><div class="schritt"><span class="nr">%d</span><div>%s%s</div></div></li>' % (
                        i, "<p>%s</p>" % inline(text(t, sp), sp), bild_html(name, unter, sp)))
                    abschnitt["x"].append(klartext(text(t, sp)))
                else:
                    punkte.append('<li><div class="schritt"><span class="nr">%d</span><div><p>%s</p></div></div></li>' % (
                        i, inline(text(x, sp), sp)))
                    abschnitt["x"].append(klartext(text(x, sp)))
            aus.append('<ol class="schritte">' + "".join(punkte) + "</ol>")
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
            aus.append('<div class="karten">' + "".join(karten) + "</div>")
        else:
            raise SystemExit("unbekannter Baustein: " + art)
    abschnitt_zu()
    return "\n".join(aus)


def seitenleiste(aktiv, sp):
    teile = []
    for gi, (gruppe_id, gruppe_titel) in enumerate(inhalt.GRUPPEN):
        seiten = [s for s in SEITEN if s["gruppe"] == gruppe_id]
        offen = any(s["id"] == aktiv["id"] for s in seiten)
        punkte = []
        for s in seiten:
            ist = s["id"] == aktiv["id"]
            unter = ""
            if ist:
                unter = '<ul class="unter">' + "".join(
                    '<li><a href="#%s">%s</a></li>' % (b[1], esc(text(b[2], sp))) for b in ueberschriften(s)) + "</ul>"
            punkte.append('<li class="%s"><a href="%s.html">%s</a>%s</li>' % (
                "aktiv" if ist else "", s["id"], esc(text(s["titel"], sp)), unter))
        teile.append('<details class="gruppe" %s><summary><span class="gnr">%d</span>%s</summary><ul>%s</ul></details>' % (
            "open" if offen else "", gi, esc(text(gruppe_titel, sp)), "".join(punkte)))
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


VORLAGE = """<!doctype html>
<html lang="{lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{titel} - BehavEd-Remake</title>
<link rel="stylesheet" href="../assets/stil.css">
<script>try{{var t=localStorage.getItem('bhed-thema');if(t)document.documentElement.dataset.theme=t;}}catch(e){{}}</script>
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
    # Einstieg: in die Sprache des Browsers.
    schreibe(os.path.join(ZIEL, "index.html"), """<!doctype html><html><head><meta charset="utf-8"><title>BehavEd-Remake</title>
<script>var l=(navigator.language||'en').slice(0,2);if(['en','de','zh','ja'].indexOf(l)<0)l='en';location.replace(l+'/index.html');</script>
</head><body><a href="en/index.html">English</a> <a href="de/index.html">Deutsch</a> <a href="zh/index.html">中文</a> <a href="ja/index.html">日本語</a></body></html>
""")
    print("Handbuch: %d Seiten x %d Sprachen -> %s" % (len(SEITEN), len(SPRACHEN), ZIEL))
    if T.fehlend:
        print("noch nicht uebersetzt (gilt englisch): " + ", ".join("%s %d" % kv for kv in sorted(T.fehlend.items())))
    if fehlende_bilder:
        print("fehlende Bilder (%d): %s" % (len(fehlende_bilder), " ".join(sorted(fehlende_bilder))))


if __name__ == "__main__":
    main()
