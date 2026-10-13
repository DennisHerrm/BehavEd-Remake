// roffbautest.cpp - Proben fuer das BAUEN von ROFF-Bahnen (bhed/roff.h,
// bhed/roffanim.h)
//
// Gewuenscht (13.10.2026): "ich will das man auch direkt eine Roff datei
// bauen und aendern kann". Geprueft wird:
//
//   * schreiben: eine gelesene Datei kommt Byte fuer Byte wieder heraus
//   * backen: aus Schluesseln wird eine Bahn, die das Spiel (roffStand,
//     Mover-Weg) genau zu den Schluesselzeiten an den Schluesseln zeigt -
//     auch am Ende, obwohl die Engine den letzten Versatz nie zeigt
//   * zerlegen: aus einer dichten Bahn werden wenige Schluessel, aus denen
//     sie sich wieder backen laesst
//   * Notizen bleiben an ihrem Bild
//   * die Markierung im Skript (Schluessel als Kommentarzeile) und der
//     eigene Block mit play ( "PLAY_ROFF", ... )
#include "bhed/edit.h"
#include "bhed/roff.h"
#include "bhed/roffanim.h"
#include "bhed/script.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace bhed;

namespace {

int fehler = 0;

void pruefe(bool ok, const char* was) {
    if (!ok) {
        std::printf("FEHLER: %s\n", was);
        ++fehler;
    } else {
        std::printf("ok: %s\n", was);
    }
}

bool nah(float a, float b, float eps = 0.01F) { return std::fabs(a - b) <= eps; }

RoffSchluessel sk(double ms, float x, float y, float z, float p = 0.0F, float g = 0.0F, float r = 0.0F) {
    RoffSchluessel k;
    k.ms = ms;
    k.ort[0] = x;
    k.ort[1] = y;
    k.ort[2] = z;
    k.winkel[0] = p;
    k.winkel[1] = g;
    k.winkel[2] = r;
    return k;
}

Script lies(const std::string& t) {
    Script s;
    std::vector<Diag> d;
    (void)readScript(t, s, d);
    return s;
}

// --- 1. Schreiben -----------------------------------------------------------
void probeSchreiben() {
    Roff r;
    r.fassung = 2;
    r.bildMs = 50;
    r.lerp = 20;
    r.bilder.resize(3);
    r.bilder[0].ort[0] = 1.5F;
    r.bilder[1].winkel[1] = -2.25F;
    r.bilder[1].ersteNotiz = 0;
    r.bilder[1].notizen = 1;
    r.notizen = {"sound sound/a.wav"};
    const std::string d = schreibeRoff(r);
    pruefe(d.size() == 20U + 3U * 32U + 18U, "Fassung 2: 20 Byte Kopf, 32 je Bild, Notiz mit Endnull");
    pruefe(d.compare(0, 4, "ROFF") == 0, "Kennung ROFF");
    Roff z;
    std::string grund;
    pruefe(leseRoff(d, z, &grund), "die geschriebene Datei liest sich");
    pruefe(z.fassung == 2 && z.bildMs == 50 && z.bilder.size() == 3 && z.notizen.size() == 1, "Kopf kommt zurueck");
    pruefe(nah(z.bilder[0].ort[0], 1.5F) && nah(z.bilder[1].winkel[1], -2.25F), "Versaetze kommen zurueck");
    pruefe(z.bilder[0].ersteNotiz == -1 && z.bilder[1].ersteNotiz == 0 && z.bilder[1].notizen == 1,
           "Notizindex kommt zurueck (-1 = keine)");
    pruefe(schreibeRoff(z) == d, "lesen und wieder schreiben: bytegleich");
}

// --- 2. Backen, gleichmaessig ------------------------------------------------
void probeBackenLinear() {
    const std::vector<RoffSchluessel> k = {sk(0, 0, 0, 0), sk(1000, 100, 0, 0, 0, 90, 0), sk(2000, 100, 200, 0, 0, 90, 30)};
    const Roff r = baueRoff(k, false);
    pruefe(r.fassung == 2 && r.bildMs == 50 && r.lerp == 20, "gebacken: Fassung 2, 50 ms je Bild");
    // 2000 ms in 50-ms-Schritten = 40 Wege, dazu das letzte Bild, das die
    // Engine nie zeigt.
    pruefe(r.bilder.size() == 41U, "41 Bilder fuer 2 s");
    const float start[3] = {500.0F, 600.0F, 70.0F};
    const float w0[3] = {0.0F, 0.0F, 0.0F};
    const RoffBahn b = roffBahn(r, start, w0);
    const double t0 = roffBildZeitMs(r, 0);
    for (const RoffSchluessel& s : k) {
        const RoffStand st = roffStand(r, b, t0 + s.ms, false);
        char z[160];
        std::snprintf(z, sizeof(z), "Mover bei %.0f ms am Schluessel (%.1f %.1f %.1f / %.1f %.1f)", s.ms,
                      static_cast<double>(st.ort[0]), static_cast<double>(st.ort[1]), static_cast<double>(st.ort[2]),
                      static_cast<double>(st.winkel[1]), static_cast<double>(st.winkel[2]));
        pruefe(nah(st.ort[0], start[0] + s.ort[0]) && nah(st.ort[1], start[1] + s.ort[1]) &&
                   nah(st.ort[2], start[2] + s.ort[2]) && nah(st.winkel[1], s.winkel[1]) && nah(st.winkel[2], s.winkel[2]),
               z);
    }
    const RoffStand mitte = roffStand(r, b, t0 + 500.0, false);
    pruefe(nah(mitte.ort[0], start[0] + 50.0F) && nah(mitte.winkel[1], 45.0F), "gleichmaessig: halbe Zeit, halber Weg");
    const RoffStand ende = roffEndstand(r, b, false);
    pruefe(nah(ende.ort[1], start[1] + 200.0F) && nah(ende.winkel[2], 30.0F), "Endstand = letzter Schluessel");
    pruefe(std::fabs(roffLaufzeitMs(r) - (t0 + 2000.0)) < 0.5, "Aufgabe fertig mit dem letzten Schluessel");
    // Ein einzelner Schluessel: eine Datei mit einem Bild (G_ValidRoff will > 0).
    const Roff eins = baueRoff({sk(0, 0, 0, 0)}, false);
    Roff z;
    pruefe(eins.bilder.size() == 1U && leseRoff(schreibeRoff(eins), z), "ein Schluessel: gueltige Datei mit einem Bild");
    // Mehr als eine Umdrehung: Winkel werden NICHT auf den kurzen Weg gelegt.
    const Roff dreh = baueRoff({sk(0, 0, 0, 0), sk(1000, 0, 0, 0, 0, 720, 0)}, false);
    const RoffBahn db = roffBahn(dreh, start, w0);
    pruefe(nah(roffEndstand(dreh, db, false).winkel[1], 720.0F, 0.05F), "zwei volle Umdrehungen bleiben zwei");
}

// --- 3. Backen, weich ----------------------------------------------------------
void probeBackenWeich() {
    const std::vector<RoffSchluessel> k = {sk(0, 0, 0, 0), sk(1000, 100, 0, 0), sk(2000, 100, 100, 0)};
    const Roff r = baueRoff(k, true);
    const float null[3] = {0.0F, 0.0F, 0.0F};
    const RoffBahn b = roffBahn(r, null, null);
    const double t0 = roffBildZeitMs(r, 0);
    bool alle = true;
    for (const RoffSchluessel& s : k) {
        const RoffStand st = roffStand(r, b, t0 + s.ms, false);
        alle = alle && nah(st.ort[0], s.ort[0]) && nah(st.ort[1], s.ort[1]);
    }
    pruefe(alle, "weich: geht genau durch jeden Schluessel");
    const RoffStand frueh = roffStand(r, b, t0 + 100.0, false);
    pruefe(frueh.ort[0] < 10.0F * 0.8F, "weich: faehrt langsam an (nach 10 % der Zeit unter 8 % des Wegs)");
    const RoffStand bogen = roffStand(r, b, t0 + 1500.0, false);
    pruefe(bogen.ort[0] > 100.5F || bogen.ort[1] > 50.5F || bogen.ort[1] < 49.5F, "weich: zwischen den Schluesseln ein Bogen, keine Gerade");
}

// --- 4. Zerlegen ----------------------------------------------------------------
void probeZerlegen() {
    const std::vector<RoffSchluessel> k = {sk(0, 0, 0, 0), sk(1000, 100, 0, 0, 0, 90, 0), sk(2500, 100, 200, 50, 10, 90, 0)};
    const Roff r = baueRoff(k, false);
    const float start[3] = {10.0F, 20.0F, 30.0F};
    const float w0[3] = {0.0F, 45.0F, 0.0F};
    const std::vector<RoffSchluessel> z = roffSchluessel(r, start, w0);
    char t[120];
    std::snprintf(t, sizeof(t), "gerade Stuecke: genau die drei Schluessel zurueck (%d)", static_cast<int>(z.size()));
    pruefe(z.size() == 3U, t);
    if (z.size() == 3U) {
        pruefe(std::fabs(z[1].ms - 1000.0) < 0.5 && std::fabs(z[2].ms - 2500.0) < 0.5, "Zeiten zurueck");
        pruefe(nah(z[1].ort[0], 110.0F) && nah(z[2].ort[1], 220.0F) && nah(z[2].ort[2], 80.0F), "Orte absolut (Start + Versatz)");
        pruefe(nah(z[1].winkel[1], 135.0F) && nah(z[2].winkel[0], 10.0F), "Winkel absolut");
    }
    const RoffZerlegung zg = zerlegeRoff(r, start, w0);
    pruefe(!zg.weich && zg.schluessel.size() == 3U, "zerlegen waehlt bei geraden Stuecken gleichmaessig (3 Schluessel)");
    // Eine weiche Bahn (beschleunigt und bremst wie die aus 3ds max): mit
    // geraden Stuecken braeuchte sie fast jedes Bild - weich zerlegt wenige
    // Schluessel, und neu gebacken bleibt sie dicht dran.
    const Roff w = baueRoff({sk(0, 0, 0, 0), sk(1000, 300, 0, 0), sk(2000, 300, 300, 0), sk(3000, 0, 300, 100)}, true);
    const float null[3] = {0.0F, 0.0F, 0.0F};
    const RoffZerlegung zz = zerlegeRoff(w, null, null);
    const std::vector<RoffSchluessel>& zw = zz.schluessel;
    const Roff neu = baueRoff(zw, zz.weich);
    const RoffBahn ba = roffBahn(w, null, null);
    const RoffBahn bb = roffBahn(neu, null, null);
    float weit = 0.0F;
    for (double ms = 0.0; ms <= 3200.0; ms += 25.0) {
        const RoffStand a = roffStand(w, ba, ms, false);
        const RoffStand b = roffStand(neu, bb, ms, false);
        for (int i = 0; i < 3; ++i) { weit = std::max(weit, std::fabs(a.ort[i] - b.ort[i])); }
    }
    std::snprintf(t, sizeof(t), "weiche Bahn: weich, %d Schluessel, neu gebacken hoechstens 1 Einheit daneben (%.2f)",
                  static_cast<int>(zw.size()), static_cast<double>(weit));
    pruefe(zz.weich && zw.size() >= 4U && zw.size() <= 6U && weit <= 1.0F, t);
}

// --- 5. Notizen ----------------------------------------------------------------
void probeNotizen() {
    std::vector<RoffNotiz> n = {{500.0, "effect explosions/big"}, {1500.0, "sound sound/boom.wav"}};
    const Roff r = baueRoff({sk(0, 0, 0, 0), sk(2000, 100, 0, 0)}, false, 50, n);
    const std::vector<RoffAusloesung> a = roffAusloesungen(r);
    pruefe(a.size() == 2U && a[0].bild == 10U && a[1].bild == 30U, "Notizen an Bild 10 und 30 (500 / 1500 ms)");
    const std::vector<RoffNotiz> zur = roffNotizen(r);
    pruefe(zur.size() == 2U && zur[0].text == n[0].text && std::fabs(zur[1].ms - 1500.0) < 0.5, "Notizen kommen zurueck");
}

// --- 6. Die Markierung im Skript -------------------------------------------------
void probeMarke() {
    RoffAnim a;
    a.datei = "md_test/roffs/schiff";
    a.weich = true;
    a.schluessel = {sk(0, 0, 0, 0), sk(1500, 120.5F, -3, 4, 1, 2, 3)};
    a.notizen = {{250.0, "effect \"x\" 1+2+3"}};
    const std::string m = roffMarke(a);
    pruefe(m.rfind("//(BehavEd) roff \"md_test/roffs/schiff\"", 0) == 0, "Marke beginnt mit //(BehavEd) roff \"datei\"");
    RoffAnim z;
    pruefe(leseRoffMarke(m, z), "Marke liest sich");
    pruefe(z.datei == a.datei && z.weich && z.schluessel.size() == 2U && nah(z.schluessel[1].ort[0], 120.5F) &&
               nah(z.schluessel[1].winkel[2], 3.0F) && std::fabs(z.schluessel[1].ms - 1500.0) < 0.01,
           "Schluessel kommen zurueck");
    pruefe(z.notizen.size() == 1U && z.notizen[0].text == a.notizen[0].text, "Notiz mit Anfuehrungszeichen kommt zurueck");
    pruefe(!leseRoffMarke("//(BehavEd) animation \"x\"", z), "andere Kommentare sind keine Marke");

    // Der eigene Block.
    const Script leer = lies("rem ( \"vorher\" );\n");
    const Script s = mitRoffBlock(leer, "schiff", 1450.0, &a);
    const std::string text = writeScript(s);
    pruefe(text.find("\"PLAY_ROFF\", \"md_test/roffs/schiff\" );") != std::string::npos, "Block ruft PLAY_ROFF");
    pruefe(text.find("wait ( 1450.000 );") != std::string::npos, "Block wartet bis zum Start");
    pruefe(s.nodes.size() == 3U && s.nodes[1].name == "affect", "Block steht am Anfang (Marke, affect, dann der Rest)");
    const auto at = findeRoffBlock(s, "schiff");
    RoffAnim gelesen;
    double play = 0.0;
    pruefe(at.has_value() && leseRoffBlock(s, *at, gelesen, play) && std::fabs(play - 1450.0) < 0.01 &&
               gelesen.schluessel.size() == 2U,
           "Block liest sich zurueck (Start 1450 ms)");
    const Script zweimal = mitRoffBlock(lies(writeScript(s)), "schiff", 200.0, &a);
    pruefe(zweimal.nodes.size() == 3U && writeScript(zweimal).find("wait ( 200.000 );") != std::string::npos,
           "neu schreiben ersetzt den Block, statt einen zweiten anzulegen");
    const Script weg = mitRoffBlock(zweimal, "schiff", 0.0, nullptr);
    pruefe(weg.nodes.size() == 1U, "ohne Bahn faellt der Block weg");
    // Die Entity heisst inzwischen anders: nach der Datei gefunden und mit dem
    // neuen Namen ersetzt - kein zweiter Block (rc599).
    const auto beiDatei = findeRoffBlockNachDatei(zweimal, "MD_TEST/roffs/schiff");
    pruefe(beiDatei.has_value() && *beiDatei == 0U && !findeRoffBlock(zweimal, "schiff2").has_value(),
           "nach der Datei gefunden, auch wenn die Entity anders heisst");
    const Script umbenannt = mitRoffBlockBei(zweimal, *beiDatei, "schiff2", 200.0, &a);
    pruefe(umbenannt.nodes.size() == 3U && umbenannt.nodes[1].args[0].text == "schiff2",
           "ersetzt an seiner Stelle, affect auf den neuen Namen");

    // Eine Marke vor einem fremden play irgendwo im Skript.
    const Script fremd = lies("affect ( \"stein\", FLUSH )\n{\n\twait ( 100.000 );\n"
                              "\tplay ( \"PLAY_ROFF\", \"kor2/roffs/boulder\" );\n}\n");
    RoffAnim b;
    b.datei = "kor2/roffs/boulder_2";
    b.schluessel = {sk(0, 0, 0, 0), sk(500, 0, 0, -40)};
    const Script mit = mitRoffMarkeVorPlay(fremd, Path{0, 1}, b);
    const std::vector<RoffMarkeFund> f = alleRoffMarken(mit);
    pruefe(f.size() == 1U && f[0].anim.datei == "kor2/roffs/boulder_2" && f[0].path == Path({0, 1}),
           "Marke vor dem play gefunden (im affect-Block)");
    const Node* p = nodeAt(mit, Path{0, 2});
    pruefe(p != nullptr && p->name == "play" && p->args.size() >= 2 && p->args[1].text == "kor2/roffs/boulder_2",
           "das play zeigt auf die neue Datei");
    const auto gefunden = findeRoffMarke(mit, "KOR2/roffs/Boulder_2");
    pruefe(gefunden.has_value(), "Suche nach der Datei ohne Gross/Klein");
    const Script nochmal = mitRoffMarkeVorPlay(mit, Path{0, 2}, b);
    pruefe(alleRoffMarken(nochmal).size() == 1U, "zweites Schreiben ersetzt die Marke");
    const Roff gebacken = roffAusAnim(b);
    const float null[3] = {0.0F, 0.0F, 0.0F};
    pruefe(nah(roffEndstand(gebacken, roffBahn(gebacken, null, null), false).ort[2], -40.0F), "aus der Marke gebacken");
}

}  // namespace

int main() {
    probeSchreiben();
    probeBackenLinear();
    probeBackenWeich();
    probeZerlegen();
    probeNotizen();
    probeMarke();
    std::printf("%s\n", fehler == 0 ? "alle Proben bestanden" : "FEHLGESCHLAGEN");
    return fehler == 0 ? 0 : 1;
}
