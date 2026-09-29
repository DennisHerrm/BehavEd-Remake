#include "bhed/bsp.h"
#include "bhed/diag.h"

#include <cctype>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace bhed {
namespace {

std::int32_t readInt(const std::string& b, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) {
        v = (v << 8) | static_cast<unsigned char>(b[at + static_cast<std::size_t>(i)]);
    }
    std::int32_t out = 0;
    std::memcpy(&out, &v, 4);
    return out;
}

void addUnique(std::vector<std::string>& list, const std::string& value) {
    if (value.empty()) {
        return;
    }
    for (const std::string& v : list) {
        if (v == value) {
            return;
        }
    }
    list.push_back(value);
}

}  // namespace

const std::string* MapEntity::find(const std::string& key) const {
    // OHNE Ruecksicht auf Gross- und Kleinschreibung - so macht es die
    // Engine. code/game/g_spawn.cpp, G_ParseField():
    //
    //     if ( !Q_stricmp(f->name, key) ) {
    //
    // Die Feldtabelle dort schreibt "NPC_targetname" und "NPC_type" gross,
    // Karten schreiben mal so, mal so. Vorher verglich diese Stelle genau,
    // und ein "NPC_targetname" in der .ent fand keinen Spawner - waehrend
    // dieselbe Karte im Spiel einwandfrei lief.
    //
    // Nur die SCHLUESSEL sind unempfindlich. Klassennamen vergleicht die
    // Engine mit strcmp, also genau - das bleibt auch hier so.
    if (key.size() > 64) {
        return nullptr;
    }
    for (const auto& [k, v] : keys) {
        if (k.size() != key.size()) {
            continue;
        }
        bool same = true;
        for (std::size_t i = 0; i < k.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(k[i])) !=
                std::tolower(static_cast<unsigned char>(key[i]))) {
                same = false;
                break;
            }
        }
        if (same) {
            return &v;
        }
    }
    return nullptr;
}

void parseEntities(const std::string& text, std::vector<MapEntity>& out) {
    out.clear();
    std::size_t i = 0;
    const std::size_t n = text.size();

    while (i < n) {
        // Bis zur naechsten oeffnenden Klammer
        while (i < n && text[i] != '{') {
            ++i;
        }
        if (i >= n) {
            break;
        }
        ++i;

        MapEntity e;
        while (i < n && text[i] != '}') {
            // Schluessel und Wert stehen jeweils in Anfuehrungszeichen.
            while (i < n && text[i] != '"' && text[i] != '}') {
                ++i;
            }
            if (i >= n || text[i] == '}') {
                break;
            }
            const std::size_t ks = ++i;
            while (i < n && text[i] != '"') {
                ++i;
            }
            if (i >= n) {
                break;
            }
            const std::string key = text.substr(ks, i - ks);
            ++i;

            while (i < n && text[i] != '"' && text[i] != '}') {
                ++i;
            }
            if (i >= n || text[i] == '}') {
                // Schluessel ohne Wert - halbe Zeile, kommt in von Hand
                // bearbeiteten Karten vor. Nicht abweisen, nur uebergehen.
                break;
            }
            const std::size_t vs = ++i;
            while (i < n && text[i] != '"') {
                ++i;
            }
            if (i >= n) {
                break;
            }
            const std::string value = text.substr(vs, i - vs);
            ++i;

            if (key == "classname") { e.classname = value; }
            else if (key == "targetname") { e.targetname = value; }
            else if (key == "cameraGroup") { e.cameraGroup = value; }
            else if (key == "origin") { e.origin = value; }
            e.keys.emplace_back(key, value);
        }
        if (i < n && text[i] == '}') {
            ++i;
        }
        if (!e.keys.empty()) {
            out.push_back(std::move(e));
        }
    }
}

bool readBsp(const std::string& bytes, MapData& out, std::string* error) {
    out = MapData{};
    // Kopf: Kennung (4) + Fassung (4) + Lumpverzeichnis. Lump 0 ist das
    // Entity-Lump, bei RBSP wie bei IBSP.
    if (bytes.size() < 16) {
        if (error != nullptr) { *error = "zu kurz fuer eine .bsp"; }
        return false;
    }
    out.magic = bytes.substr(0, 4);
    if (out.magic != "RBSP" && out.magic != "IBSP" && out.magic != "FBSP") {
        if (error != nullptr) { *error = "unbekannte Kennung: " + out.magic; }
        return false;
    }
    out.version = readInt(bytes, 4);

    const std::int32_t offset = readInt(bytes, 8);
    const std::int32_t length = readInt(bytes, 12);
    if (offset < 0 || length < 0 ||
        static_cast<std::size_t>(offset) + static_cast<std::size_t>(length) > bytes.size()) {
        if (error != nullptr) { *error = "Entity-Lump liegt ausserhalb der Datei"; }
        return false;
    }

    parseEntities(bytes.substr(static_cast<std::size_t>(offset),
                               static_cast<std::size_t>(length)),
                  out.entities);

    collectNames(out);
    // Was wirklich in der Karte steht - nicht nur, dass das Lesen klappte.
    //
    // shank: "hast du ueberall fuer alles debug output hinzugefuegt? das es
    // bei mapping, Model laden usw. ueberall debug aufschreibt."
    //
    // Bisher protokollierte nur die Oberflaeche, WAS sie angestossen hat.
    // Die Leser selbst schwiegen ueber das, was sie gelesen haben - und
    // genau das braucht man, wenn eine Karte anders aussieht als erwartet.
    diag::detail("BSP gelesen: " + out.magic + " Fassung " +
                 std::to_string(out.version) + ", " +
                 std::to_string(bytes.size()) + " Bytes, " +
                 std::to_string(out.entities.size()) + " Einheiten, " +
                 std::to_string(out.targetNames.size()) + " Zielnamen, " +
                 std::to_string(out.cameraGroups.size()) + " Kameragruppen, " +
                 std::to_string(out.scripts.size()) + " Skriptverweise");
    return true;
}

void collectNames(MapData& out) {
    out.targetNames.clear();
    out.cameraGroups.clear();
    out.scripts.clear();

    // Die Schluessel, unter denen eine Entity ein Skript nennt. Alle fuenf
    // kommen in Movie Duels vor; usescript und deathscript am haeufigsten.
    static const char* kScriptKeys[] = {"usescript", "spawnscript",
                                        "deathscript", "painscript",
                                        "awakescript", "angerscript",
                                        "lostenemyscript", "blockedscript"};

    for (const MapEntity& e : out.entities) {
        addUnique(out.targetNames, e.targetname);
        addUnique(out.cameraGroups, e.cameraGroup);

        for (const char* key : kScriptKeys) {
            const std::string* v = e.find(key);
            if (v == nullptr || v->empty()) {
                continue;
            }
            // Dubletten weglassen: dasselbe deathscript steht oft an
            // mehreren Wachen. In md_tfoaj_jedi.ent dreimal.
            bool known = false;
            for (const MapData::ScriptRef& r : out.scripts) {
                if (r.path == *v) {
                    known = true;
                }
            }
            if (known) {
                continue;
            }
            MapData::ScriptRef ref;
            ref.path = *v;
            ref.key = key;
            ref.owner = e.targetname.empty() ? e.classname : e.targetname;
            out.scripts.push_back(std::move(ref));
        }
    }
    std::sort(out.targetNames.begin(), out.targetNames.end());
    std::sort(out.cameraGroups.begin(), out.cameraGroups.end());
}

}  // namespace bhed
