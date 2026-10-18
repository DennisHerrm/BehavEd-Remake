"""Grundbausteine des Handbuchs: Sprachen, Texte, feste Beschriftungen."""

SPRACHEN = ("en", "de", "zh", "ja")


import glob
import json
import os
import re

_HIER = os.path.dirname(os.path.abspath(__file__))
# rc601 (shank: "mach es fuer alle 4 sprachen"): Texte, die aus Daten oder in
# Schleifen entstehen (Befehlsseiten, Knopftabellen, Tutorial-Schritte, die
# Beschreibungen aus Q3_Interface.h/behaved.bhc ...), stehen nicht als
# T(en, de, zh, ja) im Quelltext. Ihre Uebersetzungen liegen in
# uebersetzung/<sprache>/*.json - {englischer Text: Uebersetzung}. Aendert sich
# der englische Text, fehlt die Uebersetzung, und der Bau meldet es.
_TABELLE = None
benutzt = set()


def tabelle():
    global _TABELLE
    if _TABELLE is None:
        _TABELLE = {}
        for sp in SPRACHEN[1:]:
            t = {}
            for datei in sorted(glob.glob(os.path.join(_HIER, "uebersetzung", sp, "*.json"))):
                with open(datei, encoding="utf-8") as f:
                    for en, wert in json.load(f).items():
                        if en in t and t[en] != wert:
                            raise ValueError("%s: zwei Uebersetzungen fuer %r" % (datei, en))
                        if not isinstance(wert, str) or not wert.strip():
                            raise ValueError("%s: leere Uebersetzung fuer %r" % (datei, en))
                        t[en] = wert
            _TABELLE[sp] = t
    return _TABELLE


def neutral(en):
    """Ohne Woerter - nur `Code`, {{Kennung}}, [[Seite]], Zahlen, Zeichen:
    in jeder Sprache gleich."""
    rest = re.sub(r"`[^`]*`|\{\{[^}]*\}\}|\[\[[^\]]*\]\]", "", en)
    return not re.search(r"[A-Za-z]", rest)


class T:
    """Ein Text in allen vier Sprachen. Fehlt eine Uebersetzung im Aufruf,
    gilt die aus uebersetzung/<sprache>/*.json; fehlt sie auch dort, vorerst
    der englische Text - der Bau zaehlt sie (T.fehlend, T.fehlende_texte),
    gen_handbuch.py bricht dann ab."""

    fehlend = {}
    fehlende_texte = {}

    def __init__(self, en, de=None, zh=None, ja=None):
        if not isinstance(en, str) or not en.strip():
            raise ValueError("Text ohne Englisch: %r" % (en,))
        self.t = {"en": en}
        for name, wert in (("de", de), ("zh", zh), ("ja", ja)):
            if wert is None:
                if neutral(en):
                    wert = en
                elif en in tabelle()[name]:
                    wert = tabelle()[name][en]
                    benutzt.add((name, en))
                else:
                    T.fehlend[name] = T.fehlend.get(name, 0) + 1
                    T.fehlende_texte.setdefault(en, set()).add(name)
                    wert = en
            elif not isinstance(wert, str) or not wert.strip():
                raise ValueError("Text ohne %s: %r" % (name, en))
            self.t[name] = wert

    def get(self, sp):
        return self.t[sp]


def Tf(vorlage, *werte):
    """T aus einer Vorlage mit %s/%d - uebersetzt wird die Vorlage."""
    t = T(vorlage)
    for sp in SPRACHEN:
        t.t[sp] = t.t[sp] % werte
    return t


_UI = {
    "guide": T("Guide", "Handbuch", "手册", "ガイド"),
    "suchen": T("Search the guide...  (/)", "Im Handbuch suchen ...  (/)", "搜索手册...  (/)", "ガイドを検索...  (/)"),
    "thema": T("Light or dark", "Hell oder dunkel", "浅色或深色", "ライト / ダーク"),
    "zurueck": T("Previous", "Zurück", "上一页", "前へ"),
    "weiter": T("Next", "Weiter", "下一页", "次へ"),
    "animation": T("Animation", "Animation", "动画", "アニメーション"),
    "pause": T("Pause", "Anhalten", "暂停", "一時停止"),
    "ansicht": T("View:", "Ansicht:", "视图:", "ビュー:"),
    "keine_treffer": T("Nothing found.", "Nichts gefunden.", "未找到结果。", "見つかりませんでした。"),
    "treffer_auf": T("on", "auf", "位于", "ページ:"),
    "fuss": T("BehavEd-Remake guide. The pictures show the program itself; they are made by its self-test.",
              "Handbuch zu BehavEd-Remake. Die Bilder zeigen das Programm selbst; sein Selbsttest nimmt sie auf.",
              "BehavEd-Remake 手册。图片展示的是程序本身,由其自检功能截取。",
              "BehavEd-Remake ガイド。画像はプログラム自身の画面で、セルフテストが撮影しています。"),
}


def ui(schluessel, sp):
    return _UI[schluessel].get(sp)
