#!/usr/bin/env python3
"""Liest die Dialoge aus der originalen BehavEd.exe.

Warum es das gibt:

"Kannst du alle behaved Funktionen abgleichen mit dem Originalen, dass wir
wissen die Funktion ist sauber und geht" - und die einzige belastbare
Antwort darauf ist eine gemessene, keine erinnerte.

Die .exe von 1999 ist ein 32-Bit-PE mit DIALOG-Ressourcen. Darin steht die
vollstaendige Bedienoberflaeche des Originals: jeder Knopf, jedes Feld,
jede Beschriftung. Das ist die Liste, gegen die verglichen wird.

Aufruf:  python3 tools/originaldialoge.py <Pfad zur BehavEd.exe>

Das Ergebnis steht in ABGLEICH.md. Wer den Abgleich wiederholen will,
braucht diese Datei - deshalb liegt sie hier und nicht in einem Wegwerfordner.
"""
import sys

import struct
d=open(sys.argv[1] if len(sys.argv)>1 else 'BehavEd.exe','rb').read()
pe=struct.unpack('<I', d[0x3c:0x40])[0]
nsec=struct.unpack('<H', d[pe+6:pe+8])[0]
optsz=struct.unpack('<H', d[pe+20:pe+22])[0]
off=pe+24+optsz
secs={}
for i in range(nsec):
    b=d[off+i*40:off+(i+1)*40]
    name=b[:8].rstrip(b'\0').decode('latin1')
    vsz,va,rsz,ra=struct.unpack('<IIII', b[8:24])
    secs[name]=(va,rsz,ra)
rva,rsz,raw=secs['.rsrc']

def entries(o):
    nn,ni=struct.unpack('<HH', d[raw+o+12:raw+o+16])
    out=[]
    for i in range(nn+ni):
        nameid,sub=struct.unpack('<II', d[raw+o+16+i*8: raw+o+16+(i+1)*8])
        out.append((nameid,sub))
    return out

def resname(nameid):
    if nameid & 0x80000000:
        o=raw+(nameid & 0x7fffffff)
        ln=struct.unpack('<H', d[o:o+2])[0]
        return d[o+2:o+2+ln*2].decode('utf-16-le')
    return str(nameid)

def wstr(o):
    # gibt (text, neuer offset)
    v=struct.unpack('<H', d[o:o+2])[0]
    if v==0x0000: return ("", o+2)
    if v==0xFFFF: return ("#%d" % struct.unpack('<H', d[o+2:o+4])[0], o+4)
    s=[]
    while True:
        c=struct.unpack('<H', d[o:o+2])[0]; o+=2
        if c==0: break
        s.append(chr(c))
    return ("".join(s), o)

KLASSE={0x80:'BUTTON',0x81:'EDIT',0x82:'STATIC',0x83:'LISTBOX',
        0x84:'SCROLLBAR',0x85:'COMBOBOX'}

for tid, tsub in entries(0):
    if (tid & 0x7fffffff) != 5 or not (tsub & 0x80000000): continue
    for nid, nsub in entries(tsub & 0x7fffffff):
        for lid, lsub in entries(nsub & 0x7fffffff):
            do,dsz = struct.unpack('<II', d[raw+(lsub&0x7fffffff): raw+(lsub&0x7fffffff)+8])
            o = raw + (do - rva)
            sig, = struct.unpack('<H', d[o+2:o+4])
            erweitert = (sig == 0xFFFF)
            if erweitert:
                cnt = struct.unpack('<H', d[o+16:o+18])[0]; p=o+18
            else:
                cnt = struct.unpack('<H', d[o+8:o+10])[0]; p=o+16
            menu,p = wstr(p); klasse,p = wstr(p); titel,p = wstr(p)
            print("\n--- Dialog %s: \"%s\"  (%d Steuerelemente) ---" % (resname(nid), titel, cnt))
            # Schriftblock ueberspringen
            stil = struct.unpack('<I', d[o+12:o+16])[0] if erweitert else struct.unpack('<I', d[o:o+4])[0]
            if stil & 0x40:
                p += 2
                if erweitert: p += 6
                _,p = wstr(p)
            texte=[]
            for i in range(cnt):
                p = (p+3) & ~3
                p += 24 if erweitert else 18
                kl,p = wstr(p); tx,p = wstr(p)
                extra = struct.unpack('<H', d[p:p+2])[0]; p += 2 + extra
                kn = KLASSE.get(ord(kl[0]), kl) if kl and ord(kl[0])<0x100 else kl
                if tx: texte.append("%-9s %s" % (kn, tx))
            for t in texte[:40]: print("   ", t)
