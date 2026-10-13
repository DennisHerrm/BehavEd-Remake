# Handbuch pflegen

Das Handbuch (Ordner `handbuch/`, wird neben `behaved.exe` ausgeliefert und über
**Help → Guide and tutorials** bzw. den Knopf **Guide** im About-Fenster
geöffnet) entsteht vollständig aus diesem Ordner. Nichts unter `handbuch/` von
Hand ändern – es wird überschrieben.

**Regel:** Jede Änderung an einer Funktion zieht die betroffene Seite mit –
Text in allen vier Sprachen und, wenn sich das Bild ändert, die Bilder.

## Aufbau

| Datei | Inhalt |
|---|---|
| `inhalt_basis.py` | `T(en, de, zh, ja)` – jeder Text in allen vier Sprachen (fehlt einer, bricht der Bau ab); feste Beschriftungen des Handbuchs |
| `inhalt.py` | Gruppen (Seitenleiste) und Reihenfolge der Seiten |
| `inhalt_start.py`, `inhalt_skript.py`, `inhalt_karte.py`, `inhalt_szene.py`, `inhalt_mission.py`, `inhalt_tutorials.py`, `inhalt_referenz.py` | die Seiten |
| `gen_handbuch.py` | baut `handbuch/<sprache>/*.html`, den Suchindex `suche.js` je Sprache, kopiert `assets/` |
| `bilder.py` | Fotos des Selbsttests → `handbuch/bilder/<sprache>/*.webp` (Ausschnitt, Größe, Bildfolgen als bewegtes WebP) |
| `assets/` | Stil, Suche/Hell-Dunkel/Bildvergrößerung (`handbuch.js`), Logo |

### Bausteine einer Seite

`("p", T)`, `("h2", "anker", T)` (erscheint in der Seitenleiste und als
Suchtreffer), `("h3", T)`, `("list", [T…])`, `("steps", [T | (T, bild, T)…])`,
`("img", name, T[, "breit"])`, `("imgs", [(name, T)…])`, `("anim", name, T)`,
`("note", T)`, `("warn", T)`, `("keys", [(taste, T)…])`,
`("table", [kopf…], [[zelle…]…])`, `("cards", [seiten-id…])`.

Im Text: `**fett**`, `` `code` ``, `[[seite]]`, `[[seite#anker]]`,
`[[seite#anker|Text]]`, `<kbd>Strg</kbd>` und **`{{Kennung}}`** – eine
Beschriftung des Programms aus `tools/gen_i18n.py`. Sie erscheint in jeder
Sprache genau so wie im Programm; benennt man einen Knopf um, zieht das
Handbuch beim nächsten Bau mit. Eine unbekannte Kennung bricht den Bau ab.

## Bauen

```
python tools/handbuch/gen_handbuch.py            # -> handbuch/
python tools/handbuch/gen_handbuch.py <ordner>   # -> <ordner>/handbuch/
```

Am Ende nennt der Bau fehlende Bilder.

## Bilder neu aufnehmen

Der Selbsttest `BHED_EDITORTEST=handbuch` (gui/selbsttest.cpp,
`handbuchSchritte`) lädt `md_am_sith` aus `MD_Missions_Ep3.pk3`, stellt jede
Szene ein und fotografiert **nur das eigene Fenster**; neben jedes Foto
schreibt er den Ausschnitt (`<name>.rect`). Umgebung:

- `BHED_FOTOS` – Zielordner, `BHED_SPRACHE` – en / de / zh-Hans / ja (Kennungen wie in der Einstellungsdatei)
- `BHED_MISSION_ARCHIV`, `BHED_MISSION_NAME`
- `BHED_MISSIONEN`, `BHED_HB_ZIEL` – die Übungsmission und ihr Archiv. Beim
  Aufnehmen auf ein Laufwerk aus `subst` legen (z. B. `X:\missionen`), damit
  kein Benutzerpfad im Bild steht; danach `subst X: /d`.

Je Sprache ein Lauf, dann

```
python tools/handbuch/bilder.py <fotoordner> <sprache>
```

Ein neues Bild: im Selbsttest einen Schritt `hb::fotoTeil("name", ausschnitt)`
an passender Stelle, im Inhalt `("img", "name", T(...))`. Bildfolgen heißen
`a_<name>_00 … _NN` und werden zu einem bewegten Bild `a_<name>.webp`.

## Prüfen

- Suche: im Browser `…/en/index.html`, `/` drücken, einen Begriff tippen –
  der Treffer springt an den Abschnitt und markiert den Begriff.
- Alle vier Sprachen ansehen (Sprachwahl oben rechts).
- Kein Pfad mit Benutzernamen in den Bildern.
