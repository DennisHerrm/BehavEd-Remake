#!/bin/sh
# Alle Pruefungen des Projekts.
set -e
cd "$(dirname "$0")/.."
SCRIPTS="${1:-/home/claude/work/jascripts}"
BHC="${2:-/mnt/user-data/uploads/behaved.bhc}"
HDR="${3:-/mnt/user-data/uploads}"
IBI="${4:-/mnt/user-data/uploads/start_cin2.IBI}"
SRC="$(ls src/*.cpp | tr '\n' ' ')"   # ALLE Quellen - die Liste von Hand zu pflegen hatte 19 Dateien verpasst (rc531)
FLAGS="-std=c++20 -O2 -Wall -Wextra -Werror -Iinclude -Ithird_party"

echo "== Uebersetzen =="
g++ $FLAGS $SRC tests/roundtrip.cpp -o roundtrip
g++ $FLAGS $SRC tests/checkall.cpp  -o checkall
g++ $FLAGS $SRC tests/checks.cpp    -o checks
g++ $FLAGS $SRC tests/blockmove.cpp -o blockmove
g++ $FLAGS $SRC tests/einzelkind.cpp -o einzelkind
g++ $FLAGS $SRC tests/kennungen.cpp -o kennungen
# MIT ASan - die Probe prueft unter anderem, dass die Zeilen der Makro-
# Kinder nicht auf freigegebenen Speicher zeigen. Ohne Sanitizer faellt
# genau das nicht auf.
g++ $FLAGS -fsanitize=address -g $SRC tests/makrozeilen.cpp -o makrozeilen
g++ $FLAGS $SRC tests/abweisung.cpp -o abweisung
g++ $FLAGS $SRC tests/sprache.cpp -o sprache
g++ $FLAGS tests/helferwerte.cpp -o helferwerte
g++ $FLAGS $SRC tests/treedump.cpp  -o treedump

echo "== Tastenkuerzel gegen ACCELERATOR 135 =="
g++ $FLAGS $SRC tests/keytest.cpp -o keytest
./keytest

echo "== Protokoll =="
g++ $FLAGS $SRC tests/diagtest.cpp -o diagtest
./diagtest

echo "== Karte =="
g++ $FLAGS $SRC tests/bsptest.cpp -o bsptest
./bsptest "${5:-/mnt/user-data/uploads/duel_kamino_lp.bsp}"

echo "== Klang =="
g++ $FLAGS $SRC tests/soundtest.cpp -o soundtest
./soundtest

echo "== Kartengeometrie =="
g++ $FLAGS $SRC tests/bspgeotest.cpp -o bspgeotest
./bspgeotest "${5:-/mnt/user-data/uploads/yavin2.bsp}"

echo "== Bilder und Shader =="
g++ $FLAGS $SRC tests/imagetest.cpp -o imagetest
./imagetest

echo "== Modelle =="
g++ $FLAGS $SRC tests/glmtest.cpp -o glmtest
./glmtest /mnt/user-data/uploads/model.glm /mnt/user-data/uploads/model_blue.skin

echo "== Skelett =="
g++ $FLAGS $SRC tests/glatest.cpp -o glatest
./glatest

echo "== Kartenansicht =="
g++ $FLAGS $SRC tests/mapviewtest.cpp -o mapviewtest
./mapviewtest "${5:-/mnt/user-data/uploads/yavin2.bsp}" /mnt/user-data/uploads/model.glm

echo "== Missionen =="
g++ $FLAGS $SRC tests/missiontest.cpp -o missiontest
./missiontest

echo "== Szene =="
g++ $FLAGS $SRC tests/scenetest.cpp -o scenetest
./scenetest data/fixtures/intro_jedi.txt data/fixtures/md_twj_jedi.ent

echo "== Kamerabahn =="
g++ $FLAGS $SRC tests/camtracktest.cpp -o camtracktest
./camtracktest data/fixtures/intro_jedi.txt data/fixtures/cin2_jedi.txt

echo "== Zeitleiste =="
g++ $FLAGS $SRC tests/timelinetest.cpp -o timelinetest
./timelinetest "$BHC" "$HDR" data/fixtures/*.txt

echo "== Archiv =="
g++ $FLAGS $SRC tests/pk3test.cpp -o pk3test
./pk3test "${7:-/tmp/test_scripts.pk3}"

echo "== Tabellen gegen die Original-Kopfdateien =="
g++ $FLAGS $SRC tests/tabellen.cpp -o tabellen
./tabellen "$BHC" "$HDR"

echo "== ICARUS-Bedeutungsregeln (Handbuch) =="
python3 tools/icarus_regeln.py 'data/fixtures/*.txt'

echo "== Gegenproben =="
./checks "$BHC" "$HDR" data/supplement.bhc
./blockmove
./einzelkind
./kennungen
./makrozeilen
./abweisung
./sprache
./helferwerte

echo "== Kennungen wandern NICHT in die Datei =="
g++ $FLAGS $SRC tests/rundlauf_kennung.cpp -o rundlauf_kennung
./rundlauf_kennung data/fixtures/*.txt

echo "== Bearbeitung, Rueckgaengig, REM =="
g++ $FLAGS $SRC tests/edittest.cpp -o edittest
./edittest "$SCRIPTS" "$BHC" "$HDR" data/supplement.bhc

echo "== Bearbeitungsrundgang ueber den Bestand =="
g++ $FLAGS $SRC tests/edittour.cpp -o edittour
./edittour "$SCRIPTS" "$BHC" "$HDR" data/supplement.bhc

echo "== Modell gegen das Bildschirmfoto =="
g++ $FLAGS $SRC tests/screenshot.cpp -o screenshot
./screenshot "$BHC" "$HDR" data/fixtures/*.txt

echo "== Rundlauf ueber den Raven-Bestand =="
./roundtrip "$SCRIPTS" || true

echo "== Modellpruefung =="
./checkall "$SCRIPTS" "$BHC" "$HDR" data/supplement.bhc

echo "== Fuzzer (kaputte Eingaben) =="
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -Iinclude $SRC tests/fuzz.cpp -o fuzz
./fuzz "$SCRIPTS" "$BHC" "$HDR" 5000

echo "== Bearbeitungs-Fuzzer =="
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -Iinclude $SRC tests/editfuzz.cpp -o editfuzz
./editfuzz "$SCRIPTS" "$BHC" "$HDR" 300

echo "== .ibi uebersetzen =="
g++ $FLAGS $SRC tests/ibitest.cpp -o ibitest
# "-" heisst: die eingebaute start_cin2.IBI. Ein gesetztes $IBI geht vor.
./ibitest "${IBI:--}" "$SCRIPTS" "$BHC" "$HDR" data/supplement.bhc

echo "== Oberflaeche gegen echtes ImGui =="
sh tools/check_gui.sh

echo "== Windows-exe bauen und Importtabelle pruefen =="
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 && [ -f "${IMGUI:-$HOME/imgui}/imgui.cpp" ]; then
  IM="${IMGUI:-$HOME/imgui}"
  # ImGui ohne unsere Warnungspolitik: GCC meldet dort falsch-positive
  # -Warray-bounds, und -Werror wuerde daran scheitern.
  IMDEF='-DIMGUI_USER_CONFIG="imgui_config.h"'
  IMINC="-Iinclude -Igui -I$IM -I$IM/backends"
  for f in "$IM"/imgui.cpp "$IM"/imgui_draw.cpp "$IM"/imgui_tables.cpp \
           "$IM"/imgui_widgets.cpp "$IM"/backends/imgui_impl_win32.cpp \
           "$IM"/backends/imgui_impl_opengl3.cpp; do
    x86_64-w64-mingw32-g++ -std=c++20 -O2 -w $IMINC "$IMDEF" -c "$f" \
      -o "/tmp/imgui_$(basename "$f" .cpp).o"
  done
  x86_64-w64-mingw32-g++ -std=c++20 -O2 -Wall -Wextra -Werror \
    $IMINC "$IMDEF" $SRC gui/*.cpp /tmp/imgui_*.o "$RESOBJ" \
    -o behaved.exe -municode -mwindows -static -lopengl32 -lgdi32 -limm32 -ldwmapi -lcomdlg32 -lole32 -lshell32 -luuid -lversion -lwinmm
  python3 tools/check_exe_imports.py behaved.exe
python3 tools/check_exe_resources.py behaved.exe
else
  echo "  uebersprungen (MinGW oder ImGui fehlt)"
fi

echo "== Pruefer =="
python3 tools/lint_sources.py
python3 tools/lint_headers.py
python3 tools/lint_imgui_context.py
python3 tools/lint_imgui_alpha.py
python3 tools/lint_i18n.py

# Passen die ERZEUGTEN i18n-Dateien noch zu ihrem Erzeuger?
#
# rc536: vierundzwanzig Eintraege standen nur in include/bhed/i18n.h und
# src/i18n.cpp, nicht in tools/gen_i18n.py. Beim naechsten Lauf des
# Erzeugers waren sie weg, und MSVC brach mit 24x C2065 ab. Hier faellt es
# nicht auf, weil gui/app.cpp ohne ImGui gar nicht uebersetzt wird.
python3 tools/lint_i18n_erzeugt.py
python3 tools/lint_i18n_erzeugt.py --gegentest

# Startet ein Pruefwerkzeug ein fremdes Programm ungeschuetzt?
#
# rc548 brach auf shanks Windows-Rechner mit einem Python-Rueckverfolg ab,
# weil lint_headers.py `g++` rief, das es dort nicht gibt.
python3 tools/lint_fremdprogramm.py
python3 tools/lint_fremdprogramm.py --gegentest

# Gibt es jedes benutzte g_app->Feld auch wirklich?
#
# gui/app.cpp wird hier ohne ImGui gar nicht uebersetzt. Ein vertippter
# Feldname faellt deshalb erst bei shank unter MSVC auf - eine Runde
# spaeter. Das ist derselbe blinde Fleck, der rc536 gekostet hat.
python3 tools/lint_gapp.py
python3 tools/lint_gapp.py --gegentest
python3 tools/lint_portability.py
# Das Vergleichswerkzeug fuer den GPU-Umbau prueft sich selbst - siehe
# UMBAU-GPU.md. Ein Vergleicher, der nicht geprueft ist, sagt "innerhalb der
# Toleranz" und niemand sieht nach.
python3 tools/lint_bildvergleich.py
# Jede Einstellung muss geschrieben UND gelesen werden - sonst geht sie bei
# jedem Start verloren, ohne dass es irgendwo auffaellt.
python3 tools/lint_settings.py

# Die Feldlagen der cbuffer nach den HLSL-Regeln. Sie liegen NICHT
# fortlaufend, und wer fortlaufend fuellt, bekommt ein Bild, das falsch
# aussieht, ohne abzustuerzen - siehe rc402 (Himmel aus radialen Strahlen).
python3 tools/lint_cbuffer.py

# Felder, deren Anfangsbelegung nur das erste Element trifft.
# rc530: `float g_messWert[kAbschnitte] = {-1.0F};` belegte NUR
# Element 0 mit -1; sieben Abschnitte meldeten 0.000 ms, obwohl
# nie gemessen wurde. Kein Uebersetzer warnt davor.
python3 tools/lint_feldinit.py
python3 tools/lint_feldinit.py --gegentest

# Hinweis: dieses Skript uebersetzt jedes Mal aus den Quellen (siehe SRC
# oben) und hat deshalb nie einen veralteten Stand geprueft.
#
# Zweimal in dieser Entwicklung hat `t_checks` trotzdem einen Fehler
# gemeldet, den es nicht gab - beide Male in einem BEHELFSBAU, in dem eine
# alte Objektdatei liegengeblieben war. Beim ersten Mal habe ich eine ganze
# Runde damit verbracht, die Uebersetzungstabelle auseinanderzunehmen: 388
# Eintraege, 388 Aufzaehlungswerte, Reihenfolge gleich, keiner doppelt. Es
# war nichts falsch daran.
#
# Wer die Proben von Hand baut, baut die geaenderte Quelle zuerst neu.


echo "== liest die Engine jeden Block auf? =="
g++ $FLAGS $SRC tests/enginecheck.cpp -o enginecheck
./enginecheck "${6:-/home/claude/work/story}"

echo "== .ibi gegen IBIZE.EXE =="
g++ $FLAGS $SRC tests/ibiround.cpp -o ibiround
./ibiround "${6:-/home/claude/work/story}" "$BHC" "$HDR" data/supplement.bhc

echo "== Erzeuger =="
python3 tools/gen_i18n.py >/dev/null
python3 tools/gen_icons.py
python3 tools/gen_model.py >/dev/null
python3 tools/gen_ui.py    >/dev/null
echo "ok"
