#include "bhed/mission.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
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

// Die Verweise auf wirklich vorhandene Dateien aufloesen. Die Endung ist
// nicht verlaesslich: .txt, .icarus und .ibi kommen vor, und .IBI ebenso.
std::vector<Mission::Script> aufloesen(const std::vector<MapData::ScriptRef>& verweise,
                                       const DateiGibt& gibt) {
    std::vector<Mission::Script> out;
    out.reserve(verweise.size());
    for (const MapData::ScriptRef& r : verweise) {
        Mission::Script s;
        s.ref = r.path;
        s.key = r.key;
        for (const char* ext : {".txt", ".icarus", ".ibi"}) {
            const std::string candidate = "scripts/" + r.path + ext;
            if (gibt(candidate)) {
                s.file = candidate;
                break;
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

// Ein Pfad fuer den Vergleich: / statt Rueckstrich, klein, ohne Schraegstrich
// am Ende.
std::string pfadSchluessel(std::string p) {
    for (char& c : p) {
        c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    while (p.size() > 1 && p.back() == '/') {
        p.pop_back();
    }
    return p;
}

bool enthaelt(const std::vector<std::string>& ordner, const std::string& f) {
    return std::any_of(ordner.begin(), ordner.end(), [&](const std::string& o) { return gleicherOrdner(o, f); });
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

        m.scripts = aufloesen(map.scripts, exists);

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

// --- rc603: die Missionen eines ganzen Spiels ----------------------------------

std::vector<MissionsKarte> missionsKarten(const std::vector<SpielArchiv>& archive, const std::string& gewaehlt) {
    // Je Name die Datei, die die Engine laedt: RUECKWAERTS durch die Archive,
    // der erste Fund gewinnt (FS_AddGameDirectory haengt jedes Archiv vorn
    // ein, files.cpp:2758-2759; readFromArchives in gui/app.cpp sucht
    // genauso). Ohne Gross/Klein - FS_FilenameCompare.
    struct Fund {
        std::string name;
        std::size_t archiv = 0;
    };
    std::map<std::string, Fund> bsp;
    std::map<std::string, Fund> ent;
    std::unordered_set<std::string> imGewaehlten;
    const std::string gewaehltSchluessel = pfadSchluessel(gewaehlt);
    for (std::size_t i = archive.size(); i-- > 0;) {
        const bool istGewaehlt = !gewaehlt.empty() && pfadSchluessel(archive[i].pfad) == gewaehltSchluessel;
        for (const std::string& n : archive[i].namen) {
            if (n.size() < 9 || (n[0] != 'm' && n[0] != 'M')) {
                continue;   // "maps/x.bsp" hat mindestens 10 Zeichen
            }
            const std::string low = lower(n);
            if (low.rfind("maps/", 0) != 0) {
                continue;
            }
            const bool istBsp = endsWith(low, ".bsp");
            if (!istBsp && !endsWith(low, ".ent")) {
                continue;
            }
            (istBsp ? bsp : ent).emplace(low, Fund{n, i});
            if (istGewaehlt) {
                imGewaehlten.insert(low);
            }
        }
    }
    std::vector<MissionsKarte> out;
    out.reserve(bsp.size());
    for (const auto& [low, f] : bsp) {
        MissionsKarte k;
        k.name = baseName(f.name);
        k.bsp = f.name;
        k.bspArchiv = f.archiv;
        const std::string entLow = low.substr(0, low.size() - 4) + ".ent";
        const auto e = ent.find(entLow);
        if (e != ent.end()) {
            k.ent = e->second.name;
            k.entArchiv = e->second.archiv;
        }
        k.imGewaehlten = imGewaehlten.count(low) != 0U || imGewaehlten.count(entLow) != 0U;
        out.push_back(std::move(k));
    }
    return out;
}

std::unordered_set<std::string> skriptNamen(const std::vector<SpielArchiv>& archive) {
    std::unordered_set<std::string> out;
    for (const SpielArchiv& a : archive) {
        for (const std::string& n : a.namen) {
            if (n.size() > 8 && (n[0] == 's' || n[0] == 'S')) {
                std::string low = lower(n);
                if (low.rfind("scripts/", 0) == 0) {
                    out.insert(std::move(low));
                }
            }
        }
    }
    return out;
}

std::vector<MapData::ScriptRef> kartenSkriptVerweise(std::vector<MapEntity> ents) {
    MapData map;
    map.entities = std::move(ents);
    collectNames(map);
    return std::move(map.scripts);
}

Mission missionAus(const MissionsKarte& k, const std::vector<MapData::ScriptRef>& verweise, const DateiGibt& gibt) {
    Mission m;
    m.mapFile = k.bsp;
    m.entFile = k.ent;
    m.name = k.name;
    m.imGewaehlten = k.imGewaehlten;
    m.scripts = aufloesen(verweise, gibt);
    return m;
}

void sortiereMissionen(std::vector<Mission>& missionen) {
    std::stable_sort(missionen.begin(), missionen.end(), [](const Mission& a, const Mission& b) {
        if (a.imGewaehlten != b.imGewaehlten) {
            return a.imGewaehlten;
        }
        if (a.playable() != b.playable()) {
            return a.playable();
        }
        return a.name < b.name;
    });
}

bool leseSkriptVerweise(const MissionsAuftrag& a, std::vector<MapData::ScriptRef>& out) {
    out.clear();
    std::string daten;
    std::vector<MapEntity> ents;
    // Die .ent hat Vorrang - wie in findMissions und loadMission.
    if (!a.entArchiv.empty()) {
        Pk3 arc;
        arc.path = a.entArchiv;
        if (readPk3File(arc, a.ent, daten)) {
            parseEntities(daten, ents);
            out = kartenSkriptVerweise(std::move(ents));
            return true;
        }
    }
    Pk3 arc;
    arc.path = a.bspArchiv;
    if (!readPk3File(arc, a.bsp, daten)) {
        return false;
    }
    std::string text;
    if (!readBspEntityLump(daten, text)) {
        return false;
    }
    parseEntities(text, ents);
    out = kartenSkriptVerweise(std::move(ents));
    return true;
}

MissionsSuche::MissionsSuche(std::vector<MissionsAuftrag> auftraege, unsigned faeden)
    : auftraege_(std::move(auftraege)),
      verweise_(auftraege_.size()),
      gelesen_(auftraege_.size(), 0),
      bearbeitet_(auftraege_.size(), 0) {
    const unsigned n = std::max(1U, std::min<unsigned>(faeden, static_cast<unsigned>(std::max<std::size_t>(1, auftraege_.size()))));
    if (auftraege_.empty()) {
        return;
    }
    faeden_.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        faeden_.emplace_back([this] { arbeite(); });
    }
}

MissionsSuche::~MissionsSuche() { abbrechen(); }

void MissionsSuche::arbeite() {
    for (;;) {
        if (halt_.load()) {
            return;
        }
        const std::size_t i = naechster_.fetch_add(1);
        if (i >= auftraege_.size()) {
            return;
        }
        // Jeder Platz gehoert genau einem Faden; gelesen wird erst nach
        // fertig() (erledigt_ zaehlt mit Freigabe-Semantik hoch).
        gelesen_[i] = leseSkriptVerweise(auftraege_[i], verweise_[i]) ? 1 : 0;
        bearbeitet_[i] = 1;
        erledigt_.fetch_add(1);
    }
}

void MissionsSuche::abbrechen() {
    halt_.store(true);
    for (std::thread& t : faeden_) {
        if (t.joinable()) {
            t.join();
        }
    }
    faeden_.clear();
}

// --- rc603: Spielordner zu einem Archiv ----------------------------------------

bool gleicherOrdner(const std::string& a, const std::string& b) { return pfadSchluessel(a) == pfadSchluessel(b); }

std::string ordnerVonDatei(const std::string& pfad) {
    const std::size_t s = pfad.find_last_of("/\\");
    return s == std::string::npos ? std::string{} : pfad.substr(0, s);
}

SpielordnerWahl spielordnerFuerArchiv(const std::string& archivPfad, const std::vector<std::string>& eingerichtet,
                                      const std::vector<std::string>& sitzung,
                                      const std::function<bool(const std::string& ordner)>& hatPk3) {
    SpielordnerWahl w;
    const std::string f = ordnerVonDatei(archivPfad);
    if (enthaelt(eingerichtet, f)) {
        w.art = SpielordnerWahl::Art::Eingerichtet;
        w.ordner = eingerichtet;
        return w;
    }
    if (!sitzung.empty() && enthaelt(sitzung, f)) {
        w.art = SpielordnerWahl::Art::Sitzung;
        w.ordner = sitzung;
        return w;
    }
    // Der Name des Ordners und der Ordner darueber.
    std::string ohneEnde = f;
    while (ohneEnde.size() > 1 && (ohneEnde.back() == '/' || ohneEnde.back() == '\\')) {
        ohneEnde.pop_back();
    }
    const std::size_t s = ohneEnde.find_last_of("/\\");
    const std::string name = s == std::string::npos ? ohneEnde : ohneEnde.substr(s + 1);
    // BASEGAME "base" (q_shared.h): FS_Startup laedt ihn allein, wenn kein
    // fs_game gesetzt ist (files.cpp:2897-2902).
    if (lower(name) == "base") {
        w.art = SpielordnerWahl::Art::Installation;
        w.ordner = {f};
        return w;
    }
    // Eine Mod derselben Installation: fs_basepath/base, dann
    // fs_basepath/<fs_game> (files.cpp:2902, 2936-2941).
    if (s != std::string::npos) {
        const char trenner = ohneEnde[s];
        const std::string basis = ohneEnde.substr(0, s) + trenner + "base";
        if (hatPk3 && hatPk3(basis)) {
            w.art = SpielordnerWahl::Art::Installation;
            w.ordner = {basis, f};
            return w;
        }
    }
    // Eine lose Mod: auf dem eingerichteten Spiel, als gewinnender Ordner.
    w.art = SpielordnerWahl::Art::Lose;
    w.ordner = eingerichtet;
    w.ordner.push_back(f);
    return w;
}

}  // namespace bhed
