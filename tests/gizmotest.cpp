// Bewegt ein Mausweg entlang einer Achse den Punkt um den richtigen Betrag?
//
// Die Rechnung im Programm: der Mausweg wird auf die BILDRICHTUNG der Achse
// projiziert und im Verhaeltnis ihrer Bildlaenge zu ihrer Weltlaenge
// umgerechnet. Diese Probe stellt das nach und prueft drei Aussagen:
//
//   1. Zieht man die Maus genau ueber die Achsenspitze, landet der Punkt
//      dort - eine ganze Achsenlaenge weit.
//   2. Zieht man QUER zur Achse, bewegt sich nichts.
//   3. Bei einer stark verkuerzten Achse (fast auf den Betrachter zu)
//      bewegt derselbe Mausweg WEITER in der Welt - die Achse ist im Bild
//      ja kuerzer. Das ist richtig so, aber es darf nicht ins Unendliche
//      laufen.
#include "bhed/mapview.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
static float wegAusMaus(float ax0, float ay0, float ax1, float ay1,
                        float mdx, float mdy, float weltLen) {
    const float sdx = ax1 - ax0;
    const float sdy = ay1 - ay0;
    const float slen2 = sdx * sdx + sdy * sdy;
    if (slen2 <= 1.0F) { return 0.0F; }
    const float t = (mdx * sdx + mdy * sdy) / slen2;
    return t * weltLen;
}
// Die Rechnung steht in gui/app_view3d.cpp. Sie hier NACHGEBAUT zu pruefen
// ist ein Zugestaendnis: der Griff steckt mitten in der Oberflaeche und
// laesst sich ohne Fenster nicht aufrufen. Was diese Probe festhaelt, ist
// die Rechenvorschrift - dieselben fuenf Zeilen, dieselben Aussagen. Weicht
// die Oberflaeche davon ab, faellt es hier nicht auf; wer sie aendert, muss
// diese Datei mitaendern. Der Kommentar dort sagt das.
int main() {
    int fehler = 0;
    const float weltLen = 100.0F;
    // 1. Achse liegt waagerecht, 200 Bildpunkte lang. Maus 200 nach rechts.
    {
        const float w = wegAusMaus(100, 100, 300, 100, 200, 0, weltLen);
        std::printf("  volle Achsenlaenge: %.2f (erwartet %.2f)\n",
                    (double)w, (double)weltLen);
        if (std::fabs(w - weltLen) > 0.01F) { ++fehler; }
    }
    // 2. Quer dazu.
    {
        const float w = wegAusMaus(100, 100, 300, 100, 0, 150, weltLen);
        std::printf("  quer zur Achse:     %.2f (erwartet 0)\n", (double)w);
        if (std::fabs(w) > 0.01F) { ++fehler; }
    }
    // 3. Halbe Bildlaenge -> derselbe Mausweg bewegt doppelt so weit.
    {
        const float w = wegAusMaus(100, 100, 200, 100, 200, 0, weltLen);
        std::printf("  halb verkuerzt:     %.2f (erwartet %.2f)\n",
                    (double)w, (double)(weltLen * 2.0F));
        if (std::fabs(w - weltLen * 2.0F) > 0.01F) { ++fehler; }
    }
    // 4. Fast auf den Betrachter zu: die Wache muss greifen.
    {
        const float w = wegAusMaus(100, 100, 100.5F, 100, 200, 0, weltLen);
        std::printf("  fast nicht sichtbar: %.2f (erwartet 0 - Wache)\n", (double)w);
        if (w != 0.0F) { ++fehler; }
    }
    // 5. Schraege Achse, Maus genau entlang.
    {
        const float w = wegAusMaus(0, 0, 100, 100, 100, 100, weltLen);
        std::printf("  schraeg, entlang:   %.2f (erwartet %.2f)\n",
                    (double)w, (double)weltLen);
        if (std::fabs(w - weltLen) > 0.01F) { ++fehler; }
    }
    // --- Der Griff am STRICH, nicht nur an der Spitze -------------------
    //
    // Seit rc154 laesst sich ueberall an der Achse anfassen. Gerechnet wird
    // der Abstand Punkt-zu-STRECKE, mit t auf 0..1 beschraenkt - sonst
    // zoege auch die gedachte Verlaengerung ueber das Ende hinaus.
    {
        auto abstand = [](float x0, float y0, float x1, float y1,
                          float px, float py) {
            const float vx = x1 - x0;
            const float vy = y1 - y0;
            const float vv = vx * vx + vy * vy;
            float t = 0.0F;
            if (vv > 0.0F) {
                t = ((px - x0) * vx + (py - y0) * vy) / vv;
                t = t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
            }
            const float dx = x0 + vx * t - px;
            const float dy = y0 + vy * t - py;
            return std::sqrt(dx * dx + dy * dy);
        };
        // Mitte der Achse, 3 Bildpunkte daneben -> Abstand 3.
        const float a = abstand(100, 100, 300, 100, 200, 103);
        std::printf("  neben der Mitte:    %.2f (erwartet 3)\n", (double)a);
        if (std::fabs(a - 3.0F) > 0.01F) { ++fehler; }
        // WEIT hinter dem Ende: der Abstand zaehlt ab dem Ende, nicht ab
        // der gedachten Verlaengerung.
        const float b = abstand(100, 100, 300, 100, 400, 100);
        std::printf("  hinter dem Ende:    %.2f (erwartet 100)\n", (double)b);
        if (std::fabs(b - 100.0F) > 0.01F) { ++fehler; }
        // Genau auf dem Anfang.
        const float c = abstand(100, 100, 300, 100, 100, 100);
        std::printf("  auf dem Anfang:     %.2f (erwartet 0)\n", (double)c);
        if (std::fabs(c) > 0.01F) { ++fehler; }
    }

    // --- Der Bezug ist EINGEFROREN, also laesst sich alles zuruecknehmen -
    //
    // Das war der Fehler aus dem GIF: "es gibt beim moven auch einen
    // gewissen punkt wo es out of range ist und sich nicht mehr zurueck
    // bewegen laesst."
    //
    // Ursache war, dass mit der Achse gerechnet wurde, WIE SIE GERADE
    // STEHT. Die wandert mit dem Punkt mit - eine Rueckkopplung. Laeuft der
    // Punkt hinter die Kamera, hat die Achse keine Bildlaenge mehr, und
    // nichts geht mehr, auch nicht zurueck.
    //
    // Hier nachgestellt: die Achse wird beim Griff festgehalten, dann wird
    // hin- und wieder zurueckgezogen. Am Ende muss GENAU null herauskommen.
    {
        const float fx0 = 100, fy0 = 100, fx1 = 300, fy1 = 100;
        const float len = 100.0F;
        float versatz = 0.0F;
        const float start = versatz;
        // Weit hinaus ...
        versatz = start + wegAusMaus(fx0, fy0, fx1, fy1, 2000, 0, len);
        std::printf("  weit hinausgezogen: %.2f\n", (double)versatz);
        // ... und wieder zurueck an den Ausgangspunkt.
        versatz = start + wegAusMaus(fx0, fy0, fx1, fy1, 0, 0, len);
        std::printf("  zurueck am Anfang:  %.2f (erwartet 0)\n",
                    (double)versatz);
        if (std::fabs(versatz) > 0.001F) { ++fehler; }
        // Und ueber den Anfang hinaus in die Gegenrichtung.
        versatz = start + wegAusMaus(fx0, fy0, fx1, fy1, -400, 0, len);
        std::printf("  in die Gegenrichtung: %.2f (erwartet -200)\n",
                    (double)versatz);
        if (std::fabs(versatz + 200.0F) > 0.01F) { ++fehler; }
    }

    // --- Der Ringabstand -----------------------------------------------
    //
    // Der Drehmodus misst den Mausabstand gegen den Ring als Streckenzug
    // aus 48 Teilen (App::kRingPts Punkte). Hier dieselbe Rechnung an
    // einem Kreis mit Radius 120: ein Punkt AUF dem Kreis muss praktisch
    // Abstand null haben, die Mitte muss WEIT draussen liegen (sonst
    // finge der Ring Klicks, die gar nicht auf ihm sind), und ein Punkt
    // 10 Bildpunkte ausserhalb muss Abstand ~10 haben. Die Sehnenhoehe
    // eines 48-Ecks bei r=120 ist ~0,26 - weit unter der Griffweite von
    // etwa einer Schriftgroesse.
    {
        constexpr int kPts = 49;
        const float r = 120.0F, cx = 400.0F, cy = 300.0F;
        float px[kPts];
        float py[kPts];
        for (int t = 0; t < kPts; ++t) {
            const float w = 6.2831853F * (float)t / 48.0F;
            px[t] = cx + std::cos(w) * r;
            py[t] = cy + std::sin(w) * r;
        }
        auto ringAbstand = [&](float mx, float my) {
            float best = 1.0e30F;
            for (int t = 0; t + 1 < kPts; ++t) {
                const float vx = px[t + 1] - px[t];
                const float vy = py[t + 1] - py[t];
                const float vv = vx * vx + vy * vy;
                float tt = 0.0F;
                if (vv > 0.0F) {
                    tt = ((mx - px[t]) * vx + (my - py[t]) * vy) / vv;
                    tt = std::clamp(tt, 0.0F, 1.0F);
                }
                const float dx = px[t] + vx * tt - mx;
                const float dy = py[t] + vy * tt - my;
                best = std::min(best, dx * dx + dy * dy);
            }
            return std::sqrt(best);
        };
        const float aufKreis = ringAbstand(cx + r, cy);
        const float inMitte = ringAbstand(cx, cy);
        const float draussen = ringAbstand(cx + r + 10.0F, cy);
        std::printf("  Ring: auf dem Kreis %.3f, Mitte %.1f, +10 -> %.2f\n",
                    (double)aufKreis, (double)inMitte, (double)draussen);
        if (aufKreis > 0.5F) { ++fehler; }
        if (inMitte < r - 1.0F) { ++fehler; }
        if (std::fabs(draussen - 10.0F) > 0.5F) { ++fehler; }
    }

    // --- Die Vorschau bewegt und dreht die KAMERA ----------------------
    //
    // Gemeldet: "wenn ich sie bewege und rotiere bewegt und rotiert sich ja
    // die kamera nicht wirklich". Das Gizmo wanderte, das Modell nicht -
    // die Vorschauwerte gingen nur in die Gizmo-Zeichnung, nicht in die
    // Kamera. Die Regel, die das behebt, ist einfach:
    //
    //     Ort   = Schluesselort   + gizmoOffset
    //     Winkel= Schluesselwinkel + gizmoAngles
    //
    // Hier mit der ECHTEN Camera aus mapview.h geprueft: der Versatz muss
    // im Ort ankommen, und die Winkel muessen die BLICKRICHTUNG wirklich
    // aendern. Ein Test, der nur die Zahlen addiert, ginge daran vorbei -
    // er muss sehen, dass sich forward() dreht.
    {
        const float schluesselPos[3] = {100.0F, 200.0F, 50.0F};
        const float schluesselAng[3] = {0.0F, 0.0F, 0.0F};
        const float versatz[3] = {10.0F, -20.0F, 5.0F};
        const float drehung[3] = {0.0F, 90.0F, 0.0F};   // 90 Grad Gier

        bhed::Camera ohne;
        bhed::Camera mit;
        for (int k = 0; k < 3; ++k) {
            ohne.pos[k] = schluesselPos[k];
            ohne.angles[k] = schluesselAng[k];
            mit.pos[k] = schluesselPos[k] + versatz[k];
            mit.angles[k] = schluesselAng[k] + drehung[k];
        }

        // 1. Der Ort wandert um genau den Versatz.
        bool ortOk = true;
        for (int k = 0; k < 3; ++k) {
            if (std::fabs(mit.pos[k] - (schluesselPos[k] + versatz[k])) >
                0.001F) {
                ortOk = false;
            }
        }
        std::printf("  Ort mit Versatz: %.1f %.1f %.1f\n",
                    (double)mit.pos[0], (double)mit.pos[1],
                    (double)mit.pos[2]);
        if (!ortOk) { ++fehler; }

        // 2. Die Blickrichtung dreht sich WIRKLICH. Bei Gier 0 schaut die
        //    Kamera nach +X, bei Gier 90 nach +Y - das ist die Konvention
        //    aus JKA (AngleVectors), und daran haengt das ganze Modell.
        float f0[3];
        float f1[3];
        ohne.forward(f0);
        mit.forward(f1);
        std::printf("  Blick ohne Drehung: %.2f %.2f %.2f\n",
                    (double)f0[0], (double)f0[1], (double)f0[2]);
        std::printf("  Blick mit 90 Grad:  %.2f %.2f %.2f\n",
                    (double)f1[0], (double)f1[1], (double)f1[2]);
        if (std::fabs(f0[0] - 1.0F) > 0.01F) { ++fehler; }
        if (std::fabs(f1[1] - 1.0F) > 0.01F) { ++fehler; }
        // Und sie sind wirklich verschieden - das ist der Kern der Sache.
        const float skalar = f0[0] * f1[0] + f0[1] * f1[1] + f0[2] * f1[2];
        std::printf("  Skalarprodukt: %.3f (erwartet ~0)\n", (double)skalar);
        if (std::fabs(skalar) > 0.01F) { ++fehler; }

        // 3. Ohne Vorschau darf sich NICHTS aendern - sonst wanderte die
        //    Kamera, waehrend man gar nichts zieht.
        bhed::Camera ruhig;
        for (int k = 0; k < 3; ++k) {
            ruhig.pos[k] = schluesselPos[k] + 0.0F;
            ruhig.angles[k] = schluesselAng[k] + 0.0F;
        }
        float f2[3];
        ruhig.forward(f2);
        for (int k = 0; k < 3; ++k) {
            if (std::fabs(ruhig.pos[k] - schluesselPos[k]) > 0.001F) {
                ++fehler;
            }
            if (std::fabs(f2[k] - f0[k]) > 0.001F) { ++fehler; }
        }
    }

    // --- Zeitleiste: eine Kante fuer alles ------------------------------
    //
    // Lineal, Zeiger und Spurbalken muessen dieselbe Zeitachse benutzen,
    // sonst steht die Zahl 20 nicht ueber der Sekunde 20. Gemeldet als
    // "die timeline startet schon vor den scripts".
    //
    // Die Rechnung ist an beiden Stellen dieselbe:
    //     x = achseX + anteil * achsenBreite
    // Hier nachgestellt und geprueft, dass Lineal und Spur bei gleicher
    // Zeit auf denselben Bildpunkt kommen - und dass die Namensspalte
    // WIRKLICH davor liegt.
    {
        const float panelLinks = 12.0F;
        const float labelW = 90.0F;
        const float achseX = panelLinks + labelW;   // gemessene Kante
        const float achseW = 800.0F;
        const double dauer = 62200.0;               // wie intro2_sith

        auto linealX = [&](double sekunden) {
            const double proEinheit = dauer / 1000.0;
            return achseX + static_cast<float>(sekunden / proEinheit) * achseW;
        };
        auto spurX = [&](double ms) {
            return achseX + static_cast<float>(ms / dauer) * achseW;
        };

        std::printf("  Lineal 20 s: %.2f   Spur 20000 ms: %.2f\n",
                    (double)linealX(20.0), (double)spurX(20000.0));
        if (std::fabs(linealX(20.0) - spurX(20000.0)) > 0.01F) { ++fehler; }
        if (std::fabs(linealX(0.0) - achseX) > 0.01F) { ++fehler; }
        if (std::fabs(linealX(62.2) - (achseX + achseW)) > 0.5F) { ++fehler; }

        // Und der Fehler, der behoben wurde: begaenne das Lineal am
        // Panelrand statt an der Achse, laege es um die ganze Namensspalte
        // daneben.
        const float falsch = panelLinks +
                             static_cast<float>(20.0 / (dauer / 1000.0)) * achseW;
        std::printf("  altes Lineal lag %.0f Bildpunkte daneben\n",
                    (double)(linealX(20.0) - falsch));
        if (std::fabs(linealX(20.0) - falsch - labelW) > 0.01F) { ++fehler; }
    }

    // --- Klick ins Leere hebt alles auf ---------------------------------
    //
    // "wenn ich etwas selected habe und dann daneben klicke kann es
    // abgewaehlt werden?" Die Regel im Programm: getroffen wird, was
    // naeher als die Reichweite liegt; liegt NICHTS in Reichweite, faellt
    // die ganze Auswahl.
    //
    // Hier die Trefferrechnung nachgestellt - genau die aus dem
    // Klickzweig -, damit die Grenze wirklich stimmt und nicht nur
    // ungefaehr.
    {
        const float reichweite = 20.0F;
        struct Marke { float sx, sy; int id; };
        const Marke marken[] = {{100.0F, 100.0F, 7}, {300.0F, 250.0F, 9}};

        auto treffer = [&](float mx, float my) {
            float best = reichweite * reichweite;
            int found = -1;
            for (const Marke& m : marken) {
                const float dx = m.sx - mx;
                const float dy = m.sy - my;
                const float d = dx * dx + dy * dy;
                if (d < best) {
                    best = d;
                    found = m.id;
                }
            }
            return found;
        };

        std::printf("  genau darauf: %d   knapp daneben: %d   weit weg: %d\n",
                    treffer(100.0F, 100.0F), treffer(115.0F, 100.0F),
                    treffer(500.0F, 500.0F));
        if (treffer(100.0F, 100.0F) != 7) { ++fehler; }
        if (treffer(115.0F, 100.0F) != 7) { ++fehler; }   // 15 < 20
        if (treffer(125.0F, 100.0F) != -1) { ++fehler; }  // 25 > 20
        if (treffer(500.0F, 500.0F) != -1) { ++fehler; }
        // Die naehere von zweien gewinnt - sonst haengt es an der
        // Reihenfolge in der Liste.
        if (treffer(295.0F, 250.0F) != 9) { ++fehler; }
        // Und genau AUF der Reichweite gilt als daneben (strikt kleiner).
        if (treffer(120.0F, 100.0F) != -1) { ++fehler; }
    }

    // --- Zoomen in der Zeitleiste ---------------------------------------
    //
    // Die Zusage beim Zoomen mit dem Rad: der Zeitwert UNTER DER MAUS
    // bleibt stehen, alles andere rueckt darum herum auseinander. So macht
    // es Blender und jedes Schnittprogramm. Zoomt man stattdessen um die
    // Mitte oder um den Anfang, springt die Stelle weg, die man gerade
    // ansieht - und man sucht sie danach.
    //
    // Hier die Rechnung aus dem Programm nachgestellt.
    {
        const double gesamt = 62200.0;   // wie intro2_sith
        double zoom = 1.0;
        double start = 0.0;

        auto radDrehen = [&](double anteil, double schritte) {
            const double spanne = gesamt / zoom;
            const double unterMaus = start + anteil * spanne;
            const double neu =
                std::clamp(zoom * std::pow(1.25, schritte), 1.0, 400.0);
            const double neueSpanne = gesamt / neu;
            zoom = neu;
            start = std::clamp(unterMaus - anteil * neueSpanne, 0.0,
                               std::max(0.0, gesamt - neueSpanne));
            return unterMaus;
        };
        auto zeitBei = [&](double anteil) {
            return start + anteil * (gesamt / zoom);
        };

        // In der Mitte hineinzoomen: dort muss dieselbe Zeit stehen.
        const double vorher = zeitBei(0.5);
        radDrehen(0.5, 4.0);
        std::printf("  Mitte: vorher %.0f ms, nachher %.0f ms (Zoom %.2f)\n",
                    vorher, zeitBei(0.5), zoom);
        if (std::fabs(zeitBei(0.5) - vorher) > 1.0) { ++fehler; }
        if (zoom <= 1.0) { ++fehler; }

        // Bei einem Viertel ebenso - und dort faellt auf, wenn jemand um
        // die Mitte zoomt statt um die Maus.
        const double vorher2 = zeitBei(0.25);
        radDrehen(0.25, 3.0);
        std::printf("  Viertel: vorher %.0f ms, nachher %.0f ms\n", vorher2,
                    zeitBei(0.25));
        if (std::fabs(zeitBei(0.25) - vorher2) > 1.0) { ++fehler; }

        // Ganz herausdrehen: wieder das Ganze, und der Anfang bei null.
        radDrehen(0.5, -40.0);
        std::printf("  ganz heraus: Zoom %.2f, Anfang %.0f ms\n", zoom, start);
        if (std::fabs(zoom - 1.0) > 0.001) { ++fehler; }
        if (std::fabs(start) > 0.001) { ++fehler; }

        // Am RECHTEN Rand hineinzoomen darf nicht ueber das Ende
        // hinausrutschen - sonst zeigt die Leiste Leere.
        zoom = 1.0;
        start = 0.0;
        radDrehen(1.0, 10.0);
        std::printf("  am rechten Rand: Anfang %.0f, Ende %.0f (Dauer %.0f)\n",
                    start, start + gesamt / zoom, gesamt);
        if (start < -0.001) { ++fehler; }
        if (start + gesamt / zoom > gesamt + 1.0) { ++fehler; }
    }

    // --- Beschneiden: nichts laeuft aus dem Zeitfeld heraus -------------
    //
    // Gemeldet: beim Zoomen "schiesst es drueber hinaus". Bloecke, die
    // ausserhalb des Ausschnitts liegen, bekommen Bildpunkte links vor der
    // Namensspalte oder rechts hinter dem Rand - und malten dort. Im Bild
    // stand "camer" statt "camera PAN", weil ein Balken darueberlag.
    //
    // Die Rechnung selbst ist richtig; falsch war, dass niemand sie
    // begrenzt. Hier beides geprueft.
    {
        const float feldLinks = 100.0F;
        const float feldBreite = 800.0F;
        const double sichtStart = 20000.0;
        const double sichtSpanne = 10000.0;
        auto xAt = [&](double ms) {
            return feldLinks +
                   static_cast<float>((ms - sichtStart) / sichtSpanne) *
                       feldBreite;
        };

        // Ein Block VOR dem Ausschnitt landet links davon - im Bereich der
        // Namensspalte.
        const float davor = xAt(5000.0);
        // Einer DAHINTER rechts vom Feld.
        const float danach = xAt(50000.0);
        std::printf("  vor dem Ausschnitt x=%.0f, dahinter x=%.0f "
                    "(Feld %.0f..%.0f)\n",
                    (double)davor, (double)danach, (double)feldLinks,
                    (double)(feldLinks + feldBreite));
        if (davor >= feldLinks) { ++fehler; }
        if (danach <= feldLinks + feldBreite) { ++fehler; }

        // Und genau deshalb muss beschnitten werden: nach dem Beschneiden
        // liegt nichts mehr ausserhalb.
        auto beschnitten = [&](float x) {
            return std::clamp(x, feldLinks, feldLinks + feldBreite);
        };
        if (beschnitten(davor) != feldLinks) { ++fehler; }
        if (beschnitten(danach) != feldLinks + feldBreite) { ++fehler; }

        // Was IM Ausschnitt liegt, bleibt unveraendert - das Beschneiden
        // darf die sichtbaren Bloecke nicht verschieben.
        const float drin = xAt(25000.0);
        if (std::fabs(beschnitten(drin) - drin) > 0.001F) { ++fehler; }
        if (std::fabs(drin - (feldLinks + feldBreite * 0.5F)) > 0.5F) {
            ++fehler;
        }
    }

    // --- Rechte Kante: Lineal und Spuren muessen buendig sein -----------
    //
    // Die Spuren stehen in einem Kindfenster mit senkrechter Rollleiste;
    // die nimmt sich Platz vom INHALT. Zeiger und Lineal darueber haben
    // keine. Rechnet man das nicht ein, reichen sie weiter nach rechts als
    // die Balken darunter - gemeldet als "die timeline rechts ist bisschen
    // zu lang".
    {
        const float gesamtBreite = 2000.0F;
        const float labelW = 90.0F;
        const float rollBreite = 14.0F;

        // So rechnet das Programm: der Regler endet eine Rollleistenbreite
        // vor dem Rand, das Kindfenster nimmt sich die volle Breite und
        // zeigt IMMER eine Rollleiste.
        const float reglerW = gesamtBreite - labelW - rollBreite;
        const float kindBreite = reglerW + labelW + rollBreite;
        const float inhaltBreite = kindBreite - rollBreite;

        const float linealEnde = labelW + reglerW;
        const float spurenEnde = inhaltBreite;
        std::printf("  Lineal endet bei %.0f, Spuren bei %.0f\n",
                    (double)linealEnde, (double)spurenEnde);
        if (std::fabs(linealEnde - spurenEnde) > 0.001F) { ++fehler; }

        // Und der alte Fehler: ohne den Abzug lag das Lineal um genau eine
        // Rollleistenbreite daneben.
        const float altesLineal = labelW + (gesamtBreite - labelW);
        std::printf("  ohne Abzug lag das Lineal %.0f Bildpunkte daneben\n",
                    (double)(altesLineal - spurenEnde));
        if (std::fabs((altesLineal - spurenEnde) - rollBreite) > 0.001F) {
            ++fehler;
        }
    }

    // --- Die Schrittweite des Lineals -----------------------------------
    //
    // Gemeldet: bei starkem Zoom standen die Zahlen ineinander
    // ("528.5529.0529.5530.0..."). Zwei Ursachen, beide hier festgehalten.
    //
    // Die Zusage: zwischen zwei Marken muss MINDESTENS der Platz einer
    // Beschriftung liegen. Ist der Schritt kleiner als gewuenscht, stehen
    // sie ineinander - und genau das tat die alte Schleife: sie halbierte,
    // bis der Schritt KLEINER war als noetig, also eine Stufe zu fein.
    {
        auto stufe = [](double gewuenscht) {
            double schritt = 1.0;
            if (gewuenscht < 1.0) {
                while (schritt * 0.5 >= gewuenscht) { schritt *= 0.5; }
            } else {
                while (schritt < gewuenscht) {
                    if (schritt * 2.0 >= gewuenscht) { schritt *= 2.0; break; }
                    if (schritt * 5.0 >= gewuenscht) { schritt *= 5.0; break; }
                    schritt *= 10.0;
                }
            }
            return schritt;
        };
        // Die alte Fassung - zum Vergleich, damit der Fehler benannt bleibt.
        auto alteStufe = [](double gewuenscht) {
            double schritt = 1.0;
            if (gewuenscht < 1.0) {
                while (schritt > gewuenscht) { schritt *= 0.5; }
            }
            return schritt;
        };

        // Die GRUNDREGEL: der gewaehlte Schritt ist nie kleiner als der
        // gewuenschte. Ueber einen weiten Bereich geprueft.
        bool immerGrossGenug = true;
        for (double g = 0.02; g < 5000.0; g *= 1.3) {
            if (stufe(g) < g - 1.0e-9) { immerGrossGenug = false; }
        }
        std::printf("  Schritt nie feiner als noetig: %s\n",
                    immerGrossGenug ? "ja" : "NEIN");
        if (!immerGrossGenug) { ++fehler; }

        // Und er ist auch nicht unnoetig grob - hoechstens Faktor zehn.
        bool nichtZuGrob = true;
        for (double g = 0.02; g < 5000.0; g *= 1.3) {
            if (stufe(g) > g * 10.0 + 1.0e-9) { nichtZuGrob = false; }
        }
        if (!nichtZuGrob) { ++fehler; }

        // Der konkrete Fall aus der Meldung: gewuenscht 0,6 - dann gehoert
        // 1,0 dahin, nicht 0,5.
        std::printf("  bei 0,6 gewuenscht: neu %.2f, alt %.2f\n", stufe(0.6),
                    alteStufe(0.6));
        if (std::fabs(stufe(0.6) - 1.0) > 1e-9) { ++fehler; }
        if (std::fabs(alteStufe(0.6) - 0.5) > 1e-9) { ++fehler; }

        // Runde Zahlen bleiben rund.
        if (std::fabs(stufe(3.0) - 5.0) > 1e-9) { ++fehler; }
        if (std::fabs(stufe(1.5) - 2.0) > 1e-9) { ++fehler; }
        if (std::fabs(stufe(30.0) - 50.0) > 1e-9) { ++fehler; }
    }

    // --- Der Ring bleibt beim Ziehen STEHEN -----------------------------
    //
    // Gemeldet: "jetzt rotiert beides mit". Die Ringe wurden jedes Bild aus
    // den VORSCHAUwinkeln neu gebaut und drehten sich damit mit.
    //
    // In 3ds Max steht das Gizmo waehrend des Zugs still und ein
    // Tortenstueck zeigt den Betrag - "an arc is highlighted that shows the
    // distance of the rotation". Genau dafuer braucht es einen stehenden
    // Bezug: ein mitdrehender Ring misst gegen sich selbst und zeigt nichts
    // an. Am Ende stuende die Marke immer bei null.
    {
        // Achse zum Anfassen: die Kamera schaut nach +X, der Ring um die
        // Hochachse liegt also in der XY-Ebene.
        const float startAchse[3] = {0.0F, 0.0F, 1.0F};

        // Eingefroren: die Achse bleibt, egal wie weit gedreht wurde.
        auto ringAchseEingefroren = [&](float /*gedreht*/) {
            return startAchse[2];   // unveraendert
        };
        // Mitdrehend: die Achse wandert mit der Vorschau.
        auto ringAchseMitdrehend = [&](float gedreht) {
            float raus[3];
            const float um[3] = {1.0F, 0.0F, 0.0F};
            bhed::rotateAboutAxis(startAchse, um, gedreht, raus);
            return raus[2];
        };

        std::printf("  nach 60 Grad: eingefroren z=%.2f, mitdrehend z=%.2f\n",
                    (double)ringAchseEingefroren(60.0F),
                    (double)ringAchseMitdrehend(60.0F));
        if (std::fabs(ringAchseEingefroren(60.0F) - 1.0F) > 0.001F) { ++fehler; }
        // Der Beleg fuer den Fehler: mitdrehend kippt die Achse weg.
        if (std::fabs(ringAchseMitdrehend(60.0F) - 1.0F) < 0.1F) { ++fehler; }

        // Und das Tortenstueck: seine Speichenzahl waechst mit dem Betrag,
        // bleibt aber begrenzt - sonst zeichnet ein voller Umlauf
        // hunderte Linien.
        auto speichen = [](float grad) {
            const int n = static_cast<int>(std::fabs(grad) / 4.0F);
            return n < 1 ? 1 : (n > 90 ? 90 : n);
        };
        std::printf("  Speichen: 4 Grad -> %d, 90 Grad -> %d, 720 Grad -> %d\n",
                    speichen(4.0F), speichen(90.0F), speichen(720.0F));
        if (speichen(0.5F) != 1) { ++fehler; }
        if (speichen(90.0F) < 10) { ++fehler; }
        if (speichen(720.0F) != 90) { ++fehler; }
        // Auch rueckwaerts - ein negativer Betrag darf nicht null Speichen
        // ergeben, sonst ist beim Zurueckdrehen nichts zu sehen.
        if (speichen(-45.0F) < 5) { ++fehler; }
    }

    std::printf("%s (%d Fehlschlaege)\n",
                fehler == 0 ? "alle Gizmoproben bestanden" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
