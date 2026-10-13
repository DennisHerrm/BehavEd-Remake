"""Grundbausteine des Handbuchs: Sprachen, Texte, feste Beschriftungen."""

SPRACHEN = ("en", "de", "zh", "ja")


class T:
    """Ein Text in allen vier Sprachen. Fehlt keiner - sonst bricht der Bau ab."""

    def __init__(self, en, de, zh, ja):
        for name, wert in (("en", en), ("de", de), ("zh", zh), ("ja", ja)):
            if not isinstance(wert, str) or not wert.strip():
                raise ValueError("Text ohne %s: %r" % (name, en))
        self.t = {"en": en, "de": de, "zh": zh, "ja": ja}

    def get(self, sp):
        return self.t[sp]


_UI = {
    "guide": T("Guide", "Handbuch", "手册", "ガイド"),
    "suchen": T("Search the guide...  (/)", "Im Handbuch suchen ...  (/)", "搜索手册...  (/)", "ガイドを検索...  (/)"),
    "thema": T("Light or dark", "Hell oder dunkel", "浅色或深色", "ライト / ダーク"),
    "zurueck": T("Previous", "Zurück", "上一页", "前へ"),
    "weiter": T("Next", "Weiter", "下一页", "次へ"),
    "animation": T("Animation", "Animation", "动画", "アニメーション"),
    "keine_treffer": T("Nothing found.", "Nichts gefunden.", "未找到结果。", "見つかりませんでした。"),
    "treffer_auf": T("on", "auf", "位于", "ページ:"),
    "fuss": T("BehavEd-Remake guide. The pictures show the program itself; they are made by its self-test.",
              "Handbuch zu BehavEd-Remake. Die Bilder zeigen das Programm selbst; sein Selbsttest nimmt sie auf.",
              "BehavEd-Remake 手册。图片展示的是程序本身,由其自检功能截取。",
              "BehavEd-Remake ガイド。画像はプログラム自身の画面で、セルフテストが撮影しています。"),
}


def ui(schluessel, sp):
    return _UI[schluessel].get(sp)
