// Landet eine Bearbeitung immer im richtigen Dokument?
//
// Das ist die Frage, an der der Umbau auf vier bearbeitbare Felder haengt.
// Der Aufbau kommt ohne ein zweites lebendes Dokument aus: das Feld mit dem
// FOKUS wird das lebende, die uebrigen liegen geparkt daneben. Getauscht
// wird beim Fokuswechsel.
//
// Hier ist dieser Tausch nachgebaut - park, take, wechsle - und danach
// geprueft, was schiefgehen kann:
//
//   1. Eine Aenderung im fokussierten Feld trifft SEIN Dokument.
//   2. Die anderen bleiben dabei unberuehrt.
//   3. Nach mehrmaligem Wechseln steht jedes Dokument noch da, wo es
//      hingehoert - der Tausch verliert nichts und vertauscht nichts.
//   4. Der Rueckgaengig-Speicher wandert mit: was in Feld A rueckgaengig
//      gemacht werden kann, kann es auch nach einem Ausflug zu Feld B.
#include "bhed/edit.h"
#include "bhed/script.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int fehler = 0;

void expect(const char* was, bool ok) {
    std::printf("  %s   %s\n", ok ? "ok  " : "FEHL", was);
    if (!ok) { ++fehler; }
}

// Ein Reiter: Dokument plus Name, wie App::Parked.
struct Reiter {
    bhed::Document doc{bhed::Script{}};
    std::string name;
};

// Der Tausch, wie ihn parkActive()/takeTab() machen.
struct Welt {
    std::vector<Reiter> reiter;
    bhed::Document lebend{bhed::Script{}};
    std::string lebendName;
    int aktiv = 0;
    // Welcher Reiter in welchem Feld steht.
    int feldReiter[4] = {0, 0, 0, 0};
    int fokus = 0;

    void parken() {
        reiter[static_cast<std::size_t>(aktiv)].doc = lebend;
        reiter[static_cast<std::size_t>(aktiv)].name = lebendName;
    }
    void holen(int i) {
        aktiv = i;
        lebend = reiter[static_cast<std::size_t>(i)].doc;
        lebendName = reiter[static_cast<std::size_t>(i)].name;
    }
    // Die Feldaufteilung gehoert dem REITER, nicht dem Programm.
    // Die Auswahl: je Feld ein Weg, dazu der des lebenden Dokuments.
    // Der Tausch uebertraegt sie wie drawSplitGrid seit rc165: das alte
    // Feld behaelt die lebende Auswahl, die im neuen Feld angeklickte
    // wird die lebende - VS-Code-Verhalten, jede Gruppe ihr Zeiger.
    std::string feldAuswahl[4];
    std::string lebendAuswahl;

    void fokusAuf(int feld) {
        if (feld == fokus) { return; }
        feldReiter[fokus] = aktiv;
        feldAuswahl[fokus] = lebendAuswahl;
        fokus = feld;
        parken();
        holen(feldReiter[feld]);
        if (!feldAuswahl[feld].empty()) {
            lebendAuswahl = feldAuswahl[feld];
        }
    }

    // Ein Klick in ein UNfokussiertes Feld: erst die Zeile dort merken,
    // dann den Fokus anfordern - genau die Reihenfolge im Programm.
    void klickIn(int feld, const std::string& zeile) {
        feldAuswahl[feld] = zeile;
        fokusAuf(feld);
    }

    // --- Die Aufteilung gehoert dem REITER, nicht dem Programm ---------
    //
    // Jeder Reiter hat sein eigenes Fenster: wie viele Felder, welcher
    // Reiter in welchem, wo der Fokus lag. Wer in den Reiter nebenan
    // wechselt, findet dort SEINE Aufteilung vor - ein frischer Reiter
    // ist ungeteilt.
    struct Aufteilung {
        int felder = 1;
        int feldReiter[4] = {0, 0, 0, 0};
        int fokus = 0;
    };
    Aufteilung reiterLayout[4];
    int felder = 1;

    // Reiterwechsel: die Aufteilung wird mitgeparkt und geholt.
    void reiterWechsel(int neuerReiter) {
        if (neuerReiter == aktiv) { return; }
        Aufteilung& alt = reiterLayout[static_cast<std::size_t>(aktiv)];
        alt.felder = felder;
        alt.fokus = fokus;
        for (int i = 0; i < 4; ++i) {
            alt.feldReiter[i] = (i == fokus) ? aktiv : feldReiter[i];
        }
        parken();
        holen(neuerReiter);
        const Aufteilung& neu =
            reiterLayout[static_cast<std::size_t>(neuerReiter)];
        felder = neu.felder;
        fokus = neu.fokus;
        for (int i = 0; i < 4; ++i) { feldReiter[i] = neu.feldReiter[i]; }
    }
};

bhed::Node befehl(const std::string& name) {
    bhed::Node n;
    n.name = name;
    return n;
}

}  // namespace

int main() {
    Welt w;
    // Drei Reiter mit je einem erkennbaren Befehl.
    for (int i = 0; i < 3; ++i) {
        Reiter r;
        bhed::Script sc;
        sc.nodes.push_back(befehl("start" + std::to_string(i)));
        r.doc = bhed::Document{sc};
        r.name = "skript" + std::to_string(i);
        w.reiter.push_back(std::move(r));
    }
    // Feld 0 zeigt Reiter 0, Feld 1 den Reiter 1, Feld 2 den Reiter 2.
    w.feldReiter[0] = 0;
    w.feldReiter[1] = 1;
    w.feldReiter[2] = 2;
    w.holen(0);

    // --- 1. Aenderung im fokussierten Feld trifft SEIN Dokument ---------
    bhed::Path wurzel;
    wurzel.push_back(0);
    (void)w.lebend.insertAfter(wurzel, befehl("neu_in_0"));
    expect("Aenderung landet im lebenden Dokument",
           w.lebend.script().nodes.size() == 2);

    // --- 2. Die anderen bleiben unberuehrt ------------------------------
    expect("Reiter 1 unberuehrt",
           w.reiter[1].doc.script().nodes.size() == 1);
    expect("Reiter 2 unberuehrt",
           w.reiter[2].doc.script().nodes.size() == 1);

    // --- 3. Nach mehrmaligem Wechseln steht alles am richtigen Platz ----
    w.fokusAuf(1);
    expect("Feld 1 zeigt sein eigenes Skript", w.lebendName == "skript1");
    (void)w.lebend.insertAfter(wurzel, befehl("neu_in_1"));
    (void)w.lebend.insertAfter(wurzel, befehl("noch_eins_in_1"));

    w.fokusAuf(2);
    expect("Feld 2 zeigt sein eigenes Skript", w.lebendName == "skript2");

    w.fokusAuf(0);
    expect("zurueck in Feld 0: wieder sein Skript",
           w.lebendName == "skript0");
    expect("und seine Aenderung ist noch da",
           w.lebend.script().nodes.size() == 2);

    w.fokusAuf(1);
    expect("Feld 1 hat seine ZWEI Aenderungen behalten",
           w.lebend.script().nodes.size() == 3);
    expect("Feld 2 hat KEINE bekommen",
           w.reiter[2].doc.script().nodes.size() == 1);

    // --- 4. Der Rueckgaengig-Speicher wandert mit -----------------------
    //
    // Das ist der heikelste Punkt: waere er an das Programm gebunden statt
    // an das Dokument, koennte ein Rueckgaengig in Feld B eine Aenderung
    // aus Feld A zuruecknehmen.
    expect("in Feld 1 laesst sich rueckgaengig machen", w.lebend.undoDepth() > 0);
    (void)w.lebend.undo();
    expect("und es nimmt EINE Aenderung zurueck",
           w.lebend.script().nodes.size() == 2);
    w.fokusAuf(0);
    (void)w.lebend.undo();
    expect("in Feld 0 nimmt es SEINE Aenderung zurueck",
           w.lebend.script().nodes.size() == 1);
    // --- 5. Die Auswahl wandert mit dem Klick, nicht mit dem Tausch -----
    //
    // Das war das "springt beim anderen das umher": ohne die Uebertragung
    // zeigte das alte Feld eine URALTE Auswahl, und die angeklickte Zeile
    // im neuen Feld wurde gar nicht ausgewaehlt.
    w.lebendAuswahl = "zeile_A";      // in Feld 0 ist zeile_A gewaehlt
    w.klickIn(1, "zeile_B");          // Klick auf zeile_B in Feld 1
    expect("die angeklickte Zeile ist jetzt die lebende Auswahl",
           w.lebendAuswahl == "zeile_B");
    expect("das alte Feld behaelt SEINE Auswahl",
           w.feldAuswahl[0] == "zeile_A");
    w.klickIn(0, w.feldAuswahl[0]);   // zurueck nach Feld 0
    expect("zurueck in Feld 0: dort steht wieder zeile_A",
           w.lebendAuswahl == "zeile_A");
    expect("und Feld 1 hat zeile_B behalten",
           w.feldAuswahl[1] == "zeile_B");

    // --- 6. Die Aufteilung gehoert dem Reiter --------------------------
    //
    // Gemeldet: "wenn ich im ersten tab 4 reiter offen habe und dann in
    // den nebenan gehe, dann sollte da wieder ein einzelner sein und
    // nicht global ueber alle verteilt".
    w.fokusAuf(0);
    w.reiterWechsel(0);
    w.felder = 4;                      // Reiter 0 wird vierfach geteilt
    w.feldReiter[1] = 1;
    w.feldReiter[2] = 2;
    w.reiterWechsel(1);                // hinueber zum Reiter nebenan
    expect("der Reiter nebenan ist UNGETEILT", w.felder == 1);
    w.felder = 2;                      // dort auf zwei Felder gestellt
    w.reiterWechsel(0);
    expect("zurueck im ersten: wieder VIER Felder", w.felder == 4);
    expect("und seine Feldbelegung steht noch",
           w.feldReiter[1] == 1 && w.feldReiter[2] == 2);
    w.reiterWechsel(1);
    expect("und der zweite hat SEINE zwei behalten", w.felder == 2);

    // Der Fokuswechsel darf die Aufteilung NICHT anfassen - sonst
    // klappte ein Klick ins Nachbarfeld die Ansicht um.
    // In ein ANDERES Feld als das aktuelle - sonst laeuft fokusAuf() in
    // seinen Kurzausgang und die Probe prueft nichts. (Erst aufgefallen,
    // als die Gegenprobe gruen blieb.)
    const int vorher = w.felder;
    const int vorherFokus = w.fokus;
    w.fokusAuf(vorherFokus == 0 ? 1 : 0);
    expect("der Fokus ist wirklich gewandert", w.fokus != vorherFokus);
    expect("ein Fokuswechsel laesst die Aufteilung stehen",
           w.felder == vorher);

    // --- 7. Abwaehlen, und kein ungefragtes Hervorheben -----------------
    //
    // Drei Zusagen aus rc167:
    //   a) dieselbe Zeile nochmal anklicken hebt die Auswahl auf,
    //   b) ein Klick ins Leere ebenso,
    //   c) der Fokuswechsel schiebt KEINE Auswahl in das abgebende Feld.
    {
        Welt v;
        for (int i = 0; i < 2; ++i) {
            Reiter r;
            bhed::Script sc;
            sc.nodes.push_back(befehl("start" + std::to_string(i)));
            r.doc = bhed::Document{sc};
            r.name = "skript" + std::to_string(i);
            v.reiter.push_back(std::move(r));
        }
        v.holen(0);

        // a) Klick, nochmal Klick auf dieselbe Zeile.
        v.klickIn(0, "zeile_X");
        expect("nach dem Klick ist zeile_X gewaehlt",
               v.feldAuswahl[0] == "zeile_X");
        // Nochmal dieselbe: das Feld raeumt seine Auswahl weg.
        if (v.feldAuswahl[0] == "zeile_X") { v.feldAuswahl[0].clear(); }
        expect("nochmal angeklickt: nichts mehr gewaehlt",
               v.feldAuswahl[0].empty());

        // b) Klick ins Leere in Feld 1.
        v.feldAuswahl[1] = "zeile_Y";
        v.feldAuswahl[1].clear();  // Klick auf freie Flaeche
        expect("Klick ins Leere raeumt auch auf",
               v.feldAuswahl[1].empty());

        // c) Feld 2 wurde nie angeklickt und bleibt leer, auch wenn der
        //    Fokus durch die Felder wandert. Das war das "zuckt": in
        //    rc165 bekam das abgebende Feld die lebende Auswahl
        //    nachgeschoben und leuchtete ploetzlich auf.
        v.lebendAuswahl = "zeile_Z";
        v.klickIn(0, "zeile_A");
        expect("das Feld ohne Klick bleibt leer",
               v.feldAuswahl[2].empty());
        expect("und das angeklickte hat SEINE Zeile",
               v.feldAuswahl[0] == "zeile_A");
    }

    // --- Ein neuer Reiter und die Karte ---------------------------------
    //
    // Zwei Wege, ein Verhalten:
    //   "+" oder "Neu"        -> leeres Blatt, KEINE Karte
    //   Skript aus der Mission -> erbt die Karte, alle gehoeren zur .bsp
    //
    // Der Fehler in rc182 war, dass die Absicht nur im geparkten Reiter
    // stand ("frisch.mapPath" leer), die LEBENDE Karte aber nie
    // abgeraeumt wurde. Hier beides nachgestellt: der gemerkte Wert UND
    // der lebende muessen zusammenpassen.
    {
        std::string lebendeKarte = "maps/md_am_sith.bsp";
        std::string reiterKarte;   // ein frischer Reiter hat keine

        auto neuerReiter = [&](bool erben) {
            reiterKarte = erben ? lebendeKarte : std::string();
            // genau der Abgleich aus addTab()
            if (reiterKarte != lebendeKarte) {
                lebendeKarte = reiterKarte;
            }
        };

        neuerReiter(false);
        expect("ein neuer Reiter hat keine Karte gemerkt",
               reiterKarte.empty());
        expect("und die lebende Karte ist wirklich weg",
               lebendeKarte.empty());

        lebendeKarte = "maps/md_am_sith.bsp";
        neuerReiter(true);
        expect("ein Skript aus der Mission erbt die Karte",
               reiterKarte == "maps/md_am_sith.bsp");
        expect("und sie bleibt geladen",
               lebendeKarte == "maps/md_am_sith.bsp");
    }

    // --- Nicht mehr Felder als Skripte ----------------------------------
    //
    // "Das split view sollte nur gehen wenn es neue tabs gibt und nicht
    // sich selbst duplizieren."
    {
        auto erlaubt = [](int felder, int reiter) {
            return felder <= std::max(1, reiter);
        };
        expect("mit EINEM Skript ist nur ein Feld moeglich",
               erlaubt(1, 1) && !erlaubt(2, 1) && !erlaubt(4, 1));
        expect("mit zwei Skripten gehen zwei Felder",
               erlaubt(2, 2) && !erlaubt(3, 2));
        expect("mit vieren alle vier", erlaubt(4, 4));

        // Und beim SCHLIESSEN schrumpft die Aufteilung mit - sonst zeigte
        // ein Feld zwangslaeufig dasselbe Skript wie ein anderes.
        int felder = 4;
        int reiter = 4;
        int fokus = 3;
        int feldReiterNr[4] = {0, 1, 2, 3};
        reiter = 2;   // zwei geschlossen
        felder = std::min(felder, std::max(1, reiter));
        fokus = std::min(fokus, felder - 1);
        for (int& fr : feldReiterNr) {
            fr = std::min(fr, std::max(0, reiter - 1));
        }
        expect("nach dem Schliessen bleiben zwei Felder", felder == 2);
        expect("der Fokus liegt in einem, das es noch gibt", fokus < felder);
        expect("und kein Feld zeigt auf einen geloeschten Reiter",
               feldReiterNr[3] <= reiter - 1);
    }

    // --- Die Karte gehoert dem Reiter, auch aus einem Archiv ------------
    //
    // Gemeldet: "die map ist nicht pro tab".
    //
    // Verglichen wurde der PLATTENPFAD. Eine Mission kommt aber aus einem
    // .pk3 und hat gar keinen - er ist leer. "leer gegen leer" ist immer
    // gleich, also wurde nie gewechselt.
    //
    // Jetzt entscheidet die KENNUNG der Karte (etwa "maps/md_am_sith.bsp"),
    // die es auch bei einer Karte aus dem Archiv gibt.
    {
        struct Reiterchen { std::string pfad; std::string id; };
        const Reiterchen ausArchiv{"", "maps/md_am_sith.bsp"};
        const Reiterchen andere{"", "maps/duel_deathstar.bsp"};
        const Reiterchen leer{"", ""};
        const Reiterchen vonPlatte{"C:/x/md_am_sith.bsp", "maps/md_am_sith.bsp"};

        auto wechselNoetigAlt = [](const Reiterchen& a, const Reiterchen& b) {
            return a.pfad != b.pfad;            // die alte Regel
        };
        auto wechselNoetigNeu = [](const Reiterchen& a, const Reiterchen& b) {
            return a.id != b.id;                // die neue
        };

        expect("alt: zwei verschiedene Missionskarten galten als gleich",
               !wechselNoetigAlt(ausArchiv, andere));
        expect("neu: sie werden unterschieden",
               wechselNoetigNeu(ausArchiv, andere));
        expect("neu: ein leerer Reiter raeumt die Karte weg",
               wechselNoetigNeu(ausArchiv, leer));
        expect("neu: dieselbe Karte bleibt stehen",
               !wechselNoetigNeu(ausArchiv, ausArchiv));
        expect("und von der Platte geladen zaehlt sie als dieselbe",
               !wechselNoetigNeu(ausArchiv, vonPlatte));
    }

    // --- Die vier Regeln, die immer gelten muessen ----------------------
    //
    // Seit rc218 prueft das Programm sie selbst und meldet sich im
    // Protokoll. Alle vier stammen aus echten Fehlern dieser Sitzung -
    // hier stehen sie noch einmal als Probe, damit die Bedingungen selbst
    // richtig sind.
    {
        struct Zustand {
            int reiter;
            int felder;
            int fokus;
            int feldReiter[4];
        };
        auto inOrdnung = [](const Zustand& z) {
            if (z.felder > z.reiter) { return false; }
            if (z.fokus < 0 || z.fokus >= z.felder) { return false; }
            for (int i = 0; i < z.felder; ++i) {
                if (z.feldReiter[i] < 0 || z.feldReiter[i] >= z.reiter) {
                    return false;
                }
                for (int j = i + 1; j < z.felder; ++j) {
                    if (z.feldReiter[i] == z.feldReiter[j]) { return false; }
                }
            }
            return true;
        };

        expect("ein gesunder Zustand geht durch",
               inOrdnung({3, 2, 1, {0, 2, 0, 0}}));
        // rc179: neue Felder bekamen immer Reiter 0.
        expect("zwei Felder auf demselben Reiter fallen auf",
               !inOrdnung({3, 2, 0, {1, 1, 0, 0}}));
        // rc184: nach dem Schliessen zeigte ein Feld ins Leere.
        expect("ein Feld auf einem geloeschten Reiter faellt auf",
               !inOrdnung({2, 2, 0, {0, 5, 0, 0}}));
        // rc184: der Fokus lag ausserhalb.
        expect("ein Fokus ausserhalb faellt auf",
               !inOrdnung({3, 2, 2, {0, 1, 0, 0}}));
        // rc184: mehr Felder als Skripte.
        expect("mehr Felder als Reiter fallen auf",
               !inOrdnung({1, 2, 0, {0, 0, 0, 0}}));
    }

    // --- Die Regel, die das Bearbeiten in vier Feldern traegt -----------
    //
    // Gewuenscht: in einem Reiter bis zu vier Skripte gegenueberstellen und
    // an ALLEN arbeiten koennen.
    //
    // So macht es auch Brackets in seiner SplitView, und EasyMDView
    // formuliert die Regel knapp: das aktive Feld ist das zuletzt
    // angeklickte, und die globalen Werkzeuge gelten ihm.
    //
    // Bei uns heisst das: das FOKUSSIERTE Feld muss den AKTIVEN Reiter
    // zeigen. Gilt das nicht, ginge eine Aenderung in ein anderes Skript
    // als das angeklickte - der schlimmste Fehler, den dieses Fenster haben
    // kann, weil er unbemerkt bleibt, bis man die falsche Datei sichert.
    {
        struct Stand { int felder; int fokus; int feldReiter[4]; int aktiv; };
        auto aenderungLandetRichtig = [](const Stand& z) {
            if (z.fokus < 0 || z.fokus >= z.felder) { return false; }
            return z.feldReiter[z.fokus] == z.aktiv;
        };

        expect("Fokus auf Feld 1, das Reiter 2 zeigt, aktiv ist 2",
               aenderungLandetRichtig({2, 1, {0, 2, 0, 0}, 2}));
        expect("und mit vier Feldern ebenso",
               aenderungLandetRichtig({4, 3, {0, 1, 2, 3}, 3}));
        // Der Fehlerfall: der Fokus wanderte, der aktive Reiter nicht.
        expect("laeuft der aktive Reiter davon, faellt es auf",
               !aenderungLandetRichtig({2, 1, {0, 2, 0, 0}, 0}));
        // Und der Umschaltvorgang selbst: nach dem Wechsel auf Feld k muss
        // der aktive Reiter der von Feld k sein.
        auto nachWechsel = [](Stand z, int ziel) {
            z.fokus = ziel;
            z.aktiv = z.feldReiter[ziel];   // genau das tut switchTab
            return z;
        };
        const Stand ausgangslage{3, 0, {5, 6, 7, 0}, 5};
        expect("nach dem Wechsel stimmt es wieder",
               aenderungLandetRichtig(nachWechsel(ausgangslage, 2)));
        expect("und der aktive Reiter ist der des Ziels",
               nachWechsel(ausgangslage, 2).aktiv == 7);
    }

    // --- Arbeitsbereich und bearbeitetes Skript sind ZWEI Fragen --------
    //
    // Gemeldet: "wenn ich ins zweite Fenster klicke, bin ich ploetzlich in
    // einem anderen Tab... dabei sollte er das nur im ersten anzeigen, weil
    // ich die DA gesplittet habe."
    //
    // Genau richtig. Vorher war beides derselbe Wert:
    //
    //   "In welchem Arbeitsbereich bin ich?"  und
    //   "Welches Skript aendere ich gerade?"
    //
    // Ein Klick in ein Vergleichsfeld muss das ZWEITE aendern, ohne das
    // erste anzuruehren. Sonst wandert das Reiterband mit, und die
    // Aufteilung wird in Reiter geschrieben, die nie geteilt wurden.
    {
        struct Welt { int home; int aktiv; int felder; int feldReiter[4]; };
        // Geteilt wurde in Reiter 0, die Felder zeigen 0, 1, 2, 3.
        const Welt bereich{0, 0, 4, {0, 1, 2, 3}};

        auto klickInFeld = [](Welt v, int feld) {
            v.aktiv = v.feldReiter[feld];   // nur das bearbeitete Skript
            return v;                       // der Arbeitsbereich bleibt
        };
        auto klickAufReiter = [](Welt v, int reiter) {
            v.home = reiter;                // BEIDES wandert
            v.aktiv = reiter;
            return v;
        };

        const Welt nachFeld = klickInFeld(bereich, 2);
        std::printf("     nach Klick in Feld 2: Arbeitsbereich %d, "
                    "bearbeitet %d\n", nachFeld.home, nachFeld.aktiv);
        expect("der Arbeitsbereich bleibt Reiter 0", nachFeld.home == 0);
        expect("bearbeitet wird jetzt Reiter 2", nachFeld.aktiv == 2);

        const Welt nachReiter = klickAufReiter(nachFeld, 3);
        expect("ein angeklickter Reiter wird der neue Arbeitsbereich",
               nachReiter.home == 3 && nachReiter.aktiv == 3);

        // Und die Aufteilung gehoert dem Arbeitsbereich - NICHT dem gerade
        // bearbeiteten Reiter. Sonst bekaeme Reiter 2 die vier Felder
        // angehaengt, obwohl dort nie geteilt wurde.
        auto wohinGehoertDieAufteilung = [](const Welt& v) { return v.home; };
        expect("die Aufteilung wird in Reiter 0 gemerkt",
               wohinGehoertDieAufteilung(nachFeld) == 0);
        expect("nicht in den gerade bearbeiteten",
               wohinGehoertDieAufteilung(nachFeld) != nachFeld.aktiv);
    }

    // --- Kein Feld zweimal dasselbe Skript ------------------------------
    //
    // Gemeldet: "es hat es sogar einmal geschafft zu comparen mit demselben
    // script, was nicht gehen sollte".
    //
    // Zwei gleiche Baeume nebeneinander sehen aus wie Absicht - deshalb
    // faellt es kaum auf, und deshalb steht seit rc222 ein Riegel davor.
    // Hier die Reparatur nachgestellt.
    {
        auto entdoppeln = [](std::vector<int> feld, int fokus, int reiter) {
            const int n = static_cast<int>(feld.size());
            for (int i = 0; i < n; ++i) {
                if (i == fokus) { continue; }
                bool doppelt = false;
                for (int j = 0; j < n; ++j) {
                    if (j != i && feld[j] == feld[i]) { doppelt = true; }
                }
                if (!doppelt) { continue; }
                for (int k = 0; k < reiter; ++k) {
                    bool belegt = false;
                    for (int j = 0; j < n; ++j) {
                        if (feld[j] == k) { belegt = true; }
                    }
                    if (!belegt) { feld[i] = k; break; }
                }
            }
            return feld;
        };

        // Der gemeldete Fall: Feld 0 und Feld 1 zeigen beide Reiter 0.
        const std::vector<int> repariert = entdoppeln({0, 0}, 0, 4);
        std::printf("     aus {0,0} wird {%d,%d}\n", repariert[0],
                    repariert[1]);
        expect("das fokussierte Feld behaelt seinen Reiter",
               repariert[0] == 0);
        expect("das andere bekommt einen freien", repariert[1] != 0);

        // Vier Felder, vier Reiter, alles verschieden - nichts aendert sich.
        const std::vector<int> heil = entdoppeln({0, 1, 2, 3}, 1, 4);
        expect("ein gesunder Stand bleibt unangetastet",
               heil[0] == 0 && heil[1] == 1 && heil[2] == 2 && heil[3] == 3);

        // Und der Fall ohne Ausweg: zwei Felder, nur ein Reiter. Dann
        // bleibt es, wie es ist - beschwert wird sich woanders.
        const std::vector<int> eng = entdoppeln({0, 0}, 0, 1);
        expect("ohne freien Reiter bleibt es stehen", eng[1] == 0);
    }

    // --- Ziehen zwischen den Feldern ------------------------------------
    //
    // Nachgesehen, wie ImGui das vorsieht (Fehlerberichte 7539, 8225 und
    // die Doku zu SetDragDropPayload):
    //
    //   * Die QUELLE muss ein Element sein - ein Selectable geht, ein
    //     Kindfenster nicht.
    //   * Die Nutzlast wird SOFORT kopiert. Ein Zeiger darin waere beim
    //     Loslassen ungueltig; also nur kleine Werte hineingeben.
    //   * Ob die Uebergabe geklappt hat, weiss nur das ZIEL. Deshalb wird
    //     dort eingefuegt und nicht in der Quelle.
    //
    // Daraus folgt unsere Nutzlast: Feld, Reiter, Zeile. Diese Probe haelt
    // fest, dass sie klein und selbsttragend ist - der haeufigste Fehler
    // waere, einen Weg oder Zeiger mitzugeben.
    {
        struct DragNode { int pane; int tab; int row; };
        expect("die Nutzlast ist klein und ohne Zeiger",
               sizeof(DragNode) <= 16);

        // Und die Aufloesung im Ziel: aus Feld+Zeile wird der Knoten
        // gesucht, aus dem Zielfeld die Stelle dahinter.
        auto gueltig = [](const DragNode& d, int felder, int reiter,
                          int zeilenQuelle, int nachPane, int nachRow,
                          int zeilenZiel) {
            if (d.pane < 0 || d.pane >= felder) { return false; }
            if (nachPane < 0 || nachPane >= felder) { return false; }
            if (d.tab < 0 || d.tab >= reiter) { return false; }
            if (d.row < 0 || d.row >= zeilenQuelle) { return false; }
            if (nachRow < 0 || nachRow >= zeilenZiel) { return false; }
            return true;
        };

        expect("ein gueltiger Zug geht durch",
               gueltig({0, 0, 5}, 4, 4, 20, 2, 3, 10));
        // Genau diese Faelle treten auf, wenn waehrend des Ziehens ein
        // Reiter geschlossen oder ein Baum neu gebaut wurde.
        expect("eine Zeile, die es nicht mehr gibt, wird abgelehnt",
               !gueltig({0, 0, 99}, 4, 4, 20, 2, 3, 10));
        expect("ein Reiter, den es nicht mehr gibt, ebenso",
               !gueltig({0, 7, 5}, 4, 4, 20, 2, 3, 10));
        expect("ein Feld ausserhalb ebenso",
               !gueltig({9, 0, 5}, 4, 4, 20, 2, 3, 10));
        expect("und eine Zielzeile ausserhalb",
               !gueltig({0, 0, 5}, 4, 4, 20, 2, 99, 10));

        // In dasselbe Feld zu ziehen ist erlaubt - das ist Verdoppeln an
        // anderer Stelle, und dafuer gibt es gute Gruende.
        expect("in dasselbe Feld ziehen ist erlaubt",
               gueltig({1, 1, 2}, 4, 4, 20, 1, 5, 20));
    }

    // --- Eine Karte, mehrere Skripte ------------------------------------
    //
    // Gewuenscht: mehrere Skripte duerfen sich eine Karte teilen, und
    // trotzdem soll ein Reiter seine EIGENE aufmachen koennen.
    //
    // Beides geht ueber eine einzige Unterscheidung: hat der Reiter eine
    // eigene Karte oder nicht?
    {
        struct Reiterchen { bool eigene; std::string karte; };
        auto wechselNoetig = [](const Reiterchen& ziel,
                                const std::string& geladen) {
            // Ohne eigene Karte: die vorhandene bleibt stehen.
            if (!ziel.eigene) { return false; }
            return ziel.karte != geladen;
        };

        const Reiterchen mission{true, "maps/md_am_sith.bsp"};
        const Reiterchen andere{true, "maps/duel_kamino.bsp"};
        const Reiterchen nurSkript{false, ""};

        expect("ein Reiter mit eigener Karte holt sie",
               wechselNoetig(andere, "maps/md_am_sith.bsp"));
        expect("dieselbe Karte wird nicht neu geladen",
               !wechselNoetig(mission, "maps/md_am_sith.bsp"));
        expect("ein Skript ohne eigene Karte teilt sich die vorhandene",
               !wechselNoetig(nurSkript, "maps/md_am_sith.bsp"));
        expect("auch wenn gerade eine andere geladen ist",
               !wechselNoetig(nurSkript, "maps/duel_kamino.bsp"));

        // Und der alte Zustand: dort galt "keine eigene" als "keine
        // Karte", und die vorhandene flog weg - bei jedem Klick, hin und
        // zurueck.
        auto wechselAlt = [](const Reiterchen& ziel,
                             const std::string& geladen) {
            return ziel.karte != geladen;
        };
        expect("vorher warf ein Skript ohne Karte die vorhandene weg",
               wechselAlt(nurSkript, "maps/md_am_sith.bsp"));
    }

    expect("Feld 1 bleibt dabei, wo es war",
           w.reiter[1].doc.script().nodes.size() == 2);

    std::printf("%s (%d Fehlschlaege)\n",
                fehler == 0 ? "alle Fokusproben bestanden" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
