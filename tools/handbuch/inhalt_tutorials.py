"""Tutorials: die Uebersicht - das Yavin-Projekt und die kurzen Tutorials.

Bis rc599 standen hier vier Tutorials auf einer Movie-Duels-Mission. shank:
"es geht nicht nur drum, Movie-Duels-Missionen zu machen, das ist ein Tool
fuer die gesamte JKA-Community". Jetzt: eine eigene Mission auf yavin1
(inhalt_projekt.py) und viele kurze Tutorials (inhalt_kurz.py), alle Bilder
von derselben Karte.
"""
from inhalt_basis import T
import inhalt_kurz
import inhalt_projekt
import inhalt_kurz2
import inhalt_kurz3

UEBERSICHT = {
    "id": "tutorials",
    "gruppe": "tut",
    "titel": T("Tutorials", "Tutorials", "教程", "チュートリアル"),
    "kurz": T("Start with the Taspir mission - it builds a whole cutscene for Jedi Academy from nothing and touches almost every part of the program. The short tutorials below each show one thing in a few steps."),
    "bloecke": [
        ("h2", "projekt", T("The big one: your own mission")),
        ("cards", ["tut-projekt"]),
        ("p", T("All tutorials use the base game - the map `taspir1` and its characters - so the pictures match what you see. Any other map, and any mod such as Movie Duels, works the same way. If the parts of the window are new to you, read [[fenster]] and [[karte#bewegen]] first.")),
        ("h2", "kurz", T("Short tutorials")),
        ("p", T("Each one shows one small thing in three to six steps - start with any of them.")),
        ("h3", T("Scripts")),
        ("cards", [s["id"] for s in inhalt_kurz.SKRIPT + inhalt_kurz2.SKRIPT2 + inhalt_kurz3.VORHANDEN]),
        ("h3", T("Camera and timeline")),
        ("cards", [s["id"] for s in inhalt_kurz.KAMERA + inhalt_kurz2.KAMERA2 + inhalt_kurz3.ZEIT + [inhalt_kurz3.SZENE[1]]]),
        ("h3", T("Figures")),
        ("cards", [s["id"] for s in inhalt_kurz.FIGUREN + inhalt_kurz2.FIGUREN2]),
        ("h3", T("Map and entities")),
        ("cards", [s["id"] for s in inhalt_kurz.KARTE + inhalt_kurz2.KARTE2 + [inhalt_kurz3.SZENE[0], inhalt_kurz3.SZENE[2], inhalt_kurz3.SZENE[3]]]),
        ("h3", T("Animation")),
        ("cards", [s["id"] for s in inhalt_kurz.ANIMATION]),
        ("h3", T("Missions and models")),
        ("cards", [s["id"] for s in inhalt_kurz.MISSION + inhalt_kurz.MODELL + inhalt_kurz2.MISSION2 + inhalt_kurz3.LOGIK + inhalt_kurz3.MODELLE + inhalt_kurz2.OBERFLAECHE]),
    ],
}

SEITEN = [UEBERSICHT] + inhalt_projekt.SEITEN
