// Probe fuer bhed/saberkontakt.h (Paket saber, rc601): Lichtschwerter an
// Waenden und gegeneinander, wie WP_SaberDamageTrace und CG_AddSaberBladeGo.
//
// Gemeldet: "There are no saber collisions". Die Proben halten die Rechnungen
// der Engine fest - auch dort, wo sie seltsam sind (tri_tri_intersect mit
// umgedrehter Pruefung, die Naeherung in G_FindClosestPointOnLineSegment).
#include "bhed/saberkontakt.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace bhed;

namespace {
int g_fehler = 0;
void pruefe(bool ok, const std::string& was) {
    std::printf("%s %s\n", ok ? "OK    " : "FEHLER", was.c_str());
    if (!ok) { ++g_fehler; }
}
bool nah(float a, float b, float eps = 0.01F) { return std::fabs(a - b) <= eps; }

// Eine Wand: eine Scheibe von x = wandX bis wandX + 4 ist fest (oder
// Wasser/Himmel, je nach inhalt/flaeche). Wie gi.trace fuer eine Strecke
// ohne Kasten.
SaberSpurFn wand(float wandX, std::uint32_t inhalt = kInhaltFest, std::uint32_t flaeche = 0) {
    const float x0 = wandX;
    const float x1 = wandX + 4.0F;
    return [=](const float a[3], const float b[3], std::uint32_t maske) {
        SaberSpur t;
        for (int i = 0; i < 3; ++i) { t.endpos[i] = b[i]; }
        if ((inhalt & maske) == 0U) { return t; }
        const auto drin = [&](float x) { return x >= x0 && x <= x1; };
        if (drin(a[0])) {
            t.startsolid = true;
            t.allsolid = drin(b[0]);
            t.fraction = t.allsolid ? 0.0F : 1.0F;
            t.inhalt = inhalt;
            return t;
        }
        float f = 2.0F;
        float n = 0.0F;
        if (a[0] < x0 && b[0] >= x0) {
            f = (x0 - a[0]) / (b[0] - a[0]);
            n = -1.0F;
        } else if (a[0] > x1 && b[0] <= x1) {
            f = (a[0] - x1) / (a[0] - b[0]);
            n = 1.0F;
        }
        if (f > 1.0F) { return t; }
        t.fraction = f;
        for (int i = 0; i < 3; ++i) { t.endpos[i] = a[i] + f * (b[i] - a[i]); }
        t.normale[0] = n;
        t.inhalt = inhalt;
        t.flaeche = flaeche;
        return t;
    };
}

SaberFigur kinoFigur(int nr, float ox, float oy) {
    SaberFigur f;
    f.figur = nr;
    f.verhalten = "BS_CINEMATIC";
    f.saberKollisionen = true;
    f.ort[0] = ox;
    f.ort[1] = oy;
    f.schwert[0].typ = "SABER_SINGLE";
    f.schwert[0].numBlades = 1;
    f.schwert[0].klinge[0].aktiv = true;
    f.schwert[0].klinge[0].laengeMax = 40.0F;
    f.schwert[0].klinge[0].laenge = 40.0F;
    f.schwert[0].klinge[0].laengeAlt = 40.0F;
    return f;
}
void lage(SaberKlinge& k, float x, float y, float z, float dx, float dy, float dz) {
    k.mp[0] = x;
    k.mp[1] = y;
    k.mp[2] = z;
    k.md[0] = dx;
    k.md[1] = dy;
    k.md[2] = dz;
}

// Zwei Bilder: A steht still quer (y), B faehrt von oben (z 60) nach unten
// (z 30) quer (x) durch A hindurch. Liefert die Klirren des zweiten Bildes.
std::vector<SaberKlirren> durchSchlag(std::vector<SaberFigur> f) {
    std::vector<SaberKlirren> aus;
    lage(f[0].schwert[0].klinge[0], 0.0F, -20.0F, 40.0F, 0.0F, 1.0F, 0.0F);
    lage(f[1].schwert[0].klinge[0], 20.0F, 0.0F, 60.0F, -1.0F, 0.0F, 0.0F);
    saberServerTakt(f, 450.0, SaberSpurFn{}, aus);
    aus.clear();
    lage(f[0].schwert[0].klinge[0], 0.0F, -20.0F, 40.0F, 0.0F, 1.0F, 0.0F);
    lage(f[1].schwert[0].klinge[0], 20.0F, 0.0F, 30.0F, -1.0F, 0.0F, 0.0F);
    saberServerTakt(f, 500.0, SaberSpurFn{}, aus);
    return aus;
}
}  // namespace

int main() {
    // --- ShortestLineSegBewteen2LineSegs ---------------------------------
    {
        float p1[3];
        float p2[3];
        const float a0[3] = {-10, 0, 0};
        const float a1[3] = {10, 0, 0};
        const float b0[3] = {0, -10, 0};
        const float b1[3] = {0, 10, 0};
        const float d = kuerzesterAbstand(a0, a1, b0, b1, p1, p2);
        pruefe(nah(d, 0.0F) && nah(p1[0], 0.0F) && nah(p2[1], 0.0F), "Kreuz: Abstand 0 im Ursprung");
        const float c0[3] = {0, 5, 0};
        const float c1[3] = {20, 5, 0};
        // Parallel: denom 0, es bleiben die Endpunkte und die vier Punkt-
        // Strecken-Proben - mit der Naeherung von G_FindClosestPointOnLineSegment
        // kommt 5.10 heraus, nicht 5 (so rechnet die Engine).
        const float dp = kuerzesterAbstand(a0, a1, c0, c1, p1, p2);
        pruefe(dp >= 5.0F && dp < 5.2F, "parallel 5 auseinander: " + std::to_string(dp) + " (Engine-Naeherung)");
        const float s0[3] = {0, 0, 0};
        const float s1[3] = {10, 0, 0};
        const float t0[3] = {5, -5, 3};
        const float t1[3] = {5, 5, 3};
        pruefe(nah(kuerzesterAbstand(s0, s1, t0, t1, p1, p2), 3.0F), "windschief: 3");
    }
    // --- G_FindClosestPointOnLineSegment mit seiner Naeherung -------------
    {
        const float a[3] = {0, 0, 0};
        const float b[3] = {10, 0, 0};
        const float von[3] = {5, 5, 0};
        float r[3];
        const bool innen = naechsterPunktAufStrecke(a, b, von, r);
        // theta = 90*(1-0.7071) = 26.36 Grad, cos = 0.8961, 7.071*0.8961 =
        // 6.336 vom Ende: x = 3.664 - nicht 5, so rechnet die Engine.
        pruefe(innen && nah(r[0], 3.664F, 0.01F), "naechster Punkt wie q_math.c (Naeherung, x 3.664)");
        const float hinter[3] = {-3, 2, 0};
        (void)naechsterPunktAufStrecke(a, b, hinter, r);
        pruefe(nah(r[0], 0.0F), "naechster Punkt hinter dem Anfang: der Anfang");
    }
    // --- tri_tri_intersect, Raven-Fassung ---------------------------------
    {
        const float a0[3] = {-1, -1, 0};
        const float a1[3] = {1, -1, 0};
        const float a2[3] = {0, 1, 0};
        // Senkrecht in x = 0, weit weg in y: die Ebenen schneiden beide
        // Dreiecke, die Intervalle auf der Schnittgeraden sind getrennt.
        const float b0[3] = {0, 5, -1};
        const float b1[3] = {0, 5, 1};
        const float b2[3] = {0, 7, 0};
        pruefe(dreieckeSchneiden(a0, a1, a2, b0, b1, b2),
               "tri_tri_intersect: getrennte Intervalle -> true (umgedrehte Pruefung wie im Spiel)");
        const float c0[3] = {0, 0, -1};
        const float c1[3] = {0, 0, 1};
        const float c2[3] = {0, 2, 0};
        pruefe(!dreieckeSchneiden(a0, a1, a2, c0, c1, c2),
               "tri_tri_intersect: echte Durchdringung -> false (wie im Spiel)");
        const float d0[3] = {-1, -1, 5};
        const float d1[3] = {1, -1, 5};
        const float d2[3] = {0, 1, 5};
        pruefe(!dreieckeSchneiden(a0, a1, a2, d0, d1, d2), "parallel uebereinander: false");
    }
    // --- Kasten (CM_TraceThroughBrush) -------------------------------------
    {
        const float mins[3] = {-5, -5, -5};
        const float maxs[3] = {5, 5, 5};
        const float null3[3] = {0, 0, 0};
        const float s[3] = {-20, 0, 0};
        const float e[3] = {20, 0, 0};
        SaberSpur t = kastenSpur(s, e, null3, null3, mins, maxs);
        pruefe(nah(t.fraction, (15.0F - 0.125F) / 40.0F, 1e-5F) && t.normale[0] == -1.0F,
               "Kasten: Anteil (15-0.125)/40, Normale -x");
        const float innen[3] = {0, 0, 0};
        t = kastenSpur(innen, e, null3, null3, mins, maxs);
        pruefe(t.startsolid && !t.allsolid && t.fraction == 1.0F, "Kasten: innen begonnen, aussen geendet");
        const float innen2[3] = {2, 0, 0};
        t = kastenSpur(innen, innen2, null3, null3, mins, maxs);
        pruefe(t.allsolid && t.fraction == 0.0F, "Kasten: ganz innen -> allsolid");
        t = kastenSpur(innen, innen, null3, null3, mins, maxs);
        pruefe(t.allsolid, "Kasten: Lagetest (Anfang = Ende) innen");
        const float r2[3] = {2, 2, 2};
        const float rm2[3] = {-2, -2, -2};
        const float s2[3] = {-20, 6, 0};
        const float e2[3] = {20, 6, 0};
        pruefe(kastenSpur(s2, e2, null3, null3, mins, maxs).fraction == 1.0F &&
                   kastenSpur(s2, e2, rm2, r2, mins, maxs).fraction < 1.0F,
               "Kasten: Spur mit +-2 trifft, wo der Punkt vorbeigeht");
    }
    // --- Schwertkasten (WP_SaberUpdate) ------------------------------------
    {
        setzeSaberMovieDuelsRegeln(true);
        SaberFigur f = kinoFigur(0, 0, 0);
        lage(f.schwert[0].klinge[0], 0.0F, 0.0F, 40.0F, 1.0F, 0.0F, 0.0F);
        f.schwert[0].klinge[0].laenge = 32.0F;
        saberKastenSetzen(f);
        pruefe(f.kasten.fest && nah(f.kasten.ort[0], 16.0F) && nah(f.kasten.mins[0], -24.0F) &&
                   nah(f.kasten.maxs[0], 24.0F) && nah(f.kasten.mins[1], -16.0F) && nah(f.kasten.maxs[2], 16.0F),
               "Kasten um die Klinge: Mitte 16, x +-24, sonst +-16");
        setzeSaberMovieDuelsRegeln(false);
        SaberFigur g = f;
        g.verhalten.clear();
        saberKastenSetzen(g);
        pruefe(g.kasten.fest && nah(g.kasten.ort[0], 12.0F) && nah(g.kasten.mins[0], -24.0F) &&
                   nah(g.kasten.maxs[2], 48.0F),
               "OpenJK, kein Kino: Kasten um den Koerper, 12 vor ihm (realistic 0)");
        setzeSaberMovieDuelsRegeln(true);
        g.verhalten.clear();
        saberKastenSetzen(g);
        pruefe(g.kasten.fest && nah(g.kasten.ort[0], 16.0F), "Movie Duels (realistic 1): Kasten um die Klinge");
        g.unsichtbar = true;
        saberKastenSetzen(g);
        pruefe(!g.kasten.fest, "Movie Duels: SET_INVISIBLE -> kein Schwertkasten");
    }
    // --- rc601 integ: Ausfahrrate fuer Zeichnung und Zuendklang -------------
    // Das Summen (Paket ton, tonKlingen) rechnete fest mit /10; jetzt nehmen
    // beide saberZuendTeiler: Movie Duels SS_FAST (1) /5, sonst /10.
    {
        setzeSaberMovieDuelsRegeln(true);
        pruefe(saberZuendTeiler(1) == 5.0F && saberZuendTeiler(2) == 10.0F && saberZuendTeiler(7) == 10.0F &&
                   saberZuendTeiler(0) == 10.0F,
               "MD: SS_FAST zuendet mit lengthMax/5, alle anderen /10");
        setzeSaberMovieDuelsRegeln(false);
        pruefe(saberZuendTeiler(1) == 10.0F && saberZuendTeiler(3) == 10.0F, "OpenJK: immer lengthMax/10");
        setzeSaberMovieDuelsRegeln(true);
    }
    // --- G_InCinematicSaberAnim ---------------------------------------------
    {
        setzeSaberMovieDuelsRegeln(true);
        SaberFigur f = kinoFigur(0, 0, 0);
        pruefe(kinoSchwertAnim(f), "MD: BS_CINEMATIC + SET_SABERCOLLISIONS");
        f.verhalten = "BS_DEFAULT";
        pruefe(!kinoSchwertAnim(f), "MD: ohne BS_CINEMATIC nicht");
        f.verhalten = "BS_CINEMATIC";
        f.saberKollisionen = false;
        pruefe(!kinoSchwertAnim(f), "MD: ohne SET_SABERCOLLISIONS nicht");
        f.torsoAnim = "BOTH_CIN_16";
        pruefe(kinoSchwertAnim(f), "BOTH_CIN_16 in BS_CINEMATIC");
        setzeSaberMovieDuelsRegeln(false);
        f.torsoAnim.clear();
        f.saberKollisionen = true;
        pruefe(!kinoSchwertAnim(f), "OpenJK kennt SET_SABERCOLLISIONS nicht");
        setzeSaberMovieDuelsRegeln(true);
    }
    // --- Klingen gegeneinander (WP_SaberDamageTrace) -----------------------
    {
        setzeSaberMovieDuelsRegeln(true);
        std::vector<SaberFigur> f = {kinoFigur(0, 0, -40), kinoFigur(1, 40, 0)};
        const std::vector<SaberKlirren> k = durchSchlag(f);
        pruefe(k.size() == 1U, "Durchschlag: genau ein Klirren (" + std::to_string(k.size()) + ")");
        if (!k.empty()) {
            pruefe(k[0].figur == 1 && k[0].gegner == 0 && k[0].ms == 500.0, "... von B gegen A im zweiten Bild");
            pruefe(k[0].effekt == "saber/saber_block", "... Effekt saber/saber_block");
            pruefe(k[0].klang.rfind("sound/weapons/saber/saberblock", 0) == 0 &&
                       k[0].klang.find(".mp3") != std::string::npos,
                   "... Klang saberblock%d.mp3 (MD 1..90): " + k[0].klang);
            pruefe(k[0].normale[0] == 0.0F && k[0].normale[1] == 0.0F && k[0].normale[2] == 0.0F,
                   "... Normale geloescht (0,0,0) wie saberHitNormal");
            pruefe(nah(k[0].klangOrt[0], 40.0F) && k[0].blitz, "... Klang am Ursprung von B, Blitz an");
            // saberHitLocation = tr.endpos: der Eintritt in den Kasten von A.
            pruefe(std::fabs(k[0].ort[2] - 56.0F) < 1.0F && std::fabs(k[0].ort[0]) <= 16.5F,
                   "... Ort: Eintritt in den Schwertkasten von A (z ~56)");
        }
        const std::vector<SaberKlirren> k2 = durchSchlag(f);
        pruefe(k2.size() == k.size() && (k.empty() || k2[0].klang == k[0].klang), "gleiche Eingabe, gleiches Ergebnis");
        std::vector<SaberFigur> ohne = f;
        ohne[1].saberKollisionen = false;
        pruefe(durchSchlag(ohne).empty(), "MD: ohne SET_SABERCOLLISIONS beim Angreifer kein Klirren");
        std::vector<SaberFigur> ohneKino = f;
        ohneKino[1].verhalten = "BS_DEFAULT";
        pruefe(durchSchlag(ohneKino).empty(), "nicht BS_CINEMATIC: kein Klirren");
        std::vector<SaberFigur> fx = f;
        fx[1].schwert[0].werte.blockKlang[0] = "sound/a.wav";
        fx[1].schwert[0].werte.blockEffekt = "saber/eigen";
        fx[1].schwert[0].werte.keinBlitz = true;
        const std::vector<SaberKlirren> k3 = durchSchlag(fx);
        pruefe(k3.size() == 1U && k3[0].effekt == "saber/eigen" && !k3[0].blitz &&
                   (k3[0].klang == "sound/a.wav" || k3[0].klang.empty()),
               ".sab blockEffect/blockSound1/noClashFlare");
        setzeSaberMovieDuelsRegeln(false);
        pruefe(durchSchlag(f).empty(), "OpenJK: SET_SABERCOLLISIONS wirkt nicht");
        std::vector<SaberFigur> cin = f;
        cin[0].torsoAnim = cin[1].torsoAnim = "BOTH_CIN_16";
        const std::vector<SaberKlirren> k4 = durchSchlag(cin);
        pruefe(k4.size() == 1U && !k4.empty() && k4[0].klang.find(".wav") != std::string::npos,
               "OpenJK: BOTH_CIN_16 klirrt, saberblock%d.wav (1..9)");
        setzeSaberMovieDuelsRegeln(true);
        // Zu weit auseinander: B faehrt 60 Einheiten neben A vorbei.
        std::vector<SaberFigur> weit = f;
        weit[1].ort[1] = 60.0F;
        std::vector<SaberKlirren> aus;
        lage(weit[0].schwert[0].klinge[0], 0.0F, -20.0F, 40.0F, 0.0F, 1.0F, 0.0F);
        lage(weit[1].schwert[0].klinge[0], 20.0F, 60.0F, 60.0F, -1.0F, 0.0F, 0.0F);
        saberServerTakt(weit, 450.0, SaberSpurFn{}, aus);
        lage(weit[0].schwert[0].klinge[0], 0.0F, -20.0F, 40.0F, 0.0F, 1.0F, 0.0F);
        lage(weit[1].schwert[0].klinge[0], 20.0F, 60.0F, 30.0F, -1.0F, 0.0F, 0.0F);
        aus.clear();
        saberServerTakt(weit, 500.0, SaberSpurFn{}, aus);
        pruefe(aus.empty(), "vorbei: kein Klirren");
        // Eine Wand zwischen den beiden haelt die Spur auf.
        aus.clear();
        std::vector<SaberFigur> mitWand = f;
        lage(mitWand[0].schwert[0].klinge[0], 0.0F, -20.0F, 40.0F, 0.0F, 1.0F, 0.0F);
        lage(mitWand[1].schwert[0].klinge[0], 20.0F, 0.0F, 60.0F, -1.0F, 0.0F, 0.0F);
        saberServerTakt(mitWand, 450.0, SaberSpurFn{}, aus);
        lage(mitWand[0].schwert[0].klinge[0], 0.0F, -20.0F, 40.0F, 0.0F, 1.0F, 0.0F);
        lage(mitWand[1].schwert[0].klinge[0], 20.0F, 0.0F, 30.0F, -1.0F, 0.0F, 0.0F);
        aus.clear();
        const SaberSpurFn boden = [](const float a[3], const float b[3], std::uint32_t) {
            SaberSpur t;
            for (int i = 0; i < 3; ++i) { t.endpos[i] = b[i]; }
            if (a[2] > 58.0F && b[2] < 58.0F) {
                t.fraction = (a[2] - 58.0F) / (a[2] - b[2]);
                t.inhalt = kInhaltFest;
                t.normale[2] = 1.0F;
            }
            return t;
        };
        saberServerTakt(mitWand, 500.0, boden, aus);
        pruefe(aus.empty(), "eine Platte ueber A haelt den Schlag auf");
        // Eine Klinge mit Laenge 1 klirrt nicht.
        std::vector<SaberFigur> kurz = f;
        kurz[1].schwert[0].klinge[0].laenge = 1.0F;
        kurz[1].schwert[0].klinge[0].laengeAlt = 1.0F;
        pruefe(durchSchlag(kurz).empty(), "Klinge mit Laenge 1: nichts");
    }
    // --- Wand (CG_AddSaberBladeGo) ------------------------------------------
    {
        setzeSaberMovieDuelsRegeln(true);
        SaberWandKlinge k;
        k.laenge = 32.0F;
        SaberWandFigur fig;
        SaberWandEingabe in;
        in.org[2] = 40.0F;
        in.achse[0] = 1.0F;
        in.wurzelOrt[0] = -10.0F;
        in.wurzelOrt[2] = 24.0F;
        in.laengeMax = 32.0F;
        in.aktiv = true;
        const SaberSpurFn w = wand(20.0F);
        std::vector<SaberWandEreignis> ev;
        in.ms = 8.0;
        in.saat = 1;
        SaberWandBild b = saberWandTakt(k, fig, in, w, ev);
        pruefe(b.gezeichnet && nah(b.zeichenLaenge, 32.0F), "erstes Bild an der Wand zeichnet noch voll (32)");
        pruefe(nah(k.laenge, 32.0F * 20.0F / 33.0F, 0.001F), "... und kuerzt die Laenge auf 32*20/33");
        int funken = 0;
        int klaenge = 0;
        int spuren = 0;
        double letzter = -1000.0;
        bool abstandOk = true;
        for (int i = 2; i <= 126; ++i) {
            in.ms = 8.0 * i;
            in.saat = static_cast<std::uint64_t>(i);
            ev.clear();
            b = saberWandTakt(k, fig, in, w, ev);
            for (const SaberWandEreignis& e : ev) {
                if (e.art == SaberWandEreignis::Art::Funken) {
                    ++funken;
                    if (!nah(e.ort[0], 20.0F) || e.normale[0] != -1.0F || e.name != "sparks/spark_nosnd") {
                        abstandOk = false;
                    }
                }
                if (e.art == SaberWandEreignis::Art::Spur) { ++spuren; }
                if (e.art == SaberWandEreignis::Art::Klang) {
                    ++klaenge;
                    if (e.ms - letzter < 100.0 || e.name.rfind("sound/weapons/saber/saberhitwall", 0) != 0) {
                        abstandOk = false;
                    }
                    letzter = e.ms;
                }
            }
        }
        pruefe(b.zeichenLaenge >= 19.0F && b.zeichenLaenge <= 20.0F,
               "nach 1 s steht die gezeichnete Spitze knapp vor der Wand (" + std::to_string(b.zeichenLaenge) + ")");
        pruefe(funken >= 15 && funken <= 50, "Funken etwa jedes vierte Bild (MD): " + std::to_string(funken));
        pruefe(klaenge >= 3 && klaenge <= 10 && abstandOk,
               "saberhitwall etwa 1 in 4, mindestens 100 ms auseinander: " + std::to_string(klaenge));
        pruefe(spuren == 125, "Brandspur ab dem zweiten Bild: " + std::to_string(spuren));
        // Nach dem Weggehen waechst sie mit lengthMax je Sekunde nach.
        const SaberSpurFn frei = wand(1000.0F);
        for (int i = 0; i < 125; ++i) {
            ev.clear();
            b = saberWandTakt(k, fig, in, frei, ev);
        }
        pruefe(nah(b.zeichenLaenge, 32.0F) && nah(k.laenge, 32.0F) && !k.hatAltePos,
               "ohne Wand nach 1 s wieder voll");
        // Hinter der Wand: Laenge 0.1, gezeichnet werden 8.1 + Wachstum.
        SaberWandEingabe hinter = in;
        hinter.wurzelOrt[0] = 30.0F;
        ev.clear();
        (void)saberWandTakt(k, fig, hinter, w, ev);
        pruefe(nah(k.laenge, 0.1F) && ev.empty(), "Griff hinter der Wand: Laenge 0.1, keine Ereignisse");
        b = saberWandTakt(k, fig, hinter, w, ev);
        pruefe(nah(b.zeichenLaenge, 0.1F + 0.256F + 8.0F, 0.001F), "... das naechste Bild zeichnet L+8");
        // Himmel (SURF_NOIMPACT): gekuerzt, aber keine Funken.
        SaberWandKlinge kh;
        kh.laenge = 32.0F;
        int himmelFunken = 0;
        for (int i = 0; i < 60; ++i) {
            ev.clear();
            in.saat = static_cast<std::uint64_t>(i + 500);
            (void)saberWandTakt(kh, fig, in, wand(20.0F, kInhaltFest, kFlaecheKeinEinschlag), ev);
            for (const auto& e : ev) { himmelFunken += e.art == SaberWandEreignis::Art::Funken ? 1 : 0; }
        }
        pruefe(himmelFunken == 0 && kh.laenge < 20.0F, "SURF_NOIMPACT: gekuerzt, keine Funken");
        // noIdleEffect: keine Funken/Spuren/Klaenge, gekuerzt wird trotzdem.
        SaberWandKlinge kl;
        kl.laenge = 32.0F;
        SaberWandEingabe leer = in;
        leer.keinLeerlauf = true;
        int leerEv = 0;
        for (int i = 0; i < 60; ++i) {
            ev.clear();
            leer.saat = static_cast<std::uint64_t>(i + 900);
            (void)saberWandTakt(kl, fig, leer, w, ev);
            leerEv += static_cast<int>(ev.size());
        }
        pruefe(leerEv == 0 && kl.laenge < 20.0F, "noIdleEffect im Leerlauf: still, aber gekuerzt");
        // ... im Sonderangriff doch, mit saberstrikewall (MD).
        SaberWandEingabe angriff = leer;
        angriff.angriff = true;
        bool strike = false;
        SaberWandFigur fig2;
        for (int i = 0; i < 125; ++i) {
            ev.clear();
            angriff.ms = 2000.0 + 8.0 * i;
            angriff.saat = static_cast<std::uint64_t>(i + 1300);
            (void)saberWandTakt(kl, fig2, angriff, w, ev);
            for (const auto& e : ev) {
                if (e.art == SaberWandEreignis::Art::Klang && e.name.find("saberstrikewall") != std::string::npos) {
                    strike = true;
                }
            }
        }
        pruefe(strike, "Sonderangriff: saberstrikewall%d.mp3 (MD)");
        // Wasser: kochen (1 in 11), nicht gekuerzt.
        SaberWandKlinge kw;
        kw.laenge = 32.0F;
        int kochen = 0;
        for (int i = 0; i < 250; ++i) {
            ev.clear();
            in.saat = static_cast<std::uint64_t>(i + 3000);
            (void)saberWandTakt(kw, fig, in, wand(20.0F, kInhaltWasser), ev);
            for (const auto& e : ev) { kochen += e.art == SaberWandEreignis::Art::Kochen ? 1 : 0; }
        }
        pruefe(kochen >= 8 && kochen <= 45 && nah(kw.laenge, 32.0F), "Wasser: saber/boil etwa 1 in 11, nicht gekuerzt: " +
                                                                        std::to_string(kochen));
        // OpenJK: Funken Q_irand(1,numBlades)==1 - bei einer Klinge jedes Bild.
        setzeSaberMovieDuelsRegeln(false);
        SaberWandKlinge ko;
        ko.laenge = 32.0F;
        int ojFunken = 0;
        for (int i = 0; i < 40; ++i) {
            ev.clear();
            in.saat = static_cast<std::uint64_t>(i + 7000);
            (void)saberWandTakt(ko, fig, in, w, ev);
            for (const auto& e : ev) { ojFunken += e.art == SaberWandEreignis::Art::Funken ? 1 : 0; }
        }
        pruefe(ojFunken == 40, "OpenJK: eine Klinge spruehrt jedes Bild: " + std::to_string(ojFunken));
        setzeSaberMovieDuelsRegeln(true);
    }
    // --- .sab ---------------------------------------------------------------
    {
        SaberKontaktWerte w;
        pruefe(saberKontaktSchluessel(w, "blocksound2", "sound/x.wav") && w.blockKlang[1] == "sound/x.wav",
               "blockSound2");
        pruefe(saberKontaktSchluessel(w, "blockeffect", "effects/saber/saber_block_blue.efx") &&
                   w.blockEffekt == "saber/saber_block_blue",
               "blockEffect ohne effects/ und .efx");
        pruefe(saberKontaktSchluessel(w, "nowallmarks", "1") && w.keineWandspuren, "noWallMarks 1");
        (void)saberKontaktSchluessel(w, "nowallmarks", "0");
        pruefe(w.keineWandspuren, "noWallMarks 0 loescht nicht (\"if (n)\")");
        pruefe(!saberKontaktSchluessel(w, "sabercolor", "red"), "fremder Schluessel");
    }
    // =====================================================================
    // rc601 saber: review - Befunde, je mit Probe (vorher rot, nachher gruen)
    // =====================================================================
    // R1: Wasser schreibt trail.oldPos[1], nicht [0] - CG_AddSaberBladeGo setzt
    // im Wasserzweig "i = 1" (cg_players.cpp:14364 MD, 6458 OJ), und das
    // folgende VectorCopy(trace.endpos, oldPos[i]) trifft den zweiten Platz.
    // Die naechste Brandspur an der Wand beginnt deshalb an der letzten
    // WANDstelle, nicht an der Wasserstelle.
    {
        SaberWandKlinge k;
        k.laenge = 32.0F;
        SaberWandFigur fig;
        SaberWandEingabe in;
        in.org[0] = 0.0F;
        in.org[2] = 24.0F;
        in.achse[0] = 1.0F;
        in.wurzelOrt[0] = -10.0F;
        in.wurzelOrt[2] = 24.0F;
        in.laengeMax = 32.0F;
        in.aktiv = true;
        std::vector<SaberWandEreignis> ev;
        in.ms = 8.0;
        (void)saberWandTakt(k, fig, in, wand(20.0F), ev);       // Wand bei x 20: oldPos[0] = (20,0,24)
        in.ms = 16.0;
        in.org[1] = 5.0F;
        (void)saberWandTakt(k, fig, in, wand(10.0F, kInhaltWasser), ev);   // Wasser bei x 10
        in.ms = 24.0;
        in.org[1] = 10.0F;
        ev.clear();
        (void)saberWandTakt(k, fig, in, wand(15.0F), ev);       // Wand bei x 15
        bool gefunden = false;
        bool vonWand = false;
        for (const SaberWandEreignis& e : ev) {
            if (e.art == SaberWandEreignis::Art::Spur) {
                gefunden = true;
                vonWand = nah(e.von[0], 20.0F) && nah(e.von[1], 0.0F);
            }
        }
        pruefe(gefunden && vonWand, "R1: nach Wasser beginnt die Brandspur an der letzten Wandstelle (oldPos[0])");
    }
    // R2: der Wassertest des Servers fragt renderInfo.muzzlePoint - die Lage von
    // saber[0].blade[0] (CG_AddSaberBladeGo schreibt sie nur fuer 0/0,
    // cg_players.cpp:14207 MD) -, nicht die Klinge, die gerade spurt
    // (WP_SaberDamageTrace, wp_saber.cpp:8178 MD, 4537 OJ).
    {
        std::vector<SaberFigur> f = {kinoFigur(0, 0.0F, 0.0F), kinoFigur(1, 40.0F, 0.0F)};
        f[1].dual = true;
        f[1].schwert[1] = f[1].schwert[0];
        // Das erste Schwert von B steht trocken still daneben (WP_SabersDistance
        // misst nur saber[0].blade[0]: 30 < (8+2)*4), das zweite schlaegt.
        const auto setze = [&f](float z) {
            lage(f[0].schwert[0].klinge[0], 0.0F, -20.0F, 40.0F, 0.0F, 1.0F, 0.0F);
            lage(f[1].schwert[0].klinge[0], 30.0F, 0.0F, 10.0F, 0.0F, 0.0F, 1.0F);
            lage(f[1].schwert[1].klinge[0], 20.0F, 0.0F, z, -1.0F, 0.0F, 0.0F);
        };
        const SaberSpurFn wasser = wand(19.0F, kInhaltWasser);   // x 19..23: der Griff von B2 steht darin
        std::vector<SaberKlirren> aus;
        setze(60.0F);
        saberServerTakt(f, 450.0, wasser, aus);
        aus.clear();
        setze(30.0F);
        saberServerTakt(f, 500.0, wasser, aus);
        bool vonB = false;
        for (const SaberKlirren& k : aus) { vonB = vonB || (k.figur == 1 && k.schwert == 1); }
        pruefe(vonB, "R2: Wassertest an saber[0].blade[0] - das zweite Schwert im Wasser klirrt trotzdem");
    }
    // R3: das Einfahren bleibt unter null stehen (kein Kappen auf 0,
    // cg_players.cpp:16233 MD, 7333 OJ); das naechste Zuenden waechst von dort.
    {
        SaberWandKlinge k;
        k.laenge = 1.0F;
        SaberWandFigur fig;
        SaberWandEingabe in;
        in.achse[0] = 1.0F;
        in.laengeMax = 40.0F;   // Schritt 40/10*8/100 = 0,32
        std::vector<SaberWandEreignis> ev;
        in.aktiv = false;
        for (int i = 0; i < 5; ++i) { (void)saberWandTakt(k, fig, in, SaberSpurFn{}, ev); }
        const float unten = k.laenge;   // 1 - 4*0,32 = -0,28
        in.aktiv = true;
        const SaberWandBild b = saberWandTakt(k, fig, in, SaberSpurFn{}, ev);
        pruefe(nah(unten, -0.28F, 0.001F) && nah(k.laenge, 0.04F, 0.001F) && b.gezeichnet,
               "R3: eingefahren -0,28 (" + std::to_string(unten) + "), gezuendet 0,04 (" + std::to_string(k.laenge) + ")");
    }
    // R4: der Spieler (s.number 0) bekommt mit g_saberAutoBlocking "1" (g_main.cpp:662 OJ,
    // :785 MD) und BLK_WIDE den Kasten um den Koerper - in beiden Regelwerken
    // (wp_saber.cpp:7993 OJ, :23719 MD) - und sonst mindestens 16.
    for (int md = 0; md < 2; ++md) {
        setzeSaberMovieDuelsRegeln(md != 0);
        SaberFigur p = kinoFigur(0, 100.0F, 0.0F);
        p.npc = false;
        p.verhalten.clear();
        p.gier = 90.0F;
        lage(p.schwert[0].klinge[0], 100.0F, 10.0F, 30.0F, 0.0F, 0.0F, 1.0F);
        saberKastenSetzen(p);
        pruefe(p.kasten.fest && nah(p.kasten.ort[0], 100.0F) && nah(p.kasten.ort[1], 12.0F) &&
                   nah(p.kasten.mins[0], -24.0F) && nah(p.kasten.maxs[2], 48.0F),
               std::string("R4: Spieler mit Klinge - Kasten um den Koerper, 12 vor ihm (") + (md ? "MD" : "OJ") + ")");
    }
    setzeSaberMovieDuelsRegeln(true);
    // R5: PM_SuperBreakLoseAnim im Oberkoerper: kein Schwertkasten (WP_SaberUpdate,
    // wp_saber.cpp:7936 OJ, :23638 MD) und keine Schadensspur (WP_SabersDamageTrace,
    // :12698 MD, :5683 OJ).
    {
        std::vector<SaberFigur> f = {kinoFigur(0, 0.0F, 0.0F), kinoFigur(1, 40.0F, 0.0F)};
        f[1].torsoAnim = "BOTH_LK_S_S_S_SB_1_L";
        const std::vector<SaberKlirren> k = durchSchlag(f);
        SaberFigur g = f[1];
        lage(g.schwert[0].klinge[0], 20.0F, 0.0F, 30.0F, -1.0F, 0.0F, 0.0F);
        saberKastenSetzen(g);
        pruefe(k.empty() && !g.kasten.fest, "R5: PM_SuperBreakLoseAnim - kein Kasten, keine Spur (" +
                                                std::to_string(k.size()) + " Klirren)");
    }
    // rc601 saber review 2b: Zuenden nach Kampfstil. Movie Duels: SS_FAST
    // lengthMax/5, alle anderen /10 (cg_players.cpp:16578-16620 MD, cg_ignitionSpeed
    // und cg_ignitionSpeedstaff "1", cg_main.cpp:711); OpenJK immer /10
    // (cg_players.cpp:7377 OJ). PM_SuperBreakWinAnim: sofort volle Laenge (beide).
    {
        const auto ersterSchritt = [](bool md, int stil, bool sieg) {
            setzeSaberMovieDuelsRegeln(md);
            SaberWandKlinge k;
            SaberWandFigur fig;
            SaberWandEingabe in;
            in.achse[0] = 1.0F;
            in.laengeMax = 40.0F;
            in.aktiv = true;
            in.stil = stil;
            in.superBruchSieg = sieg;
            std::vector<SaberWandEreignis> ev;
            (void)saberWandTakt(k, fig, in, SaberSpurFn{}, ev);
            setzeSaberMovieDuelsRegeln(true);
            return k.laenge;
        };
        pruefe(nah(ersterSchritt(true, 1, false), 0.64F, 0.0001F), "2b: MD SS_FAST zuendet mit lengthMax/5 (0,64 je Bild)");
        pruefe(nah(ersterSchritt(true, 2, false), 0.32F, 0.0001F) && nah(ersterSchritt(true, 7, false), 0.32F, 0.0001F),
               "2b: MD SS_MEDIUM/SS_STAFF mit lengthMax/10");
        pruefe(nah(ersterSchritt(false, 1, false), 0.32F, 0.0001F), "2b: OpenJK auch bei SS_FAST lengthMax/10");
        pruefe(nah(ersterSchritt(true, 2, true), 40.0F) && nah(ersterSchritt(false, 2, true), 40.0F),
               "2b: PM_SuperBreakWinAnim - sofort volle Laenge");
        // Einfahren bleibt in beiden Regelwerken /10 (cg_players.cpp:16486 MD).
        setzeSaberMovieDuelsRegeln(true);
        SaberWandKlinge k;
        k.laenge = 40.0F;
        SaberWandFigur fig;
        SaberWandEingabe in;
        in.achse[0] = 1.0F;
        in.laengeMax = 40.0F;
        in.stil = 1;
        std::vector<SaberWandEreignis> ev;
        (void)saberWandTakt(k, fig, in, SaberSpurFn{}, ev);
        pruefe(nah(k.laenge, 39.68F, 0.0001F), "2b: MD SS_FAST faehrt mit lengthMax/10 ein");
    }
    pruefe(nah(saberZeichenLaenge(0.1F, 32.0F), 8.1F) && nah(saberZeichenLaenge(30.0F, 32.0F), 32.0F) &&
               nah(saberZeichenLaenge(32.0F, 32.0F), 32.0F),
           "Zeichenlaenge: L+8, hoechstens lengthMax");
    std::printf("%d Fehler\n", g_fehler);
    return g_fehler == 0 ? 0 : 1;
}
