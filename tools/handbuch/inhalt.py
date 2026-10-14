"""Alle Seiten des Handbuchs in Lesereihenfolge.

Jede Seite: id (Dateiname), gruppe, titel, kurz (Einleitung), optional
titelbild + meta (fuer die Karten der Tutorials), bloecke.
Bausteine siehe gen_handbuch.py (bloecke_html).
"""
from inhalt_basis import T
import inhalt_start
import inhalt_funktionen
import inhalt_skript
import inhalt_karte
import inhalt_mission
import inhalt_tutorials
import inhalt_kurz
import inhalt_kurz2
import inhalt_kurz3
import inhalt_referenz
import inhalt_befehle
import inhalt_knoepfe

GRUPPEN = [
    ("start", T("Start here", "Einstieg", "入门", "はじめに")),
    ("skript", T("Scripts", "Skripte", "脚本", "スクリプト")),
    ("karte", T("Map and scene", "Karte und Szene", "地图与场景", "マップとシーン")),
    ("mission", T("Missions", "Missionen", "任务", "ミッション")),
    ("tut", T("Tutorials", "Tutorials", "教程", "チュートリアル")),
] + inhalt_kurz.GRUPPEN + [
    ("ref", T("Reference", "Nachschlagen", "参考", "リファレンス")),
]

SEITEN = (inhalt_start.SEITEN[:1] + inhalt_funktionen.SEITEN + inhalt_start.SEITEN[1:] + [inhalt_skript.FENSTER] + inhalt_skript.SEITEN + inhalt_karte.SEITEN +
          inhalt_mission.SEITEN + inhalt_tutorials.SEITEN + inhalt_kurz.SEITEN + inhalt_kurz2.SEITEN + inhalt_kurz3.SEITEN + inhalt_referenz.SEITEN + inhalt_knoepfe.SEITEN + inhalt_befehle.SEITEN)

_ids = [s["id"] for s in SEITEN]
assert len(_ids) == len(set(_ids)), "doppelte Seiten-id"
_gruppen = {g for g, _ in GRUPPEN}
for _s in SEITEN:
    assert _s["gruppe"] in _gruppen, _s["id"]
