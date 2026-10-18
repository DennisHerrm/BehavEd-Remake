// Probe fuer bhed/kartenpruefung.h (rc602): der Abgleich "zeigt behaved
// alles, was das Spiel zeigt?" - Shader-Anweisungen je Zusammenhang und die
// Spawntabellen beider Regelwerke.
#include "bhed/kartenpruefung.h"
#include "bhed/bsp.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace bhed::kartenpruefung;

namespace {
int g_fehler = 0;
void pruefe(bool ok, const std::string& was) {
    std::printf("%s %s\n", ok ? "OK    " : "FEHLER", was.c_str());
    if (!ok) { ++g_fehler; }
}
bool hat(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
std::string liste(const std::vector<std::string>& v) {
    std::string r;
    for (const std::string& s : v) { r += "[" + s + "]"; }
    return r;
}
}  // namespace

int main() {
    // Aus yavin (textures/yavin/grass.shader, Aufbau wie im Spiel).
    const std::string text =
        "// Kommentar { }\n"
        "textures/yavin/ground_grass\n"
        "{\n"
        "\tq3map_nolightmap\n"
        "\tqer_editorimage textures/yavin/ground\n"
        "\tsurfaceparm grass\n"
        "    {\n"
        "        map $lightmap\n"
        "    }\n"
        "    {\n"
        "        map textures/yavin/ground.tga\n"
        "        blendFunc GL_DST_COLOR GL_ZERO\n"
        "    }\n"
        "    {\n"
        "        map gfx/grass1\n"
        "        surfaceSprites vertical 16 24 48 1000\n"
        "        ssFademax 1500\n"
        "        ssVariance 1 2\n"
        "        alphaFunc GE128\n"
        "    }\n"
        "}\n"
        "models/players/sbd/body { cull none\n"
        "  { map models/players/sbd/body_x\n rgbGen lightingDiffuse }\n"
        "  { map models/players/sbd/body_e\n blendFunc GL_ONE GL_ONE\n glow }\n"
        "  { map textures/common/env\n tcGen environment\n blendFunc GL_ONE GL_ONE }\n"
        "}\n"
        "gfx/effects/glowball\n{\n{\nmap gfx/effects/glowball.tga\nblendFunc add\nglow\n}\n}\n"
        "textures/test/neu\n{\n deformVertexes text0\n videoMap intro.roq\n tcGen vector ( 1 0 0 ) ( 0 1 0 )\n"
        " unbekanntesWort 1\n{\nmap $lightmap\nrgbGen oneMinusEntity\n}\n}\n";
    std::map<std::string, ShaderAnweisungen> m;
    sammleShader(text, m);
    pruefe(m.size() == 4U, "vier Shader gelesen (" + std::to_string(m.size()) + ")");
    const ShaderAnweisungen& gras = m["textures/yavin/ground_grass"];
    pruefe(gras.stufen.size() == 3U, "Gras: drei Stufen");
    pruefe(gras.stufenBild.size() == 3U && gras.stufenBild[1] == "textures/yavin/ground",
           "Gras: Bild der zweiten Stufe ohne Endung, klein");
    const auto fg = fehlendeMerkmale(gras, Zusammenhang::Welt);
    pruefe(fg.size() == 1U && fg[0] == "surfacesprites vertical",
           "Welt: nur surfaceSprites fehlt, q3map/qer/surfaceparm/ss* zaehlen nicht " + liste(fg));

    const ShaderAnweisungen& sbd = m["models/players/sbd/body"];
    const auto ff = fehlendeMerkmale(sbd, Zusammenhang::Figur, "models/players/sbd/body");
    pruefe(hat(ff, "figur: stufe tcgen environment"), "Figur: Chromstufe fehlt " + liste(ff));
    pruefe(hat(ff, "figur: grundstufe anderes bild"), "Figur: Grundstufe body_x statt body (Skript vor Bild)");
    pruefe(!hat(ff, "figur: weitere stufe"), "Figur: glow-Stufe zaehlt nicht als Luecke");
    const auto fw = fehlendeMerkmale(sbd, Zusammenhang::Welt);
    pruefe(fw.empty(), "derselbe Shader an der Welt: nichts fehlt " + liste(fw));

    const auto fe = fehlendeMerkmale(m["gfx/effects/glowball"], Zusammenhang::Effekt);
    pruefe(fe.empty(), "Effekt: eine glow-Stufe geht seit rc602 ins Gluehen - keine Luecke " + liste(fe));

    const auto fn = fehlendeMerkmale(m["textures/test/neu"], Zusammenhang::Welt);
    pruefe(hat(fn, "deformvertexes text") && hat(fn, "videomap") && hat(fn, "tcgen vector") &&
               hat(fn, "rgbgen oneminusentity"),
           "Welt: text0, videoMap, tcGen vector, rgbGen oneMinusEntity fehlen " + liste(fn));
    pruefe(!hat(fn, "unbekanntesworts") && !hat(fn, "unbekanntesWort") && fn.size() == 4U,
           "was das Spiel nicht kennt, ist keine Luecke");

    // Spawntabellen
    pruefe(imSpiel("fx_runner", true) && imSpiel("fx_runner", false), "fx_runner in beiden");
    pruefe(imSpiel("misc_turretG2", true) && !imSpiel("misc_turretG2", false), "misc_turretG2 nur MD");
    pruefe(imSpiel("info_NPCnav", false) && !imSpiel("info_NPCnav", true), "info_NPCnav nur OpenJK");
    pruefe(!imSpiel("FX_RUNNER", true), "strcmp: Gross-/Kleinschreibung zaehlt");
    pruefe(imSpiel("weapon_blaster", true) && imSpiel("item_medpak_instant", true), "Gegenstaende (itemlist)");
    pruefe(!imSpiel("misc_siege_item", true), "Mehrspieler-Klasse nicht im Einzelspieler");
    pruefe(sichtbarImSpiel("weapon_blaster") == "Gegenstand", "Waffe liegt sichtbar");
    pruefe(sichtbarImSpiel("misc_model").empty(), "misc_model gibt das Spiel frei (in der Karte gebacken)");
    pruefe(sichtbarImSpiel("target_relay").empty() && sichtbarImSpiel("light").empty(), "Logik/Licht unsichtbar");
    pruefe(sichtbarImSpiel("emplaced_gun") == "Geschuetz", "emplaced_gun sichtbar");

    // --- Gegenstaende (items.dat, G_CallSpawn) ------------------------------
    const std::string dat =
        "// Kommentar\n"
        "{\nitemname\tITM_SABER_PICKUP\n\nclassname\tweapon_saber\nworldmodel\tmodels/weapons2/saber/saber_w.md3\n"
        "type IT_WEAPON\ntag WP_SABER\nmin -16 -16 -8\nmax 16 16 16\n}\n"
        "{\nitemname ITM_BLASTER_PISTOL_PICKUP\nclassname weapon_blaster_pistol\n"
        "worldmodel models/weapons2/blaster_pistol/blaster_pistol_w.glm\nmin -10 -10 -2\nmax 10 10 12\n"
        "unbekannt 1 2 3\n}\n"
        // ITM_BLASTER_PICKUP (Platz 3) fehlt - alles danach erreicht G_CallSpawn nicht.
        "{\nitemname ITM_DISRUPTOR_PICKUP\nclassname weapon_disruptor\nworldmodel x.glm\n}\n"
        "{\nitemname ITM_GIBTSNICHT\nclassname item_falsch\n}\n";
    const bhed::Gegenstandsliste gl = bhed::leseGegenstaende(dat, true);
    pruefe(gl.plaetze.size() == 80U, "MD: 80 Plaetze ITM_NONE..ITM_SECURITY_KEY_PICKUP (" +
                                         std::to_string(gl.plaetze.size()) + ")");
    pruefe(bhed::leseGegenstaende(dat, false).plaetze.size() == 60U, "OpenJK: 60 Plaetze");
    const bhed::Gegenstand* sab = gl.spawnbar("weapon_saber");
    pruefe(sab != nullptr && sab->worldmodel == "models/weapons2/saber/saber_w.md3" && sab->mins[2] == -8.0F &&
               sab->typ == "IT_WEAPON" && sab->tag == "WP_SABER",
           "weapon_saber: Modell, min, type, tag");
    const bhed::Gegenstand* bp = gl.spawnbar("weapon_blaster_pistol");
    pruefe(bp != nullptr && bp->maxs[2] == 12.0F && bp->mins[0] == -10.0F, "weapon_blaster_pistol: min/max");
    pruefe(gl.spawnbar("weapon_disruptor") == nullptr, "nach dem ersten leeren Platz spawnt nichts mehr (Schleife bricht ab)");
    pruefe(gl.plaetze[0].classname == "item_falsch", "unbekannter itemname landet auf Platz 0 (ITM_NONE)");
    pruefe(gl.spawnbar("item_falsch") == nullptr, "Platz 0 zaehlt nicht (Schleife ab bg_itemlist + 1)");
    pruefe(!imSpiel("weapon_disruptor", true, &gl) && imSpiel("weapon_saber", true, &gl),
           "imSpiel mit der Liste");
    pruefe(sichtbarImSpiel("weapon_saber", &gl) == "Gegenstand" && sichtbarImSpiel("item_shield_lrg_instant", &gl).empty(),
           "sichtbarImSpiel mit der Liste");

    // --- Aufstellen (FinishSpawningItem) -------------------------------------
    {
        bhed::MapData m;
        bhed::parseEntities("{\n\"classname\" \"weapon_saber\"\n\"origin\" \"0 0 100\"\n\"angles\" \"90\"\n"
                            "\"spawnflags\" \"16\"\n}\n"
                            "{\n\"classname\" \"weapon_saber\"\n\"origin\" \"0 0 100\"\n\"targetname\" \"t1\"\n}\n"
                            "{\n\"classname\" \"weapon_saber\"\n\"origin\" \"0 0 100\"\n\"spawnflags\" \"512\"\n}\n"
                            "{\n\"classname\" \"weapon_saber\"\n\"origin\" \"0 0 100\"\n\"spawnflags\" \"1\"\n}\n"
                            "{\n\"classname\" \"weapon_saber\"\n\"origin\" \"0 0 100\"\n\"notsingle\" \"1\"\n}\n",
                            m.entities);
        int spuren = 0;
        bool fest = false;
        std::array<float, 3> geseheneMins{};
        const bhed::GegenstandSpur spur = [&](const std::array<float, 3>& start, const std::array<float, 3>& mins,
                                              const std::array<float, 3>&, const std::array<float, 3>& ende,
                                              std::array<float, 3>& endpos, bool& startsolid) {
            ++spuren;
            geseheneMins = mins;
            pruefe(start[2] == 101.0F && ende[2] == -65536.0F, "Spur von origin+1 bis MIN_WORLD_COORD");
            endpos = {start[0], start[1], 8.0F};
            startsolid = fest;
        };
        const bhed::GegenstandAufstellung a0 = bhed::gegenstandAufstellen(m.entities[0], *sab, 1, true, spur);
        pruefe(a0.sichtbar && a0.ort[2] == 8.0F && geseheneMins[2] == -8.0F, "faellt mit den mins des Gegenstands");
        pruefe(a0.winkel[1] == 90.0F && a0.winkel[0] == 0.0F, "MD: \"angles\" \"90\" ist der Gierwinkel");
        pruefe(a0.md3Winkel[0] == -20.0F, "ITMSF_VERTICAL am Schwert: Nicken -20 (nur Achsen)");
        const bhed::GegenstandAufstellung a0oj = bhed::gegenstandAufstellen(m.entities[0], *sab, 1, false, spur);
        pruefe(a0oj.winkel[0] == 90.0F && a0oj.winkel[1] == 0.0F, "OpenJK: \"angles\" \"90\" ist Nicken");
        pruefe(!bhed::gegenstandAufstellen(m.entities[1], *sab, 1, true, spur).sichtbar, "targetname: erst per use");
        pruefe(!bhed::gegenstandAufstellen(m.entities[2], *sab, 1, true, spur).sichtbar &&
                   bhed::gegenstandAufstellen(m.entities[2], *sab, 2, true, spur).sichtbar,
               "spawnflags 512: auf g_spskill 1 entfernt, auf 2 da");
        const int vorher = spuren;
        const bhed::GegenstandAufstellung a3 = bhed::gegenstandAufstellen(m.entities[3], *sab, 1, true, spur);
        pruefe(a3.sichtbar && spuren == vorher && a3.ort[2] == 101.0F, "ITMSF_SUSPEND: kein Fallen, origin+1");
        pruefe(!bhed::gegenstandAufstellen(m.entities[4], *sab, 1, true, spur).sichtbar, "notsingle: entfernt");
        fest = true;
        pruefe(!bhed::gegenstandAufstellen(m.entities[0], *sab, 1, true, spur).sichtbar, "im Festen: entfernt");
    }

    std::printf("%s: %d Fehler\n", g_fehler == 0 ? "OK" : "FEHLER", g_fehler);
    return g_fehler == 0 ? 0 : 1;
}
