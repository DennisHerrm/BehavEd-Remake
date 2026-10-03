#include "bhed/pk3.h"

#include "bhed/inflate.h"
#include "bhed/diag.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace bhed {
namespace {

constexpr std::uint32_t kEndOfCentralDir = 0x06054B50U;
constexpr std::uint32_t kCentralFileHeader = 0x02014B50U;
constexpr std::uint32_t kLocalFileHeader = 0x04034B50U;

std::uint16_t u16(const std::string& b, std::size_t at) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(b[at]) |
        (static_cast<unsigned>(static_cast<unsigned char>(b[at + 1])) << 8));
}

std::uint32_t u32(const std::string& b, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) {
        v = (v << 8) | static_cast<unsigned char>(b[at + static_cast<std::size_t>(i)]);
    }
    return v;
}

// Einen Namen auf die Form bringen, unter der er im Index steht.
//
// Drei Dinge, und jedes davon kommt in echten Mods vor:
//
//   * Gross- und Kleinschreibung. Ein .skin kann auf
//     "models/players/Kyle/Torso.tga" zeigen, im Archiv steht
//     "models/players/kyle/torso.tga". Auf Windows faellt das nie auf, im
//     Archiv aber schon - dort ist der Name blosser Text.
//   * Rueckstriche. Radiant und manche Werkzeuge schreiben
//     "models\players\kyle\model.glm"; im Archiv steht immer der
//     Schraegstrich.
//   * Ein fuehrender Schraegstrich oder "./" davor.
std::string normaliseName(std::string s) {
    for (char& c : s) {
        if (c == '\\') {
            c = '/';
        }
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    while (s.size() > 1 && (s[0] == '/' || (s[0] == '.' && s[1] == '/'))) {
        s.erase(0, s[0] == '/' ? 1 : 2);
    }
    return s;
}

std::string lower(std::string s) { return normaliseName(std::move(s)); }


}  // namespace

const Pk3Entry* Pk3::find(const std::string& name) const {
    const auto it = index.find(lower(name));
    return it == index.end() ? nullptr : &entries[it->second];
}

bool readPk3Directory(const std::string& path, Pk3& out, std::string* error) {
    out = Pk3{};
    out.path = path;

    // NICHT das ganze Archiv lesen. Ein assets0.pk3 hat 350 MB, und beim
    // Start werden alle Archive eines Spielordners durchgesehen.
    //
    // Gebraucht werden nur zwei Ausschnitte: das Endverzeichnis ganz hinten
    // (es kann bis zu 64 KiB Kommentar hinter sich haben) und danach das
    // Inhaltsverzeichnis, dessen Ort und Groesse dort stehen.
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error != nullptr) { *error = "nicht lesbar"; }
        return false;
    }
    f.seekg(0, std::ios::end);
    const std::streamoff fileSize = f.tellg();
    if (fileSize < 22) {
        if (error != nullptr) { *error = "zu kurz fuer ein Archiv"; }
        return false;
    }
    const std::streamoff tailSize = std::min<std::streamoff>(fileSize, 0x10000 + 22);
    std::string tail(static_cast<std::size_t>(tailSize), '\0');
    f.seekg(fileSize - tailSize);
    f.read(tail.data(), tailSize);

    std::size_t eocd = std::string::npos;
    for (std::size_t i = tail.size() - 22; ; --i) {
        if (u32(tail, i) == kEndOfCentralDir) {
            eocd = i;
            break;
        }
        if (i == 0) {
            break;
        }
    }
    if (eocd == std::string::npos) {
        if (error != nullptr) { *error = "kein Zip-Endverzeichnis gefunden"; }
        return false;
    }

    const std::uint16_t count = u16(tail, eocd + 10);
    const std::uint32_t dirSize = u32(tail, eocd + 12);
    const std::uint32_t dirOffset = u32(tail, eocd + 16);
    if (static_cast<std::streamoff>(dirOffset) + dirSize > fileSize) {
        if (error != nullptr) { *error = "Inhaltsverzeichnis liegt ausserhalb der Datei"; }
        return false;
    }

    std::string b(dirSize, '\0');
    f.seekg(static_cast<std::streamoff>(dirOffset));
    f.read(b.data(), static_cast<std::streamsize>(dirSize));
    if (f.gcount() != static_cast<std::streamsize>(dirSize)) {
        if (error != nullptr) { *error = "Inhaltsverzeichnis abgeschnitten"; }
        return false;
    }

    std::size_t p = 0;
    out.entries.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        if (p + 46 > b.size() || u32(b, p) != kCentralFileHeader) {
            break;   // abgeschnitten oder Muell - was da ist, behalten
        }
        Pk3Entry e;
        e.method = u16(b, p + 10);
        e.compressedSize = u32(b, p + 20);
        e.size = u32(b, p + 24);
        const std::uint16_t nameLen = u16(b, p + 28);
        const std::uint16_t extraLen = u16(b, p + 30);
        const std::uint16_t commentLen = u16(b, p + 32);
        e.localHeaderOffset = u32(b, p + 42);
        if (p + 46 + nameLen > b.size()) {
            break;
        }
        e.name = b.substr(p + 46, nameLen);
        // Zip benutzt /, Windows \ - vereinheitlichen.
        for (char& c : e.name) {
            if (c == '\\') { c = '/'; }
        }
        // Ordnereintraege haben keine Daten.
        if (!e.name.empty() && e.name.back() != '/') {
            const std::string key = lower(e.name);
            out.index[key] = out.entries.size();
            out.entries.push_back(std::move(e));
        }
        p += 46U + nameLen + extraLen + commentLen;
    }
    return true;
}

bool readPk3File(const Pk3& archive, const Pk3Entry& entry, std::string& out,
                 std::string* error) {
    out.clear();
    // NUR den benoetigten Ausschnitt lesen.
    //
    // Der erste Anlauf zog fuer jede einzelne Datei das ganze Archiv in den
    // Speicher. Gemessen an einem 17-MB-Archiv waren das 18 ms je Datei; eine
    // echte assets0.pk3 hat 350 MB, dort waeren es Hunderte. Genau das war
    // das kurze Haengen beim Einfuegen.
    std::ifstream f(archive.path, std::ios::binary);
    if (!f) {
        if (error != nullptr) { *error = "Archiv nicht lesbar"; }
        return false;
    }
    std::string head(30, '\0');
    f.seekg(static_cast<std::streamoff>(entry.localHeaderOffset));
    f.read(head.data(), 30);
    if (!f || u32(head, 0) != kLocalFileHeader) {
        if (error != nullptr) { *error = "lokaler Dateikopf fehlt"; }
        return false;
    }
    // Namens- und Zusatzlaenge stehen im LOKALEN Kopf und koennen von denen
    // im Verzeichnis abweichen - deshalb hier neu lesen.
    const std::uint16_t nameLen = u16(head, 26);
    const std::uint16_t extraLen = u16(head, 28);
    const std::streamoff dataAt =
        static_cast<std::streamoff>(entry.localHeaderOffset) + 30 + nameLen + extraLen;

    // Erst gegen die Archivgroesse pruefen, DANN Speicher anlegen: ein
    // Eintrag mit 4 GB Laenge in einer kleinen Datei belegte sonst 4 GB,
    // bevor das Lesen scheiterte (Code-Pruefung 03.10.).
    f.seekg(0, std::ios::end);
    const std::streamoff dateiGroesse = f.tellg();
    if (dateiGroesse < dataAt ||
        static_cast<std::uint64_t>(dateiGroesse - dataAt) < entry.compressedSize) {
        if (error != nullptr) { *error = "Daten liegen ausserhalb der Datei"; }
        return false;
    }
    std::string b(entry.compressedSize, '\0');
    f.seekg(dataAt);
    f.read(b.data(), static_cast<std::streamsize>(entry.compressedSize));
    if (f.gcount() != static_cast<std::streamsize>(entry.compressedSize)) {
        if (error != nullptr) { *error = "Daten liegen ausserhalb der Datei"; }
        return false;
    }
    const std::size_t data = 0;

    if (entry.method == 0) {
        out = b.substr(0, entry.size);
        return true;
    }
    if (entry.method != 8) {
        if (error != nullptr) {
            *error = "Packverfahren " + std::to_string(entry.method) + " nicht unterstuetzt";
        }
        return false;
    }
    const inflate::Result r = inflate::raw(
        reinterpret_cast<const unsigned char*>(b.data()) + data, entry.compressedSize,
        entry.size);
    if (!r.ok) {
        if (error != nullptr) { *error = r.error; }
        return false;
    }
    out.assign(r.data.begin(), r.data.end());
    return true;
}

bool scanGamePath(const std::string& directory, GamePath& out) {
    out = GamePath{};
    out.directory = directory;
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) {
        return false;
    }
    std::vector<std::string> files;
    for (const auto& e : fs::directory_iterator(directory, ec)) {
        if (!e.is_regular_file()) {
            continue;
        }
        if (lower(e.path().extension().string()) == ".pk3") {
            files.push_back(e.path().string());
        }
    }
    // Alphabetisch, wie die Engine: spaetere Archive ueberdecken fruehere.
    std::sort(files.begin(), files.end());
    for (const std::string& f : files) {
        Pk3 a;
        if (readPk3Directory(f, a, nullptr)) {
            out.fileCount += static_cast<int>(a.entries.size());
            out.archives.push_back(std::move(a));
        }
    }
    diag::detail("Spielordner " + directory + ": " +
                 std::to_string(out.archives.size()) + " Archive, " +
                 std::to_string(out.fileCount) + " Dateien");
    return !out.archives.empty();
}

std::vector<FoundFile> findByExtension(const GamePath& path,
                                       const std::vector<std::string>& extensions) {
    std::vector<FoundFile> out;
    // Eine geordnete Menge statt einer Liste: bei mehreren Archiven mit je
    // ein paar tausend Eintraegen war der lineare Vergleich quadratisch.
    std::set<std::string> seen;
    // Rueckwaerts, damit das spaetere Archiv gewinnt.
    for (auto a = path.archives.rbegin(); a != path.archives.rend(); ++a) {
        for (const Pk3Entry& e : a->entries) {
            const std::string low = lower(e.name);
            bool match = false;
            for (const std::string& ext : extensions) {
                if (low.size() > ext.size() &&
                    low.compare(low.size() - ext.size(), ext.size(), lower(ext)) == 0) {
                    match = true;
                    break;
                }
            }
            if (!match) {
                continue;
            }
            if (!seen.insert(low).second) {
                continue;
            }
            out.push_back(FoundFile{e.name, a->path});
        }
    }
    std::sort(out.begin(), out.end(), [](const FoundFile& x, const FoundFile& y) {
        return lower(x.name) < lower(y.name);
    });
    return out;
}

}  // namespace bhed
