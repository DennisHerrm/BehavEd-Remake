#!/usr/bin/env python3
"""
Sucht Aufrufe von ImGui-Schnittstellen, die dort selbst als vorlaeufig
gekennzeichnet sind.

Anlass: die Symbole liefen zuerst ueber ImFontAtlas::AddCustomRect. In
imgui.h steht darueber "[ALPHA] Custom Rectangles/Glyphs API", und zwei
Eigenheiten sind toedlich:
  * GetCustomRect() dereferenziert TexData ungeprueft
  * io.Fonts->Clear() macht alle Kennungen ungueltig, ohne Hinweis
Ergebnis war ein Absturz im ersten Bild, 0xC0000005 lesend an 0x8.

Wer so etwas benutzt, soll es wenigstens bewusst tun. Eine Ausnahme wird
mit einem Kommentar "// imgui-alpha: <Begruendung>" in derselben Zeile
oder der Zeile davor erlaubt.
"""
import pathlib, re, sys

ALPHA = ['AddCustomRect', 'RemoveCustomRect', 'GetCustomRect',
         'AddCustomRectFontGlyph', 'TexPixelsUseColors']

root = pathlib.Path(__file__).resolve().parent.parent
bad = 0
for f in sorted((root / 'gui').glob('*.cpp')) + sorted((root / 'gui').glob('*.h')):
    lines = f.read_text(encoding='utf-8').split('\n')
    for i, ln in enumerate(lines):
        if ln.lstrip().startswith('//'):
            continue
        for name in ALPHA:
            if re.search(r'\b' + name + r'\s*\(', ln) or (name == 'TexPixelsUseColors'
                                                          and name in ln):
                erlaubt = 'imgui-alpha:' in ln or (i > 0 and 'imgui-alpha:' in lines[i-1])
                if not erlaubt:
                    print(f"{f.relative_to(root)}:{i+1}: vorlaeufige ImGui-Schnittstelle "
                          f"{name} ohne Begruendung")
                    bad += 1

print("ImGui-Alpha-Pruefung:", "ok" if bad == 0 else f"{bad} Beanstandung(en)")
sys.exit(1 if bad else 0)
