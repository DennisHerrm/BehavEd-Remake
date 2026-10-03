cmake_minimum_required(VERSION 3.20)
project(behaved VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(bhed_core STATIC
  src/script.cpp
  src/update.cpp
  src/bhc.cpp
  src/validate.cpp
  src/tree.cpp
  src/edit.cpp
  src/ibi.cpp
  src/i18n.cpp
  src/theme.cpp
  src/settings.cpp
  src/keys.cpp
  src/diag.cpp
  src/bsp.cpp
  src/inflate.cpp
  src/pk3.cpp
  src/sound.cpp
  src/timeline.cpp
  src/bspgeo.cpp
  src/gpucam.cpp
  src/gpuconst.cpp
  src/gpudraw.cpp
  src/gpushader.cpp
  src/gpuskin.cpp
  src/gpustate.cpp
  src/mapview.cpp
  src/image.cpp
  src/interplay.cpp
  src/image_jpeg.cpp
  src/image_png.cpp
  src/shaderscript.cpp
  src/glm.cpp
  src/gla.cpp
  src/camtrack.cpp
  src/scene.cpp
  # Waffe und Lichtschwerter einer Figur ueber die Zeit (SET_WEAPON,
  # SET_SABER*, Handmodelle). Siehe den Kopf der Datei.
  src/ausruestung.cpp
  src/mover.cpp
  # EINE Uhr fuer Kamerabahn, Zeitleiste und Szene. Warum, steht in
  # bhed/clock.h.
  src/clock.cpp
  # Der ICARUS-Nachbau: alle Skripte einer Zwischensequenz Bild fuer Bild
  # ausfuehren (run, affect, do/dowait, use, Spawner). Siehe bhed/ablauf.h.
  src/ablauf.cpp
  # ROFF-Bahnen (*.rof) fuer play ( "PLAY_ROFF", ... ): lesen wie
  # G_LoadRoff, abspielen wie G_Roff. Siehe bhed/roff.h.
  src/roff.cpp
  # Zahlen aus Textdateien lesen, ohne Ausnahmen. EINE Stelle, die weiss,
  # dass std::from_chars keine fuehrenden Leerzeichen ueberspringt.
  src/num.cpp
  # .md3-Modelle: Konsolen, Kisten, Rohre, Raumschiffe. 1549 Entities in den
  # Karten der Mod zeigen darauf.
  src/md3.cpp
  # Die Wellenformen der Shader: deformVertexes wave, rgbGen wave.
  src/wave.cpp
  # Effekte. Der Leser und die Simulation stammen aus efxed und sind dort
  # gegen FxScheduler.cpp und FxPrimitives.cpp gebaut; hier kommt nur das
  # Zeichnen dazu (src/efxdraw.cpp).
  src/efx_gp2.cpp
  src/efx_effect.cpp
  src/efx_read.cpp
  src/efx_sim.cpp
  src/efx_curve.cpp
  src/efxdraw.cpp
  src/mission.cpp)
target_include_directories(bhed_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# third_party als SYSTEM: dann schweigt der Uebersetzer ueber fremden Code.
#
# minimp3.h erzeugt bei /W4 ein Dutzend C4244 ("moeglicher Datenverlust").
# Die sind nicht falsch, aber wir koennen sie nicht beheben - es ist fremder
# Code, und ein Eingriff ginge beim naechsten Abgleich verloren. Warnungen,
# die niemand beheben kann, verdecken nur die eigenen.
target_include_directories(bhed_core SYSTEM PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

if(MSVC)
  target_compile_options(bhed_core PUBLIC /W4 /permissive- /utf-8 /Zc:__cplusplus)
  target_compile_definitions(bhed_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)

  # Ausweg fuer einen INTERNEN COMPILERFEHLER.
  #
  # MSVC 14.50 (Visual Studio 18) stuerzt bei manchen Rechnern in der
  # Codeerzeugung ab - "INTERNER COMPILERFEHLER in CL.exe", Rueckgabe
  # -1073741819, das ist eine Zugriffsverletzung IM UEBERSETZER. Nicht unser
  # Code ist falsch; der Uebersetzer kommt mit ihm nicht zurecht.
  #
  # Zwei Stellschrauben, die in solchen Faellen fast immer helfen:
  #
  #   /Ob1   weniger Einbetten von Funktionen. Die Codeerzeugung arbeitet
  #          dann mit kleineren Einheiten, und genau dort stuerzt sie ab.
  #   /MP-   jede Datei einzeln statt in Buendeln. Das Buendel war hier
  #          image_png + shaderscript + glm + gla + camtrack + scene +
  #          mission; einzeln uebersetzt faellt oft auf, WELCHE Datei es
  #          ist.
  #
  # Kostet etwas Geschwindigkeit im fertigen Programm - aber ein Programm,
  # das sich nicht bauen laesst, ist unendlich langsam.
  option(BEHAVED_MSVC_SAFE_CODEGEN
         "Vorsichtige Codeerzeugung - Ausweg bei internem Compilerfehler" OFF)
  if(BEHAVED_MSVC_SAFE_CODEGEN)
    target_compile_options(bhed_core PRIVATE /Ob1)
    message(STATUS "Vorsichtige Codeerzeugung: /Ob1")
  endif()
else()
  # -Wshadow dazu.
  #
  # MSVC meldet Verdeckungen seit jeher als C4456 und hatte bei /W4 sechs
  # Stellen gefunden, die hier stumm durchliefen - GCC macht das nur mit
  # -Wshadow, und das war nicht eingeschaltet. Ein Bau, der weniger findet
  # als der andere, ist der schlechtere Bau.
  #
  # Eine verdeckte Variable ist selten sofort ein Fehler, aber sie ist
  # immer eine Falle: wer spaeter die aeussere meint und die innere
  # erwischt, sucht lange.
  target_compile_options(bhed_core PUBLIC -Wall -Wextra -Wshadow)
endif()

# Werkzeug
add_executable(bhed tools/bhed_cli.cpp)
target_link_libraries(bhed PRIVATE bhed_core)

# Befehlsmodell neben die .exe legen, damit bhed ohne --data laeuft
add_custom_command(TARGET bhed POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/data/base
          $<TARGET_FILE_DIR:bhed>/data/base)

# --- Oberflaeche -------------------------------------------------------
#
# Dear ImGui 1.92 oder neuer wird erwartet. Nicht mitgeliefert:
#   git clone --depth 1 -b v1.92.9b-docking https://github.com/ocornut/imgui.git
set(IMGUI_DIR "" CACHE PATH "Pfad zu Dear ImGui (>= 1.92)")

if(WIN32 AND IMGUI_DIR AND EXISTS "${IMGUI_DIR}/imgui.cpp")
  # Direct3D 11 nur, wenn verzoegertes Laden moeglich ist.
  #
  # Fest gebunden stuenden d3d11.dll und d3dcompiler_47.dll in der
  # Importtabelle - Windows laedt das Programm dann gar nicht, wenn eine
  # davon fehlt, und der Rueckfall auf OpenGL kaeme nie zum Zug. MSVC kann
  # /DELAYLOAD, MinGW nicht; dort wird nur OpenGL gebaut.
  option(BEHAVED_D3D11 "Direct3D 11 mitbauen (braucht /DELAYLOAD)" ${MSVC})

  # Dear ImGui als eigenes Ziel, OHNE unsere Warnungspolitik.
  #
  # Mit -Werror scheitert der Bau an ImGuis eigenen Quellen: GCC meldet dort
  # falsch-positive -Warray-bounds in DockNodeTreeUpdatePosSize und
  # IsMouseDragging. Fremdcode gehoert nicht unter unsere Regeln - wir
  # koennen ihn nicht aendern, und ein abgeschalteter -Werror im ganzen
  # Projekt waere der falsche Preis dafuer.
  add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    $<$<BOOL:${BEHAVED_D3D11}>:${IMGUI_DIR}/backends/imgui_impl_dx11.cpp>)
  target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends
                             ${CMAKE_CURRENT_SOURCE_DIR}/gui)
  target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="imgui_config.h")
  if(MSVC)
    target_compile_options(imgui PRIVATE /W0)
  else()
    target_compile_options(imgui PRIVATE -w)
  endif()

  add_executable(behaved WIN32
    gui/app.cpp
    gui/app_view3d.cpp
    gui/backend_win32.cpp
    gui/gpumap_win32.cpp
    gui/main_win32.cpp
    gui/platform_win32.cpp
    gui/audio_win32.cpp
    gui/icons_gen.cpp
    gui/selbsttest.cpp
    gui/update_win32.cpp
    gui/vektoricons.cpp
    # Symbol, Versionsangaben und Manifest. Ohne das zeigt der Explorer das
    # Standardsymbol, und Rechtsklick -> Details bleibt leer.
    res/behaved.rc)
  # IM_ASSERT wird ueber gui/imgui_config.h umgeleitet (IMGUI_USER_CONFIG am
  # Ziel imgui), damit eine ImGui-Zusicherung im Protokoll landet und nicht
  # nur im Meldungskasten der Laufzeitbibliothek.
  # MSVC erzeugt selbst ein Manifest und legt es als Ressource 1 ab. Unseres
  # steht schon in res/behaved.rc - dann bricht der Ressourcenwandler ab:
  #     CVT1100: Doppelte Ressource. type:MANIFEST, name:1
  #     LNK1123: Fehler bei der Konvertierung in COFF
  # MinGW macht das nicht, deshalb faellt es erst beim Bau mit Visual Studio
  # auf. /MANIFEST:NO schaltet das erzeugte ab; unseres bleibt und ist das
  # bessere - es nennt die unterstuetzten Windows-Fassungen und die Codepage.
  if(MSVC)
    # target_link_options, nicht set_target_properties(LINK_FLAGS): die
    # Eigenschaft ist eine einzige Zeichenkette, und wer sie spaeter noch
    # einmal setzt, ueberschreibt alles davor. Die /DELAYLOAD-Angaben weiter
    # unten laufen ebenfalls ueber target_link_options und wuerden dabei
    # lautlos verschwinden.
    target_link_options(behaved PRIVATE /MANIFEST:NO)
    # Der Selbsttest ist eine einzige grosse Datei voller Lambdas - mehr als
    # die 65536 Abschnitte einer normalen Objektdatei (C1128).
    set_source_files_properties(gui/selbsttest.cpp PROPERTIES COMPILE_OPTIONS /bigobj)
  endif()

  target_link_libraries(behaved PRIVATE bhed_core imgui opengl32 gdi32 imm32 dwmapi comdlg32 ole32 shell32 uuid version winmm winhttp)

  if(BEHAVED_D3D11)
    target_compile_definitions(behaved PRIVATE BHED_WITH_D3D11)
    target_link_libraries(behaved PRIVATE d3d11 d3dcompiler)
    if(MSVC)
      target_link_options(behaved PRIVATE
        # dxgi.dll steht bewusst NICHT dabei: wir rufen nichts daraus direkt
        # auf, und MSVC meldet dann LNK4199 "wird ignoriert; keine Importe
        # von dxgi.dll gefunden".
        /DELAYLOAD:d3d11.dll /DELAYLOAD:d3dcompiler_47.dll)
      target_link_libraries(behaved PRIVATE delayimp)
    endif()
  endif()

  if(MSVC)
    target_link_options(behaved PRIVATE /ENTRY:wWinMainCRTStartup)
  else()
    target_link_options(behaved PRIVATE -municode)
  endif()

  add_custom_command(TARGET behaved POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/data/base
            $<TARGET_FILE_DIR:behaved>/data/base)
elseif(WIN32)
  message(STATUS "Oberflaeche wird nicht gebaut: -DIMGUI_DIR=<pfad> setzen")
endif()

# --- Pruefstaende ------------------------------------------------------
option(BEHAVED_TESTS "Pruefstaende bauen" ON)
if(BEHAVED_TESTS)
  enable_testing()
  set(BEHAVED_DATA "${CMAKE_CURRENT_SOURCE_DIR}/data/base")
  set(BEHAVED_BHC  "${BEHAVED_DATA}/behaved.bhc")
  set(BEHAVED_SUPP "${BEHAVED_DATA}/supplement.bhc")
  # Ordner mit .icarus-Skripten. Ohne ihn laufen nur die Tests, die keinen
  # brauchen. Setzen mit:  cmake -DBEHAVED_SCRIPTS=C:/pfad/zu/JAscripts
  set(BEHAVED_SCRIPTS "" CACHE PATH "Ordner mit .icarus-Skripten (JAscripts)")
  set(BEHAVED_IBI "" CACHE FILEPATH "Eine echte .ibi-Datei zum Vergleich")
  set(BEHAVED_IBIDIR "" CACHE PATH "Ordner mit echten .ibi von IBIZE.EXE")
  if(BEHAVED_IBIDIR)
    add_test(NAME engine_liest_auf
             COMMAND enginecheck ${BEHAVED_IBIDIR})
    add_test(NAME ibi_gegen_ibize
             COMMAND ibiround ${BEHAVED_IBIDIR} ${BEHAVED_BHC} ${BEHAVED_DATA} ${BEHAVED_SUPP})
  endif()

  # blockmove, einzelkind, kennungen und tabellen liefen bisher NUR ueber
  # tools/verify.sh. Wer mit CMake baut, hat sie nie gesehen - genau die
  # Sorte Luecke, die tools/verify.sh bei den Quelldateien hatte (rc531).
  foreach(t checks keytest diagtest bsptest bspgeotest mapviewtest imagetest glmtest glatest camtracktest scenetest missiontest pk3test soundtest timelinetest enginecheck screenshot roundtrip checkall edittest edittour treedump fuzz editfuzz ibitest ibiround shaderabgleich)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE bhed_core)
  endforeach()

  foreach(t blockmove einzelkind kennungen abweisung sprache makrozeilen rundlauf_kennung tabellen)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE bhed_core)
  endforeach()
  # Die Proben des GPU-Wegs und des Gluehens. Sie lagen in tests/, wurden
  # aber nicht gebaut - Aenderungen an Konstantenlagen, Shadertext und
  # Zeichenliste liefen damit ungeprueft durch (rc568, gDeformSpread).
  foreach(t gpuconst gpudraw gpushader gpustate gpucam gpuskin efxtest ersetzen mover)
    add_executable(${t}_probe tests/${t}.cpp)
    target_link_libraries(${t}_probe PRIVATE bhed_core)
    add_test(NAME ${t} COMMAND ${t}_probe)
  endforeach()
  # Alle Modelle und Karten der Spielordner laden und pruefen (lange Probe).
  # Ohne Ordner meldet sie "uebersprungen". Setzen mit:
  #   cmake -DBEHAVED_GAMEDIRS="C:/.../base;C:/.../MD"
  add_executable(meshmasse tests/meshmasse.cpp)
  target_link_libraries(meshmasse PRIVATE bhed_core)
  set(BEHAVED_GAMEDIRS "" CACHE STRING "Spielordner fuer meshmasse, mit ; getrennt")
  if(BEHAVED_GAMEDIRS)
    add_test(NAME netze COMMAND meshmasse ${BEHAVED_GAMEDIRS})
  endif()
  # Der ICARUS-Nachbau (bhed/ablauf.h): Takt, do/dowait, affect, INSERT,
  # Signale, use auf Skriptstarter und Spawner.
  # Waffe und Lichtschwerter (src/ausruestung.cpp): SET_WEAPON, SET_SABER*,
  # Klingen, Farben, Handmodelle. Mit drei Argumenten (.bhc, Kopfdateien,
  # Ordner mit ausgepackten .pk3) zaehlt sie zusaetzlich die echten Skripte.
  add_executable(waffentest tests/waffentest.cpp)
  target_link_libraries(waffentest PRIVATE bhed_core)
  add_test(NAME waffen COMMAND waffentest)
  # Auto-Updater ohne Netz: JSON von GitHub, Fassungen, Zielpfade im .zip.
  add_executable(updatetest tests/updatetest.cpp)
  target_link_libraries(updatetest PRIVATE bhed_core)
  add_test(NAME update COMMAND updatetest)
  # Gegenproben zur Code-Pruefung vom 03.10.2026 (Skriptkern, Kartenleser,
  # Einstellungen, Modell- und Archivleser).
  add_executable(pruefung tests/pruefung.cpp)
  target_link_libraries(pruefung PRIVATE bhed_core)
  if(WIN32)
    target_link_libraries(pruefung PRIVATE psapi)
  endif()
  add_test(NAME pruefung COMMAND pruefung)
  add_executable(ablauftest tests/ablauftest.cpp)
  target_link_libraries(ablauftest PRIVATE bhed_core)
  add_test(NAME ablauf COMMAND ablauftest)
  # ROFF-Bahnen (bhed/roff.h): kleine Dateien im Speicher, beide Fassungen,
  # Zeitplan, Mover- und Figurenweg, Notizen, kaputte Dateien.
  add_executable(rofftest tests/rofftest.cpp)
  target_link_libraries(rofftest PRIVATE bhed_core)
  add_test(NAME roff COMMAND rofftest)
  # Jede .rof der Spielordner lesen und abspielen. Ohne BEHAVED_GAMEDIRS
  # meldet sie "uebersprungen" (wie meshmasse).
  add_executable(roffmasse tests/roffmasse.cpp)
  target_link_libraries(roffmasse PRIVATE bhed_core)
  add_test(NAME roffs COMMAND roffmasse ${BEHAVED_GAMEDIRS})
  add_test(NAME blockbewegung COMMAND blockmove)
  add_test(NAME einzelkind COMMAND einzelkind)
  add_test(NAME kennungen COMMAND kennungen)
  add_test(NAME abweisung COMMAND abweisung)
  add_test(NAME makrozeilen COMMAND makrozeilen)
  add_test(NAME sprache COMMAND sprache)
  add_executable(helferwerte tests/helferwerte.cpp)
  add_test(NAME helferwerte COMMAND helferwerte)
  add_test(NAME rundlauf_kennung COMMAND rundlauf_kennung
           ${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/intro_jedi.txt)
  add_test(NAME tabellen COMMAND tabellen ${BEHAVED_BHC} ${BEHAVED_DATA})

  add_test(NAME tasten COMMAND keytest)
  add_test(NAME protokoll COMMAND diagtest)
  set(BEHAVED_BSP "" CACHE FILEPATH "Eine .bsp zum Pruefen des Entity-Lesers")
  add_test(NAME karte COMMAND bsptest ${BEHAVED_BSP})
  add_test(NAME kartengeometrie COMMAND bspgeotest ${BEHAVED_BSP})
  add_test(NAME kartenansicht COMMAND mapviewtest)
  add_test(NAME bilder COMMAND imagetest)
  set(BEHAVED_GLM "" CACHE FILEPATH "Ein .glm zum Pruefen des Modell-Lesers")
  set(BEHAVED_SKIN "" CACHE FILEPATH "Die zugehoerige .skin")
  set(BEHAVED_GLA "" CACHE FILEPATH "Eine .gla zum Pruefen des Skelett-Lesers")
  add_test(NAME modell COMMAND glmtest ${BEHAVED_GLM} ${BEHAVED_SKIN})
  add_test(NAME skelett COMMAND glatest ${BEHAVED_GLA})
  add_test(NAME mission COMMAND missiontest)
  add_test(NAME szene COMMAND scenetest
           ${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/intro_jedi.txt
           ${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/md_twj_jedi.ent)
  add_test(NAME kamerabahn COMMAND camtracktest
           ${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/intro_jedi.txt
           ${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/cin2_jedi.txt)
  set(BEHAVED_PK3 "" CACHE FILEPATH "Ein .pk3 zum Pruefen des Archivlesers")
  add_test(NAME archiv COMMAND pk3test ${BEHAVED_PK3})
  add_test(NAME klang COMMAND soundtest)
  add_test(NAME zeitleiste COMMAND timelinetest ${BEHAVED_BHC} ${BEHAVED_DATA}
           ${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/intro_jedi.txt
           ${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/cin2_jedi.txt)
  add_test(NAME gegenproben COMMAND checks ${BEHAVED_BHC} ${BEHAVED_DATA} ${BEHAVED_SUPP})
  add_test(NAME bildschirmfoto COMMAND screenshot ${BEHAVED_BHC} ${BEHAVED_DATA} ${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/intro_jedi.txt
                   ${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/gonkability.txt
                   ${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/cin2_jedi.txt
                   ${CMAKE_CURRENT_SOURCE_DIR}/data/fixtures/cin3_jedi.txt)
  if(BEHAVED_SCRIPTS)
    # Benannte Ausnahme: in seccam_planted.icarus fehlt in Zeile 55 das
    # Leerzeichen vor der schliessenden Klammer. Das ist Ravens Handarbeit -
    # genau ein Fall unter 30158 Argumentlisten, gemessen. Unser Schreiber
    # ist gleichmaessiger als die Vorlage.
    add_test(NAME rundlauf COMMAND roundtrip ${BEHAVED_SCRIPTS} seccam_planted.icarus)
    add_test(NAME befehlsmodell COMMAND checkall ${BEHAVED_SCRIPTS} ${BEHAVED_BHC} ${BEHAVED_DATA} ${BEHAVED_SUPP})
    add_test(NAME bearbeitungsrundgang COMMAND edittour ${BEHAVED_SCRIPTS} ${BEHAVED_BHC} ${BEHAVED_DATA} ${BEHAVED_SUPP})
    add_test(NAME bearbeiten COMMAND edittest ${BEHAVED_SCRIPTS} ${BEHAVED_BHC} ${BEHAVED_DATA} ${BEHAVED_SUPP})
    add_test(NAME fuzzer   COMMAND fuzz ${BEHAVED_SCRIPTS} ${BEHAVED_BHC} ${BEHAVED_DATA} 5000)
    add_test(NAME fuzzer_bearbeiten COMMAND editfuzz ${BEHAVED_SCRIPTS} ${BEHAVED_BHC} ${BEHAVED_DATA} 300)
    # Die .ibi-Probe laeuft IMMER.
    #
    # Hier stand `if(BEHAVED_IBI)`. Wer die Variable nicht setzte, bekam die
    # Probe schweigend gar nicht - und im Prueflauf sieht das genauso aus
    # wie bestanden. Von rc347 bis rc359 hat sie deshalb nichts geprueft.
    #
    # `-` heisst: die eingebaute start_cin2.IBI nehmen (30 Byte, siehe
    # tests/ibitest.cpp). Ein gesetztes BEHAVED_IBI geht weiterhin vor.
    if(BEHAVED_IBI)
      set(BEHAVED_IBI_ARG ${BEHAVED_IBI})
    else()
      set(BEHAVED_IBI_ARG "-")
    endif()
    add_test(NAME ibi COMMAND ibitest ${BEHAVED_IBI_ARG} ${BEHAVED_SCRIPTS} ${BEHAVED_BHC} ${BEHAVED_DATA} ${BEHAVED_SUPP})
  endif()
endif()
