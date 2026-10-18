#!/usr/bin/env python3
"""
Liest die Importtabelle einer .exe und prueft, welche DLLs sie FEST braucht.

Warum das wichtig ist: steht d3d11.dll als harter Import drin, laedt Windows
das Programm gar nicht erst, wenn Direct3D fehlt - und der Rueckfall auf
OpenGL, fuer genau diesen Fall gebaut, kaeme nie zum Zug. Dasselbe gilt fuer
d3dcompiler_47.dll, die auf Windows 7 nicht mitgeliefert wird.

Liest die PE-Struktur selbst, ohne Fremdbibliothek.
"""
import struct, sys

# DLLs, die nicht fest gebunden sein duerfen
VERBOTEN = {'d3d11.dll', 'd3dcompiler_47.dll', 'd3dcompiler_46.dll',
            'dxgi.dll', 'shcore.dll'}
# DLLs, die auf jedem Windows ab Vista vorhanden sind
ERLAUBT = {'kernel32.dll', 'user32.dll', 'gdi32.dll', 'shell32.dll',
           'advapi32.dll', 'ole32.dll', 'oleaut32.dll', 'opengl32.dll',
           'msvcrt.dll', 'imm32.dll', 'dwmapi.dll', 'comdlg32.dll',
           'version.dll', 'winmm.dll', 'ws2_32.dll', 'shlwapi.dll'}
# rc601 release: winhttp.dll gehoert seit Windows XP SP1 zum System und ist
# der Weg des Updaters zu GitHub (gui/update_win32.cpp). Sie fehlte hier,
# deshalb meldete die Pruefung am veroeffentlichten rc600 neben der
# VC-Laufzeit auch WINHTTP.dll als "unbekannt". Seit dieser Fassung laeuft
# die Pruefung als ctest (exe_importe) - mit Fehlalarm waere sie dort
# unbrauchbar.
ERLAUBT |= {'winhttp.dll'}


def imports(path):
    return imports_aus_daten(open(path, 'rb').read())


# rc601 release: dieselbe Lesung fuer eine .exe, die nur im Speicher liegt -
# tools/release_paket.py prueft die behaved.exe IM Zip, ohne sie auszupacken.
def imports_aus_daten(d):
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    assert d[pe:pe+4] == b'PE\0\0', 'keine PE-Datei'
    nsec, = struct.unpack_from('<H', d, pe + 6)
    optsize, = struct.unpack_from('<H', d, pe + 20)
    magic, = struct.unpack_from('<H', d, pe + 24)
    plus = (magic == 0x20B)
    dd = pe + 24 + (112 if plus else 96)
    imp_rva, imp_size = struct.unpack_from('<II', d, dd + 8)

    sections = []
    off = pe + 24 + optsize
    for i in range(nsec):
        name, vsize, vaddr, rsize, raddr = struct.unpack_from('<8sIIII', d, off + i*40)
        sections.append((vaddr, vsize, raddr, rsize))

    def rva2off(rva):
        for vaddr, vsize, raddr, rsize in sections:
            if vaddr <= rva < vaddr + max(vsize, rsize):
                return raddr + (rva - vaddr)
        return None

    out = []
    p = rva2off(imp_rva)
    if p is None:
        return out
    while True:
        ilt, ts, fc, name_rva, iat = struct.unpack_from('<IIIII', d, p)
        if name_rva == 0:
            break
        n = rva2off(name_rva)
        end = d.index(b'\0', n)
        out.append(d[n:end].decode('ascii'))
        p += 20
    return out


def main():
    if len(sys.argv) < 2:
        print("Aufruf: check_exe_imports.py <datei.exe>", file=sys.stderr)
        return 2
    dlls = imports(sys.argv[1])
    bad = []
    print("Feste Importe:")
    for dll in sorted(dlls, key=str.lower):
        low = dll.lower()
        mark = ''
        if low in VERBOTEN:
            mark = '  <-- DARF NICHT FEST GEBUNDEN SEIN'
            bad.append(dll)
        elif low not in ERLAUBT:
            mark = '  <-- unbekannt, bitte pruefen'
            bad.append(dll)
        print(f"   {dll}{mark}")
    print("Importpruefung:", "ok" if not bad else f"{len(bad)} Beanstandung(en)")
    return 1 if bad else 0


# rc601 release: nur als Programm ausfuehren. tools/release_paket.py laedt
# diese Datei als Modul (ERLAUBT, VERBOTEN, imports_aus_daten); vorher
# beendete schon das Laden den Aufrufer.
if __name__ == "__main__":
    sys.exit(main())
