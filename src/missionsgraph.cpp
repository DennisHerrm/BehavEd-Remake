// missionsgraph.cpp - siehe missionsgraph.h
#include "bhed/missionsgraph.h"

#include "bhed/npcklasse.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace bhed {
namespace {

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

bool istNpc(const std::string& c) { return klein(c).rfind("npc_", 0) == 0; }

// Diese Werte nimmt die Engine statt eines Skripts (BSTATE_STRINGS).
bool istVerhalten(const std::string& v) {
    static const std::set<std::string> kNamen = {
        "default", "idle", "roam", "walk", "run", "standshoot", "standguard", "patrol", "huntkill", "evade",
        "evadeshoot", "runshoot", "defend", "snipe", "combat", "medic", "takecover", "getammo", "advancefight",
        "face", "wait", "investigate", "follow_leader", "sleep", "flee", "cinematic", "jump", "search", "noclip",
        "remove", "shoot"};
    return v.find('/') == std::string::npos && kNamen.count(klein(v)) != 0;
}

GraphKnoten::Art artVon(const MapEntity& e) {
    if (e.classname == "info_player_start") { return GraphKnoten::Art::Spielerstart; }
    if (e.classname == "target_scriptrunner") { return GraphKnoten::Art::Skriptstarter; }
    if (istNpc(e.classname)) { return GraphKnoten::Art::Figur; }
    if (e.classname == "target_level_change") { return GraphKnoten::Art::Kartenwechsel; }
    if (e.classname.rfind("trigger_", 0) == 0) { return GraphKnoten::Art::Ausloeser; }
    return GraphKnoten::Art::Entity;
}

// Unter welchem Namen ein Skript die Entity anspricht.
std::vector<std::string> namenVon(const MapEntity& e) {
    std::vector<std::string> n;
    if (!e.targetname.empty()) { n.push_back(e.targetname); }
    if (const std::string* s = e.find("script_targetname"); s != nullptr && !s->empty()) { n.push_back(*s); }
    if (istNpc(e.classname)) {
        if (const std::string* s = e.find("NPC_targetname"); s != nullptr && !s->empty()) { n.push_back(*s); }
    }
    return n;
}

std::string anzeigeName(const MapEntity& e) {
    if (istNpc(e.classname)) {
        if (const std::string* s = e.find("NPC_targetname"); s != nullptr && !s->empty()) { return *s; }
    }
    if (const std::string* s = e.find("script_targetname"); s != nullptr && !s->empty()) { return *s; }
    return e.targetname.empty() ? e.classname : e.targetname;
}

// Die Befehle, die auf andere zeigen, aus einem Skript (auch in Bloecken).
struct Verweis {
    std::string befehl;   // use, affect, run, kill, remove
    std::string ziel;
};

void sammle(const std::vector<Node>& nodes, std::vector<Verweis>& aus, int& befehle, int& kameras, int& saetze) {
    for (const Node& n : nodes) {
        if (n.kind != Node::Kind::Command) { continue; }
        ++befehle;
        const std::string b = klein(n.name);
        if (b == "camera" && !n.args.empty() && klein(n.args[0].text) == "move") { ++kameras; }
        if (b == "sound" && !n.args.empty() && klein(n.args[0].text).rfind("chan_voice", 0) == 0) { ++saetze; }
        if ((b == "use" || b == "affect" || b == "run" || b == "kill" || b == "remove") && !n.args.empty() &&
            n.args[0].kind == Arg::Kind::String && !n.args[0].text.empty()) {
            aus.push_back({b, n.args[0].text});
        }
        sammle(n.children, aus, befehle, kameras, saetze);
    }
}

}  // namespace

MissionsGraph baueMissionsGraph(const std::vector<MapEntity>& ents, const GraphSkriptQuelle& skript,
                                const std::string& startSkript, std::size_t maxKnoten, const std::vector<int>& immer) {
    MissionsGraph g;
    // Namen -> Entities (mehrere duerfen denselben Namen tragen).
    std::map<std::string, std::vector<int>> namen;
    for (std::size_t i = 0; i < ents.size(); ++i) {
        for (const std::string& n : namenVon(ents[i])) { namen[klein(n)].push_back(static_cast<int>(i)); }
    }
    std::map<int, int> entityKnoten;          // Entity-Index -> Knoten
    std::map<std::string, int> skriptKnoten;  // Pfad (klein) -> Knoten
    const auto knotenFuerEntity = [&](int i) {
        if (const auto it = entityKnoten.find(i); it != entityKnoten.end()) { return it->second; }
        if (g.knoten.size() >= maxKnoten) {
            g.gekuerzt = true;
            return -1;
        }
        const MapEntity& e = ents[static_cast<std::size_t>(i)];
        GraphKnoten k;
        k.art = artVon(e);
        k.entity = i;
        k.titel = anzeigeName(e);
        k.unter = e.classname;
        if (istNpc(e.classname)) {
            // rc602: der wirksame Typ - auch aus der Klasse (NPC_Droid_R2D2 -> r2d2).
            if (const std::string t = effektiverNpcTyp(e); !t.empty()) { k.unter = t; }
        }
        if (k.art == GraphKnoten::Art::Kartenwechsel) {
            if (const std::string* m = e.find("mapname"); m != nullptr && !m->empty()) { k.unter = "-> " + *m; }
        }
        k.schluessel = "e:" + std::to_string(i);
        g.knoten.push_back(k);
        entityKnoten[i] = static_cast<int>(g.knoten.size()) - 1;
        return entityKnoten[i];
    };
    std::vector<std::string> offen;   // Skripte, deren Inhalt noch zu lesen ist
    const auto knotenFuerSkript = [&](const std::string& pfad) {
        const std::string k0 = klein(pfad);
        if (const auto it = skriptKnoten.find(k0); it != skriptKnoten.end()) { return it->second; }
        if (g.knoten.size() >= maxKnoten) {
            g.gekuerzt = true;
            return -1;
        }
        GraphKnoten k;
        k.art = GraphKnoten::Art::Skript;
        k.skript = pfad;
        const auto strich = pfad.find_last_of('/');
        k.titel = strich == std::string::npos ? pfad : pfad.substr(strich + 1);
        k.schluessel = "s:" + k0;
        g.knoten.push_back(k);
        skriptKnoten[k0] = static_cast<int>(g.knoten.size()) - 1;
        offen.push_back(pfad);
        return skriptKnoten[k0];
    };
    const auto kante = [&](int von, int nach, GraphKante::Art art, const std::string& label) {
        if (von < 0 || nach < 0) { return; }
        for (const GraphKante& k : g.kanten) {
            if (k.von == von && k.nach == nach && k.label == label) { return; }
        }
        g.kanten.push_back({von, nach, art, label});
    };

    // In den Graphen gezogene Entities zuerst - sie sollen auch ohne
    // Verbindung dastehen.
    for (const int i : immer) {
        if (i >= 0 && static_cast<std::size_t>(i) < ents.size()) { (void)knotenFuerEntity(i); }
    }
    // Entity -> Skript (*script) zuerst, dann was fuer die Mission zaehlt.
    //
    // Die target-Verbindungen zwischen Entities kommen erst ZUM SCHLUSS (unten):
    // auf grossen Karten (taspir1, hunderte Wegpunkte in target-Ketten) fuellten
    // sie sonst alle Plaetze, bevor die Skripte drankamen - das Intro fehlte
    // im Graphen (Handbuch-Fotolauf rc601: "400 nodes (shortened)").
    for (std::size_t i = 0; i < ents.size(); ++i) {
        const MapEntity& e = ents[i];
        for (const auto& [k, v] : e.keys) {
            const std::string kl = klein(k);
            if (kl.size() <= 6 || kl.compare(kl.size() - 6, 6, "script") != 0 || v.empty() || istVerhalten(v)) {
                continue;
            }
            // Erst die Entity, dann das Skript - die Reihenfolge der Argumente
            // waere nicht festgelegt, und die der Knoten soll es sein.
            const int von = knotenFuerEntity(static_cast<int>(i));
            kante(von, knotenFuerSkript(v), GraphKante::Art::Skript, kl);
        }
    }
    if (!startSkript.empty()) { (void)knotenFuerSkript(startSkript); }
    // Was fuer eine Mission zaehlt, steht immer da - auch unverbunden, damit
    // man es im Graph anschliessen kann (eine frisch gesetzte Figur).
    for (std::size_t i = 0; i < ents.size(); ++i) {
        if (artVon(ents[i]) != GraphKnoten::Art::Entity) { (void)knotenFuerEntity(static_cast<int>(i)); }
    }

    // Skripte lesen: use / affect / run / kill / remove.
    for (std::size_t pos = 0; pos < offen.size(); ++pos) {
        const std::string pfad = offen[pos];
        const int von = skriptKnoten[klein(pfad)];
        const Script* sc = skript ? skript(pfad) : nullptr;
        if (sc == nullptr) {
            g.knoten[static_cast<std::size_t>(von)].unter = "?";
            continue;
        }
        std::vector<Verweis> v;
        int befehle = 0;
        int kameras = 0;
        int saetze = 0;
        sammle(sc->nodes, v, befehle, kameras, saetze);
        std::string unter = std::to_string(befehle) + " cmd";
        if (kameras > 0) { unter += " / " + std::to_string(kameras) + " cam"; }
        if (saetze > 0) { unter += " / " + std::to_string(saetze) + " voice"; }
        g.knoten[static_cast<std::size_t>(von)].unter = unter;
        for (const Verweis& w : v) {
            if (w.befehl == "run") {
                kante(von, knotenFuerSkript(w.ziel), GraphKante::Art::Skript, "run");
                continue;
            }
            const auto it = namen.find(klein(w.ziel));
            if (it == namen.end()) { continue; }
            for (const int z : it->second) {
                kante(von, knotenFuerEntity(z),
                      w.befehl == "use" ? GraphKante::Art::Ausloesen : GraphKante::Art::Steuern, w.befehl);
            }
        }
    }
    // Entity -> Entity (target): nur, wo eine Seite schon zur Mission gehoert
    // (ein Knoten von oben). Eine Kette von Wegpunkten ohne Bezug bleibt
    // draussen; der erste Wegpunkt hinter einer Figur kommt mit.
    std::set<int> dabei;
    for (const auto& [i, k] : entityKnoten) {
        (void)k;
        dabei.insert(i);
    }
    for (std::size_t i = 0; i < ents.size(); ++i) {
        const MapEntity& e = ents[i];
        for (const char* tk : {"target", "target2", "target3", "target4", "NPC_target"}) {
            const std::string* t = e.find(tk);
            if (t == nullptr || t->empty() || e.classname == "target_level_change") { continue; }
            const auto it = namen.find(klein(*t));
            if (it == namen.end()) { continue; }
            for (const int z : it->second) {
                if (z == static_cast<int>(i)) { continue; }
                if (dabei.count(static_cast<int>(i)) == 0 && dabei.count(z) == 0) { continue; }
                const int von = knotenFuerEntity(static_cast<int>(i));
                kante(von, knotenFuerEntity(z), GraphKante::Art::Ausloesen, tk);
            }
        }
    }
    return g;
}

void ordneGraph(MissionsGraph& g, float breite, float hoehe, float abstandX, float abstandY) {
    const std::size_t n = g.knoten.size();
    if (n == 0) { return; }
    std::vector<std::vector<int>> nach(n);
    std::vector<std::vector<int>> von(n);
    for (const GraphKante& k : g.kanten) {
        nach[static_cast<std::size_t>(k.von)].push_back(k.nach);
        von[static_cast<std::size_t>(k.nach)].push_back(k.von);
    }
    // Spalte = laengster Weg von einer Quelle; Kreise werden beim Besuch
    // gebrochen (eine Kante zurueck zaehlt nicht).
    std::vector<int> spalte(n, 0);
    std::vector<int> zustand(n, 0);   // 0 neu, 1 in Arbeit, 2 fertig
    std::vector<int> reihenfolge;
    std::function<void(int)> besuche = [&](int v) {
        zustand[static_cast<std::size_t>(v)] = 1;
        for (const int w : nach[static_cast<std::size_t>(v)]) {
            if (zustand[static_cast<std::size_t>(w)] == 0) { besuche(w); }
        }
        zustand[static_cast<std::size_t>(v)] = 2;
        reihenfolge.push_back(v);
    };
    // Erst von den Quellen aus (ohne Vorgaenger), dann den Rest.
    for (std::size_t v = 0; v < n; ++v) {
        if (von[v].empty() && zustand[v] == 0) { besuche(static_cast<int>(v)); }
    }
    for (std::size_t v = 0; v < n; ++v) {
        if (zustand[v] == 0) { besuche(static_cast<int>(v)); }
    }
    std::reverse(reihenfolge.begin(), reihenfolge.end());   // topologisch (bis auf Kreise)
    std::vector<int> rang(n, 0);
    for (std::size_t i = 0; i < reihenfolge.size(); ++i) { rang[static_cast<std::size_t>(reihenfolge[i])] = static_cast<int>(i); }
    for (const int v : reihenfolge) {
        for (const int w : nach[static_cast<std::size_t>(v)]) {
            if (rang[static_cast<std::size_t>(w)] > rang[static_cast<std::size_t>(v)]) {
                spalte[static_cast<std::size_t>(w)] = std::max(spalte[static_cast<std::size_t>(w)], spalte[static_cast<std::size_t>(v)] + 1);
            }
        }
    }
    // Zusammenhaengende Teile, damit sie untereinander stehen.
    std::vector<int> teil(n, -1);
    int teile = 0;
    for (std::size_t s = 0; s < n; ++s) {
        if (teil[s] >= 0) { continue; }
        std::vector<int> stapel{static_cast<int>(s)};
        teil[s] = teile;
        while (!stapel.empty()) {
            const int v = stapel.back();
            stapel.pop_back();
            for (const auto* liste : {&nach[static_cast<std::size_t>(v)], &von[static_cast<std::size_t>(v)]}) {
                for (const int w : *liste) {
                    if (teil[static_cast<std::size_t>(w)] < 0) {
                        teil[static_cast<std::size_t>(w)] = teile;
                        stapel.push_back(w);
                    }
                }
            }
        }
        ++teile;
    }
    // Groessere Teile zuerst (die Mission selbst), einzelne Knoten zuletzt.
    std::vector<int> groesse(static_cast<std::size_t>(teile), 0);
    for (std::size_t v = 0; v < n; ++v) { ++groesse[static_cast<std::size_t>(teil[v])]; }
    std::vector<int> teilFolge(static_cast<std::size_t>(teile));
    for (int t = 0; t < teile; ++t) { teilFolge[static_cast<std::size_t>(t)] = t; }
    std::stable_sort(teilFolge.begin(), teilFolge.end(), [&](int a, int b) {
        return groesse[static_cast<std::size_t>(a)] > groesse[static_cast<std::size_t>(b)];
    });
    float yTeil = 0.0F;
    for (const int t : teilFolge) {
        // Spalten dieses Teils, Zeilen nach dem Mittel der Vorgaenger.
        std::map<int, std::vector<int>> spalten;
        for (std::size_t v = 0; v < n; ++v) {
            if (teil[v] == t) { spalten[spalte[v]].push_back(static_cast<int>(v)); }
        }
        std::vector<float> zeile(n, 0.0F);
        int hoechste = 0;
        for (auto& [s, liste] : spalten) {
            (void)s;
            std::stable_sort(liste.begin(), liste.end(), [&](int a, int b) {
                const auto mittel = [&](int v) {
                    float summe = 0.0F;
                    int k = 0;
                    for (const int w : von[static_cast<std::size_t>(v)]) {
                        if (spalte[static_cast<std::size_t>(w)] < spalte[static_cast<std::size_t>(v)]) {
                            summe += zeile[static_cast<std::size_t>(w)];
                            ++k;
                        }
                    }
                    return k > 0 ? summe / static_cast<float>(k) : 1e9F;
                };
                return mittel(a) < mittel(b);
            });
            for (std::size_t i = 0; i < liste.size(); ++i) { zeile[static_cast<std::size_t>(liste[i])] = static_cast<float>(i); }
            hoechste = std::max(hoechste, static_cast<int>(liste.size()));
        }
        for (std::size_t v = 0; v < n; ++v) {
            if (teil[v] != t) { continue; }
            GraphKnoten& k = g.knoten[v];
            k.spalte = spalte[v];
            k.zeile = static_cast<int>(zeile[v]);
            k.x = static_cast<float>(k.spalte) * (breite + abstandX);
            k.y = yTeil + static_cast<float>(k.zeile) * (hoehe + abstandY);
        }
        yTeil += static_cast<float>(hoechste) * (hoehe + abstandY) + abstandY;
    }
}

}  // namespace bhed
