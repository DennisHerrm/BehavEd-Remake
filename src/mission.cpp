#include "bhed/mission.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace bhed {
namespace {

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool endsWith(const std::string& s, const char* suffix) {
    const std::string low = lower(s);
    const std::string suf = lower(suffix);
    return low.size() >= suf.size() &&
           low.compare(low.size() - suf.size(), suf.size(), suf) == 0;
}

std::string baseName(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    const std::string file =
        (slash == std::string::npos) ? path : path.substr(slash + 1);
    const std::size_t dot = file.find_last_of('.');
    return (dot == std::string::npos) ? file : file.substr(0, dot);
}

}  // namespace

std::vector<Mission> findMissions(const std::vector<std::string>& fileNames,
                                  const FileReader& read) {
    std::vector<Mission> out;
    if (!read) {
        return out;
    }

    // Alle Namen einmal in Kleinschreibung, damit die Suche nach der .ent
    // nicht an der Schreibweise scheitert - in Movie Duels stehen Endungen
    // mal gross, mal klein.
    std::vector<std::string> lowered;
    lowered.reserve(fileNames.size());
    for (const std::string& n : fileNames) {
        lowered.push_back(lower(n));
    }
    auto exists = [&](const std::string& want) {
        const std::string w = lower(want);
        return std::find(lowered.begin(), lowered.end(), w) != lowered.end();
    };

    // Ueber die .ent gehen, nicht ueber die .bsp.
    //
    // Der Grund: ein Mod bringt mehrere Missionen mit, und die .bsp kann
    // dabei MEHRFACH auftauchen - dieselbe Karte unter verschiedenen Namen,
    // oder eine Karte, die mehrere Missionen traegt. Was eine Mission
    // ausmacht, ist die Entity-Datei: sie nennt die Skripte.
    //
    // Karten OHNE .ent kommen trotzdem in die Liste - dort stehen die
    // Entities in der .bsp, und manche Missionen arbeiten so.
    for (std::size_t i = 0; i < fileNames.size(); ++i) {
        const std::string& name = fileNames[i];
        if (!endsWith(name, ".bsp")) {
            continue;
        }

        Mission m;
        m.mapFile = name;
        m.name = baseName(name);

        // Die Entities: erst die .ent daneben, sonst die aus der .bsp.
        //
        // Die .ent hat Vorrang, und das ist keine Geschmacksfrage: Movie
        // Duels laesst die .bsp unveraendert und legt die Entities daneben.
        // Wer die .bsp nimmt, bekommt die Entities der Originalkarte.
        MapData map;
        const std::string entName = name.substr(0, name.size() - 4) + ".ent";
        std::string data;
        if (exists(entName) && read(entName, data)) {
            m.entFile = entName;
            parseEntities(data, map.entities);
            collectNames(map);
        } else if (read(name, data)) {
            (void)readBsp(data, map, nullptr);
        }

        for (const MapData::ScriptRef& r : map.scripts) {
            Mission::Script s;
            s.ref = r.path;
            s.key = r.key;
            // Auf eine wirklich vorhandene Datei aufloesen. Die Endung ist
            // nicht verlaesslich: .txt, .icarus und .ibi kommen vor, und
            // .IBI ebenso.
            for (const char* ext : {".txt", ".icarus", ".ibi"}) {
                const std::string candidate = "scripts/" + r.path + ext;
                if (exists(candidate)) {
                    s.file = candidate;
                    break;
                }
            }
            m.scripts.push_back(std::move(s));
        }

        out.push_back(std::move(m));
    }

    // Spielbare zuerst: eine Karte ohne Skripte ist eine Duellkarte.
    std::stable_sort(out.begin(), out.end(),
                     [](const Mission& a, const Mission& b) {
                         if (a.playable() != b.playable()) {
                             return a.playable();
                         }
                         return a.name < b.name;
                     });
    return out;
}

}  // namespace bhed
