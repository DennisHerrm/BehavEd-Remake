// rofftest.cpp - Proben fuer die ROFF-Bahnen (bhed/roff.h)
//
// Jede Datei wird hier im Speicher gebaut - Byte fuer Byte nach den
// Strukturen in code/game/g_roff.h - und dann gelesen, abgespielt und mit
// Zahlen verglichen, die sich aus g_roff.cpp von Hand nachrechnen lassen.
// Dazu die Szene (play in einem affect-Block, Mover und Figur) und der
// ICARUS-Nachbau (dowait auf eine Bahn).
#include "bhed/ablauf.h"
#include "bhed/bsp.h"
#include "bhed/roff.h"
#include "bhed/scene.h"
#include "bhed/script.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
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

bool nah(float a, float b, float eps = 1e-3F) { return std::fabs(a - b) <= eps; }

// --- Dateien bauen ----------------------------------------------------------

void putI32(std::string& d, std::int32_t v) {
    std::uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(u));
    for (int i = 0; i < 4; ++i) {
        d.push_back(static_cast<char>((u >> (8U * static_cast<unsigned>(i))) & 0xFFU));
    }
}

void putF32(std::string& d, float v) {
    std::uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(u));
    for (int i = 0; i < 4; ++i) {
        d.push_back(static_cast<char>((u >> (8U * static_cast<unsigned>(i))) & 0xFFU));
    }
}

struct Bild {
    float o[3];
    float w[3];
    int ersteNotiz = -1;
    int notizen = 0;
};

// roff_hdr_t + move_rotate_t
std::string fassung1(float anzahl, const std::vector<Bild>& bilder) {
    std::string d = "ROFF";
    putI32(d, 1);
    putF32(d, anzahl);
    for (const Bild& b : bilder) {
        for (float v : b.o) { putF32(d, v); }
        for (float v : b.w) { putF32(d, v); }
    }
    return d;
}

// roff_hdr2_t + move_rotate2_t + Notizen
std::string fassung2(int bildMs, const std::vector<Bild>& bilder, const std::vector<std::string>& notizen,
                     int anzahl = -1, int notizAnzahl = -1) {
    std::string d = "ROFF";
    putI32(d, 2);
    putI32(d, anzahl >= 0 ? anzahl : static_cast<int>(bilder.size()));
    putI32(d, bildMs);
    putI32(d, notizAnzahl >= 0 ? notizAnzahl : static_cast<int>(notizen.size()));
    for (const Bild& b : bilder) {
        for (float v : b.o) { putF32(d, v); }
        for (float v : b.w) { putF32(d, v); }
        putI32(d, b.ersteNotiz);
        putI32(d, b.notizen);
    }
    for (const std::string& n : notizen) {
        d += n;
        d.push_back('\0');
    }
    return d;
}

// Drei Bilder, Fassung 1: je +10/+20/+30 in x, je +5 Grad Gieren.
std::string beispiel1() {
    return fassung1(3.0F, {Bild{{10, 0, 0}, {0, 5, 0}}, Bild{{20, 0, 0}, {0, 5, 0}}, Bild{{30, 0, 0}, {0, 5, 0}}});
}

// --- 1. Fassung 1 -----------------------------------------------------------
void probeFassung1() {
    Roff r;
    std::string grund;
    pruefe(leseRoff(beispiel1(), r, &grund), "Fassung 1 liest sich");
    pruefe(r.fassung == 1 && r.bilder.size() == 3, "Fassung 1: drei Bilder");
    pruefe(r.bildMs == 100 && r.lerp == 10, "Fassung 1: fest 100 ms, mLerp 10 (g_roff.cpp:329)");
    // Erstes Bild im Spielbild NACH dem Befehl, dann alle 100 ms.
    pruefe(roffBildZeitMs(r, 0) == 50.0 && roffBildZeitMs(r, 1) == 150.0 && roffBildZeitMs(r, 2) == 250.0,
           "Zeitplan 50/150/250 ms");
    pruefe(roffLaufzeitMs(r) == 250.0, "Aufgabe fertig mit dem letzten Bild (250 ms)");

    const float start[3] = {100.0F, 200.0F, 300.0F};
    const float winkel[3] = {0.0F, 90.0F, 0.0F};
    const RoffBahn b = roffBahn(r, start, winkel);
    pruefe(b.stand.size() == 4, "Bahn: vier Staende");

    // Mover (ohne client): TR_LINEAR ab pos1 mit Versatz * mLerp.
    const auto mx = [&](double ms) { return roffStand(r, b, ms, false).ort[0]; };
    pruefe(nah(mx(0.0), 100.0F) && nah(mx(49.0), 100.0F), "Mover: vor dem ersten Bild am Start");
    pruefe(nah(mx(50.0), 100.0F), "Mover: Bild 0 beginnt bei 50 ms");
    pruefe(nah(mx(100.0), 105.0F), "Mover: 50 ms in Bild 0 = +5 (10 * 10 * 0,05)");
    pruefe(nah(mx(150.0), 110.0F) && nah(mx(200.0), 120.0F), "Mover: Bild 1 ab 110");
    // Das letzte Bild: trBase = pos1 VOR dem Versatz, trDelta sofort null.
    pruefe(nah(mx(250.0), 130.0F) && nah(mx(5000.0), 130.0F), "Mover: letzter Versatz wird nie sichtbar (130, nicht 160)");
    pruefe(nah(roffStand(r, b, 100.0, false).winkel[1], 92.5F), "Mover: Gieren laeuft mit (92,5)");
    pruefe(nah(roffEndstand(r, b, false).ort[0], 130.0F) && nah(roffEndstand(r, b, false).winkel[1], 100.0F),
           "Mover: Endstand 130 / 100 Grad");

    // Figur (client): Spruenge je Bild, geglaettet zwischen 50-ms-Schnappschuessen.
    const auto fx = [&](double ms) { return roffStand(r, b, ms, true).ort[0]; };
    pruefe(nah(fx(0.0), 100.0F), "Figur: beim Befehl am Start");
    pruefe(nah(fx(25.0), 105.0F), "Figur: zwischen Schnappschuss 0 und 50 halb zu +10");
    pruefe(nah(fx(75.0), 110.0F), "Figur: 50..100 kein neues Bild");
    pruefe(nah(fx(125.0), 120.0F), "Figur: 100..150 halb zu +30");
    pruefe(nah(fx(300.0), 160.0F), "Figur: letzter Versatz WIRD angewandt (160)");
    pruefe(nah(roffEndstand(r, b, true).ort[0], 160.0F) && nah(roffEndstand(r, b, true).winkel[1], 105.0F),
           "Figur: Endstand 160 / 105 Grad");
}

// --- 2. Fassung 2 mit Notizen -----------------------------------------------
void probeFassung2() {
    std::vector<Bild> bilder = {Bild{{1, 0, 0}, {0, 0, 0}}, Bild{{2, 0, 0}, {0, 0, 0}},
                                Bild{{3, 0, 0}, {0, 0, 0}, 0, 1}, Bild{{4, 0, 0}, {0, 0, 0}, 1, 1}};
    const std::string d = fassung2(50, bilder, {"effect effects/explosion1.efx 10+0+64 0-90-0", "sound sound/x.wav"});
    Roff r;
    std::string grund;
    pruefe(leseRoff(d, r, &grund), "Fassung 2 liest sich");
    pruefe(r.fassung == 2 && r.bilder.size() == 4 && r.bildMs == 50 && r.lerp == 20, "Fassung 2: 4 Bilder zu 50 ms, mLerp 20");
    pruefe(r.notizen.size() == 2 && r.notizen[1] == "sound sound/x.wav", "Fassung 2: zwei Notizen");
    pruefe(roffLaufzeitMs(r) == 200.0, "Fassung 2: fertig bei 200 ms");
    const float null3[3] = {0.0F, 0.0F, 0.0F};
    const RoffBahn b = roffBahn(r, null3, null3);
    pruefe(nah(roffStand(r, b, 75.0, false).ort[0], 0.5F), "Fassung 2 Mover: 25 ms in Bild 0 = 0,5");
    pruefe(nah(roffStand(r, b, 100.0, false).ort[0], 1.0F), "Fassung 2 Mover: Bild 1 ab 1");
    pruefe(nah(roffStand(r, b, 125.0, false).ort[0], 2.0F), "Fassung 2 Mover: 25 ms in Bild 1 = 2");
    pruefe(nah(roffStand(r, b, 999.0, false).ort[0], 6.0F), "Fassung 2 Mover: Ende bei 6 (ohne die letzte 4)");

    const std::vector<RoffAusloesung> a = roffAusloesungen(r);
    pruefe(a.size() == 2 && a[0].bild == 2 && a[0].ms == 150.0 && a[1].bild == 3 && a[1].ms == 200.0,
           "Notizen in Bild 2 (150 ms) und 3 (200 ms)");
    if (a.size() == 2) {
        const RoffNotizBefehl e = zerlegeRoffNotiz(r.notizen[a[0].notiz]);
        pruefe(e.art == RoffNotizBefehl::Art::Effekt && e.datei == "explosion1.efx", "effect: \"effects/\" faellt weg");
        pruefe(nah(e.versatz[0], 10.0F) && nah(e.versatz[2], 64.0F), "effect: Versatz 10+0+64");
        pruefe(e.eigeneWinkel && nah(e.winkel[1], 90.0F), "effect: Winkel 0-90-0");
        // Mover: der Rueckruf sieht trBase = Stand vor Bild k-1 = 1.
        const RoffStand s = roffStandVorBild(r, b, a[0].bild, false);
        pruefe(nah(s.ort[0], 1.0F), "Notiz am Mover: Stand vor Bild k-1");
        pruefe(nah(roffStandVorBild(r, b, a[0].bild, true).ort[0], 3.0F), "Notiz an Figur: Stand vor Bild k");
        float ort[3];
        float w[3];
        roffEffektOrt(e, s.ort, s.winkel, ort, w);
        // Gieren 90: vorn = +y, oben = +z.
        pruefe(nah(ort[0], 1.0F) && nah(ort[1], 10.0F) && nah(ort[2], 64.0F), "Effektort: Basis + vorn*10 + oben*64");
        const RoffNotizBefehl k = zerlegeRoffNotiz(r.notizen[a[1].notiz]);
        pruefe(k.art == RoffNotizBefehl::Art::Klang && k.datei == "sound/x.wav", "sound: Datei");
    }
}

// --- 3. Notizen wie G_RoffNotetrackCallback ---------------------------------
void probeNotizen() {
    using A = RoffNotizBefehl::Art;
    {
        const RoffNotizBefehl b = zerlegeRoffNotiz("effect /effects/sparks.efx");
        pruefe(b.art == A::Effekt && b.datei == "sparks.efx" && !b.eigeneWinkel, "effect ohne Versatz, fuehrendes /");
    }
    {
        const RoffNotizBefehl b = zerlegeRoffNotiz("effect fx/boom 0+0+64 90-0");
        pruefe(b.art == A::Effekt && b.datei == "fx/boom" && nah(b.versatz[2], 64.0F) && !b.eigeneWinkel,
               "effect: zwei Winkel reichen nicht - Winkel der Entity");
    }
    {
        const RoffNotizBefehl b = zerlegeRoffNotiz("effect fx/boom 0+0+64 -90-0-0");
        pruefe(b.art == A::Effekt && !b.eigeneWinkel, "effect: negativer Winkel geht nicht ('-' trennt)");
    }
    {
        const RoffNotizBefehl b = zerlegeRoffNotiz("effect fx/boom 5+6");
        pruefe(b.art == A::Effekt && nah(b.versatz[0], 0.0F) && !b.eigeneWinkel, "effect: Versatz unvollstaendig - null");
    }
    {
        const RoffNotizBefehl b = zerlegeRoffNotiz("effect effects");
        pruefe(b.art == A::Keine, "effect: nur \"effects\" - leerer Name, nichts");
    }
    {
        const RoffNotizBefehl b = zerlegeRoffNotiz("fov 69.78");
        pruefe(b.art == A::Unbekannt && b.typ == "fov", "fov (Kamera-ROFF): unbekannt fuer G_Roff");
    }
    pruefe(zerlegeRoffNotiz("cut").art == A::Keine, "cut ohne Argument: nichts");
    pruefe(zerlegeRoffNotiz("effect ").art == A::Keine, "effect ohne Datei: nichts");
    pruefe(zerlegeRoffNotiz("sound snd\r\n").datei == "snd", "Zeilenenden fallen aus dem Argument");
}

// --- 4. Kaputte Dateien -----------------------------------------------------
void probeKaputt() {
    Roff r;
    std::string grund;
    pruefe(!leseRoff("ROFF", r, &grund), "4 Byte: abgelehnt");
    pruefe(!leseRoff(std::string("RIFF\x01\0\0\0\0\0\x40\x40", 12), r, &grund), "falsche Kennung: abgelehnt");
    pruefe(!leseRoff(fassung2(50, {}, {}, 0), r, &grund), "Fassung 2 mit 0 Bildern: abgelehnt (G_ValidRoff)");
    pruefe(!leseRoff(fassung2(50, {Bild{{0, 0, 0}, {0, 0, 0}}}, {}, 1000), r, &grund),
           "Fassung 2: Kopf nennt 1000 Bilder, Daten fuer 1 - abgelehnt");
    pruefe(!leseRoff(fassung2(0, {Bild{{0, 0, 0}, {0, 0, 0}}}, {}), r, &grund), "Fassung 2 mit 0 ms: abgelehnt (1000/0)");
    pruefe(!leseRoff(fassung1(1.0e30F, {}), r, &grund), "Fassung 1 mit 1e30 Bildern: abgelehnt, ohne (int)-Ueberlauf");
    pruefe(!leseRoff(fassung1(-3.0F, {}), r, &grund), "Fassung 1 mit negativer Anzahl: abgelehnt");
    {
        std::string d = fassung1(1.0F, {Bild{{1, 0, 0}, {0, 0, 0}}});
        d[4] = 3;
        pruefe(!leseRoff(d, r, &grund), "Fassung 3: abgelehnt");
    }
    {
        std::string d = fassung2(50, {Bild{{1, 0, 0}, {0, 0, 0}, 0, 1}}, {"sound x"});
        d.pop_back();   // Endnull weg
        pruefe(leseRoff(d, r, &grund) && r.notizen.size() == 1 && r.notizen[0] == "sound x" && !r.hinweise.empty(),
               "Notiz ohne Endnull: gelesen, gemeldet");
    }
    {
        const std::string d = fassung2(50, {Bild{{1, 0, 0}, {0, 0, 0}, 3, 1}}, {"sound x"}, -1, 5);
        pruefe(leseRoff(d, r, &grund) && r.notizen.size() == 1 && !r.hinweise.empty(), "5 Notizen genannt, 1 da: gemeldet");
        pruefe(roffAusloesungen(r).empty(), "mStartNote ausserhalb der Liste: keine Ausloesung");
    }
    {
        const std::string d = fassung2(42, {Bild{{1, 0, 0}, {0, 0, 0}}, Bild{{1, 0, 0}, {0, 0, 0}}}, {});
        pruefe(leseRoff(d, r, &grund) && r.lerp == 23 && !r.hinweise.empty(), "42 ms (md_arena/roll): mLerp 23, gemeldet");
        pruefe(roffBildZeitMs(r, 1) == 100.0, "42 ms: trotzdem ein Bild je Spielbild");
    }
    {
        const std::string d = fassung2(66, {Bild{{1, 0, 0}, {0, 0, 0}}, Bild{{1, 0, 0}, {0, 0, 0}}}, {});
        pruefe(leseRoff(d, r, &grund) && roffBildZeitMs(r, 1) == 150.0, "66 ms: nur jedes zweite Spielbild");
    }
    {
        const std::string d = fassung1(0.5F, {});
        pruefe(leseRoff(d, r, &grund) && r.bilder.empty() && roffLaufzeitMs(r) == 50.0,
               "Fassung 1 mit 0,5 Bildern: gueltig, keine Bewegung, fertig im ersten Spielbild");
        const float n3[3] = {1.0F, 2.0F, 3.0F};
        const RoffBahn b = roffBahn(r, n3, n3);
        pruefe(nah(roffStand(r, b, 500.0, false).ort[1], 2.0F), "ohne Bilder: bleibt am Start");
    }
    // Zufall und Verstuemmelung: darf nichts tun ausser ablehnen oder lesen.
    std::uint32_t z = 12345U;
    const auto zufall = [&z]() {
        z = z * 1664525U + 1013904223U;
        return z >> 8U;
    };
    const std::string gut = fassung2(50, {Bild{{1, 2, 3}, {4, 5, 6}, 0, 1}, Bild{{1, 2, 3}, {4, 5, 6}}}, {"effect a 1+2+3 4-5-6"});
    int gelesen = 0;
    for (int i = 0; i < 3000; ++i) {
        std::string d = gut;
        if (i % 3 == 0) {
            d.resize(zufall() % (gut.size() + 1U));
        } else {
            for (int k = 0; k < 4; ++k) {
                d[zufall() % d.size()] = static_cast<char>(zufall() & 0xFFU);
            }
        }
        Roff x;
        if (leseRoff(d, x, nullptr)) {
            ++gelesen;
            const float n3[3] = {0.0F, 0.0F, 0.0F};
            const RoffBahn b = roffBahn(x, n3, n3);
            (void)roffStand(x, b, 123.0, false);
            (void)roffStand(x, b, 123.0, true);
            for (const RoffAusloesung& a : roffAusloesungen(x)) {
                (void)zerlegeRoffNotiz(x.notizen[a.notiz]);
            }
        }
    }
    std::printf("   (%d von 3000 verstuemmelten Dateien noch lesbar)\n", gelesen);
    pruefe(true, "verstuemmelte Dateien ohne Absturz");
}

// --- 5. Szene: play in affect-Bloecken --------------------------------------
Script lies(const std::string& text) {
    Script s;
    std::vector<Diag> d;
    (void)readScript(text, s, d);
    return s;
}

void probeSzene() {
    MapData karte;
    MapEntity schiff;
    schiff.classname = "func_static";
    schiff.targetname = "schiff";
    schiff.origin = "100 200 300";
    schiff.keys = {{"classname", "func_static"}, {"targetname", "schiff"}, {"model", "*3"}, {"origin", "100 200 300"}};
    MapEntity sp;
    sp.classname = "NPC_spawner";
    sp.targetname = "sp";
    sp.origin = "0 0 0";
    sp.keys = {{"classname", "NPC_spawner"}, {"NPC_targetname", "bob"}, {"NPC_type", "stormtrooper"}, {"origin", "0 0 0"}};
    karte.entities = {schiff, sp};

    auto bahn = std::make_shared<Roff>();
    std::string grund;
    pruefe(leseRoff(beispiel1(), *bahn, &grund), "Szene: Bahn gelesen");
    SzenenHilfe hilfe;
    hilfe.roff = [bahn](const std::string& n) -> std::shared_ptr<const Roff> {
        return (n == "test/bahn") ? bahn : nullptr;
    };
    const Script s = lies(
        "affect ( \"schiff\", FLUSH )\n{\n\twait ( 1000.000 );\n\tplay ( \"PLAY_ROFF\", \"test/bahn\" );\n"
        "\twait ( 500.000 );\n\tmove ( < 0 0 0 >, 1000.000 );\n}\n"
        "affect ( \"bob\", FLUSH )\n{\n\twait ( 1000.000 );\n\tplay ( \"PLAY_ROFF\", \"test/bahn\" );\n}\n"
        "affect ( \"leer\", FLUSH )\n{\n\tplay ( \"PLAY_ROFF\", \"gibt/es/nicht\" );\n}\n");
    const Scene sc = buildScene(s, karte, &hilfe);
    const Actor* a = sc.find("schiff");
    pruefe(a != nullptr && a->brushModel == 3, "Szene: Schiff ist ein Brush-Modell");
    if (a != nullptr) {
        bool hatRoff = false;
        for (const ActorStep& st : a->steps) {
            if (st.kind == ActorStep::Kind::Roff) {
                hatRoff = true;
                pruefe(st.startMs == 1000.0 && st.endMs == 1250.0 && !st.roffFigur, "Szene: Roff-Schritt 1000..1250, Mover");
            }
        }
        pruefe(hatRoff, "Szene: play wird zum Roff-Schritt");
        pruefe(nah(a->at(900.0).pos[0], 100.0F), "Szene: vor dem play am Kartenort");
        pruefe(nah(a->at(1100.0).pos[0], 105.0F), "Szene: 100 ms nach play = +5");
        pruefe(nah(a->at(1100.0).angles[1], 2.5F), "Szene: Gieren laeuft ab 0");
        pruefe(nah(a->at(1400.0).pos[0], 130.0F) && nah(a->at(1400.0).pos[1], 200.0F), "Szene: steht danach bei 130");
        // move danach: GLEICHFOERMIG, weil G_Roff alt_fire gesetzt hat.
        const ActorState m = a->at(2000.0);
        pruefe(nah(m.pos[0], 65.0F, 0.01F) && nah(m.pos[2], 150.0F, 0.01F),
               "Szene: move nach ROFF faehrt linear (alt_fire bleibt, g_roff.cpp:597)");
    }
    const Actor* f = sc.find("bob");
    pruefe(f != nullptr && !f->npcType.empty(), "Szene: bob ist eine Figur");
    if (f != nullptr) {
        pruefe(nah(f->at(1125.0).pos[0], 20.0F), "Szene: Figur geglaettet zwischen Schnappschuessen (+20)");
        pruefe(nah(f->at(3000.0).pos[0], 60.0F), "Szene: Figur bekommt auch den letzten Versatz (60)");
        pruefe(!f->at(1125.0).moving, "Szene: Figur geht dabei nicht (keine Laufanimation)");
    }
    const Actor* l = sc.find("leer");
    bool leerRoff = false;
    if (l != nullptr) {
        for (const ActorStep& st : l->steps) { leerRoff = leerRoff || st.kind == ActorStep::Kind::Roff; }
    }
    pruefe(!leerRoff, "Szene: fehlende Datei - kein Schritt, steht still");
    // So schreibt BehavEd die Zeile (mit Typsatz-Kommentar) - das erste
    // Argument muss trotzdem "PLAY_ROFF" heissen.
    {
        const Script z = lies("play ( /*@PLAY_TYPES*/ \"PLAY_ROFF\", \"kor2/roffs/lsend_ceiling01\" );\n");
        pruefe(!z.nodes.empty() && z.nodes[0].args.size() == 2 && z.nodes[0].args[0].text == "PLAY_ROFF" &&
                   z.nodes[0].args[1].text == "kor2/roffs/lsend_ceiling01",
               "Szene: play mit Typsatz-Kommentar liest sich als PLAY_ROFF");
        pruefe(roffPfad("kor2\\roffs\\x") == "scripts/kor2/roffs/x.rof", "roffPfad: scripts/<name>.rof");
    }
    pruefe(sc.durationMs >= 2500.0, "Szene: Dauer umfasst die Bahnen");

    // Notizen in der Szene.
    auto mitNotiz = std::make_shared<Roff>();
    const std::string d = fassung2(50, {Bild{{8, 0, 0}, {0, 0, 0}}, Bild{{8, 0, 0}, {0, 0, 0}, 0, 1}, Bild{{8, 0, 0}, {0, 0, 0}}},
                                   {"sound sound/boom.wav"});
    pruefe(leseRoff(d, *mitNotiz, &grund), "Szene: Bahn mit Notiz gelesen");
    SzenenHilfe h2;
    h2.roff = [mitNotiz](const std::string&) -> std::shared_ptr<const Roff> { return mitNotiz; };
    const Scene s2 = buildScene(lies("affect ( \"schiff\", FLUSH )\n{\n\twait ( 200.000 );\n\tplay ( \"PLAY_ROFF\", \"x\" );\n}\n"),
                                karte, &h2);
    pruefe(s2.roffEreignisse.size() == 1, "Szene: eine Notiz ausgeloest");
    if (s2.roffEreignisse.size() == 1) {
        const RoffEreignis& e = s2.roffEreignisse[0];
        pruefe(e.ms == 300.0 && e.figur == "schiff" && e.befehl.art == RoffNotizBefehl::Art::Klang &&
                   e.befehl.datei == "sound/boom.wav",
               "Szene: Klang bei 300 ms (200 + Bild 1 bei 100)");
        pruefe(nah(e.ort[0], 100.0F), "Szene: Klang am Mover-Stand vor Bild 0 (trBase)");
    }
}

// --- 6. Nachbau: dowait wartet auf die Bahn ---------------------------------
double zeitVon(const Ablauf& a, const std::string& spur, const std::string& arg) {
    for (const Node& n : a.flach.nodes) {
        if (n.name != "affect" || n.args.empty() || n.args[0].text != spur) {
            continue;
        }
        double t = 0.0;
        for (const Node& k : n.children) {
            if (k.name == "wait") {
                t += std::atof(k.args[0].text.c_str());
            } else if (k.name == "print" && !k.args.empty() && k.args[0].text == arg) {
                return t;
            }
        }
    }
    return -1.0;
}

void probeAblauf() {
    auto bahn = std::make_shared<Roff>();
    std::string grund;
    // 50 Bilder zu 50 ms: fertig bei 50 * 50 = 2500 ms.
    std::vector<Bild> bilder(50, Bild{{1, 0, 0}, {0, 0, 0}});
    pruefe(leseRoff(fassung2(50, bilder, {}), *bahn, &grund), "Nachbau: Bahn gelesen");
    pruefe(roffLaufzeitMs(*bahn) == 2500.0, "Nachbau: Laufzeit 2500 ms");
    const std::string text =
        "affect ( \"a\", FLUSH )\n{\n\ttask ( \"flug\" )\n\t{\n\t\tplay ( \"PLAY_ROFF\", \"x/bahn\" );\n\t}\n"
        "\tdowait ( \"flug\" );\n\tprint ( \"danach\" );\n}\n";
    const Script s = lies(text);
    AblaufWelt w;
    w.roffDauer = [bahn](const std::string& n) { return n == "x/bahn" ? roffLaufzeitMs(*bahn) : kRoffFehlt; };
    const Ablauf a = simuliereAblauf(s, "", w);
    const double t = zeitVon(a, "a", "danach");
    pruefe(t >= 2500.0 && t <= 2550.0, "Nachbau: dowait wartet bis zum letzten Bild (TID_MOVE_NAV)");

    AblaufWelt ohne;
    const Ablauf b = simuliereAblauf(s, "", ohne);
    pruefe(zeitVon(b, "a", "danach") == 0.0, "Nachbau: ohne Angabe sofort fertig (wie bisher)");

    AblaufWelt fehlt;
    fehlt.roffDauer = [](const std::string&) { return kRoffFehlt; };
    const Ablauf c = simuliereAblauf(s, "", fehlt);
    bool gemeldet = false;
    for (const std::string& h : c.hinweise) { gemeldet = gemeldet || h.find("fehlt") != std::string::npos; }
    pruefe(zeitVon(c, "a", "danach") < 0.0 && gemeldet, "Nachbau: fehlende Datei - dowait haengt wie im Spiel, gemeldet");

    // Ein zweites move/play auf TID_MOVE_NAV erledigt das erste (Q3_TaskIDSet).
    const Script s2 = lies(
        "affect ( \"a\", FLUSH )\n{\n\ttask ( \"flug\" )\n\t{\n\t\tplay ( \"PLAY_ROFF\", \"x/bahn\" );\n\t}\n"
        "\tdo ( \"flug\" );\n\twait ( 500.000 );\n\tmove ( < 0 0 0 >, 100.000 );\n\twait ( \"flug\" );\n\tprint ( \"danach\" );\n}\n");
    const Ablauf d = simuliereAblauf(s2, "", w);
    const double t2 = zeitVon(d, "a", "danach");
    pruefe(t2 >= 500.0 && t2 <= 700.0, "Nachbau: move ersetzt die Bahn im Fach TID_MOVE_NAV");
}

}  // namespace

int main() {
    probeFassung1();
    probeFassung2();
    probeNotizen();
    probeKaputt();
    probeSzene();
    probeAblauf();
    std::printf("%s\n", fehler == 0 ? "alle Proben bestanden" : "FEHLGESCHLAGEN");
    return fehler == 0 ? 0 : 1;
}
