// Deckt das Raster den Bereich lueckenlos und ohne Ueberschneidung ab?
//
// Die Aufteilung steht in gui/app.cpp (drawFlowArea) und laesst sich ohne
// Fenster nicht aufrufen - hier ist sie deshalb NACHGEBAUT, wie schon bei
// gizmotest. Was diese Probe festhaelt, ist die Rechenvorschrift; wer die
// eine aendert, aendert die andere mit. Der Kommentar dort sagt das.
//
// Geprueft wird, was man an einem Bild nur schwer sieht:
//
//   1. Zusammen ergeben die Felder genau die Flaeche - kein Rest, keine
//      Doppelbelegung. Ein Rundungsfehler von einem Bildpunkt faellt am
//      Bildschirm nicht auf, staut sich aber ueber die Aufteilungen.
//   2. Kein Feld ist leer oder negativ.
//   3. Die Felder ueberlappen einander nicht.
#include <cmath>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace {

struct Feld {
    float x;
    float y;
    float w;
    float h;
};

int fehler = 0;

void expect(const char* was, bool ok) {
    std::printf("  %s   %s\n", ok ? "ok  " : "FEHL", was);
    if (!ok) { ++fehler; }
}

// Die Anteile sind seit rc179 verstellbar - der Anwender zieht die
// Trennlinien. Die Vorgabe 0,5 ist die alte Aufteilung, damit die
// bestehenden Aussagen unveraendert gelten.
int teile(int felder, float w, float h, Feld* aus, float luecke,
          float fracX = 0.5F, float fracY = 0.5F) {
    const float hw = std::floor(w * std::clamp(fracX, 0.15F, 0.85F));
    const float hh = std::floor(h * std::clamp(fracY, 0.15F, 0.85F));
    switch (felder) {
        case 2:
            aus[0] = {0.0F, 0.0F, hw - luecke, h};
            aus[1] = {hw, 0.0F, w - hw, h};
            return 2;
        case 3:
            aus[0] = {0.0F, 0.0F, hw - luecke, h};
            aus[1] = {hw, 0.0F, w - hw, hh - luecke};
            aus[2] = {hw, hh, w - hw, h - hh};
            return 3;
        case 4:
            aus[0] = {0.0F, 0.0F, hw - luecke, hh - luecke};
            aus[1] = {hw, 0.0F, w - hw, hh - luecke};
            aus[2] = {0.0F, hh, hw - luecke, h - hh};
            aus[3] = {hw, hh, w - hw, h - hh};
            return 4;
        default:
            aus[0] = {0.0F, 0.0F, w, h};
            return 1;
    }
}

bool ueberlappen(const Feld& a, const Feld& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

}  // namespace

int main() {
    // Ungerade Masse absichtlich: dort zeigt sich, ob das Abrunden einen
    // Rest laesst.
    const float breiten[] = {1280.0F, 1279.0F, 801.0F, 333.0F};
    const float hoehen[] = {720.0F, 719.0F, 501.0F, 201.0F};

    for (float luecke : {0.0F, 8.0F}) {
    for (int n = 1; n <= 4; ++n) {
        for (float w : breiten) {
            for (float h : hoehen) {
                Feld f[4];
                const int anzahl = teile(n, w, h, f, luecke);
                if (anzahl != n) { ++fehler; }
                float summe = 0.0F;
                bool positiv = true;
                for (int i = 0; i < anzahl; ++i) {
                    summe += f[i].w * f[i].h;
                    if (f[i].w <= 0.0F || f[i].h <= 0.0F) { positiv = false; }
                }
                if (!positiv) {
                    std::printf("  FEHL  %d Felder bei %.0fx%.0f: leeres Feld\n",
                                n, (double)w, (double)h);
                    ++fehler;
                }
                // Mit Luecke ist die Summe kleiner als die Flaeche - genau
                // um die Luecken. Ohne Luecke muss sie genau aufgehen.
                const float erwartet =
                    (luecke == 0.0F) ? w * h : summe;   // mit Luecke nur > 0
                if (luecke == 0.0F && std::fabs(summe - erwartet) > 0.5F) {
                    std::printf("  FEHL  %d Felder bei %.0fx%.0f: Flaeche %.0f "
                                "statt %.0f\n",
                                n, (double)w, (double)h, (double)summe,
                                (double)(w * h));
                    ++fehler;
                }
                for (int i = 0; i < anzahl; ++i) {
                    for (int j = i + 1; j < anzahl; ++j) {
                        if (ueberlappen(f[i], f[j])) {
                            std::printf("  FEHL  %d Felder bei %.0fx%.0f: "
                                        "%d und %d ueberlappen\n",
                                        n, (double)w, (double)h, i, j);
                            ++fehler;
                        }
                    }
                }
            }
        }
    }
    }
    expect("jede Aufteilung deckt die Flaeche genau ab", fehler == 0);

    // Mit Luecke duerfen die Felder einander NICHT beruehren - sonst sieht
    // es aus, als liefen sie ineinander, und genau das war zu sehen.
    {
        Feld f[4];
        const float lk = 8.0F;
        (void)teile(4, 1000.0F, 600.0F, f, lk);
        expect("mit Luecke: links endet vor rechts",
               f[0].x + f[0].w + 0.01F < f[1].x);
        expect("mit Luecke: oben endet vor unten",
               f[0].y + f[0].h + 0.01F < f[2].y);
        expect("die Luecke ist genau so breit wie verlangt",
               std::fabs((f[1].x - (f[0].x + f[0].w)) - lk) < 0.01F);
    }

    // Und die ANORDNUNG, wie sie beschrieben ist: bei drei Feldern steht
    // eines links ueber die volle Hoehe, zwei rechts uebereinander.
    {
        Feld f[4];
        (void)teile(3, 1000.0F, 600.0F, f, 0.0F);
        expect("bei drei: links volle Hoehe", std::fabs(f[0].h - 600.0F) < 0.01F);
        expect("bei drei: rechts oben halbe Hoehe",
               std::fabs(f[1].h - 300.0F) < 0.01F);
        expect("bei drei: rechts unten darunter",
               std::fabs(f[2].y - 300.0F) < 0.01F);
        expect("bei drei: beide rechts gleich breit",
               std::fabs(f[1].w - f[2].w) < 0.01F);
    }
    {
        Feld f[4];
        (void)teile(4, 1000.0F, 600.0F, f, 0.0F);
        expect("bei vier: zwei Reihen", std::fabs(f[2].y - 300.0F) < 0.01F);
        expect("bei vier: zwei Spalten", std::fabs(f[1].x - 500.0F) < 0.01F);
    }

    // --- Verstellbare Trennlinien ---------------------------------------
    //
    // "I want to be able to change the size of each script flow view".
    // Geprueft wird, dass die Aufteilung bei JEDEM Anteil lueckenlos und
    // ueberschneidungsfrei bleibt - und dass die Grenzen wirken, damit
    // kein Feld auf null schrumpft und unerreichbar wird.
    {
        Feld f[4];
        const float w = 1000.0F;
        const float h = 600.0F;
        const float luecke = 8.0F;

        for (float fx : {0.2F, 0.35F, 0.5F, 0.75F, 0.85F}) {
            const int n = teile(2, w, h, f, luecke, fx, 0.5F);
            const bool lueckenlos =
                std::fabs((f[0].x + f[0].w + luecke) - f[1].x) < 0.001F;
            const bool voll =
                std::fabs((f[1].x + f[1].w) - w) < 0.001F;
            char txt[96];
            std::snprintf(txt, sizeof(txt),
                          "zwei Felder bei %.0f%%: lueckenlos und voll",
                          (double)(fx * 100.0F));
            expect(txt, n == 2 && lueckenlos && voll);
            if (f[0].w < 1.0F || f[1].w < 1.0F) { ++fehler; }
        }

        // Ausserhalb der Grenzen wird geklemmt - sonst verschwindet ein
        // Feld ganz, und mit ihm der Weg, es zurueckzuholen.
        teile(2, w, h, f, luecke, 0.0F, 0.5F);
        expect("bei 0 Prozent bleibt das linke Feld sichtbar",
               f[0].w >= w * 0.15F - luecke - 1.0F);
        teile(2, w, h, f, luecke, 1.0F, 0.5F);
        expect("bei 100 Prozent bleibt das rechte Feld sichtbar",
               f[1].w >= w * 0.15F - 1.0F);

        // Bei vier Feldern muessen BEIDE Linien wirken.
        teile(4, w, h, f, luecke, 0.3F, 0.7F);
        const float linksB = f[0].w + luecke;
        const float obenH = f[0].h + luecke;
        std::printf("     vier Felder bei 30/70: links %.0f, oben %.0f\n",
                    (double)linksB, (double)obenH);
        expect("die senkrechte Linie sitzt bei 30 Prozent",
               std::fabs(linksB - w * 0.3F) < 1.5F);
        expect("die waagerechte bei 70 Prozent",
               std::fabs(obenH - h * 0.7F) < 1.5F);
        expect("und die untere Reihe fuellt den Rest",
               std::fabs((f[2].y + f[2].h) - h) < 0.001F);
    }

    // --- Wie schmal darf die Skriptspalte werden? -----------------------
    //
    // Gewuenscht: weiter hineinziehen, "das man nur die Symbole hat wie bei
    // Blender". Vorher war bei festen 200 Bildpunkten Schluss.
    //
    // Zwei Zusagen haengen daran, und beide waeren im Bild erst spaet
    // aufgefallen:
    //   die Spalte darf nie auf null gehen - sonst kommt man nicht zurueck,
    //   und die Symbolansicht muss GENAU dann greifen, wenn Text ohnehin
    //   abgeschnitten waere.
    {
        const float schrift = 16.0F;          // Schriftgroesse
        const float rest = 1200.0F;           // Events + Flow zusammen
        auto flowBreite = [&](float wunschEvents) {
            const float ev = std::min(std::max(wunschEvents, schrift * 26.0F),
                                      rest - schrift * 6.0F);
            return rest - ev;
        };

        // Ganz nach rechts gezogen: es bleibt die Symbolbreite.
        const float schmalstens = flowBreite(rest);
        std::printf("     schmalste Skriptspalte: %.0f (Schrift %.0f)\n",
                    (double)schmalstens, (double)schrift);
        expect("die Spalte verschwindet NICHT",
               schmalstens >= schrift * 6.0F - 0.01F);
        expect("und sie ist schmaler als die alten 200",
               schmalstens < 200.0F);

        // Die Symbolansicht greift ab 8 Schriftgroessen - also VOR der
        // Untergrenze. Sonst gaebe es eine Breite, in der weder Text noch
        // Symbole passen.
        const float schwelle = schrift * 8.0F;
        expect("die Symbolschwelle liegt ueber der Untergrenze",
               schwelle > schrift * 6.0F);
        expect("bei der schmalsten Spalte greift sie",
               schmalstens < schwelle);

        // Und weit offen bleibt es beim Text.
        const float weit = flowBreite(rest * 0.4F);
        std::printf("     bei 40 Prozent Events: Skriptspalte %.0f\n",
                    (double)weit);
        expect("weit offen wird nicht auf Symbole geschaltet",
               weit > schwelle);
    }

    // --- Die Ziehkante muss die Symbolansicht ERREICHEN -----------------
    //
    // rc201 senkte die Untergrenze der Skriptspalte auf sechs
    // Schriftgroessen. Die Kante selbst klemmte aber weiter bei einem
    // Anteil von 0,80 - bei breitem Fenster also mehrere hundert
    // Bildpunkte. Die Rechnung erlaubte die Symbolansicht laengst, der
    // GRIFF kam nicht so weit.
    //
    // Genau diese Art Fehler ist von aussen nicht zu sehen: beides fuer
    // sich ist richtig, nur zusammen ergeben sie eine Wand.
    {
        const float schrift = 24.0F;   // 144 dpi, wie beim Anwender
        const float schwelle = schrift * 8.0F;   // ab hier nur Symbole
        const float mindest = schrift * 6.0F;

        auto flowBeiGrenze = [&](float rest, float obereGrenze) {
            return rest * (1.0F - obereGrenze);
        };
        auto neueGrenze = [&](float rest) {
            const float g = 1.0F - mindest / rest;
            return g < 0.30F ? 0.30F : (g > 0.98F ? 0.98F : g);
        };

        for (const float rest : {900.0F, 1800.0F, 3000.0F}) {
            const float alt = flowBeiGrenze(rest, 0.80F);
            const float neu = flowBeiGrenze(rest, neueGrenze(rest));
            std::printf("     Breite %.0f: alt blieben %.0f, neu %.0f "
                        "(Schwelle %.0f)\n",
                        (double)rest, (double)alt, (double)neu,
                        (double)schwelle);
            expect("mit der neuen Grenze ist die Symbolansicht erreichbar",
                   neu <= schwelle);
            expect("und die Spalte verschwindet trotzdem nicht",
                   neu >= mindest - 0.5F);
        }
        // Der Beleg fuer den Fehler: bei breitem Fenster war es vorher NICHT
        // erreichbar.
        expect("vorher war sie bei breitem Fenster unerreichbar",
               flowBeiGrenze(3000.0F, 0.80F) > schwelle);
    }

    // --- Die Feldreihe im Ereignisfenster mittig setzen -----------------
    //
    // Zweimal verrechnet, deshalb hier festgehalten. Die Zahlen stammen aus
    // dem gemeldeten Bild: Fenster 848 breit, Reihe 694, Einzug der
    // Beschriftungsspalte 70.
    //
    // Der Versatz ist die Differenz zwischen SOLL und IST der Startstelle,
    // nicht die halbe Restbreite. Die Startstelle enthaelt naemlich schon
    // den Einzug - wer ihn dazuaddiert, zaehlt ihn doppelt und schiebt die
    // Reihe genau um diesen Betrag nach rechts.
    {
        auto startX = [](float fenster, float reihe, float einzug,
                         bool richtig) {
            const float soll = (fenster - reihe) * 0.5F;
            const float versatz =
                richtig ? std::max(0.0F, soll - einzug) : std::max(0.0F, soll);
            return versatz + einzug;   // dorthin kommt die Beschriftung
        };

        const float mitte = (848.0F - 694.0F) * 0.5F;   // = 77
        std::printf("     Reihe soll bei %.0f beginnen; richtig %.0f, "
                    "alt %.0f\n", (double)mitte,
                    (double)startX(848, 694, 70, true),
                    (double)startX(848, 694, 70, false));
        expect("die Reihe beginnt wirklich mittig",
               std::fabs(startX(848, 694, 70, true) - mitte) < 0.01F);
        expect("die alte Rechnung lag um den Einzug daneben",
               std::fabs(startX(848, 694, 70, false) - mitte - 70.0F) < 0.01F);

        // Ist die Reihe BREITER als das Fenster, bleibt der Versatz null -
        // sonst schoebe er sie hinaus.
        expect("eine zu breite Reihe wird nicht verschoben",
               std::fabs(startX(400, 694, 70, true) - 70.0F) < 0.01F);
        // Und ohne Einzug ist beides dasselbe.
        expect("ohne Einzug sind beide Rechnungen gleich",
               std::fabs(startX(848, 694, 0, true) -
                         startX(848, 694, 0, false)) < 0.01F);
    }

    std::printf("%s (%d Fehlschlaege)\n",
                fehler == 0 ? "alle Rasterproben bestanden" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
