#!/bin/sh
# Uebersetzt die Oberflaechendateien gegen ECHTES Dear ImGui.
# Ohne das faellt jeder Schnittstellenwechsel von ImGui erst beim Bauen
# unter Windows auf. ImGui wird nach $IMGUI erwartet (Vorgabe ../imgui).
set -e
cd "$(dirname "$0")/.."
IMGUI="${IMGUI:-$HOME/imgui}"
if [ ! -f "$IMGUI/imgui.h" ]; then
  echo "ImGui nicht gefunden in $IMGUI  (IMGUI=<pfad> setzen)"; exit 2
fi
echo "ImGui: $(grep -m1 'define IMGUI_VERSION ' "$IMGUI/imgui.h" | cut -d'"' -f2)"
INC="-Iinclude -Ithird_party -Igui -I$IMGUI -I$IMGUI/backends"
DEF='-DIMGUI_USER_CONFIG="imgui_config.h"'

# Plattformfreie Dateien: mit dem hiesigen Uebersetzer
# gpumap_win32.cpp ist auch dabei: ohne BHED_WITH_D3D11 uebersetzt sie ihren
# Rueckfallzweig, und der soll nicht unbemerkt brechen. Der D3D-Zweig selbst
# laesst sich hier nicht pruefen - das steht im Kopf der Datei.
# --- Der Direct3D-Zweig, syntaktisch --------------------------------------
#
# Er liess sich hier lange gar nicht pruefen. Die Folge stand in einem
# Bauprotokoll von shank:
#
#     error C2065: "ecken": nichtdeklarierter Bezeichner
#
# Eine Variable im falschen Block - ein Fehler, den jeder Compiler in einer
# Sekunde findet, der aber erst auf einem fremden Rechner auffiel.
#
# tools/d3dstub enthaelt gerade so viel Direct3D, dass der Quelltext geprueft
# werden kann: Namen, Argumentzahlen, Sichtbarkeit. NICHT, ob die Aufrufe
# richtig benutzt werden - ein falsches Bild findet das hier nicht.
echo "== gpumap_win32.cpp mit dem D3D-Zweig (nur Syntax) =="
if g++ -std=c++20 -fsyntax-only -DBHED_WITH_D3D11 \
       -Itools/d3dstub -Iinclude -Ithird_party -Igui \
       gui/gpumap_win32.cpp; then
  echo "  D3D-Zweig syntaktisch ok"
  # Und binden: eine Erklaerung im Kopf mit anderer Parameterliste als die
  # Definition ist erst fuer den BINDER ein Fehler, nicht fuer den
  # Uebersetzer. Der lief bisher nur auf shanks Rechner, und zweimal hat ihn
  # das einen Bau gekostet (LNK2019).
  if g++ -std=c++20 -DBHED_WITH_D3D11 -Itools/d3dstub -Iinclude \
         -Ithird_party -Igui tools/d3dstub/linkcheck.cpp \
         gui/gpumap_win32.cpp tools/d3dstub/backendstub.cpp $(ls src/*.cpp) \
         -pthread -o /tmp/bhed_linkcheck 2>/tmp/bhed_linkcheck.err; then
    echo "  D3D-Zweig bindet"
  else
    echo "  D3D-ZWEIG BINDET NICHT:"
    grep -E "undefined reference|error" /tmp/bhed_linkcheck.err | head -5
    exit 1
  fi
else
  echo "  D3D-ZWEIG FEHLERHAFT"
  exit 1
fi

for f in gui/app.cpp gui/app_view3d.cpp gui/gpumap_win32.cpp; do
  g++ -std=c++20 -fsyntax-only -Wall -Wextra -Werror $INC "$DEF" "$f"
  echo "  $f uebersetzt (Linux)"
done

# Windows-Dateien: nur mit dem Kreuzuebersetzer, sonst fehlen windows.h,
# d3d11.h und die Rueckwaertskompatibilitaet zu aelteren Windows-Fassungen.
CROSS="${CROSS:-x86_64-w64-mingw32-g++}"
if command -v "$CROSS" >/dev/null 2>&1; then
  for f in gui/backend_win32.cpp gui/main_win32.cpp gui/platform_win32.cpp gui/app.cpp gui/app_view3d.cpp; do
    "$CROSS" -std=c++20 -fsyntax-only -Wall -Wextra -Werror $INC "$DEF" "$f"
    echo "  $f uebersetzt (Windows)"
  done
else
  echo "  $CROSS nicht vorhanden - Windows-Dateien nicht geprueft"
fi

# --- Reihenfolge der Tastenbehandlung ------------------------------------
#
# Baut den Bildablauf gegen echtes ImGui nach und prueft, dass eine
# Eingabetaste einen Dialog nicht zugleich schliesst und wieder oeffnet.
# Braucht keinen Bildschirm.
echo "== Tastenreihenfolge =="
if [ -f "$IMGUI/imgui.cpp" ]; then
  g++ -std=c++20 -O1 -I"$IMGUI" tools/check_keyorder.cpp \
    "$IMGUI/imgui.cpp" "$IMGUI/imgui_draw.cpp" \
    "$IMGUI/imgui_tables.cpp" "$IMGUI/imgui_widgets.cpp" -o /tmp/bhed_keyorder
  /tmp/bhed_keyorder | tail -1
  rm -f /tmp/bhed_keyorder
else
  echo "  (uebersprungen - ImGui-Quelltext nicht vorhanden)"
fi

# --- Spalten der Baumansicht ---------------------------------------------
#
# Misst im laufenden ImGui nach, wo Symbol, Name und Klammer landen, und
# haelt sie auf den Werten des Originals fest.
echo "== Spalten der Baumansicht =="
if [ -f "$IMGUI/imgui.cpp" ]; then
  g++ -std=c++20 -O1 -Iinclude -I"$IMGUI" tools/check_treecols.cpp \
    src/tree.cpp src/script.cpp src/bhc.cpp src/diag.cpp \
    "$IMGUI/imgui.cpp" "$IMGUI/imgui_draw.cpp" \
    "$IMGUI/imgui_tables.cpp" "$IMGUI/imgui_widgets.cpp" -o /tmp/bhed_treecols
  /tmp/bhed_treecols | tail -1
  rm -f /tmp/bhed_treecols
else
  echo "  (uebersprungen - ImGui-Quelltext nicht vorhanden)"
fi

# --- Wirklich BINDEN, nicht nur uebersetzen -------------------------------
#
# Anlass: nach der Aufteilung von app.cpp standen vier Funktionen im anonymen
# Namensraum, waren aber in app_internal.h deklariert. Ein anonymer
# Namensraum ist je Uebersetzungseinheit ein eigener - die Definition lag
# also woanders als versprochen.
#
# Mit -fsyntax-only faellt das NICHT auf. Es faellt erst beim Binden auf, und
# das passierte hier zweimal erst nach dem Packen. Deshalb bindet diese
# Pruefung jetzt wirklich.
echo "== Binden =="
if [ -n "$IMGUI" ] && [ -f /tmp/imgui_imgui.o ]; then
  # -DBHED_WITH_D3D11 ist keine Feinheit: OHNE das Wort bindet dieser
  # Schritt den RUECKFALLZWEIG von gpumap_win32.cpp und nicht die 1837
  # Zeilen Direct3D. Er meldete dann "gebunden: ok" fuer Quelltext, den er
  # gar nicht angefasst hatte. Aufgefallen bei der Gegenpruefung zu rc430.
  x86_64-w64-mingw32-g++ -std=c++20 -O1 -Iinclude -Ithird_party -Igui \
    -I"$IMGUI" -I"$IMGUI/backends" -DIMGUI_USER_CONFIG='"imgui_config.h"' \
    -DBHED_WITH_D3D11 -DNOMINMAX \
    src/*.cpp gui/*.cpp /tmp/imgui_*.o -o /tmp/linkcheck.exe \
    -municode -mwindows -static -ld3d11 -ld3dcompiler -ldxgi \
    -lopengl32 -lgdi32 -limm32 -ldwmapi \
    -lcomdlg32 -lole32 -lshell32 -luuid -lversion -lwinmm 2>&1 | head -5
  if [ -f /tmp/linkcheck.exe ]; then
    echo "  gebunden: ok"
    rm -f /tmp/linkcheck.exe
  else
    echo "  BINDEN FEHLGESCHLAGEN"
    exit 1
  fi
else
  # Die alte Meldung sagte "IMGUI nicht gesetzt" - auch dann, wenn IMGUI
  # gesetzt war und nur die ImGui-Objektdateien fehlten. Eine Auskunft, die
  # den falschen Grund nennt, kostet die naechste Runde.
  if [ -z "$IMGUI" ]; then
    echo "  (uebersprungen - IMGUI nicht gesetzt)"
  else
    echo "  (uebersprungen - /tmp/imgui_*.o fehlen; siehe BAUEN.md)"
  fi
fi
