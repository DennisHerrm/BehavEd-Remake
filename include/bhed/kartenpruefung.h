// kartenpruefung.h - rc602: der Abgleich "zeigt behaved alles, was das Spiel
// zeigt?" fuer jede Karte der Spielordner (Selbsttest BHED_EDITORTEST=
// kartenpruefung, tools/kartenpruefung.py).
//
// Gewuenscht: "er soll einfach alle karten oeffnen und checken ob alles da
// ist". Dieser Teil ist rein (ohne Programmzustand) und als Unit-Test
// pruefbar: welche Shader-Anweisungen kennt das Spiel, welche bildet
// behaved in welchem Zusammenhang (Welt, Figur, Effekt) nicht nach.
//
// Massstab ist das Spiel: die Schluesselwoerter von ParseShader/ParseStage
// (MovieDuels code/rd-vanilla/tr_shader.cpp). Was das Spiel selbst nicht
// kennt ("unknown general shader parameter"), uebergeht es - das ist dann
// auch keine Luecke von behaved. Was keine Wirkung auf das Bild hat
// (q3map_*, qer_*, surfaceparm, nopicmip, ...), zaehlt ebenfalls nicht.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "bhed/gegenstaende.h"

namespace bhed::kartenpruefung {

// Die Anweisungen EINES Shaders, normiert: Schluesselwort klein, bei
// rgbGen/alphaGen/tcGen/tcMod/deformVertexes/sort/surfaceSprites mit der Art
// ("rgbgen wave", "deformvertexes autosprite2").
struct ShaderAnweisungen {
    std::vector<std::string> oben;                  // Ebene des Shaders
    std::vector<std::vector<std::string>> stufen;   // je Stufe
    std::vector<std::string> stufenBild;            // erstes map/animMap-Bild je Stufe (klein)
};

// Alle Shader eines .shader-Textes eintragen (Name klein). Ein spaeterer
// Eintrag desselben Namens ersetzt den frueheren - dieselbe Regel wie
// parseShaderScript (der spaetere Spielordner gewinnt).
void sammleShader(const std::string& text, std::map<std::string, ShaderAnweisungen>& out);

enum class Zusammenhang { Welt, Figur, Effekt };

// Was behaved von diesem Shader in diesem Zusammenhang NICHT so zeichnet wie
// das Spiel. `hautName` (nur Figur): der Name, unter dem die .skin/das
// Modell den Shader nennt, klein und ohne Endung - fuer die Regel "Skript
// vor Bild" (R_FindShader).
std::vector<std::string> fehlendeMerkmale(const ShaderAnweisungen& s, Zusammenhang z,
                                          const std::string& hautName = std::string());

// --- Entities -------------------------------------------------------------
//
// Steht der Klassenname in der Spawntabelle des Spiels (spawns[] in
// g_spawn.cpp, beide Regelwerke) oder ist er ein Gegenstand der itemlist
// (item_*, weapon_*, ammo_*, holocron_*)? G_CallSpawn vergleicht mit strcmp.
// `gegenstaende`: die Liste aus ext_data/items.dat (leseGegenstaende) - ohne
// sie gilt jedes item_/weapon_/ammo_ als Gegenstand (grob).
bool imSpiel(const std::string& klasse, bool movieDuels, const Gegenstandsliste* gegenstaende = nullptr);

// Was das Spiel fuer diese Klasse ZEIGT, sofern sie kein Brush-Modell ("*N")
// traegt. Leer = nichts Sichtbares (Logik, Ziele, Ausloeser, Lichtquellen).
// Sonst eine kurze Art ("Modell", "Gegenstand", "Effekt", "Figur", "Geschuetz").
std::string sichtbarImSpiel(const std::string& klasse, const Gegenstandsliste* gegenstaende = nullptr);

}  // namespace bhed::kartenpruefung
