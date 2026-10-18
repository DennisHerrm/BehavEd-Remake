#!/usr/bin/env python3
"""rc602: jede Karte der Spielordner gegen das Spiel pruefen.

Gewuenscht: "er soll einfach alle karten oeffnen und checken ob alles da ist".

Startet behaved.exe mit BHED_EDITORTEST=kartenpruefung (gui/selbsttest.cpp,
kartenPruefungSchritte). Das Programm schreibt je Karte eine JSON-Zeile nach
<aus>/kartenpruefung.jsonl und vor jeder Karte deren Nummer nach
<aus>/kartenpruefung.jsonl.lauf. Stuerzt es ab oder kommt eine Karte nicht
voran (--zeit Sekunden ohne neue Zeile), wird NUR dieser Prozess beendet
(taskkill /PID /T), die Karte vermerkt und mit der naechsten weitergemacht
(BHED_KARTEN_AB).

Danach (oder mit --nur-auswerten) die Auswertung:
  <aus>/bericht.json        Summen und alle Karten
  <aus>/karten.csv          eine Zeile je Karte
  <aus>/zusammenfassung.txt Ranglisten: was fehlt auf wie vielen Karten
--vergleich <alt/bericht.json> setzt die Summen vorher/nachher daneben.

Aufruf (eigener Einstellungsordner, nie die behaved.cfg des Anwenders):
  python tools/kartenpruefung.py --exe <lauf>/behaved.exe --einst <lauf>/einst --aus <ordner>
Kein Ton (der Selbsttest ist stumm), keine Dialoge.
"""
import argparse
import csv
import json
import os
import subprocess
import sys
import time
from collections import defaultdict


def lies_jsonl(pfad):
    karten = {}
    if not os.path.exists(pfad):
        return karten
    with open(pfad, encoding='utf-8', errors='replace') as f:
        for zeile in f:
            zeile = zeile.strip()
            if not zeile:
                continue
            try:
                d = json.loads(zeile)
            except ValueError:
                continue
            karten[d['karte']] = d
    return karten


def lies_lauf(pfad):
    try:
        with open(pfad, encoding='utf-8', errors='replace') as f:
            return f.read().strip()
    except OSError:
        return ''


def beende(p):
    # Nur den eigenen Prozess (mit Kindern) - nie andere behaved.exe.
    subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        p.wait(timeout=30)
    except subprocess.TimeoutExpired:
        pass


def laufen(a):
    os.makedirs(a.aus, exist_ok=True)
    bericht = os.path.abspath(os.path.join(a.aus, 'kartenpruefung.jsonl'))
    lauf = bericht + '.lauf'
    probleme_pfad = os.path.join(a.aus, 'probleme.json')
    probleme = []
    if a.weiter and os.path.exists(probleme_pfad):
        probleme = json.load(open(probleme_pfad, encoding='utf-8'))
    else:
        for p in (bericht, lauf):
            if os.path.exists(p):
                os.remove(p)
    ab = a.ab
    ordner = os.path.dirname(os.path.abspath(a.exe))
    versuche_ohne_fortschritt = 0
    while True:
        env = dict(os.environ)
        env.update({'BHED_EDITORTEST': 'kartenpruefung', 'BHED_EINSTELLUNGEN': os.path.abspath(a.einst),
                    'BHED_PRUEFBERICHT': bericht, 'BHED_KARTEN_AB': str(ab)})
        if a.karten:
            env['BHED_KARTEN'] = a.karten
        if os.path.exists(lauf):
            os.remove(lauf)
        t0 = time.time()
        p = subprocess.Popen([os.path.abspath(a.exe)], cwd=ordner, env=env)
        print('gestartet PID %d ab Karte %d' % (p.pid, ab), flush=True)
        letzter = None
        seit = time.time()
        grund = ''
        while p.poll() is None:
            time.sleep(2)
            stand = (os.path.getsize(bericht) if os.path.exists(bericht) else 0, lies_lauf(lauf))
            if stand != letzter:
                letzter = stand
                seit = time.time()
            # Der Start (Shader lesen, erste Karte) darf laenger dauern.
            grenze = a.zeit * (3 if not stand[1] else 1)
            if time.time() - seit > grenze:
                grund = 'haengt (%d s ohne Fortschritt)' % grenze
                beende(p)
                break
        code = p.returncode
        stand = lies_lauf(lauf)
        if stand.startswith('fertig'):
            print('fertig nach %.0f s' % (time.time() - t0), flush=True)
            break
        if not stand:
            versuche_ohne_fortschritt += 1
            if versuche_ohne_fortschritt >= 2:
                print('kein Fortschritt - Abbruch (Code %s)' % code, flush=True)
                break
            continue
        teile = stand.split(' ', 2)
        nr, karte = int(teile[0]), teile[2] if len(teile) > 2 else '?'
        fertig = lies_jsonl(bericht)
        if karte in fertig:
            # Absturz/Haenger NACH dem Bericht dieser Karte - beim Wechsel.
            nr_weiter = nr + 1
        else:
            nr_weiter = nr + 1
            probleme.append({'karte': karte, 'nr': nr, 'grund': grund or ('abgestuerzt (Code %s)' % code)})
            # Protokolle und Abbild dieses Laufs aufheben - der Neustart
            # ueberschreibt sie sonst.
            for name in ('behaved.log', 'behaved-detail.log', 'behaved-absturz.dmp'):
                q = os.path.join(ordner, name)
                if os.path.exists(q):
                    z = os.path.join(a.aus, 'karte%d_%s' % (nr, name))
                    try:
                        os.replace(q, z)
                    except OSError:
                        pass
            print('PROBLEM %s: %s' % (karte, probleme[-1]['grund']), flush=True)
        json.dump(probleme, open(probleme_pfad, 'w', encoding='utf-8'), indent=1)
        versuche_ohne_fortschritt = 0
        ab = nr_weiter
    json.dump(probleme, open(probleme_pfad, 'w', encoding='utf-8'), indent=1)


def auswerten(a):
    bericht = os.path.join(a.aus, 'kartenpruefung.jsonl')
    karten = lies_jsonl(bericht)
    probleme_pfad = os.path.join(a.aus, 'probleme.json')
    probleme = json.load(open(probleme_pfad, encoding='utf-8')) if os.path.exists(probleme_pfad) else []
    # Rangliste: Merkmal -> {karten, anzahl}
    def rang(feld, liste=False):
        r = defaultdict(lambda: {'karten': 0, 'anzahl': 0, 'beispiele': []})
        for k, d in sorted(karten.items()):
            werte = d.get(feld, [] if liste else {})
            if liste:
                werte = {w: 1 for w in werte}
            for m, n in werte.items():
                r[m]['karten'] += 1
                r[m]['anzahl'] += n
                if len(r[m]['beispiele']) < 5:
                    r[m]['beispiele'].append(k.replace('maps/', '').replace('.bsp', ''))
        return dict(sorted(r.items(), key=lambda kv: (-kv[1]['karten'], -kv[1]['anzahl'], kv[0])))

    raenge = {
        'nicht_gezeichnet': rang('nicht_gezeichnet'),
        'shader_welt': rang('shader_welt'),
        'shader_welt_dreiecke': rang('shader_welt_dreiecke'),
        'shader_figur': rang('shader_figur'),
        'shader_effekt': rang('shader_effekt'),
        'npc_ohne_modell': rang('npc_ohne_modell', True),
        'efx_fehlt': rang('efx_fehlt', True),
        'klang_fehlt': rang('klang_fehlt', True),
        'textur_fehlt': rang('textur_fehlt', True),
        'modell_fehlt': rang('modell_fehlt', True),
        'efx_warnungen': rang('efx_warnungen', True),
        'nicht_im_spiel': rang('nicht_im_spiel'),
    }
    def zahl_karten(bed):
        return sum(1 for d in karten.values() if bed(d))
    summen = {
        'karten_geprueft': len(karten),
        'karten_abgestuerzt_oder_haengend': len(probleme),
        'karten_nicht_geladen': zahl_karten(lambda d: not d['geladen'] or not d['bereit']),
        'karten_mit_gpu_fehler': zahl_karten(lambda d: bool(d['gpu_fehler'])),
        'karten_mit_d3d_meldungen': zahl_karten(lambda d: d['d3d_meldungen'] > 0),
        'karten_mit_protokollfehlern': zahl_karten(lambda d: d['protokoll_err'] > 0),
        'karten_mit_nicht_gezeichneten_entities': zahl_karten(lambda d: bool(d['nicht_gezeichnet'])),
        'nicht_gezeichnete_entities': sum(sum(d['nicht_gezeichnet'].values()) for d in karten.values()),
        'karten_mit_npc_ohne_modell': zahl_karten(lambda d: bool(d['npc_ohne_modell'])),
        'karten_mit_weltshader_luecken': zahl_karten(lambda d: bool(d['shader_welt'])),
        'weltshader_luecken_dreiecke': sum(sum(d['shader_welt_dreiecke'].values()) for d in karten.values()),
        'karten_mit_figurshader_luecken': zahl_karten(lambda d: bool(d['shader_figur'])),
        'karten_mit_effektshader_luecken': zahl_karten(lambda d: bool(d['shader_effekt'])),
        'karten_mit_fehlender_efx': zahl_karten(lambda d: bool(d['efx_fehlt'])),
        'karten_mit_fehlendem_klang': zahl_karten(lambda d: bool(d['klang_fehlt'])),
        'karten_mit_fehlender_textur': zahl_karten(lambda d: bool(d['textur_fehlt'])),
        'karten_mit_fehlendem_modell': zahl_karten(lambda d: bool(d['modell_fehlt'])),
    }
    json.dump({'summen': summen, 'probleme': probleme, 'raenge': raenge,
               'karten': [karten[k] for k in sorted(karten)]},
              open(os.path.join(a.aus, 'bericht.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
    with open(os.path.join(a.aus, 'karten.csv'), 'w', encoding='utf-8', newline='') as f:
        w = csv.writer(f)
        w.writerow(['karte', 'geladen', 'lade_ms', 'entities', 'dreiecke', 'figuren', 'modelle', 'effekte',
                    'nicht_gezeichnet', 'npc_ohne_modell', 'efx_fehlt', 'klang_fehlt', 'textur_fehlt',
                    'modell_fehlt', 'shader_welt', 'shader_figur', 'shader_effekt', 'gpu_fehler', 'd3d',
                    'protokoll_err'])
        for k in sorted(karten):
            d = karten[k]
            fmt = lambda m: ' '.join('%s=%d' % kv for kv in sorted(m.items()))
            w.writerow([k, int(d['geladen'] and d['bereit']), d['lade_ms'], d['entities'], d['dreiecke'],
                        d['figuren'], d['modelle'], d['effekte'], fmt(d['nicht_gezeichnet']),
                        ' '.join(d['npc_ohne_modell']), ' '.join(d['efx_fehlt']), ' '.join(d['klang_fehlt']),
                        len(d['textur_fehlt']), ' '.join(d['modell_fehlt']), fmt(d['shader_welt']),
                        fmt(d['shader_figur']), fmt(d['shader_effekt']), d['gpu_fehler'], d['d3d_meldungen'],
                        d['protokoll_err']])
    alt = None
    if a.vergleich:
        alt = json.load(open(a.vergleich, encoding='utf-8'))['summen']
    z = ['Kartenpruefung - %d Karten (Regelwerk %s)' % (len(karten), ', '.join(sorted({d['regelwerk'] for d in karten.values()}))), '']
    z.append('SUMMEN' + ('  (vorher -> nachher)' if alt else ''))
    for k, v in summen.items():
        z.append('  %-42s %s' % (k, ('%s -> %s' % (alt.get(k, '?'), v)) if alt else v))
    for p in probleme:
        z.append('  PROBLEM %s: %s' % (p['karte'], p['grund']))
    titel = {
        'nicht_gezeichnet': 'Entities, die das Spiel zeigt und behaved nicht zeichnet (Klasse: Karten / Anzahl)',
        'shader_welt': 'Welt-Shader: Anweisungen, die behaved nicht nachbildet (Karten / Shader)',
        'shader_welt_dreiecke': 'Welt-Shader: betroffene Dreiecke (Karten / Dreiecke)',
        'shader_figur': 'Figuren-Shader: fehlende Stufen/Merkmale (Karten / Shader)',
        'shader_effekt': 'Effekt-Shader: fehlende Merkmale (Karten / Shader)',
        'npc_ohne_modell': 'NPCs ohne Modell (Klasse (Typ): Karten)',
        'efx_fehlt': 'fx_runner-Dateien, die in keinem Archiv liegen (auch im Spiel nichts)',
        'klang_fehlt': 'Klaenge, die in keinem Archiv liegen',
        'textur_fehlt': 'Texturen, die nicht gefunden wurden',
        'modell_fehlt': 'Kartenmodelle, die nicht gefunden wurden',
        'efx_warnungen': 'efx-Leser: Warnungen (unbekannte Schluessel/Gruppen)',
        'nicht_im_spiel': 'Klassen, die das Spiel nicht spawnt (zur Info)',
    }
    for feld, t in titel.items():
        r = raenge[feld]
        z.append('')
        z.append('%s - %d verschiedene' % (t, len(r)))
        for m, w in list(r.items())[:a.top]:
            z.append('  %-50s %4d Karten %8d   z.B. %s' % (m[:50], w['karten'], w['anzahl'], ', '.join(w['beispiele'][:3])))
    open(os.path.join(a.aus, 'zusammenfassung.txt'), 'w', encoding='utf-8').write('\n'.join(z) + '\n')
    print('\n'.join(z[:40]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', help='behaved.exe (in einem eigenen Laufordner)')
    ap.add_argument('--einst', help='eigener Einstellungsordner (BHED_EINSTELLUNGEN)')
    ap.add_argument('--aus', required=True, help='Ausgabeordner')
    ap.add_argument('--zeit', type=int, default=180, help='Sekunden ohne Fortschritt, dann gilt die Karte als haengend')
    ap.add_argument('--ab', type=int, default=0, help='mit Karte Nummer N beginnen')
    ap.add_argument('--karten', default='', help='nur Karten, deren Name eines davon enthaelt (Komma)')
    ap.add_argument('--weiter', action='store_true', help='an einen vorhandenen Bericht anhaengen')
    ap.add_argument('--nur-auswerten', action='store_true')
    ap.add_argument('--vergleich', default='', help='frueherer bericht.json fuer vorher/nachher')
    ap.add_argument('--top', type=int, default=25)
    a = ap.parse_args()
    if not a.nur_auswerten:
        if not a.exe or not a.einst:
            ap.error('--exe und --einst noetig')
        laufen(a)
    auswerten(a)
    return 0


if __name__ == '__main__':
    sys.exit(main())
