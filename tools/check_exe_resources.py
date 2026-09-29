#!/usr/bin/env python3
"""
Prueft, ob Symbol, Manifest und Versionsangaben wirklich in der .exe stehen.

Anlass: Pillow schrieb auf die Anforderung von sieben Symbolgroessen genau
EINE 16x16 in die .ico - ohne Fehlermeldung. Gebaut hat es trotzdem, und der
Explorer haette eine verwaschene Kachel gezeigt. Was in der fertigen Datei
steht, laesst sich nur an der fertigen Datei pruefen.
"""
import sys, pefile

WANT_ICON_SIZES = 7

def main(path):
    pe = pefile.PE(path)
    kinds = {}
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        kinds[t.struct.Id] = len(t.directory.entries)
    bad = 0

    icons = kinds.get(3, 0)
    print(f"  Symbolgroessen : {icons}")
    if icons < WANT_ICON_SIZES:
        print(f"  [FEHLER] erwartet {WANT_ICON_SIZES}, gefunden {icons}")
        bad += 1
    if kinds.get(14, 0) < 1:
        print("  [FEHLER] keine GROUP_ICON - der Explorer findet das Symbol nicht")
        bad += 1
    if kinds.get(24, 0) < 1:
        print("  [FEHLER] kein Manifest")
        bad += 1
    else:
        print("  Manifest       : vorhanden")
    if kinds.get(16, 0) < 1:
        print("  [FEHLER] keine Versionsangaben")
        bad += 1

    # Die Felder, die Rechtsklick -> Details zeigt.
    seen = {}
    for fi in getattr(pe, 'FileInfo', []):
        for e in fi:
            if e.Key == b'StringFileInfo':
                for st in e.StringTable:
                    for k, v in st.entries.items():
                        seen[k.decode()] = v.decode()
    for key in ('FileDescription', 'FileVersion', 'ProductName', 'OriginalFilename'):
        if key not in seen or not seen[key]:
            print(f"  [FEHLER] Versionsfeld {key} fehlt")
            bad += 1
    if bad == 0:
        print(f"  Beschreibung   : {seen.get('FileDescription','')}")
        print(f"  Fassung        : {seen.get('FileVersion','')}")

    # Administratorrechte anzufordern waere ein Warnsignal.
    man = None
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if t.struct.Id != 24:
            continue
        for e in t.directory.entries:
            for l in e.directory.entries:
                d = l.data.struct
                man = pe.get_data(d.OffsetToData, d.Size).decode('utf-8', 'replace')
    if man and 'requireAdministrator' in man:
        print("  [FEHLER] das Manifest fordert Administratorrechte an")
        bad += 1
    elif man and 'asInvoker' in man:
        print("  Rechte         : asInvoker, keine UAC-Nachfrage")

    print("Ressourcenpruefung:", "ok" if bad == 0 else f"{bad} Beanstandung(en)")
    return 1 if bad else 0

sys.exit(main(sys.argv[1]))
