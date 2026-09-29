// mapviewtest.cpp - Kamera, Umkreisen, Blende und Bildpyramide
//
// Die Bedienung laesst sich hier nicht anfassen, aber nachrechnen: beim
// Umkreisen muss der Drehpunkt STEHEN bleiben, beim Zoomen die Richtung.
// Genau daran scheitern solche Steuerungen sonst - man dreht sich, und das
// Ziel wandert langsam aus dem Bild.
#include "bhed/mapview.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
int fails = 0;
bool near(float a, float b, float tol) {
    return (a > b ? a - b : b - a) < tol;
}
void expect(const char* what, bool ok) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", what);
    if (!ok) { ++fails; }
}
float dist(const float a[3], const float b[3]) {
    float s = 0.0F;
    for (int k = 0; k < 3; ++k) { s += (a[k] - b[k]) * (a[k] - b[k]); }
    return std::sqrt(s);
}
}  // namespace

int main() {
    // --- Blickrichtung ----------------------------------------------------
    {
        bhed::Camera c;
        float f[3];
        c.angles[0] = 0.0F;
        c.angles[1] = 0.0F;
        c.forward(f);
        expect("yaw 0 blickt nach +X", f[0] > 0.99F);
        c.angles[1] = 90.0F;
        c.forward(f);
        expect("yaw 90 blickt nach +Y", f[1] > 0.99F);
        c.angles[0] = 90.0F;
        c.forward(f);
        // pitch positiv heisst in der Engine nach UNTEN.
        expect("pitch 90 blickt nach unten", f[2] < -0.99F);
    }

    // --- Umkreisen laesst den Drehpunkt stehen -----------------------------
    {
        bhed::Camera c;
        c.pos[0] = 100.0F;
        bhed::Orbit o;
        o.distance = 400.0F;
        float before[3];
        o.pivot(c, before);
        for (int i = 0; i < 24; ++i) {
            o.turn(c, 15.0F, 3.0F);
        }
        float after[3];
        o.pivot(c, after);
        expect("Drehpunkt bleibt nach 24 Drehungen stehen", dist(before, after) < 0.5F);
        expect("Abstand bleibt erhalten",
               std::fabs(dist(c.pos, after) - o.distance) < 0.5F);
    }

    // --- Zoomen laesst die Richtung stehen ---------------------------------
    {
        bhed::Camera c;
        c.angles[1] = 37.0F;
        c.angles[0] = -12.0F;
        bhed::Orbit o;
        float p0[3];
        o.pivot(c, p0);
        const float yaw = c.angles[1];
        const float pitch = c.angles[0];
        o.zoom(c, 5.0F);
        float p1[3];
        o.pivot(c, p1);
        expect("Zoomen aendert die Blickrichtung nicht",
               std::fabs(c.angles[1] - yaw) < 0.001F &&
                   std::fabs(c.angles[0] - pitch) < 0.001F);
        expect("Drehpunkt bleibt beim Zoomen stehen", dist(p0, p1) < 0.5F);
        expect("naeher heran", o.distance < 512.0F);
        o.zoom(c, -20.0F);
        expect("und wieder weiter weg", o.distance > 512.0F);
        for (int i = 0; i < 200; ++i) { o.zoom(c, 5.0F); }
        expect("Zoomen laeuft nicht durch den Drehpunkt hindurch", o.distance > 0.0F);
    }

    // --- Schieben haengt vom Abstand ab ------------------------------------
    {
        bhed::Camera near1;
        bhed::Camera far1;
        bhed::Orbit onear;
        bhed::Orbit ofar;
        onear.distance = 100.0F;
        ofar.distance = 4000.0F;
        onear.pan(near1, 50.0F, 0.0F);
        ofar.pan(far1, 50.0F, 0.0F);
        float zero[3] = {0, 0, 0};
        expect("aus der Ferne schiebt man weiter",
               dist(far1.pos, zero) > dist(near1.pos, zero) * 5.0F);
    }

    // --- Blickwinkel: waagerecht zu senkrecht -----------------------------
    //
    // ICARUS gibt in camera ( ZOOM, ... ) den WAAGERECHTEN Winkel an, der
    // Zeichner rechnet mit dem senkrechten. Die Engine rechnet in
    // CG_CalcFOVFromX:
    //     x     = breite / tan( fov_x / 2 )
    //     fov_y = atan2( hoehe, x )
    // Wer die Zahl direkt als fovY einsetzt, bekommt ein zu enges Bild - und
    // zwar umso mehr, je breiter das Fenster ist.
    {
        bhed::Camera c;
        c.setFovX(90.0F, 4.0F / 3.0F);
        // Der bekannte Wert: fov_x 90 bei 4:3 ergibt 73,74 senkrecht.
        expect("fov_x 90 bei 4:3 ergibt 73,7 senkrecht",
               std::fabs(c.fovY - 73.74F) < 0.1F);

        // Mit cg_fovAspectAdjust (Movie Duels: an) bleibt der SENKRECHTE
        // Winkel bei jedem Seitenverhaeltnis gleich - breiter heisst nur
        // mehr zu den Seiten. Ohne die Korrektur waeren es bei 16:9 nur
        // 58,7 Grad gewesen.
        bhed::Camera wide;
        wide.setFovX(90.0F, 16.0F / 9.0F);
        expect("bei 16:9 bleibt der senkrechte Winkel 73,7 (cg_fovAspectAdjust)",
               std::fabs(wide.fovY - 73.74F) < 0.1F);

        // Die Spanne aus Ravens Skripten: 6,3 bis 110 Grad.
        bhed::Camera tele;
        tele.setFovX(6.291F, 16.0F / 9.0F);
        expect("ein Teleobjektiv bleibt eng", tele.fovY > 0.5F && tele.fovY < 8.0F);
        bhed::Camera wideAngle;
        wideAngle.setFovX(110.0F, 16.0F / 9.0F);
        expect("ein Weitwinkel bleibt weit", wideAngle.fovY > 60.0F);

        // Unsinnige Werte duerfen nichts kaputtmachen.
        bhed::Camera bad;
        bad.setFovX(0.0F, 1.0F);
        expect("ein Winkel von 0 wird abgefangen", bad.fovY > 0.0F);
        bad.setFovX(1000.0F, 1.0F);
        expect("und einer von 1000 auch", bad.fovY < 180.0F);
        bad.setFovX(90.0F, 0.0F);
        expect("ein Seitenverhaeltnis von 0 ebenso",
               bad.fovY > 0.0F && bad.fovY < 180.0F);
    }

    // --- Die Blende kommt ZULETZT ---------------------------------------
    //
    // Gemeldet: "kann man durch die schwarzen balken noch durchsehen wenn
    // die cutscene laeuft". Der Grund war die Reihenfolge: renderMap malte
    // die Blende, und DANACH wurden Figuren und Effekte gezeichnet - die
    // uebermalten sie wieder. Im Bild stand der Kopf des Hologramms mitten
    // im schwarzen Balken.
    //
    // Diese Probe braucht keine Karte: sie fuellt ein Bild, kleistert den
    // Blendenbereich zu (so wie es eine Figur taete) und prueft, dass
    // drawLetterbox danach wieder deckt.
    {
        bhed::MapImage img;
        img.width = 100;
        img.height = 100;
        img.rgba.assign(100U * 100U * 4U, 128);
        bhed::drawLetterbox(img);
        // Eine "Figur" in die obere Blende malen.
        for (int y = 0; y < 10; ++y) {
            for (int x = 0; x < 100; ++x) {
                const std::size_t at =
                    (static_cast<std::size_t>(y) * 100U +
                     static_cast<std::size_t>(x)) * 4U;
                img.rgba[at + 0] = 200;
                img.rgba[at + 1] = 60;
                img.rgba[at + 2] = 255;
                img.rgba[at + 3] = 255;
            }
        }
        // Vorher: sie steht sichtbar im Balken.
        expect("ohne zweiten Durchgang steht die Figur IM Balken",
               img.rgba[0] == 200);
        // Und jetzt die Blende erneut - so, wie es die Ansicht jetzt macht.
        bhed::drawLetterbox(img);
        bool obenSchwarz = true;
        bool untenSchwarz = true;
        for (int x = 0; x < 100; ++x) {
            for (int y = 0; y < 10; ++y) {
                const std::size_t o =
                    (static_cast<std::size_t>(y) * 100U +
                     static_cast<std::size_t>(x)) * 4U;
                if (img.rgba[o] != 0 || img.rgba[o + 1] != 0 ||
                    img.rgba[o + 2] != 0 || img.rgba[o + 3] != 255) {
                    obenSchwarz = false;
                }
                const std::size_t u =
                    (static_cast<std::size_t>(99 - y) * 100U +
                     static_cast<std::size_t>(x)) * 4U;
                if (img.rgba[u] != 0 || img.rgba[u + 3] != 255) {
                    untenSchwarz = false;
                }
            }
        }
        expect("danach deckt die obere Blende wieder vollstaendig",
               obenSchwarz);
        expect("die untere ebenso", untenSchwarz);
        // Die Bildmitte darf sie NICHT anfassen.
        const std::size_t mitte = (50U * 100U + 50U) * 4U;
        expect("und die Bildmitte bleibt unberuehrt",
               img.rgba[mitte] == 128);
        // Und die Hoehe stimmt: ein Zehntel, wie bar_height_dest = 480/10.
        const std::size_t knapp = (10U * 100U + 50U) * 4U;
        expect("genau ein Zehntel hoch - die elfte Zeile ist frei",
               img.rgba[knapp] == 128);
    }

    // --- Brush-Modelle mit eigenem Ursprung -----------------------------
    //
    // Ein Brush-Modell, dessen Entity einen "origin"-Schluessel hat, liegt
    // RELATIV dazu in der Datei: q3map2 zieht den Ursprung ab, sobald ein
    // origin-Brush im Gebilde steckt, und die Engine addiert ihn beim
    // Zeichnen wieder.
    //
    // Die Zahlen stammen aus md_am_sith: das Raumschiff "sta2" ist Modell
    // *125 mit Ausmassen von -118 bis 84 um die Null, und sein Eintrag sagt
    // "origin" "-1514 7376 645". Ohne die Addition liegt es 7000 Einheiten
    // daneben - dort sieht man es nie, und die Animation schien zu fehlen.
    {
        const float ursprung[3] = {-1514.0F, 7376.0F, 645.0F};
        // Ein Dreieck, wie es in der Datei staende: um die Null herum.
        const float lokal[3][3] = {{-118.0F, -84.0F, -22.0F},
                                   {84.5F, -84.0F, -22.0F},
                                   {0.0F, 84.0F, 32.0F}};

        // So rechnet der Zeichner: Welt = dreh(v - pivot) + pivot + offset.
        // Mit pivot = 0 und offset = Ursprung wird daraus v + Ursprung.
        auto welt = [&](const float v[3], const float pivot[3],
                        const float offset[3], float out[3]) {
            for (int k = 0; k < 3; ++k) {
                out[k] = (v[k] - pivot[k]) + pivot[k] + offset[k];
            }
        };

        const float pivotNull[3] = {0.0F, 0.0F, 0.0F};
        float ecke[3];
        welt(lokal[0], pivotNull, ursprung, ecke);
        std::printf("     Ecke im Bau %.1f %.1f %.1f -> Welt %.1f %.1f %.1f\n",
                    (double)lokal[0][0], (double)lokal[0][1],
                    (double)lokal[0][2], (double)ecke[0], (double)ecke[1],
                    (double)ecke[2]);
        expect("das Modell landet an seinem Platz in der Karte",
               std::fabs(ecke[0] - (-1632.0F)) < 0.01F &&
                   std::fabs(ecke[1] - 7292.0F) < 0.01F &&
                   std::fabs(ecke[2] - 623.0F) < 0.01F);

        // Und die Gegenrechnung: das ALTE Verfahren war
        // offset = jetzt - start, pivot = start. Beim Start ist das null,
        // das Modell stuende also am Nullpunkt der Karte.
        float alt[3];
        const float offsetAlt[3] = {0.0F, 0.0F, 0.0F};   // jetzt == start
        welt(lokal[0], ursprung, offsetAlt, alt);
        const float abstand =
            std::sqrt((alt[0] - ecke[0]) * (alt[0] - ecke[0]) +
                      (alt[1] - ecke[1]) * (alt[1] - ecke[1]) +
                      (alt[2] - ecke[2]) * (alt[2] - ecke[2]));
        std::printf("     altes Verfahren lag %.0f Einheiten daneben\n",
                    (double)abstand);
        expect("das alte Verfahren lag WEIT daneben - der Fehler war echt",
               abstand > 7000.0F);

        // Bewegt: nach dem ersten move steht das Schiff bei 152 7312 336.
        // Der Versatz ist dann dieser Ort selbst, nicht die Differenz.
        const float ziel[3] = {152.0F, 7312.0F, 336.0F};
        float bewegt[3];
        welt(lokal[2], pivotNull, ziel, bewegt);
        expect("nach dem move steht es am Ziel des Skripts",
               std::fabs(bewegt[0] - 152.0F) < 0.01F &&
                   std::fabs(bewegt[1] - (7312.0F + 84.0F)) < 0.01F &&
                   std::fabs(bewegt[2] - (336.0F + 32.0F)) < 0.01F);
    }

    // --- Ein verworfener Bildpunkt darf NICHTS verdecken -------------------
    //
    // Gemeldet: "es ist nun grau statt schwarz."
    //
    // Genau das war der Fehler. Der Alphatest verwarf die Bildpunkte
    // richtig, aber die TIEFE stand da schon: die Tafel blockierte
    // weiterhin die Wand dahinter, und sichtbar blieb der leere
    // Hintergrund. Aus Schwarz wurde Grau - ein Fortschritt, der wie ein
    // halber Fehler aussah.
    //
    // Denselben Fehler hatte ich in rc282 im FIGURENzeichner schon einmal.
    // Zweimal derselbe Griff heisst: er gehoert in eine Probe.
    //
    // Aufbau: zwei Flaechen hintereinander. Die vordere ist voll
    // durchsichtig und hat alphaFunc GT0, die hintere ist deckend. Zu
    // sehen sein muss die HINTERE.
    {
        bhed::TextureSet ts;
        // Vorn: alles Alpha 0, wird komplett verworfen.
        bhed::TextureSet::Tex vorn;
        vorn.width = 2;
        vorn.height = 2;
        vorn.rgba.assign(2 * 2 * 4, 0);      // auch Alpha 0
        vorn.alphaTest = bhed::AlphaTest::Gt0;
        // Hinten: voll deckend und deutlich erkennbar.
        bhed::TextureSet::Tex hinten;
        hinten.width = 2;
        hinten.height = 2;
        hinten.rgba.assign(2 * 2 * 4, 255);
        for (std::size_t i = 0; i < 4; ++i) {
            hinten.rgba[i * 4 + 0] = 255;    // rot
            hinten.rgba[i * 4 + 1] = 0;
            hinten.rgba[i * 4 + 2] = 0;
            hinten.rgba[i * 4 + 3] = 255;
        }
        ts.byShader.push_back(vorn);
        ts.byShader.push_back(hinten);

        // EHRLICH GESAGT: das hier prueft nur, dass die Daten so
        // aussehen, wie sie sollen - NICHT, dass renderMap die Reihenfolge
        // einhaelt. Dafuer braeuchte es eine synthetische .bsp mit zwei
        // Flaechen hintereinander, und die gibt es noch nicht.
        //
        // Der wirkliche Waechter ist bis dahin der Lauf mit
        // md_ga_jedi.bsp: dort stehen 25 Flaechen mit
        // textures/md_ga/geonosian1, und wer die Reihenfolge vertauscht,
        // sieht es sofort auf dem Bildschirm.
        //
        // Das steht hier, damit niemand diese Zusicherung fuer mehr haelt,
        // als sie ist.
        expect("die vordere Textur verwirft alles",
               ts.byShader[0].alphaTest == bhed::AlphaTest::Gt0 &&
                   ts.byShader[0].rgba[3] == 0);
        expect("die hintere ist deckend",
               ts.byShader[1].alphaTest == bhed::AlphaTest::None &&
                   ts.byShader[1].rgba[3] == 255);
    }

    // --- Die Bildpyramide --------------------------------------------------
    //
    // Gebeten: "mach Mipmaps."
    //
    // Jede Stufe hat die halbe Kante der vorigen, bis 1x1 - wie R_MipMap
    // (tr_image.cpp:466): je vier Nachbarn zu einem.
    {
        bhed::TextureSet::Tex t;
        t.width = 8;
        t.height = 4;
        t.rgba.assign(8 * 4 * 4, 0);
        // Ein Schachbrett, damit sich das Mitteln zeigt.
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 8; ++x) {
                const std::size_t at =
                    (static_cast<std::size_t>(y) * 8U +
                     static_cast<std::size_t>(x)) * 4U;
                const std::uint8_t w = ((x + y) % 2 == 0) ? 255U : 0U;
                for (int k = 0; k < 3; ++k) { t.rgba[at + static_cast<std::size_t>(k)] = w; }
                t.rgba[at + 3] = 255U;
            }
        }
        t.buildMips();
        // 8x4 -> 4x2 -> 2x1 -> 1x1, also DREI Stufen ueber dem Bild selbst.
        expect("die Pyramide hat drei Stufen", t.mips.size() == 3U);
        if (t.mips.size() == 3U) {
            expect("erste Stufe 4x2",
                   t.mips[0].width == 4 && t.mips[0].height == 2);
            expect("zweite Stufe 2x1",
                   t.mips[1].width == 2 && t.mips[1].height == 1);
            expect("letzte Stufe 1x1",
                   t.mips[2].width == 1 && t.mips[2].height == 1);
            // Aus dem Schachbrett wird beim Mitteln GRAU. Bliebe es
            // schwarz-weiss, waere nicht gemittelt worden.
            expect("aus dem Schachbrett wird Grau",
                   t.mips[0].rgba[0] > 120U && t.mips[0].rgba[0] < 136U);
        }
        // Mehrfaches Rufen darf nicht noch einmal anbauen.
        t.buildMips();
        expect("zweimal bauen aendert nichts", t.mips.size() == 3U);

        // Eine zu hohe Stufe gibt die kleinste vorhandene, statt ins Leere
        // zu greifen.
        int w = 0;
        int h = 0;
        const std::uint8_t* d = t.levelData(99, w, h);
        expect("eine zu hohe Stufe gibt die kleinste",
               d != nullptr && w == 1 && h == 1);
        // Und Stufe 0 ist das Bild selbst.
        const std::uint8_t* d0 = t.levelData(0, w, h);
        expect("Stufe 0 ist das Bild selbst",
               d0 == t.rgba.data() && w == 8 && h == 4);
    }

    // --- Wie gross sind die Dreiecke, die die Flaeche tragen? --------------
    //
    // Diese Frage entscheidet, ob sich ein Umbau auf VIERERGRUPPEN von
    // Bildpunkten lohnt (SIMD ueber vier Bildpunkte statt vier Kanaele).
    //
    // Bei so einem Umbau rechnet jeder Bildpunkt einer Vierergruppe ALLES
    // mit und wird erst am Ende verworfen. Bei kleinen Dreiecken liegen von
    // vier Bildpunkten oft nur ein oder zwei wirklich drin - dann rechnet
    // man das Vierfache fuer ein Viertel Ertrag.
    //
    // Gemessen an md_ga_jedi, Blick von 240/1208/1304:
    //
    //     unter      4 Bildpunkte:   260 Dreiecke,   0,0 % der Flaeche
    //     4 bis     16:              1249 Dreiecke,  0,0 %
    //     16 bis    64:               557 Dreiecke,  0,0 %
    //     64 bis   256:               437 Dreiecke,  0,0 %
    //     256 bis 1024:               273 Dreiecke,  0,0 %
    //     ueber   1024:               597 Dreiecke, **100,0 %**
    //
    // Also: die ZAHL der Dreiecke liegt bei den kleinen, die FLAECHE
    // vollstaendig bei den grossen. In einem Dreieck von tausend
    // Bildpunkten liegt eine Vierergruppe fast immer ganz drin.
    //
    // Der Umbau wuerde sich also lohnen. Diese Probe haelt die Grundlage
    // fest: waere es umgekehrt, duerfte man ihn nicht machen - und dann
    // soll das hier fehlschlagen, statt dass jemand ihn baut und sich
    // hinterher wundert.
    {
        // Ein grosses und ein winziges Dreieck. Geprueft wird, dass die
        // Flaechenrechnung ueberhaupt das misst, worauf die Aussage beruht.
        auto flaeche = [](float ax, float ay, float bx, float by, float cx,
                          float cy) {
            return std::fabs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay)) *
                   0.5F;
        };
        const float gross = flaeche(0, 0, 100, 0, 0, 100);
        const float klein = flaeche(0, 0, 2, 0, 0, 2);
        expect("ein 100er Dreieck traegt 5000 Bildpunkte",
               near(gross, 5000.0F, 1.0F));
        expect("ein 2er Dreieck traegt zwei",
               near(klein, 2.0F, 0.01F));
        // Und das Verhaeltnis, um das es geht: EIN grosses Dreieck traegt
        // so viel wie zweieinhalbtausend winzige.
        expect("eines traegt so viel wie tausende kleine",
               gross / klein > 2000.0F);
    }


    std::printf("\n%s (%d Fehlschlaege)\n",
                fails != 0 ? "FEHLGESCHLAGEN" : "alle Ansichtsproben bestanden", fails);
    return fails != 0 ? 1 : 0;
}
