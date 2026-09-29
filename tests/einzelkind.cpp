#include "bhed/edit.h"
#include "bhed/script.h"
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
static std::string flach(const bhed::Script& s) {
    std::string o;
    const std::function<void(const std::vector<bhed::Node>&)> g =
        [&](const std::vector<bhed::Node>& ns) {
            for (const bhed::Node& n : ns) {
                if (!o.empty()) o += " ";
                o += n.name;
                if (n.hasBlock) { o += "{"; g(n.children); o += " }"; }
            }
        };
    g(s.nodes); return o;
}
// Ein Block mit EINEM einzigen Kind.
//
// shank: "ich kann es immer noch nicht raus moven wenn es alleine ist."
//
// Nachgestellt: es geht ueber alle drei Wege. Diese Probe haelt das fest,
// damit die Frage nicht ein fuenftes Mal aufkommt.
int main() {
    int fehler = 0;
    // affect { a }   -- EIN einziges Kind, sonst nichts
    bhed::Node a; a.name = "a";
    bhed::Node aff; aff.name = "affect"; aff.hasBlock = true; aff.children = {a};
    bhed::Script s; s.nodes = {aff};

    { bhed::Document d{s}; bhed::Path n;
      bool ok = d.moveUp(bhed::Path{0,0}, &n);
      if (!ok || flach(d.script()) != "a affect{ }") {
          std::printf("  FEHL  moveUp: %s\n", flach(d.script()).c_str());
          ++fehler; } }
    { bhed::Document d{s}; bhed::Path n;
      bool ok = d.moveDown(bhed::Path{0,0}, &n);
      if (!ok || flach(d.script()) != "affect{ } a") {
          std::printf("  FEHL  moveDown: %s\n", flach(d.script()).c_str());
          ++fehler; } }
    { bhed::Document d{s};
      bool ok = d.moveTo(bhed::Path{0,0}, bhed::Path{0});
      if (!ok || flach(d.script()) != "affect{ } a") {
          std::printf("  FEHL  ziehen: %s\n", flach(d.script()).c_str());
          ++fehler; } }
    if (fehler != 0) { std::printf("FEHLGESCHLAGEN (%d)\n", fehler); return 1; }
    std::printf("Einzelkind-Proben bestanden (0 Fehlschlaege)\n");
    return 0;
}
