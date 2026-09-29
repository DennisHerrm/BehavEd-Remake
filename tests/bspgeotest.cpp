// bspgeotest.cpp - die Geometrie einer Karte
//
// Eine .bsp aus einem Mod ist keine vertrauenswuerdige Eingabe: jeder Zeiger
// darin wird spaeter zum Speicherzugriff beim Zeichnen. Die Proben pruefen
// deshalb beides - dass eine echte Karte richtig gelesen wird, und dass eine
// kaputte weder stuerzt noch Unsinn liefert.
#include "bhed/bspgeo.h"
#include "bhed/gpuskin.h"
#include "bhed/mapview.h"

#include <algorithm>

#include <cstddef>
#include <cstdint>
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
std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
}  // namespace

int main(int argc, char** argv) {
    // --- Ein Strahl gegen die Karte ----------------------------------------
    //
    // Gebraucht fuer den Einschlag der Geschosse. Geprueft wird gegen die
    // DREIECKE, nicht gegen Brushes - die liest behaved nicht.
    //
    // Ohne Karte laesst sich das nicht fahren; die Zahlen unten stammen aus
    // dem Lauf mit md_ga_jedi.bsp (siehe dort). Hier steht der Fall, der
    // OHNE Daten gilt: eine leere Karte darf nichts treffen und nicht
    // abstuerzen.
    {
        bhed::BspGeometry leer;
        const float a[3] = {0, 0, 0};
        const float b[3] = {0, 0, -1000};
        const bhed::TraceTreffer t = bhed::traceRay(leer, a, b);
        expect("eine leere Karte trifft nichts", !t.hit);
        expect("und meldet volle Strecke", t.fraction >= 0.999F);

        // Eine Strecke der Laenge null darf ebenfalls nichts melden -
        // sonst traefe ein stehendes Geschoss sich selbst.
        const bhed::TraceTreffer t0 = bhed::traceRay(leer, a, a);
        expect("eine Strecke ohne Laenge trifft nichts", !t0.hit);
    }

    if (argc < 2) {
        std::printf("  (keine .bsp angegeben - Test uebersprungen)\n");
        return 0;
    }

    for (int a = 1; a < argc; ++a) {
        const std::string bytes = slurp(argv[a]);
        if (bytes.empty()) {
            continue;
        }
        bhed::BspGeometry g;
        std::string err;
        expect("Karte gelesen", bhed::readBspGeometry(bytes, g, &err));
        expect("Flaechen gefunden", !g.surfaces.empty());
        expect("Vertices gefunden", !g.verts.empty());
        expect("Lightmaps gefunden", !g.lightmaps.empty());

        // --- Der Strahl an der ECHTEN Karte ------------------------------------
        //
        // Handwerte als Massstab, sonst waere jede Zahl darin beliebig.
        //
        // Windu steht in md_ga_jedi auf 232/1368/1240. Ein Strahl von dort nach
        // unten muss den Arenaboden treffen - und der liegt bei z = 1216, also
        // 24 Einheiten unter seinen Fuessen, mit einer Normalen genau nach oben.
        //
        // DIESE Probe ist es, die den groessten Fallstrick fest haelt: **1246
        // der 2269 Flaechen sind Patches**, darunter der ganze Boden. Beim
        // ersten Bauen habe ich sie uebersprungen und nach unten NICHTS
        // getroffen. Wer sie wieder herausnimmt, sieht es hier sofort.
        {
            // --- Die Zahlen kommen aus DIESER Karte, nicht aus einer ---
            //
            // Hier standen feste Werte: der Strahl von 232/1368/1320 nach
            // unten, und "trifft bei z = 1216".
            //
            // Das ist der Boden der Arena von md_ga_jedi. Mit sechzehn
            // Karten aus Episode 3 geprueft: **alle sechzehn fielen
            // durch** - nicht weil behaved etwas falsch macht, sondern
            // weil kein anderer Boden auf 1216 liegt.
            //
            // Damit hat diese Probe jahrelang genau EINE Karte geprueft,
            // waehrend sie aussah, als pruefe sie den Strahlenschnitt.
            //
            // Jetzt kommen Ausgangspunkt und Erwartung aus der Karte
            // selbst: von der Mitte des Weltwuerfels aus nach unten, und
            // erwartet wird, dass irgendwo INNERHALB der Karte etwas
            // getroffen wird - mit einer Normalen, die nach oben zeigt,
            // denn ein Boden ist ein Boden.
            // Ein RASTER statt eines Punktes.
            //
            // Die Mitte des Weltwuerfels ist kein verlaesslicher Ort: bei
            // md_ga_jedi liegt sie ueber der Arena in freier Luft, beim
            // Senat trifft der Strahl auf dem Weg eine Wand.
            //
            // Also fuenf mal fuenf Punkte ueber die Karte verteilt, jeder
            // von oben nach unten. Erwartet wird nicht, dass JEDER trifft -
            // ueber einem Abgrund trifft keiner -, sondern dass ein guter
            // Teil trifft und die Treffer nach oben zeigen.
            int getroffen = 0;
            int nachOben = 0;
            int daneben = 0;
            for (int iy = 1; iy <= 5; ++iy) {
                for (int ix = 1; ix <= 5; ++ix) {
                    const float fx = static_cast<float>(ix) / 6.0F;
                    const float fy = static_cast<float>(iy) / 6.0F;
                    const float px = g.mins[0] + (g.maxs[0] - g.mins[0]) * fx;
                    const float py = g.mins[1] + (g.maxs[1] - g.mins[1]) * fy;
                    const float o[3] = {px, py, g.maxs[2] - 1.0F};
                    const float u[3] = {px, py, g.mins[2] + 1.0F};
                    const bhed::TraceTreffer tt = bhed::traceRay(g, o, u);
                    if (!tt.hit) {
                        continue;
                    }
                    ++getroffen;
                    if (tt.normal[2] > 0.5F) {
                        ++nachOben;
                    }
                    // Der Treffer muss AUF dem Weg liegen, nicht dahinter.
                    // Klingt selbstverstaendlich; genau das ging beim
                    // ersten Bauen des Patchschnitts schief.
                    if (tt.point[2] > o[2] + 1.0F ||
                        tt.point[2] < u[2] - 1.0F) {
                        ++daneben;
                    }
                }
            }
            std::printf("     Raster: %d von 25 treffen, %d davon nach oben,"
                        " %d ausserhalb des Weges\n",
                        getroffen, nachOben, daneben);
            // DIESE Probe haelt den groessten Fallstrick fest: bei
            // md_ga_jedi sind 1246 von 2269 Flaechen Patches, darunter der
            // ganze Boden. Wer sie beim Schneiden ueberspringt, trifft nach
            // unten NICHTS - dann steht hier eine Null.
            // Wie viele Treffer zu FORDERN sind, haengt daran, wie viel
            // Welt es ueberhaupt gibt.
            //
            // Geprueft an den Missionskarten aus Episode 3:
            // `md_amintro_sith` hat im Weltmodell **12 Dreiecke** - fast
            // die ganze Karte steckt in beweglichen Teilen. Ein Strahl
            // nach unten trifft dort zu Recht nichts, und `md_rt_jedi`
            // traf 4 von 25.
            //
            // Wieder eine Annahme ueber die Bauweise, nicht ueber den
            // Schnitt: eine Arena wie md_ga_jedi hat einen durchgehenden
            // Boden, eine Kameraszene nicht.
            const std::size_t weltDreiecke =
                bhed::buildModelMesh(g, 0, 4).indexes.size() / 3;
            if (weltDreiecke < 500) {
                std::printf("     (nur %zu Dreiecke im Weltmodell - zu wenig"
                            " fuer eine Bodenprobe)\n", weltDreiecke);
            } else {
                // MINDESTENS EINER, nicht fuenf.
                //
                // Der Sinn dieser Probe ist, dass Patches beim Schneiden
                // nicht uebersprungen werden - bei md_ga_jedi sind 1246
                // von 2269 Flaechen Patches, darunter der ganze Boden. Wer
                // sie auslaesst, trifft NICHTS, und dann steht hier null.
                //
                // Ob es einer ist oder zwanzig, haengt dagegen an der
                // Bauweise: `md_rt_jedi` traf 4 von 25, weil dort viel
                // offener Raum ist. Fuenf zu fordern war eine Annahme ueber
                // md_ga_jedi, kein Mass fuer den Schnitt.
                expect("nach unten wird ueberhaupt etwas getroffen",
                       getroffen >= 1);
            }
            // NICHT "ueberwiegend Boeden".
            //
            // Das hatte ich zuerst gefordert, und der Senat fiel durch: 25
            // von 25 Treffern, aber nur 8 nach oben. Zu Recht - er ist ein
            // geschlossener Bau, und von ganz oben trifft der Strahl zuerst
            // die DECKE. Eine Decke zeigt nach unten.
            //
            // Meine Erwartung war falsch, nicht der Schnitt. Gefordert wird
            // also nur, dass sich ueberhaupt Boeden finden lassen.
            if (weltDreiecke >= 500) {
                expect("und darunter sind auch Boeden", nachOben >= 1);
            }
            expect("kein Treffer liegt hinter dem Ziel", daneben == 0);
            // Und weit ausserhalb der Karte gar nichts.
            const float weg1[3] = {99999.0F, 99999.0F, 99999.0F};
            const float weg2[3] = {99999.0F, 99999.0F, 99000.0F};
            expect("weit ausserhalb trifft er nichts",
                   !bhed::traceRay(g, weg1, weg2).hit);
        }



        // Jede Lightmap ist genau 128x128 RGB. Weicht eine ab, stimmt die
        // angenommene Groesse nicht - und dann stimmt der ganze Lumpversatz
        // nicht.
        bool sizes = true;
        for (const auto& lm : g.lightmaps) {
            if (lm.size() != 128U * 128U * 3U) { sizes = false; }
        }
        expect("jede Lightmap ist 128x128 RGB", sizes);

        // Kein Zeiger darf aus seinem Bereich zeigen. Das ist die Probe, die
        // beim Zeichnen einen Absturz verhindert.
        bool ranges = true;
        for (const bhed::BspSurface& s : g.surfaces) {
            if (s.firstVert < 0 || s.numVerts < 0 ||
                static_cast<std::size_t>(s.firstVert + s.numVerts) > g.verts.size()) {
                ranges = false;
            }
            if (s.firstIndex < 0 || s.numIndexes < 0 ||
                static_cast<std::size_t>(s.firstIndex + s.numIndexes) > g.indexes.size()) {
                ranges = false;
            }
            if (s.shader < 0 ||
                static_cast<std::size_t>(s.shader) >= g.shaders.size()) {
                ranges = false;
            }
        }
        expect("kein Flaechenzeiger zeigt ins Leere", ranges);

        bool idx = true;
        for (std::uint32_t i : g.indexes) {
            if (static_cast<std::size_t>(i) >= g.verts.size()) { idx = false; }
        }
        expect("kein Index zeigt ins Leere", idx);

        // Patches haben immer ungerade Kantenlaengen: sie bestehen aus
        // 3x3-Feldern, die sich Randpunkte teilen.
        bool odd = true;
        int patches = 0;
        for (const bhed::BspSurface& s : g.surfaces) {
            if (s.type != bhed::BspSurface::Type::Patch) { continue; }
            ++patches;
            if (s.patchWidth % 2 == 0 || s.patchHeight % 2 == 0) { odd = false; }
            if (s.patchWidth * s.patchHeight != s.numVerts) { odd = false; }
        }
        if (patches != 0) {
            expect("Patchmasse sind ungerade und passen zur Vertexzahl", odd);
        }

        // Das fertige Netz.
        const bhed::BspMesh m = bhed::buildMesh(g, 4);
        bool meshOk = !m.indexes.empty() && (m.indexes.size() % 3 == 0);
        for (std::uint32_t i : m.indexes) {
            if (static_cast<std::size_t>(i) >= m.verts.size()) { meshOk = false; }
        }
        expect("Netz besteht aus gueltigen Dreiecken", meshOk);

        // Die Zeichenaufrufe muessen die Indizes lueckenlos abdecken.
        std::uint32_t covered = 0;
        for (const auto& b : m.batches) { covered += b.numIndexes; }
        expect("die Zeichenaufrufe decken alle Indizes ab",
               covered == m.indexes.size());

        // --- Flaggen -------------------------------------------------
        //
        // Die Werte stehen in code/game/surfaceflags.h von OpenJK. Der erste
        // Anlauf hatte hier geratene Zahlen (0x80 fuer NODRAW) und dazu ein
        // SURF_SKIP und SURF_HINT, die es in JKA gar nicht gibt. Dass die
        // Ansicht trotzdem stimmte, lag allein an einer zweiten Pruefung
        // ueber die Shadernamen - die Flaggen fanden nie etwas.
        {
            int sky = 0;
            int hidden = 0;
            bool skyFlagUsed = false;
            for (const bhed::BspSurface& s2 : g.surfaces) {
                const bhed::BspShader& sh =
                    g.shaders[static_cast<std::size_t>(s2.shader)];
                if (!sh.isDrawn()) { ++hidden; }
                if (sh.isSky()) {
                    ++sky;
                    if ((sh.surfaceFlags & 0x00002000U) != 0) { skyFlagUsed = true; }
                }
            }
            std::printf("     %d Himmelsflaechen, %d Hilfsflaechen\n", sky, hidden);
            // Nicht jede Karte hat Himmel. md_tfoaj_jedi.bsp spielt im
            // Jedi-Tempel, komplett innen - dort ist sky == 0 richtig und
            // nicht falsch. Frueher schlugen hier drei Proben an, obwohl
            // die Karte einwandfrei gelesen wurde.
            //
            // Geprueft wird deshalb der Zusammenhang: WENN Himmel da ist,
            // muss er ueber SURF_SKY erkannt worden sein und beim Weglassen
            // Dreiecke sparen. Ist keiner da, muss das Weglassen nichts
            // aendern - auch das ist eine Aussage.
            const bhed::BspMesh withSky = bhed::buildMesh(g, 4, false);
            const bhed::BspMesh noSky = bhed::buildMesh(g, 4, true);
            if (sky > 0) {
                expect("Himmel ueber die echte SURF_SKY-Flagge erkannt", skyFlagUsed);
                expect("ohne Himmel sind es weniger Dreiecke",
                       noSky.indexes.size() < withSky.indexes.size());
            } else {
                std::printf("     (Karte ohne Himmel - Innenraum)\n");
                expect("ohne Himmel in der Karte aendert das Weglassen nichts",
                       noSky.indexes.size() == withSky.indexes.size());
            }
        }

        // Eine feinere Unterteilung darf nur MEHR Dreiecke ergeben, nie
        // weniger - sonst stimmt die Zerlegung nicht.
        const bhed::BspMesh coarse = bhed::buildMesh(g, 1);
        expect("feiner unterteilt gibt mehr Dreiecke",
               m.indexes.size() >= coarse.indexes.size());

        std::printf("     %s: %zu Flaechen, %zu Lightmaps, %zu Dreiecke, "
                    "%zu Zeichenaufrufe\n", argv[a], g.surfaces.size(),
                    g.lightmaps.size(), m.indexes.size() / 3, m.batches.size());

        // --- Kaputte Eingaben ---------------------------------------------
        for (std::size_t cut : {std::size_t{0}, std::size_t{8}, std::size_t{200},
                                bytes.size() / 3, bytes.size() / 2}) {
            bhed::BspGeometry g2;
            if (bhed::readBspGeometry(bytes.substr(0, cut), g2, nullptr)) {
                // Wenn es durchgeht, muss das Ergebnis trotzdem heil sein.
                const bhed::BspMesh m2 = bhed::buildMesh(g2, 2);
                for (std::uint32_t i : m2.indexes) {
                    if (static_cast<std::size_t>(i) >= m2.verts.size()) {
                        expect("abgeschnittene Karte liefert kein kaputtes Netz", false);
                    }
                }
            }
        }
        // --- Bewegte Brush-Modelle -------------------------------------------
    //
    // Tueren, Plattformen, Raumschiffe: ihre Geometrie steht in der .bsp an
    // ihrem GEBAUTEN Platz. Das Skript aendert die Verschiebung dagegen.
    //
    // Der heikle Teil ist die Aufteilung. buildMesh(geo) enthaelt die
    // Untermodelle SCHON MIT - wer eines zusaetzlich bewegt zeichnet, hat
    // es doppelt. Deshalb ist die Welt Untermodell 0, und alles andere
    // kommt als bewegliches Teil dazu, so wie es die Engine trennt.
    //
    // Geprueft wird das GEOMETRISCH, nicht am Bild.
    //
    // Der erste Anlauf verglich zwei Bilder und schlug bei drei von fuenf
    // Karten an. Der Grund war nicht die Aufteilung, sondern der
    // Tiefenpuffer: deckungsgleiche Flaechen - ein Schild auf einer Wand -
    // liegen gleichauf, und welche oben landet, entscheidet die
    // Reihenfolge. Die aendert sich durchs Aufteilen. Bei duel_kamino waren
    // 5071 Bildpunkte anders und davon KEIN EINZIGER deutlich; das ist
    // Rauschen, keine fehlende Geometrie. Ein Bildvergleich kann das nicht
    // trennen, eine Abzaehlung schon.
    {
        std::vector<int> mal(g.surfaces.size(), 0);
        // "sub" statt "m": weiter oben steht schon ein m fuer das Netz.
        // MSVC meldet solche Verdeckungen als C4456, GCC mit -Wshadow -
        // eingeschaltet hatten wir es nicht, deshalb fiel es erst beim
        // Windows-Bau auf.
        for (const bhed::BspGeometry::SubModel& sub : g.models) {
            for (int k = 0; k < sub.numSurfaces; ++k) {
                const auto at = static_cast<std::size_t>(sub.firstSurface) +
                                static_cast<std::size_t>(k);
                if (at < mal.size()) { ++mal[at]; }
            }
        }
        std::size_t nie = 0;
        std::size_t mehrfach = 0;
        for (int n : mal) {
            if (n == 0) { ++nie; } else if (n > 1) { ++mehrfach; }
        }
        std::printf("     Aufteilung: %zu Flaechen ohne Modell, %zu mehrfach\n",
                    nie, mehrfach);
        expect("jede Flaeche gehoert GENAU EINEM Untermodell",
               nie == 0 && mehrfach == 0);

        // Und die Dreiecke muessen sich ebenso aufteilen: Welt plus Teile
        // ergibt die ganze Karte, kein Stueck fehlt und keines doppelt.
        // skipSky MUSS auf beiden Seiten gleich sein. buildMesh laesst den
        // Himmel per Vorgabe weg, buildModelMesh nicht - das allein machte
        // die erste Fassung dieser Probe falsch.
        const std::size_t ganz = bhed::buildMesh(g, 4, true).indexes.size();
        std::size_t summe = bhed::buildModelMesh(g, 0, 4, true).indexes.size();
        for (std::size_t k = 1; k < g.models.size(); ++k) {
            summe += bhed::buildModelMesh(g, static_cast<int>(k), 4, true)
                         .indexes.size();
        }
        expect("Welt plus Teile ergibt genau die Dreiecke der ganzen Karte",
               summe == ganz);

        // Bewegt muss sich am Bild etwas aendern - mit einer Kamera, die
        // das Teil auch sieht.
        // "teilNr" statt "idx": weiter oben steht schon ein idx.
        int teilNr = -1;
        for (std::size_t k = 1; k < g.models.size(); ++k) {
            if (!bhed::buildModelMesh(g, static_cast<int>(k), 4).indexes.empty()) {
                teilNr = static_cast<int>(k);
                break;
            }
        }
        if (teilNr < 0) {
            std::printf("     (keine beweglichen Teile in dieser Karte)\n");
        } else {
            const bhed::BspMesh welt = bhed::buildModelMesh(g, 0, 4);
            const bhed::BspMesh teil = bhed::buildModelMesh(g, teilNr, 4);
            const bhed::BspGeometry::SubModel& sm =
                g.models[static_cast<std::size_t>(teilNr)];
            float mitte[3];
            for (int k = 0; k < 3; ++k) {
                mitte[k] = (sm.mins[k] + sm.maxs[k]) * 0.5F;
            }
            // --- Der Abstand richtet sich nach der BREITE, nicht der Hoehe
            //
            // Vorher stand hier die groesste der drei Ausdehnungen. In
            // md_ga_jedi ist das erste bewegliche Teil eine Saeule von
            // 56 x 56 x 472 - die 472 schoben die Kamera 944 Einheiten weg,
            // und bei rund 20 Einheiten je Bildpunkt blieb davon ein
            // Streifen von knapp drei Bildpunkten uebrig, 40 von 6912.
            //
            // Eine Drehung aendert daran dann fast nichts, und die
            // Zusicherung "gedreht aendert das Bild" wurde zum Muenzwurf:
            // bei 15 und 270 Grad schlug sie an, bei 45, 90 und 180 nicht.
            // Der Zeichner war die ganze Zeit in Ordnung - die Probe war es
            // nicht.
            //
            // Jetzt zaehlt die waagerechte Ausdehnung: das Teil fuellt das
            // Bild, und ein paar Grad Drehung sind sichtbar.
            // --- Und die RICHTUNG nach der duennsten Achse ------------
            //
            // Mit sechzehn Karten aus Episode 3 geprueft: im Senat ist das
            // erste bewegliche Teil eine flache Platte, 280 x 280 x **4**.
            // Von der Seite betrachtet sind das vier Einheiten auf 560
            // Abstand - weniger als ein Bildpunkt. Die Probe sah nichts und
            // schlug fehl, obwohl der Zeichner richtig arbeitete.
            //
            // Dasselbe Muster wie beim Abstand darueber: die Probe stellte
            // eine Annahme ueber md_ga_jedi, nicht ueber den Zeichner.
            //
            // Jetzt wird aus der Richtung geschaut, in der das Teil am
            // DUENNSTEN ist - dann zeigt es dem Auge seine groesste
            // Flaeche, ob Saeule oder Platte.
            float ausdehnung[3];
            for (int k = 0; k < 3; ++k) {
                ausdehnung[k] = sm.maxs[k] - sm.mins[k];
            }
            int duennste = 0;
            for (int k = 1; k < 3; ++k) {
                if (ausdehnung[k] < ausdehnung[duennste]) {
                    duennste = k;
                }
            }
            // Der Abstand nach den BEIDEN anderen Achsen - die fuellen das
            // Bild.
            float breite = 8.0F;
            for (int k = 0; k < 3; ++k) {
                if (k != duennste) {
                    breite = std::max(breite, ausdehnung[k]);
                }
            }
            // --- Ueber die Stellungsmatrix des GPU-Wegs ------------------
            //
            // Bis rc568 zeichnete diese Probe das Teil mit dem Rasterer und
            // verglich Bilder. Der Rasterer ist entfernt; geprueft wird
            // jetzt dieselbe Rechnung, mit der die Grafikkarte das Teil
            // stellt (gpu::baueMoverWelt) - an einer Ecke des Netzes.
            (void)welt;
            expect("ein ruhendes Teil hat Ecken", !teil.verts.empty() && !teil.indexes.empty());
            auto stelle = [&](const bhed::MoverDraw& d, const float in[3], float out[3]) {
                float m[16];
                bhed::gpu::baueMoverWelt(d.pivot, d.offset, d.yaw, d.pitch, d.roll,
                                         d.pitch != 0.0F || d.roll != 0.0F, m);
                for (int r = 0; r < 3; ++r) {
                    out[r] = m[r * 4 + 0] * in[0] + m[r * 4 + 1] * in[1] +
                             m[r * 4 + 2] * in[2] + m[r * 4 + 3];
                }
            };
            if (!teil.verts.empty()) {
                const float* e = teil.verts.front().xyz;
                bool drin = true;
                for (int k = 0; k < 3; ++k) {
                    drin = drin && e[k] >= sm.mins[k] - 1.0F && e[k] <= sm.maxs[k] + 1.0F;
                }
                expect("die Ecke liegt im Huellkasten des Modells", drin);
                bhed::MoverDraw ruhe;
                ruhe.mesh = &teil;
                float r0[3];
                stelle(ruhe, e, r0);
                expect("ruhend bleibt die Ecke, wo sie ist",
                       std::fabs(r0[0] - e[0]) < 0.01F && std::fabs(r0[1] - e[1]) < 0.01F &&
                           std::fabs(r0[2] - e[2]) < 0.01F);
                bhed::MoverDraw weg = ruhe;
                weg.offset[0] = breite;
                weg.offset[2] = breite;
                float r1[3];
                stelle(weg, e, r1);
                expect("verschoben wandert die Ecke um den Versatz",
                       std::fabs(r1[0] - e[0] - breite) < 0.01F &&
                           std::fabs(r1[2] - e[2] - breite) < 0.01F);
                // Um einen Angelpunkt NEBEN dem Teil, wie eine Tuer: der
                // Abstand zum Angelpunkt bleibt, die Ecke wandert.
                bhed::MoverDraw gedreht = ruhe;
                gedreht.yaw = 30.0F;
                gedreht.pivot[0] = mitte[0] + breite * 2.0F;
                gedreht.pivot[1] = mitte[1];
                gedreht.pivot[2] = mitte[2];
                float r2[3];
                stelle(gedreht, e, r2);
                const auto abstand = [&](const float* q) {
                    const float dx = q[0] - gedreht.pivot[0];
                    const float dy = q[1] - gedreht.pivot[1];
                    return std::sqrt(dx * dx + dy * dy);
                };
                expect("um einen Angelpunkt daneben gedreht: Abstand bleibt, Ecke wandert",
                       std::fabs(abstand(r2) - abstand(e)) < 0.05F &&
                           std::fabs(r2[0] - e[0]) + std::fabs(r2[1] - e[1]) > 1.0F);
            }
        }
    }

    // --- Die Abschnitte decken jeden Zeichenaufruf lueckenlos ab --------
    //
    // Sie sind keine Auswahl, sondern eine Unterteilung: jeder Aufruf
    // zerfaellt in Abschnitte, einer je Flaeche der Karte. Nur so darf der
    // Zeichner einzelne davon weglassen, ohne dass Loecher entstehen.
    //
    // Diese Probe hat einen echten Fehler gefunden: die Schleife, die die
    // Abschnitte aufzeichnet, hat DREI Ausgaenge - ebene Flaechen,
    // Dreiecksnetze und Patches verlassen sie an verschiedenen Stellen mit
    // continue. Meine erste Fassung merkte sich den Abschnitt nur am
    // letzten davon und erfasste 146016 von 303744 Indizes. Am Bild sah man
    // davon zunaechst nur, dass "irgendwas fehlt".
    {
        const bhed::BspMesh mm = bhed::buildMesh(g, 4, true);
        long fehler = 0;
        long inAbschnitten = 0;
        long inAufrufen = 0;
        for (const bhed::BspMesh::Batch& bt : mm.batches) {
            std::uint32_t erwartet = bt.firstIndex;
            std::uint32_t summe = 0;
            for (std::uint32_t k = 0; k < bt.numRuns; ++k) {
                const std::size_t at =
                    static_cast<std::size_t>(bt.firstRun) + k;
                if (at >= mm.runs.size()) {
                    ++fehler;
                    break;
                }
                const bhed::BspMesh::Run& r = mm.runs[at];
                if (r.firstIndex != erwartet) { ++fehler; }
                erwartet += r.numIndexes;
                summe += r.numIndexes;
            }
            if (summe != bt.numIndexes) { ++fehler; }
            inAbschnitten += summe;
            inAufrufen += bt.numIndexes;
        }
        std::printf("     %zu Abschnitte, %ld von %ld Indizes erfasst\n",
                    mm.runs.size(), inAbschnitten, inAufrufen);
        expect("die Abschnitte decken jeden Aufruf lueckenlos ab", fehler == 0);
        expect("und erfassen alle Indizes", inAbschnitten == inAufrufen);

        // Jeder Abschnitt nennt eine Flaeche, die es gibt.
        bool gueltig = true;
        for (const bhed::BspMesh::Run& r : mm.runs) {
            if (static_cast<std::size_t>(r.surface) >= g.surfaces.size()) {
                gueltig = false;
            }
        }
        expect("jeder Abschnitt nennt eine vorhandene Flaeche", gueltig);
    }

    expect("abgeschnittene Karten ueberstanden", true);

        // --- Untermodelle (Lump 7) ------------------------------------
        //
        // Modell 0 ist die Welt, 1..n sind die beweglichen Teile - Tueren,
        // Plattformen, die Rampe des Falcon. Eine Entity zeigt mit
        // "model" "*12" darauf. Aufbau wie dmodel_t in
        // code/qcommon/qfiles.h, gelesen wie R_LoadSubmodels().
        {
            expect("Untermodell 0 ist die Welt",
                   !g.models.empty() && g.models[0].firstSurface == 0);
            bool inGrenzen = true;
            for (const bhed::BspGeometry::SubModel& sub : g.models) {
                if (sub.firstSurface < 0 || sub.numSurfaces < 0 ||
                    static_cast<std::size_t>(sub.firstSurface) + 
                        static_cast<std::size_t>(sub.numSurfaces) >
                        g.surfaces.size()) {
                    inGrenzen = false;
                }
            }
            expect("jedes Untermodell zeigt in die Flaechenliste", inGrenzen);
            // Modell 0 ist die Welt OHNE die beweglichen Teile.
            //
            // Das ist der Punkt, an dem ich mich geirrt hatte: ich hielt
            // buildMesh(geo) fuer dasselbe wie buildModelMesh(geo, 0). Ist
            // es nicht - buildMesh laeuft ueber ALLE Flaechen, also auch
            // ueber die der Untermodelle. Deshalb stehen Tueren und
            // Plattformen in der Kartenansicht schon jetzt da, nur eben
            // immer an ihrem gebauten Platz.
            const bhed::BspMesh ganz = bhed::buildMesh(g, 4);
            const bhed::BspMesh null = bhed::buildModelMesh(g, 0, 4);
            // KLEINER ODER GLEICH, nicht echt kleiner.
            //
            // Hat eine Karte gar keine beweglichen Teile, ist Untermodell 0
            // die ganze Welt - dann sind beide Zahlen gleich, und das ist
            // richtig so. Geprueft an duel_jt_outside aus Episode 3, wo
            // genau das der Fall ist.
            //
            // Vorher stand hier "echt kleiner", und md_ga_jedi hat zufaellig
            // bewegliche Teile. Wieder eine Annahme ueber EINE Karte.
            expect("Untermodell 0 ist hoechstens die ganze Welt",
                   null.indexes.size() <= ganz.indexes.size());
            // --- Und bei GLEICHEM Bereich genau gleich viele -------------
            //
            // Deckt Untermodell 0 die ganze Karte ab (keine beweglichen
            // Teile), muessen beide Wege dasselbe liefern. Alles andere
            // heisst: sie filtern verschieden.
            //
            // Genau das war der Fall. Gefunden mit duel_jt_outside:
            // 75050 gegen 75038 Dreiecke, weil `buildMesh` den Himmel
            // wegliess und `buildModelMesh` nicht - gegenlaeufige Vorgaben
            // fuer denselben Schalter.
            //
            // In sechzehn Runden gegen md_ga_jedi ist das nie aufgefallen,
            // weil dort die Bereiche verschieden sind (47 Untermodelle).
            // Erst eine Karte OHNE bewegliche Teile hat die beiden Wege
            // aufeinander gelegt.
            if (g.models.size() == 1U) {
                expect("bei gleichem Bereich auch gleich viele Dreiecke",
                       null.indexes.size() == ganz.indexes.size());
            }
            // Ein Weltmodell DARF leer sein.
            //
            // `md_amintro_sith` aus Episode 3 ist eine Kameraszene: die
            // ganze Kulisse steckt in beweglichen Teilen, der worldspawn
            // selbst hat nichts Zeichenbares. Das ist erlaubt und kommt
            // vor - gefordert wird nur, dass ueberhaupt Geometrie da ist.
            expect("die Karte hat ueberhaupt Geometrie", !ganz.indexes.empty());
            expect("ein Index ausserhalb ergibt nichts, statt zu stuerzen",
                   bhed::buildModelMesh(g, 9999, 4).indexes.empty() &&
                       bhed::buildModelMesh(g, -1, 4).indexes.empty());
        }
    }

    // --- Die drei Faelle der Sichtbarkeitstabelle -----------------------
    //
    // Anlass duel_deathstar. Nachgerechnet gegen die echte Karte:
    // leafAt trifft bei 19996 von 20000 Punkten den richtigen Blattkasten,
    // 76 Prozent des Kartenkastens sind Fels (Blatt ohne Cluster), und die
    // Bitrechnung stimmt mit R_ClusterPVS ueberein.
    //
    // Was die Anzeige seit rc198 unterscheidbar macht, sind die drei
    // Faelle - und die haelt diese Probe fest, weil sie im Bild GLEICH
    // aussehen:
    //
    //   ohne Tabelle        -> alles sichtbar
    //   Cluster ausserhalb  -> alles sichtbar (nicht: nichts!)
    //   gueltiger Cluster   -> die Tabelle entscheidet
    {
        bhed::BspGeometry g;
        g.numClusters = 3;
        g.clusterBytes = 1;
        // Cluster 0 sieht 0 und 1; Cluster 1 sieht 1; Cluster 2 sieht 2.
        g.vis = {0x03, 0x02, 0x04};

        expect("Cluster 0 sieht sich selbst", g.clusterVisible(0, 0));
        expect("Cluster 0 sieht 1", g.clusterVisible(0, 1));
        expect("Cluster 0 sieht 2 NICHT", !g.clusterVisible(0, 2));
        expect("jeder Cluster sieht sich selbst",
               g.clusterVisible(1, 1) && g.clusterVisible(2, 2));

        // Der wichtigste Fall: eine Nummer ausserhalb heisst SICHTBAR.
        // Andersherum verschwaende die halbe Karte, sobald die Kamera im
        // Fels steht - und genau so sieht ein Fehler aus, den man dann in
        // der Tabelle sucht statt bei der Kameraposition.
        expect("Cluster -1 (im Fels) zeichnet alles",
               g.clusterVisible(-1, 0) && g.clusterVisible(0, -1));
        expect("eine zu grosse Nummer ebenso", g.clusterVisible(99, 0));

        bhed::BspGeometry leer;
        expect("ohne Tabelle ist alles sichtbar", leer.clusterVisible(0, 5));
    }

    // --- Das Lichtgitter ---------------------------------------------------
    //
    // Gemeldet als Unterschied zwischen Spiel und Programm: die Figuren
    // standen flach und gleich hell da, wo das Spiel eine dunkle Hoehle mit
    // hellem Ausgang zeigt.
    //
    // Flaechen der Karte haben Lightmaps, Figuren nicht - die Engine holt
    // ihr Licht aus einem Gitter (Lump 15 die Werte, Lump 17 die Verweise).
    //
    // Die Probe baut sich ein Gitter mit BEKANNTEN Werten, damit sich die
    // Mittelung nachrechnen laesst: 2x2x2 Punkte, der erste schwarz, alle
    // anderen weiss.
    {
        bhed::BspGeometry g;
        g.lightGridSize[0] = g.lightGridSize[1] = g.lightGridSize[2] = 64.0F;
        g.lightGridOrigin[0] = g.lightGridOrigin[1] = g.lightGridOrigin[2] = 0.0F;
        g.lightGridBounds[0] = g.lightGridBounds[1] = g.lightGridBounds[2] = 2;
        g.lightGrid.resize(2);
        // Punkt 0: alles null. Punkt 1: Umgebungslicht 200.
        for (int st = 0; st < 4; ++st) {
            g.lightGrid[0].styles[st] = (st == 0) ? 0 : 0xFF;
            g.lightGrid[1].styles[st] = (st == 0) ? 0 : 0xFF;
        }
        for (int c = 0; c < 3; ++c) {
            g.lightGrid[1].ambient[0][c] = 200;
        }
        g.lightArray.assign(8, 1);
        g.lightArray[0] = 0;   // die Ecke bei (0,0,0) ist der dunkle Punkt

        // Genau AUF dem dunklen Punkt: sein Gewicht ist eins, die anderen
        // sieben null. Dazu der Mindestzuschlag von 32
        // (tr_light.cpp:424, "give everything a minimum light add").
        const float ecke[3] = {0.0F, 0.0F, 0.0F};
        const bhed::GridLight a = bhed::sampleLightGrid(g, ecke);
        expect("auf dem dunklen Punkt greift der Mindestzuschlag",
               a.ok && near(a.ambient[0], 32.0F, 0.01F));

        // In der Mitte: der dunkle Punkt traegt ein Achtel, die sieben
        // hellen sieben Achtel. 200 * 7/8 = 175, plus 32 = 207.
        const float mitte[3] = {32.0F, 32.0F, 32.0F};
        const bhed::GridLight m = bhed::sampleLightGrid(g, mitte);
        expect("in der Mitte wird ueber acht Punkte gemittelt",
               m.ok && near(m.ambient[0], 207.0F, 0.5F));

        // Ein Punkt IN EINER WAND (styles[0] == LS_NONE, 0xff) wird
        // uebersprungen - "ignore samples in walls", tr_light.cpp:229.
        // Dann traegt nur noch der helle Punkt, und es kommen volle 200+32
        // heraus statt 207.
        bhed::BspGeometry w = g;
        w.lightGrid[0].styles[0] = 0xFF;
        const bhed::GridLight n = bhed::sampleLightGrid(w, mitte);
        expect("ein Punkt in der Wand zaehlt nicht mit",
               n.ok && near(n.ambient[0], 232.0F, 0.5F));

        // Und ohne Gitter meldet es sich ehrlich als leer, statt alles
        // schwarz zu faerben. Alte Karten haben keins.
        bhed::BspGeometry leer;
        expect("ohne Gitter kein Licht, aber auch kein Schwarz",
               !bhed::sampleLightGrid(leer, mitte).ok);
    }

    std::printf("\n%s (%d Fehlschlaege)\n",
                fails != 0 ? "FEHLGESCHLAGEN" : "alle Geometrieproben bestanden", fails);
    return fails != 0 ? 1 : 0;
}
