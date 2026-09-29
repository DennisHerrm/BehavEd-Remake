#!/usr/bin/env python3
"""Wird jede Direct3D-Ressource auch wieder freigegeben?

Warum das ein eigener Pruefer ist:

`gui/gpumap_win32.cpp` gibt auf rund 1900 Zeilen 91 mal von Hand frei. Der
uebliche Rat waere `Microsoft::WRL::ComPtr` - ein Zeiger, der das selbst
tut. Nachgemessen wurde vorher aber, ob es ueberhaupt Lecks GIBT: jedes Feld
wird in `shutdown` freigegeben, und `bereiteZiel` gibt vor dem Neuanlegen
frei. Es gab keines.

91 blinde Aenderungen an Quelltext, den hier niemand ausfuehren kann, ohne
messbaren Gewinn - das ist die falsche Rechnung. Was fehlte, war nicht der
Umbau, sondern die Zusicherung, dass die naechste hinzugefuegte Ressource
nicht doch durchrutscht.

Geprueft wird zweierlei:

  1. Jedes globale Direct3D-Feld kommt in `shutdown()` mit `->Release()` vor.
  2. Wer eine Ressource in ein Feld legt, das schon eine haelt, gibt die
     alte vorher frei - oder prueft, dass keine da ist. Das ist die Stelle,
     an der ein Leck wirklich entsteht: beim Neuanlegen nach einer
     Groessenaenderung, nicht beim Beenden.
"""
import re
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent
QUELLE = HIER.parent / "gui" / "gpumap_win32.cpp"

FELD = re.compile(r"^\s*(?:ID3D11\w+|IDXGI\w+)\*\s+(g_\w+)\s*=", re.M)
TABELLE = re.compile(
    r"^\s*std::(?:map|vector)<[^>]*?(?:ID3D11\w+|IDXGI\w+)\*>\s+(g_\w+);", re.M)


def ohneKommentare(t: str) -> str:
    t = re.sub(r"//[^\n]*", "", t)
    return re.sub(r"/\*.*?\*/", "", t, flags=re.S)


def funktionen(t: str):
    """Grob nach Funktionen zerlegen: Name -> Rumpf."""
    aus = {}
    for m in re.finditer(r"^[A-Za-z_][\w:<>*&\s]*?(\w+)\([^;{]*\)\s*\{", t,
                         re.M):
        i = m.end() - 1
        tiefe = 0
        k = i
        while k < len(t):
            if t[k] == "{":
                tiefe += 1
            elif t[k] == "}":
                tiefe -= 1
                if tiefe == 0:
                    break
            k += 1
        aus.setdefault(m.group(1), []).append(t[i:k + 1])
    return aus


def main() -> int:
    if not QUELLE.exists():
        print("D3D-Besitz: %s fehlt" % QUELLE)
        return 1
    t = ohneKommentare(QUELLE.read_text(encoding="utf-8"))
    felder = FELD.findall(t)
    tabellen = TABELLE.findall(t)
    if not felder:
        print("D3D-Besitz: keine Felder gefunden - hat sich die Datei "
              "geaendert?")
        return 1

    fn = funktionen(t)
    if "shutdown" not in fn:
        print("D3D-Besitz: shutdown() nicht gefunden")
        return 1
    aufraeumen = "\n".join(fn["shutdown"])

    fehler = 0

    # --- 1. Alles wird beim Beenden freigegeben --------------------------
    for f in felder:
        if not re.search(r"\b%s->Release\(\)" % f, aufraeumen):
            print('  "%s" wird in shutdown() nicht freigegeben' % f)
            fehler += 1
    for f in tabellen:
        if not re.search(r"\b%s\b" % f, aufraeumen):
            print('  Tabelle "%s" wird in shutdown() nicht geleert' % f)
            fehler += 1

    # --- 2. Neuanlegen gibt die alte Ressource frei ----------------------
    #
    # Gesucht: `Create...(..., &g_feld)` ausserhalb von shutdown. Davor muss
    # im selben Rumpf entweder ein `g_feld->Release()` stehen oder eine
    # Pruefung auf `g_feld != nullptr` / `== nullptr` - dann wird nur
    # angelegt, wenn keine da ist.
    for name, rumpfe in fn.items():
        if name == "shutdown":
            continue
        for rumpf in rumpfe:
            for m in re.finditer(r"Create\w+\([^;]*?&(g_\w+)\s*\)", rumpf):
                feld = m.group(1)
                if feld not in felder:
                    continue
                davor = rumpf[:m.start()]
                gibtFrei = re.search(r"\b%s->Release\(\)" % feld, davor)
                geprueft = re.search(r"\b%s\s*[!=]=\s*nullptr" % feld, davor)
                if not gibtFrei and not geprueft:
                    print('  %s(): legt "%s" an, ohne die alte Ressource '
                          "freizugeben oder auf nullptr zu pruefen"
                          % (name, feld))
                    fehler += 1

    if fehler != 0:
        print("D3D-Besitzpruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("D3D-Besitzpruefung: ok (%d Felder, %d Tabellen)"
          % (len(felder), len(tabellen)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
