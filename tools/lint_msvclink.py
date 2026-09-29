#!/usr/bin/env python3
"""Symbole, die MinGW mitbringt und MSVC nicht.

Warum das ein eigener Pruefer ist:

`WKPDID_D3DDebugObjectName` ist in `d3dcommon.h` nur ERKLAERT; die Definition
liegt in `dxguid.lib`. MinGW hat sie in seinen Bibliotheken und band
klaglos. Bei shank:

    error LNK2019: nicht aufgeloestes externes Symbol
                   "WKPDID_D3DDebugObjectName"

Der Pruefzug hier bindet mit MinGW. Dass es dort geht, sagt nichts darueber,
ob es mit MSVC geht - und genau das ist die Luecke, durch die rc461 gefallen
ist.

Geprueft wird: keiner der bekannten Faelle steht im Quelltext, ohne dass der
Wert daneben selbst definiert ist. Die Liste waechst mit jedem Fall, der
durchrutscht - vollstaendig ist sie nie.
"""
import re
import sys
from pathlib import Path

HIER = Path(__file__).resolve().parent
GUI = HIER.parent / "gui"

# Symbol -> welche Bibliothek MSVC dafuer braucht.
NUR_MIT_LIB = {
    "WKPDID_D3DDebugObjectName": "dxguid.lib",
    "IID_ID3D11Debug": "dxguid.lib",
    "IID_ID3D11InfoQueue": "dxguid.lib",
    "IID_IDXGIDevice": "dxguid.lib",
    "IID_IDXGIFactory": "dxguid.lib",
}


def main() -> int:
    fehler = 0
    geprueft = 0
    for datei in sorted(GUI.glob("*.cpp")):
        t = datei.read_text(encoding="utf-8")
        t = re.sub(r"//[^\n]*", "", t)
        t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
        geprueft += 1
        for sym, lib in NUR_MIT_LIB.items():
            if re.search(r"\b" + sym + r"\b", t):
                print("  %s benutzt %s - MSVC braucht dafuer %s. Entweder "
                      "die Bibliothek binden oder den Wert selbst definieren."
                      % (datei.name, sym, lib))
                fehler += 1

    if fehler != 0:
        print("MSVC-Bindepruefung: %d Beanstandung(en)" % fehler)
        return 1
    print("MSVC-Bindepruefung: ok (%d Dateien, %d bekannte Faelle)"
          % (geprueft, len(NUR_MIT_LIB)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
