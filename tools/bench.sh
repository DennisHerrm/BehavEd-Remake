#!/bin/sh
# Feste Messstellen für die Zeichnerarbeit.
#
# Warum es das gibt
# -----------------
# Zwei Runden hintereinander habe ich falsche Zahlen gemeldet, weil das
# Messwerkzeug etwas anderes tat als das Programm:
#
#   rc371  "116 ms für die Effekte" - gemessen mit einem ZWEITEN
#          renderMap-Aufruf. Der löscht das Bild und setzt den Tiefenpuffer
#          zurück (mapview.cpp:901), also wurde nichts verdeckt und jeder
#          Bildpunkt geschrieben.
#
#   danach "1,3 ms für die Effekte" - repariert, aber an zwei Kameras
#          gemessen, an denen die Effekte zufällig hinter Geometrie lagen
#          oder ausserhalb des Bildes. Genauso falsch, nur andersherum.
#
# Eine Messstelle, die man jedes Mal neu erfindet, misst jedes Mal etwas
# anderes. Deshalb stehen die Kameras hier fest, mit dem Befund daneben.
#
# Aufruf: bench.sh <refrender> <karte.bsp>

set -e
REF="${1:-/tmp/refrender}"
BSP="${2:-/home/claude/m3/maps/md_am_sith.bsp}"

# Immer den ZWEITEN Durchgang vergleichen. Der erste wärmt die Zwischenspeicher
# und ist um zwanzig bis dreissig Prozent langsamer - wer ihn mitmisst,
# vergleicht Rauschen.
messe() {
    name="$1"; shift
    ohne=$(NOCULLFX=1 OHNEFX=1 "$REF" "$BSP" /dev/null "$@" 2>&1 \
           | grep "Karte+Effekte" | sed 's/.*Effekte \([0-9.]*\) ms.*/\1/')
    mit=$(NOCULLFX=1 "$REF" "$BSP" /dev/null "$@" 2>&1 \
          | grep "Karte+Effekte" | sed 's/.*Effekte \([0-9.]*\) ms.*/\1/')
    printf '  %-28s Karte %8s ms   mit Effekten %8s ms\n' "$name" "$ohne" "$mit"
}

echo "== Zeichnerzeiten, md_am_sith =="

# Im Vulkanschacht, Kamera MITTEN in der Teilchenwolke. Der schlimmste Fall
# und der, um den es geht: gemessen 38 ms gegen 1752 ms, also Faktor 46 durch
# 1131 zusätzliche Dreiecke.
#
# Der Grund steht in der Fläche: diese 1131 Dreiecke decken im Mittel 71685
# Bildpunkte, das grösste volle 307200 - das ganze Bild. Das ist kein Fehler,
# sondern was passiert, wenn die Kamera in der Wolke steht. Genau hier muss
# eine Beschleunigung wirken, und genau hier fällt ein Fehler sofort auf.
messe "im Vulkanschacht" --pos 1360 2864 -700 --ang 20 0 --fxms 36800

# Danebenstehend, Wolke im Bild aber nicht um die Kamera herum.
messe "neben dem Vulkan" --pos 1744 2352 -700 --ang 20 0 --fxms 36800

# Weiter weg. Hier kosten die Effekte fast nichts - wichtig als Gegenprobe:
# eine Beschleunigung, die NUR hier misst, misst nichts.
messe "aus der Ferne" --pos -389 7103 220 --ang 0 90 --fxms 36800

echo
echo "Hinweis: die Effektkosten haengen extrem von der Kameralage ab."
echo "Eine einzelne Zahl ohne Kameraangabe ist wertlos."
