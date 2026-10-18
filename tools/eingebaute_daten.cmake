# eingebaute_daten.cmake - data/base als Bytes ins Programm (rc601 release)
#
# Warum: siehe include/bhed/eingebaut.h. Kurz: eine behaved.exe allein lief
# bis rc600 nicht ("behaved.bhc not found in ..."), und shank wollte zu jedem
# Release "eine .exe ..., dass die Leute sie runterladen und nutzen koennen".
#
# behaved_eingebaute_daten(<ziel> <ausgabe.inc> <datei>...)
#
# Schreibt beim KONFIGURIEREN je Datei ein Feld "const unsigned char[]" und
# die Tabelle kEingebauteDateien[] {Name, Bytes, Groesse} nach <ausgabe.inc>
# und setzt fuer src/eingebaut.cpp den Schalter BHED_EINGEBAUTE_DATEN, damit
# die Datei die Tabelle einbindet. Jede Quelldatei steht in
# CMAKE_CONFIGURE_DEPENDS: aendert sich etwas in data/base, laeuft CMake beim
# naechsten Bauen von selbst neu. configure_file(COPYONLY) ersetzt die .inc
# nur, wenn sich ihr Inhalt geaendert hat - sonst wuerde src/eingebaut.cpp
# bei jedem Konfigurieren neu uebersetzt.
#
# Bytes statt Zeichenkette: MSVC begrenzt Zeichenkettenliterale (C2026), ein
# Feld aus Zahlen nicht. CMake liest mit file(READ ... HEX) und kennt in
# regulaeren Ausdruecken keine {n}-Wiederholung - daher die 64 Punkte (32 Byte
# je Zeile).
function(behaved_eingebaute_daten ziel ausgabe)
  set(_dateien ${ARGN})
  list(SORT _dateien)
  set(_text "// Erzeugt von tools/eingebaute_daten.cmake (rc601 release) aus data/base - nicht von Hand aendern.\n")
  set(_tabelle "")
  set(_nr 0)
  foreach(_datei IN LISTS _dateien)
    get_filename_component(_name "${_datei}" NAME)
    if(_name MATCHES "[^A-Za-z0-9_.-]")
      message(FATAL_ERROR "eingebaute Daten: Dateiname mit Sonderzeichen: ${_name}")
    endif()
    file(READ "${_datei}" _hex HEX)
    string(LENGTH "${_hex}" _laenge)
    math(EXPR _groesse "${_laenge} / 2")
    if(_groesse EQUAL 0)
      set(_hex "00")   # ein Feld darf nicht leer sein; die Groesse bleibt 0
    endif()
    string(REGEX REPLACE "(................................................................)" "\\1\n" _hex "${_hex}")
    string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1," _hex "${_hex}")
    string(APPEND _text "const unsigned char kEingebaut${_nr}[] = {\n${_hex}\n};\n")
    string(APPEND _tabelle "    {\"${_name}\", kEingebaut${_nr}, ${_groesse}u},\n")
    math(EXPR _nr "${_nr} + 1")
  endforeach()
  if(_nr EQUAL 0)
    message(FATAL_ERROR "eingebaute Daten: keine Dateien angegeben")
  endif()
  string(APPEND _text "const ::bhed::eingebaut::Datei kEingebauteDateien[] = {\n${_tabelle}};\n")
  file(WRITE "${ausgabe}.neu" "${_text}")
  configure_file("${ausgabe}.neu" "${ausgabe}" COPYONLY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_dateien})
  get_filename_component(_ordner "${ausgabe}" DIRECTORY)
  target_include_directories(${ziel} PRIVATE "${_ordner}")
  set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/eingebaut.cpp" APPEND PROPERTY
               COMPILE_DEFINITIONS BHED_EINGEBAUTE_DATEN=1)
  message(STATUS "Eingebaute Daten: ${_nr} Dateien aus data/base")
endfunction()
