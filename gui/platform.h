// platform.h - was nur Windows kann
//
// Die Oberflaeche kennt keine Windows-Aufrufe. Das ist kein Selbstzweck:
// app.cpp laesst sich dadurch auch unter Linux uebersetzen, und genau das
// prueft tools/check_gui.sh bei jedem Durchlauf.
#ifndef BHED_PLATFORM_H
#define BHED_PLATFORM_H

#include <string>
#include <vector>

namespace bhed::platform {

// Leerer Rueckgabewert heisst abgebrochen.
[[nodiscard]] std::string openFileDialog(const char* title, const char* filter,
                                         const std::string& startDir);
[[nodiscard]] std::string saveFileDialog(const char* title, const char* filter,
                                         const std::string& startDir,
                                         const std::string& suggestedName);

enum class Answer { Yes, No, Cancel };
[[nodiscard]] Answer askSaveChanges(const std::string& question, const char* title);
void showError(const std::string& message, const char* title);

// Eine Datei mit dem Programm oeffnen, das Windows dafuer vorgesehen hat.
// Der Export des Originals macht genau das - im Bildschirmfoto erscheint
// Notepad mit dem erzeugten Text.
void openWithDefaultApp(const std::string& path);
// behaved noch einmal starten, mit einer Datei auf der Kommandozeile.
//
// Das Original tut das beim "run"-Knopf eines Skriptfeldes ("start %s %s",
// "\BehavEd.exe"): das angegebene Skript geht in einer NEUEN Instanz auf,
// der Ereigniseditor der alten bleibt unberuehrt.
bool startBehavedWith(const std::string& datei);

// Ordner fuer die Einstellungsdatei (%APPDATA%\behaved), wird angelegt.
[[nodiscard]] std::string settingsDirectory();

// Der Ordner, in dem das Programm selbst liegt.
//
// Dorthin gehoert das Protokoll: wer es hinschicken soll, findet es neben der
// .exe, statt es in %APPDATA% suchen zu muessen.
[[nodiscard]] std::string executableDirectory();
// Einen Ordner im Explorer zeigen.
void openInExplorer(const std::string& folder);

// Die Dateinamen eines Ordners, ohne Pfad und ohne Unterordner.
//
// Gebraucht, um die Haeute neben einem Modell zu finden: davon liegen oft
// mehrere - model_default.skin, model_red.skin, model_blue.skin - und sie
// entscheiden, welche Flaechen sichtbar sind.
[[nodiscard]] std::vector<std::string> listDirectory(const std::string& dir);

// Einen ORDNER auswaehlen lassen.
//
// Vorher gab es das nicht: man musste eine .pk3 im gewuenschten Ordner
// auswaehlen, damit das Programm den Ordner davon nehmen konnte. Der Common
// Item Dialog kann es mit FOS_PICKFOLDERS.
[[nodiscard]] std::string pickFolder(const char* title, const std::string& startDir);


// --- RenderDoc aus dem Programm heraus ---------------------------------
//
// RenderDoc kann sich NICHT nachtraeglich an ein laufendes Programm
// haengen - es legt seine Bibliothek beim Start hinein. Laeuft behaved
// schon darunter, liegt `renderdoc.dll` im Prozess, und ueber
// `RENDERDOC_GetAPI` laesst sich eine Aufnahme ausloesen: dasselbe wie F12,
// nur aus dem Menue.
//
// `renderDocDa()` sagt, ob die Bibliothek da ist - sonst waere der
// Menuepunkt ein Knopf, der nichts tut.
[[nodiscard]] bool renderDocDa();
void renderDocAufnehmen();

}  // namespace bhed::platform
#endif
