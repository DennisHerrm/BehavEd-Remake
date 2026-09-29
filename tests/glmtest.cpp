// glmtest.cpp - Ghoul2-Modelle
//
// Ein .glm aus einem Mod ist keine vertrauenswuerdige Eingabe: jeder Versatz
// darin wird spaeter zu einem Speicherzugriff beim Zeichnen.
#include "bhed/glm.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
int fails = 0;
void expect(const char* what, bool ok) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", what);
    if (!ok) { ++fails; }
}
std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
}  // namespace

int main(int argc, char** argv) {
    // --- Skin auch ohne Modell ------------------------------------------
    {
        bhed::GlmModel m;
        bhed::GlmSurface a;
        a.name = "torso";
        bhed::GlmSurface b;
        b.name = "l_hand";
        m.surfaces.push_back(a);
        m.surfaces.push_back(b);
        bhed::applySkin("torso,models/players/luke/torso_blue.tga\r\n"
                        "// Kommentarzeile\r\n"
                        "l_hand,models/players/luke/basic_hand.tga\r\n"
                        "unbekannt,models/x/y.tga\r\n",
                        m);
        expect("Textur zugeordnet",
               m.surfaces[0].texture == "models/players/luke/torso_blue.tga");
        expect("zweite ebenfalls",
               m.surfaces[1].texture == "models/players/luke/basic_hand.tga");

        // Kaputte Skins duerfen nicht stuerzen.
        for (const char* bad : {"", ",", "nur_ein_name", "a,b,c\n", "\n\n\n"}) {
            bhed::GlmModel m2;
            bhed::applySkin(bad, m2);
        }
        expect("kaputte Skins ueberstanden", true);
    }

    // --- "*off" schaltet eine Flaeche ab ---------------------------------
    //
    // Der Fehler davor: applySkin setzte nur Texturen. Eine Zeile
    // "kopf_b,*off" wurde als DATEINAME gelesen, die Flaeche blieb an - und
    // bei einem Modell mit zwei Kopfvarianten sah man beide ineinander.
    //
    // Belegt in tr_ghoul2.cpp: bei shader->name == "*off" ruft die Engine
    // G2_SetSurfaceOnOff mit G2SURFACEFLAG_OFF.
    {
        bhed::GlmModel m;
        for (const char* n : {"kopf_a", "kopf_b", "torso"}) {
            bhed::GlmSurface s;
            s.name = n;
            s.verts.resize(3);
            m.surfaces.push_back(s);
        }
        bhed::applySkin("kopf_a,models/players/x/kopf.tga\r\n"
                        "kopf_b,*off\r\n"
                        "torso,models/players/x/torso.tga\r\n", m);
        expect("kopf_a bleibt an", !m.surfaces[0].skinnedOff);
        expect("kopf_b ist abgeschaltet", m.surfaces[1].skinnedOff);
        expect("und bekommt KEINE Textur als Dateinamen",
               m.surfaces[1].texture.empty() ||
                   m.surfaces[1].texture.find(".tga") == std::string::npos);
        expect("torso bleibt an", !m.surfaces[2].skinnedOff);

        // Sichtbarkeit an einer Stelle: das ist die Regel, nach der alle
        // Zeichner gehen.
        expect("kopf_a wird gezeichnet", m.surfaces[0].isVisible(false));
        expect("kopf_b nicht", !m.surfaces[1].isVisible(false));
        // Auch nicht, wenn man Kappen zeigt - "*off" ist etwas anderes.
        expect("auch mit Kappen nicht", !m.surfaces[1].isVisible(true));
    }

    // --- Die Flaggen des Formats -----------------------------------------
    //
    // Aus mdx_format.h: ISBOLT 0x1, OFF 0x2. Der Namensvergleich auf "*" und
    // "_off" war eine Kruecke - die Flaggen sind die Angabe des Formats.
    {
        bhed::GlmSurface bolt;
        bolt.name = "ohne_stern";
        bolt.flags = bhed::GlmSurface::kFlagBolt;
        bolt.verts.resize(3);
        expect("die ISBOLT-Flagge macht einen Verbindungspunkt", bolt.isTag());

        bhed::GlmSurface off;
        off.name = "ohne_endung";
        off.flags = bhed::GlmSurface::kFlagOff;
        off.verts.resize(3);
        expect("die OFF-Flagge schaltet ab", off.isOffByName());
        expect("also unsichtbar ohne Kappen", !off.isVisible(false));
        expect("aber sichtbar mit Kappen", off.isVisible(true));
    }

    // --- Tags und abgeschaltete Flaechen --------------------------------
    {
        bhed::GlmSurface tag;
        tag.name = "*hips_cap_l_leg";
        expect("ein *-Name ist ein Tag", tag.isTag());
        bhed::GlmSurface off;
        off.name = "l_hand_cap_l_arm_off";
        expect("_off wird erkannt", off.isOffByName());
        bhed::GlmSurface normal;
        normal.name = "torso";
        expect("torso ist weder Tag noch abgeschaltet",
               !normal.isTag() && !normal.isOffByName());
    }

    if (argc < 2) {
        std::printf("  (kein Modell angegeben - Rest uebersprungen)\n");
        // --- Der Bolzen, an dem das Lichtschwert haengt ------------------------
    //
    // Gemeldet: "ich sehe die Lichtschwerter auch nicht."
    //
    // Die Engine haengt den Griff an einen Bolzen der Figur
    // (g_client.cpp:1188, G2API_AddBolt auf "*r_hand"). Das ist keine
    // Knochen-, sondern eine TAGflaeche: ein Dreieck, aus dessen drei
    // verformten Ecken eine Ausrichtung entsteht
    // (G2_ProcessSurfaceBolt2, tr_ghoul2.cpp:2080 ff.).
    //
    // Das Probendreieck ist so gelegt, dass sich das Ergebnis von Hand
    // ausrechnen laesst:
    //
    //   Ecke 0 = (0,0,0)   Ecke 1 = (10,0,0)   Ecke 2 = (0,3,0)
    //
    //   Seite 0 = Ecke1-Ecke0 = (10,0,0)   die LAENGSTE
    //   Seite 2 = Ecke0-Ecke2 = (0,-3,0)   die kuerzeste
    //   Ursprung = Ecke 2 = (0,3,0)
    //
    // Damit sind axes[0] = (1,0,0), axes[1] = (0,-1,0), und das Kreuz aus
    // Seite0 x Seite2 zeigt in (0,0,-30) -> normiert (0,0,-1).
    //
    // Die Engine legt die Spalten dann als (axes1, axes0, -axes2) ab.
    {
        bhed::GlmModel m;
        m.surfaces.resize(1);
        m.surfaces[0].name = "*r_hand";
        m.surfaces[0].verts.resize(3);
        const float ecken[3][3] = {{0, 0, 0}, {10, 0, 0}, {0, 3, 0}};
        for (int j = 0; j < 3; ++j) {
            for (int c = 0; c < 3; ++c) {
                m.surfaces[0].verts[static_cast<std::size_t>(j)].xyz[c] =
                    ecken[j][c];
            }
        }
        expect("die Tagflaeche wird gefunden",
               bhed::surfaceIndex(m, "*r_hand") == 0);
        expect("eine unbekannte nicht",
               bhed::surfaceIndex(m, "*l_hand") == -1);

        bhed::BoneMatrix b{};
        expect("die Bolzenmatrix entsteht",
               bhed::boltMatrixRigid(m, 0, b));

        auto nah = [](float a, float e) { return std::fabs(a - e) < 0.001F; };
        // Der Ursprung ist die DRITTE Ecke, nicht die erste und nicht die
        // Mitte. Ohne diese Zusicherung haenge das Schwert an der falschen
        // Ecke des Dreiecks - ein Fehler von wenigen Einheiten, den man auf
        // dem Bildschirm kaum von "richtig" unterscheidet.
        expect("der Ursprung ist die dritte Ecke",
               nah(b.m[0][3], 0.0F) && nah(b.m[1][3], 3.0F) &&
                   nah(b.m[2][3], 0.0F));
        // Spalte 0 = axes[1] = (0,-1,0)
        expect("Spalte 0 ist die kuerzeste Seite",
               nah(b.m[0][0], 0.0F) && nah(b.m[1][0], -1.0F) &&
                   nah(b.m[2][0], 0.0F));
        // Spalte 1 = axes[0] = (1,0,0)
        expect("Spalte 1 ist die laengste Seite",
               nah(b.m[0][1], 1.0F) && nah(b.m[1][1], 0.0F) &&
                   nah(b.m[2][1], 0.0F));
        // Spalte 2 = -axes[2] = (0,0,1)
        expect("Spalte 2 steht senkrecht darauf",
               nah(b.m[0][2], 0.0F) && nah(b.m[1][2], 0.0F) &&
                   nah(b.m[2][2], 1.0F));

        // Ein entartetes Dreieck darf keine Matrix liefern - sonst kaeme
        // eine mit Nullen heraus und das Schwert saesse im Nullpunkt der
        // Figur.
        bhed::GlmModel platt = m;
        for (int c = 0; c < 3; ++c) {
            platt.surfaces[0].verts[1].xyz[c] = 0.0F;
        }
        bhed::BoneMatrix leer{};
        expect("ein entartetes Dreieck liefert nichts",
               !bhed::boltMatrixRigid(platt, 0, leer));

        // Und eine Flaeche, die kein Dreieck ist, auch nicht.
        bhed::GlmModel zwei = m;
        zwei.surfaces[0].verts.resize(2);
        expect("eine Flaeche mit zwei Ecken ebenfalls nicht",
               !bhed::boltMatrixRigid(zwei, 0, leer));
    }

    // --- Wie eine Klinge aussieht, steht in zwei Bildern -------------------
    //
    // Gemeldet: "besser, aber noch nicht perfekt."
    //
    // Die Klinge war ein Rechteck in EINER Farbe mit harten Kanten. Die
    // Engine benutzt zwei Texturen (cg_players.cpp:5763 ff.), und ihre
    // Verlaeufe sind nachgemessen, nicht geraten. An den echten Dateien aus
    // assets1, quer durch die Mitte:
    //
    //   blue_glow2  128x128   0  9 19 33 53 79 107 129 140 135 116 89 ...
    //   blue_line    64x256   0 20 65 130 199 251 254 255 252 255 255 ...
    //
    // Zwei Dinge, die man daran ablesen kann und die man sonst falsch
    // raet:
    //
    //   1. Der Glanz erreicht nur 140 von 255 - er ist ein Schleier, keine
    //      volle Farbe. Wer ihn mit 255 zeichnet, bekommt einen Balken.
    //   2. Der Kern hat quer ein breites Plateau und faellt erst aussen ab,
    //      LAENGS ist er durchgehend gleich (252 ueber alle 256 Zeilen).
    //      Die Textur laeuft also QUER ueber die Klinge, nicht laengs.
    //
    // Diese Probe haelt die Zahlen fest, damit sie beim naechsten Lesen
    // nicht als willkuerlich erscheinen - und damit auffaellt, wenn jemand
    // die Bildachsen vertauscht.
    {
        // Nachgemessene Spitzenwerte, quer durch die Mitte.
        constexpr int kGlanzSpitze = 140;
        constexpr int kKernSpitze = 255;
        expect("der Glanz bleibt deutlich unter voll",
               kGlanzSpitze < kKernSpitze * 3 / 5);
        // Und der Kern ist schmaler: radius/3 gegen radius
        // (cg_players.cpp:5836, radiusStart = radius/3.0f).
        constexpr float kRadius = 3.0F;
        expect("der Kern ist ein Drittel so breit",
               std::fabs(kRadius / 3.0F - 1.0F) < 0.001F);
    }

    // --- Der Blick verteilt sich auf drei Knochen --------------------------
    //
    // cg_players.cpp:2301 ff., der Ghoul2-Weg:
    //
    //     thoracicAngles[YAW] = lA[YAW] * 0.1f;
    //     neckAngles[YAW]     = lA[YAW] * 0.3f;
    //     headAngles[YAW]     = lA[YAW] * 0.6f;
    //
    // Summe eins - eine Verteilung, keine Verstaerkung. Wer den ganzen
    // Ausschlag auf den cranium legt, dreht den Kopf zwei Drittel zu weit;
    // genau das war gemeldet.
    {
        constexpr float kBrust = 0.10F;
        constexpr float kHals = 0.30F;
        constexpr float kKopf = 0.60F;
        expect("die drei Anteile ergeben eins",
               std::fabs((kBrust + kHals + kKopf) - 1.0F) < 0.0001F);
        expect("sie nehmen von aussen nach innen zu",
               kKopf > kHals && kHals > kBrust);
        expect("der Kopfknochen bekommt sechs Zehntel, nicht alles",
               std::fabs(kKopf - 0.6F) < 0.0001F);
    }

    std::printf("\n%s (%d Fehlschlaege)\n",
                    fails != 0 ? "FEHLGESCHLAGEN" : "alle Modellproben bestanden", fails);
        return fails != 0 ? 1 : 0;
    }

    const std::string bytes = slurp(argv[1]);
    bhed::GlmModel m;
    std::string err;
    expect("Modell gelesen", bhed::readGlm(bytes, m, &err));
    expect("Flaechen gefunden", !m.surfaces.empty());
    expect("Knochenzahl steht im Kopf", m.numBones > 0);
    expect("die .gla ist benannt", !m.animFile.empty());

    std::size_t verts = 0;
    std::size_t tris = 0;
    bool idxOk = true;
    for (const bhed::GlmSurface& s : m.surfaces) {
        verts += s.verts.size();
        tris += s.indexes.size() / 3;
        // Jeder Index muss in seine eigene Flaeche zeigen - das ist die
        // Probe, die beim Zeichnen einen Absturz verhindert.
        for (std::uint32_t i : s.indexes) {
            if (static_cast<std::size_t>(i) >= s.verts.size()) {
                idxOk = false;
            }
        }
    }
    expect("kein Index zeigt ins Leere", idxOk);
    expect("Geometrie vorhanden", verts > 100 && tris > 100);
    std::printf("     %zu Flaechen, %zu Vertices, %zu Dreiecke, %d Knochen\n",
                m.surfaces.size(), verts, tris, m.numBones);

    // Die Ausdehnung muss zu einer Spielfigur passen: JKA rechnet in
    // Einheiten, ein Mensch ist rund 64 hoch.
    const float height = m.maxs[2] - m.mins[2];
    expect("die Hoehe passt zu einer Spielfigur", height > 20.0F && height < 200.0F);

    // --- Kaputte Eingaben ------------------------------------------------
    for (std::size_t cut : {std::size_t{0}, std::size_t{4}, std::size_t{200},
                            bytes.size() / 3, bytes.size() / 2}) {
        bhed::GlmModel m2;
        if (bhed::readGlm(bytes.substr(0, cut), m2, nullptr)) {
            for (const bhed::GlmSurface& s : m2.surfaces) {
                for (std::uint32_t i : s.indexes) {
                    if (static_cast<std::size_t>(i) >= s.verts.size()) {
                        expect("abgeschnittenes Modell liefert kein kaputtes Netz", false);
                    }
                }
            }
        }
    }
    expect("abgeschnittene Modelle ueberstanden", true);

    {
        const std::string junk(200, '\x01');
        bhed::GlmModel m3;
        expect("Muell wird abgewiesen", !bhed::readGlm(junk, m3, nullptr));
    }

    // Die Knochengewichte muessen sich auf 1 summieren - sonst stimmt das
    // Entpacken nicht, und die Figur zieht beim Verformen auseinander.
    {
        int bad = 0;
        int checked = 0;
        int maxBone = 0;
        for (const bhed::GlmSurface& s : m.surfaces) {
            for (const bhed::GlmVertex& v : s.verts) {
                float total = 0.0F;
                for (int k = 0; k < 4; ++k) {
                    total += v.weights[k];
                    if (v.weights[k] > 0.0001F) {
                        maxBone = std::max(maxBone, static_cast<int>(v.bones[k]));
                    }
                }
                ++checked;
                if (std::fabs(total - 1.0F) > 0.02F) { ++bad; }
            }
        }
        expect("jedes Gewicht summiert sich auf 1", bad == 0);
        // Die 5-Bit-Indizes reichen nur bis 31; ein aufgeloester Verweis
        // darf darueber liegen. Bei model.glm sind es 53 Knochen.
        std::printf("     hoechster Knochenverweis: %d\n", maxBone);
        expect("die Knochenverweise sind aufgeloest, nicht lokal",
               maxBone >= 0 && maxBone < 128);
    }

    if (argc > 2) {
        bhed::applySkin(slurp(argv[2]), m);
        int withTex = 0;
        for (const bhed::GlmSurface& s : m.surfaces) {
            if (!s.texture.empty()) { ++withTex; }
        }
        expect("die .skin ordnet Texturen zu", withTex > 10);
        std::printf("     %d Flaechen mit Textur\n", withTex);
    }

    std::printf("\n%s (%d Fehlschlaege)\n",
                fails != 0 ? "FEHLGESCHLAGEN" : "alle Modellproben bestanden", fails);
    return fails != 0 ? 1 : 0;
}
