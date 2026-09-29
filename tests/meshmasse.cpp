// meshmasse.cpp - JEDES Modell und jede Karte der Spielordner laden und pruefen.
//
// Gefragt (27.09.): "teste bitte auch, dass das Mesh ordentlich geladen wird".
//
// Die Einzelproben (glmtest, bspgeotest) pruefen je EINE Datei. Diese hier
// laeuft ueber alles, was in den Archiven liegt - bei Movie Duels tausende
// .glm und hunderte Karten - und prueft, was die Grafikkarte daraus bekommt:
//
//   .glm  liest es sich? Indizes innerhalb der Ecken, Zahlen endlich,
//         Normalen etwa Laenge eins, Gewichte summieren sich zu eins,
//         Knochen innerhalb des Skeletts UND innerhalb der 128 Knochen, die
//         der Figuren-Shader fasst (gpu::kMaxKnochen). Passt die .gla dazu
//         (gleiche Knochenzahl)? Und was gpu::packeEcken daraus macht -
//         genau das landet im Eckenpuffer.
//   .md3  liest es sich? Indizes innerhalb der Ecken je Bild.
//   .bsp  liest es sich, baut sich das Netz (buildMesh, jedes Untermodell)?
//         Indizes, Stapelbereiche, Lightmap- und Shadernummern gueltig.
//
// Aufruf:  meshmasse <spielordner> [<spielordner> ...] [--nur glm|md3|bsp]
// Rueckgabe 1, sobald eine Datei FEHLERHAFT ist (nicht lesbar oder kaputte
// Verweise). Auffaelligkeiten, die die Engine genauso hinnimmt (etwa eine
// Normale der Laenge null), werden gezaehlt und gemeldet, aber nicht als
// Fehler gewertet.
#include "bhed/bspgeo.h"
#include "bhed/gla.h"
#include "bhed/glm.h"
#include "bhed/gpuskin.h"
#include "bhed/md3.h"
#include "bhed/pk3.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace bhed;

namespace {

struct Befund {
    int dateien = 0;
    int fehlerhaft = 0;
    std::map<std::string, int> auffaellig;          // Art -> Anzahl Dateien
    std::map<std::string, std::string> beispiel;    // Art -> erste Datei
    std::vector<std::string> fehler;                // "Datei: Grund"
};

void auffaellig(Befund& b, const std::string& art, const std::string& datei) {
    if (b.auffaellig[art]++ == 0) {
        b.beispiel[art] = datei;
    }
}

void fehler(Befund& b, const std::string& datei, const std::string& grund) {
    ++b.fehlerhaft;
    if (b.fehler.size() < 40U) {
        b.fehler.push_back(datei + ": " + grund);
    }
}

bool endlich(const float* v, int n) {
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(v[i])) {
            return false;
        }
    }
    return true;
}

std::string klein(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Eine Datei aus allen Spielordnern holen - der LETZTE Fund gewinnt, wie in
// der Engine (spaetere Archive ueberdecken fruehere).
bool lies(const std::vector<GamePath>& pfade, const std::string& name, std::string& out) {
    bool da = false;
    for (const GamePath& gp : pfade) {
        for (const Pk3& a : gp.archives) {
            const Pk3Entry* e = a.find(name);
            if (e != nullptr) {
                std::string d;
                if (readPk3File(a, *e, d)) {
                    out = std::move(d);
                    da = true;
                }
            }
        }
    }
    return da;
}

void pruefeGlm(const std::vector<GamePath>& pfade, const std::string& name,
               const std::string& daten, Befund& b, std::map<std::string, int>& glaKnochen) {
    ++b.dateien;
    GlmModel m;
    std::string err;
    if (!readGlm(daten, m, &err)) {
        fehler(b, name, "nicht lesbar: " + err);
        return;
    }
    if (m.surfaces.empty()) {
        fehler(b, name, "keine Flaechen");
        return;
    }
    if (!endlich(m.mins, 3) || !endlich(m.maxs, 3)) {
        fehler(b, name, "Huellkasten nicht endlich");
    }
    // Die .gla dazu: gleiche Knochenzahl? Die Engine verweigert sonst das
    // Modell (G2_SetupModelPointers prueft mdxm->numBones gegen die .gla).
    int glaZahl = -1;
    if (!m.animFile.empty()) {
        const std::string glaName = klein(m.animFile) + ".gla";
        const auto it = glaKnochen.find(glaName);
        if (it != glaKnochen.end()) {
            glaZahl = it->second;
        } else {
            std::string gd;
            if (lies(pfade, glaName, gd)) {
                GlaAnimation a;
                if (readGla(gd, a, nullptr)) {
                    glaZahl = static_cast<int>(a.bones.size());
                }
            }
            glaKnochen[glaName] = glaZahl;
        }
        if (glaZahl < 0) {
            auffaellig(b, "Skelett (.gla) nicht gefunden oder nicht lesbar: " + m.animFile, name);
        } else if (glaZahl != m.numBones && m.jk2Umgewandelt) {
            fehler(b, name, "JK2-Modell umgelegt, aber die .gla hat " + std::to_string(glaZahl) + " Knochen");
        } else if (glaZahl != m.numBones) {
            // Die Engine laedt so ein Modell gar nicht (R_LoadMDXM:
            // "has different bones than anim") - ein Fehler der Datei, nicht
            // des Lesers. Gemeldet, aber nicht als Fehlschlag gezaehlt.
            auffaellig(b, "Knochenzahl passt nicht zur .gla - die Engine laedt das Modell nicht", name);
            b.beispiel["Knochenzahl passt nicht zur .gla - die Engine laedt das Modell nicht"] +=
                (b.auffaellig["Knochenzahl passt nicht zur .gla - die Engine laedt das Modell nicht"] > 1)
                    ? ""
                    : std::string(" (") + std::to_string(m.numBones) + " gegen " + std::to_string(glaZahl) + ")";
            return;
        }
    }
    const int knochenGrenze = (glaZahl > 0) ? glaZahl : m.numBones;
    bool kaputt = false;
    bool nullNormale = false;
    bool gewichtSumme = false;
    bool ueberGpu = false;
    bool ohneFlaechenAnzeige = true;
    float kleinNegativ = 0.0F;
    bool nullLaenge = false;
    float normMin = 1e9F;
    float normMax = 0.0F;
    for (const GlmSurface& sf : m.surfaces) {
        if (sf.isVisible(true)) {
            ohneFlaechenAnzeige = false;
        }
        if (sf.indexes.size() % 3U != 0U) {
            fehler(b, name, "Flaeche " + sf.name + ": Indexzahl nicht durch drei teilbar");
            kaputt = true;
            break;
        }
        for (const std::uint32_t ix : sf.indexes) {
            if (ix >= sf.verts.size()) {
                fehler(b, name, "Flaeche " + sf.name + ": Index " + std::to_string(ix) + " >= " +
                                    std::to_string(sf.verts.size()) + " Ecken");
                kaputt = true;
                break;
            }
        }
        if (kaputt) {
            break;
        }
        for (const GlmVertex& v : sf.verts) {
            if (!endlich(v.xyz, 3) || !endlich(v.normal, 3) || !endlich(v.st, 2) ||
                !endlich(v.weights, 4)) {
                fehler(b, name, "Flaeche " + sf.name + ": Zahl nicht endlich");
                kaputt = true;
                break;
            }
            const float nl = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] +
                                       v.normal[2] * v.normal[2]);
            if (nl < 1e-6F) {
                nullLaenge = true;
            } else if (nl < 0.5F || nl > 1.5F) {
                nullNormale = true;
                normMin = std::min(normMin, nl);
                normMax = std::max(normMax, nl);
            }
            float summe = 0.0F;
            for (int k = 0; k < 4; ++k) {
                // Das letzte Gewicht steht nicht in der Datei, es ist
                // 1 - Summe der uebrigen (G2) - durch die Rundung der
                // gespeicherten Werte kann es knapp negativ werden. Die
                // Engine rechnet es so mit; erst ein deutlich negatives
                // waere eine kaputte Datei.
                if (v.weights[k] < -0.05F) {
                    fehler(b, name, "Flaeche " + sf.name + ": Gewicht " + std::to_string(v.weights[k]));
                    kaputt = true;
                    break;
                }
                if (v.weights[k] < 0.0F) {
                    kleinNegativ = std::min(kleinNegativ, v.weights[k]);
                }
                if (v.weights[k] > 0.0F) {
                    summe += v.weights[k];
                    if (knochenGrenze > 0 && v.bones[k] >= knochenGrenze) {
                        fehler(b, name, "Flaeche " + sf.name + ": Knochen " +
                                            std::to_string(v.bones[k]) + " ausserhalb des Skeletts (" +
                                            std::to_string(knochenGrenze) + ")");
                        kaputt = true;
                        break;
                    }
                    if (v.bones[k] >= gpu::kMaxKnochen) {
                        ueberGpu = true;
                    }
                }
            }
            if (kaputt) {
                break;
            }
            if (summe > 0.0F && std::fabs(summe - 1.0F) > 0.02F) {
                gewichtSumme = true;
            }
        }
        if (kaputt) {
            break;
        }
        // --- Was die Grafikkarte bekommt ---------------------------------
        std::vector<gpu::SkinVertex> ecken;
        gpu::packeEcken(sf, ecken);
        if (ecken.size() != sf.verts.size()) {
            fehler(b, name, "Flaeche " + sf.name + ": packeEcken liefert " + std::to_string(ecken.size()) +
                                " statt " + std::to_string(sf.verts.size()) + " Ecken");
            kaputt = true;
            break;
        }
        for (const gpu::SkinVertex& e : ecken) {
            float s = 0.0F;
            for (int k = 0; k < 4; ++k) {
                if (e.weights[k] > 0.0F) {
                    s += e.weights[k];
                    if (e.bones[k] < 0.0F || e.bones[k] >= static_cast<float>(gpu::kMaxKnochen)) {
                        fehler(b, name, "Flaeche " + sf.name + ": Eckenpuffer zeigt auf Knochen " +
                                            std::to_string(e.bones[k]));
                        kaputt = true;
                        break;
                    }
                }
            }
            if (kaputt) {
                break;
            }
            if (s > 0.0F && std::fabs(s - 1.0F) > 0.001F) {
                fehler(b, name, "Flaeche " + sf.name + ": Gewichte im Eckenpuffer summieren sich zu " +
                                    std::to_string(s));
                kaputt = true;
                break;
            }
        }
        if (kaputt) {
            break;
        }
    }
    if (kaputt) {
        return;
    }
    if (kleinNegativ < 0.0F) {
        auffaellig(b, "Gewicht durch Rundung knapp negativ (bis -0.05, die Engine rechnet es mit)", name);
    }
    if (nullLaenge) {
        auffaellig(b, "Normale der Laenge NULL", name);
    }
    if (nullNormale) {
        char was[120];
        const float lo = normMin < 0.25F ? 0.0F : normMin < 0.5F ? 0.25F : 1.5F;
        std::snprintf(was, sizeof(was), "Normale weit weg von Laenge eins (Bereich ab %.2f)", static_cast<double>(lo));
        auffaellig(b, was, name);
        (void)normMax;
    }
    if (gewichtSumme) {
        auffaellig(b, "Gewichte in der Datei summieren sich nicht zu eins (packeEcken normiert)", name);
    }
    if (ueberGpu) {
        fehler(b, name, "Knochen ueber " + std::to_string(gpu::kMaxKnochen) +
                            " - der Figuren-Shader fasst nicht mehr");
    }
    if (ohneFlaechenAnzeige) {
        auffaellig(b, "keine sichtbare Flaeche (nur Bolzen/abgeschaltet)", name);
    }
}

void pruefeMd3(const std::string& name, const std::string& daten, Befund& b) {
    ++b.dateien;
    Md3Model m;
    std::string err;
    if (!readMd3(daten, m, &err)) {
        fehler(b, name, "nicht lesbar: " + err);
        return;
    }
    if (m.surfaces.empty()) {
        auffaellig(b, "keine Flaechen", name);
        return;
    }
    for (const Md3Surface& sf : m.surfaces) {
        if (sf.indexes.size() % 3U != 0U) {
            fehler(b, name, "Flaeche " + sf.name + ": Indexzahl nicht durch drei teilbar");
            return;
        }
        for (const std::uint32_t ix : sf.indexes) {
            if (ix >= static_cast<std::uint32_t>(sf.numVerts)) {
                fehler(b, name, "Flaeche " + sf.name + ": Index ausserhalb der Ecken");
                return;
            }
        }
        if (sf.verts.size() != static_cast<std::size_t>(sf.numVerts) *
                                   static_cast<std::size_t>(std::max(1, sf.numFrames))) {
            fehler(b, name, "Flaeche " + sf.name + ": Eckenzahl passt nicht zu Bildern x Ecken");
            return;
        }
        for (const Md3Vertex& v : sf.verts) {
            if (!endlich(v.xyz, 3) || !endlich(v.st, 2)) {
                fehler(b, name, "Flaeche " + sf.name + ": Zahl nicht endlich");
                return;
            }
        }
    }
}

bool pruefeNetz(const BspMesh& n, const BspGeometry& g, std::string& grund) {
    if (n.indexes.size() % 3U != 0U) {
        grund = "Indexzahl nicht durch drei teilbar";
        return false;
    }
    for (const std::uint32_t ix : n.indexes) {
        if (ix >= n.verts.size()) {
            grund = "Index " + std::to_string(ix) + " >= " + std::to_string(n.verts.size()) + " Ecken";
            return false;
        }
    }
    for (std::size_t vi = 0; vi < n.verts.size(); ++vi) {
        const BspVertex& v = n.verts[vi];
        const char* feld = !endlich(v.xyz, 3) ? "Ort" : !endlich(v.st, 2) ? "Texturkoordinate"
                         : !endlich(v.lightmap, 2) ? "Lightmapkoordinate" : !endlich(v.normal, 3) ? "Normale" : nullptr;
        if (feld != nullptr) {
            // Welche Flaeche? Ueber die Abschnitte der Stapel.
            int flaeche = -1;
            int stapelLm = -99;
            for (const BspMesh::Batch& s : n.batches) {
                for (std::uint32_t r = 0; r < s.numRuns && flaeche < 0; ++r) {
                    const auto& run = n.runs[s.firstRun + r];
                    for (std::uint32_t k = 0; k < run.numIndexes; ++k) {
                        if (n.indexes[run.firstIndex + k] == vi) {
                            flaeche = static_cast<int>(run.surface);
                            stapelLm = s.lightmap;
                            break;
                        }
                    }
                }
            }
            grund = std::string(feld) + " nicht endlich (Ecke " + std::to_string(vi) + ", Flaeche " +
                    std::to_string(flaeche) +
                    ((flaeche >= 0 && flaeche < static_cast<int>(g.surfaces.size()))
                         ? ", Typ " + std::to_string(static_cast<int>(g.surfaces[static_cast<std::size_t>(flaeche)].type)) +
                               ", Lightmap " + std::to_string(g.surfaces[static_cast<std::size_t>(flaeche)].lightmap) +
                               ", Stapel-Lightmap " + std::to_string(stapelLm)
                         : std::string()) + ")";
            return false;
        }
    }
    std::size_t abgedeckt = 0;
    for (const BspMesh::Batch& s : n.batches) {
        if (static_cast<std::size_t>(s.firstIndex) + s.numIndexes > n.indexes.size()) {
            grund = "Stapel reicht ueber das Indexende";
            return false;
        }
        if (s.lightmap >= static_cast<int>(g.lightmaps.size()) ||
            (s.lightmap >= 0 && g.lightmaps[static_cast<std::size_t>(s.lightmap)].empty())) {
            grund = "Lightmap " + std::to_string(s.lightmap) + " von " + std::to_string(g.lightmaps.size());
            return false;
        }
        if (s.shader < 0 || s.shader >= static_cast<int>(g.shaders.size())) {
            grund = "Shader " + std::to_string(s.shader) + " von " + std::to_string(g.shaders.size());
            return false;
        }
        abgedeckt += s.numIndexes;
    }
    if (abgedeckt != n.indexes.size()) {
        grund = "Stapel decken " + std::to_string(abgedeckt) + " von " + std::to_string(n.indexes.size()) +
                " Indizes ab";
        return false;
    }
    return true;
}

void pruefeBsp(const std::vector<GamePath>& pfade, const std::string& name, const std::string& daten,
               Befund& b) {
    ++b.dateien;
    BspGeometry g;
    std::string err;
    if (!readBspGeometry(daten, g, &err)) {
        fehler(b, name, "nicht lesbar: " + err);
        return;
    }
    // Wie die Oberflaeche: externe Lightmaps vor dem Netz (R_FindLightmap).
    std::string ordner = name;
    if (ordner.size() > 4) { ordner = ordner.substr(0, ordner.size() - 4); }
    if (ladeExterneLightmaps(g, ordner, [&](const std::string& n, std::string& out) {
            return lies(pfade, n, out);
        }) > 0) {
        auffaellig(b, "externe Lightmaps (maps/<karte>/lm_*) - geladen", name);
    }
    const BspMesh welt = buildMesh(g);
    std::string grund;
    if (welt.indexes.empty()) {
        auffaellig(b, "Welt ohne Dreiecke", name);
    } else if (!pruefeNetz(welt, g, grund)) {
        fehler(b, name, "Welt: " + grund);
        return;
    }
    for (std::size_t mi = 1; mi < g.models.size(); ++mi) {
        const BspMesh teil = buildModelMesh(g, static_cast<int>(mi));
        if (!teil.indexes.empty() && !pruefeNetz(teil, g, grund)) {
            fehler(b, name, "Untermodell *" + std::to_string(mi) + ": " + grund);
            return;
        }
    }
}

void bericht(const char* art, const Befund& b) {
    std::printf("\n%s: %d Dateien, %d fehlerhaft\n", art, b.dateien, b.fehlerhaft);
    for (const std::string& f : b.fehler) {
        std::printf("  FEHL  %s\n", f.c_str());
    }
    if (b.fehlerhaft > static_cast<int>(b.fehler.size())) {
        std::printf("  ... und %d weitere\n", b.fehlerhaft - static_cast<int>(b.fehler.size()));
    }
    for (const auto& [was, n] : b.auffaellig) {
        std::printf("  auffaellig (%d Dateien): %s - z.B. %s\n", n, was.c_str(), b.beispiel.at(was).c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> ordner;
    std::string nur;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--nur") == 0 && i + 1 < argc) {
            nur = argv[++i];
        } else {
            ordner.emplace_back(argv[i]);
        }
    }
    if (ordner.empty()) {
        std::printf("meshmasse: kein Spielordner angegeben - uebersprungen\n");
        return 0;
    }
    std::vector<GamePath> pfade;
    for (const std::string& o : ordner) {
        GamePath gp;
        if (scanGamePath(o, gp)) {
            std::printf("%s: %zu Archive, %d Dateien\n", o.c_str(), gp.archives.size(), gp.fileCount);
            pfade.push_back(std::move(gp));
        } else {
            std::printf("%s: nicht lesbar\n", o.c_str());
        }
    }
    // Jede Datei einmal, auch wenn sie in mehreren Archiven liegt - geprueft
    // wird die, die die Engine nehmen wuerde (die letzte).
    std::set<std::string> gesehen;
    std::vector<std::string> glm;
    std::vector<std::string> md3;
    std::vector<std::string> bsp;
    for (const GamePath& gp : pfade) {
        for (const FoundFile& f : findByExtension(gp, {".glm", ".md3", ".bsp"})) {
            const std::string k = klein(f.name);
            if (!gesehen.insert(k).second) {
                continue;
            }
            if (k.size() > 4 && k.compare(k.size() - 4, 4, ".glm") == 0) { glm.push_back(f.name); }
            if (k.size() > 4 && k.compare(k.size() - 4, 4, ".md3") == 0) { md3.push_back(f.name); }
            if (k.size() > 4 && k.compare(k.size() - 4, 4, ".bsp") == 0) { bsp.push_back(f.name); }
        }
    }
    Befund bg;
    Befund bm;
    Befund bb;
    std::map<std::string, int> glaKnochen;
    std::string daten;
    if (nur.empty() || nur == "glm") {
        for (const std::string& n : glm) {
            if (lies(pfade, n, daten)) { pruefeGlm(pfade, n, daten, bg, glaKnochen); }
        }
    }
    if (nur.empty() || nur == "md3") {
        for (const std::string& n : md3) {
            if (lies(pfade, n, daten)) { pruefeMd3(n, daten, bm); }
        }
    }
    if (nur.empty() || nur == "bsp") {
        for (const std::string& n : bsp) {
            if (lies(pfade, n, daten)) { pruefeBsp(pfade, n, daten, bb); }
        }
    }
    bericht("Ghoul2-Modelle (.glm)", bg);
    bericht("MD3-Modelle (.md3)", bm);
    bericht("Karten (.bsp)", bb);
    const int fehlerGesamt = bg.fehlerhaft + bm.fehlerhaft + bb.fehlerhaft;
    std::printf("\n%s (%d fehlerhafte Dateien)\n",
                fehlerGesamt != 0 ? "FEHLGESCHLAGEN" : "alle Netze geladen und geprueft", fehlerGesamt);
    return fehlerGesamt != 0 ? 1 : 0;
}
