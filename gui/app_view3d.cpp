// app_view3d.cpp - Karte, Modell und Zeitleiste
//
// Herausgeloest aus app.cpp, das 5138 Zeilen lang geworden war. Diese Datei
// haelt alles, was mit den beiden 3D-Ansichten und der Zeitleiste zu tun
// hat: Karte zeichnen, Kamera-Gizmos, Modelle samt Skelett, die Kamerabahn
// und ihre Bedienung.
//
// Die Trennung folgt der Zustaendigkeit, nicht der Zeilenzahl: was hier
// steht, braucht die Grafikschnittstelle und die Formatleser; was in
// app.cpp bleibt, ist Baum, Dialoge und Dateiverwaltung.
#include "app_internal.h"

#include "gpumap.h"

#include "bhed/gpucam.h"
#include "bhed/gpustate.h"
#include "bhed/gpuskin.h"
#include "bhed/num.h"

#include <chrono>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <sstream>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bhed::gui {

// KEINE anonyme Namensgruppe um das Ganze: die Zeichenfunktionen werden aus
// app.cpp aufgerufen und brauchen aeussere Bindung. Was nur hier gebraucht
// wird, steht weiter unten in einer eigenen anonymen Gruppe.

// --- Die MOVE-Schluessel des Skripts ------------------------------------
//
// EINE Liste fuer Anzeige, Anklicken und Zurueckschreiben. Vorher war
// App::selectedKey mal eine Nummer in dieser Liste (Zeichnen) und mal eine in
// App::keyMarks (Zurueckschreiben, Anklicken) - und keyMarks enthielt nur die
// Schluessel, die gerade IM BILD lagen. Lag ein frueherer hinter der Kamera,
// verrutschten die Nummern: "write to script" schrieb in die falsche Zeile
// oder gar nicht, und das Gizmo sass an einem anderen Schluessel als dem
// angeklickten. Beim Abspielen und beim Blick durch die Kamera war keyMarks
// ganz leer - dann ging Zurueckschreiben ueberhaupt nicht.
//
// Die Winkel: das erste PAN nach dem MOVE, sonst das zuletzt gesehene.
std::vector<KameraSchluessel> kameraSchluessel(const Script& script) {
    std::vector<KameraSchluessel> aus;
    KameraSchluessel current;
    bool hatPos = false;
    float letzteAng[3] = {0.0F, 0.0F, 0.0F};
    bool hatteAng = false;
    const auto abschliessen = [&]() {
        if (!hatPos) {
            return;
        }
        if (!current.hasAng && hatteAng) {
            for (int k = 0; k < 3; ++k) {
                current.ang[k] = letzteAng[k];
            }
            current.hasAng = true;
        }
        aus.push_back(current);
    };
    std::function<void(const std::vector<Node>&, Path&)> walk =
        [&](const std::vector<Node>& nodes, Path& prefix) {
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                const Node& n = nodes[i];
                prefix.push_back(i);
                if (n.name == "camera" && n.args.size() >= 2) {
                    const std::string& type = n.args[0].text;
                    float v[3] = {0, 0, 0};
                    std::istringstream is(n.args[1].text);
                    is >> v[0] >> v[1] >> v[2];
                    if (type == "MOVE") {
                        abschliessen();
                        current = KameraSchluessel{};
                        for (int k = 0; k < 3; ++k) { current.pos[k] = v[k]; }
                        hatPos = true;
                        current.path = prefix;
                    } else if (type == "PAN") {
                        if (hatPos && !current.hasAng) {
                            for (int k = 0; k < 3; ++k) {
                                current.ang[k] = v[k];
                            }
                            current.hasAng = true;
                        }
                        for (int k = 0; k < 3; ++k) {
                            letzteAng[k] = v[k];
                        }
                        hatteAng = true;
                    }
                }
                walk(n.children, prefix);
                prefix.pop_back();
            }
        };
    Path prefix;
    walk(script.nodes, prefix);
    abschliessen();
    return aus;
}

// Der Schluessel, der zur Zeit `ms` gilt: der letzte MOVE, der bis dahin
// begonnen hat - dorthin faehrt die Kamera gerade, oder dort steht sie.
// Vor dem ersten MOVE der erste. -1, wenn es keinen gibt.
int schluesselFuerZeit(double ms) {
    const std::vector<KameraSchluessel> ks = kameraSchluessel(g_app->doc.script());
    if (ks.empty()) {
        return -1;
    }
    const CamSegment* bester = nullptr;
    for (const CamSegment& s : g_app->camTrack.segments) {
        if (s.kind != CamSegment::Kind::Move || s.startMs > ms + 0.5) {
            continue;
        }
        if (bester == nullptr || s.startMs >= bester->startMs) {
            bester = &s;
        }
    }
    if (bester == nullptr) {
        return 0;
    }
    for (std::size_t i = 0; i < ks.size(); ++i) {
        if (ks[i].path == bester->path) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

// Die Kameras des Skripts als Gizmo einzeichnen.
//
// Aufgebaut wie eine Zielkamera in 3ds Max: ein Koerper mit Objektiv, davor
// ein Ziel, dazwischen eine Linie, und der Sichtkegel als Pyramide. Ohne das
// sieht man zwar die Karte, aber nicht, was das Skript daraus macht.
//
// Warum ein Ziel und nicht nur eine Richtung: in ICARUS bewegt
// camera ( MOVE, ... ) den Ort und camera ( PAN, ... ) den Blick - zwei
// getrennte Befehle mit eigenen Dauern. Genau das ist eine Zielkamera:
// Koerper und Ziel lassen sich unabhaengig bewegen.
//
// Gezeichnet wird direkt ins fertige Bild, mit derselben Projektion wie die
// Karte. Ein eigener Zeichenweg waere doppelte Arbeit fuer ein paar Linien.
void drawCameraGizmos(MapImage& img) {
    // --- Auch den PUFFER pruefen, nicht nur die Groesse ------------------
    //
    // Hier stand nur `width <= 0 || height <= 0`. Auf dem GPU-Weg fuellt
    // niemand `mapImage.rgba` - renderMap wird uebersprungen -, und dann
    // sind die Masse gesetzt, aber der Puffer ist leer. Diese Funktion
    // schreibt trotzdem hinein.
    //
    // Gemeldet: Absturz NUR beim Bewegen der Kamera oder beim Vollbild.
    // Genau dann werden die Gizmos gezeichnet beziehungsweise die Groesse
    // geaendert. Die Adresse 0x47B810 blieb ueber Programmstarts gleich -
    // ein fester Versatz auf einen Nullzeiger.
    //
    // Dieselbe Luecke wie bei createTexture in rc412: die Masse sagen
    // nichts darueber, ob Speicher dahintersteht.
    const std::size_t noetig = static_cast<std::size_t>(img.width) *
                               static_cast<std::size_t>(img.height) * 4U;
    if (img.width <= 0 || img.height <= 0 || img.rgba.size() < noetig) {
        return;
    }

    float fwd[3];
    float rgt[3];
    float upv[3];
    g_app->cam.forward(fwd);
    g_app->cam.right(rgt);
    g_app->cam.up(upv);
    const float focal = 1.0F / std::tan(g_app->cam.fovY * 3.14159265F / 360.0F);
    const float halfW = static_cast<float>(img.width) * 0.5F;
    const float halfH = static_cast<float>(img.height) * 0.5F;

    struct P2 {
        float x = 0;
        float y = 0;
        // Der Abstand zur Kamera. Gebraucht, damit eine Linie hinter einer
        // Figur auch dahinter bleibt - siehe plot().
        float z = 0;
        bool ok = false;
    };
    auto project = [&](const float p[3]) {
        P2 out;
        const float d[3] = {p[0] - g_app->cam.pos[0], p[1] - g_app->cam.pos[1],
                            p[2] - g_app->cam.pos[2]};
        const float z = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2];
        if (z < 1.0F) {
            return out;
        }
        out.z = z;
        const float x = d[0] * rgt[0] + d[1] * rgt[1] + d[2] * rgt[2];
        const float y = d[0] * upv[0] + d[1] * upv[1] + d[2] * upv[2];
        out.x = halfW + x * focal / z * halfH;
        out.y = halfH - y * focal / z * halfH;
        out.ok = true;
        return out;
    };

    // --- Ein Punkt, aber nur wenn nichts davor steht ----------------------
    //
    // Gefragt: "warum werden die Kamera und Entities immer davor gerendert
    // statt dahinter?"
    //
    // Weil hier bisher direkt ins fertige Bild geschrieben wurde, ohne den
    // Tiefenpuffer je anzusehen. Karte und Figuren fuellen ihn beim
    // Zeichnen; die Gizmos kamen danach und uebermalten alles.
    //
    // Sichtbar war das als Kamerakaesten, die durch Waende scheinen, und
    // als Entitykreuze auf Windus Robe.
    //
    // `z` ist der Abstand zur Kamera. Steht dort schon etwas NAEHERES,
    // wird nicht gezeichnet - genau die Regel, nach der auch Karte und
    // Figuren gegeneinander pruefen.
    //
    // GESCHRIEBEN wird die Tiefe nicht: eine Linie ist ein Strich, kein
    // Koerper. Wer sie einträgt, laesst die naechste Linie an ihr
    // abprallen, und zwei sich kreuzende Gizmos loeschen einander Loecher.
    auto plot = [&](int x, int y, float z, std::uint8_t r, std::uint8_t g,
                    std::uint8_t b) {
        if (x < 0 || y < 0 || x >= img.width || y >= img.height) {
            return;
        }
        const std::size_t at = (static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(img.width) +
                                static_cast<std::size_t>(x));
        if (at < img.depth.size() && z >= img.depth[at]) {
            return;   // etwas Naeheres steht davor
        }
        img.rgba[at * 4U + 0] = r;
        img.rgba[at * 4U + 1] = g;
        img.rgba[at * 4U + 2] = b;
        img.rgba[at * 4U + 3] = 255;   // fuer die durchsichtige GPU-Ueberlagerung
    };

    // Die Linie ZUERST auf das Bild beschneiden.
    //
    // Vorher lief die Schleife ueber die ganze Laenge und liess plot() die
    // Punkte ausserhalb wegwerfen. Das ist genau die falsche Reihenfolge:
    // faehrt man auf eine Entity oder eine Kamera zu, werden ihre Linien
    // auf dem Bildschirm riesig, und dann werden bis zu 6000 Punkte je
    // Linie gerechnet, von denen die meisten gar nicht im Bild liegen.
    // Bei rund hundert Entities mit je drei Linien und den Kamerarahmen
    // dazu ist das die Hauptarbeit - und sie faellt genau dann an, wenn
    // man nah herankommt. Deshalb brach die Bildrate beim Zufahren ein.
    //
    // Cohen-Sutherland, das aelteste Verfahren dafuer: jedem Endpunkt
    // einen Vier-Bit-Code geben, der sagt, auf welcher Seite des Fensters
    // er liegt.
    //
    //   beide Codes 0            ganz drin, nichts zu tun
    //   Codes haben ein Bit gemeinsam   beide auf DERSELBEN Seite
    //                            draussen - die Linie kann das Fenster
    //                            nicht schneiden, ganz verwerfen
    //   sonst                    den aeusseren Punkt auf den Rand ziehen
    //                            und von vorn
    //
    // Der billige Teil ist der zweite Fall: er wirft die meisten Linien
    // ohne jede Rechnung weg.
    constexpr int kInside = 0;
    constexpr int kLeft = 1;
    constexpr int kRight = 2;
    constexpr int kBottom = 4;
    constexpr int kTop = 8;
    const auto outcode = [&img](float x, float y) {
        int c = kInside;
        if (x < 0.0F) { c |= kLeft; }
        else if (x > static_cast<float>(img.width - 1)) { c |= kRight; }
        if (y < 0.0F) { c |= kBottom; }
        else if (y > static_cast<float>(img.height - 1)) { c |= kTop; }
        return c;
    };

    auto line = [&](const P2& a0, const P2& b0, std::uint8_t r, std::uint8_t g,
                    std::uint8_t bl, bool thick) {
        if (!a0.ok || !b0.ok) {
            return;
        }
        float x0 = a0.x;
        float y0 = a0.y;
        float x1 = b0.x;
        float y1 = b0.y;
        const auto xmax = static_cast<float>(img.width - 1);
        const auto ymax = static_cast<float>(img.height - 1);
        int c0 = outcode(x0, y0);
        int c1 = outcode(x1, y1);
        bool sichtbar = false;
        for (int guard = 0; guard < 8; ++guard) {
            if ((c0 | c1) == 0) {
                sichtbar = true;
                break;
            }
            if ((c0 & c1) != 0) {
                break;   // beide auf derselben Seite draussen
            }
            const int aus = (c0 != 0) ? c0 : c1;
            float x = 0.0F;
            float y = 0.0F;
            if ((aus & kTop) != 0) {
                x = x0 + (x1 - x0) * (ymax - y0) / (y1 - y0);
                y = ymax;
            } else if ((aus & kBottom) != 0) {
                x = x0 + (x1 - x0) * (0.0F - y0) / (y1 - y0);
                y = 0.0F;
            } else if ((aus & kRight) != 0) {
                y = y0 + (y1 - y0) * (xmax - x0) / (x1 - x0);
                x = xmax;
            } else {
                y = y0 + (y1 - y0) * (0.0F - x0) / (x1 - x0);
                x = 0.0F;
            }
            if (!std::isfinite(x) || !std::isfinite(y)) {
                return;
            }
            if (aus == c0) {
                x0 = x;
                y0 = y;
                c0 = outcode(x0, y0);
            } else {
                x1 = x;
                y1 = y;
                c1 = outcode(x1, y1);
            }
        }
        if (!sichtbar) {
            return;
        }
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const int steps = static_cast<int>(std::max(std::fabs(dx), std::fabs(dy)));
        if (steps <= 0) {
            plot(static_cast<int>(x0), static_cast<int>(y0), a0.z, r, g, bl);
            return;
        }
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const int px = static_cast<int>(x0 + dx * t);
            const int py = static_cast<int>(y0 + dy * t);
            // Die Tiefe laeuft mit. Ohne sie waere eine Linie entweder ganz
            // sichtbar oder ganz verdeckt - eine, die halb hinter einer
            // Saeule verschwindet, gibt es dann nicht.
            //
            // Linear zwischen den Endpunkten, nicht perspektivisch korrekt:
            // fuer eine Linie, die auf ein paar Bildpunkte genau stimmen
            // soll, waere das Aufwand ohne sichtbaren Gewinn.
            const float z = a0.z + (b0.z - a0.z) * t;
            plot(px, py, z, r, g, bl);
            if (thick) {
                plot(px + 1, py, z, r, g, bl);
                plot(px, py + 1, z, r, g, bl);
            }
        }
    };

    // Alle camera-Befehle einsammeln. MOVE gibt den Ort, das folgende PAN
    // die Blickrichtung.
    // Die MOVE-Schluessel des Skripts - dieselbe Liste und Reihenfolge, die
    // auch das Zurueckschreiben benutzt (kameraSchluessel).
    struct Shot {
        float pos[3]{};
        float ang[3]{};
        bool hasPos = false;
        bool hasAng = false;
        Path path;
    };
    std::vector<Shot> shots;
    for (const KameraSchluessel& ks : kameraSchluessel(g_app->doc.script())) {
        Shot sh;
        for (int k = 0; k < 3; ++k) {
            sh.pos[k] = ks.pos[k];
            sh.ang[k] = ks.ang[k];
        }
        sh.hasPos = true;
        sh.hasAng = ks.hasAng;
        sh.path = ks.path;
        shots.push_back(sh);
    }

    // Die Groesse haengt vom Abstand ab, sonst ist das Gizmo aus der Ferne
    // ein Punkt und aus der Naehe bildfuellend.
    auto sizeAt = [&](const float p[3]) {
        float d = 0.0F;
        for (int k = 0; k < 3; ++k) {
            const float t = p[k] - g_app->cam.pos[k];
            d += t * t;
        }
        return std::clamp(std::sqrt(d) * 0.035F, 6.0F, 220.0F);
    };

    // --- Das Kameramodell an einem Ort mit einer Blickrichtung ---------
    //
    // Herausgeloest, weil es an ZWEI Stellen gebraucht wird: fuer die
    // laufende Kamera, und fuer die Vorschau am Gizmo. Vorher stand es nur
    // an der ersten - deshalb blieb die Kamera beim Ziehen und Drehen
    // stehen ("wenn ich sie bewege und rotiere bewegt und rotiert sich ja
    // die kamera nicht wirklich"). Das Gizmo wanderte, das Modell nicht.
    //
    // mitAngaben: Zielkreuz und Sichtkegel dazu. Die laufende Kamera zeigt
    // sie nur, wenn sie ueberhaupt eine Blickrichtung hat.
    auto zeichneKamera = [&](const float pos[3], const float ang[3],
                             std::uint8_t r, std::uint8_t g, std::uint8_t b,
                             bool betont, bool mitAngaben) {
        Camera shot;
        for (int i = 0; i < 3; ++i) {
            shot.pos[i] = pos[i];
            shot.angles[i] = ang[i];
        }
        float f[3];
        float rr[3];
        float uu[3];
        shot.forward(f);
        shot.right(rr);
        shot.up(uu);

        const float S = sizeAt(pos);
        auto at = [&](float df, float dr, float du, float out[3]) {
            for (int i = 0; i < 3; ++i) {
                out[i] = pos[i] + f[i] * df + rr[i] * dr + uu[i] * du;
            }
        };

        // --- Koerper: ein Quader hinter dem Objektiv --------------------
        const float bw = S * 0.45F;
        const float bh = S * 0.38F;
        const float bl = S * 0.75F;
        P2 c[8];
        int idx = 0;
        for (int zi = 0; zi < 2; ++zi) {
            for (int yi = 0; yi < 2; ++yi) {
                for (int xi = 0; xi < 2; ++xi) {
                    float pp[3];
                    at(zi == 0 ? 0.0F : -bl, (xi == 0 ? -bw : bw),
                       (yi == 0 ? -bh : bh), pp);
                    c[idx++] = project(pp);
                }
            }
        }
        const int edges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0},
                                  {4, 5}, {5, 7}, {7, 6}, {6, 4},
                                  {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& e : edges) {
            line(c[e[0]], c[e[1]], r, g, b, betont);
        }

        // --- Objektiv: ein kurzer Kegel nach vorn ------------------------
        float tipP[3];
        at(S * 0.5F, 0.0F, 0.0F, tipP);
        const P2 tip = project(tipP);
        for (int e = 0; e < 4; ++e) {
            line(c[e], tip, r, g, b, false);
        }
        if (!mitAngaben) {
            return;
        }

        // --- Ziel und Verbindungslinie ----------------------------------
        const float kTargetDist = S * 9.0F;
        const float kConeDist = S * 5.0F;
        float tg[3];
        for (int i = 0; i < 3; ++i) {
            tg[i] = pos[i] + f[i] * kTargetDist;
        }
        const P2 target = project(tg);
        const P2 centre = project(pos);
        line(centre, target, r, g, b, false);

        const float ts = sizeAt(tg) * 0.35F;
        float t1[3];
        float t2[3];
        float t3[3];
        float t4[3];
        for (int i = 0; i < 3; ++i) {
            t1[i] = tg[i] - rr[i] * ts;
            t2[i] = tg[i] + rr[i] * ts;
            t3[i] = tg[i] - uu[i] * ts;
            t4[i] = tg[i] + uu[i] * ts;
        }
        line(project(t1), project(t2), r, g, b, betont);
        line(project(t3), project(t4), r, g, b, betont);

        // --- Sichtkegel als Pyramide ------------------------------------
        constexpr float kSpread = 0.36F;   // entspricht etwa 80 Grad
        P2 corner[4];
        for (int q = 0; q < 4; ++q) {
            const float sxp = (q == 0 || q == 3) ? -1.0F : 1.0F;
            const float syp = (q < 2) ? 1.0F : -1.0F;
            float pp[3];
            for (int i = 0; i < 3; ++i) {
                pp[i] = pos[i] + f[i] * kConeDist +
                        rr[i] * (sxp * kConeDist * kSpread) +
                        uu[i] * (syp * kConeDist * kSpread * 0.75F);
            }
            corner[q] = project(pp);
            line(centre, corner[q], r, g, b, false);
        }
        for (int q = 0; q < 4; ++q) {
            line(corner[q], corner[(q + 1) % 4], r, g, b, false);
        }
    };

    // --- Entities der Karte -------------------------------------------
    //
    // Nicht alle, sondern die, deren Namen im Skript auftauchen:
    //   waypoint_navgoal   -> SET_NAVGOAL
    //   target_position    -> SET_LOOK_TARGET
    //   info_null          -> Blickziele und Lichtrichtungen
    //   target_scriptrunner-> startet ein Skript, per usescript
    // In md_twj_jedi.ent sind das 26 Wegpunkte, drei Blickziele und sechs
    // Skriptstarter - genau die Werte, die man beim Schreiben braucht.
    // Die Marken werden beim Zeichnen gemerkt, damit der Klick sie
    // wiederfindet. Neu aufbauen, sobald gezeichnet wird - und AUSSERHALB
    // des Schalters leeren: mit "Entities" aus blieben sonst die alten
    // Stellen stehen, und ein Klick ins scheinbar Leere waehlte eine
    // unsichtbare Entity (Kartentest 27.09.).
    g_app->markers.clear();
    if (g_app->showEntities) {
        for (std::size_t ei = 0; ei < g_app->map.entities.size(); ++ei) {
            const MapEntity& e = g_app->map.entities[ei];
            if (e.origin.empty()) {
                continue;
            }
            // Dieselben Gruppen und Farben wie in der Entity-Liste links.
            const int kat = entityKategorie(e);
            if (kat < 0) {
                continue;
            }
            const EntityKat& ek = entityKategorieInfo(kat);
            const std::uint8_t er = ek.r;
            const std::uint8_t eg = ek.g;
            const std::uint8_t eb = ek.b;

            // Ausgeblendete werden nicht gezeichnet - und damit auch nicht
            // angeklickt, denn die Marken entstehen hier.
            if (!g_app->entityVisible(ei, e.classname)) {
                continue;
            }

            float p[3] = {0, 0, 0};
            std::istringstream is(e.origin);
            is >> p[0] >> p[1] >> p[2];
            const P2 c0 = project(p);
            if (!c0.ok) {
                continue;
            }
            g_app->markers.push_back(App::PickedMarker{c0.x, c0.y, ei});
            const float es = sizeAt(p) * 0.30F;
            float a[3];
            float bq[3];
            for (int i = 0; i < 3; ++i) {
                a[i] = p[i];
                bq[i] = p[i];
            }
            a[2] -= es;
            bq[2] += es;
            line(project(a), project(bq), er, eg, eb, false);
            // Ein liegendes Kreuz dazu, damit es von oben sichtbar bleibt.
            float l1[3] = {p[0] - es, p[1], p[2]};
            float l2[3] = {p[0] + es, p[1], p[2]};
            float l3[3] = {p[0], p[1] - es, p[2]};
            float l4[3] = {p[0], p[1] + es, p[2]};
            line(project(l1), project(l2), er, eg, eb, false);
            line(project(l3), project(l4), er, eg, eb, false);
        }
    }

    // --- Wo die Figuren gerade stehen (fuer ihre Namen) ------------------
    g_app->figurNamen.clear();
    if (g_app->zeigeNamen && g_app->showActors) {
        for (const Actor& a : g_app->scene.actors) {
            if (!a.haveStart || a.brushModel > 0) {
                continue;
            }
            const ActorState st = a.at(g_app->playMs);
            if (!st.visible) {
                continue;
            }
            float kopf[3] = {st.pos[0], st.pos[1], st.pos[2] + 48.0F};
            const P2 c0 = project(kopf);
            if (c0.ok) {
                g_app->figurNamen.push_back(App::NamensMarke{c0.x, c0.y, a.name});
            }
        }
    }

    // --- Die BAHN der Kamera ---------------------------------------------
    //
    // Die magenta Marken sind Stuetzstellen: dort steht im Skript ein MOVE.
    // Was DAZWISCHEN passiert, sagt keine von ihnen - die Kamera faehrt
    // eine Strecke ab, und wie sie das tut, entscheidet die Dauer im
    // Befehl, nicht der Abstand der Punkte.
    //
    // Deshalb eine duenne Linie entlang der ganzen Fahrt, in kleinen
    // Schritten abgetastet. Dieselbe Rechnung wie bei "Durch die Kamera"
    // und bei der gruenen Marke - drei Darstellungen, eine Quelle.
    //
    // Zweihundert Schritte: genug, dass eine Kurve rund aussieht, und
    // wenig genug, dass es bei einer Minute Sequenz nicht ins Gewicht
    // faellt. Die Linien werden ohnehin am Bildrand beschnitten (rc137).
    // --- Wo stehen die Figuren auf dem Bildschirm? ------------------------
    //
    // Hier, weil hier project() zu Hause ist. Der Klick liest die Liste
    // spaeter ab - dieselbe Arbeitsteilung wie bei der Kamera (cameraSx)
    // und den Ringen des Gizmos.
    //
    // Gezielt wird auf Brusthoehe, nicht auf die Fuesse: dorthin klickt
    // man, wenn man eine Figur meint.
    g_app->actorMarks.clear();
    for (std::size_t ai = 0; ai < g_app->scene.actors.size(); ++ai) {
        const Actor& fa = g_app->scene.actors[ai];
        if (!fa.haveStart) {
            continue;
        }
        const ActorState fs = fa.at(g_app->playMs);
        if (!fs.visible) {
            continue;
        }
        const float brust[3] = {fs.pos[0], fs.pos[1], fs.pos[2] + 40.0F};
        const P2 mk = project(brust);
        if (mk.ok) {
            g_app->actorMarks.push_back(
                App::ActorMark{static_cast<int>(ai), mk.x, mk.y});
        }
    }

    // --- Der Laufweg der angewaehlten Figur -------------------------------
    //
    // Dieselbe Rechnung wie bei der Kamerabahn, nur mit Actor::at() statt
    // CameraTrack::at(): in kleinen Schritten abtasten und verbinden. Damit
    // sieht man, WO eine Figur entlanggeht - und weil die Quelle dieselbe
    // ist wie beim Abspielen, stimmt die Linie mit der Bewegung ueberein.
    //
    // Nur bei ausgewaehlter Figur, genau wie in 3ds Max: dort erscheint
    // die Trajektorie erst, wenn das Objekt gewaehlt ist.
    if (g_app->selectedActor >= 0 &&
        static_cast<std::size_t>(g_app->selectedActor) <
            g_app->scene.actors.size()) {
        const Actor& fig =
            g_app->scene.actors[static_cast<std::size_t>(g_app->selectedActor)];
        const double dauer = std::max(g_app->scene.durationMs, 1.0);
        constexpr int kFigSchritte = 200;
        P2 vorFig;
        bool hatVorFig = false;
        for (int i = 0; i <= kFigSchritte; ++i) {
            const double ms =
                dauer * static_cast<double>(i) / static_cast<double>(kFigSchritte);
            const ActorState fst = fig.at(ms);
            if (!fst.visible) {
                hatVorFig = false;   // Luecke: die Figur ist gerade weg
                continue;
            }
            const P2 jetzt = project(fst.pos);
            if (hatVorFig) {
                // Gelb - abgesetzt von der gruenen Kamerabahn, damit man
                // beide gleichzeitig lesen kann.
                line(vorFig, jetzt, 220, 190, 60, false);
            }
            vorFig = jetzt;
            hatVorFig = true;
        }
        // Und die STUETZSTELLEN: dort steht ein Befehl im Skript. In Max
        // sind das die weissen Quadrate auf der Bahn.
        for (const ActorStep& sp : fig.steps) {
            const ActorState fst = fig.at(sp.startMs);
            if (!fst.visible) {
                continue;
            }
            const P2 mp = project(fst.pos);
            if (!mp.ok) {
                continue;
            }
            const int mx = static_cast<int>(mp.x);
            const int my = static_cast<int>(mp.y);
            for (int d = -3; d <= 3; ++d) {
                plot(mx + d, my - 3, mp.z, 255, 230, 120);
                plot(mx + d, my + 3, mp.z, 255, 230, 120);
                plot(mx - 3, my + d, mp.z, 255, 230, 120);
                plot(mx + 3, my + d, mp.z, 255, 230, 120);
            }
        }
    }

    if (g_app->cameraSelected && g_app->camTrack.durationMs > 1.0) {
        constexpr int kSchritte = 200;
        P2 vorherP;
        bool hatVorher = false;
        for (int i = 0; i <= kSchritte; ++i) {
            const double ms = g_app->camTrack.durationMs *
                              static_cast<double>(i) /
                              static_cast<double>(kSchritte);
            const CameraState st = camStateAt(ms);
            if (!st.enabled) {
                hatVorher = false;   // Luecke: die Kamera ist gerade aus
                continue;
            }
            const P2 jetzt = project(st.pos);
            if (hatVorher) {
                // Gedaempftes Gruen - die Bahn soll die Marken nicht
                // uebertoenen, sondern sie verbinden.
                line(vorherP, jetzt, 40, 130, 70, false);
            }
            vorherP = jetzt;
            hatVorher = true;

            // Zwischenpunkte: kleine Punkte in gleichen ZEITabstaenden.
            //
            // Ihr Abstand auf der Linie zeigt die Geschwindigkeit - eng
            // beieinander heisst langsam, weit auseinander heisst schnell.
            // Genau dafuer hat Max sie ("the white dots are in-betweens",
            // "ticks give you an idea of the animation timing").
            if (jetzt.ok && (i % 5) == 0) {
                plot(static_cast<int>(jetzt.x), static_cast<int>(jetzt.y),
                     jetzt.z, 170, 255, 190);
            }
        }

        // Die Schluesselstellen: kleine Quadrate, dort steht ein MOVE.
        //
        // In Max sind es weisse Quadrate auf der Bahn. Sie sagen, WO im
        // Skript etwas steht - die Punkte dazwischen sagen, WIE schnell es
        // dorthin geht.
        // Liegt gerade eine Verschiebung als Vorschau an?
        const bool verschobenNun =
            g_app->gizmoOffset[0] != 0.0F || g_app->gizmoOffset[1] != 0.0F ||
            g_app->gizmoOffset[2] != 0.0F;
        g_app->keyMarks.clear();
        for (std::size_t si2 = 0; si2 < shots.size(); ++si2) {
            const Shot& sh2 = shots[si2];
            const P2 kp = project(sh2.pos);
            // Fuer den Klick merken - samt Skriptzeile. JEDER Schluessel
            // bekommt seinen Platz, auch einer ausserhalb des Bildes: die
            // Nummer in keyMarks muss die in kameraSchluessel sein.
            App::KeyMark mk;
            mk.sx = kp.ok ? kp.x : -1.0e9F;
            mk.sy = kp.ok ? kp.y : -1.0e9F;
            mk.imBild = kp.ok;
            mk.path = sh2.path;
            g_app->keyMarks.push_back(mk);
            if (!kp.ok) {
                continue;
            }

            const bool ausgewaehlt =
                (static_cast<int>(si2) == g_app->selectedKey);
            const int kx = static_cast<int>(kp.x);
            const int ky = static_cast<int>(kp.y);
            // Der ausgewaehlte Schluessel groesser und gelb - wie in Max
            // die hervorgehobene Stuetzstelle.
            const int rad = ausgewaehlt ? 5 : 3;
            const std::uint8_t kr = 255;
            const std::uint8_t kg = ausgewaehlt ? 210 : 255;
            const std::uint8_t kb = ausgewaehlt ? 60 : 255;
            for (int d = -rad; d <= rad; ++d) {
                plot(kx + d, ky - rad, kp.z, kr, kg, kb);
                plot(kx + d, ky + rad, kp.z, kr, kg, kb);
                plot(kx - rad, ky + d, kp.z, kr, kg, kb);
                plot(kx + rad, ky + d, kp.z, kr, kg, kb);
            }

            // --- Das Gizmo am ausgewaehlten Schluessel -------------------
            //
            // Drei Achsen, wie in 3ds Max: X rot, Y gruen, Z blau. Die
            // Laenge richtet sich nach der Entfernung, damit es aus jedem
            // Abstand gleich gross aussieht - dieselbe Regel wie bei den
            // Kamerarahmen (sizeAt).
            //
            // Die Bildschirmenden werden gemerkt: der Griff braucht sie zum
            // Treffen, und das Ziehen braucht sie, um einen Mausweg in
            // Welteinheiten umzurechnen.
            // Die Kamera geht MIT.
            //
            // Der Einwand war: "dann bewegt sich die kamera nicht wirklich
            // mit". Richtig - in rc153 wanderte nur das Gizmo.
            //
            // Vorwegnehmen laesst sich das genau, weil ein camera(MOVE) in
            // JKA eine GERADE ist (CGCam_MoveUpdate rechnet linear, siehe
            // rc143). Die beiden Abschnitte links und rechts des
            // verschobenen Schluessels sind also einfach zwei Strecken zum
            // neuen Ort - kein Naeherungsverfahren noetig.
            //
            // Gezeichnet in hellem Gruen, damit man die Vorschau von der
            // bestehenden Bahn unterscheiden kann.
            if (ausgewaehlt && verschobenNun) {
                float neu[3];
                for (int k = 0; k < 3; ++k) {
                    neu[k] = sh2.pos[k] + g_app->gizmoOffset[k];
                }
                const P2 np = project(neu);
                // Der Weg vom vorigen Schluessel hierher ...
                for (std::size_t vor = si2; vor > 0; --vor) {
                    if (!shots[vor - 1].hasPos) { continue; }
                    line(project(shots[vor - 1].pos), np, 150, 255, 180, false);
                    break;
                }
                // ... und von hier zum naechsten.
                for (std::size_t nach = si2 + 1; nach < shots.size(); ++nach) {
                    if (!shots[nach].hasPos) { continue; }
                    line(np, project(shots[nach].pos), 150, 255, 180, false);
                    break;
                }
                // Und die KAMERA am neuen Ort - nicht nur ein Kaestchen.
                //
                // Ein Quadrat sagt, wo sie steht; das Modell sagt auch,
                // wohin sie blickt. Genau das fehlte: das Gizmo wanderte,
                // die Kamera nicht.
                float vang[3] = {0.0F, 0.0F, 0.0F};
                if (sh2.hasAng) {
                    for (int k = 0; k < 3; ++k) {
                        vang[k] = sh2.ang[k] + g_app->gizmoAngles[k];
                    }
                }
                // Betont (der vierte Wert), damit die Vorschau vor der weissen
                // Kamera aus dem Skript steht - sie ist das, was man gerade
                // einstellt.
                zeichneKamera(neu, vang, 120, 255, 140, true, sh2.hasAng);
            }

            // --- Drehen: ein Ring je Achse ------------------------------
            //
            // 3ds Max zeichnet drei Kreise um den Ursprung, einen je Achse.
            // Hier dasselbe, nur ohne den freien Trackball - in ICARUS hat
            // eine Kamera drei Winkel (PAN), und die dreht man einzeln.
            //
            // Gezogen wird waagerecht: nach rechts dreht im Uhrzeigersinn.
            // Das ist die Vereinbarung aus jedem Werkzeug, das einen
            // Drehgriff hat, und sie ist unabhaengig vom Blickwinkel - bei
            // einem Ring, der fast von der Seite zu sehen ist, koennte man
            // sonst nicht zielen.
            // Die drei Achsenrichtungen des gewaehlten Bezugssystems.
            //
            // Welt: X, Y, Z der Karte. Lokal: rechts, oben, vorwaerts der
            // Kamera - "as we rotate the object, the rotate gizmo is stuck
            // to the object", wie es in Max heisst.
            auto achsenRichtungen = [&](App::GizmoSpace raum,
                                        float dir[3][3]) {
                if (raum == App::GizmoSpace::World) {
                    for (int a = 0; a < 3; ++a) {
                        for (int k = 0; k < 3; ++k) {
                            dir[a][k] = (a == k) ? 1.0F : 0.0F;
                        }
                    }
                    return;
                }
                Camera hilfL;
                for (int k = 0; k < 3; ++k) {
                    hilfL.pos[k] = sh2.pos[k];
                    hilfL.angles[k] =
                        (sh2.hasAng ? sh2.ang[k] : 0.0F) + g_app->gizmoAngles[k];
                }
                float fL[3];
                float rL[3];
                float uL[3];
                hilfL.forward(fL);
                hilfL.right(rL);
                hilfL.up(uL);
                for (int k = 0; k < 3; ++k) {
                    dir[0][k] = rL[k];
                    dir[1][k] = uL[k];
                    dir[2][k] = fL[k];
                }
            };

            // Beim reinen Drehen gibt es keinen Versatz - der Zweig oben
            // greift also nicht. Trotzdem soll man sehen, wohin sie nach
            // der Drehung blickt.
            const bool gedrehtNun =
                g_app->gizmoAngles[0] != 0.0F ||
                g_app->gizmoAngles[1] != 0.0F ||
                g_app->gizmoAngles[2] != 0.0F;
            if (ausgewaehlt && gedrehtNun && !verschobenNun && sh2.hasAng) {
                float vang2[3];
                for (int k = 0; k < 3; ++k) {
                    vang2[k] = sh2.ang[k] + g_app->gizmoAngles[k];
                }
                zeichneKamera(sh2.pos, vang2, 120, 255, 140, true, true);
            }

            if (ausgewaehlt) {
                // Fuer den Griff bereitlegen.
                float dnow[3][3];
                achsenRichtungen(g_app->gizmoMode == App::GizmoMode::Rotate
                                     ? g_app->gizmoRotSpace
                                     : g_app->gizmoMoveSpace,
                                 dnow);
                for (int a = 0; a < 3; ++a) {
                    for (int k = 0; k < 3; ++k) {
                        g_app->gizmoDirNow[a][k] = dnow[a][k];
                    }
                }
                for (int k = 0; k < 3; ++k) {
                    g_app->gizmoAngNow[k] = sh2.hasAng ? sh2.ang[k] : 0.0F;
                }
            }

            if (ausgewaehlt && g_app->gizmoMode == App::GizmoMode::Rotate) {
                // Auch die Ringe sind ein Gizmo im Bild. Vorher setzte nur
                // der Verschieben-Zweig gizmoShown - der Drehmodus hing
                // also davon ab, ob man ZUFAELLIG vorher im Verschieben
                // war. War man es nie, liess sich kein Ring anfassen.
                g_app->gizmoShown = true;
                const float ringRad = sizeAt(sh2.pos) * 3.0F;
                float rdir[3][3];
                // WAEHREND DES ZIEHENS bleiben die Ringe stehen.
                //
                // Vorher wurden sie jedes Bild aus sh2.ang + gizmoAngles neu
                // gebaut - sie drehten sich also mit der Vorschau mit.
                // Gemeldet als "jetzt rotiert beides mit".
                //
                // In 3ds Max bleibt das Gizmo stehen und ein Tortenstueck
                // zeigt den Betrag: "As you select an axis and drag, an arc
                // is highlighted that shows the distance of the rotation".
                // Genau dafuer braucht es einen STEHENDEN Bezug - ein
                // mitdrehender Ring misst gegen sich selbst und zeigt
                // nichts an.
                //
                // Dasselbe Einfrieren wie in rc156 beim Verschieben, nur
                // dort fuer die Rechnung und hier fuer die Zeichnung.
                if (g_app->gizmoAxis >= 0 && g_app->gizmoAxis < 3) {
                    for (int a = 0; a < 3; ++a) {
                        for (int k = 0; k < 3; ++k) {
                            rdir[a][k] = g_app->gizmoAxisDir[a][k];
                        }
                    }
                } else {
                    achsenRichtungen(g_app->gizmoRotSpace, rdir);
                }
                const float* achsen[3] = {rdir[0], rdir[1], rdir[2]};
                for (int ax = 0; ax < 3; ++ax) {
                    // Der Ring liegt in der Ebene der beiden ANDEREN Achsen.
                    const float* a1 = achsen[(ax + 1) % 3];
                    const float* a2 = achsen[(ax + 2) % 3];
                    const bool aktivR =
                        (g_app->gizmoRotating && g_app->gizmoAxis == ax) ||
                        (!g_app->gizmoRotating && g_app->gizmoHover == ax);
                    const std::uint8_t hell = aktivR ? 255U : 190U;
                    const std::uint8_t dunkel = aktivR ? 140U : 30U;
                    const std::uint8_t rr2 = (ax == 0) ? hell : dunkel;
                    const std::uint8_t gg2 = (ax == 1) ? hell : dunkel;
                    const std::uint8_t bb2 = (ax == 2) ? hell : dunkel;
                    P2 vorigP;
                    bool hatVorig = false;
                    constexpr int kTeile = 48;
                    for (int t = 0; t <= kTeile; ++t) {
                        const float w2 = 6.2831853F *
                                         static_cast<float>(t) /
                                         static_cast<float>(kTeile);
                        float pt[3];
                        for (int k = 0; k < 3; ++k) {
                            pt[k] = sh2.pos[k] + g_app->gizmoOffset[k] +
                                    (a1[k] * std::cos(w2) + a2[k] * std::sin(w2)) *
                                        ringRad;
                        }
                        const P2 jetzt2 = project(pt);
                        // Den Punkt fuer den Ueberfahr-Test merken - der
                        // Test laeuft jede Runde, gezeichnet wird nur bei
                        // mapDirty. Dieselbe Arbeitsteilung wie bei den
                        // Achsenstrecken des Verschiebens.
                        if (t < App::kRingPts) {
                            g_app->gizmoRingSx[ax][t] =
                                jetzt2.ok ? jetzt2.x : -1.0F;
                            g_app->gizmoRingSy[ax][t] =
                                jetzt2.ok ? jetzt2.y : -1.0F;
                        }
                        if (hatVorig) {
                            line(vorigP, jetzt2, rr2, gg2, bb2, aktivR);
                        }
                        vorigP = jetzt2;
                        hatVorig = true;
                    }

                    // --- Das Tortenstueck --------------------------------
                    //
                    // Nur an der Achse, an der gezogen wird, und nur dann.
                    // Es zeigt, WIE WEIT gedreht wurde - in Max heisst das
                    // "Show Pie Slice", und ohne es ist ein stehender Ring
                    // nur ein Ring.
                    //
                    // Gezeichnet als Speichen vom Mittelpunkt zum Rand:
                    // gefuellte Flaechen kann der Zeichner nicht, aber ein
                    // dichter Faecher liest sich genauso.
                    if (aktivR && g_app->gizmoAxis == ax) {
                        const float grad = g_app->gizmoAngles[ax] -
                                           g_app->gizmoRotStart[ax];
                        const float bogen = grad * 3.14159265F / 180.0F;
                        const int speichen =
                            std::clamp(static_cast<int>(std::fabs(grad) / 4.0F),
                                       1, 90);
                        float mitte[3];
                        for (int k = 0; k < 3; ++k) {
                            mitte[k] = sh2.pos[k] + g_app->gizmoOffset[k];
                        }
                        const P2 mp2 = project(mitte);
                        for (int q = 0; q <= speichen; ++q) {
                            const float w3 = bogen * static_cast<float>(q) /
                                             static_cast<float>(speichen);
                            float pt2[3];
                            for (int k = 0; k < 3; ++k) {
                                pt2[k] = mitte[k] +
                                         (a1[k] * std::cos(w3) +
                                          a2[k] * std::sin(w3)) * ringRad;
                            }
                            line(mp2, project(pt2), 255, 230, 120, false);
                        }
                    }
                }
            }

            if (ausgewaehlt && g_app->gizmoMode == App::GizmoMode::Move) {
                // Deutlich groesser als in rc153. Ein Gizmo, das man suchen
                // muss, ist keines - in Max nimmt es einen guten Teil des
                // Bildes ein, und das ist der Grund.
                const float len = sizeAt(sh2.pos) * 5.5F;
                g_app->gizmoWorldLen = len;
                g_app->gizmoShown = true;
                float mdir[3][3];
                achsenRichtungen(g_app->gizmoMoveSpace, mdir);
                for (int ax = 0; ax < 3; ++ax) {
                    float ende[3];
                    for (int k = 0; k < 3; ++k) {
                        ende[k] = sh2.pos[k] + g_app->gizmoOffset[k];
                    }
                    float anfang[3];
                    for (int k = 0; k < 3; ++k) {
                        anfang[k] = ende[k];
                    }
                    // Im Weltsystem ist mdir die Einheitsmatrix, das ist
                    // also genau das bisherige ende[ax] += len.
                    for (int k = 0; k < 3; ++k) {
                        ende[k] += mdir[ax][k] * len;
                    }
                    const P2 a2 = project(anfang);
                    const P2 b2 = project(ende);
                    g_app->gizmoSx[ax][0] = a2.ok ? a2.x : -1.0F;
                    g_app->gizmoSy[ax][0] = a2.ok ? a2.y : -1.0F;
                    g_app->gizmoSx[ax][1] = b2.ok ? b2.x : -1.0F;
                    g_app->gizmoSy[ax][1] = b2.ok ? b2.y : -1.0F;
                    // Angefasst ODER ueberfahren: die Achse leuchtet.
                    //
                    // Ueberfahren, damit man vor dem Klick weiss, ob man
                    // trifft. Angefasst, damit man waehrend des Ziehens
                    // sieht, welche wirkt - auch wenn die Maus laengst
                    // daneben steht.
                    const bool aktiv = (g_app->gizmoAxis == ax) ||
                                       (g_app->gizmoAxis < 0 &&
                                        g_app->gizmoHover == ax);
                    const std::uint8_t hell = aktiv ? 255U : 200U;
                    const std::uint8_t dunkel = aktiv ? 140U : 30U;
                    const std::uint8_t ar = (ax == 0) ? hell : dunkel;
                    const std::uint8_t ag = (ax == 1) ? hell : dunkel;
                    const std::uint8_t ab = (ax == 2) ? hell : dunkel;
                    line(a2, b2, ar, ag, ab, true);
                    // Ein Kloetzchen an der Spitze - da faellt das Zielen
                    // leichter als auf einen Strich.
                    if (b2.ok) {
                        const int bx = static_cast<int>(b2.x);
                        const int by = static_cast<int>(b2.y);
                        const int r2 = aktiv ? 7 : 5;
                        for (int dy2 = -r2; dy2 <= r2; ++dy2) {
                            for (int dx2 = -r2; dx2 <= r2; ++dx2) {
                                plot(bx + dx2, by + dy2, b2.z, ar, ag, ab);
                            }
                        }
                    }
                }

                // --- Das Kaestchen in der Mitte ----------------------
                //
                // 3ds Max nennt es "center box": "You can constrain
                // translation to the viewport plane by dragging the center
                // box." Damit bewegt man in der BILDEBENE - also in alle
                // Richtungen zugleich, so wie man es gerade sieht.
                //
                // Es ist kein Ersatz fuer die Achsen, sondern ihr
                // Gegenstueck: die Achsen sind genau, das Kaestchen ist
                // schnell.
                float mittePos[3];
                for (int k = 0; k < 3; ++k) {
                    mittePos[k] = sh2.pos[k] + g_app->gizmoOffset[k];
                }
                const P2 mitte = project(mittePos);
                if (mitte.ok) {
                    const bool aktivM =
                        (g_app->gizmoAxis == App::kGizmoFree) ||
                        (g_app->gizmoAxis < 0 &&
                         g_app->gizmoHover == App::kGizmoFree);
                    const std::uint8_t mv = aktivM ? 255U : 190U;
                    const int mr = aktivM ? 9 : 7;
                    const int cx = static_cast<int>(mitte.x);
                    const int cy = static_cast<int>(mitte.y);
                    for (int d = -mr; d <= mr; ++d) {
                        plot(cx + d, cy - mr, mitte.z, mv, mv, mv);
                        plot(cx + d, cy + mr, mitte.z, mv, mv, mv);
                        plot(cx - mr, cy + d, mitte.z, mv, mv, mv);
                        plot(cx + mr, cy + d, mitte.z, mv, mv, mv);
                    }
                }
            }
        }
    }

    // --- Die Kamera, WIE SIE JETZT STEHT ---------------------------------
    //
    // Die Marken oben zeigen, wo im Skript ein MOVE oder PAN steht - also
    // die Stuetzstellen. Was dazwischen passiert, sah man nicht: schiebt man
    // den Zeitregler, blieben sie stehen.
    //
    // Diese eine Marke bewegt sich mit. Sie sitzt genau da, wo die Kamera
    // zum eingestellten Zeitpunkt ist, und zeigt dorthin, wo sie hinsieht -
    // dieselbe Rechnung, die auch "Durch die Kamera" benutzt (camTrack.at).
    // So sieht man die Fahrt von aussen, waehrend man den Regler zieht.
    g_app->geisterKamera = false;
    Shot laufend;
    bool zeigeLaufend = false;
    {
        const CameraState st = camStateAt(g_app->playMs);
        if (st.enabled) {
            for (int i = 0; i < 3; ++i) {
                laufend.pos[i] = st.pos[i];
                laufend.ang[i] = st.angles[i];
            }
            laufend.hasPos = true;
            laufend.hasAng = true;
            zeigeLaufend = true;
        }
    }

    // Nur noch EINE Kamera.
    //
    // Vorher stand fuer jeden MOVE-Befehl ein eigener Rahmen im Bild - bei
    // zehn Befehlen zehn Kameras, obwohl es im Skript nur eine gibt, die
    // sich bewegt. Das war irrefuehrend.
    //
    // Jetzt steht dort die Kamera, wie sie zur eingestellten Zeit steht.
    // Wo im Skript ihre Stuetzstellen liegen, sieht man, sobald man sie
    // ANKLICKT - dann erscheint die Bahn mit den Quadraten. So halten es
    // Max und Maya auch.
    // --- Welche Kamera ist der AUSGANGSPUNKT? -----------------------------
    //
    // Ist ein Schluessel angewaehlt, dann SEINE - nicht die laufende.
    //
    // Vorher zeigte die laufende Kamera den Stand zur aktuellen Zeit, und
    // die gruene Vorschau den des Schluessels. Zwei richtige Werte zu
    // verschiedenen Zeitpunkten, und im Bild sah es aus, als zeige die
    // Vorschau falsch.
    //
    // In rc208/rc209 habe ich versucht, das ueber die ZEIT zu loesen: den
    // Zeiger zum Schluessel springen lassen. Das war der falsche Hebel und
    // hat mehr kaputt gemacht, als es half - landet der Zeiger hinter
    // einem camera(DISABLE), ist gar keine Kamera mehr da, und nach einem
    // Rueckgaengig erst recht nicht.
    //
    // Richtig ist, das BILD zu aendern statt den Zustand: beim Bearbeiten
    // eines Schluessels wird er selbst gezeigt. Die Zeit bleibt, wo sie
    // ist, und nichts anderes im Programm merkt etwas davon.
    const Shot* schluesselShot = nullptr;
    if (g_app->selectedKey >= 0 &&
        static_cast<std::size_t>(g_app->selectedKey) < shots.size()) {
        schluesselShot = &shots[static_cast<std::size_t>(g_app->selectedKey)];
    }

    // Das Protokoll beim Anwaehlen ist wieder heraus - es hat seine Arbeit
    // getan (rc213) und faellt sonst bei jedem Klick an.
    for (std::size_t k = 0; k <= shots.size(); ++k) {
        const bool istLaufend = (k == shots.size());
        if (!istLaufend) {
            continue;   // die Stuetzstellen zeichnet die Bahn, nicht hier
        }
        if (schluesselShot == nullptr && !zeigeLaufend) {
            break;
        }
        // Die Kamera steht, wo sie zur eingestellten ZEIT steht - auch mit
        // angewaehltem Schluessel. Gemeldet: "wenn ich sie selected habe und
        // die Timeline bewege, dann geht das nicht" - bis hierher hatte der
        // Schluessel Vorrang, und die Kamera klebte an ihm. Welcher
        // Schluessel bearbeitet wird, zeigen das gelbe Quadrat und das Gizmo
        // daran; beim Ziehen der Zeitleiste geht er mit (schluesselMitZeit).
        // Nur wenn die Kamera zu dieser Zeit aus ist, steht sie am Schluessel.
        const Shot& sh = zeigeLaufend                ? laufend
                         : (schluesselShot != nullptr) ? *schluesselShot
                                                       : laufend;
        if (istLaufend) {
            // Wo sitzt sie auf dem Bildschirm? Der Klick braucht das.
            const P2 mitte = project(sh.pos);
            g_app->cameraSx = mitte.ok ? mitte.x : -1.0F;
            g_app->cameraSy = mitte.ok ? mitte.y : -1.0F;
        }
        const bool selected = !istLaufend && (sh.path == g_app->selectedPath);
        // Die laufende Kamera in Gruen: sie gehoert zu keiner Zeile, sie
        // ist der Zustand zwischen den Zeilen.
        // Angeklickt: kraeftiges Weiss, wie eine Auswahl in Max. Sonst
        // gruen - sie gehoert zu keiner Skriptzeile, sondern ist der
        // Zustand zwischen ihnen.
        const bool camSel = istLaufend && g_app->cameraSelected;
        // --- Farben: GRUEN gehoert der Vorschau, sonst niemandem ---------
        //
        // Die laufende Kamera war gruen (90, 235, 120), solange sie nicht
        // selbst angewaehlt war - und die Vorschau am Gizmo ist ebenfalls
        // gruen. Wer einen SCHLUESSEL anwaehlt (nicht die Kamera), bekam
        // also zwei gruene Kameras dicht beieinander, und die eine
        // verschwand optisch in der anderen. Gemeldet als "die weisse
        // Kamera wird unsichtbar gemacht oder geloescht".
        //
        // Jetzt ist die Aufteilung eindeutig:
        //   angewaehlt      - kraeftiges Weiss
        //   sonst           - gedecktes Hellgrau
        //   die VORSCHAU    - gruen, und nur sie
        //
        // Damit sagt die Farbe, was das Ding IST, statt was gerade
        // angeklickt wurde.
        const std::uint8_t r = camSel ? 255 : (istLaufend ? 200 : 255);
        const std::uint8_t g =
            camSel ? 255 : (istLaufend ? 205 : (selected ? 210 : 110));
        const std::uint8_t b =
            camSel ? 255 : (istLaufend ? 215 : (selected ? 50 : 210));

        // Steht gerade eine Vorschau am Gizmo an, wird die Kamera DAMIT
        // gezeichnet - sonst zieht man am Gizmo und die Kamera bleibt
        // stehen. Der Versatz gilt nur, wenn die Kamera auch angewaehlt
        // ist; sonst wanderte sie, waehrend man einen anderen Schluessel
        // bearbeitet.
        // Die laufende Kamera zeigt den Stand AUS DEM SKRIPT - ohne die
        // Vorschau am Gizmo.
        //
        // In rc168 bekam sie den Versatz dazu, damit sie sich beim Ziehen
        // ueberhaupt bewegt. Inzwischen zeichnet der Gizmo-Zweig aber eine
        // eigene, gruene Vorschaukamera - und damit standen ZWEI Modelle
        // am selben Ort mit derselben Stellung uebereinander. Gemeldet als
        // "die 2. Kamera die dabei erstellt wird ist etwas komisch".
        //
        // Jetzt ist es ein Vorher-Nachher-Paar: weiss, wo sie laut Skript
        // steht, gruen, wo sie nach "Ins Skript schreiben" staende. Genau
        // das will man beim Einstellen sehen.
        //
        // Der Durchblick nimmt die Vorschau weiter mit - dort gibt es kein
        // Paar, sondern nur das eine Bild, das man beurteilt.
        zeichneKamera(sh.pos, sh.ang, r, g, b, selected, sh.hasAng);

        // --- Unterwegs: wohin die Kamera faehrt ---------------------------
        //
        // shank: "sobald sich die Kamera bewegt, ist der Gizmo nicht mehr
        // dran - soll das so sein?" Ja: bearbeitet wird der ZIELPUNKT des
        // MOVE, und das Gizmo sitzt dort. Damit man das sieht, steht dort
        // eine gelbe Geisterkamera mit der Blickrichtung am Ziel, und eine
        // gestrichelte Linie fuehrt von der fahrenden Kamera hin. Steht die
        // Kamera auf ihrem Punkt, faellt beides weg.
        if (zeigeLaufend && schluesselShot != nullptr && g_app->cameraSelected &&
            &sh == &laufend) {
            const float dx = laufend.pos[0] - schluesselShot->pos[0];
            const float dy = laufend.pos[1] - schluesselShot->pos[1];
            const float dz = laufend.pos[2] - schluesselShot->pos[2];
            if (dx * dx + dy * dy + dz * dz > 4.0F) {
                zeichneKamera(schluesselShot->pos, schluesselShot->ang, 200, 170, 60, false,
                              schluesselShot->hasAng);
                constexpr int kStriche = 24;
                for (int q = 0; q < kStriche; q += 2) {
                    float a1[3];
                    float b1[3];
                    for (int k2 = 0; k2 < 3; ++k2) {
                        const float von = laufend.pos[k2];
                        const float nach = schluesselShot->pos[k2];
                        a1[k2] = von + (nach - von) * static_cast<float>(q) / kStriche;
                        b1[k2] = von + (nach - von) * static_cast<float>(q + 1) / kStriche;
                    }
                    line(project(a1), project(b1), 220, 190, 80, false);
                }
                g_app->geisterKamera = true;
            }
        }
    }
}

// Was der gewaehlte camera-Befehl sieht.
//
// Ort, Blickrichtung UND Zoom. Der Zoom ist dabei nicht kosmetisch: in
// Ravens 1510 Skripten stehen 461 ZOOM-Befehle mit Werten von 6,3 bis 110
// Grad. Eine Einstellung mit 30 Grad zeigt etwas voellig anderes als eine
// mit 80, und ohne den richtigen Blickwinkel beurteilt man das falsche Bild.
//
// Gesucht wird ab der gewaehlten Zeile RUECKWAERTS: die Engine arbeitet die
// Befehle nacheinander ab, also gilt der letzte MOVE, der letzte PAN und der
// letzte ZOOM vor dieser Stelle. Wer nur die gewaehlte Zeile ansieht,
// bekommt bei einem einzelnen ZOOM keine Position.
struct ShotView {
    float pos[3]{};
    float ang[3]{};
    float fovX = 90.0F;      // CAMERA_DEFAULT_FOV aus cg_camera.h
    bool havePos = false;
    bool haveAng = false;
    std::string label;
};

bool shotAtSelection(ShotView& out) {
    const Path& sel = g_app->selectedPath;
    if (sel.empty()) {
        return false;
    }
    // Nur auf der Ebene der Auswahl suchen - ein camera-Befehl in einem
    // anderen Block gehoert nicht zu dieser Einstellung.
    Path p = sel;
    bool any = false;
    for (;;) {
        const Node* n = nodeAt(g_app->doc.script(), p);
        if (n != nullptr && n->name == "camera" && !n->args.empty()) {
            const std::string& type = n->args[0].text;
            float v[3] = {0, 0, 0};
            if (n->args.size() >= 2) {
                std::istringstream is(n->args[1].text);
                is >> v[0] >> v[1] >> v[2];
            }
            if (type == "MOVE" && !out.havePos) {
                for (int k = 0; k < 3; ++k) { out.pos[k] = v[k]; }
                out.havePos = true;
                any = true;
            } else if (type == "PAN" && !out.haveAng) {
                for (int k = 0; k < 3; ++k) { out.ang[k] = v[k]; }
                out.haveAng = true;
                any = true;
            } else if (type == "ZOOM" && out.fovX == 90.0F && n->args.size() >= 2) {
                try {
                    out.fovX = std::stof(n->args[1].text);
                    any = true;
                } catch (...) {
                    // kein Zahlenwert - dann bleibt die Vorgabe
                }
            }
            if (out.label.empty()) {
                out.label = type;
            }
        }
        if (out.havePos && out.haveAng) {
            break;
        }
        if (p.back() == 0) {
            break;
        }
        --p.back();
    }
    return any && out.havePos;
}

// Aus der Kamerastellung einen Befehl bauen und einfuegen.
//
// Zwei Befehle, weil es im Skript auch zwei sind: MOVE setzt den Ort, PAN
// die Blickrichtung. Die Reihenfolge der Winkel ist die der Engine -
// pitch, yaw, roll -, genau wie camera ( PAN, < p y r >, ... ) sie erwartet.
void insertCameraHere() {
    const std::vector<const Command*> ov = g_app->db.overloads("camera");
    if (ov.empty()) {
        return;
    }
    auto make = [&](const char* type, const std::string& vec) {
        Node n;
        n.kind = Node::Kind::Command;
        n.name = "camera";
        Arg a;
        a.kind = Arg::Kind::Ident;
        a.text = type;
        a.typeset = "CAMERA_COMMANDS";
        n.args.push_back(a);
        Arg v;
        v.kind = Arg::Kind::Vector;
        v.text = vec;
        n.args.push_back(v);
        if (std::string(type) == "PAN") {
            Arg zero;
            zero.kind = Arg::Kind::Vector;
            zero.text = "0.000 0.000 0.000";
            n.args.push_back(zero);
        }
        Arg t;
        t.kind = Arg::Kind::Number;
        t.text = "0.000";
        n.args.push_back(t);
        return n;
    };
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.3f %.3f %.3f",
                  static_cast<double>(g_app->cam.pos[0]),
                  static_cast<double>(g_app->cam.pos[1]),
                  static_cast<double>(g_app->cam.pos[2]));
    const std::string origin = buf;
    std::snprintf(buf, sizeof(buf), "%.3f %.3f %.3f",
                  static_cast<double>(g_app->cam.angles[0]),
                  static_cast<double>(g_app->cam.angles[1]),
                  static_cast<double>(g_app->cam.angles[2]));
    const std::string angles = buf;

    // Beide Befehle als EIN Rueckgaengig-Schritt (insertAfterAll). Vorher
    // zwei insertAfter: ein Strg+Z nahm nur das PAN zurueck, das MOVE blieb
    // stehen (Kartentest 27.09.).
    Path at;
    if (!g_app->doc.insertAfterAll(g_app->selectedPath,
                                   {make("MOVE", origin), make("PAN", angles)}, &at)) {
        return;
    }
    g_app->selectedPath = at;
    rebuildTree();
}

// Die Kartenansicht. Sie steht dort, wo sonst die Ereignisliste ist:
// waehrend man Kameras setzt, braucht man die Befehlsliste nicht, und der
// Skriptablauf bleibt daneben sichtbar - dort landet ja der neue Befehl.
// --- Dateien fuer Modell und Karte -------------------------------------
//
// Jede Ansicht bekommt ihre eigenen Knoepfe. Der Grundsatz dahinter: beim
// Laden wird das Uebliche automatisch mitgesucht - zur .glm die .skin und
// die im Modell vermerkte .gla, zur .bsp die Entities darin. Wer etwas
// anderes will, waehlt es danach von Hand.
void openModelFile() {
    const std::string p = platform::openFileDialog(
        tr(Str::OpenGlm), "Ghoul2 models (*.glm)|*.glm|All files (*.*)|*.*",
        g_app->settings.lastDir);
    if (p.empty()) {
        return;
    }
    diag::Step step("Modell laden: " + p);
    GlmModel gm;
    std::string err;
    if (!readGlm(slurp(p), gm, &err)) {
        step.fail(err);
        platform::showError(p + "\n" + err, tr(Str::AppTitle));
        return;
    }
    g_app->settings.lastDir = directoryOf(p);

    g_app->model = std::move(gm);
    g_app->modelPath = p;
    g_app->modelDir = directoryOf(p);

    // ALLE Haeute daneben sammeln, nicht nur die erste.
    //
    // Ein Modell bringt oft mehrere mit - model_default, model_red,
    // model_blue. Sie entscheiden nicht nur ueber Texturen, sondern auch
    // darueber, WELCHE Flaechen sichtbar sind: eine Zeile "kopf_b,*off"
    // schaltet eine Variante ab. Ohne Auswahl zeichnet man alle Varianten
    // uebereinander.
    g_app->modelSkins.clear();
    g_app->modelSkinIndex = -1;
    for (const std::string& f : platform::listDirectory(g_app->modelDir)) {
        if (f.size() > 5 && f.compare(f.size() - 5, 5, ".skin") == 0) {
            g_app->modelSkins.push_back(f);
        }
    }
    std::sort(g_app->modelSkins.begin(), g_app->modelSkins.end());
    // Die Vorgabehaut zuerst, wenn es sie gibt.
    for (std::size_t i = 0; i < g_app->modelSkins.size(); ++i) {
        if (g_app->modelSkins[i] == "model_default.skin") {
            applySkinByIndex(static_cast<int>(i));
            break;
        }
    }
    if (g_app->modelSkinIndex < 0 && !g_app->modelSkins.empty()) {
        applySkinByIndex(0);
    }
    diag::info(std::to_string(g_app->modelSkins.size()) + " Haeute gefunden");
    g_app->anim = GlaAnimation{};
    g_app->animList.clear();
    g_app->animIndex = -1;
    g_app->animFrame = 0;
    for (int k = 0; k < 3; ++k) {
        g_app->modelCam.pos[k] =
            (g_app->model.mins[k] + g_app->model.maxs[k]) * 0.5F;
    }
    float size = 0.0F;
    for (int k = 0; k < 3; ++k) {
        size = std::max(size, g_app->model.maxs[k] - g_app->model.mins[k]);
    }
    // Abstand so, dass die Figur das Bild fuellt. 2.2 war zu weit - eine
    // 66 Einheiten hohe Figur stand als Streichholz in der Mitte.
    g_app->modelOrbit.distance = std::max(size * 1.15F, 24.0F);
    g_app->modelCam.angles[0] = 0.0F;
    g_app->modelCam.angles[1] = 180.0F;
    // Die Kamera VOR die Figur setzen: der Drehpunkt liegt in ihrer Mitte.
    {
        float f[3];
        g_app->modelCam.forward(f);
        for (int k = 0; k < 3; ++k) {
            g_app->modelCam.pos[k] =
                (g_app->model.mins[k] + g_app->model.maxs[k]) * 0.5F -
                f[k] * g_app->modelOrbit.distance;
        }
    }
    g_app->modelDirty = true;
    g_app->leftMode = 2;

    loadModelTextures();
    // Das Skelett gleich mitsuchen - der Pfad steht im Modell.
    loadSkeletonForModel();
}

// Eine Haut aus der Liste anwenden.
//
// Wichtig: das Modell wird VORHER zurueckgesetzt. Eine Haut schaltet Flaechen
// ab, und die bleiben sonst aus, wenn man zu einer Haut wechselt, die sie
// nicht erwaehnt.
void applySkinByIndex(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= g_app->modelSkins.size()) {
        return;
    }
    const std::string path =
        g_app->modelDir + "/" + g_app->modelSkins[static_cast<std::size_t>(index)];
    // Erst von der Platte, dann aus den Archiven.
    //
    // Ein Modell aus einer .pk3 hat keinen Pfad auf der Platte; slurp allein
    // faende die Haut nie, und der Wechsel taete stillschweigend nichts.
    std::string text = slurp(path);
    if (text.empty() && !readFromArchives(path, text)) {
        diag::info("Haut nicht lesbar: " + path);
        return;
    }
    for (GlmSurface& s : g_app->model.surfaces) {
        s.skinnedOff = false;
        s.texture.clear();
    }
    applySkin(text, g_app->model);
    g_app->modelSkinIndex = index;
    loadModelTextures();
    diag::info("Haut: " + g_app->modelSkins[static_cast<std::size_t>(index)]);
}

void openSkinFile() {
    const std::string p = platform::openFileDialog(
        tr(Str::OpenSkin), "Skins (*.skin)|*.skin|All files (*.*)|*.*",
        g_app->modelPath.empty() ? g_app->settings.lastDir
                                 : directoryOf(g_app->modelPath));
    if (p.empty()) {
        return;
    }
    const std::string skin = slurp(p);
    if (skin.empty()) {
        platform::showError(p, tr(Str::AppTitle));
        return;
    }
    applySkin(skin, g_app->model);
    loadModelTextures();
    diag::info("Haut gewechselt: " + p);
}

// Eine .gla von Hand waehlen.
//
// Normalerweise steht der Pfad im Modell, und wir nehmen ihn. Aber ein Mod
// kann eine eigene .gla mitbringen, und beim Bauen einer neuen Animation
// will man sie ausprobieren, bevor sie am gewohnten Ort liegt.
void openSkeletonFile() {
    const std::string p = platform::openFileDialog(
        tr(Str::ModelLoadGla), "Ghoul2 skeleton (*.gla)|*.gla|All files (*.*)|*.*",
        g_app->settings.lastDir);
    if (p.empty()) {
        return;
    }
    diag::Step step("Skelett laden: " + p);
    GlaAnimation a;
    std::string err;
    if (!readGla(slurp(p), a, &err)) {
        step.fail(err);
        platform::showError(p + "\n" + err, tr(Str::AppTitle));
        return;
    }
    // Die animation.cfg liegt daneben und nennt die Abschnitte.
    const std::string cfg = slurp(directoryOf(p) + "/animation.cfg");
    g_app->animList = cfg.empty() ? std::vector<AnimEntry>{}
                                  : parseAnimationCfg(cfg);
    diag::info(std::to_string(a.bones.size()) + " Knochen, " +
               std::to_string(a.numFrames) + " Bilder, " +
               std::to_string(g_app->animList.size()) + " Abschnitte");
    g_app->anim = std::move(a);
    g_app->animIndex = -1;
    g_app->animFrame = 0;
    g_app->modelDirty = true;
}

// Ein Bild auf eine handliche Groesse bringen.
//
// Eine Editoransicht braucht keine 1024er-Textur; bei siebzig Shadern spart
// das ein Vielfaches an Speicher. Der Code stand dreimal woertlich da -
// einmal fuer die Karte, zweimal fuer das Modell.
TextureSet::Tex shrink(const image::Image& im, int maxSize) {
    // Die Rechnung selbst steht im Bildmodul - sie wird an drei Stellen
    // gebraucht und muss ueberall dieselbe sein.
    TextureSet::Tex t;
    image::shrinkTo(im, maxSize, t.width, t.height, t.rgba);
    return t;
}

// Die Texturen des Modells aus den .pk3 holen.
//
// Die .skin nennt je Flaeche eine Datei. Der Weg ist derselbe wie bei der
// Karte, nur dass hier kein Shaderskript dazwischensteht - die .skin zeigt
// direkt auf ein Bild.
// Die Texturen zu einem Modell beschaffen.
//
// Herausgezogen, weil es jetzt zwei Aufrufer gibt: die Modellansicht und die
// Figuren in der Karte.
ModelTextures texturesFor(const GlmModel& model);

void loadModelTextures() {
    diag::Step step("Modelltexturen suchen");
    g_app->modelTextures = texturesFor(g_app->model);
    g_app->modelDirty = true;
}

ModelTextures texturesFor(const GlmModel& model) {
    ModelTextures out;
    if (model.empty()) {
        return out;
    }
    if (g_app->gamePaths.empty()) {
        rescanGamePaths();
    }

    // Die Shaderskripte einmal einlesen. Eine .skin zeigt meistens direkt
    // auf eine Bilddatei - model_blue.skin tut das bei allen 34 Eintraegen -
    // aber bei Lichtschwertern und Glaseffekten steht dort ein Shadername.
    // Dann liegt das Bild in einer "map"-Zeile des Skripts.
    //
    // Erst wenn die direkte Suche nichts ergibt, wird nachgesehen: bei den
    // meisten Modellen ist der Umweg unnoetig.
    // Die Tabelle lebt im Programmzustand, nicht hier.
    //
    // Vorher stand sie als oertliche Veraenderliche genau an dieser Stelle -
    // also wurde sie JE MODELL neu aufgebaut. Bei 40 Archiven, 9014
    // Eintraegen und 64 Figuren stand im Protokoll dutzendfach "9014
    // Shadereintraege gelesen", und das Programm schien zu haengen.
    ShaderMap& shaderMap = g_app->shaderMap;
    auto readShaders = [&]() {
        if (g_app->shaderMapRead) {
            return;
        }
        g_app->shaderMapRead = true;
        for (const GamePath& gp : g_app->gamePaths) {
            for (const FoundFile& f : findByExtension(gp, {".shader"})) {
                std::string text;
                if (readFromArchives(f.name, text)) {
                    parseShaderScript(text, shaderMap);
                }
            }
        }
        diag::info(std::to_string(shaderMap.size()) + " Shadereintraege gelesen");
    };
    constexpr int kMaxSize = 256;
    out.bySurface.resize(model.surfaces.size());
    // Gezaehlt statt je Flaeche gemeldet. Bei einem Sturmtruppler stand
    // sechzehnmal dieselbe Zeile im Protokoll - jede kostet einen Schreib-
    // vorgang, und zusammen machten sie das Protokoll unlesbar.
    int viaShader = 0;

    for (std::size_t i = 0; i < model.surfaces.size(); ++i) {
        const GlmSurface& s = model.surfaces[i];
        if (s.isTag()) {
            continue;
        }
        // --- Wo der Texturname steht -----------------------------------
        //
        // Gemeldet: "die Lichtschwerter sehen nicht richtig aus, da fehlt
        // glaub die Textur?"
        //
        // Bis rc259 wurde NUR s.texture gelesen - das Feld, das eine
        // .skin-Datei fuellt. Figuren haben eine, Waffen nicht: bei
        // models/weapons2/saber/saber_w.glm steht der Name in der
        // HIERARCHIE der Flaeche, also in s.shader:
        //
        //     w_saber   shader="models/weapons2/saber/saber.tga"  texture=""
        //     w_handle  shader="models/weapons2/saber/saber.tga"  texture=""
        //
        // Deshalb stand im Protokoll zehnmal "0 gefunden, 0 nicht
        // gefunden" - es wurde gar nicht erst gesucht, und der Griff kam
        // in der Ersatzfarbe heraus.
        //
        // Die .skin geht VOR: sie ist die Auswahl fuer diese eine Figur,
        // der Shader nur das, was beim Ausleiten des Modells dranstand.
        const std::string& name = s.texture.empty() ? s.shader : s.texture;
        // --- Was KEIN Dateiname ist ------------------------------------
        //
        // Aus einem Protokoll: 244 nicht gefundene Texturen, davon **240
        // mit dem Namen `[nomaterial]`**. rc260 hat auf `[NoMaterial]`
        // verglichen - buchstabengenau, und die Dateien schreiben es
        // klein. Der Filter griff also nie.
        //
        // Der Rest waren `08 - default` und Aehnliches: Materialnamen aus
        // dem Modellierwerkzeug, die beim Ausleiten im Shaderfeld
        // stehenbleiben.
        //
        // Zwei Regeln statt einer Liste:
        //
        //   1. `[nomaterial]` ohne Ruecksicht auf Gross- und
        //      Kleinschreibung.
        //   2. Ein Texturname hat einen SCHRAEGSTRICH. "models/weapons2/
        //      saber/saber.tga" hat drei, "08 - default" keinen. Wer keinen
        //      hat, ist ein Werkzeugrest und keine Datei.
        //
        // Die zweite Regel gilt nur fuer den Rueckfall auf den Shader. Was
        // in einer .skin steht, wird nicht angezweifelt - dort hat jemand
        // bewusst etwas eingetragen.
        if (name.empty()) {
            continue;
        }
        {
            std::string klein = name;
            for (char& c : klein) {
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }
            if (klein == "[nomaterial]") {
                continue;
            }
            if (s.texture.empty() && name.find('/') == std::string::npos) {
                continue;
            }
        }
        std::string data;
        bool got = false;
        std::vector<std::string> tries = textureCandidates(name);

        // Schon einmal geladen? Dann NICHT noch einmal entpacken und
        // entziffern. Der Schluessel wird wie in der Engine gebildet:
        // klein, ohne Endung, nur Schraegstriche.
        // Die GROESSE gehoert in den Schluessel: die Karte verkleinert auf
        // 512, die Figuren auf 256. Ohne sie bekaeme, wer zuerst kommt,
        // recht - und die Lava saehe kloetzchenhaft aus oder eine
        // Figurentextur waere unnoetig gross.
        const std::string schluessel =
            image::mappingName(name) + "@" + std::to_string(kMaxSize);
        const auto imCache = g_app->texCache.find(schluessel);
        if (imCache != g_app->texCache.end()) {
            out.bySurface[i] = imCache->second;
            ++out.found;
            continue;
        }

        for (const std::string& name : tries) {
            if (!readFromArchives(name, data)) {
                continue;
            }
            const image::Image im = image::decode(
                reinterpret_cast<const unsigned char*>(data.data()), data.size());
            if (!im.ok || im.width <= 0 || im.height <= 0) {
                continue;
            }
            TextureSet::Tex t = shrink(im, kMaxSize);
            g_app->texCache[schluessel] = t;
            out.bySurface[i] = std::move(t);
            ++out.found;
            got = true;
            break;
        }
        // Nichts gefunden? Dann ueber das Shaderskript.
        if (!got) {
            readShaders();
            std::string lowered = name;
            for (char& c : lowered) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            // Auch ohne Endung nachsehen: in der .skin steht oft
            // "models/weapons2/saber/blade.tga", im Skript aber
            // "models/weapons2/saber/blade".
            const std::size_t dot = lowered.find_last_of('.');
            const std::size_t slash = lowered.find_last_of('/');
            std::vector<std::string> keys{lowered};
            if (dot != std::string::npos &&
                (slash == std::string::npos || dot > slash)) {
                keys.push_back(lowered.substr(0, dot));
            }
            for (const std::string& key : keys) {
                const auto it = shaderMap.find(key);
                if (it == shaderMap.end()) {
                    continue;
                }
                for (const std::string& name : textureCandidates(it->second.image)) {
                    if (!readFromArchives(name, data)) {
                        continue;
                    }
                    const image::Image im = image::decode(
                        reinterpret_cast<const unsigned char*>(data.data()),
                        data.size());
                    if (!im.ok || im.width <= 0 || im.height <= 0) {
                        continue;
                    }
                    // Auch dieser Weg endet in der Tabelle - unter DEM
                    // Namen, mit dem gesucht wurde. Sonst laeuft jede
                    // Flaeche, deren Bild nur ueber ein Shaderskript zu
                    // finden ist, jedes Mal neu durch die Suche; im
                    // Protokoll sind das die "ueber Shaderskripte"
                    // gezaehlten, und davon gibt es reichlich.
                    TextureSet::Tex t2 = shrink(im, kMaxSize);
                    g_app->texCache[schluessel] = t2;
                    // --- alphaFunc gehoert mit -------------------------
                    //
                    // Der Figurenzeichner prueft den Alphakanal nur noch,
                    // wenn ein Shader es verlangt (mapview.cpp, Notiz bei
                    // `alphaTestet`). Ohne diese Zeile waere er ueberall
                    // aus - und der Geonosianer haette wieder ein schwarzes
                    // Rechteck statt Fluegeln.
                    //
                    // NACH dem Zwischenspeicher gesetzt: der speichert nur
                    // Bildpunkte, und derselbe Bildinhalt kann unter zwei
                    // Shadern verschieden getestet werden.
                    out.bySurface[i] = std::move(t2);
                    out.bySurface[i].alphaTest = it->second.alphaTest;
                    ++out.found;
                    got = true;
                    ++viaShader;
                    break;
                }
                if (got) {
                    break;
                }
            }
        }
        if (!got) {
            ++out.missing;
            diag::info("keine Textur fuer " + s.name + " (" + name + ")");
        }
    }
    std::string line = std::to_string(out.found) + " gefunden, " +
                       std::to_string(out.missing) + " nicht gefunden";
    if (viaShader != 0) {
        line += " (" + std::to_string(viaShader) + " ueber Shaderskripte)";
    }
    diag::info(line);
    return out;
}

// Zu einem Animationsnamen die Bildnummer suchen.
//
// Die animation.cfg nennt je Abschnitt Anfang, Laenge und Tempo. Beim
// Abspielen laeuft die Figur durch ihren Abschnitt - deshalb haengt die
// Bildnummer an der Zeit auf der Zeitleiste, nicht an einer festen Zahl.
// Die Abschnittsliste kommt als Argument: jede Figur hat ihre eigene, denn
// ein Modell darf ein anderes Skelett tragen als die Modellansicht gerade
// zeigt. Vorher nahm diese Funktion immer g_app->animList - also die von
// Hand geladene - und die ist bei einer Mission leer. Deshalb stand jede
// Figur regungslos in ihrer Ruhelage.
// Die Modellansicht benutzt weiterhin die von Hand geladene Liste.
int frameForAnimation(const std::string& name, double sinceMs, bool hold) {
    return frameForAnimationIn(g_app->animList, name, sinceMs, hold);
}

// Die Modelle der Figuren beschaffen.
//
// Der Weg ist dreistufig, und jede Stufe kann fehlschlagen:
//   NPC_type          "md_ani_tcwa"      aus dem NPC_spawner der Karte
//   -> playerModel    "anakin_tcw"       aus ext_data/npcs/*.npc
//   -> Modell         models/players/anakin_tcw/model.glm
//
// Fehlt eine Stufe, bleibt die Figur ohne Modell und wird nicht gezeichnet.
// Geraten wird nichts - eine falsche Figur waere schlimmer als keine.
// Zu einer .gla die animation.cfg finden - in der Reihenfolge der Engine.
//
// code/game/NPC_stats.cpp, G_ParseAnimationFile():
//
//     Com_sprintf(skeletonPath, "models/players/%s/%s.cfg", skeletonName, skeletonName);
//     len = gi.RE_GetAnimationCFG(skeletonPath, ...);
//     if ( len <= 0 ) {
//         Com_sprintf(skeletonPath, "models/players/%s/animation.cfg", skeletonName);
//
// Also ERST <name>/<name>.cfg, DANN <name>/animation.cfg. Der erste Weg
// fehlte hier ganz; bei einem Skelett mit eigener .cfg wurden dadurch die
// Abschnitte des _humanoid genommen oder gar keine.
//
// skeletonName ist der LETZTE Teil des .gla-Ordners, nicht der ganze Pfad:
// aus "models/players/_humanoid/_humanoid" wird "_humanoid" (ebenda, ueber
// COM_SkipPath auf dem abgeschnittenen Pfad).
std::vector<AnimEntry> animSectionsFor(const std::string& animFile) {
    // Die Namensregel liegt im Kern (bhed/gla.h) und wird dort von Proben
    // festgehalten - hier bleibt nur das Nachsehen in den Archiven.
    std::string cfg;
    for (const std::string& name : animCfgCandidates(animFile)) {
        if (readFromArchives(name, cfg)) {
            return parseAnimationCfg(cfg);
        }
    }
    return {};
}

// Das Skelett zu einem Modell holen, hoechstens einmal je .gla.
// Wohin zielt camera(FOLLOW, "gruppe", ...) zum Zeitpunkt ms?
//
// Der MITTELPUNKT aller Entities mit diesem cameraGroup -
// CGCam_FollowUpdate() in cg_camera.cpp sammelt alle Treffer und mittelt
// sie. Bewegt das Skript eine davon gerade, gilt ihr AKTUELLER Ort, nicht
// der aus der Karte: sonst zielt die Kamera auf den Startpunkt, waehrend
// die Figur schon woanders steht.
//
// Herausgezogen, weil es an drei Stellen gebraucht wird - beim Abspielen,
// beim Zeichnen der Kamera und beim Abtasten der Bahn. Vorher stand es nur
// an der ersten, und die gezeichnete Kamera zeigte deshalb woandershin als
// die abgespielte.
bool followCentre(const std::string& gruppe, double ms, float out[3]) {
    if (gruppe.empty()) {
        return false;
    }
    float mitte[3] = {0.0F, 0.0F, 0.0F};
    int wieviele = 0;
    const auto klein = [](std::string x) {
        for (char& c : x) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        return x;
    };
    // Die Gruppe einer Entity zu diesem Zeitpunkt: aus der Karte, bis ein
    // SET_CAMERA_GROUP im Ablauf sie aendert (Q3_SetCameraGroup setzt
    // ent->cameraGroup, CGCam_FollowUpdate liest es jedes Bild).
    const auto gruppeVon = [&](const std::string& name, const std::string& ausKarte) -> std::string {
        const auto it = g_app->kameraGruppen.find(klein(name));
        std::string g = ausKarte;
        if (it != g_app->kameraGruppen.end()) {
            for (const auto& [ab, neu] : it->second) {
                if (ab <= ms) { g = neu; }
            }
        }
        return g;
    };
    std::set<std::string> gesehen;
    for (const MapEntity& e : g_app->map.entities) {
        // Eine Figur heisst wie ihr Spawner sie nennt (NPC_targetname), und
        // sie erbt dessen cameraGroup.
        const std::string* npc = e.find("NPC_targetname");
        const std::string name = (npc != nullptr && !npc->empty()) ? *npc : e.targetname;
        if (e.origin.empty() || gruppeVon(name, e.cameraGroup) != gruppe) {
            continue;
        }
        gesehen.insert(klein(name));
        float o[3] = {0.0F, 0.0F, 0.0F};
        (void)std::sscanf(e.origin.c_str(), "%f %f %f", &o[0], &o[1], &o[2]);
        for (const Actor& a : g_app->scene.actors) {
            if (a.haveStart && klein(a.name) == klein(name)) {
                const ActorState as = a.at(ms);
                for (int k = 0; k < 3; ++k) {
                    o[k] = as.pos[k];
                }
                // Figuren zielt die Engine auf die AUGEN: + ps.viewheight
                // ("Track to their eyes"). Stehend standheight + offset =
                // DEFAULT_MAXS_2 40 + STANDARD_VIEWHEIGHT_OFFSET -4 = 36.
                o[2] += 36.0F;
                break;
            }
        }
        for (int k = 0; k < 3; ++k) {
            mitte[k] += o[k];
        }
        ++wieviele;
    }
    // Figuren, die erst das Skript in die Gruppe nimmt und die in der Karte
    // keinen Eintrag unter diesem Namen haben (etwa der Spieler).
    for (const Actor& a : g_app->scene.actors) {
        if (!a.haveStart || gesehen.count(klein(a.name)) != 0 || gruppeVon(a.name, "") != gruppe) {
            continue;
        }
        const ActorState as = a.at(ms);
        for (int k = 0; k < 3; ++k) {
            mitte[k] += as.pos[k];
        }
        mitte[2] += (a.brushModel > 0) ? 0.0F : 36.0F;
        ++wieviele;
    }
    if (wieviele == 0) {
        return false;
    }
    for (int k = 0; k < 3; ++k) {
        out[k] = mitte[k] / static_cast<float>(wieviele);
    }
    return true;
}

// Die Kamerastellung MIT FOLLOW. Ueberall zu benutzen, wo camTrack.at()
// gefragt wird - sonst zeigt eine der drei Ansichten woandershin.
CameraState camStateAt(double ms) {
    CameraState st = g_app->camTrack.at(ms);
    // Waehrend eines Schwenks zielt FOLLOW nicht (check_follow).
    if (st.followGroup.empty() || st.panning) {
        return st;
    }
    float ziel[3];
    if (!followCentre(st.followGroup, ms, ziel)) {
        return st;
    }
    // --- Nachziehen wie CGCam_FollowUpdate --------------------------------
    //
    // Je Bild: frac = frametime / 100 * followSpeed / 100, dann
    // angles += frac * AngleNormalize180(ziel - angles) - ein Nachziehen mit
    // der Zeitkonstante 100 ms * 100 / speed. Ohne initLerp springt das erste
    // Bild aufs Ziel, danach wird nachgezogen.
    //
    // Nachgerechnet wird in 10-ms-Schritten, aber nur ueber die letzten sechs
    // Zeitkonstanten: was davor lag, ist auf unter ein Prozent abgeklungen.
    const double tau = 100.0 * 100.0 / std::max(1.0F, std::fabs(st.followSpeed));
    const double fenster = 6.0 * tau;
    double t0 = std::max(st.followSinceMs, ms - fenster);
    float blick[3];
    {
        const CameraState anfang = g_app->camTrack.at(t0);
        float z0[3];
        if (t0 <= st.followSinceMs && st.followInitLerp) {
            for (int k = 0; k < 3; ++k) { blick[k] = anfang.angles[k] - ((k < 2) ? anfang.shakeAngles[k] : 0.0F); }
        } else if (followCentre(st.followGroup, t0, z0)) {
            aimAngles(anfang.pos, z0, blick);
        } else {
            for (int k = 0; k < 3; ++k) { blick[k] = anfang.angles[k]; }
        }
    }
    constexpr double kSchritt = 10.0;
    const auto frac = static_cast<float>(std::min(1.0, kSchritt / 100.0 * std::fabs(st.followSpeed) / 100.0));
    for (double t = t0 + kSchritt; t <= ms + 1.0e-6; t += kSchritt) {
        const CameraState jetzt = g_app->camTrack.at(t);
        float z[3];
        if (!followCentre(st.followGroup, t, z)) {
            break;
        }
        float soll[3];
        aimAngles(jetzt.pos, z, soll);
        for (int k = 0; k < 3; ++k) {
            const float d = std::fmod(soll[k] - blick[k] + 540.0F, 360.0F) - 180.0F;
            blick[k] += frac * d;
        }
    }
    for (int k = 0; k < 3; ++k) {
        st.angles[k] = blick[k];
    }
    // Das Zittern kommt NACH dem Zielen (CGCam_UpdateShake zuletzt).
    st.angles[0] += st.shakeAngles[0];
    st.angles[1] += st.shakeAngles[1];
    (void)ziel;
    return st;
}

// Was jede Figur GERADE tut, in einer Zeile.
//
// Gefragt: "ist das alles dokumentiert im Debug-Output?" - Nein, war es
// nicht. Das Protokoll sagte, WELCHES Skelett eine Figur bekommt, aber
// nichts darueber, was sie damit anstellt. Damit liess sich keine der
// Meldungen dieser Sitzung nachpruefen: "der Kopf bewegt sich nicht
// richtig", "die Transition geht nicht richtig, hab ich das Gefuehl".
//
// Ein Gefuehl ist kein Befund. Diese Zeile macht daraus Zahlen: Animation
// und Bild, der laufende Uebergang samt Anteil, der Kopfwinkel, das Blick-
// und das Watchtarget.
//
// Auf Zuruf, nicht dauernd: bei 62 Figuren und sechzig Bildern je Sekunde
// waeren das 3720 Zeilen in der Sekunde.
// Vorwaerts angekuendigt: der Auszug nennt den Mund mit, und die
// Umsetzung steht weiter unten bei den anderen Helfern der Ansicht.
std::string faceAnimFor(const std::string& figur, double ms);
const App::ActorAssets* hiltFor(const std::string& pfad);
// Griffe und Klingenbilder vorab holen - siehe die Fassung weiter unten.
void preloadSaberAssets();
const Md3Model* weaponModelFor(const std::string& pfad);

void dumpActorsAtPlayhead() {
    char kopf[160];
    std::snprintf(kopf, sizeof(kopf),
                  "--- Figuren bei %.0f ms (%.2f s) ---",
                  g_app->playMs, g_app->playMs / 1000.0);
    // Auch ins gewoehnliche Protokoll - siehe die Notiz beim Zeitknopf in
    // gui/app.cpp. Ein Knopf, dessen Wirkung nur im Detailprotokoll steht,
    // wirkt kaputt.
    diag::detail(kopf);
    diag::info(kopf);
    int gezeigt = 0;
    for (std::size_t ai = 0; ai < g_app->scene.actors.size(); ++ai) {
        const Actor& a = g_app->scene.actors[ai];
        const ActorState st = a.at(g_app->playMs);
        const App::Skeleton* sk =
            (ai < g_app->actorAssets.size()) ? g_app->actorAssets[ai].skeleton
                                             : nullptr;

        // Die Bildnummern ueber DENSELBEN Weg wie beim Zeichnen holen -
        // sonst protokolliert man etwas anderes, als man sieht, und der
        // Auszug wuerde eher in die Irre fuehren als helfen.
        int bild = -1;
        int vorher = -1;
        if (sk != nullptr) {
            bild = frameForAnimationIn(sk->sections, st.animation,
                                       g_app->playMs - st.animStartMs,
                                       st.holdAnim, st.animSpeed);
            vorher = frameForAnimationIn(sk->sections, st.prevAnimation,
                                         st.prevAnimSinceMs, st.prevHoldAnim);
        }
        const float l = blendFraction(st, g_app->playMs);

        std::string z = "  " + a.name;
        z += st.visible ? "" : (st.removed ? "  [entfernt]" : "  [unsichtbar]");
        z += "  Anim \"" + st.animation + "\" Bild " + std::to_string(bild);
        if (bild < 0) {
            z += " (dem Skelett unbekannt)";
        }
        // --- Woran man ein Stottern erkennt ---------------------------
        //
        // Gemeldet: "das Einlaufen von Mace stottert ein wenig."
        //
        // Ort und Animationswahl sind nachgemessen sauber. Was fehlt, ist
        // ein Blick auf den ABSCHNITT: wie lang er ist, wie schnell er
        // laeuft und WO im Zyklus die Figur gerade steht.
        //
        // Damit laesst sich der Verdacht pruefen, ohne zu raten. Bei Mace
        // hat BOTH_WALK1 im _humanoid_mace **30 Bilder bei 20 fps**, also
        // 1500 ms je Zyklus - er geht aber 2133 ms. Die Schleife springt
        // also EINMAL mitten im Gehen um, und genau dort waere ein Ruckeln
        // zu erwarten.
        if (sk != nullptr && !st.animation.empty()) {
            for (const AnimEntry& e : sk->sections) {
                if (e.name != st.animation) {
                    continue;
                }
                const double seit = g_app->playMs - st.animStartMs;
                const double zyklus =
                    static_cast<double>(e.numFrames) * 1000.0 /
                    static_cast<double>(std::max(e.fps, 1));
                char az[160];
                std::snprintf(az, sizeof(az),
                              " [%d Bilder ab %d, %d fps, Zyklus %.0f ms;"
                              " laeuft seit %.0f ms = %.2f Zyklen%s]",
                              e.numFrames, e.firstFrame, e.fps, zyklus,
                              seit, (zyklus > 0.0) ? seit / zyklus : 0.0,
                              (e.loopFrame < 0) ? ", EINMALIG" : "");
                z += az;
                break;
            }
        }
        if (l < 1.0F && !st.prevAnimation.empty()) {
            char u[128];
            std::snprintf(u, sizeof(u),
                          "  Uebergang von \"%s\" Bild %d, %.0f %% von %.0f ms",
                          st.prevAnimation.c_str(), vorher,
                          static_cast<double>(l) * 100.0, st.blendMs);
            z += u;
        } else {
            z += "  kein Uebergang";
        }
        char rest[192];
        std::snprintf(rest, sizeof(rest), "  Gier %.1f", st.angles[1]);
        z += rest;
        {
            const auto itn = g_app->npcMap.find(a.npcType);
            if (itn != g_app->npcMap.end() && !itn->second.saber.empty()) {
                z += "  Klinge \"" + itn->second.saber + "\"";
                std::string k = itn->second.saber;
                for (char& c : k) {
                    c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }
                const auto its = g_app->saberMap.find(k);
                if (its == g_app->saberMap.end()) {
                    z += " (nicht in den .sab-Dateien)";
                } else {
                    z += " -> " + its->second.model;
                    z += (hiltFor(its->second.model) != nullptr)
                             ? " (Griff geladen)"
                             : " (GRIFF NICHT GELADEN)";
                    z += st.saberActive ? "  Klinge AN" : "  Klinge aus";
                    // --- Wo die Klinge WIRKLICH sitzt --------------------
                    //
                    // Gemeldet: "das Lichtschwert sollte vor ihm sein."
                    //
                    // Die Kette ist gegen den Quelltext geprueft - Bolzen
                    // (tr_ghoul2.cpp:2080 ff.), Richtung (-x, ebenda:6114),
                    // kein Versatz bei SABER_SINGLE (ebenda:6731). Sie
                    // stimmt, soweit sich das lesen laesst.
                    //
                    // Was fehlt, sind ZAHLEN: wo landet der Bolzen, und wo
                    // steht die Figur? Erst der Vergleich der beiden sagt,
                    // ob das Schwert an der Hand haengt oder daneben.
                    //
                    // Alles in Figurenkoordinaten - VOR der Drehung um den
                    // Gierwinkel und der Verschiebung an den Ort. Der
                    // Bolzen soll bei einer stehenden Figur ungefaehr auf
                    // Huefthoehe und seitlich vom Rumpf liegen.
                    const App::ActorAssets* hg = hiltFor(its->second.model);
                    if (sk != nullptr && hg != nullptr && bild >= 0) {
                        std::vector<BoneMatrix> welt;
                        sk->anim.worldMatrices(
                            std::clamp(bild, 0, sk->anim.numFrames - 1), welt);
                        BoneMatrix hand{};
                        const GlmModel& figMdl = g_app->actorAssets[ai].model;
                        const int hs = surfaceIndex(figMdl, "*r_hand");
                        if (hs < 0) {
                            z += "  [KEIN *r_hand im Modell]";
                        } else if (!boltMatrix(figMdl, hs, welt,
                                               sk->anim, hand)) {
                            z += "  [Bolzen *r_hand nicht berechenbar]";
                        } else {
                            char bz[220];
                            std::snprintf(bz, sizeof(bz),
                                          "  Hand bei %.1f/%.1f/%.1f",
                                          hand.m[0][3], hand.m[1][3],
                                          hand.m[2][3]);
                            z += bz;
                            BoneMatrix kl{};
                            int bsx = surfaceIndex(hg->model, "*blade1");
                            if (bsx < 0) {
                                bsx = surfaceIndex(hg->model, "*flash");
                            }
                            if (boltMatrixRigid(hg->model, bsx, kl)) {
                                float wu[3] = {kl.m[0][3], kl.m[1][3],
                                               kl.m[2][3]};
                                float sp[3];
                                // Dieselbe Vorgabe wie beim Zeichnen:
                                // 32 (wp_saberLoad.cpp:364).
                                for (int c = 0; c < 3; ++c) {
                                    sp[c] = wu[c] - kl.m[c][0] * 32.0F;
                                }
                                float a1[3];
                                float b1[3];
                                hand.transform(wu, a1);
                                hand.transform(sp, b1);
                                std::snprintf(
                                    bz, sizeof(bz),
                                    "  Klinge von %.1f/%.1f/%.1f nach "
                                    "%.1f/%.1f/%.1f",
                                    a1[0], a1[1], a1[2], b1[0], b1[1], b1[2]);
                                z += bz;
                            } else {
                                z += "  [kein *blade1 im Griff]";
                            }
                        }
                    }
                }
            }
        }
        // --- Was die Figur gerade in den Haenden haelt ---------------
        //
        // Aus dem Zustand (src/ausruestung.cpp) - damit sich nachlesen
        // laesst, warum ein Griff fehlt oder eine Klinge nicht brennt.
        if (!st.waffe.empty()) {
            z += "  Waffe " + st.waffe;
            for (std::size_t n = 0; n < 2; ++n) {
                const HandBelegung& h = st.hand[n];
                if (h.modell >= 0 && static_cast<std::size_t>(h.modell) < a.modelle.size()) {
                    z += (n == 0) ? "  rechts " : "  links ";
                    z += a.modelle[static_cast<std::size_t>(h.modell)];
                    if (hiltFor(a.modelle[static_cast<std::size_t>(h.modell)]) == nullptr) {
                        z += " (NICHT GELADEN)";
                    }
                }
            }
            for (std::size_t n = 0; n < 2; ++n) {
                const SaberZustand& sz = st.saber[n];
                if (n == 1 && !st.dualSabers) {
                    break;
                }
                char kz[96];
                int an = 0;
                for (int b = 0; b < sz.numBlades && b < kMaxKlingen; ++b) {
                    if (sz.klinge[static_cast<std::size_t>(b)].an) { ++an; }
                }
                const std::string nm =
                    (sz.art >= 0 && static_cast<std::size_t>(sz.art) < a.saberArten.size())
                        ? a.saberArten[static_cast<std::size_t>(sz.art)].name
                        : std::string("-");
                std::snprintf(kz, sizeof(kz), "  Schwert%d \"%s\" %d/%d Klingen an",
                              static_cast<int>(n) + 1, nm.c_str(), an, sz.numBlades);
                z += kz;
            }
        }
        if (st.firing) {
            char fz[110];
            std::snprintf(fz, sizeof(fz),
                          "  SCHIESST (alle %.0f ms, BOTH_ATTACK3)",
                          static_cast<double>(st.shotSpacingMs));
            z += fz;
            // --- Und WOMIT, und wo die Muendung sitzt --------------------
            //
            // Gefragt: "wo bekommen wir das Projektil her?"
            //
            // Die Kette hat vier Glieder, und diese Zeile zeigt sie alle -
            // damit sich beantworten laesst, wo sie reisst, BEVOR
            // irgendetwas gezeichnet wird:
            //
            //   NPC_type -> .npc `weapon` -> weapons.dat -> Modell/Blitz
            //   -> Tag "tag_flash" im Modell
            const auto itw = g_app->npcMap.find(a.npcType);
            if (itw == g_app->npcMap.end() || itw->second.weapon.empty()) {
                z += "  [keine Waffe in der .npc]";
            } else {
                z += "  Waffe " + itw->second.weapon;
                const auto itd = g_app->weaponMap.find(itw->second.weapon);
                if (itd == g_app->weaponMap.end()) {
                    z += " (NICHT in weapons.dat)";
                } else {
                    z += itd->second.muzzleEffect.empty()
                             ? " (ohne Muendungsblitz)"
                             : (" -> " + itd->second.muzzleEffect);
                    // --- Zwei Wege zur Muendung, und welcher gilt -----
                    //
                    // Berichtigt: "SBD schiesst es aus der Hand, deshalb
                    // gibt es kein eigenes Waffenmodell."
                    //
                    // Also nicht immer tag_flash. Wird ein Waffenmodell
                    // gezeichnet, sitzt die Muendung in dessen Tag; sonst
                    // rechnet die Engine sie aus dem Entityursprung
                    // (CalcMuzzlePoint, g_weapon.cpp:449 ff.).
                    //
                    // Der Auszug nennt BEIDE, samt dem Weg - so laesst sich
                    // sehen, welcher greift und ob er plausibel liegt.
                    const Md3Model* wm = weaponModelFor(itd->second.model);
                    const Md3Tag* tf =
                        (wm != nullptr) ? wm->tag("tag_flash") : nullptr;
                    if (tf != nullptr) {
                        char tz[110];
                        std::snprintf(tz, sizeof(tz),
                                      "  Muendung am Modell %.1f/%.1f/%.1f",
                                      static_cast<double>(tf->origin[0]),
                                      static_cast<double>(tf->origin[1]),
                                      static_cast<double>(tf->origin[2]));
                        z += tz;
                    }
                    float mp[3] = {};
                    muzzlePoint(st.pos, st.angles[1], itw->second.weapon, mp);
                    char mz[130];
                    std::snprintf(mz, sizeof(mz),
                                  "  Muendung aus dem Ursprung "
                                  "%.0f/%.0f/%.0f%s",
                                  static_cast<double>(mp[0]),
                                  static_cast<double>(mp[1]),
                                  static_cast<double>(mp[2]),
                                  (tf == nullptr) ? " (gilt)" : "");
                    z += mz;
                }
            }
        }
        {
            const std::string mund = faceAnimFor(a.name, g_app->playMs);
            z += mund.empty() ? "  Mund still" : ("  Mund \"" + mund + "\"");
        }
        if (!st.torsoAnimation.empty()) {
            int tb = -1;
            if (sk != nullptr) {
                tb = frameForAnimationIn(sk->sections, st.torsoAnimation,
                                         g_app->playMs - st.torsoStartMs,
                                         st.torsoHold);
            }
            z += "  Oberkoerper \"" + st.torsoAnimation + "\" Bild " +
                 std::to_string(tb);
        }
        if (st.hasWatchTarget) {
            std::snprintf(rest, sizeof(rest), "  Watchtarget an (%.0f/%.0f)",
                          st.watchAt[0], st.watchAt[1]);
            z += rest;
        }
        if (st.hasLookTarget) {
            const float dx = st.lookAt[0] - st.pos[0];
            const float dy = st.lookAt[1] - st.pos[1];
            const float ziel =
                std::atan2(dy, dx) * 180.0F / 3.14159265F;
            const float roh = shortestAngleDelta(st.angles[1], ziel);
            const float kw =
                std::clamp(roh, kHeadYawClampMin, kHeadYawClampMax);
            std::snprintf(rest, sizeof(rest),
                          "  Blickziel (%.0f/%.0f), Ausschlag %.1f%s "
                          "-> Brust %.1f Hals %.1f Kopf %.1f",
                          st.lookAt[0], st.lookAt[1], kw,
                          (roh < kHeadYawClampMin || roh > kHeadYawClampMax)
                              ? " (am Anschlag)"
                              : "",
                          // Dieselbe Verteilung wie im Zeichner
                          // (mapview.cpp, kette[]) - der Auszug soll
                          // zeigen, was WIRKLICH auf die Knochen geht.
                          kw * 0.10F, kw * 0.30F, kw * 0.60F);
            z += rest;
        } else {
            z += "  kein Blickziel";
        }
        if (sk == nullptr) {
            z += "  [KEIN Skelett]";
        }
        diag::detail(z);
        ++gezeigt;
    }
    if (gezeigt == 0) {
        diag::detail("  (keine Figuren)");
    }
}

// Welche Mundanimation eine Figur gerade spielt, oder leer.
//
// Gemeldet: "face animationen fehlen noch."
//
// Der Weg, nachgelesen und nicht geraten:
//
//   1. Welcher Klang laeuft an DIESER Figur? Die Zeitleiste fuehrt je
//      affect eine Spur, und ihr Name ist der Figurenname. Der SPAETESTE
//      begonnene gewinnt - dieselbe Regel, nach der auch das Abspielen
//      beim Einschalten des Tons einsteigt.
//   2. Wie laut ist er an dieser Stelle? bhed::sound::voiceLevel, Zeile
//      fuer Zeile nach snd_dma.cpp:2333 ff.
//   3. Welche Animation gehoert zu dieser Stufe? cg_players.cpp:5191:
//          anim = FACE_TALK1 + VoiceVolume - 1;
//      und bei -1 (spricht, aber gerade still) FACE_TALK0
//      (ebenda:5200).
//
// Nicht nachgebaut: das Blinzeln und die Leerlaufmimik. Beide haengen in
// der Engine an Zufallszeitgebern (Q_flrand(4000,8000) fuer den Lidschlag,
// cg_players.cpp:5159) - etwas Zufaelliges in einer Vorschau, die man
// anhaelt und zurueckspult, waere kein Gewinn, sondern Unruhe.
std::string faceAnimFor(const std::string& figur, double ms) {
    const TimelineEvent* laufend = nullptr;
    for (const TimelineTrack& track : g_app->timeline.tracks) {
        if (track.entity != figur) {
            continue;
        }
        for (const TimelineEvent& e : track.events) {
            if (e.kind != TimelineEvent::Kind::Sound || e.sound.empty()) {
                continue;
            }
            if (e.startMs <= ms &&
                (laufend == nullptr || e.startMs > laufend->startMs)) {
                laufend = &e;
            }
        }
    }
    if (laufend == nullptr) {
        return {};
    }
    const sound::Sound* s = soundFor(laufend->sound);
    if (s == nullptr) {
        return {};
    }
    const int stufe = sound::voiceLevel(*s, ms - laufend->startMs);
    if (stufe == 0) {
        return {};        // spricht nicht (mehr)
    }
    if (stufe < 0) {
        return "FACE_TALK0";
    }
    return "FACE_TALK" + std::to_string(stufe);
}

// Was spielt eine Entity, die per `use` angesprochen wird?
//
// Gemeldet: "mir fehlen noch Sounds, Lichtschwerter und Musik. Nur die
// Stimmen gehen bisher."
//
// Die Stimmen sind die einzigen, die als `sound`-Befehl IM SKRIPT stehen -
// 22 Stueck in der ganzen Mission. Alles andere haengt an Entities der
// KARTE, die das Skript mit `use` anspricht:
//
//   target_play_music   Schluessel "music"  -> g_target.cpp:1225 f.
//                       target_play_music_use setzt CS_MUSIC, und
//                       CG_StartMusic (cg_main.cpp:2953) spielt es ab.
//   target_speaker      Schluessel "noise"  -> g_target.cpp:243
//
// In md_ga_jedi sind das sieben Musikstuecke (mus1..mus5, mus_boss,
// mus_bossintro) und zwei Lautsprecher (hammer_sound, march1). Die
// Skripte rufen mus1, mus2, mus3, mus5 und march1 auf.
//
// Andere Ziele - func_door, trigger_once, target_autosave, fx_runner -
// geben keinen Klang her und liefern hier leer.
// Welchen Klang macht die Klinge dieser Figur beim Ein- oder Ausschalten?
//
// Die ganze Kette an einer Stelle, damit sie nachvollziehbar bleibt:
//
//   Figur          -> NPC_type aus dem NPC_spawner der Karte
//   -> .npc-Datei  -> Schluessel `saber`
//   -> .sab-Eintrag-> soundOn / soundOff
//
// Reisst die Kette irgendwo, gelten die Vorgaben aus
// wp_saberLoad.cpp:368 ff. Das ist kein Notbehelf, sondern das, was die
// Engine tut: Saber_SetDefaults setzt sie, BEVOR die .sab-Datei gelesen
// wird, und was dort fehlt, bleibt stehen. Gemessen an den 28 .sab-Dateien
// aus MD_Hilts und assets1: 254 Eintraege, 9 davon ohne eigenes soundOn.
// Die Farbe einer Klinge aus dem Namen in der .sab-Datei.
//
// Gezaehlt ueber die 31 .sab-Dateien: rot 68, random 64, blau 52, gruen 31,
// lila 13, gelb 12, dazu je zwei bis drei orange, weiss, schwarz und
// unstable_red.
//
// "random" waehlt die Engine beim Spawnen zufaellig (SABER_RED bis
// SABER_PURPLE). In einer Vorschau, die man anhaelt und zurueckspult, waere
// Zufall Unruhe statt Hilfe - deshalb blau, die haeufigste feste Farbe.
// Die beiden Bilder einer Klinge, nach ihrer Farbe.
//
// Gemeldet: "besser, aber noch nicht perfekt."
//
// Die Klinge war ein Rechteck in einer Farbe. In der Engine sind es zwei
// TEXTUREN, und welche, entscheidet die Farbe (cg_players.cpp:5763 ff.):
//
//     case SABER_BLUE:
//         glow  = cgs.media.blueSaberGlowShader;
//         blade = cgs.media.blueSaberCoreShader;
//
// Die Shader dahinter zeigen auf gfx/effects/sabers/<farbe>_glow2 und
// <farbe>_line. Nachgemessen an den echten Dateien aus assets1:
//
//   blue_glow2  128x128, quer 0 .. 140 .. 0 - ein weicher Verlauf, und der
//               Spitzenwert ist nur 140 von 255
//   blue_line    64x256, quer 0 .. 255 .. 0 mit breitem Plateau, LAENGS
//               durchgehend 252 - ein Band mit weichen Raendern
//
// Diese Verlaeufe nachzurechnen hiesse raten. Die Bilder liegen vor.
//
// Vorhanden sind sie fuer rot, orange, gelb, gruen, blau und lila - genau
// die sechs, die SABER_RED..SABER_PURPLE aufzaehlen. Alles andere (weiss,
// schwarz, unstable_red) faellt auf blau zurueck, so wie es auch in
// CG_DoSaber keinen eigenen Zweig bekaeme.
void meldeNachladen(const char* was, const std::string& name, double ms);

// Die Fernebene aus der Kartengroesse, nicht fest.
//
// 20000 war zu knapp: duel_kamino_lp misst 34816 Einheiten in der Breite und
// hat eine Diagonale von 48229. Wer von einer Ecke zur anderen schaut,
// verliert alles dahinter. Der GPU-Weg hatte die feste Zahl noch.
float kartenFern() {
    float diag = 0.0F;
    for (int k = 0; k < 3; ++k) {
        const float d = g_app->geo.maxs[k] - g_app->geo.mins[k];
        diag += d * d;
    }
    return std::max(std::sqrt(diag) * 1.2F, 8000.0F);
}

const TextureSet::Tex* bladeTexture(const std::string& farbe, bool glow) {
    static const char* const kBekannt[] = {"red",  "orange", "yellow",
                                           "green", "blue",  "purple"};
    std::string k = farbe;
    for (char& c : k) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::string gewaehlt = "blue";
    for (const char* n : kBekannt) {
        if (k.find(n) != std::string::npos) {
            gewaehlt = n;
            break;
        }
    }
    // Eine RGB-Farbe ("xff3f00"): die Engine faerbt dafuer ein eigenes Bild
    // ein (rgbSaberGlowShader, cg_players.cpp:7691). Das gibt es hier nicht;
    // das NAECHSTE der sechs Bilder ist naeher an der Wahrheit als blau.
    float rgb[3];
    if (saberFarbeRgb(farbe, rgb)) {
        static constexpr float kBezug[6][3] = {{1.0F, 0.0F, 0.0F}, {1.0F, 0.5F, 0.0F},
                                               {1.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
                                               {0.0F, 0.0F, 1.0F}, {0.5F, 0.0F, 1.0F}};
        float best = 1.0e9F;
        for (int i = 0; i < 6; ++i) {
            float d = 0.0F;
            for (int c = 0; c < 3; ++c) {
                const float x = rgb[c] - kBezug[i][c];
                d += x * x;
            }
            if (d < best) {
                best = d;
                gewaehlt = kBekannt[i];
            }
        }
    }
    const std::string pfad = "gfx/effects/sabers/" + gewaehlt +
                             (glow ? "_glow2" : "_line");
    const auto have = g_app->texCache.find(pfad);
    if (have != g_app->texCache.end()) {
        return have->second.empty() ? nullptr : &have->second;
    }
    // Ab hier wird MITTEN IM BILD geladen - siehe meldeNachladen.
    const auto ladeBeginn = std::chrono::steady_clock::now();
    TextureSet::Tex t;
    for (const std::string& versuch : textureCandidates(pfad)) {
        std::string data;
        if (!readFromArchives(versuch, data)) {
            continue;
        }
        const image::Image im = image::decode(
            reinterpret_cast<const unsigned char*>(data.data()), data.size());
        if (!im.ok || im.width <= 0 || im.height <= 0) {
            continue;
        }
        t = shrink(im, 256);
        break;
    }
    if (t.empty()) {
        diag::detail("Klingenbild " + pfad + " nicht gefunden");
    }
    // AUCH den Fehlschlag merken - sonst wird je Bild erneut gesucht.
    const auto put = g_app->texCache.emplace(pfad, std::move(t));
    meldeNachladen(
        "Klingenbild", pfad,
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - ladeBeginn).count());
    return put.first->second.empty() ? nullptr : &put.first->second;
}

void bladeColorFor(const std::string& name, std::uint8_t out[3]) {
    struct Eintrag {
        const char* name;
        std::uint8_t rgb[3];
    };
    static constexpr Eintrag kFarben[] = {
        {"red", {255, 70, 60}},      {"orange", {255, 150, 60}},
        {"yellow", {255, 235, 90}},  {"green", {90, 255, 110}},
        {"blue", {110, 180, 255}},   {"purple", {200, 120, 255}},
        {"white", {245, 245, 255}},  {"black", {90, 60, 130}},
    };
    std::string k = name;
    for (char& c : k) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // RGB-Farben ("xff3f00") und Farbtoene (Q_parseSaberColor).
    float rgb[3];
    if (saberFarbeRgb(name, rgb)) {
        for (int c = 0; c < 3; ++c) {
            out[c] = static_cast<std::uint8_t>(std::clamp(rgb[c], 0.0F, 1.0F) * 255.0F + 0.5F);
        }
        return;
    }
    // "unstable_red" und Aehnliches: der Farbname steht vorn.
    for (const Eintrag& e : kFarben) {
        if (k.find(e.name) != std::string::npos) {
            out[0] = e.rgb[0];
            out[1] = e.rgb[1];
            out[2] = e.rgb[2];
            return;
        }
    }
    out[0] = 110;
    out[1] = 180;
    out[2] = 255;
}

std::string saberSoundFor(const Actor& a, bool an, double ms) {
    // Aus dem ZUSTAND zur Zeit des Befehls, nicht mehr aus der .npc: ein
    // SET_SABER1 vorher hat das Schwert - und damit seine Klaenge - schon
    // getauscht. Die Vorgaben aus wp_saberLoad.cpp:368 ff. stecken bereits
    // in der SaberArt (saberArtIndex).
    const ActorState st = a.at(ms);
    // Haelt die Figur kein Schwert, laeuft SET_SABERACTIVE in der Engine
    // ins Leere (Q3_SetSaberActive: "is not using a saber!") - also auch
    // kein Klang.
    if (!haeltSaber(st)) {
        return {};
    }
    const int art = st.saber[0].art;
    if (art >= 0 && static_cast<std::size_t>(art) < a.saberArten.size()) {
        const SaberArt& d = a.saberArten[static_cast<std::size_t>(art)];
        return an ? d.soundOn : d.soundOff;
    }
    // Die Nullstruktur (kein `saber` in der .npc) hat keine Klinge, die
    // zuenden koennte - und keinen Klang.
    return {};
}

std::string useSoundFor(const std::string& ziel, bool* istMusik) {
    if (istMusik != nullptr) {
        *istMusik = false;
    }
    if (ziel.empty()) {
        return {};
    }
    const auto gleich = [](const std::string& a, const std::string& b) {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    };
    for (const MapEntity& e : g_app->map.entities) {
        // G_Find vergleicht mit Q_stricmp - ohne Gross/klein.
        if (!gleich(e.targetname, ziel)) {
            continue;
        }
        // Der Schluessel entscheidet, nicht der Klassenname: eine Karte
        // darf beides tragen, und wer nach der Klasse geht, verpasst
        // Sonderfaelle wie target_speaker mit music-Schluessel.
        //
        // Und der Schluessel sagt auch, WOHIN: Musik ist ein eigener Strom
        // und gehoert auf das Musikgeraet, ein Lautsprecher nicht.
        if (const std::string* m = e.find("music")) {
            if (!m->empty()) {
                if (istMusik != nullptr) { *istMusik = true; }
                return *m;
            }
        }
        if (const std::string* n = e.find("noise")) {
            if (!n->empty() && *n != "*NOSOUND*") { return *n; }
        }
    }
    return {};
}

// --- Klaenge am Ort, wie die Engine sie hoert ------------------------------
//
// Der Hoerer ist die Kamera (im Kameramodus setzt die Engine listener_origin
// auf cg.refdef.vieworg). Rechts ist die Achse aus dem Gierwinkel - Quake
// dreht mit +y nach links, also rechts = (sin gier, -cos gier, 0).
namespace {
void hoerer(float ort[3], float rechts[3]) {
    for (int k = 0; k < 3; ++k) {
        ort[k] = g_app->cam.pos[k];
    }
    const float gier = g_app->cam.angles[1] * 3.14159265F / 180.0F;
    rechts[0] = std::sin(gier);
    rechts[1] = -std::cos(gier);
    rechts[2] = 0.0F;
}

std::uint64_t kanalSchluessel(const std::string& wer, sound::Kanal k) {
    if (k == sound::Kanal::Auto || k == sound::Kanal::LessAtten) {
        return 0;   // CHAN_AUTO sucht sich einen freien Kanal
    }
    // Die drei Stimmkanaele schneiden einander ab (S_CheckChannelStomp).
    const int gruppe = (k == sound::Kanal::Voice || k == sound::Kanal::VoiceAtten ||
                        k == sound::Kanal::VoiceGlobal)
                           ? 1
                           : 10 + static_cast<int>(k);
    std::uint64_t h = 1469598103934665603ULL;
    for (const char c : wer) {
        h = (h ^ static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(c)))) * 1099511628211ULL;
    }
    return (h ^ static_cast<std::uint64_t>(gruppe) * 0x9E3779B97F4A7C15ULL) | 1ULL;
}
}  // namespace

// Ein sound-Befehl aus dem Skript: auf der Entity `wer` (leer = der Traeger
// des Skripts) im Kanal `kanal`.
//
// PlayIcarusSound (Q3_Interface.cpp:8435): Stimmkanaele gehen ueber
// G_SoundOnEnt an den Ort der Entity; ein target_scriptrunner und
// CHAN_ANNOUNCER senden "broadcast" - ueberall voll zu hoeren. Der Spieler
// ist die Hoerer-Entity (listener_number) und damit auch immer voll.
void spieleSkriptKlang(const std::string& werSpur, const std::string& datei, const std::string& kanalName,
                       double ms) {
    if (datei.empty()) {
        return;
    }
    const sound::Kanal k = sound::kanalAusName(kanalName);
    const std::string wer = werSpur.empty() ? g_app->ablauf.traeger : werSpur;
    const auto klein = [](std::string x) {
        for (char& c : x) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        return x;
    };
    bool voll = (k == sound::Kanal::Announcer || k == sound::Kanal::VoiceGlobal || klein(wer) == "player");
    float ort[3] = {0.0F, 0.0F, 0.0F};
    bool ortDa = false;
    if (!voll) {
        if (const Actor* a = g_app->scene.find(wer); a != nullptr) {
            const ActorState st = a->at(ms);
            for (int i = 0; i < 3; ++i) { ort[i] = st.pos[i]; }
            ortDa = true;
        } else {
            for (const MapEntity& e : g_app->map.entities) {
                const std::string* st = e.find("script_targetname");
                const bool trifft = (st != nullptr && klein(*st) == klein(wer)) || klein(e.targetname) == klein(wer);
                if (!trifft) {
                    continue;
                }
                if (e.classname == "target_scriptrunner") {
                    voll = true;   // bBroadcast
                } else if (!e.origin.empty()) {
                    std::istringstream is(e.origin);
                    is >> ort[0] >> ort[1] >> ort[2];
                    ortDa = true;
                }
                break;
            }
        }
        // Ohne bekannten Ort (Skript ohne Karte, Traeger unbekannt): voll.
        if (!ortDa) {
            voll = true;
        }
    }
    sound::Raumlaut r;
    if (!voll) {
        float ohr[3];
        float rechts[3];
        hoerer(ohr, rechts);
        r = sound::raumlaut(ort, ohr, rechts, k);
    }
    playSound(datei, 0.0, false, r.links, r.rechts, kanalSchluessel(wer, k));
}

std::string weltMusik() {
    for (const MapEntity& e : g_app->map.entities) {
        if (e.classname == "worldspawn") {
            const std::string* m = e.find("music");
            return (m == nullptr) ? std::string{} : *m;
        }
    }
    return {};
}

// Ein target_speaker, benutzt: am Ort des Lautsprechers, ausser mit
// spawnflag 4 (GLOBAL, EV_GLOBAL_SOUND) - Use_Target_Speaker,
// g_target.cpp:171.
void spieleLautsprecher(const std::string& name, const std::string& datei) {
    for (const MapEntity& e : g_app->map.entities) {
        if (e.targetname.size() != name.size() ||
            !std::equal(e.targetname.begin(), e.targetname.end(), name.begin(), [](char x, char y) {
                return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
            })) {
            continue;
        }
        const std::string* sf = e.find("spawnflags");
        const int flags = (sf == nullptr) ? 0 : std::atoi(sf->c_str());
        if ((flags & 4) != 0 || e.origin.empty()) {
            playSound(datei);
            return;
        }
        float ort[3] = {0.0F, 0.0F, 0.0F};
        std::istringstream is(e.origin);
        is >> ort[0] >> ort[1] >> ort[2];
        float ohr[3];
        float rechts[3];
        hoerer(ohr, rechts);
        const sound::Raumlaut r = sound::raumlaut(ort, ohr, rechts, sound::Kanal::Auto);
        playSound(datei, 0.0, false, r.links, r.rechts, 0);
        return;
    }
    playSound(datei);
}

// Das Modell eines Schwertgriffs, gepuffert.
//
// Eigener Puffer neben modelCache: dort liegen Figuren, die immer unter
// models/players/<name>/model.glm stehen. Ein Griff wird in der .sab-Datei
// mit vollem Pfad genannt (models/weapons2/saber_plasma/saber_aayla.glm),
// und viele Figuren teilen sich denselben.
// Ein Waffenmodell gepuffert holen - wegen seines Tags "tag_flash".
//
// Gefragt: "wo bekommen wir das Projektil her?" Von dort: die Muendung ist
// ein Tag im .md3 der Waffe. An models/weapons2/blaster_r/blaster.md3
// nachgemessen: zwei Tags, "tag_flash" bei -1.3/-11.7/-3.9.
//
// Ueber die 74 .md3 unter models/weapons2 in assets1 gezaehlt: 54 haben
// einen tag_flash, 20 nicht - letztere sind Halterungen und Einzelteile.
// Die Effekte einer Waffe: Muendungsblitz und Geschoss.
//
// Der BLITZ steht in weapons.dat (`muzzleEffect`). Das GESCHOSS nicht - es
// steht in C++, je Waffe ein RegisterEffect (cg_weapons.cpp:401). Aus den
// Daten allein ist es also nicht zu erfahren.
//
// Was sich ableiten laesst, ist der VORSATZ: aus "blaster/muzzle_flash"
// wird "blaster". Nachgezaehlt ueber die 22 Waffen mit Muendungsblitz und
// die 376 .efx aus assets1: bei 16 gibt es unter demselben Vorsatz ein
// shot oder npcshot, bei 6 nicht (Disruptor, Repeater, DEMP2, Thermal,
// Stolperdraht, Sprengpaket - deren Geschosse werden anders gezeichnet
// oder sind gar keine).
//
// NPCs bekommen dabei ein ANDERES als der Spieler. FX_Blaster.cpp:71 ff.:
//
//     if ( cent->gent->owner->s.number > 0 )
//         theFxScheduler.PlayEffect( "blaster/NPCshot", ... );
//     else
//         theFxScheduler.PlayEffect( cgs.effects.blasterShotEffect, ... );
//
// Also erst npcshot, dann shot. Findet sich keines, wird nichts gezeichnet
// - und das Protokoll sagt es.
struct WeaponFx {
    const efx::Effect* muzzle = nullptr;
    const efx::Effect* shot = nullptr;
    const efx::Effect* impact = nullptr;
};

const efx::Effect* effectByName(const std::string& name) {
    if (name.empty()) {
        return nullptr;
    }
    const std::string pfad = "effects/" + name + ".efx";
    const auto have = g_app->effects.find(pfad);
    if (have != g_app->effects.end()) {
        return have->second.primitives.empty() ? nullptr : &have->second;
    }
    // Ab hier wird MITTEN IM BILD geladen - siehe meldeNachladen.
    const auto ladeBeginn = std::chrono::steady_clock::now();
    std::string text;
    efx::Effect leer;
    if (readFromArchives(pfad, text)) {
        const efx::ReadResult r = efx::read(text);
        if (!r.hasErrors()) {
            leer = r.effect;
        } else {
            diag::detail("Effekt " + pfad + " nicht lesbar");
        }
    }
    // AUCH den Fehlschlag merken, sonst wird je Bild erneut gesucht.
    const auto put = g_app->effects.emplace(pfad, std::move(leer));
    meldeNachladen(
        "Effektdatei", pfad,
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - ladeBeginn).count());
    return put.first->second.primitives.empty() ? nullptr
                                                : &put.first->second;
}

// Alle Schuesse, die zu diesem Zeitpunkt zu sehen sind.
//
// Zwei Dinge je Schuss: der Muendungsblitz, der nur wenige Bilder lebt und
// an der Muendung bleibt, und das GESCHOSS, das fliegt.
//
// Ein EffectInstance hat einen festen Ort. Die Engine spielt den
// Schusseffekt aber in JEDEM Bild neu, an der aktuellen Stelle des
// Geschosses (FX_BlasterProjectileThink, FX_Blaster.cpp:37 ff.) - der
// "Strahl" ist gar kein bleibendes Ding, sondern ein Line-Primitiv, das
// mitwandert. Genau so wird es hier gemacht: die Stelle wird aus der
// Flugzeit gerechnet, und der Effekt beginnt JETZT.
//
// BLASTER_VELOCITY ist 2300 (weapons.h:180).
std::vector<EffectInstance> shotEffectsAt(double nowMs);

WeaponFx weaponFxFor(const std::string& typ) {
    WeaponFx out;
    const auto it = g_app->weaponMap.find(typ);
    if (it == g_app->weaponMap.end() || it->second.muzzleEffect.empty()) {
        return out;
    }
    out.muzzle = effectByName(it->second.muzzleEffect);
    const std::size_t slash = it->second.muzzleEffect.find('/');
    if (slash != std::string::npos) {
        const std::string vorsatz = it->second.muzzleEffect.substr(0, slash);
        out.shot = effectByName(vorsatz + "/npcshot");
        if (out.shot == nullptr) {
            out.shot = effectByName(vorsatz + "/shot");
        }
        // Der EINSCHLAG. In der Engine haengt er am Aufschlag des
        // Geschosses (FX_BlasterProjectileThink -> die Wand), und der
        // Effekt heisst durchweg "<vorsatz>/wall_impact".
        out.impact = effectByName(vorsatz + "/wall_impact");
    }
    return out;
}

std::vector<EffectInstance> shotEffectsAt(double nowMs) {
    std::vector<EffectInstance> out;
    if (!g_app->showEffects) {
        return out;
    }
    constexpr float kBlasterVelocity = 2300.0F;   // weapons.h:180
    // Wie lange ein Muendungsblitz zu sehen ist. Aus muzzle_flash.efx:
    // "life 30" - dreissig Millisekunden, also ein bis zwei Bilder.
    constexpr double kBlitzMs = 30.0;
    // Wie weit ein Geschoss verfolgt wird. Bei 2300 Einheiten je Sekunde
    // sind zwei Sekunden 4600 Einheiten - laenger als jede Sichtlinie in
    // diesen Karten, und eine Grenze muss es geben.
    constexpr double kFlugMs = 2000.0;

    for (std::size_t ai = 0; ai < g_app->scene.actors.size(); ++ai) {
        const Actor& a = g_app->scene.actors[ai];
        const ActorState st = a.at(nowMs);
        if (!st.firing || st.removed) {
            continue;
        }
        // Die AKTUELLE Waffe (SET_WEAPON), nicht die aus der .npc - ohne
        // .npc bleibt es wie bisher bei keiner.
        const std::string& waffe = st.waffe;
        if (waffe.empty()) {
            continue;
        }
        const WeaponFx fx = weaponFxFor(waffe);
        if (fx.muzzle == nullptr && fx.shot == nullptr) {
            continue;
        }

        // Die Muendung und die Richtung.
        float mp[3] = {};
        muzzlePoint(st.pos, st.angles[1], waffe, mp);
        const float r = st.angles[1] * 3.14159265358979F / 180.0F;
        float vorn[3] = {std::cos(r), std::sin(r), 0.0F};
        // Zielt die Figur auf etwas, fliegt es dorthin. SET_WATCHTARGET
        // steht bei den Droiden dieser Mission auf "win2" - ohne das floege
        // jeder Schuss geradeaus, egal wohin die Figur schaut.
        if (st.hasWatchTarget) {
            const float dx = st.watchAt[0] - mp[0];
            const float dy = st.watchAt[1] - mp[1];
            const float dz = st.watchAt[2] + 30.0F - mp[2];
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len > 1.0F) {
                vorn[0] = dx / len;
                vorn[1] = dy / len;
                vorn[2] = dz / len;
            }
        }

        // Welche Schuesse liegen zurueck? Der erste faellt auf den Beginn
        // des Feuerns, die weiteren im Abstand von shotSpacingMs.
        const double takt = std::max(static_cast<double>(st.shotSpacingMs),
                                     1.0);
        const double seit = nowMs - st.firingSinceMs;
        if (seit < 0.0) {
            continue;
        }
        // --- Wo trifft der Schuss auf? ------------------------------
        //
        // EINMAL je Figur, nicht je Schuss: alle Schuesse dieser Figur
        // fliegen dieselbe Bahn, solange sie steht. Je Schuss waere es bei
        // drei Droiden und drei Schuessen in der Luft neunmal derselbe
        // Strahl durch 980 Blaetter.
        //
        // Die Strecke ist so lang wie die Flugzeit reicht: bei 2300
        // Einheiten je Sekunde und zwei Sekunden sind das 4600.
        float treffer[3] = {mp[0] + vorn[0] * 4600.0F,
                            mp[1] + vorn[1] * 4600.0F,
                            mp[2] + vorn[2] * 4600.0F};
        float wegBisWand = 4600.0F;
        bool trifft = false;
        if (!g_app->geo.empty()) {
            const TraceTreffer t = traceRay(g_app->geo, mp, treffer);
            if (t.hit) {
                trifft = true;
                for (int k = 0; k < 3; ++k) {
                    // Ein Stueck VOR die Wand, sonst steckt der Effekt zur
                    // Haelfte darin und die halben Teilchen verschwinden.
                    treffer[k] = t.point[k] + t.normal[k] * 2.0F;
                }
                wegBisWand = 4600.0F * t.fraction;
            }
        }
        // Wie lange fliegt das Geschoss bis dahin?
        const double flugMs =
            static_cast<double>(wegBisWand) /
            static_cast<double>(kBlasterVelocity) * 1000.0;

        const auto letzter = static_cast<long long>(seit / takt);
        for (long long n = letzter; n >= 0; --n) {
            const double alter = seit - static_cast<double>(n) * takt;
            if (alter > kFlugMs) {
                break;
            }
            if (fx.muzzle != nullptr && alter <= kBlitzMs) {
                EffectInstance b;
                b.effect = fx.muzzle;
                for (int k = 0; k < 3; ++k) {
                    b.origin[k] = mp[k];
                }
                b.startMs = nowMs - alter;
                b.seed = static_cast<unsigned>(ai * 31 + n);
                out.push_back(b);
            }
            // --- Der EINSCHLAG ------------------------------------
            //
            // Sobald das Geschoss die Wand erreicht hat, hoert es auf zu
            // fliegen und wall_impact.efx spielt an der Trefferstelle.
            //
            // Das Fenster ist die Lebensdauer des Einschlageffekts, grob
            // gegriffen: laenger als ein paar hundert Millisekunden lebt
            // keiner von ihnen.
            if (trifft && alter >= flugMs && alter < flugMs + 400.0) {
                if (fx.impact != nullptr) {
                    EffectInstance ei;
                    ei.effect = fx.impact;
                    for (int k = 0; k < 3; ++k) {
                        ei.origin[k] = treffer[k];
                    }
                    ei.startMs = nowMs - (alter - flugMs);
                    ei.seed = static_cast<unsigned>(ai * 31 + n);
                    out.push_back(ei);
                }
                continue;   // das Geschoss selbst ist weg
            }
            if (trifft && alter >= flugMs) {
                continue;   // laengst eingeschlagen
            }
            if (fx.shot != nullptr) {
                EffectInstance g;
                g.effect = fx.shot;
                const auto weg = static_cast<float>(
                    alter / 1000.0 * static_cast<double>(kBlasterVelocity));
                for (int k = 0; k < 3; ++k) {
                    g.origin[k] = mp[k] + vorn[k] * weg;
                }
                // Der Effekt beginnt JETZT: er ist kein bleibendes Ding,
                // sondern wird je Bild neu an der Stelle des Geschosses
                // gespielt (FX_Blaster.cpp:37 ff.).
                g.startMs = nowMs;
                g.seed = static_cast<unsigned>(ai * 31 + n);
                out.push_back(g);
            }
        }
    }
    return out;
}

const Md3Model* weaponModelFor(const std::string& pfad) {
    if (pfad.empty()) {
        return nullptr;
    }
    const auto have = g_app->weaponModels.find(pfad);
    if (have != g_app->weaponModels.end()) {
        return have->second.empty() && have->second.numTags == 0
                   ? nullptr
                   : &have->second;
    }
    Md3Model m;
    std::string data;
    if (readFromArchives(pfad, data)) {
        std::string err;
        if (!readMd3(data, m, &err)) {
            diag::detail("Waffenmodell " + pfad + " nicht lesbar: " + err);
            m = Md3Model{};
        }
    } else {
        diag::detail("Waffenmodell " + pfad + " nicht gefunden");
    }
    // AUCH den Fehlschlag merken, sonst wird je Bild erneut gesucht.
    const auto put = g_app->weaponModels.emplace(pfad, std::move(m));
    return put.first->second.empty() && put.first->second.numTags == 0
               ? nullptr
               : &put.first->second;
}

// --- Wenn mitten im Bild geladen wird, soll es dastehen ------------------
//
// Aus deinem Protokoll:
//
//     Zeit je Bild 51.6 ms: ... Figuren 23.1, Szene 13.3 ... spielt
//     Zeit je Bild 35.4 ms: ... Figuren  9.2, Szene 13.9 ... spielt
//
// Zweimal waehrend des Abspielens kostet der Szeneabschnitt dreizehn
// Millisekunden statt einem Zehntel. Dort stehen die verzoegerten
// Ladevorgaenge: Schwertgriffe, Klingentexturen, Effektdateien. Sie werden
// beim ERSTEN Gebrauch geholt - und der faellt mitten in ein Bild.
//
// Das ist kein Fehler, sondern eine Entscheidung: alles vorab zu laden
// kostet beim Missionsstart Zeit fuer Dinge, die vielleicht nie
// vorkommen. Aber es soll im Protokoll STEHEN, sonst sucht man den
// Ausreisser bei der Bildrate statt beim Laden.
void meldeNachladen(const char* was, const std::string& name, double ms) {
    if (ms < 1.0) {
        return;   // unter einer Millisekunde ist es kein Ausreisser
    }
    char z[220];
    std::snprintf(z, sizeof(z),
                  "Nachgeladen mitten im Bild: %s \"%s\" - %.1f ms%s",
                  was, name.c_str(), ms,
                  g_app->playing ? " (waehrend des Abspielens)" : "");
    diag::detail(z);
}

const App::ActorAssets* hiltFor(const std::string& pfad) {
    if (pfad.empty()) {
        return nullptr;
    }
    const auto have = g_app->hiltCache.find(pfad);
    if (have != g_app->hiltCache.end()) {
        return have->second.model.empty() ? nullptr : &have->second;
    }
    // Ab hier wird MITTEN IM BILD geladen - siehe meldeNachladen.
    const auto ladeBeginn = std::chrono::steady_clock::now();
    App::ActorAssets a;
    std::string data;
    if (readFromArchives(pfad, data)) {
        std::string err;
        if (!readGlm(data, a.model, &err)) {
            diag::detail("Griff " + pfad + " nicht lesbar: " + err);
            a.model = GlmModel{};
        } else {
            // Die Haut liegt neben dem Modell, nicht darin.
            const std::size_t slash = pfad.find_last_of('/');
            if (slash != std::string::npos) {
                const std::string dir = pfad.substr(0, slash);
                std::string skin;
                if (readFromArchives(dir + "/model_default.skin", skin)) {
                    applySkin(skin, a.model);
                }
            }
            a.textures = texturesFor(a.model);
        }
    } else {
        diag::detail("Griff " + pfad + " nicht gefunden");
    }
    // AUCH den Fehlschlag merken, sonst wird je Bild erneut gesucht.
    const auto put = g_app->hiltCache.emplace(pfad, std::move(a));
    meldeNachladen(
        "Schwertgriff", pfad,
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - ladeBeginn).count());
    return put.first->second.model.empty() ? nullptr : &put.first->second;
}

// Was die Figur in den Haenden haelt, fuer dieses Bild.
//
// Ohne bekannte Waffe (keine .npc zur Figur) bleibt die Hand leer - so war
// es vorher auch: ohne .npc gab es keinen Griff.
void anbautenFuer(const Actor& a, const ActorState& st, double ms, ActorDraw& d) {
    static_assert(kAnbauKlingen == kMaxKlingen,
                  "mapview.h und scene.h muessen dieselbe Klingenzahl haben");
    static_assert(kAnbauMax >= 2 + kMaxKinoModelle,
                  "zwei Hand-Faecher und alle Kinomodelle muessen passen");
    d.anbauten = 0;
    const auto pfad = [&a](int m) -> const std::string* {
        if (m < 0 || static_cast<std::size_t>(m) >= a.modelle.size()) {
            return nullptr;
        }
        return &a.modelle[static_cast<std::size_t>(m)];
    };
    const auto neu = [&d](const App::ActorAssets* hg, const char* bolzen) -> AnbauDraw* {
        if (hg == nullptr || d.anbauten >= kAnbauMax) {
            return nullptr;
        }
        AnbauDraw& an = d.anbau[static_cast<std::size_t>(d.anbauten)];
        ++d.anbauten;
        an = AnbauDraw{};
        an.model = &hg->model;
        an.textures = &hg->textures;
        an.bolzen = bolzen;
        return &an;
    };
    if (!st.waffe.empty()) {
        for (std::size_t n = 0; n < 2; ++n) {
            const HandBelegung& h = st.hand[n];
            const std::string* p = pfad(h.modell);
            if (p == nullptr) {
                continue;
            }
            const SaberZustand& s = st.saber[n];
            const SaberArt* art =
                (h.istSaber && s.art >= 0 &&
                 static_cast<std::size_t>(s.art) < a.saberArten.size())
                    ? &a.saberArten[static_cast<std::size_t>(s.art)]
                    : nullptr;
            // boltToWrist haengt den Griff ans Handgelenk
            // (wp_saber.cpp:858 ff.).
            const bool gelenk = art != nullptr && art->handgelenk;
            const char* bolzen = (n == 0) ? (gelenk ? "*r_hand_cap_r_arm" : "*r_hand")
                                          : (gelenk ? "*l_hand_cap_l_arm" : "*l_hand");
            AnbauDraw* an = neu(hiltFor(*p), bolzen);
            if (an == nullptr || !h.istSaber) {
                continue;
            }
            // Klingen nur mit dem Schwert in der Hand, saber[1] nur mit
            // dualSabers (cg_players.cpp:16200 ff.), und keine beim
            // Sith-Schwert (ebenda:14556).
            if (!haeltSaber(st) || (n == 1 && !st.dualSabers)) {
                continue;
            }
            if (art != nullptr && art->typ == "SABER_SITH_SWORD") {
                continue;
            }
            an->lage = klingenLageFuer(art != nullptr ? art->typ : std::string());
            for (int b = 0; b < s.numBlades && b < kMaxKlingen; ++b) {
                // noBlade / noBlade2, je nach bladeStyle2Start
                // (ebenda:14562 ff.) - der Griff eines Elektrostabs hat
                // keine Klinge, nur das Licht.
                if (art != nullptr) {
                    const bool zweiter =
                        art->bladeStyle2Start > 0 && b >= art->bladeStyle2Start;
                    if (zweiter ? art->ohneKlinge2 : art->ohneKlinge) {
                        continue;
                    }
                }
                const float laenge = klingenLaenge(st, static_cast<int>(n), b, ms);
                // Unter MIN_SABERBLADE_DRAW_LENGTH (0.5, wp_saber.h:122)
                // zeichnet die Engine gar nichts.
                if (laenge < 0.5F) {
                    continue;
                }
                const KlingenZustand& kz = s.klinge[static_cast<std::size_t>(b)];
                KlingeDraw& kd = an->klinge[static_cast<std::size_t>(an->klingen)];
                ++an->klingen;
                kd.nummer = b;
                kd.laenge = laenge;
                kd.radius = (kz.radius > 0.0F) ? kz.radius : 3.0F;
                bladeColorFor(kz.farbe, kd.farbe);
                kd.glowTex = bladeTexture(kz.farbe, true);
                kd.coreTex = bladeTexture(kz.farbe, false);
            }
        }
    }
    // Kinomodelle haengen unabhaengig von der Waffe
    // (Q3_AddRHandModel & Co., Q3_Interface.cpp:6336 ff.).
    for (const KinoModell& km : st.kino) {
        const std::string* p = pfad(km.modell);
        if (p != nullptr) {
            (void)neu(hiltFor(*p), km.links ? "*l_hand" : "*r_hand");
        }
    }
}

const App::Skeleton* skeletonFor(const GlmModel& model) {
    // Kein Pfad im Kopf? Die Engine raet dann _humanoid:
    // "take a guess, maybe it's right?" (NPC_stats.cpp). Wir tun dasselbe.
    std::string animFile = model.animFile;
    if (animFile.empty()) {
        animFile = "models/players/_humanoid/_humanoid";
    }
    const auto have = g_app->skeletons.find(animFile);
    if (have != g_app->skeletons.end()) {
        return have->second.anim.empty() ? nullptr : &have->second;
    }
    App::Skeleton sk;
    std::string data;
    // Das Archiv MIT ins Protokoll.
    //
    // Movie Duels ersetzt models/players/_humanoid/_humanoid.gla durch eine
    // Fassung mit 30384 statt 21376 Bildern - gleicher Name, gleicher
    // Ordner. Ohne diese Angabe steht im Protokoll nur die Bilderzahl, und
    // man muss sie auswendig kennen, um zu merken, dass die falsche Datei
    // geladen wurde. Mit ihr sieht man es auf einen Blick.
    std::string ausArchiv;
    if (readFromArchives(animFile + ".gla", data, &ausArchiv)) {
        std::string err;
        if (readGla(data, sk.anim, &err)) {
            sk.sections = animSectionsFor(animFile);
            diag::info("Skelett " + animFile + ": " +
                       std::to_string(sk.anim.bones.size()) + " Knochen, " +
                       std::to_string(sk.anim.numFrames) + " Bilder, " +
                       std::to_string(sk.sections.size()) + " Abschnitte, aus " +
                       fileName(ausArchiv));
        } else {
            diag::info("Skelett nicht lesbar: " + animFile + ".gla - " + err);
        }
    } else {
        diag::info("keine .gla gefunden: " + animFile + ".gla");
    }
    const auto put = g_app->skeletons.emplace(animFile, std::move(sk));
    return put.first->second.anim.empty() ? nullptr : &put.first->second;
}

// Die fx_runner der Karte einlesen.
//
// 374 Stueck ueber die 23 Karten von MD_Maps_Ep1/2/4/6 - nach waypoint und
// info_null die haeufigste Entity. Rauch, Funken, Dampf; ohne sie sieht ein
// Hangar aus wie ein leerer Raum.
//
// Der Pfad wird so gebildet, wie es die Engine tut
// (FxScheduler.cpp, CFxScheduler::RegisterEffect):
//
//     COM_StripExtension( path, filenameNoExt, ... );
//     Com_sprintf( buf, ..., "%s/%s.efx", FX_FILE_PATH, filenameNoExt );
//
// Also Endung abschneiden, dann "effects/" davor und ".efx" dahinter. Der
// Schluessel "fxfile" steht in der Karte oft schon mit Endung, oft ohne -
// beides muss zum selben Ergebnis fuehren.
void loadMapEffects();
void loadMapModels();

// --- Fuer den ICARUS-Nachbau ------------------------------------------------

// Wie heisst das offene Skript fuer ICARUS? "md_ga/intro_jedi" - aus dem
// Archivnamen, sonst aus dem Pfad auf der Platte (alles hinter "scripts/").
std::string aktiverSkriptPfad() {
    if (!g_app->skriptPfad.empty()) {
        return g_app->skriptPfad;
    }
    std::string p = g_app->path;
    for (char& c : p) {
        if (c == '\\') { c = '/'; }
    }
    std::string k = p;
    for (char& c : k) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const auto s = k.rfind("/scripts/");
    std::string r = (s == std::string::npos) ? p.substr(p.find_last_of('/') + 1) : p.substr(s + 9);
    const auto punkt = r.find_last_of('.');
    const auto strich = r.find_last_of('/');
    if (punkt != std::string::npos && (strich == std::string::npos || punkt > strich)) {
        r.resize(punkt);
    }
    return r;
}

// Ein Skript fuer run / use / spawnscript. Zuerst ein offener Reiter (dann
// wirken ungespeicherte Aenderungen sofort), sonst aus den Archiven.
const Script* skriptFuerAblauf(const std::string& pfad) {
    const auto klein = [](std::string x) {
        for (char& c : x) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (c == '\\') { c = '/'; }
        }
        return x;
    };
    const std::string k = klein(pfad);
    for (const App::Parked& t : g_app->tabs) {
        if (!t.skriptPfad.empty() && klein(t.skriptPfad) == k) {
            return &t.doc.script();
        }
        std::string tp = klein(t.path);
        const auto s = tp.rfind("/scripts/");
        if (s != std::string::npos) {
            tp = tp.substr(s + 9);
            const auto punkt = tp.find_last_of('.');
            if (punkt != std::string::npos) { tp.resize(punkt); }
            if (tp == k) {
                return &t.doc.script();
            }
        }
    }
    const auto it = g_app->ablaufSkripte.find(k);
    if (it != g_app->ablaufSkripte.end()) {
        return it->second.get();
    }
    std::unique_ptr<Script> s;
    std::string data;
    for (const char* ext : {".txt", ".icarus", ".ibi", ".IBI"}) {
        if (!readFromArchives("scripts/" + pfad + ext, data)) {
            continue;
        }
        auto sc = std::make_unique<Script>();
        std::vector<Diag> d;
        if (data.size() > 4 && data.compare(0, 3, "IBI") == 0) {
            std::vector<IbiBlock> bl;
            if (readIbi(data, bl, d)) {
                (void)decompile(bl, g_app->db, *sc, d);
            }
        } else {
            (void)readScript(data, *sc, d);
        }
        s = std::move(sc);
        break;
    }
    const Script* r = s.get();
    g_app->ablaufSkripte[k] = std::move(s);   // auch "nicht gefunden" merken
    return r;
}

// Eine ROFF-Bahn aus den Archiven, gemerkt (bhed/roff.h). Der Name wie im
// Skript: play ( "PLAY_ROFF", "kor2/roffs/boulder" ) sucht
// scripts/kor2/roffs/boulder.rof, genau wie G_LoadRoff (g_roff.cpp:437).
std::shared_ptr<const Roff> roffFuer(const std::string& name) {
    if (name.empty()) {
        return nullptr;
    }
    const std::string pfad = roffPfad(name);
    std::string k = pfad;
    for (char& c : k) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const auto it = g_app->roffs.find(k);
    if (it != g_app->roffs.end()) {
        return it->second;
    }
    std::shared_ptr<const Roff> r;
    std::string data;
    if (readFromArchives(pfad, data)) {
        auto neu = std::make_shared<Roff>();
        std::string grund;
        if (leseRoff(data, *neu, &grund)) {
            diag::detail("ROFF " + pfad + ": Fassung " + std::to_string(neu->fassung) + ", " +
                         std::to_string(neu->bilder.size()) + " Bilder zu " + std::to_string(neu->bildMs) +
                         " ms, " + std::to_string(neu->notizen.size()) + " Notizen");
            for (const std::string& h : neu->hinweise) {
                diag::detail("ROFF " + pfad + ": " + h);
            }
            r = std::move(neu);
        } else {
            // G_ValidRoff: "Invalid roff format" - im Spiel bewegt sich dann
            // nichts, und die Aufgabe bleibt offen.
            diag::warn("ROFF " + pfad + " ungueltig: " + grund);
        }
    } else if (!g_app->gamePaths.empty()) {
        diag::warn("ROFF " + pfad + " nicht in den Archiven");
    }
    g_app->roffs[k] = r;   // auch "nicht gefunden" merken
    return r;
}

AblaufWelt ablaufWelt() {
    AblaufWelt w;
    w.karte = g_app->map.empty() ? nullptr : &g_app->map;
    w.skript = [](const std::string& p) { return skriptFuerAblauf(p); };
    w.klangDauer = [](const std::string& f) { return soundLengthMs(f); };
    // Wie lange play ( "PLAY_ROFF", ... ) die Aufgabe haelt. Ohne
    // Spielordner weiss niemand, ob es die Datei gibt: dann "unbekannt" und
    // sofort fertig. Mit Spielordnern und ohne Datei bleibt die Aufgabe
    // offen - wie im Spiel.
    w.roffDauer = [](const std::string& name) {
        const std::shared_ptr<const Roff> r = roffFuer(name);
        if (r) {
            return roffLaufzeitMs(*r);
        }
        return g_app->gamePaths.empty() ? -1.0 : kRoffFehlt;
    };
    return w;
}

// Pfade aus dem flachen Skript zurueck auf das offene Skript. Befehle aus
// anderen Skripten behalten ihren Skriptnamen in `fremd` und bekommen einen
// leeren Pfad (dann waehlt ein Klick nichts an, und keine Kante ist
// ziehbar).
void pfadeAufsSkript() {
    const Ablauf& a = g_app->ablauf;
    const auto um = [&a](Path& p, std::string& fremd) {
        const AblaufHerkunft* h = a.woher(p);
        if (h == nullptr || h->skript < 0) {
            p.clear();
            fremd.clear();
            return;
        }
        if (h->skript == 0) {
            p = h->path;
            fremd.clear();
            return;
        }
        fremd = a.skripte[static_cast<std::size_t>(h->skript)];
        p.clear();
    };
    for (CamSegment& s : g_app->camTrack.segments) {
        um(s.path, s.fremd);
    }
    for (TimelineTrack& tr : g_app->timeline.tracks) {
        for (TimelineEvent& e : tr.events) {
            um(e.path, e.fremd);
        }
    }
    for (Diag& d : g_app->timeline.notes) {
        std::string f;
        um(d.path, f);
    }
    for (Actor& ac : g_app->scene.actors) {
        for (ActorStep& st : ac.steps) {
            um(st.path, st.fremd);
        }
    }
}

void prepareScene() {
    diag::Step step("Ansicht vorbereiten");
    // Dieselbe Reihenfolge wie bisher im Zeichnen - sie ist begruendet:
    // die Szene braucht die Karte, die Effekte haengen an den Entities,
    // und die .md3-Modelle haengen ihre Texturen hinten an
    // textures.byShader an, also muss die Karte vorher stehen.
    // --- Marken aus der Karte aufloesen ----------------------------------
    //
    // Viele Missionen setzen die Kamera nicht auf Zahlen, sondern auf eine
    // Entity: camera( MOVE, tag( "cam1b", ORIGIN ), 0 ). In md_ga_jedi ist
    // JEDER Kamerapunkt so gesetzt - 507 Entities, darunter ref_tag "cam1b"
    // bei 208 1796 1224.
    //
    // Ohne Aufloesung las der Vektorleser dort null Zahlen, und die Kamera
    // stand auf 0 0 0. Genau so gemeldet: "sie ist immer im Boden."
    //
    // Die Auskunft steht hier und nicht im Kern, weil nur die Oberflaeche
    // die Karte hat.
    const auto marke = [](const std::string& name, float* o,
                          float* a) -> bool {
        // Welche Entities die Engine als Marke kennt: nur ref_tag
        // (g_ref.cpp, ref_link -> TAG_Add) und waypoint_navgoal (g_nav.cpp).
        // Gesucht wird ohne Gross/klein (TAG_Add: Q_strlwr "for case
        // insensitive searches on a map"). Eine andere Entity gleichen
        // Namens findet das Spiel NICHT - der Kamerabefehl scheitert dort.
        const auto klein = [](std::string x) {
            for (char& c : x) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            return x;
        };
        const std::string gesucht = klein(name);
        for (const MapEntity& e : g_app->map.entities) {
            if (klein(e.targetname) != gesucht) {
                continue;
            }
            if (e.classname != "ref_tag" && e.classname.rfind("waypoint_navgoal", 0) != 0) {
                diag::detail("Kameramarke \"" + name + "\": " + e.classname +
                             " ist im Spiel keine Marke (nur ref_tag, waypoint_navgoal)");
                continue;
            }
            if (!e.origin.empty()) {
                std::istringstream is(e.origin);
                is >> o[0] >> o[1] >> o[2];
            }
            // "angle" setzt nur die Gier (F_ANGLEHACK), "angles" alle drei.
            if (const std::string* an1 = e.find("angle")) {
                a[0] = 0.0F;
                a[1] = static_cast<float>(std::atof(an1->c_str()));
                a[2] = 0.0F;
            }
            if (const std::string* an = e.find("angles")) {
                std::istringstream is(*an);
                is >> a[0] >> a[1] >> a[2];
            }
            // --- Ein ref_tag mit "target" ZEIGT auf etwas ----------------
            //
            // Gemeldet: "die Kamera von md_ga_jedi, die erste ja, aber alle
            // anderen sind falsch."
            //
            // Hier stand bisher der Satz, ein ref_tag ohne "angles" schaue
            // geradeaus. Das ist falsch, und der Quelltext sagt es
            // ausdruecklich. code/game/g_ref.cpp, ref_link():
            //
            //     if ( ent->target ) {
            //         target = G_Find( NULL, FOFS(targetname), ent->target );
            //         VectorSubtract( target->s.origin, ent->s.origin, dir );
            //         VectorNormalize( dir );
            //         vectoangles( dir, ent->s.angles );
            //     }
            //     TAG_Add( ent->targetname, ..., ent->s.angles, ... );
            //
            // Der Kommentar ueber SP_reference_tag sagt es in Worten:
            // "If you target a ref_tag at an entity, that will set the
            // ref_tag's angles toward that entity."
            //
            // In md_ga_jedi hat JEDER Kamerapunkt ein target - cam1b auf
            // t2, cam2 auf t5, cam3 auf t6 und so fort. Ohne diese Regel
            // blieben alle Winkel null.
            //
            // Warum die ERSTE trotzdem stimmte: cam1b liegt bei
            // 208 1796 1224 und t2 genau in +x davor. Die richtige Antwort
            // ist dort tatsaechlich 0 0 0 - der eine Fall, in dem der
            // Fehler nicht auffaellt. Bei cam2 sind es 273 Grad Yaw, bei
            // cam3 153, bei cam13 233.
            //
            // Das TARGET GEWINNT gegen einen vorhandenen "angles"-Schluessel:
            // ref_link schreibt s.angles ohne Bedingung, wenn es das Ziel
            // findet. Nur wenn das Ziel fehlt, bleibt es beim Schluessel -
            // deshalb steht die Aufloesung NACH dem Lesen von "angles" und
            // nicht davor.
            if (const std::string* tgt = e.find("target")) {
                if (!tgt->empty()) {
                    for (const MapEntity& z : g_app->map.entities) {
                        if (z.targetname != *tgt || z.origin.empty()) {
                            continue;
                        }
                        float ziel[3] = {0.0F, 0.0F, 0.0F};
                        std::istringstream is(z.origin);
                        is >> ziel[0] >> ziel[1] >> ziel[2];
                        // aimAngles statt einer eigenen Rechnung: dieselbe
                        // Funktion, die camera(FOLLOW) und die gezeichnete
                        // Kamera benutzen. Eine vierte Abschrift von
                        // vectoangles waere genau die Sorte Doppelung, die
                        // hier schon einmal auseinandergelaufen ist.
                        aimAngles(o, ziel, a);
                        break;
                    }
                }
            }
            return true;
        }
        return false;
    };
    // --- Der ICARUS-Nachbau (bhed/ablauf.h) -------------------------------
    //
    // Das Skript wird nicht mehr nur durchgezaehlt, sondern ausgefuehrt: im
    // Takt der Spiellogik, mit run, do/dowait, affect in affect, use auf
    // Skriptstarter und Spawner. Heraus kommt ein flaches Skript, aus dem
    // Kamerabahn, Zeitleiste und Szene entstehen - wie vorher, nur dass die
    // Zeiten jetzt die des Spiels sind.
    AblaufWelt welt = ablaufWelt();
    g_app->ablauf = simuliereAblauf(g_app->doc.script(), aktiverSkriptPfad(), welt);
    {
        const Ablauf& ab = g_app->ablauf;
        diag::detail("Ablauf: Traeger \"" + ab.traeger + "\", " + std::to_string(ab.befehle) + " Befehle aus " +
                     std::to_string(ab.skripte.size()) + " Skript(en), Ende " +
                     std::to_string(static_cast<int>(ab.endeMs)) + " ms");
        for (const std::string& h : ab.hinweise) {
            diag::detail("Ablauf: " + h);
        }
    }
    const Script& flach = g_app->ablauf.flach;
    g_app->camTrack = buildCameraTrack(flach, marke);
    g_app->timeline = buildTimeline(flach, g_app->db);
    // Die ROFF-Bahnen schon im ersten Aufbau: eine Entity, die nur per
    // PLAY_ROFF fliegt, soll auch ohne zweiten Durchgang fliegen.
    SzenenHilfe nurRoff;
    nurRoff.roff = [](const std::string& n) { return roffFuer(n); };
    g_app->scene = buildScene(flach, g_app->map, &nurRoff);
    g_app->camTrackValid = true;
    // --- Zweiter Durchgang: mit dem, was die Figuren mitbringen ---------
    //
    // Der erste Aufbau sagt, WELCHE Figuren es gibt. Mit ihren Modellen
    // kommen die .npc (Geh- und Lauftempo) und die animation.cfg (Laenge
    // jeder Animation). Beides aendert die Zeiten: ein wait( "task" )
    // wartet, bis die Animation darin abgelaufen ist, und ein Weg dauert
    // so lange, wie das Tempo der Figur es sagt.
    if (!g_app->scene.actors.empty()) {
        loadActorModels();
        kinoSkeletteAbgleichen();
        std::map<std::string, std::size_t> index;
        for (std::size_t i = 0; i < g_app->scene.actors.size(); ++i) {
            index[g_app->scene.actors[i].name] = i;
        }
        const auto eintrag = [index](const std::string& figur,
                                     const std::string& anim) -> const AnimEntry* {
            const auto it = index.find(figur);
            if (it == index.end() || it->second >= g_app->actorAssets.size()) {
                return nullptr;
            }
            const App::Skeleton* sk = g_app->actorAssets[it->second].skeleton;
            if (sk == nullptr) {
                return nullptr;
            }
            for (const AnimEntry& e : sk->sections) {
                if (e.name == anim) {
                    return &e;
                }
            }
            return nullptr;
        };
        // frameLerp wie die Engine: ceil(1000/fps), negativ floor.
        const auto lerpVon = [](const AnimEntry& e) {
            return (e.fps > 0) ? std::ceil(1000.0 / e.fps)
                               : (e.fps < 0) ? std::fabs(std::floor(1000.0 / e.fps)) : 1000.0;
        };
        SzenenHilfe hilfe;
        hilfe.npcs = &g_app->npcMap;
        // Fuer Waffe und Schwerter (src/ausruestung.cpp). Beide sind hier
        // geladen: loadActorModels liest sie zusammen mit den .npc.
        hilfe.sabers = &g_app->saberMap;
        hilfe.waffen = &g_app->weaponMap;
        hilfe.roff = [](const std::string& n) { return roffFuer(n); };
        hilfe.animDauer = [eintrag, lerpVon](const std::string& f, const std::string& a) {
            const AnimEntry* e = eintrag(f, a);
            return (e == nullptr) ? -1.0
                                  : std::max(0, e->numFrames - 1) * lerpVon(*e);
        };
        hilfe.bildDauer = [eintrag, lerpVon](const std::string& f, const std::string& a) {
            const AnimEntry* e = eintrag(f, a);
            return (e == nullptr) ? -1.0 : lerpVon(*e);
        };
        // Mit den Laengen der Animationen und den Tempi der Figuren noch
        // einmal ausfuehren: dowait wartet auf beides, also verschieben
        // sich die Zeiten ALLER Spuren, auch die der Kamera.
        const Scene ersteSzene = buildScene(g_app->ablauf.flach, g_app->map, &hilfe);
        welt.animDauer = hilfe.animDauer;
        welt.tempo = [ersteSzene](const std::string& figur, bool gehen) -> float {
            for (const Actor& a : ersteSzene.actors) {
                if (a.name == figur) {
                    return gehen ? a.gehTempo : a.laufTempo;
                }
            }
            return -1.0F;
        };
        g_app->ablauf = simuliereAblauf(g_app->doc.script(), aktiverSkriptPfad(), welt);
        g_app->camTrack = buildCameraTrack(g_app->ablauf.flach, marke);
        g_app->timeline = buildTimeline(g_app->ablauf.flach, g_app->db);
        g_app->scene = buildScene(g_app->ablauf.flach, g_app->map, &hilfe);
    }
    // Figuren, die erst waehrend der Szene gespawnt werden (NPC_spawner per
    // use), gibt es vorher nicht: unsichtbar bis NPC_Begin.
    for (Actor& ac : g_app->scene.actors) {
        const auto it = g_app->ablauf.erscheint.find(ac.name);
        if (it == g_app->ablauf.erscheint.end()) {
            continue;
        }
        ActorStep weg;
        weg.kind = ActorStep::Kind::Hide;
        ActorStep da;
        da.kind = ActorStep::Kind::Show;
        da.startMs = it->second;
        da.endMs = it->second;
        ac.steps.insert(ac.steps.begin(), weg);
        ac.steps.push_back(da);
        std::stable_sort(ac.steps.begin(), ac.steps.end(),
                         [](const ActorStep& x, const ActorStep& y) { return x.startMs < y.startMs; });
    }
    // Der Spieler ist unsichtbar, solange die Skriptkamera an ist: CG_Player
    // bricht fuer clientNum 0 ab, wenn in_camera gesetzt ist
    // (cg_players.cpp:15602). In md_am/end_cin stand sonst die Vorgabefigur
    // mitten auf der Plattform - im Spiel spielt dort ani3 die Rolle, und
    // erst nach der Szene uebernimmt der Spieler (SET_COPY_ORIGIN).
    for (Actor& ac : g_app->scene.actors) {
        std::string n = ac.name;
        for (char& c : n) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        if (n != "player") {
            continue;
        }
        ac.kameraVerborgen.clear();
        std::vector<const CamSegment*> schalter;
        for (const CamSegment& s : g_app->camTrack.segments) {
            if (s.kind == CamSegment::Kind::Enable || s.kind == CamSegment::Kind::Disable) {
                schalter.push_back(&s);
            }
        }
        std::stable_sort(schalter.begin(), schalter.end(),
                         [](const CamSegment* x, const CamSegment* y) { return x->startMs < y->startMs; });
        double an = -1.0;
        for (const CamSegment* s : schalter) {
            if (s->kind == CamSegment::Kind::Enable) {
                if (an < 0.0) { an = s->startMs; }
            } else if (an >= 0.0) {
                ac.kameraVerborgen.emplace_back(an, s->startMs);
                an = -1.0;
            }
        }
        if (an >= 0.0) {
            ac.kameraVerborgen.emplace_back(an, -1.0);
        }
    }
    // Ein NPC_type, der in keiner .npc steht, wird im Spiel NIE gespawnt:
    // NPC_ParseParms schlaegt fehl, NPC_Spawn_Do meldet "Couldn't spawn
    // NPC" und gibt die Entity frei (NPC_spawn.cpp:2577). So Tavion am Ende
    // von yavin1 ("tavion_new_dark" gibt es in keinem Archiv).
    if (!g_app->npcMap.empty()) {
        for (Actor& ac : g_app->scene.actors) {
            if (ac.npcType.empty() || ac.brushModel > 0) {
                continue;
            }
            std::string typ = ac.npcType;
            for (char& c : typ) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            if (g_app->npcMap.count(typ) != 0) {
                continue;
            }
            diag::info("Figur " + ac.name + ": NPC_type \"" + ac.npcType +
                       "\" steht in keiner .npc - im Spiel wird sie nicht gespawnt");
            std::erase_if(ac.steps, [](const ActorStep& s) { return s.kind == ActorStep::Kind::Show; });
            ActorStep weg;
            weg.kind = ActorStep::Kind::Hide;
            ac.steps.insert(ac.steps.begin(), weg);
        }
    }
    // SET_CAMERA_GROUP aus dem Ablauf einsammeln (fuer camera FOLLOW).
    g_app->kameraGruppen.clear();
    for (const Node& n : g_app->ablauf.flach.nodes) {
        if (n.name != "affect" || n.args.empty()) {
            continue;
        }
        std::string wer = n.args[0].text;
        for (char& c : wer) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        double uhr = 0.0;
        for (const Node& k : n.children) {
            if (k.name == "wait" && !k.args.empty()) {
                uhr += std::atof(k.args[0].text.c_str());
            } else if (k.name == "set" && k.args.size() >= 2 &&
                       (k.args[0].text == "SET_CAMERA_GROUP" || k.args[0].text == "set_camera_group")) {
                g_app->kameraGruppen[wer].emplace_back(uhr, k.args[1].text);
            }
        }
    }
    // Die Pfade zeigen jetzt ins flache Skript - zurueck auf die Zeilen des
    // offenen Skripts, damit Anklicken, Kantenziehen und "write to script"
    // die richtige Zeile treffen.
    pfadeAufsSkript();
    // --- Die Karte selbst: Tueren, func_wall, fx_runner auf use --------
    //
    // Aus jedem `use` der Zeitleiste und den Wegen der Figuren (fuer die
    // Ausloesefelder der Tueren). Die Figurenorte einmal im Takt der
    // Spiellogik abgetastet - die Simulation fragt sie tausendfach ab.
    {
        // Aus dem Nachbau: jede Benutzung, auch ueber target_relay,
        // target_delay, target_counter und trigger_* hinweg - die Kette ist
        // dort schon aufgeloest (MoverUse::aufgeloest).
        std::vector<MoverUse> uses;
        for (const Ablauf::Benutzung& b : g_app->ablauf.benutzt) {
            // Mit dem Ausloeser: target_print zeigt nur, was der Spieler
            // ausloest (Use_Target_Print, activator->client).
            uses.push_back(MoverUse{b.ms, b.name, true, b.ausloeser});
        }
        const double ende = std::max(g_app->camTrack.durationMs,
                                     g_app->timeline.practicalEndMs) + 5000.0;
        constexpr double kTakt = 50.0;
        struct Ort { float p[3]; };
        std::vector<std::vector<Ort>> figuren;   // [schritt][figur]
        const auto schritte = static_cast<std::size_t>(ende / kTakt) + 2U;
        figuren.resize(schritte);
        for (const Actor& a : g_app->scene.actors) {
            if (a.brushModel > 0 || !a.haveStart) {
                continue;   // nur Figuren loesen Tueren aus
            }
            for (std::size_t s = 0; s < schritte; ++s) {
                const ActorState st = a.at(static_cast<double>(s) * kTakt);
                if (!st.visible) {
                    continue;
                }
                figuren[s].push_back(Ort{{st.pos[0], st.pos[1], st.pos[2]}});
            }
        }
        const auto figur = [&figuren](double ms, const float mn[3], const float mx[3]) {
            const auto s = static_cast<std::size_t>(std::max(0.0, ms) / kTakt);
            if (s >= figuren.size()) {
                return false;
            }
            // Der Kasten eines NPC (-16 -16 -24 .. 16 16 40) gegen das Feld.
            for (const Ort& o : figuren[s]) {
                if (o.p[0] + 16.0F >= mn[0] && o.p[0] - 16.0F <= mx[0] &&
                    o.p[1] + 16.0F >= mn[1] && o.p[1] - 16.0F <= mx[1] &&
                    o.p[2] + 40.0F >= mn[2] && o.p[2] - 24.0F <= mx[2]) {
                    return true;
                }
            }
            return false;
        };
        // Das eigene Bruchstueckmodell "_c1" nimmt CG_Chunks nur, wenn es
        // geladen werden konnte - also: liegt es in einem Archiv?
        g_app->moverSim.dateiDa = [](const std::string& pfad) {
            for (const auto& gp : g_app->gamePaths) {
                for (const auto& a : gp.archives) {
                    if (a.find(pfad) != nullptr) {
                        return true;
                    }
                }
            }
            return false;
        };
        g_app->moverSim.baue(g_app->map, g_app->geo, std::move(uses), figur, ende);
        for (const Bruch& b : g_app->moverSim.brueche()) {
            diag::detail(b.klasse + " #" + std::to_string(b.entity) + " zerbricht bei " +
                         std::to_string(static_cast<int>(b.ms)) + " ms (Material " +
                         std::to_string(b.material) + ", " + std::to_string(b.stuecke) + " Stuecke)");
        }
        for (const Bildschirmtext& bt : g_app->moverSim.bildschirmtexte()) {
            diag::detail("target_print #" + std::to_string(bt.entity) + " bei " +
                         std::to_string(static_cast<int>(bt.ms)) + " ms" +
                         (bt.gezeigt ? "" : " (nicht vom Spieler ausgeloest - kommt im Spiel nicht an)") +
                         ": " + bt.text);
        }
        for (const MoverSim::Oeffnung& o : g_app->moverSim.oeffnungen()) {
            diag::detail("Tuer *" + std::to_string(o.modell) + " oeffnet ab " +
                         std::to_string(static_cast<int>(o.abMs)) + " ms, offen ab " +
                         std::to_string(static_cast<int>(o.offenMs)) + " ms");
        }
        for (const FxRunnerStart& f : g_app->moverSim.fxStarts()) {
            diag::detail("fx_runner #" + std::to_string(f.entity) + " startet bei " +
                         std::to_string(static_cast<int>(f.ms)) + " ms" +
                         (f.oneShot ? " (einmal)" : ""));
        }
        // Die Fahrgeraeusche (soundSet -> sound/sound.txt, bmodelSet).
        if (!g_app->bmodelSetsGelesen) {
            g_app->bmodelSetsGelesen = true;
            std::string text;
            if (readFromArchives("sound/sound.txt", text)) {
                g_app->bmodelSets = leseBmodelSets(text);
            }
        }
        g_app->moverKlaenge = g_app->moverSim.klaenge(ende);
        if (!g_app->moverKlaenge.empty()) {
            diag::detail(std::to_string(g_app->moverKlaenge.size()) + " Moverklaenge, " +
                         std::to_string(g_app->bmodelSets.size()) + " bmodelSets bekannt");
        }
    }
    // Die LAENGE der Klaenge nachtragen.
    //
    // Das Skript sagt nur, WANN ein sound anfaengt - wie lange er dauert,
    // steht in der Datei. Ohne diese Zahl blieb endMs gleich startMs, und
    // die Frage "laeuft dieser Klang gerade?" war nie mit ja zu
    // beantworten. Genau daran scheiterte der Wiedereinstieg beim
    // Einschalten des Tons: er suchte einen laufenden Klang und fand nie
    // einen, also blieb es still bis zum naechsten - beim Anwender fuenf
    // Sekunden.
    //
    // Hier und nicht beim Zeichnen: die Dateien werden dafuer gelesen, und
    // das gehoert in die Ladephase.
    for (TimelineTrack& tr2 : g_app->timeline.tracks) {
        for (TimelineEvent& e : tr2.events) {
            if (e.kind != TimelineEvent::Kind::Sound || e.sound.empty()) {
                continue;
            }
            const double len = soundLengthMs(e.sound);
            if (len > 0.0) {
                e.endMs = e.startMs + len;
            }
        }
    }
    g_app->playMs = std::min(g_app->playMs, g_app->camTrack.durationMs);
    g_app->actorAssets.clear();
    // Effekte (fx_runner) und Kartenmodelle (misc_model) gehoeren zur
    // KARTE, nicht zu den Figuren. Sie standen hinter der Abfrage unten und
    // wurden fuer ein Skript ohne Figuren nie geladen (Kartentest 27.09.).
    loadMapEffects();
    loadMapModels();
    // --- Kartenmodelle, die ein Skript bewegt ---------------------------
    //
    // Ein misc_model mit script_targetname kann per affect bewegt werden
    // (move, rotate, PLAY_ROFF - t1_rail: der Greifer "script_introClaw"
    // und die Jaeger fliegen so durch die Stadt). Die Szene rechnet die
    // Bahn; gezeichnet wurde das Modell aber an seinem festen Platz.
    g_app->modellFigur.assign(g_app->mapModels.size(), -1);
    for (std::size_t m = 0; m < g_app->mapModels.size(); ++m) {
        const std::size_t ei = g_app->mapModels[m].entity;
        if (ei >= g_app->map.entities.size()) {
            continue;
        }
        const std::string name = entitySkriptName(g_app->map.entities[ei]);
        if (name.empty()) {
            continue;
        }
        for (std::size_t a = 0; a < g_app->scene.actors.size(); ++a) {
            const std::string& an = g_app->scene.actors[a].name;
            if (an.size() == name.size() &&
                std::equal(an.begin(), an.end(), name.begin(), [](char x, char y) {
                    return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
                })) {
                g_app->modellFigur[m] = static_cast<int>(a);
                break;
            }
        }
    }
    // --- Figuren aus alten MD3-Teilen ------------------------------------
    //
    // Ein NPC-Typ mit legsmodel und ohne playerModel (remote_sp: "headmodel
    // none, torsomodel none, legsmodel remote_sp") bekommt im Spiel kein
    // Ghoul2-Modell; gezeichnet wird models/players/<legsmodel>/lower.md3
    // (CG_RegisterClientModelname, cg_main.cpp:1255), mit
    // lower_<skin>.skin, falls es sie gibt. Hier reist die Figur als
    // Kartenmodell, das ihrer Bahn folgt - wie ein bewegtes misc_model.
    for (std::size_t a = 0; a < g_app->scene.actors.size(); ++a) {
        const Actor& fig = g_app->scene.actors[a];
        if (fig.npcType.empty() || !fig.haveStart) {
            continue;
        }
        std::string typ = fig.npcType;
        for (char& c : typ) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const auto def = g_app->npcMap.find(typ);
        if (def == g_app->npcMap.end() || !def->second.playerModel.empty() || def->second.legsModel.empty()) {
            continue;
        }
        std::string pfad = "models/players/" + def->second.legsModel + "/lower.md3";
        for (char& c : pfad) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (g_app->md3Files.find(pfad) == g_app->md3Files.end()) {
            std::string data;
            Md3Model mm;
            std::string err;
            if (!readFromArchives(pfad, data) || !readMd3(data, mm, &err)) {
                diag::info("ohne Modell: " + fig.name + " - " + pfad + " fehlt oder ist nicht lesbar");
                continue;
            }
            g_app->md3Files[pfad] = std::move(mm);
        }
        const Md3Model& mm = g_app->md3Files[pfad];
        if (mm.empty()) {
            continue;
        }
        // Die Haut: "Flaeche,Shader" je Zeile (lower_default.skin).
        std::map<std::string, std::string> haut;
        {
            const std::string skin = def->second.customSkin.empty() ? "default" : def->second.customSkin;
            std::string text;
            const std::string ordner = "models/players/" + def->second.legsModel + "/";
            if (readFromArchives(ordner + "lower_" + skin + ".skin", text) ||
                readFromArchives(ordner + "lower_default.skin", text)) {
                std::istringstream is(text);
                std::string zeile;
                while (std::getline(is, zeile)) {
                    const auto komma = zeile.find(',');
                    if (komma == std::string::npos) {
                        continue;
                    }
                    std::string f = zeile.substr(0, komma);
                    std::string s = zeile.substr(komma + 1);
                    const auto rand = [](std::string& x) {
                        while (!x.empty() && std::isspace(static_cast<unsigned char>(x.back()))) { x.pop_back(); }
                        while (!x.empty() && std::isspace(static_cast<unsigned char>(x.front()))) { x.erase(0, 1); }
                        for (char& c : x) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                    };
                    rand(f);
                    rand(s);
                    if (!f.empty() && !s.empty()) {
                        haut[f] = s;
                    }
                }
            }
        }
        App::MapModel item;
        item.path = pfad;
        item.frames = mm.numFrames;
        item.entity = static_cast<std::size_t>(-1);
        item.standbild = true;
        for (int k = 0; k < 3; ++k) {
            item.origin[k] = fig.start[k];
            item.angles[k] = fig.startAngles[k];
        }
        item.shaderBase = static_cast<int>(g_app->textures.byShader.size());
        for (const Md3Surface& sf : mm.surfaces) {
            std::string name = sf.name;
            for (char& c : name) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            const auto h = haut.find(name);
            TextureSet::Tex t;
            loadTextureFor((h != haut.end()) ? h->second : sf.shader, t);
            g_app->textures.byShader.push_back(std::move(t));
        }
        g_app->mapModels.push_back(std::move(item));
        g_app->modellFigur.push_back(static_cast<int>(a));
    }
    if (g_app->scene.actors.empty()) {
        return;
    }
    loadActorModels();
    kinoSkeletteAbgleichen();
    // --- UND HIER AUCH ------------------------------------------------
    //
    // Zweimal habe ich das Vorladen fuer behoben erklaert (rc307, rc309),
    // und zweimal stand es danach unveraendert im Protokoll:
    //
    //     Nachgeladen: Klingenbild "...purple_glow2" - 10.7 ms (spielt)
    //
    // Der Grund war banal: `loadActorModels()` wird an ZWEI Stellen
    // gerufen, und ich hatte das Vorladen nur an die zweite gehaengt.
    // Diese hier - beim Aufbereiten der Ansicht - laeuft zuerst; danach
    // stimmen die Groessen, und die zweite Stelle kommt nie an die Reihe.
    //
    // Die Lehre: bei einer Behebung, die nicht wirkt, ist die erste Frage
    // nicht "rechnet sie richtig", sondern "laeuft sie ueberhaupt". Ich
    // habe zweimal an der Rechnung gebessert, ohne das zu pruefen.
    preloadSaberAssets();
}

// Die Shader eines Effekts laden - EINMAL je Name.
//
// Stand bis rc568 mitten in loadMapEffects (danach in ladeEffektDatei)
// und galt damit nur fuer Effekte, die dort gelesen wurden. Folgeeffekte
// (Aufprall, Tod, Fahne, playfx) kamen spaeter ueber effectByName und
// bekamen nie ein Bild - Slot 0, die erste Textur der Karte. Seit rc569 gehen sie denselben Weg (siehe die
// Vorabladung in loadMapEffects).
void ladeEffektShader(const efx::Effect& ef) {
    for (const efx::Primitive& pr : ef.primitives) {
        if (pr.shaders.empty()) {
            continue;
        }
        // --- ALLE Texturen des Bausteins, nicht nur die erste ----
        //
        // Bis rc367 bekam nur `shaders[0]` einen Platz. Das war
        // stimmig, solange der Zeichner auch nur die erste nahm.
        //
        // Mit rc367 wuerfelt er (CMediaHandles::GetHandle,
        // FxScheduler.h:92) - und traf damit Namen, die hier nie
        // eingetragen wurden. Ohne Platz bleibt `slot` auf 0, und
        // 0 ist nicht "ohne Bild", sondern die ERSTE Textur der
        // Karte. In md_am_sith ist das die Lava.
        //
        // Auf shanks Bildschirm: riesige Lavavierecke mitten im
        // Bild, gestapelt. Ein Fehler, den ich selbst eingebaut und
        // nicht nachgemessen habe - der Effektvergleich zeigt nur
        // Bytes, und dass die Bytes sich aendern, war ja gerade der
        // Zweck der Aenderung.
        for (const std::string& shName : pr.shaders) {
        std::string low = shName;
        for (char& ch : low) {
            ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
        }
        if (g_app->effectShaders.count(low) != 0U) {
            continue;
        }
        TextureSet::Tex t;
        if (!loadTextureFor(shName, t)) {
            // Kein Bild: Nummer 0 heisst "ohne", dann bleibt es
            // beim farbigen Viereck wie bisher.
            g_app->effectShaders[low] = 0;
            diag::detail("  kein Bild fuer Effektshader " + shName);
            continue;
        }
        // Ein Effektpartikel DECKEND zu zeichnen ist nie richtig.
        //
        // Genau das ergab die weissen Flecken: kein blendFunc im
        // Shaderskript heisst bei einer Wand "deckend", und bei
        // einer Wand stimmt das auch. Bei einem Partikel nicht -
        // ein deckendes Viereck mit weisser Textur ist ein weisser
        // Fleck, egal wie das Bild aussieht.
        //
        // Woran unterscheiden? Am Alphakanal des Bildes:
        //
        //   hat er Werte unter voll   -> durchscheinend. Rauch und
        //                                Wolken sind so gebaut: die
        //                                Form steckt im Alpha.
        //   ist er ueberall voll      -> additiv. Leuchten sind so
        //                                gebaut: die Form steckt in
        //                                der Helligkeit, Schwarz
        //                                ist unsichtbar.
        //
        // Das ist eine Faustregel, keine Ableitung aus dem
        // Quelltext - die Engine liest die Mischart aus dem
        // Shaderskript, und wo eines da ist, gilt weiterhin dieses.
        // Nur wenn KEINES da ist, greift die Regel.
        if (t.blend == BlendMode::Opaque && !t.empty()) {
            std::uint8_t kleinstesAlpha = 255;
            for (std::size_t k = 3; k < t.rgba.size(); k += 4U) {
                kleinstesAlpha = std::min(kleinstesAlpha, t.rgba[k]);
            }
            // setBlend, nicht `t.blend = ...`: die Faktoren muessen
            // mit. Siehe TextureSet::Tex::setBlend.
            t.setBlend((kleinstesAlpha < 250U) ? BlendMode::Alpha
                                               : BlendMode::Add);
            diag::detail("  Effektshader " + pr.shaders[0] +
                         " ohne blendFunc -> " +
                         (t.blend == BlendMode::Alpha ? "durchscheinend"
                                                      : "additiv") +
                         " (kleinstes Alpha " +
                         std::to_string(kleinstesAlpha) + ")");
        }
        // --- Der Alphawert der ECKE gilt hier ausdruecklich ----
        //
        // Ein Effekt legt seine Blende in den Eckenalphawert
        // (`useAlpha`, 278 Vorkommen, FxPrimitives.cpp:599).
        //
        // Bei der KARTE ist die Vorgabe der Engine eine andere:
        // ohne alphaGen gilt dort AGEN_IDENTITY, die Ecke zaehlt
        // nicht. behaved nimmt bisher fuer beides den Eckenwert -
        // gemessen betrifft der Unterschied in den 43 Karten null
        // Flaechen, aber richtig ist es nicht.
        //
        // Damit die Vorgabe spaeter in EINER Zeile umgestellt
        // werden kann, ohne die Effekte mitzureissen, tragen die
        // Effekttexturen ihr `Vertex` hier ausdruecklich. Heute
        // aendert das nichts; es ist die Vorarbeit, die den
        // spaeteren Schritt ungefaehrlich macht.
        t.alphaGen = AlphaGen::Vertex;
        g_app->effectShaders[low] =
            static_cast<int>(g_app->textures.byShader.size());
        g_app->textures.byShader.push_back(std::move(t));
        }   // Ende: alle Texturen dieses Bausteins
    }
}

// Eine Effektdatei EINMAL lesen und ihre Shader eintragen.
//
// Herausgezogen aus loadMapEffects, weil nicht nur fx_runner Effekte
// brauchen: auch was zerbricht (CG_MiscModelExplosion, MoverSim::
// bruchEffekte) spielt "chunks/metalexplode" & Co. - und das ohne Shader
// ueber effectByName zu laden, gaebe Partikel ohne Bild (Platz 0 ist die
// erste Textur der Karte, siehe efxdraw.cpp).
//
// `herkunft` steht nur im Protokoll (was in der Karte stand). false: die
// Datei fehlt oder ist nicht lesbar - schon gemeldet.
bool ladeEffektDatei(const std::string& path, const std::string& herkunft) {
    if (g_app->effects.find(path) != g_app->effects.end()) {
        return true;
    }
    {
        std::string text;
        if (!readFromArchives(path, text)) {
            diag::info("keine Effektdatei: " + path);
            diag::detail("Effekt " + path + "  (fxFile stand als \"" +
                         herkunft + "\")");
            // Dieselbe Frage wie bei den Texturen: liegt die Datei
            // woanders, oder gibt es sie nirgends?
            reportMissingTexture(path);
            return false;
        }
        const efx::ReadResult r = efx::read(text);
        // Die Shader der Primitive laden - EINMAL je Name.
        //
        // Damit bekommen Partikel endlich ihr Bild. Sie gehen denselben
        // Weg wie die Flaechen der Karte und die .md3-Modelle
        // (loadTextureFor), also auch mit blendFunc: fast alle
        // Effektshader sind additiv, und erst dadurch werden aus
        // schwarzen Kaesten weiche Wolken.
        ladeEffektShader(r.effect);
        if (diag::detailOn()) {
            diag::detail("Effekt " + path);
            for (const efx::Diagnostic& d : r.diagnostics) {
                diag::detail("  " +
                             std::string(d.severity == efx::Severity::Error
                                             ? "Fehler: "
                                             : "Anmerkung: ") +
                             d.message);
            }
            for (const efx::Primitive& pr : r.effect.primitives) {
                // Was zeichnen wir davon wirklich - und womit?
                const bool zeichenbar =
                    pr.type == efx::PrimitiveType::Particle ||
                    pr.type == efx::PrimitiveType::OrientedParticle ||
                    pr.type == efx::PrimitiveType::Tail ||
                    pr.type == efx::PrimitiveType::Line ||
                    pr.type == efx::PrimitiveType::Electricity ||
                    pr.type == efx::PrimitiveType::Cylinder ||
                    pr.type == efx::PrimitiveType::Decal ||
                    pr.type == efx::PrimitiveType::ScreenFlash ||
                    (pr.type == efx::PrimitiveType::Emitter &&
                     (pr.flags & efx::kFlagAttachedModel) != 0U);
                std::string zeile = std::string("  ") +
                                    efx::typeName(pr.type) +
                                    (zeichenbar ? "  gezeichnet"
                                                : "  NICHT gezeichnet");
                if (!pr.shaders.empty()) {
                    zeile += "  Shader: " + pr.shaders[0];
                } else {
                    zeile += "  ohne Shader";
                }
                if (!pr.alpha.present) { zeile += "  (kein alpha-Block)"; }
                if (!pr.rgb.present) { zeile += "  (kein rgb-Block)"; }
                if (!pr.size.present) { zeile += "  (kein size-Block)"; }
                diag::detail(zeile);
            }
        }
        if (r.hasErrors()) {
            std::string why = "unlesbar";
            for (const efx::Diagnostic& d : r.diagnostics) {
                if (d.severity == efx::Severity::Error) { why = d.message; }
            }
            diag::info("Effekt nicht lesbar: " + path + " - " + why);
            return false;
        }
        g_app->effects[path] = r.effect;
    }
    return true;
}

void loadMapEffects() {
    diag::Step step("Effekte suchen");
    g_app->effectRunners.clear();
    int gefunden = 0;
    int fehlt = 0;
    int wartend = 0;
    for (const MapEntity& e : g_app->map.entities) {
        if (e.classname != "fx_runner") {
            continue;
        }
        const std::string* file = e.find("fxFile");
        if (file == nullptr || file->empty() || e.origin.empty()) {
            continue;
        }
        std::string name = *file;
        const std::size_t dot = name.find_last_of('.');
        const std::size_t slash = name.find_last_of('/');
        if (dot != std::string::npos &&
            (slash == std::string::npos || dot > slash)) {
            name = name.substr(0, dot);
        }
        const std::string path = "effects/" + name + ".efx";

        if (!ladeEffektDatei(path, *file)) {
            ++fehlt;
            continue;
        }

        EffectInstance inst;
        inst.effect = &g_app->effects[path];
        float o[3] = {0, 0, 0};
        (void)std::sscanf(e.origin.c_str(), "%f %f %f", &o[0], &o[1], &o[2]);
        for (int k = 0; k < 3; ++k) {
            inst.origin[k] = o[k];
        }
        // --- Die Richtung des Runners --------------------------------
        //
        // Sie war nie gesetzt und stand damit auf der Vorgabe (0,0,1) -
        // zufaellig richtig, aber aus dem falschen Grund.
        //
        // SP_fx_runner (g_fx.cpp:242) sagt es ausdruecklich:
        //
        //     if ( !G_SpawnAngleHack( "angle", "0", ent->s.angles ))
        //     {
        //         // didn't have angles, so give us the default of up
        //         VectorSet( ent->s.angles, -90, 0, 0 );
        //     }
        //
        // Ohne `angle` zeigt ein fx_runner also NACH OBEN, nicht nach +x.
        // Das ist keine Kleinigkeit: in md_am_sith hat KEIN EINZIGER der
        // 41 Runner ein `angles`. Neun davon haben ein `target`, die
        // uebrigen gar nichts.
        //
        // Und es zaehlt erst, seit die oertliche Geschwindigkeit in diese
        // Achse gedreht wird (siehe inAchse in efxdraw.cpp). Vorher war
        // die Richtung ohne Wirkung - deshalb ist sie nie aufgefallen.
        inst.normal[0] = 0.0F;
        inst.normal[1] = 0.0F;
        inst.normal[2] = 1.0F;
        if (const std::string* t = e.find("target")) {
            // `target` zeigt auf einen Punkt; die Richtung ist der Weg
            // dorthin. Das ist die uebliche Art, einen Dampfstrahl zu
            // richten - neun der Runner in md_am_sith machen es so.
            for (const MapEntity& z : g_app->map.entities) {
                const std::string* tn = z.find("targetname");
                if (tn == nullptr || *tn != *t) {
                    continue;
                }
                float p2[3] = {0, 0, 0};
                (void)std::sscanf(z.origin.c_str(), "%f %f %f", &p2[0],
                                  &p2[1], &p2[2]);
                float d[3] = {p2[0] - o[0], p2[1] - o[1], p2[2] - o[2]};
                const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] +
                                          d[2] * d[2]);
                if (l > 0.0001F) {
                    for (int k = 0; k < 3; ++k) {
                        inst.normal[k] = d[k] / l;
                    }
                }
                break;
            }
        } else if (const std::string* a = e.find("angle")) {
            const float gier = static_cast<float>(std::atof(a->c_str())) *
                               3.14159265F / 180.0F;
            inst.normal[0] = std::cos(gier);
            inst.normal[1] = std::sin(gier);
            inst.normal[2] = 0.0F;
        }
        // Die Regeln stehen in code/game/g_fx.cpp, und ich hatte sie mir
        // vorher AUSGEDACHT. Was wirklich gilt:
        //
        //   SP_fx_runner():
        //     G_SpawnInt( "delay", "200", &ent->delay );
        //     G_SpawnFloat( "random", "0", &ent->random );
        //
        //   fx_runner_think(), Zeile 70:
        //     ent->nextthink = level.time + ent->delay
        //                      + Q_flrand(0,1) * ent->random;
        //
        //   fx_runner_link():
        //     if ( spawnflags & 1 || spawnflags & 2 )  // STARTOFF || ONESHOT
        //         ent->nextthink = -1;                 // wartet auf use
        //     else
        //         ent->nextthink = level.time + 200;   // laeuft sofort los
        //
        // Also: delay ist der WIEDERHOLABSTAND in Millisekunden, Vorgabe
        // 200 - keine einmalige Verzoegerung. random streut ihn. Ein "wait"
        // gibt es gar nicht; das hatte ich erfunden. Und ein Runner mit
        // STARTOFF oder ONESHOT laeuft erst, wenn ihn etwas benutzt.
        constexpr int kStartOff = 1;
        constexpr int kOneShot = 2;
        int flags = 0;
        if (const std::string* sf = e.find("spawnflags")) {
            flags = std::atoi(sf->c_str());
        }
        const bool wartet = (flags & (kStartOff | kOneShot)) != 0;
        inst.loops = !wartet;
        inst.intervalMs = 200.0F;
        if (const std::string* d = e.find("delay")) {
            const float v = static_cast<float>(std::atof(d->c_str()));
            if (v > 0.0F) {
                inst.intervalMs = v;
            }
        }
        // random streut den Abstand. Fuer eine wiederholbare Vorschau die
        // MITTE nehmen - dieselbe Wahl wie bei $random$ in der Zeitleiste.
        if (const std::string* rnd = e.find("random")) {
            inst.intervalMs += static_cast<float>(std::atof(rnd->c_str())) * 0.5F;
        }
        // Der erste Durchgang kommt 200 ms nach dem Start (fx_runner_link).
        // Wer auf ein use wartet, faengt bei uns gar nicht an - wann das
        // use kommt, weiss nur das Skript, und das werten wir hier nicht
        // aus.
        inst.startMs = wartet ? -1.0 : 200.0;
        // Der Ausgangswert haengt am Ort: zwei Runner nebeneinander sollen
        // nicht Partikel fuer Partikel gleich aussehen.
        inst.seed = static_cast<unsigned>(
            std::abs(static_cast<int>(o[0]) * 73856093 ^
                     static_cast<int>(o[1]) * 19349663 ^
                     static_cast<int>(o[2]) * 83492791));
        if (inst.startMs < 0.0) {
            // STARTOFF oder ONESHOT: laeuft erst auf ein use. Wann das
            // kommt, rechnet die Mover-Simulation aus der Zeitleiste
            // (fx_runner_use, g_fx.cpp): ONESHOT einmal, START_OFF ab dem
            // use und bis zum naechsten.
            const auto ei = static_cast<std::size_t>(&e - g_app->map.entities.data());
            bool gestartet = false;
            for (const FxRunnerStart& f : g_app->moverSim.fxStarts()) {
                if (f.entity != ei) {
                    continue;
                }
                EffectInstance an = inst;
                an.startMs = f.ms;
                an.loops = !f.oneShot;
                an.endMs = f.bisMs;
                g_app->effectRunners.push_back(an);
                gestartet = true;
            }
            if (gestartet) {
                ++gefunden;
            } else {
                ++wartend;
            }
            continue;
        }
        g_app->effectRunners.push_back(inst);
        ++gefunden;
    }
    // --- Was beim Zerbrechen spielt (CG_MiscModelExplosion) --------------
    //
    // func_breakable und misc_model_breakable, die ein use zerbricht: je
    // Material "chunks/metalexplode", "chunks/glassbreak" ..., mehrfach im
    // Kasten verteilt und von der Mitte weg gerichtet - die Engine spielt
    // sie mit theFxScheduler.PlayEffect(name, org, dir). Wann und wo,
    // rechnet MoverSim (bruchEffekte); hier nur Datei und Instanz.
    //
    // Sie stehen in effectRunners wie jeder Effekt - dann zeichnen,
    // beleuchten und klingen sie auf demselben Weg. Gezaehlt werden sie
    // eigens (bruchFxZahl), damit der Knopftest die per use gestarteten
    // Effekte weiterhin abziehen kann.
    g_app->bruchFxZahl = 0;
    for (const BruchEffekt& bf : g_app->moverSim.bruchEffekte()) {
        const std::string path = "effects/" + bf.effekt + ".efx";
        if (!ladeEffektDatei(path, bf.effekt)) {
            continue;
        }
        EffectInstance inst;
        inst.effect = &g_app->effects[path];
        for (int k = 0; k < 3; ++k) {
            inst.origin[k] = bf.ort[k];
            inst.normal[k] = bf.richtung[k];
        }
        inst.startMs = bf.ms;
        inst.loops = false;
        inst.seed = static_cast<unsigned>(g_app->bruchFxZahl * 2654435761U + bf.entity * 40503U) | 1U;
        g_app->effectRunners.push_back(inst);
        ++g_app->bruchFxZahl;
    }
    if (g_app->bruchFxZahl != 0U) {
        diag::info(std::to_string(g_app->bruchFxZahl) + " Brucheffekte (func_breakable, misc_model_breakable)");
    }
    diag::info(std::to_string(gefunden) + " fx_runner laufen, " +
               std::to_string(wartend) + " warten auf ein use (STARTOFF/ONESHOT), " +
               std::to_string(fehlt) + " ohne Datei, " +
               std::to_string(g_app->effects.size()) + " Effektdateien gelesen");

    // --- Die Folgeeffekte gleich mitladen, samt Shadern -----------------
    //
    // impactfx, deathfx, emitfx und playfx nennen weitere Effekte. Bis rc568
    // wurden sie erst beim ersten Gebrauch geholt (effectByName) - mitten im
    // Bild, und ohne ihre Shader einzutragen. Hier werden sie Stufe fuer
    // Stufe vorab gelesen, bis nichts Neues mehr dazukommt. Damit stehen
    // auch ihre Emittermodelle im naechsten Schritt bereit (die Schleife
    // dort geht ueber g_app->effects).
    {
        std::vector<const efx::Effect*> offen;
        for (const auto& [pfad, ef] : g_app->effects) {
            (void)pfad;
            offen.push_back(&ef);
        }
        std::set<std::string> gesehen;
        int nachgeladen = 0;
        int ohneDatei = 0;
        while (!offen.empty()) {
            const efx::Effect* ef = offen.back();
            offen.pop_back();
            for (const efx::Primitive& pr : ef->primitives) {
                for (const auto* liste :
                     {&pr.impactFx, &pr.deathFx, &pr.emitFx, &pr.playFx}) {
                    for (const std::string& name : *liste) {
                        if (!gesehen.insert(name).second) {
                            continue;
                        }
                        const std::string pfad = "effects/" + name + ".efx";
                        if (g_app->effects.count(pfad) != 0U) {
                            continue;   // schon da (Runner oder Waffe)
                        }
                        std::string text;
                        efx::Effect gelesen;
                        if (readFromArchives(pfad, text)) {
                            const efx::ReadResult rr = efx::read(text);
                            if (!rr.hasErrors()) {
                                gelesen = rr.effect;
                            }
                        }
                        // Auch den Fehlschlag merken - effectByName fragt
                        // sonst je Bild erneut im Archiv nach.
                        const auto put =
                            g_app->effects.emplace(pfad, std::move(gelesen));
                        if (put.first->second.primitives.empty()) {
                            ++ohneDatei;
                            diag::detail("Folgeeffekt " + pfad +
                                         " nicht gefunden oder leer");
                            continue;
                        }
                        ladeEffektShader(put.first->second);
                        offen.push_back(&put.first->second);
                        ++nachgeladen;
                    }
                }
            }
        }
        if (nachgeladen != 0 || ohneDatei != 0) {
            diag::info(std::to_string(nachgeladen) +
                       " Folgeeffekte vorab geladen, " +
                       std::to_string(ohneDatei) + " fehlen");
        }
    }

    // --- Die Modelle, die an Emittern haengen, gleich mitladen -----------
    //
    // "Emitters don't draw themselves, but they may need to add an
    // attached model" (FxPrimitives.cpp:1375). Den Rauch gibt es seit
    // rc323, hier kommt der Koerper dazu.
    //
    // HIER und nicht beim ersten Gebrauch: eine .md3 mitten in einem Bild
    // zu lesen kostet zwanzig bis fuenfzig Millisekunden, und das haben wir
    // in rc307 bis rc311 dreimal durchgemacht.
    {
        g_app->emitterModels.clear();
        int neu = 0;
        int fehlt2 = 0;
        for (const auto& [pfad, ef] : g_app->effects) {
            (void)pfad;
            for (const efx::Primitive& pr : ef.primitives) {
                if ((pr.flags & efx::kFlagAttachedModel) == 0U ||
                    pr.models.empty()) {
                    continue;
                }
                for (const std::string& roh : pr.models) {
                    std::string low = roh;
                    for (char& c : low) {
                        c = static_cast<char>(
                            std::tolower(static_cast<unsigned char>(c)));
                    }
                    if (g_app->emitterModels.count(low) != 0U) {
                        continue;   // schon da
                    }
                    std::string data;
                    if (!readFromArchives(low, data)) {
                        diag::detail("Emittermodell \"" + low +
                                     "\" nicht gefunden");
                        ++fehlt2;
                        continue;
                    }
                    Md3Model mm;
                    std::string err;
                    if (!readMd3(data, mm, &err)) {
                        diag::detail("Emittermodell \"" + low +
                                     "\" nicht lesbar: " + err);
                        ++fehlt2;
                        continue;
                    }
                    App::EmitterModel em;
                    em.path = low;
                    // Die Texturen der Flaechen hinten anhaengen - derselbe
                    // Weg wie bei den Kartenmodellen.
                    em.shaderBase =
                        static_cast<int>(g_app->textures.byShader.size());
                    for (const Md3Surface& sf : mm.surfaces) {
                        TextureSet::Tex t;
                        loadTextureFor(sf.shader, t);
                        g_app->textures.byShader.push_back(std::move(t));
                    }
                    g_app->md3Files[low] = std::move(mm);
                    g_app->emitterModels[low] = std::move(em);
                    ++neu;
                }
            }
        }
        if (neu != 0 || fehlt2 != 0) {
            diag::info(std::to_string(neu) + " Emittermodelle geladen, " +
                       std::to_string(fehlt2) + " fehlen");
        }
    }

    // --- Was in diesen Effekten steckt, und was davon behaved kann -------
    //
    // Gebeten: "baue bei allem Debug ein, dass du sehen kannst, wenn was
    // nicht stimmt."
    //
    // Genau hier war es noetig. Bis rc271 stand im Protokoll nur, WIE VIELE
    // Effekte laufen - nicht, was in ihnen steht. Dass 156 Klangprimitive
    // ueber alle Dateien nie gespielt wurden, ist mir deshalb erst beim
    // Auszaehlen von aussen aufgefallen, nicht beim Lesen des Protokolls.
    //
    // Die Zeile nennt jetzt je Primitivtyp die Anzahl und ob behaved damit
    // etwas anfaengt. Wer ein Protokoll liest, sieht damit sofort, ob ein
    // fehlender Effekt an einer nicht gefundenen Datei liegt oder an einem
    // Typ, den es noch nicht gibt.
    {
        std::map<efx::PrimitiveType, int> zahl;
        for (const auto& [pfad, ef] : g_app->effects) {
            (void)pfad;
            for (const efx::Primitive& pr : ef.primitives) {
                ++zahl[pr.type];
            }
        }
        auto kann = [](efx::PrimitiveType t) {
            switch (t) {
                case efx::PrimitiveType::Particle:
                case efx::PrimitiveType::OrientedParticle:
                case efx::PrimitiveType::Tail:
                case efx::PrimitiveType::Line:
                case efx::PrimitiveType::Electricity:
                case efx::PrimitiveType::Cylinder:
                    return "gezeichnet";
                case efx::PrimitiveType::FxRunner:
                    // Seit rc333 - er zeichnet nichts, er startet einen
                    // anderen Effekt. "gezeichnet" waere hier irrefuehrend.
                    return "startet seinen playfx-Effekt";
                case efx::PrimitiveType::Decal:
                    // Seit rc332 - als flaches Viereck auf der Flaeche,
                    // ohne das Umschlagen um Kanten (CM_MarkFragments).
                    return "gezeichnet (flach, ohne Kantenumschlag)";
                case efx::PrimitiveType::Sound:
                    return "gespielt";
                case efx::PrimitiveType::Light:
                    return "beleuchtet";
                case efx::PrimitiveType::CameraShake:
                    // effectShakeAt, laengst gebaut - stand trotzdem hier
                    // als "NOCH NICHT".
                    return "wackelt die Kamera";
                case efx::PrimitiveType::Emitter:
                    // Seit rc569 je Teilchen: Modell (useModel) mit
                    // Groessenkurve und Drehung, Fahne (emitFx), Aufprall
                    // und Tod.
                    return "Modell, Fahne und Folgeeffekte";
                case efx::PrimitiveType::ScreenFlash:
                    // Seit rc569: Sprite vor dem Auge wie CFlash::Draw.
                    return "Vollbildblitz";
                default:
                    return "NOCH NICHT";
            }
        };
        // NUR bei Aenderung. Im Protokoll standen dieselben drei Zeilen
        // dreimal hintereinander - einmal je Aufbau der Ansicht. Wer ein
        // Protokoll liest, haelt Wiederholungen fuer einen Hinweis und
        // sucht nach einem Unterschied, den es nicht gibt.
        std::string uebersicht;
        for (const auto& [t, c] : zahl) {
            uebersicht += std::string("   ") + efx::typeName(t) + ": " +
                          std::to_string(c) + " - " + kann(t) + "\n";
        }
        if (uebersicht != g_app->lastEffectSummary) {
            g_app->lastEffectSummary = uebersicht;
            std::size_t at = 0;
            while (at < uebersicht.size()) {
                const std::size_t nl = uebersicht.find('\n', at);
                diag::detail(uebersicht.substr(at, nl - at));
                at = nl + 1;
            }
        }
    }

    // --- Und was von den FLAGS beachtet wird -----------------------------
    //
    // --- Diese Meldung hat GELOGEN, und das ist schlimmer als keine ------
    //
    // Hier stand bis rc357: "der Zeichner beachtet derzeit kein einziges".
    // Das war zum Zeitpunkt des Schreibens richtig und ist seit mehreren
    // Runden falsch. Im Protokoll des Nutzers stand deshalb
    //
    //     Flags, die behaved NICHT auswertet (der Zeichner beachtet
    //     derzeit kein einziges):
    //        6x impactFx - kein Aufpralleffekt
    //        1x useAlpha - Alpha nicht als Helligkeit
    //        2x usePhysics - keine Schwerkraft
    //
    // waehrend alle drei laengst gebaut sind (efxdraw.cpp:378, :455, :765).
    // Eine Meldung, die das Gegenteil der Wahrheit sagt, schickt jeden -
    // mich eingeschlossen - hinter dem falschen Fehler her.
    //
    // Nachgezaehlt: von den zwoelf Flags dieser Liste sind ACHT gebaut.
    //
    // Und der Text war auch sachlich falsch. `usePhysics` ist NICHT die
    // Schwerkraft: FX_APPLY_PHYSICS schaltet in UpdateOrigin
    // (FxPrimitives.cpp:266) den Spurtest und das Abprallen ein. Die
    // Schwerkraft kommt aus einem EIGENEN Feld ueber mAccel und wirkt
    // unabhaengig davon. Genau diese Verwechslung steht schon in der
    // Uebergabe als Falle - "Zahlen gegen die Datei halten, nicht 'es
    // bewegt sich' pruefen".
    //
    // Deshalb wird jetzt beides gemeldet, getrennt: was GEBAUT ist und
    // was FEHLT. Wer nur eine Haelfte meldet, meldet frueher oder spaeter
    // wieder das Falsche.
    //
    // Der Abgleich, Stand jetzt:
    //
    //   TYPEN: die Engine kennt dreizehn (FxScheduler.h:150 ff.). behaved
    //   liest alle dreizehn und setzt seit rc569 alle dreizehn um - siehe
    //   die Uebersicht darueber. (ScreenFlash fehlt in den 391 Dateien aus
    //   assets1, steht aber 19 mal in den 649 Dateien von base und MD.)
    //
    //   FLAGS: gezaehlt ueber die 649 Dateien von base und MD -
    //
    //     gebaut    usePhysics 464x, useModel 144x, useBBox 121x, useAlpha,
    //               impactFx, emitFx, deathFx, depthHack, setShaderTime,
    //               killOnImpact
    //     ohne      expensivePhysics 115x (wir spuren immer),
    //     Wirkung   relative 0x (nur MP-Parser)
    //
    {
        // Drei Staende, nicht zwei.
        //
        // `expensivePhysics` hat mich das gelehrt: es ist weder gebaut noch
        // fehlend. Die Engine erzwingt damit den Spurtest, den sie sonst
        // erst nach einer billigen Vorpruefung macht - und behaved spurt
        // ohnehin immer. Das Flag hat hier schlicht nichts mehr zu tun.
        //
        // Es als "gebaut" einzutragen waere gelogen (der Zeichner fasst es
        // nie an), als "fehlend" auch (es fehlt nichts). Der dritte Stand
        // ist die einzige wahre Auskunft - und der Pruefer
        // tools/lint_efxflags.py kennt ihn.
        enum class Stand { Gebaut, Fehlt, OhneWirkung };
        struct Merkmal {
            std::uint32_t bit;
            const char* name;
            const char* folge;
            Stand stand;
        };
        static constexpr Merkmal merkmale[] = {
            // --- gebaut ---------------------------------------------------
            {efx::kFlagDepthHack, "depthHack",
             "zeichnet vor allem anderen", Stand::Gebaut},
            {efx::kFlagApplyPhysics, "usePhysics",
             "Spurtest und Abprallen", Stand::Gebaut},
            {efx::kFlagEmitFx, "emitFx", "erzeugt Folgeeffekte", Stand::Gebaut},
            {efx::kFlagKillOnImpact, "killOnImpact",
             "endet an der Wand", Stand::Gebaut},
            {efx::kFlagUseAlpha, "useAlpha",
             "Blende in den Alphakanal statt in die Farbe", Stand::Gebaut},
            {efx::kFlagImpactRunsFx, "impactFx",
             "Aufpralleffekt", Stand::Gebaut},
            {efx::kFlagDeathRunsFx, "deathFx", "Endeffekt", Stand::Gebaut},
            {efx::kFlagAttachedModel, "attachedModel", "Modell", Stand::Gebaut},
            // --- ohne Wirkung -------------------------------------------
            // `relative` liest nur der MP-Parser. Im Einzelspieler entsteht
            // FX_RELATIVE allein zur Laufzeit, wenn ein Effekt an einem Bolt
            // gespielt wird (FxScheduler.cpp:1542) - dafuer gibt es seit
            // rc569 EffectInstance::anker/relativ. Das Wort in der Datei hat
            // fuer Movie Duels keine Wirkung, weder im Spiel noch hier.
            {efx::kFlagRelative, "relative",
             "ohne Wirkung - der SP-Parser ueberliest es", Stand::OhneWirkung},
            // `expensivePhysics` ERZWINGT in der Engine den Spurtest;
            // ohne das Flag prueft sie erst billig mit CG_PointContents und
            // spurt nur, wenn dort etwas Festes steht
            // (FxPrimitives.cpp:269). behaved spurt bei eingeschalteter
            // Physik IMMER - das ist strenger, nicht laxer. Das Flag hat
            // hier also nichts mehr zu tun, und "fehlt" waere die falsche
            // Auskunft.
            //
            // Es steht trotzdem in der Liste, damit die Zahl im Protokoll
            // erscheint: 60 Vorkommen in 35 Dateien.
            {efx::kFlagExpensivePhysics, "expensivePhysics",
             "ohne Wirkung - wir spuren ohnehin immer", Stand::OhneWirkung},
            {efx::kFlagSetShaderTime, "setShaderTime",
             "Zeitursprung auf den Beginn des Effekts", Stand::Gebaut},
            // Seit rc569: Strahl mit Kastenvorlauf (efxdraw.cpp, spurFuer) -
            // das Teilchen bleibt um seine Ausdehnung vor der Flaeche
            // stehen. Eine Naeherung, kein echter Kastentest.
            {efx::kFlagUseBBox, "useBBox",
             "Spurtest mit Kastenvorlauf (Naeherung)", Stand::Gebaut},
        };
        // Getrennt zaehlen. Beides zu melden ist der Punkt: eine Liste
        // allein sagt nie, ob sie die gebauten oder die fehlenden meint.
        std::map<std::string, int> gebaut;
        std::map<std::string, int> fehlt;
        std::map<std::string, int> ohne;
        for (const auto& [pfad, ef] : g_app->effects) {
            (void)pfad;
            for (const efx::Primitive& pr : ef.primitives) {
                for (const Merkmal& m : merkmale) {
                    if ((pr.flags & m.bit) != 0U) {
                        auto& wohin = (m.stand == Stand::Gebaut) ? gebaut
                                      : (m.stand == Stand::Fehlt) ? fehlt
                                                                  : ohne;
                        ++wohin[std::string(m.name) + " - " + m.folge];
                    }
                }
            }
        }
        // --- Und was von den TYPEN noch fehlt --------------------------
        //
        // Gebeten: "vergiss nicht, bei der Sache Debug einzubauen, damit du
        // siehst. Es soll alles ins Log schreiben."
        //
        // Die Typenuebersicht darueber sagt, WAS vorkommt. Diese Zeile sagt
        // zusaetzlich, was davon in DIESER Mission noch fehlt - und bei den
        // Decals, welche Einschraenkung gilt.
        {
            int decals = 0;
            int fxrunner = 0;
            for (const auto& [pf, ef] : g_app->effects) {
                (void)pf;
                for (const efx::Primitive& pr : ef.primitives) {
                    if (pr.type == efx::PrimitiveType::Decal) {
                        ++decals;
                    } else if (pr.type == efx::PrimitiveType::FxRunner) {
                        ++fxrunner;
                    }
                }
            }
            if (decals != 0) {
                diag::info(std::to_string(decals) +
                           " Decals: flach auf die Flaeche gelegt. Die "
                           "Engine schneidet sie zusaetzlich gegen die "
                           "Geometrie (CM_MarkFragments) - ueber einer Kante "
                           "steht unseres durch die Wand.");
            }
            if (fxrunner != 0) {
                diag::info(std::to_string(fxrunner) +
                           " FxRunner: spielen ihren playfx-Effekt am "
                           "eigenen Ort (seit rc333).");
            }
        }

        if (!gebaut.empty()) {
            diag::info("Flags, die behaved AUSWERTET:");
            for (const auto& [was, n] : gebaut) {
                diag::info("   " + std::to_string(n) + "x " + was);
            }
        }
        if (!ohne.empty()) {
            diag::info("Flags ohne Wirkung fuer eine Vorschau:");
            for (const auto& [was, n] : ohne) {
                diag::info("   " + std::to_string(n) + "x " + was);
            }
        }
        if (fehlt.empty()) {
            diag::info("Flags: kein unbeachtetes kommt in dieser Mission vor");
        } else {
            diag::info("Flags, die behaved NICHT auswertet:");
            for (const auto& [was, n] : fehlt) {
                diag::info("   " + std::to_string(n) + "x " + was);
            }
        }
    }

}

// Die .md3-Modelle der Karte einlesen.
//
// misc_model_static und misc_model_breakable verweisen ueber "model" auf
// eine Datei. In den Karten der Mod sind das 1549 Entities - Konsolen,
// Kisten, Rohre, Raumschiffe. Ohne sie fehlt der halbe Raum.
//
// Die Texturen kommen ueber den Shadernamen der Flaeche, genau wie bei der
// Karte selbst. Sie werden HINTEN an textures.byShader angehaengt; die
// Nummern der Karte bleiben damit unberuehrt.

void loadMapModels() {
    diag::Step step("Modelle der Karte suchen");
    g_app->mapModels.clear();
    g_app->fehlendeModelle.clear();
    g_app->md3Files.clear();
    // Die Grafikpuffer hingen an diesen Netzen (Schluessel: Adresse).
    for (const auto& [name, netz] : g_app->md3Meshes) {
        gpu::vergissNetz(&netz);
    }
    g_app->md3Meshes.clear();

    int gefunden = 0;
    int fehlt = 0;
    for (const MapEntity& e : g_app->map.entities) {
        const std::string* mp = e.find("model");
        // Die Waffenstaender nehmen ihr Modell NICHT aus dem Schluessel -
        // g_misc_model.cpp setzt es fest: SP_misc_model_gun_rack
        // G_ModelIndex("models/map_objects/kejim/weaponsrack.md3"),
        // spawn_rack_goods (ammo_rack) ".../kejim/weaponsrung.md3".
        // In den MD-Karten steht oft imperial/... (gibt es nicht) oder gar
        // nichts - 270 Staender, die vorher fehlten.
        static const std::string kGunRack = "models/map_objects/kejim/weaponsrack.md3";
        static const std::string kAmmoRack = "models/map_objects/kejim/weaponsrung.md3";
        // Ebenso fest im Code (g_misc.cpp): SP_misc_exploding_crate,
        // SP_misc_gas_tank, SP_misc_crystal_crate, SP_misc_model_bomb_planted
        // und SP_misc_model_beacon. Die Kiste "rocket_crate_dan" in md_afh
        // fehlte so.
        static const std::pair<const char*, std::string> kFest[] = {
            {"misc_exploding_crate", "models/map_objects/nar_shaddar/crate_xplode.md3"},
            {"misc_gas_tank", "models/map_objects/imp_mine/tank.md3"},
            {"misc_crystal_crate", "models/map_objects/imp_mine/crate_open.md3"},
            {"misc_model_bomb_planted", "models/map_objects/factory/bomb_new_deact.md3"},
            {"misc_model_beacon", "models/map_objects/wedge/beacon.md3"},
        };
        bool fest = false;
        if (e.classname == "misc_model_gun_rack") {
            mp = &kGunRack;
        } else if (e.classname == "misc_model_ammo_rack") {
            mp = &kAmmoRack;
        } else {
            for (const auto& [klasse, pfad] : kFest) {
                if (e.classname == klasse) {
                    mp = &pfad;
                    fest = true;
                }
            }
        }
        if (mp == nullptr || mp->size() < 5 || e.origin.empty()) {
            continue;
        }
        // Nur, was die Engine als Modell aufstellt: misc_model_static,
        // _breakable, _gun_rack, _ammo_rack, _beacon ... Ein target_speaker
        // mit "model"-Schluessel (yavin1b) oder misc_siege_item (nur im
        // Mehrspieler) zeichnet das Einzelspieler-Spiel nicht.
        if (e.classname.rfind("misc_model", 0) != 0 && !fest) {
            continue;
        }
        std::string low = *mp;
        for (char& c : low) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (low.size() < 4 || low.compare(low.size() - 4, 4, ".md3") != 0) {
            continue;   // "*12" ist ein Brush-Modell, das laeuft anderswo
        }

        if (g_app->md3Files.find(low) == g_app->md3Files.end()) {
            std::string data;
            if (!readFromArchives(*mp, data)) {
                diag::info("kein Modell: " + *mp);
                g_app->fehlendeModelle.push_back(*mp);
                ++fehlt;
                continue;
            }
            Md3Model mm;
            std::string err;
            if (!readMd3(data, mm, &err)) {
                diag::info("Modell nicht lesbar: " + *mp + " - " + err);
                ++fehlt;
                continue;
            }
            g_app->md3Files[low] = std::move(mm);
        }
        const Md3Model& mm = g_app->md3Files[low];
        if (mm.empty()) {
            continue;   // gueltig, aber ohne Flaechen (reines Tag-Modell)
        }

        App::MapModel item;
        item.path = low;
        item.frames = mm.numFrames;
        item.entity = static_cast<std::size_t>(&e - g_app->map.entities.data());
        (void)std::sscanf(e.origin.c_str(), "%f %f %f", &item.origin[0],
                          &item.origin[1], &item.origin[2]);
        // Dieselbe Regel wie bei den Figuren: "angles" nimmt drei Werte,
        // "angle" nur den Gierwinkel (g_spawn.cpp, F_ANGLEHACK).
        // Winkel und Groesse wie SP_misc_model_static / G_ParseField:
        // "angle" setzt die Gier, "angles" alle drei; "modelscale_vec" je
        // Achse, "modelscale" (ungleich null) ueberschreibt alle drei.
        float einheitlich = 0.0F;
        for (const auto& [k, v] : e.keys) {
            std::string lk = k;
            for (char& c : lk) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (lk == "angles") {
                float a[3] = {0, 0, 0};
                (void)std::sscanf(v.c_str(), "%f %f %f", &a[0], &a[1], &a[2]);
                for (int q = 0; q < 3; ++q) { item.angles[q] = a[q]; }
            } else if (lk == "angle") {
                item.angles[0] = 0.0F;
                item.angles[1] = static_cast<float>(std::atof(v.c_str()));
                item.angles[2] = 0.0F;
            } else if (lk == "modelscale_vec") {
                float s[3] = {1, 1, 1};
                (void)std::sscanf(v.c_str(), "%f %f %f", &s[0], &s[1], &s[2]);
                for (int q = 0; q < 3; ++q) { item.scale[q] = s[q]; }
            } else if (lk == "modelscale") {
                einheitlich = static_cast<float>(std::atof(v.c_str()));
            }
        }
        if (einheitlich != 0.0F) {
            for (int q = 0; q < 3; ++q) { item.scale[q] = einheitlich; }
        }

        item.shaderBase = static_cast<int>(g_app->textures.byShader.size());
        for (const Md3Surface& sf : mm.surfaces) {
            TextureSet::Tex t;
            loadTextureFor(sf.shader, t);
            // Ins Protokoll, WELCHE Flaeche welchen Test bekommt.
            //
            // Gemeldet: "leider sind die Geonosianer immer noch so."
            //
            // Zweimal am falschen Ende gesucht - erst im Figurenzeichner,
            // dann im Flaechenzeichner. Die 34 gri_head.md3 sind
            // KARTENMODELLE und gehen einen dritten Weg. Ohne eine Zeile,
            // die sagt, was bei einem Modell ankommt, faellt so etwas erst
            // beim Hinsehen auf - und dann ist eine Runde vorbei.
            if (t.alphaTest != AlphaTest::None) {
                diag::detail("Kartenmodell " + item.path + ", Flaeche \"" +
                             sf.shader + "\": Alphatest an");
            }
            g_app->textures.byShader.push_back(std::move(t));
        }
        g_app->mapModels.push_back(std::move(item));
        ++gefunden;
    }
    diag::info(std::to_string(gefunden) + " Modelle aufgestellt, " +
               std::to_string(fehlt) + " fehlen, " +
               std::to_string(g_app->md3Files.size()) + " Dateien gelesen");

    // --- Was erst ein use braucht ----------------------------------------
    //
    // Das Schadensmodell "<name>_d1.md3" eines misc_model_breakable, das
    // Benutzmodell "_u1" und die Bruchstuecke (models/chunks/..., oder das
    // eigene "_c1"). Welche es sind, weiss MoverSim (nebenModelle) - hier
    // werden sie VORHER gelesen, nicht mitten im Bild.
    //
    // Fehlt ein _d1, zeigt die Engine nichts: G_ModelIndex vergibt die
    // Nummer trotzdem, R_RegisterModel findet keine Datei, und Modell 0
    // wird nicht gezeichnet. Genau so bleibt es hier weg.
    g_app->nebenModelle.clear();
    int neben = 0;
    for (const std::string& pfad : g_app->moverSim.nebenModelle()) {
        const std::string low = pfad;   // MoverSim liefert schon klein
        if (g_app->md3Files.find(low) == g_app->md3Files.end()) {
            std::string data;
            if (!readFromArchives(low, data)) {
                diag::detail("Nebenmodell fehlt (die Engine zeigt dann nichts): " + low);
                continue;
            }
            Md3Model mm;
            std::string err;
            if (!readMd3(data, mm, &err)) {
                diag::detail("Nebenmodell nicht lesbar: " + low + " - " + err);
                continue;
            }
            g_app->md3Files[low] = std::move(mm);
        }
        const Md3Model& mm = g_app->md3Files[low];
        if (mm.empty()) {
            continue;
        }
        const int basis = static_cast<int>(g_app->textures.byShader.size());
        for (const Md3Surface& sf : mm.surfaces) {
            TextureSet::Tex t;
            loadTextureFor(sf.shader, t);
            g_app->textures.byShader.push_back(std::move(t));
        }
        g_app->nebenModelle[low] = basis;
        ++neben;
    }
    if (neben != 0) {
        diag::info(std::to_string(neben) + " Neben- und Bruchstueckmodelle gelesen");
    }
}

// Alles, was die Ansicht braucht, JETZT bereitstellen.
//
// Vorher geschah das im Zeichnen: drawTimeline() baute die Szene, wenn
// camTrackValid falsch war, und leerte actorAssets; das naechste Bild sah
// die Groessen auseinanderlaufen und lud Figurenmodelle, Effekte und
// .md3-Modelle nach - mitten im Bild. Gemeldet als "erst laedt sie und ich
// sehe die map und wenn ich es dann bewege haengt es kurz und laedt
// nochmal irgendwas".
//
// Der Grund ist einfach: gezeichnet wird nur bei mapDirty. Nach dem Laden
// stand ein Bild, danach passierte nichts mehr - bis eine Bewegung ein
// neues Bild anforderte, und DANN lief das Nachladen los.
//
// Jetzt laeuft es in der Ladephase, wo es hingehoert: der Fortschritt steht
// im Protokoll, und die Ansicht ist fertig, sobald sie erscheint.
void prepareScene();

// --- Griffe und Klingenbilder VORLADEN -----------------------------------
//
// Aus deinem Protokoll:
//
//     Nachgeladen mitten im Bild: Schwertgriff "...saber_mace.glm" - 51.9 ms
//     ... zehn Stueck, zusammen rund 510 ms
//     Zeit je Bild 535.0 ms: ... Szene 511.8 ...
//
// Damit ist das erste Bild erklaert: zehn Schwertgriffe, jeder rund fuenfzig
// Millisekunden. Und waehrend des Abspielens:
//
//     Nachgeladen mitten im Bild: Klingenbild "...purple_glow2" - 11.2 ms
//     Zeit je Bild 53.3 ms: ... Szene 14.5 ...
//
// Die 14,5 aus rc305 waren also die Klingenbilder, nicht die Griffe.
//
// Warum fuenfzig Millisekunden fuer eine kleine Datei? Nicht das Suchen -
// das ist eine Hashtabelle. Es ist das Auspacken, Dekodieren und die
// Bildpyramide der GRIFFTEXTUREN, mehrere je Griff.
//
// Schneller machen laesst sich das kaum. Aber es muss nicht mitten in ein
// Bild fallen: WELCHE Griffe und Farben vorkommen, steht in den .npc der
// Figuren, und die sind hier schon geladen.
//
// WICHTIG, und in rc307 hatte ich es falsch behauptet: das hier laeuft
// INNERHALB der Bildzeitmessung, denn der ganze Ablauf ist bildgetrieben.
// Die halbe Sekunde bleibt also im ERSTEN Bild stehen und wandert nicht in
// den Ladevorgang.
//
// Was es tut, ist trotzdem das Richtige: es holt alles EINMAL beim ersten
// Bild, statt es spaeter mitten ins Abspielen fallen zu lassen. Ein
// langsames erstes Bild beim Missionsstart faellt nicht auf; ein Ruckler
// mitten in einer Szene schon.
void preloadSaberAssets() {
    std::size_t griffe = 0;
    std::size_t bilder = 0;
    // Jedes Modell, das eine Figur im Lauf der Szene in die Hand nehmen
    // kann: Waffen aus SET_WEAPON und der .npc, Griffe aus SET_SABER1/2 und
    // der .npc, Kinomodelle. Actor::modelle fuehrt sie alle.
    for (const Actor& a : g_app->scene.actors) {
        for (const std::string& m : a.modelle) {
            if (!m.empty() && g_app->hiltCache.find(m) == g_app->hiltCache.end()) {
                (void)hiltFor(m);
                ++griffe;
            }
        }
    }

    // --- ALLE Klingenfarben, nicht nur die gefundenen -------------------
    //
    // Mein erster Versuch (rc307) hat nur die Farben aus den .sab der
    // vorhandenen Figuren geholt. Dein Protokoll zeigte danach trotzdem:
    //
    //     Nachgeladen: Klingenbild "...purple_glow2" - 10.8 ms (spielt)
    //     Nachgeladen: Klingenbild "...blue_glow2"   - 10.7 ms (spielt)
    //     Nachgeladen: Klingenbild "...green_glow2"  - 11.2 ms (spielt)
    //
    // Drei Farben, die ich nicht erwischt hatte. WARUM, weiss ich nicht
    // sicher - und ich habe in dieser Sitzung schon zweimal an dieser
    // Stelle falsch geraten.
    //
    // Also nicht noch einmal raten: es gibt genau NEUN Klingenfarben
    // (siehe bladeColorFor). Alle zu holen kostet einmalig gut hundert
    // Millisekunden beim Laden und ist dafuer VOLLSTAENDIG - keine
    // Herleitung, die daneben liegen kann.
    for (const char* farbe : {"red", "orange", "yellow", "green", "blue",
                              "purple", "white", "black", "unstable_red"}) {
        const std::size_t vorher = g_app->texCache.size();
        (void)bladeTexture(farbe, true);
        (void)bladeTexture(farbe, false);
        bilder += g_app->texCache.size() - vorher;
    }
    if (griffe != 0 || bilder != 0) {
        diag::info("Vorgeladen: " + std::to_string(griffe) +
                   " Schwertgriffe, " + std::to_string(bilder) +
                   " Klingenbilder");
    }
}

// --- Die Kino-Skelette der Karte ---------------------------------------------
//
// NPC_stats.cpp:1245: fuer jeden Standard-Humanoiden (G_StandardHumanoid,
// g_client.cpp:1284) laedt die Engine zusaetzlich
//     models/players/_humanoid_<karte>/_humanoid_<karte>.gla
// und dessen animation.cfg als zweite Animationsdatei. Die BOTH_CIN_*-
// Animationen der Zwischensequenzen stehen NUR dort - ohne sie blieb eine
// Figur in academy1 bei "BOTH_CIN_1" einfach stehen (Szenenlauf 27.09.:
// "steht nicht in der .cfg von _humanoid").
//
// Das Grundskelett ist geteilt (ein Eintrag fuer alle Karten), deshalb
// wird der Anhang bei jedem Aufbau auf die AKTUELLE Karte abgeglichen:
// alte Kino-Abschnitte weg, neue dran.
void kinoSkeletteAbgleichen() {
    static const char* const kHumanoid[] = {
        "_humanoid", "JK2anims", "_humanoid_ani", "_humanoid_bdroid", "_humanoid_ben", "_humanoid_cal",
        "_humanoid_clo", "_humanoid_deka", "_humanoid_df2", "_humanoid_dooku", "_humanoid_galen",
        "_humanoid_gon", "_humanoid_grievous", "_humanoid_jabba", "_humanoid_jango", "_humanoid_kotor",
        "_humanoid_luke", "_humanoid_mace", "_humanoid_maul", "_humanoid_md", "_humanoid_melee",
        "_humanoid_obi", "_humanoid_obi3", "_humanoid_pal", "_humanoid_reb", "_humanoid_ren", "_humanoid_rey",
        "_humanoid_sbd", "_humanoid_vader", "_humanoid_yoda", "protocol", "assassin_droid", "saber_droid",
        "hazardtrooper", "rockettrooper", "wampa", "galak_mech", "droideka", "kotor_monster"};
    const auto klein = [](std::string x) {
        for (char& c : x) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (c == '\\') { c = '/'; }
        }
        return x;
    };
    // Die Karte: "maps/academy1.bsp" -> "academy1" (level.mapname ohne Pfad).
    std::string karte = klein(g_app->map.path);
    if (const auto s = karte.find_last_of('/'); s != std::string::npos) { karte = karte.substr(s + 1); }
    if (karte.size() > 4 && karte.compare(karte.size() - 4, 4, ".bsp") == 0) { karte.resize(karte.size() - 4); }
    const std::string kinoName = "models/players/_humanoid_" + karte + "/_humanoid_" + karte;
    // Das Kino-Skelett einmal lesen (auch "nicht da" merken: leeres anim).
    App::Skeleton* kino = nullptr;
    if (!karte.empty()) {
        auto it = g_app->skeletons.find(kinoName);
        if (it == g_app->skeletons.end()) {
            App::Skeleton neu;
            std::string data;
            if (readFromArchives(kinoName + ".gla", data)) {
                std::string err;
                if (!readGla(data, neu.anim, &err)) {
                    diag::detail("Kino-Skelett " + kinoName + " nicht lesbar: " + err);
                }
                neu.sections = animSectionsFor(kinoName);
                diag::info("Kino-Skelett " + kinoName + ": " + std::to_string(neu.anim.numFrames) + " Bilder, " +
                           std::to_string(neu.sections.size()) + " Abschnitte");
            }
            it = g_app->skeletons.emplace(kinoName, std::move(neu)).first;
        }
        if (!it->second.anim.empty()) {
            kino = &it->second;
        }
    }
    for (auto& [name, sk] : g_app->skeletons) {
        if (&sk == kino) {
            continue;
        }
        // Der Skelettname der Engine ist der Ordner: ".../_humanoid_md/_humanoid" -> "_humanoid_md".
        std::string ordner = name;
        if (const auto s = ordner.find_last_of("/\\"); s != std::string::npos) { ordner = ordner.substr(0, s); }
        if (const auto s = ordner.find_last_of("/\\"); s != std::string::npos) { ordner = ordner.substr(s + 1); }
        bool humanoid = false;
        for (const char* h : kHumanoid) {
            humanoid = humanoid || klein(ordner) == klein(h);
        }
        if (!humanoid) {
            continue;
        }
        if (sk.kinoKarte == karte && sk.grundAbschnitte != static_cast<std::size_t>(-1)) {
            continue;   // schon fuer diese Karte
        }
        if (sk.grundAbschnitte == static_cast<std::size_t>(-1)) {
            sk.grundAbschnitte = sk.sections.size();
        }
        sk.sections.resize(sk.grundAbschnitte);
        sk.anim.anhang = nullptr;
        sk.kinoKarte = karte;
        if (kino == nullptr) {
            continue;
        }
        if (kino->anim.bones.size() != sk.anim.bones.size()) {
            diag::detail("Kino-Skelett " + kinoName + ": " + std::to_string(kino->anim.bones.size()) +
                         " Knochen, " + name + " hat " + std::to_string(sk.anim.bones.size()) + " - nicht angehaengt");
            continue;
        }
        sk.anim.anhang = &kino->anim;
        for (AnimEntry e : kino->sections) {
            e.firstFrame += sk.anim.numFrames;
            sk.sections.push_back(std::move(e));
        }
        diag::detail("Kino-Skelett " + kinoName + " an " + name + " angehaengt (" +
                     std::to_string(kino->sections.size()) + " Abschnitte)");
    }
}

void loadActorModels() {
    diag::Step step("Figurenmodelle suchen");
    g_app->actorAssets.assign(g_app->scene.actors.size(), App::ActorAssets{});
    if (g_app->scene.actors.empty()) {
        return;
    }
    if (g_app->gamePaths.empty()) {
        rescanGamePaths();
    }

    // --- Woher kommt die Zeit? -------------------------------------------
    //
    // "Figurenmodelle suchen" dauerte beim Anwender 14,6 s, und das ist der
    // groesste Einzelposten beim Laden einer Mission. Bevor daran etwas
    // geaendert wird, muss klar sein, WELCHER Teil davon es ist - sonst
    // baut man einen Zwischenspeicher fuer die falsche Haelfte.
    //
    // Drei Kandidaten, hier einzeln aufsummiert:
    //   die .npc-Dateien lesen (242 Stueck ueber zwei Spielordner),
    //   die .glm-Modelle lesen (aus den Archiven, also mit Entpacken),
    //   die Skelette lesen (.gla, im Protokoll mit 48000 Bildern).
    //
    // Einzelne Schritte je Figur waeren unlesbar; deshalb Summen.
    using Uhr = std::chrono::steady_clock;
    double msNpc = 0.0;
    double msGlm = 0.0;
    double msTex = 0.0;
    double msGla = 0.0;
    const auto zeitAb = [](Uhr::time_point t0) {
        return std::chrono::duration<double, std::milli>(Uhr::now() - t0)
            .count();
    };

    // Die NPC-Beschreibungen einmal einlesen.
    const auto tNpc = Uhr::now();
    if (!g_app->npcMapRead) {
        g_app->npcMapRead = true;
        // Ueber ALLE Spielordner, nicht nur den zuletzt gewaehlten. Was
        // wo gefunden wurde, kommt ins Protokoll - sonst laesst sich
        // hinterher nicht sagen, ob ein Ordner ueberhaupt durchsucht wurde.
        int files = 0;
        for (const GamePath& gp : g_app->gamePaths) {
            int here = 0;
            for (const FoundFile& f : findByExtension(gp, {".npc"})) {
                std::string text;
                if (readFromArchives(f.name, text)) {
                    parseNpcFile(text, g_app->npcMap);
                    ++here;
                }
            }
            files += here;
            diag::info(gp.directory + ": " + std::to_string(here) +
                       " .npc-Dateien");
        }
        diag::info(std::to_string(files) + " .npc-Dateien in " +
                   std::to_string(g_app->gamePaths.size()) + " Spielordnern -> " +
                   std::to_string(g_app->npcMap.size()) + " NPC-Typen");
        // Der Typ "player" (NPC_Player-Spawner und die Spielerfigur selbst)
        // steht in keiner .npc: NPC_ParseParms nimmt dafuer die Cvars des
        // Spielers - g_char_model "jedi_hm", g_saber "single_1"
        // (g_main.cpp:801, 808). Deren Vorgaben, solange keine .npc
        // "player" etwas anderes sagt.
        // "playerModel player" in einer .npc heisst dasselbe: das Modell des
        // Spielers (NPC_stats.cpp:1364: Q_stricmp("player", ...) ->
        // g_char_model). Movie Duels hat so einen Eintrag - behaved suchte
        // vorher models/players/player/ und fand nichts (270 Szenen ohne
        // Spielerfigur im Szenenlauf vom 27.09.).
        for (auto& [typ, def] : g_app->npcMap) {
            (void)typ;
            std::string pm = def.playerModel;
            for (char& c : pm) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            if (pm == "player") {
                def.playerModel = "jedi_hm";
            }
        }
        // --- Fahrzeuge: das Modell aus ext_data/vehicles/*.veh -------------
        //
        // NPC_ParseParms nimmt bei CLASS_VEHICLE g_vehicleInfo[...].model
        // (NPC_stats.cpp:4562 ff.), gefunden ueber den NPC_type als `name`
        // der .veh (BG_VehicleGetIndex). Der Skin: bei einer Liste der, den
        // soundSet nennt, sonst ein zufaelliger - hier der erste.
        {
            std::map<std::string, std::pair<std::string, std::string>> fahrzeuge;   // name -> model, skin
            for (const GamePath& gp : g_app->gamePaths) {
                for (const FoundFile& f : findByExtension(gp, {".veh"})) {
                    std::string text;
                    if (!readFromArchives(f.name, text)) {
                        continue;
                    }
                    std::string vname;
                    std::string vmodel;
                    std::string vskin;
                    std::istringstream is(text);
                    std::string zeile;
                    while (std::getline(is, zeile)) {
                        std::istringstream zs(zeile);
                        std::string k;
                        std::string v;
                        zs >> k >> v;
                        for (char& c : k) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') { v = v.substr(1, v.size() - 2); }
                        if (k == "name" && vname.empty()) { vname = v; }
                        if (k == "model" && vmodel.empty()) { vmodel = v; }
                        if (k == "skin" && vskin.empty()) { vskin = v; }
                    }
                    for (char& c : vname) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                    if (!vname.empty() && !vmodel.empty()) {
                        fahrzeuge[vname] = {vmodel, vskin.substr(0, vskin.find('|'))};
                    }
                }
            }
            for (auto& [typ, def] : g_app->npcMap) {
                if (!def.playerModel.empty()) {
                    continue;
                }
                std::string kl = def.klasse;
                for (char& c : kl) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
                if (kl != "CLASS_VEHICLE") {
                    continue;
                }
                const auto v = fahrzeuge.find(typ);
                if (v == fahrzeuge.end()) {
                    continue;
                }
                def.playerModel = v->second.first;
                def.customSkin = v->second.second.empty() ? "default" : v->second.second;
            }
            diag::info(std::to_string(fahrzeuge.size()) + " Fahrzeuge (.veh)");
        }
        if (g_app->npcMap.find("player") == g_app->npcMap.end()) {
            NpcDef spieler;
            spieler.playerModel = "jedi_hm";
            spieler.saber = "single_1";
            spieler.weapon = "WP_SABER";
            g_app->npcMap["player"] = spieler;
        }

        // --- Und die Klingen -------------------------------------------
        //
        // Gemeldet: "weder die Lichtschwerter werden geladen und
        // ordentlich angezeigt als auch die Sounds."
        //
        // Derselbe Weg wie bei den .npc-Dateien, eine Stufe weiter: der
        // `saber`-Schluessel eines NPC nennt einen Eintrag in einer
        // .sab-Datei, und der traegt Modell, Klaenge, Farbe und Laenge.
        int sabs = 0;
        for (const GamePath& gp : g_app->gamePaths) {
            for (const FoundFile& f : findByExtension(gp, {".sab"})) {
                std::string text;
                if (readFromArchives(f.name, text)) {
                    parseSaberFile(text, g_app->saberMap);
                    ++sabs;
                }
            }
        }
        diag::info(std::to_string(sabs) + " .sab-Dateien -> " +
                   std::to_string(g_app->saberMap.size()) + " Klingen");

        // --- Und die Waffen ---------------------------------------------
        //
        // Gefragt: "wo bekommen wir das Projektil her?" Von hier.
        // ext_data/weapons.dat nennt je Waffe das Modell und den
        // Muendungsblitz; der Blitz ist eine gewoehnliche .efx, und das
        // Modell traegt den Tag "tag_flash", an dem die Muendung sitzt.
        std::string wdat;
        if (readFromArchives("ext_data/weapons.dat", wdat)) {
            parseWeaponsDat(wdat, g_app->weaponMap);
        }
        int mitBlitz = 0;
        int ohneBlitz = 0;
        for (const auto& [typ, def] : g_app->weaponMap) {
            (void)typ;
            if (def.muzzleEffect.empty()) { ++ohneBlitz; } else { ++mitBlitz; }
        }
        diag::info(std::to_string(g_app->weaponMap.size()) +
                   " Waffen aus weapons.dat, " + std::to_string(mitBlitz) +
                   " mit Muendungsblitz, " + std::to_string(ohneBlitz) +
                   " ohne");
        if (g_app->weaponMap.empty()) {
            diag::info("   ext_data/weapons.dat NICHT GEFUNDEN - ohne sie "
                       "gibt es keine Muendungsblitze und keine Geschosse");
        }
    }
    msNpc = zeitAb(tNpc);

    // --- Vorwaermen in drei Stufen ---------------------------------------
    //
    // Gemessen: Texturen 1265 ms, Skelette 948 ms - beides der Reihe nach.
    // Beides ist Lesen und Entziffern ohne gemeinsamen Zustand, also
    // verteilbar; derselbe Griff wie bei den Kartentexturen in rc189.
    //
    // Drei Stufen, und die Reihenfolge ist der Kern:
    //
    //   1. Modelle lesen (verteilt). Erst daraus weiss man, WELCHE Texturen
    //      und WELCHES Skelett gebraucht werden.
    //   2. Die dabei gesammelten Namen VEREINIGEN.
    //   3. Erst dann verteilt entziffern - je Name genau einmal.
    //
    // Warum nicht gleich alles je Modell parallel? Weil sich zwoelf Modelle
    // vier Skelette teilen. Zwoelf Faeden wuerden dieselbe .gla mehrfach
    // laden - und das waere LANGSAMER als der Reihe nach. Erst vereinigen,
    // dann verteilen.
    //
    // Die Ergebnisse landen in denselben Tabellen, die der Hauptdurchgang
    // danach abfragt; er findet also alles fertig vor. Die Modelle werden
    // dabei ein zweites Mal gelesen (49 ms fuer alle), und das ist der
    // Preis dafuer, dass es nur EINE Fassung des Zusammenbaus gibt.
    {
        // Stufe 0: welche Verzeichnisse ueberhaupt? Billig, der Reihe nach.
        std::vector<std::string> dirs;
        for (const Actor& a : g_app->scene.actors) {
            if (a.npcType.empty()) {
                continue;
            }
            std::string k = a.npcType;
            for (char& c : k) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            const auto itn = g_app->npcMap.find(k);
            if (itn == g_app->npcMap.end() || itn->second.playerModel.empty()) {
                continue;   // auch MD3-Figuren (legsmodel) - siehe prepareScene
            }
            const std::string dir = "models/players/" + itn->second.playerModel;
            // Der Schluessel traegt den SKIN mit.
            //
            // Gemeldet: "was noch fehlt, sind richtige Skinvarianten von den
            // Modellen."
            //
            // In assets1 nennen 73 NPC-Typen einen eigenen Skin, und
            // darunter ist DASSELBE Modell mit verschiedenen: cultist in
            // braun und rot, reborn_new in blau und rot. Ein Puffer, der
            // nur nach dem Modellnamen schluesselt, gibt dem zweiten den
            // Skin des ersten - und zwar dem, der zufaellig spaeter dran
            // ist.
            const std::string schluessel = dir + "|" + itn->second.customSkin;
            if (g_app->modelCache.count(schluessel) != 0U) {
                continue;
            }
            if (std::find(dirs.begin(), dirs.end(), schluessel) ==
                dirs.end()) {
                dirs.push_back(schluessel);
            }
        }

        // Ein Arbeitstrupp, der eine Aufgabe ueber einen gemeinsamen Zaehler
        // abarbeitet - wie beim Vorwaermen der Kartentexturen.
        const auto trupp = [](std::size_t auftraege,
                              const std::function<void(std::size_t, std::size_t)>& tun) {
            if (auftraege == 0) {
                return std::size_t{0};
            }
            const unsigned kerne =
                std::max(1U, std::thread::hardware_concurrency());
            const std::size_t faeden =
                std::min<std::size_t>(kerne, auftraege);
            std::atomic<std::size_t> naechste{0};
            const auto lauf = [&](std::size_t fach) {
                for (;;) {
                    const std::size_t k = naechste.fetch_add(1);
                    if (k >= auftraege) {
                        return;
                    }
                    tun(k, fach);
                }
            };
            std::vector<std::thread> mannschaft;
            mannschaft.reserve(faeden);
            for (std::size_t f = 1; f < faeden; ++f) {
                mannschaft.emplace_back(lauf, f);
            }
            lauf(0);
            for (std::thread& th : mannschaft) {
                th.join();
            }
            return faeden;
        };

        // Stufe 1: die Modelle lesen - nur, um an die Namen zu kommen.
        std::vector<GlmModel> modelle(dirs.size());
        std::vector<char> heil(dirs.size(), 0);
        trupp(dirs.size(), [&](std::size_t k, std::size_t) {
            std::string data;
            // dirs[] traegt jetzt "Pfad|Skin". Fuer die Datei zaehlt nur
            // der Pfad; der Skin entscheidet gleich, WELCHE .skin gelesen
            // wird.
            const std::size_t strich = dirs[k].find('|');
            const std::string pfad = dirs[k].substr(0, strich);
            const std::string skinName =
                (strich == std::string::npos) ? std::string("default")
                                              : dirs[k].substr(strich + 1);
            if (!readFromArchives(pfad + "/model.glm", data)) {
                return;
            }
            std::string err;
            if (!readGlm(data, modelle[k], &err)) {
                return;
            }
            // Die HAUT dazu - und genau hier lag der Fehler im ersten
            // Anlauf.
            //
            // Die Texturnamen stehen nicht im Modell, sondern in der
            // .skin: erst applySkin() traegt sie in die Flaechen ein. Ohne
            // diesen Schritt waren alle Namen leer, es gab nichts zu
            // vereinigen, und das Protokoll sagte es klar - "0 Bilder, 0
            // Faeden". Die Skelette wurden trotzdem vorgewaermt, weil ihr
            // Pfad im Kopf des Modells steht.
            //
            // Dieselbe Reihenfolge wie im Hauptdurchgang: erst
            // model_default.skin, sonst model.skin.
            // Erst der genannte Skin, dann default, dann model.skin.
            //
            // Die Reihenfolge ist die der Engine: sie baut
            // model_<customSkin>.skin (NPC_stats.cpp:1908) und faellt auf
            // die Vorgabe "default" zurueck, wenn keiner genannt ist
            // (ebenda:1597). Der dritte Schritt ist unserer - alte Modelle
            // haben nur model.skin.
            for (const std::string& which :
                 {"model_" + skinName + ".skin",
                  std::string("model_default.skin"),
                  std::string("model.skin")}) {
                std::string skin;
                if (readFromArchives(pfad + "/" + which, skin)) {
                    applySkin(skin, modelle[k]);
                    break;
                }
            }
            heil[k] = 1;
        });

        // Stufe 2: vereinigen.
        std::vector<std::string> texNamen;
        std::vector<std::string> glaNamen;
        for (std::size_t k = 0; k < dirs.size(); ++k) {
            if (heil[k] == 0) {
                continue;
            }
            for (const auto& sf : modelle[k].surfaces) {
                if (sf.texture.empty()) {
                    continue;
                }
                if (std::find(texNamen.begin(), texNamen.end(), sf.texture) ==
                    texNamen.end()) {
                    texNamen.push_back(sf.texture);
                }
            }
            std::string an = modelle[k].animFile;
            if (an.empty()) {
                an = "models/players/_humanoid/_humanoid";
            }
            if (g_app->skeletons.count(an) == 0U &&
                std::find(glaNamen.begin(), glaNamen.end(), an) ==
                    glaNamen.end()) {
                glaNamen.push_back(an);
            }
        }

        // Stufe 3a: die Texturen - je Name genau einmal.
        std::vector<std::pair<std::string, TextureSet::Tex>> texErgebnis(
            texNamen.size());
        std::vector<char> texOk(texNamen.size(), 0);
        // ABSICHTLICH nur die direkten Kandidaten, nicht der Umweg ueber
        // die Shadertabelle. Die wird moeglicherweise erst beim ersten
        // Zugriff aufgebaut, und ein Wettlauf darauf waere teurer als die
        // wenigen Bilder, die dann im Hauptdurchgang der Reihe nach
        // aufgeloest werden - im Protokoll die als "ueber Shaderskripte"
        // gezaehlten, bei md_am_sith fuenf Stueck.
        const std::size_t texFaeden =
            trupp(texNamen.size(), [&](std::size_t k, std::size_t) {
                const std::string key =
                    image::mappingName(texNamen[k]) + "@256";
                // Schon da? Dann nicht noch einmal entziffern - derselbe
                // Fehler wie bei den Kartentexturen, nur hier.
                if (g_app->texCache.count(key) != 0U) {
                    return;
                }
                for (const std::string& name : textureCandidates(texNamen[k])) {
                    std::string data;
                    if (!readFromArchives(name, data)) {
                        continue;
                    }
                    const image::Image im = image::decode(
                        reinterpret_cast<const unsigned char*>(data.data()),
                        data.size());
                    if (!im.ok || im.width <= 0 || im.height <= 0) {
                        continue;
                    }
                    TextureSet::Tex t;
                    image::shrinkTo(im, 256, t.width, t.height, t.rgba);
                    texErgebnis[k] = {key, std::move(t)};
                    texOk[k] = 1;
                    return;
                }
            });
        std::size_t texNeu = 0;
        for (std::size_t k = 0; k < texNamen.size(); ++k) {
            if (texOk[k] != 0 &&
                g_app->texCache.emplace(texErgebnis[k].first,
                                        texErgebnis[k].second).second) {
                ++texNeu;
            }
        }

        // Stufe 3b: die Skelette - vier grosse .gla, vier Faeden.
        std::vector<GlaAnimation> glaErgebnis(glaNamen.size());
        std::vector<std::string> glaArchiv(glaNamen.size());
        std::vector<char> glaOk(glaNamen.size(), 0);
        trupp(glaNamen.size(), [&](std::size_t k, std::size_t) {
            std::string data;
            if (!readFromArchives(glaNamen[k] + ".gla", data, &glaArchiv[k])) {
                return;
            }
            std::string err;
            if (readGla(data, glaErgebnis[k], &err)) {
                glaOk[k] = 1;
            }
        });
        for (std::size_t k = 0; k < glaNamen.size(); ++k) {
            if (glaOk[k] == 0) {
                continue;
            }
            App::Skeleton sk;
            sk.anim = std::move(glaErgebnis[k]);
            sk.sections = animSectionsFor(glaNamen[k]);
            diag::info("Skelett " + glaNamen[k] + ": " +
                       std::to_string(sk.anim.bones.size()) + " Knochen, " +
                       std::to_string(sk.anim.numFrames) + " Bilder, " +
                       std::to_string(sk.sections.size()) + " Abschnitte, aus " +
                       glaArchiv[k]);
            g_app->skeletons[glaNamen[k]] = std::move(sk);
        }

        if (!dirs.empty()) {
            diag::info("Vorgewaermt: " + std::to_string(dirs.size()) +
                       " Modelle, " + std::to_string(texNeu) + " Bilder, " +
                       std::to_string(glaNamen.size()) + " Skelette, " +
                       std::to_string(texFaeden) + " Faeden");
        }
    }

    int found = 0;
    // Mitzaehlen, WO die Kette reisst:
    //   NPC_type fehlt -> kein Eintrag in den .npc -> kein Modell im Archiv
    //   -> Modell nicht lesbar
    int noType = 0;
    int noNpc = 0;
    int noModel = 0;
    int badModel = 0;
    for (std::size_t i = 0; i < g_app->scene.actors.size(); ++i) {
        const Actor& a = g_app->scene.actors[i];
        App::ActorAssets& assets = g_app->actorAssets[i];
        assets.tried = true;
        if (a.npcType.empty()) {
            // MIT NAMEN. "10 von 11 Figuren mit Modell" sagt nicht, WELCHE
            // fehlt - und genau die will man nachsehen. Der Grund steht
            // dabei, damit klar ist, an welcher Stelle die Kette reisst.
            diag::info("ohne Modell: " + a.name +
                       " - kein NPC_type (kein NPC_*-Spawner mit diesem "
                       "Namen in der Karte)");
            ++noType;
            continue;
        }
        std::string key = a.npcType;
        for (char& c : key) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const auto it = g_app->npcMap.find(key);
        if (it == g_app->npcMap.end()) {
            diag::info("ohne Modell: " + a.name + " - NPC_type \"" + a.npcType +
                       "\" steht in keiner .npc");
            ++noNpc;
            continue;
        }
        if (it->second.playerModel.empty() && !it->second.legsModel.empty()) {
            // Eine MD3-Figur: sie zeichnet der Kartenmodell-Weg (prepareScene).
            diag::detail("Figur " + a.name + ": MD3-Teile models/players/" + it->second.legsModel +
                         "/lower.md3");
            continue;
        }
        const std::string dir = "models/players/" + it->second.playerModel;

        // Schon geladen? Dann nur noch abschreiben.
        //
        // In einer Massenszene sind die meisten Figuren dasselbe Modell.
        // Im Protokoll von shank stand rund zwanzigmal hintereinander "42
        // gefunden, 0 nicht gefunden" - jedes Mal derselbe Sturmtruppler,
        // neu gelesen, neu entpackt, neu verkleinert.
        const std::string schluessel = dir + "|" + it->second.customSkin;
        const auto cached = g_app->modelCache.find(schluessel);
        if (cached != g_app->modelCache.end()) {
            assets = cached->second;
            ++found;
            continue;
        }

        std::string data;
        const auto tGlm = Uhr::now();
        if (!readFromArchives(dir + "/model.glm", data)) {
            // Den gesuchten Pfad SAMT Quelle nennen: der NPC-Typ steht im
            // Skript, der Modellname in der .npc, und das Modell in einer
            // dritten Datei. Ohne alle drei Namen ist nicht zu sagen, wo es
            // hakt.
            diag::info("ohne Modell: " + a.name + " - " + a.npcType + " -> " +
                       it->second.playerModel + " -> " + dir + "/model.glm fehlt");
            ++noModel;
            continue;
        }
        GlmModel gm;
        std::string err;
        if (!readGlm(data, gm, &err)) {
            diag::info("ohne Modell: " + a.name + " - " + dir + " nicht lesbar: " + err);
            ++badModel;
            continue;
        }
        // Die uebliche Haut daneben.
        // Erst der genannte Skin, dann default, dann model.skin - dieselbe
        // Reihenfolge wie im Vorwaermen weiter oben. Zwei Stellen, die
        // dasselbe tun, muessen es GLEICH tun; laufen sie auseinander,
        // haengt das Aussehen davon ab, welcher Weg zuerst drankam.
        bool skinGefunden = false;
        for (const std::string& which :
             {"model_" + it->second.customSkin + ".skin",
              std::string("model_default.skin"), std::string("model.skin")}) {
            std::string skin;
            if (readFromArchives(dir + "/" + which, skin)) {
                applySkin(skin, gm);
                skinGefunden = true;
                if (which != "model_" + it->second.customSkin + ".skin") {
                    diag::detail("Figur " + a.name + ": Skin \"" +
                                 it->second.customSkin + "\" nicht gefunden, " +
                                 which + " genommen");
                }
                break;
            }
        }
        if (!skinGefunden) {
            diag::detail("Figur " + a.name + ": KEINE .skin unter " + dir +
                         " - die Flaechen bleiben ohne Texturnamen");
        }
        assets.model = std::move(gm);
        msGlm += zeitAb(tGlm);
        // Texturen dazu, ueber denselben Weg wie beim einzelnen Modell.
        const auto tTex = Uhr::now();
        assets.textures = texturesFor(assets.model);
        msTex += zeitAb(tTex);
        // Und das Skelett, dessen Pfad im Kopf des Modells steht.
        const auto tGla = Uhr::now();
        assets.skeleton = skeletonFor(assets.model);
        msGla += zeitAb(tGla);
        // --- Knochenzahl von Modell und Skelett -------------------------
        //
        // R_LoadMDXM (tr_ghoul2.cpp:3683) laedt ein Modell NICHT, dessen
        // Knochenzahl nicht zur .gla passt ("has different bones than
        // anim") - ausser es ist ein JK2-Modell, das umgelegt wird (das tut
        // readGlm). Gefunden mit tests/meshmasse.cpp: models/players/sbd
        // aus zzzz_zz_MD_Models_PT.pk3 nennt _humanoid (53) und hat 43.
        // behaved zeichnet es trotzdem, sagt es aber - so wie die Engine.
        if (assets.skeleton != nullptr && !assets.model.empty() &&
            static_cast<int>(assets.skeleton->anim.bones.size()) != assets.model.numBones) {
            diag::info("Modell " + assets.model.name + " hat " + std::to_string(assets.model.numBones) +
                       " Knochen, sein Skelett " + assets.model.animFile + " " +
                       std::to_string(assets.skeleton->anim.bones.size()) +
                       " - die Engine laedt es so nicht (R_LoadMDXM: different bones than anim)");
        }
        // --- Welche Figur welches Skelett bekommen hat ------------------
        //
        // Gemeldet: "die Jango-Figur hat keine Animation, sie steht in der
        // Root-Pose fest."
        //
        // Das Protokoll zeigte bisher NUR, welche Skelette geladen wurden -
        // nicht, welche Figur welches abbekommt. Damit liess sich der Fall
        // nicht auseinanderhalten:
        //
        //   kein Skelett          -> gezeichnet wird die Grundstellung des
        //                            .glm, und das IST die Root-Pose
        //   Skelett, aber der
        //   Animationsname fehlt
        //   in seiner .cfg        -> frameForAnimationIn gibt -1 zurueck,
        //                            und wir fallen auf die vorige zurueck
        //
        // Beides sieht auf dem Bildschirm gleich aus und hat verschiedene
        // Ursachen. Die Zeile hier nennt Ross und Reiter.
        //
        // Ins Detailprotokoll, nicht ins normale: bei 64 Figuren waeren das
        // 64 Zeilen, und im Normalfall interessiert keine davon.
        if (assets.skeleton == nullptr) {
            diag::detail("Figur " + a.name + " (" + dir +
                         "): KEIN Skelett - wird in der Grundstellung "
                         "gezeichnet. Kopf des .glm nennt \"" +
                         assets.model.animFile + "\"");
        } else {
            diag::detail("Figur " + a.name + " (" + dir + "): Skelett \"" +
                         assets.model.animFile + "\", " +
                         std::to_string(assets.skeleton->sections.size()) +
                         " Abschnitte");
        }
        g_app->modelCache[schluessel] = assets;
        ++found;
    }
    diag::info(std::to_string(found) + " von " +
               std::to_string(g_app->scene.actors.size()) + " Figuren mit Modell");
    {
        char zeiten[192];
        std::snprintf(zeiten, sizeof(zeiten),
                      "Zeit: .npc %.0f ms, .glm %.0f ms, Texturen %.0f ms, "
                      "Skelette %.0f ms",
                      msNpc, msGlm, msTex, msGla);
        diag::info(zeiten);
    }

    // Den Grund im Klartext merken, statt eines pauschalen Hinweises.
    //
    // "Keine Modelle gefunden - Spielordner hinzufuegen" ist nutzlos, wenn
    // Spielordner da sind. Die Kette hat vier Glieder, und welches gerissen
    // ist, sagt genau, was zu tun waere.
    char why[224];
    if (found > 0) {
        std::snprintf(why, sizeof(why), tr(Str::ActorsFound), found,
                      static_cast<int>(g_app->scene.actors.size()));
    } else if (g_app->npcMap.empty()) {
        std::snprintf(why, sizeof(why), "%s", tr(Str::ActorsNoNpcFiles));
    } else if (noType > 0 && noNpc == 0 && noModel == 0) {
        std::snprintf(why, sizeof(why), tr(Str::ActorsNoType), noType);
    } else if (noNpc > 0) {
        std::snprintf(why, sizeof(why), tr(Str::ActorsNoNpcEntry), noNpc,
                      static_cast<int>(g_app->npcMap.size()));
    } else if (noModel > 0) {
        std::snprintf(why, sizeof(why), tr(Str::ActorsNoGlm), noModel);
    } else if (badModel > 0) {
        std::snprintf(why, sizeof(why), tr(Str::ActorsBadGlm), badModel);
    } else if (g_app->scene.actors.empty()) {
        // GAR KEINE Figuren - bisher der einzige Fall, der nichts sagte.
        //
        // Eine Figur entsteht aus einem affect-Block. Hat das Skript keinen,
        // gibt es nichts zu zeigen, und das ist kein Fehler.
        std::snprintf(why, sizeof(why), "%s", tr(Str::ActorsNoAffect));
    } else {
        // Alle Faelle sind oben abgedeckt; bleibt trotzdem einer uebrig,
        // soll er sichtbar werden statt still zu verschwinden.
        std::snprintf(why, sizeof(why), tr(Str::ActorsFound), found,
                      static_cast<int>(g_app->scene.actors.size()));
    }
    g_app->actorWhy = why;
    // `actorWhy` ist ANZEIGETEXT und folgt der Sprache der Oberflaeche.
    // In shanks rc549-Protokoll stand deshalb mitten im deutschen Ablauf
    // eine englische Zeile:
    //
    //     Figurenmodelle: 27 of 31 figures have a model
    //
    // Das Protokoll ist sonst durchgehend fest deutsch. Der Zusatz sagt,
    // warum diese eine Zeile die Sprache wechselt - sonst sucht man den
    // Fehler dort, wo keiner ist.
    diag::info("Figurenmodelle (Anzeigetext, Sprache der Oberflaeche): " +
               g_app->actorWhy);

    // Wenn KEINE Figur einen NPC_type hat, liegt es fast nie am Skript,
    // sondern an den Entities. Die drei Zahlen hier sagen, an welcher.
    //
    // Der Fall, der dazu gefuehrt hat: ein Protokoll meldete "0 von 64
    // Figuren mit Modell", und das legte den Verdacht auf fehlende .glm -
    // dabei kam die Kette schon eine Stufe frueher nicht in Gang. Ohne
    // diese Zeilen liess sich das aus der Ferne nicht unterscheiden.
    if (noType > 0 && found == 0) {
        int npcEnts = 0;
        int withType = 0;
        int plainSpawner = 0;
        for (const MapEntity& e : g_app->map.entities) {
            if (e.classname.rfind("NPC_", 0) != 0) {
                continue;
            }
            ++npcEnts;
            if (e.classname == "NPC_spawner") {
                ++plainSpawner;
            }
            if (e.find("NPC_type") != nullptr) {
                ++withType;
            }
        }
        diag::info("   Entities der Karte: " +
                   std::to_string(g_app->map.entities.size()) + ", davon NPC_*: " +
                   std::to_string(npcEnts) + " (NPC_spawner: " +
                   std::to_string(plainSpawner) + "), mit NPC_type: " +
                   std::to_string(withType));
        if (npcEnts == 0) {
            diag::info("   kein einziges NPC_* - stehen die Figuren in einer "
                       ".ent neben der .bsp? Dann wurde sie nicht geladen.");
        }
    }
    g_app->mapDirty = true;
}

// Das Skelett zum geladenen Modell holen.
//
// Der Pfad steht im Modell selbst - "models/players/_humanoid/_humanoid".
// Wir suchen erst dort in den Archiven und fragen nur nach, wenn nichts zu
// finden ist. Die animation.cfg liegt daneben und nennt die Abschnitte.
void loadSkeletonForModel() {
    diag::Step step("Skelett laden");
    if (g_app->model.animFile.empty()) {
        return;
    }
    std::string data;
    const std::string gla = g_app->model.animFile + ".gla";
    if (!readFromArchives(gla, data)) {
        const std::string p = platform::openFileDialog(
            tr(Str::ModelLoadGla), "Ghoul2 skeleton (*.gla)|*.gla|All files (*.*)|*.*",
            g_app->settings.lastDir);
        if (p.empty()) {
            return;
        }
        data = slurp(p);
        // Die animation.cfg liegt daneben.
        const std::string cfg = slurp(directoryOf(p) + "/animation.cfg");
        if (!cfg.empty()) {
            g_app->animList = parseAnimationCfg(cfg);
        }
    } else {
        // Dieselbe Reihenfolge wie bei den Figuren - erst <name>/<name>.cfg,
        // dann <name>/animation.cfg. Vorher stand hier nur der zweite Weg,
        // und damit gab es zwei verschiedene Regeln im selben Programm.
        g_app->animList = animSectionsFor(g_app->model.animFile);
    }

    GlaAnimation a;
    std::string err;
    if (!readGla(data, a, &err)) {
        step.fail(err);
        platform::showError(gla + "\n" + err, tr(Str::AppTitle));
        return;
    }
    diag::info(std::to_string(a.bones.size()) + " Knochen, " +
               std::to_string(a.numFrames) + " Bilder, " +
               std::to_string(g_app->animList.size()) + " Abschnitte");
    g_app->anim = std::move(a);
    g_app->animIndex = -1;
    g_app->animFrame = 0;
    g_app->modelDirty = true;
}

// Die Modellansicht.
//
// Ein .glm zeigt eine Figur in ihrer RUHELAGE. Die Bewegung steht in der
// .gla, deren Pfad im Kopf des Modells steht - bei model.glm ist das
// "models/players/_humanoid/_humanoid". Ohne sie ist das Modell ein
// Standbild; das steht auch so unter der Ansicht, damit niemand ein fehlendes
// Merkmal fuer einen Fehler haelt.
// Die Werkzeugleiste der Modellansicht. Steht ueber dem Rahmen, damit die
// Ansicht auf gleicher Hoehe mit dem Skriptfenster beginnt.
void drawModelToolbar() {
    if (ImGui::Button(tr(Str::OpenGlm))) { openModelFile(); }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::OpenGlmPk3))) {
        if (g_app->gamePaths.empty()) { rescanGamePaths(); }
        g_app->pk3Kind = 2;
        refreshPk3List();
        g_app->pk3Open = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(g_app->model.empty());
    if (ImGui::Button(tr(Str::OpenSkin))) { openSkinFile(); }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::ModelLoadGla))) { openSkeletonFile(); }
    ImGui::EndDisabled();
}

void drawModelView() {
    // Ohne Modell nur der Hinweis - die Bedienleiste steht trotzdem, damit
    // die Anordnung nicht springt, sobald etwas geladen wird.
    const bool haveModel = !g_app->model.empty();
    // Platz fuer die Bedienleiste unten freihalten.
    // Kein Platz mehr fuer die Bedienung reservieren: sie steht jetzt UNTER
    // allen drei Spalten und nicht mehr hier. Das Bild bekommt die ganze
    // Hoehe der Spalte.
    const float panelH = 0.0F;
    if (!haveModel) {
        const ImVec2 free = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("modelempty",
                          ImVec2{free.x, std::max(free.y - panelH, 40.0F)});
        ImGui::TextDisabled("%s", tr(Str::ModelNone));
        ImGui::EndChild();
        return;
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int w = std::max(64, static_cast<int>(avail.x));
    const int h = std::max(64, static_cast<int>(avail.y - panelH));

    // Die Ansicht in einen EIGENEN Bereich: nur hier wirkt die Maus. Sonst
    // zoomt das Rad auch dann, wenn man in der Animationsliste blaettert.
    // OHNE Innenabstand: das Bild ist genau so gross wie sein Kind. Mit
    // Abstand passt es nicht hinein, ImGui setzt einen Rollbalken daneben,
    // und der Rahmen endet sichtbar hoeher als die Spalten rechts davon.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    ImGui::BeginChild("modelcanvas", ImVec2{static_cast<float>(w),
                                            static_cast<float>(h)},
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    const bool hovered = ImGui::IsWindowHovered();
    ImGuiIO& io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        if (io.KeyAlt) {
            g_app->modelOrbit.turn(g_app->modelCam, -io.MouseDelta.x * 0.4F,
                                   -io.MouseDelta.y * 0.4F);
        } else {
            g_app->modelOrbit.pan(g_app->modelCam, io.MouseDelta.x, io.MouseDelta.y);
        }
    }
    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // Bei einem einzelnen Modell ist die linke Taste frei - da erwartet
        // man das Drehen, wie in jedem Modellbetrachter.
        g_app->modelOrbit.turn(g_app->modelCam, -io.MouseDelta.x * 0.4F,
                               -io.MouseDelta.y * 0.4F);
    }
    if (hovered && io.MouseWheel != 0.0F) {
        g_app->modelOrbit.zoom(g_app->modelCam, io.MouseWheel);
    }

    if (g_app->modelImage.width != w || g_app->modelImage.height != h) {
        g_app->modelImage.width = w;
        g_app->modelImage.height = h;
        g_app->modelDirty = true;
    }
    const float now[7] = {g_app->modelCam.pos[0], g_app->modelCam.pos[1],
                          g_app->modelCam.pos[2], g_app->modelCam.angles[0],
                          g_app->modelCam.angles[1], g_app->modelCam.angles[2],
                          g_app->modelOrbit.distance};
    for (int k = 0; k < 7; ++k) {
        if (std::fabs(now[k] - g_app->lastModelCam[k]) > 0.0001F) {
            g_app->modelDirty = true;
        }
        g_app->lastModelCam[k] = now[k];
    }

    // --- Ueber die Grafikkarte, in ein eigenes Renderziel -----------------
    //
    // Bis rc568 zeichnete das Modellfenster der Software-Rasterer
    // (renderModel). Jetzt derselbe Figuren-Shader wie in der Karte, mit dem
    // Verlauf des alten Betrachters als Hintergrund.
    if (!gpu::verfuegbar()) {
        ImGui::TextWrapped("%s", tr(Str::MapNeedsD3d));
    } else {
        if (g_app->modelDirty && gpu::bereiteModellZiel(w, h)) {
            std::vector<BoneMatrix> knochen;
            if (!g_app->anim.bones.empty() && g_app->anim.numFrames > 0) {
                std::vector<BoneMatrix> welt;
                g_app->anim.worldMatrices(
                    std::clamp(g_app->animFrame, 0, g_app->anim.numFrames - 1), welt);
                gpu::baueKnochenmatrizen(welt, g_app->anim.bones, knochen);
            }
            float vp[16];
            gpu::baueViewProj(g_app->modelCam, w, h, 1.0F, 8192.0F, vp);
            std::string fehler;
            const int n = gpu::zeichneModellAnsicht(
                g_app->model, (g_app->showModelTextures && g_app->modelTextures.found != 0)
                                  ? &g_app->modelTextures
                                  : nullptr,
                knochen, vp, g_app->showCaps, &fehler);
            if (n == 0 && !fehler.empty()) {
                diag::detail("Modellfenster: " + fehler);
            }
            for (const std::string& m : gpu::debugMeldungen()) {
                diag::detail("Direct3D (Modellfenster): " + m);
            }
            g_app->modelDirty = false;
        }
        if (gpu::modellZielTextur() != nullptr) {
            ImGui::Image(reinterpret_cast<ImTextureID>(gpu::modellZielTextur()),
                         ImVec2{static_cast<float>(w), static_cast<float>(h)});
        }
    }
    ImGui::EndChild();

}

// Die Bedienleiste des Modells - UNTER allen drei Spalten, ueber die volle
// Breite.
//
// ZWEI Zeilen, nicht vier. Die Leiste geht ueber das ganze Fenster; bei
// zweitausend Punkten Breite passt nebeneinander, was vorher untereinander
// stand - und untereinander brauchte es mehr Hoehe, als das Band hat. Folge
// war ein Rollbalken, und "Bild 1 von 6" stand abgeschnitten am Rand.
//
//   Zeile 1   Abspielen | Animation | Wiederholen | Tempo | Haut | Flaechen
//   Zeile 2   Zeitleiste, daneben Bild und Sekunden
void drawModelPanel() {
    // Ohne Modell steht alles trotzdem da, nur ohne Wirkung.
    //
    // Vorher verschwand die Leiste ganz - und die Anordnung sprang, sobald
    // man ein Modell lud. Wer die Bedienung schon sieht, weiss, was ihn
    // erwartet, und nichts wandert.
    const bool have = !g_app->model.empty();
    ImGui::BeginDisabled(!have);

    // --- Zeile 1 ----------------------------------------------------------
    if (ImGui::Button(g_app->animPlaying ? tr(Str::ModelStop) : tr(Str::ModelPlay),
                      ImVec2{ImGui::GetFontSize() * 4.0F, 0.0F})) {
        g_app->animPlaying = !g_app->animPlaying;
    }
    ImGui::SameLine();

    if (g_app->anim.empty()) {
        // KEIN zweiter "Laden"-Knopf: derselbe steht schon in der
        // Werkzeugleiste ueber der Ansicht. Zweimal dasselbe an zwei Orten
        // ist nur verwirrend - hier bleibt der Hinweis.
        ImGui::TextDisabled("%s", tr(Str::ModelNoAnim));
    } else {
        // Die Animation: tippbar, und der getippte Text ist der Filter.
        std::string& animName = g_app->animText;
        static std::vector<TypeEntry> animEntries;
        static const std::vector<AnimEntry>* builtFrom = nullptr;
        if (builtFrom != &g_app->animList ||
            animEntries.size() != g_app->animList.size()) {
            animEntries.clear();
            animEntries.reserve(g_app->animList.size());
            for (const AnimEntry& a : g_app->animList) {
                TypeEntry t;
                t.name = a.name;
                animEntries.push_back(std::move(t));
            }
            builtFrom = &g_app->animList;
        }
        const ComboEditResult r = comboEdit("##anim", animName, &animEntries,
                                            ImGui::GetFontSize() * 16.0F);
        if (r.edited || r.picked) {
            for (std::size_t i = 0; i < g_app->animList.size(); ++i) {
                if (g_app->animList[i].name != animName) {
                    continue;
                }
                g_app->animIndex = static_cast<int>(i);
                g_app->animFrame = g_app->animList[i].firstFrame;
                g_app->animTime = 0.0F;
                g_app->modelDirty = true;
                break;
            }
        }

        // Den Namen in die Zwischenablage.
        //
        // shank: "In the Model preview, could you add a 'copy to clipboard'
        // icon next to the animation name? Then I can select an animation
        // and copy and paste the name into my script."
        //
        // Ein SYMBOL waere schoener, aber der Zeichensatz kennt nur die
        // Befehlssymbole aus den Original-BehavEd-Ressourcen (I_SET,
        // I_LOOP, ...) - ein Kopieren-Symbol ist nicht dabei, und ein
        // erfundenes passte nicht dazu. Also der Text, den es fuer diese
        // Handlung ohnehin schon gibt.
        ImGui::SameLine();
        if (ImGui::Button(tr(Str::ActCopy)) && !animName.empty()) {
            ImGui::SetClipboardText(animName.c_str());
            char kop[200];
            std::snprintf(kop, sizeof(kop), tr(Str::ModelAnimCopied),
                          animName.c_str());
            addStatus(kop);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::HintCopy));
        }

        // Wiederholen als HAKEN: die animation.cfg legt je Abschnitt fest, ob
        // er wiederholt - der Haken erzwingt nur.
        ImGui::SameLine();
        bool forceLoop = (g_app->loopMode == 1);
        if (ImGui::Checkbox(tr(Str::ModelLoop), &forceLoop)) {
            g_app->loopMode = forceLoop ? 1 : 0;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("%s", tr(Str::ModelLoopHint));
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0F);
        ImGui::SliderFloat(tr(Str::ModelSpeed), &g_app->animSpeed, 0.1F, 3.0F,
                           "%.1fx");
    }

    // Die Haut - dieselbe Zeile, rechts daneben.
    if (!g_app->modelSkins.empty()) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0F);
        const std::string current =
            (g_app->modelSkinIndex >= 0 &&
             static_cast<std::size_t>(g_app->modelSkinIndex) <
                 g_app->modelSkins.size())
                ? g_app->modelSkins[static_cast<std::size_t>(g_app->modelSkinIndex)]
                : std::string("-");
        if (ImGui::BeginCombo(tr(Str::ModelSkin), current.c_str())) {
            for (std::size_t i = 0; i < g_app->modelSkins.size(); ++i) {
                const bool sel = (static_cast<int>(i) == g_app->modelSkinIndex);
                if (ImGui::Selectable(g_app->modelSkins[i].c_str(), sel)) {
                    applySkinByIndex(static_cast<int>(i));
                }
                if (sel) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("%s", tr(Str::ModelSkinHint));
        }
        ImGui::SameLine();
        int shown = 0;
        int total = 0;
        for (const GlmSurface& sf : g_app->model.surfaces) {
            if (sf.isTag()) {
                continue;
            }
            ++total;
            if (sf.isVisible(g_app->showCaps)) {
                ++shown;
            }
        }
        ImGui::TextDisabled(tr(Str::ModelSurfaces), shown, total);
        // --- Was der Zeichner von jeder Flaeche sieht -------------------
        //
        // Gemeldet: "bei Watt Tambor fehlt der Brustpanzer".
        //
        // Die Suche danach hat zwoelf Runden gebraucht, und der Grund war
        // jedesmal derselbe: von aussen ist nicht zu sehen, WELCHE Zahl
        // nicht stimmt. Das Modell laedt richtig, die Flaeche gilt als
        // sichtbar, die Ecken sind vollstaendig - und im Bild fehlt sie
        // trotzdem.
        //
        // Diese Ausgabe stellt alle Zahlen nebeneinander, die dabei eine
        // Rolle spielen. Sie steht im DETAILPROTOKOLL, nicht in der
        // Oberflaeche: sie ist zum Nachsehen da, nicht zum Anschauen.
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::ModelSurfaceDump));
        }
        if (ImGui::IsItemClicked()) {
            diag::info("Flaechen von \"" + g_app->model.name + "\":");
            for (std::size_t i = 0; i < g_app->model.surfaces.size(); ++i) {
                const GlmSurface& sf = g_app->model.surfaces[i];
                if (sf.isTag()) {
                    continue;
                }
                float mn[3] = {1e9F, 1e9F, 1e9F};
                float mx[3] = {-1e9F, -1e9F, -1e9F};
                for (const GlmVertex& v : sf.verts) {
                    for (int k = 0; k < 3; ++k) {
                        mn[k] = std::min(mn[k], v.xyz[k]);
                        mx[k] = std::max(mx[k], v.xyz[k]);
                    }
                }
                std::set<int> knochen;
                for (const GlmVertex& v : sf.verts) {
                    for (int k = 0; k < 4; ++k) {
                        if (v.weights[k] > 0.0001F) {
                            knochen.insert(static_cast<int>(v.bones[k]));
                        }
                    }
                }
                std::string kn;
                for (const int k : knochen) {
                    kn += std::to_string(k) + " ";
                }
                const bool hatTextur =
                    i < g_app->modelTextures.bySurface.size() &&
                    !g_app->modelTextures.bySurface[i].empty();
                char zeile[320];
                std::snprintf(
                    zeile, sizeof zeile,
                    "   %-22s Ecken %-5zu Dreiecke %-5zu sichtbar %d "
                    "Textur %d Flagge 0x%02x  x %.1f..%.1f y %.1f..%.1f "
                    "z %.1f..%.1f  Knochen %s",
                    sf.name.c_str(), sf.verts.size(), sf.indexes.size() / 3,
                    static_cast<int>(sf.isVisible(g_app->showCaps)),
                    static_cast<int>(hatTextur),
                    static_cast<unsigned>(sf.flags),
                    static_cast<double>(sf.verts.empty() ? 0.0F : mn[0]),
                    static_cast<double>(sf.verts.empty() ? 0.0F : mx[0]),
                    static_cast<double>(sf.verts.empty() ? 0.0F : mn[1]),
                    static_cast<double>(sf.verts.empty() ? 0.0F : mx[1]),
                    static_cast<double>(sf.verts.empty() ? 0.0F : mn[2]),
                    static_cast<double>(sf.verts.empty() ? 0.0F : mx[2]),
                    kn.c_str());
                diag::info(zeile);
            }
        }
    }

    // Ohne Animation eine leere Zeitleiste zeigen - dieselbe Hoehe, damit
    // nichts springt.
    if (!have || g_app->anim.empty() || g_app->animIndex < 0 ||
        static_cast<std::size_t>(g_app->animIndex) >= g_app->animList.size()) {
        int none = 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##frameoff", &none, 0, 0, "");
        ImGui::EndDisabled();
        return;
    }
    const AnimEntry& e = g_app->animList[static_cast<std::size_t>(g_app->animIndex)];

    // --- Abspielen --------------------------------------------------------
    if (g_app->animPlaying) {
        const bool wantLoop = (g_app->loopMode == 1) ||
                              (g_app->loopMode == 0 && e.loopFrame >= 0);
        g_app->animTime += ImGui::GetIO().DeltaTime * g_app->animSpeed *
                           static_cast<float>(std::max(e.fps, 1));
        while (g_app->animTime >= 1.0F) {
            g_app->animTime -= 1.0F;
            ++g_app->animFrame;
            if (g_app->animFrame >= e.firstFrame + e.numFrames) {
                if (wantLoop) {
                    // Ab loopFrame, nicht ab dem Anfang: manche Animationen
                    // haben einen Vorlauf, der nur einmal laufen soll.
                    const int from = (g_app->loopMode == 1 && e.loopFrame < 0)
                                         ? 0
                                         : std::max(e.loopFrame, 0);
                    g_app->animFrame = e.firstFrame + from;
                } else {
                    g_app->animFrame = e.firstFrame + e.numFrames - 1;
                    g_app->animPlaying = false;
                }
            }
            g_app->modelDirty = true;
        }
    }

    // --- Zeile 2: die Zeitleiste ------------------------------------------
    //
    // Bild und Sekunden stehen DANEBEN, nicht darunter: eine dritte Zeile
    // war genau die, die nicht mehr ins Band passte.
    int rel = g_app->animFrame - e.firstFrame;
    // Beide Teile ueber die uebersetzten Vorlagen: "Bild %d von %d" und
    // "%.2f s von %.2f s". Ein festes Format waere in jeder Sprache gleich
    // falsch.
    char frames[48];
    std::snprintf(frames, sizeof(frames), tr(Str::ModelFrame), rel + 1,
                  e.numFrames);
    char secs[48];
    std::snprintf(secs, sizeof(secs), tr(Str::ModelSeconds),
                  static_cast<double>(rel) / static_cast<double>(std::max(e.fps, 1)),
                  static_cast<double>(e.numFrames) /
                      static_cast<double>(std::max(e.fps, 1)));
    char info[112];
    std::snprintf(info, sizeof(info), "%s   %s", frames, secs);
    // ZWEI Abstaende abziehen, nicht einen: SameLine setzt selbst noch einen
    // dazwischen. Fehlte er, war die Zeile um genau diesen Betrag zu breit -
    // ImGui setzte einen waagerechten Rollbalken ins Band, und der nahm der
    // Ansicht darueber Hoehe weg. Genau daran lagen die ungleichen Rahmen.
    const float infoW = ImGui::CalcTextSize(info).x +
                        ImGui::GetStyle().ItemSpacing.x * 2.0F;
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - infoW,
                                     ImGui::GetFontSize() * 8.0F));
    if (ImGui::SliderInt("##frame", &rel, 0, std::max(e.numFrames - 1, 0), "")) {
        g_app->animFrame = e.firstFrame + rel;
        g_app->animPlaying = false;
        g_app->modelDirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", info);
    ImGui::EndDisabled();
}

// Die Skripte, auf die die geladene Karte verweist.
//
// So haengt eine Mission zusammen: die Karte selbst weiss nichts von
// Skripten, aber ihre Entities tun es - target_scriptrunner nennt ein
// usescript, ein NPC_spawner ein deathscript. Bei md_tfoaj_jedi sind das
// sieben, und alle liegen im selben Archiv.
//
// Damit muss niemand mehr raten, welches Skript zu einer Karte gehoert.
void drawMapScriptList() {
    if (g_app->map.scripts.empty()) {
        ImGui::TextDisabled("%s", tr(Str::MapScriptsNone));
        return;
    }
    for (std::size_t i = 0; i < g_app->map.scripts.size(); ++i) {
        const MapData::ScriptRef& r = g_app->map.scripts[i];
        ImGui::PushID(static_cast<int>(i));

        // Liegt die Datei vor? Erst .txt, dann .ibi - beides kommt vor, und
        // die Endung ist mal gross, mal klein geschrieben. Der Archivindex
        // nimmt beides.
        std::string data;
        std::string found;
        for (const char* ext : {".txt", ".icarus", ".ibi"}) {
            if (readFromArchives("scripts/" + r.path + ext, data)) {
                found = ext;
                break;
            }
        }

        ImGui::BeginDisabled(found.empty());
        // withUnsaved statt confirmDiscard.
        //
        // confirmDiscard gab es einmal - eine Windows-MessageBox antwortet
        // sofort, also konnte man ihr Ergebnis in einer Bedingung benutzen.
        // Seit die Frage im eigenen Stil gestellt wird, dauert sie mehrere
        // Bilder, und was danach geschehen soll, wird MITGEGEBEN.
        //
        // Diese eine Stelle war beim Umbau uebersehen worden - sie stand
        // als einzige nicht in app.cpp. Uebersetzt hat es trotzdem, weil
        // die Erklaerung im Kopf blieb; erst der Bindeschritt haette es
        // gemeldet, und der laeuft hier nicht.
        if (ImGui::Button(r.path.c_str()) && !found.empty()) {
            const std::string dat = data;
            const std::string nm = r.path + found;
            withUnsaved([dat, nm] { openScriptFromMemory(dat, nm); });
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s von %s", r.key.c_str(), r.owner.c_str());
        }
        if (found.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr(Str::MapScriptMissing));
        }
        ImGui::PopID();
    }
}

// Eine .pk3 waehlen und die Missionen darin anzeigen.
//
// Das ist der Einstieg, den man sich wuenscht: eine Datei waehlen, und alles
// weitere ergibt sich. Die Kette steht in den Daten, man muss sie nur
// ablaufen - Karte, Entities daneben, Skripte aus den Entities, Figuren aus
// den Skripten, Modelle aus den NPC-Typen.
void loadMissionArchive() {
    const std::string p = platform::openFileDialog(
        tr(Str::LoadMission), "Game archives (*.pk3)|*.pk3|All files (*.*)|*.*",
        g_app->settings.lastDir);
    if (p.empty()) {
        return;
    }
    missionenAusArchiv(p);
}

void missionenAusArchiv(const std::string& p) {
    diag::Step step("Mission suchen: " + p);
    g_app->settings.lastDir = directoryOf(p);
    g_app->missionArchive = p;

    // Das Archiv als Spielordner aufnehmen, damit Texturen und Modelle
    // daraus gefunden werden. Der Ordner darum kommt mit - dort liegen die
    // Basisdaten.
    addGamePathDirectory(directoryOf(p));

    Pk3 archive;
    std::string err;
    if (!readPk3Directory(p, archive, &err)) {
        step.fail(err);
        platform::showError(p + "\n" + err, tr(Str::AppTitle));
        return;
    }
    std::vector<std::string> names;
    names.reserve(archive.entries.size());
    for (const Pk3Entry& e : archive.entries) {
        names.push_back(e.name);
    }
    // Gelesen wird aus ALLEN Spielordnern, nicht nur aus diesem Archiv:
    // eine Mission kann ihre Karte im Mod und die Skripte in den Basisdaten
    // haben. Das Archiv selbst ist gerade dazugekommen.
    g_app->missions = findMissions(names, [](const std::string& n,
                                             std::string& out) {
        return readFromArchives(n, out);
    });
    diag::info(std::to_string(g_app->missions.size()) + " Karten gefunden");

    // Genau eine spielbare Mission? Dann direkt laden.
    //
    // Bei mehreren wird gefragt - und das ist der Normalfall: ein Mod bringt
    // ueblicherweise mehrere Missionen mit, je eine .ent je Karte.
    int playable = 0;
    const Mission* only = nullptr;
    for (const Mission& m : g_app->missions) {
        if (m.playable()) {
            ++playable;
            only = &m;
        }
    }
    if (playable == 1 && only != nullptr) {
        loadMission(*only);
        return;
    }
    g_app->missionPickOpen = true;
}

// Eine Mission einrichten: Karte, Entities, erstes Skript.
void loadMission(const Mission& m) {
    // --- Die Karte des VORIGEN Reiters festhalten -------------------------
    //
    // Gemeldet: eine Mission zu laden oeffnet richtigerweise einen neuen
    // Reiter - aber im ersten war die Karte danach auch geladen.
    //
    // Der Grund liegt in der Reihenfolge: hier wird ERST die Karte geladen,
    // und das Skript danach legt den neuen Reiter an. Beim Anlegen parkt das
    // Programm den alten - und schreibt ihm dabei die Karte zu, die gerade
    // lebt. Das ist inzwischen die neue.
    //
    // Also merken, was der alte Reiter hatte, und es ihm hinterher
    // zurueckgeben. Sauberer waere, den Reiter vor der Karte anzulegen -
    // dazu muesste aber das Anlegen aus dem Skriptoeffnen heraus, und das
    // ist ein eigener Umbau.
    const int vorherTab = g_app->activeTab;
    const std::string vorherMapId = g_app->map.path;
    const std::string vorherMapPath = g_app->settings.mapPath;
    diag::Step step("Mission laden: " + m.name);

    std::string data;
    if (!readFromArchives(m.mapFile, data)) {
        step.fail("Karte nicht lesbar: " + m.mapFile);
        return;
    }
    loadMapFromBytes(data, m.mapFile);
    // Aus einem Archiv gibt es keinen Pfad auf der Platte.
    g_app->settings.mapPath.clear();

    // Die .ent hat Vorrang: Movie Duels laesst die .bsp unveraendert und
    // legt die Entities daneben. Ohne sie bekaeme man die Entities der
    // Originalkarte - ohne die Figuren der Mission.
    if (!m.entFile.empty() && readFromArchives(m.entFile, data)) {
        std::vector<MapEntity> ents;
        parseEntities(data, ents);
        if (!ents.empty()) {
            g_app->map.entities = std::move(ents);
            collectNames(g_app->map);
            diag::info(std::to_string(g_app->map.entities.size()) +
                       " Entities aus " + m.entFile);
        }
    }

    // --- ALLE Skripte der Mission, jedes in einen eigenen Reiter ----------
    //
    // Gewuenscht: "wenn eine Mission mehrere Cutscenes hat, wuerde ich gerne
    // alle laden koennen und auch umschalten koennen."
    //
    // Vorher stand hier ein `break` nach dem ersten - meistens dem intro.
    // Die Mission kennt aber alle, und seit rc235 teilen sie sich die Karte,
    // ohne sie einander wegzunehmen. Umschalten ist dann das, was es
    // ohnehin schon gibt: das Reiterband, die geteilte Ansicht, das
    // Klappfeld ueber jedem Feld.
    //
    // Eine Obergrenze, weil manche Missionen sehr viele haben: 27 Karten
    // wurden in Ep3 gefunden, und ebenso viele Reiter waeren unbedienbar.
    // Wer mehr braucht, oeffnet sie einzeln.
    // Alle, nicht acht.
    //
    // Gemeldet: "ich muesste alle laden koennen, auch wenn es mehr sind als
    // 8." Bei md_ga_jedi waren es 24 - 16 blieben liegen.
    //
    // Die Grenze war Vorsicht vor unbedienbar vielen Reitern. Die ist
    // unbegruendet: das Reiterband rollt, und wer sie nicht braucht,
    // schliesst sie. Ein Skript, das nicht geladen ist, kann man dagegen
    // gar nicht ansehen - das ist die schlimmere Einschraenkung.
    //
    // Teuer ist es nicht: die Karte wird seit rc235 geteilt, ein weiteres
    // Skript kostet nur das Lesen der Datei.
    constexpr std::size_t kMaxSkripte = 999;
    std::size_t geladen = 0;
    std::size_t uebersprungen = 0;
    for (const Mission::Script& sc2 : m.scripts) {
        if (sc2.file.empty()) {
            continue;
        }
        if (geladen >= kMaxSkripte) {
            ++uebersprungen;
            continue;
        }
        if (readFromArchives(sc2.file, data)) {
            openScriptFromMemory(data, sc2.file);
            ++geladen;
        }
    }
    diag::info("Skripte der Mission: " + std::to_string(geladen) +
               " geladen" +
               (uebersprungen != 0U
                    ? ", " + std::to_string(uebersprungen) +
                          " nicht (Grenze " + std::to_string(kMaxSkripte) + ")"
                    : std::string()));

    // Zurueck auf das ERSTE - meistens das intro, und das will man sehen.
    //
    // Ohne das stuende man im letzten geladenen, was willkuerlich waere.
    if (geladen > 1 && !g_app->tabs.empty()) {
        const int erstes =
            static_cast<int>(g_app->tabs.size()) - static_cast<int>(geladen);
        if (erstes >= 0) {
            activateTab(erstes);
        }
    }

    // Und jetzt alles auf einmal, statt beim ersten Bewegen.
    prepareScene();
    // Dem vorigen Reiter seine Karte zurueckgeben - siehe oben.
    //
    // Nur, wenn wirklich ein NEUER Reiter entstanden ist. Wurde die Mission
    // in denselben geladen (weil er leer und ungeaendert war), gehoert ihm
    // die neue Karte zu Recht.
    if (g_app->activeTab != vorherTab && vorherTab >= 0 &&
        static_cast<std::size_t>(vorherTab) < g_app->tabs.size()) {
        App::Parked& alt = g_app->tabs[static_cast<std::size_t>(vorherTab)];
        alt.mapId = vorherMapId;
        alt.mapPath = vorherMapPath;
        diag::detail("Mission in neuem Reiter: Reiter " +
                     std::to_string(vorherTab) + " behaelt seine Karte \"" +
                     (vorherMapId.empty() ? std::string("(keine)")
                                          : vorherMapId) +
                     "\"");
    }
    g_app->leftMode = 1;
    g_app->missionPickOpen = false;
    char msg[256];
    std::snprintf(msg, sizeof(msg), tr(Str::MissionLoaded), m.name.c_str(),
                  static_cast<int>(g_app->map.entities.size()),
                  static_cast<int>(m.scripts.size()));
    addStatus(msg);
}

// Die Auswahl, wenn ein Archiv mehrere Karten enthaelt.
void drawMissionPicker() {
    if (!g_app->missionPickOpen) {
        return;
    }
    ImGui::OpenPopup("##missionpick");
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2{0.5F, 0.5F});
    if (!ImGui::BeginPopupModal("##missionpick", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted(tr(Str::MissionPick));
    ImGui::Separator();

    bool anyPlayable = false;
    for (std::size_t i = 0; i < g_app->missions.size(); ++i) {
        const Mission& m = g_app->missions[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(!m.playable());
        if (ImGui::Button(m.name.c_str(), ImVec2{ImGui::GetFontSize() * 14.0F, 0.0F})) {
            const Mission copy = m;   // loadMission raeumt die Liste auf
            ImGui::CloseCurrentPopup();
            ImGui::EndDisabled();
            ImGui::PopID();
            ImGui::EndPopup();
            loadMission(copy);
            return;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (m.playable()) {
            anyPlayable = true;
            ImGui::TextDisabled("%d", static_cast<int>(m.scripts.size()));

            // --- Teilen sich mehrere Missionen eine Karte? ----------------
            //
            // Gefragt: "sind das alle Missionen, oder teilen sich davon
            // welche was? Das muesste ich wissen."
            //
            // Die Antwort steht in den Daten - sie wurde nur nicht gezeigt.
            // Jede Mission kennt ihre .bsp; tragen zwei dieselbe, spielen
            // sie auf derselben Karte. Bei Movie Duels ist das der Regelfall:
            // md_afif2_jedi und md_afif2_sith sind dieselbe Karte, einmal
            // aus Sicht des Jedi und einmal des Sith.
            //
            // Das ist keine Nebensache: wer eine davon laedt, sieht die
            // Geometrie der anderen mit, und die Skripte laufen ueber
            // dieselben Entities.
            {
                int gleicheKarte = 0;
                for (const Mission& andere : g_app->missions) {
                    if (andere.playable() && andere.mapFile == m.mapFile) {
                        ++gleicheKarte;
                    }
                }
                if (gleicheKarte > 1) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s %d", tr(Str::MissionSharesMap),
                                        gleicheKarte);
                }
            }
            // Beim Ueberfahren zeigen, WAS geladen wird - Entity-Datei und
            // die Skripte. Sonst raet man am Kartennamen herum.
            if (ImGui::IsItemHovered() || ImGui::IsItemHovered(
                    ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(m.mapFile.c_str());
                // Und im Hinweisfenster die Namen dazu - so sieht man
                // sofort, WELCHE es sind.
                for (const Mission& andere : g_app->missions) {
                    if (andere.playable() && andere.mapFile == m.mapFile &&
                        andere.name != m.name) {
                        ImGui::TextDisabled("%s %s",
                                            tr(Str::MissionSharesMap),
                                            andere.name.c_str());
                    }
                }
                if (!m.entFile.empty()) {
                    ImGui::TextDisabled("%s", m.entFile.c_str());
                }
                ImGui::Separator();
                for (const Mission::Script& sc : m.scripts) {
                    if (sc.file.empty()) {
                        ImGui::TextDisabled("%s  (%s)", sc.ref.c_str(),
                                            tr(Str::MapScriptMissing));
                    } else {
                        ImGui::TextUnformatted(sc.ref.c_str());
                    }
                }
                ImGui::EndTooltip();
            }
        } else {
            ImGui::TextDisabled("%s", tr(Str::MissionDuel));
        }
        ImGui::PopID();
    }
    if (!anyPlayable) {
        ImGui::TextDisabled("%s", tr(Str::MissionNone));
    }
    ImGui::Separator();
    if (ImGui::Button(tr(Str::EditorCancel))) {
        g_app->missionPickOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Der Ebenen-Verwalter fuer Entities.
//
// Nach dem Vorbild von 3ds Max: eine Klasse ist eine Ebene, das Auge davor
// schaltet sie um, ein Klick auf den Namen klappt ihre Mitglieder auf. Was
// dort "Layer Explorer" heisst, ist hier nach Klassennamen gruppiert - eine
// Karte hat keine Ebenen, aber classname trennt genauso sauber.
//
// Zwei Griffe aus Max uebernommen:
//   * Alt-Klick auf ein Auge stellt die Gruppe ALLEIN (Solo). In csTools
//     heisst das "Alt+click a button to solo it".
//   * Eine versteckte Ebene versteckt ihre Mitglieder mit, auch wenn deren
//     eigenes Auge offen ist.
void drawEntityLayers(float height) {
    // --- Die Entities, die man beim Schreiben einer Szene braucht ---------
    //
    // Vorher vier Klassen (waypoint_navgoal, target_position, NPC_spawner,
    // target_scriptrunner). Fuer eine Zwischensequenz fehlten gerade die
    // wichtigsten: die Kameramarken (ref_tag - in md_ga_jedi steht JEDE
    // Kamera auf einer), alle NPC_*-Klassen, Effekte, Lautsprecher, Musik
    // und die Brushes, die ein Skript bewegt.
    //
    // Dazu eine Suche und je Eintrag, was man damit tut: Doppelklick fliegt
    // hin, Rechtsklick setzt den passenden Befehl hinter die gewaehlte Zeile
    // - "Kamera auf diese Marke" (MOVE + PAN mit $tag$), "Laufziel",
    // "Blickziel", "affect-Block", "use". So entsteht eine Szene aus der
    // Karte heraus, ohne Namen abzutippen.
    ImGui::BeginChild("entlayers", ImVec2{ImGui::GetFontSize() * 15.0F, height},
                      ImGuiChildFlags_Borders);
    static char suche[64] = "";
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##entsuche", tr(Str::EntitySearch), suche, sizeof(suche));
    std::string such = suche;
    for (char& c : such) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    const auto klein = [](std::string x) {
        for (char& c : x) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        return x;
    };
    for (int kat = 0; kat < entityKategorien(); ++kat) {
        // Welche Entities, welche Klassen, wie viele sichtbar?
        std::vector<std::size_t> drin;
        std::set<std::string> klassen;
        int shown = 0;
        for (std::size_t i = 0; i < g_app->map.entities.size(); ++i) {
            const MapEntity& e = g_app->map.entities[i];
            if (entityKategorie(e) != kat) {
                continue;
            }
            const std::string name = entitySkriptName(e);
            if (!such.empty() && klein(name).find(such) == std::string::npos &&
                klein(e.classname).find(such) == std::string::npos) {
                continue;
            }
            drin.push_back(i);
            klassen.insert(e.classname);
            if (g_app->entityVisible(i, e.classname)) {
                ++shown;
            }
        }
        if (drin.empty()) {
            continue;
        }
        const EntityKat& ek = entityKategorieInfo(kat);
        ImGui::PushID(kat);
        bool alleVersteckt = true;
        for (const std::string& k2 : klassen) {
            alleVersteckt = alleVersteckt && g_app->hiddenClasses.count(k2) != 0;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(ek.r, ek.g, ek.b, 255));
        if (ImGui::SmallButton(alleVersteckt ? "-" : "O")) {
            for (const std::string& k2 : klassen) {
                if (alleVersteckt) {
                    g_app->hiddenClasses.erase(k2);
                } else {
                    g_app->hiddenClasses.insert(k2);
                }
            }
            g_app->mapDirty = true;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        const std::string schluessel = "kat" + std::to_string(kat);
        const bool open = (g_app->expandedClass == schluessel) || !such.empty();
        if (ImGui::Selectable(tr(ek.titel), open)) {
            g_app->expandedClass = (g_app->expandedClass == schluessel) ? std::string() : schluessel;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(tr(Str::EntityCount), shown, static_cast<int>(drin.size()));
        if (open) {
            ImGui::Indent(ImGui::GetFontSize());
            for (const std::size_t i : drin) {
                const MapEntity& e = g_app->map.entities[i];
                ImGui::PushID(static_cast<int>(i));
                const bool own = g_app->hiddenEntities.count(i) != 0;
                ImGui::BeginDisabled(g_app->hiddenClasses.count(e.classname) != 0);
                if (ImGui::SmallButton(own ? "-" : "O")) {
                    if (own) {
                        g_app->hiddenEntities.erase(i);
                    } else {
                        g_app->hiddenEntities.insert(i);
                    }
                    g_app->mapDirty = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                const std::string name = entitySkriptName(e);
                const std::string label = name.empty() ? std::string("(") + e.classname + ")" : name;
                if (ImGui::Selectable(label.c_str(), g_app->pickedEntity == static_cast<int>(i),
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    g_app->pickedEntity = static_cast<int>(i);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        zurEntityFliegen(i);
                    }
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(e.classname.c_str());
                    ImGui::TextDisabled("%s", tr(Str::EntityRowHint));
                    ImGui::EndTooltip();
                }
                if (ImGui::BeginPopupContextItem("entmenu")) {
                    g_app->pickedEntity = static_cast<int>(i);
                    entityMenue(i);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::Unindent(ImGui::GetFontSize());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// --- Gruppen der Entity-Liste und der Marken in der Ansicht ---------------
namespace {
const EntityKat kKategorien[] = {
    {Str::EntityKatTags, 80, 200, 255},
    {Str::EntityKatNav, 90, 220, 120},
    {Str::EntityKatLook, 255, 160, 60},
    {Str::EntityKatNpc, 200, 200, 90},
    {Str::EntityKatRunner, 220, 90, 220},
    {Str::EntityKatFx, 255, 110, 80},
    {Str::EntityKatSound, 150, 170, 255},
    {Str::EntityKatMover, 150, 165, 190},
    {Str::EntityKatBreak, 190, 130, 80},
};
}  // namespace

int entityKategorien() { return static_cast<int>(sizeof(kKategorien) / sizeof(kKategorien[0])); }

const EntityKat& entityKategorieInfo(int k) {
    return kKategorien[static_cast<std::size_t>(std::clamp(k, 0, entityKategorien() - 1))];
}

int entityKategorie(const MapEntity& e) {
    const std::string& c = e.classname;
    if (c == "ref_tag") { return 0; }
    if (c.rfind("waypoint_navgoal", 0) == 0) { return 1; }
    if (c == "target_position" || c == "info_notnull") { return 2; }
    if (c.rfind("NPC_", 0) == 0) { return 3; }
    if (c == "target_scriptrunner") { return 4; }
    if (c == "fx_runner") { return 5; }
    if (c == "target_speaker" || c == "target_play_music") { return 6; }
    if (c == "func_breakable" || c == "misc_model_breakable") { return 8; }
    if (c.rfind("func_", 0) == 0 && (!e.targetname.empty() || e.find("script_targetname") != nullptr)) { return 7; }
    return -1;
}

// Unter welchem Namen ein Skript die Entity anspricht: eine Figur ueber
// NPC_targetname, sonst script_targetname, sonst targetname.
std::string entitySkriptName(const MapEntity& e) {
    // NPC_targetname gilt nur fuer NPC-Spawner (NPC_Spawn_Do gibt ihn der
    // gespawnten Figur). Ein misc_model_breakable mit npc_targetname (das
    // X-Wing in t2_wedge) heisst fuer ICARUS weiter script_targetname.
    if (e.classname.rfind("NPC_", 0) == 0 || e.classname.rfind("npc_", 0) == 0) {
        if (const std::string* n = e.find("NPC_targetname"); n != nullptr && !n->empty()) { return *n; }
    }
    if (const std::string* n = e.find("script_targetname"); n != nullptr && !n->empty()) { return *n; }
    return e.targetname;
}

// Wo steht die Entity? origin, sonst die Mitte ihres Brush-Modells.
bool entityOrt(const MapEntity& e, float out[3]) {
    if (!e.origin.empty()) {
        std::istringstream is(e.origin);
        is >> out[0] >> out[1] >> out[2];
        return true;
    }
    return false;
}

// Die freie Kamera so stellen, dass die Entity vor einem liegt.
void zurEntityFliegen(std::size_t i) {
    if (i >= g_app->map.entities.size()) {
        return;
    }
    float p[3];
    if (!entityOrt(g_app->map.entities[i], p)) {
        return;
    }
    const float gier = g_app->cam.angles[1] * 3.14159265F / 180.0F;
    g_app->cam.pos[0] = p[0] - std::cos(gier) * 180.0F;
    g_app->cam.pos[1] = p[1] - std::sin(gier) * 180.0F;
    g_app->cam.pos[2] = p[2] + 70.0F;
    aimAngles(g_app->cam.pos, p, g_app->cam.angles);
    g_app->throughCamera = false;
    g_app->mapDirty = true;
}

namespace {
Node befehl(const char* name, std::initializer_list<std::pair<Arg::Kind, std::string>> args,
            const char* erstesTypeset = "") {
    Node n;
    n.kind = Node::Kind::Command;
    n.name = name;
    bool erstes = true;
    for (const auto& [k, t] : args) {
        Arg a;
        a.kind = k;
        a.text = t;
        if (erstes) {
            a.typeset = erstesTypeset;
            erstes = false;
        }
        n.args.push_back(a);
    }
    return n;
}

void einfuegen(std::vector<Node> ns) {
    Path at;
    if (!g_app->doc.insertAfterAll(g_app->selectedPath, std::move(ns), &at)) {
        return;
    }
    g_app->selectedPath = at;
    rebuildTree();
    g_app->camTrackValid = false;
    g_app->mapDirty = true;
}
}  // namespace

// Das Rechtsklickmenue einer Entity.
void entityMenue(std::size_t i) {
    const MapEntity& e = g_app->map.entities[i];
    const std::string name = entitySkriptName(e);
    const int kat = entityKategorie(e);
    if (ImGui::MenuItem(tr(Str::EntityFlyTo))) {
        zurEntityFliegen(i);
    }
    if (!name.empty() && ImGui::MenuItem(tr(Str::EntityCopyName))) {
        ImGui::SetClipboardText(name.c_str());
    }
    if (name.empty()) {
        return;
    }
    ImGui::Separator();
    const std::string q = "\"" + name + "\"";
    if (kat == 0) {
        // Genau so stehen die Kameras in den Missionen: auf einer Marke.
        if (ImGui::MenuItem(tr(Str::EntityCamToTag))) {
            einfuegen({befehl("camera", {{Arg::Kind::Ident, "MOVE"},
                                         {Arg::Kind::Expr, "tag( " + q + ", ORIGIN )"},
                                         {Arg::Kind::Number, "0.000"}}, "CAMERA_COMMANDS"),
                       befehl("camera", {{Arg::Kind::Ident, "PAN"},
                                         {Arg::Kind::Expr, "tag( " + q + ", ANGLES )"},
                                         {Arg::Kind::Vector, "0.000 0.000 0.000"},
                                         {Arg::Kind::Number, "0.000"}}, "CAMERA_COMMANDS")});
        }
    }
    if (kat == 0 || kat == 1) {
        if (ImGui::MenuItem(tr(Str::EntityAsNavgoal))) {
            einfuegen({befehl("set", {{Arg::Kind::String, "SET_NAVGOAL"}, {Arg::Kind::String, name}}, "SET_TYPES")});
        }
    }
    if (kat == 0 || kat == 1 || kat == 2 || kat == 3) {
        if (ImGui::MenuItem(tr(Str::EntityAsLook))) {
            einfuegen({befehl("set", {{Arg::Kind::String, "SET_LOOK_TARGET"}, {Arg::Kind::String, name}}, "SET_TYPES")});
        }
        if (ImGui::MenuItem(tr(Str::EntityAsWatch))) {
            einfuegen({befehl("set", {{Arg::Kind::String, "SET_WATCHTARGET"}, {Arg::Kind::String, name}}, "SET_TYPES")});
        }
    }
    if (kat == 3 || kat == 7 || kat == 8) {
        if (ImGui::MenuItem(tr(Str::EntityAffect))) {
            Node a = befehl("affect", {{Arg::Kind::String, name}, {Arg::Kind::Ident, "FLUSH"}});
            a.args[1].typeset = "AFFECT_TYPE";
            a.hasBlock = true;
            einfuegen({a});
        }
    }
    if (!e.targetname.empty() && ImGui::MenuItem(tr(Str::EntityUse))) {
        einfuegen({befehl("use", {{Arg::Kind::String, e.targetname}})});
    }
}

// Was zur angeklickten Entity gehoert.
//
// Hier geht es ueber ein reines Anzeigewerkzeug hinaus: SomaZ' Editor zeigt
// die Entity, wir zeigen zusaetzlich, WO DAS SKRIPT SIE BENUTZT. Genau
// dafuer sitzt die Karte in einem Skripteditor - ein Wegpunkt allein sagt
// wenig, die Zeile, die ihn anspricht, sagt alles.
void drawPickedEntity() {
    if (g_app->pickedEntity < 0 ||
        static_cast<std::size_t>(g_app->pickedEntity) >=
            g_app->map.entities.size()) {
        // In der Zeile darueber, hinter "Insert camera here" - eine eigene
        // Zeile fuer einen Hinweis fehlte der Zeitleiste.
        ImGui::SameLine(0.0F, ImGui::GetFontSize() * 1.5F);
        ImGui::TextDisabled("%s", tr(Str::PickHint));
        return;
    }
    const MapEntity& e =
        g_app->map.entities[static_cast<std::size_t>(g_app->pickedEntity)];
    const std::string name = e.targetname.empty() ? e.classname : e.targetname;
    ImGui::Text(tr(Str::PickedEntity), name.c_str(), e.classname.c_str());

    if (e.targetname.empty()) {
        return;
    }

    // Die Zeilen suchen, die diesen Namen als Argument tragen. Ein Name kann
    // in mehreren Befehlen vorkommen - SET_NAVGOAL, SET_LOOK_TARGET, use.
    std::vector<Path> hits;
    for (std::size_t i = 0; i < g_app->rows.size(); ++i) {
        const Node* n = nodeAt(g_app->doc.script(), g_app->rows[i].path);
        if (n == nullptr) {
            continue;
        }
        bool match = false;
        for (const Arg& a : n->args) {
            if (a.text == e.targetname) {
                match = true;
            }
        }
        if (match) {
            hits.push_back(g_app->rows[i].path);
        }
    }

    ImGui::SameLine();
    if (hits.empty()) {
        ImGui::TextDisabled("%s", tr(Str::PickedUnused));
        return;
    }
    ImGui::TextDisabled(tr(Str::PickedUses), static_cast<int>(hits.size()));

    // Anklickbar: zur Zeile im Baum springen.
    for (std::size_t k = 0; k < hits.size() && k < 6; ++k) {
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(k));
        char label[24];
        std::snprintf(label, sizeof(label), "%d", static_cast<int>(k) + 1);
        if (ImGui::SmallButton(label)) {
            g_app->selectedPath = hits[k];
            g_app->selection.assign(1, hits[k]);
            g_app->scrollToSelected = true;
            for (std::size_t i = 0; i < g_app->rows.size(); ++i) {
                if (g_app->rows[i].path == hits[k]) {
                    g_app->selected = static_cast<int>(i);
                    break;
                }
            }
        }
        ImGui::PopID();
    }
}

void drawMapToolbar() {
    if (ImGui::Button(tr(Str::LoadMission))) { loadMissionArchive(); }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::LoadMissionHint));
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::OpenMapFile))) { doLoadMap(); }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::OpenMapPk3))) {
        if (g_app->gamePaths.empty()) { rescanGamePaths(); }
        g_app->pk3Kind = 1;
        refreshPk3List();
        g_app->pk3Open = true;
    }
    ImGui::SameLine();
    // Die .ent-Datei ersetzt die Entities der Karte. Movie Duels arbeitet
    // so: die .bsp bleibt unveraendert, die Entities kommen aus einer
    // eigenen Datei daneben.
    if (ImGui::Button(tr(Str::OpenEnt))) { openEntFile(); }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::OpenEntHint));
    }
    // Die Skripte der Karte als Klappliste - so findet man die Sequenz zur
    // Karte, ohne den Pfad zu kennen.
    if (!g_app->map.scripts.empty()) {
        ImGui::SameLine();
        char label[64];
        std::snprintf(label, sizeof(label), "%s (%d)", tr(Str::MapScripts),
                      static_cast<int>(g_app->map.scripts.size()));
        if (ImGui::BeginCombo("##mapscripts", label,
                              ImGuiComboFlags_HeightLarge)) {
            drawMapScriptList();
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::MapScriptsHint));
        }
    }
}

// Die Zeitleiste unter der Kartenansicht.
//
// Sie zeigt die Kamerabahn des Skripts: wann welcher Befehl laeuft und wo
// die Kamera dabei steht. Der Zeiger laesst sich ziehen, und beim Abspielen
// bewegt sich die Ansicht mit - das ist der Punkt, fuer den das Ganze
// gebaut wird: sieht die Fahrt gut aus, passt sie zur Laenge des Satzes.
// Der Griff an der rechten Kante eines Blocks.
//
// Gezogen wird die DAUER. Waehrend des Ziehens wandert nur die Vorschau;
// erst beim Loslassen geht es ueber replaceAt() ins Skript und damit in
// den Rueckgaengig-Speicher - dieselbe Regel wie beim Gizmo.
//
// Gibt true zurueck, wenn dieser Block gerade gezogen wird; dann zeichnet
// der Aufrufer seine rechte Kante an der Vorschauposition statt am Wert
// aus dem Skript.
bool kantenGriff(const Path& pfad, double startMs, double endMs,
                 const ImVec2& a, const ImVec2& b, double msJeBildpunkt) {
    if (pfad.empty()) {
        return false;
    }
    const Node* n = nodeAt(g_app->doc.script(), pfad);
    if (n == nullptr || durationArgIndex(*n) < 0) {
        return false;   // keine Dauer - also kein Griff
    }

    const bool ichZiehe = g_app->tlDragging && g_app->tlDragPath == pfad;
    const float greifW = std::max(4.0F, ImGui::GetFontSize() * 0.35F);
    const ImVec2 gA{b.x - greifW, a.y};
    const ImVec2 gB{b.x + greifW, b.y};
    const bool drauf = ImGui::IsMouseHoveringRect(gA, gB);

    if (drauf || ichZiehe) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (drauf && !g_app->tlDragging &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // Nur VORMERKEN - ein spaeter gezeichneter Block an derselben Stelle
        // liegt oben und gewinnt (siehe tlGriffKandidat).
        App::TlKandidat& k = g_app->tlGriffKandidat;
        k.da = true;
        k.pfad = pfad;
        k.startMs = startMs;
        k.endMs = endMs;
    }
    if (!ichZiehe) {
        return false;
    }

    // Neues Ende aus der Mausbewegung. Mindestens eine Millisekunde: eine
    // Dauer von null laesst die Engine den Befehl ueberspringen, und das
    // ist beim Ziehen nie gemeint.
    g_app->tlDragEndMs =
        std::max(g_app->tlDragStartMs + 1.0,
                 g_app->tlDragEndMs +
                     static_cast<double>(ImGui::GetIO().MouseDelta.x) *
                         msJeBildpunkt);

    ImGui::BeginTooltip();
    ImGui::Text(tr(Str::TlDragDur),
                (g_app->tlDragEndMs - g_app->tlDragStartMs) / 1000.0);
    ImGui::EndTooltip();

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        double dauer = g_app->tlDragEndMs - g_app->tlDragStartMs;
        // Ein wait ist im Spiel erst im Bild NACH seinem Ablauf fertig
        // (stempel + dauer < jetzt, TaskManager.cpp:1100): die Leiste zeigt
        // es also ein Spielbild (50 ms) laenger, als der Wert sagt. Beim
        // Zurueckschreiben dieses Bild wieder abziehen - sonst wuerde jedes
        // Ziehen den Wert um 50 ms verfaelschen.
        if (n->name == "wait") {
            dauer = std::max(1.0, dauer - 50.0);
        }
        diag::detail("Zeitleiste: Kante losgelassen, neue Dauer " + std::to_string(dauer) + " ms");
        Node neu = *n;
        const int idx = durationArgIndex(neu);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3f", dauer);
        neu.args[static_cast<std::size_t>(idx)].text = buf;
        if (g_app->doc.replaceAt(pfad, neu)) {
            rebuildTree();
            g_app->camTrackValid = false;   // Zeiten haben sich geaendert
            g_app->mapDirty = true;
        }
        g_app->tlDragging = false;
        g_app->tlDragPath.clear();
    }
    return true;
}

// Einen Befehl in der Zeit verschieben (siehe "Einen Block verschieben" in
// drawTimeline). Gibt zurueck, ob sich etwas geaendert hat.
bool verschiebeImAblauf(const Path& p, double delta, bool nurDieser) {
    if (p.empty() || std::fabs(delta) < 1.0) {
        return false;
    }
    const Script& s = g_app->doc.script();
    const std::vector<Node>* liste = &s.nodes;
    const Path eltern(p.begin(), p.end() - 1);
    if (!eltern.empty()) {
        const Node* e = nodeAt(s, eltern);
        if (e == nullptr) {
            return false;
        }
        liste = &e->children;
    }
    const std::size_t idx = p.back();
    if (idx >= liste->size()) {
        return false;
    }
    const auto zahlWait = [](const Node& n) {
        return n.kind == Node::Kind::Command && n.name == "wait" && !n.args.empty() &&
               n.args[0].kind == Arg::Kind::Number;
    };
    // Auch das blockiert, ist aber keine Zahl: dahinter wird eingefuegt.
    const auto grenze = [](const Node& n) {
        return n.kind == Node::Kind::Command &&
               (n.name == "wait" || n.name == "waitsignal" || n.name == "dowait");
    };
    std::size_t g = idx;
    while (g > 0 && !grenze((*liste)[g - 1])) {
        --g;
    }
    const bool davorZahl = g > 0 && zahlWait((*liste)[g - 1]);
    const double wert = davorZahl ? std::atof((*liste)[g - 1].args[0].text.c_str()) : 0.0;
    double d = delta;
    if (wert + d < 0.0) {
        d = -wert;   // weiter nach vorn geht es nicht: das wait ist aufgebraucht
    }
    if (std::fabs(d) < 1.0) {
        setStatus(tr(Str::TlMoveNone), 0, 0);
        return false;
    }
    // Das wait danach (fuer Alt).
    std::size_t n = idx + 1;
    while (n < liste->size() && !grenze((*liste)[n])) {
        ++n;
    }
    const bool danachZahl = n < liste->size() && zahlWait((*liste)[n]);
    const auto mitWert = [](Node x, double v) {
        char b[32];
        std::snprintf(b, sizeof(b), "%.3f", std::max(0.0, v));
        x.args[0].text = b;
        return x;
    };
    bool ok = false;
    if (davorZahl) {
        std::vector<std::pair<Path, Node>> ersatz;
        Path pw = eltern;
        pw.push_back(g - 1);
        ersatz.emplace_back(pw, mitWert((*liste)[g - 1], wert + d));
        if (nurDieser && danachZahl) {
            Path pn = eltern;
            pn.push_back(n);
            const double wn = std::atof((*liste)[n].args[0].text.c_str());
            ersatz.emplace_back(pn, mitWert((*liste)[n], wn - d));
        }
        ok = g_app->doc.replaceMany(ersatz);
    } else if (d > 0.0) {
        // Kein wait davor: eines einfuegen.
        Node w;
        w.kind = Node::Kind::Command;
        w.name = "wait";
        Arg a;
        a.kind = Arg::Kind::Number;
        w.args.push_back(a);
        Path pg = eltern;
        pg.push_back(g);
        ok = g_app->doc.insertBefore(pg, mitWert(w, d));
        if (ok && nurDieser && danachZahl) {
            Path pn = eltern;
            pn.push_back(n + 1);   // eins weiter: davor kam eine Zeile dazu
            const Node* nn = nodeAt(g_app->doc.script(), pn);
            if (nn != nullptr) {
                const double wn = std::atof(nn->args[0].text.c_str());
                (void)g_app->doc.replaceAt(pn, mitWert(*nn, wn - d));
            }
        }
    }
    if (ok) {
        diag::detail("Zeitleiste: Befehl um " + std::to_string(static_cast<int>(d)) + " ms verschoben" +
                     (nurDieser ? " (nur dieser)" : " (alles danach mit)"));
        rebuildTree();
        g_app->camTrackValid = false;
        g_app->mapDirty = true;
    }
    return ok;
}

// Passiert in dieser Spur nach dem Anfang noch etwas? "Ruhig" heisst: alle
// Befehle liegen bei 0 und haben keine Dauer (etwa ein einzelnes set zu
// Beginn). Solche Spuren blendet die Zeitleiste auf Wunsch aus.
bool spurIstRuhig(const TimelineTrack& spur) {
    for (const TimelineEvent& e : spur.events) {
        if (e.endMs > 1.0 || e.startMs > 1.0) {
            return false;
        }
    }
    return true;
}

void klaengeVorladen() {
    if (!g_app->playAudio) {
        return;
    }
    diag::Step step("Klaenge vorladen");
    int anzahl = 0;
    for (const TimelineTrack& track : g_app->timeline.tracks) {
        for (const TimelineEvent& e : track.events) {
            std::string datei;
            if (e.kind == TimelineEvent::Kind::Sound && !e.sound.empty()) {
                datei = e.sound;
            } else if (e.kind == TimelineEvent::Kind::Use) {
                bool istMusik = false;
                datei = useSoundFor(e.target, &istMusik);
            }
            if (!datei.empty() && klangFuerWiedergabe(datei) != nullptr) {
                ++anzahl;
            }
        }
    }
    diag::info(std::to_string(anzahl) + " Klaenge bereit");
}

void drawTimeline() {
    if (!g_app->camTrackValid) {
        // Ueber prepareScene(), damit es NUR EINE Stelle gibt, die die
        // Ansicht aufbaut. Vorher baute diese hier die Szene und leerte
        // die Modelle, und das Zeichnen lud sie kurz darauf nach - zwei
        // Haelften desselben Vorgangs an zwei Orten.
        //
        // Teuer ist nur das erste Mal: die Modelle liegen danach in
        // modelCache, die .npc-Dateien sind ueber npcMapRead abgehakt.
        // Nach einer Skriptaenderung kostet der Aufbau also fast nichts.
        prepareScene();
    }
    const CameraTrack& t = g_app->camTrack;
    // Ohne Kamerabefehle bleibt die Leiste stehen, nur ohne Wirkung - sonst
    // springt die Anordnung, sobald ein Skript geladen wird.
    const bool haveTrack = !t.segments.empty();
    ImGui::BeginDisabled(!haveTrack);
    if (!haveTrack) {
        // Auch ohne Kamerabefehle die LEISTE zeigen, nicht nur einen Satz.
        //
        // Sonst klafft an ihrer Stelle ein leeres Feld, und beim Laden
        // eines Skripts springt die Anordnung. Gezeigt wird dieselbe
        // Anordnung wie sonst - Zeiger und Lineal -, nur abgeblendet und
        // mit einer angenommenen Minute als Massstab.
        ImGui::TextDisabled("%s", tr(Str::TlNone));
        float none = 0.0F;
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##playheadoff", &none, 0.0F, 1.0F, "");
        const ImVec2 bMin = ImGui::GetItemRectMin();
        const ImVec2 bMax = ImGui::GetItemRectMax();
        const float bw = bMax.x - bMin.x;
        ImDrawList* dl0 = ImGui::GetWindowDrawList();
        const float lh0 = ImGui::GetFontSize() * 0.9F;
        const ImVec2 rp0{bMin.x, bMax.y + 4.0F};
        const ImU32 grau = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        for (int e = 0; e <= 60; e += 5) {
            const float x = rp0.x + static_cast<float>(e) / 60.0F * bw;
            dl0->AddLine(ImVec2{x, rp0.y + lh0 * 0.45F},
                         ImVec2{x, rp0.y + lh0}, grau);
            char be[16];
            std::snprintf(be, sizeof(be), "%d", e);
            dl0->AddText(ImVec2{x + 2.0F, rp0.y - 1.0F}, grau, be);
        }
        ImGui::Dummy(ImVec2{bw, lh0 + 4.0F});
        ImGui::EndDisabled();
        return;
    }
    ImGui::EndDisabled();

    // Wieder einsteigen, wo die Zeitmarke steht.
    //
    // Gemeldet: "wenn ich die Musik pausiere und wieder laufen lasse, hoert
    // sie auf zu spielen."
    //
    // Zwei Luecken, beide an derselben Stelle:
    //
    //   1. Der Abspielknopf stieg GAR NICHT wieder ein. Er setzte nur
    //      audioUpTo, und getroffen wird nur, was NEU anfaengt - ein Stueck,
    //      das laengst begonnen hatte, blieb stumm.
    //   2. Der Tonschalter stieg zwar ein, kannte aber nur Kind::Sound.
    //      Musik ist ein Kind::Use (sie haengt an einer Entity der Karte,
    //      seit rc255), fiel also auch dort durch.
    //
    // Deshalb blieb die Musik weg, bis das naechste use kam - in
    // intro_jedi liegen zwischen mus1 bei 0 ms und mus2 bei 48600 ms
    // achtundvierzig Sekunden.
    //
    // Beide Faelle rufen jetzt DIESE Funktion. Zwei Wege, die dasselbe
    // tun sollen, laufen sonst auseinander.
    const auto wiedereinstieg = [&]() {
        if (!g_app->playAudio) {
            return;
        }
        // Je Art getrennt: eine Stimme uebertoent die andere, aber Musik
        // und Stimme laufen NEBENeinander - sie haben seit rc257 eigene
        // Geraete.
        const TimelineEvent* stimme = nullptr;
        for (const TimelineTrack& track : g_app->timeline.tracks) {
            for (const TimelineEvent& e : track.events) {
                if (e.startMs > g_app->playMs) {
                    continue;
                }
                if (e.kind == TimelineEvent::Kind::Sound && !e.sound.empty()) {
                    // Die Laenge steht in der Datei, nicht im Skript.
                    const double ende =
                        e.startMs + soundLengthMs(e.sound);
                    if (ende > g_app->playMs &&
                        (stimme == nullptr || e.startMs > stimme->startMs)) {
                        stimme = &e;
                    }
                }
            }
        }
        if (stimme != nullptr) {
            playSound(stimme->sound, g_app->playMs - stimme->startMs);
        }
        // Musik: das letzte target_play_music vor dem Zeiger (aus der
        // aufgeloesten use-Kette des Nachbaus), sonst die Levelmusik ab 0.
        // Musik laeuft endlos (Schleife) - also kein "schon zu Ende".
        double musikAb = 0.0;
        std::string musikDatei = weltMusik();
        for (const Ablauf::Benutzung& b : g_app->ablauf.benutzt) {
            if (b.ms > g_app->playMs) {
                continue;
            }
            bool istMusik = false;
            const std::string datei = useSoundFor(b.name, &istMusik);
            if (istMusik && !datei.empty() && b.ms >= musikAb) {
                musikAb = b.ms;
                musikDatei = datei;
            }
        }
        if (!musikDatei.empty()) {
            spieleMusik(musikDatei, g_app->playMs - musikAb);
        }
    };

    if (ImGui::Button(g_app->playing ? tr(Str::TlStop) : tr(Str::TlPlay),
                      ImVec2{ImGui::GetFontSize() * 4.5F, 0.0F})) {
        g_app->playing = !g_app->playing;
        if (!g_app->playing) {
            g_app->audio.stopAll();
            g_app->laufendeSchleifen.clear();
            g_app->musicAudio.stopAll();
        } else {
            // Erst alle Klaenge laden, DANN die Uhr laufen lassen. Vorher
            // wurde jeder Klang erst geladen, wenn er dran war; das Bild
            // stand 130-215 ms, und die Zeit sprang danach um genau so viel
            // vor (Selbsttest: 1582 ms Skriptzeit in 955 ms).
            klaengeVorladen();
            g_app->uhrAussetzen = 2;   // dieses und das naechste Bild
        }
        // Am Ende angekommen? Dann von vorn.
        if (g_app->playing && g_app->playMs >= t.durationMs - 1.0) {
            g_app->playMs = 0.0;
        }
        // Ab hier zaehlen: sonst kaeme beim Fortsetzen alles nach, was
        // waehrend der Pause im Fenster lag.
        g_app->audioUpTo = g_app->playing ? g_app->playMs - 0.001 : g_app->playMs;
        if (g_app->playing) {
            wiedereinstieg();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::TlRewind))) {
        g_app->playMs = 0.0;
        // -1, damit ein Klang bei 0 ms beim naechsten Abspielen kommt.
        g_app->audioUpTo = -1.0;
        g_app->audio.stopAll();
        g_app->laufendeSchleifen.clear();
        g_app->musicAudio.stopAll();
        g_app->mapDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(tr(Str::TlAudio), &g_app->playAudio)) {
        if (!g_app->playAudio) {
            g_app->audio.stopAll();
            g_app->laufendeSchleifen.clear();
            g_app->musicAudio.stopAll();
        } else if (g_app->playing) {
            // Beim EINschalten dort einsteigen, wo die Einstellung steht.
            //
            // Vorher passierte hier nichts: getroffen wird nur, was NEU
            // anfaengt, und der laufende Klang hatte schon begonnen. Also
            // blieb es still bis zum naechsten - beim Anwender fuenf
            // Sekunden, waehrend die Szene weiterlief.
            //
            // Der SPAETESTE Klang gewinnt: liegen zwei uebereinander, ist
            // der zuletzt begonnene der, den man hoert.
            wiedereinstieg();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::TlAudioHint));
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(tr(Str::TlFollow), &g_app->followCam)) {
        g_app->mapDirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::TlFollowHint));
    }
    ImGui::SameLine();
    // --- Tempo und Schleife (Sequencer) --------------------------------
    {
        static const float kTempi[] = {0.25F, 0.5F, 1.0F, 2.0F};
        char tempo[16];
        std::snprintf(tempo, sizeof(tempo), "%gx", static_cast<double>(g_app->abspielTempo));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0F);
        if (ImGui::BeginCombo("##tempo", tempo)) {
            for (const float v : kTempi) {
                char n[16];
                std::snprintf(n, sizeof(n), "%gx", static_cast<double>(v));
                if (ImGui::Selectable(n, v == g_app->abspielTempo)) {
                    g_app->abspielTempo = v;
                    if (v != 1.0F) {
                        g_app->audio.stopAll();
                        g_app->laufendeSchleifen.clear();
                        g_app->musicAudio.stopAll();
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::TlSpeedHint));
        }
        ImGui::SameLine();
        ImGui::Checkbox(tr(Str::TlLoop), &g_app->schleife);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::TlLoopHint));
        }
    }
    ImGui::SameLine();
    // Bilder oder Sekunden - umschaltbar, wie in 3ds Max. Die Anzeige
    // rechts daneben richtet sich danach.
    if (ImGui::Button(g_app->timelineFrames ? tr(Str::TlUnitFrames)
                                            : tr(Str::TlUnitSeconds))) {
        g_app->timelineFrames = !g_app->timelineFrames;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::TlUnitHint));
    }
    ImGui::SameLine();
    if (g_app->timelineFrames) {
        const double fps = static_cast<double>(std::max(1, g_app->timelineFps));
        ImGui::TextDisabled(tr(Str::TlFrame),
                            static_cast<int>(g_app->playMs / 1000.0 * fps),
                            static_cast<int>(t.durationMs / 1000.0 * fps));
    } else {
        ImGui::TextDisabled(tr(Str::TlTime), g_app->playMs / 1000.0,
                            t.durationMs / 1000.0);
    }
    // Die Hoehe stellt man seit rc200 an der KANTE ueber dem Band ein,
    // nicht mehr mit einem Regler. Zwei Wege fuer dieselbe Sache waeren
    // genau der Fehler, der in dieser Sitzung viermal aufgetreten ist.
    ImGui::SameLine();
    // Zurueck zur Gesamtansicht. Ohne diesen Knopf sucht man sich nach
    // starkem Zoom muehsam zurueck - der Ausschnitt sagt ja nicht, wie weit
    // man vom Ganzen entfernt ist.
    // --- Ein kleines Menue fuer den Ausschnitt ---------------------------
    //
    // Radeln allein genuegt nicht: man sieht dem Rad nicht an, dass es
    // etwas tut, und feste Stufen trifft man damit nie genau. Der Knopf
    // zeigt zugleich den aktuellen Grad an - damit ist beides sichtbar,
    // die Moeglichkeit und der Zustand.
    {
        char knopf[48];
        if (g_app->timelineZoom > 1.0) {
            char grad[24];
            std::snprintf(grad, sizeof(grad), tr(Str::TlZoomFactor),
                          g_app->timelineZoom);
            std::snprintf(knopf, sizeof(knopf), "%s: %s", tr(Str::TlZoomMenu),
                          grad);
        } else {
            std::snprintf(knopf, sizeof(knopf), "%s: %s", tr(Str::TlZoomMenu),
                          tr(Str::TlFit));
        }
        if (ImGui::Button(knopf)) {
            ImGui::OpenPopup("##zoommenu");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::TlZoomHint));
        }
        if (ImGui::BeginPopup("##zoommenu")) {
            // Beim Umstellen die MITTE festhalten - sonst springt man
            // beim Stufenwechsel an den Anfang zurueck und sucht die
            // Stelle wieder, die man gerade ansah.
            const double gesamtM = std::max({g_app->timeline.practicalEndMs,
                                             t.durationMs, 1.0});
            const auto stellen = [&](double neuerZoom, double mitteMs) {
                const double zz = std::clamp(neuerZoom, 1.0, App::kMaxZoom);
                const double sp = gesamtM / zz;
                g_app->timelineZoom = zz;
                g_app->timelineViewStartMs =
                    std::clamp(mitteMs - sp * 0.5, 0.0,
                               std::max(0.0, gesamtM - sp));
            };
            const double mitteJetzt =
                g_app->timelineViewStartMs +
                gesamtM / std::max(g_app->timelineZoom, 1.0) * 0.5;

            if (ImGui::MenuItem(tr(Str::TlFit), nullptr,
                                g_app->timelineZoom <= 1.0)) {
                g_app->timelineZoom = 1.0;
                g_app->timelineViewStartMs = 0.0;
            }
            ImGui::Separator();
            for (const double stufe : {2.0, 4.0, 8.0, 16.0, 32.0}) {
                char be[24];
                std::snprintf(be, sizeof(be), tr(Str::TlZoomFactor), stufe);
                if (ImGui::MenuItem(be, nullptr,
                                    std::fabs(g_app->timelineZoom - stufe) <
                                        0.01)) {
                    stellen(stufe, mitteJetzt);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr(Str::TlZoomToPlay))) {
                stellen(std::max(g_app->timelineZoom, 8.0), g_app->playMs);
            }
            ImGui::EndPopup();
        }
    }

    ImGui::SameLine();
    // Das Kaestchen stand bisher NEBEN dem Lineal und lag damit mitten in
    // den Zahlen - gemeldet als "die track checkbox ist falsch". Es gehoert
    // zur Bedienung, also in diese Zeile.
    if (ImGui::Checkbox(tr(Str::TlTracks), &g_app->showTracks)) {
        g_app->mapDirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::TlTracksHint));
    }
    // Ruhige Spuren: nach dem Anfang passiert darin nichts mehr (in
    // intro_jedi 6 von 10). Ausgeblendet, damit die Spuren mit Handlung Platz
    // haben; der Schalter nennt, wie viele es sind.
    {
        int ruhig = 0;
        for (const TimelineTrack& tr2 : g_app->timeline.tracks) {
            if (spurIstRuhig(tr2)) { ++ruhig; }
        }
        if (ruhig > 0 && g_app->showTracks) {
            ImGui::SameLine();
            char lab[64];
            std::snprintf(lab, sizeof(lab), tr(Str::TlIdleTracks), ruhig);
            ImGui::Checkbox(lab, &g_app->zeigeRuhigeSpuren);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr(Str::TlIdleTracksHint));
            }
        }
    }

    if (g_app->playing) {
        // audioUpTo statt "der Stand vom letzten Bild".
        //
        // Der Unterschied ist genau ein Klang: einer bei 0 ms. Mit
        // "startMs > vorheriger Stand" ist die Bedingung beim ersten Bild
        // 0 > 0 - also nie wahr, und der erste Satz einer Sequenz bliebe
        // stumm. audioUpTo beginnt bei -1, und damit faellt auch die Null
        // ins Fenster.
        if (g_app->uhrAussetzen > 0) {
            --g_app->uhrAussetzen;   // Vorladezeit ist keine Abspielzeit
        } else {
            g_app->playMs += static_cast<double>(ImGui::GetIO().DeltaTime) * 1000.0 *
                             static_cast<double>(g_app->abspielTempo);
        }
        // Klaenge ausloesen, die in dieses Zeitfenster fallen.
        //
        // Ueber das FENSTER, nicht ueber "steht der Zeiger genau darauf":
        // bei sechzig Bildern je Sekunde liegen sechzehn Millisekunden
        // zwischen zwei Abfragen, ein Klang faellt sonst durch.
        if (g_app->playAudio && g_app->abspielTempo == 1.0F) {
            for (const TimelineTrack& track : g_app->timeline.tracks) {
                for (const TimelineEvent& e : track.events) {
                    if (e.startMs <= g_app->audioUpTo ||
                        e.startMs > g_app->playMs) {
                        continue;
                    }
                    if (e.kind == TimelineEvent::Kind::Sound &&
                        !e.sound.empty()) {
                        spieleSkriptKlang(track.entity, e.sound, e.kanal, e.startMs);
                    } else if (e.kind == TimelineEvent::Kind::Saber) {
                        // Die Klinge gehoert der Figur der SPUR - der
                        // affect-Block sagt, wer gemeint ist. Am Ort der
                        // Figur (cg_players.cpp: S_StartSound auf ihre
                        // Entity, CHAN_AUTO).
                        const Actor* fig =
                            g_app->scene.find(track.entity);
                        if (fig != nullptr) {
                            const std::string klang = saberSoundFor(*fig, e.on, e.startMs);
                            if (!klang.empty()) {
                                spieleSkriptKlang(track.entity, klang, "CHAN_AUTO", e.startMs);
                            }
                        }
                    }
                }
            }
            // --- Die Levelmusik (worldspawn "music") ab 0 ms --------------
            if (g_app->audioUpTo < 0.0 && g_app->playMs >= 0.0) {
                const std::string welt = weltMusik();
                if (!welt.empty()) {
                    spieleMusik(welt, g_app->playMs);
                }
            }
            // --- use: Musik und Lautsprecher ----------------------------
            //
            // Aus dem Nachbau, nicht aus der Zeitleiste: dort stehen nur die
            // use-Befehle des Skripts, hier auch alles, was ueber
            // target_relay, target_delay, target_counter und trigger_*
            // weitergereicht wird. Ein Lautsprecher hinter einem Relay blieb
            // vorher stumm.
            for (const Ablauf::Benutzung& b : g_app->ablauf.benutzt) {
                if (b.ms <= g_app->audioUpTo || b.ms > g_app->playMs) {
                    continue;
                }
                bool musik = false;
                const std::string datei = useSoundFor(b.name, &musik);
                if (datei.empty()) {
                    continue;
                }
                if (musik) {
                    spieleMusik(datei, 0.0);
                } else {
                    spieleLautsprecher(b.name, datei);
                }
            }
            // --- Die Klaenge der MOVER ----------------------------------
            //
            // Tueren und Co. mit "soundSet": START bei der Abfahrt, die
            // Schleife waehrend der Fahrt, END bei der Ankunft - am Ort der
            // Tuer, gedaempft nach Entfernung wie die Effektklaenge
            // (G_PlayDoorSound, g_mover.cpp:88).
            for (const MoverKlang& mk : g_app->moverKlaenge) {
                if (mk.ms > g_app->playMs) {
                    break;   // nach Zeit geordnet
                }
                if (mk.ms <= g_app->audioUpTo) {
                    continue;
                }
                std::string setName = mk.soundSet;
                for (char& c : setName) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                const auto set = g_app->bmodelSets.find(setName);
                if (set == g_app->bmodelSets.end() ||
                    static_cast<std::size_t>(mk.stufe) >= set->second.size() ||
                    set->second[static_cast<std::size_t>(mk.stufe)].empty()) {
                    continue;   // wie AS_GetBModelSound: -1, kein Klang
                }
                const std::string& datei = set->second[static_cast<std::size_t>(mk.stufe)];
                float ort[3];
                g_app->moverSim.klangOrt(mk.modell, mk.ms, ort);
                float ohr[3];
                float rechts[3];
                hoerer(ohr, rechts);
                // Am Ort, mit Links/rechts wie S_SpatializeOrigin.
                const sound::Raumlaut r = sound::raumlaut(ort, ohr, rechts, sound::Kanal::Auto);
                if (mk.stufe == 1) {
                    playSoundSchleife(datei, mk.bisMs - mk.ms, 0.5F * (r.links + r.rechts));
                } else {
                    playSound(datei, 0.0, false, r.links, r.rechts, 0);
                }
            }
            // --- Die Klaenge beim ZERBRECHEN ---------------------------
            //
            // Material ("glasslcar", "crateBust1", "wall_smash" ...), der
            // eigene "noise" eines func_breakable und cargoexplode
            // (CG_Chunks, funcBBrushDieGo, misc_model_breakable_die) - am
            // Ort des Bruchs, gedaempft wie die Moverklaenge.
            //
            // Die target_speaker (MoverSim::lautsprecher) spielen hier NICHT:
            // ihre Einmalklaenge laufen schon ueber useSoundFor oben, die
            // Schleifen und das Abspielen am Ort stehen noch aus.
            for (const KartenKlang& bk : g_app->moverSim.bruchKlaenge()) {
                if (bk.ms > g_app->playMs) {
                    break;   // nach Zeit geordnet
                }
                if (bk.ms <= g_app->audioUpTo) {
                    continue;
                }
                const float ohr[3] = {g_app->cam.pos[0], g_app->cam.pos[1], g_app->cam.pos[2]};
                playSound(bk.datei, 0.0, false, sound::distanceVolume(bk.ort, ohr));
            }
            // --- Die Klaenge der EFFEKTE --------------------------------
            //
            // Ein Effekt kann Klang machen: `sound` ist ein Primitivtyp wie
            // `particle` oder `line`, und der Block nennt eine Liste von
            // Dateien:
            //
            //     sound
            //     {
            //         sounds
            //         [
            //             sound/weapons/detpack/fire.wav
            //         ]
            //     }
            //
            // Ueber die 376 .efx-Dateien aus assets1 gezaehlt: 156
            // Klangprimitive - nach den Partikeln der zweithaeufigste Typ
            // ueberhaupt. Bisher wurden sie gelesen und nie gespielt.
            //
            // Die Auslesestellen stehen laengst da: effectRunners kennt je
            // Effekt den Startzeitpunkt, ob er sich wiederholt und in
            // welchem Abstand. Nur das Abspielen fehlte.
            if (g_app->playAudio) {
                for (const EffectInstance& fx : g_app->effectRunners) {
                    if (fx.effect == nullptr) {
                        continue;
                    }
                    // Welche Wiederholungen fallen in dieses Fenster?
                    //
                    // Ueber das FENSTER, nicht ueber "steht der Zeiger
                    // genau darauf" - derselbe Grund wie bei den Stimmen
                    // weiter oben: bei sechzig Bildern je Sekunde faellt
                    // sonst jeder zweite Klang durch.
                    const double ab = std::max(g_app->audioUpTo, fx.startMs);
                    if (fx.startMs > g_app->playMs) {
                        continue;
                    }
                    int von = 0;
                    int bis = 0;
                    if (fx.loops && fx.intervalMs > 1.0F) {
                        von = static_cast<int>(
                            std::ceil((ab - fx.startMs) / fx.intervalMs));
                        bis = static_cast<int>(
                            std::floor((g_app->playMs - fx.startMs) /
                                       fx.intervalMs));
                        // Eine Obergrenze: springt man weit nach vorn, lagen
                        // sonst hunderte Wiederholungen im Fenster, und
                        // waveOut nimmt sie alle an.
                        bis = std::min(bis, von + 2);
                    } else if (fx.startMs <= g_app->audioUpTo) {
                        continue;   // einmalig und schon gewesen
                    }
                    for (int n = von; n <= bis; ++n) {
                        for (const efx::Primitive& pr :
                             fx.effect->primitives) {
                            if (pr.type != efx::PrimitiveType::Sound ||
                                pr.sounds.empty()) {
                                continue;
                            }
                            // Die Engine waehlt aus der Liste zufaellig.
                            // Hier ueber Saat und Wiederholungsnummer,
                            // damit dieselbe Stelle immer dasselbe spielt -
                            // in einer Vorschau, die man zurueckspult, ist
                            // echter Zufall Unruhe statt Wiedergabetreue.
                            const std::size_t w =
                                (fx.seed + static_cast<unsigned>(n)) %
                                pr.sounds.size();
                            // --- MIT der Entfernung -----------------
                            //
                            // Gemeldet: "die Effektsounds sind so
                            // penetrant und spielen dauerhaft."
                            //
                            // Die Engine spielt sie AM ORT und daempft
                            // nach Entfernung (snd_dma.cpp:1356 ff.).
                            // behaved spielte jeden in voller Lautstaerke
                            // - bei 28 fx_runnern in einer Karte ergibt
                            // das genau diesen Dauerlaerm.
                            //
                            // Gehoert wird von der Kamera aus, denn die
                            // ist hier der Zuhoerer.
                            float ohr[3];
                            float rechtsA[3];
                            hoerer(ohr, rechtsA);
                            const sound::Raumlaut rl =
                                sound::raumlaut(fx.origin, ohr, rechtsA, sound::Kanal::Auto);
                            const float laut = std::max(rl.links, rl.rechts);
                            playSound(pr.sounds[w], 0.0, false, rl.links, rl.rechts, 0);
                            // Ins Protokoll, damit sich nachvollziehen
                            // laesst, WARUM ein Klang laut oder leise war.
                            // Gemeldet wird nur, was auch zu hoeren ist.
                            if (laut > 0.001F) {
                                const float dx = fx.origin[0] - ohr[0];
                                const float dy = fx.origin[1] - ohr[1];
                                const float dz = fx.origin[2] - ohr[2];
                                char kz[200];
                                std::snprintf(
                                    kz, sizeof(kz),
                                    "Effektklang \"%s\": %.0f Einheiten "
                                    "entfernt, Lautstaerke %.0f %%",
                                    pr.sounds[w].c_str(),
                                    std::sqrt(dx * dx + dy * dy + dz * dz),
                                    static_cast<double>(laut) * 100.0);
                                diag::detail(kz);
                            }
                        }
                    }
                }
            }
        }
        // --- Dauerklaenge: Schwertsummen und Lautsprecherschleifen -------
        //
        // Wie die Loop-Sounds der Engine (cgi_S_AddLoopingSound, jedes Bild
        // neu gemeldet): was jetzt laufen soll, laeuft; was nicht mehr soll,
        // verstummt. Das Summen kommt aus der .sab (soundLoop, CG_AddSaberBlade),
        // die Lautsprecher aus target_speaker mit spawnflags 1/2 (an/aus per
        // use, MoverSim::lautsprecher). Am Ort, mit Panorama beim Start.
        if (g_app->playAudio && g_app->abspielTempo == 1.0F) {
            float ohr[3];
            float rechtsV[3];
            hoerer(ohr, rechtsV);
            std::set<std::uint64_t> soll;
            const auto schluessel = [](const std::string& s) {
                std::uint64_t h = 1469598103934665603ULL;
                for (const char c : s) { h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ULL; }
                return h | 1ULL;
            };
            for (const Actor& a : g_app->scene.actors) {
                const ActorState st = a.at(g_app->playMs);
                if (!st.visible || st.saberLoop.empty()) {
                    continue;
                }
                const std::uint64_t k = schluessel("summen:" + a.name);
                soll.insert(k);
                if (g_app->laufendeSchleifen.count(k) == 0) {
                    const sound::Raumlaut r = sound::raumlaut(st.pos, ohr, rechtsV, sound::Kanal::Auto);
                    playSoundSchleife(st.saberLoop, 120000.0, r.links, r.rechts, k);
                    g_app->laufendeSchleifen.insert(k);
                }
            }
            for (const KartenKlang& kk : g_app->moverSim.lautsprecher()) {
                if (!kk.schleife || kk.datei.empty() || kk.ms > g_app->playMs ||
                    (kk.bisMs >= 0.0 && kk.bisMs <= g_app->playMs)) {
                    continue;
                }
                const std::uint64_t k = schluessel("lautsprecher:" + std::to_string(kk.entity));
                soll.insert(k);
                if (g_app->laufendeSchleifen.count(k) == 0) {
                    sound::Raumlaut r;
                    if (!kk.global) {
                        r = sound::raumlaut(kk.ort, ohr, rechtsV, sound::Kanal::Auto);
                    }
                    const double rest = (kk.bisMs >= 0.0) ? kk.bisMs - g_app->playMs : 120000.0;
                    playSoundSchleife(kk.datei, rest, r.links, r.rechts, k);
                    g_app->laufendeSchleifen.insert(k);
                }
            }
            for (auto it = g_app->laufendeSchleifen.begin(); it != g_app->laufendeSchleifen.end();) {
                if (soll.count(*it) == 0) {
                    if (g_app->audio.ready()) {
                        (void)g_app->audio.play({}, 0.0F, 0.0F, *it);   // abschneiden
                    }
                    it = g_app->laufendeSchleifen.erase(it);
                } else {
                    ++it;
                }
            }
        }
        g_app->audioUpTo = g_app->playMs;
        if (g_app->playMs >= t.durationMs) {
            if (g_app->schleife && t.durationMs > 1.0) {
                // Von vorn, ohne anzuhalten - und die Klaenge ab 0 wieder.
                g_app->playMs = 0.0;
                g_app->audioUpTo = -1.0;
                g_app->audio.stopAll();
                g_app->laufendeSchleifen.clear();
                g_app->musicAudio.stopAll();
            } else {
                g_app->playMs = t.durationMs;
                g_app->playing = false;
            }
        }
        g_app->mapDirty = true;
    }

    // --- EINE Namensspalte fuer alles ------------------------------------
    //
    // Zeiger, Balken und Lineal begannen am linken Rand, die Spuren erst
    // hinter ihren Namen. Damit stand ueber jeder Spur eine Zeitachse, die
    // um die Namensbreite verschoben war - die Zahl 20 stand nicht ueber
    // der Sekunde 20. Gemeldet als "die timeline startet schon vor den
    // scripts".
    //
    // Jetzt gibt es EINE Kante, an der alles beginnt. Sie richtet sich nach
    // dem laengsten Namen, damit auch "bigguard1" hineinpasst, bleibt aber
    // in Grenzen - sonst frisst die Namensspalte die Zeitachse auf.
    float labelW = ImGui::GetFontSize() * 6.0F;
    for (const TimelineTrack& tr2 : g_app->timeline.tracks) {
        const std::string nm = tr2.entity.empty() ? std::string(tr(Str::TlScript))
                                                  : tr2.entity;
        labelW = std::max(labelW, ImGui::CalcTextSize(nm.c_str()).x);
    }
    labelW = std::min(labelW + ImGui::GetStyle().ItemSpacing.x * 2.0F,
                      ImGui::GetContentRegionAvail().x * 0.25F);

    // Der Zeiger steht seit 27.09. IM LINEAL: klicken oder ziehen setzt die
    // Zeit (wie im Unreal-Sequencer und in jedem Schnittprogramm). Der
    // eigene Schieberegler darueber kostete eine Zeile und war ein zweites
    // Bedienelement fuer dieselbe Sache.
    ImGui::Dummy(ImVec2{labelW, 1.0F});
    ImGui::SameLine(0.0F, 0.0F);
    // Die ROLLLEISTE der Spurenliste mit einrechnen.
    //
    // Die Spuren stehen in einem Kindfenster, das senkrecht rollt - seine
    // Rollleiste nimmt sich Platz vom INHALT. Zeiger und Lineal darueber
    // haben keine und reichten deshalb ein Stueck weiter nach rechts als
    // die Balken darunter. Gemeldet als "die timeline rechts ist bisschen
    // zu lang mit dem runter scrollen".
    //
    // Der Platz wird IMMER abgezogen, nicht nur wenn gerade gerollt wird -
    // sonst wandert die rechte Kante, sobald eine Spur dazukommt oder
    // wegfaellt, und die Zahlen im Lineal springen mit.
    const float rollBreite = ImGui::GetStyle().ScrollbarSize;
    ImGui::SetNextItemWidth(-rollBreite);
    // Der Regler laeuft ueber den SICHTBAREN Bereich. Sonst waere beim
    // Hineinzoomen der Griff auf einem Zehntel Bildpunkt zu treffen.
    // Hinter dem Ende 5 % Luft: sonst liegt die Endkante des letzten Blocks
    // genau am rechten Rand, und man kann ihn kaum greifen und verlaengern.
    const double ablaufEnde = std::max({g_app->timeline.practicalEndMs, t.durationMs, 1.0});
    const double rGesamt = ablaufEnde * 1.05;
    const double rSpanne = rGesamt / std::clamp(g_app->timelineZoom, 1.0, App::kMaxZoom);
    const double rStart = std::clamp(g_app->timelineViewStartMs, 0.0,
                                     std::max(0.0, rGesamt - rSpanne));
    const ImVec2 barMin = ImGui::GetCursorScreenPos();
    const float w = std::max(40.0F, ImGui::GetContentRegionAvail().x - rollBreite);
    (void)rStart;

    // Der UEBERSICHTSBALKEN stand frueher HIER - zwischen Zeiger und
    // Lineal. Gemeldet: "da ist was ueber unserer Timeline, alles wo Dinge
    // wie camera sind sollte unter der Timeline sein."
    //
    // Er ist jetzt hinter das Lineal gerueckt. Der Grund ist nicht nur
    // Geschmack: der Balken zeigt Kamerabefehle, und die Kamerazeilen der
    // Spurenliste zeigen dieselben Befehle noch einmal einzeln. Steht der
    // Balken oben, liegt eine Kameraanzeige ueber der Zeitachse und eine
    // darunter - zwei Orte fuer dieselbe Sache, getrennt durch die
    // Beschriftung. Unten stehen sie beieinander.

    // --- Die Spuren ------------------------------------------------------
    //
    // Eine Zeile je affect-Ziel. Das ist der Teil, fuer den die Zeitleiste
    // wirklich da ist: man sieht, wann welche Figur spricht, und kann den
    // Schnitt darauf abstimmen.
    //
    // In intro_jedi.txt sind das zehn Spuren - das Skript selbst mit 79
    // Kamerabefehlen, dazu anakin1, barriss1 und die Wachen. Die Klaenge
    // liegen bei 20,2 s (barriss1), 23,0 s (anakin1) und so fort.
    // --- Das Lineal ------------------------------------------------------
    //
    // Bilder ODER Sekunden, umschaltbar - in 3ds Max geht beides, und
    // beides wird gebraucht: Sekunden fuer die Laenge einer Einstellung,
    // Bilder fuer das genaue Setzen eines Schluessels. JKA rechnet intern
    // in Millisekunden; die Bildzahl ist eine reine Anzeige.
    {
        // Hoeher als frueher (0,9 Zeilen), und mit eigenem Grund.
        //
        // Das Rad wirkt ueber DIESEM Streifen - bei einer knappen Zeile
        // Hoehe trifft man ihn kaum und weiss auch nicht, dass dort etwas
        // zu holen ist. Gemeldet als "was fuer ein Lineal? wie geht das?".
        // Jetzt ist er anderthalb Zeilen hoch und hebt sich ab, damit man
        // sieht, dass er ein Bedienelement ist.
        const float lh = ImGui::GetFontSize() * 1.5F;
        ImVec2 rp = ImGui::GetCursorScreenPos();
        rp.x = barMin.x;   // dieselbe Kante wie Zeiger, Balken und Spuren
        ImDrawList* rl = ImGui::GetWindowDrawList();
        // Ein eigener Grund, damit der Streifen als Leiste zu erkennen ist.
        rl->AddRectFilled(ImVec2{rp.x, rp.y},
                          ImVec2{rp.x + w, rp.y + lh},
                          IM_COL32(34, 37, 46, 255));
        // Nur den SICHTBAREN Bereich beschriften. Vorher lief das Lineal
        // immer von null bis zum Ende - beim Zoomen haetten die Zahlen
        // nicht mehr zu den Balken darunter gepasst.
        const double gesamt = rGesamt;
        const double zSpanne = gesamt / std::clamp(g_app->timelineZoom, 1.0, App::kMaxZoom);
        const double zStart =
            std::clamp(g_app->timelineViewStartMs, 0.0,
                       std::max(0.0, gesamt - zSpanne));
        const double fps = static_cast<double>(std::max(1, g_app->timelineFps));
        // Schrittweite so waehlen, dass die Beschriftungen nicht kleben:
        // etwa alle 80 Bildpunkte eine. Die Stufen sind die gewohnten -
        // 1, 2, 5, 10, ... - damit runde Zahlen dastehen.
        // In der eingestellten Einheit: erste und letzte sichtbare Marke.
        const double jeMs = g_app->timelineFrames ? (fps / 1000.0) : (1.0 / 1000.0);
        const double vonE = zStart * jeMs;
        const double bisE = (zStart + zSpanne) * jeMs;
        const double sichtbar = std::max(bisE - vonE, 1.0e-6);
        // Schrittweite in den gewohnten Stufen 1, 2, 5, 10 ... - beim
        // Hineinzoomen wird sie von selbst feiner, weil "sichtbar" kleiner
        // wird. Genau dafuer ist die Rechnung da.
        double schritt = 1.0;
        // Wie viel Platz braucht EINE Beschriftung? Gemessen, nicht
        // geraten: bei 144 dpi ist "1234.5" deutlich breiter als die 80
        // Bildpunkte, mit denen hier frueher gerechnet wurde - und dann
        // stehen die Zahlen ineinander, wie im gemeldeten Bild.
        const float platz =
            ImGui::CalcTextSize("0000.0").x + ImGui::GetFontSize();
        const double gewuenscht =
            std::max(1.0e-6, sichtbar / std::max(w / platz, 1.0F));
        if (gewuenscht < 1.0) {
            // Die kleinste Stufe, die NOCH GROSS GENUG ist.
            //
            // Vorher lief die Schleife, bis der Schritt KLEINER war als
            // gewuenscht - also eine Stufe zu fein, und damit standen
            // doppelt so viele Zahlen da wie Platz war. Jetzt wird nur
            // halbiert, solange die Haelfte noch reicht.
            while (schritt * 0.5 >= gewuenscht) { schritt *= 0.5; }
        } else {
            while (schritt < gewuenscht) {
                if (schritt * 2.0 >= gewuenscht) { schritt *= 2.0; break; }
                if (schritt * 5.0 >= gewuenscht) { schritt *= 5.0; break; }
                schritt *= 10.0;
            }
        }
        const double ersteE = std::ceil(vonE / schritt) * schritt;
        for (double e = ersteE; e <= bisE + schritt * 0.001; e += schritt) {
            const auto x =
                rp.x + static_cast<float>((e - vonE) / sichtbar) * w;
            rl->AddLine(ImVec2{x, rp.y + lh * 0.45F}, ImVec2{x, rp.y + lh},
                        IM_COL32(120, 130, 150, 255));
            char be[24];
            // Bei starkem Zoom sind ganze Zahlen zu grob. So viele
            // Nachkommastellen, wie der Schritt braucht: bei 0,25 zwei -
            // mit einer stand "0.2, 0.5, 0.8" da, wo 0,25, 0,5, 0,75 gemeint
            // war (Zeitleistentest 27.09.).
            int stellen = 0;
            while (stellen < 3) {
                const double f = schritt * std::pow(10.0, stellen);
                if (std::fabs(f - std::round(f)) < 1.0e-6) { break; }
                ++stellen;
            }
            std::snprintf(be, sizeof(be), "%.*f", stellen, e);
            rl->AddText(ImVec2{x + 2.0F, rp.y - 1.0F},
                        IM_COL32(150, 160, 180, 255), be);
        }
        // --- Zoomen und Verschieben ---------------------------------
        //
        // Mausrad ueber dem Lineal vergroessert um den PUNKT UNTER DER
        // MAUS - so, wie es Blender und jedes Schnittprogramm machen.
        // Der Zeitwert dort bleibt stehen, alles andere rueckt darum
        // herum auseinander. Ohne diesen Bezug zoomt man ins Blaue und
        // muss danach suchen.
        //
        // Mittlere Taste ziehen schiebt den Ausschnitt.
        g_app->tlLinealY = rp.y + lh * 0.5F;
        ImGui::InvisibleButton("##zeitlineal", ImVec2{w, lh});
        // Klicken oder Ziehen mit der linken Taste setzt die Zeit.
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const double anteil = std::clamp<double>(
                (ImGui::GetIO().MousePos.x - rp.x) / std::max(w, 1.0F), 0.0, 1.0);
            const double ms = std::clamp(zStart + anteil * zSpanne, 0.0, gesamt);
            g_app->playMs = ms;
            g_app->playing = false;
            // Beim Ziehen nichts nachtroepfeln lassen.
            g_app->audioUpTo = ms;
            g_app->audio.stopAll();
            g_app->laufendeSchleifen.clear();
            g_app->musicAudio.stopAll();
            g_app->mapDirty = true;
        }
        // Der Zeiger im Lineal: eine Spitze oben und eine Linie, dazu das
        // Ende des Ablaufs als rote Marke.
        {
            const auto px = rp.x + static_cast<float>((g_app->playMs - zStart) / zSpanne) * w;
            if (px >= rp.x - 1.0F && px <= rp.x + w + 1.0F) {
                const float sp = ImGui::GetFontSize() * 0.35F;
                rl->AddTriangleFilled(ImVec2{px - sp, rp.y}, ImVec2{px + sp, rp.y}, ImVec2{px, rp.y + sp * 1.4F},
                                      IM_COL32(255, 255, 255, 235));
                rl->AddLine(ImVec2{px, rp.y}, ImVec2{px, rp.y + lh}, IM_COL32(255, 255, 255, 235), 1.5F);
            }
            const auto ex = rp.x + static_cast<float>((ablaufEnde - zStart) / zSpanne) * w;
            if (ex >= rp.x && ex <= rp.x + w) {
                rl->AddLine(ImVec2{ex, rp.y}, ImVec2{ex, rp.y + lh}, IM_COL32(230, 80, 80, 230), 2.0F);
            }
        }
        if (ImGui::IsItemHovered()) {
            const float rad = ImGui::GetIO().MouseWheel;
            if (rad != 0.0F) {
                const float mausX = ImGui::GetIO().MousePos.x;
                const double anteil =
                    std::clamp<double>((mausX - rp.x) / std::max(w, 1.0F), 0.0, 1.0);
                const double unterMaus = zStart + anteil * zSpanne;
                const double neu =
                    std::clamp(g_app->timelineZoom * std::pow(1.25, rad), 1.0, App::kMaxZoom);
                const double neueSpanne = gesamt / neu;
                g_app->timelineZoom = neu;
                g_app->timelineViewStartMs =
                    std::clamp(unterMaus - anteil * neueSpanne, 0.0,
                               std::max(0.0, gesamt - neueSpanne));
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                const float dx = ImGui::GetIO().MouseDelta.x;
                g_app->timelineViewStartMs =
                    std::clamp(g_app->timelineViewStartMs -
                                   static_cast<double>(dx) / std::max(w, 1.0F) *
                                       zSpanne,
                               0.0, std::max(0.0, gesamt - zSpanne));
            }
            ImGui::SetTooltip("%s", tr(Str::TlZoomHint));
        }
    }

    // --- Der Uebersichtsbalken -------------------------------------------
    //
    // Eine Zeile, in der alle Kamerabefehle nebeneinander liegen: blau
    // fahren, orange schwenken, gruen zoomen. Darunter stehen dieselben
    // Befehle noch einmal einzeln je Art - der Balken ist der Blick aufs
    // Ganze, die Zeilen sind die Einzelheiten.
    //
    // Er steht seit rc242 UNTER dem Lineal. Vorher lag er darueber, also
    // auf der falschen Seite der Zeitachse.
    //
    // Seit 27.09. nur noch, wenn die Spuren AUS sind: sonst zeigen die
    // Kamerazeilen darunter genau dasselbe, und die Zeile fehlte den Spuren.
    if (!g_app->showTracks || g_app->timeline.tracks.empty()) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float h = ImGui::GetFontSize() * 0.5F;
        // Der Anker kommt vom CURSOR, nicht mehr von der Unterkante des
        // Zeigers.
        //
        // Das ist der Fallstrick aus Abschnitt 4.1 des Handouts: etwas
        // messen, das man selbst verschoben hat. Bliebe hier barMax.y+2
        // stehen, zeichnete der Balken weiterhin an der alten Stelle -
        // ueber dem Lineal -, waehrend die Dummy-Zeile unten Platz
        // freihielte. Man saehe zwei Fehler auf einmal und keinen davon
        // erklaert.
        //
        // Die x-Kante dagegen MUSS von barMin kommen: sie ist dieselbe
        // Kante, an der Zeiger, Lineal und Spuren beginnen.
        ImVec2 top = ImGui::GetCursorScreenPos();
        top.x = barMin.x;
        top.y += 2.0F;
        dl->AddRectFilled(top, ImVec2{top.x + w, top.y + h},
                          IM_COL32(30, 33, 42, 255));
        for (const CamSegment& seg : t.segments) {
            if (t.durationMs <= 0.0) {
                break;
            }
            // Gegen das ZOOMFENSTER rechnen, nicht gegen die Gesamtdauer.
            //
            // Gemeldet: "es laeuft asynchron mit dem anderen darunter."
            //
            // Der Zeiger darueber laeuft von rStart bis rStart+rSpanne -
            // dem Ausschnitt, den man gerade sieht. Diese Balken rechneten
            // gegen t.durationMs, die GESAMTdauer. Zwei Massstaebe
            // uebereinander: solange der ganze Ablauf zu sehen ist, faellt
            // es kaum auf, sobald man hineinzoomt, laufen sie auseinander.
            //
            // Auch bei "Zoom: All" stimmten sie nicht genau, weil rSpanne
            // sich nach der Zeitleiste richtet und durationMs nach der
            // Kamerabahn - das sind nicht dieselben Zahlen.
            auto x0 = static_cast<float>((seg.startMs - rStart) / rSpanne) * w;
            auto x1 = static_cast<float>((seg.endMs - rStart) / rSpanne) * w;
            // Beschneiden, sobald hineingezoomt ist: ein Abschnitt, der vor
            // dem Ausschnitt beginnt, haette sonst einen negativen Anfang
            // und zeichnete nach links aus der Leiste heraus.
            if (x1 < 0.0F || x0 > w) {
                continue;
            }
            x0 = std::max(x0, 0.0F);
            x1 = std::min(x1, w);
            ImU32 col = IM_COL32(90, 110, 140, 255);
            switch (seg.kind) {
                case CamSegment::Kind::Move: col = IM_COL32(96, 170, 255, 255); break;
                case CamSegment::Kind::Pan:  col = IM_COL32(255, 190, 80, 255); break;
                case CamSegment::Kind::Zoom: col = IM_COL32(140, 220, 140, 255); break;
                default: break;
            }
            // Ein Sprung hat keine Dauer - trotzdem zwei Punkte breit
            // zeichnen, sonst ist er unsichtbar.
            dl->AddRectFilled(ImVec2{top.x + x0, top.y},
                              ImVec2{top.x + std::max(x1, x0 + 2.0F), top.y + h},
                              col);
        }
        // Der weisse Strich im Balken lief noch im ALTEN Massstab.
        //
        // rc241 hat die farbigen Abschnitte auf das Zoomfenster umgestellt
        // und diese eine Zeile stehenlassen - sie rechnete weiter
        // playMs/durationMs, also gegen die Gesamtdauer. Beim Hineinzoomen
        // stand der Strich damit woanders als die Abschnitte, durch die er
        // laufen soll. Derselbe Fehler, dieselbe Leiste, nur eine Zeile
        // weiter unten uebersehen.
        const auto px = static_cast<float>((g_app->playMs - rStart) / rSpanne) * w;
        if (px >= 0.0F && px <= w) {
            dl->AddLine(ImVec2{top.x + px, top.y - 2.0F},
                        ImVec2{top.x + px, top.y + h + 2.0F},
                        IM_COL32(255, 255, 255, 220), 1.5F);
        }
        ImGui::Dummy(ImVec2{w, h + 4.0F});
    }

    if (!g_app->showTracks || g_app->timeline.tracks.empty()) {
        return;
    }

    // Die Gesamtdauer der SPUREN, nicht die der Kamerabahn: eine Figur kann
    // nach dem letzten Kamerabefehl noch sprechen.
    const double span = rGesamt;
    // Zeilen so hoch wie eine Textzeile - vorher waren es 0,72 davon, und
    // die Namen standen gequetscht uebereinander.
    const float rowH = ImGui::GetTextLineHeightWithSpacing();
    // labelW steht schon oben - eine Kante fuer alles.
    const float trackW = std::max(w, 40.0F);

    // --- Der sichtbare Ausschnitt ----------------------------------------
    //
    // Alles unterhalb rechnet ab jetzt in DIESEM Ausschnitt, nicht mehr in
    // der Gesamtdauer. Die Vergroesserung wird geklemmt, damit man weder
    // hinter das Ende noch vor den Anfang rutscht - sonst sucht man die
    // Leiste und findet Leere.
    g_app->timelineZoom = std::clamp(g_app->timelineZoom, 1.0, App::kMaxZoom);
    const double sichtSpanne = span / g_app->timelineZoom;
    g_app->timelineViewStartMs =
        std::clamp(g_app->timelineViewStartMs, 0.0, std::max(0.0, span - sichtSpanne));
    const double sichtStart = g_app->timelineViewStartMs;
    // Zeit -> Bildpunkt. EINE Stelle, an der die Umrechnung steht; vorher
    // stand sie sechsmal ausgeschrieben da, und beim Zoom haetten sechs
    // Abschriften auseinanderlaufen muessen.
    const auto xAt = [&](double ms) {
        return barMin.x +
               static_cast<float>((ms - sichtStart) / sichtSpanne) * trackW;
    };
    g_app->tlFeldX = barMin.x;
    g_app->tlFeldW = trackW;
    g_app->tlSichtStart = sichtStart;
    g_app->tlSichtSpanne = sichtSpanne;
    g_app->tlZeilenY.clear();
    // Die Spuren fuellen, was uebrig ist. Wer die Leiste hochzieht, sieht
    // MEHR Spuren - genau das ist der Sinn der ziehbaren Kante. Vorher
    // waren es feste 4,6 Zeilen, egal wie viel Platz da war.
    const float restH = std::max(rowH, ImGui::GetContentRegionAvail().y - 2.0F);
    // Die Kamerazeilen zaehlen mit - sonst waere der Platz zu knapp
    // berechnet und die Spuren darunter waeren nicht mehr erreichbar.
    // Gezaehlt wird ueber die vorkommenden ARTEN, nicht ueber eine
    // angenommene Zahl: die Aufzaehlung hat neun Werte, gezeigt werden
    // sieben (Enable und Disable sind Zustaende, keine Bewegung).
    int camZeilenZahl = 0;
    for (const CamSegment::Kind art :
         {CamSegment::Kind::Move, CamSegment::Kind::Pan,
          CamSegment::Kind::Zoom, CamSegment::Kind::Roll,
          CamSegment::Kind::Fade, CamSegment::Kind::Shake,
          CamSegment::Kind::Follow}) {
        for (const CamSegment& seg : t.segments) {
            if (seg.kind == art) {
                ++camZeilenZahl;
                break;
            }
        }
    }
    int sichtbareSpuren = 0;
    for (const TimelineTrack& tr2 : g_app->timeline.tracks) {
        if (g_app->zeigeRuhigeSpuren || !spurIstRuhig(tr2)) { ++sichtbareSpuren; }
    }
    const float noetig =
        rowH * (static_cast<float>(sichtbareSpuren) +
                static_cast<float>(camZeilenZahl)) +
        4.0F;
    // Volle Breite UND immer eine Rollleiste: dann ist der Inhalt genau so
    // breit wie Zeiger und Lineal darueber, und die Kanten fallen zusammen.
    ImGui::BeginChild("tracks", ImVec2{w + labelW + rollBreite,
                                       std::min(restH, noetig)},
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImDrawList* tl = ImGui::GetWindowDrawList();

    // --- Die Kamera in ihre STUFEN aufgeteilt ----------------------------
    //
    // Vorher lag die ganze Kamerabahn in einem einzigen Streifen: Fahrt,
    // Schwenk und Zoom uebereinander in derselben Zeile, und wo zwei
    // gleichzeitig laufen, sah man nur den letzten. Gemeldet als "die
    // Camera ist ein grosser Balken - wenn es mehrere kleine Abschnitte
    // hat, sollten die besser aufgeteilt sein".
    //
    // Jetzt bekommt jeder Unterbefehl seine eigene Zeile - wie die Spuren
    // eines Schnittprogramms. Eine Zeile bleibt leer, wenn ihr Unterbefehl
    // im Skript nicht vorkommt; das haelt die Leiste kurz.
    struct CamZeile {
        CamSegment::Kind kind;
        const char* name;
        ImU32 farbe;
    };
    static const CamZeile camZeilen[] = {
        {CamSegment::Kind::Move, "MOVE", IM_COL32(96, 170, 255, 255)},
        {CamSegment::Kind::Pan, "PAN", IM_COL32(255, 190, 80, 255)},
        {CamSegment::Kind::Zoom, "ZOOM", IM_COL32(140, 220, 140, 255)},
        {CamSegment::Kind::Roll, "ROLL", IM_COL32(200, 140, 255, 255)},
        {CamSegment::Kind::Fade, "FADE", IM_COL32(230, 230, 230, 255)},
        {CamSegment::Kind::Shake, "SHAKE", IM_COL32(255, 120, 120, 255)},
        {CamSegment::Kind::Follow, "FOLLOW", IM_COL32(120, 220, 220, 255)},
    };
    g_app->tlUeberlappungen = 0;
    struct RoteStelle {
        float x0;
        float x1;
        ImVec2 a;   // der ganze Block, fuer den Rahmen
        ImVec2 b;
    };
    for (const CamZeile& cz : camZeilen) {
        std::vector<RoteStelle> roteStellen;
        bool hat = false;
        for (const CamSegment& seg : t.segments) {
            if (seg.kind == cz.kind) { hat = true; break; }
        }
        if (!hat) {
            continue;
        }
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        char nm[24];
        std::snprintf(nm, sizeof(nm), "camera %s", cz.name);
        g_app->tlZeilenY.emplace_back(nm, cp.y + rowH * 0.5F);
        // Der NAME ist anklickbar: er waehlt die Kamera an, und damit
        // erscheint ihre Bahn in der Ansicht. Angewaehlt steht er hell.
        const bool camAn = g_app->cameraSelected;
        tl->AddText(cp,
                    camAn ? IM_COL32(230, 235, 245, 255)
                          : IM_COL32(150, 160, 180, 255),
                    nm);
        if (ImGui::IsMouseHoveringRect(
                cp, ImVec2{barMin.x - 2.0F, cp.y + rowH - 2.0F}) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            g_app->cameraSelected = true;
            g_app->selectedActor = -1;
            g_app->mapDirty = true;
        }
        tl->AddRectFilled(ImVec2{barMin.x, cp.y + 1.0F},
                          ImVec2{barMin.x + trackW, cp.y + rowH - 2.0F},
                          IM_COL32(28, 31, 40, 255));
        // Ab hier NUR noch im Zeitfeld zeichnen.
        //
        // Beim Zoomen liegen Bloecke ausserhalb des Ausschnitts - links
        // davon und rechts davon. Ohne Beschneiden malen sie ueber die
        // Namensspalte und ueber den rechten Rand hinaus; im Bild stand
        // "camer" statt "camera PAN", weil ein Balken darueberlag.
        tl->PushClipRect(ImVec2{barMin.x, cp.y},
                         ImVec2{barMin.x + trackW, cp.y + rowH}, true);
        for (const CamSegment& seg : t.segments) {
            if (seg.kind != cz.kind) {
                continue;
            }
            const float sx0 = xAt(seg.startMs);
            // Wird gerade an DIESEM Block gezogen? Dann folgt seine rechte
            // Kante der Maus statt dem Wert aus dem Skript.
            const bool zieht =
                g_app->tlDragging && g_app->tlDragPath == seg.path;
            const float sx1 = xAt(zieht ? g_app->tlDragEndMs : seg.endMs);
            const ImVec2 a{sx0, cp.y + 1.0F};
            const ImVec2 b{std::max(sx1, sx0 + 3.0F), cp.y + rowH - 2.0F};
            tl->AddRectFilled(a, b, cz.farbe);
            // --- Laeuft er in den naechsten hinein? ----------------------
            //
            // Die Engine uebernimmt Ort/Winkel/Bildwinkel erst, wenn ein
            // Uebergang FERTIG ist; ein neuer, der vorher beginnt, rechnet
            // vom Anfang des alten - im Spiel springt die Kamera zurueck.
            // Rot schraffiert, was sich ueberlappt, auch schon beim Ziehen.
            const double ende = zieht ? g_app->tlDragEndMs : seg.endMs;
            const CamSegment* naechster = nullptr;
            const auto gleicheGruppe = [&](const CamSegment& o) {
                const bool schwenkA = seg.kind == CamSegment::Kind::Pan ||
                                      (seg.kind == CamSegment::Kind::Roll && seg.alleAchsen);
                const bool schwenkB = o.kind == CamSegment::Kind::Pan ||
                                      (o.kind == CamSegment::Kind::Roll && o.alleAchsen);
                return schwenkA ? schwenkB : (o.kind == seg.kind);
            };
            if (seg.kind == CamSegment::Kind::Move || seg.kind == CamSegment::Kind::Pan ||
                seg.kind == CamSegment::Kind::Zoom || seg.kind == CamSegment::Kind::Roll) {
                for (const CamSegment& o : t.segments) {
                    if (&o != &seg && gleicheGruppe(o) && o.startMs > seg.startMs + 0.5 &&
                        (naechster == nullptr || o.startMs < naechster->startMs)) {
                        naechster = &o;
                    }
                }
            }
            const bool ueberlappt = naechster != nullptr && naechster->startMs < ende - 0.5;
            if (ueberlappt) {
                // Erst NACH allen Bloecken der Zeile zeichnen - sonst liegt der
                // naechste Block darueber, und die Schraffur ist verdeckt.
                ++g_app->tlUeberlappungen;
                roteStellen.push_back({std::max(a.x, xAt(naechster->startMs)), b.x, a, b});
            }
            // --- Beschriftung: wohin, welcher Winkel, welche Marke --------
            if (b.x - a.x > ImGui::GetFontSize() * 2.0F) {
                if (const Node* cn = nodeAt(g_app->doc.script(), seg.path);
                    cn != nullptr && cn->args.size() >= 2) {
                    std::string was = cn->args[1].text;
                    // tag( "cam1b", ORIGIN ) -> cam1b
                    const std::size_t q1 = was.find('"');
                    const std::size_t q2 = (q1 == std::string::npos) ? q1 : was.find('"', q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos && was.find("tag") != std::string::npos) {
                        was = was.substr(q1 + 1, q2 - q1 - 1);
                    } else {
                        // Zahlen kuerzen: "-1762.000 -146.000 1076.000" -> "-1762 -146 1076"
                        std::string kurz;
                        std::istringstream zs(was);
                        std::string teil;
                        while (zs >> teil) {
                            char* endp = nullptr;
                            const double v = std::strtod(teil.c_str(), &endp);
                            if (endp != nullptr && *endp == '\0') {
                                char b2[24];
                                std::snprintf(b2, sizeof(b2), "%g", std::round(v * 10.0) / 10.0);
                                teil = b2;
                            }
                            if (teil != "<" && teil != ">") {
                                if (!kurz.empty()) { kurz += " "; }
                                kurz += teil;
                            }
                        }
                        was = kurz;
                    }
                    tl->PushClipRect(a, b, true);
                    tl->AddText(ImVec2{a.x + 3.0F, a.y}, IM_COL32(16, 18, 24, 230), was.c_str());
                    tl->PopClipRect();
                }
            }
            kantenGriff(seg.path, seg.startMs,
                        zieht ? g_app->tlDragEndMs : seg.endMs, a, b,
                        sichtSpanne / std::max(trackW, 1.0F));
            tl->AddLine(ImVec2{a.x, a.y}, ImVec2{a.x, b.y},
                        IM_COL32(20, 22, 28, 255), 1.0F);
            if (ImGui::IsMouseHoveringRect(a, ImVec2{b.x + 2.0F, b.y})) {
                ImGui::BeginTooltip();
                ImGui::Text("camera %s", cz.name);
                if (ueberlappt) {
                    ImGui::TextColored(ImVec4{1.0F, 0.45F, 0.45F, 1.0F}, tr(Str::TlOverlap), cz.name,
                                       naechster->startMs / 1000.0);
                }
                if (g_app->timelineFrames) {
                    const double fps =
                        static_cast<double>(std::max(1, g_app->timelineFps));
                    ImGui::TextDisabled(
                        tr(Str::TlSpanFrames),
                        static_cast<int>(seg.startMs / 1000.0 * fps),
                        static_cast<int>(seg.endMs / 1000.0 * fps));
                } else {
                    ImGui::TextDisabled(tr(Str::TlSpanSeconds),
                                        seg.startMs / 1000.0,
                                        seg.endMs / 1000.0);
                }
                ImGui::EndTooltip();
                if (!g_app->tlDragging &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    // Zum Zeitpunkt springen UND die Kamera anwaehlen -
                    // sonst sieht man zwar die Stellung, aber nicht die
                    // Bahn, auf der sie dorthin kommt. Ausgefuehrt am Ende
                    // der Spuren (tlKlickKandidat).
                    App::TlKandidat& k = g_app->tlKlickKandidat;
                    k.da = true;
                    k.pfad = seg.path;
                    k.startMs = seg.startMs;
                    k.kamera = true;
                    k.figur = -1;
                }
            }
        }
        // Die Ueberlappungen OBENAUF: rot schraffiert, der Block rot umrandet.
        for (const RoteStelle& rs : roteStellen) {
            const ImU32 rot = IM_COL32(235, 60, 60, 255);
            const float h = rs.b.y - rs.a.y;
            tl->PushClipRect(ImVec2{rs.x0, rs.a.y}, ImVec2{rs.x1, rs.b.y}, true);
            tl->AddRectFilled(ImVec2{rs.x0, rs.a.y}, ImVec2{rs.x1, rs.b.y}, IM_COL32(235, 60, 60, 150));
            for (float hx = rs.x0 - h; hx < rs.x1; hx += 6.0F) {
                tl->AddLine(ImVec2{hx, rs.b.y}, ImVec2{hx + h, rs.a.y}, rot, 1.5F);
            }
            tl->PopClipRect();
            tl->AddRect(rs.a, rs.b, rot, 0.0F, 0, 1.5F);
        }
        const float clx = xAt(g_app->playMs);
        tl->AddLine(ImVec2{clx, cp.y}, ImVec2{clx, cp.y + rowH},
                    IM_COL32(255, 255, 255, 160), 1.0F);
        tl->PopClipRect();
        ImGui::Dummy(ImVec2{w, rowH});
    }

    for (const TimelineTrack& track : g_app->timeline.tracks) {
        if (!g_app->zeigeRuhigeSpuren && spurIstRuhig(track)) {
            continue;
        }
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const std::string name = track.entity.empty() ? std::string(tr(Str::TlScript))
                                                      : track.entity;
        g_app->tlZeilenY.emplace_back(name, p0.y + rowH * 0.5F);
        // Der Name waehlt die FIGUR an - dann zeigt die Ansicht ihren
        // Laufweg, so wie sie es fuer die Kamera schon tut.
        int figIdx = -1;
        if (!track.entity.empty()) {
            for (std::size_t ai = 0; ai < g_app->scene.actors.size(); ++ai) {
                if (g_app->scene.actors[ai].name == track.entity) {
                    figIdx = static_cast<int>(ai);
                    break;
                }
            }
        }
        const bool figAn = (figIdx >= 0 && figIdx == g_app->selectedActor);
        tl->AddText(p0,
                    figAn ? IM_COL32(230, 235, 245, 255)
                          : IM_COL32(150, 160, 180, 255),
                    name.c_str());
        if (figIdx >= 0 &&
            ImGui::IsMouseHoveringRect(
                p0, ImVec2{barMin.x - 2.0F, p0.y + rowH - 2.0F})) {
            ImGui::SetTooltip("%s", tr(Str::TlActorHint));
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                // Nochmal derselbe Name hebt die Auswahl auf - dieselbe
                // Regel wie im Skriptbaum seit rc167.
                g_app->selectedActor = figAn ? -1 : figIdx;
                g_app->cameraSelected = false;
                g_app->mapDirty = true;
            }
        }

        // Die Balken beginnen an DERSELBEN Kante wie Zeiger und Lineal -
        // und zwar an der gemessenen, nicht an einer nachgerechneten.
        //
        // "p0.x + labelW" waere fast richtig: p0.x ist die Innenkante des
        // Kindfensters, also um dessen Innenabstand nach rechts versetzt.
        // Ein paar Bildpunkte Versatz je Zeile - genug, dass die Balken
        // sichtbar gegen die Zahlen darueber laufen. barMin.x ist der Ort,
        // an dem der Zeiger wirklich anfaengt; Zeichenlisten rechnen ohnehin
        // in Bildschirmkoordinaten.
        const float x0 = barMin.x;
        tl->AddRectFilled(ImVec2{x0, p0.y + 1.0F},
                          ImVec2{x0 + trackW, p0.y + rowH - 2.0F},
                          IM_COL32(28, 31, 40, 255));
        // Dasselbe wie bei den Kamerazeilen: ab hier nur noch im Zeitfeld.
        tl->PushClipRect(ImVec2{x0, p0.y}, ImVec2{x0 + trackW, p0.y + rowH},
                         true);

        for (const TimelineEvent& e : track.events) {
            // Die Kamerabefehle des Skripts haben ihre eigenen Zeilen oben -
            // in der Script-Zeile lagen sie noch einmal, UNTER den waits.
            if (track.entity.empty() && e.kind == TimelineEvent::Kind::Camera) {
                continue;
            }
            // Nur zeigen, was beim Abstimmen zaehlt. Ein SET_PARM ist
            // richtig, aber uninteressant; ein Klang und eine Bewegung
            // nicht.
            // Jede Art ihre Farbe - auch Warten und Setzen. Vorher fielen
            // die unter "default: continue", und damit sah eine Spur wie
            // ein einziger langer Balken aus: man sah, DASS etwas laeuft,
            // aber nicht WAS und nicht, wo eines aufhoert und das naechste
            // anfaengt.
            ImU32 col = 0;
            switch (e.kind) {
                case TimelineEvent::Kind::Sound:
                    col = IM_COL32(255, 120, 200, 255);
                    break;
                // Ein use, das einen Klang ausloest, bekommt dieselbe
                // Farbe wie ein sound - fuer den Anwender ist es dasselbe:
                // hier faengt etwas an zu klingen. Ein use ohne Klang
                // (Tuer, Ausloeser, Speicherpunkt) bleibt gedeckt.
                case TimelineEvent::Kind::Use:
                    col = useSoundFor(e.target, nullptr).empty()
                              ? IM_COL32(120, 130, 150, 255)
                              : IM_COL32(255, 120, 200, 255);
                    break;
                // Die Klinge klingt immer - notfalls mit der Vorgabe.
                case TimelineEvent::Kind::Saber:
                    col = IM_COL32(255, 120, 200, 255);
                    break;
                case TimelineEvent::Kind::Camera:
                    col = IM_COL32(96, 170, 255, 255);
                    break;
                case TimelineEvent::Kind::Move:
                    col = IM_COL32(140, 220, 140, 255);
                    break;
                case TimelineEvent::Kind::Wait:
                    // Warten gedeckt und dunkel: es ist eine Luecke, keine
                    // Handlung - aber eine, die man SEHEN will, weil sie
                    // den Takt bestimmt.
                    col = IM_COL32(70, 74, 90, 255);
                    break;
                case TimelineEvent::Kind::Set:
                    col = IM_COL32(190, 160, 90, 255);
                    break;
                default:
                    col = IM_COL32(110, 116, 132, 255);
                    break;
            }
            // Aus einem ANDEREN Skript (run, use, spawnscript): blasser -
            // man sieht, dass es passiert, aber es ist nicht diese Datei.
            if (!e.fremd.empty()) {
                col = (col & 0x00FFFFFFU) | (0x70U << IM_COL32_A_SHIFT);
            }
            const float ex0 = xAt(e.startMs);
            const bool zieht = g_app->tlDragging && g_app->tlDragPath == e.path;
            const float ex1 = xAt(zieht ? g_app->tlDragEndMs : e.endMs);
            // Mindestens drei Bildpunkte breit, damit ein Befehl ohne Dauer
            // (ein Klang, ein set) ueberhaupt zu treffen ist.
            const ImVec2 a{ex0, p0.y + 1.0F};
            const ImVec2 b{std::max(ex1, ex0 + 3.0F), p0.y + rowH - 2.0F};
            tl->AddRectFilled(a, b, col);
            kantenGriff(e.path, e.startMs,
                        zieht ? g_app->tlDragEndMs : e.endMs, a, b,
                        sichtSpanne / std::max(trackW, 1.0F));
            // Eine dunkle Kante an den Anfang: damit zwei Bloecke, die
            // aneinanderstossen, als ZWEI zu erkennen sind. Ohne sie
            // verschmelzen sie zu einem Balken.
            tl->AddLine(ImVec2{a.x, a.y}, ImVec2{a.x, b.y},
                        IM_COL32(20, 22, 28, 255), 1.0F);

            // Die Beschriftung hinein, wenn Platz ist. Das ist der
            // Unterschied zwischen "da laeuft etwas" und "da laeuft PAN".
            if (b.x - a.x > ImGui::GetFontSize() * 2.0F && !e.label.empty()) {
                tl->PushClipRect(a, b, true);
                tl->AddText(ImVec2{a.x + 3.0F, a.y}, IM_COL32(16, 18, 24, 230),
                            e.label.c_str());
                tl->PopClipRect();
            }

            // Ueberfahren zeigt Anfang UND Ende - in der Einheit, die
            // gerade eingestellt ist.
            if (ImGui::IsMouseHoveringRect(a, ImVec2{b.x + 2.0F, b.y})) {
                ImGui::BeginTooltip();
                if (g_app->timelineFrames) {
                    const double fps =
                        static_cast<double>(std::max(1, g_app->timelineFps));
                    ImGui::Text("%s", e.label.c_str());
                    ImGui::TextDisabled(
                        tr(Str::TlSpanFrames),
                        static_cast<int>(e.startMs / 1000.0 * fps),
                        static_cast<int>(e.endMs / 1000.0 * fps));
                } else {
                    ImGui::Text("%s", e.label.c_str());
                    ImGui::TextDisabled(tr(Str::TlSpanSeconds),
                                        e.startMs / 1000.0, e.endMs / 1000.0);
                }
                if (!e.sound.empty()) {
                    ImGui::TextDisabled("%s", e.sound.c_str());
                }
                if (!e.fremd.empty()) {
                    ImGui::TextColored(ImVec4{1.0F, 0.8F, 0.4F, 1.0F}, tr(Str::TlFromScript), e.fremd.c_str());
                }
                ImGui::TextDisabled("%s", tr(Str::TlClickHint));
                ImGui::EndTooltip();
                // Anklicken springt zur Zeile im Skript UND setzt den
                // Zeiger dorthin. Damit wird aus der Anzeige ein Werkzeug:
                // man sucht in der Leiste und landet im Text.
                // NICHT springen, wenn gerade an einer Kante gezogen wird.
                // Der Griff liegt im Block, und ein Klick darauf soll die
                // Dauer aendern - nicht zugleich die Ansicht umstellen.
                if (!g_app->tlDragging &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    App::TlKandidat& k = g_app->tlKlickKandidat;
                    k.da = true;
                    k.pfad = e.path;
                    k.startMs = e.startMs;
                    k.kamera = false;
                    k.figur = figIdx;
                }
            }
        }
        // Das Ende des Ablaufs, schwach.
        tl->AddLine(ImVec2{xAt(ablaufEnde), p0.y}, ImVec2{xAt(ablaufEnde), p0.y + rowH},
                    IM_COL32(230, 80, 80, 110), 1.0F);
        // Der Zeiger durch alle Spuren.
        const float lx = xAt(g_app->playMs);
        tl->AddLine(ImVec2{lx, p0.y}, ImVec2{lx, p0.y + rowH},
                    IM_COL32(255, 255, 255, 160), 1.0F);
        tl->PopClipRect();
        ImGui::Dummy(ImVec2{w, rowH});
    }

    // --- Klick und Kantengriff: der OBERSTE Block gewinnt -----------------
    //
    // Beide wurden beim Zeichnen nur vorgemerkt; der zuletzt gezeichnete
    // Block liegt oben. Eine gepackte Kante geht vor dem Klick auf den Block.
    if (g_app->tlGriffKandidat.da) {
        const App::TlKandidat k = g_app->tlGriffKandidat;
        g_app->tlDragging = true;
        g_app->tlDragPath = k.pfad;
        g_app->tlDragStartMs = k.startMs;
        g_app->tlDragEndMs = k.endMs;
        const Node* gn = nodeAt(g_app->doc.script(), k.pfad);
        diag::detail("Zeitleiste: Kante von \"" + std::string(gn != nullptr ? gn->name : "?") + "\" gepackt (" +
                     std::to_string(k.startMs) + " - " + std::to_string(k.endMs) + " ms)");
    } else if (g_app->tlKlickKandidat.da) {
        const App::TlKandidat k = g_app->tlKlickKandidat;
        // Anklicken springt zur Zeile im Skript UND setzt den Zeiger dorthin -
        // auch bei einem Kamerablock (vorher nur bei den Spuren).
        if (!k.pfad.empty()) {
            selectByPath(k.pfad);
        }
        if (k.kamera) {
            g_app->cameraSelected = true;
            g_app->selectedActor = -1;
        } else if (k.figur >= 0) {
            g_app->selectedActor = k.figur;
            g_app->cameraSelected = false;
        }
        g_app->playMs = k.startMs;
        g_app->playing = false;
        g_app->audioUpTo = k.startMs;
        g_app->audio.stopAll();
        g_app->laufendeSchleifen.clear();
        g_app->musicAudio.stopAll();
        g_app->mapDirty = true;
        // Vielleicht wird daraus ein Verschieben: merken, wo es anfing.
        if (!k.pfad.empty()) {
            g_app->tlZiehPfad = k.pfad;
            g_app->tlZiehMausX = ImGui::GetIO().MousePos.x;
            g_app->tlZiehStartMs = k.startMs;
            g_app->tlZiehMsJePx = sichtSpanne / std::max(1.0, static_cast<double>(trackW));
            g_app->tlZiehAktiv = false;
            g_app->tlZiehDelta = 0.0;
        }
    }
    g_app->tlGriffKandidat = App::TlKandidat{};
    g_app->tlKlickKandidat = App::TlKandidat{};

    // --- Einen Block verschieben -------------------------------------------
    //
    // shank: "hast du eingebaut, dass man die Clips unten nehmen und
    // verschieben kann?" Jetzt ja. Ein ICARUS-Skript ist eine Folge: ein
    // Befehl beginnt, wenn das wait davor abgelaufen ist. Einen Block
    // verschieben heisst also, dieses wait laenger oder kuerzer zu machen -
    // alles danach rueckt mit (Ripple, wie im Sequencer). Mit Alt bleibt der
    // Rest stehen: das wait danach wird um dasselbe kuerzer. Im Raster der
    // Spiellogik (50 ms).
    if (!g_app->tlZiehPfad.empty()) {
        const float dx = ImGui::GetIO().MousePos.x - g_app->tlZiehMausX;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!g_app->tlZiehAktiv && std::fabs(dx) > 4.0F && !g_app->tlDragging) {
                g_app->tlZiehAktiv = true;
            }
            if (g_app->tlZiehAktiv) {
                g_app->tlZiehDelta = std::round(static_cast<double>(dx) * g_app->tlZiehMsJePx / 50.0) * 50.0;
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                const float gx = xAt(g_app->tlZiehStartMs + g_app->tlZiehDelta);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 fo = ImGui::GetWindowPos();
                const ImVec2 fg = ImGui::GetWindowSize();
                dl->AddLine(ImVec2{gx, fo.y}, ImVec2{gx, fo.y + fg.y}, IM_COL32(255, 210, 90, 230), 2.0F);
                ImGui::BeginTooltip();
                ImGui::Text(tr(Str::TlMoveDelta), g_app->tlZiehDelta / 1000.0);
                ImGui::TextDisabled("%s", tr(ImGui::GetIO().KeyAlt ? Str::TlMoveSlide : Str::TlMoveRipple));
                ImGui::EndTooltip();
            }
        } else {
            if (g_app->tlZiehAktiv && std::fabs(g_app->tlZiehDelta) >= 1.0) {
                verschiebeImAblauf(g_app->tlZiehPfad, g_app->tlZiehDelta, ImGui::GetIO().KeyAlt);
            }
            g_app->tlZiehPfad.clear();
            g_app->tlZiehAktiv = false;
            g_app->tlZiehDelta = 0.0;
        }
    }

    // --- Bildschritt per Tastatur (wie in jedem Schnittprogramm) ----------
    //
    // Links/rechts ein Spielbild (50 ms), mit Umschalt eine Sekunde, Pos1
    // und Ende an Anfang und Ende. Nur, solange die Maus ueber der Leiste
    // steht - sonst gehoeren die Pfeile dem Baum (handleTreeKeys).
    g_app->tlUeberMaus = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    if (g_app->tlUeberMaus && !ImGui::GetIO().WantTextInput) {
        const double schritt = ImGui::GetIO().KeyShift ? 1000.0 : 50.0;
        double ziel = g_app->playMs;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) { ziel += schritt; }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) { ziel -= schritt; }
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) { ziel = 0.0; }
        if (ImGui::IsKeyPressed(ImGuiKey_End, false)) { ziel = t.durationMs; }
        ziel = std::clamp(ziel, 0.0, std::max(0.0, t.durationMs));
        if (ziel != g_app->playMs) {
            g_app->playMs = ziel;
            g_app->playing = false;
            g_app->audioUpTo = ziel;
            g_app->audio.stopAll();
            g_app->laufendeSchleifen.clear();
            g_app->musicAudio.stopAll();
            g_app->mapDirty = true;
        }
    }

    // --- Zoomen auch UEBER DEN SPUREN ------------------------------------
    //
    // Dort verbringt man die Zeit, nicht auf dem Lineal. Mit STRG, damit
    // das Rad allein weiterhin senkrecht rollt - bei vielen Spuren braucht
    // man das.
    //
    // Dieselbe Rechnung wie am Lineal: vergroessert wird um den Punkt unter
    // der Maus.
    if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl) {
        const float rad2 = ImGui::GetIO().MouseWheel;
        if (rad2 != 0.0F) {
            const float mausX = ImGui::GetIO().MousePos.x;
            const double anteil = std::clamp<double>(
                (mausX - barMin.x) / std::max(trackW, 1.0F), 0.0, 1.0);
            const double unterMaus = sichtStart + anteil * sichtSpanne;
            const double neu = std::clamp(
                g_app->timelineZoom * std::pow(1.25, rad2), 1.0, App::kMaxZoom);
            const double neueSpanne = span / neu;
            g_app->timelineZoom = neu;
            g_app->timelineViewStartMs =
                std::clamp(unterMaus - anteil * neueSpanne, 0.0,
                           std::max(0.0, span - neueSpanne));
        }
    }
    ImGui::EndChild();
}

// Die Seitenleiste, RECHTS neben der Ansicht.
//
// EIN Helfer fuer beide Faelle - mit und ohne Karte. Vorher gab es zwei
// Fassungen: die richtige am Ende von drawMapView, und eine Notloesung im
// Zweig "keine Karte geladen", die die Einstellungen quer ueber die ganze
// Flaeche legte. Gemeldet als "die version wo die map nicht geladen wird
// ist komplett falsch".
//
// Zwei Fassungen desselben Dings laufen immer auseinander. Jetzt gibt es
// nur noch eine, und der leere Fall sieht aus wie der volle.
void drawMapSidebarColumn(float breite, float hoehe) {
    ImGui::BeginGroup();
    // Der Knopf steht IMMER da, auch eingeklappt - sonst kaeme man nicht
    // mehr an die Einstellungen heran.
    if (ImGui::Button(g_app->mapSidebar ? ">" : "<", ImVec2{breite, 0.0F})) {
        g_app->mapSidebar = !g_app->mapSidebar;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::MapSidebarHint));
    }
    if (g_app->mapSidebar) {
        const float restH =
            std::max(60.0F, hoehe - ImGui::GetFrameHeightWithSpacing());
        if (ImGui::BeginChild("mapopts", ImVec2{breite, restH},
                              ImGuiChildFlags_Borders)) {
            drawMapSidebar();
        }
        ImGui::EndChild();
    }
    ImGui::EndGroup();
}

// Wie breit ist die Leiste? Dieselbe Rechnung fuer beide Faelle.
float mapSidebarWidth(float verfuegbar) {
    const float knopfW = ImGui::GetFrameHeight();
    return g_app->mapSidebar
               ? std::clamp(ImGui::GetFontSize() * 13.0F, 120.0F,
                            std::max(160.0F, verfuegbar * 0.45F))
               : knopfW;
}

// --- Der bearbeitete Schluessel geht mit der Zeitleiste ---------------------
//
// Ist die Kamera angewaehlt und Verschieben oder Drehen an, gilt immer der
// Schluessel der eingestellten ZEIT (der letzte MOVE, der begonnen hat).
// Zieht man die Zeitleiste, wandert er mit - ausser es steht eine noch
// nicht geschriebene Aenderung an; die bleibt an ihrem Schluessel, bis sie
// geschrieben oder verworfen ist. Waehrend eines Zuges am Gizmo nie.
void schluesselMitZeit() {
    const std::size_t anzahl = kameraSchluessel(g_app->doc.script()).size();
    if (g_app->selectedKey >= 0 && static_cast<std::size_t>(g_app->selectedKey) >= anzahl) {
        // Die Zeile gibt es nicht mehr (geloescht, Rueckgaengig).
        g_app->selectedKey = anzahl == 0 ? -1 : static_cast<int>(anzahl) - 1;
        for (int k = 0; k < 3; ++k) {
            g_app->gizmoOffset[k] = 0.0F;
            g_app->gizmoAngles[k] = 0.0F;
        }
    }
    if (!g_app->cameraSelected || g_app->gizmoMode == App::GizmoMode::Select ||
        g_app->gizmoAxis >= 0 || g_app->gizmoRotating ||
        std::fabs(g_app->playMs - g_app->gizmoZeitBezug) <= 0.5) {
        return;
    }
    g_app->gizmoZeitBezug = g_app->playMs;
    const bool offen = g_app->gizmoOffset[0] != 0.0F || g_app->gizmoOffset[1] != 0.0F ||
                       g_app->gizmoOffset[2] != 0.0F || g_app->gizmoAngles[0] != 0.0F ||
                       g_app->gizmoAngles[1] != 0.0F || g_app->gizmoAngles[2] != 0.0F;
    if (offen) {
        return;
    }
    const int k = schluesselFuerZeit(g_app->playMs);
    if (k != g_app->selectedKey) {
        g_app->selectedKey = k;
        g_app->mapDirty = true;
    }
}

void drawMapView() {
    schluesselMitZeit();
    // OHNE Karte kein frueher Ausstieg mehr.
    //
    // Vorher stand hier ein return - und die Seitenleiste, die seit rc173
    // am ENDE dieser Funktion gezeichnet wird, kam damit nie an die Reihe.
    // Gemeldet als "wo ist die timeline und die map optionen?": ohne
    // geladene Karte war die ganze rechte Spalte weg, samt dem Knopf, mit
    // dem man sie zurueckholt.
    //
    // Jetzt steht statt des Bildes nur der Hinweis, und die Leiste kommt
    // trotzdem - abgeblendet, wie alles andere ohne Karte.
    // Beim Moduswechsel EIN Bild lang nichts zeichnen (rc515).
    //
    // Ob dieses Bild das Einschwingbild ist, entscheidet nicht mehr diese
    // Funktion, sondern `moduswechselBild` - gesetzt dort, wo ALLE Modi
    // vorbeikommen. Vorher stand die Merkstelle hier drin, und diese
    // Funktion laeuft nur im Karten-Modus: Karte -> Events -> Karte griff
    // der Uebersprung beim zweiten Mal nicht mehr.
    if (g_app->moduswechselBild) {
        ImGui::Dummy(ImGui::GetContentRegionAvail());
        return;
    }

    if (g_app->geo.empty()) {
        // Dieselbe Anordnung wie mit Karte: links das Bildfeld, rechts die
        // Leiste. Statt des Bildes steht ein Platzhalter derselben Groesse
        // - so springt beim Laden einer Karte nichts, und der Anwender
        // sieht schon vorher, wo was liegen wird.
        const ImVec2 leer = ImGui::GetContentRegionAvail();
        const float leisteW0 = mapSidebarWidth(leer.x);
        g_app->mapSidebarW = leisteW0;
        const float bildW = std::max(
            64.0F, leer.x - leisteW0 - ImGui::GetStyle().ItemSpacing.x);
        g_app->dbgBildW = bildW;
        const float bildH = std::max(64.0F, leer.y - 2.0F);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImDrawList* dl0 = ImGui::GetWindowDrawList();
        dl0->AddRectFilled(p0, ImVec2{p0.x + bildW, p0.y + bildH},
                           IM_COL32(18, 20, 26, 255));
        dl0->AddRect(p0, ImVec2{p0.x + bildW, p0.y + bildH},
                     ImGui::GetColorU32(ImGuiCol_Border));
        // Der Hinweis in die MITTE des Feldes - dort sucht man ihn.
        const ImVec2 ts = ImGui::CalcTextSize(tr(Str::MapEmpty));
        dl0->AddText(ImVec2{p0.x + (bildW - ts.x) * 0.5F,
                            p0.y + (bildH - ts.y) * 0.5F},
                     ImGui::GetColorU32(ImGuiCol_TextDisabled),
                     tr(Str::MapEmpty));
        ImGui::Dummy(ImVec2{bildW, bildH});
        ImGui::SameLine();
        drawMapSidebarColumn(leisteW0, bildH);
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    // Die Seitenleiste nimmt sich ihren Platz VOM BILD, nicht von der
    // Hoehe. Eingeklappt bleibt nur ein schmaler Knopf stehen - so wie in
    // Blender, wo die Leiste ganz verschwindet und ein kleiner Pfeil sie
    // zurueckholt.
    const float leisteW = mapSidebarWidth(avail.x);
    g_app->mapSidebarW = leisteW;
    const int w = std::max(64, static_cast<int>(avail.x - leisteW -
                                                ImGui::GetStyle().ItemSpacing.x));
    // Nur noch EINE Zeile Bedienung unter dem Bild: Ort und Kameraknopf.
    // Alles andere steht rechts oder in der Zeitleiste.
    const int h = std::max(64, static_cast<int>(avail.y) -
                               static_cast<int>(ImGui::GetFrameHeightWithSpacing() * 1.4F));

    // --- Umsehen und Fliegen ---------------------------------------------
    //
    // Rechte Maustaste zum Umsehen, wie in jedem Editor. Die linke bleibt
    // frei fuers Anklicken.
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                         !g_app->throughCamera;
    ImGuiIO& io = ImGui::GetIO();

    // Bedienung wie im Ansichtsfenster von 3ds Max:
    //   mittlere Taste        schieben
    //   Alt + mittlere Taste  umkreisen
    //   Rad                   zoomen
    //   rechte Taste + WASD   fliegen
    //
    // Zum SETZEN einer Kamera will man ein Ziel umkreisen und sehen, wie es
    // aus verschiedenen Richtungen aussieht. Fliegen ist gut, um irgendwo
    // hinzukommen, aber umstaendlich, um sich um etwas herumzubewegen.
    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        if (io.KeyAlt) {
            g_app->orbit.turn(g_app->cam, -io.MouseDelta.x * 0.3F,
                              -io.MouseDelta.y * 0.3F);
        } else {
            g_app->orbit.pan(g_app->cam, io.MouseDelta.x, io.MouseDelta.y);
        }
    }
    if (hovered && io.MouseWheel != 0.0F) {
        g_app->orbit.zoom(g_app->cam, io.MouseWheel);
    }
    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        g_app->cam.angles[1] -= io.MouseDelta.x * 0.25F;
        g_app->cam.angles[0] = std::clamp(
            g_app->cam.angles[0] - io.MouseDelta.y * 0.25F, -89.0F, 89.0F);
    }
    if (hovered) {
        const float base = io.KeyShift ? 40.0F : 12.0F;
        const float step = base * std::max(io.DeltaTime, 0.001F) * 60.0F;
        float fwd = 0.0F;
        float side = 0.0F;
        float vert = 0.0F;
        if (ImGui::IsKeyDown(ImGuiKey_W)) { fwd += step; }
        if (ImGui::IsKeyDown(ImGuiKey_S)) { fwd -= step; }
        if (ImGui::IsKeyDown(ImGuiKey_D)) { side += step; }
        if (ImGui::IsKeyDown(ImGuiKey_A)) { side -= step; }
        if (ImGui::IsKeyDown(ImGuiKey_E)) { vert += step; }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) { vert -= step; }
        if (fwd != 0.0F || side != 0.0F || vert != 0.0F) {
            g_app->cam.move(fwd, side, vert);
        }
    }

    // --- Zeichnen ---------------------------------------------------------
    // Die Groesse des Renderziels setzt der Block weiter unten; hier wird
    // nur bemerkt, dass sich das FENSTER geaendert hat.
    if (g_app->mapViewW != w || g_app->mapViewH != h) {
        g_app->mapViewW = w;
        g_app->mapViewH = h;
        g_app->mapDirty = true;
    }
    // Durch die gewaehlte Kamera schauen: Stellung und Blickwinkel kommen
    // dann aus dem Skript, nicht aus der freien Kamera.
    Camera useCam = g_app->cam;
    ShotView shot;
    // Beim Abspielen bestimmt die Bahn die Stellung, nicht die Auswahl.
    const bool playingNow = g_app->playing && g_app->followCam &&
                            g_app->camTrackValid &&
                            !g_app->camTrack.segments.empty();
    const bool through = !playingNow && g_app->throughCamera &&
                         shotAtSelection(shot);
    // --- Durch die Kamera, auch OHNE Auswahl ------------------------------
    //
    // Gemeldet: "Through Camera hat nichts geaendert, sieht genau aus wie
    // vorher."
    //
    // Es hat auch nichts geaendert. Die Ansicht sprang bisher nur an, wenn
    // im Skriptbaum ein Kamerabefehl AUSGEWAEHLT war - oder waehrend des
    // Abspielens. Steht der Zeiger still und ist nichts angeklickt, blieb
    // die freie Kamera stehen, und daneben stand ein Hinweis, den man
    // leicht uebersieht.
    //
    // Das ist die falsche Vorgabe. Wer das Haekchen setzt, will sehen, was
    // die Kamera AN DIESER STELLE sieht - der Zeitzeiger sagt ja bereits,
    // welche Stelle gemeint ist. Die Auswahl bleibt als Uebersteuerung: sie
    // geht vor, damit man einen einzelnen Befehl gezielt anschauen kann.
    const bool amZeiger = !playingNow && !through && g_app->throughCamera &&
                          g_app->camTrackValid &&
                          !g_app->camTrack.segments.empty();
    // Fuer das Band merken. Ist keine der beiden Arten aktiv, muessen
    // Beschriftung und Bildwinkel zurueckgesetzt werden - sonst steht
    // neben dem Haekchen noch der Name der letzten Kamera.
    g_app->camViewActive = through || amZeiger;
    if (!g_app->camViewActive) {
        g_app->camViewLabel.clear();
        g_app->camViewFov = 90.0F;
    }
    if (playingNow || amZeiger) {
        // camStateAt statt camTrack.at: darin steckt FOLLOW.
        const CameraState st = camStateAt(g_app->playMs);
        g_app->camViewFov = st.fovX;
        for (int k = 0; k < 3; ++k) {
            useCam.pos[k] = st.pos[k];
            useCam.angles[k] = st.angles[k];
        }
        useCam.setFovX(st.fovX,
                       static_cast<float>(w) / static_cast<float>(std::max(h, 1)));
        g_app->camViewLabel = tr(Str::MapCamAtPlayhead);
    } else if (through) {
        g_app->camViewLabel = shot.label;
        g_app->camViewFov = shot.fovX;
        // Die Vorschau am Gizmo gilt AUCH hier.
        //
        // Sonst zieht man am Gizmo, und der Durchblick zeigt weiter die
        // alte Einstellung - also gerade das Bild, das man beurteilen
        // will, bleibt stehen. Nur wenn die Kamera angewaehlt ist; sonst
        // schwenkte der Durchblick, waehrend man anderswo arbeitet.
        const bool vorschau = g_app->cameraSelected;
        for (int k = 0; k < 3; ++k) {
            useCam.pos[k] = shot.pos[k];
            useCam.angles[k] = shot.haveAng ? shot.ang[k] : 0.0F;
            if (vorschau) {
                useCam.pos[k] += g_app->gizmoOffset[k];
                useCam.angles[k] += g_app->gizmoAngles[k];
            }
        }
        useCam.setFovX(shot.fovX,
                       static_cast<float>(w) / static_cast<float>(std::max(h, 1)));
        g_app->mapDirty = true;   // haengt an der Auswahl, also jedes Bild pruefen
    }

    // Hat sich die Kamera bewegt?
    g_app->gezeigteKamera = useCam;

    const float now[7] = {g_app->cam.pos[0], g_app->cam.pos[1], g_app->cam.pos[2],
                          g_app->cam.angles[0], g_app->cam.angles[1],
                          g_app->cam.angles[2], g_app->orbit.distance};
    for (int k = 0; k < 7; ++k) {
        if (std::fabs(now[k] - g_app->lastCam[k]) > 0.0001F) {
            g_app->mapDirty = true;
        }
        g_app->lastCam[k] = now[k];
    }

    // --- Wackeln aus Effekten -------------------------------------------
    //
    // Der Wackler aus dem SKRIPT (camera SHAKE) sitzt in camtrack.cpp und
    // wirkt schon lange. Effekte koennen dasselbe ausloesen - 73
    // CameraShake-Primitive in den .efx aus assets1 -, und das fehlte.
    //
    // Angewandt wird es HIER, nach allen Kamerazweigen, damit es die freie
    // Ansicht genauso trifft wie den Durchblick: eine Explosion neben dem
    // Betrachter wackelt, egal durch welche Kamera man sieht.
    if (g_app->showEffects && !g_app->effectRunners.empty()) {
        const float amp =
            effectShakeAt(g_app->effectRunners, g_app->playMs, useCam.pos);
        if (amp > 0.001F) {
            // DERSELBE zeitabgeleitete Wert wie in camtrack.cpp. Echter
            // Zufall waere in einer Vorschau, die man zurueckspult, Unruhe
            // statt Wiedergabetreue - man wuesste nie, ob eine Aenderung
            // von einem selbst kommt oder vom Wuerfel.
            auto wobble = [&](int axis) {
                auto h = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(g_app->playMs * 3.0) +
                    static_cast<std::int64_t>(axis) * 7919);
                h *= 2654435761U;
                h ^= h >> 15U;
                h *= 2246822519U;
                h ^= h >> 13U;
                return (static_cast<float>(h >> 8U) / 8388608.0F) - 1.0F;
            };
            for (int k = 0; k < 3; ++k) {
                useCam.pos[k] += wobble(k) * amp;
            }
            for (int k = 0; k < 2; ++k) {   // kein ROLL, wie in der Engine
                useCam.angles[k] += wobble(k + 3) * amp;
            }
            g_app->mapDirty = true;   // sonst steht das Bild still
        }
    }

    // --- Bewegt sich die Kamera? ----------------------------------------
    //
    // Verglichen wird die tatsaechlich benutzte Kamera, nicht `g_app->cam` -
    // eine Skriptfahrt bewegt sie ebenso wie die Maus.
    bool kameraBewegt = !g_app->haveLastCam;
    for (int k = 0; k < 3 && !kameraBewegt; ++k) {
        if (useCam.pos[k] != g_app->lastCamPos[k] ||
            useCam.angles[k] != g_app->lastCamAng[k]) {
            kameraBewegt = true;
        }
    }
    for (int k = 0; k < 3; ++k) {
        g_app->lastCamPos[k] = useCam.pos[k];
        g_app->lastCamAng[k] = useCam.angles[k];
    }
    g_app->haveLastCam = true;
    if (kameraBewegt) {
        g_app->stillCount = 0;
    } else if (g_app->stillCount < 1) {
        // Gerade stehengeblieben: EIN Bild in voller Aufloesung nachlegen.
        ++g_app->stillCount;
        g_app->mapDirty = true;
    }
    // Das Renderziel hat die Groesse der Ansicht. (Frueher rasterte der
    // Software-Zeichner waehrend der Bewegung groeber; der GPU-Weg braucht
    // das nicht.)
    {
        const int rw = std::max(64, w);
        const int rh = std::max(64, h);
        if (g_app->mapImage.width != rw || g_app->mapImage.height != rh) {
            g_app->mapImage.width = rw;
            g_app->mapImage.height = rh;
            g_app->mapDirty = true;
        }
    }

    // --- Erst aufbauen, dann ueber die Grafikkarte zeichnen ---------------
    //
    // In diesem Block steckt beides: die Szene und das Effektnetz AUFBAUEN,
    // und sie zeichnen. Gezeichnet wird nur noch ueber die Grafikkarte; der
    // Software-Rasterer ist entfernt (rc568).
    //
    // --- Beim Laden NICHT zeichnen ------------------------------------
    //
    // Das Netz wird aufgebaut, waehrend die Ansicht weiterlaeuft. Ein halbes
    // Netz an die Grafikkarte zu geben fuehrte zum Absturz (gemeldet beim
    // Laden einer Mission). Solange es nicht fertig ist, bleibt `mapDirty`
    // stehen, und das naechste Bild versucht es wieder.
    // `mapBereit` wird dort gesetzt, wo das Netz ausgetauscht wird - nicht
    // aus dem Netz erraten. Siehe App::mapBereit.
    const bool netzFertig = g_app->mapBereit && !g_app->mesh.batches.empty() &&
                            !g_app->mesh.verts.empty() &&
                            !g_app->mesh.indexes.empty();
    const bool gpuBereit = gpu::verfuegbar() && netzFertig;
    // Die Figuren dieses Bildes, wenn der GPU-Weg laeuft. Sie werden im
    // Block gesammelt und am ENDE gezeichnet - nach der Karte, damit sie auf
    // deren Tiefe treffen.
    std::vector<ActorDraw> gpuFiguren;
    bool gezeichnet = false;   // hat die Grafikkarte in diesem Bild gezeichnet?
    if (g_app->mapDirty) {
        // Die Blende kommt ganz zuletzt, in die Ueberlagerung - nach Figuren
        // und Effekten. Sonst uebermalen die sie wieder: "kann man durch die
        // schwarzen balken noch durchsehen wenn die cutscene laeuft".
        // Auch "am Abspielzeiger" (Through camera ohne Auswahl) ist ein Blick
        // durch die Kamera - mit Blende, wie im Spiel (Kartentest 27.09.).
        const bool blende = g_app->camViewActive || playingNow;
        const auto t0 = std::chrono::steady_clock::now();
        App::FrameCost kosten;
        // Die bewegten Teile sammeln: Tueren, Plattformen, Raumschiffe.
        //
        // Ihre Geometrie steht in der .bsp an ihrem GEBAUTEN Platz. Was das
        // Skript aendert, ist die Verschiebung dagegen - deshalb aktueller
        // Ort minus Startort, und nicht der Ort selbst. Bei t=0 ist beides
        // gleich, und nichts verrutscht.
        // Alles, was in diesem Bild an Effekten laeuft - Runner, Schuesse
        // und Folgeeffekte aller Stufen. Die Lichter weiter unten brauchen
        // dieselbe Liste: eine Explosion als Aufpralleffekt (rocket/
        // explosion.efx hat ein `Light`) soll die Figur daneben genauso
        // anstrahlen wie ein Runner.
        std::vector<EffectInstance> alleEffekte;

        // --- Effekte ------------------------------------------------------
        //
        // Die Partikel werden zu Vierecken und reisen als bewegliches Teil
        // durch denselben Aufsammelschritt wie alles andere. Kein zweiter
        // Zeichenweg - siehe bhed/efxdraw.h.
        //
        // Die Achsen der Kamera braucht es, weil die Vierecke senkrecht zur
        // Blickrichtung stehen. Sie kommen aus derselben Rechnung wie im
        // Zeichner: vorn aus den Winkeln, rechts = vorn x oben.
        if (g_app->showEffects && !g_app->effectRunners.empty()) {
            const float p = useCam.angles[0] * 3.14159265F / 180.0F;
            const float y = useCam.angles[1] * 3.14159265F / 180.0F;
            const float fwd[3] = {std::cos(p) * std::cos(y),
                                  std::cos(p) * std::sin(y), -std::sin(p)};
            float rgt[3] = {fwd[1], -fwd[0], 0.0F};
            const float rl = std::sqrt(rgt[0] * rgt[0] + rgt[1] * rgt[1]);
            if (rl > 0.0001F) {
                rgt[0] /= rl;
                rgt[1] /= rl;
            }
            const float upv[3] = {rgt[1] * fwd[2] - rgt[2] * fwd[1],
                                  rgt[2] * fwd[0] - rgt[0] * fwd[2],
                                  rgt[0] * fwd[1] - rgt[1] * fwd[0]};
            // Die Schuesse kommen dazu - dieselbe Liste, derselbe
            // Zeichenweg. Ein eigener waere ein zweiter Rasterisierer.
            std::vector<EffectInstance> alle = g_app->effectRunners;
            const std::vector<EffectInstance> schuesse =
                shotEffectsAt(g_app->playMs);
            alle.insert(alle.end(), schuesse.begin(), schuesse.end());

            // --- Und was daraus entsteht - ueber ALLE Stufen ------------
            //
            // impactFx, deathFx, emitFx und playfx starten weitere Effekte
            // (FxPrimitives.cpp:318, :109, :1477; FxScheduler.cpp:1953).
            // `effectFollowUpsAt` rechnet sie Teilchen fuer Teilchen und
            // Stufe fuer Stufe; die Dateien schlaegt die Anwendung nach.
            //
            // Hier stand bis rc568 "NUR EINE STUFE", mit der Begruendung,
            // keine der Vorlagen brauche mehr. Gezaehlt ueber die 649
            // Dateien von base und MD reichen Ketten aber bis vier Stufen
            // (exegol/exegol_lightning_strikes_rift), und Truemmer, die
            // rauchen, brauchen schon zwei. Die Bremse steckt jetzt in
            // effectFollowUpsAt: kMaxFolgeTiefe und kMaxFolgeEffekte (die
            // 1200 von MAX_EFFECTS).
            //
            // Jeder Folgeeffekt traegt seinen Samen vom ausloesenden
            // Teilchen (EffectImpact::seed) - derselbe Aufprall zeigt also
            // bei jedem Bild und nach dem Zurueckspringen dieselben Funken.
            {
                const std::vector<EffectInstance> folgen = effectFollowUpsAt(
                    alle, g_app->playMs, &g_app->geo,
                    [](const std::string& name) { return effectByName(name); });
                alle.insert(alle.end(), folgen.begin(), folgen.end());
            }
            if (schuesse.size() != g_app->lastShotFxCount) {
                g_app->lastShotFxCount = schuesse.size();
                char sz[110];
                std::snprintf(sz, sizeof(sz),
                              "Schusseffekte bei %.0f ms: %zu",
                              g_app->playMs, schuesse.size());
                diag::detail(sz);
            }
            const auto tE0 = std::chrono::steady_clock::now();
            EffectCull auge;
            for (int k = 0; k < 3; ++k) {
                auge.viewer[k] = useCam.pos[k];
            }
            auge.haveViewer = true;
            auge.sichtTabelle = false;
            g_app->effectMesh =
                // Die Kartengeometrie mit: ohne sie kann ein Partikel mit
                // `usePhysics` nicht abprallen - siehe efx::sim::flugbahn.
                // --- Die Keulung ist NOCH NICHT eingeschaltet -----------
                //
                // `EffectCull` steht bereit (bhed/efxdraw.h) und spart
                // gemessen 337 auf 0,6 ms. Sie wird trotzdem nicht
                // uebergeben, weil sie in drei Probeansichten SICHTBARE
                // Effekte weggenommen hat - auch mit einem Freiraum von
                // 4000 Einheiten.
                //
                // Der Grund: die Sichtbarkeitstabelle beantwortet die Frage
                // "sieht Raum A Raum B". Ein Effekt ist aber kein Punkt -
                // die Teilchen von volcano.efx fliegen 1200 Einheiten weit
                // und reichen damit in Raeume, die der Runner selbst nie
                // beruehrt. Und eine Vorschaukamera steht oft AUSSERHALB
                // der Karte, wo der Cluster nichts taugt.
                //
                // Eine Keulung, die manchmal etwas wegnimmt, ist schlimmer
                // als eine, die es nicht gibt: sie erzeugt Fehler, die man
                // fuer Effektfehler haelt und woanders sucht.
                //
                // Das AUGE wird trotzdem uebergeben - ohne die Tabelle
                // (sichtTabelle = false). Ohne Auge gibt es weder die
                // Bandbreite nach RB_SurfaceLine noch die Richtung eines
                // Blitzes (RB_SurfaceElectricity) noch CParticle::Cull.
                buildEffectMesh(alle, g_app->playMs, rgt, upv,
                                &g_app->effectShaders, &g_app->geo, &auge);
            // --- Der Vollbildblitz (Flash) ------------------------------
            //
            // Als Sprite IM Effektnetz, nicht in der 2D-Ueberlagerung: nur so
            // gelten Shader und Mischart der .efx, und der Blitz liegt wie im
            // Spiel UNTER den Kinobalken (siehe appendScreenFlashes).
            {
                const float seiten = static_cast<float>(w) /
                                     static_cast<float>(std::max(h, 1));
                const float fovX =
                    2.0F *
                    std::atan(std::tan(useCam.fovY * 3.14159265F / 360.0F) *
                              seiten) *
                    180.0F / 3.14159265F;
                const std::vector<ScreenFlash> blitze =
                    effectScreenFlashesAt(alle, g_app->playMs, useCam.pos, fwd);
                appendScreenFlashes(g_app->effectMesh, blitze, useCam.pos, fwd,
                                    rgt, upv, fovX, &g_app->effectShaders);
            }
            // Und was an Emittern haengt - hier, wo `alle` steht.
            g_app->emitterFlug =
                emitterModelsAt(alle, g_app->playMs, &g_app->geo);
            alleEffekte = std::move(alle);
            kosten.effects = std::chrono::duration<float, std::milli>(
                                 std::chrono::steady_clock::now() - tE0)
                                 .count();
        } else {
            g_app->effectMesh = BspMesh{};
        }

        std::vector<MoverDraw> movers;

        // --- .md3-Modelle -------------------------------------------------
        //
        // Auch sie reisen als bewegliches Teil durch denselben
        // Aufsammelschritt. Der Unterschied zu den Brush-Modellen: ihre
        // Ecken stehen im Modellraum, nicht in Weltkoordinaten. Deshalb ist
        // pivot der Nullpunkt und offset der Ort der Entity.
        //
        // Animierte Modelle - 163 der 451 Dateien - laufen mit dem
        // Abspielzeiger. Die Bildrate im Spiel ist ueblicherweise 20 Bilder
        // je Sekunde; genau danach richtet sich die Auswahl.
        for (std::size_t mi = 0; mi < g_app->mapModels.size(); ++mi) {
            const App::MapModel& mm = g_app->mapModels[mi];
            // Bewegt es ein Skript? Dann steht es, wo die Szene es hat.
            const int fig = (mi < g_app->modellFigur.size()) ? g_app->modellFigur[mi] : -1;
            ActorState figStand;
            const bool bewegt = fig >= 0 && static_cast<std::size_t>(fig) < g_app->scene.actors.size();
            if (bewegt) {
                figStand = g_app->scene.actors[static_cast<std::size_t>(fig)].at(g_app->playMs);
                if (!figStand.visible) {
                    continue;
                }
            }
            // Ein misc_model_breakable nach use: Schadensmodell, _u1 oder
            // nichts (MoverSim::modellAt, misc_model_breakable_die).
            const MoverSim::ModellStand bruch =
                g_app->moverSim.modellAt(mm.entity, g_app->playMs);
            if (bruch.bekannt && !bruch.sichtbar) {
                continue;
            }
            std::string pfad = mm.path;
            int basis = mm.shaderBase;
            if (bruch.bekannt && bruch.modell != mm.path) {
                const auto nb = g_app->nebenModelle.find(bruch.modell);
                if (nb == g_app->nebenModelle.end()) {
                    continue;   // _d1 fehlt: die Engine zeigt Modell 0, also nichts
                }
                pfad = bruch.modell;
                basis = nb->second;
            }
            const auto have = g_app->md3Files.find(pfad);
            if (have == g_app->md3Files.end()) {
                continue;
            }
            int frame = 0;
            const int bilder = have->second.numFrames;
            // Nach dem Bruch steht die Animation (s.frame = 0).
            if (bilder > 1 && !bruch.angehalten && !mm.standbild) {
                frame = static_cast<int>(g_app->playMs * 0.020) % bilder;
            }
            const std::string key = pfad + "#" + std::to_string(frame);
            auto netz = g_app->md3Meshes.find(key);
            if (netz == g_app->md3Meshes.end()) {
                netz = g_app->md3Meshes
                           .emplace(key, md3ToMesh(have->second, frame,
                                                   basis))
                           .first;
            }
            if (netz->second.indexes.empty()) {
                continue;
            }
            MoverDraw d;
            d.mesh = &netz->second;
            for (int k = 0; k < 3; ++k) {
                d.offset[k] = bewegt ? figStand.pos[k] : mm.origin[k];
                d.pivot[k] = 0.0F;
                d.scale[k] = mm.scale[k];
            }
            d.pitch = bewegt ? figStand.angles[0] : mm.angles[0];
            d.yaw = bewegt ? figStand.angles[1] : mm.angles[1];
            d.roll = bewegt ? figStand.angles[2] : mm.angles[2];
            movers.push_back(d);
        }

        // --- Die Bruchstuecke (CG_Chunks -> LE_FRAGMENT) ---------------
        //
        // Wo jedes Stueck gerade fliegt oder liegt, rechnet MoverSim
        // (truemmerAt). Das Netz je Modell einmal, gedreht und skaliert
        // ueber den MoverDraw - wie die Modelle an den Emittern.
        //
        // OFFEN: das Ausblenden der letzten Sekunde (RF_ALPHA_FADE) - ein
        // MoverDraw kennt keine Deckkraft. Das Stueck verschwindet am Ende
        // seiner Lebenszeit auf einmal.
        for (const Truemmer& tr : g_app->moverSim.truemmer()) {
            const TruemmerStand st = truemmerAt(tr, g_app->playMs);
            if (!st.sichtbar) {
                continue;
            }
            const auto nb = g_app->nebenModelle.find(tr.modell);
            const auto datei = g_app->md3Files.find(tr.modell);
            if (nb == g_app->nebenModelle.end() || datei == g_app->md3Files.end()) {
                continue;
            }
            const std::string key = tr.modell + "#0";
            auto netz = g_app->md3Meshes.find(key);
            if (netz == g_app->md3Meshes.end()) {
                netz = g_app->md3Meshes
                           .emplace(key, md3ToMesh(datei->second, 0, nb->second))
                           .first;
            }
            if (netz->second.indexes.empty()) {
                continue;
            }
            MoverDraw d;
            d.mesh = &netz->second;
            for (int k = 0; k < 3; ++k) {
                d.offset[k] = st.ort[k];
                d.pivot[k] = 0.0F;
                d.scale[k] = st.radius;   // le->radius auf allen drei Achsen
            }
            d.pitch = st.winkel[0];
            d.yaw = st.winkel[1];
            d.roll = st.winkel[2];
            movers.push_back(d);
        }

        // --- Die Modelle an den Emittern -----------------------------
        //
        // "Emitters don't draw themselves, but they may need to add an
        // attached model" (FxPrimitives.cpp:1375). Rauch seit rc323, Ort
        // und Drehung seit rc324 - hier wird endlich gezeichnet.
        for (const EmitterModelDraw& em : g_app->emitterFlug) {
            const auto wo = g_app->emitterModels.find(em.modelPath);
            if (wo == g_app->emitterModels.end()) {
                continue;   // beim Laden nicht gefunden, schon gemeldet
            }
            const auto datei = g_app->md3Files.find(em.modelPath);
            if (datei == g_app->md3Files.end()) {
                continue;
            }
            // Das Netz je Modell einmal bauen, wie bei den Kartenmodellen.
            // Gedreht wird ueber den MoverDraw, NICHT durch ein neues Netz
            // je Bild - sonst baute ein taumelndes Modell sechzig Netze je
            // Sekunde.
            const std::string key = em.modelPath + "#0";
            auto netz = g_app->md3Meshes.find(key);
            if (netz == g_app->md3Meshes.end()) {
                netz = g_app->md3Meshes
                           .emplace(key, md3ToMesh(datei->second, 0,
                                                   wo->second.shaderBase))
                           .first;
            }
            if (netz->second.indexes.empty()) {
                continue;
            }
            MoverDraw d;
            d.mesh = &netz->second;
            for (int k = 0; k < 3; ++k) {
                d.offset[k] = em.origin[k];
                d.pivot[k] = 0.0F;
            }
            // Die Reihenfolge im efx ist Nick/Gier/Roll, wie bei allen
            // Winkeln in Quake-Engines.
            d.pitch = em.angles[0];
            d.yaw = em.angles[1];
            d.roll = em.angles[2];
            // Der Massstab aus der size-Kurve: CEmitter::Draw skaliert die
            // Achsen mit mRefEnt.radius (FxPrimitives.cpp:1394). Bis rc568
            // stand hier nichts, und jedes Modell hatte Groesse eins.
            for (int k = 0; k < 3; ++k) {
                d.scale[k] = em.scale;
            }
            movers.push_back(d);
        }

        {
            // JEDES Untermodell wird gezeichnet, nicht nur die bewegten:
            // die Welt (Modell 0) enthaelt sie ja nicht mehr. Wer kein
            // Skript hat, steht mit Verschiebung null an seinem Platz -
            // also genau dort, wo er vorher auch stand.
            // Wo steht welches Brush-Modell?
            //
            // Ein Brush-Modell mit einem "origin"-Schluessel hat seine
            // Geometrie RELATIV dazu gespeichert - q3map2 zieht den
            // Ursprung ab, sobald ein origin-Brush im Gebilde steckt. Die
            // Engine addiert ihn beim Zeichnen wieder.
            //
            // Genau das fehlte: md_am_sith hat vier solche Gebilde
            // (armpiece1/2, plat1 und das Raumschiff sta2), und alle vier
            // wurden am NULLPUNKT der Karte gezeichnet statt an ihrem
            // Platz. Bei sta2 fiel es auf, weil es sich auch noch bewegen
            // sollte - "die raumschiff animation fehlt".
            //
            // Modell 125 hat Ausmasse von -118 bis 84 um die Null, der
            // Eintrag dazu "origin" "-1514 7376 645". Ohne die Addition
            // liegt das Schiff 7000 Einheiten daneben.
            std::unordered_map<int, std::array<float, 3>> modellOrt;
            for (const MapEntity& e : g_app->map.entities) {
                const std::string* mdl = e.find("model");
                if (mdl == nullptr || mdl->empty() || (*mdl)[0] != '*') {
                    continue;
                }
                int nummer = 0;
                if (!parseInt(mdl->substr(1), nummer)) {
                    continue;
                }
                std::array<float, 3> o{0.0F, 0.0F, 0.0F};
                if (!e.origin.empty()) {
                    std::istringstream is(e.origin);
                    is >> o[0] >> o[1] >> o[2];
                }
                modellOrt[nummer] = o;
            }
            std::vector<bool> schonDa(g_app->geo.models.size(), false);
            for (const Actor& a : g_app->scene.actors) {
                if (a.brushModel <= 0 || !a.haveStart ||
                    static_cast<std::size_t>(a.brushModel) >=
                        schonDa.size()) {
                    continue;   // 0 waere die Welt selbst
                }
                schonDa[static_cast<std::size_t>(a.brushModel)] = true;
            }
            for (std::size_t k = 1; k < g_app->geo.models.size(); ++k) {
                if (schonDa[k]) {
                    continue;   // kommt gleich mit seiner Bewegung
                }
                const auto have = g_app->brushMeshes.find(static_cast<int>(k));
                if (have == g_app->brushMeshes.end()) {
                    g_app->brushMeshes.emplace(
                        static_cast<int>(k),
                        // Der Himmelsschalter gilt auch fuer bewegliche
                        // Teile - siehe die Vorgabe in bspgeo.h.
                        buildModelMesh(g_app->geo, static_cast<int>(k),
                                       g_app->meshDetail,
                                       !g_app->showSky));
                }
                const BspMesh& bm = g_app->brushMeshes[static_cast<int>(k)];
                if (bm.indexes.empty()) {
                    continue;
                }
                MoverDraw m;
                m.mesh = &bm;
                // Tueren, func_wall & Co.: der Stand aus der Mover-
                // Simulation (bhed/mover.h) - Ort, Winkel, sichtbar.
                const MoverSim::Stand ms =
                    g_app->moverSim.at(static_cast<int>(k), g_app->playMs);
                if (ms.bekannt) {
                    if (!ms.sichtbar) {
                        continue;   // func_wall/func_usable START_OFF
                    }
                    for (int q = 0; q < 3; ++q) {
                        m.offset[q] = ms.origin[q];
                    }
                    m.pitch = ms.angles[0];
                    m.yaw = ms.angles[1];
                    m.roll = ms.angles[2];
                    movers.push_back(m);
                    continue;
                }
                // Der Ursprung der Entity ist die Verschiebung. Fuer die
                // allermeisten Modelle ist er null - dann steht es, wo es
                // steht, wie bisher.
                const auto ort = modellOrt.find(static_cast<int>(k));
                if (ort != modellOrt.end()) {
                    for (int q = 0; q < 3; ++q) {
                        m.offset[q] = ort->second[static_cast<std::size_t>(q)];
                    }
                }
                movers.push_back(m);
            }
        }
        if (g_app->showActors && !g_app->scene.actors.empty()) {
            for (const Actor& a : g_app->scene.actors) {
                if (a.brushModel <= 0 || !a.haveStart) {
                    continue;   // 0 waere die Welt selbst
                }
                const auto have = g_app->brushMeshes.find(a.brushModel);
                if (have == g_app->brushMeshes.end()) {
                    g_app->brushMeshes.emplace(
                        a.brushModel,
                        buildModelMesh(g_app->geo, a.brushModel, g_app->meshDetail));
                }
                const BspMesh& bm = g_app->brushMeshes[a.brushModel];
                if (bm.indexes.empty()) {
                    continue;
                }
                const ActorState st = a.at(g_app->playMs);
                if (!st.visible) {
                    continue;
                }
                MoverDraw m;
                m.mesh = &bm;
                // Verschoben wird um den AKTUELLEN Ursprung, nicht um die
                // Differenz zum Startort.
                //
                // Der Grund ist derselbe wie oben: die Geometrie liegt
                // relativ zum Ursprung. "Aktueller Ort minus Startort"
                // stimmte nur scheinbar - beim Start kam null heraus, und
                // damit stand das Schiff am Nullpunkt der Karte statt an
                // seinem Platz. Gedreht wird um die Null der Geometrie,
                // also um den Ursprung der Entity - dort liegt er.
                for (int k = 0; k < 3; ++k) {
                    m.offset[k] = st.pos[k];
                    m.pivot[k] = 0.0F;
                }
                // Die Winkel ABSOLUT, mit Nicken und Rollen - so zeichnet
                // die Engine currentAngles. Vorher nur die Gier, und die
                // relativ zum Kartenwinkel: kippende Schiffe, Luken und
                // Zugbruecken standen still, und ein rotate auf eine Tuer
                // mit "angle" tat nichts.
                m.pitch = st.angles[0];
                m.yaw = st.angles[1];
                m.roll = st.angles[2];
                movers.push_back(m);
            }
        }
        // Die Figuren NACH der Karte: sie benutzen deren Tiefenpuffer und
        // verschwinden dadurch hinter Waenden.
        // Welche NPC-Typen schon gemeldet wurden, dass ihnen die
        // Schussanimation fehlt. Ohne das kaeme die Zeile in jedem Bild.
        static std::set<std::string> schussFehlt;
        const auto tS0 = std::chrono::steady_clock::now();
        // Die Lichter, die Effekte gerade werfen - EINMAL fuer alle
        // Figuren, nicht je Figur neu. In md_ga_jedi laufen 28 fx_runner;
        // sie je Figur durchzugehen waere 28 mal 62 Durchlaeufe je Bild.
        const std::vector<EffectLight> fxLichter =
            g_app->showEffects
                ? effectLightsAt(alleEffekte.empty() ? g_app->effectRunners
                                                     : alleEffekte,
                                 g_app->playMs)
                : std::vector<EffectLight>{};
        if (g_app->showActors && !g_app->scene.actors.empty()) {
            if (g_app->actorAssets.size() != g_app->scene.actors.size()) {
                loadActorModels();
                kinoSkeletteAbgleichen();
                // Griffe und Klingenbilder gleich mit - siehe
                // preloadSaberAssets. Sonst fallen sie mitten in ein Bild.
                preloadSaberAssets();
                // Die Effekte der Karte gleich mit: sie haengen an den
                // Entities, nicht am Skript, und aendern sich erst mit der
                // naechsten Karte.
                loadMapEffects();
                // Die .md3-Modelle danach: sie haengen ihre Texturen hinten
                // an textures.byShader an, und die Karte muss vorher stehen.
                loadMapModels();
            }
            std::vector<ActorDraw> draws;
            draws.reserve(g_app->scene.actors.size());
            for (std::size_t ai = 0; ai < g_app->scene.actors.size(); ++ai) {
                const Actor& a = g_app->scene.actors[ai];
                if (ai >= g_app->actorAssets.size()) {
                    break;
                }
                App::ActorAssets& assets = g_app->actorAssets[ai];
                if (assets.model.empty() || !a.haveStart) {
                    continue;
                }
                const ActorState st = a.at(g_app->playMs);
                if (!st.visible) {
                    continue;
                }
                ActorDraw d;
                d.model = &assets.model;
                d.textures = (assets.textures.found != 0) ? &assets.textures
                                                          : nullptr;
                // Das Skelett der FIGUR, nicht das der Modellansicht.
                //
                // Vorher stand hier g_app->anim - das von Hand geladene.
                // Wer eine Mission oeffnet, hat dort nichts geladen, also
                // war es leer, und jede Figur blieb in Ruhelage stehen.
                // Der Pfad der .gla steht im Kopf des Modells; skeletonFor()
                // holt sie und die animation.cfg dazu.
                if (assets.skeleton != nullptr) {
                    d.anim = &assets.skeleton->anim;
                    // --- Kennt dieses Skelett den Namen? ----------------
                    //
                    // Kennt es ihn nicht, laesst die Engine die Figur in
                    // der Animation, die sie schon hatte (siehe den Kopf
                    // von frameForAnimationIn). Genau das tun wir: erst
                    // die gewuenschte, dann die vorige, zuletzt der
                    // Grundzustand.
                    //
                    // Bleibt auch der unbekannt, ist das Skelett zu
                    // fremd - dann Bild 0, und die Meldung sagt warum.
                    // st.animSpeed: die Animation laeuft MIT dem Gehtempo,
                    // sonst rutschen die Fuesse - siehe kAnimSpeedWalk in
                    // scene.h.
                    d.frame = frameForAnimationIn(
                        assets.skeleton->sections, st.animation,
                        g_app->playMs - st.animStartMs, st.holdAnim,
                        st.animSpeed, &d.frameFraction, &d.frameNext);
                    if (d.frame < 0) {
                        // EINMAL je Skelett und Name melden, nicht je Bild.
                        // Sonst stehen sechzig Zeilen je Sekunde im
                        // Protokoll und die Datei laeuft voll.
                        const std::string schluessel =
                            assets.model.animFile + "|" + st.animation;
                        if (g_app->unknownAnims.insert(schluessel).second) {
                            diag::detail(
                                "Figur " + a.name + ": Animation \"" +
                                st.animation + "\" steht nicht in der .cfg "
                                "von \"" + assets.model.animFile +
                                "\" - die Engine laesst die Figur in diesem "
                                "Fall stehen, wo sie war.");
                        }
                        d.frame = frameForAnimationIn(
                            assets.skeleton->sections, st.prevAnimation,
                            st.prevAnimSinceMs, st.prevHoldAnim);
                    }
                    if (d.frame < 0) {
                        d.frame = frameForAnimationIn(
                            assets.skeleton->sections, "BOTH_STAND1", 0.0,
                            false);
                    }
                    if (d.frame < 0) {
                        d.frame = 0;
                    }
                    // --- Die Schussanimation --------------------------
                    //
                    // Sie liegt auf dem OBERKOERPER (SETANIM_TORSO) und
                    // faengt bei JEDEM Schuss neu an
                    // (SETANIM_FLAG_RESTART, bg_pmove.cpp:13810). Der
                    // Abstand kommt aus SET_SHOT_SPACING.
                    //
                    // Sie geht VOR einer Oberkoerperanimation aus dem
                    // Skript: die Engine setzt sie im Bewegungsschritt,
                    // also nach allem, was ICARUS gesetzt hat.
                    if (st.firing && assets.skeleton != nullptr) {
                        // Seit dem FEUERBEGINN, nicht seit dem
                        // Animationsbeginn - siehe firingSinceMs.
                        const double seit =
                            g_app->playMs - st.firingSinceMs;
                        const double takt =
                            std::fmod(std::max(seit, 0.0),
                                      static_cast<double>(
                                          std::max(st.shotSpacingMs, 1.0F)));
                        const int bild = frameForAnimationIn(
                            assets.skeleton->sections, "BOTH_ATTACK3", takt,
                            false);
                        if (bild >= 0) {
                            d.torsoFrame = bild;
                        } else if (!schussFehlt.count(a.npcType)) {
                            // EINMAL je NPC-Typ melden, nicht je Bild.
                            schussFehlt.insert(a.npcType);
                            diag::detail(
                                "Figur " + a.name + " schiesst, aber \"" +
                                "BOTH_ATTACK3\" steht nicht in der .cfg " +
                                "seines Skeletts (NPC-Typ " + a.npcType + ")");
                        }
                    }

                    // Der MUND, wenn die Figur gerade spricht.
                    const std::string mund =
                        faceAnimFor(a.name, g_app->playMs);
                    if (!mund.empty()) {
                        d.faceFrame = frameForAnimationIn(
                            assets.skeleton->sections, mund, 0.0, false);
                    }
                    // Der OBERKOERPER, wenn er eine eigene Animation hat.
                    //
                    // Kennt das Skelett den Namen nicht, bleibt torsoFrame
                    // bei -1 und der Oberkoerper folgt dem Koerper - genau
                    // wie beim Koerper selbst: die Engine tut in dem Fall
                    // gar nichts.
                    //
                    // Schiesst die Figur, gewinnt der Schuss: die Engine
                    // setzt BOTH_ATTACK3 mit SETANIM_FLAG_OVERRIDE
                    // (bg_pmove.cpp:13810). Vorher ueberschrieb die
                    // Skriptanimation das Schussbild wieder.
                    if (!st.torsoAnimation.empty() && d.torsoFrame < 0) {
                        d.torsoFrame = frameForAnimationIn(
                            assets.skeleton->sections, st.torsoAnimation,
                            g_app->playMs - st.torsoStartMs, st.torsoHold, 1.0F,
                            &d.torsoFraction, &d.torsoNext);
                        if (d.torsoFrame < 0) {
                            const std::string schluessel =
                                assets.model.animFile + "|" + st.torsoAnimation;
                            if (g_app->unknownAnims.insert(schluessel).second) {
                                diag::detail(
                                    "Figur " + a.name + ": Oberkoerperanimation \"" +
                                    st.torsoAnimation +
                                    "\" steht nicht in der .cfg von \"" +
                                    assets.model.animFile + "\".");
                            }
                        }
                    }
                    // Laeuft gerade ein Uebergang? Dann die EINGEFRORENE
                    // Frame der vorigen Animation dazu.
                    //
                    // prevAnimSinceMs steht fest - es ist der Stand vom
                    // Moment des Umschaltens, nicht "jetzt minus damals".
                    // Genau daran erkennt man, ob es richtig gebaut ist:
                    // die alte Seite darf sich waehrend des Uebergangs
                    // nicht bewegen.
                    // Der eigene Uebergang des Oberkoerpers.
                    const float lt = torsoBlendFraction(st, g_app->playMs);
                    if (lt < 1.0F) {
                        d.torsoPrevFrame = frameForAnimationIn(
                            assets.skeleton->sections, st.torsoPrevAnimation,
                            st.torsoPrevSinceMs, false);
                        d.torsoBlendLerp = (d.torsoPrevFrame < 0) ? 1.0F : lt;
                    }
                    const float l = blendFraction(st, g_app->playMs);
                    if (l < 1.0F) {
                        d.prevFrame =
                            frameForAnimationIn(assets.skeleton->sections,
                                                st.prevAnimation,
                                                st.prevAnimSinceMs,
                                                st.prevHoldAnim);
                        // Kennt das Skelett die vorige nicht, gibt es
                        // nichts zu ueberblenden - dann lieber hart
                        // umschalten als gegen ein falsches Bild mischen.
                        d.blendLerp = (d.prevFrame < 0) ? 1.0F : l;
                    }
                }
                for (int k = 0; k < 3; ++k) {
                    d.pos[k] = st.pos[k];
                }
                d.yaw = st.angles[1];
                // Das Licht an ihrem Standort, aus dem Gitter der Karte.
                //
                // Die Engine nimmt dafuer den URSPRUNG der Entity
                // (R_SetupEntityLighting, tr_light.cpp:401), nicht die
                // Mitte des Modells - eine Figur wird also nach dem Licht
                // an ihren Fuessen beleuchtet. Das ist grob, aber es ist,
                // was das Spiel zeigt.
                d.light = sampleLightGrid(g_app->geo, d.pos);
                // Dazu die Lichter, die Effekte gerade werfen. Die Engine
                // rechnet sie an derselben Stelle drauf
                // (R_SetupEntityLighting, tr_light.cpp:435 ff.).
                addDynamicLights(d.light, d.pos, fxLichter);

                // --- Waffe, Lichtschwerter, Kinomodelle -------------
                //
                // Alles aus dem Zustand der Figur (ActorState::hand,
                // saber, kino) - siehe src/ausruestung.cpp. Der GRIFF haengt,
                // solange er angehaengt ist, auch mit ausgeschalteter
                // Klinge; die KLINGEN nur, solange sie laenger als 0.5 sind.
                anbautenFuer(a, st, g_app->playMs, d);
                // --- Wohin der Kopf schaut ---------------------------
                //
                // Die Richtung zum Blickziel MINUS der Richtung, in die
                // der Koerper schaut - der Kopf dreht sich ja gegen den
                // Koerper, nicht gegen die Welt. Beschnitten auf die 64
                // Grad, die die Engine zulaesst; wer weiter schauen soll,
                // braucht ein SET_WATCHTARGET.
                if (st.hasLookTarget && g_app->lookRotation) {
                    const float dx = st.lookAt[0] - st.pos[0];
                    const float dy = st.lookAt[1] - st.pos[1];
                    if (dx * dx + dy * dy > 1.0F) {
                        const float ziel =
                            std::atan2(dy, dx) * 180.0F / 3.14159265F;
                        // Der Ghoul2-Weg der Engine dreht den Oberkoerper
                        // NICHT dem Blick nach - siehe kHeadYawClampMin in
                        // scene.h. Es gibt nur den Kopfausschlag, und der
                        // verteilt sich im Zeichner auf drei Knochen.
                        d.headYaw = std::clamp(
                            shortestAngleDelta(st.angles[1], ziel),
                            kHeadYawClampMin, kHeadYawClampMax);
                    }
                }
                d.yawOffset = static_cast<float>(g_app->actorYaw);
                draws.push_back(d);
            }
            if (!draws.empty()) {
                kosten.scene = std::chrono::duration<float, std::milli>(
                                   std::chrono::steady_clock::now() - tS0)
                                   .count();
                const auto tA0 = std::chrono::steady_clock::now();
                // Merken - gezeichnet wird am Ende des Blocks, nach der
                // Karte, damit die Figuren auf deren Tiefe treffen.
                gpuFiguren = draws;
                kosten.actors = std::chrono::duration<float, std::milli>(
                                    std::chrono::steady_clock::now() - tA0)
                                    .count();
            }
        }
        const auto tG0 = std::chrono::steady_clock::now();
        // Ohne Gizmos auch keine Klickziele: sonst bleiben die Stellen des
        // letzten freien Bildes stehen, und ein Klick in den Kamerablick
        // waehlt etwas, das dort gar nicht zu sehen ist.
        if (g_app->camViewActive || playingNow) {
            g_app->markers.clear();
            g_app->keyMarks.clear();
            g_app->actorMarks.clear();
            g_app->cameraSx = -1.0F;
            g_app->cameraSy = -1.0F;
        }

        const auto t1 = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
        kosten.gizmos =
            std::chrono::duration<float, std::milli>(t1 - tG0).count();
        kosten.total = ms;
        // Geglaettet: eine ungeglaettete Anzeige springt zu stark.
        g_app->mapFrameMs = (g_app->mapFrameMs <= 0.0F)
                                ? ms
                                : g_app->mapFrameMs * 0.9F + ms * 0.1F;
        // Dieselbe Glaettung fuer die Einzelteile, damit sie sich mit der
        // Gesamtzeit vergleichen lassen.
        auto glatt = [](float& alt2, float neu2) {
            alt2 = (alt2 <= 0.0F) ? neu2 : alt2 * 0.9F + neu2 * 0.1F;
        };
        glatt(g_app->frameCost.scene, kosten.scene);
        glatt(g_app->frameCost.effects, kosten.effects);
        glatt(g_app->frameCost.map, kosten.map);
        glatt(g_app->frameCost.actors, kosten.actors);
        glatt(g_app->frameCost.glow, kosten.glow);
        glatt(g_app->frameCost.gizmos, kosten.gizmos);
        glatt(g_app->frameCost.total, kosten.total);
        // Ungeglaettet daneben - siehe FrameCost::rawMap.
        g_app->frameCost.rawMap = kosten.map;
        g_app->frameCost.rawTotal = kosten.total;

        // --- Die Aufschluesselung ins Protokoll --------------------------
        //
        // Gefragt: "hast du jetzt eingebaut im Debug-Log, was wie viel
        // kostet und warum?"
        //
        // Bis hierher stand sie nur im Mauszeiger-Kasten und hinter einem
        // Menuepunkt. Beim Abspielen muesste man also gleichzeitig
        // hinsehen - und genau dort ist es am langsamsten.
        //
        // Jetzt schreibt sie sich selbst, sobald sich die Gesamtzeit um
        // mehr als ein Fuenftel aendert. Nicht in jedem Bild: bei sechzig
        // Bildern je Sekunde waere das Protokoll unlesbar, und die Zeiten
        // schwanken ohnehin staendig um ein paar Prozent. Dasselbe Mass
        // wie beim Keulen und bei den Effektlichtern.
        //
        // Das "warum" steht mit dabei: die Zeile nennt jede Stufe einzeln,
        // dazu die Groesse des Bildes und die Zahl der Figuren. Ohne die
        // beiden weiss man zwar, WAS teuer ist, aber nicht WOMIT es
        // verglichen gehoert.
        {
            // --- ROH melden, nicht geglaettet ----------------------------
            //
            // Aus einem Protokoll: die Zahl "Szene" lief ueber ZWOELF
            // Zeilen von 490 auf 0,1 herunter, jede Zeile ein Verhaeltnis
            // von 0,729 zur vorigen. Das ist keine Messung, das ist meine
            // Glaettung (0,9 je Bild), die EIN teures Bild vierzig Bilder
            // lang nachhallen laesst - und meine Regel "melde bei 20 %
            // Aenderung" hat jeden Abklingschritt als Neuigkeit gemeldet.
            //
            // Ein Messgeraet, das seinen eigenen Nachhall als Befund
            // ausgibt, ist schlimmer als keines: man sucht dann nach einer
            // Ursache fuer etwas, das langst vorbei ist.
            //
            // Fuer die ANZEIGE bleibt die Glaettung richtig - eine
            // springende Zahl liest sich nicht. Fuers PROTOKOLL zaehlt der
            // rohe Wert DIESES Bildes: ein Ausreisser gibt dann genau eine
            // Zeile, und die naechste sagt schon wieder die Wahrheit.
            const App::FrameCost& k = kosten;
            // WICHTIG: die ganze Messung sitzt innerhalb von `mapDirty`.
            // Wird das Bild nicht neu gezeichnet, laufen weder Karte noch
            // Figuren - und dann stuenden hier lauter Nullen, die wie ein
            // Befund aussaehen und keiner sind. Deshalb wird nur
            // geschrieben, wenn wirklich gezeichnet wurde.
            const bool ersteMal = g_app->lastFrameMsLogged < 0.0F;
            // Dreissig Prozent UND mindestens drei Millisekunden. Ohne
            // die zweite Bedingung meldete ein Sprung von 0,3 auf 0,4 ms
            // genauso laut wie einer von 20 auf 60.
            const bool starkAnders =
                !ersteMal &&
                std::fabs(k.total - g_app->lastFrameMsLogged) >
                    std::max(g_app->lastFrameMsLogged * 0.3F, 3.0F);
            if ((ersteMal && k.total > 0.0F) || starkAnders) {
                g_app->lastFrameMsLogged = k.total;
                char zz[320];
                std::snprintf(
                    zz, sizeof(zz),
                    "Zeit je Bild %.1f ms (%.0f/s): Karte %.1f, Figuren %.1f,"
                    " Effekte %.1f, Szene %.1f, Gluehen %.1f,"
                    " Gizmos %.1f"
                    "  |  Bild %dx%d, %zu Figuren, %s",
                    static_cast<double>(k.total),
                    (k.total > 0.01F) ? 1000.0 / static_cast<double>(k.total)
                                      : 0.0,
                    static_cast<double>(k.map), static_cast<double>(k.actors),
                    static_cast<double>(k.effects),
                    static_cast<double>(k.scene),
                    static_cast<double>(k.glow),
                    static_cast<double>(k.gizmos), g_app->mapImage.width,
                    g_app->mapImage.height, g_app->scene.actors.size(),
                    g_app->playing ? "spielt" : "steht");
                diag::detail(zz);
            }
        }
        // --- Der GPU-Weg -------------------------------------------------
        //
        // HIER und nicht weiter oben: an dieser Stelle sind Szene und
        // Effektnetz fertig aufgebaut. Vor rc400 stand der Aufruf vor dem
        // Block, und das Effektnetz existierte noch gar nicht.
        //
        if (gpuBereit) {
            const auto tGpu0 = std::chrono::steady_clock::now();
            const int gw = g_app->mapImage.width;
            const int gh = g_app->mapImage.height;
            gpu::setzeSchritt("Ziel anlegen");
            if (gpu::bereiteZiel(gw, gh)) {
                float vp[16];
                gpu::baueViewProj(useCam, gw, gh, 4.0F, kartenFern(), vp);
                // Die Kameraachsen fuer den Himmel - aus derselben Quelle
                // wie der Rasterer, sonst zeigt der Himmel woandershin.
                float gvorn[3];
                float grechts[3];
                float ghoch[3];
                Camera hc = useCam;
                hc.forward(gvorn);
                hc.right(grechts);
                hc.up(ghoch);
                const float gfokus =
                    1.0F / std::tan(useCam.fovY * 3.14159265F / 180.0F * 0.5F);
                std::string f;
                const float zeit = static_cast<float>(g_app->playMs) * 0.001F;
                // Alle vier werden jetzt ADDIERT (zwei Durchgaenge),
                // deshalb hier auf null - auch gpuAufrufe und
                // gpuUebersprungen, die vorher zugewiesen wurden.
                g_app->gpuKarte = g_app->gpuMover = 0;
                g_app->gpuFiguren2 = g_app->gpuGluehen = 0;
                g_app->gpuEffekte = 0;
                g_app->gpuAufrufe = 0;
                g_app->gpuUebersprungen = 0;
                // Die Klammer um das ganze Bild. Sie muss VOR der ersten
                // Abschnittsmarke stehen; ohne sie wird gar nicht gemessen.
                gpu::bildBeginnt();

                // --- Das Bild laeuft ZWEIMAL ueber alle Quellen ---------
                //
                // Erst alles Deckende, dann alles Gemischte. Vorher lief es
                // einmal, Quelle fuer Quelle: Karte ganz, dann Mover ganz.
                // Jede Liste war IN SICH richtig sortiert - und trotzdem
                // standen die gemischten Kartenflaechen vor den deckenden
                // Tueren. Das war der Fehler "Tueren verschwinden hinter
                // durchsichtigen Flaechen".
                //
                // Die Engine hat ihn nicht, weil sie gar nicht nach Quellen
                // zeichnet: R_AddDrawSurf legt Welt, Mover und Figuren in
                // EINE Liste mit einem Sortierschluessel aus der
                // Sortierstufe des Shaders, und R_RadixSort sortiert einmal
                // ueber alles (tr_main.cpp). Das hier ist derselbe
                // Schluessel mit zwei Stufen statt acht.
                //
                // Die Figuren laufen nur im ERSTEN Durchgang: der GPU-Weg
                // zeichnet sie durchweg deckend (holeBlend(Blend::Opaque)),
                // es gibt bei ihnen nichts zu teilen.
                std::vector<gpu::KlingenQuad> klingen;
                for (int durchgang = 0; durchgang < 2; ++durchgang) {
                    const gpu::Lage lage = (durchgang == 0)
                                               ? gpu::Lage::Deckend
                                               : gpu::Lage::Gemischt;
                    const bool erst = (durchgang == 0);

                    gpu::beginneMessung(erst ? gpu::Abschnitt::KarteDeckend
                                             : gpu::Abschnitt::KarteGemischt);
                    const int nk = gpu::zeichneKarte(
                        g_app->mesh, &g_app->textures, &g_app->geo, vp,
                        useCam.pos, zeit, g_app->mapBrightness,
                        g_app->mapMinLight, 0, gvorn, grechts, ghoch, gfokus,
                        &f, &g_app->gpuUebersprungen, lage);
                    g_app->gpuAufrufe += nk;
                    g_app->gpuKarte += nk;
                    gpu::beendeMessung(erst ? gpu::Abschnitt::KarteDeckend
                                            : gpu::Abschnitt::KarteGemischt);

                    // Das Effektnetz als ZWEITES Netz - es loescht nicht und
                    // trifft damit auf die Tiefe der Karte.
                    if (!g_app->effectMesh.indexes.empty()) {
                        gpu::beginneMessung(
                            erst ? gpu::Abschnitt::EffekteDeckend
                                 : gpu::Abschnitt::EffekteGemischt);
                        std::string f2;
                        const int ne = gpu::zeichneKarte(
                            g_app->effectMesh, &g_app->textures, &g_app->geo,
                            vp, useCam.pos, zeit, g_app->mapBrightness,
                            g_app->mapMinLight, 1, gvorn, grechts, ghoch,
                            gfokus, &f2, &g_app->gpuUebersprungen, lage);
                        g_app->gpuAufrufe += ne;
                        g_app->gpuEffekte += ne;
                        gpu::beendeMessung(
                            erst ? gpu::Abschnitt::EffekteDeckend
                                 : gpu::Abschnitt::EffekteGemischt);
                        if (f.empty()) { f = f2; }
                    }

                    // Die Mover: Tueren, Plattformen, Schiffe. `movers`
                    // enthaelt AUCH das Effektnetz und die Figuren - die
                    // haben kein eigenes Netz aus brushMeshes und sind hier
                    // schon gezeichnet.
                    gpu::setzeSchritt("Mover");
                    gpu::beginneMessung(erst ? gpu::Abschnitt::MoverDeckend
                                             : gpu::Abschnitt::MoverGemischt);
                    for (const MoverDraw& mv : movers) {
                        if (mv.mesh == nullptr || mv.mesh->indexes.empty() ||
                            mv.mesh == &g_app->effectMesh) {
                            continue;
                        }
                        float mw[16];
                        // Dieselbe Bedingung wie der Rasterer
                        // (mapview.cpp:1254): getaumelt wird nur, wenn Nick
                        // oder Roll ungleich null sind.
                        const bool taumelt =
                            (mv.pitch != 0.0F || mv.roll != 0.0F);
                        gpu::baueMoverWelt(mv.pivot, mv.offset, mv.yaw,
                                           mv.pitch, mv.roll, taumelt, mw, mv.scale);
                        std::string fm;
                        const int nm = gpu::zeichneMover(
                            *mv.mesh, &g_app->textures, &g_app->geo, vp, mw,
                            useCam.pos, gvorn, grechts, ghoch, gfokus, zeit,
                            g_app->mapBrightness, g_app->mapMinLight, &fm,
                            &g_app->gpuUebersprungen, lage);
                        g_app->gpuAufrufe += nm;
                        g_app->gpuMover += nm;
                        if (f.empty()) { f = fm; }
                    }
                    gpu::beendeMessung(erst ? gpu::Abschnitt::MoverDeckend
                                            : gpu::Abschnitt::MoverGemischt);

                    if (!erst || g_app->gpuAufrufe <= 0) {
                        continue;
                    }
                    // --- Die Figuren, im deckenden Durchgang -----------
                    gpu::setzeSchritt("Figuren vorbereiten");
                    gpu::beginneMessung(gpu::Abschnitt::Figuren);
                    std::vector<BoneMatrix> welt;
                    std::vector<BoneMatrix> hilf;
                    std::vector<BoneMatrix> skin;
                    // --- Was liegt auf den Figuren? --------------------
                    //
                    // Einmal je Bild neu, damit der Bericht den Stand von
                    // eben zeigt und nicht den vom Laden.
                    g_app->figurArten.clear();
                    for (const ActorDraw& a : gpuFiguren) {
                        if (a.model == nullptr) {
                            continue;
                        }
                        skin.clear();
                        // DIESELBE Funktion wie der Rasterer
                        // (mapview.cpp, figurKnochen). Vorher stand hier
                        // ein blosses worldMatrices(a.frame, welt) - ohne
                        // Vorrang von Ober- und Unterkoerper, ohne Mund,
                        // ohne Mischung zwischen zwei Bildern, ohne
                        // Uebergang zwischen zwei Animationen und ohne
                        // Kopfwinkel. Das ergab eine falsche Haltung und
                        // ein Springen, wo der Rasterer weich blendet.
                        if (figurKnochen(a, welt, hilf)) {
                            gpu::setzeSchritt("Knochenmatrizen");
                            gpu::baueKnochenmatrizen(welt, a.anim->bones, skin);
                        }
                        // --- ZUSAMMENFASSEN, nicht aufzaehlen -----------
                        //
                        // Die erste Fassung schrieb eine Zeile je Flaeche und
                        // brach bei 60 ab. Ein einziger Battledroid hat 26
                        // Flaechen - drei davon haben die Liste gefuellt, und
                        // die Hologramme auf dem Tisch, wegen derer sie
                        // gebaut wurde, standen gar nicht drin.
                        //
                        // Interessant ist nicht die einzelne Flaeche, sondern
                        // die KOMBINATION aus Shader, Skin und Mischart. Von
                        // denen gibt es in einer Karte ein Dutzend.
                        if (a.textures != nullptr) {
                            for (std::size_t si = 0;
                                 si < a.model->surfaces.size(); ++si) {
                                const GlmSurface& sf = a.model->surfaces[si];
                                if (!sf.isVisible(false)) { continue; }
                                const char* misch = "deckend";
                                bool ohneShader = true;
                                if (si < a.textures->bySurface.size()) {
                                    const TextureSet::Tex& tx =
                                        a.textures->bySurface[si];
                                    ohneShader = tx.empty();
                                    switch (tx.blend) {
                                        case BlendMode::Add: misch = "additiv"; break;
                                        case BlendMode::Alpha: misch = "alpha"; break;
                                        case BlendMode::AddAlpha: misch = "additiv-alpha"; break;
                                        case BlendMode::Filter: misch = "filter"; break;
                                        default: break;
                                    }
                                }
                                const std::string zeile =
                                    "shader=\"" + sf.shader + "\"  skin=\"" +
                                    sf.texture + "\"  " + misch +
                                    (ohneShader ? "  OHNE TEXTUR" : "");
                                ++g_app->figurArten[zeile];
                            }
                        }
                        float fw[16];
                        gpu::baueFigurWelt(a.yaw + a.yawOffset, a.pos, fw);
                        // Das Licht an ihrem Standort (Gitter + Effekt-
                        // lichter), mal Helligkeit und auf 0..1 - wie im
                        // Rasterer (renderActors).
                        gpu::FigurLicht licht;
                        if (a.light.ok) {
                            licht.gitter = true;
                            for (int k = 0; k < 3; ++k) {
                                licht.umgebung[k] =
                                    a.light.ambient[k] * g_app->mapBrightness / 255.0F;
                                licht.gerichtet[k] =
                                    a.light.directed[k] * g_app->mapBrightness / 255.0F;
                                licht.richtung[k] = a.light.dir[k];
                            }
                        }
                        std::string ff;
                        const int n = gpu::zeichneFigur(
                            *a.model, a.textures, skin, fw, vp, &ff, &licht,
                            g_app->showCaps);
                        g_app->gpuAufrufe += n;
                        g_app->gpuFiguren2 += n;
                        if (n == 0 && f.empty()) { f = ff; }

                        // --- Was die Figur in den Haenden haelt ----------
                        //
                        // Waffe, Schwertgriff(e) und Kinomodelle haengen
                        // starr an einem Bolzen der Figur - "*r_hand" oder
                        // "*l_hand" (g_client.cpp:1628 f.). Klinge N beginnt
                        // am Bolzen "*bladeN" des Griffs und laeuft in dessen
                        // NEGATIVE x-Richtung (CG_AddSaberBladeGo,
                        // cg_players.cpp:13767 ff.).
                        if (a.anim != nullptr && !welt.empty()) {
                            for (int ai2 = 0; ai2 < a.anbauten; ++ai2) {
                                const AnbauDraw& an =
                                    a.anbau[static_cast<std::size_t>(ai2)];
                                if (an.model == nullptr) {
                                    continue;
                                }
                                BoneMatrix hand{};
                                // Fehlt der Bolzen, haengt die Engine NICHTS an
                                // ("if (bolt_num == -1) return",
                                // wp_saber.cpp:748) - also auch hier nicht.
                                const int handSurf = surfaceIndex(*a.model, an.bolzen);
                                if (!boltMatrix(*a.model, handSurf, welt, *a.anim, hand)) {
                                    continue;
                                }
                                float gw2[16];
                                gpu::baueGriffWelt(fw, hand, gw2);
                                static const std::vector<BoneMatrix> kStarr;
                                std::string fh;
                                const int nh = gpu::zeichneFigur(
                                    *an.model, an.textures, kStarr, gw2, vp, &fh,
                                    &licht, g_app->showCaps);
                                g_app->gpuAufrufe += nh;
                                g_app->gpuFiguren2 += nh;
                                // In die Welt: dieselbe 4x4 wie der Griff.
                                const auto nachWelt = [&gw2](const float in[3], float o[3]) {
                                    for (int r = 0; r < 3; ++r) {
                                        o[r] = gw2[r * 4 + 0] * in[0] + gw2[r * 4 + 1] * in[1] +
                                               gw2[r * 4 + 2] * in[2] + gw2[r * 4 + 3];
                                    }
                                };
                                for (int kn = 0; kn < an.klingen; ++kn) {
                                    const KlingeDraw& kd =
                                        an.klinge[static_cast<std::size_t>(kn)];
                                    float wl[3];
                                    float tl[3];
                                    if (!klingenStrecke(*an.model, an.lage, kd.nummer,
                                                        kd.laenge, wl, tl)) {
                                        continue;
                                    }
                                    float wA[3];
                                    float wB[3];
                                    nachWelt(wl, wA);
                                    nachWelt(tl, wB);
                                    // Erst der breite Glanz in der Klingen-
                                    // farbe, dann der schmale, fast weisse
                                    // Kern (Radius / 3) - CG_DoSaber.
                                    const float radien[2] = {kd.radius, kd.radius / 3.0F};
                                    for (int lage2 = 0; lage2 < 2; ++lage2) {
                                        gpu::KlingenQuad q;
                                        gpu::baueKlingenband(wA, wB, useCam.pos, grechts,
                                                             radien[lage2], q.ecke);
                                        for (int k = 0; k < 3; ++k) {
                                            q.farbe[k] = (lage2 == 0)
                                                             ? static_cast<float>(kd.farbe[k])
                                                             : static_cast<float>(std::min(
                                                                   255, 190 + kd.farbe[k] / 4));
                                        }
                                        q.tex = (lage2 == 0) ? kd.glowTex : kd.coreTex;
                                        klingen.push_back(q);
                                    }
                                }
                            }
                        }
                    }
                    gpu::beendeMessung(gpu::Abschnitt::Figuren);
                }
                // --- Die Klingen nach allem anderen --------------------
                //
                // Durchscheinend und additiv: sie kommen nach der Karte,
                // den Movern und den Figuren, damit sie ueber allem liegen,
                // das sie nicht verdeckt - wie im Rasterer.
                if (!klingen.empty()) {
                    std::string fk;
                    const int nk2 = gpu::zeichneKlingen(klingen, vp, &fk);
                    g_app->gpuAufrufe += nk2;
                    g_app->gpuFiguren2 += nk2;
                    if (nk2 == 0 && f.empty()) { f = fk; }
                }
                // --- Das Gluehen, ganz zuletzt -------------------------
                if (g_app->gpuAufrufe > 0 && g_app->showGlow &&
                    g_app->textures.found > 0) {
                    gpu::beginneMessung(gpu::Abschnitt::Gluehen);
                    std::string fg;
                    g_app->gpuGluehen = gpu::zeichneGluehen(
                                        g_app->mesh, &g_app->textures,
                                        &g_app->geo, vp, zeit,
                                        320, 240,
                                        true, &fg);
                    gpu::beendeMessung(gpu::Abschnitt::Gluehen);
                    if (f.empty()) {
                        f = fg;
                    }
                }
                // Schliesst das Bild. Danach darf keine Marke mehr kommen.
                gpu::bildFertig();
                gezeichnet = true;
                // --- Die Debugschicht abfragen -------------------------
                //
                // Sie laeuft seit rc425 und schreibt an den
                // Windows-Debugger - gelesen hat ihre Warteschlange bis
                // rc447 niemand. Jede Beanstandung von Direct3D war damit
                // unsichtbar, obwohl sie erzeugt wurde.
                //
                // Gleiche Meldungen kommen nur einmal durch (siehe
                // gpu::debugMeldungen), also kostet das Sammeln nichts.
                for (const std::string& m : gpu::debugMeldungen()) {
                    diag::detail("Direct3D: " + m);
                    if (g_app->d3dMeldungen.size() < 200U) {
                        g_app->d3dMeldungen.push_back(m);
                    }
                }
                g_app->gpuFehler = f;
                if (g_app->gpuAufrufe <= 0 && g_app->gpuFehler.empty()) {
                    g_app->gpuFehler = "kein Zeichenaufruf abgesetzt";
                }
            } else {
                g_app->gpuFehler = "Renderziel nicht anlegbar";
            }
            // NICHT leeren: dann schweigt die Marke genau dann, wenn der
            // Absturz nach einem gelungenen Bild kommt - und das war beim
            // zweiten Versuch der Fall. Ein Wort, das "fertig" sagt, ist
            // eine Auskunft; ein leeres ist keine.
            gpu::setzeSchritt("Bild fertig, ausserhalb des GPU-Wegs");
            const float ms = static_cast<float>(
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - tGpu0).count());
            // Die Anzeige "N fps" kommt aus mapFrameMs, und das setzt sonst
            // nur der Rasterer - beim GPU-Weg staende dort die Zahl des
            // letzten Rasterbildes.
            g_app->mapFrameMs = (g_app->mapFrameMs <= 0.0F)
                                    ? ms
                                    : g_app->mapFrameMs * 0.9F + ms * 0.1F;
            g_app->frameCost.rawMap = ms;
        }
        // --- Auf dem GPU-Weg: dieselbe Ueberlagerung als eigenes Bild ------
        //
        // Gefunden beim Durchsehen aller Kartenfunktionen (27.09.): auf dem
        // GPU-Weg - shanks Einstellung - fehlten Entitykreuze, Kamera,
        // Kamerabahn, Schluessel, Gizmos, Figurenwege und die Blende ganz.
        // drawCameraGizmos schreibt in mapImage.rgba, und das fuellt dort
        // niemand; es brach sofort ab. Damit wurden auch die Klickziele
        // (markers, keyMarks, actorMarks, cameraSx) nie gesetzt - Anklicken
        // und Gizmos gingen ins Leere.
        //
        // Jetzt zeichnet es in ein durchsichtiges Bild derselben Groesse,
        // das ueber das GPU-Bild gelegt wird. Verdeckt wird wie beim
        // Rasterer: die Tiefe kommt aus dem GPU-Tiefenpuffer zurueck.
        if (gpuBereit && g_app->gpuAufrufe > 0) {
            MapImage& ov = g_app->gizmoBild;
            ov.width = g_app->mapImage.width;
            ov.height = g_app->mapImage.height;
            const std::size_t n = static_cast<std::size_t>(ov.width) *
                                  static_cast<std::size_t>(ov.height);
            ov.rgba.assign(n * 4U, 0);
            const bool gizmos = !g_app->camViewActive && !playingNow;
            if (!gizmos || !gpu::leseTiefe(ov.depth, ov.width, ov.height, 4.0F, kartenFern())) {
                ov.depth.clear();   // ohne Tiefe: alles obenauf
            }
            if (blende) {
                // Balken und Vollbild-Blende aus dem Kamerastand, wie im
                // Spiel: nach ENABLE blenden die Balken in einer Sekunde ein,
                // nach DISABLE aus; FADE ueberdeckt alles (CGCam_DrawWideScreen).
                // Beim Blick auf eine einzelne Einstellung (Auswahl) - oder in
                // einem Skript ganz ohne ENABLE - volle Balken wie bisher.
                float balken = 1.0F;
                float vollblende[4] = {0, 0, 0, 0};
                const bool zeitleiste = playingNow || (g_app->camViewActive && g_app->selectedPath.empty());
                bool hatEnable = false;
                for (const CamSegment& sg : g_app->camTrack.segments) {
                    if (sg.kind == CamSegment::Kind::Enable) { hatEnable = true; break; }
                }
                if (zeitleiste && hatEnable) {
                    const CameraState cs = g_app->camTrack.at(g_app->playMs);
                    balken = cs.bars;
                    for (int k = 0; k < 4; ++k) { vollblende[k] = cs.fadeColor[k]; }
                }
                drawLetterbox(ov, balken);
                drawVollblende(ov, vollblende);
            }
            if (gizmos) {
                drawCameraGizmos(ov);
            }
            g_app->gizmoZeigen = true;
            if (g_app->gizmoTexture == nullptr || g_app->gizmoTexW != ov.width ||
                g_app->gizmoTexH != ov.height) {
                g_app->gizmoTexture = render::createTexture(ov.rgba.data(), ov.width, ov.height);
                g_app->gizmoTexW = ov.width;
                g_app->gizmoTexH = ov.height;
            } else {
                render::updateTexture(g_app->gizmoTexture, ov.rgba.data(), ov.width, ov.height);
            }
        } else {
            g_app->gizmoZeigen = false;
        }
        // Stand das Netz noch nicht bereit, im naechsten Bild wieder
        // versuchen. Ohne Grafikkarte gibt es nichts zu versuchen.
        g_app->mapDirty = gpu::verfuegbar() && !gpuBereit;
    }

    if (ImGui::GetIO().Framerate > 0.0F) {
        g_app->frameCost.frame = 1000.0F / ImGui::GetIO().Framerate;
    }
    // --- Jedes wirklich gezeichnete Bild notieren ------------------------
    //
    // Nur wenn dieses Bild neu gezeichnet wurde.
    // Im Stillstand passiert nichts, und genau das war der Fehler des
    // frueheren Knopfes: er mass den Leerlauf.
    if (g_app->logFrameCost && gezeichnet) {
        ++g_app->frameCostTick;
        if (g_app->frameCostTick % 20 == 0) {
            const App::FrameCost& k = g_app->frameCost;
            char z[300];
            std::snprintf(z, sizeof(z),
                          "Zeit je Bild #%d: Karte %.1f, Figuren %.1f, "
                          "Effekte %.1f, Szene %.1f, Gluehen %.1f, "
                          "Gizmos %.1f, gemessen %.1f, "
                          "Bild wirklich %.1f ms | ROH: Karte %.1f, "
                          "gemessen %.1f",
                          g_app->frameCostTick,
                          static_cast<double>(k.map),
                          static_cast<double>(k.actors),
                          static_cast<double>(k.effects),
                          static_cast<double>(k.scene),
                          static_cast<double>(k.glow),
                          static_cast<double>(k.gizmos),
                          static_cast<double>(k.total),
                          static_cast<double>(k.frame),
                          static_cast<double>(k.rawMap),
                          static_cast<double>(k.rawTotal));
            diag::info(z);
        }
    }
    // Das Renderziel selbst wird gezeigt - es wurde direkt bemalt und muss
    // nicht erst hochgeladen werden.
    void* zeigeTextur = nullptr;
    if (gpu::zielTextur() != nullptr && g_app->gpuAufrufe > 0) {
        zeigeTextur = gpu::zielTextur();
    }
    if (!gpu::verfuegbar()) {
        // Ohne Direct3D 11 (OpenGL-Rueckfall oder ein Bau ohne D3D) gibt es
        // keine Kartenansicht mehr - der Software-Rasterer ist entfernt.
        // Sagen statt schwarz lassen.
        const ImVec2 hier = ImGui::GetCursorPos();
        ImGui::Dummy(ImVec2{static_cast<float>(w), static_cast<float>(h)});
        ImGui::SetCursorPos(ImVec2{hier.x + 12.0F, hier.y + 12.0F});
        ImGui::TextWrapped("%s", tr(Str::MapNeedsD3d));
    }
    if (zeigeTextur != nullptr) {
        const ImVec2 topLeft = ImGui::GetCursorScreenPos();
        const ImVec2 imgTopLeft = ImGui::GetCursorScreenPos();
        ImGui::Image(reinterpret_cast<ImTextureID>(zeigeTextur),
                     ImVec2{static_cast<float>(w), static_cast<float>(h)});
        // Die Ueberlagerung des GPU-Wegs obenauf (siehe oben).
        if (g_app->gizmoZeigen && g_app->gizmoTexture != nullptr) {
            ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(g_app->gizmoTexture),
                                                 ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }
        {
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            g_app->kartenAnsicht[0] = a.x;
            g_app->kartenAnsicht[1] = a.y;
            g_app->kartenAnsicht[2] = b.x;
            g_app->kartenAnsicht[3] = b.y;
        }
        // --- Namen an Entities und Figuren -------------------------------
        if (g_app->zeigeNamen) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 a = ImGui::GetItemRectMin();
            dl->PushClipRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true);
            const auto schrift = [&](float x, float y, const std::string& text, ImU32 farbe) {
                const ImVec2 p{a.x + x + 6.0F, a.y + y - ImGui::GetFontSize() * 0.5F};
                dl->AddText(ImVec2{p.x + 1.0F, p.y + 1.0F}, IM_COL32(0, 0, 0, 200), text.c_str());
                dl->AddText(p, farbe, text.c_str());
            };
            if (g_app->showEntities) {
                for (const App::PickedMarker& m : g_app->markers) {
                    if (m.entity >= g_app->map.entities.size()) {
                        continue;
                    }
                    const MapEntity& e = g_app->map.entities[m.entity];
                    const std::string name = entitySkriptName(e);
                    const int kat = entityKategorie(e);
                    const EntityKat& ek = entityKategorieInfo(std::max(kat, 0));
                    schrift(m.sx, m.sy, name.empty() ? e.classname : name, IM_COL32(ek.r, ek.g, ek.b, 230));
                }
            }
            for (const App::NamensMarke& f : g_app->figurNamen) {
                schrift(f.sx, f.sy, f.name, IM_COL32(255, 255, 255, 240));
            }
            dl->PopClipRect();
        }

        // --- target_print: der Text in der Bildmitte --------------------
        //
        // CG_DrawCenterString (cg_text.cpp:695): im 640x480-Raster
        // waagrecht mittig, die Zeilen um y = 480 * 0.25 gestapelt
        // (CG_CenterPrint_f), weiss, Schrift "ergoec" mit pointSize 20 mal
        // cg_textprintscale 0.5 - also 10 von 480 Zeilen hoch. 3 s zu
        // sehen, die letzten 200 ms blendet er aus.
        //
        // Waehrend einer Skriptkamera zeichnet die Engine ihn NICHT
        // (CG_Draw2D: in_camera -> frueher Ausstieg, cg_draw.cpp:9451);
        // endet die Kamera vor Ablauf der drei Sekunden, erscheint der Rest.
        // Und nur, was der Spieler ausgeloest hat, kommt ueberhaupt an
        // (MoverSim::bildschirmtextAt beachtet das schon).
        //
        // OFFEN: "@REFERENZ"-Texte bleiben roh - behaved liest die
        // StringPackages (strings/*.str) nicht; die Engine zeigt dann
        // ebenfalls den rohen Text, nur mit Fehlermeldung.
        if (const Bildschirmtext* bt = g_app->moverSim.bildschirmtextAt(g_app->playMs)) {
            const CameraState cs = g_app->camTrack.at(g_app->playMs);
            const float deck = bildschirmtextDeckkraft(*bt, g_app->playMs);
            if (!cs.enabled && deck > 0.0F) {
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                const float breite = b.x - a.x;
                const float hoehe = b.y - a.y;
                const float zeile = std::max(10.0F, hoehe * 10.0F / 480.0F);
                std::vector<std::string> zeilen;
                {
                    std::string rest = bt->text;
                    for (;;) {
                        const std::size_t nl = rest.find('\n');
                        zeilen.push_back(rest.substr(0, nl));
                        if (nl == std::string::npos) { break; }
                        rest = rest.substr(nl + 1);
                    }
                }
                ImFont* schrift = ImGui::GetFont();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const auto alpha = static_cast<int>(deck * 255.0F);
                float y = a.y + hoehe * 0.25F - static_cast<float>(zeilen.size()) * zeile * 0.5F;
                for (const std::string& z : zeilen) {
                    const float tw = schrift->CalcTextSizeA(zeile, 1.0e9F, 0.0F, z.c_str()).x;
                    const float x = a.x + (breite - tw) * 0.5F;
                    // Ohne Schatten: CG_DrawCenterString gibt kein
                    // STYLE_DROPSHADOW mit - Weiss auf hellem Grund ist im
                    // Spiel genauso schlecht zu lesen.
                    dl->AddText(schrift, zeile, ImVec2{x, y}, IM_COL32(255, 255, 255, alpha), z.c_str());
                    y += zeile;
                }
            }
        }

        // --- Anklicken einer Entity ------------------------------------
        //
        // SomaZ' BSP-Entity-Edit macht das ueber einen Nebenpuffer mit
        // Kennungen (ogl_fbo.py): jede Entity wird in einer eigenen Farbe
        // gezeichnet, beim Klick liest man den Bildpunkt.
        //
        // Unser Zeichner ist Software, da geht es einfacher: die Marken
        // merken sich beim Zeichnen ihren Bildschirmort, und der Klick nimmt
        // die naechstgelegene. Das ist zudem angenehmer zu treffen - eine
        // Marke ist ein duennes Kreuz, ein Nebenpuffer verlangt einen
        // Treffer auf den Strich.
        // --- Das Gizmo anfassen und ziehen ----------------------------
        //
        // Vor der uebrigen Klickbehandlung, denn ein Griff am Gizmo soll
        // NICHT zugleich die Auswahl aendern.
        //
        // Das Umrechnen Mausweg -> Weltweg geht ueber die Achse selbst: sie
        // liegt als Strecke im Bild vor (Anfang und Ende gemerkt), und ihre
        // Laenge in Welteinheiten ist bekannt. Der Mausweg wird auf die
        // Bildrichtung der Achse PROJIZIERT und im selben Verhaeltnis
        // umgerechnet. Damit stimmt die Bewegung aus jedem Blickwinkel, und
        // eine Achse, die fast auf den Betrachter zeigt, bewegt sich von
        // selbst kaum - genau richtig, denn dort kann man nicht zielen.
        {
            const ImVec2 m2 = ImGui::GetIO().MousePos;
            // --- Maus in RASTERkoordinaten ---------------------------
            //
            // Das Renderziel und das angezeigte Bild koennen verschieden gross
            // sein (Fensterkante, 64-Punkte-Untergrenze). Alles, was der Zeichner an
            // Bildschirmlagen liefert - Marker, Gizmoachsen - steht dann in
            // der kleineren Aufloesung, die Maus aber in der grossen.
            //
            // Ohne diese Umrechnung greift man waehrend einer Fahrt daneben,
            // und zwar genau um den Faktor.
            const float rasterSkal =
                (w > 0) ? static_cast<float>(g_app->mapImage.width) /
                              static_cast<float>(w)
                        : 1.0F;
            const float mx2 = (m2.x - imgTopLeft.x) * rasterSkal;
            const float my2 = (m2.y - imgTopLeft.y) * rasterSkal;
            // Ueber welcher Achse steht die Maus? Nur zum Aufleuchten.
            //
            // Ohne das raet man, ob man trifft - gemeldet als "wenn ich bei
            // der Kamera ueber die Linien hover sollten sie aufleuchten das
            // man weiss man ist genau drueber".
            auto abstandZurAchse = [&](int ax) {
                if (g_app->gizmoSx[ax][1] < 0.0F ||
                    g_app->gizmoSx[ax][0] < 0.0F) {
                    return 1.0e30F;
                }
                const float x0 = g_app->gizmoSx[ax][0];
                const float y0 = g_app->gizmoSy[ax][0];
                const float vx = g_app->gizmoSx[ax][1] - x0;
                const float vy = g_app->gizmoSy[ax][1] - y0;
                const float vv = vx * vx + vy * vy;
                float t = 0.0F;
                if (vv > 0.0F) {
                    t = ((mx2 - x0) * vx + (my2 - y0) * vy) / vv;
                    t = std::clamp(t, 0.0F, 1.0F);
                }
                const float dx = x0 + vx * t - mx2;
                const float dy = y0 + vy * t - my2;
                return dx * dx + dy * dy;
            };
            // Der Abstand zum RING einer Achse - kuerzester Abstand zu
            // irgendeinem seiner Teilstuecke. Der Drehmodus zeichnet Ringe
            // und keine Achsenstrecken; der Test muss messen, was zu sehen
            // ist. Vorher mass er gegen die (unsichtbaren, veralteten)
            // Strecken des Verschiebens - deshalb "schwer greifbar".
            auto abstandZumRing = [&](int ax) {
                float best = 1.0e30F;
                for (int t = 0; t + 1 < App::kRingPts; ++t) {
                    const float x0 = g_app->gizmoRingSx[ax][t];
                    const float y0 = g_app->gizmoRingSy[ax][t];
                    const float x1 = g_app->gizmoRingSx[ax][t + 1];
                    const float y1 = g_app->gizmoRingSy[ax][t + 1];
                    if (x0 < 0.0F || x1 < 0.0F) {
                        continue;
                    }
                    const float vx = x1 - x0;
                    const float vy = y1 - y0;
                    const float vv = vx * vx + vy * vy;
                    float tt = 0.0F;
                    if (vv > 0.0F) {
                        tt = ((mx2 - x0) * vx + (my2 - y0) * vy) / vv;
                        tt = std::clamp(tt, 0.0F, 1.0F);
                    }
                    const float dx = x0 + vx * tt - mx2;
                    const float dy = y0 + vy * tt - my2;
                    const float d = dx * dx + dy * dy;
                    if (d < best) {
                        best = d;
                    }
                }
                return best;
            };
            const float reach2 = ImGui::GetFontSize() * 1.1F;
            if (g_app->gizmoAxis < 0) {
                // Was vorher galt - fuer den Vergleich unten.
                const int vorher = g_app->gizmoHover;
                g_app->gizmoHover = -1;
                float best2 = reach2 * reach2;
                const bool dreht =
                    (g_app->gizmoMode == App::GizmoMode::Rotate);
                // Das Kaestchen in der Mitte zuerst - es liegt oben. Nur
                // im Verschieben: der Drehmodus hat keines.
                if (!dreht && g_app->gizmoShown &&
                    g_app->gizmoSx[0][0] >= 0.0F) {
                    const float dx = g_app->gizmoSx[0][0] - mx2;
                    const float dy = g_app->gizmoSy[0][0] - my2;
                    const float mitte = ImGui::GetFontSize() * 0.7F;
                    if (dx * dx + dy * dy < mitte * mitte) {
                        g_app->gizmoHover = App::kGizmoFree;
                    }
                }
                if (g_app->gizmoShown && g_app->gizmoHover < 0) {
                    for (int ax = 0; ax < 3; ++ax) {
                        const float d = dreht ? abstandZumRing(ax)
                                              : abstandZurAchse(ax);
                        if (d < best2) {
                            best2 = d;
                            g_app->gizmoHover = ax;
                        }
                    }
                }
                // Hat sich etwas geaendert? Dann NEU ZEICHNEN.
                //
                // Das war der Grund, warum das Aufleuchten "inkonsistent"
                // wirkte: die Ansicht wird nur neu gezeichnet, wenn
                // mapDirty gesetzt ist. Ohne das blieb das Bild stehen -
                // die Achse leuchtete erst auf, wenn zufaellig etwas
                // anderes ein Neuzeichnen ausloeste, und blieb dann
                // leuchten, wenn man wegfuhr.
                if (g_app->gizmoHover != vorher) {
                    g_app->mapDirty = true;
                }
            }

            if (g_app->gizmoMode == App::GizmoMode::Move &&
                g_app->gizmoAxis < 0 && g_app->gizmoShown &&
                ImGui::IsItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                g_app->gizmoHover >= 0) {
g_app->gizmoAxis = g_app->gizmoHover;
                g_app->gizmoDragSx = mx2;
                g_app->gizmoDragSy = my2;
                for (int k = 0; k < 3; ++k) {
                    g_app->gizmoStart[k] = g_app->gizmoOffset[k];
                }
                // Den Bezug EINFRIEREN - siehe app_internal.h.
                if (g_app->gizmoAxis < App::kGizmoFree) {
                    const int ax = g_app->gizmoAxis;
                    for (int e = 0; e < 2; ++e) {
                        g_app->gizmoFixSx[e] = g_app->gizmoSx[ax][e];
                        g_app->gizmoFixSy[e] = g_app->gizmoSy[ax][e];
                    }
                    g_app->gizmoFixLen = g_app->gizmoWorldLen;
                    // Auch die RICHTUNG einfrieren: im lokalen System zeigt
                    // die Achse nicht mehr entlang X, Y oder Z.
                    for (int a = 0; a < 3; ++a) {
                        for (int k = 0; k < 3; ++k) {
                            g_app->gizmoAxisDir[a][k] = g_app->gizmoDirNow[a][k];
                        }
                    }
                } else {
                    // Freie Bewegung: die Bildebene der Kamera festhalten.
                    g_app->cam.right(g_app->gizmoFreeRight);
                    g_app->cam.up(g_app->gizmoFreeUp);
                    // Wie viele Welteinheiten sind ein Bildpunkt? Von der
                    // Achse ablesen, die im Bild am LAENGSTEN ist - die ist
                    // am wenigsten verkuerzt und damit am verlaesslichsten.
                    float beste = 0.0F;
                    for (int ax = 0; ax < 3; ++ax) {
                        if (g_app->gizmoSx[ax][1] < 0.0F) { continue; }
                        const float dx =
                            g_app->gizmoSx[ax][1] - g_app->gizmoSx[ax][0];
                        const float dy =
                            g_app->gizmoSy[ax][1] - g_app->gizmoSy[ax][0];
                        const float l = std::sqrt(dx * dx + dy * dy);
                        if (l > beste) { beste = l; }
                    }
                    g_app->gizmoFreeScale =
                        (beste > 1.0F) ? (g_app->gizmoWorldLen / beste) : 0.0F;
                }
            }
            // --- Drehen ---------------------------------------------------
            //
            // Waagerecht ziehen dreht: 180 Bildpunkte sind 90 Grad. Welche
            // Achse, sagt derselbe Ueberfahr-Test wie beim Verschieben -
            // die Ringe liegen dort, wo die Achsen laegen.
            if (g_app->gizmoMode == App::GizmoMode::Rotate) {
                if (!g_app->gizmoRotating && g_app->gizmoShown &&
                    ImGui::IsItemHovered() &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                    g_app->gizmoHover >= 0 &&
                    g_app->gizmoHover < App::kGizmoFree) {
    g_app->gizmoRotating = true;
                    g_app->gizmoAxis = g_app->gizmoHover;
                    g_app->gizmoRotStartSx = mx2;
                    g_app->gizmoRotStartSy = my2;
                    for (int k = 0; k < 3; ++k) {
                        g_app->gizmoRotStart[k] = g_app->gizmoAngles[k];
                    }
                    // Achsen und Ausgangswinkel EINFRIEREN - dasselbe
                    // Prinzip wie beim Verschieben (rc156). Im Weltsystem
                    // wandert die Kameraachse waehrend der Drehung; wuerde
                    // man jedes Bild neu ablesen, koppelte das zurueck.
                    for (int k = 0; k < 3; ++k) {
                        g_app->gizmoAngBase[k] = g_app->gizmoAngNow[k];
                    }
                    for (int a = 0; a < 3; ++a) {
                        for (int k = 0; k < 3; ++k) {
                            g_app->gizmoAxisDir[a][k] = g_app->gizmoDirNow[a][k];
                        }
                    }
                }
                if (g_app->gizmoRotating) {
                    const float mdx2 = mx2 - g_app->gizmoRotStartSx;
                    const int ax = std::clamp(g_app->gizmoAxis, 0, 2);
                    for (int k = 0; k < 3; ++k) {
                        g_app->gizmoAngles[k] = g_app->gizmoRotStart[k];
                    }
                    const float grad = mdx2 * 0.5F;
                    if (g_app->gizmoRotSpace == App::GizmoSpace::Local) {
                        // PAN ist Pitch, Yaw, Roll - in dieser Reihenfolge,
                        // wie ueberall in Quake-Engines. Achse 0 ist rechts
                        // (Pitch), 1 ist oben (Yaw), 2 ist vorwaerts
                        // (Roll). Im lokalen System entspricht jede Achse
                        // GENAU einem dieser Werte - deshalb kommt man hier
                        // ohne Umrechnung aus.
                        g_app->gizmoAngles[ax] = g_app->gizmoRotStart[ax] + grad;
                    } else {
                        // Weltsystem: um X, Y oder Z der Karte drehen.
                        //
                        // Eine Weltdrehung laesst sich NICHT auf einen der
                        // drei Eulerwerte abbilden - sie aendert je nach
                        // Blickrichtung zwei oder drei davon. Also den Weg
                        // ueber die Orientierung nehmen: Basis bauen, um
                        // die Weltachse drehen, Winkel zurueckrechnen.
                        // anglestest haelt fest, dass dieser Rundlauf
                        // stimmt - samt Drehrichtung und Roll-Vorzeichen.
                        Camera basis;
                        for (int k = 0; k < 3; ++k) {
                            basis.angles[k] = g_app->gizmoAngBase[k] +
                                              g_app->gizmoRotStart[k];
                        }
                        float fB[3];
                        float uB[3];
                        basis.forward(fB);
                        basis.up(uB);
                        float fN[3];
                        float uN[3];
                        rotateAboutAxis(fB, g_app->gizmoAxisDir[ax], grad, fN);
                        rotateAboutAxis(uB, g_app->gizmoAxisDir[ax], grad, uN);
                        float neuW[3];
                        anglesFromBasis(fN, uN, neuW);
                        // gizmoAngles ist ein ZUSATZ zum Skriptwert, also
                        // die Differenz bilden.
                        for (int k = 0; k < 3; ++k) {
                            g_app->gizmoAngles[k] =
                                neuW[k] - g_app->gizmoAngBase[k];
                        }
                    }
                    g_app->mapDirty = true;
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        g_app->gizmoRotating = false;
                        g_app->gizmoAxis = -1;
                    }
                }
            }

            if (g_app->gizmoMode == App::GizmoMode::Move &&
                g_app->gizmoAxis >= 0) {
                const float mdx = mx2 - g_app->gizmoDragSx;
                const float mdy = my2 - g_app->gizmoDragSy;
                if (g_app->gizmoAxis == App::kGizmoFree) {
                    // In der Bildebene: rechts mal Mausweg nach rechts,
                    // oben mal Mausweg nach OBEN - der Bildschirm zaehlt
                    // nach unten, daher das Minus.
                    for (int k = 0; k < 3; ++k) {
                        g_app->gizmoOffset[k] =
                            g_app->gizmoStart[k] +
                            (g_app->gizmoFreeRight[k] * mdx -
                             g_app->gizmoFreeUp[k] * mdy) *
                                g_app->gizmoFreeScale;
                    }
                    g_app->mapDirty = true;
                } else {
                    const int ax = g_app->gizmoAxis;
                    const float sdx =
                        g_app->gizmoFixSx[1] - g_app->gizmoFixSx[0];
                    const float sdy =
                        g_app->gizmoFixSy[1] - g_app->gizmoFixSy[0];
                    const float slen2 = sdx * sdx + sdy * sdy;
                    if (slen2 > 1.0F && g_app->gizmoFixLen > 0.0F) {
                        const float t = (mdx * sdx + mdy * sdy) / slen2;
                        // Entlang der eingefrorenen Richtung. Im
                        // Weltsystem ist das der Einheitsvektor der Achse,
                        // also genau das bisherige gizmoOffset[ax] += ...
                        for (int k = 0; k < 3; ++k) {
                            g_app->gizmoOffset[k] =
                                g_app->gizmoStart[k] +
                                g_app->gizmoAxisDir[ax][k] * t *
                                    g_app->gizmoFixLen;
                        }
                        g_app->mapDirty = true;
                    }
                }
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    g_app->gizmoAxis = -1;
                }
            }
        }

        // Kein return: der Rest der Ansicht - Fadenkreuz, Zustandszeile -
        // soll weiter gezeichnet werden. Nur die Auswahl darf ein Griff am
        // Gizmo nicht aendern, und dafuer genuegt diese eine Bedingung.
        // --- Rechtsklick: die Betriebsart waehlen ---------------------
        //
        // Wie in 3ds Max, wo Auswaehlen, Verschieben und Drehen drei
        // getrennte Werkzeuge sind. Ohne diese Trennung ist jeder Klick in
        // die Naehe einer Achse ein Verschieben - und man kann nichts mehr
        // anklicken, ohne etwas zu bewegen.
        // Nur bei einem KURZEN Rechtsklick, nicht nach dem Drehen.
        //
        // Mit der rechten Maustaste dreht man die Kamera. BeginPopupContextItem
        // oeffnet aber beim LOSLASSEN - also auch nach einem langen Zug ueber
        // das halbe Bild. Gemeldet als: "wenn ich das gedrueckt halte und los
        // lasse oeffnet sich das rechtsklick menu".
        //
        // ImGui fuehrt mit, wie weit die Maus seit dem Druecken gewandert ist:
        // io.MouseDragMaxDistanceSqr. Ist das mehr als ein paar Bildpunkte,
        // war es ein Zug und kein Klick. Genau diese Groesse benutzt ImGui
        // selbst fuer seine Zugschwelle - sie steht in imgui.h und braucht
        // keine interne Kopfdatei.
        {
            const ImGuiIO& io2 = ImGui::GetIO();
            const float grenze = io2.MouseDragThreshold * io2.MouseDragThreshold;
            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
                io2.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] <= grenze) {
                ImGui::OpenPopup("gizmomodus");
            }
        }
        if (ImGui::BeginPopup("gizmomodus")) {
            if (ImGui::MenuItem(tr(Str::GizmoSelect), nullptr,
                                g_app->gizmoMode == App::GizmoMode::Select)) {
                g_app->gizmoMode = App::GizmoMode::Select;
                g_app->mapDirty = true;
            }
            if (ImGui::MenuItem(tr(Str::GizmoMove), nullptr,
                                g_app->gizmoMode == App::GizmoMode::Move)) {
                g_app->gizmoMode = App::GizmoMode::Move;
                // Ist eine Kamera ausgewaehlt, aber noch kein Schluessel,
                // gleich den ersten nehmen - sonst steht man im
                // Verschiebemodus ohne etwas zu verschieben.
                // Der Schluessel, der zur eingestellten ZEIT gilt - nicht
                // der erste. Vorher sprang die Kamera beim Umschalten auf
                // den ersten MOVE, egal wo die Zeitleiste stand.
                if (g_app->cameraSelected && g_app->selectedKey < 0) {
                    g_app->selectedKey = schluesselFuerZeit(g_app->playMs);
                    g_app->gizmoZeitBezug = g_app->playMs;
                }
                g_app->mapDirty = true;
            }
            // Drehen heisst, den PAN-Befehl zu aendern - das gehoert zum
            // Rueckschreiben und kommt damit zusammen. Der Eintrag steht
            // schon da, abgeblendet, damit klar ist, dass er vorgesehen ist.
            if (ImGui::MenuItem(tr(Str::GizmoRotate), nullptr,
                                g_app->gizmoMode == App::GizmoMode::Rotate)) {
                g_app->gizmoMode = App::GizmoMode::Rotate;
                // Der Schluessel, der zur eingestellten ZEIT gilt - nicht
                // der erste. Vorher sprang die Kamera beim Umschalten auf
                // den ersten MOVE, egal wo die Zeitleiste stand.
                if (g_app->cameraSelected && g_app->selectedKey < 0) {
                    g_app->selectedKey = schluesselFuerZeit(g_app->playMs);
                    g_app->gizmoZeitBezug = g_app->playMs;
                }
                g_app->mapDirty = true;
            }
            // --- Bezugssystem ------------------------------------------
            //
            // Wie die Liste "Reference Coordinate System" in 3ds Max, und
            // wie dort JE UMFORMUNG eigen: "If you change this pull down
            // list for move, that does not change it for rotate." Welche
            // gerade gilt, richtet sich nach dem eingestellten Modus.
            if (g_app->gizmoMode != App::GizmoMode::Select) {
                ImGui::Separator();
                const bool dreht = (g_app->gizmoMode == App::GizmoMode::Rotate);
                App::GizmoSpace& raum =
                    dreht ? g_app->gizmoRotSpace : g_app->gizmoMoveSpace;
                if (ImGui::MenuItem(tr(Str::GizmoSpaceWorld), nullptr,
                                    raum == App::GizmoSpace::World)) {
                    raum = App::GizmoSpace::World;
                    g_app->mapDirty = true;
                }
                if (ImGui::MenuItem(tr(Str::GizmoSpaceLocal), nullptr,
                                    raum == App::GizmoSpace::Local)) {
                    raum = App::GizmoSpace::Local;
                    g_app->mapDirty = true;
                }
            }
            ImGui::EndPopup();
        }

        if (g_app->gizmoAxis < 0 && ImGui::IsItemHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            // Siehe die Notiz weiter oben zur Rastergroesse.
            const float rasterSkal2 =
                (w > 0) ? static_cast<float>(g_app->mapImage.width) /
                              static_cast<float>(w)
                        : 1.0F;
            const float mx = (m.x - imgTopLeft.x) * rasterSkal2;
            const float my = (m.y - imgTopLeft.y) * rasterSkal2;
            const float reach = ImGui::GetFontSize() * 1.2F;
            float best = reach * reach;
            int found = -1;
            for (const App::PickedMarker& mk : g_app->markers) {
                const float dx = mk.sx - mx;
                const float dy = mk.sy - my;
                const float d = dx * dx + dy * dy;
                if (d < best) {
                    best = d;
                    found = static_cast<int>(mk.entity);
                }
            }
            // Die Kamera mitpruefen - sie ist nicht in markers, weil sie zu
            // keiner Entity gehoert.
            // Die Kamera trifft man grosszuegiger als eine Entity.
            //
            // Gemeldet als "irgendwie laesst sich die Kamera auch nicht so
            // gut selecten". Der Grund: sie wurde mit derselben engen
            // Reichweite geprueft wie ein Entity-Kreuz, obwohl ihr Rahmen
            // ein vielfach groesseres Gebilde ist. Jetzt zaehlt der ganze
            // Bereich, den das Gizmo einnimmt - "kann man die am ganzen
            // gizmo selecten" war genau der Vorschlag.
            bool traf = false;
            if (g_app->cameraSx >= 0.0F) {
                const float dx = g_app->cameraSx - mx;
                const float dy = g_app->cameraSy - my;
                const float weit = reach * 2.5F;
                if (dx * dx + dy * dy < weit * weit) {
                    traf = true;
                }
            }
            // Erst die Schluessel auf der Bahn - sie liegen oben.
            //
            // Sie sind nur da, wenn die Kamera ausgewaehlt ist; dann aber
            // haben sie Vorrang vor der Kamera selbst, weil man sie
            // gezielt trifft.
            int keyHit = -1;
            if (g_app->cameraSelected) {
                float bestKey = reach * reach;
                for (std::size_t ki = 0; ki < g_app->keyMarks.size(); ++ki) {
                    const float dx = g_app->keyMarks[ki].sx - mx;
                    const float dy = g_app->keyMarks[ki].sy - my;
                    const float d = dx * dx + dy * dy;
                    if (d < bestKey) {
                        bestKey = d;
                        keyHit = static_cast<int>(ki);
                    }
                }
            }
            // Welche FIGUR liegt unter der Maus? Aus den Marken, die das
            // Zeichnen hinterlassen hat. Die Kamera und ihre Schluessel
            // haben Vorrang - sie sind kleiner und schwerer zu treffen.
            int figHit = -1;
            {
                const float reichweite = ImGui::GetFontSize() * 1.6F;
                float bestF = reichweite * reichweite;
                for (const App::ActorMark& am : g_app->actorMarks) {
                    const float dx = am.sx - mx;
                    const float dy = am.sy - my;
                    const float d = dx * dx + dy * dy;
                    if (d < bestF) {
                        bestF = d;
                        figHit = am.index;
                    }
                }
            }

            if (keyHit >= 0) {
                g_app->selectedKey =
                    (g_app->selectedKey == keyHit) ? -1 : keyHit;
                g_app->gizmoZeitBezug = g_app->playMs;

                // BEIDE Vorschauen verwerfen. Vorher nur die Verschiebung -
                // eine angefangene Drehung wanderte zum naechsten Schluessel
                // mit und landete beim Schreiben dort.
                for (int k = 0; k < 3; ++k) {
                    g_app->gizmoOffset[k] = 0.0F;
                    g_app->gizmoAngles[k] = 0.0F;
                }
                g_app->gizmoAxis = -1;
                // Und die zugehoerige Skriptzeile mit auswaehlen - so
                // sieht man sofort, welcher Befehl gemeint ist.
                if (g_app->selectedKey >= 0) {
                    const Path& kp2 =
                        g_app->keyMarks[static_cast<std::size_t>(keyHit)].path;
                    selectByPath(kp2);
                }
            } else if (figHit >= 0) {
                // Eine FIGUR getroffen - sie anwaehlen, damit ihr Laufweg
                // erscheint. Nochmal dieselbe hebt die Auswahl auf.
                g_app->selectedActor =
                    (g_app->selectedActor == figHit) ? -1 : figHit;
                g_app->cameraSelected = false;
                g_app->gizmoShown = false;
                g_app->pickedEntity = -1;
                g_app->mapDirty = true;
            } else if (traf) {
                // Nochmal darauf klicken hebt die Auswahl wieder auf.
                g_app->cameraSelected = !g_app->cameraSelected;
                g_app->selectedActor = -1;   // entweder Kamera oder Figur
                g_app->pickedEntity = -1;
                // Ohne Kamera kein Gizmo - sonst faengt der Ueberfahr-Test
                // weiter unsichtbare Ringe und Achsen.
                if (!g_app->cameraSelected) {
                    g_app->gizmoShown = false;
                }
                // Beim Auswaehlen gleich den naechstgelegenen Schluessel
                // mitnehmen.
                //
                // Sonst brauchte es ZWEI Klicks, bis die Achsen erschienen:
                // einen fuer die Kamera, einen fuer den Schluessel. Genau
                // so gemeldet - "das passiert erst wenn ich 2 mal mit links
                // klick drauf druecke".
                //
                // Genommen wird der, der auf dem Bildschirm am naechsten
                // liegt: man hat ja gerade dorthin geklickt.
                g_app->selectedKey = -1;
                // Der Schluessel, der zur eingestellten ZEIT gilt. Vorher der
                // auf dem Bildschirm naechstgelegene - bei einer Bahn, die an
                // sich selbst vorbeilaeuft, oft ein ganz anderer.
                if (g_app->cameraSelected) {
                    g_app->selectedKey = schluesselFuerZeit(g_app->playMs);
                    g_app->gizmoZeitBezug = g_app->playMs;
                    const std::vector<KameraSchluessel> ks = kameraSchluessel(g_app->doc.script());
                    if (g_app->selectedKey >= 0 &&
                        static_cast<std::size_t>(g_app->selectedKey) < ks.size()) {
                        const Path& kp2 = ks[static_cast<std::size_t>(g_app->selectedKey)].path;
                        if (!kp2.empty()) {
                            g_app->selectedPath = kp2;
                            g_app->selection.clear();
                            g_app->selection.push_back(kp2);
                            rebuildTree();
                            g_app->scrollToSelected = true;
                        }
                    }
                }
                for (int k = 0; k < 3; ++k) {
                    g_app->gizmoAngles[k] = 0.0F;
                }
                for (int k = 0; k < 3; ++k) {
                    g_app->gizmoOffset[k] = 0.0F;
                }
            } else {
                // Weder Kamera noch Schluessel noch Figur getroffen.
                //
                // Wurde auch keine Entity getroffen (found ist -1), war das
                // ein Klick INS LEERE - und der hebt alles auf. "wenn ich
                // etwas selected habe und dann daneben klicke kann es
                // abgewaehlt werden?" Ja, und zwar alles auf einmal: es
                // ist der natuerliche Weg, eine Auswahl loszuwerden, ohne
                // sich zu merken, was gerade woran haengt.
                //
                // Das Gizmo geht mit: ohne Auswahl faengt sein
                // Ueberfahr-Test sonst weiter unsichtbare Achsen und Ringe
                // (derselbe Fall wie in rc167).
                g_app->selectedKey = -1;
                g_app->cameraSelected = false;
                g_app->pickedEntity = found;
                if (found < 0) {
                    g_app->selectedActor = -1;
                    g_app->gizmoShown = false;
                    g_app->gizmoAxis = -1;
                    for (int k = 0; k < 3; ++k) {
                        g_app->gizmoOffset[k] = 0.0F;
                        g_app->gizmoAngles[k] = 0.0F;
                    }
                }
            }
            g_app->mapDirty = true;
        }

        // --- Fadenkreuz in der Bildmitte ------------------------------
        //
        // Beim Setzen einer Kamera sieht man sonst nicht genau, wohin sie
        // zeigt: die Mitte des Bildes IST die Blickrichtung, aber ohne
        // Marke schaetzt man sie.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 mid{topLeft.x + static_cast<float>(w) * 0.5F,
                         topLeft.y + static_cast<float>(h) * 0.5F};
        const float arm = ImGui::GetFontSize() * 0.55F;
        const float gap = arm * 0.35F;
        const ImU32 col = IM_COL32(255, 210, 60, 200);
        const ImU32 shadow = IM_COL32(0, 0, 0, 140);
        for (int pass = 0; pass < 2; ++pass) {
            // Erst schwarz mit Versatz, dann farbig - so bleibt das Kreuz
            // auf hellem wie auf dunklem Grund sichtbar.
            const float o = (pass == 0) ? 1.0F : 0.0F;
            const ImU32 c = (pass == 0) ? shadow : col;
            dl->AddLine(ImVec2{mid.x - arm + o, mid.y + o},
                        ImVec2{mid.x - gap + o, mid.y + o}, c, 1.0F);
            dl->AddLine(ImVec2{mid.x + gap + o, mid.y + o},
                        ImVec2{mid.x + arm + o, mid.y + o}, c, 1.0F);
            dl->AddLine(ImVec2{mid.x + o, mid.y - arm + o},
                        ImVec2{mid.x + o, mid.y - gap + o}, c, 1.0F);
            dl->AddLine(ImVec2{mid.x + o, mid.y + gap + o},
                        ImVec2{mid.x + o, mid.y + arm + o}, c, 1.0F);
        }

        // --- Name der Entity unter dem Zeiger -------------------------
        //
        // 977 Zielnamen sind zu viele, um sie alle einzublenden. Der unter
        // dem Zeiger genuegt - und der ist genau der, den man gerade in ein
        // SET_NAVGOAL schreiben will.
        if (g_app->showEntities && !g_app->camViewActive && ImGui::IsItemHovered()) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            const MapEntity* best = nullptr;
            float bestDist = 18.0F;   // Fangbereich in Bildpunkten

            float fwd[3];
            float rgt[3];
            float upv[3];
            g_app->cam.forward(fwd);
            g_app->cam.right(rgt);
            g_app->cam.up(upv);
            const float focal =
                1.0F / std::tan(g_app->cam.fovY * 3.14159265F / 360.0F);
            const float halfW = static_cast<float>(w) * 0.5F;
            const float halfH = static_cast<float>(h) * 0.5F;

            for (const MapEntity& e : g_app->map.entities) {
                if (e.origin.empty() || e.targetname.empty()) {
                    continue;
                }
                float p[3] = {0, 0, 0};
                std::istringstream is(e.origin);
                is >> p[0] >> p[1] >> p[2];
                const float d[3] = {p[0] - g_app->cam.pos[0], p[1] - g_app->cam.pos[1],
                                    p[2] - g_app->cam.pos[2]};
                const float z = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2];
                if (z < 1.0F) {
                    continue;
                }
                const float sx = topLeft.x + halfW +
                                 (d[0] * rgt[0] + d[1] * rgt[1] + d[2] * rgt[2]) *
                                     focal / z * halfH;
                const float sy = topLeft.y + halfH -
                                 (d[0] * upv[0] + d[1] * upv[1] + d[2] * upv[2]) *
                                     focal / z * halfH;
                const float dist = std::sqrt((sx - m.x) * (sx - m.x) +
                                             (sy - m.y) * (sy - m.y));
                if (dist < bestDist) {
                    bestDist = dist;
                    best = &e;
                }
            }
            if (best != nullptr) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(best->targetname.c_str());
                ImGui::TextDisabled("%s", best->classname.c_str());
                ImGui::TextDisabled("%s", best->origin.c_str());
                ImGui::EndTooltip();
            }
        }
    }

    // --- Die Seitenleiste, RECHTS neben dem Bild -------------------------
    //
    // SameLine setzt sie neben das Bild; die Breite ist oben schon vom Bild
    // abgezogen worden. Ein eigenes Kindfenster, damit sie fuer sich rollen
    // kann - bei kleinem Fenster passen die Einstellungen sonst nicht.
    ImGui::SameLine();
    drawMapSidebarColumn(g_app->mapSidebarW, static_cast<float>(h));
}

// Die Bedienung der Karte - UNTER allen Spalten, ueber die volle Breite.
//
// Sie stand in der Kartenspalte und musste sich deren Breite teilen. Im
// Band ist reichlich Platz, und die Ansicht darueber verliert nur die
// Hoehe, die wirklich gebraucht wird - genauso wie beim Modell.
// Die Einstellungen der Ansicht - in einer Spalte am rechten Rand.
//
// Sie standen als drei Zeilen Kaestchen UNTER dem Bild. Das kostete
// dauerhaft drei Zeilen Hoehe - genau den Platz, den die Zeitleiste
// braucht. Blender loest das mit einer Seitenleiste am rechten Rand des
// Ansichtsfensters, die sich mit N ein- und ausklappen laesst; hier
// dasselbe, mit einem Knopf statt einer Taste.
//
// Die Reihenfolge ist die alte geblieben, nur untereinander statt
// nebeneinander: wer sie gewohnt ist, findet alles am selben Platz in der
// Liste.
void drawMapSidebar() {
    ImGui::BeginDisabled(g_app->geo.empty());
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0F);
    if (ImGui::SliderFloat(tr(Str::MapBrightness), &g_app->mapBrightness,
                           0.5F, 6.0F, "%.1fx")) {
        g_app->mapDirty = true;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0F);
    if (ImGui::SliderFloat(tr(Str::MapMinLight), &g_app->mapMinLight,
                           0.0F, 0.5F, "%.2f")) {
        g_app->mapDirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::MapMinLightHint));
    }
    // Bildrate: die Zeit fuers Zeichnen, nicht die des ganzen Fensters -
    // ImGui zeichnet auch dann, wenn die Karte unveraendert bleibt.
    if (g_app->mapFrameMs > 0.0F) {
        ImGui::TextDisabled(tr(Str::MapFps), 1000.0F / g_app->mapFrameMs);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tr(Str::MapFrameMs), static_cast<double>(g_app->mapFrameMs));
        }
        }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0F);
    if (ImGui::SliderInt(tr(Str::MapDetail), &g_app->meshDetail, 1, 10)) {
        // Die Welt ist Untermodell 0 - OHNE die beweglichen Teile.
        //
        // buildMesh(geo) enthaelt sie mit. Wer sie zusaetzlich bewegt
        // zeichnet, hat sie doppelt: einmal am gebauten Platz, einmal am
        // aktuellen. In Ruhe faellt das nicht auf, weil beide Kopien
        // deckungsgleich liegen - beim ersten Verschieben steht der
        // Doppelgaenger da. Eine Probe hat es gefangen.
        //
        // Genau so trennt es die Engine: Welt ist Modell 0, alles andere
        // haengt an einer Entity (R_LoadSubmodels).
        g_app->mapBereit = false;
        g_app->mesh = g_app->geo.models.empty()
                          ? buildMesh(g_app->geo, g_app->meshDetail, !g_app->showSky)
                          : buildModelMesh(g_app->geo, 0, g_app->meshDetail,
                                           !g_app->showSky);
        g_app->mapBereit = !g_app->mesh.batches.empty();
        // Die Netze der bewegten Teile stammen aus derselben Karte und
        // derselben Feinheit - wechselt eines davon, sind sie ungueltig.
        g_app->brushMeshes.clear();
        gpu::vergissMoverNetze();   // die Puffer hingen an den alten Netzen
        g_app->mapDirty = true;
    }
    if (ImGui::Checkbox(tr(Str::MapNames), &g_app->zeigeNamen)) {
        g_app->mapDirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::MapNamesHint));
    }
    if (ImGui::Checkbox(tr(Str::MapEntities), &g_app->showEntities)) {
        g_app->mapDirty = true;
    }
    // Der Hinweis gehoert HIERHER. Er stand hinter "Sky" und ueberschrieb
    // dort dessen eigenen - wer ueber Sky zeigte, las den Entities-Text,
    // und Entities selbst hatte keinen (Kartentest 27.09.).
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::MapEntitiesHint));
    }
    if (ImGui::Checkbox(tr(Str::MapEffects), &g_app->showEffects)) {
        g_app->mapDirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::MapEffectsHint));
    }
    if (ImGui::Checkbox(tr(Str::MapGlow), &g_app->showGlow)) {
        g_app->mapDirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::MapGlowHint));
    }
    if (g_app->showGlow) {
        // Wie viele Bildpunkte im Gluehpuffer standen. Null heisst: nichts
        // im Bild glueht, und dann ist das Bild unveraendert. Die Zahl statt
        // des Eindrucks - sonst dreht man am Schalter und schaut, was besser
        // aussieht.
        ImGui::SameLine();
        // Die Zahl der Zeichenaufrufe des Gluehdurchgangs - die Zahl der
        // Bildpunkte kennt die Grafikkarte nicht ohne Rueckuebertragung.
        ImGui::TextDisabled("(%d Aufrufe)", g_app->gpuGluehen);
    }
    if (ImGui::Checkbox(tr(Str::MapActors), &g_app->showActors)) {
        g_app->mapDirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::MapActorsHint));
    }
    if (g_app->showActors) {
        // Ein Knopf, der 0 -> 90 -> 180 -> 270 durchschaltet. Diagnose, wie
        // "Beidseitig" beim Boden: erscheinen die Figuren erst bei einem
        // dieser Werte richtig, ist die Frage entschieden und der Wert kann
        // fest in den Zeichner.
            char lbl[64];
        std::snprintf(lbl, sizeof(lbl), tr(Str::ActorYaw), g_app->actorYaw);
        if (ImGui::SmallButton(lbl)) {
            g_app->actorYaw = (g_app->actorYaw + 90) % 360;
            g_app->mapDirty = true;
        }
    }
    if (g_app->showActors && !g_app->scene.actors.empty()) {
        int withModel = 0;
        for (const App::ActorAssets& a : g_app->actorAssets) {
            if (!a.model.empty()) { ++withModel; }
        }
            if (withModel == 0) {
            ImGui::TextDisabled("%s", tr(Str::ActorsNoModels));
        } else {
            ImGui::TextDisabled(tr(Str::ActorsFound), withModel,
                                static_cast<int>(g_app->scene.actors.size()));
        }
    }
    if (ImGui::Checkbox(tr(Str::MapThroughCam), &g_app->throughCamera)) {
        g_app->mapDirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::MapThroughCamHint));
    }
    if (g_app->throughCamera) {
            if (g_app->camViewActive) {
            ImGui::TextDisabled(tr(Str::MapCamInfo), g_app->camViewLabel.c_str(),
                                static_cast<double>(g_app->camViewFov));
        } else {
            ImGui::TextDisabled("%s", tr(Str::MapCamNone));
        }
    }
    if (ImGui::Checkbox(tr(Str::MapSky), &g_app->showSky)) {
        // Die Welt ist Untermodell 0 - OHNE die beweglichen Teile.
        //
        // buildMesh(geo) enthaelt sie mit. Wer sie zusaetzlich bewegt
        // zeichnet, hat sie doppelt: einmal am gebauten Platz, einmal am
        // aktuellen. In Ruhe faellt das nicht auf, weil beide Kopien
        // deckungsgleich liegen - beim ersten Verschieben steht der
        // Doppelgaenger da. Eine Probe hat es gefangen.
        //
        // Genau so trennt es die Engine: Welt ist Modell 0, alles andere
        // haengt an einer Entity (R_LoadSubmodels).
        g_app->mapBereit = false;
        g_app->mesh = g_app->geo.models.empty()
                          ? buildMesh(g_app->geo, g_app->meshDetail, !g_app->showSky)
                          : buildModelMesh(g_app->geo, 0, g_app->meshDetail,
                                           !g_app->showSky);
        g_app->mapBereit = !g_app->mesh.batches.empty();
        // Die Netze der bewegten Teile stammen aus derselben Karte und
        // derselben Feinheit - wechselt eines davon, sind sie ungueltig.
        g_app->brushMeshes.clear();
        gpu::vergissMoverNetze();   // die Puffer hingen an den alten Netzen
        g_app->mapDirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::MapSkyHint));
    }
    if (g_app->textures.found + g_app->textures.missing != 0) {
            ImGui::TextDisabled(tr(Str::MapTexFound), g_app->textures.found,
                            g_app->textures.found + g_app->textures.missing);
    } else if (g_app->gamePaths.empty()) {
            ImGui::TextDisabled("%s", tr(Str::MapTexNone));
    }
    ImGui::TextDisabled(tr(Str::MapTriangles),
                        static_cast<int>(g_app->mesh.indexes.size() / 3),
                        static_cast<int>(g_app->mesh.batches.size()));
    // --- Die Zahlen des GPU-Wegs, wenn er laeuft --------------------------
    //
    // Sie standen im View-Menue, und das ist beim Hinsehen zu. Hier stehen
    // sie neben den Dreiecken, wo man ohnehin hinschaut.
    //
    // "uebersprungen" ist die wichtige Zahl: ein Stapel ohne Textur oder
    // ohne Shader wird still ausgelassen, und genau so verschwindet eine
    // halbe Tuer, ohne dass es irgendwo auffaellt.
    {
        ImGui::TextDisabled("GPU: %d Aufrufe, %d uebersprungen",
                            g_app->gpuAufrufe, g_app->gpuUebersprungen);
        // Welche Zustaende aus rc434 laufen? Steuerbar ueber
        // BHED_D3D_STATES. Ein Bild ohne diese Zeile kostet eine Runde.
        ImGui::TextDisabled("%s", gpu::zustandsLage());
        // --- Die Aufstellung, mit der Zeit der Grafikkarte daneben -------
        //
        // Die Zeit kommt von der Karte selbst (D3D11_QUERY_TIMESTAMP). Sie
        // ist zwei bis drei Bilder alt - das ist der Preis dafuer, dass
        // niemand auf sie wartet. Steht keine da, hat das Geraet keine
        // Zeitmarken oder es liegt noch kein vollstaendiges Bild vor.
        //
        // Je Quelle sind es ZWEI Messungen, deckend und gemischt, weil das
        // Bild seit rc432 zweimal ueber alle Quellen laeuft. Angezeigt wird
        // die Summe.
        const struct {
            const char* name;
            int aufrufe;
            gpu::Abschnitt a;
            gpu::Abschnitt b;
        } zeilen[] = {
            {"Karte",   g_app->gpuKarte,    gpu::Abschnitt::KarteDeckend,
                                            gpu::Abschnitt::KarteGemischt},
            {"Effekte", g_app->gpuEffekte,  gpu::Abschnitt::EffekteDeckend,
                                            gpu::Abschnitt::EffekteGemischt},
            {"Mover",   g_app->gpuMover,    gpu::Abschnitt::MoverDeckend,
                                            gpu::Abschnitt::MoverGemischt},
            {"Figuren", g_app->gpuFiguren2, gpu::Abschnitt::Figuren,
                                            gpu::Abschnitt::Figuren},
            {"Gluehen", g_app->gpuGluehen,  gpu::Abschnitt::Gluehen,
                                            gpu::Abschnitt::Gluehen},
        };
        for (const auto& z : zeilen) {
            const float ms = (z.a == z.b) ? gpu::millisekunden(z.a)
                                          : gpu::millisekundenBeide(z.a, z.b);
            if (ms >= 0.0F) {
                ImGui::TextDisabled("  %-8s %5d Aufrufe  %6.2f ms", z.name,
                                    z.aufrufe, static_cast<double>(ms));
            } else {
                ImGui::TextDisabled("  %-8s %5d Aufrufe", z.name, z.aufrufe);
            }
        }
        if (!g_app->gpuFehler.empty()) {
            // Umbrechen statt abschneiden: die Meldung war laenger als die
            // Leiste breit ist, und die abgeschnittene Haelfte war gerade
            // die mit der Auskunft.
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextDisabled("GPU: %s", g_app->gpuFehler.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::EndDisabled();
}

void drawMapPanel() {
    // Ohne Karte steht alles trotzdem da, nur ohne Wirkung - wie bei der
    // Modellleiste. Sonst springt die Anordnung beim Laden.
    ImGui::BeginDisabled(g_app->geo.empty());
    // --- Bedienung --------------------------------------------------------
    ImGui::Text(tr(Str::MapPos), static_cast<int>(g_app->cam.pos[0]),
                static_cast<int>(g_app->cam.pos[1]), static_cast<int>(g_app->cam.pos[2]));
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::MapInsertCamera))) {
        insertCameraHere();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(Str::MapFly));
    }
    ImGui::EndDisabled();

    // Was das Gizmo verschoben hat - in Zahlen.
    //
    // Ohne sie ist eine Verschiebung nicht nachvollziehbar: man sieht, dass
    // sich etwas bewegt hat, aber nicht um wie viel. Und die Zeile sagt
    // ausdruecklich, dass noch nichts im Skript steht - solange das so ist,
    // darf darueber kein Zweifel bestehen.
    const bool verschoben =
        g_app->gizmoOffset[0] != 0.0F || g_app->gizmoOffset[1] != 0.0F ||
        g_app->gizmoOffset[2] != 0.0F;
    const bool gedreht =
        g_app->gizmoAngles[0] != 0.0F || g_app->gizmoAngles[1] != 0.0F ||
        g_app->gizmoAngles[2] != 0.0F;
    // Die Zeile bekommt IMMER ihren Platz.
    //
    // Vorher erschien und verschwand sie mit der Vorschau, und alles
    // darunter - Hinweiszeile, Abspielknoepfe, Zeitleiste - sprang um eine
    // Zeilenhoehe mit. Gemeldet als "kann man das smoother machen".
    //
    // Ein Platzhalter derselben Hoehe kostet nichts und haelt die
    // Anordnung ruhig; dieselbe Ueberlegung wie beim Klappfeld der
    // geteilten Ansicht in rc165.
    // Gizmo-Meldungen stehen seit 27.09. in DERSELBEN Zeile wie Koordinate
    // und "Insert camera here" (vorher hielt eine eigene Zeile ihnen dauernd
    // Platz frei - die fehlte der Zeitleiste). Springen kann so nichts: die
    // Zeile ist ohnehin da.
    const float zeilenAbstand = ImGui::GetFontSize() * 1.5F;

    // Und die Bestaetigung nach dem Schreiben BLEIBT kurz stehen und geht
    // dann weich aus, statt im selben Bild zu verschwinden. Erst damit
    // sieht man ueberhaupt, dass etwas passiert ist.
    if (!gedreht && !verschoben && g_app->gizmoDoneFade > 0.0F) {
        g_app->gizmoDoneFade -= ImGui::GetIO().DeltaTime;
        // Erst STEHEN, dann ausgehen. Bei 4,5 Sekunden Vorrat und 1,2
        // Sekunden Ausblenden heisst das: gut drei Sekunden voll lesbar,
        // danach weich weg. Vorher waren es 2,2 mit 0,8 - der Text war
        // fort, bevor man ihn gelesen hatte.
        //
        // Das Ausblenden allein zu verlaengern haette nicht gereicht: ein
        // Text, der die halbe Zeit halb durchsichtig ist, liest sich
        // schlechter als einer, der erst steht.
        const float a = std::clamp(g_app->gizmoDoneFade / 1.2F, 0.0F, 1.0F);
        ImGui::SameLine(0.0F, zeilenAbstand);
        ImGui::TextColored(ImVec4{0.55F, 0.90F, 0.60F, a},
                           "%s", tr(Str::GizmoWroteShort));
        g_app->mapDirty = true;   // damit das Ausblenden auch laeuft
    }

    if (gedreht) {
        ImGui::SameLine(0.0F, zeilenAbstand);
        ImGui::TextColored(ImVec4{1.0F, 0.82F, 0.25F, 1.0F},
                           tr(Str::GizmoTurned),
                           static_cast<double>(g_app->gizmoAngles[0]),
                           static_cast<double>(g_app->gizmoAngles[1]),
                           static_cast<double>(g_app->gizmoAngles[2]));
        ImGui::SameLine();
        if (ImGui::SmallButton(tr(Str::GizmoApply))) {
            if (gizmoWriteAngles()) {
                setStatus(tr(Str::GizmoWrote), 0, 0);
                // 2,2 Sekunden stehen, davon die letzten 0,8 ausblendend.
                g_app->gizmoDoneFade = 4.5F;
            } else {
                setStatus(tr(Str::GizmoNoMove), 0, 0);
            }
            g_app->mapDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr(Str::GizmoReset))) {
            for (int k = 0; k < 3; ++k) {
                g_app->gizmoAngles[k] = 0.0F;
            }
            g_app->mapDirty = true;
        }
    }
    if (verschoben) {
        ImGui::SameLine(0.0F, zeilenAbstand);
        ImGui::TextColored(ImVec4{1.0F, 0.82F, 0.25F, 1.0F},
                           tr(Str::GizmoMoved),
                           static_cast<double>(g_app->gizmoOffset[0]),
                           static_cast<double>(g_app->gizmoOffset[1]),
                           static_cast<double>(g_app->gizmoOffset[2]));
        ImGui::SameLine();
        // Ins Skript schreiben - das macht aus der Vorschau eine Aenderung.
        //
        // Absichtlich ein eigener Knopf und nicht das Loslassen der Maus:
        // beim Ziehen probiert man herum, und jede Zwischenstellung im
        // Rueckgaengig-Speicher zu haben waere laestig. So steht am Ende
        // EIN Schritt darin.
        if (ImGui::SmallButton(tr(Str::GizmoApply))) {
            if (gizmoWriteBack()) {
                setStatus(tr(Str::GizmoWrote), 0, 0);
                g_app->gizmoDoneFade = 4.5F;
            } else {
                setStatus(tr(Str::GizmoNoMove), 0, 0);
            }
            g_app->mapDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr(Str::GizmoReset))) {
            for (int k = 0; k < 3; ++k) {
                g_app->gizmoOffset[k] = 0.0F;
            }
            g_app->mapDirty = true;
        }
    }
}

}  // namespace bhed::gui
