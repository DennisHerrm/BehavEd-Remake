// shaderabgleich.cpp - alle Shader der Spielordner gegen unseren Leser
//
// Aufruf:  shaderabgleich <spielordner> [<spielordner> ...]
//
// Liest jede .shader-Datei aus allen .pk3 der Ordner und
//
//   1. zaehlt, welche Schluesselwoerter WIRKLICH vorkommen - auf Shader- und
//      auf Stufenebene, dazu die Werte von rgbGen, alphaGen, tcGen, tcMod und
//      deformVertexes. Verglichen mit der Liste, die der JKA-Renderer
//      auswertet (OpenJK code/rd-vanilla/tr_shader.cpp, ParseShader und
//      ParseStage), zeigt das, was bei uns fehlt und wie oft es gebraucht
//      wird;
//   2. prueft das Gluehen: jede Stufe mit "glow" muss bei uns als
//      Gluehstufe ankommen, und der Shader muss hasGlow tragen (wie
//      shader.hasGlow in OpenJK).
//
// Keine ctest-Probe: sie braucht die Spieldaten.
#include "bhed/pk3.h"
#include "bhed/shaderscript.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

// Zeilenweise zerlegen, Kommentare weg, Klammern als eigene Zeichen.
std::vector<std::vector<std::string>> zeilen(const std::string& text) {
    std::vector<std::vector<std::string>> out;
    std::vector<std::string> zeile;
    std::string wort;
    bool blockKommentar = false;
    auto wortFertig = [&] {
        if (!wort.empty()) { zeile.push_back(wort); wort.clear(); }
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (blockKommentar) {
            if (c == '*' && i + 1 < text.size() && text[i + 1] == '/') { blockKommentar = false; ++i; }
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') { ++i; }
            wortFertig();
            if (!zeile.empty()) { out.push_back(zeile); zeile.clear(); }
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') { blockKommentar = true; ++i; continue; }
        if (c == '\n') {
            wortFertig();
            if (!zeile.empty()) { out.push_back(zeile); zeile.clear(); }
            continue;
        }
        if (c == '{' || c == '}') {
            wortFertig();
            if (!zeile.empty()) { out.push_back(zeile); zeile.clear(); }
            out.push_back({std::string(1, c)});
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) { wortFertig(); continue; }
        wort += c;
    }
    wortFertig();
    if (!zeile.empty()) { out.push_back(zeile); }
    return out;
}

// Was der JKA-Renderer auswertet (OpenJK tr_shader.cpp). qer* und
// q3map* ueberliest er bis auf q3map_material/q3map_sun*.
const std::set<std::string> kShaderWorte = {
    "surfaceparm", "material", "q3map_material", "nomipmaps", "nopicmip", "notc", "noglfog",
    "cull", "sort", "portal", "polygonoffset", "entitymergable", "deformvertexes", "deform",
    "skyparms", "fogparms", "sun", "q3map_sun", "q3map_sunext", "surfacelight",
    "q3map_surfacelight", "lightcolor", "hitlocation", "hitmaterial", "tesssize", "clamptime", "light"};
const std::set<std::string> kStufenWorte = {
    "map", "clampmap", "animmap", "clampanimmap", "oneshotanimmap", "videomap", "blendfunc",
    "alphafunc", "depthfunc", "depthwrite", "rgbgen", "alphagen", "texgen", "tcgen", "tcmod",
    "detail", "glow", "surfacesprites"};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Aufruf: shaderabgleich <spielordner> [...]\n");
        return 2;
    }
    std::map<std::string, int> shaderWorte;
    std::map<std::string, int> stufenWorte;
    std::map<std::string, int> werte;   // "rgbgen vertex", "tcmod scroll", ...
    int dateien = 0;
    int shaderZahl = 0;
    int gluehStufen = 0;
    int gluehFehlt = 0;
    int gluehShaderFehlt = 0;
    int gluehOhneBild = 0;
    std::vector<std::string> gluehBeispiele;

    for (int a = 1; a < argc; ++a) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(fs::u8path(argv[a]), ec)) {
            if (klein(e.path().extension().string()) != ".pk3") { continue; }
            bhed::Pk3 arc;
            if (!bhed::readPk3Directory(e.path().string(), arc)) { continue; }
            for (const bhed::Pk3Entry& en : arc.entries) {
                const std::string n = klein(en.name);
                if (n.rfind("shaders/", 0) != 0 || n.size() < 8 ||
                    n.compare(n.size() - 7, 7, ".shader") != 0) {
                    continue;
                }
                std::string text;
                if (!bhed::readPk3File(arc, en, text)) { continue; }
                ++dateien;

                // 1. Zaehlen, und je Shader festhalten, welche Stufen gluehen.
                std::map<std::string, std::vector<bool>> gluehSoll;
                int tiefe = 0;
                std::string name;
                std::string vorigeZeile;
                std::vector<bool>* stufen = nullptr;
                // Hat die Stufe ein Bild? Ohne schaltet die Engine sie ab
                // (FinishShader: "has a stage with no image").
                std::map<std::string, std::vector<bool>> bildDa;
                std::vector<bool>* bilder = nullptr;
                for (const auto& z : zeilen(text)) {
                    if (z[0] == "{") {
                        ++tiefe;
                        if (tiefe == 1) {
                            name = klein(vorigeZeile);
                            stufen = &gluehSoll[name];
                            stufen->clear();
                            bilder = &bildDa[name];
                            bilder->clear();
                            ++shaderZahl;
                        } else if (tiefe == 2 && stufen != nullptr) {
                            stufen->push_back(false);
                            bilder->push_back(false);
                        }
                        continue;
                    }
                    if (z[0] == "}") { tiefe = std::max(0, tiefe - 1); continue; }
                    if (tiefe == 0) { vorigeZeile = z[0]; continue; }
                    const std::string w = klein(z[0]);
                    if (tiefe == 1) {
                        if (w.rfind("qer", 0) == 0) { continue; }
                        ++shaderWorte[w];
                        if ((w == "deformvertexes" || w == "deform") && z.size() > 1) {
                            ++werte["deformvertexes " + klein(z[1])];
                        }
                    } else if (tiefe == 2) {
                        ++stufenWorte[w];
                        if ((w == "rgbgen" || w == "alphagen" || w == "tcgen" || w == "texgen" ||
                             w == "tcmod") && z.size() > 1) {
                            ++werte[(w == "texgen" ? std::string("tcgen") : w) + " " + klein(z[1])];
                        }
                        if (w == "glow" && stufen != nullptr && !stufen->empty()) {
                            stufen->back() = true;
                        }
                        if ((w == "map" || w == "clampmap" || w == "animmap" ||
                             w == "clampanimmap" || w == "oneshotanimmap" ||
                             w == "videomap") && bilder != nullptr && !bilder->empty()) {
                            bilder->back() = true;
                        }
                    }
                }

                // 2. Unser Leser: kommt jede Gluehstufe an?
                bhed::ShaderMap m;
                bhed::parseShaderScript(text, m);
                for (const auto& [sn, soll] : gluehSoll) {
                    const bool irgendeine = std::find(soll.begin(), soll.end(), true) != soll.end();
                    if (!irgendeine) { continue; }
                    auto it = m.find(sn);
                    if (it == m.end()) {
                        for (auto& [k, v] : m) {
                            if (klein(k) == sn) { it = m.find(k); break; }
                        }
                    }
                    if (it == m.end()) { continue; }
                    const bhed::ShaderInfo& si = it->second;
                    if (!si.hasGlow) {
                        ++gluehShaderFehlt;
                        if (gluehBeispiele.size() < 8) {
                            gluehBeispiele.push_back(sn + " (hasGlow fehlt)");
                        }
                    }
                    // Stufen ohne Bild zaehlen nicht mit - weder hier noch bei uns.
                    const std::vector<bool>& bd = bildDa[sn];
                    std::size_t unsere = 0;
                    for (std::size_t s = 0; s < soll.size(); ++s) {
                        if (s < bd.size() && !bd[s]) {
                            if (soll[s]) { ++gluehOhneBild; }
                            continue;
                        }
                        const std::size_t u = unsere++;
                        if (!soll[s]) { continue; }
                        ++gluehStufen;
                        const bool ist = u < si.stages.size() && si.stages[u].glow;
                        if (!ist) {
                            ++gluehFehlt;
                            if (gluehBeispiele.size() < 8) {
                                gluehBeispiele.push_back(sn + " Stufe " + std::to_string(s) +
                                                         " (bei uns " +
                                                         std::to_string(si.stages.size()) +
                                                         " Stufen)");
                            }
                        }
                    }
                }
            }
        }
    }

    std::printf("%d .shader-Dateien, %d Shader\n\n", dateien, shaderZahl);
    auto zeige = [](const char* titel, const std::map<std::string, int>& m,
                    const std::set<std::string>* bekannt) {
        std::vector<std::pair<int, std::string>> v;
        for (const auto& [k, n] : m) { v.emplace_back(n, k); }
        std::sort(v.rbegin(), v.rend());
        std::printf("%s\n", titel);
        for (const auto& [n, k] : v) {
            // ss* sind die Parameter von surfaceSprites (ParseSurfaceSpritesOptional).
            const bool jka = bekannt == nullptr || bekannt->count(k) != 0 ||
                             k.rfind("q3map", 0) == 0 || k.rfind("ss", 0) == 0;
            std::printf("  %7d  %s%s\n", n, k.c_str(), jka ? "" : "   (wertet JKA nicht aus)");
        }
        std::printf("\n");
    };
    zeige("Shaderebene:", shaderWorte, &kShaderWorte);
    zeige("Stufenebene:", stufenWorte, &kStufenWorte);
    zeige("Werte:", werte, nullptr);
    std::printf("Gluehen: %d Gluehstufen, davon %d bei uns NICHT als Gluehen; "
                "%d Shader ohne hasGlow\n",
                gluehStufen, gluehFehlt, gluehShaderFehlt);
    std::printf("(%d Gluehstufen ohne Bild - die Engine schaltet sie ab)\n", gluehOhneBild);
    for (const std::string& b : gluehBeispiele) { std::printf("  %s\n", b.c_str()); }
    return (gluehFehlt == 0 && gluehShaderFehlt == 0) ? 0 : 1;
}
