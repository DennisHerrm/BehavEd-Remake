#!/usr/bin/env python3
"""
Vollstaendiger Nachbau: Hauptfenster (Dialog 102) + Event-Editor (Dialog 137).

Der Event-Editor wird NICHT je Befehl nachgebaut, sondern zur Laufzeit aus
der Parameterangabe erzeugt - so wie BehavEd es tut. Beleg: Dialog 137
enthaelt genau ein Bedienelement (STATIC "TEST_FTYPESTRING") als Schablone.

Abgeleitete Rasterung, geht bei 539x58 dlu genau auf:
    Rand links 13, Feldbreite 78, Feldabstand 86, 6 Felder
        13 + 6*86 = 529,  + 10 Rand rechts = 539   <- Breite von Dialog 137
    Beschriftung y=9 h=8, Bedienelement y=20 h=13, Knoepfe y=41 h=14
        41 + 14 + 3 = 58                           <- Hoehe von Dialog 137
    6 Felder ist genau das Maximum: camera(FADE) hat Auswahl + 5 Parameter.
"""
import json, os, html

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DX, DY = 1.5, 1.625


def px(v, a):
    return round(v * (DX if a == 'x' else DY))


CMD_ICON = {'flush': 'I_FLUSH', 'if': 'I_IF', 'else': 'I_IF', 'loop': 'I_LOOP',
            'wait': 'I_WAITCLOCK', 'waitsignal': 'I_WAITSIGNAL',
            'signal': 'I_SIGNAL', 'sound': 'I_SOUND', 'move': 'I_MOVE',
            'rotate': 'I_ROTATE', 'remove': 'I_REMOVE', 'set': 'I_SET',
            'camera': 'I_CAMERA', 'do': 'I_DO', 'dowait': 'I_DOWAIT',
            'affect': 'I_BRACE', 'task': 'I_BRACE', 'rem': 'I_MACRO'}

CSS = """
body{margin:0;background:#5a7a9a;font:11px 'MS Sans Serif',Tahoma,Verdana,sans-serif}
#win{position:relative;margin:18px auto;background:#d4d0c8;
 box-shadow:inset -1px -1px 0 #404040,inset 1px 1px 0 #fff,inset -2px -2px 0 #808080,inset 2px 2px 0 #dfdfdf}
.cap{height:18px;background:linear-gradient(90deg,#0a246a,#a6caf0);color:#fff;
 font-weight:bold;padding:2px 4px;display:flex;justify-content:space-between;user-select:none}
#client{position:relative}
.grp{position:absolute;margin:0;padding:0;border:1px solid #fff;border-top-color:#808080;
 border-left-color:#808080;box-sizing:border-box}
.grp:after{content:'';position:absolute;inset:-1px;border:1px solid #808080;
 border-top-color:#fff;border-left-color:#fff;pointer-events:none}
.grp>legend{padding:0 3px}
.btn{position:absolute;background:#d4d0c8;border:0;padding:0;color:#000;
 font:11px 'MS Sans Serif',Tahoma,sans-serif;cursor:default;overflow:hidden;
 box-shadow:inset -1px -1px 0 #404040,inset 1px 1px 0 #fff,inset -2px -2px 0 #808080,inset 2px 2px 0 #dfdfdf}
.btn:active{box-shadow:inset 1px 1px 0 #404040,inset 2px 2px 0 #808080}
.btn.def{outline:1px solid #000}
.dis{color:#808080;text-shadow:1px 1px 0 #fff}
.lbl{position:absolute;overflow:hidden;white-space:nowrap}
.hint{position:absolute;text-align:center;color:#606060;font-size:10px;white-space:nowrap}
.edit,.tree,.list{position:absolute;background:#fff;
 box-shadow:inset 1px 1px 0 #404040,inset -1px -1px 0 #fff,inset 2px 2px 0 #808080,inset -2px -2px 0 #d4d0c8}
.tree,.list{overflow:auto}
.tree{padding:2px}
.chk{position:absolute;display:flex;align-items:center;gap:4px}
.chk .box{width:13px;height:13px;background:#fff;flex:none;
 box-shadow:inset 1px 1px 0 #404040,inset -1px -1px 0 #fff,inset 2px 2px 0 #808080,inset -2px -2px 0 #d4d0c8}
.row{display:flex;align-items:center;gap:3px;height:17px;white-space:nowrap;padding-left:2px;cursor:default}
.row img{width:16px;height:16px;image-rendering:pixelated;flex:none}
.row.sel,.row:hover{background:#000080;color:#fff}
.prog{position:absolute;background:#d4d0c8;box-shadow:inset 1px 1px 0 #404040,inset -1px -1px 0 #fff}
/* Event-Editor */
#dlgwrap{position:fixed;inset:0;display:none;align-items:center;justify-content:center;
 background:rgba(0,0,0,.25);z-index:9}
#dlg{position:relative;background:#d4d0c8;
 box-shadow:inset -1px -1px 0 #404040,inset 1px 1px 0 #fff,inset -2px -2px 0 #808080,inset 2px 2px 0 #dfdfdf}
#dlgbody{position:relative}
#dlg input,#dlg select{position:absolute;font:11px 'MS Sans Serif',Tahoma,sans-serif;
 background:#fff;border:0;padding:0 2px;box-sizing:border-box;
 box-shadow:inset 1px 1px 0 #404040,inset -1px -1px 0 #fff,inset 2px 2px 0 #808080,inset -2px -2px 0 #d4d0c8}
#dlg select{padding:0}
#dlg input:disabled{background:#d4d0c8;color:#000}
"""

JS = r"""
const MODEL = __MODEL__, ICONS = __ICONS__;
const DX=1.5, DY=1.625, PX=(v,a)=>Math.round(v*(a==='x'?DX:DY));
// Rasterkonstanten AM ORIGINAL GEMESSEN (Bildschirmfotos vom 11.08.2026,
// 150 % Windows-Skalierung, herausgerechnet ueber den Knopfabstand der
// File-Gruppe: 13 dlu = 31,5 px statt 21,1 px).
//   Feld (Eingabe oder Klappliste) 167 x 14 dlu
//   angehaengter Knopf              33 dlu breit
//   Abstand zum naechsten Feld       9 dlu
//   erstes Feld bei x = 100; die Beschriftung endet rechtsbuendig bei 91,
//   genau dort, wo der STATIC "TEST_FTYPESTRING" aus Dialog 137 aufhoert
//   (x=13, breit 78) - das ist also die Befehlsbeschriftung, keine Schablone
//   fuer Feldnamen.
//   Knopfzeile 88 dlu unter der Feldzeile, unabhaengig von der Feldzahl.
const G = {left:100, fieldw:167, fieldh:14, btnw:33, gap:9, labelend:91,
           yfield:7, yhint:23, yhelp:49, ybtn:95, okw:67, okh:13, okpitch:77,
           right:26, helperPitch:28};

function entryOf(tsName, val){
  const ts = MODEL.typesets[tsName]; if(!ts) return null;
  return ts.entries.find(e=>e.name===val) || null;
}

// Feldliste eines Befehls: eigene Parameter, plus die des gewaehlten
// Auswahllisten-Eintrags. Genau das macht set(SET_HEALTH,...) zum Zahlenfeld.
function fieldsFor(cmd, state){
  const out=[]; let skip=0;
  cmd.params.forEach((p,i)=>{
    if(skip>0){ skip--; return; }
    out.push({p, idx:i});
    if(p.kind==='%t'){
      const e = entryOf(p.default, state[i]);
      if(e && e.params && e.params.length){
        // Die Parameter des gewaehlten Eintrags ersetzen die restlichen
        // Parameter des Befehls (sound/set), oder ergaenzen sie (camera).
        // Der ANGEZEIGTE Vorgabewert bleibt aber der aus der Signatur:
        // set(SET_PARM1, ...) zeigt "DEFAULT", sound(CHAN_AUTO, ...) zeigt
        // "FILENAME" - beides steht so in den Bildschirmfotos.
        e.params.forEach((sp,k)=>{
          const repl = cmd.params[i+1+k];
          const q = Object.assign({}, sp);
          if(repl && repl.default) q.default = repl.default;
          out.push({p:q, idx:'s'+i+'_'+k, sub:true});
        });
        skip = e.params.length;
      }
    }
  });
  return out;
}

function labelFor(f){
  const p=f.p;
  if(p.kind==='%t') return p.default;
  if(p.kind==='%op') return 'operator';
  if(p.kind==='%e') return 'expression';
  if(p.filter) return 'filename';
  return ({'%s':'string','%d':'int','%f':'float','%v':'vector'})[p.kind]||p.kind;
}

let CUR=null, STATE={};
function openEditor(ci){
  CUR = MODEL.commands[ci]; STATE={};
  CUR.params.forEach((p,i)=>{
    if(p.kind==='%t'){ const ts=MODEL.typesets[p.default];
      STATE[i] = ts && ts.entries.length ? ts.entries[0].name : ''; }
    else STATE[i]=p.default;
  });
  drawEditor();
  document.getElementById('dlgwrap').style.display='flex';
}

// Knopfbreiten am Bild gemessen - sie richten sich nach dem Text.
const BW = {'Expr!':33,'Helper':40,'Revert':40,'...':13,'Do':16,'Get':22,'Tag':22,'Rnd':22};
function bw(t){ return BW[t]||33; }
function buttonsFor(p){
  if(p.kind==='%t') return [];
  if(p.filter) return ['...','Do','Helper'];
  return ['Expr!'];
}
function hintFor(p){
  if(p.kind==='%t') return '<enum>';
  if(p.kind==='%e'||p.kind==='%op') return '<expr>';
  return ({'%s':'<str>','%d':'<int>','%f':'<float>','%v':'<vec>'})[p.kind]||'<?>';
}

function drawEditor(){
  const F = fieldsFor(CUR, STATE);
  const body=[];
  let x=G.left, maxRight=G.left;
  F.forEach((f,n)=>{
    const p=f.p, btns=buttonsFor(p);
    const pos=(xx,ww,yy,hh)=>`left:${PX(xx,'x')}px;top:${PX(yy,'y')}px;width:${PX(ww,'x')}px;height:${PX(hh,'y')}px`;
    if(p.kind==='%t'){
      const ts=MODEL.typesets[p.default]||{entries:[]};
      const opts=ts.entries.map(e=>`<option${e.name===STATE[f.idx]?' selected':''}>${e.name}</option>`).join('');
      body.push(`<select data-i="${f.idx}" style="${pos(x,G.fieldw,G.yfield,G.fieldh)}">${opts}</select>`);
    } else if(p.kind==='%op'){
      body.push(`<select data-i="${f.idx}" style="${pos(x,G.fieldw,G.yfield,G.fieldh)}">`+
        ['=','<','>','!'].map(o=>`<option>${o}</option>`).join('')+`</select>`);
    } else {
      const v=(p.default||'').replace(/"/g,'&quot;');
      body.push(`<input data-i="${f.idx}" value="${v}" style="${pos(x,G.fieldw,G.yfield,G.fieldh)};text-align:center"${p.locked?' disabled':''}>`);
    }
    let bx=x+G.fieldw;
    btns.forEach(t=>{ body.push(`<button class="btn" style="${pos(bx,bw(t),G.yfield,G.fieldh)}">${t}</button>`); bx+=bw(t); });
    body.push(`<div class="hint" style="${pos(x,G.fieldw,G.yhint,9)}">${hintFor(p)}</div>`);
    maxRight=bx;
    x = bx + G.gap;
  });
  // Befehlsbeschriftung, rechtsbuendig bis 91 dlu
  body.push(`<div class="lbl" style="left:0;top:${PX(G.yfield+3,'y')}px;width:${PX(G.labelend,'x')}px;text-align:right;font-weight:bold">${CUR.name.toUpperCase()}</div>`);
  // Hilfetext aus der .bhc bzw. dem //#-Kommentar des Kopfdatei-Eintrags
  let help = CUR.desc || '';
  const first = CUR.params[0];
  if(first && first.kind==='%t'){
    const e = entryOf(first.default, STATE[0]);
    if(e && e.desc) help = e.desc;
  }
  // Mindestbreite kommt von der Knopfzeile: 100 + 3*77 - 10 + 67 + 26.
  // Beleg: camera(ENABLE) hat nur ein Feld und ist trotzdem 345 dlu breit.
  const btnRight = G.left + 2*G.okpitch + G.okw;
  const wdlu = Math.max(maxRight, btnRight) + G.right;
  body.push(`<div class="lbl" style="left:0;top:${PX(G.yhelp,'y')}px;width:${PX(wdlu,'x')}px;text-align:center;font-weight:bold">( ${help||'no help comment available'} )</div>`);
  ['Ok','Cancel','Re-Evaluate'].forEach((t,i)=>{
    const dis = t==='Re-Evaluate' ? ' dis' : '';
    body.push(`<button class="btn${i===0?' def':''}${dis}" onclick="${i===0?'accept()':(i===1?'closeEditor()':'')}" `+
      `style="left:${PX(G.left+i*G.okpitch,'x')}px;top:${PX(G.ybtn,'y')}px;width:${PX(G.okw,'x')}px;height:${PX(G.okh,'y')}px">${t}</button>`);
  });
  const hdlu = G.ybtn + G.okh + 11;
  document.getElementById('dlg').style.width=PX(wdlu,'x')+6+'px';
  document.getElementById('dlgcap').textContent='Event editor';
  const bd=document.getElementById('dlgbody');
  bd.style.width=PX(wdlu,'x')+'px'; bd.style.height=PX(hdlu,'y')+'px';
  bd.innerHTML=body.join('');
  bd.querySelectorAll('select[data-i],input[data-i]').forEach(el=>{
    el.addEventListener('change',()=>{ STATE[el.dataset.i]=el.value;
      if(String(el.dataset.i).indexOf('s')!==0) drawEditor(); });
  });
  document.getElementById('dims').textContent = wdlu+'x'+hdlu+' dlu';
}

function closeEditor(){ document.getElementById('dlgwrap').style.display='none'; }

function accept(){
  const F=fieldsFor(CUR,STATE), vals=[];
  const inputs=[...document.querySelectorAll('#dlgbody select,#dlgbody input')];
  let vi=0;
  F.forEach(f=>{
    if(f.p.kind==='%v'){ vals.push('< '+[0,1,2].map(()=>inputs[vi++].value).join(' ')+' >'); }
    else { const v=inputs[vi++].value;
      vals.push(f.p.kind==='%s'||f.p.kind==='%t' ? '"'+v+'"' : v); }
  });
  const line = CUR.name+' ( '+vals.join(', ')+' )'+(CUR.block?' {':' ;');
  const t=document.getElementById('tree');
  const d=document.createElement('div'); d.className='row';
  d.innerHTML=`<img src="data:image/png;base64,${ICONS[CUR.icon]||ICONS.I_SPACE}"><span>${line}</span>`;
  t.appendChild(d); t.scrollTop=t.scrollHeight;
  closeEditor();
}
document.addEventListener('keydown',e=>{ if(e.key==='Escape') closeEditor(); });
"""


def build():
    lay = json.load(open(os.path.join(HERE, 'data/behaved_layout.json')))['dialogs']['102']
    ico = json.load(open(os.path.join(HERE, 'data/icons_b64.json')))
    model = json.load(open(os.path.join(HERE, 'data/model.json')))

    W, H = px(lay['cx'], 'x'), px(lay['cy'], 'y')
    parts = []
    for c in lay['ctrls']:
        if c['cls'] == 'BUTTON' and (c['style'] & 0xF) == 7:
            parts.append(f'<fieldset class="grp" style="left:{px(c["x"],"x")}px;'
                         f'top:{px(c["y"],"y")}px;width:{px(c["w"],"x")}px;'
                         f'height:{px(c["h"],"y")}px"><legend>'
                         f'{html.escape(c["text"])}</legend></fieldset>')
    for c in lay['ctrls']:
        pos = (f'left:{px(c["x"],"x")}px;top:{px(c["y"],"y")}px;'
               f'width:{px(c["w"],"x")}px;height:{px(c["h"],"y")}px')
        cls, st, txt = c['cls'], c['style'], html.escape(c['text'])
        if cls == 'BUTTON':
            bs = st & 0xF
            if bs == 7:
                continue
            dis = ' dis' if st & 0x08000000 else ''
            if bs in (2, 3):
                parts.append(f'<div class="chk{dis}" style="{pos}"><span class="box"></span>{txt}</div>')
            else:
                parts.append(f'<button class="btn{" def" if bs==1 else ""}{dis}" style="{pos}">{txt}</button>')
        elif cls == 'STATIC':
            parts.append(f'<div class="lbl" style="{pos}">{txt}</div>')
        elif cls == 'EDIT':
            parts.append(f'<div class="edit" style="{pos}"></div>')
        elif cls == 'SysTreeView32':
            parts.append(f'<div class="tree" id="tree" style="{pos}"></div>')
        elif cls == 'SysListView32':
            if c['id'] == 1011:
                rows = []
                for i, cm in enumerate(model['commands']):
                    icon = CMD_ICON.get(cm['name'], 'I_SPACE')
                    sig = ', '.join(p['kind'] for p in cm['params'])
                    rows.append(f'<div class="row" ondblclick="openEditor({i})" '
                                f'onclick="openEditor({i})" title="{html.escape(cm["desc"])}">'
                                f'<img src="data:image/png;base64,{ico.get(icon, ico["I_SPACE"])}">'
                                f'<span>{cm["name"]}<span style="opacity:.45"> ({sig})</span></span></div>')
                for m in model['macros']:
                    rows.append(f'<div class="row"><img src="data:image/png;base64,'
                                f'{ico["I_MACRO"]}"><span>{m}</span></div>')
                parts.append(f'<div class="list" style="{pos}">{"".join(rows)}</div>')
            else:
                n = sum(len(v['entries']) for v in model['typesets'].values())
                parts.append(f'<div class="list" style="{pos}">'
                             f'<div class="row"><span>behaved.bhc: {len(model["commands"])} '
                             f'Signaturen, {len(model["typesets"])} Typmengen, '
                             f'{len(model["macros"])} Makros</span></div>'
                             f'<div class="row"><span>Typmengen aufgeloest: {n} Eintraege '
                             f'aus 11 Kopfdateien</span></div>'
                             f'<div class="row"><span>Doppelklick links oeffnet den '
                             f'Event-Editor (Dialog 137, <span id="dims">539x58 dlu</span>)'
                             f'</span></div></div>')
        elif cls == 'msctls_progress32':
            parts.append(f'<div class="prog" style="{pos}"></div>')

    js = (JS.replace('__MODEL__', json.dumps(model))
            .replace('__ICONS__', json.dumps(ico)))
    doc = (f'<!DOCTYPE html><meta charset="utf-8"><title>BehavEd 2.0 — Nachbau</title>'
           f'<style>{CSS}</style>'
           f'<div id="win" style="width:{W+6}px"><div class="cap">'
           f'<b>BehavEd</b><span>_ □ ×</span></div>'
           f'<div id="client" style="width:{W}px;height:{H}px">{"".join(parts)}</div></div>'
           f'<div id="dlgwrap" onclick="if(event.target===this)closeEditor()">'
           f'<div id="dlg"><div class="cap"><b id="dlgcap">Event editor</b>'
           f'<span onclick="closeEditor()" style="cursor:default">×</span></div>'
           f'<div id="dlgbody"></div></div></div>'
           f'<script>{js}</script>')
    out = os.path.join(HERE, 'behaved_ui.html')
    open(out, 'w').write(doc)
    print(f"{out}: Hauptfenster {W}x{H} px, {len(model['commands'])} anklickbare Befehle")


build()
