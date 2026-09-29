@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

REM ===================================================================
REM  behaved - Bauen
REM
REM  Der gesamte Rumpf laeuft ueber "call :main". Grund: springt etwas
REM  vorzeitig zu :eof, landen wir hinter dem call und nicht im Nichts -
REM  das Fenster bleibt offen und man kann die Meldung lesen.
REM
REM  Aufruf:
REM     build.bat              bauen und pruefen
REM     build.bat nurbauen     ohne Tests
REM     build.bat sauber       build und out entfernen
REM ===================================================================

call :main %*
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (
    echo ============================================================
    echo  Fertig.
    echo ============================================================
) else (
    echo ============================================================
    echo  Mit Fehlern beendet. Meldungen stehen weiter oben.
    echo ============================================================
)
if not "%1"=="/nopause" pause
exit /b %RC%


:main
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"

echo ============================================================
echo  behaved - Bauen
echo ============================================================
echo  Verzeichnis: %ROOT%
echo.

REM --- Voraussetzung: CMake -----------------------------------------
where cmake >nul 2>&1
if errorlevel 1 (
    echo [FEHLER] CMake wurde nicht gefunden.
    echo          Von https://cmake.org holen, beim Installieren
    echo          "Add to PATH" ankreuzen.
    exit /b 1
)
for /f "tokens=3" %%V in ('cmake --version ^| findstr /r "^cmake version"') do set "CMAKEVER=%%V"
echo  CMake      : !CMAKEVER!

REM --- Voraussetzung: Visual Studio ---------------------------------
REM  Absichtlich OHNE -G: der Generator wird nicht festgenagelt. Bei efxed
REM  stand dort "Visual Studio 17 2022" fest verdrahtet, und auf einem
REM  Rechner mit VS 2026 schlug jeder Aufruf mit "could not find any
REM  instance of Visual Studio" fehl. CMake findet die neueste selbst.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName`) do set "VSNAME=%%I"
    if defined VSNAME (
        echo  Visual Stu.: !VSNAME!
    ) else (
        echo  Visual Stu.: gefunden, aber ohne C++-Werkzeuge
        echo.
        echo [FEHLER] Im Visual-Studio-Installer fehlt der Baustein
        echo          "Desktopentwicklung mit C++".
        exit /b 1
    )
) else (
    echo  Visual Stu.: vswhere nicht gefunden, CMake sucht selbst
)

REM --- Python finden -------------------------------------------------
REM  "where python" allein reicht nicht: Windows legt unter
REM  %LOCALAPPDATA%\Microsoft\WindowsApps einen Platzhalter namens
REM  python.exe ab, der nur den Store oeffnet. "where" findet ihn, der
REM  Aufruf liefert aber einen Fehlercode - bei efxed meldeten die Pruefer
REM  deshalb faelschlich Fehler.
set "PYEXE="
for %%P in (py python3 python) do (
    if not defined PYEXE (
        %%P --version >nul 2>&1 && set "PYEXE=%%P"
    )
)
if defined PYEXE (
    echo  Python     : !PYEXE!
) else (
    echo  Python     : NICHT GEFUNDEN
    echo               Die Pruefer entfallen damit - auch der, der vor dem
    echo               Uebersetzen meldet, wenn eine Quelldatei in
    echo               CMakeLists.txt fehlt. Python von python.org holen und
    echo               beim Installieren "Add to PATH" ankreuzen.
)

set "TARGET=%1"
if "%TARGET%"=="/nopause" set "TARGET="
if "%TARGET%"=="" set "TARGET=alles"

if /i "%TARGET%"=="sauber" (
    echo.
    echo --- Aufraeumen ---
    if exist "%ROOT%\build" rmdir /s /q "%ROOT%\build"
    if exist "%ROOT%\out"   rmdir /s /q "%ROOT%\out"
    echo  build und out entfernt.
    exit /b 0
)

REM --- Dear ImGui ----------------------------------------------------
REM  Nicht mitgeliefert. Fehlt es, wird es geholt. Ohne ImGui entstehen
REM  nur die Bibliothek und das Befehlszeilenwerkzeug, kein Fenster.
set "IMGUIDIR="
for %%D in ("%ROOT%\imgui" "%ROOT%\..\imgui" "%ROOT%\external\imgui") do (
    if not defined IMGUIDIR if exist "%%~D\imgui.cpp" set "IMGUIDIR=%%~fD"
)
if not defined IMGUIDIR (
    where git >nul 2>&1
    if errorlevel 1 (
        echo  Dear ImGui : nicht gefunden, und git auch nicht.
        echo               Von Hand holen:
        echo               git clone --depth 1 -b v1.92.9b-docking https://github.com/ocornut/imgui.git
    ) else (
        echo  Dear ImGui : wird geholt ^(v1.92.9b-docking^) ...
        git clone --depth 1 -b v1.92.9b-docking https://github.com/ocornut/imgui.git "%ROOT%\imgui"
        if exist "%ROOT%\imgui\imgui.cpp" set "IMGUIDIR=%ROOT%\imgui"
    )
)
if defined IMGUIDIR echo  Dear ImGui : !IMGUIDIR!
echo.

REM --- Quellen gegen das Bauskript pruefen ---------------------------
REM  Vor dem Uebersetzen. Eine Quelldatei, die in CMakeLists.txt fehlt,
REM  faellt sonst erst beim BINDEN auf - nach Minuten, mit einer Meldung
REM  ueber ein "nicht aufgeloestes externes Symbol", die den Grund nicht
REM  nennt.
REM  Jeder Pruefer bekommt SEINE eigene Meldung.
REM
REM  Vorher teilten sich beide eine: brach lint_headers ab, stand da
REM  "Eine Quelldatei fehlt in CMakeLists.txt" - die Ursache eines
REM  ganz anderen Pruefers. shank suchte in rc548 an der falschen
REM  Stelle, und das lag an dieser Zeile.
if defined PYEXE (
    !PYEXE! "%ROOT%\tools\lint_sources.py"
    if errorlevel 1 (
        echo.
        echo [FEHLER] Eine Quelldatei fehlt in CMakeLists.txt ^(siehe oben^).
        echo          Ohne sie gibt es beim Binden LNK2019.
        exit /b 1
    )
    !PYEXE! "%ROOT%\tools\lint_headers.py"
    !PYEXE! "%ROOT%\tools\lint_fremdprogramm.py"
    if errorlevel 1 (
        echo.
        echo [FEHLER] Eine Kopfdatei ist nicht eigenstaendig uebersetzbar
        echo          ^(siehe oben^). Sie bricht, sobald jemand die
        echo          Einbindereihenfolge aendert.
        exit /b 1
    )
)

echo ============================================================
echo  Einrichten
echo ============================================================
if not exist "%ROOT%\build" mkdir "%ROOT%\build"

REM  Kein -DCMAKE_BUILD_TYPE: der Visual-Studio-Generator waehlt die
REM  Konfiguration erst beim Uebersetzen. CMake wirft die Variable sonst
REM  mit "Manually-specified variables were not used" zurueck.
set "CMOPT=-A x64"
if defined IMGUIDIR set "CMOPT=!CMOPT! -DIMGUI_DIR=!IMGUIDIR:\=/!"
if exist "%ROOT%\data\jascripts" set "CMOPT=!CMOPT! -DBEHAVED_SCRIPTS=%ROOT:\=/%/data/jascripts"

cmake -S "%ROOT%" -B "%ROOT%\build" !CMOPT!
if errorlevel 1 (
    echo.
    echo [FEHLER] CMake konnte das Projekt nicht einrichten.
    echo          Haeufigste Ursache: der Baustein "Desktopentwicklung
    echo          mit C++" fehlt im Visual-Studio-Installer.
    exit /b 1
)
echo.

echo ============================================================
echo  Uebersetzen
echo ============================================================
cmake --build "%ROOT%\build" --config Release --parallel
if errorlevel 1 (
    echo.
    echo ============================================================
    echo  Das Uebersetzen ist fehlgeschlagen.
    echo.
    echo  Steht oben "INTERNER COMPILERFEHLER in ... CL.exe", dann ist
    echo  der UEBERSETZER abgestuerzt, nicht unser Programm. Das kommt
    echo  bei sehr neuen MSVC-Fassungen vor.
    echo.
    echo  Zweiter Anlauf mit vorsichtiger Codeerzeugung...
    echo ============================================================
    echo.
    cmake -S "%ROOT%" -B "%ROOT%\build" -DBEHAVED_MSVC_SAFE_CODEGEN=ON
    cmake --build "%ROOT%\build" --config Release --parallel
)
if errorlevel 1 (
    echo.
    echo [FEHLER] Das Uebersetzen ist fehlgeschlagen.
    exit /b 1
)
echo.

REM --- Bereitstellen -------------------------------------------------
REM
REM  Zwei Orte absuchen. CMake legt die Programme nach bin\Release, weil
REM  CMAKE_RUNTIME_OUTPUT_DIRECTORY das so vorgibt; ohne die Vorgabe waeren
REM  sie in Release. Der erste Anlauf sah nur in Release nach, fand nichts
REM  und lief WORTLOS weiter - die Ausgabe zeigte nur "bereit: out\data\base",
REM  und man musste die MSBuild-Zeilen lesen, um zu sehen, wo die .exe liegt.
set "FOUND="
if not exist "%ROOT%\out" mkdir "%ROOT%\out"
for %%F in (behaved.exe bhed.exe) do (
    set "SRCEXE="
    if exist "%ROOT%\build\bin\Release\%%F" set "SRCEXE=%ROOT%\build\bin\Release\%%F"
    if not defined SRCEXE if exist "%ROOT%\build\Release\%%F" set "SRCEXE=%ROOT%\build\Release\%%F"
    if defined SRCEXE (
        copy /y "!SRCEXE!" "%ROOT%\out\" >nul
        echo  bereit: out\%%F
        set "FOUND=1"
    ) else (
        echo  [WARNUNG] %%F wurde nicht gefunden - weder in build\bin\Release
        echo            noch in build\Release.
    )
)
if not defined FOUND (
    echo.
    echo [FEHLER] Es wurde kein einziges Programm bereitgestellt.
    echo          Das Uebersetzen lief durch, aber die .exe liegt woanders.
    exit /b 1
)
REM  Das Befehlsmodell muss neben die .exe, sonst findet sie es nicht.
if not exist "%ROOT%\out\data\base" mkdir "%ROOT%\out\data\base"
copy /y "%ROOT%\data\base\*" "%ROOT%\out\data\base\" >nul
echo  bereit: out\data\base
echo.

if /i "%TARGET%"=="nurbauen" (
    echo  Tests uebersprungen ^(Aufruf mit "nurbauen"^).
    exit /b 0
)

if defined PYEXE (
    echo ============================================================
    echo  Pruefer  ^(!PYEXE!^)
    echo ============================================================
    !PYEXE! "%ROOT%\tools\lint_i18n.py"
    if errorlevel 1 echo [WARNUNG] Nicht uebersetzte Oberflaechentexte - siehe oben.
    !PYEXE! "%ROOT%\tools\lint_portability.py"
    if errorlevel 1 echo [WARNUNG] Fehlende Standard-Koepfe oder Nicht-ASCII - siehe oben.
    !PYEXE! "%ROOT%\tools\lint_imgui_context.py"
    if errorlevel 1 echo [WARNUNG] ImGui wird vor CreateContext angefasst - siehe oben.

    if exist "%ROOT%\out\behaved.exe" (
        echo.
        echo ============================================================
        echo  Importtabelle der .exe
        echo ============================================================
        !PYEXE! "%ROOT%\tools\check_exe_imports.py" "%ROOT%\out\behaved.exe"
        if errorlevel 1 set "IMPBAD=1"
        echo.
        echo ============================================================
        echo  Symbol, Manifest und Versionsangaben
        echo ============================================================
        !PYEXE! "%ROOT%\tools\check_exe_resources.py" "%ROOT%\out\behaved.exe"
        if errorlevel 1 echo [WARNUNG] Der .exe fehlen Ressourcen - siehe oben.
        if defined IMPBAD (
            echo.
            echo  Eine DLL ist fest gebunden, die es nicht ueberall gibt.
            echo  Steht d3d11.dll darunter, laedt das Programm auf Rechnern
            echo  ohne Direct3D gar nicht - und der Rueckfall auf OpenGL
            echo  kaeme nie zum Zug.
        )
    )
    echo.
)

echo ============================================================
echo  Testlauf
echo ============================================================
ctest --test-dir "%ROOT%\build" -C Release --output-on-failure
if errorlevel 1 (
    echo.
    echo [WARNUNG] Mindestens ein Test ist fehlgeschlagen.
    echo           Die grossen Tests brauchen einen Ordner mit .icarus-
    echo           Skripten. Lege ihn unter data\jascripts ab, dann laufen
    echo           sie beim naechsten Mal mit.
)
exit /b 0
