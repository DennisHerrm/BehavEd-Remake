// moveranimtest.cpp - Schluesselstellen <-> affect-Block, und was die Szene daraus macht
#include "bhed/moveranim.h"
#include "bhed/scene.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

using namespace bhed;

namespace {
int g_fehler = 0;
void pruefe(bool ok, const std::string& was) {
    std::printf("%s %s\n", ok ? "OK    " : "FEHLER", was.c_str());
    if (!ok) { ++g_fehler; }
}
bool nah(float a, float b) { return std::fabs(a - b) < 0.01F; }
}  // namespace

int main() {
    std::vector<AnimSchluessel> k(3);
    k[0].ms = 1500.0;
    k[0].ort = {100.0F, 200.0F, 300.0F};
    k[1].ms = 3500.0;
    k[1].ort = {600.0F, 200.0F, 400.0F};
    k[1].winkel = {0.0F, 90.0F, 0.0F};
    k[2].ms = 4000.0;
    k[2].ort = {600.0F, 900.0F, 400.0F};
    k[2].winkel = {10.0F, 180.0F, 0.0F};

    const auto block = baueAnimBlock("schiff", k);
    pruefe(block.size() == 2U && block[0].kind == Node::Kind::LineComment && block[1].name == "affect" &&
               block[1].args.size() >= 1 && block[1].args[0].text == "schiff",
           "Block: Markierung und affect ( \"schiff\" )");
    Script s;
    s.nodes.push_back(Node{});   // eine Leerzeile vorn
    s.nodes.back().kind = Node::Kind::Blank;
    Script mit = mitAnimBlock(s, "schiff", k);
    const auto at = findeAnimBlock(mit, "schiff");
    pruefe(at.has_value() && *at == 0U && mit.nodes.size() == 3U, "der Block steht am Anfang und wird gefunden");
    const auto zur = leseAnimBlock(mit, *at);
    bool gleich = zur.size() == k.size();
    for (std::size_t i = 0; gleich && i < k.size(); ++i) {
        gleich = std::fabs(zur[i].ms - k[i].ms) < 0.5 && nah(zur[i].ort[0], k[i].ort[0]) && nah(zur[i].ort[1], k[i].ort[1]) &&
                 nah(zur[i].ort[2], k[i].ort[2]) && nah(zur[i].winkel[1], k[i].winkel[1]);
    }
    pruefe(gleich, "zurueckgelesen: dieselben Zeiten, Orte und Winkel");
    // Ein Schluessel geaendert: der Block wird ERSETZT, nicht verdoppelt.
    k[1].ort[2] = 999.0F;
    Script mit2 = mitAnimBlock(mit, "schiff", k);
    pruefe(mit2.nodes.size() == 3U && nah(leseAnimBlock(mit2, 0)[1].ort[2], 999.0F), "geaendert: ersetzt, nicht verdoppelt");
    // Zwei Schiffe nebeneinander
    Script zwei = mitAnimBlock(mit2, "zweites", {k[0]});
    pruefe(findeAnimBlock(zwei, "schiff").has_value() && findeAnimBlock(zwei, "zweites").has_value() &&
               zwei.nodes.size() == 5U,
           "zwei Bloecke fuer zwei Schiffe");
    // Ohne Schluessel faellt der Block weg
    Script leer = mitAnimBlock(zwei, "schiff", {});
    pruefe(!findeAnimBlock(leer, "schiff").has_value() && findeAnimBlock(leer, "zweites").has_value(),
           "ohne Schluessel: der Block ist weg, der andere bleibt");
    // Rundlauf ueber Text
    const std::string text = writeScript(mit2);
    Script gelesen;
    std::vector<Diag> d;
    pruefe(readScript(text, gelesen, d) && findeAnimBlock(gelesen, "schiff").has_value() &&
               leseAnimBlock(gelesen, *findeAnimBlock(gelesen, "schiff")).size() == 3U,
           "geschrieben und wieder gelesen: der Block ist wiederzuerkennen");
    // Was die Szene daraus macht: bei 3,5 s steht das Schiff am zweiten Schluessel.
    {
        MapData karte;
        MapEntity e;
        e.classname = "func_static";
        e.targetname = "schiff";
        e.origin = "100 200 300";
        e.keys = {{"classname", "func_static"}, {"script_targetname", "schiff"}, {"origin", "100 200 300"}};
        karte.entities.push_back(e);
        const Scene sc = buildScene(mit2, karte);
        const Actor* a = nullptr;
        for (const Actor& x : sc.actors) {
            if (x.name == "schiff") { a = &x; }
        }
        pruefe(a != nullptr, "die Szene kennt das Schiff");
        if (a != nullptr) {
            const ActorState s1 = a->at(1500.0);
            const ActorState s2 = a->at(3500.0);
            const ActorState s3 = a->at(4200.0);
            std::printf("       bei 1,5 s %.0f %.0f %.0f, bei 3,5 s %.0f %.0f %.0f, bei 4,2 s %.0f %.0f %.0f\n",
                        static_cast<double>(s1.pos[0]), static_cast<double>(s1.pos[1]), static_cast<double>(s1.pos[2]),
                        static_cast<double>(s2.pos[0]), static_cast<double>(s2.pos[1]), static_cast<double>(s2.pos[2]),
                        static_cast<double>(s3.pos[0]), static_cast<double>(s3.pos[1]), static_cast<double>(s3.pos[2]));
            pruefe(std::fabs(s1.pos[0] - 100.0F) < 1.0F && std::fabs(s2.pos[0] - 600.0F) < 1.0F &&
                       std::fabs(s2.pos[2] - 999.0F) < 1.0F && std::fabs(s3.pos[1] - 900.0F) < 1.0F,
                   "die Szene faehrt die Schluessel zur richtigen Zeit an");
        }
    }
    std::printf("\n%s: %d Fehler\n", g_fehler == 0 ? "moveranim OK" : "moveranim FEHLER", g_fehler);
    return g_fehler == 0 ? 0 : 1;
}
