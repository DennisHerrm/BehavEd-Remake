// pk3.h - in .pk3-Archiven blaettern
//
// Ein .pk3 ist eine Zip-Datei. Gelesen wird nur das Inhaltsverzeichnis am
// Ende (End of Central Directory + Central Directory), nicht die einzelnen
// Dateikoepfe - das ist derselbe Weg, den auch die Engine geht, und er
// braucht kein Durchsuchen des ganzen Archivs.
//
// Wozu: die Skripte einer Mod liegen fast nie ausgepackt herum. Sie stecken
// in `assets*.pk3` oder in der `.pk3` der Mod, zusammen mit den Karten. Ohne
// diesen Leser muesste man vorher von Hand auspacken.
//
// Nur lesend. Ein Archiv aus dem Spielordner ist keine vertrauenswuerdige
// Eingabe: jede Groesse und jeder Versatz wird geprueft, bevor er benutzt
// wird, und die im Verzeichnis angegebene Groesse ist eine harte Obergrenze
// beim Auspacken.
#ifndef BHED_PK3_H
#define BHED_PK3_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bhed {

struct Pk3Entry {
    std::string name;              // Pfad im Archiv, mit /
    std::uint32_t compressedSize = 0;
    std::uint32_t size = 0;
    std::uint32_t localHeaderOffset = 0;
    std::uint16_t method = 0;      // 0 = ohne Packung, 8 = deflate
};

struct Pk3 {
    std::string path;
    std::vector<Pk3Entry> entries;
    // Kleingeschriebener Name -> Index. Einmal beim Lesen des Verzeichnisses
    // aufgebaut. Ohne ihn kostete jede Namenssuche einen Durchlauf ueber alle
    // Eintraege samt einer Kleinschreibung je Eintrag - bei 5240 Eintraegen
    // rund eine halbe Millisekunde, und gesucht wird oft.
    std::map<std::string, std::size_t> index;
    [[nodiscard]] bool empty() const { return entries.empty(); }
    // Gross- und Kleinschreibung wird ignoriert: Zip unterscheidet sie, die
    // Engine nicht, und Ravens Dateien sind darin uneinheitlich.
    [[nodiscard]] const Pk3Entry* find(const std::string& name) const;
};

// Inhaltsverzeichnis lesen. false, wenn es keine Zip-Datei ist.
[[nodiscard]] bool readPk3Directory(const std::string& path, Pk3& out,
                                    std::string* error = nullptr);

// Eine einzelne Datei auspacken.
[[nodiscard]] bool readPk3File(const Pk3& archive, const Pk3Entry& entry,
                               std::string& out, std::string* error = nullptr);

// Alle .pk3 eines Ordners, in der Reihenfolge, die die Engine benutzt:
// alphabetisch, spaetere ueberdecken fruehere.
struct GamePath {
    std::string directory;
    std::vector<Pk3> archives;
    int fileCount = 0;
};

[[nodiscard]] bool scanGamePath(const std::string& directory, GamePath& out);

// Alle Eintraege eines Suchpfads, die auf eine der Endungen passen -
// zusammengefasst und ohne Dubletten, spaetere Archive gewinnen.
struct FoundFile {
    std::string name;        // Pfad im Archiv
    std::string archive;     // welches .pk3
};
[[nodiscard]] std::vector<FoundFile> findByExtension(
    const GamePath& path, const std::vector<std::string>& extensions);

}  // namespace bhed
#endif
